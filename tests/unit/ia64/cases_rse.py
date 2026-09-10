"""Register-stack and rotating-register microprograms."""

from __future__ import annotations

import re

from .case import IA64Case, bind_cases
from .encoding import (
    CHECK_LOAD_DATA,
    DTR_PTE_UC,
    DTR_PTE_WB,
    EIGHT_K_ITIR,
    ExpectedFP,
    HIGH_TR_BASE,
    IA64_ALT_DTLB_VECTOR,
    IA64_BREAK_VECTOR,
    IA64_CR_ITM,
    IA64_CR_ITV,
    IA64_CR_SAPIC_EOI,
    IA64_CR_SAPIC_IVR,
    IA64_DATA_ACCESS_VECTOR,
    IA64_DATA_KEY_MISS_VECTOR,
    IA64_DATA_NESTED_TLB_VECTOR,
    IA64_DEBUG_VECTOR,
    IA64_EXCP_DEBUG,
    IA64_EXCP_ILLEGAL,
    IA64_EXCP_NONE,
    IA64_EXCP_RESERVED_REG_FIELD,
    IA64_EXCP_TAKEN_BRANCH,
    IA64_FIRMWARE_IVT_BASE,
    IA64_GENERAL_VECTOR,
    IA64_GENEX_UNIMPL_DATA_ADDR,
    IA64_IMPL_PA_BITS,
    IA64_ISR_CODE_SS,
    IA64_ISR_CODE_TB,
    IA64_ISR_EI_SHIFT,
    IA64_ISR_IR,
    IA64_ISR_NI,
    IA64_ISR_R,
    IA64_ISR_RS,
    IA64_ISR_W,
    IA64_PSR_CPL3,
    IA64_PSR_DB,
    IA64_PSR_DD,
    IA64_PSR_DT,
    IA64_PSR_IC,
    IA64_PSR_I,
    IA64_PSR_PK,
    IA64_PSR_RT,
    IA64_PSR_SS,
    IA64_PSR_TB,
    IA64_PKR_RD,
    IA64_PKR_VALID,
    IA64_RSC_BE,
    IA64_RSC_PL3,
    IA64_TAKEN_BRANCH_VECTOR,
    KERNEL_TR_ITIR,
    KEY_TEST_KEY,
    LOW_VECTOR_ITIR,
    LOW_VECTOR_TR_PTE,
    _strcpy_pipeline_data,
    add_one,
    addl,
    adds,
    alloc,
    alloc_m,
    bitfield,
    br_call,
    br_call_indirect,
    br_cond,
    br_ctop_few,
    br_ctop_many,
    br_indirect,
    br_ret,
    break_m,
    bundle_words,
    chk_s_i,
    chk_s_m,
    clrrrb_b,
    cmp4_eq_imm,
    cmp_eq_imm,
    cover_b,
    cover_b_ignored_fields,
    czx1_r,
    dtr_setup_bundles,
    flushrs_enc,
    itc_d,
    itr_d,
    itr_i,
    ld2,
    ld2_c_clr,
    ld2_sa,
    ld8,
    ld8_a,
    ld8_c_nc,
    ld8_fill_postinc,
    ld8_postinc,
    ld8_s_postinc,
    lfetch_postinc,
    loadrs_enc,
    mov_ar,
    mov_ar_lc,
    mov_b_gr,
    mov_gr_b,
    mov_gr_psr_full,
    mov_dbr_indexed_write,
    mov_i_imm_ar,
    mov_lc_gr,
    mov_m_ar_gr,
    mov_m_cr_gr,
    mov_m_gr_ar,
    mov_m_gr_cr,
    mov_m_imm_ar,
    mov_m_psr_gr,
    mov_pkr_indexed,
    mov_gr_pr,
    mov_pr_gr,
    mov_pr_rot_imm,
    mov_rr_write,
    movl_mlx,
    nop_b,
    nop_f,
    nop_i,
    nop_m,
    or_reg,
    ptr_d,
    raw_bundle,
    require_exception,
    require_registers,
    rfi_b,
    rfi_to_gr,
    rsm,
    run_program,
    setf_sig,
    srlz_d,
    ssm,
    st2,
    st4,
    st8,
    st8_postinc,
    tnat_nz_or,
)
from .runner import read_stopped_state

# ── RSE tests ──


def _require_reset_rse_state(qemu, *, machine, cpu, cfm, partitions):
    result = read_stopped_state(qemu, machine=machine, cpu=cpu)
    fields = ("sof", "sol", "sor", "rrb_gr", "rrb_fr", "rrb_pr")
    observed_cfm = tuple(result.state.cfm[field] for field in fields)
    if observed_cfm != cfm:
        raise RuntimeError(
            f"{machine} reset CFM: expected {cfm!r}, got {observed_cfm!r}\n"
            f"reset state:\n{result.register_output}")

    match = re.search(
        r"RSE: bol=(\d+) dirty=(-?\d+)/(-?\d+) "
        r"clean=(-?\d+)/(-?\d+) invalid=(-?\d+)",
        result.register_output)
    if match is None:
        raise RuntimeError("info registers did not contain RSE partitions:\n" +
                           result.register_output)
    observed_partitions = tuple(int(value) for value in match.groups())
    if observed_partitions != partitions:
        raise RuntimeError(
            f"{machine} reset RSE: expected {partitions!r}, "
            f"got {observed_partitions!r}\n"
            f"reset state:\n{result.register_output}")


def test_rse_architectural_reset_exposes_full_frame(qemu):
    _require_reset_rse_state(
        qemu,
        machine="none",
        cpu="madison",
        cfm=(96, 0, 0, 0, 0, 0),
        partitions=(0, 0, 0, 0, 0, 0),
    )


def test_rse_boot_handoff_preserves_empty_frame(qemu):
    _require_reset_rse_state(
        qemu,
        machine="ia64-vpc",
        cpu=None,
        cfm=(0, 0, 0, 0, 0, 0),
        partitions=(0, 0, 0, 0, 0, 96),
    )


def _empty_frame_prologue(address, target):
    """Make tests that require an empty caller frame independent of reset."""
    return [
        (address, 0x00, alloc_m(2, 0, 0, 0, 0), nop_i(), nop_i()),
        (address + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(address + 0x10, target)),
    ]


