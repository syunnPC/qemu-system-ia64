"""MMU, TLB, VHPT, and protection-key microprograms."""

from __future__ import annotations

from .case import bind_cases
from .encoding import (
    DTR_PTE_NATPAGE,
    DTR_PTE_UC,
    DTR_PTE_WB,
    EIGHT_K_ITIR,
    HIGH_TR_BASE,
    IA64_ALT_DTLB_VECTOR,
    IA64_BREAK_VECTOR,
    IA64_CR_SAPIC_IRR3,
    IA64_DATA_ACCESS_BIT_VECTOR,
    IA64_DATA_ACCESS_VECTOR,
    IA64_DATA_DIRTY_VECTOR,
    IA64_DATA_KEY_MISS_VECTOR,
    IA64_DATA_NESTED_TLB_VECTOR,
    IA64_DEBUG_VECTOR,
    IA64_DCR_BE,
    IA64_DCR_DA,
    IA64_DCR_DD,
    IA64_DCR_DK,
    IA64_DCR_DM,
    IA64_DTLB_VECTOR,
    IA64_EXCP_ALT_DTLB,
    IA64_EXCP_DATA_ACCESS,
    IA64_EXCP_DATA_KEY_MISS,
    IA64_EXCP_DEBUG,
    IA64_EXCP_ILLEGAL,
    IA64_EXCP_NAT_CONSUMPTION,
    IA64_EXCP_NONE,
    IA64_EXCP_PAGE_NOT_PRESENT,
    IA64_EXCP_PRIVILEGED_OP,
    IA64_EXCP_RESERVED_REG_FIELD,
    IA64_EXCP_UNIMPL_DATA_ADDR,
    IA64_EXCP_UNALIGNED,
    IA64_EXCP_VIRTUALIZATION,
    IA64_FIRMWARE_IVT_BASE,
    IA64_FW_IDENTITY_BASE,
    IA64_GENERAL_VECTOR,
    IA64_GENEX_UNIMPL_DATA_ADDR,
    IA64_IMPL_PA_BITS,
    IA64_IMPL_VA_MSB,
    IA64_INST_ACCESS_BIT_VECTOR,
    IA64_INST_ACCESS_VECTOR,
    IA64_INST_KEY_MISS_VECTOR,
    IA64_ITLB_VECTOR,
    IA64_ISR_CODE_REG_NAT,
    IA64_ISR_NA,
    IA64_ISR_NI,
    IA64_ISR_ED,
    IA64_ISR_R,
    IA64_ISR_SP,
    IA64_ISR_W,
    IA64_ISR_X,
    IA64_KEY_PERMISSION_VECTOR,
    IA64_NAT_CONSUMPTION_VECTOR,
    IA64_PAGE_NOT_PRESENT_VECTOR,
    IA64_PHYS_UC_BIT,
    IA64_PKR_RD,
    IA64_PKR_COUNT,
    IA64_PKR_VALID,
    IA64_PKR_WD,
    IA64_PSR_AC,
    IA64_PSR_BN,
    IA64_PSR_CPL3,
    IA64_PSR_DB,
    IA64_PSR_DT,
    IA64_PSR_ED,
    IA64_PSR_IC,
    IA64_PSR_IT,
    IA64_PSR_MC,
    IA64_PSR_PK,
    IA64_PSR_VM,
    IA64_TLB_MAX,
    MADISON_TR_COUNT,
    MERCED_DTLB_ENTRIES,
    MONTECITO_TR_COUNT,
    IA64_UNALIGNED_VECTOR,
    IA64_VIRTUALIZATION_VECTOR,
    KERNEL_TR_ITIR,
    KEY_TEST_KEY,
    LONG_VHPT_RID1_TAG,
    LONG_VHPT_RID2_TAG,
    LONG_VHPT_RID2_TAG_BYTE_SWAPPED,
    LOW_VECTOR_ITIR,
    LOW_VECTOR_TR_PTE,
    PTE_ED,
    UINT64_MAX,
    addl,
    adds,
    alloc,
    alloc_m,
    br_call,
    br_call_indirect,
    br_cond,
    br_indirect,
    br_ret,
    break_m,
    bsw1,
    bundle_words,
    cmp4_eq_imm,
    cmpxchg4_acq,
    cover_b,
    dtr_setup_bundles,
    fc,
    fc_i,
    fetchadd4_acq,
    itc_d,
    itc_i,
    itr_d,
    itr_i,
    ld1,
    ld2,
    ld2_bias,
    ld2_s,
    ld8,
    ld8_a,
    ld8_c_clr,
    ld8_fill_postinc,
    ld8_s,
    lfetch,
    lfetch_fault,
    load_mem,
    mov_br_gr,
    mov_gr_psr_full,
    mov_dbr_indexed_write,
    mov_dbr_indexed_read,
    mov_ibr_indexed_read,
    mov_cpuid,
    mov_m_ar_gr,
    mov_m_cr_gr,
    mov_m_gr_ar,
    mov_m_gr_cr,
    mov_m_gr_psrl,
    mov_m_imm_ar,
    mov_m_psr_gr,
    mov_grpmc_indexed,
    mov_grpmd_indexed,
    mov_pkr_indexed,
    mov_pkr_indexed_read,
    mov_pmcgr_indexed,
    mov_pmdgr_indexed,
    mov_rr_read,
    mov_rr_write,
    movl_mlx,
    nop_i,
    nop_m,
    or_reg,
    probe_r_fault,
    probe_r_fault_ignored,
    probe_r_imm,
    probe_r_reg,
    probe_rw_fault,
    probe_w_fault,
    probe_w_imm,
    probe_w_reg,
    ptc_e,
    ptc_g,
    ptc_ga,
    ptc_l,
    ptr_d,
    ptr_d_alt,
    ptr_i,
    ptr_i_alt,
    raw_bundle,
    re,
    register_nat_consumption_test,
    require_exception,
    require_registers,
    require_uncollected_reserved_field,
    rfi_b,
    rfi_to_gr,
    rsm,
    run_program,
    run_program_jit,
    srlz_d,
    srlz_i,
    ssm,
    st1_postinc,
    st2,
    st4,
    st8,
    sub_reg,
    sync_i,
    tak,
    thash,
    tpa,
    ttag,
    xchg,
)

KERNEL_REGION5_RR = (5 << 8) | LOW_VECTOR_ITIR | 1
PERCPU_ADDR = 0xfffffffffffc0000
PERCPU_ITIR = 18 << 2
REGION7_GRANULE_RR = (7 << 8) | (24 << 2)
PERCPU_OLD_DATA = 0x1111111111111111
PERCPU_OLD_DATA_LOW, _ = bundle_words(0x00, PERCPU_OLD_DATA, 0, 0)
PERCPU_NEW_DATA = 0x2222222222222222
PERCPU_NEW_DATA_LOW, _ = bundle_words(0x00, PERCPU_NEW_DATA, 0, 0)
PTE_ACCESSED = 1 << 5
PTE_DIRTY = 1 << 6
REGION7_SCRATCH_VA = 0xe000000082fd00b0
REGION7_SCRATCH_PA = 0x4100b0
KEY_TEST_VA = 0x9000
KEY_TEST_RID = 0x123
KEY_TEST_RR = (KEY_TEST_RID << 8) | LOW_VECTOR_ITIR
KEY_TEST_ITIR = (KEY_TEST_KEY << 8) | LOW_VECTOR_ITIR
KEY_TEST_PSR = (1 << 13) | (1 << 17) | IA64_PSR_PK
KEY_TEST_PKR = IA64_PKR_VALID | (KEY_TEST_KEY << 8)

test_fetchadd4_alt_dtlb_sets_read_write_isr = require_registers(
    "fetchadd4_alt_dtlb_sets_read_write_isr", [
        (0x10, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x20, *movl_mlx(3, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         bsw1()),
        (0x50, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x60, 0x00, fetchadd4_acq(7, 3, 1), nop_i(),
         nop_i()),
        (0x1000, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x1010, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x1020, 0x10, nop_m(), nop_i(),
         br_cond(0x1020, 0x1020)),
    ], {
        "ip": 0x1020,
        "exception": IA64_EXCP_NONE,
        "r14": HIGH_TR_BASE + 0x20000,
        "r15": IA64_ISR_R | IA64_ISR_W,
    }, entry=0x10)

test_speculative_recovery_dcr_dm_defers_tlb_miss = require_registers(
    "speculative_recovery_dcr_dm_defers_tlb_miss", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17) | (1 << 36))),
        (0x30, *movl_mlx(20, IA64_DCR_DM)),
        (0x40, *movl_mlx(2, 0xa000000100020000)),
        (0x50, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(20, 0), nop_i(),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x90, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_d(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0xb0, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x4000440, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000450, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000460, 0x10, nop_m(), nop_i(),
         br_cond(0x4000460, 0x460)),
    ], {
        "ip": 0x460,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)

test_speculative_recovery_dcr_da_defers_access_bit = require_registers(
    "speculative_recovery_dcr_da_defers_access_bit", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, *movl_mlx(23, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 17) | (1 << 36))),
        (0x40, *movl_mlx(20, IA64_DCR_DA)),
        (0x50, *movl_mlx(2, 0x2000)),
        (0x60, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x70, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(20, 0), adds(6, 6, 0),
         nop_i()),
        (0x90, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0xa0, 0x00, itr_d(6, 23), nop_i(),
         nop_i()),
        (0xb0, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0xd0, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x4000440, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000450, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000460, 0x10, nop_m(), nop_i(),
         br_cond(0x4000460, 0x460)),
    ], {
        "ip": 0x460,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)


# With an ED instruction translation, DCR.dd makes a speculative data-debug
# condition recoverable.  The load produces a deferred NaT instead of taking
# the Debug fault.
test_speculative_recovery_dcr_dd_defers_data_debug = require_registers(
    "speculative_recovery_dcr_dd_defers_data_debug", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, *movl_mlx(23, LOW_VECTOR_TR_PTE)),
        (0x30, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_IT | IA64_PSR_DB)),
        (0x40, *movl_mlx(20, IA64_DCR_DD)),
        (0x50, *movl_mlx(2, 0x2000)),
        (0x60, *movl_mlx(4, 0)),
        (0x70, *movl_mlx(5, 0x2000)),
        (0x80, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        (0xa0, *movl_mlx(5, 0x81ffffffffffffff)),
        (0xb0, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0xc0, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(20, 0), adds(6, 6, 0),
         nop_i()),
        (0xf0, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0x100, 0x00, itr_d(6, 23), nop_i(), nop_i()),
        (0x110, 0x00, srlz_i(), nop_i(), nop_i()),
        (0x120, 0x00, srlz_d(), adds(31, 0x430, 0), nop_i()),
        *rfi_to_gr(0x130, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 2), nop_i(), nop_i()),
        (0x4000440, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000450, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000460, 0x10, nop_m(), nop_i(),
         br_cond(0x4000460, 0x460)),
    ], {
        "ip": 0x460,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)


# Data Access Bit has priority over Data Debug, but a deferrable higher
# condition does not hide a non-deferrable lower condition.  DCR.da defers the
# access-bit fault while the matching DBR still enters the Debug vector.
test_speculative_deferred_access_bit_yields_to_data_debug = require_registers(
    "speculative_deferred_access_bit_yields_to_data_debug", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, *movl_mlx(23, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_IT | IA64_PSR_DB)),
        (0x40, *movl_mlx(20, IA64_DCR_DA)),
        (0x50, *movl_mlx(2, 0x2000)),
        (0x60, *movl_mlx(4, 0)),
        (0x70, *movl_mlx(5, 0x2000)),
        (0x80, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        (0xa0, *movl_mlx(5, 0x81ffffffffffffff)),
        (0xb0, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        # Map the Debug vector in the same instruction translation.
        (0xc0, 0x00, adds(7, 16 << 2, 0), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(20, 0), adds(6, 6, 0),
         nop_i()),
        (0xf0, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0x100, 0x00, itr_d(6, 23), nop_i(), nop_i()),
        (0x110, 0x00, srlz_i(), nop_i(), nop_i()),
        (0x120, 0x00, srlz_d(), adds(31, 0x430, 0), nop_i()),
        *rfi_to_gr(0x130, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 2), nop_i(), nop_i()),
        (0x4000000 + IA64_DEBUG_VECTOR, 0x00,
         mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (0x4000000 + IA64_DEBUG_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(9, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_DEBUG_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_DEBUG_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_DEBUG_VECTOR + 0x30,
                 IA64_DEBUG_VECTOR + 0x30)),
    ], {
        "ip": IA64_DEBUG_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_DEBUG,
        "r8": 0x430,
        "r9": 0x2000,
        "r10": IA64_ISR_R | IA64_ISR_SP | IA64_ISR_ED,
    }, entry=0x10)

test_speculative_recovery_dcr_dk_defers_key_miss = require_registers(
    "speculative_recovery_dcr_dk_defers_key_miss", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, *movl_mlx(23, LOW_VECTOR_TR_PTE)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 17) |
                         (1 << 36) | IA64_PSR_PK)),
        (0x40, *movl_mlx(20, IA64_DCR_DK)),
        (0x50, *movl_mlx(2, 0x2000)),
        (0x60, *movl_mlx(4, IA64_PKR_VALID)),
        (0x70, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x80, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x90, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(20, 0), adds(3, 0, 0),
         nop_i()),
        (0xb0, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(7, KEY_TEST_ITIR)),
        (0xd0, 0x00, mov_m_gr_cr(7, 21), adds(6, 6, 0),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xf0, 0x00, itr_d(6, 23), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0x110, 0x00, srlz_d(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0x120, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x4000440, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000450, 0x00, nop_m(), nop_i(), nop_i()),
        (0x4000460, 0x10, nop_m(), nop_i(),
         br_cond(0x4000460, 0x460)),
    ], {
        "ip": 0x460,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)

test_cmpxchg4_region7_store = require_registers("cmpxchg4_region7_store", [
    *dtr_setup_bundles(0x10, REGION7_SCRATCH_VA, REGION7_SCRATCH_PA),
    (0x70, *movl_mlx(3, REGION7_SCRATCH_VA)),
    (0x80, *movl_mlx(4, 0xffffffff)),
    (0x90, *movl_mlx(6, 0xffffff7f)),
    (0xa0, 0x00, ssm(1 << 17), nop_i(),
     nop_i()),
    (0xb0, 0x00, st4(3, 4), nop_i(),
     nop_i()),
    (0xc0, 0x00, mov_m_gr_ar(4, 32), nop_i(),
     nop_i()),
    (0xd0, 0x00, cmpxchg4_acq(5, 3, 6), nop_i(),
     nop_i()),
    (0xe0, 0x00, load_mem(0x02, 7, 3), nop_i(),
     nop_i()),
    (0xf0, 0x10, nop_m(), nop_i(),
     br_cond(0xf0, 0xf0)),
], {
    "ip": 0xf0,
    "exception": IA64_EXCP_NONE,
    "r5": 0xffffffff,
    "r7": 0xffffff7f,
}, entry=0x10)

# SDM Vol 2, section 4.4.8: instruction fetches, non-speculative loads,
# stores and semaphores to a page marked NaTPage raise a NaT Page
# Consumption fault; only control-speculative loads defer it.
def natpage_access_test(name, access_bundle, expected_isr):
    return require_registers(name, [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x200)),
        (0xb0, *movl_mlx(4, 0x1122334455667788)),
        (0xc0, 0x00, access_bundle, nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(16, 19), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x30,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x30)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "r14": HIGH_TR_BASE + 0x200,
        "r15": expected_isr,
        "r16": 0xc0,
    }, entry=0x10)

test_natpage_load_raises_nat_consumption = natpage_access_test(
    "natpage_load_raises_nat_consumption", ld8(8, 3),
    IA64_ISR_R | 0x20)

test_natpage_store_raises_nat_consumption = natpage_access_test(
    "natpage_store_raises_nat_consumption", st8(3, 4),
    IA64_ISR_W | 0x20)

test_natpage_xchg_raises_nat_consumption = natpage_access_test(
    "natpage_xchg_raises_nat_consumption", xchg(3, 8, 3, 4),
    IA64_ISR_R | IA64_ISR_W | 0x20)

# Unaligned Data Reference is the lowest-priority data fault: the access
# translates before the read or write reports misalignment, so a misaligned
# store to a NaTPage translation reports NaT Page Consumption even with
# PSR.ac forcing the alignment check.
test_natpage_unaligned_store_outranks_unaligned = require_registers(
    "natpage_unaligned_store_outranks_unaligned", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_AC)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x201)),
        (0xb0, *movl_mlx(4, 0x1122334455667788)),
        (0xc0, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(16, 19), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x30,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x30)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "r14": HIGH_TR_BASE + 0x201,
        "r15": IA64_ISR_W | 0x20,
        "r16": 0xc0,
    }, entry=0x10)

# The control case: with a well-formed translation the same misaligned
# store still reports Unaligned Data Reference.
test_unaligned_store_reports_unaligned_when_mapped = require_registers(
    "unaligned_store_reports_unaligned_when_mapped", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_AC)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x201)),
        (0xb0, *movl_mlx(4, 0x1122334455667788)),
        (0xc0, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_UNALIGNED_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_UNALIGNED_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_UNALIGNED_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_UNALIGNED_VECTOR + 0x20,
                 IA64_UNALIGNED_VECTOR + 0x20)),
    ], {
        "ip": IA64_UNALIGNED_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_UNALIGNED,
        "r14": HIGH_TR_BASE + 0x201,
        "r15": IA64_ISR_W,
    }, entry=0x10)

# A control-speculative load defers instead of faulting, and the deferred
# exception indicator is written to the target register.
test_natpage_speculative_load_defers = require_registers(
    "natpage_speculative_load_defers", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x200)),
        (0xb0, 0x00, ld8_s(31, 3), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)

# The same rule applies when the NaTPage translation arrives through a
# short-format VHPT walk rather than a pinned translation register.
test_natpage_short_vhpt_load_raises_nat_consumption = require_registers(
    "natpage_short_vhpt_load_raises_nat_consumption", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661 | (7 << 2))),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0), nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(), nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(), nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x110, 0x00, ld8(8, 2), nop_i(), nop_i()),
        (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(16, 19), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x30,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x30)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "r14": 0xa000000000000430,
        "r15": IA64_ISR_R | 0x20,
        "r16": 0x110,
    }, entry=0x10)

# An instruction fetch from a NaTPage reports the same vector with ISR.x
# set and no read/write bits.
test_natpage_instruction_fetch_raises_nat_consumption = require_registers(
    "natpage_instruction_fetch_raises_nat_consumption", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, 0x00, adds(7, 16 << 2, 0), adds(5, 5, 0), nop_i()),
        (0x30, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20), nop_i()),
        (0x40, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0x50, *movl_mlx(19, (LOW_VECTOR_TR_PTE + 0x10000) | (7 << 2))),
        (0x60, *movl_mlx(6, 0x10000)),
        (0x70, 0x00, mov_m_gr_cr(6, 20), adds(8, 6, 0), nop_i()),
        (0x80, 0x00, itr_i(8, 19), nop_i(), nop_i()),
        (0x90, *movl_mlx(20, IA64_PSR_IC | IA64_PSR_IT)),
        (0xa0, 0x00, srlz_i(), adds(31, 0x430, 0), nop_i()),
        *rfi_to_gr(0xb0, 20, 31),
        (0x4000430, *movl_mlx(9, 0x10000)),
        (0x4000440, 0x00, nop_m(), nop_i(), mov_br_gr(1, 9)),
        (0x4000450, 0x10, nop_m(), nop_i(), br_indirect(1)),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "r14": 0x10000,
        "r15": IA64_ISR_X | 0x20,
    }, entry=0x10)

test_cmpxchg4_acq_region7_store = require_registers("cmpxchg4_acq_region7_store", [
    *dtr_setup_bundles(0x10, REGION7_SCRATCH_VA, REGION7_SCRATCH_PA),
    (0x70, *movl_mlx(3, REGION7_SCRATCH_VA)),
    (0x80, *movl_mlx(4, 0xffffffff)),
    (0x90, *movl_mlx(6, 0xffffff7f)),
    (0xa0, 0x00, ssm(1 << 17), nop_i(),
     nop_i()),
    (0xb0, 0x00, st4(3, 4), nop_i(),
     nop_i()),
    (0xc0, 0x00, mov_m_gr_ar(4, 32), nop_i(),
     nop_i()),
    (0xd0, 0x00, cmpxchg4_acq(5, 3, 6), nop_i(),
     nop_i()),
    (0xe0, 0x00, load_mem(0x02, 7, 3), nop_i(),
     nop_i()),
    (0xf0, 0x10, nop_m(), nop_i(),
     br_cond(0xf0, 0xf0)),
], {
    "ip": 0xf0,
    "exception": IA64_EXCP_NONE,
    "r5": 0xffffffff,
    "r7": 0xffffff7f,
}, entry=0x10)

test_probe_r_fault_ignored_fields_decode = require_registers(
    "probe_r_fault_ignored_fields_decode", [
        (0x10, *movl_mlx(24, HIGH_TR_BASE + 0x80000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x08, probe_r_fault_ignored(24, 3, ignored5=0x0c,
                                            ignored7=0x45, bit36=1),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x80000,
        "r31": IA64_ISR_NA | IA64_ISR_R | 5,
    }, entry=0x10)

test_probe_w_imm_decode = require_registers(
    "probe_w_imm_decode", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (3 << 7))),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x70, 0x08, probe_w_imm(7, 3, 1), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r7": 1}, entry=0x10)

test_probe_result_clears_destination_nat = require_registers(
    "probe_result_clears_destination_nat", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (3 << 7))),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(9, 1 << 32)),
        (0x70, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x100, 0),
         nop_i()),
        (0x80, 0x00, ld8_fill_postinc(17, 3, 0), nop_i(),
         nop_i()),
        (0x90, 0x08, probe_w_imm(17, 3, 0), nop_i(),
         nop_i()),
        (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r17": 1,
        "r17_nat": 0,
    }, entry=0x10)

test_probe_stacked_result_survives_call_return = require_registers(
    "probe_stacked_result_survives_call_return", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (3 << 7))),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(8, 2, 2, 0, 0),
         addl(3, 0x100, 0)),
        (0x70, 0x08, probe_w_imm(33, 3, 0), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, nop_m(), adds(8, 0, 33),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
        "r33": 1,
        "cfm_sof": 2,
        "cfm_sol": 2,
    }, entry=0x10)


def probe_register_level_nat_consumption_test(name, probe, isr_access):
    return require_registers(name, [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(2, 6, 0), addl(3, 0x100, 0),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x08, probe(7, 3, 2), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_NA | isr_access | 2,
    }, entry=0x10)


test_probe_r_register_level_nat_consumption = \
    probe_register_level_nat_consumption_test(
        "probe_r_register_level_nat_consumption", probe_r_reg, IA64_ISR_R)
test_probe_w_register_level_nat_consumption = \
    probe_register_level_nat_consumption_test(
        "probe_w_register_level_nat_consumption", probe_w_reg, IA64_ISR_W)

test_probe_w_dt_disabled_miss_raises_alt_dtlb = require_registers(
    "probe_w_dt_disabled_miss_raises_alt_dtlb", [
        (0x10, *movl_mlx(3, HIGH_TR_BASE + 0x80000)),
        (0x20, *movl_mlx(19, 1 << 13)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x08, probe_w_imm(7, 3, 1), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x80000,
        "r31": IA64_ISR_NA | IA64_ISR_W | 2,
    }, entry=0x10)

# A data-translation miss taken with PSR.ic clear reports the Data Nested
# TLB vector in place of the Alternate Data TLB vector, exactly as an
# ordinary data reference does.
test_probe_w_dt_disabled_ic_clear_miss_raises_data_nested_tlb = \
    require_registers(
        "probe_w_dt_disabled_ic_clear_miss_raises_data_nested_tlb", [
            (0x10, *movl_mlx(3, HIGH_TR_BASE + 0x80000)),
            (0x20, *movl_mlx(19, 1 << 13)),
            (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
             nop_i()),
            (0x40, 0x00, srlz_d(), nop_i(),
             nop_i()),
            (0x50, 0x00, rsm(1 << 13), nop_i(),
             nop_i()),
            (0x60, 0x00, srlz_d(), nop_i(),
             nop_i()),
            (0x70, 0x08, probe_w_imm(7, 3, 1), nop_i(),
             nop_i()),
            (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(31, 0x71, 0),
             br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                     IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
            (IA64_DATA_NESTED_TLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
             br_cond(IA64_DATA_NESTED_TLB_VECTOR + 0x10,
                     IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
        ], {
            "ip": IA64_DATA_NESTED_TLB_VECTOR + 0x10,
            "exception": IA64_EXCP_NONE,
            "r31": 0x71,
        }, entry=0x10)

# With PSR.dt=1 the non-faulting probe still takes TLB-miss faults through
# tlb_grant_permission(); with the walker disabled for the region the miss
# reports through the Alternate Data TLB vector.
test_probe_w_dt_enabled_miss_raises_alt_dtlb = require_registers(
    "probe_w_dt_enabled_miss_raises_alt_dtlb", [
        (0x10, *movl_mlx(3, HIGH_TR_BASE + 0x80000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | (1 << 17))),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x08, probe_w_imm(7, 3, 1), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x80000,
        "r31": IA64_ISR_NA | IA64_ISR_W | 2,
    }, entry=0x10)

# Maintenance conditions do not gate the granted access: a clean page still
# grants a write probe instead of reporting a Data Dirty Bit fault.
test_probe_w_clean_page_dirty_bit_not_checked = require_registers(
    "probe_w_clean_page_dirty_bit_not_checked", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_DIRTY)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, IA64_PSR_IC | (1 << 17))),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, probe_w_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
    }, entry=0x10)

# Permission failures report through GR r1 without a fault: a privilege
# level 3 probe of a privilege level 0 page returns 0.
test_probe_r_insufficient_privilege_returns_zero = require_registers(
    "probe_r_insufficient_privilege_returns_zero", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, IA64_PSR_IC | (1 << 17))),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, probe_r_imm(8, 2, 3), nop_i(),
         nop_i()),
        (0xb0, 0x00, probe_r_imm(9, 2, 0), nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 1,
    }, entry=0x10)

# A NaTPage translation is in tlb_grant_permission()'s checked list, so the
# non-faulting probe raises Data NaT Page Consumption instead of granting.
test_probe_r_natpage_dtr_raises_nat_consumption = require_registers(
    "probe_r_natpage_dtr_raises_nat_consumption", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x208)),
        (0xb0, 0x00, probe_r_imm(8, 3, 0), nop_i(),
         nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x10,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x10)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0xb0,
        "r14": HIGH_TR_BASE + 0x208,
    }, entry=0x10)

# NaT Page Consumption outranks the Key Miss, Key Permission, Access Rights,
# Access Bit and Dirty Bit checks, so a NaTPage that also fails one of those
# still reports NaT Page Consumption.  The ISR carries the Data NaT Page
# Consumption class (code{5:4} = 2) above the non-access instruction code in
# code{3:0}, and reports ed and sp as 0.
def natpage_priority_probe_test(name, pte_flags, probe_insn, expected_isr,
                                psr_extra=0):
    return require_registers(name, [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, pte_flags)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT | psr_extra), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xb0, 0x00, probe_insn, nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0xb0,
        "r14": KEY_TEST_VA,
        "r15": expected_isr,
    }, entry=0x10)


# PSR.pk is on and no PKR matches the page key, which alone would raise Data
# Key Miss.  NaTPage takes priority.
test_probe_natpage_outranks_key_miss = natpage_priority_probe_test(
    "probe_natpage_outranks_key_miss",
    LOW_VECTOR_TR_PTE | (7 << 2),
    probe_w_imm(8, 2, 0),
    IA64_ISR_NA | IA64_ISR_W | 0x22,
    psr_extra=IA64_PSR_PK)

# A probe at privilege level 3 against a privilege level 0 page would return
# 0 on its own.  NaTPage takes priority and faults instead.
test_probe_natpage_outranks_access_rights = natpage_priority_probe_test(
    "probe_natpage_outranks_access_rights",
    LOW_VECTOR_TR_PTE | (7 << 2),
    probe_r_imm(8, 2, 3),
    IA64_ISR_NA | IA64_ISR_R | 0x22)

# An unaccessed page grants the probe on its own; NaTPage still faults.
test_probe_natpage_outranks_access_bit = natpage_priority_probe_test(
    "probe_natpage_outranks_access_bit",
    (LOW_VECTOR_TR_PTE & ~PTE_ACCESSED) | (7 << 2),
    probe_r_imm(8, 2, 0),
    IA64_ISR_NA | IA64_ISR_R | 0x22)

# A clean page grants a write probe on its own; NaTPage still faults.
test_probe_natpage_outranks_dirty_bit = natpage_priority_probe_test(
    "probe_natpage_outranks_dirty_bit",
    (LOW_VECTOR_TR_PTE & ~PTE_DIRTY) | (7 << 2),
    probe_w_imm(8, 2, 0),
    IA64_ISR_NA | IA64_ISR_W | 0x22)

# Data NaT Page Consumption reports ISR.ed as 0 even while the executing
# code runs under an instruction translation whose PTE.ed is set, which
# would otherwise set ISR.ed (see unaligned_check_load_sets_isr_ed).
test_probe_natpage_reports_isr_ed_zero = require_registers(
    "probe_natpage_reports_isr_ed_zero", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, 0x00, adds(7, 16 << 2, 0), adds(5, 5, 0),
         nop_i()),
        (0x30, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x40, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(2, KEY_TEST_VA)),
        (0x60, *movl_mlx(19, LOW_VECTOR_TR_PTE | (7 << 2))),
        (0x70, 0x00, adds(6, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(6, 21), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(19), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(20,
                         IA64_PSR_IC | IA64_PSR_IT | IA64_PSR_DT)),
        (0xc0, 0x00, srlz_i(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0xd0, 20, 31),
        (0x4000430, 0x00, probe_r_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "r14": KEY_TEST_VA,
        "r15": IA64_ISR_NA | IA64_ISR_R | 0x22,
    }, entry=0x10)

# Page Not Present outranks NaT Page Consumption, so a non-present NaTPage
# reports the Page Not Present vector.
test_probe_not_present_outranks_natpage = require_registers(
    "probe_not_present_outranks_natpage", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(18,
                         (LOW_VECTOR_TR_PTE | (7 << 2)) & ~1)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(),
         nop_i()),
        (0x80, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x90, 0x00, probe_r_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
                 IA64_PAGE_NOT_PRESENT_VECTOR + 0x20)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_PAGE_NOT_PRESENT,
        "fault_ip": 0x90,
        "r14": KEY_TEST_VA,
        "r15": IA64_ISR_NA | IA64_ISR_R | 2,
    }, entry=0x10)

