/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 CPU QOM and execution-engine glue.
 *
 * Instruction decoding, family generators, and architectural helper logic
 * live in decode/, translate/, and arch/ respectively.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "ia32/ia32.h"
#include "debug.h"
#include "exec-access.h"
#include "translate/translate.h"
#include "exec/cputlb.h"
#include "exec/cpu-common.h"
#include "exec/tlb-flags.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "hw/core/sysemu-cpu-ops.h"
#include "hw/ia64/ia64_ras.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/debug-assert.h"
#include "exec/translator.h"
#include "exec/helper-proto.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "system/physmem.h"
#include "system/qtest.h"
#include "system/tcg.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef HELPER_H

static void ia64_cpu_set_pc(CPUState *cs, vaddr value)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    if (cpu->env.psr & IA64_PSR_IS) {
        cpu->env.ia32.eip =
            (uint32_t)(value - cpu->env.ia32.segs[R_CS].base);
    }
    cpu->env.ip = value;
}

static vaddr ia64_cpu_get_pc(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    return cpu->env.psr & IA64_PSR_IS ?
           ia64_ia32_virtual_ip(&cpu->env) : cpu->env.ip;
}


static TCGTBCPUState ia64_get_tb_cpu_state(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    uint64_t psr = cpu->env.psr;
    CPUX86State *xenv = &cpu->env.ia32;

    if (psr & IA64_PSR_IS) {
        uint32_t cs_base = xenv->segs[R_CS].base;
        uint32_t flags = xenv->hflags |
            (xenv->eflags &
             (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK)) |
            ((psr & IA64_PSR_DB) ? IA64_TB_FLAG_IA32_PSR_DB : 0) |
            ((psr & IA64_PSR_AC) ? IA64_TB_FLAG_IA32_PSR_AC : 0) |
            ((psr & IA64_PSR_SS) ? TF_MASK : 0);

        return (TCGTBCPUState) {
            .pc = (uint32_t)(cs_base + xenv->eip),
            .cs_base = cs_base,
            .flags = flags | IA64_TB_FLAG_PSR_IS,
        };
    }

    uint32_t flags =
        ((uint32_t)psr &
         (IA64_TB_FLAG_BE | IA64_TB_FLAG_PSR_IC | IA64_TB_FLAG_DT)) |
        ((psr >> (IA64_PSR_CPL_SHIFT - IA64_TB_FLAG_CPL_SHIFT)) &
         (IA64_TB_FLAG_CPL_MASK | IA64_TB_FLAG_IT |
          IA64_TB_FLAG_RI_MASK)) |
        ((uint32_t)cpu->env.instruction_group_start << 7);

    flags |= (psr & IA64_PSR_FAULT_SUPPRESS_MASK) != 0 ?
             IA64_TB_FLAG_PSR_SUPPRESS : 0;
    flags |= (psr & IA64_PSR_TB) ? IA64_TB_FLAG_PSR_TB : 0;
    flags |= (psr & IA64_PSR_SS) ? IA64_TB_FLAG_PSR_SS : 0;
    flags |= (psr & IA64_PSR_DB) ? IA64_TB_FLAG_PSR_DB : 0;
    if (likely((cpu->env.nat[0] | cpu->env.nat[1]) == 0)) {
        flags |= IA64_TB_FLAG_NAT_CLEAR;
    }

    return (TCGTBCPUState) {
        .pc = cpu->env.ip,
        .flags = flags,
    };
}

void ia64_tlb_bump_generation(CPUIA64State *env, bool is_ifetch)
{
    IA64MicroTlbEntry *micro = is_ifetch ? env->mmu.tlb_inst_micro :
                                           env->mmu.tlb_data_micro;
    uint32_t *generation = is_ifetch ? &env->mmu.tlb_inst_generation :
                                       &env->mmu.tlb_data_generation;

    (*generation)++;
    if (*generation == 0) {
        *generation = 1;
        memset(micro, 0, sizeof(*micro) * IA64_MICRO_TLB_SIZE);
    }
}

void ia64_tlb_bump_slot_generation(CPUIA64State *env, bool is_ifetch,
                                   uint16_t slot)
{
    IA64TlbEntry *tlb = is_ifetch ? env->mmu.tlb_inst :
                                    env->mmu.tlb_data;

    g_assert(slot < IA64_TLB_MAX);
    if (++tlb[slot].micro_generation == 0) {
        /*
         * A wrapped slot version could validate a very old hint.  Make the
         * wrap unambiguous by invalidating all hints before reusing version
         * one.  This path requires over four billion changes to one slot.
         */
        tlb[slot].micro_generation = 1;
        ia64_tlb_bump_generation(env, is_ifetch);
    }
}

const IA64TlbEntry *ia64_tlb_find_slow(CPUIA64State *env, uint64_t va,
                                       uint32_t rid, bool is_ifetch)
{
    IA64TlbEntry *tlb = is_ifetch ? env->mmu.tlb_inst : env->mmu.tlb_data;
    IA64MicroTlbEntry *micro = is_ifetch ? env->mmu.tlb_inst_micro :
                                           env->mmu.tlb_data_micro;
    uint16_t tlb_count = is_ifetch ? env->mmu.tlb_inst_count :
                                     env->mmu.tlb_data_count;
    uint32_t generation = is_ifetch ? env->mmu.tlb_inst_generation :
                                      env->mmu.tlb_data_generation;
    uint16_t i;

    /*
     * Merced's DTLB1 and DTLB2 are non-inclusive.  Although DTLB1 is not
     * architecturally enumerated as additional TR/TC storage, an entry
     * cached there continues to translate accesses after its DTLB2 source
     * has been replaced.  The host softmmu TLB is only an optimization and
     * may be flushed independently, so it cannot stand in for this lookup.
     */
    if (!is_ifetch &&
        ia64_env_cpu_class(env)->model == IA64_CPU_MODEL_MERCED) {
        int cached = ia64_merced_dtlb1_lookup(env, va, rid);

        if (cached >= 0) {
            return &env->mmu.tlb_data_l1[cached];
        }
        for (i = 0; i < IA64_DTLB1_MAX; i++) {
            IA64TlbEntry *entry = &env->mmu.tlb_data_l1[i];

            if (ia64_tlb_match(entry, va, rid)) {
                return entry;
            }
        }
    }

    for (i = 0; i < tlb_count; i++) {
        IA64TlbEntry *entry = &tlb[i];

        /*
         * Main TLB entries are validated when inserted or loaded, so a
         * valid entry always has a nonzero page size.  Check the more
         * selective RID before valid: active operating-system TLBs are
         * dense, while a substantial fraction belongs to another RID.
         */
        if (entry->rid == rid && entry->valid &&
            ((va ^ entry->va) & entry->page_mask) == 0) {
            micro[ia64_micro_tlb_index(va, rid)] = (IA64MicroTlbEntry) {
                .va = entry->va,
                .page_mask = entry->page_mask,
                .pte = entry->pte,
                .rid = entry->rid,
                .generation = generation,
                .slot_generation = entry->micro_generation,
                .slot = i,
                .valid = true,
            };
            return entry;
        }
    }
    return NULL;
}

static void ia64_cpu_synchronize_from_tb(CPUState *cs,
                                         const TranslationBlock *tb)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    if (tb->flags & IA64_TB_FLAG_PSR_IS) {
        tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
        cpu->env.ia32.eip = (uint32_t)(tb->pc - tb->cs_base);
        cpu->env.ip = (uint32_t)tb->pc;
        return;
    }

    uint64_t ri =
        (tb->flags & IA64_TB_FLAG_RI_MASK) >> IA64_TB_FLAG_RI_SHIFT;

    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu->env.ip = tb->pc;
    /*
     * Translation-time instruction fetch faults occur before generated TCG
     * can update PSR.ri.  Restore the slot encoded in the TB key along with
     * its bundle address; otherwise a stale slot from the preceding TB is
     * saved in IPSR and rfi can skip the faulting bundle's prologue.
     */
    cpu->env.psr = (cpu->env.psr & ~IA64_PSR_RI_MASK) |
                   (ri << IA64_PSR_RI_SHIFT);
}

static void ia64_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    if (tb->flags & IA64_TB_FLAG_PSR_IS) {
        CPUX86State *xenv = &cpu->env.ia32;
        uint64_t new_pc;

        if (tb_cflags(tb) & CF_PCREL) {
            uint64_t pc = xenv->eip + tb->cs_base;

            new_pc = (pc & TARGET_PAGE_MASK) | data[0];
        } else {
            new_pc = data[0];
        }
        xenv->eip = (uint32_t)(new_pc - tb->cs_base);
        cpu->env.ip = (uint32_t)new_pc;
        if (data[1] != CC_OP_DYNAMIC) {
            xenv->cc_op = data[1];
        }
        return;
    }

    cpu->env.ip = data[0];
}

static int ia64_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    if (cpu->env.psr & (ifetch ? IA64_PSR_IT : IA64_PSR_DT)) {
        return MMU_IDX_VIRT_CPL(ia64_psr_cpl(cpu->env.psr));
    }
    return MMU_PHYS_IDX;
}

static vaddr ia64_pointer_wrap(CPUState *cs, int mmu_idx,
                               vaddr result, vaddr base)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    return cpu->env.psr & IA64_PSR_IS ? (uint32_t)result : result;
}


static int ia64_tlb_perm_to_prot(uint8_t perm)
{
    int prot = 0;

    if (perm & IA64_TLB_R) {
        prot |= PAGE_READ;
    }
    if (perm & IA64_TLB_W) {
        prot |= PAGE_WRITE;
    }
    if (perm & IA64_TLB_X) {
        prot |= PAGE_EXEC;
    }
    return prot;
}

static int ia64_tlb_prot_for_pte_psr(uint64_t pte, uint8_t perm,
                                     bool is_ifetch, uint64_t psr)
{
    int prot = ia64_tlb_perm_to_prot(perm);

    /* IA-64 has independent instruction and data translation caches. */
    prot &= is_ifetch ? PAGE_EXEC : (PAGE_READ | PAGE_WRITE);

    /*
     * QEMU's software TLB may satisfy later accesses without re-entering
     * tlb_fill.  Do not cache write permission for a clean IA-64 PTE: a
     * later store must take Data Dirty so the OS can update the PTE or break
     * copy-on-write sharing.
     */
    if (!is_ifetch && !(psr & IA64_PSR_DA)) {
        if (!(pte & IA64_PTE_ACCESSED)) {
            prot &= ~(PAGE_READ | PAGE_WRITE);
        }
        if (!(pte & IA64_PTE_DIRTY)) {
            prot &= ~PAGE_WRITE;
        }
    } else if (is_ifetch && !(psr & IA64_PSR_IA) &&
               !(pte & IA64_PTE_ACCESSED)) {
        prot &= ~PAGE_EXEC;
    }

    return prot;
}

static int ia64_tlb_prot_for_pte(CPUIA64State *env, uint64_t pte,
                                 uint8_t perm, bool is_ifetch)
{
    return ia64_tlb_prot_for_pte_psr(pte, perm, is_ifetch, env->psr);
}

static void ia64_record_suppressed_tlb_fill(CPUIA64State *env, vaddr addr,
                                             int mmu_idx)
{
    uint64_t page = addr & TARGET_PAGE_MASK;
    uint16_t idxmap = 1u << mmu_idx;
    uint8_t i;

    for (i = 0; i < env->exception_state.suppressed_tlb_count; i++) {
        if (env->exception_state.suppressed_tlb_pages[i] == page) {
            env->exception_state.suppressed_tlb_idxmaps[i] |= idxmap;
            return;
        }
    }

    if (env->exception_state.suppressed_tlb_count == IA64_SUPPRESSED_TLB_MAX) {
        env->exception_state.suppressed_tlb_overflow = true;
        return;
    }

    i = env->exception_state.suppressed_tlb_count++;
    env->exception_state.suppressed_tlb_pages[i] = page;
    env->exception_state.suppressed_tlb_idxmaps[i] = idxmap;
}

static void ia64_record_suppressed_tlb_fill_if_needed(
    CPUIA64State *env, vaddr addr, int mmu_idx, uint64_t pte, uint8_t perm,
    bool is_ifetch, int prot)
{
    uint64_t unsuppressed_psr = env->psr & ~(IA64_PSR_DA | IA64_PSR_IA);
    int unsuppressed_prot;

    if (!(env->psr & (IA64_PSR_DA | IA64_PSR_IA))) {
        return;
    }

    unsuppressed_prot = ia64_tlb_prot_for_pte_psr(
        pte, perm, is_ifetch, unsuppressed_psr);
    if (prot != unsuppressed_prot) {
        ia64_record_suppressed_tlb_fill(env, addr, mmu_idx);
    }
}

void ia64_flush_suppressed_tlb(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    uint8_t i;

    if (env->exception_state.suppressed_tlb_overflow) {
        tlb_flush(cs);
    } else {
        for (i = 0; i < env->exception_state.suppressed_tlb_count; i++) {
            tlb_flush_page_by_mmuidx(
                cs, env->exception_state.suppressed_tlb_pages[i],
                env->exception_state.suppressed_tlb_idxmaps[i]);
        }
    }

    env->exception_state.suppressed_tlb_count = 0;
    env->exception_state.suppressed_tlb_overflow = false;
}

static void ia64_tlb_set_entry_page(CPUState *cs, vaddr addr, hwaddr pa,
                                    uint64_t page_size, int prot, int mmu_idx,
                                    IA64MemorySpeculation speculation,
                                    uint8_t memory_attribute)
{
    CPUTLBEntryFull full = {
        .phys_addr = pa & TARGET_PAGE_MASK,
        .attrs = MEMTXATTRS_UNSPECIFIED,
        .prot = prot,
        .lg_page_size = TARGET_PAGE_BITS,
    };

    (void)page_size;
    full.extra.ia64.speculation = speculation;
    full.extra.ia64.memory_attribute = memory_attribute;
    /* UC and UCE accesses to a peripheral domain are sequential. */
    if (memory_attribute == IA64_PTE_MA_UC ||
        memory_attribute == IA64_PTE_MA_UCE) {
        full.tlb_fill_flags |= TLB_FORCE_ST_LD;
    }
    tlb_set_page_full(cs, mmu_idx, addr & TARGET_PAGE_MASK, &full);
}

static hwaddr ia64_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    uint64_t pa;

    if (!ia64_mmu_translate_debug(&cpu->env, addr, &pa)) {
        return -1;
    }
    return pa & TARGET_PAGE_MASK;
}

