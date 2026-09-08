/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 floating-point instruction generation.
 */

#include "qemu/osdep.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#include "target/ia64/translate/translate.h"

static TCGv_i32 ia64_fp_context(const Ia64Instruction *insn)
{
    const IA64FloatingOperands *op = &insn->operands.floating;

    return tcg_constant_i32(IA64_FP_CONTEXT(op->status_field, op->precision));
}

typedef enum IA64FPCopyMode {
    IA64_FP_COPY,
    IA64_FP_COPY_ABS,
    IA64_FP_COPY_NEG,
    IA64_FP_COPY_NEG_ABS,
} IA64FPCopyMode;

static void ia64_gen_fp_copy(const Ia64Instruction *insn,
                             IA64FPCopyMode mode)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 value;
    TCGv_i64 exponent;
    TCGLabel *slow = NULL;
    TCGLabel *significand = NULL;
    TCGLabel *done = NULL;

    if (op->destination <= 1) {
        return;
    }

    if (op->source1 > 1) {
        slow = gen_new_label();
        done = gen_new_label();
        if (mode == IA64_FP_COPY) {
            significand = gen_new_label();
            tcg_gen_brcondi_i64(TCG_COND_NE,
                                ia64_gen_fr_fmov_slow_read(op->source1),
                                0, slow);
            tcg_gen_brcondi_i64(TCG_COND_NE,
                                ia64_gen_fr_sig_read(op->source1),
                                0, significand);
        } else {
            tcg_gen_brcondi_i64(TCG_COND_NE,
                                ia64_gen_fr_special_read(op->source1),
                                0, slow);
        }
    }

    value = tcg_temp_new_i64();
    switch (mode) {
    case IA64_FP_COPY:
        tcg_gen_mov_i64(value, ia64_fr_binary_src(op->source1));
        break;
    case IA64_FP_COPY_ABS:
    case IA64_FP_COPY_NEG:
    case IA64_FP_COPY_NEG_ABS:
        if (op->source1 <= 1) {
            tcg_gen_movi_i64(value, op->source1 == IA64_FR_ONE_INDEX ?
                            UINT64_C(1) << 63 : 0);
        } else {
            /*
             * The packed sign pseudo-ops are fpmerge operations over the
             * architected significand, not scalar operations over the
             * binary64 cache.  Reconstruct that significand for the fast
             * representation; tagged register formats take the helper path.
             */
            tcg_gen_andi_i64(value, ia64_fr_binary_src(op->source1),
                             UINT64_C(0x000fffffffffffff));
            tcg_gen_shli_i64(value, value, 11);
            exponent = tcg_temp_new_i64();
            tcg_gen_andi_i64(exponent, ia64_fr_binary_src(op->source1),
                             UINT64_C(0x7ff0000000000000));
            tcg_gen_setcondi_i64(TCG_COND_NE, exponent, exponent, 0);
            tcg_gen_shli_i64(exponent, exponent, 63);
            tcg_gen_or_i64(value, value, exponent);
        }
        if (mode == IA64_FP_COPY_ABS) {
            tcg_gen_andi_i64(value, value,
                             UINT64_C(0x7fffffff7fffffff));
        } else if (mode == IA64_FP_COPY_NEG) {
            tcg_gen_xori_i64(value, value,
                             UINT64_C(0x8000000080000000));
        } else {
            tcg_gen_ori_i64(value, value,
                            UINT64_C(0x8000000080000000));
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (mode == IA64_FP_COPY) {
        ia64_gen_fr_mov(op->destination, value);
    } else {
        ia64_gen_fr_mov_sig(op->destination, value);
    }
    if (op->source1 <= 1) {
        return;
    }
    tcg_gen_br(done);

    if (significand) {
        gen_set_label(significand);
        ia64_gen_fr_mov_sig(op->destination,
                            ia64_fr_significand_src(op->source1));
        tcg_gen_br(done);
    }

    gen_set_label(slow);
    switch (mode) {
    case IA64_FP_COPY:
        gen_helper_fmov(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1));
        break;
    case IA64_FP_COPY_ABS:
        gen_helper_fpabs(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1));
        break;
    case IA64_FP_COPY_NEG:
        gen_helper_fpneg(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1));
        break;
    case IA64_FP_COPY_NEG_ABS:
        gen_helper_fpnegabs(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1));
        break;
    default:
        g_assert_not_reached();
    }
    gen_set_label(done);
}

