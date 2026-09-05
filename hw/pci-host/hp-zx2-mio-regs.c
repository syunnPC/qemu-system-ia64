/*
 * HP zx2 MIOC-specific register layer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-mio-regs.h"
#include "hw/pci-host/hp-zx2-mio-regs.h"

#define HP_ZX2_MIO_CAPABILITIES_VALUE UINT64_C(0x0000003208040001)

static bool hp_zx2_mio_access(uint64_t offset, unsigned int size,
                              uint64_t *base, uint64_t *mask,
                              uint64_t *data, uint64_t value)
{
    unsigned int lane = offset & 7;

    if ((size != 1 && size != 2 && size != 4 && size != 8) ||
        (offset & (size - 1)) || lane + size > 8) {
        return false;
    }
    *base = offset & ~UINT64_C(7);
    *mask = size == 8 ? UINT64_MAX :
            ((UINT64_C(1) << (size * 8)) - 1) << (lane * 8);
    *data = (value << (lane * 8)) & *mask;
    return true;
}

void hp_zx2_mio_regs_reset(HPZX2MIORegs *regs)
{
    unsigned int group;

    if (!regs) {
        return;
    }
    memset(regs, 0, sizeof(*regs));
    for (group = 0; group < HP_ZX2_MIO_GROUP_COUNT; group++) {
        regs->group_ropes[group] =
            (UINT64_C(0x0303) << (group * 2)) & HP_ZX2_MIO_ROPE_MASK;
        regs->group_control[group] = HP_ZX2_MIO_GROUP_ENABLE |
            ((uint64_t)group << HP_ZX2_MIO_GROUP_CONTEXT_SHIFT);
    }
}

static bool hp_zx2_mio_group_register(uint64_t base, uint64_t first,
                                      unsigned int *group)
{
    if (base < first || base >= first + HP_ZX2_MIO_GROUP_COUNT * 8) {
        return false;
    }
    *group = (base - first) / 8;
    return true;
}

bool hp_zx2_mio_regs_read(const HPZX2MIORegs *regs, uint64_t offset,
                          unsigned int size, uint64_t *value)
{
    uint64_t base;
    uint64_t mask;
    uint64_t data;
    uint64_t reg;
    unsigned int group;

    if (!regs || !value ||
        !hp_zx2_mio_access(offset, size, &base, &mask, &data, 0)) {
        return false;
    }
    if (base == HP_ZX2_MIO_CAPABILITIES) {
        reg = HP_ZX2_MIO_CAPABILITIES_VALUE;
    } else if (hp_zx2_mio_group_register(
                   base, HP_ZX2_MIO_GROUP_ROPES(0), &group)) {
        reg = regs->group_ropes[group];
    } else if (hp_zx2_mio_group_register(
                   base, HP_ZX2_MIO_GROUP_CONTROL(0), &group)) {
        reg = regs->group_control[group];
    } else {
        switch (base) {
        case HP_ZX2_MIO_IOMMU_SELECT:
            reg = regs->iommu_select;
            break;
        case HP_ZX2_MIO_ERROR_INTERRUPT:
            reg = regs->error_interrupt;
            break;
        case HP_ZX2_MIO_ERROR_STATUS:
            reg = regs->error_status;
            break;
        case HP_ZX2_MIO_ERROR_ADDRESS:
            reg = regs->error_address;
            break;
        case HP_ZX2_MIO_ERROR_INFORMATION:
            reg = regs->error_information;
            break;
        default:
            return false;
        }
    }
    *value = (reg & mask) >> ((offset & 7) * 8);
    return true;
}

bool hp_zx2_mio_regs_write(HPZX2MIORegs *regs, uint64_t offset,
                           unsigned int size, uint64_t value)
{
    uint64_t base;
    uint64_t mask;
    uint64_t data;
    uint64_t writable;
    uint64_t *reg;
    unsigned int group;

    if (!regs ||
        !hp_zx2_mio_access(offset, size, &base, &mask, &data, value)) {
        return false;
    }
    if (base == HP_ZX2_MIO_CAPABILITIES ||
        base == HP_ZX2_MIO_ERROR_ADDRESS ||
        base == HP_ZX2_MIO_ERROR_INFORMATION) {
        return true;
    }
    if (hp_zx2_mio_group_register(base, HP_ZX2_MIO_GROUP_ROPES(0),
                                  &group)) {
        reg = &regs->group_ropes[group];
        writable = HP_ZX2_MIO_ROPE_MASK;
    } else if (hp_zx2_mio_group_register(
                   base, HP_ZX2_MIO_GROUP_CONTROL(0), &group)) {
        reg = &regs->group_control[group];
        writable = HP_ZX2_MIO_GROUP_ENABLE |
                   HP_ZX2_MIO_GROUP_CONTEXT_MASK;
    } else if (base == HP_ZX2_MIO_IOMMU_SELECT) {
        reg = &regs->iommu_select;
        writable = HP_ZX2_MIO_IOMMU_SELECT_MASK;
    } else if (base == HP_ZX2_MIO_ERROR_INTERRUPT) {
        reg = &regs->error_interrupt;
        writable = HP_ZX2_MIO_ERROR_INTERRUPT_ENABLE |
                   HP_ZX2_MIO_ERROR_INTERRUPT_VECTOR |
                   HP_ZX2_MIO_ERROR_INTERRUPT_ID |
                   HP_ZX2_MIO_ERROR_INTERRUPT_EID;
    } else if (base == HP_ZX2_MIO_ERROR_STATUS) {
        regs->error_status &= ~(data & mask &
            HP_ZX1_MIO_ERROR_STATUS_W1C);
        if (!(regs->error_status & HP_ZX1_MIO_ERROR_VALID)) {
            regs->error_status = 0;
            regs->error_address = 0;
            regs->error_information = 0;
        }
        return true;
    } else {
        return false;
    }
    *reg = (*reg & ~(mask & writable)) | (data & mask & writable);
    return true;
}

bool hp_zx2_mio_regs_group_for_ropes(const HPZX2MIORegs *regs,
                                     uint16_t ropes, unsigned int *group,
                                     unsigned int *context)
{
    unsigned int index;

    if (!regs || !ropes || !group || !context) {
        return false;
    }
    for (index = 0; index < HP_ZX2_MIO_GROUP_COUNT; index++) {
        uint16_t map = regs->group_ropes[index];

        if ((regs->group_control[index] & HP_ZX2_MIO_GROUP_ENABLE) &&
            (ropes & map) == ropes) {
            *group = index;
            *context = (regs->group_control[index] &
                        HP_ZX2_MIO_GROUP_CONTEXT_MASK) >>
                       HP_ZX2_MIO_GROUP_CONTEXT_SHIFT;
            return true;
        }
    }
    return false;
}

void hp_zx2_mio_regs_report_fault(HPZX2MIORegs *regs, uint64_t status,
                                  uint64_t address, uint64_t information)
{
    if (!regs || !(status & (HP_ZX1_MIO_ERROR_IOMMU |
                            HP_ZX1_MIO_ERROR_CSR_DECODE))) {
        return;
    }
    if (regs->error_status & HP_ZX1_MIO_ERROR_VALID) {
        regs->error_status |= status | HP_ZX1_MIO_ERROR_MULTIPLE;
    } else {
        regs->error_status = status | HP_ZX1_MIO_ERROR_VALID;
        regs->error_address = address;
        regs->error_information = information;
    }
}

bool hp_zx2_mio_regs_state_valid(const HPZX2MIORegs *regs)
{
    uint16_t assigned = 0;
    unsigned int group;

    if (!regs || (regs->iommu_select & ~HP_ZX2_MIO_IOMMU_SELECT_MASK) ||
        (regs->error_interrupt &
         ~(HP_ZX2_MIO_ERROR_INTERRUPT_ENABLE |
           HP_ZX2_MIO_ERROR_INTERRUPT_VECTOR |
           HP_ZX2_MIO_ERROR_INTERRUPT_ID |
           HP_ZX2_MIO_ERROR_INTERRUPT_EID)) ||
        (regs->error_status & ~HP_ZX1_MIO_ERROR_STATUS_W1C)) {
        return false;
    }
    for (group = 0; group < HP_ZX2_MIO_GROUP_COUNT; group++) {
        uint64_t control = regs->group_control[group];
        uint64_t ropes = regs->group_ropes[group];

        if (!ropes || (ropes & ~(uint64_t)HP_ZX2_MIO_ROPE_MASK) ||
            (assigned & ropes) ||
            (control & ~(HP_ZX2_MIO_GROUP_ENABLE |
                         HP_ZX2_MIO_GROUP_CONTEXT_MASK))) {
            return false;
        }
        assigned |= ropes;
    }
    return assigned == HP_ZX2_MIO_ROPE_MASK;
}