static bool ia64_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                              MMUAccessType access_type, int mmu_idx,
                              bool probe, uintptr_t retaddr)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    bool is_ifetch = (access_type == MMU_INST_FETCH);
    uint8_t needed = is_ifetch ? IA64_TLB_X :
                     (access_type == MMU_DATA_STORE ? IA64_TLB_W :
                      IA64_TLB_R);
    uint64_t pa;
    uint8_t perm;
    uint32_t rid;
    IA64Exception excp;
    bool is_rse = !is_ifetch && cpu->env.rse.rse_access;
    uint8_t access_level;
    bool virt_translation_enabled;

    if (!probe && is_ifetch && (cpu->env.psr & IA64_PSR_IS) &&
        (uint32_t)addr == ia64_ia32_virtual_ip(&cpu->env)) {
        /*
         * The first executable-page lookup happens before x86 decoding.
         * Preserve the architectural ordering of IA-32 instruction
         * breakpoint and code-fetch faults ahead of instruction TLB faults.
         */
        ia64_ia32_check_fetch_fault_priority(&cpu->env, addr, 0);
    }

    rid = ia64_region_rid(&cpu->env, addr);
    if (mmu_idx == MMU_PHYS_IDX) {
        if (!ia64_pa_is_implemented(&cpu->env, addr)) {
            if (probe) {
                return false;
            }
            excp = is_ifetch ? IA64_EXCP_UNIMPL_INST_ADDR :
                   IA64_EXCP_UNIMPL_DATA_ADDR;
            if (is_ifetch) {
                cpu->env.ip = ia64_pa_canonicalize(&cpu->env, addr);
            }
            goto raise_exception;
        }
        pa = ia64_physical_address(addr);
        ia64_tlb_set_entry_page(
            cs, addr, pa, TARGET_PAGE_SIZE,
            PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
            (addr & IA64_PHYS_UC_BIT) ? IA64_MEM_NON_SPECULATIVE :
                                       IA64_MEM_LIMITED_SPECULATION,
            (addr & IA64_PHYS_UC_BIT) ? 4 : 0);
        return true;
    }

    if (is_rse) {
        access_level = ia64_rsc_pl(cpu->env.ar_rsc);
    } else {
        g_assert(mmu_idx >= MMU_IDX_VIRT_CPL0 &&
                 mmu_idx <= MMU_IDX_VIRT_CPL3);
        access_level = mmu_idx - MMU_IDX_VIRT_CPL0;
    }

    /* A translated MMU index is itself the serialized translation state. */
    virt_translation_enabled = true;
    if (virt_translation_enabled &&
        !ia64_va_is_implemented(&cpu->env, addr)) {
        if (probe) {
            return false;
        }
        excp = is_ifetch ? IA64_EXCP_UNIMPL_INST_ADDR :
               IA64_EXCP_UNIMPL_DATA_ADDR;
        if (is_ifetch) {
            cpu->env.ip = ia64_va_canonicalize(&cpu->env, addr);
        }
        goto raise_exception;
    }

    if (ia64_firmware_identity_pa(cpu->env.cr_iva,
                                  is_ifetch ? addr : cpu->env.ip,
                                  cpu->env.psr, addr, &pa)) {
        int prot = is_ifetch ? PAGE_EXEC : (PAGE_READ | PAGE_WRITE);

        ia64_tlb_set_entry_page(cs, addr, pa, TARGET_PAGE_SIZE, prot,
                                mmu_idx, IA64_MEM_SPECULATIVE, 0);
        return true;
    }

    {
        const IA64TlbEntry *entry = ia64_tlb_find_cached(
            &cpu->env, addr, rid, is_ifetch);

        if (entry) {
            int prot;
            IA64Exception pte_excp;

            ia64_tlb_entry_translate(entry, addr, access_level, &pa, &perm);
            pte_excp = ia64_tlb_exception_for_access(
                &cpu->env, entry, perm, needed, is_ifetch,
                access_type == MMU_DATA_STORE, is_rse);
            if (pte_excp != IA64_EXCP_NONE) {
                if (probe) {
                    return false;
                }
                excp = pte_excp;
                goto raise_exception;
            }
            prot = ia64_tlb_prot_for_pte(&cpu->env, entry->pte, perm,
                                         is_ifetch);
            ia64_record_suppressed_tlb_fill_if_needed(
                &cpu->env, addr, mmu_idx, entry->pte, perm, is_ifetch, prot);
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 tlb hit %c va=0x%016" PRIx64
                          " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                          " perm=0x%x\n",
                          is_ifetch ? 'i' : 'd', (uint64_t)addr, rid, pa,
                          perm);
            /*
             * An L1 replacement can flush a derived softmmu entry.  Do it
             * before installing this access's host entry so the fill caller
             * never resumes through an entry invalidated during the fill.
             */
            if (!is_ifetch && !probe) {
                ia64_mmu_data_access(&cpu->env, addr, size, true);
            }
            ia64_tlb_set_entry_page(
                cs, addr, pa, entry->ps, prot, mmu_idx,
                ia64_pte_memory_speculation(entry->pte),
                (entry->pte >> 2) & 7);
            return true;
        }
    }

    if (!is_ifetch) {
        const IA64TlbEntry *new_entry;
        uint64_t pte = 0;
        uint32_t key = 0;

        if (ia64_vhpt_walk_full(&cpu->env, addr, rid, false, is_rse,
                                access_level, &pa, &perm, &pte, &key,
                                &new_entry)) {
            int prot;
            IA64Exception pte_excp;
            uint64_t page_size = new_entry ? new_entry->ps : TARGET_PAGE_SIZE;

            pte_excp = new_entry ?
                ia64_tlb_exception_for_access(
                    &cpu->env, new_entry, perm, needed, false,
                    access_type == MMU_DATA_STORE, is_rse) :
                ia64_translation_exception_for_access(
                    &cpu->env, pte, key, perm, needed, false,
                    access_type == MMU_DATA_STORE, is_rse);
            if (pte_excp != IA64_EXCP_NONE) {
                if (probe) {
                    return false;
                }
                excp = pte_excp;
                goto raise_exception;
            }
            prot = ia64_tlb_prot_for_pte(&cpu->env,
                                         new_entry ? new_entry->pte : pte,
                                         perm, false);
            ia64_record_suppressed_tlb_fill_if_needed(
                &cpu->env, addr, mmu_idx,
                new_entry ? new_entry->pte : pte, perm, false, prot);
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt hit d va=0x%016" PRIx64
                          " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                          " perm=0x%x iha=0x%016" PRIx64 "\n",
                          (uint64_t)addr, rid, pa, perm,
                          ia64_vhpt_hash_address(&cpu->env, addr));
            if (!probe) {
                ia64_mmu_data_access(&cpu->env, addr, size, true);
            }
            ia64_tlb_set_entry_page(
                cs, addr, pa, page_size, prot, mmu_idx,
                ia64_pte_memory_speculation(new_entry ? new_entry->pte :
                                                        pte),
                ((new_entry ? new_entry->pte : pte) >> 2) & 7);
            return true;
        }
    }

    if (is_ifetch) {
        const IA64TlbEntry *new_entry;
        uint64_t pte = 0;
        uint32_t key = 0;

        if (ia64_vhpt_walk_full(&cpu->env, addr, rid, true, false,
                                access_level, &pa, &perm, &pte, &key,
                                &new_entry)) {
            int prot;
            IA64Exception pte_excp;
            uint64_t page_size = new_entry ? new_entry->ps : TARGET_PAGE_SIZE;

            pte_excp = new_entry ?
                ia64_tlb_exception_for_access(
                    &cpu->env, new_entry, perm, needed, true, false, false) :
                ia64_translation_exception_for_access(
                    &cpu->env, pte, key, perm, needed, true, false, false);
            if (pte_excp != IA64_EXCP_NONE) {
                if (probe) {
                    return false;
                }
                excp = pte_excp;
                goto raise_exception;
            }
            prot = ia64_tlb_prot_for_pte(&cpu->env,
                                         new_entry ? new_entry->pte : pte,
                                         perm, true);
            ia64_record_suppressed_tlb_fill_if_needed(
                &cpu->env, addr, mmu_idx,
                new_entry ? new_entry->pte : pte, perm, true, prot);
            qemu_log_mask(CPU_LOG_MMU,
                          "ia64 vhpt hit i va=0x%016" PRIx64
                          " rid=0x%06" PRIx32 " pa=0x%016" PRIx64
                          " perm=0x%x iha=0x%016" PRIx64 "\n",
                          (uint64_t)addr, rid, pa, perm,
                          ia64_vhpt_hash_address(&cpu->env, addr));
            ia64_tlb_set_entry_page(
                cs, addr, pa, page_size, prot, mmu_idx,
                ia64_pte_memory_speculation(new_entry ? new_entry->pte :
                                                        pte),
                ((new_entry ? new_entry->pte : pte) >> 2) & 7);
            return true;
        }
    }
    if (probe) {
        return false;
    }

    {
        uint64_t vhpt_entry_va;
        uint8_t vhpt_size;
        bool vhpt_long_format;
        bool vhpt_enabled = ia64_vhpt_walker_enabled(&cpu->env, addr,
                                                     is_ifetch, is_rse,
                                                     &vhpt_size,
                                                     &vhpt_long_format);

        if (!is_ifetch && ia64_data_nested_tlb_active(&cpu->env)) {
            excp = IA64_EXCP_DATA_NESTED_TLB;
        } else if (vhpt_enabled &&
                   ia64_vhpt_pte_not_present(&cpu->env, addr, is_ifetch,
                                             is_rse, &vhpt_entry_va)) {
            excp = IA64_EXCP_PAGE_NOT_PRESENT;
        } else if (!ia64_vhpt_entry_accessible(&cpu->env, addr, is_ifetch,
                                               is_rse, &vhpt_entry_va)) {
            excp = IA64_EXCP_VHPT_FAULT;
        } else if (vhpt_enabled) {
            excp = is_ifetch ? IA64_EXCP_ITLB_FAULT : IA64_EXCP_DTLB_FAULT;
        } else {
            excp = is_ifetch ? IA64_EXCP_ALT_ITLB : IA64_EXCP_ALT_DTLB;
        }
    }
raise_exception:
    /*
     * A softmmu access can fault after translated code has advanced beyond
     * the last architecturally synchronized bundle.  Restore the precise
     * instruction before deriving IA-64 state such as IPSR.ri and ISR.ed.
     * cpu_loop_exit_restore() would otherwise do this only after those
     * fields had already been computed.
     */
    if (retaddr) {
        cpu_restore_state(cs, retaddr);
        retaddr = 0;
    }
    if (cpu->env.psr & IA64_PSR_IS) {
        cpu->env.ip = ia64_ia32_virtual_ip(&cpu->env);
        cpu->env.exception_state.fault_ip = cpu->env.ip;
    }
    if (is_ifetch && excp == IA64_EXCP_PAGE_NOT_PRESENT &&
        (cpu->env.psr & IA64_PSR_IC) &&
        !(cpu->env.psr & IA64_PSR_IS)) {
        /*
         * IIP receives IP on interruption entry, and for faults it must point
         * at the faulting instruction bundle when interruption collection is
         * enabled.  Instruction fetch page-not-present faults may be raised
         * while looking up the next TB, before env->ip has otherwise advanced
         * to the fetched bundle.
         */
        cpu->env.ip = ia64_ip_bundle_addr(addr);
    }
    /*
     * IPSR.ri must name the slot execution resumes at.  PSR.ri holds
     * the current slot for data references and, for instruction
     * fetches, the slot the fetch will resume at (0 after a branch;
     * the interrupted slot when refetching after an rfi).  Without
     * this, an instruction-fetch fault would reuse a stale fault_slot
     * and the handler's rfi would skip slots of the target bundle.
     */
    cpu->env.exception_state.fault_slot =
        cpu->env.psr & IA64_PSR_IS ? 0 :
        (cpu->env.psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT;
    if (cpu->env.psr & IA64_PSR_IC) {
        cpu->env.cr_ifa = is_ifetch && (cpu->env.psr & IA64_PSR_IS) ?
                          addr & ~0xfULL : addr;
        if (ia64_exception_initializes_iha(excp)) {
            cpu->env.cr_iha = ia64_vhpt_hash_address(&cpu->env, addr);
        }
        cpu->env.cr_itir = ia64_region_itir(
            &cpu->env, excp == IA64_EXCP_VHPT_FAULT ? cpu->env.cr_iha : addr);
    }
    if (excp != IA64_EXCP_DATA_NESTED_TLB) {
        if (excp == IA64_EXCP_UNIMPL_DATA_ADDR) {
            cpu->env.cr_isr = IA64_GENEX_UNIMPL_DATA_ADDR |
                              (access_type == MMU_DATA_STORE ?
                               IA64_ISR_W : IA64_ISR_R);
        } else if (excp == IA64_EXCP_UNIMPL_INST_ADDR) {
            cpu->env.cr_isr = IA64_GENEX_UNIMPL_INST_ADDR | IA64_ISR_X;
        } else {
            cpu->env.cr_isr = is_ifetch ? IA64_ISR_X :
                              (access_type == MMU_DATA_STORE ? IA64_ISR_W :
                               IA64_ISR_R);
            if (excp == IA64_EXCP_NAT_CONSUMPTION) {
                /*
                 * NaT Page Consumption reports ISR.code{5:4} = 2; the
                 * non-access code in ISR.code{3:0} is zero for an access.
                 */
                cpu->env.cr_isr |= IA64_ISR_CODE_NAT_PAGE;
            }
        }
        if (is_rse) {
            cpu->env.cr_isr |= IA64_ISR_RS;
            if (cpu->env.rse.rse_dirty < 0 || cpu->env.rse.rse_dirty_nat < 0) {
                /* Mandatory load for an incomplete frame (SDM 6.8). */
                cpu->env.cr_isr |= IA64_ISR_IR;
            }
        } else if (!is_ifetch && excp != IA64_EXCP_NAT_CONSUMPTION &&
                   ia64_current_code_tlb_ed(&cpu->env)) {
            /* NaT Page Consumption always reports ISR.ed as 0. */
            cpu->env.cr_isr |= IA64_ISR_ED;
        }
    }
    qemu_log_mask(CPU_LOG_MMU,
                  "ia64 tlb miss %c va=0x%016" PRIx64
                  " rid=0x%06" PRIx32 " ps=0x%016" PRIx64
                  " iha=0x%016" PRIx64 " pta=0x%016" PRIx64
                  " isr=0x%016" PRIx64 "\n",
                  is_ifetch ? 'i' :
                  (access_type == MMU_DATA_STORE ? 'w' : 'r'),
                  (uint64_t)addr, rid, cpu->env.cr_itir,
                  cpu->env.cr_iha, cpu->env.cr_pta, cpu->env.cr_isr);
    cs->exception_index = excp;
    if (is_rse) {
        cpu->env.rse.rse_access = false;
    }
    if (cpu->env.psr & IA64_PSR_IS) {
        cpu_loop_exit(cs);
    }
    cpu_loop_exit_restore(cs, retaddr);
}


