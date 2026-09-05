/*
 * HP zx1 MIO system bus device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX1_MIO_H
#define HW_PCI_HOST_HP_ZX1_MIO_H

#include "hw/core/sysbus.h"
#include "hw/ia64/ia64_ras.h"
#include "hw/pci-host/hp-io-sapic.h"

#define TYPE_HP_ZX1_MIO "hp-zx1-mio"
#define TYPE_HP_ZX2_MIO "hp-zx2-mio"
OBJECT_DECLARE_SIMPLE_TYPE(HPZX1MIOState, HP_ZX1_MIO)

typedef struct HPZX1IOAState HPZX1IOAState;

typedef struct HPZX1MIOIOMMUResetConfig {
    uint64_t ibase;
    uint64_t imask;
    uint64_t pcom;
    uint64_t tcnfg;
    uint64_t pdir_base;
} HPZX1MIOIOMMUResetConfig;

/* Must be called exactly once, before realizing the internal device. */
bool hp_zx1_mio_configure_iommu_reset(
    HPZX1MIOState *s, const HPZX1MIOIOMMUResetConfig *config,
    Error **errp);

bool hp_zx1_mio_set_fault_notifier(HPZX1MIOState *s,
                                    IA64ChipsetFaultNotify notify,
                                    void *opaque, Error **errp);
bool hp_zx1_mio_set_error_delivery(HPZX1MIOState *s,
                                    HPIOSAPICDeliver deliver,
                                    void *opaque, Error **errp);

/*
 * Attach a root-private view of the shared IOC translator to an empty,
 * unowned PCI root.  The MIO retains the bus until detach; failures leave
 * ownership unchanged.  All devices use dvi=false.
 */
bool hp_zx1_mio_attach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                Error **errp);

/* The rope mask selects the zx2 translation context for this root. */
bool hp_zx2_mio_attach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                uint16_t ropes, Error **errp);

/*
 * Attach a Mercury root through a root-private DMA frontend.  The frontend
 * overlays that IOA's programmable MSI window on the otherwise shared IOC
 * IOMMU, so all ordinary DMA still uses the one MIO translation cache.
 */
bool hp_zx1_mio_attach_ioa(HPZX1MIOState *s, HPZX1IOAState *ioa,
                           Error **errp);
bool hp_zx1_mio_detach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                Error **errp);

/* Available only after reset configuration and successful realization. */
AddressSpace *hp_zx1_mio_iommu_address_space(HPZX1MIOState *s);

#endif