# The non-access subcode is preserved beside the NaT Page Consumption class
# for each non-access instruction that can reach a NaTPage translation.
test_probe_r_fault_natpage_isr_code = natpage_priority_probe_test(
    "probe_r_fault_natpage_isr_code",
    LOW_VECTOR_TR_PTE | (7 << 2),
    probe_r_fault(2, 0),
    IA64_ISR_NA | IA64_ISR_R | 0x25)

test_probe_rw_fault_natpage_isr_code = natpage_priority_probe_test(
    "probe_rw_fault_natpage_isr_code",
    LOW_VECTOR_TR_PTE | (7 << 2),
    probe_rw_fault(2, 0),
    IA64_ISR_NA | IA64_ISR_R | IA64_ISR_W | 0x25)

test_lfetch_fault_natpage_isr_code = natpage_priority_probe_test(
    "lfetch_fault_natpage_isr_code",
    LOW_VECTOR_TR_PTE | (7 << 2),
    lfetch_fault(2),
    IA64_ISR_NA | IA64_ISR_R | 0x24)


# The faulting probe forms list Data NaT Page Consumption as well.
test_probe_r_fault_natpage_raises_nat_consumption = require_registers(
    "probe_r_fault_natpage_raises_nat_consumption", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x208)),
        (0xb0, 0x00, probe_r_fault_ignored(3, 0), nop_i(),
         nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x10,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x10)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0xb0,
        "r14": HIGH_TR_BASE + 0x208,
    }, entry=0x10)

# The same NaTPage rule applies when the translation arrives through a
# short-format VHPT walk rather than an already-cached entry.
test_probe_w_natpage_short_vhpt_walk_raises_nat_consumption = \
    require_registers(
        "probe_w_natpage_short_vhpt_walk_raises_nat_consumption", [
            (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
            (0x20, *movl_mlx(17, 0xa000000000000000)),
            (0x30, *movl_mlx(18, 0x539)),
            (0x40, *movl_mlx(19, 0xbffc000000000000)),
            (0x50, *movl_mlx(20, 0x0010000004009661)),
            (0x60, *movl_mlx(21, 0x0010000004000661 | (7 << 2))),
            (0x70, *movl_mlx(22, 0x4008000)),
            (0x80, 0x00, st8(22, 21), nop_i(),
             nop_i()),
            (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
             nop_i()),
            (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
             nop_i()),
            (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
             nop_i()),
            (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
             nop_i()),
            (0xd0, 0x00, itr_d(5, 20), nop_i(),
             nop_i()),
            (0xe0, *movl_mlx(2, 0xa000000000000430)),
            (0xf0, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(),
             nop_i()),
            (0x100, 0x00, srlz_d(), nop_i(),
             nop_i()),
            (0x110, 0x00, probe_w_imm(8, 2, 0), nop_i(),
             nop_i()),
            (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
             mov_m_cr_gr(14, 20), nop_i(), nop_i()),
            (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x10,
             nop_m(), nop_i(),
             br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x10,
                     IA64_NAT_CONSUMPTION_VECTOR + 0x10)),
        ], {
            "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x10,
            "exception": IA64_EXCP_NONE,
            "fault_code": IA64_EXCP_NAT_CONSUMPTION,
            "fault_ip": 0x110,
            "r14": 0xa000000000000430,
        }, entry=0x10)

# The dt=0 probe queries the DTLB with a virtual address, so NaTPage
# translations raise Data NaT Page Consumption there as well.
test_probe_r_dt_disabled_natpage_raises_nat_consumption = require_registers(
    "probe_r_dt_disabled_natpage_raises_nat_consumption", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE | (7 << 2))),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, 0x00, ssm(IA64_PSR_IC), nop_i(),
         nop_i()),
        (0x80, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x90, 0x00, probe_r_imm(8, 2, 0), nop_i(),
         nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x10,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x10)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0x90,
        "r14": 0x9000,
    }, entry=0x10)

# Maintenance conditions do not gate the dt=0 probe either: an unaccessed
# page still grants a read and a clean page still grants a write.
test_probe_dt_disabled_maintenance_bits_grant = require_registers(
    "probe_dt_disabled_maintenance_bits_grant", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(3, 0xd000)),
        (0x80, *movl_mlx(19, LOW_VECTOR_TR_PTE & ~PTE_DIRTY)),
        (0x90, 0x00, mov_m_gr_cr(3, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(19), nop_i(),
         nop_i()),
        (0xb0, 0x00, probe_r_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0xc0, 0x00, probe_w_imm(9, 3, 0), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
        "r9": 1,
    }, entry=0x10)

# Protection keys apply to the probe query whenever PSR.pk is 1, even with
# PSR.dt = 0: a missing PKR raises Data Key Miss.
test_probe_w_dt_disabled_key_miss_raises_data_key_miss = require_registers(
    "probe_w_dt_disabled_key_miss_raises_data_key_miss", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, 0x00, ssm(IA64_PSR_IC | IA64_PSR_PK), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xb0, 0x00, probe_w_imm(8, 2, 0), nop_i(),
         nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_DATA_KEY_MISS_VECTOR + 0x10,
                 IA64_DATA_KEY_MISS_VECTOR + 0x10)),
    ], {
        "ip": IA64_DATA_KEY_MISS_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_DATA_KEY_MISS,
        "fault_ip": 0xb0,
        "r14": KEY_TEST_VA,
    }, entry=0x10)

# The read-disable bit is the symmetric case: probe.r reports 0 while the
# write side stays granted.
test_probe_r_dt_disabled_key_read_disable_returns_zero = require_registers(
    "probe_r_dt_disabled_key_read_disable_returns_zero", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, *movl_mlx(4, KEY_TEST_PKR | IA64_PKR_RD)),
        (0x60, 0x00, mov_rr_write(16, 0), adds(3, 0, 0),
         nop_i()),
        (0x70, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xb0, 0x00, ssm(IA64_PSR_IC | IA64_PSR_PK), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xd0, 0x00, probe_r_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0xe0, 0x00, probe_w_imm(9, 2, 0), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 1,
    }, entry=0x10)

# A matching PKR with the write-disable bit set reports through GR r1
# instead of faulting, and the read side stays granted.
test_probe_w_dt_disabled_key_write_disable_returns_zero = require_registers(
    "probe_w_dt_disabled_key_write_disable_returns_zero", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, *movl_mlx(4, KEY_TEST_PKR | IA64_PKR_WD)),
        (0x60, 0x00, mov_rr_write(16, 0), adds(3, 0, 0),
         nop_i()),
        (0x70, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xb0, 0x00, ssm(IA64_PSR_IC | IA64_PSR_PK), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xd0, 0x00, probe_w_imm(8, 2, 0), nop_i(),
         nop_i()),
        (0xe0, 0x00, probe_r_imm(9, 2, 0), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 1,
    }, entry=0x10)

test_lfetch_nonfault_suppresses_translation_fault = require_registers(
    "lfetch_nonfault_suppresses_translation_fault", [
        (0x10, *movl_mlx(3, HIGH_TR_BASE + 0x80000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x08, lfetch(3), nop_m(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_lfetch_fault_checks_translation = require_registers(
    "lfetch_fault_checks_translation", [
        (0x10, *movl_mlx(3, HIGH_TR_BASE + 0x80000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x08, lfetch(3, 0x2e), nop_m(), nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x80000,
        "r31": IA64_ISR_NA | IA64_ISR_R | 4,
    }, entry=0x10)

def test_high_ram_above_4g_physical_and_translated_access(qemu):
    # Use an address above 4 GiB and cover it with a supported large
    # CPU translation.  The word is in the upper 4 KiB half of an 8 KiB guest
    # page.  Keep a distinct value at the corresponding address below 4 GiB
    # so that a truncating physical-address path cannot pass merely by reading
    # back its own aliased write.
    count_pa = 0x000000010368f84c
    low_count_pa = count_pa & 0xffffffff
    page_pa = 0x000000019b502000
    count_va = 0xe00001260268f84c
    page_va = 0xe00001269b502000
    run_program(qemu, [
        *dtr_setup_bundles(0x10, count_va, count_pa,
                           page_shift=24, slot=5),
        *dtr_setup_bundles(0x70, page_va, page_pa, slot=6),
        (0xd0, *movl_mlx(2, count_va)),
        (0xe0, *movl_mlx(3, page_va)),
        (0xf0, *movl_mlx(6, low_count_pa)),
        (0x100, *movl_mlx(7, 0xabcd)),
        (0x110, 0x00, st2(6, 7), nop_i(), nop_i()),
        (0x120, *movl_mlx(4, 0x1234)),
        (0x130, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x140, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x150, 0x00, st2(2, 4), nop_i(), nop_i()),
        (0x160, 0x00, ld2_bias(8, 2), nop_i(), nop_i()),
        (0x170, 0x00, ld2_s(11, 2), nop_i(), nop_i()),
        # Exercise an atomic update in the same high physical PFN record;
        # it must neither alias nor overwrite the adjacent 16-bit count.
        (0x180, *movl_mlx(13, count_va - 0x14)),
        (0x190, *movl_mlx(14, 0x1020304050607080)),
        (0x1a0, 0x00, st8(13, 14), nop_i(), nop_i()),
        (0x1b0, *movl_mlx(14, 0x8877665544332211)),
        (0x1c0, 0x00, xchg(3, 15, 13, 14), nop_i(), nop_i()),
        (0x1d0, 0x00, ld8(16, 13), nop_i(), nop_i()),
        (0x1e0, *movl_mlx(5, 0x1122334455667788)),
        (0x1f0, 0x00, st8(3, 5), nop_i(), nop_i()),
        (0x200, *movl_mlx(6, 0x8877665544332211)),
        (0x210, 0x00, xchg(3, 9, 3, 6), nop_i(), nop_i()),
        (0x220, 0x00, ld8(10, 3), nop_i(), nop_i()),
        (0x230, 0x00, ld2(17, 2), nop_i(), nop_i()),
        (0x240, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x250, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x260, *movl_mlx(6, low_count_pa)),
        (0x270, 0x00, ld2(12, 6), nop_i(), nop_i()),
        (0x280, 0x10, nop_m(), nop_i(), br_cond(0x280, 0x280)),
    ], entry=0x10, terminal_ip=0x280, memory="8G", expected={
        "r8": 0x1234,
        "r11": 0x1234,
        "r12": 0xabcd,
        "r15": 0x1020304050607080,
        "r16": 0x8877665544332211,
        "r17": 0x1234,
        "r9": 0x1122334455667788,
        "r10": 0x8877665544332211,
    }, name="high_ram_above_4g_physical_and_translated_access")


def test_long_vhpt_large_page_high_ram_subword_remap(qemu):
    # Exercise a high-RAM subword mapping.  Unlike the pinned-DTR
    # coverage above, install the 16 MiB mapping through a long-format VHPT
    # walk, update its leaf translation, purge the old TC, and refill it at
    # the same virtual address.
    page_shift = 24
    page_mask = (1 << page_shift) - 1
    rid = 7
    count_va = 0xe00001260268f84c
    count_a_pa = 0x000000010368f84c
    count_b_pa = 0x000000011368f84c
    low_count_pa = count_a_pa & 0xffffffff
    pta = 0x10013d
    pta_size = (pta >> 2) & 0x3f
    hpn_bits = IA64_IMPL_VA_MSB + 1 - page_shift
    payload = count_va & ((1 << (IA64_IMPL_VA_MSB + 1)) - 1)
    hpn = payload >> page_shift
    entries = 1 << (pta_size - 5)
    vhpt_hash = (hpn ^ (hpn >> 7) ^ rid) & (entries - 1)
    entry_pa = (pta & ~((1 << pta_size) - 1)) | (vhpt_hash << 5)
    tag = (rid << hpn_bits) | (hpn & ((1 << hpn_bits) - 1))
    pte_a = (count_a_pa & ~page_mask) | DTR_PTE_WB
    pte_b = (count_b_pa & ~page_mask) | DTR_PTE_WB
    rr = (rid << 8) | (page_shift << 2) | 1
    itir = page_shift << 2

    run_program(qemu, [
        (0x10, *movl_mlx(2, count_a_pa)),
        (0x20, *movl_mlx(3, count_b_pa)),
        (0x30, *movl_mlx(4, low_count_pa)),
        (0x40, *movl_mlx(5, 0x1111)),
        (0x50, 0x00, st2(2, 5), nop_i(), nop_i()),
        (0x60, *movl_mlx(5, 0x2222)),
        (0x70, 0x00, st2(3, 5), nop_i(), nop_i()),
        (0x80, *movl_mlx(5, 0x5555)),
        (0x90, 0x00, st2(4, 5), nop_i(), nop_i()),
        (0xa0, *movl_mlx(15, entry_pa)),
        (0xb0, *movl_mlx(16, pte_a)),
        (0xc0, *movl_mlx(17, itir)),
        (0xd0, *movl_mlx(18, tag)),
        (0xe0, 0x00, st8(15, 16), adds(6, 8, 15), nop_i()),
        (0xf0, 0x00, st8(6, 17), adds(6, 16, 15), nop_i()),
        (0x100, 0x00, st8(6, 18), nop_i(), nop_i()),
        (0x110, *movl_mlx(19, pta)),
        (0x120, 0x00, mov_m_gr_cr(19, 8), nop_i(), nop_i()),
        (0x130, *movl_mlx(20, rr)),
        (0x140, *movl_mlx(21, count_va)),
        (0x150, 0x00, mov_rr_write(20, 21), nop_i(), nop_i()),
        (0x160, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x170, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x180, 0x00, ld2(8, 21), nop_i(), nop_i()),
        (0x190, *movl_mlx(22, 0x3333)),
        (0x1a0, 0x00, st2(21, 22), nop_i(), nop_i()),
        (0x1b0, 0x00, ld2(9, 21), nop_i(), nop_i()),

        # Modify the VHPT leaf with translation disabled, then perform the
        # architected local purge/serialization before the second walk.
        (0x1c0, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x1d0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x1e0, *movl_mlx(16, pte_b)),
        (0x1f0, 0x00, st8(15, 16), nop_i(), nop_i()),
        (0x200, 0x00, ptc_l(21, 17), nop_i(), nop_i()),
        (0x210, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x220, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x230, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x240, 0x00, ld2(10, 21), nop_i(), nop_i()),
        (0x250, *movl_mlx(22, 0x4444)),
        (0x260, 0x00, st2(21, 22), nop_i(), nop_i()),
        (0x270, 0x00, ld2(11, 21), nop_i(), nop_i()),
        (0x280, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x290, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x2a0, 0x00, ld2(12, 2), nop_i(), nop_i()),
        (0x2b0, 0x00, ld2(13, 3), nop_i(), nop_i()),
        (0x2c0, 0x00, ld2(14, 4), nop_i(), nop_i()),
        (0x2d0, 0x10, nop_m(), nop_i(), br_cond(0x2d0, 0x2d0)),
    ], entry=0x10, terminal_ip=0x2d0, memory="8G", expected={
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111,
        "r9": 0x3333,
        "r10": 0x2222,
        "r11": 0x4444,
        "r12": 0x3333,
        "r13": 0x4444,
        "r14": 0x5555,
    }, name="long_vhpt_large_page_high_ram_subword_remap")

test_itr_i_indexed_decode = require_registers("itr_i_indexed_decode", [
    (0x10, *movl_mlx(18, 0x0010000004000661)),
    (0x20, *movl_mlx(19, 1 << 36)),
    (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
     nop_i()),
    (0x60, 0x00, itr_i(5, 18), addl(31, 0x8430, 0),
     nop_i()),
    *rfi_to_gr(0x70, 19, 31),
    (0x4008430, 0x10, nop_m(), adds(31, 0x7b, 0),
     br_cond(0x4008430, 0x8440)),
    (0x4008440, 0x10, nop_m(), nop_i(),
     br_cond(0x4008440, 0x8440)),
], {"ip": 0x8440, "exception": IA64_EXCP_NONE, "r31": 0x7b}, entry=0x10)

test_itr_i_slot_uses_low_8_bits = require_registers(
    "itr_i_slot_uses_low_8_bits", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, 1 << 36)),
        (0x30, *movl_mlx(5, 0x1234000000000005)),
        (0x40, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x70, 0x00, itr_i(5, 18), addl(31, 0x8430, 0),
         nop_i()),
        *rfi_to_gr(0x80, 19, 31),
        (0x4008430, 0x10, nop_m(), adds(31, 0x7b, 0),
         br_cond(0x4008430, 0x8440)),
        (0x4008440, 0x10, nop_m(), nop_i(),
         br_cond(0x4008440, 0x8440)),
    ], {
        "ip": 0x8440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7b,
    }, entry=0x10)

test_itr_i_reserved_slot_faults = require_uncollected_reserved_field(
    "itr_i_reserved_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, MONTECITO_TR_COUNT, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
    ], fault_ip=0x30, fault_imm=itr_i(5, 18), entry=0x10)

test_itr_i_madison_last_slot = require_registers(
    "itr_i_madison_last_slot", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, 1 << 36)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21),
         adds(5, MADISON_TR_COUNT - 1, 0), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(), nop_i()),
        (0x60, 0x00, itr_i(5, 18), addl(31, 0x8430, 0), nop_i()),
        *rfi_to_gr(0x70, 19, 31),
        (0x4008430, 0x10, nop_m(), adds(31, 0x7b, 0),
         br_cond(0x4008430, 0x8440)),
        (0x4008440, 0x10, nop_m(), nop_i(),
         br_cond(0x4008440, 0x8440)),
    ], {
        "ip": 0x8440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7b,
    }, entry=0x10, cpu="madison")

test_itr_i_madison_first_invalid_slot_faults = \
    require_uncollected_reserved_field(
    "itr_i_madison_first_invalid_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, MADISON_TR_COUNT, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_i(5, 18), nop_i(), nop_i()),
    ], fault_ip=0x30, fault_imm=itr_i(5, 18), entry=0x10,
    cpu="madison")

test_itr_i_merced_first_invalid_slot_faults = \
    require_uncollected_reserved_field(
    "itr_i_merced_first_invalid_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, 8, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_i(5, 18), nop_i(), nop_i()),
    ], fault_ip=0x30, fault_imm=itr_i(5, 18), entry=0x10,
    cpu="merced")

test_itr_i_resumes_next_slot_after_tb_exit = require_registers(
    "itr_i_resumes_next_slot_after_tb_exit", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, itr_i(5, 18), adds(31, 1, 0),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r31": 1,
    }, entry=0x10)

test_itr_i_8k_translation_uses_unrounded_paddr = require_registers(
    "itr_i_8k_translation_uses_unrounded_paddr", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (0x20, *movl_mlx(19, 1 << 36)),
        (0x30, 0x00, adds(7, EIGHT_K_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), adds(31, 0x430, 0),
         nop_i()),
        (0x70, 0x00, srlz_i(), nop_i(),
         nop_i()),
        *rfi_to_gr(0x80, 19, 31),
        (0x4000430, 0x10, nop_m(), adds(31, 0x16, 0),
         br_cond(0x4000430, 0x4000430)),
        (0x4002430, 0x10, nop_m(), adds(31, 0x2b, 0),
         br_cond(0x4002430, 0x4002440)),
        (0x4002440, 0x10, nop_m(), nop_i(),
         br_cond(0x4002440, 0x4002440)),
    ], {
        "ip": 0x440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x2b,
    }, entry=0x10)

IT_ONLY_DATA_BUNDLE = (0x4009000, 0x10, nop_m(), nop_i(),
                       br_cond(0x4009000, 0x4009000))
IT_ONLY_DATA_LOW, _ = bundle_words(*IT_ONLY_DATA_BUNDLE[1:])

test_it_only_keeps_data_physical = require_registers("it_only_keeps_data_physical", [
    (0x10, *movl_mlx(18, 0x0010000004000661)),
    (0x20, *movl_mlx(19, 1 << 36)),
    (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
     nop_i()),
    (0x60, 0x00, itr_i(5, 18), addl(31, 0x8430, 0),
     nop_i()),
    *rfi_to_gr(0x70, 19, 31),
    (0x4008430, *movl_mlx(2, 0x4009000)),
    (0x4008440, 0x00, ld8(31, 2), nop_i(),
     nop_i()),
    (0x4008450, 0x10, nop_m(), nop_i(),
     br_cond(0x4008450, 0x8450)),
    IT_ONLY_DATA_BUNDLE,
], {"ip": 0x8450, "exception": IA64_EXCP_NONE, "r31": IT_ONLY_DATA_LOW}, entry=0x10)

test_itlb_uses_instruction_translation_after_data_fill = require_registers(
    "itlb_uses_instruction_translation_after_data_fill", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, DTR_PTE_WB | 0x5000000)),
        (0x30, *movl_mlx(20, DTR_PTE_WB | 0x5010000)),
        (0x40, *movl_mlx(21, 0x8000)),
        (0x50, *movl_mlx(
            23, IA64_PSR_IC | IA64_PSR_IT | IA64_PSR_DT)),
        (0x60, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x70, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), adds(6, 6, 0),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
        (0xa0, 0x00, itr_d(6, 19), nop_i(), nop_i()),
        (0xb0, 0x00, itr_i(6, 20), nop_i(), nop_i()),
        (0xc0, 0x00, srlz_i(), nop_i(), nop_i()),
        (0xd0, 0x00, srlz_d(), adds(31, 0x430, 0), nop_i()),
        *rfi_to_gr(0xe0, 23, 31),

        # Populate the data soft-TLB at 0x8000 before fetching code there.
        (0x4000430, 0x00, ld8(8, 21), mov_br_gr(1, 21), nop_i()),
        (0x4000440, 0x10, nop_m(), nop_i(), br_indirect(1)),

        # The DTR and ITR deliberately map the same VA to different pages.
        (0x5000000, 0x10, nop_m(), adds(31, 0x11, 0),
         br_cond(0x8000, 0x8010)),
        (0x5000010, 0x10, nop_m(), nop_i(), br_cond(0x8010, 0x8010)),
        (0x5010000, 0x10, nop_m(), adds(31, 0x22, 0),
         br_cond(0x8000, 0x8010)),
        (0x5010010, 0x10, nop_m(), nop_i(), br_cond(0x8010, 0x8010)),
    ], {
        "ip": 0x8010,
        "exception": IA64_EXCP_NONE,
        "r31": 0x22,
    }, entry=0x10)

PHYSICAL_ALIAS_ADDR = 0x220000
PHYSICAL_ALIAS_UC_ADDR = IA64_PHYS_UC_BIT | PHYSICAL_ALIAS_ADDR
PHYSICAL_ALIAS_FIRST = 0x1122334455667788
PHYSICAL_ALIAS_SECOND = 0x8877665544332211

test_data_physical_uc_bit_aliases_wbl_space = require_registers(
    "data_physical_uc_bit_aliases_wbl_space", [
        (0x10, *movl_mlx(3, PHYSICAL_ALIAS_ADDR)),
        (0x20, *movl_mlx(4, PHYSICAL_ALIAS_UC_ADDR)),
        (0x30, *movl_mlx(5, PHYSICAL_ALIAS_FIRST)),
        (0x40, *movl_mlx(6, PHYSICAL_ALIAS_SECOND)),
        (0x50, 0x00, st8(3, 5), nop_i(), nop_i()),
        (0x60, 0x00, ld8(8, 4), nop_i(), nop_i()),
        (0x70, 0x00, st8(4, 6), nop_i(), nop_i()),
        (0x80, 0x00, ld8(9, 3), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r8": PHYSICAL_ALIAS_FIRST,
        "r9": PHYSICAL_ALIAS_SECOND,
    }, entry=0x10)

test_itr_i_uses_region_rid = require_registers("itr_i_uses_region_rid", [
    (0x10, *movl_mlx(18, 0x0010000004000661)),
    (0x20, *movl_mlx(19, (1 << 36) | (1 << 44))),
    (0x30, *movl_mlx(20, (0x12345 << 8) | 0x68)),
    (0x40, 0x00, mov_rr_write(20, 0), nop_i(),
     nop_i()),
    (0x50, 0x00, adds(7, 0x68, 0), nop_i(),
     nop_i()),
    (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
     nop_i()),
    (0x70, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
     nop_i()),
    (0x80, 0x00, itr_i(5, 18), addl(31, 0x8430, 0),
     nop_i()),
    *rfi_to_gr(0x90, 19, 31),
    (0x4008430, 0x10, nop_m(), adds(31, 0x7b, 0),
     br_cond(0x4008430, 0x8440)),
    (0x4008440, 0x10, nop_m(), nop_i(),
     br_cond(0x4008440, 0x8440)),
], {"ip": 0x8440, "exception": IA64_EXCP_NONE, "r31": 0x7b}, entry=0x10)

test_itr_i_survives_region_register_write = require_registers(
    "itr_i_survives_region_register_write", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, (1 << 36) | (1 << 44))),
        (0x30, *movl_mlx(20, (0x12345 << 8) | 0x68)),
        (0x40, 0x00, mov_rr_write(20, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_rr_write(20, 0), addl(31, 0x8430, 0),
         nop_i()),
        *rfi_to_gr(0xa0, 19, 31),
        (0x4008430, 0x10, nop_m(), adds(31, 0x7f, 0),
         br_cond(0x4008430, 0x8440)),
        (0x4008440, 0x10, nop_m(), nop_i(),
         br_cond(0x4008440, 0x8440)),
    ], {
        "ip": 0x8440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7f,
    }, entry=0x10)

test_itr_i_match_ignores_vrn = require_registers(
    "itr_i_match_ignores_vrn", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(2, 0xa000000000000430)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 36))),
        (0x40, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x70, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x80, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0x90, 0x00, nop_m(), mov_br_gr(7, 2),
         nop_i()),
        *rfi_to_gr(0xa0, 19, 2),
        (0x4000430, 0x10, nop_m(), adds(31, 0x5a, 0),
         br_cond(0x4000430, 0x4000440)),
        (0x4000440, 0x10, nop_m(), nop_i(),
         br_cond(0x4000440, 0x4000440)),
    ], {
        "ip": 0xa000000000000440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x5a,
    }, entry=0x10)

test_itr_i_cached_translation_survives_region_register_write = \
    require_registers(
        "itr_i_cached_translation_survives_region_register_write", [
            (0x10, *movl_mlx(18, 0x0010000004000661)),
            (0x20, *movl_mlx(19, 0x0010000004010661)),
            (0x30, *movl_mlx(20, (0x12345 << 8) | LOW_VECTOR_ITIR)),
            (0x40, *movl_mlx(21, 0x8000)),
            (0x50, *movl_mlx(22, 0x20000)),
            (0x60, *movl_mlx(23, (1 << 13) | (1 << 36) | (1 << 44))),
            (0x70, 0x00, mov_rr_write(20, 0), adds(7, LOW_VECTOR_ITIR, 0),
             adds(5, 5, 0)),
            (0x80, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
            (0x90, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (0xa0, 0x00, itr_i(5, 18), nop_i(), nop_i()),
            (0xb0, 0x00, mov_m_gr_cr(22, 20), nop_i(), nop_i()),
            (0xc0, 0x00, itr_i(5, 19), nop_i(), nop_i()),
            (0xd0, 0x00, mov_rr_write(20, 0), nop_i(), nop_i()),
            (0xe0, 0x00, srlz_i(), addl(31, 0x8430, 0), nop_i()),
            *rfi_to_gr(0xf0, 23, 31),
            (0x4000430, 0x10, nop_m(), adds(31, 0x73, 0),
             br_cond(0x8430, 0x8440)),
            (0x4000440, 0x10, nop_m(), nop_i(),
             br_cond(0x8440, 0x8440)),
        ], {
            "ip": 0x8440,
            "exception": IA64_EXCP_NONE,
            "r31": 0x73,
        }, entry=0x10)

ITC_DATA_BUNDLE = (0x4009000, 0x00, 0x123456789a, 0, 0)
ITC_DATA_LOW, _ = bundle_words(*ITC_DATA_BUNDLE[1:])
KEY_TEST_DATA_BUNDLE = (0x4001000, *ITC_DATA_BUNDLE[1:])


def _dumped_tlb_vas(register_output, name):
    prefix = f"{name}["
    result = set()

    for line in register_output.splitlines():
        if not line.startswith(prefix):
            continue
        marker = " va=0x"
        if marker not in line:
            continue
        result.add(int(line.split(marker, 1)[1].split()[0], 16))
    return result

test_itc_d_uses_source_pte_and_cr_ifa = require_registers(
    "itc_d_uses_source_pte_and_cr_ifa", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x9000)),
        (0x30, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(2, 0x9000)),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        ITC_DATA_BUNDLE,
    ], {"ip": 0xa0, "exception": IA64_EXCP_NONE, "r31": ITC_DATA_LOW},
    entry=0x10)

