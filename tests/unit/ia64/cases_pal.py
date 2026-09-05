"""PAL procedure and firmware microprograms."""

from __future__ import annotations

from .case import IA64Case, bind_cases
from .encoding import (
    IA64_DBR_IMPLEMENTED_COUNT,
    IA64_IBR_IMPLEMENTED_COUNT,
    IA64_CR_ITM,
    IA64_CR_ITV,
    IA64_EXCP_NONE,
    IA64_PHYS_UC_BIT,
    MADISON_PAL_VERSION_VALUE,
    MADISON_9M_PAL_VERSION_VALUE,
    MADISON_9M_PAL_CACHE_INFO_L2_U_1,
    MADISON_9M_PAL_CACHE_INFO_L2_U_2,
    MADISON_PAL_CACHE_INFO_L2_U_1,
    MADISON_PAL_CACHE_INFO_L2_U_2,
    MADISON_PAL_VM_SUMMARY_INFO_1,
    MADISON_TR_COUNT,
    MADISON_ZX6000_PAL_CACHE_INFO_L2_U_1,
    MADISON_ZX6000_PAL_CACHE_INFO_L2_U_2,
    MERCED_INSERTABLE_PAGE_SIZE_MASK,
    MERCED_IMPL_PA_BITS,
    MERCED_PAL_PERF_MON_INFO_VALUE,
    MERCED_PAL_VERSION_VALUE,
    MERCED_PAL_CACHE_INFO_L0_2,
    MERCED_PAL_CACHE_INFO_L0_D_1,
    MERCED_PAL_CACHE_INFO_L0_I_1,
    MERCED_PAL_CACHE_INFO_L1_U_1,
    MERCED_PAL_CACHE_INFO_L1_U_2,
    MERCED_PAL_CACHE_INFO_L2_U_1,
    MERCED_PAL_CACHE_INFO_L2_U_2,
    MERCED_PAL_VM_INFO_L0_D,
    MERCED_PAL_VM_INFO_L0_I,
    MERCED_PAL_VM_INFO_L1_D,
    MERCED_PAL_VM_SUMMARY_INFO_1,
    MERCED_PAL_VM_SUMMARY_INFO_2,
    MERCED_PURGEABLE_PAGE_SIZE_MASK,
    MCKINLEY_PAL_VERSION_VALUE,
    MCKINLEY_PAL_CACHE_INFO_L2_U_1,
    MCKINLEY_PAL_CACHE_INFO_L2_U_2,
    MONTECITO_TR_COUNT,
    MONTVALE_PAL_VERSION_VALUE,
    PAL_AR_IMPLEMENTED_HIGH,
    PAL_AR_IMPLEMENTED_LOW,
    PAL_BRAND_BUFFER,
    PAL_BRAND_INFO,
    PAL_BUS_GET_FEATURES,
    PAL_BUS_SET_FEATURES,
    PAL_CACHE_FLUSH,
    PAL_CACHE_INFO,
    PAL_CACHE_INFO_L0_2,
    PAL_CACHE_INFO_L0_D_1,
    PAL_CACHE_INFO_L0_I_1,
    PAL_CACHE_INFO_L1_D_1,
    PAL_CACHE_INFO_L1_D_2,
    PAL_CACHE_INFO_L1_I_1,
    PAL_CACHE_INFO_L1_I_2,
    PAL_CACHE_INFO_L2_U_1,
    PAL_CACHE_INFO_L2_U_2,
    PAL_CACHE_INIT,
    PAL_CACHE_LINE_INIT,
    PAL_CACHE_PROT_DATA_NONE,
    PAL_CACHE_PROT_INFO,
    PAL_CACHE_PROT_TAG_NONE_L0,
    PAL_CACHE_SHARED_INFO,
    PAL_CACHE_SUMMARY,
    PAL_COPY_BUFFER_ALIGN,
    PAL_COPY_BUFFER_SIZE,
    PAL_COPY_INFO,
    PAL_COPY_PAL,
    PAL_COPY_TARGET,
    PAL_CR_IMPLEMENTED_HIGH,
    PAL_CR_IMPLEMENTED_LOW,
    PAL_CR_READ_SIDE_EFFECT_HIGH,
    PAL_DEBUG_INFO,
    PAL_FIXED_ADDR,
    PAL_FREQ_BASE,
    PAL_FREQ_RATIOS,
    PAL_HALT,
    PAL_HALT_INFO,
    PAL_HALT_INFO_BUFFER,
    PAL_HALT_LIGHT,
    PAL_HALT_LIGHT_INFO,
    PAL_HALT_STATE1_INFO,
    PAL_INSERTABLE_PAGE_SIZE_MASK,
    PAL_INTERRUPT_BLOCK_DEFAULT,
    PAL_IO_BLOCK_DEFAULT,
    PAL_LOGICAL_TO_PHYSICAL,
    PAL_MC_CLEAR_LOG,
    PAL_MC_DRAIN,
    PAL_MC_DYNAMIC_STATE,
    PAL_MC_ERROR_INFO,
    PAL_MC_EXPECTED,
    PAL_MC_REGISTER_MEM,
    PAL_MC_RESUME,
    PAL_MEM_ATTRIB,
    PAL_MEM_ATTRIB_ITANIUM2,
    MERCED_PAL_MEM_ATTRIB,
    MERCED_PAL_IO_BLOCK_DEFAULT,
    PAL_MEM_FOR_TEST,
    PAL_PERF_BUFFER,
    PAL_PERF_MON_INFO,
    PAL_PLATFORM_ADDR,
    PAL_PLATFORM_INTERRUPT_BLOCK,
    PAL_PLATFORM_IO_BLOCK,
    PAL_PMI_ENTRYPOINT,
    PAL_PREFETCH_VIS,
    PAL_PROC_ENTRY,
    PAL_PROC_GET_FEATURES,
    PAL_PROC_SET_FEATURES,
    PAL_PTCE_INFO,
    PAL_PURGE_PAGE_SIZE_MASK,
    PAL_RATIO_16_1,
    PAL_RATIO_16_3,
    PAL_RATIO_15_1,
    PAL_RATIO_4_3,
    PAL_RATIO_4_1,
    PAL_RATIO_8_1,
    PAL_REGISTER_INFO,
    PAL_RSE_INFO,
    PAL_SELF_TEST_STATE_TESTED,
    PAL_TEST_PROC,
    PAL_TR_TEST_IFA,
    PAL_TR_TEST_ITIR,
    PAL_TR_TEST_PTE,
    PAL_TR_VALID_ALL,
    PAL_VERSION,
    PAL_VERSION_VALUE,
    PAL_VIRTUAL_CODE_BASE,
    PAL_VIRTUAL_CODE_ENTRY,
    PAL_VIRTUAL_CODE_ENTRY_PA,
    PAL_VIRTUAL_CODE_PTE,
    PAL_VIRTUAL_ITIR,
    PAL_VIRTUAL_PROC_BASE,
    PAL_VIRTUAL_PROC_ENTRY,
    PAL_VIRTUAL_PROC_PTE,
    PAL_VIRTUAL_PSR,
    PAL_VIRTUAL_RR,
    PAL_VM_INFO,
    PAL_VM_INFO_L0,
    PAL_VM_INFO_L1,
    PAL_VM_PAGE_SIZE,
    PAL_VM_SUMMARY,
    PAL_VM_SUMMARY_INFO_1,
    PAL_VM_SUMMARY_INFO_2,
    PAL_VM_TR_READ,
    addl,
    adds,
    alloc,
    br_call,
    br_call_indirect,
    br_cond,
    br_ret,
    bundle_words,
    itr_d,
    itr_i,
    ld8,
    ld8_s,
    mov_b_gr,
    mov_cpuid,
    mov_gr_psr_full,
    mov_m_gr_cr,
    mov_rr_write,
    movl_mlx,
    nop_i,
    nop_m,
    pal_break,
    pal_call_program,
    pal_stacked_call_program,
    require_registers,
    rfi_to_gr,
    run_program_jit,
    srlz_i,
    st8,
    sum_um,
)


CPU_PROFILE_IDENTITIES = (
    ("merced", 0x0000000007000804, 0,
     0x0000883001008830),
    ("itanium", 0x0000000007000804, 0,
     0x0000883001008830),
    ("madison", 0x000000001f010504, 1,
     0x0000057301000573),
    ("deerfield", 0x000000001f010504, 1,
     0x0000057301000573),
    ("madison-1.5m", 0x000000001f010504, 1,
     0x0000057301000573),
    ("madison-3m", 0x000000001f010504, 1,
     0x0000057301000573),
    ("madison-4m", 0x000000001f010504, 1,
     0x0000057301000573),
    ("madison-6m", 0x000000001f010504, 1,
     0x0000057301000573),
    ("madison-zx6000", 0x000000001f010504, 1,
     0x0000057301000573),
    ("mckinley", 0x000000001f000704, 1,
     0x0000077901000779),
    ("mckinley-900", 0x000000001f000704, 1,
     0x0000077901000779),
    ("mckinley-1000", 0x000000001f000704, 1,
     0x0000077901000779),
    ("madison-9m", 0x000000001f020204, 1,
     0x0000022501000225),
    ("montecito", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9010", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9015", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9020", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9030", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9040", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montecito-9050", 0x0000000020000704, 5,
     0x0000096801000968),
    ("itanium2", 0x0000000020000704, 5,
     0x0000096801000968),
    ("montvale", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9110n", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9120n", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9130m", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9140m", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9140n", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9150m", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9150n", 0x0000000020010104, 5,
     0x0000010801000108),
    ("montvale-9152m", 0x0000000020010104, 5,
     0x0000010801000108),
)


