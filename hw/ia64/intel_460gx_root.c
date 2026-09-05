/*
 * Intel 460GX numbered PCI root infrastructure
 *
 * Each independent root reports the first bus number supplied by the board.
 * Device identity and address mapping are supplied separately.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_root.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_INTEL_460GX_ROOT_BUS "intel-460gx-root-bus"
OBJECT_DECLARE_SIMPLE_TYPE(Intel460GXRootBus, INTEL_460GX_ROOT_BUS)

struct Intel460GXRootBus {
    PCIBus parent_obj;

    uint8_t first_bus;
};

struct Intel460GXRootHostState {
    PCIHostState parent_obj;

    MemoryRegion pci_mem;
    MemoryRegion pci_io;
    qemu_irq intx[PCI_NUM_PINS];

    /* uint16_t permits an out-of-range sentinel for the required property. */
    uint16_t initial_first_bus;
    char root_bus_path[8];
};

static int intel_460gx_root_bus_num(PCIBus *bus)
{
    return INTEL_460GX_ROOT_BUS(bus)->first_bus;
}

static void intel_460gx_root_bus_class_init(ObjectClass *klass,
                                             const void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(klass);

    pbc->bus_num = intel_460gx_root_bus_num;
}

static const TypeInfo intel_460gx_root_bus_type_info = {
    .name = TYPE_INTEL_460GX_ROOT_BUS,
    .parent = TYPE_PCI_BUS,
    .instance_size = sizeof(Intel460GXRootBus),
    .class_init = intel_460gx_root_bus_class_init,
};

PCIBus *intel_460gx_numbered_root_bus_register(
    PCIHostState *host, const char *name,
    pci_set_irq_fn set_irq, pci_map_irq_fn map_irq, void *irq_opaque,
    MemoryRegion *mem, MemoryRegion *io, uint8_t devfn_min, int nirq,
    uint8_t first_bus, Error **errp)
{
    PCIBus *bus;

    if (host == NULL) {
        error_setg(errp, "460GX numbered PCI root requires a host");
        return NULL;
    }
    if (host->bus != NULL) {
        error_setg(errp, "460GX PCI host already owns a root bus");
        return NULL;
    }
    if (mem == NULL || io == NULL) {
        error_setg(errp,
                   "460GX numbered PCI root requires memory and I/O spaces");
        return NULL;
    }
    if (set_irq == NULL || map_irq == NULL || nirq <= 0) {
        error_setg(errp,
                   "460GX numbered PCI root requires interrupt routing");
        return NULL;
    }

    bus = pci_register_root_bus(DEVICE(host), name, set_irq, map_irq,
                                irq_opaque, mem, io, devfn_min, nirq,
                                TYPE_INTEL_460GX_ROOT_BUS);
    INTEL_460GX_ROOT_BUS(bus)->first_bus = first_bus;
    host->bus = bus;
    return bus;
}

void intel_460gx_numbered_root_bus_set_number(PCIBus *bus,
                                               uint8_t first_bus)
{
    if (bus && object_dynamic_cast(OBJECT(bus), TYPE_INTEL_460GX_ROOT_BUS)) {
        INTEL_460GX_ROOT_BUS(bus)->first_bus = first_bus;
    }
}

static void intel_460gx_root_set_irq(void *opaque, int irq_num, int level)
{
    Intel460GXRootHostState *s = opaque;

    if (irq_num >= 0 && irq_num < PCI_NUM_PINS) {
        qemu_set_irq(s->intx[irq_num], level);
    }
}

static int intel_460gx_root_map_irq(PCIDevice *pdev, int irq_num)
{
    (void)pdev;
    return intel_460gx_root_intx_index(irq_num);
}

static void intel_460gx_root_host_reset(DeviceState *dev)
{
    Intel460GXRootHostState *s = INTEL_460GX_ROOT_HOST(dev);
    PCIHostState *host = PCI_HOST_BRIDGE(dev);

    if (host->bus != NULL) {
        INTEL_460GX_ROOT_BUS(host->bus)->first_bus = s->initial_first_bus;
    }
}