test_alloc_m34_ignored_bits_decode = require_registers(
    "alloc_m34_ignored_bits_decode", [
        (0x10, 0x00, alloc_m(5, 80, 72, 9, 1,
                              ignored31=1, ignored36=1),
         nop_i(), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
        "r5": 0,
        "cfm_sof": 80,
        "cfm_sol": 72,
        "cfm_sor": 9,
    }, entry=0x10)

test_alloc_predicated_illegal = require_exception(
    "alloc_predicated_illegal", [
        (0x10, 0x00, alloc_m(5, 8, 8, 0, 0, qp=1), nop_i(),
         nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_alloc_rejects_frame_larger_than_register_stack = require_exception(
    "alloc_rejects_frame_larger_than_register_stack", [
        (0x10, 0x00, alloc_m(5, 97, 0, 0, 0), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_alloc_rejects_locals_larger_than_frame = require_exception(
    "alloc_rejects_locals_larger_than_frame", [
        (0x10, 0x00, alloc_m(5, 8, 9, 0, 0), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_alloc_rejects_rotating_region_larger_than_frame = require_exception(
    "alloc_rejects_rotating_region_larger_than_frame", [
        (0x10, 0x00, alloc_m(5, 0, 0, 1, 0), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_rse_alloc_call_ret = require_registers("rse_alloc_call_ret", [
    (0x10, 0x00, nop_m(), alloc(2, 1, 1, 0, 0),
     adds(1, 0x42, 0)),
    (0x20, 0x00, nop_m(), adds(2, 0x99, 0),
     nop_i()),
    (0x30, 0x10, nop_m(), nop_i(),
     br_call(0, 0x30, 0x50)),
    (0x50, 0x00, nop_m(), alloc(3, 1, 1, 0, 0),
     adds(2, 0x77, 0)),
    (0x60, 0x10, nop_m(), nop_i(),
     br_cond(0x60, 0x60)),
], {"ip": 0x60, "r1": 0x42, "r2": 0x77}, entry=0x10)

test_rse_call_sets_callee_input_frame = require_registers(
    "rse_call_sets_callee_input_frame", [
        (0x10, 0x00, nop_m(), alloc(2, 5, 2, 0, 0),
         nop_i()),
        (0x20, 0x00, nop_m(), addl(34, 0x11, 0),
         addl(35, 0x22, 0)),
        (0x30, 0x00, nop_m(), addl(36, 0x33, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_call(0, 0x40, 0x60)),
        (0x60, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r8": 0x11,
        "cfm_sof": 3,
        "cfm_sol": 0,
        "ar_pfs": 0x105,
    }, entry=0x10)

test_rse_nested_alloc_call_preserves_output_arg = require_registers(
    "rse_nested_alloc_call_preserves_output_arg", [
        (0x10, 0x00, nop_m(), alloc(2, 5, 4, 0, 0),
         nop_i()),
        (0x20, *movl_mlx(36, 0x123456789abcdef0)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call(0, 0x30, 0x80)),
        (0x80, 0x00, nop_m(), alloc(34, 5, 4, 0, 0),
         nop_i()),
        (0x90, 0x00, nop_m(), adds(36, 0, 32),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, 0xe0)),
        (0xe0, 0x00, nop_m(), alloc(34, 7, 4, 0, 0),
         nop_i()),
        (0xf0, 0x00, nop_m(), adds(36, 0, 32),
         adds(37, 0, 0)),
        (0x100, 0x00, nop_m(), addl(38, 560, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(0, 0x110, 0x150)),
        (0x150, 0x00, nop_m(), alloc(31, 3, 3, 0, 0),
         nop_i()),
        (0x160, 0x00, nop_m(), adds(8, 0, 32),
         adds(9, 0, 34)),
        (0x170, 0x10, nop_m(), nop_i(),
         br_cond(0x170, 0x170)),
    ], {
        "ip": 0x170,
        "r8": 0x123456789abcdef0,
        "r9": 560,
    }, entry=0x10)

test_rse_call_uses_high_sol_output_arg = require_registers(
    "rse_call_uses_high_sol_output_arg", [
        (0x10, 0x00, nop_m(), alloc(45, 19, 16, 0, 0),
         nop_i()),
        (0x20, *movl_mlx(48, 0xfedcba9876543210)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call(0, 0x30, 0x50)),
        (0x50, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r8": 0xfedcba9876543210,
        "cfm_sof": 3,
        "cfm_sol": 0,
        "ar_pfs": 0x813,
    }, entry=0x10)

test_rse_call_maps_all_high_output_args = require_registers(
    "rse_call_maps_all_high_output_args", [
        (0x10, 0x00, nop_m(), alloc(56, 32, 28, 0, 0), nop_i()),
        (0x20, *movl_mlx(60, 0x200)),
        (0x30, *movl_mlx(61, 1007)),
        (0x40, *movl_mlx(62, 1)),
        (0x50, 0x10, nop_m(), nop_i(), br_call(0, 0x50, 0x80)),
        (0x80, 0x00, nop_m(), or_reg(8, 32, 0), or_reg(9, 33, 0)),
        (0x90, 0x00, nop_m(), or_reg(10, 34, 0), add_one(11, 32, 33)),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
    ], {
        "ip": 0xa0,
        "r8": 0x200,
        "r9": 1007,
        "r10": 1,
        "r11": 0x5f0,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10)

test_rse_call_moves_output_nat_across_word_boundary = require_registers(
    "rse_call_moves_output_nat_across_word_boundary", [
        (0x10, 0x00, nop_m(), alloc(20, 96, 30, 0, 0), nop_i()),
        (0x20, 0x00, mov_m_imm_ar(36, 1),
         addl(6, 0x400, 0), nop_i()),
        (0x30, 0x08, ld8_fill_postinc(62, 6, 0), nop_i(), nop_i()),
        (0x40, 0x08, ld8_fill_postinc(64, 6, 0), nop_i(), nop_i()),
        (0x50, 0x08, ld8_fill_postinc(94, 6, 0), nop_i(), nop_i()),
        (0x60, 0x08, ld8_fill_postinc(126, 6, 0), nop_i(), nop_i()),
        # r33 starts NaT, but must be cleared by the source r63 bit.
        (0x70, 0x08, ld8_fill_postinc(33, 6, 0), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_call(0, 0x80, 0x100)),

        # The caller's r62..r127 outputs become the callee's r32..r97.
        # The terminal GR NaT map directly covers both sides of every bit-63
        # boundary; no guest-side predicate observer is involved.
        (0x100, 0x00, nop_m(), nop_i(), nop_i()),
        (0x110, 0x00, nop_m(), nop_i(), nop_i()),
        (0x120, 0x00, nop_m(), nop_i(), nop_i()),
        (0x130, 0x00, nop_m(), nop_i(), nop_i()),
        (0x140, 0x00, nop_m(), nop_i(), nop_i()),
        (0x150, 0x00, nop_m(), nop_i(), nop_i()),
        (0x160, 0x00, nop_m(), nop_i(), nop_i()),
        (0x170, 0x00, nop_m(), nop_i(), nop_i()),
        (0x180, 0x10, nop_m(), nop_i(), br_cond(0x180, 0x180)),
        (0x400, 0x00, 0, 0, 0),
    ], {
        "ip": 0x180,
        "exception": IA64_EXCP_NONE,
        "cfm_sof": 66,
        "cfm_sol": 0,
        "r32_nat": 1,
        "r33_nat": 0,
        "r34_nat": 1,
        "r63_nat": 0,
        "r64_nat": 1,
        "r65_nat": 0,
        "r96_nat": 1,
        "r97_nat": 0,
    }, entry=0x10)

test_rse_callee_alloc_stores_input_arg = require_registers(
    "rse_callee_alloc_stores_input_arg", [
        (0x10, 0x00, nop_m(), alloc(45, 19, 16, 0, 0),
         nop_i()),
        (0x20, *movl_mlx(48, 0x123456789abcdef0)),
        (0x30, *movl_mlx(12, 0x1000)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_call(0, 0x40, 0x80)),
        (0x80, 0x00, alloc_m(45, 24, 16, 0, 0), adds(12, -16, 12),
         nop_i()),
        (0x90, 0x00, nop_m(), adds(14, 16, 12),
         nop_i()),
        (0xa0, 0x00, st8(14, 32), nop_i(),
         nop_i()),
        (0xb0, 0x00, ld8(8, 14), nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
    }, entry=0x10)

test_rse_alloc_preserves_ar_pfs = require_registers(
    "rse_alloc_preserves_ar_pfs", [
        (0x10, 0x00, nop_m(), alloc(2, 5, 2, 0, 0),
         nop_i()),
        (0x20, 0x00, nop_m(), addl(34, 0x11, 0),
         nop_i()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call(0, 0x30, 0x50)),
        (0x50, 0x00, nop_m(), alloc(12, 6, 5, 0, 0),
         adds(8, 0, 32)),
        (0x60, 0x00, nop_m(), adds(9, 0, 12),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "r8": 0x11,
        "r9": 0x105,
        "cfm_sof": 6,
        "cfm_sol": 5,
        "ar_pfs": 0x105,
    }, entry=0x10)

test_rse_bsp_is_current_frame_base = require_registers(
    "rse_bsp_is_current_frame_base", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 3, 2, 0, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
    ], {
        "ip": 0x50,
        "r8": 0x100000,
        "cfm_sof": 3,
        "cfm_sol": 2,
    }, entry=0x10)

test_rse_call_ret_updates_bsp_base = require_registers(
    "rse_call_ret_updates_bsp_base", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_call(0, 0x40, 0x70)),
        (0x50, 0x00, mov_m_ar_gr(9, 17), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x70, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x60,
        "r8": 0x100018,
        "r9": 0x100000,
        "cfm_sof": 5,
        "cfm_sol": 3,
    }, entry=0x10)

test_rse_call_ret_preserves_caller_local = require_registers(
    "rse_call_ret_preserves_caller_local", [
        (0x10, 0x00, nop_m(), alloc(36, 8, 6, 0, 0), nop_i()),
        (0x20, *movl_mlx(34, 0x123456789abcdef0)),
        (0x30, 0x10, nop_m(), nop_i(),
         br_call(0, 0x30, 0x60)),
        (0x40, 0x00, nop_m(), adds(8, 0, 34), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_cond(0x50, 0x50)),
        (0x60, 0x00, nop_m(), alloc(34, 6, 4, 0, 0), nop_i()),
        (0x70, *movl_mlx(34, 0x0badf00d0badf00d)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x50,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 8,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_large_callee_preserves_high_caller_local = require_registers(
    "rse_large_callee_preserves_high_caller_local", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(56, 32, 28, 0, 0), nop_i()),
        (0x40, *movl_mlx(37, 1008)),
        (0x50, 0x10, nop_m(), nop_i(), br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 37), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(40, 96, 88, 0, 0), nop_i()),
        (0x110, *movl_mlx(37, 0x0badf00d0badf00d)),
        (0x120, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 1008,
        "exception": IA64_EXCP_NONE,
        "cfm_sof": 32,
        "cfm_sol": 28,
    }, entry=0x10)

test_rse_parent_spill_keeps_call_snapshot = require_registers(
    "rse_parent_spill_keeps_call_snapshot", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(36, 60, 50, 0, 0), nop_i()),
        (0x40, *movl_mlx(34, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 34), nop_i()),
        (0x70, 0x00, nop_m(), adds(9, 0, 36), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, 0x00, nop_m(), alloc(37, 60, 50, 0, 0),
         nop_i()),
        (0x110, *movl_mlx(34, 0x0badf00d0badf00d)),
        (0x120, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0,
        "cfm_sof": 60,
        "cfm_sol": 50,
    }, entry=0x10)

test_rse_call_preserves_same_bundle_local_write = require_registers(
    "rse_call_preserves_same_bundle_local_write", [
        (0x10, 0x00, nop_m(), alloc(53, 29, 23, 0, 0), nop_i()),
        (0x20, *movl_mlx(12, 0x1000)),
        (0x30, 0x00, nop_m(), adds(47, 66, 12), nop_i()),
        (0x40, 0x10, nop_m(), adds(47, 32, 12),
         br_call(0, 0x40, 0x100)),
        (0x50, 0x00, nop_m(), adds(8, 0, 47), nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x100, 0x00, nop_m(), alloc(32, 8, 6, 0, 0), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x60,
        "r8": 0x1020,
        "r47": 0x1020,
        "cfm_sof": 29,
        "cfm_sol": 23,
    }, entry=0x10)

test_rse_cover_skips_trailing_rnat_slot = require_registers(
    "rse_cover_skips_trailing_rnat_slot", [
        (0x10, *movl_mlx(3, 0x100108)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 30, 0, 0, 0),
         nop_i()),
        (0x40, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x50, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
    ], {
        "ip": 0x60,
        "r8": 0x100200,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x10)

test_rse_bspstore_preserves_dirty_partition_across_rnat = require_registers(
    "rse_bspstore_preserves_dirty_partition_across_rnat", [
        (0x10, *movl_mlx(3, 0x100108)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 30, 0, 0, 0),
         nop_i()),
        (0x40, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x50, *movl_mlx(3, 0x200108)),
        (0x60, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "r8": 0x200200,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x10)

"""mov-to-BSPSTORE makes RNAT undefined to software and to mandatory fills,
but it does not revoke the coherence guarantee for registers already backed
below BSPSTORE.  A later suffix spill must retain their collection bits."""
test_rse_bspstore_partial_rnat_store_preserves_backed_prefix = \
    require_registers(
        "rse_bspstore_partial_rnat_store_preserves_backed_prefix", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 0x6)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x100030)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(1, 60, 0, 0, 0), nop_i()),
        (0x70, 0x18, nop_m(), nop_m(), cover_b()),
        (0x80, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x90, *movl_mlx(3, 0x1001f8)),
        (0xa0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x6,
    }, entry=0x10)


def test_rse_partial_rnat_store_merge_all_split_points(qemu):
    """At every RNATBitIndex, preserve the backed prefix, replace the newly
    spilled suffix, and keep collection bit 63 zero."""
    for split in range(63):
        if split == 0:
            old = (1 << 64) - 1
            expected = 0
        else:
            old = ((1 << 63) | (1 << (split - 1)) | (1 << split))
            expected = 1 << (split - 1)
        run_program(qemu, [
            (0x10, *movl_mlx(3, 0x1001f8)),
            (0x20, *movl_mlx(4, old)),
            (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
            (0x40, *movl_mlx(3, 0x100000 + split * 8)),
            (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
            (0x60, 0x00, nop_m(), alloc(1, 63 - split, 0, 0, 0),
             nop_i()),
            (0x70, 0x18, nop_m(), nop_m(), cover_b()),
            (0x80, 0x00, flushrs_enc(), nop_i(), nop_i()),
            (0x90, *movl_mlx(3, 0x1001f8)),
            (0xa0, 0x00, ld8(8, 3), nop_i(), nop_i()),
            (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
        ], entry=0x10, terminal_ip=0xb0, expected={
            "exception": IA64_EXCP_NONE,
            "r8": expected,
        }, name=f"rse_partial_rnat_store_merge_split_{split}")


"""A context-return sequence can leave a dirty suffix beginning at bit 31 and
then execute loadrs, which makes AR.RNAT undefined.  The bit-24 register is
still backed below BSPSTORE: it must survive the subsequent collection store
even though mov-from-RNAT reads zero after loadrs."""
test_rse_loadrs_partial_rnat_store_preserves_backed_prefix = \
    require_registers(
        "rse_loadrs_partial_rnat_store_preserves_backed_prefix", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 1 << 24)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x1000f8)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(1, 32, 0, 0, 0), nop_i()),
        (0x70, 0x18, nop_m(), nop_m(), cover_b()),
        (0x80, *movl_mlx(3, (33 * 8) << 16)),
        (0x90, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0xa0, 0x00, loadrs_enc(), nop_i(), nop_i()),
        (0xb0, 0x00, mov_m_ar_gr(9, 19), nop_i(), nop_i()),
        (0xc0, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xd0, *movl_mlx(3, 0x1001f8)),
        (0xe0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 24,
        "r9": 0,
    }, entry=0x10)

"""A defined clear in the physical RNAT must not be reconstructed from an
older collection word after loadrs.  The saved image is writeback-only:
mov-from-RNAT still returns zero, while the later completed collection also
contains zero rather than resurrecting the stale bit-24 NaT."""
test_rse_loadrs_writeback_preserves_defined_zero = require_registers(
    "rse_loadrs_writeback_preserves_defined_zero", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 1 << 24)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x1000f8)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, *movl_mlx(4, 0)),
        (0x70, 0x00, mov_m_gr_ar(4, 19), nop_i(), nop_i()),
        (0x80, 0x00, loadrs_enc(), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_ar_gr(9, 19), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), alloc(1, 32, 0, 0, 0), nop_i()),
        (0xb0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xc0, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xd0, *movl_mlx(3, 0x1001f8)),
        (0xe0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 0,
    }, entry=0x10)


"""An explicit BSPSTORE/RNAT rewrite is authoritative over a writeback image
captured by loadrs.  This is the SDM 6.10 edit protocol with the saved image
armed: the edited register and its cleared NaT must both win."""
test_rse_loadrs_writeback_yields_to_bspstore_edit = require_registers(
    "rse_loadrs_writeback_yields_to_bspstore_edit", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(5, 1 << 24)),
        (0x30, 0x00, st8(3, 5), nop_i(), nop_i()),
        (0x40, *movl_mlx(20, 0x1000f8)),
        (0x50, 0x00, mov_m_gr_ar(20, 18), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_gr_ar(5, 19), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_ar_gr(11, 19), nop_i(), nop_i()),
        (0x80, 0x00, loadrs_enc(), nop_i(), nop_i()),
        (0x90, 0x00, mov_m_ar_gr(9, 19), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, 0x1000c0)),
        (0xb0, *movl_mlx(4, 0x1122334455667788)),
        (0xc0, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0xd0, *movl_mlx(5, 0)),
        (0xe0, 0x00, mov_m_gr_ar(20, 18), nop_i(), nop_i()),
        (0xf0, 0x00, mov_m_gr_ar(5, 19), nop_i(), nop_i()),
        (0x100, 0x00, nop_m(), alloc(1, 32, 0, 0, 0), nop_i()),
        (0x110, 0x18, nop_m(), nop_m(), cover_b()),
        (0x120, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x130, *movl_mlx(3, 0x1001f8)),
        (0x140, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0x150, *movl_mlx(3, 0x1000c0)),
        (0x160, 0x00, ld8(10, 3), nop_i(), nop_i()),
        (0x170, 0x10, nop_m(), nop_i(), br_cond(0x170, 0x170)),
    ], {
        "ip": 0x170,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 0,
        "r10": 0x1122334455667788,
        "r11": 1 << 24,
    }, entry=0x10)


def test_rse_bspstore_rnat_edit_protocol_overrides_memory_image(qemu):
    """SDM 6.10's edit protocol must win in both directions: read RNAT,
    edit a backed register and its saved NaT, rewrite the same BSPSTORE, then
    rewrite RNAT.  No implementation-only memory image may override it."""
    for old_nat, new_nat in ((1, 0), (0, 1)):
        old_collection = old_nat << 24
        new_collection = new_nat << 24
        edited_data = (0xaaaabbbbccccdddd if new_nat else
                       0x1111222233334444)

        run_program(qemu, [
            (0x10, *movl_mlx(3, 0x1001f8)),
            (0x20, *movl_mlx(4, old_collection)),
            (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
            (0x40, *movl_mlx(20, 0x1000f8)),
            (0x50, 0x00, mov_m_gr_ar(20, 18), nop_i(), nop_i()),
            (0x60, *movl_mlx(5, old_collection)),
            (0x70, 0x00, mov_m_gr_ar(5, 19), nop_i(), nop_i()),
            (0x80, 0x00, mov_m_ar_gr(5, 19), nop_i(), nop_i()),
            (0x90, *movl_mlx(3, 0x1000c0)),
            (0xa0, *movl_mlx(4, edited_data)),
            (0xb0, 0x00, st8(3, 4), nop_i(), nop_i()),
            (0xc0, *movl_mlx(5, new_collection)),
            (0xd0, 0x00, mov_m_gr_ar(20, 18), nop_i(), nop_i()),
            (0xe0, 0x00, mov_m_gr_ar(5, 19), nop_i(), nop_i()),
            (0xf0, 0x00, nop_m(), alloc(1, 32, 0, 0, 0), nop_i()),
            (0x100, 0x18, nop_m(), nop_m(), cover_b()),
            (0x110, 0x00, flushrs_enc(), nop_i(), nop_i()),
            (0x120, *movl_mlx(3, 0x1001f8)),
            (0x130, 0x00, ld8(8, 3), nop_i(), nop_i()),
            (0x140, *movl_mlx(3, 0x1000c0)),
            (0x150, 0x00, ld8(9, 3), nop_i(), nop_i()),
            (0x160, 0x10, nop_m(), nop_i(), br_cond(0x160, 0x160)),
        ], entry=0x10, terminal_ip=0x160, expected={
            "exception": IA64_EXCP_NONE,
            "r8": new_collection,
            "r9": edited_data,
        }, name=f"rse_bspstore_rnat_edit_{old_nat}_to_{new_nat}")


"""Only RNAT{RNATBitIndex:0} has a defined source after mov-to-RNAT.
Undefined high bits read as zero under the target policy, and even a
same-value mov-to-BSPSTORE makes the complete RNAT value undefined."""
test_rse_rnat_defined_mask_and_bspstore_invalidation = require_registers(
    "rse_rnat_defined_mask_and_bspstore_invalidation", [
        (0x10, *movl_mlx(3, 0x100030)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(4, 0x7fffffffffffffff)),
        (0x40, 0x00, mov_m_gr_ar(4, 19), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_ar_gr(8, 19), nop_i(), nop_i()),
        (0x60, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_ar_gr(9, 19), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r8": 0x7f,
        "r9": 0,
    }, entry=0x10)

test_rse_loadrs_bspstore_return_uses_covered_frame = require_registers(
    "rse_loadrs_bspstore_return_uses_covered_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 8, 0, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(36, 0x123456789abcdef0)),
        (0x50, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x60, *movl_mlx(3, 64 << 16)),
        (0x70, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x80, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(3, 0x200000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, nop_m(), alloc(39, 8, 0, 0, 0),
         nop_i()),
        (0xc0, *movl_mlx(3, 0x308)),
        (0xd0, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0xe0, *movl_mlx(3, 0x110)),
        (0xf0, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0x100, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x110, 0x00, nop_m(), adds(8, 0, 34),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 8,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_loadrs_zero_current_frame_invalidates_parents = require_registers(
    "rse_loadrs_zero_current_frame_invalidates_parents", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(40, 9, 5, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(35, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x200)),
        (0x70, 0x00, nop_m(), adds(8, 0, 35),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),

        (0x100, 0x00, nop_m(), alloc(35, 4, 0, 0, 0),
         nop_i()),
        (0x110, 0x00, mov_m_ar_gr(20, 17), mov_gr_b(21, 0),
         nop_i()),
        (0x120, 0x00, nop_m(), adds(21, 16, 21),
         nop_i()),
        (0x130, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(22, 0x200000)),
        (0x150, 0x00, st8(22, 35), nop_i(),
         nop_i()),
        (0x160, *movl_mlx(22, 0x200008)),
        (0x170, 0x00, st8(22, 20), nop_i(),
         nop_i()),
        (0x180, *movl_mlx(22, 0x200010)),
        (0x190, 0x00, st8(22, 21), nop_i(),
         nop_i()),
        (0x1a0, 0x10, nop_m(), nop_i(),
         br_ret(0)),

        (0x200, 0x00, nop_m(), alloc(38, 7, 5, 0, 0),
         nop_i()),
        (0x210, *movl_mlx(35, 0x206)),
        (0x220, 0x10, nop_m(), nop_i(),
         br_call(0, 0x220, 0x300)),
        (0x230, 0x10, nop_m(), nop_i(),
         br_ret(0)),

        (0x300, 0x00, nop_m(), alloc(33, 2, 0, 0, 0),
         nop_i()),
        (0x310, *movl_mlx(3, 0x200000)),
        (0x320, 0x00, ld8(14, 3), nop_i(),
         nop_i()),
        (0x330, *movl_mlx(3, 0x200008)),
        (0x340, 0x00, ld8(15, 3), nop_i(),
         nop_i()),
        (0x350, *movl_mlx(3, 0x200010)),
        (0x360, 0x00, ld8(16, 3), nop_i(),
         nop_i()),
        (0x370, 0x00, mov_m_gr_ar(0, 16), nop_i(),
         nop_i()),
        (0x380, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x390, 0x00, mov_m_gr_ar(14, 64), nop_i(),
         nop_i()),
        (0x3a0, 0x00, mov_ar(15, 18), nop_i(),
         nop_i()),
        (0x3b0, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 16)),
        (0x3c0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 9,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_call_ret_preserves_region7_bsp = require_registers(
    "rse_call_ret_preserves_region7_bsp", [
        (0x10, *movl_mlx(3, 0xe000000000100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(19, 1 << 17)),
        (0x40, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x50, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x90)),
        (0x70, 0x00, mov_m_ar_gr(9, 17), nop_i(),
         nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x90, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "r8": 0xe000000000100018,
        "r9": 0xe000000000100000,
        "cfm_sof": 5,
        "cfm_sol": 3,
    }, entry=0x10)

test_rse_flushrs_clears_stale_rnat = require_registers(
    "rse_flushrs_clears_stale_rnat", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 1 << 3)),
        (0x40, 0x00, mov_m_gr_ar(4, 19), nop_i(),
         nop_i()),
        (0x50, 0x00, nop_m(), alloc(36, 8, 5, 0, 0),
         nop_i()),
        (0x60, 0x00, nop_m(), mov_gr_b(35, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x100, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x110, 0x00, mov_m_ar_gr(8, 19), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
    }, entry=0x10)

test_rse_merced_flushrs_invalidates_spilled_frame = require_registers(
    "rse_merced_flushrs_invalidates_spilled_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(), nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 32), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x110, *movl_mlx(3, 0x100000)),
        (0x120, *movl_mlx(4, 0x2222)),
        (0x130, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x140, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0x2222,
        "exception": IA64_EXCP_NONE,
    }, entry=0x10, cpu="merced")

"""Merced has no clean partition, but the backing-store coherence rule is the
same: a suffix spill retains the collection prefix below BSPSTORE."""
test_rse_merced_partial_rnat_store_preserves_backed_prefix = \
    require_registers(
        "rse_merced_partial_rnat_store_preserves_backed_prefix", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 1 << 11)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, 0x100160)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(1, 19, 0, 0, 0), nop_i()),
        (0x70, 0x18, nop_m(), nop_m(), cover_b()),
        (0x80, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x90, *movl_mlx(3, 0x1001f8)),
        (0xa0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 11,
    }, entry=0x10, cpu="merced")

test_rse_merced_return_publishes_filled_rnat_collection = require_registers(
    "rse_merced_return_publishes_filled_rnat_collection", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 96, 88, 0, 0), nop_i()),
        (0x40, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x50, 0x08, ld8_fill_postinc(88, 6, 0), nop_i(), nop_i()),
        (0x60, 0x18, nop_m(), nop_m(), cover_b()),
        (0x70, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x80, *movl_mlx(4, 40 | (40 << 7))),
        (0x90, 0x00, mov_m_gr_ar(4, 64), nop_i(), nop_i()),
        (0xa0, *movl_mlx(5, 0x100)),
        (0xb0, 0x09, nop_m(), nop_m(), mov_b_gr(0, 5)),
        (0xc0, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x100, 0x00, mov_m_ar_gr(8, 19), nop_i(), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x110)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 56,
        "cfm_sof": 40,
        "cfm_sol": 40,
    }, entry=0x10, cpu="merced")

"""A mandatory return fill publishes the completed NaT collection in RNAT.
If the restored suffix of that collection is covered and spilled again, the
RSE must preserve the already-backed lower prefix when it writes the
collection word.  In particular, rebinding the spill state must not discard
the matching fill-side collection that mov-from-RNAT just exposed."""
test_rse_merced_respill_preserves_filled_rnat_prefix = require_registers(
    "rse_merced_respill_preserves_filled_rnat_prefix", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 63, 0, 0, 0), nop_i()),
        (0x40, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x400, 0), nop_i()),
        (0x50, 0x08, ld8_fill_postinc(42, 6, 0), nop_i(), nop_i()),
        (0x60, 0x18, nop_m(), nop_m(), cover_b()),
        (0x70, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x80, *movl_mlx(3, 0x1001f8)),
        (0x90, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xa0, *movl_mlx(4, 40 | (40 << 7))),
        (0xb0, 0x00, mov_m_gr_ar(4, 64), nop_i(), nop_i()),
        (0xc0, *movl_mlx(5, 0x100)),
        (0xd0, 0x09, nop_m(), nop_m(), mov_b_gr(0, 5)),
        (0xe0, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x100, 0x00, mov_m_ar_gr(9, 19), nop_i(), nop_i()),
        (0x110, 0x18, nop_m(), nop_m(), cover_b()),
        (0x120, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x130, *movl_mlx(3, 0x1001f8)),
        (0x140, 0x00, ld8(10, 3), nop_i(), nop_i()),
        (0x150, 0x10, nop_m(), nop_i(), br_cond(0x150, 0x150)),
        (0x400, 0x00, 0, 0, 0),
    ], {
        "ip": 0x150,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 10,
        "r9": 1 << 10,
        "r10": 1 << 10,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x10, cpu="merced")

"""A fill-side NaT collection displaced by another collection remains part
of the physical RSE state across loadrs.  The first synthetic return restores
a NaT in a partial upper collection, then lower fills displace that collection
from the one-entry load latch.  An interruption covers the frame and executes
loadrs with a 30-word tear point, making the NaT-bearing physical register
invalid before its partial collection word has reached memory.  Growing an
80-register handler frame crosses that collection word and reuses its physical
slot.  The writeback must merge the retained NaT image so rfi and the two
ordinary returns restore the original NaT instead of silently clearing it."""
test_rse_displaced_dispersal_survives_loadrs_physical_reuse = \
    require_registers(
        "rse_displaced_dispersal_survives_loadrs_physical_reuse", [
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100100)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(39, 96, 88, 0, 0), nop_i()),
        (0x70, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x400, 0),
         nop_i()),
        (0x80, 0x08, ld8_fill_postinc(127, 6, 0), nop_i(), nop_i()),
        (0x90, 0x18, nop_m(), nop_m(), cover_b()),
        (0xa0, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xb0, *movl_mlx(4, 96 | (88 << 7))),
        (0xc0, 0x00, mov_m_gr_ar(4, 64), nop_i(), nop_i()),
        (0xd0, *movl_mlx(5, 0x140)),
        (0xe0, 0x09, nop_m(), nop_m(), mov_b_gr(0, 5)),
        (0xf0, 0x10, nop_m(), nop_i(), br_ret(0)),

        (0x140, 0x10, nop_m(), nop_i(), br_call(0, 0x140, 0x200)),
        (0x150, 0x00, nop_m(), nop_i(), nop_i()),
        (0x160, 0x10, nop_m(), nop_i(), br_cond(0x160, 0x160)),
        (0x200, 0x00, nop_m(), alloc(40, 90, 80, 0, 0), nop_i()),
        (0x210, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x220, 0x10, nop_m(), nop_i(), br_ret(0)),

        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, (30 * 8) << 16)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(20, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        # Handler BOL is 90.  Its virtual offset five therefore reuses p95,
        # the physical home of the interrupted frame's NaT-bearing r119.
        (IA64_BREAK_VECTOR + 0x40, 0x00, nop_m(),
         alloc(32, 80, 80, 0, 0), nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x01, mov_m_ar_gr(8, 19),
         adds(37, 0, 0), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x00, nop_m(),
         alloc(14, 0, 0, 0, 0), nop_i()),
        (IA64_BREAK_VECTOR + 0x70, *movl_mlx(20, 0x220)),
        (IA64_BREAK_VECTOR + 0x80, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x90, 0x10, nop_m(), nop_i(), rfi_b()),
        raw_bundle(0x400, 0x123456789abcdef0, 0),
    ], {
        "ip": 0x160,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r119": 0x123456789abcdef0,
        "r119_nat": 1,
        "cfm_sof": 96,
        "cfm_sol": 88,
    }, entry=0x10, cpu="merced")

test_rse_cover_flushrs_spills_covered_frame = require_registers(
    "rse_cover_flushrs_spills_covered_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, *movl_mlx(33, 0x2222)),
        (0x60, *movl_mlx(34, 0x3333)),
        (0x70, *movl_mlx(35, 0x4444)),
        (0x80, *movl_mlx(36, 0x5555)),
        (0x90, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0xa0, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0xb0, *movl_mlx(4, 0x100020)),
        (0xc0, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0xd0, 0x00, ld8(9, 4), nop_i(),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
    ], {
        "ip": 0xe0,
        "r8": 0x1111,
        "r9": 0x5555,
    }, entry=0x10)

test_rse_tracked_return_redirties_reused_frame = require_registers(
    "rse_tracked_return_redirties_reused_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 8, 4, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, *movl_mlx(32, 0x2222)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x120)),
        (0x80, *movl_mlx(3, 0x100000)),
        (0x90, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x120, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xa0,
        "r8": 0x2222,
    }, entry=0x10)

test_rse_nested_return_restores_bspstore_base = require_registers(
    "rse_nested_return_restores_bspstore_base", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 27, 22, 0, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_call(6, 0x40, 0x100)),
        (0x50, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_ar_gr(9, 18), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(33, 32, 27, 0, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(7, 0x110, 0x180)),
        (0x120, 0x00, nop_m(), mov_m_gr_ar(33, 64),
         nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (0x180, 0x00, nop_m(), alloc(1, 13, 11, 0, 0),
         nop_i()),
        (0x190, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x1a0, 0x10, nop_m(), nop_i(),
         br_ret(7)),
    ], {
        "ip": 0x70,
        "r8": 0x100000,
        "r9": 0x100000,
        "cfm_sof": 27,
        "cfm_sol": 22,
    }, entry=0x10)

def deep_rse_return_program(depth):
    bundles = [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 3, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(34, 0x5a)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 34),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
    ]

    for i in range(depth):
        base = 0x100 + i * 0x60
        bundles.append((base, 0x00, alloc(32, 3, 3, 0, 0), nop_i(),
                        nop_i()))
        bundles.append((base + 0x10, 0x00, nop_m(), mov_gr_b(33, 0),
                        nop_i()))
        if i + 1 < depth:
            bundles.append((base + 0x20, 0x10, nop_m(), nop_i(),
                            br_call(0, base + 0x20, base + 0x60)))
        else:
            bundles.append((base + 0x20, 0x00, nop_m(), nop_i(),
                            nop_i()))
        bundles.append((base + 0x30, 0x00, mov_m_gr_ar(32, 64), nop_i(),
                        nop_i()))
        bundles.append((base + 0x40, 0x09, nop_m(), nop_m(),
                        mov_b_gr(0, 33)))
        bundles.append((base + 0x50, 0x10, nop_m(), nop_i(),
                        br_ret(0)))

    return bundles

test_rse_deep_call_chain_spills_parent_frames = require_registers(
    "rse_deep_call_chain_spills_parent_frames",
    deep_rse_return_program(140), {
        "ip": 0x70,
        "r8": 0x5a,
    }, entry=0x10)

test_rse_evict_parent_frames_preserves_caller_local = require_registers(
    "rse_evict_parent_frames_preserves_caller_local", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(39, 14, 9, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(34, 0x123456789abcdef0)),
        (0x50, *movl_mlx(35, 0x0fedcba987654321)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x100)),
        (0x70, 0x00, nop_m(), adds(8, 0, 34),
         adds(9, 0, 35)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, 0x00, nop_m(), alloc(39, 15, 9, 0, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(6, 0x110, 0x180)),
        (0x120, 0x00, nop_m(), mov_m_gr_ar(39, 64), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x180, 0x00, nop_m(), alloc(40, 90, 80, 0, 0),
         nop_i()),
        (0x190, 0x00, nop_m(), mov_m_gr_ar(40, 64), nop_i()),
        (0x1a0, 0x10, nop_m(), nop_i(), br_ret(6)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0x0fedcba987654321,
        "cfm_sof": 14,
        "cfm_sol": 9,
    }, entry=0x10)

test_rse_untracked_return_uses_each_rnat_collection = require_registers(
    "rse_untracked_return_uses_each_rnat_collection", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(39, 96, 88, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(32, 0x123456789abcdef0)),
        (0x50, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x60, 0x08, ld8_fill_postinc(95, 6, 0), nop_i(),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (0x100, 0x00, nop_m(), alloc(40, 90, 80, 0, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r32_nat": 0,
        "r95_nat": 1,
        "cfm_sof": 96,
        "cfm_sol": 88,
    }, entry=0x10)

"""A return that does not reach the collection word must not modify it.
The no-merge rule applies when the RSE actually spills RNAT; it does not
authorize changing unrelated backing memory without an RSE store."""
test_rse_return_reclaims_clean_keeps_unreached_rnat = require_registers(
    "rse_return_reclaims_clean_keeps_unreached_rnat", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 1 << 57)),
        (0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(3, 0x1001a8)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(39, 14, 10, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(36, 0x123456789abcdef0)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xb0, *movl_mlx(3, 0x1001f8)),
        (0xc0, 0x00, ld8(10, 3), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x100, 0x00, nop_m(), alloc(40, 90, 80, 0, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r36_nat": 0,
        "r10": 1 << 57,
        "cfm_sof": 14,
        "cfm_sol": 10,
    }, entry=0x10)

"""A br.ret that reclaims clean registers moves AR.BSPSTORE down without
any RSE traffic.  Here r95 is spilled with its NaT set into the second
collection (bit 0), the return rebases BSPSTORE into the first collection
at bit 56, and a shortened frame completes exactly that first collection.
The partial second collection must remain visible when BSPSTORE reaches it
again even though no register from that collection was spilled twice."""
test_rse_return_reclaims_clean_rebases_rnat_collection = require_registers(
    "rse_return_reclaims_clean_rebases_rnat_collection", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 96, 88, 0, 0),
         nop_i()),
        (0x40, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x50, 0x08, ld8_fill_postinc(95, 6, 0), nop_i(),
         nop_i()),
        (0x60, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x70, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(4, 40 | (40 << 7))),
        (0x90, 0x00, mov_m_gr_ar(4, 64), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(5, 0x100)),
        (0xb0, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 5)),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x100, 0x00, nop_m(), alloc(1, 7, 0, 0, 0),
         nop_i()),
        (0x110, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x120, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x130, 0x00, mov_m_ar_gr(8, 19), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(7, 0x1001f8)),
        (0x150, 0x00, ld8(10, 7), nop_i(),
         nop_i()),
        (0x160, 0x10, nop_m(), nop_i(),
         br_cond(0x160, 0x160)),
        (0x200, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x160,
        "exception": IA64_EXCP_NONE,
        "r8": 1,
        "r10": 0,
    }, entry=0x10)

RSE_TRIM_RNAT_DATA = bundle_words(0x00, 0x123456789abcdef0, 0, 0)[0]

test_rse_untracked_return_resyncs_trimmed_rnat = require_registers(
    "rse_untracked_return_resyncs_trimmed_rnat", [
        (0x10, *movl_mlx(3, 0x100f38)),
        (0x20, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, 1 << 17)),
        (0x40, 0x00, mov_m_gr_ar(3, 19), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(3, 72 | (72 << 7))),
        (0x60, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(3, 0xa0)),
        (0x80, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0xa0, *movl_mlx(3, 0x815)),
        (0xb0, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(3, 0xf0)),
        (0xd0, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0xf0, 0x00, nop_m(), nop_i(), nop_i()),
        (0x100, 0x00, nop_m(), nop_i(), nop_i()),
        (0x110, 0x00, nop_m(), adds(10, 0, 35),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (0x100c88, 0x00, 0x123456789abcdef0, 0,
         0),
        (0x100df8, 0x00, 0, 0,
         0),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r35_nat": 0,
        "r10": RSE_TRIM_RNAT_DATA,
        "cfm_sof": 21,
        "cfm_sol": 16,
    }, entry=0x10)

test_rse_bspstore_keeps_saved_frame = require_registers(
    "rse_bspstore_keeps_saved_frame", [
        (0x10, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_call(0, 0x20, 0x50)),
        (0x30, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (0x50, *movl_mlx(3, 0x100000)),
        (0x60, 0x00, mov_ar(3, 18), addl(20, 0x99, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {"ip": 0x40, "r8": 0x5a}, entry=0x10)

test_rse_firmware_unmatched_return_restores_matching_frame = require_registers(
    "rse_firmware_unmatched_return_restores_matching_frame", [
        (0x10, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_call(0, 0x20, 0x100100)),
        (0x30, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (0x100100, 0x00, nop_m(), alloc(2, 1, 0, 0, 0),
         nop_i()),
        (0x100110, 0x10, nop_m(), nop_i(),
         br_call(7, 0x100110, 0x100130)),
        (0x100130, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {"ip": 0x40, "r8": 0x5a}, entry=0x10)

test_rse_untracked_return_redirties_restored_frame = require_registers(
    "rse_untracked_return_redirties_restored_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, *movl_mlx(32, 0x2222)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x160)),
        (0x80, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x100, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x110, *movl_mlx(3, 0x200000)),
        (0x120, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x130, *movl_mlx(3, 0x100018)),
        (0x140, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x160, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x170, *movl_mlx(3, 0x200000)),
        (0x180, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x190, *movl_mlx(3, 0x100018)),
        (0x1a0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x1b0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {"ip": 0x90, "r8": 0x2222}, entry=0x10)

test_rse_untracked_return_restores_high_caller_local = require_registers(
    "rse_untracked_return_restores_high_caller_local", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(37, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x110, *movl_mlx(3, 0x200000)),
        (0x120, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x130, *movl_mlx(3, 0x100030)),
        (0x140, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_loadrs_cover_span_restores_embedded_frame = require_registers(
    "rse_loadrs_cover_span_restores_embedded_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(37, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, *movl_mlx(3, 0x200000)),
        (0x110, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x120, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x130, 0x00, nop_m(), alloc(2, 0, 0, 0, 0),
         nop_i()),
        (0x140, *movl_mlx(3, 64 << 16)),
        (0x150, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x160, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(3, 0x0ffff0)),
        (0x180, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_loadrs_cover_span_uses_preserved_sol = require_registers(
    "rse_loadrs_cover_span_uses_preserved_sol", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(37, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, *movl_mlx(3, 0x200000)),
        (0x110, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x120, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x130, 0x00, nop_m(), alloc(2, 0, 0, 0, 0),
         nop_i()),
        (0x140, *movl_mlx(3, 64 << 16)),
        (0x150, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x160, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(3, 0x0ffff0)),
        (0x180, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x190, 0x00, nop_m(), alloc(38, 7, 0, 0, 0),
         nop_i()),
        (0x1a0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_zero_sol_cover_return_restores_bsp_base = require_registers(
    "rse_zero_sol_cover_return_restores_bsp_base", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(37, 0x123456789abcdef0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, mov_m_ar_gr(14, 64), nop_i(),
         nop_i()),
        (0x110, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x120, *movl_mlx(15, 56 << 16)),
        (0x130, 0x00, mov_m_gr_ar(15, 16), nop_i(),
         nop_i()),
        (0x140, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x150, *movl_mlx(15, 1 | (1 << 7))),
        (0x160, 0x00, mov_m_gr_ar(15, 64), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(16, 0x1b0)),
        (0x180, 0x09, nop_m(), nop_m(),
         mov_b_gr(6, 16)),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (0x1b0, 0x00, mov_m_gr_ar(14, 64), nop_i(),
         nop_i()),
        (0x1c0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
        "r37": 0x123456789abcdef0,
    }, entry=0x10)

test_rse_loadrs_zero_sol_return_keeps_bsp_without_cover = require_registers(
    "rse_loadrs_zero_sol_return_keeps_bsp_without_cover", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(1, 8, 0, 0, 0),
         nop_i()),
        (0x40, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x50, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(3, 64 << 16)),
        (0x70, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x80, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(3, 0x8)),
        (0xa0, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(3, 0xe0)),
        (0xc0, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0xe0, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(),
         br_cond(0xf0, 0xf0)),
    ], {
        "ip": 0xf0,
        "r8": 0x100040,
        "cfm_sof": 8,
        "cfm_sol": 0,
    }, entry=0x10)
HIGH_TR_TARGET = HIGH_TR_BASE + 0x8430
HIGH_TR_PSR = ((1 << 13) | (1 << 17) | (1 << 27) |
               (1 << 36) | (1 << 44))


test_rsc_write_clips_pl_to_cpl = require_registers(
    "rsc_write_clips_pl_to_cpl", [
        (0x10, *movl_mlx(19, IA64_PSR_CPL3)),
        (0x20, 0x00, nop_m(), adds(31, 0x50, 0), nop_i()),
        *rfi_to_gr(0x30, 19, 31),
        (0x50, 0x00, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x60, 0x00, mov_m_ar_gr(8, 16), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r8": IA64_RSC_PL3,
    }, entry=0x10)

test_rse_uses_rsc_pl_for_access_rights = require_registers(
    "rse_uses_rsc_pl_for_access_rights", [
        *_empty_frame_prologue(0x800, 0x10),
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, 0x00, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x80, *movl_mlx(3, HIGH_TR_BASE + 0x8000)),
        (0x90, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0xb0, *movl_mlx(32, 0x123456789abcdef0)),
        (0xc0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xd0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT |
                         IA64_PSR_CPL3)),
        (0xe0, *movl_mlx(6, 0x130)),
        # Keep the covered frame dirty while rfi installs CPL3.
        (0xf0, 0x00, mov_m_gr_cr(0, 23), nop_i(), nop_i()),
        *rfi_to_gr(0x100, 19, 6),
        (0x120, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(), br_cond(0x130, 0x130)),
        (IA64_DATA_ACCESS_VECTOR, 0x10, nop_m(), adds(31, 0x71, 0),
         br_cond(IA64_DATA_ACCESS_VECTOR, IA64_DATA_ACCESS_VECTOR)),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r31": 0,
    }, entry=0x800)

test_rse_rt_enables_protection_key_checks = require_registers(
    "rse_rt_enables_protection_key_checks", [
        (0x10, *movl_mlx(18, 0x0010000000400661)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE)),
        (0x30, *movl_mlx(21, (KEY_TEST_KEY << 8) | (16 << 2))),
        (0x40, 0x00, mov_m_gr_cr(19, 20), adds(10, 5, 0), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(21, 21), nop_i(), nop_i()),
        (0x60, 0x00, itr_d(10, 18), nop_i(), nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, 0x00, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x90, *movl_mlx(3, HIGH_TR_BASE + 0x8000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0xc0, *movl_mlx(32, 0x123456789abcdef0)),
        (0xd0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xe0, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_RT | IA64_PSR_PK)),
        (0xf0, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x100, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x110, 0x00, flushrs_enc(), nop_i(), nop_i()),
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
        "r30": HIGH_TR_BASE + 0x8000,
        "r31": IA64_ISR_W | IA64_ISR_RS,
    }, entry=0x10)


# A matching DBR is below Data Key Miss in the mandatory-RSE priority order.
# With PSR.dt clear, the pre-debug translation must still gate keys with
# PSR.rt; otherwise the debug precheck would incorrectly reach 0x5900 first.
test_rse_key_miss_precedes_mandatory_data_debug_with_dt_clear = require_registers(
    "rse_key_miss_precedes_mandatory_data_debug_with_dt_clear", [
        (0x10, *movl_mlx(18, 0x0010000000400661)),
        (0x20, *movl_mlx(19, HIGH_TR_BASE)),
        (0x30, *movl_mlx(21, (KEY_TEST_KEY << 8) | (16 << 2))),
        (0x40, 0x00, mov_m_gr_cr(19, 20), adds(10, 5, 0), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(21, 21), nop_i(), nop_i()),
        (0x60, 0x00, itr_d(10, 18), nop_i(), nop_i()),
        (0x70, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x80, *movl_mlx(4, 0)),
        (0x90, *movl_mlx(5, HIGH_TR_BASE + 0x8000)),
        (0xa0, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        (0xc0, *movl_mlx(5, 0x41ffffffffffffff)),
        (0xd0, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0xe0, 0x00, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0xf0, *movl_mlx(3, HIGH_TR_BASE + 0x8000)),
        (0x100, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x110, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x120, *movl_mlx(32, 0x123456789abcdef0)),
        (0x130, 0x18, nop_m(), nop_m(), cover_b()),
        (0x140, *movl_mlx(
            19, IA64_PSR_IC | IA64_PSR_RT | IA64_PSR_PK | IA64_PSR_DB)),
        (0x150, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x160, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x170, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x10, 0x00, mov_m_cr_gr(31, 17),
         nop_i(), nop_i()),
        (IA64_DATA_KEY_MISS_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DATA_KEY_MISS_VECTOR + 0x20,
                 IA64_DATA_KEY_MISS_VECTOR + 0x20)),
        (IA64_DEBUG_VECTOR, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DEBUG_VECTOR, IA64_DEBUG_VECTOR)),
    ], {
        "ip": IA64_DATA_KEY_MISS_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": HIGH_TR_BASE + 0x8000,
        "r31": IA64_ISR_W | IA64_ISR_RS,
    }, entry=0x10)


"""The RSE collection write is permitted by the PKR while ordinary reads are
disabled.  Preserving the old prefix must therefore use the resolved store
target privately; a guest-visible read would raise Key Permission."""
test_rse_write_only_rnat_store_preserves_backed_prefix = require_registers(
    "rse_write_only_rnat_store_preserves_backed_prefix", [
        (0x10, *movl_mlx(3, 0x4081f8)),
        (0x20, *movl_mlx(4, 1 << 2)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(18, 0x400000 | DTR_PTE_WB)),
        (0x50, *movl_mlx(20, HIGH_TR_BASE)),
        (0x60, *movl_mlx(21, (KEY_TEST_KEY << 8) | (16 << 2))),
        (0x70, 0x00, mov_m_gr_cr(20, 20), adds(10, 5, 0), nop_i()),
        (0x80, 0x00, mov_m_gr_cr(21, 21), nop_i(), nop_i()),
        (0x90, 0x00, itr_d(10, 18), nop_i(), nop_i()),
        (0xa0, 0x00, srlz_d(), nop_i(), nop_i()),
        (0xb0, *movl_mlx(4, IA64_PKR_VALID | IA64_PKR_RD |
                         (KEY_TEST_KEY << 8))),
        (0xc0, 0x00, adds(3, 0, 0), nop_i(), nop_i()),
        (0xd0, 0x00, mov_pkr_indexed(3, 4, bit36=1), nop_i(), nop_i()),
        (0xe0, *movl_mlx(3, HIGH_TR_BASE + 0x8030)),
        (0xf0, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x100, 0x00, nop_m(), alloc(1, 60, 0, 0, 0), nop_i()),
        (0x110, 0x18, nop_m(), nop_m(), cover_b()),
        (0x120, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_RT | IA64_PSR_PK)),
        (0x130, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x140, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x150, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x160, *movl_mlx(19, 0)),
        (0x170, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x180, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x190, *movl_mlx(3, 0x4081f8)),
        (0x1a0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0x1b0, 0x10, nop_m(), nop_i(), br_cond(0x1b0, 0x1b0)),
    ], {
        "ip": 0x1b0,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 2,
    }, entry=0x10)

test_rse_big_endian_backing_store = require_registers(
    "rse_big_endian_backing_store", [
        (0x10, *movl_mlx(3, IA64_RSC_BE)),
        (0x20, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x100000)),
        (0x40, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x60, *movl_mlx(32, 0x1122334455667788)),
        (0x70, 0x18, nop_m(), nop_m(), cover_b()),
        (0x80, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x90, *movl_mlx(3, 0x100000)),
        (0xa0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_cond(0xb0, 0xb0)),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x8877665544332211,
    }, entry=0x10)

test_rse_big_endian_partial_rnat_store_preserves_backed_prefix = \
    require_registers(
        "rse_big_endian_partial_rnat_store_preserves_backed_prefix", [
        # Ordinary stores are little-endian here.  These bytes are the
        # big-endian RSE representation of collection bit 2.
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 0x0400000000000000)),
        (0x30, 0x00, st8(3, 4), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, IA64_RSC_BE)),
        (0x50, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0x60, *movl_mlx(3, 0x1001f0)),
        (0x70, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x80, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x90, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0xa0, 0x08, ld8_fill_postinc(32, 6, 0), nop_i(), nop_i()),
        (0xb0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xc0, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xd0, *movl_mlx(3, 0x1001f8)),
        (0xe0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), br_cond(0xf0, 0xf0)),
        (0x200, 0x00, 0, 0, 0),
    ], {
        "ip": 0xf0,
        "exception": IA64_EXCP_NONE,
        # LE observation of a BE collection containing bits 62 and 2.
        "r8": 0x0400000000000040,
    }, entry=0x10)

test_rse_big_endian_rnat_collection = require_registers(
    "rse_big_endian_rnat_collection", [
        (0x10, *movl_mlx(3, IA64_RSC_BE)),
        (0x20, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x1001f0)),
        (0x40, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x60, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x70, 0x08, ld8_fill_postinc(32, 6, 0), nop_i(), nop_i()),
        (0x80, 0x18, nop_m(), nop_m(), cover_b()),
        (0x90, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xa0, *movl_mlx(3, 0x1001f8)),
        (0xb0, 0x00, ld8(8, 3), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x40,
    }, entry=0x10)

test_rse_spill_fault_sets_isr_rs = require_registers(
    "rse_spill_fault_sets_isr_rs", [
        (0x10, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT)),
        (0x20, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, HIGH_TR_BASE + 0x10000)),
        (0x40, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x60, *movl_mlx(32, 0x123456789abcdef0)),
        (0x70, 0x18, nop_m(), nop_m(), cover_b()),
        (0x80, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_ALT_DTLB_VECTOR + 0x20,
                 IA64_ALT_DTLB_VECTOR + 0x20)),
        (IA64_DATA_NESTED_TLB_VECTOR, 0x10, nop_m(), adds(29, 1, 0),
         br_cond(IA64_DATA_NESTED_TLB_VECTOR,
                 IA64_DATA_NESTED_TLB_VECTOR)),
    ], {
        "ip": IA64_ALT_DTLB_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r29": 0,
        "r30": HIGH_TR_BASE + 0x10000,
        "r31": IA64_ISR_W | IA64_ISR_RS | IA64_ISR_NI,
    }, entry=0x10)

test_rse_physical_spill_fault_sets_isr_rs = require_registers(
    "rse_physical_spill_fault_sets_isr_rs", [
        (0x10, *movl_mlx(3, 1 << IA64_IMPL_PA_BITS)),
        (0x20, *movl_mlx(19, IA64_PSR_IC)),
        (0x30, 0x00, mov_gr_psr_full(19), nop_i(), nop_i()),
        (0x40, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(1, 1, 0, 0, 0), nop_i()),
        (0x70, *movl_mlx(32, 0x123456789abcdef0)),
        (0x80, 0x18, nop_m(), nop_m(), cover_b()),
        (0x90, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x20,
                 IA64_GENERAL_VECTOR + 0x20)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 1 << IA64_IMPL_PA_BITS,
        "r31": IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_W | IA64_ISR_RS,
    }, entry=0x10)

test_rse_physical_target_fill_fault_sets_isr_rs_ir = require_registers(
    "rse_physical_target_fill_fault_sets_isr_rs_ir", [
        (0x10, *movl_mlx(3, (1 << IA64_IMPL_PA_BITS) + 8)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(20, (1 << 63) | 1)),
        (0x40, 0x00, mov_m_gr_cr(20, 23), nop_i(), nop_i()),
        (0x50, *movl_mlx(20, 0x200)),
        (0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (0x70, *movl_mlx(20, IA64_PSR_IC)),
        (0x80, 0x00, mov_m_gr_cr(20, 16), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), rfi_b()),
        (IA64_GENERAL_VECTOR, 0x00, mov_m_cr_gr(31, 17), nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x10, 0x00, mov_m_cr_gr(30, 20),
         nop_i(), nop_i()),
        (IA64_GENERAL_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_GENERAL_VECTOR + 0x20,
                 IA64_GENERAL_VECTOR + 0x20)),
    ], {
        "ip": IA64_GENERAL_VECTOR + 0x20,
        "exception": IA64_EXCP_NONE,
        "r30": 1 << IA64_IMPL_PA_BITS,
        "r31": (IA64_GENEX_UNIMPL_DATA_ADDR | IA64_ISR_R |
                IA64_ISR_RS | IA64_ISR_IR),
    }, entry=0x10)


