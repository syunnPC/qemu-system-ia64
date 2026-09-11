/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Internal interfaces shared by the IA-64 TCG translation modules.
 */

#ifndef TARGET_IA64_TRANSLATE_TRANSLATE_H
#define TARGET_IA64_TRANSLATE_TRANSLATE_H

#include "exec/translator.h"
#include "tcg/tcg.h"

#include "target/ia64/cpu.h"
#include "target/ia64/decode/operand.h"

#define IA64_DISAS_EXIT DISAS_TARGET_0

/*
 * Keep low PSR bits in place and map CPL, IT, and RI with one PSR >> 30.
 * These flags are internal TB keys and are never migration state.
 */
#define IA64_TB_FLAG_BE           (1u << 1)
#define IA64_TB_FLAG_CPL_SHIFT    2
#define IA64_TB_FLAG_CPL_MASK     (3u << IA64_TB_FLAG_CPL_SHIFT)
#define IA64_TB_FLAG_PSR_SUPPRESS (1u << 5)
#define IA64_TB_FLAG_IT           (1u << 6)
#define IA64_TB_FLAG_GROUP_START  (1u << 7)
#define IA64_TB_FLAG_RI_SHIFT     11
#define IA64_TB_FLAG_RI_MASK      (3u << IA64_TB_FLAG_RI_SHIFT)
#define IA64_TB_FLAG_PSR_IC       (1u << 13)
#define IA64_TB_FLAG_DT           (1u << 17)
#define IA64_TB_FLAG_PSR_TB       (1u << 18)
#define IA64_TB_FLAG_PSR_SS       (1u << 19)
#define IA64_TB_FLAG_PSR_DB       (1u << 20)
#define IA64_TB_FLAG_NAT_CLEAR    (1u << 21)
#define IA64_TB_FLAG_IRQ_DEFER    (1u << 22)
#define IA64_TB_FLAG_IA32_PSR_DB  (1u << 29)
#define IA64_TB_FLAG_IA32_PSR_AC  (1u << 30)
#define IA64_TB_FLAG_PSR_IS       (1u << 31)

typedef struct IA64TranslationMemoryState {
    int mmu_idx;
    bool be_data;
    bool full_alat;
    bool direct_chain_nat_safe;
    uint64_t nat_known_clear[2];
    uint64_t nat_known_set[2];
} IA64TranslationMemoryState;

typedef struct IA64TranslationRestartState {
    uint8_t start_slot;
    uint8_t current_ri;
    bool current_ri_known;
    bool psr_ic_known;
    bool psr_ic;
    bool track_iipa;
    bool track_psr_suppression;
    bool exit_after_bundle;
    bool native_trap_psr_dynamic;
    bool instruction_debug_psr_dynamic;
    bool native_trap_saved_psr;
    bool instruction_group_start;
    bool next_instruction_group_start;
} IA64TranslationRestartState;

typedef struct IA64TranslationBranchState {
    uint8_t goto_tb_slots;
    TCGLabel *counted_self_label;
    TCGv_i64 counted_self_budget;
    uint64_t counted_self_ip;
    bool cloop_fill_st1_valid;
    bool counted_self_preserves_nat_clear;
    bool cloop_fill_st1_release;
    uint8_t cloop_fill_st1_base;
    uint8_t cloop_fill_st1_source;
    uint8_t cloop_fill_st1_slot;
} IA64TranslationBranchState;

typedef struct IA64TranslationRegisterState {
    TCGv_i32 cfm_sof;
    uint64_t pr_known_zero;
    uint64_t pr_known_one;
    uint64_t rse_dirty_known[2];
    uint8_t cpl;
    uint8_t cfm_sof_checked;
    bool cfm_sof_valid;
    bool cpl_known;
    bool current_qp_known;
    bool current_qp_value;
} IA64TranslationRegisterState;

typedef struct IA64TranslationSIMDState {
    /* Reused within one TB so packed operations do not consume more temps. */
    TCGv_vec a;
    TCGv_vec b;
    TCGv_vec result;
} IA64TranslationSIMDState;

typedef enum IA64TranslationFRFormat {
    IA64_FR_FORMAT_UNKNOWN,
    IA64_FR_FORMAT_BINARY,
    IA64_FR_FORMAT_SIG,
    IA64_FR_FORMAT_NAT,
} IA64TranslationFRFormat;

typedef struct IA64TranslationFPState {
    uint8_t format[IA64_FR_COUNT];
    uint8_t enabled_sets;
    uint8_t written_sets;
    bool rotating_synced;
    bool rotating_live;
} IA64TranslationFPState;

typedef struct DisasContext {
    DisasContextBase base;
    CPUIA64State *env;
    IA64TranslationMemoryState memory;
    IA64TranslationRestartState restart;
    IA64TranslationBranchState branch;
    IA64TranslationRegisterState reg;
    IA64TranslationSIMDState simd;
    IA64TranslationFPState fp;
} DisasContext;