test_dtr_match_ignores_vrn = require_registers(
    "dtr_match_ignores_vrn", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x9000)),
        (0x30, *movl_mlx(20, (0x12345 << 8) | LOW_VECTOR_ITIR)),
        (0x40, *movl_mlx(21, 0xe000000000000000)),
        (0x50, 0x00, mov_rr_write(20, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_rr_write(20, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(19, 20), adds(7, LOW_VECTOR_ITIR, 0),
         adds(5, 5, 0)),
        (0x90, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0xa0, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(2, 0xe000000000009000)),
        (0xd0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0xe0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        ITC_DATA_BUNDLE,
    ], {"ip": 0xf0, "exception": IA64_EXCP_NONE, "r31": ITC_DATA_LOW},
    entry=0x10)

test_itc_d_pl0_user_read_faults = require_registers(
    "itc_d_pl0_user_read_faults", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x9000)),
        (0x30, 0x00, adds(7, 0x38, 0), adds(31, 0xb0, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(2, 0x9000)),
        (0x80, *movl_mlx(3, (1 << 13) | (1 << 17) |
                         (3 << 32) | (1 << 44))),
        *rfi_to_gr(0x90, 3, 31),
        (0xb0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (IA64_DATA_ACCESS_VECTOR, 0x10, nop_m(), adds(31, 0x71, 0),
         br_cond(IA64_DATA_ACCESS_VECTOR,
                 IA64_DATA_ACCESS_VECTOR + 0x10)),
        (IA64_DATA_ACCESS_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_VECTOR + 0x10,
                 IA64_DATA_ACCESS_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_ACCESS_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x71,
    }, entry=0x10)

test_br_ret_cpl_change_does_not_reuse_kernel_tlb = require_registers(
    "br_ret_cpl_change_does_not_reuse_kernel_tlb", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x9000)),
        (0x30, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(2, 0x9000)),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(3, 3 << 62)),
        (0xb0, *movl_mlx(4, 0xe0)),
        (0xc0, 0x00, nop_m(), mov_m_gr_ar(3, 64),
         mov_br_gr(0, 4)),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0xe0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        (IA64_DATA_ACCESS_VECTOR, 0x10, nop_m(), adds(31, 0x72, 0),
         br_cond(IA64_DATA_ACCESS_VECTOR,
                 IA64_DATA_ACCESS_VECTOR + 0x10)),
        (IA64_DATA_ACCESS_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_VECTOR + 0x10,
                 IA64_DATA_ACCESS_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_ACCESS_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r28": ITC_DATA_LOW,
        "r31": 0x72,
    }, entry=0x10)

def test_itc_d_replaces_full_tc(qemu):
    tc_fill_count = IA64_TLB_MAX
    fill_base = 0x100000
    final_va = fill_base + (tc_fill_count + 1) * 0x4000 + 0x1000
    cursor = 0x40
    bundles = [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
    ]

    for i in range(tc_fill_count):
        va = fill_base + i * 0x4000
        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(),
             nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(),
             nop_i()),
        ])
        cursor += 0x30

    bundles.extend([
        (cursor, *movl_mlx(19, final_va)),
        (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (cursor + 0x20, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (cursor + 0x30, *movl_mlx(2, final_va)),
        (cursor + 0x40, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (cursor + 0x50, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (cursor + 0x60, 0x10, nop_m(), nop_i(),
         br_cond(cursor + 0x60, cursor + 0x60)),
        ITC_DATA_BUNDLE,
    ])

    run_program(qemu, bundles, entry=0x10, expected={
        "ip": cursor + 0x60,
        "exception": IA64_EXCP_NONE,
        "r31": ITC_DATA_LOW,
    }, name="itc_d_replaces_full_tc")


def test_itc_d_merced_main_tlb_capacity(qemu):
    fill_base = 0x100000
    page_size = 0x4000
    # Keep this microprogram outside the loader-owned IVA value, whose
    # narrowly gated boot mappings would intentionally bypass an ordinary
    # translated data access.
    ivt_base = IA64_FIRMWARE_IVT_BASE + 0x10000
    fault_ip = ivt_base + IA64_ALT_DTLB_VECTOR
    cursor = 0x60
    bundles = [
        (0x10, *movl_mlx(16, ivt_base)),
        (0x20, *movl_mlx(18, 0x4009661)),
        (0x30, 0x00, mov_m_gr_cr(16, 2), adds(7, 0x38, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, adds(31, 0, 0), nop_i(), nop_i()),
    ]

    # The hardware reference defines 96 main data-TLB entries but leaves TC
    # victim selection implementation-specific.  The model therefore uses
    # its pre-existing deterministic circular policy; after 97 insertions,
    # the first entry is the observable victim.
    for index in range(MERCED_DTLB_ENTRIES + 1):
        va = fill_base + index * page_size
        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(),
             nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(), nop_i()),
        ])
        cursor += 0x30

    bundles.extend([
        (cursor, *movl_mlx(2, fill_base)),
        (cursor + 0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (cursor + 0x20, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (cursor + 0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (cursor + 0x40, 0x00, ld8(31, 2), nop_i(), nop_i()),
        (cursor + 0x50, 0x10, nop_m(), nop_i(),
         br_cond(cursor + 0x50, cursor + 0x50)),
        (fault_ip, 0x00, mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (fault_ip + 0x10, 0x10, nop_m(), adds(31, 0x6d, 0),
         br_cond(fault_ip + 0x10, fault_ip + 0x10)),
    ])

    run_program(qemu, bundles, entry=0x10, expected={
        "ip": fault_ip + 0x10,
        "exception": IA64_EXCP_NONE,
        "r30": fill_base,
        "r31": 0x6d,
    }, name="itc_d_merced_main_tlb_capacity", cpu="merced")


def _run_itc_d_merced_dtlb1_lru(qemu, name, hot_access):
    # Keep translated data outside the firmware identity range.
    fill_base = 0x200000
    page_size = 0x4000
    data_offset = 0x1000
    cursor = 0x10000
    entry = cursor
    bundles = [
        (cursor, *movl_mlx(18, 0x4009661)),
        (cursor + 0x10, 0x00, adds(7, 0x38, 0), nop_i(), nop_i()),
        (cursor + 0x20, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
    ]
    cursor += 0x30

    def append_itc(index):
        nonlocal cursor
        va = fill_base + index * page_size + data_offset

        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(), nop_i()),
        ])
        cursor += 0x30

    def append_access(index, access=ld8, dest=30):
        nonlocal cursor
        va = fill_base + index * page_size + data_offset

        bundles.extend([
            (cursor, *movl_mlx(2, va)),
            (cursor + 0x10, 0x00, access(dest, 2), nop_i(), nop_i()),
        ])
        cursor += 0x20

    # Seed 33 DTLB2 translations, then fill all 32 DTLB1 slots.
    for index in range(33):
        append_itc(index)
    bundles.extend([
        (cursor, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (cursor + 0x10, 0x00, srlz_d(), nop_i(), nop_i()),
    ])
    cursor += 0x20
    for index in range(32):
        append_access(index)

    # Keep page zero in the direct touch cache while every other page is made
    # newer.  Its repeated hits must still update the modeled DTLB1 LRU;
    # otherwise the 33rd fill below incorrectly chooses page zero as victim.
    append_access(0, hot_access)
    for index in range(1, 32):
        append_access(index)
        append_access(0, hot_access)
    append_access(32)

    # Rotate the page-zero and page-one sources out of Merced's 96-entry
    # DTLB2.  Page zero must remain accessible solely through its recently
    # touched, non-inclusive DTLB1 copy.
    for index in range(33, MERCED_DTLB_ENTRIES + 2):
        append_itc(index)
    append_access(0, dest=31)
    terminal_ip = cursor
    bundles.append((terminal_ip, 0x10, nop_m(), nop_i(),
                    br_cond(terminal_ip, terminal_ip)))

    run_program(qemu, bundles, entry=entry, expected={
        "ip": terminal_ip,
        "exception": IA64_EXCP_NONE,
        "r31": 0,
    }, name=name, cpu="merced")


def test_itc_d_merced_dtlb1_micro_hit_updates_lru(qemu):
    _run_itc_d_merced_dtlb1_lru(
        qemu, "itc_d_merced_dtlb1_micro_hit_updates_lru", ld8)


def test_itc_d_merced_dtlb1_cmpxchg_hit_updates_lru(qemu):
    def cmpxchg_zero(dest, base):
        return cmpxchg4_acq(dest, base, 0)

    _run_itc_d_merced_dtlb1_lru(
        qemu, "itc_d_merced_dtlb1_cmpxchg_hit_updates_lru", cmpxchg_zero)


def test_itc_d_full_tc_replacement_rotates(qemu):
    tc_fill_count = IA64_TLB_MAX
    fill_base = 0x100000
    first_va = fill_base + (tc_fill_count + 1) * 0x4000 + 0x1000
    second_va = first_va + 0x4000
    cursor = 0x40
    bundles = [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
    ]

    for i in range(tc_fill_count):
        va = fill_base + i * 0x4000
        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(),
             nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(),
             nop_i()),
        ])
        cursor += 0x30

    for va in (first_va, second_va):
        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(),
             nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(),
             nop_i()),
        ])
        cursor += 0x30

    bundles.extend([
        (cursor, *movl_mlx(2, first_va)),
        (cursor + 0x10, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (cursor + 0x20, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (cursor + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(cursor + 0x30, cursor + 0x30)),
        ITC_DATA_BUNDLE,
    ])

    run_program(qemu, bundles, entry=0x10, expected={
        "ip": cursor + 0x30,
        "exception": IA64_EXCP_NONE,
        "r31": ITC_DATA_LOW,
    }, name="itc_d_full_tc_replacement_rotates")


def test_itc_d_evicted_refill_flushes_host_tlb(qemu):
    page_shift = 14
    page_size = 1 << page_shift
    itir = page_shift << 2
    target_va = 0x08000000
    fill_va = 0x10000000
    page_a = 0x05000000
    page_b = page_a + page_size
    value_a = 0x1122334455667788
    value_b = 0x8877665544332211
    cursor = 0x10
    bundles = []

    def append_itc(va, pa):
        nonlocal cursor

        bundles.extend([
            (cursor, *movl_mlx(18, pa | DTR_PTE_WB)),
            (cursor + 0x10, *movl_mlx(19, va)),
            (cursor + 0x20, 0x00, mov_m_gr_cr(19, 20),
             adds(7, itir, 0), nop_i()),
            (cursor + 0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (cursor + 0x40, 0x00, itc_d(18), nop_i(), nop_i()),
        ])
        cursor += 0x50

    # Warm QEMU's translated soft-TLB with A, then evict the modeled TC
    # entry without explicitly purging the address.  A later refill of the
    # same virtual page must not continue using the obsolete host mapping.
    append_itc(target_va, page_a)
    bundles.extend([
        (cursor, *movl_mlx(2, target_va)),
        (cursor + 0x10, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (cursor + 0x20, 0x00, srlz_d(), nop_i(), nop_i()),
        (cursor + 0x30, 0x00, ld8(30, 2), nop_i(), nop_i()),
        (cursor + 0x40, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
        (cursor + 0x50, 0x00, srlz_d(), nop_i(), nop_i()),
    ])
    cursor += 0x60

    for index in range(IA64_TLB_MAX):
        append_itc(fill_va + index * page_size, page_a)

    append_itc(target_va, page_b)
    terminal_ip = cursor + 0x40
    bundles.extend([
        (cursor, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (cursor + 0x10, 0x00, srlz_d(), nop_i(), nop_i()),
        (cursor + 0x20, 0x00, ld8(31, 2), nop_i(), nop_i()),
        (cursor + 0x30, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
        (terminal_ip, 0x10, nop_m(), nop_i(),
         br_cond(terminal_ip, terminal_ip)),
        raw_bundle(page_a, value_a, ~value_a & UINT64_MAX),
        raw_bundle(page_b, value_b, ~value_b & UINT64_MAX),
    ])

    run_program(qemu, bundles, entry=0x10, terminal_ip=terminal_ip,
                expected={
                    "exception": IA64_EXCP_NONE,
                    "r30": value_a,
                    "r31": value_b,
                }, name="itc_d_evicted_refill_flushes_host_tlb",
                timeout=5.0)


def test_itr_d_all_tr_slots_survive_tc_churn(qemu):
    page_shift = 14
    page_size = 1 << page_shift
    tr_va_base = 0x01000000
    tr_pa_base = 0x02000000
    tc_va_base = 0x10000000
    target_tc_va = tc_va_base - page_size
    target_tc_pa_a = 0x04000000
    target_tc_pa_b = target_tc_pa_a + page_size
    target_tc_value_a = 0x0123456789abcdef
    target_tc_value_b = 0xfedcba9876543210
    tc_fill_count = IA64_TLB_MAX + MONTECITO_TR_COUNT
    cursor = 0x10
    bundles = []
    data_bundles = []
    mappings = []

    for slot in range(MONTECITO_TR_COUNT):
        va = tr_va_base + slot * page_size
        pa = tr_pa_base + slot * page_size
        value = 0xa5a5000000000000 | (slot << 8) | (slot ^ 0x5a)

        mappings.append((va, value))
        data_bundles.append(raw_bundle(pa, value, ~value))
        bundles.extend(dtr_setup_bundles(
            cursor, va, pa, page_shift=page_shift, slot=slot))
        cursor += 0x60

    bundles.extend([
        (cursor, 0x00, srlz_d(), nop_i(), nop_i()),
        (cursor + 0x10, 0x00, ssm(1 << 17), adds(31, 0, 0), nop_i()),
        (cursor + 0x20, 0x00, srlz_d(), nop_i(), nop_i()),
    ])
    cursor += 0x30

    def append_mapping_checks():
        nonlocal cursor

        for va, value in mappings:
            bundles.extend([
                (cursor, *movl_mlx(2, va)),
                (cursor + 0x10, *movl_mlx(4, value)),
                (cursor + 0x20, 0x01, ld8(3, 2), nop_i(), nop_i()),
                (cursor + 0x30, 0x01, nop_m(), nop_i(),
                 sub_reg(3, 3, 4)),
                (cursor + 0x40, 0x01, nop_m(), or_reg(31, 31, 3),
                 nop_i()),
            ])
            cursor += 0x50

    # Warm the translated host cache as well as checking the model entries.
    append_mapping_checks()

    tc_pte = target_tc_pa_a | DTR_PTE_WB
    bundles.extend([
        (cursor, *movl_mlx(18, tc_pte)),
        (cursor + 0x10, 0x00, adds(7, page_shift << 2, 0), nop_i(),
         nop_i()),
        (cursor + 0x20, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
    ])
    cursor += 0x30

    # Keep one ordinary TC alongside every DTR and warm its translated
    # host entry before the replacement churn.  The later refill must not
    # retain this first physical mapping.
    bundles.extend([
        (cursor, *movl_mlx(19, target_tc_va)),
        (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (cursor + 0x20, 0x00, itc_d(18), nop_i(), nop_i()),
        (cursor + 0x30, *movl_mlx(2, target_tc_va)),
        (cursor + 0x40, 0x00, ld8(12, 2), nop_i(), nop_i()),
    ])
    cursor += 0x50

    # The DTRs share the TLB array with the TC, so MONTECITO_TR_COUNT
    # pinned entries leave IA64_TLB_MAX minus that many ordinary data-TC
    # slots.  Insert enough unique translations to fill those slots and
    # rotate through replacement.
    for index in range(tc_fill_count):
        va = tc_va_base + index * page_size
        bundles.extend([
            (cursor, *movl_mlx(19, va)),
            (cursor + 0x10, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
            (cursor + 0x20, 0x00, itc_d(18), nop_i(), nop_i()),
        ])
        cursor += 0x30

    bundles.extend([
        (cursor, *movl_mlx(18, target_tc_pa_b | DTR_PTE_WB)),
        (cursor + 0x10, *movl_mlx(19, target_tc_va)),
        (cursor + 0x20, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (cursor + 0x30, 0x00, itc_d(18), nop_i(), nop_i()),
        (cursor + 0x40, 0x00, ld8(13, 2), nop_i(), nop_i()),
    ])
    cursor += 0x50

    bundles.append((cursor, 0x00, srlz_d(), nop_i(), nop_i()))
    cursor += 0x10
    append_mapping_checks()
    terminal_ip = cursor
    bundles.extend([
        (terminal_ip, 0x10, nop_m(), nop_i(),
         br_cond(terminal_ip, terminal_ip)),
        *data_bundles,
        raw_bundle(target_tc_pa_a, target_tc_value_a,
                   ~target_tc_value_a & UINT64_MAX),
        raw_bundle(target_tc_pa_b, target_tc_value_b,
                   ~target_tc_value_b & UINT64_MAX),
    ])

    run_program(qemu, bundles, entry=0x10, expected={
        "ip": terminal_ip,
        "exception": IA64_EXCP_NONE,
        "r31": 0,
        "r12": target_tc_value_a,
        "r13": target_tc_value_b,
    }, name="itr_d_all_tr_slots_survive_tc_churn", timeout=5.0)

test_itc_d_preserves_24bit_key = require_registers(
    "itc_d_preserves_24bit_key", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(7, (0x12345 << 8) | LOW_VECTOR_ITIR)),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, 0x00, tak(31, 19), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r31": 0x12345,
    }, entry=0x10)

# A 4 GiB (2**32) page is an architected insertable page size, so itc.d/itr.d
# with ITIR.ps == 32 completes without a Reserved Register/Field fault and
# execution falls through to the terminal spin loop.
test_itc_d_4g_page_size_is_insertable = require_registers(
    "itc_d_4g_page_size_is_insertable", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, 32 << 2, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {"ip": 0x70}, entry=0x10)

test_itr_d_4g_page_size_is_insertable = require_registers(
    "itr_d_4g_page_size_is_insertable", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, 32 << 2, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), adds(5, 5, 0), nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {"ip": 0x70}, entry=0x10)

test_itc_d_present_reserved_pte_field_fault = \
    require_uncollected_reserved_field(
    "itc_d_present_reserved_pte_field_fault", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (1 << 1))),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
    ], fault_ip=0x60, fault_imm=itc_d(18), entry=0x10)

test_itc_d_present_reserved_itir_field_fault = \
    require_uncollected_reserved_field(
    "itc_d_present_reserved_itir_field_fault", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(7, LOW_VECTOR_ITIR | (1 << 63))),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
    ], fault_ip=0x60, fault_imm=itc_d(18), entry=0x10)

test_itc_d_present_reserved_ma_field_fault = \
    require_uncollected_reserved_field(
    "itc_d_present_reserved_ma_field_fault", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (1 << 2))),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
    ], fault_ip=0x60, fault_imm=itc_d(18), entry=0x10)

test_itc_i_present_reserved_pte_field_fault = \
    require_uncollected_reserved_field(
    "itc_i_present_reserved_pte_field_fault", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (1 << 1))),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_i(18), nop_i(), nop_i()),
    ], fault_ip=0x60, fault_imm=itc_i(18), entry=0x10)

test_itc_d_not_present_rejects_low_itir_reserved_field = \
    require_uncollected_reserved_field(
    "itc_d_not_present_rejects_low_itir_reserved_field", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~1)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR | 1, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
    ], fault_ip=0x60, fault_imm=itc_d(18), entry=0x10)

test_itc_d_not_present_raises_page_fault = require_registers(
    "itc_d_not_present_raises_page_fault", [
        (0x10, *movl_mlx(2, 0xa000000000000430)),
        (0x20, *movl_mlx(18, 0x0010000004000660)),
        (0x30, *movl_mlx(7, LOW_VECTOR_ITIR | (1 << 63))),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x80, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_tak_not_present_dtlb_returns_one = require_registers(
    "tak_not_present_dtlb_returns_one", [
        (0x10, *movl_mlx(18, 0x0010000004000660)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(7, (0x12345 << 8) | LOW_VECTOR_ITIR)),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, 0x00, tak(31, 19), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r31": 1,
    }, entry=0x10)

test_itr_d_not_present_raises_page_fault = require_registers(
    "itr_d_not_present_raises_page_fault", [
        (0x10, *movl_mlx(2, 0xa000000000000430)),
        (0x20, *movl_mlx(18, 0x0010000004000660)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x80, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_itc_d_clear_accessed_raises_data_access_bit = require_registers(
    "itc_d_clear_accessed_raises_data_access_bit", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x80, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_BIT_VECTOR + 0x20,
                 IA64_DATA_ACCESS_BIT_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_ACCESS_BIT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x9000,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_itc_d_clear_dirty_raises_dirty_bit = require_registers(
    "itc_d_clear_dirty_raises_dirty_bit", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_DIRTY)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(29, 0x1122334455667788)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xb0, 0x00, st8(2, 29), nop_i(),
         nop_i()),
        (IA64_DATA_DIRTY_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_DIRTY_VECTOR + 0x20,
                 IA64_DATA_DIRTY_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_DIRTY_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x9000,
        "r31": IA64_ISR_W,
    }, entry=0x10)

test_itc_d_clean_page_read_fill_store_raises_dirty_bit = require_registers(
    "itc_d_clean_page_read_fill_store_raises_dirty_bit", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, 0x0010000004009661 & ~PTE_DIRTY)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x80, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xa0, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(29, 0x1122334455667788)),
        (0xc0, 0x00, st8(2, 29), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (IA64_DATA_DIRTY_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_DIRTY_VECTOR + 0x20,
                 IA64_DATA_DIRTY_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_DIRTY_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r28": ITC_DATA_LOW,
        "r30": 0x9000,
        "r31": IA64_ISR_W,
    }, entry=0x10)

test_data_dirty_rfi_retries_word_store_once = require_registers(
    "data_dirty_rfi_retries_word_store_once", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18,
                         0x0010000004009661 & ~PTE_DIRTY)),
        (0x30, *movl_mlx(23, 0x0010000004009661)),
        (0x40, *movl_mlx(29, 0x1234)),
        (0x50, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_gr_cr(2, 20), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x90, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0xa0, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0xb0, 0x08, st2(2, 29), adds(16, 1, 16),
         adds(17, 1, 17)),
        (0xc0, 0x00, ld2(31, 2), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_DATA_DIRTY_VECTOR, 0x00, mov_m_gr_cr(7, 21),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x10, 0x00, itc_d(23),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x20, 0x00, nop_m(),
         adds(30, 1, 30), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x30, 0x10, nop_m(), nop_i(), rfi_b()),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r16": 1,
        "r17": 1,
        "r30": 1,
        "r31": 0x1234,
    }, entry=0x10)

test_data_dirty_rfi_preserves_bank1_word_store = require_registers(
    "data_dirty_rfi_preserves_bank1_word_store", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18,
                         0x0010000004009661 & ~PTE_DIRTY)),
        (0x30, 0x00, adds(6, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(6, 21), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x70, *movl_mlx(7, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x10, nop_m(), nop_i(), bsw1()),
        (0x90, 0x08, mov_gr_psr_full(7), srlz_d(), nop_i()),
        (0xa0, 0x00, nop_m(), adds(22, 0, 2),
         adds(19, 0x1234, 0)),
        (0xb0, 0x08, st2(22, 19), adds(10, 1, 10), nop_i()),
        (0xc0, 0x00, ld2(8, 2), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_DATA_DIRTY_VECTOR, *movl_mlx(18, 0x0010000004009661)),
        (IA64_DATA_DIRTY_VECTOR + 0x10, 0x00, mov_m_gr_cr(6, 21),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x20, 0x00, itc_d(18),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x30, 0x00, nop_m(),
         adds(19, 0x55, 0), adds(22, 0x66, 0)),
        (IA64_DATA_DIRTY_VECTOR + 0x40, 0x00, nop_m(),
         adds(9, 1, 9), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x50, 0x10,
         nop_m(), nop_i(), rfi_b()),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "psr": IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_BN,
        "r8": 0x1234,
        "r9": 1,
        "r10": 1,
        "r19": 0x1234,
        "r22": 0x9000,
    }, entry=0x10)

# A store to a page with both A and D clear reports the Data Dirty Bit
# fault: it outranks the Data Access Bit fault, which is what lets one
# handler set both bits instead of faulting twice.
test_itc_d_clear_accessed_store_precedes_access_bit = require_registers(
    "itc_d_clear_accessed_store_precedes_access_bit", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE &
                         ~(PTE_ACCESSED | PTE_DIRTY))),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(29, 0x1122334455667788)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xb0, 0x00, st8(2, 29), nop_i(),
         nop_i()),
        (IA64_DATA_DIRTY_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_DATA_DIRTY_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_DIRTY_VECTOR + 0x20,
                 IA64_DATA_DIRTY_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_DIRTY_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x9000,
        "r31": IA64_ISR_W,
    }, entry=0x10)

test_itc_d_psr_da_suppresses_one_data_access_bit = require_registers(
    "itc_d_psr_da_suppresses_one_data_access_bit", [
        (0x10, *movl_mlx(2, 0x9000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 17) | (1 << 38))),
        (0x80, *movl_mlx(20, 0x100)),
        (0x90, 0x00, mov_m_gr_cr(19, 16), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x100, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (0x110, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_BIT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_BIT_VECTOR + 0x20,
                 IA64_DATA_ACCESS_BIT_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_ACCESS_BIT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r28": 0,
        "r30": 0x9000,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_itc_d_data_key_miss_raises_key_vector = require_registers(
    "itc_d_data_key_miss_raises_key_vector", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, *movl_mlx(20, 0x123456789abc0000)),
        (0x60, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x90, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(20, 25), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(19, KEY_TEST_PSR)),
        (0xc0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xc0, 0xd0)),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x20, 0x00, mov_m_cr_gr(28, 21),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x30, 0x00, mov_m_cr_gr(27, 25),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_KEY_MISS_VECTOR + 0x40,
                 IA64_DATA_KEY_MISS_VECTOR + 0x40)),
    ], {
        "ip": IA64_DATA_KEY_MISS_VECTOR + 0x40,
        "exception": IA64_EXCP_NONE,
        "r30": KEY_TEST_VA,
        "r31": IA64_ISR_R,
        "r28": KEY_TEST_RR,
        "r27": 0x123456789abc0000,
    }, entry=0x10)

test_itc_d_key_permission_store_raises_permission_vector = require_registers(
    "itc_d_key_permission_store_raises_permission_vector", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, *movl_mlx(4, KEY_TEST_PKR | IA64_PKR_WD)),
        (0x60, 0x00, mov_rr_write(16, 0), adds(3, 0, 0),
         nop_i()),
        (0x70, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(29, 0x1122334455667788)),
        (0xc0, *movl_mlx(19, KEY_TEST_PSR)),
        (0xd0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xd0, 0xe0)),
        (0xe0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xf0, 0x00, st8(2, 29), nop_i(),
         nop_i()),
        (IA64_KEY_PERMISSION_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_KEY_PERMISSION_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_KEY_PERMISSION_VECTOR + 0x20, 0x00, mov_m_cr_gr(28, 21),
         nop_i(), nop_i()),
        (IA64_KEY_PERMISSION_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_KEY_PERMISSION_VECTOR + 0x30,
                 IA64_KEY_PERMISSION_VECTOR + 0x30)),
    ], {
        "ip": IA64_KEY_PERMISSION_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r30": KEY_TEST_VA,
        "r31": IA64_ISR_W,
        "r28": KEY_TEST_RR,
    }, entry=0x10)

test_itc_d_matching_pkr_allows_keyed_load = require_registers(
    "itc_d_matching_pkr_allows_keyed_load", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, *movl_mlx(4, KEY_TEST_PKR)),
        (0x60, 0x00, mov_rr_write(16, 0), adds(3, 0, 0),
         nop_i()),
        (0x70, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(19, KEY_TEST_PSR)),
        (0xc0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xc0, 0xd0)),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        KEY_TEST_DATA_BUNDLE,
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r31": ITC_DATA_LOW,
    }, entry=0x10)

test_ssm_pk_invalidates_cached_keyless_access = require_registers(
    "ssm_pk_invalidates_cached_keyless_access", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(),
         nop_i()),
        (0xa0, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (0xb0, 0x00, ssm(IA64_PSR_PK), nop_i(),
         nop_i()),
        (0xc0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (IA64_DATA_KEY_MISS_VECTOR, 0x00, nop_m(), adds(31, 0x74, 0),
         nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_KEY_MISS_VECTOR + 0x10,
                 IA64_DATA_KEY_MISS_VECTOR + 0x10)),
        KEY_TEST_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_KEY_MISS_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r28": ITC_DATA_LOW,
        "r31": 0x74,
    }, entry=0x10)

test_tpa_ignores_key_and_access_bit = require_registers(
    "tpa_ignores_key_and_access_bit", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(19, KEY_TEST_PSR)),
        (0xa0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xa0, 0xb0)),
        (0xb0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xc0, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4001000,
    }, entry=0x10)

test_tpa_natpage_reports_nonaccess_without_read = require_registers(
    "tpa_natpage_reports_nonaccess_without_read", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(2, HIGH_TR_BASE + 0x208)),
        (0xb0, 0x00, tpa(31, 2), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0xb0,
        "r30": HIGH_TR_BASE + 0x208,
        "r31": IA64_ISR_NA | 0x20,
    }, entry=0x10)

# The non-faulting probe forms evaluate the address with the architected
# tlb_grant_permission() function, which raises Key Miss faults like a
# normal reference; only permission failures report through GR r1.
test_probe_w_key_miss_raises_data_key_miss = require_registers(
    "probe_w_key_miss_raises_data_key_miss", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(19, KEY_TEST_PSR)),
        (0xa0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xa0, 0xb0)),
        (0xb0, 0x00, srlz_d(), adds(31, 0x55, 0),
         nop_i()),
        (0xc0, 0x08, probe_w_imm(7, 2, 0), nop_i(),
         nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_KEY_MISS_VECTOR + 0x20,
                 IA64_DATA_KEY_MISS_VECTOR + 0x20)),
    ], {
        "ip": IA64_DATA_KEY_MISS_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": KEY_TEST_VA,
        "r31": IA64_ISR_NA | IA64_ISR_W | 2,
    }, entry=0x10)