def _cpu_profile_identity_case(cpu, cpuid_version, cpuid_features,
                               pal_version):
    name = "cpu_profile_identity_" + cpu.replace("-", "_")
    return require_registers(name, [
        (0x10, 0x00, nop_m(), addl(31, 3, 0), nop_i()),
        (0x20, 0x00, mov_cpuid(27, 31), addl(31, 4, 0), nop_i()),
        (0x30, 0x00, mov_cpuid(26, 31), nop_i(), nop_i()),
        (0x40, *movl_mlx(28, PAL_VERSION)),
        (0x50, 0x00, nop_m(), adds(29, 0, 0), adds(30, 0, 0)),
        (0x60, 0x10, nop_m(), adds(31, 0, 0),
         br_call(0, 0x60, PAL_PROC_ENTRY)),
        (0x70, 0x10, nop_m(), nop_i(), br_cond(0x70, 0x70)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x70,
        "r8": 0,
        "r9": pal_version,
        "r10": pal_version,
        "r26": cpuid_features,
        "r27": cpuid_version,
    }, entry=0x10, cpu=cpu)


def _test_cpu_profile_identity_table(qemu):
    for profile in CPU_PROFILE_IDENTITIES:
        _cpu_profile_identity_case(*profile)(qemu)


test_cpu_profile_identity_table = IA64Case(
    name="cpu_profile_identity_table",
    runner=_test_cpu_profile_identity_table,
)


test_pal_halt_light_wakes_on_due_itm = require_registers(
    "pal_halt_light_wakes_on_due_itm", [
        (0x10, 0x00, adds(3, 0xef, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, mov_m_gr_cr(3, IA64_CR_ITV), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x200000)),
        (0x40, 0x00, mov_m_gr_cr(4, IA64_CR_ITM), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(28, PAL_HALT_LIGHT)),
        (0x60, *movl_mlx(19, (1 << 13) | (1 << 14))),
        (0x70, 0x10, mov_gr_psr_full(19), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(),
         nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x3000, 0x10, nop_m(), adds(31, 0x5a, 0),
         br_cond(0x3000, 0x3010)),
        (0x3010, 0x10, nop_m(), nop_i(),
         br_cond(0x3010, 0x3010)),
    ], {
        "ip": 0x3010,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r31": 0x5a,
    }, entry=0x10)

test_pal_halt_light_stops_at_pal_continuation = require_registers(
    "pal_halt_light_stops_at_pal_continuation", [
        (0x10, *movl_mlx(28, PAL_HALT_LIGHT)),
        (0x20, 0x10, nop_m(), nop_i(),
         br_call(0, 0x20, PAL_PROC_ENTRY)),
        (0x30, 0x00, nop_m(), adds(31, 0x5a, 0),
         nop_i()),
        (0x40, 0x10, nop_m(), nop_i(),
         br_cond(0x40, 0x40)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(),
         nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ], {
        "ip": PAL_PROC_ENTRY + 0x10,
        "halted": 1,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r31": 0,
    }, entry=0x10)

test_pal_halt_light_reserved_arg = require_registers(
    "pal_halt_light_reserved_arg",
    pal_call_program(PAL_HALT_LIGHT, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_HALT_LIGHT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0,
     "halted": 0},
    entry=0x10)

test_pal_halt_wakes_on_due_itm = require_registers(
    "pal_halt_wakes_on_due_itm", [
        (0x10, 0x00, adds(3, 0xef, 0), nop_i(),
         nop_i()),
        (0x20, 0x00, mov_m_gr_cr(3, IA64_CR_ITV), nop_i(),
         nop_i()),
        (0x30, *movl_mlx(4, 0x200000)),
        (0x40, 0x00, mov_m_gr_cr(4, IA64_CR_ITM), nop_i(),
         nop_i()),
        (0x50, *movl_mlx(28, PAL_HALT)),
        (0x60, 0x00, nop_m(), addl(29, 1, 0), addl(30, 0, 0)),
        (0x70, *movl_mlx(19, (1 << 13) | (1 << 14))),
        (0x80, 0x10, mov_gr_psr_full(19), addl(31, 0, 0),
         br_call(0, 0x80, PAL_PROC_ENTRY)),
        (0x90, 0x10, nop_m(), nop_i(),
         br_cond(0x90, 0x90)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(),
         nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (0x3000, 0x10, nop_m(), adds(31, 0x5a, 0),
         br_cond(0x3000, 0x3010)),
        (0x3010, 0x10, nop_m(), nop_i(),
         br_cond(0x3010, 0x3010)),
    ], {
        "ip": 0x3010,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r9": 0,
        "r31": 0x5a,
    }, entry=0x10)

_pal_cache_flush_patch_low, _pal_cache_flush_patch_high = bundle_words(
    0x11, nop_m(), adds(21, 2, 0), br_cond(0x120, 0x180)
)

test_pal_cache_flush_invalidates_translated_target = require_registers(
    "pal_cache_flush_invalidates_translated_target", [
        (0x10, 0x10, nop_m(), nop_i(),
         br_cond(0x10, 0x120)),
        (0x40, *movl_mlx(16, 0x120)),
        (0x50, *movl_mlx(17, _pal_cache_flush_patch_low)),
        (0x60, *movl_mlx(18, _pal_cache_flush_patch_high)),
        (0x70, 0x00, st8(16, 17), adds(19, 8, 16),
         nop_i()),
        (0x80, 0x00, st8(19, 18), addl(28, PAL_CACHE_FLUSH, 0),
         nop_i()),
        (0x90, 0x00, nop_m(), addl(29, 4, 0),
         addl(30, 3, 0)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, PAL_PROC_ENTRY)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0x120)),
        (0x120, 0x11, nop_m(), adds(20, 1, 20),
         br_cond(0x120, 0x40)),
        (0x180, 0x10, nop_m(), nop_i(),
         br_cond(0x180, 0x180)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x180,
        "exception": IA64_EXCP_NONE,
        "r8": 0,
        "r20": 1,
        "r21": 2,
    }, entry=0x10)


def test_pal_cache_flush_keeps_unrelated_tb_cache(qemu):
    stats, output = run_program_jit(
        qemu,
        pal_call_program(PAL_CACHE_FLUSH,
                         [(29, 4), (30, 3), (31, 0)]),
        entry=0x10, terminal_ip=0x60)
    if stats.get("TB count", 0) < 1:
        raise AssertionError(f"missing translated TB:\n{output}")
    if stats.get("TB flush count") != 0:
        raise AssertionError(
            "PAL_CACHE_FLUSH discarded the coherent TCG code cache:\n" +
            output)



# ── PAL tests ──

test_pal_version = require_registers("pal_version",
    pal_call_program(PAL_VERSION), {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
    "r9": PAL_VERSION_VALUE, "r10": PAL_VERSION_VALUE}, entry=0x10)

test_pal_version_merced = require_registers("pal_version_merced",
    pal_call_program(PAL_VERSION), {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
    "r9": MERCED_PAL_VERSION_VALUE, "r10": MERCED_PAL_VERSION_VALUE},
    entry=0x10, cpu="merced")

test_pal_version_madison = require_registers("pal_version_madison",
    pal_call_program(PAL_VERSION), {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
    "r9": MADISON_PAL_VERSION_VALUE, "r10": MADISON_PAL_VERSION_VALUE},
    entry=0x10, cpu="madison")

test_pal_version_madison_zx6000 = require_registers(
    "pal_version_madison_zx6000", pal_call_program(PAL_VERSION),
    {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
     "r9": MADISON_PAL_VERSION_VALUE, "r10": MADISON_PAL_VERSION_VALUE},
    entry=0x10, cpu="madison-zx6000")

test_pal_version_mckinley = require_registers(
    "pal_version_mckinley", pal_call_program(PAL_VERSION),
    {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
     "r9": MCKINLEY_PAL_VERSION_VALUE,
     "r10": MCKINLEY_PAL_VERSION_VALUE},
    entry=0x10, cpu="mckinley")

test_pal_version_madison_9m = require_registers(
    "pal_version_madison_9m", pal_call_program(PAL_VERSION),
    {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
     "r9": MADISON_9M_PAL_VERSION_VALUE,
     "r10": MADISON_9M_PAL_VERSION_VALUE},
    entry=0x10, cpu="madison-9m")

test_pal_version_montvale = require_registers(
    "pal_version_montvale", pal_call_program(PAL_VERSION),
    {"ip": 0x30, "r28": PAL_VERSION, "r8": 0,
     "r9": MONTVALE_PAL_VERSION_VALUE,
     "r10": MONTVALE_PAL_VERSION_VALUE},
    entry=0x10, cpu="montvale")

test_pal_return_values_clear_nat = require_registers(
    "pal_return_values_clear_nat", [
        (0x10, 0x00, sum_um(0x8), adds(3, 0x104, 0), nop_i()),
        (0x20, 0x00, ld8_s(8, 3), nop_i(), nop_i()),
        (0x30, 0x00, ld8_s(9, 3), nop_i(), nop_i()),
        (0x40, 0x00, ld8_s(10, 3), nop_i(), nop_i()),
        (0x50, 0x00, ld8_s(11, 3), nop_i(), nop_i()),
        (0x60, *movl_mlx(28, PAL_VERSION)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x80,
        "r8": 0,
        "r9": MERCED_PAL_VERSION_VALUE,
        "r10": MERCED_PAL_VERSION_VALUE,
        "r11": 0,
        "r8_nat": 0,
        "r9_nat": 0,
        "r10_nat": 0,
        "r11_nat": 0,
    }, entry=0x10, cpu="merced")

test_pal_version_reserved_arg = require_registers("pal_version_reserved_arg",
    pal_call_program(PAL_VERSION, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VERSION,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_rse_info = require_registers("pal_rse_info",
    pal_call_program(PAL_RSE_INFO),
    {"ip": 0x30, "r28": PAL_RSE_INFO, "r8": 0, "r9": 96, "r10": 0},
    entry=0x10)

test_pal_rse_info_madison = require_registers("pal_rse_info_madison",
    pal_call_program(PAL_RSE_INFO),
    {"ip": 0x30, "r28": PAL_RSE_INFO, "r8": 0, "r9": 96, "r10": 0},
    entry=0x10, cpu="madison")

test_pal_rse_info_merced = require_registers("pal_rse_info_merced",
    pal_call_program(PAL_RSE_INFO),
    {"ip": 0x30, "r28": PAL_RSE_INFO, "r8": 0, "r9": 96, "r10": 0},
    entry=0x10, cpu="merced")

test_pal_rse_info_reserved_arg = require_registers("pal_rse_info_reserved_arg",
    pal_call_program(PAL_RSE_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_RSE_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_summary = require_registers("pal_vm_summary",
    pal_call_program(PAL_VM_SUMMARY),
    {"ip": 0x30, "r28": PAL_VM_SUMMARY, "r8": 0,
    "r9": PAL_VM_SUMMARY_INFO_1, "r10": PAL_VM_SUMMARY_INFO_2}, entry=0x10)

test_pal_vm_summary_merced = require_registers("pal_vm_summary_merced",
    pal_call_program(PAL_VM_SUMMARY),
    {"ip": 0x30, "r28": PAL_VM_SUMMARY, "r8": 0,
     "r9": MERCED_PAL_VM_SUMMARY_INFO_1,
     "r10": MERCED_PAL_VM_SUMMARY_INFO_2},
    entry=0x10, cpu="merced")

test_pal_vm_summary_madison = require_registers("pal_vm_summary_madison",
    pal_call_program(PAL_VM_SUMMARY),
    {"ip": 0x30, "r28": PAL_VM_SUMMARY, "r8": 0,
     "r9": MADISON_PAL_VM_SUMMARY_INFO_1,
     "r10": PAL_VM_SUMMARY_INFO_2},
    entry=0x10, cpu="madison")

test_pal_vm_summary_reserved_arg = require_registers(
    "pal_vm_summary_reserved_arg",
    pal_call_program(PAL_VM_SUMMARY, [(29, 0), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_VM_SUMMARY,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_summary = require_registers("pal_cache_summary",
    pal_call_program(PAL_CACHE_SUMMARY),
    {"ip": 0x30, "r28": PAL_CACHE_SUMMARY, "r8": 0,
    "r9": 3, "r10": 5}, entry=0x10)

test_pal_cache_summary_madison = require_registers(
    "pal_cache_summary_madison",
    pal_call_program(PAL_CACHE_SUMMARY),
    {"ip": 0x30, "r28": PAL_CACHE_SUMMARY, "r8": 0,
     "r9": 3, "r10": 4}, entry=0x10, cpu="madison")

test_pal_cache_summary_merced = require_registers(
    "pal_cache_summary_merced",
    pal_call_program(PAL_CACHE_SUMMARY),
    {"ip": 0x30, "r28": PAL_CACHE_SUMMARY, "r8": 0,
     "r9": 3, "r10": 4}, entry=0x10, cpu="merced")

test_pal_cache_summary_reserved_arg = require_registers(
    "pal_cache_summary_reserved_arg",
    pal_call_program(PAL_CACHE_SUMMARY, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_SUMMARY,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_info = require_registers("pal_cache_info",
    pal_call_program(PAL_CACHE_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": PAL_CACHE_INFO_L0_I_1, "r10": PAL_CACHE_INFO_L0_2,
     "r11": 0}, entry=0x10)

test_pal_cache_info_l0_data = require_registers("pal_cache_info_l0_data",
    pal_call_program(PAL_CACHE_INFO, [(29, 0), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": PAL_CACHE_INFO_L0_D_1, "r10": PAL_CACHE_INFO_L0_2,
     "r11": 0}, entry=0x10)

test_pal_cache_info_l1_data = require_registers(
    "pal_cache_info_l1_data",
    pal_call_program(PAL_CACHE_INFO, [(29, 1), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": PAL_CACHE_INFO_L1_D_1, "r10": PAL_CACHE_INFO_L1_D_2,
     "r11": 0}, entry=0x10)

test_pal_cache_info_l1_instruction = require_registers(
    "pal_cache_info_l1_instruction",
    pal_call_program(PAL_CACHE_INFO, [(29, 1), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": PAL_CACHE_INFO_L1_I_1, "r10": PAL_CACHE_INFO_L1_I_2,
     "r11": 0}, entry=0x10)

test_pal_cache_info_l2_unified = require_registers(
    "pal_cache_info_l2_unified",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": PAL_CACHE_INFO_L2_U_1, "r10": PAL_CACHE_INFO_L2_U_2,
     "r11": 0}, entry=0x10)

test_pal_cache_info_l2_unified_madison = require_registers(
    "pal_cache_info_l2_unified_madison",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MADISON_PAL_CACHE_INFO_L2_U_1,
     "r10": MADISON_PAL_CACHE_INFO_L2_U_2, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_cache_info_l2_unified_madison_zx6000 = require_registers(
    "pal_cache_info_l2_unified_madison_zx6000",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MADISON_ZX6000_PAL_CACHE_INFO_L2_U_1,
     "r10": MADISON_ZX6000_PAL_CACHE_INFO_L2_U_2, "r11": 0},
    entry=0x10, cpu="madison-zx6000")

test_pal_cache_info_l2_unified_mckinley = require_registers(
    "pal_cache_info_l2_unified_mckinley",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MCKINLEY_PAL_CACHE_INFO_L2_U_1,
     "r10": MCKINLEY_PAL_CACHE_INFO_L2_U_2, "r11": 0},
    entry=0x10, cpu="mckinley")

test_pal_cache_info_l2_unified_madison_9m = require_registers(
    "pal_cache_info_l2_unified_madison_9m",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MADISON_9M_PAL_CACHE_INFO_L2_U_1,
     "r10": MADISON_9M_PAL_CACHE_INFO_L2_U_2, "r11": 0},
    entry=0x10, cpu="madison-9m")

test_pal_cache_info_l2_unified_montecito_9010 = require_registers(
    "pal_cache_info_l2_unified_montecito_9010",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": ((PAL_CACHE_INFO_L2_U_1 & ~(0xff << 8)) | (6 << 8)),
     "r10": ((PAL_CACHE_INFO_L2_U_2 & ~0xffffffff) |
             (6 * 1024 * 1024)),
     "r11": 0}, entry=0x10, cpu="montecito-9010")

test_pal_cache_info_l2_unified_montecito_9040 = require_registers(
    "pal_cache_info_l2_unified_montecito_9040",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": ((PAL_CACHE_INFO_L2_U_1 & ~(0xff << 8)) | (9 << 8)),
     "r10": ((PAL_CACHE_INFO_L2_U_2 & ~0xffffffff) |
             (9 * 1024 * 1024)),
     "r11": 0}, entry=0x10, cpu="montecito-9040")

test_pal_cache_info_invalid = require_registers("pal_cache_info_invalid",
    pal_call_program(PAL_CACHE_INFO, [(29, 3), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_info_l2_unified_bad_type = require_registers(
    "pal_cache_info_l2_unified_bad_type",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_info_merced_l0_instruction = require_registers(
    "pal_cache_info_merced_l0_instruction",
    pal_call_program(PAL_CACHE_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MERCED_PAL_CACHE_INFO_L0_I_1,
     "r10": MERCED_PAL_CACHE_INFO_L0_2, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_info_merced_l0_data = require_registers(
    "pal_cache_info_merced_l0_data",
    pal_call_program(PAL_CACHE_INFO, [(29, 0), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MERCED_PAL_CACHE_INFO_L0_D_1,
     "r10": MERCED_PAL_CACHE_INFO_L0_2, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_info_merced_l1_unified = require_registers(
    "pal_cache_info_merced_l1_unified",
    pal_call_program(PAL_CACHE_INFO, [(29, 1), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MERCED_PAL_CACHE_INFO_L1_U_1,
     "r10": MERCED_PAL_CACHE_INFO_L1_U_2, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_info_merced_l1_instruction_invalid = require_registers(
    "pal_cache_info_merced_l1_instruction_invalid",
    pal_call_program(PAL_CACHE_INFO, [(29, 1), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_info_merced_l2_unified = require_registers(
    "pal_cache_info_merced_l2_unified",
    pal_call_program(PAL_CACHE_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INFO, "r8": 0,
     "r9": MERCED_PAL_CACHE_INFO_L2_U_1,
     "r10": MERCED_PAL_CACHE_INFO_L2_U_2, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_freq_base = require_registers("pal_freq_base",
    pal_call_program(PAL_FREQ_BASE),
    {"ip": 0x30, "r28": PAL_FREQ_BASE, "r8": 0,
    "r9": 100000000, "r10": 0, "r11": 0}, entry=0x10)

test_pal_freq_base_madison_zx6000 = require_registers(
    "pal_freq_base_madison_zx6000", pal_call_program(PAL_FREQ_BASE),
    {"ip": 0x30, "r28": PAL_FREQ_BASE, "r8": 0,
     "r9": 100000000, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison-zx6000")

test_pal_freq_base_reserved_arg = require_registers(
    "pal_freq_base_reserved_arg",
    pal_call_program(PAL_FREQ_BASE, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_FREQ_BASE,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_freq_ratios = require_registers("pal_freq_ratios",
    pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
    "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
    "r11": PAL_RATIO_4_1}, entry=0x10)

test_pal_freq_ratios_madison = require_registers(
    "pal_freq_ratios_madison", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_4_1,
     "r11": PAL_RATIO_16_1}, entry=0x10, cpu="madison")

test_pal_freq_ratios_madison_zx6000 = require_registers(
    "pal_freq_ratios_madison_zx6000", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_15_1, "r10": PAL_RATIO_4_1,
     "r11": PAL_RATIO_15_1}, entry=0x10, cpu="madison-zx6000")

test_pal_freq_ratios_madison_9m = require_registers(
    "pal_freq_ratios_madison_9m", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
     "r11": PAL_RATIO_16_1}, entry=0x10, cpu="madison-9m")

test_pal_freq_ratios_merced = require_registers(
    "pal_freq_ratios_merced", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_8_1, "r10": PAL_RATIO_4_3,
     "r11": PAL_RATIO_8_1}, entry=0x10, cpu="merced")

test_pal_freq_ratios_montecito_9010 = require_registers(
    "pal_freq_ratios_montecito_9010", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
     "r11": PAL_RATIO_4_1}, entry=0x10, cpu="montecito-9010")

test_pal_freq_ratios_montecito_9015 = require_registers(
    "pal_freq_ratios_montecito_9015", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": (14 << 32) | 1, "r10": PAL_RATIO_4_1,
     "r11": (7 << 32) | 2}, entry=0x10, cpu="montecito-9015")

test_pal_freq_ratios_montecito_9020 = require_registers(
    "pal_freq_ratios_montecito_9020", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": (142 << 32) | 10, "r10": PAL_RATIO_16_3,
     "r11": (71 << 32) | 20}, entry=0x10, cpu="montecito-9020")

test_pal_freq_ratios_montecito_9030 = require_registers(
    "pal_freq_ratios_montecito_9030", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
     "r11": PAL_RATIO_4_1}, entry=0x10, cpu="montecito-9030")

test_pal_freq_ratios_montecito_9040 = require_registers(
    "pal_freq_ratios_montecito_9040", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
     "r11": PAL_RATIO_4_1}, entry=0x10, cpu="montecito-9040")

test_pal_freq_ratios_montecito_9050 = require_registers(
    "pal_freq_ratios_montecito_9050", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": PAL_RATIO_16_1, "r10": PAL_RATIO_16_3,
     "r11": PAL_RATIO_4_1}, entry=0x10, cpu="montecito-9050")

test_pal_freq_ratios_montvale_9140m = require_registers(
    "pal_freq_ratios_montvale_9140m", pal_call_program(PAL_FREQ_RATIOS),
    {"ip": 0x30, "r28": PAL_FREQ_RATIOS, "r8": 0,
     "r9": (1666 << 32) | 100, "r10": (20 << 32) | 3,
     "r11": (1666 << 32) | 400}, entry=0x10, cpu="montvale-9140m")

test_pal_freq_ratios_reserved_arg = require_registers(
    "pal_freq_ratios_reserved_arg",
    pal_call_program(PAL_FREQ_RATIOS, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_FREQ_RATIOS,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_page_size = require_registers("pal_vm_page_size",
    pal_call_program(PAL_VM_PAGE_SIZE),
    {"ip": 0x30, "r28": PAL_VM_PAGE_SIZE, "r8": 0,
    "r9": PAL_INSERTABLE_PAGE_SIZE_MASK, "r10": PAL_PURGE_PAGE_SIZE_MASK},
    entry=0x10)

test_pal_vm_page_size_merced = require_registers(
    "pal_vm_page_size_merced",
    pal_call_program(PAL_VM_PAGE_SIZE),
    {"ip": 0x30, "r28": PAL_VM_PAGE_SIZE, "r8": 0,
     "r9": MERCED_INSERTABLE_PAGE_SIZE_MASK,
     "r10": MERCED_PURGEABLE_PAGE_SIZE_MASK},
    entry=0x10, cpu="merced")

test_pal_vm_page_size_reserved_arg = require_registers(
    "pal_vm_page_size_reserved_arg",
    pal_call_program(PAL_VM_PAGE_SIZE, [(29, 0), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_VM_PAGE_SIZE,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_ptce_info = require_registers("pal_ptce_info",
    pal_call_program(PAL_PTCE_INFO),
    {"ip": 0x30, "r28": PAL_PTCE_INFO, "r8": 0,
     "r9": 0, "r10": (1 << 32) | 1,
     "r11": 0}, entry=0x10)

test_pal_ptce_info_madison = require_registers("pal_ptce_info_madison",
    pal_call_program(PAL_PTCE_INFO),
    {"ip": 0x30, "r28": PAL_PTCE_INFO, "r8": 0,
     "r9": 0, "r10": (1 << 32) | 1,
     "r11": 0}, entry=0x10, cpu="madison")

test_pal_ptce_info_merced = require_registers("pal_ptce_info_merced",
    pal_call_program(PAL_PTCE_INFO),
    {"ip": 0x30, "r28": PAL_PTCE_INFO, "r8": 0,
     "r9": 0, "r10": (1 << 32) | 1,
     "r11": 0}, entry=0x10, cpu="merced")

test_pal_ptce_info_reserved_arg = require_registers(
    "pal_ptce_info_reserved_arg",
    pal_call_program(PAL_PTCE_INFO, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PTCE_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_info = require_registers("pal_vm_info",
    pal_call_program(PAL_VM_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": PAL_VM_INFO_L0, "r10": 1 << 12,
     "r11": 0}, entry=0x10)

test_pal_vm_info_l0_data = require_registers("pal_vm_info_l0_data",
    pal_call_program(PAL_VM_INFO, [(29, 0), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": PAL_VM_INFO_L0, "r10": 1 << 12,
     "r11": 0}, entry=0x10)

test_pal_vm_info_l1_data = require_registers("pal_vm_info_l1_data",
    pal_call_program(PAL_VM_INFO, [(29, 1), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": PAL_VM_INFO_L1, "r10": PAL_INSERTABLE_PAGE_SIZE_MASK,
     "r11": 0}, entry=0x10)

test_pal_vm_info_l1_instruction = require_registers(
    "pal_vm_info_l1_instruction",
    pal_call_program(PAL_VM_INFO, [(29, 1), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": PAL_VM_INFO_L1, "r10": PAL_INSERTABLE_PAGE_SIZE_MASK,
     "r11": 0}, entry=0x10)

test_pal_vm_info_l2_invalid = require_registers("pal_vm_info_l2_invalid",
    pal_call_program(PAL_VM_INFO, [(29, 2), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO,
     "r8": -2 & 0xffffffffffffffff, "r9": 0, "r10": 0,
     "r11": 0}, entry=0x10)

test_pal_vm_info_invalid = require_registers("pal_vm_info_invalid",
    pal_call_program(PAL_VM_INFO, [(29, 0), (30, 4), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_info_merced_l0_instruction = require_registers(
    "pal_vm_info_merced_l0_instruction",
    pal_call_program(PAL_VM_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": MERCED_PAL_VM_INFO_L0_I,
     "r10": MERCED_INSERTABLE_PAGE_SIZE_MASK, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_info_merced_l0_data = require_registers(
    "pal_vm_info_merced_l0_data",
    pal_call_program(PAL_VM_INFO, [(29, 0), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": MERCED_PAL_VM_INFO_L0_D,
     "r10": MERCED_INSERTABLE_PAGE_SIZE_MASK, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_info_merced_l1_data = require_registers(
    "pal_vm_info_merced_l1_data",
    pal_call_program(PAL_VM_INFO, [(29, 1), (30, 2), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO, "r8": 0,
     "r9": MERCED_PAL_VM_INFO_L1_D,
     "r10": MERCED_INSERTABLE_PAGE_SIZE_MASK, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_info_merced_l1_instruction_invalid = require_registers(
    "pal_vm_info_merced_l1_instruction_invalid",
    pal_call_program(PAL_VM_INFO, [(29, 1), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_VM_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_tr_read_dtr = require_registers("pal_vm_tr_read_dtr", [
    (0x10, *movl_mlx(18, PAL_TR_TEST_PTE)),
    (0x20, 0x00, nop_m(), addl(19, PAL_TR_TEST_IFA & ~0xfff, 0),
     nop_i()),
    (0x30, 0x00, nop_m(), addl(7, PAL_TR_TEST_ITIR, 0), addl(5, 5, 0)),
    (0x40, 0x00, mov_m_gr_cr(19, 20), nop_i(), nop_i()),
    (0x50, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
    (0x60, 0x00, itr_d(5, 18), nop_i(), nop_i()),
    (0x70, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
    (0x80, *movl_mlx(28, PAL_VM_TR_READ)),
    (0x90, *movl_mlx(32, PAL_VM_TR_READ)),
    (0xa0, 0x00, nop_m(), addl(33, 5, 0), addl(34, 1, 0)),
    (0xb0, 0x00, nop_m(), addl(35, 0x2000, 0), nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(), br_call(0, 0xc0, PAL_PROC_ENTRY)),
    (0xd0, 0x00, nop_m(), addl(2, 0x2000, 0), nop_i()),
    (0xe0, 0x00, ld8(20, 2), adds(2, 8, 2), nop_i()),
    (0xf0, 0x00, ld8(21, 2), adds(2, 8, 2), nop_i()),
    (0x100, 0x00, ld8(22, 2), adds(2, 8, 2), nop_i()),
    (0x110, 0x00, ld8(23, 2), nop_i(), nop_i()),
    (0x120, 0x10, nop_m(), nop_i(), br_cond(0x120, 0x120)),
    (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
    (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
], {"ip": 0x120, "r28": PAL_VM_TR_READ, "r8": 0,
    "r9": PAL_TR_VALID_ALL, "r10": 0, "r11": 0,
    "r20": PAL_TR_TEST_PTE, "r21": PAL_TR_TEST_ITIR,
    "r22": PAL_TR_TEST_IFA, "r23": PAL_TR_TEST_ITIR}, entry=0x10)

test_pal_vm_tr_read_max_dtr = require_registers("pal_vm_tr_read_max_dtr", [
        (0x10, *movl_mlx(18, PAL_TR_TEST_PTE)),
        (0x20, 0x00, nop_m(), addl(19, PAL_TR_TEST_IFA & ~0xfff, 0),
         nop_i()),
        (0x30, 0x00, mov_m_gr_cr(19, 20),
         addl(5, MONTECITO_TR_COUNT - 1, 0),
         addl(7, PAL_TR_TEST_ITIR, 0)),
        (0x40, 0x00, mov_m_gr_cr(7, 21), nop_i(), nop_i()),
        (0x50, 0x00, itr_d(5, 18), nop_i(), nop_i()),
        (0x60, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
        (0x70, *movl_mlx(28, PAL_VM_TR_READ)),
        (0x80, *movl_mlx(32, PAL_VM_TR_READ)),
        (0x90, 0x00, nop_m(),
         addl(33, MONTECITO_TR_COUNT - 1, 0), addl(34, 1, 0)),
        (0xa0, 0x00, nop_m(), addl(35, 0x2000, 0), nop_i()),
        (0xb0, 0x10, nop_m(), nop_i(), br_call(0, 0xb0, PAL_PROC_ENTRY)),
        (0xc0, 0x00, nop_m(), addl(2, 0x2000, 0), nop_i()),
        (0xd0, 0x00, ld8(20, 2), adds(2, 8, 2), nop_i()),
        (0xe0, 0x00, ld8(21, 2), adds(2, 8, 2), nop_i()),
        (0xf0, 0x00, ld8(22, 2), adds(2, 8, 2), nop_i()),
        (0x100, 0x00, ld8(23, 2), nop_i(), nop_i()),
        (0x110, 0x10, nop_m(), nop_i(), br_cond(0x110, 0x110)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {"ip": 0x110, "r28": PAL_VM_TR_READ, "r8": 0,
        "r9": PAL_TR_VALID_ALL, "r10": 0, "r11": 0,
        "r20": PAL_TR_TEST_PTE, "r21": PAL_TR_TEST_ITIR,
        "r22": PAL_TR_TEST_IFA, "r23": PAL_TR_TEST_ITIR}, entry=0x10)

test_pal_vm_tr_read_empty = require_registers("pal_vm_tr_read_empty",
    pal_stacked_call_program(PAL_VM_TR_READ, [4, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_vm_tr_read_rejects_first_non_tr = require_registers(
    "pal_vm_tr_read_rejects_first_non_tr",
    pal_stacked_call_program(PAL_VM_TR_READ,
                             [MONTECITO_TR_COUNT, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_tr_read_madison_last_dtr = require_registers(
    "pal_vm_tr_read_madison_last_dtr",
    pal_stacked_call_program(
        PAL_VM_TR_READ, [MADISON_TR_COUNT - 1, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_vm_tr_read_madison_first_invalid_dtr = require_registers(
    "pal_vm_tr_read_madison_first_invalid_dtr",
    pal_stacked_call_program(
        PAL_VM_TR_READ, [MADISON_TR_COUNT, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_vm_tr_read_merced_last_dtr = require_registers(
    "pal_vm_tr_read_merced_last_dtr",
    pal_stacked_call_program(PAL_VM_TR_READ, [47, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_tr_read_merced_first_invalid_dtr = require_registers(
    "pal_vm_tr_read_merced_first_invalid_dtr",
    pal_stacked_call_program(PAL_VM_TR_READ, [48, 1, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_tr_read_merced_last_itr = require_registers(
    "pal_vm_tr_read_merced_last_itr",
    pal_stacked_call_program(PAL_VM_TR_READ, [7, 0, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_tr_read_merced_first_invalid_itr = require_registers(
    "pal_vm_tr_read_merced_first_invalid_itr",
    pal_stacked_call_program(PAL_VM_TR_READ, [8, 0, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_vm_tr_read_invalid = require_registers("pal_vm_tr_read_invalid",
    pal_stacked_call_program(PAL_VM_TR_READ, [0, 2, 0x2000]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_vm_tr_read_misaligned_buffer = require_registers(
    "pal_vm_tr_read_misaligned_buffer",
    pal_stacked_call_program(PAL_VM_TR_READ, [0, 1, 0x2004]),
    {"ip": 0x80, "r28": PAL_VM_TR_READ,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_entry_virtual_itr = require_registers(
    "pal_proc_entry_virtual_itr", [
        (0x10, *movl_mlx(18, PAL_VIRTUAL_CODE_PTE)),
        (0x20, *movl_mlx(22, PAL_VIRTUAL_PROC_PTE)),
        (0x30, *movl_mlx(20, PAL_VIRTUAL_CODE_BASE)),
        (0x40, *movl_mlx(21, PAL_VIRTUAL_PROC_BASE)),
        (0x50, *movl_mlx(25, PAL_VIRTUAL_RR)),
        (0x60, 0x00, mov_rr_write(25, 21), nop_i(), nop_i()),
        (0x70, 0x00, srlz_i(), nop_i(), nop_i()),
        (0x80, 0x00, mov_m_gr_cr(20, 20), adds(7, PAL_VIRTUAL_ITIR, 0),
         nop_i()),
        (0x90, 0x00, mov_m_gr_cr(7, 21), adds(5, 5, 0), nop_i()),
        (0xa0, 0x00, itr_i(5, 18), nop_i(), nop_i()),
        (0xb0, 0x00, mov_m_gr_cr(21, 20), adds(6, 6, 0), nop_i()),
        (0xc0, 0x00, itr_i(6, 22), nop_i(), nop_i()),
        (0xd0, *movl_mlx(23, PAL_VIRTUAL_CODE_ENTRY)),
        (0xe0, *movl_mlx(24, PAL_VIRTUAL_PROC_ENTRY)),
        (0xf0, *movl_mlx(19, PAL_VIRTUAL_PSR)),
        (0x100, 0x00, nop_m(), mov_b_gr(7, 23), nop_i()),
        *rfi_to_gr(0x110, 19, 23),
        (PAL_VIRTUAL_CODE_ENTRY_PA, *movl_mlx(28, PAL_VERSION)),
        (PAL_VIRTUAL_CODE_ENTRY_PA + 0x10, 0x00, nop_m(), mov_b_gr(7, 24),
         nop_i()),
        (PAL_VIRTUAL_CODE_ENTRY_PA + 0x20, 0x10, nop_m(), nop_i(),
         br_call_indirect(0, 7)),
        (PAL_VIRTUAL_CODE_ENTRY_PA + 0x30, 0x10, nop_m(), nop_i(),
         br_cond(PAL_VIRTUAL_CODE_ENTRY + 0x30,
                 PAL_VIRTUAL_CODE_ENTRY + 0x30)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": PAL_VIRTUAL_CODE_ENTRY + 0x30,
        "r28": PAL_VERSION,
        "r8": 0,
        "r9": PAL_VERSION_VALUE,
        "r10": PAL_VERSION_VALUE,
        "r11": 0,
    }, entry=0x10)

test_pal_prefetch_vis = require_registers("pal_prefetch_vis",
    pal_call_program(PAL_PREFETCH_VIS),
    {"ip": 0x30, "r28": PAL_PREFETCH_VIS, "r8": 1,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_prefetch_vis_merced = require_registers(
    "pal_prefetch_vis_merced",
    pal_call_program(PAL_PREFETCH_VIS),
    {"ip": 0x30, "r28": PAL_PREFETCH_VIS, "r8": 1,
     "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_prefetch_vis_physical = require_registers(
    "pal_prefetch_vis_physical",
    pal_call_program(PAL_PREFETCH_VIS, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PREFETCH_VIS, "r8": 1,
     "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_prefetch_vis_merced_physical = require_registers(
    "pal_prefetch_vis_merced_physical",
    pal_call_program(PAL_PREFETCH_VIS, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PREFETCH_VIS,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_prefetch_vis_reserved_arg = require_registers(
    "pal_prefetch_vis_reserved_arg",
    pal_call_program(PAL_PREFETCH_VIS, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PREFETCH_VIS,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_flush = require_registers("pal_cache_flush",
    pal_call_program(PAL_CACHE_FLUSH, [(29, 3), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_FLUSH, "r8": 0, "r9": 0, "r10": 0},
    entry=0x10)

test_pal_cache_flush_coherent_icache = require_registers(
    "pal_cache_flush_coherent_icache",
    pal_call_program(PAL_CACHE_FLUSH, [(29, 4), (30, 3), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_FLUSH, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_cache_flush_bad_type = require_registers(
    "pal_cache_flush_bad_type",
    pal_call_program(PAL_CACHE_FLUSH, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_FLUSH,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_flush_bad_operation = require_registers(
    "pal_cache_flush_bad_operation",
    pal_call_program(PAL_CACHE_FLUSH, [(29, 3), (30, 4), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_FLUSH,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_init = require_registers("pal_cache_init",
    pal_call_program(PAL_CACHE_INIT, [(29, 0), (30, 3), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_INIT, "r8": 0, "r9": 0, "r10": 0,
     "r11": 0}, entry=0x10)

test_pal_cache_init_invalid = require_registers("pal_cache_init_invalid",
    pal_call_program(PAL_CACHE_INIT, [(29, 0), (30, 3), (31, 2)]),
    {"ip": 0x60, "r28": PAL_CACHE_INIT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_prot_info = require_registers("pal_cache_prot_info",
    pal_call_program(PAL_CACHE_PROT_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_PROT_INFO, "r8": 0,
     "r9": PAL_CACHE_PROT_DATA_NONE | (PAL_CACHE_PROT_TAG_NONE_L0 << 32),
     "r10": 0, "r11": 0}, entry=0x10)

test_pal_cache_prot_info_invalid = require_registers(
    "pal_cache_prot_info_invalid",
    pal_call_program(PAL_CACHE_PROT_INFO, [(29, 0), (30, 3), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_PROT_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_cache_prot_info_unified_bad_type = require_registers(
    "pal_cache_prot_info_unified_bad_type",
    pal_call_program(PAL_CACHE_PROT_INFO, [(29, 2), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_PROT_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mem_attrib = require_registers("pal_mem_attrib",
    pal_call_program(PAL_MEM_ATTRIB),
    {"ip": 0x30, "r28": PAL_MEM_ATTRIB, "r8": 0,
     "r9": PAL_MEM_ATTRIB_ITANIUM2, "r10": 0, "r11": 0}, entry=0x10)

test_pal_mem_attrib_madison = require_registers(
    "pal_mem_attrib_madison",
    pal_call_program(PAL_MEM_ATTRIB),
    {"ip": 0x30, "r28": PAL_MEM_ATTRIB, "r8": 0,
     "r9": PAL_MEM_ATTRIB_ITANIUM2, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_mem_attrib_merced = require_registers(
    "pal_mem_attrib_merced",
    pal_call_program(PAL_MEM_ATTRIB),
    {"ip": 0x30, "r28": PAL_MEM_ATTRIB, "r8": 0,
     "r9": MERCED_PAL_MEM_ATTRIB, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_mem_attrib_reserved_arg = require_registers(
    "pal_mem_attrib_reserved_arg",
    pal_call_program(PAL_MEM_ATTRIB, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MEM_ATTRIB,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_bus_get_features = require_registers("pal_bus_get_features",
    pal_call_program(PAL_BUS_GET_FEATURES),
    {"ip": 0x30, "r28": PAL_BUS_GET_FEATURES, "r8": 0,
     "r9": ((1 << 30) | (1 << 29)), "r10": 0,
     "r11": ((1 << 30) | (1 << 29))}, entry=0x10)

test_pal_bus_get_features_merced = require_registers(
    "pal_bus_get_features_merced",
    pal_call_program(PAL_BUS_GET_FEATURES),
    {"ip": 0x30, "r28": PAL_BUS_GET_FEATURES, "r8": 0,
     "r9": ((1 << 30) | (1 << 29)), "r10": 0,
     "r11": ((1 << 30) | (1 << 29))},
    entry=0x10, cpu="merced")

test_pal_bus_get_features_reserved_arg = require_registers(
    "pal_bus_get_features_reserved_arg",
    pal_call_program(PAL_BUS_GET_FEATURES, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_BUS_GET_FEATURES,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_bus_set_features = require_registers("pal_bus_set_features",
    pal_call_program(PAL_BUS_SET_FEATURES, [(29, 0x1234), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_BUS_SET_FEATURES, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_bus_set_features_invalid = require_registers(
    "pal_bus_set_features_invalid",
    pal_call_program(PAL_BUS_SET_FEATURES, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_BUS_SET_FEATURES,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_set_features = require_registers("pal_proc_set_features",
    pal_call_program(PAL_PROC_SET_FEATURES, [(29, 0x55), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_proc_set_features_invalid = require_registers(
    "pal_proc_set_features_invalid",
    pal_call_program(PAL_PROC_SET_FEATURES, [(29, 0), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_set_features_montecito_next_set = require_registers(
    "pal_proc_set_features_montecito_next_set",
    pal_call_program(PAL_PROC_SET_FEATURES,
                     [(29, 0), (30, 16), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": 1, "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_proc_set_features_montecito_set18 = require_registers(
    "pal_proc_set_features_montecito_set18",
    pal_call_program(PAL_PROC_SET_FEATURES,
                     [(29, (1 << 7) | (1 << 18)), (30, 18), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_proc_set_features_montecito_round_trip = require_registers(
    "pal_proc_set_features_montecito_round_trip", [
        (0x10, *movl_mlx(28, PAL_PROC_SET_FEATURES)),
        (0x20, *movl_mlx(29, (1 << 7) | (1 << 18))),
        (0x30, *movl_mlx(30, 18)),
        (0x40, *movl_mlx(31, 0)),
        (0x50, 0x10, nop_m(), nop_i(),
         br_call(0, 0x50, PAL_PROC_ENTRY)),
        (0x60, 0x00, nop_m(), addl(28, PAL_PROC_GET_FEATURES, 0),
         addl(29, 0, 0)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {
        "ip": 0x80,
        "r28": PAL_PROC_GET_FEATURES,
        "r8": 0,
        "r9": (1 << 5) | (1 << 7) | (1 << 18),
        "r10": (1 << 7) | (1 << 18),
        "r11": (1 << 5) | (1 << 7),
    }, entry=0x10)

test_pal_proc_set_features_montecito_beyond_max = require_registers(
    "pal_proc_set_features_montecito_beyond_max",
    pal_call_program(PAL_PROC_SET_FEATURES,
                     [(29, 0), (30, 19), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": (-8 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_set_features_madison_beyond_max = require_registers(
    "pal_proc_set_features_madison_beyond_max",
    pal_call_program(PAL_PROC_SET_FEATURES,
                     [(29, 0), (30, 17), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": (-8 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_proc_set_features_madison_set16 = require_registers(
    "pal_proc_set_features_madison_set16",
    pal_call_program(PAL_PROC_SET_FEATURES,
                     [(29, 0xffffffffffffffff), (30, 16), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_SET_FEATURES,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_proc_get_features = require_registers("pal_proc_get_features",
    pal_call_program(PAL_PROC_GET_FEATURES),
    {"ip": 0x30, "r28": PAL_PROC_GET_FEATURES, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_proc_get_features_reserved_arg = require_registers(
    "pal_proc_get_features_reserved_arg",
    pal_call_program(PAL_PROC_GET_FEATURES, [(29, 0), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_get_features_montecito_next_set = require_registers(
    "pal_proc_get_features_montecito_next_set",
    pal_call_program(PAL_PROC_GET_FEATURES, [(29, 0), (30, 16), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": 1, "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_proc_get_features_montecito_set18 = require_registers(
    "pal_proc_get_features_montecito_set18",
    pal_call_program(PAL_PROC_GET_FEATURES, [(29, 0), (30, 18), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": 0,
     "r9": (1 << 5) | (1 << 7) | (1 << 18),
     "r10": (1 << 5) | (1 << 7) | (1 << 18),
     "r11": (1 << 5) | (1 << 7)}, entry=0x10)

test_pal_proc_get_features_montecito_9010_set18 = require_registers(
    "pal_proc_get_features_montecito_9010_set18",
    pal_call_program(PAL_PROC_GET_FEATURES,
                     [(29, 0), (30, 18), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": 0,
     "r9": (1 << 5) | (1 << 7),
     "r10": (1 << 5) | (1 << 7),
     "r11": (1 << 5) | (1 << 7)}, entry=0x10,
    cpu="montecito-9010")

test_pal_proc_get_features_montecito_beyond_max = require_registers(
    "pal_proc_get_features_montecito_beyond_max",
    pal_call_program(PAL_PROC_GET_FEATURES, [(29, 0), (30, 19), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": (-8 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_proc_get_features_madison_beyond_max = require_registers(
    "pal_proc_get_features_madison_beyond_max",
    pal_call_program(PAL_PROC_GET_FEATURES, [(29, 0), (30, 17), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": (-8 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_proc_get_features_madison_set16 = require_registers(
    "pal_proc_get_features_madison_set16",
    pal_call_program(PAL_PROC_GET_FEATURES,
                     [(29, 0), (30, 16), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PROC_GET_FEATURES,
     "r8": 0, "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_logical_to_physical_current = require_registers(
    "pal_logical_to_physical_current",
    pal_call_program(PAL_LOGICAL_TO_PHYSICAL,
                     [(29, 0xffffffffffffffff), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_LOGICAL_TO_PHYSICAL, "r8": 0,
     "r9": 1 | (1 << 16) | (1 << 32), "r10": 0, "r11": 0},
    entry=0x10)

test_pal_logical_to_physical_multicore_thread = require_registers(
    "pal_logical_to_physical_multicore_thread",
    pal_call_program(PAL_LOGICAL_TO_PHYSICAL,
                     [(29, 3), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_LOGICAL_TO_PHYSICAL, "r8": 0,
     "r9": 4 | (2 << 16) | (2 << 32),
     "r10": 1 | (1 << 32), "r11": 3},
    entry=0x10, alat=None, smp="4,sockets=1,cores=2,threads=2")

test_pal_logical_to_physical_madison_unimplemented = require_registers(
    "pal_logical_to_physical_madison_unimplemented",
    pal_call_program(PAL_LOGICAL_TO_PHYSICAL,
                     [(29, 0xffffffffffffffff), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_LOGICAL_TO_PHYSICAL,
     "r8": (-1 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_logical_to_physical_merced_unimplemented = require_registers(
    "pal_logical_to_physical_merced_unimplemented",
    pal_call_program(PAL_LOGICAL_TO_PHYSICAL,
                     [(29, 0xffffffffffffffff), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_LOGICAL_TO_PHYSICAL,
     "r8": (-1 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_shared_info_single_thread = require_registers(
    "pal_cache_shared_info_single_thread",
    pal_call_program(PAL_CACHE_SHARED_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_SHARED_INFO, "r8": 0,
     "r9": 1, "r10": 0, "r11": 0}, entry=0x10)

test_pal_cache_shared_info_sibling_thread = require_registers(
    "pal_cache_shared_info_sibling_thread",
    pal_call_program(PAL_CACHE_SHARED_INFO, [(29, 2), (30, 2), (31, 1)]),
    {"ip": 0x60, "r28": PAL_CACHE_SHARED_INFO, "r8": 0,
     "r9": 2, "r10": 1, "r11": 1},
    entry=0x10, alat=None, smp="4,sockets=1,cores=2,threads=2")

test_pal_cache_shared_info_merced_unimplemented = require_registers(
    "pal_cache_shared_info_merced_unimplemented",
    pal_call_program(PAL_CACHE_SHARED_INFO,
                     [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_SHARED_INFO,
     "r8": (-1 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_brand_info_string = require_registers(
    "pal_brand_info_string", [
        (0x10, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
        (0x20, *movl_mlx(28, PAL_BRAND_INFO)),
        (0x30, *movl_mlx(32, PAL_BRAND_INFO)),
        (0x40, *movl_mlx(33, 0)),
        (0x50, *movl_mlx(34, PAL_BRAND_BUFFER)),
        (0x60, *movl_mlx(35, 0)),
        (0x70, 0x10, nop_m(), nop_i(), br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, *movl_mlx(2, PAL_BRAND_BUFFER)),
        (0x90, 0x00, ld8(20, 2), nop_i(), nop_i()),
        (0xa0, 0x10, nop_m(), nop_i(), br_cond(0xa0, 0xa0)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {"ip": 0xa0, "r28": PAL_BRAND_INFO, "r8": 0,
        "r9": len("QEMU Montecito-compatible IA-64 CPU 1.60GHz 24MB"),
        "r10": 0, "r11": 0,
        "r20": int.from_bytes(b"QEMU Mon", "little")}, entry=0x10)

test_pal_brand_info_string_madison_zx6000 = require_registers(
    "pal_brand_info_string_madison_zx6000", [
        (0x10, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
        (0x20, *movl_mlx(28, PAL_BRAND_INFO)),
        (0x30, *movl_mlx(32, PAL_BRAND_INFO)),
        (0x40, *movl_mlx(33, 0)),
        (0x50, *movl_mlx(34, PAL_BRAND_BUFFER)),
        (0x60, *movl_mlx(35, 0)),
        (0x70, 0x10, nop_m(), nop_i(), br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, *movl_mlx(2, PAL_BRAND_BUFFER)),
        (0x90, 0x00, ld8(20, 2), adds(2, 40, 2), nop_i()),
        (0xa0, 0x00, ld8(21, 2), adds(2, 8, 2), nop_i()),
        (0xb0, 0x00, ld8(22, 2), nop_i(), nop_i()),
        (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ], {"ip": 0xc0, "r28": PAL_BRAND_INFO, "r8": 0,
        "r9": len("QEMU Madison zx6000-compatible IA-64 CPU 1.50GHz 6MB"),
        "r10": 0, "r11": 0,
        "r20": int.from_bytes(b"QEMU Mad", "little"),
        "r21": int.from_bytes(b" 1.50GHz", "little"),
        "r22": int.from_bytes(b" 6MB\0\0\0\0", "little")},
    entry=0x10, cpu="madison-zx6000")

test_pal_brand_info_frequency = require_registers(
    "pal_brand_info_frequency",
    pal_stacked_call_program(PAL_BRAND_INFO, [16, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 1600000000, "r10": 0, "r11": 0}, entry=0x10)

test_pal_brand_info_frequency_madison_unavailable = require_registers(
    "pal_brand_info_frequency_madison_unavailable",
    pal_stacked_call_program(PAL_BRAND_INFO, [16, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO,
     "r8": (-6 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_brand_info_cache = require_registers(
    "pal_brand_info_cache",
    pal_stacked_call_program(PAL_BRAND_INFO, [17, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 24 * 1024 * 1024, "r10": 0, "r11": 0}, entry=0x10)

test_pal_brand_info_cache_madison = require_registers(
    "pal_brand_info_cache_madison",
    pal_stacked_call_program(PAL_BRAND_INFO, [17, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 3 * 1024 * 1024, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_brand_info_cache_madison_zx6000 = require_registers(
    "pal_brand_info_cache_madison_zx6000",
    pal_stacked_call_program(PAL_BRAND_INFO, [17, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 6 * 1024 * 1024, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison-zx6000")

test_pal_brand_info_cache_montecito_9010 = require_registers(
    "pal_brand_info_cache_montecito_9010",
    pal_stacked_call_program(PAL_BRAND_INFO, [17, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 6 * 1024 * 1024, "r10": 0, "r11": 0},
    entry=0x10, cpu="montecito-9010")

test_pal_brand_info_cache_montecito_9040 = require_registers(
    "pal_brand_info_cache_montecito_9040",
    pal_stacked_call_program(PAL_BRAND_INFO, [17, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 18 * 1024 * 1024, "r10": 0, "r11": 0},
    entry=0x10, cpu="montecito-9040")

test_pal_brand_info_bus = require_registers(
    "pal_brand_info_bus",
    pal_stacked_call_program(PAL_BRAND_INFO, [18, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO, "r8": 0,
     "r9": 533333333, "r10": 0, "r11": 0}, entry=0x10)

test_pal_brand_info_bus_madison_unavailable = require_registers(
    "pal_brand_info_bus_madison_unavailable",
    pal_stacked_call_program(PAL_BRAND_INFO, [18, 0, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO,
     "r8": (-6 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="madison")

test_pal_brand_info_merced_unimplemented = require_registers(
    "pal_brand_info_merced_unimplemented",
    pal_stacked_call_program(PAL_BRAND_INFO, [0, PAL_BRAND_BUFFER, 0]),
    {"ip": 0x80, "r28": PAL_BRAND_INFO,
     "r8": (-1 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_debug_info = require_registers("pal_debug_info",
    pal_call_program(PAL_DEBUG_INFO),
    {"ip": 0x30, "r28": PAL_DEBUG_INFO, "r8": 0,
     "r9": IA64_IBR_IMPLEMENTED_COUNT // 2,
     "r10": IA64_DBR_IMPLEMENTED_COUNT // 2, "r11": 0}, entry=0x10)

test_pal_debug_info_reserved_arg = require_registers(
    "pal_debug_info_reserved_arg",
    pal_call_program(PAL_DEBUG_INFO, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_DEBUG_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_register_info_application_implemented = require_registers(
    "pal_register_info_application_implemented",
    pal_call_program(PAL_REGISTER_INFO, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO, "r8": 0,
     "r9": PAL_AR_IMPLEMENTED_LOW, "r10": PAL_AR_IMPLEMENTED_HIGH,
     "r11": 0}, entry=0x10)

test_pal_register_info_application_side_effects = require_registers(
    "pal_register_info_application_side_effects",
    pal_call_program(PAL_REGISTER_INFO, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_register_info_control_implemented = require_registers(
    "pal_register_info_control_implemented",
    pal_call_program(PAL_REGISTER_INFO, [(29, 2), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO, "r8": 0,
     "r9": PAL_CR_IMPLEMENTED_LOW, "r10": PAL_CR_IMPLEMENTED_HIGH,
     "r11": 0}, entry=0x10)

test_pal_register_info_control_side_effects = require_registers(
    "pal_register_info_control_side_effects",
    pal_call_program(PAL_REGISTER_INFO, [(29, 3), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO, "r8": 0,
     "r9": 0, "r10": PAL_CR_READ_SIDE_EFFECT_HIGH, "r11": 0},
    entry=0x10)

test_pal_register_info_invalid_request = require_registers(
    "pal_register_info_invalid_request",
    pal_call_program(PAL_REGISTER_INFO, [(29, 4), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_register_info_reserved_arg = require_registers(
    "pal_register_info_reserved_arg",
    pal_call_program(PAL_REGISTER_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_REGISTER_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

pal_perf_mon_info_program = [
    (0x10, *movl_mlx(29, PAL_PERF_BUFFER)),
    (0x20, 0x00, nop_m(), addl(30, 0, 0), addl(31, 0, 0)),
    (0x30, 0x00, nop_m(), addl(28, PAL_PERF_MON_INFO, 0), nop_i()),
    (0x40, 0x10, nop_m(), nop_i(), br_call(0, 0x40, PAL_PROC_ENTRY)),
    (0x50, *movl_mlx(2, PAL_PERF_BUFFER)),
    (0x60, 0x00, ld8(20, 2), adds(2, 8, 2), nop_i()),
    (0x70, 0x00, ld8(21, 2), adds(2, 0x18, 2), nop_i()),
    (0x80, 0x00, ld8(22, 2), adds(2, 8, 2), nop_i()),
    (0x90, 0x00, ld8(23, 2), adds(2, 0x18, 2), nop_i()),
    (0xa0, 0x00, ld8(24, 2), adds(2, 0x20, 2), nop_i()),
    (0xb0, 0x00, ld8(25, 2), nop_i(), nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
    (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
]

test_pal_perf_mon_info = require_registers(
    "pal_perf_mon_info", pal_perf_mon_info_program,
    {"ip": 0xc0, "r28": PAL_PERF_MON_INFO, "r8": 0,
    "r9": 0x08123004, "r10": 0, "r11": 0,
    "r20": 0x3fff, "r21": 0, "r22": 0x3ffff, "r23": 0,
    "r24": 0xf0, "r25": 0xf0},
    entry=0x10)

test_pal_perf_mon_info_merced = require_registers(
    "pal_perf_mon_info_merced", pal_perf_mon_info_program,
    {"ip": 0xc0, "r28": PAL_PERF_MON_INFO, "r8": 0,
     "r9": MERCED_PAL_PERF_MON_INFO_VALUE, "r10": 0, "r11": 0,
     "r20": 0x3fff, "r21": 0, "r22": 0x3ffff, "r23": 0,
     "r24": 0xf0, "r25": 0x30},
    entry=0x10, cpu="merced")

test_pal_perf_mon_info_bad_buffer = require_registers(
    "pal_perf_mon_info_bad_buffer",
    pal_call_program(PAL_PERF_MON_INFO,
                     [(29, PAL_PERF_BUFFER + 4), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PERF_MON_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_perf_mon_info_reserved_arg = require_registers(
    "pal_perf_mon_info_reserved_arg",
    pal_call_program(PAL_PERF_MON_INFO,
                     [(29, PAL_PERF_BUFFER), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PERF_MON_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_fixed_addr = require_registers("pal_fixed_addr",
    pal_call_program(PAL_FIXED_ADDR),
    {"ip": 0x30, "r28": PAL_FIXED_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_fixed_addr_reserved_arg = require_registers(
    "pal_fixed_addr_reserved_arg",
    pal_call_program(PAL_FIXED_ADDR, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_FIXED_ADDR,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_platform_addr_interrupt = require_registers(
    "pal_platform_addr_interrupt",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_INTERRUPT_BLOCK),
                      (30, PAL_INTERRUPT_BLOCK_DEFAULT), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_platform_addr_ignores_bit63 = require_registers(
    "pal_platform_addr_ignores_bit63",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_INTERRUPT_BLOCK),
                      (30, PAL_INTERRUPT_BLOCK_DEFAULT | (1 << 63)),
                      (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_platform_addr_io = require_registers(
    "pal_platform_addr_io",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_IO_BLOCK),
                      (30, PAL_IO_BLOCK_DEFAULT), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_platform_addr_bad_type = require_registers(
    "pal_platform_addr_bad_type",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, 2), (30, PAL_INTERRUPT_BLOCK_DEFAULT), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_platform_addr_alternate_interrupt = require_registers(
    "pal_platform_addr_alternate_interrupt",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_INTERRUPT_BLOCK),
                      (30, 0x200000), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_platform_addr_unimplemented = require_registers(
    "pal_platform_addr_unimplemented",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_IO_BLOCK),
                      (30, 1 << 50), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_platform_addr_misaligned = require_registers(
    "pal_platform_addr_misaligned",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_IO_BLOCK),
                      (30, PAL_IO_BLOCK_DEFAULT + 0x1000), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_platform_addr_merced_default = require_registers(
    "pal_platform_addr_merced_default",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_IO_BLOCK),
                      (30, MERCED_PAL_IO_BLOCK_DEFAULT), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_platform_addr_merced_unimplemented = require_registers(
    "pal_platform_addr_merced_unimplemented",
    pal_call_program(PAL_PLATFORM_ADDR,
                     [(29, PAL_PLATFORM_IO_BLOCK),
                      (30, 1 << 44), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PLATFORM_ADDR,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_mc_clear_log = require_registers("pal_mc_clear_log",
    pal_call_program(PAL_MC_CLEAR_LOG, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_CLEAR_LOG, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_copy_info = require_registers("pal_copy_info",
    pal_call_program(PAL_COPY_INFO, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_COPY_INFO, "r8": 0,
     "r9": PAL_COPY_BUFFER_SIZE, "r10": PAL_COPY_BUFFER_ALIGN, "r11": 0},
    entry=0x10)

test_pal_copy_info_bad_type = require_registers("pal_copy_info_bad_type",
    pal_call_program(PAL_COPY_INFO, [(29, 2), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_COPY_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_info_ia32_unsupported = require_registers(
    "pal_copy_info_ia32_unsupported",
    pal_call_program(PAL_COPY_INFO, [(29, 1), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_COPY_INFO,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_info_platform_for_ia64 = require_registers(
    "pal_copy_info_platform_for_ia64",
    pal_call_program(PAL_COPY_INFO, [(29, 0), (30, 1), (31, 0)]),
    {"ip": 0x60, "r28": PAL_COPY_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_pal_entry_callable = require_registers(
    "pal_copy_pal_entry_callable", [
        (0x10, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
        (0x20, *movl_mlx(28, PAL_COPY_PAL)),
        (0x30, *movl_mlx(32, PAL_COPY_PAL)),
        (0x40, *movl_mlx(33, PAL_COPY_TARGET | (1 << 63))),
        (0x50, *movl_mlx(34, PAL_COPY_BUFFER_SIZE)),
        (0x60, *movl_mlx(35, 0)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, *movl_mlx(28, PAL_VERSION)),
        (0x90, 0x00, nop_m(), addl(29, 0, 0), addl(30, 0, 0)),
        (0xa0, 0x10, nop_m(), addl(31, 0, 0),
         br_call(0, 0xa0, PAL_COPY_TARGET)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ],
    {"ip": 0xb0, "r28": PAL_VERSION, "r8": 0,
     "r9": PAL_VERSION_VALUE, "r10": PAL_VERSION_VALUE, "r11": 0},
    entry=0x10)

test_pal_copy_pal_ap_entry_callable = require_registers(
    "pal_copy_pal_ap_entry_callable", [
        (0x10, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
        (0x20, *movl_mlx(28, PAL_COPY_PAL)),
        (0x30, *movl_mlx(32, PAL_COPY_PAL)),
        (0x40, *movl_mlx(33, PAL_COPY_TARGET | (1 << 63))),
        (0x50, *movl_mlx(34, PAL_COPY_BUFFER_SIZE)),
        (0x60, *movl_mlx(35, 1)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, *movl_mlx(28, PAL_VERSION)),
        (0x90, 0x00, nop_m(), addl(29, 0, 0), addl(30, 0, 0)),
        (0xa0, 0x10, nop_m(), addl(31, 0, 0),
         br_call(0, 0xa0, PAL_COPY_TARGET)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
        (PAL_COPY_TARGET, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_COPY_TARGET + 0x10, 0x10, nop_m(), nop_i(),
         br_ret(0)),
    ],
    {"ip": 0xb0, "r28": PAL_VERSION, "r8": 0,
     "r9": PAL_VERSION_VALUE, "r10": PAL_VERSION_VALUE, "r11": 0},
    entry=0x10)

test_pal_copy_pal_bad_alloc = require_registers("pal_copy_pal_bad_alloc",
    pal_stacked_call_program(PAL_COPY_PAL,
                             [PAL_COPY_TARGET, PAL_COPY_BUFFER_SIZE - 1, 0]),
    {"ip": 0x80, "r28": PAL_COPY_PAL,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_pal_bad_alignment = require_registers(
    "pal_copy_pal_bad_alignment",
    pal_stacked_call_program(PAL_COPY_PAL,
                             [PAL_COPY_TARGET + 0x20,
                              PAL_COPY_BUFFER_SIZE, 0]),
    {"ip": 0x80, "r28": PAL_COPY_PAL,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_pal_bad_processor = require_registers(
    "pal_copy_pal_bad_processor",
    pal_stacked_call_program(PAL_COPY_PAL,
                             [PAL_COPY_TARGET, PAL_COPY_BUFFER_SIZE, 2]),
    {"ip": 0x80, "r28": PAL_COPY_PAL,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_copy_pal_unimplemented_pa_merced = require_registers(
    "pal_copy_pal_unimplemented_pa_merced",
    pal_stacked_call_program(
        PAL_COPY_PAL,
        [(1 << MERCED_IMPL_PA_BITS) - PAL_COPY_BUFFER_SIZE,
         PAL_COPY_BUFFER_SIZE * 2, 0]),
    {"ip": 0x80, "r28": PAL_COPY_PAL,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_halt_info = require_registers("pal_halt_info", [
    (0x10, 0x00, nop_m(), alloc(2, 4, 0, 0, 0), nop_i()),
    (0x20, *movl_mlx(28, PAL_HALT_INFO)),
    (0x30, *movl_mlx(32, PAL_HALT_INFO)),
    (0x40, *movl_mlx(33, PAL_HALT_INFO_BUFFER)),
    (0x50, *movl_mlx(34, 0)),
    (0x60, *movl_mlx(35, 0)),
    (0x70, 0x10, nop_m(), nop_i(), br_call(0, 0x70, PAL_PROC_ENTRY)),
    (0x80, *movl_mlx(2, PAL_HALT_INFO_BUFFER)),
    (0x90, 0x00, ld8(20, 2), adds(2, 8, 2), nop_i()),
    (0xa0, 0x00, ld8(21, 2), adds(2, 0x30, 2), nop_i()),
    (0xb0, 0x00, ld8(22, 2), nop_i(), nop_i()),
    (0xc0, 0x10, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
    (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
    (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
], {"ip": 0xc0, "r28": PAL_HALT_INFO, "r8": 0,
    "r9": 0, "r10": 0, "r11": 0,
    "r20": PAL_HALT_LIGHT_INFO, "r21": PAL_HALT_STATE1_INFO, "r22": 0},
    entry=0x10)

test_pal_halt_invalid_state = require_registers("pal_halt_invalid_state",
    pal_call_program(PAL_HALT, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_HALT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_halt_reserved_arg = require_registers("pal_halt_reserved_arg",
    pal_call_program(PAL_HALT, [(29, 1), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_HALT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_halt_unimplemented_detail_pa_merced = require_registers(
    "pal_halt_unimplemented_detail_pa_merced",
    pal_call_program(PAL_HALT,
                     [(29, 1), (30, 1 << MERCED_IMPL_PA_BITS), (31, 0)]),
    {"ip": 0x60, "r28": PAL_HALT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0,
     "halted": 0},
    entry=0x10, cpu="merced")

test_pal_halt_io_reserved_info = require_registers(
    "pal_halt_io_reserved_info", [
        (0x10, *movl_mlx(2, 0x5000)),
        (0x20, *movl_mlx(3, 1 << 16)),
        (0x30, 0x00, st8(2, 3), addl(29, 1, 0), nop_i()),
        (0x40, *movl_mlx(30, 0x5000)),
        (0x50, *movl_mlx(31, 0)),
        (0x60, *movl_mlx(28, PAL_HALT)),
        (0x70, 0x10, nop_m(), nop_i(),
         br_call(0, 0x70, PAL_PROC_ENTRY)),
        (0x80, 0x10, nop_m(), nop_i(),
         br_cond(0x80, 0x80)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ],
    {"ip": 0x80, "r28": PAL_HALT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0,
     "halted": 0},
    entry=0x10)

test_pal_halt_io_unimplemented_pa_merced = require_registers(
    "pal_halt_io_unimplemented_pa_merced", [
        (0x10, *movl_mlx(2, 0x5000)),
        (0x20, *movl_mlx(3, 1 | (8 << 8))),
        (0x30, 0x00, st8(2, 3), nop_i(), nop_i()),
        (0x40, *movl_mlx(2, 0x5008)),
        (0x50, *movl_mlx(3, 1 << MERCED_IMPL_PA_BITS)),
        (0x60, 0x00, st8(2, 3), addl(29, 1, 0), nop_i()),
        (0x70, *movl_mlx(30, 0x5000)),
        (0x80, *movl_mlx(31, 0)),
        (0x90, *movl_mlx(28, PAL_HALT)),
        (0xa0, 0x10, nop_m(), nop_i(),
         br_call(0, 0xa0, PAL_PROC_ENTRY)),
        (0xb0, 0x10, nop_m(), nop_i(),
         br_cond(0xb0, 0xb0)),
        (PAL_PROC_ENTRY, 0x0a, pal_break(), nop_m(), nop_i()),
        (PAL_PROC_ENTRY + 0x10, 0x10, nop_m(), nop_i(), br_ret(0)),
    ],
    {"ip": 0xb0, "r28": PAL_HALT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0,
     "halted": 0},
    entry=0x10, cpu="merced")

test_pal_halt_info_bad_buffer = require_registers(
    "pal_halt_info_bad_buffer",
    pal_stacked_call_program(PAL_HALT_INFO,
                             [PAL_HALT_INFO_BUFFER + 4, 0, 0]),
    {"ip": 0x80, "r28": PAL_HALT_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_halt_info_reserved_arg = require_registers(
    "pal_halt_info_reserved_arg",
    pal_stacked_call_program(PAL_HALT_INFO, [PAL_HALT_INFO_BUFFER, 1, 0]),
    {"ip": 0x80, "r28": PAL_HALT_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_drain = require_registers("pal_mc_drain",
    pal_call_program(PAL_MC_DRAIN),
    {"ip": 0x30, "r28": PAL_MC_DRAIN, "r8": 0, "r9": 0, "r10": 0},
    entry=0x10)

test_pal_mc_drain_reserved_arg = require_registers(
    "pal_mc_drain_reserved_arg",
    pal_call_program(PAL_MC_DRAIN, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_DRAIN,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_expected = require_registers("pal_mc_expected",
    pal_call_program(PAL_MC_EXPECTED, [(29, 1), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_EXPECTED, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_mc_dynamic_state_empty = require_registers(
    "pal_mc_dynamic_state_empty",
    pal_call_program(PAL_MC_DYNAMIC_STATE, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_DYNAMIC_STATE, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_mc_dynamic_state_empty_aligned_offset = require_registers(
    "pal_mc_dynamic_state_empty_aligned_offset",
    pal_call_program(PAL_MC_DYNAMIC_STATE, [(29, 8), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_DYNAMIC_STATE, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_mc_dynamic_state_bad_offset = require_registers(
    "pal_mc_dynamic_state_bad_offset",
    pal_call_program(PAL_MC_DYNAMIC_STATE, [(29, 4), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_DYNAMIC_STATE,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_dynamic_state_reserved_arg = require_registers(
    "pal_mc_dynamic_state_reserved_arg",
    pal_call_program(PAL_MC_DYNAMIC_STATE, [(29, 0), (30, 0), (31, 1)]),
    {"ip": 0x60, "r28": PAL_MC_DYNAMIC_STATE,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_error_info_map_empty = require_registers(
    "pal_mc_error_info_map_empty",
    pal_call_program(PAL_MC_ERROR_INFO, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_ERROR_INFO,
     "r8": (-6 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_error_info_structure_empty = require_registers(
    "pal_mc_error_info_structure_empty",
    pal_call_program(PAL_MC_ERROR_INFO, [(29, 2), (30, 1 << 8), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_ERROR_INFO,
     "r8": (-6 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_error_info_bad_index = require_registers(
    "pal_mc_error_info_bad_index",
    pal_call_program(PAL_MC_ERROR_INFO, [(29, 3), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_ERROR_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_error_info_bad_level = require_registers(
    "pal_mc_error_info_bad_level",
    pal_call_program(PAL_MC_ERROR_INFO, [(29, 2), (30, 0x300), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_ERROR_INFO,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_resume_no_context = require_registers(
    "pal_mc_resume_no_context",
    pal_call_program(PAL_MC_RESUME,
                     [(29, 0), (30, IA64_PHYS_UC_BIT | 0x2000), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_resume_new_context_no_context = require_registers(
    "pal_mc_resume_new_context_no_context",
    pal_call_program(PAL_MC_RESUME,
                     [(29, 1), (30, IA64_PHYS_UC_BIT | 0x2000), (31, 1)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-3 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_resume_bad_args = require_registers(
    "pal_mc_resume_bad_args",
    pal_call_program(PAL_MC_RESUME,
                     [(29, 2), (30, IA64_PHYS_UC_BIT | 0x2000), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_resume_bad_save_ptr = require_registers(
    "pal_mc_resume_bad_save_ptr",
    pal_call_program(PAL_MC_RESUME,
                     [(29, 0), (30, IA64_PHYS_UC_BIT | 0x2100), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_resume_cacheable_save_ptr = require_registers(
    "pal_mc_resume_cacheable_save_ptr",
    pal_call_program(PAL_MC_RESUME, [(29, 0), (30, 0x2000), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_register_mem = require_registers("pal_mc_register_mem",
    pal_call_program(PAL_MC_REGISTER_MEM,
                     [(29, IA64_PHYS_UC_BIT | 0x2000),
                      (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_REGISTER_MEM, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_mc_register_mem_cacheable_rejected = require_registers(
    "pal_mc_register_mem_cacheable_rejected",
    pal_call_program(PAL_MC_REGISTER_MEM,
                     [(29, 0x2000), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_REGISTER_MEM,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_mc_register_mem_crosses_pa_limit_merced = require_registers(
    "pal_mc_register_mem_crosses_pa_limit_merced",
    pal_call_program(PAL_MC_REGISTER_MEM,
                     [(29, IA64_PHYS_UC_BIT |
                           ((1 << MERCED_IMPL_PA_BITS) - 0x800)),
                      (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_REGISTER_MEM,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_mc_resume_crosses_pa_limit_merced = require_registers(
    "pal_mc_resume_crosses_pa_limit_merced",
    pal_call_program(PAL_MC_RESUME,
                     [(29, 0),
                      (30, IA64_PHYS_UC_BIT |
                           ((1 << MERCED_IMPL_PA_BITS) - 0x800)),
                      (31, 0)]),
    {"ip": 0x60, "r28": PAL_MC_RESUME,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_cache_line_init = require_registers("pal_cache_line_init",
    pal_call_program(PAL_CACHE_LINE_INIT,
                     [(29, 0x4000), (30, 0x1234), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_LINE_INIT, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_cache_line_init_unimplemented_pa_merced = require_registers(
    "pal_cache_line_init_unimplemented_pa_merced",
    pal_call_program(PAL_CACHE_LINE_INIT,
                     [(29, 1 << MERCED_IMPL_PA_BITS),
                      (30, 0x1234), (31, 0)]),
    {"ip": 0x60, "r28": PAL_CACHE_LINE_INIT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_pmi_entrypoint = require_registers("pal_pmi_entrypoint",
    pal_call_program(PAL_PMI_ENTRYPOINT, [(29, 0x5000), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PMI_ENTRYPOINT, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0}, entry=0x10)

test_pal_pmi_entrypoint_uncacheable = require_registers(
    "pal_pmi_entrypoint_uncacheable",
    pal_call_program(PAL_PMI_ENTRYPOINT,
                     [(29, IA64_PHYS_UC_BIT | 0x5000),
                      (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PMI_ENTRYPOINT, "r8": 0,
     "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_pmi_entrypoint_unimplemented_pa_merced = require_registers(
    "pal_pmi_entrypoint_unimplemented_pa_merced",
    pal_call_program(PAL_PMI_ENTRYPOINT,
                     [(29, 1 << MERCED_IMPL_PA_BITS),
                      (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_PMI_ENTRYPOINT,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_mem_for_test = require_registers("pal_mem_for_test",
    pal_call_program(PAL_MEM_FOR_TEST, [(29, 0), (30, 0), (31, 0)]),
    {"ip": 0x60, "r28": PAL_MEM_FOR_TEST, "r8": 0,
     "r9": 0, "r10": 1, "r11": 0}, entry=0x10)

test_pal_test_proc_healthy = require_registers("pal_test_proc_healthy",
    pal_stacked_call_program(PAL_TEST_PROC, [0x2000, 0, 1]),
    {"ip": 0x80, "r28": PAL_TEST_PROC, "r8": 0,
     "r9": PAL_SELF_TEST_STATE_TESTED, "r10": 0, "r11": 0}, entry=0x10)

test_pal_test_proc_missing_cacheable_attr = require_registers(
    "pal_test_proc_missing_cacheable_attr",
    pal_stacked_call_program(PAL_TEST_PROC, [0x2000, 0, 0]),
    {"ip": 0x80, "r28": PAL_TEST_PROC,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_test_proc_bad_address = require_registers(
    "pal_test_proc_bad_address",
    pal_stacked_call_program(PAL_TEST_PROC, [1 << 63, 0, 1]),
    {"ip": 0x80, "r28": PAL_TEST_PROC,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_test_proc_bad_attributes = require_registers(
    "pal_test_proc_bad_attributes",
    pal_stacked_call_program(PAL_TEST_PROC, [0x2000, 0, 1 << 16]),
    {"ip": 0x80, "r28": PAL_TEST_PROC,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10)

test_pal_test_proc_crosses_pa_limit_merced = require_registers(
    "pal_test_proc_crosses_pa_limit_merced",
    pal_stacked_call_program(
        PAL_TEST_PROC,
        [(1 << MERCED_IMPL_PA_BITS) - 0x100, 0x200, 1]),
    {"ip": 0x80, "r28": PAL_TEST_PROC,
     "r8": (-2 & 0xffffffffffffffff), "r9": 0, "r10": 0, "r11": 0},
    entry=0x10, cpu="merced")

test_pal_unknown = require_registers("pal_unknown",
    pal_call_program(0xffff),
    {"ip": 0x30, "r28": 0xffff, "r8": (-1 & 0xffffffffffffffff)},
    entry=0x10)

GROUP = 'pal'
CASE_NAMES = (

    'cpu_profile_identity_table',
    'pal_bus_get_features',
    'pal_bus_get_features_merced',
    'pal_bus_get_features_reserved_arg',
    'pal_bus_set_features',
    'pal_bus_set_features_invalid',
    'pal_brand_info_bus',
    'pal_brand_info_bus_madison_unavailable',
    'pal_brand_info_cache',
    'pal_brand_info_cache_madison',
    'pal_brand_info_cache_madison_zx6000',
    'pal_brand_info_cache_montecito_9010',
    'pal_brand_info_cache_montecito_9040',
    'pal_brand_info_frequency',
    'pal_brand_info_frequency_madison_unavailable',
    'pal_brand_info_merced_unimplemented',
    'pal_brand_info_string',
    'pal_brand_info_string_madison_zx6000',
    'pal_cache_flush',
    'pal_cache_flush_bad_operation',
    'pal_cache_flush_bad_type',
    'pal_cache_flush_coherent_icache',
    'pal_cache_flush_invalidates_translated_target',
    'pal_cache_flush_keeps_unrelated_tb_cache',
    'pal_cache_info',
    'pal_cache_info_invalid',
    'pal_cache_info_l0_data',
    'pal_cache_info_l1_data',
    'pal_cache_info_l1_instruction',
    'pal_cache_info_l2_unified',
    'pal_cache_info_l2_unified_bad_type',
    'pal_cache_info_l2_unified_madison_9m',
    'pal_cache_info_l2_unified_madison',
    'pal_cache_info_l2_unified_madison_zx6000',
    'pal_cache_info_l2_unified_mckinley',
    'pal_cache_info_l2_unified_montecito_9010',
    'pal_cache_info_l2_unified_montecito_9040',
    'pal_cache_info_merced_l0_data',
    'pal_cache_info_merced_l0_instruction',
    'pal_cache_info_merced_l1_instruction_invalid',
    'pal_cache_info_merced_l1_unified',
    'pal_cache_info_merced_l2_unified',
    'pal_cache_init',
    'pal_cache_init_invalid',
    'pal_cache_line_init',
    'pal_cache_line_init_unimplemented_pa_merced',
    'pal_cache_prot_info',
    'pal_cache_prot_info_invalid',
    'pal_cache_prot_info_unified_bad_type',
    'pal_cache_summary',
    'pal_cache_summary_madison',
    'pal_cache_summary_merced',
    'pal_cache_summary_reserved_arg',
    'pal_cache_shared_info_single_thread',
    'pal_cache_shared_info_sibling_thread',
    'pal_cache_shared_info_merced_unimplemented',
    'pal_copy_info',
    'pal_copy_info_bad_type',
    'pal_copy_info_ia32_unsupported',
    'pal_copy_info_platform_for_ia64',
    'pal_copy_pal_bad_alignment',
    'pal_copy_pal_bad_alloc',
    'pal_copy_pal_bad_processor',
    'pal_copy_pal_ap_entry_callable',
    'pal_copy_pal_entry_callable',
    'pal_copy_pal_unimplemented_pa_merced',
    'pal_debug_info',
    'pal_debug_info_reserved_arg',
    'pal_fixed_addr',
    'pal_fixed_addr_reserved_arg',
    'pal_freq_base',
    'pal_freq_base_madison_zx6000',
    'pal_freq_base_reserved_arg',
    'pal_freq_ratios',
    'pal_freq_ratios_madison',
    'pal_freq_ratios_madison_9m',
    'pal_freq_ratios_madison_zx6000',
    'pal_freq_ratios_merced',
    'pal_freq_ratios_montecito_9010',
    'pal_freq_ratios_montecito_9015',
    'pal_freq_ratios_montecito_9020',
    'pal_freq_ratios_montecito_9030',
    'pal_freq_ratios_montecito_9040',
    'pal_freq_ratios_montecito_9050',
    'pal_freq_ratios_montvale_9140m',
    'pal_freq_ratios_reserved_arg',
    'pal_halt_info',
    'pal_halt_info_bad_buffer',
    'pal_halt_info_reserved_arg',
    'pal_halt_invalid_state',
    'pal_halt_io_reserved_info',
    'pal_halt_io_unimplemented_pa_merced',
    'pal_halt_light_reserved_arg',
    'pal_halt_light_stops_at_pal_continuation',
    'pal_halt_light_wakes_on_due_itm',
    'pal_halt_reserved_arg',
    'pal_halt_unimplemented_detail_pa_merced',
    'pal_halt_wakes_on_due_itm',
    'pal_logical_to_physical_current',
    'pal_logical_to_physical_multicore_thread',
    'pal_logical_to_physical_madison_unimplemented',
    'pal_logical_to_physical_merced_unimplemented',
    'pal_mc_clear_log',
    'pal_mc_drain',
    'pal_mc_drain_reserved_arg',
    'pal_mc_dynamic_state_bad_offset',
    'pal_mc_dynamic_state_empty',
    'pal_mc_dynamic_state_empty_aligned_offset',
    'pal_mc_dynamic_state_reserved_arg',
    'pal_mc_error_info_bad_index',
    'pal_mc_error_info_bad_level',
    'pal_mc_error_info_map_empty',
    'pal_mc_error_info_structure_empty',
    'pal_mc_expected',
    'pal_mc_register_mem',
    'pal_mc_register_mem_cacheable_rejected',
    'pal_mc_register_mem_crosses_pa_limit_merced',
    'pal_mc_resume_bad_args',
    'pal_mc_resume_bad_save_ptr',
    'pal_mc_resume_cacheable_save_ptr',
    'pal_mc_resume_crosses_pa_limit_merced',
    'pal_mc_resume_new_context_no_context',
    'pal_mc_resume_no_context',
    'pal_mem_attrib',
    'pal_mem_attrib_madison',
    'pal_mem_attrib_merced',
    'pal_mem_attrib_reserved_arg',
    'pal_mem_for_test',
    'pal_perf_mon_info',
    'pal_perf_mon_info_bad_buffer',
    'pal_perf_mon_info_merced',
    'pal_perf_mon_info_reserved_arg',
    'pal_platform_addr_bad_type',
    'pal_platform_addr_alternate_interrupt',
    'pal_platform_addr_ignores_bit63',
    'pal_platform_addr_interrupt',
    'pal_platform_addr_io',
    'pal_platform_addr_merced_default',
    'pal_platform_addr_merced_unimplemented',
    'pal_platform_addr_misaligned',
    'pal_platform_addr_unimplemented',
    'pal_pmi_entrypoint',
    'pal_pmi_entrypoint_uncacheable',
    'pal_pmi_entrypoint_unimplemented_pa_merced',
    'pal_prefetch_vis',
    'pal_prefetch_vis_merced',
    'pal_prefetch_vis_physical',
    'pal_prefetch_vis_merced_physical',
    'pal_prefetch_vis_reserved_arg',
    'pal_proc_entry_virtual_itr',
    'pal_proc_get_features',
    'pal_proc_get_features_madison_beyond_max',
    'pal_proc_get_features_madison_set16',
    'pal_proc_get_features_montecito_beyond_max',
    'pal_proc_get_features_montecito_next_set',
    'pal_proc_get_features_montecito_9010_set18',
    'pal_proc_get_features_montecito_set18',
    'pal_proc_get_features_reserved_arg',
    'pal_proc_set_features',
    'pal_proc_set_features_invalid',
    'pal_proc_set_features_madison_beyond_max',
    'pal_proc_set_features_madison_set16',
    'pal_proc_set_features_montecito_beyond_max',
    'pal_proc_set_features_montecito_next_set',
    'pal_proc_set_features_montecito_round_trip',
    'pal_proc_set_features_montecito_set18',
    'pal_ptce_info',
    'pal_ptce_info_madison',
    'pal_ptce_info_merced',
    'pal_ptce_info_reserved_arg',
    'pal_register_info_application_implemented',
    'pal_register_info_application_side_effects',
    'pal_register_info_control_implemented',
    'pal_register_info_control_side_effects',
    'pal_register_info_invalid_request',
    'pal_register_info_reserved_arg',
    'pal_rse_info',
    'pal_rse_info_madison',
    'pal_rse_info_merced',
    'pal_rse_info_reserved_arg',
    'pal_test_proc_bad_address',
    'pal_test_proc_bad_attributes',
    'pal_test_proc_crosses_pa_limit_merced',
    'pal_test_proc_healthy',
    'pal_test_proc_missing_cacheable_attr',
    'pal_unknown',
    'pal_return_values_clear_nat',
    'pal_version',
    'pal_version_madison_9m',
    'pal_version_madison',
    'pal_version_madison_zx6000',
    'pal_version_mckinley',
    'pal_version_merced',
    'pal_version_montvale',
    'pal_version_reserved_arg',
    'pal_vm_info',
    'pal_vm_info_invalid',
    'pal_vm_info_l0_data',
    'pal_vm_info_l1_data',
    'pal_vm_info_l1_instruction',
    'pal_vm_info_l2_invalid',
    'pal_vm_info_merced_l0_data',
    'pal_vm_info_merced_l0_instruction',
    'pal_vm_info_merced_l1_data',
    'pal_vm_info_merced_l1_instruction_invalid',
    'pal_vm_page_size',
    'pal_vm_page_size_merced',
    'pal_vm_page_size_reserved_arg',
    'pal_vm_summary',
    'pal_vm_summary_madison',
    'pal_vm_summary_merced',
    'pal_vm_summary_reserved_arg',
    'pal_vm_tr_read_dtr',
    'pal_vm_tr_read_empty',
    'pal_vm_tr_read_invalid',
    'pal_vm_tr_read_madison_first_invalid_dtr',
    'pal_vm_tr_read_madison_last_dtr',
    'pal_vm_tr_read_max_dtr',
    'pal_vm_tr_read_merced_first_invalid_dtr',
    'pal_vm_tr_read_merced_first_invalid_itr',
    'pal_vm_tr_read_merced_last_dtr',
    'pal_vm_tr_read_merced_last_itr',
    'pal_vm_tr_read_misaligned_buffer',
    'pal_vm_tr_read_rejects_first_non_tr',
)

CASES = bind_cases(GROUP, CASE_NAMES, globals())
