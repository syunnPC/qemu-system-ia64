/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IA-64 Linux user-mode CPU loop.
 *
 * The kernel behaviour follows arch/ia64 of Linux 6.6, the last kernel with
 * IA-64: break_fault and ia64_syscall_setup in kernel/ivt.S for system calls,
 * ia64_bad_break() and ia64_fault() in kernel/traps.c for the other
 * interruptions.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "loader.h"
#include "user/cpu_loop.h"
#include "signal-common.h"

/* The IA-64 si_code values of include/uapi/asm-generic/siginfo.h */
#define TARGET_ILL_BADIADDR     9
#define TARGET_ILL_BREAK        10
#define TARGET_ILL_BNDMOD       11
#define TARGET_FPE_DECOVF       9
#define TARGET_FPE_DECDIV       10
#define TARGET_FPE_DECERR       11
#define TARGET_FPE_INVASC       12
#define TARGET_FPE_INVDEC       13
#define TARGET_SEGV_PSTKOVF     4

/* ISR.code of an lfetch.fault (IA64_ISR_CODE_LFETCH in asm/kregs.h). */
#define IA64_ISR_CODE_LFETCH    4

/* The system call being made. */
typedef struct IA64SyscallArgs {
    abi_ulong nr;
    abi_ulong arg[8];
} IA64SyscallArgs;

/* Resume at the faulting instruction, as the kernel's rfi would. */
static void ia64_resume_at_fault(CPUIA64State *env)
{
    ia64_user_set_ip(env, env->exception_state.fault_ip |
                          env->exception_state.fault_slot);
}

/*
 * ia64_mmap_check() in arch/ia64/kernel/sys_ia64.c: a mapping stays inside
 * one region's mappable part.  Here that part is the region's window.
 */
static bool ia64_user_range_ok(abi_ulong addr, abi_ulong len)
{
    return addr < IA64_USER_TASK_SIZE &&
           ia64_user_range_in_window(addr, len);
}

/*
 * The error Linux gives a fixed mapping at addr: above TASK_SIZE is
 * ENOMEM (get_unmapped_area), past the region's limit EINVAL.
 */
static abi_long ia64_fixed_range_error(abi_ulong addr, abi_ulong len)
{
    if (addr >= IA64_USER_TASK_SIZE || len > IA64_USER_TASK_SIZE - addr) {
        return -TARGET_ENOMEM;
    }
    return ia64_user_range_in_window(addr, len) ? 0 : -TARGET_EINVAL;
}

static abi_long ia64_mmap(CPUIA64State *env, IA64SyscallArgs *a,
                          abi_long offset)
{
    abi_ulong addr = a->arg[0];
    abi_ulong len = a->arg[1];
    int flags = a->arg[3];
    bool fixed = flags & (TARGET_MAP_FIXED | TARGET_MAP_FIXED_NOREPLACE);
    abi_long ret;

    if (addr != 0 && !ia64_user_range_ok(addr, len)) {
        if (fixed) {
            return ia64_fixed_range_error(addr, len);
        }
        /* A hint outside the user windows leaves the choice to mmap. */
        addr = 0;
    }
    ret = do_syscall(env, TARGET_NR_mmap, ia64_user_fold(addr), len,
                     a->arg[2], flags, a->arg[4], offset, 0, 0);
    if (is_error(ret)) {
        return ret;
    }
    if (!ia64_user_range_in_window(ia64_user_unfold(ret), len)) {
        /* No IA-64 address names a mapping that straddles two windows. */
        target_munmap(ret, len);
        return -TARGET_ENOMEM;
    }
    return ia64_user_unfold(ret);
}

static abi_long ia64_mremap(CPUIA64State *env, IA64SyscallArgs *a)
{
    abi_ulong old_addr = a->arg[0];
    abi_ulong old_size = a->arg[1];
    abi_ulong new_size = a->arg[2];
    abi_ulong flags = a->arg[3];
    abi_ulong new_addr = a->arg[4];
    abi_long ret;

    if (!ia64_user_range_ok(old_addr, old_size)) {
        return -TARGET_EFAULT;
    }
    if (flags & MREMAP_FIXED) {
        if (!ia64_user_range_ok(new_addr, new_size)) {
            return ia64_fixed_range_error(new_addr, new_size);
        }
    } else if (!(flags & MREMAP_MAYMOVE) &&
               !ia64_user_range_ok(old_addr, new_size)) {
        /* Growing in place would run into the next region's window. */
        return -TARGET_ENOMEM;
    }
    ret = do_syscall(env, TARGET_NR_mremap, ia64_user_fold(old_addr),
                     old_size, new_size, flags, ia64_user_fold(new_addr),
                     0, 0, 0);
    if (is_error(ret)) {
        return ret;
    }
    if (!ia64_user_range_in_window(ia64_user_unfold(ret), new_size)) {
        target_munmap(ret, new_size);
        return -TARGET_ENOMEM;
    }
    return ia64_user_unfold(ret);
}