void ia64_cpu_set_boot_info(IA64CPU *cpu, const IA64BootInfo *info)
{
    cpu->boot_info = *info;
    cpu->boot_info_valid = true;
    cpu->boot_info_pending = true;
    CPU(cpu)->start_powered_off = info->powered_off;
}

void ia64_cpu_reset_to_boot_info(IA64CPU *cpu)
{
    g_assert(cpu->boot_info_valid);
    cpu->boot_info_pending = true;
    cpu_reset(CPU(cpu));
}

static void ia64_cpu_set_architectural_reset_rse(CPUIA64State *env)
{
    /*
     * SDM Vol.2 6.12: reset exposes all 96 stacked registers in one frame,
     * with BOF at physical GR32.  rse_bol is zero-based within the physical
     * stacked-register file, so zero denotes physical GR32.
     */
    env->cfm_sof = IA64_STACKED_GR_COUNT;
    env->cfm_sol = 0;
    env->cfm_sor = 0;
    env->cfm_rrb_gr = 0;
    env->cfm_rrb_fr = 0;
    env->cfm_rrb_pr = 0;
    env->rse.rse_bol = 0;
    env->rse.rse_dirty = 0;
    env->rse.rse_dirty_nat = 0;
    env->rse.rse_clean = 0;
    env->rse.rse_clean_nat = 0;
    env->rse.rse_invalid = 0;
}

static void ia64_cpu_set_boot_handoff_rse(CPUIA64State *env)
{
    /*
     * IA64BootInfo is a synthetic post-reset firmware handoff, not the
     * architectural processor-reset state.  Preserve its historical empty
     * frame so the firmware and loader microprogram ABI starts with all 96
     * physical stacked registers in the invalid partition.
     */
    env->cfm_sof = 0;
    env->cfm_sol = 0;
    env->cfm_sor = 0;
    env->cfm_rrb_gr = 0;
    env->cfm_rrb_fr = 0;
    env->cfm_rrb_pr = 0;
    env->rse.rse_bol = 0;
    env->rse.rse_dirty = 0;
    env->rse.rse_dirty_nat = 0;
    env->rse.rse_clean = 0;
    env->rse.rse_clean_nat = 0;
    env->rse.rse_invalid = IA64_STACKED_GR_COUNT;
}

static void ia64_cpu_apply_boot_info(IA64CPU *cpu)
{
    CPUIA64State *env = &cpu->env;
    const IA64BootInfo *info = &cpu->boot_info;

    if (!cpu->boot_info_valid || !cpu->boot_info_pending) {
        return;
    }
    cpu->boot_info_pending = false;

    ia64_cpu_set_boot_handoff_rse(env);
    env->psr = 0;
    env->ip = info->firmware_entry;
    env->br[IA64_BR_RETURN_LINK] = info->firmware_entry;
    env->cr_iva = info->iva;
    /*
     * Start with the VHPT walker disabled and the architected minimum table
     * size.  A size smaller than 15 is a reserved PTA encoding.
     */
    env->cr_pta = 15ULL << IA64_PTA_SIZE_SHIFT;
    env->cr_dcr = IA64_DCR_DM | IA64_DCR_DP;
    env->ar_kr0 = info->platform_addresses_valid ?
        info->io_port_base :
        ia64_cpu_default_io_block_pa(IA64_CPU_GET_CLASS(cpu));
    env->ar_kr7 = 0;
    env->ar_rsc = info->rsc;
    env->ar_bsp = info->bsp;
    env->ar_bspstore = info->bsp;
    env->ar_rnat = 0;
    ia64_rse_rnat_undefined(env, "reset");
    env->gr[IA64_GR_STACK_POINTER] = info->stack_pointer;
    env->gr[IA64_GR_GLOBAL_POINTER] = info->global_pointer;
    if (info->firmware_args_valid) {
        env->gr[IA64_GR_RETURN0] = info->firmware_arg0;
        env->gr[IA64_GR_RETURN1] = info->firmware_arg1;
        env->gr[IA64_GR_RETURN2] = info->firmware_arg2;
    }
    if (info->platform_addresses_valid) {
        env->pal.pal_io_block_addr = info->io_port_base;
        env->pal.pal_interrupt_block_addr = info->interrupt_block_base;
    }
    env->interrupt.pal_halt_wake = info->powered_off;
    env->ar_fpsr = IA64_FPSR_DEFAULT;
    set_float_rounding_mode(float_round_nearest_even, &env->fp.fp_status);
    set_flush_to_zero(false, &env->fp.fp_status);
    set_flush_inputs_to_zero(false, &env->fp.fp_status);
    set_default_nan_mode(false, &env->fp.fp_status);
}

static void ia64_cpu_reset_hold(Object *obj, ResetType type)
{
    IA64CPUClass *icc = IA64_CPU_GET_CLASS(obj);
    IA64CPU *cpu = IA64_CPU(obj);

    if (icc->parent_phases.hold) {
        icc->parent_phases.hold(obj, type);
    }

    if (cpu->itm_timer != NULL) {
        timer_del(cpu->itm_timer);
    }
    g_assert(!cpu->env.alat_state.write_active);
    memset(&cpu->env, 0, sizeof(cpu->env));
    cpu->mca_rse_valid = false;
    cpu->env.alat_state.alat_full = cpu->alat_full;
    cpu->env.fp.fr[IA64_FR_ONE_INDEX] = IA64_FR_ONE;
    cpu->env.pr[IA64_PR_TRUE] = 1;
    cpu->env.psr = 0;
    cpu->env.ar_rsc = 0;
    ia64_cpu_set_architectural_reset_rse(&cpu->env);
    ia64_rse_rnat_undefined(&cpu->env, "processor reset");
    cpu->env.ar_fpsr = IA64_FPSR_DEFAULT;
    cpu->env.cr_iva = 0;
    cpu->env.instruction_group_start = true;
    ia64_itc_write(&cpu->env, 0);
    set_float_2nan_prop_rule(float_2nan_prop_ab, &cpu->env.fp.fp_status);
    set_float_3nan_prop_rule(float_3nan_prop_abc, &cpu->env.fp.fp_status);
    set_float_infzeronan_rule(float_infzeronan_dnan_never |
                              float_infzeronan_suppress_invalid,
                              &cpu->env.fp.fp_status);
    set_float_default_nan_pattern(0b11000000, &cpu->env.fp.fp_status);
    cpu->env.cr[IA64_CR_SAPIC_LID] =
        ia64_sapic_lid(MAX(CPU(cpu)->cpu_index, 0), 0);
    cpu->env.cr[IA64_CR_SAPIC_TPR] = 0;
    cpu->env.cr[IA64_CR_ITV] = IA64_VECTOR_MASKED;
    cpu->env.cr[IA64_CR_PMV] = IA64_VECTOR_MASKED;
    cpu->env.cr[IA64_CR_CMCV] = IA64_VECTOR_MASKED;
    cpu->env.interrupt.sapic_xtp = IA64_SAPIC_XTP_DISABLE;
    if (icc->model == IA64_CPU_MODEL_MERCED) {
        cpu->env.pmc[8] = 0xf00000003ffffff8ULL;
        cpu->env.pmc[9] = 0xf00000003ffffff8ULL;
        cpu->env.pmc[11] = 1ULL << 28;
        cpu->env.pmc[13] = 1;
    }
    cpu->env.pal.pal_bus_feature_status = 0;
    cpu->env.pal.pal_proc_feature_status =
        icc->pal_proc_feature_available;
    cpu->env.pal.pal_proc_copy_valid = false;
    cpu->env.pal.pal_proc_copy_addr = 0;
    cpu->env.pal.pal_interrupt_block_addr = IA64_LOCAL_SAPIC_PA;
    cpu->env.pal.pal_io_block_addr =
        ia64_cpu_default_io_block_pa(icc);
    ia64_cpu_apply_boot_info(cpu);
}

