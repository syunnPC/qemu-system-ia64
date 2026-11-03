/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IA-64 Linux user-mode signal frames.
 *
 * The frame and sigcontext layouts are the IA-64 Linux ABI
 * (arch/ia64/kernel/sigframe.h, include/uapi/asm/sigcontext.h); delivery and
 * return follow setup_frame() and ia64_rt_sigreturn() in
 * arch/ia64/kernel/signal.c of Linux 6.6.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "signal-common.h"
#include "linux-user/trace.h"

/*
 * The software conventions' scratch registers that the kernel restores:
 * b6-b7, f6-f15, and every static general register but the preserved
 * r4-r7.
 */
#define IA64_BR_SCRATCH_FIRST   6
#define IA64_FR_SCRATCH_FIRST   6
#define IA64_FR_SCRATCH_LAST    15
#define IA64_GR_PRESERVED_FIRST 4
#define IA64_GR_PRESERVED_LAST  7

/* sc_flags */
#define TARGET_IA64_SC_FLAG_ONSTACK     (1 << 0)
#define TARGET_IA64_SC_FLAG_FPH_VALID   (1 << 2)

/* include/uapi/asm/fpu.h */
struct target_ia64_fpreg {
    abi_ulong bits[2];
} QEMU_ALIGNED(16);

struct target_sigcontext {
    abi_ulong sc_flags;
    abi_ulong sc_nat;
    target_stack_t sc_stack;
    abi_ulong sc_ip;
    abi_ulong sc_cfm;
    abi_ulong sc_um;
    abi_ulong sc_ar_rsc;
    abi_ulong sc_ar_bsp;
    abi_ulong sc_ar_rnat;
    abi_ulong sc_ar_ccv;
    abi_ulong sc_ar_unat;
    abi_ulong sc_ar_fpsr;
    abi_ulong sc_ar_pfs;
    abi_ulong sc_ar_lc;
    abi_ulong sc_pr;
    abi_ulong sc_br[8];
    /* sc_gr[0] is the uc_link of ucontext_t */
    abi_ulong sc_gr[32];
    struct target_ia64_fpreg sc_fr[128];
    abi_ulong sc_rbs_base;
    abi_ulong sc_loadrs;
    abi_ulong sc_ar25;
    abi_ulong sc_ar26;
    abi_ulong sc_rsvd[12];
    target_sigset_t sc_mask;
};

/* The first three words are what user-level unwinders read. */
struct target_sigframe {
    abi_ulong arg0;
    abi_ulong arg1;
    abi_ulong arg2;
    abi_ulong handler;
    target_siginfo_t info;
    struct target_sigcontext sc;
};

/* Offsets in the kernel's struct sigcontext (glibc's ucontext_i.sym). */
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_ip) != 40);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_pr) != 128);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_br) != 136);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_gr) != 200);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_fr) != 464);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_rbs_base) != 2512);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_mask) != 2640);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigframe, info) != 32);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigframe, sc) != 160);

/*
 * The gate page.  libgcc's ia64_fallback_frame_state() takes a return
 * address in [0xa000000000000100, 0xa000000000030000) for the signal
 * trampoline, so the trampoline sits where Linux's __kernel_sigtramp does.
 */
#define IA64_GATE_ADDR          0xa000000000000000ULL
#define IA64_SIGTRAMP_ADDR      (IA64_GATE_ADDR + 0x100)

/*
 * The trampoline, from linux-user/ia64/sigtramp.S: load the handler's
 * function descriptor, call it with (signum, &info, &sc), then
 * rt_sigreturn.
 */
static const uint64_t ia64_sigtramp[] = {
    0x8030210018a01001ULL, 0x0004000000420030ULL,
    0x18e010180400880aULL, 0x00040000000b0000ULL,
    0x4100141806210009ULL, 0x0004000000283044ULL,
    0x0010141806210809ULL, 0x07000900c0203044ULL,
    0x0000101806011011ULL, 0x1080006800000200ULL,
    0x0000240900747803ULL, 0x0800000000000200ULL,
};

void setup_sigtramp(abi_ulong gate)
{
    uint64_t *tramp = lock_user(VERIFY_WRITE, IA64_SIGTRAMP_ADDR,
                                sizeof(ia64_sigtramp), 0);

    assert(tramp != NULL);
    for (int i = 0; i < ARRAY_SIZE(ia64_sigtramp); i++) {
        __put_user(ia64_sigtramp[i], &tramp[i]);
    }
    unlock_user(tramp, IA64_SIGTRAMP_ADDR, sizeof(ia64_sigtramp));
    default_rt_sigreturn = IA64_SIGTRAMP_ADDR;
    vdso_sigreturn_region_start = IA64_SIGTRAMP_ADDR;
    vdso_sigreturn_region_end = IA64_SIGTRAMP_ADDR + sizeof(ia64_sigtramp);
}

