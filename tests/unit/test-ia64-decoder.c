/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 decoder unit tests.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "target/ia64/decode/decode.h"

typedef const char *(*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

static char failure[128];

static const uint8_t reserved_templates[] = {
    0x06, 0x07, 0x14, 0x15, 0x1a, 0x1b, 0x1e, 0x1f,
};

static const char *failf(const char *message)
{
    snprintf(failure, sizeof(failure), "%s", message);
    return failure;
}

static const char *fail_uint(const char *label, unsigned long long actual,
                             unsigned long long expected)
{
    snprintf(failure, sizeof(failure), "%s: expected %llu got %llu",
             label, expected, actual);
    return failure;
}

static void set_bits(uint8_t bytes[16], unsigned low, unsigned width,
                     uint64_t value)
{
    unsigned i;

    for (i = 0; i < width; ++i) {
        const unsigned bit = low + i;
        const uint8_t mask = 1u << (bit & 7);

        if ((value >> i) & 1ULL) {
            bytes[bit >> 3] |= mask;
        } else {
            bytes[bit >> 3] &= ~mask;
        }
    }
}

static void build_bundle(uint8_t bytes[16], uint8_t template_code,
                         uint64_t slot0, uint64_t slot1, uint64_t slot2)
{
    memset(bytes, 0, 16);
    set_bits(bytes, 0, 5, template_code);
    set_bits(bytes, 5, 41, slot0);
    set_bits(bytes, 46, 41, slot1);
    set_bits(bytes, 87, 41, slot2);
}

static uint64_t load_le64(const uint8_t *bytes)
{
    unsigned i;
    uint64_t value = 0;

    for (i = 0; i < 8; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8);
    }
    return value;
}

static const char *test_template_inventory(void)
{
    unsigned i;
    unsigned defined = 0;
    unsigned reserved = 0;

    for (i = 0; i < 32; ++i) {
        const IA64TemplateInfo *info = ia64_template_info(i);
        bool expected_reserved = false;
        unsigned j;

        for (j = 0; j < sizeof(reserved_templates); ++j) {
            if (reserved_templates[j] == i) {
                expected_reserved = true;
                break;
            }
        }

        if (info->defined == expected_reserved) {
            snprintf(failure, sizeof(failure),
                     "template 0x%02x: expected %s", i,
                     expected_reserved ? "reserved" : "defined");
            return failure;
        }

        if (info->defined) {
            ++defined;
        } else {
            ++reserved;
        }
    }

    if (defined != 24) {
        return fail_uint("defined templates", defined, 24);
    }
    if (reserved != 8) {
        return fail_uint("reserved templates", reserved, 8);
    }
    if (strcmp(ia64_template_info(0x04)->name, "MLX") != 0) {
        return failf("template 0x04 name");
    }
    if (strcmp(ia64_template_info(0x16)->name, "BBB") != 0) {
        return failf("template 0x16 name");
    }
    return NULL;
}

static const char *test_template_stops(void)
{
    static const uint8_t expected[32] = {
        0, 4, 2, 6, 0, 4, 0, 0,
        0, 4, 1, 5, 0, 4, 0, 4,
        0, 4, 0, 4, 0, 0, 0, 4,
        0, 4, 0, 0, 0, 4, 0, 0,
    };
    unsigned int code;

    for (code = 0; code < 32; code++) {
        const IA64TemplateInfo *info = ia64_template_info(code);
        uint8_t actual = ((uint8_t)info->stop_after[0] << 0) |
                         ((uint8_t)info->stop_after[1] << 1) |
                         ((uint8_t)info->stop_after[2] << 2);

        if (actual != expected[code]) {
            snprintf(failure, sizeof(failure),
                     "template 0x%02x stop map: expected 0x%x got 0x%x",
                     code, expected[code], actual);
            return failure;
        }
    }
    return NULL;
}

static const char *test_bundle_unpack(void)
{
    uint8_t bytes[16];
    const uint64_t slot0 = 0x0012345678ULL & IA64_SLOT_MASK;
    const uint64_t slot1 = 0x000abcdef0ULL & IA64_SLOT_MASK;
    const uint64_t slot2 = 0x00013579bdULL & IA64_SLOT_MASK;
    uint64_t low;
    uint64_t high;

    build_bundle(bytes, 0x10, slot0, slot1, slot2);
    low = load_le64(bytes);
    high = load_le64(bytes + 8);

    if (ia64_bundle_template_code(low) != 0x10) {
        return fail_uint("bundle template",
                         ia64_bundle_template_code(low), 0x10);
    }
    if (ia64_bundle_slot(low, high, 0) != slot0) {
        return failf("slot 0 unpack");
    }
    if (ia64_bundle_slot(low, high, 1) != slot1) {
        return failf("slot 1 unpack");
    }
    if (ia64_bundle_slot(low, high, 2) != slot2) {
        return failf("slot 2 unpack");
    }
    return NULL;
}

