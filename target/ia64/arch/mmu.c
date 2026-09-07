/*
 * IA-64 MMU, translation cache, VHPT, purge, and probe architecture logic.
 *
 * IA64MMUState owns architected TR/TC entries and derived replacement and
 * purge bookkeeping.  TCG helper ABI adapters live in helper/helper-mmu.c.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "arch/arch.h"
#include "exec-access.h"
#include "exec/cpu-common.h"
#include "exec/cputlb.h"
#include "exec/tb-flush.h"
#include "exec/target_page.h"
#include "exec/tlb-flags.h"
#include "trace.h"

#define IA64_PTE_PPN_MASK 0x0003fffffffff000ULL
#define IA64_PTE_PL_SHIFT 7
#define IA64_PTE_PL_MASK  (3ULL << IA64_PTE_PL_SHIFT)
#define IA64_PTE_AR_SHIFT 9
#define IA64_PTE_AR_MASK  (7ULL << IA64_PTE_AR_SHIFT)
#define IA64_PTE_RESERVED_MASK ((1ULL << 1) | (3ULL << 50))
#define IA64_ITIR_RESERVED_MASK (3ULL | (0xffffffffULL << 32))

static uint8_t ia64_pte_ar(uint64_t pte)
{
    return (pte & IA64_PTE_AR_MASK) >> IA64_PTE_AR_SHIFT;
}

static uint8_t ia64_pte_pl(uint64_t pte)
{
    return (pte & IA64_PTE_PL_MASK) >> IA64_PTE_PL_SHIFT;
}

static uint8_t ia64_pte_perm(uint64_t pte, uint8_t access_level)
{
    if (!(pte & IA64_PTE_PRESENT)) {
        return 0;
    }

    return ia64_tlb_effective_perm(ia64_pte_ar(pte), ia64_pte_pl(pte),
                                   access_level);
}

static uint64_t ia64_page_size_from_shift(uint64_t ps_bits)
{
    if (ps_bits < 12) {
        ps_bits = 12;
    }
    return 1ULL << ps_bits;
}

static uint64_t ia64_itir_page_size(CPUIA64State *env)
{
    uint64_t ps_bits = (env->cr_itir >> IA64_ITIR_PS_SHIFT) & IA64_ITIR_PS_MASK;

    return ia64_page_size_from_shift(ps_bits);
}

static uint64_t ia64_gr_page_size(uint64_t value)
{
    return ia64_page_size_from_shift((value >> IA64_ITIR_PS_SHIFT) &
                                     IA64_ITIR_PS_MASK);
}

static bool ia64_translation_insert_fields_valid(CPUIA64State *env,
                                                 uint64_t pte,
                                                 uint64_t itir)
{
    uint8_t page_shift = (itir >> IA64_ITIR_PS_SHIFT) &
                         IA64_ITIR_PS_MASK;
    uint64_t itir_reserved = IA64_ITIR_RESERVED_MASK;
    uint8_t ma;

    if (!ia64_page_shift_insertable(env, page_shift)) {
        return false;
    }
    if (!(pte & IA64_PTE_PRESENT)) {
        itir_reserved &= 3;
        return !(itir & itir_reserved);
    }

    ma = (pte & IA64_PTE_MA_MASK) >> IA64_PTE_MA_SHIFT;
    return !(pte & IA64_PTE_RESERVED_MASK) &&
           !(itir & itir_reserved) && (ma == 0 || ma >= 4);
}

static bool ia64_tlb_entry_overlaps(const IA64TlbEntry *entry,
                                    uint64_t start, uint64_t ps,
                                    uint32_t rid)
{
    uint64_t mask;

    if (entry->rid != rid || !entry->valid || entry->ps == 0) {
        return false;
    }

    /*
     * Both ranges are power-of-two sized and naturally aligned.  They
     * overlap exactly when their bases agree after masking by the larger
     * page size.  Reuse the entry's precomputed mask in the common case.
     */
    mask = entry->ps >= ps ? entry->page_mask : ia64_va_vpn_mask(ps);
    return ((entry->va ^ start) & mask) == 0;
}

static void ia64_qemu_tlb_flush_entry(CPUIA64State *env,
                                      const IA64TlbEntry *entry,
                                      bool is_data)
{
    uint64_t base;
    uint8_t region;

    if (!entry->valid || entry->ps < TARGET_PAGE_SIZE) {
        return;
    }

    base = ia64_va_page_base(entry->va, entry->ps);
    for (region = 0; region <= IA64_REGION_MASK; region++) {
        if (ia64_rr_rid(env->rr[region]) != entry->rid) {
            continue;
        }

        uint64_t va = ((uint64_t)region << IA64_REGION_SHIFT) | base;

        if (is_data) {
            /*
             * IA-64 has separate data and instruction translation caches.
             * Clearing QEMU's unified softmmu entry is still required, but
             * an architecturally data-only change cannot stale a translated
             * code block or its jump-cache hint.
             */
            tlb_flush_range_by_mmuidx_no_jmp_cache(
                env_cpu(env), va, entry->ps, MMU_IDX_TRANSLATED_MASK,
                TARGET_LONG_BITS);
        } else {
            tlb_flush_range_by_mmuidx(env_cpu(env), va, entry->ps,
                                      MMU_IDX_TRANSLATED_MASK,
                                      TARGET_LONG_BITS);
        }
    }
}

static int ia64_tlb_select_tc_slot(IA64TlbEntry *tlb, uint16_t capacity,
                                   uint16_t *next_replace, uint64_t va,
                                   uint32_t rid);

bool ia64_memory_allows_advanced_load(IA64MemorySpeculation spec)
{
    return spec != IA64_MEM_NON_SPECULATIVE;
}

static bool ia64_memory_allows_control_speculation(IA64MemorySpeculation spec)
{
    return spec == IA64_MEM_SPECULATIVE;
}

static bool ia64_data_address_to_mapped_phys_attr(CPUIA64State *env,
                                                  uint64_t va, bool is_rse,
                                                  uint8_t access_level,
                                                  uint64_t *pa,
                                                  IA64MemorySpeculation *spec)
{
    uint8_t perm;
    uint32_t rid;
    const IA64TlbEntry *entry;

    if (ia64_firmware_identity_pa(env->cr_iva, env->psr, va,
                                  pa)) {
        if (spec) {
            *spec = IA64_MEM_SPECULATIVE;
        }
        return true;
    }

    rid = ia64_region_rid(env, va);
    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (entry) {
        ia64_tlb_entry_translate(entry, va, access_level, pa, &perm);
        if (spec) {
            *spec = ia64_pte_memory_speculation(entry->pte);
        }
        return (perm & IA64_TLB_R) != 0;
    }

    {
        uint64_t pte = 0;

        if (ia64_vhpt_walk_full(env, va, rid, false, is_rse, access_level,
                                pa, &perm, &pte, NULL, NULL)) {
            if (spec) {
                *spec = ia64_pte_memory_speculation(pte);
            }
            return (perm & IA64_TLB_R) != 0;
        }
    }

    return false;
}


bool ia64_data_address_to_phys_attr(CPUIA64State *env, uint64_t va,
                                    uint64_t *pa,
                                    IA64MemorySpeculation *spec)
{
    if (!(env->psr & IA64_PSR_DT)) {
        *pa = ia64_physical_address(va);
        if (spec) {
            *spec = (va & IA64_PHYS_UC_BIT) ?
                    IA64_MEM_NON_SPECULATIVE :
                    IA64_MEM_LIMITED_SPECULATION;
        }
        return true;
    }

    return ia64_data_address_to_mapped_phys_attr(
        env, va, false, ia64_psr_cpl(env->psr), pa, spec);
}

bool ia64_data_address_to_phys(CPUIA64State *env, uint64_t va,
                               uint64_t *pa)
{
    return ia64_data_address_to_phys_attr(env, va, pa, NULL);
}

typedef enum IA64NonAccessKind {
    IA64_NON_ACCESS_TPA,
    IA64_NON_ACCESS_FC,
} IA64NonAccessKind;

static IA64Exception
ia64_translate_nonaccess(CPUIA64State *env, uint64_t va,
                         IA64NonAccessKind kind, uint64_t *pa)
{
    const bool is_tpa = kind == IA64_NON_ACCESS_TPA;
    const uint8_t access_level = ia64_psr_cpl(env->psr);
    const IA64TlbEntry *entry;
    uint32_t rid = ia64_region_rid(env, va);
    uint8_t perm = 0;
    uint8_t vhpt_size;
    bool vhpt_long_format;
    bool vhpt_enabled;
    uint64_t pte = 0;

    if (!(env->psr & IA64_PSR_DT)) {
        /*
         * fc/fc.i are physical references while data translation is off.
         * tpa is deliberately different: it still queries the DTLB, but
         * never invokes the VHPT walker (SDM Vol. 3, tpa).
         */
        if (!is_tpa) {
            if (!ia64_pa_is_implemented(env, va)) {
                return IA64_EXCP_UNIMPL_DATA_ADDR;
            }
            *pa = ia64_physical_address(va);
            return IA64_EXCP_NONE;
        }
        if (!ia64_va_is_implemented(env, va)) {
            return IA64_EXCP_UNIMPL_DATA_ADDR;
        }
    } else {
        if (!ia64_va_is_implemented(env, va)) {
            return IA64_EXCP_UNIMPL_DATA_ADDR;
        }

        /* Preserve the synthetic firmware identity window used at boot. */
        if (!is_tpa &&
            ia64_firmware_identity_pa(env->cr_iva, env->psr,
                                      va, pa)) {
            return IA64_EXCP_NONE;
        }
    }

    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (entry) {
        ia64_tlb_entry_translate(entry, va, access_level, pa, &perm);
        pte = entry->pte;
    } else if (env->psr & IA64_PSR_DT) {
        const IA64TlbEntry *installed_entry = NULL;

        if (ia64_vhpt_walk_full(env, va, rid, false, false, access_level,
                                pa, &perm, &pte, NULL, &installed_entry)) {
            if (installed_entry) {
                pte = installed_entry->pte;
            }
        } else {
            if (ia64_data_nested_tlb_active(env)) {
                return IA64_EXCP_DATA_NESTED_TLB;
            }

            vhpt_enabled = ia64_vhpt_walker_enabled(
                env, va, false, false, &vhpt_size, &vhpt_long_format);
            if (vhpt_enabled &&
                !ia64_vhpt_entry_accessible(env, va, false, false,
                                            &env->cr_iha)) {
                return IA64_EXCP_VHPT_FAULT;
            }
            return vhpt_enabled ? IA64_EXCP_DTLB_FAULT :
                                  IA64_EXCP_ALT_DTLB;
        }
    } else {
        return ia64_data_nested_tlb_active(env) ?
               IA64_EXCP_DATA_NESTED_TLB : IA64_EXCP_ALT_DTLB;
    }

    /*
     * A non-access translation checks only P, NaTPage, and (for fc/fc.i
     * above PL0) read access rights.  It never checks protection keys,
     * dirty/access bits, the memory attribute, or data breakpoints.
     */
    if (!(pte & IA64_PTE_PRESENT)) {
        return IA64_EXCP_PAGE_NOT_PRESENT;
    }
    if (ia64_pte_ma(pte) == IA64_PTE_MA_NATPAGE) {
        return IA64_EXCP_NAT_CONSUMPTION;
    }
    if (!is_tpa && access_level != 0 && !(perm & IA64_TLB_R)) {
        return IA64_EXCP_DATA_ACCESS;
    }
    return IA64_EXCP_NONE;
}

static G_NORETURN void
ia64_raise_nonaccess_exception(CPUIA64State *env, uint64_t va,
                               IA64NonAccessKind kind, IA64Exception excp)
{
    const bool is_fc = kind == IA64_NON_ACCESS_FC;
    const uint8_t code = is_fc ? 1 : 0;
    uint64_t isr = IA64_ISR_NA | code | (is_fc ? IA64_ISR_R : 0);

    if (env->psr & IA64_PSR_IC) {
        env->cr_ifa = va;
        if (ia64_exception_initializes_iha(excp)) {
            env->cr_iha = ia64_vhpt_hash_address(env, va);
        }
        env->cr_itir = ia64_region_itir(
            env, excp == IA64_EXCP_VHPT_FAULT ? env->cr_iha : va);
    }
    if (excp != IA64_EXCP_DATA_NESTED_TLB) {
        if (excp == IA64_EXCP_UNIMPL_DATA_ADDR) {
            isr |= IA64_GENEX_UNIMPL_DATA_ADDR;
        } else if (excp == IA64_EXCP_NAT_CONSUMPTION) {
            isr |= IA64_ISR_CODE_NAT_PAGE;
        }
        if (excp != IA64_EXCP_NAT_CONSUMPTION &&
            ia64_current_code_tlb_ed(env)) {
            isr |= IA64_ISR_ED;
        }
        env->cr_isr = isr;
    }

    ia64_raise_exception(env, excp, ia64_ip_bundle_addr(env->ip), 0,
                         (env->psr & IA64_PSR_RI_MASK) >>
                         IA64_PSR_RI_SHIFT);
}


void ia64_mmu_fc(CPUIA64State *env, uint64_t addr,
                 bool instruction_cache_coherent)
{
    uint64_t line_size = ia64_env_cpu_class(env)->fc_line_size;
    IA64Exception excp;
    uint64_t pa;

    excp = ia64_translate_nonaccess(env, addr, IA64_NON_ACCESS_FC, &pa);
    if (excp != IA64_EXCP_NONE) {
        ia64_raise_nonaccess_exception(env, addr, IA64_NON_ACCESS_FC, excp);
    }

    /*
     * Removing a cache line is an architected ALAT collision event.  fc.i is
     * permitted to remove the line as part of making the I-cache coherent,
     * and an implementation may conservatively discard an ALAT entry at any
     * time, so use the same line invalidation for both forms.
     */
    ia64_invalidate_alat_phys_range(
        env, pa & ~(line_size - 1), line_size);

    if (instruction_cache_coherent) {
        uint64_t start = pa & ~(line_size - 1);

        ia64_exec_invalidate_phys_range(env, start, line_size);
    }
}

static void ia64_discard_pending_purge(IA64TlbEntry *entry,
                                       uint16_t *pending_count)
{
    if (!entry->pending_purge) {
        return;
    }

    g_assert(*pending_count > 0);
    entry->pending_purge = 0;
    (*pending_count)--;
}

static bool ia64_merced_dtlb1_enabled(CPUIA64State *env)
{
    return ia64_env_cpu_class(env)->model == IA64_CPU_MODEL_MERCED;
}

static bool ia64_tlb_entries_equivalent(const IA64TlbEntry *a,
                                        const IA64TlbEntry *b)
{
    return a->valid && b->valid &&
           a->va == b->va && a->pa == b->pa &&
           a->ps == b->ps && a->pte == b->pte &&
           a->rid == b->rid && a->key == b->key;
}

static bool ia64_merced_dtlb1_contains(CPUIA64State *env,
                                      const IA64TlbEntry *entry)
{
    uint16_t i;

    if (!ia64_merced_dtlb1_enabled(env) || !entry->valid) {
        return false;
    }
    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        if (ia64_tlb_entries_equivalent(&env->mmu.tlb_data_l1[i], entry)) {
            return true;
        }
    }
    return false;
}