static void intel_460gx_root_host_realize(DeviceState *dev, Error **errp)
{
    Intel460GXRootHostState *s = INTEL_460GX_ROOT_HOST(dev);
    PCIHostState *host = PCI_HOST_BRIDGE(dev);
    g_autofree char *bus_name = NULL;
    uint8_t first_bus;

    if (s->initial_first_bus > UINT8_MAX) {
        error_setg(errp, "%s is required (0..255)",
                   INTEL_460GX_ROOT_PROP_FIRST_BUS);
        return;
    }
    first_bus = s->initial_first_bus;

    memory_region_init(&s->pci_mem, OBJECT(dev),
                       TYPE_INTEL_460GX_ROOT_HOST ".pci-mem", UINT64_MAX);
    memory_region_init(&s->pci_io, OBJECT(dev),
                       TYPE_INTEL_460GX_ROOT_HOST ".pci-io", 0x10000);
    qdev_init_gpio_out_named(dev, s->intx, INTEL_460GX_ROOT_GPIO_INTX,
                             PCI_NUM_PINS);

    snprintf(s->root_bus_path, sizeof(s->root_bus_path), "0000:%02x",
             first_bus);
    bus_name = first_bus ? g_strdup_printf("pci.%u", first_bus) :
                           g_strdup("pci");
    intel_460gx_numbered_root_bus_register(
        host, bus_name, intel_460gx_root_set_irq, intel_460gx_root_map_irq, s,
        &s->pci_mem, &s->pci_io, PCI_DEVFN(0, 0), PCI_NUM_PINS,
        first_bus, errp);
}

static void intel_460gx_root_host_unrealize(DeviceState *dev)
{
    PCIHostState *host = PCI_HOST_BRIDGE(dev);

    if (host->bus != NULL) {
        pci_unregister_root_bus(host->bus);
        host->bus = NULL;
    }
}

static const char *intel_460gx_root_host_bus_path(PCIHostState *host,
                                                  PCIBus *root_bus)
{
    Intel460GXRootHostState *s = INTEL_460GX_ROOT_HOST(host);

    g_assert(root_bus == host->bus);
    return s->root_bus_path;
}

static const VMStateDescription vmstate_intel_460gx_root_host = {
    .name = TYPE_INTEL_460GX_ROOT_HOST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        /* The root number is immutable board wiring. */
        VMSTATE_UINT16_EQUAL(initial_first_bus, Intel460GXRootHostState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property intel_460gx_root_host_properties[] = {
    DEFINE_PROP_UINT16(INTEL_460GX_ROOT_PROP_FIRST_BUS,
                       Intel460GXRootHostState, initial_first_bus,
                       UINT16_MAX),
};

static void intel_460gx_root_host_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    dc->desc = "Intel 460GX internal numbered PCI root";
    dc->realize = intel_460gx_root_host_realize;
    dc->unrealize = intel_460gx_root_host_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_intel_460gx_root_host;
    device_class_set_legacy_reset(dc, intel_460gx_root_host_reset);
    device_class_set_props(dc, intel_460gx_root_host_properties);
    hc->root_bus_path = intel_460gx_root_host_bus_path;
}

static const TypeInfo intel_460gx_root_host_type_info = {
    .name = TYPE_INTEL_460GX_ROOT_HOST,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(Intel460GXRootHostState),
    .class_init = intel_460gx_root_host_class_init,
};

static void intel_460gx_root_register_types(void)
{
    type_register_static(&intel_460gx_root_bus_type_info);
    type_register_static(&intel_460gx_root_host_type_info);
}
type_init(intel_460gx_root_register_types)

PCIBus *intel_460gx_root_host_bus(Intel460GXRootHostState *s)
{
    return PCI_HOST_BRIDGE(s)->bus;
}

MemoryRegion *intel_460gx_root_host_mem(Intel460GXRootHostState *s)
{
    return &s->pci_mem;
}

MemoryRegion *intel_460gx_root_host_io(Intel460GXRootHostState *s)
{
    return &s->pci_io;
}

uint8_t intel_460gx_root_host_first_bus(const Intel460GXRootHostState *s)
{
    g_assert(s->initial_first_bus <= UINT8_MAX);
    return s->initial_first_bus;
}
