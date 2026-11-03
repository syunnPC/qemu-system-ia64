/* IA-32 exception, software-interrupt and intercept delivery. */

#include "helper-compat.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"
#include "exec-access.h"
#include "arch/arch.h"
#include "ia32/ia32.h"
#include "target/i386/tcg/helper-tcg.h"

#define IA32_MXCSR_IE (1U << 0)
#define IA32_MXCSR_DE (1U << 1)
#define IA32_MXCSR_ZE (1U << 2)
#define IA32_MXCSR_OE (1U << 3)
#define IA32_MXCSR_UE (1U << 4)
#define IA32_MXCSR_PE (1U << 5)
#define IA32_EXCP_STREAMING_SIMD 19

G_NORETURN void helper_ia32_raise_exception(CPUIA64State *env, int vector);
void helper_ia32_system_flag(CPUIA64State *env, target_ulong old_flags,
                             uint32_t ident, target_ulong next_eip);
void helper_ia32_code_fetch_check(CPUIA64State *env);
void helper_ia32_segment_access(CPUIA64State *env, target_ulong linear,
                                uint32_t seg, uint32_t size, uint32_t access);
void helper_ia32_bound_access(CPUIA64State *env, target_ulong linear,
                              uint32_t seg, uint32_t element_size);
void helper_ia32_fxstate_access(CPUIA64State *env, target_ulong linear,
                                uint32_t seg, uint32_t access);
void helper_ia32_lock_check(CPUIA64State *env, target_ulong linear,
                            uint32_t size);
void helper_ia32_virtual_sti_check(CPUIA64State *env);
void helper_ia32_virtual_popf(CPUIA64State *env, target_ulong value);
void helper_ia32_taken_branch(CPUIA64State *env);
G_NORETURN void helper_ia32_instruction_intercept(CPUIA64State *env,
                                                  uint64_t iim, uint32_t code);
void helper_ia32_complete_instruction(CPUIA64State *env, target_ulong next_eip);
void helper_ia32_rep_iteration(CPUIA64State *env);
void helper_ia32_check_disabled_fp(CPUIA64State *env, uint32_t fp_instruction);
void helper_ia32_sse_exception_begin(CPUIA64State *env);
void helper_ia32_sse_exception_end(CPUIA64State *env);

static G_NORETURN void ia32_raise_disabled_fp(CPUIA64State *env,
                                              uint64_t disabled)
{
    env->cr_isr = disabled >> 18;
    ia64_raise_exception(env, IA64_EXCP_DISABLED_FP, env->ip, 0, 0);
}