# PSR.dd suppresses the first matching mandatory backing-store reference.
# A successful mandatory reference consumes it immediately, so the second
# spill in the same flushrs faults with ISR.rs and the second backing address.
test_rse_mandatory_spill_consumes_psr_dd = require_registers(
    "rse_mandatory_spill_consumes_psr_dd", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(4, 0)),
        (0x20, *movl_mlx(5, 0x100000)),
        (0x30, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x40, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        # Ignore address bit 3 so both adjacent eight-byte spills match.
        (0x50, *movl_mlx(5, 0x41fffffffffffff7)),
        (0x60, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x70, *movl_mlx(3, 0x100000)),
        (0x80, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), alloc(1, 2, 0, 0, 0), nop_i()),
        (0xa0, *movl_mlx(32, 0x1122334455667788)),
        (0xb0, *movl_mlx(33, 0x8877665544332211)),
        (0xc0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xd0, *movl_mlx(
            2, IA64_PSR_IC | IA64_PSR_DB | IA64_PSR_DD)),
        (0xe0, *movl_mlx(6, 0x120)),
        # cover saved a valid IFS while IC was clear.  Invalidate it so this
        # rfi only installs PSR.dd and does not reclaim the covered frame.
        (0xf0, 0x00, mov_m_gr_cr(0, 23), nop_i(), nop_i()),
        *rfi_to_gr(0x100, 2, 6),
        (0x120, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 20),
         nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DEBUG_VECTOR + 0x30,
                 IA64_DEBUG_VECTOR + 0x30)),
    ], {
        "ip": IA64_DEBUG_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_DEBUG,
        "r8": 0x120,
        "r9": 0x100008,
        "r10": IA64_ISR_W | IA64_ISR_RS,
    }, entry=0x800)


# A target-frame fill is a mandatory RSE read.  Its Debug fault is attributed
# to the restored target instruction and reports both ISR.rs and ISR.ir.
test_rse_mandatory_target_fill_debug_sets_isr_rs_ir = require_registers(
    "rse_mandatory_target_fill_debug_sets_isr_rs_ir", [
        (0x10, *movl_mlx(4, 0)),
        (0x20, *movl_mlx(5, 0x100000)),
        (0x30, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x40, 0x00, nop_m(), adds(4, 1, 0), nop_i()),
        (0x50, *movl_mlx(5, 0x81ffffffffffffff)),
        (0x60, 0x00, mov_dbr_indexed_write(4, 5), nop_i(), nop_i()),
        (0x70, *movl_mlx(3, 0x100008)),
        (0x80, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x90, *movl_mlx(20, (1 << 63) | 1)),
        (0xa0, 0x00, mov_m_gr_cr(20, 23), nop_i(), nop_i()),
        (0xb0, *movl_mlx(20, 0x200)),
        (0xc0, 0x00, mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (0xd0, *movl_mlx(20, IA64_PSR_IC | IA64_PSR_DB)),
        (0xe0, 0x00, mov_m_gr_cr(20, 16), nop_i(), nop_i()),
        (0xf0, 0x10, nop_m(), nop_i(), rfi_b()),
        raw_bundle(0x100000, 0x123456789abcdef0, 0),
        (IA64_DEBUG_VECTOR, 0x00, mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x10, 0x00, mov_m_cr_gr(9, 20),
         nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x20, 0x00, mov_m_cr_gr(10, 17),
         nop_i(), nop_i()),
        (IA64_DEBUG_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_DEBUG_VECTOR + 0x30,
                 IA64_DEBUG_VECTOR + 0x30)),
    ], {
        "ip": IA64_DEBUG_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_DEBUG,
        "r8": 0x200,
        "r9": 0x100000,
        "r10": IA64_ISR_R | IA64_ISR_RS | IA64_ISR_IR,
    }, entry=0x10)

test_rse_rfi_bspstore_rebase_preserves_interrupted_call = require_registers(
    "rse_rfi_bspstore_rebase_preserves_interrupted_call", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x60, 0x70)),
        (0x70, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x80, *movl_mlx(37, 0x123456789abcdef0)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_call(0, 0x90, 0x100)),
        (0xa0, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x100, 0x00, ld8(10, 2), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00, mov_ar(3, 18),
         adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, mov_m_gr_cr(7, 21),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_rfi_same_iip_preserves_interrupted_call_nat = require_registers(
    "rse_rfi_same_iip_preserves_interrupted_call_nat", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x60, 0x70)),
        (0x70, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x80, *movl_mlx(37, 0x123456789abcdef0)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_call(0, 0x90, 0x200)),
        (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xc0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x200, 0x00, nop_m(), alloc(48, 26, 18, 0, 0),
         nop_i()),
        (0x210, 0x00, ld8(50, 2), nop_i(),
         nop_i()),
        (0x220, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00, loadrs_enc(),
         adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, mov_m_gr_cr(7, 21),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r37": 0x123456789abcdef0,
        "r37_nat": 0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_rfi_bspstore_advanced_iip_spills_parent_frame = require_registers(
    "rse_rfi_bspstore_advanced_iip_spills_parent_frame", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(37, 0x123456789abcdef0)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(20, 0x110)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

"""The interrupted dirty frame survives an rfi without an RSE spill.  The
handler's mov-to-BSPSTORE operations independently make RNAT undefined, so
the target's deterministic readback after return is zero."""
test_rse_rfi_does_not_spill_dirty_frame_rnat = require_registers(
    "rse_rfi_does_not_spill_dirty_frame_rnat", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        # PFM.sof=1, PFM.sol=1: returning preserves one unavailable register.
        (0x60, *movl_mlx(4, 1 | (1 << 7))),
        (0x70, 0x00, mov_m_gr_ar(4, 19), nop_i(),
         nop_i()),
        (0x80, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x90, *movl_mlx(37, 0x123456789abcdef0)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, 0x120)),
        (0xb0, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (0x120, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x130, 0x00, mov_m_ar_gr(9, 19), nop_i(),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(20, 0x130)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_rfi_does_not_overwrite_trailing_rnat = require_registers(
    "rse_rfi_does_not_overwrite_trailing_rnat", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x1001a8)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(39, 14, 10, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(36, 0x123456789abcdef0)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x160)),
        (0x90, 0x00, nop_m(), nop_i(), nop_i()),
        (0xa0, 0x00, nop_m(), nop_i(), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x160, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(3, 0x1001f8)),
        (0x180, 0x00, ld8(10, 3), nop_i(),
         nop_i()),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(3, 0x1001a8)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(3, 0x1001f8)),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(4, 1 << 57)),
        (IA64_BREAK_VECTOR + 0x70, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x80, *movl_mlx(20, 0x170)),
        (IA64_BREAK_VECTOR + 0x90, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0xa0, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r36_nat": 0,
        "r10": 1 << 57,
        "cfm_sof": 14,
        "cfm_sol": 10,
    }, entry=0x10)

test_rse_rfi_advanced_iip_uses_covered_current_frame = require_registers(
    "rse_rfi_advanced_iip_uses_covered_current_frame", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(40, 12, 4, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(33, 0x1122334455667788)),
        (0x80, *movl_mlx(34, 0x8877665544332211)),
        (0x90, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0xa0, 0x00, nop_m(), adds(8, 0, 33),
         nop_i()),
        (0xb0, 0x00, nop_m(), adds(9, 0, 34),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, 0xa0)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1122334455667788,
        "r9": 0x8877665544332211,
        "r33": 0x1122334455667788,
        "r34": 0x8877665544332211,
        "cfm_sof": 12,
        "cfm_sol": 4,
    }, entry=0x10)

test_rse_rfi_repeated_cover_uses_latest_current_frame = require_registers(
    "rse_rfi_repeated_cover_uses_latest_current_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(40, 12, 4, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(33, 0x1111222233334444)),
        (0x50, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0),
         nop_i()),
        (0x60, 0x08, ld8_fill_postinc(34, 6, 0), nop_i(),
         nop_i()),
        (0x70, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x80, *movl_mlx(20, (1 << 63) | 12 | (4 << 7))),
        (0x90, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(20, 0xd0)),
        (0xb0, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0xd0, *movl_mlx(33, 0xaaaabbbbccccdddd)),
        (0xe0, *movl_mlx(34, 0x123456789abcdef0)),
        (0xf0, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x100, *movl_mlx(20, (1 << 63) | 12 | (4 << 7))),
        (0x110, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0x120, *movl_mlx(20, 0x150)),
        (0x130, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x150, 0x00, nop_m(), nop_i(), nop_i()),
        (0x160, 0x00, nop_m(), adds(8, 0, 33),
         nop_i()),
        (0x170, 0x10, nop_m(), nop_i(),
         br_cond(0x170, 0x180)),
        (0x180, 0x10, nop_m(), nop_i(),
         br_cond(0x180, 0x180)),
    ], {
        "ip": 0x180,
        "exception": IA64_EXCP_NONE,
        "r8": 0xaaaabbbbccccdddd,
        "r33": 0xaaaabbbbccccdddd,
        "r34": 0x123456789abcdef0,
        "r34_nat": 0,
        "cfm_sof": 12,
        "cfm_sol": 4,
    }, entry=0x10)

test_rse_rfi_repeated_cover_preserves_latest_dirty_partition = require_registers(
    "rse_rfi_repeated_cover_preserves_latest_dirty_partition", [
        (0x10, *movl_mlx(2, 0)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(40, 12, 4, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(33, 0x1111222233334444)),
        (0x80, *movl_mlx(34, 0x5555666677778888)),
        (0x90, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0xa0, *movl_mlx(20, (1 << 63) | 12 | (4 << 7))),
        (0xb0, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(20, 0xf0)),
        (0xd0, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0xf0, *movl_mlx(33, 0xaaaabbbbccccdddd)),
        (0x100, *movl_mlx(34, 0x123456789abcdef0)),
        (0x110, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x120, *movl_mlx(2, 1 << 13)),
        (0x130, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x130, 0x150)),
        (0x150, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x170, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x180, *movl_mlx(3, 0x100008)),
        (0x190, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0x1a0, *movl_mlx(3, 0x100010)),
        (0x1b0, 0x00, ld8(9, 3), nop_i(),
         nop_i()),
        (0x1c0, 0x10, nop_m(), nop_i(),
         br_cond(0x1c0, 0x1c0)),
        (IA64_BREAK_VECTOR, *movl_mlx(20, (1 << 63) | 1)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x170)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x1c0,
        "exception": IA64_EXCP_NONE,
        "r8": 0xaaaabbbbccccdddd,
        "r9": 0x123456789abcdef0,
        "cfm_sof": 1,
        "cfm_sol": 0,
    }, entry=0x10)

test_rse_rfi_advanced_iip_bspstore_switch_loads_external_frame = \
    require_registers(
        "rse_rfi_advanced_iip_bspstore_switch_loads_external_frame", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(40, 14, 6, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(33, 0x123456789abcdef0)),
        (0x80, *movl_mlx(34, 0x0fedcba987654321)),
        (0x90, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0xa0, 0x00, nop_m(), adds(8, 0, 33),
         nop_i()),
        (0xb0, 0x00, mov_m_ar_gr(10, 17), adds(9, 0, 34),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (IA64_BREAK_VECTOR, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20,
         *movl_mlx(20, (1 << 63) | 12 | (4 << 7))),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0xa0)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 0,
        "r10": 0x1fff98,
        "cfm_sof": 12,
        "cfm_sol": 4,
    }, entry=0x10)

test_rse_rfi_advanced_iip_preserves_nested_call_locals = require_registers(
    "rse_rfi_advanced_iip_preserves_nested_call_locals", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x100)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(35, 0),
         nop_i()),
        (0x120, *movl_mlx(37, 0x123456789abcdef0)),
        (0x130, 0x00, addl(38, 0x14000, 0), nop_i(),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         br_call(0, 0x140, 0x200)),
        (0x150, 0x00, addl(38, 0x24000, 0), nop_i(),
         nop_i()),
        (0x160, 0x10, nop_m(), nop_i(),
         br_call(0, 0x160, 0x200)),
        (0x170, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x180, 0x00, mov_m_gr_ar(36, 64), mov_b_gr(0, 35),
         nop_i()),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x200, 0x00, mov_m_ar_gr(11, 64), nop_i(),
         nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_call(6, 0x210, 0x300)),
        (0x220, 0x00, mov_m_gr_ar(11, 64), nop_i(),
         nop_i()),
        (0x230, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x300, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x310, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(20, 0x310)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x800)

test_rse_rfi_bypassed_call_drops_returned_frame = require_registers(
    "rse_rfi_bypassed_call_drops_returned_frame", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x100)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(36, 7, 6, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(35, 0),
         nop_i()),
        (0x120, *movl_mlx(37, 0x123456789abcdef0)),
        (0x130, 0x10, nop_m(), nop_i(),
         br_call(0, 0x130, 0x200)),
        (0x140, 0x00, nop_m(), adds(8, 0, 37),
         nop_i()),
        (0x150, 0x00, mov_m_gr_ar(36, 64), mov_b_gr(0, 35),
         nop_i()),
        (0x160, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        # Model a no-alloc libc syscall wrapper.  Its gateway call returns
        # through rfi directly to 0x220, without executing br.ret b6.
        (0x200, 0x00, mov_m_ar_gr(11, 64), nop_i(),
         nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_call(6, 0x210, 0x300)),
        (0x220, 0x00, mov_m_gr_ar(11, 64), nop_i(),
         nop_i()),
        (0x230, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x300, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, 0x220)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x800)

test_rse_manual_rfi_loadrs_restores_current_frame_base = require_registers(
    "rse_manual_rfi_loadrs_restores_current_frame_base", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(14, 9, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(33, 0x123456789abcdef0)),
        (0x50, *movl_mlx(34, 0x0fedcba987654321)),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x100)),
        (0x70, 0x00, nop_m(), adds(8, 0, 33),
         nop_i()),
        (0x80, 0x00, nop_m(), adds(9, 0, 34),
         nop_i()),
        (0x90, 0x00, mov_m_ar_gr(10, 17), nop_i(),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, alloc(68, 49, 41, 0, 0), nop_i(),
         nop_i()),
        (0x110, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x120, *movl_mlx(20, (1 << 63) | 0x14b1)),
        (0x130, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(20, 0x200)),
        (0x150, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0x160, 0x00, mov_m_gr_cr(0, 16), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(20, (52 * 8) << 16)),
        (0x180, 0x00, mov_m_gr_ar(20, 16), nop_i(),
         nop_i()),
        (0x190, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x1a0, *movl_mlx(20, 0x100000)),
        (0x1b0, 0x00, mov_ar(20, 18), nop_i(),
         nop_i()),
        (0x1c0, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x200, 0x00, mov_m_gr_ar(68, 64), nop_i(),
         nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0x0fedcba987654321,
        "r10": 0x100000,
        "cfm_sof": 9,
        "cfm_sol": 3,
    }, entry=0x10)

test_rse_rfi_user_context_preserves_loadrs_dirty_partition = \
    require_registers(
        "rse_rfi_user_context_preserves_loadrs_dirty_partition", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(2, IA64_PSR_IC)),
        (0x40, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x40, 0x300)),
        # The rfi target deliberately precedes the interrupted IP.  It is a
        # guest-built user context, not a return to the cached break frame.
        (0x200, *movl_mlx(3, 0x200000)),
        (0x210, 0x00, st8_postinc(3, 0, 8), nop_i(), nop_i()),
        (0x220, 0x00, st8_postinc(3, 0, 8), nop_i(), nop_i()),
        (0x230, 0x00, st8(3, 0), nop_i(), nop_i()),
        (0x240, 0x00, mov_m_gr_ar(68, 64), nop_i(), nop_i()),
        (0x250, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x300, 0x00, alloc(14, 9, 3, 0, 0), nop_i(), nop_i()),
        (0x310, *movl_mlx(33, 0x123456789abcdef0)),
        (0x320, *movl_mlx(34, 0x0fedcba987654321)),
        (0x330, 0x10, nop_m(), nop_i(),
         br_call(0, 0x330, 0x400)),
        (0x340, 0x00, nop_m(), adds(8, 0, 33), adds(9, 0, 34)),
        (0x350, 0x10, nop_m(), nop_i(),
         br_cond(0x350, 0x350)),
        (0x400, 0x00, alloc(68, 49, 41, 0, 0), nop_i(), nop_i()),
        (0x410, 0x00, break_m(0x42), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10,
         *movl_mlx(20, (52 * 8) << 16)),
        (IA64_BREAK_VECTOR + 0x20, 0x00,
         mov_m_gr_ar(20, 16), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00,
         loadrs_enc(), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0x200000)),
        (IA64_BREAK_VECTOR + 0x50, 0x00,
         mov_ar(20, 18), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(20, 0x200)),
        (IA64_BREAK_VECTOR + 0x70, 0x00,
         mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x80, *movl_mlx(20, IA64_PSR_CPL3)),
        (IA64_BREAK_VECTOR + 0x90, 0x00,
         mov_m_gr_cr(20, 16), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0xa0, 0x10,
         nop_m(), nop_i(), rfi_b()),
    ], {
        "ip": 0x350,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0x0fedcba987654321,
        "cfm_sof": 9,
        "cfm_sol": 3,
    }, entry=0x10)

