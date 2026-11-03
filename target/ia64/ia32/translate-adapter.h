/*
 * IA-32 TCG translation adapter.
 *
 * The decoder and code generator are QEMU's existing x86 implementation.
 * Redirect its CPUX86State references to the private x86 backing state
 * embedded in CPUIA64State.  Architectural register synchronization is
 * performed at instruction-set transitions.
 */

#ifndef TARGET_IA64_IA32_TRANSLATE_ADAPTER_H
#define TARGET_IA64_IA32_TRANSLATE_ADAPTER_H

#include "qemu/osdep.h"
#include "qemu/atomic128.h"
#include "cpu.h"
#include "ia32/ia32.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#define IA32_TB_FLAG_PSR_DB (1u << 29)
#define IA32_TB_FLAG_PSR_AC (1u << 30)
#define IA32_TB_FLAG_PSR_IS (1u << 31)

/*
 * cpu_env normally returns CPUIA64State.  Decode-time reads need the
 * private x86 view instead; generated TCG accesses still use tcg_env and
 * work because that view is the first member of CPUIA64State.
 */
#define cpu_env(cpu) (&IA64_CPU(cpu)->env.ia32)
#define X86_TRANSLATOR_ENV(env) ((CPUIA64State *)(env))

#define X86_GEN_HELPER_RAISE_EXCEPTION gen_helper_ia32_raise_exception
#define X86_GEN_HELPER_RSM gen_helper_ia32_rsm
#define X86_TB_FLAGS(flags) \
    ((flags) & ~(IA32_TB_FLAG_PSR_DB | IA32_TB_FLAG_PSR_AC | \
                 IA32_TB_FLAG_PSR_IS))
/* Ordinary IA-32 #AC checks run after translation in the segment hook. */
#define X86_MEMOP_ALIGNMENT(s, memop) MO_UNALN
#define X86_GEN_INSN_START(pc) \
    tcg_gen_st_i64(tcg_constant_i64((uint32_t)(pc)), tcg_env, \
                   offsetof(CPUIA64State, ip))
#define X86_INT3_VECTOR(vector) ((vector) | 0x100)
#define X86_IA32_SYSTEM_ENV 1
/* Enter exclusive execution before instruction side effects. */
#define X86_GEN_CODE_FETCH_CHECK(s) do {                  \
    gen_helper_alat_ensure_exclusive(tcg_env);            \
    gen_helper_ia32_code_fetch_check(tcg_env);            \
} while (0)
#define X86_GEN_CPUID_SERIALIZE(s) do {                               \
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);                              \
    (s)->base.is_jmp = DISAS_EOB_NEXT;                                \
} while (0)
#define X86_GEN_X87_FOP(s, fop)                                       \
    tcg_gen_st16_i32(tcg_constant_i32(fop), tcg_env,                  \
                     offsetof(CPUX86State, fpop))
#define X86_SPLIT_NEAR_PAGE_END(s) true
#define X86_CODE_FETCH_VALID(env, pc, size) \
    ia64_ia32_code_fetch_valid((env), (uint32_t)(pc), (size))
#define X86_CODE_FETCH_FAULT_PROBES_SECOND_PAGE(env, insn, pc, size) \
    ia64_ia32_code_fetch_fault_probes_second_page(                    \
        (env), (uint32_t)(insn), (uint32_t)(pc), (size))
#define X86_GEN_SEGMENT_ACCESS_CHECK(s, addr, seg, size, access) do { \
    gen_helper_ia32_segment_access(                                  \
        tcg_env, (addr), tcg_constant_i32(seg),                      \
        tcg_constant_i32(size), tcg_constant_i32(access));           \
} while (0)
#define X86_GEN_BOUND_ACCESS_CHECK(s, addr, seg, element_size) do {   \
    gen_helper_ia32_bound_access(                                    \
        tcg_env, (addr), tcg_constant_i32(seg),                      \
        tcg_constant_i32(element_size));                              \
} while (0)
#define X86_GEN_FXSTATE_ACCESS_CHECK(s, addr, seg, access) do {       \
    gen_helper_ia32_fxstate_access(                                  \
        tcg_env, (addr), tcg_constant_i32(seg),                      \
        tcg_constant_i32(access));                                    \
} while (0)
#define X86_GEN_LOCK_INTERCEPT_CHECK(s, addr, size)                   \
    gen_helper_ia32_lock_check(tcg_env, (addr), tcg_constant_i32(size))