typedef enum IA64GenResult {
    IA64_GEN_UNHANDLED,
    IA64_GEN_CONTINUE,
    IA64_GEN_NORETURN,
} IA64GenResult;

typedef enum Ia64NatConsumptionKind {
    IA64_NAT_ACCESS,
    IA64_NAT_NON_ACCESS,
} Ia64NatConsumptionKind;

typedef enum IA64FPRegisterLoadFormat {
    IA64_FP_REGISTER_LOAD_DOUBLE,
    IA64_FP_REGISTER_LOAD_SINGLE,
    IA64_FP_REGISTER_LOAD_SIGNIFICAND,
} IA64FPRegisterLoadFormat;

typedef enum IA64BranchTargetKind {
    IA64_BRANCH_TARGET_DIRECT,
    IA64_BRANCH_TARGET_TCG,
    IA64_BRANCH_TARGET_CURRENT,
} IA64BranchTargetKind;

typedef struct IA64BranchCompletion {
    IA64BranchTargetKind target_kind;
    uint64_t direct_target;
    TCGv_i64 tcg_target;
    uint64_t completed_ip;
    bool record_iipa;
    bool track_psr_suppression;
    TCGv_i32 trap_conditions;
    bool skip_native_traps;
} IA64BranchCompletion;

extern TCGv_i64 cpu_ip;
extern TCGv_i64 cpu_gr[IA64_GR_COUNT];
extern TCGv_i64 cpu_pr[IA64_PR_COUNT];
extern TCGv_i64 cpu_br[IA64_BR_COUNT];
extern TCGv_i64 cpu_psr;

TCGv_i64 ia64_gr_src(uint8_t reg);
TCGv_i64 ia64_gen_fr_sig_read(DisasContext *ctx, uint8_t reg);
TCGv_i64 ia64_gen_fr_sig_mask_read(DisasContext *ctx, uint32_t word,
                                   uint64_t mask);
TCGv_i64 ia64_fr_significand_src(uint8_t reg);
TCGv_i64 ia64_gen_fr_nat_read(DisasContext *ctx, uint8_t reg);
TCGv_i64 ia64_gen_fr_special_read(DisasContext *ctx, uint8_t reg);
TCGv_i64 ia64_gen_fr_fmov_slow_read(DisasContext *ctx, uint8_t reg);
TCGv_i64 ia64_fr_binary_src(uint8_t reg);
void ia64_gen_fr_read_double(DisasContext *ctx, TCGv_i64 result, uint8_t reg);
TCGv_i64 ia64_gen_gr_nat_read(uint8_t reg);
bool ia64_gr_nat_is_known_clear(const Ia64Instruction *insn, uint8_t reg);
bool ia64_gr_nat_is_known_set(const Ia64Instruction *insn, uint8_t reg);
void ia64_gen_gr_nat_clear(const Ia64Instruction *insn, uint8_t reg);
void ia64_gen_gr_nat_set(const Ia64Instruction *insn, uint8_t reg);
void ia64_gen_gr_nat_assign(const Ia64Instruction *insn, uint8_t reg,
                            TCGv_i64 bit);
void ia64_gen_gr_nat_or_from_1(const Ia64Instruction *insn, uint8_t dst,
                               uint8_t src);
void ia64_gen_gr_nat_from_1(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src);
void ia64_gen_gr_nat_from_2(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src1, uint8_t src2);
void ia64_gen_gr_nat_from_3(const Ia64Instruction *insn, uint8_t dst,
                            uint8_t src1, uint8_t src2, uint8_t src3);
void ia64_gen_fr_nat_from_gr(uint8_t dst, uint8_t src);
void ia64_gen_fr_mov(DisasContext *ctx, uint8_t reg, TCGv_i64 value);
void ia64_gen_fr_mov_sig(DisasContext *ctx, uint8_t reg, TCGv_i64 value);
void ia64_gen_fr_mov_s(DisasContext *ctx, uint8_t reg, TCGv_i64 value);
void ia64_gen_qemu_ld_i64(DisasContext *ctx, TCGv_i64 value, TCGv_i64 addr,
                          int mmu_idx, MemOp memop);
void ia64_gen_qemu_st_i64(DisasContext *ctx, TCGv_i64 value, TCGv_i64 addr,
                          int mmu_idx, MemOp memop);
void ia64_gen_qemu_ld_i128(DisasContext *ctx, TCGv_i128 value, TCGv_i64 addr,
                           int mmu_idx, MemOp memop);
void ia64_gen_qemu_st_i128(DisasContext *ctx, TCGv_i128 value, TCGv_i64 addr,
                           int mmu_idx, MemOp memop);
