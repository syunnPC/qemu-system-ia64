/*
 * Intel 82468GX I/O and Firmware Bridge
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/acpi/acpi.h"
#include "hw/core/irq.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/ide/pci.h"
#include "hw/intc/i8259.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/southbridge/intel_82468gx.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/runstate.h"

typedef struct Intel82468GXSMBusState {
    PCIDevice parent_obj;

    PMSMBus smb;
    MemoryRegion io_alias;
    qemu_irq smi;
} Intel82468GXSMBusState;

#define INTEL_82468GX_IFB_SMBUS(obj) \
    OBJECT_CHECK(Intel82468GXSMBusState, (obj), \
                 TYPE_INTEL_82468GX_IFB_SMBUS)

struct Intel82468GXIFBState {
    PCIDevice parent_obj;

    PCIDevice *functions[INTEL_82468GX_IFB_FUNCTIONS];
    ISABus *isa_bus;
    qemu_irq legacy_irq;
    qemu_irq isa_irq[ISA_NUM_IRQS];
    qemu_irq sci;
    qemu_irq *pic_irqs;
    qemu_irq *isa_irqs;
    PICCommonState *master_pic;
    MemoryRegion acpi_pm;
    MemoryRegion acpi_gpe;
    ACPIREGS acpi_regs;
    Notifier powerdown_notifier;
};

#define IFB_ACPI_PM_IO_SIZE 0x40
#define IFB_ACPI_GPE_OFFSET 0x0c
#define IFB_ACPI_GPE_LENGTH 4

static void ifb_acpi_update_sci(ACPIREGS *ar)
{
    Intel82468GXIFBState *s = container_of(ar, Intel82468GXIFBState, acpi_regs);

    if (ar->pm1.cnt.cnt & ACPI_BITMASK_SCI_ENABLE) {
        acpi_update_sci(ar, s->sci);
    } else {
        qemu_set_irq(s->sci, 0);
    }
}

static void ifb_isa_irq_handler(void *opaque, int irq, int level)
{
    Intel82468GXIFBState *s = opaque;

    qemu_set_irq(s->pic_irqs[irq], level);
    qemu_set_irq(s->isa_irq[irq], level);
}

static uint64_t ifb_acpi_gpe_read(void *opaque, hwaddr addr, unsigned size)
{
    Intel82468GXIFBState *s = opaque;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)acpi_gpe_ioport_readb(&s->acpi_regs, addr + i)
                 << (i * 8);
    }
    return value;
}

static void ifb_acpi_gpe_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    Intel82468GXIFBState *s = opaque;
    unsigned i;

    for (i = 0; i < size; i++) {
        acpi_gpe_ioport_writeb(&s->acpi_regs, addr + i, value >> (i * 8));
    }
    ifb_acpi_update_sci(&s->acpi_regs);
}

static const MemoryRegionOps ifb_acpi_gpe_ops = {
    .read = ifb_acpi_gpe_read,
    .write = ifb_acpi_gpe_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void ifb_acpi_io_update(Intel82468GXIFBState *s)
{
    PCIDevice *pci = PCI_DEVICE(s);
    uint32_t base = pci_get_long(pci->config + 0x40) & 0xffc0U;

    memory_region_transaction_begin();
    memory_region_set_address(&s->acpi_pm, base);
    memory_region_set_enabled(&s->acpi_pm,
                              base != 0 && (pci->config[0x44] & BIT(0)));
    memory_region_transaction_commit();
}

static void ifb_acpi_powerdown(Notifier *notifier, void *opaque)
{
    Intel82468GXIFBState *s =
        container_of(notifier, Intel82468GXIFBState, powerdown_notifier);

    (void)opaque;
    acpi_pm1_evt_power_down(&s->acpi_regs);
}

static void ifb_lpc_reset(DeviceState *dev)
{
    Intel82468GXIFBState *s = INTEL_82468GX_IFB(dev);
    PCIDevice *pci = PCI_DEVICE(s);

    pci_set_word(pci->config + PCI_COMMAND, 0x0007);
    pci_set_word(pci->config + PCI_STATUS, 0x0280);
    pci_set_long(pci->config + 0x40, 0x00000001);
    pci->config[0x44] = 0;
    pci->config[0x45] = 0;
    pci_set_word(pci->config + 0x4e, 0x07c1);
    memset(pci->config + 0x60, 0x80, 4);
    pci->config[0x64] = 0x10;
    pci->config[0x69] = 0x02;
    pci_set_word(pci->config + 0x6a, 0);
    pci->config[0x82] = 0;
    pci_set_word(pci->config + 0x84, 0x0500);
    pci_set_word(pci->config + 0x90, 0);
    pci_set_long(pci->config + 0x92, 0);
    pci->config[0xc8] = 0;
    pci_set_long(pci->config + 0xd0, 1);
    pci->config[0xd4] = 0;
    memset(pci->config + 0xe0, 0, 9);
    pci_set_long(pci->config + 0xe8, 0x00112233);

    acpi_pm1_evt_reset(&s->acpi_regs);
    acpi_pm1_cnt_reset(&s->acpi_regs);
    acpi_pm_tmr_reset(&s->acpi_regs);
    acpi_gpe_reset(&s->acpi_regs);
    s->acpi_regs.gpe.sts[1] = BIT(3);
    ifb_acpi_update_sci(&s->acpi_regs);
    ifb_acpi_io_update(s);
}

static void ifb_lpc_write_config(PCIDevice *pci, uint32_t address,
                                 uint32_t value, int length)
{
    uint16_t biosen = pci_get_word(pci->config + 0x4e);
    uint8_t rtccfg = pci->config[0xc8];

    pci_default_write_config(pci, address, value, length);
    if (biosen & BIT(15)) {
        pci_word_test_and_set_mask(pci->config + 0x4e, BIT(15));
    }
    pci->config[0xc8] |= rtccfg & (BIT(4) | BIT(3));
    if (ranges_overlap(address, length, 0x40, 5)) {
        ifb_acpi_io_update(INTEL_82468GX_IFB(pci));
    }
}

static void ifb_lpc_init_config(PCIDevice *pci)
{
    /*
     * ACPI decode and BIOS/RTC locks have modeled side effects. The other
     * writable chipset fields are compatibility readback latches; their
     * hardware effects are not implemented.
     */
    memset(pci->wmask, 0, pci_config_size(pci));
    memset(pci->w1cmask, 0, pci_config_size(pci));
    pci_set_word(pci->wmask + PCI_COMMAND, BIT(8) | BIT(3));
    pci_set_word(pci->w1cmask + PCI_STATUS,
                 BIT(14) | BIT(13) | BIT(12) | BIT(11));
    pci_set_long(pci->wmask + 0x40, 0x0000ffc0);
    pci->wmask[0x44] = BIT(0);
    pci->wmask[0x45] = 0x07;
    pci_set_word(pci->wmask + 0x4e, BIT(15) | BIT(2));
    memset(pci->wmask + 0x60, 0x8f, 4);
    pci->wmask[0x64] = 0xff;
    pci->wmask[0x69] = 0xf0;
    pci->wmask[0x6a] = BIT(0);
    pci->w1cmask[0x6b] = BIT(7);
    pci->wmask[0x82] = 0x0f;
    pci_set_long(pci->wmask + 0x92, 0xffc0ffc0);
    pci->wmask[0xc8] = BIT(4) | BIT(3) | BIT(2);
    pci_set_long(pci->wmask + 0xd0, 0x0000ffc0);
    pci->wmask[0xd4] = BIT(0);
    pci->wmask[0xe0] = 0x77;
    pci->wmask[0xe1] = 0x13;
    pci->wmask[0xe2] = 0x3b;
    pci->wmask[0xe3] = 0xff;
    pci_set_word(pci->wmask + 0xe4, 0xfe01);
    pci_set_word(pci->wmask + 0xe6, 0x1fff);
    pci_set_long(pci->wmask + 0xe8, UINT32_MAX);
}