static uint32_t ia32_trap_code(CPUIA64State *env)
{
    uint32_t eflags = cpu_compute_eflags(&env->ia32);
    uint32_t code = (env->ia32_data_breakpoints & 0xf) << 4;

    return code | (((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) ? 8 : 0);
}

static bool ia32_debug_plm_match(uint64_t control, unsigned cpl)
{
    return control & (1ULL << (56 + cpl));
}

static bool ia32_debug_address_match(uint64_t reference, uint64_t address,
                                     uint64_t mask)
{
    return (reference & mask) == (address & mask);
}

static bool ia32_instruction_breakpoint_match(CPUIA64State *env, uint32_t ip)
{
    unsigned cpl = ia64_psr_cpl(env->psr);
    unsigned pair;

    for (pair = 0; pair < 4; pair++) {
        uint64_t address = env->ibr[pair * 2];
        uint64_t control = env->ibr[pair * 2 + 1];

        if ((address >> 32) == 0 && (control & (1ULL << 63)) &&
            ia32_debug_plm_match(control, cpl) &&
            ia32_debug_address_match(ip, address, (uint32_t)control)) {
            return true;
        }
    }
    return false;
}

static void ia32_record_data_breakpoints(CPUIA64State *env, uint64_t reference,
                                         uint32_t size, uint32_t access,
                                         bool require_32bit_address)
{
    const uint64_t address_mask = UINT64_C(0x00ffffffffffffff);
    unsigned cpl;
    unsigned pair;

    if (!(env->psr & IA64_PSR_DB)) {
        return;
    }
    cpl = ia64_psr_cpl(env->psr);
    for (pair = 0; pair < 4; pair++) {
        uint64_t address = env->dbr[pair * 2];
        uint64_t control = env->dbr[pair * 2 + 1];
        bool access_match =
            ((access & IA64_IA32_SEG_ACCESS_READ) &&
             (control & (1ULL << 63))) ||
            ((access & IA64_IA32_SEG_ACCESS_WRITE) && (control & (1ULL << 62)));
        uint32_t byte;

        if ((require_32bit_address && (address >> 32) != 0) || !access_match ||
            !ia32_debug_plm_match(control, cpl)) {
            continue;
        }
        for (byte = 0; byte < size; byte++) {
            uint64_t byte_reference = require_32bit_address
                                          ? (uint32_t)(reference + byte)
                                          : reference + byte;

            if (ia32_debug_address_match(byte_reference, address,
                                         control & address_mask)) {
                env->ia32_data_breakpoints |= 1U << pair;
                break;
            }
        }
    }
}

void ia64_ia32_record_data_access(CPUIA64State *env, uint32_t address,
                                  unsigned size, unsigned access)
{
    ia32_record_data_breakpoints(env, address, size, access, true);
}

void ia64_ia32_record_io_access(CPUIA64State *env, uint64_t address,
                                unsigned size, unsigned access)
{
    ia32_record_data_breakpoints(env, address, size, access, false);
}

void ia64_ia32_probe_access(CPUIA64State *env, uint32_t address, unsigned size,
                            MMUAccessType access_type, int mmu_idx,
                            uintptr_t retaddr)
{
    while (size > 0) {
        unsigned page_left =
            TARGET_PAGE_SIZE - (address & (TARGET_PAGE_SIZE - 1));
        unsigned chunk = MIN(size, page_left);

        probe_access(env, address, chunk, access_type, mmu_idx, retaddr);
        address += chunk;
        size -= chunk;
    }
}

static G_NORETURN void
ia32_leave_for_exception(CPUX86State *xenv, IA64Exception exception,
                         uint32_t vector, uint32_t code, bool trap,
                         uint32_t fault_ip, uint32_t next_ip, uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    CPUState *cs = env_cpu(env);

    if (retaddr) {
        cpu_restore_state(cs, retaddr);
    }

    xenv->error_code = code;
    xenv->exception_is_int = trap;
    xenv->exception_next_eip = (uint32_t)(next_ip - xenv->segs[R_CS].base);
    env->cr_isr = ((uint64_t)vector << IA64_ISR_VECTOR_SHIFT) |
                  (code & IA64_ISR_CODE_MASK);
    if (exception == IA64_EXCP_IA32_EXCEPTION && vector == EXCP01_DB && !trap) {
        env->cr_isr |= IA64_ISR_X;
    }
    env->exception_state.fault_ip = fault_ip;
    env->exception_state.fault_imm = next_ip;
    env->exception_state.fault_slot = 0;
    env->exception_state.fault_exception = exception;
    env->exception_state.exception = exception;
    env->exception_state.ia32_trap = trap;
    cs->exception_index = exception;
    cpu_loop_exit(cs);
}

G_NORETURN void ia64_ia32_raise_intercept(CPUX86State *xenv, uint32_t intercept,
                                          uint32_t code, uintptr_t retaddr)
{
    uint32_t ip;

    if (retaddr) {
        CPUState *cs = env_cpu((CPUIA64State *)xenv);

        cpu_restore_state(cs, retaddr);
        retaddr = 0;
    }
    ip = ia64_ia32_virtual_ip((CPUIA64State *)xenv);
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_INTERCEPT, intercept, code,
                             false, ip, ip, retaddr);
}

G_NORETURN void ia64_ia32_gate_intercept(CPUX86State *xenv, uint16_t selector,
                                         uint32_t desc_low, uint32_t desc_high,
                                         uint32_t ident, target_ulong next_eip,
                                         uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    uint32_t old_ip;
    uint32_t next_ip;
    uint32_t eflags;
    uint32_t code =
        ((ident & 3) << 14) | ((env->ia32_data_breakpoints & 0xf) << 4);

    if (retaddr) {
        cpu_restore_state(env_cpu(env), retaddr);
    }
    old_ip = ia64_ia32_virtual_ip(env);
    next_ip = (uint32_t)(xenv->segs[R_CS].base + (uint32_t)next_eip);
    eflags = cpu_compute_eflags(xenv);
    if (env->psr & IA64_PSR_TB) {
        code |= 1 << 2;
    }
    if ((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) {
        code |= 1 << 3;
    }

    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    env->cr_ifa = selector;
    env->cr_iim = desc_low | (uint64_t)desc_high << 32;
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_INTERCEPT,
                             IA64_IA32_INTERCEPT_GATE, code, true, old_ip,
                             next_ip, 0);
}