void ia64_gen_atomic_xchg_i64(DisasContext *ctx, TCGv_i64 result,
                              TCGv_i64 addr, TCGv_i64 value, int mmu_idx,
                              MemOp memop);
void ia64_gen_atomic_cmpxchg_i64(DisasContext *ctx, TCGv_i64 result,
                                 TCGv_i64 addr, TCGv_i64 cmp,
                                 TCGv_i64 value, int mmu_idx, MemOp memop);
void ia64_gen_atomic_fetch_add_i64(DisasContext *ctx, TCGv_i64 result,
                                   TCGv_i64 addr, TCGv_i64 value, int mmu_idx,
                                   MemOp memop);
void ia64_gen_fr_load(DisasContext *ctx, uint8_t reg, TCGv_i64 addr,
                      int mmu_idx, MemOp memop,
                      IA64FPRegisterLoadFormat format);
void ia64_gen_fr_set_nat(DisasContext *ctx, uint8_t reg);
void ia64_gen_predicate_test_write(const Ia64Instruction *insn,
                                   TCGv_i64 cond, TCGv_i64 not_cond);
void ia64_gen_gr_write_nat_clear(const Ia64Instruction *insn, uint8_t reg,
                                 TCGv_i64 value);
void ia64_gen_check_nat_register(const Ia64Instruction *insn, uint8_t reg);
void ia64_gen_check_nat_consumption(const Ia64Instruction *insn,
                                    uint8_t reg, uint64_t isr_access,
                                    Ia64NatConsumptionKind kind);
void ia64_gen_gr_nat_from_1_or_unimplemented_va(
    const Ia64Instruction *insn, uint8_t dst, uint8_t src,
    uint8_t impl_va_msb);
MemOp ia64_data_memop(DisasContext *ctx, MemOp memop);
void ia64_gen_check_data_debug(DisasContext *ctx,
                               const Ia64Instruction *insn, TCGv_i64 addr,
                               uint32_t size, uint64_t isr_access);
void ia64_gen_check_alignment_access(DisasContext *ctx,
                                     const Ia64Instruction *insn,
                                     TCGv_i64 addr, uint32_t size,
                                     bool always_fault,
                                     uint64_t isr_access);
void ia64_gen_check_alignment_model(DisasContext *ctx,
                                    const Ia64Instruction *insn,
                                    TCGv_i64 addr, uint32_t debug_size,
                                    uint32_t datum_size,
                                    uint32_t natural_alignment,
                                    IA64AlignmentClass alignment_class,
                                    uint64_t isr_access);
void ia64_gen_check_alignment_only(const Ia64Instruction *insn,
                                   TCGv_i64 addr, uint32_t datum_size,
                                   uint32_t natural_alignment,
                                   IA64AlignmentClass alignment_class,
                                   uint64_t isr_access);
void ia64_gen_check_alignment(DisasContext *ctx,
                              const Ia64Instruction *insn, TCGv_i64 addr,
                              uint32_t size, bool always_fault, bool is_write);
void ia64_gen_check_fr_nat_consumption(const Ia64Instruction *insn,
                                       uint8_t reg, uint64_t isr_access);
void ia64_gen_memory_acquire(const Ia64Instruction *insn);
void ia64_gen_memory_release(const Ia64Instruction *insn);
bool ia64_ar_is_simple(uint32_t ar);
void ia64_gen_read_simple_ar(TCGv_i64 value, uint32_t ar);
void ia64_gen_write_simple_ar(uint32_t ar, TCGv_i64 value);
void ia64_gen_validate_ar_access(const Ia64Instruction *insn,
                                 TCGv_i64 value, bool write);
bool ia64_ar_access_reads_clock(uint32_t ar_num);
bool ia64_clock_access_needs_io(const DisasContext *ctx);
void ia64_gen_validate_cr_access(TCGv_i64 result,
                                 const Ia64Instruction *insn,
                                 TCGv_i64 value, bool write);
bool ia64_cr_is_read_only(uint32_t cr_num);
bool ia64_cr_write_reads_clock(uint32_t cr_num);
void ia64_gen_raise_exception(uint32_t exception, uint64_t fault_ip,
                              uint64_t fault_imm, uint32_t fault_slot);
void ia64_gen_check_native_traps(TCGv_i64 target_ip, uint8_t target_slot,
                                 uint64_t source_ip, uint8_t source_slot,
                                 TCGv_i32 conditions, uint64_t base_isr,
                                 TCGv_i64 trap_psr);
void ia64_gen_check_register_index(const Ia64Instruction *insn,
                                   TCGv_i64 index, uint32_t count);
void ia64_gen_check_reserved_bits(const Ia64Instruction *insn,
                                  TCGv_i64 value, uint64_t allowed);
