/*
 * HP System Bus Adapter IOMMU helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-sba-iommu.h"
#include "qemu/host-utils.h"

#define HP_ZX1_IMASK_DAC UINT64_C(0xffffffff00000000)
#define HP_ZX1_PCOM_SIZE_MASK UINT64_C(0x1f)

bool hp_sba_iommu_translate(const HPSBAIOMMUParams *params, uint64_t addr,
                            HPSBAIOMMUEntry *result)
{
    uint64_t page_mask;
    uint64_t translated_addr;
    uint64_t index;
    uint64_t pdir_ptr;
    uint64_t entry;

    if (!params || !result || !params->bypass || params->page_shift == 0 ||
        params->page_shift >= 64) {
        return false;
    }

    page_mask = (UINT64_C(1) << params->page_shift) - 1;
    addr &= ~page_mask;
    translated_addr = params->bypass(params->opaque, addr);

    if ((params->ibase & 1) == 0 ||
        (addr & params->imask) != (params->ibase & ~UINT64_C(1))) {
        *result = (HPSBAIOMMUEntry) {
            .iova = addr,
            .translated_addr = translated_addr,
            .addr_mask = page_mask,
        };
        return true;
    }

    if (!params->pdir_read) {
        return false;
    }

    index = (addr >> params->page_shift) & params->pdir_index_mask;

    /* Preserve the SBA's unsigned, wrapping PDIR pointer arithmetic. */
    pdir_ptr = params->pdir_base + index * sizeof(entry);
    if (!params->pdir_read(params->opaque, pdir_ptr, &entry) ||
        !(entry & HP_SBA_IOPDIR_VALID_BIT)) {
        return false;
    }

    entry &= ~HP_SBA_IOPDIR_VALID_BIT;
    entry >>= params->page_shift;
    entry <<= params->page_shift;

    *result = (HPSBAIOMMUEntry) {
        .iova = addr,
        .translated_addr = entry,
        .addr_mask = page_mask,
    };
    return true;
}

bool hp_sba_iommu_decode_pcom(uint64_t pcom, unsigned int page_shift,
                              HPSBAIOMMUPurge *purge)
{
    HPSBAIOMMUPurge decoded;
    unsigned int size_shift;

    if (!purge || page_shift == 0 || page_shift > 31) {
        return false;
    }

    size_shift = pcom & HP_ZX1_PCOM_SIZE_MASK;
    decoded.iova = pcom & ~HP_ZX1_PCOM_SIZE_MASK;
    if (size_shift < page_shift) {
        return false;
    }

    decoded.size = UINT64_C(1) << size_shift;
    if ((decoded.iova & (decoded.size - 1)) ||
        decoded.iova > UINT64_MAX - (decoded.size - 1)) {
        return false;
    }

    *purge = decoded;
    return true;
}

static bool hp_zx1_iotlb_entry_valid(const HPSBAIOMMUEntry *entry)
{
    uint64_t page_size;

    if (!entry || entry->addr_mask == 0 || entry->addr_mask == UINT64_MAX) {
        return false;
    }

    page_size = entry->addr_mask + 1;
    return is_power_of_2(page_size) && !(entry->iova & entry->addr_mask) &&
           !(entry->translated_addr & entry->addr_mask);
}

void hp_zx1_iotlb_clear(HPZX1IOTLB *iotlb)
{
    if (iotlb) {
        memset(iotlb, 0, sizeof(*iotlb));
    }
}

bool hp_zx1_iotlb_lookup(const HPZX1IOTLB *iotlb, uint64_t iova,
                         HPSBAIOMMUEntry *result)
{
    unsigned int i;

    if (!iotlb || !result) {
        return false;
    }

    for (i = 0; i < HP_ZX1_IOTLB_SLOT_COUNT; i++) {
        const HPZX1IOTLBSlot *slot = &iotlb->slots[i];

        if (slot->valid &&
            (iova & ~slot->entry.addr_mask) == slot->entry.iova) {
            *result = slot->entry;
            return true;
        }
    }

    return false;
}