/* The caller has excluded NaTVal, extended and orphan integer formats. */
static TCGv_i64 ia64_gen_fp_significand(uint8_t reg)
{
    TCGv_i64 value, exponent;

    value = tcg_temp_new_i64();
    if (reg <= 1) {
        tcg_gen_mov_i64(value, ia64_fr_significand_src(reg));
        return value;
    }
    exponent = tcg_temp_new_i64();
    tcg_gen_andi_i64(value, ia64_fr_binary_src(reg),
                     UINT64_C(0x000fffffffffffff));
    tcg_gen_shli_i64(value, value, 11);
    tcg_gen_andi_i64(exponent, ia64_fr_binary_src(reg),
                     UINT64_C(0x7ff0000000000000));
    tcg_gen_setcondi_i64(TCG_COND_NE, exponent, exponent, 0);
    tcg_gen_shli_i64(exponent, exponent, 63);
    tcg_gen_or_i64(value, value, exponent);
    tcg_gen_movcond_i64(TCG_COND_NE, value, ia64_gen_fr_sig_read(reg),
                        tcg_constant_i64(0), ia64_fr_significand_src(reg),
                        value);
    return value;
}

static void ia64_gen_fp_bitwise_operands(const IA64FloatingOperands *op,
                                          TCGLabel *slow, TCGv_i64 *left,
                                          TCGv_i64 *right)
{
    TCGv_i64 special = tcg_temp_new_i64();

    tcg_gen_or_i64(special, ia64_gen_fr_fmov_slow_read(op->source1),
                   ia64_gen_fr_fmov_slow_read(op->source2));
    tcg_gen_brcondi_i64(TCG_COND_NE, special, 0, slow);
    *left = ia64_gen_fp_significand(op->source1);
    *right = op->source1 == op->source2 ? *left :
             ia64_gen_fp_significand(op->source2);
}

static void ia64_gen_fp_logical(const Ia64Instruction *insn, uint32_t mode)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 left, right, value;
    TCGLabel *slow, *done;

    if (op->destination <= 1) {
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    value = tcg_temp_new_i64();
    ia64_gen_fp_bitwise_operands(op, slow, &left, &right);
    switch (mode) {
    case 0:
        tcg_gen_and_i64(value, left, right);
        break;
    case 1:
        tcg_gen_andc_i64(value, left, right);
        break;
    case 2:
        tcg_gen_or_i64(value, left, right);
        break;
    case 3:
        tcg_gen_xor_i64(value, left, right);
        break;
    default:
        g_assert_not_reached();
    }
    ia64_gen_fr_mov_sig(op->destination, value);
    tcg_gen_br(done);
    gen_set_label(slow);

    /* Helpers read the architected significand for every FR representation. */
    switch (mode) {
    case 0:
        gen_helper_flogical_and(tcg_env,
                                tcg_constant_i32(op->destination),
                                tcg_constant_i32(op->source1),
                                tcg_constant_i32(op->source2));
        break;
    case 1:
        gen_helper_flogical_andcm(tcg_env,
                                  tcg_constant_i32(op->destination),
                                  tcg_constant_i32(op->source1),
                                  tcg_constant_i32(op->source2));
        break;
    case 2:
        gen_helper_flogical_or(tcg_env,
                               tcg_constant_i32(op->destination),
                               tcg_constant_i32(op->source1),
                               tcg_constant_i32(op->source2));
        break;
    case 3:
        gen_helper_flogical_xor(tcg_env,
                                tcg_constant_i32(op->destination),
                                tcg_constant_i32(op->source1),
                                tcg_constant_i32(op->source2));
        break;
    default:
        g_assert_not_reached();
    }
    gen_set_label(done);
}

