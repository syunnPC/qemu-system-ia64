"""Floating-point and register-format microprograms."""

from __future__ import annotations

from .case import bind_cases
from .encoding import (
    ADV_UC_LOAD_BUNDLE,
    ADV_UC_LOAD_DATA,
    ADV_UC_LOAD_VA,
    BINARY32_EDGE_VECTORS,
    BINARY64_EDGE_VECTORS,
    CHECK_LOAD_DATA,
    DEFAULT_FPSR,
    DTR_PTE_UC,
    ExpectedBits,
    ExpectedFP,
    FPSR_SF0_SHIFT,
    FPSR_SF1_SHIFT,
    FPSR_SF2_SHIFT,
    FPSR_SF_D_FLAG,
    FPSR_SF_FLAGS_SHIFT,
    FPSR_SF_RESERVED_PC1,
    FPSR_SF_TD,
    HIGH_TR_BASE,
    IA64_DISABLED_FP_VECTOR,
    IA64_EXCP_BREAK,
    IA64_EXCP_ILLEGAL,
    IA64_EXCP_NAT_CONSUMPTION,
    IA64_EXCP_NONE,
    IA64_EXCP_RESERVED_REG_FIELD,
    IA64_FP_FAULT_VECTOR,
    IA64_FP_TRAP_VECTOR,
    IA64_ISR_CODE_REG_NAT,
    IA64_ISR_CODE_FP,
    IA64_ISR_CODE_SS,
    IA64_ISR_EI_SHIFT,
    IA64_ISR_NI,
    IA64_ISR_R,
    IA64_ISR_W,
    IA64_NAT_CONSUMPTION_VECTOR,
    IA64_PSR_BE,
    IA64_PSR_DFH,
    IA64_PSR_DFL,
    IA64_PSR_IC,
    IA64_PSR_MFH,
    IA64_PSR_MFL,
    IA64_PSR_SS,
    UINT64_MAX,
    addl,
    adds,
    binary32_to_spill,
    binary64_to_spill,
    bitfield,
    br_cond,
    br_ctop_many,
    break_b,
    bundle_words,
    chk_a_clr_f,
    chk_a_nc_f,
    chk_s_f,
    cmp4_eq_unc_imm,
    cmp_eq_imm,
    cmp_ltu_unc,
    dep,
    deterministic_words,
    dtr_setup_bundles,
    extr_u,
    famax,
    famin,
    fand,
    fandcm,
    fchkf,
    fclass_m,
    fclrf,
    fcmp,
    fcvt_fx,
    fcvt_fxu,
    fcvt_xf,
    fma_d_s0,
    fma_s0,
    fma_s1,
    fma_s_s0,
    fmax,
    fmerge_ns,
    fmerge_s,
    fmerge_se,
    fmin,
    fmix_l,
    fmix_lr,
    fmix_r,
    fmov,
    fmpy_s0,
    fmpy_s1,
    fmpy_s_s1,
    fms_d_s0,
    fms_s0,
    fms_s3,
    fnma_d_s1,
    fnma_s0,
    fnma_s1,
    fnma_s_s0,
    fnmpy_s_s1,
    fnorm,
    fnorm_d,
    fnorm_s,
    fnorm_setf_sig,
    for_,
    fpabs,
    fpack,
    fpamax,
    fpamin,
    fpcmp,
    fpcvt_fx,
    fpcvt_fx_trunc,
    fpcvt_fxu,
    fpcvt_fxu_trunc,
    fpma,
    fpmax,
    fpmerge_ns,
    fpmerge_s,
    fpmerge_se,
    fpmin,
    fpms,
    fpneg,
    fpnegabs,
    fpnma,
    fprcpa,
    fprsqrta,
    frcpa,
    frsqrta,
    fselect,
    fsetc,
    fsub_d_s0,
    fsub_s0,
    fsub_s_s0,
    fswap,
    fswap_nl,
    fswap_nr,
    fsxt_l,
    fsxt_r,
    fxor,
    getf_d,
    getf_exp,
    getf_s,
    getf_sig,
    invala_e_fp,
    ld1,
    ld2,
    ld4,
    ld4_a,
    ld4_c_nc,
    ld8,
    ld8_c_nc,
    ld8_fill_postinc,
    ldf8,
    ldf8_a,
    ldf8_c_nc,
    ldf8_s,
    ldf8_sa,
    ldf_fill_postinc,
    ldfd,
    ldfe,
    ldfe_a,
    ldfp8_postinc,
    ldfps,
    ldfs,
    mov_gr_psr_full,
    mov_gr_pr,
    mov_m_psr_gr,
    mov_i_imm_ar,
    mov_lc_imm,
    mov_m_cr_gr,
    mov_m_gr_ar,
    mov_m_imm_ar,
    movl_mlx,
    nop_b,
    nop_f,
    nop_i,
    nop_m,
    require_exception,
    require_registers,
    rfi_to_gr,
    rum,
    setf_d,
    setf_exp,
    setf_s,
    setf_sig,
    spill_to_binary32,
    spill_to_binary64,
    srlz_d,
    ssm,
    st1_postinc,
    st2,
    st4,
    st8,
    stf8,
    stf8_postinc,
    stf_spill_postinc,
    stfd,
    stfe,
    stfs,
    sub_reg,
    sum_um,
    tf_z,
    xma_h,
    xma_hu,
    xma_l,
    xmpy_hu,
)


