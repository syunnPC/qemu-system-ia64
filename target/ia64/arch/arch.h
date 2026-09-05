/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 architecture subsystem APIs.  These entry points are independent
 * of the TCG helper ABI; helper/ contains the ABI adapters.
 */

#ifndef TARGET_IA64_ARCH_ARCH_H
#define TARGET_IA64_ARCH_ARCH_H

#include "cpu.h"
#include "decode/opcode.h"

void ia64_rfi(CPUIA64State *env, uint64_t fault_ip, uint32_t fault_slot);
void ia64_rse_resume_incomplete_frame(CPUIA64State *env);
void ia64_write_cr(CPUIA64State *env, uint32_t cr_num, uint64_t value);
void ia64_tlb_serialize(CPUIA64State *env, uint32_t include_data,
                       uint32_t include_inst);
void ia64_mmu_data_access(CPUIA64State *env, uint64_t va, uint32_t size,
                          bool translated);
void ia64_mmu_fc(CPUIA64State *env, uint64_t addr,
                 bool instruction_cache_coherent);
void ia64_mmu_itr_insert(CPUIA64State *env, uint64_t pte,
                         uint64_t slot_reg, uint32_t is_data,
                         uint64_t raw, uint32_t fault_slot);
void ia64_mmu_ptr_purge(CPUIA64State *env, uint64_t ifa,
                        uint64_t size_reg, uint32_t is_data,
                        uint64_t raw, uint32_t fault_slot);
void ia64_mmu_invalidate_tc(CPUIA64State *env);
void ia64_mmu_ptc_purge(CPUIA64State *env, uint64_t va,
                        uint64_t size_reg, uint32_t mode,
                        uint64_t raw, uint32_t fault_slot);
uint64_t ia64_mmu_tpa(CPUIA64State *env, uint64_t va);
bool ia64_mmu_translate_debug(CPUIA64State *env, uint64_t va, uint64_t *pa);
uint64_t ia64_mmu_probe(CPUIA64State *env, uint64_t va, uint32_t is_write,
                        uint64_t access_level);
void ia64_mmu_probe_fault(CPUIA64State *env, uint64_t va,
                          uint32_t is_write, uint32_t is_rw,
                          uint64_t access_level);
void ia64_mmu_lfetch_fault(CPUIA64State *env, uint64_t va,
                           uint64_t fault_info, uint32_t hint);
void ia64_mmu_check_data_debug(CPUIA64State *env, uint64_t va,
                               uint32_t size, uint64_t isr_access,
                               uint8_t access_level, bool mandatory_rse,
                               uint64_t fault_ip, uint8_t fault_slot);
void ia64_check_instruction_debug(CPUIA64State *env, uint64_t address,
                                  uint8_t slot);
void ia64_mmu_check_semaphore_access(CPUIA64State *env, uint64_t va);
void ia64_mmu_check_montecito_16byte_access(CPUIA64State *env, uint64_t va,
                                            uint32_t is_write);
void ia64_mmu_check_alignment(CPUIA64State *env, uint64_t va,
                              uint32_t alignment_info,
                              uint64_t isr_access, uint64_t fault_info);
uint64_t ia64_mmu_speculative_probe(CPUIA64State *env, uint64_t va,
                                    uint32_t is_write, uint32_t is_ifetch,
                                    uint32_t debug_size,
                                    uint32_t alignment_info);
uint64_t ia64_mmu_speculative_int_probe(CPUIA64State *env, uint64_t va,
                                        uint32_t size);
uint64_t ia64_mmu_advanced_load_allowed(CPUIA64State *env, uint64_t va);
uint64_t ia64_mmu_tak(CPUIA64State *env, uint64_t va);
uint64_t ia64_mmu_thash(CPUIA64State *env, uint64_t va);
uint64_t ia64_mmu_ttag(CPUIA64State *env, uint64_t va);
void ia64_mmu_itc_insert(CPUIA64State *env, uint64_t pte,
                         uint32_t is_data, uint64_t raw,
                         uint32_t fault_slot);
bool ia64_mmu_insert_firmware_tc(CPUIA64State *env, uint64_t va,
                                 uint64_t pa, bool is_data,
                                 uint8_t page_shift);
bool ia64_firmware_boot_miss_mapping(IA64CPU *cpu, uint64_t va,
                                     uint64_t *pa, uint8_t *page_shift,
                                     bool *direct);
