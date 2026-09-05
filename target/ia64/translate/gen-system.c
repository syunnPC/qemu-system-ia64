/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 system register, RSE and translation-control generation.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#include "target/ia64/translate/translate.h"

static void ia64_request_exit_after_bundle(DisasContext *ctx)
{
    /*
     * A predicate proven false makes the state-changing operation
     * unreachable.  Do not shorten the TB merely because its dead path
     * contains an operation that would otherwise require a new TB key.
     */
    if (!ctx->reg.current_qp_known || ctx->reg.current_qp_value) {
        ctx->restart.exit_after_bundle = true;
    }
}

static bool ia64_cr_write_requires_tb_exit(uint32_t cr_num)
{
    switch (cr_num) {
    case IA64_CR_ITM:
    case IA64_CR_IVA:
    case IA64_CR_PTA:
    case IA64_CR_SAPIC_TPR:
    case IA64_CR_SAPIC_EOI:
    case IA64_CR_ITV:
    case IA64_CR_CMCV:
        /*
         * These writes reschedule interrupts, change firmware instruction
         * identity, or flush translation state.  Return to the CPU loop at
         * the bundle boundary so those effects are observed immediately.
         */
        return true;
    default:
        /*
         * The remaining CRs are plain state consumed dynamically by later
         * helpers (or by rfi, which exits on its own).
         */
        return false;
    }
}

static void ia64_gen_cr_read(TCGv_i64 value, uint32_t cr_num)
{
    switch (cr_num) {
    case IA64_CR_SAPIC_LID:
    case IA64_CR_SAPIC_IVR:
    case IA64_CR_SAPIC_IRR0:
    case IA64_CR_SAPIC_IRR1:
    case IA64_CR_SAPIC_IRR2:
    case IA64_CR_SAPIC_IRR3:
        /*
         * These registers are atomic, have read side effects, or are backed
         * by the asynchronous interrupt bitmap rather than env->cr[].
         */
        gen_helper_read_cr(value, tcg_env, tcg_constant_i32(cr_num));
        break;
    default:
        tcg_gen_ld_i64(value, tcg_env,
                       offsetof(CPUIA64State, cr) +
                       cr_num * sizeof(uint64_t));
        break;
    }
}

static void ia64_gen_cr_write(uint32_t cr_num, TCGv_i64 value)
{
    switch (cr_num) {
    case IA64_CR_IIPA:
        tcg_gen_st_i64(value, tcg_env,
                       offsetof(CPUIA64State, cr) +
                       cr_num * sizeof(uint64_t));
        /* mov to IIPA also establishes the architected last_IP value. */
        tcg_gen_st_i64(value, tcg_env,
                       offsetof(CPUIA64State, last_successful_bundle));
        break;
    case IA64_CR_ITM:
    case IA64_CR_IVA:
    case IA64_CR_PTA:
    case IA64_CR_SAPIC_LID:
    case IA64_CR_SAPIC_IVR:
    case IA64_CR_SAPIC_TPR:
    case IA64_CR_SAPIC_EOI:
    case IA64_CR_SAPIC_IRR0:
    case IA64_CR_SAPIC_IRR1:
    case IA64_CR_SAPIC_IRR2:
    case IA64_CR_SAPIC_IRR3:
    case IA64_CR_ITV:
    case IA64_CR_CMCV:
        gen_helper_write_cr(tcg_env, tcg_constant_i32(cr_num), value);
        break;
    default:
        tcg_gen_st_i64(value, tcg_env,
                       offsetof(CPUIA64State, cr) +
                       cr_num * sizeof(uint64_t));
        break;
    }
}