static void ia64_merced_dtlb1_stamp(CPUIA64State *env, uint8_t slot)
{
    if (env->mmu.tlb_data_l1_clock == UINT64_MAX) {
        uint16_t i;

        /*
         * The wrap interval is purely theoretical, but keep the derived
         * replacement state deterministic without changing relative age.
         */
        for (i = 0; i < IA64_DTLB1_MAX; i++) {
            env->mmu.tlb_data_l1_age[i] =
                env->mmu.tlb_data_l1[i].valid ? i + 1U : 0;
        }
        env->mmu.tlb_data_l1_clock = IA64_DTLB1_MAX;
    }
    env->mmu.tlb_data_l1_age[slot] =
        ++env->mmu.tlb_data_l1_clock;
    env->mmu.tlb_data_l1_last = slot + 1U;
}

static void ia64_merced_dtlb1_invalidate_slot(CPUIA64State *env,
                                               uint8_t slot)
{
    IA64TlbEntry *entry = &env->mmu.tlb_data_l1[slot];

    if (!entry->valid) {
        return;
    }
    ia64_qemu_tlb_flush_entry(env, entry, true);
    entry->valid = 0;
    env->mmu.tlb_data_l1_age[slot] = 0;
    g_assert(env->mmu.tlb_data_l1_count > 0);
    env->mmu.tlb_data_l1_count--;
}

static void ia64_merced_dtlb1_invalidate_copy(CPUIA64State *env,
                                               const IA64TlbEntry *source)
{
    uint16_t i;

    if (!ia64_merced_dtlb1_enabled(env)) {
        return;
    }
    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        if (ia64_tlb_entries_equivalent(&env->mmu.tlb_data_l1[i],
                                        source)) {
            ia64_merced_dtlb1_invalidate_slot(env, i);
        }
    }
}

static void ia64_merced_dtlb1_remember(CPUIA64State *env, uint64_t va,
                                       uint32_t rid, uint8_t slot)
{
    uint64_t page = ia64_merced_dtlb1_lookup_page(va);
    IA64DTlb1Lookup *lookup = &env->mmu.tlb_data_l1_lookup[
        ia64_merced_dtlb1_lookup_index(page, rid)];

    *lookup = (IA64DTlb1Lookup) {
        .page = page,
        .rid = rid,
        .slot = slot,
        .valid = true,
    };
}

static void ia64_merced_dtlb1_touch_one(CPUIA64State *env, uint64_t va)
{
    const IA64TlbEntry *source = NULL;
    int empty = -1;
    int cached;
    uint64_t oldest_age = UINT64_MAX;
    uint8_t oldest = 0;
    uint32_t rid = ia64_region_rid(env, va);
    uint16_t i;

    cached = ia64_merced_dtlb1_lookup(env, va, rid);
    if (cached >= 0) {
        ia64_merced_dtlb1_stamp(env, cached);
        return;
    }
    if (env->mmu.tlb_data_l1_count == 0) {
        goto find_dtlb2;
    }
    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        if (ia64_tlb_match(&env->mmu.tlb_data_l1[i], va, rid)) {
            ia64_merced_dtlb1_remember(env, va, rid, i);
            ia64_merced_dtlb1_stamp(env, i);
            return;
        }
    }

find_dtlb2:
    for (i = 0; i < env->mmu.tlb_data_count; i++) {
        if (!env->mmu.tlb_data[i].pending_purge &&
            ia64_tlb_match(&env->mmu.tlb_data[i], va, rid)) {
            source = &env->mmu.tlb_data[i];
            break;
        }
    }
    if (!source) {
        return;
    }

    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        IA64TlbEntry *entry = &env->mmu.tlb_data_l1[i];

        if (ia64_tlb_match(entry, va, rid)) {
            if (ia64_tlb_entries_equivalent(entry, source)) {
                *entry = *source;
                ia64_merced_dtlb1_remember(env, va, rid, i);
                ia64_merced_dtlb1_stamp(env, i);
                return;
            }
            ia64_merced_dtlb1_invalidate_slot(env, i);
        }
        if (!entry->valid) {
            if (empty < 0) {
                empty = i;
            }
        } else if (env->mmu.tlb_data_l1_age[i] < oldest_age) {
            oldest_age = env->mmu.tlb_data_l1_age[i];
            oldest = i;
        }
    }

    if (empty < 0) {
        empty = oldest;
        ia64_merced_dtlb1_invalidate_slot(env, empty);
    }
    env->mmu.tlb_data_l1[empty] = *source;
    env->mmu.tlb_data_l1_count++;
    ia64_merced_dtlb1_remember(env, va, rid, empty);
    ia64_merced_dtlb1_stamp(env, empty);
}

void ia64_mmu_data_access(CPUIA64State *env, uint64_t va, uint32_t size,
                          bool translated)
{
    uint64_t end;

    if (!ia64_merced_dtlb1_enabled(env) ||
        !translated || size == 0) {
        return;
    }

    ia64_merced_dtlb1_touch_one(env, va);
    end = va + size - 1U;
    if (end >= va && ((end ^ va) & TARGET_PAGE_MASK) != 0) {
        ia64_merced_dtlb1_touch_one(env, end);
    }
}

static bool ia64_merced_dtlb1_purge_range(CPUIA64State *env, uint64_t va,
                                          uint64_t ps, uint32_t rid,
                                          bool tc_only)
{
    uint64_t start = ia64_va_page_base(va, ps);
    bool purged = false;
    uint16_t i;

    if (!ia64_merced_dtlb1_enabled(env)) {
        return false;
    }
    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        IA64TlbEntry *entry = &env->mmu.tlb_data_l1[i];

        if ((!tc_only || !entry->is_tr) &&
            ia64_tlb_entry_overlaps(entry, start, ps, rid)) {
            ia64_merced_dtlb1_invalidate_slot(env, i);
            purged = true;
        }
    }
    return purged;
}

static void ia64_qemu_tlb_flush_replaced_entry(CPUIA64State *env,
                                                IA64TlbEntry *entry,
                                                bool is_data)
{
    if (!is_data) {
        ia64_qemu_tlb_flush_entry(env, entry, false);
        return;
    }
    if (entry->pending_purge) {
        ia64_merced_dtlb1_invalidate_copy(env, entry);
    } else if (ia64_merced_dtlb1_contains(env, entry)) {
        return;
    }
    ia64_qemu_tlb_flush_entry(env, entry, true);
}

static int ia64_tlb_circular_tc_victim(const IA64TlbEntry *tlb,
                                       uint16_t capacity,
                                       uint16_t next_replace)
{
    uint16_t i = next_replace;
    uint16_t n;

    for (n = 0; n < capacity; n++) {
        if (tlb[i].valid && !tlb[i].is_tr) {
            return i;
        }
        if (++i == capacity) {
            i = 0;
        }
    }
    return -1;
}

static bool ia64_purge_tc_entries(CPUIA64State *env, IA64TlbEntry *tlb,
                                  uint16_t capacity, uint16_t *count,
                                  uint16_t *pending_count, uint64_t va,
                                  uint64_t ps, uint32_t rid, bool is_data,
                                  uint16_t *next_replace, int *insert_slot)
{
    int empty = -1;
    uint64_t start = ia64_va_page_base(va, ps);
    uint16_t i;
    bool purged = false;

    if (insert_slot) {
        *insert_slot = -1;
    }
    if (is_data) {
        purged |= ia64_merced_dtlb1_purge_range(env, start, ps, rid, true);
    }
    for (i = 0; i < *count; i++) {
        if (!tlb[i].valid) {
            if (insert_slot && empty < 0) {
                empty = i;
            }
            continue;
        }
        if (tlb[i].rid != rid || tlb[i].is_tr) {
            continue;
        }
        if (ia64_tlb_entry_overlaps(&tlb[i], start, ps, rid)) {
            ia64_qemu_tlb_flush_entry(env, &tlb[i], is_data);
            ia64_discard_pending_purge(&tlb[i], pending_count);
            tlb[i].valid = 0;
            ia64_tlb_bump_slot_generation(env, !is_data, i);
            if (insert_slot && empty < 0) {
                empty = i;
            }
            purged = true;
        }
    }

    while (*count > 0 && !tlb[*count - 1].valid) {
        (*count)--;
    }

    if (insert_slot) {
        if (empty < 0 && *count < capacity) {
            empty = *count;
        }
        *insert_slot = empty >= 0 ? empty :
            ia64_tlb_circular_tc_victim(tlb, capacity, *next_replace);
        if (*insert_slot >= 0) {
            uint16_t next = *insert_slot + 1;

            *next_replace = next == capacity ? 0 : next;
        }
    }

    return purged;
}

static bool ia64_mark_pending_purge_entries(IA64TlbEntry *tlb, uint16_t count,
                                            uint16_t *pending_count,
                                            uint64_t va, uint64_t ps,
                                            uint32_t rid, bool tc_only,
                                            char kind)
{
    uint64_t start = ia64_va_page_base(va, ps);
    uint16_t i;
    bool marked = false;

    for (i = 0; i < count; i++) {
        if ((!tc_only || !tlb[i].is_tr) &&
            ia64_tlb_entry_overlaps(&tlb[i], start, ps, rid)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 pending purge.%c slot=%u %s"
                          " va=0x%016" PRIx64 " rid=0x%06" PRIx32
                          " pa=0x%016" PRIx64 " ps=0x%016" PRIx64
                          " purge_va=0x%016" PRIx64
                          " purge_ps=0x%016" PRIx64 "\n",
                          kind, i, tlb[i].is_tr ? "TR" : "TC",
                          tlb[i].va, tlb[i].rid, tlb[i].pa, tlb[i].ps,
                          va, ps);
            if (!tlb[i].pending_purge) {
                tlb[i].pending_purge = 1;
                (*pending_count)++;
            }
            marked = true;
        }
    }

    return marked;
}

static bool ia64_mark_pending_purge_all_tc(IA64TlbEntry *tlb, uint16_t count,
                                           uint16_t *pending_count,
                                           char kind)
{
    uint16_t i;
    bool marked = false;

    for (i = 0; i < count; i++) {
        if (!tlb[i].is_tr && tlb[i].valid) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 pending purge.%c slot=%u TC"
                          " va=0x%016" PRIx64 " rid=0x%06" PRIx32
                          " pa=0x%016" PRIx64 " ps=0x%016" PRIx64
                          " purge=all-tc\n",
                          kind, i, tlb[i].va, tlb[i].rid,
                          tlb[i].pa, tlb[i].ps);
            if (!tlb[i].pending_purge) {
                tlb[i].pending_purge = 1;
                (*pending_count)++;
            }
            marked = true;
        }
    }

    return marked;
}

#define IA64_TLB_TARGETED_PURGE_LIMIT 8

#ifdef CONFIG_DEBUG_TCG
static uint16_t ia64_count_pending_purges(const IA64TlbEntry *tlb,
                                          uint16_t count)
{
    uint16_t pending = 0;
    uint16_t i;

    for (i = 0; i < count; i++) {
        pending += tlb[i].valid && tlb[i].pending_purge;
    }
    return pending;
}

static void ia64_assert_pending_purge_counts(CPUIA64State *env)
{
    g_assert(env->mmu.pending_purge_data_count ==
             ia64_count_pending_purges(env->mmu.tlb_data,
                                       env->mmu.tlb_data_count));
    g_assert(env->mmu.pending_purge_inst_count ==
             ia64_count_pending_purges(env->mmu.tlb_inst,
                                       env->mmu.tlb_inst_count));
}
#else
static inline void ia64_assert_pending_purge_counts(CPUIA64State *env)
{
    (void)env;
}
#endif

static void ia64_mark_pending_purge_all_tc_env(CPUIA64State *env)
{
    ia64_mark_pending_purge_all_tc(
        env->mmu.tlb_data, env->mmu.tlb_data_count,
        &env->mmu.pending_purge_data_count, 'd');
    if (ia64_merced_dtlb1_enabled(env)) {
        uint16_t i;

        for (i = 0; i < IA64_DTLB1_MAX; i++) {
            ia64_merced_dtlb1_invalidate_slot(env, i);
        }
    }
    ia64_mark_pending_purge_all_tc(
        env->mmu.tlb_inst, env->mmu.tlb_inst_count,
        &env->mmu.pending_purge_inst_count, 'i');
    ia64_assert_pending_purge_counts(env);
}

static bool ia64_complete_pending_purges(CPUIA64State *env,
                                         IA64TlbEntry *tlb, uint16_t *count,
                                         uint16_t *pending_count, char kind,
                                         bool targeted, bool is_ifetch)
{
    uint16_t i;
    bool purged = false;

    for (i = 0; i < *count; i++) {
        if (tlb[i].valid && tlb[i].pending_purge) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 complete purge.%c slot=%u %s"
                          " va=0x%016" PRIx64 " rid=0x%06" PRIx32
                          " pa=0x%016" PRIx64 " ps=0x%016" PRIx64 "\n",
                          kind, i, tlb[i].is_tr ? "TR" : "TC",
                          tlb[i].va, tlb[i].rid, tlb[i].pa, tlb[i].ps);
            if (!is_ifetch) {
                ia64_merced_dtlb1_invalidate_copy(env, &tlb[i]);
            }
            if (targeted) {
                ia64_qemu_tlb_flush_entry(env, &tlb[i], !is_ifetch);
            }
            tlb[i].pending_purge = 0;
            g_assert(*pending_count > 0);
            (*pending_count)--;
            tlb[i].valid = 0;
            ia64_tlb_bump_slot_generation(env, is_ifetch, i);
            purged = true;
        }
    }

    while (*count > 0 && !tlb[*count - 1].valid) {
        (*count)--;
    }

    return purged;
}

void ia64_tlb_serialize(CPUIA64State *env, uint32_t serialize_data,
                          uint32_t serialize_inst)
{
    bool data_purged = false;
    bool inst_purged = false;
    uint16_t pending = 0;
    bool targeted;

    if (serialize_data) {
        pending += env->mmu.pending_purge_data_count;
    }
    if (serialize_inst) {
        pending += env->mmu.pending_purge_inst_count;
    }
    targeted = pending <= IA64_TLB_TARGETED_PURGE_LIMIT;
    trace_ia64_mmu_serialize(env_cpu(env)->cpu_index, serialize_data,
                             serialize_inst, pending, targeted);

    if (serialize_data) {
        env->exception_state.psr_ic_inflight = false;
        env->exception_state.psr_i_deferred = false;
        if (env->mmu.pending_purge_data_count != 0) {
            data_purged = ia64_complete_pending_purges(
                env, env->mmu.tlb_data, &env->mmu.tlb_data_count,
                &env->mmu.pending_purge_data_count, 'd', targeted, false);
        }
    }
    if (serialize_inst && env->mmu.pending_purge_inst_count != 0) {
        inst_purged = ia64_complete_pending_purges(
            env, env->mmu.tlb_inst, &env->mmu.tlb_inst_count,
            &env->mmu.pending_purge_inst_count, 'i', targeted, true);
    }
    if (!targeted && (data_purged || inst_purged)) {
        tlb_flush(env_cpu(env));
    }
    ia64_assert_pending_purge_counts(env);

    /*
     * An instruction translation change does not change guest code.  The
     * softmmu flush also clears the virtual-PC jump cache, and the next
     * global TB lookup resolves the new physical page before matching a TB.
     * Keep the physical-page-keyed TBs so they can be reused when a mapping
     * becomes current again.
     */
}

