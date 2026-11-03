/*
 * Multiple ISA bus and emulated i8259 pair unit tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/isa/isa.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "system/system.h"

#define TYPE_TEST_ISA_OWNER "test-isa-owner"
#define TYPE_TEST_ISA_DEVICE "test-isa-device"

typedef struct TestISADevice TestISADevice;
DECLARE_INSTANCE_CHECKER(TestISADevice, TEST_ISA_DEVICE, TYPE_TEST_ISA_DEVICE)

struct TestISADevice {
    ISADevice parent_obj;

    PortioList portio;
    qemu_irq resolved_irq;
};

/* isa-bus.c's legacy-only branches are not exercised by this host test. */
int vga_interface_type = VGA_NONE;
bool vga_interface_created;
const VMStateInfo vmstate_info_uint8;

const char *qdev_fw_name(DeviceState *dev)
{
    return object_get_typename(OBJECT(dev));
}

bool sysbus_realize_and_unref(SysBusDevice *dev, Error **errp)
{
    g_assert_not_reached();
    return false;
}

/* Minimal MemoryRegion and PortioList definitions for this unit test. */
void memory_region_init_io(MemoryRegion *mr, Object *owner,
                           const MemoryRegionOps *ops, void *opaque,
                           const char *name, uint64_t size)
{
    memset(mr, 0, sizeof(*mr));
    mr->owner = owner;
    mr->ops = ops;
    mr->opaque = opaque;
    mr->name = name;
    mr->size = int128_make64(size);
    mr->terminates = true;
}

void memory_region_add_subregion(MemoryRegion *mr, hwaddr offset,
                                 MemoryRegion *subregion)
{
    g_assert_null(subregion->container);
    subregion->container = mr;
    subregion->addr = offset;
}

void portio_list_init(PortioList *piolist, Object *owner,
                      const MemoryRegionPortio *callbacks, void *opaque,
                      const char *name)
{
    piolist->ports = callbacks;
    piolist->owner = owner;
    piolist->opaque = opaque;
    piolist->name = name;
}

void portio_list_add(PortioList *piolist, MemoryRegion *address_space,
                     uint32_t addr)
{
    piolist->address_space = address_space;
    piolist->addr = addr;
}

static uint32_t test_port_read(void *opaque, uint32_t address)
{
    return 0;
}

static void test_port_write(void *opaque, uint32_t address, uint32_t data)
{
}

static const MemoryRegionPortio test_portio[] = {
    { 0, 1, 1, .read = test_port_read, .write = test_port_write },
    PORTIO_END_OF_LIST(),
};

static void test_isa_device_realize(DeviceState *dev, Error **errp)
{
    TestISADevice *s = TEST_ISA_DEVICE(dev);
    ISADevice *isadev = ISA_DEVICE(dev);
    int ret;

    s->resolved_irq = isa_get_irq(isadev, 5);
    ret = isa_register_portio_list(isadev, &s->portio, 0x300, test_portio, s,
                                   "test-isa-portio");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not register test ISA ports");
    }
}

static void test_isa_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = test_isa_device_realize;
}

static const TypeInfo test_isa_owner_info = {
    .name = TYPE_TEST_ISA_OWNER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DeviceState),
};

static const TypeInfo test_isa_device_info = {
    .name = TYPE_TEST_ISA_DEVICE,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(TestISADevice),
    .class_init = test_isa_device_class_init,
};

static void test_isa_types_register(void)
{
    type_register_static(&test_isa_owner_info);
    type_register_static(&test_isa_device_info);
}

type_init(test_isa_types_register)

    static void test_init_machine(void)
{
    Object *machine;

    machine = object_property_add_new_container(object_get_root(), "machine");
    object_property_add_new_container(machine, "unattached");
}

static void test_memory_root_init(MemoryRegion *mr, const char *name)
{
    memset(mr, 0, sizeof(*mr));
    mr->name = name;
    mr->size = int128_make64(UINT16_MAX + 1ULL);
}

static void pic_write(MemoryRegion *mr, hwaddr addr, uint8_t value)
{
    g_assert_nonnull(mr->ops);
    g_assert_nonnull(mr->ops->write);
    mr->ops->write(mr->opaque, addr, value, 1);
}

static void program_pic_pair(PICCommonState *master, uint8_t slave_base)
{
    PICCommonState *slave = master->cascade_slave;

    g_assert_nonnull(slave);

    pic_write(&master->base_io, 0, 0x11);
    pic_write(&master->base_io, 1, 0x20);
    pic_write(&master->base_io, 1, 0x04);
    pic_write(&master->base_io, 1, 0x01);

    pic_write(&slave->base_io, 0, 0x11);
    pic_write(&slave->base_io, 1, slave_base);
    pic_write(&slave->base_io, 1, 0x02);
    pic_write(&slave->base_io, 1, 0x01);
}

