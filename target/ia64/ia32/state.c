/* IA-64 application-register mapping for native IA-32 execution. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "fpreg.h"
#include "arch/arch.h"
#include "ia32/ia32.h"

#define IA32_EFLAGS_CC_MASK \
    (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C)
#define IA32_EFLAGS_VALID_MASK 0x003f7fd7U
#define IA32_FPUS_TOP_MASK 0x3800U
#define IA32_FPUS_ES       0x0080U
#define IA32_FPUS_B        0x8000U
#define IA32_CFLG_VIRTUAL_MASK ((1U << 6) | (1U << 7) | (1U << 8))
#define IA32_CFLG_CR0_MASK \
    (CR0_PE_MASK | CR0_MP_MASK | CR0_EM_MASK | CR0_TS_MASK | \
     CR0_ET_MASK | CR0_NE_MASK | CR0_WP_MASK | CR0_AM_MASK | \
     CR0_NW_MASK | CR0_CD_MASK | CR0_PG_MASK)
#define IA32_CFLG_CR4_MASK \
    (CR4_VME_MASK | CR4_PVI_MASK | CR4_TSD_MASK | CR4_DE_MASK | \
     CR4_PSE_MASK | CR4_PAE_MASK | CR4_MCE_MASK | CR4_PGE_MASK | \
     CR4_PCE_MASK | CR4_OSFXSR_MASK | CR4_OSXMMEXCPT_MASK)

static uint32_t ia32_desc_flags(uint64_t desc)
{
    return (((desc >> 52) & 0xf) << DESC_TYPE_SHIFT) |
           (((desc >> 56) & 1) << DESC_S_SHIFT) |
           (((desc >> 57) & 3) << DESC_DPL_SHIFT) |
           (((desc >> 59) & 1) << DESC_P_SHIFT) |
           (((desc >> 60) & 1) << DESC_AVL_SHIFT) |
           (((desc >> 61) & 1) << DESC_L_SHIFT) |
           (((desc >> 62) & 1) << DESC_B_SHIFT) |
           (((desc >> 63) & 1) << DESC_G_SHIFT);
}

static uint32_t ia32_desc_limit(uint64_t desc)
{
    uint32_t limit = (desc >> 32) & 0xfffff;

    if (desc & (1ULL << 63)) {
        limit = (limit << 12) | 0xfff;
    }
    return limit;
}

static void ia32_load_desc(SegmentCache *cache, uint16_t selector,
                           uint64_t desc)
{
    cache->selector = selector;
    cache->base = (uint32_t)desc;
    cache->limit = ia32_desc_limit(desc);
    cache->flags = ia32_desc_flags(desc);
}

static uint64_t ia32_store_desc(const SegmentCache *cache, bool clear_ignored)
{
    uint32_t limit = cache->limit;
    uint32_t flags = cache->flags;
    uint64_t desc;

    if (flags & DESC_G_MASK) {
        limit >>= 12;
    }
    desc = (uint32_t)cache->base;
    desc |= (uint64_t)(limit & 0xfffff) << 32;
    desc |= (uint64_t)((flags >> DESC_TYPE_SHIFT) & 0xf) << 52;
    desc |= (uint64_t)((flags >> DESC_S_SHIFT) & 1) << 56;
    desc |= (uint64_t)((flags >> DESC_DPL_SHIFT) & 3) << 57;
    desc |= (uint64_t)((flags >> DESC_P_SHIFT) & 1) << 59;
    if (!clear_ignored) {
        desc |= (uint64_t)((flags >> DESC_AVL_SHIFT) & 1) << 60;
        desc |= (uint64_t)((flags >> DESC_L_SHIFT) & 1) << 61;
    }
    desc |= (uint64_t)((flags >> DESC_B_SHIFT) & 1) << 62;
    desc |= (uint64_t)((flags >> DESC_G_SHIFT) & 1) << 63;
    return desc;
}

void ia64_ia32_load_seg_cache(CPUX86State *xenv, int seg_reg,
                              unsigned int selector, target_ulong base,
                              unsigned int limit, unsigned int flags)
{
    /* IA-32 descriptor loads make the IA-64-only ignored bits read zero. */
    flags &= ~(DESC_AVL_MASK | DESC_L_MASK);
    cpu_x86_load_seg_cache(xenv, seg_reg, selector, base, limit, flags);
}

