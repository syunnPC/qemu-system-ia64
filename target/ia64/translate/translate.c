/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 TCG translation pipeline and translation-block lifecycle.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/log.h"

#include "arch/arch.h"
#include "cpu.h"
#include "debug.h"
#include "decode/decode.h"
#include "decoder.h"
#include "ia32/translate.h"
#include "translate/translate.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translation-block.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"

TCGv_i64 cpu_ip;
TCGv_i64 cpu_gr[IA64_GR_COUNT];
TCGv_i64 cpu_pr[IA64_PR_COUNT];
TCGv_i64 cpu_br[IA64_BR_COUNT];
TCGv_i64 cpu_psr;
static TCGv_i64 cpu_fr[IA64_FR_COUNT];
static TCGv_i64 cpu_fr_nat[2];
static TCGv_i64 cpu_fr_sig[2];
static TCGv_i64 cpu_fr_ext_valid[2];
static TCGv_i64 cpu_fr_int_origin[2];
static TCGv_i64 cpu_nat[2];
static TCGv_i64 cpu_rse_gr_dirty[2];

#define IA64_COUNTED_SELF_BUDGET 4096
#define IA64_CLOOP_FILL_ST1_MAX IA64_COUNTED_SELF_BUDGET

static bool ia64_current_insn_cannot_execute(
    const DisasContext *ctx, const Ia64Instruction *insn);
static void ia64_forget_fp_written_formats(DisasContext *ctx,
                                           const Ia64Instruction *insn);

static void ia64_gen_merced_dtlb1_touch(DisasContext *ctx, TCGv_i64 addr,
                                        int mmu_idx, MemOp memop)
{
    if (mmu_idx != MMU_PHYS_IDX &&
        ia64_env_cpu_class(ctx->env)->model == IA64_CPU_MODEL_MERCED) {
        unsigned size = ia64_memop_size(memop);
        unsigned page_size = 1U << IA64_DTLB1_LOOKUP_PAGE_SHIFT;
        TCGv_i64 index = tcg_temp_new_i64();
        TCGv_i64 field = tcg_temp_new_i64();
        TCGv_i64 rid = tcg_temp_new_i64();
        TCGv_i64 match = tcg_temp_new_i64();
        TCGv_ptr entry = tcg_temp_new_ptr();
        TCGLabel *slow = gen_new_label();
        TCGLabel *done = gen_new_label();
        const size_t base = offsetof(CPUIA64State, mmu.tlb_data_l1);

        if (size > 1) {
            tcg_gen_andi_i64(field, addr, page_size - 1);
            tcg_gen_brcondi_i64(TCG_COND_GTU, field, page_size - size, slow);
        }
        tcg_gen_ld_i64(field, tcg_env,
                       offsetof(CPUIA64State, mmu.tlb_data_l1_clock));
        tcg_gen_brcondi_i64(TCG_COND_EQ, field, UINT64_MAX, slow);
        tcg_gen_ld8u_i64(index, tcg_env,
                         offsetof(CPUIA64State, mmu.tlb_data_l1_last));
        tcg_gen_subi_i64(index, index, 1);
        tcg_gen_brcondi_i64(TCG_COND_GEU, index, IA64_DTLB1_MAX, slow);
        tcg_gen_muli_i64(index, index, sizeof(IA64TlbEntry));
        tcg_gen_trunc_i64_ptr(entry, index);
        tcg_gen_add_ptr(entry, entry, tcg_env);
        tcg_gen_ld8u_i64(field, entry, base + offsetof(IA64TlbEntry, valid));
        tcg_gen_brcondi_i64(TCG_COND_EQ, field, 0, slow);
        tcg_gen_ld_i64(field, entry, base + offsetof(IA64TlbEntry, ps));
        tcg_gen_brcondi_i64(TCG_COND_EQ, field, 0, slow);
        tcg_gen_ld_i64(field, entry, base + offsetof(IA64TlbEntry, va));
        tcg_gen_xor_i64(match, field, addr);
        tcg_gen_ld_i64(field, entry, base + offsetof(IA64TlbEntry, page_mask));
        tcg_gen_and_i64(match, match, field);
        tcg_gen_brcondi_i64(TCG_COND_NE, match, 0, slow);
        tcg_gen_ld32u_i64(rid, entry, base + offsetof(IA64TlbEntry, rid));
        tcg_gen_shri_i64(index, addr, IA64_REGION_SHIFT);
        tcg_gen_shli_i64(index, index, 3);
        tcg_gen_trunc_i64_ptr(entry, index);
        tcg_gen_add_ptr(entry, entry, tcg_env);
        tcg_gen_ld_i64(field, entry, offsetof(CPUIA64State, rr));
        tcg_gen_extract_i64(field, field, IA64_RR_RID_SHIFT, 24);
        tcg_gen_brcond_i64(TCG_COND_EQ, field, rid, done);

        gen_set_label(slow);
        gen_helper_merced_dtlb1_touch(
            tcg_env, addr, tcg_constant_i32(size));
        gen_set_label(done);
    }
}

static void ia64_gen_alat_write_begin(DisasContext *ctx)
{
    if (ctx->memory.full_alat) {
        gen_helper_alat_write_begin(tcg_env);
    }
}

static void ia64_gen_alat_write_end(DisasContext *ctx, TCGv_i64 addr,
                                    uint32_t size)
{
    if (ctx->memory.full_alat) {
        gen_helper_alat_write_end(tcg_env, addr, tcg_constant_i32(size));
    }
}

void ia64_gen_qemu_ld_i64(DisasContext *ctx, TCGv_i64 value, TCGv_i64 addr,
                          int mmu_idx, MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    tcg_gen_qemu_ld_i64(value, addr, mmu_idx, memop);
}

void ia64_gen_qemu_st_i64(DisasContext *ctx, TCGv_i64 value, TCGv_i64 addr,
                          int mmu_idx, MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    ia64_gen_alat_write_begin(ctx);
    tcg_gen_qemu_st_i64(value, addr, mmu_idx, memop);
    ia64_gen_alat_write_end(ctx, addr, ia64_memop_size(memop));
}

void ia64_gen_qemu_ld_i128(DisasContext *ctx, TCGv_i128 value, TCGv_i64 addr,
                           int mmu_idx, MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    tcg_gen_qemu_ld_i128(value, addr, mmu_idx, memop);
}

void ia64_gen_qemu_st_i128(DisasContext *ctx, TCGv_i128 value, TCGv_i64 addr,
                           int mmu_idx, MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    ia64_gen_alat_write_begin(ctx);
    tcg_gen_qemu_st_i128(value, addr, mmu_idx, memop);
    ia64_gen_alat_write_end(ctx, addr, 16);
}

void ia64_gen_atomic_xchg_i64(DisasContext *ctx, TCGv_i64 result,
                              TCGv_i64 addr, TCGv_i64 value, int mmu_idx,
                              MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    ia64_gen_alat_write_begin(ctx);
    tcg_gen_atomic_xchg_i64(result, addr, value, mmu_idx, memop);
    ia64_gen_alat_write_end(ctx, addr, ia64_memop_size(memop));
}

void ia64_gen_atomic_cmpxchg_i64(DisasContext *ctx, TCGv_i64 result,
                                 TCGv_i64 addr, TCGv_i64 cmp,
                                 TCGv_i64 value, int mmu_idx, MemOp memop)
{
    unsigned size = ia64_memop_size(memop);
    TCGLabel *load_only = NULL;
    TCGLabel *done;

    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    if (ctx->restart.exit_after_bundle) {
        gen_helper_cmpxchg(result, tcg_env, addr, cmp, value,
                           tcg_constant_i32(size));
        return;
    }
    done = gen_new_label();
    if (size < 8) {
        load_only = gen_new_label();
        tcg_gen_brcondi_i64(TCG_COND_GTU, cmp,
                            (1ULL << (size * 8)) - 1, load_only);
    }
    ia64_gen_alat_write_begin(ctx);
    tcg_gen_atomic_cmpxchg_i64(result, addr, cmp, value, mmu_idx, memop);
    if (ctx->memory.full_alat) {
        TCGLabel *failed = gen_new_label();

        tcg_gen_brcond_i64(TCG_COND_NE, result, cmp, failed);
        ia64_gen_alat_write_end(ctx, addr, size);
        tcg_gen_br(done);
        gen_set_label(failed);
        gen_helper_alat_write_cancel(tcg_env);
    }
    if (load_only) {
        tcg_gen_br(done);
        gen_set_label(load_only);
        tcg_gen_qemu_ld_i64(result, addr, mmu_idx, memop);
    }
    gen_set_label(done);
}

void ia64_gen_atomic_fetch_add_i64(DisasContext *ctx, TCGv_i64 result,
                                   TCGv_i64 addr, TCGv_i64 value, int mmu_idx,
                                   MemOp memop)
{
    ia64_gen_merced_dtlb1_touch(ctx, addr, mmu_idx, memop);
    ia64_gen_alat_write_begin(ctx);
    tcg_gen_atomic_fetch_add_i64(result, addr, value, mmu_idx, memop);
    ia64_gen_alat_write_end(ctx, addr, ia64_memop_size(memop));
}

static bool ia64_insn_may_set_fault_suppression(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_SSM:
        return insn->operands.system.immediate & IA64_PSR_FAULT_SUPPRESS_MASK;
    case IA64_OP_MOV_GRPSR:
        return insn->operands.system.immediate == 0;
    default:
        return false;
    }
}

static bool ia64_insn_may_set_psr_ic(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_SSM:
        return insn->operands.system.immediate & IA64_PSR_IC;
    case IA64_OP_MOV_GRPSR:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_may_modify_psr_ic(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_SSM:
    case IA64_OP_RSM:
        return insn->operands.system.immediate & IA64_PSR_IC;
    case IA64_OP_MOV_GRPSR:
    case IA64_OP_BREAK:
    case IA64_OP_RFI:
    case IA64_OP_BR_IA:
        return true;
    default:
        return false;
    }
}

static void ia64_update_psr_ic_known(DisasContext *ctx,
                                     const Ia64Instruction *insn,
                                     bool unconditional)
{
    if (!ia64_insn_may_modify_psr_ic(insn)) {
        return;
    }
    if (insn->opcode == IA64_OP_SSM || insn->opcode == IA64_OP_RSM) {
        bool value = insn->opcode == IA64_OP_SSM;

        if (unconditional) {
            ctx->restart.psr_ic_known = true;
            ctx->restart.psr_ic = value;
        } else if (ctx->restart.psr_ic != value) {
            ctx->restart.psr_ic_known = false;
        }
    } else {
        ctx->restart.psr_ic_known = false;
    }
}

static void ia64_invalidate_fp_state(DisasContext *ctx,
                                     const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_SSM:
    case IA64_OP_RSM:
        if (insn->operands.system.immediate & IA64_PSR_DFL) {
            ctx->fp.enabled_sets &= ~1;
        }
        if (insn->operands.system.immediate & IA64_PSR_DFH) {
            ctx->fp.enabled_sets &= ~2;
        }
        /* fall through */
    case IA64_OP_RUM:
    case IA64_OP_SUM_UM:
        if (insn->operands.system.immediate & IA64_PSR_MFL) {
            ctx->fp.written_sets &= ~1;
        }
        if (insn->operands.system.immediate & IA64_PSR_MFH) {
            ctx->fp.written_sets &= ~2;
        }
        break;
    case IA64_OP_MOV_GRPSR:
        ctx->fp.enabled_sets = 0;
        /* fall through */
    case IA64_OP_MOV_GRUM:
        ctx->fp.written_sets = 0;
        break;
    case IA64_OP_BREAK:
    case IA64_OP_RFI:
    case IA64_OP_BR_IA:
        ctx->fp = (IA64TranslationFPState) { 0 };
        break;
    case IA64_OP_COVER:
    case IA64_OP_CLRRRB:
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CTOP:
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
        ctx->fp.rotating_synced = false;
        memset(ctx->fp.format + 32, 0, IA64_FR_COUNT - 32);
        break;
    default:
        break;
    }
}

static bool ia64_insn_may_modify_native_trap_psr(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_SSM:
    case IA64_OP_RSM:
        /* Of the native-trap state, these immediates can change only IC. */
        return insn->operands.system.immediate & IA64_PSR_IC;
    case IA64_OP_MOV_GRPSR:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_may_modify_psr_ri(const Ia64Instruction *insn)
{
    return insn->opcode == IA64_OP_MOV_GRPSR;
}

static bool ia64_insn_may_modify_cpl(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_MOV_GRPSR:
    case IA64_OP_SSM:
    case IA64_OP_RSM:
    case IA64_OP_EPC:
    case IA64_OP_RFI:
    case IA64_OP_BREAK:
    case IA64_OP_BR_IA:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_may_modify_cfm_sof(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_ALLOC:
    case IA64_OP_COVER:
    case IA64_OP_LOADRS:
    case IA64_OP_RFI:
    case IA64_OP_BREAK:
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_BR_IA:
        return true;
    default:
        return false;
    }
}

static void ia64_update_cached_register_state(
    DisasContext *ctx, const Ia64Instruction *insn)
{
    if (ia64_current_insn_cannot_execute(ctx, insn)) {
        return;
    }
    if (ia64_insn_may_modify_cpl(insn)) {
        ctx->reg.cpl_known = false;
    }
    if (ia64_insn_may_modify_cfm_sof(insn)) {
        ctx->reg.cfm_sof_valid = false;
        ctx->reg.cfm_sof_checked = 0;
    }

    /*
     * ALLOC is architecturally unpredicated and installs an immediate SOF,
     * so retain that value rather than reloading it for the next frame
     * check.
     */
    if (insn->opcode == IA64_OP_ALLOC) {
        uint8_t new_sof = insn->operands.common.immediate & 0x7f;

        ctx->reg.cfm_sof = tcg_constant_i32(new_sof);
        ctx->reg.cfm_sof_checked = new_sof;
        ctx->reg.cfm_sof_valid = true;
    }
}

bool ia64_insn_is_empty_hint(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_NOP:
    case IA64_OP_HINT_I:
    case IA64_OP_HINT_B:
    case IA64_OP_HINT_F:
    case IA64_OP_HINT_X:
    case IA64_OP_BRP:
        return true;
    default:
        return false;
    }
}

static bool ia64_instruction_address_matches_physical_entry(CPUIA64State *env,
                                                            uint64_t address,
                                                            uint64_t entry_pa)
{
    const IA64TlbEntry *entry;
    uint64_t pa;
    uint8_t perm;
    uint32_t rid;

    if (!(env->psr & IA64_PSR_IT)) {
        return address == entry_pa;
    }

    if (ia64_firmware_identity_pa(env->cr_iva, env->psr,
                                  address, &pa)) {
        return pa == entry_pa;
    }

    rid = ia64_region_rid(env, address);
    entry = ia64_tlb_find_cached(env, address, rid, true);
    if (entry) {
        ia64_tlb_entry_translate(entry, address, ia64_psr_cpl(env->psr),
                                 &pa, &perm);
    }
    if (entry && (perm & IA64_TLB_X)) {
        return pa == entry_pa;
    }

    return false;
}

bool ia64_is_pal_proc_break(CPUIA64State *env, uint64_t address)
{
    const uint64_t pal_proc_entry_pa = IA64_FW_IDENTITY_BASE + 0x60;

    if (ia64_instruction_address_matches_physical_entry(
            env, address, pal_proc_entry_pa)) {
        return true;
    }

    return env->pal.pal_proc_copy_valid &&
           ia64_instruction_address_matches_physical_entry(
               env, address, env->pal.pal_proc_copy_addr);
}

bool ia64_is_firmware_debug_break(uint64_t address, uint64_t imm)
{
    if (imm == 0x100002) {
        return address >= IA64_FIRMWARE_IVT_BASE &&
               address < IA64_FIRMWARE_IVT_BASE + 0x8000;
    }
    if (imm == 0x100003 || imm == 0x100004) {
        return address >= IA64_FW_IDENTITY_BASE &&
               address < IA64_FW_IDENTITY_BASE + IA64_FW_IDENTITY_SIZE;
    }
    return false;
}

static MemOp ia64_memop_with_endian(MemOp memop, bool big_endian)
{
    if ((memop & MO_SIZE) == MO_8) {
        return memop & ~MO_BSWAP;
    }
    return (memop & ~MO_BSWAP) | (big_endian ? MO_BE : MO_LE);
}

MemOp ia64_data_memop(DisasContext *ctx, MemOp memop)
{
    return ia64_memop_with_endian(memop, ctx->memory.be_data);
}

static bool ia64_insn_has_base_update(const Ia64Instruction *insn)
{
    return insn->reg_base_update || insn->imm_base_update;
}

static bool ia64_load_base_update_has_same_target(const Ia64Instruction *insn)
{
    return ia64_opcode_is_load(insn->opcode) &&
           ia64_insn_has_base_update(insn) &&
           insn->operands.common.destination == insn->operands.common.source2;
}

TCGv_i64 ia64_gr_src(uint8_t reg)
{
    return reg == 0 ? tcg_constant_i64(0) : cpu_gr[reg];
}

bool ia64_insn_must_start_group(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_ALLOC:
    case IA64_OP_FLUSHRS:
    case IA64_OP_LOADRS:
        return true;
    default:
        return false;
    }
}

bool ia64_insn_must_end_group(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_BSW0:
    case IA64_OP_BSW1:
    case IA64_OP_CLRRRB:
    case IA64_OP_CLRRRB_PR:
    case IA64_OP_COVER:
    case IA64_OP_ITC_D:
    case IA64_OP_ITC_I:
    case IA64_OP_RFI:
        return true;
    case IA64_OP_PTC_G:
    case IA64_OP_PTC_GA:
        /* Global purges without a stop are undefined, not required to fault. */
        return false;
    default:
        return false;
    }
}

bool ia64_insn_requires_slot2(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CLOOP:
    case IA64_OP_BR_CTOP:
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
        return true;
    default:
        return false;
    }
}

bool ia64_insn_has_invalid_fp_target(const Ia64Instruction *insn)
{
    Ia64Opcode opcode = insn->opcode;

    switch (insn->opcode) {
    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS: {
        uint8_t f1 = insn->operands.common.destination;
        uint8_t f2 = insn->operands.common.source1;

        if (f1 <= 1 || f2 <= 1) {
            return true;
        }
        /* If exactly one operand rotates, RRB.FR determines its physical
         * bank and the conflict check must be generated at run time.
         */
        return (f1 < IA64_FR_ROTATING_BASE) ==
               (f2 < IA64_FR_ROTATING_BASE) && ((f1 ^ f2) & 1) == 0;
    }
    default:
        break;
    }

    if ((opcode >= IA64_OP_FADD && opcode <= IA64_OP_XMPY_HU) ||
        (opcode >= IA64_OP_FMOV && opcode <= IA64_OP_FCVT_FXU) ||
        (opcode >= IA64_OP_SETF_D && opcode <= IA64_OP_SETF_EXP) ||
        (opcode >= IA64_OP_FMERGE_S && opcode <= IA64_OP_FPRCPA) ||
        (opcode >= IA64_OP_FMS && opcode <= IA64_OP_FPNMA)) {
        return insn->operands.common.destination <= 1;
    }

    switch (opcode) {
    case IA64_OP_LDFD:
    case IA64_OP_LDFS:
    case IA64_OP_LDF_FILL:
    case IA64_OP_LDF8:
    case IA64_OP_LDFE:
    case IA64_OP_FMERGE:
    case IA64_OP_SETF_SIG:
        return insn->operands.common.destination <= 1;
    default:
        return false;
    }
}

static void ia64_gen_check_fp_pair_bank(const Ia64Instruction *insn)
{
    uint8_t f1;
    uint8_t f2;
    TCGv_i32 rrb_parity;
    TCGLabel *valid;

    switch (insn->opcode) {
    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
        break;
    default:
        return;
    }

    f1 = insn->operands.common.destination;
    f2 = insn->operands.common.source1;
    if ((f1 < IA64_FR_ROTATING_BASE) == (f2 < IA64_FR_ROTATING_BASE)) {
        return;
    }

    rrb_parity = tcg_temp_new_i32();
    valid = gen_new_label();
    tcg_gen_ld8u_i32(rrb_parity, tcg_env,
                     offsetof(CPUIA64State, cfm_rrb_fr));
    tcg_gen_andi_i32(rrb_parity, rrb_parity, 1);
    /* Physical bank parity is logical parity XOR RRB.FR parity for a
     * rotating FR.  A load pair is legal only when the two parities differ.
     */
    tcg_gen_brcondi_i32(((f1 ^ f2) & 1) ? TCG_COND_EQ : TCG_COND_NE,
                        rrb_parity, 0, valid);
    ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                             insn->raw, insn->slot);
    gen_set_label(valid);
}

typedef enum IA64NatResultPolicy {
    IA64_NAT_RESULT_UNKNOWN,
    IA64_NAT_RESULT_CLEAR,
    IA64_NAT_RESULT_SOURCE_R1,
    IA64_NAT_RESULT_SOURCE_R2,
    IA64_NAT_RESULT_SOURCE_R3,
    IA64_NAT_RESULT_SOURCES_R2_R3,
    IA64_NAT_RESULT_SOURCES_R1_R2_R3,
    IA64_NAT_RESULT_PRESERVE_ON_FULL_ALAT,
    IA64_NAT_RESULT_SET_IF_R3_SET,
    IA64_NAT_RESULT_R3_OR_UNIMPLEMENTED_VA,
} IA64NatResultPolicy;

typedef enum IA64GeneratorKind {
    IA64_GENERATOR_NONE,
    IA64_GENERATOR_INTEGER,
    IA64_GENERATOR_MEMORY,
    IA64_GENERATOR_FP,
    IA64_GENERATOR_SIMD,
    IA64_GENERATOR_SYSTEM,
    IA64_GENERATOR_BRANCH,
} IA64GeneratorKind;

typedef struct IA64OpcodeProperties {
    uint8_t generator;
    uint8_t nat_policy;
    bool writes_gr;
} IA64OpcodeProperties;

static const IA64OpcodeProperties ia64_opcode_properties[IA64_OP_COUNT] = {
#define IA64_OPCODE(name, generator, gr, nat) \
    [IA64_OP_ ## name] = { IA64_GENERATOR_ ## generator, \
                          IA64_NAT_RESULT_ ## nat, gr },
#include "target/ia64/decode/opcode.inc"
#undef IA64_OPCODE
};

static bool ia64_insn_writes_gr_r1(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_PROBE_R:
    case IA64_OP_PROBE_W:
    case IA64_OP_PROBE_RW:
        return !insn->probe_fault;
    default:
        return (unsigned)insn->opcode < IA64_OP_COUNT &&
               ia64_opcode_properties[insn->opcode].writes_gr;
    }
}

typedef enum IA64KnownNat {
    IA64_NAT_UNKNOWN = -1,
    IA64_NAT_CLEAR = 0,
    IA64_NAT_SET = 1,
} IA64KnownNat;