test_rse_manual_rfi_smaller_frame_restores_current_frame_base = \
    require_registers(
        "rse_manual_rfi_smaller_frame_restores_current_frame_base", [
            (0x10, *movl_mlx(3, 0x100000)),
            (0x20, 0x00, mov_ar(3, 18), nop_i(),
             nop_i()),
            (0x30, 0x00, alloc(2, 5, 0, 0, 0), nop_i(),
             nop_i()),
            (0x40, *movl_mlx(32, 0x1111222233334444)),
            (0x50, *movl_mlx(33, 0x5555666677778888)),
            (0x60, *movl_mlx(34, 0x9999aaaabbbbcccc)),
            (0x70, *movl_mlx(35, 0xddddeeeeffff0000)),
            (0x80, *movl_mlx(36, 0x123456789abcdef0)),
            (0x90, 0x18, nop_m(), nop_m(),
             cover_b()),
            (0xa0, 0x00, alloc(2, 8, 6, 0, 0), nop_i(),
             nop_i()),
            (0xb0, *movl_mlx(32, 0xa1a2a3a4a5a6a7a8)),
            (0xc0, *movl_mlx(33, 0xb1b2b3b4b5b6b7b8)),
            (0xd0, *movl_mlx(20, 0x200)),
            (0xe0, 0x00, mov_m_gr_cr(0, 16), nop_i(),
             nop_i()),
            (0xf0, 0x00, mov_m_gr_cr(20, 19), nop_i(),
             nop_i()),
            (0x100, *movl_mlx(20, (1 << 63) | 5)),
            (0x110, 0x00, mov_m_gr_cr(20, 23), nop_i(),
             nop_i()),
            (0x120, 0x10, nop_m(), nop_i(),
             rfi_b()),
            (0x200, 0x00, nop_m(), adds(8, 0, 32),
             adds(9, 0, 33)),
            (0x210, 0x00, nop_m(), adds(10, 0, 34),
             adds(11, 0, 35)),
            (0x220, 0x00, mov_m_ar_gr(13, 17), adds(12, 0, 36),
             nop_i()),
            (0x230, 0x10, nop_m(), nop_i(),
             br_cond(0x230, 0x230)),
        ], {
            "ip": 0x230,
            "exception": IA64_EXCP_NONE,
            "r8": 0x1111222233334444,
            "r9": 0x5555666677778888,
            "r10": 0x9999aaaabbbbcccc,
            "r11": 0xddddeeeeffff0000,
            "r12": 0x123456789abcdef0,
            "r13": 0x100000,
            "cfm_sof": 5,
            "cfm_sol": 0,
        }, entry=0x10)

test_rse_rfi_loadrs_preserves_high_sol_caller_local = require_registers(
    "rse_rfi_loadrs_preserves_high_sol_caller_local", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 62, 57, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(87, 0x123456789abcdef0)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, mov_m_ar_gr(9, 17), adds(8, 0, 87),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, nop_m(), alloc(9, 8, 0, 0, 0),
         nop_i()),
        (0x110, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(20, 0x120)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0x100000,
        "r87": 0x123456789abcdef0,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10)

test_rse_rfi_loadrs_preserves_low_sol_caller_local = require_registers(
    "rse_rfi_loadrs_preserves_low_sol_caller_local", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 62, 57, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(40, 0x1111222233334444)),
        (0x80, *movl_mlx(41, 0x5555666677778888)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_call(0, 0x90, 0x100)),
        (0xa0, 0x00, nop_m(), adds(8, 0, 40),
         adds(9, 0, 41)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (0x100, 0x00, nop_m(), alloc(9, 8, 0, 0, 0),
         nop_i()),
        (0x110, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(20, 0x120)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xb0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x5555666677778888,
        "r40": 0x1111222233334444,
        "r41": 0x5555666677778888,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10)

test_rse_rfi_loadrs_preserves_caller_locals_after_nested_return = require_registers(
    "rse_rfi_loadrs_preserves_caller_locals_after_nested_return", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 62, 57, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(40, 0x1111222233334444)),
        (0x80, *movl_mlx(41, 0x5555666677778888)),
        (0x90, *movl_mlx(87, 0x123456789abcdef0)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, 0x100)),
        (0xb0, 0x00, nop_m(), adds(8, 0, 40),
         adds(9, 0, 41)),
        (0xc0, 0x00, nop_m(), adds(10, 0, 87),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x100, 0x00, nop_m(), alloc(9, 8, 0, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(10, 0),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_call(0, 0x120, 0x200)),
        (0x130, 0x00, mov_m_gr_ar(9, 64), mov_b_gr(0, 10),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x200, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, (65 * 8) << 16)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(20, 16), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0x210)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x5555666677778888,
        "r10": 0x123456789abcdef0,
        "r40": 0x1111222233334444,
        "r41": 0x5555666677778888,
        "r87": 0x123456789abcdef0,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10)

