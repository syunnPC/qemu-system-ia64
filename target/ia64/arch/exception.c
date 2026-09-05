/*
 * IA-64 exception and interruption delivery.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "arch/arch.h"
#include "ia32/ia32.h"
#include "trace.h"

const uint16_t ia64_ivt_vectors[IA64_EXCP_MAX] = {
    [IA64_EXCP_NONE]             = 0,
    [IA64_EXCP_BREAK]            = 0x2c00,
    [IA64_EXCP_ILLEGAL]          = 0x5400,
    [IA64_EXCP_RESERVED_TEMPLATE] = 0x5400,
    [IA64_EXCP_VHPT_FAULT]       = 0x0000,
    [IA64_EXCP_ITLB_FAULT]       = 0x0400,
    [IA64_EXCP_DTLB_FAULT]       = 0x0800,
    [IA64_EXCP_ALT_ITLB]         = 0x0c00,
    [IA64_EXCP_ALT_DTLB]         = 0x1000,
    [IA64_EXCP_DATA_NESTED_TLB]  = 0x1400,
    [IA64_EXCP_DATA_ACCESS]      = 0x5300,
    [IA64_EXCP_GENERAL]          = 0x5400,
    [IA64_EXCP_NAT_CONSUMPTION]  = 0x5600,
    [IA64_EXCP_EXTINT]           = 0x3000,
    [IA64_EXCP_UNALIGNED]        = 0x5a00,
    [IA64_EXCP_PAGE_NOT_PRESENT] = 0x5000,
    [IA64_EXCP_INST_ACCESS]      = 0x5200,
    [IA64_EXCP_DATA_DIRTY]       = 0x2000,
    [IA64_EXCP_INST_ACCESS_BIT]  = 0x2400,
    [IA64_EXCP_DATA_ACCESS_BIT]  = 0x2800,
    [IA64_EXCP_INST_KEY_MISS]    = 0x1800,
    [IA64_EXCP_DATA_KEY_MISS]    = 0x1c00,
    [IA64_EXCP_KEY_PERMISSION]   = 0x5100,
    [IA64_EXCP_UNIMPL_DATA_ADDR] = 0x5400,
    [IA64_EXCP_UNIMPL_INST_ADDR] = 0x5e00,
    [IA64_EXCP_PRIVILEGED_OP]    = 0x5400,
    [IA64_EXCP_PRIVILEGED_REG]   = 0x5400,
    [IA64_EXCP_RESERVED_REG_FIELD] = 0x5400,
    [IA64_EXCP_FP_FAULT]         = 0x5c00,
    [IA64_EXCP_FP_TRAP]          = 0x5d00,
    [IA64_EXCP_DISABLED_ISA_TRANSITION] = 0x5400,
    [IA64_EXCP_DISABLED_FP]      = 0x5500,
    [IA64_EXCP_UNSUPPORTED_DATA_REFERENCE] = 0x5b00,
    /*
     * The virtualization extensions post-date the reference SDM revision,
     * which leaves 0x6100 through 0x6800 reserved.  0x6100 is the first
     * reserved slot after the single-step trap and is where the Virtualization
     * fault vector was subsequently defined.
     */
    [IA64_EXCP_VIRTUALIZATION]   = 0x6100,
    [IA64_EXCP_IA32_EXCEPTION]  = 0x6900,
    [IA64_EXCP_IA32_INTERCEPT]  = 0x6a00,
    [IA64_EXCP_IA32_INTERRUPT]  = 0x6b00,
    [IA64_EXCP_TAKEN_BRANCH]    = 0x5f00,
    [IA64_EXCP_SINGLE_STEP]     = 0x6000,
    [IA64_EXCP_DEBUG]           = 0x5900,
    [IA64_EXCP_LOWER_PRIVILEGE] = 0x5e00,
};



G_NORETURN void ia64_raise_exception(CPUIA64State *env, uint32_t exception,
                            uint64_t fault_ip, uint64_t fault_imm,
                            uint32_t fault_slot)
{
    CPUState *cs = env_cpu(env);

    trace_ia64_exception_raise(cs->cpu_index, exception, fault_ip, fault_imm,
                               fault_slot, env->cr_isr);
    if (exception == IA64_EXCP_RESERVED_REG_FIELD) {
        qemu_log_mask(CPU_LOG_INT,
                      "ia64 reserved-field exception ip=%016" PRIx64
                      " imm=%016" PRIx64 " slot=%u isr=%016" PRIx64
                      " cfm=%016" PRIx64 "\n",
                      fault_ip, fault_imm, fault_slot, env->cr_isr,
                      ia64_rse_current_cfm(env));
    }
    env->ip = fault_ip;
    env->exception_state.fault_ip = fault_ip;
    env->exception_state.fault_imm = fault_imm;
    env->exception_state.fault_slot = fault_slot;
    env->exception_state.fault_exception = exception;
    env->exception_state.exception = exception;
    cs->exception_index = exception;
    cpu_loop_exit(cs);
}

