/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 integer/floating load, store and semaphore generation.
 */

#include "qemu/osdep.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#include "target/ia64/arch/arch.h"
#include "target/ia64/translate/translate.h"

typedef struct IA64MemoryPlan {
    TCGv_i64 address;
    TCGv_i64 increment;
    TCGv_i64 base_nat;
    TCGv_i64 increment_nat;
    MemOp memop;
    uint32_t size;
} IA64MemoryPlan;

typedef enum IA64IntegerLoadKind {
    IA64_INTEGER_LOAD_NORMAL,
    IA64_INTEGER_LOAD_ADVANCED,
    IA64_INTEGER_LOAD_FILL,
    IA64_INTEGER_LOAD_CHECK,
} IA64IntegerLoadKind;

static void ia64_gen_check_nat_access(const Ia64Instruction *insn,
                                      uint8_t reg, bool is_write);
static void ia64_gen_load_reg_base_update_inputs(
    const Ia64Instruction *insn, TCGv_i64 *increment,
    TCGv_i64 *base_nat, TCGv_i64 *increment_nat);
static void ia64_gen_load_reg_base_update(const Ia64Instruction *insn,
                                          TCGv_i64 addr,
                                          TCGv_i64 increment,
                                          TCGv_i64 base_nat,
                                          TCGv_i64 increment_nat);
static void ia64_gen_ld_fill_nat(const Ia64Instruction *insn, uint8_t reg,
                                 TCGv_i64 addr);

static TCGv_i64 ia64_gen_alat_load_begin(void)
{
    TCGv_i64 generation = tcg_temp_new_i64();

    gen_helper_alat_load_begin(generation, tcg_env);
    return generation;
}

static TCGv_i64 ia64_gen_speculative_load_probe(
    TCGv_i64 ok, TCGv_i64 addr, uint32_t size, uint32_t debug_size,
    uint32_t alignment, bool integer, bool record_alat,
    TCGv_i64 *generation)
{
    TCGv_i64 pa = NULL;

    if (record_alat) {
        TCGv_i128 qualified = tcg_temp_new_i128();

        /* Include qualification in the same memory-generation window. */
        *generation = ia64_gen_alat_load_begin();
        pa = tcg_temp_new_i64();
        if (integer) {
            gen_helper_speculative_int_probe_pa(qualified, tcg_env, addr,
                                                tcg_constant_i32(size));
        } else {
            gen_helper_speculative_probe_pa(
                qualified, tcg_env, addr, tcg_constant_i32(0),
                tcg_constant_i32(0), tcg_constant_i32(debug_size),
                tcg_constant_i32(alignment));
        }
        tcg_gen_extr_i128_i64(ok, pa, qualified);
    } else if (integer) {
        gen_helper_speculative_int_probe(ok, tcg_env, addr,
                                         tcg_constant_i32(size));
    } else {
        gen_helper_speculative_probe(
            ok, tcg_env, addr, tcg_constant_i32(0), tcg_constant_i32(0),
            tcg_constant_i32(debug_size), tcg_constant_i32(alignment));
    }
    return pa;
}

static void ia64_gen_set_check_load_alat(TCGv_i64 addr, uint32_t reg,
                                         uint32_t size, bool fp,
                                         TCGv_i64 generation)
{
    TCGv_i64 allowed = tcg_temp_new_i64();
    TCGLabel *done = gen_new_label();

    /* A c.nc miss reloads sequential memory and may allocate an ALAT entry. */
    gen_helper_advanced_load_allowed(allowed, tcg_env, addr);
    tcg_gen_brcondi_i64(TCG_COND_EQ, allowed, 0, done);
    if (fp) {
        gen_helper_set_alat_fp(tcg_env, tcg_constant_i32(reg), addr,
                               tcg_constant_i32(size), generation);
    } else {
        gen_helper_set_alat(tcg_env, tcg_constant_i32(reg), addr,
                            tcg_constant_i32(size), generation);
    }
    gen_set_label(done);
}

static IA64MemoryPlan ia64_memory_plan(DisasContext *ctx,
                                       const Ia64Instruction *insn,
                                       bool prepare_base_update)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    IA64MemoryPlan plan = {
        .address = tcg_temp_new_i64(),
        .memop = ia64_data_memop(ctx,
                                 ia64_memop_for_opcode(insn->opcode)),
    };

    plan.size = ia64_memop_size(plan.memop);
    tcg_gen_mov_i64(plan.address, ia64_gr_src(op->base));
    if (prepare_base_update) {
        ia64_gen_load_reg_base_update_inputs(
            insn, &plan.increment, &plan.base_nat, &plan.increment_nat);
    }
    return plan;
}

static void ia64_gen_memory_plan_base_update(
    const Ia64Instruction *insn, const IA64MemoryPlan *plan)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (insn->reg_base_update && op->base != 0) {
        ia64_gen_load_reg_base_update(insn, plan->address, plan->increment,
                                      plan->base_nat, plan->increment_nat);
    } else if (insn->imm_base_update && op->base != 0) {
        tcg_gen_addi_i64(cpu_gr[op->base], plan->address, op->immediate);
        ia64_gen_note_stacked_gr_write(insn, op->base);
    }
}

