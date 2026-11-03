/*
 * IA-64 ISP12160 qtest topology
 *
 * Combines the ISP12160 models with the i2000 460GX host.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_isp12160_test.h"
#include "hw/core/boards.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/scsi/isp12160.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "system/memory.h"
#include "system/qtest.h"

struct IA64ISP12160TestState {
    DeviceState parent_obj;

    IA64I2000460GXTestState *test_460gx;
    PCIDevice *isp;
    AddressSpace isp_io_as;
    bool isp_io_as_initialized;
    uint16_t variant;
};

static void isp12160_test_cancel_mailbox(void *opaque, int line, int level)
{
    IA64ISP12160TestState *s = opaque;
    AddressSpace *io;
    MemTxResult result;

    if (line != 0 || !level || !s->isp_io_as_initialized) {
        return;
    }

    /* Issue SET_HOST_INT and RESET_RISC before the mailbox BH can run. */
    io = &s->isp_io_as;
    address_space_stw_le(
        io, IA64_ISP12160_TEST_IO_BASE + ISP12160_REG_HOST_COMMAND,
        ISP12160_HC_SET_HOST_INT, MEMTXATTRS_UNSPECIFIED, &result);
    g_assert(result == MEMTX_OK);
    address_space_stw_le(
        io, IA64_ISP12160_TEST_IO_BASE + ISP12160_REG_HOST_COMMAND,
        ISP12160_HC_RESET_RISC, MEMTXATTRS_UNSPECIFIED, &result);
    g_assert(result == MEMTX_OK);
}

static DeviceState *isp12160_test_add_child(DeviceState *parent,
                                            const char *name, const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void isp12160_test_remove_isp(IA64ISP12160TestState *s)
{
    if (s->isp_io_as_initialized) {
        address_space_destroy(&s->isp_io_as);
        s->isp_io_as_initialized = false;
    }
    if (!s->isp) {
        return;
    }
    if (qdev_is_realized(DEVICE(s->isp))) {
        qdev_unrealize(DEVICE(s->isp));
    }
    object_unparent(OBJECT(s->isp));
    object_unref(OBJECT(s->isp));
    s->isp = NULL;

    /* Retire its bus-master AddressSpace before removing the DMA view. */
    drain_call_rcu();
}

static void isp12160_test_remove_460gx_test(IA64ISP12160TestState *s)
{
    DeviceState *test_460gx;

    if (!s->test_460gx) {
        return;
    }
    test_460gx = DEVICE(s->test_460gx);
    if (qdev_is_realized(test_460gx)) {
        qdev_unrealize(test_460gx);
    }
    object_unparent(OBJECT(test_460gx));
    s->test_460gx = NULL;
}

static void isp12160_test_realize(DeviceState *dev, Error **errp)
{
    IA64ISP12160TestState *s = IA64_ISP12160_MAILBOX_QTEST(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *test_460gx;
    PCIBus *bus;
    Error *local_err = NULL;
    const char *qtest_type;
    const char *isp_type;
    const char *io_as_name;

    switch (s->variant) {
    case ISP12160_VARIANT_SCSI:
        qtest_type = TYPE_IA64_ISP12160_SCSI_QTEST;
        isp_type = TYPE_ISP12160_SCSI;
        io_as_name = TYPE_IA64_ISP12160_SCSI_QTEST ".io";
        break;
    case ISP12160_VARIANT_QUEUE:
        qtest_type = TYPE_IA64_ISP12160_QUEUE_QTEST;
        isp_type = TYPE_ISP12160_QUEUE;
        io_as_name = TYPE_IA64_ISP12160_QUEUE_QTEST ".io";
        break;
    default:
        qtest_type = TYPE_IA64_ISP12160_MAILBOX_QTEST;
        isp_type = TYPE_ISP12160_MAILBOX;
        io_as_name = TYPE_IA64_ISP12160_MAILBOX_QTEST ".io";
        break;
    }

    if (!qtest_enabled()) {
        error_setg(errp, "%s is available only under qtest", qtest_type);
        return;
    }
    if (!machine->ram) {
        error_setg(errp, "%s requires parent-machine RAM", qtest_type);
        return;
    }

    test_460gx = isp12160_test_add_child(
        dev, IA64_ISP12160_TEST_460GX_TEST_CHILD, TYPE_IA64_I2000_460GX_TEST);
    s->test_460gx = IA64_I2000_460GX_TEST(test_460gx);
    if (!object_property_set_link(OBJECT(test_460gx),
                                  IA64_I2000_460GX_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(test_460gx, NULL, &local_err)) {
        goto fail;
    }

    bus = ia64_i2000_460gx_test_root_bus(s->test_460gx,
                                         IA64_ISP12160_TEST_ROOT_INDEX);
    if (!bus || pci_bus_num(bus) != IA64_ISP12160_TEST_PCI_BUS) {
        error_setg(&local_err, "%s did not find root-1 bus 0x%x", qtest_type,
                   IA64_ISP12160_TEST_PCI_BUS);
        goto fail;
    }

    s->isp = pci_new(
        PCI_DEVFN(IA64_ISP12160_TEST_PCI_SLOT, IA64_ISP12160_TEST_PCI_FUNCTION),
        isp_type);
    if (!qdev_realize(DEVICE(s->isp), BUS(bus), &local_err)) {
        object_unref(OBJECT(s->isp));
        s->isp = NULL;
        goto fail;
    }
    if (s->isp->config[PCI_INTERRUPT_PIN] != IA64_ISP12160_TEST_INTERRUPT_PIN) {
        error_setg(&local_err, "%s did not expose INTA", isp_type);
        goto fail;
    }
    address_space_init(&s->isp_io_as, pci_address_space_io(s->isp), io_as_name);
    s->isp_io_as_initialized = true;

    /* Root 1 INTA is routed to PID pin 20 without slot swizzling. */
    return;

fail:
    isp12160_test_remove_isp(s);
    isp12160_test_remove_460gx_test(s);
    error_propagate(errp, local_err);
}

static void isp12160_test_unrealize(DeviceState *dev)
{
    IA64ISP12160TestState *s = IA64_ISP12160_MAILBOX_QTEST(dev);

    isp12160_test_remove_isp(s);
    isp12160_test_remove_460gx_test(s);
}

static void isp12160_mailbox_test_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), isp12160_test_cancel_mailbox,
                            IA64_ISP12160_TEST_GPIO_CANCEL_MAILBOX, 1);
}

