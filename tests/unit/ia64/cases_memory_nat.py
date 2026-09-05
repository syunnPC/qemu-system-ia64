"""Memory, ALAT, atomic, and NaT microprograms."""

from __future__ import annotations

from .case import bind_cases
from .encoding import (
    ADV_UC_LOAD_BUNDLE,
    ADV_UC_LOAD_DATA,
    ADV_UC_LOAD_VA,
    CHECK_LOAD_DATA,
    DTR_PTE_NATPAGE,
    DTR_PTE_UC,
    DTR_PTE_UCE,
    ExpectedFP,
    HIGH_TR_BASE,
    IA64_ALT_DTLB_VECTOR,
    IA64_ALT_ITLB_VECTOR,
    IA64_BREAK_VECTOR,
    IA64_EXCP_SINGLE_STEP,
    IA64_EXCP_ILLEGAL,
    IA64_EXCP_NAT_CONSUMPTION,
    IA64_EXCP_NONE,
    IA64_EXCP_UNALIGNED,
    IA64_EXCP_UNSUPPORTED_DATA_REFERENCE,
    IA64_FIRMWARE_IVT_BASE,
    IA64_GENERAL_VECTOR,
    IA64_GENEX_UNIMPL_DATA_ADDR,
    IA64_IMPL_PA_BITS,
    IA64_ISR_CODE_REG_NAT,
    IA64_ISR_CODE_SS,
    IA64_ISR_ED,
    IA64_ISR_EI_SHIFT,
    IA64_ISR_NA,
    IA64_ISR_NI,
    IA64_ISR_R,
    IA64_ISR_SP,
    IA64_ISR_W,
    IA64_NAT_CONSUMPTION_VECTOR,
    IA64_PHYS_UC_BIT,
    IA64_PSR_AC,
    IA64_PSR_BE,
    IA64_PSR_DT,
    IA64_PSR_ED,
    IA64_PSR_IC,
    IA64_PSR_IT,
    IA64_PSR_SS,
    IA64_SINGLE_STEP_VECTOR,
    IA64_UNALIGNED_VECTOR,
    IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR,
    LOW_VECTOR_ITIR,
    LOW_VECTOR_TR_PTE,
    PTE_ED,
    addl,
    adds,
    alloc,
    alloc_m,
    br_call,
    bitfield,
    br_cloop,
    br_cond,
    br_ctop_many,
    br_ret,
    break_m,
    bsw0,
    bsw1,
    bundle_words,
    chk_a_clr_m,
    chk_a_nc_m,
    chk_s_i,
    chk_s_m,
    cmp4_eq_imm,
    cmp4_eq_unc_imm,
    cmp8xchg16_acq,
    cmp8xchg16_rel,
    cmp_eq_and,
    cmp_ge_or,
    cmpxchg4_acq,
    cmpxchg_rel,
    cover_b,
    czx1_r,
    dtr_setup_bundles,
    extr_u,
    fc,
    fc_i,
    fetchadd4_acq,
    fetchadd4_rel,
    flushrs_enc,
    invala,
    invala_e_gr,
    itr_d,
    itr_i,
    ld1,
    ld16,
    ld16_acq,
    ld1_acq,
    ld1_postinc,
    ld1_reg_postinc,
    ld1_sa_postinc,
    ld2,
    ld2_c_clr_reg_update,
    ld4,
    ld4_a,
    ld4_bias,
    ld4_c_clr,
    ld4_c_clr_acq,
    ld4_c_nc,
    ld4_sa,
    ld8,
    ld8_a,
    ld8_c_clr,
    ld8_c_clr_acq,
    ld8_c_nc,
    ld8_fill_postinc,
    ld8_postinc,
    ld8_s,
    ld8_s_hint,
    ld8_s_postinc,
    ld8_sa,
    ldf8_a,
    ldf8_s,
    ldf_fill_postinc,
    ldfd,
    ldfe,
    ldfps,
    lfetch,
    lfetch_count,
    lfetch_postinc,
    lfetch_reg_postinc,
    loadrs_enc,
    load_mem,
    load_mem_postinc,
    load_mem_reg_postinc,
    mov_ar_lc,
    mov_b_gr,
    mov_cpuid,
    mov_gr_psr_full,
    mov_grpmc_indexed,
    mov_i_imm_ar,
    mov_lc_gr,
    mov_m_ar_gr,
    mov_m_cr_gr,
    mov_m_gr_ar,
    mov_m_gr_cr,
    mov_m_gr_psr_um,
    mov_m_gr_psrl,
    mov_m_imm_ar,
    mov_m_psr_gr,
    mov_pkr_indexed,
    mov_pr_rot_imm,
    mov_rr_read,
    movl_mlx,
    mux1_rev,
    nop_b,
    nop_i,
    nop_m,
    nop_x,
    or_reg,
    pack2_sss,
    pmpy2,
    pshl2,
    pshl4,
    pshr2,
    pshr4,
    raw_bundle,
    register_nat_consumption_test,
    require_exception,
    require_registers,
    reserved_m_major2,
    reserved_memory_selector,
    rfi_b,
    rfi_to_gr,
    rum,
    run_program,
    srlz_d,
    srlz_i,
    ssm,
    st16,
    st1_postinc,
    st2,
    st4,
    st4_postinc,
    st4_rel,
    st8,
    st8_postinc,
    st8_rel,
    st8_spill_postinc,
    stfd,
    stf_spill_postinc,
    store_mem,
    store_mem_postinc,
    sum_um,
    tbit_z,
    tbit_z_and,
    tbit_z_or,
    tnat_nz_and,
    tnat_nz_or,
    tnat_z_unc,
    xchg,
    xchg4,
)


test_alloc_clears_destination_nat = require_registers(
    "alloc_clears_destination_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x00, nop_m(), alloc(1, 6, 4, 0, 0),
         nop_i()),
        (0x30, 0x08, ld8_fill_postinc(34, 6, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, nop_m(), adds(35, 0, 34),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(34, 6, 4, 0, 0),
         nop_i()),
        (0x70, 0x00, nop_m(), nop_i(), nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_gr_ar(34, 64), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r34_nat": 0,
        "r35_nat": 1,
    }, entry=0x10)

test_ld1_acq_decode = require_registers("ld1_acq_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, nop_m(), ld1_acq(4, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
    (0x100, 0x00, 0x5a, 0,
     0),
], {"ip": 0x30, "r4": 0x40}, entry=0x10)

LD4_BIAS_DATA = bundle_words(0x00, 0x091a2b3c, 0, 0)[0] & 0xffffffff

test_ld4_bias_decode = require_registers("ld4_bias_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, nop_m(), ld4_bias(4, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
    (0x100, 0x00, 0x091a2b3c, 0,
     0),
], {"ip": 0x30, "r4": LD4_BIAS_DATA}, entry=0x10)
CHECK_LOAD_MISMATCH_DATA = bundle_words(0x00, 0x0fedcba987654321, 0, 0)[0]

test_ld8_c_nc_hit_preserves_target = require_registers(
    "ld8_c_nc_hit_preserves_target", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x50, "r4": 0x55}, entry=0x10)

test_ld8_c_nc_hit_consumes_nat_base = require_exception(
    "ld8_c_nc_hit_consumes_nat_base", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x40, 0x08, ld8_fill_postinc(5, 6, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8_c_nc(4, 5), nop_i(),
         nop_i()),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x200, 0x00, 0x100, 0,
         0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x50, entry=0x10)

test_ld8_c_clr_hit_clears_entry = require_registers(
    "ld8_c_clr_hit_clears_entry", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_clr(4, 3), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(4, 0xaa)),
        (0x60, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x70, "r4": CHECK_LOAD_DATA}, entry=0x10)

test_ld4_c_clr_hit_clears_entry = require_registers(
    "ld4_c_clr_hit_clears_entry", [
        (0x10, *movl_mlx(2, 0xfeedface89abcdef)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, st8(3, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, ld4_a(4, 3), nop_i(),
         nop_i()),
        (0x50, 0x00, nop_m(), adds(4, 0x55, 0),
         nop_i()),
        # On an ALAT hit, this implementation takes the architecturally
        # permitted option of leaving the target unchanged.  The .c.clr
        # operation must still remove the four-byte entry (SDM Vol. 1,
        # 4.4.5.3).
        (0x60, 0x00, ld4_c_clr(4, 3), nop_i(),
         nop_i()),
        (0x70, 0x00, nop_m(), adds(5, 0, 4),
         nop_i()),
        (0x80, 0x00, nop_m(), adds(4, 0x66, 0),
         nop_i()),
        # The cleared entry makes this check miss and reload exactly four
        # bytes, zero-extending the result into the general register.
        (0x90, 0x00, ld4_c_nc(4, 3), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r4": 0x89abcdef,
        "r5": 0x55,
    }, entry=0x10)

test_zero_alat_check_load_always_reloads = require_registers(
    "zero_alat_check_load_always_reloads", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x50, "r4": CHECK_LOAD_DATA}, entry=0x10, alat=None)

test_smp_full_alat_model_remains_enabled = require_registers(
    "smp_full_alat_model_remains_enabled", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(), nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_nc(4, 3), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0, 0),
    ], {"ip": 0x50, "r4": 0x55}, entry=0x10,
    alat="full", smp="2")

test_zero_alat_chk_a_always_branches = require_registers(
    "zero_alat_chk_a_always_branches", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, chk_a_nc_m(22, 0x30, 0x50), adds(4, 1, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x50, "r4": 0}, entry=0x10, alat=None)

test_ld8_sa_failure_invalidates_old_entry = require_registers(
    "ld8_sa_failure_invalidates_old_entry", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 0x105, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, sum_um(0x8), nop_i(),
         nop_i()),
        (0x40, 0x00, ld8_sa(4, 5), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(4, 0x55)),
        (0x60, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x70, "r4": CHECK_LOAD_DATA}, entry=0x10)

test_ld8_a_uc_zeroes_target_and_skips_alat = require_registers(
    "ld8_a_uc_zeroes_target_and_skips_alat", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(),
         nop_i()),
        (0xa0, 0x00, ld8_a(4, 2), nop_i(),
         nop_i()),
        (0xb0, 0x00, nop_m(), adds(5, 0, 4),
         nop_i()),
        (0xc0, 0x00, ld8_c_nc(4, 2), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        ADV_UC_LOAD_BUNDLE,
    ], {"ip": 0xd0, "r4": ADV_UC_LOAD_DATA, "r5": 0,
        "exception": IA64_EXCP_NONE}, entry=0x10)

test_ld8_c_nc_uc_miss_does_not_allocate_alat = require_registers(
    "ld8_c_nc_uc_miss_does_not_allocate_alat", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(), nop_i()),
        (0xa0, 0x00, ld8_c_nc(4, 2), nop_i(), nop_i()),
        (0xb0, 0x00, chk_a_nc_m(4, 0xb0, 0xd0), adds(5, 1, 0), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        ADV_UC_LOAD_BUNDLE,
    ], {"ip": 0xd0, "r4": ADV_UC_LOAD_DATA, "r5": 0,
        "exception": IA64_EXCP_NONE}, entry=0x10)

test_ld8_s_uc_defers = require_registers(
    "ld8_s_uc_defers", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(),
         nop_i()),
        (0xa0, 0x00, ld8_s(4, 2), nop_i(),
         nop_i()),
        (0xb0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xc0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        ADV_UC_LOAD_BUNDLE,
    ], {"ip": 0xd0, "r4_nat": 1,
        "exception": IA64_EXCP_NONE}, entry=0x10)