static ObjectClass *ia64_cpu_class_by_name(const char *cpu_model)
{
    char *typename;
    ObjectClass *oc;

    typename = g_strdup_printf(IA64_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

typedef struct IA64QTestStaleVictimWork {
    uint64_t va;
    uint64_t old_pa;
    uint64_t new_pa;
    uint64_t value;
    uint64_t probe_result;
    bool model_ready;
} IA64QTestStaleVictimWork;

typedef struct IA64QTestAlatWriterWork {
    bool model_ready;
    bool full_model;
    bool setup_hit;
    bool active_hit;
    bool active_alloc_hit;
    uint32_t active_alloc_count;
    bool memory_write_ok;
    bool smp_hit;
} IA64QTestAlatWriterWork;

typedef struct IA64QTestRasMinStateWork {
    uint64_t result;
} IA64QTestRasMinStateWork;

typedef struct IA64QTestSapicWork {
    const char *operation;
    uint64_t value;
    uint64_t address;
    unsigned int size;
    int64_t result;
} IA64QTestSapicWork;

#define IA64_QTEST_ALAT_VA  UINT64_C(0x8000)
#define IA64_QTEST_ALAT_PA  (UINT64_C(8) * MiB)
#define IA64_QTEST_ALAT_REG 22
#define IA64_QTEST_MCA_SAVE_PA (UINT64_C(16) * MiB)

enum {
    IA64_QTEST_RAS_ENTERED = BIT(0),
    IA64_QTEST_RAS_ENTRY_CFM = BIT(1),
    IA64_QTEST_RAS_ENTRY_CONTROL = BIT(2),
    IA64_QTEST_RAS_HANDOFF = BIT(3),
    IA64_QTEST_RAS_RESTORED = BIT(4),
    IA64_QTEST_RAS_STATIC_STATE = BIT(5),
    IA64_QTEST_RAS_BANK_VALUES = BIT(6),
    IA64_QTEST_RAS_BANK_NATS = BIT(7),
    IA64_QTEST_RAS_RSE_STATE = BIT(8),
    IA64_QTEST_RAS_SNAPSHOT_CONSUMED = BIT(9),
    IA64_QTEST_RAS_SECOND_LEVEL_STATE = BIT(10),
    IA64_QTEST_RAS_NEW_CONTEXT = BIT(11),
    IA64_QTEST_RAS_PENDING_REDRIVE = BIT(12),
    IA64_QTEST_RAS_INIT_ENTRY = BIT(13),
    IA64_QTEST_RAS_INIT_RESUME = BIT(14),
    IA64_QTEST_RAS_MCA_MASKED = BIT(15),
    IA64_QTEST_RAS_MCA_UNMASKED = BIT(16),
};

static void ia64_qtest_ras_min_state_work(CPUState *cs,
                                           run_on_cpu_data data)
{
    IA64QTestRasMinStateWork *work = data.host_ptr;
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    const uint64_t save_address =
        IA64_PHYS_UC_BIT | IA64_QTEST_MCA_SAVE_PA;
    const uint64_t original_psr = IA64_PSR_BE | IA64_PSR_MFL |
        IA64_PSR_IC | IA64_PSR_I | IA64_PSR_PK | IA64_PSR_BN;
    const uint64_t original_rsc = IA64_RSC_MODE | IA64_RSC_BE;
    const uint16_t bank0_nats = 0x5555;
    const uint16_t bank1_nats = 0xaaaa;
    uint64_t original_bsp;
    uint64_t original_bspstore;
    uint32_t original_bol;
    int32_t original_dirty;
    int32_t original_invalid;
    uint64_t image[128];
    unsigned int i;

    if (env->cfm_sof != 0 || env->rse.rse_invalid !=
        IA64_STACKED_GR_COUNT) {
        return;
    }

    ia64_set_psr(env, 0);
    for (i = 1; i < 16; i++) {
        env->gr[i] = UINT64_C(0x1100000000000000) + i;
        ia64_gr_nat_set(env, i, (i & 3) == 1);
    }
    for (i = 0; i < 16; i++) {
        env->gr[16 + i] = UINT64_C(0x2200000000000000) + i;
        ia64_gr_nat_set(env, 16 + i, (bank0_nats >> i) & 1);
        env->banked_gr[i] = UINT64_C(0x3300000000000000) + i;
    }
    env->banked_nat = bank1_nats;
    ia64_system_write_pr(env, UINT64_C(0x25), UINT64_MAX);
    env->br[IA64_BR_RETURN_LINK] = UINT64_C(0x4400000000000000);
    env->br[IA64_BR_STATIC0] = UINT64_C(0x5500000000000000);
    env->ip = UINT64_C(0x123450);
    env->ar_rsc = original_rsc;
    ia64_rse_alloc(env, 0, 4 | (2 << IA64_CFM_SOL_SHIFT), 0, 0, 0);
    for (i = 0; i < 4; i++) {
        env->gr[IA64_STACKED_GR_BASE + i] =
            UINT64_C(0x6600000000000000) + i;
        ia64_gr_nat_set(env, IA64_STACKED_GR_BASE + i, i == 1);
        ia64_rse_mark_gr_dirty(env, IA64_STACKED_GR_BASE + i);
    }
    ia64_set_psr(env, original_psr);

    original_bsp = env->ar_bsp;
    original_bspstore = env->ar_bspstore;
    original_bol = env->rse.rse_bol;
    original_dirty = env->rse.rse_dirty;
    original_invalid = env->rse.rse_invalid;
    env->pal.pal_mc_save_addr = save_address;
    env->pal.pal_mca_entry = UINT64_C(0x100000);
    env->pal.pal_mca_gp = UINT64_C(0x700000);
    env->pal.pal_mca_pending_record_id = UINT64_C(0x778899);
    env->pal.pal_mc_severity = 1;
    env->pal.pal_mc_state_parameter = UINT64_C(0x12340000);
    env->pal.pal_mca_pending = true;
    env->pal.pal_mca_active = false;

    /* Masking must neither enter MCA nor discard the pending record. */
    ia64_set_psr(env, (original_psr | IA64_PSR_MC) & ~IA64_PSR_I);
    if (ia64_ras_enter_mca(cpu) || !env->pal.pal_mca_pending ||
        env->pal.pal_mca_active || cpu->mca_rse_valid ||
        cpu_test_interrupt(cs, CPU_INTERRUPT_HARD) || ia64_cpu_has_work(cs)) {
        return;
    }
    work->result |= IA64_QTEST_RAS_MCA_MASKED;

    /* Unmasking reasserts the request even with ordinary interrupts off. */
    ia64_set_psr(env, original_psr & ~IA64_PSR_I);
    if (!cpu_test_interrupt(cs, CPU_INTERRUPT_HARD) ||
        !ia64_cpu_has_work(cs)) {
        return;
    }
    work->result |= IA64_QTEST_RAS_MCA_UNMASKED;
    ia64_set_psr(env, original_psr);

    if (!ia64_ras_enter_mca(cpu)) {
        return;
    }
    work->result |= IA64_QTEST_RAS_ENTERED;
    if (ia64_rse_current_cfm(env) == 0 &&
        env->ar_bsp != original_bsp) {
        work->result |= IA64_QTEST_RAS_ENTRY_CFM;
    }
    if (env->psr == (IA64_PSR_MC | IA64_PSR_MFL | IA64_PSR_PK |
                     IA64_PSR_BN) &&
        env->ar_rsc == (original_rsc & ~IA64_RSC_MODE)) {
        work->result |= IA64_QTEST_RAS_ENTRY_CONTROL;
    }
    if (env->gr[IA64_RAS_GR_SAL_MIN_STATE] == save_address + 1024 &&
        env->gr[IA64_RAS_GR_PAL_MIN_STATE] == save_address &&
        env->gr[IA64_RAS_GR_PROCESSOR_STATE] == UINT64_C(0x12340000) &&
        env->gr[IA64_RAS_GR_PALE_RETURN] == 0 &&
        env->gr[IA64_RAS_GR_SALE_ENTRY_STATE] == 1) {
        work->result |= IA64_QTEST_RAS_HANDOFF;
    }
    ia64_cpu_request_mca(cs, UINT64_C(0x100000), UINT64_C(0x700000),
                         UINT64_C(0x77889a), 0);
    if (ia64_ras_enter_mca(cpu) || !env->pal.pal_mca_pending ||
        env->pal.pal_mca_pending_record_id != UINT64_C(0x77889a)) {
        return;
    }

    ia64_rse_alloc(env, 0, 3, 0, 0, 0);
    env->gr[IA64_GR_GLOBAL_POINTER] = 0;
    env->gr[IA64_RAS_GR_SAL_MIN_STATE] = 0;
    env->banked_gr[0] = 0;
    env->ip = 0;
    env->gr[IA64_PAL_GR_INDEX] = 0x1a;
    env->gr[IA64_PAL_GR_ARG1] = 0;
    env->gr[IA64_PAL_GR_ARG2] = save_address;
    env->gr[IA64_PAL_GR_ARG3] = 0;
    if (!(ia64_pal_dispatch(env, 0) & IA64_PAL_DISPATCH_RESUMED)) {
        return;
    }
    work->result |= IA64_QTEST_RAS_RESTORED;

    if (env->psr == original_psr && env->ar_rsc == original_rsc &&
        env->ip == UINT64_C(0x123450) &&
        env->gr[IA64_GR_GLOBAL_POINTER] ==
            UINT64_C(0x1100000000000001) &&
        ia64_gr_nat_get(env, IA64_GR_GLOBAL_POINTER) &&
        env->gr[IA64_GR_RETURN0] == UINT64_C(0x1100000000000008) &&
        env->gr[IA64_GR_RETURN1] == UINT64_C(0x1100000000000009) &&
        !ia64_gr_nat_get(env, IA64_GR_RETURN0) &&
        ia64_gr_nat_get(env, IA64_GR_RETURN1) &&
        ia64_system_read_pr(env) == UINT64_C(0x25) &&
        env->br[IA64_BR_RETURN_LINK] == UINT64_C(0x4400000000000000) &&
        env->br[IA64_BR_STATIC0] == UINT64_C(0x5500000000000000)) {
        work->result |= IA64_QTEST_RAS_STATIC_STATE;
    }
    for (i = 0; i < 16; i++) {
        if (env->banked_gr[i] != UINT64_C(0x2200000000000000) + i ||
            env->gr[16 + i] != UINT64_C(0x3300000000000000) + i) {
            break;
        }
    }
    if (i == 16) {
        work->result |= IA64_QTEST_RAS_BANK_VALUES;
    }
    for (i = 0; i < 16; i++) {
        if (((env->banked_nat >> i) & 1) !=
                ((bank0_nats >> i) & 1) ||
            ia64_gr_nat_get(env, 16 + i) !=
                ((bank1_nats >> i) & 1)) {
            break;
        }
    }
    if (i == 16) {
        work->result |= IA64_QTEST_RAS_BANK_NATS;
    }
    if (ia64_rse_current_cfm(env) ==
            (4 | (2 << IA64_CFM_SOL_SHIFT)) &&
        env->ar_bsp == original_bsp &&
        env->ar_bspstore == original_bspstore &&
        env->rse.rse_bol == original_bol &&
        env->rse.rse_dirty == original_dirty &&
        env->rse.rse_invalid == original_invalid &&
        env->gr[IA64_STACKED_GR_BASE] == UINT64_C(0x6600000000000000) &&
        env->gr[IA64_STACKED_GR_BASE + 1] ==
            UINT64_C(0x6600000000000001) &&
        ia64_gr_nat_get(env, IA64_STACKED_GR_BASE + 1)) {
        work->result |= IA64_QTEST_RAS_RSE_STATE;
    }
    if (!cpu->mca_rse_valid && !env->pal.pal_mca_active) {
        work->result |= IA64_QTEST_RAS_SNAPSHOT_CONSUMED;
    }
    if (!ia64_ras_enter_mca(cpu) ||
        env->pal.pal_mca_active_record_id != UINT64_C(0x77889a)) {
        return;
    }
    env->gr[IA64_PAL_GR_INDEX] = 0x1a;
    env->gr[IA64_PAL_GR_ARG1] = 0;
    env->gr[IA64_PAL_GR_ARG2] = save_address;
    env->gr[IA64_PAL_GR_ARG3] = 0;
    if (!(ia64_pal_dispatch(env, 0) & IA64_PAL_DISPATCH_RESUMED) ||
        env->pal.pal_mca_pending || env->pal.pal_mca_active ||
        cpu->mca_rse_valid) {
        return;
    }
    work->result |= IA64_QTEST_RAS_PENDING_REDRIVE;

    ia64_set_psr(env, original_psr & ~IA64_PSR_IC);
    env->cr_iip = UINT64_C(0xabc000);
    env->cr_ipsr = original_psr;
    env->cr_ifs = IA64_IFS_V | ia64_rse_current_cfm(env);
    if (!ia64_ras_save_min_state(env, save_address) ||
        !ia64_exec_physical_rw(ia64_physical_address(save_address), image,
                               sizeof(image), false)) {
        return;
    }
    if (le64_to_cpu(image[54]) == UINT64_C(0xabc000) &&
        le64_to_cpu(image[55]) == original_psr &&
        le64_to_cpu(image[56]) ==
            (IA64_IFS_V | ia64_rse_current_cfm(env))) {
        work->result |= IA64_QTEST_RAS_SECOND_LEVEL_STATE;
    }

    image[1] = cpu_to_le64(UINT64_C(0x7100000000000001));
    image[49] = cpu_to_le64(UINT64_C(0x7200000000000000));
    image[50] = cpu_to_le64(original_rsc);
    image[51] = cpu_to_le64(UINT64_C(0x730000));
    image[52] = cpu_to_le64(original_psr &
                            ~(IA64_PSR_IC | IA64_PSR_I));
    image[53] = cpu_to_le64(
        IA64_IFS_V | (4 | (2 << IA64_CFM_SOL_SHIFT)));
    image[54] = cpu_to_le64(UINT64_C(0x740000));
    image[55] = cpu_to_le64(original_psr);
    image[56] = cpu_to_le64(
        IA64_IFS_V | (4 | (2 << IA64_CFM_SOL_SHIFT)));
    image[57] = cpu_to_le64(UINT64_C(0x7500000000000000));
    if (!ia64_exec_physical_rw(ia64_physical_address(save_address), image,
                               sizeof(image), true)) {
        return;
    }

    ia64_rse_cover(env);
    env->br[IA64_BR_STATIC0] = UINT64_C(0x7600000000000000);
    env->pal.pal_mca_active = true;
    env->gr[IA64_PAL_GR_INDEX] = 0x1a;
    env->gr[IA64_PAL_GR_ARG1] = 0;
    env->gr[IA64_PAL_GR_ARG2] = save_address;
    env->gr[IA64_PAL_GR_ARG3] = 1;
    if (!(ia64_pal_dispatch(env, 0) & IA64_PAL_DISPATCH_RESUMED)) {
        return;
    }
    if (env->ip == UINT64_C(0x730000) &&
        env->psr == (original_psr & ~(IA64_PSR_IC | IA64_PSR_I)) &&
        env->gr[IA64_GR_GLOBAL_POINTER] ==
            UINT64_C(0x7100000000000001) &&
        env->br[IA64_BR_RETURN_LINK] == UINT64_C(0x7200000000000000) &&
        env->br[IA64_BR_STATIC0] == UINT64_C(0x7600000000000000) &&
        env->cr_iip == UINT64_C(0x740000) &&
        env->cr_ipsr == original_psr &&
        env->cr_ifs ==
            (IA64_IFS_V | (4 | (2 << IA64_CFM_SOL_SHIFT))) &&
        ia64_rse_current_cfm(env) ==
            (4 | (2 << IA64_CFM_SOL_SHIFT)) &&
        !cpu->mca_rse_valid && !env->pal.pal_mca_active) {
        work->result |= IA64_QTEST_RAS_NEW_CONTEXT;
    }

    ia64_cpu_set_init_entry(cs, UINT64_C(0x101000),
                            UINT64_C(0x701000));
    ia64_sapic_set_init(cs, 1);
    if (!ia64_ras_enter_init(cpu)) {
        return;
    }
    if (env->pal.pal_init_active && cpu->mca_rse_valid &&
        env->ip == UINT64_C(0x101000) &&
        env->gr[IA64_GR_RETURN0] == 1 &&
        env->gr[IA64_GR_RETURN1] == save_address) {
        work->result |= IA64_QTEST_RAS_INIT_ENTRY;
    }
    env->gr[IA64_PAL_GR_INDEX] = 0x1a;
    env->gr[IA64_PAL_GR_ARG1] = 0;
    env->gr[IA64_PAL_GR_ARG2] = save_address;
    env->gr[IA64_PAL_GR_ARG3] = 0;
    if ((ia64_pal_dispatch(env, 0) & IA64_PAL_DISPATCH_RESUMED) &&
        !env->pal.pal_init_active && !cpu->mca_rse_valid) {
        work->result |= IA64_QTEST_RAS_INIT_RESUME;
    }
}

static void ia64_qtest_stale_victim_load_work(CPUState *cs,
                                               run_on_cpu_data data)
{
    IA64QTestStaleVictimWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);
    const int mmu_idx = MMU_IDX_VIRT_CPL0;
    CPUTLBEntryFull full = {
        .attrs = MEMTXATTRS_UNSPECIFIED,
        .prot = PAGE_READ,
        .lg_page_size = TARGET_PAGE_BITS,
        .extra.ia64 = {
            .speculation = IA64_MEM_SPECULATIVE,
            .memory_attribute = IA64_PTE_MA_WB,
        },
    };
    uint64_t collision_va = work->va ^ (UINT64_C(1) << 32);

    RCU_READ_LOCK_GUARD();

    /* Establish one deterministic translated data context at CPL 0. */
    env->psr &= ~(IA64_PSR_CPL_MASK | IA64_PSR_IS | IA64_PSR_PK |
                  IA64_PSR_DB | IA64_PSR_DA | IA64_PSR_DD |
                  IA64_PSR_ED | IA64_PSR_VM);
    env->psr |= IA64_PSR_DT;
    tlb_flush(cs);
    work->model_ready = ia64_mmu_insert_firmware_tc(
        env, work->va, work->new_pa, true, TARGET_PAGE_BITS);
    if (!work->model_ready) {
        return;
    }

    /*
     * Seed a contradictory clean load entry, then evict it to the victim
     * TLB.  Flipping VA bit 32 preserves every possible soft-TLB index:
     * with 4K target pages QEMU caps the table at 2^20 entries.
     */
    full.phys_addr = work->old_pa;
    tlb_set_page_full(cs, mmu_idx, work->va, &full);
    tlb_set_page_full(cs, mmu_idx, collision_va, &full);

    work->probe_result = ia64_mmu_speculative_int_probe(env, work->va, 8);
    if (!work->probe_result) {
        return;
    }

    /* Match the Merced helper emitted immediately before a translated load. */
    ia64_mmu_data_access(env, work->va, 8, true);
    work->value = ia64_exec_load_mmuidx(env, work->va, 8, false,
                                       mmu_idx, 0);
}

static void ia64_qtest_alat_writer_setup_work(CPUState *cs,
                                               run_on_cpu_data data)
{
    IA64QTestAlatWriterWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);
    uint64_t generation;

    work->full_model = env->alat_state.alat_full;
    if (!work->full_model) {
        return;
    }

    env->psr &= ~(IA64_PSR_CPL_MASK | IA64_PSR_IS | IA64_PSR_PK |
                  IA64_PSR_DB | IA64_PSR_DA | IA64_PSR_DD |
                  IA64_PSR_ED | IA64_PSR_VM);
    env->psr |= IA64_PSR_DT;
    tlb_flush(cs);
    work->model_ready = ia64_mmu_insert_firmware_tc(
        env, IA64_QTEST_ALAT_VA, IA64_QTEST_ALAT_PA, true,
        TARGET_PAGE_BITS);
    if (!work->model_ready) {
        return;
    }

    generation = ia64_alat_load_begin(env);
    ia64_alat_set(env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8,
                  generation);
    work->setup_hit = ia64_alat_check_load_addr(
        env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8, false);
}

static void ia64_qtest_alat_writer_active_work(CPUState *cs,
                                                run_on_cpu_data data)
{
    IA64QTestAlatWriterWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);
    IA64AlatEntry *entry = &env->alat_state.alat[0];
    uint64_t generation;

    /* Seed while the external writer is active. */
    ia64_alat_invala(env);
    *entry = (IA64AlatEntry) {
        .phys_addr = IA64_QTEST_ALAT_PA,
        .size = 8,
        .reg = IA64_QTEST_ALAT_REG,
        .valid = true,
    };
    env->alat_state.alat_active_count = 1;
    work->active_hit = ia64_alat_check_load_addr(
        env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8, false);

    generation = ia64_alat_load_begin(env);
    ia64_alat_set(env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8,
                  generation);
    work->active_alloc_count = env->alat_state.alat_active_count;
    work->active_alloc_hit = ia64_alat_check_load_addr(
        env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8, false);
}