static G_NORETURN void ia32_raise_fault(CPUX86State *xenv, int vector,
                                        int error_code, uintptr_t retaddr)
{
    uint32_t ip;

    if (vector == EXCP06_ILLOP) {
        ia64_ia32_raise_intercept(xenv, IA64_IA32_INTERCEPT_INSTRUCTION, 0,
                                  retaddr);
    }
    if (retaddr) {
        CPUState *cs = env_cpu((CPUIA64State *)xenv);

        cpu_restore_state(cs, retaddr);
        retaddr = 0;
    }
    ip = ia64_ia32_virtual_ip((CPUIA64State *)xenv);
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, vector, error_code,
                             false, ip, ip, retaddr);
}

static uint32_t ia32_sse_status_from_flags(uint16_t flags)
{
    const uint16_t invalid_detail =
        float_flag_invalid_isi | float_flag_invalid_imz |
        float_flag_invalid_idi | float_flag_invalid_zdz |
        float_flag_invalid_sqrt | float_flag_invalid_cvti |
        float_flag_invalid_snan;

    return ((flags & (float_flag_invalid | invalid_detail)) ? IA32_MXCSR_IE
                                                            : 0) |
           ((flags & float_flag_input_denormal_used) ? IA32_MXCSR_DE : 0) |
           ((flags & float_flag_divbyzero) ? IA32_MXCSR_ZE : 0) |
           ((flags & float_flag_overflow) ? IA32_MXCSR_OE : 0) |
           ((flags & float_flag_underflow) ? IA32_MXCSR_UE : 0) |
           ((flags & float_flag_inexact) ? IA32_MXCSR_PE : 0) |
           ((flags & float_flag_output_denormal_flushed)
                ? IA32_MXCSR_UE | IA32_MXCSR_PE
                : 0);
}

static void ia32_restore_sse_results(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    unsigned i;

    memcpy(xenv->fpregs, env->ia32_sse_old_fpregs,
           sizeof(env->ia32_sse_old_fpregs));
    for (i = 0; i < 8; i++) {
        xenv->xmm_regs[i].ZMM_Q(0) = env->ia32_sse_old_xmm[i][0];
        xenv->xmm_regs[i].ZMM_Q(1) = env->ia32_sse_old_xmm[i][1];
    }
    xenv->cc_dst = env->ia32_sse_old_cc_dst;
    xenv->cc_src = env->ia32_sse_old_cc_src;
    xenv->cc_src2 = env->ia32_sse_old_cc_src2;
    xenv->cc_op = env->ia32_sse_old_cc_op;
}

void ia64_ia32_abort_sse_instruction(CPUIA64State *env)
{
    if (!env->ia32_sse_instruction_active) {
        return;
    }

    ia32_restore_sse_results(env);
    set_float_exception_flags(env->ia32_sse_old_flags, &env->ia32.sse_status);
    env->ia32_sse_instruction_active = false;
}

void helper_ia32_sse_exception_begin(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    unsigned i;

    g_assert(!env->ia32_sse_instruction_active);

    /* Keep MXCSR coherent before isolating exceptions from this instruction. */
    update_mxcsr_from_sse_status(xenv);
    env->ia32_sse_old_flags = get_float_exception_flags(&xenv->sse_status);
    memcpy(env->ia32_sse_old_fpregs, xenv->fpregs,
           sizeof(env->ia32_sse_old_fpregs));
    for (i = 0; i < 8; i++) {
        env->ia32_sse_old_xmm[i][0] = xenv->xmm_regs[i].ZMM_Q(0);
        env->ia32_sse_old_xmm[i][1] = xenv->xmm_regs[i].ZMM_Q(1);
    }
    env->ia32_sse_old_cc_dst = xenv->cc_dst;
    env->ia32_sse_old_cc_src = xenv->cc_src;
    env->ia32_sse_old_cc_src2 = xenv->cc_src2;
    env->ia32_sse_old_cc_op = xenv->cc_op;

    set_float_exception_flags(0, &xenv->sse_status);
    env->ia32_sse_instruction_active = true;
}