def _guest_frame_ld8_s_nested_switch_case(name, *, pte_flags, nat):
    bundles = [
        # Construct a SOF=26/SOL=23 frame with the function entry loaded into
        # local r44 and the descriptor base postincremented in r42.
        # WB selects the successful load case; control-speculative loads on
        # UC memory defer into NaT for the companion recovery-path case.
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                           pte_flags=pte_flags),
        (0x70, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT)),
        (0x80, 0x08, mov_gr_psr_full(2), srlz_d(), nop_i()),

        # Establish the backing-store geometry measured immediately before
        # the failing guest load.  Writing ar.pfs into the highest local makes
        # the complete 37-register bootstrap frame live.  cover+flushrs
        # advances BSPSTORE from 0x100000 to 0x100128 and BOL from 0 to 37;
        # alloc(0)+loadrs(0) then leaves all 96 physical registers invalid.
        (0x90, *movl_mlx(3, 0x100000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0xb0, 0x00, nop_m(), alloc(68, 37, 37, 0, 0), nop_i()),
        (0xc0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xd0, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0xe0, 0x00, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0xf0, 0x00, nop_m(), alloc(14, 0, 0, 0, 0), nop_i()),
        (0x100, 0x00, loadrs_enc(), nop_i(), nop_i()),

        # The outer SOF=64/SOL=38 frame has exactly 26 outputs.  Its highest
        # local is dirty, so br.call leaves the target at BOL=37+38=75 with
        # dirty=38/1, invalid=32, BSP=0x100260 and BSPSTORE=0x100128.
        (0x110, 0x00, nop_m(), alloc(69, 64, 38, 0, 0), nop_i()),
        (0x120, 0x10, nop_m(), nop_i(), br_call(0, 0x120, 0x300)),
        (0x130, 0x10, nop_m(), nop_i(), br_cond(0x130, 0x130)),

        # alloc keeps the inherited 26-register frame but changes SOL to 23.
        # At BOL=75, r44 maps to physical register
        # (75 + (44 - 32)) % 96 = 87.  Its backing-store address is
        # 0x100260 + 12*8 = 0x1002c0, bit 24 of the 0x1003f8 RNAT word.
        (0x300, 0x00, nop_m(), alloc(52, 26, 23, 0, 0),
         adds(10, 0, 0)),
        (0x310, *movl_mlx(42, HIGH_TR_BASE)),
        (0x320, 0x00, ld8_s_postinc(44, 42, 8), nop_i(), nop_i()),
        (0x330, 0x10, nop_m(), nop_i(), br_call(0, 0x330, 0x500)),
        (0x340, 0x08, nop_m(), chk_s_m(44, 0x340, 0x380), nop_i()),
        (0x350, 0x00, nop_m(), adds(8, 0, 44), adds(9, 0, 42)),
        (0x360, 0x10, nop_m(), nop_i(), br_cond(0x360, 0x360)),
        (0x380, 0x10, nop_m(), adds(10, 1, 0),
         br_cond(0x380, 0x360)),

        # The nested call supplies the interruption boundary without changing
        # the target's frame shape.  Returning through b0 must recover the
        # target frame, including its r44 value and NaT bit.
        (0x500, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x510, 0x10, nop_m(), nop_i(), br_ret(0)),

        # Exercise a complete context switch: quiesce the RSE, save BSP
        # before flushrs and RNAT after it, execute alloc(0)+loadrs with
        # loadrs=0 to invalidate the physical stack, then restore
        # BSPSTORE/RNAT and lazy RSC mode.
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, nop_m(), adds(28, 0, 0),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(28, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_ar_gr(25, 17),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_ar_gr(26, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x00, nop_m(),
         alloc(14, 0, 0, 0, 0), nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x80, 0x00, mov_m_gr_ar(25, 18),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x90, 0x00, mov_m_gr_ar(26, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0xa0, 0x00, nop_m(), adds(28, 3, 0),
         nop_i()),
        (IA64_BREAK_VECTOR + 0xb0, 0x00, mov_m_gr_ar(28, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0xc0, *movl_mlx(20, 0x510)),
        (IA64_BREAK_VECTOR + 0xd0, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0xe0, 0x10, nop_m(), nop_i(), rfi_b()),
        raw_bundle(0x400000, 0x7763c020, 0),
    ]
    expected = {
        "ip": 0x360,
        "exception": IA64_EXCP_NONE,
        "r42": HIGH_TR_BASE + 8,
        "ar_rsc": 3,
        "cfm_sof": 26,
        "cfm_sol": 23,
    }
    if nat:
        expected.update({
            "r10": 1,
            "r44_nat": 1,
        })
    else:
        expected.update({
            "r8": 0x7763c020,
            "r8_nat": 0,
            "r9": HIGH_TR_BASE + 8,
            "r10": 0,
            "r44": 0x7763c020,
            "r44_nat": 0,
        })
    return require_registers(name, bundles, expected, entry=0x10,
                             cpu="merced")


test_rse_guest_frame_ld8_s_survives_nested_switch = \
    _guest_frame_ld8_s_nested_switch_case(
        "rse_guest_frame_ld8_s_survives_nested_switch",
        pte_flags=DTR_PTE_WB, nat=False)

test_rse_guest_frame_ld8_s_nat_survives_nested_switch = \
    _guest_frame_ld8_s_nested_switch_case(
        "rse_guest_frame_ld8_s_nat_survives_nested_switch",
        pte_flags=DTR_PTE_UC, nat=True)


def _guest_same_mfb_pressure_poison_case(
        name, *, context_remap, initial_bol=0, remap_delta=83):
    """Exercise an r41 spill/fill failure shape deterministically.

    The producer MFB contains ld8.s plus a direct call; the later consumer MFB
    contains the indirect call.  Fusing ld8.s in slot 0 with that indirect
    br.call in slot 2 deliberately strengthens the path.  The outer
    SOF/SOL=29/21 and producer callee SOF/SOL=15/9 preserve the relevant
    register-stack pressure.
    """
    sentinel = 0x123456789abcdef0
    descriptor = HIGH_TR_BASE
    descriptor_physical = 0x400000
    callee = 0x300
    bsp_base = 0x100040
    r41_home = 0x100088
    rnat_collection = 0x1001f8
    lazy_user_rsc = 0xf
    resume_ip = 0x430
    seed_bsp = 0x200040
    dummy_bsp = 0x300040
    dtr_bundles = dtr_setup_bundles(
        0x1000, descriptor, descriptor_physical, pte_flags=DTR_PTE_WB)
    dtr_stop_addresses = {0x1010, 0x1020, 0x1030}
    dtr_bundles = [
        (address, template | int(address in dtr_stop_addresses),
         slot0, slot1, slot2)
        for address, template, slot0, slot1, slot2 in dtr_bundles
    ]
    setup_target = 0x1090 if initial_bol else 0x10
    bol_seed_bundles = [] if initial_bol == 0 else [
        # Establish the selected absolute BOL without
        # retaining any registers from this bootstrap context.  BOL is not an
        # architected save field: cover advances it, and flushrs leaves the
        # physical stack fully invalid while preserving that absolute bias.
        (0x1090, *movl_mlx(3, seed_bsp)),
        (0x10a0, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x10b0, 0x00,
         alloc(32, initial_bol, initial_bol, 0, 0), nop_i(), nop_i()),
        (0x10c0, 0x18, nop_m(), nop_m(), cover_b()),
        (0x10d0, 0x01, flushrs_enc(), nop_i(), nop_i()),
        (0x10e0, 0x10, nop_m(), nop_i(), br_cond(0x10e0, 0x10)),
    ]
    bundles = [
        *dtr_bundles,
        (0x1060, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT)),
        (0x1070, 0x0a, mov_gr_psr_full(2), srlz_d(), nop_i()),
        (0x1080, 0x10, nop_m(), nop_i(),
         br_cond(0x1080, setup_target)),
        *bol_seed_bundles,

        (0x10, *movl_mlx(3, bsp_base)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(13, lazy_user_rsc)),
        (0x40, 0x01, mov_m_gr_ar(13, 16), nop_i(), nop_i()),
        (0x50, *movl_mlx(2, descriptor)),
        (0x60, *movl_mlx(20, callee)),
        (0x70, 0x01, nop_m(), mov_b_gr(7, 20), nop_i()),

        # The outer frame maps virtual r41 offset 9 to physical
        # (BOL+9)%96, whose backing-store home is 0x100088 independently of
        # that physical bias.  The fused producer/indirect-call shape is
        # intentionally stronger than separate producer and consumer
        # bundles.
        (0x80, 0x00, alloc(43, 29, 21, 0, 0), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), adds(44, 0, 2),
         br_cond(0x90, 0x100)),
        (0x100, 0x1d, ld8_s_postinc(41, 44, 8), nop_f(),
         br_call_indirect(0, 7, wh=5, many=True)),

        # The indirect callee inherits eight outputs, then takes a 15/9
        # frame.  Its b6 nested call advances BOL to 30 while leaving b0
        # intact for the eventual return to the outer frame.
        (callee, 0x00, alloc_m(39, 15, 9, 0, 0), nop_i(), nop_i()),
        (callee + 0x10, 0x10, nop_m(), nop_i(),
         br_call(6, callee + 0x10, 0x400)),
        (callee + 0x20, 0x01, mov_m_gr_ar(39, 64), nop_i(), nop_i()),
        (callee + 0x30, 0x10, nop_m(), nop_i(), br_ret(0)),

        # The nested call inherits six registers.  Growing to SOF=76 consumes
        # all 60 invalid physical registers and spills exactly p0..p9.  Its
        # r107 maps back onto p9, so this write poisons the physical home after
        # r41 has been saved.  The two returns must mandatory-fill p9 from
        # 0x100088; retaining the poisoned physical value cannot pass.
        (0x400, 0x00, alloc_m(36, 76, 68, 0, 0), nop_i(), nop_i()),
        (0x410, 0x01, nop_m(), adds(107, 0, 0), nop_i()),
        (0x420, 0x03,
         break_m(0x42) if context_remap else nop_m(), nop_i(), nop_i()),
        (resume_ip, 0x01, mov_m_gr_ar(36, 64), nop_i(), nop_i()),
        (0x440, 0x10, nop_m(), nop_i(), br_ret(6)),

        # Independently observe the backing-store image left by the exact ten
        # spills, plus the final BSP and BSPSTORE after both mandatory fills.
        (0x110, *movl_mlx(2, r41_home)),
        (0x120, 0x01, ld8(8, 2), nop_i(), nop_i()),
        (0x130, *movl_mlx(3, rnat_collection)),
        (0x140, 0x01, ld8(10, 3), nop_i(), nop_i()),
        (0x150, 0x01, mov_m_ar_gr(11, 17), nop_i(), nop_i()),
        (0x160, 0x01, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x170, 0x01, mov_m_ar_gr(12, 18), nop_i(), nop_i()),
        (0x180, 0x01, mov_m_gr_ar(13, 16), nop_i(), nop_i()),
        (0x190, 0x10, nop_m(), nop_i(), br_cond(0x190, 0x190)),
        raw_bundle(descriptor_physical, sentinel, 0),
    ]
    if context_remap:
        bundles += [
            # Save the covered target context, quiesce it to memory, then run
            # a dummy backing-store context on the same CPU.  Its second cover
            # deliberately changes BOL and overwrites CR.IFS, so both the
            # original IFS and backing-store state must be restored explicitly.
            (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
            (IA64_BREAK_VECTOR + 0x10, 0x09,
             mov_m_cr_gr(24, 23), mov_m_ar_gr(27, 16), nop_i()),
            (IA64_BREAK_VECTOR + 0x20, 0x01,
             mov_m_ar_gr(25, 17), adds(28, 0, 0), nop_i()),
            (IA64_BREAK_VECTOR + 0x30, 0x01,
             mov_m_gr_ar(28, 16), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x40, 0x01,
             flushrs_enc(), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x50, 0x01,
             mov_m_ar_gr(26, 19), nop_i(), nop_i()),

            (IA64_BREAK_VECTOR + 0x60, *movl_mlx(28, dummy_bsp)),
            (IA64_BREAK_VECTOR + 0x70, 0x01,
             mov_m_gr_ar(28, 18), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x80, 0x01,
             mov_m_gr_ar(0, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x90, 0x00,
             alloc(32, remap_delta, remap_delta, 0, 0), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xa0, 0x18,
             nop_m(), nop_m(), cover_b()),
            (IA64_BREAK_VECTOR + 0xb0, 0x01,
             flushrs_enc(), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xc0, 0x00,
             alloc(14, 0, 0, 0, 0), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xd0, 0x01,
             loadrs_enc(), nop_i(), nop_i()),

            # Dummy cover changes the target's eventual BOL by remap_delta.
            # A 96-register temporary frame maps r127 to the physical slot
            # that the final outer r41 will use after rfi and both returns.
            # Poison it before restoring the target; correct backing-store
            # fills must overwrite this zero.
            (IA64_BREAK_VECTOR + 0xe0, 0x00,
             alloc(32, 96, 96, 0, 0), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xf0, 0x01,
             nop_m(), adds(127, 0, 0), nop_i()),
            (IA64_BREAK_VECTOR + 0x100, 0x00,
             alloc(14, 0, 0, 0, 0), nop_i(), nop_i()),

            (IA64_BREAK_VECTOR + 0x110, 0x01,
             mov_m_gr_ar(25, 18), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x120, 0x01,
             mov_m_gr_ar(26, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x130, 0x01,
             mov_m_gr_ar(27, 16), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x140, 0x01,
             mov_m_gr_cr(24, 23), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x150, *movl_mlx(20, resume_ip)),
            (IA64_BREAK_VECTOR + 0x160, 0x01,
             mov_m_gr_cr(20, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x170, 0x10,
             nop_m(), nop_i(), rfi_b()),
        ]
    expected = {
        "ip": 0x190,
        "exception": IA64_EXCP_NONE,
        "r8": sentinel,
        "r10": 0,
        "r11": bsp_base,
        "r12": bsp_base,
        "r13": lazy_user_rsc,
        "r41": sentinel,
        "r41_nat": 0,
        "r44": descriptor + 8,
        "b0": 0x110,
        "b7": callee,
        "ar_rsc": lazy_user_rsc,
        "cfm_sof": 29,
        "cfm_sol": 21,
    }

    def run_and_check(qemu):
        result = run_program(qemu, bundles, entry=0x1000, alat=None,
                             expected=expected, name=name, cpu="merced")
        cfm = re.search(
            r"CFM: sof=(\d+) sol=(\d+).* "
            r"BSP=0x([0-9a-f]+) BSPSTORE=0x([0-9a-f]+)",
            result.register_output)
        observed_pointers = None if cfm is None else (
            int(cfm.group(3), 16), int(cfm.group(4), 16))
        wanted_pointers = (bsp_base, bsp_base)
        if observed_pointers != wanted_pointers:
            raise RuntimeError(
                f"{name} final BSP/BSPSTORE: expected "
                f"{wanted_pointers!r}, got {observed_pointers!r}\n"
                f"{result.register_output}")

        rse = re.search(
            r"RSE: bol=(\d+) dirty=(-?\d+)/(-?\d+) "
            r"clean=(-?\d+)/(-?\d+) invalid=(-?\d+)",
            result.register_output)
        observed_partitions = None if rse is None else tuple(
            int(value) for value in rse.groups())
        final_bol = ((initial_bol + remap_delta) % 96
                     if context_remap else initial_bol)
        wanted_partitions = (final_bol, 0, 0, 0, 0, 67)
        if observed_partitions != wanted_partitions:
            raise RuntimeError(
                f"{name} final RSE: expected {wanted_partitions!r}, "
                f"got {observed_partitions!r}\n{result.register_output}")

        dirty = re.search(
            r"RSE-GR-DIRTY: 0x([0-9a-f]{16})/0x([0-9a-f]{16}) "
            r"PGRNAT=0x([0-9a-f]{16})/0x([0-9a-f]{16})",
            result.register_output)
        pgr = re.findall(
            r"^RSE-PGR\[(\d+)\]: 0x([0-9a-f]{16}) nat=([01])\r?$",
            result.register_output, re.MULTILINE)
        if dirty is None or [int(fields[0]) for fields in pgr] != \
                list(range(96)):
            raise RuntimeError(
                f"{name} lacks a complete physical RSE image\n"
                f"{result.register_output}")
        dirty_words = tuple(int(value, 16) for value in dirty.groups()[:2])
        nat_words = tuple(int(value, 16) for value in dirty.groups()[2:])
        final_pgr = (final_bol + 9) % 96
        pgr_value = (int(pgr[final_pgr][1], 16),
                     int(pgr[final_pgr][2]))
        pgr_mask = 1 << (final_pgr % 64)
        pgr_word = final_pgr // 64
        # RSE-GR-DIRTY is indexed by logical stacked offset, whereas PGRNAT
        # and RSE-PGR are indexed by the BOL-rotated physical register.
        if dirty_words[0] & (1 << 9) or \
                nat_words[pgr_word] & pgr_mask or \
                pgr_value != (sentinel, 0):
            raise RuntimeError(
                f"{name} final p{final_pgr}: expected "
                f"value=0x{sentinel:016x}, nat=0, clean dirty-bit; got "
                f"value/nat={pgr_value!r}, dirty={dirty_words!r}, "
                f"nat={nat_words!r}\n"
                f"{result.register_output}")

    return IA64Case(
        name=name,
        runner=run_and_check,
        bundles=tuple(tuple(bundle) for bundle in bundles),
        expected=expected,
    )


test_rse_guest_same_mfb_ld8_s_r41_survives_nested_pressure_poison = \
    _guest_same_mfb_pressure_poison_case(
        "rse_guest_same_mfb_ld8_s_r41_survives_nested_pressure_poison",
        context_remap=False)

test_rse_guest_same_mfb_ld8_s_r41_survives_same_cpu_context_remap_poison = \
    _guest_same_mfb_pressure_poison_case(
        "rse_guest_same_mfb_ld8_s_r41_survives_same_cpu_"
        "context_remap_poison", context_remap=True)

test_rse_guest_same_mfb_ld8_s_r41_survives_bol79_to8_remap_poison = \
    _guest_same_mfb_pressure_poison_case(
        "rse_guest_same_mfb_ld8_s_r41_survives_bol79_to8_"
        "remap_poison", context_remap=True, initial_bol=79,
        remap_delta=25)


def _guest_helper_ld8_s_r41_nested_pressure_case(name, *, switch):
    entry_value = 0x77cf16e0
    descriptor_gp = 0x78002000
    bsp_base = 0x100040
    lazy_user_rsc = 0xf
    resume_ip = 0x420
    dtr_bundles = dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000,
                                    pte_flags=DTR_PTE_WB)
    dtr_stop_addresses = {0x20, 0x30, 0x40}
    dtr_bundles = [
        (address, template | int(address in dtr_stop_addresses),
         slot0, slot1, slot2)
        for address, template, slot0, slot1, slot2 in dtr_bundles
    ]
    bundles = [
        # A successful descriptor load from WB memory produces a valid,
        # non-NaT entry point and postincrements the stacked base.
        *dtr_bundles,
        (0x70, 0x05, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT)[1:]),
        (0x80, 0x0a, mov_gr_psr_full(2), srlz_d(), nop_i()),
        (0x90, 0x05, *movl_mlx(3, bsp_base)[1:]),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0xb0, 0x05, *movl_mlx(13, lazy_user_rsc)[1:]),
        (0xc0, 0x00, mov_m_gr_ar(13, 16), nop_i(), nop_i()),

        # The outer frame has SOF=19/SOL=13.  With reset BOL=0,
        # r41 is physical register 9 and its backing-store home is
        # 0x100088 (RNAT collection bit 17 in the word at 0x1001f8).
        (0xd0, 0x00, alloc(43, 19, 13, 0, 0), adds(10, 0, 0),
         nop_i()),
        (0xe0, 0x05, *movl_mlx(35, HIGH_TR_BASE)[1:]),
        (0xf0, 0x00, ld8_s_postinc(41, 35, 8), nop_i(), nop_i()),
        (0x100, 0x10, nop_m(), nop_i(), br_call(0, 0x100, 0x300)),
        (0x110, 0x08, nop_m(), chk_s_m(41, 0x110, 0x190), nop_i()),
        (0x120, 0x00, nop_m(), adds(8, 0, 41), adds(9, 0, 35)),

        # AR.BSPSTORE is accessible only with RSC.mode=0.  Capture both
        # backing-store pointers, then restore the selected lazy PL3 value.
        (0x130, 0x00, mov_m_ar_gr(11, 17), nop_i(), nop_i()),
        (0x140, 0x01, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x150, 0x00, mov_m_ar_gr(12, 18), nop_i(), nop_i()),
        (0x160, 0x00, mov_m_gr_ar(13, 16), nop_i(), nop_i()),
        (0x170, 0x10, nop_m(), nop_i(), br_cond(0x170, 0x170)),
        (0x190, 0x10, nop_m(), adds(10, 1, 0),
         br_cond(0x190, 0x130)),

        # Match the intervening helper: SOF=16/SOL=9, with b0 saved in its
        # highest local before a nested call.  The child's inherited seven
        # inputs put its BOL at 22 while outer r41 remains physical register
        # 9, exactly thirteen physical registers behind it.
        (0x300, 0x00, alloc_m(39, 16, 9, 0, 0), nop_i(), nop_i()),
        (0x310, 0x01, nop_m(), mov_gr_b(40, 0), nop_i()),
        (0x320, 0x10, nop_m(), nop_i(), br_call(0, 0x320, 0x400)),
        (0x330, 0x00, nop_m(), mov_m_gr_ar(39, 64), mov_b_gr(0, 40)),
        (0x340, 0x10, nop_m(), nop_i(), br_ret(0)),

        # Before this alloc there are 67 invalid and 22 dirty physical
        # registers.  Growing the inherited seven-register frame to 96 uses
        # all 67 invalid registers, then spills exactly those 22 dirty
        # registers.  Outer r41 is the tenth store, at 0x100088.
        (0x400, 0x00, alloc_m(34, 96, 88, 0, 0), nop_i(), nop_i()),
        (0x410, 0x01, break_m(0x42) if switch else nop_m(), nop_i(),
         nop_i()),
        (resume_ip, 0x00, nop_m(), mov_m_gr_ar(34, 64), nop_i()),
        (0x430, 0x10, nop_m(), nop_i(), br_ret(0)),
        raw_bundle(0x400000, entry_value, descriptor_gp),
    ]

    if switch:
        bundles += [
            # At the break, the child owns all 96 physical registers at
            # BOL=22 and BSP=0x1000f0.  cover crosses the RNAT words at
            # 0x1001f8 and 0x1003f8 and advances BSP to 0x100400.  The
            # flushrs/loadrs(0) sequence invalidates the complete physical
            # stack before rfi reloads it and resumes the two normal returns.
            (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
            (IA64_BREAK_VECTOR + 0x10, 0x00,
             mov_m_ar_gr(27, 16), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x20, 0x01, nop_m(), adds(28, 0, 0),
             nop_i()),
            (IA64_BREAK_VECTOR + 0x30, 0x01,
             mov_m_gr_ar(28, 16), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x40, 0x00,
             mov_m_ar_gr(25, 17), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x50, 0x01,
             flushrs_enc(), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x60, 0x00,
             mov_m_ar_gr(26, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x70, 0x00, nop_m(),
             alloc(14, 0, 0, 0, 0), nop_i()),
            (IA64_BREAK_VECTOR + 0x80, 0x01,
             loadrs_enc(), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x90, 0x01,
             mov_m_gr_ar(25, 18), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xa0, 0x00,
             mov_m_gr_ar(26, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xb0, 0x00,
             mov_m_gr_ar(27, 16), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xc0, 0x05,
             *movl_mlx(20, resume_ip)[1:]),
            (IA64_BREAK_VECTOR + 0xd0, 0x01,
             mov_m_gr_cr(20, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0xe0, 0x10,
             nop_m(), nop_i(), rfi_b()),
        ]

    return require_registers(name, bundles, {
        "ip": 0x170,
        "exception": IA64_EXCP_NONE,
        "r8": entry_value,
        "r8_nat": 0,
        "r9": HIGH_TR_BASE + 8,
        "r10": 0,
        "r11": bsp_base,
        "r12": bsp_base,
        "r13": lazy_user_rsc,
        "r35": HIGH_TR_BASE + 8,
        "r41": entry_value,
        "r41_nat": 0,
        "ar_rsc": lazy_user_rsc,
        "cfm_sof": 19,
        "cfm_sol": 13,
    }, entry=0x10, alat=None, cpu="merced", smp="2")


test_rse_guest_helper_ld8_s_r41_survives_nested_pressure = \
    _guest_helper_ld8_s_r41_nested_pressure_case(
        "rse_guest_helper_ld8_s_r41_survives_nested_pressure", switch=False)

test_rse_guest_helper_ld8_s_r41_survives_nested_switch = \
    _guest_helper_ld8_s_r41_nested_pressure_case(
        "rse_guest_helper_ld8_s_r41_survives_nested_switch", switch=True)


def _guest_bol82_switch_poison_case(name, *, switch_site,
                                         deferred=False):
    """Construct the required cold-start BOL and partition geometry."""
    entry_value = 0x77cf16e0
    descriptor_gp = 0x78002000
    bsp_seed = 0x100038
    outer_bsp = 0x1002d0
    lazy_user_rsc = 0xf
    pre_call_switch = switch_site == "pre-call"
    helper_switches = switch_site == "helper"
    if not (pre_call_switch or helper_switches):
        raise ValueError(f"unsupported guest switch site: {switch_site}")
    if deferred and not helper_switches:
        raise ValueError("deferred guest case requires helper switches")

    # cover leaves BOL=5 for an interruption in the SOF=19 outer frame and
    # BOL=19 for the SOF=11 child.  Grow a temporary frame
    # far enough that the selected virtual register maps to physical r91, and
    # overwrite that register after loadrs has made the physical stack stale.
    # A correct rfi/return fill must replace this zero with the backing-store
    # value; merely retaining the old physical-register contents cannot pass.
    poison_sof, poison_reg = ((87, 118) if pre_call_switch else (73, 104))
    switch_vector = 0x3000 if pre_call_switch else IA64_BREAK_VECTOR

    dtr_bundles = dtr_setup_bundles(
        0x10, HIGH_TR_BASE, 0x400000,
        pte_flags=DTR_PTE_UC if deferred else DTR_PTE_WB)
    dtr_stop_addresses = {0x20, 0x30, 0x40}
    dtr_bundles = [
        (address, template | int(address in dtr_stop_addresses),
         slot0, slot1, slot2)
        for address, template, slot0, slot1, slot2 in dtr_bundles
    ]
    timer_prep_bundles = []
    if pre_call_switch:
        timer_prep_bundles = [
            # Mirror the deterministic pending-timer primitive used by the
            # interrupt tests.  PSR.i remains clear while the timer is armed;
            # the branch back also gives the zero-deadline timer a TB exit at
            # which to become pending before the speculative load sequence.
            (0xa00, 0x01, adds(4, 0xef, 0), nop_i(), nop_i()),
            (0xa10, 0x01, mov_m_gr_cr(4, IA64_CR_ITV), nop_i(), nop_i()),
            (0xa20, 0x01, mov_m_gr_ar(0, 44), nop_i(), nop_i()),
            (0xa30, 0x01, mov_m_gr_cr(0, IA64_CR_ITM), nop_i(), nop_i()),
            (0xa40, 0x10, nop_m(), nop_i(),
             br_cond(0xa40, 0x210)),
        ]
    recovery_bundles = [
        (0x2f0, 0x10, nop_m(), adds(10, 1, 0),
         br_cond(0x2f0, 0x290)),
    ]
    if deferred:
        recovery_bundles = [
            # Rewind the speculative postincrement, redo the descriptor load
            # non-speculatively, publish its entry in b7, then rejoin the
            # indirect-call bundle.
            (0x2f0, 0x0b, adds(35, -8, 35),
             ld8_postinc(41, 35, 8), nop_i()),
            (0x300, 0x11, nop_m(),
             mov_b_gr(7, 41) | bitfield(1, 20, 1),
             br_cond(0x300, 0x270)),
        ]

    bundles = [
        *dtr_bundles,
        (0x70, 0x05, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT)[1:]),
        (0x80, 0x0a, mov_gr_psr_full(2), srlz_d(), nop_i()),
        (0x90, 0x05, *movl_mlx(3, bsp_seed)[1:]),
        (0xa0, 0x01, mov_ar(3, 18), nop_i(), nop_i()),
        (0xb0, 0x05, *movl_mlx(13, lazy_user_rsc)[1:]),
        (0xc0, 0x01, mov_m_gr_ar(13, 16), nop_i(), nop_i()),

        # Seed BOL=45, flush the 45-register bootstrap frame, then invalidate
        # all 96 physical registers without changing the flushed BSPSTORE.
        (0xd0, 0x00, alloc(76, 45, 45, 0, 0), nop_i(), nop_i()),
        (0xe0, 0x18, nop_m(), nop_m(), cover_b()),
        (0xf0, 0x01, flushrs_enc(), nop_i(), nop_i()),
        (0x100, 0x01, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x110, 0x00, alloc(14, 0, 0, 0, 0), nop_i(), nop_i()),
        (0x120, 0x01, loadrs_enc(), nop_i(), nop_i()),
        (0x130, 0x01, mov_m_gr_ar(0, 19), nop_i(), nop_i()),
        (0x140, 0x01, mov_m_gr_ar(13, 16), nop_i(), nop_i()),

        # SOF=43/SOL=37 leaves six outputs.  The call therefore enters with
        # BOL=82 and six inherited inputs; alloc grows by thirteen registers
        # to the selected SOF=19/SOL=13 partition.
        (0x150, 0x00, alloc(68, 43, 37, 0, 0), nop_i(), nop_i()),
        (0x160, 0x10, nop_m(), nop_i(), br_call(0, 0x160, 0x200)),
        (0x170, 0x10, nop_m(), nop_i(), br_cond(0x170, 0x170)),

        (0x200, 0x10 if pre_call_switch else 0x00,
         alloc(43, 19, 13, 0, 0), adds(10, 0, 0),
         br_cond(0x200, 0xa00) if pre_call_switch else
         adds(41, 0, 0) if deferred else nop_i()),
        (0x210, 0x05, *movl_mlx(35, HIGH_TR_BASE)[1:]),
        # Keep the successful ld8.s and three register moves in one group
        # through slot 0 of the second bundle.  The timer variant then enables
        # PSR.i in a separate group, exposing an external-interrupt boundary
        # before the helper call; the companion case uses software
        # interruptions at all five helper sites.
        (0x220, 0x00, ld8_s_postinc(41, 35, 8),
         adds(45, 0, 38), adds(46, 0, 33)),
        (0x230, 0x0a, adds(47, 0, 34), addl(48, 983071, 0),
         addl(49, 2, 0)),
        (0x240, 0x01,
         ssm(IA64_PSR_I) if pre_call_switch else nop_m(),
         tnat_nz_or(6, 0, 41) if deferred else nop_i(), nop_i()),
        (0x250, 0x10, nop_m(), nop_i(), br_call(0, 0x250, 0x400)),
        # The raw +0 prediction hint in mov b7=r41 is bit 20; it has no
        # architectural effect, but retaining it exercises that decoder path.
        # There is no stop between these bundles, so chk.s, the BR write, the
        # descriptor-GP load and the indirect call form one instruction group.
        (0x260, 0x10, chk_s_m(41, 0x260, 0x2f0),
         mov_b_gr(7, 41) | bitfield(1, 20, 1), nop_b()),
        (0x270, 0x1d, ld8(1, 35), nop_f(),
         br_call_indirect(0, 7, wh=5, many=True)),
        (0x280, 0x01, nop_m(), adds(8, 0, 41), adds(9, 0, 35)),
        (0x290, 0x01, mov_m_ar_gr(11, 17), nop_i(), nop_i()),
        (0x2a0, 0x01, mov_m_gr_ar(0, 16), nop_i(), nop_i()),
        (0x2b0, 0x01, mov_m_ar_gr(12, 18), nop_i(), nop_i()),
        (0x2c0, 0x01, mov_m_ar_gr(14, 19), nop_i(), nop_i()),
        (0x2d0, 0x01, mov_m_gr_ar(13, 16), nop_i(), nop_i()),
        (0x2e0, 0x10, nop_m(), nop_i(), br_cond(0x2e0, 0x2e0)),
        *recovery_bundles,

        # Intermediate helper: SOF=16/SOL=9, with b0, ar.pfs, and gp in
        # r38-r40.  The call sequence uses the selected frame sizes.
        (0x400, 0x00, alloc_m(39, 16, 9, 0, 0), nop_i(), nop_i()),
        (0x410, 0x01, nop_m(), mov_gr_b(38, 0), adds(40, 0, 1)),
        (0x420, 0x10, nop_m(), nop_i(), br_call(0, 0x420, 0x600)),
        (0x430, 0x10, nop_m(), nop_i(), br_call(0, 0x430, 0x700)),
        (0x440, 0x10, nop_m(), nop_i(), br_call(0, 0x440, 0x600)),
        (0x450, 0x10, nop_m(), nop_i(), br_call(0, 0x450, 0x700)),
        (0x460, 0x10, nop_m(), nop_i(), br_call(0, 0x460, 0x800)),
        (0x470, 0x10, nop_m(), nop_i(), br_call(0, 0x470, 0x900)),
        (0x480, 0x10, nop_m(), nop_i(), br_call(0, 0x480, 0x900)),
        (0x490, 0x01, nop_m(), mov_m_gr_ar(39, 64), adds(1, 0, 40)),
        (0x4a0, 0x01, nop_m(), mov_b_gr(0, 38), nop_i()),
        (0x4b0, 0x10, nop_m(), nop_i(), br_ret(0)),

        # The first nested helper is a leaf in the recovered call chain.
        (0x600, 0x10, nop_m(), nop_i(), br_ret(0)),

        # The SOF=11/SOL=7 nested helper is reached twice.
        (0x700, 0x00, alloc_m(37, 11, 7, 0, 0), nop_i(), nop_i()),
        (0x710, 0x01, nop_m(), mov_gr_b(38, 0), nop_i()),
        (0x720, 0x03,
         break_m(0x42) if helper_switches else nop_m(), nop_i(), nop_i()),
        (0x730, 0x01, mov_m_gr_ar(37, 64), mov_b_gr(0, 38), nop_i()),
        (0x740, 0x10, nop_m(), nop_i(), br_ret(0)),

        # The SOF=18/SOL=11 nested helper is reached once.
        (0x800, 0x00, alloc_m(41, 18, 11, 0, 0), nop_i(), nop_i()),
        (0x810, 0x01, nop_m(), mov_gr_b(42, 0), nop_i()),
        (0x820, 0x03,
         break_m(0x42) if helper_switches else nop_m(), nop_i(), nop_i()),
        (0x830, 0x01, mov_m_gr_ar(41, 64), mov_b_gr(0, 42), nop_i()),
        (0x840, 0x10, nop_m(), nop_i(), br_ret(0)),

        # The SOF=5/SOL=4 leaf helper is reached twice.
        (0x900, 0x00, alloc_m(34, 5, 4, 0, 0), nop_i(), nop_i()),
        (0x910, 0x01, nop_m(), mov_gr_b(35, 0), nop_i()),
        (0x920, 0x03,
         break_m(0x42) if helper_switches else nop_m(), nop_i(), nop_i()),
        (0x930, 0x01, mov_m_gr_ar(34, 64), mov_b_gr(0, 35), nop_i()),
        (0x940, 0x10, nop_m(), nop_i(), br_ret(0)),

        *timer_prep_bundles,

        # Dynamic resume supports all five helper break sites.  The pre-call
        # variant instead acknowledges the pending timer and naturally
        # resumes at 0x250.  Both paths cover, flush, invalidate and reload
        # the RSE, deliberately poison physical r91 while it is invalid, then
        # restore the interrupted backing-store state before rfi.
        (switch_vector, 0x18, nop_m(), nop_m(), cover_b()),
        (switch_vector + 0x10, 0x0b,
         mov_m_cr_gr(20, IA64_CR_SAPIC_IVR) if pre_call_switch else
         mov_m_cr_gr(20, 19),
         mov_m_cr_gr(21, 19) if pre_call_switch else adds(20, 16, 20),
         adds(15, 1, 15)),
        (switch_vector + 0x20, 0x01,
         mov_m_ar_gr(27, 16), nop_i(), nop_i()),
        (switch_vector + 0x30, 0x01,
         nop_m(), adds(28, 0, 0), nop_i()),
        (switch_vector + 0x40, 0x01,
         mov_m_gr_ar(28, 16), nop_i(), nop_i()),
        (switch_vector + 0x50, 0x01,
         mov_m_ar_gr(25, 17), nop_i(), nop_i()),
        (switch_vector + 0x60, 0x01,
         flushrs_enc(), nop_i(), nop_i()),
        (switch_vector + 0x70, 0x01,
         mov_m_ar_gr(26, 19), nop_i(), nop_i()),
        (switch_vector + 0x80, 0x00,
         alloc(14, 0, 0, 0, 0), nop_i(), nop_i()),
        (switch_vector + 0x90, 0x01,
         loadrs_enc(), nop_i(), nop_i()),
        (switch_vector + 0xa0, 0x00,
         alloc(32, poison_sof, poison_sof, 0, 0), nop_i(), nop_i()),
        (switch_vector + 0xb0, 0x01,
         nop_m(), adds(poison_reg, 0, 0), nop_i()),
        (switch_vector + 0xc0, 0x00,
         alloc(14, 0, 0, 0, 0), nop_i(), nop_i()),
        (switch_vector + 0xd0, 0x01,
         mov_m_gr_ar(25, 18), nop_i(), nop_i()),
        (switch_vector + 0xe0, 0x01,
         mov_m_gr_ar(26, 19), nop_i(), nop_i()),
        (switch_vector + 0xf0, 0x01,
         mov_m_gr_ar(27, 16), nop_i(), nop_i()),
        (switch_vector + 0x100, 0x01,
         mov_m_gr_cr(0, IA64_CR_SAPIC_EOI) if pre_call_switch else
         mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (switch_vector + 0x110, 0x10,
         nop_m(), nop_i(), rfi_b()),

        raw_bundle(0x400000, entry_value, descriptor_gp),

        # The descriptor entry is below the machine's default 2 GiB RAM
        # ceiling.  A marker and GP capture prove that b7 selected this leaf
        # and that the call bundle loaded the descriptor's second word before
        # branching.
        (entry_value, 0x01, nop_m(), adds(2, 0, 1), adds(3, 0x5a, 0)),
        (entry_value + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ]
    expected = {
        "ip": 0x2e0,
        "exception": IA64_EXCP_NONE,
        "r1": descriptor_gp,
        "r2": descriptor_gp,
        "r3": 0x5a,
        "r8": entry_value,
        "r8_nat": 0,
        "r9": HIGH_TR_BASE + 8,
        "r10": 0,
        "r11": outer_bsp,
        "r12": outer_bsp,
        "r14": 0,
        "r35": HIGH_TR_BASE + 8,
        "r41": entry_value,
        "r41_nat": 0,
        "b0": 0x280,
        "b7": entry_value,
        "ar_rnat": 0,
        "ar_rsc": lazy_user_rsc,
        "cfm_sof": 19,
        "cfm_sol": 13,
    }
    expected["r15"] = 1 if pre_call_switch else 5
    if pre_call_switch:
        expected.update({
            "r20": 0xef,
            "r21": 0x250,
        })
    if deferred:
        expected.update({
            # p6 records that the original UC ld8.s produced NaT before any
            # helper call or RSE switch.  The remaining values prove that the
            # NaT survived all five interruptions, selected the fixup, was
            # cleared by the ordinary reload, and reached the real call leaf.
            "p6": 1,
            "r20": 0x930,
        })

    def run_and_check(qemu):
        result = run_program(qemu, bundles, entry=0x10, alat=None,
                             expected=expected, name=name, cpu="merced",
                             smp="2")
        match = re.search(
            r"RSE: bol=(\d+) dirty=(-?\d+)/(-?\d+) "
            r"clean=(-?\d+)/(-?\d+) invalid=(-?\d+)",
            result.register_output)
        observed = None if match is None else tuple(
            int(value) for value in match.groups())
        wanted = (82, 0, 0, 0, 0, 77)
        if observed != wanted:
            raise RuntimeError(
                f"{name} final RSE: expected {wanted!r}, got {observed!r}\n"
                f"{result.register_output}")

    return IA64Case(
        name=name,
        runner=run_and_check,
        bundles=tuple(tuple(bundle) for bundle in bundles),
        expected=expected,
    )


test_rse_guest_bol82_ld8_s_survives_precall_switch_poison = \
    _guest_bol82_switch_poison_case(
        "rse_guest_bol82_ld8_s_survives_precall_switch_poison",
        switch_site="pre-call")

test_rse_guest_bol82_ld8_s_survives_helper_switches_poison = \
    _guest_bol82_switch_poison_case(
        "rse_guest_bol82_ld8_s_survives_helper_switches_poison",
        switch_site="helper")

test_rse_guest_bol82_ld8_s_deferred_nat_recovers_after_helper_switches = \
    _guest_bol82_switch_poison_case(
        "rse_guest_bol82_ld8_s_deferred_nat_recovers_after_"
        "helper_switches",
        switch_site="helper", deferred=True)


test_rse_rfi_loadrs_preserves_caller_locals_after_syscall_error = require_registers(
    "rse_rfi_loadrs_preserves_caller_locals_after_syscall_error", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 62, 57, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(40, 0x1111222233334444)),
        (0x80, *movl_mlx(41, 0x5555666677778888)),
        (0x90, *movl_mlx(87, 0x123456789abcdef0)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, 0x100)),
        (0xb0, 0x00, nop_m(), adds(8, 0, 40),
         adds(9, 0, 41)),
        (0xc0, 0x00, nop_m(), adds(10, 0, 87),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
        (0x100, 0x00, nop_m(), alloc(9, 8, 0, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(11, 0),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_call(0, 0x120, 0x200)),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x180)),
        (0x180, 0x00, nop_m(), alloc(32, 4, 3, 0, 0),
         nop_i()),
        (0x190, 0x00, nop_m(), adds(33, 0, 11),
         adds(34, 0, 9)),
        (0x1a0, 0x10, nop_m(), nop_i(),
         br_call(0, 0x1a0, 0x300)),
        (0x1b0, 0x00, nop_m(), mov_m_gr_ar(34, 64),
         mov_b_gr(0, 33)),
        (0x1c0, 0x10, nop_m(), adds(8, -1, 0), br_ret(0)),
        (0x200, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x210, 0x10, nop_m(), adds(10, 1, 0),
         br_ret(0)),
        (0x300, 0x00, nop_m(), alloc(36, 14, 4, 0, 0),
         nop_i()),
        (0x310, *movl_mlx(40, 0xaaaabbbbccccdddd)),
        (0x320, *movl_mlx(41, 0xddddccccbbbbaaaa)),
        (0x330, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, (65 * 8) << 16)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(20, 16), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0x210)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xd0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x5555666677778888,
        "r10": 0x123456789abcdef0,
        "r40": 0x1111222233334444,
        "r41": 0x5555666677778888,
        "r87": 0x123456789abcdef0,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10)

test_rse_rfi_loadrs_preserves_gp_save_after_syscall_error = require_registers(
    "rse_rfi_loadrs_preserves_gp_save_after_syscall_error", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(1, 0x123456789abcdef0)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x00, nop_m(), adds(9, 0, 1),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x100, 0x00, nop_m(), alloc(33, 4, 3, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(32, 0),
         adds(34, 0, 1)),
        (0x120, 0x10, nop_m(), nop_i(),
         br_call(0, 0x120, 0x200)),
        (0x130, 0x00, nop_m(), adds(1, 0, 34),
         adds(8, 0, 34)),
        (0x140, 0x00, mov_m_gr_ar(33, 64), mov_b_gr(0, 32),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x200, 0x00, nop_m(), alloc(9, 8, 0, 0, 0),
         nop_i()),
        (0x210, 0x00, nop_m(), mov_gr_b(11, 0),
         nop_i()),
        (0x220, 0x10, nop_m(), nop_i(),
         br_call(0, 0x220, 0x300)),
        (0x230, 0x10, nop_m(), nop_i(),
         br_cond(0x230, 0x280)),
        (0x280, 0x00, nop_m(), alloc(32, 4, 3, 0, 0),
         nop_i()),
        (0x290, 0x00, nop_m(), adds(33, 0, 11),
         adds(34, 0, 9)),
        (0x2a0, 0x10, nop_m(), nop_i(),
         br_call(0, 0x2a0, 0x400)),
        (0x2b0, 0x00, nop_m(), mov_m_gr_ar(34, 64),
         mov_b_gr(0, 33)),
        (0x2c0, 0x10, nop_m(), adds(8, -1, 0), br_ret(0)),
        (0x300, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x310, 0x10, nop_m(), adds(10, 1, 0),
         br_ret(0)),
        (0x400, 0x00, nop_m(), alloc(36, 14, 4, 0, 0),
         nop_i()),
        (0x410, *movl_mlx(40, 0xaaaabbbbccccdddd)),
        (0x420, *movl_mlx(41, 0xddddccccbbbbaaaa)),
        (0x430, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, (16 * 8) << 16)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_ar(20, 16), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0x310)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r1": 0x123456789abcdef0,
        "r8": 0x123456789abcdef0,
        "r9": 0x123456789abcdef0,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x10)

test_rse_rt_translates_with_dt_disabled = require_registers(
    "rse_rt_translates_with_dt_disabled", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, 1 << 27)),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(3, HIGH_TR_BASE + 0x10000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, alloc(1, 1, 1, 0, 0), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(32, 0x1234)),
        (0xd0, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0xe0, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0xf0, *movl_mlx(4, 0x04010000)),
        (0x100, 0x00, ld8(8, 4), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1234,
    }, entry=0x10)

test_rse_flushrs_crosses_reverse_mapped_virtual_pages = require_registers(
    "rse_flushrs_crosses_reverse_mapped_virtual_pages", [
        *dtr_setup_bundles(0x10, 0xe0000106014cc000, 0xa096000,
                           page_shift=13, slot=5),
        *dtr_setup_bundles(0x70, 0xe0000106014ce000, 0xa094000,
                           page_shift=13, slot=6),
        (0xd0, *movl_mlx(3, 0xe0000106014cde00)),
        (0xe0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xf0, *movl_mlx(19, IA64_PSR_RT)),
        (0x100, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x110, 0x00, nop_m(), alloc(41, 96, 80, 0, 0),
         nop_i()),
        (0x120, *movl_mlx(32, 0x123456789abcdef0)),
        (0x130, 0x10, nop_m(), nop_i(),
         br_call(0, 0x130, 0x180)),
        (0x180, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x190, *movl_mlx(3, 0xa097e00)),
        (0x1a0, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0x1b0, 0x10, nop_m(), nop_i(),
         br_cond(0x1b0, 0x1b0)),
    ], {
        "ip": 0x1b0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
    }, entry=0x10)

test_rse_bspstore_rewrite_reloads_spilled_frame = require_registers(
    "rse_bspstore_rewrite_reloads_spilled_frame", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 17) | (1 << 27))),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(3, HIGH_TR_BASE + 0x10000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xc0, 0x100)),
        (0xd0, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
        (0x100, *movl_mlx(3, HIGH_TR_BASE + 0x10078)),
        (0x110, 0x00, alloc(2, 0, 0, 0, 0), nop_i(),
         nop_i()),
        (0x120, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x130, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x5a,
    }, entry=0x10)

test_rse_bspstore_physical_alias_survives_rt_disable = require_registers(
    "rse_bspstore_physical_alias_survives_rt_disable", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0),
         nop_i()),
        (0x60, 0x00, itr_d(5, 18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(19, (1 << 17) | (1 << 27))),
        (0x80, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x90, *movl_mlx(3, HIGH_TR_BASE + 0x10000)),
        (0xa0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xb0, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xc0, 0x110)),
        (0xd0, 0x00, mov_m_ar_gr(9, 17), adds(8, 0, 43),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
        (0x110, *movl_mlx(3, 0x04010078)),
        (0x120, 0x00, alloc(2, 0, 0, 0, 0), nop_i(),
         nop_i()),
        (0x130, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x140, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x150, *movl_mlx(19, 0)),
        (0x160, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x170, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x5a,
        "r9": 0x04010000,
    }, entry=0x10)