static IA64KnownNat ia64_nat_known(const DisasContext *ctx, uint8_t reg)
{
    uint64_t bit;

    if (reg == 0) {
        return IA64_NAT_CLEAR;
    }
    bit = UINT64_C(1) << (reg % 64);
    if (ctx->memory.nat_known_clear[reg / 64] & bit) {
        return IA64_NAT_CLEAR;
    }
    if (ctx->memory.nat_known_set[reg / 64] & bit) {
        return IA64_NAT_SET;
    }
    return IA64_NAT_UNKNOWN;
}

static void ia64_nat_set_known(DisasContext *ctx, uint8_t reg,
                               IA64KnownNat value)
{
    uint32_t word;
    uint64_t bit;

    if (reg == 0) {
        return;
    }
    word = reg / 64;
    bit = 1ULL << (reg % 64);
    ctx->memory.nat_known_clear[word] &= ~bit;
    ctx->memory.nat_known_set[word] &= ~bit;
    if (value == IA64_NAT_CLEAR) {
        ctx->memory.nat_known_clear[word] |= bit;
    } else if (value == IA64_NAT_SET) {
        ctx->memory.nat_known_set[word] |= bit;
    }
}

static IA64KnownNat ia64_nat_merge(IA64KnownNat a, IA64KnownNat b)
{
    return a == b ? a : IA64_NAT_UNKNOWN;
}

static IA64KnownNat ia64_nat_or(IA64KnownNat a, IA64KnownNat b)
{
    if (a == IA64_NAT_SET || b == IA64_NAT_SET) {
        return IA64_NAT_SET;
    }
    if (a == IA64_NAT_CLEAR && b == IA64_NAT_CLEAR) {
        return IA64_NAT_CLEAR;
    }
    return IA64_NAT_UNKNOWN;
}

static IA64NatResultPolicy ia64_nat_result_policy(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_PSHR2:
    case IA64_OP_PSHR2_U:
    case IA64_OP_PSHR4:
    case IA64_OP_PSHR4_U:
        return insn->operands.common.immediate >= 0 ?
               IA64_NAT_RESULT_SOURCE_R3 :
               IA64_NAT_RESULT_SOURCES_R2_R3;
    case IA64_OP_PSHL2:
    case IA64_OP_PSHL4:
        return insn->operands.common.immediate >= 0 ?
               IA64_NAT_RESULT_SOURCE_R2 :
               IA64_NAT_RESULT_SOURCES_R2_R3;
    case IA64_OP_GETF_D:
    case IA64_OP_GETF_S:
    case IA64_OP_GETF_EXP:
    case IA64_OP_GETF_SIG:
        return insn->operands.common.source1 <= 1 ?
               IA64_NAT_RESULT_CLEAR : IA64_NAT_RESULT_UNKNOWN;
    default:
        return (unsigned)insn->opcode < IA64_OP_COUNT ?
               ia64_opcode_properties[insn->opcode].nat_policy :
               IA64_NAT_RESULT_UNKNOWN;
    }
}

static IA64KnownNat ia64_nat_result_known(const DisasContext *ctx,
                                          const Ia64Instruction *insn,
                                          IA64KnownNat old_r1)
{
    IA64KnownNat r2 =
        ia64_nat_known(ctx, insn->operands.common.source1);
    IA64KnownNat r3 =
        ia64_nat_known(ctx, insn->operands.common.source2);

    switch (ia64_nat_result_policy(insn)) {
    case IA64_NAT_RESULT_CLEAR:
        return IA64_NAT_CLEAR;
    case IA64_NAT_RESULT_SOURCE_R1:
        return old_r1;
    case IA64_NAT_RESULT_SOURCE_R2:
        return r2;
    case IA64_NAT_RESULT_SOURCE_R3:
        return r3;
    case IA64_NAT_RESULT_SOURCES_R2_R3:
        return ia64_nat_or(r2, r3);
    case IA64_NAT_RESULT_SOURCES_R1_R2_R3:
        return ia64_nat_or(old_r1, ia64_nat_or(r2, r3));
    case IA64_NAT_RESULT_PRESERVE_ON_FULL_ALAT:
        return ctx->memory.full_alat ?
               ia64_nat_merge(old_r1, IA64_NAT_CLEAR) : IA64_NAT_CLEAR;
    case IA64_NAT_RESULT_SET_IF_R3_SET:
        return r3 == IA64_NAT_SET ?
               IA64_NAT_SET : IA64_NAT_UNKNOWN;
    case IA64_NAT_RESULT_R3_OR_UNIMPLEMENTED_VA:
        if (r3 == IA64_NAT_SET) {
            return IA64_NAT_SET;
        }
        return ia64_env_cpu_class(ctx->env)->impl_va_msb >= 60 ?
               r3 : IA64_NAT_UNKNOWN;
    case IA64_NAT_RESULT_UNKNOWN:
    default:
        return IA64_NAT_UNKNOWN;
    }
}

static IA64KnownNat ia64_nat_after_predication(const DisasContext *ctx,
                                               IA64KnownNat old,
                                               IA64KnownNat executed)
{
    if (ctx->reg.current_qp_known) {
        return ctx->reg.current_qp_value ? executed : old;
    }
    return ia64_nat_merge(old, executed);
}

static void ia64_invalidate_nat_mask(DisasContext *ctx, uint32_t word,
                                     uint64_t mask)
{
    ctx->memory.nat_known_clear[word] &= ~mask;
    ctx->memory.nat_known_set[word] &= ~mask;
}

static bool ia64_insn_may_rewrite_stacked_gr_view(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_CLRRRB_PR:
        /*
         * clrrrb.pr does not change the GR rename base, but the shared RSE
         * helper still synchronizes the current frame out and back in.
         */
        return true;
    case IA64_OP_ALLOC:
    case IA64_OP_COVER:
    case IA64_OP_CLRRRB:
    case IA64_OP_LOADRS:
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_BR_IA:
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CTOP:
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
    case IA64_OP_RFI:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_may_swap_banked_gr(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_BSW0:
    case IA64_OP_BSW1:
    case IA64_OP_MOV_GRPSR:
    case IA64_OP_RFI:
        return true;
    case IA64_OP_SSM:
    case IA64_OP_RSM:
        return insn->operands.common.immediate & IA64_PSR_BN;
    default:
        return false;
    }
}

static void ia64_invalidate_nat_known_for_insn(
    DisasContext *ctx, const Ia64Instruction *insn)
{
    if (insn->opcode == IA64_OP_BREAK) {
        ctx->memory.nat_known_clear[0] = 1;
        ctx->memory.nat_known_clear[1] = 0;
        ctx->memory.nat_known_set[0] = 0;
        ctx->memory.nat_known_set[1] = 0;
        return;
    }
    if (ia64_insn_may_swap_banked_gr(insn)) {
        ia64_invalidate_nat_mask(
            ctx, 0, MAKE_64BIT_MASK(16, 16));
    }
    if (ia64_insn_may_rewrite_stacked_gr_view(insn)) {
        ia64_invalidate_nat_mask(
            ctx, 0, MAKE_64BIT_MASK(IA64_STACKED_GR_BASE,
                                    64 - IA64_STACKED_GR_BASE));
        ia64_invalidate_nat_mask(ctx, 1, UINT64_MAX);
    }
}

/*
 * Return true when executing insn cannot introduce a GR NaT into a state in
 * which every GR NaT is clear.  This is deliberately stricter than the
 * ordinary per-register dataflow: it protects both an internal TCG back edge
 * and direct chaining to a TB keyed by IA64_TB_FLAG_NAT_CLEAR.
 */
static bool ia64_insn_preserves_all_nat_clear(
    const DisasContext *ctx, const Ia64Instruction *insn)
{
    if (!insn->valid || insn->placement_illegal || insn->reserved_field ||
        insn->opcode == IA64_OP_BREAK ||
        ia64_insn_may_swap_banked_gr(insn) ||
        ia64_insn_may_rewrite_stacked_gr_view(insn)) {
        return false;
    }

    if (!ia64_insn_writes_gr_r1(insn)) {
        return true;
    }

    switch (ia64_nat_result_policy(insn)) {
    case IA64_NAT_RESULT_CLEAR:
    case IA64_NAT_RESULT_SOURCE_R1:
    case IA64_NAT_RESULT_SOURCE_R2:
    case IA64_NAT_RESULT_SOURCE_R3:
    case IA64_NAT_RESULT_SOURCES_R2_R3:
    case IA64_NAT_RESULT_SOURCES_R1_R2_R3:
    case IA64_NAT_RESULT_PRESERVE_ON_FULL_ALAT:
        return true;
    case IA64_NAT_RESULT_R3_OR_UNIMPLEMENTED_VA:
        return ia64_env_cpu_class(ctx->env)->impl_va_msb >= 60;
    case IA64_NAT_RESULT_SET_IF_R3_SET:
    case IA64_NAT_RESULT_UNKNOWN:
    default:
        return false;
    }
}

void ia64_update_nat_known(DisasContext *ctx,
                           const Ia64Instruction *insn)
{
    IA64KnownNat old_r1;
    IA64KnownNat old_r2;
    IA64KnownNat old_r3;

    if (ia64_current_insn_cannot_execute(ctx, insn)) {
        return;
    }

    ia64_invalidate_nat_known_for_insn(ctx, insn);
    old_r1 = ia64_nat_known(ctx, insn->operands.common.destination);
    old_r2 = ia64_nat_known(ctx, insn->operands.common.source1);
    old_r3 = ia64_nat_known(ctx, insn->operands.common.source2);

    if (ia64_insn_writes_gr_r1(insn)) {
        IA64KnownNat result = ia64_nat_result_known(ctx, insn, old_r1);

        /* A predicated-off instruction preserves the old destination. */
        ia64_nat_set_known(
            ctx, insn->operands.common.destination,
            ia64_nat_after_predication(ctx, old_r1, result));
    }

    if (insn->reg_base_update) {
        /* The executed update assigns base.NaT | increment.NaT. */
        ia64_nat_set_known(
            ctx, insn->operands.common.source2,
            ia64_nat_after_predication(ctx, old_r3,
                                       ia64_nat_or(old_r3, old_r2)));
    }
    /* An immediate base update preserves the base register's NaT bit. */
}

static bool ia64_insn_is_privileged(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_BSW0:
    case IA64_OP_BSW1:
    case IA64_OP_RFI:
    case IA64_OP_MOV_CRGR:
    case IA64_OP_MOV_GRCR:
    case IA64_OP_MOV_GRPSR:
    case IA64_OP_MOV_PSRGR:
    case IA64_OP_MOV_RRGR:
    case IA64_OP_MOV_GRRR:
    case IA64_OP_MOV_PKRGR:
    case IA64_OP_MOV_PKRGR_INDEXED:
    case IA64_OP_MOV_GRPKR:
    case IA64_OP_MOV_GRPKR_INDEXED:
    case IA64_OP_MOV_IBRGR:
    case IA64_OP_MOV_GRIBR:
    case IA64_OP_MOV_IBRGR_INDEXED:
    case IA64_OP_MOV_GRIBR_INDEXED:
    case IA64_OP_MOV_DBRGR:
    case IA64_OP_MOV_GRDBR:
    case IA64_OP_MOV_DBRGR_INDEXED:
    case IA64_OP_MOV_GRDBR_INDEXED:
    case IA64_OP_MOV_PMCGR:
    case IA64_OP_MOV_GRPMC:
    case IA64_OP_MOV_PMCGR_INDEXED:
    case IA64_OP_MOV_GRPMC_INDEXED:
    case IA64_OP_MOV_GRPMD:
    case IA64_OP_MOV_GRPMD_INDEXED:
    case IA64_OP_MOV_MSRGR:
    case IA64_OP_MOV_GRMSR:
    case IA64_OP_ITC_D:
    case IA64_OP_ITC_I:
    case IA64_OP_ITR_D:
    case IA64_OP_ITR_I:
    case IA64_OP_SSM:
    case IA64_OP_RSM:
    case IA64_OP_PTC_G:
    case IA64_OP_PTC_GA:
    case IA64_OP_PTC_L:
    case IA64_OP_PTR_D:
    case IA64_OP_PTR_I:
    case IA64_OP_PTC_E:
    case IA64_OP_TAK:
    case IA64_OP_TPA:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_psr_ic_must_be_clear(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_ITC_D:
    case IA64_OP_ITC_I:
    case IA64_OP_ITR_D:
    case IA64_OP_ITR_I:
        return true;
    case IA64_OP_MOV_CRGR:
    case IA64_OP_MOV_GRCR:
        /* CR16 through CR25 are implemented interruption registers. */
        return insn->operands.common.source1 >= 16 &&
               insn->operands.common.source1 <= 25;
    default:
        return false;
    }
}

static bool ia64_reserved_ar_for_unit(uint8_t ar, IA64SlotUnit unit,
                                      bool write)
{
    if (unit == IA64_UNIT_I) {
        return ar <= 47 || (ar >= 67 && ar <= 111);
    }

    if ((ar >= 8 && ar <= 15) || ar == 20 ||
        (ar >= 22 && ar <= 23) || ar == 31 ||
        (ar >= 33 && ar <= 35) || (ar >= 37 && ar <= 39) ||
        (ar >= 41 && ar <= 43) || (ar >= 46 && ar <= 47) ||
        (ar >= 64 && ar <= 111)) {
        return true;
    }
    return write && ar == 17;
}

static bool ia64_insn_has_reserved_ar(const Ia64Instruction *insn)
{
    const DisasContext *ctx = insn->ctx;
    uint8_t ar;

    switch (insn->opcode) {
    case IA64_OP_MOV_ARGR:
        ar = insn->operands.common.source1;
        if (ar == IA64_AR_RUC &&
            !(ia64_env_cpu_class(ctx->env)->cpuid_features &
              IA64_CPUID4_RU)) {
            return true;
        }
        return ia64_reserved_ar_for_unit(ar, insn->unit, false);
    case IA64_OP_MOV_GRAR:
    case IA64_OP_MOV_IMMAR:
        ar = insn->operands.common.source1;
        if (ar == IA64_AR_RUC &&
            !(ia64_env_cpu_class(ctx->env)->cpuid_features &
              IA64_CPUID4_RU)) {
            return true;
        }
        return ia64_reserved_ar_for_unit(ar, insn->unit, true);
    default:
        return false;
    }
}

static bool ia64_reserved_cr(uint8_t cr)
{
    return (cr >= 3 && cr <= 7) || (cr >= 10 && cr <= 15) ||
           cr == 18 || (cr >= 26 && cr <= 63) ||
           (cr >= 75 && cr <= 79) || (cr >= 82);
}

bool ia64_insn_has_illegal_register(const Ia64Instruction *insn)
{
    if (ia64_insn_has_reserved_ar(insn)) {
        return true;
    }
    switch (insn->opcode) {
    case IA64_OP_MOV_CRGR:
        return ia64_reserved_cr(insn->operands.common.source1);
    case IA64_OP_MOV_GRCR:
        return ia64_reserved_cr(insn->operands.common.source1) ||
               ia64_cr_is_read_only(insn->operands.common.source1);
    default:
        return false;
    }
}

bool ia64_insn_has_reserved_mask_field(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_RUM:
    case IA64_OP_SUM_UM:
        return insn->operands.common.immediate & ~0x3eULL;
    default:
        return false;
    }
}
static bool ia64_fr_is_writable(uint8_t reg)
{
    return reg > 1;
}


static void ia64_gen_fr_nat_clear(uint8_t reg)
{
    if (reg <= 1) {
        return;
    }

    tcg_gen_andi_i64(cpu_fr_nat[reg / 64], cpu_fr_nat[reg / 64],
                     ~(1ULL << (reg % 64)));
}

static void ia64_gen_fr_sig_clear(uint8_t reg)
{
    if (reg <= 1) {
        return;
    }

    tcg_gen_andi_i64(cpu_fr_sig[reg / 64], cpu_fr_sig[reg / 64],
                     ~(1ULL << (reg % 64)));
    tcg_gen_andi_i64(cpu_fr_int_origin[reg / 64],
                     cpu_fr_int_origin[reg / 64],
                     ~(1ULL << (reg % 64)));
}

static void ia64_gen_fr_sig_set(uint8_t reg)
{
    if (reg <= 1) {
        return;
    }

    tcg_gen_ori_i64(cpu_fr_sig[reg / 64], cpu_fr_sig[reg / 64],
                    1ULL << (reg % 64));
    tcg_gen_ori_i64(cpu_fr_int_origin[reg / 64],
                    cpu_fr_int_origin[reg / 64],
                    1ULL << (reg % 64));
    tcg_gen_st_i64(cpu_fr[reg], tcg_env,
                   offsetof(CPUIA64State, fp.fr_int_value[reg]));
}

TCGv_i64 ia64_gen_fr_sig_read(DisasContext *ctx, uint8_t reg)
{
    TCGv_i64 bit;

    if (reg <= 1) {
        return tcg_constant_i64(1);
    }
    if (ctx->fp.format[reg] != IA64_FR_FORMAT_UNKNOWN) {
        return tcg_constant_i64(ctx->fp.format[reg] == IA64_FR_FORMAT_SIG);
    }

    bit = tcg_temp_new_i64();
    tcg_gen_shri_i64(bit, cpu_fr_sig[reg / 64], reg % 64);
    tcg_gen_andi_i64(bit, bit, 1);
    return bit;
}

TCGv_i64 ia64_gen_fr_sig_mask_read(DisasContext *ctx, uint32_t word,
                                   uint64_t mask)
{
    TCGv_i64 bits;
    uint64_t pending = mask;
    uint64_t known = 0;

    g_assert(word < ARRAY_SIZE(cpu_fr_sig));
    while (pending) {
        unsigned bit = ctz64(pending);
        unsigned format = ctx->fp.format[word * 64 + bit];

        pending &= pending - 1;
        if (format != IA64_FR_FORMAT_UNKNOWN) {
            mask &= ~(UINT64_C(1) << bit);
            if (format == IA64_FR_FORMAT_SIG) {
                known |= UINT64_C(1) << bit;
            }
        }
    }
    if (!mask) {
        return tcg_constant_i64(known);
    }
    bits = tcg_temp_new_i64();
    tcg_gen_andi_i64(bits, cpu_fr_sig[word], mask);
    tcg_gen_ori_i64(bits, bits, known);
    return bits;
}

TCGv_i64 ia64_fr_significand_src(uint8_t reg)
{
    if (reg == 0) {
        return tcg_constant_i64(0);
    }
    if (reg == 1) {
        return tcg_constant_i64(1ULL << 63);
    }
    return cpu_fr[reg];
}

TCGv_i64 ia64_fr_binary_src(uint8_t reg)
{
    if (reg == IA64_FR_ZERO_INDEX) {
        return tcg_constant_i64(0);
    }
    if (reg == IA64_FR_ONE_INDEX) {
        return tcg_constant_i64(IA64_FR_ONE);
    }
    return cpu_fr[reg];
}

TCGv_i64 ia64_gen_fr_special_read(DisasContext *ctx, uint8_t reg)
{
    TCGv_i64 tags;

    if (reg <= 1) {
        return tcg_constant_i64(0);
    }
    if (ctx->fp.format[reg] != IA64_FR_FORMAT_UNKNOWN) {
        return tcg_constant_i64(ctx->fp.format[reg] != IA64_FR_FORMAT_BINARY);
    }

    tags = tcg_temp_new_i64();
    tcg_gen_or_i64(tags, cpu_fr_nat[reg / 64],
                   cpu_fr_sig[reg / 64]);
    tcg_gen_or_i64(tags, tags, cpu_fr_ext_valid[reg / 64]);
    tcg_gen_or_i64(tags, tags, cpu_fr_int_origin[reg / 64]);
    tcg_gen_shri_i64(tags, tags, reg % 64);
    tcg_gen_andi_i64(tags, tags, 1);
    return tags;
}

void ia64_gen_fr_read_double(DisasContext *ctx, TCGv_i64 result, uint8_t reg)
{
    TCGLabel *slow;
    TCGLabel *done;

    if (reg <= 1 || ctx->fp.format[reg] == IA64_FR_FORMAT_BINARY) {
        tcg_gen_mov_i64(result, ia64_fr_binary_src(reg));
        return;
    }
    if (ctx->fp.format[reg] != IA64_FR_FORMAT_UNKNOWN) {
        gen_helper_getf(result, tcg_env, tcg_constant_i32(reg),
                        tcg_constant_i32(0));
        return;
    }
    slow = gen_new_label();
    done = gen_new_label();
    tcg_gen_brcondi_i64(TCG_COND_NE, ia64_gen_fr_special_read(ctx, reg),
                        0, slow);
    tcg_gen_mov_i64(result, ia64_fr_binary_src(reg));
    tcg_gen_br(done);
    gen_set_label(slow);
    gen_helper_getf(result, tcg_env, tcg_constant_i32(reg),
                    tcg_constant_i32(0));
    gen_set_label(done);
}

/*
 * Plain fmov can copy both the compact binary representation and a tagged
 * integer significand directly.  NaTVal and extended-register values still
 * need the architectural helper.  Route a lone integer-origin tag through the
 * helper as well because it is not a valid compact representation.  The
 * paired significand and integer-origin tags produced by setf.sig/xma are
 * deliberately accepted.
 */
TCGv_i64 ia64_gen_fr_fmov_slow_read(DisasContext *ctx, uint8_t reg)
{
    TCGv_i64 tags;
    TCGv_i64 orphan_origin;

    if (reg <= 1) {
        return tcg_constant_i64(0);
    }
    if (ctx->fp.format[reg] != IA64_FR_FORMAT_UNKNOWN) {
        return tcg_constant_i64(ctx->fp.format[reg] == IA64_FR_FORMAT_NAT);
    }

    tags = tcg_temp_new_i64();
    orphan_origin = tcg_temp_new_i64();
    tcg_gen_andc_i64(orphan_origin, cpu_fr_int_origin[reg / 64],
                     cpu_fr_sig[reg / 64]);
    tcg_gen_or_i64(tags, cpu_fr_nat[reg / 64],
                   cpu_fr_ext_valid[reg / 64]);
    tcg_gen_or_i64(tags, tags, orphan_origin);
    tcg_gen_shri_i64(tags, tags, reg % 64);
    tcg_gen_andi_i64(tags, tags, 1);
    return tags;
}

static void ia64_gen_fr_ext_clear(uint8_t reg)
{
    if (reg <= 1) {
        return;
    }

    tcg_gen_andi_i64(cpu_fr_ext_valid[reg / 64],
                     cpu_fr_ext_valid[reg / 64],
                     ~(1ULL << (reg % 64)));
}