static const char *test_instruction_decode(void)
{
    Ia64Instruction insn;

    insn = ia64_decode_insn(IA64_UNIT_RESERVED, 0, 0x1000, 0);
    if (insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_RESERVED) {
        return failf("reserved-unit instruction must be illegal");
    }

    insn = ia64_decode_insn(IA64_UNIT_M, (1ULL << 27) | 5,
                            0x2000, 1);
    if (!insn.valid || insn.opcode != IA64_OP_NOP || insn.qp != 5 ||
        insn.address != 0x2000 || insn.slot != 1) {
        return failf("M-unit nop decode");
    }
    if (insn.operands.system.immediate != 0) {
        return failf("typed system immediate operand");
    }

    {
        const uint64_t imm21 = 0x145678;
        const uint64_t raw = (1ULL << 27) | (1ULL << 26) |
                             ((imm21 & 0xfffff) << 6) |
                             ((imm21 >> 20) << 36) | 17;

        insn = ia64_decode_insn(IA64_UNIT_I, raw, 0x2800, 1);
        if (!insn.valid || insn.opcode != IA64_OP_HINT_I || insn.qp != 17 ||
            insn.operands.system.immediate != imm21) {
            return failf("I18 hint.i must accept the full imm21 field");
        }
    }

    insn = ia64_decode_insn(IA64_UNIT_I, 0x2a, 0x3000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_BREAK || insn.qp != 0x2a) {
        return failf("I-unit break decode");
    }

    insn = ia64_decode_insn(IA64_UNIT_B,
                            (1ULL << 36) | (0x5a5aULL << 6) | 7,
                            0x3000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_BREAK || insn.qp != 7 ||
        insn.operands.system.immediate != 0 ||
        insn.raw != ((1ULL << 36) | (0x5a5aULL << 6) | 7)) {
        return failf("B-unit break must ignore imm21 and retain raw bits");
    }

    /*
     * This long forward chk.s.i displacement shares the hint.i x6 and r3
     * fields.  The x3 field distinguishes the two encodings.
     */
    insn = ia64_decode_insn(IA64_UNIT_I, 0x020c042680ULL, 0x1b6c0, 2);
    if (!insn.valid || insn.opcode != IA64_OP_CHK_S ||
        insn.operands.memory.source != 33 ||
        insn.operands.memory.immediate != 0x601a0) {
        return failf("I-unit chk.s must not decode as hint.i");
    }
    return NULL;
}

static const char *test_a7_constant_zero_field(void)
{
    static const IA64SlotUnit units[] = { IA64_UNIT_M, IA64_UNIT_I };
    const uint64_t raw = (0xcULL << 37) | (1ULL << 36) |
                         (1ULL << 33) | (3ULL << 20) | 17;
    unsigned i;

    for (i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
        Ia64Instruction insn =
            ia64_decode_insn(units[i], raw, 0x3800, i);

        if (!insn.valid || insn.opcode != IA64_OP_CMP_GE_AND ||
            insn.encoding_class != IA64_ENCODING_DEFINED) {
            return failf("A7 literal-zero canonical encoding");
        }

        insn = ia64_decode_insn(units[i], raw | (0x55ULL << 13),
                                0x3800, i);
        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
            return failf("A7 bits 19:13 must be a constant-zero field");
        }
    }
    return NULL;
}

static const char *test_i_unit_reserved_cells(void)
{
    static const uint64_t raws[] = {
        (4ULL << 33) | 17,                         /* Table 4-24 */
        (2ULL << 27) | 17,                         /* Table 4-25 */
        (1ULL << 37) | 17,                         /* Table 4-3 */
        (2ULL << 37) | 17,                         /* Table 4-3 */
        (3ULL << 37) | 17,                         /* Table 4-3 */
        (5ULL << 37) | (2ULL << 34) | 17,          /* Table 4-21 */
        (6ULL << 37) | 17,                         /* Table 4-3 */
        (7ULL << 37) | 17,                         /* Table 4-17 */
        (7ULL << 37) | (1ULL << 36) | (3ULL << 33) |
            (0x12ULL << 27) | 17,                  /* Table 4-20 */
        (7ULL << 37) | (1ULL << 36) | (3ULL << 33) |
            (0x1aULL << 27) | 17,                  /* Table 4-20 */
        (8ULL << 37) | (2ULL << 34) |
            (1ULL << 33) | 17,                     /* Table 4-8 */
        (0xaULL << 37) | 17,                       /* Table 4-3 */
        (0xbULL << 37) | 17,                       /* Table 4-3 */
        (0xfULL << 37) | 17,                       /* Table 4-3 */
    };
    unsigned i;

    for (i = 0; i < sizeof(raws) / sizeof(raws[0]); ++i) {
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_I, raws[i], 0x3c00, 1);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("I-unit purple cell must be predicated-reserved");
        }
    }

    {
        const uint64_t raw = (8ULL << 37) | (2ULL << 34) |
                             (1ULL << 33) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raw, 0x3c00, 0);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("M-slot A-unit purple cell must be predicated-reserved");
        }
    }
    return NULL;
}