#define X86_GEN_TAKEN_BRANCH(s) gen_helper_ia32_taken_branch(tcg_env)
#define X86_GEN_NOT_TAKEN_BRANCH(s)                                   \
    gen_helper_ia32_complete_instruction(tcg_env, eip_next_tl(s))
#define X86_GEN_DISABLED_FP_CHECK(s, decode) do {                      \
    bool fp_instruction_ =                                            \
        (decode)->e.gen == gen_x87 ||                                 \
        (decode)->e.gen == gen_WAIT ||                                \
        (decode)->e.gen == gen_EMMS ||                                \
        (decode)->e.special == X86_SPECIAL_MMX ||                     \
        (decode)->e.cpuid == X86_FEAT_SSE ||                          \
        (decode)->e.cpuid == X86_FEAT_FXSR ||                         \
        (decode)->op[0].unit == X86_OP_MMX ||                         \
        (decode)->op[0].unit == X86_OP_SSE ||                         \
        (decode)->op[1].unit == X86_OP_MMX ||                         \
        (decode)->op[1].unit == X86_OP_SSE ||                         \
        (decode)->op[2].unit == X86_OP_MMX ||                         \
        (decode)->op[2].unit == X86_OP_SSE;                           \
    gen_helper_ia32_check_disabled_fp(                                \
        tcg_env, tcg_constant_i32(fp_instruction_));                  \
} while (0)
#define X86_GEN_PENDING_FP_CHECK(s, decode) do {                       \
    bool mmx_instruction_ =                                           \
        (decode)->e.gen == gen_EMMS ||                                \
        ((decode)->e.special == X86_SPECIAL_MMX &&                    \
         !((s)->prefix &                                              \
           (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA)));              \
    if (mmx_instruction_) {                                           \
        gen_helper_fwait(tcg_env);                                    \
    }                                                                  \
} while (0)
#define X86_IA32_SSE_INSTRUCTION(decode)                               \
    ((decode)->op[0].unit == X86_OP_SSE ||                             \
     (decode)->op[1].unit == X86_OP_SSE ||                             \
     (decode)->op[2].unit == X86_OP_SSE)
#define X86_GEN_SSE_EXCEPTION_BEGIN(s, decode) do {                    \
    if (X86_IA32_SSE_INSTRUCTION(decode)) {                            \
        gen_helper_ia32_sse_exception_begin(tcg_env);                  \
    }                                                                  \
} while (0)
#define X86_GEN_SSE_EXCEPTION_END(s, decode) do {                      \
    if (X86_IA32_SSE_INSTRUCTION(decode)) {                            \
        gen_helper_ia32_sse_exception_end(tcg_env);                    \
    }                                                                  \
} while (0)
#define X86_REP_CAN_LOOP(s)                                            \
    (!((s)->base.tb->flags & IA32_TB_FLAG_PSR_DB))
#define X86_REP_FAULT_SETS_RF(s) true
#define X86_REP_FINAL_ITERATION_COMPLETES(s) true
#define X86_GEN_REP_ITERATION(s)                                       \
    gen_helper_ia32_rep_iteration(tcg_env)
#define X86_GEN_REP_COMPLETE(s)                                        \
    gen_helper_ia32_complete_instruction(tcg_env, eip_next_tl(s))
/*
 * An indirect branch commits its runtime target to cpu_eip and invalidates
 * pc_save.  That target is the next IP for instruction-completion traps.
 */
#define X86_IA32_COMPLETION_EIP(s)                                     \
    ((s)->pc_save == -1 ? cpu_eip : eip_next_tl(s))
