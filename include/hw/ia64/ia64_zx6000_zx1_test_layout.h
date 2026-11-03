/*
 * IA-64 zx6000 ZX1 integration test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_ZX6000_ZX1_TEST_LAYOUT_H
#define HW_IA64_ZX6000_ZX1_TEST_LAYOUT_H

#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "qemu/typedefs.h"

/* Address map and topology used by integration tests. */
#define IA64_ZX6000_ZX1_TEST_ROOT_COUNT          2U
#define IA64_ZX6000_ZX1_TEST_SLOT_COUNT          32U
#define IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT      4U
#define IA64_ZX6000_ZX1_TEST_ROPE_COUNT          8U
#define IA64_ZX6000_ZX1_TEST_AGP_INPUT_COUNT     7U
#define IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT     10U

#define IA64_ZX6000_ZX1_TEST_RAM_BASE            UINT64_C(0x00000000)
#define IA64_ZX6000_ZX1_TEST_RAM_SIZE            UINT64_C(0x20000000)

#define IA64_ZX6000_ZX1_TEST_MIO_BASE            UINT64_C(0xfed00000)
#define IA64_ZX6000_ZX1_TEST_MIO_SIZE            UINT64_C(0x00010000)
#define IA64_ZX6000_ZX1_TEST_PIB_BASE            UINT64_C(0xfee00000)
#define IA64_ZX6000_ZX1_TEST_PIB_SIZE            UINT64_C(0x00100000)

#define IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE        UINT64_C(0x00002000)
#define IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE       UINT64_C(0xfed20000)
#define IA64_ZX6000_ZX1_TEST_IOA1_CSR_BASE       UINT64_C(0xfed22000)

#define IA64_ZX6000_ZX1_TEST_ROOT0_FIRST_BUS     UINT8_C(0x20)
#define IA64_ZX6000_ZX1_TEST_ROOT0_LAST_BUS      UINT8_C(0x2f)
#define IA64_ZX6000_ZX1_TEST_ROOT1_FIRST_BUS     UINT8_C(0x40)
#define IA64_ZX6000_ZX1_TEST_ROOT1_LAST_BUS      UINT8_C(0x4f)
#define IA64_ZX6000_ZX1_TEST_ROOT0_ROPE_MASK     UINT32_C(0x01)
#define IA64_ZX6000_ZX1_TEST_ROOT1_ROPE_MASK     UINT32_C(0x0c)
#define IA64_ZX6000_ZX1_TEST_ROOT0_BUS_MODE_RESET \
    UINT64_C(0x20)
#define IA64_ZX6000_ZX1_TEST_ROOT1_BUS_MODE_RESET \
    UINT64_C(0x2000)

/* Each PCI range starts at zero in its root's independent PCI address space. */
#define IA64_ZX6000_ZX1_TEST_PCI_MMIO_BASE       UINT64_C(0x00000000)
#define IA64_ZX6000_ZX1_TEST_MMIO_SIZE           UINT64_C(0x01000000)
#define IA64_ZX6000_ZX1_TEST_ROOT0_CPU_MMIO_BASE UINT64_C(0x90000000)
#define IA64_ZX6000_ZX1_TEST_ROOT1_CPU_MMIO_BASE UINT64_C(0xa0000000)

#define IA64_ZX6000_ZX1_TEST_IOMMU_BASE          UINT64_C(0x40000000)
#define IA64_ZX6000_ZX1_TEST_IOMMU_SIZE          UINT64_C(0x10000000)
#define IA64_ZX6000_ZX1_TEST_IOMMU_IBASE_RESET   UINT64_C(0x40000000)
#define IA64_ZX6000_ZX1_TEST_IOMMU_IMASK_RESET   UINT64_C(0xf0000000)
#define IA64_ZX6000_ZX1_TEST_IOMMU_PCOM_RESET    UINT64_C(0)
#define IA64_ZX6000_ZX1_TEST_IOMMU_TCNFG_RESET   UINT64_C(0)

#define IA64_ZX6000_ZX1_TEST_PAGE_SIZE           UINT64_C(0x1000)
#define IA64_ZX6000_ZX1_TEST_PDIR_BASE           UINT64_C(0x01000000)
#define IA64_ZX6000_ZX1_TEST_PDIR_SIZE           UINT64_C(0x00080000)
#define IA64_ZX6000_ZX1_TEST_TARGET_BASE    UINT64_C(0x02000000)
#define IA64_ZX6000_ZX1_TEST_TARGET_PAGES   18U
#define IA64_ZX6000_ZX1_TEST_TARGET_SIZE    \
    (IA64_ZX6000_ZX1_TEST_TARGET_PAGES *    \
     IA64_ZX6000_ZX1_TEST_PAGE_SIZE)

typedef struct IA64ZX6000ZX1TestRange {
    uint64_t base;
    uint64_t size;
} IA64ZX6000ZX1TestRange;

typedef struct IA64ZX6000ZX1TestRoot {
    IA64ZX6000ZX1TestRange ioa_csr;
    IA64ZX6000ZX1TestRange pci_mmio;
    IA64ZX6000ZX1TestRange cpu_mmio;
    uint32_t rope_mask;
    HPZX1IOAMode mode;
    uint64_t bus_mode_reset;
    uint8_t first_bus;
    uint8_t last_bus;
    /* Every slot's INTA..INTD aggregates to input 0..3. */
    uint8_t intx_route[IA64_ZX6000_ZX1_TEST_SLOT_COUNT]
                      [IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT];
} IA64ZX6000ZX1TestRoot;

typedef struct IA64ZX6000ZX1TestIOMMU {
    IA64ZX6000ZX1TestRange aperture;
    IA64ZX6000ZX1TestRange pdir;
    IA64ZX6000ZX1TestRange test_target;
    uint64_t ibase_reset;
    uint64_t imask_reset;
    uint64_t pcom_reset;
    uint64_t tcnfg_reset;
    uint64_t pdir_base_reset;
} IA64ZX6000ZX1TestIOMMU;

typedef struct IA64ZX6000ZX1TestLayout {
    IA64ZX6000ZX1TestRange ram;
    IA64ZX6000ZX1TestRange mio;
    IA64ZX6000ZX1TestRange pib;
    IA64ZX6000ZX1TestIOMMU iommu;
    IA64ZX6000ZX1TestRoot roots[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
} IA64ZX6000ZX1TestLayout;

/* Populate the fixed ZX1 test layout. */
void ia64_zx6000_zx1_test_layout_init(IA64ZX6000ZX1TestLayout *layout);

/* Validate without mutation; failure leaves the layout unchanged. */
bool ia64_zx6000_zx1_test_layout_validate(const IA64ZX6000ZX1TestLayout *layout,
                                          Error **errp);

#endif