static int ia64_tlb_select_tc_slot(IA64TlbEntry *tlb, uint16_t capacity,
                                   uint16_t *next_replace, uint64_t va,
                                   uint32_t rid)
{
    int empty = -1;
    int victim;
    uint16_t i;

    /* Prefer an empty slot, then use deterministic circular replacement. */
    for (i = 0; i < capacity; i++) {
        if (!tlb[i].valid) {
            if (empty < 0) {
                empty = i;
            }
            continue;
        }
        if (tlb[i].is_tr) {
            continue;
        }
        if (tlb[i].va == va && tlb[i].rid == rid) {
            return i;
        }
    }

    if (empty >= 0) {
        *next_replace = (empty + 1) % capacity;
        return empty;
    }

    victim = ia64_tlb_circular_tc_victim(tlb, capacity, *next_replace);
    if (victim >= 0) {
        /* Keep replacement moving forward over non-TR entries. */
        *next_replace = (victim + 1) % capacity;
        return victim;
    }

    return -1;
}

static bool ia64_cache_replaced_tr(CPUIA64State *env, IA64TlbEntry *tlb,
                                   uint16_t *cnt,
                                   uint16_t capacity,
                                   uint16_t *next_replace,
                                   uint16_t *pending_count,
                                   const IA64TlbEntry *old_tr,
                                   bool is_ifetch)
{
    uint32_t micro_generation;
    int slot;

    if (!old_tr->valid || !old_tr->is_tr) {
        return false;
    }

    /*
     * Replacing a TR slot does not purge the previous translation from the
     * processor TLBs.  Model that architected behavior as a TC copy, which
     * remains until normal TC replacement or an explicit ptr purge.
     */
    slot = ia64_tlb_select_tc_slot(tlb, capacity, next_replace,
                                   old_tr->va, old_tr->rid);
    if (slot < 0) {
        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 cache replaced tr failed va=0x%016" PRIx64
                      " rid=0x%06" PRIx32 " ps=0x%016" PRIx64 "\n",
                      old_tr->va, old_tr->rid, old_tr->ps);
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 cache replaced tr slot=%d va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                  " ps=0x%016" PRIx64 "\n",
                  slot, old_tr->va, old_tr->rid, old_tr->pa, old_tr->ps);
    ia64_discard_pending_purge(&tlb[slot], pending_count);
    micro_generation = tlb[slot].micro_generation;
    tlb[slot] = *old_tr;
    tlb[slot].micro_generation = micro_generation;
    tlb[slot].is_tr = 0;
    tlb[slot].slot = slot;
    ia64_tlb_bump_slot_generation(env, is_ifetch, slot);
    if (slot >= *cnt) {
        *cnt = slot + 1;
    }
    return true;
}

static void ia64_mmu_check_virtualization(CPUIA64State *env,
                                          uint64_t raw,
                                          uint32_t fault_slot)
{
    if (ia64_env_cpu_class(env)->has_virtualization &&
        (env->psr & IA64_PSR_VM)) {
        ia64_raise_exception(env, IA64_EXCP_VIRTUALIZATION,
                             ia64_ip_bundle_addr(env->ip), raw, fault_slot);
    }
}

void ia64_mmu_itr_insert(CPUIA64State *env, uint64_t pte, uint64_t slot_reg,
                       uint32_t is_data, uint64_t raw, uint32_t fault_slot)
{
    IA64TlbEntry *tlb;
    uint16_t *cnt;
    uint16_t *pending_count;
    uint64_t ps;
    uint64_t va;
    uint64_t pa;
    uint32_t key;
    uint8_t ar;
    uint8_t pl;
    uint8_t perm;
    uint32_t rid;
    uint32_t slot = slot_reg & 0xff;
    uint16_t capacity = ia64_cpu_tlb_capacity(env, is_data);
    uint16_t *next_replace;
    CPUState *cs = env_cpu(env);
    bool cached_old_tr;

    if (slot >= (is_data ? ia64_env_cpu_class(env)->dtr_count :
                          ia64_env_cpu_class(env)->itr_count)) {
        env->cr_isr = 0x30;
        ia64_raise_exception(env, IA64_EXCP_RESERVED_REG_FIELD,
                               ia64_ip_bundle_addr(env->ip), raw,
                               fault_slot);
        return;
    }
    if (!ia64_translation_insert_fields_valid(env, pte, env->cr_itir)) {
        env->cr_isr = 0x30;
        ia64_raise_exception(env, IA64_EXCP_RESERVED_REG_FIELD,
                               ia64_ip_bundle_addr(env->ip), raw,
                               fault_slot);
        return;
    }

    ps = ia64_itir_page_size(env);
    va = env->cr_ifa & ~(ps - 1);
    pa = (pte & IA64_PTE_PPN_MASK) & ~(ps - 1);
    key = (env->cr_itir & IA64_ITIR_KEY_MASK) >> IA64_ITIR_KEY_SHIFT;
    ar = ia64_pte_ar(pte);
    pl = ia64_pte_pl(pte);
    perm = ia64_pte_perm(pte, 0);
    rid = ia64_region_rid(env, env->cr_ifa);

    if (!ia64_va_is_implemented(env, env->cr_ifa)) {
        ia64_raise_unimplemented_data_address(
            env, env->cr_ifa, 0, true, false, ia64_current_code_tlb_ed(env));
    }
    ia64_mmu_check_virtualization(env, raw, fault_slot);

    if ((pte & IA64_PTE_PRESENT) && perm == 0) {
        return;
    }

    if (is_data) {
        tlb = env->mmu.tlb_data;
        cnt = &env->mmu.tlb_data_count;
        next_replace = &env->mmu.tlb_data_replace;
        pending_count = &env->mmu.pending_purge_data_count;
    } else {
        tlb = env->mmu.tlb_inst;
        cnt = &env->mmu.tlb_inst_count;
        next_replace = &env->mmu.tlb_inst_replace;
        pending_count = &env->mmu.pending_purge_inst_count;
    }

    IA64TlbEntry old_tr = tlb[slot];

    ia64_purge_tc_entries(env, tlb, capacity, cnt, pending_count, va, ps, rid,
                          is_data, NULL, NULL);
    if (old_tr.valid && !old_tr.is_tr) {
        ia64_qemu_tlb_flush_entry(env, &old_tr, is_data);
    }
    cached_old_tr = ia64_cache_replaced_tr(
        env, tlb, cnt, capacity, next_replace, pending_count, &old_tr,
        !is_data);
    if (is_data) {
        ia64_merced_dtlb1_invalidate_copy(env, &old_tr);
    }
    if (!cached_old_tr) {
        ia64_discard_pending_purge(&tlb[slot], pending_count);
    }

    tlb[slot].va = va;
    tlb[slot].pa = pa;
    tlb[slot].ps = ps;
    tlb[slot].page_mask = ia64_va_vpn_mask(ps);
    tlb[slot].pte = pte;
    tlb[slot].perm = perm;
    tlb[slot].ar = ar;
    tlb[slot].pl = pl;
    tlb[slot].valid = 1;
    tlb[slot].is_tr = 1;
    tlb[slot].pending_purge = 0;
    tlb[slot].rid = rid;
    tlb[slot].key = key;
    tlb[slot].slot = slot;
    if (slot >= *cnt) {
        *cnt = slot + 1;
    }
    ia64_tlb_bump_slot_generation(env, !is_data, slot);
    ia64_assert_pending_purge_counts(env);
    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 itr.%c slot=%u va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                  " ps=0x%016" PRIx64 " pte=0x%016" PRIx64 "\n",
                  is_data ? 'd' : 'i', slot, va, rid, pa, ps, pte);
    tlb_flush(cs);
}

void ia64_mmu_ptr_purge(CPUIA64State *env, uint64_t ifa, uint64_t size_reg,
                      uint32_t is_data, uint64_t raw, uint32_t fault_slot)
{
    IA64TlbEntry *tlb;
    uint64_t ps = ia64_gr_page_size(size_reg);
    uint64_t va = ifa & ~(ps - 1);
    uint32_t rid = ia64_region_rid(env, ifa);
    uint16_t count;

    if (!ia64_va_is_implemented(env, ifa)) {
        ia64_raise_unimplemented_data_address(
            env, ifa, 0, true, false, ia64_current_code_tlb_ed(env));
    }
    ia64_mmu_check_virtualization(env, raw, fault_slot);

    if (is_data) {
        tlb = env->mmu.tlb_data;
        count = env->mmu.tlb_data_count;
    } else {
        tlb = env->mmu.tlb_inst;
        count = env->mmu.tlb_inst_count;
    }

    trace_ia64_mmu_purge(env_cpu(env)->cpu_index,
                         is_data ? "ptr.d" : "ptr.i", va, ps, rid,
                         is_data);
    ia64_mark_pending_purge_entries(
        tlb, count,
        is_data ? &env->mmu.pending_purge_data_count :
                  &env->mmu.pending_purge_inst_count,
        va, ps, rid, false, is_data ? 'd' : 'i');
    if (is_data) {
        ia64_merced_dtlb1_purge_range(env, va, ps, rid, false);
    }
    ia64_assert_pending_purge_counts(env);
}

typedef struct IA64PtcGlobalWork {
    uint64_t va;
    uint64_t ps;
    uint32_t rid;
    bool global_alat;
} IA64PtcGlobalWork;

static void ia64_ptc_mark_global(CPUIA64State *env,
                                 const IA64PtcGlobalWork *work,
                                 bool complete)
{
    ia64_merced_dtlb1_purge_range(env, work->va, work->ps,
                                  work->rid, true);
    ia64_mark_pending_purge_entries(
        env->mmu.tlb_data, env->mmu.tlb_data_count,
        &env->mmu.pending_purge_data_count,
        work->va, work->ps, work->rid, true, 'd');
    ia64_mark_pending_purge_entries(
        env->mmu.tlb_inst, env->mmu.tlb_inst_count,
        &env->mmu.pending_purge_inst_count,
        work->va, work->ps, work->rid, true, 'i');
    ia64_assert_pending_purge_counts(env);

    if (complete) {
        /* The remote processor must not execute through the old mapping. */
        ia64_tlb_serialize(env, 1, 1);
    }
    if (work->global_alat) {
        memset(env->alat_state.alat, 0, sizeof(env->alat_state.alat));
        env->alat_state.alat_active_count = 0;
    }
}

static void ia64_ptc_global_remote_work(CPUState *cs, run_on_cpu_data data)
{
    IA64PtcGlobalWork *work = data.host_ptr;

    ia64_ptc_mark_global(cpu_env(cs), work, true);
    g_free(work);
}

static void ia64_ptc_global_source_barrier(CPUState *cs,
                                           run_on_cpu_data data)
{
    /* The exclusive work item is the synchronization point itself. */
    (void)cs;
    (void)data;
}

void ia64_mmu_ptc_purge(CPUIA64State *env, uint64_t va, uint64_t size_reg,
                      uint32_t mode, uint64_t raw, uint32_t fault_slot)
{
    uint32_t rid = ia64_region_rid(env, va);
    uint64_t ps = ia64_gr_page_size(size_reg);

    if (mode != 2 && !ia64_va_is_implemented(env, va)) {
        ia64_raise_unimplemented_data_address(
            env, va, 0, true, false, ia64_current_code_tlb_ed(env));
    }
    ia64_mmu_check_virtualization(env, raw, fault_slot);

    trace_ia64_mmu_purge(env_cpu(env)->cpu_index, "ptc", va, ps, rid, mode);
    if (mode == 1 || mode == 3) {
        CPUState *src = env_cpu(env);
        CPUState *cs;
        IA64PtcGlobalWork template = {
            .va = va & ~(ps - 1),
            .ps = ps,
            .rid = rid,
            .global_alat = mode == 3,
        };
        bool wait = false;

        /*
         * Mark the issuing processor before returning from the helper.
         * A stop after ptc.g/ptc.ga may place srlz in a later instruction
         * group of the same bundle, before queued CPU work is processed.
         */
        ia64_ptc_mark_global(env, &template, false);

        CPU_FOREACH(cs) {
            if (cs != src) {
                IA64PtcGlobalWork *work = g_new(IA64PtcGlobalWork, 1);

                *work = template;
                async_run_on_cpu(cs, ia64_ptc_global_remote_work,
                                 RUN_ON_CPU_HOST_PTR(work));
                wait = true;
            }
        }
        if (wait) {
            async_safe_run_on_cpu(src, ia64_ptc_global_source_barrier,
                                  RUN_ON_CPU_NULL);
        }
    } else if (mode == 2) {
        /*
         * ptc.e is architecturally local.  Some dual-thread silicon could
         * accidentally over-purge translation resources belonging to the
         * other logical processor, but its specification update explicitly
         * describes that as an erratum and says the purge should not
         * propagate.  Keep each vCPU's translation caches independent rather
         * than reproducing an incidental sibling purge.
         */
        ia64_mark_pending_purge_all_tc_env(env);
    } else {
        ia64_merced_dtlb1_purge_range(env, va, ps, rid, true);
        ia64_mark_pending_purge_entries(
            env->mmu.tlb_data, env->mmu.tlb_data_count,
            &env->mmu.pending_purge_data_count, va, ps, rid, true, 'd');
        ia64_mark_pending_purge_entries(
            env->mmu.tlb_inst, env->mmu.tlb_inst_count,
            &env->mmu.pending_purge_inst_count, va, ps, rid, true, 'i');
    }
    ia64_assert_pending_purge_counts(env);
}

void ia64_mmu_invalidate_tc(CPUIA64State *env)
{
    bool psr_ic_inflight = env->exception_state.psr_ic_inflight;

    ia64_mark_pending_purge_all_tc_env(env);
    ia64_tlb_serialize(env, 1, 1);
    /* TC replacement is not an instruction/data serialization event. */
    env->exception_state.psr_ic_inflight = psr_ic_inflight;
}

uint64_t ia64_mmu_tpa(CPUIA64State *env, uint64_t va)
{
    uint64_t pa;
    IA64Exception excp;

    excp = ia64_translate_nonaccess(env, va, IA64_NON_ACCESS_TPA, &pa);
    if (excp != IA64_EXCP_NONE) {
        ia64_raise_nonaccess_exception(env, va, IA64_NON_ACCESS_TPA, excp);
    }
    return pa;
}

