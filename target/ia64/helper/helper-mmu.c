/* IA-64 TCG helper ABI adapters for MMU serialization. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "arch/arch.h"
#include "trace.h"

void helper_tlb_serialize(CPUIA64State *env, uint32_t include_data,
                          uint32_t include_inst)
{
    ia64_tlb_serialize(env, include_data, include_inst);
}

void helper_merced_dtlb1_touch(CPUIA64State *env, uint64_t va, uint32_t size)
{
    uint64_t end = va + size - 1U;

    /*
     * Translated memory helpers normally revisit the same minimum page many
     * times before replacement.  Complete that direct-hit case here so the
     * generated helper call does not enter the general MMU access path.
     * Cross-page accesses and the theoretical LRU clock wrap retain the full
     * path, which touches both endpoints and rebases all ages respectively.
     */
    if (ia64_env_cpu_class(env)->model == IA64_CPU_MODEL_MERCED &&
        size != 0 && end >= va &&
        ia64_merced_dtlb1_lookup_page(end) ==
            ia64_merced_dtlb1_lookup_page(va) &&
        env->mmu.tlb_data_l1_clock != UINT64_MAX) {
        uint32_t rid = ia64_region_rid(env, va);
        uint8_t last = env->mmu.tlb_data_l1_last;
        int cached;

        /*
         * Repeatedly touching the current MRU entry cannot change LRU order.
         * Revalidate the derived hint against the live entry so purges,
         * replacements, RR changes, and hash collisions remain harmless.
         */
        if (last != 0 && last <= IA64_DTLB1_MAX &&
            ia64_tlb_match(&env->mmu.tlb_data_l1[last - 1U], va, rid)) {
            return;
        }
        cached = ia64_merced_dtlb1_lookup(env, va, rid);
        if (cached >= 0) {
            env->mmu.tlb_data_l1_age[cached] =
                ++env->mmu.tlb_data_l1_clock;
            env->mmu.tlb_data_l1_last = cached + 1U;
            return;
        }
    }

    ia64_mmu_data_access(env, va, size, true);
}

void helper_fc(CPUIA64State *env, uint64_t addr,
               uint32_t instruction_cache_coherent)
{
    ia64_mmu_fc(env, addr, instruction_cache_coherent);
}

void helper_itr_insert(CPUIA64State *env, uint64_t pte, uint64_t slot_reg,
                       uint32_t is_data, uint64_t raw, uint32_t fault_slot)
{
    ia64_mmu_itr_insert(env, pte, slot_reg, is_data, raw, fault_slot);
}

void helper_ptr_purge(CPUIA64State *env, uint64_t ifa, uint64_t size_reg,
                      uint32_t is_data, uint64_t raw, uint32_t fault_slot)
{
    ia64_mmu_ptr_purge(env, ifa, size_reg, is_data, raw, fault_slot);
}

void helper_ptc_purge(CPUIA64State *env, uint64_t va, uint64_t size_reg,
                      uint32_t mode, uint64_t raw, uint32_t fault_slot)
{
    ia64_mmu_ptc_purge(env, va, size_reg, mode, raw, fault_slot);
}

uint64_t helper_tpa(CPUIA64State *env, uint64_t va)
{
    return ia64_mmu_tpa(env, va);
}

uint64_t helper_probe(CPUIA64State *env, uint64_t va, uint32_t is_write,
                      uint64_t access_level)
{
    return ia64_mmu_probe(env, va, is_write, access_level);
}

void helper_probe_fault(CPUIA64State *env, uint64_t va, uint32_t is_write,
                        uint32_t is_rw, uint64_t access_level)
{
    ia64_mmu_probe_fault(env, va, is_write, is_rw, access_level);
}

void helper_lfetch_fault(CPUIA64State *env, uint64_t va,
                         uint64_t fault_info, uint32_t hint)
{
    ia64_mmu_lfetch_fault(env, va, fault_info, hint);
}