void helper_ia32_sse_exception_end(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint16_t flags;
    uint32_t status;
    uint32_t unmasked;

    g_assert(env->ia32_sse_instruction_active);

    flags = get_float_exception_flags(&xenv->sse_status);
    status = ia32_sse_status_from_flags(flags);
    xenv->mxcsr |= status;
    set_float_exception_flags(env->ia32_sse_old_flags | flags,
                              &xenv->sse_status);
    env->ia32_sse_instruction_active = false;

    unmasked = status & ~((xenv->mxcsr >> 7) & 0x3f);
    if (!unmasked) {
        return;
    }

    /* CFLG.mmxex=0 masks every SIMD exception in the IA-64 environment. */
    if (!(xenv->cr[IA32_CR4_INDEX] & CR4_OSXMMEXCPT_MASK)) {
        return;
    }

    /* An unmasked exception is a fault: no architectural result is stored. */
    ia32_restore_sse_results(env);
    ia32_raise_fault(xenv, IA32_EXCP_STREAMING_SIMD, 0, GETPC());
}

static void ia32_check_block_alignment(CPUX86State *xenv, vaddr addr,
                                       unsigned size, bool allow_eflags_ac,
                                       uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    bool psr_ac = env->psr & IA64_PSR_AC;
    unsigned alignment;

    if (!psr_ac) {
        uint32_t eflags;

        if (!allow_eflags_ac) {
            return;
        }
        eflags = cpu_compute_eflags(xenv);
        if (!(eflags & AC_MASK) || !(xenv->cr[IA32_CR0_INDEX] & CR0_AM_MASK) ||
            (xenv->hflags & HF_CPL_MASK) != 3) {
            return;
        }
    }

    switch (size) {
    case 2:
    case 4:
    case 8:
        alignment = size;
        break;
    case 10:
        alignment = psr_ac ? 16 : 8;
        break;
    case 14:
    case 94:
        alignment = 2;
        break;
    case 28:
    case 108:
        alignment = 4;
        break;
    default:
        return;
    }

    if (addr & (alignment - 1)) {
        env->cr_ifa = addr;
        ia32_raise_fault(xenv, EXCP11_ALGN, 0, retaddr);
    }
}

void ia64_ia32_check_block_alignment(CPUX86State *xenv, vaddr addr,
                                     unsigned size, uintptr_t retaddr)
{
    ia32_check_block_alignment(xenv, addr, size, true, retaddr);
}

void ia64_ia32_check_psr_alignment(CPUX86State *xenv, vaddr addr, unsigned size,
                                   uintptr_t retaddr)
{
    ia32_check_block_alignment(xenv, addr, size, false, retaddr);
}

G_NORETURN void raise_exception(CPUX86State *xenv, int vector)
{
    ia32_raise_fault(xenv, vector, 0, 0);
}

G_NORETURN void raise_exception_ra(CPUX86State *xenv, int vector,
                                   uintptr_t retaddr)
{
    ia32_raise_fault(xenv, vector, 0, retaddr);
}

G_NORETURN void raise_exception_err(CPUX86State *xenv, int vector,
                                    int error_code)
{
    ia32_raise_fault(xenv, vector, error_code, 0);
}

G_NORETURN void raise_exception_err_ra(CPUX86State *xenv, int vector,
                                       int error_code, uintptr_t retaddr)
{
    ia32_raise_fault(xenv, vector, error_code, retaddr);
}

/* Used by INTO; INT n is generated through helper_raise_interrupt below. */
G_NORETURN void raise_interrupt(CPUX86State *xenv, int vector,
                                int next_eip_addend)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    uint32_t fault_ip = ia64_ia32_virtual_ip(env);
    uint32_t next_ip = fault_ip + next_eip_addend;
    uint32_t code = ia32_trap_code(env);

    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, vector, code, true,
                             fault_ip, next_ip, 0);
}

