/*
 * HP System Bus Adapter IOMMU helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_SBA_IOMMU_H
#define HW_PCI_HOST_HP_SBA_IOMMU_H

#define HP_SBA_IOPDIR_VALID_BIT UINT64_C(0x8000000000000000)
#define HP_ZX1_IOTLB_SLOT_COUNT 16

/*
 * This is a page-walk primitive.  The caller owns aperture/DVI checks,
 * register validation, fault classification, and any translation cache.
 * PDIR address arithmetic wraps modulo 2^64.
 */

typedef uint64_t (*HPSBAIOMMUBypassFn)(void *opaque, uint64_t addr);
typedef bool (*HPSBAIOMMUPdirReadFn)(void *opaque, uint64_t addr,
                                     uint64_t *entry);

typedef struct HPSBAIOMMUParams {
    uint64_t ibase;
    uint64_t imask;
    uint64_t pdir_base;
    /* Applied after shifting the absolute IOVA by page_shift. */
    uint64_t pdir_index_mask;
    unsigned int page_shift; /* Must be in the range 1..63. */
    HPSBAIOMMUBypassFn bypass;
    HPSBAIOMMUPdirReadFn pdir_read;
    void *opaque;
} HPSBAIOMMUParams;

typedef struct HPSBAIOMMUEntry {
    uint64_t iova;
    uint64_t translated_addr;
    uint64_t addr_mask;
} HPSBAIOMMUEntry;

typedef struct HPZX1IOMMUPurge {
    uint64_t iova;
    uint64_t size;
} HPZX1IOMMUPurge;

typedef HPZX1IOMMUPurge HPSBAIOMMUPurge;

typedef struct HPZX1IOMMUWindow {
    uint64_t base;
    uint64_t mask;
    uint64_t aperture_size;
    uint64_t pdir_index_mask;
    unsigned int page_shift;
    bool enabled;
} HPZX1IOMMUWindow;

typedef struct HPZX1IOTLBSlot {
    HPSBAIOMMUEntry entry;
    bool valid;
} HPZX1IOTLBSlot;

/*
 * 16-entry fully-associative I/O TLB.  The caller chooses the fill slot.
 * Faults and bypass mappings are not stored.  The frontend owns reset and
 * migration of this state.
 */
typedef struct HPZX1IOTLB {
    HPZX1IOTLBSlot slots[HP_ZX1_IOTLB_SLOT_COUNT];
} HPZX1IOTLB;

/* result is initialized only when the translation succeeds. */
bool hp_sba_iommu_translate(const HPSBAIOMMUParams *params, uint64_t addr,
                            HPSBAIOMMUEntry *result);

/*
 * Decode the common SBA PCOM address/size encoding.  The range need not be
 * contained by an enabled translation aperture: PCOM is valid before IBASE
 * is enabled, and an invalidation may harmlessly cover unmapped addresses.
 * Output is initialized only on success.
 */
bool hp_sba_iommu_decode_pcom(uint64_t pcom, unsigned int page_shift,
                              HPSBAIOMMUPurge *purge);

void hp_zx1_iotlb_clear(HPZX1IOTLB *iotlb);
bool hp_zx1_iotlb_lookup(const HPZX1IOTLB *iotlb, uint64_t iova,
                         HPSBAIOMMUEntry *result);
bool hp_zx1_iotlb_store_slot(HPZX1IOTLB *iotlb, unsigned int slot,
                             const HPSBAIOMMUEntry *entry);
bool hp_zx1_iotlb_invalidate(HPZX1IOTLB *iotlb, const HPSBAIOMMUPurge *purge);

/*
 * zx1 register decoders.  Outputs are initialized only on success.
 * decode_window canonicalizes the IMASK DAC bits.  The frontend enforces the
 * IBASE write mask and a nonwrapping, physically addressable PDIR range, and
 * owns register effects, cache invalidation, and IOMMU notifier delivery.
 */
bool hp_zx1_iommu_decode_tcnfg(uint64_t tcnfg, unsigned int *page_shift);
bool hp_zx1_iommu_decode_window(uint64_t ibase, uint64_t imask,
                                unsigned int page_shift,
                                HPZX1IOMMUWindow *window);
bool hp_zx1_iommu_iova_is_bypass(const HPZX1IOMMUWindow *window, uint64_t iova,
                                 bool dvi);
bool hp_zx1_iommu_pdir_index(const HPZX1IOMMUWindow *window, uint64_t iova,
                             bool dvi, uint64_t *index);
bool hp_zx1_iommu_decode_pcom(const HPZX1IOMMUWindow *window, uint64_t pcom,
                              HPZX1IOMMUPurge *purge);

#endif