static void ia64_gen_integer_load(DisasContext *ctx,
                                  const Ia64Instruction *insn,
                                  IA64IntegerLoadKind kind)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    IA64MemoryPlan plan = ia64_memory_plan(ctx, insn, true);

    if (op->destination == 0) {
        ia64_gen_memory_plan_base_update(insn, &plan);
        return;
    }

    ia64_gen_check_nat_access(insn, op->base, false);

    if (kind == IA64_INTEGER_LOAD_CHECK) {
        const bool clear = insn->opcode >= IA64_OP_LD1C_CLR &&
                           insn->opcode <= IA64_OP_LD8C_CLR;
        TCGv_i64 alat_generation = NULL;
        TCGLabel *done = NULL;

        if (ctx->memory.full_alat) {
            TCGv_i64 hit = tcg_temp_new_i64();

            done = gen_new_label();
            gen_helper_check_load_alat_addr(
                hit, tcg_env, tcg_constant_i32(op->destination), plan.address,
                tcg_constant_i32(plan.size), tcg_constant_i32(clear));
            tcg_gen_brcondi_i64(TCG_COND_NE, hit, 0, done);
        }

        ia64_gen_check_alignment(ctx, insn, plan.address, plan.size, false,
                                 false);
        if (!clear && ctx->memory.full_alat) {
            alat_generation = ia64_gen_alat_load_begin();
        }
        ia64_gen_qemu_ld_i64(ctx, cpu_gr[op->destination], plan.address,
                             ctx->memory.mmu_idx, plan.memop);
        ia64_gen_gr_nat_clear(insn, op->destination);
        if (!clear && ctx->memory.full_alat) {
            ia64_gen_set_check_load_alat(plan.address, op->destination,
                                         plan.size, false, alat_generation);
        }
        if (done) {
            gen_set_label(done);
        }
        /* ld.c.clr.acq orders the ALAT lookup even when it hits. */
        ia64_gen_memory_acquire(insn);
    } else if (kind == IA64_INTEGER_LOAD_ADVANCED) {
        TCGv_i64 allowed = tcg_temp_new_i64();
        TCGv_i64 alat_generation = NULL;
        TCGLabel *fail = gen_new_label();
        TCGLabel *done = gen_new_label();

        /*
         * Fault qualification precedes the memory-attribute response.  A
         * non-speculative page can force the advanced load to return zero,
         * but does not suppress a matching Data Debug or Unaligned fault.
         */
        ia64_gen_check_alignment(ctx, insn, plan.address, plan.size, false,
                                 false);
        gen_helper_advanced_load_allowed(allowed, tcg_env, plan.address);
        tcg_gen_brcondi_i64(TCG_COND_EQ, allowed, 0, fail);
        if (ctx->memory.full_alat) {
            alat_generation = ia64_gen_alat_load_begin();
        }

        ia64_gen_qemu_ld_i64(ctx, cpu_gr[op->destination], plan.address,
                             ctx->memory.mmu_idx, plan.memop);
        ia64_gen_gr_nat_clear(insn, op->destination);
        if (ctx->memory.full_alat) {
            gen_helper_set_alat(tcg_env, tcg_constant_i32(op->destination),
                                plan.address, tcg_constant_i32(plan.size),
                                alat_generation);
        }
        tcg_gen_br(done);

        gen_set_label(fail);
        if (ctx->memory.full_alat) {
            gen_helper_invalidate_alat_reg(tcg_env,
                                           tcg_constant_i32(op->destination));
        }
        tcg_gen_movi_i64(cpu_gr[op->destination], 0);
        ia64_gen_gr_nat_clear(insn, op->destination);
        gen_set_label(done);
    } else {
        ia64_gen_check_alignment(ctx, insn, plan.address, plan.size, false,
                                 false);
        ia64_gen_qemu_ld_i64(ctx, cpu_gr[op->destination], plan.address,
                             ctx->memory.mmu_idx, plan.memop);
        if (kind == IA64_INTEGER_LOAD_FILL) {
            ia64_gen_ld_fill_nat(insn, op->destination, plan.address);
        } else {
            ia64_gen_gr_nat_clear(insn, op->destination);
        }
        ia64_gen_memory_acquire(insn);
    }

    ia64_gen_memory_plan_base_update(insn, &plan);
}

static void ia64_gen_check_nat_access(const Ia64Instruction *insn,
                                      uint8_t reg, bool is_write)
{
    DisasContext *ctx = insn->ctx;
    uint64_t bit;

    ia64_gen_check_nat_consumption(insn, reg,
                                   is_write ? IA64_ISR_W : IA64_ISR_R,
                                   IA64_NAT_ACCESS);

    /*
     * A faulting memory access can continue past the consumption check only
     * when its address/value GR is not NaTed.  Preserve that fact for later
     * instructions in the TB.  Do not infer anything across a dynamic
     * predicate: its nullified path bypasses this check.
     */
    if (reg == 0 || !ctx->reg.current_qp_known ||
        !ctx->reg.current_qp_value) {
        return;
    }
    bit = UINT64_C(1) << (reg % 64);
    ctx->memory.nat_known_clear[reg / 64] |= bit;
    ctx->memory.nat_known_set[reg / 64] &= ~bit;
}

static void ia64_gen_check_nat_semaphore(const Ia64Instruction *insn,
                                         uint8_t reg,
                                         Ia64NatConsumptionKind kind)
{
    ia64_gen_check_nat_consumption(insn, reg, IA64_ISR_R | IA64_ISR_W,
                                   kind);
}

static void ia64_gen_load_reg_base_update_inputs(const Ia64Instruction *insn,
                                                 TCGv_i64 *increment,
                                                 TCGv_i64 *base_nat,
                                                 TCGv_i64 *increment_nat)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (!insn->reg_base_update) {
        return;
    }

    *increment = tcg_temp_new_i64();
    *base_nat = ia64_gen_gr_nat_read(op->base);
    *increment_nat = ia64_gen_gr_nat_read(op->source);
    tcg_gen_mov_i64(*increment, ia64_gr_src(op->source));
}

static void ia64_gen_load_reg_base_update(const Ia64Instruction *insn,
                                          TCGv_i64 addr,
                                          TCGv_i64 increment,
                                          TCGv_i64 base_nat,
                                          TCGv_i64 increment_nat)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    TCGv_i64 new_base = tcg_temp_new_i64();
    TCGv_i64 new_nat = tcg_temp_new_i64();

    tcg_gen_add_i64(new_base, addr, increment);
    tcg_gen_mov_i64(cpu_gr[op->base], new_base);
    tcg_gen_or_i64(new_nat, base_nat, increment_nat);
    ia64_gen_gr_nat_assign(insn, op->base, new_nat);
}

static void ia64_gen_lfetch(const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 increment = NULL;
    TCGv_i64 base_nat = NULL;
    TCGv_i64 increment_nat = NULL;

    tcg_gen_mov_i64(addr, ia64_gr_src(op->base));
    ia64_gen_load_reg_base_update_inputs(insn, &increment, &base_nat,
                                         &increment_nat);

    if (insn->opcode == IA64_OP_LFETCH_FAULT) {
        TCGLabel *done = gen_new_label();
        TCGv_i64 ed = tcg_temp_new_i64();

        tcg_gen_andi_i64(ed, cpu_psr, IA64_PSR_ED);
        tcg_gen_brcondi_i64(TCG_COND_NE, ed, 0, done);
        ia64_gen_check_nat_consumption(insn, op->base,
                                       IA64_ISR_R | 4,
                                       IA64_NAT_NON_ACCESS);
        tcg_gen_movi_i64(cpu_ip, ia64_ip_bundle_addr(insn->address));
        gen_helper_lfetch_fault(tcg_env, addr,
                                tcg_constant_i64(
                                    ia64_ip_bundle_addr(insn->address)),
                                tcg_constant_i32(insn->slot));
        gen_set_label(done);
    }

    if (insn->reg_base_update) {
        ia64_gen_load_reg_base_update(insn, addr, increment, base_nat,
                                      increment_nat);
    } else if (insn->imm_base_update && op->base != 0) {
        tcg_gen_addi_i64(cpu_gr[op->base], addr, op->immediate);
        ia64_gen_note_stacked_gr_write(insn, op->base);
    }
}