static uint16_t ia32_selector(uint64_t packed, unsigned slot)
{
    return packed >> (slot * 16);
}

static uint16_t ia32_ext_exp(uint32_t reg_exp)
{
    if (reg_exp == IA64_FP_REG_SPECIAL_EXP) {
        return 0x7fff;
    }
    if (reg_exp == 0) {
        return 0;
    }
    if (reg_exp >= 0xc000 && reg_exp - 0xc000 < 0x7fff) {
        return reg_exp - 0xc000;
    }
    return reg_exp < 0xc000 ? 0 : 0x7fff;
}

static uint32_t ia32_reg_exp(uint16_t ext_exp)
{
    if (ext_exp == 0x7fff) {
        return IA64_FP_REG_SPECIAL_EXP;
    }
    return ext_exp == 0 ? 0 : (uint32_t)ext_exp + 0xc000;
}

static void ia32_load_fp(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint64_t fsr = env->ar_fsr;
    uint64_t fcr = env->ar_fcr;
    unsigned i;

    cpu_init_fp_statuses(xenv);
    /* QEMU keeps TOP separately from the remaining x87 status word. */
    xenv->fpus = (uint16_t)fsr & ~IA32_FPUS_TOP_MASK;
    xenv->fpstt = (fsr >> 11) & 7;
    xenv->fpus = (xenv->fpus & ~IA32_FPUS_B) |
                 ((xenv->fpus & IA32_FPUS_ES) ? IA32_FPUS_B : 0);
    cpu_set_fpuc(xenv, fcr & 0x1fff);
    cpu_set_mxcsr(xenv, ((fcr >> 32) & 0xffc0) | ((fsr >> 32) & 0x3f));

    for (i = 0; i < 8; i++) {
        uint64_t low, high;
        uint16_t ext;

        ia64_fpreg_to_spill(env, 8 + i, &low, &high);
        ext = ia32_ext_exp(high & 0x1ffff);
        xenv->fpregs[i].d =
            ia64_make_floatx80(ext | (((high >> 17) & 1) << 15), low);
        xenv->fptags[i] = (fsr >> (16 + i * 2)) & 1;

        ia64_fpreg_to_spill(env, 16 + i * 2, &low, &high);
        xenv->xmm_regs[i].ZMM_Q(0) = low;
        ia64_fpreg_to_spill(env, 17 + i * 2, &low, &high);
        xenv->xmm_regs[i].ZMM_Q(1) = low;
    }

    xenv->fpip = (uint32_t)env->ar_fir;
    xenv->fpcs = env->ar_fir >> 32;
    xenv->fpop = env->ar_fir >> 48;
    xenv->fpdp = (uint32_t)env->ar_fdr;
    xenv->fpds = env->ar_fdr >> 32;
}

static void ia32_store_fp(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint64_t fsr =
        (xenv->fpus & ~IA32_FPUS_TOP_MASK) | (uint64_t)(xenv->fpstt & 7) << 11;
    uint64_t fcr = xenv->fpuc & 0x1fff;
    unsigned i;

    fsr &= 0xffff;
    for (i = 0; i < 8; i++) {
        uint16_t high = xenv->fpregs[i].d.high;
        uint64_t spill_high =
            ia32_reg_exp(high & 0x7fff) | (uint64_t)(high >> 15) << 17;

        ia64_fpreg_from_spill(env, 8 + i, xenv->fpregs[i].d.low, spill_high);
        fsr |= (uint64_t)(xenv->fptags[i] & 1) << (16 + i * 2);

        ia64_fpreg_from_spill(env, 16 + i * 2, xenv->xmm_regs[i].ZMM_Q(0),
                              IA64_FP_REG_INTEGER_EXP);
        ia64_fpreg_from_spill(env, 17 + i * 2, xenv->xmm_regs[i].ZMM_Q(1),
                              IA64_FP_REG_INTEGER_EXP);
    }
    fsr |= (uint64_t)(xenv->mxcsr & 0x3f) << 32;
    fcr |= (uint64_t)(xenv->mxcsr & 0xffc0) << 32;
    env->ar_fsr = fsr;
    env->ar_fcr = fcr;
    env->ar_fir = (uint32_t)xenv->fpip | (uint64_t)xenv->fpcs << 32 |
                  (uint64_t)(xenv->fpop & 0x7ff) << 48;
    env->ar_fdr = (uint32_t)xenv->fpdp | (uint64_t)xenv->fpds << 32;

    if (!(env->psr & IA64_PSR_DFL)) {
        env->psr |= IA64_PSR_MFL;
    }
}