G_NORETURN void helper_raise_interrupt(CPUX86State *xenv, int vector,
                                       int next_eip_addend)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    /*
     * INT n passes its immediate sign-extended (the decoder's "I,b"), so
     * INT 80h-FFh arrive negative with bit 8 set: only exactly the INT3
     * marker selects the IA-32 exception vector; every INT n is an
     * IA-32 interrupt.
     */
    bool int3 = (vector & ~0xff) == IA64_IA32_INT_BREAKPOINT;
    uint32_t fault_ip = ia64_ia32_virtual_ip(env);
    uint32_t next_ip = fault_ip + next_eip_addend;
    uint32_t code = ia32_trap_code(env);

    vector &= 0xff;
    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    ia32_leave_for_exception(
        xenv, int3 ? IA64_EXCP_IA32_EXCEPTION : IA64_EXCP_IA32_INTERRUPT,
        vector, code, true, fault_ip, next_ip, 0);
}

G_NORETURN void helper_ia32_raise_exception(CPUIA64State *env, int vector)
{
    uint64_t disabled = env->psr & IA64_PSR_DFH;

    if (vector == EXCP07_PREX) {
        disabled |= env->psr & IA64_PSR_DFL;
    }
    if (disabled) {
        ia32_raise_disabled_fp(env, disabled);
    }
    ia32_raise_fault(&env->ia32, vector, 0, 0);
}

void helper_ia32_system_flag(CPUIA64State *env, target_ulong old_flags,
                             uint32_t ident, target_ulong next_eip)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t old = old_flags;
    uint32_t current = cpu_compute_eflags(xenv);
    uint32_t changed = old ^ current;
    uint32_t source_ip = env->ip;
    uint32_t next_ip = (uint32_t)(xenv->segs[R_CS].base + (uint32_t)next_eip);
    uint32_t code;
    bool intercept;

    switch (ident) {
    case 0: /* CLI */
    case 1: /* STI */
        intercept = (env->ar_cflg & (1ULL << 8)) && (changed & IF_MASK);
        break;
    case 2: /* POPF/POPFD */
        intercept = (changed & (TF_MASK | AC_MASK)) ||
                    ((env->ar_cflg & (1ULL << 8)) && (changed & IF_MASK));
        break;
    case 3: /* MOV SS / POP SS */
        intercept = true;
        break;
    default:
        g_assert_not_reached();
    }

    if (!intercept) {
        return;
    }

    code = (ident << 14) | ((env->ia32_data_breakpoints & 0xf) << 4);
    if ((env->psr & IA64_PSR_SS) || (old & TF_MASK)) {
        code |= 1 << 3;
    }

    /* This is a post-instruction trap, so one-instruction suppression ends. */
    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    env->cr_iim = old;
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_INTERCEPT,
                             IA64_IA32_INTERCEPT_SYSTEM_FLAG, code, true,
                             source_ip, next_ip, 0);
}