void ia64_setup_gate_page(void)
{
    abi_long gate = target_mmap(ia64_user_fold(IA64_GATE_ADDR),
                                TARGET_PAGE_SIZE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS |
                                MAP_FIXED_NOREPLACE, -1, 0);

    if (gate == -1) {
        error_report("qemu-ia64: mapping the gate page at 0x%016llx "
                     "failed: %s", (unsigned long long)IA64_GATE_ADDR,
                     strerror(errno));
        exit(EXIT_FAILURE);
    }
    setup_sigtramp(gate);
    target_mprotect(gate, TARGET_PAGE_SIZE, PROT_READ | PROT_EXEC);
}

static void setup_sigcontext(struct target_sigcontext *sc, CPUIA64State *env,
                             target_sigset_t *set, uint64_t ip, uint64_t cfm,
                             uint64_t um, abi_ulong sp)
{
    uint64_t nat = 0;
    uint64_t spill[2];

    __put_user(TARGET_IA64_SC_FLAG_FPH_VALID |
               (on_sig_stack(sp) ? TARGET_IA64_SC_FLAG_ONSTACK : 0),
               &sc->sc_flags);
    __put_user(ip, &sc->sc_ip);
    __put_user(cfm, &sc->sc_cfm);
    __put_user(um, &sc->sc_um);
    __put_user(env->ar_rsc, &sc->sc_ar_rsc);
    /* The trampoline's ar.bsp after the cover. */
    __put_user(env->ar_bsp, &sc->sc_ar_bsp);
    __put_user(ia64_user_read_ar(env, IA64_AR_RNAT), &sc->sc_ar_rnat);
    __put_user(env->ar_ccv, &sc->sc_ar_ccv);
    __put_user(env->ar_unat, &sc->sc_ar_unat);
    __put_user(env->ar_fpsr, &sc->sc_ar_fpsr);
    __put_user(env->ar_pfs, &sc->sc_ar_pfs);
    __put_user(env->ar_lc, &sc->sc_ar_lc);
    __put_user(env->ar_csd, &sc->sc_ar25);
    __put_user(env->ar_ssd, &sc->sc_ar26);
    /* After the cover: the rotating registers in their physical order. */
    __put_user(ia64_user_read_pr(env), &sc->sc_pr);
    for (int i = 0; i < IA64_BR_COUNT; i++) {
        __put_user(env->br[i], &sc->sc_br[i]);
    }
    __put_user(0, &sc->sc_gr[0]);
    for (int i = 1; i < IA64_STACKED_GR_BASE; i++) {
        __put_user(env->gr[i], &sc->sc_gr[i]);
        if (ia64_user_gr_nat(env, i)) {
            nat |= 1ULL << i;
        }
    }
    __put_user(nat, &sc->sc_nat);
    for (int i = IA64_FR_ONE_INDEX + 1; i < IA64_FR_COUNT; i++) {
        ia64_user_get_fr(env, i, spill);
        __put_user(spill[0], &sc->sc_fr[i].bits[0]);
        __put_user(spill[1], &sc->sc_fr[i].bits[1]);
    }
    /* The handler's register stack stays on the interrupted one. */
    __put_user(0, &sc->sc_rbs_base);
    __put_user(0, &sc->sc_loadrs);
    for (int i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &sc->sc_mask.sig[i]);
    }
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUIA64State *env)
{
    struct target_sigframe *frame;
    abi_ulong sp = get_sp_from_cpustate(env);
    abi_ulong frame_addr;
    uint64_t ip = ia64_user_get_ip(env);
    uint64_t um = env->psr & IA64_PSR_UM_MASK;
    uint64_t cfm;

    frame_addr = (target_sigsp(sp, ka) - sizeof(*frame)) & -16ULL;
    trace_user_setup_rt_frame(env, frame_addr);
    if (!ia64_user_frame_complete(env) ||
        !lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        /*
         * An incomplete frame means a mandatory register-stack load
         * faulted; the handler would need the registers it never got.
         */
        force_sigsegv(sig);
        return;
    }

    /*
     * Linux enters the trampoline with the interrupted frame covered: it
     * stays in the dirty partition, and sc_cfm brings it back.
     */
    cfm = ia64_user_cover(env);

    __put_user(sig, &frame->arg0);
    __put_user(frame_addr + offsetof(struct target_sigframe, info),
               &frame->arg1);
    __put_user(frame_addr + offsetof(struct target_sigframe, sc),
               &frame->arg2);
    __put_user(ka->_sa_handler, &frame->handler);
    frame->info = *info;
    target_save_altstack(&frame->sc.sc_stack, env);
    setup_sigcontext(&frame->sc, env, set, ip, cfm, um, frame_addr);
    unlock_user_struct(frame, frame_addr, 1);

    ia64_user_set_gr(env, IA64_GR_STACK_POINTER, frame_addr - 16);
    ia64_user_write_ar(env, IA64_AR_FPSR, IA64_FPSR_DEFAULT);
    env->psr &= ~IA64_PSR_BE;
    ia64_user_set_ip(env, IA64_SIGTRAMP_ADDR);
}