static void ia32_init_features(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    IA64CPUClass *icc = ia64_env_cpu_class(env);

    /*
     * The native IA-32 engines expose the common processor-family feature
     * set.  PAE is implemented for the legacy IA-32 System Environment and
     * therefore remains advertised even though CFLG.pae is ignored in the
     * IA-64 System Environment.  PSN remains clear because that feature was
     * removed from the architecture.  IA64 reports that JMPE can return to
     * the 64-bit instruction set.
     */
    xenv->features[FEAT_1_EDX] =
        CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR |
        CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP |
        CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT |
        CPUID_PSE36 | CPUID_MMX | CPUID_FXSR | CPUID_SSE | CPUID_IA64;
    xenv->features[FEAT_1_ECX] = 0;
    xenv->cpuid_level = 2;
    xenv->cpuid_version = icc->ia32_cpuid_version;
    xenv->cpuid_vendor1 = CPUID_VENDOR_INTEL_1;
    xenv->cpuid_vendor2 = CPUID_VENDOR_INTEL_2;
    xenv->cpuid_vendor3 = CPUID_VENDOR_INTEL_3;
}

static void ia32_update_hflags(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t hflags = ia64_psr_cpl(env->psr);

    hflags |= (xenv->cr[IA32_CR0_INDEX] & CR0_PE_MASK) ? HF_PE_MASK : 0;
    hflags |= (xenv->cr[IA32_CR0_INDEX] & CR0_MP_MASK) ? HF_MP_MASK : 0;
    hflags |= (xenv->cr[IA32_CR0_INDEX] & CR0_EM_MASK) ? HF_EM_MASK : 0;
    hflags |= (xenv->cr[IA32_CR0_INDEX] & CR0_TS_MASK) ? HF_TS_MASK : 0;
    hflags |= (xenv->segs[R_CS].flags & DESC_B_MASK) ? HF_CS32_MASK : 0;
    hflags |= (xenv->segs[R_SS].flags & DESC_B_MASK) ? HF_SS32_MASK : 0;
    hflags |= (xenv->cr[IA32_CR4_INDEX] & CR4_OSFXSR_MASK) ? HF_OSFXSR_MASK : 0;

    if (!(xenv->cr[IA32_CR0_INDEX] & CR0_PE_MASK) || (xenv->eflags & VM_MASK) ||
        !(hflags & HF_CS32_MASK) || xenv->segs[R_DS].base ||
        xenv->segs[R_ES].base || xenv->segs[R_SS].base) {
        hflags |= HF_ADDSEG_MASK;
    }
    xenv->hflags = hflags;
}

uint32_t ia64_ia32_virtual_ip(const CPUIA64State *env)
{
    return (uint32_t)(env->ia32.segs[R_CS].base + env->ia32.eip);
}

