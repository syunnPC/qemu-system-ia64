/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IA-64 specific CPU ABI and functions for linux-user
 */

#ifndef IA64_TARGET_CPU_H
#define IA64_TARGET_CPU_H

/*
 * The region windows of target/ia64/user-vaddr.h fill the whole guest
 * address space, which must be reserved so that no host mapping lands in it.
 */
#define MAX_RESERVED_VA(CPU)        ((1ul << IA64_USER_SPACE_BITS) - 1)
#define TARGET_DEFAULT_RESERVED_VA  1

/* ELF segments and faults convert between the two address forms. */
#define target_vaddr_to_guest(va)       ia64_user_fold(va)
#define target_guest_to_vaddr(addr)     ia64_user_unfold(addr)

static inline void cpu_clone_regs_child(CPUIA64State *env, target_ulong newsp,
                                        unsigned flags)
{
    ia64_user_clone_child(env, newsp);
}

static inline void cpu_clone_regs_parent(CPUIA64State *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUIA64State *env, target_ulong newtls)
{
    ia64_user_set_gr(env, IA64_GR_THREAD_POINTER, newtls);
}

static inline abi_ulong get_sp_from_cpustate(CPUIA64State *state)
{
    return state->gr[IA64_GR_STACK_POINTER];
}

#endif
