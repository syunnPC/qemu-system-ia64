/*
 * HP zx1 IOC IOMMU frontend helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-iommu.h"

static uint64_t hp_zx1_iommu_merge_bytes(uint64_t old_value,
                                         uint64_t value,
                                         uint8_t byte_enable)
{
    unsigned int lane;

    for (lane = 0; lane < 8; lane++) {
        uint64_t mask;

        if (!(byte_enable & (1U << lane))) {
            continue;
        }
        mask = UINT64_C(0xff) << (lane * 8);
        old_value = (old_value & ~mask) | (value & mask);
    }
    return old_value;
}

bool hp_zx1_iommu_frontend_reset(HPZX1IOMMUFrontend *iommu,
                                  const HPZX1IOMMUResetConfig *config)
{
    if (!iommu || !config) {
        return false;
    }

    iommu->ibase = config->ibase;
    iommu->imask = config->imask;
    iommu->pcom = config->pcom;
    iommu->tcnfg = config->tcnfg;
    iommu->pdir_base = config->pdir_base;
    hp_zx1_iotlb_clear(&iommu->iotlb);
    iommu->rr_next = 0;
    return true;
}

static uint64_t *hp_zx1_iommu_reg_ptr(HPZX1IOMMUFrontend *iommu,
                                      uint64_t reg_offset)
{
    switch (reg_offset) {
    case HP_ZX1_IOC_IOMMU_IBASE:
        return &iommu->ibase;
    case HP_ZX1_IOC_IOMMU_IMASK:
        return &iommu->imask;
    case HP_ZX1_IOC_IOMMU_PCOM:
        return &iommu->pcom;
    case HP_ZX1_IOC_IOMMU_TCNFG:
        return &iommu->tcnfg;
    case HP_ZX1_IOC_IOMMU_PDIR_BASE:
        return &iommu->pdir_base;
    default:
        return NULL;
    }
}

static const uint64_t *hp_zx1_iommu_const_reg_ptr(
    const HPZX1IOMMUFrontend *iommu, uint64_t reg_offset)
{
    switch (reg_offset) {
    case HP_ZX1_IOC_IOMMU_IBASE:
        return &iommu->ibase;
    case HP_ZX1_IOC_IOMMU_IMASK:
        return &iommu->imask;
    case HP_ZX1_IOC_IOMMU_PCOM:
        return &iommu->pcom;
    case HP_ZX1_IOC_IOMMU_TCNFG:
        return &iommu->tcnfg;
    case HP_ZX1_IOC_IOMMU_PDIR_BASE:
        return &iommu->pdir_base;
    default:
        return NULL;
    }
}

bool hp_zx1_iommu_frontend_reg_latch(const HPZX1IOMMUFrontend *iommu,
                                     uint64_t reg_offset, uint64_t *value)
{
    const uint64_t *reg;

    if (!iommu || !value) {
        return false;
    }

    reg = hp_zx1_iommu_const_reg_ptr(iommu, reg_offset);
    if (!reg) {
        return false;
    }

    *value = *reg;
    return true;
}

bool hp_zx1_iommu_frontend_reg_write(HPZX1IOMMUFrontend *iommu,
                                     uint64_t reg_offset, uint64_t value,
                                     uint8_t byte_enable,
                                     HPZX1IOMMUWriteResult *result)
{
    HPZX1IOMMUWriteResult write_result = { 0 };
    HPSBAIOMMUPurge purge;
    unsigned int page_shift;
    uint64_t *reg;

    if (!iommu || !result) {
        return false;
    }

    reg = hp_zx1_iommu_reg_ptr(iommu, reg_offset);
    if (!reg) {
        return false;
    }

    *reg = hp_zx1_iommu_merge_bytes(*reg, value, byte_enable);

    /*
     * The logical low dword is the command lane.  A high-dword-only update
     * does not reissue a command retained in the latch.
     */
    if (reg_offset == HP_ZX1_IOC_IOMMU_PCOM && (byte_enable & 0x0f) &&
        hp_zx1_iommu_decode_tcnfg(iommu->tcnfg, &page_shift) &&
        hp_sba_iommu_decode_pcom(iommu->pcom, page_shift, &purge)) {
        bool invalidated = hp_zx1_iotlb_invalidate(&iommu->iotlb, &purge);

        assert(invalidated);
        write_result.purged = true;
        write_result.purge = purge;
    }

    *result = write_result;
    return true;
}

static void hp_zx1_iommu_identity(uint64_t iova, unsigned int page_shift,
                                  HPSBAIOMMUEntry *entry)
{
    uint64_t page_mask = (UINT64_C(1) << page_shift) - 1;
    uint64_t page_base = iova & ~page_mask;

    *entry = (HPSBAIOMMUEntry) {
        .iova = page_base,
        .translated_addr = page_base,
        .addr_mask = page_mask,
    };
}

static bool hp_zx1_iommu_pdir_address(const HPZX1IOMMUFrontend *iommu,
                                      const HPZX1IOMMUWindow *window,
                                      uint64_t index, uint64_t *address)
{
    uint64_t max_pdir_index;

    if (!address || (iommu->pdir_base & 7) ||
        iommu->pdir_base > HP_ZX1_IOMMU_PHYS_MASK - 7) {
        return false;
    }

    max_pdir_index = (HP_ZX1_IOMMU_PHYS_MASK - 7 -
                      iommu->pdir_base) / sizeof(uint64_t);

    /* Validate the complete aperture's PDIR, not only this selected entry. */
    if (window->pdir_index_mask > max_pdir_index ||
        index > window->pdir_index_mask) {
        return false;
    }

    *address = iommu->pdir_base + index * sizeof(uint64_t);
    return true;
}

