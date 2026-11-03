/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 CPU state for Linux user-mode emulation.
 *
 * linux-user/ia64 drives the CPU model through these functions so that the
 * register stack, predicate and application-register rules stay in the
 * architecture code.  The Linux behaviour each one follows is named in its
 * comment (arch/ia64 of Linux 6.6, the last kernel with IA-64).
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "fpreg.h"
#include "ia32/ia32.h"
#include "decoder.h"
#include "exec-access.h"

/*
 * The PSR of a process: translation and interruption collection on, bank 1,
 * CPL 3, and IA-32 transitions disabled (arch/ia64/kernel/head.S and
 * start_thread() in arch/ia64/include/asm/processor.h).  Linux starts with
 * PSR.dfh set and grants f32-f127 on first use; leaving it clear has the
 * same effect for the process.
 */
#define IA64_USER_PSR (IA64_PSR_IT | IA64_PSR_DT | IA64_PSR_RT | \
                       IA64_PSR_IC | IA64_PSR_I | IA64_PSR_BN | \
                       IA64_PSR_DI | IA64_PSR_SP | IA64_PSR_CPL_MASK)

/*
 * cpu_init() in arch/ia64/kernel/setup.c sets every deferral bit but DM;
 * the kernel then defers a speculative load that misses a page with no
 * mapping.  A process has no TLB to miss on a mapped page here, so DM
 * gives the same NaT result directly.
 */
#define IA64_USER_DCR (IA64_DCR_DM | IA64_DCR_DP | IA64_DCR_DK | \
                       IA64_DCR_DX | IA64_DCR_DR | IA64_DCR_DA | \
                       IA64_DCR_DD | IA64_DCR_LC)

/* start_thread(): eager RSE mode at privilege level 3. */
#define IA64_USER_RSC 0xfULL

void ia64_user_set_gr(CPUIA64State *env, uint32_t reg, uint64_t value)
{
    env->gr[reg] = value;
    ia64_gr_nat_set(env, reg, false);
    ia64_rse_mark_gr_dirty(env, reg);
}

bool ia64_user_gr_nat(const CPUIA64State *env, uint32_t reg)
{
    return ia64_gr_nat_get(env, reg);
}

void ia64_user_set_gr_nat(CPUIA64State *env, uint32_t reg, bool nat)
{
    ia64_gr_nat_set(env, reg, nat);
}

void ia64_user_start(CPUIA64State *env, uint64_t entry, uint64_t sp,
                     uint64_t rbs)
{
    env->psr = IA64_USER_PSR;
    env->cr_dcr = IA64_USER_DCR;
    env->ar_rsc = IA64_USER_RSC;
    env->ar_fpsr = IA64_FPSR_DEFAULT;
    env->ip = entry;
    env->instruction_group_start = true;
    ia64_rse_restart_backing_store(env, rbs);
    /* start_thread() leaves a 16-byte scratch area below the stack. */
    ia64_user_set_gr(env, IA64_GR_STACK_POINTER, sp - 16);
    /*
     * The process starts as execve returns, with r8 = 0: _start passes r8
     * to __libc_start_main as the dynamic linker's destructor, which a
     * static program must not have.
     */
    ia64_user_set_gr(env, IA64_GR_RETURN0, 0);
}

void ia64_user_clone_child(CPUIA64State *env, uint64_t newsp)
{
    /*
     * copy_thread(): with a new stack the child's memory stack starts
     * 16 bytes below its top and its register stack backing store at the
     * base the clone call gave, with no dirty registers to reload.  The
     * current frame keeps its values, which glibc's __clone2 relies on.
     */
    if (newsp) {
        ia64_user_set_gr(env, IA64_GR_STACK_POINTER, newsp - 16);
        ia64_rse_restart_backing_store(env, env->user_clone_rbs);
    }
    env->user_clone_rbs = 0;
    ia64_user_set_gr(env, IA64_GR_RETURN0, 0);
    ia64_user_set_gr(env, IA64_GR_RETURN2, 0);
}

uint64_t ia64_user_syscall_arg(CPUIA64State *env, uint32_t n)
{
    /*
     * The arguments are the caller's output registers.  ia64_syscall_setup
     * in arch/ia64/kernel/ivt.S passes -1 for an argument that is a NaT.
     */
    uint32_t reg = IA64_STACKED_GR_BASE + env->cfm_sol + n;

    if (reg >= IA64_GR_COUNT || ia64_gr_nat_get(env, reg)) {
        return -1;
    }
    return env->gr[reg];
}

