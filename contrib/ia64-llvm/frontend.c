/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lower.h"

typedef struct Compare {
    LLVMIntPredicate relation;
    bool word;
    bool immediate;
} Compare;

static const Compare compares[IA64_OP_COUNT] = {
    [IA64_OP_CMP_EQ] = { LLVMIntEQ },
    [IA64_OP_CMP_LT] = { LLVMIntSLT },
    [IA64_OP_CMP_LTU] = { LLVMIntULT },
    [IA64_OP_CMP_EQ_IMM] = { LLVMIntEQ, .immediate = true },
    [IA64_OP_CMP_LT_IMM] = { LLVMIntSLT, .immediate = true },
    [IA64_OP_CMP_LTU_IMM] = { LLVMIntULT, .immediate = true },
    [IA64_OP_CMP4_EQ] = { LLVMIntEQ, .word = true },
    [IA64_OP_CMP4_LT] = { LLVMIntSLT, .word = true },
    [IA64_OP_CMP4_LTU] = { LLVMIntULT, .word = true },
    [IA64_OP_CMP4_EQ_IMM] = { LLVMIntEQ, .word = true, .immediate = true },
    [IA64_OP_CMP4_LT_IMM] = { LLVMIntSLT, .word = true, .immediate = true },
    [IA64_OP_CMP4_LTU_IMM] = { LLVMIntULT, .word = true, .immediate = true },
};

static LLVMValueRef extend_field(LLVMBuilderRef b, LLVMValueRef v,
                                 unsigned width, bool sign)
{
    LLVMTypeRef i64 = LLVMTypeOf(v);
    if (width == 64) {
        return v;
    }
    LLVMTypeRef field = LLVMIntTypeInContext(LLVMGetTypeContext(i64), width);
    v = LLVMBuildTrunc(b, v, field, "");
    return sign ? LLVMBuildSExt(b, v, i64, "") : LLVMBuildZExt(b, v, i64, "");
}

static LLVMValueRef register_shift(LLVMBuilderRef b, Ia64Opcode op,
                                   LLVMValueRef count, LLVMValueRef value)
{
    LLVMTypeRef i64 = LLVMTypeOf(value);
    LLVMValueRef max = LLVMConstInt(i64, 63, 0);
    LLVMValueRef large = LLVMBuildICmp(b, LLVMIntUGT, count, max, "");
    /* IA-64 consumes the full count. LLVM shifts require a count below 64. */
    count = LLVMBuildSelect(b, large, max, count, "");
    if (op == IA64_OP_SHR) {
        return LLVMBuildAShr(b, value, count, "");
    }
    value = op == IA64_OP_SHL ? LLVMBuildShl(b, value, count, "")
                              : LLVMBuildLShr(b, value, count, "");
    return LLVMBuildSelect(b, large, LLVMConstNull(i64), value, "");
}

