#ifndef IA64_CPU_H
#define IA64_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/cpu-interrupt.h"
#include "fpu/softfloat.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "qemu/timer.h"

#ifdef CONFIG_USER_ONLY
#error "IA-64 target supports system mode only"
#endif

#define CPU_RESOLVING_TYPE TYPE_IA64_CPU

#undef NB_MMU_MODES
#define MMU_PHYS_IDX       0
#define MMU_IDX_VIRT_CPL0  1
#define MMU_IDX_VIRT_CPL1  2
#define MMU_IDX_VIRT_CPL2  3
#define MMU_IDX_VIRT_CPL3  4
#define MMU_IDX_RSE        5
#define NB_MMU_MODES       6

#define MMU_IDX_VIRT_CPL(cpl) (MMU_IDX_VIRT_CPL0 + (cpl))
#define MMU_IDX_VIRT_MASK \
    (((1u << 4) - 1) << MMU_IDX_VIRT_CPL0)
#define MMU_IDX_TRANSLATED_MASK \
    (MMU_IDX_VIRT_MASK | (1u << MMU_IDX_RSE))

#define IA64_GR_COUNT    128
#define IA64_STACKED_GR_BASE   32
#define IA64_STACKED_GR_COUNT  (IA64_GR_COUNT - IA64_STACKED_GR_BASE)
/*
 * Every suspended partial collection owns at least one record in the
 * physical stacked-register window, so that window also bounds the cache.
 */
#define IA64_RSE_RNAT_SHADOW_COUNT IA64_STACKED_GR_COUNT
#define IA64_PR_COUNT    64
#define IA64_BR_COUNT    8
#define IA64_AR_COUNT    128
#define IA64_CR_COUNT    128
#define IA64_DBR_COUNT   16
#define IA64_DBR_IMPLEMENTED_COUNT 8
#define IA64_FR_COUNT    128
#define IA64_IBR_COUNT   16
#define IA64_IBR_IMPLEMENTED_COUNT 8
#define IA64_PMC_COUNT   64
#define IA64_PMD_COUNT   64
#define IA64_PKR_COUNT   16
#define IA64_RR_COUNT    8
#define IA64_MSR_COUNT   1024
/* Maximum translation-register count implemented by a supported CPU model. */
#define IA64_TR_MAX      64
/* Storage upper bound; each CPU model selects its usable TLB capacity. */
#define IA64_TLB_MAX     128
/* Merced's non-architectural first-level data TLB. */
#define IA64_DTLB1_MAX   32
/* Direct lookup uses minimum-page keys and a power-of-two bucket count. */
#define IA64_DTLB1_LOOKUP_PAGE_SHIFT 12
#define IA64_DTLB1_LOOKUP_SIZE 64

/*
 * CPUID register 4 general features/capability bits.  A model advertises
 * only what it implements, and the translator refuses the corresponding
 * instructions on models that do not.
 */
#define IA64_CPUID4_LB   (1ULL << 0)  /* brl, long branch */
#define IA64_CPUID4_SD   (1ULL << 1)  /* spontaneous deferral */
#define IA64_CPUID4_AO   (1ULL << 2)  /* ld16/st16/cmp8xchg16 atomics */
#define IA64_CPUID4_RU   (1ULL << 3)  /* resource utilization counter */
#define IA64_CPUID4_CZ   (1ULL << 32) /* clz */
#define IA64_CPUID4_X2   (1ULL << 33) /* mpy4/mpyshl4 */

/*
 * Direct-mapped lookup for modeled TR/TC entries.  IA-64's minimum page is
 * 4 KiB; hashing at that granularity lets separate softmmu pages covered by
 * one large architected translation retain independent hints.  Eight buckets
 * per maximum modeled TR/TC entry keep collision misses low without adding
 * another comparison to the hit path.
 */
#define IA64_MICRO_TLB_PAGE_SHIFT 12
#define IA64_MICRO_TLB_SIZE 1024
#define IA64_SUPPRESSED_TLB_MAX 4

#define IA64_REGION_BITS 3
#define IA64_IMPL_PA_BITS 50
/*
 * This CPU model exposes 64-bit virtual addressing: VA{60:0} plus the
 * three region bits VA{63:61}.
 */
#define IA64_IMPL_VA_MSB 60
#define IA64_IMPL_VA_BITS (IA64_IMPL_VA_MSB + 1 + IA64_REGION_BITS)
#define IA64_PAL_IMPL_VA_MSB IA64_IMPL_VA_MSB
#define IA64_IMPL_RID_BITS 24
#define IA64_IMPL_KEY_BITS 24

#define IA64_PAL_DISPATCH_HALTED  (1U << 0)
#define IA64_PAL_DISPATCH_EXIT_TB (1U << 1)
#define IA64_PAL_DISPATCH_RESUMED (1U << 2)

#define IA64_FR_ONE      0x3ff0000000000000ULL
/* Architected FP status expected by IA-64 firmware and OS hand-off. */
#define IA64_FPSR_DEFAULT 0x0009804c0270033fULL

#define IA64_FP_CONTEXT_SF_MASK       0x3u
#define IA64_FP_CONTEXT_PREC_SHIFT    2
#define IA64_FP_CONTEXT(sf, precision) \
    (((sf) & IA64_FP_CONTEXT_SF_MASK) | \
     ((precision) << IA64_FP_CONTEXT_PREC_SHIFT))

/* ---- PSR bit definitions ---- */
#define IA64_PSR_BE      (1ULL << 1)
#define IA64_PSR_UP      (1ULL << 2)
#define IA64_PSR_AC      (1ULL << 3)
#define IA64_PSR_MFL     (1ULL << 4)
#define IA64_PSR_MFH     (1ULL << 5)
#define IA64_PSR_IC      (1ULL << 13)
#define IA64_PSR_I       (1ULL << 14)
#define IA64_PSR_PK      (1ULL << 15)
#define IA64_PSR_UM_MASK 0x3fULL
#define IA64_PSR_UM_WRITABLE_MASK 0x3eULL
#define IA64_PSR_DT      (1ULL << 17)
#define IA64_PSR_DFL     (1ULL << 18)
#define IA64_PSR_DFH     (1ULL << 19)
#define IA64_PSR_SP      (1ULL << 20)
#define IA64_PSR_PP      (1ULL << 21)
#define IA64_PSR_DI      (1ULL << 22)
#define IA64_PSR_SI      (1ULL << 23)
#define IA64_PSR_DB      (1ULL << 24)
#define IA64_PSR_LP      (1ULL << 25)
#define IA64_PSR_TB      (1ULL << 26)
#define IA64_PSR_RT      (1ULL << 27)
#define IA64_PSR_IS      (1ULL << 34)
#define IA64_PSR_MC      (1ULL << 35)
#define IA64_PSR_IT      (1ULL << 36)
#define IA64_PSR_ID      (1ULL << 37)
#define IA64_PSR_CPL_MASK (3ULL << 32)
#define IA64_PSR_CPL_SHIFT 32
#define IA64_PSR_BN      (1ULL << 44)
#define IA64_PSR_VM      (1ULL << 46)
#define IA64_PSR_ED      (1ULL << 43)
#define IA64_PSR_DA      (1ULL << 38)
#define IA64_PSR_DD      (1ULL << 39)
#define IA64_PSR_SS      (1ULL << 40)
#define IA64_PSR_IA      (1ULL << 45)
#define IA64_PSR_RI_MASK (3ULL << 41)
#define IA64_PSR_RI_SHIFT  41
#define IA64_PSR_FAULT_SUPPRESS_MASK \
    (IA64_PSR_ID | IA64_PSR_DA | IA64_PSR_DD | IA64_PSR_ED | IA64_PSR_IA)

#define IA64_REGION_SHIFT 61
#define IA64_REGION_MASK  ((1ULL << IA64_REGION_BITS) - 1)
#define IA64_BUNDLE_SIZE  16ULL
#define IA64_IP_BUNDLE_MASK (~(IA64_BUNDLE_SIZE - 1))
#define IA64_REGION7_PHYS_MASK ((1ULL << IA64_REGION_SHIFT) - 1)
#define IA64_PHYS_UC_BIT (1ULL << 63)
#define IA64_FW_IDENTITY_BASE 0x00100000ULL
#define IA64_FW_IDENTITY_SIZE 0x00100000ULL
#define IA64_FIRMWARE_IVT_BASE 0x10000ULL
#define IA64_LOCAL_SAPIC_PA   0x00000000fee00000ULL
#define IA64_LOCAL_SAPIC_SIZE 0x00200000ULL
#define IA64_IO_PORT_SPACE_SIZE (64ULL << 20)

#define IA64_SAPIC_LID_ID_SHIFT   24
#define IA64_SAPIC_LID_EID_SHIFT  16
#define IA64_SAPIC_LID_ID_MASK    (0xffULL << IA64_SAPIC_LID_ID_SHIFT)
#define IA64_SAPIC_LID_EID_MASK   (0xffULL << IA64_SAPIC_LID_EID_SHIFT)

static inline uint64_t ia64_sapic_lid(uint8_t id, uint8_t eid)
{
    return ((uint64_t)id << IA64_SAPIC_LID_ID_SHIFT) |
           ((uint64_t)eid << IA64_SAPIC_LID_EID_SHIFT);
}