static PCIDevice *ifb_realize_function(Intel82468GXIFBState *s,
                                       unsigned function, const char *name,
                                       const char *type, Error **errp)
{
    PCIDevice *pci = pci_new(s->parent_obj.devfn + function, type);

    object_property_add_child(OBJECT(s), name, OBJECT(pci));
    object_unref(OBJECT(pci));
    if (!qdev_realize(DEVICE(pci), BUS(pci_get_bus(&s->parent_obj)), errp)) {
        object_unparent(OBJECT(pci));
        return NULL;
    }
    s->functions[function] = pci;
    return pci;
}

static void ifb_remove_function(PCIDevice **pci)
{
    if (!*pci) {
        return;
    }
    if (qdev_is_realized(DEVICE(*pci))) {
        qdev_unrealize(DEVICE(*pci));
    }
    object_unparent(OBJECT(*pci));
    *pci = NULL;
}

static void ifb_lpc_realize(PCIDevice *pci, Error **errp)
{
    Intel82468GXIFBState *s = INTEL_82468GX_IFB(pci);

    if (PCI_FUNC(pci->devfn) != 0 ||
        !(pci->config[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MULTI_FUNCTION)) {
        error_setg(errp, "%s must occupy function 0 of a multifunction slot",
                   TYPE_INTEL_82468GX_IFB);
        return;
    }

    ifb_lpc_init_config(pci);
    s->functions[0] = pci;
    s->isa_bus = isa_bus_new_non_default(DEVICE(s), pci_address_space(pci),
                                         pci_address_space_io(pci));
    s->pic_irqs = i8259_init_pair(s->isa_bus, s->legacy_irq, &s->master_pic);
    s->isa_irqs = qemu_allocate_irqs(ifb_isa_irq_handler, s, ISA_NUM_IRQS);
    isa_bus_register_input_irqs(s->isa_bus, s->isa_irqs);

    memory_region_init(&s->acpi_pm, OBJECT(s),
                       TYPE_INTEL_82468GX_IFB ".acpi-pm", IFB_ACPI_PM_IO_SIZE);
    memory_region_add_subregion(pci_address_space_io(pci), 0, &s->acpi_pm);
    memory_region_set_enabled(&s->acpi_pm, false);
    acpi_pm1_evt_init(&s->acpi_regs, ifb_acpi_update_sci, &s->acpi_pm);
    acpi_pm1_cnt_init(&s->acpi_regs, &s->acpi_pm, false, false, 4, false);
    acpi_pm_tmr_init(&s->acpi_regs, ifb_acpi_update_sci, &s->acpi_pm);
    acpi_gpe_init(&s->acpi_regs, IFB_ACPI_GPE_LENGTH);
    memory_region_init_io(&s->acpi_gpe, OBJECT(s), &ifb_acpi_gpe_ops, s,
                          TYPE_INTEL_82468GX_IFB ".acpi-gpe",
                          IFB_ACPI_GPE_LENGTH);
    memory_region_add_subregion(&s->acpi_pm, IFB_ACPI_GPE_OFFSET, &s->acpi_gpe);
    s->powerdown_notifier.notify = ifb_acpi_powerdown;
    qemu_register_powerdown_notifier(&s->powerdown_notifier);
    ifb_lpc_reset(DEVICE(pci));

    if (!ifb_realize_function(s, 1, "ide", TYPE_INTEL_82468GX_IFB_IDE, errp) ||
        !ifb_realize_function(s, 2, "usb", TYPE_INTEL_82468GX_IFB_USB, errp) ||
        !ifb_realize_function(s, 3, "smbus", TYPE_INTEL_82468GX_IFB_SMBUS,
                              errp)) {
        ifb_remove_function(&s->functions[3]);
        ifb_remove_function(&s->functions[2]);
        ifb_remove_function(&s->functions[1]);
        if (s->isa_irqs) {
            qemu_free_irqs(s->isa_irqs, ISA_NUM_IRQS);
            s->isa_irqs = NULL;
        }
        g_clear_pointer(&s->pic_irqs, g_free);
    }
}

static void ifb_lpc_exit(PCIDevice *pci)
{
    Intel82468GXIFBState *s = INTEL_82468GX_IFB(pci);

    ifb_remove_function(&s->functions[3]);
    ifb_remove_function(&s->functions[2]);
    ifb_remove_function(&s->functions[1]);
    if (s->isa_irqs) {
        qemu_free_irqs(s->isa_irqs, ISA_NUM_IRQS);
        s->isa_irqs = NULL;
    }
    g_clear_pointer(&s->pic_irqs, g_free);
    s->isa_bus = NULL;
    s->master_pic = NULL;
}

static int ifb_lpc_post_load(void *opaque, int version_id)
{
    Intel82468GXIFBState *s = opaque;
    uint16_t pm_enable = s->acpi_regs.pm1.evt.en;

    (void)version_id;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC,
                              (pm_enable & ACPI_BITMASK_RT_CLOCK_ENABLE) != 0);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER,
                              (pm_enable & ACPI_BITMASK_TIMER_ENABLE) != 0);
    ifb_acpi_io_update(s);
    ifb_acpi_update_sci(&s->acpi_regs);
    return 0;
}