#define X86_AFTER_INSN_WRITEBACK(s, decode) do {                       \
    /* Helpers inspect lazy flags through CPUX86State.cc_op. */         \
    gen_update_cc_op(s);                                               \
    if (((decode)->e.gen == gen_MOV &&                                 \
         (decode)->e.op0 == X86_TYPE_S &&                              \
         (decode)->op[0].n == R_SS) ||                                 \
        ((decode)->e.gen == gen_POP &&                                 \
         (decode)->e.op0 == X86_TYPE_SS)) {                            \
        TCGv old_eflags_ = tcg_temp_new();                             \
        gen_helper_read_eflags(old_eflags_, tcg_env);                  \
        assume_cc_op(s, CC_OP_EFLAGS);                                 \
        gen_helper_ia32_system_flag(tcg_env, old_eflags_,              \
                                    tcg_constant_i32(3),               \
                                    eip_next_tl(s));                   \
    }                                                                  \
    gen_helper_ia32_complete_instruction(                              \
        tcg_env, X86_IA32_COMPLETION_EIP(s));                          \
} while (0)
#define X86_SYSTEM_INSTRUCTION_INTERCEPT(decode) \
    ((decode)->e.gen == gen_CLTS || \
     (decode)->e.gen == gen_HLT || \
     (decode)->e.gen == gen_IRET || \
     (decode)->e.gen == gen_RDMSR || \
     (decode)->e.gen == gen_RSM || \
     (decode)->e.gen == gen_SYSCALL || \
     (decode)->e.gen == gen_SYSENTER || \
     (decode)->e.gen == gen_SYSEXIT || \
     (decode)->e.gen == gen_SYSRET || \
     (decode)->e.gen == gen_SYSTEM || \
     (decode)->e.gen == gen_WRMSR || \
     ((decode)->e.gen == gen_MOV && \
      ((decode)->e.op0 == X86_TYPE_D || \
       (decode)->e.op1 == X86_TYPE_D || \
       (decode)->e.op0 == X86_TYPE_C)))

static void ia64_ia32_gen_dtlb1_touch(TCGv_i64 addr, TCGArg mmu_idx,
                                      MemOp memop)
{
    if (mmu_idx != MMU_PHYS_IDX) {
        gen_helper_merced_dtlb1_touch(tcg_env, addr,
                                      tcg_constant_i32(1 << (memop & MO_SIZE)));
    }
}

static void ia64_ia32_gen_qemu_ld_i32(TCGv_i32 value, TCGv_i64 addr,
                                      TCGArg mmu_idx, MemOp memop)
{
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_ld_i32(value, addr, mmu_idx, memop);
}

static void ia64_ia32_gen_qemu_st_i32(TCGv_i32 value, TCGv_i64 addr,
                                      TCGArg mmu_idx, MemOp memop)
{
    gen_helper_alat_ensure_exclusive(tcg_env);
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_st_i32(value, addr, mmu_idx, memop);
    gen_helper_notify_alat_store(tcg_env);
}

static void ia64_ia32_gen_qemu_ld_i64(TCGv_i64 value, TCGv_i64 addr,
                                      TCGArg mmu_idx, MemOp memop)
{
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_ld_i64(value, addr, mmu_idx, memop);
}

static void ia64_ia32_gen_qemu_st_i64(TCGv_i64 value, TCGv_i64 addr,
                                      TCGArg mmu_idx, MemOp memop)
{
    gen_helper_alat_ensure_exclusive(tcg_env);
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_st_i64(value, addr, mmu_idx, memop);
    gen_helper_notify_alat_store(tcg_env);
}

static void ia64_ia32_gen_qemu_ld_i128(TCGv_i128 value, TCGv_i64 addr,
                                       TCGArg mmu_idx, MemOp memop)
{
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_ld_i128(value, addr, mmu_idx, memop);
}

static void ia64_ia32_gen_qemu_st_i128(TCGv_i128 value, TCGv_i64 addr,
                                       TCGArg mmu_idx, MemOp memop)
{
    gen_helper_alat_ensure_exclusive(tcg_env);
    ia64_ia32_gen_dtlb1_touch(addr, mmu_idx, memop);
    tcg_gen_qemu_st_i128(value, addr, mmu_idx, memop);
    gen_helper_notify_alat_store(tcg_env);
}