static uint64_t ia64_probe_address(CPUIA64State *env, uint64_t va,
                                   uint32_t is_write, uint32_t is_ifetch,
                                   uint8_t access_level)
{
    uint64_t pa;
    uint8_t perm;
    uint32_t rid = ia64_region_rid(env, va);
    uint8_t needed;
    const IA64TlbEntry *entry;

    needed = is_write ? IA64_TLB_W :
             (is_ifetch ? IA64_TLB_X : IA64_TLB_R);

    if (!(env->psr & (is_ifetch ? IA64_PSR_IT : IA64_PSR_DT))) {
        return 1;
    }

    if (!ia64_va_is_implemented(env, va)) {
        return 0;
    }

    if (ia64_firmware_identity_pa(env->cr_iva, env->psr, va,
                                  &pa)) {
        return 1;
    }

    entry = ia64_tlb_find_cached(env, va, rid, is_ifetch);
    if (entry) {
        IA64Exception excp;

        ia64_tlb_entry_translate(entry, va, access_level, &pa, &perm);
        excp = ia64_tlb_exception_for_access(env, entry, perm, needed,
                                             is_ifetch, is_write, false);
        return excp == IA64_EXCP_NONE ? 1 : 0;
    }

    return 0;
}

typedef struct IA64DataReferenceResult {
    uint64_t pa;
    IA64MemorySpeculation speculation;
    uint8_t memory_attribute;
    bool valid;
} IA64DataReferenceResult;

static void ia64_set_data_reference_result(IA64DataReferenceResult *result,
                                           uint64_t pa,
                                           IA64MemorySpeculation speculation,
                                           uint8_t memory_attribute)
{
    if (result) {
        result->pa = pa;
        result->speculation = speculation;
        result->memory_attribute = memory_attribute;
        result->valid = true;
    }
}

static bool ia64_debug_plm_match(uint64_t control, uint8_t access_level)
{
    return control & (1ULL << (56 + (access_level & 3)));
}

static bool ia64_debug_address_match(uint64_t reference, uint64_t address,
                                     uint64_t mask)
{
    return (reference & mask) == (address & mask);
}

static bool ia64_instruction_breakpoint_match(CPUIA64State *env,
                                              uint64_t address,
                                              uint8_t access_level)
{
    const uint64_t fixed_mask = UINT64_C(0xff00000000000000);
    unsigned pair;

    for (pair = 0; pair < IA64_IBR_IMPLEMENTED_COUNT / 2; pair++) {
        uint64_t breakpoint = env->ibr[pair * 2];
        uint64_t control = env->ibr[pair * 2 + 1];
        uint64_t mask = fixed_mask |
                        (control & UINT64_C(0x00ffffffffffffff));

        /* Itanium instruction breakpoints describe whole 16-byte bundles. */
        mask &= ~UINT64_C(0xf);
        if ((control & (1ULL << 63)) &&
            ia64_debug_plm_match(control, access_level) &&
            ia64_debug_address_match(address, breakpoint, mask)) {
            return true;
        }
    }
    return false;
}

static bool ia64_data_breakpoint_control_enabled(uint64_t control,
                                                 uint64_t isr_access,
                                                 uint8_t access_level)
{
    bool access_match =
        ((isr_access & IA64_ISR_R) && (control & (1ULL << 63))) ||
        ((isr_access & IA64_ISR_W) && (control & (1ULL << 62)));

    return access_match && ia64_debug_plm_match(control, access_level);
}

static bool ia64_data_breakpoint_register_match(CPUIA64State *env,
                                                uint64_t address,
                                                uint32_t size,
                                                uint64_t isr_access,
                                                uint8_t access_level)
{
    const uint64_t fixed_mask = UINT64_C(0xff00000000000000);
    unsigned pair;

    for (pair = 0; pair < IA64_DBR_IMPLEMENTED_COUNT / 2; pair++) {
        uint64_t breakpoint = env->dbr[pair * 2];
        uint64_t control = env->dbr[pair * 2 + 1];
        uint64_t mask = fixed_mask |
                        (control & UINT64_C(0x00ffffffffffffff));
        uint32_t byte;

        if (!ia64_data_breakpoint_control_enabled(
                control, isr_access, access_level)) {
            continue;
        }
        for (byte = 0; byte < size; byte++) {
            if (ia64_debug_address_match(address + byte, breakpoint, mask)) {
                return true;
            }
        }
    }
    return false;
}

static bool ia64_data_breakpoint_enabled(CPUIA64State *env,
                                         uint64_t isr_access,
                                         uint8_t access_level)
{
    unsigned pair;

    for (pair = 0; pair < IA64_DBR_IMPLEMENTED_COUNT / 2; pair++) {
        if (ia64_data_breakpoint_control_enabled(
                env->dbr[pair * 2 + 1], isr_access, access_level)) {
            return true;
        }
    }
    return false;
}

static bool ia64_data_breakpoint_match(CPUIA64State *env,
                                       uint64_t address, uint32_t size,
                                       uint64_t isr_access,
                                       uint8_t access_level)
{
    /*
     * The model-specific cross-boundary condition ignores DBR addresses,
     * but still requires an enabled access type and privilege level.
     */
    if (ia64_env_cpu_class(env)->data_debug_cross_16byte &&
        ia64_data_breakpoint_enabled(env, isr_access, access_level) &&
        (address & 0xf) + size > 16) {
        return true;
    }

    return ia64_data_breakpoint_register_match(
        env, address, size, isr_access, access_level);
}

static bool ia64_vhpt_data_breakpoint_match(CPUIA64State *env,
                                            uint64_t address, uint32_t size,
                                            bool tak_access)
{
    /*
     * A programmed DBR.r match on a VHPT reference aborts the walk to the
     * original Instruction/Data TLB Miss.  The walker references at PL0;
     * tak is the architectural exception.  This is deliberately the exact
     * programmed-register match, not the Madison unaligned-data extension.
     */
    return !tak_access && (env->psr & IA64_PSR_DB) &&
           !(env->psr & IA64_PSR_DD) &&
           ia64_data_breakpoint_register_match(
               env, address, size, IA64_ISR_R, 0);
}

void ia64_check_instruction_debug(CPUIA64State *env, uint64_t address,
                                  uint8_t slot)
{
    if (!(env->psr & IA64_PSR_DB) || (env->psr & IA64_PSR_ID) ||
        !ia64_instruction_breakpoint_match(
            env, address, ia64_psr_cpl(env->psr))) {
        return;
    }

    if (env->psr & IA64_PSR_IC) {
        env->cr_ifa = address;
    }
    env->cr_isr = IA64_ISR_X;
    ia64_raise_exception(env, IA64_EXCP_DEBUG, address, 0, slot);
}

static IA64Exception
ia64_data_reference_exception(CPUIA64State *env, uint64_t va,
                              uint32_t is_write, uint32_t is_rw,
                              uint8_t access_level, bool walk_vhpt,
                              bool is_rse,
                              IA64DataReferenceResult *result)
{
    uint64_t pa;
    uint8_t perm;
    uint8_t needed = is_rw ? (IA64_TLB_R | IA64_TLB_W) :
                     (is_write ? IA64_TLB_W : IA64_TLB_R);
    uint32_t rid = ia64_region_rid(env, va);
    uint8_t vhpt_size;
    bool vhpt_long_format;
    bool vhpt_enabled;
    uint64_t pte = IA64_PTE_PRESENT;
    uint32_t key = 0;
    const IA64TlbEntry *entry;
    bool found = false;

    if (result) {
        result->valid = false;
    }
    if (!(env->psr & (is_rse ? IA64_PSR_RT : IA64_PSR_DT))) {
        if (!ia64_pa_is_implemented(env, va)) {
            return IA64_EXCP_UNIMPL_DATA_ADDR;
        }
        pa = ia64_physical_address(va);
        ia64_set_data_reference_result(
            result, pa, (va & IA64_PHYS_UC_BIT) ?
                        IA64_MEM_NON_SPECULATIVE :
                        IA64_MEM_LIMITED_SPECULATION,
            (va & IA64_PHYS_UC_BIT) ? IA64_PTE_MA_UC : IA64_PTE_MA_WB);
        return IA64_EXCP_NONE;
    }
    if (ia64_firmware_identity_pa(env->cr_iva, env->psr, va,
                                  &pa)) {
        ia64_set_data_reference_result(result, pa, IA64_MEM_SPECULATIVE,
                                       IA64_PTE_MA_WB);
        return IA64_EXCP_NONE;
    }

    if (!ia64_va_is_implemented(env, va)) {
        return IA64_EXCP_UNIMPL_DATA_ADDR;
    }

    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (entry) {
        ia64_tlb_entry_translate(entry, va, access_level, &pa, &perm);
        pte = entry->pte;
        found = true;
    } else if (walk_vhpt) {
        found = ia64_vhpt_walk_full(env, va, rid, false, is_rse,
                                    access_level, &pa, &perm, &pte, &key,
                                    &entry);
    }

    if (found) {
        uint64_t resolved_pte = entry ? entry->pte : pte;
        uint8_t ma = (resolved_pte & IA64_PTE_MA_MASK) >> IA64_PTE_MA_SHIFT;

        ia64_set_data_reference_result(
            result, pa, ia64_pte_memory_speculation(resolved_pte), ma);

        if (entry) {
            return ia64_tlb_exception_for_access(env, entry, perm, needed,
                                                false, is_write || is_rw,
                                                is_rse);
        }
        return ia64_translation_exception_for_access(env, pte, key, perm,
                                                     needed, false,
                                                     is_write || is_rw,
                                                     is_rse);
    }
    if (ia64_data_nested_tlb_active(env)) {
        return IA64_EXCP_DATA_NESTED_TLB;
    }
    if (ia64_vhpt_pte_not_present(env, va, false, is_rse, NULL)) {
        return IA64_EXCP_PAGE_NOT_PRESENT;
    }
    vhpt_enabled = ia64_vhpt_walker_enabled(env, va, false, is_rse,
                                            &vhpt_size, &vhpt_long_format);
    if (!ia64_vhpt_entry_accessible(env, va, false, is_rse, &pa)) {
        return IA64_EXCP_VHPT_FAULT;
    }

    return vhpt_enabled ? IA64_EXCP_DTLB_FAULT : IA64_EXCP_ALT_DTLB;
}

bool ia64_translate_data_access(CPUIA64State *env, uint64_t va,
                                bool is_write, uint64_t *pa)
{
    IA64DataReferenceResult result;
    IA64Exception excp;

    if (env == NULL || pa == NULL) {
        return false;
    }
    excp = ia64_data_reference_exception(
        env, va, is_write, false, ia64_psr_cpl(env->psr), true, false,
        &result);
    if (excp != IA64_EXCP_NONE || !result.valid) {
        return false;
    }
    *pa = result.pa;
    return true;
}

static uint64_t ia64_speculative_deferral_dcr_mask(IA64Exception excp)
{
    switch (excp) {
    case IA64_EXCP_ALT_DTLB:
    case IA64_EXCP_VHPT_FAULT:
    case IA64_EXCP_DTLB_FAULT:
        return IA64_DCR_DM;
    case IA64_EXCP_PAGE_NOT_PRESENT:
        return IA64_DCR_DP;
    case IA64_EXCP_DATA_KEY_MISS:
        return IA64_DCR_DK;
    case IA64_EXCP_KEY_PERMISSION:
        return IA64_DCR_DX;
    case IA64_EXCP_DATA_ACCESS:
        return IA64_DCR_DR;
    case IA64_EXCP_DATA_ACCESS_BIT:
        return IA64_DCR_DA;
    case IA64_EXCP_DEBUG:
        return IA64_DCR_DD;
    case IA64_EXCP_UNIMPL_DATA_ADDR:
        return UINT64_MAX;
    default:
        return 0;
    }
}

static bool ia64_speculative_exception_deferrable(CPUIA64State *env,
                                                  IA64Exception excp,
                                                  bool itlb_ed)
{
    uint64_t dcr_mask;

    if (!(env->psr & IA64_PSR_IC)) {
        return true;
    }

    if (excp == IA64_EXCP_NAT_CONSUMPTION) {
        return true;
    }

    if (excp == IA64_EXCP_UNALIGNED) {
        return (env->psr & IA64_PSR_IT) && itlb_ed;
    }

    dcr_mask = ia64_speculative_deferral_dcr_mask(excp);
    if (dcr_mask == UINT64_MAX) {
        return true;
    }

    return dcr_mask != 0 &&
           (env->psr & IA64_PSR_IT) &&
           itlb_ed &&
           (env->cr_dcr & dcr_mask);
}

static bool ia64_crosses_alignment_window(uint64_t va, uint32_t size,
                                          uint32_t window)
{
    return size > 1 && (va & (window - 1U)) + size - 1U >= window;
}

static bool ia64_alignment_memory_attribute_requires_fault(
    uint8_t memory_attribute)
{
    return memory_attribute == IA64_PTE_MA_UC ||
           memory_attribute == IA64_PTE_MA_UCE ||
           memory_attribute == IA64_PTE_MA_WC;
}

static bool ia64_alignment_fault(CPUIA64State *env, uint64_t va,
                                 uint32_t alignment_info,
                                 bool is_write,
                                 const IA64DataReferenceResult *translation)
{
    const IA64CPUClass *icc;
    uint32_t size = alignment_info & IA64_ALIGNMENT_DATUM_MASK;
    uint32_t natural = (alignment_info & IA64_ALIGNMENT_NATURAL_MASK) >>
                       IA64_ALIGNMENT_NATURAL_SHIFT;
    IA64AlignmentClass alignment_class =
        (alignment_info & IA64_ALIGNMENT_CLASS_MASK) >>
        IA64_ALIGNMENT_CLASS_SHIFT;
    bool naturally_aligned;

    if (size <= 1 || natural <= 1) {
        return false;
    }
    naturally_aligned = (va & (natural - 1U)) == 0;
    if (naturally_aligned) {
        return false;
    }
    icc = ia64_env_cpu_class(env);

    if (!naturally_aligned &&
        (alignment_class == IA64_ALIGNMENT_NATURAL_REQUIRED ||
         (env->psr & IA64_PSR_AC))) {
        return true;
    }

    switch (naturally_aligned ? IA64_ALIGNMENT_GENERIC : alignment_class) {
    case IA64_ALIGNMENT_INTEGER:
        /*
         * Merced integer references span one 16-byte block.  Madison
         * narrows that to 8.  Montecito loads retain the 8-byte window,
         * while its WB stores may span a 16-byte block.
         */
        if (ia64_crosses_alignment_window(
                va, size,
                icc->model == IA64_CPU_MODEL_MERCED ||
                (icc->model == IA64_CPU_MODEL_MONTECITO && is_write) ?
                16U : 8U)) {
            return true;
        }
        break;
    case IA64_ALIGNMENT_FP:
        if (icc->model == IA64_CPU_MODEL_MADISON &&
            ia64_crosses_alignment_window(va, size, 16U)) {
            return true;
        }
        if (icc->model == IA64_CPU_MODEL_MONTECITO &&
            ia64_crosses_alignment_window(va, size,
                                          is_write ? 16U : 8U)) {
            return true;
        }
        break;
    case IA64_ALIGNMENT_FP_PAIR:
    case IA64_ALIGNMENT_FP_FILL_SPILL:
        if (icc->model != IA64_CPU_MODEL_MERCED) {
            return true;
        }
        break;
    case IA64_ALIGNMENT_GENERIC:
    case IA64_ALIGNMENT_NATURAL_REQUIRED:
        break;
    default:
        /* A malformed internal descriptor must fail safely. */
        return true;
    }

    /*
     * Itanium 2 UC/UCE/WC references may not cross an 8-byte boundary,
     * even for an unaligned FP operation that a WB page permits within 16
     * bytes.  An architecturally aligned reference must never raise an
     * Unaligned Data Reference fault; opcode-specific memory-attribute
     * restrictions are reported separately as Unsupported Data Reference.
     */
    if (!naturally_aligned &&
        alignment_class != IA64_ALIGNMENT_NATURAL_REQUIRED &&
        icc->model != IA64_CPU_MODEL_MERCED && translation &&
        translation->valid && ia64_crosses_alignment_window(va, size, 8U) &&
        ia64_alignment_memory_attribute_requires_fault(
            translation->memory_attribute)) {
        return true;
    }

    return !naturally_aligned &&
           ia64_crosses_alignment_window(va, size, 0x1000U);
}