bool ia64_ia32_code_fetch_valid(CPUX86State *xenv, uint32_t linear,
                                unsigned size)
{
    SegmentCache *cs = &xenv->segs[R_CS];
    uint32_t eflags = cpu_compute_eflags(xenv);
    uint32_t eip = linear - (uint32_t)cs->base;
    uint32_t last = eip + size - 1;
    unsigned type = (cs->flags >> DESC_TYPE_SHIFT) & 0xf;
    unsigned cpl = xenv->hflags & HF_CPL_MASK;
    bool virtual_interrupts =
        (xenv->cr[IA32_CR4_INDEX] & CR4_PVI_MASK) ||
        ((eflags & VM_MASK) && (xenv->cr[IA32_CR4_INDEX] & CR4_VME_MASK));

    if (!(cs->flags & DESC_S_MASK) || !(cs->flags & DESC_P_MASK) ||
        !(cs->flags & DESC_A_MASK) || (type & 0xc) == 4 || eip > cs->limit ||
        (last >= eip ? last > cs->limit : cs->limit != UINT32_MAX) ||
        (!(xenv->cr[IA32_CR0_INDEX] & CR0_PE_MASK) &&
         (cpl != 0 || (eflags & VM_MASK))) ||
        ((eflags & VM_MASK) &&
         (cpl != 3 || ((cs->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT) != 3 ||
          (cs->flags & DESC_B_MASK))) ||
        ((eflags & (VIP_MASK | VIF_MASK)) == (VIP_MASK | VIF_MASK) &&
         (xenv->cr[IA32_CR0_INDEX] & CR0_PE_MASK) && cpl == 3 &&
         virtual_interrupts)) {
        return false;
    }
    return true;
}

bool ia64_ia32_code_fetch_fault_probes_second_page(CPUX86State *xenv,
                                                   uint32_t insn,
                                                   uint32_t linear,
                                                   unsigned size)
{
    SegmentCache *cs = &xenv->segs[R_CS];
    uint32_t last = linear + size - 1;
    uint32_t boundary = (linear & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    uint32_t insn_eip = insn - (uint32_t)cs->base;
    uint32_t boundary_eip = boundary - (uint32_t)cs->base;
    unsigned first_page_size = boundary - linear;

    /*
     * Vol. 2, 5.6.1: for an instruction spanning a page, a second-page
     * instruction TLB fault precedes a later CS-limit fault only when the
     * instruction starts within the segment and the limit extends beyond
     * the page boundary.  Other code-fetch checks retain their priority.
     */
    return size && ((linear ^ last) & TARGET_PAGE_MASK) &&
           boundary_eip >= insn_eip && cs->limit > boundary_eip &&
           ia64_ia32_code_fetch_valid(xenv, insn, 1) &&
           ia64_ia32_code_fetch_valid(xenv, linear, first_page_size);
}

void ia64_ia32_check_fetch_fault_priority(CPUIA64State *env, uint32_t linear,
                                          uintptr_t retaddr)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t eflags = cpu_compute_eflags(xenv);

    if ((env->psr & IA64_PSR_DB) && !(env->psr & IA64_PSR_ID) &&
        !(eflags & RF_MASK) && ia32_instruction_breakpoint_match(env, linear)) {
        ia32_raise_fault(xenv, EXCP01_DB, 0, retaddr);
    }

    /* Vol. 1, Table 6-4: checks applied to every IA-32 code fetch. */
    if (!ia64_ia32_code_fetch_valid(xenv, linear, 1)) {
        ia32_raise_fault(xenv, EXCP0D_GPF, 0, retaddr);
    }
}

void helper_ia32_code_fetch_check(CPUIA64State *env)
{
    env->ia32_data_breakpoints = 0;
    ia64_ia32_check_fetch_fault_priority(env, env->ip, GETPC());
}

void ia64_ia32_check_segment_access(CPUX86State *xenv, uint32_t linear,
                                    unsigned seg, unsigned size,
                                    unsigned access, uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    SegmentCache *cache;
    uint32_t eflags = cpu_compute_eflags(xenv);
    uint32_t offset;
    uint32_t last;
    uint32_t upper;
    unsigned cpl = xenv->hflags & HF_CPL_MASK;
    unsigned dpl;
    bool vm86 = eflags & VM_MASK;
    bool protected_mode = (xenv->cr[IA32_CR0_INDEX] & CR0_PE_MASK) && !vm86;
    bool code;
    bool expand_down;
    bool readable;
    bool writable;
    bool invalid;
    MMUAccessType mmu_access;
    int mmu_idx;

    if (size == 0) {
        return;
    }
    ia64_ia32_record_data_access(env, linear, size, access);
    if (seg > R_GS) {
        return;
    }

    cache = &xenv->segs[seg];
    offset = (uint32_t)linear - (uint32_t)cache->base;
    last = offset + size - 1;
    dpl = (cache->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT;
    code = cache->flags & DESC_CS_MASK;
    expand_down = !code && (cache->flags & DESC_E_MASK);
    readable = !code || (cache->flags & DESC_R_MASK);
    writable = !code && (cache->flags & DESC_W_MASK);

    invalid = !(cache->flags & DESC_S_MASK) || !(cache->flags & DESC_P_MASK) ||
              !(cache->flags & DESC_A_MASK);

    if (expand_down) {
        upper = (cache->flags & DESC_B_MASK) ? UINT32_MAX : UINT16_MAX;
        invalid |= offset <= cache->limit || offset > upper || last < offset ||
                   last > upper;
    } else {
        invalid |=
            offset > cache->limit ||
            (last >= offset ? last > cache->limit : cache->limit != UINT32_MAX);
    }

    if (seg == R_CS) {
        invalid |= expand_down;
        if (vm86) {
            invalid |= cache->flags & DESC_B_MASK;
        }
        if (protected_mode) {
            invalid |= ((access & IA64_IA32_SEG_ACCESS_READ) && !readable) ||
                       ((access & IA64_IA32_SEG_ACCESS_WRITE) && !writable);
        }
    } else {
        if (seg == R_SS) {
            invalid |= dpl != cpl;
        }
        if (vm86) {
            invalid |= (cache->flags & DESC_B_MASK) || expand_down;
        }
        invalid |= ((access & IA64_IA32_SEG_ACCESS_READ) && !readable) ||
                   ((access & IA64_IA32_SEG_ACCESS_WRITE) && !writable);
    }

    if (invalid) {
        ia32_raise_fault(xenv, seg == R_SS ? EXCP0C_STACK : EXCP0D_GPF, 0,
                         retaddr);
    }

    /* Data translation/protection faults precede AlignmentCheck. */
    mmu_access =
        access & IA64_IA32_SEG_ACCESS_WRITE ? MMU_DATA_STORE : MMU_DATA_LOAD;
    mmu_idx = cpu_mmu_index(env_cpu(env), false);
    ia64_ia32_probe_access(env, linear, size, mmu_access, mmu_idx, retaddr);
    ia64_ia32_check_block_alignment(xenv, linear, size, retaddr);
}

void helper_ia32_segment_access(CPUIA64State *env, target_ulong linear,
                                uint32_t seg, uint32_t size, uint32_t access)
{
    ia64_ia32_check_segment_access(&env->ia32, linear, seg, size, access,
                                   GETPC());
}

void helper_ia32_bound_access(CPUIA64State *env, target_ulong linear,
                              uint32_t seg, uint32_t element_size)
{
    uintptr_t retaddr = GETPC();

    /* BOUND reads two independently aligned words or doublewords. */
    ia64_ia32_check_segment_access(&env->ia32, linear, seg, element_size,
                                   IA64_IA32_SEG_ACCESS_READ, retaddr);
    ia64_ia32_check_segment_access(
        &env->ia32, (uint32_t)(linear + element_size), seg, element_size,
        IA64_IA32_SEG_ACCESS_READ, retaddr);
}

void helper_ia32_fxstate_access(CPUIA64State *env, target_ulong linear,
                                uint32_t seg, uint32_t access)
{
    uintptr_t retaddr = GETPC();

    /* FXSAVE/FXRSTOR define a #GP, rather than ordinary block #AC. */
    if ((uint32_t)linear & 0xf) {
        ia32_raise_fault(&env->ia32, EXCP0D_GPF, 0, retaddr);
    }
    ia64_ia32_check_segment_access(&env->ia32, linear, seg, 512, access,
                                   retaddr);
}

void helper_ia32_lock_check(CPUIA64State *env, target_ulong linear,
                            uint32_t size)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t addr = linear;
    bool writeback;
    bool external_lock;

    if (!(env->cr_dcr & IA64_DCR_LC) || size == 0) {
        return;
    }

    /*
     * Translation, protection, key, dirty/access-bit and data debug faults
     * precede Alignment Check and Lock Intercept.  Probe the complete RMW
     * range before deciding whether an external lock is required.
     */
    writeback =
        ia64_exec_probe_writeback(env, addr, size, MMU_DATA_STORE, GETPC());
    external_lock = !writeback || (addr & 7) + size > 8;
    if (!external_lock) {
        return;
    }

    /* IA-32 Alignment Check has priority over Locked Data Reference. */
    if (size > 1 && (addr & (size - 1)) &&
        ((env->psr & IA64_PSR_AC) ||
         ((cpu_compute_eflags(xenv) & AC_MASK) &&
          (xenv->cr[IA32_CR0_INDEX] & CR0_AM_MASK) &&
          (xenv->hflags & HF_CPL_MASK) == 3))) {
        env->cr_ifa = addr;
        ia32_raise_fault(xenv, EXCP11_ALGN, 0, GETPC());
    }

    env->cr_ifa = addr;
    ia64_ia32_raise_intercept(xenv, IA64_IA32_INTERCEPT_LOCK, 0, GETPC());
}

void helper_ia32_virtual_sti_check(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;

    if (cpu_compute_eflags(xenv) & VIP_MASK) {
        ia32_raise_fault(xenv, EXCP0D_GPF, 0, GETPC());
    }
}

void helper_ia32_virtual_popf(CPUIA64State *env, target_ulong value)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t old = cpu_compute_eflags(xenv);

    if ((value & TF_MASK) || ((old & VIP_MASK) && (value & IF_MASK))) {
        ia32_raise_fault(xenv, EXCP0D_GPF, 0, GETPC());
    }

    cpu_load_eflags(xenv, value, TF_MASK | NT_MASK);
    xenv->eflags = (xenv->eflags & ~VIF_MASK) | ((value & IF_MASK) << (19 - 9));
}