static void ia64_gen_fr_mark_written(DisasContext *ctx, uint8_t reg)
{
    if (reg > 1) {
        unsigned set = reg >= 32 ? 2 : 1;

        if (!(ctx->fp.written_sets & set)) {
            tcg_gen_ori_i64(cpu_psr, cpu_psr,
                            reg >= 32 ? IA64_PSR_MFH : IA64_PSR_MFL);
        }
        if (reg >= 32 && !ctx->fp.rotating_live) {
            tcg_gen_st8_i32(tcg_constant_i32(1), tcg_env,
                            offsetof(CPUIA64State, fp.rotating_fr_live));
        }
    }
}

static void ia64_gen_fr_nat_set(DisasContext *ctx, uint8_t reg)
{
    if (reg <= 1) {
        return;
    }

    tcg_gen_ori_i64(cpu_fr_nat[reg / 64], cpu_fr_nat[reg / 64],
                    1ULL << (reg % 64));
    ia64_gen_fr_sig_clear(reg);
    ia64_gen_fr_ext_clear(reg);
    ia64_gen_fr_mark_written(ctx, reg);
}

TCGv_i64 ia64_gen_fr_nat_read(DisasContext *ctx, uint8_t reg)
{
    TCGv_i64 bit;

    if (reg <= 1) {
        return tcg_constant_i64(0);
    }
    if (ctx->fp.format[reg] != IA64_FR_FORMAT_UNKNOWN) {
        return tcg_constant_i64(ctx->fp.format[reg] == IA64_FR_FORMAT_NAT);
    }

    bit = tcg_temp_new_i64();
    tcg_gen_shri_i64(bit, cpu_fr_nat[reg / 64], reg % 64);
    tcg_gen_andi_i64(bit, bit, 1);
    return bit;
}

static void ia64_gen_fr_nat_assign(uint8_t reg, TCGv_i64 bit)
{
    TCGv_i64 shifted;

    if (reg <= 1) {
        return;
    }

    shifted = tcg_temp_new_i64();
    tcg_gen_andi_i64(shifted, bit, 1);
    tcg_gen_shli_i64(shifted, shifted, reg % 64);
    tcg_gen_andi_i64(cpu_fr_nat[reg / 64], cpu_fr_nat[reg / 64],
                     ~(1ULL << (reg % 64)));
    tcg_gen_or_i64(cpu_fr_nat[reg / 64], cpu_fr_nat[reg / 64], shifted);
    tcg_gen_not_i64(shifted, shifted);
    tcg_gen_and_i64(cpu_fr_sig[reg / 64], cpu_fr_sig[reg / 64], shifted);
    tcg_gen_and_i64(cpu_fr_int_origin[reg / 64],
                    cpu_fr_int_origin[reg / 64], shifted);
    tcg_gen_and_i64(cpu_fr_ext_valid[reg / 64],
                    cpu_fr_ext_valid[reg / 64], shifted);
}


void ia64_gen_fr_mov(DisasContext *ctx, uint8_t reg, TCGv_i64 value)
{
    if (ia64_fr_is_writable(reg)) {
        tcg_gen_mov_i64(cpu_fr[reg], value);
        ia64_gen_fr_nat_clear(reg);
        ia64_gen_fr_sig_clear(reg);
        ia64_gen_fr_ext_clear(reg);
        ia64_gen_fr_mark_written(ctx, reg);
    }
}

void ia64_gen_fr_mov_sig(DisasContext *ctx, uint8_t reg, TCGv_i64 value)
{
    if (ia64_fr_is_writable(reg)) {
        tcg_gen_mov_i64(cpu_fr[reg], value);
        ia64_gen_fr_nat_clear(reg);
        ia64_gen_fr_sig_set(reg);
        ia64_gen_fr_ext_clear(reg);
        ia64_gen_fr_mark_written(ctx, reg);
    }
}

void ia64_gen_fr_mov_s(DisasContext *ctx, uint8_t reg, TCGv_i64 value)
{
    TCGv_i64 magnitude = tcg_temp_new_i64();
    TCGv_i64 sign = tcg_temp_new_i64();
    TCGv_i64 exp = tcg_temp_new_i64();
    TCGLabel *zero = gen_new_label();
    TCGLabel *slow = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_andi_i64(magnitude, value, INT32_MAX);
    tcg_gen_extract_i64(sign, value, 31, 1);
    tcg_gen_shli_i64(sign, sign, 63);
    tcg_gen_brcondi_i64(TCG_COND_EQ, magnitude, 0, zero);
    tcg_gen_shri_i64(exp, magnitude, 23);
    tcg_gen_subi_i64(exp, exp, 1);
    tcg_gen_brcondi_i64(TCG_COND_GTU, exp, 253, slow);
    tcg_gen_shli_i64(magnitude, magnitude, 29);
    tcg_gen_addi_i64(magnitude, magnitude, UINT64_C(0x380) << 52);
    tcg_gen_or_i64(sign, sign, magnitude);
    gen_set_label(zero);
    ia64_gen_fr_mov(ctx, reg, sign);
    tcg_gen_br(done);
    gen_set_label(slow);
    gen_helper_setf_s(tcg_env, tcg_constant_i32(reg), value);
    gen_set_label(done);
}

static void ia64_gen_fr_ld(DisasContext *ctx, uint8_t reg, TCGv_i64 addr,
                           int mmu_idx, MemOp memop)
{
    TCGv_i64 dst = ia64_fr_is_writable(reg) ? cpu_fr[reg] : tcg_temp_new_i64();

    ia64_gen_qemu_ld_i64(ctx, dst, addr, mmu_idx, memop);
    if (ia64_fr_is_writable(reg)) {
        ia64_gen_fr_nat_clear(reg);
        ia64_gen_fr_sig_clear(reg);
        ia64_gen_fr_ext_clear(reg);
        ia64_gen_fr_mark_written(ctx, reg);
    }
}

static void ia64_gen_fr_ld_s(DisasContext *ctx, uint8_t reg, TCGv_i64 addr,
                             int mmu_idx, MemOp memop)
{
    TCGv_i64 value = tcg_temp_new_i64();

    ia64_gen_qemu_ld_i64(ctx, value, addr, mmu_idx, memop);
    ia64_gen_fr_mov_s(ctx, reg, value);
}

static void ia64_gen_fr_ld_sig(DisasContext *ctx, uint8_t reg, TCGv_i64 addr,
                               int mmu_idx, MemOp memop)
{
    TCGv_i64 dst = ia64_fr_is_writable(reg) ? cpu_fr[reg] : tcg_temp_new_i64();

    ia64_gen_qemu_ld_i64(ctx, dst, addr, mmu_idx, memop);
    if (ia64_fr_is_writable(reg)) {
        ia64_gen_fr_nat_clear(reg);
        ia64_gen_fr_sig_set(reg);
        ia64_gen_fr_ext_clear(reg);
        ia64_gen_fr_mark_written(ctx, reg);
    }
}

void ia64_gen_fr_load(DisasContext *ctx, uint8_t reg, TCGv_i64 addr,
                      int mmu_idx, MemOp memop,
                      IA64FPRegisterLoadFormat format)
{
    switch (format) {
    case IA64_FP_REGISTER_LOAD_DOUBLE:
        ia64_gen_fr_ld(ctx, reg, addr, mmu_idx, memop);
        break;
    case IA64_FP_REGISTER_LOAD_SINGLE:
        ia64_gen_fr_ld_s(ctx, reg, addr, mmu_idx, memop);
        break;
    case IA64_FP_REGISTER_LOAD_SIGNIFICAND:
        ia64_gen_fr_ld_sig(ctx, reg, addr, mmu_idx, memop);
        break;
    default:
        g_assert_not_reached();
    }
}

void ia64_gen_fr_set_nat(DisasContext *ctx, uint8_t reg)
{
    if (ia64_fr_is_writable(reg)) {
        tcg_gen_movi_i64(cpu_fr[reg], 0);
    }
    ia64_gen_fr_nat_set(ctx, reg);
}

void ia64_gen_check_data_debug(DisasContext *ctx,
                               const Ia64Instruction *insn, TCGv_i64 addr,
                               uint32_t size, uint64_t isr_access)
{
    TCGv_i64 enabled = tcg_temp_new_i64();
    TCGLabel *done;

    if (!ctx->restart.instruction_debug_psr_dynamic) {
        if (!(ctx->base.tb->flags & IA64_TB_FLAG_PSR_DB)) {
            return;
        }
        ia64_gen_sync_ip_for_helper(insn);
        gen_helper_data_debug(tcg_env, addr, tcg_constant_i32(size),
                              tcg_constant_i64(isr_access),
                              tcg_constant_i64(insn->address | insn->slot));
        return;
    }

    done = gen_new_label();

    tcg_gen_andi_i64(enabled, cpu_psr, IA64_PSR_DB);
    tcg_gen_brcondi_i64(TCG_COND_EQ, enabled, 0, done);
    ia64_gen_sync_ip_for_helper(insn);
    gen_helper_data_debug(tcg_env, addr, tcg_constant_i32(size),
                          tcg_constant_i64(isr_access),
                          tcg_constant_i64(insn->address | insn->slot));
    gen_set_label(done);
}

void ia64_gen_check_alignment_access(DisasContext *ctx,
                                     const Ia64Instruction *insn,
                                     TCGv_i64 addr, uint32_t size,
                                     bool always_fault,
                                     uint64_t isr_access)
{
    ia64_gen_check_alignment_model(
        ctx, insn, addr, size, size, size,
        always_fault ? IA64_ALIGNMENT_NATURAL_REQUIRED :
                       IA64_ALIGNMENT_GENERIC,
        isr_access);
}

void ia64_gen_check_alignment_model(DisasContext *ctx,
                                    const Ia64Instruction *insn,
                                    TCGv_i64 addr, uint32_t debug_size,
                                    uint32_t datum_size,
                                    uint32_t natural_alignment,
                                    IA64AlignmentClass alignment_class,
                                    uint64_t isr_access)
{
    if (debug_size > datum_size) {
        TCGv_i64 misalignment = tcg_temp_new_i64();
        TCGLabel *aligned = gen_new_label();
        TCGLabel *done = gen_new_label();

        /*
         * FP 10-byte operands are 16-byte DBR datums only when naturally
         * aligned.  For an implementation-supported unaligned reference,
         * breakpoint matching covers precisely the ten transferred bytes.
         */
        tcg_gen_andi_i64(misalignment, addr, natural_alignment - 1U);
        tcg_gen_brcondi_i64(TCG_COND_EQ, misalignment, 0, aligned);
        ia64_gen_check_data_debug(ctx, insn, addr, datum_size, isr_access);
        tcg_gen_br(done);
        gen_set_label(aligned);
        ia64_gen_check_data_debug(ctx, insn, addr, debug_size, isr_access);
        gen_set_label(done);
    } else {
        ia64_gen_check_data_debug(ctx, insn, addr, debug_size, isr_access);
    }
    ia64_gen_check_alignment_only(insn, addr, datum_size,
                                  natural_alignment, alignment_class,
                                  isr_access);
}

void ia64_gen_check_alignment_only(const Ia64Instruction *insn,
                                   TCGv_i64 addr, uint32_t datum_size,
                                   uint32_t natural_alignment,
                                   IA64AlignmentClass alignment_class,
                                   uint64_t isr_access)
{
    TCGv_i64 misalignment;
    TCGLabel *done;
    uint32_t alignment_info = IA64_ALIGNMENT_INFO(
        datum_size, natural_alignment, alignment_class);

    if (datum_size <= 1 || natural_alignment <= 1) {
        return;
    }

    misalignment = tcg_temp_new_i64();
    done = gen_new_label();
    tcg_gen_andi_i64(misalignment, addr, natural_alignment - 1U);
    tcg_gen_brcondi_i64(TCG_COND_EQ, misalignment, 0, done);
    ia64_gen_sync_ip_for_helper(insn);
    gen_helper_check_alignment(
        tcg_env, addr, tcg_constant_i32(alignment_info),
        tcg_constant_i64(isr_access),
        tcg_constant_i64(insn->address | insn->slot));
    gen_set_label(done);
}

void ia64_gen_check_alignment(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGv_i64 addr, uint32_t size,
                              bool always_fault, bool is_write)
{
    ia64_gen_check_alignment_model(
        ctx, insn, addr, size, size, size,
        always_fault ? IA64_ALIGNMENT_NATURAL_REQUIRED :
                       IA64_ALIGNMENT_INTEGER,
        is_write ? IA64_ISR_W : IA64_ISR_R);
}

static TCGLabel *ia64_gen_predicate_skip(DisasContext *ctx,
                                         const Ia64Instruction *insn,
                                         TCGv_i64 qp_value)
{
    TCGLabel *skip;

    /*
     * For while-loop branches, PR[qp] is the kernel loop condition rather
     * than a predicate that nullifies the instruction.  A false condition
     * must still drain the software pipeline while AR.EC is nonzero.
     */
    if (insn->qp == 0 ||
        insn->opcode == IA64_OP_BR_WEXIT ||
        insn->opcode == IA64_OP_BR_WTOP) {
        return NULL;
    }
    if (ctx->reg.current_qp_known && ctx->reg.current_qp_value) {
        return NULL;
    }

    skip = gen_new_label();
    if (ctx->reg.current_qp_known) {
        tcg_gen_br(skip);
    } else {
        tcg_gen_brcondi_i64(TCG_COND_EQ, qp_value, 0, skip);
    }
    return skip;
}

static void ia64_gen_predicate_end(TCGLabel *skip)
{
    if (skip != NULL) {
        gen_set_label(skip);
    }
}

typedef enum IA64KnownPredicate {
    IA64_PREDICATE_UNKNOWN = -1,
    IA64_PREDICATE_ZERO = 0,
    IA64_PREDICATE_ONE = 1,
} IA64KnownPredicate;

static IA64KnownPredicate ia64_predicate_known(const DisasContext *ctx,
                                               uint8_t reg)
{
    uint64_t bit = UINT64_C(1) << reg;

    if (ctx->reg.pr_known_zero & bit) {
        return IA64_PREDICATE_ZERO;
    }
    if (ctx->reg.pr_known_one & bit) {
        return IA64_PREDICATE_ONE;
    }
    return IA64_PREDICATE_UNKNOWN;
}

static void ia64_predicate_set_known(DisasContext *ctx, uint8_t reg,
                                     IA64KnownPredicate value)
{
    uint64_t bit;

    if (reg == 0) {
        return;
    }
    bit = UINT64_C(1) << reg;
    ctx->reg.pr_known_zero &= ~bit;
    ctx->reg.pr_known_one &= ~bit;
    if (value == IA64_PREDICATE_ZERO) {
        ctx->reg.pr_known_zero |= bit;
    } else if (value == IA64_PREDICATE_ONE) {
        ctx->reg.pr_known_one |= bit;
    }
}

static IA64KnownPredicate ia64_predicate_merge(IA64KnownPredicate a,
                                               IA64KnownPredicate b)
{
    return a == b ? a : IA64_PREDICATE_UNKNOWN;
}

static void ia64_gen_clear_unc_compare_targets(
    DisasContext *ctx, const Ia64Instruction *insn)
{
    if (insn->compare_unc) {
        if (insn->operands.common.auxiliary1 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary1], 0);
            ia64_predicate_set_known(
                ctx, insn->operands.common.auxiliary1,
                IA64_PREDICATE_ZERO);
        }
        if (insn->operands.common.auxiliary2 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary2], 0);
            ia64_predicate_set_known(
                ctx, insn->operands.common.auxiliary2,
                IA64_PREDICATE_ZERO);
        }
    }
}

static bool ia64_insn_writes_predicate_pair(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_CMP_EQ:
    case IA64_OP_CMP_LT:
    case IA64_OP_CMP_LE:
    case IA64_OP_CMP_GT:
    case IA64_OP_CMP_GE:
    case IA64_OP_CMP_LTU:
    case IA64_OP_CMP_LEU:
    case IA64_OP_CMP_GTU:
    case IA64_OP_CMP_GEU:
    case IA64_OP_CMP_NE:
    case IA64_OP_CMP_EQ_AND:
    case IA64_OP_CMP_NE_AND:
    case IA64_OP_CMP_GT_AND:
    case IA64_OP_CMP_LE_AND:
    case IA64_OP_CMP_GE_AND:
    case IA64_OP_CMP_LT_AND:
    case IA64_OP_CMP_EQ_OR:
    case IA64_OP_CMP_NE_OR:
    case IA64_OP_CMP_GT_OR:
    case IA64_OP_CMP_LE_OR:
    case IA64_OP_CMP_GE_OR:
    case IA64_OP_CMP_LT_OR:
    case IA64_OP_CMP_EQ_OR_ANDCM:
    case IA64_OP_CMP_NE_OR_ANDCM:
    case IA64_OP_CMP_GT_OR_ANDCM:
    case IA64_OP_CMP_LE_OR_ANDCM:
    case IA64_OP_CMP_GE_OR_ANDCM:
    case IA64_OP_CMP_LT_OR_ANDCM:
    case IA64_OP_CMP_EQ_IMM:
    case IA64_OP_CMP_LT_IMM:
    case IA64_OP_CMP_EQ_AND_IMM:
    case IA64_OP_CMP_NE_AND_IMM:
    case IA64_OP_CMP_EQ_OR_IMM:
    case IA64_OP_CMP_NE_OR_IMM:
    case IA64_OP_CMP_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP_LTU_IMM:
    case IA64_OP_CMP4_EQ:
    case IA64_OP_CMP4_LT:
    case IA64_OP_CMP4_LE:
    case IA64_OP_CMP4_GT:
    case IA64_OP_CMP4_GE:
    case IA64_OP_CMP4_LTU:
    case IA64_OP_CMP4_LEU:
    case IA64_OP_CMP4_GTU:
    case IA64_OP_CMP4_GEU:
    case IA64_OP_CMP4_EQ_AND:
    case IA64_OP_CMP4_NE_AND:
    case IA64_OP_CMP4_EQ_OR:
    case IA64_OP_CMP4_NE_OR:
    case IA64_OP_CMP4_EQ_OR_ANDCM:
    case IA64_OP_CMP4_NE_OR_ANDCM:
    case IA64_OP_CMP4_GT_AND:
    case IA64_OP_CMP4_LE_AND:
    case IA64_OP_CMP4_GE_AND:
    case IA64_OP_CMP4_LT_AND:
    case IA64_OP_CMP4_GT_OR:
    case IA64_OP_CMP4_LE_OR:
    case IA64_OP_CMP4_GE_OR:
    case IA64_OP_CMP4_LT_OR:
    case IA64_OP_CMP4_GT_OR_ANDCM:
    case IA64_OP_CMP4_LE_OR_ANDCM:
    case IA64_OP_CMP4_GE_OR_ANDCM:
    case IA64_OP_CMP4_LT_OR_ANDCM:
    case IA64_OP_CMP4_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP4_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP4_EQ_IMM:
    case IA64_OP_CMP4_LT_IMM:
    case IA64_OP_CMP4_LTU_IMM:
    case IA64_OP_CMP4_EQ_AND_IMM:
    case IA64_OP_CMP4_NE_AND_IMM:
    case IA64_OP_CMP4_EQ_OR_IMM:
    case IA64_OP_CMP4_NE_OR_IMM:
    case IA64_OP_TBIT_Z:
    case IA64_OP_TBIT_NZ:
    case IA64_OP_TBIT_Z_OR_ANDCM:
    case IA64_OP_TBIT_NZ_OR_ANDCM:
    case IA64_OP_TNAT_Z:
    case IA64_OP_TNAT_NZ:
    case IA64_OP_TNAT_NZ_AND:
    case IA64_OP_TF_Z:
    case IA64_OP_TF_NZ:
    case IA64_OP_FCMP:
    case IA64_OP_FCLASS:
        return true;
    default:
        return false;
    }
}

static bool ia64_insn_is_integer_compare(const Ia64Instruction *insn)
{
    return (insn->opcode >= IA64_OP_CMP_EQ &&
            insn->opcode <= IA64_OP_CMP_LTU_IMM) ||
           (insn->opcode >= IA64_OP_CMP4_EQ &&
            insn->opcode <= IA64_OP_CMP4_NE_OR_IMM);
}

static bool ia64_integer_compare_uses_immediate(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_CMP_EQ_IMM:
    case IA64_OP_CMP_LT_IMM:
    case IA64_OP_CMP_EQ_AND_IMM:
    case IA64_OP_CMP_NE_AND_IMM:
    case IA64_OP_CMP_EQ_OR_IMM:
    case IA64_OP_CMP_NE_OR_IMM:
    case IA64_OP_CMP_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP4_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP4_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP_LTU_IMM:
    case IA64_OP_CMP4_EQ_IMM:
    case IA64_OP_CMP4_LT_IMM:
    case IA64_OP_CMP4_LTU_IMM:
    case IA64_OP_CMP4_EQ_AND_IMM:
    case IA64_OP_CMP4_NE_AND_IMM:
    case IA64_OP_CMP4_EQ_OR_IMM:
    case IA64_OP_CMP4_NE_OR_IMM:
        return true;
    default:
        return false;
    }
}

static bool ia64_compare_has_equal_targets(const Ia64Instruction *insn)
{
    return insn->operands.common.auxiliary1 ==
               insn->operands.common.auxiliary2 &&
           ia64_insn_writes_predicate_pair(insn);
}

void ia64_gen_predicate_test_write(const Ia64Instruction *insn,
                                   TCGv_i64 cond, TCGv_i64 not_cond)
{
    switch (insn->pred_update) {
    case IA64_PRED_UPDATE_AND: {
        TCGLabel *done = gen_new_label();

        tcg_gen_brcondi_i64(TCG_COND_NE, cond, 0, done);
        if (insn->operands.common.auxiliary1 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary1], 0);
        }
        if (insn->operands.common.auxiliary2 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary2], 0);
        }
        gen_set_label(done);
        break;
    }
    case IA64_PRED_UPDATE_OR: {
        TCGLabel *done = gen_new_label();

        tcg_gen_brcondi_i64(TCG_COND_EQ, cond, 0, done);
        if (insn->operands.common.auxiliary1 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary1], 1);
        }
        if (insn->operands.common.auxiliary2 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary2], 1);
        }
        gen_set_label(done);
        break;
    }
    case IA64_PRED_UPDATE_OR_ANDCM: {
        TCGLabel *done = gen_new_label();

        tcg_gen_brcondi_i64(TCG_COND_EQ, cond, 0, done);
        if (insn->operands.common.auxiliary1 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary1], 1);
        }
        if (insn->operands.common.auxiliary2 != 0) {
            tcg_gen_movi_i64(cpu_pr[insn->operands.common.auxiliary2], 0);
        }
        gen_set_label(done);
        break;
    }
    case IA64_PRED_UPDATE_NORMAL:
        if (insn->operands.common.auxiliary1 != 0) {
            tcg_gen_mov_i64(cpu_pr[insn->operands.common.auxiliary1], cond);
        }
        if (insn->operands.common.auxiliary2 != 0) {
            tcg_gen_mov_i64(cpu_pr[insn->operands.common.auxiliary2], not_cond);
        }
        break;
    }
}

