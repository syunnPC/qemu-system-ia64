/*
 * IA-64 i2000 I/O test device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_i2000_io_test.h"
#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "hw/ia64/ia64_i2000_rtc_bank1.h"
#include "hw/ide/ide-dev.h"
#include "hw/ide/isa.h"
#include "hw/intc/i8259.h"
#include "hw/isa/isa.h"
#include "hw/isa/lpc47b27.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/scsi/isp12160.h"
#include "hw/scsi/scsi.h"
#include "hw/timer/i8254.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/blockdev.h"
#include "system/qtest.h"

#define TYPE_IA64_I2000_IO_TEST_IFB_F0 "ia64-i2000-io-test-ifb-f0"
#define TYPE_IA64_I2000_IO_TEST_IFB_F1 "ia64-i2000-io-test-ifb-f1"

typedef struct IA64I2000IoTestIFBF0State {
    PCIDevice parent_obj;
} IA64I2000IoTestIFBF0State;

typedef struct IA64I2000IoTestIFBF1State {
    PCIDevice parent_obj;
} IA64I2000IoTestIFBF1State;

typedef struct IA64I2000IoQTestState {
    DeviceState parent_obj;

    IA64I2000460GXTestState *test_460gx;
    IA64I2000IoTestState *io_test;
    NICPeers i82559_peers;
} IA64I2000IoQTestState;

#define IA64_I2000_IO_QTEST(obj) \
    OBJECT_CHECK(IA64I2000IoQTestState, (obj), \
                 TYPE_IA64_I2000_IO_QTEST)

struct IA64I2000IoTestState {
    DeviceState parent_obj;

    IA64I2000IoTestLayout layout;
    IA64I2000460GXTestState *test_460gx;
    Chardev *uart_chardev;
    DriveInfo *cd_drive;
    PCIDevice *f0;
    PCIDevice *f1;
    PCIDevice *i82559;
    PCIDevice *isp12160;
    NICPeers i82559_peers;
    ISABus *isa_bus;
    ISABus *lpc_bus;
    qemu_irq *pic_irqs;
    qemu_irq *static_irqs;
    qemu_irq *dynamic_irqs;
    PICCommonState *master_pic;
    ISAIDEState *ide;
    uint16_t static_irq_levels;
    uint16_t dynamic_irq_levels;
    bool router_active;
};

static void io_test_pci_config_realize(PCIDevice *pci, Error **errp)
{
    /* Function-specific configuration registers are immutable zero. */
    memset(pci->wmask + PCI_CONFIG_HEADER_SIZE, 0,
           pci_config_size(pci) - PCI_CONFIG_HEADER_SIZE);
    pci_set_word(pci->wmask + PCI_COMMAND, 0);
    pci->config[PCI_CLASS_PROG] = 0;
    pci_set_word(pci->config + PCI_SUBSYSTEM_VENDOR_ID, 0);
    pci_set_word(pci->config + PCI_SUBSYSTEM_ID, 0);
}

static void io_test_ifb_f1_realize(PCIDevice *pci, Error **errp)
{
    io_test_pci_config_realize(pci, errp);
    pci->config[PCI_CLASS_PROG] = IA64_I2000_IO_TEST_F1_PROG_IF;
}

static const VMStateDescription vmstate_io_test_ifb_f0 = {
    .name = TYPE_IA64_I2000_IO_TEST_IFB_F0,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, IA64I2000IoTestIFBF0State),
            VMSTATE_END_OF_LIST() },
};

static const VMStateDescription vmstate_io_test_ifb_f1 = {
    .name = TYPE_IA64_I2000_IO_TEST_IFB_F1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, IA64I2000IoTestIFBF1State),
            VMSTATE_END_OF_LIST() },
};

static void io_test_ifb_f0_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = io_test_pci_config_realize;
    pc->vendor_id = IA64_I2000_IO_TEST_F0_VENDOR_ID;
    pc->device_id = IA64_I2000_IO_TEST_F0_DEVICE_ID;
    pc->revision = IA64_I2000_IO_TEST_F0_REVISION;
    pc->class_id = IA64_I2000_IO_TEST_F0_CLASS;
    dc->desc = "i2000 I/O test ISA PCI function";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_io_test_ifb_f0;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static void io_test_ifb_f1_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = io_test_ifb_f1_realize;
    pc->vendor_id = IA64_I2000_IO_TEST_F1_VENDOR_ID;
    pc->device_id = IA64_I2000_IO_TEST_F1_DEVICE_ID;
    pc->revision = IA64_I2000_IO_TEST_F1_REVISION;
    pc->class_id = IA64_I2000_IO_TEST_F1_CLASS;
    dc->desc = "i2000 I/O test PIO IDE PCI function";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_io_test_ifb_f1;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void io_test_remove_pci_device(PCIDevice **pci)
{
    if (!*pci) {
        return;
    }
    object_unparent(OBJECT(*pci));
    *pci = NULL;
}

