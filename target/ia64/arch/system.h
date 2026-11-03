/* Internal IA-64 system-register architecture API. */

#ifndef TARGET_IA64_ARCH_SYSTEM_H
#define TARGET_IA64_ARCH_SYSTEM_H

#include "cpu.h"

uint64_t ia64_system_read_pr(const CPUIA64State *env);
void ia64_system_epc(CPUIA64State *env, uint64_t fault_ip, uint64_t raw,
                     uint32_t fault_slot);
void ia64_system_write_pr(CPUIA64State *env, uint64_t value, uint64_t mask);
uint64_t ia64_system_read_ar(CPUIA64State *env, uint32_t ar_num);
void ia64_system_validate_ar_access(CPUIA64State *env, uint64_t value,
                                    uint32_t ar_num, uint32_t write,
                                    uint64_t fault_ip, uint64_t raw,
                                    uint32_t slot);
void ia64_system_write_ar(CPUIA64State *env, uint32_t ar_num, uint64_t value);
uint64_t ia64_system_read_cr(CPUIA64State *env, uint32_t cr_num);
uint64_t ia64_system_validate_cr_access(CPUIA64State *env, uint64_t value,
                                        uint32_t cr_num, uint32_t write,
                                        uint64_t fault_ip, uint64_t raw,
                                        uint32_t slot);
uint64_t ia64_system_read_cpuid(CPUIA64State *env, uint64_t index);
uint64_t ia64_system_read_dahr_indexed(CPUIA64State *env, uint64_t index);
void ia64_system_write_dahr(CPUIA64State *env, uint32_t index, uint64_t value);
void ia64_system_reset_dahr(CPUIA64State *env);
uint64_t ia64_system_read_msr(CPUIA64State *env, uint64_t index);
void ia64_system_write_msr(CPUIA64State *env, uint64_t index, uint64_t value);
uint64_t ia64_system_read_dbr(CPUIA64State *env, uint32_t index);
void ia64_system_write_dbr(CPUIA64State *env, uint32_t index, uint64_t value);
uint64_t ia64_system_read_ibr(CPUIA64State *env, uint32_t index);
void ia64_system_write_ibr(CPUIA64State *env, uint32_t index, uint64_t value);
uint64_t ia64_system_read_pmc(CPUIA64State *env, uint32_t index);
void ia64_system_write_pmc(CPUIA64State *env, uint32_t index, uint64_t value);
uint64_t ia64_system_read_pmc_indexed(CPUIA64State *env, uint64_t index);
void ia64_system_write_pmc_indexed(CPUIA64State *env, uint64_t index,
                                   uint64_t value);
uint64_t ia64_system_read_pmd(CPUIA64State *env, uint32_t index);
uint64_t ia64_system_read_pmd_checked(CPUIA64State *env, uint64_t index,
                                      uint64_t fault_ip, uint64_t raw,
                                      uint32_t slot);
void ia64_system_write_pmd(CPUIA64State *env, uint32_t index, uint64_t value);
uint64_t ia64_system_read_pmd_indexed(CPUIA64State *env, uint64_t index);
void ia64_system_write_pmd_indexed(CPUIA64State *env, uint64_t index,
                                   uint64_t value);
void ia64_system_st_spill_unat(CPUIA64State *env, uint32_t reg, uint64_t addr);
void ia64_system_clear_psr_fault_suppression(CPUIA64State *env);
void ia64_system_set_psr_bn(CPUIA64State *env, uint32_t bank1);
void ia64_system_ssm(CPUIA64State *env, uint64_t imm);
void ia64_system_rsm(CPUIA64State *env, uint64_t imm);
void ia64_system_mov_psr_write(CPUIA64State *env, uint64_t value,
                               uint32_t unused);
uint64_t ia64_system_mov_rrgr_read(CPUIA64State *env, uint64_t rr_addr);
uint64_t ia64_system_validate_rr_value(CPUIA64State *env, uint64_t value,
                                       uint64_t fault_ip, uint64_t raw,
                                       uint32_t slot);
void ia64_system_mov_grrr_write(CPUIA64State *env, uint64_t rr_addr,
                                uint64_t value);
uint64_t ia64_system_mov_pkrgr_read(CPUIA64State *env, uint32_t pkr_num);
uint64_t ia64_system_mov_pkrgr_indexed_read(CPUIA64State *env,
                                            uint64_t pkr_num);
void ia64_system_mov_grpkr_write(CPUIA64State *env, uint32_t pkr_num,
                                 uint64_t value);
void ia64_system_mov_grpkr_indexed_write(CPUIA64State *env, uint64_t pkr_num,
                                         uint64_t value);

#endif /* TARGET_IA64_ARCH_SYSTEM_H */