static bool hp_zx1_iommu_select_fill_slot(
    const HPZX1IOMMUFrontend *iommu, unsigned int *selected,
    HPZX1IOMMUEvictionResult *eviction)
{
    HPZX1IOMMUEvictionResult result = { 0 };
    unsigned int slot;

    for (slot = 0; slot < HP_ZX1_IOTLB_SLOT_COUNT; slot++) {
        if (!iommu->iotlb.slots[slot].valid) {
            *selected = slot;
            *eviction = result;
            return true;
        }
    }

    if (iommu->rr_next >= HP_ZX1_IOTLB_SLOT_COUNT) {
        return false;
    }

    slot = iommu->rr_next;
    result.evicted = true;
    result.range.iova = iommu->iotlb.slots[slot].entry.iova;
    result.range.size = iommu->iotlb.slots[slot].entry.addr_mask + 1;
    *selected = slot;
    *eviction = result;
    return true;
}

HPZX1IOMMUTranslateResult hp_zx1_iommu_frontend_translate_ex(
    HPZX1IOMMUFrontend *iommu, uint64_t iova, bool dvi,
    HPSBAIOMMUPdirReadFn pdir_read, void *opaque,
    HPSBAIOMMUEntry *entry, HPZX1IOMMUEvictionResult *eviction,
    HPZX1IOMMUFault *fault)
{
    HPZX1IOMMUEvictionResult eviction_result = { 0 };
    HPSBAIOMMUEntry translated;
    HPZX1IOMMUWindow window;
    unsigned int page_shift;
    unsigned int slot;
    uint64_t page_mask;
    uint64_t pdir_address;
    uint64_t pdir_index;
    uint64_t pte;

    if (fault) {
        *fault = (HPZX1IOMMUFault) {
            .reason = HP_ZX1_IOMMU_FAULT_NONE,
            .iova = iova,
        };
    }

    if (!iommu || !entry) {
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }
    if (iova > HP_ZX1_IOMMU_PHYS_MASK) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_ADDRESS_WIDTH;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }

    if (dvi || !(iommu->ibase & 1)) {
        /* Invalid TCNFG during bypass uses 4 KiB identity granularity. */
        if (!hp_zx1_iommu_decode_tcnfg(iommu->tcnfg, &page_shift)) {
            page_shift = 12;
        }
        hp_zx1_iommu_identity(iova, page_shift, &translated);
        *entry = translated;
        if (eviction) {
            *eviction = eviction_result;
        }
        return HP_ZX1_IOMMU_TRANSLATE_IDENTITY;
    }

    if (!hp_zx1_iommu_decode_tcnfg(iommu->tcnfg, &page_shift)) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_PAGE_SIZE;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }

    if (!hp_zx1_iommu_decode_window(iommu->ibase, iommu->imask,
                                     page_shift, &window)) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_WINDOW;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }

    if (hp_zx1_iommu_iova_is_bypass(&window, iova, false)) {
        hp_zx1_iommu_identity(iova, page_shift, &translated);
        *entry = translated;
        if (eviction) {
            *eviction = eviction_result;
        }
        return HP_ZX1_IOMMU_TRANSLATE_IDENTITY;
    }

    if (hp_zx1_iotlb_lookup(&iommu->iotlb, iova, &translated)) {
        *entry = translated;
        if (eviction) {
            *eviction = eviction_result;
        }
        return HP_ZX1_IOMMU_TRANSLATE_TRANSLATED;
    }

    if (!hp_zx1_iommu_pdir_index(&window, iova, false, &pdir_index) ||
        !hp_zx1_iommu_pdir_address(iommu, &window, pdir_index,
                                    &pdir_address)) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_PDIR_RANGE;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }
    if (!pdir_read || !pdir_read(opaque, pdir_address, &pte)) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_PDIR_READ;
            fault->pdir_address = pdir_address;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }
    if (!(pte & HP_SBA_IOPDIR_VALID_BIT) ||
        (pte & ~(HP_SBA_IOPDIR_VALID_BIT | HP_ZX1_IOMMU_PHYS_MASK))) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_INVALID_PTE;
            fault->pdir_address = pdir_address;
            fault->pte = pte;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }

    page_mask = (UINT64_C(1) << page_shift) - 1;
    translated = (HPSBAIOMMUEntry) {
        .iova = iova & ~page_mask,
        .translated_addr = (pte & HP_ZX1_IOMMU_PHYS_MASK) & ~page_mask,
        .addr_mask = page_mask,
    };

    if (!hp_zx1_iommu_select_fill_slot(iommu, &slot, &eviction_result) ||
        !hp_zx1_iotlb_store_slot(&iommu->iotlb, slot, &translated)) {
        if (fault) {
            fault->reason = HP_ZX1_IOMMU_FAULT_CACHE;
            fault->pdir_address = pdir_address;
            fault->pte = pte;
        }
        return HP_ZX1_IOMMU_TRANSLATE_BLOCKED;
    }
    if (eviction_result.evicted) {
        iommu->rr_next = (iommu->rr_next + 1) %
                              HP_ZX1_IOTLB_SLOT_COUNT;
    }

    *entry = translated;
    if (eviction) {
        *eviction = eviction_result;
    }
    return HP_ZX1_IOMMU_TRANSLATE_TRANSLATED;
}

HPZX1IOMMUTranslateResult hp_zx1_iommu_frontend_translate(
    HPZX1IOMMUFrontend *iommu, uint64_t iova, bool dvi,
    HPSBAIOMMUPdirReadFn pdir_read, void *opaque,
    HPSBAIOMMUEntry *entry, HPZX1IOMMUEvictionResult *eviction)
{
    return hp_zx1_iommu_frontend_translate_ex(
        iommu, iova, dvi, pdir_read, opaque, entry, eviction, NULL);
}