test_non_speculative_attribute_returns_failure_values = \
    require_registers(
        "non_speculative_attribute_returns_failure_values", [
            *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                               pte_flags=DTR_PTE_UC),
            (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
            (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
            (0x90, 0x08, mov_gr_psr_full(19), srlz_d(), nop_i()),
            # A UC/non-speculative attribute forces advanced loads to return
            # zero and control-speculative loads to defer.  The address is
            # aligned because fault qualification precedes that response.
            (0xa0, 0x00, ld8_a(4, 2), nop_i(), nop_i()),
            (0xb0, 0x00, ldf8_a(7, 2), nop_i(), nop_i()),
            (0xc0, 0x00, ld8_s(5, 2), nop_i(), nop_i()),
            (0xd0, 0x00, ldf8_s(8, 2), nop_i(), nop_i()),
            (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
            ADV_UC_LOAD_BUNDLE,
        ], {
            "ip": 0xe0,
            "r4": 0,
            "r4_nat": 0,
            "r5_nat": 1,
            "f7": ExpectedFP(0, 0x1003e),
            "f8": ExpectedFP(0, 0x1fffe, nat=True),
            "exception": IA64_EXCP_NONE,
        }, entry=0x10)

test_integer_advanced_non_speculative_unaligned_faults = require_exception(
    "integer_advanced_non_speculative_unaligned_faults", [
        (0x10, *movl_mlx(2, IA64_PHYS_UC_BIT | 0x101)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_AC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, ld8_a(4, 2), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x40)

test_fp_advanced_non_speculative_unaligned_faults = require_exception(
    "fp_advanced_non_speculative_unaligned_faults", [
        (0x10, *movl_mlx(2, IA64_PHYS_UC_BIT | 0x101)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_AC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, ldf8_a(7, 2), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x40)

test_ld8_c_nc_address_mismatch_reloads = require_registers(
    "ld8_c_nc_address_mismatch_reloads", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 0x110, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_nc(4, 5), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x110, 0x00, 0x0fedcba987654321, 0,
         0),
    ], {"ip": 0x50, "r4": CHECK_LOAD_MISMATCH_DATA}, entry=0x10)

test_ld8_c_clr_address_mismatch_reloads = require_registers(
    "ld8_c_clr_address_mismatch_reloads", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 0x110, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_clr(4, 5), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x110, 0x00, 0x0fedcba987654321, 0,
         0),
    ], {"ip": 0x50, "r4": CHECK_LOAD_MISMATCH_DATA}, entry=0x10)

test_ld16_loads_gr_and_csd = require_registers("ld16_loads_gr_and_csd", [
    (0x10, 0x00, addl(3, 0x100, 0), addl(4, 0x108, 0),
     nop_i()),
    (0x20, *movl_mlx(16, 0x0123456789abcdef)),
    (0x30, *movl_mlx(17, 0xfedcba9876543210)),
    (0x40, 0x00, st8(3, 16), nop_i(),
     nop_i()),
    (0x50, 0x00, st8(4, 17), nop_i(),
     nop_i()),
    (0x60, 0x00, ld16(8, 3), nop_i(),
     nop_i()),
    (0x70, 0x00, mov_m_ar_gr(9, 25), nop_i(),
     nop_i()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "r8": 0x0123456789abcdef,
    "r9": 0xfedcba9876543210,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_ld16_acq_hint_decode = require_registers("ld16_acq_hint_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), addl(4, 0x108, 0),
     nop_i()),
    (0x20, *movl_mlx(16, 0x1111222233334444)),
    (0x30, *movl_mlx(17, 0x5555666677778888)),
    (0x40, 0x00, st8(3, 16), nop_i(),
     nop_i()),
    (0x50, 0x00, st8(4, 17), nop_i(),
     nop_i()),
    (0x60, 0x00, ld16_acq(8, 3, hint=2), nop_i(),
     nop_i()),
    (0x70, 0x00, mov_m_ar_gr(9, 25), nop_i(),
     nop_i()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "r8": 0x1111222233334444,
    "r9": 0x5555666677778888,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_st16_stores_gr_and_csd = require_registers("st16_stores_gr_and_csd", [
    (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
     nop_i()),
    (0x20, *movl_mlx(15, 0x0123456789abcdef)),
    (0x30, *movl_mlx(5, 0xfedcba9876543210)),
    (0x40, 0x00, mov_m_gr_ar(5, 25), nop_i(),
     nop_i()),
    (0x50, 0x00, st16(3, 15), nop_i(),
     nop_i()),
    (0x60, 0x00, ld8(29, 3), nop_i(),
     nop_i()),
    (0x70, 0x00, ld8(30, 4), nop_i(),
     nop_i()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "r29": 0x0123456789abcdef,
    "r30": 0xfedcba9876543210,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_st16_rel_stores_gr_and_csd = require_registers(
    "st16_rel_stores_gr_and_csd", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
         nop_i()),
        (0x20, *movl_mlx(15, 0x1020304050607080)),
        (0x30, *movl_mlx(5, 0x8877665544332211)),
        (0x40, 0x00, mov_m_gr_ar(5, 25), nop_i(),
         nop_i()),
        (0x50, 0x00, st16(3, 15, x6=0x21), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8(29, 3), nop_i(),
         nop_i()),
        (0x70, 0x00, ld8(30, 4), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r29": 0x1020304050607080,
        "r30": 0x8877665544332211,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

# ld16/st16 are architecturally 16-byte aligned even when PSR.ac is clear.
test_ld16_unaligned_always_faults = require_exception(
    "ld16_unaligned_always_faults", [
        (0x10, 0x00, addl(3, 0x108, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld16(8, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

test_st16_unaligned_always_faults = require_exception(
    "st16_unaligned_always_faults", [
        (0x10, 0x00, addl(3, 0x208, 0), nop_i(), nop_i()),
        (0x20, *movl_mlx(4, 0x1122334455667788)),
        (0x30, 0x00, st16(3, 4), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x30, cpu="montecito")

# Product-specific windows differ: Merced permits an integer reference
# within one 16-byte block, whereas Madison restricts it to an 8-byte block.
test_merced_integer_load_within_16byte_window = require_registers(
    "merced_integer_load_within_16byte_window", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld4(4, 3), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="merced")

test_madison_integer_load_crossing_8byte_window_faults = require_exception(
    "madison_integer_load_crossing_8byte_window_faults", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld4(4, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

# A write-back Montecito store may span the 8-byte half-block but not the
# containing 16-byte block; loads retain the narrower 8-byte window.
test_montecito_store_within_16byte_window = require_registers(
    "montecito_store_within_16byte_window", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, *movl_mlx(4, 0x11223344)),
        (0x30, 0x00, st4(3, 4), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {"ip": 0x40, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="montecito")

test_montecito_load_crossing_8byte_window_faults = require_exception(
    "montecito_load_crossing_8byte_window_faults", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld4(4, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

test_montecito_store_crossing_16byte_window_faults = require_exception(
    "montecito_store_crossing_16byte_window_faults", [
        (0x10, 0x00, addl(3, 0x10e, 0), addl(4, 0x55, 0), nop_i()),
        (0x20, 0x00, st4(3, 4), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

# Floating-point model rules use their architected datum size rather than
# the host helper width.  In particular ldfe transfers ten bytes but is
# naturally aligned on 16 bytes.
test_madison_fp_load_within_16byte_window = require_registers(
    "madison_fp_load_within_16byte_window", [
        (0x10, 0x00, addl(3, 0x102, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfd(6, 3), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="madison")

test_madison_fp_load_crossing_16byte_window_faults = require_exception(
    "madison_fp_load_crossing_16byte_window_faults", [
        (0x10, 0x00, addl(3, 0x10c, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfd(6, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

test_montecito_fp_load_crossing_8byte_window_faults = require_exception(
    "montecito_fp_load_crossing_8byte_window_faults", [
        (0x10, 0x00, addl(3, 0x102, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfd(6, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

test_montecito_fp_store_within_16byte_window = require_registers(
    "montecito_fp_store_within_16byte_window", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, stfd(3, 1), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="montecito")

test_montecito_fp_store_crossing_16byte_window_faults = require_exception(
    "montecito_fp_store_crossing_16byte_window_faults", [
        (0x10, 0x00, addl(3, 0x10c, 0), nop_i(), nop_i()),
        (0x20, 0x00, stfd(3, 1), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

test_madison_fp_pair_requires_natural_alignment = require_exception(
    "madison_fp_pair_requires_natural_alignment", [
        (0x10, 0x00, addl(3, 0x104, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfps(6, 7, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

test_madison_fp_fill_requires_natural_alignment = require_exception(
    "madison_fp_fill_requires_natural_alignment", [
        (0x10, 0x00, addl(3, 0x108, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

test_madison_fp_spill_requires_natural_alignment = require_exception(
    "madison_fp_spill_requires_natural_alignment", [
        (0x10, 0x00, addl(3, 0x108, 0), nop_i(), nop_i()),
        (0x20, 0x00, stf_spill_postinc(3, 1, 0), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

test_madison_ldfe_within_16byte_window = require_registers(
    "madison_ldfe_within_16byte_window", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfe(6, 3), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="madison")

test_madison_ldfe_crossing_16byte_window_faults = require_exception(
    "madison_ldfe_crossing_16byte_window_faults", [
        (0x10, 0x00, addl(3, 0x107, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldfe(6, 3), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="madison")

test_montecito_uc_fp_store_crossing_8byte_window_faults = require_exception(
    "montecito_uc_fp_store_crossing_8byte_window_faults", [
        (0x10, *movl_mlx(3, IA64_PHYS_UC_BIT | 0x106)),
        (0x20, 0x00, stfd(3, 1), nop_i(), nop_i()),
    ], IA64_EXCP_UNALIGNED, fault_ip=0x20, cpu="montecito")

test_madison_speculative_model_unaligned_defers = require_registers(
    "madison_speculative_model_unaligned_defers", [
        (0x10, 0x00, addl(3, 0x106, 0), nop_i(), nop_i()),
        (0x20, 0x00, load_mem(0x06, 4, 3), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4_nat": 1, "exception": IA64_EXCP_NONE},
    entry=0x10, cpu="madison")

test_montecito_speculative_fp_model_unaligned_defers = require_registers(
    "montecito_speculative_fp_model_unaligned_defers", [
        (0x10, 0x00, addl(3, 0x102, 0), nop_i(), nop_i()),
        (0x20, 0x00, ldf8_s(6, 3), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "f6": ExpectedFP(0, 0x1fffe, nat=True),
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10, cpu="montecito")


def montecito_uc_memory_fault_test(name, fault_bundle, address,
                                   expected_isr):
    return require_registers(name, [
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, address)),
        (0x50, *movl_mlx(4, 0x1122334455667788)),
        (0x60, *fault_bundle),
        (IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR, 0x00,
         mov_m_cr_gr(14, 20), nop_i(), nop_i()),
        (IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17), nop_i(), nop_i()),
        (IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR + 0x20, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR + 0x20,
                 IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR + 0x20)),
    ], {
        "ip": IA64_UNSUPPORTED_DATA_REFERENCE_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_UNSUPPORTED_DATA_REFERENCE,
        "fault_ip": 0x60,
        "r14": address,
        "r15": expected_isr,
    }, entry=0x10)


MONTECITO_UC_16BYTE_ADDRESS = IA64_PHYS_UC_BIT | 0x2000

test_ld16_uc_unsupported_data_reference = montecito_uc_memory_fault_test(
    "ld16_uc_unsupported_data_reference",
    (0x00, ld16(8, 3), nop_i(), nop_i()),
    MONTECITO_UC_16BYTE_ADDRESS, IA64_ISR_R)

test_st16_uc_unsupported_data_reference = montecito_uc_memory_fault_test(
    "st16_uc_unsupported_data_reference",
    (0x00, st16(3, 4), nop_i(), nop_i()),
    MONTECITO_UC_16BYTE_ADDRESS, IA64_ISR_W)

test_cmp8xchg16_uc_unsupported_data_reference = \
    montecito_uc_memory_fault_test(
        "cmp8xchg16_uc_unsupported_data_reference",
        (0x00, cmp8xchg16_acq(8, 3, 4), nop_i(), nop_i()),
        MONTECITO_UC_16BYTE_ADDRESS + 8, IA64_ISR_R | IA64_ISR_W)

# Madison clears CPUID[4].ao, so the 16-byte atomic encodings are reserved.
test_ld16_madison_illegal_operation = require_exception(
    "ld16_madison_illegal_operation", [
        (0x10, 0x00, ld16(8, 3), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

test_st16_madison_illegal_operation = require_exception(
    "st16_madison_illegal_operation", [
        (0x10, 0x00, st16(3, 4), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

test_cmp8xchg16_madison_illegal_operation = require_exception(
    "cmp8xchg16_madison_illegal_operation", [
        (0x10, 0x00, cmp8xchg16_acq(8, 3, 4), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

test_memory_order_completers_decode = require_registers(
    "memory_order_completers_decode", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x300, 0),
         nop_i()),
        (0x20, *movl_mlx(10, 0x0102030405060708)),
        (0x30, 0x00, nop_m(), addl(11, 10, 0),
         nop_i()),
        (0x40, 0x00, st8_rel(3, 10), nop_i(),
         nop_i()),
        (0x50, 0x00, st4(4, 11), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8_c_clr_acq(12, 3), nop_i(),
         nop_i()),
        (0x70, 0x00, fetchadd4_rel(13, 4, 1), nop_i(),
         nop_i()),
        (0x80, 0x00, ld8(14, 4), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r12": 0x0102030405060708,
        "r13": 10,
        "r14": 11,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_data_big_endian_load_store = require_registers(
    "data_big_endian_load_store", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x201, 0),
         addl(5, 0x202, 0)),
        (0x20, 0x00, addl(6, 0x203, 0), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(16, 0x11223344)),
        (0x40, 0x00, sum_um(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0x50, 0x08, st4(3, 16), ld4(17, 3),
         nop_i()),
        (0x60, 0x00, rum(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0x70, 0x08, ld1(18, 3), ld1(19, 4),
         nop_i()),
        (0x80, 0x08, ld1(20, 5), ld1(21, 6),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r17": 0x11223344,
        "r18": 0x11,
        "r19": 0x22,
        "r20": 0x33,
        "r21": 0x44,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_data_big_endian_cmpxchg4 = require_registers(
    "data_big_endian_cmpxchg4", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x201, 0),
         addl(5, 0x202, 0)),
        (0x20, 0x00, addl(6, 0x203, 0), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(10, 0x01020304)),
        (0x40, *movl_mlx(16, 0x01020304)),
        (0x50, *movl_mlx(18, 0x11223344)),
        (0x60, 0x00, sum_um(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0x70, 0x00, st4(3, 16), nop_i(),
         nop_i()),
        (0x80, 0x00, mov_m_gr_ar(10, 32), nop_i(),
         nop_i()),
        (0x90, 0x00, cmpxchg4_acq(17, 3, 18), nop_i(),
         nop_i()),
        (0xa0, 0x00, rum(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0xb0, 0x08, ld1(19, 3), ld1(20, 4),
         nop_i()),
        (0xc0, 0x08, ld1(21, 5), ld1(22, 6),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "r17": 0x01020304,
        "r19": 0x11,
        "r20": 0x22,
        "r21": 0x33,
        "r22": 0x44,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_store_invalidates_advanced_load = require_registers(
    "store_invalidates_advanced_load", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(5, 0xfeedfacecafebeef)),
        (0x40, 0x00, st8(3, 5), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(4, 0xaa)),
        (0x60, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x70, "r4": 0xfeedfacecafebeef}, entry=0x10)


def test_store_fault_longjmp_closes_alat_writer(qemu):
    """Verify that a faulting store closes its active-writer scope."""
    data_addr = 0x300
    value = 0x123456789abcdef0

    run_program(qemu, [
        (0x10, *movl_mlx(3, 0x400000)),
        (0x20, *movl_mlx(4, 0x55)),
        # The handler retries the store; the ALAT hit checks the active count.
        (0x30, 0x00, adds(5, 0, 0), adds(6, 0x50, 0), nop_i()),
        (0x40, 0x00, ssm(IA64_PSR_IC | IA64_PSR_DT), nop_i(), nop_i()),
        (0x50, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x60, 0x00, addl(7, data_addr, 0), nop_i(), nop_i()),
        (0x70, 0x00, ld8_a(8, 7), nop_i(), nop_i()),
        (0x80, *movl_mlx(8, 0xaa)),
        (0x90, 0x00, ld8_c_nc(8, 7), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
        (IA64_ALT_DTLB_VECTOR, 0x09,
         mov_m_gr_cr(5, 16), mov_m_gr_cr(6, 19), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x11,
         nop_m(), nop_i(), rfi_b()),
        raw_bundle(data_addr, value, 0),
    ], entry=0x10, terminal_ip=0xa0, expected={
        "exception": IA64_EXCP_NONE,
        "r8": 0xaa,
    }, name="store_fault_longjmp_closes_alat_writer",
       smp="1", memory="4G")


test_rse_spill_preserves_static_alat_entry = require_registers(
    "rse_spill_preserves_static_alat_entry", [
        (0x10, *movl_mlx(2, 0x100000)),
        (0x20, 0x00, mov_m_gr_ar(2, 18), addl(3, 0x200, 0),
         nop_i()),
        (0x30, 0x00, ld8_a(4, 3), nop_i(), nop_i()),
        (0x40, *movl_mlx(4, 0x55)),
        (0x50, 0x00, alloc_m(35, 96, 0, 0, 0), nop_i(), nop_i()),
        (0x60, *movl_mlx(127, 0x123456789abcdef0)),
        (0x70, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x80, 0x00, ld8_c_nc(4, 3), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
        (0x200, 0x00, 0xfeedfacecafebeef, 0, 0),
    ], {"ip": 0x90, "r4": 0x55}, entry=0x10)

test_semaphore_ops_invalidate_advanced_loads = require_registers(
    "semaphore_ops_invalidate_advanced_loads", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x10, 0),
         nop_i()),
        (0x20, 0x08, st4(3, 4), ld4_a(5, 3),
         nop_i()),
        (0x30, 0x00, fetchadd4_acq(7, 3, 1, hint=3, ignored=0xf),
         addl(5, 0xaa, 0),
         nop_i()),
        (0x40, 0x00, ld4_c_nc(5, 3), addl(3, 0x210, 0),
         addl(4, 0x20, 0)),

        (0x50, 0x08, st4(3, 4), ld4_a(8, 3),
         addl(6, 0x33, 0)),
        (0x60, 0x00, xchg4(9, 3, 6), addl(8, 0xbb, 0),
         nop_i()),
        (0x70, 0x00, ld4_c_nc(8, 3), addl(3, 0x220, 0),
         addl(4, 0x40, 0)),

        (0x80, 0x08, st4(3, 4), ld4_a(10, 3),
         addl(6, 0x55, 0)),
        (0x90, 0x00, mov_m_imm_ar(32, 0x40), addl(10, 0xcc, 0),
         nop_i()),
        (0xa0, 0x00, cmpxchg4_acq(11, 3, 6), nop_i(),
         nop_i()),
        (0xb0, 0x00, ld4_c_nc(10, 3), addl(3, 0x230, 0),
         addl(4, 0x60, 0)),

        (0xc0, 0x08, st4(3, 4), ld4_a(12, 3),
         addl(6, 0x66, 0)),
        (0xd0, 0x00, mov_m_imm_ar(32, 0x61), addl(12, 0xdd, 0),
         nop_i()),
        (0xe0, 0x00, cmpxchg4_acq(13, 3, 6), nop_i(),
         nop_i()),
        (0xf0, 0x10, ld4_c_nc(12, 3), nop_i(),
         br_cond(0xf0, 0x100)),
        (0x100, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
    ], {"ip": 0x100, "r5": 0x11, "r7": 0x10,
        "r8": 0x33, "r9": 0x20, "r10": 0x55, "r11": 0x40,
        "r12": 0xdd, "r13": 0x60}, entry=0x10)

test_xchg4_result_base_alias_invalidates_alat = require_registers(
    "xchg4_result_base_alias_invalidates_alat", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x11, 0),
         nop_i()),
        (0x20, 0x08, st4(3, 4), ld4_a(5, 3),
         addl(6, 0x22, 0)),
        (0x30, 0x00, xchg4(3, 3, 6), addl(5, 0xaa, 0),
         nop_i()),
        (0x40, 0x00, addl(7, 0x200, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, ld4_c_nc(5, 7), nop_i(),
         nop_i()),
        (0x60, 0x00, ld4(8, 7), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {"ip": 0x70, "r3": 0x11, "r5": 0x22, "r8": 0x22},
    entry=0x10)

test_fetchadd4_result_base_alias_invalidates_alat = require_registers(
    "fetchadd4_result_base_alias_invalidates_alat", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x10, 0),
         nop_i()),
        (0x20, 0x08, st4(3, 4), ld4_a(5, 3),
         nop_i()),
        (0x30, 0x00, fetchadd4_acq(3, 3, 1), addl(5, 0xaa, 0),
         nop_i()),
        (0x40, 0x00, addl(7, 0x200, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, ld4_c_nc(5, 7), nop_i(),
         nop_i()),
        (0x60, 0x00, ld4(8, 7), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {"ip": 0x70, "r3": 0x10, "r5": 0x11, "r8": 0x11},
    entry=0x10)

test_cmpxchg4_result_base_alias_success_invalidates_alat = require_registers(
    "cmpxchg4_result_base_alias_success_invalidates_alat", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x40, 0),
         nop_i()),
        (0x20, 0x08, st4(3, 4), ld4_a(5, 3),
         addl(6, 0x55, 0)),
        (0x30, 0x00, mov_m_imm_ar(32, 0x40), nop_i(),
         nop_i()),
        (0x40, 0x00, cmpxchg4_acq(3, 3, 6), addl(5, 0xaa, 0),
         nop_i()),
        (0x50, 0x00, addl(7, 0x200, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, ld4_c_nc(5, 7), nop_i(),
         nop_i()),
        (0x70, 0x00, ld4(8, 7), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r3": 0x40, "r5": 0x55, "r8": 0x55},
    entry=0x10)

test_cmpxchg4_result_base_alias_failure_keeps_alat = require_registers(
    "cmpxchg4_result_base_alias_failure_keeps_alat", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x40, 0),
         nop_i()),
        (0x20, 0x08, st4(3, 4), ld4_a(5, 3),
         addl(6, 0x55, 0)),
        (0x30, 0x00, mov_m_imm_ar(32, 0x41), nop_i(),
         nop_i()),
        (0x40, 0x00, cmpxchg4_acq(3, 3, 6), addl(5, 0xaa, 0),
         nop_i()),
        (0x50, 0x00, addl(7, 0x200, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, ld4_c_nc(5, 7), nop_i(),
         nop_i()),
        (0x70, 0x00, ld4(8, 7), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r3": 0x40, "r5": 0xaa, "r8": 0x40},
    entry=0x10)

test_cmpxchg4_full_ar_ccv_compare = require_registers(
    "cmpxchg4_full_ar_ccv_compare", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x40, 0),
         nop_i()),
        (0x20, *movl_mlx(9, 0x100000040)),
        (0x30, 0x08, st4(3, 4), ld4_a(5, 3),
         addl(6, 0x55, 0)),
        (0x40, 0x00, mov_m_gr_ar(9, 32), addl(5, 0xaa, 0),
         nop_i()),
        (0x50, 0x00, cmpxchg4_acq(10, 3, 6), nop_i(),
         nop_i()),
        (0x60, 0x00, ld4_c_nc(5, 3), nop_i(),
         nop_i()),
        (0x70, 0x00, ld4(8, 3), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r5": 0xaa, "r8": 0x40, "r10": 0x40},
    entry=0x10)

test_semaphore_ops_clear_result_nat = require_registers(
    "semaphore_ops_clear_result_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),

        (0x20, *movl_mlx(3, 0x300)),
        (0x30, 0x00, addl(4, 0x44, 0), nop_i(),
         nop_i()),
        (0x40, 0x08, st4(3, 4), ld8_fill_postinc(7, 6, 0),
         nop_i()),
        (0x50, 0x00, fetchadd4_acq(7, 3, 1), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x00, nop_m(), nop_i(), nop_i()),

        (0x80, *movl_mlx(3, 0x310)),
        (0x90, 0x00, addl(4, 0x55, 0), addl(8, 0x66, 0),
         nop_i()),
        (0xa0, 0x08, st4(3, 4), ld8_fill_postinc(9, 6, 0),
         nop_i()),
        (0xb0, 0x00, xchg4(9, 3, 8), nop_i(),
         nop_i()),
        (0xc0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xd0, 0x00, nop_m(), nop_i(), nop_i()),

        (0xe0, *movl_mlx(3, 0x320)),
        (0xf0, 0x00, addl(4, 0x77, 0), addl(10, 0x88, 0),
         nop_i()),
        (0x100, 0x08, st4(3, 4), ld8_fill_postinc(11, 6, 0),
         nop_i()),
        (0x110, 0x00, mov_m_gr_ar(4, 32), nop_i(),
         nop_i()),
        (0x120, 0x00, cmpxchg4_acq(11, 3, 10), nop_i(),
         nop_i()),
        (0x130, 0x00, nop_m(), nop_i(), nop_i()),
        (0x140, 0x00, nop_m(), nop_i(), nop_i()),

        (0x150, *movl_mlx(3, 0x330)),
        (0x160, 0x00, addl(4, 0x99, 0), addl(10, 0xaa, 0),
         nop_i()),
        (0x170, 0x08, st4(3, 4), ld8_fill_postinc(12, 6, 0),
         nop_i()),
        (0x180, 0x00, mov_m_imm_ar(32, 0x98), nop_i(),
         nop_i()),
        (0x190, 0x00, cmpxchg4_acq(12, 3, 10), nop_i(),
         nop_i()),
        (0x1a0, 0x00, nop_m(), nop_i(), nop_i()),
        (0x1b0, 0x00, nop_m(), nop_i(), nop_i()),

        (0x1c0, 0x10, nop_m(), nop_i(),
         br_cond(0x1c0, 0x1c0)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x1c0, "r7": 0x44, "r7_nat": 0,
        "r9": 0x55, "r9_nat": 0,
        "r11": 0x77, "r11_nat": 0,
        "r12": 0x99, "r12_nat": 0}, entry=0x10)

test_fetchadd4_unaligned_sets_read_write_isr = require_registers(
    "fetchadd4_unaligned_sets_read_write_isr", [
        (0x10, 0x00, addl(3, 0x101, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x40, 0x00, fetchadd4_acq(7, 3, 1), nop_i(),
         nop_i()),
        (0x5a00, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5a10, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5a20, 0x10, nop_m(), nop_i(),
         br_cond(0x5a20, 0x5a20)),
    ], {
        "ip": 0x5a20,
        "exception": IA64_EXCP_NONE,
        "r14": 0x101,
        "r15": IA64_ISR_R | IA64_ISR_W,
    }, entry=0x10)

test_fetchadd4_nat_base_sets_read_write_isr = require_registers(
    "fetchadd4_nat_base_sets_read_write_isr", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, fetchadd4_acq(7, 3, 1), nop_i(),
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
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_R | IA64_ISR_W,
    }, entry=0x10)

NORMAL_LOAD_DATA = bundle_words(0x00, 0xdead, 0, 0)[0]

test_integer_nat_propagates_and_clears = require_registers(
    "integer_nat_propagates_and_clears", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(5, 4, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), adds(6, 1, 5),
         adds(7, 1, 0)),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x60, "r6_nat": 1, "r7_nat": 0}, entry=0x10)

test_normal_load_clears_stale_nat = require_registers(
    "normal_load_clears_stale_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(5, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x08, ld8(5, 6), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (0x200, 0x00, 0xdead, 0,
         0),
    ], {"ip": 0x40, "r5": NORMAL_LOAD_DATA, "r5_nat": 0},
    entry=0x10)

test_integer_compare_nat_source_rules = require_registers(
    "integer_compare_nat_source_rules", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(8, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 8, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, cmp4_eq_imm(6, 7, 1, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, cmp_ge_or(8, 9, 3), nop_i(),
         nop_i()),
        (0x50, 0x00, cmp4_eq_unc_imm(10, 0, 0, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, cmp4_eq_unc_imm(11, 0, 0, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, cmp_eq_and(10, 11, 0, 3), nop_i(),
         nop_i()),
        (0x80, 0x00, nop_m(), adds(4, 1, 0, qp=6),
         adds(5, 1, 0, qp=7)),
        (0x90, 0x00, nop_m(), adds(12, 1, 0, qp=8),
         adds(13, 1, 0, qp=9)),
        (0xa0, 0x00, nop_m(), adds(14, 1, 0, qp=10),
         adds(15, 1, 0, qp=11)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0xb0,
        "r4": 0,
        "r5": 0,
        "r12": 0,
        "r13": 0,
        "r14": 0,
        "r15": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_tbit_nat_source_rules = require_registers(
    "tbit_nat_source_rules", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(8, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 8, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), tbit_z(6, 7, 3, 0),
         nop_i()),
        (0x40, 0x00, nop_m(), tbit_z_or(8, 9, 3, 0),
         nop_i()),
        (0x50, 0x00, cmp4_eq_unc_imm(10, 0, 0, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, cmp4_eq_unc_imm(11, 0, 0, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, nop_m(), tbit_z_and(10, 11, 3, 0),
         nop_i()),
        (0x80, 0x00, nop_m(), adds(4, 1, 0, qp=6),
         adds(5, 1, 0, qp=7)),
        (0x90, 0x00, nop_m(), adds(12, 1, 0, qp=8),
         adds(13, 1, 0, qp=9)),
        (0xa0, 0x00, nop_m(), adds(14, 1, 0, qp=10),
         adds(15, 1, 0, qp=11)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0xb0,
        "r4": 0,
        "r5": 0,
        "r12": 0,
        "r13": 0,
        "r14": 0,
        "r15": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_normal_load_consumes_nat_base = require_exception(
    "normal_load_consumes_nat_base", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x08, ld1(4, 3), nop_i(),
         nop_i()),
        (0x200, 0x00, 0, 0,
         0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x30, entry=0x10)


test_nat_tracking_predicated_load_does_not_clear_base = require_exception(
    "nat_tracking_predicated_load_does_not_clear_base", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        # p1 is dynamically false.  Its skipped consumption check must not
        # make the following unpredicated access treat r3 as NaT-clear.
        (0x30, 0x00, ld8(4, 3, qp=1), nop_i(), nop_i()),
        (0x40, 0x00, ld8(5, 3), nop_i(), nop_i()),
        raw_bundle(0x200, 0, 0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x40, entry=0x10)


test_nat_tracking_predicated_store_does_not_clear_source = require_exception(
    "nat_tracking_predicated_store_does_not_clear_source", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(5, 6, 0), addl(7, 0x100, 0),
         nop_i()),
        # As above, but cover the store-value consumption path as well as
        # the load-address path.
        (0x30, 0x00, st8(7, 5, qp=1), nop_i(), nop_i()),
        (0x40, 0x00, st8(7, 5), nop_i(), nop_i()),
        raw_bundle(0x200, 0, 0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x40, entry=0x10)


test_nat_tracking_later_fill_overrides_clear = require_exception(
    "nat_tracking_later_fill_overrides_clear", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(3, 0x100, 0),
         nop_i()),
        (0x20, 0x10, nop_m(), addl(6, 0x200, 0),
         br_cond(0x20, 0x40)),
        # The branch makes r3 unknown to this TB.  The first load proves it
        # clear, then ld8.fill must replace that proof with its dynamic NaT.
        (0x40, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (0x50, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(), nop_i()),
        (0x60, 0x00, ld8(5, 3), nop_i(), nop_i()),
        raw_bundle(0x100, 0x1122334455667788, 0),
        raw_bundle(0x200, 0, 0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x60, entry=0x10)


test_nat_tracking_reg_postinc_reinvalidates_base = require_exception(
    "nat_tracking_reg_postinc_reinvalidates_base", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(5, 6, 0), addl(3, 0x100, 0),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x40)),
        # r3 and r5 are both unknown at this TB entry.  The access proves
        # the old base clear, but the register update assigns base.NaT |
        # increment.NaT and must therefore invalidate that proof.
        (0x40, 0x08, ld1_reg_postinc(4, 3, 5), nop_i(), nop_i()),
        (0x50, 0x00, ld1(7, 3), nop_i(), nop_i()),
        raw_bundle(0x100, 0x5a, 0),
        raw_bundle(0x200, 1, 0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x50, entry=0x10)


test_nat_tracking_load_destination_base_alias = require_registers(
    "nat_tracking_load_destination_base_alias", [
        (0x10, 0x00, nop_m(), addl(3, 0x100, 0), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x40)),
        # Marking the consumed base clear may elide the destination clear
        # when r1 == r3.  That is safe only because the old bit just proved
        # clear; verify the alias remains clear and usable by the next load.
        (0x40, 0x00, ld8(3, 3), nop_i(), nop_i()),
        (0x50, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        raw_bundle(0x100, 0x108, 0x8877665544332211),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r3": 0x108,
        "r3_nat": 0,
        "r4": 0x8877665544332211,
    }, entry=0x10)


test_nat_consumption_sets_ifa_isr = require_registers(
    "nat_consumption_sets_ifa_isr", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x08, ld1(4, 3), nop_i(),
         nop_i()),
        (0x5600, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5610, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5620, 0x10, nop_m(), nop_i(),
         br_cond(0x5620, 0x5620)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x5620, "exception": IA64_EXCP_NONE, "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_R},
    entry=0x10)

test_nat_consumption_ic0_preserves_ifa = register_nat_consumption_test(
    "nat_consumption_ic0_preserves_ifa",
    (0x00, mov_m_gr_ar(16, 65), nop_i(), nop_i()),
    IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT), enable_ic=False,
    initial_ifa=0x1122334455667788)

test_nat_store_data_consumption_is_access = require_registers(
    "nat_store_data_consumption_is_access", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(5, 6, 0), addl(7, 0x208, 0),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, st8(7, 5), nop_i(),
         nop_i()),
        (0x5600, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5610, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5620, 0x10, nop_m(), nop_i(),
         br_cond(0x5620, 0x5620)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x5620, "exception": IA64_EXCP_NONE, "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_W}, entry=0x10)

test_nat_store_base_consumption_is_access = require_registers(
    "nat_store_base_consumption_is_access", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), addl(5, 0x55, 0),
         nop_i()),
        (0x30, 0x00, ssm(1 << 13), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, st8(3, 5), nop_i(),
         nop_i()),
        (0x5600, 0x00, mov_m_cr_gr(14, 20), nop_i(),
         nop_i()),
        (0x5610, 0x00, mov_m_cr_gr(15, 17), nop_i(),
         nop_i()),
        (0x5620, 0x10, nop_m(), nop_i(),
         br_cond(0x5620, 0x5620)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x5620, "exception": IA64_EXCP_NONE, "r14": 0,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_W}, entry=0x10)

test_speculative_load_defers_nat_base = require_registers(
    "speculative_load_defers_nat_base", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x08, ld8_s(4, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x60, "exception": IA64_EXCP_NONE, "r4_nat": 1},
    entry=0x10)

test_nat_clear_tb_speculative_exit_rechecks_flags = require_exception(
    "nat_clear_tb_speculative_exit_rechecks_flags", [
        (0x10, *movl_mlx(4, 0x200)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_AC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x70)),
        # Execute this TB twice.  Its first speculative load succeeds and
        # reaches the all-NaT-clear target variant; its second load defers an
        # unimplemented-address fault and must look up the generic variant.
        (0x70, 0x00, ld8_s(3, 4), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0xa0)),
        (0xa0, 0x00, ld1(5, 3), nop_i(), nop_i()),
        (0xb0, *movl_mlx(4, 0x76520ec5b2369f9e)),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0x70)),
        raw_bundle(0x200, 0x300, 0),
        raw_bundle(0x300, 0x5a, 0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0xa0, entry=0x10)

test_nat_clear_self_loop_prefix_rechecks_facts = require_registers(
    "nat_clear_self_loop_prefix_rechecks_facts", [
        (0x10, *movl_mlx(3, 0x76520ec5b2369f9e)),
        (0x20, 0x00, nop_m(), adds(8, 1, 0), nop_i()),
        (0x30, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
        # The clear-specialized TB starts before this speculative load.  The
        # self-loop must use the NaT facts at its label, not the TB entry flag.
        (0x40, 0x00, ld8_s(34, 3), nop_i(), nop_i()),
        (0x50, 0x10, adds(33, 0, 32), adds(32, 0, 34),
         br_cloop(0x50, 0x50)),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r32_nat": 1,
        "r33_nat": 1,
    }, entry=0x10)

test_speculative_load_defers_psr_ed = require_registers(
    "speculative_load_defers_psr_ed", [
        (0x10, *movl_mlx(16, IA64_PSR_ED)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(17, 0x100)),
        (0x40, 0x00, mov_m_gr_cr(16, 16), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(17, 19), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x100, 0x00, ld8_s(4, 3), nop_i(),
         nop_i()),
        (0x110, 0x00, nop_m(), nop_i(), nop_i()),
        (0x120, 0x00, nop_m(), nop_i(), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
        (0x200, 0x00, 0x12345678, 0,
         0),
    ], {"ip": 0x130, "exception": IA64_EXCP_NONE, "r4_nat": 1},
    entry=0x10)

test_speculative_load_no_recovery_tlb_miss_faults = require_registers(
    "speculative_load_no_recovery_tlb_miss_faults", [
        (0x10, *movl_mlx(2, 0xa000000100020000)),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x00, ld8_s(4, 2), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(30, 19),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 0x70,
        "r31": IA64_ISR_R | IA64_ISR_SP,
    }, entry=0x10)

test_speculative_load_handler_psr_ed_defers_retry = require_registers(
    "speculative_load_handler_psr_ed_defers_retry", [
        (0x10, *movl_mlx(2, 0xa000000100020000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8_s_postinc(4, 2, 8), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_psr_gr(8), nop_i(),
         nop_i()),
        (0x70, 0x00, nop_m(), nop_i(), nop_i()),
        (0x80, 0x00, nop_m(), tbit_z(3, 4, 8, 43),
         nop_i()),
        (0x90, 0x00, nop_m(), addl(9, 1, 0, qp=3),
         addl(10, 1, 0, qp=4)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(20, 16),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, *movl_mlx(21, IA64_PSR_ED)),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00, nop_m(),
         or_reg(20, 20, 21), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 16),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r2": 0xa000000100020008,
        "r4_nat": 1,
        "r9": 1,
        "r10": 0,
    }, entry=0x10)

test_firmware_alt_dtlb_speculative_load_defers = require_registers(
    "firmware_alt_dtlb_speculative_load_defers", [
        (0x10, *movl_mlx(3, 0x0000101e18001880)),
        (0x20, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
        (0x30, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x40, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x60, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x70, 0x00, ld8_s_postinc(4, 3, 8), nop_i(), nop_i()),
        (0x80, 0x00, mov_m_psr_gr(8), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), tbit_z(3, 4, 8, 43), nop_i()),
        (0xb0, 0x00, nop_m(), addl(9, 1, 0, qp=3),
         addl(10, 1, 0, qp=4)),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r3": 0x0000101e18001888,
        "r4_nat": 1,
        "r9": 1,
        "r10": 0,
    }, entry=0x10)

test_firmware_alt_dtlb_nonspeculative_load_installs_identity_tc = \
    require_registers(
        "firmware_alt_dtlb_nonspeculative_load_installs_identity_tc", [
            (0x10, *movl_mlx(3, 0x3000)),
            (0x20, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
            (0x30, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
            (0x40, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
            (0x50, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
            (0x60, 0x00, srlz_d(), nop_i(), nop_i()),
            (0x70, 0x00, ld8(4, 3), nop_i(), nop_i()),
            (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
            raw_bundle(0x3000, 0x1122334455667788,
                       0x8877665544332211),
        ], {
            "ip": 0x80,
            "exception": IA64_EXCP_NONE,
            "r4": 0x1122334455667788,
        }, entry=0x10)

test_firmware_alt_dtlb_nonspeculative_load_faults = require_registers(
    "firmware_alt_dtlb_nonspeculative_load_faults", [
        (0x10, *movl_mlx(3, 1 << IA64_IMPL_PA_BITS)),
        (0x20, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
        (0x30, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x40, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x50, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x60, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x70, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE | IA64_ALT_DTLB_VECTOR, 0x00,
         mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE | IA64_ALT_DTLB_VECTOR | 0x10, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_FIRMWARE_IVT_BASE | IA64_ALT_DTLB_VECTOR | 0x10,
                 IA64_FIRMWARE_IVT_BASE | IA64_ALT_DTLB_VECTOR | 0x10)),
    ], {
        "ip": IA64_FIRMWARE_IVT_BASE | IA64_ALT_DTLB_VECTOR | 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": IA64_ISR_R,
    }, entry=0x10)

test_speculative_unaligned_no_recovery_faults = require_registers(
    "speculative_unaligned_no_recovery_faults", [
        (0x10, 0x00, nop_m(), addl(3, 0x104, 0),
         nop_i()),
        (0x20, *movl_mlx(19, (1 << 13) | (1 << 3))),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8_s(4, 3), nop_i(),
         nop_i()),
        (IA64_UNALIGNED_VECTOR, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_UNALIGNED_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_UNALIGNED_VECTOR + 0x10,
                 IA64_UNALIGNED_VECTOR + 0x10)),
    ], {
        "ip": IA64_UNALIGNED_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": IA64_ISR_R | IA64_ISR_SP,
    }, entry=0x10)

test_speculative_unimplemented_physical_unaligned_defers = require_registers(
    "speculative_unimplemented_physical_unaligned_defers", [
        (0x10, *movl_mlx(3, 0x76520ec5b2369f9e)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_AC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ld8_s(4, 3), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r4_nat": 1,
    }, entry=0x10)

test_unimplemented_physical_load_faults = require_registers(
    "unimplemented_physical_load_faults", [
        (0x10, *movl_mlx(3, 1 << IA64_IMPL_PA_BITS)),
        (0x20, *movl_mlx(19, IA64_PSR_IC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x30,
                 IA64_GENERAL_VECTOR + 0x30)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R,
        "r10": 1 << IA64_IMPL_PA_BITS,
    }, entry=0x10)

test_unimplemented_physical_load_merced_44bit = require_registers(
    "unimplemented_physical_load_merced_44bit", [
        (0x10, *movl_mlx(3, 1 << 44)),
        (0x20, *movl_mlx(19, IA64_PSR_IC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x30,
                 IA64_GENERAL_VECTOR + 0x30)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R,
        "r10": 1 << 44,
    }, entry=0x10, cpu="merced")

test_unimplemented_virtual_load_merced_54bit = require_registers(
    "unimplemented_virtual_load_merced_54bit", [
        (0x10, *movl_mlx(3, 1 << 51)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x30,
                 IA64_GENERAL_VECTOR + 0x30)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R,
        "r10": 1 << 51,
    }, entry=0x10, cpu="merced")

test_unimplemented_physical_precludes_unaligned = require_registers(
    "unimplemented_physical_precludes_unaligned", [
        (0x10, *movl_mlx(3, (1 << IA64_IMPL_PA_BITS) | 1)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_AC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, ld8(4, 3), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x30,
                 IA64_GENERAL_VECTOR + 0x30)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R,
        "r10": (1 << IA64_IMPL_PA_BITS) | 1,
    }, entry=0x10)

test_speculative_recovery_unaligned_defers = require_registers(
    "speculative_recovery_unaligned_defers", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, 0x00, nop_m(), addl(3, 0x104, 0),
         nop_i()),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 36) | (1 << 3))),
        (0x40, 0x00, adds(7, LOW_VECTOR_ITIR, 0), adds(5, 5, 0),
         nop_i()),
        (0x50, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_i(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0x80, 19, 31),
        (0x4000430, 0x00, ld8_s(31, 3), nop_i(),
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

_SPLIT_TLB_CODE_VA = 0x000006fb7c447750
_SPLIT_TLB_CODE_PAGE = _SPLIT_TLB_CODE_VA & ~0x1fff
_SPLIT_TLB_CODE_PHYS_PAGE = 0x400000
_SPLIT_TLB_CODE_PHYS = (
    _SPLIT_TLB_CODE_PHYS_PAGE +
    (_SPLIT_TLB_CODE_VA & 0x1fff)
)
_SPLIT_TLB_CODE_PTE = (
    PTE_ED | _SPLIT_TLB_CODE_PHYS_PAGE | 0x661
)

# Use a fixed raw IA-64 bundle as the regression input.  Its ld8.sa accepts
# an unaligned pointer, defers the Madison alignment fault through the code
# PTE's ED bit, and produces a NaT for a later recovery load.  Warm the data
# soft-TLB at the code VA before fetching it: an incorrectly shared I/D entry
# grants execute permission, bypasses the modeled ITLB and loses the
# code-page ED.  The Alternate ITLB handler is consequently reached only by
# a correctly separated I/D soft-TLB and installs the instruction translation
# used by the bundle.
test_ld8_sa_instruction_ed_survives_data_soft_tlb_warmup = \
    require_registers(
        "ld8_sa_instruction_ed_survives_data_soft_tlb_warmup", [
            # Keep the interruption vectors executable and the unaligned
            # test pointer readable while translated execution is enabled.
            # Every synthetic setup bundle ends its instruction group so
            # register/control-register dependencies and the itr/srlz pairs
            # obey SDM Vol. 2 section 3.2.2 serialization requirements.
            (0x10, 0x05, *movl_mlx(17, PTE_ED | 0x661)[1:]),
            (0x20, 0x05, *movl_mlx(20, 0)[1:]),
            (0x30, 0x01, mov_m_gr_cr(20, 20), adds(7, 16 << 2, 0),
             nop_i()),
            (0x40, 0x01, mov_m_gr_cr(7, 21), adds(6, 6, 0), nop_i()),
            (0x50, 0x01, itr_i(6, 17), nop_i(), nop_i()),
            (0x60, 0x01, itr_d(6, 17), nop_i(), nop_i()),
            (0x70, 0x01, srlz_i(), nop_i(), nop_i()),
            (0x80, 0x01, srlz_d(), nop_i(), nop_i()),

            # Install only the target's data translation, then touch its
            # bytes as data to seed QEMU's shared soft-TLB comparator.
            (0x90, 0x05,
             *movl_mlx(18, _SPLIT_TLB_CODE_PTE)[1:]),
            (0xa0, 0x05,
             *movl_mlx(20, _SPLIT_TLB_CODE_PAGE)[1:]),
            (0xb0, 0x01, mov_m_gr_cr(20, 20), adds(7, 13 << 2, 0),
             nop_i()),
            (0xc0, 0x01, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
            (0xd0, 0x01, itr_d(5, 18), nop_i(), nop_i()),
            (0xe0, 0x01, srlz_d(), nop_i(), nop_i()),
            (0xf0, 0x05,
             *movl_mlx(2, _SPLIT_TLB_CODE_VA)[1:]),
            (0x100, 0x05, *movl_mlx(19, IA64_PSR_DT)[1:]),
            (0x110, 0x01, mov_gr_psr_full(19), nop_i(), nop_i()),
            (0x120, 0x01, srlz_d(), nop_i(), nop_i()),
            (0x130, 0x00, ld8(8, 2), nop_i(), nop_i()),

            (0x140, 0x01, nop_m(), alloc(2, 5, 4, 0, 0), nop_i()),
            (0x150, 0x05, *movl_mlx(36, 0x105)[1:]),
            (0x160, 0x05, *movl_mlx(
                19, IA64_PSR_IC | IA64_PSR_IT |
                IA64_PSR_DT | IA64_PSR_AC)[1:]),
            (0x170, 0x05,
             *movl_mlx(31, _SPLIT_TLB_CODE_VA)[1:]),
            *rfi_to_gr(0x180, 19, 31),

            raw_bundle(_SPLIT_TLB_CODE_PHYS,
                       0x01d005801a515808, 0x5f000403e020f090),
            (_SPLIT_TLB_CODE_PHYS + 0x10, 0x10,
             nop_m(), nop_i(),
             br_cond(_SPLIT_TLB_CODE_VA + 0x10,
                     _SPLIT_TLB_CODE_VA + 0x10)),

            # A split soft-TLB rejects the warmed data entry for execution,
            # so service the expected initial instruction-translation miss.
            (IA64_ALT_ITLB_VECTOR, 0x01,
             mov_m_gr_cr(7, 21), nop_i(), nop_i()),
            (IA64_ALT_ITLB_VECTOR + 0x10, 0x01,
             itr_i(5, 18), nop_i(), nop_i()),
            (IA64_ALT_ITLB_VECTOR + 0x20, 0x01,
             srlz_i(), nop_i(), nop_i()),
            (IA64_ALT_ITLB_VECTOR + 0x30, 0x10,
             nop_m(), nop_i(), rfi_b()),
            (IA64_UNALIGNED_VECTOR, 0x00, nop_m(), nop_i(), nop_i()),
            (IA64_UNALIGNED_VECTOR + 0x10, 0x10,
             nop_m(), nop_i(),
             br_cond(IA64_UNALIGNED_VECTOR + 0x10,
                     IA64_UNALIGNED_VECTOR + 0x10)),
        ], {
            "ip": _SPLIT_TLB_CODE_VA + 0x10,
            "exception": IA64_EXCP_NONE,
            "r8": 0x01d005801a515808,
            "r29_nat": 1,
        }, entry=0x10, cpu="madison")

test_unaligned_check_load_sets_isr_ed = require_registers(
    "unaligned_check_load_sets_isr_ed", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE | PTE_ED)),
        (0x20, 0x00, nop_m(), addl(3, 0x102, 0),
         nop_i()),
        (0x30, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_IT | IA64_PSR_AC)),
        (0x40, 0x00, adds(7, 16 << 2, 0), adds(5, 5, 0),
         nop_i()),
        (0x50, 0x08, mov_m_gr_cr(7, 21), mov_m_gr_cr(0, 20),
         nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, srlz_i(), adds(31, 0x430, 0),
         nop_i()),
        *rfi_to_gr(0x80, 19, 31),
        (0x4000430, 0x00, ld4_sa(30, 3), nop_i(),
         nop_i()),
        (0x4000440, 0x00, ld4_c_clr(30, 3), nop_i(),
         nop_i()),
        (0x4000000 + IA64_UNALIGNED_VECTOR, 0x00,
         mov_m_cr_gr(14, 20),
         nop_i(), nop_i()),
        (0x4000000 + IA64_UNALIGNED_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(15, 17),
         nop_i(), nop_i()),
        (0x4000000 + IA64_UNALIGNED_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(16, 22),
         nop_i(), nop_i()),
        (0x4000000 + IA64_UNALIGNED_VECTOR + 0x30, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_UNALIGNED_VECTOR + 0x30,
                 IA64_UNALIGNED_VECTOR + 0x30)),
    ], {
        "ip": IA64_UNALIGNED_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "r14": 0x102,
        "r15": IA64_ISR_R | IA64_ISR_ED,
        "r16": 0x430,
    }, entry=0x10)

test_unaligned_check_load_full_alat_hit_skips_access = require_registers(
    "unaligned_check_load_full_alat_hit_skips_access", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE + 0x102, 0x400102),
        (0x70, *movl_mlx(3, HIGH_TR_BASE + 0x102)),
        (0x80, 0x00, ssm(IA64_PSR_DT), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        # Allocate while AC is clear, then enable alignment checking before
        # the check load.  An ALAT hit performs no misaligned memory access.
        (0xa0, 0x00, ld4_sa(22, 3), nop_i(), nop_i()),
        (0xb0, *movl_mlx(22, 0x55)),
        (0xc0, 0x00, sum_um(IA64_PSR_AC), nop_i(), nop_i()),
        (0xd0, 0x00, ld4_c_clr(22, 3), nop_i(), nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
        (0x400100, 0x00, 0x1122334455667788, 0, 0),
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r22": 0x55,
    }, entry=0x10)

test_ld8_s_d2_hint_decode = require_registers("ld8_s_d2_hint_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, ld8_s_hint(4, 3, 2), nop_i(),
     nop_i()),
    (0x30, 0x00, nop_m(), nop_i(), nop_i()),
    (0x40, 0x00, nop_m(), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
    (0x100, 0x00, 0x1122334455667788, 0,
     0),
], {"ip": 0x50, "exception": IA64_EXCP_NONE, "r4_nat": 1}, entry=0x10)

test_mov_crgr_clears_stale_nat = require_registers(
    "mov_crgr_clears_stale_nat", [
        (0x10, 0x00, addl(3, 0x104, 0), addl(5, 0x200, 0),
         nop_i()),
        (0x20, 0x00, sum_um(0x8), addl(16, 0x188, 0),
         nop_i()),
        (0x30, 0x00, ld8_s(28, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(16, 20), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_cr_gr(28, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, st8(5, 28), nop_i(),
         nop_i()),
        (0x70, 0x00, ld8(31, 5), nop_i(),
         nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {"ip": 0xa0, "exception": IA64_EXCP_NONE, "r28_nat": 0,
        "r31": 0x188}, entry=0x10)

test_ld1_postinc_decode = require_registers("ld1_postinc_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, ld1_postinc(4, 3, 1), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
    (0x100, 0x00, 0x5a, 0,
     0),
], {"ip": 0x30, "r3": 0x101, "r4": 0x40}, entry=0x10)

test_ld1_reg_postinc_decode = require_registers(
    "ld1_reg_postinc_decode", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 1, 0),
         nop_i()),
        (0x20, 0x08, ld1_reg_postinc(4, 3, 5), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
        (0x100, 0x00, 0x5a, 0,
        0),
    ], {"ip": 0x30, "r3": 0x101, "r4": 0x40}, entry=0x10)

test_ld1_reg_postinc_uses_old_increment = require_registers(
    "ld1_reg_postinc_uses_old_increment", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 1, 0),
         nop_i()),
        (0x20, 0x08, ld1_reg_postinc(5, 3, 5), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
        (0x100, 0x00, 0x5a, 0,
         0),
    ], {"ip": 0x30, "r3": 0x101, "r5": 0x40}, entry=0x10)

test_ld_reg_postinc_same_target_illegal = require_exception(
    "ld_reg_postinc_same_target_illegal", [
        (0x10, 0x08, ld1_reg_postinc(3, 3, 5), nop_i(),
         nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_ld_imm_postinc_same_target_illegal = require_exception(
    "ld_imm_postinc_same_target_illegal", [
        (0x10, 0x08, ld1_postinc(3, 3, 0), nop_i(),
         nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_ld_postinc_same_target_predicated_false = require_registers(
    "ld_postinc_same_target_predicated_false", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x08, ld1_postinc(3, 3, 0, qp=1), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
        (0x100, 0x00, 0x5a, 0,
         0),
    ], {"ip": 0x30, "r3": 0x100, "exception": IA64_EXCP_NONE}, entry=0x10)

test_ld1_sa_postinc_decode = require_registers("ld1_sa_postinc_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, ld1_sa_postinc(4, 3, 2), nop_i(),
     nop_i()),
    (0x30, 0x00, nop_m(), nop_i(), nop_i()),
    (0x40, 0x00, nop_m(), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
    (0x100, 0x00, 0x5a, 0,
     0),
], {"ip": 0x50, "r3": 0x102, "r4_nat": 1},
   entry=0x10)

test_ld8_nt1_postinc_decode = require_registers("ld8_nt1_postinc_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, ld8_postinc(4, 3, 8, hint=1), nop_i(),
     nop_i()),
    (0x30, 0x08, ld8_s_postinc(5, 3, 8, hint=1), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
    (0x100, 0x00, 0x12345678, 0,
     0),
], {"ip": 0x40, "r3": 0x110, "exception": IA64_EXCP_NONE}, entry=0x10)

MEMORY_HINT_LD2_DATA = bundle_words(0x00, 0x1234, 0, 0)[0] & 0xffff
MEMORY_HINT_XCHG_OLD = bundle_words(0x00, 0x12, 0, 0)[0] & 0xff
MEMORY_HINT_CMPXCHG_OLD = bundle_words(0x00, 0x3344, 0, 0)[0] & 0xffff
MEMORY_HINT_LD4_ACQ_DATA = bundle_words(0x00, 0x778899aa, 0, 0)[0] & 0xffffffff

test_memory_cache_hints_decode = require_registers(
    "memory_cache_hints_decode", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 2, 0),
         nop_i()),
        (0x20, 0x00, ld2_c_clr_reg_update(4, 3, 5, hint=3), addl(6, 0xab, 0),
         addl(8, 0x200, 0)),
        (0x30, 0x00, xchg(0, 7, 8, 6, hint=3), nop_i(),
         nop_i()),
        (0x40, 0x00, ld1(9, 8), addl(10, 0x210, 0),
         addl(11, 0x5566, 0)),
        (0x50, 0x00, addl(14, MEMORY_HINT_CMPXCHG_OLD, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_ar(14, 32), nop_i(),
         nop_i()),
        (0x70, 0x00, cmpxchg_rel(1, 12, 10, 11, hint=1), nop_i(),
         nop_i()),
        (0x80, 0x00, ld2(13, 10), addl(15, 0x220, 0),
         nop_i()),
        (0x90, 0x10, ld4_c_clr_acq(16, 15, hint=3), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, 0x1234, 0,
         0),
        (0x200, 0x00, 0x12, 0,
         0),
        (0x210, 0x00, 0x3344, 0,
         0),
        (0x220, 0x00, 0x778899aa, 0,
         0),
    ], {
        "ip": 0xa0,
        "r3": 0x102,
        "r4": MEMORY_HINT_LD2_DATA,
        "r7": MEMORY_HINT_XCHG_OLD,
        "r9": 0xab,
        "r12": MEMORY_HINT_CMPXCHG_OLD,
        "r13": 0x5566,
        "r16": MEMORY_HINT_LD4_ACQ_DATA,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_ld8_fill_st8_spill_postinc_decode = require_registers(
    "ld8_fill_st8_spill_postinc_decode", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(16, 0x123456789abcdef0)),
        (0x30, 0x00, st8_spill_postinc(3, 16, 16), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(16, 0)),
        (0x50, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8_fill_postinc(17, 3, 16), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r3": 0x110,
        "r16": 0,
        "r17": 0x123456789abcdef0,
    }, entry=0x10)

test_ld8_fill_restores_unat_bit = require_registers(
    "ld8_fill_restores_unat_bit", [
        (0x10, *movl_mlx(9, 1 << 32)),
        (0x20, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x100, 0),
         nop_i()),
        (0x30, 0x00, ld8_fill_postinc(17, 3, 0), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r17_nat": 1,
        "ar_unat": 1 << 32,
    }, entry=0x10)

test_st8_spill_updates_unat_bit = require_registers(
    "st8_spill_updates_unat_bit", [
        (0x10, *movl_mlx(9, 1 << 32)),
        (0x20, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x100, 0),
         nop_i()),
        (0x30, *movl_mlx(16, 0x123456789abcdef0)),
        (0x40, 0x00, st8_spill_postinc(3, 16, 16), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {"ip": 0x50, "ar_unat": 0}, entry=0x10)

test_st8_spill_nated_source_writes_zero = require_registers(
    "st8_spill_nated_source_writes_zero", [
        (0x10, *movl_mlx(9, 1 << 32)),
        (0x20, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x100, 0),
         nop_i()),
        (0x30, *movl_mlx(16, 0x123456789abcdef0)),
        (0x40, 0x00, st8(3, 16), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8_fill_postinc(16, 3, 8), nop_i(),
         nop_i()),
        (0x60, 0x00, st8_spill_postinc(3, 16, 8), nop_i(),
         nop_i()),
        (0x70, 0x00, addl(3, 0x108, 0), nop_i(),
         nop_i()),
        (0x80, 0x00, ld8(17, 3), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r16": 0x123456789abcdef0,
        "r16_nat": 1,
        "r17": 0,
        "ar_unat": (1 << 32) | (1 << 33),
    }, entry=0x10)

test_integer_postinc_imm9_decode = require_registers(
    "integer_postinc_imm9_decode", [
        (0x10, 0x00, addl(3, 0x300, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(16, 0x8877665544332211)),
        (0x30, 0x00, st8_spill_postinc(3, 16, 176), nop_i(),
         nop_i()),
        (0x40, 0x00, nop_m(), adds(4, 0, 3),
         nop_i()),
        (0x50, 0x00, addl(3, 0x300, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, ld8_fill_postinc(17, 3, -200), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r3": 0x238,
        "r4": 0x3b0,
        "r17": 0x8877665544332211,
    }, entry=0x10)

test_st1_postinc_decode = require_registers("st1_postinc_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), adds(4, 0x5a, 0),
     nop_i()),
    (0x20, 0x08, st1_postinc(3, 4, -1), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r3": 0xff}, entry=0x10)

test_st8_postinc_same_base_value_uses_old_base = require_registers(
    "st8_postinc_same_base_value_uses_old_base", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(5, 0x200, 0),
         nop_i()),
        (0x20, 0x08, st8_postinc(3, 3, 8), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8(4, 5), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r3": 0x208,
        "r4": 0x200,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_cmpxchg4_uses_ar_ccv = require_registers("cmpxchg4_uses_ar_ccv", [
    (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0xff, 0),
     nop_i()),
    (0x20, 0x00, st4(3, 4), addl(6, 0xf7, 0),
     nop_i()),
    (0x30, 0x00, mov_m_gr_ar(4, 32), nop_i(),
     nop_i()),
    (0x40, 0x00, cmpxchg4_acq(5, 3, 6), nop_i(),
     nop_i()),
    (0x50, 0x00, load_mem(0x02, 7, 3), nop_i(),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r5": 0xff,
    "r7": 0xf7,
    "ar_ccv": 0xff,
}, entry=0x10)

test_xchg4_decode = require_registers("xchg4_decode", [
    (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0xff, 0),
     nop_i()),
    (0x20, 0x00, st4(3, 4), addl(6, 0xf7, 0),
     nop_i()),
    (0x30, 0x00, xchg4(5, 3, 6), nop_i(),
     nop_i()),
    (0x40, 0x00, load_mem(0x02, 7, 3), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r5": 0xff, "r7": 0xf7}, entry=0x10)

test_cmpxchg4_repeated_word_updates = require_registers(
    "cmpxchg4_repeated_word_updates", [
        (0x10, *movl_mlx(3, 0x200)),
        (0x20, *movl_mlx(4, 0xffffffff)),
        (0x30, *movl_mlx(6, 0xfffffffe)),
        (0x40, *movl_mlx(7, 0xfffffffc)),
        (0x50, *movl_mlx(8, 0xfffffff8)),
        (0x60, 0x00, st4(3, 4), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_ar(4, 32), nop_i(),
         nop_i()),
        (0x80, 0x00, cmpxchg4_acq(5, 3, 6), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_ar(6, 32), nop_i(),
         nop_i()),
        (0xa0, 0x00, cmpxchg4_acq(9, 3, 7), nop_i(),
         nop_i()),
        (0xb0, 0x00, mov_m_gr_ar(7, 32), nop_i(),
         nop_i()),
        (0xc0, 0x00, cmpxchg4_acq(10, 3, 8), nop_i(),
         nop_i()),
        (0xd0, 0x00, load_mem(0x02, 11, 3), nop_i(),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
    ], {
        "ip": 0xe0,
        "r5": 0xffffffff,
        "r9": 0xfffffffe,
        "r10": 0xfffffffc,
        "r11": 0xfffffff8,
    }, entry=0x10)

test_cmp8xchg16_acq_stores_pair = require_registers(
    "cmp8xchg16_acq_stores_pair", [
        (0x10, *movl_mlx(20, 0x200)),
        (0x20, *movl_mlx(21, 0x208)),
        (0x30, *movl_mlx(4, 0x1111111122222222)),
        (0x40, *movl_mlx(5, 0x3333333344444444)),
        (0x50, *movl_mlx(6, 0x5555555566666666)),
        (0x60, *movl_mlx(7, 0x7777777788888888)),
        (0x70, 0x09, st8(20, 4), st8(21, 5),
         nop_i()),
        (0x80, 0x09, mov_m_gr_ar(5, 32), mov_m_gr_ar(7, 25),
         nop_i()),
        (0x90, 0x00, cmp8xchg16_acq(8, 21, 6, hint=3), nop_i(),
         nop_i()),
        (0xa0, 0x08, ld8(9, 20), ld8(10, 21),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "r8": 0x3333333344444444,
        "r9": 0x5555555566666666,
        "r10": 0x7777777788888888,
    }, entry=0x10)

test_cmp8xchg16_rel_mismatch_keeps_pair = require_registers(
    "cmp8xchg16_rel_mismatch_keeps_pair", [
        (0x10, *movl_mlx(20, 0x240)),
        (0x20, *movl_mlx(21, 0x248)),
        (0x30, *movl_mlx(4, 0xaaaaaaaa55555555)),
        (0x40, *movl_mlx(5, 0xbbbbbbbb66666666)),
        (0x50, *movl_mlx(6, 0xcccccccc77777777)),
        (0x60, *movl_mlx(7, 0xdddddddd88888888)),
        (0x70, 0x09, st8(20, 4), st8(21, 5),
         nop_i()),
        (0x80, 0x09, mov_m_gr_ar(6, 32), mov_m_gr_ar(7, 25),
         nop_i()),
        (0x90, 0x00, cmp8xchg16_rel(8, 21, 6, hint=1), nop_i(),
         nop_i()),
        (0xa0, 0x08, ld8(9, 20), ld8(10, 21),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "r8": 0xbbbbbbbb66666666,
        "r9": 0xaaaaaaaa55555555,
        "r10": 0xbbbbbbbb66666666,
    }, entry=0x10)

test_cmp8xchg16_unaligned = require_exception(
    "cmp8xchg16_unaligned", [
        (0x10, 0x00, addl(3, 0x204, 0), addl(2, 1, 0),
         nop_i()),
        (0x20, 0x00, cmp8xchg16_acq(4, 3, 2), nop_i(),
         nop_i()),
    ],
    IA64_EXCP_UNALIGNED, fault_ip=0x20,
)

test_cmp8xchg16_natpage_consumption = require_registers(
    "cmp8xchg16_natpage_consumption", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_NATPAGE),
        (0x70, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x90, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, HIGH_TR_BASE + 0x208)),
        (0xb0, *movl_mlx(4, 0x1122334455667788)),
        (0xc0, 0x00, cmp8xchg16_acq(8, 3, 4), nop_i(), nop_i()),
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
        "fault_ip": 0xc0,
        "r14": HIGH_TR_BASE + 0x208,
        "r15": 0x20 | IA64_ISR_R | IA64_ISR_W,
    }, entry=0x10)

test_lfetch_decode = require_registers("lfetch_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), addl(4, 0x180, 0),
     nop_i()),
    (0x20, 0x08, lfetch(3), lfetch(4, 0x2d),
     nop_i()),
    (0x30, 0x08, lfetch_postinc(3, 64), lfetch_postinc(4, 128, 0x2d, 1),
     nop_i()),
    (0x40, 0x08, adds(5, 0x20, 0), lfetch_reg_postinc(3, 5, 0x2e, 2),
     nop_i()),
    (0x50, 0x08, lfetch_count(3, 64, -1024, hint=3, h=1),
     nop_m(), nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
    br_cond(0x60, 0x60)),
], {"ip": 0x60, "r3": 0x160, "r4": 0x200}, entry=0x10)

test_reserved_m_major2_predicate_semantics = require_registers(
    "reserved_m_major2_predicate_semantics", [
        (0x10, 0x00, reserved_m_major2(qp=1), nop_i(), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_reserved_m_major2_true_illegal = require_exception(
    "reserved_m_major2_true_illegal", [
        (0x10, 0x00, reserved_m_major2(), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_tnat_unc_same_pred_pred_false_illegal = require_exception(
    "tnat_unc_same_pred_pred_false_illegal",
    [(0x10, 0x00, nop_m(), tnat_z_unc(6, 6, 0, qp=7), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_tnat_nz_or_decode = require_registers("tnat_nz_or_decode", [
    (0x10, 0x00, nop_m(), adds(3, 0x104, 0),
     nop_i()),
    (0x20, 0x00, sum_um(0x8), nop_i(),
     nop_i()),
    (0x30, 0x00, ld8_s(15, 3), nop_i(),
     nop_i()),
    (0x40, 0x00, nop_m(), tnat_nz_or(7, 0, 15),
     nop_i()),
    (0x50, 0x00, nop_m(), adds(31, 1, 0, qp=7),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {"ip": 0x60, "r31": 1}, entry=0x10)

test_tnat_nz_and_ignored_bits_decode = require_registers(
    "tnat_nz_and_ignored_bits_decode", [
        (0x10, 0x00, cmp4_eq_imm(5, 31, 0, 0), adds(3, 0x104, 0),
         nop_i()),
        (0x20, 0x00, sum_um(0x8), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8_s(15, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, nop_m(), tnat_nz_and(5, 31, 15, ignored=0x0d),
         nop_i()),
        (0x50, 0x00, nop_m(), adds(31, 1, 0, qp=5),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "r31": 1}, entry=0x10)

test_chk_s_m_branches_on_nat = require_registers(
    "chk_s_m_branches_on_nat", [
        (0x10, *movl_mlx(9, 1 << 32)),
        (0x20, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x100, 0),
         nop_i()),
        (0x30, 0x00, ld8_fill_postinc(17, 3, 0), nop_i(),
         nop_i()),
        (0x40, 0x08, nop_m(), chk_s_m(17, 0x40, 0x60),
         nop_i()),
        (0x50, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "r4": 0}, entry=0x10)

test_chk_s_i_long_branch_on_stacked_nat = require_registers(
    "chk_s_i_long_branch_on_stacked_nat", [
        (0x10, 0x00, alloc_m(45, 24, 16, 0, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(9, 1)),
        (0x30, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x200, 0),
         nop_i()),
        (0x40, 0x00, ld8_fill_postinc(33, 3, 0), nop_i(),
         nop_i()),
        # The displacement fields deliberately overlap the hint.i pattern.
        (0x50, 0x08, nop_m(), nop_m(),
         chk_s_i(33, 0x50, 0x601f0)),
        (0x60, 0x10, nop_m(), adds(4, 1, 0),
         br_cond(0x60, 0x80)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x601f0, 0x10, nop_m(), adds(5, 1, 0),
         br_cond(0x601f0, 0x60200)),
        (0x60200, 0x10, nop_m(), nop_i(),
         br_cond(0x60200, 0x60200)),
    ], {
        "ip": 0x60200,
        "exception": IA64_EXCP_NONE,
        "r4": 0,
        "r5": 1,
        "r33_nat": 1,
    }, entry=0x10)

def test_speculative_stacked_nat_survives_call_return(qemu):
    for collect_bit in range(63):
        bspstore = 0x100000 + collect_bit * 8
        run_program(qemu, [
            (0x10, *movl_mlx(2, bspstore)),
            (0x20, 0x01, mov_m_gr_ar(2, 18), nop_i(),
             nop_i()),
            (0x30, 0x00, alloc_m(43, 20, 13, 0, 0), nop_i(),
             nop_i()),
            (0x40, *movl_mlx(9, 1)),
            (0x50, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x300, 0),
             nop_i()),
            (0x60, 0x08, ld8_fill_postinc(40, 3, 0), nop_i(),
             nop_i()),
            (0x70, 0x00, ld8_s_postinc(32, 40, 8), nop_i(),
             nop_i()),
            (0x80, 0x10, nop_m(), nop_i(),
             br_call(0, 0x80, 0x200)),
            (0x90, 0x10, nop_m(), nop_i(),
             br_call(0, 0x90, 0x200)),
            (0xa0, 0x08, nop_m(), chk_s_m(32, 0xa0, 0xc0),
             nop_i()),
            (0xb0, 0x10, nop_m(), adds(8, 1, 0),
             br_cond(0xb0, 0xd0)),
            (0xc0, 0x10, nop_m(), adds(10, 1, 0),
             br_cond(0xc0, 0xd0)),
            (0xd0, 0x10, nop_m(), nop_i(),
             br_cond(0xd0, 0xd0)),
            (0x200, 0x00, alloc_m(34, 96, 88, 0, 0), nop_i(),
             nop_i()),
            (0x210, 0x00, mov_m_gr_ar(34, 64), nop_i(),
             nop_i()),
            (0x220, 0x10, nop_m(), nop_i(),
             br_ret(0)),
            (0x300, 0x00, 0x400, 0,
             0),
        ], entry=0x10, terminal_ip=0xd0, expected={
            "exception": IA64_EXCP_NONE,
            "r8": 0,
            "r10": 1,
            "r32_nat": 1,
        }, name=(
            "speculative_stacked_nat_survives_call_return_"
            f"collect_bit_{collect_bit}"))


def test_speculative_stacked_nat_survives_interrupt_flush_return(qemu):
    for collect_bit in range(63):
        bspstore = 0x100000 + collect_bit * 8
        run_program(qemu, [
            (0x10, *movl_mlx(2, bspstore)),
            (0x20, 0x01, mov_m_gr_ar(2, 18), nop_i(),
             nop_i()),
            (0x30, *movl_mlx(19, IA64_PSR_IC)),
            (0x40, 0x10, mov_gr_psr_full(19), nop_i(),
             br_cond(0x40, 0x50)),
            (0x50, 0x00, alloc_m(43, 20, 13, 0, 0), nop_i(),
             nop_i()),
            (0x60, 0x00, addl(3, 0x300, 0), nop_i(),
             nop_i()),
            (0x70, 0x08, ld8_fill_postinc(40, 3, 0), nop_i(),
             nop_i()),
            (0x80, 0x00, ld8_s_postinc(32, 40, 8), nop_i(),
             nop_i()),
            (0x90, 0x10, nop_m(), nop_i(),
             br_call(0, 0x90, 0x200)),
            (0xa0, 0x08, nop_m(), chk_s_m(32, 0xa0, 0xc0),
             nop_i()),
            (0xb0, 0x10, nop_m(), adds(8, 1, 0),
             br_cond(0xb0, 0xd0)),
            (0xc0, 0x10, nop_m(), adds(10, 1, 0),
             br_cond(0xc0, 0xd0)),
            (0xd0, 0x10, nop_m(), nop_i(),
             br_cond(0xd0, 0xd0)),
            (0x200, 0x00, alloc_m(34, 96, 88, 0, 0), nop_i(),
             nop_i()),
            (0x210, 0x00, break_m(0x42), nop_i(),
             nop_i()),
            (0x220, 0x10, nop_m(), nop_i(),
             br_ret(0)),
            (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
             cover_b()),
            (IA64_BREAK_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
             nop_i()),
            (IA64_BREAK_VECTOR + 0x20, 0x00, loadrs_enc(), nop_i(),
             nop_i()),
            (IA64_BREAK_VECTOR + 0x30, *movl_mlx(20, 0x220)),
            (IA64_BREAK_VECTOR + 0x40, 0x00,
             mov_m_gr_cr(20, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
             rfi_b()),
            (0x300, 0x00, 0x400, 0,
             0),
        ], entry=0x10, terminal_ip=0xd0, expected={
            "exception": IA64_EXCP_NONE,
            "r8": 0,
            "r10": 1,
            "r32_nat": 1,
        }, name=(
            "speculative_stacked_nat_survives_interrupt_flush_return_"
            f"collect_bit_{collect_bit}"))


def test_speculative_stacked_nat_survives_backing_store_switch(qemu):
    """A deferred speculative load's NaT must survive a software backing-store
    switch.  Move to BSPSTORE makes AR.RNAT undefined and drops every internal
    partial collection, so the architected save and restore of BSPSTORE and
    RNAT around the switch is the only path back for a spilled NaT bit.  This
    is the sequence an operating system runs on a kernel entry, and it is the
    one case the call and interrupt variants above never reach.  Sweeping the
    initial collection index covers both a bit that already reached the
    backing store and a bit still held in a partial AR.RNAT collection."""
    for collect_bit in range(63):
        bspstore = 0x100000 + collect_bit * 8
        run_program(qemu, [
            (0x10, *movl_mlx(2, bspstore)),
            (0x20, 0x01, mov_m_gr_ar(2, 18), nop_i(),
             nop_i()),
            (0x30, 0x00, alloc_m(43, 20, 13, 0, 0), nop_i(),
             nop_i()),
            (0x40, *movl_mlx(9, 1)),
            (0x50, 0x00, mov_m_gr_ar(9, 36), addl(3, 0x300, 0),
             nop_i()),
            (0x60, 0x08, ld8_fill_postinc(40, 3, 0), nop_i(),
             nop_i()),
            (0x70, 0x00, ld8_s_postinc(32, 40, 8), nop_i(),
             nop_i()),
            (0x80, 0x10, nop_m(), nop_i(),
             br_call(0, 0x80, 0x200)),
            (0x90, 0x08, nop_m(), chk_s_m(32, 0x90, 0xb0),
             nop_i()),
            (0xa0, 0x10, nop_m(), adds(8, 1, 0),
             br_cond(0xa0, 0xc0)),
            (0xb0, 0x10, nop_m(), adds(10, 1, 0),
             br_cond(0xb0, 0xc0)),
            (0xc0, 0x10, nop_m(), nop_i(),
             br_cond(0xc0, 0xc0)),
            # Spill the caller frame into the original store, switch to a
            # second store and drive the RSE against it, then restore the
            # architected pointer pair before returning.
            (0x200, 0x01, alloc_m(34, 96, 88, 0, 0), nop_i(),
             nop_i()),
            (0x210, 0x01, flushrs_enc(), nop_i(),
             nop_i()),
            (0x220, 0x00, mov_m_ar_gr(20, 18), nop_i(),
             nop_i()),
            (0x230, 0x00, mov_m_ar_gr(21, 19), nop_i(),
             nop_i()),
            (0x240, *movl_mlx(22, 0x200000)),
            (0x250, 0x00, mov_m_gr_ar(22, 18), nop_i(),
             nop_i()),
            (0x260, 0x18, nop_m(), nop_m(),
             cover_b()),
            (0x270, 0x00, flushrs_enc(), nop_i(),
             nop_i()),
            (0x280, 0x00, mov_m_gr_ar(20, 18), nop_i(),
             nop_i()),
            (0x290, 0x00, mov_m_gr_ar(21, 19), nop_i(),
             nop_i()),
            (0x2a0, 0x10, nop_m(), nop_i(),
             br_ret(0)),
            (0x300, 0x00, 0x400, 0,
             0),
        ], entry=0x10, terminal_ip=0xc0, expected={
            "exception": IA64_EXCP_NONE,
            "r8": 0,
            "r10": 1,
            "r32_nat": 1,
        }, name=(
            "speculative_stacked_nat_survives_backing_store_switch_"
            f"collect_bit_{collect_bit}"))


test_chk_a_nc_m_decode = require_registers("chk_a_nc_m_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, ld8_a(27, 3), nop_i(),
     nop_i()),
    (0x30, 0x00, chk_a_nc_m(27, 0x30, 0x50), adds(31, 0x56, 0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
    (0x100, 0x00, 0x123456789abcdef0, 0,
     0),
], {"ip": 0x40, "exception": IA64_EXCP_NONE, "r31": 0x56}, entry=0x10)

test_mlx_chk_a_clr_nop_x_decode = require_registers(
    "mlx_chk_a_clr_nop_x_decode", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x30, 0x04, chk_a_clr_m(22, 0x30, 0x50), 0,
         nop_x()),
        (0x40, 0x10, nop_m(), adds(31, 0x55, 0),
         br_cond(0x40, 0x50)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x50, "exception": IA64_EXCP_NONE, "r31": 0x55},
    entry=0x10)

test_chk_a_m_branches_on_miss = require_registers(
    "chk_a_m_branches_on_miss", [
        (0x10, 0x00, chk_a_nc_m(27, 0x10, 0x30), nop_i(),
         nop_i()),
        (0x20, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 0}, entry=0x10)

test_chk_a_clr_removes_entry = require_registers(
    "chk_a_clr_removes_entry", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, chk_a_clr_m(22, 0x30, 0x60), nop_i(),
         nop_i()),
        (0x40, 0x00, chk_a_nc_m(22, 0x40, 0x60), adds(4, 1, 0),
         nop_i()),
        (0x50, 0x00, adds(5, 1, 0), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x60, "r4": 0, "r5": 0}, entry=0x10)

test_invala_e_gr_invalidates_selected_register = require_registers(
    "invala_e_gr_invalidates_selected_register", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8_a(23, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, invala_e_gr(22), nop_i(),
         nop_i()),
        (0x50, 0x00, chk_a_nc_m(22, 0x50, 0x90), adds(4, 1, 0),
         nop_i()),
        (0x60, 0x00, adds(6, 1, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x90, 0x00, chk_a_nc_m(23, 0x90, 0xc0), adds(5, 1, 0),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0xc0, 0x00, adds(7, 1, 0), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0xa0, "r4": 0, "r5": 1, "r6": 0, "r7": 0},
    entry=0x10)

test_alat_reloading_register_does_not_leave_duplicate = require_registers(
    "alat_reloading_register_does_not_leave_duplicate", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 0x110, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(21, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8_a(22, 5), nop_i(),
         nop_i()),
        (0x40, 0x00, invala_e_gr(21), addl(5, 0x120, 0),
         nop_i()),
        (0x50, 0x00, ld8_a(22, 5), nop_i(),
         nop_i()),
        (0x60, 0x00, invala_e_gr(22), nop_i(),
         nop_i()),
        (0x70, 0x00, chk_a_nc_m(22, 0x70, 0xa0), nop_i(),
         nop_i()),
        (0x80, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, 0x1111111111111111, 0,
         0),
        (0x110, 0x00, 0x2222222222222222, 0,
         0),
        (0x120, 0x00, 0x3333333333333333, 0,
         0),
    ], {"ip": 0xa0, "r4": 0}, entry=0x10)

test_invala_clears_all_alat_entries = require_registers(
    "invala_clears_all_alat_entries", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8_a(23, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, invala(), nop_i(),
         nop_i()),
        (0x50, 0x00, chk_a_nc_m(22, 0x50, 0x80), adds(4, 1, 0),
         nop_i()),
        (0x60, 0x00, adds(6, 1, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x80, 0x00, chk_a_nc_m(23, 0x80, 0xb0), adds(5, 1, 0),
         nop_i()),
        (0x90, 0x00, adds(7, 1, 0), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0xb0, "r4": 0, "r5": 0, "r6": 0, "r7": 0},
    entry=0x10)

test_fc_invalidates_overlapping_alat_cache_line = require_registers(
    "fc_invalidates_overlapping_alat_cache_line", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(5, 0x140, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(), nop_i()),
        (0x30, 0x00, ld8_a(23, 5), nop_i(), nop_i()),
        (0x40, 0x00, fc(3), nop_i(), nop_i()),
        (0x50, 0x00, chk_a_nc_m(22, 0x50, 0x90), adds(4, 1, 0),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        (0x90, 0x00, chk_a_nc_m(23, 0x90, 0xd0), adds(6, 1, 0),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        (0x100, 0x00, 0x123456789abcdef0, 0, 0),
        (0x140, 0x00, 0xfedcba9876543210, 0, 0),
    ], {"ip": 0xd0, "r4": 0, "r6": 0}, entry=0x10, cpu="madison")


test_st4_variants_preserve_adjacent_halfword = require_registers(
    "st4_variants_preserve_adjacent_halfword", [
        (0x10, *movl_mlx(2, 0x4000)),
        (0x20, *movl_mlx(3, 0xa1b2c3d455667788)),
        (0x30, *movl_mlx(4, 0x01234567deadbeef)),

        (0x40, 0x00, st8(2, 3), nop_i(), nop_i()),
        (0x50, 0x00, st4(2, 4), nop_i(), nop_i()),
        (0x60, 0x00, ld8(8, 2), nop_i(), nop_i()),

        (0x70, 0x00, st8(2, 3), nop_i(), nop_i()),
        (0x80, 0x00, st4_rel(2, 4), nop_i(), nop_i()),
        (0x90, 0x00, ld8(9, 2), nop_i(), nop_i()),

        (0xa0, 0x00, st8(2, 3), adds(5, 0, 2), nop_i()),
        (0xb0, 0x00, st4_postinc(5, 4, 4), nop_i(), nop_i()),
        (0xc0, 0x00, ld8(10, 2), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r5": 0x4004,
        "r8": 0xa1b2c3d4deadbeef,
        "r9": 0xa1b2c3d4deadbeef,
        "r10": 0xa1b2c3d4deadbeef,
    }, entry=0x10)


# SDM Vol 3, Table 4-30 (integer load/store x6 opcode extensions): spill and
# fill exist only as the 8-byte forms (ld8.fill x6=0x1b, st8.spill x6=0x3b).
# The x6 values 0x18-0x1a and 0x38-0x3a are purple blanks in the table, so an
# instruction format that shares the integer load/store x6 space must raise
# Illegal Operation when its qualifying predicate is true and execute as a
# nop when it is false.  The no-update (M1/M4) and
# imm-base-update (M3/M5) forms are covered separately so a future decoder
# split per format keeps faulting on the reserved values.
def reserved_memory_x6_test(name, slot0):
    return require_exception(name, [
        (0x10, 0x00, slot0, nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_load_x6_18_reserved_illegal_operation = reserved_memory_x6_test(
    "load_x6_18_reserved_illegal_operation", load_mem(0x18, 8, 3))

test_load_x6_19_reserved_illegal_operation = reserved_memory_x6_test(
    "load_x6_19_reserved_illegal_operation", load_mem(0x19, 8, 3))

test_load_x6_1a_reserved_illegal_operation = reserved_memory_x6_test(
    "load_x6_1a_reserved_illegal_operation", load_mem(0x1a, 8, 3))

test_load_postinc_x6_18_reserved_illegal_operation = reserved_memory_x6_test(
    "load_postinc_x6_18_reserved_illegal_operation",
    load_mem_postinc(0x18, 8, 3, 8))

test_load_postinc_x6_19_reserved_illegal_operation = reserved_memory_x6_test(
    "load_postinc_x6_19_reserved_illegal_operation",
    load_mem_postinc(0x19, 8, 3, 8))

test_load_postinc_x6_1a_reserved_illegal_operation = reserved_memory_x6_test(
    "load_postinc_x6_1a_reserved_illegal_operation",
    load_mem_postinc(0x1a, 8, 3, 8))

# The register base-update form (M2) shares the same x6 space; stores have
# no register base-update form, so only the load side applies.
test_load_reg_postinc_x6_18_reserved_illegal_operation = \
    reserved_memory_x6_test(
        "load_reg_postinc_x6_18_reserved_illegal_operation",
        load_mem_reg_postinc(0x18, 8, 3, 4))

test_load_reg_postinc_x6_19_reserved_illegal_operation = \
    reserved_memory_x6_test(
        "load_reg_postinc_x6_19_reserved_illegal_operation",
        load_mem_reg_postinc(0x19, 8, 3, 4))

test_load_reg_postinc_x6_1a_reserved_illegal_operation = \
    reserved_memory_x6_test(
        "load_reg_postinc_x6_1a_reserved_illegal_operation",
        load_mem_reg_postinc(0x1a, 8, 3, 4))

test_store_x6_38_reserved_illegal_operation = reserved_memory_x6_test(
    "store_x6_38_reserved_illegal_operation", store_mem(0x38, 3, 4))

test_store_x6_39_reserved_illegal_operation = reserved_memory_x6_test(
    "store_x6_39_reserved_illegal_operation", store_mem(0x39, 3, 4))

test_store_x6_3a_reserved_illegal_operation = reserved_memory_x6_test(
    "store_x6_3a_reserved_illegal_operation", store_mem(0x3a, 3, 4))

test_store_postinc_x6_38_reserved_illegal_operation = reserved_memory_x6_test(
    "store_postinc_x6_38_reserved_illegal_operation",
    store_mem_postinc(0x38, 3, 4, 8))

test_store_postinc_x6_39_reserved_illegal_operation = reserved_memory_x6_test(
    "store_postinc_x6_39_reserved_illegal_operation",
    store_mem_postinc(0x39, 3, 4, 8))

test_store_postinc_x6_3a_reserved_illegal_operation = reserved_memory_x6_test(
    "store_postinc_x6_3a_reserved_illegal_operation",
    store_mem_postinc(0x3a, 3, 4, 8))


# One representative purple cell from each integer-memory selector space in
# SDM Vol. 3 Tables 4-28 and 4-30 through 4-33.  PR1 is reset-clear, so the
# five exact qp=1 encodings below must all be nullified.
_reserved_memory_selector_representatives = (
    reserved_memory_selector(4, 0, 0, 0x18, qp=1),  # 0x8600000001
    reserved_memory_selector(4, 1, 0, 0x18, qp=1),  # 0x9600000001
    reserved_memory_selector(5, 0, 0, 0x18, qp=1),  # 0xa600000001
    reserved_memory_selector(4, 0, 1, 0x0c, qp=1),  # 0x8308000001
    reserved_memory_selector(4, 1, 1, 0x00, qp=1),  # 0x9008000001
)

test_reserved_memory_selectors_predicated_off_are_nops = require_registers(
    "reserved_memory_selectors_predicated_off_are_nops", [
        (0x10 + index * 0x10, 0x00, raw, nop_i(), nop_i())
        for index, raw in enumerate(_reserved_memory_selector_representatives)
    ] + [
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)


def reserved_memory_selector_true_test(name, raw):
    return require_exception(name, [
        (0x10, 0x00, raw & ~0x3f, nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)


test_reserved_memory_selector_m4_m0_x0_true_illegal = \
    reserved_memory_selector_true_test(
        "reserved_memory_selector_m4_m0_x0_true_illegal",
        _reserved_memory_selector_representatives[0])
test_reserved_memory_selector_m4_m1_x0_true_illegal = \
    reserved_memory_selector_true_test(
        "reserved_memory_selector_m4_m1_x0_true_illegal",
        _reserved_memory_selector_representatives[1])
test_reserved_memory_selector_m5_true_illegal = \
    reserved_memory_selector_true_test(
        "reserved_memory_selector_m5_true_illegal",
        _reserved_memory_selector_representatives[2])
test_reserved_memory_selector_m4_m0_x1_true_illegal = \
    reserved_memory_selector_true_test(
        "reserved_memory_selector_m4_m0_x1_true_illegal",
        _reserved_memory_selector_representatives[3])
test_reserved_memory_selector_m4_m1_x1_true_illegal = \
    reserved_memory_selector_true_test(
        "reserved_memory_selector_m4_m1_x1_true_illegal",
        _reserved_memory_selector_representatives[4])

test_bsw_restores_banked_nat = require_registers(
    "bsw_restores_banked_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x13, nop_m(), nop_b(), bsw1()),
        # Clear the bank-1 r16 NaT while bank 0's NaT is saved.  Switching
        # back must restore the saved NaT bit together with the GR bank
        # (SDM Vol. 2, 3.3.7).
        (0x40, *movl_mlx(16, 0x55)),
        (0x50, 0x13, nop_m(), nop_b(), bsw0()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "psr": 0,
        "r16_nat": 1,
    }, entry=0x10)

test_cloop_zero_st1_invalidates_alat_range = require_registers(
    "cloop_zero_st1_invalidates_alat_range", [
        (0x10, *movl_mlx(2, 0x8000)),
        (0x20, 0x00, adds(3, 8, 2), nop_i(),
         nop_i()),
        (0x30, 0x00, ld8_a(22, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, adds(8, 15, 0), nop_i(),
         nop_i()),
        (0x50, 0x02, nop_m(), mov_lc_gr(8),
         nop_i()),
        (0x60, 0x10, st1_postinc(2, 0, 1), nop_i(),
         br_cloop(0x60, 0x60)),
        (0x70, 0x00, chk_a_nc_m(22, 0x70, 0xa0), adds(4, 1, 0),
         nop_i()),
        (0x80, 0x00, adds(5, 1, 0), nop_i(),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r2": 0x8010,
        "r4": 0,
        "r5": 0,
    }, entry=0x10)

test_cloop_zero_st1_clears_cross_page_range = require_registers(
    "cloop_zero_st1_clears_cross_page_range", [
        (0x10, *movl_mlx(2, 0x7ff0)),
        (0x20, 0x00, adds(4, 0xff, 0),
         addl(8, 8224 - 1, 0), nop_i()),
        (0x30, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
        (0x40, 0x10, st1_postinc(2, 4, 1), nop_i(),
         br_cloop(0x40, 0x40)),

        (0x50, *movl_mlx(2, 0x7ff0)),
        (0x60, 0x00, addl(8, 8224 - 1, 0), nop_i(), nop_i()),
        (0x70, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
        (0x80, 0x10, st1_postinc(2, 0, 1), nop_i(),
         br_cloop(0x80, 0x80)),

        (0x90, *movl_mlx(2, 0x7ff0)),
        (0xa0, 0x00, addl(8, 8224 - 1, 0), adds(10, 0, 0), nop_i()),
        (0xb0, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
        (0xc0, 0x10, ld1_postinc(11, 2, 1), or_reg(10, 10, 11),
         br_cloop(0xc0, 0xc0)),
        (0xd0, 0x02, nop_m(), mov_ar_lc(9), nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r2": 0xa010,
        "r9": 0,
        "r10": 0,
    }, entry=0x10)


def test_ld2_bias_st2_raw_large_frame_sequence(qemu):
    result = run_program(qemu, [
        (0x10, *movl_mlx(2, 0x8000)),
        (0x20, 0x00, st2(2, 0), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x8010)),
        (0x40, 0x00, st8(3, 0), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), alloc(100, 73, 70, 0, 0), nop_i()),
        (0x60, *movl_mlx(88, 0x8010)),
        (0x70, *movl_mlx(5, 13)),
        (0x80, 0x02, nop_m(), mov_lc_gr(5), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), bsw1()),
        (0xa0, *movl_mlx(22, 0x8000)),

        # Keep fixed instruction words, template stops, and an unrelated
        # speculative load around the 16-bit accounting update.  Fourteen
        # iterations exercise the large-frame loop.
        raw_bundle(0xb0, 0x093010882c00a80b, 0x0004000000420054),
        raw_bundle(0xc0, 0x067011882c4c0018, 0x2000000000207160),
        (0xd0, 0x11, nop_m(), nop_i(), br_cloop(0xd0, 0xb0)),
        (0xe0, 0x01, ld2(8, 22), nop_i(), nop_i()),
        (0xf0, 0x11, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
    ], entry=0x10, terminal_ip=0xf0, expected={
        "exception": IA64_EXCP_NONE,
        "r8": 14,
    }, name="ld2_bias_st2_raw_large_frame_sequence")
    if result.state.gr[8] != 14:
        raise RuntimeError(
            "ld2_bias_st2_raw_large_frame_sequence failed: "
            f"counter={result.state.gr[8]!r}\n{result.register_output}")

test_mov_ar_nat_source_consumes = register_nat_consumption_test(
    "mov_ar_nat_source_consumes",
    (0x00, mov_m_gr_ar(16, 65), nop_i(), nop_i()),
    1 << IA64_ISR_EI_SHIFT)

test_mov_br_nat_source_consumes = register_nat_consumption_test(
    "mov_br_nat_source_consumes",
    (0x09, nop_m(), nop_m(), mov_b_gr(0, 16)),
    2 << IA64_ISR_EI_SHIFT)

test_mov_pr_nat_source_consumes = register_nat_consumption_test(
    "mov_pr_nat_source_consumes",
    (0x00,
     nop_m(),
     bitfield(3, 33, 3) | bitfield(16, 13, 7) | bitfield(0x7f, 6, 7),
     nop_i()),
    1 << IA64_ISR_EI_SHIFT)

test_mov_cr_nat_source_consumes = register_nat_consumption_test(
    "mov_cr_nat_source_consumes",
    (0x00, mov_m_gr_cr(16, 0), nop_i(), nop_i()))

test_mov_psr_nat_source_consumes = register_nat_consumption_test(
    "mov_psr_nat_source_consumes",
    (0x00, mov_m_gr_psrl(16), nop_i(), nop_i()))

test_mov_um_nat_source_consumes = register_nat_consumption_test(
    "mov_um_nat_source_consumes",
    (0x00, mov_m_gr_psr_um(16), nop_i(), nop_i()))

test_mov_rr_nat_index_consumes = register_nat_consumption_test(
    "mov_rr_nat_index_consumes",
    (0x00, mov_rr_read(17, 16), nop_i(), nop_i()))

test_mov_pkr_nat_index_consumes = register_nat_consumption_test(
    "mov_pkr_nat_index_consumes",
    (0x00, mov_pkr_indexed(16, 17, bit36=1), nop_i(), nop_i()))

test_mov_pmc_nat_value_consumes = register_nat_consumption_test(
    "mov_pmc_nat_value_consumes",
    (0x00, mov_grpmc_indexed(3, 16), nop_i(), nop_i()))

test_mov_cpuid_nat_index_consumes = register_nat_consumption_test(
    "mov_cpuid_nat_index_consumes",
    (0x00, mov_cpuid(17, 16), nop_i(), nop_i()))

test_fc_nat_source_consumes_non_access = register_nat_consumption_test(
    "fc_nat_source_consumes_non_access",
    (0x00, fc_i(16), nop_i(), nop_i()),
    IA64_ISR_NA | IA64_ISR_R | 1)

test_simd_helper_nat_propagates = require_registers("simd_helper_nat_propagates", [
    (0x10, 0x00, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
     nop_i()),
    (0x20, 0x08, ld8_fill_postinc(5, 4, 0), nop_i(),
     nop_i()),
    (0x30, *movl_mlx(6, 0x0001000200030004)),
    (0x40, 0x02, nop_m(), pmpy2(7, 5, 6),
     mux1_rev(8, 5)),
    (0x50, 0x02, nop_m(), czx1_r(9, 5),
     pack2_sss(10, 5, 6)),
    (0x60, 0x00, nop_m(), nop_i(), nop_i()),
    (0x70, 0x00, nop_m(), nop_i(), nop_i()),
    (0x80, 0x00, nop_m(), nop_i(), nop_i()),
    (0x90, 0x00, nop_m(), nop_i(), nop_i()),
    (0xa0, 0x10, nop_m(), nop_i(),
     br_cond(0xa0, 0xa0)),
    (0x200, 0x00, 0, 0,
     0),
], {
    "ip": 0xa0,
    "r7_nat": 1,
    "r8_nat": 1,
    "r9_nat": 1,
    "r10_nat": 1,
}, entry=0x10)

test_pshr_nat_propagates = require_registers("pshr_nat_propagates", [
    (0x10, 0x00, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
     nop_i()),
    (0x20, 0x08, ld8_fill_postinc(5, 4, 0), nop_i(),
     nop_i()),
    (0x30, 0x00, nop_m(), pshr4(6, 5, 1),
     pshr2(7, 0, 5, variable=True)),
    (0x40, 0x00, nop_m(), nop_i(), nop_i()),
    (0x50, 0x00, nop_m(), nop_i(), nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
    (0x200, 0x00, 0, 0,
     0),
], {
    "ip": 0x60,
    "r6_nat": 1,
    "r7_nat": 1,
}, entry=0x10)

test_pshl_nat_propagates = require_registers("pshl_nat_propagates", [
    (0x10, 0x00, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
     nop_i()),
    (0x20, 0x08, ld8_fill_postinc(5, 4, 0), nop_i(),
     nop_i()),
    (0x30, 0x00, addl(6, 4, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, nop_m(), pshl4(7, 5, 6),
     pshl2(8, 0, 5)),
    (0x50, 0x00, nop_m(), nop_i(), nop_i()),
    (0x60, 0x00, nop_m(), nop_i(), nop_i()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
    (0x200, 0x00, 0, 0,
     0),
], {
    "ip": 0x70,
    "r7_nat": 1,
    "r8_nat": 1,
}, entry=0x10)

test_firmware_unaligned_load_assist = require_registers(
    "firmware_unaligned_load_assist",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, *movl_mlx(21, 0x99aabbccddeeff00)),
        (0x30, 0x00, addl(3, 0x100, 0), nop_i(), nop_i()),
        (0x40, 0x0a, st8(3, 20), adds(3, 8, 3), nop_i()),
        (0x50, 0x0a, st8(3, 21), adds(3, -4, 3), nop_i()),
        (0x60, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x80, 0x00, ssm((1 << 13) | (1 << 3)), nop_i(), nop_i()),
        (0x90, 0x0a, ld8(22, 3), adds(23, 1, 0), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ],
    {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r22": 0xddeeff0011223344,
        "r23": 1,
    },
)

test_firmware_unaligned_reg_postinc_uses_old_increment = require_registers(
    "firmware_unaligned_reg_postinc_uses_old_increment",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, *movl_mlx(21, 0x99aabbccddeeff00)),
        (0x30, 0x00, addl(3, 0x100, 0), addl(5, 0x20, 0), nop_i()),
        (0x40, 0x0a, st8(3, 20), adds(3, 8, 3), nop_i()),
        (0x50, 0x0a, st8(3, 21), adds(3, -4, 3), nop_i()),
        (0x60, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x80, 0x00, ssm((1 << 13) | (1 << 3)), nop_i(), nop_i()),
        (0x90, 0x0a, load_mem_reg_postinc(0x03, 5, 3, 5),
         adds(23, 1, 0), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ],
    {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r3": 0x124,
        "r5": 0xddeeff0011223344,
        "r23": 1,
    },
)

test_firmware_unaligned_assist_retires_single_step = require_registers(
    "firmware_unaligned_assist_retires_single_step",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
        (0x30, 0x00, st8(3, 20), nop_i(), nop_i()),
        (0x40, 0x00, nop_m(), adds(3, 4, 3), nop_i()),
        (0x50, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
        (0x60, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x70, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_AC | IA64_PSR_SS)),
        (0x80, *movl_mlx(4, 0x110)),
        *rfi_to_gr(0x90, 2, 4),
        # A firmware-assisted instruction still completes in slot 0.  The
        # single-step trap must therefore name slot 1 as its target; trapping
        # only after the following nop would incorrectly report slot 2.
        (0x110, 0x00, ld8(22, 3), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR, 0x00,
         mov_m_cr_gr(24, 19), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(25, 22), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(26, 17), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x30, 0x00,
         mov_m_cr_gr(27, 16), nop_i(), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x40, 0x02,
         nop_m(), extr_u(28, 27, 41, 2), nop_i()),
        (IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x50, 0x10,
         nop_m(), nop_i(),
         br_cond(IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x50,
                 IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x50)),
    ],
    {
        "ip": IA64_FIRMWARE_IVT_BASE + IA64_SINGLE_STEP_VECTOR + 0x50,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_SINGLE_STEP,
        "r22": 0x11223344,
        "r24": 0x110,
        "r25": 0x110,
        "r26": IA64_ISR_CODE_SS,
        "r28": 1,
    },
)

test_firmware_unaligned_store_assist = require_registers(
    "firmware_unaligned_store_assist",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, *movl_mlx(21, 0x99aabbccddeeff00)),
        (0x30, *movl_mlx(24, 0xaabbccddeeff0011)),
        (0x40, 0x00, addl(3, 0x100, 0), nop_i(), nop_i()),
        (0x50, 0x0a, st8(3, 20), adds(3, 8, 3), nop_i()),
        (0x60, 0x0a, st8(3, 21), adds(3, -4, 3), nop_i()),
        (0x70, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0x80, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x90, 0x00, ssm((1 << 13) | (1 << 3)), nop_i(), nop_i()),
        (0xa0, 0x0a, st8(3, 24), adds(25, 1, 0), nop_i()),
        (0xb0, 0x00, adds(3, -4, 3), nop_i(), nop_i()),
        (0xc0, 0x0a, ld8(26, 3), adds(3, 8, 3), nop_i()),
        (0xd0, 0x00, ld8(27, 3), nop_i(), nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
    ],
    {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r25": 1,
        "r26": 0xeeff001155667788,
        "r27": 0x99aabbccaabbccdd,
    },
)


def _firmware_unaligned_mapped_load_store_assist(name, pte_flags):
    va = HIGH_TR_BASE + 0x8404
    value = 0xaabbccddeeff0011

    return require_registers(name, [
        *dtr_setup_bundles(0x10, va, 0x408404,
                           pte_flags=pte_flags),
        (0x70, *movl_mlx(3, va)),
        (0x80, *movl_mlx(24, value)),
        (0x90, 0x00, addl(2, IA64_FIRMWARE_IVT_BASE, 0),
         nop_i(), nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0xb0, 0x00, ssm(IA64_PSR_DT | IA64_PSR_IC | IA64_PSR_AC),
         nop_i(), nop_i()),
        # Both references are misaligned and must complete through the
        # firmware assist while retaining the mapping's sequential attribute.
        (0xc0, 0x00, st8(3, 24), nop_i(), nop_i()),
        (0xd0, 0x00, ld8(22, 3), nop_i(), nop_i()),
        (0xe0, 0x00, nop_m(), adds(23, 1, 0), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r22": value,
        "r23": 1,
    })


test_firmware_unaligned_uc_load_store_assist = \
    _firmware_unaligned_mapped_load_store_assist(
        "firmware_unaligned_uc_load_store_assist", DTR_PTE_UC)

test_firmware_unaligned_uce_load_store_assist = \
    _firmware_unaligned_mapped_load_store_assist(
        "firmware_unaligned_uce_load_store_assist", DTR_PTE_UCE)


test_firmware_unaligned_speculative_load_assist = require_registers(
    "firmware_unaligned_speculative_load_assist",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, *movl_mlx(21, 0x99aabbccddeeff00)),
        (0x30, 0x00, addl(3, 0x300, 0), addl(5, 0x304, 0),
         nop_i()),
        (0x40, 0x0a, st8(3, 20), adds(3, 8, 3), nop_i()),
        (0x50, 0x0a, st8(3, 21), adds(3, -8, 3), nop_i()),
        (0x60, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x80, 0x00, ssm((1 << 13) | (1 << 3)), nop_i(), nop_i()),
        (0x90, 0x00, ld8_s(4, 5), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), adds(8, 0, 4), nop_i()),
        (0xb0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xc0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xd0, 0x00, ld8_a(6, 3), nop_i(), nop_i()),
        (0xe0, 0x00, nop_m(), addl(6, 0x55, 0), nop_i()),
        (0xf0, 0x00, ld8_a(6, 5), nop_i(), nop_i()),
        (0x100, 0x00, nop_m(), adds(11, 0, 6), nop_i()),
        (0x110, 0x00, nop_m(), addl(6, 0x77, 0), nop_i()),
        (0x120, 0x00, ld8_c_nc(6, 3), nop_i(), nop_i()),
        (0x130, 0x00, ld8_a(7, 3), nop_i(), nop_i()),
        (0x140, 0x00, nop_m(), addl(7, 0x66, 0), nop_i()),
        (0x150, 0x00, ld8_sa(7, 5), nop_i(), nop_i()),
        (0x160, 0x00, nop_m(), adds(12, 0, 7), nop_i()),
        (0x170, 0x00, nop_m(), nop_i(), nop_i()),
        (0x180, 0x00, nop_m(), nop_i(), nop_i()),
        (0x190, 0x00, nop_m(), addl(7, 0x88, 0), nop_i()),
        (0x1a0, 0x00, ld8_c_nc(7, 3), nop_i(), nop_i()),
        (0x1b0, 0x10, nop_m(), nop_i(), br_cond(0x1b0, 0x1b0)),
    ],
    {
        "ip": 0x1b0,
        "exception": IA64_EXCP_NONE,
        "r6": 0x1122334455667788,
        "r7": 0x1122334455667788,
        "r8": 0xddeeff0011223344,
        "r8_nat": 0,
        "r11": 0x55,
        "r12": 0x66,
        "r12_nat": 0,
    },
)

test_firmware_unaligned_virtual_load_assist = require_registers(
    "firmware_unaligned_virtual_load_assist",
    [
        (0x10, *movl_mlx(20, 0x1122334455667788)),
        (0x20, *movl_mlx(21, 0x99aabbccddeeff00)),
        (0x30, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
        (0x40, 0x0a, st8(3, 20), adds(3, 8, 3), nop_i()),
        (0x50, 0x00, st8(3, 21), nop_i(), nop_i()),
        *dtr_setup_bundles(0x60, 0xe000000000000304, 0x304),
        (0xc0, *movl_mlx(5, 0xe000000000000304)),
        (0xd0, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0xe0, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0xf0, 0x00, ssm((1 << 17) | (1 << 13) | (1 << 3)),
         nop_i(), nop_i()),
        (0x100, 0x00, ld8_s(22, 5), nop_i(), nop_i()),
        (0x110, 0x00, nop_m(), adds(23, 1, 0), nop_i()),
        (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
    ],
    {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r22": 0xddeeff0011223344,
        "r22_nat": 0,
        "r23": 1,
    },
)

test_speculative_unaligned_defers = require_registers(
    "speculative_unaligned_defers",
    [
        (0x10, 0x00, nop_m(), addl(3, 0x104, 0), nop_i()),
        (0x20, 0x00, sum_um(0x8), nop_i(), nop_i()),
        # Misalignment must leave the aligned integer fast path and retain
        # the complete speculative fault qualification in its cold fallback.
        (0x30, 0x00, ld8_s(4, 3), nop_i(), nop_i()),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ],
    {"ip": 0x60, "r4_nat": 1},
)

_br_ctop_spec_data = [i + 1 for i in range(131)] + [0]
test_br_ctop_long_speculative_load_pipeline = require_registers(
    "br_ctop_long_speculative_load_pipeline", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(20, HIGH_TR_BASE + 0x8000)),
        (0x80, *movl_mlx(5, 130)),
        (0x90, 0x01, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x00, alloc_m(9, 32, 32, 4, 0),
         mov_i_imm_ar(66, 1), mov_lc_gr(5)),
        (0xb0, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0xc0, 0x08, mov_gr_psr_full(19), srlz_d(), nop_i()),
        (0xd0, 0x00, nop_m(), mov_pr_rot_imm(0x10000), nop_i()),
        (0xe0, 0x00, ld8_s_postinc(32, 20, 8, qp=16), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), adds(8, 0, 34, qp=18),
         br_ctop_many(0xf0, 0xe0)),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
        *(raw_bundle(0x408000 + i * 8, _br_ctop_spec_data[i],
                     _br_ctop_spec_data[i + 1])
          for i in range(0, len(_br_ctop_spec_data), 2)),
    ], {"exception": IA64_EXCP_NONE, "ip": 0x100,
        "r8": 129, "r20": HIGH_TR_BASE + 0x8418}, entry=0x10)

_WARM_SPECULATIVE_LOAD_DATA = 0x8877665544332211
test_speculative_load_warm_soft_tlb_succeeds = require_registers(
    "speculative_load_warm_soft_tlb_succeeds", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(2, HIGH_TR_BASE + 0x8200)),
        (0x80, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(), nop_i()),
        # The ordinary load installs a direct LOAD comparator.  The following
        # aligned integer speculative loads of every size must reuse its
        # cached WB attributes.  Both ld.s and ld.sa use the dedicated helper.
        (0xa0, 0x00, ld8(4, 2), nop_i(), nop_i()),
        (0xb0, 0x00, load_mem(0x04, 5, 2), nop_i(), nop_i()),
        (0xc0, 0x00, load_mem(0x05, 6, 2), nop_i(), nop_i()),
        (0xd0, 0x00, load_mem(0x06, 7, 2), nop_i(), nop_i()),
        (0xe0, 0x00, load_mem(0x07, 8, 2), nop_i(), nop_i()),
        (0xf0, 0x00, load_mem(0x0c, 9, 2) | bitfield(2, 27, 2),
         nop_i(), nop_i()),
        (0x100, 0x00, load_mem(0x0d, 10, 2) | bitfield(2, 27, 2),
         nop_i(), nop_i()),
        (0x110, 0x00, load_mem(0x0e, 11, 2) | bitfield(2, 27, 2),
         nop_i(), nop_i()),
        (0x120, 0x00, load_mem(0x0f, 12, 2) | bitfield(2, 27, 2),
         nop_i(), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(), br_cond(0x130, 0x130)),
        raw_bundle(0x408200, _WARM_SPECULATIVE_LOAD_DATA, 0),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r4": _WARM_SPECULATIVE_LOAD_DATA,
        "r5": 0x11,
        "r6": 0x2211,
        "r7": 0x44332211,
        "r8": _WARM_SPECULATIVE_LOAD_DATA,
        "r9": 0x11,
        "r10": 0x2211,
        "r11": 0x44332211,
        "r12": _WARM_SPECULATIVE_LOAD_DATA,
        "r5_nat": 0, "r6_nat": 0, "r7_nat": 0, "r8_nat": 0,
        "r9_nat": 0, "r10_nat": 0, "r11_nat": 0, "r12_nat": 0,
    }, entry=0x10)

GROUP = 'memory-nat'
CASE_NAMES = (

    'alat_reloading_register_does_not_leave_duplicate',
    'alloc_clears_destination_nat',
    'br_ctop_long_speculative_load_pipeline',
    'bsw_restores_banked_nat',
    'chk_a_clr_removes_entry',
    'chk_a_m_branches_on_miss',
    'chk_a_nc_m_decode',
    'chk_s_i_long_branch_on_stacked_nat',
    'chk_s_m_branches_on_nat',
    'cloop_zero_st1_clears_cross_page_range',
    'cloop_zero_st1_invalidates_alat_range',
    'cmp8xchg16_acq_stores_pair',
    'cmp8xchg16_madison_illegal_operation',
    'cmp8xchg16_natpage_consumption',
    'cmp8xchg16_rel_mismatch_keeps_pair',
    'cmp8xchg16_uc_unsupported_data_reference',
    'cmp8xchg16_unaligned',
    'cmpxchg4_full_ar_ccv_compare',
    'cmpxchg4_repeated_word_updates',
    'cmpxchg4_result_base_alias_failure_keeps_alat',
    'cmpxchg4_result_base_alias_success_invalidates_alat',
    'cmpxchg4_uses_ar_ccv',
    'data_big_endian_cmpxchg4',
    'data_big_endian_load_store',
    'fc_invalidates_overlapping_alat_cache_line',
    'fc_nat_source_consumes_non_access',
    'fetchadd4_nat_base_sets_read_write_isr',
    'fetchadd4_result_base_alias_invalidates_alat',
    'fetchadd4_unaligned_sets_read_write_isr',
    'firmware_alt_dtlb_nonspeculative_load_installs_identity_tc',
    'firmware_alt_dtlb_nonspeculative_load_faults',
    'firmware_alt_dtlb_speculative_load_defers',
    'firmware_unaligned_load_assist',
    'firmware_unaligned_assist_retires_single_step',
    'firmware_unaligned_reg_postinc_uses_old_increment',
    'firmware_unaligned_speculative_load_assist',
    'firmware_unaligned_store_assist',
    'firmware_unaligned_uc_load_store_assist',
    'firmware_unaligned_uce_load_store_assist',
    'firmware_unaligned_virtual_load_assist',
    'fp_advanced_non_speculative_unaligned_faults',
    'integer_advanced_non_speculative_unaligned_faults',
    'integer_compare_nat_source_rules',
    'integer_nat_propagates_and_clears',
    'integer_postinc_imm9_decode',
    'invala_clears_all_alat_entries',
    'invala_e_gr_invalidates_selected_register',
    'ld16_acq_hint_decode',
    'ld16_loads_gr_and_csd',
    'ld16_madison_illegal_operation',
    'ld16_uc_unsupported_data_reference',
    'ld16_unaligned_always_faults',
    'ld1_acq_decode',
    'ld1_postinc_decode',
    'ld1_reg_postinc_decode',
    'ld1_reg_postinc_uses_old_increment',
    'ld1_sa_postinc_decode',
    'ld2_bias_st2_raw_large_frame_sequence',
    'ld4_c_clr_hit_clears_entry',
    'ld4_bias_decode',
    'ld8_a_uc_zeroes_target_and_skips_alat',
    'ld8_c_clr_address_mismatch_reloads',
    'ld8_c_clr_hit_clears_entry',
    'ld8_c_nc_address_mismatch_reloads',
    'ld8_c_nc_hit_consumes_nat_base',
    'ld8_c_nc_hit_preserves_target',
    'ld8_c_nc_uc_miss_does_not_allocate_alat',
    'ld8_fill_restores_unat_bit',
    'ld8_fill_st8_spill_postinc_decode',
    'ld8_nt1_postinc_decode',
    'ld8_s_d2_hint_decode',
    'ld8_s_uc_defers',
    'ld8_sa_failure_invalidates_old_entry',
    'ld8_sa_instruction_ed_survives_data_soft_tlb_warmup',
    'ld_imm_postinc_same_target_illegal',
    'ld_postinc_same_target_predicated_false',
    'ld_reg_postinc_same_target_illegal',
    'lfetch_decode',
    'reserved_m_major2_predicate_semantics',
    'reserved_m_major2_true_illegal',
    'reserved_memory_selector_m4_m0_x0_true_illegal',
    'reserved_memory_selector_m4_m0_x1_true_illegal',
    'reserved_memory_selector_m4_m1_x0_true_illegal',
    'reserved_memory_selector_m4_m1_x1_true_illegal',
    'reserved_memory_selector_m5_true_illegal',
    'reserved_memory_selectors_predicated_off_are_nops',
    'load_postinc_x6_18_reserved_illegal_operation',
    'load_postinc_x6_19_reserved_illegal_operation',
    'load_postinc_x6_1a_reserved_illegal_operation',
    'load_reg_postinc_x6_18_reserved_illegal_operation',
    'load_reg_postinc_x6_19_reserved_illegal_operation',
    'load_reg_postinc_x6_1a_reserved_illegal_operation',
    'load_x6_18_reserved_illegal_operation',
    'load_x6_19_reserved_illegal_operation',
    'load_x6_1a_reserved_illegal_operation',
    'memory_cache_hints_decode',
    'memory_order_completers_decode',
    'madison_fp_fill_requires_natural_alignment',
    'madison_fp_load_crossing_16byte_window_faults',
    'madison_fp_load_within_16byte_window',
    'madison_fp_pair_requires_natural_alignment',
    'madison_fp_spill_requires_natural_alignment',
    'madison_integer_load_crossing_8byte_window_faults',
    'madison_ldfe_crossing_16byte_window_faults',
    'madison_ldfe_within_16byte_window',
    'madison_speculative_model_unaligned_defers',
    'merced_integer_load_within_16byte_window',
    'mlx_chk_a_clr_nop_x_decode',
    'mov_ar_nat_source_consumes',
    'mov_br_nat_source_consumes',
    'mov_cpuid_nat_index_consumes',
    'mov_cr_nat_source_consumes',
    'mov_crgr_clears_stale_nat',
    'mov_pkr_nat_index_consumes',
    'mov_pmc_nat_value_consumes',
    'mov_pr_nat_source_consumes',
    'mov_psr_nat_source_consumes',
    'mov_rr_nat_index_consumes',
    'mov_um_nat_source_consumes',
    'montecito_fp_load_crossing_8byte_window_faults',
    'montecito_fp_store_crossing_16byte_window_faults',
    'montecito_fp_store_within_16byte_window',
    'montecito_load_crossing_8byte_window_faults',
    'montecito_speculative_fp_model_unaligned_defers',
    'montecito_store_crossing_16byte_window_faults',
    'montecito_store_within_16byte_window',
    'montecito_uc_fp_store_crossing_8byte_window_faults',
    'nat_clear_tb_speculative_exit_rechecks_flags',
    'nat_clear_self_loop_prefix_rechecks_facts',
    'nat_consumption_ic0_preserves_ifa',
    'nat_consumption_sets_ifa_isr',
    'nat_store_base_consumption_is_access',
    'nat_store_data_consumption_is_access',
    'nat_tracking_later_fill_overrides_clear',
    'nat_tracking_load_destination_base_alias',
    'nat_tracking_predicated_load_does_not_clear_base',
    'nat_tracking_predicated_store_does_not_clear_source',
    'nat_tracking_reg_postinc_reinvalidates_base',
    'normal_load_clears_stale_nat',
    'normal_load_consumes_nat_base',
    'non_speculative_attribute_returns_failure_values',
    'pshl_nat_propagates',
    'pshr_nat_propagates',
    'rse_spill_preserves_static_alat_entry',
    'semaphore_ops_clear_result_nat',
    'semaphore_ops_invalidate_advanced_loads',
    'simd_helper_nat_propagates',
    'smp_full_alat_model_remains_enabled',
    'speculative_load_defers_nat_base',
    'speculative_load_defers_psr_ed',
    'speculative_load_handler_psr_ed_defers_retry',
    'speculative_load_no_recovery_tlb_miss_faults',
    'speculative_load_warm_soft_tlb_succeeds',
    'speculative_recovery_unaligned_defers',
    'speculative_stacked_nat_survives_backing_store_switch',
    'speculative_stacked_nat_survives_interrupt_flush_return',
    'speculative_stacked_nat_survives_call_return',
    'speculative_unimplemented_physical_unaligned_defers',
    'speculative_unaligned_defers',
    'speculative_unaligned_no_recovery_faults',
    'st16_madison_illegal_operation',
    'st16_rel_stores_gr_and_csd',
    'st16_stores_gr_and_csd',
    'st16_uc_unsupported_data_reference',
    'st16_unaligned_always_faults',
    'st1_postinc_decode',
    'st4_variants_preserve_adjacent_halfword',
    'st8_postinc_same_base_value_uses_old_base',
    'st8_spill_nated_source_writes_zero',
    'st8_spill_updates_unat_bit',
    'store_invalidates_advanced_load',
    'store_fault_longjmp_closes_alat_writer',
    'store_postinc_x6_38_reserved_illegal_operation',
    'store_postinc_x6_39_reserved_illegal_operation',
    'store_postinc_x6_3a_reserved_illegal_operation',
    'store_x6_38_reserved_illegal_operation',
    'store_x6_39_reserved_illegal_operation',
    'store_x6_3a_reserved_illegal_operation',
    'tbit_nat_source_rules',
    'tnat_nz_and_ignored_bits_decode',
    'tnat_nz_or_decode',
    'tnat_unc_same_pred_pred_false_illegal',
    'unimplemented_physical_load_faults',
    'unimplemented_physical_load_merced_44bit',
    'unimplemented_physical_precludes_unaligned',
    'unimplemented_virtual_load_merced_54bit',
    'unaligned_check_load_full_alat_hit_skips_access',
    'unaligned_check_load_sets_isr_ed',
    'xchg4_decode',
    'xchg4_result_base_alias_invalidates_alat',
    'zero_alat_check_load_always_reloads',
    'zero_alat_chk_a_always_branches',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals())