void ia64_check_native_traps(CPUIA64State *env, uint64_t target_ip,
                             uint64_t source_ip, uint32_t slots,
                             uint32_t conditions, uint64_t base_isr,
                             uint64_t trap_psr)
{
    uint32_t target_slot = slots & 3;
    uint32_t source_slot = (slots >> 2) & 3;
    uint64_t code = base_isr;
    IA64Exception trap;
    bool unimplemented = trap_psr & IA64_PSR_IT ?
                         !ia64_va_is_implemented(env, target_ip) :
                         !ia64_pa_is_implemented(env, target_ip);

    if (unimplemented) {
        code |= IA64_ISR_CODE_UI;
    }
    if ((conditions & IA64_NATIVE_TRAP_FP) != 0) {
        code |= IA64_ISR_CODE_FP;
    }
    if ((conditions & IA64_NATIVE_TRAP_LOWER) != 0 &&
        (trap_psr & IA64_PSR_LP) != 0) {
        code |= IA64_ISR_CODE_LP;
    }
    if ((conditions & IA64_NATIVE_TRAP_TAKEN) != 0 &&
        (trap_psr & IA64_PSR_TB) != 0) {
        code |= IA64_ISR_CODE_TB;
    }
    if ((trap_psr & IA64_PSR_SS) != 0) {
        code |= IA64_ISR_CODE_SS;
    }
    if (code == 0) {
        return;
    }

    /*
     * Completion traps report the just-completed instruction as IIPA.  Most
     * translated paths have already recorded it, but FP traps are raised
     * directly by the FP helper before translated retirement code runs.
     */
    if ((trap_psr & IA64_PSR_IC) != 0) {
        env->last_successful_bundle = ia64_ip_bundle_addr(source_ip);
    }

    if (unimplemented) {
        trap = IA64_EXCP_UNIMPL_INST_ADDR;
    } else if ((conditions & IA64_NATIVE_TRAP_FP) != 0) {
        trap = IA64_EXCP_FP_TRAP;
    } else if ((code & IA64_ISR_CODE_LP) != 0) {
        trap = IA64_EXCP_LOWER_PRIVILEGE;
    } else if ((code & IA64_ISR_CODE_TB) != 0) {
        trap = IA64_EXCP_TAKEN_BRANCH;
    } else {
        trap = IA64_EXCP_SINGLE_STEP;
    }

    env->psr = (env->psr & ~IA64_PSR_RI_MASK) |
               ((uint64_t)target_slot << IA64_PSR_RI_SHIFT);
    env->ip = target_ip;
    env->cr_isr = code;
    env->exception_state.psr_suppression_before_insn = 0;
    env->exception_state.native_completion_trap = true;
    ia64_raise_exception(env, trap, target_ip,
                         ia64_ip_bundle_addr(source_ip), source_slot);
}

G_NORETURN void ia64_ia32_unsupported(CPUIA64State *env)
{
    cpu_abort(env_cpu(env),
              "IA-32 instruction set execution is not implemented "
              "(IP=0x%016" PRIx64 " PSR=0x%016" PRIx64 ")\n",
              env->ip, env->psr);
}

G_NORETURN void
ia64_raise_disabled_isa_transition(CPUIA64State *env, uint64_t fault_ip,
                                   uint32_t fault_slot)
{
    env->cr_isr = 4ULL << 4;
    ia64_raise_exception(env, IA64_EXCP_DISABLED_ISA_TRANSITION,
                           fault_ip, 0, fault_slot);
}

G_NORETURN void ia64_raise_unaligned(CPUIA64State *env, uint64_t addr,
                            uint64_t isr_access, uint64_t fault_info)
{
    bool unimplemented = env->psr & IA64_PSR_DT ?
                         !ia64_va_is_implemented(env, addr) :
                         !ia64_pa_is_implemented(env, addr);

    /*
     * An unimplemented address precludes a concurrent unaligned-reference
     * condition (SDM Vol. 2, Table 5-3).  Alignment is checked in generated
     * code before a normal memory access reaches the MMU, so preserve that
     * qualification here as well.
     */
    if (unimplemented) {
        ia64_raise_unimplemented_data_address(
            env, addr, isr_access, false, false,
            ia64_current_code_tlb_ed(env));
    }

    /*
     * The same generated-code ordering would otherwise report an unaligned
     * reference ahead of a translation or PTE fault.  Architecturally the
     * access translates first and only the subsequent memory read/write
     * detects the misalignment, so every fault tlb_translate() can raise
     * outranks Unaligned Data Reference.
     */
    ia64_raise_pre_unaligned_data_fault(
        env, addr, (isr_access & IA64_ISR_W) != 0,
        (isr_access & (IA64_ISR_R | IA64_ISR_W)) ==
        (IA64_ISR_R | IA64_ISR_W),
        fault_info & ~3ULL, fault_info & 3);

    env->exception_state.fault_addr = addr;
    env->cr_isr = isr_access;
    if (ia64_current_code_tlb_ed(env)) {
        env->cr_isr |= IA64_ISR_ED;
    }
    ia64_raise_exception(env, IA64_EXCP_UNALIGNED, fault_info & ~3ULL, 0,
                           fault_info & 3);
}