#define IA64_RR_RID_MASK   (0xffffffULL << 8)
#define IA64_RR_RID_SHIFT  8
#define IA64_RR_VE         (1ULL << 0)

static inline uint32_t ia64_rr_rid(uint64_t rr)
{
    return (rr & IA64_RR_RID_MASK) >> IA64_RR_RID_SHIFT;
}

static inline bool ia64_firmware_owns_iva(uint64_t iva)
{
    return iva == 0 || iva == IA64_FIRMWARE_IVT_BASE;
}

static inline bool ia64_firmware_identity_pa(uint64_t iva, uint64_t ip,
                                             uint64_t psr, uint64_t va,
                                             uint64_t *pa)
{
    bool firmware_context =
        (psr & IA64_PSR_CPL_MASK) == 0 &&
        (ia64_firmware_owns_iva(iva) ||
         (ip >= IA64_FW_IDENTITY_BASE &&
          ip < IA64_FW_IDENTITY_BASE + IA64_FW_IDENTITY_SIZE));

    if (firmware_context &&
        va >= IA64_FW_IDENTITY_BASE &&
        va < IA64_FW_IDENTITY_BASE + IA64_FW_IDENTITY_SIZE) {
        *pa = va;
        return true;
    }

    return false;
}

static inline uint64_t ia64_physical_address(uint64_t addr)
{
    return addr & ~IA64_PHYS_UC_BIT;
}

static inline uint64_t ia64_ip_bundle_addr(uint64_t ip)
{
    return ip & IA64_IP_BUNDLE_MASK;
}

static inline uint8_t ia64_psr_cpl(uint64_t psr)
{
    return (psr & IA64_PSR_CPL_MASK) >> IA64_PSR_CPL_SHIFT;
}

#define IA64_RI_0        (0ULL << 41)
#define IA64_RI_1        (1ULL << 41)
#define IA64_RI_2        (2ULL << 41)

/* ---- RSC bit definitions ---- */
#define IA64_RSC_MODE    0x3ULL
#define IA64_RSC_PL      0xcULL
#define IA64_RSC_PL_SHIFT 2
#define IA64_RSC_BE      0x10ULL
#define IA64_RSC_LOADRS_SHIFT 16
#define IA64_RSC_LOADRS_MASK  0x3fffULL

static inline uint8_t ia64_rsc_pl(uint64_t rsc)
{
    return (rsc & IA64_RSC_PL) >> IA64_RSC_PL_SHIFT;
}

/* ---- CFM mask definitions ---- */
#define IA64_CFM_SOF_MASK     0x7f
#define IA64_CFM_SOL_MASK     (0x7f << 7)
#define IA64_CFM_SOL_SHIFT    7
#define IA64_CFM_SOR_MASK     (0x0f << 14)
#define IA64_CFM_SOR_SHIFT    14
#define IA64_CFM_RRB_GR_MASK  (0x7f << 18)
#define IA64_CFM_RRB_GR_SHIFT 18
#define IA64_CFM_RRB_FR_MASK  (0x7fULL << 25)
#define IA64_CFM_RRB_FR_SHIFT 25
#define IA64_CFM_RRB_PR_MASK  (0x3fULL << 32)
#define IA64_CFM_RRB_PR_SHIFT 32
#define IA64_IFS_V            (1ULL << 63)
#define IA64_IFS_IFM_MASK     (IA64_CFM_SOF_MASK | IA64_CFM_SOL_MASK | \
                               IA64_CFM_SOR_MASK | IA64_CFM_RRB_GR_MASK | \
                               IA64_CFM_RRB_FR_MASK | IA64_CFM_RRB_PR_MASK)

/*
 * CFM.sor is encoded in groups of eight general registers.  These are the
 * frame-size constraints shared by alloc, AR.PFS and CR.IFS validation.
 */
static inline bool ia64_cfm_frame_fields_valid(uint32_t sof, uint32_t sol,
                                               uint32_t sor)
{
    return sof <= IA64_STACKED_GR_COUNT && sol <= sof &&
           (sor << 3) <= sof;
}

/* ---- PFS field definitions ---- */
#define IA64_PFS_PFM_MASK     ((1ULL << 38) - 1)
#define IA64_PFS_PEC_SHIFT    52
#define IA64_PFS_PEC_MASK     (0x3fULL << IA64_PFS_PEC_SHIFT)
#define IA64_PFS_PPL_SHIFT    62
#define IA64_PFS_PPL_MASK     (0x3ULL << IA64_PFS_PPL_SHIFT)

/* ---- ISR fields ---- */
#define IA64_ISR_CODE_MASK   0xffff
#define IA64_ISR_VECTOR_MASK (0xffUL << 16)
#define IA64_ISR_VECTOR_SHIFT 16
#define IA64_ISR_X           (1ULL << 32)
#define IA64_ISR_W           (1ULL << 33)
#define IA64_ISR_R           (1ULL << 34)
#define IA64_ISR_NA          (1ULL << 35)
#define IA64_ISR_SP          (1ULL << 36)
#define IA64_ISR_RS          (1ULL << 37)
#define IA64_ISR_IR          (1ULL << 38)
#define IA64_ISR_NI          (1ULL << 39)
#define IA64_ISR_EI_MASK     (3ULL << 41)
#define IA64_ISR_EI_SHIFT    41
#define IA64_ISR_ED          (1ULL << 43)
#define IA64_ISR_CODE_REG_NAT  0x10
/* NaT Consumption ISR.code{5:4} = 2 for a NaTPage reference. */
#define IA64_ISR_CODE_NAT_PAGE 0x20
/* Concurrent trap conditions reported in ISR.code (SDM Vol. 2, Table 8-3). */
#define IA64_ISR_CODE_FP       (1ULL << 0)
#define IA64_ISR_CODE_LP       (1ULL << 1)
#define IA64_ISR_CODE_TB       (1ULL << 2)
#define IA64_ISR_CODE_SS       (1ULL << 3)
#define IA64_ISR_CODE_UI       (1ULL << 4)

/* Conditions supplied to the common successful-instruction trap check. */
#define IA64_NATIVE_TRAP_TAKEN  (1U << 0)
#define IA64_NATIVE_TRAP_LOWER  (1U << 1)
#define IA64_NATIVE_TRAP_FP     (1U << 2)
#define IA64_NATIVE_TRAP_SLOTS(target, source) \
    (((target) & 3U) | (((source) & 3U) << 2))

/* ---- ITIR fields ---- */
#define IA64_ITIR_PS_MASK    0x3f
#define IA64_ITIR_PS_SHIFT   2
#define IA64_ITIR_KEY_MASK   (0xffffffULL << 8)
#define IA64_ITIR_KEY_SHIFT  8
#define IA64_INSERTABLE_PAGE_SIZE_MASK \
    ((1ULL << 12) | (1ULL << 13) | (1ULL << 14) | (1ULL << 16) | \
     (1ULL << 18) | (1ULL << 20) | (1ULL << 22) | (1ULL << 24) | \
     (1ULL << 26) | (1ULL << 28) | (1ULL << 30) | (1ULL << 32))
#define IA64_PURGEABLE_PAGE_SIZE_MASK IA64_INSERTABLE_PAGE_SIZE_MASK

/* ---- General exception codes ---- */
/* ISR.code{7:4}; code{3:0} remains available to non-access instructions. */
#define IA64_GENEX_UNIMPL_DATA_ADDR (3ULL << 4)
/* A fetch-side Unimplemented Instruction Address fault has zero trap code. */
#define IA64_GENEX_UNIMPL_INST_ADDR 0

/* ---- Protection Key Register fields ---- */
#define IA64_PKR_VALID       (1ULL << 0)
#define IA64_PKR_WD          (1ULL << 1)
#define IA64_PKR_RD          (1ULL << 2)
#define IA64_PKR_XD          (1ULL << 3)
#define IA64_PKR_KEY_SHIFT   8
#define IA64_PKR_KEY_MASK \
    (((1ULL << IA64_IMPL_KEY_BITS) - 1) << IA64_PKR_KEY_SHIFT)
#define IA64_PKR_MASK        (IA64_PKR_VALID | IA64_PKR_WD | IA64_PKR_RD | \
                              IA64_PKR_XD | IA64_PKR_KEY_MASK)

/* ---- PTE fields ---- */
#define IA64_PTE_PRESENT  (1ULL << 0)
#define IA64_PTE_ACCESSED (1ULL << 5)
#define IA64_PTE_DIRTY    (1ULL << 6)
#define IA64_PTE_ED       (1ULL << 52)
#define IA64_PTE_MA_SHIFT 2
#define IA64_PTE_MA_MASK  (7ULL << IA64_PTE_MA_SHIFT)
#define IA64_PTE_MA_WB      0
#define IA64_PTE_MA_UC      4
#define IA64_PTE_MA_UCE     5
#define IA64_PTE_MA_WC      6
#define IA64_PTE_MA_NATPAGE 7

static inline uint8_t ia64_pte_ma(uint64_t pte)
{
    return (pte & IA64_PTE_MA_MASK) >> IA64_PTE_MA_SHIFT;
}