static const VMStateDescription vmstate_ifb_lpc = {
    .name = TYPE_INTEL_82468GX_IFB,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ifb_lpc_post_load,
    .fields =
        (const VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, Intel82468GXIFBState),
            VMSTATE_UINT16(acpi_regs.pm1.evt.sts, Intel82468GXIFBState),
            VMSTATE_UINT16(acpi_regs.pm1.evt.en, Intel82468GXIFBState),
            VMSTATE_UINT16(acpi_regs.pm1.cnt.cnt, Intel82468GXIFBState),
            VMSTATE_TIMER_PTR(acpi_regs.tmr.timer, Intel82468GXIFBState),
            VMSTATE_INT64(acpi_regs.tmr.overflow_time, Intel82468GXIFBState),
            VMSTATE_BUFFER_POINTER_UNSAFE(acpi_regs.gpe.sts,
                                          Intel82468GXIFBState, 1,
                                          IFB_ACPI_GPE_LENGTH),
            VMSTATE_BUFFER_POINTER_UNSAFE(
                acpi_regs.gpe.en, Intel82468GXIFBState, 1, IFB_ACPI_GPE_LENGTH),
            VMSTATE_END_OF_LIST() },
};

static void ifb_lpc_init(Object *obj)
{
    Intel82468GXIFBState *s = INTEL_82468GX_IFB(obj);

    qdev_init_gpio_out_named(DEVICE(obj), &s->legacy_irq,
                             INTEL_82468GX_IFB_GPIO_LEGACY, 1);
    qdev_init_gpio_out_named(DEVICE(obj), s->isa_irq,
                             INTEL_82468GX_IFB_GPIO_ISA_IRQ, ISA_NUM_IRQS);
    qdev_init_gpio_out_named(DEVICE(obj), &s->sci, INTEL_82468GX_IFB_GPIO_SCI,
                             1);
}

