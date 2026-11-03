/*
 * IA-64 i2000 460GX integration test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/ia64/intel_460gx_dma.h"
#include "hw/ia64/intel_460gx_host.h"
#include "hw/ia64/intel_460gx_pid.h"
#include "hw/ia64/intel_460gx_root.h"
#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/qtest.h"

struct IA64I2000460GXTestState {
    DeviceState parent_obj;

    IA64I2000460GXTestLayout layout;
    MemoryRegion *ram;

    Intel460GXHostState *host;
    Intel460GXPIDState *pid;
    Intel460GXRootHostState *roots[IA64_I2000_460GX_TEST_ROOT_COUNT];
    Intel460GXDMA *dma[IA64_I2000_460GX_TEST_ROOT_COUNT];

    MemoryRegion root_mmio[IA64_I2000_460GX_TEST_ROOT_COUNT];
    MemoryRegion subword_cf8_io;
    MemoryRegion sparse_io;
    AddressSpace host_conf_as;
    AddressSpace host_data_as;
    AddressSpace root_io_as[IA64_I2000_460GX_TEST_ROOT_COUNT];

    uint64_t cf8_subdword_count;
    uint32_t legacy_pin;
    uint8_t subword_cf8_bytes[4];
    bool root_mmio_initialized[IA64_I2000_460GX_TEST_ROOT_COUNT];
    bool subword_cf8_mapped;
    bool host_spaces_initialized;
    bool root_io_initialized[IA64_I2000_460GX_TEST_ROOT_COUNT];
    bool system_regions_mapped;
};

typedef struct IA64I2000460GXQTestState {
    DeviceState parent_obj;

    IA64I2000460GXTestState *fixture;
    PCIDevice *probes[IA64_I2000_460GX_TEST_ROOT_COUNT];
    uint32_t legacy_pin;
} IA64I2000460GXQTestState;

#define IA64_I2000_460GX_QTEST(obj) \
    OBJECT_CHECK(IA64I2000460GXQTestState, (obj), \
                 TYPE_IA64_I2000_460GX_QTEST)

static bool fixture_system_range_is_free(uint64_t base, uint64_t size,
                                         const char *name, Error **errp)
{
    MemoryRegionSection section =
        memory_region_find(get_system_memory(), base, size);

    if (!section.mr) {
        return true;
    }

    error_setg(errp, "i2000 460GX test %s range overlaps system region '%s'",
               name, memory_region_name(section.mr));
    memory_region_unref(section.mr);
    return false;
}

static bool fixture_system_range_is_ram(IA64I2000460GXTestState *s,
                                        uint64_t base, uint64_t size,
                                        Error **errp)
{
    MemoryRegionSection section =
        memory_region_find(get_system_memory(), base, size);
    bool valid;

    valid = section.mr == s->ram &&
            section.offset_within_address_space == base &&
            int128_get64(section.size) == size &&
            section.offset_within_region == base - s->layout.ram.base &&
            !section.readonly;
    if (section.mr) {
        memory_region_unref(section.mr);
    }
    if (!valid) {
        error_setg(errp,
                   "i2000 460GX test requires its linked RAM to be flat at "
                   "[0x%" PRIx64 ", 0x%" PRIx64 ")",
                   base, base + size);
    }
    return valid;
}

static bool fixture_system_pib_is_present(uint64_t base, uint64_t size,
                                          Error **errp)
{
    MemoryRegionSection section =
        memory_region_find(get_system_memory(), base, size);
    bool valid = section.mr && section.offset_within_address_space == base &&
                 int128_get64(section.size) == size &&
                 !memory_region_is_ram(section.mr) &&
                 !memory_region_is_ram_device(section.mr) &&
                 !memory_region_is_rom(section.mr) && !section.readonly;

    if (section.mr) {
        memory_region_unref(section.mr);
    }
    if (!valid) {
        error_setg(errp,
                   "i2000 460GX test requires the parent board's non-RAM "
                   "shared PIB range "
                   "[0x%" PRIx64 ", 0x%" PRIx64 ")",
                   base, base + size);
    }
    return valid;
}

static bool fixture_validate_parent_resources(IA64I2000460GXTestState *s,
                                              Error **errp)
{
    unsigned i;

    if (!s->ram) {
        error_setg(errp, "i2000 460GX test requires the '%s' link",
                   IA64_I2000_460GX_TEST_PROP_RAM);
        return false;
    }
    if (!memory_region_is_ram(s->ram) || memory_region_is_ram_device(s->ram) ||
        memory_region_is_rom(s->ram) || memory_region_is_protected(s->ram) ||
        memory_region_size(s->ram) != s->layout.ram.size) {
        error_setg(errp, "i2000 460GX test requires exactly 2 GiB of ordinary "
                         "writable RAM");
        return false;
    }
    if (!fixture_system_range_is_ram(s, s->layout.ram.base, s->layout.ram.size,
                                     errp) ||
        !fixture_system_pib_is_present(s->layout.pib.base, s->layout.pib.size,
                                       errp) ||
        !fixture_system_range_is_free(s->layout.pid_envelope.base,
                                      s->layout.pid_envelope.size,
                                      "PID envelope", errp) ||
        !fixture_system_range_is_free(s->layout.legacy_io.base,
                                      s->layout.legacy_io.size,
                                      "sparse legacy-I/O", errp)) {
        return false;
    }

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        g_autofree char *name = g_strdup_printf("root %u MMIO32", i);

        if (!fixture_system_range_is_free(s->layout.roots[i].cpu_mmio32_base,
                                          s->layout.roots[i].mmio32_size, name,
                                          errp)) {
            return false;
        }
    }
    return true;
}

static hwaddr fixture_sparse_io_port(hwaddr encoded)
{
    hwaddr group = encoded >> 12;
    hwaddr low = encoded & 0xfff;

    return (group << 2) | (low & 3);
}

static MemTxResult fixture_address_space_read(AddressSpace *as, hwaddr addr,
                                              uint64_t *value, unsigned size,
                                              MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        *value = address_space_ldub(as, addr, attrs, &result);
        break;
    case 2:
        *value = address_space_lduw_le(as, addr, attrs, &result);
        break;
    case 4:
        *value = address_space_ldl_le(as, addr, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static MemTxResult fixture_address_space_write(AddressSpace *as, hwaddr addr,
                                               uint64_t value, unsigned size,
                                               MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        address_space_stb(as, addr, value, attrs, &result);
        break;
    case 2:
        address_space_stw_le(as, addr, value, attrs, &result);
        break;
    case 4:
        address_space_stl_le(as, addr, value, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static AddressSpace *fixture_root_io_for_port(IA64I2000460GXTestState *s,
                                              hwaddr port, unsigned size)
{
    unsigned i;

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        const IA64I2000460GXTestRoot *root = &s->layout.roots[i];
        uint64_t end = (uint64_t)root->io_base + root->io_size;

        if (port >= root->io_base && port < end && size <= end - port) {
            return &s->root_io_as[i];
        }
    }
    return NULL;
}

static MemTxResult fixture_sparse_io_read(void *opaque, hwaddr addr,
                                          uint64_t *value, unsigned size,
                                          MemTxAttrs attrs)
{
    IA64I2000460GXTestState *s = opaque;
    hwaddr port = fixture_sparse_io_port(addr);
    AddressSpace *as;

    if (port == IA64_I2000_460GX_TEST_CF8_PORT && size == 4) {
        return fixture_address_space_read(&s->host_conf_as, 0, value, size,
                                          attrs);
    }
    if (port >= IA64_I2000_460GX_TEST_CF8_PORT &&
        port < IA64_I2000_460GX_TEST_CFC_PORT &&
        size <= IA64_I2000_460GX_TEST_CFC_PORT - port) {
        s->cf8_subdword_count++;
        as = &s->root_io_as[s->layout.cf8_io_root];
        return fixture_address_space_read(as, port, value, size, attrs);
    }
    if (port >= IA64_I2000_460GX_TEST_CFC_PORT &&
        port < IA64_I2000_460GX_TEST_CFC_PORT + 4 &&
        size <= IA64_I2000_460GX_TEST_CFC_PORT + 4 - port) {
        return fixture_address_space_read(&s->host_data_as,
                                          port - IA64_I2000_460GX_TEST_CFC_PORT,
                                          value, size, attrs);
    }

    as = fixture_root_io_for_port(s, port, size);
    if (!as) {
        *value = UINT64_MAX;
        return MEMTX_DECODE_ERROR;
    }
    return fixture_address_space_read(as, port, value, size, attrs);
}

static MemTxResult fixture_sparse_io_write(void *opaque, hwaddr addr,
                                           uint64_t value, unsigned size,
                                           MemTxAttrs attrs)
{
    IA64I2000460GXTestState *s = opaque;
    hwaddr port = fixture_sparse_io_port(addr);
    AddressSpace *as;

    if (port == IA64_I2000_460GX_TEST_CF8_PORT && size == 4) {
        return fixture_address_space_write(&s->host_conf_as, 0, value, size,
                                           attrs);
    }
    if (port >= IA64_I2000_460GX_TEST_CF8_PORT &&
        port < IA64_I2000_460GX_TEST_CFC_PORT &&
        size <= IA64_I2000_460GX_TEST_CFC_PORT - port) {
        s->cf8_subdword_count++;
        as = &s->root_io_as[s->layout.cf8_io_root];
        return fixture_address_space_write(as, port, value, size, attrs);
    }
    if (port >= IA64_I2000_460GX_TEST_CFC_PORT &&
        port < IA64_I2000_460GX_TEST_CFC_PORT + 4 &&
        size <= IA64_I2000_460GX_TEST_CFC_PORT + 4 - port) {
        return fixture_address_space_write(
            &s->host_data_as, port - IA64_I2000_460GX_TEST_CFC_PORT, value,
            size, attrs);
    }

    as = fixture_root_io_for_port(s, port, size);
    return as ? fixture_address_space_write(as, port, value, size, attrs)
              : MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps fixture_sparse_io_ops = {
    .read_with_attrs = fixture_sparse_io_read,
    .write_with_attrs = fixture_sparse_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static uint64_t fixture_subword_cf8_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    IA64I2000460GXTestState *s = opaque;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)s->subword_cf8_bytes[addr + i] << (i * 8);
    }
    return value;
}

static void fixture_subword_cf8_write(void *opaque, hwaddr addr, uint64_t value,
                                      unsigned size)
{
    IA64I2000460GXTestState *s = opaque;
    unsigned i;

    for (i = 0; i < size; i++) {
        s->subword_cf8_bytes[addr + i] = value >> (i * 8);
    }
}

static bool fixture_subword_cf8_accepts(void *opaque, hwaddr addr,
                                        unsigned size, bool is_write,
                                        MemTxAttrs attrs)
{
    (void)opaque;
    (void)is_write;
    (void)attrs;
    return (size == 1 || size == 2) && addr < 4 && size <= 4 - addr;
}

static const MemoryRegionOps fixture_subword_cf8_ops = {
    .read = fixture_subword_cf8_read,
    .write = fixture_subword_cf8_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = true,
        .accepts = fixture_subword_cf8_accepts,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = true,
    },
};

static DeviceState *fixture_add_child(DeviceState *parent, const char *name,
                                      const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void fixture_remove_child(DeviceState **child)
{
    if (!*child) {
        return;
    }
    if (qdev_is_realized(*child)) {
        qdev_unrealize(*child);
    }
    object_unparent(OBJECT(*child));
    *child = NULL;
}

static void fixture_unmap_system(IA64I2000460GXTestState *s)
{
    unsigned i;

    if (!s->system_regions_mapped) {
        return;
    }

    memory_region_transaction_begin();
    memory_region_del_subregion(get_system_memory(), &s->sparse_io);
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        memory_region_del_subregion(get_system_memory(), &s->root_mmio[i]);
    }
    memory_region_del_subregion(
        get_system_memory(), sysbus_mmio_get_region(SYS_BUS_DEVICE(s->pid), 0));
    memory_region_transaction_commit();
    s->system_regions_mapped = false;
}

static bool fixture_destroy_dma(IA64I2000460GXTestState *s)
{
    Error *local_err = NULL;
    bool success = true;
    int i;

    for (i = IA64_I2000_460GX_TEST_ROOT_COUNT - 1; i >= 0; i--) {
        if (!s->dma[i]) {
            continue;
        }
        if (!intel_460gx_dma_destroy(s->dma[i], &local_err)) {
            error_report_err(local_err);
            local_err = NULL;
            success = false;
        } else {
            s->dma[i] = NULL;
        }
    }
    return success;
}

static void fixture_destroy_spaces(IA64I2000460GXTestState *s)
{
    unsigned i;

    if (s->subword_cf8_mapped) {
        memory_region_del_subregion(
            intel_460gx_root_host_io(s->roots[s->layout.cf8_io_root]),
            &s->subword_cf8_io);
        s->subword_cf8_mapped = false;
    }
    if (s->host_spaces_initialized) {
        address_space_destroy(&s->host_data_as);
        address_space_destroy(&s->host_conf_as);
        s->host_spaces_initialized = false;
    }
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        if (s->root_io_initialized[i]) {
            address_space_destroy(&s->root_io_as[i]);
            s->root_io_initialized[i] = false;
        }
        if (s->root_mmio_initialized[i]) {
            object_unparent(OBJECT(&s->root_mmio[i]));
            s->root_mmio_initialized[i] = false;
        }
    }
}

static void fixture_cleanup(IA64I2000460GXTestState *s)
{
    DeviceState *child;
    bool destroyed;
    int i;

    /* Remove PCI children before this device. */
    destroyed = fixture_destroy_dma(s);
    g_assert(destroyed);
    drain_call_rcu();
    fixture_unmap_system(s);
    fixture_destroy_spaces(s);

    if (s->host) {
        child = DEVICE(s->host);
        fixture_remove_child(&child);
        s->host = NULL;
    }
    for (i = IA64_I2000_460GX_TEST_ROOT_COUNT - 1; i >= 0; i--) {
        if (s->roots[i]) {
            child = DEVICE(s->roots[i]);
            fixture_remove_child(&child);
            s->roots[i] = NULL;
        }
    }
    if (s->pid) {
        child = DEVICE(s->pid);
        fixture_remove_child(&child);
        s->pid = NULL;
    }
}