static void ia64_raise_data_reference_exception_at(CPUIA64State *env,
                                                   uint64_t va,
                                                   uint32_t is_write,
                                                   uint32_t is_rw,
                                                   bool is_non_access,
                                                   uint8_t non_access_code,
                                                   IA64Exception excp,
                                                   bool is_speculative,
                                                   bool itlb_ed,
                                                   bool is_rse,
                                                   uint64_t fault_ip,
                                                   uint8_t fault_slot)
{
    CPUState *cs = env_cpu(env);

    env->exception_state.fault_addr = va;
    if (env->psr & IA64_PSR_IC) {
        env->cr_ifa = va;
        if (ia64_exception_initializes_iha(excp)) {
            env->cr_iha = ia64_vhpt_hash_address(env, va);
        }
        env->cr_itir = ia64_region_itir(
            env, excp == IA64_EXCP_VHPT_FAULT ? env->cr_iha : va);
    }
    if (excp != IA64_EXCP_DATA_NESTED_TLB) {
        if (excp == IA64_EXCP_UNIMPL_DATA_ADDR) {
            env->cr_isr = IA64_GENEX_UNIMPL_DATA_ADDR |
                          (is_non_access ? IA64_ISR_NA : 0) |
                          (is_non_access ? non_access_code : 0) |
                          (is_rw ? (IA64_ISR_R | IA64_ISR_W) :
                           (is_write ? IA64_ISR_W : IA64_ISR_R));
        } else {
            env->cr_isr = (is_non_access ?
                           IA64_ISR_NA | non_access_code : 0) |
                          (is_rw ? (IA64_ISR_R | IA64_ISR_W) :
                           (is_write ? IA64_ISR_W : IA64_ISR_R));
            if (excp == IA64_EXCP_NAT_CONSUMPTION) {
                /*
                 * Data NaT Page Consumption reports ISR.code{5:4} = 2 above
                 * the non-access instruction code in ISR.code{3:0}, which is
                 * zero for an ordinary access.
                 */
                env->cr_isr |= IA64_ISR_CODE_NAT_PAGE;
            }
        }
        /* Data NaT Page Consumption always reports ISR.sp and ISR.ed as 0. */
        if (is_speculative && excp != IA64_EXCP_NAT_CONSUMPTION) {
            env->cr_isr |= IA64_ISR_SP;
        }
        if (is_rse) {
            env->cr_isr |= IA64_ISR_RS;
            if (env->rse.rse_dirty < 0 || env->rse.rse_dirty_nat < 0) {
                env->cr_isr |= IA64_ISR_IR;
            }
        } else if (itlb_ed && excp != IA64_EXCP_NAT_CONSUMPTION) {
            env->cr_isr |= IA64_ISR_ED;
        }
    }
    env->exception_state.fault_ip = fault_ip;
    env->exception_state.fault_imm = 0;
    env->exception_state.fault_slot = fault_slot;
    env->exception_state.exception = excp;
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

static void ia64_raise_data_reference_exception(CPUIA64State *env,
                                                uint64_t va,
                                                uint32_t is_write,
                                                uint32_t is_rw,
                                                bool is_non_access,
                                                uint8_t non_access_code,
                                                IA64Exception excp,
                                                bool is_speculative,
                                                bool itlb_ed)
{
    ia64_raise_data_reference_exception_at(
        env, va, is_write, is_rw, is_non_access, non_access_code, excp,
        is_speculative, itlb_ed, false, ia64_ip_bundle_addr(env->ip),
        (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT);
}

static G_NORETURN void
ia64_raise_data_debug_at(CPUIA64State *env, uint64_t va,
                         uint64_t isr_access, bool is_speculative,
                         bool itlb_ed, bool mandatory_rse,
                         uint64_t fault_ip, uint8_t fault_slot)
{
    if (env->psr & IA64_PSR_IC) {
        env->cr_ifa = va;
    }
    env->cr_isr = isr_access;
    if (is_speculative) {
        env->cr_isr |= IA64_ISR_SP;
    }
    if (mandatory_rse) {
        env->cr_isr |= IA64_ISR_RS;
        if (env->rse.rse_dirty < 0 || env->rse.rse_dirty_nat < 0) {
            env->cr_isr |= IA64_ISR_IR;
        }
    } else if (itlb_ed) {
        env->cr_isr |= IA64_ISR_ED;
    }
    ia64_raise_exception(env, IA64_EXCP_DEBUG, fault_ip, 0, fault_slot);
}

void ia64_mmu_check_data_debug(CPUIA64State *env, uint64_t va,
                               uint32_t size, uint64_t isr_access,
                               uint8_t access_level, bool mandatory_rse,
                               uint64_t fault_ip, uint8_t fault_slot)
{
    bool is_write;
    bool is_rw;
    IA64Exception excp;

    if (!(env->psr & IA64_PSR_DB) || (env->psr & IA64_PSR_DD) ||
        !ia64_data_breakpoint_match(env, va, size, isr_access,
                                    access_level)) {
        return;
    }

    is_write = isr_access & IA64_ISR_W;
    is_rw = (isr_access & (IA64_ISR_R | IA64_ISR_W)) ==
            (IA64_ISR_R | IA64_ISR_W);
    excp = ia64_data_reference_exception(
        env, va, is_write, is_rw, access_level, true, mandatory_rse, NULL);
    if (excp != IA64_EXCP_NONE) {
        ia64_raise_data_reference_exception_at(
            env, va, is_write, is_rw, isr_access & IA64_ISR_NA,
            isr_access & IA64_ISR_CODE_MASK, excp, false,
            ia64_current_code_tlb_ed(env), mandatory_rse,
            fault_ip, fault_slot);
    }

    ia64_raise_data_debug_at(env, va, isr_access, false,
                             ia64_current_code_tlb_ed(env), mandatory_rse,
                             fault_ip, fault_slot);
}

static uint8_t ia64_probe_access_level(CPUIA64State *env,
                                       uint64_t access_level)
{
    uint8_t requested_pl = access_level & 3;
    uint8_t current_cpl = ia64_psr_cpl(env->psr);

    return requested_pl < current_cpl ? current_cpl : requested_pl;
}

/*
 * Map a translation-check result to the architected result of the
 * non-faulting probe forms.  tlb_grant_permission() takes VHPT
 * Translation, TLB Miss, Nested TLB, Page Not Present, NaT Page
 * Consumption and Key Miss faults exactly like a normal reference;
 * permission failures and unimplemented addresses return 0; maintenance
 * conditions (access/dirty bit) do not gate the granted access.
 */
static uint64_t ia64_probe_grant(CPUIA64State *env, uint64_t va,
                                 uint32_t is_write, IA64Exception excp)
{
    switch (excp) {
    case IA64_EXCP_NONE:
    case IA64_EXCP_DATA_DIRTY:
    case IA64_EXCP_DATA_ACCESS_BIT:
        return 1;
    case IA64_EXCP_UNIMPL_DATA_ADDR:
    case IA64_EXCP_DATA_ACCESS:
    case IA64_EXCP_KEY_PERMISSION:
        return 0;
    default:
        ia64_raise_data_reference_exception(
            env, va, is_write, false, true, 2, excp, false,
            ia64_current_code_tlb_ed(env));
        g_assert_not_reached();
    }
}

static uint64_t ia64_probe_dt_disabled(CPUIA64State *env, uint64_t va,
                                       uint32_t is_write,
                                       uint8_t access_level)
{
    uint64_t pa;
    uint8_t perm;
    uint8_t needed = is_write ? IA64_TLB_W : IA64_TLB_R;
    uint32_t rid = ia64_region_rid(env, va);
    const IA64TlbEntry *entry;
    IA64Exception excp;

    if (!ia64_va_is_implemented(env, va)) {
        return 0;
    }

    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (!entry) {
        /*
         * A data-translation miss taken with PSR.ic clear reports the Data
         * Nested TLB vector in place of the Alternate Data TLB vector, the
         * same conversion the ordinary fill path performs.
         */
        IA64Exception miss_excp = ia64_data_nested_tlb_active(env) ?
                                  IA64_EXCP_DATA_NESTED_TLB :
                                  IA64_EXCP_ALT_DTLB;

        ia64_raise_data_reference_exception(
            env, va, is_write, false, true, 2, miss_excp, false,
            ia64_current_code_tlb_ed(env));
        g_assert_not_reached();
    }

    /*
     * The dt=0 probe queries the DTLB with a virtual address, so the
     * checked conditions keep their architected order: present, NaTPage,
     * protection key (gated by PSR.pk alone), then access rights.
     */
    ia64_tlb_entry_translate(entry, va, access_level, &pa, &perm);
    if (!(entry->pte & IA64_PTE_PRESENT)) {
        excp = IA64_EXCP_PAGE_NOT_PRESENT;
    } else if (ia64_pte_ma(entry->pte) == IA64_PTE_MA_NATPAGE) {
        excp = IA64_EXCP_NAT_CONSUMPTION;
    } else {
        excp = IA64_EXCP_NONE;
        if (env->psr & IA64_PSR_PK) {
            excp = ia64_key_exception_for_key(env, entry->key, needed,
                                              false);
        }
        if (excp == IA64_EXCP_NONE) {
            excp = ia64_pte_exception_for_access(entry->pte, perm, needed,
                                                 false, is_write, env->psr);
        }
    }
    return ia64_probe_grant(env, va, is_write, excp);
}

/*
 * Only the non-faulting probe.r/probe.w forms reach here; probe.rw is
 * encoded solely as probe.rw.fault and goes through ia64_mmu_probe_fault().
 */
uint64_t ia64_mmu_probe(CPUIA64State *env, uint64_t va, uint32_t is_write,
                      uint64_t access_level)
{
    uint8_t effective_pl = ia64_probe_access_level(env, access_level);
    IA64Exception excp;

    if (!(env->psr & IA64_PSR_DT)) {
        return ia64_probe_dt_disabled(env, va, is_write, effective_pl);
    }

    excp = ia64_data_reference_exception(env, va, is_write, false,
                                         effective_pl, true, false, NULL);
    return ia64_probe_grant(env, va, is_write, excp);
}

static void ia64_raise_data_reference_fault_if_needed(CPUIA64State *env,
                                                      uint64_t va,
                                                      uint32_t is_write,
                                                      uint32_t is_rw,
                                                      uint8_t access_level,
                                                      bool is_non_access,
                                                      uint8_t non_access_code)
{
    IA64Exception excp = ia64_data_reference_exception(
        env, va, is_write, is_rw, access_level, true, false, NULL);

    if (excp == IA64_EXCP_NONE) {
        return;
    }
    ia64_raise_data_reference_exception(env, va, is_write, is_rw,
                                        is_non_access, non_access_code,
                                        excp, false,
                                        ia64_current_code_tlb_ed(env));
}

void ia64_raise_pre_unaligned_data_fault(CPUIA64State *env,
                                                uint64_t va,
                                                uint32_t is_write,
                                                uint32_t is_rw,
                                                uint64_t fault_ip,
                                                uint8_t fault_slot)
{
    IA64Exception excp = ia64_data_reference_exception(
        env, va, is_write, is_rw, ia64_psr_cpl(env->psr), true, false,
        NULL);

    if (excp == IA64_EXCP_NONE) {
        return;
    }
    ia64_raise_data_reference_exception_at(
        env, va, is_write, is_rw, false, 0, excp, false,
        ia64_current_code_tlb_ed(env), false, fault_ip, fault_slot);
}

void ia64_mmu_probe_fault(CPUIA64State *env, uint64_t va, uint32_t is_write,
                        uint32_t is_rw, uint64_t access_level)
{
    uint8_t effective_pl = ia64_probe_access_level(env, access_level);
    uint64_t isr_access = IA64_ISR_NA | 5 |
        (is_rw ? (IA64_ISR_R | IA64_ISR_W) :
         (is_write ? IA64_ISR_W : IA64_ISR_R));

    ia64_raise_data_reference_fault_if_needed(env, va, is_write, is_rw,
                                              effective_pl, true, 5);
    ia64_mmu_check_data_debug(
        env, va, 1, isr_access, effective_pl, false,
        ia64_ip_bundle_addr(env->ip),
        (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT);
}

void ia64_mmu_lfetch_fault(CPUIA64State *env, uint64_t va,
                         uint64_t fault_ip, uint32_t fault_slot)
{
    IA64Exception excp = ia64_data_reference_exception(
        env, va, false, false, ia64_psr_cpl(env->psr), true, false, NULL);

    if (excp == IA64_EXCP_NONE) {
        ia64_mmu_check_data_debug(
            env, va, 1, IA64_ISR_NA | IA64_ISR_R | 4,
            ia64_psr_cpl(env->psr), false, fault_ip, fault_slot);
        return;
    }
    ia64_raise_data_reference_exception_at(
        env, va, false, false, true, 4, excp, false,
        ia64_current_code_tlb_ed(env), false, fault_ip, fault_slot);
}

void ia64_mmu_check_semaphore_access(CPUIA64State *env, uint64_t va)
{
    IA64DataReferenceResult translation = { 0 };
    IA64Exception excp = ia64_data_reference_exception(
        env, va, true, true, ia64_psr_cpl(env->psr), true, false,
        &translation);

    /* A NaTPage translation already reports NaT Page Consumption here. */
    if (excp != IA64_EXCP_NONE) {
        ia64_raise_data_reference_exception(
            env, va, true, true, false, 0, excp, false,
            ia64_current_code_tlb_ed(env));
    }
    if (translation.memory_attribute != IA64_PTE_MA_WB) {
        ia64_raise_data_reference_exception(
            env, va, true, true, false, 0,
            IA64_EXCP_UNSUPPORTED_DATA_REFERENCE, false,
            ia64_current_code_tlb_ed(env));
    }
}

void ia64_mmu_check_montecito_16byte_access(CPUIA64State *env, uint64_t va,
                                          uint32_t is_write)
{
    IA64DataReferenceResult translation = { 0 };
    IA64Exception excp;

    if (!ia64_env_cpu_class(env)->is_montecito) {
        return;
    }

    excp = ia64_data_reference_exception(
        env, va, is_write, false, ia64_psr_cpl(env->psr), true, false,
        &translation);
    /* A NaTPage translation already reports NaT Page Consumption here. */
    if (excp != IA64_EXCP_NONE) {
        ia64_raise_data_reference_exception(
            env, va, is_write, false, false, 0, excp, false,
            ia64_current_code_tlb_ed(env));
    }
    if (translation.memory_attribute != IA64_PTE_MA_WB) {
        ia64_raise_data_reference_exception(
            env, va, is_write, false, false, 0,
            IA64_EXCP_UNSUPPORTED_DATA_REFERENCE, false,
            ia64_current_code_tlb_ed(env));
    }
}

void ia64_mmu_check_alignment(CPUIA64State *env, uint64_t va,
                              uint32_t alignment_info,
                              uint64_t isr_access, uint64_t fault_info)
{
    IA64DataReferenceResult translation = { 0 };
    uint32_t size = alignment_info & IA64_ALIGNMENT_DATUM_MASK;
    uint32_t natural = (alignment_info & IA64_ALIGNMENT_NATURAL_MASK) >>
                       IA64_ALIGNMENT_NATURAL_SHIFT;
    IA64AlignmentClass alignment_class =
        (alignment_info & IA64_ALIGNMENT_CLASS_MASK) >>
        IA64_ALIGNMENT_CLASS_SHIFT;
    bool attribute_may_fault;

    if (size <= 1 || natural <= 1) {
        return;
    }

    attribute_may_fault =
        (va & (natural - 1U)) != 0 &&
        alignment_class != IA64_ALIGNMENT_NATURAL_REQUIRED &&
        ia64_env_cpu_class(env)->model != IA64_CPU_MODEL_MERCED &&
        ia64_crosses_alignment_window(va, size, 8U);
    if ((va & (natural - 1U)) == 0 && !attribute_may_fault) {
        return;
    }

    if (!ia64_alignment_fault(env, va, alignment_info,
                              (isr_access & IA64_ISR_W) != 0, NULL) &&
        attribute_may_fault) {
        /*
         * The memory attribute only matters for an otherwise-permitted
         * Itanium 2 FP reference.  A concurrent translation/PTE condition
         * is intentionally left for ia64_raise_unaligned() to re-evaluate
         * at its higher architectural priority.
         */
        ia64_data_reference_exception(
            env, va, (isr_access & IA64_ISR_W) != 0,
            (isr_access & (IA64_ISR_R | IA64_ISR_W)) ==
                (IA64_ISR_R | IA64_ISR_W),
            ia64_psr_cpl(env->psr), true, false, &translation);
    }

    if (ia64_alignment_fault(env, va, alignment_info,
                             (isr_access & IA64_ISR_W) != 0,
                             &translation)) {
        ia64_raise_unaligned(env, va, isr_access, fault_info);
    }
}

static bool ia64_cached_speculative_data_load_qualifies(
    CPUIA64State *env, uint64_t va, uint32_t datum_size,
    IA64MemorySpeculation *speculation, uint8_t *memory_attribute)
{
    IA64CachedLoadTranslation cached;
    const IA64TlbEntry *entry;
    uint64_t page_offset = va & ~TARGET_PAGE_MASK;
    uint64_t pa = UINT64_MAX;
    uint64_t modeled_phys_page = UINT64_MAX;
    uint64_t modeled_pte = 0;
    uint8_t perm;
    uint32_t rid;
    int modeled_exception = -1;
    int mmu_idx;

    /*
     * A load comparator does not currently certify all PKR read-disable
     * state, so protection-key accesses must retain full qualification.
     */
    if (!(env->psr & IA64_PSR_DT) ||
        (env->psr & (IA64_PSR_IS | IA64_PSR_PK))) {
        return false;
    }
    if (datum_size == 0 ||
        datum_size > TARGET_PAGE_SIZE - page_offset) {
        return false;
    }
    if (!ia64_va_is_implemented(env, va)) {
        return false;
    }

    /* Firmware identity mappings additionally depend on the current IP. */
    if (va >= IA64_FW_IDENTITY_BASE &&
        va < IA64_FW_IDENTITY_BASE + IA64_FW_IDENTITY_SIZE) {
        return false;
    }

    mmu_idx = ia64_exec_mmu_index(env, false);
    if (!ia64_exec_cached_load_translation(env, va, mmu_idx, &cached)) {
        return false;
    }

    rid = ia64_region_rid(env, va);
    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (entry && ia64_tlb_match(entry, va, rid)) {
        modeled_pte = entry->pte;
        ia64_tlb_entry_translate(entry, va, ia64_psr_cpl(env->psr),
                                 &pa, &perm);
        modeled_phys_page = pa & TARGET_PAGE_MASK;
        modeled_exception = ia64_tlb_exception_for_access(
            env, entry, perm, IA64_TLB_R, false, false, false);
        if (modeled_exception == IA64_EXCP_NONE &&
            (pa & TARGET_PAGE_MASK) == cached.phys_page &&
            ia64_pte_memory_speculation(entry->pte) == cached.speculation &&
            ((entry->pte >> 2) & 7) == cached.memory_attribute &&
            (cached.prot & PAGE_READ)) {
            *speculation = cached.speculation;
            *memory_attribute = cached.memory_attribute;
            goto attributes_current;
        }
    }

    /*
     * A reusable soft-TLB comparator matched, but the modeled IA-64
     * translation no longer describes the same readable physical page.
     * Returning to the cold qualifier alone is insufficient: a successful
     * cold probe would otherwise let the following qemu_ld reuse this stale
     * comparator.  Evict only this contradictory data entry, retaining the
     * instruction jump cache and unrelated address spaces.
     */
    trace_ia64_stale_soft_tlb_repair(
        env_cpu(env)->cpu_index, env->ip, va, mmu_idx, rid,
        cached.from_victim, cached.phys_page, modeled_phys_page,
        modeled_pte);
    tlb_flush_range_by_mmuidx_no_jmp_cache(
        env_cpu(env), va & TARGET_PAGE_MASK, TARGET_PAGE_SIZE,
        (MMUIdxMap)1U << mmu_idx, TARGET_LONG_BITS);
    return false;

attributes_current:
    if (!ia64_memory_allows_control_speculation(*speculation) ||
        (*memory_attribute != IA64_PTE_MA_WB &&
         *memory_attribute != IA64_PTE_MA_WC)) {
        return false;
    }

    return true;
}

static bool ia64_cached_speculative_load_succeeds(
    CPUIA64State *env, uint64_t va, uint32_t is_write, uint32_t is_ifetch,
    uint32_t debug_size, uint32_t alignment_info)
{
    uint32_t datum_size = alignment_info & IA64_ALIGNMENT_DATUM_MASK;
    IA64MemorySpeculation speculation;
    uint8_t memory_attribute;
    IA64DataReferenceResult translation = { 0 };

    if (is_write || is_ifetch ||
        !ia64_cached_speculative_data_load_qualifies(
            env, va, datum_size, &speculation, &memory_attribute)) {
        return false;
    }

    translation.valid = true;
    translation.speculation = speculation;
    translation.memory_attribute = memory_attribute;
    if (ia64_alignment_fault(env, va, alignment_info, false, &translation)) {
        return false;
    }
    if ((env->psr & IA64_PSR_DB) && !(env->psr & IA64_PSR_DD) &&
        ia64_data_breakpoint_match(env, va, debug_size, IA64_ISR_R,
                                   ia64_psr_cpl(env->psr))) {
        return false;
    }

    return true;
}

uint64_t ia64_mmu_speculative_probe(CPUIA64State *env, uint64_t va,
                                    uint32_t is_write, uint32_t is_ifetch,
                                    uint32_t debug_size,
                                    uint32_t alignment_info)
{
    uint32_t datum_size = alignment_info & IA64_ALIGNMENT_DATUM_MASK;
    uint32_t natural = (alignment_info & IA64_ALIGNMENT_NATURAL_MASK) >>
                       IA64_ALIGNMENT_NATURAL_SHIFT;
    bool alignment_fault = false;
    bool debug_fault = false;
    bool itlb_ed = false;
    IA64Exception excp;
    IA64DataReferenceResult translation = { 0 };

    if (env->psr & IA64_PSR_ED) {
        return 0;
    }

    if (debug_size > datum_size && natural > 1 &&
        (va & (natural - 1U)) != 0) {
        /* See the aligned-versus-unaligned FP DBR rule in Vol. 2, 7.1.2. */
        debug_size = datum_size;
    }
    if (ia64_cached_speculative_load_succeeds(
            env, va, is_write, is_ifetch, debug_size, alignment_info)) {
        return 1;
    }

    if (is_ifetch) {
        alignment_fault = ia64_alignment_fault(
            env, va, alignment_info, is_write, NULL);
        if (alignment_fault) {
            excp = IA64_EXCP_UNALIGNED;
            goto qualify;
        }
        return ia64_probe_address(env, va, is_write, is_ifetch,
                                  ia64_psr_cpl(env->psr));
    } else {
        /* PSR.ic controls fault collection, not data VHPT walking. */
        excp = ia64_data_reference_exception(
            env, va, is_write, false, ia64_psr_cpl(env->psr),
            true, false, &translation);
        alignment_fault = ia64_alignment_fault(
            env, va, alignment_info, is_write, &translation);
        debug_fault = (env->psr & IA64_PSR_DB) &&
                      !(env->psr & IA64_PSR_DD) &&
                      ia64_data_breakpoint_match(
                          env, va, debug_size,
                          is_write ? IA64_ISR_W : IA64_ISR_R,
                          ia64_psr_cpl(env->psr));
    }

qualify:
    /*
     * An unimplemented address precludes an unaligned-reference condition.
     * Other data-reference conditions retain their architectural priority;
     * only a condition that is itself deferred permits a lower-priority
     * unaligned condition to be considered.
     */
    if (excp == IA64_EXCP_UNIMPL_DATA_ADDR) {
        alignment_fault = false;
        debug_fault = false;
    }
    if (excp != IA64_EXCP_NONE || debug_fault || alignment_fault) {
        itlb_ed = ia64_current_code_tlb_ed(env);
    }
    if (excp != IA64_EXCP_NONE &&
        !ia64_speculative_exception_deferrable(env, excp, itlb_ed)) {
        ia64_raise_data_reference_exception(
            env, va, is_write, false, false, 0, excp, true, itlb_ed);
    }
    if (debug_fault &&
        !ia64_speculative_exception_deferrable(
            env, IA64_EXCP_DEBUG, itlb_ed)) {
        ia64_raise_data_debug_at(
            env, va, is_write ? IA64_ISR_W : IA64_ISR_R, true, itlb_ed,
            false, ia64_ip_bundle_addr(env->ip),
            (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT);
    }
    if (alignment_fault &&
        !ia64_speculative_exception_deferrable(
            env, IA64_EXCP_UNALIGNED, itlb_ed)) {
        ia64_raise_data_reference_exception(
            env, va, is_write, false, false, 0, IA64_EXCP_UNALIGNED, true,
            itlb_ed);
    }
    if (excp != IA64_EXCP_NONE || debug_fault || alignment_fault) {
        return 0;
    }

    if (!is_ifetch && translation.valid &&
        !ia64_memory_allows_control_speculation(translation.speculation)) {
        return 0;
    }
    return 1;
}

static G_GNUC_NO_INLINE uint64_t
ia64_mmu_speculative_int_probe_cold(CPUIA64State *env, uint64_t va,
                                    uint32_t size)
{
    return ia64_mmu_speculative_probe(
        env, va, 0, 0, size,
        IA64_ALIGNMENT_INFO(size, size, IA64_ALIGNMENT_INTEGER));
}

uint64_t ia64_mmu_speculative_int_probe(CPUIA64State *env, uint64_t va,
                                        uint32_t size)
{
    IA64MemorySpeculation speculation;
    uint8_t memory_attribute;

    if (env->psr & IA64_PSR_ED) {
        return 0;
    }

    if (unlikely((size != 1 && size != 2 && size != 4 && size != 8) ||
                 (va & (size - 1U)) != 0)) {
        return ia64_mmu_speculative_int_probe_cold(env, va, size);
    }

    if (!ia64_cached_speculative_data_load_qualifies(
            env, va, size, &speculation, &memory_attribute)) {
        return ia64_mmu_speculative_int_probe_cold(env, va, size);
    }
    if ((env->psr & IA64_PSR_DB) && !(env->psr & IA64_PSR_DD) &&
        ia64_data_breakpoint_match(env, va, size, IA64_ISR_R,
                                   ia64_psr_cpl(env->psr))) {
        return ia64_mmu_speculative_int_probe_cold(env, va, size);
    }

    return 1;
}

uint64_t ia64_mmu_advanced_load_allowed(CPUIA64State *env, uint64_t va)
{
    int mmu_idx = env->psr & IA64_PSR_DT ?
                  MMU_IDX_VIRT_CPL(ia64_psr_cpl(env->psr)) : MMU_PHYS_IDX;

    return ia64_exec_advanced_load_allowed(env, va, mmu_idx);
}

static bool ia64_vhpt_walk_full_internal(
    CPUIA64State *env, uint64_t va, uint32_t rid, bool is_ifetch,
    bool is_rse, uint8_t access_level, bool tak_access, uint64_t *pa,
    uint8_t *perm, uint64_t *pte, uint32_t *access_key,
    const IA64TlbEntry **installed_entry);

uint64_t ia64_mmu_tak(CPUIA64State *env, uint64_t va)
{
    uint32_t rid;
    const IA64TlbEntry *entry;
    uint64_t pa;
    uint8_t perm;
    uint64_t pte = 0;

    /* An unimplemented virtual address returns the architected miss value. */
    if (!ia64_va_is_implemented(env, va)) {
        return 1;
    }

    rid = ia64_region_rid(env, va);
    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (entry && ia64_tlb_entry_present(entry)) {
        /* Bits 31:8 hold the key; bit zero distinguishes a translation miss. */
        return (uint64_t)entry->key << 8;
    }

    if ((env->psr & IA64_PSR_DT) &&
        ia64_vhpt_walk_full_internal(
            env, va, rid, false, false, ia64_psr_cpl(env->psr), true,
            &pa, &perm, &pte, NULL, &entry) &&
        (pte & IA64_PTE_PRESENT)) {
        if (entry && ia64_tlb_entry_present(entry)) {
            return (uint64_t)entry->key << 8;
        }
    }

    return 1;
}

uint64_t ia64_mmu_thash(CPUIA64State *env, uint64_t va)
{
    return ia64_vhpt_hash_address(env, va);
}

static uint64_t ia64_implemented_va_payload(CPUIA64State *env, uint64_t va)
{
    uint8_t impl_va_msb = ia64_env_cpu_class(env)->impl_va_msb;

    return va & ((1ULL << (impl_va_msb + 1)) - 1);
}

static uint8_t ia64_region_preferred_ps(CPUIA64State *env, uint64_t va)
{
    uint8_t rr_ps = (env->rr[ia64_rr_index(va)] >> IA64_ITIR_PS_SHIFT) &
                    IA64_ITIR_PS_MASK;

    return rr_ps < 12 ? 12 : rr_ps;
}

static bool ia64_vhpt_preferred_page_size_supported(CPUIA64State *env,
                                                    uint64_t va)
{
    uint8_t rr_ps = (env->rr[ia64_rr_index(va)] >> IA64_ITIR_PS_SHIFT) &
                    IA64_ITIR_PS_MASK;

    return ia64_page_shift_insertable(env, rr_ps);
}

static uint64_t ia64_vhpt_hpn(CPUIA64State *env, uint64_t va)
{
    return ia64_implemented_va_payload(env, va) >>
           ia64_region_preferred_ps(env, va);
}

static uint64_t ia64_vhpt_long_tag(CPUIA64State *env, uint64_t va)
{
    uint64_t hpn = ia64_vhpt_hpn(env, va);
    uint64_t rid = ia64_region_rid(env, va);

    /* The index disambiguates overlapping bits; bit 63 remains clear. */
    return hpn ^ (rid << 39);
}

static uint64_t ia64_vhpt_short_hash_address(CPUIA64State *env, uint64_t va,
                                             uint8_t size)
{
    uint64_t region = va & (IA64_REGION_MASK << IA64_REGION_SHIFT);
    uint64_t offset;
    uint64_t mask;
    uint64_t base;

    offset = ia64_vhpt_hpn(env, va) << 3;
    mask = (1ULL << size) - 1;
    base = env->cr_pta & (((1ULL << IA64_REGION_SHIFT) - 1) & ~0x7fffULL);
    return region | ((base & ~mask) | (offset & mask));
}

static uint64_t ia64_vhpt_long_hash_address(CPUIA64State *env, uint64_t va,
                                            uint8_t size, uint64_t *hash_out)
{
    uint64_t base = env->cr_pta & IA64_PTA_BASE_MASK;
    uint64_t entries = 1ULL << (size - 5);
    uint64_t hpn = ia64_vhpt_hpn(env, va);
    uint64_t hash;
    uint64_t offset;
    uint64_t mask = (1ULL << size) - 1;

    hash = hpn ^ ia64_region_rid(env, va);
    hash &= entries - 1;
    offset = hash << 5;

    if (hash_out) {
        *hash_out = hash;
    }
    return (base & ~mask) | (offset & mask);
}

uint64_t ia64_vhpt_hash_address(CPUIA64State *env, uint64_t va)
{
    uint8_t size;
    bool long_format;

    if (!ia64_vhpt_config_valid(env, &size, &long_format)) {
        return va;
    }

    if (!long_format) {
        return ia64_vhpt_short_hash_address(env, va, size);
    }

    return ia64_vhpt_long_hash_address(env, va, size, NULL);
}

uint64_t ia64_mmu_ttag(CPUIA64State *env, uint64_t va)
{
    return ia64_vhpt_long_tag(env, va);
}

typedef enum IA64VhptEntryStatus {
    IA64_VHPT_ENTRY_TRANSLATED,
    IA64_VHPT_ENTRY_TLB_MISS,
    IA64_VHPT_ENTRY_ABORT,
} IA64VhptEntryStatus;

static IA64VhptEntryStatus ia64_vhpt_entry_phys(CPUIA64State *env,
                                                uint64_t entry_va,
                                                uint64_t *entry_pa)
{
    const IA64TlbEntry *entry;
    uint8_t perm;
    uint32_t rid;

    if (ia64_firmware_identity_pa(env->cr_iva, env->psr,
                                  entry_va, entry_pa)) {
        return IA64_VHPT_ENTRY_TRANSLATED;
    }

    rid = ia64_region_rid(env, entry_va);
    /* VHPT walker references use privilege level 0 regardless of PSR.cpl. */
    entry = ia64_tlb_find_cached(env, entry_va, rid, false);
    if (entry && entry->pending_purge) {
        /*
         * A purge may complete before its required serialization point.
         * Do not use a pending translation to read the VHPT: a PTE fetched
         * through it could install a new, non-pending TC entry which survives
         * completion of the original purge.
         */
        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 vhpt entry pending purge va=0x%016" PRIx64
                      " rid=0x%06" PRIx32 "\n",
                      entry_va, rid);
        return IA64_VHPT_ENTRY_TLB_MISS;
    }
    if (entry) {
        IA64Exception excp;
        uint8_t ma = (entry->pte & IA64_PTE_MA_MASK) >> IA64_PTE_MA_SHIFT;

        ia64_tlb_entry_translate(entry, entry_va, 0, entry_pa, &perm);
        excp = ia64_tlb_exception_for_access(env, entry, perm, IA64_TLB_R,
                                             false, false, false);
        if (excp == IA64_EXCP_NONE && ma == 0) {
            return IA64_VHPT_ENTRY_TRANSLATED;
        }
        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 vhpt entry abort va=0x%016" PRIx64
                      " rid=0x%06" PRIx32
                      " pte=0x%016" PRIx64 " ma=%u excp=%u\n",
                      entry_va, rid, entry->pte, ma, excp);
        return IA64_VHPT_ENTRY_ABORT;
    }

    return IA64_VHPT_ENTRY_TLB_MISS;
}

bool ia64_vhpt_entry_accessible(CPUIA64State *env, uint64_t va,
                                bool is_ifetch, bool is_rse,
                                uint64_t *entry_va)
{
    uint64_t entry_pa;
    bool long_format;
    uint8_t size;

    if (!ia64_vhpt_walker_enabled(env, va, is_ifetch, is_rse,
                                  &size, &long_format)) {
        return true;
    }
    if (!ia64_vhpt_preferred_page_size_supported(env, va)) {
        return true;
    }
    *entry_va = long_format ? ia64_vhpt_long_hash_address(env, va, size,
                                                          NULL) :
                ia64_vhpt_short_hash_address(env, va, size);
    /*
     * A present-but-faulting translation for the VHPT entry makes the walker
     * abort to the original TLB miss.  Only a missing DTLB translation for
     * the VHPT entry raises a VHPT Translation fault.
     */
    return ia64_vhpt_entry_phys(env, *entry_va, &entry_pa) !=
           IA64_VHPT_ENTRY_TLB_MISS;
}

static uint64_t ia64_vhpt_load_u64(CPUIA64State *env, uint64_t pa)
{
    uint8_t buf[8];

    (void)ia64_exec_physical_rw(pa, buf, sizeof(buf), false);
    return env->cr_dcr & IA64_DCR_BE ? ldq_be_p(buf) : ldq_le_p(buf);
}

static void ia64_vhpt_load_long_entry(CPUIA64State *env, uint64_t pa,
                                      uint64_t *pte, uint64_t *itir,
                                      uint64_t *tag)
{
    *pte = ia64_vhpt_load_u64(env, pa);
    *itir = ia64_vhpt_load_u64(env, pa + 8);
    *tag = ia64_vhpt_load_u64(env, pa + 16);
}

static bool ia64_vhpt_pte_valid(uint64_t pte)
{
    uint8_t ma;

    if (!(pte & IA64_PTE_PRESENT)) {
        return true;
    }
    ma = (pte & IA64_PTE_MA_MASK) >> IA64_PTE_MA_SHIFT;
    return !(pte & IA64_PTE_RESERVED_MASK) && (ma == 0 || ma >= 4);
}

static bool ia64_vhpt_itir_valid(CPUIA64State *env, uint64_t pte,
                                 uint64_t itir)
{
    uint8_t page_shift = (itir >> IA64_ITIR_PS_SHIFT) & IA64_ITIR_PS_MASK;
    uint64_t reserved_mask = IA64_ITIR_RESERVED_MASK;

    if (!(pte & IA64_PTE_PRESENT)) {
        reserved_mask &= 3;
    }
    return !(itir & reserved_mask) &&
           ia64_page_shift_insertable(env, page_shift);
}

static bool ia64_vhpt_lookup_pte_itir(CPUIA64State *env, uint64_t va,
                                      bool is_ifetch, bool is_rse,
                                      uint64_t *pte, uint64_t *entry_va,
                                      uint64_t *out_itir)
{
    uint8_t size;
    bool long_format;
    uint64_t entry_pa;

    if (out_itir) {
        *out_itir = 0;
    }

    if (!ia64_vhpt_walker_enabled(env, va, is_ifetch, is_rse,
                                  &size, &long_format)) {
        return false;
    }
    if (!ia64_vhpt_preferred_page_size_supported(env, va)) {
        return false;
    }

    if (!long_format) {
        *entry_va = ia64_vhpt_short_hash_address(env, va, size);
        if (ia64_vhpt_entry_phys(env, *entry_va, &entry_pa) !=
            IA64_VHPT_ENTRY_TRANSLATED) {
            return false;
        }
        *pte = ia64_vhpt_load_u64(env, entry_pa);
        return ia64_vhpt_pte_valid(*pte);
    }

    {
        uint64_t expected_tag = ia64_vhpt_long_tag(env, va);
        uint64_t itir;
        uint64_t tag;

        *entry_va = ia64_vhpt_long_hash_address(env, va, size, NULL);
        if (ia64_vhpt_entry_phys(env, *entry_va, &entry_pa) !=
            IA64_VHPT_ENTRY_TRANSLATED) {
            return false;
        }
        ia64_vhpt_load_long_entry(env, entry_pa, pte, &itir, &tag);
        if ((tag & (1ULL << 63)) || tag != expected_tag) {
            return false;
        }
        if (out_itir) {
            *out_itir = itir;
        }
        return ia64_vhpt_pte_valid(*pte) &&
               ia64_vhpt_itir_valid(env, *pte, itir);
    }
}

static bool ia64_vhpt_lookup_pte(CPUIA64State *env, uint64_t va,
                                 bool is_ifetch, bool is_rse, uint64_t *pte,
                                 uint64_t *entry_va)
{
    return ia64_vhpt_lookup_pte_itir(env, va, is_ifetch, is_rse, pte,
                                     entry_va, NULL);
}

/*
 * Translate for the monitor, gva2gpa and the gdbstub.  This must not disturb
 * the modeled MMU: no TC insertion, no fault, no PSR update.  Returning false
 * makes callers report the address as unmapped; the previous behaviour of
 * falling back to the virtual address silently produced a physical address
 * that belonged to something else entirely.
 *
 * A miss is not proof that the guest cannot reach the address.  On IA-64 the
 * VHPT is a translation cache rather than the authoritative page table, and
 * an OS that keeps separate tables can map an address this function cannot
 * resolve.
 */
bool ia64_mmu_translate_debug(CPUIA64State *env, uint64_t va, uint64_t *pa)
{
    const IA64TlbEntry *entry;
    uint64_t pte;
    uint64_t itir;
    uint64_t entry_va;
    uint64_t page_mask;
    uint8_t page_shift;
    uint8_t perm;
    uint32_t rid;

    if (!(env->psr & IA64_PSR_IT)) {
        *pa = va;
        return true;
    }
    if (ia64_firmware_identity_pa(env->cr_iva, env->psr, va, pa)) {
        return true;
    }

    rid = ia64_region_rid(env, va);

    /* Data first: debug accesses are overwhelmingly data, not code. */
    entry = ia64_tlb_find_cached(env, va, rid, false);
    if (!entry) {
        entry = ia64_tlb_find_cached(env, va, rid, true);
    }
    if (entry) {
        ia64_tlb_entry_translate(entry, va, ia64_psr_cpl(env->psr), pa, &perm);
        return true;
    }

    if (!ia64_vhpt_lookup_pte_itir(env, va, false, false, &pte, &entry_va,
                                   &itir) ||
        !(pte & IA64_PTE_PRESENT)) {
        return false;
    }

    page_shift = itir ? ((itir >> IA64_ITIR_PS_SHIFT) & IA64_ITIR_PS_MASK) :
                        ia64_region_preferred_ps(env, va);
    page_mask = (1ULL << page_shift) - 1;
    *pa = ((pte & IA64_PTE_PPN_MASK) & ~page_mask) | (va & page_mask);
    return true;
}

bool ia64_vhpt_pte_not_present(CPUIA64State *env, uint64_t va,
                               bool is_ifetch, bool is_rse,
                               uint64_t *entry_va)
{
    uint64_t local_entry_va;
    uint64_t pte;
    uint8_t size;
    bool long_format;
    bool enabled;

    if (!entry_va) {
        entry_va = &local_entry_va;
    }

    /* A DBR.r abort takes the original TLB Miss, not Page Not Present. */
    enabled = ia64_vhpt_walker_enabled(env, va, is_ifetch, is_rse,
                                       &size, &long_format) &&
              ia64_vhpt_preferred_page_size_supported(env, va);
    if (enabled) {
        *entry_va = long_format ?
            ia64_vhpt_long_hash_address(env, va, size, NULL) :
            ia64_vhpt_short_hash_address(env, va, size);
    }
    if (enabled &&
        ia64_vhpt_data_breakpoint_match(
            env, *entry_va, long_format ? 32 : 8, false)) {
        return false;
    }

    return ia64_vhpt_lookup_pte(env, va, is_ifetch, is_rse,
                                &pte, entry_va) &&
           !(pte & IA64_PTE_PRESENT);
}

static const IA64TlbEntry *
ia64_vhpt_install_tc(CPUIA64State *env, uint64_t va, uint32_t rid,
                     bool is_ifetch, uint64_t pa, uint64_t page_size,
                     uint8_t ar, uint8_t pl, uint8_t perm, uint32_t key,
                     uint64_t pte)
{
    IA64TlbEntry *tlb = is_ifetch ? env->mmu.tlb_inst : env->mmu.tlb_data;
    uint16_t *cnt = is_ifetch ?
        &env->mmu.tlb_inst_count : &env->mmu.tlb_data_count;
    uint16_t *next_replace = is_ifetch ? &env->mmu.tlb_inst_replace :
                                         &env->mmu.tlb_data_replace;
    uint16_t *pending_count = is_ifetch ?
        &env->mmu.pending_purge_inst_count : &env->mmu.pending_purge_data_count;
    uint16_t capacity = ia64_cpu_tlb_capacity(env, !is_ifetch);
    uint64_t base_va = va & ~(page_size - 1);
    uint64_t base_pa = pa & ~(page_size - 1);
    int slot;

    ia64_purge_tc_entries(env, tlb, capacity, cnt, pending_count, base_va,
                          page_size, rid, !is_ifetch, next_replace, &slot);
    if (slot < 0) {
        return NULL;
    }

    ia64_qemu_tlb_flush_replaced_entry(env, &tlb[slot], !is_ifetch);
    ia64_discard_pending_purge(&tlb[slot], pending_count);
    tlb[slot].va = base_va;
    tlb[slot].pa = base_pa;
    tlb[slot].ps = page_size;
    tlb[slot].page_mask = ia64_va_vpn_mask(page_size);
    tlb[slot].pte = pte;
    tlb[slot].perm = perm;
    tlb[slot].ar = ar;
    tlb[slot].pl = pl;
    tlb[slot].valid = 1;
    tlb[slot].is_tr = 0;
    tlb[slot].pending_purge = 0;
    tlb[slot].rid = rid;
    tlb[slot].key = key;
    tlb[slot].slot = slot;
    if (slot >= *cnt) {
        *cnt = slot + 1;
    }
    ia64_tlb_bump_slot_generation(env, is_ifetch, slot);
    ia64_assert_pending_purge_counts(env);
    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 vhpt install tc.%c slot=%d va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                  " ps=0x%016" PRIx64 " perm=0x%x key=0x%x"
                  " pte=0x%016" PRIx64 "\n",
                  is_ifetch ? 'i' : 'd', slot, base_va, rid, base_pa,
                  page_size, perm, key, pte);
    /*
     * The QEMU softmmu TLB is indexed by guest virtual address and mmu_idx;
     * it does not carry IA-64 region IDs.  A VHPT walk can install a TC entry
     * for a different RID than a cached same-VA host entry, so discard the
     * host translation range covered by the installed TC.
    */
    ia64_qemu_tlb_flush_entry(env, &tlb[slot], !is_ifetch);
    return &tlb[slot];
}

/* ---- VHPT walker ---- */

static bool ia64_vhpt_walk_full_internal(
    CPUIA64State *env, uint64_t va, uint32_t rid, bool is_ifetch,
    bool is_rse, uint8_t access_level, bool tak_access, uint64_t *pa,
    uint8_t *perm, uint64_t *pte, uint32_t *access_key,
    const IA64TlbEntry **installed_entry)
{
    uint64_t vhpt_base;
    uint64_t hash;
    uint64_t tag;
    uint64_t expected_tag;
    uint64_t translation;
    uint64_t itir;
    uint64_t entry_pa;
    uint64_t entry_va;
    uint8_t page_shift;
    uint8_t size;
    bool long_format;

    if (installed_entry) {
        *installed_entry = NULL;
    }

    if (!ia64_vhpt_walker_enabled(env, va, is_ifetch, is_rse,
                                  &size, &long_format)) {
        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 vhpt disabled %c va=0x%016" PRIx64
                      " rid=0x%06" PRIx32 " pta=0x%016" PRIx64
                      " rr=0x%016" PRIx64 " psr=0x%016" PRIx64 "\n",
                      is_ifetch ? 'i' : 'd', va, rid, env->cr_pta,
                      env->rr[ia64_rr_index(va)], env->psr);
        return false;
    }
    if (!ia64_vhpt_preferred_page_size_supported(env, va)) {
        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 vhpt unsupported preferred page size %c"
                      " va=0x%016" PRIx64 " rid=0x%06" PRIx32 "\n",
                      is_ifetch ? 'i' : 'd', va, rid);
        return false;
    }

    if (!long_format) {
        uint64_t page_mask;

        entry_va = ia64_vhpt_short_hash_address(env, va, size);
        page_shift = ia64_region_preferred_ps(env, va);
        if (ia64_vhpt_entry_phys(env, entry_va, &entry_pa) !=
            IA64_VHPT_ENTRY_TRANSLATED) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt short entry miss %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va);
            return false;
        }
        if (ia64_vhpt_data_breakpoint_match(env, entry_va, 8,
                                            tak_access)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt short DBR abort %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va);
            return false;
        }

        translation = ia64_vhpt_load_u64(env, entry_pa);
        if (!ia64_vhpt_pte_valid(translation)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt short reserved translation %c"
                          " va=0x%016" PRIx64 " rid=0x%06" PRIx32
                          " pte=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, translation);
            return false;
        }
        if (pte) {
            *pte = translation;
        }
        if (access_key) {
            *access_key = rid;
        }
        {
            uint8_t ar = ia64_pte_ar(translation);
            uint8_t pl = ia64_pte_pl(translation);

            *perm = ia64_pte_perm(translation, access_level);
            page_mask = (1ULL << page_shift) - 1;
            *pa = ((translation & IA64_PTE_PPN_MASK) & ~page_mask) |
                  (va & page_mask);
            const IA64TlbEntry *entry = ia64_vhpt_install_tc(
                env, va, rid, is_ifetch, *pa, 1ULL << page_shift, ar, pl,
                *perm, rid, translation);

            if (installed_entry) {
                *installed_entry = entry;
            }
        }
        if (!(translation & IA64_PTE_PRESENT)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt short not-present %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64
                          " entry_pa=0x%016" PRIx64
                          " pte=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va, entry_pa,
                          translation);
            return true;
        }
        if (*perm == 0) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt short access denied %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64
                          " entry_pa=0x%016" PRIx64
                          " pte=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va,
                          entry_pa, translation);
            return true;
        }

        qemu_log_mask(CPU_LOG_MMU,
                      "ia64 vhpt short walk %c va=0x%016" PRIx64
                      " rid=0x%06" PRIx32
                      " entry_va=0x%016" PRIx64
                      " entry_pa=0x%016" PRIx64
                      " pte=0x%016" PRIx64
                      " pa=0x%016" PRIx64 " perm=0x%x\n",
                      is_ifetch ? 'i' : 'd', va, rid, entry_va, entry_pa,
                      translation, *pa, *perm);
        return true;
    }

    vhpt_base = env->cr_pta & IA64_PTA_BASE_MASK;
    expected_tag = ia64_vhpt_long_tag(env, va);
    {
        entry_va = ia64_vhpt_long_hash_address(env, va, size, &hash);
        if (ia64_vhpt_entry_phys(env, entry_va, &entry_pa) !=
            IA64_VHPT_ENTRY_TRANSLATED) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt long entry miss %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va);
            return false;
        }
        if (ia64_vhpt_data_breakpoint_match(env, entry_va, 32,
                                            tak_access)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt long DBR abort %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va);
            return false;
        }

        ia64_vhpt_load_long_entry(env, entry_pa, &translation, &itir, &tag);
        if (tag & (1ULL << 63)) {
            goto long_miss;
        }
        if (tag != expected_tag) {
            goto long_miss;
        }
        if (!ia64_vhpt_pte_valid(translation) ||
            !ia64_vhpt_itir_valid(env, translation, itir)) {
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt reserved translation %c"
                          " va=0x%016" PRIx64 " rid=0x%06" PRIx32
                          " pte=0x%016" PRIx64 " itir=0x%016" PRIx64 "\n",
                          is_ifetch ? 'i' : 'd', va, rid, translation, itir);
            return false;
        }
        if (pte) {
            *pte = translation;
        }

        {
            uint64_t page_mask;
            uint8_t long_page_shift =
                (itir >> IA64_ITIR_PS_SHIFT) & IA64_ITIR_PS_MASK;

            page_mask = (1ULL << long_page_shift) - 1;
            {
                uint8_t ar = ia64_pte_ar(translation);
                uint8_t pl = ia64_pte_pl(translation);
                uint32_t entry_key = (itir & IA64_ITIR_KEY_MASK) >>
                                     IA64_ITIR_KEY_SHIFT;

                if (access_key) {
                    *access_key = entry_key;
                }
                *perm = ia64_pte_perm(translation, access_level);
                *pa = ((translation & IA64_PTE_PPN_MASK) & ~page_mask) |
                      (va & page_mask);
                const IA64TlbEntry *entry = ia64_vhpt_install_tc(
                    env, va, rid, is_ifetch, *pa,
                    1ULL << long_page_shift, ar, pl, *perm, entry_key,
                    translation);

                if (installed_entry) {
                    *installed_entry = entry;
                }
            }
            if (!(translation & IA64_PTE_PRESENT)) {
                qemu_log_mask(CPU_LOG_MMU,
                              "ia64 vhpt not-present %c va=0x%016" PRIx64
                              " rid=0x%06" PRIx32
                              " entry_va=0x%016" PRIx64
                              " entry_pa=0x%016" PRIx64
                              " tag=0x%016" PRIx64
                              " pte=0x%016" PRIx64 "\n",
                              is_ifetch ? 'i' : 'd', va, rid, entry_va,
                              entry_pa, tag, translation);
                return true;
            }
            if (*perm == 0) {
                qemu_log_mask(CPU_LOG_MMU,
                              "ia64 vhpt access denied %c va=0x%016" PRIx64
                              " rid=0x%06" PRIx32
                              " entry_va=0x%016" PRIx64
                              " entry_pa=0x%016" PRIx64
                              " tag=0x%016" PRIx64
                              " pte=0x%016" PRIx64 "\n",
                              is_ifetch ? 'i' : 'd', va, rid, entry_va,
                              entry_pa, tag, translation);
                return true;
            }

            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt walk %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32
                          " entry_va=0x%016" PRIx64
                          " entry_pa=0x%016" PRIx64
                          " tag=0x%016" PRIx64 " pte=0x%016" PRIx64
                          " pa=0x%016" PRIx64 " perm=0x%x\n",
                          is_ifetch ? 'i' : 'd', va, rid, entry_va, entry_pa,
                          tag, translation, *pa, *perm);
            return true;
        }
    }