test_itr_i_instruction_key_miss_raises_key_vector = require_registers(
    "itr_i_instruction_key_miss_raises_key_vector", [
        (0x10, *movl_mlx(16, KEY_TEST_RR)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(19, (1 << 13) | (1 << 36) | IA64_PSR_PK)),
        (0x50, 0x00, mov_rr_write(16, 17), nop_i(),
         nop_i()),
        (0x60, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x70, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(2, 0xa000000000000430)),
        (0xa0, *movl_mlx(7, KEY_TEST_ITIR)),
        (0xb0, 0x00, mov_m_gr_cr(7, 21), adds(6, 6, 0),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(2, 20), nop_i(),
         nop_i()),
        (0xd0, 0x00, itr_i(6, 18), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(4, IA64_PKR_VALID)),
        (0xf0, 0x00, adds(3, 0, 0), mov_br_gr(7, 2),
         nop_i()),
        (0x100, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(),
         nop_i()),
        *rfi_to_gr(0x110, 19, 2),
        (0x4000000 + IA64_INST_KEY_MISS_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_KEY_MISS_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_KEY_MISS_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(28, 21), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_KEY_MISS_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_INST_KEY_MISS_VECTOR + 0x30,
                 IA64_INST_KEY_MISS_VECTOR + 0x30)),
    ], {
        "ip": IA64_INST_KEY_MISS_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_X,
        "r28": KEY_TEST_RR,
    }, entry=0x10)

test_itr_d_8k_translation_uses_unrounded_paddr = require_registers(
    "itr_d_8k_translation_uses_unrounded_paddr", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (0x20, *movl_mlx(2, 0x4000430)),
        (0x30, *movl_mlx(3, 0x1111111111111111)),
        (0x40, *movl_mlx(4, 0x4002430)),
        (0x50, *movl_mlx(5, 0x2222222222222222)),
        (0x60, 0x00, st8(2, 3), nop_i(),
         nop_i()),
        (0x70, 0x00, st8(4, 5), nop_i(),
         nop_i()),
        (0x80, 0x00, adds(7, EIGHT_K_ITIR, 0), adds(6, 5, 0),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0xb0, 0x00, itr_d(6, 18), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xd0, *movl_mlx(19, 1 << 17)),
        (0xe0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0xf0, *movl_mlx(2, 0x430)),
        (0x100, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": 0x2222222222222222,
    }, entry=0x10)

test_itr_d_8k_odd_subpage_store_visible_across_call = require_registers(
    "itr_d_8k_odd_subpage_store_visible_across_call", [
        *dtr_setup_bundles(0x10, 0xe0000106014cdcf0, 0x4101cf0,
                           page_shift=13),
        (0x70, *movl_mlx(3, 0xe0000106014cdcf0)),
        (0x80, *movl_mlx(4, 0x1122334455667788)),
        (0x90, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0xa0, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xb0, 0x100)),
        (0x100, 0x00, ld8(31, 3), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": 0x1122334455667788,
    }, entry=0x10)

test_itc_d_virtual_stack_local_passed_as_high_sol_output = require_registers(
    "itc_d_virtual_stack_local_passed_as_high_sol_output", [
        (0x10, *movl_mlx(18, 0x001000000a096461)),
        (0x20, *movl_mlx(19, 0xe0000106014cdcf0)),
        (0x30, *movl_mlx(21, (1 << 8) | EIGHT_K_ITIR)),
        (0x40, 0x00, mov_rr_write(21, 19), nop_i(),
         nop_i()),
        (0x50, 0x08, mov_m_gr_cr(19, 20), mov_m_gr_cr(21, 21),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(12, 0xe0000106014cdcf0)),
        (0x80, 0x00, nop_m(), alloc(35, 4, 0, 0, 0),
         nop_i()),
        (0x90, *movl_mlx(32, 0x1ec50000)),
        (0xa0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xb0, 0x200)),
        (0x200, 0x00, alloc_m(41, 19, 11, 0, 0),
         adds(12, -16, 12), nop_i()),
        (0x210, 0x00, nop_m(), adds(14, 16, 12),
         nop_i()),
        (0x220, 0x00, st8(14, 32), adds(43, 16, 12),
         nop_i()),
        (0x230, 0x10, nop_m(), nop_i(),
         br_call(0, 0x230, 0x300)),
        (0x300, 0x00, alloc_m(36, 8, 7, 0, 0), nop_i(),
         nop_i()),
        (0x310, 0x00, ld8(8, 32), adds(9, 0, 32),
         nop_i()),
        (0x320, 0x10, nop_m(), nop_i(),
         br_cond(0x320, 0x320)),
    ], {
        "ip": 0x320,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1ec50000,
        "r9": 0xe0000106014cdcf0,
    }, entry=0x10)


def _itc_d_mii_slot0_without_stop_is_illegal(name, template):
    return require_registers(name, [
        # Prepare a valid itc.d with PSR.ic clear.  Converting itc_d() to int
        # deliberately suppresses bundle_words()'s automatic end-group
        # template selection.  The raw MII 0x02/0x03 template has no stop
        # after slot 0, so the must-end instruction is illegal.
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(2, 0x9000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(2, 20), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x60, template, int(itc_d(18)), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "exception": IA64_EXCP_ILLEGAL,
        "fault_ip": 0x60,
    }, entry=0x10)


test_itc_d_mii_02_slot0_without_stop_is_illegal = \
    _itc_d_mii_slot0_without_stop_is_illegal(
        "itc_d_mii_02_slot0_without_stop_is_illegal", 0x02)

test_itc_d_mii_03_slot0_without_stop_is_illegal = \
    _itc_d_mii_slot0_without_stop_is_illegal(
        "itc_d_mii_03_slot0_without_stop_is_illegal", 0x03)


test_itc_i_m_unit_decode = require_registers("itc_i_m_unit_decode", [
    (0x10, *movl_mlx(18, 0x0010000004000661)),
    (0x20, *movl_mlx(19, 1 << 36)),
    (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
     nop_i()),
    (0x60, 0x08, itc_i(18), addl(31, 0x8430, 0),
     nop_i()),
    *rfi_to_gr(0x70, 19, 31),
    (0x4008430, 0x10, nop_m(), adds(31, 0x7b, 0),
     br_cond(0x4008430, 0x8440)),
    (0x4008440, 0x10, nop_m(), nop_i(),
     br_cond(0x4008440, 0x8440)),
], {"ip": 0x8440, "exception": IA64_EXCP_NONE, "r31": 0x7b},
   entry=0x10)

test_itc_i_resumes_next_slot_after_tb_exit = require_registers(
    "itc_i_resumes_next_slot_after_tb_exit", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x08, itc_i(18), adds(31, 1, 0),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r31": 1,
    }, entry=0x10)

test_ptc_l_4g_page_size_is_purgeable = require_registers(
    "ptc_l_4g_page_size_is_purgeable", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(20, 0x9000)),
        (0x30, 0x00, adds(7, 0x38, 0), adds(24, 32 << 2, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x70, 0x08, nop_m(), ptc_l(20, 24), nop_i()),
        (0x80, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x90, *movl_mlx(22, IA64_PSR_IC | IA64_PSR_DT)),
        (0xa0, 0x00, mov_gr_psr_full(22), nop_i(), nop_i()),
        (0xb0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xc0, 0x00, ld8(29, 20), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x9000,
        "r31": IA64_ISR_R,
    }, entry=0x10)

PTC_SURVIVOR_BUNDLE = (0x4010000, 0x00, 0xfeedfacecafebeef, 0, 0)
PTC_SURVIVOR_LOW, _ = bundle_words(*PTC_SURVIVOR_BUNDLE[1:])

test_ptc_l_keeps_nonoverlapping_tc = require_registers(
    "ptc_l_keeps_nonoverlapping_tc", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x4010661)),
        (0x30, *movl_mlx(20, 0x9000)),
        (0x40, *movl_mlx(21, 0x20000)),
        (0x50, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
        (0xa0, 0x00, itc_d(19), nop_i(), nop_i()),
        (0xb0, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(), nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xd0, 0x00, ld8(28, 20), nop_i(), nop_i()),
        (0xe0, 0x08, nop_m(), ptc_l(20, 7), nop_i()),
        (0xf0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x100, 0x00, ld8(31, 21), nop_i(), nop_i()),
        (0x110, 0x00, ld8(29, 20), nop_i(), nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (0x1000, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
        ITC_DATA_BUNDLE,
        PTC_SURVIVOR_BUNDLE,
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r30": 0x9000,
        "r28": ITC_DATA_LOW,
        "r31": PTC_SURVIVOR_LOW,
    }, entry=0x10)

test_ptc_l_does_not_clear_local_alat = require_registers(
    "ptc_l_does_not_clear_local_alat", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(19, 0x9000)),
        (0x90, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0xc0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(22, (1 << 13) | (1 << 17))),
        (0xf0, 0x00, mov_gr_psr_full(22), nop_i(),
         nop_i()),
        (0x100, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x110, 0x00, ld8_a(31, 2), nop_i(),
         nop_i()),
        (0x120, *movl_mlx(31, 0x55)),
        (0x130, 0x00, ptc_l(19, 7), nop_i(),
         nop_i()),
        (0x140, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x150, 0x00, ld8_c_clr(31, 2), nop_i(),
         nop_i()),
        (0x160, 0x10, nop_m(), nop_i(),
         br_cond(0x160, 0x160)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0x160,
        "exception": IA64_EXCP_NONE,
        "r31": 0x55,
    }, entry=0x10)

test_ptr_i_preserves_non_overlapping_itr = require_registers(
    "ptr_i_preserves_non_overlapping_itr", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 36) | (1 << 44))),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), adds(5, 0, 0),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(3, 0x10000)),
        (0x80, 0x00, ptr_i(3, 7), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(20, HIGH_TR_BASE + 0x20000)),
        *rfi_to_gr(0xa0, 19, 20),
        (0x4000c00, 0x10, nop_m(), adds(31, 0x68, 0),
         br_cond(0x4000c00, 0x0c10)),
        (0x4000c10, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c10, 0x0c10)),
    ], {
        "ip": 0x0c10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x68,
    }, entry=0x10)

test_ptr_i_purges_matching_itr_by_address = require_registers(
    "ptr_i_purges_matching_itr_by_address", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 36) | (1 << 44))),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), adds(5, 0, 0),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x10000)),
        (0x80, *movl_mlx(20, 0x10000)),
        (0x90, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0),
         nop_i()),
        (0xa0, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(3, 0x10000)),
        (0xc0, 0x00, ptr_i(3, 7), nop_i(),
         nop_i()),
        (0xd0, 0x00, srlz_i(), nop_i(),
         nop_i()),
        *rfi_to_gr(0xe0, 19, 20),
        (0x4000c00, 0x10, nop_m(), adds(31, 0x69, 0),
         br_cond(0x4000c00, 0x0c10)),
        (0x4000c10, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c10, 0x0c10)),
        (0x4010000, 0x10, nop_m(), adds(31, 0x44, 0),
         br_cond(0x4010000, 0x10000)),
    ], {
        "ip": 0x0c10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x69,
    }, entry=0x10)

test_ptr_alt_decode = require_registers("ptr_alt_decode", [
    (0x10, 0x00, adds(2, 0x10000, 0), adds(3, LOW_VECTOR_ITIR, 0),
     nop_i()),
    (0x20, 0x00, ptr_d_alt(2, 3), nop_i(),
     nop_i()),
    (0x30, 0x00, ptr_i_alt(2, 3), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), adds(31, 0x6a, 0),
     br_cond(0x40, 0x50)),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "exception": IA64_EXCP_NONE,
    "r31": 0x6a,
}, entry=0x10)

test_alt_itlb_when_vhpt_disabled = require_registers(
    "alt_itlb_when_vhpt_disabled", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 36) | (1 << 44))),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(20, HIGH_TR_BASE + 0x20000)),
        *rfi_to_gr(0x80, 19, 20),
        (0x4000c00, 0x10, nop_m(), adds(31, 0x7d, 0),
         br_cond(0x4000c00, 0x0c10)),
        (0x4000c10, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c10, 0x0c10)),
    ], {
        "ip": 0x0c10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7d,
    }, entry=0x10)

test_alt_dtlb_when_vhpt_disabled = require_registers(
    "alt_dtlb_when_vhpt_disabled", [
        (0x10, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x20, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         bsw1()),
        (0x50, 0x00, ld8(8, 2), nop_i(),
         nop_i()),
        (0x1000, 0x10, nop_m(), adds(31, 0x7e, 0),
         br_cond(0x1000, 0x1010)),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7e,
    }, entry=0x10)

test_alt_dtlb_preserves_iha = require_registers(
    "alt_dtlb_preserves_iha", [
        (0x10, *movl_mlx(16, 0x123456789abc0000)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x30, *movl_mlx(2, HIGH_TR_BASE + 0x21000)),
        (0x40, 0x00, mov_m_gr_cr(16, 25), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         bsw1()),
        (0x70, 0x00, ld8(8, 2), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 25), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x123456789abc0000,
        "r31": HIGH_TR_BASE + 0x21000,
    }, entry=0x10)

test_alt_itlb_preserves_iha = require_registers(
    "alt_itlb_preserves_iha", [
        (0x10, *movl_mlx(16, 0x123456789abc0000)),
        (0x20, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 36) | (1 << 44))),
        (0x40, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
         nop_i()),
        (0x70, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(20, HIGH_TR_BASE + 0x22000)),
        (0x90, 0x00, mov_m_gr_cr(16, 25), mov_br_gr(7, 20),
         nop_i()),
        *rfi_to_gr(0xa0, 19, 20),
        (0x4000c00, 0x00, mov_m_cr_gr(30, 25), nop_i(),
         nop_i()),
        (0x4000c10, 0x00, mov_m_cr_gr(31, 20), nop_i(),
         nop_i()),
        (0x4000c20, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c20, 0x0c20)),
    ], {
        "ip": 0x0c20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x123456789abc0000,
        "r31": HIGH_TR_BASE + 0x22000,
    }, entry=0x10)

test_dtlb_miss_slot1_resumes_without_replaying_slot0 = require_registers(
    "dtlb_miss_slot1_resumes_without_replaying_slot0", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x20, *movl_mlx(4, 0x12345678)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x40, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x60, 0x08, adds(16, 1, 16), st8(2, 4),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x1000, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(), nop_i()),
        (0x1010, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x1020, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x1030, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r16": 1,
    }, entry=0x10)

test_dtlb_fault_itir_uses_region_rid = require_registers(
    "dtlb_fault_itir_uses_region_rid", [
        (0x10, *movl_mlx(17, 0xe000010000020000)),
        (0x20, *movl_mlx(18, (0x123 << 8) | (13 << 2))),
        (0x30, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8(8, 17), nop_i(),
         nop_i()),
        (0x1000, 0x10, mov_m_cr_gr(31, 21), nop_i(),
         br_cond(0x1000, 0x1010)),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r31": (0x123 << 8) | (13 << 2),
    }, entry=0x10)

test_alt_itlb_when_vhpt_ic_disabled = require_registers(
    "alt_itlb_when_vhpt_ic_disabled", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, HIGH_TR_BASE)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, (1 << 17) | (1 << 36) | (1 << 44))),
        (0x50, *movl_mlx(20, LOW_VECTOR_TR_PTE)),
        (0x60, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
         nop_i()),
        (0x90, 0x00, itr_i(5, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(20, HIGH_TR_BASE + 0x30000)),
        *rfi_to_gr(0xd0, 19, 20),
        (0x4000c00, 0x10, nop_m(), adds(31, 0x6c, 0),
         br_cond(0x4000c00, 0x0c10)),
        (0x4000c10, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c10, 0x0c10)),
    ], {
        "ip": 0x0c10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6c,
    }, entry=0x10)

test_data_nested_tlb_when_vhpt_ic_disabled = require_registers(
    "data_nested_tlb_when_vhpt_ic_disabled", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, HIGH_TR_BASE)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 1 << 17)),
        (0x50, *movl_mlx(2, HIGH_TR_BASE + 0x40000)),
        (0x60, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         bsw1()),
        (0xa0, 0x08, ld8(8, 2), nop_i(),
         nop_i()),
        (0x1400, 0x10, nop_m(), adds(31, 0x6e, 0),
         br_cond(0x1400, 0x1410)),
        (0x1410, 0x10, nop_m(), nop_i(),
         br_cond(0x1410, 0x1410)),
    ], {
        "ip": 0x1410,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6e,
    }, entry=0x10)

test_ssm_ic_inflight_dtlb_sets_ni = require_registers(
    "ssm_ic_inflight_dtlb_sets_ni", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x50000)),
        (0x20, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x40, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8(8, 2), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(30, 1, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": IA64_ISR_R | IA64_ISR_NI,
    }, entry=0x10)

test_kr3_update_does_not_serialize_inflight_psr_ic = require_registers(
    "kr3_update_does_not_serialize_inflight_psr_ic", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x58000)),
        (0x20, *movl_mlx(20, 0x1140000)),
        (0x30, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ssm(IA64_PSR_IC), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_gr_ar(20, 3), nop_i(), nop_i()),
        (0x70, 0x00, ld8(8, 2), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(30, 1, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": IA64_ISR_R | IA64_ISR_NI,
    }, entry=0x10)

test_rsm_ic_inflight_dtlb_not_data_nested = require_registers(
    "rsm_ic_inflight_dtlb_not_data_nested", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x60000)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, rsm(1 << 13), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8(8, 2), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(30, 1, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": IA64_ISR_R | IA64_ISR_NI,
    }, entry=0x10)

test_rsm_ic_serialized_data_nested_tlb = require_registers(
    "rsm_ic_serialized_data_nested_tlb", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x70000)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, rsm(1 << 13), nop_i(),
         nop_i()),
        (0x60, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x70, 0x00, ld8(8, 2), nop_i(),
         nop_i()),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(31, 0x6f, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR + 0x10,
                 IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
    ], {
        "ip": IA64_DATA_NESTED_TLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6f,
    }, entry=0x10)

test_exception_preserves_translation_bits = require_registers(
    "exception_preserves_translation_bits", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17) |
                         (1 << 27) | (1 << 36))),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), adds(31, 0x80, 0),
         nop_i()),
        *rfi_to_gr(0x70, 19, 31),
        (0x4000080, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x4000000 + IA64_BREAK_VECTOR, 0x10, nop_m(), adds(31, 0x7c, 0),
         br_cond(IA64_BREAK_VECTOR, IA64_BREAK_VECTOR + 0x10)),
        (0x4000000 + IA64_BREAK_VECTOR + 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_BREAK_VECTOR + 0x10,
                 IA64_BREAK_VECTOR + 0x10)),
    ], {
        "ip": IA64_BREAK_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x7c,
        "psr": (1 << 17) | (1 << 27) | (1 << 36),
    }, entry=0x10)

test_tpa_indexed_decode = require_registers("tpa_indexed_decode", [
    (0x10, *movl_mlx(18, 0x0010000004000661)),
    (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_m_gr_cr(0, 20), adds(5, 5, 0),
     nop_i()),
    (0x50, 0x00, itr_d(5, 18), nop_i(),
     nop_i()),
    (0x60, 0x00, addl(2, 0x8430, 0), nop_i(),
     nop_i()),
    (0x70, 0x00, ssm(1 << 17), nop_i(),
     nop_i()),
    (0x80, 0x00, tpa(31, 2), nop_i(),
     nop_i()),
    (0x90, 0x10, nop_m(), nop_i(),
     br_cond(0x90, 0x90)),
], {"ip": 0x90, "exception": IA64_EXCP_NONE, "r31": 0x4008430}, entry=0x10)

test_itr_d_slot_uses_low_8_bits = require_registers(
    "itr_d_slot_uses_low_8_bits", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(5, 0x1234000000000005)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, addl(2, 0x8430, 0), nop_i(),
         nop_i()),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4008430,
    }, entry=0x10)

test_itr_d_reserved_slot_faults = require_uncollected_reserved_field(
    "itr_d_reserved_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, MONTECITO_TR_COUNT, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
    ], fault_ip=0x30, fault_imm=itr_d(5, 18), entry=0x10)

test_itr_d_madison_last_slot = require_registers(
    "itr_d_madison_last_slot", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(7, 12 << 2, 0),
         adds(5, MADISON_TR_COUNT - 1, 0), nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(), nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x60, 0x00, addl(2, 0x430, 0), nop_i(), nop_i()),
        (0x70, 0x00, ssm(1 << 17), nop_i(), nop_i()),
        (0x80, 0x00, tpa(31, 2), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
    }, entry=0x10, cpu="madison")

test_itr_d_madison_first_invalid_slot_faults = \
    require_uncollected_reserved_field(
    "itr_d_madison_first_invalid_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, MADISON_TR_COUNT, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_d(5, 18), nop_i(), nop_i()),
    ], fault_ip=0x30, fault_imm=itr_d(5, 18), entry=0x10,
    cpu="madison")

test_itr_d_merced_last_slot = require_registers(
    "itr_d_merced_last_slot", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(7, 12 << 2, 0), adds(5, 47, 0), nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(), nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x60, 0x00, addl(2, 0x430, 0), nop_i(), nop_i()),
        (0x70, 0x00, ssm(1 << 17), nop_i(), nop_i()),
        (0x80, 0x00, tpa(31, 2), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
    }, entry=0x10, cpu="merced")

test_itr_d_merced_64m_page_size = require_registers(
    "itr_d_merced_64m_page_size", [
        (0x10, *movl_mlx(18, 0x00100ffffc000671)),
        (0x20, *movl_mlx(19, 0xe0000000f0000000)),
        (0x30, 0x00, adds(7, 26 << 2, 0), adds(5, 5, 0), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x70, *movl_mlx(2, 0xe0000000f0000430)),
        (0x80, 0x00, tpa(31, 2), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r31": 0x00000ffffc000430,
    }, entry=0x10, cpu="merced")

test_itr_d_merced_first_invalid_slot_faults = \
    require_uncollected_reserved_field(
    "itr_d_merced_first_invalid_slot_faults", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, 0x00, adds(5, 48, 0), nop_i(), nop_i()),
        (0x30, 0x00, itr_d(5, 18), nop_i(), nop_i()),
    ], fault_ip=0x30, fault_imm=itr_d(5, 18), entry=0x10,
    cpu="merced")

test_tpa_dt_disabled_uses_dtlb_entry = require_registers(
    "tpa_dt_disabled_uses_dtlb_entry", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, addl(2, 0x8430, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4008430,
    }, entry=0x10)

test_tpa_region5_kernel_dtr_large_page = require_registers(
    "tpa_region5_kernel_dtr_large_page", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE)),
        (0x30, *movl_mlx(20, KERNEL_REGION5_RR)),
        (0x40, 0x00, mov_rr_write(20, 19), nop_i(),
         nop_i()),
        (0x50, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x60, 0x00, adds(7, KERNEL_TR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x90, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(2, HIGH_TR_BASE + 0x12c0000)),
        (0xc0, *movl_mlx(21, IA64_PSR_DT)),
        (0xd0, 0x00, mov_gr_psr_full(21), nop_i(),
         nop_i()),
        (0xe0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xf0, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x100, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
    ], {
        "ip": 0x100,
        "exception": IA64_EXCP_NONE,
        "r31": 0x52c0000,
    }, entry=0x10)

test_tpa_dt_disabled_miss_raises_alt_dtlb = require_registers(
    "tpa_dt_disabled_miss_raises_alt_dtlb", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x90000)),
        (0x20, *movl_mlx(19, 1 << 13)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x90000,
        "r31": IA64_ISR_NA,
    }, entry=0x10)

test_tpa_uses_short_vhpt_walk = require_registers(
    "tpa_uses_short_vhpt_walk", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x00, tak(30, 2), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
        "r30": 5,
    }, entry=0x10)

test_short_vhpt_walker_rejects_pending_table_purge = require_registers(
    "short_vhpt_walker_rejects_pending_table_purge", [
        (0x200, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x210, *movl_mlx(17, 0xa000000000000000)),
        (0x220, *movl_mlx(18, 0x539)),
        (0x230, *movl_mlx(19, 0xbffc000000000000)),
        (0x240, *movl_mlx(20, 0x0010000004009661)),
        (0x250, *movl_mlx(21, 0x0010000004000661)),
        (0x260, *movl_mlx(22, 0x4008000)),
        (0x270, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x280, *movl_mlx(23, 0x4000430)),
        (0x290, *movl_mlx(24, 0x1122334455667788)),
        (0x2a0, 0x00, st8(23, 24), nop_i(),
         nop_i()),
        (0x2b0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0x2c0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x2d0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x2e0, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x2f0, 0x00, itc_d(20), nop_i(),
         nop_i()),
        (0x300, *movl_mlx(2, 0xa000000000000430)),
        (0x310, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x320, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x330, 0x00, ptr_d(19, 7), nop_i(),
         nop_i()),
        (0x340, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (0x350, 0x10, nop_m(), adds(28, 0x66, 0),
         br_cond(0x350, 0x350)),
        (0x000, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (0x010, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (0x020, 0x10, nop_m(), nop_i(),
         br_cond(0x020, 0x020)),
    ], {
        "ip": 0x020,
        "exception": IA64_EXCP_NONE,
        "r28": 0,
        "r29": 0,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x200)


def _short_vhpt_dbr_setup(target_pte):
    return [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, target_pte)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0), nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(), nop_i()),
        (0xe0, 0x00, nop_m(), adds(23, 0, 0), nop_i()),
        (0xf0, *movl_mlx(24, 0xbffc000000000000)),
        (0x100, 0x00, mov_dbr_indexed_write(23, 24), nop_i(), nop_i()),
        (0x110, 0x00, nop_m(), adds(23, 1, 0), nop_i()),
        (0x120, *movl_mlx(24, 0x81ffffffffffffff)),
        (0x130, 0x00, mov_dbr_indexed_write(23, 24), nop_i(), nop_i()),
        (0x140, *movl_mlx(2, 0xa000000000000430)),
    ]


# SDM Vol. 2, 7.1.1: DBR.r applies to the walker's PL0 read of a short
# (8-byte) VHPT entry, but aborts to the original Data TLB Miss rather than
# reporting Data Debug.
test_short_vhpt_dbr_read_match_aborts_to_dtlb_miss = require_registers(
    "short_vhpt_dbr_read_match_aborts_to_dtlb_miss",
    _short_vhpt_dbr_setup(0x0010000004000661) + [
        (0x150, *movl_mlx(
            24, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_DB)),
        (0x160, 0x00, mov_gr_psr_full(24), nop_i(), nop_i()),
        (0x170, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x180, 0x00, ld8(28, 2), nop_i(), nop_i()),
        (0x190, 0x10, nop_m(), nop_i(), br_cond(0x190, 0x190)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(29, 17), nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x30,
                 IA64_DTLB_VECTOR + 0x30)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r29": IA64_ISR_R,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x10)


# The DBR match belongs to the VHPT memory reference and therefore aborts the
# walk before a non-present target PTE can become a Page Not Present fault.
test_short_vhpt_dbr_read_match_precedes_not_present = require_registers(
    "short_vhpt_dbr_read_match_precedes_not_present",
    _short_vhpt_dbr_setup(0x0010000004000660) + [
        (0x150, *movl_mlx(
            24, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_DB)),
        (0x160, 0x00, mov_gr_psr_full(24), nop_i(), nop_i()),
        (0x170, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x180, 0x00, ld8(28, 2), nop_i(), nop_i()),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(29, 17), nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x30,
                 IA64_DTLB_VECTOR + 0x30)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r29": IA64_ISR_R,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x10)


# tak is the explicit exception to the VHPT DBR.r rule.  With the same
# breakpoint active, it must consume the short entry and return its key.
test_tak_vhpt_access_ignores_dbr_read_match = require_registers(
    "tak_vhpt_access_ignores_dbr_read_match",
    _short_vhpt_dbr_setup(0x0010000004000661) + [
        (0x150, *movl_mlx(24, IA64_PSR_DT | IA64_PSR_DB)),
        (0x160, 0x00, mov_gr_psr_full(24), nop_i(), nop_i()),
        (0x170, 0x00, tak(31, 2), nop_i(), nop_i()),
        (0x180, 0x10, nop_m(), nop_i(), br_cond(0x180, 0x180)),
    ], {
        "ip": 0x180,
        "exception": IA64_EXCP_NONE,
        "r31": 5,
    }, entry=0x10)

test_short_vhpt_walk_uses_dcr_byte_order = require_registers(
    "short_vhpt_walk_uses_dcr_byte_order", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x6106000400001000)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, *movl_mlx(23, IA64_DCR_BE)),
        (0x90, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(23, 0), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xc0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xd0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xf0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0x100, *movl_mlx(2, 0xa000000000000430)),
        (0x110, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x120, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
    }, entry=0x10)

test_short_vhpt_reserved_pte_aborts_to_dtlb_miss = require_registers(
    "short_vhpt_reserved_pte_aborts_to_dtlb_miss", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000663)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x20,
                 IA64_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x10)

test_short_vhpt_walker_ignores_uncacheable_mapping = require_registers(
    "short_vhpt_walker_ignores_uncacheable_mapping", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009671)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x20,
                 IA64_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x10)

test_tak_uses_short_vhpt_walk = require_registers(
    "tak_uses_short_vhpt_walk", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x00, tak(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": 5,
    }, entry=0x10)

# TAK returns 1 for an unimplemented virtual address before consulting the
# DTLB or VHPT.  Bit 51 is above Montecito's implemented VA payload and would
# otherwise alias the populated short-VHPT entry for 0xa000000000000430.
test_tak_unimplemented_va_does_not_alias_short_vhpt = require_registers(
    "tak_unimplemented_va_does_not_alias_short_vhpt", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0), nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(), nop_i()),
        (0xe0, *movl_mlx(2, 0xa008000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(), nop_i()),
        (0x100, 0x00, tak(31, 2), nop_i(), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": 1,
    }, entry=0x10, cpu="montecito")

test_short_vhpt_not_present_raises_page_fault = require_registers(
    "short_vhpt_not_present_raises_page_fault", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000660)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_R,
    }, entry=0x10)

