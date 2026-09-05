"""Core integer, branch, system, and decode microprograms."""

from __future__ import annotations

from .case import bind_cases
from .encoding import (
    HIGH_TR_BASE,
    IA64_EXCP_BREAK,
    IA64_EXCP_ILLEGAL,
    IA64_EXCP_NONE,
    IA64_EXCP_PRIVILEGED_OP,
    IA64_EXCP_RESERVED_REG_FIELD,
    IA64_EXCP_VIRTUALIZATION,
    IA64_GENERAL_VECTOR,
    IA64_ISR_EI_SHIFT,
    IA64_PSR_BN,
    IA64_PSR_CPL3,
    IA64_PSR_DI,
    IA64_PSR_I,
    IA64_PSR_IC,
    IA64_PSR_IS,
    IA64_PSR_SP,
    IA64_PSR_UP,
    UINT64_MAX,
    _strcpy_pipeline_data,
    add,
    addl,
    addp4,
    addp4_imm,
    adds,
    alloc,
    alloc_m,
    andcm_imm,
    bitfield,
    br_call,
    br_call_indirect,
    br_cloop,
    br_cond,
    br_ctop_few,
    br_ctop_many,
    br_indirect,
    cover_b,
    br_ret,
    br_wexit,
    br_wtop,
    break_b,
    brl_call_mlx,
    brl_cond_mlx,
    brp_loop_imp,
    brp_sptk,
    bsw0,
    bsw1,
    chk_s_i,
    clrrrb_b,
    clrrrb_pr_b,
    clz,
    cmp4_eq_and,
    cmp4_eq_and_imm,
    cmp4_eq_imm,
    cmp4_eq_or,
    cmp4_eq_or_imm,
    cmp4_eq_unc_imm,
    cmp4_ge_or_andcm,
    cmp4_lt_unc,
    cmp4_ltu_imm,
    cmp4_ltu_unc,
    cmp4_ltu_unc_imm,
    cmp4_ne_and_imm,
    cmp4_ne_or,
    cmp4_ne_or_andcm,
    cmp4_ne_or_imm,
    cmp_eq_and,
    cmp_eq_and_imm,
    cmp_eq_imm,
    cmp_eq_or_imm,
    cmp_ge_and,
    cmp_ge_or,
    cmp_ge_or_andcm_issue_raw,
    cmp_ge_or_issue_raw,
    cmp_gt_and,
    cmp_le_or,
    cmp_lt_unc_imm,
    cmp_ltu_imm,
    cmp_ltu_unc,
    cmp_ne_and,
    cmp_ne_and_imm,
    cmp_ne_or_andcm,
    cmp_ne_or_andcm_imm,
    cmp_ne_or_imm,
    chk_a_nc_m,
    czx1_l,
    czx1_r,
    czx2_l,
    czx2_r,
    dep,
    depz_imm,
    depz_reg,
    dtr_setup_bundles,
    epc_b,
    extr,
    extr_u,
    fc_i,
    flushrs_enc,
    fwb,
    hint_i,
    hint_m,
    hint_x_mlx,
    ia32_bundle,
    ia32_environment_bundles,
    IA32_TEST_CSD,
    ip_relative_branch_btype,
    ld1,
    ld4_postinc,
    ld8,
    ld8_a,
    ld8_fill_postinc,
    ld8_postinc,
    ld8_s_postinc,
    loadrs_enc,
    mf,
    mix1_l,
    mix1_r,
    mix2_l,
    mix2_r,
    mix4_l,
    mix4_r,
    mov_ar,
    mov_ar_lc,
    mov_b_gr,
    mov_br_gr,
    mov_cpuid,
    mov_dahr_read,
    mov_dahr_write,
    mov_dbr_indexed_read,
    mov_dbr_indexed_write,
    mov_gr_b,
    mov_gr_pr,
    mov_gr_psr_full,
    mov_grpmc_indexed,
    mov_grpmd_indexed,
    mov_i_ar_gr,
    mov_i_imm_ar,
    mov_ibr_indexed_read,
    mov_ibr_indexed_write,
    mov_ip,
    mov_lc_gr,
    mov_lc_imm,
    mov_m_ar_gr,
    mov_m_gr_ar,
    mov_m_cr_gr,
    mov_m_gr_cr,
    mov_m_gr_psr_um,
    mov_m_gr_psrl,
    mov_m_imm_ar,
    mov_m_psr_gr,
    mov_m_psr_um_gr,
    mov_msr_read,
    mov_msr_write,
    mov_pmcgr_indexed,
    mov_pmdgr_indexed,
    mov_pr_gr,
    mov_pr_rot_imm,
    movl_mlx,
    mpy4,
    mpyshl4,
    mux1,
    mux1_rev,
    mux2,
    nop_b,
    nop_i,
    nop_m,
    nop_x,
    nop_x_mlx,
    op,
    pack2_sss,
    pack2_uss,
    pack4_sss,
    padd1,
    pavg,
    pavgsub,
    pcmp1_eq,
    pmax1_u,
    pmax2,
    pmin1_u,
    pmin2,
    pmpy2,
    pmpyshr2,
    popcnt,
    psad1,
    pshl2,
    pshl2_fixed,
    pshl4,
    pshl4_fixed,
    pshladd2,
    pshr2,
    pshr4,
    pshradd2,
    psub1_uuu,
    raw_bundle,
    require_exception,
    require_registers,
    reserved_a1_x4_5_x2b_1,
    rfi_to_gr,
    rsm,
    rum,
    shl_var,
    shladd,
    shladdp4,
    shr_u_imm,
    shr_u_var,
    shr_var,
    shrp_imm,
    srlz_d,
    srlz_i,
    ssm,
    st1_postinc,
    st4_postinc,
    st8_postinc,
    sub_reg,
    sum_um,
    sxt1,
    sync_i,
    tbit_z,
    tbit_z_unc,
    tf_nz_and,
    tf_nz_or_andcm,
    tf_z,
    tf_z_unc,
    unpack1_h,
    unpack1_l,
    unpack2_h,
    unpack2_l,
    unpack4_h,
    unpack4_l,
    vmsw0,
    vmsw1,
)