long_miss:
    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 vhpt miss %c va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " base=0x%016" PRIx64
                  " hash=0x%016" PRIx64 "\n",
                  is_ifetch ? 'i' : 'd', va, rid, vhpt_base, hash);
    return false;
}

bool ia64_vhpt_walk_full(CPUIA64State *env, uint64_t va, uint32_t rid,
                         bool is_ifetch, bool is_rse, uint8_t access_level,
                         uint64_t *pa, uint8_t *perm, uint64_t *pte,
                         uint32_t *access_key,
                         const IA64TlbEntry **installed_entry)
{
    return ia64_vhpt_walk_full_internal(
        env, va, rid, is_ifetch, is_rse, access_level, false, pa, perm, pte,
        access_key, installed_entry);
}

bool ia64_vhpt_walk(CPUIA64State *env, uint64_t va, uint32_t rid,
                    bool is_ifetch, bool is_rse, uint8_t access_level,
                    uint64_t *pa, uint8_t *perm)
{
    return ia64_vhpt_walk_full(env, va, rid, is_ifetch, is_rse, access_level,
                               pa, perm, NULL, NULL, NULL);
}

/* ---- ITC insert helper (software-managed TLB insert) ---- */

void ia64_mmu_itc_insert(CPUIA64State *env, uint64_t pte, uint32_t is_data,
                       uint64_t raw, uint32_t fault_slot)
{
    IA64TlbEntry *tlb;
    uint16_t *cnt;
    uint16_t *next_replace;
    uint16_t *pending_count;
    uint64_t ps = ia64_itir_page_size(env);
    uint64_t va = env->cr_ifa & ~(ps - 1);
    uint64_t pa = (pte & IA64_PTE_PPN_MASK) & ~(ps - 1);
    uint32_t key = (env->cr_itir & IA64_ITIR_KEY_MASK) >>
                   IA64_ITIR_KEY_SHIFT;
    uint32_t rid = ia64_region_rid(env, env->cr_ifa);
    uint8_t ar = ia64_pte_ar(pte);
    uint8_t pl = ia64_pte_pl(pte);
    uint8_t perm = ia64_pte_perm(pte, 0);
    uint16_t capacity = ia64_cpu_tlb_capacity(env, is_data);
    int slot;
    bool purged;

    if (!ia64_translation_insert_fields_valid(env, pte, env->cr_itir)) {
        env->cr_isr = 0x30;
        ia64_raise_exception(env, IA64_EXCP_RESERVED_REG_FIELD,
                               ia64_ip_bundle_addr(env->ip), raw,
                               fault_slot);
        return;
    }

    if (!ia64_va_is_implemented(env, env->cr_ifa)) {
        ia64_raise_unimplemented_data_address(
            env, env->cr_ifa, 0, true, false, ia64_current_code_tlb_ed(env));
    }
    ia64_mmu_check_virtualization(env, raw, fault_slot);

    if ((pte & IA64_PTE_PRESENT) && perm == 0) {
        return;
    }

    if (is_data) {
        tlb = env->mmu.tlb_data;
        cnt = &env->mmu.tlb_data_count;
        next_replace = &env->mmu.tlb_data_replace;
        pending_count = &env->mmu.pending_purge_data_count;
    } else {
        tlb = env->mmu.tlb_inst;
        cnt = &env->mmu.tlb_inst_count;
        next_replace = &env->mmu.tlb_inst_replace;
        pending_count = &env->mmu.pending_purge_inst_count;
    }

    purged = ia64_purge_tc_entries(env, tlb, capacity, cnt, pending_count, va,
                                   ps, rid, is_data, next_replace, &slot);
    if (slot < 0) {
        return;
    }
    ia64_qemu_tlb_flush_replaced_entry(env, &tlb[slot], is_data);
    ia64_discard_pending_purge(&tlb[slot], pending_count);

    tlb[slot].va = va;
    tlb[slot].pa = pa;
    tlb[slot].ps = ps;
    tlb[slot].page_mask = ia64_va_vpn_mask(ps);
    tlb[slot].pte = pte;
    tlb[slot].perm = perm;
    tlb[slot].ar = ar;
    tlb[slot].pl = pl;
    tlb[slot].valid = 1;
    tlb[slot].is_tr = 0;
    tlb[slot].pending_purge = 0;
    tlb[slot].rid = rid;
    tlb[slot].key = key;
    tlb[slot].slot = slot;
    if (slot >= *cnt) {
        *cnt = slot + 1;
    }
    ia64_tlb_bump_slot_generation(env, !is_data, slot);
    ia64_assert_pending_purge_counts(env);
    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 itc.%c %s slot=%u va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                  " ps=0x%016" PRIx64 " perm=0x%x"
                  " pte=0x%016" PRIx64 "\n",
                  is_data ? 'd' : 'i', purged ? "update" : "slot",
                  slot, va, rid, pa, ps, perm, pte);
    /*
     * Overlapping guest TC entries were already purged above.  Only the
     * emulator-provided firmware/SAL identity mappings can exist in the
     * QEMU TLB without a corresponding modeled TC entry.
     */
    ia64_qemu_tlb_flush_entry(env, &tlb[slot], is_data);
}