IA64GenResult ia64_gen_system(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression)
{
    const IA64SystemOperands *op = &insn->operands.system;
    const uint64_t next_ip = insn->address + 16;

    switch (insn->opcode) {
    case IA64_OP_MOV_BRGR:
        ia64_gen_gr_write_nat_clear(insn, op->destination,
                                    cpu_br[op->source & 7]);
        break;
    case IA64_OP_MOV_GRBR:
        ia64_gen_check_nat_register(insn, op->destination);
        tcg_gen_mov_i64(cpu_br[op->source & 7],
                        ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_PRGR:
        if (op->destination != 0) {
            TCGv_i64 packed = tcg_temp_new_i64();

            gen_helper_read_pr(packed, tcg_env);
            ia64_gen_gr_write_nat_clear(insn, op->destination, packed);
        }
        break;
    case IA64_OP_MOV_GRPR:
        ia64_gen_check_nat_register(insn, op->destination);
        gen_helper_write_pr(tcg_env, ia64_gr_src(op->destination),
                            tcg_constant_i64(op->immediate));
        break;
    case IA64_OP_MOV_PR_ROT_IMM:
        gen_helper_write_pr(tcg_env, tcg_constant_i64(op->immediate),
                            tcg_constant_i64(0xffffffffffff0000ULL));
        break;
    case IA64_OP_MOV_ARGR:
        if (!ia64_ar_is_simple(op->source)) {
            ia64_gen_validate_ar_access(insn, tcg_constant_i64(0), false);
        }
        if (op->destination != 0) {
            TCGv_i64 val = tcg_temp_new_i64();

            if (ia64_ar_is_simple(op->source)) {
                ia64_gen_read_simple_ar(val, op->source);
            } else {
                if (ia64_ar_access_reads_clock(op->source) &&
                    ia64_clock_access_needs_io(ctx)) {
                    translator_io_start(&ctx->base);
                }
                if (op->source == IA64_AR_ITC) {
                    gen_helper_itc_read(val, tcg_env);
                } else {
                    gen_helper_read_ar(val, tcg_env,
                                       tcg_constant_i32(op->source));
                }
            }
            ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        }
        break;
    case IA64_OP_MOV_GRAR:
        /*
         * mov-to-BSPSTORE/RNAT checks RSC.mode before consuming a NaT
         * source (SDM Vol. 3, mov ar operation).  Other application-register
         * access checks follow Register NaT Consumption in the instruction's
         * priority order.
         */
        if (op->source == IA64_AR_BSPSTORE || op->source == IA64_AR_RNAT) {
            ia64_gen_validate_ar_access(insn,
                                        ia64_gr_src(op->destination), true);
        }
        ia64_gen_check_nat_register(insn, op->destination);
        if (ia64_ar_is_simple(op->source)) {
            if (op->source == 40 || op->source == 64) {
                ia64_gen_validate_ar_access(insn, ia64_gr_src(op->destination),
                                            true);
            }
            ia64_gen_write_simple_ar(op->source, ia64_gr_src(op->destination));
        } else {
            if (op->source != IA64_AR_BSPSTORE &&
                op->source != IA64_AR_RNAT) {
                ia64_gen_validate_ar_access(
                    insn, ia64_gr_src(op->destination), true);
            }
            if (ia64_ar_access_reads_clock(op->source)) {
                translator_io_start(&ctx->base);
                ia64_request_exit_after_bundle(ctx);
            }
            gen_helper_write_ar(tcg_env, tcg_constant_i32(op->source),
                                ia64_gr_src(op->destination));
        }
        break;
    case IA64_OP_MOV_IMMAR:
        if (ia64_ar_is_simple(op->source)) {
            if (op->source == 40 || op->source == 64) {
                ia64_gen_validate_ar_access(
                    insn, tcg_constant_i64(op->immediate), true);
            }
            ia64_gen_write_simple_ar(op->source,
                                     tcg_constant_i64(op->immediate));
        } else {
            ia64_gen_validate_ar_access(
                insn, tcg_constant_i64(op->immediate), true);
            if (ia64_ar_access_reads_clock(op->source)) {
                translator_io_start(&ctx->base);
                ia64_request_exit_after_bundle(ctx);
            }
            gen_helper_write_ar(tcg_env, tcg_constant_i32(op->source),
                                tcg_constant_i64(op->immediate));
        }
        break;
    case IA64_OP_MOV_CRGR:
    {
        TCGv_i64 checked = tcg_temp_new_i64();

        ia64_gen_validate_cr_access(checked, insn,
                                    tcg_constant_i64(0), false);
        if (op->destination != 0) {
            TCGv_i64 val = tcg_temp_new_i64();

            ia64_gen_cr_read(val, op->source);
            ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        }
        break;
    }
    case IA64_OP_MOV_GRCR:
    {
        TCGv_i64 checked = tcg_temp_new_i64();

        if (ia64_cr_is_read_only(op->source)) {
            ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                      insn->raw, insn->slot);
            if (skip == NULL) {
                return IA64_GEN_NORETURN;
            }
            break;
        }
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_validate_cr_access(checked, insn,
                                    ia64_gr_src(op->destination), true);
        if (ia64_cr_write_reads_clock(op->source)) {
            translator_io_start(&ctx->base);
        }
        ia64_gen_cr_write(op->source, checked);
        if (ia64_cr_write_requires_tb_exit(op->source)) {
            ia64_request_exit_after_bundle(ctx);
        }
        break;
    }
    case IA64_OP_MOV_RRGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_rrgr_read(val, tcg_env, ia64_gr_src(op->source));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRRR:
    {
        TCGv_i64 checked = tcg_temp_new_i64();

        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_check_nat_register(insn, op->destination);
        gen_helper_validate_rr_value(
            checked, tcg_env, ia64_gr_src(op->destination),
            tcg_constant_i64(insn->address),
            tcg_constant_i64(insn->raw),
            tcg_constant_i32(insn->slot));
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_grrr_write(tcg_env, ia64_gr_src(op->source),
                                  checked);
        ia64_request_exit_after_bundle(ctx);
        break;
    }
    case IA64_OP_MOV_PKRGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_pkrgr_read(val, tcg_env, tcg_constant_i32(op->source));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_PKRGR_INDEXED: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(insn, ia64_gr_src(op->register_index),
                                      IA64_PKR_COUNT);
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_pkrgr_indexed_read(
            val, tcg_env, ia64_gr_src(op->register_index));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPKR:
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_reserved_bits(insn, ia64_gr_src(op->destination),
                                     ia64_pkr_mask(ctx->env));
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_grpkr_write(tcg_env, tcg_constant_i32(op->source),
                                   ia64_gr_src(op->destination));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_MOV_GRPKR_INDEXED:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_register_index(insn, ia64_gr_src(op->register_index),
                                      IA64_PKR_COUNT);
        ia64_gen_check_reserved_bits(insn, ia64_gr_src(op->destination),
                                     ia64_pkr_mask(ctx->env));
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_grpkr_indexed_write(
            tcg_env, ia64_gr_src(op->register_index),
            ia64_gr_src(op->destination));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_MOV_UMGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        tcg_gen_andi_i64(val, cpu_psr, IA64_PSR_UM_WRITABLE_MASK);
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRUM: {
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_reserved_bits(insn, ia64_gr_src(op->destination),
                                     IA64_PSR_UM_WRITABLE_MASK);
        ia64_gen_write_user_mask(ia64_gr_src(op->destination));
        ia64_request_exit_after_bundle(ctx);
        break;
    }
    case IA64_OP_MOV_IBRGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_virtualization(insn);
        gen_helper_read_ibr(val, tcg_env, tcg_constant_i32(op->source - 12));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRIBR:
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_virtualization(insn);
        gen_helper_write_ibr(tcg_env, tcg_constant_i32(op->source - 12),
                             ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_IBRGR_INDEXED:
    case IA64_OP_MOV_DBRGR_INDEXED: {
        TCGv_i64 index64 = tcg_temp_new_i64();
        TCGv_i32 index = tcg_temp_new_i32();
        TCGv_i64 val = tcg_temp_new_i64();

        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(
            insn, ia64_gr_src(op->register_index),
            insn->opcode == IA64_OP_MOV_IBRGR_INDEXED ?
            IA64_IBR_IMPLEMENTED_COUNT : IA64_DBR_IMPLEMENTED_COUNT);
        ia64_gen_check_virtualization(insn);
        tcg_gen_mov_i64(index64, ia64_gr_src(op->register_index));
        tcg_gen_extrl_i64_i32(index, index64);
        if (insn->opcode == IA64_OP_MOV_IBRGR_INDEXED) {
            gen_helper_read_ibr(val, tcg_env, index);
        } else {
            gen_helper_read_dbr(val, tcg_env, index);
        }
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRIBR_INDEXED:
    case IA64_OP_MOV_GRDBR_INDEXED: {
        TCGv_i64 index64 = tcg_temp_new_i64();
        TCGv_i32 index = tcg_temp_new_i32();

        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_register_index(
            insn, ia64_gr_src(op->register_index),
            insn->opcode == IA64_OP_MOV_GRIBR_INDEXED ?
            IA64_IBR_IMPLEMENTED_COUNT : IA64_DBR_IMPLEMENTED_COUNT);
        ia64_gen_check_virtualization(insn);
        tcg_gen_mov_i64(index64, ia64_gr_src(op->register_index));
        tcg_gen_extrl_i64_i32(index, index64);
        if (insn->opcode == IA64_OP_MOV_GRIBR_INDEXED) {
            gen_helper_write_ibr(tcg_env, index, ia64_gr_src(op->destination));
        } else {
            gen_helper_write_dbr(tcg_env, index, ia64_gr_src(op->destination));
        }
        break;
    }
    case IA64_OP_MOV_DBRGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_virtualization(insn);
        gen_helper_read_dbr(val, tcg_env, tcg_constant_i32(op->source - 8));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRDBR:
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_virtualization(insn);
        gen_helper_write_dbr(tcg_env, tcg_constant_i32(op->source - 8),
                             ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_PMCGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_virtualization(insn);
        gen_helper_read_pmc(val, tcg_env, tcg_constant_i32(op->source - 32));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPMC:
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_virtualization(insn);
        gen_helper_write_pmc(tcg_env, tcg_constant_i32(op->source - 32),
                            ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_PMCGR_INDEXED: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        /* Unimplemented PMC indices read as zero rather than faulting. */
        ia64_gen_check_virtualization(insn);
        gen_helper_read_pmc_indexed(
            val, tcg_env, ia64_gr_src(op->register_index));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPMC_INDEXED:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->destination);
        /* Writes to unimplemented PMC indices are ignored. */
        ia64_gen_check_virtualization(insn);
        gen_helper_write_pmc_indexed(tcg_env, ia64_gr_src(op->register_index),
                                     ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_PMDGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        gen_helper_read_pmd_checked(
            val, tcg_env, tcg_constant_i64(op->source - 64),
            tcg_constant_i64(insn->address),
            tcg_constant_i64(insn->raw),
            tcg_constant_i32(insn->slot));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPMD:
        ia64_gen_check_nat_register(insn, op->destination);
        ia64_gen_check_virtualization(insn);
        gen_helper_write_pmd(tcg_env, tcg_constant_i32(op->source - 64),
                            ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_PMDGR_INDEXED: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        gen_helper_read_pmd_checked(
            val, tcg_env, ia64_gr_src(op->register_index),
            tcg_constant_i64(insn->address),
            tcg_constant_i64(insn->raw),
            tcg_constant_i32(insn->slot));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPMD_INDEXED:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->destination);
        /* PMC/PMD implementation-dependent indices never fault. */
        ia64_gen_check_virtualization(insn);
        gen_helper_write_pmd_indexed(tcg_env, ia64_gr_src(op->register_index),
                                     ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_CPUID: {
        TCGv_i64 val = tcg_temp_new_i64();

        ia64_gen_check_virtualization(insn);
        ia64_gen_cr_read(val, 13);
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_CPUID_INDEXED: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(insn, ia64_gr_src(op->register_index), 5);
        ia64_gen_check_virtualization(insn);
        gen_helper_read_cpuid(val, tcg_env, ia64_gr_src(op->register_index));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_DAHRGR_INDEXED: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(insn, ia64_gr_src(op->register_index), 8);
        gen_helper_read_dahr_indexed(
            val, tcg_env, ia64_gr_src(op->register_index));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_DAHR_IMM:
        gen_helper_write_dahr(tcg_env,
                              tcg_constant_i32(op->destination),
                              tcg_constant_i64(op->immediate));
        break;
    case IA64_OP_MOV_MSRGR: {
        TCGv_i64 val = tcg_temp_new_i64();
        ia64_gen_check_nat_register(insn, op->register_index);
        gen_helper_read_msr(val, tcg_env, ia64_gr_src(op->register_index));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRMSR:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->destination);
        gen_helper_write_msr(tcg_env, ia64_gr_src(op->register_index),
                             ia64_gr_src(op->destination));
        break;
    case IA64_OP_MOV_IP:
    case IA64_OP_MOV_CURRENT_IP:
        if (op->destination != 0) {
            tcg_gen_movi_i64(cpu_gr[op->destination], insn->address);
            ia64_gen_gr_nat_clear(insn, op->destination);
        }
        break;
    case IA64_OP_SSM:
        ia64_gen_check_reserved_bits(
            insn, tcg_constant_i64(op->immediate),
            0x3eULL | (0x7ULL << 13) | (0x7fULL << 17));
        ia64_gen_check_virtualization(insn);
        gen_helper_ssm(tcg_env, tcg_constant_i64(op->immediate));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_RSM:
        ia64_gen_check_reserved_bits(
            insn, tcg_constant_i64(op->immediate),
            0x3eULL | (0x7ULL << 13) | (0x7fULL << 17));
        ia64_gen_check_virtualization(insn);
        gen_helper_rsm(tcg_env, tcg_constant_i64(op->immediate));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_MOV_PSRGR: {
        TCGv_i64 val = tcg_temp_new_i64();

        ia64_gen_check_virtualization(insn);
        tcg_gen_andi_i64(val, cpu_psr,
                         UINT64_C(0xffffffff) | (UINT64_C(3) << 35));
        ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        break;
    }
    case IA64_OP_MOV_GRPSR: {
        uint64_t allowed = UINT64_C(0x3fff00000000) |
                           UINT64_C(0x3e) |
                           (UINT64_C(0x7) << 13) |
                           (UINT64_C(0x7ff) << 17);

        if (ia64_env_cpu_class(ctx->env)->has_virtualization) {
            allowed |= IA64_PSR_VM;
        }
        ia64_gen_check_nat_register(insn, op->destination);
        /*
         * mov psr.l ignores GR[r2]{63:32}.  Bits corresponding to defined
         * high PSR fields must not fault; checking corresponding reserved
         * high fields is implementation optional and retained here.
         */
        ia64_gen_check_reserved_bits(insn, ia64_gr_src(op->destination),
                                     allowed);
        ia64_gen_check_virtualization(insn);
        gen_helper_mov_psr_write(tcg_env, ia64_gr_src(op->destination),
                                 tcg_constant_i32(op->immediate != 0));
        ia64_request_exit_after_bundle(ctx);
        break;
    }
    case IA64_OP_BSW0:
        ia64_gen_check_virtualization(insn);
        gen_helper_set_psr_bn(tcg_env, tcg_constant_i32(0));
        if (skip == NULL) {
            ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                       record_iipa,
                                       track_psr_suppression);
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_BSW1:
        ia64_gen_check_virtualization(insn);
        gen_helper_set_psr_bn(tcg_env, tcg_constant_i32(1));
        if (skip == NULL) {
            ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                       record_iipa,
                                       track_psr_suppression);
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_ALLOC: {
        uint64_t packed = (uint64_t)op->immediate;
        uint32_t sof  = (packed >> 0) & 0x7f;
        uint32_t sol  = (packed >> 7) & 0x7f;
        uint32_t sor  = (packed >> 14) & 0x0f;
        if (!ia64_cfm_frame_fields_valid(sof, sol, sor)) {
            ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                      insn->raw, insn->slot);
            return IA64_GEN_NORETURN;
        }
        gen_helper_alloc_rse(tcg_env,
                              tcg_constant_i32(op->destination),
                              tcg_constant_i32(sof | (sol << 7) |
                                               (sor << 14)),
                              tcg_constant_i64(insn->address),
                              tcg_constant_i32(insn->slot));
        break;
    }
    case IA64_OP_COVER:
        ia64_gen_check_cover_virtualization(insn);
        gen_helper_cover_rse(tcg_env);
        break;
    case IA64_OP_FLUSHRS:
        gen_helper_flushrs_rse(tcg_env);
        break;
    case IA64_OP_LOADRS:
        gen_helper_loadrs_rse(tcg_env,
                              tcg_constant_i64(insn->address),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        break;
    case IA64_OP_ITR_D:
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(
            insn, ia64_gr_src(op->register_index),
            ia64_env_cpu_class(ctx->env)->dtr_count);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_itr_insert(tcg_env, ia64_gr_src(op->source),
                               ia64_gr_src(op->register_index),
                               tcg_constant_i32(1),
                               tcg_constant_i64(insn->raw),
                               tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_ITR_I:
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_register_index(
            insn, ia64_gr_src(op->register_index),
            ia64_env_cpu_class(ctx->env)->itr_count);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_itr_insert(tcg_env, ia64_gr_src(op->source),
                               ia64_gr_src(op->register_index),
                               tcg_constant_i32(0),
                               tcg_constant_i64(insn->raw),
                               tcg_constant_i32(insn->slot));
        ia64_gen_exit_to_slot_completed(ctx, insn->address, insn->slot + 1,
                                        insn->address,
                                        record_iipa,
                                        track_psr_suppression);
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_PTR_D:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_ptr_purge(tcg_env, ia64_gr_src(op->register_index),
                              ia64_gr_src(op->source),
                              tcg_constant_i32(1),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_PTR_I:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_ptr_purge(tcg_env, ia64_gr_src(op->register_index),
                              ia64_gr_src(op->source),
                              tcg_constant_i32(0),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_ITC_D:
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_itc_insert(tcg_env, ia64_gr_src(op->source),
                              tcg_constant_i32(1),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_ITC_I:
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_itc_insert(tcg_env, ia64_gr_src(op->source),
                              tcg_constant_i32(0),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_gen_exit_to_slot_completed(ctx, insn->address, insn->slot + 1,
                                        insn->address,
                                        record_iipa,
                                        track_psr_suppression);
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_PTC_L:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        ia64_gen_memory_release(insn);
        gen_helper_ptc_purge(tcg_env, ia64_gr_src(op->register_index),
                              ia64_gr_src(op->source),
                              tcg_constant_i32(0),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_PTC_G:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        ia64_gen_memory_release(insn);
        gen_helper_ptc_purge(tcg_env, ia64_gr_src(op->register_index),
                              ia64_gr_src(op->source),
                              tcg_constant_i32(1),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_PTC_E:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_sync_ip_for_helper(insn);
        ia64_gen_memory_release(insn);
        gen_helper_ptc_purge(tcg_env, ia64_gr_src(op->register_index),
                              tcg_constant_i64(0),
                              tcg_constant_i32(2),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_PTC_GA:
        ia64_gen_check_nat_register(insn, op->register_index);
        ia64_gen_check_nat_register(insn, op->source);
        ia64_gen_sync_ip_for_helper(insn);
        ia64_gen_memory_release(insn);
        gen_helper_ptc_purge(tcg_env, ia64_gr_src(op->register_index),
                              ia64_gr_src(op->source),
                              tcg_constant_i32(3),
                              tcg_constant_i64(insn->raw),
                              tcg_constant_i32(insn->slot));
        ia64_request_exit_after_bundle(ctx);
        break;
    case IA64_OP_NOP:
    case IA64_OP_HINT_I:
    case IA64_OP_HINT_B:
    case IA64_OP_HINT_F:
    case IA64_OP_HINT_X:
        break;
    case IA64_OP_HINT_M:
        if (insn->hint_m_reg_increment && op->register_index != 0) {
            tcg_gen_add_i64(cpu_gr[op->register_index],
                            cpu_gr[op->register_index],
                            cpu_gr[op->source]);
            ia64_gen_note_stacked_gr_write(insn, op->register_index);
        } else if (insn->imm_base_update && op->register_index != 0) {
            tcg_gen_addi_i64(cpu_gr[op->register_index],
                             cpu_gr[op->register_index], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->register_index);
        }
        break;
    case IA64_OP_CLRRRB:
    case IA64_OP_CLRRRB_PR:
        gen_helper_clrrrb_rse(
            tcg_env,
            tcg_constant_i32(insn->opcode == IA64_OP_CLRRRB_PR));
        if (skip == NULL) {
            ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                       record_iipa,
                                       track_psr_suppression);
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_VMSW:
        /*
         * Models without the virtualization extensions do not decode vmsw
         * at all: the encoding is reserved and raises an Illegal Operation
         * fault regardless of privilege.  Models with the extensions treat
         * vmsw as a privileged instruction, so PSR.cpl > 0 raises a
         * Privileged Operation fault first; at cpl 0 a Virtualization
         * fault is reported because this implementation provides no
         * virtual-machine environment.
         */
        if (ia64_env_cpu_class(ctx->env)->has_virtualization) {
            ia64_gen_check_privileged(insn);
            ia64_gen_raise_exception(IA64_EXCP_VIRTUALIZATION,
                                      insn->address, insn->raw, insn->slot);
        } else {
            ia64_gen_raise_exception(IA64_EXCP_ILLEGAL,
                                      insn->address, insn->raw, insn->slot);
        }
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_RUM:
    {
        uint64_t mask = op->immediate & IA64_PSR_UM_WRITABLE_MASK;
        TCGv_i64 value = tcg_temp_new_i64();

        if (mask == 0) {
            break;
        }
        tcg_gen_andi_i64(value, cpu_psr, ~mask);
        ia64_gen_write_user_mask(value);
        if (mask & IA64_PSR_BE) {
            if (ctx->memory.be_data) {
                ia64_request_exit_after_bundle(ctx);
            }
            if (ctx->reg.current_qp_known) {
                ctx->memory.be_data = false;
            }
        }
        break;
    }
    case IA64_OP_SUM_UM:
    {
        uint64_t mask = op->immediate & IA64_PSR_UM_WRITABLE_MASK;
        TCGv_i64 value = tcg_temp_new_i64();

        if (mask == 0) {
            break;
        }
        tcg_gen_ori_i64(value, cpu_psr, mask);
        ia64_gen_write_user_mask(value);
        if (mask & IA64_PSR_BE) {
            if (!ctx->memory.be_data) {
                ia64_request_exit_after_bundle(ctx);
            }
            if (ctx->reg.current_qp_known) {
                ctx->memory.be_data = true;
            }
        }
        break;
    }
    case IA64_OP_BRP:
        break;
    case IA64_OP_FSETC:
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_fsetc(tcg_env, tcg_constant_i32(op->auxiliary1),
                         tcg_constant_i32(op->source),
                         tcg_constant_i32(op->register_index));
        break;
    case IA64_OP_FCLRF:
        gen_helper_fclrf(tcg_env, tcg_constant_i32(op->auxiliary1));
        break;
    case IA64_OP_FCHKF: {
        TCGv_i64 taken = tcg_temp_new_i64();
        gen_helper_fchkf(taken, tcg_env, tcg_constant_i32(op->auxiliary1));
        ia64_gen_check_branch(ctx, taken, insn->address + op->immediate,
                              insn->address, record_iipa,
                              track_psr_suppression);
        break;
    }
    case IA64_OP_BREAK:
        if (op->auxiliary1 == 0 &&
            ia64_is_firmware_debug_break(insn->address, op->immediate)) {
            TCGv_i32 handled = tcg_temp_new_i32();
            TCGLabel *architected_break = gen_new_label();

            if (op->immediate == 0x100002) {
                gen_helper_firmware_debug_enter(
                    handled, tcg_env, tcg_constant_i64(insn->address));
            } else if (op->immediate == 0x100003) {
                gen_helper_firmware_debug_save(handled, tcg_env);
            } else {
                gen_helper_firmware_debug_restore(handled, tcg_env);
            }
            tcg_gen_brcondi_i32(TCG_COND_EQ, handled, 0,
                                architected_break);
            if (op->immediate == 0x100003) {
                ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                           record_iipa,
                                           track_psr_suppression);
            } else {
                tcg_gen_exit_tb(NULL, 0);
            }
            gen_set_label(architected_break);
            ia64_gen_raise_exception(IA64_EXCP_BREAK, insn->address,
                                      op->immediate, insn->slot);
            return IA64_GEN_NORETURN;
        } else if (op->immediate == 0x100001) {
            gen_helper_fpswa_dispatch(tcg_env);
        } else if ((op->immediate & 0x100000) &&
            ia64_is_pal_proc_break(ctx->env, insn->address)) {
            TCGv_i32 flags = tcg_temp_new_i32();
            TCGv_i32 resumed = tcg_temp_new_i32();
            TCGLabel *no_exit = gen_new_label();
            TCGLabel *resumed_exit = gen_new_label();

            gen_helper_pal_dispatch(flags, tcg_env);
            tcg_gen_andi_i32(resumed, flags,
                             IA64_PAL_DISPATCH_RESUMED);
            tcg_gen_brcondi_i32(TCG_COND_NE, resumed, 0, resumed_exit);
            tcg_gen_andi_i32(flags, flags,
                             IA64_PAL_DISPATCH_HALTED |
                             IA64_PAL_DISPATCH_EXIT_TB);
            tcg_gen_brcondi_i32(TCG_COND_EQ, flags, 0, no_exit);
            ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                       record_iipa,
                                       track_psr_suppression);
            gen_set_label(resumed_exit);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(no_exit);
        } else {
            ia64_gen_raise_exception(IA64_EXCP_BREAK, insn->address,
                                      op->immediate, insn->slot);
            if (skip == NULL) {
                return IA64_GEN_NORETURN;
            }
        }
        break;
    case IA64_OP_RFI:
        ia64_gen_check_virtualization(insn);
        gen_helper_rfi(tcg_env, tcg_constant_i64(insn->address),
                       tcg_constant_i32(insn->slot));
        tcg_gen_lookup_and_goto_ptr();
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_EPC:
        gen_helper_epc(tcg_env, tcg_constant_i64(insn->address),
                       tcg_constant_i64(insn->raw),
                       tcg_constant_i32(insn->slot));
        if (skip == NULL) {
            ia64_gen_exit_to_completed(ctx, next_ip, insn->address,
                                       record_iipa,
                                       track_psr_suppression);
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_SRLZ:
        gen_helper_tlb_serialize(tcg_env, tcg_constant_i32(1),
                                 tcg_constant_i32(1));
        ia64_gen_exit_to_slot_completed(ctx, insn->address, insn->slot + 1,
                                        insn->address,
                                        record_iipa,
                                        track_psr_suppression);
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_SRLZ_D:
        gen_helper_tlb_serialize(tcg_env, tcg_constant_i32(1),
                                 tcg_constant_i32(0));
        ia64_gen_exit_to_slot_completed(ctx, insn->address, insn->slot + 1,
                                        insn->address,
                                        record_iipa,
                                        track_psr_suppression);
        if (skip == NULL) {
            return IA64_GEN_NORETURN;
        }
        break;
    case IA64_OP_MF:
    case IA64_OP_MF_A:
        tcg_gen_mb(TCG_MO_ALL);
        break;
    case IA64_OP_TPA: {
        TCGv_i64 val = tcg_temp_new_i64();

        ia64_gen_check_nat_consumption(insn, op->register_index, 0,
                                       IA64_NAT_NON_ACCESS);
        ia64_gen_check_virtualization(insn);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_tpa(val, tcg_env, ia64_gr_src(op->register_index));
        if (op->destination != 0) {
            ia64_gen_gr_write_nat_clear(insn, op->destination, val);
        }
        break;
    }
    case IA64_OP_SYNC_I:
    case IA64_OP_FWB:
        break;
    case IA64_OP_TAK:
    case IA64_OP_THASH:
    case IA64_OP_TTAG: {
        TCGv_i64 result = tcg_temp_new_i64();

        if (insn->opcode == IA64_OP_TAK) {
            ia64_gen_check_nat_consumption(insn, op->register_index, 3,
                                           IA64_NAT_NON_ACCESS);
            ia64_gen_check_virtualization(insn);
            gen_helper_tak(result, tcg_env, ia64_gr_src(op->register_index));
            if (op->destination != 0) {
                ia64_gen_gr_write_nat_clear(insn, op->destination, result);
            }
        } else if (insn->opcode == IA64_OP_THASH) {
            ia64_gen_check_virtualization(insn);
            gen_helper_thash(result, tcg_env, ia64_gr_src(op->register_index));
            if (op->destination != 0) {
                /*
                 * Determine the result NaT from the original input before
                 * writing r1.  The source and destination may name the same
                 * GR, in which case an earlier data write would hide an
                 * unimplemented input virtual address.
                 */
                ia64_gen_gr_nat_from_1_or_unimplemented_va(
                    insn, op->destination, op->register_index,
                    ia64_env_cpu_class(ctx->env)->impl_va_msb);
                tcg_gen_mov_i64(cpu_gr[op->destination], result);
            }
        } else {
            ia64_gen_check_virtualization(insn);
            gen_helper_ttag(result, tcg_env, ia64_gr_src(op->register_index));
            if (op->destination != 0) {
                ia64_gen_gr_nat_from_1_or_unimplemented_va(
                    insn, op->destination, op->register_index,
                    ia64_env_cpu_class(ctx->env)->impl_va_msb);
                tcg_gen_mov_i64(cpu_gr[op->destination], result);
            }
        }
        break;
    }
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