static const char *test_i9_constant_zero_field(void)
{
    static const struct {
        uint8_t x6;
        Ia64Opcode opcode;
    } cases[] = {
        { 0x12, IA64_OP_POPCNT },
        { 0x1a, IA64_OP_CLZ },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const uint64_t raw = (7ULL << 37) | (3ULL << 33) |
                             ((uint64_t)cases[i].x6 << 27) |
                             (3ULL << 20) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_I, raw, 0x3e00, 1);

        if (!insn.valid || insn.opcode != cases[i].opcode ||
            insn.encoding_class != IA64_ENCODING_DEFINED) {
            return failf("I9 canonical encoding");
        }

        insn = ia64_decode_insn(IA64_UNIT_I, raw | (1ULL << 27),
                                0x3e00, 1);
        if (!insn.valid || insn.opcode != cases[i].opcode ||
            insn.encoding_class != IA64_ENCODING_DEFINED) {
            return failf("I9 bit 27 must be ignored");
        }

        insn = ia64_decode_insn(IA64_UNIT_I, raw | (0x55ULL << 13),
                                0x3e00, 1);
        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
            return failf("I9 bits 19:13 must be a constant-zero field");
        }
    }
    return NULL;
}

static const char *test_setf_getf_ignored_bit36(void)
{
    static const struct {
        uint8_t major;
        uint16_t selector;
        Ia64Opcode opcode;
    } cases[] = {
        { 4, 0xe1, IA64_OP_GETF_SIG },
        { 4, 0xe9, IA64_OP_GETF_EXP },
        { 4, 0xf1, IA64_OP_GETF_S },
        { 4, 0xf9, IA64_OP_GETF_D },
        { 6, 0xe1, IA64_OP_SETF_SIG },
        { 6, 0xe9, IA64_OP_SETF_EXP },
        { 6, 0xf1, IA64_OP_SETF_S },
        { 6, 0xf9, IA64_OP_SETF_D },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint64_t raw = ((uint64_t)cases[i].major << 37) |
                       (1ULL << 36) |
                       ((uint64_t)cases[i].selector << 27) |
                       (3ULL << 13) | (4ULL << 6) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raw, 0x3f00, 0);

        if (!insn.valid || insn.opcode != cases[i].opcode ||
            insn.operands.decoder.r1 != 4 ||
            insn.operands.decoder.r2 != 3 ||
            insn.encoding_class != IA64_ENCODING_DEFINED) {
            return failf("M18/M19 bit 36 must be ignored");
        }
    }
    return NULL;
}

static const char *test_fc_and_fc_i_decode(void)
{
    const uint64_t raw = (1ULL << 37) | (0x30ULL << 27) |
                         (3ULL << 20) | 17;
    Ia64Instruction fc =
        ia64_decode_insn(IA64_UNIT_M, raw, 0x3f10, 0);
    Ia64Instruction fc_i =
        ia64_decode_insn(IA64_UNIT_M, raw | (1ULL << 36), 0x3f10, 0);

    if (!fc.valid || fc.opcode != IA64_OP_FC || fc.qp != 17 ||
        fc.operands.decoder.r3 != 3 ||
        fc.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("M28 x=0 must decode as fc");
    }
    if (!fc_i.valid || fc_i.opcode != IA64_OP_FC_I || fc_i.qp != 17 ||
        fc_i.operands.decoder.r3 != 3 ||
        fc_i.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("M28 x=1 must decode as fc.i");
    }
    return NULL;
}

