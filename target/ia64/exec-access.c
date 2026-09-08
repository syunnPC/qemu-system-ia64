/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TCG softmmu implementation of the IA-64 execution access boundary.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/atomic128.h"
#include "qemu/log.h"
#include "cpu.h"
#include "arch/arch.h"
#include "exec-access.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/cpu-common.h"
#include "exec/tlb-flags.h"
#include "exec/translation-block.h"
#include "system/address-spaces.h"
#include "system/memory.h"

bool ia64_exec_is_parallel(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);

    return tcg_cflags_has(cs, CF_PARALLEL) &&
           !cpu_in_exclusive_context(cs);
}

void ia64_exec_ensure_alat_exclusive(CPUIA64State *env, uintptr_t ra)
{
    if (env->alat_state.alat_full && ia64_exec_is_parallel(env)) {
        ia64_exec_exit_atomic(env, ra);
    }
}

int ia64_exec_mmu_index(CPUIA64State *env, bool ifetch)
{
    return cpu_mmu_index(env_cpu(env), ifetch);
}

void ia64_exec_exit_atomic(CPUIA64State *env, uintptr_t ra)
{
    cpu_loop_exit_atomic(env_cpu(env), ra);
}

uint64_t ia64_exec_load_data(CPUIA64State *env, uint64_t addr,
                             unsigned size, bool big_endian, uintptr_t ra)
{
    switch (size) {
    case 1:
        return cpu_ldub_data_ra(env, addr, ra);
    case 2:
        return big_endian ? cpu_lduw_be_data_ra(env, addr, ra) :
                            cpu_lduw_le_data_ra(env, addr, ra);
    case 4:
        return big_endian ? cpu_ldl_be_data_ra(env, addr, ra) :
                            cpu_ldl_le_data_ra(env, addr, ra);
    case 8:
        return big_endian ? cpu_ldq_be_data_ra(env, addr, ra) :
                            cpu_ldq_le_data_ra(env, addr, ra);
    default:
        g_assert_not_reached();
    }
}

void ia64_exec_store_data(CPUIA64State *env, uint64_t addr, uint64_t value,
                          unsigned size, bool big_endian, uintptr_t ra)
{
    ia64_alat_write_begin(env);
    switch (size) {
    case 1:
        cpu_stb_data_ra(env, addr, value, ra);
        break;
    case 2:
        if (big_endian) {
            cpu_stw_be_data_ra(env, addr, value, ra);
        } else {
            cpu_stw_le_data_ra(env, addr, value, ra);
        }
        break;
    case 4:
        if (big_endian) {
            cpu_stl_be_data_ra(env, addr, value, ra);
        } else {
            cpu_stl_le_data_ra(env, addr, value, ra);
        }
        break;
    case 8:
        if (big_endian) {
            cpu_stq_be_data_ra(env, addr, value, ra);
        } else {
            cpu_stq_le_data_ra(env, addr, value, ra);
        }
        break;
    default:
        g_assert_not_reached();
    }
    ia64_alat_write_end(env, addr, size);
}

uint64_t ia64_exec_load_mmuidx(CPUIA64State *env, uint64_t addr,
                               unsigned size, bool big_endian, int mmu_idx,
                               uintptr_t ra)
{
    switch (size) {
    case 1:
        return cpu_ldub_mmuidx_ra(env, addr, mmu_idx, ra);
    case 8:
        return big_endian ? cpu_ldq_be_mmuidx_ra(env, addr, mmu_idx, ra) :
                            cpu_ldq_le_mmuidx_ra(env, addr, mmu_idx, ra);
    default:
        g_assert_not_reached();
    }
}

void ia64_exec_store_mmuidx(CPUIA64State *env, uint64_t addr, uint64_t value,
                            unsigned size, bool big_endian, int mmu_idx,
                            uintptr_t ra)
{
    ia64_alat_write_begin(env);
    switch (size) {
    case 1:
        cpu_stb_mmuidx_ra(env, addr, value, mmu_idx, ra);
        break;
    case 8:
        if (big_endian) {
            cpu_stq_be_mmuidx_ra(env, addr, value, mmu_idx, ra);
        } else {
            cpu_stq_le_mmuidx_ra(env, addr, value, mmu_idx, ra);
        }
        break;
    default:
        g_assert_not_reached();
    }
    ia64_alat_write_end(env, addr, size);
}

/*
 * Store a completed RSE NaT collection without exposing the preserved
 * portion as an architectural load.
 *
 * When undefined positions need preservation, a store probe performs the
 * guest's write translation and permission checks.  For writable RAM,
 * inspect the resolved host backing directly so that read permission, read
 * watchpoints and plugin load callbacks are not involved.  The normal store
 * below remains the only architected memory access and therefore retains
 * write watchpoint, dirty-page and plugin semantics.  A fully defined
 * collection takes the ordinary single-store path without probing.
 */
