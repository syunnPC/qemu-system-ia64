/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 instruction decoding.  This translation unit is TCG-independent.
 */

#include "qemu/osdep.h"
#include "target/ia64/decode/decode.h"

typedef struct Ia64A3AluPattern {
    uint8_t x4;
    uint8_t x2b;
    Ia64Opcode opcode;
    bool immediate;
} Ia64A3AluPattern;

static const Ia64A3AluPattern ia64_a3_alu_patterns[] = {
    { 0x0, 0, IA64_OP_ADD, false },
    { 0x0, 1, IA64_OP_ADD_ONE, false },
    { 0x1, 0, IA64_OP_SUB_ONE, false },
    { 0x1, 1, IA64_OP_SUB, false },
    { 0x2, 0, IA64_OP_ADDP4, false },
    { 0x3, 0, IA64_OP_AND, false },
    { 0x3, 1, IA64_OP_ANDCM, false },
    { 0x3, 2, IA64_OP_OR, false },
    { 0x3, 3, IA64_OP_XOR, false },
    { 0x9, 1, IA64_OP_SUB_IMM, true },
    { 0xb, 0, IA64_OP_AND_IMM, true },
    { 0xb, 1, IA64_OP_ANDCM_IMM, true },
    { 0xb, 2, IA64_OP_OR_IMM, true },
    { 0xb, 3, IA64_OP_XOR_IMM, true },
};

static uint64_t ia64_bits(uint64_t value, unsigned low, unsigned width)
{
    return (value >> low) & ((1ULL << width) - 1);
}

static uint64_t ia64_b_op(uint64_t value)
{
    return ia64_bits(value, 37, 4);
}

int64_t ia64_sign_extend(uint64_t value, unsigned width)
{
    const uint64_t sign = 1ULL << (width - 1);
    const uint64_t mask = (1ULL << width) - 1;

    value &= mask;
    return (int64_t)((value ^ sign) - sign);
}

static uint64_t ia64_immu21(uint64_t raw)
{
    return ia64_bits(raw, 6, 20) | (ia64_bits(raw, 36, 1) << 20);
}

static int64_t ia64_imm14(uint64_t raw)
{
    return ia64_sign_extend(ia64_bits(raw, 13, 7) |
                            (ia64_bits(raw, 27, 6) << 7) |
                            (ia64_bits(raw, 36, 1) << 13), 14);
}

static int64_t ia64_imm8(uint64_t raw)
{
    return ia64_sign_extend(ia64_bits(raw, 13, 7) |
                            (ia64_bits(raw, 36, 1) << 7), 8);
}

static uint64_t ia64_pr_mask(uint64_t raw)
{
    uint64_t imm17 = (ia64_bits(raw, 6, 7) << 1) |
                     (ia64_bits(raw, 24, 8) << 8) |
                     (ia64_bits(raw, 36, 1) << 16);

    return ia64_sign_extend(imm17, 17) & ~1ULL;
}

static uint64_t ia64_pr_rot_imm(uint64_t raw)
{
    uint64_t imm = ia64_bits(raw, 6, 7) |
                   (ia64_bits(raw, 13, 7) << 7) |
                   (ia64_bits(raw, 20, 4) << 14) |
                   (ia64_bits(raw, 24, 8) << 18) |
                   (ia64_bits(raw, 32, 1) << 26) |
                   (ia64_bits(raw, 36, 1) << 27);

    /* imm28 encodes imm44{43:16}; imm44 is sign-extended to 64 bits. */
    return (uint64_t)ia64_sign_extend(imm, 28) << 16;
}

static uint64_t ia64_psr_mask(uint64_t raw)
{
    return ia64_bits(raw, 6, 7) |
           (ia64_bits(raw, 13, 7) << 7) |
           (ia64_bits(raw, 20, 7) << 14) |
           (ia64_bits(raw, 31, 2) << 21) |
           (ia64_bits(raw, 36, 1) << 23);
}

uint64_t ia64_low_mask(uint64_t len)
{
    return len >= 64 ? UINT64_MAX : ((1ULL << len) - 1);
}

uint64_t ia64_bitfield_effective_len(uint64_t pos, uint64_t len)
{
    pos &= 0x3f;
    return len > 64 - pos ? 64 - pos : len;
}

uint64_t ia64_deposit_mask(uint64_t pos, uint64_t len)
{
    len = ia64_bitfield_effective_len(pos, len);
    return ia64_low_mask(len) << (pos & 0x3f);
}

static int64_t ia64_imm22(uint64_t raw)
{
    return ia64_sign_extend(ia64_bits(raw, 13, 7) |
                            (ia64_bits(raw, 27, 9) << 7) |
                            (ia64_bits(raw, 22, 5) << 16) |
                            (ia64_bits(raw, 36, 1) << 21), 22);
}

static int64_t ia64_branch_disp(uint64_t raw)
{
    const uint64_t field = ia64_bits(raw, 13, 20) |
                           (ia64_bits(raw, 36, 1) << 20);

    return ia64_sign_extend(field, 21) * 16;
}

static uint64_t ia64_mlx_x1_imm62(uint64_t l_slot, uint64_t x_slot)
{
    return ia64_immu21(x_slot) | (ia64_bits(l_slot, 0, 41) << 21);
}

static int64_t ia64_mlx_brl_disp(uint64_t l_slot, uint64_t x_slot)
{
    const uint64_t field = ia64_bits(x_slot, 13, 20) |
                           (ia64_bits(l_slot, 2, 39) << 20) |
                           (ia64_bits(x_slot, 36, 1) << 59);

    return ia64_sign_extend(field, 60) * 16;
}

static void ia64_fill_mlx_movl(Ia64Instruction *insn,
                               uint64_t l_slot, uint64_t x_slot)
{
    uint64_t i     = (x_slot >> 36) & 1;
    uint64_t imm9d = (x_slot >> 27) & 0x1ff;
    uint64_t imm5c = (x_slot >> 22) & 0x1f;
    uint64_t ic    = (x_slot >> 21) & 1;
    uint64_t imm7b = (x_slot >> 13) & 0x7f;
    uint64_t imm41 = l_slot & 0x1ffffffffffULL;
    uint64_t imm64 = (imm7b)
                   | (imm9d << 7)
                   | (imm5c << 16)
                   | (ic << 21)
                   | (imm41 << 22)
                   | (i << 63);

    insn->qp = x_slot & 0x3f;
    insn->operands.decoder.r1 = (x_slot >> 6) & 0x7f;
    insn->operands.decoder.imm = (int64_t)imm64;
}

static int64_t ia64_chk_disp(uint64_t raw)
{
    const uint64_t field = ia64_bits(raw, 6, 7) |
                           (ia64_bits(raw, 20, 13) << 7) |
                           (ia64_bits(raw, 36, 1) << 20);

    return ia64_sign_extend(field, 21) * 16;
}

static int64_t ia64_chk_a_disp(uint64_t raw)
{
    const uint64_t field = ia64_bits(raw, 13, 20) |
                           (ia64_bits(raw, 36, 1) << 20);

    return ia64_sign_extend(field, 21) * 16;
}


/*
 * Integer load/store x6 opcode extensions, SDM Vol 3 Table 4-30 (and the
 * matching +reg/+imm forms in Tables 4-31/4-32).  Values absent from those
 * tables are purple (Reserved if PR[qp] is 1), so this helper leaves them
 * unmatched for the selector-space catch at the end of ia64_decode_insn().
 * In particular spill/fill exist only as 8-byte forms (ld8.fill x6=0x1b,
 * st8.spill x6=0x3b), so 0x18-0x1a and 0x38-0x3a fall through to the
 * default.  0x28-0x2b are the ld.c.clr.acq forms; their acquire semantics
 * come from ia64_memory_x6a_is_acquire_load() at the decode call sites.
 */