test_rse_bspstore_dtlb_miss_retries_spill = require_registers(
    "rse_bspstore_dtlb_miss_retries_spill", [
        (0x10, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x20, 0x00, mov_gr_psr_full(19), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(3, HIGH_TR_BASE + 0x10000)),
        (0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x50, 0x00, nop_m(), alloc(2, 8, 0, 0, 0),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_call(0, 0x60, 0x100)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(68, 49, 41, 0, 0),
         addl(70, 0x1234, 0)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(6, 0x110, 0x180)),
        (0x120, 0x00, nop_m(), adds(8, 0, 70),
         nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
        (0x180, 0x00, nop_m(), alloc(61, 36, 32, 0, 0),
         nop_i()),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (0x1000, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x1010, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x1020, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1234,
    }, entry=0x10)

test_rse_br_ret_fill_dtlb_miss_retries_atomically = require_registers(
    "rse_br_ret_fill_dtlb_miss_retries_atomically", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, *movl_mlx(7, EIGHT_K_ITIR)),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0x60, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (0x80, *movl_mlx(20, HIGH_TR_BASE + 0x2000)),
        (0x90, 0x00, mov_m_gr_cr(20, 20), nop_i(),
         nop_i()),
        (0xa0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (0xb0, *movl_mlx(19, (1 << 13) | (1 << 17) | (1 << 27))),
        (0xc0, *movl_mlx(3, HIGH_TR_BASE + 0x1f00)),
        (0xd0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xe0, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0xe0, 0x100)),
        (0x100, 0x00, nop_m(), alloc(2, 8, 0, 0, 0),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(0, 0x110, 0x200)),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (0x200, 0x00, nop_m(), alloc(89, 62, 57, 0, 0),
         nop_i()),
        (0x210, *movl_mlx(40, 0x1111222233334444)),
        (0x220, *movl_mlx(70, 0x5555666677778888)),
        (0x230, *movl_mlx(87, 0x123456789abcdef0)),
        (0x240, 0x10, nop_m(), nop_i(),
         br_call(6, 0x240, 0x300)),
        (0x250, 0x00, nop_m(), adds(8, 0, 40),
         adds(9, 0, 70)),
        (0x260, 0x00, nop_m(), adds(10, 0, 87),
         adds(11, 0, 89)),
        (0x270, 0x00, nop_m(), adds(14, 0, 90),
         adds(15, 0, 91)),
        (0x280, 0x10, nop_m(), nop_i(),
         br_cond(0x280, 0x280)),
        (0x300, 0x00, nop_m(), alloc(61, 36, 32, 0, 0),
         nop_i()),
        (0x310, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x320, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x330, *movl_mlx(3, HIGH_TR_BASE + 0x2000)),
        (0x340, 0x00, ptr_d(3, 7), nop_i(),
         nop_i()),
        (0x350, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (0x360, *movl_mlx(32, 0xa1a2a3a4a5a6a7a8)),
        (0x370, *movl_mlx(33, 0xb1b2b3b4b5b6b7b8)),
        (0x380, *movl_mlx(34, 0xc1c2c3c4c5c6c7c8)),
        (0x390, *movl_mlx(35, 0xd1d2d3d4d5d6d7d8)),
        (0x3a0, *movl_mlx(36, 0xe1e2e3e4e5e6e7e8)),
        (0x3b0, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (IA64_ALT_DTLB_VECTOR, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00,
         adds(7, EIGHT_K_ITIR, 0), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00,
         mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x08,
         itc_d(18), adds(29, 0x77, 0),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x280,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x5555666677778888,
        "r10": 0x123456789abcdef0,
        "r11": 0xa1a2a3a4a5a6a7a8,
        "r14": 0xb1b2b3b4b5b6b7b8,
        "r15": 0xc1c2c3c4c5c6c7c8,
        "r29": 0x77,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10)

# A br.ret does not become eligible for completion traps until all mandatory
# target-frame fills succeed.  The miss handler executes several instructions,
# so this also verifies that the original branch's PSR.tb/ss snapshot survives
# interruption delivery and the handler's rfi.
test_rse_br_ret_completion_trap_waits_for_target_fill = require_registers(
    "rse_br_ret_completion_trap_waits_for_target_fill", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (0x20, *movl_mlx(7, EIGHT_K_ITIR)),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, HIGH_TR_BASE + 0x2008)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        # PFS.sof=1, PFS.sol=1 forces the target register below BOF and
        # therefore requires a mandatory fill before the completion trap.
        (0x60, *movl_mlx(4, 0x81)),
        (0x70, *movl_mlx(5, 0x200)),
        (0x80, 0x01, nop_m(), mov_m_gr_ar(4, 64), mov_b_gr(7, 5)),
        (0x90, *movl_mlx(
            2, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT |
               IA64_PSR_TB | IA64_PSR_SS | (2 << 41))),
        (0xa0, *movl_mlx(3, 0x100)),
        *rfi_to_gr(0xb0, 2, 3),
        (0x100, 0x10, nop_m(), nop_i(), br_ret(7)),
        (0x200, 0x10, nop_m(), nop_i(), br_cond(0x200, 0x200)),
        raw_bundle(0x4002000, 0x123456789abcdef0, 0),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00,
         mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00,
         itc_d(18), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x10, nop_m(), nop_i(), rfi_b()),
        (IA64_TAKEN_BRANCH_VECTOR, 0x00,
         mov_m_cr_gr(8, 19), nop_i(), nop_i()),
        (IA64_TAKEN_BRANCH_VECTOR + 0x10, 0x00,
         mov_m_cr_gr(9, 22), nop_i(), nop_i()),
        (IA64_TAKEN_BRANCH_VECTOR + 0x20, 0x00,
         mov_m_cr_gr(10, 17), nop_i(), nop_i()),
        (IA64_TAKEN_BRANCH_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(IA64_TAKEN_BRANCH_VECTOR + 0x30,
                 IA64_TAKEN_BRANCH_VECTOR + 0x30)),
    ], {
        "ip": IA64_TAKEN_BRANCH_VECTOR + 0x30,
        "exception": IA64_EXCP_NONE,
        "fault_code": IA64_EXCP_TAKEN_BRANCH,
        "r8": 0x200,
        "r9": 0x100,
        "r10": (IA64_ISR_CODE_TB | IA64_ISR_CODE_SS |
                 (2 << IA64_ISR_EI_SHIFT)),
    }, entry=0x10)


# AR.EC is part of the completed br.ret state, so a mandatory target-frame
# fill fault handler must already observe PFS.pec.  The handler covers before
# rfi; the resumed fill and completed return must retain the same EC value.
test_rse_br_ret_ec_restored_before_target_fill_fault = require_registers(
    "rse_br_ret_ec_restored_before_target_fill_fault", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE + 0x2000)),
        (0x20, *movl_mlx(7, EIGHT_K_ITIR)),
        (0x30, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x40, *movl_mlx(3, HIGH_TR_BASE + 0x2008)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x60, *movl_mlx(4, 0x81 | (0x2d << 52))),
        (0x70, *movl_mlx(5, 0x200)),
        (0x80, 0x01, nop_m(), mov_m_gr_ar(4, 64), mov_b_gr(7, 5)),
        (0x90, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT)),
        (0xa0, *movl_mlx(3, 0x100)),
        *rfi_to_gr(0xb0, 2, 3),
        (0x100, 0x00, nop_m(), mov_i_imm_ar(66, 7), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_ret(7)),
        (0x200, 0x00, nop_m(), mov_m_ar_gr(9, 66),
         adds(11, 0, 32)),
        (0x210, 0x10, nop_m(), nop_i(), br_cond(0x210, 0x210)),
        raw_bundle(0x4002000, 0x123456789abcdef0, 0),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(), cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, nop_m(),
         mov_m_ar_gr(8, 66), adds(10, 1, 10)),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x08,
         mov_m_cr_gr(12, 19), mov_m_cr_gr(13, 22), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00,
         mov_m_cr_gr(14, 17), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00,
         mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x00,
         itc_d(18), nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x60, 0x10,
         nop_m(), nop_i(), rfi_b()),
    ], {
        "ip": 0x210,
        "exception": IA64_EXCP_NONE,
        "r8": 0x2d,
        "r9": 0x2d,
        "r10": 1,
        "r11": 0x123456789abcdef0,
        "r12": 0x200,
        "r13": 0x110,
        "r14": IA64_ISR_R | IA64_ISR_RS | IA64_ISR_IR,
    }, entry=0x10)

SAL_DIRECT_RSE_BASE = 0xe000000080400000

test_rse_sal_alt_dtlb_resumes_br_ret_fill = require_registers(
    "rse_sal_alt_dtlb_resumes_br_ret_fill", [
        # Region 7, RID 1, 8 KiB pages, VHPT disabled: the boot-loader
        # direct window used while the firmware IVT owns TLB misses.
        (0x10, *movl_mlx(17, SAL_DIRECT_RSE_BASE)),
        (0x20, *movl_mlx(18, (1 << 8) | EIGHT_K_ITIR | 1)),
        (0x30, 0x00, mov_rr_write(18, 17), nop_i(), nop_i()),
        (0x40, *movl_mlx(2, IA64_FIRMWARE_IVT_BASE)),
        (0x50, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0x60, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT |
                         IA64_PSR_RT)),
        (0x70, *movl_mlx(3, SAL_DIRECT_RSE_BASE + 0x1f00)),
        (0x80, 0x00, mov_ar(3, 18), adds(7, EIGHT_K_ITIR, 0), nop_i()),
        (0x90, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, 0x00, nop_m(), alloc(2, 8, 0, 0, 0), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_call(0, 0xb0, 0x200)),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),

        (0x200, 0x00, nop_m(), alloc(89, 62, 57, 0, 0), nop_i()),
        (0x210, *movl_mlx(40, 0x1111222233334444)),
        (0x220, *movl_mlx(70, 0x5555666677778888)),
        (0x230, *movl_mlx(87, 0x123456789abcdef0)),
        (0x240, 0x10, nop_m(), nop_i(), br_call(6, 0x240, 0x300)),
        (0x250, 0x00, nop_m(), adds(8, 0, 40), adds(9, 0, 70)),
        (0x260, 0x00, nop_m(), adds(10, 0, 87), adds(11, 0, 89)),
        (0x270, 0x00, nop_m(), adds(14, 0, 90), adds(15, 0, 91)),
        (0x280, 0x10, nop_m(), nop_i(), br_cond(0x280, 0x280)),

        (0x300, 0x00, nop_m(), alloc(61, 36, 32, 0, 0), nop_i()),
        (0x310, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x320, 0x00, loadrs_enc(), nop_i(), nop_i()),
        (0x330, *movl_mlx(3, SAL_DIRECT_RSE_BASE + 0x2000)),
        (0x340, 0x00, ptr_d(3, 7), nop_i(), nop_i()),
        (0x350, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x360, *movl_mlx(32, 0xa1a2a3a4a5a6a7a8)),
        (0x370, *movl_mlx(33, 0xb1b2b3b4b5b6b7b8)),
        (0x380, *movl_mlx(34, 0xc1c2c3c4c5c6c7c8)),
        (0x390, *movl_mlx(35, 0xd1d2d3d4d5d6d7d8)),
        (0x3a0, *movl_mlx(36, 0xe1e2e3e4e5e6e7e8)),
        (0x3b0, 0x10, nop_m(), nop_i(), br_ret(6)),
    ], {
        "ip": 0x280,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x5555666677778888,
        "r10": 0x123456789abcdef0,
        "r11": 0xa1a2a3a4a5a6a7a8,
        "r14": 0xb1b2b3b4b5b6b7b8,
        "r15": 0xc1c2c3c4c5c6c7c8,
        "cfm_sof": 62,
        "cfm_sol": 57,
    }, entry=0x10, machine="itanium-vpc")

test_rse_exception_loadrs_preserves_interrupted_call = require_registers(
    "rse_exception_loadrs_preserves_interrupted_call", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x100, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, *movl_mlx(20, 0x110)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r8": 0x5a,
    }, entry=0x10)

test_rse_rfi_flushed_interrupted_frame_reads_backing_store = \
    require_registers(
        "rse_rfi_flushed_interrupted_frame_reads_backing_store", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 5, 5, 0, 0),
         addl(32, 4, 0)),
        (0x70, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x80, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x90, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(4, 3)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(20, 0x90)),
        (IA64_BREAK_VECTOR + 0x70, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x80, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 3,
        "r32": 3,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_rfi_flushed_same_iip_uses_interrupted_frame = require_registers(
    "rse_rfi_flushed_same_iip_uses_interrupted_frame", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x30, *movl_mlx(19, (1 << 13) | (1 << 17))),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x60, 0x70)),
        (0x70, 0x00, nop_m(), alloc(36, 5, 5, 0, 0),
         addl(32, 4, 0)),
        (0x80, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x90, 0x00, nop_m(), addl(32, 5, 0),
         nop_i()),
        (0xa0, 0x00, ld8(10, 2), nop_i(),
         nop_i()),
        (0xb0, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(),
         br_cond(0xc0, 0xc0)),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, *movl_mlx(3, 0x100000)),
        (IA64_ALT_DTLB_VECTOR + 0x20, *movl_mlx(4, 3)),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, st8(3, 4),
         adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, mov_m_gr_cr(7, 21),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xc0,
        "exception": IA64_EXCP_NONE,
        "r8": 5,
        "r32": 5,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_rfi_nested_handler_preserves_faulting_frame = require_registers(
    "rse_rfi_nested_handler_preserves_faulting_frame", [
        *dtr_setup_bundles(0x10, 0, 0, page_shift=16, slot=6),
        (0x70, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x80, *movl_mlx(2, IA64_PSR_IC | IA64_PSR_DT)),
        (0x90, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x90, 0xa0)),
        (0xa0, *movl_mlx(3, 0x100000)),
        (0xb0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xc0, *movl_mlx(8, HIGH_TR_BASE + 0x20000)),
        (0xd0, 0x00, nop_m(), alloc(41, 24, 19, 0, 0),
         addl(33, 0x300, 0)),
        (0xe0, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0xf0, 0x00, nop_m(), adds(33, 0, 8),
         nop_i()),
        (0x100, 0x00, st4(33, 0), nop_i(),
         nop_i()),
        (0x110, 0x00, nop_m(), adds(9, 0, 33),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
        (IA64_ALT_DTLB_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, nop_m(),
         alloc(40, 16, 8, 0, 0), addl(33, 1, 0)),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00, mov_m_cr_gr(26, 19),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x30, 0x00, mov_m_cr_gr(27, 23),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x40, 0x00, mov_m_cr_gr(28, 16),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x50, 0x00, ssm(IA64_PSR_IC),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x60, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x70, 0x00, break_m(0x99), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x80, 0x00, rsm(IA64_PSR_IC),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x90, 0x00, srlz_d(), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xa0, 0x00, mov_m_gr_cr(28, 16),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xb0, 0x00, mov_m_gr_cr(27, 23),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xc0, 0x00, mov_m_gr_cr(26, 19),
         adds(7, LOW_VECTOR_ITIR, 0), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xd0, 0x00, mov_m_gr_cr(7, 21),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xe0, 0x00, itc_d(18), nop_i(),
         nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0xf0, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_m_cr_gr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, nop_m(), adds(20, 16, 20),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r9": HIGH_TR_BASE + 0x20000,
        "r33": HIGH_TR_BASE + 0x20000,
        "cfm_sof": 24,
        "cfm_sol": 19,
    }, entry=0x10)

test_rse_postinc_after_flushrs_preserves_register_value = require_registers(
    "rse_postinc_after_flushrs_preserves_register_value", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(2, 0x8000)),
        (0x70, *movl_mlx(4, 0x1122334455667788)),
        (0x80, 0x00, st8(2, 4), nop_i(),
         nop_i()),
        (0x90, 0x00, nop_m(), alloc(36, 5, 5, 0, 0),
         addl(33, 0x8000, 0)),
        (0xa0, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0xb0, 0x08, ld8_postinc(8, 33, 8), nop_i(),
         nop_i()),
        (0xc0, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0xd0, 0x00, nop_m(), adds(9, 0, 33),
         nop_i()),
        (0xe0, 0x10, nop_m(), nop_i(),
         br_cond(0xe0, 0xe0)),
        (IA64_BREAK_VECTOR, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x100008)),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(4, 0x4444)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0xd0)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xe0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1122334455667788,
        "r9": 0x8008,
        "r33": 0x8008,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_firmware_unaligned_postinc_marks_stacked_base_dirty = \
    require_registers(
        "rse_firmware_unaligned_postinc_marks_stacked_base_dirty", [
        (0x10, *movl_mlx(2, (1 << 13) | (1 << 3))),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(2, 0x8000)),
        (0x70, *movl_mlx(4, 0x1122334455667788)),
        (0x80, 0x0a, st8(2, 4), adds(2, 8, 2), nop_i()),
        (0x90, *movl_mlx(4, 0x99aabbccddeeff00)),
        (0xa0, 0x00, st8(2, 4), nop_i(),
         nop_i()),
        (0xb0, 0x00, addl(2, 0x10000, 0), nop_i(), nop_i()),
        (0xc0, 0x00, mov_m_gr_cr(2, 2), nop_i(), nop_i()),
        (0xd0, 0x00, nop_m(), alloc(36, 5, 5, 0, 0),
         addl(33, 0x8004, 0)),
        (0xe0, 0x13, nop_m(), nop_b(), clrrrb_b()),
        (0xf0, 0x08, ld8_postinc(8, 33, 8), nop_i(),
         nop_i()),
        (0x100, 0x13, nop_m(), nop_b(), clrrrb_b()),
        (0x110, 0x00, nop_m(), adds(9, 0, 33),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_cond(0x120, 0x120)),
    ], {
        "ip": 0x120,
        "exception": IA64_EXCP_NONE,
        "r8": 0xddeeff0011223344,
        "r9": 0x800c,
        "r33": 0x800c,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_rfi_invalid_ifs_unchanged_stack_restores_call = require_registers(
    "rse_rfi_invalid_ifs_unchanged_stack_restores_call", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, *movl_mlx(20, 0x110)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(0, 23),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r8": 0x5a,
    }, entry=0x10)

test_rse_exception_restores_snapshot_arrays = require_registers(
    "rse_exception_restores_snapshot_arrays", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(41, 22, 15, 0, 0), addl(43, 0x5a, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x00, nop_m(), adds(8, 0, 43),
         nop_i()),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (0x100, 0x00, alloc(34, 5, 4, 0, 0), nop_i(),
         break_m(0x42)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (IA64_BREAK_VECTOR, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, alloc(2, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         br_call(6, IA64_BREAK_VECTOR + 0x40, IA64_BREAK_VECTOR + 0x100)),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(20, 0x110)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (IA64_BREAK_VECTOR + 0x100, 0x00, alloc(3, 3, 3, 0, 0), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x110, 0x10, nop_m(), nop_i(),
         br_ret(6)),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "r8": 0x5a,
    }, entry=0x10)

test_rse_exception_flushrs_preserves_high_local = require_registers(
    "rse_exception_flushrs_preserves_high_local", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(2, 8, 0, 0, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, 0x00, nop_m(), alloc(68, 49, 41, 0, 0),
         addl(70, 0x1234, 0)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_call(6, 0x110, 0x180)),
        (0x120, 0x00, nop_m(), adds(8, 0, 70),
         nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
        (0x180, 0x00, nop_m(), alloc(61, 36, 32, 0, 0),
         break_m(0x42)),
        (0x190, 0x10, nop_m(), nop_i(),
         br_ret(6)),
        (IA64_BREAK_VECTOR, 0x00, mov_m_ar_gr(21, 64), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x190)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x00, alloc(2, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x10, nop_m(), nop_i(),
         br_call(7, IA64_BREAK_VECTOR + 0x50,
                 IA64_BREAK_VECTOR + 0x100)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, nop_m(),
         mov_m_gr_ar(21, 64), nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(), rfi_b()),
        (IA64_BREAK_VECTOR + 0x100, 0x00, alloc(3, 3, 3, 0, 0), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x110, 0x10, nop_m(), nop_i(),
         br_ret(7)),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1234,
    }, entry=0x10)

test_rse_exception_bspstore_restore_skips_unrelated_frame = require_registers(
    "rse_exception_bspstore_restore_skips_unrelated_frame", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(36, 14, 6, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(37, 0x123456789abcdef0)),
        (0x80, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_ar_gr(8, 17), adds(9, 0, 37),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (IA64_BREAK_VECTOR, *movl_mlx(3, 0x0ff000)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, 0x18, nop_m(), nop_m(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x30, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(20, 0x90)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x100000,
        "r9": 0x123456789abcdef0,
        "cfm_sof": 14,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_rfi_context_switch_drops_empty_frame_snapshots = require_registers(
    "rse_rfi_context_switch_drops_empty_frame_snapshots", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(1, 5, 5, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(32, 0x1111)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x200, 0x00, nop_m(), alloc(33, 5, 5, 0, 0),
         nop_i()),
        (0x210, *movl_mlx(32, 0x2222)),
        (0x220, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x240, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x250, 0x10, nop_m(), nop_i(),
         br_cond(0x250, 0x250)),
        (IA64_BREAK_VECTOR, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x200)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(21, 1 << 63)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(21, 23), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(22, 0x240)),
        (IA64_BREAK_VECTOR + 0x70, 0x00, mov_m_gr_ar(0, 64),
         mov_b_gr(0, 22), nop_i()),
        (IA64_BREAK_VECTOR + 0x80, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x250,
        "exception": IA64_EXCP_NONE,
        "r8": 0x2222,
    }, entry=0x10)

test_rse_rfi_invalid_ifs_context_switch_drops_snapshot = require_registers(
    "rse_rfi_invalid_ifs_context_switch_drops_snapshot", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(1, 5, 5, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(32, 0x1111)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x90, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_cond(0xa0, 0xa0)),
        (0x100, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x200, 0x00, nop_m(), alloc(33, 5, 5, 0, 0),
         nop_i()),
        (0x210, *movl_mlx(32, 0x2222)),
        (0x220, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x240, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x250, 0x10, nop_m(), nop_i(),
         br_cond(0x250, 0x250)),
        (IA64_BREAK_VECTOR, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x200)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_m_gr_cr(0, 23), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x50, *movl_mlx(22, 0x240)),
        (IA64_BREAK_VECTOR + 0x60, 0x00, mov_m_gr_ar(0, 64),
         mov_b_gr(0, 22), nop_i()),
        (IA64_BREAK_VECTOR + 0x70, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x250,
        "exception": IA64_EXCP_NONE,
        "r8": 0x2222,
    }, entry=0x10)

test_rse_rfi_invalid_ifs_same_bspstore_keeps_guest_gr = require_registers(
    "rse_rfi_invalid_ifs_same_bspstore_keeps_guest_gr", [
        (0x10, *movl_mlx(2, 1 << 13)),
        (0x20, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x20, 0x40)),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, alloc(1, 5, 5, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(32, 0x1111)),
        (0x80, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x200, 0x00, nop_m(), adds(8, 0, 32),
         nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_cond(0x210, 0x210)),
        (IA64_BREAK_VECTOR, *movl_mlx(32, 0x2222)),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, 0x200)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(0, 23),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x210,
        "exception": IA64_EXCP_NONE,
        "r8": 0x2222,
        "r32": 0x2222,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_rfi_invalid_ifs_exact_iip_keeps_guest_gr = require_registers(
    "rse_rfi_invalid_ifs_exact_iip_keeps_guest_gr", [
        (0x10, *movl_mlx(2, HIGH_TR_BASE + 0x20000)),
        (0x20, *movl_mlx(19, IA64_PSR_IC | IA64_PSR_DT)),
        (0x30, *movl_mlx(3, 0x100000)),
        (0x40, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x50, 0x10, mov_gr_psr_full(19), nop_i(),
         br_cond(0x50, 0x60)),
        (0x60, 0x00, alloc(1, 5, 5, 0, 0), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(32, 0x1111)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x100)),
        (0x100, 0x00, ld8(4, 2), adds(8, 0, 32),
         nop_i()),
        (0x110, 0x10, nop_m(), nop_i(),
         br_cond(0x110, 0x110)),
        (IA64_ALT_DTLB_VECTOR, 0x00, ssm(IA64_PSR_IC),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x10, 0x00, srlz_d(),
         nop_i(), nop_i()),
        (IA64_ALT_DTLB_VECTOR + 0x20, 0x00, break_m(0x43),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR, *movl_mlx(32, 0x2222)),
        (IA64_BREAK_VECTOR + 0x10,
         *movl_mlx(20, IA64_PSR_IC | IA64_PSR_DT | (1 << 41))),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(21, 0x100)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x00, mov_m_gr_cr(21, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(0, 23),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x110,
        "exception": IA64_EXCP_NONE,
        "r8": 0x2222,
        "r32": 0x2222,
        "cfm_sof": 5,
        "cfm_sol": 5,
    }, entry=0x10)

test_rse_rfi_unmatched_context_keeps_guest_interruption_resources = \
    require_registers(
    "rse_rfi_unmatched_context_keeps_guest_interruption_resources", [
        (0x10, *movl_mlx(3, 0x100008)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(2, 1 << 13)),
        (0x40, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x40, 0x60)),
        (0x60, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x200, 0x00, mov_m_cr_gr(8, 20), nop_i(), nop_i()),
        (0x210, 0x10, nop_m(), nop_i(),
         br_cond(0x210, 0x210)),
        (IA64_BREAK_VECTOR, *movl_mlx(3, 0x200008)),
        (IA64_BREAK_VECTOR + 0x10, 0x00, mov_ar(3, 18),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x123456789abcdef0)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 20),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0x200)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(20, (1 << 63) | 1)),
        (IA64_BREAK_VECTOR + 0x70, 0x00, mov_m_gr_cr(20, 23),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x80, 0x00, mov_m_gr_cr(0, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x90, 0x10, nop_m(), nop_i(), rfi_b()),
        (0x200000, 0x00, 0x0fedcba987654321, 0, 0),
    ], {
        "ip": 0x210,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r32": bundle_words(0x00, 0x0fedcba987654321, 0, 0)[0],
        "cfm_sof": 1,
    }, entry=0x10)

test_rse_rfi_cross_region_context_ignores_stale_exception_frame = \
    require_registers(
        "rse_rfi_cross_region_context_ignores_stale_exception_frame", [
        (0x10, *movl_mlx(18, LOW_VECTOR_TR_PTE)),
        (0x20, *movl_mlx(20, HIGH_TR_BASE)),
        (0x30, *movl_mlx(7, KERNEL_TR_ITIR)),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, mov_m_gr_cr(20, 20), adds(5, 5, 0), nop_i()),
        (0x60, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0x70, *movl_mlx(3, 0x100000)),
        (0x80, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), alloc(1, 4, 4, 0, 0), nop_i()),
        (0xa0, *movl_mlx(2, IA64_PSR_IC)),
        (0xb0, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0xb0, 0xc0)),
        (0xc0, *movl_mlx(32, 0x1111222233334444)),
        (0xd0, 0x00, break_m(0x42), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR, *movl_mlx(20, 0x123456789abcdef0)),
        (IA64_BREAK_VECTOR + 0x10, 0x00,
         mov_m_gr_cr(20, 22), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, HIGH_TR_PSR)),
        (IA64_BREAK_VECTOR + 0x30, 0x00,
         mov_m_gr_cr(20, 16), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, HIGH_TR_TARGET)),
        (IA64_BREAK_VECTOR + 0x50, 0x00,
         mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, *movl_mlx(20, 0)),
        (IA64_BREAK_VECTOR + 0x70, 0x00,
         mov_m_gr_cr(20, 23), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x80, 0x10,
         nop_m(), nop_i(), rfi_b()),
        (0x4008430, 0x00, rsm(IA64_PSR_IC), nop_i(), nop_i()),
        (0x4008440, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x4008450, 0x00, mov_m_cr_gr(8, 22), nop_i(), nop_i()),
        (0x4008460, 0x10, nop_m(), nop_i(),
         br_cond(HIGH_TR_BASE + 0x8460, HIGH_TR_BASE + 0x8460)),
    ], {
        "ip": HIGH_TR_BASE + 0x8460,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
    }, entry=0x10)