static void ifb_lpc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = ifb_lpc_realize;
    pc->exit = ifb_lpc_exit;
    pc->config_write = ifb_lpc_write_config;
    pc->vendor_id = INTEL_82468GX_IFB_VENDOR_ID;
    pc->device_id = INTEL_82468GX_IFB_LPC_DEVICE_ID;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->desc = "Intel 82468GX I/O and Firmware Bridge";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_ifb_lpc;
    device_class_set_legacy_reset(dc, ifb_lpc_reset);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static void ifb_smbus_set_irq(PMSMBus *smb, bool level)
{
    Intel82468GXSMBusState *s = smb->opaque;
    PCIDevice *pci = PCI_DEVICE(s);
    bool smi = pci->config[0x40] & BIT(1);

    pci_set_irq(pci, level && !smi);
    qemu_set_irq(s->smi, level && smi);
}

static void ifb_smbus_reset(DeviceState *dev)
{
    Intel82468GXSMBusState *s = INTEL_82468GX_IFB_SMBUS(dev);
    PCIDevice *pci = PCI_DEVICE(dev);

    pci_set_word(pci->config + PCI_COMMAND, 0);
    pci_set_word(pci->config + PCI_STATUS, 0x0280);
    pci->config[PCI_INTERRUPT_LINE] = 0;
    pci_config_set_interrupt_pin(pci->config, 2);
    memset(pci->config + 0x40, 0, 4);
    if (s->smb.reset) {
        s->smb.reset(&s->smb);
    }
    ifb_smbus_set_irq(&s->smb, false);
}

static void ifb_smbus_write_config(PCIDevice *pci, uint32_t address,
                                   uint32_t value, int length)
{
    Intel82468GXSMBusState *s = INTEL_82468GX_IFB_SMBUS(pci);

    pci_default_write_config(pci, address, value, length);
    ifb_smbus_set_irq(&s->smb, s->smb.smb_stat & 0x1e);
}

static void ifb_smbus_realize(PCIDevice *pci, Error **errp)
{
    Intel82468GXSMBusState *s = INTEL_82468GX_IFB_SMBUS(pci);

    (void)errp;
    memset(pci->wmask, 0, pci_config_size(pci));
    memset(pci->w1cmask, 0, pci_config_size(pci));
    pci_set_word(pci->wmask + PCI_COMMAND, BIT(3) | BIT(1) | BIT(0));
    pci_set_word(pci->w1cmask + PCI_STATUS, BIT(11));
    pci->wmask[PCI_LATENCY_TIMER] = 0;
    pci->wmask[PCI_INTERRUPT_LINE] = 0xff;
    pci->wmask[0x40] = 0x03;
    memset(pci->wmask + 0x41, 0xff, 3);

    pm_smbus_init(DEVICE(s), &s->smb, false);
    s->smb.opaque = s;
    s->smb.set_irq = ifb_smbus_set_irq;
    memory_region_init_alias(&s->io_alias, OBJECT(s),
                             TYPE_INTEL_82468GX_IFB_SMBUS ".io", &s->smb.io, 0,
                             0x10);
    pci_register_bar(pci, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->io_alias);
    ifb_smbus_reset(DEVICE(pci));
}