void helper_data_debug(CPUIA64State *env, uint64_t va, uint32_t size,
                       uint64_t isr_access, uint64_t fault_info)
{
    ia64_mmu_check_data_debug(
        env, va, size, isr_access, ia64_psr_cpl(env->psr), false,
        fault_info & ~3ULL, fault_info & 3);
}

void helper_instruction_debug(CPUIA64State *env, uint64_t address,
                              uint32_t slot)
{
    ia64_check_instruction_debug(env, address, slot);
}

void helper_check_semaphore_access(CPUIA64State *env, uint64_t va)
{
    ia64_mmu_check_semaphore_access(env, va);
}

void helper_check_montecito_16byte_access(CPUIA64State *env, uint64_t va,
                                          uint32_t is_write)
{
    ia64_mmu_check_montecito_16byte_access(env, va, is_write);
}

void helper_check_alignment(CPUIA64State *env, uint64_t va,
                            uint32_t alignment_info, uint64_t isr_access,
                            uint64_t fault_info)
{
    ia64_mmu_check_alignment(env, va, alignment_info, isr_access, fault_info);
}

uint64_t helper_speculative_probe(CPUIA64State *env, uint64_t va,
                                  uint32_t is_write, uint32_t is_ifetch,
                                  uint32_t debug_size,
                                  uint32_t alignment_info)
{
    uint64_t result = ia64_mmu_speculative_probe(
        env, va, is_write, is_ifetch, debug_size, alignment_info);

    if (!result) {
        trace_ia64_speculative_probe_defer(
            env_cpu(env)->cpu_index, env->ip, va,
            alignment_info & IA64_ALIGNMENT_DATUM_MASK);
    }
    return result;
}

uint64_t helper_speculative_int_probe(CPUIA64State *env, uint64_t va,
                                      uint32_t size)
{
    uint64_t result = ia64_mmu_speculative_int_probe(env, va, size);
    if (!result) {
        trace_ia64_speculative_probe_defer(
            env_cpu(env)->cpu_index, env->ip, va, size);
    }
    return result;
}

Int128 helper_speculative_int_probe_pa(CPUIA64State *env, uint64_t va,
                                        uint32_t size)
{
    uint64_t pa;
    uint64_t result = ia64_mmu_speculative_int_probe_pa(env, va, size, &pa);

    if (!result) {
        trace_ia64_speculative_probe_defer(
            env_cpu(env)->cpu_index, env->ip, va, size);
    }
    return int128_make128(result, pa);
}

Int128 helper_speculative_probe_pa(CPUIA64State *env, uint64_t va,
                                    uint32_t is_write, uint32_t is_ifetch,
                                    uint32_t debug_size,
                                    uint32_t alignment_info)
{
    uint64_t pa;
    uint64_t result = ia64_mmu_speculative_probe_pa(
        env, va, is_write, is_ifetch, debug_size, alignment_info, &pa);

    if (!result) {
        trace_ia64_speculative_probe_defer(
            env_cpu(env)->cpu_index, env->ip, va,
            alignment_info & IA64_ALIGNMENT_DATUM_MASK);
    }
    return int128_make128(result, pa);
}

uint64_t helper_advanced_load_allowed(CPUIA64State *env, uint64_t va)
{
    return ia64_mmu_advanced_load_allowed(env, va);
}

uint64_t helper_tak(CPUIA64State *env, uint64_t va)
{
    return ia64_mmu_tak(env, va);
}

uint64_t helper_thash(CPUIA64State *env, uint64_t va)
{
    return ia64_mmu_thash(env, va);
}

uint64_t helper_ttag(CPUIA64State *env, uint64_t va)
{
    return ia64_mmu_ttag(env, va);
}

void helper_itc_insert(CPUIA64State *env, uint64_t pte, uint32_t is_data,
                       uint64_t raw, uint32_t fault_slot)
{
    ia64_mmu_itc_insert(env, pte, is_data, raw, fault_slot);
}