static void ia64_gen_ld_fill_nat(const Ia64Instruction *insn, uint8_t reg,
                                 TCGv_i64 addr)
{
    TCGv_i64 unat = tcg_temp_new_i64();
    TCGv_i64 bitpos = tcg_temp_new_i64();
    TCGv_i64 natbit = tcg_temp_new_i64();

    ia64_gen_read_simple_ar(unat, IA64_AR_UNAT);
    tcg_gen_shri_i64(bitpos, addr, 3);
    tcg_gen_andi_i64(bitpos, bitpos, 0x3f);
    tcg_gen_shr_i64(natbit, unat, bitpos);
    tcg_gen_andi_i64(natbit, natbit, 1);
    ia64_gen_gr_nat_assign(insn, reg, natbit);
}

static void ia64_gen_speculative_load(DisasContext *ctx,
                                      const Ia64Instruction *insn,
                                      bool advanced)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    const bool base_nat_clear =
        ia64_gr_nat_is_known_clear(insn, op->base);
    const bool base_nat_set =
        ia64_gr_nat_is_known_set(insn, op->base);
    MemOp mop = ia64_data_memop(ctx, ia64_memop_for_opcode(insn->opcode));
    TCGv_i64 addr;
    TCGv_i64 increment = NULL;
    TCGv_i64 base_nat = NULL;
    TCGv_i64 increment_nat = NULL;
    TCGv_i64 alat_generation = NULL;
    TCGv_i64 ok;
    TCGLabel *l_fail;
    TCGLabel *l_done;

    if (op->destination == 0) {
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base],
                             op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        return;
    }

    addr = tcg_temp_new_i64();
    ia64_gen_load_reg_base_update_inputs(insn, &increment, &base_nat,
                                         &increment_nat);
    ok = tcg_temp_new_i64();
    l_fail = base_nat_set ? NULL : gen_new_label();
    l_done = base_nat_set ? NULL : gen_new_label();

    tcg_gen_mov_i64(addr, ia64_gr_src(op->base));
    if (!base_nat_set) {
        if (!base_nat_clear) {
            TCGv_i64 addr_nat = ia64_gen_gr_nat_read(op->base);

            tcg_gen_brcondi_i64(TCG_COND_NE, addr_nat, 0, l_fail);
        }
        ia64_gen_sync_ip_for_helper(insn);
        TCGv_i64 pa = ia64_gen_speculative_load_probe(
            ok, addr, ia64_memop_size(mop), 0, 0, true,
            advanced && ctx->memory.full_alat, &alat_generation);
        tcg_gen_brcondi_i64(TCG_COND_EQ, ok, 0, l_fail);

        ia64_gen_qemu_ld_i64(ctx, cpu_gr[op->destination], addr,
                             ctx->memory.mmu_idx, mop);
        ia64_gen_gr_nat_clear(insn, op->destination);
        if (advanced && ctx->memory.full_alat) {
            gen_helper_set_alat_pa(
                tcg_env, tcg_constant_i32(op->destination), addr,
                tcg_constant_i32(ia64_memop_size(mop)), alat_generation, pa);
        }
        tcg_gen_br(l_done);
        gen_set_label(l_fail);
    }
    if (advanced && ctx->memory.full_alat) {
        gen_helper_invalidate_alat_reg(
            tcg_env, tcg_constant_i32(op->destination));
    }
    tcg_gen_movi_i64(cpu_gr[op->destination], 0);
    ia64_gen_gr_nat_set(insn, op->destination);

    if (l_done != NULL) {
        gen_set_label(l_done);
    }
    if (insn->reg_base_update && op->base != 0) {
        ia64_gen_load_reg_base_update(insn, addr, increment, base_nat,
                                      increment_nat);
    } else if (insn->imm_base_update && op->base != 0) {
        tcg_gen_addi_i64(cpu_gr[op->base], addr, op->immediate);
        ia64_gen_note_stacked_gr_write(insn, op->base);
    }
}

static uint32_t ia64_fp_load_size(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LDFS:
        return 4;
    case IA64_OP_LDFD:
    case IA64_OP_LDF8:
        return 8;
    case IA64_OP_LDFE:
        return 10;
    case IA64_OP_LDF_FILL:
        return 16;
    default:
        g_assert_not_reached();
    }
}

static uint32_t ia64_fp_load_alat_size(Ia64Opcode opcode)
{
    return ia64_fp_load_size(opcode);
}

static uint32_t ia64_fp_load_debug_size(Ia64Opcode opcode)
{
    /* Extended and spill/fill operands are 16-byte data-debug datums. */
    return opcode == IA64_OP_LDFE || opcode == IA64_OP_LDF_FILL ?
           16 : ia64_fp_load_size(opcode);
}

static uint32_t ia64_fp_load_natural_alignment(Ia64Opcode opcode)
{
    return opcode == IA64_OP_LDFE || opcode == IA64_OP_LDF_FILL ?
           16 : ia64_fp_load_size(opcode);
}

static IA64AlignmentClass ia64_fp_load_alignment_class(Ia64Opcode opcode)
{
    return opcode == IA64_OP_LDF_FILL ?
           IA64_ALIGNMENT_FP_FILL_SPILL : IA64_ALIGNMENT_FP;
}

static void ia64_gen_check_fp_load_alignment(
    DisasContext *ctx, const Ia64Instruction *insn, TCGv_i64 addr)
{
    ia64_gen_check_alignment_model(
        ctx, insn, addr, ia64_fp_load_debug_size(insn->opcode),
        ia64_fp_load_size(insn->opcode),
        ia64_fp_load_natural_alignment(insn->opcode),
        ia64_fp_load_alignment_class(insn->opcode), IA64_ISR_R);
}

static void ia64_gen_fp_load_value(DisasContext *ctx,
                                   const Ia64Instruction *insn,
                                   TCGv_i64 addr)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    switch (insn->opcode) {
    case IA64_OP_LDFS:
        ia64_gen_fr_load(ctx, op->destination, addr,
                         ctx->memory.mmu_idx,
                         ia64_data_memop(ctx, MO_LEUL),
                         IA64_FP_REGISTER_LOAD_SINGLE);
        break;
    case IA64_OP_LDFD:
        ia64_gen_fr_load(ctx, op->destination, addr,
                         ctx->memory.mmu_idx,
                         ia64_data_memop(ctx, MO_LEUQ),
                         IA64_FP_REGISTER_LOAD_DOUBLE);
        break;
    case IA64_OP_LDF8:
        ia64_gen_fr_load(ctx, op->destination, addr,
                         ctx->memory.mmu_idx,
                         ia64_data_memop(ctx, MO_LEUQ),
                         IA64_FP_REGISTER_LOAD_SIGNIFICAND);
        break;
    case IA64_OP_LDF_FILL:
        gen_helper_ldf_fill(tcg_env, tcg_constant_i32(op->destination), addr);
        break;
    case IA64_OP_LDFE:
        gen_helper_ldfe(tcg_env, tcg_constant_i32(op->destination), addr);
        break;
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_fp_load_advanced_fail_value(const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (insn->opcode == IA64_OP_LDF8) {
        ia64_gen_fr_mov_sig(insn->ctx, op->destination, tcg_constant_i64(0));
    } else {
        ia64_gen_fr_mov(insn->ctx, op->destination, tcg_constant_i64(0));
    }
}

static void ia64_gen_fp_load_base_update(const Ia64Instruction *insn,
                                         TCGv_i64 addr,
                                         TCGv_i64 increment,
                                         TCGv_i64 base_nat,
                                         TCGv_i64 increment_nat)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (insn->reg_base_update && op->base != 0) {
        ia64_gen_load_reg_base_update(insn, addr, increment, base_nat,
                                      increment_nat);
    } else if (insn->imm_base_update && op->base != 0) {
        tcg_gen_addi_i64(cpu_gr[op->base], addr, op->immediate);
        ia64_gen_note_stacked_gr_write(insn, op->base);
    }
}

