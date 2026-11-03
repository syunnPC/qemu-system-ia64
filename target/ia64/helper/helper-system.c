/* IA-64 TCG helper ABI adapters for system-register operations. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "arch/arch.h"
#include "arch/system.h"

uint64_t helper_read_pr(CPUIA64State *env)
{
    return ia64_system_read_pr(env);
}

void helper_epc(CPUIA64State *env, uint64_t fault_ip, uint64_t raw,
                uint32_t fault_slot)
{
    ia64_system_epc(env, fault_ip, raw, fault_slot);
}

void helper_write_pr(CPUIA64State *env, uint64_t value, uint64_t mask)
{
    ia64_system_write_pr(env, value, mask);
}

uint64_t helper_read_ar(CPUIA64State *env, uint32_t ar_num)
{
    return ia64_system_read_ar(env, ar_num);
}

void helper_validate_ar_access(CPUIA64State *env, uint64_t value,
                               uint32_t ar_num, uint32_t write,
                               uint64_t fault_ip, uint64_t raw, uint32_t slot)
{
    ia64_system_validate_ar_access(env, value, ar_num, write, fault_ip, raw,
                                   slot);
}

void helper_write_ar(CPUIA64State *env, uint32_t ar_num, uint64_t value)
{
    ia64_system_write_ar(env, ar_num, value);
}

uint64_t helper_read_cr(CPUIA64State *env, uint32_t cr_num)
{
    return ia64_system_read_cr(env, cr_num);
}

void helper_write_cr(CPUIA64State *env, uint32_t cr_num, uint64_t value)
{
    ia64_write_cr(env, cr_num, value);
}

uint64_t helper_validate_cr_access(CPUIA64State *env, uint64_t value,
                                   uint32_t cr_num, uint32_t write,
                                   uint64_t fault_ip, uint64_t raw,
                                   uint32_t slot)
{
    return ia64_system_validate_cr_access(env, value, cr_num, write, fault_ip,
                                          raw, slot);
}

uint64_t helper_read_cpuid(CPUIA64State *env, uint64_t index)
{
    return ia64_system_read_cpuid(env, index);
}

uint64_t helper_read_dahr_indexed(CPUIA64State *env, uint64_t index)
{
    return ia64_system_read_dahr_indexed(env, index);
}

void helper_write_dahr(CPUIA64State *env, uint32_t index, uint64_t value)
{
    ia64_system_write_dahr(env, index, value);
}

uint64_t helper_read_msr(CPUIA64State *env, uint64_t index)
{
    return ia64_system_read_msr(env, index);
}

void helper_write_msr(CPUIA64State *env, uint64_t index, uint64_t value)
{
    ia64_system_write_msr(env, index, value);
}

uint64_t helper_read_dbr(CPUIA64State *env, uint32_t index)
{
    return ia64_system_read_dbr(env, index);
}

void helper_write_dbr(CPUIA64State *env, uint32_t index, uint64_t value)
{
    ia64_system_write_dbr(env, index, value);
}

uint64_t helper_read_ibr(CPUIA64State *env, uint32_t index)
{
    return ia64_system_read_ibr(env, index);
}

void helper_write_ibr(CPUIA64State *env, uint32_t index, uint64_t value)
{
    ia64_system_write_ibr(env, index, value);
}

uint64_t helper_read_pmc(CPUIA64State *env, uint32_t index)
{
    return ia64_system_read_pmc(env, index);
}

void helper_write_pmc(CPUIA64State *env, uint32_t index, uint64_t value)
{
    ia64_system_write_pmc(env, index, value);
}

uint64_t helper_read_pmc_indexed(CPUIA64State *env, uint64_t index)
{
    return ia64_system_read_pmc_indexed(env, index);
}

void helper_write_pmc_indexed(CPUIA64State *env, uint64_t index, uint64_t value)
{
    ia64_system_write_pmc_indexed(env, index, value);
}

uint64_t helper_read_pmd(CPUIA64State *env, uint32_t index)
{
    return ia64_system_read_pmd(env, index);
}

uint64_t helper_read_pmd_checked(CPUIA64State *env, uint64_t index,
                                 uint64_t fault_ip, uint64_t raw, uint32_t slot)
{
    return ia64_system_read_pmd_checked(env, index, fault_ip, raw, slot);
}

void helper_write_pmd(CPUIA64State *env, uint32_t index, uint64_t value)
{
    ia64_system_write_pmd(env, index, value);
}

uint64_t helper_read_pmd_indexed(CPUIA64State *env, uint64_t index)
{
    return ia64_system_read_pmd_indexed(env, index);
}

void helper_write_pmd_indexed(CPUIA64State *env, uint64_t index, uint64_t value)
{
    ia64_system_write_pmd_indexed(env, index, value);
}

void helper_st_spill_unat(CPUIA64State *env, uint32_t reg, uint64_t addr)
{
    ia64_system_st_spill_unat(env, reg, addr);
}

void helper_clear_psr_fault_suppression(CPUIA64State *env)
{
    ia64_system_clear_psr_fault_suppression(env);
}

void helper_set_psr_bn(CPUIA64State *env, uint32_t bank1)
{
    ia64_system_set_psr_bn(env, bank1);
}

void helper_ssm(CPUIA64State *env, uint64_t imm)
{
    ia64_system_ssm(env, imm);
}

void helper_rsm(CPUIA64State *env, uint64_t imm)
{
    ia64_system_rsm(env, imm);
}

void helper_mov_psr_write(CPUIA64State *env, uint64_t value, uint32_t unused)
{
    ia64_system_mov_psr_write(env, value, unused);
}

uint64_t helper_mov_rrgr_read(CPUIA64State *env, uint64_t rr_addr)
{
    return ia64_system_mov_rrgr_read(env, rr_addr);
}

uint64_t helper_validate_rr_value(CPUIA64State *env, uint64_t value,
                                  uint64_t fault_ip, uint64_t raw,
                                  uint32_t slot)
{
    return ia64_system_validate_rr_value(env, value, fault_ip, raw, slot);
}

void helper_mov_grrr_write(CPUIA64State *env, uint64_t rr_addr, uint64_t value)
{
    ia64_system_mov_grrr_write(env, rr_addr, value);
}

uint64_t helper_mov_pkrgr_read(CPUIA64State *env, uint32_t pkr_num)
{
    return ia64_system_mov_pkrgr_read(env, pkr_num);
}

uint64_t helper_mov_pkrgr_indexed_read(CPUIA64State *env, uint64_t pkr_num)
{
    return ia64_system_mov_pkrgr_indexed_read(env, pkr_num);
}

void helper_mov_grpkr_write(CPUIA64State *env, uint32_t pkr_num, uint64_t value)
{
    ia64_system_mov_grpkr_write(env, pkr_num, value);
}

void helper_mov_grpkr_indexed_write(CPUIA64State *env, uint64_t pkr_num,
                                    uint64_t value)
{
    ia64_system_mov_grpkr_indexed_write(env, pkr_num, value);
}