static void ia64_qtest_alat_smp_store_work(CPUState *cs,
                                           run_on_cpu_data data)
{
    IA64QTestAlatWriterWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);
    uint64_t value = UINT64_C(0x0123456789abcdef);

    /* Exercise a CPU store, independently of the external/DMA generation. */
    ia64_exec_store_mmuidx(env, IA64_QTEST_ALAT_PA, value, 8, false,
                           MMU_PHYS_IDX, 0);
    work->memory_write_ok = ia64_exec_load_mmuidx(
        env, IA64_QTEST_ALAT_PA, 8, false, MMU_PHYS_IDX, 0) == value;
}

static void ia64_qtest_alat_writer_begin_work(CPUState *cs,
                                              run_on_cpu_data data)
{
    ia64_alat_write_begin(cpu_env(cs));
}

static void ia64_qtest_alat_writer_cancel_work(CPUState *cs,
                                               run_on_cpu_data data)
{
    ia64_alat_write_cancel(cpu_env(cs));
}

static void ia64_qtest_alat_smp_check_work(CPUState *cs,
                                           run_on_cpu_data data)
{
    IA64QTestAlatWriterWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);

    work->smp_hit = ia64_alat_check_load_addr(
        env, IA64_QTEST_ALAT_REG, IA64_QTEST_ALAT_VA, 8, false);
}

static bool ia64_qtest_alat_writer_command(CharFrontend *chr, gchar **words)
{
    IA64QTestAlatWriterWork work = { 0 };
    bool observed;
    CPUState *cs;
    CPUState *other_cs;

    if (words[1]) {
        qtest_sendf(chr, "FAIL command takes no arguments\n");
        return true;
    }
    if (!tcg_enabled()) {
        qtest_sendf(chr, "FAIL command requires TCG\n");
        return true;
    }

    cs = qemu_get_cpu(0);
    if (!cs) {
        qtest_sendf(chr, "FAIL command requires CPU 0\n");
        return true;
    }
    CPU_FOREACH(other_cs) {
        if (!cpu_is_stopped(other_cs)) {
            qtest_sendf(chr, "FAIL command requires stopped CPUs\n");
            return true;
        }
    }

    run_on_cpu(cs, ia64_qtest_alat_writer_setup_work,
               RUN_ON_CPU_HOST_PTR(&work));
    if (!work.full_model || !work.model_ready) {
        qtest_sendf(chr, "FAIL command requires a full ALAT model\n");
        return true;
    }

    observed = physical_memory_write_begin();
    physical_memory_write_external_begin();
    physical_memory_write_end(observed);
    run_on_cpu(cs, ia64_qtest_alat_writer_active_work,
               RUN_ON_CPU_HOST_PTR(&work));
    physical_memory_write_external_cancel();

    other_cs = qemu_get_cpu(1);
    if (other_cs) {
        IA64QTestAlatWriterWork cpu_work = { 0 };

        run_on_cpu(cs, ia64_qtest_alat_writer_setup_work,
                   RUN_ON_CPU_HOST_PTR(&cpu_work));
        run_on_cpu(other_cs, ia64_qtest_alat_writer_begin_work,
                   RUN_ON_CPU_NULL);
        run_on_cpu(cs, ia64_qtest_alat_writer_active_work,
                   RUN_ON_CPU_HOST_PTR(&cpu_work));
        run_on_cpu(other_cs, ia64_qtest_alat_writer_cancel_work,
                   RUN_ON_CPU_NULL);
        work.setup_hit &= cpu_work.setup_hit;
        work.active_hit |= cpu_work.active_hit;
        work.active_alloc_count += cpu_work.active_alloc_count;
        work.active_alloc_hit |= cpu_work.active_alloc_hit;
    }

    qtest_sendf(chr, "OK %u %u %u %u\n",
                work.setup_hit, work.active_hit,
                work.active_alloc_count, work.active_alloc_hit);
    return true;
}

static bool ia64_qtest_alat_smp_writer_command(CharFrontend *chr,
                                                gchar **words)
{
    IA64QTestAlatWriterWork work = { 0 };
    CPUState *cs;
    CPUState *other_cs;

    if (words[1]) {
        qtest_sendf(chr, "FAIL command takes no arguments\n");
        return true;
    }
    if (!tcg_enabled()) {
        qtest_sendf(chr, "FAIL command requires TCG\n");
        return true;
    }

    cs = qemu_get_cpu(0);
    other_cs = qemu_get_cpu(1);
    if (!cs || !other_cs || qemu_get_cpu(2)) {
        qtest_sendf(chr, "FAIL command requires two CPUs\n");
        return true;
    }
    if (!cpu_is_stopped(cs) || !cpu_is_stopped(other_cs)) {
        qtest_sendf(chr, "FAIL command requires stopped CPUs\n");
        return true;
    }

    run_on_cpu(cs, ia64_qtest_alat_writer_setup_work,
               RUN_ON_CPU_HOST_PTR(&work));
    if (!work.full_model || !work.model_ready) {
        qtest_sendf(chr, "FAIL command requires a full ALAT model\n");
        return true;
    }

    run_on_cpu(other_cs, ia64_qtest_alat_smp_store_work,
               RUN_ON_CPU_HOST_PTR(&work));
    run_on_cpu(cs, ia64_qtest_alat_smp_check_work,
               RUN_ON_CPU_HOST_PTR(&work));
    qtest_sendf(chr, "OK %u %u %u\n", work.setup_hit,
                work.memory_write_ok, work.smp_hit);
    return true;
}

static bool ia64_qtest_ras_min_state_command(CharFrontend *chr,
                                              gchar **words)
{
    IA64QTestRasMinStateWork work = { 0 };
    CPUState *cs;

    if (words[1]) {
        qtest_sendf(chr, "FAIL command takes no arguments\n");
        return true;
    }
    if (!tcg_enabled()) {
        qtest_sendf(chr, "FAIL command requires TCG\n");
        return true;
    }
    cs = qemu_get_cpu(0);
    if (!cs || qemu_get_cpu(1) || !cpu_is_stopped(cs)) {
        qtest_sendf(chr, "FAIL command requires one stopped CPU\n");
        return true;
    }

    run_on_cpu(cs, ia64_qtest_ras_min_state_work,
               RUN_ON_CPU_HOST_PTR(&work));
    qtest_sendf(chr, "OK 0x%016" PRIx64 "\n", work.result);
    return true;
}

typedef struct IA64QTestRasInjectWork {
    IA64RasHubState *hub;
    IA64RasSeverity severity;
    uint64_t status;
    uint64_t address;
    uint64_t information;
    uint8_t cmc_vector;
    bool accepted;
} IA64QTestRasInjectWork;

static void ia64_qtest_ras_inject_processor_work(CPUState *cs,
                                                  run_on_cpu_data data)
{
    IA64QTestRasInjectWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);

    if (work->severity == IA64_RAS_SEVERITY_CORRECTED) {
        env->cr[IA64_CR_CMCV] = work->cmc_vector;
    }
    work->accepted = ia64_ras_hub_report_processor_error(
        work->hub, cs, work->severity, work->status,
        work->address, work->information);
}

static bool ia64_qtest_ras_inject_command(CharFrontend *chr, gchar **words)
{
    IA64QTestRasInjectWork work = { 0 };
    IA64RasHubState *hub;
    uint64_t args[6];
    CPUState *cs;
    bool ambiguous;
    unsigned int i;
    int ret = 0;

    if (!words[1] || !words[2] || !words[3] || !words[4] || !words[5] ||
        !words[6] || !words[7] || words[8]) {
        qtest_sendf(chr, "FAIL expected KIND ARG0 ARG1 ARG2 ARG3 ARG4 ARG5\n");
        return true;
    }
    for (i = 0; i < ARRAY_SIZE(args); i++) {
        ret |= qemu_strtou64(words[i + 2], NULL, 0, &args[i]);
    }
    if (ret) {
        qtest_sendf(chr, "FAIL invalid argument\n");
        return true;
    }
    hub = IA64_RAS_HUB(object_resolve_path_type(
        "", TYPE_IA64_RAS_HUB, &ambiguous));
    if (!hub || ambiguous) {
        qtest_sendf(chr, "FAIL requires one IA-64 RAS hub\n");
        return true;
    }

    if (strcmp(words[1], "processor") == 0) {
        if (args[0] > INT_MAX || args[1] > IA64_RAS_SEVERITY_CORRECTED ||
            args[5] > UINT8_MAX ||
            (args[1] == IA64_RAS_SEVERITY_CORRECTED &&
             !ia64_external_interrupt_vector_valid(args[5]))) {
            qtest_sendf(chr, "FAIL invalid processor error\n");
            return true;
        }
        cs = qemu_get_cpu(args[0]);
        if (!cs || !cpu_is_stopped(cs)) {
            qtest_sendf(chr, "FAIL command requires a stopped CPU\n");
            return true;
        }
        work.hub = hub;
        work.severity = args[1];
        work.status = args[2];
        work.address = args[3];
        work.information = args[4];
        work.cmc_vector = args[5];
        run_on_cpu(cs, ia64_qtest_ras_inject_processor_work,
                   RUN_ON_CPU_HOST_PTR(&work));
    } else if (strcmp(words[1], "chipset") == 0) {
        IA64ChipsetFault fault;

        if (args[0] > IA64_CHIPSET_FAULT_POWER ||
            args[1] > IA64_RAS_SEVERITY_CORRECTED ||
            args[5] > UINT32_MAX) {
            qtest_sendf(chr, "FAIL invalid chipset error\n");
            return true;
        }
        fault = (IA64ChipsetFault) {
            .source = IA64_CHIPSET_FAULT_460GX,
            .reason = args[0],
            .severity = args[1],
            .address = args[2],
            .status = args[3],
            .information = args[4],
            .requester = args[5],
        };
        work.accepted = ia64_ras_hub_report_chipset_fault(hub, &fault);
    } else {
        qtest_sendf(chr, "FAIL invalid error kind\n");
        return true;
    }
    qtest_sendf(chr, "OK %u\n", work.accepted);
    return true;
}

static void ia64_qtest_sapic_cpu_work(CPUState *cs, run_on_cpu_data data)
{
    IA64QTestSapicWork *work = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);

    if (strcmp(work->operation, "pib-read") == 0) {
        uint8_t bytes[8] = { 0 };

        if (ia64_exec_physical_rw(work->address, bytes, work->size, false)) {
            work->result = ldq_le_p(bytes);
        } else {
            work->result = -1;
        }
    } else if (strcmp(work->operation, "pib-write") == 0) {
        uint8_t bytes[8] = { 0 };

        stq_le_p(bytes, work->value);
        work->result = ia64_exec_physical_rw(
            work->address, bytes, work->size, true);
    } else if (strcmp(work->operation, "xtp") == 0) {
        ia64_sapic_set_xtp(cs, work->value);
        work->result = ia64_sapic_get_xtp(cs);
    } else if (strcmp(work->operation, "state") == 0) {
        unsigned int vector = work->value;

        work->result = ia64_sapic_get_xtp(cs) |
            ((env->interrupt.sapic_irr[vector / 64] >>
              (vector % 64)) & 1) << 8 |
            ((env->interrupt.sapic_isr[vector / 64] >>
              (vector % 64)) & 1) << 9 |
            ((vector < 16 ? env->interrupt.sapic_pmi_pending >> vector : 0)
             & 1) << 10 |
            (uint64_t)env->interrupt.sapic_init_pending << 11;
    } else if (strcmp(work->operation, "halt-state") == 0) {
        work->result = cs->halted |
            (uint64_t)env->interrupt.pal_halt_wake << 1;
    } else if (strcmp(work->operation, "accept") == 0) {
        work->result = ia64_sapic_accept(env);
    } else if (strcmp(work->operation, "accept-pmi") == 0) {
        work->result = ia64_sapic_accept_pmi(env);
    } else if (strcmp(work->operation, "accept-init") == 0) {
        work->result = ia64_sapic_accept_init(env);
    } else if (strcmp(work->operation, "ras-arm") == 0) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

        if (env->pal.pal_mca_pending || env->pal.pal_mca_active ||
            cpu->mca_rse_valid) {
            work->result = 0;
            return;
        }
        env->pal.pal_mc_save_addr =
            IA64_PHYS_UC_BIT | IA64_QTEST_MCA_SAVE_PA;
        ia64_cpu_record_machine_check(
            cs, IA64_RAS_SEVERITY_RECOVERABLE,
            UINT64_C(0x1111222233334444), UINT64_C(0x12345000),
            UINT64_C(0x5555666677778888));
        ia64_cpu_request_mca(cs, UINT64_C(0x100000), UINT64_C(0x700000),
                             UINT64_C(0x778899),
                             IA64_RAS_SEVERITY_RECOVERABLE);
        work->result = ia64_ras_enter_mca(cpu) &&
            env->pal.pal_mca_active && cpu->mca_rse_valid;
    } else if (strcmp(work->operation, "ras-resume") == 0) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
        uint64_t save_address = env->pal.pal_mc_save_addr;

        if (!env->pal.pal_mca_active || !cpu->mca_rse_valid) {
            work->result = 0;
            return;
        }
        env->gr[IA64_PAL_GR_INDEX] = 0x1a;
        env->gr[IA64_PAL_GR_ARG1] = 0;
        env->gr[IA64_PAL_GR_ARG2] = save_address;
        env->gr[IA64_PAL_GR_ARG3] = 0;
        work->result =
            !!(ia64_pal_dispatch(env, 0) & IA64_PAL_DISPATCH_RESUMED) &&
            !env->pal.pal_mca_active && !cpu->mca_rse_valid;
    } else if (strcmp(work->operation, "ras-state") == 0) {
        work->result = (uint64_t)env->pal.pal_mca_pending |
            (uint64_t)env->pal.pal_mca_active << 1 |
            (uint64_t)env->pal.pal_cmc_pending << 2 |
            (uint64_t)env->pal.pal_mc_log_valid << 3;
    } else {
        g_assert(strcmp(work->operation, "eoi") == 0);
        ia64_sapic_eoi(env);
        work->result = 0;
    }
}