void ia64_user_syscall_return(CPUIA64State *env, uint64_t value, bool error)
{
    /* r8 holds the result or errno, and r10 is -1 for an error. */
    ia64_user_set_gr(env, IA64_GR_RETURN0, value);
    ia64_user_set_gr(env, IA64_GR_RETURN2, error ? -1 : 0);
}

void ia64_user_next_insn(CPUIA64State *env)
{
    uint64_t bundle = ia64_ip_bundle_addr(env->ip);
    uint32_t slot = (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT;
    uint8_t low[8];

    if (!ia64_exec_debug_read(env_cpu(env), bundle, low, sizeof(low))) {
        cpu_abort(env_cpu(env), "the bundle at 0x%016" PRIx64 " that just "
                  "ran cannot be read\n", bundle);
    }
    /* An MLX bundle's L+X instruction takes slots 1 and 2. */
    if (slot == 1 &&
        ia64_template_info(ia64_bundle_template_code(ldq_le_p(low)))->
            units[1] == IA64_UNIT_L) {
        slot = 2;
    }

    /*
     * break_fault in arch/ia64/kernel/ivt.S: the next slot, or the next
     * bundle.  The kernel returns through rfi, which starts a new
     * instruction group.
     */
    env->psr &= ~IA64_PSR_RI_MASK;
    if (slot >= 2) {
        env->ip = bundle + IA64_BUNDLE_SIZE;
    } else {
        env->ip = bundle;
        env->psr |= (uint64_t)(slot + 1) << IA64_PSR_RI_SHIFT;
    }
    env->instruction_group_start = true;
}

uint64_t ia64_user_get_ip(const CPUIA64State *env)
{
    return ia64_ip_bundle_addr(env->ip) |
           ((env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT);
}

void ia64_user_set_ip(CPUIA64State *env, uint64_t ip)
{
    env->ip = ia64_ip_bundle_addr(ip);
    env->psr = (env->psr & ~IA64_PSR_RI_MASK) |
               ((ip & 3) << IA64_PSR_RI_SHIFT);
    env->instruction_group_start = true;
}

uint64_t ia64_user_read_pr(CPUIA64State *env)
{
    return ia64_system_read_pr(env);
}

void ia64_user_write_pr(CPUIA64State *env, uint64_t value)
{
    ia64_system_write_pr(env, value, UINT64_MAX);
}

uint64_t ia64_user_read_ar(CPUIA64State *env, uint32_t ar)
{
    return ia64_system_read_ar(env, ar);
}

void ia64_user_write_ar(CPUIA64State *env, uint32_t ar, uint64_t value)
{
    ia64_system_write_ar(env, ar, value);
}

bool ia64_user_frame_complete(const CPUIA64State *env)
{
    return !env->rse.rse_cfle && env->rse.rse_dirty >= 0 &&
           env->rse.rse_dirty_nat >= 0;
}

uint64_t ia64_user_cover(CPUIA64State *env)
{
    uint64_t cfm = ia64_rse_current_cfm(env);

    ia64_rse_cover(env);
    return cfm;
}

uint64_t ia64_user_uncovered_bsp(const CPUIA64State *env, uint64_t cfm)
{
    return ia64_rse_bsp_below(env->ar_bsp, cfm & IA64_CFM_SOF_MASK);
}

void ia64_user_uncover(CPUIA64State *env, uint64_t cfm)
{
    /*
     * rt_sigreturn returns through rfi with IFS = sc_cfm: the signal
     * trampoline's frame is dropped and the covered frame comes back,
     * from the backing store where the handler spilled it.
     */
    ia64_rse_return_from_min_state(env, cfm & IA64_IFS_IFM_MASK);
}

void ia64_user_get_fr(const CPUIA64State *env, uint32_t reg,
                      uint64_t spill[2])
{
    ia64_fpreg_to_spill(env, reg, &spill[0], &spill[1]);
}

void ia64_user_set_fr(CPUIA64State *env, uint32_t reg,
                      const uint64_t spill[2])
{
    ia64_fpreg_from_spill(env, reg, spill[0], spill[1]);
}

void ia64_ia32_enter(CPUIA64State *env)
{
    /* PSR.di is set and outside the user mask, so br.ia faults first. */
    cpu_abort(env_cpu(env),
              "IA-32 execution entered in a Linux user-mode process at "
              "0x%016" PRIx64 "\n", env->ip);
}