test_rse_rfi_backward_context_ignores_stale_exception_frame = \
    require_registers(
    "rse_rfi_backward_context_ignores_stale_exception_frame", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(2, IA64_PSR_IC)),
        (0x40, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(0x40, 0x200)),
        (0x100, 0x00, rsm(IA64_PSR_IC), nop_i(), nop_i()),
        (0x110, 0x00, srlz_d(), nop_i(), nop_i()),
        (0x120, 0x00, mov_m_cr_gr(8, 22), nop_i(), nop_i()),
        (0x130, 0x10, nop_m(), nop_i(),
         br_cond(0x130, 0x130)),
        (0x200, 0x00, break_m(0x42), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR, *movl_mlx(20, 0x123456789abcdef0)),
        (IA64_BREAK_VECTOR + 0x10, 0x00,
         mov_m_gr_cr(20, 22), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x100)),
        (IA64_BREAK_VECTOR + 0x30, 0x00,
         mov_m_gr_cr(20, 19), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(20, 0)),
        (IA64_BREAK_VECTOR + 0x50, 0x00,
         mov_m_gr_cr(20, 23), nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10,
         nop_m(), nop_i(), rfi_b()),
    ], {
        "ip": 0x130,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
    }, entry=0x10)

test_rse_loadrs_clamps_stacked_grs = require_registers(
    "rse_loadrs_clamps_stacked_grs", [
        (0x10, *movl_mlx(18, 0x0010000004000661)),
        (0x20, *movl_mlx(19, HIGH_TR_PSR)),
        (0x30, *movl_mlx(20, HIGH_TR_TARGET)),
        (0x40, *movl_mlx(21, HIGH_TR_BASE)),
        (0x50, 0x00, adds(7, 0x68, 0), nop_i(),
         nop_i()),
        (0x60, 0x00, mov_m_gr_cr(7, 21), nop_i(),
         nop_i()),
        (0x70, 0x00, mov_m_gr_cr(21, 20), adds(5, 5, 0),
         nop_i()),
        (0x80, 0x00, itr_i(5, 18), nop_i(),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(19, 16), nop_i(),
         nop_i()),
        (0xa0, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0xb0, 0x10, mov_m_gr_cr(0, 23), nop_i(),
         rfi_b()),
        (0x4008430, *movl_mlx(3, 0x100000)),
        (0x4008440, 0x00, mov_m_gr_ar(3, 18), nop_i(), nop_i()),
        (0x4008450, 0x00, nop_m(), alloc(2, 96, 0, 0, 0),
         nop_i()),
        (0x4008460, 0x00, nop_m(), alloc(2, 96, 0, 0, 0),
         nop_i()),
        (0x4008470, 0x00, nop_m(), nop_i(),
         flushrs_enc()),
        (0x4008480, 0x00, nop_m(), nop_i(),
         loadrs_enc()),
        (0x4008490, 0x00, mov_m_psr_gr(31), nop_i(),
         nop_i()),
        (0x40084a0, 0x10, nop_m(), nop_i(),
         br_cond(HIGH_TR_BASE + 0x84a0, HIGH_TR_BASE + 0x84a0)),
    ], {
        "ip": HIGH_TR_BASE + 0x84a0,
        "exception": IA64_EXCP_NONE,
        # mov r=psr.l does not return the separately banked PSR.bn bit.
        "r31": HIGH_TR_PSR & ~(1 << 44),
    }, entry=0x10)

test_rse_loadrs_sets_tear_point = require_registers(
    "rse_loadrs_sets_tear_point", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, *movl_mlx(33, 0x2222)),
        (0x60, *movl_mlx(34, 0x3333)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x100, 0x00, alloc(2, 0, 0, 0, 0), nop_i(),
         nop_i()),
        (0x110, *movl_mlx(3, 8 << 16)),
        (0x120, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x130, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x140, 0x00, mov_m_ar_gr(8, 18), nop_i(),
         nop_i()),
        (0x150, *movl_mlx(3, 0x200000)),
        (0x160, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x170, 0x00, mov_m_ar_gr(9, 17), nop_i(),
         nop_i()),
        (0x180, 0x10, nop_m(), nop_i(),
         br_cond(0x180, 0x180)),
    ], {
        "ip": 0x180,
        "r8": 0x100010,
        "r9": 0x200008,
    }, entry=0x10)

test_rse_loadrs_preserves_clean_partial_rnat_collection = require_registers(
    "rse_loadrs_preserves_clean_partial_rnat_collection", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(3, 0x1001d8)),
        (0x20, *movl_mlx(4, 0xe000000012345678)),
        (0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(3, 0x1001f8)),
        (0x50, 0x00, st8(3, 0), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(3, 0x100220)),
        (0x70, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(4, 1 << 59)),
        (0x90, 0x00, mov_m_gr_ar(4, 19), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(3, 64 << 16)),
        (0xb0, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0xc0, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0xd0, *movl_mlx(3, 16 | (16 << 7))),
        (0xe0, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0xf0, *movl_mlx(3, 0x120)),
        (0x100, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x120, 0x00, nop_m(), nop_i(), nop_i()),
        (0x130, 0x00, nop_m(), nop_i(), nop_i()),
        (0x140, 0x00, nop_m(), adds(10, 0, 40),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_cond(0x150, 0x150)),
    ], {
        "ip": 0x150,
        "exception": IA64_EXCP_NONE,
        "r10": 0xe000000012345678,
        "r40_nat": 0,
        "cfm_sof": 16,
        "cfm_sol": 16,
    }, entry=0x800)

"""The RNAT word for the collection containing BSPSTORE is not yet in
memory.  loadrs makes AR.RNAT architecturally undefined, while the fill path
uses separately modeled NaT dispersal state.  Retaining the known partial
collection only in that state prevents the untouched word at 0x1001f8 from
clearing the NaT when r35 is refilled."""
test_rse_loadrs_retains_partial_dispersal_for_fill = require_registers(
    "rse_loadrs_retains_partial_dispersal_for_fill", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(3, 0x100120)),
        (0x20, *movl_mlx(4, 0xe000000087654321)),
        (0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(3, 0x1001f8)),
        (0x50, 0x00, st8(3, 0), nop_i(),
         nop_i()),
        (0x60, *movl_mlx(3, 0x100188)),
        (0x70, 0x00, mov_m_gr_ar(3, 18), nop_i(),
         nop_i()),
        (0x80, *movl_mlx(4, 1 << 36)),
        (0x90, 0x00, mov_m_gr_ar(4, 19), nop_i(),
         nop_i()),
        (0xa0, *movl_mlx(3, 64 << 16)),
        (0xb0, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0xc0, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0xd0, *movl_mlx(3, 16 | (16 << 7))),
        (0xe0, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0xf0, *movl_mlx(3, 0x120)),
        (0x100, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0x110, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x120, 0x00, nop_m(), nop_i(), nop_i()),
        (0x130, 0x00, nop_m(), nop_i(), nop_i()),
        (0x140, 0x00, nop_m(), adds(10, 0, 35),
         nop_i()),
        (0x150, 0x10, nop_m(), nop_i(),
         br_cond(0x150, 0x150)),
    ], {
        "ip": 0x150,
        "exception": IA64_EXCP_NONE,
        "r10": 0xe000000087654321,
        "r35_nat": 1,
        "cfm_sof": 16,
        "cfm_sol": 16,
    }, entry=0x800)

"""loadrs makes AR.RNAT undefined without making its value a fill source.
The covered frame below spills a NaT-bearing r35, stops before its collection
word, and reloads only the top six backing-store words.  The following return
uses the separately modeled NaT dispersal state for the partial collection,
rather than treating the untouched collection word in memory as authoritative.
"""
test_rse_loadrs_retains_dispersal_below_tear = require_registers(
    "rse_loadrs_retains_dispersal_below_tear", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(39, 40, 40, 0, 0), nop_i()),
        (0x40, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x400, 0),
         nop_i()),
        (0x50, 0x08, ld8_fill_postinc(35, 6, 0), nop_i(), nop_i()),
        (0x60, 0x18, nop_m(), nop_m(), cover_b()),
        (0x70, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x80, *movl_mlx(3, (6 * 8) << 16)),
        (0x90, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0xa0, 0x00, loadrs_enc(), nop_i(), nop_i()),
        (0xb0, *movl_mlx(4, 40 | (40 << 7))),
        (0xc0, 0x00, mov_m_gr_ar(4, 64), nop_i(), nop_i()),
        (0xd0, *movl_mlx(5, 0x100)),
        (0xe0, 0x09, nop_m(), nop_m(), mov_b_gr(0, 5)),
        (0xf0, 0x10, nop_m(), nop_i(), br_ret(0)),
        (0x100, 0x10, nop_m(), nop_i(), br_cond(0x100, 0x100)),
        raw_bundle(0x400, 0x123456789abcdef0, 0),
    ], {
        "ip": 0x100,
        "exception": IA64_EXCP_NONE,
        "r35": 0x123456789abcdef0,
        "r35_nat": 1,
        "cfm_sof": 40,
        "cfm_sol": 40,
    }, entry=0x10, cpu="merced")

test_rse_return_growth_keeps_dirty_bsp_distance = require_registers(
    "rse_return_growth_keeps_dirty_bsp_distance", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 57, 6, 0, 0), nop_i(),
         nop_i()),
        (0x40, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x50, *movl_mlx(3, 0x307)),
        (0x60, 0x00, mov_m_gr_ar(3, 64), nop_i(),
         nop_i()),
        (0x70, *movl_mlx(3, 0xa0)),
        (0x80, 0x09, nop_m(), nop_m(),
         mov_b_gr(0, 3)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0xa0, *movl_mlx(3, 0x200000)),
        (0xb0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0xc0, 0x00, mov_m_ar_gr(8, 17), nop_i(),
         nop_i()),
        (0xd0, 0x10, nop_m(), nop_i(),
         br_cond(0xd0, 0xd0)),
    ], {
        "ip": 0xd0,
        "r8": 0x200198,
        "cfm_sof": 7,
        "cfm_sol": 6,
    }, entry=0x10)

test_rse_bspstore_write_rebases_dirty_partition = require_registers(
    "rse_bspstore_write_rebases_dirty_partition", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, *movl_mlx(33, 0x2222)),
        (0x60, *movl_mlx(34, 0x3333)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, *movl_mlx(3, 0x200000)),
        (0x110, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x120, 0x00, flushrs_enc(), nop_i(),
         nop_i()),
        (0x130, *movl_mlx(3, 0x200000)),
        (0x140, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0x150, 0x00, nop_m(), adds(3, 8, 3),
         nop_i()),
        (0x160, 0x00, ld8(9, 3), nop_i(),
         nop_i()),
        (0x170, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "r8": 0x1111,
        "r9": 0x2222,
    }, entry=0x10)

test_rse_bspstore_rebase_preserves_dirty_cover_prefix = require_registers(
    "rse_bspstore_rebase_preserves_dirty_cover_prefix", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, nop_m(), alloc(2, 8, 0, 0, 0),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111222233334444)),
        (0x50, *movl_mlx(33, 0x2222333344445555)),
        (0x60, *movl_mlx(34, 0x3333444455556666)),
        (0x70, *movl_mlx(35, 0x4444555566667777)),
        (0x80, *movl_mlx(36, 0x5555666677778888)),
        (0x90, *movl_mlx(37, 0x6666777788889999)),
        (0xa0, *movl_mlx(38, 0x777788889999aaaa)),
        (0xb0, *movl_mlx(39, 0x88889999aaaabbbb)),
        (0xc0, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0xd0, *movl_mlx(3, 0x3000e8)),
        (0xe0, *movl_mlx(4, 0xaaaabbbbccccdddd)),
        (0xf0, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x100, *movl_mlx(3, 0x3000f0)),
        (0x110, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x120, *movl_mlx(3, 0x3000f8)),
        (0x130, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x140, 0x00, mov_m_gr_cr(0, 16), nop_i(),
         nop_i()),
        (0x150, *movl_mlx(20, 0x200)),
        (0x160, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0x170, *movl_mlx(20, (1 << 63) | 5)),
        (0x180, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0x190, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x200, 0x18, nop_m(), nop_m(),
         cover_b()),
        (0x210, *movl_mlx(3, 0x300100)),
        (0x220, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x230, 0x00, mov_m_gr_cr(0, 16), nop_i(),
         nop_i()),
        (0x240, *movl_mlx(20, 0x300)),
        (0x250, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (0x260, *movl_mlx(20, (1 << 63) | 8)),
        (0x270, 0x00, mov_m_gr_cr(20, 23), nop_i(),
         nop_i()),
        (0x280, 0x10, nop_m(), nop_i(),
         rfi_b()),
        (0x300, 0x00, nop_m(), adds(8, 0, 32),
         adds(9, 0, 33)),
        (0x310, 0x00, nop_m(), adds(10, 0, 34),
         adds(11, 0, 35)),
        (0x320, 0x00, nop_m(), adds(12, 0, 36),
         adds(13, 0, 37)),
        (0x330, 0x00, nop_m(), adds(14, 0, 38),
         adds(15, 0, 39)),
        (0x340, 0x10, nop_m(), nop_i(),
         br_cond(0x340, 0x340)),
    ], {
        "ip": 0x340,
        "exception": IA64_EXCP_NONE,
        "r8": 0x1111222233334444,
        "r9": 0x2222333344445555,
        "r10": 0x3333444455556666,
        "r11": 0x4444555566667777,
        "r12": 0x5555666677778888,
        "r13": 0x6666777788889999,
        "r14": 0x777788889999aaaa,
        "r15": 0x88889999aaaabbbb,
        "cfm_sof": 8,
        "cfm_sol": 0,
    }, entry=0x10)

test_rse_bspstore_rebase_writes_no_memory = require_registers(
    "rse_bspstore_rebase_writes_no_memory", [
        (0x10, *movl_mlx(3, 0x2001f8)),
        (0x20, *movl_mlx(4, 1 << 57)),
        (0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(3, 0x1001a8)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(39, 14, 10, 0, 0),
         nop_i()),
        (0x70, *movl_mlx(36, 0x123456789abcdef0)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_call(0, 0x80, 0x100)),
        (0x100, *movl_mlx(3, 0x2001a8)),
        (0x110, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x120, *movl_mlx(3, 0x2001f8)),
        (0x130, 0x00, ld8(8, 3), nop_i(),
         nop_i()),
        (0x140, *movl_mlx(3, 0x2001c8)),
        (0x150, 0x00, ld8(9, 3), nop_i(),
         nop_i()),
        (0x160, 0x10, nop_m(), nop_i(),
         br_cond(0x160, 0x160)),
    ], {
        "ip": 0x160,
        "exception": IA64_EXCP_NONE,
        "r8": 1 << 57,
        "r9": 0,
        "cfm_sof": 4,
        "cfm_sol": 0,
    }, entry=0x10)

test_rse_br_ret_fill_ignores_rsc_mode = require_registers(
    "rse_br_ret_fill_ignores_rsc_mode", [
        (0x10, *movl_mlx(3, 0x100000)),
        (0x20, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x30, 0x00, alloc(1, 5, 3, 0, 0), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, mov_m_imm_ar(16, 0), nop_i(), nop_i()),
        (0x70, 0x00, mov_m_ar_gr(8, 18), nop_i(), nop_i()),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, 0x00, alloc(2, 0, 0, 0, 0), nop_i(), nop_i()),
        (0x110, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x120, *movl_mlx(3, 3)),
        (0x130, 0x00, mov_m_gr_ar(3, 16), nop_i(),
         nop_i()),
        (0x140, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "r8": 0x100000,
    }, entry=0x10)

test_gcc_alloc_and_ar_lc = require_registers("gcc_alloc_and_ar_lc", [
    (0x10, 0x00, alloc_m(63, 42, 34, 0, 0), adds(8, 5, 0),
     nop_i()),
    (0x20, 0x02, nop_m(), mov_lc_gr(8),
     mov_ar_lc(9)),
    (0x30, 0x10, nop_m(), nop_i(),
     br_cond(0x30, 0x30)),
], {"ip": 0x30, "r8": 5, "r9": 5}, entry=0x10)

test_rse_call_invalidates_stacked_alat = require_registers(
    "rse_call_invalidates_stacked_alat", [
        (0x10, 0x00, addl(3, 0x100, 0), alloc(35, 5, 3, 0, 0),
         nop_i()),
        (0x20, 0x00, ld8_a(36, 3), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(36, 0x55)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_call(0, 0x40, 0x80)),
        (0x50, 0x00, ld8_c_nc(36, 3), nop_i(),
         nop_i()),
        (0x60, 0x10, nop_m(), nop_i(),
         br_cond(0x60, 0x60)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x100, 0x00, 0x123456789abcdef0, 0,
         0),
    ], {"ip": 0x60, "r36": CHECK_LOAD_DATA}, entry=0x10)

test_cover_b_ignored_fields_decode = require_registers(
    "cover_b_ignored_fields_decode", [
        (0x10, 0x00, nop_m(), alloc(5, 8, 4, 0, 0), nop_i()),
        (0x20, 0x18, nop_m(), nop_m(),
         cover_b_ignored_fields()),
        (0x30, 0x10, nop_m(), nop_i(),
         br_cond(0x30, 0x30)),
    ], {
        "ip": 0x30,
        "exception": IA64_EXCP_NONE,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x10)


def test_rse_small_kernel_frame_preserves_counter_across_spill_calls(qemu):
    bundles = [
        # The caller supplies 19 output registers.  Its thirteenth output is
        # the callee's r44, matching the page-table counter pointer in the
        # failing SOF=21/SOL=19 kernel frame.
        (0x10, *movl_mlx(2, 0x1001e8)),
        (0x20, 0x00, mov_ar(2, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x8000)),
        (0x40, 0x00, st2(3, 0), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), alloc(2, 21, 2, 0, 0), nop_i()),
        (0x60, *movl_mlx(46, 0x8000)),
        (0x70, 0x10, nop_m(), nop_i(), br_call(0, 0x70, 0x100)),

        # alloc r49=ar.pfs,19,0,2,0 encodes SOF=21/SOL=19.
        (0x100, 0x00, nop_m(), alloc(49, 21, 19, 0, 0), nop_i()),
        (0x110, 0x00, nop_m(), adds(47, 0, 0), nop_i()),
    ]
    cursor = 0x120
    for _ in range(13):
        bundles.extend([
            # Preserve the checked-load/add/store grouping used by the
            # kernel, then call through a frame large enough to evict and
            # reload the small caller's stacked registers.
            (cursor, 0x03, ld2_sa(29, 44), nop_i(), nop_i()),
            (cursor + 0x10, 0x01, nop_m(), nop_i(), nop_i()),
            (cursor + 0x20, 0x01, nop_m(), nop_i(), nop_i()),
            (cursor + 0x30, 0x00, ld2_c_clr(29, 44),
             adds(31, 1, 29), addl(52, 2, 0)),
            (cursor + 0x40, 0x0b, adds(47, 1, 47), st2(44, 31),
             nop_i()),
            (cursor + 0x50, 0x10, nop_m(), nop_i(),
             br_call(0, cursor + 0x50, 0x4000)),
        ])
        cursor += 0x60

    terminal_ip = cursor + 0x10
    bundles.extend([
        (cursor, 0x00, ld2(8, 44), adds(9, 0, 44), adds(10, 0, 47)),
        (terminal_ip, 0x10, nop_m(), nop_i(),
         br_cond(terminal_ip, terminal_ip)),

        (0x4000, 0x00, nop_m(), alloc(36, 96, 88, 0, 0), nop_i()),
        (0x4010, *movl_mlx(127, 0x8877665544332211)),
        (0x4020, 0x00, flushrs_enc(), nop_i(), nop_i()),
        (0x4030, 0x00, mov_m_gr_ar(36, 64), nop_i(), nop_i()),
        (0x4040, 0x10, nop_m(), nop_i(), br_ret(0)),
    ])

    run_program(qemu, bundles, entry=0x10, terminal_ip=terminal_ip,
                timeout=5.0, alat=None, smp="4", expected={
                    "exception": IA64_EXCP_NONE,
                    "r8": 13,
                    "r9": 0x8000,
                    "r10": 13,
                    "cfm_sof": 21,
                    "cfm_sol": 19,
                }, name=(
                    "rse_small_kernel_frame_preserves_counter_across_"
                    "spill_calls"))


def test_rse_seventy_output_handoff_preserves_kernel_locals(qemu):
    sentinels = {
        53: 0x1111111111111153,
        54: 0x2222222222222254,
        55: 0x3333333333333355,
        76: 0x4444444444444476,
        91: 0x5555555555555591,
        98: 0x6666666666666698,
    }
    result = run_program(qemu, [
        # Start beside an RNAT collection boundary and shift BOF so both the
        # 70-register handoff and the nested frame wrap the physical stack.
        (0x10, *movl_mlx(2, 0x1001e8)),
        (0x20, 0x00, mov_ar(2, 18), nop_i(), nop_i()),
        (0x30, 0x00, nop_m(), alloc(40, 45, 41, 0, 0), nop_i()),
        (0x40, 0x10, nop_m(), nop_i(), br_call(0, 0x40, 0x100)),

        # A 96-register caller with SOL=26 passes 70 output registers.  The
        # selected output positions become the same r53/r54/r55/r76/r91/r98
        # inputs used by the page-table mapping helper in the crash image.
        (0x100, 0x00, nop_m(), alloc(41, 96, 26, 0, 0), nop_i()),
        (0x110, *movl_mlx(79, sentinels[53])),
        (0x120, *movl_mlx(80, sentinels[54])),
        (0x130, *movl_mlx(81, sentinels[55])),
        (0x140, *movl_mlx(102, sentinels[76])),
        (0x150, *movl_mlx(117, sentinels[91])),
        (0x160, *movl_mlx(124, sentinels[98])),
        (0x170, 0x10, nop_m(), nop_i(), br_call(0, 0x170, 0x300)),

        # Match the kernel frame (SOF=73, SOL=70), then force a wide nested
        # frame to spill and reload across the RNAT boundary before checking
        # the inherited values.
        (0x300, 0x00, nop_m(), alloc(100, 73, 70, 0, 0), nop_i()),
        (0x310, 0x10, nop_m(), nop_i(), br_call(0, 0x310, 0x500)),
        (0x320, 0x00, nop_m(), adds(8, 0, 53), adds(9, 0, 54)),
        (0x330, 0x00, nop_m(), adds(10, 0, 55), adds(11, 0, 76)),
        (0x340, 0x00, nop_m(), adds(14, 0, 91), adds(15, 0, 98)),
        (0x350, 0x10, nop_m(), nop_i(), br_cond(0x350, 0x350)),

        (0x500, 0x00, nop_m(), alloc(36, 96, 88, 0, 0), nop_i()),
        (0x510, 0x00, mov_m_gr_ar(36, 64), nop_i(), nop_i()),
        (0x520, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], entry=0x10, terminal_ip=0x350, timeout=8.0)
    state = result.state
    observed = {
        53: state.gr[8],
        54: state.gr[9],
        55: state.gr[10],
        76: state.gr[11],
        91: state.gr[14],
        98: state.gr[15],
    }
    if (state.ip != 0x350 or
        state.exception != IA64_EXCP_NONE or
        observed != sentinels or
        state.cfm.get("sof") != 73 or
        state.cfm.get("sol") != 70):
        raise RuntimeError(
            "rse_seventy_output_handoff_preserves_kernel_locals failed: "
            f"locals={observed!r} expected={sentinels!r} "
            f"sof={state.cfm.get('sof')!r} sol={state.cfm.get('sol')!r} "
            f"ip={state.ip!r} exception={state.exception!r}\n"
            f"{result.register_output}")

test_cover_rfi_rebases_rotating_floating_registers = require_registers(
    "cover_rfi_rebases_rotating_floating_registers", [
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, *movl_mlx(3, 0x12345678)),
        (0x30, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x40, 0x01, setf_sig(32, 3), mov_i_imm_ar(66, 1), nop_i()),
        (0x50, 0x13, nop_m(), nop_b(), br_ctop_many(0x50, 0x50)),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x80, 0x00, nop_m(), nop_i(), nop_i()),
        (0x90, 0x10, nop_m(), nop_i(), br_cond(0x90, 0x90)),
        (IA64_BREAK_VECTOR, 0x10, nop_m(), nop_i(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10, 0x00, nop_m(), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x80)),
        (IA64_BREAK_VECTOR + 0x30, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x40, 0x10, nop_m(), nop_i(), rfi_b()),
    ], {
        "ip": 0x90,
        "exception": IA64_EXCP_NONE,
        "f33": ExpectedFP(0x12345678, 0x1003e),
        "cfm_rrb_fr": 95,
    }, entry=0x10)

