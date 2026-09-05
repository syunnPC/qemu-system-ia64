/*
 * HP zx1 Mercury I/O adapter QOM wrapper
 *
 * The board supplies placement, bus numbering, rope selection, and interrupt
 * routing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX1_IOA_H
#define HW_PCI_HOST_HP_ZX1_IOA_H

#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"

#define TYPE_HP_ZX1_IOA "hp-zx1-ioa"
OBJECT_DECLARE_SIMPLE_TYPE(HPZX1IOAState, HP_ZX1_IOA)

/* Always ten named pins; AGP mode rejects pins 7..9. */
#define HP_ZX1_IOA_GPIO_INTX "intx"

typedef struct HPZX1IOASetup {
    HPZX1IOAMode mode;
    uint8_t rope_mask;

    /*
     * The numbered root bus starts at secondary_bus.  Mercury selector bus
     * zero remains the type-0 form and maps to that root bus.
     */
    uint8_t secondary_bus;
    uint8_t subordinate_bus;

    bool pci_reset_asserted;
    uint64_t bus_mode_reset;
    uint64_t slave_control_reset_straps;
    uint32_t error_configuration_reset_straps;

    /*
     * Every [slot][INTA..INTD] entry is required and names one of this mode's
     * logical Mercury inputs.
     */
    uint8_t intx_route[PCI_SLOT_MAX][PCI_NUM_PINS];

    /*
     * The core builds the interrupt address/data pair.  The board callback
     * chooses how to deliver it; the wrapper supplies no interrupt target.
     */
    HPIOSAPICDeliver deliver;
    void *delivery_opaque;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
} HPZX1IOASetup;

/* Must be called exactly once, before realizing the internal device. */
bool hp_zx1_ioa_setup(HPZX1IOAState *s, const HPZX1IOASetup *setup,
                      Error **errp);

PCIBus *hp_zx1_ioa_bus(HPZX1IOAState *s);
MemoryRegion *hp_zx1_ioa_pci_mem(HPZX1IOAState *s);
MemoryRegion *hp_zx1_ioa_pci_io(HPZX1IOAState *s);
uint8_t hp_zx1_ioa_root_bus_num(const HPZX1IOAState *s);
uint8_t hp_zx1_ioa_rope_mask(const HPZX1IOAState *s);

/* Internal MIO-owned DMA frontend lifecycle; the root must still be empty. */
bool hp_zx1_ioa_attach_msi_window(HPZX1IOAState *s,
                                  MemoryRegion *dma_root, Error **errp);
void hp_zx1_ioa_detach_msi_window(HPZX1IOAState *s,
                                  MemoryRegion *dma_root);

#endif