static int ifb_smbus_post_load(void *opaque, int version_id)
{
    Intel82468GXSMBusState *s = opaque;

    (void)version_id;
    ifb_smbus_set_irq(&s->smb, s->smb.smb_stat & 0x1e);
    return 0;
}

static const VMStateDescription vmstate_ifb_smbus = {
    .name = TYPE_INTEL_82468GX_IFB_SMBUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ifb_smbus_post_load,
    .fields =
        (const VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, Intel82468GXSMBusState),
            VMSTATE_STRUCT(smb, Intel82468GXSMBusState, 1, pmsmb_vmstate,
                           PMSMBus),
            VMSTATE_END_OF_LIST() },
};

static void ifb_smbus_init(Object *obj)
{
    Intel82468GXSMBusState *s = INTEL_82468GX_IFB_SMBUS(obj);

    qdev_init_gpio_out_named(DEVICE(obj), &s->smi, INTEL_82468GX_IFB_GPIO_SMI,
                             1);
}

static void ifb_smbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = ifb_smbus_realize;
    pc->config_write = ifb_smbus_write_config;
    pc->vendor_id = INTEL_82468GX_IFB_VENDOR_ID;
    pc->device_id = INTEL_82468GX_IFB_SMBUS_DEVICE_ID;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_SERIAL_SMBUS;
    dc->desc = "Intel 82468GX SMBus controller";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_ifb_smbus;
    device_class_set_legacy_reset(dc, ifb_smbus_reset);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo intel_82468gx_ifb_types[] = {
    {
        .name = TYPE_INTEL_82468GX_IFB,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Intel82468GXIFBState),
        .instance_init = ifb_lpc_init,
        .class_init = ifb_lpc_class_init,
        .interfaces =
            (const InterfaceInfo[]) {
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                {},
            },
    },
    {
        .name = TYPE_INTEL_82468GX_IFB_SMBUS,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Intel82468GXSMBusState),
        .instance_init = ifb_smbus_init,
        .class_init = ifb_smbus_class_init,
        .interfaces =
            (const InterfaceInfo[]) {
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                {},
            },
    },
};

DEFINE_TYPES(intel_82468gx_ifb_types)

Intel82468GXIFBState *intel_82468gx_ifb_create(PCIBus *bus, int devfn,
                                               Error **errp)
{
    PCIDevice *pci;

    if (!bus) {
        error_setg(errp, "82468GX IFB requires a PCI bus");
        return NULL;
    }
    if (devfn < 0 || devfn >= PCI_DEVFN_MAX || PCI_FUNC(devfn) != 0) {
        error_setg(errp, "82468GX IFB requires a function-zero devfn");
        return NULL;
    }

    pci = pci_new_multifunction(devfn, TYPE_INTEL_82468GX_IFB);
    if (!pci_realize_and_unref(pci, bus, errp)) {
        return NULL;
    }
    return INTEL_82468GX_IFB(pci);
}

PCIDevice *intel_82468gx_ifb_function(Intel82468GXIFBState *s,
                                      unsigned function)
{
    return s && function < INTEL_82468GX_IFB_FUNCTIONS ? s->functions[function]
                                                       : NULL;
}

ISABus *intel_82468gx_ifb_isa_bus(Intel82468GXIFBState *s)
{
    return s ? s->isa_bus : NULL;
}

IDEBus *intel_82468gx_ifb_ide_bus(Intel82468GXIFBState *s, unsigned channel)
{
    PCIDevice *pci;

    if (!s || channel >= 2) {
        return NULL;
    }
    pci = s->functions[1];
    return pci ? &PCI_IDE(pci)->bus[channel] : NULL;
}

I2CBus *intel_82468gx_ifb_smbus(Intel82468GXIFBState *s)
{
    Intel82468GXSMBusState *sm;

    if (!s || !s->functions[3]) {
        return NULL;
    }
    sm = INTEL_82468GX_IFB_SMBUS(s->functions[3]);
    return sm->smb.smbus;
}

int intel_82468gx_ifb_pic_read_irq(Intel82468GXIFBState *s)
{
    return s && s->master_pic ? pic_read_irq(s->master_pic) : -1;
}

void intel_82468gx_ifb_configure_acpi(Intel82468GXIFBState *s, uint16_t io_base)
{
    PCIDevice *pci;

    g_return_if_fail(s != NULL);
    g_return_if_fail(io_base != 0 && (io_base & 0x3fU) == 0);
    pci = PCI_DEVICE(s);
    ifb_lpc_write_config(pci, 0x40, io_base | 1U, 4);
    ifb_lpc_write_config(pci, 0x44, 1U, 1);
}