static bool ia64_qtest_sapic_command(CharFrontend *chr, gchar **words)
{
    static const char *const cpu_operations[] = {
        "xtp", "state", "halt-state", "accept", "accept-pmi",
        "accept-init", "eoi", "ras-arm", "ras-resume", "ras-state",
        "pib-read", "pib-write",
    };
    IA64QTestSapicWork work = { 0 };
    uint64_t args[5];
    CPUState *cs;
    unsigned int i;
    int ret = 0;

    if (!words[1] || !words[2] || !words[3] || !words[4] || !words[5] ||
        !words[6] || words[7]) {
        qtest_sendf(chr, "FAIL expected OP ARG0 ARG1 ARG2 ARG3 ARG4\n");
        return true;
    }
    for (i = 0; i < ARRAY_SIZE(args); i++) {
        ret |= qemu_strtou64(words[i + 2], NULL, 0, &args[i]);
    }
    if (ret) {
        qtest_sendf(chr, "FAIL invalid argument\n");
        return true;
    }

    if (strcmp(words[1], "deliver") == 0) {
        uint8_t id = args[1] >> 8;
        uint8_t eid = args[1];

        if (args[0] > IA64_SAPIC_DESTINATION_LOGICAL ||
            args[1] > UINT16_MAX || args[2] > 7 || args[3] > 1 ||
            args[4] > UINT8_MAX) {
            qtest_sendf(chr, "FAIL invalid delivery\n");
            return true;
        }
        work.result = ia64_sapic_deliver(
            args[0], id, eid, args[3], args[2], args[4]);
        qtest_sendf(chr, "OK %" PRId64 "\n", work.result);
        return true;
    }

    for (i = 0; i < ARRAY_SIZE(cpu_operations); i++) {
        if (strcmp(words[1], cpu_operations[i]) == 0) {
            break;
        }
    }
    if (i == ARRAY_SIZE(cpu_operations) || args[0] > INT_MAX ||
        (strcmp(words[1], "state") == 0 && args[1] > UINT8_MAX) ||
        (strcmp(words[1], "xtp") == 0 && args[1] > UINT8_MAX) ||
        ((strcmp(words[1], "pib-read") == 0 ||
          strcmp(words[1], "pib-write") == 0) &&
         args[2] != 1 && args[2] != 4 && args[2] != 8)) {
        qtest_sendf(chr, "FAIL invalid operation\n");
        return true;
    }
    cs = qemu_get_cpu(args[0]);
    if (!cs || !cpu_is_stopped(cs)) {
        qtest_sendf(chr, "FAIL command requires a stopped CPU\n");
        return true;
    }
    work.operation = words[1];
    work.value = args[1];
    if (strcmp(words[1], "pib-read") == 0 ||
        strcmp(words[1], "pib-write") == 0) {
        work.address = args[1];
        work.size = args[2];
        work.value = args[3];
    }
    run_on_cpu(cs, ia64_qtest_sapic_cpu_work, RUN_ON_CPU_HOST_PTR(&work));
    qtest_sendf(chr, "OK %" PRId64 "\n", work.result);
    return true;
}

static bool ia64_qtest_command(CharFrontend *chr, gchar **words)
{
    IA64QTestStaleVictimWork work = { 0 };
    CPUState *cs;
    int ret;

    if (strcmp(words[0], "ia64-alat-active-writer") == 0) {
        return ia64_qtest_alat_writer_command(chr, words);
    }
    if (strcmp(words[0], "ia64-alat-smp-writer") == 0) {
        return ia64_qtest_alat_smp_writer_command(chr, words);
    }
    if (strcmp(words[0], "ia64-ras-min-state") == 0) {
        return ia64_qtest_ras_min_state_command(chr, words);
    }
    if (strcmp(words[0], "ia64-ras-inject") == 0) {
        return ia64_qtest_ras_inject_command(chr, words);
    }
    if (strcmp(words[0], "ia64-sapic") == 0) {
        return ia64_qtest_sapic_command(chr, words);
    }
    if (strcmp(words[0], "ia64-stale-victim-load") != 0) {
        return false;
    }
    if (!words[1] || !words[2] || !words[3] || words[4]) {
        qtest_sendf(chr, "FAIL expected VA OLD_PA NEW_PA\n");
        return true;
    }

    ret = qemu_strtou64(words[1], NULL, 0, &work.va);
    ret |= qemu_strtou64(words[2], NULL, 0, &work.old_pa);
    ret |= qemu_strtou64(words[3], NULL, 0, &work.new_pa);
    if (ret || ((work.va | work.old_pa | work.new_pa) &
                ~TARGET_PAGE_MASK) || work.old_pa == work.new_pa) {
        qtest_sendf(chr, "FAIL invalid or unaligned address\n");
        return true;
    }
    if (!tcg_enabled()) {
        qtest_sendf(chr, "FAIL command requires TCG\n");
        return true;
    }

    cs = qemu_get_cpu(0);
    if (!cs || qemu_get_cpu(1) || !cpu_is_stopped(cs)) {
        qtest_sendf(chr, "FAIL command requires one stopped CPU\n");
        return true;
    }

    run_on_cpu(cs, ia64_qtest_stale_victim_load_work,
               RUN_ON_CPU_HOST_PTR(&work));
    if (!work.model_ready) {
        qtest_sendf(chr, "FAIL could not install modeled translation\n");
        return true;
    }

    qtest_sendf(chr, "OK %" PRIu64 " 0x%016" PRIx64 "\n",
                work.probe_result, work.value);
    return true;
}

static void ia64_register_qtest_command(void)
{
    static bool registered;

    if (qtest_driver() && !registered) {
        registered = true;
        qtest_add_command_cb(ia64_qtest_command);
    }
}

static void ia64_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    IA64CPU *cpu = IA64_CPU(dev);
    IA64CPUClass *icc = IA64_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu->semantic_profile_id = icc->semantic_profile_id;
    cpu->semantic_profile_abi = icc->semantic_profile_abi;
    cpu->migration_alat_full = cpu->alat_full;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
    if (cpu->alat_full) {
        physical_memory_write_observer_enable();
    }

    cpu->itm_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ia64_itm_timer_cb, cpu);

    qemu_init_vcpu(cs);
    cpu_reset(cs);
    ia64_register_qtest_command();

    icc->parent_realize(dev, errp);
}

static void ia64_cpu_finalize(Object *obj)
{
    IA64CPU *cpu = IA64_CPU(obj);

    if (cpu->itm_timer != NULL) {
        timer_free(cpu->itm_timer);
        cpu->itm_timer = NULL;
    }
}

static void ia64_cpu_unrealize(DeviceState *dev)
{
    IA64CPU *cpu = IA64_CPU(dev);
    IA64CPUClass *icc = IA64_CPU_GET_CLASS(dev);

    if (cpu->itm_timer != NULL) {
        timer_free(cpu->itm_timer);
        cpu->itm_timer = NULL;
    }
    icc->parent_unrealize(dev);
}

static const struct SysemuCPUOps ia64_sysemu_ops = {
    .has_work = ia64_cpu_has_work,
    .get_phys_page_debug = ia64_cpu_get_phys_page_debug,
};

static bool ia64_precise_smc_enabled(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    /* IA-32 stores, unlike IA-64 stores, participate in hardware SMC. */
    return cpu->env.psr & IA64_PSR_IS;
}

static void ia64_cpu_exec_longjmp_cleanup(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    /* Abort an active CPU-store window. */
    ia64_alat_write_abort(&cpu->env);
}

static const TCGCPUOps ia64_tcg_ops = {
    /*
     * Native IA-64 loads and stores are weakly ordered; their acquire,
     * release, and mf semantics are emitted explicitly by the translator.
     * Keep the x86 TSO default for the embedded IA-32 execution mode.  TCG
     * supplies the barriers missing from weakly ordered hosts, while TSO
     * hosts avoid a redundant store-load fence before every ordinary load.
     * UC and UCE mappings restore their sequential store-load ordering
     * through TLB_FORCE_ST_LD.
     */
    .guest_default_memory_order = TCG_MO_ALL & ~TCG_MO_ST_LD,
    .mttcg_supported = true,
    .precise_smc = true,
    .precise_smc_enabled = ia64_precise_smc_enabled,
    .initialize = ia64_translate_init,
    .translate_code = ia64_translate_code,
    .get_tb_cpu_state = ia64_get_tb_cpu_state,
    .synchronize_from_tb = ia64_cpu_synchronize_from_tb,
    .restore_state_to_opc = ia64_restore_state_to_opc,
    .cpu_exec_longjmp_cleanup = ia64_cpu_exec_longjmp_cleanup,
    .mmu_index = ia64_cpu_mmu_index,
    .tlb_fill = ia64_cpu_tlb_fill,
    .pointer_wrap = ia64_pointer_wrap,
#ifndef CONFIG_USER_ONLY
    .do_unaligned_access = ia64_cpu_do_unaligned_access,
#endif
    .cpu_exec_interrupt = ia64_cpu_exec_interrupt,
    .cpu_exec_halt = ia64_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = ia64_cpu_do_interrupt,
};

#define IA64_ITANIUM2_MEMORY_ATTRIBUTE_MASK \
    ((1U << IA64_PTE_MA_WB) | (1U << IA64_PTE_MA_UC) | \
     (1U << IA64_PTE_MA_UCE) | (1U << IA64_PTE_MA_WC))

#define IA64_FREQUENCY_RATIO(numerator, denominator) \
    (((uint64_t)(numerator) << 32) | (denominator))

static void ia64_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    IA64CPUClass *icc = IA64_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, ia64_cpu_realize,
                                    &icc->parent_realize);
    device_class_set_parent_unrealize(dc, ia64_cpu_unrealize,
                                      &icc->parent_unrealize);
    resettable_class_set_parent_phases(rc, NULL, ia64_cpu_reset_hold, NULL,
                                       &icc->parent_phases);

    cc->class_by_name = ia64_cpu_class_by_name;
    cc->dump_state = ia64_cpu_dump_state;
    cc->set_pc = ia64_cpu_set_pc;
    cc->get_pc = ia64_cpu_get_pc;
    cc->sysemu_ops = &ia64_sysemu_ops;
    cc->gdb_read_register = ia64_cpu_gdb_read_register;
    cc->gdb_write_register = ia64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = IA64_GDB_NUM_CORE_REGS;
    cc->tcg_ops = &ia64_tcg_ops;
    dc->vmsd = &vmstate_ia64_cpu;

    icc->semantic_profile_abi = 1;
    icc->model = IA64_CPU_MODEL_MONTECITO;
    icc->cpuid_version = 0x0000000020000704ULL;
    icc->cpuid_features = IA64_CPUID4_LB | IA64_CPUID4_AO;
    icc->pal_version = 0x0000096801000968ULL;
    icc->pal_brand =
        "QEMU Montecito-compatible IA-64 CPU 1.60GHz 24MB";
    icc->frequency_base_hz = 100000000;
    icc->itc_frequency_hz = 400000000;
    icc->pal_l3_cache_size = 12 * MiB;
    icc->pal_package_cache_size = 24 * MiB;
    icc->pal_processor_frequency_hz = 1600000000ULL;
    icc->pal_bus_frequency_hz = 533333333ULL;
    icc->processor_frequency_ratio = IA64_FREQUENCY_RATIO(16, 1);
    icc->bus_frequency_ratio = IA64_FREQUENCY_RATIO(16, 3);
    icc->itc_frequency_ratio = IA64_FREQUENCY_RATIO(4, 1);
    icc->ia32_cpuid_version = 0;
    memset(icc->ia32_cpuid_leaf2, 0, sizeof(icc->ia32_cpuid_leaf2));
    icc->insertable_page_size_mask = IA64_INSERTABLE_PAGE_SIZE_MASK;
    icc->purgeable_page_size_mask = IA64_PURGEABLE_PAGE_SIZE_MASK;
    icc->itr_count = 32;
    icc->dtr_count = 32;
    icc->itlb_entries = IA64_TLB_MAX;
    icc->dtlb_entries = IA64_TLB_MAX;
    icc->phys_addr_bits = IA64_IMPL_PA_BITS;
    icc->impl_va_msb = IA64_IMPL_VA_MSB;
    icc->rid_bits = IA64_IMPL_RID_BITS;
    icc->key_bits = IA64_IMPL_KEY_BITS;
    icc->hash_tag_id = 8;
    icc->unique_tcs = 4;
    icc->tc_levels = 2;
    icc->perf_counter_width = 48;
    icc->pal_l3_associativity = 12;
    icc->pal_l3_load_latency = 14;
    icc->pal_l3_tag_lsb = 20;
    /* WB, UC, UCE, and WC are distinct; WC buffering is not modeled. */
    icc->memory_attribute_mask = IA64_ITANIUM2_MEMORY_ATTRIBUTE_MASK;
    icc->fc_line_size = 128;
    icc->implemented_pmc_mask = 0x3fffULL;
    icc->implemented_pmd_mask = 0x3ffffULL;
    icc->perf_cycles_mask = 0xf0ULL;
    icc->perf_retired_mask = 0xf0ULL;
    icc->rse_has_clean_partition = true;
    icc->has_native_ia32 = false;
    icc->has_virtualization = true;
    icc->is_montecito = true;
}

typedef struct IA64CPUModelDef {
    uint64_t semantic_profile_id;
    IA64CPUModel model;
    uint64_t cpuid_version;
    uint64_t cpuid_features;
    uint64_t pal_version;
    const char *pal_brand;
    uint32_t frequency_base_hz;
    uint32_t itc_frequency_hz;
    uint32_t pal_l3_cache_size;
    uint32_t pal_package_cache_size;
    uint64_t pal_processor_frequency_hz;
    uint64_t pal_bus_frequency_hz;
    uint64_t processor_frequency_ratio;
    uint64_t bus_frequency_ratio;
    uint64_t itc_frequency_ratio;
    uint32_t ia32_cpuid_version;
    uint32_t ia32_cpuid_leaf2[4];
    uint64_t insertable_page_size_mask;
    uint64_t purgeable_page_size_mask;
    uint8_t itr_count;
    uint8_t dtr_count;
    uint16_t itlb_entries;
    uint16_t dtlb_entries;
    uint8_t phys_addr_bits;
    uint8_t impl_va_msb;
    uint8_t rid_bits;
    uint8_t key_bits;
    uint8_t hash_tag_id;
    uint8_t unique_tcs;
    uint8_t tc_levels;
    uint8_t perf_counter_width;
    uint8_t memory_attribute_mask;
    uint8_t pal_l3_associativity;
    uint8_t pal_l3_load_latency;
    uint8_t pal_l3_tag_lsb;
    uint16_t fc_line_size;
    uint64_t implemented_pmc_mask;
    uint64_t implemented_pmd_mask;
    uint64_t perf_cycles_mask;
    uint64_t perf_retired_mask;
    bool rse_has_clean_partition;
    bool data_debug_cross_16byte;
    bool has_native_ia32;
    bool has_virtualization;
    bool is_montecito;
} IA64CPUModelDef;