test_cover_rfi_rebases_rotating_general_registers = require_registers(
    "cover_rfi_rebases_rotating_general_registers", [
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, *movl_mlx(3, 0x123456789abcdef0)),
        (0x30, 0x00, alloc(4, 32, 2, 4, 0), nop_i(), nop_i()),
        (0x40, 0x00, mov_gr_psr_full(2), nop_i(), nop_i()),
        (0x50, 0x00, nop_m(), adds(32, 0, 3), mov_i_imm_ar(66, 1)),
        (0x60, 0x13, nop_m(), nop_b(), br_ctop_many(0x60, 0x60)),
        (0x70, 0x00, nop_m(), adds(8, 0, 33), nop_i()),
        (0x80, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x90, 0x00, nop_m(), adds(9, 0, 33), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
        (IA64_BREAK_VECTOR, 0x10, nop_m(), nop_i(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(20, 0x90)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(20, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(), rfi_b()),
    ], {
        "ip": 0xa0,
        "exception": IA64_EXCP_NONE,
        "r8": 0x123456789abcdef0,
        "r9": 0x123456789abcdef0,
        "cfm_rrb_gr": 31,
    }, entry=0x10)

test_cover_rfi_restores_rotating_predicates_by_physical_number = \
    require_registers(
        "cover_rfi_restores_rotating_predicates_by_physical_number", [
            (0x10, *movl_mlx(2, IA64_PSR_IC)),
            (0x20, 0x00, mov_gr_psr_full(2), mov_i_imm_ar(66, 1),
             nop_i()),
            (0x30, 0x01, nop_m(), mov_pr_rot_imm(1 << 16), nop_i()),
            # One rotation leaves physical p16 set, but exposes it as the
            # logical p17 while CFM.rrb.pr is 47.
            (0x40, 0x13, nop_m(), nop_b(),
             br_ctop_many(0x40, 0x40)),
            (0x50, 0x00, nop_m(), mov_pr_gr(8), nop_i()),
            (0x60, 0x00, break_m(0x42), nop_i(), nop_i()),
            (0x70, 0x00, nop_m(), adds(9, 1, 9, qp=17),
             mov_pr_gr(11)),
            (0x80, 0x10, nop_m(), nop_i(), br_cond(0x80, 0x80)),

            # Linux saves PR before cover and restores it before rfi.  mov
            # r=pr and mov pr=r both address the physical PR file as though
            # RRB.PR were zero; cover/rfi rebase the logical view around it.
            (IA64_BREAK_VECTOR, 0x10, nop_m(), mov_pr_gr(31), cover_b()),
            (IA64_BREAK_VECTOR + 0x10, 0x00, nop_m(),
             adds(10, 1, 10, qp=16), mov_gr_pr(31, -2)),
            (IA64_BREAK_VECTOR + 0x20, *movl_mlx(20, 0x70)),
            (IA64_BREAK_VECTOR + 0x30, 0x00,
             mov_m_gr_cr(20, 19), nop_i(), nop_i()),
            (IA64_BREAK_VECTOR + 0x40, 0x10,
             nop_m(), nop_i(), rfi_b()),
        ], {
            "ip": 0x80,
            "exception": IA64_EXCP_NONE,
            "r8": (1 << 16) | 1,
            "r9": 1,
            "r10": 1,
            "r11": (1 << 16) | 1,
            "cfm_rrb_pr": 47,
        }, entry=0x10)

test_mov_pr_rot_with_nonzero_rrb_tracks_logical_predicates = \
    require_registers(
        "mov_pr_rot_with_nonzero_rrb_tracks_logical_predicates", [
            (0x10, *movl_mlx(2, IA64_PSR_IC)),
            (0x20, 0x00, mov_gr_psr_full(2), mov_i_imm_ar(66, 1),
             nop_i()),
            (0x30, 0x13, nop_m(), nop_b(), br_ctop_many(0x30, 0x30)),
            # Physical p16 is logical p17 while CFM.rrb.pr is 47.
            (0x40, 0x01, nop_m(), mov_pr_rot_imm(1 << 16), nop_i()),
            (0x50, 0x00, nop_m(), adds(8, 1, 8, qp=17),
             adds(9, 1, 9, qp=16)),
            (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        ], {
            "ip": 0x60,
            "exception": IA64_EXCP_NONE,
            "r8": 1,
            "r9": 0,
            "cfm_rrb_pr": 47,
        }, entry=0x10)

test_br_call_ret_rebases_rotating_floating_registers = require_registers(
    "br_call_ret_rebases_rotating_floating_registers", [
        (0x10, *movl_mlx(3, 0x12345678)),
        (0x20, 0x01, setf_sig(32, 3), mov_i_imm_ar(66, 1), nop_i()),
        (0x30, 0x13, nop_m(), nop_b(), br_ctop_many(0x30, 0x30)),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x10, nop_m(), nop_i(), br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), nop_i(), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "f33": ExpectedFP(0x12345678, 0x1003e),
        "cfm_rrb_fr": 95,
    }, entry=0x10)

test_clrrrb_rebases_rotating_floating_registers = require_registers(
    "clrrrb_rebases_rotating_floating_registers", [
        (0x10, *movl_mlx(3, 0x12345678)),
        (0x20, 0x01, setf_sig(32, 3), mov_i_imm_ar(66, 1), nop_i()),
        (0x30, 0x13, nop_m(), nop_b(), br_ctop_many(0x30, 0x30)),
        (0x40, 0x00, nop_m(), nop_i(), nop_i()),
        (0x50, 0x13, nop_m(), nop_b(), clrrrb_b()),
        (0x60, 0x00, nop_m(), nop_i(), nop_i()),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "f32": ExpectedFP(0x12345678, 0x1003e),
        "cfm_rrb_fr": 0,
    }, entry=0x10)

test_rse_rfi_selects_matching_outer_exception_frame = require_registers(
    "rse_rfi_selects_matching_outer_exception_frame", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, *movl_mlx(2, IA64_PSR_IC)),
        (0x20, *movl_mlx(3, 0x100000)),
        (0x30, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(1, 0x123456789abcdef0)),
        (0x50, 0x10, mov_gr_psr_full(2), nop_i(),
         br_call(0, 0x50, 0x100)),
        (0x60, 0x00, nop_m(), adds(9, 0, 1),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_cond(0x70, 0x70)),
        (0x100, 0x00, nop_m(), alloc(33, 4, 3, 0, 0),
         nop_i()),
        (0x110, 0x00, nop_m(), mov_gr_b(32, 0),
         adds(34, 0, 1)),
        (0x120, 0x00, break_m(0x42), nop_i(),
         nop_i()),
        (0x130, 0x00, nop_m(), adds(1, 0, 34),
         adds(8, 0, 34)),
        (0x140, 0x00, nop_m(), mov_m_ar_gr(33, 64),
         mov_b_gr(0, 32)),
        (0x150, 0x10, nop_m(), nop_i(), br_ret(0)),
        (IA64_BREAK_VECTOR, 0x08, nop_m(), cmp4_eq_imm(1, 2, 0, 6),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x10, 0x10, nop_m(), nop_i(),
         br_cond(IA64_BREAK_VECTOR + 0x10,
                 IA64_BREAK_VECTOR + 0x30, qp=1)),
        (IA64_BREAK_VECTOR + 0x20, 0x10, nop_m(), nop_i(),
         br_cond(IA64_BREAK_VECTOR + 0x20,
                 IA64_BREAK_VECTOR + 0x90, qp=2)),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(),
         cover_b()),
        (IA64_BREAK_VECTOR + 0x40, *movl_mlx(3, 0x200000)),
        (IA64_BREAK_VECTOR + 0x50, 0x00, mov_ar(3, 18), adds(6, 1, 0),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x60, 0x10, mov_gr_psr_full(2), nop_i(),
         br_cond(IA64_BREAK_VECTOR + 0x60, IA64_BREAK_VECTOR + 0x70)),
        (IA64_BREAK_VECTOR + 0x70, 0x00, break_m(0x43), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x90, 0x00, nop_m(), alloc(34, 4, 3, 0, 0),
         nop_i()),
        (IA64_BREAK_VECTOR + 0xa0, *movl_mlx(34, 0x0badf00ddeadbeef)),
        (IA64_BREAK_VECTOR + 0xb0, *movl_mlx(3, 0x100000)),
        (IA64_BREAK_VECTOR + 0xc0, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0xd0, *movl_mlx(20, IA64_PSR_IC)),
        (IA64_BREAK_VECTOR + 0xe0, 0x00, mov_m_gr_cr(20, 16),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0xf0, *movl_mlx(20, 0x130)),
        (IA64_BREAK_VECTOR + 0x100, 0x00, mov_m_gr_cr(20, 19),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x110,
         *movl_mlx(20, (1 << 63) | 4 | (3 << 7))),
        (IA64_BREAK_VECTOR + 0x120, 0x00, mov_m_gr_cr(20, 23),
         nop_i(), nop_i()),
        (IA64_BREAK_VECTOR + 0x130, 0x10, nop_m(), nop_i(),
         rfi_b()),
    ], {
        "ip": 0x70,
        "exception": IA64_EXCP_NONE,
        "r1": 0x123456789abcdef0,
        "r8": 0x123456789abcdef0,
        "r9": 0x123456789abcdef0,
        "r10": 0,
        "cfm_sof": 0,
        "cfm_sol": 0,
    }, entry=0x800)

test_cover_requires_group_stop = require_exception(
    "cover_requires_group_stop", [
        (0x10, 0x18, nop_m(), nop_m(), int(cover_b())),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10)

test_alloc_requires_group_start = require_exception(
    "alloc_requires_group_start", [
        (0x10, 0x00, nop_m(), nop_i(), nop_i()),
        (0x20, 0x00, alloc_m(1, 1, 0, 0, 0), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x20)

test_loadrs_rejects_nonzero_rsc_mode = require_exception(
    "loadrs_rejects_nonzero_rsc_mode", [
        (0x10, 0x01, nop_m(), adds(3, 1, 0), nop_i()),
        (0x20, 0x03, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0x30, 0x01, loadrs_enc(), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x30)

test_loadrs_capacity_illegal_precedes_backing_store_uda = require_exception(
    "loadrs_capacity_illegal_precedes_backing_store_uda", [
        # BSP-0x318..BSP contains 97 register words and two RNAT words.
        (0x10, *movl_mlx(3, 1 << (IA64_IMPL_PA_BITS + 1))),
        (0x20, 0x00, mov_m_gr_ar(3, 18), nop_i(), nop_i()),
        (0x30, *movl_mlx(3, 0x318 << 16)),
        (0x40, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
        (0x50, 0x01, loadrs_enc(), nop_i(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x50)

test_mov_bspstore_rsc_mode_precedes_source_nat = require_exception(
    "mov_bspstore_rsc_mode_precedes_source_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_m_imm_ar(16, 1), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_ar(16, 18), nop_i(), nop_i()),
        (0x200, 0x00, 0, 0, 0),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x40)

test_mov_rnat_rsc_mode_precedes_source_nat = require_exception(
    "mov_rnat_rsc_mode_precedes_source_nat", [
        (0x10, 0x00, mov_m_imm_ar(36, 1), addl(6, 0x200, 0), nop_i()),
        (0x20, 0x08, ld8_fill_postinc(16, 6, 0), nop_i(), nop_i()),
        (0x30, 0x00, mov_m_imm_ar(16, 1), nop_i(), nop_i()),
        (0x40, 0x00, mov_m_gr_ar(16, 19), nop_i(), nop_i()),
        (0x200, 0x00, 0, 0, 0),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x40)

test_rsc_reserved_field_fault = require_exception(
    "rsc_reserved_field_fault", [
        (0x10, *movl_mlx(3, 1 << 63)),
        (0x20, 0x00, mov_m_gr_ar(3, 16), nop_i(), nop_i()),
    ], IA64_EXCP_RESERVED_REG_FIELD, fault_ip=0x20)

test_stacked_gr_destination_out_of_frame = require_exception(
    "stacked_gr_destination_out_of_frame", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, 0x00, nop_m(), adds(32, 1, 0), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, entry=0x800)

test_predicated_off_stacked_gr_destination_does_not_fault = require_registers(
    "predicated_off_stacked_gr_destination_does_not_fault", [
        (0x10, 0x00, nop_m(), adds(32, 1, 0, qp=1), nop_i()),
        (0x20, 0x10, nop_m(), nop_i(), br_cond(0x20, 0x20)),
    ], {
        "ip": 0x20,
        "exception": IA64_EXCP_NONE,
        "r32": 0,
    }, entry=0x10)

test_predicated_off_stacked_write_keeps_following_write_valid = \
    require_registers(
        "predicated_off_stacked_write_keeps_following_write_valid", [
            (0x10, 0x00, alloc_m(9, 2, 2, 0, 0), nop_i(), nop_i()),
            (0x20, 0x01, nop_m(), cmp_eq_imm(6, 7, 0, 0), nop_i()),
            (0x30, 0x10, nop_m(), nop_i(), br_cond(0x30, 0x50)),
            # The p7 write is the first frame check in this translation
            # block.  Its false predicate must not affect the next check.
            (0x50, 0x01, nop_m(), adds(32, 1, 0, qp=7),
             adds(33, 2, 0)),
            (0x60, 0x10, nop_m(), nop_i(), br_cond(0x60, 0x60)),
        ], {
            "ip": 0x60,
            "exception": IA64_EXCP_NONE,
            "r32": 0,
            "r33": 2,
        }, entry=0x10)

test_postincrement_base_out_of_frame = require_exception(
    "postincrement_base_out_of_frame", [
        *_empty_frame_prologue(0x800, 0x10),
        (0x10, 0x08, lfetch_postinc(32, 8), nop_m(), nop_i()),
    ], IA64_EXCP_ILLEGAL, fault_ip=0x10, entry=0x800)

test_br_ia_bspstore_mismatch_illegal = require_exception(
    "br_ia_bspstore_mismatch_illegal", [
        (0x10, 0x00, alloc_m(2, 8, 4, 0, 0), nop_i(),
         nop_i()),
        (0x20, 0x10, nop_m(), nop_i(),
         br_call(0, 0x20, 0x40)),
        (0x40, 0x10, nop_m(), nop_i(),
         br_indirect(7, btype=1)),
    ],
    IA64_EXCP_ILLEGAL,
    fault_ip=0x40,
)

test_br_ctop_strcpy_pipeline_survives_cover_rfi = require_registers(
    "br_ctop_strcpy_pipeline_survives_cover_rfi", [
        *dtr_setup_bundles(0x10, HIGH_TR_BASE, 0x400000),
        (0x70, *movl_mlx(20, HIGH_TR_BASE + 0x8000)),
        (0x80, *movl_mlx(19, HIGH_TR_BASE + 0xc000)),
        (0x90, *movl_mlx(21, HIGH_TR_BASE + 0xc000 + 1000)),
        (0xa0, *movl_mlx(5, -1)),
        # mov psr.l updates only the low half and preserves PSR.bn.
        (0xb0, *movl_mlx(17, (1 << 13) | (1 << 17))),
        (0xc0, 0x08, mov_gr_psr_full(17), srlz_d(), nop_i()),
        (0xd0, 0x00, alloc(2, 32, 2, 4, 0), mov_lc_gr(5), nop_i()),
        (0xe0, 0x00, nop_m(), mov_pr_rot_imm(0x10000), nop_i()),
        (0xf0, 0x00, ld8_s_postinc(32, 20, 8, qp=16),
         chk_s_i(34, 0xf0, 0x250, qp=18), nop_i()),
        (0x100, 0x02, adds(31, 0, 34, qp=18),
         czx1_r(24, 34, qp=18), cmp_eq_imm(0, 7, 8, 24, qp=18)),
        (0x110, 0x00, nop_m(), adds(30, 1, 30),
         cmp4_eq_imm(6, 0, 129, 30)),
        (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x200, qp=6)),
        (0x130, 0x10, nop_m(), nop_i(), br_cond(0x130, 0x160, qp=7)),
        (0x140, 0x11, st8_postinc(19, 34, 8, qp=18), nop_i(),
         br_ctop_few(0x140, 0xf0)),
        (0x160, 0x00, ld8_postinc(9, 21, 8), nop_i(), nop_i()),
        (0x170, 0x00, ld8(10, 21), adds(8, 0, 19), nop_i()),
        (0x180, 0x10, nop_m(), nop_i(), br_cond(0x180, 0x180)),
        (0x200, 0x00, break_m(0x42), nop_i(), nop_i()),
        (0x250, 0x10, nop_m(), nop_i(), br_cond(0x250, 0x250)),
        (IA64_BREAK_VECTOR, 0x10, nop_m(), nop_i(), cover_b()),
        (IA64_BREAK_VECTOR + 0x10, *movl_mlx(14, 0x130)),
        (IA64_BREAK_VECTOR + 0x20, 0x00, mov_m_gr_cr(14, 19), nop_i(),
         nop_i()),
        (IA64_BREAK_VECTOR + 0x30, 0x10, nop_m(), nop_i(), rfi_b()),
        *(raw_bundle(0x408000 + i * 8, _strcpy_pipeline_data[i],
                     _strcpy_pipeline_data[i + 1])
          for i in range(0, len(_strcpy_pipeline_data), 2)),
    ], {"exception": IA64_EXCP_NONE, "ip": 0x180,
        "r8": HIGH_TR_BASE + 0xc000 + 1008,
        "r9": 0x6d6e6f7071727374, "r10": 0,
        "r30": 129}, entry=0x10)

GROUP = 'rse'
"""SDM Vol.2 6.5.2: "The RSE never saves partial NaT collections to the
backing store."  Until AR.BSPSTORE reaches a group's collection word the RSE
has not written it, so the NaT bits of the registers already spilled into that
group are held in AR.RNAT and the backing-store word still holds whatever
occupied that memory beforehand.  Here the word at 0x1001f8 is preloaded with
all ones, the nested frame spills only ten registers so BSPSTORE stops far
below it, and loadrs then leaves AR.RNAT undefined.  The mandatory fills on
br.ret must not turn that untouched word into NaT on ten live registers."""
test_rse_partial_group_fill_ignores_unwritten_collection = require_registers(
    "rse_partial_group_fill_ignores_unwritten_collection", [
        (0x10, *movl_mlx(3, 0x1001f8)),
        (0x20, *movl_mlx(4, 0xffffffffffffffff)),
        (0x30, 0x00, st8(3, 4), nop_i(),
         nop_i()),
        (0x40, *movl_mlx(3, 0x100000)),
        (0x50, 0x00, mov_ar(3, 18), nop_i(),
         nop_i()),
        (0x60, 0x00, nop_m(), alloc(39, 20, 16, 0, 0),
         nop_i()),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, 0x100)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (0x100, 0x00, nop_m(), alloc(40, 90, 80, 0, 0),
         nop_i()),
        (0x110, 0x00, loadrs_enc(), nop_i(),
         nop_i()),
        (0x120, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": 0x80,
        "exception": IA64_EXCP_NONE,
        "r32_nat": 0, "r33_nat": 0, "r34_nat": 0, "r35_nat": 0,
        "r36_nat": 0, "r37_nat": 0, "r38_nat": 0, "r39_nat": 0,
        "r40_nat": 0, "r41_nat": 0,
        "cfm_sof": 20, "cfm_sol": 16,
    }, entry=0x10, cpu="merced")


CASE_NAMES = (

    'rse_architectural_reset_exposes_full_frame',
    'rse_boot_handoff_preserves_empty_frame',
    'alloc_m34_ignored_bits_decode',
    'alloc_predicated_illegal',
    'alloc_rejects_frame_larger_than_register_stack',
    'alloc_rejects_locals_larger_than_frame',
    'alloc_rejects_rotating_region_larger_than_frame',
    'alloc_requires_group_start',
    'br_call_ret_rebases_rotating_floating_registers',
    'br_ctop_strcpy_pipeline_survives_cover_rfi',
    'br_ia_bspstore_mismatch_illegal',
    'clrrrb_rebases_rotating_floating_registers',
    'cover_b_ignored_fields_decode',
    'cover_requires_group_stop',
    'cover_rfi_rebases_rotating_floating_registers',
    'cover_rfi_rebases_rotating_general_registers',
    'cover_rfi_restores_rotating_predicates_by_physical_number',
    'gcc_alloc_and_ar_lc',
    'loadrs_capacity_illegal_precedes_backing_store_uda',
    'loadrs_rejects_nonzero_rsc_mode',
    'mov_bspstore_rsc_mode_precedes_source_nat',
    'mov_pr_rot_with_nonzero_rrb_tracks_logical_predicates',
    'mov_rnat_rsc_mode_precedes_source_nat',
    'postincrement_base_out_of_frame',
    'predicated_off_stacked_gr_destination_does_not_fault',
    'predicated_off_stacked_write_keeps_following_write_valid',
    'rsc_reserved_field_fault',
    'rsc_write_clips_pl_to_cpl',
    'rse_alloc_call_ret',
    'rse_alloc_preserves_ar_pfs',
    'rse_big_endian_backing_store',
    'rse_big_endian_partial_rnat_store_preserves_backed_prefix',
    'rse_big_endian_rnat_collection',
    'rse_br_ret_completion_trap_waits_for_target_fill',
    'rse_br_ret_ec_restored_before_target_fill_fault',
    'rse_br_ret_fill_dtlb_miss_retries_atomically',
    'rse_br_ret_fill_ignores_rsc_mode',
    'rse_bsp_is_current_frame_base',
    'rse_bspstore_dtlb_miss_retries_spill',
    'rse_bspstore_keeps_saved_frame',
    'rse_bspstore_physical_alias_survives_rt_disable',
    'rse_bspstore_preserves_dirty_partition_across_rnat',
    'rse_bspstore_rebase_preserves_dirty_cover_prefix',
    'rse_bspstore_rebase_writes_no_memory',
    'rse_bspstore_rnat_edit_protocol_overrides_memory_image',
    'rse_bspstore_rewrite_reloads_spilled_frame',
    'rse_bspstore_partial_rnat_store_preserves_backed_prefix',
    'rse_bspstore_write_rebases_dirty_partition',
    'rse_call_invalidates_stacked_alat',
    'rse_seventy_output_handoff_preserves_kernel_locals',
    'rse_small_kernel_frame_preserves_counter_across_spill_calls',
    'rse_call_maps_all_high_output_args',
    'rse_call_moves_output_nat_across_word_boundary',
    'rse_call_preserves_same_bundle_local_write',
    'rse_call_ret_preserves_caller_local',
    'rse_call_ret_preserves_region7_bsp',
    'rse_call_ret_updates_bsp_base',
    'rse_call_sets_callee_input_frame',
    'rse_call_uses_high_sol_output_arg',
    'rse_callee_alloc_stores_input_arg',
    'rse_cover_flushrs_spills_covered_frame',
    'rse_cover_skips_trailing_rnat_slot',
    'rse_deep_call_chain_spills_parent_frames',
    'rse_evict_parent_frames_preserves_caller_local',
    'rse_exception_bspstore_restore_skips_unrelated_frame',
    'rse_exception_flushrs_preserves_high_local',
    'rse_exception_loadrs_preserves_interrupted_call',
    'rse_exception_restores_snapshot_arrays',
    'rse_firmware_unaligned_postinc_marks_stacked_base_dirty',
    'rse_firmware_unmatched_return_restores_matching_frame',
    'rse_flushrs_clears_stale_rnat',
    'rse_flushrs_crosses_reverse_mapped_virtual_pages',
    'rse_large_callee_preserves_high_caller_local',
    'rse_loadrs_bspstore_return_uses_covered_frame',
    'rse_loadrs_clamps_stacked_grs',
    'rse_loadrs_cover_span_restores_embedded_frame',
    'rse_loadrs_cover_span_uses_preserved_sol',
    'rse_loadrs_partial_rnat_store_preserves_backed_prefix',
    'rse_loadrs_retains_dispersal_below_tear',
    'rse_loadrs_preserves_clean_partial_rnat_collection',
    'rse_loadrs_retains_partial_dispersal_for_fill',
    'rse_loadrs_sets_tear_point',
    'rse_loadrs_writeback_preserves_defined_zero',
    'rse_loadrs_writeback_yields_to_bspstore_edit',
    'rse_loadrs_zero_current_frame_invalidates_parents',
    'rse_loadrs_zero_sol_return_keeps_bsp_without_cover',
    'rse_mandatory_spill_consumes_psr_dd',
    'rse_mandatory_target_fill_debug_sets_isr_rs_ir',
    'rse_merced_flushrs_invalidates_spilled_frame',
    'rse_merced_partial_rnat_store_preserves_backed_prefix',
    'rse_displaced_dispersal_survives_loadrs_physical_reuse',
    'rse_merced_respill_preserves_filled_rnat_prefix',
    'rse_merced_return_publishes_filled_rnat_collection',
    'rse_manual_rfi_loadrs_restores_current_frame_base',
    'rse_manual_rfi_smaller_frame_restores_current_frame_base',
    'rse_nested_alloc_call_preserves_output_arg',
    'rse_nested_return_restores_bspstore_base',
    'rse_partial_group_fill_ignores_unwritten_collection',
    'rse_partial_rnat_store_merge_all_split_points',
    'rse_parent_spill_keeps_call_snapshot',
    'rse_postinc_after_flushrs_preserves_register_value',
    'rse_return_growth_keeps_dirty_bsp_distance',
    'rse_return_reclaims_clean_keeps_unreached_rnat',
    'rse_return_reclaims_clean_rebases_rnat_collection',
    'rse_rnat_defined_mask_and_bspstore_invalidation',
    'rse_rfi_advanced_iip_bspstore_switch_loads_external_frame',
    'rse_rfi_advanced_iip_preserves_nested_call_locals',
    'rse_rfi_advanced_iip_uses_covered_current_frame',
    'rse_rfi_backward_context_ignores_stale_exception_frame',
    'rse_rfi_bspstore_advanced_iip_spills_parent_frame',
    'rse_rfi_bspstore_rebase_preserves_interrupted_call',
    'rse_rfi_bypassed_call_drops_returned_frame',
    'rse_rfi_context_switch_drops_empty_frame_snapshots',
    'rse_rfi_cross_region_context_ignores_stale_exception_frame',
    'rse_rfi_does_not_overwrite_trailing_rnat',
    'rse_rfi_does_not_spill_dirty_frame_rnat',
    'rse_rfi_flushed_interrupted_frame_reads_backing_store',
    'rse_rfi_flushed_same_iip_uses_interrupted_frame',
    'rse_rfi_invalid_ifs_context_switch_drops_snapshot',
    'rse_rfi_invalid_ifs_exact_iip_keeps_guest_gr',
    'rse_rfi_invalid_ifs_same_bspstore_keeps_guest_gr',
    'rse_rfi_invalid_ifs_unchanged_stack_restores_call',
    'rse_rfi_loadrs_preserves_caller_locals_after_nested_return',
    'rse_rfi_loadrs_preserves_caller_locals_after_syscall_error',
    'rse_rfi_loadrs_preserves_gp_save_after_syscall_error',
    'rse_rfi_loadrs_preserves_high_sol_caller_local',
    'rse_rfi_loadrs_preserves_low_sol_caller_local',
    'rse_guest_same_mfb_ld8_s_r41_survives_nested_pressure_poison',
    'rse_guest_same_mfb_ld8_s_r41_survives_same_cpu_context_remap_poison',
    'rse_guest_same_mfb_ld8_s_r41_survives_bol79_to8_remap_poison',
    'rse_guest_frame_ld8_s_nat_survives_nested_switch',
    'rse_guest_frame_ld8_s_survives_nested_switch',
    'rse_guest_bol82_ld8_s_deferred_nat_recovers_after_helper_switches',
    'rse_guest_bol82_ld8_s_survives_helper_switches_poison',
    'rse_guest_bol82_ld8_s_survives_precall_switch_poison',
    'rse_guest_helper_ld8_s_r41_survives_nested_pressure',
    'rse_guest_helper_ld8_s_r41_survives_nested_switch',
    'rse_rfi_nested_handler_preserves_faulting_frame',
    'rse_rfi_repeated_cover_preserves_latest_dirty_partition',
    'rse_rfi_repeated_cover_uses_latest_current_frame',
    'rse_rfi_same_iip_preserves_interrupted_call_nat',
    'rse_rfi_selects_matching_outer_exception_frame',
    'rse_rfi_unmatched_context_keeps_guest_interruption_resources',
    'rse_rfi_user_context_preserves_loadrs_dirty_partition',
    'rse_rt_enables_protection_key_checks',
    'rse_key_miss_precedes_mandatory_data_debug_with_dt_clear',
    'rse_sal_alt_dtlb_resumes_br_ret_fill',
    'rse_rt_translates_with_dt_disabled',
    'rse_physical_spill_fault_sets_isr_rs',
    'rse_physical_target_fill_fault_sets_isr_rs_ir',
    'rse_spill_fault_sets_isr_rs',
    'rse_tracked_return_redirties_reused_frame',
    'rse_untracked_return_redirties_restored_frame',
    'rse_untracked_return_restores_high_caller_local',
    'rse_untracked_return_resyncs_trimmed_rnat',
    'rse_untracked_return_uses_each_rnat_collection',
    'rse_uses_rsc_pl_for_access_rights',
    'rse_write_only_rnat_store_preserves_backed_prefix',
    'rse_zero_sol_cover_return_restores_bsp_base',
    'stacked_gr_destination_out_of_frame',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals())
