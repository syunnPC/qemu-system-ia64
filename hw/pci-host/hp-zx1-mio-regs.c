/*
 * HP zx1 MIO register helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-mio-regs.h"

typedef struct HPZX1MIORegDesc {
    uint16_t offset;
    size_t state_offset;
    uint64_t reset;
    uint64_t writable_mask;
    bool function1;
} HPZX1MIORegDesc;

#define NO_STATE_OFFSET SIZE_MAX
#define REG_RO(_offset, _value, _f1) {                           \
    .offset = (_offset),                                        \
    .state_offset = NO_STATE_OFFSET,                            \
    .reset = (_value),                                          \
    .function1 = (_f1),                                        \
}
#define REG_RW(_offset, _field, _reset, _mask, _f1) {           \
    .offset = (_offset),                                        \
    .state_offset = offsetof(HPZX1MIORegs, _field),             \
    .reset = (_reset),                                          \
    .writable_mask = (_mask),                                  \
    .function1 = (_f1),                                        \
}

#define LMMIO_BASE_MASK (UINT64_C(0x7ff) << 20)
#define LMMIO_ROUTE_MASK (UINT64_C(0x3f) << 58)
#define GMMIO_BASE_MASK (UINT64_C(0xfff) << 32)
#define IOS_DIR_MASK (UINT64_C(0xff) << 8)
#define ZX1_CLASS_VALUE UINT64_C(0x0000002006800023)

static const HPZX1MIORegDesc hp_zx1_mio_reg_descs[] = {
    REG_RO(HP_ZX1_MIO_F0_ID, UINT64_C(0x1229103c), false),
    REG_RO(HP_ZX1_MIO_F0_CLASS, ZX1_CLASS_VALUE, false),
    REG_RO(HP_ZX1_MIO_MODULE_INFO, UINT64_C(0x0703000a), false),

    REG_RW(HP_ZX1_MIO_LMMIO_DIR_BASE(0), lmmio_dir_base[0],
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK | 1, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIR_MASK(0), lmmio_dir_mask[0],
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIR_ROUTE(0), lmmio_dir_route[0], 0,
           UINT64_C(0x7), false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIR_BASE(1), lmmio_dir_base[1],
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK | 1, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIR_MASK(1), lmmio_dir_mask[1],
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIR_ROUTE(1), lmmio_dir_route[1], 0,
           UINT64_C(0x7), false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIST_BASE, lmmio_dist_base,
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK | 1, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIST_MASK, lmmio_dist_mask,
           UINT64_C(0x80000000),
           LMMIO_BASE_MASK, false),
    REG_RW(HP_ZX1_MIO_LMMIO_DIST_ROUTE, lmmio_dist_route, 0,
           LMMIO_ROUTE_MASK, false),
    REG_RW(HP_ZX1_MIO_GMMIO_DIST_BASE, gmmio_dist_base, 0,
           GMMIO_BASE_MASK | UINT64_C(0x7), false),
    REG_RW(HP_ZX1_MIO_GMMIO_DIST_MASK, gmmio_dist_mask, 0,
           GMMIO_BASE_MASK, false),
    REG_RW(HP_ZX1_MIO_GMMIO_DIST_ROUTE, gmmio_dist_route, 0,
           LMMIO_ROUTE_MASK, false),
    REG_RW(HP_ZX1_MIO_IOS_DIST_BASE, ios_dist_base, 0,
           UINT64_C(0x1), false),
    REG_RW(HP_ZX1_MIO_IOS_DIST_MASK, ios_dist_mask,
           UINT64_C(0xffff0000), UINT64_C(0x000f0000), false),
    REG_RW(HP_ZX1_MIO_IOS_DIST_ROUTE, ios_dist_route,
           UINT64_C(12) << 58, LMMIO_ROUTE_MASK, false),
    REG_RW(HP_ZX1_MIO_ROPE_CONFIG_BASE, rope_config_base,
           UINT64_C(0x80000000),
           (UINT64_C(0x3fff) << 17) | 1, false),
    REG_RW(HP_ZX1_MIO_VGA_ROUTE, vga_route, 0,
           (UINT64_C(0x3) << 62) | UINT64_C(0xf), false),
    REG_RW(HP_ZX1_MIO_IOS_DIR_BASE, ios_dir_base, 0,
           IOS_DIR_MASK | 1, false),
    REG_RW(HP_ZX1_MIO_IOS_DIR_MASK, ios_dir_mask, UINT64_C(0xffff0000),
           IOS_DIR_MASK, false),
    REG_RW(HP_ZX1_MIO_IOS_DIR_ROUTE, ios_dir_route, 0,
           UINT64_C(0x7), false),

    REG_RO(HP_ZX1_MIO_F1_ID, UINT64_C(0x122a103c), true),
    REG_RO(HP_ZX1_MIO_F1_CLASS, ZX1_CLASS_VALUE, true),
    REG_RW(HP_ZX1_MIO_ROPE_CONFIG, rope_config, UINT64_C(0x00ff),
           UINT64_C(0xffff), true),
    REG_RW(HP_ZX1_MIO_ERROR_CONFIG, error_config, 0,
           HP_ZX1_MIO_ERROR_CONFIG_NOTIFY, true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(0), lba_port_control[0], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(1), lba_port_control[1], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(2), lba_port_control[2], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(3), lba_port_control[3], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(4), lba_port_control[4], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(5), lba_port_control[5], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(6), lba_port_control[6], 0,
           UINT64_C(0x70), true),
    REG_RW(HP_ZX1_MIO_LBA_PORT_CONTROL(7), lba_port_control[7], 0,
           UINT64_C(0x70), true),
};

void hp_zx1_mio_regs_report_fault(HPZX1MIORegs *regs, uint64_t status,
                                  uint64_t address, uint64_t information)
{
    uint64_t reason = status & (HP_ZX1_MIO_ERROR_IOMMU |
                                HP_ZX1_MIO_ERROR_CSR_DECODE);

    if (!regs || !reason) {
        return;
    }
    if (regs->error_status & HP_ZX1_MIO_ERROR_VALID) {
        regs->error_status |= reason | HP_ZX1_MIO_ERROR_MULTIPLE;
        return;
    }
    regs->error_status = reason | HP_ZX1_MIO_ERROR_VALID;
    regs->error_address = address;
    regs->error_information = information;
}

static const HPZX1MIORegDesc *hp_zx1_mio_find_reg(uint64_t offset)
{
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(hp_zx1_mio_reg_descs); i++) {
        if (hp_zx1_mio_reg_descs[i].offset == offset) {
            return &hp_zx1_mio_reg_descs[i];
        }
    }

    return NULL;
}

static uint64_t *hp_zx1_mio_reg_ptr(HPZX1MIORegs *regs,
                                    const HPZX1MIORegDesc *desc)
{
    return (uint64_t *)((uint8_t *)regs + desc->state_offset);
}

static const uint64_t *hp_zx1_mio_const_reg_ptr(
    const HPZX1MIORegs *regs, const HPZX1MIORegDesc *desc)
{
    return (const uint64_t *)((const uint8_t *)regs + desc->state_offset);
}

static bool hp_zx1_mio_f1_write_shape_valid(uint64_t offset,
                                            unsigned int size,
                                            uint64_t base)
{
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return false;
    }
    if (offset & (size - 1)) {
        return false;
    }

    return offset >= base && offset - base + size <= 8;
}

static uint64_t hp_zx1_mio_merge_be(uint64_t old, uint64_t value,
                                    uint8_t byte_enable)
{
    unsigned int lane;

    for (lane = 0; lane < 8; lane++) {
        uint64_t mask;

        if (!(byte_enable & (1U << lane))) {
            continue;
        }
        mask = UINT64_C(0xff) << (lane * 8);
        old = (old & ~mask) | (value & mask);
    }
    return old;
}

void hp_zx1_mio_regs_reset(HPZX1MIORegs *regs)
{
    unsigned int i;

    if (!regs) {
        return;
    }

    memset(regs, 0, sizeof(*regs));
    for (i = 0; i < G_N_ELEMENTS(hp_zx1_mio_reg_descs); i++) {
        const HPZX1MIORegDesc *desc = &hp_zx1_mio_reg_descs[i];

        if (desc->state_offset != NO_STATE_OFFSET) {
            *hp_zx1_mio_reg_ptr(regs, desc) = desc->reset;
        }
    }
}

bool hp_zx1_mio_regs_read(const HPZX1MIORegs *regs, uint64_t offset,
                          unsigned int size, uint64_t *value)
{
    const HPZX1MIORegDesc *desc;

    if (!regs || !value || size != 8 || (offset & 7)) {
        return false;
    }

    switch (offset) {
    case HP_ZX1_MIO_ERROR_STATUS:
        *value = regs->error_status;
        return true;
    case HP_ZX1_MIO_ERROR_ADDRESS:
        *value = regs->error_address;
        return true;
    case HP_ZX1_MIO_ERROR_INFORMATION:
        *value = regs->error_information;
        return true;
    default:
        break;
    }

    desc = hp_zx1_mio_find_reg(offset);
    if (!desc) {
        return false;
    }

    if (desc->state_offset == NO_STATE_OFFSET) {
        *value = desc->reset;
    } else {
        *value = *hp_zx1_mio_const_reg_ptr(regs, desc);
    }
    return true;
}

bool hp_zx1_mio_regs_write(HPZX1MIORegs *regs, uint64_t offset,
                           unsigned int size, uint64_t value)
{
    const HPZX1MIORegDesc *desc;
    uint64_t base = offset & ~UINT64_C(7);
    uint64_t *reg;
    unsigned int lane;
    uint8_t byte_enable;

    if (!regs) {
        return false;
    }

    if (base == HP_ZX1_MIO_ERROR_STATUS) {
        if (!hp_zx1_mio_f1_write_shape_valid(offset, size, base)) {
            return false;
        }
        lane = offset & 7;
        byte_enable = size == 8 ? UINT8_MAX :
                      ((1U << size) - 1) << lane;
        value = size == 8 ? value : value << (lane * 8);
        return hp_zx1_mio_regs_write_be(regs, base, value, byte_enable);
    }
    if (base == HP_ZX1_MIO_ERROR_ADDRESS ||
        base == HP_ZX1_MIO_ERROR_INFORMATION) {
        return hp_zx1_mio_f1_write_shape_valid(offset, size, base);
    }

    desc = hp_zx1_mio_find_reg(base);
    if (!desc) {
        return false;
    }

    if (desc->function1) {
        if (!hp_zx1_mio_f1_write_shape_valid(offset, size, base)) {
            return false;
        }
        lane = offset & 7;
        byte_enable = size == 8 ? UINT8_MAX :
                      ((1U << size) - 1) << lane;
        value = size == 8 ? value : value << (lane * 8);
        return hp_zx1_mio_regs_write_be(regs, base, value, byte_enable);
    } else if (size != 8 || offset != base) {
        return false;
    }

    /* Writes to read-only registers are accepted and ignored. */
    if (desc->state_offset == NO_STATE_OFFSET) {
        return true;
    }

    reg = hp_zx1_mio_reg_ptr(regs, desc);
    *reg = (*reg & ~desc->writable_mask) |
           (value & desc->writable_mask);
    return true;
}