static void isp12160_queue_test_init(Object *obj)
{
    IA64ISP12160TestState *s = IA64_ISP12160_MAILBOX_QTEST(obj);

    s->variant = ISP12160_VARIANT_QUEUE;
}

static void isp12160_scsi_test_init(Object *obj)
{
    IA64ISP12160TestState *s = IA64_ISP12160_MAILBOX_QTEST(obj);

    s->variant = ISP12160_VARIANT_SCSI;
}

static void isp12160_mailbox_test_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 ISP12160 mailbox qtest device";
    dc->realize = isp12160_test_realize;
    dc->unrealize = isp12160_test_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
}

static const TypeInfo isp12160_mailbox_test_info = {
    .name = TYPE_IA64_ISP12160_MAILBOX_QTEST,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IA64ISP12160TestState),
    .instance_init = isp12160_mailbox_test_init,
    .class_init = isp12160_mailbox_test_class_init,
};

static void isp12160_queue_test_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 ISP12160 queue qtest device";
    dc->user_creatable = true;
    dc->hotpluggable = false;
}

static const TypeInfo isp12160_queue_test_info = {
    .name = TYPE_IA64_ISP12160_QUEUE_QTEST,
    .parent = TYPE_IA64_ISP12160_MAILBOX_QTEST,
    .instance_init = isp12160_queue_test_init,
    .class_init = isp12160_queue_test_class_init,
};

static void isp12160_scsi_test_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 ISP12160 SCSI qtest device";
    dc->user_creatable = true;
    dc->hotpluggable = false;
}

static const TypeInfo isp12160_scsi_test_info = {
    .name = TYPE_IA64_ISP12160_SCSI_QTEST,
    .parent = TYPE_IA64_ISP12160_QUEUE_QTEST,
    .instance_init = isp12160_scsi_test_init,
    .class_init = isp12160_scsi_test_class_init,
};

static void isp12160_test_register_types(void)
{
    type_register_static(&isp12160_mailbox_test_info);
    type_register_static(&isp12160_queue_test_info);
    type_register_static(&isp12160_scsi_test_info);
}

type_init(isp12160_test_register_types)