static void io_test_cleanup(IA64I2000IoTestState *s)
{
    s->router_active = false;
    io_test_remove_pci_device(&s->isp12160);
    io_test_remove_pci_device(&s->i82559);
    io_test_remove_pci_device(&s->f1);
    io_test_remove_pci_device(&s->f0);
    g_free(s->pic_irqs);
    s->pic_irqs = NULL;
    if (s->static_irqs) {
        qemu_free_irqs(s->static_irqs, ISA_NUM_IRQS);
        s->static_irqs = NULL;
    }
    if (s->dynamic_irqs) {
        qemu_free_irqs(s->dynamic_irqs, ISA_NUM_IRQS);
        s->dynamic_irqs = NULL;
    }
    s->master_pic = NULL;
    s->isa_bus = NULL;
    s->lpc_bus = NULL;
    s->ide = NULL;
}

static bool io_test_irq_aggregate_level(const IA64I2000IoTestState *s,
                                        unsigned irq)
{
    uint16_t mask = BIT(irq);

    return (s->static_irq_levels | s->dynamic_irq_levels) & mask;
}

static void io_test_set_router_input(IA64I2000IoTestState *s, unsigned irq,
                                     int level, bool dynamic)
{
    uint16_t *levels = dynamic ? &s->dynamic_irq_levels : &s->static_irq_levels;
    uint16_t mask;
    bool old_output;
    bool new_output;

    g_assert(irq < ISA_NUM_IRQS);

    /* Master input 2 is owned exclusively by the slave PIC cascade. */
    if (irq == s->layout.pic_cascade_irq) {
        return;
    }

    mask = BIT(irq);
    old_output = io_test_irq_aggregate_level(s, irq);
    if (level) {
        *levels |= mask;
    } else {
        *levels &= ~mask;
    }
    new_output = io_test_irq_aggregate_level(s, irq);
    if (s->router_active && old_output != new_output) {
        qemu_set_irq(s->pic_irqs[irq], new_output);
    }
}

static void io_test_set_static_irq(void *opaque, int irq, int level)
{
    io_test_set_router_input(opaque, irq, level, false);
}

static void io_test_set_dynamic_irq(void *opaque, int irq, int level)
{
    io_test_set_router_input(opaque, irq, level, true);
}

static bool io_test_create_pci_functions(IA64I2000IoTestState *s, PCIBus *root,
                                         Error **errp)
{
    PCIDevice *pci;

    pci = pci_new_multifunction(
        PCI_DEVFN(s->layout.pci_slot, s->layout.f0.function),
        TYPE_IA64_I2000_IO_TEST_IFB_F0);
    if (!pci_realize_and_unref(pci, root, errp)) {
        return false;
    }
    s->f0 = pci;

    pci = pci_new(PCI_DEVFN(s->layout.pci_slot, s->layout.f1.function),
                  TYPE_IA64_I2000_IO_TEST_IFB_F1);
    if (!pci_realize_and_unref(pci, root, errp)) {
        return false;
    }
    s->f1 = pci;
    return true;
}

static bool io_test_create_i82559(IA64I2000IoTestState *s, PCIBus *root,
                                  Error **errp)
{
    uint8_t mac[6] = {
        s->layout.i82559_mac_word0, s->layout.i82559_mac_word0 >> 8,
        s->layout.i82559_mac_word1, s->layout.i82559_mac_word1 >> 8,
        s->layout.i82559_mac_word2, s->layout.i82559_mac_word2 >> 8,
    };
    PCIDevice *pci = pci_new(
        PCI_DEVFN(s->layout.i82559_slot, s->layout.i82559.function), "i82559c");

    qdev_prop_set_string(DEVICE(pci), "romfile", "");
    qdev_prop_set_macaddr(DEVICE(pci), "mac", mac);
    if (s->i82559_peers.ncs[0]) {
        qdev_prop_set_netdev(DEVICE(pci), "netdev", s->i82559_peers.ncs[0]);
    }
    if (!pci_realize_and_unref(pci, root, errp)) {
        return false;
    }
    s->i82559 = pci;
    return true;
}