G_NORETURN void ia64_raise_nat_consumption(CPUIA64State *env,
                                           uint64_t isr_access,
                                  uint64_t fault_info)
{
    if (env->psr & IA64_PSR_IC) {
        env->cr_ifa = 0;
    }
    env->cr_isr = IA64_ISR_CODE_REG_NAT | isr_access;
    ia64_raise_exception(env, IA64_EXCP_NAT_CONSUMPTION,
                           fault_info & ~3ULL, 0, fault_info & 3);
}

G_NORETURN void
ia64_raise_unimplemented_data_address(CPUIA64State *env, uint64_t va,
                                      uint64_t access, bool is_non_access,
                                      bool is_speculative, bool itlb_ed)
{
    uint64_t isr = IA64_GENEX_UNIMPL_DATA_ADDR | access;

    if (is_non_access) {
        isr |= IA64_ISR_NA;
    }
    if (is_speculative) {
        isr |= IA64_ISR_SP;
    }
    if (itlb_ed) {
        isr |= IA64_ISR_ED;
    }

    env->cr_ifa = va;
    env->cr_isr = isr;
    ia64_raise_exception(env, IA64_EXCP_UNIMPL_DATA_ADDR,
                           ia64_ip_bundle_addr(env->ip), 0,
                           (env->psr & IA64_PSR_RI_MASK) >>
                           IA64_PSR_RI_SHIFT);
}

#ifndef CONFIG_USER_ONLY
G_NORETURN void ia64_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                                    MMUAccessType access_type,
                                                    int mmu_idx,
                                                    uintptr_t retaddr)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;

    (void)mmu_idx;
    if (env->psr & IA64_PSR_IS) {
        ia64_ia32_unaligned_access(&env->ia32, addr, access_type, retaddr);
    }
    cpu_restore_state(cs, retaddr);
    env->exception_state.fault_ip = env->ip;
    env->exception_state.fault_addr = addr;
    env->exception_state.fault_imm = 0;
    env->cr_isr = access_type == MMU_DATA_STORE ? IA64_ISR_W : IA64_ISR_R;
    if (ia64_current_code_tlb_ed(env)) {
        env->cr_isr |= IA64_ISR_ED;
    }
    env->exception_state.exception = IA64_EXCP_UNALIGNED;
    cs->exception_index = IA64_EXCP_UNALIGNED;
    cpu_loop_exit(cs);
}
#endif

static bool ia64_exception_writes_ifa(IA64Exception excp)
{
    switch (excp) {
    case IA64_EXCP_VHPT_FAULT:
    case IA64_EXCP_ITLB_FAULT:
    case IA64_EXCP_DTLB_FAULT:
    case IA64_EXCP_ALT_ITLB:
    case IA64_EXCP_ALT_DTLB:
    case IA64_EXCP_DATA_ACCESS:
    case IA64_EXCP_INST_ACCESS:
    case IA64_EXCP_INST_KEY_MISS:
    case IA64_EXCP_DATA_KEY_MISS:
    case IA64_EXCP_KEY_PERMISSION:
    case IA64_EXCP_DATA_DIRTY:
    case IA64_EXCP_INST_ACCESS_BIT:
    case IA64_EXCP_DATA_ACCESS_BIT:
    case IA64_EXCP_NAT_CONSUMPTION:
    case IA64_EXCP_UNALIGNED:
    case IA64_EXCP_UNSUPPORTED_DATA_REFERENCE:
    case IA64_EXCP_PAGE_NOT_PRESENT:
    case IA64_EXCP_UNIMPL_DATA_ADDR:
    case IA64_EXCP_DEBUG:
        return true;
    default:
        return false;
    }
}

#define IA64_PSR_INTERRUPTION_PRESERVED_MASK \
    (IA64_PSR_UP | IA64_PSR_MFL | IA64_PSR_MFH | IA64_PSR_PK | \
     IA64_PSR_DT | IA64_PSR_RT | IA64_PSR_MC | IA64_PSR_IT)

