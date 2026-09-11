/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 branch instruction generation.
 */

#include "qemu/osdep.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#include "target/ia64/translate/translate.h"

IA64GenResult ia64_gen_branch(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression)
{
    const IA64BranchOperands *op = &insn->operands.branch;
    const uint64_t next_ip = insn->address + 16;

    switch (insn->opcode) {
    case IA64_OP_BR_COND:
    case IA64_OP_BRL_COND:
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_DIRECT,
                .direct_target = insn->address + op->displacement,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
            });
    case IA64_OP_BR_INDIRECT: {
        TCGv_i64 target = tcg_temp_new_i64();

        tcg_gen_andi_i64(target, cpu_br[op->target], IA64_IP_BUNDLE_MASK);
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_TCG,
                .tcg_target = target,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
            });
    }
    case IA64_OP_BR_CLOOP: {
        uint64_t target = insn->address + op->displacement;
        TCGv_i64 lc = tcg_temp_new_i64();
        TCGLabel *l_nobr = gen_new_label();

        tcg_gen_ld_i64(lc, tcg_env,
                       offsetof(CPUIA64State, ar[IA64_AR_LC]));
        tcg_gen_brcondi_i64(TCG_COND_EQ, lc, 0, l_nobr);
        if (ia64_gen_fill_st1_cloop(ctx, insn, target, l_nobr, record_iipa,
                                    track_psr_suppression)) {
            gen_set_label(l_nobr);
            return IA64_GEN_CONTINUE;
        }
        tcg_gen_subi_i64(lc, lc, 1);
        tcg_gen_st_i64(lc, tcg_env,
                       offsetof(CPUIA64State, ar[IA64_AR_LC]));
        if (ia64_gen_self_counted_loop(ctx, target, insn->address,
                                       record_iipa,
                                       track_psr_suppression)) {
            gen_set_label(l_nobr);
            return IA64_GEN_CONTINUE;
        }
        ia64_gen_goto_completed(ctx, target, insn->address, record_iipa,
                                track_psr_suppression);
        gen_set_label(l_nobr);
        return IA64_GEN_CONTINUE;
    }
    case IA64_OP_BR_IA:
        gen_helper_br_ia(tcg_env, tcg_constant_i32(op->target),
                         tcg_constant_i64(insn->address),
                         tcg_constant_i32(insn->slot));
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_CURRENT,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
                .skip_native_traps = true,
            });
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL: {
        const uint64_t target = insn->address + op->displacement;

        tcg_gen_movi_i64(cpu_ip, insn->address);
        gen_helper_br_call_rse(tcg_env, tcg_constant_i32(op->link),
                               tcg_constant_i64(next_ip),
                               tcg_constant_i64(target));
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_DIRECT,
                .direct_target = target,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
            });
    }
    case IA64_OP_BR_CALL_INDIRECT: {
        TCGv_i64 target = tcg_temp_new_i64();

        tcg_gen_mov_i64(target, cpu_br[op->target]);
        tcg_gen_movi_i64(cpu_ip, insn->address);
        gen_helper_br_call_rse(tcg_env, tcg_constant_i32(op->link),
                               tcg_constant_i64(next_ip), target);
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_CURRENT,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
            });
    }
    case IA64_OP_BR_RET: {
        tcg_gen_movi_i64(cpu_ip, insn->address);
        gen_helper_br_ret_rse(tcg_env, tcg_constant_i32(op->target),
                              tcg_constant_i64(insn->address),
                              tcg_constant_i32(insn->slot));
        return ia64_gen_complete_branch(
            ctx, skip, &(IA64BranchCompletion) {
                .target_kind = IA64_BRANCH_TARGET_CURRENT,
                .completed_ip = insn->address,
                .record_iipa = record_iipa,
                .track_psr_suppression = track_psr_suppression,
                .skip_native_traps = true,
            });
    }
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CTOP: {
        TCGv_i32 taken = tcg_temp_new_i32();
        TCGLabel *l_nobr = gen_new_label();
        uint64_t static_target = insn->address + op->displacement;

        if (insn->opcode == IA64_OP_BR_WEXIT) {
            gen_helper_br_wexit(taken, tcg_env,
                                tcg_constant_i32(insn->qp));
        } else if (insn->opcode == IA64_OP_BR_WTOP) {
            gen_helper_br_wtop(taken, tcg_env,
                               tcg_constant_i32(insn->qp));
        } else if (insn->opcode == IA64_OP_BR_CEXIT) {
            gen_helper_br_cexit(taken, tcg_env);
        } else {
            gen_helper_br_ctop(taken, tcg_env);
        }
        tcg_gen_brcondi_i32(TCG_COND_EQ, taken, 0, l_nobr);
        if (insn->opcode == IA64_OP_BR_CTOP &&
            ia64_gen_self_counted_loop(ctx, static_target, insn->address,
                                       record_iipa,
                                       track_psr_suppression)) {
            gen_set_label(l_nobr);
            break;
        }
        ia64_gen_goto_completed(ctx, static_target, insn->address,
                                record_iipa, track_psr_suppression);
        gen_set_label(l_nobr);
        break;
    }
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