static bool io_test_create_isp12160(IA64I2000IoTestState *s, PCIBus *root,
                                    Error **errp)
{
    PCIDevice *pci;
    BusState *child_bus;

    if (pci_bus_num(root) != s->layout.isp12160_bus) {
        error_setg(errp,
                   "i2000 I/O test ISP12160 root has bus 0x%x, expected 0x%x",
                   pci_bus_num(root), s->layout.isp12160_bus);
        return false;
    }

    pci =
        pci_new(PCI_DEVFN(s->layout.isp12160_slot, s->layout.isp12160.function),
                TYPE_ISP12160_SCSI);
    if (!pci_realize_and_unref(pci, root, errp)) {
        return false;
    }
    s->isp12160 = pci;
    ia64_i2000_io_test_restore_pci_resources(s);

    child_bus = qdev_get_child_bus(DEVICE(pci), "isp12160-scsi.0");
    if (!child_bus) {
        error_setg(errp, "i2000 I/O test ISP12160 did not create its SCSI bus");
        return false;
    }
    scsi_bus_legacy_handle_cmdline(SCSI_BUS(child_bus));
    return true;
}

void ia64_i2000_io_test_restore_pci_resources(IA64I2000IoTestState *s)
{
    if (!s || !s->isp12160) {
        return;
    }

    /*
     * The platform descriptor preassigns these BARs.  PCI bus reset clears
     * writable BAR bits, so the machine restores them after every
     * machine-wide reset.  Decode and bus mastering remain disabled until
     * firmware or a guest driver enables them.
     */
    pci_default_write_config(
        s->isp12160, PCI_BASE_ADDRESS_0,
        s->layout.isp12160_io_bar_base | PCI_BASE_ADDRESS_SPACE_IO, 4);
    pci_default_write_config(s->isp12160, PCI_BASE_ADDRESS_1,
                             s->layout.isp12160_mmio_bar_base, 4);
    pci_default_write_config(s->isp12160, PCI_COMMAND, 0, 2);
}

static bool io_test_create_lpc47b27(IA64I2000IoTestState *s, Error **errp)
{
    ISADevice *lpc = isa_new(TYPE_LPC47B27_ISA);

    qdev_prop_set_uint16(DEVICE(lpc), LPC47B27_ISA_PROP_CONFIG_IOBASE,
                         s->layout.superio_config.base);
    qdev_prop_set_uint32(DEVICE(lpc), LPC47B27_ISA_PROP_UART_INPUT_CLOCK_HZ,
                         s->layout.uart_input_clock_hz);
    if (s->uart_chardev) {
        qdev_prop_set_chr(DEVICE(lpc), "chardev", s->uart_chardev);
    }
    return isa_realize_and_unref(lpc, s->lpc_bus, errp);
}

static bool io_test_create_rtc(IA64I2000IoTestState *s, Error **errp)
{
    ISADevice *rtc;

    rtc = isa_new(TYPE_MC146818_RTC);
    qdev_prop_set_int32(DEVICE(rtc), "base_year", s->layout.rtc_base_year);
    qdev_prop_set_uint16(DEVICE(rtc), "iobase", s->layout.rtc_bank0.base);
    qdev_prop_set_uint8(DEVICE(rtc), "irq", s->layout.rtc_irq);
    if (!isa_realize_and_unref(rtc, s->isa_bus, errp)) {
        return false;
    }
    isa_connect_gpio_out(rtc, 0, s->layout.rtc_irq);

    rtc = isa_new(TYPE_IA64_I2000_RTC_BANK1);
    qdev_prop_set_uint16(DEVICE(rtc), IA64_I2000_RTC_BANK1_PROP_IOBASE,
                         s->layout.rtc_bank1.base);
    return isa_realize_and_unref(rtc, s->isa_bus, errp);
}