static uint64_t ia64_interruption_psr(CPUIA64State *env)
{
    uint64_t psr = env->psr & IA64_PSR_INTERRUPTION_PRESERVED_MASK;

    if (env->cr_dcr & IA64_DCR_BE) {
        psr |= IA64_PSR_BE;
    }
    if (env->cr_dcr & IA64_DCR_PP) {
        psr |= IA64_PSR_PP;
    }

    return psr;
}

static void ia64_deliver_exception(CPUState *cs, IA64Exception excp,
                                   uint64_t fault_addr, uint8_t slot)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    uint64_t vector;
    uint64_t isr_status = 0;
    uint64_t ia32_fault_ip = 0;
    uint64_t ia32_next_ip = 0;
    bool psr_ic_inflight;
    bool collect;
    bool ia32;
    bool ia32_entry_trap;
    bool ia32_transition_trap;
    bool ia32_trap = false;

    if (excp >= IA64_EXCP_MAX || excp == IA64_EXCP_NONE) {
        return;
    }

    ia32 = cpu->env.psr & IA64_PSR_IS;
    ia32_transition_trap = cpu->env.exception_state.ia32_transition_trap;
    ia32_entry_trap = ia32 && ia32_transition_trap;
    if (ia32_entry_trap) {
        /*
         * br.ia/rfi committed the IA-32 register image, but their IA-64
         * trap records retain the original 64-bit target and source bundle.
         */
        ia64_ia32_abort_sse_instruction(&cpu->env);
        ia64_ia32_sync_to_ia64(&cpu->env);
    } else if (ia32 || ia32_transition_trap) {
        if (excp == IA64_EXCP_IA32_EXCEPTION ||
            excp == IA64_EXCP_IA32_INTERCEPT ||
            excp == IA64_EXCP_IA32_INTERRUPT) {
            ia32_fault_ip = (uint32_t)cpu->env.exception_state.fault_ip;
            ia32_trap = cpu->env.exception_state.ia32_trap;
            ia32_next_ip = ia32_trap ?
                (uint32_t)cpu->env.exception_state.fault_imm :
                ia32_fault_ip;
        } else if (excp == IA64_EXCP_EXTINT) {
            ia32_fault_ip = ia64_ia32_virtual_ip(&cpu->env);
            ia32_next_ip = ia32_fault_ip;
        } else {
            ia32_fault_ip = (uint32_t)cpu->env.exception_state.fault_ip;
            ia32_next_ip = ia32_fault_ip;
        }
        if (ia32) {
            ia64_ia32_abort_sse_instruction(&cpu->env);
            ia64_ia32_sync_to_ia64(&cpu->env);
        }
    }

    vector = ia64_ivt_vectors[excp];
    switch (excp) {
    case IA64_EXCP_VHPT_FAULT:
    case IA64_EXCP_ITLB_FAULT:
    case IA64_EXCP_DTLB_FAULT:
    case IA64_EXCP_ALT_ITLB:
    case IA64_EXCP_ALT_DTLB:
    case IA64_EXCP_DATA_NESTED_TLB:
    case IA64_EXCP_DATA_ACCESS:
    case IA64_EXCP_INST_ACCESS:
    case IA64_EXCP_INST_KEY_MISS:
    case IA64_EXCP_DATA_KEY_MISS:
    case IA64_EXCP_KEY_PERMISSION:
    case IA64_EXCP_DATA_DIRTY:
    case IA64_EXCP_INST_ACCESS_BIT:
    case IA64_EXCP_DATA_ACCESS_BIT:
    case IA64_EXCP_NAT_CONSUMPTION:
    case IA64_EXCP_UNALIGNED:
    case IA64_EXCP_UNSUPPORTED_DATA_REFERENCE:
    case IA64_EXCP_PAGE_NOT_PRESENT:
    case IA64_EXCP_UNIMPL_DATA_ADDR:
    case IA64_EXCP_UNIMPL_INST_ADDR:
    case IA64_EXCP_PRIVILEGED_OP:
    case IA64_EXCP_PRIVILEGED_REG:
    case IA64_EXCP_RESERVED_REG_FIELD:
    case IA64_EXCP_FP_FAULT:
    case IA64_EXCP_FP_TRAP:
    case IA64_EXCP_DISABLED_ISA_TRANSITION:
    case IA64_EXCP_DISABLED_FP:
    case IA64_EXCP_IA32_EXCEPTION:
    case IA64_EXCP_IA32_INTERCEPT:
    case IA64_EXCP_IA32_INTERRUPT:
    case IA64_EXCP_TAKEN_BRANCH:
    case IA64_EXCP_SINGLE_STEP:
    case IA64_EXCP_DEBUG:
    case IA64_EXCP_LOWER_PRIVILEGE:
        isr_status = cpu->env.cr_isr;
        break;
    case IA64_EXCP_EXTINT:
        /* ISR.ir captures RSE.CFLE at the interruption window. */
        isr_status = cpu->env.rse.rse_cfle ? IA64_ISR_IR : 0;
        break;
    default:
        break;
    }
    qemu_log_mask(CPU_LOG_INT,
                  "ia64 exception excp=%u vector=0x%04x ip=0x%016" PRIx64
                  " fault=0x%016" PRIx64 " slot=%u psr=0x%016" PRIx64
                  " ifa=0x%016" PRIx64 " isr=0x%016" PRIx64 "\n",
                  excp, ia64_ivt_vectors[excp], cpu->env.ip, fault_addr,
                  slot, cpu->env.psr, cpu->env.cr_ifa, cpu->env.cr_isr);
    psr_ic_inflight = cpu->env.exception_state.psr_ic_inflight;
    collect = cpu->env.psr & IA64_PSR_IC;
    trace_ia64_exception_deliver(cs->cpu_index, excp, vector, cpu->env.ip,
                                 fault_addr, slot, cpu->env.cr_isr, collect);

    /*
     * An interruption is an instruction serialization operation and also
     * performs data serialization (SDM Vol. 2, 3.1.4).  Complete any TLB
     * purges before the handler can make instruction or data references.
     */
    ia64_flush_suppressed_tlb(&cpu->env);
    cpu->env.exception_state.psr_suppression_before_insn = 0;
    ia64_tlb_serialize(&cpu->env, 1, 1);

    if (collect) {
        cpu->env.cr_ipsr = cpu->env.psr & ~IA64_PSR_RI_MASK;
        if (ia32_entry_trap) {
            cpu->env.cr_iip = cpu->env.exception_state.fault_ip;
            cpu->env.cr_iipa = cpu->env.exception_state.fault_imm;
        } else if (ia32 || ia32_transition_trap) {
            cpu->env.cr_iip = ia32_next_ip;
            cpu->env.cr_iipa = ia32_fault_ip;
        } else if (cpu->env.exception_state.native_completion_trap) {
            cpu->env.cr_ipsr |= cpu->env.psr & IA64_PSR_RI_MASK;
            cpu->env.cr_iip = ia64_ip_bundle_addr(cpu->env.ip);
            cpu->env.cr_iipa = cpu->env.exception_state.fault_imm;
        } else {
            cpu->env.cr_ipsr |=
                ((uint64_t)slot & 3) << IA64_PSR_RI_SHIFT;
            cpu->env.cr_iip = ia64_ip_bundle_addr(cpu->env.ip);
            cpu->env.cr_iipa = excp == IA64_EXCP_FP_TRAP ?
                               cpu->env.exception_state.fault_imm :
                               cpu->env.last_successful_bundle;
        }
        if (ia64_exception_writes_ifa(excp)) {
            cpu->env.cr_ifa = fault_addr;
        }
        /*
         * A collected interruption records the interrupted IP/PSR and clears
         * IFS.v.  The interrupted frame remains current until the handler
         * executes cover, which then copies CFM into IFS.ifm.
         */
        cpu->env.cr_ifs = 0;

        if (excp == IA64_EXCP_BREAK) {
            cpu->env.cr_iim = cpu->env.exception_state.fault_imm;
        }
    }

    if (excp != IA64_EXCP_DATA_NESTED_TLB) {
        cpu->env.cr_isr = isr_status;
        if ((!ia32 || ia32_entry_trap) && slot > 0) {
            cpu->env.cr_isr |= ((uint64_t)slot & 3) << IA64_ISR_EI_SHIFT;
        }
        if (!collect || psr_ic_inflight) {
            cpu->env.cr_isr |= IA64_ISR_NI;
        }
    }
    ia64_rse_delivery_check(&cpu->env, excp);
    ia64_firmware_debug_capture(&cpu->env, vector, collect);
    /*
     * Interruption delivery clears RSE.CFLE (SDM Vol.2 6.6): the
     * handler runs with the (possibly incomplete) interrupted frame,
     * which is completed by cover or by an rfi resuming the loads.
     */
    cpu->env.rse.rse_cfle = false;
    ia64_set_psr(&cpu->env, ia64_interruption_psr(&cpu->env));
    cpu->env.exception_state.psr_ic_inflight = false;

    cpu->env.ip = (cpu->env.cr_iva & ~0x7fffULL) | vector;
    cpu->env.instruction_group_start = true;
    if (excp == IA64_EXCP_EXTINT) {
        cs->halted = 0;
    }

    cpu->env.exception_state.exception = 0;
    cpu->env.exception_state.ia32_trap = false;
    cpu->env.exception_state.ia32_transition_trap = false;
    cpu->env.exception_state.native_completion_trap = false;
    cpu->env.exception_state.psr_before_insn = 0;
    cpu->env.interrupt.pending_extint = 0;
}

