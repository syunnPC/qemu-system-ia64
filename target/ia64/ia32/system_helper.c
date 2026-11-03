/* Native IA-32 system-environment adapters. */

#include "helper-compat.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "target/i386/tcg/helper-tcg.h"
#include "target/i386/tcg/tcg-cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "ia32/ia32.h"

void helper_ia32_rsm(CPUIA64State *env);
void helper_ia32_rdtsc(CPUIA64State *env);
void helper_ia32_rdpmc(CPUIA64State *env);

/* A macro, so that GETPC() is the helper's own return into translated code. */
#define ia32_instruction_intercept(xenv)                              \
    ia64_ia32_raise_intercept((xenv), IA64_IA32_INTERCEPT_INSTRUCTION, \
                              0, GETPC())

static uint64_t ia32_io_address(CPUIA64State *env, uint32_t port)
{
    /* Preserve bit 16 for the three architected bytes beyond port 0xffff. */
    return env->ar_kr0 | ((uint64_t)(port & ~3U) << 10) | (port & 0xfff);
}

static uint64_t ia32_io_checked_address(CPUX86State *xenv, uint32_t port,
                                        uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    uint64_t addr = ia32_io_address(env, port);

    if (!ia64_va_is_implemented((CPUIA64State *)xenv, addr)) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    return addr;
}

static void ia32_io_prepare(CPUX86State *xenv, uint32_t port, unsigned size,
                            MMUAccessType type, uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);
    unsigned access = type == MMU_DATA_STORE ? IA64_IA32_SEG_ACCESS_WRITE
                                             : IA64_IA32_SEG_ACCESS_READ;
    uint64_t first = ia32_io_checked_address(xenv, port, retaddr);
    unsigned i;

    /* Validate every possibly discontinuous port byte before doing I/O. */
    for (i = 0; i < size; i++) {
        uint64_t addr = ia32_io_checked_address(xenv, port + i, retaddr);

        probe_access(env, addr, 1, type, mmu_idx, retaddr);
        ia64_ia32_record_io_access(env, addr, 1, access);
    }

    /* EFLAG.ac does not detect unaligned I/O-port references. */
    if ((env->psr & IA64_PSR_AC) && size > 1 && (first & (size - 1))) {
        env->cr_ifa = first;
        raise_exception_ra(xenv, EXCP11_ALGN, retaddr);
    }
}

static uint16_t ia32_tss_lduw(CPUX86State *xenv, uint32_t addr, int mmu_idx,
                              uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;

    ia64_ia32_probe_access(env, addr, 2, MMU_DATA_LOAD, mmu_idx, retaddr);
    ia64_ia32_record_data_access(env, addr, 2, IA64_IA32_SEG_ACCESS_READ);
    ia64_ia32_check_psr_alignment(xenv, addr, 2, retaddr);
    return cpu_lduw_le_mmuidx_ra(env, addr, mmu_idx, retaddr);
}

/*
 * RETADDR is the helper's GETPC(): taken here, in a function the helper
 * calls, it would not lead back into the translated code, and a fault would
 * leave EIP at an earlier instruction.
 */
static uint32_t ia32_io_read(CPUX86State *xenv, uint32_t port, unsigned size,
                             uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    CPUState *cs = env_cpu(env);
    uint64_t addr;
    int mmu_idx;

    if (!(env->psr & IA64_PSR_DT)) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    mmu_idx = cpu_mmu_index(cs, false);
    ia32_io_prepare(xenv, port, size, MMU_DATA_LOAD, retaddr);
    addr = ia32_io_checked_address(xenv, port, retaddr);
    if ((port & 3) + size > 4) {
        uint32_t value = 0;
        unsigned i;

        for (i = 0; i < size; i++) {
            addr = ia32_io_checked_address(xenv, port + i, retaddr);
            value |= cpu_ldub_mmuidx_ra(env, addr, mmu_idx, retaddr) << (i * 8);
        }
        return value;
    }
    switch (size) {
    case 1:
        return cpu_ldub_mmuidx_ra(env, addr, mmu_idx, retaddr);
    case 2:
        return cpu_lduw_le_mmuidx_ra(env, addr, mmu_idx, retaddr);
    case 4:
        return cpu_ldl_le_mmuidx_ra(env, addr, mmu_idx, retaddr);
    default:
        g_assert_not_reached();
    }
}