static const char *test_a_unit_reserved_aliases(void)
{
    static const struct {
        uint8_t x4;
        uint8_t x2b;
    } reserved[] = {
        { 0x0, 3 },
        { 0x5, 0 },
        { 0x7, 0 },
        { 0x7, 1 },
        { 0x8, 0 },
        { 0x8, 1 },
        { 0x8, 2 },
        { 0xa, 1 },
    };
    static const IA64SlotUnit units[] = { IA64_UNIT_M, IA64_UNIT_I };
    unsigned i;
    unsigned j;

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        uint64_t raw = (8ULL << 37) |
                       ((uint64_t)reserved[i].x4 << 29) |
                       ((uint64_t)reserved[i].x2b << 27) | 17;

        for (j = 0; j < sizeof(units) / sizeof(units[0]); ++j) {
            Ia64Instruction insn =
                ia64_decode_insn(units[j], raw, 0x4000, j);

            if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
                insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP ||
                insn.qp != 17) {
                snprintf(failure, sizeof(failure),
                         "A-unit reserved x4=%x x2b=%x unit=%u",
                         reserved[i].x4, reserved[i].x2b, units[j]);
                return failure;
            }
        }
    }

    {
        uint64_t raw = (8ULL << 37) | (1ULL << 34) |
                       (5ULL << 29) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_I, raw, 0x4000, 1);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("multimedia blank entry must be predicated-reserved");
        }
    }
    {
        static const struct {
            uint8_t x4;
            Ia64Opcode opcode;
        } ignored_bit36[] = {
            { 0, IA64_OP_ADD },
            { 4, IA64_OP_SHLADD },
        };
        static const IA64SlotUnit ignored_units[] = {
            IA64_UNIT_M, IA64_UNIT_I,
        };
        unsigned unit_idx;

        for (i = 0; i < sizeof(ignored_bit36) / sizeof(ignored_bit36[0]); ++i) {
            uint64_t raw = (8ULL << 37) | (1ULL << 36) |
                           ((uint64_t)ignored_bit36[i].x4 << 29) | 17;

            for (unit_idx = 0;
                 unit_idx < sizeof(ignored_units) / sizeof(ignored_units[0]);
                 ++unit_idx) {
                Ia64Instruction insn = ia64_decode_insn(
                    ignored_units[unit_idx], raw, 0x4000, unit_idx);

                if (!insn.valid || insn.opcode != ignored_bit36[i].opcode ||
                    insn.encoding_class != IA64_ENCODING_DEFINED) {
                    return failf("A1/A2 bit 36 must be ignored");
                }
            }
        }
    }
    for (i = 4; i <= 6; i += 2) {
        uint64_t raw = (8ULL << 37) | (1ULL << 34) | (1ULL << 33) |
                       ((uint64_t)i << 29) | (3ULL << 27) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_I, raw, 0x4000, 1);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("packed shift-add count 4 must be reserved");
        }
    }
    return NULL;
}

static const char *test_lfetch_count_decode(void)
{
    uint64_t raw = (6ULL << 37) | (0x2cULL << 30) | (2ULL << 28) |
                   (7ULL << 20) | (1ULL << 19) | (0x1dULL << 14) |
                   (1ULL << 12) | (62ULL << 6) | 9;
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_M, raw, 0x5000, 0);

    if (!insn.valid || insn.opcode != IA64_OP_LFETCH_COUNT ||
        insn.qp != 9 || insn.operands.memory.base != 7) {
        return failf("lfetch.count M52 decode");
    }

    insn = ia64_decode_insn(IA64_UNIT_M, raw | (1ULL << 13),
                            0x5000, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("lfetch.count constant-zero field");
    }
    return NULL;
}

static const char *test_reserved_integer_memory_selectors(void)
{
    static const uint64_t raws[] = {
        0x8600000001ULL, /* major 4, m=0, x=0, x6=0x18 */
        0x9600000001ULL, /* major 4, m=1, x=0, x6=0x18 */
        0xa600000001ULL, /* major 5,                 x6=0x18 */
        0x8308000001ULL, /* major 4, m=0, x=1, x6=0x0c */
        0x9008000001ULL, /* major 4, m=1, x=1, x6=0x00 */
    };
    unsigned i;

    for (i = 0; i < sizeof(raws) / sizeof(raws[0]); ++i) {
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raws[i], 0x5000, 0);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP ||
            insn.qp != 1) {
            snprintf(failure, sizeof(failure),
                     "integer-memory selector %u must be predicated-reserved",
                     i);
            return failure;
        }
    }
    return NULL;
}