static bool ia64_exception_is_translation_fault(IA64Exception excp)
{
    switch (excp) {
    case IA64_EXCP_VHPT_FAULT:
    case IA64_EXCP_ITLB_FAULT:
    case IA64_EXCP_DTLB_FAULT:
    case IA64_EXCP_ALT_ITLB:
    case IA64_EXCP_ALT_DTLB:
    case IA64_EXCP_DATA_NESTED_TLB:
        return true;
    default:
        return false;
    }
}

static bool ia64_exception_uses_psr_ri_slot(IA64Exception excp, uint64_t isr)
{
    switch (excp) {
    case IA64_EXCP_EXTINT:
    case IA64_EXCP_ITLB_FAULT:
    case IA64_EXCP_ALT_ITLB:
    case IA64_EXCP_INST_ACCESS:
    case IA64_EXCP_INST_ACCESS_BIT:
    case IA64_EXCP_UNIMPL_INST_ADDR:
        return true;
    case IA64_EXCP_PAGE_NOT_PRESENT:
        return isr & IA64_ISR_X;
    default:
        return false;
    }
}

static bool ia64_try_defer_sal_speculative_alt_dtlb(
    CPUState *cs, IA64Exception excp, uint64_t fault_addr, uint8_t slot)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    if (excp != IA64_EXCP_ALT_DTLB ||
        !ia64_sal_boot_environment_active(&cpu->env) ||
        !(cpu->env.cr_isr & IA64_ISR_SP) ||
        (cpu->env.cr_isr & IA64_ISR_RS)) {
        return false;
    }

    /*
     * SAL 3.0 section 3.3.1 requires its boot-time Alternate Data TLB
     * handler to return a speculative miss with IPSR.ed set.  Retrying the
     * instruction then writes NaT and completes without another fault.
     */
    ia64_deliver_exception(cs, excp, fault_addr, slot);
    cpu->env.cr_ipsr |= IA64_PSR_ED;
    ia64_rfi(&cpu->env, cpu->env.ip, 0);
    return true;
}