static void ia32_io_write(CPUX86State *xenv, uint32_t port, uint32_t value,
                          unsigned size, uintptr_t retaddr)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    CPUState *cs = env_cpu(env);
    uint64_t addr;
    int mmu_idx;

    if (!(env->psr & IA64_PSR_DT)) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    mmu_idx = cpu_mmu_index(cs, false);
    ia32_io_prepare(xenv, port, size, MMU_DATA_STORE, retaddr);
    addr = ia32_io_checked_address(xenv, port, retaddr);
    if ((port & 3) + size > 4) {
        unsigned i;

        for (i = 0; i < size; i++) {
            addr = ia32_io_checked_address(xenv, port + i, retaddr);
            cpu_stb_mmuidx_ra(env, addr, value >> (i * 8), mmu_idx, retaddr);
        }
        return;
    }
    switch (size) {
    case 1:
        cpu_stb_mmuidx_ra(env, addr, value, mmu_idx, retaddr);
        break;
    case 2:
        cpu_stw_le_mmuidx_ra(env, addr, value, mmu_idx, retaddr);
        break;
    case 4:
        cpu_stl_le_mmuidx_ra(env, addr, value, mmu_idx, retaddr);
        break;
    default:
        g_assert_not_reached();
    }
}

void helper_outb(CPUX86State *xenv, uint32_t port, uint32_t value)
{
    ia32_io_write(xenv, port, value, 1, GETPC());
}

target_ulong helper_inb(CPUX86State *xenv, uint32_t port)
{
    return ia32_io_read(xenv, port, 1, GETPC());
}

void helper_outw(CPUX86State *xenv, uint32_t port, uint32_t value)
{
    ia32_io_write(xenv, port, value, 2, GETPC());
}

target_ulong helper_inw(CPUX86State *xenv, uint32_t port)
{
    return ia32_io_read(xenv, port, 2, GETPC());
}

void helper_outl(CPUX86State *xenv, uint32_t port, uint32_t value)
{
    ia32_io_write(xenv, port, value, 4, GETPC());
}

target_ulong helper_inl(CPUX86State *xenv, uint32_t port)
{
    return ia32_io_read(xenv, port, 4, GETPC());
}

void helper_check_io(CPUX86State *xenv, uint32_t port, uint32_t size)
{
    CPUIA64State *env = (CPUIA64State *)xenv;
    uintptr_t retaddr = GETPC();
    uint32_t io_offset, bits, mask;
    unsigned type = (xenv->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);

    /* With a failed IOPL check, CFLG.io=0 denies access without a TSS read. */
    if (!(env->ar_cflg & (1ULL << 6))) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    if ((xenv->tr.flags & (DESC_S_MASK | DESC_P_MASK)) != DESC_P_MASK ||
        (type != 9 && type != 11) || xenv->tr.limit < 103) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    io_offset = ia32_tss_lduw(xenv, xenv->tr.base + 0x66, mmu_idx, retaddr);
    io_offset += port >> 3;
    if (io_offset + 1 > xenv->tr.limit) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
    bits = ia32_tss_lduw(xenv, xenv->tr.base + io_offset, mmu_idx, retaddr);
    bits >>= port & 7;
    mask = (1u << size) - 1;
    if (bits & mask) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, retaddr);
    }
}

void helper_ia32_rdtsc(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint64_t value;

    if (ia64_psr_cpl(env->psr) != 0 &&
        ((env->psr & IA64_PSR_SI) ||
         (xenv->cr[IA32_CR4_INDEX] & CR4_TSD_MASK))) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, GETPC());
    }

    value = ia64_itc_read(env);
    xenv->regs[R_EAX] = (uint32_t)value;
    xenv->regs[R_EDX] = value >> 32;
}

void helper_ia32_rdpmc(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t counter = xenv->regs[R_ECX];
    uint32_t index = counter + 4;
    uint64_t value;

    /* Native IA-32 execution exposes the four generic PMD4..PMD7 counters. */
    if (counter >= 4 || (ia64_psr_cpl(env->psr) != 0 &&
                         ((env->psr & IA64_PSR_SP) ||
                          !(xenv->cr[IA32_CR4_INDEX] & CR4_PCE_MASK) ||
                          (env->pmc[index] & (1ULL << 6))))) {
        raise_exception_err_ra(xenv, EXCP0D_GPF, 0, GETPC());
    }

    value = ia64_system_read_pmd(env, index);
    xenv->regs[R_EAX] = (uint32_t)value;
    xenv->regs[R_EDX] = value >> 32;
}

void helper_bpt_io(CPUX86State *xenv, uint32_t port, uint32_t size,
                   target_ulong next_eip)
{
}

void helper_set_dr(CPUX86State *xenv, int reg, target_ulong value)
{
    ia32_instruction_intercept(xenv);
}

target_ulong helper_get_dr(CPUX86State *xenv, int reg)
{
    ia32_instruction_intercept(xenv);
}

void helper_syscall(CPUX86State *xenv, int next_eip_addend)
{
    ia32_instruction_intercept(xenv);
}

void helper_ia32_rsm(CPUIA64State *env)
{
    ia32_instruction_intercept(&env->ia32);
}