static Ia64PredicateUpdate ia64_effective_predicate_update(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_TNAT_NZ_AND:
        return IA64_PRED_UPDATE_AND;
    case IA64_OP_TBIT_Z_OR_ANDCM:
    case IA64_OP_TBIT_NZ_OR_ANDCM:
        return IA64_PRED_UPDATE_OR_ANDCM;
    default:
        return insn->pred_update;
    }
}

static IA64KnownPredicate ia64_predicate_dynamic_result(
    IA64KnownPredicate old, Ia64PredicateUpdate update, bool second)
{
    switch (update) {
    case IA64_PRED_UPDATE_AND:
        return old == IA64_PREDICATE_ZERO ?
               IA64_PREDICATE_ZERO : IA64_PREDICATE_UNKNOWN;
    case IA64_PRED_UPDATE_OR:
        return old == IA64_PREDICATE_ONE ?
               IA64_PREDICATE_ONE : IA64_PREDICATE_UNKNOWN;
    case IA64_PRED_UPDATE_OR_ANDCM:
        if ((!second && old == IA64_PREDICATE_ONE) ||
            (second && old == IA64_PREDICATE_ZERO)) {
            return old;
        }
        return IA64_PREDICATE_UNKNOWN;
    case IA64_PRED_UPDATE_NORMAL:
    default:
        return IA64_PREDICATE_UNKNOWN;
    }
}

static IA64KnownPredicate ia64_predicate_known_result(
    IA64KnownPredicate old, Ia64PredicateUpdate update, bool second,
    bool condition)
{
    switch (update) {
    case IA64_PRED_UPDATE_AND:
        return condition ? old : IA64_PREDICATE_ZERO;
    case IA64_PRED_UPDATE_OR:
        return condition ? IA64_PREDICATE_ONE : old;
    case IA64_PRED_UPDATE_OR_ANDCM:
        if (!condition) {
            return old;
        }
        return second ? IA64_PREDICATE_ZERO : IA64_PREDICATE_ONE;
    case IA64_PRED_UPDATE_NORMAL:
    default:
        return (condition ^ second) ?
               IA64_PREDICATE_ONE : IA64_PREDICATE_ZERO;
    }
}

static bool ia64_predicate_nullifies_insn(const Ia64Instruction *insn)
{
    return insn->opcode != IA64_OP_BR_WEXIT &&
           insn->opcode != IA64_OP_BR_WTOP;
}

static bool ia64_current_insn_cannot_execute(
    const DisasContext *ctx, const Ia64Instruction *insn)
{
    return ia64_predicate_nullifies_insn(insn) &&
           ctx->reg.current_qp_known &&
           !ctx->reg.current_qp_value;
}

static bool ia64_current_insn_definitely_executes(
    const DisasContext *ctx, const Ia64Instruction *insn)
{
    return !ia64_predicate_nullifies_insn(insn) ||
           (ctx->reg.current_qp_known && ctx->reg.current_qp_value);
}

static void ia64_predicate_commit_result(
    DisasContext *ctx, uint8_t reg, IA64KnownPredicate old,
    IA64KnownPredicate executed, bool definitely_executes)
{
    ia64_predicate_set_known(
        ctx, reg, definitely_executes ?
        executed : ia64_predicate_merge(old, executed));
}

static void ia64_update_predicate_pair_known(
    DisasContext *ctx, const Ia64Instruction *insn)
{
    Ia64PredicateUpdate update = ia64_effective_predicate_update(insn);
    uint8_t p1 = insn->operands.common.auxiliary1;
    uint8_t p2 = insn->operands.common.auxiliary2;
    IA64KnownPredicate old1 = ia64_predicate_known(ctx, p1);
    IA64KnownPredicate old2 = ia64_predicate_known(ctx, p2);
    IA64KnownPredicate result1;
    IA64KnownPredicate result2;
    bool definitely_executes =
        ia64_current_insn_definitely_executes(ctx, insn);

    if (insn->opcode == IA64_OP_TF_Z ||
        insn->opcode == IA64_OP_TF_NZ) {
        bool bit = extract64(
            ia64_env_cpu_class(ctx->env)->cpuid_features,
            insn->operands.common.immediate, 1);
        bool condition = insn->opcode == IA64_OP_TF_NZ ? bit : !bit;

        result1 = ia64_predicate_known_result(old1, update, false,
                                               condition);
        result2 = ia64_predicate_known_result(old2, update, true,
                                               condition);
    } else if (insn->opcode == IA64_OP_TNAT_Z ||
               insn->opcode == IA64_OP_TNAT_NZ ||
               insn->opcode == IA64_OP_TNAT_NZ_AND) {
        IA64KnownNat nat = ia64_nat_known(
            ctx, insn->operands.common.source2);

        if (nat == IA64_NAT_UNKNOWN) {
            result1 = ia64_predicate_dynamic_result(old1, update, false);
            result2 = ia64_predicate_dynamic_result(old2, update, true);
        } else {
            bool is_nz = insn->opcode != IA64_OP_TNAT_Z;
            bool condition = is_nz ?
                nat == IA64_NAT_SET : nat == IA64_NAT_CLEAR;

            result1 = ia64_predicate_known_result(
                old1, update, false, condition);
            result2 = ia64_predicate_known_result(
                old2, update, true, condition);
        }
    } else if ((insn->opcode == IA64_OP_TBIT_Z ||
                insn->opcode == IA64_OP_TBIT_NZ ||
                insn->opcode == IA64_OP_TBIT_Z_OR_ANDCM ||
                insn->opcode == IA64_OP_TBIT_NZ_OR_ANDCM) &&
               ia64_nat_known(ctx, insn->operands.common.source2) ==
               IA64_NAT_SET) {
        if (update == IA64_PRED_UPDATE_OR ||
            update == IA64_PRED_UPDATE_OR_ANDCM) {
            result1 = old1;
            result2 = old2;
        } else {
            result1 = IA64_PREDICATE_ZERO;
            result2 = IA64_PREDICATE_ZERO;
        }
    } else if (ia64_insn_is_integer_compare(insn)) {
        bool immediate = ia64_integer_compare_uses_immediate(insn);
        bool nat_set =
            ia64_nat_known(ctx, insn->operands.common.source2) ==
                IA64_NAT_SET ||
            (!immediate &&
             ia64_nat_known(ctx, insn->operands.common.source1) ==
                IA64_NAT_SET);

        if (nat_set &&
            (update == IA64_PRED_UPDATE_OR ||
             update == IA64_PRED_UPDATE_OR_ANDCM)) {
            result1 = old1;
            result2 = old2;
        } else if (nat_set) {
            result1 = IA64_PREDICATE_ZERO;
            result2 = IA64_PREDICATE_ZERO;
        } else {
            result1 = ia64_predicate_dynamic_result(old1, update, false);
            result2 = ia64_predicate_dynamic_result(old2, update, true);
        }
    } else {
        result1 = ia64_predicate_dynamic_result(old1, update, false);
        result2 = ia64_predicate_dynamic_result(old2, update, true);
    }

    ia64_predicate_commit_result(ctx, p1, old1, result1,
                                 definitely_executes);
    ia64_predicate_commit_result(ctx, p2, old2, result2,
                                 definitely_executes);
}

static void ia64_invalidate_predicate_mask(DisasContext *ctx, uint64_t mask)
{
    mask &= ~UINT64_C(1);
    ctx->reg.pr_known_zero &= ~mask;
    ctx->reg.pr_known_one &= ~mask;
}

static uint64_t ia64_insn_predicate_write_mask(const Ia64Instruction *insn)
{
    if (ia64_insn_writes_predicate_pair(insn)) {
        return (UINT64_C(1) << insn->operands.common.auxiliary1) |
               (UINT64_C(1) << insn->operands.common.auxiliary2);
    }
    switch (insn->opcode) {
    case IA64_OP_FRCPA:
        return UINT64_C(1) << insn->operands.common.auxiliary1;
    case IA64_OP_FPRCPA:
    case IA64_OP_FPRSQRTA:
    case IA64_OP_FRSQRTA:
        return UINT64_C(1) << insn->operands.common.auxiliary2;
    case IA64_OP_MOV_GRPR:
        return insn->operands.common.immediate;
    case IA64_OP_MOV_PR_ROT_IMM:
    case IA64_OP_CLRRRB:
    case IA64_OP_CLRRRB_PR:
    case IA64_OP_ALLOC:
    case IA64_OP_COVER:
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CTOP:
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
        return ~UINT64_C(0xffff);
    case IA64_OP_BREAK:
    case IA64_OP_RFI:
    case IA64_OP_BR_IA:
        return UINT64_MAX;
    default:
        return 0;
    }
}

static void ia64_update_predicate_known(DisasContext *ctx,
                                        const Ia64Instruction *insn)
{
    if (ia64_current_insn_cannot_execute(ctx, insn)) {
        return;
    }
    if (ia64_insn_writes_predicate_pair(insn)) {
        ia64_update_predicate_pair_known(ctx, insn);
    } else {
        ia64_invalidate_predicate_mask(
            ctx, ia64_insn_predicate_write_mask(insn));
    }
}

static void ia64_rse_dirty_set_known(DisasContext *ctx, uint8_t reg)
{
    uint32_t bit;

    if (reg < IA64_STACKED_GR_BASE) {
        return;
    }
    bit = reg - IA64_STACKED_GR_BASE;
    ctx->reg.rse_dirty_known[bit / 64] |= UINT64_C(1) << (bit % 64);
}

static bool ia64_insn_may_preserve_gr_destination(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
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

static bool ia64_insn_resets_rse_dirty(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_ALLOC:
    case IA64_OP_COVER:
    case IA64_OP_CLRRRB:
    case IA64_OP_CLRRRB_PR:
    case IA64_OP_FLUSHRS:
    case IA64_OP_LOADRS:
    case IA64_OP_BR_CALL:
    case IA64_OP_BRL_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_BR_IA:
    case IA64_OP_BR_CEXIT:
    case IA64_OP_BR_CTOP:
    case IA64_OP_BR_WEXIT:
    case IA64_OP_BR_WTOP:
    case IA64_OP_RFI:
    case IA64_OP_BREAK:
        return true;
    default:
        return false;
    }
}

static void ia64_update_rse_dirty_known(DisasContext *ctx,
                                        const Ia64Instruction *insn)
{
    if (ia64_current_insn_cannot_execute(ctx, insn)) {
        return;
    }
    if (ia64_insn_resets_rse_dirty(insn)) {
        ctx->reg.rse_dirty_known[0] = 0;
        ctx->reg.rse_dirty_known[1] = 0;
    }
    if (!ia64_current_insn_definitely_executes(ctx, insn)) {
        return;
    }
    if (ia64_insn_writes_gr_r1(insn) &&
        !ia64_insn_may_preserve_gr_destination(insn)) {
        ia64_rse_dirty_set_known(
            ctx, insn->operands.common.destination);
    }
    if (insn->reg_base_update || insn->imm_base_update ||
        (insn->opcode == IA64_OP_HINT_M &&
         insn->hint_m_reg_increment)) {
        ia64_rse_dirty_set_known(ctx, insn->operands.common.source2);
    }
}


void ia64_gen_clear_ri(void)
{
    tcg_gen_andi_i64(cpu_psr, cpu_psr, ~IA64_PSR_RI_MASK);
}

static void ia64_gen_set_ri(uint8_t slot)
{
    ia64_gen_clear_ri();
    if (slot != 0) {
        tcg_gen_ori_i64(cpu_psr, cpu_psr,
                        (uint64_t)slot << IA64_PSR_RI_SHIFT);
    }
}

void ia64_gen_set_ri_tracked(DisasContext *ctx, uint8_t slot)
{
    if (ctx->restart.current_ri_known && ctx->restart.current_ri == slot) {
        return;
    }

    if (ctx->restart.current_ri_known) {
        /* Only the two known RI bits differ; toggle them in one operation. */
        tcg_gen_xori_i64(
            cpu_psr, cpu_psr,
            (uint64_t)(ctx->restart.current_ri ^ slot) <<
                IA64_PSR_RI_SHIFT);
    } else {
        ia64_gen_clear_ri();
        if (slot != 0) {
            tcg_gen_ori_i64(cpu_psr, cpu_psr,
                            (uint64_t)slot << IA64_PSR_RI_SHIFT);
        }
    }
    ctx->restart.current_ri = slot;
    ctx->restart.current_ri_known = true;
}

static void ia64_gen_force_ri_tracked(DisasContext *ctx, uint8_t slot)
{
    ia64_gen_set_ri(slot);
    ctx->restart.current_ri = slot;
    ctx->restart.current_ri_known = true;
}

/*
 * Keep the architecturally visible restart point at the next instruction
 * boundary after an instruction completes.  In particular, a vCPU kick can
 * make TCG leave a translated block between the end of one instruction and
 * the generated code for the next one.  Leaving PSR.ri at the slot that just
 * completed makes an asynchronous interruption replay that instruction; at
 * the end of a bundle it can also pair the following bundle address with a
 * stale slot number.
 *
 * An MLX L+X instruction occupies slots 1 and 2, so its successor is slot 0
 * of the next bundle rather than slot 2 of the current bundle.  SDM Vol. 2,
 * 5.5.3 likewise defines the successor of an MLX slot-1 instruction as the
 * next bundle at RI 0.
 */
void ia64_gen_advance_restart_point(DisasContext *ctx, uint64_t bundle_ip,
                                    uint8_t slot, bool mlx_long)
{
    if (slot == 2 || (slot == 1 && mlx_long)) {
        ia64_gen_set_ri_tracked(ctx, 0);
        tcg_gen_movi_i64(cpu_ip, bundle_ip + 16);
    } else {
        ia64_gen_set_ri_tracked(ctx, slot + 1);
    }
}

static void ia64_gen_set_fault_slot(uint8_t slot)
{
    tcg_gen_st_i32(tcg_constant_i32(slot), tcg_env,
                   offsetof(CPUIA64State, exception_state.fault_slot));
}

static void ia64_gen_save_fault_slot_from_ri(void)
{
    TCGv_i64 slot64 = tcg_temp_new_i64();
    TCGv_i32 slot32 = tcg_temp_new_i32();

    tcg_gen_extract_i64(slot64, cpu_psr, IA64_PSR_RI_SHIFT, 2);
    tcg_gen_extrl_i64_i32(slot32, slot64);
    tcg_gen_st_i32(
        slot32, tcg_env,
        offsetof(CPUIA64State, exception_state.fault_slot));
}

void ia64_gen_save_fault_slot_for_exit(DisasContext *ctx)
{
    if (ctx->restart.current_ri_known) {
        ia64_gen_set_fault_slot(ctx->restart.current_ri);
    } else {
        ia64_gen_save_fault_slot_from_ri();
    }
}

static void ia64_gen_note_successful_bundle(DisasContext *ctx,
                                            uint64_t bundle_ip,
                                            bool record_iipa,
                                            bool track_psr_suppression)
{
    if (record_iipa &&
        (!ctx->restart.psr_ic_known || ctx->restart.psr_ic)) {
        TCGLabel *l_done = NULL;

        if (!ctx->restart.psr_ic_known) {
            TCGv_i64 collecting = tcg_temp_new_i64();

            l_done = gen_new_label();
            tcg_gen_andi_i64(collecting, cpu_psr, IA64_PSR_IC);
            tcg_gen_brcondi_i64(TCG_COND_EQ, collecting, 0, l_done);
        }
        tcg_gen_st_i64(tcg_constant_i64(ia64_ip_bundle_addr(bundle_ip)),
                       tcg_env,
                       offsetof(CPUIA64State, last_successful_bundle));
        if (l_done) {
            gen_set_label(l_done);
        }
    }

    if (track_psr_suppression) {
        TCGv_i64 suppression = tcg_temp_new_i64();
        TCGLabel *l_no_suppression = gen_new_label();

        tcg_gen_ld_i64(
            suppression, tcg_env,
            offsetof(CPUIA64State,
                     exception_state.psr_suppression_before_insn));
        tcg_gen_brcondi_i64(TCG_COND_EQ, suppression, 0, l_no_suppression);
        gen_helper_clear_psr_fault_suppression(tcg_env);
        gen_set_label(l_no_suppression);
    }
}

void ia64_gen_check_native_traps(TCGv_i64 target_ip, uint8_t target_slot,
                                 uint64_t source_ip, uint8_t source_slot,
                                 TCGv_i32 conditions, uint64_t base_isr,
                                 TCGv_i64 trap_psr)
{
    if (conditions == NULL) {
        conditions = tcg_constant_i32(0);
    }
    gen_helper_check_native_traps(
        tcg_env, target_ip, tcg_constant_i64(source_ip),
        tcg_constant_i32(IA64_NATIVE_TRAP_SLOTS(target_slot, source_slot)),
        conditions, tcg_constant_i64(base_isr), trap_psr);
}

static TCGv_i64 ia64_gen_native_trap_psr(DisasContext *ctx)
{
    TCGv_i64 trap_psr;

    if (!ctx->restart.native_trap_saved_psr) {
        return cpu_psr;
    }

    trap_psr = tcg_temp_new_i64();
    tcg_gen_ld_i64(trap_psr, tcg_env,
                   offsetof(CPUIA64State,
                            exception_state.psr_before_insn));
    return trap_psr;
}

static bool ia64_static_native_trap_needed(DisasContext *ctx,
                                           uint64_t target_ip,
                                           uint32_t conditions,
                                           uint64_t base_isr)
{
    uint32_t flags = ctx->base.tb->flags;

    if (base_isr != 0 || ctx->restart.native_trap_psr_dynamic ||
        (conditions & (IA64_NATIVE_TRAP_FP | IA64_NATIVE_TRAP_LOWER)) != 0 ||
        (flags & IA64_TB_FLAG_PSR_SS) != 0 ||
        ((conditions & IA64_NATIVE_TRAP_TAKEN) != 0 &&
         (flags & IA64_TB_FLAG_PSR_TB) != 0)) {
        return true;
    }

    if (flags & IA64_TB_FLAG_IT) {
        return !ia64_va_is_implemented(ctx->env, target_ip);
    }
    return !ia64_pa_is_implemented(ctx->env, target_ip);
}

static void ia64_gen_check_static_native_traps(
    DisasContext *ctx, uint64_t target_ip, uint8_t target_slot,
    uint64_t source_ip, uint8_t source_slot, uint32_t conditions,
    uint64_t base_isr)
{
    if (!ia64_static_native_trap_needed(ctx, target_ip, conditions,
                                        base_isr)) {
        return;
    }

    ia64_gen_check_native_traps(
        tcg_constant_i64(target_ip), target_slot, source_ip, source_slot,
        tcg_constant_i32(conditions), base_isr,
        ia64_gen_native_trap_psr(ctx));
}

static void ia64_gen_exit_to(DisasContext *ctx, uint64_t ip)
{
    ia64_gen_save_fault_slot_from_ri();
    ia64_gen_clear_ri();
    tcg_gen_movi_i64(cpu_ip, ip);
    tcg_gen_exit_tb(NULL, 0);
}

void ia64_gen_store_instruction_group_start(bool group_start)
{
    tcg_gen_st8_i32(tcg_constant_i32(group_start), tcg_env,
                    offsetof(CPUIA64State, instruction_group_start));
}

void ia64_gen_exit_to_completed(DisasContext *ctx, uint64_t ip,
                                uint64_t completed_ip,
                                bool record_iipa,
                                bool track_psr_suppression)
{
    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    ia64_gen_check_static_native_traps(
        ctx, ip, 0, completed_ip, ctx->restart.current_ri, 0, 0);
    ia64_gen_store_instruction_group_start(
        ctx->restart.next_instruction_group_start);
    ia64_gen_exit_to(ctx, ip);
}