static const char *test_reserved_fp_memory_selectors(void)
{
    static const uint64_t raws[] = {
        (6ULL << 37) | (0x10ULL << 30) | 17,                /* 4-34 */
        (6ULL << 37) | (1ULL << 36) | (0x10ULL << 30) | 17, /* 4-35 */
        (7ULL << 37) | (0x10ULL << 30) | 17,                /* 4-36 */
        (6ULL << 37) | (1ULL << 27) | 17,                   /* 4-37 */
        (6ULL << 37) | (1ULL << 36) | (1ULL << 27) | 17,    /* 4-38 */
    };
    unsigned i;

    for (i = 0; i < sizeof(raws) / sizeof(raws[0]); ++i) {
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raws[i], 0x5c00, 0);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("FP-memory purple cell must be predicated-reserved");
        }
    }
    return NULL;
}

static const char *test_branch_blank_and_constant_zero_classes(void)
{
    uint64_t raw = (4ULL << 37) | (5ULL << 6) | 1;
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);

    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("B2 low bits must be a constant-zero violation");
    }

    raw = (0x10ULL << 27) | 1;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("B8 low bits must be an unpredicated "
                     "constant-zero violation");
    }

    raw = (1ULL << 27) | 0x3f;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 0x3f ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP_B) {
        return failf("Table 4-48 x6=01 must be B-unit predicated-reserved");
    }

    raw = (3ULL << 27) | 17;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP_B) {
        return failf("Table 4-48 cyan cell must be B-unit predicated-reserved");
    }

    raw = (9ULL << 27) | 17;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP_B) {
        return failf("Table 4-48 x6=09 must be B-unit predicated-reserved");
    }

    raw = (0x22ULL << 27) | 17;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_RESERVED) {
        return failf("Table 4-48 brown cell must be unconditionally reserved");
    }

    raw = (3ULL << 37) | 17;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_NOP || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_IGNORED) {
        return failf("Table 4-3 B-unit major 3 must decode as ignored");
    }

    raw = (2ULL << 37) | (2ULL << 27) | 0x3f;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_NOP || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_IGNORED) {
        return failf("Table 4-55 blank cell must decode as ignored");
    }

    raw = (2ULL << 37) | (0x10ULL << 27) | 0x3f;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_BRP || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-58 unused wh must retain brp behavior");
    }

    raw = (1ULL << 37) | 17;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_BR_CALL_INDIRECT ||
        insn.qp != 17 || insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-53 unused wh must retain indirect-call behavior");
    }

    raw |= 1ULL << 32;
    insn = ia64_decode_insn(IA64_UNIT_B, raw, 0x6000, 2);
    if (!insn.valid || insn.opcode != IA64_OP_BR_CALL_INDIRECT ||
        insn.qp != 17 || insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-53 odd wh must remain indirect call");
    }
    return NULL;
}

static const char *test_m_system_reserved_and_m25_classes(void)
{
    uint64_t raw = 0x0aULL << 27;
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);

    raw = (1ULL << 27) | (1ULL << 26) |
          ((0x145678ULL & 0xfffff) << 6) | (1ULL << 36);
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_HINT_M ||
        insn.encoding_class != IA64_ENCODING_DEFINED ||
        insn.operands.decoder.imm != 0x145678) {
        return failf("M48 hint.m must accept the full imm21 field");
    }

    raw = 0x0aULL << 27;
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);

    if (!insn.valid || insn.opcode != IA64_OP_LOADRS || insn.qp != 0) {
        return failf("M25 loadrs with a zero field must remain defined");
    }

    insn = ia64_decode_insn(IA64_UNIT_M, raw | (1ULL << 36),
                            0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_LOADRS || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("M25 loadrs bit 36 must be ignored");
    }

    insn = ia64_decode_insn(IA64_UNIT_M, raw | 17, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("M25 loadrs low bits must be an unpredicated "
                     "constant-zero violation");
    }

    raw = 0x0cULL << 27;
    insn = ia64_decode_insn(IA64_UNIT_M, raw | (1ULL << 36),
                            0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_FLUSHRS || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("M25 flushrs bit 36 must be ignored");
    }

    insn = ia64_decode_insn(IA64_UNIT_M, raw | 17, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 0 ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("M25 flushrs low bits must be an unpredicated "
                     "constant-zero violation");
    }

    raw = 0x12ULL << 27;
    insn = ia64_decode_insn(IA64_UNIT_M, raw | (1ULL << 36) |
                            (3ULL << 6) | 17, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_INVALAT ||
        insn.operands.decoder.r1 != 3 ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("M26 bit 36 must be ignored");
    }

    /* x3=1 must not alias x4=4 to sum. */
    raw = (1ULL << 33) | (4ULL << 27) | 17;
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
        return failf("Table 4-42 x3=1 must be predicated-reserved");
    }

    raw = (1ULL << 37) | (2ULL << 33) | 17;
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
        return failf("Table 4-44 x3=2 must be predicated-reserved");
    }

    raw = (0xaULL << 37) | 17;
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
        return failf("Table 4-3 M-unit major A must be predicated-reserved");
    }

    raw = 4ULL << 27;
    insn = ia64_decode_insn(IA64_UNIT_M, raw, 0x6800, 0);
    if (!insn.valid || insn.opcode != IA64_OP_SUM_UM ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-43 x3=0 x4=4 must remain sum");
    }
    return NULL;
}

