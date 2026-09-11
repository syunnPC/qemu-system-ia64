/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 packed integer and multimedia instruction generation.
 */

#include "qemu/osdep.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec-common.h"

#include "target/ia64/translate/translate.h"

static void ia64_gen_saturate_signed_i64(TCGv_i64 value, int bits)
{
    int64_t min_value = -(1LL << (bits - 1));
    int64_t max_value = (1LL << (bits - 1)) - 1;

    tcg_gen_smax_i64(value, value, tcg_constant_i64(min_value));
    tcg_gen_smin_i64(value, value, tcg_constant_i64(max_value));
}

typedef struct IA64SIMDLanePlan {
    unsigned bits;
    unsigned count;
    uint64_t mask;
} IA64SIMDLanePlan;

static unsigned ia64_simd_vece(unsigned bits)
{
    switch (bits) {
    case 8:
        return MO_8;
    case 16:
        return MO_16;
    case 32:
        return MO_32;
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_vec_result_i64(TCGv_i64 result, TCGv_vec value)
{
    if (tcg_gen_mov_vec_i64(result, value)) {
        return;
    }
    /*
     * Hosts without a vector-to-integer register transfer retain the private
     * per-vCPU slot, without synchronizing globals around a helper.
     */
    tcg_gen_st_vec(value, tcg_env,
                   offsetof(CPUIA64State, tcg_vec_scratch));
    tcg_gen_ld_i64(result, tcg_env,
                   offsetof(CPUIA64State, tcg_vec_scratch));
}

static void ia64_simd_vec_temps(DisasContext *ctx)
{
    if (ctx->simd.a == NULL) {
        ctx->simd.a = tcg_temp_new_vec(TCG_TYPE_V64);
        ctx->simd.b = tcg_temp_new_vec(TCG_TYPE_V64);
        ctx->simd.result = tcg_temp_new_vec(TCG_TYPE_V64);
    }
}

static bool ia64_gen_packed_vec_binary(DisasContext *ctx, TCGv_i64 result,
                                       TCGv_i64 a,
                                       TCGv_i64 b, unsigned bits,
                                       TCGOpcode opcode, TCGCond cond)
{
    unsigned vece = ia64_simd_vece(bits);
    TCGv_vec va, vb, vr;

    if (!tcg_op_supported(INDEX_op_dup_vec, TCG_TYPE_V64, 0) ||
        tcg_can_emit_vec_op(opcode, TCG_TYPE_V64, vece) == 0) {
        return false;
    }

    ia64_simd_vec_temps(ctx);
    va = ctx->simd.a;
    vb = ctx->simd.b;
    vr = ctx->simd.result;
    tcg_gen_dup_i64_vec(MO_64, va, a);
    tcg_gen_dup_i64_vec(MO_64, vb, b);

    switch (opcode) {
    case INDEX_op_ssadd_vec:
        tcg_gen_ssadd_vec(vece, vr, va, vb);
        break;
    case INDEX_op_usadd_vec:
        tcg_gen_usadd_vec(vece, vr, va, vb);
        break;
    case INDEX_op_sssub_vec:
        tcg_gen_sssub_vec(vece, vr, va, vb);
        break;
    case INDEX_op_ussub_vec:
        tcg_gen_ussub_vec(vece, vr, va, vb);
        break;
    case INDEX_op_smin_vec:
        tcg_gen_smin_vec(vece, vr, va, vb);
        break;
    case INDEX_op_umin_vec:
        tcg_gen_umin_vec(vece, vr, va, vb);
        break;
    case INDEX_op_smax_vec:
        tcg_gen_smax_vec(vece, vr, va, vb);
        break;
    case INDEX_op_umax_vec:
        tcg_gen_umax_vec(vece, vr, va, vb);
        break;
    case INDEX_op_cmp_vec:
        tcg_gen_cmp_vec(cond, vece, vr, va, vb);
        break;
    default:
        g_assert_not_reached();
    }

    ia64_gen_vec_result_i64(result, vr);
    return true;
}

static bool ia64_gen_packed_vec_shift(DisasContext *ctx, TCGv_i64 result,
                                      TCGv_i64 value,
                                      TCGv_i64 count, unsigned bits,
                                      TCGOpcode opcode)
{
    unsigned vece = ia64_simd_vece(bits);
    TCGv_i32 count32;
    TCGv_vec vv, vr;

    if (!tcg_op_supported(INDEX_op_dup_vec, TCG_TYPE_V64, 0) ||
        tcg_can_emit_vec_op(opcode, TCG_TYPE_V64, vece) == 0) {
        return false;
    }

    count32 = tcg_temp_new_i32();
    ia64_simd_vec_temps(ctx);
    vv = ctx->simd.a;
    vr = ctx->simd.result;
    tcg_gen_extrl_i64_i32(count32, count);
    tcg_gen_dup_i64_vec(MO_64, vv, value);
    switch (opcode) {
    case INDEX_op_shls_vec:
        tcg_gen_shls_vec(vece, vr, vv, count32);
        break;
    case INDEX_op_shrs_vec:
        tcg_gen_shrs_vec(vece, vr, vv, count32);
        break;
    case INDEX_op_sars_vec:
        tcg_gen_sars_vec(vece, vr, vv, count32);
        break;
    default:
        g_assert_not_reached();
    }
    ia64_gen_vec_result_i64(result, vr);
    return true;
}

static bool ia64_gen_packed_vec_shifti(DisasContext *ctx, TCGv_i64 result,
                                      TCGv_i64 value, unsigned count,
                                      unsigned bits, TCGOpcode opcode)
{
    unsigned vece = ia64_simd_vece(bits);

    if (count == 0) {
        tcg_gen_mov_i64(result, value);
        return true;
    }
    if (count >= bits) {
        if (opcode != INDEX_op_sari_vec) {
            tcg_gen_movi_i64(result, 0);
            return true;
        }
        count = bits - 1;
    }
    if (!tcg_op_supported(INDEX_op_dup_vec, TCG_TYPE_V64, 0) ||
        tcg_can_emit_vec_op(opcode, TCG_TYPE_V64, vece) == 0) {
        return false;
    }

    ia64_simd_vec_temps(ctx);
    tcg_gen_dup_i64_vec(MO_64, ctx->simd.a, value);
    switch (opcode) {
    case INDEX_op_shli_vec:
        tcg_gen_shli_vec(vece, ctx->simd.result, ctx->simd.a, count);
        break;
    case INDEX_op_shri_vec:
        tcg_gen_shri_vec(vece, ctx->simd.result, ctx->simd.a, count);
        break;
    case INDEX_op_sari_vec:
        tcg_gen_sari_vec(vece, ctx->simd.result, ctx->simd.a, count);
        break;
    default:
        g_assert_not_reached();
    }
    ia64_gen_vec_result_i64(result, ctx->simd.result);
    return true;
}

static bool ia64_gen_pshladd2_vec(DisasContext *ctx, TCGv_i64 result,
                                  TCGv_i64 a,
                                  TCGv_i64 b, unsigned count)
{
    TCGv_vec va, vb, vr, shift_ok;

    if (!tcg_op_supported(INDEX_op_dup_vec, TCG_TYPE_V64, 0) ||
        tcg_can_emit_vec_op(INDEX_op_sari_vec, TCG_TYPE_V64, MO_16) == 0 ||
        tcg_can_emit_vec_op(INDEX_op_cmp_vec, TCG_TYPE_V64, MO_16) == 0 ||
        tcg_can_emit_vec_op(INDEX_op_ssadd_vec, TCG_TYPE_V64, MO_16) == 0) {
        return false;
    }

    ia64_simd_vec_temps(ctx);
    va = ctx->simd.a;
    vb = ctx->simd.b;
    vr = ctx->simd.result;
    shift_ok = tcg_temp_new_vec(TCG_TYPE_V64);
    tcg_gen_dup_i64_vec(MO_64, va, a);
    tcg_gen_dup_i64_vec(MO_64, vb, b);
    tcg_gen_mov_vec(vr, va);
    for (unsigned i = 0; i < count; i++) {
        tcg_gen_ssadd_vec(MO_16, vr, vr, vr);
    }

    /* A saturated shift is terminal; add b only in lanes without overflow. */
    tcg_gen_sari_vec(MO_16, shift_ok, vr, count);
    tcg_gen_cmp_vec(TCG_COND_EQ, MO_16, shift_ok, shift_ok, va);
    tcg_gen_ssadd_vec(MO_16, vb, vr, vb);
    tcg_gen_bitsel_vec(MO_16, vr, shift_ok, vb, vr);
    ia64_gen_vec_result_i64(result, vr);
    return true;
}

static bool ia64_gen_pshradd2_vec(DisasContext *ctx, TCGv_i64 result,
                                  TCGv_i64 a,
                                  TCGv_i64 b, unsigned count)
{
    TCGv_vec va, vb;

    if (!tcg_op_supported(INDEX_op_dup_vec, TCG_TYPE_V64, 0) ||
        tcg_can_emit_vec_op(INDEX_op_sari_vec, TCG_TYPE_V64, MO_16) == 0 ||
        tcg_can_emit_vec_op(INDEX_op_ssadd_vec, TCG_TYPE_V64, MO_16) == 0) {
        return false;
    }

    ia64_simd_vec_temps(ctx);
    va = ctx->simd.a;
    vb = ctx->simd.b;
    tcg_gen_dup_i64_vec(MO_64, va, a);
    tcg_gen_dup_i64_vec(MO_64, vb, b);
    tcg_gen_sari_vec(MO_16, va, va, count);
    tcg_gen_ssadd_vec(MO_16, va, va, vb);
    ia64_gen_vec_result_i64(result, va);
    return true;
}

static IA64SIMDLanePlan ia64_simd_lane_plan(unsigned bits)
{
    return (IA64SIMDLanePlan) {
        .bits = bits,
        .count = 64 / bits,
        .mask = (1ULL << bits) - 1,
    };
}

static uint64_t ia64_simd_repeated_bit(unsigned bits, unsigned bit)
{
    uint64_t result = 0;

    for (unsigned lane = 0; lane < 64; lane += bits) {
        result |= UINT64_C(1) << (lane + bit);
    }
    return result;
}

static void ia64_gen_simd_mix(TCGv_i64 result, TCGv_i64 a, TCGv_i64 b,
                              unsigned bits, bool left)
{
    const uint64_t low_mask =
        bits == 8 ? UINT64_C(0x00ff00ff00ff00ff) :
        bits == 16 ? UINT64_C(0x0000ffff0000ffff) :
                     UINT64_C(0x00000000ffffffff);
    TCGv_i64 tmp = tcg_temp_new_i64();

    if (left) {
        tcg_gen_andi_i64(result, a, ~low_mask);
        tcg_gen_shri_i64(tmp, b, bits);
        tcg_gen_andi_i64(tmp, tmp, low_mask);
    } else {
        tcg_gen_shli_i64(tmp, a, bits);
        tcg_gen_andi_i64(tmp, tmp, ~low_mask);
        tcg_gen_andi_i64(result, b, low_mask);
    }
    tcg_gen_or_i64(result, result, tmp);
}

static void ia64_gen_packed_wrap(TCGv_i64 result, TCGv_i64 a, TCGv_i64 b,
                                 unsigned bits, bool subtract)
{
    switch (bits) {
    case 8:
        if (subtract) {
            tcg_gen_vec_sub8_i64(result, a, b);
        } else {
            tcg_gen_vec_add8_i64(result, a, b);
        }
        break;
    case 16:
        if (subtract) {
            tcg_gen_vec_sub16_i64(result, a, b);
        } else {
            tcg_gen_vec_add16_i64(result, a, b);
        }
        break;
    case 32:
        if (subtract) {
            tcg_gen_vec_sub32_i64(result, a, b);
        } else {
            tcg_gen_vec_add32_i64(result, a, b);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_packed_average(TCGv_i64 result, TCGv_i64 a, TCGv_i64 b,
                                    unsigned bits, bool round_away_from_zero)
{
    uint64_t low_mask = ia64_simd_repeated_bit(bits, 0);
    TCGv_i64 differing = tcg_temp_new_i64();
    TCGv_i64 half = tcg_temp_new_i64();

    tcg_gen_xor_i64(differing, a, b);
    tcg_gen_andi_i64(half, differing, ~low_mask);
    tcg_gen_shri_i64(half, half, 1);
    if (round_away_from_zero) {
        tcg_gen_or_i64(result, a, b);
        tcg_gen_sub_i64(result, result, half);
    } else {
        /*
         * Ordinary pavg rounds an inexact half result to an odd low bit,
         * whereas pavg.raz rounds it upward.  OR, rather than addition, is
         * therefore intentional for the discarded low bit.
         */
        tcg_gen_and_i64(result, a, b);
        tcg_gen_add_i64(result, result, half);
        tcg_gen_andi_i64(differing, differing, low_mask);
        tcg_gen_or_i64(result, result, differing);
    }
}

static void ia64_gen_packed_equal(TCGv_i64 result, TCGv_i64 a, TCGv_i64 b,
                                  unsigned bits)
{
    uint64_t high_mask = ia64_simd_repeated_bit(bits, bits - 1);
    uint64_t low_mask = ia64_simd_repeated_bit(bits, 0);
    uint64_t lane_mask = (UINT64_C(1) << bits) - 1;
    TCGv_i64 different = tcg_temp_new_i64();
    TCGv_i64 nonzero = tcg_temp_new_i64();

    /*
     * Setting every lane's high bit before subtracting one prevents borrows
     * from crossing lane boundaries.  The remaining high bits identify
     * exactly the zero lanes in a ^ b; multiplication expands each one-bit
     * result to an all-ones lane.
     */
    tcg_gen_xor_i64(different, a, b);
    tcg_gen_ori_i64(nonzero, different, high_mask);
    tcg_gen_subi_i64(nonzero, nonzero, low_mask);
    tcg_gen_or_i64(nonzero, nonzero, different);
    tcg_gen_not_i64(nonzero, nonzero);
    tcg_gen_andi_i64(nonzero, nonzero, high_mask);
    tcg_gen_shri_i64(nonzero, nonzero, bits - 1);
    tcg_gen_muli_i64(result, nonzero, lane_mask);
}

static void ia64_gen_czx(TCGv_i64 result, TCGv_i64 value,
                         unsigned bits, bool from_left)
{
    uint64_t high_mask = ia64_simd_repeated_bit(bits, bits - 1);
    uint64_t low_mask = ia64_simd_repeated_bit(bits, 0);
    TCGv_i64 zero_high = tcg_temp_new_i64();

    /*
     * Form one high bit for every zero lane, then let clz/ctz select the
     * first lane.  Their defined 64 fallback also produces the architectural
     * lane count when no lane is zero.
     */
    tcg_gen_ori_i64(zero_high, value, high_mask);
    tcg_gen_subi_i64(zero_high, zero_high, low_mask);
    tcg_gen_or_i64(zero_high, zero_high, value);
    tcg_gen_not_i64(zero_high, zero_high);
    tcg_gen_andi_i64(zero_high, zero_high, high_mask);
    if (from_left) {
        tcg_gen_clzi_i64(result, zero_high, 64);
    } else {
        tcg_gen_ctzi_i64(result, zero_high, 64);
    }
    tcg_gen_shri_i64(result, result, bits == 8 ? 3 : 4);
}

static void ia64_gen_mux1(TCGv_i64 result, TCGv_i64 value, uint32_t imm)
{
    static const uint8_t permutations[3][8] = {
        { 0, 4, 2, 6, 1, 5, 3, 7 },
        { 0, 4, 1, 5, 2, 6, 3, 7 },
        { 0, 2, 4, 6, 1, 3, 5, 7 },
    };
    uint64_t masks[15] = { 0 };
    TCGv_i64 part = tcg_temp_new_i64();
    bool first = true;

    if (imm == 0) {
        tcg_gen_ext8u_i64(result, value);
        tcg_gen_muli_i64(result, result, UINT64_C(0x0101010101010101));
        return;
    }
    if (imm == 0xb) {
        tcg_gen_bswap64_i64(result, value);
        return;
    }
    g_assert(imm >= 8 && imm <= 0xa);
    for (unsigned i = 0; i < 8; i++) {
        int shift = (int)i - permutations[imm - 8][i];

        masks[shift + 7] |= UINT64_C(0xff) << (i * 8);
    }
    for (int shift = -7; shift <= 7; shift++) {
        if (!masks[shift + 7]) {
            continue;
        }
        if (shift < 0) {
            tcg_gen_shri_i64(part, value, -shift * 8);
        } else {
            tcg_gen_shli_i64(part, value, shift * 8);
        }
        tcg_gen_andi_i64(part, part, masks[shift + 7]);
        if (first) {
            tcg_gen_mov_i64(result, part);
            first = false;
        } else {
            tcg_gen_or_i64(result, result, part);
        }
    }
}

static void ia64_gen_unpack(TCGv_i64 result, TCGv_i64 a, TCGv_i64 b,
                            unsigned bits, bool low)
{
    TCGv_i64 left = tcg_temp_new_i64();
    TCGv_i64 right = tcg_temp_new_i64();
    TCGv_i64 shifted = tcg_temp_new_i64();
    TCGv_i64 parts[2] = { left, right };

    tcg_gen_extract_i64(left, a, low ? 0 : 32, 32);
    tcg_gen_extract_i64(right, b, low ? 0 : 32, 32);
    for (unsigned i = 0; i < ARRAY_SIZE(parts); i++) {
        TCGv_i64 part = parts[i];

        if (bits <= 16) {
            tcg_gen_shli_i64(shifted, part, 16);
            tcg_gen_or_i64(part, part, shifted);
            tcg_gen_andi_i64(part, part, UINT64_C(0x0000ffff0000ffff));
        }
        if (bits == 8) {
            tcg_gen_shli_i64(shifted, part, 8);
            tcg_gen_or_i64(part, part, shifted);
            tcg_gen_andi_i64(part, part, UINT64_C(0x00ff00ff00ff00ff));
        }
    }
    tcg_gen_shli_i64(left, left, bits);
    tcg_gen_or_i64(result, left, right);
}

static void ia64_gen_mux2(TCGv_i64 result, TCGv_i64 value, uint32_t imm)
{
    uint32_t first_lane = imm & 3;
    uint32_t broadcast = first_lane * 0x55;
    bool first = true;
    int i;

    if ((imm & 0xff) == 0xe4) {
        /* Each destination lane selects the same-numbered source lane. */
        tcg_gen_mov_i64(result, value);
        return;
    }
    if ((imm & 0xff) == broadcast) {
        if (first_lane != 0) {
            tcg_gen_shri_i64(result, value, first_lane * 16);
        } else {
            tcg_gen_mov_i64(result, value);
        }
        tcg_gen_ext16u_i64(result, result);
        tcg_gen_muli_i64(result, result, UINT64_C(0x0001000100010001));
        return;
    }

    for (i = 0; i < 4; i++) {
        uint32_t source_lane = (imm >> (i * 2)) & 3;
        int shift = ((int)source_lane - i) * 16;
        uint64_t mask = UINT64_C(0xffff) << (i * 16);
        TCGv_i64 lane = tcg_temp_new_i64();

        if (shift > 0) {
            tcg_gen_shri_i64(lane, value, shift);
        } else if (shift < 0) {
            tcg_gen_shli_i64(lane, value, -shift);
        } else {
            tcg_gen_mov_i64(lane, value);
        }
        tcg_gen_andi_i64(lane, lane, mask);
        if (first) {
            tcg_gen_mov_i64(result, lane);
            first = false;
        } else {
            tcg_gen_or_i64(result, result, lane);
        }
    }
}

static TCGv_i64 ia64_simd_extract_lane(TCGv_i64 source,
                                       const IA64SIMDLanePlan *plan,
                                       unsigned lane, bool sign_extend)
{
    TCGv_i64 value = tcg_temp_new_i64();

    if (sign_extend) {
        tcg_gen_sextract_i64(value, source, lane * plan->bits, plan->bits);
    } else {
        tcg_gen_extract_i64(value, source, lane * plan->bits, plan->bits);
    }
    return value;
}

static void ia64_simd_insert_lane(TCGv_i64 result, TCGv_i64 value,
                                  const IA64SIMDLanePlan *plan,
                                  unsigned lane)
{
    tcg_gen_andi_i64(value, value, plan->mask);
    tcg_gen_shli_i64(value, value, lane * plan->bits);
    tcg_gen_or_i64(result, result, value);
}

static void ia64_gen_pshr(DisasContext *ctx, const Ia64Instruction *insn,
                          int lane_bits,
                          bool unsigned_shift)
{
    const IA64SimdOperands *op = &insn->operands.simd;
    const IA64SIMDLanePlan plan = ia64_simd_lane_plan(lane_bits);
    TCGv_i64 result;
    TCGv_i64 count;
    TCGv_i64 clamped_count;

    if (op->destination == 0) {
        return;
    }

    result = tcg_temp_new_i64();
    if (op->immediate >= 0 &&
        ia64_gen_packed_vec_shifti(ctx, result, ia64_gr_src(op->source2),
                                   op->immediate, lane_bits,
                                   unsigned_shift ? INDEX_op_shri_vec :
                                                    INDEX_op_sari_vec)) {
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source2);
        return;
    }
    count = tcg_temp_new_i64();
    clamped_count = tcg_temp_new_i64();

    if (op->immediate >= 0) {
        tcg_gen_movi_i64(count, op->immediate);
    } else {
        tcg_gen_mov_i64(count, ia64_gr_src(op->source1));
    }
    tcg_gen_movcond_i64(TCG_COND_GTU, clamped_count, count,
                        tcg_constant_i64(lane_bits),
                        tcg_constant_i64(lane_bits), count);

    if (ia64_gen_packed_vec_shift(ctx,
            result, ia64_gr_src(op->source2), clamped_count, lane_bits,
            unsigned_shift ? INDEX_op_shrs_vec : INDEX_op_sars_vec)) {
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        if (op->immediate >= 0) {
            ia64_gen_gr_nat_from_1(insn, op->destination, op->source2);
        } else {
            ia64_gen_gr_nat_from_2(insn, op->destination,
                                   op->source1, op->source2);
        }
        return;
    }

    tcg_gen_movi_i64(result, 0);
    for (int i = 0; i < plan.count; i++) {
        TCGv_i64 lane = ia64_simd_extract_lane(
            ia64_gr_src(op->source2), &plan, i, !unsigned_shift);

        if (!unsigned_shift) {
            tcg_gen_sar_i64(lane, lane, clamped_count);
        } else {
            tcg_gen_shr_i64(lane, lane, clamped_count);
        }
        ia64_simd_insert_lane(result, lane, &plan, i);
    }

    tcg_gen_mov_i64(cpu_gr[op->destination], result);
    if (op->immediate >= 0) {
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source2);
    } else {
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
    }
}

static void ia64_gen_pshl(DisasContext *ctx, const Ia64Instruction *insn,
                          int lane_bits)
{
    const IA64SimdOperands *op = &insn->operands.simd;
    const IA64SIMDLanePlan plan = ia64_simd_lane_plan(lane_bits);
    TCGv_i64 result;
    TCGv_i64 count;
    TCGv_i64 clamped_count;

    if (op->destination == 0) {
        return;
    }

    result = tcg_temp_new_i64();
    if (op->immediate >= 0 &&
        ia64_gen_packed_vec_shifti(ctx, result, ia64_gr_src(op->source1),
                                   op->immediate, lane_bits,
                                   INDEX_op_shli_vec)) {
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source1);
        return;
    }
    count = tcg_temp_new_i64();
    clamped_count = tcg_temp_new_i64();

    if (op->immediate >= 0) {
        tcg_gen_movi_i64(count, op->immediate);
    } else {
        tcg_gen_mov_i64(count, ia64_gr_src(op->source2));
    }
    tcg_gen_movcond_i64(TCG_COND_GTU, clamped_count, count,
                        tcg_constant_i64(lane_bits),
                        tcg_constant_i64(lane_bits), count);

    if (ia64_gen_packed_vec_shift(ctx, result, ia64_gr_src(op->source1),
                                  clamped_count, lane_bits,
                                  INDEX_op_shls_vec)) {
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        if (op->immediate >= 0) {
            ia64_gen_gr_nat_from_1(insn, op->destination, op->source1);
        } else {
            ia64_gen_gr_nat_from_2(insn, op->destination,
                                   op->source1, op->source2);
        }
        return;
    }

    tcg_gen_movi_i64(result, 0);
    for (int i = 0; i < plan.count; i++) {
        TCGv_i64 lane = ia64_simd_extract_lane(
            ia64_gr_src(op->source1), &plan, i, false);

        tcg_gen_shl_i64(lane, lane, clamped_count);
        ia64_simd_insert_lane(result, lane, &plan, i);
    }

    tcg_gen_mov_i64(cpu_gr[op->destination], result);
    if (op->immediate >= 0) {
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source1);
    } else {
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
    }
}


IA64GenResult ia64_gen_simd(DisasContext *ctx,
                            const Ia64Instruction *insn)
{
    const IA64SimdOperands *op = &insn->operands.simd;

    switch (insn->opcode) {
    case IA64_OP_PADD1:
    case IA64_OP_PADD2:
    case IA64_OP_PADD4:
    case IA64_OP_PSUB1:
    case IA64_OP_PSUB2:
    case IA64_OP_PSUB4: {
        IA64SIMDLanePlan plan;

        if (insn->opcode == IA64_OP_PADD1 || insn->opcode == IA64_OP_PSUB1) {
            plan = ia64_simd_lane_plan(8);
        } else if (insn->opcode == IA64_OP_PADD2 ||
                   insn->opcode == IA64_OP_PSUB2) {
            plan = ia64_simd_lane_plan(16);
        } else {
            plan = ia64_simd_lane_plan(32);
        }
        bool is_sub = (insn->opcode == IA64_OP_PSUB1 ||
                       insn->opcode == IA64_OP_PSUB2 ||
                       insn->opcode == IA64_OP_PSUB4);
        const unsigned saturation = op->immediate & 3;
        TCGv_i64 result = tcg_temp_new_i64();

        if (op->destination == 0) {
            break;
        }
        if (saturation == 0) {
            ia64_gen_packed_wrap(result, ia64_gr_src(op->source1),
                                 ia64_gr_src(op->source2), plan.bits, is_sub);
        } else if ((saturation == 1 || saturation == 2) &&
                   ia64_gen_packed_vec_binary(
                       ctx, result, ia64_gr_src(op->source1),
                       ia64_gr_src(op->source2), plan.bits,
                       is_sub ?
                           (saturation == 1 ? INDEX_op_sssub_vec :
                                              INDEX_op_ussub_vec) :
                           (saturation == 1 ? INDEX_op_ssadd_vec :
                                              INDEX_op_usadd_vec),
                       TCG_COND_NEVER)) {
            /* Generated above. */
        } else {
            tcg_gen_movi_i64(result, 0);
            for (int i = 0; i < plan.count; ++i) {
                TCGv_i64 lane_a = ia64_simd_extract_lane(
                    ia64_gr_src(op->source1), &plan, i,
                    plan.bits < 32 && saturation == 1);
                TCGv_i64 lane_b = ia64_simd_extract_lane(
                    ia64_gr_src(op->source2), &plan, i,
                    plan.bits < 32 &&
                    (saturation == 1 || saturation == 3));
                TCGv_i64 lane_res = tcg_temp_new_i64();

                if (is_sub) {
                    tcg_gen_sub_i64(lane_res, lane_a, lane_b);
                } else {
                    tcg_gen_add_i64(lane_res, lane_a, lane_b);
                }
                if (plan.bits < 32) {
                    int64_t min_value = 0;
                    int64_t max_value = plan.mask;

                    if (saturation == 1) {
                        min_value = -(1LL << (plan.bits - 1));
                        max_value = (1LL << (plan.bits - 1)) - 1;
                    }

                    tcg_gen_smax_i64(lane_res, lane_res,
                                     tcg_constant_i64(min_value));
                    tcg_gen_smin_i64(lane_res, lane_res,
                                     tcg_constant_i64(max_value));
                }
                ia64_simd_insert_lane(result, lane_res, &plan, i);
            }
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PSHLADD2: {
        TCGv_i64 result = tcg_temp_new_i64();

        if (op->destination == 0) {
            break;
        }
        if (ia64_gen_pshladd2_vec(ctx, result, ia64_gr_src(op->source1),
                                  ia64_gr_src(op->source2), op->immediate)) {
            tcg_gen_mov_i64(cpu_gr[op->destination], result);
            ia64_gen_gr_nat_from_2(insn, op->destination,
                                   op->source1, op->source2);
            break;
        }
        gen_helper_simd_pshladd2(result, ia64_gr_src(op->source1),
                                 ia64_gr_src(op->source2),
                                 tcg_constant_i32(op->immediate));
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PSHRADD2: {
        const IA64SIMDLanePlan plan = ia64_simd_lane_plan(16);
        TCGv_i64 result = tcg_temp_new_i64();

        if (op->destination == 0) {
            break;
        }
        if (ia64_gen_pshradd2_vec(ctx, result, ia64_gr_src(op->source1),
                                  ia64_gr_src(op->source2), op->immediate)) {
            tcg_gen_mov_i64(cpu_gr[op->destination], result);
            ia64_gen_gr_nat_from_2(insn, op->destination,
                                   op->source1, op->source2);
            break;
        }
        tcg_gen_movi_i64(result, 0);
        for (int i = 0; i < plan.count; ++i) {
            TCGv_i64 lane_a = ia64_simd_extract_lane(
                ia64_gr_src(op->source1), &plan, i, true);
            TCGv_i64 lane_b = ia64_simd_extract_lane(
                ia64_gr_src(op->source2), &plan, i, true);
            TCGv_i64 lane_res = tcg_temp_new_i64();

            tcg_gen_sari_i64(lane_res, lane_a, op->immediate);
            tcg_gen_add_i64(lane_res, lane_res, lane_b);
            ia64_gen_saturate_signed_i64(lane_res, 16);
            ia64_simd_insert_lane(result, lane_res, &plan, i);
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PSHR2:
        ia64_gen_pshr(ctx, insn, 16, false);
        break;
    case IA64_OP_PSHR2_U:
        ia64_gen_pshr(ctx, insn, 16, true);
        break;
    case IA64_OP_PSHR4:
        ia64_gen_pshr(ctx, insn, 32, false);
        break;
    case IA64_OP_PSHR4_U:
        ia64_gen_pshr(ctx, insn, 32, true);
        break;
    case IA64_OP_PAVG1:
    case IA64_OP_PAVG2:
    case IA64_OP_PAVGSUB1:
    case IA64_OP_PAVGSUB2: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_PAVG1) {
            sel = op->immediate ? 4 : 0;
        } else if (insn->opcode == IA64_OP_PAVG2) {
            sel = op->immediate ? 5 : 1;
        } else if (insn->opcode == IA64_OP_PAVGSUB1) {
            sel = 2;
        } else {
            sel = 3;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        if (sel == 0 || sel == 1 || sel == 4 || sel == 5) {
            ia64_gen_packed_average(result, ia64_gr_src(op->source1),
                                    ia64_gr_src(op->source2),
                                    (sel & 1) ? 16 : 8, sel >= 4);
        } else {
            gen_helper_simd_pavg(result, tcg_constant_i32(sel),
                                 ia64_gr_src(op->source1),
                                 ia64_gr_src(op->source2));
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PCMP1_EQ:
    case IA64_OP_PCMP1_GT:
    case IA64_OP_PCMP2_EQ:
    case IA64_OP_PCMP2_GT:
    case IA64_OP_PCMP4_EQ:
    case IA64_OP_PCMP4_GT: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_PCMP1_EQ) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_PCMP1_GT) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_PCMP2_EQ) {
            sel = 2;
        } else if (insn->opcode == IA64_OP_PCMP2_GT) {
            sel = 3;
        } else if (insn->opcode == IA64_OP_PCMP4_EQ) {
            sel = 4;
        } else {
            sel = 5;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        if (ia64_gen_packed_vec_binary(
                ctx, result, ia64_gr_src(op->source1),
                ia64_gr_src(op->source2),
                sel <= 1 ? 8 : sel <= 3 ? 16 : 32,
                INDEX_op_cmp_vec,
                (sel & 1) ? TCG_COND_GT : TCG_COND_EQ)) {
            /* Generated above. */
        } else if ((sel & 1) == 0) {
            ia64_gen_packed_equal(result, ia64_gr_src(op->source1),
                                  ia64_gr_src(op->source2),
                                  sel == 0 ? 8 : sel == 2 ? 16 : 32);
        } else {
            gen_helper_simd_pcmp(result, tcg_constant_i32(sel),
                                 ia64_gr_src(op->source1),
                                 ia64_gr_src(op->source2));
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PMAX1_U:
    case IA64_OP_PMAX2:
    case IA64_OP_PMIN1_U:
    case IA64_OP_PMIN2: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_PMAX1_U) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_PMIN1_U) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_PMAX2) {
            sel = 2;
        } else {
            sel = 3;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        if (!ia64_gen_packed_vec_binary(
                ctx, result, ia64_gr_src(op->source1),
                ia64_gr_src(op->source2), sel <= 1 ? 8 : 16,
                sel == 0 ? INDEX_op_umax_vec :
                sel == 1 ? INDEX_op_umin_vec :
                sel == 2 ? INDEX_op_smax_vec : INDEX_op_smin_vec,
                TCG_COND_NEVER)) {
            gen_helper_simd_pminmax(result, tcg_constant_i32(sel),
                                    ia64_gr_src(op->source1),
                                    ia64_gr_src(op->source2));
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PMPY2_L:
    case IA64_OP_PMPY2_R:
    case IA64_OP_PMPYSH2:
    case IA64_OP_PMPYSH2_U: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_PMPY2_L) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_PMPY2_R) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_PMPYSH2) {
            sel = 2;
        } else {
            sel = 3;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        gen_helper_simd_pmpy(result, tcg_constant_i32(sel),
                             ia64_gr_src(op->source1),
                             ia64_gr_src(op->source2),
                             tcg_constant_i32(op->immediate));
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PSHL2:
        ia64_gen_pshl(ctx, insn, 16);
        break;
    case IA64_OP_PSHL4:
        ia64_gen_pshl(ctx, insn, 32);
        break;
    case IA64_OP_PSAD1: {
        TCGv_i64 result;

        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        gen_helper_simd_psad1(result, ia64_gr_src(op->source1),
                              ia64_gr_src(op->source2));
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_MUX1:
    case IA64_OP_MUX2: {
        TCGv_i64 result;

        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        if (insn->opcode == IA64_OP_MUX2) {
            ia64_gen_mux2(result, ia64_gr_src(op->source1), op->immediate);
        } else {
            ia64_gen_mux1(result, ia64_gr_src(op->source1), op->immediate);
        }
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source1);
        break;
    }
    case IA64_OP_MIX1_L:
    case IA64_OP_MIX1_R:
    case IA64_OP_MIX2_L:
    case IA64_OP_MIX2_R:
    case IA64_OP_MIX4_L:
    case IA64_OP_MIX4_R: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_MIX1_L) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_MIX1_R) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_MIX2_L) {
            sel = 2;
        } else if (insn->opcode == IA64_OP_MIX2_R) {
            sel = 3;
        } else if (insn->opcode == IA64_OP_MIX4_L) {
            sel = 4;
        } else {
            sel = 5;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        ia64_gen_simd_mix(result, ia64_gr_src(op->source1),
                          ia64_gr_src(op->source2), 8u << (sel >> 1),
                          (sel & 1) == 0);
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_PACK2_SSS:
    case IA64_OP_PACK2_USS:
    case IA64_OP_PACK4_SSS: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_PACK2_SSS) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_PACK2_USS) {
            sel = 1;
        } else {
            sel = 2;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        gen_helper_simd_pack(result, tcg_constant_i32(sel),
                             ia64_gr_src(op->source1),
                             ia64_gr_src(op->source2));
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_UNPACK1_H:
    case IA64_OP_UNPACK1_L:
    case IA64_OP_UNPACK2_H:
    case IA64_OP_UNPACK2_L:
    case IA64_OP_UNPACK4_H:
    case IA64_OP_UNPACK4_L: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_UNPACK1_H) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_UNPACK1_L) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_UNPACK2_H) {
            sel = 2;
        } else if (insn->opcode == IA64_OP_UNPACK2_L) {
            sel = 3;
        } else if (insn->opcode == IA64_OP_UNPACK4_H) {
            sel = 4;
        } else {
            sel = 5;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        ia64_gen_unpack(result, ia64_gr_src(op->source1),
                         ia64_gr_src(op->source2), 8U << (sel >> 1),
                         sel & 1);
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_SUM: {
        TCGv_i64 result;

        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        gen_helper_simd_sum(result, ia64_gr_src(op->source1),
                            ia64_gr_src(op->source2));
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_2(insn, op->destination,
                               op->source1, op->source2);
        break;
    }
    case IA64_OP_CZX1_L:
    case IA64_OP_CZX1_R:
    case IA64_OP_CZX2_L:
    case IA64_OP_CZX2_R: {
        uint32_t sel;
        TCGv_i64 result;

        if (insn->opcode == IA64_OP_CZX1_L) {
            sel = 0;
        } else if (insn->opcode == IA64_OP_CZX1_R) {
            sel = 1;
        } else if (insn->opcode == IA64_OP_CZX2_L) {
            sel = 2;
        } else {
            sel = 3;
        }
        if (op->destination == 0) {
            break;
        }
        result = tcg_temp_new_i64();
        ia64_gen_czx(result, ia64_gr_src(op->source2),
                     sel <= 1 ? 8 : 16, sel == 0 || sel == 2);
        tcg_gen_mov_i64(cpu_gr[op->destination], result);
        ia64_gen_gr_nat_from_1(insn, op->destination, op->source2);
        break;
    }
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