uint64_t ia64_exec_rse_store_collection(CPUIA64State *env, uint64_t addr,
                                        uint64_t value, uint64_t defined,
                                        bool big_endian, int mmu_idx,
                                        uint64_t *previous, uintptr_t ra)
{
    static int non_ram_reported;
    CPUTLBEntryFull *full;
    void *host;
    uint64_t old = 0;
    int flags;
    bool writable_ram;

    g_assert((addr & 7) == 0);
    defined &= INT64_MAX;
    value &= defined;

    if (defined == INT64_MAX) {
        *previous = 0;
        ia64_exec_store_mmuidx(env, addr, value, 8, big_endian, mmu_idx, ra);
        return value;
    }

    flags = probe_access_full(env, addr, 8, MMU_DATA_STORE, mmu_idx, false,
                              &host, &full, ra);
    writable_ram = memory_region_is_ram(full->section->mr) &&
                   !memory_region_is_protected(full->section->mr) &&
                   !full->section->readonly &&
                   !(full->slow_flags[MMU_DATA_STORE] &
                     (TLB_BSWAP | TLB_DISCARD_WRITE | TLB_MMIO));

    if (writable_ram) {
        if (host == NULL) {
            /*
             * Plugin instrumentation deliberately hides the direct host
             * pointer.  xlat_offset still names the same RAM backing and is
             * safe to consume before the next TLB access.
             */
            host = qemu_map_ram_ptr(NULL, addr + full->xlat_offset);
        }
        old = big_endian ? ldq_be_p(host) : ldq_le_p(host);
    } else if (qatomic_cmpxchg(&non_ram_reported, 0, 1) == 0) {
        /*
         * The architecture requires an RSE backing store to be ordinary
         * cacheable memory.  Never issue a private read against MMIO or ROMD:
         * retain the deterministic zero choice for undefined bits instead.
         */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ia64 RSE RNAT store to non-writable RAM/MMIO at "
                      "0x%016" PRIx64 " cannot preserve undefined bits "
                      "(flags=0x%x)\n", addr, flags);
    }

    *previous = old;
    value = ((old & ~defined) | value) & INT64_MAX;
    ia64_exec_store_mmuidx(env, addr, value, 8, big_endian, mmu_idx, ra);
    return value;
}

Int128 ia64_exec_load_16(CPUIA64State *env, uint64_t addr, MemOpIdx oi,
                         uintptr_t ra)
{
    return cpu_ld16_mmu(env, addr, oi, ra);
}

void ia64_exec_store_16(CPUIA64State *env, uint64_t addr, Int128 value,
                        MemOpIdx oi, uintptr_t ra)
{
    ia64_alat_write_begin(env);
    cpu_st16_mmu(env, addr, value, oi, ra);
    ia64_alat_write_end(env, addr, 16);
}

uint64_t ia64_exec_cmpxchg(CPUIA64State *env, uint64_t addr, uint64_t cmp,
                           uint64_t value, unsigned size, bool big_endian,
                           MemOpIdx oi, uintptr_t ra)
{
    uint64_t result;
    uint64_t mask;

    ia64_alat_write_begin(env);
    switch (size) {
    case 1:
        mask = UINT8_MAX;
        result = cpu_atomic_cmpxchgb_mmu(env, addr, cmp, value, oi, ra);
        break;
    case 2:
        mask = UINT16_MAX;
        result = big_endian ?
            cpu_atomic_cmpxchgw_be_mmu(env, addr, cmp, value, oi, ra) :
            cpu_atomic_cmpxchgw_le_mmu(env, addr, cmp, value, oi, ra);
        break;
    case 4:
        mask = UINT32_MAX;
        result = big_endian ?
            cpu_atomic_cmpxchgl_be_mmu(env, addr, cmp, value, oi, ra) :
            cpu_atomic_cmpxchgl_le_mmu(env, addr, cmp, value, oi, ra);
        break;
    case 8:
        mask = UINT64_MAX;
        result = big_endian ?
            cpu_atomic_cmpxchgq_be_mmu(env, addr, cmp, value, oi, ra) :
            cpu_atomic_cmpxchgq_le_mmu(env, addr, cmp, value, oi, ra);
        break;
    default:
        g_assert_not_reached();
    }
    if ((result & mask) == (cmp & mask)) {
        ia64_alat_write_end(env, addr, size);
    } else {
        ia64_alat_write_cancel(env);
    }
    return result;
}

Int128 ia64_exec_cmpxchg_16(CPUIA64State *env, uint64_t addr, Int128 cmp,
                            Int128 value, bool big_endian,
                            unsigned invalidation_size, MemOpIdx oi,
                            uintptr_t ra)
{
#if HAVE_CMPXCHG128
    Int128 result;

    ia64_alat_write_begin(env);
    result = big_endian ?
        cpu_atomic_cmpxchgo_be_mmu(env, addr, cmp, value, oi, ra) :
        cpu_atomic_cmpxchgo_le_mmu(env, addr, cmp, value, oi, ra);
    if (int128_eq(result, cmp)) {
        ia64_alat_write_end(env, addr, invalidation_size);
    } else {
        ia64_alat_write_cancel(env);
    }
    return result;
#else
    g_assert_not_reached();
#endif
}