/* ---- PTA fields ---- */
#define IA64_PTA_VE          (1ULL << 0)
#define IA64_PTA_SIZE_MASK   (0x3fULL << 2)
#define IA64_PTA_SIZE_SHIFT  2
#define IA64_PTA_VF          (1ULL << 8)
#define IA64_PTA_BASE_MASK   (~0x7fffULL)
#define IA64_PTA_BASE_SHIFT  15

/* ---- DCR fields ---- */
#define IA64_DCR_PP          (1ULL << 0)
#define IA64_DCR_BE          (1ULL << 1)
#define IA64_DCR_LC          (1ULL << 2)
#define IA64_DCR_DM          (1ULL << 8)
#define IA64_DCR_DP          (1ULL << 9)
#define IA64_DCR_DK          (1ULL << 10)
#define IA64_DCR_DX          (1ULL << 11)
#define IA64_DCR_DR          (1ULL << 12)
#define IA64_DCR_DA          (1ULL << 13)
#define IA64_DCR_DD          (1ULL << 14)

/*
 * Architecturally and software-convention-defined register indices.
 * See IA-64 SDM volume 2, section 11.8.2.5, and the FPSWA interface.
 */
typedef enum IA64GeneralRegisterIndex {
    IA64_GR_ZERO = 0,
    IA64_GR_GLOBAL_POINTER = 1,
    IA64_GR_RETURN0 = 8,
    IA64_GR_RETURN1 = 9,
    IA64_GR_RETURN2 = 10,
    IA64_GR_RETURN3 = 11,
    IA64_GR_STACK_POINTER = 12,
    IA64_GR_THREAD_POINTER = 13,
} IA64GeneralRegisterIndex;

typedef enum IA64IA32GeneralRegisterIndex {
    IA64_IA32_GR_RETURN_POINTER = IA64_GR_GLOBAL_POINTER,
    IA64_IA32_GR_INTEGER_BASE = IA64_GR_RETURN0,
    IA64_IA32_GR_DATA_SELECTORS = 16,
    IA64_IA32_GR_SYSTEM_SELECTORS = 17,
    IA64_IA32_GR_ES_DESCRIPTOR = 24,
    IA64_IA32_GR_CS_DESCRIPTOR_SCRATCH = 25,
    IA64_IA32_GR_SS_DESCRIPTOR_SCRATCH = 26,
    IA64_IA32_GR_DS_DESCRIPTOR = 27,
    IA64_IA32_GR_FS_DESCRIPTOR = 28,
    IA64_IA32_GR_GS_DESCRIPTOR = 29,
    IA64_IA32_GR_LDT_DESCRIPTOR = 30,
    IA64_IA32_GR_GDT_DESCRIPTOR = 31,
} IA64IA32GeneralRegisterIndex;

typedef enum IA64PredicateRegisterIndex {
    IA64_PR_TRUE = 0,
    IA64_PR_ROTATING_BASE = 16,
    IA64_PR_ROTATING_NEXT = 17,
    IA64_PR_LAST = 63,
} IA64PredicateRegisterIndex;

typedef enum IA64BranchRegisterIndex {
    IA64_BR_RETURN_LINK = 0,
    IA64_BR_STATIC0 = 1,
} IA64BranchRegisterIndex;

typedef enum IA64RasGeneralRegisterIndex {
    IA64_RAS_GR_SAL_MIN_STATE = 16,
    IA64_RAS_GR_PAL_MIN_STATE = 17,
    IA64_RAS_GR_PROCESSOR_STATE = 18,
    IA64_RAS_GR_PALE_RETURN = 19,
    IA64_RAS_GR_SALE_ENTRY_STATE = 20,
    IA64_RAS_GR_PMI_VECTOR = 24,
    IA64_RAS_GR_PMI_MIN_STATE = 25,
    IA64_RAS_GR_PMI_RSC = 26,
    IA64_RAS_GR_PMI_B0 = 27,
    IA64_RAS_GR_PMI_B1 = 28,
    IA64_RAS_GR_PMI_PREDICATES = 29,
} IA64RasGeneralRegisterIndex;

typedef enum IA64FloatingRegisterIndex {
    IA64_FR_ZERO_INDEX = 0,
    IA64_FR_ONE_INDEX = 1,
    IA64_FR_ROTATING_BASE = 32,
} IA64FloatingRegisterIndex;

typedef enum IA64RegionRegisterIndex {
    IA64_RR_REGION0 = 0,
    IA64_RR_REGION1 = 1,
    IA64_RR_REGION2 = 2,
    IA64_RR_REGION3 = 3,
    IA64_RR_REGION4 = 4,
    IA64_RR_REGION5 = 5,
    IA64_RR_REGION6 = 6,
    IA64_RR_REGION7 = 7,
} IA64RegionRegisterIndex;

typedef enum IA64ProtectionKeyRegisterIndex {
    IA64_PKR_0 = 0,
    IA64_PKR_1 = 1,
    IA64_PKR_2 = 2,
    IA64_PKR_3 = 3,
} IA64ProtectionKeyRegisterIndex;

typedef enum IA64PalGeneralRegisterIndex {
    IA64_PAL_GR_STATUS = IA64_GR_RETURN0,
    IA64_PAL_GR_RESULT1 = IA64_GR_RETURN1,
    IA64_PAL_GR_RESULT2 = IA64_GR_RETURN2,
    IA64_PAL_GR_RESULT3 = IA64_GR_RETURN3,
    IA64_PAL_GR_INDEX = 28,
    IA64_PAL_GR_ARG1 = 29,
    IA64_PAL_GR_ARG2 = 30,
    IA64_PAL_GR_ARG3 = 31,
} IA64PalGeneralRegisterIndex;

typedef enum IA64FirmwareDebugRegisterIndex {
    IA64_FW_DEBUG_GR_HANDLER = 16,
    IA64_FW_DEBUG_GR_EXCEPTION = 16,
    IA64_FW_DEBUG_GR_CONTEXT = 17,
    IA64_FW_DEBUG_GR_CPU = 18,
} IA64FirmwareDebugRegisterIndex;

typedef enum IA64FpswaGeneralRegisterIndex {
    IA64_FPSWA_GR_TRAP_TYPE = 32,
    IA64_FPSWA_GR_BUNDLE = 33,
    IA64_FPSWA_GR_IPSR_PTR = 34,
    IA64_FPSWA_GR_FPSR_PTR = 35,
    IA64_FPSWA_GR_ISR_PTR = 36,
    IA64_FPSWA_GR_PREDS_PTR = 37,
    IA64_FPSWA_GR_IFS_PTR = 38,
    IA64_FPSWA_GR_STATE = 39,
} IA64FpswaGeneralRegisterIndex;

typedef enum IA64ControlRegisterIndex {
    IA64_CR_DCR = 0,
    IA64_CR_ITM = 1,
    IA64_CR_IVA = 2,
    IA64_CR_PTA = 8,
    IA64_CR_IPSR = 16,
    IA64_CR_ISR = 17,
    IA64_CR_IIP = 19,
    IA64_CR_IFA = 20,
    IA64_CR_ITIR = 21,
    IA64_CR_IIPA = 22,
    IA64_CR_IFS = 23,
    IA64_CR_IIM = 24,
    IA64_CR_IHA = 25,
    IA64_CR_SAPIC_LID = 64,
    IA64_CR_SAPIC_IVR = 65,
    IA64_CR_SAPIC_TPR = 66,
    IA64_CR_SAPIC_EOI = 67,
    IA64_CR_SAPIC_IRR0 = 68,
    IA64_CR_SAPIC_IRR1 = 69,
    IA64_CR_SAPIC_IRR2 = 70,
    IA64_CR_SAPIC_IRR3 = 71,
    IA64_CR_ITV = 72,
    IA64_CR_PMV = 73,
    IA64_CR_CMCV = 74,
    IA64_CR_LRR0 = 80,
    IA64_CR_LRR1 = 81,
} IA64ControlRegisterIndex;

typedef enum IA64ApplicationRegisterIndex {
    IA64_AR_KR0 = 0,
    IA64_AR_KR1 = 1,
    IA64_AR_KR2 = 2,
    IA64_AR_KR3 = 3,
    IA64_AR_KR7 = 7,
    IA64_AR_RSC = 16,
    IA64_AR_BSP = 17,
    IA64_AR_BSPSTORE = 18,
    IA64_AR_RNAT = 19,
    IA64_AR_FCR = 21,
    IA64_AR_EFLAG = 24,
    IA64_AR_CSD = 25,
    IA64_AR_SSD = 26,
    IA64_AR_CFLG = 27,
    IA64_AR_FSR = 28,
    IA64_AR_FIR = 29,
    IA64_AR_FDR = 30,
    IA64_AR_CCV = 32,
    IA64_AR_UNAT = 36,
    IA64_AR_FPSR = 40,
    IA64_AR_ITC = 44,
    IA64_AR_RUC = 45,
    IA64_AR_PFS = 64,
    IA64_AR_LC = 65,
    IA64_AR_EC = 66,
} IA64ApplicationRegisterIndex;

typedef enum IA64IA32ApplicationRegisterIndex {
    IA64_IA32_AR_IOBASE = IA64_AR_KR0,
    IA64_IA32_AR_TSS_DESCRIPTOR = IA64_AR_KR1,
    IA64_IA32_AR_CR3_CR2 = IA64_AR_KR2,
} IA64IA32ApplicationRegisterIndex;