static bool fixture_create_host_and_pid(IA64I2000460GXTestState *s,
                                        Error **errp)
{
    DeviceState *child;

    child = fixture_add_child(DEVICE(s), "host", TYPE_INTEL_460GX_HOST);
    s->host = INTEL_460GX_HOST(child);
    qdev_prop_set_uint16(child, "x-initial-cbn", s->layout.cbn);
    qdev_prop_set_uint32(child, "x-initial-chipset-present",
                         s->layout.chipset_present);
    if (!sysbus_realize(SYS_BUS_DEVICE(child), errp)) {
        return false;
    }

    child = fixture_add_child(DEVICE(s), "pid", TYPE_INTEL_460GX_PID);
    s->pid = INTEL_460GX_PID(child);
    qdev_prop_set_uint8(child, INTEL_460GX_PID_PROP_INITIAL_ID,
                        s->layout.pid_id);
    qdev_prop_set_uint32(child, INTEL_460GX_PID_PROP_LEGACY_PIN, s->legacy_pin);
    return sysbus_realize(SYS_BUS_DEVICE(child), errp);
}

static bool fixture_create_root(IA64I2000460GXTestState *s, unsigned index,
                                Error **errp)
{
    const IA64I2000460GXTestRoot *layout_root = &s->layout.roots[index];
    g_autofree char *child_name = g_strdup_printf("root%u", index);
    g_autofree char *io_name =
        g_strdup_printf(TYPE_IA64_I2000_460GX_TEST ".root%u-io", index);
    g_autofree char *mmio_name =
        g_strdup_printf(TYPE_IA64_I2000_460GX_TEST ".root%u-mmio32", index);
    DeviceState *child;
    PCIBus *bus;
    unsigned pin;

    child =
        fixture_add_child(DEVICE(s), child_name, TYPE_INTEL_460GX_ROOT_HOST);
    s->roots[index] = INTEL_460GX_ROOT_HOST(child);
    qdev_prop_set_uint16(child, INTEL_460GX_ROOT_PROP_FIRST_BUS,
                         layout_root->first_bus);
    if (!sysbus_realize(SYS_BUS_DEVICE(child), errp)) {
        return false;
    }
    bus = intel_460gx_root_host_bus(s->roots[index]);
    if (!intel_460gx_host_attach_downstream_bus(s->host, layout_root->host_port,
                                                bus, layout_root->first_bus,
                                                layout_root->last_bus, errp)) {
        return false;
    }

    address_space_init(&s->root_io_as[index],
                       intel_460gx_root_host_io(s->roots[index]), io_name);
    s->root_io_initialized[index] = true;
    memory_region_init_alias(&s->root_mmio[index], OBJECT(s), mmio_name,
                             intel_460gx_root_host_mem(s->roots[index]),
                             layout_root->pci_mmio32_base,
                             layout_root->mmio32_size);
    s->root_mmio_initialized[index] = true;

    s->dma[index] =
        intel_460gx_dma_new(layout_root->dma_base, layout_root->dma_size, errp);
    if (!s->dma[index] ||
        !intel_460gx_dma_add_ram_alias(s->dma[index], layout_root->dma_base,
                                       layout_root->dma_size, s->ram,
                                       layout_root->dma_target_offset, errp) ||
        !intel_460gx_dma_seal(s->dma[index], errp) ||
        !intel_460gx_dma_attach_root(s->dma[index], bus, errp)) {
        return false;
    }

    for (pin = 0; pin < IA64_I2000_460GX_TEST_PCI_INTX_COUNT; pin++) {
        qdev_connect_gpio_out_named(
            child, INTEL_460GX_ROOT_GPIO_INTX, pin,
            qdev_get_gpio_in_named(DEVICE(s->pid), INTEL_460GX_PID_GPIO_IRQ,
                                   layout_root->intx_base + pin));
    }
    return true;
}