static const char *test_packed_multiply_size_selector_classes(void)
{
    uint64_t raw = (7ULL << 37) | (5ULL << 33) | (0x1eULL << 27);
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_I, raw, 0x6c00, 1);

    if (!insn.valid || insn.opcode != IA64_OP_PMPY2_L ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-18 pmpy2.l must remain defined");
    }

    insn = ia64_decode_insn(IA64_UNIT_I, raw | (1ULL << 36), 0x6c00, 1);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
        return failf("Table 4-20 pmpy2 selector must be predicated-reserved");
    }

    raw = (7ULL << 37) | (1ULL << 33) | (0x1eULL << 27);
    insn = ia64_decode_insn(IA64_UNIT_I, raw, 0x6c00, 1);
    if (!insn.valid || insn.opcode != IA64_OP_PMPYSH2 ||
        insn.encoding_class != IA64_ENCODING_DEFINED) {
        return failf("Table 4-18 pmpyshr2 must remain defined");
    }

    insn = ia64_decode_insn(IA64_UNIT_I, raw | (1ULL << 36), 0x6c00, 1);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
        return failf("Table 4-20 pmpyshr2 selector must be predicated-reserved");
    }
    return NULL;
}

static const char *test_f_unit_ignored_fields_and_reserved_cell(void)
{
    static const struct {
        const char *name;
        uint64_t raw;
        uint64_t ignored_mask;
        Ia64Opcode opcode;
    } cases[] = {
        { "fmin F8", (2ULL << 34) | (0x14ULL << 27) |
                     (7ULL << 20) | (6ULL << 13) | (8ULL << 6) | 17,
          1ULL << 36, IA64_OP_FMIN },
        { "fpmin F8", (1ULL << 37) | (1ULL << 34) | (0x14ULL << 27) |
                      (7ULL << 20) | (6ULL << 13) | (8ULL << 6) | 17,
          1ULL << 36, IA64_OP_FPMIN },
        { "fmerge.s F9", (0x10ULL << 27) | (7ULL << 20) |
                         (6ULL << 13) | (8ULL << 6) | 17,
          7ULL << 34, IA64_OP_FMERGE_S },
        { "fpmerge.s F9", (1ULL << 37) | (0x10ULL << 27) |
                          (7ULL << 20) | (6ULL << 13) | (8ULL << 6) | 17,
          7ULL << 34, IA64_OP_FPMERGE_S },
        { "fpack F9", (0x28ULL << 27) | (7ULL << 20) |
                      (6ULL << 13) | (8ULL << 6) | 17,
          7ULL << 34, IA64_OP_FPACK },
        { "fcvt.fx F10", (3ULL << 34) | (0x18ULL << 27) |
                         (6ULL << 13) | (8ULL << 6) | 17,
          1ULL << 36, IA64_OP_FCVT_FX },
        { "fpcvt.fx F10", (1ULL << 37) | (2ULL << 34) |
                          (0x18ULL << 27) | (6ULL << 13) |
                          (8ULL << 6) | 17,
          1ULL << 36, IA64_OP_FPCVT },
        { "fcvt.xf F11", (0x1cULL << 27) | (6ULL << 13) |
                         (8ULL << 6) | 17,
          7ULL << 34, IA64_OP_FCVT_XF },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Ia64Instruction canonical = ia64_decode_insn(
            IA64_UNIT_F, cases[i].raw, 0x6e00, 1);
        Ia64Instruction ignored = ia64_decode_insn(
            IA64_UNIT_F, cases[i].raw | cases[i].ignored_mask, 0x6e00, 1);

        if (!canonical.valid || !ignored.valid ||
            canonical.opcode != cases[i].opcode ||
            ignored.opcode != cases[i].opcode ||
            canonical.encoding_class != IA64_ENCODING_DEFINED ||
            ignored.encoding_class != IA64_ENCODING_DEFINED ||
            canonical.qp != ignored.qp ||
            canonical.operands.decoder.r1 != ignored.operands.decoder.r1 ||
            canonical.operands.decoder.r2 != ignored.operands.decoder.r2 ||
            canonical.operands.decoder.r3 != ignored.operands.decoder.r3 ||
            canonical.operands.decoder.imm != ignored.operands.decoder.imm) {
            snprintf(failure, sizeof(failure),
                     "%s ignored-field differential", cases[i].name);
            return failure;
        }
    }

    for (i = 0; i < 2; ++i) {
        uint64_t raw = (0x20ULL << 27) | (17ULL << 0) |
                       ((uint64_t)i << 36);
        Ia64Instruction insn = ia64_decode_insn(
            IA64_UNIT_F, raw, 0x6e00, 1);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 17 ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP) {
            return failf("Table 4-60 blank cell must be predicated-reserved");
        }
    }
    return NULL;
}