static void ia64_gen_fp_swap(const Ia64Instruction *insn, uint32_t form)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 left, right, value;
    TCGLabel *slow, *done;

    if (op->destination <= 1) {
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    value = tcg_temp_new_i64();
    ia64_gen_fp_bitwise_operands(op, slow, &left, &right);
    tcg_gen_shri_i64(value, left, 32);
    tcg_gen_deposit_i64(value, value, right, 32, 32);
    if (form != 0) {
        tcg_gen_xori_i64(value, value, UINT64_C(1) << (form == 1 ? 63 : 31));
    }
    ia64_gen_fr_mov_sig(op->destination, value);
    tcg_gen_br(done);
    gen_set_label(slow);

    gen_helper_fswap(tcg_env, tcg_constant_i32(op->destination),
                     tcg_constant_i32(op->source1),
                     tcg_constant_i32(op->source2),
                     tcg_constant_i32(form));
    gen_set_label(done);
}

static void ia64_gen_fp_mix(const Ia64Instruction *insn, uint32_t form)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 left, right, value;
    TCGLabel *slow, *done;

    if (op->destination <= 1) {
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    value = tcg_temp_new_i64();
    ia64_gen_fp_bitwise_operands(op, slow, &left, &right);
    if (form == 2) {
        tcg_gen_shri_i64(value, right, 32);
    } else {
        tcg_gen_ext32u_i64(value, right);
    }
    if (form == 1) {
        tcg_gen_deposit_i64(value, value, left, 32, 32);
    } else {
        tcg_gen_andi_i64(left, left, UINT64_C(0xffffffff00000000));
        tcg_gen_or_i64(value, value, left);
    }
    ia64_gen_fr_mov_sig(op->destination, value);
    tcg_gen_br(done);
    gen_set_label(slow);

    gen_helper_fmix(tcg_env, tcg_constant_i32(op->destination),
                    tcg_constant_i32(op->source1),
                    tcg_constant_i32(op->source2),
                    tcg_constant_i32(form));
    gen_set_label(done);
}

static void ia64_gen_fp_sxt(const Ia64Instruction *insn, uint32_t form)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 left, right, value;
    TCGLabel *slow, *done;

    if (op->destination <= 1) {
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    value = tcg_temp_new_i64();
    ia64_gen_fp_bitwise_operands(op, slow, &left, &right);
    if (form == 1) {
        tcg_gen_shri_i64(value, right, 32);
        tcg_gen_sari_i64(left, left, 63);
    } else {
        tcg_gen_ext32u_i64(value, right);
        tcg_gen_sextract_i64(left, left, 31, 1);
    }
    tcg_gen_deposit_i64(value, value, left, 32, 32);
    ia64_gen_fr_mov_sig(op->destination, value);
    tcg_gen_br(done);
    gen_set_label(slow);

    gen_helper_fsxt(tcg_env, tcg_constant_i32(op->destination),
                    tcg_constant_i32(op->source1),
                    tcg_constant_i32(op->source2),
                    tcg_constant_i32(form));
    gen_set_label(done);
}