uint32_t ia64_firmware_debug_enter(CPUIA64State *env, uint64_t address);
uint32_t ia64_firmware_debug_save(CPUIA64State *env);
uint32_t ia64_firmware_debug_restore(CPUIA64State *env);
uint32_t ia64_pal_dispatch(CPUIA64State *env, uintptr_t ra);

void ia64_itm_timer_cb(void *opaque);
bool ia64_cpu_has_work(CPUState *cs);
uint64_t ia64_interrupt_itc_read(CPUIA64State *env);
G_NORETURN void ia64_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                             MMUAccessType access_type,
                                             int mmu_idx,
                                             uintptr_t retaddr);
G_NORETURN void ia64_raise_exception(CPUIA64State *env, uint32_t exception,
                                     uint64_t fault_ip, uint64_t fault_imm,
                                     uint32_t fault_slot);
void ia64_check_native_traps(CPUIA64State *env, uint64_t target_ip,
                             uint64_t source_ip, uint32_t slots,
                             uint32_t conditions, uint64_t base_isr,
                             uint64_t trap_psr);
G_NORETURN void ia64_ia32_unsupported(CPUIA64State *env);
G_NORETURN void ia64_raise_unaligned(CPUIA64State *env, uint64_t addr,
                                     uint64_t isr_access,
                                     uint64_t fault_info);
G_NORETURN void ia64_raise_nat_consumption(CPUIA64State *env,
                                           uint64_t isr_access,
                                           uint64_t fault_info);
G_NORETURN void ia64_raise_disabled_isa_transition(CPUIA64State *env,
                                                   uint64_t fault_ip,
                                                   uint32_t fault_slot);
G_NORETURN void ia64_raise_unimplemented_data_address(
    CPUIA64State *env, uint64_t va, uint64_t access, bool is_non_access,
    bool is_speculative, bool itlb_ed);
void ia64_raise_pre_unaligned_data_fault(CPUIA64State *env, uint64_t va,
                                         uint32_t is_write, uint32_t is_rw,
                                         uint64_t fault_ip,
                                         uint8_t fault_slot);
void ia64_cpu_do_interrupt(CPUState *cs);
bool ia64_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void ia64_rse_interrupt_window(CPUIA64State *env);
bool ia64_try_emulate_firmware_unaligned(CPUState *cs,
                                         uint64_t fault_addr,
                                         uint8_t fault_slot);

bool ia64_memory_allows_advanced_load(IA64MemorySpeculation spec);
bool ia64_data_address_to_phys(CPUIA64State *env, uint64_t va,
                               uint64_t *pa);
bool ia64_data_address_to_phys_attr(CPUIA64State *env, uint64_t va,
                                    uint64_t *pa,
                                    IA64MemorySpeculation *spec);
bool ia64_data_big_endian(CPUIA64State *env);
MemOp ia64_memop_for_opcode(Ia64Opcode opcode);
uint32_t ia64_memop_size(MemOp memop);
MemOp ia64_runtime_data_memop(CPUIA64State *env, MemOp memop);
uint32_t ia64_lduw_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra);
uint32_t ia64_ldl_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra);
uint64_t ia64_ldq_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra);
void ia64_stq_data_ra(CPUIA64State *env, uint64_t addr, uint64_t value,
                      uintptr_t ra);
uint64_t ia64_memory_cmpxchg(CPUIA64State *env, uint64_t addr,
                             uint64_t cmp, uint64_t val, uint32_t size,
                             uintptr_t ra);
uint64_t ia64_memory_cmp8xchg16(CPUIA64State *env, uint64_t addr,
                                uint64_t cmp, uint64_t val, uint64_t csd,
                                uintptr_t ra);
bool ia64_gr_nat_get(const CPUIA64State *env, uint32_t reg);
void ia64_gr_nat_set(CPUIA64State *env, uint32_t reg, bool nat);
void ia64_flush_on_pk_change(CPUIA64State *env, uint64_t old_psr);

uint64_t ia64_rse_current_cfm(const CPUIA64State *env);
void ia64_rse_save_context(CPUIA64State *env,
                           IA64RSEContextState *state);
void ia64_rse_restore_context(CPUIA64State *env,
                              const IA64RSEContextState *state);