static void ia64_gen_lookup_or_exit(DisasContext *ctx)
{
    if (ctx->base.tb->flags & IA64_TB_FLAG_IRQ_DEFER) {
        tcg_gen_exit_tb(NULL, 0);
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
}

void ia64_gen_lookup_tcg_completed(DisasContext *ctx, TCGv_i64 ip,
                                   uint64_t completed_ip,
                                   bool record_iipa,
                                   bool track_psr_suppression)
{
    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    ia64_gen_check_native_traps(
        ip, 0, completed_ip, ctx->restart.current_ri,
        tcg_constant_i32(IA64_NATIVE_TRAP_TAKEN), 0,
        ia64_gen_native_trap_psr(ctx));
    ia64_gen_store_instruction_group_start(true);
    ia64_gen_save_fault_slot_from_ri();
    ia64_gen_clear_ri();
    tcg_gen_mov_i64(cpu_ip, ip);
    ia64_gen_lookup_or_exit(ctx);
}

void ia64_gen_lookup_current_completed(DisasContext *ctx,
                                       uint64_t completed_ip,
                                       bool record_iipa,
                                       bool track_psr_suppression,
                                       TCGv_i32 trap_conditions,
                                       bool skip_native_traps)
{
    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    if (!skip_native_traps) {
        if (trap_conditions == NULL) {
            trap_conditions = tcg_constant_i32(IA64_NATIVE_TRAP_TAKEN);
        }
        ia64_gen_check_native_traps(cpu_ip, 0, completed_ip,
                                    ctx->restart.current_ri,
                                    trap_conditions, 0,
                                    ia64_gen_native_trap_psr(ctx));
    }
    ia64_gen_store_instruction_group_start(true);
    ia64_gen_save_fault_slot_from_ri();
    ia64_gen_clear_ri();
    ia64_gen_lookup_or_exit(ctx);
}

static void ia64_gen_exit_to_slot(DisasContext *ctx, uint64_t ip, uint8_t slot)
{
    if (slot >= 3) {
        ia64_gen_exit_to(ctx, ip + 16);
        return;
    }

    ia64_gen_set_fault_slot(slot);
    ia64_gen_set_ri(slot);
    tcg_gen_movi_i64(cpu_ip, ip);
    tcg_gen_exit_tb(NULL, 0);
}

void ia64_gen_exit_to_slot_completed(DisasContext *ctx, uint64_t ip,
                                     uint8_t slot,
                                     uint64_t completed_ip,
                                     bool record_iipa,
                                     bool track_psr_suppression)
{
    uint64_t target_ip = slot >= 3 ? ip + 16 : ip;
    uint8_t target_slot = slot >= 3 ? 0 : slot;

    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    ia64_gen_check_static_native_traps(
        ctx, target_ip, target_slot, completed_ip,
        ctx->restart.current_ri, 0, 0);
    ia64_gen_store_instruction_group_start(
        ctx->restart.next_instruction_group_start);
    ia64_gen_exit_to_slot(ctx, ip, slot);
}

void ia64_gen_goto_completed(DisasContext *ctx, uint64_t ip,
                             uint64_t completed_ip,
                             bool record_iipa,
                             bool track_psr_suppression)
{
    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    ia64_gen_check_static_native_traps(
        ctx, ip, 0, completed_ip, ctx->restart.current_ri,
        IA64_NATIVE_TRAP_TAKEN, 0);
    ia64_gen_goto_tb_group(ctx, ip, true);
}

bool ia64_gen_completed_direct_branch(DisasContext *ctx, TCGLabel *skip,
                                      uint64_t target,
                                      uint64_t completed_ip,
                                      bool record_iipa,
                                      bool track_psr_suppression)
{
    ia64_gen_goto_completed(ctx, target, completed_ip, record_iipa,
                            track_psr_suppression);
    return skip == NULL;
}

IA64GenResult ia64_gen_complete_branch(
    DisasContext *ctx, TCGLabel *skip,
    const IA64BranchCompletion *completion)
{
    switch (completion->target_kind) {
    case IA64_BRANCH_TARGET_DIRECT:
        if (ia64_gen_completed_direct_branch(
                ctx, skip, completion->direct_target,
                completion->completed_ip, completion->record_iipa,
                completion->track_psr_suppression)) {
            return IA64_GEN_NORETURN;
        }
        return IA64_GEN_CONTINUE;
    case IA64_BRANCH_TARGET_TCG:
        ia64_gen_lookup_tcg_completed(
            ctx, completion->tcg_target, completion->completed_ip,
            completion->record_iipa, completion->track_psr_suppression);
        break;
    case IA64_BRANCH_TARGET_CURRENT:
        ia64_gen_lookup_current_completed(
            ctx, completion->completed_ip, completion->record_iipa,
            completion->track_psr_suppression,
            completion->trap_conditions,
            completion->skip_native_traps);
        break;
    default:
        g_assert_not_reached();
    }
    return skip == NULL ? IA64_GEN_NORETURN : IA64_GEN_CONTINUE;
}

static Ia64Instruction ia64_decode_insn_for_model(
    CPUIA64State *env, IA64SlotUnit unit, uint64_t raw,
    uint64_t address, uint8_t slot)
{
    Ia64Instruction insn = ia64_decode_insn(unit, raw, address, slot);

    return insn;
}

static bool ia64_is_fill_st1_postinc(const Ia64Instruction *insn)
{
    return (insn->opcode == IA64_OP_ST1 ||
            insn->opcode == IA64_OP_ST1REL) &&
           insn->qp == 0 &&
           insn->operands.common.source1 != insn->operands.common.source2 &&
           insn->operands.common.source2 != 0 &&
           insn->operands.common.immediate == 1;
}

static bool ia64_analyze_self_counted_loop(
    const Ia64Instruction decoded[3], bool skip_x_slot,
    uint64_t bundle_ip, uint8_t start_slot, DisasContext *ctx)
{
    bool fill_st1_exact = true;
    bool fill_st1_seen = false;
    uint8_t fill_st1_base = 0;
    uint8_t fill_st1_source = 0;
    uint8_t fill_st1_slot = 0;
    bool fill_st1_release = false;
    bool nat_clear_preserving = true;
    int self_loop_slot = -1;
    int slot;

    for (slot = start_slot; slot < 3; ++slot) {
        Ia64Instruction insn;

        if (skip_x_slot && slot == 2) {
            continue;
        }

        insn = decoded[slot];
        nat_clear_preserving &=
            ia64_insn_preserves_all_nat_clear(ctx, &insn);
        if (insn.valid &&
            ((insn.opcode == IA64_OP_BR_CLOOP &&
              insn.address + insn.operands.common.immediate == bundle_ip) ||
             (insn.opcode == IA64_OP_BR_CTOP &&
              insn.operands.common.branch2 == 0 &&
              insn.address + insn.operands.common.immediate == bundle_ip))) {
            self_loop_slot = slot;
            if (insn.opcode != IA64_OP_BR_CLOOP || insn.qp != 0) {
                fill_st1_exact = false;
            }
            continue;
        }
        if (insn.valid && ia64_is_fill_st1_postinc(&insn)) {
            if (fill_st1_seen) {
                fill_st1_exact = false;
            } else {
                fill_st1_seen = true;
                fill_st1_base = insn.operands.common.source2;
                fill_st1_source = insn.operands.common.source1;
                fill_st1_slot = slot;
                fill_st1_release = insn.mem_release;
            }
            continue;
        }
        if (!insn.valid || !ia64_insn_is_empty_hint(&insn)) {
            fill_st1_exact = false;
        }
    }

    if (self_loop_slot < 0) {
        return false;
    }

    ctx->branch.counted_self_preserves_nat_clear = nat_clear_preserving;

    if (fill_st1_exact && fill_st1_seen && fill_st1_slot < self_loop_slot &&
        !(ctx->base.tb->flags & IA64_TB_FLAG_PSR_DB) &&
        !ctx->restart.instruction_debug_psr_dynamic &&
        !ctx->restart.native_trap_psr_dynamic &&
        !ctx->restart.exit_after_bundle) {
        ctx->branch.cloop_fill_st1_valid = true;
        ctx->branch.cloop_fill_st1_release = fill_st1_release;
        ctx->branch.cloop_fill_st1_base = fill_st1_base;
        ctx->branch.cloop_fill_st1_source = fill_st1_source;
        ctx->branch.cloop_fill_st1_slot = fill_st1_slot;
    }
    return true;
}

static bool ia64_all_gr_nats_known_clear(const DisasContext *ctx)
{
    return ctx->memory.nat_known_clear[0] == UINT64_MAX &&
           ctx->memory.nat_known_clear[1] == UINT64_MAX;
}

static void ia64_prepare_self_counted_loop(
    DisasContext *ctx, const Ia64Instruction decoded[3], bool skip_x_slot,
    uint64_t bundle_ip)
{
    ctx->branch.counted_self_label = NULL;
    ctx->branch.cloop_fill_st1_valid = false;
    ctx->branch.counted_self_preserves_nat_clear = false;

    if (ctx->restart.start_slot != 0 ||
        ctx->base.plugin_enabled ||
        (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) ||
        (ctx->base.tb->flags &
         (IA64_TB_FLAG_PSR_TB | IA64_TB_FLAG_PSR_SS)) ||
        !ia64_analyze_self_counted_loop(
            decoded, skip_x_slot, bundle_ip, ctx->restart.start_slot, ctx)) {
        return;
    }

    /*
     * The generated TCG back edge reuses this bundle after architectural
     * state has changed.  Values learned before the loop therefore do not
     * necessarily describe later iterations.
     */
    if (!(ctx->base.tb->flags & IA64_TB_FLAG_NAT_CLEAR) ||
        !ctx->branch.counted_self_preserves_nat_clear ||
        !ia64_all_gr_nats_known_clear(ctx)) {
        ctx->memory.nat_known_clear[0] = 1;
        ctx->memory.nat_known_clear[1] = 0;
        ctx->memory.nat_known_set[0] = 0;
        ctx->memory.nat_known_set[1] = 0;
    }
    for (int slot = 0; slot < (skip_x_slot ? 2 : 3); slot++) {
        const Ia64Instruction *insn = &decoded[slot];

        if (ia64_insn_may_modify_cfm_sof(insn)) {
            ctx->reg.cfm_sof_valid = false;
            ctx->reg.cfm_sof_checked = 0;
        }
        if (ia64_insn_may_modify_cpl(insn)) {
            ctx->reg.cpl_known = false;
        }
        ia64_invalidate_predicate_mask(
            ctx, ia64_insn_predicate_write_mask(insn));
        if (ia64_insn_resets_rse_dirty(insn)) {
            ctx->reg.rse_dirty_known[0] = 0;
            ctx->reg.rse_dirty_known[1] = 0;
        }
        ia64_invalidate_fp_state(ctx, insn);
        ia64_forget_fp_written_formats(ctx, insn);
        if (ia64_insn_may_modify_psr_ic(insn)) {
            ctx->restart.psr_ic_known = false;
        }
    }
    ctx->branch.counted_self_label = gen_new_label();
    ctx->branch.counted_self_budget = tcg_temp_new_i64();
    ctx->branch.counted_self_ip = bundle_ip;
    tcg_gen_movi_i64(ctx->branch.counted_self_budget, IA64_COUNTED_SELF_BUDGET);
    gen_set_label(ctx->branch.counted_self_label);
}

bool ia64_gen_self_counted_loop(DisasContext *ctx, uint64_t target,
                                uint64_t completed_ip,
                                bool record_iipa,
                                bool track_psr_suppression)
{
    TCGLabel *exit_to_tb;
    uint8_t fallthrough_ri;
    bool fallthrough_ri_known;

    if (ctx->branch.counted_self_label == NULL ||
        ctx->restart.exit_after_bundle ||
        target != ctx->branch.counted_self_ip ||
        completed_ip != ctx->branch.counted_self_ip) {
        return false;
    }

    exit_to_tb = gen_new_label();
    tcg_gen_brcondi_i64(TCG_COND_EQ, ctx->branch.counted_self_budget,
                        0, exit_to_tb);
    tcg_gen_subi_i64(ctx->branch.counted_self_budget,
                     ctx->branch.counted_self_budget, 1);
    ia64_gen_note_successful_bundle(ctx, completed_ip, record_iipa,
                                    track_psr_suppression);
    ia64_gen_check_static_native_traps(
        ctx, target, 0, completed_ip, ctx->restart.current_ri,
        IA64_NATIVE_TRAP_TAKEN, 0);
    /*
     * The taken branch restarts the target bundle at slot 0.  The internal
     * TCG back-edge bypasses the normal TB-entry RI initialization, so clear
     * the branch slot's RI before jumping back.  Otherwise a following
     * iteration can OR slot 1 onto stale slot 2 and expose reserved RI=3 to
     * a fault or interruption.
     */
    fallthrough_ri = ctx->restart.current_ri;
    fallthrough_ri_known = ctx->restart.current_ri_known;
    ia64_gen_force_ri_tracked(ctx, 0);
    tcg_gen_br(ctx->branch.counted_self_label);

    gen_set_label(exit_to_tb);
    ia64_gen_goto_completed(ctx, target, completed_ip, record_iipa,
                            track_psr_suppression);
    /* The not-taken branch bypasses both generated exit paths above. */
    ctx->restart.current_ri = fallthrough_ri;
    ctx->restart.current_ri_known = fallthrough_ri_known;
    return true;
}

void ia64_gen_raise_exception(uint32_t exception, uint64_t fault_ip,
                              uint64_t fault_imm, uint32_t fault_slot)
{
    tcg_gen_movi_i64(cpu_ip, fault_ip);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(exception),
                                tcg_constant_i64(fault_ip),
                                tcg_constant_i64(fault_imm),
                                tcg_constant_i32(fault_slot));
}

bool ia64_cr_is_read_only(uint32_t cr_num)
{
    switch (cr_num) {
    case IA64_CR_SAPIC_IVR:
    case IA64_CR_SAPIC_IRR0:
    case IA64_CR_SAPIC_IRR1:
    case IA64_CR_SAPIC_IRR2:
    case IA64_CR_SAPIC_IRR3:
        return true;
    default:
        return false;
    }
}

bool ia64_ar_access_reads_clock(uint32_t ar_num)
{
    return ar_num == 44;
}

bool ia64_cr_write_reads_clock(uint32_t cr_num)
{
    return cr_num == IA64_CR_ITM || cr_num == IA64_CR_ITV;
}

bool ia64_clock_access_needs_io(const DisasContext *ctx)
{
    return tb_cflags(ctx->base.tb) & CF_USE_ICOUNT;
}

void ia64_gen_note_stacked_gr_write(const Ia64Instruction *insn, uint8_t reg)
{
    DisasContext *ctx = insn ? insn->ctx : NULL;
    uint32_t bit;

    if (reg < IA64_STACKED_GR_BASE) {
        return;
    }
    bit = reg - IA64_STACKED_GR_BASE;
    if (ctx && (ctx->reg.rse_dirty_known[bit / 64] &
                (UINT64_C(1) << (bit % 64)))) {
        return;
    }
    tcg_gen_ori_i64(cpu_rse_gr_dirty[bit / 64],
                    cpu_rse_gr_dirty[bit / 64], 1ULL << (bit % 64));
}

static IA64KnownNat ia64_insn_nat_known(const Ia64Instruction *insn,
                                        uint8_t reg)
{
    const DisasContext *ctx = insn ? insn->ctx : NULL;

    return ctx ? ia64_nat_known(ctx, reg) : IA64_NAT_UNKNOWN;
}

bool ia64_gr_nat_is_known_clear(const Ia64Instruction *insn, uint8_t reg)
{
    return ia64_insn_nat_known(insn, reg) == IA64_NAT_CLEAR;
}

bool ia64_gr_nat_is_known_set(const Ia64Instruction *insn, uint8_t reg)
{
    return ia64_insn_nat_known(insn, reg) == IA64_NAT_SET;
}

void ia64_gen_gr_nat_clear(const Ia64Instruction *insn, uint8_t reg)
{
    if (reg == 0) {
        return;
    }

    ia64_gen_note_stacked_gr_write(insn, reg);
    if (ia64_insn_nat_known(insn, reg) == IA64_NAT_CLEAR) {
        return;
    }
    tcg_gen_andi_i64(cpu_nat[reg / 64], cpu_nat[reg / 64],
                     ~(1ULL << (reg % 64)));
}

void ia64_gen_gr_nat_set(const Ia64Instruction *insn, uint8_t reg)
{
    if (reg == 0) {
        return;
    }

    ia64_gen_note_stacked_gr_write(insn, reg);
    if (ia64_insn_nat_known(insn, reg) == IA64_NAT_SET) {
        return;
    }
    tcg_gen_ori_i64(cpu_nat[reg / 64], cpu_nat[reg / 64],
                    1ULL << (reg % 64));
}

void ia64_gen_gr_nat_or_from_1(const Ia64Instruction *insn, uint8_t dst,
                               uint8_t src)
{
    IA64KnownNat src_known;
    TCGv_i64 set;
    uint64_t dst_mask;
    uint64_t src_mask;

    if (dst == 0) {
        return;
    }
    if (dst == src || ia64_insn_nat_known(insn, dst) == IA64_NAT_SET) {
        ia64_gen_note_stacked_gr_write(insn, dst);
        return;
    }

    src_known = ia64_insn_nat_known(insn, src);
    if (src_known == IA64_NAT_CLEAR) {
        ia64_gen_note_stacked_gr_write(insn, dst);
        return;
    }
    if (src_known == IA64_NAT_SET) {
        ia64_gen_gr_nat_set(insn, dst);
        return;
    }

    ia64_gen_note_stacked_gr_write(insn, dst);
    dst_mask = 1ULL << (dst % 64);
    src_mask = 1ULL << (src % 64);
    set = tcg_temp_new_i64();
    tcg_gen_ori_i64(set, cpu_nat[dst / 64], dst_mask);
    tcg_gen_movcond_i64(TCG_COND_TSTNE, cpu_nat[dst / 64],
                        cpu_nat[src / 64], tcg_constant_i64(src_mask),
                        set, cpu_nat[dst / 64]);
}

void ia64_gen_gr_write_nat_clear(const Ia64Instruction *insn, uint8_t reg,
                                 TCGv_i64 value)
{
    if (reg == 0) {
        return;
    }
    tcg_gen_mov_i64(cpu_gr[reg], value);
    ia64_gen_gr_nat_clear(insn, reg);
}

static void ia64_gen_gr_nat_assign_masked(const Ia64Instruction *insn,
                                          uint8_t reg, TCGv_i64 source,
                                          uint64_t source_mask)
{
    IA64KnownNat old_known;
    TCGv_i64 cleared;
    TCGv_i64 set;
    uint64_t dst_mask;

    if (reg == 0) {
        return;
    }

    ia64_gen_note_stacked_gr_write(insn, reg);
    old_known = ia64_insn_nat_known(insn, reg);
    dst_mask = 1ULL << (reg % 64);
    if (old_known == IA64_NAT_CLEAR) {
        cleared = cpu_nat[reg / 64];
    } else {
        cleared = tcg_temp_new_i64();
        tcg_gen_andi_i64(cleared, cpu_nat[reg / 64], ~dst_mask);
    }
    if (old_known == IA64_NAT_SET) {
        set = cpu_nat[reg / 64];
    } else {
        set = tcg_temp_new_i64();
        tcg_gen_ori_i64(set, cpu_nat[reg / 64], dst_mask);
    }
    tcg_gen_movcond_i64(TCG_COND_TSTNE, cpu_nat[reg / 64],
                        source, tcg_constant_i64(source_mask), set, cleared);
}

void ia64_gen_gr_nat_assign(const Ia64Instruction *insn, uint8_t reg,
                            TCGv_i64 bit)
{
    ia64_gen_gr_nat_assign_masked(insn, reg, bit, 1);
}

TCGv_i64 ia64_gen_gr_nat_read(uint8_t reg)
{
    TCGv_i64 bit;

    if (reg == 0) {
        return tcg_constant_i64(0);
    }

    bit = tcg_temp_new_i64();
    tcg_gen_shri_i64(bit, cpu_nat[reg / 64], reg % 64);
    tcg_gen_andi_i64(bit, bit, 1);
    return bit;
}

void ia64_gen_gr_nat_from_1(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src)
{
    IA64KnownNat known;

    if (dst == 0) {
        return;
    }
    if (dst == src) {
        ia64_gen_note_stacked_gr_write(insn, dst);
        return;
    }

    known = ia64_insn_nat_known(insn, src);
    if (known == IA64_NAT_CLEAR) {
        ia64_gen_gr_nat_clear(insn, dst);
    } else if (known == IA64_NAT_SET) {
        ia64_gen_gr_nat_set(insn, dst);
    } else {
        ia64_gen_gr_nat_assign_masked(
            insn, dst, cpu_nat[src / 64], 1ULL << (src % 64));
    }
}

static TCGv_i64 ia64_gen_va_unimplemented(TCGv_i64 va, uint8_t impl_va_msb)
{
    TCGv_i64 result = tcg_temp_new_i64();

    if (impl_va_msb >= 60) {
        tcg_gen_movi_i64(result, 0);
    } else {
        TCGv_i64 sign = tcg_temp_new_i64();
        TCGv_i64 expected = tcg_temp_new_i64();
        uint64_t count = 60 - impl_va_msb;
        uint64_t mask = (1ULL << count) - 1;

        tcg_gen_shri_i64(sign, va, impl_va_msb);
        tcg_gen_andi_i64(sign, sign, 1);
        tcg_gen_neg_i64(expected, sign);
        tcg_gen_andi_i64(expected, expected, mask);
        tcg_gen_shri_i64(result, va, impl_va_msb + 1);
        tcg_gen_andi_i64(result, result, mask);
        tcg_gen_xor_i64(result, result, expected);
        tcg_gen_setcondi_i64(TCG_COND_NE, result, result, 0);
    }

    return result;
}

void ia64_gen_gr_nat_from_1_or_unimplemented_va(
    const Ia64Instruction *insn, uint8_t dst, uint8_t src,
    uint8_t impl_va_msb)
{
    IA64KnownNat known;
    TCGv_i64 nat;
    TCGv_i64 unimplemented;

    if (dst == 0) {
        return;
    }

    known = ia64_insn_nat_known(insn, src);
    if (known == IA64_NAT_SET) {
        ia64_gen_gr_nat_set(insn, dst);
        return;
    }
    unimplemented = ia64_gen_va_unimplemented(ia64_gr_src(src),
                                              impl_va_msb);
    if (known == IA64_NAT_CLEAR) {
        ia64_gen_gr_nat_assign(insn, dst, unimplemented);
        return;
    }
    nat = ia64_gen_gr_nat_read(src);
    tcg_gen_or_i64(nat, nat, unimplemented);
    ia64_gen_gr_nat_assign(insn, dst, nat);
}

void ia64_gen_fr_nat_from_gr(uint8_t dst, uint8_t src)
{
    if (dst <= 1) {
        return;
    }

    ia64_gen_fr_nat_assign(dst, ia64_gen_gr_nat_read(src));
}

void ia64_gen_gr_nat_from_2(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src1, uint8_t src2)
{
    IA64KnownNat known1;
    IA64KnownNat known2;
    TCGv_i64 bit;
    TCGv_i64 src_bit;

    if (dst == 0) {
        return;
    }

    known1 = ia64_insn_nat_known(insn, src1);
    known2 = ia64_insn_nat_known(insn, src2);
    if (known1 == IA64_NAT_SET || known2 == IA64_NAT_SET) {
        ia64_gen_gr_nat_set(insn, dst);
        return;
    }
    if (known1 == IA64_NAT_CLEAR) {
        ia64_gen_gr_nat_from_1(insn, dst, src2);
        return;
    }
    if (known2 == IA64_NAT_CLEAR) {
        ia64_gen_gr_nat_from_1(insn, dst, src1);
        return;
    }
    if (src1 / 64 == src2 / 64) {
        ia64_gen_gr_nat_assign_masked(
            insn, dst, cpu_nat[src1 / 64],
            (1ULL << (src1 % 64)) | (1ULL << (src2 % 64)));
        return;
    }
    bit = ia64_gen_gr_nat_read(src1);
    src_bit = ia64_gen_gr_nat_read(src2);
    tcg_gen_or_i64(bit, bit, src_bit);
    ia64_gen_gr_nat_assign(insn, dst, bit);
}

void ia64_gen_gr_nat_from_3(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src1, uint8_t src2, uint8_t src3)
{
    IA64KnownNat known1;
    IA64KnownNat known2;
    IA64KnownNat known3;
    TCGv_i64 bit = NULL;
    uint64_t source_mask = 0;
    int source_word = -1;
    bool one_source_word = true;

    if (dst == 0) {
        return;
    }

    known1 = ia64_insn_nat_known(insn, src1);
    known2 = ia64_insn_nat_known(insn, src2);
    known3 = ia64_insn_nat_known(insn, src3);
    if (known1 == IA64_NAT_SET ||
        known2 == IA64_NAT_SET ||
        known3 == IA64_NAT_SET) {
        ia64_gen_gr_nat_set(insn, dst);
        return;
    }
    if (known1 == IA64_NAT_UNKNOWN) {
        source_word = src1 / 64;
        source_mask = 1ULL << (src1 % 64);
    }
    if (known2 == IA64_NAT_UNKNOWN) {
        if (source_word < 0) {
            source_word = src2 / 64;
            source_mask = 1ULL << (src2 % 64);
        } else if (source_word == src2 / 64) {
            source_mask |= 1ULL << (src2 % 64);
        } else {
            one_source_word = false;
        }
    }
    if (known3 == IA64_NAT_UNKNOWN) {
        if (source_word < 0) {
            source_word = src3 / 64;
            source_mask = 1ULL << (src3 % 64);
        } else if (source_word == src3 / 64) {
            source_mask |= 1ULL << (src3 % 64);
        } else {
            one_source_word = false;
        }
    }
    if (source_word < 0) {
        ia64_gen_gr_nat_clear(insn, dst);
    } else if (one_source_word) {
        ia64_gen_gr_nat_assign_masked(
            insn, dst, cpu_nat[source_word], source_mask);
    } else {
        if (known1 == IA64_NAT_UNKNOWN) {
            bit = ia64_gen_gr_nat_read(src1);
        }
        if (known2 == IA64_NAT_UNKNOWN) {
            TCGv_i64 src_bit = ia64_gen_gr_nat_read(src2);

            if (bit == NULL) {
                bit = src_bit;
            } else {
                tcg_gen_or_i64(bit, bit, src_bit);
            }
        }
        if (known3 == IA64_NAT_UNKNOWN) {
            TCGv_i64 src_bit = ia64_gen_gr_nat_read(src3);

            if (bit == NULL) {
                bit = src_bit;
            } else {
                tcg_gen_or_i64(bit, bit, src_bit);
            }
        }
        ia64_gen_gr_nat_assign(insn, dst, bit);
    }
}