#define IA64_SPURIOUS_VECTOR      0x0F
#define IA64_VECTOR_MASKED        (1ULL << 16)
#define IA64_TPR_MIC_MASK         0x00000000000000f0ULL
#define IA64_TPR_MMI              (1ULL << 16)
#define IA64_TPR_WRITABLE_MASK    (IA64_TPR_MIC_MASK | IA64_TPR_MMI)

/* ---- TLB permissions ---- */
#define IA64_TLB_R           1
#define IA64_TLB_W           2
#define IA64_TLB_X           4
#define IA64_TLB_ALL         7

static inline uint8_t ia64_tlb_effective_perm(uint8_t ar, uint8_t pl,
                                              uint8_t access_level)
{
    if (access_level > 3 || pl > 3) {
        return 0;
    }

    switch (ar & 7) {
    case 0:
        return access_level <= pl ? IA64_TLB_R : 0;
    case 1:
        return access_level <= pl ? (IA64_TLB_R | IA64_TLB_X) : 0;
    case 2:
        return access_level <= pl ? (IA64_TLB_R | IA64_TLB_W) : 0;
    case 3:
        return access_level <= pl ? IA64_TLB_ALL : 0;
    case 4:
        if (access_level > pl) {
            return 0;
        }
        return access_level < pl ? (IA64_TLB_R | IA64_TLB_W) : IA64_TLB_R;
    case 5:
        if (access_level > pl) {
            return 0;
        }
        return access_level < pl ? IA64_TLB_ALL : (IA64_TLB_R | IA64_TLB_X);
    case 6:
        if (access_level > pl) {
            return 0;
        }
        return access_level < pl ? IA64_TLB_ALL : (IA64_TLB_R | IA64_TLB_W);
    case 7:
        return access_level == 0 ? (IA64_TLB_R | IA64_TLB_X) : IA64_TLB_X;
    default:
        return 0;
    }
}

/* ---- ALAT (Advanced Load Address Table) ---- */
#define IA64_ALAT_ENTRIES    32

typedef struct IA64AlatEntry {
    uint64_t phys_addr;
    uint8_t  size;
    uint8_t  reg;
    bool     fp;
    bool     valid;
} IA64AlatEntry;

/* ---- Exception type enum (internal IDs, not IVT vectors) ---- */
typedef enum IA64Exception {
    IA64_EXCP_NONE = 0,
    IA64_EXCP_BREAK = 1,
    IA64_EXCP_ILLEGAL = 2,
    IA64_EXCP_RESERVED_TEMPLATE = 3,
    IA64_EXCP_VHPT_FAULT = 4,
    IA64_EXCP_ITLB_FAULT = 5,
    IA64_EXCP_DTLB_FAULT = 6,
    IA64_EXCP_ALT_ITLB = 7,
    IA64_EXCP_ALT_DTLB = 8,
    IA64_EXCP_DATA_NESTED_TLB = 9,
    IA64_EXCP_DATA_ACCESS = 10,
    IA64_EXCP_GENERAL = 11,
    IA64_EXCP_NAT_CONSUMPTION = 12,
    IA64_EXCP_EXTINT = 13,
    IA64_EXCP_UNALIGNED = 14,
    IA64_EXCP_PAGE_NOT_PRESENT = 15,
    IA64_EXCP_INST_ACCESS = 16,
    IA64_EXCP_DATA_DIRTY = 17,
    IA64_EXCP_INST_ACCESS_BIT = 18,
    IA64_EXCP_DATA_ACCESS_BIT = 19,
    IA64_EXCP_INST_KEY_MISS = 20,
    IA64_EXCP_DATA_KEY_MISS = 21,
    IA64_EXCP_KEY_PERMISSION = 22,
    IA64_EXCP_UNIMPL_DATA_ADDR = 23,
    IA64_EXCP_UNIMPL_INST_ADDR = 24,
    IA64_EXCP_PRIVILEGED_OP = 25,
    IA64_EXCP_PRIVILEGED_REG = 26,
    IA64_EXCP_RESERVED_REG_FIELD = 27,
    IA64_EXCP_FP_FAULT = 28,
    IA64_EXCP_FP_TRAP = 29,
    IA64_EXCP_DISABLED_ISA_TRANSITION = 30,
    IA64_EXCP_DISABLED_FP = 31,
    IA64_EXCP_UNSUPPORTED_DATA_REFERENCE = 32,
    IA64_EXCP_VIRTUALIZATION = 33,
    IA64_EXCP_IA32_EXCEPTION = 34,
    IA64_EXCP_IA32_INTERCEPT = 35,
    IA64_EXCP_IA32_INTERRUPT = 36,
    IA64_EXCP_TAKEN_BRANCH = 37,
    IA64_EXCP_SINGLE_STEP = 38,
    IA64_EXCP_DEBUG = 39,
    IA64_EXCP_LOWER_PRIVILEGE = 40,
    IA64_EXCP_MAX,
} IA64Exception;

/* ---- IVT vector mapping table ---- */
extern const uint16_t ia64_ivt_vectors[IA64_EXCP_MAX];

static inline IA64Exception
ia64_pte_exception_for_access(uint64_t pte, uint8_t perm, uint8_t needed,
                              bool is_ifetch, bool is_write, uint64_t psr)
{
    if (!(pte & IA64_PTE_PRESENT)) {
        return IA64_EXCP_PAGE_NOT_PRESENT;
    }

    if ((perm & needed) != needed) {
        return is_ifetch ? IA64_EXCP_INST_ACCESS : IA64_EXCP_DATA_ACCESS;
    }

    /*
     * The Data Dirty Bit fault outranks the Data Access Bit fault, so a
     * store to a page with both bits clear reports the dirty-bit vector.
     */
    if (is_ifetch) {
        if (!(pte & IA64_PTE_ACCESSED) && !(psr & IA64_PSR_IA)) {
            return IA64_EXCP_INST_ACCESS_BIT;
        }
    } else {
        if (is_write && !(pte & IA64_PTE_DIRTY) &&
            !(psr & IA64_PSR_DA)) {
            return IA64_EXCP_DATA_DIRTY;
        }
        if (!(pte & IA64_PTE_ACCESSED) && !(psr & IA64_PSR_DA)) {
            return IA64_EXCP_DATA_ACCESS_BIT;
        }
    }

    return IA64_EXCP_NONE;
}

/* ---- TLB entry ---- */
typedef struct IA64TlbEntry {
    uint64_t va;
    uint64_t pa;
    uint64_t ps;
    uint64_t page_mask;
    uint64_t pte;
    uint8_t  perm;
    uint8_t  ar;
    uint8_t  pl;
    uint8_t  valid;
    uint8_t  is_tr;
    uint8_t  pending_purge;
    uint32_t rid;
    uint32_t key;
    uint16_t slot;
    /*
     * Derived version for micro-TLB validation.  This occupies existing
     * tail padding and is deliberately omitted from migration state.
     */
    uint32_t micro_generation;
} IA64TlbEntry;

typedef struct IA64MicroTlbEntry {
    uint64_t va;
    uint64_t page_mask;
    /* PTE used by the last successful lookup, including the code-page ED. */
    uint64_t pte;
    uint32_t rid;
    uint32_t generation;
    uint32_t slot_generation;
    uint16_t slot;
    bool valid;
} IA64MicroTlbEntry;

typedef struct IA64CodeTlbEdCache {
    uint64_t va;
    uint64_t page_mask;
    uint32_t rid;
    uint32_t generation;
    uint32_t slot_generation;
    uint16_t slot;
    bool ed;
    bool valid;
} IA64CodeTlbEdCache;

typedef struct IA64DTlb1Lookup {
    uint64_t page;
    uint32_t rid;
    uint8_t slot;
    bool valid;
} IA64DTlb1Lookup;

typedef struct IA64RnatShadowEntry {
    uint64_t value;
    uint64_t addr;
    uint64_t defined;
    bool valid;
} IA64RnatShadowEntry;

/*
 * Non-architectural image used only when a completed RNAT collection is
 * written back.  In particular, this is not a source for mov-from-RNAT or
 * mandatory fills.
 */
typedef struct IA64RnatWritebackImage {
    uint64_t value;
    uint64_t addr;
    uint64_t defined;
    bool valid;
} IA64RnatWritebackImage;

/*
 * RSE register and bookkeeping snapshot for firmware debug callbacks and
 * MCA/INIT handlers.
 */