static const char *test_dep_imm1_constant_zero_field(void)
{
    const uint64_t raw = 0xbe809ec200ULL;
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_I, raw, 0x7000, 1);

    if (!insn.valid || insn.opcode != IA64_OP_DEP_IMM) {
        return failf("I14 bit13=0 must remain dep immediate-1");
    }

    insn = ia64_decode_insn(IA64_UNIT_I, raw | (1ULL << 13), 0x7000, 1);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("I14 bit13=1 must be a constant-zero violation");
    }
    return NULL;
}

static const char *test_tf_constant_zero_field(void)
{
    const uint64_t raw = (5ULL << 37) | (1ULL << 19) |
                         (1ULL << 13) | 1;
    Ia64Instruction insn =
        ia64_decode_insn(IA64_UNIT_I, raw, 0x7000, 1);

    if (!insn.valid || insn.opcode != IA64_OP_TF_Z) {
        return failf("I30 r3=0 must remain tf");
    }

    insn = ia64_decode_insn(IA64_UNIT_I, raw | (3ULL << 20),
                            0x7000, 1);
    if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL || insn.qp != 1 ||
        insn.encoding_class != IA64_ENCODING_CONSTANT_ZERO_VIOLATION) {
        return failf("I30 nonzero r3 must be a constant-zero violation");
    }
    return NULL;
}

static const char *test_ptc_ordering_metadata(void)
{
    static const struct {
        uint8_t x6;
        Ia64Opcode opcode;
    } cases[] = {
        { 0x09, IA64_OP_PTC_L },
        { 0x0a, IA64_OP_PTC_G },
        { 0x0b, IA64_OP_PTC_GA },
        { 0x34, IA64_OP_PTC_E },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint64_t raw = (1ULL << 37) | ((uint64_t)cases[i].x6 << 27);
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raw, 0x7000, 0);

        if (!insn.valid || insn.opcode != cases[i].opcode ||
            !insn.mem_release) {
            return failf("PTC operation must order prior stores");
        }
    }
    return NULL;
}

static const char *test_mlx_break_immediate(void)
{
    const uint64_t imm62 = UINT64_C(0x34b630b4b820032b);
    const uint64_t imm21 = imm62 & 0x1fffff;
    const uint64_t slots[3] = {
        0,
        imm62 >> 21,
        ((imm21 & 0xfffff) << 6) | ((imm21 >> 20) << 36),
    };
    Ia64Instruction insn = { .address = 0x7000 };
    bool skip_x_slot = false;

    ia64_apply_mlx_long_fixup(0x04, slots, 1, &insn, &skip_x_slot);
    if (!skip_x_slot || !insn.valid || insn.opcode != IA64_OP_BREAK ||
        insn.unit != IA64_UNIT_X ||
        insn.operands.system.immediate != imm21) {
        return failf("break.x must expose only its low imm21");
    }
    return NULL;
}

static const char *test_unit_and_major_boundaries(void)
{
    static const uint64_t i_moves[] = {
        3ULL << 33,
        0x32ULL << 27,
        0x2aULL << 27,
    };
    unsigned i;

    for (i = 0; i < sizeof(i_moves) / sizeof(i_moves[0]); ++i) {
        uint64_t raw = i_moves[i] | 17;
        Ia64Instruction i_insn =
            ia64_decode_insn(IA64_UNIT_I, raw, 0x6000, 1);
        Ia64Instruction m_insn =
            ia64_decode_insn(IA64_UNIT_M, raw, 0x6000, 0);

        if (!i_insn.valid || i_insn.opcode == IA64_OP_ILLEGAL) {
            return failf("I-unit move must decode in I slot");
        }
        if (!m_insn.valid || m_insn.opcode != IA64_OP_ILLEGAL ||
            m_insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP ||
            m_insn.qp != 17) {
            return failf("I-unit move encoding in M slot must be "
                         "predicated-reserved");
        }
    }

    for (i = 2; i <= 3; ++i) {
        uint64_t raw = ((uint64_t)i << 37) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_M, raw, 0x6000, 0);

        if (!insn.valid || insn.opcode != IA64_OP_ILLEGAL ||
            insn.encoding_class != IA64_ENCODING_RESERVED_IF_QP ||
            insn.qp != 17) {
            return failf("M-unit major 2/3 must be predicated-reserved");
        }
    }

    {
        uint64_t hint_b = (2ULL << 37) | (1ULL << 27) | 17;
        Ia64Instruction insn =
            ia64_decode_insn(IA64_UNIT_B, hint_b, 0x6000, 2);

        if (!insn.valid || insn.opcode != IA64_OP_HINT_B) {
            return failf("hint.b B9 decode");
        }
    }
    return NULL;
}