void ia64_gen_check_nat_consumption(const Ia64Instruction *insn,
                                    uint8_t reg, uint64_t isr_access,
                                    Ia64NatConsumptionKind kind)
{
    const DisasContext *ctx = insn->ctx;
    IA64KnownNat known = ctx ?
        ia64_nat_known(ctx, reg) : IA64_NAT_UNKNOWN;
    TCGLabel *ok;

    if (known == IA64_NAT_CLEAR) {
        return;
    }

    if (kind == IA64_NAT_NON_ACCESS) {
        isr_access |= IA64_ISR_NA;
    }
    if (known == IA64_NAT_SET) {
        tcg_gen_movi_i64(cpu_ip, insn->address);
        gen_helper_raise_nat_consumption(
            tcg_env, tcg_constant_i64(isr_access),
            tcg_constant_i64(insn->address | insn->slot));
        return;
    }

    ok = gen_new_label();
    tcg_gen_brcondi_i64(TCG_COND_TSTEQ, cpu_nat[reg / 64],
                        1ULL << (reg % 64), ok);
    tcg_gen_movi_i64(cpu_ip, insn->address);
    gen_helper_raise_nat_consumption(
        tcg_env, tcg_constant_i64(isr_access),
        tcg_constant_i64(insn->address | insn->slot));
    gen_set_label(ok);
}

void ia64_gen_check_fr_nat_consumption(const Ia64Instruction *insn,
                                       uint8_t reg, uint64_t isr_access)
{
    TCGLabel *ok;

    if (reg <= 1) {
        return;
    }

    ok = gen_new_label();
    tcg_gen_brcondi_i64(TCG_COND_EQ,
                        ia64_gen_fr_nat_read(insn->ctx, reg), 0, ok);
    tcg_gen_movi_i64(cpu_ip, insn->address);
    gen_helper_raise_nat_consumption(
        tcg_env, tcg_constant_i64(isr_access),
        tcg_constant_i64(insn->address | insn->slot));
    gen_set_label(ok);
}

static void ia64_gen_check_gr_in_frame(const Ia64Instruction *insn,
                                       uint8_t reg)
{
    DisasContext *ctx = insn->ctx;

    if (reg == 0) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        return;
    }

    if (reg >= IA64_STACKED_GR_BASE) {
        uint8_t required_sof = reg - IA64_STACKED_GR_BASE + 1;
        TCGLabel *valid;
        TCGv_i32 cfm_sof;

        /*
         * A successful dominating check proves every lower stacked register
         * is also in frame until an instruction changes CFM.SOF.  Avoid
         * regenerating an exception path and branch for that already-proven
         * range.
         */
        if (required_sof <= ctx->reg.cfm_sof_checked) {
            return;
        }
        valid = gen_new_label();
        if (ctx->reg.cfm_sof_valid) {
            cfm_sof = ctx->reg.cfm_sof;
        } else {
            cfm_sof = tcg_temp_new_i32();
            tcg_gen_ld8u_i32(cfm_sof, tcg_env,
                             offsetof(CPUIA64State, cfm_sof));
            /*
             * A load emitted below an unknown predicate does not dominate
             * later instructions: the predicated instruction may skip it.
             * Cache only values whose defining load certainly executes.
             */
            if (ia64_current_insn_definitely_executes(ctx, insn)) {
                ctx->reg.cfm_sof = cfm_sof;
                ctx->reg.cfm_sof_valid = true;
            }
        }
        tcg_gen_brcondi_i32(TCG_COND_GTU, cfm_sof,
                            reg - IA64_STACKED_GR_BASE, valid);
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        gen_set_label(valid);
        if (ia64_current_insn_definitely_executes(ctx, insn)) {
            ctx->reg.cfm_sof_checked = required_sof;
        }
    }
}

void ia64_gen_check_privileged(const Ia64Instruction *insn)
{
    const DisasContext *ctx = insn->ctx;
    TCGLabel *allowed = NULL;
    uint64_t isr = 0x10;

    if (insn->opcode == IA64_OP_TAK) {
        isr |= IA64_ISR_NA | 3;
    } else if (insn->opcode == IA64_OP_TPA) {
        isr |= IA64_ISR_NA;
    }

    if (ctx && ctx->reg.cpl_known && ctx->reg.cpl == 0) {
        return;
    }
    if (!ctx || !ctx->reg.cpl_known) {
        TCGv_i64 cpl = tcg_temp_new_i64();

        allowed = gen_new_label();
        tcg_gen_andi_i64(cpl, cpu_psr, IA64_PSR_CPL_MASK);
        tcg_gen_brcondi_i64(TCG_COND_EQ, cpl, 0, allowed);
    }
    tcg_gen_st_i64(tcg_constant_i64(isr), tcg_env,
                   offsetof(CPUIA64State, cr_isr));
    ia64_gen_raise_exception(IA64_EXCP_PRIVILEGED_OP, insn->address,
                              insn->raw, insn->slot);
    if (allowed != NULL) {
        gen_set_label(allowed);
    }
}

void ia64_gen_check_virtualization(const Ia64Instruction *insn)
{
    DisasContext *ctx = insn->ctx;
    TCGLabel *allowed;
    TCGv_i64 vm;

    if (!ia64_env_cpu_class(ctx->env)->has_virtualization) {
        return;
    }

    allowed = gen_new_label();
    vm = tcg_temp_new_i64();
    tcg_gen_andi_i64(vm, cpu_psr, IA64_PSR_VM);
    tcg_gen_brcondi_i64(TCG_COND_EQ, vm, 0, allowed);
    ia64_gen_raise_exception(IA64_EXCP_VIRTUALIZATION, insn->address,
                             insn->raw, insn->slot);
    gen_set_label(allowed);
}

void ia64_gen_check_cover_virtualization(const Ia64Instruction *insn)
{
    DisasContext *ctx = insn->ctx;
    TCGLabel *allowed;
    TCGv_i64 cpl_vm;

    if (!ia64_env_cpu_class(ctx->env)->has_virtualization) {
        return;
    }

    /* cover virtualizes only for the cpl == 0, PSR.vm == 1 combination. */
    allowed = gen_new_label();
    cpl_vm = tcg_temp_new_i64();
    tcg_gen_andi_i64(cpl_vm, cpu_psr,
                     IA64_PSR_CPL_MASK | IA64_PSR_VM);
    tcg_gen_brcondi_i64(TCG_COND_NE, cpl_vm, IA64_PSR_VM, allowed);
    ia64_gen_raise_exception(IA64_EXCP_VIRTUALIZATION, insn->address,
                             insn->raw, insn->slot);
    gen_set_label(allowed);
}

static void ia64_gen_check_psr_ic_clear(const Ia64Instruction *insn)
{
    TCGLabel *allowed = gen_new_label();
    TCGv_i64 ic = tcg_temp_new_i64();

    tcg_gen_andi_i64(ic, cpu_psr, IA64_PSR_IC);
    tcg_gen_brcondi_i64(TCG_COND_EQ, ic, 0, allowed);
    tcg_gen_st_i64(tcg_constant_i64(0), tcg_env,
                   offsetof(CPUIA64State, cr_isr));
    ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                             insn->raw, insn->slot);
    gen_set_label(allowed);
}

void ia64_gen_check_register_index(const Ia64Instruction *insn,
                                   TCGv_i64 index, uint32_t count)
{
    TCGv_i64 low8 = tcg_temp_new_i64();
    TCGLabel *valid = gen_new_label();

    tcg_gen_andi_i64(low8, index, 0xff);
    tcg_gen_brcondi_i64(TCG_COND_LTU, low8, count, valid);
    tcg_gen_st_i64(tcg_constant_i64(0x30), tcg_env,
                   offsetof(CPUIA64State, cr_isr));
    ia64_gen_raise_exception(IA64_EXCP_RESERVED_REG_FIELD,
                              insn->address, insn->raw, insn->slot);
    gen_set_label(valid);
}

void ia64_gen_check_reserved_bits(const Ia64Instruction *insn,
                                  TCGv_i64 value, uint64_t allowed)
{
    TCGv_i64 reserved = tcg_temp_new_i64();
    TCGLabel *valid = gen_new_label();

    tcg_gen_andi_i64(reserved, value, ~allowed);
    tcg_gen_brcondi_i64(TCG_COND_EQ, reserved, 0, valid);
    tcg_gen_st_i64(tcg_constant_i64(0x30), tcg_env,
                   offsetof(CPUIA64State, cr_isr));
    ia64_gen_raise_exception(IA64_EXCP_RESERVED_REG_FIELD,
                              insn->address, insn->raw, insn->slot);
    gen_set_label(valid);
}

void ia64_gen_write_user_mask(TCGv_i64 value)
{
    TCGv_i64 new_psr = tcg_temp_new_i64();
    TCGv_i64 new_um = tcg_temp_new_i64();
    TCGv_i64 secure = tcg_temp_new_i64();
    TCGv_i64 old_up = tcg_temp_new_i64();
    TCGLabel *done = gen_new_label();

    tcg_gen_andi_i64(new_psr, cpu_psr, ~IA64_PSR_UM_WRITABLE_MASK);
    tcg_gen_andi_i64(new_um, value, IA64_PSR_UM_WRITABLE_MASK);
    tcg_gen_or_i64(new_psr, new_psr, new_um);

    /* PSR.sp makes the user performance-monitor bit read-only. */
    tcg_gen_andi_i64(secure, cpu_psr, IA64_PSR_SP);
    tcg_gen_brcondi_i64(TCG_COND_EQ, secure, 0, done);
    tcg_gen_andi_i64(new_psr, new_psr, ~IA64_PSR_UP);
    tcg_gen_andi_i64(old_up, cpu_psr, IA64_PSR_UP);
    tcg_gen_or_i64(new_psr, new_psr, old_up);
    gen_set_label(done);

    tcg_gen_mov_i64(cpu_psr, new_psr);
}

void ia64_gen_validate_ar_access(const Ia64Instruction *insn,
                                 TCGv_i64 value, bool write)
{
    uint32_t ar_num = insn->operands.common.source1;

    /*
     * Only AR.BSPSTORE/AR.RNAT depend on RSC.mode on reads; AR.ITC/RUC can
     * also fault under PSR.si or PSR.vm.  Writes additionally validate the
     * reserved fields of RSC/FPSR/PFS and the privilege of KR0-KR7/ITC/RUC.
     * Every other application-register access is unconditional, so avoid a
     * helper call for it.
     */
    if (!write) {
        if (ar_num != IA64_AR_BSPSTORE && ar_num != IA64_AR_RNAT &&
            ar_num != IA64_AR_ITC && ar_num != IA64_AR_RUC) {
            return;
        }
    } else {
        switch (ar_num) {
        case IA64_AR_RSC: /* reserved fields */
        case IA64_AR_BSPSTORE: /* RSC.mode */
        case IA64_AR_RNAT: /* RSC.mode */
        case IA64_AR_FPSR: /* reserved fields */
        case IA64_AR_PFS: /* reserved fields */
            break;
        case IA64_AR_KR0 ... IA64_AR_KR7: /* privileged */
        case IA64_AR_ITC: /* privileged and virtualizable */
        case IA64_AR_RUC:
            break;
        default:
            return;
        }
    }

    gen_helper_validate_ar_access(tcg_env, value,
                                  tcg_constant_i32(ar_num),
                                  tcg_constant_i32(write),
                                  tcg_constant_i64(insn->address),
                                  tcg_constant_i64(insn->raw),
                                  tcg_constant_i32(insn->slot));
}

bool ia64_ar_is_simple(uint32_t ar)
{
    switch (ar) {
    case 32: /* CCV */
    case 36: /* UNAT */
    case 40: /* FPSR */
    case 64: /* PFS */
    case 65: /* LC */
    case 66: /* EC */
        return true;
    default:
        return false;
    }
}

void ia64_gen_read_simple_ar(TCGv_i64 value, uint32_t ar)
{
    tcg_gen_ld_i64(value, tcg_env,
                   offsetof(CPUIA64State, ar) + ar * sizeof(uint64_t));
}

void ia64_gen_write_simple_ar(uint32_t ar, TCGv_i64 value)
{
    if (ar == 66) {
        TCGv_i64 masked = tcg_temp_new_i64();

        tcg_gen_andi_i64(masked, value, 0x3f);
        value = masked;
    }
    tcg_gen_st_i64(value, tcg_env,
                   offsetof(CPUIA64State, ar) + ar * sizeof(uint64_t));
}

void ia64_gen_validate_cr_access(TCGv_i64 result,
                                 const Ia64Instruction *insn,
                                 TCGv_i64 value, bool write)
{
    gen_helper_validate_cr_access(result, tcg_env, value,
                                  tcg_constant_i32(
                                      insn->operands.common.source1),
                                  tcg_constant_i32(write),
                                  tcg_constant_i64(insn->address),
                                  tcg_constant_i64(insn->raw),
                                  tcg_constant_i32(insn->slot));
}

void ia64_gen_check_nat_register(const Ia64Instruction *insn, uint8_t reg)
{
    ia64_gen_check_nat_consumption(insn, reg, 0, IA64_NAT_ACCESS);
}


/*
 * IA-64 acquire/release completers are one-way ordering constraints.
 * Full serialization is reserved for explicit fence instructions such as mf.
 */
#define IA64_TCG_MO_ACQUIRE \
    (TCG_BAR_LDAQ | TCG_MO_LD_LD | TCG_MO_LD_ST)
#define IA64_TCG_MO_RELEASE \
    (TCG_BAR_STRL | TCG_MO_LD_ST | TCG_MO_ST_ST)

void ia64_gen_memory_acquire(const Ia64Instruction *insn)
{
    if (insn->mem_acquire) {
        tcg_gen_mb(IA64_TCG_MO_ACQUIRE);
    }
}

void ia64_gen_memory_release(const Ia64Instruction *insn)
{
    if (insn->mem_release) {
        tcg_gen_mb(IA64_TCG_MO_RELEASE);
    }
}

bool ia64_gen_fill_st1_cloop(DisasContext *ctx,
                             const Ia64Instruction *insn,
                             uint64_t target,
                             TCGLabel *l_nobr,
                             bool record_iipa,
                             bool track_psr_suppression)
{
    TCGv_i64 taken;

    if (!ctx->branch.cloop_fill_st1_valid ||
        target != ctx->branch.counted_self_ip ||
        insn->address != ctx->branch.counted_self_ip) {
        return false;
    }

    if (ctx->branch.cloop_fill_st1_release) {
        tcg_gen_mb(IA64_TCG_MO_RELEASE);
    }

    /*
     * Faults from the helper's future loop-body stores must be reported as
     * the original store slot, not the branch slot that calls the helper.
     */
    ia64_gen_force_ri_tracked(ctx, ctx->branch.cloop_fill_st1_slot);
    taken = tcg_temp_new_i64();
    gen_helper_cloop_fill_st1(taken, tcg_env,
                              tcg_constant_i32(ctx->branch.cloop_fill_st1_base),
                              ia64_gr_src(ctx->branch.cloop_fill_st1_source),
                              tcg_constant_i32(ctx->memory.mmu_idx),
                              tcg_constant_i32(IA64_CLOOP_FILL_ST1_MAX));
    ia64_gen_note_stacked_gr_write(insn,
                                   ctx->branch.cloop_fill_st1_base);
    ia64_gen_force_ri_tracked(ctx, insn->slot);

    tcg_gen_brcondi_i64(TCG_COND_EQ, taken, 0, l_nobr);
    ia64_gen_goto_completed(ctx, target, insn->address, record_iipa,
                            track_psr_suppression);
    return true;
}


void ia64_gen_check_branch(DisasContext *ctx, TCGv_i64 failed,
                           uint64_t target,
                           uint64_t completed_ip,
                           bool record_iipa,
                           bool track_psr_suppression)
{
    TCGLabel *l_done = gen_new_label();

    tcg_gen_brcondi_i64(TCG_COND_EQ, failed, 0, l_done);
    ia64_gen_goto_completed(ctx, target, completed_ip, record_iipa,
                            track_psr_suppression);
    gen_set_label(l_done);
}

void ia64_gen_sync_ip_for_helper(const Ia64Instruction *insn)
{
    tcg_gen_movi_i64(cpu_ip, ia64_ip_bundle_addr(insn->address));
    ia64_gen_set_fault_slot(insn->slot);
}




static uint32_t ia64_fp_reg_set(uint8_t reg)
{
    if (reg >= 32) {
        return 2;
    }
    if (reg >= 2) {
        return 1;
    }
    return 0;
}

static uint32_t ia64_insn_fp_read_sets(const Ia64Instruction *insn)
{
    uint32_t sets;

    switch (insn->opcode) {
    case IA64_OP_LDFD:
    case IA64_OP_LDFS:
    case IA64_OP_LDF_FILL:
    case IA64_OP_LDF8:
    case IA64_OP_LDFE:
    case IA64_OP_SETF_D:
    case IA64_OP_SETF_S:
    case IA64_OP_SETF_EXP:
    case IA64_OP_SETF_SIG:
        return 0;

    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
        return 0;

    case IA64_OP_STFD:
    case IA64_OP_STFS:
    case IA64_OP_STF_SPILL:
    case IA64_OP_STF8:
    case IA64_OP_STFE:
    case IA64_OP_GETF_D:
    case IA64_OP_GETF_S:
    case IA64_OP_GETF_EXP:
    case IA64_OP_GETF_SIG:
    case IA64_OP_FCLASS:
        return ia64_fp_reg_set(insn->operands.common.source1);

    case IA64_OP_CHK_S:
        return insn->check_fp ?
            ia64_fp_reg_set(insn->operands.common.source1) : 0;

    case IA64_OP_CHK_A:
    case IA64_OP_CHK_A_CLR:
        /* A floating chk.a uses the FR number only as an ALAT tag. */
        return 0;

    case IA64_OP_FCMP:
        return ia64_fp_reg_set(insn->operands.common.source1) |
               ia64_fp_reg_set(insn->operands.common.source2);

    case IA64_OP_FPABS:
    case IA64_OP_FPNEG:
    case IA64_OP_FPNEGABS:
    case IA64_OP_FMOV:
    case IA64_OP_FCVT_XF:
    case IA64_OP_FCVT_FX:
    case IA64_OP_FCVT_FXU:
    case IA64_OP_FPCVT:
        return ia64_fp_reg_set(insn->operands.common.source1);

    case IA64_OP_FPRSQRTA:
    case IA64_OP_FRSQRTA:
        return ia64_fp_reg_set(insn->operands.common.source2);

    case IA64_OP_FMA:
    case IA64_OP_FMS:
    case IA64_OP_FNMA:
    case IA64_OP_FPMA:
    case IA64_OP_FPMS:
    case IA64_OP_FPNMA:
    case IA64_OP_FSELECT:
    case IA64_OP_XMA_L:
    case IA64_OP_XMA_H:
    case IA64_OP_XMA_HU:
    case IA64_OP_XMPY_HU:
        return ia64_fp_reg_set(insn->operands.common.source1) |
               ia64_fp_reg_set(insn->operands.common.source2) |
               ia64_fp_reg_set(insn->operands.common.auxiliary1);

    case IA64_OP_FADD:
    case IA64_OP_FSUB:
    case IA64_OP_FMPY:
    case IA64_OP_FMIN:
    case IA64_OP_FMAX:
    case IA64_OP_FAMIN:
    case IA64_OP_FAMAX:
    case IA64_OP_FRCPA:
    case IA64_OP_FPRCPA:
    case IA64_OP_FNORM:
    case IA64_OP_FPACK:
    case IA64_OP_FAND:
    case IA64_OP_FANDCM:
    case IA64_OP_FOR:
    case IA64_OP_FXOR:
    case IA64_OP_FSWAP:
    case IA64_OP_FSWAP_NL:
    case IA64_OP_FSWAP_NR:
    case IA64_OP_FMIX_LR:
    case IA64_OP_FMIX_R:
    case IA64_OP_FMIX_L:
    case IA64_OP_FSXT_R:
    case IA64_OP_FSXT_L:
    case IA64_OP_FPMERGE:
    case IA64_OP_FPMERGE_S:
    case IA64_OP_FPMERGE_SE:
    case IA64_OP_FPMIN:
    case IA64_OP_FPMAX:
    case IA64_OP_FPAMIN:
    case IA64_OP_FPAMAX:
    case IA64_OP_FPCMP:
    case IA64_OP_FMERGE:
    case IA64_OP_FMERGE_S:
    case IA64_OP_FMERGE_SE:
        sets = ia64_fp_reg_set(insn->operands.common.source1);
        sets |= ia64_fp_reg_set(insn->operands.common.source2);
        return sets;

    default:
        return 0;
    }
}