void ia64_exec_probe_write(CPUIA64State *env, uint64_t addr, int size,
                           int mmu_idx, uintptr_t ra)
{
    probe_write(env, addr, size, mmu_idx, ra);
}

bool ia64_exec_probe_host(CPUIA64State *env, uint64_t addr, int size,
                          MMUAccessType access_type, int mmu_idx,
                          void **host, uintptr_t ra)
{
    int flags = probe_access_flags(env, addr, size, access_type, mmu_idx,
                                   false, host, ra);
    bool direct = flags == 0 && *host != NULL;

    return direct;
}

bool ia64_exec_probe_writeback_ram(CPUIA64State *env, uint64_t addr,
                                   int size, MMUAccessType access_type,
                                   bool *direct, uintptr_t ra)
{
    CPUTLBEntryFull *full;
    void *host;
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);
    int flags = probe_access_full(env, addr, size, access_type, mmu_idx,
                                  false, &host, &full, ra);

    *direct = !(flags & (TLB_MMIO | TLB_WATCHPOINT));
    return full->extra.ia64.memory_attribute == IA64_PTE_MA_WB &&
           memory_region_is_ram(full->section->mr);
}

bool ia64_exec_probe_writeback(CPUIA64State *env, uint64_t addr,
                               int size, MMUAccessType access_type,
                               uintptr_t ra)
{
    CPUState *cs = env_cpu(env);
    int mmu_idx = cpu_mmu_index(cs, false);
    bool writeback = true;

    while (size > 0) {
        CPUTLBEntryFull *full;
        void *host;
        int page_offset = addr & ((1u << TARGET_PAGE_BITS) - 1);
        int page_left = (1u << TARGET_PAGE_BITS) - page_offset;
        int chunk = MIN(size, page_left);
        int flags = probe_access_full(env, addr, chunk, access_type,
                                      mmu_idx, false, &host, &full, ra);

        if (flags & TLB_WATCHPOINT) {
            cpu_check_watchpoint(cs, addr, chunk, full->attrs,
                                 BP_MEM_ACCESS, ra);
        }
        writeback &= full->extra.ia64.memory_attribute == IA64_PTE_MA_WB;
        addr += chunk;
        if (env->psr & IA64_PSR_IS) {
            addr = (uint32_t)addr;
        }
        size -= chunk;
    }
    return writeback;
}

bool ia64_exec_advanced_load_allowed(CPUIA64State *env, uint64_t addr,
                                     int mmu_idx)
{
    CPUTLBEntryFull *full;
    void *host;
    int flags = probe_access_full(env, addr, 1, MMU_DATA_LOAD, mmu_idx, true,
                                  &host, &full, 0);

    if (flags & TLB_INVALID_MASK) {
        return true;
    }
    return full->extra.ia64.speculation != IA64_MEM_NON_SPECULATIVE;
}

bool ia64_exec_cached_load_translation(
    CPUIA64State *env, uint64_t addr, int mmu_idx,
    IA64CachedLoadTranslation *translation)
{
    CPUTLBEntryFull *full;
    bool from_victim;

    if (!tlb_lookup_full_no_fill(env, addr, MMU_DATA_LOAD, mmu_idx, &full,
                                 &from_victim)) {
        return false;
    }

    *translation = (IA64CachedLoadTranslation) {
        .phys_page = full->phys_addr,
        .speculation = full->extra.ia64.speculation,
        .memory_attribute = full->extra.ia64.memory_attribute,
        .prot = full->prot,
        .from_victim = from_victim,
        .is_ram = memory_region_is_ram(full->section->mr),
    };
    return true;
}

bool ia64_exec_debug_read(CPUState *cs, uint64_t addr, void *buffer,
                          size_t size)
{
    return cpu_memory_rw_debug(cs, addr, buffer, size, false) == 0;
}

bool ia64_exec_physical_rw(uint64_t addr, void *buffer, size_t size,
                           bool is_write)
{
    return address_space_rw(&address_space_memory, addr,
                            MEMTXATTRS_UNSPECIFIED, buffer, size,
                            is_write) == MEMTX_OK;
}

void ia64_exec_invalidate_phys_range(CPUIA64State *env, uint64_t addr,
                                     uint64_t length)
{
    hwaddr start = addr;
    hwaddr remaining = length;

    /*
     * TB addresses use RAMBlock offsets, not guest physical addresses.
     * Resolve aliases so high RAM invalidates the correct backing pages.
     */
    RCU_READ_LOCK_GUARD();
    while (remaining != 0) {
        hwaddr xlat;
        hwaddr len = remaining;
        MemoryRegion *mr;

        mr = address_space_translate(&address_space_memory, start, &xlat,
                                     &len, false, MEMTXATTRS_UNSPECIFIED);
        if (len == 0) {
            break;
        }
        if (memory_region_is_ram(mr) || memory_region_is_romd(mr)) {
            ram_addr_t ram_addr = memory_region_get_ram_addr(mr) + xlat;

            tb_invalidate_phys_range(env_cpu(env), ram_addr,
                                     ram_addr + len - 1);
        }
        start += len;
        remaining -= len;
    }
}