bool hp_zx1_iotlb_store_slot(HPZX1IOTLB *iotlb, unsigned int slot,
                             const HPSBAIOMMUEntry *entry)
{
    if (!iotlb || slot >= HP_ZX1_IOTLB_SLOT_COUNT ||
        !hp_zx1_iotlb_entry_valid(entry)) {
        return false;
    }

    iotlb->slots[slot].entry = *entry;
    iotlb->slots[slot].valid = true;
    return true;
}

bool hp_zx1_iotlb_invalidate(HPZX1IOTLB *iotlb, const HPSBAIOMMUPurge *purge)
{
    uint64_t purge_end;
    unsigned int i;

    if (!iotlb || !purge || !purge->size || !is_power_of_2(purge->size) ||
        (purge->iova & (purge->size - 1)) ||
        purge->iova > UINT64_MAX - (purge->size - 1)) {
        return false;
    }

    purge_end = purge->iova + purge->size - 1;
    for (i = 0; i < HP_ZX1_IOTLB_SLOT_COUNT; i++) {
        HPZX1IOTLBSlot *slot = &iotlb->slots[i];
        uint64_t entry_end;

        if (!slot->valid) {
            continue;
        }

        entry_end = slot->entry.iova + slot->entry.addr_mask;
        if (slot->entry.iova <= purge_end && purge->iova <= entry_end) {
            slot->valid = false;
        }
    }

    return true;
}

bool hp_zx1_iommu_decode_tcnfg(uint64_t tcnfg, unsigned int *page_shift)
{
    unsigned int shift;

    if (!page_shift) {
        return false;
    }

    switch (tcnfg) {
    case 0:
        shift = 12;
        break;
    case 1:
        shift = 13;
        break;
    case 2:
        shift = 14;
        break;
    case 3:
        shift = 16;
        break;
    default:
        return false;
    }

    *page_shift = shift;
    return true;
}

bool hp_zx1_iommu_decode_window(uint64_t ibase, uint64_t imask,
                                unsigned int page_shift,
                                HPZX1IOMMUWindow *window)
{
    HPZX1IOMMUWindow decoded;
    uint64_t page_size;
    uint64_t page_count;

    if (!window || page_shift == 0 || page_shift >= 64) {
        return false;
    }

    decoded.enabled = ibase & 1;
    decoded.base = ibase & ~UINT64_C(1);
    decoded.mask = imask | HP_ZX1_IMASK_DAC;
    decoded.aperture_size = ~decoded.mask + 1;
    decoded.page_shift = page_shift;

    page_size = UINT64_C(1) << page_shift;
    if (decoded.aperture_size < page_size ||
        !is_power_of_2(decoded.aperture_size) ||
        (decoded.base & (decoded.aperture_size - 1)) ||
        decoded.base > UINT64_MAX - decoded.aperture_size) {
        return false;
    }

    page_count = decoded.aperture_size >> page_shift;
    decoded.pdir_index_mask = page_count - 1;
    *window = decoded;
    return true;
}

bool hp_zx1_iommu_iova_is_bypass(const HPZX1IOMMUWindow *window, uint64_t iova,
                                 bool dvi)
{
    return !window || !window->enabled || dvi ||
           (iova & window->mask) != window->base;
}

bool hp_zx1_iommu_pdir_index(const HPZX1IOMMUWindow *window, uint64_t iova,
                             bool dvi, uint64_t *index)
{
    if (!index || hp_zx1_iommu_iova_is_bypass(window, iova, dvi) ||
        window->page_shift == 0 || window->page_shift >= 64) {
        return false;
    }

    *index =
        ((iova - window->base) >> window->page_shift) & window->pdir_index_mask;
    return true;
}

bool hp_zx1_iommu_decode_pcom(const HPZX1IOMMUWindow *window, uint64_t pcom,
                              HPZX1IOMMUPurge *purge)
{
    return window && hp_sba_iommu_decode_pcom(pcom, window->page_shift, purge);
}