bool ia64_mmu_insert_firmware_tc(CPUIA64State *env, uint64_t va,
                                 uint64_t pa, bool is_data,
                                 uint8_t page_shift)
{
    const IA64TlbEntry *entry;
    uint64_t page_size;
    uint64_t pte;
    uint32_t rid;
    uint8_t perm;

    if (!ia64_page_shift_insertable(env, page_shift) ||
        !ia64_va_is_implemented(env, va) ||
        !ia64_pa_is_implemented(env, pa)) {
        return false;
    }

    page_size = 1ULL << page_shift;
    if ((va & (page_size - 1)) != (pa & (page_size - 1))) {
        return false;
    }

    env->cr_ifa = va;
    env->cr_itir = (env->cr_itir & IA64_ITIR_KEY_MASK) |
                   ((uint64_t)page_shift << IA64_ITIR_PS_SHIFT);
    pte = (pa & IA64_PTE_PPN_MASK & ~(page_size - 1)) |
          IA64_PTE_PRESENT | IA64_PTE_ACCESSED | IA64_PTE_DIRTY |
          (3ULL << IA64_PTE_AR_SHIFT);
    ia64_mmu_itc_insert(env, pte, is_data, 0, 0);

    rid = ia64_region_rid(env, va);
    entry = ia64_tlb_find_cached(env, va, rid, !is_data);
    if (entry == NULL || entry->is_tr) {
        return false;
    }
    ia64_tlb_entry_translate(entry, va, 0, &pa, &perm);
    return perm != 0;
}