def test_short_vhpt_not_present_entry_is_cached(qemu):
    result = run_program(qemu, [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000660)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], entry=0x10, expected={
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_R,
    }, name="short_vhpt_not_present_entry_is_cached")
    missing = []
    if not re.search(
        r"DTLB\[[0-9]+\] TC va=0xa000000000000000 .*"
        r"rid=0x000005 .* perm=0x0",
        result.register_output,
    ):
        missing.append("not-present VHPT entry was not installed as a data TC")
    if missing:
        raise RuntimeError(
            "short_vhpt_not_present_entry_is_cached failed: "
            f"{', '.join(missing)}\n{result.register_output}")

test_short_vhpt_entry_not_present_aborts_to_dtlb_miss = require_registers(
    "short_vhpt_entry_not_present_aborts_to_dtlb_miss", [
        (0x000, 0x10, nop_m(), adds(28, 0x55, 0),
         br_cond(0x000, 0x1f0)),
        (0x1f0, 0x10, nop_m(), nop_i(),
         br_cond(0x1f0, 0x1f0)),
        (0x200, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x210, *movl_mlx(17, 0xa000000000000000)),
        (0x220, *movl_mlx(18, 0x539)),
        (0x230, *movl_mlx(19, 0xbffc000000000000)),
        (0x240, *movl_mlx(20, 0x0010000004009660)),
        (0x250, *movl_mlx(2, 0xa000000000000430)),
        (0x260, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0x270, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x280, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x290, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x2a0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0x2b0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x2c0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x2d0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x00, mov_m_cr_gr(29, 17),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x30,
                 IA64_DTLB_VECTOR + 0x30)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r29": IA64_ISR_R,
        "r30": 0xa000000000000430,
        "r31": 0xbffc000000000000,
    }, entry=0x200)

test_ssm_ic_inflight_short_vhpt_entry_miss_raises_vhpt = require_registers(
    "ssm_ic_inflight_short_vhpt_entry_miss_raises_vhpt", [
        (0x000, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (0x010, 0x00, mov_m_cr_gr(31, 17), nop_i(),
         nop_i()),
        (0x020, 0x00, mov_m_cr_gr(28, 25), nop_i(),
         nop_i()),
        (0x030, 0x10, nop_m(), nop_i(),
         br_cond(0x030, 0x030)),
        (0x200, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x210, *movl_mlx(17, 0xa000000000000000)),
        (0x220, *movl_mlx(18, 0x539)),
        (0x230, *movl_mlx(2, 0xa000000000000430)),
        (0x240, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x250, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x260, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x270, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x280, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x290, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR, 0x10, nop_m(), adds(29, 0x55, 0),
         br_cond(IA64_DTLB_VECTOR, IA64_DTLB_VECTOR)),
    ], {
        "ip": 0x030,
        "exception": IA64_EXCP_NONE,
        "r29": 0,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_R | IA64_ISR_NI,
        "r28": 0xbffc000000000000,
    }, entry=0x200)

test_probe_fault_short_vhpt_not_present_raises_page_fault = require_registers(
    "probe_fault_short_vhpt_not_present_raises_page_fault", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000660)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x08, probe_w_fault(2, 3), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_NA | IA64_ISR_W | 5,
    }, entry=0x10)

test_short_vhpt_walker_reads_table_at_pl0 = require_registers(
    "short_vhpt_walker_reads_table_at_pl0", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x00100000040007e1)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xa000000000000430)),
        (0xf0, 0x00, nop_m(), mov_br_gr(7, 2),
         nop_i()),
        (0x100, *movl_mlx(19, (1 << 13) | (1 << 17) |
                          (1 << 36) | (3 << 32))),
        *rfi_to_gr(0x110, 19, 2),
        (0x4000430, 0x10, nop_m(), adds(31, 0x73, 0),
         br_cond(0x4000430, 0x4000440)),
        (0x4000440, 0x10, nop_m(), nop_i(),
         br_cond(0x4000440, 0x4000440)),
    ], {
        "ip": 0xa000000000000440,
        "exception": IA64_EXCP_NONE,
        "r31": 0x73,
    }, entry=0x10)


# A DBR.r hit on the walker's PL0 read applies equally to an instruction
# translation.  It aborts the walk to Instruction TLB Miss and reports the
# original instruction address, not the VHPT entry, in IFA.
test_short_vhpt_dbr_read_match_aborts_to_itlb_miss = require_registers(
    "short_vhpt_dbr_read_match_aborts_to_itlb_miss", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x00100000050007e1)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, *movl_mlx(23, LOW_VECTOR_TR_PTE)),
        (0x90, 0x00, st8(22, 21), nop_i(), nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0), nop_i()),
        (0xb0, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
        (0xd0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
        (0xe0, 0x00, itr_d(5, 20), nop_i(), nop_i()),
        (0xf0, 0x00, adds(7, 16 << 2, 0), adds(5, 6, 0), nop_i()),
        (0x100, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x110, 0x00, mov_m_gr_cr(0, 20), nop_i(), nop_i()),
        (0x120, 0x00, itr_i(5, 23), nop_i(), nop_i()),
        (0x130, 0x00, nop_m(), adds(24, 0, 0), nop_i()),
        (0x140, *movl_mlx(25, 0xbffc000000000000)),
        (0x150, 0x00, mov_dbr_indexed_write(24, 25), nop_i(), nop_i()),
        (0x160, 0x00, nop_m(), adds(24, 1, 0), nop_i()),
        (0x170, *movl_mlx(25, 0x81ffffffffffffff)),
        (0x180, 0x00, mov_dbr_indexed_write(24, 25), nop_i(), nop_i()),
        (0x190, *movl_mlx(2, 0xa000000000000430)),
        (0x1a0, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_IT | IA64_PSR_DB |
            IA64_PSR_CPL3)),
        (0x1b0, 0x00, srlz_i(), nop_i(), nop_i()),
        (0x1c0, 0x00, srlz_d(), nop_i(), nop_i()),
        *rfi_to_gr(0x1d0, 19, 2),
        (0x4000000 + IA64_ITLB_VECTOR, 0x00,
         mov_m_cr_gr(28, 19), nop_i(), nop_i()),
        (0x4000000 + IA64_ITLB_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(29, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_ITLB_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(30, 25), nop_i(), nop_i()),
        (0x4000000 + IA64_ITLB_VECTOR + 0x30, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_ITLB_VECTOR + 0x40, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_ITLB_VECTOR + 0x40, IA64_ITLB_VECTOR + 0x40)),
    ], {
        "ip": IA64_ITLB_VECTOR + 0x40,
        "exception": IA64_EXCP_NONE,
        "r28": 0xa000000000000430,
        "r29": 0xa000000000000430,
        "r30": 0xbffc000000000000,
        "r31": IA64_ISR_X,
    }, entry=0x10)

test_short_vhpt_ifetch_read_only_raises_inst_access = require_registers(
    "short_vhpt_ifetch_read_only_raises_inst_access", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x00100000040001e1)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, *movl_mlx(23, LOW_VECTOR_TR_PTE)),
        (0x90, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xd0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xe0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xf0, 0x00, adds(7, 16 << 2, 0), nop_i(),
         nop_i()),
        (0x100, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x110, 0x00, mov_m_gr_cr(0, 20), adds(5, 6, 0),
         nop_i()),
        (0x120, 0x00, itr_i(5, 23), nop_i(),
         nop_i()),
        (0x130, *movl_mlx(2, 0xa000000000000430)),
        (0x140, *movl_mlx(19, (1 << 13) | (1 << 17) |
                          (1 << 36) | (3 << 32))),
        (0x150, 0x00, nop_m(), mov_br_gr(7, 2),
         nop_i()),
        *rfi_to_gr(0x160, 19, 2),
        (0x4000000 + IA64_INST_ACCESS_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_ACCESS_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_ACCESS_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_INST_ACCESS_VECTOR + 0x20,
                 IA64_INST_ACCESS_VECTOR + 0x20)),
    ], {
        "ip": IA64_INST_ACCESS_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_X,
    }, entry=0x10)

test_itr_i_clear_accessed_raises_inst_access_bit = require_registers(
    "itr_i_clear_accessed_raises_inst_access_bit", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(23, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x30, *movl_mlx(2, 0xa000000000000430)),
        (0x40, *movl_mlx(24, KERNEL_REGION5_RR)),
        (0x50, *movl_mlx(19, (1 << 13) | (1 << 36))),
        (0x60, 0x00, mov_rr_write(24, 2), nop_i(),
         nop_i()),
        (0x70, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(2, 20), adds(5, 6, 0),
         nop_i()),
        (0xc0, 0x00, itr_i(5, 23), nop_i(),
         nop_i()),
        (0xd0, 0x00, nop_m(), mov_br_gr(7, 2),
         nop_i()),
        *rfi_to_gr(0xe0, 19, 2),
        (0x4000000 + IA64_INST_ACCESS_BIT_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_ACCESS_BIT_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (0x4000000 + IA64_INST_ACCESS_BIT_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_INST_ACCESS_BIT_VECTOR + 0x20,
                 IA64_INST_ACCESS_BIT_VECTOR + 0x20)),
        (0x4000430, 0x10, nop_m(), adds(31, 0x55, 0),
         br_cond(0x4000430, 0x4000430)),
    ], {
        "ip": IA64_INST_ACCESS_BIT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0xa000000000000430,
        "r31": IA64_ISR_X,
    }, entry=0x10)

test_ifetch_page_not_present_after_branch_restarts_slot0 = require_registers(
    "ifetch_page_not_present_after_branch_restarts_slot0", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000660)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, *movl_mlx(23, LOW_VECTOR_TR_PTE)),
        (0x90, *movl_mlx(24, 0x00100000040007e1)),
        (0xa0, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xc0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xd0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xf0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0x100, 0x00, adds(7, 16 << 2, 0), nop_i(),
         nop_i()),
        (0x110, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x120, 0x00, mov_m_gr_cr(0, 20), adds(5, 6, 0),
         nop_i()),
        (0x130, 0x00, itr_i(5, 23), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(2, 0xa000000000000430)),
        (0x150, *movl_mlx(19, (1 << 13) | (1 << 17) |
                          (1 << 36) | (3 << 32))),
        (0x160, 0x00, nop_m(), mov_br_gr(7, 2),
         nop_i()),
        *rfi_to_gr(0x170, 19, 2),
        (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR, 0x00,
         mov_m_cr_gr(25, 25), nop_i(), nop_i()),
        (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00,
         st8(25, 24), nop_i(), nop_i()),
        (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x00,
         itc_i(24), nop_i(), nop_i()),
        (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x30, 0x10,
         srlz_i(), nop_i(), rfi_b()),
        (0x4000430, 0x10, alloc_m(2, 8, 5, 0, 0), nop_i(),
         br_cond(0xa000000000000430, 0xa000000000000440)),
        (0x4000440, 0x10, nop_m(), nop_i(),
         br_cond(0xa000000000000440, 0xa000000000000440)),
    ], {
        "ip": 0xa000000000000440,
        "exception": IA64_EXCP_NONE,
        "cfm_sof": 8,
        "cfm_sol": 5,
    }, entry=0x10)

IFETCH_PNP_BASE = 0xa000000000000000
IFETCH_PNP_NEXT_PAGE = IFETCH_PNP_BASE + 0x2000
IFETCH_PNP_TARGET = IFETCH_PNP_BASE + 0x1ff0
IFETCH_PNP_CODE_PTE = 0x0010000004002660

test_ifetch_page_not_present_fallthrough_records_faulting_iip = \
    require_registers(
        "ifetch_page_not_present_fallthrough_records_faulting_iip", [
            (0x10, *movl_mlx(16, IFETCH_PNP_BASE)),
            (0x20, *movl_mlx(17, (5 << 8) | EIGHT_K_ITIR | 1)),
            (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
            (0x40, *movl_mlx(19, LOW_VECTOR_TR_PTE)),
            (0x50, *movl_mlx(20, IFETCH_PNP_CODE_PTE)),
            (0x60, *movl_mlx(21, IFETCH_PNP_BASE)),
            (0x70, *movl_mlx(22, IFETCH_PNP_NEXT_PAGE)),
            (0x80, *movl_mlx(23, IFETCH_PNP_TARGET)),
            (0x90, *movl_mlx(24, IA64_PSR_IC | IA64_PSR_IT)),
            (0xa0, 0x00, mov_rr_write(17, 16), adds(7, 16 << 2, 0),
             adds(5, 5, 0)),
            (0xb0, 0x00, mov_m_gr_cr(0, 20), nop_i(), nop_i()),
            (0xc0, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (0xd0, 0x00, itr_i(5, 18), nop_i(), nop_i()),
            (0xe0, 0x00, mov_m_gr_cr(21, 20), adds(7, 13 << 2, 0),
             adds(5, 6, 0)),
            (0xf0, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (0x100, 0x00, itr_i(5, 19), nop_i(), nop_i()),
            (0x110, 0x00, mov_m_gr_cr(22, 20), adds(7, 13 << 2, 0),
             adds(5, 7, 0)),
            (0x120, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (0x130, 0x00, itr_i(5, 20), adds(25, 0x150, 0), nop_i()),
            *rfi_to_gr(0x140, 24, 25),
            (0x4000150, 0x00, srlz_i(), nop_i(), nop_i()),
            (0x4000160, 0x00, nop_m(), mov_br_gr(7, 23), nop_i()),
            (0x4000170, 0x10, nop_m(), nop_i(), br_call_indirect(6, 7)),
            (0x4001ff0, 0x00, nop_m(), adds(28, 0x5a, 0), nop_i()),
            (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR, 0x00,
             mov_m_cr_gr(30, 19), nop_i(), nop_i()),
            (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00,
             mov_m_cr_gr(31, 20), nop_i(), nop_i()),
            (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x00,
             mov_m_cr_gr(29, 17), nop_i(), nop_i()),
            (0x4000000 + IA64_PAGE_NOT_PRESENT_VECTOR + 0x30, 0x10,
             nop_m(), nop_i(),
             br_cond(IA64_PAGE_NOT_PRESENT_VECTOR + 0x30,
                     IA64_PAGE_NOT_PRESENT_VECTOR + 0x30)),
        ], {
            "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x30,
            "exception": IA64_EXCP_NONE,
            "r28": 0x5a,
            "r29": IA64_ISR_X,
            "r30": IFETCH_PNP_NEXT_PAGE,
            "r31": IFETCH_PNP_NEXT_PAGE,
        }, entry=0x10)

test_speculative_load_walks_short_vhpt_with_ic_clear = require_registers(
    "speculative_load_walks_short_vhpt_with_ic_clear", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xbffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(23, 0x4000430)),
        (0xa0, *movl_mlx(24, 0x123456789abcdef0)),
        (0xb0, 0x00, st8(23, 24), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xd0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xf0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0x100, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0x110, *movl_mlx(2, 0xa000000000000430)),
        (0x120, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x130, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x140, 0x00, tak(30, 2), nop_i(),
         nop_i()),
        (0x150, 0x00, nop_m(), nop_i(), nop_i()),
        (0x160, 0x00, nop_m(), nop_i(), nop_i()),
        (0x170, 0x10, nop_m(), nop_i(),
         br_cond(0x170, 0x170)),
    ], {
        "ip": 0x170,
        "exception": IA64_EXCP_NONE,
        "r31": 0x123456789abcdef0,
        "r31_nat": 0,
        "r30": 5,
    }, entry=0x10)

test_speculative_load_defers_region6_vhpt_not_present = require_registers(
    "speculative_load_defers_region6_vhpt_not_present", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xc000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xdffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000660)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xc000000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x00, nop_m(), nop_i(), nop_i()),
        (0x120, 0x00, nop_m(), nop_i(), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r31_nat": 1,
    }, entry=0x10)

test_region6_short_vhpt_controls_data_mapping = require_registers(
    "region6_short_vhpt_controls_data_mapping", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xc000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xdffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xc000000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x08, ld8(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
        (0x430, 0x00, 0xfedcba9876543210, 0,
         0),
        (0x4000430, 0x00, 0x123456789a, 0,
         0),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": bundle_words(0x00, 0x123456789a, 0, 0)[0],
    }, entry=0x10)

test_region6_tpa_uses_short_vhpt_mapping = require_registers(
    "region6_tpa_uses_short_vhpt_mapping", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xc000000000000000)),
        (0x30, *movl_mlx(18, 0x539)),
        (0x40, *movl_mlx(19, 0xdffc000000000000)),
        (0x50, *movl_mlx(20, 0x0010000004009661)),
        (0x60, *movl_mlx(21, 0x0010000004000661)),
        (0x70, *movl_mlx(22, 0x4008000)),
        (0x80, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(16, 8), adds(7, 0x38, 0),
         nop_i()),
        (0xa0, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0),
         nop_i()),
        (0xd0, 0x00, itr_d(5, 20), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0xc000000000000430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
    }, entry=0x10)

test_translation_hash_m_unit_decode = require_registers(
    "translation_hash_m_unit_decode", [
        (0x10, *movl_mlx(16, 0x12345000)),
        (0x20, 0x00, tak(17, 16), nop_i(),
         nop_i()),
        (0x30, 0x00, thash(18, 16), nop_i(),
         nop_i()),
        (0x40, 0x00, ttag(19, 16), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "r17": 1,
        "r18": 0x12345000,
        "r19": 0x12345,
    }, entry=0x10)

test_translation_hash_merced_long_format = require_registers(
    "translation_hash_merced_long_format", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x12345000)),
        (0x30, *movl_mlx(18, (0x135 << 8) | (12 << 2) | 1)),
        (0x40, 0x00, mov_m_gr_cr(16, 8), nop_i(), nop_i()),
        (0x50, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0x60, 0x00, thash(20, 17), nop_i(), nop_i()),
        (0x70, 0x00, ttag(21, 17), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r20": 0x104e00,
        "r21": 0x9a8000012345,
    }, entry=0x10, cpu="merced")

test_translation_hash_m46_ignored_bits_decode = require_registers(
    "translation_hash_m46_ignored_bits_decode", [
        (0x10, *movl_mlx(16, 0x12345000)),
        (0x20, 0x00, tak(17, 16, bit36=1, ignored=0x0b), nop_i(),
         nop_i()),
        (0x30, 0x00, thash(18, 16, bit36=1, ignored=0x01), nop_i(),
         nop_i()),
        (0x40, 0x00, ttag(19, 16, bit36=1, ignored=0x01), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "r17": 1,
        "r18": 0x12345000,
        "r19": 0x12345,
    }, entry=0x10)

test_translation_hash_ops_clear_dest_nat = require_registers(
    "translation_hash_ops_clear_dest_nat", [
        (0x10, *movl_mlx(19, IA64_PSR_ED)),
        (0x20, *movl_mlx(17, 0x12345000)),
        (0x30, *movl_mlx(31, 0x60)),
        *rfi_to_gr(0x40, 19, 31),
        (0x60, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, ld8_s(20, 3), nop_i(),
         nop_i()),
        (0x80, 0x00, ld8_s(21, 3), nop_i(),
         nop_i()),
        (0x90, 0x00, ld8_s(22, 3), nop_i(),
         nop_i()),
        (0xa0, 0x00, tak(20, 17), nop_i(),
         nop_i()),
        (0xb0, 0x00, thash(21, 17), nop_i(),
         nop_i()),
        (0xc0, 0x00, ttag(22, 17), nop_i(),
         nop_i()),
        (0xd0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xe0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xf0, 0x00, nop_m(), nop_i(), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
    ], {
        "ip": 0x100,
        "exception": IA64_EXCP_NONE,
        "r20_nat": 0,
        "r21_nat": 0,
        "r22_nat": 0,
    }, entry=0x10)

test_translation_hash_nat_source_rules = require_registers(
    "translation_hash_nat_source_rules", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, thash(17, 16), nop_i(),
         nop_i()),
        (0x40, 0x00, ttag(18, 16), nop_i(),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x00, nop_m(), nop_i(), nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r17_nat": 1,
        "r18_nat": 1,
    }, entry=0x10)

test_translation_hash_unimplemented_va_same_register_sets_nat = \
    require_registers(
        "translation_hash_unimplemented_va_same_register_sets_nat", [
            (0x10, *movl_mlx(17, 0xa008000000000430)),
            (0x20, *movl_mlx(18, 0xa008000000000430)),
            (0x30, 0x00, thash(17, 17), nop_i(), nop_i()),
            (0x40, 0x00, ttag(18, 18), nop_i(), nop_i()),
            (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
        ], {
            "ip": 0x50,
            "exception": IA64_EXCP_NONE,
            "r17_nat": 1,
            "r18_nat": 1,
        }, entry=0x10, cpu="merced")

test_tak_nat_source_consumes_non_access = require_registers(
    "tak_nat_source_consumes_non_access", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, tak(17, 16), nop_i(),
         nop_i()),
        (0x5600, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5610, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5620, 0x10, nop_m(), nop_i(),
         br_cond(0x5620, 0x5620)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x5620,
        "exception": IA64_EXCP_NONE,
        "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_NA | 3,
    }, entry=0x10)

test_tpa_nat_source_consumes_non_access = require_registers(
    "tpa_nat_source_consumes_non_access", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, tpa(17, 16), nop_i(),
         nop_i()),
        (0x5600, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5610, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5620, 0x10, nop_m(), nop_i(),
         br_cond(0x5620, 0x5620)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x5620,
        "exception": IA64_EXCP_NONE,
        "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_NA,
    }, entry=0x10)


def translation_vm_expected_fault(name, bundles, exception, fault_ip,
                                  cpu="montecito"):
    vector = {
        IA64_EXCP_ILLEGAL: IA64_GENERAL_VECTOR,
        IA64_EXCP_NAT_CONSUMPTION: IA64_NAT_CONSUMPTION_VECTOR,
        IA64_EXCP_PRIVILEGED_OP: IA64_GENERAL_VECTOR,
        IA64_EXCP_RESERVED_REG_FIELD: IA64_GENERAL_VECTOR,
        IA64_EXCP_UNIMPL_DATA_ADDR: IA64_GENERAL_VECTOR,
        IA64_EXCP_VIRTUALIZATION: IA64_VIRTUALIZATION_VECTOR,
    }[exception]
    return require_registers(name, [
        *bundles,
        (vector, 0x00, nop_m(), nop_i(), nop_i()),
        (vector + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(vector + 0x10, vector + 0x10)),
    ], {
        "ip": vector + 0x10,
        "exception": IA64_EXCP_NONE,
        "fault_code": exception,
        "fault_ip": fault_ip,
    }, entry=0x10, cpu=cpu)


def system_vm_fault_test(name, instruction, expected_exception,
                         psr=IA64_PSR_IC | IA64_PSR_VM, setup=(),
                         data=(), b_slot=False, cpu="montecito",
                         instruction_template=0x00, uncollected=False):
    bundles = []
    address = 0x10

    for template_and_slots in setup:
        bundles.append((address, *template_and_slots))
        address += 0x10
    bundles.append((address, *movl_mlx(19, psr)))
    address += 0x10
    target = address + 0x30
    bundles.append((address, *movl_mlx(31, target)))
    address += 0x10
    bundles.extend(rfi_to_gr(address, 19, 31))
    fault_ip = address + 0x20
    if b_slot:
        bundles.append((fault_ip, 0x10, nop_m(), nop_i(), instruction))
    else:
        bundles.append((fault_ip, instruction_template,
                        instruction, nop_i(), nop_i()))
    bundles.extend(data)
    if uncollected:
        return require_registers(name, bundles, {
            "ip": fault_ip,
            "exception": expected_exception,
            "fault_code": expected_exception,
            "fault_ip": fault_ip,
        }, entry=0x10, cpu=cpu)
    return translation_vm_expected_fault(
        name, bundles, expected_exception, fault_ip=fault_ip, cpu=cpu)


def translation_vm_fault_test(name, instruction):
    return translation_vm_expected_fault(name, [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, instruction, nop_i(), nop_i()),
    ], IA64_EXCP_VIRTUALIZATION, fault_ip=0x50)


test_tak_vm_virtualization_fault = translation_vm_fault_test(
    "tak_vm_virtualization_fault", tak(17, 16))

test_thash_vm_virtualization_fault = translation_vm_fault_test(
    "thash_vm_virtualization_fault", thash(17, 16))

test_tpa_vm_virtualization_fault = translation_vm_fault_test(
    "tpa_vm_virtualization_fault", tpa(17, 16))

test_ttag_vm_virtualization_fault = translation_vm_fault_test(
    "ttag_vm_virtualization_fault", ttag(17, 16))


def translation_cpl_outranks_vm_test(name, instruction):
    return translation_vm_expected_fault(name, [
        (0x10, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, instruction, nop_i(), nop_i()),
    ], IA64_EXCP_PRIVILEGED_OP, fault_ip=0x50)


test_tak_cpl_outranks_vm = translation_cpl_outranks_vm_test(
    "tak_cpl_outranks_vm", tak(17, 16))

test_tpa_cpl_outranks_vm = translation_cpl_outranks_vm_test(
    "tpa_cpl_outranks_vm", tpa(17, 16))


def translation_nat_vm_priority_test(name, instruction, expected_exception):
    return translation_vm_expected_fault(name, [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
        (0x30, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x40, *movl_mlx(31, 0x70)),
        *rfi_to_gr(0x50, 19, 31),
        (0x70, 0x00, instruction, nop_i(), nop_i()),
        (0x200, 0x00, 0, 0, 0),
    ], expected_exception, fault_ip=0x70)


test_tak_nat_outranks_vm = translation_nat_vm_priority_test(
    "tak_nat_outranks_vm", tak(17, 16), IA64_EXCP_NAT_CONSUMPTION)

test_tpa_nat_outranks_vm = translation_nat_vm_priority_test(
    "tpa_nat_outranks_vm", tpa(17, 16), IA64_EXCP_NAT_CONSUMPTION)

test_thash_vm_outranks_nat = translation_nat_vm_priority_test(
    "thash_vm_outranks_nat", thash(17, 16), IA64_EXCP_VIRTUALIZATION)

test_ttag_vm_outranks_nat = translation_nat_vm_priority_test(
    "ttag_vm_outranks_nat", ttag(17, 16), IA64_EXCP_VIRTUALIZATION)


test_translation_vm_predicated_off_is_nop = require_registers(
    "translation_vm_predicated_off_is_nop", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, tak(17, 16, qp=1), nop_i(), nop_i()),
        (0x60, 0x00, thash(18, 16, qp=1), nop_i(), nop_i()),
        (0x70, 0x00, tpa(19, 16, qp=1), nop_i(), nop_i()),
        (0x80, 0x00, ttag(20, 16, qp=1), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r17": 0,
        "r18": 0,
        "r19": IA64_PSR_IC | IA64_PSR_VM,
        "r20": 0,
    }, entry=0x10, cpu="montecito")


test_bsw_vm_virtualization_fault = system_vm_fault_test(
    "bsw_vm_virtualization_fault", bsw1(), IA64_EXCP_VIRTUALIZATION,
    b_slot=True)

test_cover_cpl0_vm_virtualization_fault = system_vm_fault_test(
    "cover_cpl0_vm_virtualization_fault", cover_b(),
    IA64_EXCP_VIRTUALIZATION, b_slot=True)

test_rfi_vm_virtualization_fault = system_vm_fault_test(
    "rfi_vm_virtualization_fault", rfi_b(), IA64_EXCP_VIRTUALIZATION,
    b_slot=True)

test_mov_psr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_psr_read_vm_virtualization_fault", mov_m_psr_gr(17),
    IA64_EXCP_VIRTUALIZATION)

test_mov_psrl_write_vm_virtualization_fault = system_vm_fault_test(
    "mov_psrl_write_vm_virtualization_fault", mov_m_gr_psrl(16),
    IA64_EXCP_VIRTUALIZATION)

test_ssm_vm_virtualization_fault = system_vm_fault_test(
    "ssm_vm_virtualization_fault", ssm(1 << 5),
    IA64_EXCP_VIRTUALIZATION)

test_rsm_vm_virtualization_fault = system_vm_fault_test(
    "rsm_vm_virtualization_fault", rsm(1 << 5),
    IA64_EXCP_VIRTUALIZATION)

test_mov_cr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_cr_read_vm_virtualization_fault", mov_m_cr_gr(17, 8),
    IA64_EXCP_VIRTUALIZATION)

test_mov_itc_read_si_vm_virtualization_fault = system_vm_fault_test(
    "mov_itc_read_si_vm_virtualization_fault", mov_m_ar_gr(17, 44),
    IA64_EXCP_VIRTUALIZATION,
    psr=IA64_PSR_IC | IA64_PSR_VM | (1 << 23),
    instruction_template=0x02)

test_mov_itc_read_without_si_vm_allowed = require_registers(
    "mov_itc_read_without_si_vm_allowed", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, mov_m_ar_gr(17, 44), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "exception": IA64_EXCP_NONE}, entry=0x10,
    cpu="montecito")

test_mov_itc_write_vm_virtualization_fault = system_vm_fault_test(
    "mov_itc_write_vm_virtualization_fault", mov_m_gr_ar(16, 44),
    IA64_EXCP_VIRTUALIZATION)

test_mov_cr_ic_outranks_cpl_vm = system_vm_fault_test(
    "mov_cr_ic_outranks_cpl_vm", mov_m_cr_gr(17, 16), IA64_EXCP_ILLEGAL,
    psr=IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)

test_mov_cr_cpl_outranks_reserved_vm = system_vm_fault_test(
    "mov_cr_cpl_outranks_reserved_vm", mov_m_gr_cr(16, 8),
    IA64_EXCP_PRIVILEGED_OP,
    psr=IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)

test_mov_cr_reserved_outranks_vm = system_vm_fault_test(
    "mov_cr_reserved_outranks_vm", mov_m_gr_cr(16, 8),
    IA64_EXCP_RESERVED_REG_FIELD)

