/*
 * Intel 460GX inbound PCI DMA aperture core
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_DMA_H
#define HW_IA64_INTEL_460GX_DMA_H

#include "hw/ia64/ia64_ras.h"
#include "qemu/typedefs.h"

#define INTEL_460GX_DMA_ADDRESS_BITS 36
#define INTEL_460GX_DMA_ADDRESS_LIMIT \
    (UINT64_C(1) << INTEL_460GX_DMA_ADDRESS_BITS)

typedef struct Intel460GXDMA Intel460GXDMA;

/*
 * Construct one immutable-per-root DMA view.  Aperture (0, 0) is the
 * explicit deny-all configuration.
 */
Intel460GXDMA *intel_460gx_dma_new(uint64_t aperture_base,
                                  uint64_t aperture_size,
                                  Error **errp);

/*
 * Add a direct IOVA-to-RAM alias.  The target must be ordinary writable RAM,
 * must outlive the DMA view, and must not shrink while the view is attached.
 * Both ranges use exclusive-end bounds.
 */
bool intel_460gx_dma_add_ram_alias(Intel460GXDMA *dma,
                                   uint64_t dma_base,
                                   uint64_t size,
                                   MemoryRegion *target,
                                   uint64_t target_offset,
                                   Error **errp);

bool intel_460gx_dma_add_pci_window_alias(Intel460GXDMA *dma,
                                          uint64_t dma_base,
                                          uint64_t size,
                                          MemoryRegion *pci_memory,
                                          uint64_t pci_offset,
                                          Error **errp);

/*
 * Seal the view before attaching it to a root PCI bus.  Once sealed, its
 * aperture and aliases cannot change.  Attach must happen before any PCI
 * device is realized on the bus.
 */
bool intel_460gx_dma_seal(Intel460GXDMA *dma, Error **errp);
void intel_460gx_dma_set_fault_notify(Intel460GXDMA *dma,
                                      IA64ChipsetFaultNotify notify,
                                      void *opaque);
bool intel_460gx_dma_attach_root(Intel460GXDMA *dma, PCIBus *bus,
                                Error **errp);

AddressSpace *intel_460gx_dma_address_space(Intel460GXDMA *dma);
uint64_t intel_460gx_dma_aperture_base(const Intel460GXDMA *dma);
uint64_t intel_460gx_dma_aperture_size(const Intel460GXDMA *dma);
size_t intel_460gx_dma_alias_count(const Intel460GXDMA *dma);
bool intel_460gx_dma_is_sealed(const Intel460GXDMA *dma);

/*
 * PCI devices must be unrealized before destroying an attached view.  The
 * implementation retains the bus object until this teardown completes.
 * On failure the view and its bus attachment are left unchanged so teardown
 * can be retried after the caller fixes the ordering or ownership conflict.
 */
bool intel_460gx_dma_destroy(Intel460GXDMA *dma, Error **errp);

#endif