static Ia64Opcode ia64_memory_opcode_from_x6a(uint64_t x6a)
{
    switch (x6a) {
    case 0x00:
        return IA64_OP_LD1;
    case 0x01:
        return IA64_OP_LD2;
    case 0x02:
        return IA64_OP_LD4;
    case 0x03:
        return IA64_OP_LD8;
    case 0x04:
        return IA64_OP_LD1S;
    case 0x05:
        return IA64_OP_LD2S;
    case 0x06:
        return IA64_OP_LD4S;
    case 0x07:
        return IA64_OP_LD8S;
    case 0x08:
        return IA64_OP_LD1A;
    case 0x09:
        return IA64_OP_LD2A;
    case 0x0a:
        return IA64_OP_LD4A;
    case 0x0b:
        return IA64_OP_LD8A;
    case 0x0c:
        return IA64_OP_LD1SA;
    case 0x0d:
        return IA64_OP_LD2SA;
    case 0x0e:
        return IA64_OP_LD4SA;
    case 0x0f:
        return IA64_OP_LD8SA;
    case 0x10:
        return IA64_OP_LD1;
    case 0x11:
        return IA64_OP_LD2;
    case 0x12:
        return IA64_OP_LD4;
    case 0x13:
        return IA64_OP_LD8;
    case 0x14:
        return IA64_OP_LD1;
    case 0x15:
        return IA64_OP_LD2;
    case 0x16:
        return IA64_OP_LD4;
    case 0x17:
        return IA64_OP_LD8;
    case 0x1b:
        return IA64_OP_LD8FILL;
    case 0x20:
        return IA64_OP_LD1C_CLR;
    case 0x21:
        return IA64_OP_LD2C_CLR;
    case 0x22:
        return IA64_OP_LD4C_CLR;
    case 0x23:
        return IA64_OP_LD8C_CLR;
    case 0x24:
        return IA64_OP_LD1C_NC;
    case 0x25:
        return IA64_OP_LD2C_NC;
    case 0x26:
        return IA64_OP_LD4C_NC;
    case 0x27:
        return IA64_OP_LD8C_NC;
    case 0x28:
        return IA64_OP_LD1C_CLR;
    case 0x29:
        return IA64_OP_LD2C_CLR;
    case 0x2a:
        return IA64_OP_LD4C_CLR;
    case 0x2b:
        return IA64_OP_LD8C_CLR;
    case 0x30:
        return IA64_OP_ST1;
    case 0x31:
        return IA64_OP_ST2;
    case 0x32:
        return IA64_OP_ST4;
    case 0x33:
        return IA64_OP_ST8;
    case 0x34:
        return IA64_OP_ST1REL;
    case 0x35:
        return IA64_OP_ST2REL;
    case 0x36:
        return IA64_OP_ST4REL;
    case 0x37:
        return IA64_OP_ST8REL;
    case 0x3b:
        return IA64_OP_ST8SPILL;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static bool ia64_memory_x6a_is_acquire_load(uint64_t x6a)
{
    return (x6a >= 0x14 && x6a <= 0x17) ||
           (x6a >= 0x28 && x6a <= 0x2b);
}

static Ia64Opcode ia64_speculative_load_opcode_from_x6a(uint64_t x6a)
{
    switch (x6a) {
    case 0x00:
        return IA64_OP_LD1S;
    case 0x01:
        return IA64_OP_LD2S;
    case 0x02:
        return IA64_OP_LD4S;
    case 0x03:
        return IA64_OP_LD8S;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_fp_load_opcode_from_x6a(uint64_t x6a)
{
    if ((x6a <= 0x0f) || (x6a >= 0x20 && x6a <= 0x27)) {
        switch (x6a & 3) {
        case 0:
            return IA64_OP_LDFE;
        case 1:
            return IA64_OP_LDF8;
        case 2:
            return IA64_OP_LDFS;
        case 3:
            return IA64_OP_LDFD;
        }
    }

    switch (x6a) {
    case 0x1b:
        return IA64_OP_LDF_FILL;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static void ia64_fp_load_attrs_from_x6a(Ia64Instruction *insn, uint64_t x6a)
{
    switch (x6a >> 2) {
    case 1:
        insn->fp_load_speculative = true;
        break;
    case 2:
        insn->fp_load_advanced = true;
        break;
    case 3:
        insn->fp_load_speculative = true;
        insn->fp_load_advanced = true;
        break;
    case 8:
        insn->fp_load_check = true;
        insn->fp_load_check_clear = true;
        break;
    case 9:
        insn->fp_load_check = true;
        break;
    default:
        break;
    }
}

static Ia64Opcode ia64_fp_load_pair_opcode_from_x6a(uint64_t x6a)
{
    if ((x6a <= 0x0f || (x6a >= 0x20 && x6a <= 0x27)) &&
        (x6a & 3) != 0) {
        switch (x6a & 3) {
        case 1:
            return IA64_OP_LDFP8;
        case 2:
            return IA64_OP_LDFPS;
        case 3:
            return IA64_OP_LDFPD;
        }
    }

    return IA64_OP_ILLEGAL;
}

static Ia64Opcode ia64_fp_store_opcode_from_x6a(uint64_t x6a)
{
    switch (x6a) {
    case 0x30:
        return IA64_OP_STFE;
    case 0x31:
        return IA64_OP_STF8;
    case 0x32:
        return IA64_OP_STFS;
    case 0x33:
        return IA64_OP_STFD;
    case 0x3b:
        return IA64_OP_STF_SPILL;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_check_load_opcode_from_x6a(uint64_t x6a, bool clr)
{
    switch (x6a) {
    case 0x00:
        return clr ? IA64_OP_LD1C_CLR : IA64_OP_LD1C_NC;
    case 0x01:
        return clr ? IA64_OP_LD2C_CLR : IA64_OP_LD2C_NC;
    case 0x02:
        return clr ? IA64_OP_LD4C_CLR : IA64_OP_LD4C_NC;
    case 0x03:
        return clr ? IA64_OP_LD8C_CLR : IA64_OP_LD8C_NC;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_fetchadd_opcode_from_x6a(uint64_t x6a)
{
    switch (x6a) {
    case 0x12:
    case 0x16:
        return IA64_OP_FETCHADD4;
    case 0x13:
    case 0x17:
        return IA64_OP_FETCHADD8;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static bool ia64_fetchadd_x6a_is_acquire(uint64_t x6a)
{
    return x6a == 0x12 || x6a == 0x13;
}

static bool ia64_fetchadd_x6a_is_release(uint64_t x6a)
{
    return x6a == 0x16 || x6a == 0x17;
}

static Ia64Opcode ia64_cmpxchg_acqrel_opcode_from_size(uint64_t size)
{
    switch (size) {
    case 0:
        return IA64_OP_CMPXCHG1;
    case 1:
        return IA64_OP_CMPXCHG2;
    case 2:
        return IA64_OP_CMPXCHG4;
    case 3:
        return IA64_OP_CMPXCHG8;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_xchg_opcode_from_size(uint64_t size)
{
    switch (size) {
    case 0:
        return IA64_OP_XCHG1;
    case 1:
        return IA64_OP_XCHG2;
    case 2:
        return IA64_OP_XCHG4;
    case 3:
        return IA64_OP_XCHG8;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_unpack_opcode_from_fields(uint64_t za, uint64_t zb,
                                                 uint64_t x2b)
{
    static const Ia64Opcode high[4] = {
        IA64_OP_UNPACK1_H, IA64_OP_UNPACK2_H, IA64_OP_UNPACK4_H,
        IA64_OP_ILLEGAL,
    };
    static const Ia64Opcode low[4] = {
        IA64_OP_UNPACK1_L, IA64_OP_UNPACK2_L, IA64_OP_UNPACK4_L,
        IA64_OP_ILLEGAL,
    };
    const unsigned size_code = (za << 1) | zb;

    if (x2b == 0) {
        return high[size_code];
    }
    if (x2b == 2) {
        return low[size_code];
    }
    return IA64_OP_ILLEGAL;
}

static int64_t ia64_fetchadd_imm(uint64_t inc3)
{
    static const int8_t values[8] = {
        16, 8, 4, 1, -16, -8, -4, -1,
    };

    return values[inc3 & 7];
}

static Ia64Opcode ia64_compare_opcode_from_cmp(uint64_t x2a, uint64_t ve)
{
    switch ((x2a << 1) | ve) {
    case 0:
        return IA64_OP_CMP_EQ;
    case 1:
        return IA64_OP_CMP_LT;
    case 2:
        return IA64_OP_CMP_LE;
    case 3:
        return IA64_OP_CMP_GT;
    case 4:
        return IA64_OP_CMP_GE;
    case 5:
        return IA64_OP_CMP_LTU;
    case 6:
        return IA64_OP_CMP_LEU;
    case 7:
        return IA64_OP_CMP_GTU;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static Ia64Opcode ia64_compare_zero_opcode(uint64_t major, bool cmp4,
                                           uint64_t ta, uint64_t c)
{
    static const Ia64Opcode zero_ops[3][2][4] = {
        {
            {
                IA64_OP_CMP_GT_AND,
                IA64_OP_CMP_LE_AND,
                IA64_OP_CMP_GE_AND,
                IA64_OP_CMP_LT_AND,
            },
            {
                IA64_OP_CMP4_GT_AND,
                IA64_OP_CMP4_LE_AND,
                IA64_OP_CMP4_GE_AND,
                IA64_OP_CMP4_LT_AND,
            },
        },
        {
            {
                IA64_OP_CMP_GT_OR,
                IA64_OP_CMP_LE_OR,
                IA64_OP_CMP_GE_OR,
                IA64_OP_CMP_LT_OR,
            },
            {
                IA64_OP_CMP4_GT_OR,
                IA64_OP_CMP4_LE_OR,
                IA64_OP_CMP4_GE_OR,
                IA64_OP_CMP4_LT_OR,
            },
        },
        {
            {
                IA64_OP_CMP_GT_OR_ANDCM,
                IA64_OP_CMP_LE_OR_ANDCM,
                IA64_OP_CMP_GE_OR_ANDCM,
                IA64_OP_CMP_LT_OR_ANDCM,
            },
            {
                IA64_OP_CMP4_GT_OR_ANDCM,
                IA64_OP_CMP4_LE_OR_ANDCM,
                IA64_OP_CMP4_GE_OR_ANDCM,
                IA64_OP_CMP4_LT_OR_ANDCM,
            },
        },
    };
    const unsigned relation = (ta << 1) | c;

    if (major < 0xc || major > 0xe) {
        return IA64_OP_ILLEGAL;
    }
    return zero_ops[major - 0xc][cmp4 ? 1 : 0][relation];
}

static Ia64Opcode ia64_f1_opcode_from_major(uint64_t major, uint64_t f2,
                                            uint64_t f4)
{
    switch (major) {
    case 8:
    case 9:
        if (f4 == 1) {
            return f2 == 0 ? IA64_OP_FNORM : IA64_OP_FADD;
        }
        return f2 == 0 ? IA64_OP_FMPY : IA64_OP_FMA;
    case 0xa:
    case 0xb:
        if (f4 == 1) {
            return f2 == 0 ? IA64_OP_FNORM : IA64_OP_FSUB;
        }
        return IA64_OP_FMS;
    case 0xc:
    case 0xd:
        return IA64_OP_FNMA;
    default:
        return IA64_OP_ILLEGAL;
    }
}

static const Ia64A3AluPattern *ia64_lookup_a3_alu(uint64_t x4, uint64_t x2b)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(ia64_a3_alu_patterns); ++i) {
        const Ia64A3AluPattern *pattern = &ia64_a3_alu_patterns[i];
        if (pattern->x4 == x4 && pattern->x2b == x2b) {
            return pattern;
        }
    }
    return NULL;
}


bool ia64_opcode_is_store(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_ST1:
    case IA64_OP_ST2:
    case IA64_OP_ST4:
    case IA64_OP_ST8:
    case IA64_OP_ST16:
    case IA64_OP_ST1REL:
    case IA64_OP_ST2REL:
    case IA64_OP_ST4REL:
    case IA64_OP_ST8REL:
    case IA64_OP_ST8SPILL:
        return true;
    default:
        return false;
    }
}

bool ia64_opcode_is_release_store(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_ST1REL:
    case IA64_OP_ST2REL:
    case IA64_OP_ST4REL:
    case IA64_OP_ST8REL:
        return true;
    default:
        return false;
    }
}

bool ia64_opcode_is_load(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1:
    case IA64_OP_LD2:
    case IA64_OP_LD4:
    case IA64_OP_LD8:
    case IA64_OP_LD1S:
    case IA64_OP_LD2S:
    case IA64_OP_LD4S:
    case IA64_OP_LD8S:
    case IA64_OP_LD1A:
    case IA64_OP_LD2A:
    case IA64_OP_LD4A:
    case IA64_OP_LD8A:
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
    case IA64_OP_LD8FILL:
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD8C_CLR:
    case IA64_OP_LD1C_NC:
    case IA64_OP_LD2C_NC:
    case IA64_OP_LD4C_NC:
    case IA64_OP_LD8C_NC:
        return true;
    default:
        return false;
    }
}

static bool ia64_is_m_nop(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x7ULL << 33) |
                          (0xfULL << 27) | (0x3ULL << 31) |
                          (1ULL << 26);
    const uint64_t value = 1ULL << 27;

    return (raw & mask) == value;
}

static bool ia64_is_m_break(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x7ULL << 33) |
                          (0xfULL << 27) | (0x3ULL << 31);

    return (raw & mask) == 0;
}

static bool ia64_is_i_nop(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x7ULL << 33) |
                          (0x3fULL << 27) | (1ULL << 26);
    const uint64_t value = 1ULL << 27;

    return (raw & mask) == value;
}

static bool ia64_is_i_break(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x7ULL << 33) |
                          (0x3fULL << 27);

    return (raw & mask) == 0;
}

static bool ia64_is_b_nop(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x3fULL << 27);
    const uint64_t value = 2ULL << 37;

    return (raw & mask) == value;
}

static bool ia64_is_b_break(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (0x3fULL << 27);

    return (raw & mask) == 0;
}

static bool ia64_is_b8(uint64_t raw)
{
    if (ia64_b_op(raw) != 0) {
        return false;
    }

    switch (ia64_bits(raw, 27, 6)) {
    case 0x02: /* cover */
    case 0x04: /* clrrrb */
    case 0x05: /* clrrrb.pr */
    case 0x08: /* rfi */
    case 0x0c: /* bsw.0 */
    case 0x0d: /* bsw.1 */
    case 0x10: /* epc */
    case 0x18: /* vmsw.0 */
    case 0x19: /* vmsw.1 */
        return true;
    default:
        return false;
    }
}

static bool ia64_is_f_nop(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (1ULL << 33) |
                          (0x3fULL << 27);
    const uint64_t value = 1ULL << 27;

    return (raw & mask) == value;
}

static bool ia64_is_f_break(uint64_t raw)
{
    const uint64_t mask = (0xfULL << 37) | (1ULL << 33) |
                          (0x3fULL << 27);

    return (raw & mask) == 0;
}

static Ia64Instruction ia64_base_insn(Ia64Opcode opcode, IA64SlotUnit unit,
                                      uint64_t raw, uint64_t address,
                                      uint8_t slot)
{
    Ia64Instruction insn = {
        .opcode = opcode,
        .unit = unit,
        .raw = raw,
        .address = address,
        .slot = slot,
        .qp = ia64_bits(raw, 0, 6),
        .operands.decoder.sf =
            unit == IA64_UNIT_F ? ia64_bits(raw, 34, 2) : 0,
        .valid = true,
    };

    return insn;
}

static Ia64Instruction ia64_invalid_insn(IA64SlotUnit unit, uint64_t raw,
                                         uint64_t address, uint8_t slot)
{
    Ia64Instruction insn = {
        .opcode = IA64_OP_ILLEGAL,
        .unit = unit,
        .raw = raw,
        .address = address,
        .slot = slot,
        .qp = ia64_bits(raw, 0, 6),
        .encoding_class = IA64_ENCODING_RESERVED,
    };

    return insn;
}

static Ia64Instruction ia64_reserved_if_qp_insn(IA64SlotUnit unit,
                                                 uint64_t raw,
                                                 uint64_t address,
                                                 uint8_t slot)
{
    Ia64Instruction insn =
        ia64_base_insn(IA64_OP_ILLEGAL, unit, raw, address, slot);

    insn.encoding_class = IA64_ENCODING_RESERVED_IF_QP;
    return insn;
}

static Ia64Instruction ia64_reserved_if_qp_b_insn(IA64SlotUnit unit,
                                                   uint64_t raw,
                                                   uint64_t address,
                                                   uint8_t slot)
{
    Ia64Instruction insn =
        ia64_base_insn(IA64_OP_ILLEGAL, unit, raw, address, slot);

    insn.encoding_class = IA64_ENCODING_RESERVED_IF_QP_B;
    return insn;
}

static Ia64Instruction ia64_ignored_insn(IA64SlotUnit unit, uint64_t raw,
                                          uint64_t address, uint8_t slot)
{
    Ia64Instruction insn =
        ia64_base_insn(IA64_OP_NOP, unit, raw, address, slot);

    insn.qp = 0;
    insn.encoding_class = IA64_ENCODING_IGNORED;
    return insn;
}

static Ia64Instruction ia64_constant_zero_violation_insn(
    IA64SlotUnit unit, uint64_t raw, uint64_t address, uint8_t slot)
{
    Ia64Instruction insn =
        ia64_base_insn(IA64_OP_ILLEGAL, unit, raw, address, slot);

    /* Illegal Operation is this implementation's architecturally allowed
     * choice for a nonzero constant-zero field when PR[qp] is true. */
    insn.encoding_class = IA64_ENCODING_CONSTANT_ZERO_VIOLATION;
    return insn;
}

/*
 * MLX long forms are reported at slot 1.  The paired X slot carries the
 * opcode/predicate and low immediate bits, and must not execute separately.
 */
void ia64_apply_mlx_long_fixup(uint8_t template_code,
                                      const uint64_t slots[3],
                                      int slot, Ia64Instruction *insn,
                                      bool *skip_x_slot)
{
    if ((template_code != 4 && template_code != 5) || slot != 1) {
        return;
    }

    uint64_t x_slot = slots[2];
    uint64_t l_slot = slots[1];

    if (ia64_is_i_break(x_slot)) {
        *insn = ia64_base_insn(IA64_OP_BREAK, IA64_UNIT_X, x_slot,
                               insn->address, slot);
        /* break.x records only the low 21 bits in CR.IIM. */
        insn->operands.decoder.imm = ia64_immu21(x_slot);
        *skip_x_slot = true;
    } else if (ia64_b_op(x_slot) == 0xc &&
               ia64_bits(x_slot, 6, 3) == 0) {
        *insn = ia64_base_insn(IA64_OP_BRL_COND, IA64_UNIT_X, x_slot,
                               insn->address, slot);
        insn->operands.decoder.imm = ia64_mlx_brl_disp(l_slot, x_slot);
        *skip_x_slot = true;
    } else if (ia64_b_op(x_slot) == 0xd) {
        *insn = ia64_base_insn(IA64_OP_BRL_CALL, IA64_UNIT_X, x_slot,
                               insn->address, slot);
        insn->operands.decoder.b1 = ia64_bits(x_slot, 6, 3);
        insn->operands.decoder.imm = ia64_mlx_brl_disp(l_slot, x_slot);
        *skip_x_slot = true;
    } else if (ia64_b_op(x_slot) == 0 &&
               ia64_bits(x_slot, 27, 6) == 1) {
        Ia64Opcode opcode = ia64_bits(x_slot, 26, 1) ?
            IA64_OP_HINT_X : IA64_OP_NOP;
        *insn = ia64_base_insn(opcode, IA64_UNIT_X, x_slot,
                               insn->address, slot);
        insn->operands.decoder.imm = ia64_mlx_x1_imm62(l_slot, x_slot);
        *skip_x_slot = true;
    } else if (insn->opcode == IA64_OP_MOVL && ia64_b_op(x_slot) == 6) {
        ia64_fill_mlx_movl(insn, l_slot, x_slot);
        *skip_x_slot = true;
    } else if (insn->opcode == IA64_OP_MOVL) {
        *insn = ia64_invalid_insn(IA64_UNIT_L, l_slot, insn->address, slot);
    }
}

Ia64Instruction ia64_decode_insn(IA64SlotUnit unit, uint64_t raw,
                                        uint64_t address, uint8_t slot)
{
    raw &= IA64_SLOT_MASK;

    if (unit == IA64_UNIT_RESERVED) {
        return ia64_invalid_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_I &&
        ia64_b_op(raw) == 0 && ia64_bits(raw, 33, 3) == 7) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_MOV_GRBR, unit, raw, address, slot);

        /*
         * The btype/wh/ph/ih target-prediction fields are hints; the
         * architectural state update is still only BR[b1] = GR[r2].
         */
        insn.operands.decoder.r2 = ia64_bits(raw, 6, 3);
        insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M) {
        if (ia64_is_m_nop(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_NOP, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }

        /*
         * M50: mov dahr3 = imm16.  This format shares major opcode zero
         * with nop.m and hint.m, but has its own x2/x4/y/z extensions.
         */
        if (ia64_b_op(raw) == 0 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 31, 2) == 1 &&
            ia64_bits(raw, 27, 4) == 2 &&
            ia64_bits(raw, 26, 1) == 1 &&
            ia64_bits(raw, 10, 2) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_DAHR_IMM, unit, raw,
                               address, slot);

            insn.operands.decoder.r1 = ia64_bits(raw, 23, 3);
            insn.operands.decoder.imm =
                ia64_bits(raw, 6, 4) |
                (ia64_bits(raw, 12, 11) << 4) |
                (ia64_bits(raw, 36, 1) << 15);
            return insn;
        }
        if (ia64_is_m_break(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_BREAK, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 6) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_ALLOC, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.imm = (ia64_bits(raw, 13, 7) << 0) |
                       (ia64_bits(raw, 20, 7) << 7) |
                       (ia64_bits(raw, 27, 4) << 14);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x21) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_UMGR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x29) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRUM, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x25) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_PSRGR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x2d) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRPSR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.imm = 1;
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x24) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_CRGR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x2c) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRCR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRRR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x10) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_RRGR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x1e) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_TPA, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            (ia64_bits(raw, 27, 6) == 0x1a ||
             ia64_bits(raw, 27, 6) == 0x1b ||
             ia64_bits(raw, 27, 6) == 0x1f)) {
            Ia64Opcode opcode;

            switch (ia64_bits(raw, 27, 6)) {
            case 0x1a:
                opcode = IA64_OP_THASH;
                break;
            case 0x1b:
                opcode = IA64_OP_TTAG;
                break;
            default:
                opcode = IA64_OP_TAK;
                break;
            }

            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x17) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_CPUID_INDEXED, unit, raw,
                               address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x16) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_MSRGR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x06) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRMSR, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            (ia64_bits(raw, 27, 6) == 0x11 ||
             ia64_bits(raw, 27, 6) == 0x12)) {
            Ia64Instruction insn =
                ia64_base_insn(ia64_bits(raw, 27, 6) == 0x12 ?
                               IA64_OP_MOV_IBRGR_INDEXED :
                               IA64_OP_MOV_DBRGR_INDEXED,
                               unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            (ia64_bits(raw, 27, 6) == 0x01 ||
             ia64_bits(raw, 27, 6) == 0x02)) {
            Ia64Instruction insn =
                ia64_base_insn(ia64_bits(raw, 27, 6) == 0x02 ?
                               IA64_OP_MOV_GRIBR_INDEXED :
                               IA64_OP_MOV_GRDBR_INDEXED,
                               unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x03) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_GRPKR_INDEXED, unit, raw,
                               address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x13) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_MOV_PKRGR_INDEXED, unit, raw,
                               address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            (ia64_bits(raw, 27, 6) == 0x04 ||
             ia64_bits(raw, 27, 6) == 0x05)) {
            Ia64Instruction insn =
                ia64_base_insn(ia64_bits(raw, 27, 6) == 0x04 ?
                               IA64_OP_MOV_GRPMC_INDEXED :
                               IA64_OP_MOV_GRPMD_INDEXED,
                               unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 3) == 0 &&
            (ia64_bits(raw, 27, 6) == 0x14 ||
             ia64_bits(raw, 27, 6) == 0x15)) {
            Ia64Instruction insn =
                ia64_base_insn(ia64_bits(raw, 27, 6) == 0x14 ?
                               IA64_OP_MOV_PMCGR_INDEXED :
                               IA64_OP_MOV_PMDGR_INDEXED,
                               unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ia64_b_op(raw) == 0 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x01 &&
            ia64_bits(raw, 26, 1) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_HINT_M, unit, raw, address, slot);

            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I || unit == IA64_UNIT_X) {
        if (unit == IA64_UNIT_I &&
            ia64_b_op(raw) == 0 &&
            ia64_bits(raw, 33, 3) == 0 &&
            ia64_bits(raw, 27, 6) == 0x01 &&
            ia64_bits(raw, 26, 1) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_HINT_I, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
        if (ia64_is_i_nop(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_NOP, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
        if (ia64_is_i_break(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_BREAK, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }

        if (ia64_b_op(raw) == 1) {
            /* Table 4-3 marks I-unit and X-unit major opcode 1 purple. */
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        }
    }

    if (unit == IA64_UNIT_B) {
        if (ia64_is_b_nop(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_NOP, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
        if (ia64_is_b_break(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_BREAK, unit, raw, address, slot);
            /* B9's imm21 field is ignored and CR.IIM is written as zero. */
            insn.operands.decoder.imm = 0;
            return insn;
        }

        if (ia64_is_b8(raw)) {
            Ia64Instruction insn;

            /* B8 names bits 5:0 as a literal-zero field, not as qp. */
            if (ia64_bits(raw, 0, 6) != 0) {
                insn = ia64_constant_zero_violation_insn(
                    unit, raw, address, slot);
            } else {
                insn = ia64_base_insn(
                    ia64_bits(raw, 27, 6) == 0x02 ? IA64_OP_COVER :
                    ia64_bits(raw, 27, 6) == 0x04 ? IA64_OP_CLRRRB :
                    ia64_bits(raw, 27, 6) == 0x05 ? IA64_OP_CLRRRB_PR :
                    ia64_bits(raw, 27, 6) == 0x08 ? IA64_OP_RFI :
                    ia64_bits(raw, 27, 6) == 0x0c ? IA64_OP_BSW0 :
                    ia64_bits(raw, 27, 6) == 0x0d ? IA64_OP_BSW1 :
                    ia64_bits(raw, 27, 6) == 0x10 ? IA64_OP_EPC :
                    IA64_OP_VMSW,
                    unit, raw, address, slot);
                if (insn.opcode == IA64_OP_VMSW) {
                    insn.operands.decoder.imm =
                        ia64_bits(raw, 27, 6) & 1;
                }
            }
            insn.qp = 0;
            return insn;
        }

        if (ia64_b_op(raw) == 2 && ia64_bits(raw, 27, 6) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_HINT_B, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 8) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const uint64_t ve = ia64_bits(raw, 33, 1);

        if (x2a == 2 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_ADDS, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm14(raw);
            return insn;
        }

        const uint64_t x4 = ia64_bits(raw, 29, 4);
        const uint64_t x2b = ia64_bits(raw, 27, 2);
        const unsigned size_code = ve | (ia64_bits(raw, 36, 1) << 1);
        if (x2a == 1 && (x4 == 0 || x4 == 1) &&
            ((size_code < 2 && x2b <= 3) ||
             (size_code == 2 && x2b == 0))) {
            Ia64Opcode opcode = IA64_OP_ILLEGAL;
            if (x4 == 0) {
                if (size_code == 0) {
                    opcode = IA64_OP_PADD1;
                } else if (size_code == 1) {
                    opcode = IA64_OP_PADD2;
                } else if (size_code == 2) {
                    opcode = IA64_OP_PADD4;
                }
            } else {
                if (size_code == 0) {
                    opcode = IA64_OP_PSUB1;
                } else if (size_code == 1) {
                    opcode = IA64_OP_PSUB2;
                } else if (size_code == 2) {
                    opcode = IA64_OP_PSUB4;
                }
            }
            if (opcode != IA64_OP_ILLEGAL) {
                Ia64Instruction insn =
                    ia64_base_insn(opcode, unit, raw, address, slot);
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.imm = x2b;
                return insn;
            }
        }

        if (x2a == 1 && size_code == 1 && x4 == 4 && x2b <= 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_PSHLADD2, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = x2b + 1;
            return insn;
        }

        if (x2a == 1 && size_code == 1 && x4 == 6 && x2b <= 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_PSHRADD2, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = x2b + 1;
            return insn;
        }

        if (x2a == 1 &&
            (x4 == 2 || x4 == 3) &&
            (x2b == 2 || (x4 == 2 && x2b == 3)) &&
            ia64_bits(raw, 36, 1) == 0) {
            Ia64Opcode opcode = IA64_OP_ILLEGAL;

            if (x4 == 2) {
                if (ve == 0) {
                    opcode = IA64_OP_PAVG1;
                } else {
                    opcode = IA64_OP_PAVG2;
                }
            } else if (x2b == 2) {
                if (ve == 0) {
                    opcode = IA64_OP_PAVGSUB1;
                } else {
                    opcode = IA64_OP_PAVGSUB2;
                }
            }
            if (opcode != IA64_OP_ILLEGAL) {
                Ia64Instruction insn =
                    ia64_base_insn(opcode, unit, raw, address, slot);
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.imm = x2b == 3;
                return insn;
            }
        }

        if (x2a == 1 && x4 == 9 && x2b <= 1) {
            Ia64Opcode opcode = IA64_OP_ILLEGAL;

            if (size_code == 0) {
                opcode = x2b == 0 ? IA64_OP_PCMP1_EQ : IA64_OP_PCMP1_GT;
            } else if (size_code == 1) {
                opcode = x2b == 0 ? IA64_OP_PCMP2_EQ : IA64_OP_PCMP2_GT;
            } else if (size_code == 2) {
                opcode = x2b == 0 ? IA64_OP_PCMP4_EQ : IA64_OP_PCMP4_GT;
            }
            if (opcode != IA64_OP_ILLEGAL) {
                Ia64Instruction insn =
                    ia64_base_insn(opcode, unit, raw, address, slot);
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                return insn;
            }
        }

        if ((x4 == 4 || x4 == 6) && x2a == 0 && ve == 0) {
            const uint64_t count = x2b;
            Ia64Opcode shladd_op = (x4 == 6) ?
                IA64_OP_SHLADDP4 : IA64_OP_SHLADD;

            /* A2 bit 36 is an unnamed ignored field. */
            Ia64Instruction insn =
                ia64_base_insn(shladd_op, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = count + 1;
            return insn;
        }

        if (x2a == 0 && ve == 0) {
            const Ia64A3AluPattern *pattern = ia64_lookup_a3_alu(x4, x2b);

            if (pattern != NULL) {
                Ia64Instruction insn =
                    ia64_base_insn(pattern->opcode, unit, raw, address, slot);

                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                if (pattern->immediate) {
                    insn.operands.decoder.imm = ia64_imm8(raw);
                } else {
                    /* A1 bit 36 is an unnamed ignored field. */
                    insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                }
                return insn;
            }

            /* Blank entries in SDM Vol. 3 Table 4-9 are purple. */
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        }


        /* Blank entries in SDM Vol. 3 Tables 4-12 through 4-15 are purple. */
        if (x2a == 1) {
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 9) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_ADDL, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 2);
        insn.operands.decoder.imm = ia64_imm22(raw);
        return insn;
    }

    /*
     * A7 compare-to-zero names bits 19:13 as a literal-zero field.  Check it
     * before the individual compare paths so a nonzero value cannot alias an
     * A6 register-register compare or be silently ignored by an A7 decoder.
     */
    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        (ia64_b_op(raw) == 0xc || ia64_b_op(raw) == 0xd ||
         ia64_b_op(raw) == 0xe) &&
        ia64_bits(raw, 36, 1) == 1 &&
        ia64_bits(raw, 34, 2) <= 1 &&
        ia64_bits(raw, 13, 7) != 0) {
        return ia64_constant_zero_violation_insn(
            unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 1 &&
        (ia64_bits(raw, 33, 3) == 1 || ia64_bits(raw, 33, 3) == 3)) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CHK_S, unit, raw, address, slot);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = ia64_chk_disp(raw);
        insn.check_fp = ia64_bits(raw, 33, 3) == 3;
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 3) == 1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CHK_S, unit, raw, address, slot);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = ia64_chk_disp(raw);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_bits(raw, 33, 1) == 0 &&
        ia64_bits(raw, 36, 1) == 0 &&
        (ia64_bits(raw, 34, 2) == 0 || ia64_bits(raw, 34, 2) == 1) &&
        (ia64_b_op(raw) == 0xc || ia64_b_op(raw) == 0xd ||
         ia64_b_op(raw) == 0xe)) {
        const bool cmp4 = ia64_bits(raw, 34, 2) == 1;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        switch (ia64_b_op(raw)) {
        case 0xc:
            opcode = cmp4 ? IA64_OP_CMP4_LT : IA64_OP_CMP_LT;
            break;
        case 0xd:
            opcode = cmp4 ? IA64_OP_CMP4_LTU : IA64_OP_CMP_LTU;
            break;
        case 0xe:
            opcode = cmp4 ? IA64_OP_CMP4_EQ : IA64_OP_CMP_EQ;
            break;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.compare_unc = ia64_bits(raw, 12, 1) != 0;
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 34, 2) == 1 &&
        ia64_bits(raw, 33, 1) == 1 &&
        ia64_b_op(raw) == 0xc) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 12, 1) ?
                           IA64_OP_CMP4_NE_AND : IA64_OP_CMP4_EQ_AND,
                           unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        (ia64_b_op(raw) == 0xc || ia64_b_op(raw) == 0xd ||
         ia64_b_op(raw) == 0xe) &&
        ia64_bits(raw, 36, 1) == 1 &&
        (ia64_bits(raw, 34, 2) == 0 || ia64_bits(raw, 34, 2) == 1)) {
        Ia64Opcode opcode =
            ia64_compare_zero_opcode(ia64_b_op(raw),
                                     ia64_bits(raw, 34, 2) == 1,
                                     ia64_bits(raw, 33, 1),
                                     ia64_bits(raw, 12, 1));
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = 0;
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc &&
        ia64_bits(raw, 33, 1) == 1 &&
        (ia64_bits(raw, 34, 2) == 2 || ia64_bits(raw, 34, 2) == 3)) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const bool cmp4 = x2a == 3;
        const bool ne = ia64_bits(raw, 12, 1) != 0;
        Ia64Opcode opcode = cmp4 ?
            (ne ? IA64_OP_CMP4_NE_AND_IMM : IA64_OP_CMP4_EQ_AND_IMM) :
            (ne ? IA64_OP_CMP_NE_AND_IMM : IA64_OP_CMP_EQ_AND_IMM);
        Ia64Instruction insn = ia64_base_insn(opcode, unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.imm = ia64_imm8(raw);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc &&
        ia64_bits(raw, 33, 1) == 1 &&
        ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 36, 1) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 12, 1) ?
                           IA64_OP_CMP_NE_AND : IA64_OP_CMP_EQ_AND,
                           unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc &&
        ia64_bits(raw, 33, 1) == 1 &&
        ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 36, 1) == 1) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 12, 1) ?
                           IA64_OP_CMP_LT_AND : IA64_OP_CMP_GE_AND,
                           unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xd && ia64_bits(raw, 12, 1) == 0 &&
        ia64_bits(raw, 33, 1) == 0 && ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 36, 1) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CMP_LTU, unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc && ia64_bits(raw, 12, 1) == 0 &&
        ia64_bits(raw, 33, 1) == 0 && ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 36, 1) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CMP_LT, unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc && ia64_bits(raw, 12, 1) == 0) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        if (ia64_bits(raw, 33, 1) == 0 && (x2a == 2 || x2a == 3)) {
            Ia64Instruction insn =
                ia64_base_insn(x2a == 2 ? IA64_OP_CMP_LT_IMM :
                                           IA64_OP_CMP4_LT_IMM,
                               unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc && ia64_bits(raw, 12, 1) == 1 &&
        ia64_bits(raw, 33, 1) == 0) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const uint64_t ve = ia64_bits(raw, 36, 1);
        if (x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_LT, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.compare_unc = true;
            return insn;
        }
        if (x2a == 2 || x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(x2a == 2 ? IA64_OP_CMP_LT_IMM :
                                           IA64_OP_CMP4_LT_IMM,
                               unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.compare_unc = true;
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xe &&
        ia64_bits(raw, 36, 1) == 1) {
        const uint64_t x2 = ia64_bits(raw, 34, 2);
        const uint64_t ta = ia64_bits(raw, 33, 1);
        const uint64_t c = ia64_bits(raw, 12, 1);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x2 == 0) {
            if (ta == 0 && c == 0) {
                opcode = IA64_OP_CMP_GT_OR_ANDCM;
            } else if (ta == 0 && c == 1) {
                opcode = IA64_OP_CMP_LE_OR_ANDCM;
            } else if (ta == 1 && c == 0) {
                opcode = IA64_OP_CMP_GE_OR_ANDCM;
            } else if (ta == 1 && c == 1) {
                opcode = IA64_OP_CMP_LT_OR_ANDCM;
            }
        } else if (x2 == 1) {
            if (ta == 0 && c == 0) {
                opcode = IA64_OP_CMP4_GT_OR_ANDCM;
            } else if (ta == 0 && c == 1) {
                opcode = IA64_OP_CMP4_LE_OR_ANDCM;
            } else if (ta == 1 && c == 0) {
                opcode = IA64_OP_CMP4_GE_OR_ANDCM;
            } else if (ta == 1 && c == 1) {
                opcode = IA64_OP_CMP4_LT_OR_ANDCM;
            }
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xc && ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 33, 1) == 0) {
        const uint64_t x4 = ia64_bits(raw, 29, 4);
        const uint64_t x2b = ia64_bits(raw, 27, 2);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (x4 == 0 && x2b == 0) {
            opcode = IA64_OP_PADD1;
        } else if (x4 == 0 && x2b == 1) {
            opcode = IA64_OP_PADD2;
        } else if (x4 == 0 && x2b == 2) {
            opcode = IA64_OP_PADD4;
        } else if (x4 == 1 && x2b == 0) {
            opcode = IA64_OP_PSUB1;
        } else if (x4 == 1 && x2b == 1) {
            opcode = IA64_OP_PSUB2;
        } else if (x4 == 1 && x2b == 2) {
            opcode = IA64_OP_PSUB4;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 33, 1) == 1 && ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 36, 1) == 1) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if ((x6 & ~1ULL) == 0) {
            opcode = IA64_OP_SHRU;
        } else if ((x6 & ~1ULL) == 4) {
            opcode = IA64_OP_SHR;
        } else if ((x6 & ~1ULL) == 8) {
            opcode = IA64_OP_SHL;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            if (opcode == IA64_OP_SHL) {
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 13, 7);
            } else {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 36, 1) == 1 &&
        ia64_bits(raw, 33, 3) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if ((x6 & ~1ULL) == 0x1a) {
            opcode = IA64_OP_MPY4;
        } else if ((x6 & ~1ULL) == 0x1e) {
            opcode = IA64_OP_MPYSHL4;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 0 &&
        ((ia64_bits(raw, 36, 1) == 0 && ia64_bits(raw, 33, 1) == 1) ||
         (ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 33, 1) == 0)) &&
        (ia64_bits(raw, 27, 6) & ~1ULL) == 0x08) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 36, 1) ?
                           IA64_OP_PSHL4 : IA64_OP_PSHL2,
                           unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.operands.decoder.imm = -1;
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 3 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 1 &&
        ia64_bits(raw, 28, 2) == 1 &&
        ((ia64_bits(raw, 36, 1) == 0 && ia64_bits(raw, 33, 1) == 1) ||
         (ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 33, 1) == 0))) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 36, 1) ?
                           IA64_OP_PSHL4 : IA64_OP_PSHL2,
                           unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = 31 - ia64_bits(raw, 20, 5);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7) {
        const uint64_t pshr_sig = raw & 0x1fff0000000ULL;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        bool fixed_count = false;

        switch (pshr_sig) {
        case 0xe220000000ULL:
            opcode = IA64_OP_PSHR2;
            break;
        case 0xe200000000ULL:
            opcode = IA64_OP_PSHR2_U;
            break;
        case 0xe630000000ULL:
            opcode = IA64_OP_PSHR2;
            fixed_count = true;
            break;
        case 0xe610000000ULL:
            opcode = IA64_OP_PSHR2_U;
            fixed_count = true;
            break;
        case 0xf020000000ULL:
            opcode = IA64_OP_PSHR4;
            break;
        case 0xf000000000ULL:
            opcode = IA64_OP_PSHR4_U;
            break;
        case 0xf430000000ULL:
            opcode = IA64_OP_PSHR4;
            fixed_count = true;
            break;
        case 0xf410000000ULL:
            opcode = IA64_OP_PSHR4_U;
            fixed_count = true;
            break;
        }

        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            if (fixed_count) {
                insn.operands.decoder.imm = ia64_bits(raw, 14, 5);
            } else {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.imm = -1;
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 34, 2) == 3 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 2 &&
        ia64_bits(raw, 28, 2) == 2) {
        bool two_byte_form = ia64_bits(raw, 33, 1) != 0;
        Ia64Instruction insn =
            ia64_base_insn(two_byte_form ? IA64_OP_MUX2 : IA64_OP_MUX1,
                           unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = two_byte_form ? ia64_bits(raw, 20, 8) :
                                   ia64_bits(raw, 20, 4);
        if (!two_byte_form &&
            !(insn.operands.decoder.imm == 0 ||
              (insn.operands.decoder.imm >= 8 &&
               insn.operands.decoder.imm <= 11))) {
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        }
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 33, 3) == 3 &&
        ((ia64_bits(raw, 27, 6) & ~1ULL) == 0x12 ||
         (ia64_bits(raw, 27, 6) & ~1ULL) == 0x1a) &&
        ia64_bits(raw, 13, 7) != 0) {
        /* I9 names bits 19:13 as a literal-zero field. */
        return ia64_constant_zero_violation_insn(
            unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 36, 1) == 0 &&
        (ia64_bits(raw, 27, 6) & ~1ULL) == 0x12 &&
        ia64_bits(raw, 33, 3) == 3 &&
        ia64_bits(raw, 13, 7) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_POPCNT, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 36, 1) == 0 &&
        (ia64_bits(raw, 27, 6) & ~1ULL) == 0x1a &&
        ia64_bits(raw, 33, 3) == 3 &&
        ia64_bits(raw, 13, 7) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CLZ, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 33, 3) == 5) {
        const uint64_t x6 = ia64_bits(raw, 27, 6) & ~1ULL;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x6 == 0x1e) {
            opcode = IA64_OP_PMPY2_L;
        } else if (x6 == 0x1a) {
            opcode = IA64_OP_PMPY2_R;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            /* za=1 selects Table 4-20, where these cells are purple. */
            if (ia64_bits(raw, 36, 1) != 0) {
                return ia64_reserved_if_qp_insn(
                    unit, raw, address, slot);
            }
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 33, 3) == 1) {
        /* I1 bit 27 is an unnamed ignored field. */
        const uint64_t x6 = ia64_bits(raw, 27, 6) & ~1ULL;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        uint64_t shift = 0;

        switch (x6) {
        case 0x06:
            opcode = IA64_OP_PMPYSH2;
            shift = 0;
            break;
        case 0x0e:
            opcode = IA64_OP_PMPYSH2;
            shift = 7;
            break;
        case 0x16:
            opcode = IA64_OP_PMPYSH2;
            shift = 15;
            break;
        case 0x1e:
            opcode = IA64_OP_PMPYSH2;
            shift = 16;
            break;
        case 0x02:
            opcode = IA64_OP_PMPYSH2_U;
            shift = 0;
            break;
        case 0x0a:
            opcode = IA64_OP_PMPYSH2_U;
            shift = 7;
            break;
        case 0x12:
            opcode = IA64_OP_PMPYSH2_U;
            shift = 15;
            break;
        case 0x1a:
            opcode = IA64_OP_PMPYSH2_U;
            shift = 16;
            break;
        }

        if (opcode != IA64_OP_ILLEGAL) {
            /* za=1 selects Table 4-20, where these cells are purple. */
            if (ia64_bits(raw, 36, 1) != 0) {
                return ia64_reserved_if_qp_insn(
                    unit, raw, address, slot);
            }
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = shift;
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ((ia64_bits(raw, 27, 6) & ~1ULL) == 0x10 ||
         (ia64_bits(raw, 27, 6) & ~1ULL) == 0x14)) {
        const uint64_t x6 = ia64_bits(raw, 27, 6) & ~1ULL;
        const uint64_t x3 = ia64_bits(raw, 33, 3);
        const bool right = x6 == 0x10;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x3 == 4 && ia64_bits(raw, 36, 1) == 0) {
            opcode = right ? IA64_OP_MIX1_R : IA64_OP_MIX1_L;
        } else if (x3 == 5 && ia64_bits(raw, 36, 1) == 0) {
            opcode = right ? IA64_OP_MIX2_R : IA64_OP_MIX2_L;
        } else if (x3 == 4 && ia64_bits(raw, 36, 1) == 1) {
            opcode = right ? IA64_OP_MIX4_R : IA64_OP_MIX4_L;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 2 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 0) {
        const uint64_t za = ia64_bits(raw, 36, 1);
        const uint64_t zb = ia64_bits(raw, 33, 1);
        const uint64_t x2b = ia64_bits(raw, 28, 2);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (za == 0 && zb == 1 && x2b == 0) {
            opcode = IA64_OP_PACK2_USS;
        } else if (za == 0 && zb == 1 && x2b == 2) {
            opcode = IA64_OP_PACK2_SSS;
        } else if (za == 1 && zb == 0 && x2b == 2) {
            opcode = IA64_OP_PACK4_SSS;
        } else if (za == 0 && zb == 0 && x2b == 1) {
            opcode = IA64_OP_PMIN1_U;
        } else if (za == 0 && zb == 1 && x2b == 3) {
            opcode = IA64_OP_PMIN2;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 2 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 1) {
        const uint64_t za = ia64_bits(raw, 36, 1);
        const uint64_t zb = ia64_bits(raw, 33, 1);
        const uint64_t x2b = ia64_bits(raw, 28, 2);

        if (za == 0 && zb == 0 && x2b == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_PMAX1_U, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (za == 0 && zb == 1 && x2b == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_PMAX2, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 2 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 2 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 33, 1) == 0 &&
        ia64_bits(raw, 28, 2) == 3) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_PSAD1, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0x7 &&
        ia64_bits(raw, 34, 2) == 2 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 30, 2) == 1) {
        Ia64Opcode opcode =
            ia64_unpack_opcode_from_fields(ia64_bits(raw, 36, 1),
                                           ia64_bits(raw, 33, 1),
                                           ia64_bits(raw, 28, 2));

        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 4) == 0) {
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        switch (ia64_bits(raw, 27, 6)) {
        case 0x18:
            opcode = IA64_OP_CZX1_L;
            break;
        case 0x1c:
            opcode = IA64_OP_CZX1_R;
            break;
        case 0x19:
            opcode = IA64_OP_CZX2_L;
            break;
        case 0x1d:
            opcode = IA64_OP_CZX2_R;
            break;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xd && ia64_bits(raw, 34, 2) == 0 &&
        ia64_bits(raw, 33, 1) == 0) {
        const uint64_t x4 = ia64_bits(raw, 29, 4);
        const uint64_t x2b = ia64_bits(raw, 27, 2);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (x4 == 0 && x2b == 0) {
            opcode = IA64_OP_SXT1;
        } else if (x4 == 0 && x2b == 1) {
            opcode = IA64_OP_SXT2;
        } else if (x4 == 0 && x2b == 2) {
            opcode = IA64_OP_SXT4;
        } else if (x4 == 1 && x2b == 0) {
            opcode = IA64_OP_ZXT1;
        } else if (x4 == 1 && x2b == 1) {
            opcode = IA64_OP_ZXT2;
        } else if (x4 == 1 && x2b == 2) {
            opcode = IA64_OP_ZXT4;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    /* CMP-immediate: M/I-unit, b_op == 0xd, bits-34:33 != 0 */
    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xd &&
        !(ia64_bits(raw, 34, 2) == 0 && ia64_bits(raw, 33, 1) == 0)) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const uint64_t ve = ia64_bits(raw, 36, 1);
        const uint64_t x = ia64_bits(raw, 33, 1);
        const bool compare_unc = ia64_bits(raw, 12, 1) != 0;
        Ia64Opcode opcode = ia64_compare_opcode_from_cmp(x2a, ve);

        if (x == 1 && (x2a == 2 || x2a == 3)) {
            const bool cmp4 = x2a == 3;
            const bool ne = ia64_bits(raw, 12, 1) != 0;
            opcode = cmp4 ?
                (ne ? IA64_OP_CMP4_NE_OR_IMM : IA64_OP_CMP4_EQ_OR_IMM) :
                (ne ? IA64_OP_CMP_NE_OR_IMM : IA64_OP_CMP_EQ_OR_IMM);
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (ve == 1 && (x2a == 0 || x2a == 1)) {
            const uint64_t c = ia64_bits(raw, 12, 1);
            if (x2a == 0) {
                if (x == 0 && c == 0) {
                    opcode = IA64_OP_CMP_GT_OR;
                } else if (x == 0 && c == 1) {
                    opcode = IA64_OP_CMP_LE_OR;
                } else if (x == 1 && c == 0) {
                    opcode = IA64_OP_CMP_GE_OR;
                } else {
                    opcode = IA64_OP_CMP_LT_OR;
                }
            } else {
                if (x == 0 && c == 0) {
                    opcode = IA64_OP_CMP4_GT_OR;
                } else if (x == 0 && c == 1) {
                    opcode = IA64_OP_CMP4_LE_OR;
                } else if (x == 1 && c == 0) {
                    opcode = IA64_OP_CMP4_GE_OR;
                } else {
                    opcode = IA64_OP_CMP4_LT_OR;
                }
            }
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (x == 1 && x2a == 0 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(compare_unc ? IA64_OP_CMP_NE_OR :
                                             IA64_OP_CMP_EQ_OR,
                               unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (x == 1 && x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(compare_unc ? IA64_OP_CMP4_NE_OR :
                                             IA64_OP_CMP4_EQ_OR,
                               unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (opcode != IA64_OP_ILLEGAL && x == 0 && x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_LTU, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (x == 0 && x2a == 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_LTU_IMM, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.compare_unc = compare_unc;
            return insn;
        }
        if (x == 0 && x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_LTU_IMM, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.compare_unc = compare_unc;
            return insn;
        }
        if (opcode != IA64_OP_ILLEGAL && x == 0) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xe && ia64_bits(raw, 12, 1) == 0) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const uint64_t ve = ia64_bits(raw, 36, 1);
        const uint64_t x = ia64_bits(raw, 33, 1);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x == 0 && x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_EQ, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (x == 0 && x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_EQ_IMM, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }

        if (x == 1 && x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_EQ_OR_ANDCM_IMM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }

        if (x == 1 && x2a == 0 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_EQ_OR_ANDCM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (x == 1 && x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_EQ_OR_ANDCM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (x == 1 && x2a == 1 && ve == 1 &&
            ia64_bits(raw, 13, 7) == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_GE_OR_ANDCM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        if (x == 1 && x2a == 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_EQ_OR_ANDCM_IMM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }

        if (x == 0 && x2a == 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_EQ_IMM, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }
        if (x == 0 && x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_EQ_IMM, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }

        if (x == 0) {
            opcode = ia64_compare_opcode_from_cmp(x2a, ve);
            if (opcode == IA64_OP_ILLEGAL && x2a == 3 && ve == 1) {
                opcode = IA64_OP_CMP_NE;
            }
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 33, 1) == 0 && ia64_bits(raw, 34, 2) == 3) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_SHRP_IMM, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.operands.decoder.imm = ia64_bits(raw, 27, 6);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 33, 1) == 1 && ia64_bits(raw, 34, 2) == 1 &&
        ia64_bits(raw, 20, 7) >= 64) {
        const uint64_t imm8 = ia64_bits(raw, 13, 7) |
                              (ia64_bits(raw, 36, 1) << 7);
        const uint64_t len = ia64_bits(raw, 27, 6) + 1;
        const uint64_t pos = 127 - ia64_bits(raw, 20, 7);
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_DEPZ_IMM, unit, raw, address, slot);

        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.imm = pos | (len << 6) | (imm8 << 13);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 33, 1) == 1 && ia64_bits(raw, 34, 2) == 1) {
        const uint64_t len = ia64_bits(raw, 27, 6) + 1;
        const uint64_t pos = 63 - ia64_bits(raw, 20, 7);
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_DEPZ, unit, raw, address, slot);

        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = pos | (len << 6);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 4) {
        const uint64_t len = (ia64_bits(raw, 27, 4) + 1) & 0x3f;
        const uint64_t cpos = ia64_bits(raw, 31, 2) |
                              (ia64_bits(raw, 33, 1) << 2) |
                              (ia64_bits(raw, 34, 2) << 3) |
                              (ia64_bits(raw, 36, 1) << 5);
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_DEP, unit, raw, address, slot);

        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.operands.decoder.imm = (63 - cpos) | (len << 6);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 33, 1) == 1 && ia64_bits(raw, 34, 2) == 3) {
        const uint64_t len = ia64_bits(raw, 27, 6) + 1;
        const uint64_t pos = 63 - (ia64_bits(raw, 13, 7) >> 1);
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_DEP_IMM, unit, raw, address, slot);

        /* I14 names bit 13 as a literal zero, not part of cpos6b. */
        if (ia64_bits(raw, 13, 1) != 0) {
            return ia64_constant_zero_violation_insn(
                unit, raw, address, slot);
        }
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.operands.decoder.imm =
            pos | (len << 6) | (ia64_bits(raw, 36, 1) << 13);
        return insn;
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 34, 2) == 1) {
        const uint64_t x3 = ia64_bits(raw, 33, 3);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (x3 == 2) {
            uint64_t pos_sign = ia64_bits(raw, 13, 7);
            Ia64Instruction insn = ia64_base_insn(
                (pos_sign & 1) ? IA64_OP_EXTR : IA64_OP_EXTRU,
                unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = (pos_sign >> 1) |
                       ((ia64_bits(raw, 27, 6) + 1) << 6);
            return insn;
        } else if (x3 == 3) {
            opcode = IA64_OP_SHL_IMM;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            if (opcode == IA64_OP_SHL_IMM) {
                insn.operands.decoder.r3 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.imm = 63 - ia64_bits(raw, 20, 6);
            } else {
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.imm = 63 - ia64_bits(raw, 27, 6);
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_I && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 1) == 0 && ia64_bits(raw, 34, 2) == 1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_MOV_PR_ROT_IMM, unit, raw, address, slot);
        insn.operands.decoder.imm = ia64_pr_rot_imm(raw);
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 1) == 0 && ia64_bits(raw, 34, 2) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);

        if ((x6 == 0x0a || x6 == 0x0c) &&
            ia64_bits(raw, 0, 6) != 0) {
            Ia64Instruction insn;

            /* M25 names bits 5:0 as a literal-zero field, not as qp. */
            insn = ia64_constant_zero_violation_insn(
                unit, raw, address, slot);
            insn.qp = 0;
            return insn;
        }
        if (x6 == 0x0a) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_LOADRS, unit, raw, address, slot);

            insn.qp = 0;
            return insn;
        } else if (x6 == 0x0c) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_FLUSHRS, unit, raw, address, slot);

            insn.qp = 0;
            return insn;
        } else if (x6 == 0x10) {
            return ia64_base_insn(IA64_OP_INVALA, unit, raw, address, slot);
        } else if (x6 == 0x12 || x6 == 0x13) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_INVALAT, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.check_fp = x6 == 0x13;
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0) {
        const uint64_t x3 = ia64_bits(raw, 33, 3);
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x31) {
            opcode = IA64_OP_MOV_BRGR;  /* mov r=b (BR to GR) */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x33) {
            opcode = IA64_OP_MOV_PRGR;  /* mov r=pr (PR to GR) */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x30) {
            opcode = IA64_OP_MOV_CURRENT_IP;
        } else if (unit == IA64_UNIT_I && x3 == 3) {
            opcode = IA64_OP_MOV_GRPR;  /* mov pr=r (GR to PR) */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x32) {
            opcode = IA64_OP_MOV_ARGR;  /* mov r=ar (AR to GR) */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x2a) {
            opcode = IA64_OP_MOV_GRAR;  /* mov ar=r (GR to AR) */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x0a) {
            opcode = IA64_OP_MOV_IMMAR; /* mov ar=imm */
        } else if (unit == IA64_UNIT_M && x3 == 0 && x6 == 0x28) {
            opcode = IA64_OP_MOV_IMMAR; /* mov.m ar=imm */
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x10) {
            opcode = IA64_OP_ZXT1;
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x11) {
            opcode = IA64_OP_ZXT2;
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x12) {
            opcode = IA64_OP_ZXT4;
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x14) {
            opcode = IA64_OP_SXT1;
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x15) {
            opcode = IA64_OP_SXT2;
        } else if (unit == IA64_UNIT_I && x3 == 0 && x6 == 0x16) {
            opcode = IA64_OP_SXT4;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            if (opcode == IA64_OP_SXT1 || opcode == IA64_OP_SXT2 ||
                opcode == IA64_OP_SXT4 || opcode == IA64_OP_ZXT1 ||
                opcode == IA64_OP_ZXT2 || opcode == IA64_OP_ZXT4) {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            } else if (opcode == IA64_OP_MOV_CURRENT_IP) {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            } else if (opcode == IA64_OP_MOV_GRPR) {
                insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.imm = ia64_pr_mask(raw);
            } else if (opcode == IA64_OP_MOV_GRAR) {
                insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            } else if (opcode == IA64_OP_MOV_IMMAR) {
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.imm = ia64_imm8(raw);
            } else if (opcode == IA64_OP_MOV_ARGR) {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            } else {
                /*
                 * BR/PR-to-GR encodes the GR destination in r1 and the
                 * BR/PR source index in r2.  The system operand view gives
                 * those fields their family-level names.
                 */
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 3);
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 1 && ia64_bits(raw, 33, 3) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x6 == 0x22) {
            opcode = IA64_OP_MOV_ARGR;  /* mov.m r=ar */
        } else if (x6 == 0x2a) {
            opcode = IA64_OP_MOV_GRAR;  /* mov.m ar=r */
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);

            if (opcode == IA64_OP_MOV_ARGR) {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            } else {
                insn.operands.decoder.r1 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            }
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 0xe && ia64_bits(raw, 12, 1) == 1) {
        const uint64_t x2a = ia64_bits(raw, 34, 2);
        const uint64_t ve = ia64_bits(raw, 36, 1);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (ia64_bits(raw, 33, 1) == 0 && (x2a == 2 || x2a == 3)) {
            Ia64Instruction insn =
                ia64_base_insn(x2a == 2 ? IA64_OP_CMP_EQ_IMM :
                                           IA64_OP_CMP4_EQ_IMM,
                               unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            insn.compare_unc = true;
            return insn;
        }
        if (ia64_bits(raw, 33, 1) == 1 && x2a == 2) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_NE_OR_ANDCM_IMM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }
        if (ia64_bits(raw, 33, 1) == 1 && x2a == 0 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP_NE_OR_ANDCM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (ia64_bits(raw, 33, 1) == 1 && x2a == 1 && ve == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_NE_OR_ANDCM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (ia64_bits(raw, 33, 1) == 1 && x2a == 3) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_CMP4_NE_OR_ANDCM_IMM, unit, raw,
                               address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_imm8(raw);
            return insn;
        }
        if (ia64_bits(raw, 33, 1) == 0) {
            switch ((x2a << 1) | ve) {
            case 0:
                opcode = IA64_OP_CMP4_EQ;
                break;
            case 1:
                opcode = IA64_OP_CMP4_LT;
                break;
            case 2:
                opcode = IA64_OP_CMP4_LE;
                break;
            case 3:
                opcode = IA64_OP_CMP4_GT;
                break;
            case 4:
                opcode = IA64_OP_CMP4_GE;
                break;
            case 5:
                opcode = IA64_OP_CMP4_LTU;
                break;
            case 6:
                opcode = IA64_OP_CMP4_LEU;
                break;
            case 7:
                opcode = IA64_OP_CMP4_GTU;
                break;
            case 8:
                opcode = IA64_OP_CMP4_GEU;
                break;
            }
        } else {
            switch ((x2a << 1) | ve) {
            case 0:
                opcode = IA64_OP_TBIT_Z;
                break;
            case 1:
                opcode = IA64_OP_TBIT_NZ;
                break;
            case 2:
                opcode = IA64_OP_TNAT_Z;
                break;
            case 3:
                opcode = IA64_OP_TNAT_NZ;
                break;
            }
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_I &&
        ia64_b_op(raw) == 5 &&
        ia64_bits(raw, 34, 2) == 0) {
        const bool bit13 = ia64_bits(raw, 13, 1);
        const bool bit19 = ia64_bits(raw, 19, 1);
        const bool bit33 = ia64_bits(raw, 33, 1);
        const bool bit36 = ia64_bits(raw, 36, 1);
        const bool nz_form = ia64_bits(raw, 12, 1);
        const bool is_tf = bit13 && bit19;
        const bool is_tnat = bit13 && !bit19;
        const bool is_tbit = !bit13;

        /* I30 names r3 (bits 26:20) as a literal-zero field. */
        if (is_tf && ia64_bits(raw, 20, 7) != 0) {
            return ia64_constant_zero_violation_insn(
                unit, raw, address, slot);
        }

        if (is_tbit || is_tnat || is_tf) {
            Ia64Instruction insn =
                ia64_base_insn(is_tf ?
                               (nz_form && (bit33 || bit36) ?
                                IA64_OP_TF_NZ : IA64_OP_TF_Z) :
                               is_tnat ?
                               (nz_form && (bit33 || bit36) ?
                                IA64_OP_TNAT_NZ : IA64_OP_TNAT_Z) :
                               (nz_form && (bit33 || bit36) ?
                                IA64_OP_TBIT_NZ : IA64_OP_TBIT_Z),
                               unit, raw, address, slot);

            insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            if (is_tbit) {
                insn.operands.decoder.imm = ia64_bits(raw, 14, 6);
            } else if (is_tf) {
                insn.operands.decoder.imm = 32 + ia64_bits(raw, 14, 5);
            }
            if (!bit33 && !bit36) {
                insn.compare_unc = nz_form;
            } else if (!bit33 && bit36) {
                insn.pred_update = IA64_PRED_UPDATE_AND;
            } else if (bit33 && !bit36) {
                insn.pred_update = IA64_PRED_UPDATE_OR;
            } else {
                insn.pred_update = IA64_PRED_UPDATE_OR_ANDCM;
            }
            return insn;
        }
    }

    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_bits(raw, 12, 1) == 0 &&
        ia64_bits(raw, 33, 1) == 0 &&
        ia64_bits(raw, 34, 2) == 1 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_b_op(raw) == 0xc) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CMP4_LT, unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 && ia64_bits(raw, 27, 1) == 0 &&
        ia64_bits(raw, 28, 2) == 0) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode = ia64_memory_opcode_from_x6a(x6a);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.mem_acquire = ia64_memory_x6a_is_acquire_load(x6a);
            insn.mem_release = ia64_opcode_is_release_store(opcode);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 1 &&
        (ia64_bits(raw, 30, 6) == 0x28 ||
         ia64_bits(raw, 30, 6) == 0x2c)) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_LD16, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.mem_acquire = ia64_bits(raw, 30, 6) == 0x2c;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 33, 3) == 6 &&
        (ia64_bits(raw, 27, 6) & ~0x26ULL) == 0x01) {
        uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_ST16, unit, raw, address, slot);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.mem_release = (x6 & 0x20) != 0;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 5) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode = ia64_memory_opcode_from_x6a(x6a);
        if (ia64_opcode_is_load(opcode)) {
            /* Cache hint bits do not affect the architectural load result. */
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            uint64_t imm9 = ia64_bits(raw, 13, 7) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            (ia64_bits(raw, 36, 1) << 8);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            insn.mem_acquire = ia64_memory_x6a_is_acquire_load(x6a);
            return insn;
        }
        if (ia64_opcode_is_store(opcode)) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            uint64_t imm9 = ia64_bits(raw, 6, 7) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            (ia64_bits(raw, 36, 1) << 8);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            insn.mem_release = ia64_opcode_is_release_store(opcode);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 1 &&
        (ia64_bits(raw, 30, 6) == 0x20 ||
         ia64_bits(raw, 30, 6) == 0x24)) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_CMP8XCHG16, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.mem_acquire = ia64_bits(raw, 32, 1) == 0;
        insn.mem_release = ia64_bits(raw, 32, 1) != 0;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 1 &&
        ia64_bits(raw, 33, 3) == 0) {
        const uint64_t size = ia64_bits(raw, 30, 1) |
                              (ia64_bits(raw, 31, 1) << 1);
        const Ia64Opcode opcode = ia64_cmpxchg_acqrel_opcode_from_size(size);
        Ia64Instruction insn =
            ia64_base_insn(opcode, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.mem_acquire = ia64_bits(raw, 32, 1) == 0;
        insn.mem_release = ia64_bits(raw, 32, 1) != 0;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 1 &&
        ia64_bits(raw, 32, 1) == 0 &&
        ia64_bits(raw, 33, 3) == 1) {
        const uint64_t size = ia64_bits(raw, 30, 1) |
                              (ia64_bits(raw, 31, 1) << 1);
        const Ia64Opcode opcode = ia64_xchg_opcode_from_size(size);
        Ia64Instruction insn =
            ia64_base_insn(opcode, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        insn.mem_acquire = true;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 2) == 1) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode =
            ia64_speculative_load_opcode_from_x6a(x6a);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 0) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode = ia64_memory_opcode_from_x6a(x6a);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.mem_acquire = ia64_memory_x6a_is_acquire_load(x6a);
            insn.mem_release = ia64_opcode_is_release_store(opcode);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 && ia64_bits(raw, 27, 2) == 3) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const bool is_nc = ia64_bits(raw, 29, 1);
        const Ia64Opcode opcode =
            ia64_check_load_opcode_from_x6a(x6a, !is_nc);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 27, 1) == 0) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode = ia64_memory_opcode_from_x6a(x6a);
        if (ia64_opcode_is_load(opcode)) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.reg_base_update = true;
            insn.mem_acquire = ia64_memory_x6a_is_acquire_load(x6a);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 27, 1) == 1) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        const Ia64Opcode opcode = ia64_fetchadd_opcode_from_x6a(x6a);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm =
                ia64_fetchadd_imm(ia64_bits(raw, 13, 3));
            insn.mem_acquire = ia64_fetchadd_x6a_is_acquire(x6a);
            insn.mem_release = ia64_fetchadd_x6a_is_release(x6a);
            return insn;
        }
    }

    if (unit == IA64_UNIT_M &&
        (ia64_b_op(raw) == 2 || ia64_b_op(raw) == 3)) {
        /* M/A major opcodes 2 and 3 are purple in Table 4-3. */
        return ia64_reserved_if_qp_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_F) {
        if (ia64_is_f_nop(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_NOP, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
        if (ia64_is_f_break(raw)) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_BREAK, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }
        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 1) == 0 &&
            ia64_bits(raw, 27, 6) == 0x10 &&
            ia64_bits(raw, 13, 7) == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_FPABS, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
            return insn;
        }
        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 1) == 0 &&
            ia64_bits(raw, 27, 6) == 0x11) {
            const uint64_t f2 = ia64_bits(raw, 13, 7);
            const uint64_t f3 = ia64_bits(raw, 20, 7);

            if (f2 == 0 || f2 == f3) {
                Ia64Instruction insn =
                    ia64_base_insn(f2 == 0 ? IA64_OP_FPNEGABS :
                                             IA64_OP_FPNEG,
                                   unit, raw, address, slot);
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = f3;
                return insn;
            }
        }
        if ((ia64_b_op(raw) == 0 || ia64_b_op(raw) == 1) &&
            ia64_bits(raw, 36, 1) == 1 &&
            ia64_bits(raw, 33, 1) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(ia64_b_op(raw) == 0 ? IA64_OP_FRSQRTA :
                                                     IA64_OP_FPRSQRTA,
                               unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.clear_p2_before_predicate = true;
            return insn;
        }
        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 36, 1) == 0 &&
            ia64_bits(raw, 33, 1) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_FPRCPA, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
            insn.clear_p2_before_predicate = true;
            return insn;
        }
        if (ia64_b_op(raw) == 1 &&
            ia64_bits(raw, 33, 1) == 0) {
            uint64_t form = ia64_bits(raw, 27, 6);
            Ia64Opcode opcode = IA64_OP_ILLEGAL;

            if (form == 0x10 || form == 0x11 || form == 0x12) {
                opcode = form == 0x10 ? IA64_OP_FPMERGE_S :
                         form == 0x11 ? IA64_OP_FPMERGE :
                                          IA64_OP_FPMERGE_SE;
            } else if (form >= 0x14 && form <= 0x17) {
                opcode = form == 0x14 ? IA64_OP_FPMIN :
                         form == 0x15 ? IA64_OP_FPMAX :
                         form == 0x16 ? IA64_OP_FPAMIN :
                                         IA64_OP_FPAMAX;
            } else if (form >= 0x18 && form <= 0x1b) {
                opcode = IA64_OP_FPCVT;
            } else if (form >= 0x30 && form <= 0x37) {
                opcode = IA64_OP_FPCMP;
            }

            if (opcode != IA64_OP_ILLEGAL) {
                Ia64Instruction insn =
                    ia64_base_insn(opcode, unit, raw, address, slot);
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.p1 = ia64_bits(raw, 34, 2);
                insn.operands.decoder.imm = form & 7;
                return insn;
            }
        }
        if (ia64_b_op(raw) == 1) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_HINT_F, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_immu21(raw);
            return insn;
        }

        const uint64_t x = ia64_bits(raw, 36, 1);
        const uint64_t form = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        /*
         * In the F1 forms selected by major opcodes 8-D, bits 33:27
         * are the complete seven-bit f4 operand, not an x6 extension.
         */
        if (ia64_b_op(raw) == 4) {
            opcode = IA64_OP_FCMP;
        } else if (x == 0 && ia64_b_op(raw) >= 8 &&
                   ia64_b_op(raw) <= 0xd) {
            opcode = ia64_f1_opcode_from_major(ia64_b_op(raw),
                                               ia64_bits(raw, 13, 7),
                                               ia64_bits(raw, 27, 7));
        } else if (x == 1 &&
                   (ia64_b_op(raw) == 0x8 ||
                    ia64_b_op(raw) == 0xa ||
                    ia64_b_op(raw) == 0xc)) {
            opcode = ia64_f1_opcode_from_major(ia64_b_op(raw),
                                               ia64_bits(raw, 13, 7),
                                               ia64_bits(raw, 27, 7));
        } else if (x == 1 &&
                   (ia64_b_op(raw) == 0x9 ||
                    ia64_b_op(raw) == 0xb ||
                    ia64_b_op(raw) == 0xd)) {
            opcode = ia64_b_op(raw) == 0x9 ? IA64_OP_FPMA :
                     ia64_b_op(raw) == 0xb ? IA64_OP_FPMS :
                                             IA64_OP_FPNMA;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   form >= 0x14 && form <= 0x17) {
            opcode = form == 0x14 ? IA64_OP_FMIN :
                     form == 0x15 ? IA64_OP_FMAX :
                     form == 0x16 ? IA64_OP_FAMIN :
                                    IA64_OP_FAMAX;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 && form == 0x1c) {
            opcode = IA64_OP_FCVT_XF;
        } else if (ia64_b_op(raw) == 0 && x == 0 &&
                   ia64_bits(raw, 33, 1) == 1) {
            opcode = IA64_OP_FRCPA;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   ia64_bits(raw, 27, 6) == 0x28) {
            opcode = IA64_OP_FPACK;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   form >= 0x18 && form <= 0x1b) {
            opcode = (form & 1) ? IA64_OP_FCVT_FXU : IA64_OP_FCVT_FX;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   (ia64_bits(raw, 27, 6) == 0x10 ||
                    ia64_bits(raw, 27, 6) == 0x11 ||
                    ia64_bits(raw, 27, 6) == 0x12)) {
            opcode = form == 0x10 ? IA64_OP_FMERGE_S :
                     form == 0x11 ? IA64_OP_FMERGE :
                                      IA64_OP_FMERGE_SE;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   (ia64_bits(raw, 27, 6) == 0x04 ||
                    ia64_bits(raw, 27, 6) == 0x05 ||
                    ia64_bits(raw, 27, 6) == 0x08)) {
            opcode = form == 0x04 ? IA64_OP_FSETC :
                     form == 0x05 ? IA64_OP_FCLRF :
                                     IA64_OP_FCHKF;
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0 &&
                   ((form >= 0x2c && form <= 0x2f) ||
                    (form >= 0x34 && form <= 0x36) ||
                    (form >= 0x39 && form <= 0x3d))) {
            switch (ia64_bits(raw, 27, 6)) {
            case 0x2c:
                opcode = IA64_OP_FAND;
                break;
            case 0x2d:
                opcode = IA64_OP_FANDCM;
                break;
            case 0x2e:
                opcode = IA64_OP_FOR;
                break;
            case 0x2f:
                opcode = IA64_OP_FXOR;
                break;
            case 0x34:
                opcode = IA64_OP_FSWAP;
                break;
            case 0x35:
                opcode = IA64_OP_FSWAP_NL;
                break;
            case 0x36:
                opcode = IA64_OP_FSWAP_NR;
                break;
            case 0x39:
                opcode = IA64_OP_FMIX_LR;
                break;
            case 0x3a:
                opcode = IA64_OP_FMIX_R;
                break;
            case 0x3b:
                opcode = IA64_OP_FMIX_L;
                break;
            case 0x3c:
                opcode = IA64_OP_FSXT_R;
                break;
            case 0x3d:
                opcode = IA64_OP_FSXT_L;
                break;
            }
        } else if (ia64_b_op(raw) == 0 &&
                   ia64_bits(raw, 33, 1) == 0) {
            /* Blank Table 4-60 cells are Reserved if PR[qp] is 1. */
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        } else if (ia64_b_op(raw) == 0xe && x == 0) {
            opcode = IA64_OP_FSELECT;
        } else if (ia64_b_op(raw) == 0xe && x == 1 &&
                   ia64_bits(raw, 34, 2) == 0) {
            opcode = IA64_OP_XMA_L;
        } else if (ia64_b_op(raw) == 0xe && x == 1 &&
                   ia64_bits(raw, 34, 2) == 3) {
            opcode = IA64_OP_XMA_H;
        } else if (ia64_b_op(raw) == 0xe && x == 1 &&
                   ia64_bits(raw, 34, 2) == 2) {
            opcode = ia64_bits(raw, 13, 7) == 0 ?
                IA64_OP_XMPY_HU : IA64_OP_XMA_HU;
        }

        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.p1 = (opcode == IA64_OP_FMA ||
                       opcode == IA64_OP_FMS ||
                       opcode == IA64_OP_FNMA ||
                       opcode == IA64_OP_XMA_L ||
                       opcode == IA64_OP_XMA_H ||
                       opcode == IA64_OP_XMA_HU ||
                       opcode == IA64_OP_XMPY_HU) ?
                ia64_bits(raw, 27, 7) : ia64_bits(raw, 27, 6);
            if (opcode == IA64_OP_FCMP) {
                insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
                insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
                insn.operands.decoder.imm = (ia64_bits(raw, 33, 1) << 1) |
                           ia64_bits(raw, 36, 1);
                insn.compare_unc = ia64_bits(raw, 12, 1) != 0;
            } else if (opcode == IA64_OP_FSETC) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.p1 = ia64_bits(raw, 34, 2);
            } else if (opcode == IA64_OP_FCLRF) {
                insn.operands.decoder.p1 = ia64_bits(raw, 34, 2);
            } else if (opcode == IA64_OP_FCHKF) {
                insn.operands.decoder.p1 = ia64_bits(raw, 34, 2);
                uint64_t imm20b = ia64_bits(raw, 6, 20);
                uint64_t sign = ia64_bits(raw, 36, 1);
                insn.operands.decoder.imm =
                    ia64_sign_extend((sign << 20) | imm20b, 21) * 16;
            }
            if (opcode == IA64_OP_FMA ||
                opcode == IA64_OP_FMS ||
                opcode == IA64_OP_FNMA) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.p1 = ia64_bits(raw, 27, 7);
            } else if (opcode == IA64_OP_FPMA ||
                       opcode == IA64_OP_FPMS ||
                       opcode == IA64_OP_FPNMA) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.p1 = ia64_bits(raw, 27, 7);
            } else if (opcode == IA64_OP_FSELECT) {
                insn.operands.decoder.p1 = ia64_bits(raw, 27, 7);
            } else if (opcode == IA64_OP_FMPY) {
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 27, 7);
            } else if (opcode == IA64_OP_FSUB) {
                insn.operands.decoder.r2 = ia64_bits(raw, 20, 7);
                insn.operands.decoder.r3 = ia64_bits(raw, 13, 7);
            } else if (opcode == IA64_OP_FCVT_FX ||
                       opcode == IA64_OP_FCVT_FXU) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.operands.decoder.imm = form & 3;
            } else if (opcode == IA64_OP_FRCPA) {
                insn.operands.decoder.p2 = insn.operands.decoder.p1;
                insn.clear_p2_before_predicate = true;
            }
            if (ia64_b_op(raw) >= 8 && ia64_b_op(raw) <= 0xd) {
                insn.operands.decoder.fp_precision = (ia64_b_op(raw) & 1) ? 2 :
                                    ia64_bits(raw, 36, 1) ? 1 : 0;
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 6 &&
        ia64_bits(raw, 27, 1) == 0) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        Ia64Opcode opcode = ia64_fp_load_opcode_from_x6a(x6a);
        bool is_store = opcode == IA64_OP_ILLEGAL;

        if (is_store) {
            opcode = ia64_fp_store_opcode_from_x6a(x6a);
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            ia64_fp_load_attrs_from_x6a(&insn, x6a);
            if (is_store) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            } else {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                if (ia64_bits(raw, 36, 1) == 1) {
                    insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                    insn.reg_base_update = true;
                }
            }
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    /* M52: counted line prefetch added by architectural update A-946. */
    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 6 &&
        ia64_bits(raw, 36, 1) == 0 &&
        ia64_bits(raw, 30, 6) == 0x2c &&
        ia64_bits(raw, 27, 1) == 0 &&
        ia64_bits(raw, 19, 1) == 1) {
        Ia64Instruction insn;

        if (ia64_bits(raw, 13, 1) != 0) {
            return ia64_constant_zero_violation_insn(
                unit, raw, address, slot);
        }
        insn = ia64_base_insn(IA64_OP_LFETCH_COUNT, unit, raw,
                              address, slot);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 6) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        if (x6a >= 0x2c && x6a <= 0x2f &&
            ia64_bits(raw, 27, 1) == 0 &&
            (ia64_bits(raw, 36, 1) != 0 || x6a != 0x2c ||
             ia64_bits(raw, 19, 1) == 0)) {
            Ia64Instruction insn =
                ia64_base_insn(x6a >= 0x2e ? IA64_OP_LFETCH_FAULT :
                                              IA64_OP_LFETCH,
                               unit, raw, address, slot);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            if (ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 27, 1) == 0) {
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
                insn.reg_base_update = true;
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 7) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        if (x6a >= 0x2c && x6a <= 0x2f) {
            uint64_t imm9 = (ia64_bits(raw, 36, 1) << 8) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            ia64_bits(raw, 13, 7);
            Ia64Instruction insn =
                ia64_base_insn(x6a >= 0x2e ? IA64_OP_LFETCH_FAULT :
                                              IA64_OP_LFETCH,
                               unit, raw, address, slot);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 7) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        Ia64Opcode opcode = ia64_fp_load_opcode_from_x6a(x6a);

        if (opcode != IA64_OP_ILLEGAL) {
            uint64_t imm9 = ia64_bits(raw, 13, 7) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            (ia64_bits(raw, 36, 1) << 8);
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            ia64_fp_load_attrs_from_x6a(&insn, x6a);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 7) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        Ia64Opcode opcode = ia64_fp_store_opcode_from_x6a(x6a);

        if (opcode != IA64_OP_ILLEGAL) {
            uint64_t imm9 = ia64_bits(raw, 6, 7) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            (ia64_bits(raw, 36, 1) << 8);
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 6 &&
        ia64_bits(raw, 27, 1) == 1) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        Ia64Opcode opcode = ia64_fp_load_pair_opcode_from_x6a(x6a);
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            ia64_fp_load_attrs_from_x6a(&insn, x6a);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            if (ia64_bits(raw, 36, 1) == 1) {
                insn.operands.decoder.imm = opcode == IA64_OP_LDFPS ? 8 : 16;
                insn.imm_base_update = true;
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 7 &&
        ia64_bits(raw, 28, 2) == 0) {
        const uint64_t x6a = ia64_bits(raw, 30, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (x6a == 0x02) {
            opcode = IA64_OP_STFD;
        } else if (x6a == 0x03) {
            opcode = IA64_OP_STFS;
        } else if (x6a == 0x3b) {
            opcode = IA64_OP_STF_SPILL;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            uint64_t imm9 = ia64_bits(raw, 6, 7) |
                            (ia64_bits(raw, 27, 1) << 7) |
                            (ia64_bits(raw, 36, 1) << 8);
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);
            insn.imm_base_update = true;
            return insn;
        }
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x4 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xe1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_GETF_SIG, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x4 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xe9) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_GETF_EXP, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x4 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xf1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_GETF_S, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x4 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xf9) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_GETF_D, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x6 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xe1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_SETF_SIG, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x6 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xe9) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_SETF_EXP, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x6 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xf1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_SETF_S, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    if (unit == IA64_UNIT_M &&
        ia64_b_op(raw) == 0x6 &&
        (ia64_bits(raw, 27, 9) & ~0x06ULL) == 0xf9) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_SETF_D, unit, raw, address, slot);
        insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        return insn;
    }

    /* Blank cells in FP load/store Tables 4-34 through 4-38 are purple. */
    if (unit == IA64_UNIT_M &&
        (ia64_b_op(raw) == 6 || ia64_b_op(raw) == 7)) {
        return ia64_reserved_if_qp_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 3) >= 4) {
        const uint64_t x3 = ia64_bits(raw, 33, 3);
        Ia64Instruction insn =
            ia64_base_insn((x3 == 4 || x3 == 6) ?
                           IA64_OP_CHK_A : IA64_OP_CHK_A_CLR,
                           unit, raw, address, slot);

        insn.operands.decoder.r2 = ia64_bits(raw, 6, 7);
        insn.operands.decoder.imm = ia64_chk_a_disp(raw);
        insn.check_fp = x3 >= 6;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 1 &&
        ia64_bits(raw, 33, 3) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        if (x6 == 0x18) {
            opcode = IA64_OP_PROBE_R;
        } else if (x6 == 0x19) {
            opcode = IA64_OP_PROBE_W;
        } else if (x6 == 0x31) {
            opcode = IA64_OP_PROBE_RW;
        } else if (x6 == 0x32) {
            opcode = IA64_OP_PROBE_R;
        } else if (x6 == 0x33) {
            opcode = IA64_OP_PROBE_W;
        } else if (x6 == 0x38) {
            opcode = IA64_OP_PROBE_R;
        } else if (x6 == 0x39) {
            opcode = IA64_OP_PROBE_W;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            if (x6 == 0x18 || x6 == 0x19) {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.imm = ia64_bits(raw, 13, 2);
                insn.probe_imm = true;
            } else if (x6 == 0x31 || x6 == 0x32 || x6 == 0x33) {
                insn.operands.decoder.imm = ia64_bits(raw, 13, 2);
                insn.probe_fault = true;
                insn.probe_imm = true;
            } else {
                insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
                insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 1 &&
        ia64_bits(raw, 33, 3) == 0 &&
        ia64_bits(raw, 27, 6) == 0x30) {
        Ia64Instruction insn =
            ia64_base_insn(ia64_bits(raw, 36, 1) ?
                           IA64_OP_FC_I : IA64_OP_FC,
                           unit, raw, address, slot);
        insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) >= 5 &&
        ia64_bits(raw, 0, 6) != 0) {
        Ia64Instruction insn;

        /* B2 counted branches have a literal-zero field in bits 5:0. */
        insn = ia64_constant_zero_violation_insn(unit, raw, address, slot);
        insn.qp = 0;
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 2) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_WEXIT, unit, raw, address, slot);
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 3) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_WTOP, unit, raw, address, slot);
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 6) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_CEXIT, unit, raw, address, slot);
        insn.qp = 0;
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 7) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_CTOP, unit, raw, address, slot);
        insn.qp = 0;
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B &&
        ((ia64_b_op(raw) == 2 &&
          (ia64_bits(raw, 27, 6) == 0x10 ||
           ia64_bits(raw, 27, 6) == 0x11)) ||
         ia64_b_op(raw) == 7)) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BRP, unit, raw, address, slot);

        /* B6/B7 have no qualifying predicate field. */
        insn.qp = 0;
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 2) {
        /* Blank opcode cells in Table 4-55 are white (ignored). */
        return ia64_ignored_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_F && ia64_b_op(raw) == 5) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_FCLASS, unit, raw, address, slot);
        insn.operands.decoder.p1 = ia64_bits(raw, 6, 6);
        insn.operands.decoder.p2 = ia64_bits(raw, 27, 6);
        insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
        insn.operands.decoder.imm = (ia64_bits(raw, 20, 7) << 2) |
                   ia64_bits(raw, 33, 2);
        insn.compare_unc = ia64_bits(raw, 12, 1) != 0;
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 33, 3) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        const uint64_t x4 = x6 & 0xf;
        Ia64Opcode opcode = IA64_OP_ILLEGAL;
        if (x6 == 0x31) {
            opcode = IA64_OP_SRLZ;
        } else if (x6 == 0x30) {
            opcode = IA64_OP_SRLZ_D;
        } else if (x6 == 0x33) {
            opcode = IA64_OP_SYNC_I;
        } else if (x6 == 0x20) {
            opcode = IA64_OP_FWB;
        } else if (x4 == 0x04) {
            opcode = IA64_OP_SUM_UM;
        } else if (x4 == 0x05) {
            opcode = IA64_OP_RUM;
        } else if (x4 == 0x06) {
            opcode = IA64_OP_SSM;
        } else if (x4 == 0x07) {
            opcode = IA64_OP_RSM;
        } else if (x6 == 0x22) {
            opcode = IA64_OP_MF;
        } else if (x6 == 0x23) {
            opcode = IA64_OP_MF_A;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);
            if (opcode == IA64_OP_SUM_UM || opcode == IA64_OP_RUM ||
                opcode == IA64_OP_SSM || opcode == IA64_OP_RSM) {
                insn.operands.decoder.imm = ia64_psr_mask(raw);
            }
            return insn;
        }
    }

    if (unit == IA64_UNIT_L) {
        /*
         * movl r1 = imm64 (X2 format, MLX template).
         * In MLX, the L-slot carries imm41 (bits 62:22), and the X-slot
         * carries opcode(6), r1 and the remaining immediate fields.
         * r1/imm are completed during the MLX fixup pass.
         */
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_MOVL, unit, raw, address, slot);
        insn.operands.decoder.r1 = 0;   /* filled by MLX fixup */
        insn.operands.decoder.imm = 0;  /* filled by MLX fixup */
        return insn;
    }

    if (unit == IA64_UNIT_X && ia64_b_op(raw) == 6) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_MOVL, unit, raw, address, slot);
        insn.operands.decoder.r1 = 0;   /* filled by MLX fixup */
        insn.operands.decoder.imm = 0;  /* filled by MLX fixup */
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 0) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_COND, unit, raw, address, slot);
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 4 &&
        ia64_bits(raw, 6, 3) == 5) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_CLOOP, unit, raw, address, slot);
        insn.qp = 0;
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 27, 6) == 0x20) {
        const uint64_t btype = ia64_bits(raw, 6, 3);

        if (btype == 0 || (btype == 1 && ia64_bits(raw, 0, 6) == 0)) {
            Ia64Instruction insn =
                ia64_base_insn(btype == 0 ? IA64_OP_BR_INDIRECT :
                               IA64_OP_BR_IA,
                               unit, raw, address, slot);
            insn.operands.decoder.b2 = ia64_bits(raw, 13, 3);
            return insn;
        }
    }

    /* Indirect call: br.call bRet=bTarget, B5. Completers are hints. */
    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 1) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_CALL_INDIRECT, unit, raw, address, slot);
        /* Target and return branch registers, respectively. */
        insn.operands.decoder.b2 = ia64_bits(raw, 13, 3);
        insn.operands.decoder.b1 = ia64_bits(raw, 6, 3);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 5) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_CALL, unit, raw, address, slot);
        insn.operands.decoder.b1 = ia64_bits(raw, 6, 3);
        insn.operands.decoder.imm = ia64_branch_disp(raw);
        return insn;
    }

    if (unit == IA64_UNIT_B && ia64_b_op(raw) == 0 &&
        ia64_bits(raw, 27, 6) == 0x21 && ia64_bits(raw, 6, 3) == 4) {
        Ia64Instruction insn =
            ia64_base_insn(IA64_OP_BR_RET, unit, raw, address, slot);
        insn.operands.decoder.b2 = ia64_bits(raw, 13, 3);
        return insn;
    }

    if (unit == IA64_UNIT_M && ia64_b_op(raw) == 1 &&
        ia64_bits(raw, 33, 3) == 0) {
        const uint64_t x6 = ia64_bits(raw, 27, 6);
        Ia64Opcode opcode = IA64_OP_ILLEGAL;

        switch (x6) {
        case 0x09:
            opcode = IA64_OP_PTC_L;
            break;
        case 0x0a:
            opcode = IA64_OP_PTC_G;
            break;
        case 0x0b:
            opcode = IA64_OP_PTC_GA;
            break;
        case 0x0c:
            opcode = IA64_OP_PTR_D;
            break;
        case 0x0d:
            opcode = IA64_OP_PTR_I;
            break;
        case 0x0e:
            opcode = IA64_OP_ITR_D;
            break;
        case 0x0f:
            opcode = IA64_OP_ITR_I;
            break;
        case 0x2e:
            opcode = IA64_OP_ITC_D;
            break;
        case 0x2f:
            opcode = IA64_OP_ITC_I;
            break;
        case 0x34:
            opcode = IA64_OP_PTC_E;
            break;
        }
        if (opcode != IA64_OP_ILLEGAL) {
            Ia64Instruction insn =
                ia64_base_insn(opcode, unit, raw, address, slot);

            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            insn.mem_release = opcode == IA64_OP_PTC_L ||
                               opcode == IA64_OP_PTC_G ||
                               opcode == IA64_OP_PTC_GA ||
                               opcode == IA64_OP_PTC_E;
            return insn;
        }
    }

    /*
     * All defined integer-memory paths above take precedence.  The remaining
     * cells in each of the four major-4 (m, x) selector spaces are purple in
     * Tables 4-28 and 4-30/4-31/4-33; the unmatched cells in the major-5
     * immediate space are likewise purple in Table 4-32.
     */
    if (unit == IA64_UNIT_M &&
        (ia64_b_op(raw) == 4 || ia64_b_op(raw) == 5)) {
        return ia64_reserved_if_qp_insn(unit, raw, address, slot);
    }

    /*
     * H. ADDP4 / ADDS-imm: M/I-unit, b_op == 8
     *
     * Register form (addp4 r1=r2,r3): x3=0, x4=2, x2b=0.
     * Immediate form (addp4/adds r1=imm,r3): bits 34:33 may carry
     * immediate value bits (up to 3), so we accept any x3 for the
     * I-unit path and decode it as an ADDP4/adds immediate.
     */
    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 8) {
        const uint64_t x4 = ia64_bits(raw, 29, 4);
        const uint64_t x2b = ia64_bits(raw, 27, 2);
        const uint64_t x3 = ia64_bits(raw, 33, 3);

        /* register form — a clean {0,2,0} */
        if (x3 == 0 && x4 == 2 && x2b == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_ADDP4, unit, raw, address, slot);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r2 = ia64_bits(raw, 13, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }

        /*
         * A4 addp4 immediate uses x2a=3/ve=0; bits 36 and 32:27 are part
         * of the 14-bit signed immediate, not register-form extensions.
         */
        if (ia64_bits(raw, 34, 2) == 3 && ia64_bits(raw, 33, 1) == 0) {
            Ia64Instruction insn =
                ia64_base_insn(IA64_OP_ADDP4_IMM, unit, raw, address, slot);
            insn.operands.decoder.imm = ia64_imm14(raw);
            insn.operands.decoder.r1 = ia64_bits(raw, 6, 7);
            insn.operands.decoder.r3 = ia64_bits(raw, 20, 7);
            return insn;
        }
    }

    /*
     * All defined A-unit major-8 paths above take precedence.  The remaining
     * cells in Tables 4-8, 4-9, and 4-12 through 4-15 are purple, regardless
     * of whether the A-unit instruction occupies an M or I slot.
     */
    if ((unit == IA64_UNIT_M || unit == IA64_UNIT_I) &&
        ia64_b_op(raw) == 8) {
        return ia64_reserved_if_qp_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_I) {
        const uint64_t major = ia64_b_op(raw);

        /*
         * Defined I-unit paths have already returned.  The unmatched cells
         * below are purple in Tables 4-16 through 4-26; major opcodes
         * 1/2/3/6/A/B/F are purple directly in Table 4-3.
         */
        if (major == 0 || major == 1 || major == 2 || major == 3 ||
            major == 5 || major == 6 || major == 7 || major == 0xa ||
            major == 0xb || major == 0xf) {
            return ia64_reserved_if_qp_insn(unit, raw, address, slot);
        }
    }

    /*
     * The remaining M-unit system/memory-management cells in Tables 4-42
     * through 4-45, and the M/A major-opcode cells marked below in Table 4-3,
     * are purple (Reserved if PR[qp] is 1).
     */
    if (unit == IA64_UNIT_M &&
        (ia64_b_op(raw) == 0 || ia64_b_op(raw) == 1 ||
         ia64_b_op(raw) == 0xa || ia64_b_op(raw) == 0xb ||
         ia64_b_op(raw) == 0xf)) {
        return ia64_reserved_if_qp_insn(unit, raw, address, slot);
    }

    if (unit == IA64_UNIT_B) {
        const uint64_t major = ia64_b_op(raw);

        /* B-unit major opcodes 3 and 6 are white in Table 4-3. */
        if (major == 3 || major == 6) {
            return ia64_ignored_insn(unit, raw, address, slot);
        }

        if (major == 0) {
            const uint64_t x6 = ia64_bits(raw, 27, 6);

            /*
             * All unmatched x6 cells below 0x20 are cyan.  x6=20/21
             * use the brown btype sub-tables and x6=22..3f are brown here,
             * so they intentionally retain the unconditional invalid path.
             */
            if (x6 < 0x20) {
                return ia64_reserved_if_qp_b_insn(
                    unit, raw, address, slot);
            }
        }
    }

    return ia64_invalid_insn(unit, raw, address, slot);
}