test_mov_cr_nat_outranks_reserved_vm = system_vm_fault_test(
    "mov_cr_nat_outranks_reserved_vm", mov_m_gr_cr(16, 8),
    IA64_EXCP_NAT_CONSUMPTION, setup=(
        (0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
    ), data=((0x200, 0x00, 0, 0, 0),))

test_mov_cr_ifa_uda_merced = system_vm_fault_test(
    "mov_cr_ifa_uda_merced", mov_m_gr_cr(16, 20),
    IA64_EXCP_UNIMPL_DATA_ADDR, psr=0, cpu="merced",
    setup=(movl_mlx(16, 1 << 51),))

test_mov_pkr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_pkr_read_vm_virtualization_fault", mov_pkr_indexed_read(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_pkr_index_outranks_vm = system_vm_fault_test(
    "mov_pkr_index_outranks_vm", mov_pkr_indexed_read(17, 16),
    IA64_EXCP_RESERVED_REG_FIELD,
    setup=(movl_mlx(16, IA64_PKR_COUNT),))

test_mov_rr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_rr_read_vm_virtualization_fault", mov_rr_read(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_ibr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_ibr_read_vm_virtualization_fault", mov_ibr_indexed_read(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_dbr_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_dbr_read_vm_virtualization_fault", mov_dbr_indexed_read(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_pmc_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_pmc_read_vm_virtualization_fault", mov_pmcgr_indexed(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_cpuid_read_vm_virtualization_fault = system_vm_fault_test(
    "mov_cpuid_read_vm_virtualization_fault", mov_cpuid(17, 16),
    IA64_EXCP_VIRTUALIZATION)

test_mov_pmd_write_vm_virtualization_fault = system_vm_fault_test(
    "mov_pmd_write_vm_virtualization_fault", mov_grpmd_indexed(16, 17),
    IA64_EXCP_VIRTUALIZATION)

test_ptc_e_vm_virtualization_fault = system_vm_fault_test(
    "ptc_e_vm_virtualization_fault", ptc_e(16),
    IA64_EXCP_VIRTUALIZATION)

test_ptc_l_vm_virtualization_fault = system_vm_fault_test(
    "ptc_l_vm_virtualization_fault", ptc_l(16, 17),
    IA64_EXCP_VIRTUALIZATION)

test_ptr_d_vm_virtualization_fault = system_vm_fault_test(
    "ptr_d_vm_virtualization_fault", ptr_d(16, 17),
    IA64_EXCP_VIRTUALIZATION)

test_itc_d_vm_virtualization_fault = system_vm_fault_test(
    "itc_d_vm_virtualization_fault", itc_d(16),
    IA64_EXCP_VIRTUALIZATION, psr=IA64_PSR_VM, setup=(
        movl_mlx(16, LOW_VECTOR_TR_PTE),
        (0x00, mov_m_gr_cr(0, 20), adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
    ))

test_itr_d_vm_virtualization_fault = system_vm_fault_test(
    "itr_d_vm_virtualization_fault", itr_d(5, 16),
    IA64_EXCP_VIRTUALIZATION, psr=IA64_PSR_VM, setup=(
        movl_mlx(16, LOW_VECTOR_TR_PTE),
        (0x00, mov_m_gr_cr(0, 20), adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (0x00, mov_m_gr_cr(7, 21), adds(5, 0, 0), nop_i()),
    ))

test_itc_ic_outranks_cpl_nat_reserved_vm = system_vm_fault_test(
    "itc_ic_outranks_cpl_nat_reserved_vm", itc_d(16), IA64_EXCP_ILLEGAL,
    psr=IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)

test_itc_nat_outranks_reserved_vm = system_vm_fault_test(
    "itc_nat_outranks_reserved_vm", itc_d(16),
    IA64_EXCP_NAT_CONSUMPTION, psr=IA64_PSR_VM, setup=(
        (0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
    ), data=((0x200, 0x00, 0, 0, 0),))

test_itc_reserved_outranks_vm = system_vm_fault_test(
    "itc_reserved_outranks_vm", itc_d(16),
    IA64_EXCP_RESERVED_REG_FIELD, psr=IA64_PSR_VM, uncollected=True)

test_ptc_l_uda_merced = system_vm_fault_test(
    "ptc_l_uda_merced", ptc_l(16, 17),
    IA64_EXCP_UNIMPL_DATA_ADDR, psr=IA64_PSR_IC, cpu="merced",
    setup=(movl_mlx(16, 1 << 51),))

test_ptc_e_skips_uda_on_merced = require_registers(
    "ptc_e_skips_uda_on_merced", [
        (0x10, *movl_mlx(16, 1 << 51)),
        (0x20, 0x00, ptc_e(16), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "exception": IA64_EXCP_NONE}, entry=0x10,
    cpu="merced")

test_ssm_cpl_outranks_reserved_vm = system_vm_fault_test(
    "ssm_cpl_outranks_reserved_vm", ssm(1), IA64_EXCP_PRIVILEGED_OP,
    psr=IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)

test_ssm_reserved_outranks_vm = system_vm_fault_test(
    "ssm_reserved_outranks_vm", ssm(1), IA64_EXCP_RESERVED_REG_FIELD)

test_mov_psrl_cpl_outranks_reserved_vm = system_vm_fault_test(
    "mov_psrl_cpl_outranks_reserved_vm", mov_m_gr_psrl(16),
    IA64_EXCP_PRIVILEGED_OP,
    psr=IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM,
    setup=(movl_mlx(16, 1),))

test_mov_psrl_reserved_outranks_vm = system_vm_fault_test(
    "mov_psrl_reserved_outranks_vm", mov_m_gr_psrl(16),
    IA64_EXCP_RESERVED_REG_FIELD, setup=(movl_mlx(16, 1),))

test_mov_psrl_nat_outranks_reserved_vm = system_vm_fault_test(
    "mov_psrl_nat_outranks_reserved_vm", mov_m_gr_psrl(16),
    IA64_EXCP_NAT_CONSUMPTION, setup=(
        (0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
    ), data=((0x200, 0x00, 0, 0, 0),))

test_cover_cpl3_vm_executes = require_registers(
    "cover_cpl3_vm_executes", [
        (0x10, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_CPL3 | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x10, nop_m(), nop_i(), cover_b()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "exception": IA64_EXCP_NONE}, entry=0x10,
    cpu="montecito")

test_mov_pmd_read_vm_is_exempt = require_registers(
    "mov_pmd_read_vm_is_exempt", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, mov_pmdgr_indexed(17, 16), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "exception": IA64_EXCP_NONE}, entry=0x10,
    cpu="montecito")

test_system_vm_predicated_off_is_nop = require_registers(
    "system_vm_predicated_off_is_nop", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_VM)),
        (0x20, *movl_mlx(31, 0x50)),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, ssm(1 << 5, qp=1), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_psr_gr(17, qp=1), nop_i(), nop_i()),
        (0x70, 0x00, ptc_e(16, qp=1), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r17": 0,
    }, entry=0x10, cpu="montecito")

test_mov_psr_read_masks_high_state = require_registers(
    "mov_psr_read_masks_high_state", [
        (0x10, *movl_mlx(18, DTR_PTE_WB)),
        (0x20, 0x00, mov_m_gr_cr(0, 20), adds(7, LOW_VECTOR_ITIR, 0),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(7, 21), adds(5, 0, 0), nop_i()),
        (0x40, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0x50, 0x00, srlz_i(), nop_i(), nop_i()),
        (0x60, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_MC | IA64_PSR_IT | IA64_PSR_BN)),
        (0x70, *movl_mlx(31, 0xa0)),
        *rfi_to_gr(0x80, 19, 31),
        (0xa0, 0x00, mov_m_psr_gr(17), nop_i(), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r17": IA64_PSR_IC | IA64_PSR_MC | IA64_PSR_IT,
    }, entry=0x10, cpu="montecito")

test_itc_d_nat_pte_consumes = register_nat_consumption_test(
    "itc_d_nat_pte_consumes",
    (0x00, itc_d(16), nop_i(), nop_i()),
    IA64_ISR_NI, enable_ic=False)

test_itr_d_nat_slot_consumes = register_nat_consumption_test(
    "itr_d_nat_slot_consumes",
    (0x00, itr_d(16, 17), nop_i(), nop_i()),
    IA64_ISR_NI, enable_ic=False)

test_ptc_l_nat_addr_consumes = register_nat_consumption_test(
    "ptc_l_nat_addr_consumes",
    (0x00, ptc_l(16, 17), nop_i(), nop_i()))

test_ptr_d_nat_size_consumes = register_nat_consumption_test(
    "ptr_d_nat_size_consumes",
    (0x00, ptr_d(17, 16), nop_i(), nop_i()))

test_ptc_e_nat_addr_consumes = register_nat_consumption_test(
    "ptc_e_nat_addr_consumes",
    (0x00, ptc_e(16), nop_i(), nop_i()))

test_short_vhpt_thash_decode = require_registers(
    "short_vhpt_thash_decode", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c9)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(19, 0xa0007fffff90c010)),
        (0x40, 0x00, mov_m_gr_cr(16, 8), adds(18, 0x539, 0),
         nop_i()),
        (0x50, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x60, 0x00, thash(20, 19), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r20": 0xbffc000ffffff218,
    }, entry=0x10)

test_thash_uses_pta_with_walker_disabled = require_registers(
    "thash_uses_pta_with_walker_disabled", [
        (0x10, *movl_mlx(16, 0x1ffc0000000000c8)),
        (0x20, *movl_mlx(17, 0xa000000000000000)),
        (0x30, *movl_mlx(19, 0xa0007fffff90c010)),
        (0x40, 0x00, mov_m_gr_cr(16, 8), adds(18, 0x539, 0),
         nop_i()),
        (0x50, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x60, 0x00, thash(20, 19), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r20": 0xbffc000ffffff218,
    }, entry=0x10)

test_short_vhpt_thash_uses_implemented_va_bits = require_registers(
    "short_vhpt_thash_uses_implemented_va_bits", [
        (0x10, *movl_mlx(16, 0x1ffffe00000000d1)),
        (0x20, *movl_mlx(17, 0xe000000000000000)),
        (0x30, *movl_mlx(18, 0x135)),
        (0x40, *movl_mlx(19, 0xfffffe00003fe800)),
        (0x50, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x70, 0x00, thash(20, 19), nop_i(),
         nop_i()),
        (0x80, 0x00, ttag(21, 19), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r20": 0xfff7ffff80000ff8,
        "r21": 0x1fffff00001ff,
    }, entry=0x10)

test_short_vhpt_thash_high_region_self_map = require_registers(
    "short_vhpt_thash_high_region_self_map", [
        (0x10, *movl_mlx(16, 0x1ffff000000000b1)),
        (0x20, *movl_mlx(17, 0xe000000000000000)),
        (0x30, *movl_mlx(18, 0x135)),
        (0x40, *movl_mlx(19, 0xfffffffc00000ff8)),
        (0x50, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x70, 0x00, thash(20, 19), nop_i(),
         nop_i()),
        (0x80, 0x00, ttag(21, 19), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r20": 0xffffffffff000000,
        "r21": 0x1ffffffe00000,
    }, entry=0x10)

test_long_vhpt_walk_uses_standard_entry_layout = require_registers(
    "long_vhpt_walk_uses_standard_entry_layout", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x0010000004000661)),
        (0x40, *movl_mlx(19, 0x230)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(2, 0x430)),
        (0xd0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0xe0, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0xf0, 0x00, tak(30, 2), nop_i(),
         nop_i()),
        (0x100, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
    ], {
        "ip": 0x100,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
        "r30": 2,
    }, entry=0x10)


# Long-format walkers match one atomic 32-byte reference.  DBR byte 24 is
# beyond the three architecturally consumed 64-bit fields, but remains part
# of that breakpoint datum and must abort the walk to Data TLB Miss.
test_long_vhpt_dbr_matches_full_32_byte_reference = require_registers(
    "long_vhpt_dbr_matches_full_32_byte_reference", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x0010000004000661)),
        (0x40, *movl_mlx(19, 0x230)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21), nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21), nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(), nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(), nop_i()),
        (0xb0, 0x00, mov_rr_write(17, 0), nop_i(), nop_i()),
        (0xc0, 0x00, nop_m(), adds(24, 0, 0), nop_i()),
        (0xd0, *movl_mlx(25, 0x100058)),
        (0xe0, 0x00, mov_dbr_indexed_write(24, 25), nop_i(), nop_i()),
        (0xf0, 0x00, nop_m(), adds(24, 1, 0), nop_i()),
        (0x100, *movl_mlx(25, 0x81ffffffffffffff)),
        (0x110, 0x00, mov_dbr_indexed_write(24, 25), nop_i(), nop_i()),
        (0x120, *movl_mlx(2, 0x430)),
        (0x130, *movl_mlx(
            25, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_DB)),
        (0x140, 0x00, mov_gr_psr_full(25), nop_i(), nop_i()),
        (0x150, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x160, 0x00, ld8(28, 2), nop_i(), nop_i()),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(29, 17), nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x30,
                 IA64_DTLB_VECTOR + 0x30)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r29": IA64_ISR_R,
        "r30": 0x430,
        "r31": 0x100040,
    }, entry=0x10)

test_long_vhpt_walk_uses_dcr_byte_order = require_registers(
    "long_vhpt_walk_uses_dcr_byte_order", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x6106000400001000)),
        (0x40, *movl_mlx(19, 0x3002000000000000)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG_BYTE_SWAPPED)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, *movl_mlx(22, IA64_DCR_BE)),
        (0x80, 0x00, st8(21, 18), adds(23, 8, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 19), adds(24, 16, 21),
         nop_i()),
        (0xa0, 0x00, st8(24, 20), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(22, 0), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xd0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(2, 0x430)),
        (0xf0, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x100, 0x00, tpa(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x00, tak(30, 2), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r31": 0x4000430,
        "r30": 2,
    }, entry=0x10)

LONG_VHPT_RID1_DATA_BUNDLE = (0x4000430, 0x00, 0x1111222233334444, 0, 0)
LONG_VHPT_RID2_DATA_BUNDLE = (0x4010430, 0x00, 0x5555666677778888, 0, 0)
LONG_VHPT_RID1_DATA_LOW, _ = bundle_words(*LONG_VHPT_RID1_DATA_BUNDLE[1:])
LONG_VHPT_RID2_DATA_LOW, _ = bundle_words(*LONG_VHPT_RID2_DATA_BUNDLE[1:])

test_long_vhpt_same_va_different_rids_refills = require_registers(
    "long_vhpt_same_va_different_rids_refills", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x131)),
        (0x30, *movl_mlx(18, 0x231)),
        (0x40, *movl_mlx(19, 0x0010000004000661)),
        (0x50, *movl_mlx(20, 0x0010000004010661)),
        (0x60, *movl_mlx(21, 0x230)),
        (0x70, *movl_mlx(22, LONG_VHPT_RID1_TAG)),
        (0x80, *movl_mlx(23, LONG_VHPT_RID2_TAG)),
        (0x90, *movl_mlx(24, 0x100020)),
        (0xa0, *movl_mlx(25, 0x100040)),
        (0xb0, 0x00, st8(24, 19), adds(26, 8, 24),
         nop_i()),
        (0xc0, 0x00, st8(26, 21), adds(27, 16, 24),
         nop_i()),
        (0xd0, 0x00, st8(27, 22), nop_i(),
         nop_i()),
        (0xe0, 0x00, st8(25, 20), adds(26, 8, 25),
         nop_i()),
        (0xf0, 0x00, st8(26, 21), adds(27, 16, 25),
         nop_i()),
        (0x100, 0x00, st8(27, 23), nop_i(),
         nop_i()),
        (0x110, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x120, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0x130, *movl_mlx(2, 0x430)),
        (0x140, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x150, 0x00, ld8(28, 2), nop_i(),
         nop_i()),
        (0x160, 0x00, mov_rr_write(18, 0), nop_i(),
         nop_i()),
        (0x170, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x180, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (0x190, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0x1a0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x1b0, 0x00, ld8(30, 2), nop_i(),
         nop_i()),
        (0x1c0, 0x10, nop_m(), nop_i(),
         br_cond(0x1c0, 0x1c0)),
        LONG_VHPT_RID1_DATA_BUNDLE,
        LONG_VHPT_RID2_DATA_BUNDLE,
    ], {
        "ip": 0x1c0,
        "exception": IA64_EXCP_NONE,
        "r28": LONG_VHPT_RID1_DATA_LOW,
        "r29": LONG_VHPT_RID2_DATA_LOW,
        "r30": LONG_VHPT_RID1_DATA_LOW,
    }, entry=0x10)

test_long_vhpt_not_present_ignores_software_fields = require_registers(
    "long_vhpt_not_present_ignores_software_fields", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0xfffffffffffffffe)),
        (0x40, *movl_mlx(19, 0xdeadbeef00000030)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(2, 0x430)),
        (0xd0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0xe0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xf0, 0x00, ld8(29, 2), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR,
                 IA64_PAGE_NOT_PRESENT_VECTOR)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x430,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_long_vhpt_unsupported_page_size_aborts_to_dtlb_miss = require_registers(
    "long_vhpt_unsupported_page_size_aborts_to_dtlb_miss", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x0010000004000661)),
        (0x40, *movl_mlx(19, 0x23c)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(2, 0x430)),
        (0xd0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0xe0, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x20,
                 IA64_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x430,
        "r31": 0x100040,
    }, entry=0x10)

test_long_vhpt_walker_does_not_search_collision_chain = require_registers(
    "long_vhpt_walker_does_not_search_collision_chain", [
        (0x10, *movl_mlx(16, 0x10013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x0010000004000661)),
        (0x40, *movl_mlx(19, 0x230)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100060)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(2, 0x430)),
        (0xd0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0xe0, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25), nop_i(),
         nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x20,
                 IA64_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x430,
        "r31": 0x100040,
    }, entry=0x10)

# The long-format table lives at an unmapped region 6 address, so the
# walker's own reference to the VHPT misses the TLB and raises a VHPT
# Translation fault (the handler at offset 0 records IFA and IHA).
test_long_vhpt_table_tlb_miss_raises_vhpt_translation = require_registers(
    "long_vhpt_table_tlb_miss_raises_vhpt_translation", [
        (0x000, 0x00, mov_m_cr_gr(30, 20), nop_i(),
         nop_i()),
        (0x010, 0x00, mov_m_cr_gr(31, 25), nop_i(),
         nop_i()),
        (0x020, 0x10, nop_m(), nop_i(),
         br_cond(0x020, 0x020)),
        (0x200, *movl_mlx(16, 0xc00000000010013d)),
        (0x210, *movl_mlx(17, 0x231)),
        (0x220, *movl_mlx(18, 0x0010000004000661)),
        (0x230, *movl_mlx(19, 0x230)),
        (0x240, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x250, *movl_mlx(21, 0x100040)),
        (0x260, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x270, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x280, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0x290, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x2a0, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0x2b0, *movl_mlx(2, 0x430)),
        (0x2c0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x2d0, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0x2e0, 0x10, nop_m(), nop_i(),
         br_cond(0x2e0, 0x2e0)),
        (IA64_DTLB_VECTOR, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR, IA64_DTLB_VECTOR)),
    ], {
        "ip": 0x020,
        "exception": IA64_EXCP_NONE,
        "r30": 0x430,
        "r31": 0xc000000000100040,
    }, entry=0x200)

# When the table itself is reachable only through a non-cacheable
# translation, the VHPT is not referenced at all and the walker aborts to a
# Data TLB Miss fault instead of raising a VHPT Translation fault.
test_long_vhpt_uncacheable_table_aborts_to_dtlb_miss = require_registers(
    "long_vhpt_uncacheable_table_aborts_to_dtlb_miss", [
        (0x10, *movl_mlx(16, 0xc00000000010013d)),
        (0x20, *movl_mlx(17, 0x231)),
        (0x30, *movl_mlx(18, 0x0010000004000661)),
        (0x40, *movl_mlx(19, 0x230)),
        (0x50, *movl_mlx(20, LONG_VHPT_RID2_TAG)),
        (0x60, *movl_mlx(21, 0x100040)),
        (0x70, 0x00, st8(21, 18), adds(22, 8, 21),
         nop_i()),
        (0x80, 0x00, st8(22, 19), adds(23, 16, 21),
         nop_i()),
        (0x90, 0x00, st8(23, 20), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(24, 0xc000000000100000)),
        (0xb0, *movl_mlx(25, 0x100671)),
        (0xc0, 0x00, adds(7, 0x38, 0), nop_i(),
         nop_i()),
        (0xd0, 0x00, mov_m_gr_cr(24, 20), nop_i(),
         nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0xf0, 0x00, itc_d(25), nop_i(),
         nop_i()),
        (0x100, 0x00, mov_m_gr_cr(16, 8), nop_i(),
         nop_i()),
        (0x110, 0x00, mov_rr_write(17, 0), nop_i(),
         nop_i()),
        (0x120, *movl_mlx(2, 0x430)),
        (0x130, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0x140, 0x00, tpa(29, 2), nop_i(),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_cond(0x150, 0x150)),
        (IA64_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 25),
         nop_i(), nop_i()),
        (IA64_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DTLB_VECTOR + 0x20,
                 IA64_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x430,
        "r31": 0xc000000000100040,
    }, entry=0x10)

test_itr_d_uses_slot_register_value = require_registers(
    "itr_d_uses_slot_register_value", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, 0x4000000)),
        (0x30, 0x00, mov_m_gr_cr(19, 20), adds(21, 0x58, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(21, 21), adds(10, 7, 0),
         nop_i()),
        (0x50, 0x00, itr_d(10, 18, bit36=1), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(18, 0x0010000001000661)),
        (0x70, *movl_mlx(19, 0xe000000081000000)),
        (0x80, 0x00, mov_m_gr_cr(19, 20), adds(21, 0x60, 0),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(21, 21), adds(10, 2, 0),
         nop_i()),
        (0xa0, 0x00, itr_d(10, 18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(2, 0x4092158)),
        (0xc0, 0x00, ssm(1 << 17), adds(27, 1, 0),
         nop_i()),
        (0xd0, 0x10, st4(2, 27), nop_i(),
         br_cond(0xd0, 0xe0)),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
    ], {"ip": 0xe0, "exception": IA64_EXCP_NONE}, entry=0x10)