static void fixture_map_system(IA64I2000460GXTestState *s)
{
    unsigned i;

    memory_region_transaction_begin();
    memory_region_add_subregion(
        get_system_memory(), s->layout.pid_decode.base,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->pid), 0));
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        memory_region_add_subregion(get_system_memory(),
                                    s->layout.roots[i].cpu_mmio32_base,
                                    &s->root_mmio[i]);
    }
    memory_region_add_subregion(get_system_memory(), s->layout.legacy_io.base,
                                &s->sparse_io);
    memory_region_transaction_commit();
    s->system_regions_mapped = true;
}

static void fixture_realize(DeviceState *dev, Error **errp)
{
    IA64I2000460GXTestState *s = IA64_I2000_460GX_TEST(dev);
    Error *local_err = NULL;
    unsigned i;

    if (s->legacy_pin != IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED &&
        s->legacy_pin >= s->layout.legacy_pin_count) {
        error_setg(errp, "%s must be 0..%u or %u (disconnected)",
                   IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                   s->layout.legacy_pin_count - 1,
                   IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED);
        return;
    }

    if (!ia64_i2000_460gx_test_layout_validate(&s->layout, &local_err) ||
        !fixture_validate_parent_resources(s, &local_err) ||
        !fixture_create_host_and_pid(s, &local_err)) {
        goto fail;
    }

    address_space_init(&s->host_conf_as, intel_460gx_host_conf_region(s->host),
                       TYPE_IA64_I2000_460GX_TEST ".host-conf");
    address_space_init(&s->host_data_as, intel_460gx_host_data_region(s->host),
                       TYPE_IA64_I2000_460GX_TEST ".host-data");
    s->host_spaces_initialized = true;

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        if (!fixture_create_root(s, i, &local_err)) {
            goto fail;
        }
    }

    /*
     * Byte and word CF8 accesses use this backing; dword accesses update the
     * host latch.
     */
    memory_region_add_subregion(
        intel_460gx_root_host_io(s->roots[s->layout.cf8_io_root]),
        IA64_I2000_460GX_TEST_CF8_PORT, &s->subword_cf8_io);
    s->subword_cf8_mapped = true;

    fixture_map_system(s);
    return;

