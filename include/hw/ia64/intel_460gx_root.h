/*
 * Intel 460GX internal numbered PCI root infrastructure
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_ROOT_H
#define HW_IA64_INTEL_460GX_ROOT_H

#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"

#define TYPE_INTEL_460GX_ROOT_HOST "intel-460gx-root-host"
OBJECT_DECLARE_SIMPLE_TYPE(Intel460GXRootHostState, INTEL_460GX_ROOT_HOST)

#define INTEL_460GX_ROOT_PROP_FIRST_BUS "first-bus"
#define INTEL_460GX_ROOT_GPIO_INTX      "intx"

/*
 * The root exposes raw INTA..INTD; bridges perform their own swizzle.
 */
static inline int intel_460gx_root_intx_index(int irq_num)
{
    return irq_num >= 0 && irq_num < PCI_NUM_PINS ? irq_num : -1;
}

/*
 * Register a root bus reporting @first_bus and assign it to @host->bus.  A
 * host may own one root bus.
 */
PCIBus *intel_460gx_numbered_root_bus_register(
    PCIHostState *host, const char *name,
    pci_set_irq_fn set_irq, pci_map_irq_fn map_irq, void *irq_opaque,
    MemoryRegion *mem, MemoryRegion *io, uint8_t devfn_min, int nirq,
    uint8_t first_bus, Error **errp);

/* Accessors for the non-user-creatable root host used by a board builder. */
PCIBus *intel_460gx_root_host_bus(Intel460GXRootHostState *s);
MemoryRegion *intel_460gx_root_host_mem(Intel460GXRootHostState *s);
MemoryRegion *intel_460gx_root_host_io(Intel460GXRootHostState *s);
uint8_t intel_460gx_root_host_first_bus(const Intel460GXRootHostState *s);
void intel_460gx_numbered_root_bus_set_number(PCIBus *bus,
                                               uint8_t first_bus);

#endif