static void ia64_gen_fp_load(DisasContext *ctx, const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    const uint32_t size = ia64_fp_load_size(insn->opcode);
    const uint32_t alat_size = ia64_fp_load_alat_size(insn->opcode);
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 increment = NULL;
    TCGv_i64 base_nat = NULL;
    TCGv_i64 increment_nat = NULL;
    TCGv_i64 alat_generation = NULL;

    tcg_gen_mov_i64(addr, ia64_gr_src(op->base));
    ia64_gen_load_reg_base_update_inputs(insn, &increment, &base_nat,
                                         &increment_nat);

    if (insn->fp_load_check && ctx->memory.full_alat) {
        TCGv_i64 hit = tcg_temp_new_i64();
        TCGLabel *l_done = gen_new_label();

        ia64_gen_check_nat_access(insn, op->base, false);
        gen_helper_check_load_alat_fp_addr(hit, tcg_env,
                                           tcg_constant_i32(op->destination),
                                           addr, tcg_constant_i32(alat_size),
                                           tcg_constant_i32(
                                               insn->fp_load_check_clear));
        tcg_gen_brcondi_i64(TCG_COND_NE, hit, 0, l_done);

        ia64_gen_check_fp_load_alignment(ctx, insn, addr);
        if (!insn->fp_load_check_clear) {
            alat_generation = ia64_gen_alat_load_begin();
        }
        ia64_gen_fp_load_value(ctx, insn, addr);
        if (!insn->fp_load_check_clear) {
            ia64_gen_set_check_load_alat(addr, op->destination, alat_size,
                                         true, alat_generation);
        }

        gen_set_label(l_done);
        ia64_gen_fp_load_base_update(insn, addr, increment, base_nat,
                                     increment_nat);
        return;
    }

    if (insn->fp_load_speculative) {
        TCGv_i64 ok = tcg_temp_new_i64();
        TCGLabel *l_fail = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        if (op->base != 0) {
            TCGv_i64 addr_nat = ia64_gen_gr_nat_read(op->base);

            tcg_gen_brcondi_i64(TCG_COND_NE, addr_nat, 0, l_fail);
        }
        ia64_gen_sync_ip_for_helper(insn);
        TCGv_i64 pa = ia64_gen_speculative_load_probe(
            ok, addr, size, ia64_fp_load_debug_size(insn->opcode),
            IA64_ALIGNMENT_INFO(size,
                ia64_fp_load_natural_alignment(insn->opcode),
                ia64_fp_load_alignment_class(insn->opcode)), false,
            insn->fp_load_advanced && ctx->memory.full_alat, &alat_generation);
        tcg_gen_brcondi_i64(TCG_COND_EQ, ok, 0, l_fail);

        ia64_gen_fp_load_value(ctx, insn, addr);
        if (insn->fp_load_advanced && ctx->memory.full_alat) {
            gen_helper_set_alat_pa(
                tcg_env, tcg_constant_i32(op->destination | 128),
                addr, tcg_constant_i32(alat_size), alat_generation, pa);
        }
        tcg_gen_br(l_done);

        gen_set_label(l_fail);
        if (insn->fp_load_advanced && ctx->memory.full_alat) {
            gen_helper_invalidate_alat_fp_reg(
                tcg_env, tcg_constant_i32(op->destination));
        }
        ia64_gen_fr_set_nat(insn->ctx, op->destination);

        gen_set_label(l_done);
        ia64_gen_fp_load_base_update(insn, addr, increment, base_nat,
                                     increment_nat);
        return;
    }

    ia64_gen_check_nat_access(insn, op->base, false);
    if (insn->fp_load_advanced) {
        TCGv_i64 allowed = tcg_temp_new_i64();
        TCGLabel *l_fail = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        ia64_gen_check_fp_load_alignment(ctx, insn, addr);
        gen_helper_advanced_load_allowed(allowed, tcg_env, addr);
        tcg_gen_brcondi_i64(TCG_COND_EQ, allowed, 0, l_fail);
        if (ctx->memory.full_alat) {
            alat_generation = ia64_gen_alat_load_begin();
        }
        ia64_gen_fp_load_value(ctx, insn, addr);
        if (ctx->memory.full_alat) {
            gen_helper_set_alat_fp(tcg_env, tcg_constant_i32(op->destination),
                                   addr, tcg_constant_i32(alat_size),
                                   alat_generation);
        }
        tcg_gen_br(l_done);

        gen_set_label(l_fail);
        if (ctx->memory.full_alat) {
            gen_helper_invalidate_alat_fp_reg(
                tcg_env, tcg_constant_i32(op->destination));
        }
        ia64_gen_fp_load_advanced_fail_value(insn);

        gen_set_label(l_done);
        ia64_gen_fp_load_base_update(insn, addr, increment, base_nat,
                                     increment_nat);
        return;
    }
    ia64_gen_check_fp_load_alignment(ctx, insn, addr);
    ia64_gen_fp_load_value(ctx, insn, addr);
    ia64_gen_fp_load_base_update(insn, addr, increment, base_nat,
                                 increment_nat);
}