static uint32_t ia64_insn_fp_write_sets(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_LDFD:
    case IA64_OP_LDFS:
    case IA64_OP_LDF_FILL:
    case IA64_OP_LDF8:
    case IA64_OP_LDFE:
    case IA64_OP_SETF_D:
    case IA64_OP_SETF_S:
    case IA64_OP_SETF_EXP:
    case IA64_OP_SETF_SIG:
    case IA64_OP_FADD:
    case IA64_OP_FSUB:
    case IA64_OP_FMPY:
    case IA64_OP_FMA:
    case IA64_OP_FMS:
    case IA64_OP_FNMA:
    case IA64_OP_XMA_L:
    case IA64_OP_XMA_H:
    case IA64_OP_XMA_HU:
    case IA64_OP_XMPY_HU:
    case IA64_OP_FMIN:
    case IA64_OP_FMAX:
    case IA64_OP_FAMIN:
    case IA64_OP_FAMAX:
    case IA64_OP_FRCPA:
    case IA64_OP_FPRCPA:
    case IA64_OP_FSELECT:
    case IA64_OP_FNORM:
    case IA64_OP_FPABS:
    case IA64_OP_FPNEG:
    case IA64_OP_FPNEGABS:
    case IA64_OP_FPRSQRTA:
    case IA64_OP_FRSQRTA:
    case IA64_OP_FPACK:
    case IA64_OP_FAND:
    case IA64_OP_FANDCM:
    case IA64_OP_FOR:
    case IA64_OP_FXOR:
    case IA64_OP_FSWAP:
    case IA64_OP_FSWAP_NL:
    case IA64_OP_FSWAP_NR:
    case IA64_OP_FMIX_LR:
    case IA64_OP_FMIX_R:
    case IA64_OP_FMIX_L:
    case IA64_OP_FSXT_R:
    case IA64_OP_FSXT_L:
    case IA64_OP_FPMERGE:
    case IA64_OP_FPMERGE_S:
    case IA64_OP_FPMERGE_SE:
    case IA64_OP_FPMIN:
    case IA64_OP_FPMAX:
    case IA64_OP_FPAMIN:
    case IA64_OP_FPAMAX:
    case IA64_OP_FPCMP:
    case IA64_OP_FPCVT:
    case IA64_OP_FPMA:
    case IA64_OP_FPMS:
    case IA64_OP_FPNMA:
    case IA64_OP_FMOV:
    case IA64_OP_FCVT_XF:
    case IA64_OP_FCVT_FX:
    case IA64_OP_FCVT_FXU:
    case IA64_OP_FMERGE:
    case IA64_OP_FMERGE_S:
    case IA64_OP_FMERGE_SE:
        return ia64_fp_reg_set(insn->operands.common.destination);

    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
        return ia64_fp_reg_set(insn->operands.common.destination) |
               ia64_fp_reg_set(insn->operands.common.source1);

    default:
        return 0;
    }
}

static bool ia64_insn_writes_fp_pair(const Ia64Instruction *insn)
{
    return insn->opcode == IA64_OP_LDFP8 ||
           insn->opcode == IA64_OP_LDFPD ||
           insn->opcode == IA64_OP_LDFPS;
}

static void ia64_forget_fp_written_formats(DisasContext *ctx,
                                           const Ia64Instruction *insn)
{
    if (ia64_insn_fp_write_sets(insn)) {
        ctx->fp.format[insn->operands.common.destination] =
            IA64_FR_FORMAT_UNKNOWN;
        if (ia64_insn_writes_fp_pair(insn)) {
            ctx->fp.format[insn->operands.common.source1] =
                IA64_FR_FORMAT_UNKNOWN;
        }
    }
}

static IA64TranslationFRFormat ia64_known_fr_format(const DisasContext *ctx,
                                                    unsigned reg)
{
    return reg <= 1 ? IA64_FR_FORMAT_BINARY : ctx->fp.format[reg];
}

static IA64TranslationFRFormat ia64_fp_result_format(
    const DisasContext *ctx, const Ia64Instruction *insn)
{
    const IA64CommonOperands *op = &insn->operands.common;

    switch (insn->opcode) {
    case IA64_OP_SETF_D:
    case IA64_OP_SETF_SIG:
    case IA64_OP_SETF_S:
    case IA64_OP_SETF_EXP:
        switch (ia64_insn_nat_known(insn, op->source1)) {
        case IA64_NAT_SET:
            return IA64_FR_FORMAT_NAT;
        case IA64_NAT_CLEAR:
            if (insn->opcode == IA64_OP_SETF_D) {
                return IA64_FR_FORMAT_BINARY;
            }
            if (insn->opcode == IA64_OP_SETF_SIG) {
                return IA64_FR_FORMAT_SIG;
            }
            break;
        default:
            break;
        }
        break;
    case IA64_OP_LDFD:
    case IA64_OP_LDFPD:
        if (!insn->fp_load_speculative) {
            return IA64_FR_FORMAT_BINARY;
        }
        break;
    case IA64_OP_LDF8:
    case IA64_OP_LDFP8:
        if (!insn->fp_load_speculative) {
            return IA64_FR_FORMAT_SIG;
        }
        break;
    case IA64_OP_FMOV:
        return ia64_known_fr_format(ctx, op->source1);
    case IA64_OP_XMA_L:
    case IA64_OP_XMA_H:
    case IA64_OP_XMA_HU:
    case IA64_OP_XMPY_HU: {
        IA64TranslationFRFormat a = ia64_known_fr_format(
            ctx, insn->opcode == IA64_OP_XMPY_HU ? 0 : op->source1);
        IA64TranslationFRFormat b = ia64_known_fr_format(ctx, op->source2);
        IA64TranslationFRFormat c = ia64_known_fr_format(ctx, op->auxiliary1);

        if (a == IA64_FR_FORMAT_NAT || b == IA64_FR_FORMAT_NAT ||
            c == IA64_FR_FORMAT_NAT) {
            return IA64_FR_FORMAT_NAT;
        }
        if (a != IA64_FR_FORMAT_UNKNOWN && b != IA64_FR_FORMAT_UNKNOWN &&
            c != IA64_FR_FORMAT_UNKNOWN) {
            return IA64_FR_FORMAT_SIG;
        }
        break;
    }
    default:
        break;
    }
    return IA64_FR_FORMAT_UNKNOWN;
}

static void ia64_update_fp_written_state(DisasContext *ctx,
                                          const Ia64Instruction *insn,
                                          bool unconditional)
{
    uint32_t written = ia64_insn_fp_write_sets(insn);
    IA64TranslationFRFormat format;
    unsigned regs[2] = { insn->operands.common.destination,
                         insn->operands.common.source1 };

    if (!written) {
        return;
    }
    format = ia64_fp_result_format(ctx, insn);
    for (unsigned i = 0; i < (ia64_insn_writes_fp_pair(insn) ? 2 : 1); i++) {
        unsigned reg = regs[i];

        if (unconditional &&
            !(insn->fp_load_check && ctx->memory.full_alat)) {
            ctx->fp.format[reg] = format;
        } else if (ctx->fp.format[reg] != format) {
            ctx->fp.format[reg] = IA64_FR_FORMAT_UNKNOWN;
        }
    }
    if (unconditional && !insn->fp_load_check) {
        ctx->fp.written_sets |= written;
        ctx->fp.rotating_live |= (written & 2) != 0;
    }
}

static uint64_t ia64_insn_disabled_fp_isr_flags(
    const Ia64Instruction *insn)
{
    /* ISR.R/W describe the instruction's memory access, not its FP operands. */
    switch (insn->opcode) {
    case IA64_OP_LDFD:
    case IA64_OP_LDFS:
    case IA64_OP_LDF_FILL:
    case IA64_OP_LDF8:
    case IA64_OP_LDFE:
    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
        return IA64_ISR_R |
               (insn->fp_load_speculative ? IA64_ISR_SP : 0);

    case IA64_OP_STFD:
    case IA64_OP_STFS:
    case IA64_OP_STF_SPILL:
    case IA64_OP_STF8:
    case IA64_OP_STFE:
        return IA64_ISR_W;

    default:
        return 0;
    }
}

static void ia64_gen_check_disabled_fp(DisasContext *ctx,
                                        const Ia64Instruction *insn,
                                        uint32_t sets, bool unconditional)
{
    uint64_t disabled_mask;
    uint64_t isr_flags;
    TCGv_i64 disabled;
    TCGv_i64 isr;
    TCGLabel *done;

    sets &= ~ctx->fp.enabled_sets;
    if (sets == 0) {
        return;
    }

    /*
     * Check the live PSR: a mask instruction earlier in the same bundle can
     * change dfl/dfh after the TB was translated.
     */
    done = gen_new_label();
    disabled = tcg_temp_new_i64();
    isr = tcg_temp_new_i64();
    disabled_mask = ((sets & 1) ? IA64_PSR_DFL : 0) |
                    ((sets & 2) ? IA64_PSR_DFH : 0);
    isr_flags = ia64_insn_disabled_fp_isr_flags(insn);

    tcg_gen_andi_i64(disabled, cpu_psr, disabled_mask);
    tcg_gen_brcondi_i64(TCG_COND_EQ, disabled, 0, done);
    tcg_gen_shri_i64(isr, disabled, 18);
    if (isr_flags != 0) {
        tcg_gen_ori_i64(isr, isr, isr_flags);
    }
    tcg_gen_st_i64(isr, tcg_env, offsetof(CPUIA64State, cr_isr));
    ia64_gen_raise_exception(IA64_EXCP_DISABLED_FP, insn->address,
                             0, insn->slot);
    gen_set_label(done);
    if (unconditional) {
        ctx->fp.enabled_sets |= sets;
    }
}

static void ia64_gen_sync_rotating_fr(DisasContext *ctx, uint32_t sets,
                                       bool unconditional)
{
    TCGv_i32 current;
    TCGv_i32 materialized;
    TCGLabel *done;

    if (!(sets & 2) || ctx->fp.rotating_synced) {
        return;
    }

    current = tcg_temp_new_i32();
    materialized = tcg_temp_new_i32();
    done = gen_new_label();
    tcg_gen_ld8u_i32(current, tcg_env,
                     offsetof(CPUIA64State, cfm_rrb_fr));
    tcg_gen_ld8u_i32(materialized, tcg_env,
                     offsetof(CPUIA64State,
                              fp.rotating_fr_materialized_rrb));
    tcg_gen_brcond_i32(TCG_COND_EQ, current, materialized, done);
    gen_helper_sync_rotating_fr(tcg_env);
    gen_set_label(done);
    if (unconditional) {
        ctx->fp.rotating_synced = true;
    }
}

/* Instructions gated by the CPUID[4].ao 16-byte atomic capability bit. */
static bool ia64_insn_needs_16byte_atomics(const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_LD16:
    case IA64_OP_ST16:
    case IA64_OP_CMP8XCHG16:
        return true;
    default:
        return false;
    }
}

/* Optional integer instructions advertised by CPUID[4].cz and .x2. */
static uint64_t ia64_insn_required_integer_feature(
    const Ia64Instruction *insn)
{
    switch (insn->opcode) {
    case IA64_OP_CLZ:
        return IA64_CPUID4_CZ;
    case IA64_OP_MPY4:
    case IA64_OP_MPYSHL4:
        return IA64_CPUID4_X2;
    default:
        return 0;
    }
}

static IA64GeneratorKind ia64_insn_generator(const Ia64Instruction *insn)
{
    return (unsigned)insn->opcode < IA64_OP_COUNT ?
           ia64_opcode_properties[insn->opcode].generator : IA64_GENERATOR_NONE;
}

static IA64GenResult ia64_gen_dispatch(DisasContext *ctx,
                                       const Ia64Instruction *insn,
                                       IA64GeneratorKind generator,
                                       TCGLabel *skip, bool record_iipa,
                                       bool track_psr_suppression)
{
    switch (generator) {
    case IA64_GENERATOR_INTEGER:
        return ia64_gen_integer(ctx, insn);
    case IA64_GENERATOR_MEMORY:
        return ia64_gen_memory(ctx, insn, skip, record_iipa,
                               track_psr_suppression);
    case IA64_GENERATOR_FP:
        return ia64_gen_fp(ctx, insn);
    case IA64_GENERATOR_SIMD:
        return ia64_gen_simd(ctx, insn);
    case IA64_GENERATOR_SYSTEM:
        return ia64_gen_system(ctx, insn, skip, record_iipa,
                               track_psr_suppression);
    case IA64_GENERATOR_BRANCH:
        return ia64_gen_branch(ctx, insn, skip, record_iipa,
                               track_psr_suppression);
    default:
        return IA64_GEN_UNHANDLED;
    }
}

typedef enum IA64PrepareResult {
    IA64_PREPARE_DISPATCH,
    IA64_PREPARE_NULLIFIED,
    IA64_PREPARE_COMPLETE,
    IA64_PREPARE_NORETURN,
} IA64PrepareResult;

static IA64PrepareResult ia64_gen_prepare_insn(
    DisasContext *ctx, const Ia64Instruction *insn,
    TCGLabel **predicate_skip)
{
    IA64KnownPredicate qp_known;
    TCGLabel *skip;
    TCGv_i64 qp_value;
    uint64_t required_feature;
    uint32_t fp_sets;

    if (!insn->valid) {
        static unsigned invalid_logs;

        if (qatomic_fetch_inc(&invalid_logs) < 128) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ia64 invalid insn ip=0x%016" PRIx64
                          " slot=%u unit=%u class=%u raw=0x%010" PRIx64
                          "\n",
                          insn->address, insn->slot, insn->unit,
                          insn->encoding_class, insn->raw);
        }
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        return IA64_PREPARE_NORETURN;
    }

    qp_known = ia64_predicate_known(ctx, insn->qp);
    ctx->reg.current_qp_known = qp_known != IA64_PREDICATE_UNKNOWN;
    ctx->reg.current_qp_value = qp_known == IA64_PREDICATE_ONE;
    if (qp_known == IA64_PREDICATE_ONE) {
        qp_value = tcg_constant_i64(1);
    } else if (qp_known == IA64_PREDICATE_ZERO) {
        qp_value = tcg_constant_i64(0);
    } else {
        qp_value = cpu_pr[insn->qp];
    }
    if ((insn->compare_unc ||
         (insn->clear_p2_before_predicate &&
          insn->qp == insn->operands.common.auxiliary2)) &&
        insn->qp != 0) {
        TCGv_i64 saved_qp = tcg_temp_new_i64();

        tcg_gen_mov_i64(saved_qp, qp_value);
        qp_value = saved_qp;
    }
    if (insn->compare_unc && ia64_compare_has_equal_targets(insn)) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        return IA64_PREPARE_NORETURN;
    }
    if (insn->opcode == IA64_OP_ALLOC && insn->qp != 0) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        return IA64_PREPARE_NORETURN;
    }
    ia64_gen_clear_unc_compare_targets(ctx, insn);
    if (insn->clear_p2_before_predicate &&
        insn->operands.common.auxiliary2 != 0) {
        uint32_t p2 = insn->operands.common.auxiliary2;

        /*
         * Approximation instructions clear p2 only when qp is false.  When
         * qp is true, the instruction operation writes p2 after its FP fault
         * checks, so an FP fault must leave the old predicate intact.
         */
        if (qp_known == IA64_PREDICATE_ZERO) {
            tcg_gen_movi_i64(cpu_pr[p2], 0);
            ia64_predicate_set_known(ctx, p2, IA64_PREDICATE_ZERO);
        } else if (qp_known == IA64_PREDICATE_UNKNOWN) {
            tcg_gen_movcond_i64(TCG_COND_EQ, cpu_pr[p2], qp_value,
                                tcg_constant_i64(0), tcg_constant_i64(0),
                                cpu_pr[p2]);
            ia64_predicate_set_known(ctx, p2, IA64_PREDICATE_UNKNOWN);
        }
    }
    /* Itanium reports an unsupported long branch even when PR[qp] is 0. */
    if ((insn->opcode == IA64_OP_BRL_COND ||
         insn->opcode == IA64_OP_BRL_CALL) &&
        !(ia64_env_cpu_class(ctx->env)->cpuid_features & IA64_CPUID4_LB)) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                 insn->raw, insn->slot);
        return IA64_PREPARE_NORETURN;
    }
    if (ia64_current_insn_cannot_execute(ctx, insn)) {
        *predicate_skip = NULL;
        return IA64_PREPARE_NULLIFIED;
    }
    skip = ia64_gen_predicate_skip(ctx, insn, qp_value);
    *predicate_skip = skip;
    if (insn->placement_illegal) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ia64 illegal placement ip=0x%016" PRIx64
                      " slot=%u opcode=%u raw=0x%010" PRIx64 "\n",
                      insn->address, insn->slot, insn->opcode, insn->raw);
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }
    if (insn->reserved_field) {
        tcg_gen_st_i64(tcg_constant_i64(0x30), tcg_env,
                       offsetof(CPUIA64State, cr_isr));
        ia64_gen_raise_exception(IA64_EXCP_RESERVED_REG_FIELD,
                                  insn->address, insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }
    ia64_gen_check_fp_pair_bank(insn);
    required_feature = ia64_insn_required_integer_feature(insn);
    if (required_feature != 0 &&
        !(ia64_env_cpu_class(ctx->env)->cpuid_features & required_feature)) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }
    if (ia64_insn_needs_16byte_atomics(insn) &&
        !(ia64_env_cpu_class(ctx->env)->cpuid_features & IA64_CPUID4_AO)) {
        /*
         * The encoding is reserved on a model that clears CPUID[4].ao, so
         * refuse it the same way an unimplemented opcode is refused.
         */
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }
    if (insn->opcode == IA64_OP_ALLOC) {
        uint32_t new_sof = insn->operands.common.immediate & 0x7f;

        if (insn->operands.common.destination == 0 ||
            (insn->operands.common.destination >= IA64_STACKED_GR_BASE &&
             insn->operands.common.destination - IA64_STACKED_GR_BASE >=
             new_sof)) {
            ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                      insn->raw, insn->slot);
            if (skip == NULL) {
                return IA64_PREPARE_NORETURN;
            }
            ia64_gen_predicate_end(skip);
            return IA64_PREPARE_COMPLETE;
        }
    } else if (ia64_insn_writes_gr_r1(insn)) {
        ia64_gen_check_gr_in_frame(insn, insn->operands.common.destination);
    }
    if (insn->reg_base_update || insn->imm_base_update) {
        ia64_gen_check_gr_in_frame(insn, insn->operands.common.source2);
    }
    if (ia64_insn_psr_ic_must_be_clear(insn)) {
        ia64_gen_check_psr_ic_clear(insn);
    }
    if (ia64_insn_is_privileged(insn)) {
        ia64_gen_check_privileged(insn);
    }
    if (ia64_load_base_update_has_same_target(insn)) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }
    if (ia64_compare_has_equal_targets(insn)) {
        ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                                  insn->raw, insn->slot);
        if (skip == NULL) {
            return IA64_PREPARE_NORETURN;
        }
        ia64_gen_predicate_end(skip);
        return IA64_PREPARE_COMPLETE;
    }

    fp_sets = ia64_insn_fp_read_sets(insn) | ia64_insn_fp_write_sets(insn);
    ia64_gen_check_disabled_fp(ctx, insn, fp_sets, skip == NULL);
    ia64_gen_sync_rotating_fr(ctx, fp_sets, skip == NULL);
    return IA64_PREPARE_DISPATCH;
}