/*
 * Run a system call.  The guest address space folds the IA-64 regions into
 * windows (target/ia64/user-vaddr.h): a pointer that a system call hands to
 * the host folds through the CPU's untagged_addr hook, and the calls below
 * take or return the address of a mapping, which is folded here.
 */
static abi_long ia64_syscall(CPUIA64State *env, IA64SyscallArgs *a)
{
    abi_ulong *arg = a->arg;
    abi_long ret;

    switch (a->nr) {
    case TARGET_NR_mmap:
        return ia64_mmap(env, a, arg[5]);
    case TARGET_NR_mmap2:
        /* The offset counts pages of AT_PAGESZ, the target page size. */
        return ia64_mmap(env, a, (abi_long)arg[5] << TARGET_PAGE_BITS);
    case TARGET_NR_munmap:
        if (!ia64_user_range_ok(arg[0], arg[1])) {
            return -TARGET_EINVAL;
        }
        arg[0] = ia64_user_fold(arg[0]);
        break;
    case TARGET_NR_mprotect:
    case TARGET_NR_madvise:
        if (!ia64_user_range_ok(arg[0], arg[1])) {
            return -TARGET_ENOMEM;
        }
        arg[0] = ia64_user_fold(arg[0]);
        break;
    case TARGET_NR_mremap:
        return ia64_mremap(env, a);
    case TARGET_NR_brk:
        ret = do_syscall(env, a->nr, ia64_user_fold(arg[0]), 0, 0, 0, 0, 0,
                         0, 0);
        return ia64_user_unfold(ret);
    case TARGET_NR_shmat:
        if (arg[1] != 0 && !ia64_user_range_ok(arg[1], 1)) {
            return -TARGET_EINVAL;
        }
        ret = do_syscall(env, a->nr, arg[0], ia64_user_fold(arg[1]), arg[2],
                         0, 0, 0, 0, 0);
        return is_error(ret) ? ret : ia64_user_unfold(ret);
    case TARGET_NR_shmdt:
        arg[0] = ia64_user_fold(arg[0]);
        break;
    case TARGET_NR_clone2:
        /*
         * ia64_clone(flags, stack_base, stack_size, parent_tid, child_tid,
         * tls): the child's register stack grows up from the base and its
         * memory stack down from the top.
         */
        env->user_clone_rbs = arg[1];
        ret = do_syscall(env, TARGET_NR_clone, arg[0],
                         arg[1] ? arg[1] + arg[2] : 0, arg[3], arg[4],
                         arg[5], 0, 0, 0);
        env->user_clone_rbs = 0;
        return ret;
    case TARGET_NR_clone:
        /* sys_clone() calls ia64_clone() with a 16-byte stack. */
        env->user_clone_rbs = arg[1];
        ret = do_syscall(env, TARGET_NR_clone, arg[0],
                         arg[1] ? arg[1] + 16 : 0, arg[2], arg[3], arg[4],
                         0, 0, 0);
        env->user_clone_rbs = 0;
        return ret;
    default:
        break;
    }
    return do_syscall(env, a->nr, arg[0], arg[1], arg[2], arg[3], arg[4],
                      arg[5], arg[6], arg[7]);
}

static void ia64_break_syscall(CPUIA64State *env)
{
    IA64SyscallArgs a;
    uint64_t break_ip;
    abi_long ret;

    a.nr = env->gr[IA64_GR_SYSCALL_NUMBER];
    if (ia64_user_gr_nat(env, IA64_GR_SYSCALL_NUMBER)) {
        /* break_fault sends a NaT system call number to sys_ni_syscall. */
        a.nr = TARGET_NR_ni_syscall;
    }
    for (int i = 0; i < ARRAY_SIZE(a.arg); i++) {
        a.arg[i] = ia64_user_syscall_arg(env, i);
    }

    /* A signal handler or a clone child resumes after the break. */
    ia64_resume_at_fault(env);
    break_ip = ia64_user_get_ip(env);
    ia64_user_next_insn(env);

    ret = ia64_syscall(env, &a);
    if (ret == -QEMU_ERESTARTSYS) {
        ia64_user_set_ip(env, break_ip);
        return;
    }
    if (ret == -QEMU_ESIGRETURN || ret == -QEMU_ESETPC) {
        return;
    }
    if (is_error(ret)) {
        ia64_user_syscall_return(env, -ret, true);
    } else {
        ia64_user_syscall_return(env, ret, false);
    }
}

