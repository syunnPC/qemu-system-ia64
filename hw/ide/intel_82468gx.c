/*
 * Intel 82468GX IDE controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ide/pci.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/southbridge/intel_82468gx.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

#include "ide-internal.h"
#include "trace.h"

static uint64_t ifb_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    BMDMAState *bm = opaque;
    uint8_t value;

    if (size != 1) {
        return MAKE_64BIT_MASK(0, size * 8);
    }
    switch (addr & 3) {
    case 0:
        value = bm->cmd;
        break;
    case 2:
        value = bm->status;
        break;
    default:
        value = UINT8_MAX;
        break;
    }
    trace_bmdma_read(addr, value);
    return value;
}

static void ifb_bmdma_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    BMDMAState *bm = opaque;

    if (size != 1) {
        return;
    }
    trace_bmdma_write(addr, value);
    switch (addr & 3) {
    case 0:
        bmdma_cmd_writeb(bm, value);
        break;
    case 2:
        bmdma_status_writeb(bm, value);
        break;
    }
}

static const MemoryRegionOps ifb_bmdma_ops = {
    .read = ifb_bmdma_read,
    .write = ifb_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void ifb_bmdma_bar_init(PCIIDEState *d)
{
    unsigned i;

    memory_region_init(&d->bmdma_bar, OBJECT(d),
                       TYPE_INTEL_82468GX_IFB_IDE ".bmdma", 16);
    for (i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, OBJECT(d), &ifb_bmdma_ops, bm,
                              TYPE_INTEL_82468GX_IFB_IDE ".channel", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm,
                              TYPE_INTEL_82468GX_IFB_IDE ".descriptor", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static void ifb_ide_reset(DeviceState *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    PCIDevice *pci = PCI_DEVICE(dev);
    unsigned i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }
    pci_set_word(pci->config + PCI_COMMAND, 0);
    pci_set_word(pci->config + PCI_STATUS, 0x0280);
    pci->config[PCI_LATENCY_TIMER] = 0;
    pci_set_long(pci->config + PCI_BASE_ADDRESS_4, 1);
    pci_set_long(pci->config + 0x40, 0);
    pci->config[0x44] = 0;
    pci->config[0x48] = 0;
    pci_set_word(pci->config + 0x4a, 0);
}

static void ifb_ide_init_bus(PCIIDEState *d, ISABus *isa_bus, unsigned channel)
{
    static const struct {
        uint16_t command;
        uint16_t control;
        uint8_t irq;
    } ports[] = {
        { 0x1f0, 0x3f6, 14 },
        { 0x170, 0x376, 15 },
    };
    IDEBus *bus = &d->bus[channel];

    ide_bus_init(bus, sizeof(*bus), DEVICE(d), channel, 2);
    portio_list_init(&bus->portio_list, OBJECT(d), ide_portio_list, bus,
                     TYPE_INTEL_82468GX_IFB_IDE ".command");
    portio_list_add(&bus->portio_list, pci_address_space_io(PCI_DEVICE(d)),
                    ports[channel].command);
    portio_list_init(&bus->portio2_list, OBJECT(d), ide_portio2_list, bus,
                     TYPE_INTEL_82468GX_IFB_IDE ".control");
    portio_list_add(&bus->portio2_list, pci_address_space_io(PCI_DEVICE(d)),
                    ports[channel].control);
    ide_bus_init_output_irq(bus, isa_bus_get_irq(isa_bus, ports[channel].irq));
    bmdma_init(bus, &d->bmdma[channel], d);
    ide_bus_register_restart_cb(bus);
}

static void ifb_ide_realize(PCIDevice *pci, Error **errp)
{
    PCIIDEState *d = PCI_IDE(pci);
    PCIDevice *function_zero = pci_get_function_0(pci);
    ISABus *isa_bus;

    if (!function_zero ||
        !object_dynamic_cast(OBJECT(function_zero), TYPE_INTEL_82468GX_IFB)) {
        error_setg(errp, "%s requires an 82468GX IFB function zero",
                   TYPE_INTEL_82468GX_IFB_IDE);
        return;
    }
    isa_bus = intel_82468gx_ifb_isa_bus(INTEL_82468GX_IFB(function_zero));
    if (!isa_bus) {
        error_setg(errp, "%s requires a realized IFB ISA bus",
                   TYPE_INTEL_82468GX_IFB_IDE);
        return;
    }

    /*
     * The private timing/configuration fields are compatibility readback
     * latches; their transfer-timing effects are not implemented.
     */
    memset(pci->wmask, 0, pci_config_size(pci));
    memset(pci->w1cmask, 0, pci_config_size(pci));
    pci->config[PCI_CLASS_PROG] = 0x80;
    pci_set_word(pci->wmask + PCI_COMMAND, BIT(2) | BIT(0));
    pci_set_word(pci->w1cmask + PCI_STATUS, BIT(13) | BIT(12) | BIT(11));
    pci->wmask[PCI_LATENCY_TIMER] = 0xf0;
    pci_set_word(pci->wmask + 0x40, 0xf3ff);
    pci_set_word(pci->wmask + 0x42, 0xf3ff);
    pci->wmask[0x44] = 0xff;
    pci->wmask[0x48] = 0x0f;
    pci_set_word(pci->wmask + 0x4a, 0x3333);

    ifb_bmdma_bar_init(d);
    pci_register_bar(pci, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);
    ifb_ide_init_bus(d, isa_bus, 0);
    ifb_ide_init_bus(d, isa_bus, 1);
    ifb_ide_reset(DEVICE(pci));
}

static void ifb_ide_exit(PCIDevice *pci)
{
    PCIIDEState *d = PCI_IDE(pci);
    unsigned i;

    for (i = 0; i < 2; i++) {
        if (d->bus[i].portio2_list.owner) {
            portio_list_del(&d->bus[i].portio2_list);
            portio_list_destroy(&d->bus[i].portio2_list);
        }
        if (d->bus[i].portio_list.owner) {
            portio_list_del(&d->bus[i].portio_list);
            portio_list_destroy(&d->bus[i].portio_list);
        }
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
}

static void ifb_ide_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = ifb_ide_realize;
    pc->exit = ifb_ide_exit;
    pc->vendor_id = INTEL_82468GX_IFB_VENDOR_ID;
    pc->device_id = INTEL_82468GX_IFB_IDE_DEVICE_ID;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_STORAGE_IDE;
    dc->desc = "Intel 82468GX IDE controller";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_ide_pci;
    device_class_set_legacy_reset(dc, ifb_ide_reset);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo ifb_ide_type_info = {
    .name = TYPE_INTEL_82468GX_IFB_IDE,
    .parent = TYPE_PCI_IDE,
    .class_init = ifb_ide_class_init,
};

static void ifb_ide_register_types(void)
{
    type_register_static(&ifb_ide_type_info);
}
type_init(ifb_ide_register_types)