typedef struct IA64RSEContextState {
    uint64_t pgr[IA64_STACKED_GR_COUNT];
    uint64_t pgr_nat[2];
    uint64_t gr_dirty[2];
    uint64_t bsp;
    uint64_t bspstore;
    uint64_t rnat;
    uint32_t bol;
    int32_t dirty;
    int32_t dirty_nat;
    int32_t clean;
    int32_t clean_nat;
    int32_t invalid;
    uint64_t rnat_addr;
    uint64_t rnat_defined;
    uint64_t load_rnat;
    uint64_t load_rnat_addr;
    uint64_t load_rnat_defined;
    bool load_rnat_valid;
    IA64RnatWritebackImage writeback_rnat;
    IA64RnatShadowEntry rnat_shadow[IA64_RSE_RNAT_SHADOW_COUNT];
    uint8_t rnat_shadow_count;
    uint8_t cfm_sof;
    uint8_t cfm_sol;
    uint8_t cfm_sor;
    uint8_t cfm_rrb_gr;
    uint8_t cfm_rrb_fr;
    uint8_t cfm_rrb_pr;
    bool cfle;
    bool completion_pending;
    bool completion_demoted;
    uint64_t completion_psr;
    uint64_t completion_source_ip;
    uint8_t completion_source_slot;
} IA64RSEContextState;

typedef IA64RSEContextState IA64FirmwareDebugRseState;

typedef enum IA64MemorySpeculation {
    IA64_MEM_NON_SPECULATIVE,
    IA64_MEM_LIMITED_SPECULATION,
    IA64_MEM_SPECULATIVE,
} IA64MemorySpeculation;

/*
 * Alignment policy carried by translated memory instructions.  The datum
 * size is deliberately separate from its natural alignment: an extended
 * floating-point operand transfers 10 bytes but has a 16-byte natural
 * boundary.  The compact descriptor keeps the TCG helper ABI small enough
 * to use for both ordinary and speculative references.
 */
typedef enum IA64AlignmentClass {
    IA64_ALIGNMENT_GENERIC,
    IA64_ALIGNMENT_INTEGER,
    IA64_ALIGNMENT_FP,
    IA64_ALIGNMENT_FP_PAIR,
    IA64_ALIGNMENT_FP_FILL_SPILL,
    IA64_ALIGNMENT_NATURAL_REQUIRED,
} IA64AlignmentClass;

#define IA64_ALIGNMENT_DATUM_MASK       0xffU
#define IA64_ALIGNMENT_NATURAL_SHIFT    8
#define IA64_ALIGNMENT_NATURAL_MASK     (0xffU << IA64_ALIGNMENT_NATURAL_SHIFT)
#define IA64_ALIGNMENT_CLASS_SHIFT      16
#define IA64_ALIGNMENT_CLASS_MASK       (0xffU << IA64_ALIGNMENT_CLASS_SHIFT)
#define IA64_ALIGNMENT_INFO(datum, natural, cls)                         \
    (((uint32_t)(datum) & IA64_ALIGNMENT_DATUM_MASK) |                   \
     (((uint32_t)(natural) & 0xffU) << IA64_ALIGNMENT_NATURAL_SHIFT) |   \
     (((uint32_t)(cls) & 0xffU) << IA64_ALIGNMENT_CLASS_SHIFT))

static inline IA64MemorySpeculation
ia64_pte_memory_speculation(uint64_t pte)
{
    switch ((pte >> 2) & 7) {
    case 0: /* WB */
    case 6: /* WC */
    case 7: /* NaTPage */
        return IA64_MEM_SPECULATIVE;
    case 4: /* UC */
    case 5: /* UCE */
    default:
        return IA64_MEM_NON_SPECULATIVE;
    }
}

static inline bool ia64_tlb_entry_present(const IA64TlbEntry *entry)
{
    return entry->pte & IA64_PTE_PRESENT;
}

static inline void ia64_tlb_entry_translate(const IA64TlbEntry *entry,
                                            uint64_t va,
                                            uint8_t access_level,
                                            uint64_t *pa, uint8_t *perm)
{
    uint64_t page_offset = va & (entry->ps - 1);

    *pa = (entry->pa & ~(entry->ps - 1)) | page_offset;
    *perm = ia64_tlb_entry_present(entry) ?
            ia64_tlb_effective_perm(entry->ar, entry->pl, access_level) : 0;
}

#include "internals.h"
#include "ia32/compat.h"

/* ---- CPU architectural state ---- */
typedef struct CPUArchState {
    /*
     * Keep the private IA-32 backing state at offset zero.  The x86 TCG
     * translator addresses CPUX86State fields relative to tcg_env; placing
     * this view first lets those offsets be reused without a second CPU
     * object or a fork of the x86 decoder.
     */
    CPUX86State ia32;

    /* General / Predicate / Branch registers */
    uint64_t gr[IA64_GR_COUNT];
    uint64_t pr[IA64_PR_COUNT];
    uint64_t br[IA64_BR_COUNT];
    uint64_t ip;
    uint64_t last_successful_bundle;
    uint64_t psr;

    /* NaT bits for GRs (2x64 bits, little-endian bit numbering) */
    uint64_t nat[2];

    /* Inactive bank for GR16-GR31 selected by PSR.bn/bsw. */
    uint64_t banked_gr[16];
    uint16_t banked_nat;

    IA64ExceptionState exception_state;

    /* Control Registers */
    uint64_t cr[IA64_CR_COUNT];
#define cr_dcr    cr[IA64_CR_DCR]
#define cr_itm    cr[IA64_CR_ITM]
#define cr_iva    cr[IA64_CR_IVA]
#define cr_pta    cr[IA64_CR_PTA]
#define cr_ipsr   cr[IA64_CR_IPSR]
#define cr_isr    cr[IA64_CR_ISR]
#define cr_iip    cr[IA64_CR_IIP]
#define cr_ifa    cr[IA64_CR_IFA]
#define cr_itir   cr[IA64_CR_ITIR]
#define cr_iipa   cr[IA64_CR_IIPA]
#define cr_ifs    cr[IA64_CR_IFS]
#define cr_iim    cr[IA64_CR_IIM]
#define cr_iha    cr[IA64_CR_IHA]

    /* Model-specific registers */
    uint64_t msr[IA64_MSR_COUNT];
    uint64_t pmc[IA64_PMC_COUNT];
    uint64_t pmd[IA64_PMD_COUNT];
    uint64_t pkr[IA64_PKR_COUNT];

    /* Debug and instruction break registers */
    uint64_t dbr[IA64_DBR_COUNT];
    uint64_t ibr[IA64_IBR_COUNT];
    uint64_t dahr[8];
    uint8_t ia32_data_breakpoints;

    /* Rollback state for precise IA-32 Streaming SIMD exceptions. */
    bool ia32_sse_instruction_active;
    uint16_t ia32_sse_old_flags;
    target_ulong ia32_sse_old_cc_dst;
    target_ulong ia32_sse_old_cc_src;
    target_ulong ia32_sse_old_cc_src2;
    uint32_t ia32_sse_old_cc_op;
    FPReg ia32_sse_old_fpregs[8];
    uint64_t ia32_sse_old_xmm[8][2];

    /* Region Registers */
    uint64_t rr[IA64_RR_COUNT];

    /* Application Registers */
    uint64_t ar[IA64_AR_COUNT];
#define ar_kr0    ar[IA64_AR_KR0]
#define ar_kr7    ar[IA64_AR_KR7]
#define ar_rsc    ar[IA64_AR_RSC]
#define ar_bsp    ar[IA64_AR_BSP]
#define ar_bspstore ar[IA64_AR_BSPSTORE]
#define ar_rnat   ar[IA64_AR_RNAT]
#define ar_fcr    ar[IA64_AR_FCR]
#define ar_eflag  ar[IA64_AR_EFLAG]
#define ar_csd    ar[IA64_AR_CSD]
#define ar_ssd    ar[IA64_AR_SSD]
#define ar_cflg   ar[IA64_AR_CFLG]
#define ar_fsr    ar[IA64_AR_FSR]
#define ar_fir    ar[IA64_AR_FIR]
#define ar_fdr    ar[IA64_AR_FDR]
#define ar_ccv    ar[IA64_AR_CCV]
#define ar_unat   ar[IA64_AR_UNAT]
#define ar_fpsr   ar[IA64_AR_FPSR]
#define ar_itc    ar[IA64_AR_ITC]
#define ar_pfs    ar[IA64_AR_PFS]
#define ar_lc     ar[IA64_AR_LC]
#define ar_ec     ar[IA64_AR_EC]
    /* Current Frame Marker (derived from pfs bits) */
    uint8_t cfm_sof;
    uint8_t cfm_sol;
    uint8_t cfm_sor;
    uint8_t cfm_rrb_gr;
    uint8_t cfm_rrb_fr;
    uint8_t cfm_rrb_pr;

    IA64MMUState mmu;
    IA64InterruptState interrupt;
    IA64PalState pal;

    /*
     * Register Stack Engine state (SDM Vol.2 ch.6).  The register
     * stack backing store in guest memory is the authoritative home of
     * stacked register values; the emulator keeps the architecture's
     * physical stacked register file and its four partitions
     * explicitly.  rse_pgr[]/rse_pgr_nat[] form the circular physical
     * file and rse_bol is RSE.BOF, the physical index of the current
     * frame's GR32.  env->gr[32..127] only cache the virtual
     * (renamed/rotated) view of the current frame and are
     * re-synchronized with the physical file whenever the frame
     * mapping changes.
     *
     * Partition counters follow SDM Vol.2 6.3 (all counts in
     * registers; the intervening NaT collection words of a partition
     * are counted separately by the *_nat fields):
     *   cfm_sof + rse_dirty + rse_clean + rse_invalid == 96
     *   AR.BSPSTORE == AR.BSP - 8*(rse_dirty + rse_dirty_nat)
     * rse_dirty/rse_dirty_nat go negative while the current frame is
     * incomplete (mandatory RSE loads pending after br.ret/rfi,
     * SDM Vol.2 6.8).  rse_cfle is RSE.CFLE (SDM Vol.2 table 6-1): it
     * is set while a br.ret/rfi mandatory load sequence is in
     * progress and cleared by interruption delivery or by completing
     * the sequence.
     */
    IA64RSEState rse;

    bool instruction_group_start;  /* next instruction starts a new group */

    IA64AlatState alat_state;

    /* Performance counter */
    uint64_t bundles_retired;

    IA64FPState fp;

    /*
     * Non-architectural bridge for 64-bit packed TCG vector results.  It is
     * always overwritten before use and is intentionally not migrated.
     */
    uint64_t tcg_vec_scratch;

} CPUIA64State;