fail:
    fixture_cleanup(s);
    error_propagate(errp, local_err);
}

static void fixture_unrealize(DeviceState *dev)
{
    fixture_cleanup(IA64_I2000_460GX_TEST(dev));
}

static bool fixture_descriptor_installed(Object *obj, Error **errp)
{
    IA64I2000460GXTestState *s = IA64_I2000_460GX_TEST(obj);
    const IA64I2000460GXTestRange *range = &s->layout.descriptor_rom;
    MemoryRegionSection section =
        memory_region_find(get_system_memory(), range->base, range->size);
    bool installed;

    (void)errp;
    installed =
        section.mr != s->ram ||
        section.offset_within_address_space != range->base ||
        int128_get64(section.size) != range->size ||
        section.offset_within_region != range->base - s->layout.ram.base ||
        section.readonly;
    if (section.mr) {
        memory_region_unref(section.mr);
    }
    return installed;
}

static void fixture_reset(DeviceState *dev)
{
    IA64I2000460GXTestState *s = IA64_I2000_460GX_TEST(dev);

    memset(s->subword_cf8_bytes, 0, sizeof(s->subword_cf8_bytes));
    s->cf8_subdword_count = 0;
}

static void fixture_init(Object *obj)
{
    IA64I2000460GXTestState *s = IA64_I2000_460GX_TEST(obj);

    ia64_i2000_460gx_test_layout_init(&s->layout);
    memory_region_init_io(&s->subword_cf8_io, obj, &fixture_subword_cf8_ops, s,
                          TYPE_IA64_I2000_460GX_TEST ".subword-cf8", 4);
    memory_region_init_io(&s->sparse_io, obj, &fixture_sparse_io_ops, s,
                          TYPE_IA64_I2000_460GX_TEST ".sparse-io",
                          s->layout.legacy_io.size);
    /* This bridge only forwards to independent host/root address spaces. */
    s->sparse_io.disable_reentrancy_guard = true;
    object_property_add_uint64_ptr(obj,
                                   IA64_I2000_460GX_TEST_CF8_SUBDWORD_COUNT,
                                   &s->cf8_subdword_count, OBJ_PROP_FLAG_READ);
    object_property_add_bool(obj, IA64_I2000_460GX_TEST_DESCRIPTOR_INSTALLED,
                             fixture_descriptor_installed, NULL);
}