static bool ia64_gen_predicated_integer(DisasContext *ctx,
                                         const Ia64Instruction *insn)
{
    const IA64IntegerOperands *op = &insn->operands.integer;
    IA64KnownNat old_nat, new_nat;
    TCGv_i64 result;
    bool immediate;

    if (!insn->valid || insn->placement_illegal || insn->reserved_field ||
        !insn->qp || ia64_predicate_known(ctx, insn->qp) !=
                     IA64_PREDICATE_UNKNOWN ||
        !op->destination || op->destination >= IA64_STACKED_GR_BASE ||
        op->source1 >= IA64_STACKED_GR_BASE ||
        op->source2 >= IA64_STACKED_GR_BASE) {
        return false;
    }
    switch (insn->opcode) {
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
    case IA64_OP_AND_IMM:
    case IA64_OP_OR_IMM:
    case IA64_OP_XOR_IMM:
        immediate = true;
        break;
    case IA64_OP_ADD:
    case IA64_OP_SUB:
    case IA64_OP_AND:
    case IA64_OP_OR:
    case IA64_OP_XOR:
        immediate = false;
        break;
    default:
        return false;
    }
    result = tcg_temp_new_i64();
    switch (insn->opcode) {
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
        tcg_gen_addi_i64(result, ia64_gr_src(op->source2), op->immediate);
        break;
    case IA64_OP_AND_IMM:
        tcg_gen_andi_i64(result, ia64_gr_src(op->source2), op->immediate);
        break;
    case IA64_OP_OR_IMM:
        tcg_gen_ori_i64(result, ia64_gr_src(op->source2), op->immediate);
        break;
    case IA64_OP_XOR_IMM:
        tcg_gen_xori_i64(result, ia64_gr_src(op->source2), op->immediate);
        break;
    case IA64_OP_ADD:
        tcg_gen_add_i64(result, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_SUB:
        tcg_gen_sub_i64(result, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_AND:
        tcg_gen_and_i64(result, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_OR:
        tcg_gen_or_i64(result, ia64_gr_src(op->source1),
                       ia64_gr_src(op->source2));
        break;
    case IA64_OP_XOR:
        tcg_gen_xor_i64(result, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_movcond_i64(TCG_COND_NE, cpu_gr[op->destination],
                        cpu_pr[insn->qp], tcg_constant_i64(0), result,
                        cpu_gr[op->destination]);
    old_nat = ia64_nat_known(ctx, op->destination);
    new_nat = ia64_nat_result_known(ctx, insn, old_nat);
    if (new_nat == IA64_NAT_UNKNOWN || new_nat != old_nat) {
        TCGv_i64 nat = tcg_temp_new_i64();
        TCGv_i64 bit;

        if (new_nat != IA64_NAT_UNKNOWN) {
            bit = tcg_constant_i64(new_nat);
        } else if (immediate) {
            bit = ia64_gen_gr_nat_read(op->source2);
        } else {
            bit = tcg_temp_new_i64();
            tcg_gen_or_i64(bit, ia64_gen_gr_nat_read(op->source1),
                           ia64_gen_gr_nat_read(op->source2));
        }
        tcg_gen_deposit_i64(nat, cpu_nat[0], bit, op->destination, 1);
        tcg_gen_movcond_i64(TCG_COND_NE, cpu_nat[0], cpu_pr[insn->qp],
                            tcg_constant_i64(0), nat, cpu_nat[0]);
    }
    ctx->reg.current_qp_known = false;
    ctx->reg.current_qp_value = false;
    return true;
}

bool ia64_gen_insn(DisasContext *ctx, const Ia64Instruction *insn,
                   bool record_iipa)
{
    TCGLabel *skip = NULL;
    IA64PrepareResult prepare;
    IA64GenResult result;
    IA64GeneratorKind generator = ia64_insn_generator(insn);
    const bool track_psr_suppression = ctx->restart.track_psr_suppression;

    if (ia64_gen_predicated_integer(ctx, insn)) {
        ia64_gen_note_successful_bundle(ctx, insn->address, record_iipa,
                                        track_psr_suppression);
        return false;
    }
    prepare = ia64_gen_prepare_insn(ctx, insn, &skip);
    if (prepare == IA64_PREPARE_NORETURN) {
        return true;
    }
    if (prepare == IA64_PREPARE_NULLIFIED) {
        ia64_gen_note_successful_bundle(ctx, insn->address, record_iipa,
                                        track_psr_suppression);
        return false;
    }
    if (prepare == IA64_PREPARE_COMPLETE) {
        /* The dynamically false predicate completed the instruction. */
        ia64_gen_note_successful_bundle(ctx, insn->address, record_iipa,
                                        track_psr_suppression);
        return false;
    }
    ia64_invalidate_fp_state(ctx, insn);
    ia64_update_psr_ic_known(ctx, insn, skip == NULL);
    result = ia64_gen_dispatch(ctx, insn, generator, skip, record_iipa,
                               track_psr_suppression);
    if (result == IA64_GEN_NORETURN) {
        return true;
    }
    if (result == IA64_GEN_CONTINUE) {
        ia64_update_fp_written_state(ctx, insn, skip == NULL);
        ia64_update_predicate_known(ctx, insn);
        ia64_update_rse_dirty_known(ctx, insn);
        ia64_gen_predicate_end(skip);
        ia64_gen_note_successful_bundle(ctx, insn->address, record_iipa,
                                        track_psr_suppression);
        return false;
    }

    if (insn->opcode != IA64_OP_ILLEGAL) {
        g_assert_not_reached();
    }
    ia64_gen_raise_exception(IA64_EXCP_ILLEGAL, insn->address,
                             insn->raw, insn->slot);
    if (skip == NULL) {
        return true;
    }

    ia64_gen_predicate_end(skip);
    ia64_gen_note_successful_bundle(ctx, insn->address, record_iipa,
                                    track_psr_suppression);
    return false;
}

static void ia64_tr_init_disas_context(DisasContextBase *db, CPUState *cs)
{
    DisasContext *ctx = container_of(db, DisasContext, base);
    uint32_t flags = ctx->base.tb->flags;

    ctx->env = cpu_env(cs);
    ctx->fp = (IA64TranslationFPState) { 0 };
    if (flags & IA64_TB_FLAG_IRQ_DEFER) {
        ctx->base.max_insns = 1;
    }
    ctx->memory.mmu_idx = (flags & IA64_TB_FLAG_DT) ?
        MMU_IDX_VIRT_CPL((flags & IA64_TB_FLAG_CPL_MASK) >>
                         IA64_TB_FLAG_CPL_SHIFT) :
        MMU_PHYS_IDX;
    ctx->restart.start_slot = (ctx->base.tb->flags & IA64_TB_FLAG_RI_MASK) >>
                      IA64_TB_FLAG_RI_SHIFT;
    if (ctx->restart.start_slot > 2) {
        ctx->restart.start_slot = 0;
    }
    ctx->restart.current_ri = ctx->restart.start_slot;
    ctx->restart.current_ri_known = true;
    ctx->restart.psr_ic_known = true;
    ctx->restart.psr_ic = flags & IA64_TB_FLAG_PSR_IC;
    ctx->restart.track_iipa = ctx->base.tb->flags & IA64_TB_FLAG_PSR_IC;
    ctx->restart.track_psr_suppression =
        ctx->base.tb->flags & IA64_TB_FLAG_PSR_SUPPRESS;
    ctx->restart.native_trap_psr_dynamic = false;
    ctx->restart.instruction_debug_psr_dynamic = false;
    ctx->memory.be_data = ctx->base.tb->flags & IA64_TB_FLAG_BE;
    ctx->memory.full_alat = ctx->env->alat_state.alat_full;
    ctx->memory.direct_chain_nat_safe = true;
    if (flags & IA64_TB_FLAG_NAT_CLEAR) {
        ctx->memory.nat_known_clear[0] = UINT64_MAX;
        ctx->memory.nat_known_clear[1] = UINT64_MAX;
    } else {
        ctx->memory.nat_known_clear[0] = 1;
        ctx->memory.nat_known_clear[1] = 0;
    }
    ctx->memory.nat_known_set[0] = 0;
    ctx->memory.nat_known_set[1] = 0;
    ctx->reg.cfm_sof_valid = false;
    ctx->reg.cfm_sof_checked = 0;
    ctx->reg.pr_known_zero = 0;
    ctx->reg.pr_known_one = 1;
    ctx->reg.rse_dirty_known[0] = 0;
    ctx->reg.rse_dirty_known[1] = 0;
    ctx->reg.cpl = (flags & IA64_TB_FLAG_CPL_MASK) >>
                   IA64_TB_FLAG_CPL_SHIFT;
    ctx->reg.cpl_known = true;
    ctx->restart.instruction_group_start =
        ctx->base.tb->flags & IA64_TB_FLAG_GROUP_START;
    ctx->restart.next_instruction_group_start =
        ctx->restart.instruction_group_start;
}

static void ia64_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
    if (db->tb->flags & IA64_TB_FLAG_IRQ_DEFER) {
        tcg_gen_st8_i32(tcg_constant_i32(0), tcg_env,
                       offsetof(CPUIA64State, exception_state.psr_i_deferred));
    }
}

static void ia64_tr_insn_start(DisasContextBase *db, CPUState *cs)
{
    tcg_gen_insn_start(db->pc_next, 0, 0);
}

static void ia64_gen_check_instruction_debug(DisasContext *ctx,
                                             uint64_t address, uint8_t slot)
{
    if (!ctx->restart.instruction_debug_psr_dynamic) {
        if (!(ctx->base.tb->flags & IA64_TB_FLAG_PSR_DB)) {
            return;
        }
        gen_helper_instruction_debug(tcg_env, tcg_constant_i64(address),
                                     tcg_constant_i32(slot));
    } else {
        TCGv_i64 enabled = tcg_temp_new_i64();
        TCGLabel *done = gen_new_label();

        tcg_gen_andi_i64(enabled, cpu_psr, IA64_PSR_DB);
        tcg_gen_brcondi_i64(TCG_COND_EQ, enabled, 0, done);
        gen_helper_instruction_debug(tcg_env, tcg_constant_i64(address),
                                     tcg_constant_i32(slot));
        gen_set_label(done);
    }
}

static void ia64_gen_snapshot_psr_before_insn(void)
{
    tcg_gen_st_i64(cpu_psr, tcg_env,
                   offsetof(CPUIA64State,
                            exception_state.psr_before_insn));
}

static void ia64_gen_check_sequential_native_traps(
    const Ia64Instruction *insn, bool mlx_long)
{
    uint64_t target_ip = insn->address;
    uint8_t target_slot = insn->slot + 1;

    /* rfi restores an interrupted context and never raises completion traps. */
    if (insn->opcode == IA64_OP_RFI) {
        return;
    }
    if (insn->slot == 2 || (insn->slot == 1 && mlx_long)) {
        target_ip += 16;
        target_slot = 0;
    }
    ia64_gen_check_static_native_traps(
        insn->ctx, target_ip, target_slot, insn->address, insn->slot, 0, 0);
}

static void ia64_tr_translate_insn(DisasContextBase *db, CPUState *cs)
{
    DisasContext *ctx = container_of(db, DisasContext, base);
    const uint64_t bundle_ip = db->pc_next;
    uint64_t low;
    uint64_t high;
    uint8_t template_code;
    const IA64TemplateInfo *template_info;
    uint64_t slots[3];
    Ia64Instruction decoded[3];
    bool skip_x_slot;
    bool record_iipa;
    bool psr_ic_modified;
    int slot;

    if (ctx->base.tb->flags & IA64_TB_FLAG_PSR_IS) {
        db->pc_next = bundle_ip + 1;
        gen_helper_ia32_unsupported(tcg_env);
        db->is_jmp = DISAS_NORETURN;
        return;
    }

    low = translator_ldq_end(ctx->env, db, bundle_ip, MO_LE);
    high = translator_ldq_end(ctx->env, db, bundle_ip + 8, MO_LE);
    template_code = ia64_bundle_template_code(low);
    template_info = ia64_template_info(template_code);

    ctx->restart.exit_after_bundle = false;
    db->pc_next = bundle_ip + 16;

    if (!template_info->defined) {
        ia64_gen_check_instruction_debug(ctx, bundle_ip,
                                         ctx->restart.start_slot);
        ia64_gen_raise_exception(IA64_EXCP_RESERVED_TEMPLATE, bundle_ip,
                                  template_code, 0);
        db->is_jmp = DISAS_NORETURN;
        return;
    }

    slots[0] = ia64_bundle_slot(low, high, 0);
    slots[1] = ia64_bundle_slot(low, high, 1);
    slots[2] = ia64_bundle_slot(low, high, 2);
    skip_x_slot = false;
    for (slot = ctx->restart.start_slot; slot < 3; slot++) {
        if (skip_x_slot && slot == 2) {
            break;
        }
        decoded[slot] = ia64_decode_insn_for_model(
            ctx->env, template_info->units[slot], slots[slot], bundle_ip, slot);
        ia64_apply_mlx_long_fixup(template_code, slots, slot, &decoded[slot],
                                  &skip_x_slot);
    }
    ia64_prepare_self_counted_loop(ctx, decoded, skip_x_slot, bundle_ip);

    record_iipa = true;
    psr_ic_modified = false;
    for (slot = ctx->restart.start_slot; slot < 3; ++slot) {
        ctx->restart.native_trap_saved_psr = false;
        if (skip_x_slot && slot == 2) {
            continue;
        }

        Ia64Instruction insn = decoded[slot];
        bool known_nullified;
        bool stop_after;
        bool track_iipa_for_insn;

        insn.ctx = ctx;
        stop_after = template_info->stop_after[
            skip_x_slot && slot == 1 ? 2 : slot];
        insn.placement_illegal =
            (ia64_insn_must_start_group(&insn) &&
             !ctx->restart.instruction_group_start) ||
            (ia64_insn_must_end_group(&insn) && !stop_after) ||
            (ia64_insn_requires_slot2(&insn) && slot != 2) ||
            ia64_insn_has_invalid_fp_target(&insn) ||
            ia64_insn_has_illegal_register(&insn);
        insn.reserved_field = ia64_insn_has_reserved_mask_field(&insn);
        ctx->restart.next_instruction_group_start = stop_after;
        known_nullified =
            ia64_predicate_nullifies_insn(&insn) &&
            ia64_predicate_known(ctx, insn.qp) == IA64_PREDICATE_ZERO;
        ctx->memory.direct_chain_nat_safe = known_nullified ||
            ia64_insn_preserves_all_nat_clear(ctx, &insn);
        if (!known_nullified && ia64_insn_may_set_psr_ic(&insn)) {
            ctx->restart.track_iipa = true;
        }
        if (!known_nullified && ia64_insn_may_modify_psr_ic(&insn)) {
            psr_ic_modified = true;
        }
        track_iipa_for_insn = ctx->restart.track_iipa;
        ia64_gen_set_ri_tracked(ctx, slot);
        ia64_gen_check_instruction_debug(ctx, bundle_ip, slot);
        if (!known_nullified &&
            ia64_insn_may_modify_native_trap_psr(&insn)) {
            ia64_gen_snapshot_psr_before_insn();
            ctx->restart.native_trap_saved_psr = true;
        }
        if (ia64_insn_is_empty_hint(&insn) &&
            !(record_iipa && track_iipa_for_insn) &&
            !ctx->restart.track_psr_suppression) {
            ia64_gen_check_sequential_native_traps(&insn, skip_x_slot);
            ia64_gen_advance_restart_point(ctx, bundle_ip, slot,
                                           skip_x_slot);
            ctx->restart.instruction_group_start =
                ctx->restart.next_instruction_group_start;
            continue;
        }
        if (ctx->restart.track_psr_suppression) {
            TCGv_i64 suppression = tcg_temp_new_i64();

            tcg_gen_andi_i64(suppression, cpu_psr,
                             IA64_PSR_FAULT_SUPPRESS_MASK);
            tcg_gen_st_i64(suppression, tcg_env,
                           offsetof(CPUIA64State,
                                    exception_state.
                                    psr_suppression_before_insn));
        }
        if (ia64_gen_insn(ctx, &insn, record_iipa && track_iipa_for_insn)) {
            db->is_jmp = DISAS_NORETURN;
            return;
        }
        ia64_gen_check_sequential_native_traps(&insn, skip_x_slot);
        ctx->restart.native_trap_saved_psr = false;
        if (!known_nullified && insn.opcode == IA64_OP_MOV_GRPSR) {
            ctx->restart.native_trap_psr_dynamic = true;
            ctx->restart.instruction_debug_psr_dynamic = true;
        }
        if (!known_nullified && ia64_insn_may_modify_psr_ri(&insn)) {
            ctx->restart.current_ri_known = false;
        }
        ia64_gen_advance_restart_point(ctx, bundle_ip, slot, skip_x_slot);
        ia64_update_nat_known(ctx, &insn);
        ia64_update_cached_register_state(ctx, &insn);
        ctx->memory.direct_chain_nat_safe = true;
        ctx->restart.instruction_group_start =
            ctx->restart.next_instruction_group_start;
        if (track_iipa_for_insn && !psr_ic_modified) {
            record_iipa = false;
        }
        ctx->restart.track_psr_suppression =
            !ia64_current_insn_cannot_execute(ctx, &insn) &&
            ia64_insn_may_set_fault_suppression(&insn);
    }

    ctx->restart.start_slot = 0;
    /* Preserve translator_io_start() requests for timer register accesses. */
    if (ctx->restart.exit_after_bundle) {
        db->is_jmp = IA64_DISAS_EXIT;
    } else if (!translator_is_same_page(db, db->pc_next)) {
        /*
         * Bundles are 16-byte aligned and cannot straddle a 4 KiB target
         * page.  Do not fetch the next page until all bundles from this page
         * have executed; a translation fault there must not discard them.
         */
        db->is_jmp = DISAS_TOO_MANY;
    }
}

void ia64_gen_goto_tb_group(DisasContext *ctx, uint64_t dest,
                            bool group_start)
{
    uint8_t slot = ctx->branch.goto_tb_slots;
    bool nats_clear = ia64_all_gr_nats_known_clear(ctx);

    ia64_gen_store_instruction_group_start(group_start);
    ia64_gen_save_fault_slot_for_exit(ctx);
    ia64_gen_clear_ri();
    tcg_gen_movi_i64(cpu_ip, dest);
    if (slot < 2 && ctx->memory.direct_chain_nat_safe &&
        !(ctx->base.tb->flags & IA64_TB_FLAG_IRQ_DEFER) &&
        translator_use_goto_tb(&ctx->base, dest)) {
        TCGLabel *lookup = NULL;

        if (!nats_clear) {
            TCGv_i64 nats = tcg_temp_new_i64();

            /* Keep the successor's NaT TB key stable across executions. */
            lookup = gen_new_label();
            tcg_gen_or_i64(nats, cpu_nat[0], cpu_nat[1]);
            tcg_gen_brcondi_i64(
                (ctx->base.tb->flags & IA64_TB_FLAG_NAT_CLEAR) ?
                TCG_COND_NE : TCG_COND_EQ, nats, 0, lookup);
        }
        ctx->branch.goto_tb_slots = slot + 1;
        tcg_gen_goto_tb(slot);
        tcg_gen_exit_tb(ctx->base.tb, slot);
        if (lookup) {
            gen_set_label(lookup);
            ia64_gen_lookup_or_exit(ctx);
        }
    } else {
        ia64_gen_lookup_or_exit(ctx);
    }
}

static void ia64_gen_goto_tb(DisasContext *ctx, uint64_t dest)
{
    ia64_gen_goto_tb_group(ctx, dest, ctx->restart.instruction_group_start);
}

static void ia64_tr_tb_stop(DisasContextBase *db, CPUState *cs)
{
    DisasContext *ctx = container_of(db, DisasContext, base);

    switch (db->is_jmp) {
    case IA64_DISAS_EXIT:
    case DISAS_TOO_MANY:
        ia64_gen_goto_tb(ctx, db->pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static const char *ia64_unit_log_name(IA64SlotUnit unit)
{
    switch (unit) {
    case IA64_UNIT_M:
        return "M";
    case IA64_UNIT_I:
        return "I";
    case IA64_UNIT_B:
        return "B";
    case IA64_UNIT_F:
        return "F";
    case IA64_UNIT_L:
        return "L";
    case IA64_UNIT_X:
        return "X";
    case IA64_UNIT_RESERVED:
    default:
        return "reserved";
    }
}

static bool ia64_tr_disas_log(const DisasContextBase *db, CPUState *cs,
                              FILE *logfile)
{
    const DisasContext *ctx = container_of(db, DisasContext, base);
    vaddr bundle_ip = db->pc_first;
    size_t size = translator_st_len(db);
    int start_slot = ctx->restart.start_slot;

    fprintf(logfile, "QEMU-IA64-DECODE: pc=0x%016" PRIx64
            " size=%zu\n", (uint64_t)db->pc_first, size);

    while (size >= 16) {
        uint8_t bundle[16];
        uint64_t low;
        uint64_t high;
        uint8_t template_code;
        const IA64TemplateInfo *template_info;
        uint64_t slots[3];
        bool skip_x_slot = false;
        int slot;

        if (!translator_st(db, bundle, bundle_ip, sizeof(bundle))) {
            fprintf(logfile,
                    "QEMU-IA64-DECODE: read-failed pc=0x%016" PRIx64 "\n",
                    (uint64_t)bundle_ip);
            break;
        }

        low = ldq_le_p(bundle);
        high = ldq_le_p(bundle + 8);
        template_code = ia64_bundle_template_code(low);
        template_info = ia64_template_info(template_code);
        slots[0] = ia64_bundle_slot(low, high, 0);
        slots[1] = ia64_bundle_slot(low, high, 1);
        slots[2] = ia64_bundle_slot(low, high, 2);

        fprintf(logfile,
                "QEMU-IA64-DECODE: bundle=0x%016" PRIx64
                " template=0x%02x name=%s defined=%u\n",
                (uint64_t)bundle_ip, template_code, template_info->name,
                template_info->defined ? 1 : 0);

        if (template_info->defined) {
            for (slot = start_slot; slot < 3; ++slot) {
                IA64SlotUnit unit;
                Ia64Instruction insn;

                if (skip_x_slot && slot == 2) {
                    continue;
                }

                unit = template_info->units[slot];
                insn = ia64_decode_insn_for_model(
                    ctx->env, unit, slots[slot], bundle_ip, slot);
                ia64_apply_mlx_long_fixup(template_code, slots, slot, &insn,
                                          &skip_x_slot);
                fprintf(logfile,
                        "QEMU-IA64-DECODE: slot=%d unit=%s opcode=%u"
                        " valid=%u qp=%u raw=0x%010" PRIx64 "\n",
                        slot, ia64_unit_log_name(insn.unit),
                        (unsigned)insn.opcode, insn.valid ? 1 : 0,
                        insn.qp, insn.raw);
            }
        }

        bundle_ip += 16;
        size -= 16;
        start_slot = 0;
    }

    return false;
}

void ia64_translate_init(void)
{
    static const char * const gr_names[IA64_GR_COUNT] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
        "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
        "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",
        "r48", "r49", "r50", "r51", "r52", "r53", "r54", "r55",
        "r56", "r57", "r58", "r59", "r60", "r61", "r62", "r63",
        "r64", "r65", "r66", "r67", "r68", "r69", "r70", "r71",
        "r72", "r73", "r74", "r75", "r76", "r77", "r78", "r79",
        "r80", "r81", "r82", "r83", "r84", "r85", "r86", "r87",
        "r88", "r89", "r90", "r91", "r92", "r93", "r94", "r95",
        "r96", "r97", "r98", "r99", "r100", "r101", "r102", "r103",
        "r104", "r105", "r106", "r107", "r108", "r109", "r110",
        "r111", "r112", "r113", "r114", "r115", "r116", "r117",
        "r118", "r119", "r120", "r121", "r122", "r123", "r124",
        "r125", "r126", "r127",
    };
    static const char * const pr_names[IA64_PR_COUNT] = {
        "p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7",
        "p8", "p9", "p10", "p11", "p12", "p13", "p14", "p15",
        "p16", "p17", "p18", "p19", "p20", "p21", "p22", "p23",
        "p24", "p25", "p26", "p27", "p28", "p29", "p30", "p31",
        "p32", "p33", "p34", "p35", "p36", "p37", "p38", "p39",
        "p40", "p41", "p42", "p43", "p44", "p45", "p46", "p47",
        "p48", "p49", "p50", "p51", "p52", "p53", "p54", "p55",
        "p56", "p57", "p58", "p59", "p60", "p61", "p62", "p63",
    };
    static const char * const br_names[IA64_BR_COUNT] = {
        "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7",
    };
    int i;

    for (i = 0; i < IA64_GR_COUNT; ++i) {
        cpu_gr[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUIA64State, gr[i]),
                                           gr_names[i]);
    }
    for (i = 0; i < IA64_PR_COUNT; ++i) {
        cpu_pr[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUIA64State, pr[i]),
                                           pr_names[i]);
    }
    for (i = 0; i < IA64_BR_COUNT; ++i) {
        cpu_br[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUIA64State, br[i]),
                                           br_names[i]);
    }
    cpu_ip = tcg_global_mem_new_i64(tcg_env, offsetof(CPUIA64State, ip), "ip");
    cpu_psr = tcg_global_mem_new_i64(tcg_env, offsetof(CPUIA64State, psr),
                                     "psr");
    for (i = 0; i < IA64_FR_COUNT; ++i) {
        cpu_fr[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUIA64State, fp.fr[i]),
                                           "fr");
    }
    for (i = 0; i < 2; ++i) {
        cpu_nat[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUIA64State, nat[i]),
                                           "nat");
        cpu_fr_nat[i] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUIA64State,
                                                        fp.fr_nat[i]),
                                               "fr_nat");
        cpu_fr_sig[i] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUIA64State,
                                                        fp.fr_sig[i]),
                                               "fr_sig");
        cpu_fr_ext_valid[i] = tcg_global_mem_new_i64(
            tcg_env, offsetof(CPUIA64State, fp.fr_ext_valid[i]),
            "fr_ext_valid");
        cpu_fr_int_origin[i] = tcg_global_mem_new_i64(
            tcg_env, offsetof(CPUIA64State, fp.fr_int_origin[i]),
            "fr_int_origin");
        cpu_rse_gr_dirty[i] = tcg_global_mem_new_i64(
            tcg_env, offsetof(CPUIA64State, rse.rse_gr_dirty[i]),
            "rse_gr_dirty");
    }

    ia64_ia32_translate_init();
}

void ia64_translate_code(CPUState *cs, TranslationBlock *tb,
                                    int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext ctx = {};
    static const TranslatorOps ia64_tr_ops = {
        .init_disas_context = ia64_tr_init_disas_context,
        .tb_start = ia64_tr_tb_start,
        .insn_start = ia64_tr_insn_start,
        .translate_insn = ia64_tr_translate_insn,
        .tb_stop = ia64_tr_tb_stop,
        .disas_log = ia64_tr_disas_log,
    };

    if (tb->flags & IA64_TB_FLAG_PSR_IS) {
        ia64_ia32_translate_code(cs, tb, max_insns, pc, host_pc);
    } else {
        translator_loop(cs, tb, max_insns, pc, host_pc, &ia64_tr_ops,
                        &ctx.base, TCG_TYPE_VA);
    }
}