#define IA64_IA32_DEFINE_ATOMIC2(name, type)                           \
    static inline void ia64_ia32_##name(                               \
        type result, TCGv_va addr, type value,                         \
        TCGArg mmu_idx, MemOp memop)                                  \
    {                                                                  \
        gen_helper_alat_ensure_exclusive(tcg_env);                     \
        name(result, addr, value, mmu_idx, memop);                     \
        gen_helper_notify_alat_store(tcg_env);                         \
    }

#define IA64_IA32_DEFINE_ATOMIC3(name, type)                           \
    static inline void ia64_ia32_##name(                               \
        type result, TCGv_va addr, type old, type value,               \
        TCGArg mmu_idx, MemOp memop)                                  \
    {                                                                  \
        gen_helper_alat_ensure_exclusive(tcg_env);                     \
        name(result, addr, old, value, mmu_idx, memop);                \
        gen_helper_notify_alat_store(tcg_env);                         \
    }

IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_add_fetch_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_and_fetch_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_fetch_add_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_fetch_and_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_fetch_or_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_fetch_xor_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_or_fetch_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_xchg_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC2(tcg_gen_atomic_xor_fetch_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC3(tcg_gen_atomic_cmpxchg_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC3(tcg_gen_atomic_cmpxchg_i128, TCGv_i128)
IA64_IA32_DEFINE_ATOMIC3(tcg_gen_nonatomic_cmpxchg_i64, TCGv_i64)
IA64_IA32_DEFINE_ATOMIC3(tcg_gen_nonatomic_cmpxchg_i128, TCGv_i128)

#undef IA64_IA32_DEFINE_ATOMIC2
#undef IA64_IA32_DEFINE_ATOMIC3

#define tcg_gen_qemu_ld_i32 ia64_ia32_gen_qemu_ld_i32
#define tcg_gen_qemu_st_i32 ia64_ia32_gen_qemu_st_i32
#define tcg_gen_qemu_ld_i64 ia64_ia32_gen_qemu_ld_i64
#define tcg_gen_qemu_st_i64 ia64_ia32_gen_qemu_st_i64
#define tcg_gen_qemu_ld_i128 ia64_ia32_gen_qemu_ld_i128
#define tcg_gen_qemu_st_i128 ia64_ia32_gen_qemu_st_i128
#define tcg_gen_atomic_add_fetch_i64 \
    ia64_ia32_tcg_gen_atomic_add_fetch_i64
#define tcg_gen_atomic_and_fetch_i64 \
    ia64_ia32_tcg_gen_atomic_and_fetch_i64
#define tcg_gen_atomic_fetch_add_i64 \
    ia64_ia32_tcg_gen_atomic_fetch_add_i64
#define tcg_gen_atomic_fetch_and_i64 \
    ia64_ia32_tcg_gen_atomic_fetch_and_i64
#define tcg_gen_atomic_fetch_or_i64 \
    ia64_ia32_tcg_gen_atomic_fetch_or_i64
#define tcg_gen_atomic_fetch_xor_i64 \
    ia64_ia32_tcg_gen_atomic_fetch_xor_i64
#define tcg_gen_atomic_or_fetch_i64 \
    ia64_ia32_tcg_gen_atomic_or_fetch_i64
#define tcg_gen_atomic_xchg_i64 ia64_ia32_tcg_gen_atomic_xchg_i64
#define tcg_gen_atomic_xor_fetch_i64 \
    ia64_ia32_tcg_gen_atomic_xor_fetch_i64
#define tcg_gen_atomic_cmpxchg_i64 \
    ia64_ia32_tcg_gen_atomic_cmpxchg_i64
#define tcg_gen_atomic_cmpxchg_i128 \
    ia64_ia32_tcg_gen_atomic_cmpxchg_i128
#define tcg_gen_nonatomic_cmpxchg_i64 \
    ia64_ia32_tcg_gen_nonatomic_cmpxchg_i64
#define tcg_gen_nonatomic_cmpxchg_i128 \
    ia64_ia32_tcg_gen_nonatomic_cmpxchg_i128

#define X86_SKIP_HELPER_INFO
#define tcg_x86_init ia64_ia32_translate_init
#define x86_translate_code ia64_ia32_translate_code

#endif /* TARGET_IA64_IA32_TRANSLATE_ADAPTER_H */