/* ia64_bad_break() in arch/ia64/kernel/traps.c */
static void ia64_break_signal(CPUIA64State *env, uint64_t imm)
{
    int sig;
    int code;

    switch (imm) {
    case 0:
        sig = TARGET_SIGILL;
        code = TARGET_ILL_ILLOPC;
        break;
    case 1:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_INTDIV;
        break;
    case 2:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_INTOVF;
        break;
    case 3:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_FLTSUB;
        break;
    case 4:
        sig = TARGET_SIGSEGV;
        code = TARGET_SEGV_MAPERR;
        break;
    case 5:
        /* The kernel sends SIGSEGV with BUS_ADRALN here. */
        sig = TARGET_SIGSEGV;
        code = TARGET_BUS_ADRALN;
        break;
    case 6:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_DECOVF;
        break;
    case 7:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_DECDIV;
        break;
    case 8:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_DECERR;
        break;
    case 9:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_INVASC;
        break;
    case 10:
        sig = TARGET_SIGFPE;
        code = TARGET_FPE_INVDEC;
        break;
    case 11:
        sig = TARGET_SIGSEGV;
        code = TARGET_SEGV_PSTKOVF;
        break;
    case 0x3f000 ... 0x3ffff:
        sig = TARGET_SIGILL;
        code = TARGET_ILL_BNDMOD;
        break;
    default:
        if (imm < 0x80000) {
            sig = TARGET_SIGILL;
            code = TARGET_ILL_BREAK;
        } else {
            sig = TARGET_SIGTRAP;
            code = TARGET_TRAP_BRKPT;
        }
        break;
    }
    force_sig_fault(sig, code, ia64_user_get_ip(env));
}

/* handle_fpu_swa() in arch/ia64/kernel/traps.c */
static void ia64_fp_exception(CPUIA64State *env, bool fault)
{
    uint64_t isr = env->cr_isr;
    int code = TARGET_FPE_FLTUNK;

    if (fault) {
        switch (ia64_user_fp_assist(env, &isr)) {
        case IA64_USER_FP_COMPLETED:
            ia64_user_next_insn(env);
            return;
        case IA64_USER_FP_TRAP:
            ia64_user_next_insn(env);
            break;
        default:
            break;
        }
        if (isr & 0x11) {
            code = TARGET_FPE_FLTINV;
        } else if (isr & 0x22) {
            /* A denormal operand gets underflow's si_code. */
            code = TARGET_FPE_FLTUND;
        } else if (isr & 0x44) {
            code = TARGET_FPE_FLTDIV;
        }
    } else {
        if (isr & 0x880) {
            code = TARGET_FPE_FLTOVF;
        } else if (isr & 0x1100) {
            code = TARGET_FPE_FLTUND;
        } else if (isr & 0x2200) {
            code = TARGET_FPE_FLTRES;
        }
    }
    force_sig_fault(TARGET_SIGFPE, code, ia64_user_get_ip(env));
}

/*
 * A translation or access fault that a helper raised for an operand
 * address; plain loads and stores fault through the host instead.
 */
static void ia64_data_fault(CPUIA64State *env, bool maperr)
{
    if ((env->cr_isr & IA64_ISR_NA) &&
        (env->cr_isr & 0xf) == IA64_ISR_CODE_LFETCH) {
        /* ia64_fault(): an lfetch.fault is cancelled with PSR.ed. */
        env->psr |= IA64_PSR_ED;
        return;
    }
    force_sig_fault(TARGET_SIGSEGV,
                    maperr ? TARGET_SEGV_MAPERR : TARGET_SEGV_ACCERR,
                    env->cr_ifa);
}