static inline uint64_t ia64_pkr_key_mask(const CPUIA64State *env);

void ia64_tlb_bump_generation(CPUIA64State *env, bool is_ifetch);
void ia64_tlb_bump_slot_generation(CPUIA64State *env, bool is_ifetch,
                                   uint16_t slot);
const IA64TlbEntry *ia64_tlb_find_slow(CPUIA64State *env, uint64_t va,
                                       uint32_t rid, bool is_ifetch);

static inline void ia64_rse_mark_gr_dirty(CPUIA64State *env, uint32_t reg)
{
    if (reg >= IA64_STACKED_GR_BASE && reg < IA64_GR_COUNT) {
        uint32_t bit = reg - IA64_STACKED_GR_BASE;

        env->rse.rse_gr_dirty[bit / 64] |= 1ULL << (bit % 64);
    }
}

void ia64_set_cfm_rrb_fr(CPUIA64State *env, uint32_t new_rrb);
void ia64_sync_rotating_fr(CPUIA64State *env);
void ia64_set_cfm_rrb_pr(CPUIA64State *env, uint32_t new_rrb);
void ia64_rotate_cfm_rrb_pr_right(CPUIA64State *env);
void ia64_flush_suppressed_tlb(CPUIA64State *env);
void ia64_firmware_debug_capture(CPUIA64State *env, uint16_t vector,
                                 bool collected);

static inline bool ia64_key_check_enabled(const CPUIA64State *env,
                                          bool is_ifetch, bool is_rse)
{
    uint64_t translation_bit;

    if (!(env->psr & IA64_PSR_PK)) {
        return false;
    }

    translation_bit = is_ifetch ? IA64_PSR_IT :
                      (is_rse ? IA64_PSR_RT : IA64_PSR_DT);
    return env->psr & translation_bit;
}

/*
 * Raw protection-key lookup, gated only by the caller.  Used directly by
 * probe, whose key checks depend on PSR.pk alone: the probe query is a
 * virtual DTLB lookup even while PSR.dt is 0.
 */
static inline IA64Exception
ia64_key_exception_for_key(const CPUIA64State *env, uint32_t key,
                           uint8_t needed, bool is_ifetch)
{
    const uint64_t pkr_key = (uint64_t)key << IA64_PKR_KEY_SHIFT;
    uint64_t disable_bits = 0;
    bool matched = false;

    for (uint32_t i = 0; i < IA64_PKR_COUNT; i++) {
        uint64_t pkr = env->pkr[i];

        if ((pkr & IA64_PKR_VALID) &&
            (pkr & ia64_pkr_key_mask(env)) == pkr_key) {
            matched = true;
            disable_bits = pkr;
            break;
        }
    }

    if (!matched) {
        return is_ifetch ? IA64_EXCP_INST_KEY_MISS :
                           IA64_EXCP_DATA_KEY_MISS;
    }

    if (((needed & IA64_TLB_R) && (disable_bits & IA64_PKR_RD)) ||
        ((needed & IA64_TLB_W) && (disable_bits & IA64_PKR_WD)) ||
        ((needed & IA64_TLB_X) && (disable_bits & IA64_PKR_XD))) {
        return IA64_EXCP_KEY_PERMISSION;
    }

    return IA64_EXCP_NONE;
}

static inline IA64Exception
ia64_key_exception_for_access(const CPUIA64State *env, uint32_t key,
                              uint8_t needed, bool is_ifetch, bool is_rse)
{
    if (!ia64_key_check_enabled(env, is_ifetch, is_rse)) {
        return IA64_EXCP_NONE;
    }

    return ia64_key_exception_for_key(env, key, needed, is_ifetch);
}

static inline IA64Exception
ia64_translation_exception_for_access(const CPUIA64State *env, uint64_t pte,
                                      uint32_t key, uint8_t perm,
                                      uint8_t needed, bool is_ifetch,
                                      bool is_write, bool is_rse)
{
    IA64Exception excp;

    /*
     * Once a translation is found the remaining faults are checked in the
     * architected order: Page Not Present, NaT Page Consumption, Key Miss,
     * Key Permission, Access Rights, Dirty Bit, Access Bit.
     */
    if (!(pte & IA64_PTE_PRESENT)) {
        return IA64_EXCP_PAGE_NOT_PRESENT;
    }

    /*
     * A NaTPage translation prevents every non-speculative reference:
     * instruction fetches, loads, stores and semaphores all raise NaT Page
     * Consumption.  Control-speculative loads instead defer the fault, which
     * the speculative probe helper decides before the access is attempted.
     */
    if (ia64_pte_ma(pte) == IA64_PTE_MA_NATPAGE) {
        return IA64_EXCP_NAT_CONSUMPTION;
    }

    excp = ia64_key_exception_for_access(env, key, needed, is_ifetch, is_rse);
    if (excp != IA64_EXCP_NONE) {
        return excp;
    }

    return ia64_pte_exception_for_access(pte, perm, needed, is_ifetch,
                                         is_write, env->psr);
}

static inline IA64Exception
ia64_tlb_exception_for_access(const CPUIA64State *env,
                              const IA64TlbEntry *entry, uint8_t perm,
                              uint8_t needed, bool is_ifetch,
                              bool is_write, bool is_rse)
{
    return ia64_translation_exception_for_access(env, entry->pte, entry->key,
                                                perm, needed, is_ifetch,
                                                is_write, is_rse);
}

static inline uint8_t ia64_rr_index(uint64_t va)
{
    return (va >> IA64_REGION_SHIFT) & IA64_REGION_MASK;
}

static inline uint64_t ia64_va_vpn_mask(uint64_t ps)
{
    return IA64_REGION7_PHYS_MASK & ~(ps - 1);
}

static inline uint64_t ia64_va_page_base(uint64_t va, uint64_t ps)
{
    return va & ia64_va_vpn_mask(ps);
}

static inline bool ia64_tlb_match(const IA64TlbEntry *entry, uint64_t va,
                                  uint32_t rid)
{
    if (!entry->valid || entry->ps == 0 || entry->rid != rid) {
        return false;
    }

    return ((va ^ entry->va) & entry->page_mask) == 0;
}

static inline uint32_t
ia64_merced_dtlb1_lookup_index(uint64_t page, uint32_t rid)
{
    return (page ^ (page >> 17) ^ rid) & (IA64_DTLB1_LOOKUP_SIZE - 1);
}

static inline uint64_t ia64_merced_dtlb1_lookup_page(uint64_t va)
{
    return va >> IA64_DTLB1_LOOKUP_PAGE_SHIFT;
}

static inline QEMU_ALWAYS_INLINE int
ia64_merced_dtlb1_lookup(CPUIA64State *env, uint64_t va, uint32_t rid)
{
    uint64_t page = ia64_merced_dtlb1_lookup_page(va);
    IA64DTlb1Lookup *lookup = &env->mmu.tlb_data_l1_lookup[
        ia64_merced_dtlb1_lookup_index(page, rid)];
    IA64TlbEntry *entry;

    if (!lookup->valid || lookup->page != page ||
        lookup->rid != rid || lookup->slot >= IA64_DTLB1_MAX) {
        return -1;
    }
    entry = &env->mmu.tlb_data_l1[lookup->slot];
    if (!ia64_tlb_match(entry, va, rid)) {
        lookup->valid = false;
        return -1;
    }
    return lookup->slot;
}

static inline QEMU_ALWAYS_INLINE uint16_t
ia64_micro_tlb_index(uint64_t va, uint32_t rid)
{
    uint64_t page = va >> IA64_MICRO_TLB_PAGE_SHIFT;

    return (page ^ (page >> 17) ^ (page >> 32) ^ rid) &
           (IA64_MICRO_TLB_SIZE - 1);
}

static inline QEMU_ALWAYS_INLINE const IA64TlbEntry *
ia64_tlb_find_cached(CPUIA64State *env, uint64_t va, uint32_t rid,
                     bool is_ifetch)
{
    IA64TlbEntry *tlb = is_ifetch ? env->mmu.tlb_inst : env->mmu.tlb_data;
    IA64MicroTlbEntry *micro = is_ifetch ? env->mmu.tlb_inst_micro :
                                           env->mmu.tlb_data_micro;
    IA64MicroTlbEntry *cached = &micro[ia64_micro_tlb_index(va, rid)];
    uint32_t generation = is_ifetch ? env->mmu.tlb_inst_generation :
                                      env->mmu.tlb_data_generation;
    uint16_t tlb_count = is_ifetch ? env->mmu.tlb_inst_count :
                                     env->mmu.tlb_data_count;

    if (cached->valid && cached->generation == generation &&
        cached->rid == rid &&
        ((va ^ cached->va) & cached->page_mask) == 0) {
        if (cached->slot < tlb_count &&
            cached->slot_generation == tlb[cached->slot].micro_generation) {
            return &tlb[cached->slot];
        }
    }

    return ia64_tlb_find_slow(env, va, rid, is_ifetch);
}