test_data_big_endian_stf_spill_ldf_fill = require_registers(
    "data_big_endian_stf_spill_ldf_fill", [
        (0x10, 0x00, addl(3, 0x210, 0), addl(4, 0x215, 0),
         addl(5, 0x217, 0)),
        (0x20, 0x00, addl(6, 0x218, 0), addl(7, 0x21f, 0),
         nop_i()),
        (0x30, *movl_mlx(16, 0x1122334455667788)),
        (0x40, 0x09, setf_sig(8, 16), nop_i(),
         nop_i()),
        (0x50, 0x00, sum_um(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0x60, 0x08, stf_spill_postinc(3, 8, 0), nop_i(),
         nop_i()),
        (0x70, 0x09, setf_sig(8, 0), nop_i(),
         nop_i()),
        (0x80, 0x08, ldf_fill_postinc(8, 3, 0), nop_i(),
         nop_i()),
        (0x90, 0x09, rum(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0xa0, 0x08, ld1(10, 3), ld1(11, 4),
         nop_i()),
        (0xb0, 0x08, ld1(12, 5), ld1(13, 6),
         nop_i()),
        (0xc0, 0x08, ld1(14, 7), nop_m(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "f8": ExpectedFP(0x1122334455667788, 0x1003e),
        "r10": 0,
        "r11": 1,
        "r12": 0x3e,
        "r13": 0x11,
        "r14": 0x88,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_data_big_endian_ldfe_stfe = require_registers(
    "data_big_endian_ldfe_stfe", [
        (0x10, 0x00, addl(3, 0x210, 0), addl(4, 0x230, 0),
         addl(5, 0x231, 0)),
        (0x20, 0x00, addl(6, 0x232, 0), addl(7, 0x239, 0),
         adds(16, 0x3f, 0)),
        (0x30, 0x00, adds(17, 0xff, 0), adds(18, 0x80, 0),
         nop_i()),
        (0x40, 0x08, st1_postinc(3, 16, 1), st1_postinc(3, 17, 1),
         nop_i()),
        (0x50, 0x00, st1_postinc(3, 18, 1), nop_i(),
         nop_i()),
        (0x60, 0x00, addl(3, 0x210, 0), nop_i(),
         nop_i()),
        (0x70, 0x00, sum_um(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0x80, 0x00, ldfe(8, 3), nop_i(),
         nop_i()),
        (0x90, 0x00, stfe(4, 8), nop_i(),
         nop_i()),
        (0xa0, 0x00, rum(IA64_PSR_BE), nop_i(),
         nop_i()),
        (0xb0, 0x08, ld1(10, 4), ld1(11, 5),
         nop_i()),
        (0xc0, 0x08, ld1(12, 6), ld1(13, 7),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "r10": 0x3f,
        "r11": 0xff,
        "r12": 0x80,
        "r13": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

LDF8_DATA = bundle_words(0x00, 0x123456789a, 0x1abcdef, 0)[0]

test_ldf8_decode = require_registers("ldf8_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, ldf8(6, 3, hint=1), nop_i(),
     nop_i()),
    (0x30, 0x00, ldf8_s(7, 3), nop_i(),
     nop_i()),
    (0x40, 0x00, ldf8_sa(8, 3), nop_i(),
     nop_i()),
    (0x50, 0x00, getf_sig(4, 6), nop_i(),
     nop_i()),
    (0x60, 0x00, getf_sig(5, 7), nop_i(),
     nop_i()),
    (0x70, 0x00, getf_sig(6, 8), nop_i(),
     nop_i()),
    (0x80, 0x00, nop_m(), nop_i(), nop_i()),
    (0x90, 0x00, nop_m(), nop_i(), nop_i()),
    (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
    (0x100, 0x00, 0x123456789a, 0x1abcdef,
     0),
], {"ip": 0xb0, "r4": LDF8_DATA, "r5_nat": 1, "r6_nat": 1,
    "exception": IA64_EXCP_NONE}, entry=0x10)

LDFD_MEMORY_BITS = 0xc004000000000001

test_ldfd_loads_double_memory_format = require_registers(
    "ldfd_loads_double_memory_format", [
        (0x10, *movl_mlx(2, LDFD_MEMORY_BITS)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, st8(3, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, ldfd(8, 3), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        # Observe the architected FR representation directly: ldfd expands
        # the IEEE binary64 memory encoding into register format (SDM Vol. 1,
        # Figure 5-5), rather than treating it as an integer payload.
        "f8": ExpectedFP(*binary64_to_spill(LDFD_MEMORY_BITS)),
    }, entry=0x10)

test_ldf8_s_chk_s_f_defers_nat_base = require_registers(
    "ldf8_s_chk_s_f_defers_nat_base", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, chk_s_f(7, 0x40, 0x60), nop_i(),
         nop_i()),
        (0x50, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x200, 0x00, 0, 0,
         0),
    ], {"ip": 0x60, "r4": 0, "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldf8_a_chk_a_f_hit = require_registers("ldf8_a_chk_a_f_hit", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, ldf8_a(7, 3), nop_i(),
     nop_i()),
    (0x30, 0x00, chk_a_nc_f(7, 0x30, 0x50), adds(31, 0x56, 0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
    (0x100, 0x00, 0x123456789abcdef0, 0,
     0),
], {"ip": 0x40, "r31": 0x56, "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldfe_a_alat_tracks_ten_byte_operand = require_registers(
    "ldfe_a_alat_tracks_ten_byte_operand", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x20a, 0),
         addl(5, 0x1234, 0)),
        (0x20, 0x00, ldfe_a(7, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, st2(4, 5), nop_i(),
         nop_i()),
        (0x40, 0x00, chk_a_nc_f(7, 0x40, 0x60), adds(31, 0x56, 0),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x200, 0x00, 0x123456789abcdef0, 0x1234,
         0),
    ], {"ip": 0x50, "r31": 0x56, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_stfe_preserves_padding_alat_entry = require_registers(
    "stfe_preserves_padding_alat_entry", [
        (0x10, 0x00, addl(3, 0x20c, 0), addl(4, 0x200, 0),
         addl(5, 0x1234, 0)),
        (0x20, 0x00, ld4_a(6, 3), nop_i(), nop_i()),
        (0x30, 0x09, setf_sig(7, 5), nop_i(), nop_i()),
        (0x40, 0x00, stfe(4, 7), adds(6, 0x55, 0), nop_i()),
        (0x50, 0x00, ld4_c_nc(6, 3), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        (0x200, 0x00, 0x0123456789abcdef, 0x8877665544332211, 0),
    ], {"ip": 0x60, "r6": 0x55, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_ldf8_c_nc_hit_preserves_target = require_registers(
    "ldf8_c_nc_hit_preserves_target", [
        (0x10, 0x00, addl(3, 0x100, 0), addl(4, 0x55, 0),
         nop_i()),
        (0x20, 0x00, ldf8_a(7, 3), nop_i(),
         nop_i()),
        (0x30, 0x09, setf_sig(7, 4), nop_i(),
         nop_i()),
        (0x40, 0x00, ldf8_c_nc(7, 3), nop_i(),
         nop_i()),
        (0x50, 0x10, getf_sig(5, 7), nop_i(),
         br_cond(0x50, 0x60)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x60, "r5": 0x55, "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldf8_c_nc_hit_consumes_nat_base = require_exception(
    "ldf8_c_nc_hit_consumes_nat_base", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ldf8_a(7, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x40, 0x08, ld8_fill_postinc(5, 6, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, ldf8_c_nc(7, 5), nop_i(),
         nop_i()),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x200, 0x00, 0x100, 0,
         0),
    ], IA64_EXCP_NAT_CONSUMPTION, fault_ip=0x50, entry=0x10)

test_ldf8_a_uc_zeroes_target_and_skips_alat = require_registers(
    "ldf8_a_uc_zeroes_target_and_skips_alat", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(),
         nop_i()),
        (0xa0, 0x00, ldf8_a(7, 2), nop_i(),
         nop_i()),
        (0xb0, 0x00, getf_sig(5, 7), nop_i(),
         nop_i()),
        (0xc0, 0x00, ldf8_c_nc(7, 2), nop_i(),
         nop_i()),
        (0xd0, 0x10, getf_sig(6, 7), nop_i(),
         br_cond(0xd0, 0xe0)),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
        ADV_UC_LOAD_BUNDLE,
    ], {"ip": 0xe0, "r5": 0, "r6": ADV_UC_LOAD_DATA,
        "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldf8_c_nc_uc_miss_does_not_allocate_alat = require_registers(
    "ldf8_c_nc_uc_miss_does_not_allocate_alat", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=DTR_PTE_UC),
        (0x70, *movl_mlx(2, ADV_UC_LOAD_VA)),
        (0x80, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x90, 0x08, mov_gr_psr_full(19), srlz_d(), nop_i()),
        (0xa0, 0x00, ldf8_c_nc(7, 2), nop_i(), nop_i()),
        (0xb0, 0x00, chk_a_nc_f(7, 0xb0, 0xd0), adds(5, 1, 0), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
        ADV_UC_LOAD_BUNDLE,
    ], {"ip": 0xd0, "r5": 0, "exception": IA64_EXCP_NONE}, entry=0x10)

test_fp_alat_does_not_satisfy_gr_check_load = require_registers(
    "fp_alat_does_not_satisfy_gr_check_load", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ldf8_a(4, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x55)),
        (0x40, 0x00, ld8_c_nc(4, 3), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x50, "r4": CHECK_LOAD_DATA, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_invala_e_fp_invalidates_selected_register = require_registers(
    "invala_e_fp_invalidates_selected_register", [
        (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, ldf8_a(7, 3), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_a(8, 3), nop_i(),
         nop_i()),
        (0x40, 0x00, invala_e_fp(7), nop_i(),
         nop_i()),
        (0x50, 0x00, chk_a_nc_f(7, 0x50, 0x90), adds(4, 1, 0),
         nop_i()),
        (0x60, 0x00, adds(6, 1, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x90, 0x00, chk_a_nc_f(8, 0x90, 0xc0), adds(5, 1, 0),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0xc0, 0x00, adds(7, 1, 0), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0xa0, "r4": 0, "r5": 1, "r6": 0, "r7": 0,
        "exception": IA64_EXCP_NONE}, entry=0x10)

LDFP8_LOW, LDFP8_HIGH = bundle_words(0x00, 0x0123456789, 0x01abcdef, 0)

test_ldfp8_postinc_decode = require_registers("ldfp8_postinc_decode", [
    (0x10, 0x00, addl(3, 0x100, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, ldfp8_postinc(6, 7, 3), nop_i(),
     nop_i()),
    (0x30, 0x09, getf_sig(4, 6), getf_sig(5, 7),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
    (0x100, 0x00, 0x0123456789, 0x01abcdef,
     0),
], {"ip": 0x40, "r3": 0x110, "r4": LDFP8_LOW, "r5": LDFP8_HIGH,
    "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldf_fill_postinc_decode = require_registers("ldf_fill_postinc_decode", [
    (0x10, *movl_mlx(2, 0x123456789abcdef0)),
    (0x20, 0x00, addl(3, 0x210, 0), addl(7, 0x218, 0),
     nop_i()),
    (0x30, 0x00, addl(5, 0x1003e, 0), nop_i(),
     nop_i()),
    (0x40, 0x08, st8(3, 2), st8(7, 5),
     nop_i()),
    (0x50, 0x08, ldf_fill_postinc(9, 3, -48), nop_i(),
     nop_i()),
    (0x60, 0x00, getf_sig(4, 9), nop_i(),
     nop_i()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
], {
    "ip": 0x70,
    "r3": 0x1e0,
    "r4": 0x123456789abcdef0,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_ldf8_loads_integer_register_format = require_registers(
    "ldf8_loads_integer_register_format", [
        (0x10, *movl_mlx(2, 0x8000000000000000)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, st8(3, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, ldf8(6, 3), nop_i(),
         nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fxu(7, 6),
         nop_i()),
        (0x60, 0x10, getf_sig(4, 7), nop_i(),
         br_cond(0x60, 0x70)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r4": 0x8000000000000000,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_ldfs_expands_single_memory_format = require_registers(
    "ldfs_expands_single_memory_format", [
        (0x10, *movl_mlx(2, 0x3f4ccccd)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(), nop_i()),
        (0x30, 0x00, st4(3, 2), nop_i(), nop_i()),
        (0x40, 0x00, ldfs(6, 3), nop_i(), nop_i()),
        (0x50, 0x09, getf_s(4, 6), getf_d(5, 6), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r4": 0x3f4ccccd,
        "r5": 0x3fe99999a0000000,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_ldfs_preserves_single_nan_payload = require_registers(
    "ldfs_preserves_single_nan_payload", [
        (0x10, *movl_mlx(2, 0x7f812345)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(), nop_i()),
        (0x30, 0x00, st4(3, 2), nop_i(), nop_i()),
        (0x40, 0x00, ldfs(6, 3), nop_i(), nop_i()),
        (0x50, 0x10, getf_s(4, 6), nop_i(), br_cond(0x50, 0x60)),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r4": 0x7f812345,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_ldfps_expands_both_single_values = require_registers(
    "ldfps_expands_both_single_values", [
        (0x10, *movl_mlx(2, 0xc02000003f800000)),
        (0x20, 0x00, addl(3, 0x208, 0), nop_i(), nop_i()),
        (0x30, 0x00, st8(3, 2), nop_i(), nop_i()),
        (0x40, 0x00, ldfps(6, 7, 3), nop_i(), nop_i()),
        (0x50, 0x09, getf_s(4, 6), getf_s(5, 7), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r4": 0x3f800000,
        "r5": 0xc0200000,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_stfs_stfd_convert_register_format = require_registers(
    "stfs_stfd_convert_register_format", [
        (0x10, *movl_mlx(2, 0x3ff8000000000000)),
        (0x20, *movl_mlx(5, 0xffffffffffffffff)),
        (0x30, 0x00, addl(3, 0x200, 0), addl(8, 0x208, 0), nop_i()),
        (0x40, 0x09, setf_d(6, 2), setf_sig(7, 5), nop_i()),
        (0x50, 0x00, stfs(3, 6), nop_i(), nop_i()),
        (0x60, 0x00, stfd(8, 7), nop_i(), nop_i()),
        (0x70, 0x09, ld4(4, 3), ld8(9, 8), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r4": 0x3fc00000,
        "r9": 0x43efffffffffffff,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_getf_exact_register_format_translation = require_registers(
    "getf_exact_register_format_translation", [
        (0x10, *movl_mlx(2, 0xffffffffffffffff)),
        (0x20, 0x00, setf_sig(6, 2), nop_i(), nop_i()),
        (0x30, 0x09, getf_s(4, 6), getf_d(5, 6), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r4": 0x5f7fffff,
        "r5": 0x43efffffffffffff,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_stf8_stfe_convert_register_format = require_registers(
    "stf8_stfe_convert_register_format", [
        (0x10, *movl_mlx(2, 0x3ff0000000000000)),
        (0x20, *movl_mlx(5, 1)),
        (0x30, 0x00, addl(3, 0x200, 0), addl(4, 0x210, 0),
         addl(10, 0x218, 0)),
        (0x40, 0x09, setf_d(6, 2), setf_sig(7, 5), nop_i()),
        (0x50, 0x00, stf8(3, 6), nop_i(), nop_i()),
        (0x60, 0x00, stfe(4, 7), nop_i(), nop_i()),
        (0x70, 0x09, ld8(8, 3), ld8(9, 4), nop_i()),
        (0x80, 0x00, ld2(11, 10), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r8": 0x8000000000000000,
        "r9": 1,
        "r11": 0x403e,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)


def fp_store_natval_consumption_test(name, store):
    return require_registers(name, [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), addl(4, 0x300, 0),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(), nop_i()),
        (0x40, 0x00, ssm(1 << 13), nop_i(), nop_i()),
        (0x50, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x60, 0x00, store(4, 7), nop_i(), nop_i()),
        (IA64_NAT_CONSUMPTION_VECTOR, 0x00,
         mov_m_cr_gr(14, 19), nop_i(), nop_i()),
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
        "r14": 0x60,
        "r15": IA64_ISR_CODE_REG_NAT | IA64_ISR_W,
    }, entry=0x10)


test_stfs_natval_consumption = fp_store_natval_consumption_test(
    "stfs_natval_consumption", stfs)
test_stfd_natval_consumption = fp_store_natval_consumption_test(
    "stfd_natval_consumption", stfd)
test_stf8_natval_consumption = fp_store_natval_consumption_test(
    "stf8_natval_consumption", stf8)
test_stfe_natval_consumption = fp_store_natval_consumption_test(
    "stfe_natval_consumption", stfe)

test_stf_spill_preserves_natval = require_registers(
    "stf_spill_preserves_natval", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), addl(4, 0x300, 0),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), addl(5, 0x308, 0), nop_i()),
        (0x40, 0x00, stf_spill_postinc(4, 7, 0), nop_i(), nop_i()),
        (0x50, 0x09, ld8(8, 4), ld8(9, 5), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": 0x60,
        "r8": 0,
        "r9": 0x1fffe,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_ldf8_f1_illegal_operation = require_exception(
    "ldf8_f1_illegal_operation", [
        (0x10, *movl_mlx(2, 0xdeadbeefcafebabe)),
        (0x20, 0x00, addl(3, 0x200, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, st8(3, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, ldf8(1, 3), nop_i(),
         nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x40)

test_stf_spill_ldf_fill_preserves_sig = require_registers(
    "stf_spill_ldf_fill_preserves_sig", [
        (0x10, *movl_mlx(2, 0x0020c49ba5e353f7)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x200, 0),
         nop_i()),
        (0x30, 0x00, setf_sig(8, 2), nop_i(),
         nop_i()),
        (0x40, 0x08, stf_spill_postinc(3, 8, 16), nop_i(),
         nop_i()),
        (0x50, 0x00, setf_sig(8, 0), nop_i(),
         nop_i()),
        (0x60, 0x08, ldf_fill_postinc(8, 4, 16), nop_i(),
         nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fxu(8, 8),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x90)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "r3": 0x210,
        "r4": 0x210,
        "f8": ExpectedFP(0x0020c49ba5e353f7, 0x1003e),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_chk_s_f_decode = require_registers("chk_s_f_decode", [
    (0x10, 0x00, addl(2, 0x55, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, setf_sig(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x00, chk_s_f(6, 0x30, 0x50), adds(4, 1, 0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 1, "exception": IA64_EXCP_NONE}, entry=0x10)

test_setf_exp_decode = require_registers("setf_exp_decode", [
    (0x10, 0x00, addl(2, 0x1234, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, setf_exp(6, 2, ignored=3), nop_i(),
     nop_i()),
    (0x30, 0x10, getf_exp(4, 6), nop_i(),
     br_cond(0x30, 0x40)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 0x1234, "exception": IA64_EXCP_NONE}, entry=0x10)

test_setf_sig_ignored_bits_decode = require_registers(
    "setf_sig_ignored_bits_decode", [
        (0x10, *movl_mlx(28, 0x123456789abcdef0)),
        (0x20, 0x00, setf_sig(66, 28, ignored=3), nop_i(),
         nop_i()),
        (0x30, 0x10, getf_sig(4, 66), nop_i(),
         br_cond(0x30, 0x40)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {"ip": 0x40, "r4": 0x123456789abcdef0}, entry=0x10)

test_getf_sig_ignored_bits_decode = require_registers(
    "getf_sig_ignored_bits_decode", [
        (0x10, *movl_mlx(28, 0x123456789abcdef0)),
        (0x20, 0x00, setf_sig(66, 28), nop_i(),
         nop_i()),
        (0x30, 0x10, getf_sig(4, 66, ignored=3), nop_i(),
         br_cond(0x30, 0x40)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {"ip": 0x40, "r4": 0x123456789abcdef0}, entry=0x10)

test_stf_spill_postinc_decode = require_registers("stf_spill_postinc_decode", [
    (0x10, 0x00, addl(3, 0x200, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, stf_spill_postinc(3, 0, 128), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r3": 0x280}, entry=0x10)

test_stf8_postinc_imm9_decode = require_registers("stf8_postinc_imm9_decode", [
    (0x10, 0x00, addl(3, 0x200, 0), nop_i(),
     nop_i()),
    (0x20, 0x08, stf8_postinc(3, 0, 128), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r3": 0x280}, entry=0x10)

test_stf8_postinc_stores_setf_sig = require_registers(
    "stf8_postinc_stores_setf_sig", [
        (0x10, *movl_mlx(2, 0xffffffffffffffff)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x200, 0),
         nop_i()),
        (0x30, 0x00, setf_sig(6, 2), nop_i(),
         nop_i()),
        (0x40, 0x08, stf8_postinc(3, 6, 128), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8(5, 4), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "r3": 0x280, "r5": 0xffffffffffffffff},
    entry=0x10)

test_stfe_stores_extended_float = require_registers(
    "stfe_stores_extended_float", [
        (0x10, *movl_mlx(2, 0x3ff0000000000000)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
         nop_i()),
        (0x30, 0x00, setf_d(6, 2), nop_i(),
         nop_i()),
        (0x40, 0x08, stfe(3, 6), nop_i(),
         nop_i()),
        (0x50, 0x00, ld8(5, 3), nop_i(),
         nop_i()),
        (0x60, 0x00, ld2(7, 4), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r5": 0x8000000000000000,
        "r7": 0x3fff,
    }, entry=0x10)

test_ldfe_stfe_preserves_extended_payload = require_registers(
    "ldfe_stfe_preserves_extended_payload", [
        (0x10, *movl_mlx(2, 0x8000000000000001)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
         nop_i()),
        (0x30, 0x00, st8(3, 2), addl(2, 0x4000, 0),
         nop_i()),
        (0x40, 0x00, st2(4, 2), addl(5, 0x300, 0),
         nop_i()),
        (0x50, 0x00, addl(6, 0x308, 0), nop_i(), nop_i()),
        (0x60, 0x00, ldfe(10, 3), nop_i(), nop_i()),
        (0x70, 0x00, stfe(5, 10), nop_i(), nop_i()),
        (0x80, 0x00, ld8(8, 5), nop_i(), nop_i()),
        (0x90, 0x00, ld2(9, 6), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "r8": 0x8000000000000001,
        "r9": 0x4000,
    }, entry=0x10)

test_fma_preserves_extended_precision = require_registers(
    "fma_preserves_extended_precision", [
        (0x10, *movl_mlx(2, 0x8000000000000000)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
         nop_i()),
        (0x30, 0x00, st8(3, 2), addl(5, 0x3fff, 0), nop_i()),
        (0x40, 0x00, st2(4, 5), addl(6, 0x210, 0), nop_i()),
        (0x50, 0x00, addl(7, 0x218, 0), nop_i(), nop_i()),
        (0x60, 0x00, st8(6, 2), addl(5, 0x3fc0, 0), nop_i()),
        (0x70, 0x00, st2(7, 5), nop_i(), nop_i()),
        (0x80, 0x00, ldfe(6, 3), nop_i(), nop_i()),
        (0x90, 0x00, ldfe(7, 6), nop_i(), nop_i()),
        (0xa0, 0x00, ldfe(9, 3), nop_i(), nop_i()),
        (0xb0, 0x0d, nop_m(), fma_s0(8, 6, 9, 7), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "f8": ExpectedFP(0x8000000000000001, 0xffff),
    }, entry=0x10)

test_fp_divzero_fault_discards_result = require_registers(
    "fp_divzero_fault_discards_result", [
        (0x10, *movl_mlx(2, 0x33b)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff0000000000000)),
        (0x40, *movl_mlx(4, 0)),
        (0x50, *movl_mlx(5, 0x4000000000000000)),
        (0x60, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x70, 0x00, setf_d(7, 4), nop_i(), nop_i()),
        (0x80, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(8, 6, 6, 7), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 4,
        "f8": ExpectedFP(0x8000000000000000, 0x10000),
    }, entry=0x10)

test_fcmp_invalid_fault_restores_predicates = require_registers(
    "fcmp_invalid_fault_restores_predicates", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x00, cmp4_eq_unc_imm(6, 7, 0, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x7ff0000000000001)),
        (0x50, *movl_mlx(4, 0x3ff0000000000000)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x70, 0x1c, nop_m(), fcmp(6, 7, 6, 7), nop_b()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 1,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 6),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fcmp_qnan_quiet_relations = require_registers(
    "fcmp_qnan_quiet_relations", [
        (0x10, *movl_mlx(2, 0x7ff8000000001234)),
        (0x20, *movl_mlx(3, 0x3ff0000000000000)),
        (0x30, 0x09, setf_d(6, 2), setf_d(7, 3), nop_i()),
        (0x40, 0x1c, nop_m(), fcmp(6, 7, 6, 7, rel=0, sf=0), nop_b()),
        (0x50, 0x1c, nop_m(), fcmp(8, 9, 6, 7, rel=3, sf=1), nop_b()),
        (0x60, 0x1c, nop_m(), fcmp(10, 11, 6, 7, rel=1, sf=2), nop_b()),
        (0x70, 0x1c, nop_m(), fcmp(12, 13, 6, 7, rel=2, sf=3), nop_b()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "pr_mask": ExpectedBits(
            mask=sum(1 << predicate for predicate in range(6, 14)),
            value=(1 << 7) | (1 << 8) | (1 << 11) | (1 << 13)),
        # eq and unord are quiet; lt and le set V for this QNaN.
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF2_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (1 << (FPSR_SF2_SHIFT + 13 +
                           FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_qnan_quiet_with_invalid_enabled = require_registers(
    "fcmp_qnan_quiet_with_invalid_enabled", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~1)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x00, cmp4_eq_unc_imm(8, 9, 0, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x7ff8000000005678)),
        (0x50, *movl_mlx(4, 0x3ff0000000000000)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        # Quiet eq commits before signaling lt takes the enabled V fault.
        (0x70, 0x1c, nop_m(), fcmp(6, 7, 6, 7, rel=0), nop_b()),
        (0x80, 0x1c, nop_m(), fcmp(8, 9, 6, 7, rel=1), nop_b()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 1,
        "pr_mask": ExpectedBits(
            mask=sum(1 << predicate for predicate in range(6, 10)),
            value=(1 << 7) | (1 << 8)),
        "ar_fpsr": DEFAULT_FPSR & ~1,
    }, entry=0x10)

test_fcmp_unnormal_sets_d = require_registers(
    "fcmp_unnormal_sets_d", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        # Raw +1 encoded with its integer bit clear.
        (0x20, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x1c, nop_m(), fcmp(6, 7, 6, 1, rel=0), nop_b()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 6),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_unnormal_d_fault_restores_predicates = require_registers(
    "fcmp_unnormal_d_fault_restores_predicates", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, cmp4_eq_unc_imm(6, 7, 0, 0),
         addl(3, 0x200, 0), addl(4, 0x208, 0)),
        (0x40, 0x01, addl(21, 0x10000, 0), nop_i(), nop_i()),
        (0x50, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x60, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x70, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        # lt would invert p6/p7; the enabled D fault must preserve them.
        (0x80, 0x1c, nop_m(), fcmp(6, 7, 6, 1, rel=1), nop_b()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fcmp_qnan_suppresses_unnormal_d = require_registers(
    "fcmp_qnan_suppresses_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0x80, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        # Quiet NaN response precedes the enabled D response from f6.
        (0x90, 0x1c, nop_m(), fcmp(6, 7, 8, 6, rel=0), nop_b()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 7),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fp_inexact_trap_commits_result = require_registers(
    "fp_inexact_trap_commits_result", [
        (0x10, *movl_mlx(2, 0x31f)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x400c000000000000)),
        (0x40, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fxu(8, 6), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x2001,
        "f8": ExpectedFP(3, 0x1003e),
    }, entry=0x10)

test_fp_trap_precedes_concurrent_native_single_step = require_registers(
    "fp_trap_precedes_concurrent_native_single_step", [
        (0x10, *movl_mlx(2, 0x31f)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x400c000000000000)),
        (0x40, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x50, *movl_mlx(
            2, IA64_PSR_IC | IA64_PSR_SS | (1 << 41))),
        (0x60, *movl_mlx(3, 0xb0)),
        *rfi_to_gr(0x70, 2, 3),
        (0xb0, 0x0d, nop_m(), fcvt_fxu(8, 6), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 19),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x00, mov_m_cr_gr(11, 22),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x20, 0x00, mov_m_cr_gr(12, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x30, 0x00, mov_m_cr_gr(13, 16),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x40, 0x02, nop_m(),
         extr_u(14, 13, 41, 2), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x50,
                 IA64_FP_TRAP_VECTOR + 0x50)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x50,
        "exception": IA64_EXCP_NONE,
        "r10": 0xb0,
        "r11": 0xb0,
        "r12": (0x2000 | IA64_ISR_CODE_FP | IA64_ISR_CODE_SS |
                (1 << IA64_ISR_EI_SHIFT)),
        "r14": 2,
        "f8": ExpectedFP(3, 0x1003e),
    }, entry=0x10)

test_xma_h_decode = require_registers("xma_h_decode", [
    (0x10, 0x1d, nop_m(), xma_h(8, 0, 6, 7),
     br_cond(0x10, 0x20)),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {
    "ip": 0x20,
    "f8": ExpectedFP(0, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_xma_hu_decode = require_registers("xma_hu_decode", [
    (0x10, *movl_mlx(20, 0xffffffffffffffff)),
    (0x20, *movl_mlx(21, 2)),
    (0x30, *movl_mlx(22, 5)),
    (0x40, 0x00, setf_sig(10, 20), nop_i(),
     nop_i()),
    (0x50, 0x00, setf_sig(70, 21), nop_i(),
     nop_i()),
    (0x60, 0x00, setf_sig(9, 22), nop_i(),
     nop_i()),
    (0x70, 0x1d, nop_m(), xma_hu(11, 9, 10, 70),
     nop_b()),
    (0x80, 0x1d, nop_m(), xmpy_hu(12, 10, 70),
     nop_b()),
    (0x90, 0x10, nop_m(), nop_i(),
     nop_b()),
    (0xa0, 0x10, nop_m(), nop_i(),
     nop_b()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
], {
    "ip": 0xb0,
    "f11": ExpectedFP(2, 0x1003e),
    "f12": ExpectedFP(1, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_xma_fcvt_xf_read_architected_significand = require_registers(
    "xma_fcvt_xf_read_architected_significand", [
        (0x10, *movl_mlx(2, 0x3ff0000000000001)),
        (0x20, 0x00, setf_d(6, 2), nop_i(), nop_i()),
        (0x30, 0x1d, nop_m(), xmpy_hu(8, 6, 1), nop_b()),
        (0x40, 0x0d, nop_m(), fcvt_xf(9, 6), nop_i()),
        (0x50, 0x00, getf_sig(8, 8), nop_i(), nop_i()),
        (0x60, 0x00, getf_sig(9, 9), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r8": 0x4000000000000400,
        "r9": 0xfffffffffffff000,
        "f8": ExpectedFP(0x4000000000000400, 0x1003e),
        "f9": ExpectedFP(0xfffffffffffff000, 0x3003d),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_xma_natval_propagates = require_registers("xma_natval_propagates", [
    (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
     nop_i()),
    (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
     nop_i()),
    (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
     nop_i()),
    (0x40, 0x1d, nop_m(), xma_l(9, 8, 7, 8),
     nop_b()),
    (0x50, 0x1d, nop_m(), xma_h(10, 8, 7, 8),
     nop_b()),
    (0x60, 0x1d, nop_m(), xmpy_hu(11, 7, 8),
     nop_b()),
    (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0xd0)),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0xe0, 0xe0)),
    (0x90, 0x00, chk_s_f(10, 0x90, 0xb0), adds(5, 1, 0),
     nop_i()),
    (0xa0, 0x10, nop_m(), nop_i(),
     br_cond(0xe0, 0xe0)),
    (0xb0, 0x00, chk_s_f(11, 0xb0, 0xd0), adds(12, 1, 0),
     nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(),
     br_cond(0xe0, 0xe0)),
    (0xd0, 0x10, nop_m(), nop_i(),
     br_cond(0xd0, 0xd0)),
    (0xe0, 0x10, nop_m(), nop_i(),
     br_cond(0xe0, 0xe0)),
    (0x200, 0x00, 0, 0,
     0),
], {
    "ip": 0xd0,
    "f9": ExpectedFP(0, 0x1fffe, nat=True),
    "f10": ExpectedFP(0, 0x1fffe, nat=True),
    "f11": ExpectedFP(0, 0x1fffe, nat=True),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fnorm_normalizes_setf_sig_payload = require_registers(
    "fnorm_normalizes_setf_sig_payload", [
    (0x10, 0x00, addl(2, 1, 0), addl(3, 0x895, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), dep(3, 2, 3, 62, 1),
     nop_i()),
    (0x30, 0x00, setf_sig(6, 3), nop_i(),
     nop_i()),
    (0x40, 0x1d, nop_m(), fnorm(7, 0, 6),
     nop_b()),
    (0x50, 0x00, setf_sig(8, 0), nop_i(),
     nop_i()),
    (0x60, 0x1d, nop_m(), fnorm(9, 0, 8, sf=2),
     nop_b()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
], {
    "ip": 0x70,
    "f7": ExpectedFP(*fnorm_setf_sig(0x4000000000000895)),
    "f9": ExpectedFP(*fnorm_setf_sig(0)),
    "ar_fpsr": (DEFAULT_FPSR |
                (FPSR_SF_D_FLAG <<
                 (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                (FPSR_SF_D_FLAG <<
                 (FPSR_SF2_SHIFT + FPSR_SF_FLAGS_SHIFT))),
}, entry=0x10)

test_fnorm_static_masked_overflow_and_ftz = require_registers(
    "fnorm_static_masked_overflow_and_ftz", [
        # sf1: .s, RN, WRE=0.  sf2: .d, RN, FTZ=1, WRE=0.
        (0x10, *movl_mlx(2, 0x0009804d0260033f)),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
         addl(21, 0x1007f, 0)),
        (0x40, 0x01, addl(22, 0x0fc00, 0), nop_i(), nop_i()),
        (0x50, *movl_mlx(23, 0x8000000000000000)),
        (0x60, 0x09, st8(3, 23), st8(4, 21), nop_i()),
        (0x70, 0x09, st8(5, 23), st8(6, 22), nop_i()),
        (0x80, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x90, 0x0d, nop_m(), fnorm_s(8, 0, 6, sf=1), nop_i()),
        (0xa0, 0x0d, nop_m(), fnorm_d(9, 0, 7, sf=2), nop_i()),
        # The same exact tiny value with FTZ clear is a denormal and raises
        # neither U nor I while the exceptions are masked.
        (0xb0, 0x0d, nop_m(), fnorm_d(10, 0, 7, sf=3), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "f8": ExpectedFP(0x8000000000000000, 0x1ffff),
        "f9": ExpectedFP(0, 0),
        "f10": ExpectedFP(0x4000000000000000, 0x0fc01),
        "ar_fpsr": (0x0009804d0260033f |
                    (0x28 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (0x30 << (FPSR_SF2_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fnorm_static_enabled_overflow_wraps_and_sets_fpa = require_registers(
    "fnorm_static_enabled_overflow_wraps_and_sets_fpa", [
        # Enable O and I.  At p=24 this significand rounds upward to 2^128,
        # so the wrapped response reports O|I and ISR.fpa=1.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~((1 << 3) | (1 << 5)))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0x1007e, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0xffffff8000000001)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fnorm_s(8, 0, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2801),
        "f8": ExpectedFP(0x8000000000000000, 0x1007f),
        "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 5))) |
                    (0x28 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnorm_enabled_overflow_reports_masked_inexact_in_isr = require_registers(
    "fnorm_enabled_overflow_reports_masked_inexact_in_isr", [
        # Enable O but leave I masked.  Figure 5-12 still reports the
        # concurrent inexact result in ISR.i for the overflow trap.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 3))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0x1007e, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0xffffff8000000001)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fnorm_s(8, 0, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2801),
        "f8": ExpectedFP(0x8000000000000000, 0x1007f),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 3)) |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnorm_static_enabled_exact_underflow_wraps = require_registers(
    "fnorm_static_enabled_exact_underflow_wraps", [
        # The raw unnormal value normalizes to true biased exponent -62.
        # D is masked; enabled U returns that exponent modulo 2^17.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 4))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 1, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 1)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fnorm_d(8, 0, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x1001,
        "f8": ExpectedFP(0x8000000000000000, 0x1ffc2),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 4)) |
                    (0x12 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnorm_enabled_underflow_reports_masked_inexact_in_isr = require_registers(
    "fnorm_enabled_underflow_reports_masked_inexact_in_isr", [
        # Enable U but leave I masked.  The p24 rounding is inexact and rounds
        # upward, so ISR reports U|I and fpa with the wrapped raw response.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 4))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0xff6a, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0x8000008000000001)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fnorm_s(8, 0, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x3001),
        "f8": ExpectedFP(0x8000010000000000, 0xff6a),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 4)) |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fadd_static_range_and_rounding = require_registers(
    "fadd_static_range_and_rounding", [
        (0x10, *movl_mlx(2, 0x7fefffffffffffff)),
        (0x20, *movl_mlx(3, 0x3ff0000000000000)),
        # 3 * 2^-25 is three quarters of a binary32 ulp at 1.0.
        (0x30, *movl_mlx(4, 0x3e78000000000000)),
        (0x40, 0x09, setf_d(6, 2), setf_d(7, 3), nop_i()),
        (0x50, 0x00, setf_d(8, 4), nop_i(), nop_i()),
        # Static .d must apply the binary64 exponent range.
        (0x60, 0x0d, nop_m(), fma_d_s0(9, 6, 1, 6), nop_i()),
        # Static .s rounds once from the exact mixed-precision sum.
        (0x70, 0x0d, nop_m(), fma_s_s0(10, 7, 1, 8), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f9": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
        "f10": ExpectedFP(*binary32_to_spill(0x3f800001)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fmpy_static_enabled_overflow_wraps_and_sets_fpa = require_registers(
    "fmpy_static_enabled_overflow_wraps_and_sets_fpa", [
        # Multiplication by f1 preserves this exact value; p24 rounding then
        # increments it across Emax.  Enable O and I to observe the raw result.
        (0x10, *movl_mlx(
            2, DEFAULT_FPSR & ~((1 << 3) | (1 << 5)) &
            ~((FPSR_SF_TD | 2) << FPSR_SF1_SHIFT))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0x1007e, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0xffffff8000000001)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fmpy_s_s1(8, 6, 1), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2801),
        "f8": ExpectedFP(0x8000000000000000, 0x1007f),
        "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 5)) &
                     ~((FPSR_SF_TD | 2) << FPSR_SF1_SHIFT)) |
                    (0x28 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fsub_static_enabled_exact_underflow_wraps = require_registers(
    "fsub_static_enabled_exact_underflow_wraps", [
        # 2^-126 - 2^-127 is an exact tiny binary32 result.  Enabled U must
        # trap even without I and deliver the normalized exponent response.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 4))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3810000000000000)),
        (0x40, *movl_mlx(4, 0x3800000000000000)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fsub_s_s0(8, 6, 7), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x1001,
        "f8": ExpectedFP(0x8000000000000000, 0x0ff80),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 4)) |
                    (0x10 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fma_static_rpsp_midpoint_matches_pure_multiply = require_registers(
    "fma_static_rpsp_midpoint_matches_pure_multiply", [
        # The exact product is just above a binary64 midpoint.  Rounding it
        # first to the 113-bit SoftFloat intermediate would lose the sticky
        # bits and then tie downward; the architectural one-step result is up.
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(20, 0xffff, 0), nop_i()),
        (0x30, *movl_mlx(21, 0x8000000000000001)),
        (0x40, *movl_mlx(22, 0x80000000000003ff)),
        (0x50, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x60, 0x09, st8(5, 22), st8(6, 20), nop_i()),
        (0x70, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        # f8 is a numeric +0 addend, while f0 selects the pure-multiply form.
        (0x80, 0x00, setf_d(8, 0), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fma_d_s0(9, 6, 7, 8), nop_i()),
        (0xa0, 0x0d, nop_m(), fma_d_s0(10, 6, 7, 0), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "f9": ExpectedFP(0x8000000000000800, 0x0ffff),
        "f10": ExpectedFP(0x8000000000000800, 0x0ffff),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x20 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fms_static_product_add_cancellation_is_fused = require_registers(
    "fms_static_product_add_cancellation_is_fused", [
        # The 64x64 product is 1 + 2^-53 + a tail.  Subtracting f1 must
        # preserve the product tail through 53 bits of cancellation.
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(20, 0xffff, 0), nop_i()),
        (0x30, *movl_mlx(21, 0x8000000000000001)),
        (0x40, *movl_mlx(22, 0x80000000000003ff)),
        (0x50, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x60, 0x09, st8(5, 22), st8(6, 20), nop_i()),
        (0x70, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x80, 0x0d, nop_m(), fms_d_s0(8, 6, 7, 1), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0x8000000000000000, 0x0ffca),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x20 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

_BINARY64_FAST_SF0_I = (
    DEFAULT_FPSR |
    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT + 5))
)

test_fma_static_binary64_fast_alias = require_registers(
    "fma_static_binary64_fast_alias", [
        # Existing sticky I licenses SoftFloat hardfloat.  The destination
        # aliases the addend, so all three sources must be captured first.
        # Reconstruct one multiplier through spill/fill.  An exact binary64
        # fill must remain eligible for the same fast path.
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff8000000000000)),
        (0x40, *movl_mlx(4, 0x4000000000000000)),
        (0x50, *movl_mlx(5, 0x3fd0000000000000)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x70, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x00, addl(9, 0x200, 0), nop_i(), nop_i()),
        (0x90, 0x08, stf_spill_postinc(9, 6, 0), nop_i(), nop_i()),
        (0xa0, 0x00, setf_d(6, 0), nop_i(), nop_i()),
        (0xb0, 0x08, ldf_fill_postinc(6, 9, 0), nop_i(), nop_i()),
        (0xc0, 0x0d, nop_m(), fma_d_s0(8, 6, 7, 8), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "f8": ExpectedFP(*binary64_to_spill(0x400a000000000000)),
        "ar_fpsr": _BINARY64_FAST_SF0_I,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fms_fnma_static_binary64_fast = require_registers(
    "fms_fnma_static_binary64_fast", [
        # sf1 defaults to WRE=1; clear it so both static .d operations use the
        # IEEE binary64 computation model, with I already sticky in each SF.
        (0x10, *movl_mlx(
            2, (DEFAULT_FPSR & ~(2 << FPSR_SF1_SHIFT)) |
            (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT + 5)) |
            (1 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT + 5)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff8000000000000)),
        (0x40, *movl_mlx(4, 0x4000000000000000)),
        (0x50, *movl_mlx(5, 0x3fd0000000000000)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x70, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fms_d_s0(9, 6, 7, 8), nop_i()),
        (0x90, 0x0d, nop_m(), fnma_d_s1(10, 6, 7, 8), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f9": ExpectedFP(*binary64_to_spill(0x4006000000000000)),
        "f10": ExpectedFP(*binary64_to_spill(0xc006000000000000)),
        "ar_fpsr": ((DEFAULT_FPSR & ~(2 << FPSR_SF1_SHIFT)) |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT + 5)) |
                    (1 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT + 5))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary64_fast_masked_overflow = require_registers(
    "fma_static_binary64_fast_masked_overflow", [
        # The sentinel I must not hide a newly raised masked O flag.
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x7fefffffffffffff)),
        (0x40, *movl_mlx(4, 0x4000000000000000)),
        (0x50, *movl_mlx(5, 0x3ff0000000000000)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x70, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fma_d_s0(9, 6, 7, 8), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f9": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary64_fast_ftz_underflow = require_registers(
    "fma_static_binary64_fast_ftz_underflow", [
        # Exact 2^-1023 is tiny in the .d model.  FTZ converts it to zero and
        # output_denormal_flushed must become the architectural U|I flags.
        (0x10, *movl_mlx(
            2, _BINARY64_FAST_SF0_I | (1 << FPSR_SF0_SHIFT))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x0010000000000000)),
        (0x40, *movl_mlx(4, 0x3fe0000000000000)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x60, 0x00, setf_d(8, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fma_d_s0(9, 6, 7, 8), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f9": ExpectedFP(*binary64_to_spill(0)),
        "ar_fpsr": (DEFAULT_FPSR | (1 << FPSR_SF0_SHIFT) |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary64_fast_special_fallback = require_registers(
    "fma_static_binary64_fast_special_fallback", [
        # A compact qNaN is not admitted to the normal/zero fast path; retain
        # the architectural register-priority propagation in the slow path.
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff8000000000000)),
        (0x40, *movl_mlx(4, 0x4000000000000000)),
        (0x50, *movl_mlx(5, 0x7ff8123456789abc)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x70, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fma_d_s0(9, 6, 7, 8), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f9": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "ar_fpsr": _BINARY64_FAST_SF0_I,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary64_fast_non_rn_fallback = require_registers(
    "fma_static_binary64_fast_non_rn_fallback", [
        # Round toward +infinity makes the halfway result advance by one ulp.
        # This must use the exact raw path despite compact binary64 operands.
        (0x10, *movl_mlx(
            2, _BINARY64_FAST_SF0_I |
            (2 << (FPSR_SF0_SHIFT + 4)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff0000000000000)),
        (0x40, *movl_mlx(4, 0x3ca0000000000000)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fma_d_s0(8, 6, 6, 7), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "f8": ExpectedFP(*binary64_to_spill(0x3ff0000000000001)),
        "ar_fpsr": (_BINARY64_FAST_SF0_I |
                    (2 << (FPSR_SF0_SHIFT + 4))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary32_fast_alias_fms_fnma = require_registers(
    "fma_static_binary32_fast_alias_fms_fnma", [
        # Static .s still consumes full registers.  These sources are exact
        # binary32 normals, and the FMA destination aliases its addend.
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3fc00000)),
        (0x40, *movl_mlx(4, 0x40000000)),
        (0x50, *movl_mlx(5, 0x3e800000)),
        (0x60, 0x09, setf_s(6, 3), setf_s(7, 4), nop_i()),
        (0x70, 0x09, setf_s(8, 5), setf_s(9, 5), nop_i()),
        (0x80, 0x0d, nop_m(), fma_s_s0(8, 6, 7, 8), nop_i()),
        (0x90, 0x0d, nop_m(),
         fms_s0(10, 6, 7, 9) | bitfield(1, 36, 1), nop_i()),
        (0xa0, 0x0d, nop_m(), fnma_s_s0(11, 6, 7, 9), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "f8": ExpectedFP(*binary32_to_spill(0x40500000)),
        "f10": ExpectedFP(*binary32_to_spill(0x40300000)),
        "f11": ExpectedFP(*binary32_to_spill(0xc0300000)),
        "ar_fpsr": _BINARY64_FAST_SF0_I,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary32_fast_masked_overflow = require_registers(
    "fma_static_binary32_fast_masked_overflow", [
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x7f7fffff)),
        (0x40, *movl_mlx(4, 0x40000000)),
        (0x50, *movl_mlx(5, 0x3f800000)),
        (0x60, 0x09, setf_s(6, 3), setf_s(7, 4), nop_i()),
        (0x70, 0x00, setf_s(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fma_s_s0(9, 6, 7, 8), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f9": ExpectedFP(*binary32_to_spill(0x7f800000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary32_fast_ftz_underflow = require_registers(
    "fma_static_binary32_fast_ftz_underflow", [
        (0x10, *movl_mlx(
            2, _BINARY64_FAST_SF0_I | (1 << FPSR_SF0_SHIFT))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x00800000)),
        (0x40, *movl_mlx(4, 0x3f000000)),
        (0x50, 0x09, setf_s(6, 3), setf_s(7, 4), nop_i()),
        (0x60, 0x00, setf_s(8, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fma_s_s0(9, 6, 7, 8), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f9": ExpectedFP(*binary32_to_spill(0)),
        "ar_fpsr": (DEFAULT_FPSR | (1 << FPSR_SF0_SHIFT) |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary32_fast_non_rn_fallback = require_registers(
    "fma_static_binary32_fast_non_rn_fallback", [
        (0x10, *movl_mlx(
            2, _BINARY64_FAST_SF0_I |
            (2 << (FPSR_SF0_SHIFT + 4)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3f800000)),
        (0x40, *movl_mlx(4, 0x33800000)),
        (0x50, 0x09, setf_s(6, 3), setf_s(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fma_s_s0(8, 6, 6, 7), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "f8": ExpectedFP(*binary32_to_spill(0x3f800001)),
        "ar_fpsr": (_BINARY64_FAST_SF0_I |
                    (2 << (FPSR_SF0_SHIFT + 4))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary32_nonexact_operand_fallback = require_registers(
    "fma_static_binary32_nonexact_operand_fallback", [
        # 1 + 2^-24 + 2^-52 is compact binary64 but not exact binary32.
        # Full-register static .s arithmetic must retain the final tail.
        (0x10, *movl_mlx(2, _BINARY64_FAST_SF0_I)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff0000010000001)),
        (0x40, *movl_mlx(4, 0x3f800000)),
        (0x50, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x60, 0x09, setf_s(7, 4), setf_s(8, 0), nop_i()),
        (0x70, 0x0d, nop_m(), fma_s_s0(9, 6, 7, 8), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f9": ExpectedFP(*binary32_to_spill(0x3f800001)),
        "ar_fpsr": _BINARY64_FAST_SF0_I,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_ldf_fill_binary64_subnormal_qnan_suppresses_d = require_registers(
    "fma_ldf_fill_binary64_subnormal_qnan_suppresses_d", [
        # The exact binary64 subnormal spill encoding must retain its
        # denormal classification.  A quiet NaN has priority over D, so an
        # enabled D exception must neither fault nor become sticky.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0x0fc01, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0x0000000000000800)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, *movl_mlx(5, 0x7ff8123456789abc)),
        (0x80, 0x00, setf_d(8, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fma_d_s0(9, 6, 1, 8), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f6": ExpectedFP(0x0000000000000800, 0x0fc01),
        "f9": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

_IEEE_FAST_PREROUND_FPSR = (
    (DEFAULT_FPSR & ~(2 << FPSR_SF1_SHIFT)) |
    (1 << FPSR_SF1_SHIFT) |
    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT + 5)) |
    (1 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT + 5))
)

test_fma_static_binary32_fast_preround_underflow = require_registers(
    "fma_static_binary32_fast_preround_underflow", [
        # min-normal * nextdown(1) is tiny before rounding but rounds back to
        # min-normal.  IA-64 reports U|I before rounding; FTZ instead returns
        # zero.  Every source remains fast-path eligible.
        (0x10, *movl_mlx(2, _IEEE_FAST_PREROUND_FPSR)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x00800000)),
        (0x40, *movl_mlx(4, 0x3f7fffff)),
        (0x50, 0x09, setf_s(6, 3), setf_s(7, 4), nop_i()),
        (0x60, 0x00, setf_s(8, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fma_s_s0(9, 6, 7, 8), nop_i()),
        (0x80, 0x0d, nop_m(),
         fma_s1(10, 6, 7, 8) | bitfield(1, 36, 1), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f9": ExpectedFP(*binary32_to_spill(0x00800000)),
        "f10": ExpectedFP(*binary32_to_spill(0)),
        "ar_fpsr": (_IEEE_FAST_PREROUND_FPSR |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (0x30 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_binary64_fast_preround_underflow = require_registers(
    "fma_static_binary64_fast_preround_underflow", [
        (0x10, *movl_mlx(2, _IEEE_FAST_PREROUND_FPSR)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x0010000000000000)),
        (0x40, *movl_mlx(4, 0x3fefffffffffffff)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x60, 0x00, setf_d(8, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fma_d_s0(9, 6, 7, 8), nop_i()),
        (0x80, 0x0d, nop_m(),
         fma_d_s0(10, 6, 7, 8) | bitfield(1, 34, 2), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f9": ExpectedFP(*binary64_to_spill(0x0010000000000000)),
        "f10": ExpectedFP(*binary64_to_spill(0)),
        "ar_fpsr": (_IEEE_FAST_PREROUND_FPSR |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (0x30 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_static_enabled_exact_underflow_wraps = require_registers(
    "fma_static_enabled_exact_underflow_wraps", [
        # 2^-126 * 0.5 + numeric +0 is exactly 2^-127.  Enabled U commits
        # the normalized wide-range result before taking the trap.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 4))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3810000000000000)),
        (0x40, *movl_mlx(4, 0x3fe0000000000000)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        (0x60, 0x00, setf_d(9, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fma_s_s0(8, 6, 7, 9), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x1001,
        "f8": ExpectedFP(0x8000000000000000, 0x0ff80),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 4)) |
                    (0x10 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnma_static_enabled_overflow_wraps_and_sets_fpa = require_registers(
    "fnma_static_enabled_overflow_wraps_and_sets_fpa", [
        # Negating the exact product changes only its sign.  Rounding at p=24
        # crosses Emax and enabled O|I returns the negative wrapped result.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~((1 << 3) | (1 << 5)))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(21, 0x1007e, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(22, 0xffffff8000000001)),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x00, setf_d(7, 0), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fnma_s_s0(8, 6, 1, 7), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2801),
        "f8": ExpectedFP(0x8000000000000000, 0x3007f),
        "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 5))) |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fmpy_dynamic_wre0_extended_precision_rounds_once = require_registers(
    "fmpy_dynamic_wre0_extended_precision_rounds_once", [
        # sf0 selects dynamic pc=11, WRE=0: 64-bit precision and a 15-bit
        # exponent range.  The exact product lies just above a p64 midpoint.
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(20, 0xffff, 0), nop_i()),
        (0x30, *movl_mlx(21, 0x8000000000000001)),
        (0x40, *movl_mlx(22, 0xc000000000000001)),
        (0x50, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x60, 0x09, st8(5, 22), st8(6, 20), nop_i()),
        (0x70, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x80, 0x0d, nop_m(), fmpy_s0(8, 6, 7), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0xc000000000000003, 0x0ffff),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x20 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fnorm_dynamic_wre0_precision_uses_15bit_range = require_registers(
    "fnorm_dynamic_wre0_precision_uses_15bit_range", [
        # Configure dynamic pc=00 in sf1 and pc=10 in sf2; sf0 is pc=11.
        # WRE remains clear in all three fields.
        (0x10, 0x0d, nop_m(), fsetc(1, 0, 0), nop_i()),
        (0x20, 0x0d, nop_m(), fsetc(2, 0, 0x08), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(20, 0x11000, 0)),
        (0x40, *movl_mlx(21, 0x8000008000000401)),
        (0x50, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        # This exponent exceeds both IEEE single and double Emax, but lies
        # within the 15-bit range selected by a dynamic instruction.
        (0x70, 0x0d, nop_m(), fnorm(8, 0, 6, sf=1), nop_i()),
        (0x80, 0x0d, nop_m(), fnorm(9, 0, 6, sf=2), nop_i()),
        (0x90, 0x0d, nop_m(), fnorm(10, 0, 6, sf=0), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f8": ExpectedFP(0x8000010000000000, 0x11000),
        "f9": ExpectedFP(0x8000008000000800, 0x11000),
        "f10": ExpectedFP(0x8000008000000401, 0x11000),
        "ar_fpsr": ((DEFAULT_FPSR &
                     ~(0x7f << FPSR_SF1_SHIFT) &
                     ~(0x7f << FPSR_SF2_SHIFT)) |
                    (0x08 << FPSR_SF2_SHIFT) |
                    (0x20 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (0x20 <<
                     (FPSR_SF2_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_dynamic_wre0_enabled_overflow_wraps_and_sets_fpa = (
    require_registers(
        "fma_dynamic_wre0_enabled_overflow_wraps_and_sets_fpa", [
            # The p64 product rounds across the 15-bit Emax.  With O and I
            # enabled, the register receives the 17-bit wrapped response.
            (0x10, *movl_mlx(
                2, DEFAULT_FPSR & ~((1 << 3) | (1 << 5)))),
            (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
             addl(4, 0x208, 0)),
            (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
             addl(20, 0x13ffe, 0)),
            (0x40, 0x01, addl(23, 0xffff, 0), nop_i(), nop_i()),
            (0x50, *movl_mlx(21, 0xfffffffffffffffe)),
            (0x60, *movl_mlx(22, 0x8000000000000001)),
            (0x70, 0x09, st8(3, 21), st8(4, 20), nop_i()),
            (0x80, 0x09, st8(5, 22), st8(6, 23), nop_i()),
            (0x90, 0x09, ldf_fill_postinc(6, 3, 0),
             ldf_fill_postinc(7, 5, 0), nop_i()),
            (0xa0, 0x00, setf_d(8, 0), nop_i(), nop_i()),
            (0xb0, 0x0d, nop_m(), fma_s0(9, 6, 7, 8), nop_i()),
            (IA64_FP_TRAP_VECTOR, 0x00,
             mov_m_cr_gr(10, 17), nop_i(), nop_i()),
            (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
             br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                     IA64_FP_TRAP_VECTOR + 0x10)),
        ], {
            "ip": IA64_FP_TRAP_VECTOR + 0x10,
            "exception": IA64_EXCP_NONE,
            "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                    (1 << 14) | 0x2801),
            "f9": ExpectedFP(0x8000000000000000, 0x13fff),
            "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 5))) |
                        (0x28 <<
                         (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        }, entry=0x10)
)

test_fmpy_dynamic_wre0_enabled_exact_underflow_wraps = require_registers(
    "fmpy_dynamic_wre0_enabled_exact_underflow_wraps", [
        # 2^-16382 * 0.5 is an exact tiny result in the dynamic 15-bit model.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 4))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(20, 0x0c001, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(21, 0x8000000000000000)),
        (0x50, *movl_mlx(22, 0x3fe0000000000000)),
        (0x60, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x70, 0x09, ldf_fill_postinc(6, 3, 0), setf_d(7, 22), nop_i()),
        (0x80, 0x0d, nop_m(), fmpy_s0(8, 6, 7), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x1001,
        "f8": ExpectedFP(0x8000000000000000, 0x0c000),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 4)) |
                    (0x10 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnorm_wre1_precision_and_range = require_registers(
    "fnorm_wre1_precision_and_range", [
        # Default sf1 has WRE=1 and pc=11.  Static .s/.d ignore that pc but
        # retain the 17-bit range; the dynamic form uses p64.
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(20, 0x18000, 0)),
        (0x20, *movl_mlx(21, 0x8000008000000401)),
        (0x30, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fnorm_s(8, 0, 6, sf=1), nop_i()),
        (0x60, 0x0d, nop_m(), fnorm_d(9, 0, 6, sf=1), nop_i()),
        (0x70, 0x0d, nop_m(), fnorm(10, 0, 6, sf=1), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f8": ExpectedFP(0x8000010000000000, 0x18000),
        "f9": ExpectedFP(0x8000008000000800, 0x18000),
        "f10": ExpectedFP(0x8000008000000401, 0x18000),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x20 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_binary_wre1_transports_17bit_range = require_registers(
    "fp_binary_wre1_transports_17bit_range", [
        (0x10, *movl_mlx(
            2, DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
         addl(20, 0x18000, 0)),
        (0x40, 0x01, addl(23, 0x17fff, 0), nop_i(), nop_i()),
        (0x50, *movl_mlx(21, 0x8000000000000000)),
        (0x60, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x70, 0x09, st8(5, 21), st8(6, 23), nop_i()),
        (0x80, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x90, 0x0d, nop_m(), fma_s0(8, 6, 1, 6), nop_i()),
        (0xa0, 0x0d, nop_m(), fsub_s0(9, 6, 7), nop_i()),
        # Exact cancellation must not narrow both wide operands to infinity.
        (0xb0, 0x0d, nop_m(), fsub_s0(10, 6, 6), nop_i()),
        (0xc0, 0x0d, nop_m(), fmpy_s0(11, 6, 1), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(), br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "f8": ExpectedFP(0x8000000000000000, 0x18001),
        "f9": ExpectedFP(0x8000000000000000, 0x17fff),
        "f10": ExpectedFP(0, 0),
        "f11": ExpectedFP(0x8000000000000000, 0x18000),
        "ar_fpsr": DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_binary_wre1_endpoint_delivery = require_registers(
    "fp_binary_wre1_endpoint_delivery", [
        # sf1 is WRE=1/p64.  Adding two largest-exponent values overflows;
        # halving the smallest normal produces an exact register denormal.
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(20, 0x1fffe, 0),
         addl(23, 1, 0)),
        (0x30, *movl_mlx(21, 0xffffffffffffffff)),
        (0x40, *movl_mlx(22, 0x8000000000000000)),
        (0x50, *movl_mlx(24, 0x3fe0000000000000)),
        (0x60, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x70, 0x09, st8(5, 22), st8(6, 23), nop_i()),
        (0x80, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x90, 0x00, setf_d(8, 24), nop_i(), nop_i()),
        (0xa0, 0x0d, nop_m(), fma_s1(9, 6, 1, 6), nop_i()),
        (0xb0, 0x0d, nop_m(), fmpy_s1(10, 7, 8), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "f9": ExpectedFP(0x8000000000000000, 0x1ffff),
        "f10": ExpectedFP(0x4000000000000000, 1),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x28 <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_muladd_wre1_fused_cancellation = require_registers(
    "fp_muladd_wre1_fused_cancellation", [
        (0x10, *movl_mlx(
            2, DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
         addl(20, 0x14000, 0)),
        (0x40, 0x01, addl(25, 0x220, 0), addl(26, 0x228, 0),
         addl(28, 0x230, 0)),
        (0x50, 0x01, addl(29, 0x238, 0), addl(23, 0x18002, 0),
         nop_i()),
        (0x60, *movl_mlx(21, 0xc000000000000000)),
        (0x70, *movl_mlx(22, 0x8000000000000000)),
        (0x80, *movl_mlx(24, 0x9000000000000000)),
        (0x90, *movl_mlx(27, 0x38002)),
        (0xa0, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0xb0, 0x09, st8(5, 21), st8(6, 20), nop_i()),
        (0xc0, 0x09, st8(25, 22), st8(26, 23), nop_i()),
        (0xd0, 0x09, st8(28, 24), st8(29, 27), nop_i()),
        (0xe0, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0xf0, 0x09, ldf_fill_postinc(8, 25, 0),
         ldf_fill_postinc(9, 28, 0), nop_i()),
        (0x100, 0x0d, nop_m(), fma_s0(10, 6, 7, 9), nop_i()),
        (0x110, 0x0d, nop_m(), fms_s0(11, 6, 7, 8), nop_i()),
        (0x120, 0x0d, nop_m(), fnma_s0(12, 6, 7, 8), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(), br_cond(0x130, 0x130)),
    ], {
        "ip": 0x130,
        "f10": ExpectedFP(0, 0),
        "f11": ExpectedFP(0x8000000000000000, 0x17fff),
        "f12": ExpectedFP(0x8000000000000000, 0x37fff),
        "ar_fpsr": DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_muladd_wre1_enabled_overflow_wraps_and_sets_fpa = require_registers(
    "fp_muladd_wre1_enabled_overflow_wraps_and_sets_fpa", [
        # At p24 the exact fused sum is the midpoint above the largest
        # significand at Emax.  Ties-to-even increments it across Emax, so
        # enabled O returns the 17-bit exponent and reports I with fpa=1.
        (0x10, *movl_mlx(
            2, (DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT)) &
            ~((1 << 3) | (1 << 5)))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
         addl(20, 0x1fffe, 0)),
        (0x40, 0x01, addl(22, 0x1ffbf, 0), nop_i(), nop_i()),
        (0x50, *movl_mlx(21, 0xffffff7fffffffff)),
        (0x60, *movl_mlx(23, 0x8000000000000000)),
        (0x70, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x80, 0x09, st8(5, 23), st8(6, 22), nop_i()),
        (0x90, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0xa0, 0x0d, nop_m(), fma_s_s0(8, 6, 1, 7), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00,
         mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2801),
        "f8": ExpectedFP(0x8000000000000000, 0x1ffff),
        "ar_fpsr": (((DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT)) &
                     ~((1 << 3) | (1 << 5))) |
                    (0x28 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fp_muladd_wre1_enabled_exact_underflow_wraps = require_registers(
    "fp_muladd_wre1_enabled_exact_underflow_wraps", [
        # -(1.5 * 1.0) + 1.0 is exactly -0.5 at WRE1 Emin.  Enabled U
        # delivers the normalized true exponent modulo 2^17 without I/FPA.
        (0x10, *movl_mlx(
            2, (DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT)) & ~(1 << 4))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(5, 0x210, 0), addl(6, 0x218, 0),
         addl(20, 1, 0)),
        (0x40, *movl_mlx(21, 0xc000000000000000)),
        (0x50, *movl_mlx(22, 0x8000000000000000)),
        (0x60, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x70, 0x09, st8(5, 22), st8(6, 20), nop_i()),
        (0x80, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x90, 0x0d, nop_m(), fnma_s0(8, 6, 1, 7), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00,
         mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x1001,
        "f8": ExpectedFP(0x8000000000000000, 0x20000),
        "ar_fpsr": (((DEFAULT_FPSR | (2 << FPSR_SF0_SHIFT)) &
                     ~(1 << 4)) |
                    (0x10 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fnorm_ldf_fill_unnormal_sets_d = require_registers(
    "fnorm_ldf_fill_unnormal_sets_d", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x20, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x1d, nop_m(), fnorm(8, 0, 6, sf=0), nop_b()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fnorm_unnormal_d_fault_rolls_back = require_registers(
    "fnorm_unnormal_d_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 2, 0), nop_i()),
        (0x30, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x40, 0x09, setf_sig(6, 3), setf_d(8, 5), nop_i()),
        (0x50, 0x1d, nop_m(), fnorm(8, 0, 6, sf=0), nop_b()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

_GETF_EXP_SIG_VALUE = 0x115557000
_GETF_EXP_SIG_EXPECTED = 0xffff + (_GETF_EXP_SIG_VALUE.bit_length() - 1)

test_getf_exp_after_fnorm_sig = require_registers("getf_exp_after_fnorm_sig", [
    (0x10, *movl_mlx(3, _GETF_EXP_SIG_VALUE)),
    (0x20, 0x00, setf_sig(6, 3), nop_i(),
     nop_i()),
    (0x30, 0x1d, nop_m(), fnorm(7, 0, 6),
     nop_b()),
    (0x40, 0x10, getf_exp(4, 7), nop_i(),
     br_cond(0x40, 0x50)),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": _GETF_EXP_SIG_EXPECTED}, entry=0x10)

test_fpabs_fpneg_decode = require_registers("fpabs_fpneg_decode", [
    (0x10, *movl_mlx(2, 0xbf80000040000000)),
    (0x20, *movl_mlx(3, 0x3ff0000000000001)),
    (0x30, 0x00, setf_sig(6, 2), nop_i(),
     nop_i()),
    (0x40, 0x00, setf_d(7, 3), nop_i(),
     nop_i()),
    (0x50, 0x0d, nop_m(), fpabs(8, 6),
     nop_i()),
    (0x60, 0x0d, nop_m(), fpneg(9, 6),
     nop_i()),
    (0x70, 0x0d, nop_m(), fpnegabs(10, 6),
     nop_i()),
    (0x80, 0x0d, nop_m(), fpabs(11, 7),
     nop_i()),
    (0x90, 0x0d, nop_m(), fpneg(12, 7),
     nop_i()),
    (0xa0, 0x0d, nop_m(), fpnegabs(13, 7),
     nop_i()),
    (0xb0, 0x00, getf_sig(8, 8), nop_i(), nop_i()),
    (0xc0, 0x00, getf_sig(9, 9), nop_i(), nop_i()),
    (0xd0, 0x00, getf_sig(10, 10), nop_i(), nop_i()),
    (0xe0, 0x00, getf_sig(11, 11), nop_i(), nop_i()),
    (0xf0, 0x00, getf_sig(12, 12), nop_i(), nop_i()),
    (0x100, 0x00, getf_sig(13, 13), nop_i(), nop_i()),
    (0x110, 0x10, nop_m(), nop_i(),
     br_cond(0x110, 0x110)),
], {
    "ip": 0x110,
    "r8": 0x3f80000040000000,
    "r9": 0x3f800000c0000000,
    "r10": 0xbf800000c0000000,
    "r11": 0x0000000000000800,
    "r12": 0x0000000080000800,
    "r13": 0x8000000080000800,
    "f8": ExpectedFP(0x3f80000040000000, 0x1003e),
    "f9": ExpectedFP(0x3f800000c0000000, 0x1003e),
    "f10": ExpectedFP(0xbf800000c0000000, 0x1003e),
    "f11": ExpectedFP(0x0000000000000800, 0x1003e),
    "f12": ExpectedFP(0x0000000080000800, 0x1003e),
    "f13": ExpectedFP(0x8000000080000800, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fmerge_forms_decode = require_registers("fmerge_forms_decode", [
    (0x10, *movl_mlx(2, 0xc008000000000001)),
    (0x20, *movl_mlx(3, 0xbff0000000000002)),
    (0x30, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x40, 0x00, setf_d(7, 3), nop_i(),
     nop_i()),
    (0x50, 0x0d, nop_m(), fmerge_ns(8, 6, 7),
     nop_i()),
    (0x60, 0x0d, nop_m(), fmerge_s(9, 6, 7),
     nop_i()),
    (0x70, 0x0d, nop_m(), fmerge_se(10, 6, 7),
     nop_i()),
    (0x80, 0x10, nop_m(), nop_i(),
     nop_b()),
    (0x90, 0x10, nop_m(), nop_i(),
     nop_b()),
    (0xa0, 0x10, nop_m(), nop_i(),
     br_cond(0xa0, 0xa0)),
], {
    "ip": 0xa0,
    "f8": ExpectedFP(*binary64_to_spill(0x3ff0000000000002)),
    "f9": ExpectedFP(*binary64_to_spill(0xbff0000000000002)),
    "f10": ExpectedFP(*binary64_to_spill(0xc000000000000002)),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fmerge_se_fixed_register_format_edges = require_registers(
    "fmerge_se_fixed_register_format_edges", [
        (0x10, 0x0d, nop_m(), fmerge_se(8, 1, 0), nop_i()),
        (0x20, 0x0d, nop_m(), fmerge_se(9, 0, 1), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "f8": ExpectedFP(0, 0xffff),
        "f9": ExpectedFP(1 << 63, 0),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fmerge_natval_propagates = require_registers(
    "fmerge_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x0d, nop_m(), fmerge_s(8, 7, 1),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x00, adds(5, 1, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f8": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fminmax_scalar_decode = require_registers("fminmax_scalar_decode", [
    (0x10, *movl_mlx(2, 0x3ff0000000000000)),
    (0x20, *movl_mlx(3, 0xc000000000000000)),
    (0x30, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x40, 0x00, setf_d(7, 3), nop_i(),
     nop_i()),
    (0x50, 0x0d, nop_m(), fmin(8, 6, 7),
     nop_i()),
    (0x60, 0x0d, nop_m(), fmax(9, 6, 7, sf=1),
     nop_i()),
    (0x70, 0x0d, nop_m(), famin(10, 6, 7, sf=2),
     nop_i()),
    (0x80, 0x0d, nop_m(), famax(11, 6, 7, sf=3, bit36=1),
     nop_i()),
    (0x90, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xa0, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
], {
    "ip": 0xb0,
    "f8": ExpectedFP(*binary64_to_spill(0xc000000000000000)),
    "f9": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
    "f10": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
    "f11": ExpectedFP(*binary64_to_spill(0xc000000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fminmax_scalar_tie_uses_f3 = require_registers(
    "fminmax_scalar_tie_uses_f3", [
        (0x10, *movl_mlx(2, 0x0000000000000000)),
        (0x20, *movl_mlx(3, 0x8000000000000000)),
        (0x30, *movl_mlx(4, 0x3ff0000000000000)),
        (0x40, *movl_mlx(5, 0xbff0000000000000)),
        (0x50, 0x09, setf_d(6, 2), setf_d(7, 3),
         nop_i()),
        (0x60, 0x09, setf_d(8, 4), setf_d(9, 5),
         nop_i()),
        (0x70, 0x0d, nop_m(), fmin(10, 6, 7),
         nop_i()),
        (0x80, 0x0d, nop_m(), fmax(11, 6, 7),
         nop_i()),
        (0x90, 0x0d, nop_m(), famin(12, 8, 9),
         nop_i()),
        (0xa0, 0x0d, nop_m(), famax(13, 8, 9),
         nop_i()),
        (0xb0, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xc0, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "f10": ExpectedFP(*binary64_to_spill(0x8000000000000000)),
        "f11": ExpectedFP(*binary64_to_spill(0x8000000000000000)),
        "f12": ExpectedFP(*binary64_to_spill(0xbff0000000000000)),
        "f13": ExpectedFP(*binary64_to_spill(0xbff0000000000000)),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_wre1_orders_full_register_range = require_registers(
    "fcmp_wre1_orders_full_register_range", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(7, 0x220, 0),
         addl(8, 0x228, 0)),
        (0x30, 0x01, addl(9, 0x230, 0), addl(10, 0x238, 0),
         addl(21, 0x18000, 0)),
        (0x40, 0x01, addl(22, 0x18001, 0), addl(23, 0x8000, 0),
         addl(24, 0x8001, 0)),
        (0x50, *movl_mlx(20, 0x8000000000000000)),
        (0x60, 0x09, st8(3, 20), st8(4, 21), nop_i()),
        (0x70, 0x09, st8(5, 20), st8(6, 22), nop_i()),
        (0x80, 0x09, st8(7, 20), st8(8, 23), nop_i()),
        (0x90, 0x09, st8(9, 20), st8(10, 24), nop_i()),
        (0xa0, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0xb0, 0x09, ldf_fill_postinc(8, 7, 0),
         ldf_fill_postinc(9, 9, 0), nop_i()),
        # Neither side of the 15-bit extended range may collapse to 0/Inf.
        (0xc0, 0x1c, nop_m(), fcmp(6, 7, 6, 7, rel=1), nop_b()),
        (0xd0, 0x1c, nop_m(), fcmp(8, 9, 8, 9, rel=1), nop_b()),
        (0xe0, 0x10, nop_m(), nop_i(), br_cond(0xe0, 0xe0)),
    ], {
        "ip": 0xe0,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7) |
                                      (1 << 8) | (1 << 9),
                                value=(1 << 6) | (1 << 8)),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fminmax_wre1_selects_raw_operand = require_registers(
    "fminmax_wre1_selects_raw_operand", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(7, 0x220, 0),
         addl(8, 0x228, 0)),
        (0x30, 0x01, addl(9, 0x230, 0), addl(10, 0x238, 0),
         addl(21, 0x18000, 0)),
        (0x40, 0x01, addl(22, 0x18001, 0), addl(23, 0x28000, 0),
         addl(24, 0x28001, 0)),
        (0x50, *movl_mlx(20, 0x8000000000000000)),
        (0x60, 0x09, st8(3, 20), st8(4, 21), nop_i()),
        (0x70, 0x09, st8(5, 20), st8(6, 22), nop_i()),
        (0x80, 0x09, st8(7, 20), st8(8, 23), nop_i()),
        (0x90, 0x09, st8(9, 20), st8(10, 24), nop_i()),
        (0xa0, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0xb0, 0x09, ldf_fill_postinc(8, 7, 0),
         ldf_fill_postinc(9, 9, 0), nop_i()),
        # Reverse f2/f3 for max so a false tie would select the wrong input.
        (0xc0, 0x0d, nop_m(), fmin(10, 6, 7), nop_i()),
        (0xd0, 0x0d, nop_m(), fmax(11, 7, 6), nop_i()),
        (0xe0, 0x0d, nop_m(), famin(12, 8, 9), nop_i()),
        (0xf0, 0x0d, nop_m(), famax(13, 9, 8), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
    ], {
        "ip": 0x100,
        "f10": ExpectedFP(0x8000000000000000, 0x18000),
        "f11": ExpectedFP(0x8000000000000000, 0x18001),
        "f12": ExpectedFP(0x8000000000000000, 0x28000),
        "f13": ExpectedFP(0x8000000000000000, 0x28001),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fminmax_unnormal_sets_d = require_registers(
    "fminmax_unnormal_sets_d", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x20, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fmin(8, 6, 0), nop_i()),
        (0x60, 0x0d, nop_m(), fmax(9, 6, 0), nop_i()),
        (0x70, 0x0d, nop_m(), famin(10, 6, 0), nop_i()),
        (0x80, 0x0d, nop_m(), famax(11, 6, 0), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0, 0),
        "f9": ExpectedFP(0x4000000000000000, 0x10000),
        "f10": ExpectedFP(0, 0),
        "f11": ExpectedFP(0x4000000000000000, 0x10000),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fmin_unnormal_d_fault_rolls_back = require_registers(
    "fmin_unnormal_d_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x80, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fmin(8, 6, 0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fmin_qnan_suppresses_unnormal_d = require_registers(
    "fmin_qnan_suppresses_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0x80, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fmin(8, 7, 6), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "f8": ExpectedFP(0x4000000000000000, 0x10000),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 1)) |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fp_logical_and_swap_decode = require_registers("fp_logical_and_swap_decode", [
    (0x10, *movl_mlx(2, 0x0123456789abcdef)),
    (0x20, *movl_mlx(3, 0xf0f0f0f00f0f0f0f)),
    (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
     nop_i()),
    (0x40, 0x0d, nop_m(), fand(8, 6, 7),
     nop_i()),
    (0x50, 0x0d, nop_m(), fandcm(9, 6, 7),
     nop_i()),
    (0x60, 0x0d, nop_m(), for_(10, 6, 7),
     nop_i()),
    (0x70, 0x0d, nop_m(), fxor(11, 6, 7),
     nop_i()),
    (0x80, 0x0d, nop_m(), fswap(12, 6, 7),
     nop_i()),
    (0x90, 0x0d, nop_m(), fswap_nl(13, 6, 7),
     nop_i()),
    (0xa0, 0x0d, nop_m(), fswap_nr(14, 6, 7),
     nop_i()),
    (0xb0, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xc0, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xd0, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xe0, 0x10, nop_m(), nop_i(),
     br_cond(0xe0, 0xe0)),
], {
    "ip": 0xe0,
    "f8": ExpectedFP(0x00204060090b0d0f, 0x1003e),
    "f9": ExpectedFP(0x0103050780a0c0e0, 0x1003e),
    "f10": ExpectedFP(0xf1f3f5f78fafcfef, 0x1003e),
    "f11": ExpectedFP(0xf1d3b59786a4c2e0, 0x1003e),
    "f12": ExpectedFP(0x0f0f0f0f01234567, 0x1003e),
    "f13": ExpectedFP(0x8f0f0f0f01234567, 0x1003e),
    "f14": ExpectedFP(0x0f0f0f0f81234567, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fp_bitops_read_architected_significand = require_registers(
    "fp_bitops_read_architected_significand", [
        (0x10, *movl_mlx(2, 0x3ff123456789abcd)),
        (0x20, *movl_mlx(4, 0x3ffabcdef0123456)),
        (0x30, *movl_mlx(5, 0xbfe0fedcba987654)),
        (0x40, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
        (0x50, 0x00, st8(3, 5), nop_i(), nop_i()),
        (0x60, 0x09, ldfd(7, 3), setf_d(6, 2), nop_i()),
        (0x70, 0x00, setf_d(5, 4), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fand(8, 6, 7), nop_i()),
        (0x90, 0x0d, nop_m(), fselect(9, 6, 7, 5), nop_i()),
        (0xa0, 0x0d, nop_m(), fswap(10, 6, 7), nop_i()),
        (0xb0, 0x0d, nop_m(), fmix_l(11, 6, 7), nop_i()),
        (0xc0, 0x0d, nop_m(), fsxt_r(12, 6, 7), nop_i()),
        (0xd0, 0x0d, nop_m(), fpmerge_se(13, 6, 7), nop_i()),
        (0xe0, 0x00, getf_sig(8, 8), nop_i(), nop_i()),
        (0xf0, 0x00, getf_sig(9, 9), nop_i(), nop_i()),
        (0x100, 0x00, getf_sig(10, 10), nop_i(), nop_i()),
        (0x110, 0x00, getf_sig(11, 11), nop_i(), nop_i()),
        (0x120, 0x00, getf_sig(12, 12), nop_i(), nop_i()),
        (0x130, 0x00, getf_sig(13, 13), nop_i(), nop_i()),
        (0x140, 0x10, nop_m(), nop_i(), br_cond(0x140, 0x140)),
    ], {
        "ip": 0x140,
        "r8": 0x8112211441122000,
        "r9": 0xd5f6f594d1b2b000,
        "r10": 0xc3b2a000891a2b3c,
        "r11": 0x891a2b3c87f6e5d4,
        "r12": 0x00000000c3b2a000,
        "r13": 0x8976e5d44d32a000,
        "f8": ExpectedFP(0x8112211441122000, 0x1003e),
        "f9": ExpectedFP(0xd5f6f594d1b2b000, 0x1003e),
        "f10": ExpectedFP(0xc3b2a000891a2b3c, 0x1003e),
        "f11": ExpectedFP(0x891a2b3c87f6e5d4, 0x1003e),
        "f12": ExpectedFP(0x00000000c3b2a000, 0x1003e),
        "f13": ExpectedFP(0x8976e5d44d32a000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_logical_swap_natval_propagates = require_registers(
    "fp_logical_swap_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fswap_nr(10, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fp_mix_sign_extend_decode = require_registers("fp_mix_sign_extend_decode", [
    (0x10, *movl_mlx(2, 0x8123456789abcdef)),
    (0x20, *movl_mlx(3, 0x70f0f0f00f0f0f0f)),
    (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
     nop_i()),
    (0x40, 0x0d, nop_m(), fmix_lr(8, 6, 7),
     nop_i()),
    (0x50, 0x0d, nop_m(), fmix_r(9, 6, 7),
     nop_i()),
    (0x60, 0x0d, nop_m(), fmix_l(10, 6, 7, ignored=7),
     nop_i()),
    (0x70, 0x0d, nop_m(), fsxt_r(11, 6, 7),
     nop_i()),
    (0x80, 0x0d, nop_m(), fsxt_l(12, 6, 7),
     nop_i()),
    (0x90, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xa0, 0x09, nop_m(), nop_m(),
     nop_i()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
], {
    "ip": 0xb0,
    "f8": ExpectedFP(0x812345670f0f0f0f, 0x1003e),
    "f9": ExpectedFP(0x89abcdef0f0f0f0f, 0x1003e),
    "f10": ExpectedFP(0x8123456770f0f0f0, 0x1003e),
    "f11": ExpectedFP(0xffffffff0f0f0f0f, 0x1003e),
    "f12": ExpectedFP(0xffffffff70f0f0f0, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fp_mix_sign_extend_natval_propagates = require_registers(
    "fp_mix_sign_extend_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fmix_l(10, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fpsr_status_field_controls = require_registers(
    "fpsr_status_field_controls", [
        (0x10, *movl_mlx(2, (0x2a << FPSR_SF0_SHIFT) |
                         (0x3f << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fsetc(1, 0x0f, 0x10),
         nop_i()),
        (0x40, 0x0d, nop_m(), fclrf(1),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "ar_fpsr": ((0x2a << FPSR_SF0_SHIFT) |
                    (0x1a << FPSR_SF1_SHIFT)),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpsr_td_suppresses_fp_fault = require_registers(
    "fpsr_td_suppresses_fp_fault", [
        (0x10, *movl_mlx(2, 0x33b)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fsetc(1, 0x7f, FPSR_SF_TD),
         nop_i()),
        (0x40, *movl_mlx(3, 0x3ff0000000000000)),
        (0x50, *movl_mlx(4, 0)),
        (0x60, 0x09, setf_d(6, 3), setf_d(7, 4),
         nop_i()),
        (0x70, 0x0d, nop_m(), frcpa(8, 6, 6, 7, sf=1),
         nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f8": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
        "ar_fpsr": 0x1260033b,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fsetc_sf0_td_reserved_field_fault = require_exception(
    "fsetc_sf0_td_reserved_field_fault", [
        (0x10, 0x0d, nop_m(), fsetc(0, 0x7f, FPSR_SF_TD),
         nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x10)

test_fsetc_pc1_reserved_field_fault = require_exception(
    "fsetc_pc1_reserved_field_fault", [
        (0x10, 0x0d, nop_m(), fsetc(1, 0, FPSR_SF_RESERVED_PC1),
         nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x10)

test_fsetc_fclrf_ignored_bit36_decode = require_registers(
    "fsetc_fclrf_ignored_bit36_decode", [
        (0x10, *movl_mlx(2, 0x3f)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fsetc(1, 0x0f, 0x10) | bitfield(1, 36, 1),
         nop_i()),
        (0x40, 0x0d, nop_m(), fclrf(1) | bitfield(1, 36, 1),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "ar_fpsr": 0x3f | (0x10 << FPSR_SF1_SHIFT),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fchkf_no_branch_when_flags_committed = require_registers(
    "fchkf_no_branch_when_flags_committed", [
        (0x10, *movl_mlx(2, 0x3f |
                         (0x01 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                         (0x01 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fchkf(1, 0x30, 0x80),
         nop_i()),
        (0x40, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x50, "r4": 1, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_fchkf_branches_on_uncommitted_flag = require_registers(
    "fchkf_branches_on_uncommitted_flag", [
        (0x10, *movl_mlx(2, 0x3f |
                         (0x01 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fchkf(1, 0x30, 0x80),
         nop_i()),
        (0x40, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r4": 0, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_fchkf_positive_target_ignores_bit26 = require_registers(
    "fchkf_positive_target_ignores_bit26", [
        (0x10, *movl_mlx(2, 0x3f |
                         (0x01 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fchkf(1, 0x30, 0x80, ignored26=1),
         nop_i()),
        (0x40, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r4": 0, "exception": IA64_EXCP_NONE},
    entry=0x10)

test_fchkf_negative_target_uses_bit36 = require_registers(
    "fchkf_negative_target_uses_bit36", [
        (0x10, *movl_mlx(2, 0x3f |
                         (0x01 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x50)),
        (0x40, 0x10, adds(4, 1, 0), nop_i(),
         br_cond(0x40, 0x80)),
        (0x50, 0x0d, nop_m(), fchkf(1, 0x50, 0x40),
         nop_i()),
        (0x60, 0x00, adds(5, 1, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r4": 1,
        "r5": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_p2_high_bit_not_fchkfs = require_registers(
    "fcmp_p2_high_bit_not_fchkfs", [
        (0x10, 0x1c, nop_m(), fcmp(6, 32, 1, 1, rel=2),
         nop_b()),
        (0x20, 0x00, nop_m(), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 32),
                                value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpmerge_parallel_forms_decode = require_registers(
    "fpmerge_parallel_forms_decode", [
        (0x10, *movl_mlx(2, 0x8123456789abcdef)),
        (0x20, *movl_mlx(3, 0x70f0f0f00f0f0f0f)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpmerge_s(8, 6, 7),
         nop_i()),
        (0x50, 0x0d, nop_m(), fpmerge_ns(9, 6, 7),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpmerge_se(10, 6, 7),
         nop_i()),
        (0x70, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f8": ExpectedFP(0xf0f0f0f08f0f0f0f, 0x1003e),
        "f9": ExpectedFP(0x70f0f0f00f0f0f0f, 0x1003e),
        "f10": ExpectedFP(0x8170f0f0898f0f0f, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpminmax_parallel_decode = require_registers(
    "fpminmax_parallel_decode", [
        (0x10, *movl_mlx(2, 0x3f800000c0800000)),
        (0x20, *movl_mlx(3, 0x40000000c0400000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpmin(8, 6, 7),
         nop_i()),
        (0x50, 0x0d, nop_m(), fpmax(9, 6, 7, sf=1),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpamin(10, 6, 7, sf=2),
         nop_i()),
        (0x70, 0x0d, nop_m(), fpamax(11, 6, 7, sf=3),
         nop_i()),
        (0x80, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0x90, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f8": ExpectedFP(0x3f800000c0800000, 0x1003e),
        "f9": ExpectedFP(0x40000000c0400000, 0x1003e),
        "f10": ExpectedFP(0x3f800000c0400000, 0x1003e),
        "f11": ExpectedFP(0x40000000c0800000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpminmax_simd_high_lane_fault_isr = require_registers(
    "fpminmax_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33d)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x000000013f800000)),
        (0x40, *movl_mlx(4, 0x3f80000040000000)),
        (0x50, *movl_mlx(5, 0x4000000040400000)),
        (0x60, 0x09, setf_sig(6, 3), setf_sig(7, 4),
         nop_i()),
        (0x70, 0x00, setf_sig(8, 5), nop_i(),
         nop_i()),
        (0x80, 0x0d, nop_m(), fpmax(8, 6, 7),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x02,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33d,
    }, entry=0x10)

test_fpminmax_nan_invalid_fault = require_registers(
    "fpminmax_nan_invalid_fault", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x7fc000003f800000)),
        (0x40, *movl_mlx(4, 0x3f80000040000000)),
        (0x50, *movl_mlx(5, 0x4000000040400000)),
        (0x60, 0x09, setf_sig(6, 3), setf_sig(7, 4),
         nop_i()),
        (0x70, 0x00, setf_sig(8, 5), nop_i(),
         nop_i()),
        (0x80, 0x0d, nop_m(), fpmax(8, 6, 7),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x01,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fpcmp_parallel_decode = require_registers(
    "fpcmp_parallel_decode", [
        (0x10, *movl_mlx(2, 0x3f800000c0800000)),
        (0x20, *movl_mlx(3, 0x40000000c0400000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpcmp(0, 8, 6, 7),
         nop_i()),
        (0x50, 0x0d, nop_m(), fpcmp(1, 9, 6, 7, sf=1),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpcmp(4, 10, 6, 7, sf=2),
         nop_i()),
        (0x70, 0x0d, nop_m(), fpcmp(7, 11, 6, 7, sf=3),
         nop_i()),
        (0x80, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0x90, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f8": ExpectedFP(0, 0x1003e),
        "f9": ExpectedFP(0xffffffffffffffff, 0x1003e),
        "f10": ExpectedFP(0xffffffffffffffff, 0x1003e),
        "f11": ExpectedFP(0xffffffffffffffff, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcmp_qnan_quiet_relations = require_registers(
    "fpcmp_qnan_quiet_relations", [
        (0x10, *movl_mlx(2, 0x7fc123457fc54321)),
        (0x20, *movl_mlx(3, 0x3f8000003f800000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x40, 0x0d, nop_m(), fpcmp(0, 8, 6, 7, sf=0), nop_i()),
        (0x50, 0x0d, nop_m(), fpcmp(3, 9, 6, 7, sf=1), nop_i()),
        (0x60, 0x0d, nop_m(), fpcmp(4, 10, 6, 7, sf=2), nop_i()),
        (0x70, 0x0d, nop_m(), fpcmp(7, 11, 6, 7, sf=3), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f8": ExpectedFP(0, 0x1003e),
        "f9": ExpectedFP(UINT64_MAX, 0x1003e),
        "f10": ExpectedFP(UINT64_MAX, 0x1003e),
        "f11": ExpectedFP(0, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcmp_nan_invalid_flags = require_registers(
    "fpcmp_nan_invalid_flags", [
        (0x10, *movl_mlx(2, 0x7fc123457fc54321)),
        # The high lane is a distinct SNaN; the low lane equals f8's low lane.
        (0x20, *movl_mlx(3, 0x7f8123453f800000)),
        (0x30, *movl_mlx(4, 0x3f8000003f800000)),
        (0x40, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x50, 0x00, setf_sig(8, 4), nop_i(), nop_i()),
        # lt is signaling for a QNaN; eq is quiet but still signals on SNaN.
        (0x60, 0x0d, nop_m(), fpcmp(1, 9, 6, 8, sf=0), nop_i()),
        (0x70, 0x0d, nop_m(), fpcmp(0, 10, 7, 8, sf=1), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f9": ExpectedFP(0, 0x1003e),
        "f10": ExpectedFP(0x00000000ffffffff, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (1 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcmp_qnan_quiet_with_invalid_enabled = require_registers(
    "fpcmp_qnan_quiet_with_invalid_enabled", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~1)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x7fc123457fc54321)),
        (0x40, *movl_mlx(4, 0x3f8000003f800000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x00, setf_sig(9, 4), nop_i(), nop_i()),
        # Quiet eq commits before signaling lt takes V in both SIMD lanes.
        (0x70, 0x0d, nop_m(), fpcmp(0, 8, 6, 7), nop_i()),
        (0x80, 0x0d, nop_m(), fpcmp(1, 9, 6, 7), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x11,
        "f8": ExpectedFP(0, 0x1003e),
        "f9": ExpectedFP(0x3f8000003f800000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR & ~1,
    }, entry=0x10)

test_fpcmp_simd_high_lane_fault_isr = require_registers(
    "fpcmp_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x7fa000003f800000)),
        (0x40, *movl_mlx(4, 0x3f8000003f800000)),
        (0x50, *movl_mlx(5, 0x4000000040400000)),
        (0x60, 0x09, setf_sig(6, 3), setf_sig(7, 4),
         nop_i()),
        (0x70, 0x00, setf_sig(8, 5), nop_i(),
         nop_i()),
        (0x80, 0x0d, nop_m(), fpcmp(0, 8, 6, 7),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x01,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fp_parallel_natval_propagates = require_registers(
    "fp_parallel_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpcmp(1, 10, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fpcvt_parallel_decode = require_registers(
    "fpcvt_parallel_decode", [
        (0x10, *movl_mlx(2, 0x3fc00000c0300000)),
        (0x20, *movl_mlx(3, 0x3fc0000040300000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpcvt_fx(8, 6),
         nop_i()),
        (0x50, 0x0d, nop_m(), fpcvt_fx_trunc(9, 6, sf=2),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpcvt_fxu(10, 7, sf=1),
         nop_i()),
        (0x70, 0x0d, nop_m(), fpcvt_fxu_trunc(11, 7, sf=3),
         nop_i()),
        (0x80, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0x90, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f8": ExpectedFP(0x00000002fffffffd, 0x1003e),
        "f9": ExpectedFP(0x00000001fffffffe, 0x1003e),
        "f10": ExpectedFP(0x0000000200000003, 0x1003e),
        "f11": ExpectedFP(0x0000000100000002, 0x1003e),
        "ar_fpsr": 0x0209904c8274033f,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcvt_masked_invalid_lane_indefinite = require_registers(
    "fpcvt_masked_invalid_lane_indefinite", [
        # High +Inf is invalid for both forms; low -1 is valid signed and
        # invalid unsigned.  Invalid lanes always use 0x80000000.
        (0x10, *movl_mlx(2, 0x7f800000bf800000)),
        (0x20, 0x00, setf_sig(6, 2), nop_i(), nop_i()),
        (0x30, 0x0d, nop_m(), fpcvt_fx(8, 6), nop_i()),
        (0x40, 0x0d, nop_m(), fpcvt_fxu(9, 6), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "f8": ExpectedFP(0x80000000ffffffff, 0x1003e),
        "f9": ExpectedFP(0x8000000080000000, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcvt_parallel_natval_propagates = require_registers(
    "fpcvt_parallel_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpcvt_fx(10, 7),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fpcvt_simd_high_lane_fault_isr = require_registers(
    "fpcvt_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x7fc000003f800000)),
        (0x40, *movl_mlx(4, 0x4000000040400000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(8, 4),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpcvt_fx(8, 6),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x01,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fpcvt_simd_low_lane_fault_isr = require_registers(
    "fpcvt_simd_low_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3f8000007fc00000)),
        (0x40, *movl_mlx(4, 0x4000000040400000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(8, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpcvt_fx(8, 6), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x10,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fpcvt_denormal_lanes_set_d = require_registers(
    "fpcvt_denormal_lanes_set_d", [
        (0x10, *movl_mlx(2, 0x0000000180000001)),
        (0x20, 0x00, setf_sig(6, 2), nop_i(), nop_i()),
        (0x30, 0x0d, nop_m(), fpcvt_fx(8, 6), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "f8": ExpectedFP(0, 0x1003e),
        # Both tiny lanes round to zero, setting D and I.
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x22 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcvt_high_lane_denormal_fault_rolls_back = require_registers(
    "fpcvt_high_lane_denormal_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x05, *movl_mlx(3, 0x000000013f800000)[1:]),
        (0x40, 0x05, *movl_mlx(4, 0x4000000040400000)[1:]),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(8, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpcvt_fx(8, 6), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x02,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fpcvt_packed_faults_keep_lane_classes = require_registers(
    "fpcvt_packed_faults_keep_lane_classes", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~((1 << 0) | (1 << 1)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # High QNaN raises V while the low denormal independently raises D.
        (0x30, *movl_mlx(3, 0x7fc0000000000001)),
        (0x40, *movl_mlx(4, 0x4000000040400000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(8, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        # Table 8-3 independently reports HI V and LO D in ISR.code.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x21,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR & ~((1 << 0) | (1 << 1)),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcvt_packed_inexact_trap_maps_high_fpa = require_registers(
    "fpcvt_packed_inexact_trap_maps_high_fpa", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # High +1.75 rounds away from zero; low +1.25 rounds toward zero.
        (0x30, *movl_mlx(3, 0x3fe000003fa00000)),
        (0x40, 0x00, setf_sig(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fpcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        # HI I/FPA are ISR.code[13:14], LO I is bit 9.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x6201,
        "f8": ExpectedFP(0x0000000200000001, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (0x20 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpcvt_packed_inexact_trap_maps_low_fpa = require_registers(
    "fpcvt_packed_inexact_trap_maps_low_fpa", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # Reverse the lanes to distinguish ISR.fpa[LO] from ISR.fpa[HI].
        (0x30, *movl_mlx(3, 0x3fa000003fe00000)),
        (0x40, 0x00, setf_sig(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fpcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        # HI I is bit 13; LO I/FPA are bits 9:10.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x2601,
        "f8": ExpectedFP(0x0000000100000002, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (0x20 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpma_parallel_decode = require_registers(
    "fpma_parallel_decode", [
        (0x10, *movl_mlx(2, 0x3f80000040000000)),
        (0x20, *movl_mlx(3, 0x4000000040400000)),
        (0x30, *movl_mlx(4, 0x4080000040a00000)),
        (0x40, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x50, 0x00, setf_sig(8, 4), nop_i(),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpma(9, 6, 7, 8),
         nop_i()),
        (0x70, 0x0d, nop_m(), fpms(10, 6, 7, 8, sf=1),
         nop_i()),
        (0x80, 0x0d, nop_m(), fpnma(11, 6, 7, 8, sf=2),
         nop_i()),
        (0x90, 0x09, nop_m(), nop_m(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f9": ExpectedFP(0x4110000041880000, 0x1003e),
        "f10": ExpectedFP(0x40e0000041500000, 0x1003e),
        "f11": ExpectedFP(0xc0e00000c1500000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpms_fpnma_qnan_preserves_sign = require_registers(
    "fpms_fpnma_qnan_preserves_sign", [
        (0x10, *movl_mlx(2, 0x7fc001a17fc001a2)),
        (0x20, *movl_mlx(3, 0x3f8000003f800000)),
        (0x30, *movl_mlx(4, 0x7fc002b17fc002b2)),
        (0x40, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x50, 0x09, setf_sig(8, 4), setf_sig(9, 3), nop_i()),
        # Negating the addend/product must not negate a propagated NaN.
        (0x60, 0x0d, nop_m(), fpms(10, 6, 7, 7), nop_i()),
        (0x70, 0x0d, nop_m(), fpnma(11, 9, 8, 7, sf=1), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0x7fc001a17fc001a2, 0x1003e),
        "f11": ExpectedFP(0x7fc002b17fc002b2, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpms_fpnma_snan_quiets_without_sign_flip = require_registers(
    "fpms_fpnma_snan_quiets_without_sign_flip", [
        (0x10, *movl_mlx(2, 0x7f8001a17f8001a2)),
        (0x20, *movl_mlx(3, 0x3f8000003f800000)),
        (0x30, *movl_mlx(4, 0x7f8002b17f8002b2)),
        (0x40, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x50, 0x09, setf_sig(8, 4), setf_sig(9, 3), nop_i()),
        (0x60, 0x0d, nop_m(), fpms(10, 6, 7, 7), nop_i()),
        (0x70, 0x0d, nop_m(), fpnma(11, 9, 8, 7, sf=1), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0x7fc001a17fc001a2, 0x1003e),
        "f11": ExpectedFP(0x7fc002b17fc002b2, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (1 << (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

_FP_SIGNED_ZERO_SF3_RC_SHIFT = FPSR_SF2_SHIFT + 13 + 4
_FP_SIGNED_ZERO_SCALAR_FPSR = (
    (DEFAULT_FPSR & ~(3 << _FP_SIGNED_ZERO_SF3_RC_SHIFT)) |
    (1 << _FP_SIGNED_ZERO_SF3_RC_SHIFT)
)

test_scalar_muladd_f0_signed_zero_is_pure_multiply = require_registers(
    "scalar_muladd_f0_signed_zero_is_pure_multiply", [
        (0x10, *movl_mlx(2, _FP_SIGNED_ZERO_SCALAR_FPSR)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x0000000000000000)),
        (0x40, *movl_mlx(4, 0x3ff0000000000000)),
        (0x50, 0x09, setf_d(6, 3), setf_d(7, 4), nop_i()),
        # FMS uses round-down in sf3: +0 * +1 remains +0, not +0 + -0.
        (0x60, 0x0d, nop_m(), fms_s3(8, 6, 7, 0), nop_i()),
        # FNMA uses round-nearest in sf1: -(+0 * +1) is -0.
        (0x70, 0x0d, nop_m(), fnma_s1(9, 6, 7, 0), nop_i()),
        (0x80, 0x00, getf_d(10, 8), nop_i(), nop_i()),
        (0x90, 0x00, getf_d(11, 9), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "r10": 0x0000000000000000,
        "r11": 0x8000000000000000,
        "f8": ExpectedFP(*binary64_to_spill(0x0000000000000000)),
        "f9": ExpectedFP(*binary64_to_spill(0x8000000000000000)),
        "ar_fpsr": _FP_SIGNED_ZERO_SCALAR_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

_FP_SIGNED_ZERO_SF0_RC_SHIFT = FPSR_SF0_SHIFT + 4
_FP_SIGNED_ZERO_PARALLEL_FPSR = (
    (DEFAULT_FPSR & ~(3 << _FP_SIGNED_ZERO_SF0_RC_SHIFT)) |
    (1 << _FP_SIGNED_ZERO_SF0_RC_SHIFT)
)

test_parallel_muladd_f0_signed_zero_is_pure_multiply = require_registers(
    "parallel_muladd_f0_signed_zero_is_pure_multiply", [
        (0x10, *movl_mlx(2, _FP_SIGNED_ZERO_PARALLEL_FPSR)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # Packed (-0, +0) multiplied by (+1, +1).
        (0x30, *movl_mlx(3, 0x8000000000000000)),
        (0x40, *movl_mlx(4, 0x3f8000003f800000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpma(8, 0, 6, 7, sf=1), nop_i()),
        # Round-down must not turn the low +0 product into -0 via -f0.
        (0x70, 0x0d, nop_m(), fpms(9, 0, 6, 7, sf=0), nop_i()),
        (0x80, 0x0d, nop_m(), fpnma(10, 0, 6, 7, sf=2), nop_i()),
        (0x90, 0x00, getf_sig(8, 8), nop_i(), nop_i()),
        (0xa0, 0x00, getf_sig(9, 9), nop_i(), nop_i()),
        (0xb0, 0x00, getf_sig(10, 10), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "r8": 0x8000000000000000,
        "r9": 0x8000000000000000,
        "r10": 0x0000000080000000,
        "f8": ExpectedFP(0x8000000000000000, 0x1003e),
        "f9": ExpectedFP(0x8000000000000000, 0x1003e),
        "f10": ExpectedFP(0x0000000080000000, 0x1003e),
        "ar_fpsr": _FP_SIGNED_ZERO_PARALLEL_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_parallel_reads_architected_significand = require_registers(
    "fp_parallel_reads_architected_significand", [
        # These binary64 encodings expand to packed significands
        # (-1.0, 2.0), (-2.0, 3.0), and (-1.0, 4.0), respectively.
        (0x10, *movl_mlx(2, 0x3ff7f00000080000)),
        (0x20, *movl_mlx(3, 0x3ff8000000080800)),
        (0x30, *movl_mlx(4, 0x3ff7f00000081000)),
        (0x40, 0x09, setf_d(20, 2), setf_d(21, 3), nop_i()),
        (0x50, 0x00, setf_d(22, 4), nop_i(), nop_i()),
        (0x60, 0x0d, nop_m(), fpmin(8, 20, 21), nop_i()),
        (0x70, 0x0d, nop_m(), fpcmp(1, 9, 20, 21), nop_i()),
        (0x80, 0x0d, nop_m(), fpcvt_fx_trunc(10, 20), nop_i()),
        (0x90, 0x0d, nop_m(), fpma(11, 20, 21, 22), nop_i()),
        (0xa0, 0x0d, nop_m(), fprcpa(12, 6, 20, 22, sf=0), nop_i()),
        (0xb0, 0x0d, nop_m(), fprsqrta(13, 7, 22, sf=0), nop_i()),
        (0xc0, 0x00, getf_sig(8, 8), nop_i(), nop_i()),
        (0xd0, 0x00, getf_sig(9, 9), nop_i(), nop_i()),
        (0xe0, 0x00, getf_sig(10, 10), nop_i(), nop_i()),
        (0xf0, 0x00, getf_sig(11, 11), nop_i(), nop_i()),
        (0x100, 0x00, getf_sig(12, 12), nop_i(), nop_i()),
        (0x110, 0x00, getf_sig(13, 13), nop_i(), nop_i()),
        (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "r8": 0xc000000040000000,
        "r9": 0x00000000ffffffff,
        "r10": 0xffffffff00000002,
        "r11": 0x3f80000041600000,
        "r12": 0xbf7f80003e7f8000,
        "r13": 0xffc000003eff8000,
        "f8": ExpectedFP(0xc000000040000000, 0x1003e),
        "f9": ExpectedFP(0x00000000ffffffff, 0x1003e),
        "f10": ExpectedFP(0xffffffff00000002, 0x1003e),
        "f11": ExpectedFP(0x3f80000041600000, 0x1003e),
        "f12": ExpectedFP(0xbf7f80003e7f8000, 0x1003e),
        "f13": ExpectedFP(0xffc000003eff8000, 0x1003e),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR | (1 << (FPSR_SF0_SHIFT +
                                         FPSR_SF_FLAGS_SHIFT)),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpma_parallel_natval_propagates = require_registers(
    "fpma_parallel_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpma(10, 8, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fpma_simd_high_lane_fault_isr = require_registers(
    "fpma_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x000000003f800000)),
        (0x40, *movl_mlx(4, 0x7f8000003f800000)),
        (0x50, *movl_mlx(5, 0x000000003f800000)),
        (0x60, *movl_mlx(6, 0x4000000040400000)),
        (0x70, 0x09, setf_sig(7, 3), setf_sig(8, 4),
         nop_i()),
        (0x80, 0x09, setf_sig(9, 5), setf_sig(10, 6),
         nop_i()),
        (0x90, 0x0d, nop_m(), fpma(10, 7, 8, 9),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(11, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r11": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x01,
        "f10": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fpma_packed_enabled_ou_wraps_and_maps_lanes = require_registers(
    "fpma_packed_enabled_ou_wraps_and_maps_lanes", [
        (0x10, *movl_mlx(
            2, DEFAULT_FPSR & ~((1 << 3) | (1 << 4)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # High max-finite * 2 overflows; low min-normal * 0.5 underflows.
        (0x30, *movl_mlx(3, 0x7f7fffff00800000)),
        (0x40, *movl_mlx(4, 0x400000003f000000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpma(8, 0, 6, 7, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        # HI O is bit 11; concurrent LO U is bit 8.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x0901,
        # Enabled packed O/U returns the exponent-rebiased wrapped values.
        "f8": ExpectedFP(0x1fffffff60000000, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 4))) |
                    (0x18 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpma_packed_overflow_reports_concurrent_masked_inexact = require_registers(
    "fpma_packed_overflow_reports_concurrent_masked_inexact", [
        # Enable O but leave I masked.  Figure 5-12 still requires ISR.i.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 3))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # The high product overflows and its significand rounds upward.
        (0x30, *movl_mlx(3, 0x7f4000003f800000)),
        (0x40, *movl_mlx(4, 0x400000013f800000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpma(8, 0, 6, 7, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        # HI O/I/FPA are bits 11/13/14 even though global I is masked.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x6801,
        "f8": ExpectedFP(0x1fc000023f800000, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 3)) |
                    (0x28 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpma_packed_inexact_trap_maps_lane_fpa = require_registers(
    "fpma_packed_inexact_trap_maps_lane_fpa", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # HI is an RN tie with an odd retained LSB; LO rounds downward.
        (0x30, *movl_mlx(3, 0x3fc000003fa00000)),
        (0x40, *movl_mlx(4, 0x3f8000013f800001)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpma(8, 0, 6, 7, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x6201,
        "f8": ExpectedFP(0x3fc000023fa00001, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (0x20 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fpma_packed_ftz_sets_ui_and_traps_inexact = require_registers(
    "fpma_packed_ftz_sets_ui_and_traps_inexact", [
        # Enable FTZ and I while leaving U masked.
        (0x10, *movl_mlx(
            2, (DEFAULT_FPSR | (1 << FPSR_SF0_SHIFT)) & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        # High min-normal * 0.5 is exact but tiny; low 1.0 * 1.0 is exact.
        (0x30, *movl_mlx(3, 0x008000003f800000)),
        (0x40, *movl_mlx(4, 0x3f0000003f800000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fpma(8, 0, 6, 7, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        # FTZ sets U|I; only enabled HI I is reported and FPA stays clear.
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x2001,
        "f8": ExpectedFP(0x000000003f800000, 0x1003e),
        "ar_fpsr": (((DEFAULT_FPSR | (1 << FPSR_SF0_SHIFT)) &
                     ~(1 << 5)) |
                    (0x30 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_unary_natval_propagates = require_registers(
    "fp_unary_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x0d, nop_m(), fnorm(8, 0, 7),
         nop_i()),
        (0x50, 0x0d, nop_m(), fpabs(9, 7),
         nop_i()),
        (0x60, 0x0d, nop_m(), fpneg(10, 7),
         nop_i()),
        (0x70, 0x0d, nop_m(), fpnegabs(11, 7),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x110)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
        (0xa0, 0x00, chk_s_f(9, 0xa0, 0xc0), adds(5, 1, 0),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
        (0xc0, 0x00, chk_s_f(10, 0xc0, 0xe0), adds(12, 1, 0),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
        (0xe0, 0x00, chk_s_f(11, 0xe0, 0x110), adds(13, 1, 0),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
        (0x100, 0x10, nop_m(), nop_i(),
         br_cond(0x100, 0x100)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x110,
        "f8": ExpectedFP(0, 0x1fffe, nat=True),
        "f9": ExpectedFP(0, 0x1fffe, nat=True),
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "f11": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_arithmetic_natval_propagates = require_registers(
    "fp_arithmetic_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_d(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fmpy_s1(9, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x50,
        "f9": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_getf_natval_sets_gr_nat = require_registers(
    "getf_natval_sets_gr_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x10, getf_sig(8, 7), nop_i(),
         nop_b()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x50,
        "r8_nat": 1,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

# Alternating sticky flags expose both clearing and spurious flag additions.
_FPACK_FLAGS = 0x15 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)
_FPACK_FPSR_BASE = DEFAULT_FPSR | _FPACK_FLAGS
_FPACK_RC_SHIFT = FPSR_SF0_SHIFT + 4
_FPACK_FPSRS = tuple(
    (_FPACK_FPSR_BASE & ~(3 << _FPACK_RC_SHIFT)) |
    (rc << _FPACK_RC_SHIFT)
    for rc in range(4)
)

test_fpack_decode = require_registers("fpack_decode", [
    # These doubles lie three quarters of a binary32 ulp away from +/-1.
    # getf.s and fpack must translate register bits, rather than round them.
    (0x10, *movl_mlx(2, 0x3ff0000018000000)),
    (0x20, *movl_mlx(3, 0xbff0000018000000)),
    (0x30, 0x09, setf_d(6, 2), setf_d(7, 3),
     nop_i()),
    (0x40, 0x09, getf_s(4, 6), getf_s(5, 7),
     nop_i()),
    (0x50, *movl_mlx(20, _FPACK_FPSRS[0])),
    (0x60, *movl_mlx(21, _FPACK_FPSRS[1])),
    (0x70, *movl_mlx(22, _FPACK_FPSRS[2])),
    (0x80, *movl_mlx(23, _FPACK_FPSRS[3])),
    (0x90, 0x01, mov_m_gr_ar(20, 40), nop_i(),
     nop_i()),
    (0xa0, 0x1d, nop_m(), fmpy_s0(20, 1, 1),
     nop_b()),
    (0xb0, 0x0d, nop_m(), fpack(8, 6, 7),
     nop_i()),
    (0xc0, 0x01, mov_m_gr_ar(21, 40), nop_i(),
     nop_i()),
    (0xd0, 0x1d, nop_m(), fmpy_s0(20, 1, 1),
     nop_b()),
    (0xe0, 0x0d, nop_m(), fpack(9, 6, 7),
     nop_i()),
    (0xf0, 0x01, mov_m_gr_ar(22, 40), nop_i(),
     nop_i()),
    (0x100, 0x1d, nop_m(), fmpy_s0(20, 1, 1),
     nop_b()),
    (0x110, 0x0d, nop_m(), fpack(10, 6, 7),
     nop_i()),
    (0x120, 0x01, mov_m_gr_ar(23, 40), nop_i(),
     nop_i()),
    (0x130, 0x1d, nop_m(), fmpy_s0(20, 1, 1),
     nop_b()),
    (0x140, 0x0d, nop_m(), fpack(11, 6, 7),
     nop_i()),
    (0x150, 0x10, nop_m(), nop_i(),
     br_cond(0x150, 0x150)),
], {
    "ip": 0x150,
    "r4": 0x3f800000,
    "r5": 0xbf800000,
    "f8": ExpectedFP(0x3f800000bf800000, 0x1003e),
    "f9": ExpectedFP(0x3f800000bf800000, 0x1003e),
    "f10": ExpectedFP(0x3f800000bf800000, 0x1003e),
    "f11": ExpectedFP(0x3f800000bf800000, 0x1003e),
    "ar_fpsr": _FPACK_FPSRS[3],
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_frsqrta_decode = require_registers("frsqrta_decode", [
    (0x10, *movl_mlx(2, 0x4010000000000000)),
    (0x20, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x0d, nop_m(), frsqrta(8, 6, 6),
     nop_i()),
    (0x40, 0x00, nop_m(), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "f8": ExpectedFP(0xff80000000000000, 0x0fffd),
    "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_frsqrta_wre1_uses_raw_exponents = require_registers(
    "frsqrta_wre1_uses_raw_exponents", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(21, 0x18000, 0),
         addl(22, 0x8000, 0)),
        (0x30, *movl_mlx(23, 0x8000000000000000)),
        (0x40, 0x09, st8(3, 23), st8(4, 21), nop_i()),
        (0x50, 0x09, st8(5, 23), st8(6, 22), nop_i()),
        (0x60, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x70, 0x0d, nop_m(), frsqrta(8, 6, 6), nop_i()),
        (0x80, 0x0d, nop_m(), frsqrta(9, 7, 7), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0xb4a0000000000000, 0x0bffe),
        "f9": ExpectedFP(0xb4a0000000000000, 0x13ffe),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7),
                                value=(1 << 6) | (1 << 7)),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frsqrta_wre1_negative_normal_uses_raw_type = require_registers(
    "frsqrta_wre1_negative_normal_uses_raw_type", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x28000, 0)),
        (0x20, *movl_mlx(22, 0x8000000000000000)),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), frsqrta(8, 6, 6, sf=0), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0xc000000000000000, 0x3ffff),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x01 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frsqrta_pred_false_clears = require_registers(
    "frsqrta_pred_false_clears", [
        (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(6, 7, 0, 16),
         nop_i()),
        (0x20, *movl_mlx(2, 0x4010000000000000)),
        (0x30, 0x00, setf_d(6, 2), nop_i(),
         nop_i()),
        (0x40, 0x0d, nop_m(), frsqrta(8, 6, 6, qp=7),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0, 0),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=0),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frsqrta_special_returns_operand = require_registers(
    "frsqrta_special_returns_operand", [
        (0x10, *movl_mlx(2, 0x0000000000000000)),
        (0x20, 0x00, setf_d(6, 2), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), frsqrta(8, 6, 6),
         nop_i()),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, *movl_mlx(2, 0x8000000000000000)),
        (0x60, 0x00, setf_d(6, 2), nop_i(),
         nop_i()),
        (0x70, 0x0d, nop_m(), frsqrta(9, 6, 6),
         nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, *movl_mlx(2, 0x7ff0000000000000)),
        (0xa0, 0x00, setf_d(6, 2), nop_i(),
         nop_i()),
        (0xb0, 0x0d, nop_m(), frsqrta(12, 6, 6),
         nop_i()),
        (0xc0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "f8": ExpectedFP(*binary64_to_spill(0x0000000000000000)),
        "f9": ExpectedFP(*binary64_to_spill(0x8000000000000000)),
        "f12": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fp_approx_unsupported_masked_invalid = require_registers(
    "fp_approx_unsupported_masked_invalid", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x1ffff, 0)),
        # Positive pseudo-infinity: special exponent with integer bit clear.
        (0x20, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x30, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        # Unsupported operands produce QNaN and clear the result predicate.
        (0x40, 0x0d, nop_m(), frcpa(8, 6, 6, 1, sf=0), nop_i()),
        (0x50, 0x0d, nop_m(), frsqrta(9, 7, 6, sf=0), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f6": ExpectedFP(0, 0x1ffff),
        "f8": ExpectedFP(0xc000000000000000, 0x3ffff),
        "f9": ExpectedFP(0xc000000000000000, 0x3ffff),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=0),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_unsupported_invalid_enabled_rolls_back = require_registers(
    "frcpa_unsupported_invalid_enabled_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~1)[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x1ffff, 0)),
        (0x40, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x50, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x60, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x70, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x80, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(8, 6, 6, 1, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 1,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~1,
    }, entry=0x10)

test_fp_approx_pseudozero_returns_canonical_zero = require_registers(
    "fp_approx_pseudozero_returns_canonical_zero", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(21, 0x12345, 0),
         addl(22, 0x32345, 0)),
        # Same pseudo-zero exponent, once positive and once negative.
        (0x30, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x40, 0x09, st8(5, 0), st8(6, 22), nop_i()),
        (0x50, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x60, 0x0d, nop_m(), frsqrta(8, 6, 6, sf=0), nop_i()),
        (0x70, 0x0d, nop_m(), frsqrta(9, 7, 7, sf=0), nop_i()),
        (0x80, 0x0d, nop_m(), frcpa(10, 8, 6, 1, sf=0), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(11, 9, 7, 1, sf=0), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "f6": ExpectedFP(0, 0x12345),
        "f7": ExpectedFP(0, 0x32345),
        "f8": ExpectedFP(0, 0),
        "f9": ExpectedFP(0, 0x20000),
        "f10": ExpectedFP(0, 0),
        "f11": ExpectedFP(0, 0x20000),
        "pr_mask": ExpectedBits(mask=sum(1 << p for p in range(6, 10)),
                                value=0),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frsqrta_pseudozero_d_fault_rolls_back = require_registers(
    "frsqrta_pseudozero_d_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x12345, 0)),
        (0x40, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x50, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x60, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x70, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x80, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0x90, 0x0d, nop_m(), frsqrta(8, 6, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_frcpa_pseudozero_z_precedes_d = require_registers(
    "frcpa_pseudozero_z_precedes_d", [
        (0x10, 0x05,
         *movl_mlx(2, DEFAULT_FPSR & ~((1 << 1) | (1 << 2)))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x12345, 0)),
        (0x40, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x50, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x60, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x70, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x80, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(8, 6, 1, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 4,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~((1 << 1) | (1 << 2)),
    }, entry=0x10)

test_frsqrta_negative_unnormal_v_precedes_d = require_registers(
    "frsqrta_negative_unnormal_v_precedes_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~3)[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x30000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x80, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x90, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0xa0, 0x0d, nop_m(), frsqrta(8, 6, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 1,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~3,
    }, entry=0x10)

test_frcpa_swa_precedes_unnormal_d = require_registers(
    "frcpa_swa_precedes_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 65, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x80, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x90, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0xa0, 0x0d, nop_m(), frcpa(8, 6, 6, 1, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 8,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_frsqrta_swa_precedes_unnormal_d = require_registers(
    "frsqrta_swa_precedes_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 65, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x80, 0x01, setf_d(8, 5), adds(16, 1, 0), nop_i()),
        (0x90, 0x01, nop_m(), cmp_ltu_unc(6, 7, 0, 16), nop_i()),
        (0xa0, 0x0d, nop_m(), frsqrta(8, 6, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 8,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_frcpa_qnan_suppresses_unnormal_d = require_registers(
    "frcpa_qnan_suppresses_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(7, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0x80, 0x01, setf_d(6, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(8, 6, 6, 7), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "f8": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_frsqrta_swa_fault_discards_result = require_registers(
    "frsqrta_swa_fault_discards_result", [
        (0x10, 0x00, addl(2, 64, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(3, 0x4000000000000000)),
        (0x30, 0x00, setf_exp(6, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, setf_d(8, 3), nop_i(),
         nop_i()),
        (0x50, 0x0d, nop_m(), frsqrta(8, 6, 6),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 8,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_fprsqrta_decode = require_registers("fprsqrta_decode", [
    (0x10, *movl_mlx(2, 0x4080000041800000)),
    (0x20, 0x00, setf_sig(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x0d, nop_m(), fprsqrta(8, 6, 6),
     nop_i()),
    (0x40, 0x00, nop_m(), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "f8": ExpectedFP(0x3eff80003e7f8000, 0x1003e),
    "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fprsqrta_denormal_does_not_leak_inexact = require_registers(
    "fprsqrta_denormal_does_not_leak_inexact", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x000000017f800000)),
        (0x40, 0x00, setf_sig(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fprsqrta(8, 6, 6, sf=0), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0x64b4a00000000000, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fprsqrta_invalidates_known_predicate = require_registers(
    "fprsqrta_invalidates_known_predicate", [
        (0x10, 0x00, nop_m(), tf_z(6, 7, 35), nop_i()),
        (0x20, 0x0d, nop_m(), fprsqrta(8, 6, 0), nop_i()),
        (0x30, 0x00, adds(4, 1, 0, qp=6), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r4": 0,
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fprsqrta_simd_high_lane_fault_isr = require_registers(
    "fprsqrta_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33e)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0xbf8000003f800000)),
        (0x40, *movl_mlx(4, 0x4000000040400000)),
        (0x50, 0x00, setf_sig(6, 3), nop_i(),
         nop_i()),
        (0x60, 0x00, setf_sig(8, 4), nop_i(),
         nop_i()),
        (0x70, 0x0d, nop_m(), fprsqrta(8, 6, 6),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x01,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33e,
    }, entry=0x10)

test_fp_s1_pred_false_decode = require_registers("fp_s1_pred_false_decode", [
    (0x10, 0x1c, nop_m(), fnma_s1(7, 9, 6, 1, qp=6),
     nop_b()),
    (0x20, 0x0d, nop_m(), fmpy_s1(10, 8, 6, qp=6),
     nop_i()),
    (0x30, 0x1c, nop_m(), fma_s1(10, 7, 10, 10, qp=6),
     nop_b()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "f7": ExpectedFP(0, 0),
    "f10": ExpectedFP(0, 0),
    "pr_mask": ExpectedBits(mask=1 << 6, value=0),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fma_d_s0_decode = require_registers("fma_d_s0_decode", [
    (0x10, *movl_mlx(2, 0x4000000000000000)),
    (0x20, *movl_mlx(3, 0x4008000000000000)),
    (0x30, *movl_mlx(4, 0x4010000000000000)),
    (0x40, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x50, 0x00, setf_d(2, 3), nop_i(),
     nop_i()),
    (0x60, 0x00, setf_d(3, 4), nop_i(),
     nop_i()),
    (0x70, 0x1c, nop_m(), fma_d_s0(6, 6, 2, 3),
     nop_b()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "f6": ExpectedFP(*binary64_to_spill(0x4024000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fma_s_s0_high_f4_decode = require_registers(
    "fma_s_s0_high_f4_decode", [
        (0x10, *movl_mlx(2, 0x40000000)),
        (0x20, *movl_mlx(3, 0x40800000)),
        (0x30, *movl_mlx(4, 0xc0400000)),
        (0x40, *movl_mlx(5, 0x3f000000)),
        (0x50, 0x09, setf_s(8, 2), setf_s(9, 3), nop_i()),
        (0x60, 0x09, setf_s(33, 4), setf_s(34, 5), nop_i()),
        (0x70, 0x1c, nop_m(), fma_s_s0(35, 8, 33, 9), nop_b()),
        (0x80, 0x1c, nop_m(), fma_s_s0(36, 8, 34, 0), nop_b()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f35": ExpectedFP(*binary32_to_spill(0xc0000000)),
        "f36": ExpectedFP(*binary32_to_spill(0x3f800000)),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_fnmpy_s_s1_decode = require_registers("fnmpy_s_s1_decode", [
    (0x10, *movl_mlx(2, 0x4008000000000000)),
    (0x20, *movl_mlx(3, 0x4000000000000000)),
    (0x30, 0x09, setf_d(29, 2), setf_d(30, 3),
     nop_i()),
    (0x40, 0x1c, nop_m(), fnmpy_s_s1(7, 29, 30),
     nop_b()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "f7": ExpectedFP(*binary64_to_spill(0xc018000000000000)),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_fp_nan_register_priority = require_registers(
    "fp_nan_register_priority", [
        (0x10, *movl_mlx(2, 0x7ff80000000000a1)),
        (0x20, *movl_mlx(3, 0xfff80000000000b2)),
        (0x30, *movl_mlx(4, 0x7ff80000000000c3)),
        (0x40, *movl_mlx(5, 0x3ff0000000000000)),
        (0x50, 0x09, setf_d(6, 2), setf_d(7, 3), nop_i()),
        (0x60, 0x09, setf_d(8, 4), setf_d(9, 5), nop_i()),
        # fsub/fmpy prioritize their second SoftFloat operand (f2/f4).
        (0x70, 0x1c, nop_m(), fsub_d_s0(10, 6, 7), nop_b()),
        (0x80, 0x1c, nop_m(), fmpy_s0(11, 6, 7), nop_b()),
        # FMA priority is architectural f4, then f2, then f3 (B, C, A).
        (0x90, 0x0d, nop_m(), fma_s0(12, 6, 7, 8), nop_i()),
        (0xa0, 0x0d, nop_m(), fma_s0(13, 6, 9, 8), nop_i()),
        # fadd is decoded from fma with f4=f1.  Placing it last also proves
        # that the preceding call-local two-operand rules were restored.
        (0xb0, 0x0d, nop_m(), fma_s0(14, 6, 1, 7), nop_i()),
        (0xc0, *movl_mlx(20, 0x7fc000a17fc000a2)),
        (0xd0, *movl_mlx(21, 0xffc000b17fc000b2)),
        (0xe0, *movl_mlx(22, 0x7fc000c1ffc000c2)),
        (0xf0, *movl_mlx(23, 0x3f8000003f800000)),
        (0x100, 0x09, setf_sig(20, 20), setf_sig(21, 21), nop_i()),
        (0x110, 0x09, setf_sig(22, 22), setf_sig(23, 23), nop_i()),
        (0x120, 0x0d, nop_m(), fpma(24, 22, 20, 21), nop_i()),
        (0x130, 0x0d, nop_m(), fpma(25, 22, 20, 23), nop_i()),
        (0x140, 0x10, nop_m(), nop_i(), br_cond(0x140, 0x140)),
    ], {
        "ip": 0x140,
        "f10": ExpectedFP(*binary64_to_spill(0xfff80000000000b2)),
        "f11": ExpectedFP(*binary64_to_spill(0xfff80000000000b2)),
        "f12": ExpectedFP(*binary64_to_spill(0xfff80000000000b2)),
        "f13": ExpectedFP(*binary64_to_spill(0x7ff80000000000c3)),
        "f14": ExpectedFP(*binary64_to_spill(0xfff80000000000b2)),
        "f24": ExpectedFP(0xffc000b17fc000b2, 0x1003e),
        "f25": ExpectedFP(0x7fc000c1ffc000c2, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fsub_d_s0_decode = require_registers("fsub_d_s0_decode", [
    (0x10, *movl_mlx(2, 0x4024000000000000)),
    (0x20, *movl_mlx(3, 0x4010000000000000)),
    (0x30, 0x00, setf_d(2, 2), nop_i(),
     nop_i()),
    (0x40, 0x00, setf_d(6, 3), nop_i(),
     nop_i()),
    (0x50, 0x1c, nop_m(), fsub_d_s0(2, 2, 6),
     nop_b()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "f2": ExpectedFP(*binary64_to_spill(0x4018000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fmpy_s0_decode = require_registers("fmpy_s0_decode", [
    (0x10, *movl_mlx(2, 0x4000000000000000)),
    (0x20, *movl_mlx(3, 0x4008000000000000)),
    (0x30, 0x00, setf_d(8, 2), nop_i(),
     nop_i()),
    (0x40, 0x00, setf_d(7, 3), nop_i(),
     nop_i()),
    (0x50, 0x1c, nop_m(), fmpy_s0(9, 8, 7),
     nop_b()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "f9": ExpectedFP(*binary64_to_spill(0x4018000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fmpy_masked_invalid_negative_qnan = require_registers(
    "fmpy_masked_invalid_negative_qnan", [
        (0x10, *movl_mlx(2, 0)),
        (0x20, *movl_mlx(3, 0x7ff0000000000000)),
        (0x30, 0x09, setf_d(6, 2), setf_d(7, 3),
         nop_i()),
        (0x40, 0x1d, nop_m(), fmpy_s0(8, 6, 7),
         nop_b()),
        (0x50, 0x01, getf_d(4, 8), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r4": 0xfff8000000000000,
        "f8": ExpectedFP(*binary64_to_spill(0xfff8000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fp_binary_unnormal_sets_d = require_registers(
    "fp_binary_unnormal_sets_d", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        # Raw +1 encoded as an unnormal (integer bit clear).
        (0x20, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fma_s0(8, 6, 1, 1), nop_i()),
        (0x60, 0x1d, nop_m(), fsub_d_s0(9, 6, 1), nop_b()),
        (0x70, 0x1d, nop_m(), fmpy_s0(10, 6, 1), nop_b()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f6": ExpectedFP(0x4000000000000000, 0x10000),
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "f9": ExpectedFP(*binary64_to_spill(0x0000000000000000)),
        "f10": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fadd_unnormal_d_fault_rolls_back = require_registers(
    "fadd_unnormal_d_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4010000000000000)[1:]),
        (0x80, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fma_s0(8, 6, 1, 1), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "f8": ExpectedFP(*binary64_to_spill(0x4010000000000000)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fmpy_pseudozero_infinity_is_infinity = require_registers(
    "fmpy_pseudozero_infinity_is_infinity", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x32345, 0)),
        (0x20, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x30, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x40, 0x05, *movl_mlx(5, 0x7ff0000000000000)[1:]),
        (0x50, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x60, 0x1d, nop_m(), fmpy_s0(8, 6, 7), nop_b()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "f6": ExpectedFP(0, 0x32345),
        "f8": ExpectedFP(*binary64_to_spill(0xfff0000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fadd_qnan_suppresses_unnormal_d = require_registers(
    "fadd_qnan_suppresses_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0x80, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fma_s0(8, 6, 1, 7), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "f8": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fp_muladd_pseudozero_infinity_is_infinity = require_registers(
    "fp_muladd_pseudozero_infinity_is_infinity", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x32345, 0)),
        (0x20, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x30, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x40, 0x05, *movl_mlx(5, 0x7ff0000000000000)[1:]),
        (0x50, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x60, 0x0d, nop_m(), fma_s0(8, 6, 7, 1), nop_i()),
        (0x70, 0x1d, nop_m(), fms_s3(9, 6, 7, 1), nop_b()),
        (0x80, 0x1d, nop_m(), fnma_s1(10, 6, 7, 1), nop_b()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f6": ExpectedFP(0, 0x32345),
        "f8": ExpectedFP(*binary64_to_spill(0xfff0000000000000)),
        "f9": ExpectedFP(*binary64_to_spill(0xfff0000000000000)),
        "f10": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF2_SHIFT + 13 + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_invalid_precedes_unnormal_d = require_registers(
    "fma_invalid_precedes_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x7ff0000000000000)[1:]),
        (0x80, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x90, 0x05, *movl_mlx(5, 0xfff0000000000000)[1:]),
        (0xa0, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0xb0, 0x0d, nop_m(), fma_s0(9, 6, 7, 8), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "f9": ExpectedFP(0xc000000000000000, 0x3ffff),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 1)) |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fma_qnan_suppresses_unnormal_d = require_registers(
    "fma_qnan_suppresses_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10000, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x80, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x90, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0xa0, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0xb0, 0x0d, nop_m(), fma_s0(9, 6, 7, 8), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "f9": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fma_unsupported_precedes_qnan = require_registers(
    "fma_unsupported_precedes_qnan", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x1ffff, 0)),
        (0x20, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x30, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x40, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x50, 0x01, setf_d(7, 5), nop_i(), nop_i()),
        (0x60, 0x05, *movl_mlx(5, 0x7ff8123456789abc)[1:]),
        (0x70, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fma_s0(9, 7, 8, 6), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "f9": ExpectedFP(0xc000000000000000, 0x3ffff),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fma_qnan_precedes_zero_times_infinity = require_registers(
    "fma_qnan_precedes_zero_times_infinity", [
        (0x10, 0x05, *movl_mlx(2, 0x0000000000000000)[1:]),
        (0x20, 0x05, *movl_mlx(3, 0x7ff0000000000000)[1:]),
        (0x30, 0x05, *movl_mlx(4, 0x7ff8123456789abc)[1:]),
        (0x40, 0x09, setf_d(6, 2), setf_d(7, 3), nop_i()),
        (0x50, 0x01, setf_d(8, 4), nop_i(), nop_i()),
        (0x60, 0x0d, nop_m(), fma_s0(9, 6, 7, 8), nop_i()),
        (0x70, 0x05, *movl_mlx(4, 0x7ff0123456789abc)[1:]),
        (0x80, 0x01, setf_d(8, 4), nop_i(), nop_i()),
        (0x90, 0x0d, nop_m(), fma_s0(10, 6, 7, 8), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "f9": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "f10": ExpectedFP(*binary64_to_spill(0x7ff8123456789abc)),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fmpy_s_s1_decode = require_registers("fmpy_s_s1_decode", [
    (0x10, *movl_mlx(2, 0x4000000000000000)),
    (0x20, 0x00, setf_d(21, 2), nop_i(),
     nop_i()),
    (0x30, 0x1c, nop_m(), fmpy_s_s1(5, 21, 0),
     nop_b()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "f5": ExpectedFP(0, 0),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fms_s3_decode = require_registers("fms_s3_decode", [
    (0x10, *movl_mlx(2, 0x4000000000000000)),
    (0x20, *movl_mlx(3, 0x4008000000000000)),
    (0x30, *movl_mlx(4, 0x4010000000000000)),
    (0x40, 0x00, setf_d(18, 2), nop_i(),
     nop_i()),
    (0x50, 0x00, setf_d(19, 3), nop_i(),
     nop_i()),
    (0x60, 0x00, setf_d(20, 4), nop_i(),
     nop_i()),
    (0x70, 0x1c, nop_m(), fms_s3(4, 18, 19, 20),
     nop_b()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "f4": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fnma_d_s1_decode = require_registers("fnma_d_s1_decode", [
    (0x10, *movl_mlx(2, 0x4000000000000000)),
    (0x20, *movl_mlx(3, 0x4008000000000000)),
    (0x30, *movl_mlx(4, 0x4024000000000000)),
    (0x40, 0x00, setf_d(8, 2), nop_i(),
     nop_i()),
    (0x50, 0x00, setf_d(12, 3), nop_i(),
     nop_i()),
    (0x60, 0x00, setf_d(31, 4), nop_i(),
     nop_i()),
    (0x70, 0x1c, nop_m(), fnma_d_s1(10, 8, 12, 31),
     nop_b()),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "f10": ExpectedFP(*binary64_to_spill(0x4010000000000000)),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fclass_m_decode = require_registers("fclass_m_decode", [
    (0x10, *movl_mlx(2, 0x7ff0000000000000)),
    (0x20, 0x00, setf_d(8, 2), nop_i(),
     nop_i()),
    (0x30, 0x1c, nop_m(), fclass_m(6, 7, 8, 0x21),
     nop_b()),
    (0x40, 0x02, nop_m(), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 6),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fclass_m_ignored_bits_decode = require_registers(
    "fclass_m_ignored_bits_decode", [
        (0x10, *movl_mlx(2, 0x7ff0000000000000)),
        (0x20, 0x00, setf_d(8, 2), nop_i(),
         nop_i()),
        (0x30, 0x1c, nop_m(), fclass_m(10, 11, 8, 0x21, ignored=3),
         nop_b()),
        (0x40, 0x02, nop_m(), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "pr_mask": ExpectedBits(mask=(1 << 10) | (1 << 11),
                                value=1 << 10),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_fclass_raw_unsupported_and_pseudozero = require_registers(
    "fclass_raw_unsupported_and_pseudozero", [
        (0x10, 0x00, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x00, addl(6, 0x218, 0), addl(7, 0x220, 0),
         addl(8, 0x228, 0)),
        (0x30, 0x00, addl(21, 0x12345, 0), addl(23, 0x1ffff, 0),
         addl(25, 0x3ffff, 0)),
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        # Raw spill records: +pseudo-zero, +pseudo-NaN, -pseudo-infinity.
        (0x50, 0x08, st8(3, 0), st8(4, 21),
         nop_i()),
        (0x60, 0x08, st8(5, 22), st8(6, 23),
         nop_i()),
        (0x70, 0x09, st8(7, 0), st8(8, 25),
         nop_i()),
        (0x80, 0x08, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x90, 0x01, ldf_fill_postinc(8, 7, 0), nop_i(), nop_i()),
        # Pseudo-zero is an unnormal supported operand, not a zero.
        (0xa0, 0x1c, nop_m(), fclass_m(6, 7, 6, 0x005), nop_b()),
        (0xb0, 0x1c, nop_m(), fclass_m(8, 9, 6, 0x009), nop_b()),
        (0xc0, 0x1c, nop_m(), fclass_m(10, 11, 6, 0x1ff), nop_b()),
        # Unsupported pseudo-NaN/pseudo-infinity match neither normal nor
        # the architectural "any supported operand" mask.
        (0xd0, 0x1c, nop_m(), fclass_m(12, 13, 7, 0x013), nop_b()),
        (0xe0, 0x1c, nop_m(), fclass_m(14, 15, 7, 0x1ff), nop_b()),
        (0xf0, 0x1c, nop_m(), fclass_m(16, 17, 8, 0x013), nop_b()),
        (0x100, 0x1c, nop_m(), fclass_m(18, 19, 8, 0x1ff), nop_b()),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "f6": ExpectedFP(0, 0x12345),
        "f7": ExpectedFP(0x4000000000000000, 0x1ffff),
        "f8": ExpectedFP(0, 0x3ffff),
        "pr_mask": ExpectedBits(
            mask=sum(1 << predicate for predicate in range(6, 20)),
            value=sum(1 << predicate for predicate in
                      (7, 8, 10, 13, 15, 17, 19))),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fclass_same_pred_pred_false_noop = require_registers(
    "fclass_same_pred_pred_false_noop", [
        (0x10, 0x1c, nop_m(), fclass_m(6, 6, 1, 0x8, qp=7),
         nop_b()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=0),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_natval_clears_predicates = require_registers(
    "fcmp_natval_clears_predicates", [
        (0x10, 0x00, cmp4_eq_unc_imm(6, 0, 0, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, cmp4_eq_unc_imm(7, 0, 0, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, mov_m_imm_ar(36, 1), addl(8, 0x200, 0),
         nop_i()),
        (0x40, 0x08, ld8_fill_postinc(3, 8, 0), nop_i(),
         nop_i()),
        (0x50, 0x00, ldf8_s(9, 3), nop_i(),
         nop_i()),
        (0x60, 0x1c, nop_m(), fcmp(6, 7, 9, 1),
         nop_b()),
        (0x70, 0x02, nop_m(), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=0),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcmp_status_field_decode = require_registers(
    "fcmp_status_field_decode", [
        (0x10, *movl_mlx(2, 0x3ff0000000000000)),
        (0x20, *movl_mlx(3, 0x4000000000000000)),
        (0x30, 0x00, setf_d(10, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, setf_d(11, 3), nop_i(),
         nop_i()),
        (0x50, 0x1c, nop_m(), fcmp(7, 8, 10, 11, rel=1, sf=1),
         nop_b()),
        (0x60, 0x1c, nop_m(), fcmp(9, 10, 10, 10, rel=2, sf=3),
         nop_b()),
        (0x70, 0x02, nop_m(), nop_i(), nop_i()),
        (0x80, 0x02, nop_m(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "pr_mask": ExpectedBits(
            mask=(1 << 7) | (1 << 8) | (1 << 9) | (1 << 10),
            value=(1 << 7) | (1 << 9)),
        "ar_fpsr": DEFAULT_FPSR,
    },
    entry=0x10)

test_fcmp_same_pred_illegal = require_exception(
    "fcmp_same_pred_illegal",
    [(0x10, 0x1c, nop_m(), fcmp(6, 6, 1, 1), nop_b())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_fclass_unc_same_pred_pred_false_illegal = require_exception(
    "fclass_unc_same_pred_pred_false_illegal",
    [(0x10, 0x1c, nop_m(), fclass_m(6, 6, 1, 0x1ff, unc=True, qp=7),
      nop_b())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_fcvt_fxu_double_to_uint = require_registers("fcvt_fxu_double_to_uint", [
    (0x10, *movl_mlx(2, 0x400e000000000000)),
    (0x20, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x0d, nop_m(), fcvt_fxu(7, 6),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "f7": ExpectedFP(3, 0x1003e),
    "ar_fpsr": 0x0009804c8270033f,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fcvt_fxu_rounds_sf0 = require_registers("fcvt_fxu_rounds_sf0", [
    (0x10, *movl_mlx(2, 0x400e000000000000)),
    (0x20, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x0d, nop_m(), fcvt_fxu(7, 6, trunc=False, sf=0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "f7": ExpectedFP(4, 0x1003e),
    "ar_fpsr": 0x0009804c0274033f,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fcvt_wre_invalid_suppresses_d_and_oi = require_registers(
    "fcvt_wre_invalid_suppresses_d_and_oi", [
        # Normalizing this unnormal operand leaves exponent 0x18000.  V is
        # therefore the only response; the enabled D fault is lower priority.
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(20, 1, 0), addl(21, 0x1803f, 0), nop_i()),
        (0x40, 0x09, st8(3, 20), st8(4, 21), nop_i()),
        (0x50, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x60, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fxu(9, 6, sf=0), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "f8": ExpectedFP(0x8000000000000000, 0x1003e),
        "f9": ExpectedFP(0x8000000000000000, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 1)) |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_wre_tiny_reports_i_not_u = require_registers(
    "fcvt_wre_tiny_reports_i_not_u", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(20, 0x8000, 0)),
        (0x20, *movl_mlx(21, 0x8000000000000000)),
        (0x30, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fxu(8, 6, sf=0), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x20 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

_FCVT_WRE_RC_SHIFT = FPSR_SF0_SHIFT + 4
_FCVT_WRE_ROUND_UP_FPSR = (
    (DEFAULT_FPSR & ~(1 << 5) & ~(3 << _FCVT_WRE_RC_SHIFT)) |
    (2 << _FCVT_WRE_RC_SHIFT)
)
test_fcvt_wre_tiny_round_up_traps_i_with_fpa = require_registers(
    "fcvt_wre_tiny_round_up_traps_i_with_fpa", [
        (0x10, *movl_mlx(2, _FCVT_WRE_ROUND_UP_FPSR)),
        (0x20, 0x01, mov_m_gr_ar(2, 40), addl(3, 0x200, 0),
         addl(4, 0x208, 0)),
        (0x30, 0x01, addl(20, 0x8000, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(21, 0x8000000000000000)),
        (0x50, 0x09, st8(3, 21), st8(4, 20), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2001),
        "f8": ExpectedFP(1, 0x1003e),
        "ar_fpsr": (_FCVT_WRE_ROUND_UP_FPSR |
                    (0x20 <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_inexact_trap_magnitude_roundup_sets_fpa = require_registers(
    "fcvt_inexact_trap_magnitude_roundup_sets_fpa", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ffc000000000000)),  # +1.75
        (0x40, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "r10": (IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) |
                (1 << 14) | 0x2001),
        "f8": ExpectedFP(2, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (0x20 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_inexact_trap_magnitude_rounddown_clears_fpa = require_registers(
    "fcvt_inexact_trap_magnitude_rounddown_clears_fpa", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 5))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3ff4000000000000)),  # +1.25
        (0x40, 0x00, setf_d(6, 3), nop_i(), nop_i()),
        (0x50, 0x0d, nop_m(),
         fcvt_fxu(8, 6, trunc=False, sf=0), nop_i()),
        (IA64_FP_TRAP_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_TRAP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_TRAP_VECTOR + 0x10,
                 IA64_FP_TRAP_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_TRAP_VECTOR + 0x10,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x2001,
        "f8": ExpectedFP(1, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 5)) |
                    (0x20 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_fx_signed_trunc = require_registers("fcvt_fx_signed_trunc", [
    (0x10, *movl_mlx(2, 0xc00e000000000000)),
    (0x20, 0x00, setf_d(6, 2), nop_i(),
     nop_i()),
    (0x30, 0x0d, nop_m(), fcvt_fx(7, 6, trunc=True),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "f7": ExpectedFP(0xfffffffffffffffd, 0x1003e),
    "ar_fpsr": 0x0009804c8270033f,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fcvt_masked_invalid_integer_indefinite = require_registers(
    "fcvt_masked_invalid_integer_indefinite", [
        # As a canonical integer-format operand this is +2^64-1, which is
        # outside the signed result range.  It must not be copied verbatim.
        (0x10, *movl_mlx(2, 0xffffffffffffffff)),
        (0x20, *movl_mlx(3, 0x7ff0000000000000)),
        (0x30, 0x09, setf_sig(6, 2), setf_d(7, 3), nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fxu(9, 7, sf=0), nop_i()),
        (0x60, 0x09, getf_sig(4, 8), getf_sig(5, 9), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r4": 0x8000000000000000,
        "r5": 0x8000000000000000,
        "f8": ExpectedFP(0x8000000000000000, 0x1003e),
        "f9": ExpectedFP(0x8000000000000000, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_invalid_enabled_fault_discards_result = require_registers(
    "fcvt_invalid_enabled_fault_discards_result", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~1)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x7ff8000000001234)),
        (0x40, 0x00, addl(4, 0x55, 0), nop_i(), nop_i()),
        (0x50, 0x09, setf_d(6, 3), setf_sig(8, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 1,
        "f8": ExpectedFP(0x55, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR & ~1,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_unnormal_sets_d = require_registers(
    "fcvt_unnormal_sets_d", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x12345, 0)),
        # A raw pseudo-zero remains an unnormal input even though its value is 0.
        (0x20, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x30, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_fxu(9, 6, trunc=True, sf=1), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0, 0x1003e),
        "f9": ExpectedFP(0, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT)) |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_unnormal_d_fault_rolls_back = require_registers(
    "fcvt_unnormal_d_fault_rolls_back", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x12345, 0)),
        (0x40, 0x09, st8(3, 0), st8(4, 21), nop_i()),
        (0x50, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x60, 0x05, *movl_mlx(5, 0x4000000000000000)[1:]),
        (0x70, 0x01, setf_d(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 2,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "ar_fpsr": DEFAULT_FPSR & ~(1 << 1),
    }, entry=0x10)

test_fcvt_invalid_precedes_unnormal_d = require_registers(
    "fcvt_invalid_precedes_unnormal_d", [
        (0x10, 0x05, *movl_mlx(2, DEFAULT_FPSR & ~(1 << 1))[1:]),
        (0x20, 0x01, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x10080, 0)),
        # Unnormal finite value which normalizes beyond the integer range.
        (0x40, 0x05, *movl_mlx(22, 0x4000000000000000)[1:]),
        (0x50, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x60, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fx(8, 6, sf=0), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "f8": ExpectedFP(0x8000000000000000, 0x1003e),
        "ar_fpsr": ((DEFAULT_FPSR & ~(1 << 1)) |
                    (1 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_fcvt_fxu_preserves_sig_payload = require_registers(
    "fcvt_fxu_preserves_sig_payload", [
        (0x10, 0x00, addl(2, 0x2a, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, setf_sig(6, 2), nop_i(),
         nop_i()),
        (0x30, 0x0d, nop_m(), fcvt_fxu(7, 6, sf=0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "f7": ExpectedFP(0x2a, 0x1003e),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)

test_fcvt_xf_signed_sig_to_float = require_registers(
    "fcvt_xf_signed_sig_to_float", [
        (0x10, 0x00, addl(2, 42, 0), adds(3, -3, 0),
         nop_i()),
        (0x20, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x30, 0x0d, nop_m(), fcvt_xf(8, 6),
         nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_xf(9, 7),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(*binary64_to_spill(0x4045000000000000)),
        "f9": ExpectedFP(*binary64_to_spill(0xc008000000000000)),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_xf_ignores_prior_precision = require_registers(
    "fcvt_xf_ignores_prior_precision", [
        (0x10, *movl_mlx(2, 0xc3369a5a)),
        (0x20, 0x00, setf_sig(6, 2), nop_i(), nop_i()),
        # Prime SoftFloat with an operation whose static precision is single.
        (0x30, 0x0d, nop_m(), fma_s1(7, 1, 1, 0), nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_xf(8, 6), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "f8": ExpectedFP(0xc3369a5a00000000, 0x1001e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_xf_reads_register_significand = require_registers(
    "fcvt_xf_reads_register_significand", [
        (0x10, *movl_mlx(2, 0xc3369a5a)),
        (0x20, 0x00, setf_sig(6, 2), nop_i(), nop_i()),
        (0x30, 0x0d, nop_m(), fcvt_xf(8, 6), nop_i()),
        # The second conversion consumes f8's architectural significand,
        # not the binary64 cache used internally for display/convenience.
        (0x40, 0x0d, nop_m(), fcvt_xf(9, 8), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "f8": ExpectedFP(0xc3369a5a00000000, 0x1001e),
        "f9": ExpectedFP(0xf325969800000000, 0x3003c),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_xf_extreme_signed_round_trip = require_registers(
    "fcvt_xf_extreme_signed_round_trip", [
        (0x10, *movl_mlx(2, 0x7fffffffffffffff)),
        (0x20, *movl_mlx(3, 0x8000000000000000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_xf(8, 6), nop_i()),
        (0x50, 0x0d, nop_m(), fcvt_xf(9, 7), nop_i()),
        (0x60, 0x0d, nop_m(), fcvt_fx(10, 8, trunc=True), nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fx(11, 9, trunc=True), nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0xfffffffffffffffe, 0x1003d),
        "f9": ExpectedFP(0x8000000000000000, 0x3003e),
        "f10": ExpectedFP(0x7fffffffffffffff, 0x1003e),
        "f11": ExpectedFP(0x8000000000000000, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fcvt_xf_natval_propagates = require_registers(
    "fcvt_xf_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x00, ldf8_s(7, 3), nop_i(),
         nop_i()),
        (0x40, 0x0d, nop_m(), fcvt_xf(8, 7),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         nop_b()),
        (0x60, 0x02, nop_m(), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x70,
        "f8": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_setf_sig_direct_scalar_operand = require_registers(
    "setf_sig_direct_scalar_operand", [
        (0x10, 0x00, addl(2, 2, 0), addl(3, 3, 0),
         nop_i()),
        (0x20, 0x09, setf_sig(6, 2), setf_sig(7, 3),
         nop_i()),
        (0x30, 0x0d, nop_m(), fma_s0(8, 6, 7, 1),
         nop_i()),
        (0x40, 0x0d, nop_m(), fpneg(9, 6),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "f8": ExpectedFP(0xe000000000000000, 0x10001),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_setf_d_f1_illegal_operation = require_exception(
    "setf_d_f1_illegal_operation", [
    (0x10, *movl_mlx(2, 0)),
    (0x20, 0x00, setf_d(1, 2), nop_i(),
     nop_i()),
], IA64_EXCP_ILLEGAL, fault_ip=0x20)

test_fp_fixed_target_predicated_off_is_nop = require_registers(
    "fp_fixed_target_predicated_off_is_nop", [
        (0x10, *movl_mlx(2, 0)),
        (0x20, 0x00, setf_d(1, 2, qp=1), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "f1": ExpectedFP(0x8000000000000000, 0xffff),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fma_f0_illegal_operation = require_exception(
    "fma_f0_illegal_operation", [
        (0x10, 0x0d, nop_m(), fma_s0(0, 1, 1, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_fand_f1_illegal_operation = require_exception(
    "fand_f1_illegal_operation", [
        (0x10, 0x0d, nop_m(), fand(1, 0, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_frcpa_capacity_calc = require_registers("frcpa_capacity_calc", [
    (0x10, 0x00, addl(24, 0x230, 0), adds(25, 0x28, 0),
     nop_i()),
    (0x20, 0x09, setf_sig(8, 24), setf_sig(9, 25),
     cmp_ltu_unc(6, 0, 0, 25)),
    (0x30, 0x0d, nop_m(), fnorm(6, 0, 8),
     nop_i()),
    (0x40, 0x0d, nop_m(), fnorm(7, 0, 9),
     nop_i()),
    (0x50, 0x0d, nop_m(), frcpa(8, 6, 6, 7),
     nop_i()),
    (0x60, 0x1c, nop_m(), fnma_s1(9, 7, 8, 1, qp=6),
     nop_b()),
    (0x70, 0x0d, nop_m(), fmpy_s1(10, 6, 8, qp=6),
     nop_i()),
    (0x80, 0x1c, nop_m(), fma_s1(8, 9, 8, 8, qp=6),
     nop_b()),
    (0x90, 0x0d, nop_m(), fcvt_fxu(8, 8),
     nop_i()),
    (0xa0, 0x10, nop_m(), nop_i(),
     br_cond(0xa0, 0xb0)),
    (0xb0, 0x00, nop_m(), adds(4, 1, 4),
     nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(),
     br_cond(0xc0, 0xc0)),
], {
    "ip": 0xc0,
    "r4": 1,
    "f8": ExpectedFP(0, 0x1003e),
    "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
    "ar_fpsr": (0x0009804c8270033f |
                (FPSR_SF_D_FLAG <<
                 (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
}, entry=0x10)

test_coreutils_hash_bucket_float_division = require_registers(
    "coreutils_hash_bucket_float_division", [
        (0x10, *movl_mlx(2, 103)),
        (0x20, *movl_mlx(3, 0x5f8000003f4ccccd)),
        (0x30, 0x00, addl(4, 0x200, 0), nop_i(), nop_i()),
        (0x40, 0x09, st8(4, 3), setf_sig(8, 2), nop_i()),
        (0x50, 0x0d, ldfs(7, 4), fnorm(6, 0, 8), nop_i()),
        (0x60, 0x0d, nop_m(), fmov(9, 7), nop_i()),
        (0x70, 0x0d, nop_m(), fmov(8, 6), nop_i()),
        (0x80, 0x0d, nop_m(), frcpa(6, 6, 8, 9, sf=0), nop_i()),
        (0x90, 0x1c, nop_m(), fnma_s1(10, 9, 6, 1, qp=6), nop_b()),
        (0xa0, 0x1c, nop_m(), fma_s1(7, 6, 10, 6, qp=6), nop_b()),
        (0xb0, 0x1c, nop_m(), fma_s1(7, 7, 10, 6, qp=6), nop_b()),
        (0xc0, 0x0d, nop_m(), fmpy_s_s1(10, 8, 7, qp=6), nop_i()),
        (0xd0, 0x1c, nop_m(), fnma_s1(8, 10, 9, 8, qp=6), nop_b()),
        (0xe0, 0x1c, nop_m(), fma_s_s0(6, 8, 7, 10, qp=6), nop_b()),
        (0xf0, 0x00, nop_m(), adds(3, 4, 4), nop_i()),
        (0x100, 0x0d, nop_m(), fmov(7, 6), nop_i()),
        (0x110, 0x00, ldfs(6, 3), nop_i(), nop_i()),
        (0x120, 0x0d, nop_m(), fcmp(6, 7, 6, 7, rel=2), nop_i()),
        (0x130, 0x00, nop_m(), nop_i(), nop_i()),
        (0x140, 0x0d, nop_m(), fcvt_fxu(7, 7, sf=0), nop_i()),
        (0x150, 0x10, nop_m(), nop_i(), br_cond(0x150, 0x150)),
    ], {
        "ip": 0x150,
        "f7": ExpectedFP(128, 0x1003e),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=1 << 7),
        "ar_fpsr": (0x0009804c8274033f |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_integer_division = require_registers(
    "frcpa_integer_division", [
        (0x10, *movl_mlx(26, 0x3f800040)),
        (0x20, 0x00, addl(24, 400, 0), adds(25, 12, 0),
         nop_i()),
        (0x30, 0x09, setf_sig(10, 24), setf_sig(9, 25),
         nop_i()),
        (0x40, 0x0d, setf_s(8, 26), fnorm(10, 0, 10),
         nop_i()),
        (0x50, 0x0d, nop_m(), fnorm(9, 0, 9),
         nop_i()),
        (0x60, 0x0d, nop_m(), frcpa(6, 6, 10, 9),
         nop_i()),
        (0x70, 0x0d, nop_m(), fmpy_s1(10, 6, 10, qp=6),
         nop_i()),
        (0x80, 0x1c, nop_m(), fnma_s1(9, 9, 6, 8, qp=6),
         nop_b()),
        (0x90, 0x1c, nop_m(), fma_s1(6, 9, 10, 10, qp=6),
         nop_b()),
        (0xa0, 0x0d, nop_m(), fcvt_fxu(6, 6),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "f6": ExpectedFP(33, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": (0x0009804c8270033f |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

_HIGH_SIG_DIVIDEND = 0xa0000001006ad328

test_frcpa_setf_sig_high_integer_remainder = require_registers(
    "frcpa_setf_sig_high_integer_remainder", [
        (0x10, *movl_mlx(22, _HIGH_SIG_DIVIDEND)),
        (0x20, *movl_mlx(23, 16)),
        (0x30, 0x09, setf_sig(8, 22), setf_sig(9, 23),
         nop_i()),
        (0x40, 0x0d, nop_m(), fnorm(8, 0, 8),
         nop_i()),
        (0x50, 0x0d, nop_m(), fnorm(9, 0, 9),
         nop_i()),
        (0x60, 0x0d, nop_m(), frcpa(11, 6, 8, 9),
         nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_fxu(11, 11),
         nop_i()),
        (0x80, *movl_mlx(23, (-16) & 0xffffffffffffffff)),
        (0x90, 0x00, setf_sig(9, 23), nop_i(),
         nop_i()),
        (0xa0, 0x1d, nop_m(), xma_l(11, 8, 11, 9),
         nop_b()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "f11": ExpectedFP(_HIGH_SIG_DIVIDEND, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": (0x0009804c8270033f |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF1_SHIFT + FPSR_SF_FLAGS_SHIFT))),
    }, entry=0x10)

test_umodsi3_hash_remainder = require_registers(
    "umodsi3_hash_remainder", [
        # Compute an unsigned remainder using reciprocal refinement.
        (0x10, *movl_mlx(22, 0xc3369a5a)),
        (0x20, *movl_mlx(23, 17)),
        (0x30, 0x00, addl(2, 65501, 0), nop_i(), nop_i()),
        (0x40, 0x09, setf_sig(13, 22), setf_sig(9, 23), nop_i()),
        # fcvt.xf must remain exact after a single-precision operation.
        (0x50, 0x0d, nop_m(), fma_s1(6, 1, 1, 0), nop_i()),
        (0x60, 0x0d, sub_reg(23, 0, 23), fcvt_xf(8, 13), nop_i()),
        (0x70, 0x0d, nop_m(), fcvt_xf(9, 9), nop_i()),
        (0x80, 0x0d, setf_exp(11, 2), frcpa(10, 6, 8, 9), nop_i()),
        (0x90, 0x0c, nop_m(), fmpy_s1(12, 8, 10, qp=6), nop_i()),
        (0xa0, 0x0d, nop_m(), fnma_s1(10, 9, 10, 1, qp=6), nop_i()),
        (0xb0, 0x0c, setf_sig(9, 23),
         fma_s1(12, 10, 12, 12, qp=6), nop_i()),
        (0xc0, 0x0d, nop_m(), fma_s1(10, 10, 10, 11, qp=6), nop_i()),
        (0xd0, 0x0d, nop_m(), fma_s1(10, 10, 12, 12, qp=6), nop_i()),
        (0xe0, 0x0d, nop_m(), fcvt_fxu(10, 10), nop_i()),
        (0xf0, 0x0d, nop_m(), xma_l(10, 13, 10, 9), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
    ], {
        "ip": 0x100,
        "f10": ExpectedFP(0, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": 0x0009804c8270033f,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_double_normal_reciprocal = require_registers(
    "frcpa_double_normal_reciprocal", [
        (0x10, *movl_mlx(2, 0x4010000000000000)),
        (0x20, *movl_mlx(3, 0x4000000000000000)),
        (0x30, 0x09, setf_d(6, 2), setf_d(7, 3),
         nop_i()),
        (0x40, 0x0d, nop_m(), frcpa(8, 6, 6, 7),
         nop_i()),
        (0x50, 0x00, nop_m(), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "f8": ExpectedFP(0xff80000000000000, 0x0fffd),
        "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
        "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fmov_preserves_integer_register_format = require_registers(
    "fmov_preserves_integer_register_format", [
        (0x10, *movl_mlx(2, 3)),
        (0x20, *movl_mlx(3, 5)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x40, 0x0d, nop_m(), fmov(8, 6), nop_i()),
        (0x50, 0x1d, nop_m(), xma_l(9, 0, 6, 7), nop_b()),
        (0x60, 0x0d, nop_m(), fmov(10, 9), nop_i()),
        (0x70, 0x09, getf_sig(4, 8), getf_sig(5, 10), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r4": 3,
        "r5": 15,
        "f8": ExpectedFP(3, 0x1003e),
        "f10": ExpectedFP(15, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_wre1_uses_raw_exponents = require_registers(
    "frcpa_wre1_uses_raw_exponents", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(5, 0x210, 0)),
        (0x20, 0x01, addl(6, 0x218, 0), addl(21, 0x18000, 0),
         addl(22, 0x8000, 0)),
        (0x30, *movl_mlx(23, 0x8000000000000000)),
        (0x40, 0x09, st8(3, 23), st8(4, 21), nop_i()),
        (0x50, 0x09, st8(5, 23), st8(6, 22), nop_i()),
        (0x60, 0x09, ldf_fill_postinc(6, 3, 0),
         ldf_fill_postinc(7, 5, 0), nop_i()),
        (0x70, 0x0d, nop_m(), frcpa(8, 6, 6, 6), nop_i()),
        (0x80, 0x0d, nop_m(), frcpa(9, 7, 7, 7), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f8": ExpectedFP(0xff80000000000000, 0x07ffd),
        "f9": ExpectedFP(0xff80000000000000, 0x17ffd),
        "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7),
                                value=(1 << 6) | (1 << 7)),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_wre1_special_responses_use_raw_types = require_registers(
    "frcpa_wre1_special_responses_use_raw_types", [
        (0x10, 0x01, addl(3, 0x200, 0), addl(4, 0x208, 0),
         addl(21, 0x38000, 0)),
        (0x20, *movl_mlx(22, 0x8000000000000000)),
        (0x30, 0x09, st8(3, 22), st8(4, 21), nop_i()),
        (0x40, 0x01, ldf_fill_postinc(6, 3, 0), nop_i(), nop_i()),
        (0x50, *movl_mlx(23, 0x7ff0000000000000)),
        (0x60, 0x00, setf_d(7, 23), nop_i(), nop_i()),
        (0x70, 0x0d, nop_m(), frcpa(10, 6, 6, 7, sf=0), nop_i()),
        (0x80, 0x0d, nop_m(), frcpa(11, 7, 0, 6, sf=0), nop_i()),
        (0x90, 0x0d, nop_m(), frcpa(12, 8, 6, 0, sf=0), nop_i()),
        (0xa0, 0x0d, nop_m(), frcpa(13, 9, 7, 6, sf=0), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "f10": ExpectedFP(0, 0x20000),
        "f11": ExpectedFP(0, 0x20000),
        "f12": ExpectedFP(0x8000000000000000, 0x3ffff),
        "f13": ExpectedFP(0x8000000000000000, 0x3ffff),
        "pr_mask": ExpectedBits(mask=sum(1 << p for p in range(6, 10)),
                                value=0),
        "ar_fpsr": (DEFAULT_FPSR |
                    (0x04 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_frcpa_swa_fault_discards_result = require_registers(
    "frcpa_swa_fault_discards_result", [
        (0x10, 0x00, addl(2, 64, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(3, 0x4000000000000000)),
        (0x30, 0x00, setf_exp(6, 2), nop_i(),
         nop_i()),
        (0x40, 0x00, setf_d(8, 3), nop_i(),
         nop_i()),
        (0x50, 0x0d, nop_m(), frcpa(8, 6, 6, 1),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 8,
        "f8": ExpectedFP(*binary64_to_spill(0x4000000000000000)),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_frcpa_special_quotient = require_registers("frcpa_special_quotient", [
    (0x10, *movl_mlx(2, 0x4010000000000000)),
    (0x20, *movl_mlx(3, 0x7ff0000000000000)),
    (0x30, 0x09, setf_d(6, 2), setf_d(7, 3),
     nop_i()),
    (0x40, 0x0d, nop_m(), frcpa(8, 6, 6, 7),
     nop_i()),
    (0x50, 0x00, nop_m(), nop_i(), nop_i()),
    (0x60, *movl_mlx(3, 0x0000000000000000)),
    (0x70, 0x00, setf_d(7, 3), nop_i(),
     nop_i()),
    (0x80, 0x0d, nop_m(), frcpa(9, 6, 6, 7),
     nop_i()),
    (0x90, 0x00, nop_m(), nop_i(), nop_i()),
    (0xa0, 0x10, nop_m(), nop_i(),
     br_cond(0xa0, 0xa0)),
], {
    "ip": 0xa0,
    "f8": ExpectedFP(*binary64_to_spill(0x0000000000000000)),
    "f9": ExpectedFP(*binary64_to_spill(0x7ff0000000000000)),
    "pr_mask": ExpectedBits(mask=1 << 6, value=0),
    "ar_fpsr": 0x0009804c1270033f,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_frcpa_pred_false_clears = require_registers("frcpa_pred_false_clears", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(6, 7, 0, 16),
     nop_i()),
    (0x20, *movl_mlx(20, 0x3ff0000000000000)),
    (0x30, *movl_mlx(21, 0x4010000000000000)),
    (0x40, *movl_mlx(22, 0x4000000000000000)),
    (0x50, 0x09, setf_d(8, 20), setf_d(6, 21),
     nop_i()),
    (0x60, 0x00, setf_d(7, 22), nop_i(),
     nop_i()),
    (0x70, 0x0d, nop_m(), frcpa(8, 6, 6, 7, qp=7),
     nop_i()),
    (0x80, 0x00, nop_m(), nop_i(), nop_i()),
    (0x90, 0x10, nop_m(), nop_i(),
     br_cond(0x90, 0x90)),
], {
    "ip": 0x90,
    "f8": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
    "pr_mask": ExpectedBits(mask=(1 << 6) | (1 << 7), value=0),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_frcpa_p2_high_bits_decode = require_registers("frcpa_p2_high_bits_decode", [
    (0x10, 0x00, addl(24, 0x800, 0), addl(25, 0x800, 0),
     nop_i()),
    (0x20, 0x09, setf_sig(6, 24), setf_sig(7, 25),
     nop_i()),
    (0x30, 0x0d, nop_m(), frcpa(8, 10, 6, 7, sf=0),
     nop_i()),
    (0x40, 0x0d, nop_m(), fcvt_fxu(8, 8, sf=0),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "f8": ExpectedFP(0, 0x1003e),
    "pr_mask": ExpectedBits(mask=1 << 10, value=1 << 10),
    # setf.sig 0x800 is an unnormal floating input; frcpa records D, and
    # truncating the non-integral approximation records I.
    "ar_fpsr": (DEFAULT_FPSR |
                (0x22 << (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
}, entry=0x10)

test_frcpa_natval_propagates = require_registers("frcpa_natval_propagates", [
    (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
     nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), addl(14, 0x800, 0),
     nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(6, 14),
     cmp_ltu_unc(8, 0, 0, 1)),
    (0x40, 0x0d, nop_m(), frcpa(8, 8, 6, 7),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
    (0x200, 0x00, 0, 0,
     0),
], {
    "ip": 0x80,
    "f8": ExpectedFP(0, 0x1fffe, nat=True),
    "pr_mask": ExpectedBits(mask=1 << 8, value=0),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
},
    entry=0x10)

test_fprcpa_decode = require_registers("fprcpa_decode", [
    (0x10, *movl_mlx(2, 0x3f8000003f800000)),
    (0x20, *movl_mlx(3, 0x4080000041800000)),
    (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3),
     nop_i()),
    (0x40, 0x0d, nop_m(), fprcpa(8, 6, 6, 7),
     nop_i()),
    (0x50, 0x00, nop_m(), nop_i(), nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "f8": ExpectedFP(0x3e7f80003d7f8000, 0x1003e),
    "pr_mask": ExpectedBits(mask=1 << 6, value=1 << 6),
    "ar_fpsr": DEFAULT_FPSR,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fprcpa_denormal_denominator_returns_quotient = require_registers(
    "fprcpa_denormal_denominator_returns_quotient", [
        (0x10, *movl_mlx(2, 0x3f00000000000000)),
        (0x20, *movl_mlx(3, 0x004000003f800000)),
        (0x30, 0x09, setf_sig(6, 2), setf_sig(7, 3), nop_i()),
        (0x40, 0x0d, nop_m(), fprcpa(8, 6, 6, 7, sf=0), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "f8": ExpectedFP(0x7e80000000000000, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": (DEFAULT_FPSR |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fprcpa_denormal_overflow_does_not_leak_oi = require_registers(
    "fprcpa_denormal_overflow_does_not_leak_oi", [
        (0x10, *movl_mlx(2, DEFAULT_FPSR & ~((1 << 3) | (1 << 5)))),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3f80000000000000)),
        (0x40, *movl_mlx(4, 0x000000013f800000)),
        (0x50, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x60, 0x0d, nop_m(), fprcpa(8, 6, 6, 7, sf=0), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "f8": ExpectedFP(0x7f80000000000000, 0x1003e),
        "pr_mask": ExpectedBits(mask=1 << 6, value=0),
        "ar_fpsr": ((DEFAULT_FPSR & ~((1 << 3) | (1 << 5))) |
                    (FPSR_SF_D_FLAG <<
                     (FPSR_SF0_SHIFT + FPSR_SF_FLAGS_SHIFT))),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_fprcpa_simd_high_lane_fault_isr = require_registers(
    "fprcpa_simd_high_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33b)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 0x3f8000003f800000)),
        (0x40, *movl_mlx(4, 0x000000003f800000)),
        (0x50, *movl_mlx(5, 0x4000000040400000)),
        (0x60, 0x09, setf_sig(6, 3), setf_sig(7, 4),
         nop_i()),
        (0x70, 0x00, setf_sig(8, 5), nop_i(),
         nop_i()),
        (0x80, 0x0d, nop_m(), fprcpa(8, 6, 6, 7),
         nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x00, nop_m(),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x20,
                 IA64_FP_FAULT_VECTOR + 0x20)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x04,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33b,
    }, entry=0x10)

test_fprcpa_simd_low_lane_fault_isr = require_registers(
    "fprcpa_simd_low_lane_fault_isr", [
        (0x10, *movl_mlx(2, 0x33b)),
        (0x20, 0x00, mov_m_gr_ar(2, 40), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x3f8000003f800000)),
        (0x40, *movl_mlx(4, 0x3f80000000000000)),
        (0x50, *movl_mlx(5, 0x4000000040400000)),
        (0x60, 0x09, setf_sig(6, 3), setf_sig(7, 4), nop_i()),
        (0x70, 0x00, setf_sig(8, 5), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fprcpa(8, 6, 6, 7), nop_i()),
        (IA64_FP_FAULT_VECTOR, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_FP_FAULT_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_FP_FAULT_VECTOR + 0x10,
                 IA64_FP_FAULT_VECTOR + 0x10)),
    ], {
        "ip": IA64_FP_FAULT_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r10": IA64_ISR_NI | (1 << IA64_ISR_EI_SHIFT) | 0x40,
        "f8": ExpectedFP(0x4000000040400000, 0x1003e),
        "ar_fpsr": 0x33b,
    }, entry=0x10)

test_setf_nat_source_sets_fr_natval = require_registers(
    "setf_nat_source_sets_fr_natval", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, setf_sig(7, 16), setf_s(8, 16),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), nop_b()),
        (0x50, 0x02, nop_m(), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "f7": ExpectedFP(0, 0x1fffe, nat=True),
        "f8": ExpectedFP(0, 0x1fffe, nat=True),
    }, entry=0x10)

test_nop_f_decode = require_exception("nop_f_decode", [
    (0x10, 0x0d, nop_m(), nop_f(0x42), nop_i()),
    (0x20, 0x11, nop_m(), nop_i(), break_b()),
], IA64_EXCP_BREAK, fault_ip=0x20)

test_f_reserved_cell_respects_qualifying_predicate = require_exception(
    "f_reserved_cell_respects_qualifying_predicate", [
        # Table 4-60 x6=0x20 is reserved only when its predicate is true.
        # Bit 36 is outside the selector and must not revive the FMOV alias.
        (0x10, 0x0d, nop_m(),
         bitfield(0x20, 27, 6) | bitfield(1, 36, 1) | 1, nop_i()),
        (0x20, 0x0d, nop_m(), bitfield(0x20, 27, 6), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x20)

test_disabled_fp_high_fault = require_registers(
    "disabled_fp_high_fault", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, *movl_mlx(3, 0x1234)),
        (0x30, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, setf_sig(40, 3), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x00, mov_m_cr_gr(8, 19),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x20,
                 IA64_DISABLED_FP_VECTOR + 0x20)),
    ], {
        "ip": IA64_DISABLED_FP_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": 2,
    }, entry=0x10)

test_disabled_fp_low_fault = require_registers(
    "disabled_fp_low_fault", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFL)),
        (0x20, *movl_mlx(3, 0x1234)),
        (0x30, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, setf_sig(8, 3), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x00, mov_m_cr_gr(8, 19),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x20,
                 IA64_DISABLED_FP_VECTOR + 0x20)),
    ], {
        "ip": IA64_DISABLED_FP_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r8": 0x50,
        "r9": 1,
    }, entry=0x10)

test_disabled_fp_load_sets_isr_r = require_registers(
    "disabled_fp_load_sets_isr_r", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x40, 0x00, ldf8(40, 3), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x10,
                 IA64_DISABLED_FP_VECTOR + 0x10)),
    ], {
        "ip": IA64_DISABLED_FP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r9": IA64_ISR_R | 2,
    }, entry=0x10)

test_disabled_fp_store_sets_isr_w = require_registers(
    "disabled_fp_store_sets_isr_w", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x40, 0x00, stfe(3, 40), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x10,
                 IA64_DISABLED_FP_VECTOR + 0x10)),
    ], {
        "ip": IA64_DISABLED_FP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r9": IA64_ISR_W | 2,
    }, entry=0x10)

test_disabled_fp_mixed_sets_reports_both = require_registers(
    "disabled_fp_mixed_sets_reports_both", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFL |
                         IA64_PSR_DFH)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x40, 0x0d, nop_m(), fmerge_ns(40, 8, 0), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x00, mov_m_cr_gr(9, 17),
         nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x10,
                 IA64_DISABLED_FP_VECTOR + 0x10)),
    ], {
        "ip": IA64_DISABLED_FP_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r9": 3 | (1 << IA64_ISR_EI_SHIFT),
    }, entry=0x10)

test_disabled_fp_skipped_check_not_reused = require_registers(
    "disabled_fp_skipped_check_not_reused", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, 0x01, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x01, setf_sig(40, 2, qp=1), nop_i(), nop_i()),
        (0x40, 0x01, setf_sig(41, 2), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x01, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x01,
         mov_m_cr_gr(9, 17), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x20,
                 IA64_DISABLED_FP_VECTOR + 0x20)),
    ], {"ip": IA64_DISABLED_FP_VECTOR + 0x20, "r8": 0x40, "r9": IA64_ISR_NI | 2,
        "exception": IA64_EXCP_NONE}, entry=0x10)


test_disabled_fp_psr_change_in_bundle = require_registers(
    "disabled_fp_psr_change_in_bundle", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, 0x01, setf_sig(40, 2), nop_i(), nop_i()),
        (0x30, 0x0d, mov_gr_psr_full(2), fmov(41, 40), nop_i()),
        (IA64_DISABLED_FP_VECTOR, 0x01, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x01,
         mov_m_cr_gr(9, 17), nop_i(), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x20,
                 IA64_DISABLED_FP_VECTOR + 0x20)),
    ], {"ip": IA64_DISABLED_FP_VECTOR + 0x20, "r8": 0x30,
        "r9": IA64_ISR_NI | 2 | (1 << IA64_ISR_EI_SHIFT),
        "exception": IA64_EXCP_NONE}, entry=0x10)


test_fp_writes_set_psr_mfl_mfh = require_registers(
    "fp_writes_set_psr_mfl_mfh", [
        (0x10, *movl_mlx(2, 0x1234)),
        (0x20, 0x00, setf_sig(8, 2), nop_i(), nop_i()),
        (0x30, 0x00, setf_sig(40, 2), nop_i(), nop_i()),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "psr": ExpectedBits(mask=IA64_PSR_MFL | IA64_PSR_MFH,
                            value=IA64_PSR_MFL | IA64_PSR_MFH),
        "f8": ExpectedFP(0x1234, 0x1003e),
        "f40": ExpectedFP(0x1234, 0x1003e),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_predicated_off_disabled_fp_does_not_fault = require_registers(
    "predicated_off_disabled_fp_does_not_fault", [
        (0x10, *movl_mlx(2, IA64_PSR_DFH)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, setf_sig(40, 2, qp=1), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "psr": IA64_PSR_DFH,
        "f40": ExpectedFP(0, 0),
        "ar_fpsr": DEFAULT_FPSR,
    }, entry=0x10)

test_chk_a_clr_f_ignores_psr_dfh = require_registers(
    "chk_a_clr_f_ignores_psr_dfh", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DFH)),
        (0x20, 0x01, nop_m(), cmp_eq_imm(5, 6, 0, 0), nop_i()),
        (0x30, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        # Floating data checks query an ALAT tag without reading the FR.
        (0x50, 0x00, chk_a_clr_f(40, 0x50, 0x80, qp=5), nop_i(),
         nop_i()),
        (0x60, 0x00, adds(5, 1, 0), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x80)),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
        # Turn a regressed Disabled FP fault into an immediate state mismatch.
        (IA64_DISABLED_FP_VECTOR, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        (IA64_DISABLED_FP_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DISABLED_FP_VECTOR + 0x10, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r4": 0,
        "r5": 0,
        "psr": ExpectedBits(mask=IA64_PSR_DFH, value=IA64_PSR_DFH),
    }, entry=0x10)

test_ldfp_requires_opposite_register_banks = require_exception(
    "ldfp_requires_opposite_register_banks", [
        (0x10, 0x08, ldfp8_postinc(2, 4, 3), nop_m(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_ldfp_bank_check_uses_rotated_physical_registers = require_registers(
    "ldfp_bank_check_uses_rotated_physical_registers", [
        (0x10, 0x00, addl(3, 0x200, 0), mov_i_imm_ar(66, 1),
         nop_i()),
        # Drain one epilog stage, rotating RRB.FR from 0 to 95.
        (0x20, 0x13, nop_m(), nop_b(), br_ctop_many(0x20, 0x20)),
        # Logical f2/f32 are both even, but physical f32 is odd at RRB.FR=95.
        (0x30, 0x00, ldfp8_postinc(2, 32, 3), nop_i(),
         nop_i()),
        (0x40, 0x09, getf_sig(4, 2), getf_sig(5, 32),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x200, 0x00, 0x0123456789, 0x01abcdef,
         0),
    ], {"ip": 0x50, "r3": 0x210, "r4": LDFP8_LOW,
        "r5": LDFP8_HIGH, "cfm_rrb_fr": 95,
        "exception": IA64_EXCP_NONE}, entry=0x10)

test_ldfp_rotated_physical_bank_conflict_illegal = require_exception(
    "ldfp_rotated_physical_bank_conflict_illegal", [
        (0x10, 0x00, addl(3, 0x200, 0), mov_i_imm_ar(66, 1),
         nop_i()),
        (0x20, 0x13, nop_m(), nop_b(), br_ctop_many(0x20, 0x20)),
        # Logical f2/f33 are opposite, but physical f33 is even at RRB.FR=95.
        (0x30, 0x00, ldfp8_postinc(2, 33, 3), nop_i(),
         nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x30, entry=0x10)

test_br_ctop_rotates_floating_registers = require_registers(
    "br_ctop_rotates_floating_registers", [
        (0x10, *movl_mlx(2, 0x12345678)),
        # LC=95 plus the one epilog stage executes exactly 96 rotations.
        # The complete rotating FR bank must return to its original mapping.
        (0x20, 0x01, setf_sig(32, 2), mov_lc_imm(95),
         mov_i_imm_ar(66, 1)),
        (0x30, 0x13, nop_m(), nop_b(), br_ctop_many(0x30, 0x30)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "f32": ExpectedFP(0x12345678, 0x1003e),
        "cfm_rrb_fr": 0,
    }, entry=0x10)

# fselect: f1 = (f3 AND f2) OR (f4 AND NOT f2)
# f2 (mask) = 0xFF00, f3 = 0x1234, f4 = 0x5678
# result = (0x1234 & 0xFF00) | (0x5678 & ~0xFF00)
#        = 0x1200 | 0x0078 = 0x1278
test_fselect_decode = require_registers("fselect_decode", [
    (0x10, 0x00, addl(24, 0xFF00 & 0x1FFFFF, 0),
     addl(25, 0x1234, 0), nop_i()),
    (0x20, 0x00, addl(26, 0x5678, 0), nop_i(),
     nop_i()),
    (0x30, 0x09, setf_sig(6, 24), setf_sig(7, 25),
     nop_i()),
    (0x40, 0x09, setf_sig(8, 26), nop_m(),
     nop_i()),
    (0x50, 0x0d, nop_m(), fselect(10, 6, 7, 8),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "f10": ExpectedFP(0x1278, 0x1003e),
    "ar_fpsr": DEFAULT_FPSR,
}, entry=0x10)

test_fselect_natval_propagates = require_registers(
    "fselect_natval_propagates", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x20, 0x08, ld8_fill_postinc(3, 6, 0), nop_i(),
         nop_i()),
        (0x30, 0x09, ldf8_s(7, 3), setf_sig(8, 0),
         nop_i()),
        (0x40, 0x0d, nop_m(), fselect(10, 8, 7, 8),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "f10": ExpectedFP(0, 0x1fffe, nat=True),
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    },
    entry=0x10)


def test_getf_d_stfd_across_blocks(qemu):
    for index, value in enumerate((1, 0x8000000000000000,
                                   0x7ff0000000000001,
                                   0xfff8000000000123)):
        require_registers(f"getf_d_stfd_across_blocks_{index}", [
            (0x10, *movl_mlx(2, value)),
            (0x20, *movl_mlx(3, 0x8000000000000000)),
            (0x30, 0x09, setf_d(6, 2), setf_sig(7, 3), nop_i()),
            (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x80)),
            (0x80, 0x09, getf_d(4, 6), getf_d(5, 7), nop_i()),
            (0x90, 0x01, addl(8, 0x200, 0), addl(9, 0x208, 0), nop_i()),
            (0xa0, 0x09, stfd(8, 6), stfd(9, 7), nop_i()),
            (0xb0, 0x09, ld8(10, 8), ld8(11, 9), nop_i()),
            (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        ], {"ip": 0xc0, "r4": value, "r10": value,
            "r5": 0x43e0000000000000, "r11": 0x43e0000000000000,
            "exception": IA64_EXCP_NONE}, entry=0x10)(qemu)


def _fp_representation_case(name, value, width, model):
    if width == 32:
        value &= 0xffffffff
        integer_store, fp_load = st4, ldfs
        fp_set, fp_get, fp_store, integer_load = setf_s, getf_s, stfs, ld4
        spill_to_binary = spill_to_binary32
    elif width == 64:
        value &= UINT64_MAX
        integer_store, fp_load = st8, ldfd
        fp_set, fp_get, fp_store, integer_load = setf_d, getf_d, stfd, ld8
        spill_to_binary = spill_to_binary64
    else:
        raise ValueError("FP representation width must be 32 or 64")

    low, high = model(value)
    memory_value = spill_to_binary(low, high)
    input_value = value | (0xa5a55a5a00000000 if width == 32 else 0)
    case = require_registers(name, [
        (0x10, *movl_mlx(2, input_value)),
        (0x20, 0x00, addl(3, 0x200, 0), addl(4, 0x210, 0), nop_i()),
        (0x30, 0x00, integer_store(3, 2), nop_i(), nop_i()),
        (0x40, 0x00, fp_load(6, 3), nop_i(), nop_i()),
        (0x50, 0x00, fp_set(7, 2), nop_i(), nop_i()),
        (0x60, 0x00, fp_get(5, 6), nop_i(), nop_i()),
        (0x70, 0x00, fp_store(4, 7), nop_i(), nop_i()),
        (0x80, 0x00, integer_load(8, 4), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
    ], {
        "ip": 0x90,
        "f6": ExpectedFP(low, high),
        "f7": ExpectedFP(low, high),
        "r5": memory_value,
        "r8": memory_value,
        "ar_fpsr": DEFAULT_FPSR,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)
    return case


FP_REPRESENTATION_CASES = {}
for _width, _vectors, _model in (
        (32, BINARY32_EDGE_VECTORS + deterministic_words(32, 12),
         binary32_to_spill),
        (64, BINARY64_EDGE_VECTORS + deterministic_words(64, 12),
         binary64_to_spill)):
    for _index, _value in enumerate(_vectors):
        _name = f"fpmodel_binary{_width}_{_index:02d}"
        FP_REPRESENTATION_CASES[_name] = _fp_representation_case(
            _name, _value, _width, _model)

test_fp_tracking_predicates_check_load_and_user_mask = require_registers(
    "fp_tracking_predicates_check_load_and_user_mask", [
        (0x10, *movl_mlx(2, 0x3ff0000000000000)),
        (0x20, 0x01, addl(3, 4, 0), addl(4, 0x400, 0), nop_i()),
        (0x30, 0x01, nop_m(), mov_gr_pr(3, 6), nop_i()),
        (0x40, 0x01, setf_sig(8, 2), nop_i(), nop_i()),
        (0x50, 0x01, setf_d(8, 2, qp=1), nop_i(), nop_i()),
        (0x60, 0x0d, nop_m(), fmov(9, 8), nop_i()),
        (0x70, 0x01, setf_d(8, 2, qp=2), nop_i(), nop_i()),
        (0x80, 0x0d, nop_m(), fmov(10, 8), nop_i()),
        (0x90, 0x01, ldf8_a(40, 4), nop_i(), nop_i()),
        (0xa0, 0x01, setf_d(40, 2), nop_i(), nop_i()),
        (0xb0, 0x01, ldf8_c_nc(40, 4), nop_i(), nop_i()),
        (0xc0, 0x0d, nop_m(), fmov(41, 40), nop_i()),
        (0xd0, 0x01, rum(IA64_PSR_MFL | IA64_PSR_MFH), nop_i(), nop_i()),
        (0xe0, 0x01, setf_sig(42, 2, qp=1), nop_i(), nop_i()),
        (0xf0, 0x01, mov_m_psr_gr(16), nop_i(), nop_i()),
        (0x100, 0x01, ldf8_c_nc(40, 4), nop_i(), nop_i()),
        (0x110, 0x01, mov_m_psr_gr(17), nop_i(), nop_i()),
        (0x120, 0x01, setf_sig(43, 2), nop_i(), nop_i()),
        (0x130, 0x01, mov_m_psr_gr(18), nop_i(), nop_i()),
        (0x140, 0x01, setf_sig(44, 2), nop_i(), nop_i()),
        (0x150, 0x01, rum(IA64_PSR_MFH, qp=2), nop_i(), nop_i()),
        (0x160, 0x01, setf_sig(45, 2), nop_i(), nop_i()),
        (0x170, 0x11, nop_m(), nop_i(), br_cond(0x170, 0x170)),
    ], {
        "ip": 0x170, "exception": IA64_EXCP_NONE,
        "f9": ExpectedFP(0x3ff0000000000000, 0x1003e),
        "f10": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
        "f40": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
        "f41": ExpectedFP(*binary64_to_spill(0x3ff0000000000000)),
        "f42": ExpectedFP(0, 0),
        "f43": ExpectedFP(0x3ff0000000000000, 0x1003e),
        "f44": ExpectedFP(0x3ff0000000000000, 0x1003e),
        "f45": ExpectedFP(0x3ff0000000000000, 0x1003e),
        "r16": ExpectedBits(mask=IA64_PSR_MFL | IA64_PSR_MFH, value=0),
        "r17": ExpectedBits(mask=IA64_PSR_MFL | IA64_PSR_MFH, value=0),
        "r18": ExpectedBits(mask=IA64_PSR_MFL | IA64_PSR_MFH,
                            value=IA64_PSR_MFH),
        "psr": ExpectedBits(mask=IA64_PSR_MFL | IA64_PSR_MFH,
                            value=IA64_PSR_MFH),
    }, entry=0x10)


GROUP = 'fp'
CASE_NAMES = (
    'fp_tracking_predicates_check_load_and_user_mask',

    'br_ctop_rotates_floating_registers',
    'chk_a_clr_f_ignores_psr_dfh',
    'chk_s_f_decode',
    'coreutils_hash_bucket_float_division',
    'data_big_endian_ldfe_stfe',
    'data_big_endian_stf_spill_ldf_fill',
    'disabled_fp_high_fault',
    'disabled_fp_skipped_check_not_reused',
    'disabled_fp_psr_change_in_bundle',
    'disabled_fp_load_sets_isr_r',
    'disabled_fp_low_fault',
    'disabled_fp_mixed_sets_reports_both',
    'disabled_fp_store_sets_isr_w',
    'fchkf_branches_on_uncommitted_flag',
    'fchkf_negative_target_uses_bit36',
    'fchkf_no_branch_when_flags_committed',
    'fchkf_positive_target_ignores_bit26',
    'fclass_m_decode',
    'fclass_m_ignored_bits_decode',
    'fclass_raw_unsupported_and_pseudozero',
    'fclass_same_pred_pred_false_noop',
    'fclass_unc_same_pred_pred_false_illegal',
    'fadd_qnan_suppresses_unnormal_d',
    'fadd_static_range_and_rounding',
    'fadd_unnormal_d_fault_rolls_back',
    'fp_binary_wre1_endpoint_delivery',
    'fp_binary_wre1_transports_17bit_range',
    'fcmp_invalid_fault_restores_predicates',
    'fcmp_natval_clears_predicates',
    'fcmp_p2_high_bit_not_fchkfs',
    'fcmp_qnan_quiet_relations',
    'fcmp_qnan_quiet_with_invalid_enabled',
    'fcmp_qnan_suppresses_unnormal_d',
    'fcmp_same_pred_illegal',
    'fcmp_status_field_decode',
    'fcmp_unnormal_d_fault_restores_predicates',
    'fcmp_unnormal_sets_d',
    'fcmp_wre1_orders_full_register_range',
    'fcvt_fx_signed_trunc',
    'fcvt_fxu_double_to_uint',
    'fcvt_fxu_preserves_sig_payload',
    'fcvt_fxu_rounds_sf0',
    'fcvt_inexact_trap_magnitude_rounddown_clears_fpa',
    'fcvt_inexact_trap_magnitude_roundup_sets_fpa',
    'fcvt_invalid_enabled_fault_discards_result',
    'fcvt_invalid_precedes_unnormal_d',
    'fcvt_masked_invalid_integer_indefinite',
    'fcvt_unnormal_d_fault_rolls_back',
    'fcvt_unnormal_sets_d',
    'fcvt_wre_invalid_suppresses_d_and_oi',
    'fcvt_wre_tiny_reports_i_not_u',
    'fcvt_wre_tiny_round_up_traps_i_with_fpa',
    'fcvt_xf_extreme_signed_round_trip',
    'fcvt_xf_ignores_prior_precision',
    'fcvt_xf_natval_propagates',
    'fcvt_xf_reads_register_significand',
    'fcvt_xf_signed_sig_to_float',
    'fma_d_s0_decode',
    'fma_dynamic_wre0_enabled_overflow_wraps_and_sets_fpa',
    'fma_invalid_precedes_unnormal_d',
    'fma_ldf_fill_binary64_subnormal_qnan_suppresses_d',
    'fma_qnan_precedes_zero_times_infinity',
    'fma_qnan_suppresses_unnormal_d',
    'fma_s_s0_high_f4_decode',
    'fma_preserves_extended_precision',
    'fma_static_binary32_fast_alias_fms_fnma',
    'fma_static_binary32_fast_ftz_underflow',
    'fma_static_binary32_fast_masked_overflow',
    'fma_static_binary32_fast_non_rn_fallback',
    'fma_static_binary32_fast_preround_underflow',
    'fma_static_binary32_nonexact_operand_fallback',
    'fma_static_binary64_fast_alias',
    'fma_static_binary64_fast_ftz_underflow',
    'fma_static_binary64_fast_masked_overflow',
    'fma_static_binary64_fast_non_rn_fallback',
    'fma_static_binary64_fast_preround_underflow',
    'fma_static_binary64_fast_special_fallback',
    'fma_static_enabled_exact_underflow_wraps',
    'fma_static_rpsp_midpoint_matches_pure_multiply',
    'fma_unsupported_precedes_qnan',
    'fmerge_forms_decode',
    'fmerge_natval_propagates',
    'fmerge_se_fixed_register_format_edges',
    'fmin_qnan_suppresses_unnormal_d',
    'fmin_unnormal_d_fault_rolls_back',
    'fminmax_scalar_decode',
    'fminmax_scalar_tie_uses_f3',
    'fminmax_unnormal_sets_d',
    'fminmax_wre1_selects_raw_operand',
    'fmov_preserves_integer_register_format',
    'fmpy_masked_invalid_negative_qnan',
    'fmpy_dynamic_wre0_enabled_exact_underflow_wraps',
    'fmpy_dynamic_wre0_extended_precision_rounds_once',
    'fmpy_pseudozero_infinity_is_infinity',
    'fmpy_s0_decode',
    'fmpy_s_s1_decode',
    'fmpy_static_enabled_overflow_wraps_and_sets_fpa',
    'fms_fnma_static_binary64_fast',
    'fms_static_product_add_cancellation_is_fused',
    'fms_s3_decode',
    'fnma_static_enabled_overflow_wraps_and_sets_fpa',
    'fnma_d_s1_decode',
    'fnmpy_s_s1_decode',
    'fnorm_enabled_overflow_reports_masked_inexact_in_isr',
    'fnorm_enabled_underflow_reports_masked_inexact_in_isr',
    'fnorm_ldf_fill_unnormal_sets_d',
    'fnorm_dynamic_wre0_precision_uses_15bit_range',
    'fnorm_normalizes_setf_sig_payload',
    'fnorm_static_enabled_exact_underflow_wraps',
    'fnorm_static_enabled_overflow_wraps_and_sets_fpa',
    'fnorm_static_masked_overflow_and_ftz',
    'fnorm_unnormal_d_fault_rolls_back',
    'fnorm_wre1_precision_and_range',
    'fp_alat_does_not_satisfy_gr_check_load',
    'fp_approx_pseudozero_returns_canonical_zero',
    'fp_approx_unsupported_masked_invalid',
    'fp_arithmetic_natval_propagates',
    'fp_binary_unnormal_sets_d',
    'fp_bitops_read_architected_significand',
    'fp_divzero_fault_discards_result',
    'fp_inexact_trap_commits_result',
    'fp_trap_precedes_concurrent_native_single_step',
    'fp_logical_and_swap_decode',
    'fp_logical_swap_natval_propagates',
    'fp_mix_sign_extend_decode',
    'fp_mix_sign_extend_natval_propagates',
    'fp_muladd_pseudozero_infinity_is_infinity',
    'fp_muladd_wre1_enabled_exact_underflow_wraps',
    'fp_muladd_wre1_enabled_overflow_wraps_and_sets_fpa',
    'fp_muladd_wre1_fused_cancellation',
    'fp_nan_register_priority',
    'fp_parallel_natval_propagates',
    'fp_parallel_reads_architected_significand',
    'fp_unary_natval_propagates',
    'fp_writes_set_psr_mfl_mfh',
    'fpabs_fpneg_decode',
    'fpack_decode',
    'fpcmp_nan_invalid_flags',
    'fpcmp_parallel_decode',
    'fpcmp_qnan_quiet_relations',
    'fpcmp_qnan_quiet_with_invalid_enabled',
    'fpcmp_simd_high_lane_fault_isr',
    'fpcvt_parallel_decode',
    'fpcvt_denormal_lanes_set_d',
    'fpcvt_high_lane_denormal_fault_rolls_back',
    'fpcvt_masked_invalid_lane_indefinite',
    'fpcvt_packed_faults_keep_lane_classes',
    'fpcvt_packed_inexact_trap_maps_high_fpa',
    'fpcvt_packed_inexact_trap_maps_low_fpa',
    'fpcvt_parallel_natval_propagates',
    'fpcvt_simd_high_lane_fault_isr',
    'fpcvt_simd_low_lane_fault_isr',
    'fpma_packed_enabled_ou_wraps_and_maps_lanes',
    'fpma_packed_ftz_sets_ui_and_traps_inexact',
    'fpma_packed_inexact_trap_maps_lane_fpa',
    'fpma_packed_overflow_reports_concurrent_masked_inexact',
    'fpma_parallel_decode',
    'fpma_parallel_natval_propagates',
    'fpma_simd_high_lane_fault_isr',
    'fpmerge_parallel_forms_decode',
    'fpminmax_nan_invalid_fault',
    'fpminmax_parallel_decode',
    'fpminmax_simd_high_lane_fault_isr',
    'fpmodel_binary32_00',
    'fpmodel_binary32_01',
    'fpmodel_binary32_02',
    'fpmodel_binary32_03',
    'fpmodel_binary32_04',
    'fpmodel_binary32_05',
    'fpmodel_binary32_06',
    'fpmodel_binary32_07',
    'fpmodel_binary32_08',
    'fpmodel_binary32_09',
    'fpmodel_binary32_10',
    'fpmodel_binary32_11',
    'fpmodel_binary32_12',
    'fpmodel_binary32_13',
    'fpmodel_binary32_14',
    'fpmodel_binary32_15',
    'fpmodel_binary32_16',
    'fpmodel_binary32_17',
    'fpmodel_binary32_18',
    'fpmodel_binary32_19',
    'fpmodel_binary32_20',
    'fpmodel_binary32_21',
    'fpmodel_binary32_22',
    'fpmodel_binary32_23',
    'fpmodel_binary64_00',
    'fpmodel_binary64_01',
    'fpmodel_binary64_02',
    'fpmodel_binary64_03',
    'fpmodel_binary64_04',
    'fpmodel_binary64_05',
    'fpmodel_binary64_06',
    'fpmodel_binary64_07',
    'fpmodel_binary64_08',
    'fpmodel_binary64_09',
    'fpmodel_binary64_10',
    'fpmodel_binary64_11',
    'fpmodel_binary64_12',
    'fpmodel_binary64_13',
    'fpmodel_binary64_14',
    'fpmodel_binary64_15',
    'fpmodel_binary64_16',
    'fpmodel_binary64_17',
    'fpmodel_binary64_18',
    'fpmodel_binary64_19',
    'fpmodel_binary64_20',
    'fpmodel_binary64_21',
    'fpmodel_binary64_22',
    'fpmodel_binary64_23',
    'fpms_fpnma_qnan_preserves_sign',
    'fpms_fpnma_snan_quiets_without_sign_flip',
    'fprcpa_decode',
    'fprcpa_denormal_denominator_returns_quotient',
    'fprcpa_denormal_overflow_does_not_leak_oi',
    'fprcpa_simd_high_lane_fault_isr',
    'fprcpa_simd_low_lane_fault_isr',
    'fprsqrta_decode',
    'fprsqrta_denormal_does_not_leak_inexact',
    'fprsqrta_invalidates_known_predicate',
    'fprsqrta_simd_high_lane_fault_isr',
    'fpsr_status_field_controls',
    'fpsr_td_suppresses_fp_fault',
    'f_reserved_cell_respects_qualifying_predicate',
    'fand_f1_illegal_operation',
    'fma_f0_illegal_operation',
    'fp_fixed_target_predicated_off_is_nop',
    'frcpa_double_normal_reciprocal',
    'frcpa_natval_propagates',
    'frcpa_p2_high_bits_decode',
    'frcpa_pred_false_clears',
    'frcpa_setf_sig_high_integer_remainder',
    'frcpa_special_quotient',
    'frcpa_swa_fault_discards_result',
    'frcpa_pseudozero_z_precedes_d',
    'frcpa_qnan_suppresses_unnormal_d',
    'frcpa_swa_precedes_unnormal_d',
    'frcpa_unsupported_invalid_enabled_rolls_back',
    'frcpa_wre1_special_responses_use_raw_types',
    'frcpa_wre1_uses_raw_exponents',
    'frsqrta_decode',
    'frsqrta_negative_unnormal_v_precedes_d',
    'frsqrta_pred_false_clears',
    'frsqrta_pseudozero_d_fault_rolls_back',
    'frsqrta_special_returns_operand',
    'frsqrta_swa_fault_discards_result',
    'frsqrta_swa_precedes_unnormal_d',
    'frsqrta_wre1_negative_normal_uses_raw_type',
    'frsqrta_wre1_uses_raw_exponents',
    'fselect_decode',
    'fselect_natval_propagates',
    'fsetc_fclrf_ignored_bit36_decode',
    'fsetc_pc1_reserved_field_fault',
    'fsetc_sf0_td_reserved_field_fault',
    'fsub_d_s0_decode',
    'fsub_static_enabled_exact_underflow_wraps',
    'getf_exact_register_format_translation',
    'getf_exp_after_fnorm_sig',
    'getf_natval_sets_gr_nat',
    'getf_sig_ignored_bits_decode',
    'invala_e_fp_invalidates_selected_register',
    'ldf8_a_chk_a_f_hit',
    'ldfe_a_alat_tracks_ten_byte_operand',
    'ldf8_a_uc_zeroes_target_and_skips_alat',
    'ldf8_c_nc_hit_consumes_nat_base',
    'ldf8_c_nc_hit_preserves_target',
    'ldf8_c_nc_uc_miss_does_not_allocate_alat',
    'ldf8_decode',
    'ldf8_f1_illegal_operation',
    'ldf8_loads_integer_register_format',
    'ldf8_s_chk_s_f_defers_nat_base',
    'ldf_fill_postinc_decode',
    'ldfe_stfe_preserves_extended_payload',
    'ldfd_loads_double_memory_format',
    'ldfp8_postinc_decode',
    'ldfp_bank_check_uses_rotated_physical_registers',
    'ldfp_requires_opposite_register_banks',
    'ldfp_rotated_physical_bank_conflict_illegal',
    'ldfps_expands_both_single_values',
    'ldfs_expands_single_memory_format',
    'ldfs_preserves_single_nan_payload',
    'nop_f_decode',
    'parallel_muladd_f0_signed_zero_is_pure_multiply',
    'predicated_off_disabled_fp_does_not_fault',
    'scalar_muladd_f0_signed_zero_is_pure_multiply',
    'setf_d_f1_illegal_operation',
    'setf_exp_decode',
    'setf_nat_source_sets_fr_natval',
    'setf_sig_direct_scalar_operand',
    'setf_sig_ignored_bits_decode',
    'stf8_natval_consumption',
    'stf8_postinc_imm9_decode',
    'stf8_postinc_stores_setf_sig',
    'stf8_stfe_convert_register_format',
    'stf_spill_ldf_fill_preserves_sig',
    'stf_spill_postinc_decode',
    'stf_spill_preserves_natval',
    'stfd_natval_consumption',
    'getf_d_stfd_across_blocks',
    'stfe_natval_consumption',
    'stfe_preserves_padding_alat_entry',
    'stfe_stores_extended_float',
    'stfs_natval_consumption',
    'stfs_stfd_convert_register_format',
    'umodsi3_hash_remainder',
    'fp_s1_pred_false_decode',
    'frcpa_capacity_calc',
    'frcpa_integer_division',
    'xma_fcvt_xf_read_architected_significand',
    'xma_h_decode',
    'xma_hu_decode',
    'xma_natval_propagates',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals(),
                   extras=FP_REPRESENTATION_CASES)
