/* IA-32 JMPE instruction-set transition. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/helper-proto.h"
#include "arch/arch.h"
#include "ia32/ia32.h"

#define IA32_FPUS_SE (1 << 7)

void helper_ia32_jmpe_check(CPUIA64State *env)
{
    if (env->psr & IA64_PSR_DI) {
        ia64_raise_disabled_isa_transition(env, env->ip, 0);
    }

    /* IA-64 always reports a pending numeric error, independent of CR0.NE. */
    if (env->ia32.fpus & IA32_FPUS_SE) {
        helper_ia32_raise_exception(env, EXCP10_COPR);
    }
}

G_NORETURN void helper_ia32_jmpe(CPUIA64State *env, target_ulong target_eip,
                                 target_ulong next_ip)
{
    CPUState *cs = env_cpu(env);
    CPUX86State *xenv = &env->ia32;
    uint32_t source_ip = env->ip;
    uint32_t target_ip =
        (uint32_t)(xenv->segs[R_CS].base + (uint32_t)target_eip) & ~0xfU;
    uint32_t eflags = cpu_compute_eflags(xenv);
    uint32_t trap_code = (env->ia32_data_breakpoints & 0xf) << 4;

    xenv->eflags &= ~RF_MASK;
    ia64_ia32_sync_to_ia64(env);

    env->gr[IA64_IA32_GR_RETURN_POINTER] = (uint32_t)next_ip;
    ia64_gr_nat_set(env, IA64_IA32_GR_RETURN_POINTER, false);
    env->psr &= ~(IA64_PSR_IS | IA64_PSR_ID | IA64_PSR_RI_MASK);
    env->ip = target_ip;
    env->last_successful_bundle = source_ip;
    env->instruction_group_start = true;

    if (env->psr & IA64_PSR_TB) {
        trap_code |= 1 << 2;
    }
    if ((env->psr & IA64_PSR_SS) || (eflags & TF_MASK)) {
        trap_code |= 1 << 3;
    }

    if (trap_code) {
        env->cr_isr =
            ((uint64_t)EXCP01_DB << IA64_ISR_VECTOR_SHIFT) | trap_code;
        env->exception_state.fault_ip = source_ip;
        env->exception_state.fault_imm = target_ip;
        env->exception_state.fault_slot = 0;
        env->exception_state.fault_exception = IA64_EXCP_IA32_EXCEPTION;
        env->exception_state.exception = IA64_EXCP_IA32_EXCEPTION;
        env->exception_state.ia32_trap = true;
        env->exception_state.ia32_transition_trap = true;
        cs->exception_index = IA64_EXCP_IA32_EXCEPTION;
    }

    cpu_loop_exit(cs);
}
