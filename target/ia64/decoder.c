/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 bundle/template decode helpers shared by the runtime target and tests.
 */

#include "target/ia64/decoder.h"

static const IA64TemplateInfo ia64_templates[32] = {
    [0x00] = { true,
               "MII",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I },
               { false, false, false } },
    [0x01] = { true,
               "MII",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I },
               { false, false, true } },
    [0x02] = { true,
               "MII",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I },
               { false, true, false } },
    [0x03] = { true,
               "MII",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I },
               { false, true, true } },
    [0x04] = { true,
               "MLX",
               { IA64_UNIT_M, IA64_UNIT_L, IA64_UNIT_X },
               { false, false, false } },
    [0x05] = { true,
               "MLX",
               { IA64_UNIT_M, IA64_UNIT_L, IA64_UNIT_X },
               { false, false, true } },
    [0x06] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x07] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x08] = { true,
               "MMI",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I },
               { false, false, false } },
    [0x09] = { true,
               "MMI",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I },
               { false, false, true } },
    [0x0a] = { true,
               "MMI",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I },
               { true, false, false } },
    [0x0b] = { true,
               "MMI",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I },
               { true, false, true } },
    [0x0c] = { true,
               "MFI",
               { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_I },
               { false, false, false } },
    [0x0d] = { true,
               "MFI",
               { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_I },
               { false, false, true } },
    [0x0e] = { true,
               "MMF",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_F },
               { false, false, false } },
    [0x0f] = { true,
               "MMF",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_F },
               { false, false, true } },
    [0x10] = { true,
               "MIB",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_B },
               { false, false, false } },
    [0x11] = { true,
               "MIB",
               { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_B },
               { false, false, true } },
    [0x12] = { true,
               "MBB",
               { IA64_UNIT_M, IA64_UNIT_B, IA64_UNIT_B },
               { false, false, false } },
    [0x13] = { true,
               "MBB",
               { IA64_UNIT_M, IA64_UNIT_B, IA64_UNIT_B },
               { false, false, true } },
    [0x14] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x15] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x16] = { true,
               "BBB",
               { IA64_UNIT_B, IA64_UNIT_B, IA64_UNIT_B },
               { false, false, false } },
    [0x17] = { true,
               "BBB",
               { IA64_UNIT_B, IA64_UNIT_B, IA64_UNIT_B },
               { false, false, true } },
    [0x18] = { true,
               "MMB",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_B },
               { false, false, false } },
    [0x19] = { true,
               "MMB",
               { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_B },
               { false, false, true } },
    [0x1a] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x1b] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x1c] = { true,
               "MFB",
               { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_B },
               { false, false, false } },
    [0x1d] = { true,
               "MFB",
               { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_B },
               { false, false, true } },
    [0x1e] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
    [0x1f] = { false,
               "reserved",
               { IA64_UNIT_RESERVED, IA64_UNIT_RESERVED, IA64_UNIT_RESERVED },
               { false, false, false } },
};

const IA64TemplateInfo *ia64_template_info(uint8_t code)
{
    return &ia64_templates[code & 0x1f];
}
uint8_t ia64_bundle_template_code(uint64_t low_word)
{
    return low_word & 0x1f;
}

uint64_t ia64_bundle_slot(uint64_t low_word, uint64_t high_word, unsigned slot)
{
    switch (slot) {
    case 0:
        return (low_word >> 5) & IA64_SLOT_MASK;
    case 1:
        return ((low_word >> 46) | (high_word << 18)) & IA64_SLOT_MASK;
    case 2:
        return (high_word >> 23) & IA64_SLOT_MASK;
    default:
        return 0;
    }
}