void cpu_loop(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        if (trapnr != EXCP_INTERRUPT && trapnr != EXCP_ATOMIC &&
            !(trapnr == IA64_EXCP_BREAK && env->exception_state.fault_imm ==
              TARGET_IA64_BREAK_SYSCALL)) {
            qemu_log_mask(CPU_LOG_INT, "ia64 exception %d at 0x%016" PRIx64
                          " slot %u isr=0x%016" PRIx64 " ifa=0x%016" PRIx64
                          "\n", trapnr, env->exception_state.fault_ip,
                          env->exception_state.fault_slot, env->cr_isr,
                          env->cr_ifa);
        }

        switch (trapnr) {
        case IA64_EXCP_BREAK:
            if (env->exception_state.fault_imm ==
                TARGET_IA64_BREAK_SYSCALL) {
                ia64_break_syscall(env);
            } else {
                ia64_resume_at_fault(env);
                ia64_break_signal(env, env->exception_state.fault_imm);
            }
            break;
        case IA64_EXCP_FP_FAULT:
            ia64_resume_at_fault(env);
            ia64_fp_exception(env, true);
            break;
        case IA64_EXCP_FP_TRAP:
            /* A completion trap: IP already names the next instruction. */
            ia64_fp_exception(env, false);
            break;
        case IA64_EXCP_UNALIGNED:
            ia64_resume_at_fault(env);
            if (!ia64_user_unaligned_assist(env)) {
                force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN,
                                env->exception_state.fault_addr);
            }
            break;
        case IA64_EXCP_DISABLED_FP:
            /* disabled_fph_fault(): the process owns f32-f127 from now. */
            ia64_resume_at_fault(env);
            env->psr &= ~(IA64_PSR_DFH | IA64_PSR_DFL);
            break;
        case IA64_EXCP_LOWER_PRIVILEGE:
            env->psr &= ~IA64_PSR_LP;
            break;
        case IA64_EXCP_NAT_CONSUMPTION:
            ia64_resume_at_fault(env);
            if ((env->cr_isr & 0xf0) == IA64_ISR_CODE_NAT_PAGE) {
                force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_ACCERR,
                                env->cr_ifa);
            } else {
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN,
                                ia64_user_get_ip(env));
            }
            break;
        case IA64_EXCP_UNSUPPORTED_DATA_REFERENCE:
            ia64_resume_at_fault(env);
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN,
                            ia64_user_get_ip(env));
            break;
        case IA64_EXCP_ILLEGAL:
        case IA64_EXCP_RESERVED_TEMPLATE:
        case IA64_EXCP_PRIVILEGED_OP:
        case IA64_EXCP_PRIVILEGED_REG:
        case IA64_EXCP_RESERVED_REG_FIELD:
        case IA64_EXCP_DISABLED_ISA_TRANSITION:
        case IA64_EXCP_UNIMPL_DATA_ADDR:
        case IA64_EXCP_GENERAL:
        case IA64_EXCP_VIRTUALIZATION:
            /* ia64_fault() vector 24: die_if_kernel(), then SIGILL. */
            ia64_resume_at_fault(env);
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC,
                            ia64_user_get_ip(env));
            break;
        case IA64_EXCP_UNIMPL_INST_ADDR:
            if (!env->exception_state.native_completion_trap) {
                ia64_resume_at_fault(env);
            }
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_BADIADDR,
                            ia64_user_get_ip(env));
            break;
        case IA64_EXCP_VHPT_FAULT:
        case IA64_EXCP_ITLB_FAULT:
        case IA64_EXCP_DTLB_FAULT:
        case IA64_EXCP_ALT_ITLB:
        case IA64_EXCP_ALT_DTLB:
        case IA64_EXCP_DATA_NESTED_TLB:
        case IA64_EXCP_PAGE_NOT_PRESENT:
            ia64_resume_at_fault(env);
            ia64_data_fault(env, true);
            break;
        case IA64_EXCP_DATA_ACCESS:
        case IA64_EXCP_INST_ACCESS:
        case IA64_EXCP_DATA_DIRTY:
        case IA64_EXCP_INST_ACCESS_BIT:
        case IA64_EXCP_DATA_ACCESS_BIT:
        case IA64_EXCP_INST_KEY_MISS:
        case IA64_EXCP_DATA_KEY_MISS:
        case IA64_EXCP_KEY_PERMISSION:
            ia64_resume_at_fault(env);
            ia64_data_fault(env, false);
            break;
        case IA64_EXCP_TAKEN_BRANCH:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRANCH, 0);
            break;
        case IA64_EXCP_SINGLE_STEP:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_TRACE, 0);
            break;
        case IA64_EXCP_DEBUG:
            ia64_resume_at_fault(env);
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_HWBKPT, env->cr_ifa);
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT,
                            ia64_user_get_ip(env));
            break;
        case EXCP_INTERRUPT:
            /* Signals are delivered below. */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled IA-64 exception %d, "
                      "aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);
    abi_long rbs;

    if (reserved_va != MAX_RESERVED_VA(cs)) {
        error_report("qemu-ia64 needs the whole guest address space of "
                     "0x%lx bytes for its region windows; -R gave 0x%lx",
                     MAX_RESERVED_VA(cs) + 1, reserved_va + 1);
        exit(EXIT_FAILURE);
    }

    /*
     * Linux gives the main thread a register backing store that grows up
     * from rbs_bot (arch/ia64/include/asm/processor.h); give it as much
     * room as the memory stack has.
     */
    rbs = target_mmap(0, guest_stack_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (rbs == -1) {
        error_report("qemu-ia64: mapping the %lu-byte register backing "
                     "store failed: %s", guest_stack_size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    ia64_setup_gate_page();
    ia64_user_start(env, ia64_user_unfold(info->entry),
                    ia64_user_unfold(info->start_stack),
                    ia64_user_unfold(rbs));
}