long do_rt_sigreturn(CPUIA64State *env)
{
    abi_ulong frame_addr = env->gr[IA64_GR_STACK_POINTER] + 16;
    struct target_sigframe *frame;
    struct target_sigcontext *sc;
    sigset_t set;
    abi_ulong flags, nat, ip, cfm, um, value;
    abi_ulong bsp;
    uint64_t spill[2];

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    sc = &frame->sc;

    __get_user(cfm, &sc->sc_cfm);
    /* The covered frame comes back from where the handler spilled it. */
    bsp = ia64_user_uncovered_bsp(env, cfm);
    if (!access_ok(env_cpu(env), VERIFY_READ, bsp, env->ar_bsp - bsp)) {
        unlock_user_struct(frame, frame_addr, 0);
        goto badframe;
    }

    target_to_host_sigset(&set, &sc->sc_mask);
    set_sigmask(&set);

    /*
     * restore_sigcontext(): the scratch state.  The preserved registers
     * (r4-r7, b1-b5, f2-f5, f16-f31, ar.lc) are the handler's to keep.
     */
    __get_user(flags, &sc->sc_flags);
    __get_user(nat, &sc->sc_nat);
    __get_user(ip, &sc->sc_ip);
    __get_user(um, &sc->sc_um);
    __get_user(value, &sc->sc_ar_rsc);
    /* Force privilege level 3. */
    ia64_user_write_ar(env, IA64_AR_RSC, value | IA64_RSC_PL);
    __get_user(value, &sc->sc_ar_unat);
    ia64_user_write_ar(env, IA64_AR_UNAT, value);
    __get_user(value, &sc->sc_ar_fpsr);
    ia64_user_write_ar(env, IA64_AR_FPSR, value);
    __get_user(value, &sc->sc_ar_pfs);
    ia64_user_write_ar(env, IA64_AR_PFS, value);
    __get_user(value, &sc->sc_ar_ccv);
    ia64_user_write_ar(env, IA64_AR_CCV, value);
    __get_user(value, &sc->sc_ar25);
    ia64_user_write_ar(env, IA64_AR_CSD, value);
    __get_user(value, &sc->sc_ar26);
    ia64_user_write_ar(env, IA64_AR_SSD, value);
    /* Before the uncover, in the physical order they were saved in. */
    __get_user(value, &sc->sc_pr);
    ia64_user_write_pr(env, value);
    __get_user(env->br[IA64_BR_RETURN_LINK], &sc->sc_br[0]);
    for (int i = IA64_BR_SCRATCH_FIRST; i < IA64_BR_COUNT; i++) {
        __get_user(env->br[i], &sc->sc_br[i]);
    }
    for (int i = 1; i < IA64_STACKED_GR_BASE; i++) {
        if (i >= IA64_GR_PRESERVED_FIRST && i <= IA64_GR_PRESERVED_LAST) {
            continue;
        }
        __get_user(value, &sc->sc_gr[i]);
        ia64_user_set_gr(env, i, value);
        ia64_user_set_gr_nat(env, i, (nat >> i) & 1);
    }
    for (int i = IA64_FR_SCRATCH_FIRST; i <= IA64_FR_SCRATCH_LAST; i++) {
        __get_user(spill[0], &sc->sc_fr[i].bits[0]);
        __get_user(spill[1], &sc->sc_fr[i].bits[1]);
        ia64_user_set_fr(env, i, spill);
    }
    if (flags & TARGET_IA64_SC_FLAG_FPH_VALID) {
        for (int i = IA64_FR_ROTATING_BASE; i < IA64_FR_COUNT; i++) {
            __get_user(spill[0], &sc->sc_fr[i].bits[0]);
            __get_user(spill[1], &sc->sc_fr[i].bits[1]);
            ia64_user_set_fr(env, i, spill);
        }
        env->psr &= ~IA64_PSR_MFH;
    }
    target_restore_altstack(&sc->sc_stack, env);
    unlock_user_struct(frame, frame_addr, 0);

    ia64_user_uncover(env, cfm);
    env->psr = (env->psr & ~IA64_PSR_UM_MASK) | (um & IA64_PSR_UM_MASK);
    ia64_user_set_ip(env, ip);
    return -QEMU_ESIGRETURN;

 badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}