void ia64_gen_write_user_mask(TCGv_i64 value);
void ia64_gen_exit_to_completed(DisasContext *ctx, uint64_t ip,
                                uint64_t completed_ip, bool record_iipa,
                                bool track_psr_suppression);
void ia64_gen_exit_to_slot_completed(DisasContext *ctx, uint64_t ip,
                                     uint8_t slot, uint64_t completed_ip,
                                     bool record_iipa,
                                     bool track_psr_suppression);
void ia64_gen_sync_ip_for_helper(const Ia64Instruction *insn);
void ia64_gen_note_stacked_gr_write(const Ia64Instruction *insn, uint8_t reg);
bool ia64_insn_must_start_group(const Ia64Instruction *insn);
bool ia64_insn_must_end_group(const Ia64Instruction *insn);
bool ia64_insn_requires_slot2(const Ia64Instruction *insn);
bool ia64_insn_has_invalid_fp_target(const Ia64Instruction *insn);
bool ia64_insn_has_illegal_register(const Ia64Instruction *insn);
bool ia64_insn_has_reserved_mask_field(const Ia64Instruction *insn);
bool ia64_insn_is_empty_hint(const Ia64Instruction *insn);
void ia64_gen_advance_restart_point(DisasContext *ctx, uint64_t bundle_ip,
                                    uint8_t slot, bool mlx_long);
void ia64_gen_set_ri_tracked(DisasContext *ctx, uint8_t slot);
void ia64_gen_clear_ri(void);
void ia64_gen_save_fault_slot_for_exit(DisasContext *ctx);
void ia64_gen_store_instruction_group_start(bool group_start);
void ia64_gen_goto_tb_group(DisasContext *ctx, uint64_t dest,
                            bool group_start);
void ia64_update_nat_known(DisasContext *ctx,
                           const Ia64Instruction *insn);
bool ia64_gen_insn(DisasContext *ctx, const Ia64Instruction *insn,
                   bool record_iipa);
void ia64_gen_check_privileged(const Ia64Instruction *insn);
void ia64_gen_check_virtualization(const Ia64Instruction *insn);
void ia64_gen_check_cover_virtualization(const Ia64Instruction *insn);
void ia64_gen_check_branch(DisasContext *ctx, TCGv_i64 failed,
                           uint64_t target, uint64_t completed_ip,
                           bool record_iipa,
                           bool track_psr_suppression);
bool ia64_is_pal_proc_break(CPUIA64State *env, uint64_t address);
bool ia64_is_firmware_debug_break(uint64_t address, uint64_t imm);

bool ia64_gen_completed_direct_branch(DisasContext *ctx, TCGLabel *skip,
                                      uint64_t target,
                                      uint64_t completed_ip,
                                      bool record_iipa,
                                      bool track_psr_suppression);
void ia64_gen_lookup_tcg_completed(DisasContext *ctx, TCGv_i64 ip,
                                   uint64_t completed_ip, bool record_iipa,
                                   bool track_psr_suppression);
bool ia64_gen_fill_st1_cloop(DisasContext *ctx,
                             const Ia64Instruction *insn,
                             uint64_t target, TCGLabel *l_nobr,
                             bool record_iipa,
                             bool track_psr_suppression);
bool ia64_gen_self_counted_loop(DisasContext *ctx, uint64_t target,
                                uint64_t completed_ip, bool record_iipa,
                                bool track_psr_suppression);
void ia64_gen_goto_completed(DisasContext *ctx, uint64_t ip,
                             uint64_t completed_ip, bool record_iipa,
                             bool track_psr_suppression);
void ia64_gen_lookup_current_completed(DisasContext *ctx,
                                       uint64_t completed_ip,
                                       bool record_iipa,
                                       bool track_psr_suppression,
                                       TCGv_i32 trap_conditions,
                                       bool skip_native_traps);
IA64GenResult ia64_gen_complete_branch(
    DisasContext *ctx, TCGLabel *skip,
    const IA64BranchCompletion *completion);

IA64GenResult ia64_gen_branch(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression);
IA64GenResult ia64_gen_integer(DisasContext *ctx,
                               const Ia64Instruction *insn);
IA64GenResult ia64_gen_system(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression);
IA64GenResult ia64_gen_memory(DisasContext *ctx,
                              const Ia64Instruction *insn,
                              TCGLabel *skip, bool record_iipa,
                              bool track_psr_suppression);
IA64GenResult ia64_gen_fp(DisasContext *ctx,
                          const Ia64Instruction *insn);
IA64GenResult ia64_gen_simd(DisasContext *ctx,
                            const Ia64Instruction *insn);
void ia64_translate_code(CPUState *cs, TranslationBlock *tb,
                         int *max_insns, vaddr pc, void *host_pc);
void ia64_translate_init(void);

#endif /* TARGET_IA64_TRANSLATE_TRANSLATE_H */