static void parent_irq_set(void *opaque, int n, int level)
{
    int *parent_level = opaque;

    *parent_level = level;
}

static void test_multiple_isa_and_i8259_instances(void)
{
    static MemoryRegion default_mem;
    static MemoryRegion default_io;
    static MemoryRegion extra_mem;
    static MemoryRegion extra_io;
    static int default_parent_level;
    static int extra_parent_level;
    DeviceState *default_owner;
    DeviceState *extra_owner;
    ISABus *default_bus;
    ISABus *extra_bus;
    PICCommonState *default_master;
    PICCommonState *extra_master = NULL;
    TestISADevice *extra_dev;
    PortioList default_portio = { 0 };
    qemu_irq *default_irqs;
    qemu_irq *extra_irqs;
    qemu_irq default_parent_irq;
    qemu_irq extra_parent_irq;
    Error *err = NULL;

    test_memory_root_init(&default_mem, "default-isa-memory");
    test_memory_root_init(&default_io, "default-isa-io");
    test_memory_root_init(&extra_mem, "extra-isa-memory");
    test_memory_root_init(&extra_io, "extra-isa-io");

    default_owner = qdev_new(TYPE_TEST_ISA_OWNER);
    extra_owner = qdev_new(TYPE_TEST_ISA_OWNER);
    extra_bus = isa_bus_new_non_default(extra_owner, &extra_mem, &extra_io);
    g_assert_true(extra_bus->address_space == &extra_mem);
    g_assert_true(extra_bus->address_space_io == &extra_io);

    extra_parent_irq =
        qemu_allocate_irq(parent_irq_set, &extra_parent_level, 0);
    extra_irqs = i8259_init_pair(extra_bus, extra_parent_irq, &extra_master);
    g_assert_null(isa_pic);
    isa_bus_register_input_irqs(extra_bus, extra_irqs);

    extra_dev = TEST_ISA_DEVICE(isa_new(TYPE_TEST_ISA_DEVICE));
    isa_realize_and_unref(ISA_DEVICE(extra_dev), extra_bus, &error_abort);
    g_assert_true(extra_dev->resolved_irq == extra_irqs[5]);
    g_assert_true(extra_dev->portio.address_space == &extra_io);
    g_assert_true(isa_address_space(ISA_DEVICE(extra_dev)) == &extra_mem);
    g_assert_true(isa_address_space_io(ISA_DEVICE(extra_dev)) == &extra_io);

    default_bus =
        isa_bus_new(default_owner, &default_mem, &default_io, &error_abort);
    g_assert_true(default_bus->address_space == &default_mem);
    g_assert_true(default_bus->address_space_io == &default_io);
    g_assert_null(isa_bus_new(extra_owner, &extra_mem, &extra_io, &err));
    g_assert_nonnull(err);
    error_free(err);

    default_parent_irq =
        qemu_allocate_irq(parent_irq_set, &default_parent_level, 0);
    default_irqs = i8259_init(default_bus, default_parent_irq);
    default_master = isa_pic;

    g_assert_nonnull(default_master);
    g_assert_nonnull(extra_master);
    g_assert_true(default_master != extra_master);
    g_assert_true(isa_pic == default_master);
    g_assert_true(default_master->cascade_slave != extra_master->cascade_slave);

    g_assert_true(default_master->base_io.container == &default_io);
    g_assert_true(default_master->cascade_slave->base_io.container ==
                  &default_io);
    g_assert_true(extra_master->base_io.container == &extra_io);
    g_assert_true(extra_master->cascade_slave->base_io.container == &extra_io);

    isa_bus_register_input_irqs(default_bus, default_irqs);
    g_assert_true(isa_get_irq(NULL, 5) == default_irqs[5]);
    g_assert_cmpint(isa_register_portio_list(NULL, &default_portio, 0x310,
                                             test_portio, NULL,
                                             "default-test-portio"),
                    ==, 0);
    g_assert_true(default_portio.address_space == &default_io);

    program_pic_pair(default_master, 0x70);
    program_pic_pair(extra_master, 0x78);
    qemu_irq_pulse(default_irqs[10]);
    qemu_irq_pulse(extra_irqs[11]);
    g_assert_cmpint(default_parent_level, ==, 1);
    g_assert_cmpint(extra_parent_level, ==, 1);

    g_assert_cmphex(pic_read_irq(default_master), ==, 0x72);
    g_assert_cmphex(pic_read_irq(extra_master), ==, 0x7b);
    g_assert_cmpint(default_parent_level, ==, 0);
    g_assert_cmpint(extra_parent_level, ==, 0);
    g_assert_true(isa_pic == default_master);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    test_init_machine();

    g_test_add_func("/isa/i8259/multiple-instances",
                    test_multiple_isa_and_i8259_instances);

    return g_test_run();
}
