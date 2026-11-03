/* IA-32 application-execution support. */

#ifndef TARGET_IA64_IA32_IA32_H
#define TARGET_IA64_IA32_IA32_H

#include "cpu.h"

enum {
    IA64_IA32_INTERCEPT_INSTRUCTION = 0,
    IA64_IA32_INTERCEPT_GATE = 1,
    IA64_IA32_INTERCEPT_SYSTEM_FLAG = 2,
    IA64_IA32_INTERCEPT_LOCK = 4,
};

enum {
    IA64_IA32_SEG_ACCESS_READ = 1,
    IA64_IA32_SEG_ACCESS_WRITE = 2,
};

/* Private marker carried through QEMU's INT helper ABI. */
#define IA64_IA32_INT_BREAKPOINT 0x100

void ia64_ia32_enter(CPUIA64State *env);
void ia64_ia32_load_seg_cache(CPUX86State *xenv, int seg_reg,
                              unsigned int selector, target_ulong base,
                              unsigned int limit, unsigned int flags);
void ia64_ia32_sync_psr_cpl(CPUIA64State *env);
void ia64_ia32_sync_to_ia64(CPUIA64State *env);
void ia64_ia32_abort_sse_instruction(CPUIA64State *env);
uint32_t ia64_ia32_virtual_ip(const CPUIA64State *env);
bool ia64_ia32_code_fetch_valid(CPUX86State *xenv, uint32_t linear,
                                unsigned size);
bool ia64_ia32_code_fetch_fault_probes_second_page(CPUX86State *xenv,
                                                   uint32_t insn,
                                                   uint32_t linear,
                                                   unsigned size);
void ia64_ia32_check_fetch_fault_priority(CPUIA64State *env, uint32_t linear,
                                          uintptr_t retaddr);
void ia64_ia32_record_data_access(CPUIA64State *env, uint32_t address,
                                  unsigned size, unsigned access);
void ia64_ia32_record_io_access(CPUIA64State *env, uint64_t address,
                                unsigned size, unsigned access);
void ia64_ia32_probe_access(CPUIA64State *env, uint32_t address, unsigned size,
                            MMUAccessType access_type, int mmu_idx,
                            uintptr_t retaddr);
void ia64_ia32_check_segment_access(CPUX86State *xenv, uint32_t linear,
                                    unsigned seg, unsigned size,
                                    unsigned access, uintptr_t retaddr);
void ia64_ia32_check_block_alignment(CPUX86State *xenv, vaddr addr,
                                     unsigned size, uintptr_t retaddr);
void ia64_ia32_check_psr_alignment(CPUX86State *xenv, vaddr addr, unsigned size,
                                   uintptr_t retaddr);
G_NORETURN void ia64_ia32_unaligned_access(CPUX86State *xenv, vaddr addr,
                                           MMUAccessType access_type,
                                           uintptr_t retaddr);

G_NORETURN void ia64_ia32_raise_intercept(CPUX86State *xenv, uint32_t intercept,
                                          uint32_t code, uintptr_t retaddr);
G_NORETURN void ia64_ia32_gate_intercept(CPUX86State *xenv, uint16_t selector,
                                         uint32_t desc_low, uint32_t desc_high,
                                         uint32_t ident, target_ulong next_eip,
                                         uintptr_t retaddr);
#endif /* TARGET_IA64_IA32_IA32_H */