static uint32_t ia64_fp_load_pair_size(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LDFPS:
        return 8;
    case IA64_OP_LDFPD:
    case IA64_OP_LDFP8:
        return 16;
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_check_fp_load_pair_alignment(
    DisasContext *ctx, const Ia64Instruction *insn, TCGv_i64 addr,
    uint32_t size)
{
    ia64_gen_check_alignment_model(
        ctx, insn, addr, size, size, size, IA64_ALIGNMENT_FP_PAIR,
        IA64_ISR_R);
}

static void ia64_gen_fp_load_pair_value(DisasContext *ctx,
                                        const Ia64Instruction *insn,
                                        TCGv_i64 addr)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    TCGv_i64 first = tcg_temp_new_i64();
    TCGv_i64 second = tcg_temp_new_i64();

    switch (insn->opcode) {
    case IA64_OP_LDFPS: {
        TCGv_i64 pair = tcg_temp_new_i64();

        ia64_gen_qemu_ld_i64(ctx, pair, addr, ctx->memory.mmu_idx,
                             ia64_data_memop(ctx, MO_UQ));
        if (ctx->memory.be_data) {
            tcg_gen_shri_i64(first, pair, 32);
            tcg_gen_ext32u_i64(second, pair);
        } else {
            tcg_gen_ext32u_i64(first, pair);
            tcg_gen_shri_i64(second, pair, 32);
        }
        gen_helper_setf_s(tcg_env, tcg_constant_i32(op->destination), first);
        gen_helper_setf_s(tcg_env, tcg_constant_i32(op->source), second);
        break;
    }
    case IA64_OP_LDFPD:
    case IA64_OP_LDFP8: {
        TCGv_i128 pair = tcg_temp_new_i128();

        ia64_gen_qemu_ld_i128(
            ctx, pair, addr, ctx->memory.mmu_idx,
            ia64_data_memop(ctx, MO_UO));
        if (ctx->memory.be_data) {
            tcg_gen_extr_i128_i64(second, first, pair);
        } else {
            tcg_gen_extr_i128_i64(first, second, pair);
        }
        if (insn->opcode == IA64_OP_LDFPD) {
            ia64_gen_fr_mov(insn->ctx, op->destination, first);
            ia64_gen_fr_mov(insn->ctx, op->source, second);
        } else {
            ia64_gen_fr_mov_sig(insn->ctx, op->destination, first);
            ia64_gen_fr_mov_sig(insn->ctx, op->source, second);
        }
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_fp_load_pair_nat_set(const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    ia64_gen_fr_set_nat(insn->ctx, op->destination);
    ia64_gen_fr_set_nat(insn->ctx, op->source);
}

static void ia64_gen_fp_load_pair_advanced_fail_value(
    const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (insn->opcode == IA64_OP_LDFP8) {
        ia64_gen_fr_mov_sig(insn->ctx, op->destination, tcg_constant_i64(0));
        ia64_gen_fr_mov_sig(insn->ctx, op->source, tcg_constant_i64(0));
    } else {
        ia64_gen_fr_mov(insn->ctx, op->destination, tcg_constant_i64(0));
        ia64_gen_fr_mov(insn->ctx, op->source, tcg_constant_i64(0));
    }
}

static void ia64_gen_fp_load_pair_base_update(const Ia64Instruction *insn,
                                              TCGv_i64 addr)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    if (insn->imm_base_update && op->base != 0) {
        tcg_gen_addi_i64(cpu_gr[op->base], addr, op->immediate);
        ia64_gen_note_stacked_gr_write(insn, op->base);
    }
}

static void ia64_gen_fp_load_pair(DisasContext *ctx,
                                  const Ia64Instruction *insn)
{
    const IA64MemoryOperands *op = &insn->operands.memory;
    const uint32_t size = ia64_fp_load_pair_size(insn->opcode);
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 alat_generation = NULL;

    tcg_gen_mov_i64(addr, ia64_gr_src(op->base));

    if (insn->fp_load_check && ctx->memory.full_alat) {
        TCGv_i64 hit = tcg_temp_new_i64();
        TCGLabel *l_done = gen_new_label();

        ia64_gen_check_nat_access(insn, op->base, false);
        gen_helper_check_load_alat_fp_addr(hit, tcg_env,
                                           tcg_constant_i32(op->destination),
                                           addr, tcg_constant_i32(size),
                                           tcg_constant_i32(
                                               insn->fp_load_check_clear));
        tcg_gen_brcondi_i64(TCG_COND_NE, hit, 0, l_done);

        ia64_gen_check_fp_load_pair_alignment(ctx, insn, addr, size);
        if (!insn->fp_load_check_clear) {
            alat_generation = ia64_gen_alat_load_begin();
        }
        ia64_gen_fp_load_pair_value(ctx, insn, addr);
        if (!insn->fp_load_check_clear) {
            ia64_gen_set_check_load_alat(addr, op->destination, size, true,
                                         alat_generation);
        }

        gen_set_label(l_done);
        ia64_gen_fp_load_pair_base_update(insn, addr);
        return;
    }

    if (insn->fp_load_speculative) {
        TCGv_i64 ok = tcg_temp_new_i64();
        TCGLabel *l_fail = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        if (op->base != 0) {
            TCGv_i64 addr_nat = ia64_gen_gr_nat_read(op->base);

            tcg_gen_brcondi_i64(TCG_COND_NE, addr_nat, 0, l_fail);
        }
        ia64_gen_sync_ip_for_helper(insn);
        TCGv_i64 pa = ia64_gen_speculative_load_probe(
            ok, addr, size, size,
            IA64_ALIGNMENT_INFO(size, size, IA64_ALIGNMENT_FP_PAIR), false,
            insn->fp_load_advanced && ctx->memory.full_alat, &alat_generation);
        tcg_gen_brcondi_i64(TCG_COND_EQ, ok, 0, l_fail);

        ia64_gen_fp_load_pair_value(ctx, insn, addr);
        if (insn->fp_load_advanced && ctx->memory.full_alat) {
            gen_helper_set_alat_pa(
                tcg_env, tcg_constant_i32(op->destination | 128),
                addr, tcg_constant_i32(size), alat_generation, pa);
        }
        tcg_gen_br(l_done);

        gen_set_label(l_fail);
        if (insn->fp_load_advanced && ctx->memory.full_alat) {
            gen_helper_invalidate_alat_fp_reg(
                tcg_env, tcg_constant_i32(op->destination));
        }
        ia64_gen_fp_load_pair_nat_set(insn);

        gen_set_label(l_done);
        ia64_gen_fp_load_pair_base_update(insn, addr);
        return;
    }

    ia64_gen_check_nat_access(insn, op->base, false);
    if (insn->fp_load_advanced) {
        TCGv_i64 allowed = tcg_temp_new_i64();
        TCGLabel *l_fail = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        ia64_gen_check_fp_load_pair_alignment(ctx, insn, addr, size);
        gen_helper_advanced_load_allowed(allowed, tcg_env, addr);
        tcg_gen_brcondi_i64(TCG_COND_EQ, allowed, 0, l_fail);
        if (ctx->memory.full_alat) {
            alat_generation = ia64_gen_alat_load_begin();
        }
        ia64_gen_fp_load_pair_value(ctx, insn, addr);
        if (ctx->memory.full_alat) {
            gen_helper_set_alat_fp(tcg_env, tcg_constant_i32(op->destination),
                                   addr, tcg_constant_i32(size),
                                   alat_generation);
        }
        tcg_gen_br(l_done);

        gen_set_label(l_fail);
        if (ctx->memory.full_alat) {
            gen_helper_invalidate_alat_fp_reg(
                tcg_env, tcg_constant_i32(op->destination));
        }
        ia64_gen_fp_load_pair_advanced_fail_value(insn);

        gen_set_label(l_done);
        ia64_gen_fp_load_pair_base_update(insn, addr);
        return;
    }
    ia64_gen_check_fp_load_pair_alignment(ctx, insn, addr, size);
    ia64_gen_fp_load_pair_value(ctx, insn, addr);
    ia64_gen_fp_load_pair_base_update(insn, addr);
}

IA64GenResult ia64_gen_memory(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression)
{
    const IA64MemoryOperands *op = &insn->operands.memory;

    switch (insn->opcode) {
    case IA64_OP_LFETCH:
    case IA64_OP_LFETCH_COUNT:
    case IA64_OP_LFETCH_FAULT:
        /* Models without counted-prefetch support execute M52 as lfetch. */
        ia64_gen_lfetch(insn);
        break;
    case IA64_OP_LD1:
    case IA64_OP_LD2:
    case IA64_OP_LD4:
    case IA64_OP_LD8:
        ia64_gen_integer_load(ctx, insn, IA64_INTEGER_LOAD_NORMAL);
        break;
    case IA64_OP_LD1S:
    case IA64_OP_LD2S:
    case IA64_OP_LD4S:
    case IA64_OP_LD8S:
        ia64_gen_speculative_load(ctx, insn, false);
        break;
    case IA64_OP_LD1A:
    case IA64_OP_LD2A:
    case IA64_OP_LD4A:
    case IA64_OP_LD8A:
        ia64_gen_integer_load(ctx, insn, IA64_INTEGER_LOAD_ADVANCED);
        break;
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
        ia64_gen_speculative_load(ctx, insn, true);
        break;
    case IA64_OP_LD8FILL:
        ia64_gen_integer_load(ctx, insn, IA64_INTEGER_LOAD_FILL);
        break;
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD8C_CLR:
    case IA64_OP_LD1C_NC:
    case IA64_OP_LD2C_NC:
    case IA64_OP_LD4C_NC:
    case IA64_OP_LD8C_NC:
        ia64_gen_integer_load(ctx, insn, IA64_INTEGER_LOAD_CHECK);
        break;
    case IA64_OP_LD16:
        {
            TCGv_i128 pair = tcg_temp_new_i128();
            TCGv_i64 high = tcg_temp_new_i64();
            TCGv_i64 low = op->destination != 0 ? cpu_gr[op->destination] :
                           tcg_temp_new_i64();

            ia64_gen_check_nat_access(insn, op->base, false);
            ia64_gen_check_alignment(ctx, insn, ia64_gr_src(op->base), 16,
                                     true, false);
            if (ia64_env_cpu_class(ctx->env)->model ==
                IA64_CPU_MODEL_MONTECITO) {
                ia64_gen_sync_ip_for_helper(insn);
                gen_helper_check_montecito_16byte_access(
                    tcg_env, ia64_gr_src(op->base), tcg_constant_i32(0));
            }
            ia64_gen_qemu_ld_i128(
                ctx, pair, ia64_gr_src(op->base), ctx->memory.mmu_idx,
                ia64_data_memop(ctx, MO_UO));
            if (ctx->memory.be_data) {
                tcg_gen_extr_i128_i64(high, low, pair);
            } else {
                tcg_gen_extr_i128_i64(low, high, pair);
            }
            ia64_gen_memory_acquire(insn);
            if (op->destination != 0) {
                ia64_gen_gr_nat_clear(insn, op->destination);
            }
            gen_helper_write_ar(tcg_env, tcg_constant_i32(25), high);
        }
        break;
    case IA64_OP_ST16:
        {
            TCGv_i128 pair = tcg_temp_new_i128();
            TCGv_i64 high = tcg_temp_new_i64();

            ia64_gen_check_nat_access(insn, op->base, true);
            ia64_gen_check_nat_access(insn, op->source, true);
            ia64_gen_check_alignment(ctx, insn, ia64_gr_src(op->base), 16,
                                     true, true);
            if (ia64_env_cpu_class(ctx->env)->model ==
                IA64_CPU_MODEL_MONTECITO) {
                ia64_gen_sync_ip_for_helper(insn);
                gen_helper_check_montecito_16byte_access(
                    tcg_env, ia64_gr_src(op->base), tcg_constant_i32(1));
            }
            ia64_gen_memory_release(insn);
            gen_helper_read_ar(high, tcg_env, tcg_constant_i32(25));
            if (ctx->memory.be_data) {
                tcg_gen_concat_i64_i128(pair, high,
                                        ia64_gr_src(op->source));
            } else {
                tcg_gen_concat_i64_i128(pair, ia64_gr_src(op->source),
                                        high);
            }
            ia64_gen_qemu_st_i128(
                ctx, pair, ia64_gr_src(op->base), ctx->memory.mmu_idx,
                ia64_data_memop(ctx, MO_UO));
        }
        break;
    case IA64_OP_ST1:
    case IA64_OP_ST2:
    case IA64_OP_ST4:
    case IA64_OP_ST8:
    case IA64_OP_ST1REL:
    case IA64_OP_ST2REL:
    case IA64_OP_ST4REL:
    case IA64_OP_ST8REL:
    case IA64_OP_ST8SPILL: {
        IA64MemoryPlan plan = ia64_memory_plan(ctx, insn, false);
        bool spill = insn->opcode == IA64_OP_ST8SPILL;
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_access(insn, op->base, true);
        if (!spill) {
            ia64_gen_check_nat_access(insn, op->source, true);
        }
        if (spill) {
            TCGv_i64 nat = ia64_gen_gr_nat_read(op->source);

            /*
             * A NaTed GR has no software-visible data portion.  The SDM
             * permits st8.spill either to write zero or, under additional
             * implementation-wide constraints, to write that data portion.
             * Choose the unconditional zero behavior so this instruction
             * does not depend on how every NaT-propagating operation models
             * its otherwise undefined data bits.
             */
            tcg_gen_movcond_i64(TCG_COND_EQ, value, nat,
                                tcg_constant_i64(0),
                                ia64_gr_src(op->source),
                                tcg_constant_i64(0));
        } else {
            tcg_gen_mov_i64(value, ia64_gr_src(op->source));
        }
        ia64_gen_check_alignment(ctx, insn, plan.address, plan.size,
                                 false, true);
        ia64_gen_memory_release(insn);
        ia64_gen_qemu_st_i64(ctx, value, plan.address, ctx->memory.mmu_idx,
                             plan.memop);
        if (spill) {
            gen_helper_st_spill_unat(tcg_env, tcg_constant_i32(op->source),
                                     plan.address);
        }
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], plan.address, op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    }
    case IA64_OP_LDFD:
    case IA64_OP_LDFS:
    case IA64_OP_LDF_FILL:
    case IA64_OP_LDF8:
    case IA64_OP_LDFE:
        ia64_gen_fp_load(ctx, insn);
        break;
    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
        ia64_gen_fp_load_pair(ctx, insn);
        break;
    case IA64_OP_STFD: {
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_access(insn, op->base, true);
        ia64_gen_check_fr_nat_consumption(insn, op->source, IA64_ISR_W);
        ia64_gen_check_alignment_model(
            ctx, insn, ia64_gr_src(op->base), 8, 8, 8, IA64_ALIGNMENT_FP,
            IA64_ISR_W);
        ia64_gen_fr_read_double(ctx, value, op->source);
        ia64_gen_qemu_st_i64(ctx, value, ia64_gr_src(op->base),
                             ctx->memory.mmu_idx,
                             ia64_data_memop(ctx, MO_LEUQ));
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    }
    case IA64_OP_STFS: {
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_access(insn, op->base, true);
        ia64_gen_check_fr_nat_consumption(insn, op->source, IA64_ISR_W);
        ia64_gen_check_alignment_model(
            ctx, insn, ia64_gr_src(op->base), 4, 4, 4, IA64_ALIGNMENT_FP,
            IA64_ISR_W);
        gen_helper_getf(value, tcg_env, tcg_constant_i32(op->source),
                        tcg_constant_i32(1));
        ia64_gen_qemu_st_i64(ctx, value, ia64_gr_src(op->base),
                             ctx->memory.mmu_idx,
                             ia64_data_memop(ctx, MO_LEUL));
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    }
    case IA64_OP_STF_SPILL: {
        ia64_gen_check_nat_access(insn, op->base, true);
        ia64_gen_check_alignment_model(
            ctx, insn, ia64_gr_src(op->base), 16, 16, 16,
            IA64_ALIGNMENT_FP_FILL_SPILL, IA64_ISR_W);
        gen_helper_stf_spill(tcg_env, ia64_gr_src(op->base),
                             tcg_constant_i32(op->source));
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    }
    case IA64_OP_STF8: {
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_access(insn, op->base, true);
        ia64_gen_check_fr_nat_consumption(insn, op->source, IA64_ISR_W);
        ia64_gen_check_alignment_model(
            ctx, insn, ia64_gr_src(op->base), 8, 8, 8, IA64_ALIGNMENT_FP,
            IA64_ISR_W);
        gen_helper_getf(value, tcg_env, tcg_constant_i32(op->source),
                        tcg_constant_i32(2));
        ia64_gen_qemu_st_i64(ctx, value, ia64_gr_src(op->base),
                             ctx->memory.mmu_idx,
                             ia64_data_memop(ctx, MO_LEUQ));
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    }
    case IA64_OP_STFE:
        ia64_gen_check_nat_access(insn, op->base, true);
        ia64_gen_check_fr_nat_consumption(insn, op->source, IA64_ISR_W);
        ia64_gen_check_alignment_model(
            ctx, insn, ia64_gr_src(op->base), 16, 10, 16,
            IA64_ALIGNMENT_FP,
            IA64_ISR_W);
        gen_helper_stfe(tcg_env, ia64_gr_src(op->base),
                        tcg_constant_i32(op->source));
        if (insn->imm_base_update && op->base != 0) {
            tcg_gen_addi_i64(cpu_gr[op->base], cpu_gr[op->base], op->immediate);
            ia64_gen_note_stacked_gr_write(insn, op->base);
        }
        break;
    case IA64_OP_XCHG1:
    case IA64_OP_XCHG2:
    case IA64_OP_XCHG4:
    case IA64_OP_XCHG8: {
        IA64MemoryPlan plan = ia64_memory_plan(ctx, insn, false);
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_semaphore(insn, op->base, IA64_NAT_ACCESS);
        ia64_gen_check_nat_semaphore(insn, op->source, IA64_NAT_ACCESS);
        tcg_gen_mov_i64(value, ia64_gr_src(op->source));
        ia64_gen_check_alignment_access(ctx, insn, plan.address, plan.size,
                                        true, IA64_ISR_R | IA64_ISR_W);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_check_semaphore_access(tcg_env, plan.address);
        ia64_gen_memory_release(insn);
        ia64_gen_atomic_xchg_i64(ctx, cpu_gr[op->destination], plan.address,
                                 value, ctx->memory.mmu_idx, plan.memop);
        ia64_gen_memory_acquire(insn);
        ia64_gen_gr_nat_clear(insn, op->destination);
        break;
    }
    case IA64_OP_CMPXCHG1:
    case IA64_OP_CMPXCHG2:
    case IA64_OP_CMPXCHG4:
    case IA64_OP_CMPXCHG8: {
        IA64MemoryPlan plan = ia64_memory_plan(ctx, insn, false);
        TCGv_i64 value = tcg_temp_new_i64();
        TCGv_i64 ccv = tcg_temp_new_i64();

        ia64_gen_check_nat_semaphore(insn, op->base, IA64_NAT_ACCESS);
        ia64_gen_check_nat_semaphore(insn, op->source, IA64_NAT_ACCESS);
        tcg_gen_mov_i64(value, ia64_gr_src(op->source));
        ia64_gen_check_alignment_access(ctx, insn, plan.address, plan.size,
                                        true, IA64_ISR_R | IA64_ISR_W);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_check_semaphore_access(tcg_env, plan.address);
        ia64_gen_read_simple_ar(ccv, 32);
        ia64_gen_memory_release(insn);
        ia64_gen_atomic_cmpxchg_i64(
            ctx, cpu_gr[op->destination], plan.address, ccv, value,
            ctx->memory.mmu_idx, plan.memop);
        ia64_gen_memory_acquire(insn);
        ia64_gen_gr_nat_clear(insn, op->destination);
        break;
    }
    case IA64_OP_CMP8XCHG16: {
        TCGv_i64 ccv = tcg_temp_new_i64();
        TCGv_i64 csd = tcg_temp_new_i64();
        TCGv_i64 debug_addr = tcg_temp_new_i64();

        ia64_gen_check_nat_semaphore(insn, op->base, IA64_NAT_ACCESS);
        ia64_gen_check_nat_semaphore(insn, op->source, IA64_NAT_ACCESS);
        /*
         * The compare address may select either 8-byte half, but the atomic
         * write and both DBR match datums are the containing 16-byte block.
         */
        tcg_gen_andi_i64(debug_addr, ia64_gr_src(op->base),
                         ~UINT64_C(8));
        ia64_gen_check_data_debug(ctx, insn, debug_addr, 16,
                                  IA64_ISR_R | IA64_ISR_W);
        ia64_gen_check_alignment_only(
            insn, ia64_gr_src(op->base), 8, 8,
            IA64_ALIGNMENT_NATURAL_REQUIRED, IA64_ISR_R | IA64_ISR_W);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_check_semaphore_access(tcg_env, ia64_gr_src(op->base));
        ia64_gen_read_simple_ar(ccv, 32);
        gen_helper_read_ar(csd, tcg_env, tcg_constant_i32(25));
        ia64_gen_memory_release(insn);
        gen_helper_cmp8xchg16(cpu_gr[op->destination], tcg_env,
                              ia64_gr_src(op->base), ccv,
                              ia64_gr_src(op->source), csd);
        ia64_gen_memory_acquire(insn);
        ia64_gen_gr_nat_clear(insn, op->destination);
        break;
    }
    case IA64_OP_FETCHADD4:
    case IA64_OP_FETCHADD8: {
        IA64MemoryPlan plan = ia64_memory_plan(ctx, insn, false);
        TCGv_i64 addend = op->immediate ?
                           tcg_constant_i64(op->immediate) :
                           ia64_gr_src(op->source);
        TCGv_i64 value = tcg_temp_new_i64();

        ia64_gen_check_nat_semaphore(insn, op->base, IA64_NAT_ACCESS);
        if (!op->immediate) {
            ia64_gen_check_nat_semaphore(insn, op->source, IA64_NAT_ACCESS);
        }
        tcg_gen_mov_i64(value, addend);
        ia64_gen_check_alignment_access(ctx, insn, plan.address, plan.size,
                                        true, IA64_ISR_R | IA64_ISR_W);
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_check_semaphore_access(tcg_env, plan.address);
        ia64_gen_memory_release(insn);
        ia64_gen_atomic_fetch_add_i64(
            ctx, cpu_gr[op->destination], plan.address, value,
            ctx->memory.mmu_idx, plan.memop);
        ia64_gen_memory_acquire(insn);
        ia64_gen_gr_nat_clear(insn, op->destination);
        break;
    }
    case IA64_OP_CHK_S:
        if (!insn->check_fp) {
            TCGv_i64 failed = ia64_gen_gr_nat_read(op->source);

            ia64_gen_check_branch(ctx, failed, insn->address + op->immediate,
                                  insn->address, record_iipa,
                                  track_psr_suppression);
        } else {
            ia64_gen_check_branch(ctx, ia64_gen_fr_nat_read(ctx, op->source),
                                  insn->address + op->immediate, insn->address,
                                  record_iipa,
                                  track_psr_suppression);
        }
        break;
    case IA64_OP_CHK_A:
    case IA64_OP_CHK_A_CLR:
        if (!ctx->memory.full_alat && insn->unit == IA64_UNIT_M) {
            ia64_gen_goto_completed(ctx, insn->address + op->immediate,
                                    insn->address, record_iipa,
                                    track_psr_suppression);
            if (skip == NULL) {
                return IA64_GEN_NORETURN;
            }
        } else if (!insn->check_fp && insn->unit == IA64_UNIT_M) {
            const bool clear = insn->opcode == IA64_OP_CHK_A_CLR;
            TCGv_i64 hit = tcg_temp_new_i64();
            TCGv_i64 failed = tcg_temp_new_i64();

            gen_helper_check_load_alat(hit, tcg_env,
                                       tcg_constant_i32(op->source),
                                       tcg_constant_i32(clear));
            tcg_gen_xori_i64(failed, hit, 1);
            ia64_gen_check_branch(ctx, failed, insn->address + op->immediate,
                                  insn->address, record_iipa,
                                  track_psr_suppression);
        } else if (insn->check_fp && insn->unit == IA64_UNIT_M) {
            const bool clear = insn->opcode == IA64_OP_CHK_A_CLR;
            TCGv_i64 hit = tcg_temp_new_i64();
            TCGv_i64 failed = tcg_temp_new_i64();

            gen_helper_check_load_alat_fp(hit, tcg_env,
                                          tcg_constant_i32(op->source),
                                          tcg_constant_i32(clear));
            tcg_gen_xori_i64(failed, hit, 1);
            ia64_gen_check_branch(ctx, failed, insn->address + op->immediate,
                                  insn->address, record_iipa,
                                  track_psr_suppression);
        }
        break;
    case IA64_OP_PROBE_R:
    case IA64_OP_PROBE_W:
    case IA64_OP_PROBE_RW: {
        /*
         * probe.rw exists only in the faulting form (M40), so the
         * read/write query never reaches the non-faulting helper below.
         */
        const bool write_probe = insn->opcode == IA64_OP_PROBE_W ||
                                 insn->opcode == IA64_OP_PROBE_RW;
        const bool rw_probe = insn->opcode == IA64_OP_PROBE_RW;
        TCGv_i64 access_level = insn->probe_imm ?
                                tcg_constant_i64(op->immediate & 3) :
                                ia64_gr_src(op->source);
        TCGv_i64 result = tcg_temp_new_i64();
        uint64_t isr_access =
            insn->opcode == IA64_OP_PROBE_R ? IA64_ISR_R :
            insn->opcode == IA64_OP_PROBE_W ? IA64_ISR_W :
                                              IA64_ISR_R | IA64_ISR_W;
        if (!insn->probe_imm) {
            ia64_gen_check_nat_consumption(insn, op->source,
                                           isr_access | 2,
                                           IA64_NAT_NON_ACCESS);
        }
        ia64_gen_check_nat_consumption(
            insn, op->base,
            isr_access | (insn->probe_fault ? 5 : 2),
            IA64_NAT_NON_ACCESS);
        if (insn->probe_fault) {
            ia64_gen_sync_ip_for_helper(insn);
            gen_helper_probe_fault(tcg_env, ia64_gr_src(op->base),
                                   tcg_constant_i32(write_probe),
                                   tcg_constant_i32(rw_probe),
                                   access_level);
            break;
        }
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_probe(result, tcg_env, ia64_gr_src(op->base),
                         tcg_constant_i32(write_probe), access_level);
        if (op->destination != 0) {
            ia64_gen_gr_write_nat_clear(insn, op->destination, result);
        }
        break;
    }
    case IA64_OP_FC:
    case IA64_OP_FC_I:
        ia64_gen_check_nat_consumption(insn, op->base, IA64_ISR_R | 1,
                                       IA64_NAT_NON_ACCESS);
        gen_helper_fc(tcg_env, ia64_gr_src(op->base),
                      tcg_constant_i32(insn->opcode == IA64_OP_FC_I));
        break;
    case IA64_OP_INVALA:
        if (ctx->memory.full_alat) {
            gen_helper_invala(tcg_env);
        }
        break;
    case IA64_OP_INVALAT:
        if (!ctx->memory.full_alat) {
            break;
        } else if (insn->check_fp) {
            gen_helper_invalidate_alat_fp_reg(
                tcg_env, tcg_constant_i32(op->destination));
        } else {
            gen_helper_invalidate_alat_reg(tcg_env,
                                           tcg_constant_i32(op->destination));
        }
        break;
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