static void ia64_gen_fp_parallel_merge(const Ia64Instruction *insn,
                                       uint32_t form)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 left, right, value;
    TCGLabel *slow, *done;

    if (op->destination <= 1) {
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    value = tcg_temp_new_i64();
    ia64_gen_fp_bitwise_operands(op, slow, &left, &right);
    uint64_t mask = form == 2 ? UINT64_C(0xff800000ff800000) :
                                UINT64_C(0x8000000080000000);

    tcg_gen_andi_i64(value, left, mask);
    if (form == 0) {
        tcg_gen_xori_i64(value, value, mask);
    }
    tcg_gen_andi_i64(right, right, ~mask);
    tcg_gen_or_i64(value, value, right);
    ia64_gen_fr_mov_sig(op->destination, value);
    tcg_gen_br(done);
    gen_set_label(slow);

    gen_helper_fpmerge(tcg_env, tcg_constant_i32(op->destination),
                       tcg_constant_i32(op->source1),
                       tcg_constant_i32(op->source2),
                       tcg_constant_i32(form));
    gen_set_label(done);
}

static void ia64_gen_fmerge_helper(const IA64FloatingOperands *op,
                                   uint32_t form)
{
    if (form == 0) {
        gen_helper_fmerge_ns(tcg_env, tcg_constant_i32(op->destination),
                             tcg_constant_i32(op->source1),
                             tcg_constant_i32(op->source2));
    } else if (form == 1) {
        gen_helper_fmerge_s(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1),
                            tcg_constant_i32(op->source2));
    } else {
        gen_helper_fmerge_se(tcg_env, tcg_constant_i32(op->destination),
                             tcg_constant_i32(op->source1),
                             tcg_constant_i32(op->source2));
    }
}

static void ia64_gen_fmerge_require_binary_normal(uint8_t reg,
                                                  TCGLabel *slow)
{
    const uint64_t exp_mask = UINT64_C(0x7ff0000000000000);
    TCGv_i64 exponent;

    if (reg == IA64_FR_ONE_INDEX) {
        return;
    }
    g_assert(reg > IA64_FR_ONE_INDEX);
    exponent = tcg_temp_new_i64();
    tcg_gen_andi_i64(exponent, ia64_fr_binary_src(reg), exp_mask);
    tcg_gen_brcondi_i64(TCG_COND_EQ, exponent, 0, slow);
    tcg_gen_brcondi_i64(TCG_COND_EQ, exponent, exp_mask, slow);
}

static void ia64_gen_fmerge(const Ia64Instruction *insn, uint32_t form)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    TCGv_i64 special;
    TCGv_i64 left;
    TCGv_i64 right;
    TCGv_i64 result;
    TCGLabel *slow;
    TCGLabel *done;
    uint64_t left_mask;

    if (op->destination <= 1) {
        return;
    }

    /*
     * fmerge.se combines the IA-64 register-format sign/exponent and
     * significand fields.  IEEE binary64 bit splicing is equivalent only
     * when both untagged operands are normal.  A fixed f0 operand is known
     * not to meet that condition, so avoid emitting an unreachable fast
     * path in that case.
     */
    if (form == 2 &&
        (op->source1 == IA64_FR_ZERO_INDEX ||
         op->source2 == IA64_FR_ZERO_INDEX)) {
        ia64_gen_fmerge_helper(op, form);
        return;
    }

    slow = NULL;
    done = NULL;
    if (op->source1 > 1 || op->source2 > 1) {
        slow = gen_new_label();
        done = gen_new_label();
        if (op->source1 <= 1) {
            special = ia64_gen_fr_special_read(op->source2);
        } else if (op->source2 <= 1) {
            special = ia64_gen_fr_special_read(op->source1);
        } else {
            special = tcg_temp_new_i64();
            tcg_gen_or_i64(special,
                           ia64_gen_fr_special_read(op->source1),
                           ia64_gen_fr_special_read(op->source2));
        }
        tcg_gen_brcondi_i64(TCG_COND_NE, special, 0, slow);
        if (form == 2) {
            ia64_gen_fmerge_require_binary_normal(op->source1, slow);
            ia64_gen_fmerge_require_binary_normal(op->source2, slow);
        }
    }

    left_mask = form == 2 ? UINT64_C(0xfff0000000000000) :
                            UINT64_C(0x8000000000000000);
    left = tcg_temp_new_i64();
    right = tcg_temp_new_i64();
    result = tcg_temp_new_i64();
    if (form == 0) {
        tcg_gen_not_i64(left, ia64_fr_binary_src(op->source1));
        tcg_gen_andi_i64(left, left, left_mask);
    } else {
        tcg_gen_andi_i64(left, ia64_fr_binary_src(op->source1), left_mask);
    }
    tcg_gen_andi_i64(right, ia64_fr_binary_src(op->source2), ~left_mask);
    tcg_gen_or_i64(result, left, right);
    ia64_gen_fr_mov(op->destination, result);
    if (op->source1 <= 1 && op->source2 <= 1) {
        return;
    }
    tcg_gen_br(done);

    gen_set_label(slow);
    ia64_gen_fmerge_helper(op, form);
    gen_set_label(done);
}