static const VMStateDescription vmstate_fixture_root_layout = {
    .name = TYPE_IA64_I2000_460GX_TEST "/root-layout",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT16_EQUAL(segment, IA64I2000460GXTestRoot),
            VMSTATE_UINT8_EQUAL(config_mechanism, IA64I2000460GXTestRoot),
            VMSTATE_UINT8_EQUAL(first_bus, IA64I2000460GXTestRoot),
            VMSTATE_UINT8_EQUAL(last_bus, IA64I2000460GXTestRoot),
            VMSTATE_UINT8_EQUAL(host_port, IA64I2000460GXTestRoot),
            VMSTATE_UINT8_EQUAL(intx_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT32_EQUAL(io_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT32_EQUAL(io_size, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(pci_mmio32_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(cpu_mmio32_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(mmio32_size, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(mmio64_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(mmio64_size, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(dma_base, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(dma_size, IA64I2000460GXTestRoot),
            VMSTATE_UINT64_EQUAL(dma_target_offset, IA64I2000460GXTestRoot),
            VMSTATE_END_OF_LIST() },
};

static const VMStateDescription vmstate_fixture = {
    .name = TYPE_IA64_I2000_460GX_TEST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            /* Fixed layout fields must match at the destination. */
            VMSTATE_UINT64_EQUAL(layout.ram.base, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.ram.size, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.firmware.base, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.firmware.size, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.descriptor_rom.base,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.descriptor_rom.size,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.descriptor_envelope.base,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.descriptor_envelope.size,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pid_decode.base,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pid_decode.size,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pid_envelope.base,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pid_envelope.size,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pib.base, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.pib.size, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.legacy_io.base,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.legacy_io.size,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.cf8_pa, IA64I2000460GXTestState),
            VMSTATE_UINT64_EQUAL(layout.cfc_pa, IA64I2000460GXTestState),
            VMSTATE_UINT32_EQUAL(layout.chipset_present,
                                 IA64I2000460GXTestState),
            VMSTATE_UINT8_EQUAL(layout.cbn, IA64I2000460GXTestState),
            VMSTATE_UINT8_EQUAL(layout.pid_id, IA64I2000460GXTestState),
            VMSTATE_UINT8_EQUAL(layout.pid_pin_count, IA64I2000460GXTestState),
            VMSTATE_UINT8_EQUAL(layout.legacy_pin_count,
                                IA64I2000460GXTestState),
            VMSTATE_UINT8_EQUAL(layout.cf8_io_root, IA64I2000460GXTestState),
            VMSTATE_UINT32_EQUAL(legacy_pin, IA64I2000460GXTestState),
            VMSTATE_UINT8_ARRAY(subword_cf8_bytes, IA64I2000460GXTestState, 4),
            VMSTATE_STRUCT_ARRAY(layout.roots, IA64I2000460GXTestState,
                                 IA64_I2000_460GX_TEST_ROOT_COUNT, 1,
                                 vmstate_fixture_root_layout,
                                 IA64I2000460GXTestRoot),
            VMSTATE_END_OF_LIST() },
};

static const Property fixture_properties[] = {
    DEFINE_PROP_LINK(IA64_I2000_460GX_TEST_PROP_RAM, IA64I2000460GXTestState,
                     ram, TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT32(IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                       IA64I2000460GXTestState, legacy_pin,
                       IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED),
};

static void fixture_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 i2000 460GX integration test";
    dc->realize = fixture_realize;
    dc->unrealize = fixture_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_fixture;
    device_class_set_legacy_reset(dc, fixture_reset);
    device_class_set_props(dc, fixture_properties);
}

PCIBus *ia64_i2000_460gx_test_root_bus(IA64I2000460GXTestState *fixture,
                                       unsigned index)
{
    if (!fixture || index >= IA64_I2000_460GX_TEST_ROOT_COUNT ||
        !fixture->roots[index]) {
        return NULL;
    }
    return intel_460gx_root_host_bus(fixture->roots[index]);
}

qemu_irq ia64_i2000_460gx_test_legacy_irq(IA64I2000460GXTestState *fixture)
{
    if (!fixture || !fixture->pid) {
        return NULL;
    }
    return qdev_get_gpio_in_named(DEVICE(fixture->pid),
                                  INTEL_460GX_PID_GPIO_LEGACY, 0);
}

uint32_t
ia64_i2000_460gx_test_legacy_pin(const IA64I2000460GXTestState *fixture)
{
    return fixture ? fixture->legacy_pin
                   : IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED;
}

static void fixture_qtest_remove_probes(IA64I2000460GXQTestState *s)
{
    int i;

    for (i = IA64_I2000_460GX_TEST_ROOT_COUNT - 1; i >= 0; i--) {
        if (!s->probes[i]) {
            continue;
        }
        if (qdev_is_realized(DEVICE(s->probes[i]))) {
            qdev_unrealize(DEVICE(s->probes[i]));
        }
        object_unparent(OBJECT(s->probes[i]));
        object_unref(OBJECT(s->probes[i]));
        s->probes[i] = NULL;
    }
    drain_call_rcu();
}

static void fixture_qtest_remove_fixture(IA64I2000460GXQTestState *s)
{
    DeviceState *fixture;

    if (!s->fixture) {
        return;
    }
    fixture = DEVICE(s->fixture);
    fixture_remove_child(&fixture);
    s->fixture = NULL;
}

static void fixture_qtest_set_intx(void *opaque, int line, int level)
{
    IA64I2000460GXQTestState *s = opaque;
    unsigned root = line / IA64_I2000_460GX_TEST_PCI_INTX_COUNT;
    unsigned pin = line % IA64_I2000_460GX_TEST_PCI_INTX_COUNT;

    if (root >= IA64_I2000_460GX_TEST_ROOT_COUNT || !s->probes[root]) {
        return;
    }

    /*
     * The signal traverses PCIDevice, the root raw pin, the builder wiring,
     * and the PID, exercising all twelve configured connections.
     */
    pci_config_set_interrupt_pin(s->probes[root]->config, pin + 1);
    pci_set_irq(s->probes[root], !!level);
}

static void fixture_qtest_set_legacy(void *opaque, int line, int level)
{
    IA64I2000460GXQTestState *s = opaque;

    if (line == 0 && s->fixture) {
        qemu_set_irq(ia64_i2000_460gx_test_legacy_irq(s->fixture), level);
    }
}

static void fixture_qtest_realize(DeviceState *dev, Error **errp)
{
    IA64I2000460GXQTestState *s = IA64_I2000_460GX_QTEST(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *fixture;
    Error *local_err = NULL;
    unsigned i;

    if (!qtest_enabled()) {
        error_setg(errp, "%s is available only under qtest",
                   TYPE_IA64_I2000_460GX_QTEST);
        return;
    }
    if (!machine->ram) {
        error_setg(errp,
                   "%s requires parent-machine RAM and does not own RAM/PIB",
                   TYPE_IA64_I2000_460GX_QTEST);
        return;
    }

    fixture = fixture_add_child(dev, IA64_I2000_460GX_TEST_DEVICE_CHILD,
                                TYPE_IA64_I2000_460GX_TEST);
    s->fixture = IA64_I2000_460GX_TEST(fixture);
    qdev_prop_set_uint32(fixture, IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                         s->legacy_pin);
    if (!object_property_set_link(OBJECT(fixture),
                                  IA64_I2000_460GX_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(fixture, NULL, &local_err)) {
        goto fail;
    }

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        PCIBus *bus = ia64_i2000_460gx_test_root_bus(s->fixture, i);

        s->probes[i] = pci_new(PCI_DEVFN(IA64_I2000_460GX_TEST_PCI_SLOT, 0),
                               TYPE_IOMMU_TESTDEV);
        if (!qdev_realize(DEVICE(s->probes[i]), BUS(bus), &local_err)) {
            object_unref(OBJECT(s->probes[i]));
            s->probes[i] = NULL;
            goto fail;
        }
        pci_config_set_interrupt_pin(s->probes[i]->config, 1);
    }
    return;

fail:
    fixture_qtest_remove_probes(s);
    fixture_qtest_remove_fixture(s);
    error_propagate(errp, local_err);
}

static void fixture_qtest_unrealize(DeviceState *dev)
{
    IA64I2000460GXQTestState *s = IA64_I2000_460GX_QTEST(dev);

    /* Probes own bus-master AddressSpaces; retire them before root DMA. */
    fixture_qtest_remove_probes(s);
    fixture_qtest_remove_fixture(s);
}

static void fixture_qtest_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), fixture_qtest_set_intx,
                            IA64_I2000_460GX_TEST_GPIO_INTX,
                            IA64_I2000_460GX_TEST_ROOT_COUNT *
                                IA64_I2000_460GX_TEST_PCI_INTX_COUNT);
    qdev_init_gpio_in_named(DEVICE(obj), fixture_qtest_set_legacy,
                            IA64_I2000_460GX_TEST_GPIO_LEGACY, 1);
}

static const Property fixture_qtest_properties[] = {
    DEFINE_PROP_UINT32(IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                       IA64I2000460GXQTestState, legacy_pin,
                       IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED),
};

static void fixture_qtest_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 i2000 460GX qtest device";
    dc->realize = fixture_qtest_realize;
    dc->unrealize = fixture_qtest_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
    device_class_set_props(dc, fixture_qtest_properties);
}

static const TypeInfo fixture_types[] = {
    {
        .name = TYPE_IA64_I2000_460GX_TEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64I2000460GXTestState),
        .instance_init = fixture_init,
        .class_init = fixture_class_init,
    },
    {
        .name = TYPE_IA64_I2000_460GX_QTEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64I2000460GXQTestState),
        .instance_init = fixture_qtest_init,
        .class_init = fixture_qtest_class_init,
    },
};

DEFINE_TYPES(fixture_types)
