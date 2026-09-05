/*
 * HP zx2 MIOC-specific register layer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX2_MIO_REGS_H
#define HW_PCI_HOST_HP_ZX2_MIO_REGS_H

#define HP_ZX2_MIO_GROUP_COUNT             4
#define HP_ZX2_MIO_ROPE_COUNT              16
#define HP_ZX2_MIO_ROPE_MASK               UINT16_MAX
#define HP_ZX2_MIO_CAPABILITIES             0x2000
#define HP_ZX2_MIO_GROUP_ROPES(n)          (0x2010 + (n) * 8)
#define HP_ZX2_MIO_GROUP_CONTROL(n)        (0x2030 + (n) * 8)
#define HP_ZX2_MIO_IOMMU_SELECT             0x2050
#define HP_ZX2_MIO_ERROR_INTERRUPT          0x2058
#define HP_ZX2_MIO_ERROR_STATUS             0x2060
#define HP_ZX2_MIO_ERROR_ADDRESS            0x2068
#define HP_ZX2_MIO_ERROR_INFORMATION        0x2070

#define HP_ZX2_MIO_GROUP_ENABLE             (UINT64_C(1) << 0)
#define HP_ZX2_MIO_GROUP_CONTEXT_SHIFT      4
#define HP_ZX2_MIO_GROUP_CONTEXT_MASK       (UINT64_C(3) << 4)
#define HP_ZX2_MIO_IOMMU_SELECT_MASK        UINT64_C(3)
#define HP_ZX2_MIO_ERROR_INTERRUPT_ENABLE   (UINT64_C(1) << 0)
#define HP_ZX2_MIO_ERROR_INTERRUPT_VECTOR   (UINT64_C(0xff) << 8)
#define HP_ZX2_MIO_ERROR_INTERRUPT_ID       (UINT64_C(0xff) << 16)
#define HP_ZX2_MIO_ERROR_INTERRUPT_EID      (UINT64_C(0xff) << 24)

typedef struct HPZX2MIORegs {
    uint64_t group_ropes[HP_ZX2_MIO_GROUP_COUNT];
    uint64_t group_control[HP_ZX2_MIO_GROUP_COUNT];
    uint64_t iommu_select;
    uint64_t error_interrupt;
    uint64_t error_status;
    uint64_t error_address;
    uint64_t error_information;
} HPZX2MIORegs;

void hp_zx2_mio_regs_reset(HPZX2MIORegs *regs);
bool hp_zx2_mio_regs_read(const HPZX2MIORegs *regs, uint64_t offset,
                          unsigned int size, uint64_t *value);
bool hp_zx2_mio_regs_write(HPZX2MIORegs *regs, uint64_t offset,
                           unsigned int size, uint64_t value);
bool hp_zx2_mio_regs_group_for_ropes(const HPZX2MIORegs *regs,
                                     uint16_t ropes, unsigned int *group,
                                     unsigned int *context);
void hp_zx2_mio_regs_report_fault(HPZX2MIORegs *regs, uint64_t status,
                                  uint64_t address, uint64_t information);
bool hp_zx2_mio_regs_state_valid(const HPZX2MIORegs *regs);

#endif /* HW_PCI_HOST_HP_ZX2_MIO_REGS_H */