void ia64_ia32_enter(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint64_t selectors_ds = env->gr[IA64_IA32_GR_DATA_SELECTORS];
    uint64_t selectors_cs = env->gr[IA64_IA32_GR_SYSTEM_SELECTORS];
    uint32_t eflags = ((uint32_t)env->ar_eflag & IA32_EFLAGS_VALID_MASK) | 2;
    unsigned i;

    memset(xenv, 0, sizeof(*xenv));
    env->ia32_data_breakpoints = 0;
    env->ia32_sse_instruction_active = false;
    xenv->a20_mask = -1;
    xenv->hflags2 = HF2_GIF_MASK;
    xenv->xcr0 = XSTATE_FP_MASK | XSTATE_SSE_MASK;
    ia32_init_features(env);

    /*
     * SDM vol. 1 section 6.4.3 makes entry with a NaT in any register
     * holding IA-32 state model-specific and undefined.  This model consumes
     * the low 32-bit payload here and establishes ordinary (non-NaT) IA-32
     * integer state; sync_to_ia64() clears the mapped GR NaT bits on exit.
     */
    for (i = 0; i < 8; i++) {
        xenv->regs[i] = (uint32_t)env->gr[IA64_IA32_GR_INTEGER_BASE + i];
    }

    /*
     * GR25 and GR26 are architecturally undefined during IA-32 execution.
     * Private scratch mirrors let the common exit path store every segment
     * descriptor uniformly; only ar.csd and ar.ssd expose these two values.
     */
    env->gr[IA64_IA32_GR_CS_DESCRIPTOR_SCRATCH] = env->ar_csd;
    env->gr[IA64_IA32_GR_SS_DESCRIPTOR_SCRATCH] = env->ar_ssd;
    ia32_load_desc(&xenv->segs[R_DS], ia32_selector(selectors_ds, 0),
                   env->gr[IA64_IA32_GR_DS_DESCRIPTOR]);
    ia32_load_desc(&xenv->segs[R_ES], ia32_selector(selectors_ds, 1),
                   env->gr[IA64_IA32_GR_ES_DESCRIPTOR]);
    ia32_load_desc(&xenv->segs[R_FS], ia32_selector(selectors_ds, 2),
                   env->gr[IA64_IA32_GR_FS_DESCRIPTOR]);
    ia32_load_desc(&xenv->segs[R_GS], ia32_selector(selectors_ds, 3),
                   env->gr[IA64_IA32_GR_GS_DESCRIPTOR]);
    ia32_load_desc(&xenv->segs[R_CS], ia32_selector(selectors_cs, 0),
                   env->ar_csd);
    ia32_load_desc(&xenv->segs[R_SS], ia32_selector(selectors_cs, 1),
                   env->ar_ssd);
    ia32_load_desc(&xenv->ldt, ia32_selector(selectors_cs, 2),
                   env->gr[IA64_IA32_GR_LDT_DESCRIPTOR]);
    ia32_load_desc(&xenv->tr, ia32_selector(selectors_cs, 3),
                   env->ar[IA64_IA32_AR_TSS_DESCRIPTOR]);
    ia32_load_desc(&xenv->gdt, 0, env->gr[IA64_IA32_GR_GDT_DESCRIPTOR]);
    memset(&xenv->idt, 0, sizeof(xenv->idt));

    /* CFLG.io, CFLG.if and CFLG.ii read as zero through IA-32 CR0. */
    xenv->cr[IA32_CR0_INDEX] = ((uint32_t)env->ar_cflg & IA32_CFLG_CR0_MASK &
                                ~IA32_CFLG_VIRTUAL_MASK) |
                               CR0_ET_MASK;
    xenv->cr[IA32_CR2_INDEX] = env->ar[IA64_IA32_AR_CR3_CR2] >> 32;
    xenv->cr[IA32_CR3_INDEX] = (uint32_t)env->ar[IA64_IA32_AR_CR3_CR2];
    xenv->cr[IA32_CR4_INDEX] = (env->ar_cflg >> 32) & IA32_CFLG_CR4_MASK;
    xenv->eip = (uint32_t)(env->ip - xenv->segs[R_CS].base);
    xenv->cc_src = eflags & IA32_EFLAGS_CC_MASK;
    xenv->cc_op = CC_OP_EFLAGS;
    xenv->df = (eflags & DF_MASK) ? -1 : 1;
    xenv->eflags = eflags & ~(IA32_EFLAGS_CC_MASK | DF_MASK);
    ia32_update_hflags(env);
    ia32_load_fp(env);
}