static bool ia64_try_handle_sal_alt_tlb(CPUState *cs, IA64Exception excp,
                                        uint64_t fault_addr)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    bool is_data = excp == IA64_EXCP_ALT_DTLB;
    uint64_t pa;
    uint8_t page_shift;
    bool direct;

    if ((excp != IA64_EXCP_ALT_ITLB && !is_data) ||
        !ia64_firmware_boot_miss_mapping(cpu, fault_addr, &pa,
                                         &page_shift, &direct) ||
        !ia64_mmu_insert_firmware_tc(&cpu->env, fault_addr, pa, is_data,
                                     page_shift)) {
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 sal tc %c %s va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                  " ps=%u\n",
                  is_data ? 'd' : 'i', direct ? "direct" : "identity",
                  fault_addr, ia64_region_rid(&cpu->env, fault_addr), pa,
                  page_shift);

    /*
     * A mandatory RSE load can take the Alternate Data TLB vector after
     * br.ret has committed its target.  The SAL handler installs the TC
     * and returns with rfi; IFS.v is clear, so rfi resumes the incomplete
     * frame before the target instruction executes (SDM Vol.2 section 6.8).
     * This in-process SAL handler must perform the same resume step.
     */
    if (is_data &&
        (cpu->env.cr_isr & (IA64_ISR_RS | IA64_ISR_IR)) ==
        (IA64_ISR_RS | IA64_ISR_IR)) {
        ia64_rse_resume_incomplete_frame(&cpu->env);
    }
    return true;
}