LLVMValueRef ia64_llvm_lift_integer(LLVMBuilderRef b, const Ia64Instruction *in,
                                    LLVMValueRef lhs, LLVMValueRef rhs)
{
    Ia64Opcode op = in->opcode;
    LLVMTypeRef i64 = LLVMTypeOf(rhs);
    uint64_t imm = in->operands.integer.immediate;
    unsigned pos = imm & 63;
    unsigned len = ia64_bitfield_effective_len(pos, (imm >> 6) & 127);
    LLVMValueRef immediate = LLVMConstInt(i64, imm, 0);
    LLVMValueRef shift = LLVMConstInt(i64, pos, 0);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);

    switch (op) {
    case IA64_OP_ADDP4:
    case IA64_OP_ADDP4_IMM:
    case IA64_OP_SHLADDP4: {
        /* The region comes from rhs[31:30], independent of the carry. */
        if (op == IA64_OP_ADDP4_IMM) {
            lhs = immediate;
        } else if (op == IA64_OP_SHLADDP4) {
            lhs = LLVMBuildShl(b, lhs, shift, "");
        }
        LLVMValueRef sum =
            extend_field(b, LLVMBuildAdd(b, lhs, rhs, ""), 32, false);
        LLVMValueRef region =
            LLVMBuildLShr(b, rhs, LLVMConstInt(i64, 30, 0), "");
        region = LLVMBuildAnd(b, region, LLVMConstInt(i64, 3, 0), "");
        region = LLVMBuildShl(b, region, LLVMConstInt(i64, 61, 0), "");
        return LLVMBuildOr(b, sum, region, "");
    }
    case IA64_OP_ADD:
        return LLVMBuildAdd(b, lhs, rhs, "");
    case IA64_OP_ADD_ONE:
        return LLVMBuildAdd(b, LLVMBuildAdd(b, lhs, rhs, ""), one, "");
    case IA64_OP_SUB:
        return LLVMBuildSub(b, lhs, rhs, "");
    case IA64_OP_SUB_ONE:
        return LLVMBuildSub(b, LLVMBuildSub(b, lhs, rhs, ""), one, "");
    case IA64_OP_SUB_IMM:
        return LLVMBuildSub(b, immediate, rhs, "");
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
        return LLVMBuildAdd(b, rhs, immediate, "");
    case IA64_OP_AND:
    case IA64_OP_AND_IMM:
        return LLVMBuildAnd(b, op == IA64_OP_AND ? lhs : immediate, rhs, "");
    case IA64_OP_ANDCM:
    case IA64_OP_ANDCM_IMM:
        return LLVMBuildAnd(b, op == IA64_OP_ANDCM ? lhs : immediate,
                            LLVMBuildNot(b, rhs, ""), "");
    case IA64_OP_OR:
    case IA64_OP_OR_IMM:
        return LLVMBuildOr(b, op == IA64_OP_OR ? lhs : immediate, rhs, "");
    case IA64_OP_XOR:
    case IA64_OP_XOR_IMM:
        return LLVMBuildXor(b, op == IA64_OP_XOR ? lhs : immediate, rhs, "");
    case IA64_OP_SHLADD:
        return LLVMBuildAdd(b, LLVMBuildShl(b, lhs, shift, ""), rhs, "");
    case IA64_OP_SHL:
    case IA64_OP_SHR:
    case IA64_OP_SHRU:
        return register_shift(b, op, lhs, rhs);
    case IA64_OP_SHL_IMM:
        return LLVMBuildShl(b, rhs, shift, "");
    case IA64_OP_SHR_IMM:
        return LLVMBuildAShr(b, rhs, shift, "");
    case IA64_OP_SHRU_IMM:
        return LLVMBuildLShr(b, rhs, shift, "");
    case IA64_OP_EXTR:
    case IA64_OP_EXTRU:
        return len ? extend_field(b, LLVMBuildLShr(b, rhs, shift, ""), len,
                                  op == IA64_OP_EXTR)
                   : NULL;
    case IA64_OP_DEPZ:
    case IA64_OP_DEPZ_IMM:
    case IA64_OP_DEP:
    case IA64_OP_DEP_IMM: {
        if (!len) {
            return NULL;
        }
        if (op == IA64_OP_DEPZ_IMM) {
            lhs = LLVMConstInt(i64, ia64_sign_extend(imm >> 13, 8), 0);
        } else if (op == IA64_OP_DEP_IMM) {
            lhs = LLVMConstInt(i64, UINT64_C(0) - ((imm >> 13) & 1), 0);
        }
        uint64_t mask = ia64_deposit_mask(pos, len);
        LLVMValueRef insert = LLVMBuildAnd(b, LLVMBuildShl(b, lhs, shift, ""),
                                           LLVMConstInt(i64, mask, 0), "");
        if (op == IA64_OP_DEPZ || op == IA64_OP_DEPZ_IMM) {
            return insert;
        }
        LLVMValueRef keep =
            LLVMBuildAnd(b, rhs, LLVMConstInt(i64, ~mask, 0), "");
        return LLVMBuildOr(b, keep, insert, "");
    }
    case IA64_OP_SHRP_IMM:
        if (!pos) {
            return rhs;
        }
        return LLVMBuildOr(
            b, LLVMBuildShl(b, lhs, LLVMConstInt(i64, 64 - pos, 0), ""),
            LLVMBuildLShr(b, rhs, shift, ""), "");
    case IA64_OP_SXT1:
    case IA64_OP_SXT2:
    case IA64_OP_SXT4:
        return extend_field(b, rhs, 8u << (op - IA64_OP_SXT1), true);
    case IA64_OP_ZXT1:
    case IA64_OP_ZXT2:
    case IA64_OP_ZXT4:
        return extend_field(b, rhs, 8u << (op - IA64_OP_ZXT1), false);
    case IA64_OP_MIX4_R:
        lhs = LLVMBuildShl(b, lhs, LLVMConstInt(i64, 32, 0), "");
        return LLVMBuildOr(b, lhs, extend_field(b, rhs, 32, false), "");
    case IA64_OP_MIX4_L:
        lhs = LLVMBuildAnd(b, lhs, LLVMConstInt(i64, UINT64_MAX << 32, 0), "");
        rhs = LLVMBuildLShr(b, rhs, LLVMConstInt(i64, 32, 0), "");
        return LLVMBuildOr(b, lhs, rhs, "");
    default: {
        const Compare *cmp = &compares[op];
        if (!cmp->relation || in->pred_update != IA64_PRED_UPDATE_NORMAL) {
            return NULL;
        }
        if (cmp->immediate) {
            lhs = immediate;
        }
        if (cmp->word) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(LLVMGetTypeContext(i64));
            lhs = LLVMBuildTrunc(b, lhs, i32, "");
            rhs = LLVMBuildTrunc(b, rhs, i32, "");
        }
        return LLVMBuildICmp(b, cmp->relation, lhs, rhs, "");
    }
    }
}