bool hp_zx1_mio_regs_write_be(HPZX1MIORegs *regs, uint64_t offset,
                              uint64_t value, uint8_t byte_enable)
{
    const HPZX1MIORegDesc *desc;
    uint64_t *reg;
    uint64_t merged;

    if (!regs || (offset & 7)) {
        return false;
    }

    if (offset == HP_ZX1_MIO_ERROR_STATUS) {
        value &= hp_zx1_mio_merge_be(0, UINT64_MAX, byte_enable);
        regs->error_status &= ~(value & HP_ZX1_MIO_ERROR_STATUS_W1C);
        if (!(regs->error_status & HP_ZX1_MIO_ERROR_VALID)) {
            regs->error_status = 0;
            regs->error_address = 0;
            regs->error_information = 0;
        }
        return true;
    }
    if (offset == HP_ZX1_MIO_ERROR_ADDRESS ||
        offset == HP_ZX1_MIO_ERROR_INFORMATION) {
        return true;
    }

    desc = hp_zx1_mio_find_reg(offset);
    if (!desc || !desc->function1) {
        return false;
    }

    /* Writes to read-only registers are accepted and ignored. */
    if (desc->state_offset == NO_STATE_OFFSET) {
        return true;
    }

    reg = hp_zx1_mio_reg_ptr(regs, desc);
    merged = hp_zx1_mio_merge_be(*reg, value, byte_enable);
    *reg = (*reg & ~desc->writable_mask) |
           (merged & desc->writable_mask);
    return true;
}

bool hp_zx1_mio_regs_state_valid(const HPZX1MIORegs *regs)
{
    unsigned int i;

    if (!regs) {
        return false;
    }

    for (i = 0; i < G_N_ELEMENTS(hp_zx1_mio_reg_descs); i++) {
        const HPZX1MIORegDesc *desc = &hp_zx1_mio_reg_descs[i];
        uint64_t value;

        if (desc->state_offset == NO_STATE_OFFSET) {
            continue;
        }
        value = *hp_zx1_mio_const_reg_ptr(regs, desc);
        if ((value & ~desc->writable_mask) !=
            (desc->reset & ~desc->writable_mask)) {
            return false;
        }
    }
    if ((regs->error_status & ~HP_ZX1_MIO_ERROR_STATUS_W1C) ||
        (!(regs->error_status & HP_ZX1_MIO_ERROR_VALID) &&
         (regs->error_status || regs->error_address ||
          regs->error_information))) {
        return false;
    }
    return true;
}