void ia64_cpu_do_interrupt(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    int excp = cs->exception_index;
    uint64_t fault_addr;
    uint8_t slot;
    bool ia32_entry_trap;

    if (excp == IA64_EXCP_NONE) {
        return;
    }
    cpu->env.exception_state.fault_exception = excp;
    ia32_entry_trap = (cpu->env.psr & IA64_PSR_IS) &&
                      cpu->env.exception_state.ia32_transition_trap;

    if (!(cpu->env.psr & IA64_PSR_IC) &&
        !ia64_exception_is_translation_fault(excp) &&
        cpu->env.cr_iva == 0 &&
        (excp == IA64_EXCP_BREAK ||
         excp == IA64_EXCP_ILLEGAL ||
         excp == IA64_EXCP_RESERVED_TEMPLATE ||
         excp == IA64_EXCP_PRIVILEGED_OP ||
         excp == IA64_EXCP_PRIVILEGED_REG ||
         excp == IA64_EXCP_RESERVED_REG_FIELD ||
         excp == IA64_EXCP_DISABLED_ISA_TRANSITION ||
         excp == IA64_EXCP_DISABLED_FP)) {
        /*
         * Bare loader tests have no IVT.  Keep decoder/sentinel faults at
         * the faulting bundle for inspection, and use break as a monitor stop.
         * Other faults may deliberately target a handler at IVA-relative
         * vectors, even when IVA is zero.
         */
        if (excp == IA64_EXCP_BREAK) {
            cs->halted = 1;
            cs->exception_index = IA64_EXCP_NONE;
        } else {
            cpu->env.ip = cpu->env.exception_state.fault_ip;
        }
        return;
    }

    fault_addr = cpu->env.cr_ifa;
    switch (excp) {
    case IA64_EXCP_BREAK:
    case IA64_EXCP_ILLEGAL:
    case IA64_EXCP_RESERVED_TEMPLATE:
    case IA64_EXCP_PRIVILEGED_OP:
    case IA64_EXCP_PRIVILEGED_REG:
    case IA64_EXCP_RESERVED_REG_FIELD:
    case IA64_EXCP_FP_FAULT:
    case IA64_EXCP_FP_TRAP:
    case IA64_EXCP_DISABLED_ISA_TRANSITION:
    case IA64_EXCP_DISABLED_FP:
    case IA64_EXCP_IA32_EXCEPTION:
    case IA64_EXCP_IA32_INTERCEPT:
    case IA64_EXCP_IA32_INTERRUPT:
    case IA64_EXCP_TAKEN_BRANCH:
    case IA64_EXCP_SINGLE_STEP:
    case IA64_EXCP_LOWER_PRIVILEGE:
        fault_addr = cpu->env.exception_state.fault_ip;
        break;
    case IA64_EXCP_UNALIGNED:
        /* CR.IFA is only written for a collected interruption. */
        fault_addr = cpu->env.exception_state.fault_addr;
        break;
    case IA64_EXCP_DEBUG:
        /* Data and instruction debug faults both publish CR.IFA. */
        break;
    case IA64_EXCP_UNIMPL_INST_ADDR:
        if (ia32_entry_trap ||
            cpu->env.exception_state.native_completion_trap) {
            fault_addr = cpu->env.exception_state.fault_ip;
        } else {
            cpu->env.ip = cpu->env.psr & IA64_PSR_IT ?
                          ia64_va_canonicalize(&cpu->env, cpu->env.ip) :
                          ia64_pa_canonicalize(&cpu->env, cpu->env.ip);
            fault_addr = cpu->env.ip;
        }
        break;
    case IA64_EXCP_EXTINT:
        break;
    default:
        break;
    }
    slot = cpu->env.exception_state.fault_slot;
    if (ia32_entry_trap) {
        /* The excepting instruction is the IA-64 br.ia/rfi source slot. */
    } else if (cpu->env.psr & IA64_PSR_IS) {
        slot = 0;
    } else if (!cpu->env.exception_state.native_completion_trap &&
               ia64_exception_uses_psr_ri_slot(excp, cpu->env.cr_isr)) {
        slot = (cpu->env.psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT;
    }
    if (excp == IA64_EXCP_UNALIGNED) {
        trace_ia64_unaligned_fault(cs->cpu_index,
                                   cpu->env.exception_state.fault_ip,
                                   fault_addr, slot, cpu->env.cr_isr,
                                   cpu->env.psr,
                                   (cpu->env.psr & IA64_PSR_IC) != 0);
    }
    if (ia64_psr_cpl(cpu->env.psr) == 3 &&
        excp != IA64_EXCP_EXTINT) {
        uint64_t cfm =
            cpu->env.cfm_sof |
            ((uint64_t)cpu->env.cfm_sol << IA64_CFM_SOL_SHIFT) |
            ((uint64_t)cpu->env.cfm_sor << IA64_CFM_SOR_SHIFT) |
            ((uint64_t)cpu->env.cfm_rrb_gr << IA64_CFM_RRB_GR_SHIFT) |
            ((uint64_t)cpu->env.cfm_rrb_fr << IA64_CFM_RRB_FR_SHIFT) |
            ((uint64_t)cpu->env.cfm_rrb_pr << IA64_CFM_RRB_PR_SHIFT);

        /* Detailed user-mode exception context is opt-in via -d int. */
        qemu_log_mask(CPU_LOG_INT,
                      "ia64 user exception excp=%d ip=%016" PRIx64
                      " fault=%016" PRIx64 " slot=%u psr=%016" PRIx64
                      " isr=%016" PRIx64 " bsp=%016" PRIx64
                      " bspstore=%016" PRIx64 " rsc=%016" PRIx64
                      " cfm=%016" PRIx64 "\n",
                      excp, cpu->env.ip, fault_addr, slot, cpu->env.psr,
                      cpu->env.cr_isr, cpu->env.ar_bsp,
                      cpu->env.ar_bspstore, cpu->env.ar_rsc, cfm);

    }

    if (ia64_try_defer_sal_speculative_alt_dtlb(
            cs, excp, fault_addr, slot)) {
        cs->exception_index = IA64_EXCP_NONE;
        return;
    }

    if (ia64_try_handle_sal_alt_tlb(cs, excp, fault_addr)) {
        cs->exception_index = IA64_EXCP_NONE;
        return;
    }

    if (excp == IA64_EXCP_UNALIGNED &&
        ia64_try_emulate_firmware_unaligned(cs, fault_addr, slot)) {
        cs->exception_index = IA64_EXCP_NONE;
        return;
    }

    if (excp == IA64_EXCP_EXTINT) {
        if (ia64_sapic_has_pending(&cpu->env)) {
            ia64_deliver_exception(cs, excp, fault_addr, slot);
            cpu->env.interrupt.pending_extint = 0;
        }
    } else {
        ia64_deliver_exception(cs, excp, fault_addr, slot);
    }
    cs->exception_index = IA64_EXCP_NONE;
}

static bool ia64_external_interrupt_enabled(CPUIA64State *env)
{
    bool nmi_pending = env->interrupt.sapic_irr[0] & (1ULL << 2);
    bool interrupt_enabled = (env->psr & IA64_PSR_I) || nmi_pending;

    if ((env->psr & IA64_PSR_IS) && !nmi_pending) {
        uint32_t eflags = cpu_compute_eflags(&env->ia32);
        bool virtual_if = !(env->ar_cflg & (1ULL << 7)) ||
                          (eflags & IF_MASK);

        interrupt_enabled = (env->psr & IA64_PSR_I) && virtual_if &&
                            !(env->ia32.hflags & HF_INHIBIT_IRQ_MASK);
    }
    return interrupt_enabled;
}

void ia64_rse_interrupt_window(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);

    if (!cpu_test_interrupt(cs, CPU_INTERRUPT_HARD) ||
        !ia64_external_interrupt_enabled(env) ||
        !ia64_sapic_has_pending(env)) {
        return;
    }

    /*
     * Mandatory RSE loops expose an interrupt window after each completed
     * backing-store reference (SDM Vol. 2 section 6.7).  Deliver here,
     * before the helper can execute the next reference.  Target-frame fills
     * keep CFLE set, so ia64_deliver_exception() records ISR.ir before
     * clearing CFLE for the handler.
     */
    cs->exception_index = IA64_EXCP_EXTINT;
    ia64_cpu_do_interrupt(cs);
    cpu_loop_exit(cs);
}