void helper_ia32_taken_branch(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t eflags;
    uint32_t code;
    uint32_t source_ip;
    uint32_t target_ip;

    /* Far control transfers may have changed the IA-32 CPL. */
    ia64_ia32_sync_psr_cpl(env);
    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    eflags = cpu_compute_eflags(xenv);
    code = (env->ia32_data_breakpoints & 0xf) << 4;
    if (env->psr & IA64_PSR_TB) {
        code |= 1 << 2;
    }
    if ((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) {
        code |= 1 << 3;
    }
    if (!code) {
        return;
    }
    source_ip = env->ip;
    target_ip = ia64_ia32_virtual_ip(env);

    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, EXCP01_DB, code,
                             true, (code & (1 << 2)) ? source_ip : target_ip,
                             target_ip, 0);
}

G_NORETURN void helper_ia32_instruction_intercept(CPUIA64State *env,
                                                  uint64_t iim, uint32_t code)
{
    env->cr_iim = iim;
    ia64_ia32_raise_intercept(&env->ia32, IA64_IA32_INTERCEPT_INSTRUCTION, code,
                              0);
}

void helper_ia32_complete_instruction(CPUIA64State *env, target_ulong next_eip)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t eflags = cpu_compute_eflags(xenv);
    uint32_t code = (env->ia32_data_breakpoints & 0xf) << 4;
    uint32_t next_ip = (uint32_t)(xenv->segs[R_CS].base + (uint32_t)next_eip);

    ia64_ia32_sync_psr_cpl(env);
    xenv->eflags &= ~RF_MASK;
    env->psr &= ~IA64_PSR_ID;
    env->ia32_data_breakpoints = 0;

    if ((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) {
        code |= 1 << 3;
    }
    if (code) {
        ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, EXCP01_DB,
                                 code, true, next_ip, next_ip, 0);
    }
}