static uint64_t ia64_fixed_fr_getf_value(uint8_t reg, uint32_t kind)
{
    g_assert(reg <= 1);

    if (reg == IA64_FR_ZERO_INDEX) {
        return 0;
    }
    switch (kind) {
    case 0:
        return IA64_FR_ONE;
    case 1:
        return UINT32_C(0x3f800000);
    case 2:
        return UINT64_C(1) << 63;
    case 3:
        return UINT64_C(0xffff);
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_getf(const Ia64Instruction *insn, uint32_t kind)
{
    const IA64FloatingOperands *op = &insn->operands.floating;

    if (op->destination == 0) {
        return;
    }
    if (op->source1 <= 1) {
        ia64_gen_gr_write_nat_clear(
            insn, op->destination,
            tcg_constant_i64(ia64_fixed_fr_getf_value(op->source1, kind)));
        return;
    }

    if (kind == 2) {
        TCGLabel *slow = gen_new_label();
        TCGLabel *done = gen_new_label();

        tcg_gen_brcondi_i64(TCG_COND_EQ,
                            ia64_gen_fr_sig_read(op->source1), 0, slow);
        tcg_gen_mov_i64(cpu_gr[op->destination],
                        ia64_fr_significand_src(op->source1));
        tcg_gen_br(done);
        gen_set_label(slow);
        gen_helper_getf(cpu_gr[op->destination], tcg_env,
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(kind));
        gen_set_label(done);
    } else {
        gen_helper_getf(cpu_gr[op->destination], tcg_env,
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(kind));
    }
    ia64_gen_gr_nat_assign(insn, op->destination,
                           ia64_gen_fr_nat_read(op->source1));
}

static void ia64_gen_xma(const Ia64Instruction *insn, uint32_t mode)
{
    const IA64FloatingOperands *op = &insn->operands.floating;
    uint64_t sig_mask[2] = { 0, 0 };
    TCGv_i64 low = tcg_temp_new_i64();
    TCGv_i64 high = tcg_temp_new_i64();
    TCGv_i64 missing = NULL;
    TCGLabel *fast = NULL;
    TCGLabel *done = NULL;

    if (op->source2 > 1) {
        sig_mask[op->source2 / 64] |= 1ULL << (op->source2 % 64);
    }
    if (op->auxiliary1 > 1) {
        sig_mask[op->auxiliary1 / 64] |=
            1ULL << (op->auxiliary1 % 64);
    }
    if (mode != 3) {
        if (op->source1 > 1) {
            sig_mask[op->source1 / 64] |=
                1ULL << (op->source1 % 64);
        }
    }
    for (uint32_t word = 0; word < ARRAY_SIZE(sig_mask); word++) {
        if (sig_mask[word] != 0) {
            TCGv_i64 bits = ia64_gen_fr_sig_mask_read(word,
                                                       sig_mask[word]);

            /* A significand tag and a NaTVal tag are mutually exclusive. */
            tcg_gen_xori_i64(bits, bits, sig_mask[word]);
            if (missing == NULL) {
                missing = bits;
            } else {
                tcg_gen_or_i64(missing, missing, bits);
            }
        }
    }

    if (missing != NULL) {
        fast = gen_new_label();
        done = gen_new_label();
        tcg_gen_brcondi_i64(TCG_COND_EQ, missing, 0, fast);
        gen_helper_xma(tcg_env, tcg_constant_i32(op->destination),
                       tcg_constant_i32(mode == 3 ? 0 : op->source1),
                       tcg_constant_i32(op->source2),
                       tcg_constant_i32(op->auxiliary1),
                       tcg_constant_i32(mode));
        tcg_gen_br(done);
        gen_set_label(fast);
    }

    if (mode == 1) {
        tcg_gen_muls2_i64(low, high, ia64_fr_significand_src(op->source2),
                          ia64_fr_significand_src(op->auxiliary1));
    } else {
        tcg_gen_mulu2_i64(low, high, ia64_fr_significand_src(op->source2),
                          ia64_fr_significand_src(op->auxiliary1));
    }
    if (mode != 3) {
        tcg_gen_add2_i64(low, high, low, high,
                         ia64_fr_significand_src(op->source1),
                         tcg_constant_i64(0));
    }
    ia64_gen_fr_mov_sig(op->destination, mode == 0 ? low : high);
    if (done != NULL) {
        gen_set_label(done);
    }
}

IA64GenResult ia64_gen_fp(DisasContext *ctx,
                          const Ia64Instruction *insn)
{
    const IA64FloatingOperands *op = &insn->operands.floating;

    switch (insn->opcode) {
    case IA64_OP_FADD:
        gen_helper_fadd(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FSUB:
        gen_helper_fsub(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FMPY:
        gen_helper_fmpy(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FMA:
        gen_helper_fma4(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2),
                        tcg_constant_i32(op->auxiliary1),
                        ia64_fp_context(insn));
        break;
    case IA64_OP_XMA_L:
        ia64_gen_xma(insn, 0);
        break;
    case IA64_OP_XMA_H:
        ia64_gen_xma(insn, 1);
        break;
    case IA64_OP_XMA_HU:
        ia64_gen_xma(insn, 2);
        break;
    case IA64_OP_XMPY_HU:
        ia64_gen_xma(insn, 3);
        break;
    case IA64_OP_FMIN:
        gen_helper_fmin(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FMAX:
        gen_helper_fmax(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FAMIN:
        gen_helper_famin(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FAMAX:
        gen_helper_famax(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FRCPA:
        gen_helper_frcpa(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->auxiliary1),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FPRCPA:
        gen_helper_fprcpa(tcg_env, tcg_constant_i32(op->destination),
                          tcg_constant_i32(op->auxiliary2),
                          tcg_constant_i32(op->source1),
                          tcg_constant_i32(op->source2),
                          ia64_fp_context(insn));
        break;
    case IA64_OP_FCMP:
        gen_helper_fcmp(tcg_env,
                        tcg_constant_i32(op->auxiliary1),
                        tcg_constant_i32(op->auxiliary2),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2),
                        tcg_constant_i32(op->immediate), ia64_fp_context(insn));
        break;
    case IA64_OP_FMS:
        gen_helper_fms(tcg_env, tcg_constant_i32(op->destination),
                       tcg_constant_i32(op->source1),
                       tcg_constant_i32(op->source2),
                       tcg_constant_i32(op->auxiliary1), ia64_fp_context(insn));
        break;
    case IA64_OP_FNMA:
        gen_helper_fnma4(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2),
                         tcg_constant_i32(op->auxiliary1),
                         ia64_fp_context(insn));
        break;
    case IA64_OP_FSELECT:
        gen_helper_fselect(tcg_env, tcg_constant_i32(op->destination),
                           tcg_constant_i32(op->source1),
                           tcg_constant_i32(op->source2),
                           tcg_constant_i32(op->auxiliary1));
        break;
    case IA64_OP_FNORM:
        gen_helper_fnorm(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2), ia64_fp_context(insn));
        break;
    case IA64_OP_FPABS:
        ia64_gen_fp_copy(insn, IA64_FP_COPY_ABS);
        break;
    case IA64_OP_FPNEG:
        ia64_gen_fp_copy(insn, IA64_FP_COPY_NEG);
        break;
    case IA64_OP_FPNEGABS:
        ia64_gen_fp_copy(insn, IA64_FP_COPY_NEG_ABS);
        break;
    case IA64_OP_FPRSQRTA:
        gen_helper_fprsqrta(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->auxiliary2),
                            tcg_constant_i32(op->source2),
                            ia64_fp_context(insn));
        break;
    case IA64_OP_FRSQRTA:
        gen_helper_frsqrta(tcg_env, tcg_constant_i32(op->destination),
                           tcg_constant_i32(op->auxiliary2),
                           tcg_constant_i32(op->source2),
                           ia64_fp_context(insn));
        break;
    case IA64_OP_FPACK:
        gen_helper_fpack(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2));
        break;
    case IA64_OP_FAND:
        ia64_gen_fp_logical(insn, 0);
        break;
    case IA64_OP_FANDCM:
        ia64_gen_fp_logical(insn, 1);
        break;
    case IA64_OP_FOR:
        ia64_gen_fp_logical(insn, 2);
        break;
    case IA64_OP_FXOR:
        ia64_gen_fp_logical(insn, 3);
        break;
    case IA64_OP_FSWAP:
        ia64_gen_fp_swap(insn, 0);
        break;
    case IA64_OP_FSWAP_NL:
        ia64_gen_fp_swap(insn, 1);
        break;
    case IA64_OP_FSWAP_NR:
        ia64_gen_fp_swap(insn, 2);
        break;
    case IA64_OP_FMIX_LR:
        ia64_gen_fp_mix(insn, 0);
        break;
    case IA64_OP_FMIX_R:
        ia64_gen_fp_mix(insn, 1);
        break;
    case IA64_OP_FMIX_L:
        ia64_gen_fp_mix(insn, 2);
        break;
    case IA64_OP_FSXT_R:
        ia64_gen_fp_sxt(insn, 0);
        break;
    case IA64_OP_FSXT_L:
        ia64_gen_fp_sxt(insn, 1);
        break;
    case IA64_OP_FPMERGE:
        ia64_gen_fp_parallel_merge(insn, 0);
        break;
    case IA64_OP_FPMERGE_S:
        ia64_gen_fp_parallel_merge(insn, 1);
        break;
    case IA64_OP_FPMERGE_SE:
        ia64_gen_fp_parallel_merge(insn, 2);
        break;
    case IA64_OP_FPMIN:
        gen_helper_fpminmax(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1),
                            tcg_constant_i32(op->source2),
                            tcg_constant_i32(0),
                            tcg_constant_i32(0),
                            ia64_fp_context(insn));
        break;
    case IA64_OP_FPMAX:
        gen_helper_fpminmax(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1),
                            tcg_constant_i32(op->source2),
                            tcg_constant_i32(1),
                            tcg_constant_i32(0),
                            ia64_fp_context(insn));
        break;
    case IA64_OP_FPAMIN:
        gen_helper_fpminmax(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1),
                            tcg_constant_i32(op->source2),
                            tcg_constant_i32(0),
                            tcg_constant_i32(1),
                            ia64_fp_context(insn));
        break;
    case IA64_OP_FPAMAX:
        gen_helper_fpminmax(tcg_env, tcg_constant_i32(op->destination),
                            tcg_constant_i32(op->source1),
                            tcg_constant_i32(op->source2),
                            tcg_constant_i32(1),
                            tcg_constant_i32(1),
                            ia64_fp_context(insn));
        break;
    case IA64_OP_FPCMP:
        gen_helper_fpcmp(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->source2),
                         tcg_constant_i32(op->immediate),
                         ia64_fp_context(insn));
        break;
    case IA64_OP_FPCVT:
        gen_helper_fpcvt(tcg_env, tcg_constant_i32(op->destination),
                         tcg_constant_i32(op->source1),
                         tcg_constant_i32(op->immediate & 1),
                         tcg_constant_i32((op->immediate >> 1) & 1),
                         ia64_fp_context(insn));
        break;
    case IA64_OP_FPMA:
        gen_helper_fpma(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2),
                        tcg_constant_i32(op->auxiliary1),
                        tcg_constant_i32(0),
                        ia64_fp_context(insn));
        break;
    case IA64_OP_FPMS:
        gen_helper_fpma(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2),
                        tcg_constant_i32(op->auxiliary1),
                        tcg_constant_i32(1),
                        ia64_fp_context(insn));
        break;
    case IA64_OP_FPNMA:
        gen_helper_fpma(tcg_env, tcg_constant_i32(op->destination),
                        tcg_constant_i32(op->source1),
                        tcg_constant_i32(op->source2),
                        tcg_constant_i32(op->auxiliary1),
                        tcg_constant_i32(2),
                        ia64_fp_context(insn));
        break;
    case IA64_OP_FMOV:
        ia64_gen_fp_copy(insn, IA64_FP_COPY);
        break;
    case IA64_OP_FCVT_XF:
        gen_helper_fcvt_xf(tcg_env, tcg_constant_i32(op->destination),
                           tcg_constant_i32(op->source1));
        break;
    case IA64_OP_FCVT_FX:
    case IA64_OP_FCVT_FXU:
        gen_helper_fcvt_fx(tcg_env, tcg_constant_i32(op->destination),
                           tcg_constant_i32(op->source1),
                           tcg_constant_i32(insn->opcode == IA64_OP_FCVT_FXU),
                           tcg_constant_i32((op->immediate >> 1) & 1),
                           ia64_fp_context(insn));
        break;
    case IA64_OP_GETF_D:
        ia64_gen_getf(insn, 0);
        break;
    case IA64_OP_GETF_S:
        ia64_gen_getf(insn, 1);
        break;
    case IA64_OP_GETF_SIG:
        ia64_gen_getf(insn, 2);
        break;
    case IA64_OP_GETF_EXP:
        ia64_gen_getf(insn, 3);
        break;
    case IA64_OP_SETF_D:
        ia64_gen_fr_mov(op->destination, ia64_gr_src(op->source1));
        ia64_gen_fr_nat_from_gr(op->destination, op->source1);
        break;
    case IA64_OP_SETF_S: {
        gen_helper_setf_s(tcg_env, tcg_constant_i32(op->destination),
                          ia64_gr_src(op->source1));
        ia64_gen_fr_nat_from_gr(op->destination, op->source1);
        break;
    }
    case IA64_OP_SETF_EXP:
        gen_helper_setf_exp(tcg_env, tcg_constant_i32(op->destination),
                            ia64_gr_src(op->source1));
        ia64_gen_fr_nat_from_gr(op->destination, op->source1);
        break;
    case IA64_OP_SETF_SIG:
        ia64_gen_fr_mov_sig(op->destination, ia64_gr_src(op->source1));
        ia64_gen_fr_nat_from_gr(op->destination, op->source1);
        break;
    case IA64_OP_FCLASS:
        gen_helper_fclass(tcg_env, tcg_constant_i32(op->auxiliary1),
                          tcg_constant_i32(op->auxiliary2),
                          tcg_constant_i32(op->source1),
                          tcg_constant_i32(op->immediate));
        break;
    case IA64_OP_FMERGE:
        ia64_gen_fmerge(insn, 0);
        break;
    case IA64_OP_FMERGE_S:
        ia64_gen_fmerge(insn, 1);
        break;
    case IA64_OP_FMERGE_SE:
        ia64_gen_fmerge(insn, 2);
        break;
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