void helper_rdmsr(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_wrmsr(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

target_ulong helper_read_cr8(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_write_crN(CPUX86State *xenv, int reg, target_ulong value)
{
    ia32_instruction_intercept(xenv);
}

void helper_svm_check_intercept(CPUX86State *xenv, uint32_t type)
{
}

void helper_svm_check_io(CPUX86State *xenv, uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
}

void helper_vmrun(CPUX86State *xenv, int aflag, int next_eip_addend)
{
    ia32_instruction_intercept(xenv);
}

void helper_vmmcall(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_vmload(CPUX86State *xenv, int aflag)
{
    ia32_instruction_intercept(xenv);
}

void helper_vmsave(CPUX86State *xenv, int aflag)
{
    ia32_instruction_intercept(xenv);
}

void helper_stgi(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_clgi(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_flush_page(CPUX86State *xenv, target_ulong addr)
{
    ia32_instruction_intercept(xenv);
}

G_NORETURN void helper_hlt(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void helper_monitor(CPUX86State *xenv, target_ulong addr)
{
    ia32_instruction_intercept(xenv);
}

G_NORETURN void helper_mwait(CPUX86State *xenv, int next_eip_addend)
{
    ia32_instruction_intercept(xenv);
}

void helper_bndck(CPUX86State *xenv, uint32_t op)
{
    ia32_instruction_intercept(xenv);
}

uint64_t helper_bndldx32(CPUX86State *xenv, target_ulong base, target_ulong ptr)
{
    ia32_instruction_intercept(xenv);
}

uint64_t helper_bndldx64(CPUX86State *xenv, target_ulong base, target_ulong ptr)
{
    ia32_instruction_intercept(xenv);
}

void helper_bndstx32(CPUX86State *xenv, target_ulong base, target_ulong ptr,
                     uint64_t lb, uint64_t ub)
{
    ia32_instruction_intercept(xenv);
}

void helper_bndstx64(CPUX86State *xenv, target_ulong base, target_ulong ptr,
                     uint64_t lb, uint64_t ub)
{
    ia32_instruction_intercept(xenv);
}

void helper_bnd_jmp(CPUX86State *xenv)
{
    ia32_instruction_intercept(xenv);
}

void cpu_svm_check_intercept_param(CPUX86State *xenv, uint32_t type,
                                   uint64_t param, uintptr_t retaddr)
{
}

void cpu_sync_bndcs_hflags(CPUX86State *xenv)
{
    xenv->hflags &= ~(HF_MPX_EN_MASK | HF_MPX_IU_MASK);
    xenv->hflags2 &= ~HF2_MPX_PR_MASK;
}

void cpu_sync_avx_hflag(CPUX86State *xenv)
{
    xenv->hflags &= ~HF_AVX_EN_MASK;
}

void cpu_x86_update_cr3(CPUX86State *xenv, target_ulong value)
{
    xenv->cr[IA32_CR3_INDEX] = (uint32_t)value;
}

void cpu_x86_update_dr7(CPUX86State *xenv, uint32_t value)
{
    xenv->dr[7] = value | DR7_FIXED_1;
}

int x86_mmu_index_pl(CPUX86State *xenv, unsigned pl)
{
    CPUIA64State *env = (CPUIA64State *)xenv;

    return cpu_mmu_index(env_cpu(env), false);
}

uint32_t x86_ldl_phys(CPUState *cs, hwaddr addr)
{
    return address_space_ldl_le(&address_space_memory, addr,
                                MEMTXATTRS_UNSPECIFIED, NULL);
}

void x86_stl_phys(CPUState *cs, hwaddr addr, uint32_t value)
{
    address_space_stl_le(&address_space_memory, addr, value,
                         MEMTXATTRS_UNSPECIFIED, NULL);
}

void handle_even_inj(CPUX86State *xenv, int intno, int is_int, int error_code,
                     int is_hw, int rm)
{
}

void cpu_clear_ignne(void)
{
}

void fpu_check_raise_ferr_irq(CPUX86State *xenv)
{
}

uint32_t xsave_area_size(uint64_t mask, bool compacted)
{
    return 576;
}

void cpu_x86_cpuid(CPUX86State *xenv, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    *eax = *ebx = *ecx = *edx = 0;
    switch (index) {
    case 0:
        *eax = xenv->cpuid_level;
        *ebx = xenv->cpuid_vendor1;
        *edx = xenv->cpuid_vendor2;
        *ecx = xenv->cpuid_vendor3;
        break;
    case 1:
        *eax = xenv->cpuid_version;
        *ecx = xenv->features[FEAT_1_ECX];
        *edx = xenv->features[FEAT_1_EDX];
        break;
    case 2: {
        IA64CPUClass *icc = ia64_env_cpu_class((CPUIA64State *)xenv);

        *eax = icc->ia32_cpuid_leaf2[0];
        *ebx = icc->ia32_cpuid_leaf2[1];
        *ecx = icc->ia32_cpuid_leaf2[2];
        *edx = icc->ia32_cpuid_leaf2[3];
        break;
    }
    default:
        break;
    }
}

uint64_t cpu_get_tsc(CPUX86State *xenv)
{
    return ia64_itc_read((CPUIA64State *)xenv);
}