void ia64_ia32_sync_to_ia64(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;
    uint32_t eflags = cpu_compute_eflags(xenv) | 2;
    uint64_t ds_selectors;
    uint64_t cs_selectors;
    unsigned i;

    for (i = 0; i < 8; i++) {
        env->gr[IA64_IA32_GR_INTEGER_BASE + i] =
            (int64_t)(int32_t)xenv->regs[i];
        ia64_gr_nat_set(env, IA64_IA32_GR_INTEGER_BASE + i, false);
    }

    ds_selectors = (uint64_t)xenv->segs[R_DS].selector |
                   (uint64_t)xenv->segs[R_ES].selector << 16 |
                   (uint64_t)xenv->segs[R_FS].selector << 32 |
                   (uint64_t)xenv->segs[R_GS].selector << 48;
    cs_selectors = (uint64_t)xenv->segs[R_CS].selector |
                   (uint64_t)xenv->segs[R_SS].selector << 16 |
                   (uint64_t)xenv->ldt.selector << 32 |
                   (uint64_t)xenv->tr.selector << 48;
    env->gr[IA64_IA32_GR_DATA_SELECTORS] = ds_selectors;
    env->gr[IA64_IA32_GR_SYSTEM_SELECTORS] = cs_selectors;
    env->gr[IA64_IA32_GR_ES_DESCRIPTOR] =
        ia32_store_desc(&xenv->segs[R_ES], false);
    env->gr[IA64_IA32_GR_CS_DESCRIPTOR_SCRATCH] =
        ia32_store_desc(&xenv->segs[R_CS], true);
    env->gr[IA64_IA32_GR_SS_DESCRIPTOR_SCRATCH] =
        ia32_store_desc(&xenv->segs[R_SS], true);
    env->gr[IA64_IA32_GR_DS_DESCRIPTOR] =
        ia32_store_desc(&xenv->segs[R_DS], false);
    env->gr[IA64_IA32_GR_FS_DESCRIPTOR] =
        ia32_store_desc(&xenv->segs[R_FS], false);
    env->gr[IA64_IA32_GR_GS_DESCRIPTOR] =
        ia32_store_desc(&xenv->segs[R_GS], false);
    env->gr[IA64_IA32_GR_LDT_DESCRIPTOR] = ia32_store_desc(&xenv->ldt, false);
    env->gr[IA64_IA32_GR_GDT_DESCRIPTOR] = ia32_store_desc(&xenv->gdt, false);
    env->ar_csd = env->gr[IA64_IA32_GR_CS_DESCRIPTOR_SCRATCH];
    env->ar_ssd = env->gr[IA64_IA32_GR_SS_DESCRIPTOR_SCRATCH];
    env->ar[IA64_IA32_AR_TSS_DESCRIPTOR] = ia32_store_desc(&xenv->tr, false);
    env->ar_cflg =
        (env->ar_cflg & IA32_CFLG_VIRTUAL_MASK) |
        ((uint32_t)xenv->cr[IA32_CR0_INDEX] & IA32_CFLG_CR0_MASK &
         ~IA32_CFLG_VIRTUAL_MASK) |
        (uint64_t)((uint32_t)xenv->cr[IA32_CR4_INDEX] & IA32_CFLG_CR4_MASK)
            << 32;
    env->ar[IA64_IA32_AR_CR3_CR2] = (uint32_t)xenv->cr[IA32_CR3_INDEX] |
                                    (uint64_t)(uint32_t)xenv->cr[IA32_CR2_INDEX]
                                        << 32;

    env->ar_eflag = eflags & IA32_EFLAGS_VALID_MASK;
    ia64_ia32_sync_psr_cpl(env);
    env->ip = ia64_ia32_virtual_ip(env);
    ia32_store_fp(env);
}

void ia64_ia32_sync_psr_cpl(CPUIA64State *env)
{
    env->psr = (env->psr & ~IA64_PSR_CPL_MASK) |
               (uint64_t)(env->ia32.hflags & HF_CPL_MASK) << IA64_PSR_CPL_SHIFT;
}