test_itr_d_slot_replacement_keeps_old_translation_cached = require_registers(
    "itr_d_slot_replacement_keeps_old_translation_cached", [
        (0x10, *movl_mlx(18, 0x4009661)),
        (0x20, *movl_mlx(19, 0x4010661)),
        (0x30, *movl_mlx(20, 0x9000)),
        (0x40, *movl_mlx(21, 0x20000)),
        (0x50, 0x00, adds(7, 0x38, 0), adds(10, 4, 0),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x80, 0x00, itr_d(10, 18), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
        (0xa0, 0x00, itr_d(10, 19), nop_i(), nop_i()),
        (0xb0, *movl_mlx(22, (1 << 13) | (1 << 17))),
        (0xc0, 0x00, mov_gr_psr_full(22), nop_i(), nop_i()),
        (0xd0, 0x00, ld8(31, 20), nop_i(), nop_i()),
        (0xe0, 0x00, ld8(30, 21), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
        ITC_DATA_BUNDLE,
        PTC_SURVIVOR_BUNDLE,
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r30": PTC_SURVIVOR_LOW,
        "r31": ITC_DATA_LOW,
    }, entry=0x10)

test_itr_d_cached_translation_survives_region_register_write = \
    require_registers(
        "itr_d_cached_translation_survives_region_register_write", [
            (0x10, *movl_mlx(18, 0x4009661)),
            (0x20, *movl_mlx(19, 0x4010661)),
            (0x30, *movl_mlx(20, 0x9000)),
            (0x40, *movl_mlx(21, 0x20000)),
            (0x50, *movl_mlx(23, (0x12345 << 8) | LOW_VECTOR_ITIR)),
            (0x60, 0x00, mov_rr_write(23, 0), adds(7, LOW_VECTOR_ITIR, 0),
             adds(10, 4, 0)),
            (0x70, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
            (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (0x90, 0x00, itr_d(10, 18), nop_i(), nop_i()),
            (0xa0, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
            (0xb0, 0x00, itr_d(10, 19), nop_i(), nop_i()),
            (0xc0, 0x00, mov_rr_write(23, 0), nop_i(), nop_i()),
            (0xd0, 0x00, srlz_d(), nop_i(), nop_i()),
            (0xe0, *movl_mlx(22, (1 << 13) | (1 << 17))),
            (0xf0, 0x00, mov_gr_psr_full(22), nop_i(), nop_i()),
            (0x100, 0x00, ld8(31, 20), nop_i(), nop_i()),
            (0x110, 0x00, ld8(30, 21), nop_i(), nop_i()),
            (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
            ITC_DATA_BUNDLE,
            PTC_SURVIVOR_BUNDLE,
        ], {
            "ip": 0x120,
            "exception": IA64_EXCP_NONE,
            "r30": PTC_SURVIVOR_LOW,
            "r31": ITC_DATA_LOW,
        }, entry=0x10)

test_ptr_d_purge_completes_on_srlz_d = require_registers(
    "ptr_d_purge_completes_on_srlz_d", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(3, HIGH_TR_BASE)),
        (0x90, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0xb0, 0x00, ssm((1 << 13) | (1 << 17)), nop_i(),
         nop_i()),
        (0xc0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, 0x00, ld8(30, 2), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(29, 0x72, 0),
         br_cond(IA64_ALT_DTLB_VECTOR,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r29": 0x72,
        "r31": ITC_DATA_LOW,
    }, entry=0x10)


test_ptc_g_source_purge_precedes_same_bundle_srlz_d = require_registers(
    "ptc_g_source_purge_precedes_same_bundle_srlz_d", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(19, IA64_PSR_DT)),
        (0xa0, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        # Warm the source vCPU's data soft-TLB before the global purge.
        (0xb0, 0x00, ld8(30, 2), nop_i(), nop_i()),
        # MMI template 0x0a has a stop after slot 0.  The following srlz.d
        # is therefore a legal later instruction group in the same bundle.
        (0xc0, 0x08, ptc_g(20, 7), srlz_d(), nop_i()),
        (0xd0, 0x00, tak(31, 20), nop_i(), nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r30": ITC_DATA_LOW,
        # TAK returns one when the completed source purge removed the TC.
        "r31": 1,
    }, entry=0x10, alat=None, smp="2")


test_ptc_ga_clears_source_alat = require_registers(
    "ptc_ga_clears_source_alat", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_cr(20, 20), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(19, IA64_PSR_DT)),
        (0xa0, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0xb0, 0x00, ld8_a(31, 2), nop_i(), nop_i()),
        (0xc0, *movl_mlx(31, 0x55)),
        (0xd0, 0x00, ptc_ga(20, 7), nop_i(), nop_i()),
        # The source translation is pending until srlz.d, so ld8.c can
        # distinguish a cleared source ALAT entry by reloading memory.
        (0xe0, 0x00, ld8_c_clr(31, 2), nop_i(), nop_i()),
        (0xf0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0x100,
        "exception": IA64_EXCP_NONE,
        "r31": ITC_DATA_LOW,
    }, entry=0x10)


test_ptr_d_purge_invalidates_advanced_load = require_registers(
    "ptr_d_purge_invalidates_advanced_load", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(3, HIGH_TR_BASE)),
        (0xa0, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0xb0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0xc0, 0x00, ld8_a(31, 2), nop_i(),
         nop_i()),
        (0xd0, *movl_mlx(31, 0x55)),
        (0xe0, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        (0xf0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x100, 0x00, ld8_c_clr(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
        (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(29, 0x73, 0),
         br_cond(IA64_ALT_DTLB_VECTOR,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r29": 0x73,
    }, entry=0x10)


test_ptr_d_purge_invalidates_warm_speculative_load = require_registers(
    "ptr_d_purge_invalidates_warm_speculative_load", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(3, HIGH_TR_BASE)),
        # Keep PSR.ic clear so the post-purge speculative miss must defer.
        (0xa0, *movl_mlx(19, IA64_PSR_DT)),
        (0xb0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        # Install a direct LOAD comparator and its WB/speculative attributes.
        (0xd0, 0x00, ld8(30, 2), nop_i(),
         nop_i()),
        (0xe0, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        (0xf0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        # The completed purge must prevent the no-fill shortcut from seeing
        # the old comparator.  The current architectural lookup is a miss.
        (0x100, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r30": ITC_DATA_LOW,
        "r30_nat": 0,
        "r31": 0,
        "r31_nat": 1,
    }, entry=0x10)


_SPEC_RID_A = 0x123
_SPEC_RID_B = 0x124
_SPEC_RR_A = (_SPEC_RID_A << 8) | (16 << 2)
_SPEC_RR_B = (_SPEC_RID_B << 8) | (16 << 2)
_SPEC_RID_NEW_BUNDLE = (0x509000, 0x00, 0x1122334455667788, 0, 0)
_SPEC_RID_NEW_LOW, _ = bundle_words(*_SPEC_RID_NEW_BUNDLE[1:])

test_speculative_load_refills_after_region_rid_switch = require_registers(
    "speculative_load_refills_after_region_rid_switch", [
        (0x10, *movl_mlx(20, HIGH_TR_BASE)),
        (0x20, *movl_mlx(23, _SPEC_RR_A)),
        (0x30, 0x00, mov_rr_write(23, 20), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(18, 0x400000 | DTR_PTE_WB)),
        (0x60, *movl_mlx(19, HIGH_TR_BASE)),
        (0x70, 0x00, mov_m_gr_cr(19, 20), adds(21, 16 << 2, 0),
         nop_i()),
        (0x80, 0x00, mov_m_gr_cr(21, 21), adds(10, 4, 0),
         nop_i()),
        (0x90, 0x00, itr_d(10, 18), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_d(), nop_i(),
         nop_i()),

        (0xb0, *movl_mlx(24, _SPEC_RR_B)),
        (0xc0, 0x00, mov_rr_write(24, 20), nop_i(),
         nop_i()),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(18, 0x500000 | DTR_PTE_WB)),
        (0xf0, 0x00, mov_m_gr_cr(19, 20), adds(10, 5, 0),
         nop_i()),
        (0x100, 0x00, itr_d(10, 18), nop_i(),
         nop_i()),
        (0x110, 0x00, srlz_d(), nop_i(),
         nop_i()),

        # Warm the direct soft-TLB entry under RID A with a zero-valued page.
        (0x120, 0x00, mov_rr_write(23, 20), nop_i(),
         nop_i()),
        (0x130, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x150, *movl_mlx(22, IA64_PSR_DT)),
        (0x160, 0x00, mov_gr_psr_full(22), nop_i(),
         nop_i()),
        (0x170, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x180, 0x00, ld8(30, 2), nop_i(),
         nop_i()),

        # RR writes must discard the same-VA RID-A host entry.  The
        # speculative load must therefore resolve RID B and read its page.
        (0x190, 0x00, mov_rr_write(24, 20), nop_i(),
         nop_i()),
        (0x1a0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x1b0, 0x00, ld8_s(31, 2), nop_i(),
         nop_i()),
        (0x1c0, 0x10, nop_m(), nop_i(),
         br_cond(0x1c0, 0x1c0)),
        (0x409000, 0x00, 0, 0, 0),
        _SPEC_RID_NEW_BUNDLE,
    ], {
        "ip": 0x1c0,
        "exception": IA64_EXCP_NONE,
        "r30": 0,
        "r30_nat": 0,
        "r31": _SPEC_RID_NEW_LOW,
        "r31_nat": 0,
    }, entry=0x10)


test_interruption_serializes_pending_ptr_d = require_registers(
    "interruption_serializes_pending_ptr_d", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(3, HIGH_TR_BASE)),
        (0xa0, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0xb0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0xc0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xd0, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0xe0, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        (0xf0, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR, 0x00, ld8(30, 2), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x10, 0x10, nop_m(), adds(29, 0x74, 0),
         br_cond(IA64_BREAK_VECTOR + 0x10,
                 IA64_BREAK_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(29, 0x75, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
        (IA64_DATA_NESTED_TLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR + 0x10,
                 IA64_DATA_NESTED_TLB_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_DATA_NESTED_TLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r29": 0x75,
        "r31": ITC_DATA_LOW,
    }, entry=0x10)

test_rfi_serializes_pending_ptr_d = require_registers(
    "rfi_serializes_pending_ptr_d", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), adds(5, 5, 0),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(2, HIGH_TR_BASE + 0x9000)),
        (0x90, *movl_mlx(3, HIGH_TR_BASE)),
        (0xa0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0xb0, *movl_mlx(21, 0x100)),
        (0xc0, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        *rfi_to_gr(0xd0, 19, 21),
        (0x100, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), adds(29, 0x76, 0),
         br_cond(0x110, 0x120)),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(29, 0x77, 0),
         br_cond(IA64_ALT_DTLB_VECTOR,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        ITC_DATA_BUNDLE,
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r29": 0x77,
    }, entry=0x10)

test_rfi_serializes_pending_ptr_i = require_registers(
    "rfi_serializes_pending_ptr_i", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_IT | IA64_PSR_BN)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), adds(5, 0, 0),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(0, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x10000)),
        (0x80, *movl_mlx(20, 0x10000)),
        (0x90, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0),
         nop_i()),
        (0xa0, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(3, 0x10000)),
        (0xc0, 0x00, ptr_i(3, 7), nop_i(),
         nop_i()),
        *rfi_to_gr(0xd0, 19, 20),
        (0x4000c00, 0x10, nop_m(), adds(31, 0x78, 0),
         br_cond(0x4000c00, 0x0c10)),
        (0x4000c10, 0x10, nop_m(), nop_i(),
         br_cond(0x4000c10, 0x0c10)),
        (0x4010000, 0x10, nop_m(), adds(31, 0x44, 0),
         br_cond(0x4010000, 0x10010)),
        (0x4010010, 0x10, nop_m(), nop_i(),
         br_cond(0x4010010, 0x10010)),
    ], {
        "ip": 0x0c10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x78,
    }, entry=0x10)

test_mov_pkr_indexed_decode = require_registers("mov_pkr_indexed_decode", [
    (0x10, 0x00, addl(2, 0x5501, 0), adds(3, 0x103, 0),
     nop_i()),
    (0x20, 0x00, mov_pkr_indexed(3, 2, bit36=1), nop_i(),
     nop_i()),
    (0x30, 0x09, mov_m_cr_gr(4, 19), mov_pkr_indexed_read(5, 3, bit36=1),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "exception": IA64_EXCP_NONE,
    "r4": 0,
    "r5": 0x5501,
}, entry=0x10)

test_mov_pkr_merced_unimplemented_key_bit_faults = require_exception(
    "mov_pkr_merced_unimplemented_key_bit_faults", [
        (0x10, *movl_mlx(2, 1 << (8 + 21))),
        (0x20, 0x00, adds(3, 0, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_pkr_indexed(3, 2, bit36=1), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x30,
    entry=0x10, cpu="merced")

test_mov_pkr_duplicate_key_invalidates_old_slot = require_registers(
    "mov_pkr_duplicate_key_invalidates_old_slot", [
        (0x10, 0x00, addl(2, 0x101, 0), addl(3, 0x101, 0),
         nop_i()),
        (0x20, 0x00, mov_pkr_indexed(3, 2, bit36=1), nop_i(),
         nop_i()),
        (0x30, 0x00, addl(3, 0x102, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_pkr_indexed(3, 2, bit36=1), nop_i(),
         nop_i()),
        (0x50, 0x00, addl(3, 0x101, 0), nop_i(),
         nop_i()),
        (0x60, 0x09, mov_pkr_indexed_read(5, 3, bit36=1), addl(3, 0x102, 0),
         nop_i()),
        (0x70, 0x09, mov_pkr_indexed_read(6, 3, bit36=1), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r5": 0x100,
        "r6": 0x101,
    }, entry=0x10)

REGION7_DATA = bundle_words(0x00, 0x123456789a, 0, 0)[0]
FW_IDENTITY_DATA = bundle_words(0x00, 0x1122334455667788, 0, 0)[0]
REGION7_SCRATCH_DATA = 0x1122334455667788

test_region7_untranslated_data_faults = require_registers(
    "region7_untranslated_data_faults", [
    (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
    (0x20, *movl_mlx(3, 0xe000000000000300)),
    (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
     nop_i()),
    (0x40, 0x08, ld8(31, 3), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
    (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(31, 0x68, 0),
     br_cond(IA64_ALT_DTLB_VECTOR,
             IA64_ALT_DTLB_VECTOR + 0x10)),
    (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
     br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
             IA64_ALT_DTLB_VECTOR + 0x10)),
    (0x300, 0x00, 0x123456789a, 0,
     0),
], {
    "ip": IA64_ALT_DTLB_VECTOR + 0x10,
    "exception": IA64_EXCP_NONE,
    "r31": 0x68,
}, entry=0x10)

test_region7_untranslated_user_data_faults = require_registers(
    "region7_untranslated_user_data_faults", [
        (0x10, *movl_mlx(19, (1 << 13) | (1 << 17) |
                         (3 << 32) | (1 << 44))),
        (0x20, *movl_mlx(3, 0xe000000000000300)),
        (0x30, *movl_mlx(20, 0x60)),
        *rfi_to_gr(0x40, 19, 20),
        (0x60, 0x08, ld8(31, 3), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x1000, 0x10, nop_m(), adds(31, 0x6e, 0),
         br_cond(0x1000, 0x1010)),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
        (0x300, 0x00, 0x123456789a, 0,
         0),
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6e,
    }, entry=0x10)

test_region7_loader_scratch_store_load = require_registers("region7_loader_scratch_store_load", [
    *dtr_setup_bundles(0x10, REGION7_SCRATCH_VA, REGION7_SCRATCH_PA),
    (0x70, *movl_mlx(3, REGION7_SCRATCH_VA)),
    (0x80, *movl_mlx(4, REGION7_SCRATCH_DATA)),
    (0x90, 0x00, ssm(1 << 17), nop_i(),
     nop_i()),
    (0xa0, 0x08, st8(3, 4), ld8(31, 3),
     nop_i()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
], {
    "ip": 0xb0,
    "exception": IA64_EXCP_NONE,
    "r31": REGION7_SCRATCH_DATA,
}, entry=0x10)

test_region7_dtr_controls_data_mapping = require_registers(
    "region7_dtr_controls_data_mapping", [
        *dtr_setup_bundles(0x10, 0xe000000081000430, 0x1000430,
                           page_shift=22, slot=2),
        (0x70, *movl_mlx(3, 0xe000000081000430)),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x08, ld8(31, 3), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x1000430, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x81000430, 0x00, 0xfedcba9876543210, 0,
         0),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r31": bundle_words(0x00, 0x123456789abcdef0, 0, 0)[0],
    }, entry=0x10)

test_region7_nonzero_rid_requires_translation = require_registers(
    "region7_nonzero_rid_requires_translation", [
        (0x10, *movl_mlx(17, 0xe000000000000300)),
        (0x20, *movl_mlx(18, (1 << 8) | (13 << 2))),
        (0x30, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x60, 0x08, ld8(31, 17), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x1000, 0x10, mov_m_cr_gr(31, 21), nop_i(),
         br_cond(0x1000, 0x1010)),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
        (0x300, 0x00, 0x123456789a, 0,
         0),
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r31": (1 << 8) | (13 << 2),
    }, entry=0x10)

test_sal_boot_identity_handles_nonzero_region7_rid = require_registers(
    "sal_boot_identity_handles_nonzero_region7_rid", [
        (0x10, *movl_mlx(17, 0xe000000000000300)),
        (0x20, *movl_mlx(18, (1 << 8) | (13 << 2))),
        (0x30, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
        (0x40, 0x00, mov_m_gr_cr(2, 2), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_rr_write(18, 17), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x70, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x80, 0x08, ld8(31, 17), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x300, 0x00, 0x123456789a, 0,
         0),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r31": REGION7_DATA,
    }, entry=0x10)

test_sal_boot_nonstandard_direct_tc_is_purgeable_after_iva_handoff = \
    require_registers(
        "sal_boot_nonstandard_direct_tc_is_purgeable_after_iva_handoff", [
            (0x10, *movl_mlx(17, 0xe000000080200430)),
            (0x20, *movl_mlx(18, (1 << 8) | (13 << 2) | 1)),
            (0x30, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
            (0x40, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
            (0x50, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
            (0x60, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
            (0x70, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
            (0x80, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x90, 0x08, ld8(29, 17), nop_i(), nop_i()),
            (0xa0, *movl_mlx(2, 0x04000000)),
            (0xb0, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
            (0xc0, 0x00, srlz_i(), nop_i(), nop_i()),
            (0xd0, 0x08, ld8(30, 17), nop_i(), nop_i()),
            (0xe0, *movl_mlx(18, 13 << 2)),
            (0xf0, 0x08, nop_m(), ptc_l(17, 18), nop_i()),
            (0x100, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x110, 0x08, ld8(31, 17), nop_i(), nop_i()),
            (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
            (0x04000000 + IA64_ALT_DTLB_VECTOR, 0x00,
             mov_m_cr_gr(28, 17), nop_i(), nop_i()),
            (0x04000000 + IA64_ALT_DTLB_VECTOR + 0x10, 0x10,
             nop_m(), nop_i(),
             br_cond(0x04000000 + IA64_ALT_DTLB_VECTOR + 0x10,
                     0x04000000 + IA64_ALT_DTLB_VECTOR + 0x10)),
            raw_bundle(0x00200430, 0x1122334455667788,
                       0x8877665544332211),
        ], {
            "ip": 0x04000000 + IA64_ALT_DTLB_VECTOR + 0x10,
            "exception": IA64_EXCP_NONE,
            "r28": IA64_ISR_R,
            "r29": 0x1122334455667788,
            "r30": 0x1122334455667788,
        }, entry=0x10, machine="itanium-vpc")

test_sal_boot_identity_coexists_with_explicit_rid_mapping = \
    require_registers(
        "sal_boot_identity_coexists_with_explicit_rid_mapping", [
            (0x10, *movl_mlx(17, 0xe000000000200430)),
            (0x20, *movl_mlx(18, (1 << 8) | (12 << 2))),
            (0x30, 0x00, mov_rr_write(18, 17), nop_i(),
             nop_i()),
            *dtr_setup_bundles(0x40, 0xe000000000200430,
                               0x00400430, page_shift=12, slot=1),
            (0xa0, *movl_mlx(18, 0x100730)),
            (0xb0, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
            (0xc0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
            (0xd0, 0x00, mov_m_gr_cr(2, 2), nop_i(),
             nop_i()),
            (0xe0, 0x00, mov_rr_write(18, 17), nop_i(),
             nop_i()),
            (0xf0, 0x00, mov_gr_psr_full(19), nop_i(),
             nop_i()),
            (0x100, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x110, 0x08, ld8(30, 17), nop_i(),
             nop_i()),
            (0x120, 0x00, rsm(IA64_PSR_DT), nop_i(), nop_i()),
            (0x130, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x140, *movl_mlx(18, (1 << 8) | (12 << 2))),
            (0x150, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
            (0x160, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x170, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
            (0x180, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x190, 0x08, ld8(31, 17), nop_i(), nop_i()),
            (0x1a0, 0x10, nop_m(), nop_i(),
             br_cond(0x1a0, 0x1a0)),
            raw_bundle(0x00200430, 0x1122334455667788,
                       0x8877665544332211),
            raw_bundle(0x00400430, 0x0123456789abcdef,
                       0xfedcba9876543210),
        ], {
            "ip": 0x1a0,
            "exception": IA64_EXCP_NONE,
            "r30": 0x1122334455667788,
            "r31": 0x0123456789abcdef,
        }, entry=0x10)

test_region7_untranslated_high_va_faults = require_registers(
    "region7_untranslated_high_va_faults", [
    (0x10, *movl_mlx(19, (1 << 13) | (1 << 17))),
    (0x20, *movl_mlx(3, 0xfffffffffffc0000)),
    (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     bsw1()),
    (0x50, 0x08, ld8(31, 3), nop_i(),
     nop_i()),
    (0x1000, 0x10, nop_m(), adds(31, 0x6d, 0),
     br_cond(0x1000, 0x1010)),
    (0x1010, 0x10, nop_m(), nop_i(),
     br_cond(0x1010, 0x1010)),
], {
    "ip": 0x1010,
    "exception": IA64_EXCP_NONE,
    "r31": 0x6d,
}, entry=0x10)

test_region6_untranslated_data_faults = require_registers(
    "region6_untranslated_data_faults", [
    (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
    (0x20, *movl_mlx(3, 0xc000000000000300)),
    (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
     nop_i()),
    (0x40, 0x08, ld8(31, 3), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
    (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(31, 0x69, 0),
     br_cond(IA64_ALT_DTLB_VECTOR,
             IA64_ALT_DTLB_VECTOR + 0x10)),
    (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
     br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
             IA64_ALT_DTLB_VECTOR + 0x10)),
    (0x300, 0x00, 0x123456789a, 0,
     0),
], {
    "ip": IA64_ALT_DTLB_VECTOR + 0x10,
    "exception": IA64_EXCP_NONE,
    "r31": 0x69,
}, entry=0x10)

test_region6_untranslated_user_data_faults = require_registers(
    "region6_untranslated_user_data_faults", [
        (0x10, *movl_mlx(19, (1 << 13) | (1 << 17) |
                         (3 << 32) | (1 << 44))),
        (0x20, *movl_mlx(3, 0xc000000000000300)),
        (0x30, *movl_mlx(20, 0x60)),
        *rfi_to_gr(0x40, 19, 20),
        (0x60, 0x08, ld8(31, 3), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x1000, 0x10, nop_m(), adds(31, 0x6f, 0),
         br_cond(0x1000, 0x1010)),
        (0x1010, 0x10, nop_m(), nop_i(),
         br_cond(0x1010, 0x1010)),
        (0x300, 0x00, 0x123456789a, 0,
         0),
    ], {
        "ip": 0x1010,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6f,
    }, entry=0x10)

test_region6_high_dtr_tpa_decode = require_registers(
    "region6_high_dtr_tpa_decode", [
        *dtr_setup_bundles(0x10, 0xc00080000ff280a1, 0x5080a1),
        (0x70, *movl_mlx(3, 0xc00080000ff280a1)),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x00, tpa(31, 3, bit36=1), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r31": 0x5080a1,
    }, entry=0x10)

test_region6_local_sapic_store = require_registers("region6_local_sapic_store", [
    *dtr_setup_bundles(0x10, 0xc0000000fee00000, 0xfee00000,
                       pte_flags=DTR_PTE_UC),
    (0x70, *movl_mlx(3, 0xc0000000fee00000)),
    (0x80, *movl_mlx(4, 0xef)),
    (0x90, 0x10, ssm(1 << 17), nop_i(),
     br_cond(0x90, 0xa0)),
    (0xa0, 0x00, st8(3, 4), nop_i(),
     nop_i()),
    (0xb0, 0x00, mov_m_cr_gr(30, IA64_CR_SAPIC_IRR3), nop_i(),
     nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(),
     br_cond(0xc0, 0xc0)),
], {
    "ip": 0xc0,
    "exception": IA64_EXCP_NONE,
    "r30": 1 << 47,
}, entry=0x10)

test_region6_processor_interrupt_block_inta_read = require_registers(
    "region6_processor_interrupt_block_inta_read", [
        *dtr_setup_bundles(0x10, 0xc0000000fefe0000, 0xfefe0000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(3, 0xc0000000fefe0000)),
        (0x80, 0x10, ssm(1 << 17), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x00, ld1(31, 3), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r31": 0,
    }, entry=0x10)

test_region6_processor_interrupt_block_xtp_store = require_registers(
    "region6_processor_interrupt_block_xtp_store", [
        *dtr_setup_bundles(0x10, 0xc0000000fefe0008, 0xfefe0008,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(3, 0xc0000000fefe0008)),
        (0x80, *movl_mlx(4, 0xd0)),
        (0x90, 0x10, ssm(1 << 17), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x00, st1_postinc(3, 4, 0), nop_i(),
         nop_i()),
        (0xb0, 0x00, ld1(31, 3), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_cr_gr(30, IA64_CR_SAPIC_IRR3), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r30": 0,
        "r31": 0x80,
    }, entry=0x10)

test_firmware_identity_under_translation = require_registers(
    "firmware_identity_under_translation", [
        (0x10, *movl_mlx(2, 0x130000)),
        (0x20, *movl_mlx(3, IA64_FIRMWARE_IVT_BASE)),
        (0x30, 0x00, mov_m_gr_cr(3, 2), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(19, (1 << 17) | (1 << 36))),
        (0x50, *movl_mlx(20, 0x100000)),
        *rfi_to_gr(0x60, 19, 20),
        (0x100000, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0x100010, 0x10, nop_m(), nop_i(),
         br_cond(0x100010, 0x100010)),
        (0x130000, 0x00, 0x1122334455667788, 0,
         0),
    ], {
        "ip": 0x100010,
        "exception": IA64_EXCP_NONE,
        "r31": FW_IDENTITY_DATA,
    }, entry=0x10)

test_firmware_identity_ends_after_iva_handoff = require_registers(
    "firmware_identity_ends_after_iva_handoff", [
        *dtr_setup_bundles(0x10, 0x130000, 0x4130000),
        (0x70, *movl_mlx(3, 0x130000)),
        (0x80, 0x00, ssm(1 << 17), nop_i(),
         nop_i()),
        (0x90, 0x08, ld8(30, 3), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(2, 0x4000000)),
        (0xb0, 0x00, mov_m_gr_cr(2, 2), nop_i(),
         nop_i()),
        (0xc0, 0x08, ld8(31, 3), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x130000, 0x00, 0x1122334455667788, 0,
         0),
        (0x4130000, 0x00, 0x8877665544332211, 0,
         0),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r30": bundle_words(0x00, 0x1122334455667788, 0, 0)[0],
        "r31": bundle_words(0x00, 0x8877665544332211, 0, 0)[0],
    }, entry=0x10)

test_firmware_runtime_identity_after_iva_handoff = require_registers(
    "firmware_runtime_identity_after_iva_handoff", [
        (0x10, *movl_mlx(2, 0x130000)),
        (0x20, *movl_mlx(3, 0x4000000)),
        (0x30, 0x00, mov_m_gr_cr(3, 2), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(19, (1 << 17) | (1 << 36))),
        (0x50, *movl_mlx(20, 0x100000)),
        *rfi_to_gr(0x60, 19, 20),
        (0x100000, 0x00, ld8(31, 2), nop_i(),
         nop_i()),
        (0x100010, 0x10, nop_m(), nop_i(),
         br_cond(0x100010, 0x100010)),
        (0x130000, 0x00, 0x1122334455667788, 0,
         0),
    ], {
        "ip": 0x100010,
        "exception": IA64_EXCP_NONE,
        "r31": FW_IDENTITY_DATA,
    }, entry=0x10)

test_firmware_identity_does_not_override_user_mapping = require_registers(
    "firmware_identity_does_not_override_user_mapping", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | (3 << 7))),
        (0x20, *movl_mlx(19, (1 << 36) | IA64_PSR_CPL3)),
        (0x30, *movl_mlx(20, IA64_FW_IDENTITY_BASE)),
        (0x40, *movl_mlx(21, 0x4000000)),
        (0x50, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(21, 2), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_i(), nop_i(),
         nop_i()),
        *rfi_to_gr(0xb0, 19, 20),
        (IA64_FW_IDENTITY_BASE, 0x10, nop_m(), adds(31, 0x66, 0),
         br_cond(IA64_FW_IDENTITY_BASE,
                 IA64_FW_IDENTITY_BASE + 0x10)),
        (IA64_FW_IDENTITY_BASE + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FW_IDENTITY_BASE + 0x10,
                 IA64_FW_IDENTITY_BASE + 0x10)),
        (0x4000000, 0x10, nop_m(), adds(31, 0x55, 0),
         br_cond(IA64_FW_IDENTITY_BASE,
                 IA64_FW_IDENTITY_BASE + 0x10)),
        (0x4000010, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FW_IDENTITY_BASE + 0x10,
                 IA64_FW_IDENTITY_BASE + 0x10)),
    ], {
        "ip": IA64_FW_IDENTITY_BASE + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x55,
    }, entry=0x10)


test_rfi_restores_translation_bits = require_registers(
    "rfi_restores_translation_bits", [
        (0x10, *movl_mlx(19, (1 << 17) | (1 << 27) | (1 << 36))),
        (0x20, *movl_mlx(20, 0x70)),
        (0x30, *movl_mlx(21, 0x100000)),
        *rfi_to_gr(0x40, 19, 21),
        (0x100000, 0x00, mov_m_gr_cr(0, 16), nop_i(),
         nop_i()),
        (0x100010, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0x100020, 0x10, mov_m_gr_cr(0, 23), nop_i(),
         rfi_b()),
        (0x70, 0x00, mov_m_psr_gr(31), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "exception": IA64_EXCP_NONE, "r31": 0}, entry=0x10)

test_mov_rr_indexed_decode = require_registers("mov_rr_indexed_decode", [
    (0x10, *movl_mlx(17, 0xa000000000000000)),
    (0x20, 0x00, adds(16, 0x539, 0), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_rr_write(16, 17, ignored36=1), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_rr_read(29, 17, ignored36=1), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
    br_cond(0x50, 0x50)),
], {"ip": 0x50, "r29": 0x539}, entry=0x10)

test_mov_rr_merced_256m_page_size = require_registers(
    "mov_rr_merced_256m_page_size", [
        (0x10, *movl_mlx(17, 0xa000000000000000)),
        (0x20, 0x00, adds(16, (28 << 2) | 1, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_rr_write(16, 17), nop_i(), nop_i()),
        (0x40, 0x00, mov_rr_read(29, 17), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "r29": (28 << 2) | 1,
    }, entry=0x10, cpu="merced")

test_mov_rr_merced_64m_page_size = require_registers(
    "mov_rr_merced_64m_page_size", [
        (0x10, *movl_mlx(17, 0xa000000000000000)),
        (0x20, 0x00, adds(16, (26 << 2) | 1, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_rr_write(16, 17), nop_i(), nop_i()),
        (0x40, 0x00, mov_rr_read(29, 17), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "r29": (26 << 2) | 1,
    }, entry=0x10, cpu="merced")

test_mov_rr_merced_unimplemented_rid_bit_faults = require_exception(
    "mov_rr_merced_unimplemented_rid_bit_faults", [
        (0x10, *movl_mlx(17, 0xa000000000000000)),
        (0x20, *movl_mlx(16, (1 << (8 + 18)) | (12 << 2) | 1)),
        (0x30, 0x00, mov_rr_write(16, 17), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x30,
    entry=0x10, cpu="merced")

test_ptc_e_purges_data_tc_on_srlz_i = require_registers(
    "ptc_e_purges_data_tc_on_srlz_i", [
        (0x10, *movl_mlx(17, PERCPU_ADDR)),
        (0x20, *movl_mlx(18, 0x00100000052c0661)),
        (0x30, *movl_mlx(19, REGION7_GRANULE_RR)),
        (0x40, 0x00, mov_rr_write(19, 17), nop_i(),
         nop_i()),
        (0x50, 0x00, adds(7, PERCPU_ITIR, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(17, 20), nop_i(),
         nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x90, 0x00, ptc_e(17), nop_i(),
         nop_i()),
        (0xa0, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0xc0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0xd0, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0xe0, 0x00, ld8(31, 17), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
        (IA64_ALT_DTLB_VECTOR, 0x10, nop_m(), adds(31, 0x6e, 0),
         br_cond(IA64_ALT_DTLB_VECTOR,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x10,
                 IA64_ALT_DTLB_VECTOR + 0x10)),
        (0x52c0000, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x6e,
    }, entry=0x10)

test_percpu_alt_dtlb_uses_updated_kr3_after_ptc_e = require_registers(
    "percpu_alt_dtlb_uses_updated_kr3_after_ptc_e", [
        (0x10, *movl_mlx(17, PERCPU_ADDR + 0x4b8)),
        (0x20, *movl_mlx(18, 0x00100000052c0661)),
        (0x30, *movl_mlx(19, REGION7_GRANULE_RR)),
        (0x40, 0x00, mov_rr_write(19, 17), nop_i(),
         nop_i()),
        (0x50, 0x00, adds(7, PERCPU_ITIR, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(16, PERCPU_ADDR)),
        (0x80, 0x00, mov_m_gr_cr(16, 20), nop_i(),
         nop_i()),
        (0x90, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xa0, 0x00, ptc_e(17), nop_i(),
         nop_i()),
        (0xb0, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(20, 0x1140000)),
        (0xd0, 0x00, mov_m_gr_ar(20, 3), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0xf0, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x110, 0x00, ld8(31, 17), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(16, 20), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_ar_gr(19, 3), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, *movl_mlx(26, 0x40000)),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, sub_reg(19, 19, 26), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, adds(21, 0x661, 0), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x00, or_reg(19, 19, 21), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x60, 0x00, adds(25, PERCPU_ITIR, 0),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x70, 0x00, mov_m_gr_cr(25, 21), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x80, 0x00, itc_d(19), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x90, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x52c04b8, 0x00, 0x1111111111111111, 0,
         0),
        (0x11004b8, 0x00, PERCPU_NEW_DATA, 0,
         0),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r31": PERCPU_NEW_DATA_LOW,
    }, entry=0x10)

test_percpu_kr3_update_evicts_old_tc_mapping = require_registers(
    "percpu_kr3_update_evicts_old_tc_mapping", [
        (0x10, *movl_mlx(17, PERCPU_ADDR + 0x4b8)),
        (0x20, *movl_mlx(18, 0x00100000052c0661)),
        (0x30, *movl_mlx(19, REGION7_GRANULE_RR)),
        (0x40, *movl_mlx(20, 0x5300000)),
        (0x50, 0x00, mov_m_gr_ar(20, 3), nop_i(), nop_i()),
        (0x60, 0x00, mov_rr_write(19, 17), nop_i(), nop_i()),
        (0x70, 0x00, adds(7, PERCPU_ITIR, 0), nop_i(), nop_i()),
        (0x80, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x90, *movl_mlx(16, PERCPU_ADDR)),
        (0xa0, 0x00, mov_m_gr_cr(16, 20), nop_i(), nop_i()),
        (0xb0, 0x00, itc_d(18), nop_i(), nop_i()),
        (0xc0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0xd0, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0xe0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xf0, 0x00, ld8(30, 17), nop_i(), nop_i()),
        (0x100, *movl_mlx(20, 0x1140000)),
        (0x110, 0x00, mov_m_gr_ar(20, 3), nop_i(), nop_i()),
        (0x120, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x130, 0x00, ld8(31, 17), nop_i(), nop_i()),
        (0x140, 0x10, nop_m(), nop_i(), br_cond(0x140, 0x140)),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(16, 20), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_ar_gr(19, 3), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, *movl_mlx(26, 0x40000)),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, sub_reg(19, 19, 26), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, adds(21, 0x661, 0), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x00, or_reg(19, 19, 21), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x60, 0x00, adds(25, PERCPU_ITIR, 0),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x70, 0x00, mov_m_gr_cr(25, 21), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x80, 0x00, itc_d(19), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x90, 0x10, nop_m(), nop_i(), rfi_b()),
        (0x52c04b8, 0x00, PERCPU_OLD_DATA, 0, 0),
        (0x11004b8, 0x00, PERCPU_NEW_DATA, 0, 0),
    ], {
        "ip": 0x140,
        "exception": IA64_EXCP_NONE,
        "r30": PERCPU_OLD_DATA_LOW,
        "r31": PERCPU_NEW_DATA_LOW,
    }, entry=0x10)


def test_srlz_i_without_pending_itlb_change_keeps_tb_cache(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, 0x10, nop_m(), srlz_i(),
         br_cond(0x10, 0x10)),
    ], entry=0x10)
    if stats.get("TB count", 0) < 1:
        raise AssertionError(f"missing translated TB:\n{output}")
    if stats.get("TB flush count") != 0:
        raise AssertionError(f"srlz.i caused TB flush:\n{output}")


def test_firmware_iva_handoff_keeps_tb_cache(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(2, 0x4000000)),
        (0x20, *movl_mlx(3, IA64_FIRMWARE_IVT_BASE)),
        (0x30, 0x01, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x40, 0x01, srlz_i(), nop_i(), nop_i()),
        (0x50, 0x01, mov_m_gr_cr(3, 2), nop_i(), nop_i()),
        (0x60, 0x01, srlz_i(), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], entry=0x10)
    if stats.get("TB count", 0) < 1:
        raise AssertionError(f"missing translated TB:\n{output}")
    if stats.get("TB flush count") != 0:
        raise AssertionError(
            f"firmware IVA handoff discarded the TB cache:\n{output}")


def test_sync_i_without_pending_fc_i_keeps_tb_cache(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, 0x00, sync_i(), nop_i(), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_cond(0x20, 0x20)),
    ], entry=0x10)
    if stats.get("TB count", 0) < 1:
        raise AssertionError(f"missing translated TB:\n{output}")
    if stats.get("TB flush count") != 0:
        raise AssertionError(
            f"sync.i without fc.i caused TB flush:\n{output}")


def test_fc_i_sync_i_keeps_unrelated_tb_cache(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(16, 0x200)),
        (0x20, *movl_mlx(17, 0x240)),
        (0x30, 0x00, fc_i(16), nop_i(), nop_i()),
        (0x40, 0x01, fc_i(17), nop_i(), nop_i()),
        (0x50, 0x01, sync_i(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], entry=0x10)
    if stats.get("TB flush count") != 0:
        raise AssertionError(
            "fc.i/sync.i discarded the unrelated TB cache:\n" +
            output)


test_fc_i_unimplemented_physical_address_faults = require_registers(
    "fc_i_unimplemented_physical_address_faults", [
        (0x10, *movl_mlx(16, 1 << IA64_IMPL_PA_BITS)),
        (0x20, *movl_mlx(19, IA64_PSR_IC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x01, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, fc_i(16), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00,
         mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(9, 17), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(10, 20), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x30,
                 IA64_GENERAL_VECTOR + 0x30)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": (IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R |
               IA64_ISR_NA | 1),
        "r10": 1 << IA64_IMPL_PA_BITS,
    }, entry=0x10)

test_fc_i_translation_miss_raises_nonaccess_alt_dtlb = require_registers(
    "fc_i_translation_miss_raises_nonaccess_alt_dtlb", [
        (0x10, *movl_mlx(16, HIGH_TR_BASE + 0x90000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, fc_i(16), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_ALT_DTLB,
        "fault_ip": 0x50,
        "r30": HIGH_TR_BASE + 0x90000,
        "r31": IA64_ISR_NA | IA64_ISR_R | 1,
    }, entry=0x10)

test_fc_i_cpl0_ignores_key_and_access_bit = require_registers(
    "fc_i_cpl0_ignores_key_and_access_bit", [
        (0x10, *movl_mlx(2, KEY_TEST_VA)),
        (0x20, *movl_mlx(16, KEY_TEST_RR)),
        (0x30, *movl_mlx(18, LOW_VECTOR_TR_PTE & ~PTE_ACCESSED)),
        (0x40, *movl_mlx(7, KEY_TEST_ITIR)),
        (0x50, 0x00, mov_rr_write(16, 0), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 20), nop_i(), nop_i()),
        (0x80, 0x00, itc_d(18), nop_i(), nop_i()),
        (0x90, *movl_mlx(19, KEY_TEST_PSR)),
        (0xa0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xa0, 0xb0)),
        (0xb0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xc0, 0x00, fc_i(2), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fc_i_page_not_present_is_not_silently_ignored = require_registers(
    "fc_i_page_not_present_is_not_silently_ignored", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_WB & ~1),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(16, HIGH_TR_BASE + 0x208)),
        (0xb0, 0x00, fc_i(16), nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_PAGE_NOT_PRESENT_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
                 IA64_PAGE_NOT_PRESENT_VECTOR + 0x20)),
    ], {
        "ip": IA64_PAGE_NOT_PRESENT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_PAGE_NOT_PRESENT,
        "fault_ip": 0xb0,
        "r30": HIGH_TR_BASE + 0x208,
        "r31": IA64_ISR_NA | IA64_ISR_R | 1,
    }, entry=0x10)

test_fc_i_natpage_reports_fc_nonaccess_code = require_registers(
    "fc_i_natpage_reports_fc_nonaccess_code", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(16, HIGH_TR_BASE + 0x208)),
        (0xb0, 0x00, fc_i(16), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_NAT_CONSUMPTION_VECTOR + 0x20,
                 IA64_NAT_CONSUMPTION_VECTOR + 0x20)),
    ], {
        "ip": IA64_NAT_CONSUMPTION_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_NAT_CONSUMPTION,
        "fault_ip": 0xb0,
        "r30": HIGH_TR_BASE + 0x208,
        "r31": IA64_ISR_NA | IA64_ISR_R | 0x21,
    }, entry=0x10)

test_fc_i_cpl3_access_rights_fault_is_nonaccess = require_registers(
    "fc_i_cpl3_access_rights_fault_is_nonaccess", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_CPL3)),
        (0x80, *movl_mlx(20, 0xb0)),
        *rfi_to_gr(0x90, 19, 20),
        (0xb0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xc0, *movl_mlx(16, HIGH_TR_BASE + 0x208)),
        (0xd0, 0x00, fc_i(16), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR, 0x00,
         mov_m_cr_gr(30, 20), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_VECTOR + 0x20,
                 IA64_DATA_ACCESS_VECTOR + 0x20)),
    ], {
        "ip": IA64_DATA_ACCESS_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_DATA_ACCESS,
        "fault_ip": 0xd0,
        "r30": HIGH_TR_BASE + 0x208,
        "r31": IA64_ISR_NA | IA64_ISR_R | 1,
    }, entry=0x10)


def test_itlb_mapping_change_keeps_reusable_tb_cache(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(19, 0x8000)),
        (0x30, 0x00, adds(7, LOW_VECTOR_ITIR, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(19, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_i(), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], entry=0x10)
    if stats.get("TB count", 0) < 1:
        raise AssertionError(f"missing translated TB:\n{output}")
    if stats.get("TB flush count") != 0:
        raise AssertionError(
            f"instruction mapping change discarded reusable TBs:\n{output}")


FC_HIGH_RAM_TARGET = 0x80210000
FC_ABOVE_4G_RAM_TARGET = 0x100100000


def test_fc_does_not_invalidate_translated_target(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(16, FC_HIGH_RAM_TARGET)),
        (0x20, *movl_mlx(17, 0x60)),
        (0x30, 0x00, nop_m(), mov_br_gr(1, 17),
         mov_br_gr(2, 16)),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(2)),
        (0x60, 0x10, fc(16), nop_i(), br_cond(0x60, 0x70)),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
        (FC_HIGH_RAM_TARGET, 0x10, nop_m(), adds(30, 1, 0),
         br_indirect(1)),
    ], entry=0x10, terminal_ip=0x70, memory="4G")
    if stats.get("TB invalidate count", 0) != 0:
        raise AssertionError(
            "fc incorrectly made the instruction cache coherent:\n" + output)


def test_fc_i_high_ram_invalidates_translated_target(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(16, FC_HIGH_RAM_TARGET)),
        (0x20, *movl_mlx(17, 0x60)),
        (0x30, 0x00, nop_m(), mov_br_gr(1, 17),
         mov_br_gr(2, 16)),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(2)),
        (0x60, 0x10, fc_i(16), nop_i(), br_cond(0x60, 0x70)),
        (0x70, 0x01, sync_i(), nop_i(), nop_i()),
        (0x80, 0x01, srlz_i(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
        (FC_HIGH_RAM_TARGET, 0x10, nop_m(), adds(30, 1, 0),
         br_indirect(1)),
    ], entry=0x10, terminal_ip=0x90, memory="4G")
    if stats.get("TB invalidate count", 0) < 1:
        raise AssertionError(
            "fc.i did not invalidate code in aliased high RAM:\n" + output)


def test_fc_i_above_4g_ram_invalidates_translated_target(qemu):
    stats, output = run_program_jit(qemu, [
        (0x10, *movl_mlx(16, FC_ABOVE_4G_RAM_TARGET)),
        (0x20, *movl_mlx(17, 0x60)),
        (0x30, 0x00, nop_m(), mov_br_gr(1, 17),
         mov_br_gr(2, 16)),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(2)),
        (0x60, 0x10, fc_i(16), nop_i(), br_cond(0x60, 0x70)),
        (0x70, 0x01, sync_i(), nop_i(), nop_i()),
        (0x80, 0x01, srlz_i(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
        (FC_ABOVE_4G_RAM_TARGET, 0x10, nop_m(), adds(30, 1, 0),
         br_indirect(1)),
    ], entry=0x10, terminal_ip=0x90, memory="8G")
    if stats.get("TB invalidate count", 0) < 1:
        raise AssertionError(
            "fc.i did not invalidate code above 4 GiB RAM:\n" + output)


_fc_patch_low, _fc_patch_high = bundle_words(
    0x11, nop_m(), adds(31, 2, 0), br_cond(0x100, 0x150)
)

test_fc_i_invalidates_translated_target = require_registers(
    "fc_i_invalidates_translated_target", [
        (0x10, 0x10, nop_m(), nop_i(),
         br_cond(0x10, 0x100)),
        (0x40, *movl_mlx(16, 0x100)),
        (0x50, *movl_mlx(17, _fc_patch_low)),
        (0x60, *movl_mlx(18, _fc_patch_high)),
        (0x70, 0x00, st8(16, 17), adds(19, 8, 16),
         nop_i()),
        (0x80, 0x00, st8(19, 18), nop_i(),
         nop_i()),
        (0x90, 0x10, fc_i(16), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x01, sync_i(), nop_i(),
         nop_i()),
        (0xb0, 0x01, srlz_i(), nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0x100)),
        (0x100, 0x11, nop_m(), adds(30, 1, 0),
         br_cond(0x100, 0x40)),
        (0x150, 0x10, nop_m(), nop_i(),
         br_cond(0x150, 0x150)),
    ], {
        "ip": 0x150,
        "exception": IA64_EXCP_NONE,
        "r30": 1,
        "r31": 2,
    }, entry=0x10)

_fc_line_patch_low, _fc_line_patch_high = bundle_words(
    0x11, nop_m(), adds(31, 2, 0), br_cond(0x140, 0x180)
)

test_fc_i_invalidates_translated_cache_line = require_registers(
    "fc_i_invalidates_translated_cache_line", [
        (0x10, 0x10, nop_m(), nop_i(),
         br_cond(0x10, 0x140)),
        (0x40, *movl_mlx(16, 0x140)),
        (0x50, *movl_mlx(17, _fc_line_patch_low)),
        (0x60, *movl_mlx(18, _fc_line_patch_high)),
        (0x70, *movl_mlx(20, 0x100)),
        (0x80, 0x00, cmp4_eq_imm(6, 7, 1, 30), nop_i(),
         br_cond(0x80, 0x190, qp=7)),
        (0x90, 0x00, st8(16, 17), adds(19, 8, 16),
         nop_i()),
        (0xa0, 0x00, st8(19, 18), nop_i(),
         nop_i()),
        (0xb0, 0x10, fc_i(20), nop_i(),
         br_cond(0xb0, 0xc0)),
        (0xc0, 0x01, sync_i(), nop_i(),
         nop_i()),
        (0xd0, 0x01, srlz_i(), nop_i(),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0x140)),
        (0x140, 0x11, nop_m(), adds(30, 1, 30),
         br_cond(0x140, 0x40)),
        (0x180, 0x10, nop_m(), nop_i(),
         br_cond(0x180, 0x180)),
        (0x190, 0x10, nop_m(), nop_i(),
         br_cond(0x190, 0x190)),
    ], {
        "ip": 0x180,
        "exception": IA64_EXCP_NONE,
        "r30": 1,
        "r31": 2,
    }, entry=0x10)

test_no_ic_data_access_enters_vector_with_ni = require_registers(
    "no_ic_data_access_enters_vector_with_ni",
    [
        (0x10, *movl_mlx(16, 0x40000)),
        (0x20, *movl_mlx(17, 12 << 2)),
        (0x30, *movl_mlx(18, 0x40201)),
        (0x40, *movl_mlx(20, 0x1111222233334444)),
        (0x50, *movl_mlx(21, 0x5555666677778888)),
        (0x60, 0x00, mov_m_gr_cr(16, 20), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(17, 21), adds(5, 0, 0), nop_i()),
        (0x80, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(21, 20), nop_i(), nop_i()),
        (0xb0, 0x00, ssm(1 << 17), nop_i(), nop_i()),
        (0xc0, 0x00, st8(16, 0), nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR, 0x00, mov_m_cr_gr(8, 17), nop_i(),
         nop_i()),
        (IA64_DATA_ACCESS_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 19),
         nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 20),
         nop_i(), nop_i()),
        (IA64_DATA_ACCESS_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_ACCESS_VECTOR + 0x30,
                 IA64_DATA_ACCESS_VECTOR + 0x30)),
    ],
    {
        "ip": IA64_DATA_ACCESS_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": IA64_ISR_W | IA64_ISR_NI,
        "r9": 0x1111222233334444,
        "r10": 0x5555666677778888,
    },
)

GROUP = 'mmu'
CASE_NAMES = (

    'alt_dtlb_preserves_iha',
    'alt_dtlb_when_vhpt_disabled',
    'alt_itlb_preserves_iha',
    'alt_itlb_when_vhpt_disabled',
    'alt_itlb_when_vhpt_ic_disabled',
    'br_ret_cpl_change_does_not_reuse_kernel_tlb',
    'cmpxchg4_acq_region7_store',
    'cmpxchg4_region7_store',
    'data_dirty_rfi_retries_word_store_once',
    'data_dirty_rfi_preserves_bank1_word_store',
    'data_nested_tlb_when_vhpt_ic_disabled',
    'data_physical_uc_bit_aliases_wbl_space',
    'dtlb_fault_itir_uses_region_rid',
    'dtlb_miss_slot1_resumes_without_replaying_slot0',
    'dtr_match_ignores_vrn',
    'exception_preserves_translation_bits',
    'fc_does_not_invalidate_translated_target',
    'fc_i_above_4g_ram_invalidates_translated_target',
    'fc_i_cpl3_access_rights_fault_is_nonaccess',
    'fc_i_cpl0_ignores_key_and_access_bit',
    'fc_i_high_ram_invalidates_translated_target',
    'fc_i_invalidates_translated_cache_line',
    'fc_i_invalidates_translated_target',
    'fc_i_natpage_reports_fc_nonaccess_code',
    'fc_i_page_not_present_is_not_silently_ignored',
    'fc_i_sync_i_keeps_unrelated_tb_cache',
    'fc_i_translation_miss_raises_nonaccess_alt_dtlb',
    'fc_i_unimplemented_physical_address_faults',
    'fetchadd4_alt_dtlb_sets_read_write_isr',
    'firmware_identity_ends_after_iva_handoff',
    'firmware_identity_does_not_override_user_mapping',
    'firmware_identity_under_translation',
    'firmware_iva_handoff_keeps_tb_cache',
    'firmware_runtime_identity_after_iva_handoff',
    'high_ram_above_4g_physical_and_translated_access',
    'ifetch_page_not_present_after_branch_restarts_slot0',
    'ifetch_page_not_present_fallthrough_records_faulting_iip',
    'interruption_serializes_pending_ptr_d',
    'it_only_keeps_data_physical',
    'itlb_uses_instruction_translation_after_data_fill',
    'itc_d_4g_page_size_is_insertable',
    'itc_d_clean_page_read_fill_store_raises_dirty_bit',
    'itc_d_clear_accessed_raises_data_access_bit',
    'itc_d_clear_accessed_store_precedes_access_bit',
    'itc_d_clear_dirty_raises_dirty_bit',
    'itc_d_data_key_miss_raises_key_vector',
    'itc_d_full_tc_replacement_rotates',
    'itc_d_evicted_refill_flushes_host_tlb',
    'itc_d_key_permission_store_raises_permission_vector',
    'itc_d_matching_pkr_allows_keyed_load',
    'itc_d_merced_dtlb1_cmpxchg_hit_updates_lru',
    'itc_d_merced_dtlb1_micro_hit_updates_lru',
    'itc_d_merced_main_tlb_capacity',
    'itc_d_nat_pte_consumes',
    'itc_d_not_present_raises_page_fault',
    'itc_d_not_present_rejects_low_itir_reserved_field',
    'itc_d_pl0_user_read_faults',
    'itc_d_present_reserved_itir_field_fault',
    'itc_d_present_reserved_ma_field_fault',
    'itc_d_present_reserved_pte_field_fault',
    'itc_d_preserves_24bit_key',
    'itc_d_psr_da_suppresses_one_data_access_bit',
    'itc_d_replaces_full_tc',
    'itc_d_uses_source_pte_and_cr_ifa',
    'itc_d_virtual_stack_local_passed_as_high_sol_output',
    'itc_d_mii_02_slot0_without_stop_is_illegal',
    'itc_d_mii_03_slot0_without_stop_is_illegal',
    'itc_i_m_unit_decode',
    'itc_i_present_reserved_pte_field_fault',
    'itc_i_resumes_next_slot_after_tb_exit',
    'itlb_mapping_change_keeps_reusable_tb_cache',
    'itr_d_4g_page_size_is_insertable',
    'itr_d_8k_odd_subpage_store_visible_across_call',
    'itr_d_8k_translation_uses_unrounded_paddr',
    'itr_d_all_tr_slots_survive_tc_churn',
    'itr_d_cached_translation_survives_region_register_write',
    'itr_d_madison_first_invalid_slot_faults',
    'itr_d_madison_last_slot',
    'itr_d_merced_64m_page_size',
    'itr_d_merced_first_invalid_slot_faults',
    'itr_d_merced_last_slot',
    'itr_d_nat_slot_consumes',
    'itr_d_not_present_raises_page_fault',
    'itr_d_reserved_slot_faults',
    'itr_d_slot_replacement_keeps_old_translation_cached',
    'itr_d_slot_uses_low_8_bits',
    'itr_d_uses_slot_register_value',
    'itr_i_8k_translation_uses_unrounded_paddr',
    'itr_i_cached_translation_survives_region_register_write',
    'itr_i_clear_accessed_raises_inst_access_bit',
    'itr_i_indexed_decode',
    'itr_i_instruction_key_miss_raises_key_vector',
    'itr_i_match_ignores_vrn',
    'itr_i_madison_first_invalid_slot_faults',
    'itr_i_madison_last_slot',
    'itr_i_merced_first_invalid_slot_faults',
    'itr_i_reserved_slot_faults',
    'itr_i_resumes_next_slot_after_tb_exit',
    'itr_i_slot_uses_low_8_bits',
    'itr_i_survives_region_register_write',
    'itr_i_uses_region_rid',
    'kr3_update_does_not_serialize_inflight_psr_ic',
    'lfetch_fault_checks_translation',
    'lfetch_nonfault_suppresses_translation_fault',
    'long_vhpt_dbr_matches_full_32_byte_reference',
    'long_vhpt_not_present_ignores_software_fields',
    'long_vhpt_large_page_high_ram_subword_remap',
    'long_vhpt_same_va_different_rids_refills',
    'long_vhpt_table_tlb_miss_raises_vhpt_translation',
    'long_vhpt_uncacheable_table_aborts_to_dtlb_miss',
    'long_vhpt_unsupported_page_size_aborts_to_dtlb_miss',
    'long_vhpt_walk_uses_dcr_byte_order',
    'long_vhpt_walk_uses_standard_entry_layout',
    'long_vhpt_walker_does_not_search_collision_chain',
    'natpage_instruction_fetch_raises_nat_consumption',
    'natpage_load_raises_nat_consumption',
    'natpage_short_vhpt_load_raises_nat_consumption',
    'natpage_speculative_load_defers',
    'natpage_store_raises_nat_consumption',
    'natpage_unaligned_store_outranks_unaligned',
    'natpage_xchg_raises_nat_consumption',
    'unaligned_store_reports_unaligned_when_mapped',
    'mov_pkr_duplicate_key_invalidates_old_slot',
    'mov_pkr_indexed_decode',
    'mov_pkr_merced_unimplemented_key_bit_faults',
    'mov_rr_indexed_decode',
    'mov_rr_merced_256m_page_size',
    'mov_rr_merced_64m_page_size',
    'mov_rr_merced_unimplemented_rid_bit_faults',
    'no_ic_data_access_enters_vector_with_ni',
    'percpu_alt_dtlb_uses_updated_kr3_after_ptc_e',
    'percpu_kr3_update_evicts_old_tc_mapping',
    'probe_dt_disabled_maintenance_bits_grant',
    'lfetch_fault_natpage_isr_code',
    'probe_fault_short_vhpt_not_present_raises_page_fault',
    'probe_natpage_outranks_access_bit',
    'probe_natpage_outranks_access_rights',
    'probe_natpage_outranks_dirty_bit',
    'probe_natpage_outranks_key_miss',
    'probe_natpage_reports_isr_ed_zero',
    'probe_not_present_outranks_natpage',
    'probe_r_dt_disabled_natpage_raises_nat_consumption',
    'probe_r_fault_ignored_fields_decode',
    'probe_r_dt_disabled_key_read_disable_returns_zero',
    'probe_r_fault_natpage_isr_code',
    'probe_r_fault_natpage_raises_nat_consumption',
    'probe_r_insufficient_privilege_returns_zero',
    'probe_r_natpage_dtr_raises_nat_consumption',
    'probe_r_register_level_nat_consumption',
    'probe_result_clears_destination_nat',
    'probe_rw_fault_natpage_isr_code',
    'probe_stacked_result_survives_call_return',
    'probe_w_clean_page_dirty_bit_not_checked',
    'probe_w_dt_disabled_key_miss_raises_data_key_miss',
    'probe_w_dt_disabled_key_write_disable_returns_zero',
    'probe_w_dt_disabled_ic_clear_miss_raises_data_nested_tlb',
    'probe_w_dt_disabled_miss_raises_alt_dtlb',
    'probe_w_dt_enabled_miss_raises_alt_dtlb',
    'probe_w_imm_decode',
    'probe_w_key_miss_raises_data_key_miss',
    'probe_w_natpage_short_vhpt_walk_raises_nat_consumption',
    'probe_w_register_level_nat_consumption',
    'ptc_e_nat_addr_consumes',
    'ptc_e_purges_data_tc_on_srlz_i',
    'ptc_g_source_purge_precedes_same_bundle_srlz_d',
    'ptc_ga_clears_source_alat',
    'ptc_l_4g_page_size_is_purgeable',
    'ptc_l_does_not_clear_local_alat',
    'ptc_l_keeps_nonoverlapping_tc',
    'ptc_l_nat_addr_consumes',
    'ptr_alt_decode',
    'ptr_d_nat_size_consumes',
    'ptr_d_purge_completes_on_srlz_d',
    'ptr_d_purge_invalidates_advanced_load',
    'ptr_d_purge_invalidates_warm_speculative_load',
    'ptr_i_preserves_non_overlapping_itr',
    'ptr_i_purges_matching_itr_by_address',
    'region6_high_dtr_tpa_decode',
    'region6_local_sapic_store',
    'region6_processor_interrupt_block_inta_read',
    'region6_processor_interrupt_block_xtp_store',
    'region6_short_vhpt_controls_data_mapping',
    'region6_tpa_uses_short_vhpt_mapping',
    'region6_untranslated_data_faults',
    'region6_untranslated_user_data_faults',
    'region7_dtr_controls_data_mapping',
    'region7_loader_scratch_store_load',
    'region7_nonzero_rid_requires_translation',
    'region7_untranslated_data_faults',
    'region7_untranslated_high_va_faults',
    'region7_untranslated_user_data_faults',
    'rfi_restores_translation_bits',
    'rfi_serializes_pending_ptr_d',
    'rfi_serializes_pending_ptr_i',
    'rsm_ic_inflight_dtlb_not_data_nested',
    'rsm_ic_serialized_data_nested_tlb',
    'sal_boot_nonstandard_direct_tc_is_purgeable_after_iva_handoff',
    'sal_boot_identity_coexists_with_explicit_rid_mapping',
    'sal_boot_identity_handles_nonzero_region7_rid',
    'short_vhpt_dbr_read_match_aborts_to_dtlb_miss',
    'short_vhpt_dbr_read_match_aborts_to_itlb_miss',
    'short_vhpt_dbr_read_match_precedes_not_present',
    'short_vhpt_entry_not_present_aborts_to_dtlb_miss',
    'short_vhpt_ifetch_read_only_raises_inst_access',
    'short_vhpt_not_present_entry_is_cached',
    'short_vhpt_not_present_raises_page_fault',
    'short_vhpt_reserved_pte_aborts_to_dtlb_miss',
    'short_vhpt_thash_decode',
    'short_vhpt_thash_high_region_self_map',
    'short_vhpt_thash_uses_implemented_va_bits',
    'short_vhpt_walk_uses_dcr_byte_order',
    'short_vhpt_walker_ignores_uncacheable_mapping',
    'short_vhpt_walker_reads_table_at_pl0',
    'short_vhpt_walker_rejects_pending_table_purge',
    'speculative_deferred_access_bit_yields_to_data_debug',
    'speculative_load_defers_region6_vhpt_not_present',
    'speculative_load_refills_after_region_rid_switch',
    'speculative_load_walks_short_vhpt_with_ic_clear',
    'speculative_recovery_dcr_da_defers_access_bit',
    'speculative_recovery_dcr_dd_defers_data_debug',
    'speculative_recovery_dcr_dk_defers_key_miss',
    'speculative_recovery_dcr_dm_defers_tlb_miss',
    'srlz_i_without_pending_itlb_change_keeps_tb_cache',
    'ssm_ic_inflight_dtlb_sets_ni',
    'ssm_ic_inflight_short_vhpt_entry_miss_raises_vhpt',
    'ssm_pk_invalidates_cached_keyless_access',
    'sync_i_without_pending_fc_i_keeps_tb_cache',
    'tak_nat_source_consumes_non_access',
    'tak_cpl_outranks_vm',
    'tak_nat_outranks_vm',
    'tak_not_present_dtlb_returns_one',
    'tak_unimplemented_va_does_not_alias_short_vhpt',
    'tak_uses_short_vhpt_walk',
    'tak_vhpt_access_ignores_dbr_read_match',
    'tak_vm_virtualization_fault',
    'thash_uses_pta_with_walker_disabled',
    'thash_vm_outranks_nat',
    'thash_vm_virtualization_fault',
    'tpa_dt_disabled_miss_raises_alt_dtlb',
    'tpa_dt_disabled_uses_dtlb_entry',
    'tpa_cpl_outranks_vm',
    'tpa_indexed_decode',
    'tpa_ignores_key_and_access_bit',
    'tpa_natpage_reports_nonaccess_without_read',
    'tpa_nat_outranks_vm',
    'tpa_nat_source_consumes_non_access',
    'tpa_region5_kernel_dtr_large_page',
    'tpa_uses_short_vhpt_walk',
    'tpa_vm_virtualization_fault',
    'translation_vm_predicated_off_is_nop',
    'translation_hash_m46_ignored_bits_decode',
    'translation_hash_merced_long_format',
    'translation_hash_m_unit_decode',
    'translation_hash_nat_source_rules',
    'translation_hash_ops_clear_dest_nat',
    'translation_hash_unimplemented_va_same_register_sets_nat',
    'ttag_vm_outranks_nat',
    'ttag_vm_virtualization_fault',
    'bsw_vm_virtualization_fault',
    'cover_cpl0_vm_virtualization_fault',
    'cover_cpl3_vm_executes',
    'itc_d_vm_virtualization_fault',
    'itc_ic_outranks_cpl_nat_reserved_vm',
    'itc_nat_outranks_reserved_vm',
    'itc_reserved_outranks_vm',
    'itr_d_vm_virtualization_fault',
    'mov_cr_read_vm_virtualization_fault',
    'mov_cr_cpl_outranks_reserved_vm',
    'mov_cr_ic_outranks_cpl_vm',
    'mov_cr_nat_outranks_reserved_vm',
    'mov_cr_reserved_outranks_vm',
    'mov_cpuid_read_vm_virtualization_fault',
    'mov_cr_ifa_uda_merced',
    'mov_itc_read_si_vm_virtualization_fault',
    'mov_itc_read_without_si_vm_allowed',
    'mov_itc_write_vm_virtualization_fault',
    'mov_pkr_read_vm_virtualization_fault',
    'mov_pkr_index_outranks_vm',
    'mov_rr_read_vm_virtualization_fault',
    'mov_ibr_read_vm_virtualization_fault',
    'mov_dbr_read_vm_virtualization_fault',
    'mov_pmc_read_vm_virtualization_fault',
    'mov_pmd_read_vm_is_exempt',
    'mov_pmd_write_vm_virtualization_fault',
    'mov_psr_read_masks_high_state',
    'mov_psr_read_vm_virtualization_fault',
    'mov_psrl_cpl_outranks_reserved_vm',
    'mov_psrl_nat_outranks_reserved_vm',
    'mov_psrl_reserved_outranks_vm',
    'mov_psrl_write_vm_virtualization_fault',
    'ptc_e_skips_uda_on_merced',
    'ptc_e_vm_virtualization_fault',
    'ptc_l_uda_merced',
    'ptc_l_vm_virtualization_fault',
    'ptr_d_vm_virtualization_fault',
    'rfi_vm_virtualization_fault',
    'rsm_vm_virtualization_fault',
    'ssm_cpl_outranks_reserved_vm',
    'ssm_reserved_outranks_vm',
    'ssm_vm_virtualization_fault',
    'system_vm_predicated_off_is_nop',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals())