static inline uint32_t ia64_region_rid(const CPUIA64State *env, uint64_t va)
{
    return ia64_rr_rid(env->rr[ia64_rr_index(va)]);
}

static inline bool ia64_current_code_tlb_ed(CPUIA64State *env)
{
    IA64CodeTlbEdCache *cached = &env->mmu.code_tlb_ed;
    IA64MicroTlbEntry remembered;
    const IA64TlbEntry *entry;
    uint16_t micro_index;
    uint32_t generation;
    uint32_t rid;

    if (!(env->psr & IA64_PSR_IT)) {
        return false;
    }

    rid = ia64_region_rid(env, env->ip);
    generation = env->mmu.tlb_inst_generation;
    micro_index = ia64_micro_tlb_index(env->ip, rid);
    if (cached->valid && cached->generation == generation &&
        cached->rid == rid &&
        ((env->ip ^ cached->va) & cached->page_mask) == 0 &&
        cached->slot_generation ==
            env->mmu.tlb_inst[cached->slot].micro_generation) {
        return cached->ed;
    }

    remembered = env->mmu.tlb_inst_micro[micro_index];
    entry = ia64_tlb_find_cached(env, env->ip, rid, true);
    if (!entry) {
        cached->valid = false;
        /*
         * A translated block may still be executing after the modeled ITLB
         * slot which supplied its instruction bytes has been replaced.  ED
         * qualifies data faults using that fetched instruction translation,
         * not whichever entry happens to occupy the slot later.  Retain the
         * PTE in the micro-TLB so this in-flight instruction keeps its ED.
         * A current matching entry always wins above, and a global TLB
         * generation change makes the remembered fetch unusable.
         */
        if (remembered.valid && remembered.generation == generation &&
            remembered.rid == rid &&
            ((env->ip ^ remembered.va) & remembered.page_mask) == 0) {
            return (remembered.pte & IA64_PTE_ED) != 0;
        }
        return false;
    }

    *cached = (IA64CodeTlbEdCache) {
        .va = entry->va,
        .page_mask = entry->page_mask,
        .rid = entry->rid,
        .generation = generation,
        .slot_generation = entry->micro_generation,
        .slot = entry->slot,
        .ed = (entry->pte & IA64_PTE_ED) != 0,
        .valid = true,
    };
    return cached->ed;
}

static inline uint64_t ia64_region_itir(const CPUIA64State *env, uint64_t va)
{
    uint64_t rr = env->rr[ia64_rr_index(va)];

    return (rr & (IA64_ITIR_PS_MASK << IA64_ITIR_PS_SHIFT)) |
           (((rr & IA64_RR_RID_MASK) >> IA64_RR_RID_SHIFT) <<
            IA64_ITIR_KEY_SHIFT);
}

static inline bool ia64_sal_boot_environment_active(const CPUIA64State *env)
{
    return env->cr_iva == IA64_FIRMWARE_IVT_BASE &&
           (env->psr & IA64_PSR_IC) != 0;
}

static inline bool ia64_data_nested_tlb_active(const CPUIA64State *env)
{
    return !(env->psr & IA64_PSR_IC) && !env->exception_state.psr_ic_inflight;
}

static inline bool ia64_vhpt_config_valid(const CPUIA64State *env,
                                          uint8_t *size,
                                          bool *long_format)
{
    uint64_t pta = env->cr_pta;

    /*
     * PTA.ve gates only the hardware VHPT walker.  Software-visible thash and
     * IHA still use the configured PTA base, size, and format.
     */
    *size = (pta & IA64_PTA_SIZE_MASK) >> IA64_PTA_SIZE_SHIFT;
    *long_format = pta & IA64_PTA_VF;
    if (*size < 15 || *size > (*long_format ? 61 : 52)) {
        return false;
    }
    return true;
}

static inline bool ia64_vhpt_walker_enabled(const CPUIA64State *env,
                                            uint64_t va, bool is_ifetch,
                                            bool is_rse,
                                            uint8_t *size,
                                            bool *long_format)
{
    if (!(env->cr_pta & IA64_PTA_VE)) {
        return false;
    }
    if (!ia64_vhpt_config_valid(env, size, long_format)) {
        return false;
    }
    if (!(env->rr[ia64_rr_index(va)] & IA64_RR_VE)) {
        return false;
    }
    if (!(env->psr & IA64_PSR_DT) ||
        (is_rse && !(env->psr & IA64_PSR_RT))) {
        return false;
    }
    if (is_ifetch &&
        (env->psr & (IA64_PSR_IT | IA64_PSR_IC)) !=
        (IA64_PSR_IT | IA64_PSR_IC)) {
        return false;
    }
    return true;
}

static inline bool ia64_exception_initializes_iha(int excp)
{
    return excp != IA64_EXCP_ALT_ITLB &&
           excp != IA64_EXCP_ALT_DTLB &&
           excp != IA64_EXCP_DATA_NESTED_TLB &&
           excp != IA64_EXCP_INST_KEY_MISS &&
           excp != IA64_EXCP_DATA_KEY_MISS &&
           excp != IA64_EXCP_KEY_PERMISSION;
}

bool ia64_vhpt_walk(CPUIA64State *env, uint64_t va, uint32_t rid,
                    bool is_ifetch, bool is_rse, uint8_t access_level,
                    uint64_t *pa, uint8_t *perm);
bool ia64_vhpt_walk_full(CPUIA64State *env, uint64_t va, uint32_t rid,
                         bool is_ifetch, bool is_rse, uint8_t access_level,
                         uint64_t *pa, uint8_t *perm, uint64_t *pte,
                         uint32_t *key,
                         const IA64TlbEntry **installed_entry);
bool ia64_vhpt_pte_not_present(CPUIA64State *env, uint64_t va,
                               bool is_ifetch, bool is_rse,
                               uint64_t *entry_va);
bool ia64_vhpt_entry_accessible(CPUIA64State *env, uint64_t va,
                                bool is_ifetch, bool is_rse,
                                uint64_t *entry_va);
uint64_t ia64_vhpt_hash_address(CPUIA64State *env, uint64_t va);
bool ia64_translate_data_access(CPUIA64State *env, uint64_t va,
                                bool is_write, uint64_t *pa);

void ia64_set_psr(CPUIA64State *env, uint64_t value);
void ia64_set_psr_bn(CPUIA64State *env, bool bank1);
#ifdef CONFIG_DEBUG_TCG
void ia64_rse_delivery_check(CPUIA64State *env, int excp);
#else
#define ia64_rse_delivery_check(env, excp) do { } while (0)
#endif

CPUState *ia64_cpu_by_sapic_id(uint8_t id, uint8_t eid);

typedef enum IA64SapicDeliveryMode {
    IA64_SAPIC_DELIVERY_INT = 0,
    IA64_SAPIC_DELIVERY_INT_REDIRECT = 1,
    IA64_SAPIC_DELIVERY_PMI = 2,
    IA64_SAPIC_DELIVERY_NMI = 4,
    IA64_SAPIC_DELIVERY_INIT = 5,
    IA64_SAPIC_DELIVERY_EXTINT = 7,
} IA64SapicDeliveryMode;

typedef enum IA64SapicDestinationMode {
    IA64_SAPIC_DESTINATION_PHYSICAL = 0,
    IA64_SAPIC_DESTINATION_LOGICAL = 1,
} IA64SapicDestinationMode;

#define IA64_SAPIC_XTP_DISABLE       0x80
#define IA64_SAPIC_XTP_PRIORITY_MASK 0x0f
#define IA64_SAPIC_XTP_WRITABLE_MASK \
    (IA64_SAPIC_XTP_DISABLE | IA64_SAPIC_XTP_PRIORITY_MASK)

bool ia64_sapic_deliver(IA64SapicDestinationMode destination_mode,
                        uint8_t id, uint8_t eid, bool redirect,
                        IA64SapicDeliveryMode delivery, uint8_t vector);
void ia64_sapic_set_xtp(CPUState *cs, uint8_t xtp);
uint8_t ia64_sapic_get_xtp(CPUState *cs);
void ia64_sapic_set_irq(CPUState *cs, uint8_t vector);
void ia64_sapic_set_init(CPUState *cs, uint8_t reason);
void ia64_sapic_update_interrupt(CPUIA64State *env);
bool ia64_sapic_has_pending(CPUIA64State *env);
bool ia64_sapic_has_pmi(CPUIA64State *env);
bool ia64_sapic_has_init(CPUIA64State *env);
int ia64_sapic_accept_pmi(CPUIA64State *env);
bool ia64_sapic_accept_init(CPUIA64State *env);
int  ia64_sapic_accept(CPUIA64State *env);
void ia64_sapic_eoi(CPUIA64State *env);
int  ia64_sapic_get_ivr(CPUIA64State *env);
void ia64_itm_update(CPUIA64State *env, uint64_t itm_value);
void ia64_itc_sync(CPUIA64State *env);
void ia64_itc_advance_pending_itm(CPUIA64State *env);
void ia64_itc_check_timer(CPUIA64State *env);
void ia64_itc_enter_halt(CPUIA64State *env);