static void ia64_cpu_model_class_init(ObjectClass *oc, const void *data)
{
    IA64CPUClass *icc = IA64_CPU_CLASS(oc);
    const IA64CPUModelDef *model = data;

    icc->semantic_profile_id = model->semantic_profile_id;
    icc->model = model->model;
    icc->cpuid_version = model->cpuid_version;
    icc->cpuid_features = model->cpuid_features;
    icc->pal_version = model->pal_version;
    icc->pal_brand = model->pal_brand;
    icc->frequency_base_hz = model->frequency_base_hz;
    icc->itc_frequency_hz = model->itc_frequency_hz;
    icc->pal_l3_cache_size = model->pal_l3_cache_size;
    icc->pal_package_cache_size = model->pal_package_cache_size;
    icc->pal_processor_frequency_hz = model->pal_processor_frequency_hz;
    icc->pal_bus_frequency_hz = model->pal_bus_frequency_hz;
    icc->processor_frequency_ratio = model->processor_frequency_ratio;
    icc->bus_frequency_ratio = model->bus_frequency_ratio;
    icc->itc_frequency_ratio = model->itc_frequency_ratio;
    icc->ia32_cpuid_version = model->ia32_cpuid_version;
    memcpy(icc->ia32_cpuid_leaf2, model->ia32_cpuid_leaf2,
           sizeof(icc->ia32_cpuid_leaf2));
    icc->insertable_page_size_mask = model->insertable_page_size_mask;
    icc->purgeable_page_size_mask = model->purgeable_page_size_mask;
    icc->itr_count = model->itr_count;
    icc->dtr_count = model->dtr_count;
    icc->itlb_entries = model->itlb_entries;
    icc->dtlb_entries = model->dtlb_entries;
    icc->phys_addr_bits = model->phys_addr_bits;
    icc->impl_va_msb = model->impl_va_msb;
    icc->rid_bits = model->rid_bits;
    icc->key_bits = model->key_bits;
    icc->hash_tag_id = model->hash_tag_id;
    icc->unique_tcs = model->unique_tcs;
    icc->tc_levels = model->tc_levels;
    icc->perf_counter_width = model->perf_counter_width;
    icc->memory_attribute_mask = model->memory_attribute_mask;
    icc->pal_l3_associativity = model->pal_l3_associativity;
    icc->pal_l3_load_latency = model->pal_l3_load_latency;
    icc->pal_l3_tag_lsb = model->pal_l3_tag_lsb;
    icc->fc_line_size = model->fc_line_size;
    icc->implemented_pmc_mask = model->implemented_pmc_mask;
    icc->implemented_pmd_mask = model->implemented_pmd_mask;
    icc->perf_cycles_mask = model->perf_cycles_mask;
    icc->perf_retired_mask = model->perf_retired_mask;
    icc->pal_proc_feature_available = model->is_montecito ?
        PAL_PROC_MONTECITO_AVAILABLE : 0;
    icc->pal_proc_feature_controllable = model->is_montecito ?
        PAL_PROC_MONTECITO_CONTROLLABLE : 0;
    icc->rse_has_clean_partition = model->rse_has_clean_partition;
    icc->data_debug_cross_16byte = model->data_debug_cross_16byte;
    icc->has_native_ia32 = model->has_native_ia32;
    icc->has_virtualization = model->has_virtualization;
    icc->is_montecito = model->is_montecito;

    g_assert(model->itlb_entries > 0 &&
             model->itlb_entries <= IA64_TLB_MAX);
    g_assert(model->dtlb_entries > 0 &&
             model->dtlb_entries <= IA64_TLB_MAX);
    g_assert(model->itr_count <= model->itlb_entries);
    g_assert(model->dtr_count <= model->dtlb_entries);
    g_assert(model->frequency_base_hz > 0);
    g_assert(model->itc_frequency_hz > 0 &&
             model->itc_frequency_hz <= 1600000000);
    g_assert((uint32_t)model->itc_frequency_ratio != 0);
    g_assert((uint64_t)model->frequency_base_hz *
             (model->itc_frequency_ratio >> 32) /
             (uint32_t)model->itc_frequency_ratio ==
             model->itc_frequency_hz);
    g_assert(model->fc_line_size >= 32 &&
             is_power_of_2(model->fc_line_size));
    g_assert(model->pal_brand != NULL);
    g_assert(model->pal_l3_cache_size > 0);
    g_assert(model->pal_package_cache_size >= model->pal_l3_cache_size);
    g_assert(model->pal_l3_associativity > 0);
    g_assert(model->pal_l3_load_latency > 0);
    g_assert(model->pal_l3_tag_lsb > 0);
}

static const IA64CPUModelDef ia64_cpu_model_merced = {
    .semantic_profile_id = 0x6d65726365640001ULL,
    .model = IA64_CPU_MODEL_MERCED,
    .cpuid_version = 0x0000000007000804ULL,
    .cpuid_features = 0,
    /*
     * PAL_A model 8 and PAL_B model 8 use revision 30 for this release.
     */
    .pal_version = 0x0000883001008830ULL,
    .pal_brand = "QEMU Itanium-compatible IA-64 CPU",
    /* The 800 MHz model advances ITC at the processor frequency. */
    .frequency_base_hz = 100000000,
    .itc_frequency_hz = 800000000,
    .pal_l3_cache_size = 4 * MiB,
    .pal_package_cache_size = 4 * MiB,
    .processor_frequency_ratio = IA64_FREQUENCY_RATIO(8, 1),
    .bus_frequency_ratio = IA64_FREQUENCY_RATIO(4, 3),
    .itc_frequency_ratio = IA64_FREQUENCY_RATIO(8, 1),
    /* IA-32 CPUID signature exposed by this CPU model. */
    .ia32_cpuid_version = 0x00000715,
    .ia32_cpuid_leaf2 = {
        0x00151001, 0x0000891a, 0x009b9690, 0x80000000,
    },
    /* Include the architecturally required 64 MiB page size. */
    .insertable_page_size_mask =
        (1ULL << 12) | (1ULL << 13) | (1ULL << 14) | (1ULL << 16) |
        (1ULL << 18) | (1ULL << 20) | (1ULL << 22) | (1ULL << 24) |
        (1ULL << 26) | (1ULL << 28),
    .purgeable_page_size_mask =
        (1ULL << 12) | (1ULL << 13) | (1ULL << 14) | (1ULL << 16) |
        (1ULL << 18) | (1ULL << 20) | (1ULL << 22) | (1ULL << 24) |
        (1ULL << 26) | (1ULL << 28) | (1ULL << 32),
    .itr_count = 8,
    .dtr_count = 48,
    /*
     * The architecturally visible translation storage is the 64-entry
     * instruction TLB and the 96-entry main data TLB.  The 32-entry
     * first-level data TLB only caches the main data TLB and is not extra
     * TR/TC storage.
     */
    .itlb_entries = 64,
    .dtlb_entries = 96,
    .phys_addr_bits = 44,
    .impl_va_msb = 50,
    .rid_bits = 18,
    .key_bits = 21,
    .hash_tag_id = 0,
    .unique_tcs = 3,
    .tc_levels = 2,
    .perf_counter_width = 32,
    .memory_attribute_mask = (1U << IA64_PTE_MA_WB) |
                             (1U << IA64_PTE_MA_UC) |
                             (1U << IA64_PTE_MA_WC) |
                             (1U << IA64_PTE_MA_NATPAGE),
    .pal_l3_associativity = 4,
    .pal_l3_load_latency = 21,
    .pal_l3_tag_lsb = 20,
    /* The first-generation L2/L3 cache line is 64 bytes. */
    .fc_line_size = 64,
    .implemented_pmc_mask = 0x3fffULL,
    .implemented_pmd_mask = 0x3ffffULL,
    .perf_cycles_mask = 0xf0ULL,
    /*
     * The product manual records the old-PAL 0x10 result.  The processor
     * update identifies that as an erratum through PAL 7.7.28; PAL 8.8.30,
     * advertised above, reports the implemented PMC4/PMC5 pair.
     */
    .perf_retired_mask = 0x30ULL,
    .rse_has_clean_partition = false,
    .has_native_ia32 = true,
};

static const IA64CPUModelDef ia64_cpu_model_madison = {
    .semantic_profile_id = 0x6d616469736f0001ULL,
    .model = IA64_CPU_MODEL_MADISON,
    /* Family 0x1f, model 1, revision 5, CPUID[4] is the last register. */
    .cpuid_version = 0x000000001f010504ULL,
    /*
     * The selected family 0x1f/model 1/revision 5 processor reports only
     * long-branch support in CPUID[4].  In particular, early deferral
     * selected through PAL is not the CPUID spontaneous-deferral feature.
     */
    .cpuid_features = IA64_CPUID4_LB,
    /* Latest documented PAL release for the selected B1 model. */
    .pal_version = 0x0000057301000573ULL,
    .pal_brand = "QEMU Madison-compatible IA-64 CPU",
    .frequency_base_hz = 100000000,
    .itc_frequency_hz = 1600000000,
    .pal_l3_cache_size = 3 * MiB,
    .pal_package_cache_size = 3 * MiB,
    .processor_frequency_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_frequency_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .itc_frequency_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .ia32_cpuid_version = 0x00000673,
    .ia32_cpuid_leaf2 = {
        0x7e776701, 0x0000008d, 0, 0x80000000,
    },
    .insertable_page_size_mask = IA64_INSERTABLE_PAGE_SIZE_MASK,
    .purgeable_page_size_mask = IA64_PURGEABLE_PAGE_SIZE_MASK,
    .itr_count = 64,
    .dtr_count = 64,
    .itlb_entries = IA64_TLB_MAX,
    .dtlb_entries = IA64_TLB_MAX,
    .phys_addr_bits = IA64_IMPL_PA_BITS,
    .impl_va_msb = IA64_IMPL_VA_MSB,
    .rid_bits = IA64_IMPL_RID_BITS,
    .key_bits = IA64_IMPL_KEY_BITS,
    .hash_tag_id = 8,
    .unique_tcs = 4,
    .tc_levels = 2,
    .perf_counter_width = 48,
    .memory_attribute_mask = IA64_ITANIUM2_MEMORY_ATTRIBUTE_MASK,
    .pal_l3_associativity = 12,
    .pal_l3_load_latency = 14,
    .pal_l3_tag_lsb = 18,
    /* Intel order 251110-003, section 5.8: each fc invalidates 128 bytes. */
    .fc_line_size = 128,
    .implemented_pmc_mask = 0x3fffULL,
    .implemented_pmd_mask = 0x3ffffULL,
    .perf_cycles_mask = 0xf0ULL,
    .perf_retired_mask = 0xf0ULL,
    .rse_has_clean_partition = true,
    /* Intel order 251110-003, section 12.3. */
    .data_debug_cross_16byte = true,
    .has_native_ia32 = true,
    .has_virtualization = false,
};

static const IA64CPUModelDef ia64_cpu_model_montecito = {
    .semantic_profile_id = 0x6d6f6e7465630001ULL,
    .model = IA64_CPU_MODEL_MONTECITO,
    /* Family 0x20, model 0, C2 revision 7, CPUID[4] is the last register. */
    .cpuid_version = 0x0000000020000704ULL,
    /* C2 reports long-branch and 16-byte atomic support (CPUID[4] = 5). */
    .cpuid_features = IA64_CPUID4_LB | IA64_CPUID4_AO,
    /* Latest documented PAL release for the selected C2 model. */
    .pal_version = 0x0000096801000968ULL,
    .pal_brand = "QEMU Montecito-compatible IA-64 CPU 1.60GHz 24MB",
    /* Keep ITC advancement consistent with the advertised 4:1 ratio. */
    .frequency_base_hz = 100000000,
    .itc_frequency_hz = 400000000,
    .pal_l3_cache_size = 12 * MiB,
    .pal_package_cache_size = 24 * MiB,
    .pal_processor_frequency_hz = 1600000000ULL,
    .pal_bus_frequency_hz = 533333333ULL,
    .processor_frequency_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_frequency_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_frequency_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .ia32_cpuid_version = 0,
    .ia32_cpuid_leaf2 = { 0, 0, 0, 0 },
    .insertable_page_size_mask = IA64_INSERTABLE_PAGE_SIZE_MASK,
    .purgeable_page_size_mask = IA64_PURGEABLE_PAGE_SIZE_MASK,
    .itr_count = 32,
    .dtr_count = 32,
    .itlb_entries = IA64_TLB_MAX,
    .dtlb_entries = IA64_TLB_MAX,
    .phys_addr_bits = IA64_IMPL_PA_BITS,
    .impl_va_msb = IA64_IMPL_VA_MSB,
    .rid_bits = IA64_IMPL_RID_BITS,
    .key_bits = IA64_IMPL_KEY_BITS,
    .hash_tag_id = 8,
    .unique_tcs = 4,
    .tc_levels = 2,
    .perf_counter_width = 48,
    /*
     * The dual-core update inherits the Madison WB/UC/UCE/WC memory
     * attributes and explicitly gives UC/UCE/WC restrictions for its new
     * 16-byte operations.
     */
    .memory_attribute_mask = IA64_ITANIUM2_MEMORY_ATTRIBUTE_MASK,
    .pal_l3_associativity = 12,
    .pal_l3_load_latency = 14,
    .pal_l3_tag_lsb = 20,
    /* Montecito's L2 and L3 cache lines are 128 bytes. */
    .fc_line_size = 128,
    .implemented_pmc_mask = 0x3fffULL,
    .implemented_pmd_mask = 0x3ffffULL,
    .perf_cycles_mask = 0xf0ULL,
    .perf_retired_mask = 0xf0ULL,
    .rse_has_clean_partition = true,
    /* Native IA-32 and PAL-based IA-32 translation are not implemented. */
    .has_native_ia32 = false,
    /* Virtualization mode is not modeled; vmsw raises Virtualization Fault. */
    .has_virtualization = true,
    .is_montecito = true,
};