bool ia64_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    /*
     * Active mandatory loops accept interrupts at their explicit per-memory
     * reference windows.  Outside such a loop, keep an incomplete frame
     * protected until cover or rfi resumes its mandatory loads (SDM Vol.2
     * 6.8); otherwise a nested handler could spill registers that were never
     * loaded.
     */
    bool rse_frame_complete = !cpu->env.rse.rse_cfle &&
        cpu->env.rse.rse_dirty >= 0 && cpu->env.rse.rse_dirty_nat >= 0;

    if (!(interrupt_request & CPU_INTERRUPT_HARD) || !rse_frame_complete) {
        return false;
    }

    if (cpu->env.pal.pal_mca_pending &&
        ia64_ras_enter_mca(cpu)) {
        return true;
    }

    if (ia64_sapic_has_init(&cpu->env) &&
        !(cpu->env.psr & IA64_PSR_MC)) {
        if (!ia64_ras_enter_init(cpu)) {
            ia64_sapic_accept_init(&cpu->env);
            if (cpu->boot_info_valid) {
                ia64_cpu_reset_to_boot_info(cpu);
            } else {
                cpu_reset(cs);
            }
        }
        return true;
    }

    if (ia64_sapic_has_pmi(&cpu->env) &&
        (cpu->env.psr & IA64_PSR_IC) &&
        cpu->env.pal.pal_pmi_entry != 0) {
        int vector = ia64_sapic_accept_pmi(&cpu->env);

        if (vector >= 0 && ia64_ras_enter_pmi(&cpu->env, vector)) {
            return true;
        }
        if (vector >= 0) {
            cpu->env.interrupt.sapic_pmi_pending |= 1U << vector;
            ia64_sapic_update_interrupt(&cpu->env);
        }
    }

    if (ia64_external_interrupt_enabled(&cpu->env) &&
        ia64_sapic_has_pending(&cpu->env)) {
        cs->exception_index = IA64_EXCP_EXTINT;
        ia64_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}