void ia64_rse_return_from_min_state(CPUIA64State *env, uint64_t cfm);
uint32_t ia64_rse_nat_words_grow(uint64_t addr, uint32_t nregs);
uint64_t ia64_rse_read_rnat(const CPUIA64State *env);
uint64_t ia64_rse_read_rnat_defined(const CPUIA64State *env);
void ia64_rse_rnat_reloaded(CPUIA64State *env);
void ia64_rse_rnat_undefined(CPUIA64State *env, const char *site);
void ia64_rse_pop_return_frame(CPUIA64State *env, uint64_t pfs);
#ifdef CONFIG_DEBUG_TCG
void ia64_rse_check(CPUIA64State *env, const char *site);
#else
#define ia64_rse_check(env, site) do { } while (0)
#endif
void ia64_rse_br_call(CPUIA64State *env, uint32_t b_reg,
                      uint64_t next_ip, uint64_t target);
void ia64_rse_br_ia(CPUIA64State *env, uint32_t b_reg,
                    uint64_t fault_ip, uint32_t fault_slot);
void ia64_rse_br_ret(CPUIA64State *env, uint32_t b_reg,
                     uint64_t source_ip, uint32_t source_slot);
void ia64_rse_alloc(CPUIA64State *env, uint32_t r1, uint32_t pfm,
                    uint64_t fault_ip, uint32_t slot, uintptr_t ra);
void ia64_rse_cover(CPUIA64State *env);
void ia64_rse_flush(CPUIA64State *env, uintptr_t ra);
void ia64_rse_load(CPUIA64State *env, uint64_t fault_ip, uint64_t raw,
                   uint32_t slot, uintptr_t ra);
bool ia64_rse_br_cexit(CPUIA64State *env);
bool ia64_rse_br_ctop(CPUIA64State *env);
bool ia64_rse_br_wexit(CPUIA64State *env, uint32_t qp);
bool ia64_rse_br_wtop(CPUIA64State *env, uint32_t qp);
void ia64_rse_clrrrb(CPUIA64State *env, uint32_t predicate_only);
uint64_t ia64_rse_cloop_zero_st1(CPUIA64State *env, uint32_t base_reg,
                                 uint32_t mmu_idx, uint32_t max_stores,
                                 uintptr_t ra);

void ia64_invalidate_stacked_alat(CPUIA64State *env);
void ia64_invalidate_rotating_fp_alat(CPUIA64State *env);
void ia64_invalidate_alat_reg_range(CPUIA64State *env, uint32_t first,
                                    uint32_t last, bool fp);
void ia64_invalidate_alat_store(CPUIA64State *env, uint64_t addr,
                                uint32_t size);
void ia64_alat_notify_store(CPUIA64State *env);
void ia64_alat_write_begin(CPUIA64State *env);
void ia64_alat_write_end(CPUIA64State *env, uint64_t addr, uint32_t size);
void ia64_alat_write_cancel(CPUIA64State *env);
void ia64_alat_write_abort(CPUIA64State *env);
void ia64_invalidate_alat_phys_range(CPUIA64State *env, uint64_t pa,
                                     uint64_t size);
uint64_t ia64_alat_chk_a(CPUIA64State *env, uint64_t va, uint32_t reg);
void ia64_alat_invala(CPUIA64State *env);
uint64_t ia64_alat_load_begin(CPUIA64State *env);
void ia64_alat_set(CPUIA64State *env, uint32_t reg, uint64_t addr,
                   uint32_t size, uint64_t generation);
void ia64_alat_set_fp(CPUIA64State *env, uint32_t reg, uint64_t addr,
                      uint32_t size, uint64_t generation);
void ia64_alat_invalidate_reg(CPUIA64State *env, uint32_t reg);
void ia64_alat_invalidate_fp_reg(CPUIA64State *env, uint32_t reg);
uint64_t ia64_alat_check_load(CPUIA64State *env, uint32_t reg,
                              uint32_t clear);
uint64_t ia64_alat_check_load_addr(CPUIA64State *env, uint32_t reg,
                                   uint64_t addr, uint32_t size,
                                   uint32_t clear);
uint64_t ia64_alat_check_load_fp(CPUIA64State *env, uint32_t reg,
                                 uint32_t clear);
uint64_t ia64_alat_check_load_fp_addr(CPUIA64State *env, uint32_t reg,
                                      uint64_t addr, uint32_t size,
                                      uint32_t clear);

#endif /* TARGET_IA64_ARCH_ARCH_H */
