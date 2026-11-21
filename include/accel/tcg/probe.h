/*
 * Probe guest virtual addresses for access permissions.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef ACCEL_TCG_PROBE_H
#define ACCEL_TCG_PROBE_H

#include "exec/mmu-access-type.h"
#include "exec/memop.h"
#include "exec/vaddr.h"

/**
 * probe_access:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @size: size of the access
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @retaddr: return address for unwinding
 *
 * Look up the guest virtual address @addr.  Raise an exception if the
 * page does not satisfy @access_type.  Raise an exception if the
 * access (@addr, @size) hits a watchpoint.  For writes, mark a clean
 * page as dirty.
 *
 * Finally, return the host address for a page that is backed by RAM,
 * or NULL if the page requires I/O.
 */
void *probe_access(CPUArchState *env, vaddr addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr);

static inline void *probe_write(CPUArchState *env, vaddr addr, int size,
                                int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
}

static inline void *probe_read(CPUArchState *env, vaddr addr, int size,
                               int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_LOAD, mmu_idx, retaddr);
}

/**
 * probe_access_flags:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @size: size of the access
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @nonfault: suppress the fault
 * @phost: return value for host address
 * @retaddr: return address for unwinding
 *
 * Similar to probe_access, loosely returning the TLB_FLAGS_MASK for
 * the page, and storing the host address for RAM in @phost.
 *
 * If @nonfault is set, do not raise an exception but return TLB_INVALID_MASK.
 * Do not handle watchpoints, but include TLB_WATCHPOINT in the returned flags.
 * Do handle clean pages, so exclude TLB_NOTDIRY from the returned flags.
 * For simplicity, all "mmio-like" flags are folded to TLB_MMIO.
 */
int probe_access_flags(CPUArchState *env, vaddr addr, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t retaddr);

#ifndef CONFIG_USER_ONLY

/**
 * probe_access_full:
 * Like probe_access_flags, except also return into @pfull.
 *
 * The CPUTLBEntryFull structure returned via @pfull is transient
 * and must be consumed or copied immediately, before any further
 * access or changes to TLB @mmu_idx.
 *
 * This function will not fault if @nonfault is set, but will
 * return TLB_INVALID_MASK if the page is not mapped, or is not
 * accessible with @access_type.
 *
 * This function will return TLB_MMIO in order to force the access
 * to be handled out-of-line if plugins wish to instrument the access.
 */
int probe_access_full(CPUArchState *env, vaddr addr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool nonfault, void **phost,
                      CPUTLBEntryFull **pfull, uintptr_t retaddr);

/**
 * probe_access_full_mmu:
 * Like probe_access_full, except:
 *
 * This function is intended to be used for page table accesses by
 * the target mmu itself.  Since such page walking happens while
 * handling another potential mmu fault, this function never raises
 * exceptions (akin to @nonfault true for probe_access_full).
 * Likewise this function does not trigger plugin instrumentation.
 */
int probe_access_full_mmu(CPUArchState *env, vaddr addr, int size,
                          MMUAccessType access_type, int mmu_idx,
                          void **phost, CPUTLBEntryFull **pfull);

/**
 * tlb_lookup_full_no_fill:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @pfull: return value for the matching full TLB entry
 * @from_victim: return whether the matching entry was promoted from the
 *               victim TLB
 * @load_host: optional host address for a plain RAM read; NULL on miss or
 *             when the memory access requires slow-path handling
 *
 * Look up the soft-TLB entry that the next memory access would reuse without
 * invoking the target TLB fill.  A clean matching victim entry is promoted
 * just as it is by the normal slow lookup.  Do not fill an entry, raise an
 * exception, process a watchpoint, or impose a memory barrier.  Return false
 * if both the direct comparator and reusable victim entries miss.  The caller
 * must be running on the current CPU's owner thread.
 *
 * The CPUTLBEntryFull structure returned via @pfull is transient and must be
 * consumed or copied immediately, before any further access or changes to TLB
 * @mmu_idx. The same lifetime applies to the optional RAM pointer. Its
 * caller must check alignment, access size, and target-specific permissions.
 */
bool tlb_lookup_full_no_fill(CPUArchState *env, vaddr addr,
                             MMUAccessType access_type, int mmu_idx,
                             CPUTLBEntryFull **pfull, bool *from_victim,
                             void **load_host);

/* Naturally aligned, single-page unsigned read through a clean RAM pointer. */
uint64_t cpu_ld_cached_ram(CPUArchState *env, void *host, MemOp op);

#endif /* !CONFIG_USER_ONLY */

/**
 * tlb_vaddr_to_host:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index to use for lookup
 *
 * Look up the specified guest virtual index in the TCG softmmu TLB.
 * If we can translate a host virtual address suitable for direct RAM
 * access, without causing a guest exception, then return it.
 * Otherwise (TLB entry is for an I/O access, guest software
 * TLB fill required, etc) return NULL.
 */
void *tlb_vaddr_to_host(CPUArchState *env, vaddr addr,
                        MMUAccessType access_type, int mmu_idx);

#endif