static bool io_test_create_ide(IA64I2000IoTestState *s, Error **errp)
{
    ISADevice *isadev = isa_new(TYPE_ISA_IDE);
    DeviceState *cd;

    qdev_prop_set_uint32(DEVICE(isadev), "iobase", s->layout.ide_command.base);
    qdev_prop_set_uint32(DEVICE(isadev), "iobase2", s->layout.ide_control.base);
    qdev_prop_set_uint32(DEVICE(isadev), "irq", s->layout.ide_irq);
    qdev_prop_set_bit(DEVICE(isadev), "pio-only", true);
    if (!isa_realize_and_unref(isadev, s->isa_bus, errp)) {
        return false;
    }
    s->ide = ISA_IDE(isadev);

    cd = qdev_new("ide-cd");
    qdev_prop_set_uint32(cd, "unit", s->layout.ide_master_unit);
    if (s->cd_drive &&
        !qdev_prop_set_drive_err(cd, "drive", blk_by_legacy_dinfo(s->cd_drive),
                                 errp)) {
        object_unref(OBJECT(cd));
        return false;
    }
    return qdev_realize_and_unref(cd, BUS(isa_ide_bus(s->ide)), errp);
}

static bool io_test_create_isa(IA64I2000IoTestState *s, Error **errp)
{
    ISADevice *pit;

    s->isa_bus = isa_bus_new_non_default(
        DEVICE(s->f0), pci_address_space(s->f0), pci_address_space_io(s->f0));
    s->lpc_bus = isa_bus_new_non_default(
        DEVICE(s->f0), pci_address_space(s->f0), pci_address_space_io(s->f0));
    /* F0 was already realized before these explicitly owned buses exist. */
    if (!qbus_realize(BUS(s->isa_bus), errp) ||
        !qbus_realize(BUS(s->lpc_bus), errp)) {
        return false;
    }
    s->pic_irqs = i8259_init_pair(
        s->isa_bus, ia64_i2000_460gx_test_legacy_irq(s->test_460gx),
        &s->master_pic);
    s->static_irqs =
        qemu_allocate_irqs(io_test_set_static_irq, s, ISA_NUM_IRQS);
    s->dynamic_irqs =
        qemu_allocate_irqs(io_test_set_dynamic_irq, s, ISA_NUM_IRQS);
    isa_bus_register_input_irqs(s->isa_bus, s->static_irqs);
    isa_bus_register_input_irqs(s->lpc_bus, s->dynamic_irqs);
    s->router_active = true;

    pit =
        i8254_pit_init(s->isa_bus, s->layout.pit.base, s->layout.pit_irq, NULL);
    if (!pit) {
        error_setg(errp, "failed to create i2000 I/O test PIT");
        return false;
    }
    if (!io_test_create_lpc47b27(s, errp) || !io_test_create_rtc(s, errp) ||
        !io_test_create_ide(s, errp)) {
        return false;
    }
    return true;
}

