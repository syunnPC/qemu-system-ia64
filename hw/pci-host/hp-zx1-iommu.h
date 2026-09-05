/*
 * HP zx1 IOC IOMMU frontend helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX1_IOMMU_H
#define HW_PCI_HOST_HP_ZX1_IOMMU_H

#include "hw/pci-host/hp-sba-iommu.h"

#define HP_ZX1_IOC_IOMMU_IBASE       0x300
#define HP_ZX1_IOC_IOMMU_IMASK       0x308
#define HP_ZX1_IOC_IOMMU_PCOM        0x310
#define HP_ZX1_IOC_IOMMU_TCNFG       0x318
#define HP_ZX1_IOC_IOMMU_PDIR_BASE   0x320

#define HP_ZX1_IOMMU_PHYS_BITS       50
#define HP_ZX1_IOMMU_PHYS_MASK       UINT64_C(0x0003ffffffffffff)

typedef struct HPZX1IOMMUResetConfig {
    uint64_t ibase;
    uint64_t imask;
    uint64_t pcom;
    uint64_t tcnfg;
    uint64_t pdir_base;
} HPZX1IOMMUResetConfig;

/* Raw frontend latches preserve written bits; the wrapper controls readback. */
typedef struct HPZX1IOMMUFrontend {
    uint64_t ibase;
    uint64_t imask;
    uint64_t pcom;
    uint64_t tcnfg;
    uint64_t pdir_base;

    HPZX1IOTLB iotlb;

    /* First invalid slot, then round-robin replacement of valid slots. */
    uint8_t rr_next;
} HPZX1IOMMUFrontend;

typedef struct HPZX1IOMMUWriteResult {
    bool purged;
    HPSBAIOMMUPurge purge;
} HPZX1IOMMUWriteResult;

typedef enum HPZX1IOMMUTranslateResult {
    HP_ZX1_IOMMU_TRANSLATE_BLOCKED,
    HP_ZX1_IOMMU_TRANSLATE_IDENTITY,
    HP_ZX1_IOMMU_TRANSLATE_TRANSLATED,
} HPZX1IOMMUTranslateResult;

typedef struct HPZX1IOMMUEvictionResult {
    bool evicted;
    HPSBAIOMMUPurge range;
} HPZX1IOMMUEvictionResult;

typedef enum HPZX1IOMMUFaultReason {
    HP_ZX1_IOMMU_FAULT_NONE,
    HP_ZX1_IOMMU_FAULT_ADDRESS_WIDTH,
    HP_ZX1_IOMMU_FAULT_PAGE_SIZE,
    HP_ZX1_IOMMU_FAULT_WINDOW,
    HP_ZX1_IOMMU_FAULT_PDIR_RANGE,
    HP_ZX1_IOMMU_FAULT_PDIR_READ,
    HP_ZX1_IOMMU_FAULT_INVALID_PTE,
    HP_ZX1_IOMMU_FAULT_CACHE,
} HPZX1IOMMUFaultReason;

typedef struct HPZX1IOMMUFault {
    HPZX1IOMMUFaultReason reason;
    uint64_t iova;
    uint64_t pdir_address;
    uint64_t pte;
} HPZX1IOMMUFault;

/* Invalid input returns false without changing an existing frontend. */
bool hp_zx1_iommu_frontend_reset(HPZX1IOMMUFrontend *iommu,
                                  const HPZX1IOMMUResetConfig *config);

/*
 * Register offsets name the complete 64-bit IOC register.  byte_enable bit N
 * selects logical register bits (N * 8 + 7):(N * 8).  Raw latch bits preserve
 * every enabled byte.
 *
 * A decodable PCOM write covering any byte in the logical low dword
 * invalidates matching cached entries and returns the exact purge range.
 * High-dword-only writes update the raw latch without issuing a command.
 * Other writes and undecodable PCOM writes return purged == false.  Only PCOM
 * and reset invalidate the cache.  A false return leaves both the frontend
 * and result unchanged.
 */
bool hp_zx1_iommu_frontend_reg_write(HPZX1IOMMUFrontend *iommu,
                                     uint64_t reg_offset, uint64_t value,
                                     uint8_t byte_enable,
                                     HPZX1IOMMUWriteResult *result);

/*
 * This exposes the raw latch to a device wrapper; it does not prescribe the
 * zx1 guest-visible read value of command registers such as PCOM.  A false
 * return leaves value unchanged.
 */
bool hp_zx1_iommu_frontend_reg_latch(const HPZX1IOMMUFrontend *iommu,
                                     uint64_t reg_offset, uint64_t *value);

/*
 * pdir_read returns a host-order IOPDIR entry.  BLOCKED leaves outputs and
 * cache state unchanged; IDENTITY and TRANSLATED return a page-aligned entry.
 * A non-NULL eviction reports round-robin replacement; NULL omits it.
 * DVI, disabled translation, and out-of-aperture addresses bypass the cache.
 * Addresses beyond 50 bits are blocked, and enabled translation requires a
 * valid TCNFG.
 */
HPZX1IOMMUTranslateResult hp_zx1_iommu_frontend_translate(
    HPZX1IOMMUFrontend *iommu, uint64_t iova, bool dvi,
    HPSBAIOMMUPdirReadFn pdir_read, void *opaque,
    HPSBAIOMMUEntry *entry, HPZX1IOMMUEvictionResult *eviction);

HPZX1IOMMUTranslateResult hp_zx1_iommu_frontend_translate_ex(
    HPZX1IOMMUFrontend *iommu, uint64_t iova, bool dvi,
    HPSBAIOMMUPdirReadFn pdir_read, void *opaque,
    HPSBAIOMMUEntry *entry, HPZX1IOMMUEvictionResult *eviction,
    HPZX1IOMMUFault *fault);

#endif