test_br_call_ret_preserves_ec = require_registers(
    "br_call_ret_preserves_ec", [
        (0x10, 0x00, mov_m_imm_ar(66, 25), nop_i(),
         nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_call(6, 0x20, 0x60)),
        (0x30, 0x00, mov_m_ar_gr(4, 66), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (0x60, 0x00, mov_m_imm_ar(66, 1), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_ret(6)),
    ], {"ip": 0x40, "r4": 25}, entry=0x10)

test_popcnt_decode = require_registers("popcnt_decode", [
    (0x10, *movl_mlx(3, 0xf0f0f0f0f0f0f0f0)),
    (0x20, 0x00, nop_m(), popcnt(4, 3, ignored=1),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 32, "exception": IA64_EXCP_NONE}, entry=0x10)

test_clz_decode = require_registers("clz_decode", [
    (0x10, 0x00, nop_m(), addl(31, 4, 0), adds(4, 0x55, 0)),
    (0x20, 0x00, mov_cpuid(29, 31), clz(4, 0, qp=1, ignored=1), nop_i()),
    (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
], {
    "ip": 0x30,
    "r4": 0x55,
    "r29": 0,
    "exception": IA64_EXCP_NONE,
}, entry=0x10, cpu="merced")

test_clz_unsupported_true_illegal = require_exception(
    "clz_unsupported_true_illegal", [
        (0x10, 0x00, nop_m(), clz(4, 0), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="merced")

test_pmpy2_decode = require_registers("pmpy2_decode", [
    (0x10, *movl_mlx(29, 0xffff800000020003)),
    (0x20, *movl_mlx(31, 0x0002000300040005)),
    (0x30, 0x02, nop_m(), pmpy2(4, 29, 31),
     nop_i()),
    (0x40, 0x02, nop_m(), pmpy2(5, 29, 31, right=True),
     nop_i()),
    (0x50, 0x02, nop_m(), pmpy2(6, 29, 31, right=True, ignored=1),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r4": 0xfffffffe00000008,
    "r5": 0xfffe80000000000f,
    "r6": 0xfffe80000000000f,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_pmpy2_size_selector_predicated_off_is_nop = require_registers(
    "pmpy2_size_selector_predicated_off_is_nop", [
        (0x10, 0x00, adds(4, 0x55, 0),
         pmpy2(4, 29, 31, qp=1) | bitfield(1, 36, 1), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "r4": 0x55,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_pmpy2_size_selector_true_illegal = require_exception(
    "pmpy2_size_selector_true_illegal", [
        (0x10, 0x00, nop_m(),
         pmpy2(4, 29, 31) | bitfield(1, 36, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_mix_decode = require_registers("mix_decode", [
    (0x10, *movl_mlx(8, 0x1122334455667788)),
    (0x20, *movl_mlx(9, 0xaabbccddeeff0011)),
    (0x30, 0x02, nop_m(), mix1_l(2, 8, 9),
     mix1_r(3, 8, 9, ignored=1)),
    (0x40, 0x02, nop_m(), mix2_l(4, 8, 9),
     mix2_r(5, 8, 9)),
    (0x50, 0x02, nop_m(), mix4_l(6, 8, 9),
     mix4_r(7, 8, 9)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r2": 0x11aa33cc55ee7700,
    "r3": 0x22bb44dd66ff8811,
    "r4": 0x1122aabb5566eeff,
    "r5": 0x3344ccdd77880011,
    "r6": 0x11223344aabbccdd,
    "r7": 0x55667788eeff0011,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_unpack2_l_decode = require_registers("unpack2_l_decode", [
    (0x10, *movl_mlx(17, 0x1122334455667788)),
    (0x20, *movl_mlx(18, 0xaabbccddeeff0011)),
    (0x30, 0x02, nop_m(), unpack1_l(16, 17, 18),
     unpack1_h(19, 17, 18)),
    (0x40, 0x02, nop_m(), unpack2_l(20, 17, 18),
     unpack2_h(21, 17, 18)),
    (0x50, 0x02, nop_m(), unpack4_l(22, 17, 18),
     unpack4_h(23, 17, 18)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r16": 0x55ee66ff77008811,
    "r19": 0x11aa22bb33cc44dd,
    "r20": 0x5566eeff77880011,
    "r21": 0x1122aabb3344ccdd,
    "r22": 0x55667788eeff0011,
    "r23": 0x11223344aabbccdd,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_pmpyshr2_decode = require_registers("pmpyshr2_decode", [
    (0x10, *movl_mlx(29, 0xffff800000020003)),
    (0x20, *movl_mlx(31, 0x0002000300040005)),
    (0x30, 0x02, nop_m(), pmpyshr2(4, 29, 31, 16),
     nop_i()),
    (0x40, 0x02, nop_m(),
     pmpyshr2(5, 29, 31, 16, signed=True, ignored=1),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {
    "ip": 0x50,
    "r4": 0x0001000100000000,
    "r5": 0xfffffffe00000000,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_pmpyshr2_size_selector_predicated_off_is_nop = require_registers(
    "pmpyshr2_size_selector_predicated_off_is_nop", [
        (0x10, 0x00, adds(4, 0x55, 0),
         pmpyshr2(4, 29, 31, 16, qp=1) | bitfield(1, 36, 1), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "r4": 0x55,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_pmpyshr2_size_selector_true_illegal = require_exception(
    "pmpyshr2_size_selector_true_illegal", [
        (0x10, 0x00, nop_m(),
         pmpyshr2(4, 29, 31, 16) | bitfield(1, 36, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_andcm_imm_negative_mask_round_trip = require_registers(
    "andcm_imm_negative_mask_round_trip", [
        (0x10, *movl_mlx(25, 1 << 9)),
        (0x20, 0x00, andcm_imm(18, -1, 25), nop_i(),
         nop_i()),
        (0x30, 0x00, andcm_imm(30, -1, 18), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r18": 0xfffffffffffffdff,
        "r30": 0x200,
    }, entry=0x10)

test_hint_m_decode = require_registers("hint_m_decode", [
    (0x10, 0x00, hint_m(0x145678), adds(31, 0x66, 0),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "exception": IA64_EXCP_NONE, "r31": 0x66}, entry=0x10)

test_hint_i_decode = require_registers("hint_i_decode", [
    (0x10, 0x00, nop_m(), hint_i(0x145678),
     adds(31, 0x66, 0)),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "exception": IA64_EXCP_NONE, "r31": 0x66}, entry=0x10)

test_cmp_lt_unc_imm_decode = require_registers("cmp_lt_unc_imm_decode", [
    (0x10, 0x00, adds(3, 20, 0), cmp_lt_unc_imm(7, 8, 15, 3),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "pr_mask": 1 | (1 << 7)}, entry=0x10)

test_cmp4_lt_unc_decode = require_registers("cmp4_lt_unc_decode", [
    (0x10, 0x00, adds(3, -1, 0), cmp4_lt_unc(7, 8, 3, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=7),
     adds(5, 1, 0, qp=8)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 1, "r5": 0}, entry=0x10)

test_cmp_ltu_unc_p0_decode = require_registers("cmp_ltu_unc_p0_decode", [
    (0x10, 0x00, adds(16, 3, 0), cmp_ltu_unc(13, 14, 0, 16),
     nop_i()),
    (0x20, 0x00, cmp_ltu_unc(0, 13, 0, 16), nop_i(),
     adds(4, 1, 0, qp=13)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0}, entry=0x10)

test_cmp4_ltu_unc_p0_register_decode = require_registers(
    "cmp4_ltu_unc_p0_register_decode",
    [
        (0x10, 0x00, alloc(32, 8, 0, 0, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, adds(35, 4, 0), cmp4_ltu_unc(0, 15, 0, 35),
         nop_i()),
        (0x30, 0x00, nop_m(), adds(4, 1, 0, qp=15),
         nop_i()),
        (0x40, 0x00, adds(35, 0, 0), cmp4_ltu_unc(0, 15, 0, 35),
         nop_i()),
        (0x50, 0x00, nop_m(), adds(5, 1, 0, qp=15),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ],
    {"ip": 0x60, "r4": 0, "r5": 1},
    entry=0x10,
)

test_cmp4_eq_unc_imm_p0_decode = require_registers("cmp4_eq_unc_imm_p0_decode", [
    (0x10, 0x00, adds(31, 1, 0), cmp4_eq_unc_imm(0, 15, 1, 31),
     nop_i()),
    (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=15),
     adds(5, 1, 0)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0, "r5": 1}, entry=0x10)

test_cmp4_eq_imm_decode = require_registers("cmp4_eq_imm_decode", [
    (0x10, 0x00, adds(3, -128, 0), cmp4_eq_imm(8, 7, -128, 3),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20, qp=8)),
], {"ip": 0x20, "r3": 0xffffffffffffff80}, entry=0x10)

test_cmp_ltu_imm_negative_decode = require_registers("cmp_ltu_imm_negative_decode", [
    (0x10, 0x00, adds(8, 7, 0), cmp_ltu_imm(6, 7, -5, 8),
     nop_i()),
    (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=6),
     adds(5, 1, 0, qp=7)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0, "r5": 1}, entry=0x10)

test_cmp4_ltu_imm_negative_decode = require_registers("cmp4_ltu_imm_negative_decode", [
    (0x10, 0x00, adds(8, 7, 0), cmp4_ltu_imm(6, 7, -5, 8),
     nop_i()),
    (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=6),
     adds(5, 1, 0, qp=7)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0, "r5": 1}, entry=0x10)

test_cmp_unc_pred_false_clears = require_registers("cmp_unc_pred_false_clears", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(15, 0, 0, 16),
     nop_i()),
    (0x20, 0x00, cmp_ltu_unc(15, 0, 0, 16, qp=6), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x50, qp=15)),
    (0x40, 0x00, adds(4, 1, 0), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": 1}, entry=0x10)

test_cmp4_ltu_unc_imm_pred_false_clears = require_registers(
    "cmp4_ltu_unc_imm_pred_false_clears",
    [
        (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(15, 0, 0, 16),
         nop_i()),
        (0x20, 0x00, cmp4_ltu_unc_imm(0, 15, 79, 41, qp=6), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x50, qp=15)),
        (0x40, 0x00, adds(4, 1, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ],
    {"ip": 0x50, "r4": 1},
    entry=0x10,
)

test_cmp_unc_self_predicate_reads_old_qp = require_registers(
    "cmp_unc_self_predicate_reads_old_qp",
    [
        (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(12, 0, 0, 16),
         nop_i()),
        (0x20, 0x00, adds(17, 1, 0), adds(18, 2, 0),
         cmp_ltu_unc(12, 0, 17, 18, qp=12)),
        (0x30, 0x00, adds(4, 1, 0, qp=12), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ],
    {"ip": 0x40, "r4": 1},
    entry=0x10,
)

test_tbit_unc_pred_false_clears = require_registers("tbit_unc_pred_false_clears", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(11, 0, 0, 16),
     cmp_ltu_unc(12, 0, 0, 16)),
    (0x20, 0x00, nop_m(), tbit_z_unc(11, 12, 21, 3, qp=6),
     nop_i()),
    (0x30, 0x00, adds(17, 1, 0, qp=11), adds(18, 1, 0, qp=12),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r17": 0, "r18": 0}, entry=0x10)

test_cmp_same_pred_illegal = require_exception(
    "cmp_same_pred_illegal",
    [(0x10, 0x00, cmp4_eq_imm(6, 6, 0, 0), nop_i(), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_cmp_unc_same_pred_pred_false_illegal = require_exception(
    "cmp_unc_same_pred_pred_false_illegal",
    [(0x10, 0x00, cmp4_eq_unc_imm(6, 6, 0, 0, qp=7), nop_i(),
      nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_tbit_same_pred_illegal = require_exception(
    "tbit_same_pred_illegal",
    [(0x10, 0x00, nop_m(), tbit_z(6, 6, 0, 0), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_tf_feature_predicate_updates = require_registers(
    "tf_feature_predicate_updates", [
        (0x10, 0x00, adds(16, 1, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, nop_m(), tf_z(6, 7, 35),
         tf_nz_or_andcm(8, 9, 32)),
        (0x30, 0x00, nop_m(), cmp_ltu_unc(10, 0, 0, 16),
         cmp_ltu_unc(11, 0, 0, 16)),
        (0x40, 0x00, nop_m(), tf_nz_and(10, 11, 35),
         nop_i()),
        (0x50, 0x00, adds(4, 1, 0, qp=6), adds(5, 1, 0, qp=7),
         adds(8, 1, 0, qp=8)),
        (0x60, 0x00, adds(9, 1, 0, qp=9), adds(10, 1, 0, qp=10),
         adds(11, 1, 0, qp=11)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r4": 1,
        "r5": 0,
        "r8": 0,
        "r9": 0,
        "r10": 0,
        "r11": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_tf_upper_cpuid_feature_bits = require_registers(
    "tf_upper_cpuid_feature_bits", [
        (0x10, 0x00, nop_m(), addl(31, 4, 0),
         nop_i()),
        (0x20, 0x00, mov_cpuid(29, 31), tf_nz_or_andcm(6, 7, 33),
         nop_i()),
        (0x30, 0x00, nop_m(), tf_z(8, 9, 34),
         nop_i()),
        (0x40, 0x00, adds(4, 1, 0, qp=6), adds(5, 1, 0, qp=7),
         adds(6, 1, 0, qp=8)),
        (0x50, 0x00, nop_m(), adds(7, 1, 0, qp=9),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r4": 0,
        "r5": 0,
        "r6": 1,
        "r7": 0,
        "r29": 0x0000000000000005,
    }, entry=0x10)

test_tf_same_pred_illegal = require_exception(
    "tf_same_pred_illegal",
    [(0x10, 0x00, nop_m(), tf_z(6, 6, 34), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_tf_unc_same_pred_pred_false_illegal = require_exception(
    "tf_unc_same_pred_pred_false_illegal",
    [(0x10, 0x00, nop_m(), tf_z_unc(6, 6, 34, qp=7), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_tf_constant_zero_predicated_off_is_nop = require_registers(
    "tf_constant_zero_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(),
         tf_z(6, 7, 32, qp=1) | bitfield(3, 20, 7), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_tf_constant_zero_true_illegal = require_exception(
    "tf_constant_zero_true_illegal", [
        (0x10, 0x00, nop_m(),
         tf_z(6, 7, 32) | bitfield(3, 20, 7), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_cmp_eq_and_decode = require_registers("cmp_eq_and_decode", [
    (0x10, 0x00, adds(16, 1, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, cmp_ltu_unc(7, 0, 0, 16), nop_i(),
     nop_i()),
    (0x30, 0x00, adds(17, 5, 0), adds(18, 5, 0),
     nop_i()),
    (0x40, 0x00, cmp_eq_and(7, 0, 17, 18), nop_i(),
     nop_i()),
    (0x50, 0x00, adds(4, 1, 0, qp=7), adds(18, 6, 0),
     nop_i()),
    (0x60, 0x00, cmp_eq_and(7, 0, 17, 18), nop_i(),
     nop_i()),
    (0x70, 0x10, adds(5, 1, 0, qp=7), nop_i(),
     br_cond(0x70, 0x80)),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {"ip": 0x80, "r4": 1, "r5": 0}, entry=0x10)

test_cmp_ne_and_parallel_semantics = require_registers(
    "cmp_ne_and_parallel_semantics", [
        (0x10, 0x00, adds(16, 5, 0), adds(17, 6, 0),
         nop_i()),
        (0x20, 0x00, nop_m(),
         mov_pr_rot_imm((1 << 16) | (1 << 17) |
                        (1 << 18) | (1 << 19)),
         nop_i()),
        # For the AND compare type, a true relation leaves both targets
        # unchanged while a false relation clears both (SDM Vol. 1,
        # Table 4-9).  Use unequal and equal sources in parallel so an
        # accidental cmp.eq.and decode produces the exact opposite state.
        (0x30, 0x00, cmp_ne_and(16, 17, 16, 17),
         cmp_ne_and(18, 19, 16, 16), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "pr_mask": 1 | (1 << 16) | (1 << 17),
    }, entry=0x10)

test_compare_update_decode = require_registers(
    "compare_update_decode", [
        (0x10, *movl_mlx(10, 0x100000005)),
        (0x20, *movl_mlx(9, 0x200000005)),
        (0x30, 0x00, adds(16, 1, 0), adds(17, -1, 0),
         nop_i()),
        (0x40, 0x00, cmp_ltu_unc(7, 0, 0, 16),
         cmp_ltu_unc(6, 0, 0, 16), nop_i()),
        (0x50, 0x00, nop_m(), cmp4_eq_and(7, 0, 10, 9),
         cmp_gt_and(6, 0, 17)),
        (0x60, 0x00, nop_m(), cmp_le_or(13, 0, 16),
         adds(4, 1, 0, qp=7)),
        (0x70, 0x02, nop_m(), adds(5, 1, 0, qp=6),
         adds(6, 1, 0, qp=13)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r4": 1, "r5": 1, "r6": 1}, entry=0x10)

test_cmp4_ge_or_andcm_decode = require_registers("cmp4_ge_or_andcm_decode", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(14, 13, 0, 16),
     nop_i()),
    (0x20, 0x00, adds(29, -1, 0), cmp4_ge_or_andcm(13, 14, 29),
     nop_i()),
    (0x30, 0x02, nop_m(), adds(4, 1, 0, qp=13),
     adds(5, 1, 0, qp=14)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 1, "r5": 0}, entry=0x10)

test_cmp_ne_or_andcm_decode = require_registers("cmp_ne_or_andcm_decode", [
    (0x10, 0x00, addl(26, 0x895, 0), cmp4_eq_unc_imm(0, 13, 1, 0),
     nop_i()),
    (0x20, 0x00, cmp_ne_or_andcm(0, 13, 0, 26), nop_i(),
     nop_i()),
    (0x30, 0x00, adds(4, 1, 0, qp=13), cmp4_eq_unc_imm(0, 14, 1, 0),
     nop_i()),
    (0x40, 0x00, cmp_ne_or_andcm(0, 14, 0, 0), nop_i(),
     nop_i()),
    (0x50, 0x10, adds(5, 1, 0, qp=14), nop_i(),
     br_cond(0x50, 0x60)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {"ip": 0x60, "r4": 0, "r5": 1}, entry=0x10)

test_cmp_ne_or_andcm_imm_negative_decode = require_registers(
    "cmp_ne_or_andcm_imm_negative_decode", [
        (0x10, 0x00, adds(23, 0, 0), cmp_ne_or_andcm_imm(13, 0, -1, 23),
         nop_i()),
        (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=13),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 1}, entry=0x10)

test_cmp4_ne_or_andcm_decode = require_registers("cmp4_ne_or_andcm_decode", [
    (0x10, 0x00, addl(26, 0x895, 0), cmp4_eq_unc_imm(0, 13, 1, 0),
     nop_i()),
    (0x20, 0x00, cmp4_ne_or_andcm(0, 13, 0, 26), nop_i(),
     nop_i()),
    (0x30, 0x00, adds(4, 1, 0, qp=13), cmp4_eq_unc_imm(0, 14, 1, 0),
     nop_i()),
    (0x40, 0x00, cmp4_ne_or_andcm(0, 14, 0, 0), nop_i(),
     nop_i()),
    (0x50, 0x10, adds(5, 1, 0, qp=14), nop_i(),
     br_cond(0x50, 0x60)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {"ip": 0x60, "r4": 0, "r5": 1}, entry=0x10)

test_cmp4_eq_ne_or_decode = require_registers("cmp4_eq_ne_or_decode", [
    (0x10, *movl_mlx(16, 0x100000005)),
    (0x20, *movl_mlx(17, 0x200000005)),
    (0x30, *movl_mlx(18, 0x200000006)),
    (0x40, 0x00, cmp4_eq_or(13, 0, 16, 17),
     cmp4_ne_or(14, 0, 16, 17), nop_i()),
    (0x50, 0x00, cmp4_ne_or(15, 0, 16, 18),
     adds(4, 1, 0, qp=13), adds(5, 1, 0, qp=14)),
    (0x60, 0x00, adds(6, 1, 0, qp=15), nop_i(),
     nop_i()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
], {"ip": 0x70, "r4": 1, "r5": 0, "r6": 1}, entry=0x10)

test_cmp_imm_update_decode = require_registers(
    "cmp_imm_update_decode", [
        (0x10, *movl_mlx(9, 0x100000004)),
        (0x20, 0x00, adds(8, 4, 0), adds(16, 1, 0),
         mov_pr_rot_imm(0x00ff0000)),
        (0x30, 0x00, cmp_eq_and_imm(16, 17, 4, 8),
         cmp_ne_and_imm(18, 19, 4, 8), cmp4_eq_and_imm(20, 21, 4, 9)),
        (0x40, 0x00, cmp4_ne_and_imm(22, 23, -1, 9),
         cmp_eq_or_imm(24, 25, -1, 8), cmp_ne_or_imm(26, 27, -1, 8)),
        (0x50, 0x00, cmp4_eq_or_imm(28, 29, 4, 9),
         cmp4_ne_or_imm(30, 31, -1, 9), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "pr_mask": (1 | (1 << 16) | (1 << 17) |
                    (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23) |
                    (1 << 26) | (1 << 27) | (1 << 28) | (1 << 29) |
                    (1 << 30) | (1 << 31)),
    }, entry=0x10)

test_mlx_false_predicate_long_nop_decode = require_registers(
    "mlx_false_predicate_long_nop_decode", [
        (0x10, 0x04, nop_m(), 1, nop_x(qp=1)),
        (0x20, 0x10, nop_m(), nop_i(),
         br_cond(0x20, 0x20)),
    ], {"ip": 0x20, "exception": IA64_EXCP_NONE}, entry=0x10)

test_mlx_long_nop_x_imm_decode = require_registers(
    "mlx_long_nop_x_imm_decode", [
        (0x10, *nop_x_mlx(0x30200000)),
        (0x20, 0x00, adds(4, 0x5a, 0), nop_i(),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 0x5a, "exception": IA64_EXCP_NONE}, entry=0x10)

test_dep_decode = require_registers("dep_decode", [
    (0x10, 0x00, addl(2, 0xab, 0), addl(3, 0x1234, 0),
     nop_i()),
    (0x20, 0x01, nop_m(), dep(4, 2, 3, 8, 8),
     dep(5, 2, 3, 4, 12)),
    # cpos=14 and len=2 encode bits 35:27 as 0xe1.  This remains a valid
    # I15 dep; the same bits identify getf.sig only when used in an M-unit.
    (0x30, 0x01, nop_m(), dep(6, 2, 3, 49, 2),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 0xab34, "r5": 0xab4,
    "r6": 0x6000000001234}, entry=0x10)

test_extr_u_ignored_bit36_decode = require_registers(
    "extr_u_ignored_bit36_decode", [
        (0x10, *movl_mlx(3, 0xffff000000000000)),
        (0x20, 0x02, nop_m(), extr_u(4, 3, 48, 16, bit36=1),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 0xffff}, entry=0x10)

test_extr_signed_truncates_overlong_field = require_registers(
    "extr_signed_truncates_overlong_field", [
        (0x10, *movl_mlx(3, 0x8000000000000000)),
        (0x20, 0x02, nop_m(), extr(4, 3, 60, 8),
         extr(5, 3, 4, 64)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 0xfffffffffffffff8,
        "r5": 0xf800000000000000}, entry=0x10)

test_dep_source_alias_decode = require_registers("dep_source_alias_decode", [
    (0x10, *movl_mlx(4, 0x07c5080f)),
    (0x20, *movl_mlx(5, 0x000f8a10)),
    (0x30, 0x02, nop_m(), dep(4, 4, 5, 25, 7),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 0x1e0f8a10}, entry=0x10)

test_page_frame_record_address_arithmetic = require_registers(
    "page_frame_record_address_arithmetic", [
        (0x10, *movl_mlx(28, 0x001000019b502661)),
        (0x20, *movl_mlx(20, 0x1ffffeda00000000)),
        (0x30, 0x02, nop_m(), extr_u(27, 28, 13, 37), nop_i()),
        (0x40, 0x02, nop_m(), shladd(26, 27, 1, 27, ignored=1), nop_i()),
        (0x50, 0x02, nop_m(), shladd(24, 26, 4, 0), nop_i()),
        (0x60, 0x02, nop_m(), sub_reg(25, 24, 20), nop_i()),
        (0x70, 0x02, nop_m(), adds(31, 28, 25), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r27": 0xcda81,
        "r25": 0xe00001260268f830,
        "r31": 0xe00001260268f84c,
    }, entry=0x10)

test_page_table_pointer_dep_cascade = require_registers(
    "page_table_pointer_dep_cascade", [
        (0x10, *movl_mlx(2, 0x100000)),
        (0x20, 0x00, mov_ar(2, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(100, 73, 70, 0, 0), nop_i()),
        (0x40, *movl_mlx(31, 0xe00000000000cdef)),
        (0x50, *movl_mlx(55, 0x89ab)),
        (0x60, *movl_mlx(54, 0x4567)),
        (0x70, *movl_mlx(53, 0x123)),
        (0x80, 0x02, nop_m(), dep(104, 55, 31, 16, 16), nop_i()),
        (0x90, 0x02, nop_m(), dep(103, 54, 104, 32, 16), nop_i()),
        (0xa0, 0x02, nop_m(), dep(98, 53, 103, 48, 13), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "r98": 0xe123456789abcdef,
    }, entry=0x10)

test_depz_decode = require_registers("depz_decode", [
    (0x10, *movl_mlx(6, 0x12345678)),
    (0x20, 0x02, nop_m(), depz_imm(4, 5, 24, 3),
     nop_i()),
    (0x30, 0x02, nop_m(), depz_imm(5, -9, 0, 28),
     nop_i()),
    (0x40, 0x02, nop_m(), depz_reg(7, 6, 16, 32),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": 0x05000000, "r5": 0x0ffffff7,
    "r7": 0x123456780000}, entry=0x10)

test_depz_len64_decode = require_registers("depz_len64_decode", [
    (0x10, *movl_mlx(6, 0x8123456789abcdef)),
    (0x20, 0x02, nop_m(), depz_reg(7, 6, 0, 64),
     depz_reg(8, 6, 4, 64)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r7": 0x8123456789abcdef,
    "r8": 0x123456789abcdef0}, entry=0x10)

test_integer_nat_identity_updates = require_registers(
    "integer_nat_identity_updates", [
        # UNAT bit 0 supplies a NaT to the fill from address 0x200.
        (0x10, 0x01, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
         nop_i()),
        (0x20, 0x01, ld8_fill_postinc(5, 4, 0), nop_i(),
         nop_i()),
        # Cover unary dst==src, binary dst==src with a clear second source,
        # and duplicate binary sources copied to a different destination.
        (0x30, 0x01, nop_m(), depz_reg(5, 5, 6, 26),
         nop_i()),
        (0x40, 0x01, nop_m(), shladd(5, 5, 2, 0),
         nop_i()),
        (0x50, 0x01, nop_m(), add(6, 5, 5),
         nop_i()),
        # Exercise the same in-place binary path with a clear NaT.
        (0x60, 0x01, nop_m(), adds(7, 3, 0),
         nop_i()),
        (0x70, 0x01, nop_m(), shladd(7, 7, 2, 0),
         nop_i()),
        (0x80, 0x11, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x80,
        "r5_nat": 1,
        "r6_nat": 1,
        "r7": 12,
        "r7_nat": 0,
    }, entry=0x10)

test_integer_nat_inplace_or_updates = require_registers(
    "integer_nat_inplace_or_updates", [
        # Give r6 and r8 a NaT via UNAT bit 0.  Branch to a fresh TB so the
        # translator must handle their NaT state dynamically.
        (0x10, 0x01, mov_m_imm_ar(36, 1), addl(4, 0x200, 0),
         nop_i()),
        (0x20, 0x01, ld8_fill_postinc(6, 4, 0), adds(5, 3, 0),
         adds(7, 5, 0)),
        (0x30, 0x01, ld8_fill_postinc(8, 4, 0), adds(9, 7, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x80)),
        # Cover both destination aliases and preservation of an already-set
        # destination NaT when the other operand is clear at run time.
        (0x80, 0x00, nop_m(), add(5, 5, 6), add(7, 8, 7)),
        (0x90, 0x01, nop_m(), add(8, 8, 9), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": 0xa0,
        "r5_nat": 1,
        "r7_nat": 1,
        "r8_nat": 1,
        "r9_nat": 0,
    }, entry=0x10)

test_integer_nat_inplace_or_crosses_word_boundary = require_registers(
    "integer_nat_inplace_or_crosses_word_boundary", [
        (0x10, 0x00, nop_m(), alloc(20, 96, 30, 0, 0), nop_i()),
        (0x20, 0x01, mov_m_imm_ar(36, 1), addl(6, 0x400, 0),
         nop_i()),
        (0x30, 0x01, ld8_fill_postinc(62, 6, 0), adds(63, 3, 0),
         adds(65, 5, 0)),
        (0x40, 0x01, ld8_fill_postinc(64, 6, 0), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x80)),
        # Exercise both directions across cpu_nat[0]/cpu_nat[1].
        (0x80, 0x00, nop_m(), add(63, 63, 64), add(65, 62, 65)),
        # A call remaps and synchronizes the stacked output registers, also
        # checking that the optimized path retained dirty bookkeeping.
        (0x90, 0x10, nop_m(), nop_i(), br_call(0, 0x90, 0xc0)),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        (0x400, 0x00, 0, 0, 0),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "cfm_sof": 66,
        # Caller r63/r65 are callee r33/r35.
        "r33_nat": 1,
        "r35_nat": 1,
    }, entry=0x10)

test_sxt1_decode = require_registers("sxt1_decode", [
    (0x10, 0x00, addl(3, 0xff, 0), nop_i(),
     nop_i()),
    (0x20, 0x02, nop_m(), nop_i(),
     sxt1(4, 3)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0xffffffffffffffff}, entry=0x10)

test_mov_lc_imm_decode = require_registers("mov_lc_imm_decode", [
    (0x10, 0x02, nop_m(), nop_i(),
     mov_lc_imm(15)),
    (0x20, 0x02, nop_m(), nop_i(),
     mov_ar_lc(4)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 15}, entry=0x10)

test_mov_lc_negative_imm_sign_extends = require_registers(
    "mov_lc_negative_imm_sign_extends", [
        (0x10, 0x02, nop_m(), nop_i(), mov_lc_imm(-1)),
        (0x20, 0x02, nop_m(), nop_i(), mov_ar_lc(4)),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": UINT64_MAX}, entry=0x10)

test_mov_pr_rot_imm_sign_extends = require_registers(
    "mov_pr_rot_imm_sign_extends", [
        (0x10, 0x00, nop_m(), mov_pr_rot_imm(1 << 43), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "pr_mask": 1 | (((1 << 21) - 1) << 43),
    }, entry=0x10)

test_mov_m_negative_imm_ar_sign_extends = require_registers(
    "mov_m_negative_imm_ar_sign_extends", [
        (0x10, 0x00, mov_m_imm_ar(36, -1), nop_i(), nop_i()),
        (0x20, 0x00, mov_m_ar_gr(4, 36), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": UINT64_MAX}, entry=0x10)

test_mov_m_imm_ar_decode = require_registers("mov_m_imm_ar_decode", [
    (0x10, 0x00, mov_m_imm_ar(36, 0x5a), nop_i(),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "ar_unat": 0x5a}, entry=0x10)

test_mov_m_psr_gr_decode = require_registers("mov_m_psr_gr_decode", [
    (0x10, 0x00, sum_um(IA64_PSR_UP), nop_i(),
     nop_i()),
    (0x20, 0x00, mov_m_psr_gr(29), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r29": IA64_PSR_UP, "psr": IA64_PSR_UP}, entry=0x10)

test_mov_m_gr_psrl_decode = require_registers("mov_m_gr_psrl_decode", [
    (0x10, 0x00, addl(2, IA64_PSR_UP, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, mov_m_gr_psrl(2), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "psr": IA64_PSR_UP}, entry=0x10)

test_mov_psrl_ignores_high_source_bits = require_registers(
    "mov_psrl_ignores_high_source_bits", [
        (0x10, *movl_mlx(16, 0x111)),
        (0x20, 0x10, nop_m(), nop_i(), bsw1()),
        (0x30, *movl_mlx(16, 0x222)),
        (0x40, *movl_mlx(
            2, (0x7fff00000000 & ~IA64_PSR_BN) | IA64_PSR_UP)),
        (0x50, 0x00, mov_m_gr_psrl(2), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_psr_gr(9), adds(8, 0, 16), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "psr": IA64_PSR_BN | IA64_PSR_UP,
        "r8": 0x222,
        "r9": IA64_PSR_UP,
    }, entry=0x10, cpu="montecito")

test_epc_b_ignored_fields_decode = require_registers(
    "epc_b_ignored_fields_decode", [
        (0x10, 0x10, nop_m(), nop_i(),
         epc_b(ignored=0xf78c1)),
        (0x20, 0x10, nop_m(), addl(4, 0x44, 0),
         br_cond(0x20, 0x30)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "exception": IA64_EXCP_NONE,
        "r4": 0x44,
    }, entry=0x10)

test_bsw0_clears_bn_bit = require_registers("bsw0_clears_bn_bit", [
    (0x10, 0x10, nop_m(), nop_i(),
     bsw1()),
    (0x20, 0x10, nop_m(), nop_i(),
     bsw0()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "psr": 0}, entry=0x10)

test_bsw0_in_b_slot_falls_through = require_registers("bsw0_in_b_slot_falls_through", [
    (0x10, 0x10, nop_m(), nop_i(),
     bsw1()),
    (0x20, 0x10, nop_m(), nop_i(),
     bsw0()),
    (0x30, 0x10, nop_m(), adds(2, 0x33, 0),
     br_cond(0x30, 0x40)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "psr": 0, "r2": 0x33}, entry=0x10)

test_bsw1_sets_bn_bit = require_registers("bsw1_sets_bn_bit", [
    (0x10, 0x10, nop_m(), nop_i(),
     bsw1()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "psr": 1 << 44}, entry=0x10)

# Montecito implements the virtualization extensions but this model provides
# no virtual-machine environment, so vmsw reports the architected fault.
test_vmsw1_montecito_virtualization_fault = require_exception(
    "vmsw1_montecito_virtualization_fault", [
        (0x10, 0x10, nop_m(), nop_i(),
         vmsw1()),
    ], IA64_EXCP_VIRTUALIZATION, fault_ip=0x10)

test_vmsw0_montecito_virtualization_fault = require_exception(
    "vmsw0_montecito_virtualization_fault", [
        (0x10, 0x10, nop_m(), nop_i(),
         vmsw0()),
    ], IA64_EXCP_VIRTUALIZATION, fault_ip=0x10)

# The virtualization extensions post-date Madison, so the encoding is
# reserved there.
test_vmsw1_madison_illegal_operation = require_exception(
    "vmsw1_madison_illegal_operation", [
        (0x10, 0x10, nop_m(), nop_i(),
         vmsw1()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

test_vmsw0_madison_illegal_operation = require_exception(
    "vmsw0_madison_illegal_operation", [
        (0x10, 0x10, nop_m(), nop_i(),
         vmsw0()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

# On models with the virtualization extensions vmsw is a privileged
# instruction, so the privilege check precedes the Virtualization fault.
# (PSR.ic must be cleared before the rfi setup writes IPSR and IIP.)
test_vmsw_cpl3_montecito_privileged_operation = require_exception(
    "vmsw_cpl3_montecito_privileged_operation", [
        (0x10, 0x00, rsm(IA64_PSR_IC), nop_i(), nop_i()),
        (0x20, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x30, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
        (0x40, 0x00, nop_m(), adds(31, 0x70, 0), nop_i()),
        *rfi_to_gr(0x50, 19, 31),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         vmsw1()),
    ], IA64_EXCP_PRIVILEGED_OP, fault_ip=0x80)

# The reserved encoding on Madison stays an Illegal Operation fault even at
# a lower privilege level: there is no privilege semantic for it at all.
test_vmsw_cpl3_madison_illegal_operation = require_exception(
    "vmsw_cpl3_madison_illegal_operation", [
        (0x10, 0x00, rsm(IA64_PSR_IC), nop_i(), nop_i()),
        (0x20, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x30, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
        (0x40, 0x00, nop_m(), adds(31, 0x70, 0), nop_i()),
        *rfi_to_gr(0x50, 19, 31),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         vmsw1()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x80, cpu="madison")

test_bsw_switches_r16_r31_bank = require_registers("bsw_switches_r16_r31_bank", [
    (0x10, *movl_mlx(16, 0x1111)),
    (0x20, 0x10, nop_m(), nop_i(),
     bsw1()),
    (0x30, *movl_mlx(16, 0x2222)),
    (0x40, 0x00, adds(2, 0, 16), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     bsw0()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "psr": 0,
    "r2": 0x2222,
    "r16": 0x1111,
}, entry=0x10)

test_mov_m_cr_gr_decode = require_registers("mov_m_cr_gr_decode", [
    (0x10, 0x00, addl(2, 0x1234, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, mov_m_gr_cr(2, 19), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_m_cr_gr(29, 19), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r29": 0x1234}, entry=0x10)

test_mov_cr_lid_ignored_high_bits_read_zero = require_registers(
    "mov_cr_lid_ignored_high_bits_read_zero", [
        (0x10, *movl_mlx(2, 0xdeadbeef12340000)),
        (0x20, 0x00, mov_m_gr_cr(2, 64), nop_i(), nop_i()),
        (0x30, 0x00, mov_m_cr_gr(29, 64), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "r29": 0x12340000,
    }, entry=0x10)

test_mov_cr_lid_reserved_low_bits_fault = require_exception(
    "mov_cr_lid_reserved_low_bits_fault", [
        (0x10, *movl_mlx(2, 0x12340001)),
        (0x20, 0x00, mov_m_gr_cr(2, 64), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x20)

test_czx1_r_zero_index = require_registers("czx1_r_zero_index", [
    (0x10, *movl_mlx(3, 0x8877665500332211)),
    (0x20, 0x00, nop_m(), czx1_r(31, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 3}, entry=0x10)

test_czx1_r_no_zero = require_registers("czx1_r_no_zero", [
    (0x10, *movl_mlx(3, 0x3d6365766863616d)),
    (0x20, 0x00, nop_m(), czx1_r(31, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 8}, entry=0x10)

test_czx1_l_zero_index = require_registers("czx1_l_zero_index", [
    (0x10, *movl_mlx(3, 0x8877665500332211)),
    (0x20, 0x00, nop_m(), czx1_l(31, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 4}, entry=0x10)

test_czx2_r_zero_index = require_registers("czx2_r_zero_index", [
    (0x10, *movl_mlx(3, 0x3333000022221111)),
    (0x20, 0x00, nop_m(), czx2_r(31, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 2}, entry=0x10)

test_czx2_r_ignored_r2_decode = require_registers("czx2_r_ignored_r2_decode", [
    (0x10, *movl_mlx(3, 0x3333000022221111)),
    (0x20, 0x00, nop_m(), czx2_r(31, 3) | bitfield(6, 13, 7),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 2}, entry=0x10)

test_czx2_l_zero_index = require_registers("czx2_l_zero_index", [
    (0x10, *movl_mlx(3, 0x3333000022221111)),
    (0x20, 0x00, nop_m(), czx2_l(31, 3),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "exception": IA64_EXCP_NONE, "r31": 1}, entry=0x10)

test_mov_cpuid_indexed_decode = require_registers("mov_cpuid_indexed_decode", [
    (0x10, 0x00, nop_m(), addl(31, 3, 0),
     nop_i()),
    (0x20, 0x00, mov_cpuid(29, 31, bit36=1, ignored=0x55), nop_i(),
     nop_i()),
    (0x30, 0x00, nop_m(), addl(31, 4, 0),
     nop_i()),
    (0x40, 0x00, mov_cpuid(28, 31, bit36=1), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_cpuid(30, 0), nop_i(),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r28": 0x0000000000000005,
    "r29": 0x0000000020000704,
    "r30": 0x49656e69756e6547,
}, entry=0x10)

def _cpuid_case(name, cpu, version, features):
    return require_registers(name, [
        (0x10, 0x00, nop_m(), addl(31, 3, 0), nop_i()),
        (0x20, 0x00, mov_cpuid(29, 31), addl(31, 4, 0), nop_i()),
        (0x30, 0x00, mov_cpuid(28, 31), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r28": features,
        "r29": version,
    }, entry=0x10, cpu=cpu)


test_mov_cpuid_madison_model = _cpuid_case(
    "mov_cpuid_madison_model", "madison", 0x1f010504, 1)

test_mov_cpuid_madison_zx6000_profile = _cpuid_case(
    "mov_cpuid_madison_zx6000_profile", "madison-zx6000",
    0x1f010504, 1)

test_mov_cpuid_mckinley_model = _cpuid_case(
    "mov_cpuid_mckinley_model", "mckinley", 0x1f000704, 1)

test_mov_cpuid_madison_9m_model = _cpuid_case(
    "mov_cpuid_madison_9m_model", "madison-9m", 0x1f020204, 1)

test_mov_cpuid_montvale_model = _cpuid_case(
    "mov_cpuid_montvale_model", "montvale", 0x20010104, 5)


def _merced_cpuid_case(name, cpu):
    return require_registers(name, [
        (0x10, 0x00, nop_m(), addl(31, 3, 0), nop_i()),
        (0x20, 0x00, mov_cpuid(29, 31), addl(31, 4, 0), nop_i()),
        (0x30, 0x00, mov_cpuid(28, 31), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r28": 0,
        "r29": 0x0000000007000804,
    }, entry=0x10, cpu=cpu)


test_mov_cpuid_merced_model = _merced_cpuid_case(
    "mov_cpuid_merced_model", "merced")

test_mov_cpuid_itanium_alias = _merced_cpuid_case(
    "mov_cpuid_itanium_alias", "itanium")

test_mov_cpuid_itanium2_alias = require_registers(
    "mov_cpuid_itanium2_alias", [
        (0x10, 0x00, nop_m(), addl(31, 3, 0), nop_i()),
        (0x20, 0x00, mov_cpuid(29, 31), addl(31, 4, 0), nop_i()),
        (0x30, 0x00, mov_cpuid(28, 31), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r28": 0x5,
        "r29": 0x0000000020000704,
    }, entry=0x10, cpu="itanium2")

test_mov_dahr_indexed_decode = require_registers("mov_dahr_indexed_decode", [
    (0x10, 0x00, addl(18, 2, 0), addl(29, 0x55, 0),
     nop_i()),
    (0x20, 0x00, mov_dahr_read(29, 18, bit36=1, ignored=0x7b),
     nop_i(), nop_i()),
    (0x30, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
    (0x40, 0x00, nop_m(), adds(31, 0x80, 0), nop_i()),
    *rfi_to_gr(0x50, 19, 31),
    (0x80, 0x00, srlz_d(), nop_i(), nop_i()),
    (0x90, 0x00, mov_dahr_write(2, 0xffff), nop_i(), nop_i()),
    (0xa0, 0x00, mov_dahr_write(2, 0x123, qp=1), nop_i(), nop_i()),
    (0xb0, 0x00, mov_dahr_read(29, 18, bit36=1, ignored=0x7b),
     nop_i(), nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(),
     br_cond(0xc0, 0xc0)),
], {
    "ip": 0xc0,
    "exception": IA64_EXCP_NONE,
    "r29": 0x7ff,
}, entry=0x10)

test_dahr_resets_on_br_call = require_registers("dahr_resets_on_br_call", [
    (0x10, 0x00, mov_dahr_write(2, 0x345), addl(18, 2, 0), nop_i()),
    (0x20, 0x00, mov_dahr_read(29, 18), nop_i(), nop_i()),
    (0x30, 0x10, nop_m(), nop_i(), br_call(6, 0x30, 0x80)),
    (0x40, 0x00, mov_dahr_read(30, 18), nop_i(), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    (0x80, 0x00, mov_dahr_read(31, 18), nop_i(), nop_i()),
    (0x90, 0x10, nop_m(), nop_i(), br_ret(6)),
], {
    "ip": 0x50,
    "exception": IA64_EXCP_NONE,
    "r29": 0x345,
    "r30": 0,
    "r31": 0,
}, entry=0x10)

test_dahr_resets_on_mov_bspstore = require_registers(
    "dahr_resets_on_mov_bspstore", [
        (0x10, 0x00, mov_dahr_write(4, 0x456), addl(18, 4, 0), nop_i()),
        (0x20, 0x00, nop_m(), addl(3, 0x8000, 0), nop_i()),
        (0x30, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x40, 0x00, mov_dahr_read(29, 18), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "r29": 0,
    }, entry=0x10)

test_mov_msr_indexed_decode = require_registers("mov_msr_indexed_decode", [
    (0x10, 0x00, addl(2, 66, 0), addl(3, 0x1234, 0),
     nop_i()),
    (0x20, 0x00, mov_msr_write(2, 3, bit36=1), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_msr_read(31, 2, bit36=1, ignored=0x55), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "exception": IA64_EXCP_NONE,
    "r31": 0x1234,
}, entry=0x10)

test_mov_dbr_ibr_indexed_decode = require_registers("mov_dbr_ibr_indexed_decode", [
    (0x10, 0x00, addl(2, 6, 0), addl(3, 0x66, 0),
     nop_i()),
    (0x20, 0x00, addl(4, 0x77, 0), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_dbr_indexed_write(2, 3), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_ibr_indexed_write(2, 4), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_dbr_indexed_read(29, 2), nop_i(),
     nop_i()),
    (0x60, 0x00, mov_ibr_indexed_read(30, 2, bit36=1), nop_i(),
     nop_i()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
], {
    "ip": 0x70,
    "exception": IA64_EXCP_NONE,
    "r29": 0x66,
    "r30": 0x77,
}, entry=0x10)

test_mov_dbr_index8_reserved_register_field = require_exception(
    "mov_dbr_index8_reserved_register_field", [
        (0x10, 0x00, addl(2, 8, 0), addl(3, 0x66, 0), nop_i()),
        (0x20, 0x00, mov_dbr_indexed_write(2, 3), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x20)

test_mov_ibr_index8_reserved_register_field = require_exception(
    "mov_ibr_index8_reserved_register_field", [
        (0x10, 0x00, addl(2, 8, 0), addl(3, 0x77, 0), nop_i()),
        (0x20, 0x00, mov_ibr_indexed_write(2, 3), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x20)

test_mov_br_hint_decode = require_registers("mov_br_hint_decode", [
    (0x10, 0x00, addl(3, 0x1234, 0), nop_i(),
     nop_i()),
    (0x20, 0x09, nop_m(), nop_m(),
     mov_b_gr(0, 3, x6=2)),
    (0x30, 0x00, nop_m(), mov_gr_b(29, 0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r29": 0x1234}, entry=0x10)

test_ssm_rsm_decode = require_registers("ssm_rsm_decode", [
    (0x10, 0x00, ssm(0x4000), nop_i(),
     nop_i()),
    (0x20, 0x00, mov_m_psr_gr(29), nop_i(),
     nop_i()),
    (0x30, 0x00, rsm(0x4000), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_m_psr_gr(30), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r29": 0x4000, "r30": 0}, entry=0x10)

test_sum_um_rum_decode = require_registers("sum_um_rum_decode", [
    (0x10, 0x00, rsm(0x8), nop_i(),
     nop_i()),
    (0x20, 0x00, sum_um(0x8), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_m_psr_gr(29), nop_i(),
     nop_i()),
    (0x40, 0x00, rum(0x8), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_m_psr_gr(30), nop_i(),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {"ip": 0x60, "r29": 0x8, "r30": 0}, entry=0x10)

test_psr_high_mask_and_um_decode = require_registers("psr_high_mask_and_um_decode", [
    (0x10, 0x00, addl(18, 0x1a, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, mov_m_gr_psr_um(18), nop_i(),
     nop_i()),
    (0x30, 0x00, mov_m_psr_um_gr(29), nop_i(),
     nop_i()),
    (0x40, 0x00, ssm(0x682008), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_m_psr_gr(30), nop_i(),
     nop_i()),
    (0x60, 0x00, rsm(0x682008), nop_i(),
     nop_i()),
    (0x70, 0x00, mov_m_psr_gr(31), nop_i(),
     nop_i()),
    (0x80, 0x00, ssm(IA64_PSR_SP), nop_i(),
     nop_i()),
    (0x90, 0x00, sum_um(0x4), nop_i(),
     nop_i()),
    (0xa0, 0x00, mov_m_psr_gr(28), nop_i(),
     nop_i()),
    (0xb0, 0x10, nop_m(), nop_i(),
     br_cond(0xb0, 0xb0)),
], {
    "ip": 0xb0,
    "exception": IA64_EXCP_NONE,
    "r28": IA64_PSR_SP | 0x12,
    "r29": 0x1a,
    "r30": 0x68201a,
    "r31": 0x12,
}, entry=0x10)

test_mov_psr_um_reserved_bit_fault = require_exception(
    "mov_psr_um_reserved_bit_fault", [
        (0x10, 0x00, addl(18, 0x40, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, mov_m_gr_psr_um(18), nop_i(),
         nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x20)

test_shr_u_imm_decode = require_registers("shr_u_imm_decode", [
    (0x10, 0x00, addl(3, 0x80, 0), nop_i(),
     nop_i()),
    (0x20, 0x02, nop_m(), shr_u_imm(4, 3, 4),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 8}, entry=0x10)

test_shrp_imm_decode = require_registers("shrp_imm_decode", [
    (0x10, 0x00, addl(2, 0x1234, 0), addl(3, 0x5678, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), shrp_imm(4, 2, 3, 16),
     shrp_imm(5, 2, 3, 0, ignored=1)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r4": 0x1234000000000000, "r5": 0x5678}, entry=0x10)

test_reserved_a1_x4_5_x2b_1_illegal = require_exception(
    "reserved_a1_x4_5_x2b_1_illegal",
    [(0x10, 0x00, nop_m(), reserved_a1_x4_5_x2b_1(1, 2, 3), nop_i())],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_reserved_a1_predicated_off_is_nop = require_registers(
    "reserved_a1_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(),
         reserved_a1_x4_5_x2b_1(1, 2, 3, qp=1), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
        "r1": 0,
    }, entry=0x10)

test_mux1_rev_decode = require_registers("mux1_rev_decode", [
    (0x10, *movl_mlx(28, 0x1122334455667788)),
    (0x20, 0x02, nop_m(), mux1_rev(16, 28),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r16": 0x8877665544332211}, entry=0x10)

test_mux2_imm_decode = require_registers("mux2_imm_decode", [
    (0x10, *movl_mlx(31, 0x1122334455667788)),
    (0x20, 0x02, nop_m(), mux2(28, 31, 0x05),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r28": 0x7788778855665566}, entry=0x10)

test_pcmp1_eq_decode = require_registers("pcmp1_eq_decode", [
    (0x10, *movl_mlx(2, 0x0102030405060708)),
    (0x20, *movl_mlx(3, 0x0102030005060008)),
    (0x30, 0x02, nop_m(), pcmp1_eq(17, 2, 3),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r17": 0xffffff00ffff00ff}, entry=0x10)

test_pcmp1_eq_m_slot_decode = require_registers("pcmp1_eq_m_slot_decode", [
    (0x10, *movl_mlx(2, 0xff02030405060708)),
    (0x20, *movl_mlx(3, 0xff00030400060700)),
    (0x30, 0x02, pcmp1_eq(16, 2, 3), nop_i(),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r16": 0xff00ffff00ffff00}, entry=0x10)

test_pavg_decode = require_registers("pavg_decode", [
    (0x10, *movl_mlx(20, 0x0001020304050607)),
    (0x20, *movl_mlx(21, 0x0000000100020003)),
    (0x30, *movl_mlx(22, 0x0101010101010101)),
    (0x40, *movl_mlx(23, 0x0001000100010001)),
    (0x50, 0x02, nop_m(), pavg(4, 20, 0, 1),
     pavg(5, 20, 0, 1, raz=True)),
    (0x60, 0x02, nop_m(), pavg(6, 21, 0, 2),
     pavg(7, 21, 0, 2, raz=True)),
    (0x70, 0x02, nop_m(), pavgsub(8, 0, 22, 1),
     pavgsub(9, 0, 23, 2)),
    (0x80, 0x10, nop_m(), nop_i(),
     br_cond(0x80, 0x80)),
], {
    "ip": 0x80,
    "r4": 0x0001010102030303,
    "r5": 0x0001010202030304,
    "r6": 0x0000000100010001,
    "r7": 0x0000000100010002,
    "r8": 0xffffffffffffffff,
    "r9": 0xffffffffffffffff,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_pminmax_pack_decode = require_registers("pminmax_pack_decode", [
    (0x10, *movl_mlx(20, 0x10ff80017fff0002)),
    (0x20, *movl_mlx(21, 0x20017f02ff000003)),
    (0x30, *movl_mlx(22, 0x80017fff0002ffff)),
    (0x40, *movl_mlx(23, 0x7fff800000030001)),
    (0x50, 0x02, nop_m(), pmax1_u(4, 20, 21),
     pmin1_u(5, 20, 21)),
    (0x60, 0x02, nop_m(), pmax2(6, 22, 23),
     pmin2(7, 22, 23)),
    (0x70, *movl_mlx(24, 0x0080ff800080007f)),
    (0x80, *movl_mlx(25, 0x800001000001ffff)),
    (0x90, 0x02, nop_m(), pack2_sss(8, 24, 25),
     pack2_uss(9, 24, 25)),
    (0xa0, *movl_mlx(26, 0x0000800000007fff)),
    (0xb0, *movl_mlx(27, 0x00010000ffff8000)),
    (0xc0, 0x02, nop_m(), pack4_sss(10, 26, 27),
     nop_i()),
    (0xd0, 0x10, nop_m(), nop_i(),
     br_cond(0xd0, 0xd0)),
], {
    "ip": 0xd0,
    "r4": 0x20ff8002ffff0003,
    "r5": 0x10017f017f000002,
    "r6": 0x7fff7fff00030001,
    "r7": 0x800180000002ffff,
    "r8": 0x807f01ff7f807f7f,
    "r9": 0x00ff01008000807f,
    "r10": 0x7fff80007fff7fff,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_psad1_decode = require_registers("psad1_decode", [
    (0x10, *movl_mlx(20, 0x0102030405060708)),
    (0x20, *movl_mlx(21, 0x0806040200060a08)),
    (0x30, 0x02, nop_m(), psad1(4, 20, 21),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "r4": 0x16,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_fc_i_sync_i_decode = require_registers("fc_i_sync_i_decode", [
    (0x10, *movl_mlx(30, 0x200)),
    (0x20, 0x10, fc_i(30), adds(30, 32, 30),
     br_cond(0x20, 0x30)),
    (0x30, 0x08, sync_i(), adds(31, 1, 0),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "exception": IA64_EXCP_NONE, "r30": 0x220,
    "r31": 1}, entry=0x10)

test_srlz_sync_ignored_bit_decode = require_registers(
    "srlz_sync_ignored_bit_decode", [
        (0x10, 0x00, srlz_d(ignored36=1), adds(20, 1, 0),
         nop_i()),
        (0x20, 0x00, srlz_i(ignored36=1), adds(21, 2, 0),
         nop_i()),
        (0x30, 0x00, sync_i(ignored36=1), adds(22, 3, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "r20": 1,
        "r21": 2,
        "r22": 3,
    }, entry=0x10)


test_fwb_decode = require_registers("fwb_decode", [
    (0x10, 0x00, fwb(ignored36=1, ignored=0x73963), adds(23, 4, 0),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {
    "ip": 0x20,
    "exception": IA64_EXCP_NONE,
    "r23": 4,
}, entry=0x10)

test_mf_ignored_bit_decode = require_registers("mf_ignored_bit_decode", [
    (0x10, 0x00, mf(ignored36=1, ignored=0x150bd), adds(24, 5, 0),
     nop_i()),
    (0x20, 0x00, mf(advanced=True, ignored36=1, ignored=0x4a319),
     adds(25, 6, 0), nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {
    "ip": 0x30,
    "exception": IA64_EXCP_NONE,
    "r24": 5,
    "r25": 6,
}, entry=0x10)

test_shladdp4_decode = require_registers("shladdp4_decode", [
    (0x10, 0x00, addl(29, 3, 0), addl(28, 1, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), shladdp4(9, 29, 2, 0),
     shladd(10, 29, 4, 28)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r9": 12, "r10": 49}, entry=0x10)

test_padd1_decode = require_registers("padd1_decode", [
    (0x10, 0x00, addl(3, 0x010203, 0), addl(4, 0x010101, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), padd1(5, 3, 4),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r5": 0x020304}, entry=0x10)

test_psub1_uuu_decode = require_registers("psub1_uuu_decode", [
    (0x10, *movl_mlx(3, 0x000102ff000102ff)),
    (0x20, *movl_mlx(4, 0x0101010101010101)),
    (0x30, 0x02, nop_m(), psub1_uuu(5, 3, 4),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r5": 0x000001fe000001fe}, entry=0x10)

test_pshladd2_decode = require_registers("pshladd2_decode", [
    (0x10, *movl_mlx(3, 0x7fff40000001ffff)),
    (0x20, *movl_mlx(4, 0x00010001ffff0001)),
    (0x30, 0x02, nop_m(), pshladd2(5, 3, 3, 4),
     nop_i()),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r5": 0x7fff7fff0007fff9}, entry=0x10)

test_pshladd2_shift_overflow_suppresses_add = require_registers(
    "pshladd2_shift_overflow_suppresses_add", [
        (0x10, *movl_mlx(3, 0xc0003fffbfff4000)),
        (0x20, *movl_mlx(4, 0x000100010001ffff)),
        (0x30, 0x02, nop_m(), pshladd2(5, 3, 1, 4), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r5": 0x80017fff80007fff,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_pshradd2_decode = require_registers("pshradd2_decode", [
    (0x10, *movl_mlx(3, 0x80007fff0004fffc)),
    (0x20, *movl_mlx(4, 0x000100017fff8000)),
    (0x30, 0x02, nop_m(), pshradd2(5, 3, 1, 4),
     pshradd2(6, 3, 3, 4)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {
    "ip": 0x40,
    "r5": 0xc00140007fff8000,
    "r6": 0xf00110007fff8000,
}, entry=0x10)

test_packed_shift_add_count4_predicated_off_is_nop = require_registers(
    "packed_shift_add_count4_predicated_off_is_nop", [
        (0x10, 0x02, nop_m(), pshladd2(5, 3, 4, 4, qp=1),
         pshradd2(6, 3, 4, 4, qp=1)),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "r5": 0,
        "r6": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_packed_shift_add_count4_true_illegal = require_exception(
    "packed_shift_add_count4_true_illegal", [
        (0x10, 0x02, nop_m(), pshladd2(5, 3, 4, 4), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_shl_var_ignored_bit_decode = require_registers(
    "shl_var_ignored_bit_decode", [
        (0x10, 0x00, addl(8, 0x12, 0), addl(9, 4, 0),
         nop_i()),
        (0x20, 0x02, nop_m(), shl_var(10, 8, 9, ignored=1),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r10": 0x120}, entry=0x10)

test_mpy4_decode = require_registers("mpy4_decode", [
    (0x10, 0x00, nop_m(), addl(31, 4, 0), adds(10, 0x55, 0)),
    (0x20, 0x00, mov_cpuid(29, 31), nop_i(),
     mpy4(10, 0, 0, ignored=1, qp=1)),
    (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
], {
    "ip": 0x30,
    "r10": 0x55,
    "r29": 1,
    "exception": IA64_EXCP_NONE,
}, entry=0x10, cpu="madison")

test_mpy4_unsupported_true_illegal = require_exception(
    "mpy4_unsupported_true_illegal", [
        (0x10, 0x00, nop_m(), nop_i(), mpy4(10, 0, 0)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="madison")

test_mpyshl4_decode = require_registers("mpyshl4_decode", [
    (0x10, 0x00, nop_m(), addl(31, 4, 0), adds(10, 0x55, 0)),
    (0x20, 0x00, mov_cpuid(29, 31), nop_i(),
     mpyshl4(10, 0, 0, ignored=1, qp=1)),
    (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
], {
    "ip": 0x30,
    "r10": 0x55,
    "r29": 5,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_mpyshl4_unsupported_true_illegal = require_exception(
    "mpyshl4_unsupported_true_illegal", [
        (0x10, 0x00, nop_m(), nop_i(), mpyshl4(10, 0, 0)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_pshr_decode = require_registers("pshr_decode", [
    (0x10, *movl_mlx(24, 0x800000007fffffff)),
    (0x20, *movl_mlx(25, 0x80017fff0001ffff)),
    (0x30, 0x00, addl(26, 4, 0), addl(27, 40, 0),
     nop_i()),
    (0x40, 0x00, nop_m(), pshr4(28, 24, 12, ignored=7),
     pshr4(29, 24, 27, unsigned=True, variable=True)),
    (0x50, 0x00, nop_m(), pshr2(30, 25, 26, variable=True),
     pshr2(31, 25, 16, unsigned=True, ignored=5)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r28": 0xfff800000007ffff,
    "r29": 0,
    "r30": 0xf80007ff0000ffff,
    "r31": 0,
}, entry=0x10)

test_pshl_decode = require_registers("pshl_decode", [
    (0x10, *movl_mlx(24, 0x0001000200030004)),
    (0x20, *movl_mlx(25, 0x0000000100000002)),
    (0x30, 0x00, addl(26, 4, 0), addl(27, 8, 0),
     nop_i()),
    (0x40, 0x00, nop_m(), pshl2(28, 24, 26, ignored=1),
     pshl4(29, 25, 27, ignored=1)),
    (0x50, 0x00, nop_m(), pshl2_fixed(30, 24, 16),
     pshl4_fixed(31, 24, 31)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r28": 0x0010002000300040,
    "r29": 0x0000010000000200,
    "r30": 0,
    "r31": 0,
}, entry=0x10)

test_pshl_fixed_complement_count_decode = require_registers(
    "pshl_fixed_complement_count_decode", [
        (0x10, *movl_mlx(8, 0x0000000000000080)),
        (0x20, *movl_mlx(9, 0x0000000000000080)),
        (0x30, 0x01, nop_m(), pshl4_fixed(8, 8, 24, ignored=7),
         pshl2_fixed(9, 9, 8, ignored=7)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "r8": 0x0000000080000000,
        "r9": 0x0000000000008000,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_addp4_decode = require_registers("addp4_decode", [
    (0x10, 0x00, addl(3, 0x5a4d, 0), addl(4, 0x1000, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), addl(6, 3, 0),
     nop_i()),
    (0x30, 0x02, nop_m(), dep(4, 6, 4, 30, 2),
     nop_i()),
    (0x40, 0x02, nop_m(), addp4(5, 3, 4),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r5": 0x60000000c0006a4d}, entry=0x10)

test_addp4_imm_negative_decode = require_registers("addp4_imm_negative_decode", [
    (0x10, 0x02, nop_m(), addp4_imm(16, -1, 0),
     nop_i()),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {"ip": 0x20, "r16": 0xffffffff}, entry=0x10)

test_addp4_imm_a4_decode = require_registers("addp4_imm_a4_decode", [
    (0x10, 0x02, nop_m(), addp4_imm(24, -2048, 0),
     addp4_imm(14, -256, 0)),
    (0x20, 0x10, nop_m(), nop_i(),
     br_cond(0x20, 0x20)),
], {
    "ip": 0x20,
    "r24": 0xfffff800,
    "r14": 0xffffff00,
}, entry=0x10)

test_addp4_imm_positive_decode = require_registers(
    "addp4_imm_positive_decode", [
        (0x10, 0x02, nop_m(), addp4_imm(24, 1, 0), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "r24": 1,
    }, entry=0x10)

test_cdboot_word_add_cloop_decode = require_registers(
    "cdboot_word_add_cloop_decode",
    [
        (0x10, 0x00, alloc(32, 8, 0, 0, 0), nop_i(),
         nop_i()),
        (0x20, *movl_mlx(32, 0x8000)),
        (0x30, *movl_mlx(33, 0x9000)),
        (0x40, *movl_mlx(34, 0xa000)),
        (0x50, *movl_mlx(16, 0xffffffff)),
        (0x60, *movl_mlx(17, 0x00000001)),
        (0x70, 0x08, st4_postinc(33, 16, 4),
         st4_postinc(34, 17, 4), nop_i()),
        (0x80, *movl_mlx(16, 0x00000000)),
        (0x90, *movl_mlx(17, 0xffffffff)),
        (0xa0, 0x08, st4_postinc(33, 16, 4),
         st4_postinc(34, 17, 4), nop_i()),
        (0xb0, *movl_mlx(16, 0xffffffff)),
        (0xc0, *movl_mlx(17, 0x00000001)),
        (0xd0, 0x08, st4_postinc(33, 16, 4),
         st4_postinc(34, 17, 4), nop_i()),
        (0xe0, *movl_mlx(16, 0x00000001)),
        (0xf0, *movl_mlx(17, 0x00000002)),
        (0x100, 0x08, st4_postinc(33, 16, 4),
         st4_postinc(34, 17, 4), nop_i()),
        (0x110, *movl_mlx(32, 0x8000)),
        (0x120, *movl_mlx(33, 0x9000)),
        (0x130, *movl_mlx(34, 0xa000)),
        (0x140, 0x00, nop_m(), adds(8, 0, 0),
         adds(35, 4, 0)),
        (0x150, 0x02, nop_m(), mov_lc_imm(7),
         nop_i()),
        (0x160, 0x00, nop_m(), cmp4_ltu_unc(0, 15, 0, 35),
         mov_i_ar_gr(26, 65)),
        (0x170, 0x10, nop_m(), addp4(31, 35, 0),
         br_cond(0x170, 0x300, qp=15)),
        (0x180, 0x00, nop_m(), adds(30, -1, 31),
         nop_i()),
        (0x190, 0x02, nop_m(), mov_lc_gr(30),
         nop_i()),
        (0x1a0, 0x08, ld4_postinc(31, 33, 4),
         ld4_postinc(30, 34, 4), addp4(29, 8, 0)),
        (0x1b0, 0x00, nop_m(), add(28, 30, 31, ignored=1),
         add(27, 28, 29)),
        (0x1c0, 0x10, st4_postinc(32, 27, 4),
         shr_u_imm(8, 27, 32), br_cloop(0x1c0, 0x1a0)),
        (0x1d0, 0x02, nop_m(), mov_lc_gr(26),
         nop_i()),
        (0x1e0, *movl_mlx(3, 0x8000)),
        (0x1f0, 0x08, ld4_postinc(10, 3, 4), nop_m(),
         nop_i()),
        (0x200, 0x08, ld4_postinc(11, 3, 4), nop_m(),
         nop_i()),
        (0x210, 0x08, ld4_postinc(12, 3, 4), nop_m(),
         nop_i()),
        (0x220, 0x08, ld4_postinc(13, 3, 4), nop_m(),
         mov_ar_lc(9)),
        (0x230, 0x10, nop_m(), nop_i(),
         br_cond(0x230, 0x230)),
        (0x300, 0x10, nop_m(), nop_i(),
         br_cond(0x300, 0x300)),
    ],
    {
        "ip": 0x230,
        "r8": 0,
        "r9": 7,
        "r10": 0,
        "r11": 0,
        "r12": 1,
        "r13": 4,
        "r32": 0x8010,
        "r33": 0x9010,
        "r34": 0xa010,
    },
    entry=0x10,
)

test_sync_i_ignored_fields_do_not_write_gr = require_registers(
    "sync_i_ignored_fields_do_not_write_gr", [
        (0x10, 0x00, nop_m(), adds(5, 0x55, 0), nop_i()),
        (0x20, 0x00, sync_i(ignored=5), nop_i(), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "r5": 0x55,
    }, entry=0x10)

test_private_extension_opcode_illegal = require_exception(
    "private_extension_opcode_illegal",
    [
        (0x10, 0x11, nop_m(),
         op(0xf) | bitfield(1, 26, 1) | bitfield(0, 27, 6),
         nop_b()),
    ],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_clrrrb_b_decode = require_exception("clrrrb_b_decode", [
    (0x10, 0x13, nop_m(), nop_b(), clrrrb_b()),
    (0x20, 0x11, nop_m(), nop_i(), break_b()),
], IA64_EXCP_BREAK, fault_ip=0x20)

test_clrrrb_pr_b_decode = require_exception("clrrrb_pr_b_decode", [
    (0x10, 0x11, nop_m(), nop_i(),
     clrrrb_pr_b(ignored=0x1965d4)),
    (0x20, 0x11, nop_m(), nop_i(), break_b()),
], IA64_EXCP_BREAK, fault_ip=0x20)

test_br_ctop_many_decode = require_exception("br_ctop_many_decode", [
    (0x10, 0x00, nop_m(), addl(8, 1, 0), nop_i()),
    (0x20, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
    (0x30, 0x13, nop_m(), nop_b(), br_ctop_many(0x30, 0x30)),
    (0x40, 0x11, nop_m(), nop_i(), break_b()),
], IA64_EXCP_BREAK, fault_ip=0x40)

test_br_cloop_requires_slot2 = require_exception(
    "br_cloop_requires_slot2", [
        (0x10, 0x12, nop_m(), br_cloop(0x10, 0x10), nop_b()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_application_register_is_illegal = require_exception(
    "reserved_application_register_is_illegal", [
        (0x10, 0x00, mov_m_ar_gr(8, 8), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_ssm_reserved_mask_field_fault = require_exception(
    "ssm_reserved_mask_field_fault", [
        (0x10, 0x00, ssm(1), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x10)

test_privileged_instruction_rejected_at_cpl3 = require_registers(
    "privileged_instruction_rejected_at_cpl3", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
        (0x20, 0x00, nop_m(), adds(31, 0x50, 0), nop_i()),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x60, 0x00, ssm(IA64_PSR_I), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x10,
                 IA64_GENERAL_VECTOR + 0x10)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x10,
        "exception": IA64_EXCP_NONE,
        "r31": 0x10,
    }, entry=0x10)

test_predicated_off_privileged_instruction_does_not_fault = require_registers(
    "predicated_off_privileged_instruction_does_not_fault", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
        (0x20, 0x00, nop_m(), adds(31, 0x50, 0), nop_i()),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x60, 0x00, ssm(IA64_PSR_I, qp=1), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_br_ctop_self_loop_budgeted = require_registers(
    "br_ctop_self_loop_budgeted", [
        (0x10, 0x00, nop_m(), addl(8, 5000, 0), nop_i()),
        (0x20, 0x02, nop_m(), mov_lc_gr(8), nop_i()),
        (0x30, 0x10, nop_m(), adds(4, 1, 4),
         br_ctop_many(0x30, 0x30)),
        (0x40, 0x02, nop_m(), mov_ar_lc(5), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "r4": 5001,
        "r5": 0,
    }, entry=0x10)

test_br_ctop_self_loop_rotates_predicates = require_registers(
    "br_ctop_self_loop_rotates_predicates", [
        (0x10, 0x00, nop_m(), mov_lc_imm(0),
         mov_i_imm_ar(66, 6)),
        (0x20, 0x00, nop_m(), mov_pr_rot_imm(0x10000), nop_i()),
        # p21 starts clear and becomes set while br.ctop drains AR.EC.
        # A translated internal back edge must read the rotated value.
        (0x30, 0x10, nop_m(), adds(8, 1, 8, qp=21),
         br_ctop_many(0x30, 0x30)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
    }, entry=0x10)

test_br_ctop_taken_target_zero = require_registers(
    "br_ctop_taken_target_zero", [
        (0x00, 0x10, nop_m(), adds(8, 1, 0),
         br_cond(0x00, 0x60)),
        (0x10, 0x02, nop_m(), nop_i(), mov_lc_imm(1)),
        (0x20, 0x13, nop_m(), nop_b(),
         br_ctop_many(0x20, 0x00)),
        (0x30, 0x10, nop_m(), adds(8, 2, 0),
         br_cond(0x30, 0x60)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
    }, entry=0x10)

test_brl_call_mlx_decode = require_registers("brl_call_mlx_decode", [
    (0x10, *brl_call_mlx(6, 0x10, 0x40)),
    (0x20, 0x00, adds(8, 1, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, adds(8, 0x5a, 0), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r8": 0x5a}, entry=0x10)

test_brl_call_mlx_no_stop_decode = require_registers(
    "brl_call_mlx_no_stop_decode", [
        (0x10, *brl_call_mlx(6, 0x10, 0x40, template=0x04)),
        (0x20, 0x00, adds(8, 1, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, adds(8, 0x5c, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {"ip": 0x50, "r8": 0x5c}, entry=0x10)

test_brl_call_mlx_negative_lslot_decode = require_registers(
    "brl_call_mlx_negative_lslot_decode", [
        (0x07000030, *brl_call_mlx(6, 0x07000030, 0x010000d0,
                                   ignored_l=0x3)),
        (0x010000d0, 0x00, adds(8, 0x66, 0), nop_i(), nop_i()),
        (0x010000e0, 0x10, nop_m(), nop_i(),
         br_cond(0x010000e0, 0x010000e0)),
    ], {"ip": 0x010000e0, "r8": 0x66}, entry=0x07000030)

test_brl_cond_mlx_decode = require_registers("brl_cond_mlx_decode", [
    (0x10, *brl_cond_mlx(0x10, 0x40)),
    (0x20, 0x00, adds(8, 1, 0), nop_i(),
     nop_i()),
    (0x40, 0x00, adds(8, 0x5b, 0), nop_i(),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r8": 0x5b}, entry=0x10)

test_brl_cond_mlx_no_stop_decode = require_registers(
    "brl_cond_mlx_no_stop_decode", [
        (0x10, *brl_cond_mlx(0x10, 0x40, template=0x04)),
        (0x20, 0x00, adds(8, 1, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, adds(8, 0x5d, 0), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {"ip": 0x50, "r8": 0x5d}, entry=0x10)

test_brl_merced_illegal_operation = require_exception(
    "brl_merced_illegal_operation", [
        (0x10, *brl_cond_mlx(0x10, 0x40)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="merced")

test_brl_cond_qp_false_merced_still_illegal = require_exception(
    "brl_cond_qp_false_merced_still_illegal", [
        (0x10, *brl_cond_mlx(0x10, 0x40, qp=1)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="merced")

test_brl_call_qp_false_merced_still_illegal = require_exception(
    "brl_call_qp_false_merced_still_illegal", [
        (0x10, *brl_call_mlx(6, 0x10, 0x40, qp=1)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, cpu="merced")

test_hint_x_mlx_decode = require_registers("hint_x_mlx_decode", [
    (0x10, *hint_x_mlx(0x3456789abcde)),
    (0x20, 0x00, adds(8, 0x5c, 0), nop_i(),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r8": 0x5c, "exception": IA64_EXCP_NONE}, entry=0x10)

test_br_call_indirect_completers_decode = require_registers(
    "br_call_indirect_completers_decode", [
        (0x10, 0x01, addl(8, 0x70, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call_indirect(6, 7, wh=7, many=True, ignored=0xf2f5,
                          bit36=1)),
        (0x40, 0x10, adds(5, 0x33, 0), nop_i(),
         br_call_indirect(6, 7, wh=3, clear=True, ignored=0xd71e, qp=6)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x70, 0x10, adds(4, 0x5a, 0), nop_i(),
         br_ret(6)),
    ], {"ip": 0x50, "r4": 0x5a, "r5": 0x33}, entry=0x10)

test_br_call_indirect_unused_wh_values_execute = require_registers(
    "br_call_indirect_unused_wh_values_execute", [
        (0x10, 0x00, nop_m(), addl(8, 0x100, 0), nop_i()),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_call_indirect(6, 7, wh=0)),
        (0x40, 0x10, nop_m(), nop_i(), br_call_indirect(6, 7, wh=2)),
        (0x50, 0x10, nop_m(), nop_i(), br_call_indirect(6, 7, wh=4)),
        (0x60, 0x10, nop_m(), nop_i(), br_call_indirect(6, 7, wh=6)),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
        (0x100, 0x10, nop_m(), adds(4, 1, 4), br_ret(6)),
    ], {
        "ip": 0x70,
        "r4": 4,
        "b6": 0x70,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_br_call_indirect_unused_wh_predicated_off = require_registers(
    "br_call_indirect_unused_wh_predicated_off", [
        (0x10, 0x00, nop_m(), addl(8, 0x70, 0), nop_i()),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call_indirect(6, 7, wh=4, qp=6)),
        (0x40, 0x00, adds(5, 0x33, 0), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
        (0x70, 0x10, nop_m(), adds(4, 0x5a, 0), br_ret(6)),
    ], {
        "ip": 0x50,
        "r4": 0,
        "r5": 0x33,
        "b6": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_br_indirect_ignores_low_bits = require_registers(
    "br_indirect_ignores_low_bits", [
        (0x10, *movl_mlx(8, 0x6f)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_indirect(7, wh=3, many=True, clear=True, bit36=1)),
        (0x40, 0x00, adds(4, 0x11, 0), nop_i(), nop_i()),
        (0x60, 0x00, adds(4, 0x5a, 0), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {"ip": 0x70, "r4": 0x5a}, entry=0x10)

test_br_indirect_predicate_false_falls_through = require_registers(
    "br_indirect_predicate_false_falls_through", [
        (0x10, *movl_mlx(8, 0x60)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, qp=6)),
        (0x40, 0x00, adds(4, 0x33, 0), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
        (0x60, 0x00, adds(4, 0x5a, 0), nop_i(), nop_i()),
    ], {"ip": 0x50, "r4": 0x33}, entry=0x10)

test_br_ia_nonzero_qp_illegal = require_exception(
    "br_ia_nonzero_qp_illegal",
    [(0x10, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1, qp=1))],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_br_ia_psr_di_disabled_transition_fault = require_registers(
    "br_ia_psr_di_disabled_transition_fault", [
        (0x10, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DI)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(),
         nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_indirect(7, btype=1)),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(),
         nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17), nop_i(),
         nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x20,
                 IA64_GENERAL_VECTOR + 0x20)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x20,
        "r8": 0x40,
        "r9": 0x40 | (2 << IA64_ISR_EI_SHIFT),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_br_ia_montecito_native_ia32_disabled_fault = require_registers(
    "br_ia_montecito_native_ia32_disabled_fault", [
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x30, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 17), nop_i(),
         nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x20,
                 IA64_GENERAL_VECTOR + 0x20)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x20,
        "r8": 0x40,
        "r9": 0x40 | (2 << IA64_ISR_EI_SHIFT),
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_br_ia_executes_ia32_and_jmpe_returns_to_ia64 = require_registers(
    "br_ia_executes_ia32_and_jmpe_returns_to_ia64", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "b8 34 12 "       # mov ax,0x1234
            "83 c0 02 "       # add ax,2
            "0f b8 00 02")),  # jmpe 0x200
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r1": 0x10a,
        "r8": 0x1236,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_br_ia_merced_executes_ia32_and_jmpe_returns_to_ia64 = require_registers(
    "br_ia_merced_executes_ia32_and_jmpe_returns_to_ia64", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "b8 34 12 "
            "83 c0 02 "
            "0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r1": 0x10a,
        "r8": 0x1236,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="merced")

"""An rfi whose IPSR selects IA-32 drops the dirty partition, so the
backing store has nothing left to spill and AR.BSPSTORE meets AR.BSP.
Leaving BSPSTORE behind would break BSPSTORE == BSP - 8*ndirty and leave
AR.RNAT collecting for a group it no longer addresses.  br.ia rejects a
non-empty store up front, but an rfi arrives with whatever the
interrupted context left."""
test_rfi_to_ia32_empties_backing_store = require_registers(
    "rfi_to_ia32_empties_backing_store", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 20, 0, 0, 0),
         nop_i()),
        (0x40, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x50, 0x00, rsm(IA64_PSR_IC), nop_i(),
         nop_i()),
        (0x60, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, IA64_PSR_IS)),
        (0x80, 0x00, nop_m(), adds(31, 0x100, 0),
         nop_i()),
        *rfi_to_gr(0x90, 19, 31),
        ia32_bundle(0x100, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x210, 0x00, mov_m_ar_gr(9, 18), nop_i(),
         nop_i()),
        (0x220, 0x10, nop_m(), nop_i(),
         br_cond(0x220, 0x220)),
    ], {
        "ip": 0x220,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1000a0,
        "r9": 0x1000a0,
    }, entry=0x700, cpu="madison")

test_ia32_indirect_jump_reaches_target = require_registers(
    "ia32_indirect_jump_reaches_target", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "b8 08 01 "       # mov ax,0x108
            "ff e0 "          # jmp ax
            "90 90 90 "       # skipped bytes
            "0f b8 00 02")),  # jmpe 0x200
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r1": 0x10c,
        "r8": 0x108,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_indirect_call_return_reaches_target = require_registers(
    "ia32_indirect_call_return_reaches_target", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, *movl_mlx(12, 0x300)),
        (0x30, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "b8 10 01 "       # mov ax,0x110
            "ff d0 "          # call ax
            "b8 20 01 "       # mov ax,0x120
            "ff e0")),        # jmp ax
        ia32_bundle(0x110, bytes.fromhex("c3")),  # ret
        ia32_bundle(0x120, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r1": 0x124,
        "r8": 0x120,
        "r12": 0x300,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_self_modifying_store_updates_current_translation_block = \
    require_registers(
        "ia32_self_modifying_store_updates_current_translation_block", [
            *ia32_environment_bundles(
                0x700, 0x10, csd=IA32_TEST_CSD | (1 << 62)),
            (0x10, *movl_mlx(8, 0x100)),
            (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
            (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
            ia32_bundle(0x100, bytes.fromhex(
                "c6 05 09 01 00 00 02 "  # mov byte ptr [0x109],2
                "66 b8 01 00 "           # mov ax,1 -> mov ax,2
                "66 0f b8 00 02")),      # jmpe 0x200
            (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
        ], {
            "ip": 0x200,
            "r8": 2,
            "exception": IA64_EXCP_NONE,
        }, entry=0x700, cpu="madison")

test_br_ia_invalidates_global_alat_entries = require_registers(
    "br_ia_invalidates_global_alat_entries", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
        (0x20, 0x00, ld8_a(22, 3), nop_i(), nop_i()),
        (0x30, *movl_mlx(8, 0x100)),
        (0x40, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x00, chk_a_nc_m(22, 0x200, 0x240),
         adds(4, 1, 0), nop_i()),
        (0x210, 0x10, nop_m(), nop_i(), br_cond(0x210, 0x210)),
        (0x240, 0x10, nop_m(), nop_i(), br_cond(0x240, 0x240)),
        (0x300, 0x00, 0x123456789abcdef0, 0, 0),
    ], {
        "ip": 0x240,
        "r4": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_jmpe_gr1_includes_csd_base = require_registers(
    "ia32_jmpe_gr1_includes_csd_base", [
        *ia32_environment_bundles(
            0x700, 0x10, csd=IA32_TEST_CSD | 0x1000),
        # Keep the Real Mode selector/base relationship consistent.
        (0x10, *movl_mlx(17, 0x100)),
        (0x20, *movl_mlx(8, 0x1100)),
        (0x30, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x1100, bytes.fromhex(
            "0f b8 00 04")),  # jmpe CSD.base + 0x400 = 0x1400
        (0x1400, 0x10, nop_m(), nop_i(), br_cond(0x1400, 0x1400)),
    ], {
        "ip": 0x1400,
        "r1": 0x1104,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_descriptor_ignored_bits_preserved_on_jmpe = require_registers(
    "ia32_descriptor_ignored_bits_preserved_on_jmpe", [
        *ia32_environment_bundles(
            0x700, 0x10,
            csd=0x39b0ffff00000000,
            dsd=0x3930ffff00000000,
            ssd=0x3930ffff00000000),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r25": 0x09b0ffff00000000,
        "r26": 0x0930ffff00000000,
        "r27": 0x3930ffff00000000,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_cpuid_leaf2_reports_madison_cache_descriptors = require_registers(
    "ia32_cpuid_leaf2_reports_madison_cache_descriptors", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "66 31 c0 "           # xor eax,eax
            "0f a2 "              # cpuid: highest leaf
            "66 89 c6 "           # mov esi,eax
            "66 b8 02 00 00 00 "  # mov eax,2
            "0f a2")),            # cpuid: cache descriptors
        ia32_bundle(0x110, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r8": 0x7e776701,
        "r9": 0,
        "r10": 0xffffffff80000000,
        "r11": 0x8d,
        "r14": 2,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_cpuid_leaf2_reports_merced_cache_descriptors = require_registers(
    "ia32_cpuid_leaf2_reports_merced_cache_descriptors", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "66 31 c0 "
            "0f a2 "
            "66 89 c6 "
            "66 b8 02 00 00 00 "
            "0f a2")),
        ia32_bundle(0x110, bytes.fromhex("0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r8": 0x00151001,
        "r9": 0x009b9690,
        "r10": 0xffffffff80000000,
        "r11": 0x0000891a,
        "r14": 2,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="merced")

test_ia32_cpuid_leaf1_reports_madison_feature_word = require_registers(
    "ia32_cpuid_leaf1_reports_madison_feature_word", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "66 b8 01 00 00 00 "  # mov eax,1
            "0f a2 "              # cpuid: signature and feature word
            "0f b8 00 02")),      # jmpe 0x200
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r8": 0x673,
        "r9": 0,
        "r10": 0x4383fbff,
        "r11": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_cpuid_leaf1_reports_merced_signature = require_registers(
    "ia32_cpuid_leaf1_reports_merced_signature", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "66 b8 01 00 00 00 "
            "0f a2 "
            "0f b8 00 02")),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
    ], {
        "ip": 0x200,
        "r8": 0x715,
        "r9": 0,
        "r10": 0x4383fbff,
        "r11": 0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="merced")

test_ia32_x87_top_round_trips_through_fsr = require_registers(
    "ia32_x87_top_round_trips_through_fsr", [
        *ia32_environment_bundles(0x700, 0x10),
        # Begin with TOP=3 and all eight physical x87 registers empty.
        (0x10, *movl_mlx(3, 0x55550000 | (3 << 11))),
        (0x20, 0x00, mov_m_gr_ar(3, 28), nop_i(), nop_i()),
        (0x30, *movl_mlx(8, 0x100)),
        (0x40, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "d9 e8 "             # fld1: TOP 3 -> 2, physical tag 2 valid
            "0f b8 00 02")),     # jmpe 0x200
        (0x200, 0x00, mov_m_ar_gr(8, 28), nop_i(), nop_i()),
        (0x210, 0x00, mov_m_ar_gr(9, 29), nop_i(), nop_i()),
        (0x220, 0x10, nop_m(), nop_i(), br_cond(0x220, 0x220)),
    ], {
        "ip": 0x220,
        "r8": (0x55550000 & ~(1 << 20)) | (2 << 11),
        "r9": (0x1e8 << 48) | 0x100,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_fldenv_restores_x87_environment = require_registers(
    "ia32_fldenv_restores_x87_environment", [
        *ia32_environment_bundles(
            0x700, 0x10, csd=IA32_TEST_CSD | (1 << 62)),
        # Protected mode selects the 32-bit protected-mode FLDENV layout.
        (0x10, *movl_mlx(3, 1)),
        (0x20, 0x00, mov_m_gr_ar(3, 27), nop_i(), nop_i()),
        (0x30, *movl_mlx(8, 0x100)),
        (0x40, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "d9 25 00 03 00 00 "        # fldenv [0x300]
            "0f b8 00 02 00 00")),      # jmpe 0x200
        (0x200, 0x00, mov_m_ar_gr(8, 21), nop_i(), nop_i()),
        (0x210, 0x00, mov_m_ar_gr(9, 28), nop_i(), nop_i()),
        (0x220, 0x00, mov_m_ar_gr(10, 29), nop_i(), nop_i()),
        (0x230, 0x00, mov_m_ar_gr(11, 30), nop_i(), nop_i()),
        (0x240, 0x10, nop_m(), nop_i(), br_cond(0x240, 0x240)),
        # FCW, FSW(TOP=5), FTW(all empty), FIP, FCS/FOP, FDP, FDS.
        raw_bundle(0x300, 0x000028000000037f, 0x123456780000ffff),
        raw_bundle(0x310, 0x8765432105ab2468, 0x0000000000001357),
    ], {
        "ip": 0x240,
        "r8": 0x037f,
        "r9": 0x55550000 | (5 << 11),
        "r10": (0x5ab << 48) | (0x2468 << 32) | 0x12345678,
        "r11": (0x1357 << 32) | 0x87654321,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_fnstenv_saves_x87_environment_and_masks_exceptions = \
    require_registers(
        "ia32_fnstenv_saves_x87_environment_and_masks_exceptions", [
            *ia32_environment_bundles(
                0x700, 0x10, csd=IA32_TEST_CSD | (1 << 62)),
            # Start with every x87 register empty and every exception unmasked.
            (0x10, *movl_mlx(3, 0x55550000)),
            (0x20, 0x00, mov_m_gr_ar(3, 28), nop_i(), nop_i()),
            (0x30, 0x00, mov_m_gr_ar(0, 21), nop_i(), nop_i()),
            (0x40, *movl_mlx(3, 1)),
            (0x50, 0x00, mov_m_gr_ar(3, 27), nop_i(), nop_i()),
            (0x60, *movl_mlx(8, 0x100)),
            (0x70, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
            (0x80, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
            ia32_bundle(0x100, bytes.fromhex(
                "d9 e8 "                    # fld1: TOP 0 -> 7
                "d9 35 00 03 00 00 "        # fnstenv [0x300]
                "0f b8 00 02 00 00")),      # jmpe 0x200
            (0x200, 0x00, mov_m_ar_gr(8, 21), nop_i(), nop_i()),
            (0x210, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
            (0x220, 0x00, ld8(9, 3), nop_i(), nop_i()),
            (0x230, 0x00, addl(3, 0x308, 0), nop_i(), nop_i()),
            (0x240, 0x00, ld8(10, 3), nop_i(), nop_i()),
            (0x250, 0x00, addl(3, 0x310, 0), nop_i(), nop_i()),
            (0x260, 0x00, ld8(11, 3), nop_i(), nop_i()),
            (0x270, 0x10, nop_m(), nop_i(), br_cond(0x270, 0x270)),
            raw_bundle(0x300, 0, 0),
            raw_bundle(0x310, 0, 0),
        ], {
            "ip": 0x270,
            "r8": 0x3f,
            "r9": 0x0000380000000000,
            "r10": 0x0000010000003fff,
            "r11": 0x0000000001e80000,
            "exception": IA64_EXCP_NONE,
        }, entry=0x700, cpu="madison")

test_ia32_fxsave_records_x87_pointers_and_mxcsr_mask = require_registers(
    "ia32_fxsave_records_x87_pointers_and_mxcsr_mask", [
        *ia32_environment_bundles(
            0x700, 0x10, csd=IA32_TEST_CSD | (1 << 62)),
        (0x10, *movl_mlx(3, 0x55550000)),
        (0x20, 0x00, mov_m_gr_ar(3, 28), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, (0x1f80 << 32) | 0x037f)),
        (0x40, 0x00, mov_m_gr_ar(3, 21), nop_i(), nop_i()),
        # Protected mode and CFLG.fxsr (IA-32 CR4.OSFXSR).
        (0x50, *movl_mlx(3, 1 | ((1 << 9) << 32))),
        (0x60, 0x00, mov_m_gr_ar(3, 27), nop_i(), nop_i()),
        (0x70, *movl_mlx(8, 0x100)),
        (0x80, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "d9 e8 "                    # fld1: last FOP/FIP, TOP 0 -> 7
            "0f ae 05 00 03 00 00 "     # fxsave [0x300]
            "0f b8 00 02 00 00")),      # jmpe 0x200
        (0x200, 0x00, addl(3, 0x300, 0), nop_i(), nop_i()),
        (0x210, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0x220, 0x00, addl(3, 0x308, 0), nop_i(), nop_i()),
        (0x230, 0x00, ld8(9, 3), nop_i(), nop_i()),
        (0x240, 0x00, addl(3, 0x318, 0), nop_i(), nop_i()),
        (0x250, 0x00, ld8(10, 3), nop_i(), nop_i()),
        (0x260, 0x10, nop_m(), nop_i(), br_cond(0x260, 0x260)),
        raw_bundle(0x300, 0, 0),
        raw_bundle(0x310, 0, 0),
    ], {
        "ip": 0x260,
        "r8": 0x01e800803800037f,
        "r9": 0x100,
        "r10": 0x0000ffbf00001f80,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_pvi_cli_clears_vif_and_preserves_if = require_registers(
    "ia32_pvi_cli_clears_vif_and_preserves_if", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(3, (1 << 0) | (1 << 33))),
        (0x20, 0x00, mov_m_gr_ar(3, 27), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, (1 << 19) | (1 << 9) | 2)),
        (0x40, 0x00, mov_m_gr_ar(3, 24), nop_i(), nop_i()),
        (0x50, *movl_mlx(
            2, IA64_PSR_IC | IA64_PSR_IS | IA64_PSR_CPL3)),
        (0x60, *movl_mlx(3, 0x100)),
        *rfi_to_gr(0x70, 2, 3),
        ia32_bundle(0x100, bytes.fromhex("fa 0f b8 00 02")),
        (0x200, 0x00, mov_m_ar_gr(8, 24), nop_i(), nop_i()),
        (0x210, 0x10, nop_m(), nop_i(), br_cond(0x210, 0x210)),
    ], {
        "ip": 0x210,
        "r8": (1 << 9) | 2,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_code32_integer_width_is_32_bits = require_registers(
    "ia32_code32_integer_width_is_32_bits", [
        *ia32_environment_bundles(
            0x700, 0x10, csd=IA32_TEST_CSD | (1 << 62)),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "bc 00 04 00 00 "  # mov esp,0x400
            "b8 ff ff ff ff "  # mov eax,0xffffffff
            "83 c0 01 "        # add eax,1
            "9c "              # pushfd
            "5b")),            # pop ebx
        ia32_bundle(0x110, bytes.fromhex(
            "b8 ff ff ff 7f "  # mov eax,0x7fffffff
            "83 c0 01 "        # add eax,1
            "9c "              # pushfd
            "59 "              # pop ecx
            "ba 00 00 00 80 "  # mov edx,0x80000000
            )),
        ia32_bundle(0x120, bytes.fromhex(
            "d1 c2 "           # rol edx,1
            "89 d7 "           # mov edi,edx
            "b8 ff ff ff ff "  # mov eax,0xffffffff
            "be 02 00 00 00 "  # mov esi,2
            "f7 e6")),         # mul esi
        ia32_bundle(0x130, bytes.fromhex(
            "0f b8 00 05 00 00")),  # jmpe 0x500
        (0x500, 0x10, nop_m(), nop_i(), br_cond(0x500, 0x500)),
    ], {
        "ip": 0x500,
        "r8": 0xfffffffffffffffe,
        "r9": 0x896,
        "r10": 1,
        "r11": 0x57,
        "r12": 0x400,
        "r14": 2,
        "r15": 1,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_ia32_bound_uses_16_bit_bound_elements = require_registers(
    "ia32_bound_uses_16_bit_bound_elements", [
        *ia32_environment_bundles(0x700, 0x10),
        (0x10, *movl_mlx(8, 0x100)),
        (0x20, 0x00, nop_m(), mov_br_gr(7, 8), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_indirect(7, btype=1)),
        ia32_bundle(0x100, bytes.fromhex(
            "b8 05 00 "       # mov ax,5
            "62 06 00 03 "    # bound ax,word pair ptr [0x300]
            "bb 34 12 "       # mov bx,0x1234
            "0f b8 00 02")),  # jmpe 0x200
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
        ia32_bundle(0x300, bytes.fromhex("00 00 0a 00")),
    ], {
        "ip": 0x200,
        "r8": 5,
        "r11": 0x1234,
        "exception": IA64_EXCP_NONE,
    }, entry=0x700, cpu="madison")

test_reserved_indirect_branch_btype_illegal = require_exception(
    "reserved_indirect_branch_btype_illegal",
    [(0x10, 0x10, nop_m(), nop_i(), br_indirect(7, btype=2))],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_reserved_ip_relative_branch_btype_illegal = require_exception(
    "reserved_ip_relative_branch_btype_illegal",
    [(0x10, 0x10, nop_m(), nop_i(),
      ip_relative_branch_btype(1, 0x10, 0x20))],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x10,
)

test_br_cloop_decrements_lc = require_registers("br_cloop_decrements_lc", [
    (0x10, 0x00, nop_m(), adds(4, 0, 0), nop_i()),
    (0x20, 0x02, nop_m(), mov_lc_imm(2), nop_i()),
    (0x30, 0x10, nop_m(), adds(4, 1, 4),
     br_cloop(0x30, 0x30)),
    (0x40, 0x02, nop_m(), mov_ar_lc(5), nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": 3, "r5": 0}, entry=0x10)

test_br_ctop_rotating_pipeline = require_registers("br_ctop_rotating_pipeline", [
    (0x10, 0x00, alloc_m(9, 35, 35, 4, 0),
     mov_i_imm_ar(66, 25), mov_lc_imm(0)),
    (0x20, 0x00, nop_m(), mov_pr_rot_imm(0x10000), nop_i()),
    (0x30, 0x00, nop_m(), adds(32, 0x5a, 0, qp=16),
     adds(8, 0, 56, qp=40)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_ctop_many(0x40, 0x30)),
    (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
], {"ip": 0x50, "exception": IA64_EXCP_NONE, "r8": 0x5a}, entry=0x10)

test_br_ctop_long_rotating_pipeline = require_registers(
    "br_ctop_long_rotating_pipeline", [
        # alloc must start an instruction group.  Use MLX;; here so this
        # setup instruction cannot silently leave a pending illegal fault.
        (0x10, 0x05, *movl_mlx(5, 130)[1:]),
        (0x20, 0x00, alloc_m(9, 32, 32, 4, 0),
         mov_i_imm_ar(66, 1), mov_lc_gr(5)),
        (0x30, 0x00, nop_m(), mov_pr_rot_imm(0x10000), adds(4, 1, 0)),
        (0x40, 0x00, nop_m(), adds(32, 0, 4, qp=16), adds(4, 1, 4)),
        (0x50, 0x10, nop_m(), adds(8, 0, 34, qp=18),
         br_ctop_many(0x50, 0x40)),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {"ip": 0x60, "exception": IA64_EXCP_NONE,
        "r4": 132, "r8": 129}, entry=0x10)
test_br_ctop_strcpy_pipeline_stops_on_first_zero_word = require_registers(
    "br_ctop_strcpy_pipeline_stops_on_first_zero_word", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(20, HIGH_TR_BASE + 0x8000)),
        (0x80, *movl_mlx(19, HIGH_TR_BASE + 0xc000)),
        (0x90, *movl_mlx(21, HIGH_TR_BASE + 0xc000 + 1000)),
        (0xa0, *movl_mlx(5, -1)),
        (0xb0, *movl_mlx(17, (1 << 13) | (1 << 17))),
        (0xc0, 0x08, mov_gr_psr_full(17), srlz_d(), nop_i()),
        (0xd0, 0x00, alloc(2, 32, 2, 4, 0), mov_lc_imm(-1), nop_i()),
        # Enter through a taken branch, as optimized library routines do.
        # This also prevents a fall-through TB from executing the first loop
        # iteration while it still contains the pipeline setup.
        (0xe0, 0x11, nop_m(), mov_pr_rot_imm(0x10000),
         br_cond(0xe0, 0xf0)),
        # This is the three-stage software pipeline used by an IA-64
        # word-at-a-time string copy: speculative load, check/zero search,
        # then store.  The stop after slot 1 in the second bundle is
        # intentional and matches the compare-to-branch dependency group.
        (0xf0, 0x00, ld8_s_postinc(32, 20, 8, qp=16),
         chk_s_i(34, 0xf0, 0x200, qp=18), nop_i()),
        (0x100, 0x02, adds(31, 0, 34, qp=18),
         czx1_r(24, 34, qp=18), cmp_eq_imm(0, 7, 8, 24, qp=18)),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x140, qp=7)),
        (0x120, 0x11, st8_postinc(19, 34, 8, qp=18), nop_i(),
         br_ctop_few(0x120, 0xf0)),
        # Match the byte tail as well as the pipelined word loop.  Merely
        # checking the last bulk store leaves an early exit undetected when
        # the untouched destination bytes happen to be zero.
        (0x140, 0x00, nop_m(), mov_lc_gr(24), nop_i()),
        (0x150, 0x01, nop_m(), extr_u(27, 31, 0, 8),
         shr_u_imm(31, 31, 8)),
        (0x160, 0x11, st1_postinc(19, 27, 1), nop_i(),
         br_cloop(0x160, 0x150)),
        (0x170, 0x00, ld8_postinc(9, 21, 8), nop_i(), nop_i()),
        (0x180, 0x00, ld1(10, 21), adds(8, 0, 19), nop_i()),
        (0x190, 0x10, nop_m(), nop_i(), br_cond(0x190, 0x190)),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
        *(raw_bundle(0x408000 + i * 8, _strcpy_pipeline_data[i],
                     _strcpy_pipeline_data[i + 1])
          for i in range(0, len(_strcpy_pipeline_data), 2)),
        *(raw_bundle(0x40c000 + i * 16, 0x5a5a5a5a5a5a5a5a,
                     0x5a5a5a5a5a5a5a5a)
          for i in range(64)),
    ], {"exception": IA64_EXCP_NONE, "ip": 0x190,
        "r8": HIGH_TR_BASE + 0xc000 + 1009,
        "r9": 0x6d6e6f7071727374, "r10": 0}, entry=0x10)

test_br_call_ret_strcpy_pipeline_stops_on_first_zero_word = require_registers(
    "br_call_ret_strcpy_pipeline_stops_on_first_zero_word", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(17, (1 << 13) | (1 << 17))),
        (0x80, 0x08, mov_gr_psr_full(17), srlz_d(), nop_i()),
        (0x90, 0x00, nop_m(), alloc(2, 3, 1, 0, 0), nop_i()),
        (0xa0, *movl_mlx(33, HIGH_TR_BASE + 0xc000)),
        (0xb0, *movl_mlx(34, HIGH_TR_BASE + 0x8000)),
        (0xc0, 0x10, nop_m(), nop_i(), br_call(0, 0xc0, 0x200)),
        (0xd0, *movl_mlx(21, HIGH_TR_BASE + 0xc000 + 1000)),
        (0xe0, 0x00, ld8_postinc(9, 21, 8), nop_i(), nop_i()),
        (0xf0, 0x00, ld8(10, 21), nop_i(), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
        (0x200, 0x00, nop_m(), alloc(2, 32, 2, 4, 0), nop_i()),
        (0x210, 0x00, nop_m(), adds(19, 0, 32), adds(20, 0, 33)),
        (0x220, 0x00, nop_m(), adds(8, 0, 32), nop_i()),
        (0x230, *movl_mlx(5, -1)),
        (0x240, 0x00, nop_m(), mov_lc_gr(5), nop_i()),
        (0x250, 0x00, nop_m(), mov_pr_rot_imm(0x10000), nop_i()),
        (0x260, 0x00, ld8_s_postinc(32, 20, 8, qp=16),
         chk_s_i(34, 0x260, 0x2f0, qp=18), nop_i()),
        (0x270, 0x02, adds(31, 0, 34, qp=18),
         czx1_r(24, 34, qp=18), cmp_eq_imm(0, 7, 8, 24, qp=18)),
        (0x280, 0x10, nop_m(), nop_i(), br_cond(0x280, 0x2c0, qp=7)),
        (0x290, 0x11, st8_postinc(19, 34, 8, qp=18), nop_i(),
         br_ctop_few(0x290, 0x260)),
        (0x2c0, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x2f0, 0x10, nop_m(), nop_i(), br_cond(0x2f0, 0x2f0)),
        *(raw_bundle(0x408000 + i * 8, _strcpy_pipeline_data[i],
                     _strcpy_pipeline_data[i + 1])
          for i in range(0, len(_strcpy_pipeline_data), 2)),
    ], {"exception": IA64_EXCP_NONE, "ip": 0x100,
        "r8": HIGH_TR_BASE + 0xc000,
        "r9": 0x6d6e6f7071727374, "r10": 0}, entry=0x10)

test_br_wtop_false_predicate_drains_epilog = require_registers(
    "br_wtop_false_predicate_drains_epilog", [
        (0x10, 0x00, nop_m(), mov_i_imm_ar(66, 2), nop_i()),
        (0x20, 0x13, nop_m(), nop_b(), br_wtop(0x20, 0x50, qp=16)),
        (0x30, 0x00, adds(4, 0x11, 0), nop_i(), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_cond(0x40, 0x80)),
        (0x50, 0x00, adds(4, 0x5a, 0), nop_i(), nop_i()),
        (0x60, 0x13, nop_m(), nop_b(), br_wtop(0x60, 0x50, qp=16)),
        (0x70, 0x00, nop_m(), mov_i_ar_gr(5, 66), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r4": 0x5a, "r5": 0}, entry=0x10)

test_br_wexit_false_predicate_drains_epilog = require_registers(
    "br_wexit_false_predicate_drains_epilog", [
        (0x10, 0x00, nop_m(), mov_i_imm_ar(66, 2), nop_i()),
        (0x20, 0x13, nop_m(), nop_b(), br_wexit(0x20, 0x70, qp=16)),
        (0x30, 0x00, nop_m(), mov_i_ar_gr(4, 66), nop_i()),
        (0x40, 0x13, nop_m(), nop_b(), br_wexit(0x40, 0x70, qp=16)),
        (0x50, 0x00, adds(6, 0x11, 0), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x80)),
        (0x70, 0x00, nop_m(), mov_i_ar_gr(5, 66), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {"ip": 0x80, "r4": 1, "r5": 0, "r6": 0}, entry=0x10)

test_br_wexit_taken_target_zero = require_registers(
    "br_wexit_taken_target_zero", [
        (0x00, 0x10, nop_m(), adds(9, 1, 0),
         br_cond(0x00, 0x40)),
        (0x10, 0x13, nop_m(), nop_b(),
         br_wexit(0x10, 0x00, qp=1)),
        (0x20, 0x10, nop_m(), adds(9, 2, 0),
         br_cond(0x20, 0x40)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
    ], {
        "ip": 0x40,
        "exception": IA64_EXCP_NONE,
        "r9": 1,
    }, entry=0x10)

test_pmc_pmd_registers_are_independent = require_registers("pmc_pmd_registers_are_independent", [
    (0x10, 0x00, adds(9, 4, 0), adds(20, 0x77, 0),
     nop_i()),
    (0x20, 0x00, mov_grpmc_indexed(9, 20), adds(21, 0x55, 0),
     nop_i()),
    (0x30, 0x00, mov_grpmd_indexed(9, 21), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_pmcgr_indexed(30, 9), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_pmdgr_indexed(31, 9), nop_i(),
     nop_i()),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "exception": IA64_EXCP_NONE,
    "r30": 0x77,
    "r31": 0x55,
}, entry=0x10)

test_pmd_cpl0_secure_monitor_remains_visible = require_registers(
    "pmd_cpl0_secure_monitor_remains_visible", [
        (0x10, 0x00, adds(8, 4, 0), adds(20, 0x44, 0), nop_i()),
        (0x20, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0x30, 0x00, ssm(IA64_PSR_SP), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, mov_pmdgr_indexed(30, 8), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r30": 0x44,
    }, entry=0x10)

test_pmd_cpl3_privileged_monitor_reads_zero = require_registers(
    "pmd_cpl3_privileged_monitor_reads_zero", [
        (0x10, 0x00, adds(8, 4, 0), adds(9, 5, 0), nop_i()),
        (0x20, 0x00, adds(20, 1 << 6, 0), adds(21, 0x44, 0), nop_i()),
        (0x30, 0x00, mov_grpmc_indexed(8, 20), adds(22, 0, 0), nop_i()),
        (0x40, 0x00, mov_grpmd_indexed(8, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_grpmc_indexed(9, 22), adds(23, 0x55, 0), nop_i()),
        (0x60, 0x00, mov_grpmd_indexed(9, 23), nop_i(), nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_CPL3)),
        (0x90, 0x00, nop_m(), adds(31, 0xc0, 0), nop_i()),
        *rfi_to_gr(0xa0, 19, 31),
        (0xc0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xd0, 0x00, mov_pmdgr_indexed(30, 8), nop_i(), nop_i()),
        (0xe0, 0x00, mov_pmdgr_indexed(29, 9), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r29": 0x55,
        "r30": 0,
    }, entry=0x10)

test_pmc_pmd_unimplemented_index_does_not_fault = require_registers(
    "pmc_pmd_unimplemented_index_does_not_fault", [
        (0x10, 0x00, adds(8, 0xff, 0), adds(20, 0x5a, 0), nop_i()),
        (0x20, 0x00, mov_grpmc_indexed(8, 20), nop_i(), nop_i()),
        (0x30, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, mov_pmcgr_indexed(30, 8), nop_i(), nop_i()),
        (0x60, 0x00, mov_pmdgr_indexed(31, 8), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r30": 0,
        "r31": 0,
    }, entry=0x10)

test_pmd_merced_counter_is_32_bit_sign_extended = require_registers(
    "pmd_merced_counter_is_32_bit_sign_extended", [
        (0x10, *movl_mlx(20, 0x1234567880000001)),
        (0x20, 0x00, adds(9, 4, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_grpmd_indexed(9, 20), nop_i(), nop_i()),
        (0x40, 0x00, mov_pmdgr_indexed(30, 9), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "exception": IA64_EXCP_NONE,
        "r30": 0xffffffff80000001,
    }, entry=0x10, cpu="merced")

test_pmc_merced_pal_initial_state = require_registers(
    "pmc_merced_pal_initial_state", [
        (0x10, 0x00, nop_m(), adds(8, 8, 0), nop_i()),
        (0x20, 0x00, mov_pmcgr_indexed(30, 8), adds(8, 9, 0), nop_i()),
        (0x30, 0x00, mov_pmcgr_indexed(31, 8), adds(8, 11, 0), nop_i()),
        (0x40, 0x00, mov_pmcgr_indexed(29, 8), adds(8, 13, 0), nop_i()),
        (0x50, 0x00, mov_pmcgr_indexed(28, 8), nop_i(), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "exception": IA64_EXCP_NONE,
        "r30": 0xf00000003ffffff8,
        "r31": 0xf00000003ffffff8,
        "r29": 0x10000000,
        "r28": 1,
    }, entry=0x10, cpu="merced")

test_pmc_merced_ignored_fields_and_unimplemented_register = require_registers(
    "pmc_merced_ignored_fields_and_unimplemented_register", [
        (0x10, *movl_mlx(20, UINT64_MAX)),
        (0x20, 0x00, nop_m(), adds(8, 0, 0), adds(9, 4, 0)),
        (0x30, 0x00, nop_m(), adds(10, 6, 0), adds(11, 8, 0)),
        (0x40, 0x00, nop_m(), adds(12, 10, 0), adds(13, 11, 0)),
        (0x50, 0x00, nop_m(), adds(14, 12, 0), adds(15, 13, 0)),
        (0x60, 0x00, nop_m(), adds(16, 14, 0), nop_i()),
        (0x70, 0x00, mov_grpmc_indexed(8, 20), nop_i(), nop_i()),
        (0x80, 0x00, mov_pmcgr_indexed(30, 8), nop_i(), nop_i()),
        (0x90, 0x00, mov_grpmc_indexed(9, 20), nop_i(), nop_i()),
        (0xa0, 0x00, mov_pmcgr_indexed(31, 9), nop_i(), nop_i()),
        (0xb0, 0x00, mov_grpmc_indexed(10, 20), nop_i(), nop_i()),
        (0xc0, 0x00, mov_pmcgr_indexed(29, 10), nop_i(), nop_i()),
        (0xd0, 0x00, mov_grpmc_indexed(11, 20), nop_i(), nop_i()),
        (0xe0, 0x00, mov_pmcgr_indexed(28, 11), nop_i(), nop_i()),
        (0xf0, 0x00, mov_grpmc_indexed(12, 20), nop_i(), nop_i()),
        (0x100, 0x00, mov_pmcgr_indexed(27, 12), nop_i(), nop_i()),
        (0x110, 0x00, mov_grpmc_indexed(13, 20), nop_i(), nop_i()),
        (0x120, 0x00, mov_pmcgr_indexed(26, 13), nop_i(), nop_i()),
        (0x130, 0x00, mov_grpmc_indexed(14, 20), nop_i(), nop_i()),
        (0x140, 0x00, mov_pmcgr_indexed(25, 14), nop_i(), nop_i()),
        (0x150, 0x00, mov_grpmc_indexed(15, 20), nop_i(), nop_i()),
        (0x160, 0x00, mov_pmcgr_indexed(24, 15), nop_i(), nop_i()),
        (0x170, 0x00, mov_grpmc_indexed(16, 20), nop_i(), nop_i()),
        (0x180, 0x00, mov_pmcgr_indexed(23, 16), nop_i(), nop_i()),
        (0x190, 0x10, nop_m(), nop_i(), br_cond(0x190, 0x190)),
    ], {
        "ip": 0x190,
        "exception": IA64_EXCP_NONE,
        "r30": 0xf1,
        "r31": 0x037f7f7f,
        "r29": 0x033f7f7f,
        "r28": 0xfffffffe3ffffff8,
        "r27": 0x030f00cf,
        "r26": 0x130f00cf,
        "r25": 0x0000ffcf,
        "r24": 1,
        "r23": 0,
    }, entry=0x10, cpu="merced")

def pmc_overflow_status_ignored_fields_test(name, cpu, expected_pmc0):
    return require_registers(name, [
        (0x10, *movl_mlx(20, UINT64_MAX)),
        (0x20, 0x00, adds(8, 0, 0), adds(9, 1, 0), nop_i()),
        (0x30, 0x00, mov_grpmc_indexed(8, 20), nop_i(), nop_i()),
        (0x40, 0x00, mov_grpmc_indexed(9, 20), nop_i(), nop_i()),
        (0x50, 0x00, mov_pmcgr_indexed(30, 8), nop_i(), nop_i()),
        (0x60, 0x00, mov_pmcgr_indexed(31, 9), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r30": expected_pmc0,
        "r31": 0,
    }, entry=0x10, cpu=cpu)

test_pmc_madison_ignored_fields = pmc_overflow_status_ignored_fields_test(
    "pmc_madison_ignored_fields", "madison", 0xf1)

test_pmc_montecito_ignored_fields = pmc_overflow_status_ignored_fields_test(
    "pmc_montecito_ignored_fields", "montecito", 0xfff1)

test_pmd_merced_address_fields_and_unimplemented_register = require_registers(
    "pmd_merced_address_fields_and_unimplemented_register", [
        (0x10, *movl_mlx(20, 0x1ff800000000001f)),
        (0x20, 0x00, adds(8, 0, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0x40, 0x00, mov_pmdgr_indexed(30, 8), nop_i(), nop_i()),
        (0x50, *movl_mlx(20, 0xe004000000000123)),
        (0x60, 0x00, adds(8, 2, 0), nop_i(), nop_i()),
        (0x70, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0x80, 0x00, mov_pmdgr_indexed(31, 8), nop_i(), nop_i()),
        (0x90, *movl_mlx(20, 0x3ff800000000000f)),
        (0xa0, 0x00, adds(8, 17, 0), nop_i(), nop_i()),
        (0xb0, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0xc0, 0x00, mov_pmdgr_indexed(29, 8), nop_i(), nop_i()),
        (0xd0, *movl_mlx(20, UINT64_MAX)),
        (0xe0, 0x00, adds(8, 18, 0), nop_i(), nop_i()),
        (0xf0, 0x00, mov_grpmd_indexed(8, 20), nop_i(), nop_i()),
        (0x100, 0x00, mov_pmdgr_indexed(28, 8), nop_i(), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r30": 0x3,
        "r31": 0xfffc000000000123,
        "r29": 0x200000000000000d,
        "r28": 0,
    }, entry=0x10, cpu="merced")

test_pmc_pmd_indexed_decode = require_registers("pmc_pmd_indexed_decode", [
    (0x10, 0x00, adds(9, 4, 0), adds(10, 0x77, 0),
     nop_i()),
    (0x20, 0x00, adds(11, 5, 0), adds(12, 0x55, 0),
     nop_i()),
    (0x30, 0x00, mov_grpmc_indexed(9, 10, bit36=1), nop_i(),
     nop_i()),
    (0x40, 0x00, mov_grpmd_indexed(11, 12, bit36=1), nop_i(),
     nop_i()),
    (0x50, 0x00, mov_pmcgr_indexed(30, 9, bit36=1), nop_i(),
     nop_i()),
    (0x60, 0x00, mov_pmdgr_indexed(31, 11, bit36=1), nop_i(),
     nop_i()),
    (0x70, 0x10, nop_m(), nop_i(),
     br_cond(0x70, 0x70)),
], {
    "ip": 0x70,
    "exception": IA64_EXCP_NONE,
    "r30": 0x77,
    "r31": 0x55,
}, entry=0x10)

# cmp.ge.or: compare r3 >= 0 (signed), OR into predicates.
# r39 = 5 (positive), so GE 0 is true → p13 OR'd with 1 → p13=1.
# r39 is then set to 0 which equals 0, so GE 0 is still true → p14 OR'd → p14=1.
test_cmp_ge_or_decode = require_registers("cmp_ge_or_decode", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(13, 14, 0, 16),
     nop_i()),
    (0x20, 0x00, adds(29, 5, 0), cmp_ge_or(13, 14, 29),
     nop_i()),
    (0x30, 0x00, adds(29, 0, 0), cmp_ge_or(14, 13, 29),
     nop_i()),
    (0x40, 0x02, nop_m(), adds(4, 1, 0, qp=13),
     adds(5, 1, 0, qp=14)),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": 1, "r5": 1}, entry=0x10)

test_cmp_ge_or_issue_raw_decode = require_registers("cmp_ge_or_issue_raw_decode", [
    (0x10, 0x00, adds(16, 1, 0), cmp_ltu_unc(13, 14, 0, 16),
     nop_i()),
    (0x20, 0x00, adds(27, -5, 0), adds(28, -1, 0),
     nop_i()),
    (0x30, 0x00, nop_m(), cmp_ge_or_issue_raw(7, 50, 28, qp=13),
     nop_i()),
    (0x40, 0x02, nop_m(), adds(4, 1, 0, qp=7),
     nop_i()),
    (0x50, 0x10, nop_m(), nop_i(),
     br_cond(0x50, 0x50)),
], {"ip": 0x50, "r4": 1}, entry=0x10)

test_cmp_ge_and_decode = require_registers("cmp_ge_and_decode", [
    (0x10, 0x00, cmp4_eq_imm(6, 7, 0, 0), nop_i(),
     nop_i()),
    (0x20, 0x00, adds(3, -1, 0), cmp_ge_and(6, 7, 3),
     nop_i()),
    (0x30, 0x02, nop_m(), adds(4, 1, 0, qp=6),
     adds(5, 1, 0, qp=7)),
    (0x40, 0x10, nop_m(), nop_i(),
     br_cond(0x40, 0x40)),
], {"ip": 0x40, "r4": 1, "r5": 0}, entry=0x10)

test_cmp_ge_or_andcm_issue_raw_decode = require_registers(
    "cmp_ge_or_andcm_issue_raw_decode", [
        (0x10, 0x00, adds(3, -1, 0),
         cmp_ge_or_andcm_issue_raw(6, 7, 3),
         nop_i()),
        (0x20, 0x02, nop_m(), adds(4, 1, 0, qp=6),
         adds(5, 1, 0, qp=7)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {"ip": 0x30, "r4": 1, "r5": 0}, entry=0x10)


test_mux1_brcst_decode = require_registers("mux1_brcst_decode", [
    (0x10, 0x00, nop_m(), adds(14, 0x5a, 0), mux1(1, 14, 0)),
    (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
], {
    "ip": 0x20,
    "r1": 0x5a5a5a5a5a5a5a5a,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_mux1_reserved_mbtype_predicated_off_is_nop = require_registers(
    "mux1_reserved_mbtype_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(), adds(5, 0x44, 0), nop_i()),
        (0x20, 0x02, nop_m(), mux1(5, 3, 1, qp=1), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "r5": 0x44,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_mux1_reserved_mbtype_true_illegal = require_exception(
    "mux1_reserved_mbtype_true_illegal", [
        (0x10, 0x02, nop_m(), mux1(5, 3, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_a7_constant_zero_predicated_off_is_nop = require_registers(
    "a7_constant_zero_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(),
         cmp_ge_and(6, 7, 3, qp=1) | bitfield(0x55, 13, 7), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_a7_constant_zero_true_illegal = require_exception(
    "a7_constant_zero_true_illegal", [
        (0x10, 0x00, nop_m(),
         cmp_ge_and(6, 7, 3) | bitfield(0x55, 13, 7), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_i_selector_predicated_off_is_nop = require_registers(
    "reserved_i_selector_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(),
         op(5) | bitfield(2, 34, 2) | bitfield(1, 0, 6), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_reserved_i_selector_true_illegal = require_exception(
    "reserved_i_selector_true_illegal", [
        (0x10, 0x00, nop_m(), op(5) | bitfield(2, 34, 2), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_fp_memory_selector_predicated_off_is_nop = require_registers(
    "reserved_fp_memory_selector_predicated_off_is_nop", [
        (0x10, 0x00,
         op(6) | bitfield(0x10, 30, 6) | bitfield(1, 0, 6),
         nop_i(), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_reserved_fp_memory_selector_true_illegal = require_exception(
    "reserved_fp_memory_selector_true_illegal", [
        (0x10, 0x00, op(6) | bitfield(0x10, 30, 6), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_popcnt_constant_zero_predicated_off_is_nop = require_registers(
    "popcnt_constant_zero_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(), adds(4, 0x55, 0), nop_i()),
        (0x20, 0x00, nop_m(),
         popcnt(4, 3, qp=1) | bitfield(0x55, 13, 7), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "r4": 0x55,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_popcnt_constant_zero_true_illegal = require_exception(
    "popcnt_constant_zero_true_illegal", [
        (0x10, 0x00, nop_m(),
         popcnt(4, 3) | bitfield(0x55, 13, 7), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_popcnt_size_selector_predicated_off_is_nop = require_registers(
    "popcnt_size_selector_predicated_off_is_nop", [
        (0x10, 0x00, nop_m(), adds(4, 0x55, 0), nop_i()),
        (0x20, 0x00, nop_m(),
         popcnt(4, 3, qp=1) | bitfield(1, 36, 1), nop_i()),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "r4": 0x55,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_popcnt_size_selector_true_illegal = require_exception(
    "popcnt_size_selector_true_illegal", [
        (0x10, 0x00, nop_m(),
         popcnt(4, 3) | bitfield(1, 36, 1), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_scalar_shift_count_64 = require_registers("scalar_shift_count_64", [
    (0x10, *movl_mlx(14, UINT64_MAX)),
    (0x20, 0x00, nop_m(), adds(15, 64, 0), adds(16, 1, 0)),
    (0x30, 0x00, nop_m(), adds(17, -16, 0), shr_u_var(1, 14, 15)),
    (0x40, 0x00, nop_m(), shl_var(2, 16, 15), shr_var(3, 17, 15)),
    (0x50, 0x00, nop_m(), shr_var(4, 16, 15), nop_i()),
    (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
], {
    "ip": 0x60,
    "r1": 0,
    "r2": 0,
    "r3": UINT64_MAX,
    "r4": 0,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_mov_ip_current_bundle = require_registers("mov_ip_current_bundle", [
    (0x10, 0x00, nop_m(), mov_ip(1), mov_ip(2)),
    (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
], {
    "ip": 0x20,
    "r1": 0x10,
    "r2": 0x10,
    "exception": IA64_EXCP_NONE,
}, entry=0x10)

test_brp_loop_imp_decode = require_registers("brp_loop_imp_decode", [
    (0x10, 0x10, nop_m(), adds(1, 1, 0), brp_loop_imp()),
    (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
], {"ip": 0x20, "r1": 1, "exception": IA64_EXCP_NONE}, entry=0x10)

test_brp_sptk_decode = require_registers("brp_sptk_decode", [
    (0x10, 0x10, nop_m(), adds(1, 1, 0), brp_sptk()),
    (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
], {"ip": 0x20, "r1": 1, "exception": IA64_EXCP_NONE}, entry=0x10)

test_br_counted_low6_constant_zero_violation = require_exception(
    "br_counted_low6_constant_zero_violation", [
        (0x10, 0x10, nop_m(), nop_i(), br_cloop(0x10, 0x10) | 1),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_b8_low6_constant_zero_violation_unconditional = require_exception(
    "b8_low6_constant_zero_violation_unconditional", [
        # PR[1] is false, but B8 bits 5:0 are a literal-zero field, not qp.
        (0x10, 0x10, nop_m(), nop_i(), epc_b() | 1),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_loadrs_low6_constant_zero_violation_unconditional = require_exception(
    "loadrs_low6_constant_zero_violation_unconditional", [
        # PR[1] is false, but M25 bits 5:0 are a literal-zero field, not qp.
        (0x10, 0x00, loadrs_enc() | 1, nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_flushrs_low6_constant_zero_violation_unconditional = require_exception(
    "flushrs_low6_constant_zero_violation_unconditional", [
        (0x10, 0x00, flushrs_enc() | 1, nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_b_cyan_predicated_off_is_nop = require_registers(
    "reserved_b_cyan_predicated_off_is_nop", [
        (0x10, 0x10, nop_m(), nop_i(), bitfield(1, 27, 6) | 1),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_reserved_b_cyan_true_illegal = require_exception(
    "reserved_b_cyan_true_illegal", [
        (0x10, 0x10, nop_m(), nop_i(), bitfield(3, 27, 6)),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_b_brown_predicated_off_still_illegal = require_exception(
    "reserved_b_brown_predicated_off_still_illegal", [
        # The brown x6=22 cell is unconditional despite false PR[1].
        (0x10, 0x10, nop_m(), nop_i(), bitfield(0x22, 27, 6) | 1),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_reserved_m0_system_alias_predicated_off_is_nop = require_registers(
    "reserved_m0_system_alias_predicated_off_is_nop", [
        # Table 4-42 x3=1 must not alias x4=4 to sum.
        (0x10, 0x00,
         bitfield(1, 33, 3) | bitfield(4, 27, 4) | 1,
         nop_i(), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_reserved_m0_system_alias_true_illegal = require_exception(
    "reserved_m0_system_alias_true_illegal", [
        (0x10, 0x00,
         bitfield(1, 33, 3) | bitfield(4, 27, 4),
         nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_predicate_register_roundtrip = require_registers(
    "predicate_register_roundtrip", [
        (0x10, 0x00, nop_m(), adds(1, 0xa, 0), mov_gr_pr(1, 0xe)),
        (0x20, 0x00, nop_m(), mov_pr_gr(4), mov_gr_pr(4, -2)),
        (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "r4": 0xb,
        "pr_mask": 0xb,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

GROUP = 'core'
CASE_NAMES = (

    'a7_constant_zero_predicated_off_is_nop',
    'a7_constant_zero_true_illegal',
    'addp4_decode',
    'addp4_imm_a4_decode',
    'addp4_imm_negative_decode',
    'addp4_imm_positive_decode',
    'andcm_imm_negative_mask_round_trip',
    'b8_low6_constant_zero_violation_unconditional',
    'br_call_indirect_completers_decode',
    'br_call_indirect_unused_wh_predicated_off',
    'br_call_indirect_unused_wh_values_execute',
    'br_call_ret_preserves_ec',
    'br_call_ret_strcpy_pipeline_stops_on_first_zero_word',
    'br_counted_low6_constant_zero_violation',
    'br_cloop_decrements_lc',
    'br_cloop_requires_slot2',
    'br_ctop_long_rotating_pipeline',
    'br_ctop_many_decode',
    'br_ctop_rotating_pipeline',
    'br_ctop_self_loop_budgeted',
    'br_ctop_self_loop_rotates_predicates',
    'br_ctop_strcpy_pipeline_stops_on_first_zero_word',
    'br_ctop_taken_target_zero',
    'br_ia_executes_ia32_and_jmpe_returns_to_ia64',
    'br_ia_invalidates_global_alat_entries',
    'br_ia_merced_executes_ia32_and_jmpe_returns_to_ia64',
    'br_ia_montecito_native_ia32_disabled_fault',
    'br_ia_nonzero_qp_illegal',
    'br_ia_psr_di_disabled_transition_fault',
    'ia32_indirect_call_return_reaches_target',
    'ia32_indirect_jump_reaches_target',
    'ia32_cpuid_leaf1_reports_madison_feature_word',
    'ia32_cpuid_leaf1_reports_merced_signature',
    'ia32_cpuid_leaf2_reports_madison_cache_descriptors',
    'ia32_cpuid_leaf2_reports_merced_cache_descriptors',
    'ia32_fldenv_restores_x87_environment',
    'ia32_fnstenv_saves_x87_environment_and_masks_exceptions',
    'ia32_fxsave_records_x87_pointers_and_mxcsr_mask',
    'ia32_x87_top_round_trips_through_fsr',
    'ia32_descriptor_ignored_bits_preserved_on_jmpe',
    'ia32_jmpe_gr1_includes_csd_base',
    'ia32_code32_integer_width_is_32_bits',
    'ia32_bound_uses_16_bit_bound_elements',
    'ia32_pvi_cli_clears_vif_and_preserves_if',
    'ia32_self_modifying_store_updates_current_translation_block',
    'integer_nat_identity_updates',
    'integer_nat_inplace_or_crosses_word_boundary',
    'integer_nat_inplace_or_updates',
    'br_indirect_ignores_low_bits',
    'br_indirect_predicate_false_falls_through',
    'br_wexit_false_predicate_drains_epilog',
    'br_wexit_taken_target_zero',
    'br_wtop_false_predicate_drains_epilog',
    'brl_call_mlx_decode',
    'brl_call_qp_false_merced_still_illegal',
    'brl_call_mlx_negative_lslot_decode',
    'brl_call_mlx_no_stop_decode',
    'brl_cond_mlx_decode',
    'brl_cond_mlx_no_stop_decode',
    'brl_cond_qp_false_merced_still_illegal',
    'brl_merced_illegal_operation',
    'brp_loop_imp_decode',
    'brp_sptk_decode',
    'bsw0_clears_bn_bit',
    'bsw0_in_b_slot_falls_through',
    'bsw1_sets_bn_bit',
    'bsw_switches_r16_r31_bank',
    'cdboot_word_add_cloop_decode',
    'clrrrb_b_decode',
    'clrrrb_pr_b_decode',
    'clz_decode',
    'clz_unsupported_true_illegal',
    'cmp4_eq_imm_decode',
    'cmp4_eq_ne_or_decode',
    'cmp4_eq_unc_imm_p0_decode',
    'cmp4_ge_or_andcm_decode',
    'cmp4_lt_unc_decode',
    'cmp4_ltu_imm_negative_decode',
    'cmp4_ltu_unc_imm_pred_false_clears',
    'cmp4_ltu_unc_p0_register_decode',
    'cmp4_ne_or_andcm_decode',
    'cmp_eq_and_decode',
    'cmp_ge_and_decode',
    'cmp_ge_or_andcm_issue_raw_decode',
    'cmp_ge_or_decode',
    'cmp_ge_or_issue_raw_decode',
    'cmp_imm_update_decode',
    'cmp_lt_unc_imm_decode',
    'cmp_ltu_imm_negative_decode',
    'cmp_ltu_unc_p0_decode',
    'cmp_ne_and_parallel_semantics',
    'cmp_ne_or_andcm_decode',
    'cmp_ne_or_andcm_imm_negative_decode',
    'cmp_same_pred_illegal',
    'cmp_unc_pred_false_clears',
    'cmp_unc_same_pred_pred_false_illegal',
    'cmp_unc_self_predicate_reads_old_qp',
    'czx1_l_zero_index',
    'czx1_r_no_zero',
    'czx1_r_zero_index',
    'czx2_l_zero_index',
    'czx2_r_ignored_r2_decode',
    'czx2_r_zero_index',
    'dep_decode',
    'dep_source_alias_decode',
    'depz_decode',
    'depz_len64_decode',
    'dahr_resets_on_br_call',
    'dahr_resets_on_mov_bspstore',
    'epc_b_ignored_fields_decode',
    'extr_signed_truncates_overlong_field',
    'extr_u_ignored_bit36_decode',
    'fc_i_sync_i_decode',
    'flushrs_low6_constant_zero_violation_unconditional',
    'fwb_decode',
    'hint_i_decode',
    'hint_m_decode',
    'hint_x_mlx_decode',
    'loadrs_low6_constant_zero_violation_unconditional',
    'mf_ignored_bit_decode',
    'mix_decode',
    'mlx_false_predicate_long_nop_decode',
    'mlx_long_nop_x_imm_decode',
    'mov_br_hint_decode',
    'mov_cpuid_indexed_decode',
    'mov_cpuid_itanium2_alias',
    'mov_cpuid_itanium_alias',
    'mov_cpuid_madison_9m_model',
    'mov_cpuid_madison_model',
    'mov_cpuid_madison_zx6000_profile',
    'mov_cpuid_mckinley_model',
    'mov_cpuid_merced_model',
    'mov_cpuid_montvale_model',
    'mov_dahr_indexed_decode',
    'mov_dbr_ibr_indexed_decode',
    'mov_dbr_index8_reserved_register_field',
    'mov_ibr_index8_reserved_register_field',
    'mov_ip_current_bundle',
    'mov_lc_imm_decode',
    'mov_lc_negative_imm_sign_extends',
    'mov_pr_rot_imm_sign_extends',
    'mov_cr_lid_ignored_high_bits_read_zero',
    'mov_cr_lid_reserved_low_bits_fault',
    'mov_m_cr_gr_decode',
    'mov_m_gr_psrl_decode',
    'mov_psrl_ignores_high_source_bits',
    'mov_m_imm_ar_decode',
    'mov_m_negative_imm_ar_sign_extends',
    'mov_m_psr_gr_decode',
    'mov_msr_indexed_decode',
    'mov_psr_um_reserved_bit_fault',
    'mpy4_decode',
    'mpy4_unsupported_true_illegal',
    'mpyshl4_decode',
    'mpyshl4_unsupported_true_illegal',
    'mux1_brcst_decode',
    'mux1_rev_decode',
    'mux1_reserved_mbtype_predicated_off_is_nop',
    'mux1_reserved_mbtype_true_illegal',
    'mux2_imm_decode',
    'padd1_decode',
    'packed_shift_add_count4_predicated_off_is_nop',
    'packed_shift_add_count4_true_illegal',
    'page_frame_record_address_arithmetic',
    'page_table_pointer_dep_cascade',
    'pavg_decode',
    'pcmp1_eq_decode',
    'pcmp1_eq_m_slot_decode',
    'pmc_pmd_indexed_decode',
    'pmc_pmd_registers_are_independent',
    'pmc_pmd_unimplemented_index_does_not_fault',
    'pmc_madison_ignored_fields',
    'pmc_merced_ignored_fields_and_unimplemented_register',
    'pmc_merced_pal_initial_state',
    'pmc_montecito_ignored_fields',
    'pmd_cpl0_secure_monitor_remains_visible',
    'pmd_cpl3_privileged_monitor_reads_zero',
    'pmd_merced_address_fields_and_unimplemented_register',
    'pmd_merced_counter_is_32_bit_sign_extended',
    'pminmax_pack_decode',
    'pmpy2_decode',
    'pmpy2_size_selector_predicated_off_is_nop',
    'pmpy2_size_selector_true_illegal',
    'pmpyshr2_decode',
    'pmpyshr2_size_selector_predicated_off_is_nop',
    'pmpyshr2_size_selector_true_illegal',
    'popcnt_decode',
    'popcnt_constant_zero_predicated_off_is_nop',
    'popcnt_constant_zero_true_illegal',
    'popcnt_size_selector_predicated_off_is_nop',
    'popcnt_size_selector_true_illegal',
    'predicate_register_roundtrip',
    'predicated_off_privileged_instruction_does_not_fault',
    'private_extension_opcode_illegal',
    'privileged_instruction_rejected_at_cpl3',
    'psad1_decode',
    'pshl_decode',
    'pshl_fixed_complement_count_decode',
    'pshladd2_decode',
    'pshladd2_shift_overflow_suppresses_add',
    'pshr_decode',
    'pshradd2_decode',
    'psr_high_mask_and_um_decode',
    'psub1_uuu_decode',
    'reserved_a1_x4_5_x2b_1_illegal',
    'reserved_a1_predicated_off_is_nop',
    'reserved_application_register_is_illegal',
    'reserved_b_brown_predicated_off_still_illegal',
    'reserved_b_cyan_predicated_off_is_nop',
    'reserved_b_cyan_true_illegal',
    'reserved_indirect_branch_btype_illegal',
    'reserved_fp_memory_selector_predicated_off_is_nop',
    'reserved_fp_memory_selector_true_illegal',
    'reserved_i_selector_predicated_off_is_nop',
    'reserved_i_selector_true_illegal',
    'reserved_ip_relative_branch_btype_illegal',
    'reserved_m0_system_alias_predicated_off_is_nop',
    'reserved_m0_system_alias_true_illegal',
    'rfi_to_ia32_empties_backing_store',
    'scalar_shift_count_64',
    'shl_var_ignored_bit_decode',
    'shladdp4_decode',
    'shr_u_imm_decode',
    'shrp_imm_decode',
    'srlz_sync_ignored_bit_decode',
    'ssm_reserved_mask_field_fault',
    'ssm_rsm_decode',
    'sum_um_rum_decode',
    'sxt1_decode',
    'sync_i_ignored_fields_do_not_write_gr',
    'tbit_same_pred_illegal',
    'tbit_unc_pred_false_clears',
    'tf_feature_predicate_updates',
    'tf_constant_zero_predicated_off_is_nop',
    'tf_constant_zero_true_illegal',
    'tf_same_pred_illegal',
    'tf_unc_same_pred_pred_false_illegal',
    'tf_upper_cpuid_feature_bits',
    'unpack2_l_decode',
    'vmsw0_madison_illegal_operation',
    'vmsw0_montecito_virtualization_fault',
    'vmsw1_madison_illegal_operation',
    'vmsw1_montecito_virtualization_fault',
    'vmsw_cpl3_madison_illegal_operation',
    'vmsw_cpl3_montecito_privileged_operation',
    'compare_update_decode',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals())