static void io_test_realize(DeviceState *dev, Error **errp)
{
    IA64I2000IoTestState *s = IA64_I2000_IO_TEST(dev);
    PCIBus *root;
    PCIBus *i82559_root;
    PCIBus *isp12160_root;
    Error *local_err = NULL;

    if (!s->test_460gx) {
        error_setg(errp, "%s requires the '%s' link", TYPE_IA64_I2000_IO_TEST,
                   IA64_I2000_IO_TEST_PROP_460GX_TEST);
        return;
    }
    if (!ia64_i2000_io_test_layout_validate(&s->layout, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    if (ia64_i2000_460gx_test_legacy_pin(s->test_460gx) !=
        s->layout.pid_legacy_pin) {
        error_setg(errp,
                   "i2000 I/O test device requires 460GX test legacy-pin %u",
                   s->layout.pid_legacy_pin);
        return;
    }
    root = ia64_i2000_460gx_test_root_bus(s->test_460gx, s->layout.parent_root);
    if (!root) {
        error_setg(errp,
                   "i2000 I/O test requires a realized 460GX test root %u",
                   s->layout.parent_root);
        return;
    }
    i82559_root = ia64_i2000_460gx_test_root_bus(s->test_460gx,
                                                 s->layout.i82559_parent_root);
    if (!i82559_root) {
        error_setg(errp,
                   "i2000 I/O test device requires a realized i82559 root %u",
                   s->layout.i82559_parent_root);
        return;
    }
    isp12160_root = ia64_i2000_460gx_test_root_bus(
        s->test_460gx, s->layout.isp12160_parent_root);
    if (!isp12160_root) {
        error_setg(errp,
                   "i2000 I/O test device requires a realized ISP12160 root %u",
                   s->layout.isp12160_parent_root);
        return;
    }

    if (!io_test_create_pci_functions(s, root, &local_err) ||
        !io_test_create_i82559(s, i82559_root, &local_err) ||
        !io_test_create_isp12160(s, isp12160_root, &local_err) ||
        !io_test_create_isa(s, &local_err)) {
        io_test_cleanup(s);
        error_propagate(errp, local_err);
    }
}

static void io_test_unrealize(DeviceState *dev)
{
    io_test_cleanup(IA64_I2000_IO_TEST(dev));
}

static void io_test_reset(DeviceState *dev)
{
    IA64I2000IoTestState *s = IA64_I2000_IO_TEST(dev);
    unsigned irq;

    if (s->pic_irqs) {
        for (irq = 0; irq < ISA_NUM_IRQS; irq++) {
            if (irq != s->layout.pic_cascade_irq) {
                qemu_irq_lower(s->pic_irqs[irq]);
            }
        }
    }
    s->static_irq_levels = 0;
    s->dynamic_irq_levels = 0;
}

static void io_test_init(Object *obj)
{
    IA64I2000IoTestState *s = IA64_I2000_IO_TEST(obj);

    ia64_i2000_io_test_layout_init(&s->layout);
}

static const VMStateDescription vmstate_io_test_port_range = {
    .name = TYPE_IA64_I2000_IO_TEST "/port-range",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT32_EQUAL(base, IA64I2000IoTestIOPortRange),
            VMSTATE_UINT32_EQUAL(size, IA64I2000IoTestIOPortRange),
            VMSTATE_END_OF_LIST() },
};

static const VMStateDescription vmstate_io_test_pci_function = {
    .name = TYPE_IA64_I2000_IO_TEST "/pci-function",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT16_EQUAL(vendor_id, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT16_EQUAL(device_id, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT16_EQUAL(class_id, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT16_EQUAL(subsystem_vendor_id,
                                 IA64I2000IoTestPCIFunction),
            VMSTATE_UINT16_EQUAL(subsystem_id, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT8_EQUAL(revision, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT8_EQUAL(prog_if, IA64I2000IoTestPCIFunction),
            VMSTATE_UINT8_EQUAL(function, IA64I2000IoTestPCIFunction),
            VMSTATE_END_OF_LIST() },
};

static const VMStateDescription vmstate_io_test_layout = {
    .name = TYPE_IA64_I2000_IO_TEST "/layout",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT8_EQUAL(parent_root, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(parent_io, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(parent_cf8, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(parent_cfc, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(pci_slot, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(pci_function_count, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(f0, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_pci_function,
                           IA64I2000IoTestPCIFunction),
            VMSTATE_STRUCT(f1, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_pci_function,
                           IA64I2000IoTestPCIFunction),
            VMSTATE_UINT8_EQUAL(i82559_parent_root, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i82559_slot, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i82559_interrupt_pin, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i82559_pid_pin, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(i82559, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_pci_function,
                           IA64I2000IoTestPCIFunction),
            VMSTATE_UINT32_EQUAL(i82559_mmio_bar_size, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(i82559_io_bar_size, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(i82559_flash_bar_size, IA64I2000IoTestLayout),
            VMSTATE_UINT16_EQUAL(i82559_eeprom_words, IA64I2000IoTestLayout),
            VMSTATE_UINT16_EQUAL(i82559_eeprom_checksum, IA64I2000IoTestLayout),
            VMSTATE_UINT16_EQUAL(i82559_mac_word0, IA64I2000IoTestLayout),
            VMSTATE_UINT16_EQUAL(i82559_mac_word1, IA64I2000IoTestLayout),
            VMSTATE_UINT16_EQUAL(i82559_mac_word2, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i82559_option_rom_enabled,
                                IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_parent_root, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_bus, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_slot, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_interrupt_pin, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_pid_pin, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(isp12160, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_pci_function,
                           IA64I2000IoTestPCIFunction),
            VMSTATE_UINT32_EQUAL(isp12160_io_bar_base, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(isp12160_io_bar_size, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(isp12160_mmio_bar_base, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(isp12160_mmio_bar_size, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(isp12160_option_rom_enabled,
                                IA64I2000IoTestLayout),
            VMSTATE_STRUCT(pic_master, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(pic_slave, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(elcr_master, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(elcr_slave, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(pic_cascade_irq, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(pid_legacy_pin, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(pit, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(pit_irq, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(superio_config, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(superio_sysopt, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_enter_key, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_enter_count, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_exit_key, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_ldn_select_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_device_id_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(superio_device_id, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_ldn, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_activate_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_base_msb_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_base_lsb_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_irq_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_mode_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(uart_reset_active, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(uart, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(uart_irq, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(uart_input_clock_hz, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_ldn, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_activate_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_kbd_irq_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_mouse_irq_cr, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_reset_active, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(i8042_data, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(i8042_command, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(i8042_kbd_irq, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(i8042_mouse_irq, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(rtc_bank0, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(rtc_bank1, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(rtc_irq, IA64I2000IoTestLayout),
            VMSTATE_INT32_EQUAL(rtc_base_year, IA64I2000IoTestLayout),
            VMSTATE_STRUCT(ide_command, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_STRUCT(ide_control, IA64I2000IoTestLayout, 1,
                           vmstate_io_test_port_range,
                           IA64I2000IoTestIOPortRange),
            VMSTATE_UINT8_EQUAL(ide_irq, IA64I2000IoTestLayout),
            VMSTATE_UINT8_EQUAL(ide_master_unit, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(enabled_features, IA64I2000IoTestLayout),
            VMSTATE_UINT32_EQUAL(disabled_features, IA64I2000IoTestLayout),
            VMSTATE_END_OF_LIST() },
};

static const VMStateDescription vmstate_io_test = {
    .name = TYPE_IA64_I2000_IO_TEST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_STRUCT(layout, IA64I2000IoTestState, 1,
                           vmstate_io_test_layout, IA64I2000IoTestLayout),
            VMSTATE_UINT16(static_irq_levels, IA64I2000IoTestState),
            VMSTATE_UINT16(dynamic_irq_levels, IA64I2000IoTestState),
            VMSTATE_END_OF_LIST() },
};

static const Property io_test_properties[] = {
    DEFINE_PROP_LINK(IA64_I2000_IO_TEST_PROP_460GX_TEST, IA64I2000IoTestState,
                     test_460gx, TYPE_IA64_I2000_460GX_TEST,
                     IA64I2000460GXTestState *),
    DEFINE_PROP_NETDEV(IA64_I2000_IO_TEST_PROP_I82559_NETDEV,
                       IA64I2000IoTestState, i82559_peers),
};

static void io_test_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "IA-64 i2000 I/O test device";
    dc->realize = io_test_realize;
    dc->unrealize = io_test_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_io_test;
    device_class_set_legacy_reset(dc, io_test_reset);
    device_class_set_props(dc, io_test_properties);
}

ISABus *ia64_i2000_io_test_isa_bus(IA64I2000IoTestState *io_test)
{
    return io_test ? io_test->isa_bus : NULL;
}

IDEBus *ia64_i2000_io_test_ide_bus(IA64I2000IoTestState *io_test)
{
    return io_test && io_test->ide ? isa_ide_bus(io_test->ide) : NULL;
}

bool ia64_i2000_io_test_set_uart_chardev(IA64I2000IoTestState *io_test,
                                         Chardev *chardev, Error **errp)
{
    if (!io_test) {
        error_setg(errp, "missing i2000 I/O test device");
        return false;
    }
    if (qdev_is_realized(DEVICE(io_test))) {
        error_setg(errp,
                   "i2000 I/O test UART chardev must be set before realize");
        return false;
    }

    io_test->uart_chardev = chardev;
    return true;
}

bool ia64_i2000_io_test_set_cd_drive(IA64I2000IoTestState *io_test,
                                     DriveInfo *drive, Error **errp)
{
    if (!io_test) {
        error_setg(errp, "missing i2000 I/O test device");
        return false;
    }
    if (qdev_is_realized(DEVICE(io_test))) {
        error_setg(errp, "i2000 I/O test CD drive must be set before realize");
        return false;
    }
    if (drive &&
        (drive->type != IF_IDE || drive->bus != 0 ||
         drive->unit != io_test->layout.ide_master_unit || !drive->media_cd)) {
        error_setg(errp,
                   "i2000 I/O test accepts only IDE bus 0 unit %u CD media",
                   io_test->layout.ide_master_unit);
        return false;
    }

    io_test->cd_drive = drive;
    return true;
}

static DeviceState *io_qtest_add_child(DeviceState *parent, const char *name,
                                       const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void io_qtest_remove_child(DeviceState **child)
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

static void io_qtest_realize(DeviceState *dev, Error **errp)
{
    IA64I2000IoQTestState *s = IA64_I2000_IO_QTEST(dev);
    IA64I2000IoTestLayout layout;
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *child;
    Error *local_err = NULL;

    if (!qtest_enabled()) {
        error_setg(errp, "%s is available only under qtest",
                   TYPE_IA64_I2000_IO_QTEST);
        return;
    }
    if (!machine->ram) {
        error_setg(errp, "%s requires parent-machine RAM",
                   TYPE_IA64_I2000_IO_QTEST);
        return;
    }
    ia64_i2000_io_test_layout_init(&layout);

    child = io_qtest_add_child(dev, IA64_I2000_IO_TEST_460GX_TEST_CHILD,
                               TYPE_IA64_I2000_460GX_TEST);
    s->test_460gx = IA64_I2000_460GX_TEST(child);
    qdev_prop_set_uint32(child, IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                         layout.pid_legacy_pin);
    if (!object_property_set_link(OBJECT(child), IA64_I2000_460GX_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(child, NULL, &local_err)) {
        goto fail;
    }

    child = io_qtest_add_child(dev, IA64_I2000_IO_TEST_DEVICE_CHILD,
                               TYPE_IA64_I2000_IO_TEST);
    s->io_test = IA64_I2000_IO_TEST(child);
    if (s->i82559_peers.ncs[0]) {
        qdev_prop_set_netdev(child, IA64_I2000_IO_TEST_PROP_I82559_NETDEV,
                             s->i82559_peers.ncs[0]);
    }
    if (!object_property_set_link(OBJECT(child),
                                  IA64_I2000_IO_TEST_PROP_460GX_TEST,
                                  OBJECT(s->test_460gx), &local_err) ||
        !qdev_realize(child, NULL, &local_err)) {
        goto fail;
    }
    return;

fail:
    child = s->io_test ? DEVICE(s->io_test) : NULL;
    io_qtest_remove_child(&child);
    s->io_test = NULL;
    child = s->test_460gx ? DEVICE(s->test_460gx) : NULL;
    io_qtest_remove_child(&child);
    s->test_460gx = NULL;
    error_propagate(errp, local_err);
}

static void io_qtest_unrealize(DeviceState *dev)
{
    IA64I2000IoQTestState *s = IA64_I2000_IO_QTEST(dev);
    DeviceState *child;

    child = s->io_test ? DEVICE(s->io_test) : NULL;
    io_qtest_remove_child(&child);
    s->io_test = NULL;
    child = s->test_460gx ? DEVICE(s->test_460gx) : NULL;
    io_qtest_remove_child(&child);
    s->test_460gx = NULL;
}

static const Property io_qtest_properties[] = {
    DEFINE_PROP_NETDEV(IA64_I2000_IO_TEST_PROP_I82559_NETDEV,
                       IA64I2000IoQTestState, i82559_peers),
};

static void io_qtest_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "IA-64 i2000 I/O qtest device";
    dc->realize = io_qtest_realize;
    dc->unrealize = io_qtest_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
    device_class_set_props(dc, io_qtest_properties);
}

static const TypeInfo io_test_types[] = {
    {
        .name = TYPE_IA64_I2000_IO_TEST_IFB_F0,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(IA64I2000IoTestIFBF0State),
        .class_init = io_test_ifb_f0_class_init,
        .interfaces =
            (const InterfaceInfo[]) {
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                {},
            },
    },
    {
        .name = TYPE_IA64_I2000_IO_TEST_IFB_F1,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(IA64I2000IoTestIFBF1State),
        .class_init = io_test_ifb_f1_class_init,
        .interfaces =
            (const InterfaceInfo[]) {
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                {},
            },
    },
    {
        .name = TYPE_IA64_I2000_IO_TEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64I2000IoTestState),
        .instance_init = io_test_init,
        .class_init = io_test_class_init,
    },
    {
        .name = TYPE_IA64_I2000_IO_QTEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64I2000IoQTestState),
        .class_init = io_qtest_class_init,
    },
};

DEFINE_TYPES(io_test_types)