void helper_ia32_rep_iteration(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t eflags = cpu_compute_eflags(xenv);
    uint32_t code = (env->ia32_data_breakpoints & 0xf) << 4;
    uint32_t ip;

    if ((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) {
        code |= 1 << 3;
    }
    if (!code) {
        return;
    }

    ip = ia64_ia32_virtual_ip(env);
    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, EXCP01_DB, code,
                             true, ip, ip, 0);
}

void helper_ia32_check_disabled_fp(CPUIA64State *env, uint32_t fp_instruction)
{
    uint64_t disabled = env->psr & IA64_PSR_DFH;

    if (fp_instruction) {
        disabled |= env->psr & IA64_PSR_DFL;
    }
    if (disabled) {
        ia32_raise_disabled_fp(env, disabled);
    }
}

G_NORETURN void helper_icebp(CPUX86State *xenv)
{
    ia64_ia32_raise_intercept(xenv, IA64_IA32_INTERCEPT_INSTRUCTION, 0, 0);
}

G_NORETURN void helper_single_step(CPUX86State *xenv)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    uint32_t next_ip = ia64_ia32_virtual_ip(env);
    uint32_t fault_ip = env->ip;

    ia32_leave_for_exception(xenv, IA64_EXCP_IA32_EXCEPTION, EXCP01_DB,
                             8 | ((env->ia32_data_breakpoints & 0xf) << 4),
                             true, fault_ip, next_ip, 0);
}

void helper_rechecking_single_step(CPUX86State *xenv)
{
    if (cpu_compute_eflags(xenv) & TF_MASK) {
        helper_single_step(xenv);
    }
}

G_NORETURN void ia64_ia32_unaligned_access(CPUX86State *xenv, vaddr addr,
                                           MMUAccessType access_type,
                                           uintptr_t retaddr)
{
    (void)access_type;
    /* Remaining TCG alignment constraints are instruction-specific SSE #GP. */
    ia32_raise_fault(xenv, EXCP0D_GPF, 0, retaddr);
}

G_NORETURN void handle_unaligned_access(CPUX86State *xenv, vaddr addr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr)
{
    ia64_ia32_unaligned_access(xenv, addr, access_type, retaddr);
}