static const char *test_typed_operand_views(void)
{
    IA64Operands operands = {
        .decoder = {
            .r1 = 1,
            .r2 = 2,
            .r3 = 3,
            .p1 = 4,
            .p2 = 5,
            .b1 = 6,
            .b2 = 7,
            .sf = 8,
            .fp_precision = 9,
            .imm = -10,
        },
    };

    if (operands.integer.destination != 1 ||
        operands.integer.source1 != 2 ||
        operands.integer.source2 != 3 ||
        operands.integer.predicate1 != 4 ||
        operands.integer.predicate2 != 5 ||
        operands.integer.immediate != -10) {
        return failf("integer operand view");
    }
    if (operands.memory.destination != 1 || operands.memory.source != 2 ||
        operands.memory.base != 3 || operands.memory.immediate != -10) {
        return failf("memory operand view");
    }
    if (operands.branch.link != 6 || operands.branch.target != 7 ||
        operands.branch.displacement != -10) {
        return failf("branch operand view");
    }
    if (operands.floating.destination != 1 ||
        operands.floating.source1 != 2 ||
        operands.floating.source2 != 3 ||
        operands.floating.auxiliary1 != 4 ||
        operands.floating.auxiliary2 != 5 ||
        operands.floating.status_field != 8 ||
        operands.floating.precision != 9 ||
        operands.floating.immediate != -10) {
        return failf("floating operand view");
    }
    if (operands.simd.destination != 1 || operands.simd.source1 != 2 ||
        operands.simd.source2 != 3 || operands.simd.immediate != -10) {
        return failf("SIMD operand view");
    }
    if (operands.system.destination != 1 || operands.system.source != 2 ||
        operands.system.register_index != 3 ||
        operands.system.branch_destination != 6 ||
        operands.system.branch_source != 7 ||
        operands.system.immediate != -10) {
        return failf("system operand view");
    }
    return NULL;
}

int main(void)
{
    static const TestCase tests[] = {
        { "template inventory", test_template_inventory },
        { "template stops", test_template_stops },
        { "bundle unpack", test_bundle_unpack },
        { "instruction decode", test_instruction_decode },
        { "A7 constant-zero field", test_a7_constant_zero_field },
        { "I-unit reserved cells", test_i_unit_reserved_cells },
        { "I9 constant-zero field", test_i9_constant_zero_field },
        { "M18/M19 ignored bit36", test_setf_getf_ignored_bit36 },
        { "fc and fc.i decode", test_fc_and_fc_i_decode },
        { "A-unit reserved aliases", test_a_unit_reserved_aliases },
        { "lfetch.count decode", test_lfetch_count_decode },
        { "reserved integer memory selectors",
          test_reserved_integer_memory_selectors },
        { "reserved FP memory selectors",
          test_reserved_fp_memory_selectors },
        { "branch blank and constant-zero classes",
          test_branch_blank_and_constant_zero_classes },
        { "M system reserved and M25 classes",
          test_m_system_reserved_and_m25_classes },
        { "packed multiply size selector classes",
          test_packed_multiply_size_selector_classes },
        { "F-unit ignored fields and reserved cell",
          test_f_unit_ignored_fields_and_reserved_cell },
        { "dep immediate-1 constant-zero field",
          test_dep_imm1_constant_zero_field },
        { "tf constant-zero field", test_tf_constant_zero_field },
        { "PTC ordering metadata", test_ptc_ordering_metadata },
        { "MLX break immediate", test_mlx_break_immediate },
        { "unit and major boundaries", test_unit_and_major_boundaries },
        { "typed operand views", test_typed_operand_views },
    };
    unsigned i;
    int status = 0;

    printf("TAP version 13\n");
    printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]));

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        const char *error = tests[i].fn();

        if (error == NULL) {
            printf("ok %u - %s\n", i + 1, tests[i].name);
        } else {
            status = 1;
            printf("not ok %u - %s\n", i + 1, tests[i].name);
            printf("# %s\n", error);
        }
    }

    return status;
}