bool ia64_ras_save_min_state(CPUIA64State *env, uint64_t address);
bool ia64_ras_restore_min_state(IA64CPU *cpu, uint64_t address,
                                bool new_context);
bool ia64_ras_enter_pmi(CPUIA64State *env, uint8_t vector);
bool ia64_ras_enter_mca(IA64CPU *cpu);
bool ia64_ras_enter_init(IA64CPU *cpu);
void ia64_ras_update_cmc(CPUIA64State *env);
void ia64_cpu_request_mca(CPUState *cs, uint64_t entry, uint64_t gp,
                          uint64_t record_id, uint8_t severity);
void ia64_cpu_set_init_entry(CPUState *cs, uint64_t entry, uint64_t gp);
void ia64_cpu_record_machine_check(CPUState *cs, uint8_t severity,
                                   uint64_t status, uint64_t address,
                                   uint64_t information);

static inline bool ia64_external_interrupt_vector_valid(uint8_t vector)
{
    return vector == 0 || vector == 2 || vector >= 16;
}

static inline int64_t ia64_itc_clock_ns(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static inline uint64_t ia64_itc_read(CPUIA64State *env)
{
    ia64_itc_advance_pending_itm(env);
    return env->ar_itc;
}

static inline void ia64_itc_write(CPUIA64State *env, uint64_t value)
{
    env->ar_itc = value;
    env->interrupt.itc_last_ns = ia64_itc_clock_ns();
    env->interrupt.itc_fraction = 0;
    env->interrupt.itc_tick_debt = 0;
    env->interrupt.itm_last_match_valid = false;
}

typedef struct IA64BootInfo {
    uint64_t firmware_base;
    uint64_t firmware_entry;
    uint64_t global_pointer;
    uint64_t iva;
    uint64_t bsp;
    uint64_t stack_pointer;
    uint64_t rsc;
    uint64_t low_ram_size;
    /* Optional QEMU-owned firmware entry arguments in r8-r10. */
    uint64_t firmware_arg0;
    uint64_t firmware_arg1;
    uint64_t firmware_arg2;
    uint64_t io_port_base;
    uint64_t interrupt_block_base;
    bool powered_off;
    bool firmware_args_valid;
    bool platform_addresses_valid;
} IA64BootInfo;

struct ArchCPU {
    CPUState parent_obj;
    CPUIA64State env;
    QEMUTimer *itm_timer;
    IA64BootInfo boot_info;
    IA64FirmwareDebugState firmware_debug;
    IA64RSEContextState mca_rse;
    bool mca_rse_valid;
    bool boot_info_valid;
    bool boot_info_pending;
    bool alat_full;
    uint64_t firmware_compat_flags;
    uint32_t socket_id;
    uint32_t core_id;
    uint32_t thread_id;
    uint32_t cores_per_socket;
    uint32_t threads_per_core;
    uint32_t package_base;
    uint32_t package_cpus;
    uint64_t semantic_profile_id;
    uint32_t semantic_profile_abi;
    uint8_t migration_alat_full;
    /*
     * Single-writer store sequence, retained across CPU resets and not migrated.
     * Odd denotes an in-progress store; even denotes a completed/canceled one.
     */
    uint64_t alat_write_sequence;
};

typedef enum IA64CPUModel {
    IA64_CPU_MODEL_MERCED,
    IA64_CPU_MODEL_MADISON,
    IA64_CPU_MODEL_MONTECITO,
} IA64CPUModel;

void ia64_cpu_set_boot_info(IA64CPU *cpu, const IA64BootInfo *info);
void ia64_cpu_reset_to_boot_info(IA64CPU *cpu);
extern const VMStateDescription vmstate_ia64_cpu;

struct IA64CPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    ResettablePhases parent_phases;

    /* Guest-visible processor-model data. */
    uint64_t semantic_profile_id;
    uint32_t semantic_profile_abi;
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
    uint64_t pal_proc_feature_available;
    uint64_t pal_proc_feature_controllable;
    bool rse_has_clean_partition;
    bool data_debug_cross_16byte;
    bool has_native_ia32;
    bool has_virtualization;
    bool is_montecito;
};

static inline uint64_t
ia64_cpu_default_io_block_pa(const IA64CPUClass *icc)
{
    return (1ULL << icc->phys_addr_bits) - IA64_IO_PORT_SPACE_SIZE;
}

static inline IA64CPU *ia64_cpu_from_cpu_state(CPUState *cs)
{
    return container_of(cs, IA64CPU, parent_obj);
}

static inline IA64FirmwareDebugState *
ia64_firmware_debug_state(CPUIA64State *env)
{
    return &ia64_cpu_from_cpu_state(env_cpu(env))->firmware_debug;
}

static inline const IA64FirmwareDebugState *
ia64_firmware_debug_state_const(const CPUIA64State *env)
{
    return &ia64_cpu_from_cpu_state(env_cpu((CPUIA64State *)env))
            ->firmware_debug;
}

static inline IA64CPUClass *ia64_env_cpu_class(CPUIA64State *env)
{
    /*
     * CPUState caches CPUClass during its parent instance initialization
     * specifically so hot paths do not repeat QOM's dynamic type lookup.
     * That initialization precedes every IA64CPU instance callback.
     */
    return container_of(env_cpu(env)->cc, IA64CPUClass, parent_class);
}

static inline const IA64CPUClass *
ia64_env_cpu_class_const(const CPUIA64State *env)
{
    return ia64_env_cpu_class((CPUIA64State *)env);
}

static inline uint64_t ia64_cpu_page_size_mask(const CPUIA64State *env)
{
    return ia64_env_cpu_class_const(env)->insertable_page_size_mask;
}

static inline uint16_t ia64_cpu_tlb_capacity(const CPUIA64State *env,
                                             bool is_data)
{
    const IA64CPUClass *icc = ia64_env_cpu_class_const(env);

    return is_data ? icc->dtlb_entries : icc->itlb_entries;
}

static inline uint64_t
ia64_cpu_purge_page_size_mask(const CPUIA64State *env)
{
    return ia64_env_cpu_class_const(env)->purgeable_page_size_mask;
}

static inline bool ia64_page_shift_insertable(const CPUIA64State *env,
                                              uint8_t page_shift)
{
    return page_shift < 64 &&
           ((ia64_cpu_page_size_mask(env) >> page_shift) & 1);
}

static inline uint64_t ia64_pkr_key_mask(const CPUIA64State *env)
{
    return ((1ULL << ia64_env_cpu_class_const(env)->key_bits) - 1) <<
           IA64_PKR_KEY_SHIFT;
}

static inline uint64_t ia64_pkr_mask(const CPUIA64State *env)
{
    return IA64_PKR_VALID | IA64_PKR_WD | IA64_PKR_RD | IA64_PKR_XD |
           ia64_pkr_key_mask(env);
}

static inline bool ia64_pa_is_implemented(const CPUIA64State *env,
                                          uint64_t addr)
{
    uint64_t implemented_mask =
        (1ULL << ia64_env_cpu_class_const(env)->phys_addr_bits) - 1;

    return (addr & ~(IA64_PHYS_UC_BIT | implemented_mask)) == 0;
}

static inline uint64_t ia64_pa_canonicalize(const CPUIA64State *env,
                                            uint64_t addr)
{
    uint64_t implemented_mask =
        (1ULL << ia64_env_cpu_class_const(env)->phys_addr_bits) - 1;

    return addr & (IA64_PHYS_UC_BIT | implemented_mask);
}

static inline bool ia64_va_is_implemented(const CPUIA64State *env,
                                          uint64_t va)
{
    uint8_t impl_va_msb = ia64_env_cpu_class_const(env)->impl_va_msb;

    if (impl_va_msb >= 60) {
        return true;
    }

    {
        uint64_t count = 60 - impl_va_msb;
        uint64_t mask = (1ULL << count) - 1;
        uint64_t unimplemented = (va >> (impl_va_msb + 1)) & mask;
        uint64_t expected = (va & (1ULL << impl_va_msb)) ? mask : 0;

        return unimplemented == expected;
    }
}

static inline uint64_t ia64_va_canonicalize(const CPUIA64State *env,
                                            uint64_t va)
{
    uint8_t impl_va_msb = ia64_env_cpu_class_const(env)->impl_va_msb;
    uint64_t region = va & (IA64_REGION_MASK << IA64_REGION_SHIFT);
    uint64_t implemented_mask = (1ULL << (impl_va_msb + 1)) - 1;
    uint64_t payload = va & implemented_mask;

    if (impl_va_msb < 60 && (payload & (1ULL << impl_va_msb))) {
        payload |= ((1ULL << (60 - impl_va_msb)) - 1) <<
                   (impl_va_msb + 1);
    }

    return region | payload;
}

#endif