typedef struct IA64CPUVariantDef {
    uint64_t semantic_profile_id;
    const char *brand;
    uint64_t cpuid_version;
    uint64_t pal_version;
    uint32_t itc_frequency_hz;
    uint32_t l3_cache_size;
    uint32_t package_cache_size;
    uint64_t processor_frequency_hz;
    uint64_t bus_frequency_hz;
    uint64_t processor_ratio;
    uint64_t bus_ratio;
    uint64_t itc_ratio;
    uint8_t l3_associativity;
    uint8_t l3_load_latency;
    uint8_t l3_tag_lsb;
    bool disable_ht;
} IA64CPUVariantDef;

static void ia64_cpu_variant_class_init(ObjectClass *oc, const void *data)
{
    IA64CPUClass *icc = IA64_CPU_CLASS(oc);
    const IA64CPUVariantDef *variant = data;

    icc->semantic_profile_id = variant->semantic_profile_id;
    icc->pal_brand = variant->brand;
    if (variant->cpuid_version) {
        icc->cpuid_version = variant->cpuid_version;
    }
    if (variant->pal_version) {
        icc->pal_version = variant->pal_version;
    }
    icc->itc_frequency_hz = variant->itc_frequency_hz;
    icc->pal_l3_cache_size = variant->l3_cache_size;
    icc->pal_package_cache_size = variant->package_cache_size;
    icc->pal_processor_frequency_hz = variant->processor_frequency_hz;
    icc->pal_bus_frequency_hz = variant->bus_frequency_hz;
    icc->processor_frequency_ratio = variant->processor_ratio;
    icc->bus_frequency_ratio = variant->bus_ratio;
    icc->itc_frequency_ratio = variant->itc_ratio;
    icc->pal_l3_associativity = variant->l3_associativity;
    if (variant->l3_load_latency) {
        icc->pal_l3_load_latency = variant->l3_load_latency;
    }
    if (variant->l3_tag_lsb) {
        icc->pal_l3_tag_lsb = variant->l3_tag_lsb;
    }
    if (variant->disable_ht) {
        icc->pal_proc_feature_available &= ~PAL_PROC_MONTECITO_HT;
    }

    g_assert((uint64_t)icc->frequency_base_hz *
             (icc->itc_frequency_ratio >> 32) /
             (uint32_t)icc->itc_frequency_ratio ==
             icc->itc_frequency_hz);
}

#define IA64_SINGLE_CORE_VARIANT(_name, _brand, _mhz, _cache_kib, _assoc) \
    static const IA64CPUVariantDef _name = {                          \
        .semantic_profile_id = UINT64_C(0x1000000000000000) |        \
                               ((uint64_t)(_mhz) << 20) |            \
                               (_cache_kib),                         \
        .brand = (_brand),                                            \
        .itc_frequency_hz = (_mhz) * 1000000U,                        \
        .l3_cache_size = (_cache_kib) * KiB,                          \
        .package_cache_size = (_cache_kib) * KiB,                     \
        .processor_frequency_hz = (_mhz) * 1000000ULL,                \
        .bus_frequency_hz = 400000000ULL,                             \
        .processor_ratio = IA64_FREQUENCY_RATIO((_mhz), 100),         \
        .bus_ratio = IA64_FREQUENCY_RATIO(4, 1),                      \
        .itc_ratio = IA64_FREQUENCY_RATIO((_mhz), 100),               \
        .l3_associativity = (_assoc),                                 \
    }

static const IA64CPUVariantDef ia64_cpu_mckinley = {
    .semantic_profile_id = 0x6d636b696e6c0001ULL,
    .brand = "QEMU McKinley-compatible IA-64 CPU 1.00GHz 3MB",
    .cpuid_version = 0x000000001f000704ULL,
    .pal_version = 0x0000077901000779ULL,
    .itc_frequency_hz = 1000000000U,
    .l3_cache_size = 3 * MiB,
    .package_cache_size = 3 * MiB,
    .processor_frequency_hz = 1000000000ULL,
    .bus_frequency_hz = 400000000ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(10, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .itc_ratio = IA64_FREQUENCY_RATIO(10, 1),
    .l3_associativity = 12,
    .l3_load_latency = 12,
    .l3_tag_lsb = 18,
};

IA64_SINGLE_CORE_VARIANT(ia64_cpu_mckinley_900,
    "QEMU McKinley-compatible IA-64 CPU 900MHz 1.5MB", 900, 1536, 6);
IA64_SINGLE_CORE_VARIANT(ia64_cpu_deerfield,
    "QEMU Deerfield-compatible IA-64 CPU 1.00GHz 1.5MB", 1000, 1536, 6);
IA64_SINGLE_CORE_VARIANT(ia64_cpu_madison_1500k,
    "QEMU Madison-compatible IA-64 CPU 1.40GHz 1.5MB", 1400, 1536, 6);
IA64_SINGLE_CORE_VARIANT(ia64_cpu_madison_3m,
    "QEMU Madison-compatible IA-64 CPU 1.60GHz 3MB", 1600, 3072, 12);
IA64_SINGLE_CORE_VARIANT(ia64_cpu_madison_4m,
    "QEMU Madison-compatible IA-64 CPU 1.40GHz 4MB", 1400, 4096, 16);
IA64_SINGLE_CORE_VARIANT(ia64_cpu_madison_6m,
    "QEMU Madison-compatible IA-64 CPU 1.50GHz 6MB", 1500, 6144, 24);
static const IA64CPUVariantDef ia64_cpu_madison_9m = {
    .semantic_profile_id = 0x6d6164396d000001ULL,
    .brand = "QEMU Madison-compatible IA-64 CPU 1.60GHz 9MB",
    .cpuid_version = 0x000000001f020204ULL,
    .pal_version = 0x0000022501000225ULL,
    .itc_frequency_hz = 1600000000U,
    .l3_cache_size = 9 * MiB,
    .package_cache_size = 9 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .l3_associativity = 18,
    .l3_load_latency = 14,
    .l3_tag_lsb = 19,
};

static const IA64CPUVariantDef ia64_cpu_madison_zx6000 = {
    .semantic_profile_id = 0x6d61647a78360001ULL,
    .brand = "QEMU Madison zx6000-compatible IA-64 CPU 1.50GHz 6MB",
    .itc_frequency_hz = 1500000000U,
    .l3_cache_size = 6 * MiB,
    .package_cache_size = 6 * MiB,
    .processor_frequency_hz = 1500000000ULL,
    .bus_frequency_hz = 400000000ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(15, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .itc_ratio = IA64_FREQUENCY_RATIO(15, 1),
    .l3_associativity = 24,
    .l3_load_latency = 14,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9010 = {
    .semantic_profile_id = 0x6d6f6e0039303130ULL,
    .brand = "QEMU Montecito 9010-compatible IA-64 CPU 1.60GHz 6MB",
    .itc_frequency_hz = 400000000U,
    .l3_cache_size = 6 * MiB,
    .package_cache_size = 6 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .l3_associativity = 6,
    .disable_ht = true,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9015 = {
    .semantic_profile_id = 0x6d6f6e0039303135ULL,
    .brand = "QEMU Montecito 9015-compatible IA-64 CPU 1.40GHz 12MB",
    .itc_frequency_hz = 350000000U,
    .l3_cache_size = 6 * MiB,
    .package_cache_size = 12 * MiB,
    .processor_frequency_hz = 1400000000ULL,
    .bus_frequency_hz = 400000000ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(14, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .itc_ratio = IA64_FREQUENCY_RATIO(7, 2),
    .l3_associativity = 6,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9020 = {
    .semantic_profile_id = 0x6d6f6e0039303230ULL,
    .brand = "QEMU Montecito 9020-compatible IA-64 CPU 1.42GHz 12MB",
    .itc_frequency_hz = 355000000U,
    .l3_cache_size = 6 * MiB,
    .package_cache_size = 12 * MiB,
    .processor_frequency_hz = 1420000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(142, 10),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(71, 20),
    .l3_associativity = 6,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9030 = {
    .semantic_profile_id = 0x6d6f6e0039303330ULL,
    .brand = "QEMU Montecito 9030-compatible IA-64 CPU 1.60GHz 8MB",
    .itc_frequency_hz = 400000000U,
    .l3_cache_size = 4 * MiB,
    .package_cache_size = 8 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .l3_associativity = 4,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9040 = {
    .semantic_profile_id = 0x6d6f6e0039303430ULL,
    .brand = "QEMU Montecito 9040-compatible IA-64 CPU 1.60GHz 18MB",
    .itc_frequency_hz = 400000000U,
    .l3_cache_size = 9 * MiB,
    .package_cache_size = 18 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .l3_associativity = 9,
};

static const IA64CPUVariantDef ia64_cpu_montecito_9050 = {
    .semantic_profile_id = 0x6d6f6e0039303530ULL,
    .brand = "QEMU Montecito 9050-compatible IA-64 CPU 1.60GHz 24MB",
    .itc_frequency_hz = 400000000U,
    .l3_cache_size = 12 * MiB,
    .package_cache_size = 24 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .l3_associativity = 12,
};

#define IA64_MONTVALE_VARIANT(_name, _number, _mhz, _l3_mb, _package_mb, \
                              _bus_num, _bus_den, _assoc, _no_ht)       \
    static const IA64CPUVariantDef _name = {                           \
        .semantic_profile_id = UINT64_C(0x2000000000000000) |         \
                               ((uint64_t)(_mhz) << 24) |             \
                               ((uint64_t)(_package_mb) << 8) |       \
                               (_l3_mb),                             \
        .brand = "QEMU Montvale " #_number                            \
                 "-compatible IA-64 CPU " #_mhz "MHz "                \
                 #_package_mb "MB",                                  \
        .itc_frequency_hz = ((_mhz) * 1000000U) / 4,                  \
        .l3_cache_size = (_l3_mb) * MiB,                              \
        .package_cache_size = (_package_mb) * MiB,                    \
        .processor_frequency_hz = (_mhz) * 1000000ULL,                \
        .bus_frequency_hz =                                           \
            (100000000ULL * (_bus_num)) / (_bus_den),                 \
        .processor_ratio = IA64_FREQUENCY_RATIO((_mhz), 100),         \
        .bus_ratio = IA64_FREQUENCY_RATIO((_bus_num), (_bus_den)),    \
        .itc_ratio = IA64_FREQUENCY_RATIO((_mhz), 400),               \
        .l3_associativity = (_assoc),                                 \
        .disable_ht = (_no_ht),                                       \
    }

IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9110n, 9110N, 1600, 12, 12,
                       16, 3, 12, true);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9120n, 9120N, 1420, 6, 12,
                       16, 3, 6, false);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9130m, 9130M, 1666, 4, 8,
                       20, 3, 4, true);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9140m, 9140M, 1666, 9, 18,
                       20, 3, 9, false);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9140n, 9140N, 1600, 9, 18,
                       16, 3, 9, false);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9150m, 9150M, 1666, 12, 24,
                       20, 3, 12, false);
IA64_MONTVALE_VARIANT(ia64_cpu_montvale_9152m, 9152M, 1666, 12, 24,
                       20, 3, 12, false);

static const IA64CPUVariantDef ia64_cpu_montvale = {
    .semantic_profile_id = 0x6d6f6e0039313530ULL,
    .brand = "QEMU Montvale 9150N-compatible IA-64 CPU 1600MHz 24MB",
    .cpuid_version = 0x0000000020010104ULL,
    .pal_version = 0x0000010801000108ULL,
    .itc_frequency_hz = 400000000U,
    .l3_cache_size = 12 * MiB,
    .package_cache_size = 24 * MiB,
    .processor_frequency_hz = 1600000000ULL,
    .bus_frequency_hz = 533333333ULL,
    .processor_ratio = IA64_FREQUENCY_RATIO(16, 1),
    .bus_ratio = IA64_FREQUENCY_RATIO(16, 3),
    .itc_ratio = IA64_FREQUENCY_RATIO(4, 1),
    .l3_associativity = 12,
};

static const TypeInfo ia64_cpu_type_info[] = {
    {
        .name = TYPE_IA64_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(IA64CPU),
        .instance_align = __alignof__(IA64CPU),
        .instance_finalize = ia64_cpu_finalize,
        .class_size = sizeof(IA64CPUClass),
        .class_init = ia64_cpu_class_init,
        .abstract = true,
    },
    {
        .name = IA64_CPU_TYPE_NAME("merced"),
        .parent = TYPE_IA64_CPU,
        .class_init = ia64_cpu_model_class_init,
        .class_data = &ia64_cpu_model_merced,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison"),
        .parent = TYPE_IA64_CPU,
        .class_init = ia64_cpu_model_class_init,
        .class_data = &ia64_cpu_model_madison,
    },
    {
        .name = IA64_CPU_TYPE_NAME("mckinley"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_mckinley,
    },
    {
        .name = IA64_CPU_TYPE_NAME("mckinley-900"),
        .parent = IA64_CPU_TYPE_NAME("mckinley"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_mckinley_900,
    },
    {
        .name = IA64_CPU_TYPE_NAME("mckinley-1000"),
        .parent = IA64_CPU_TYPE_NAME("mckinley"),
    },
    {
        .name = IA64_CPU_TYPE_NAME("deerfield"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_deerfield,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-1.5m"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_1500k,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-3m"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_3m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-4m"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_4m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-6m"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_6m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-9m"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_9m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("madison-zx6000"),
        .parent = IA64_CPU_TYPE_NAME("madison"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_madison_zx6000,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito"),
        .parent = TYPE_IA64_CPU,
        .class_init = ia64_cpu_model_class_init,
        .class_data = &ia64_cpu_model_montecito,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9010"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9010,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9015"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9015,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9020"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9020,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9030"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9030,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9040"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9040,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montecito-9050"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montecito_9050,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale"),
        .parent = IA64_CPU_TYPE_NAME("montecito"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9110n"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9110n,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9120n"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9120n,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9130m"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9130m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9140m"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9140m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9140n"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9140n,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9150m"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9150m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9150n"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
    },
    {
        .name = IA64_CPU_TYPE_NAME("montvale-9152m"),
        .parent = IA64_CPU_TYPE_NAME("montvale"),
        .class_init = ia64_cpu_variant_class_init,
        .class_data = &ia64_cpu_montvale_9152m,
    },
    {
        .name = IA64_CPU_TYPE_NAME("itanium"),
        .parent = TYPE_IA64_CPU,
        .class_init = ia64_cpu_model_class_init,
        .class_data = &ia64_cpu_model_merced,
    },
    {
        .name = IA64_CPU_TYPE_NAME("itanium2"),
        .parent = TYPE_IA64_CPU,
        .class_init = ia64_cpu_model_class_init,
        .class_data = &ia64_cpu_model_montecito,
    },
};

DEFINE_TYPES(ia64_cpu_type_info)
