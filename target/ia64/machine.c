/*
 * IA-64 migration state.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "arch/arch.h"
#include "rse-migration.h"
#include "exec/cputlb.h"
#include "migration/vmstate.h"
#include "system/physmem.h"

static const VMStateDescription vmstate_ia64_tlb_entry = {
    .name = "ia64-tlb-entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(va, IA64TlbEntry),
        VMSTATE_UINT64(pa, IA64TlbEntry),
        VMSTATE_UINT64(ps, IA64TlbEntry),
        VMSTATE_UINT64(page_mask, IA64TlbEntry),
        VMSTATE_UINT64(pte, IA64TlbEntry),
        VMSTATE_UINT8(perm, IA64TlbEntry),
        VMSTATE_UINT8(ar, IA64TlbEntry),
        VMSTATE_UINT8(pl, IA64TlbEntry),
        VMSTATE_UINT8(valid, IA64TlbEntry),
        VMSTATE_UINT8(is_tr, IA64TlbEntry),
        VMSTATE_UINT8(pending_purge, IA64TlbEntry),
        VMSTATE_UINT32(rid, IA64TlbEntry),
        VMSTATE_UINT32(key, IA64TlbEntry),
        VMSTATE_UINT16(slot, IA64TlbEntry),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_rnat_shadow_entry = {
    .name = "ia64-rnat-shadow-entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(value, IA64RnatShadowEntry),
        VMSTATE_UINT64(addr, IA64RnatShadowEntry),
        VMSTATE_UINT64(defined, IA64RnatShadowEntry),
        VMSTATE_BOOL(valid, IA64RnatShadowEntry),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_rnat_writeback_image = {
    .name = "ia64-rnat-writeback-image",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(value, IA64RnatWritebackImage),
        VMSTATE_UINT64(addr, IA64RnatWritebackImage),
        VMSTATE_UINT64(defined, IA64RnatWritebackImage),
        VMSTATE_BOOL(valid, IA64RnatWritebackImage),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_alat_entry = {
    .name = "ia64-alat-entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(phys_addr, IA64AlatEntry),
        VMSTATE_UINT8(size, IA64AlatEntry),
        VMSTATE_UINT8(reg, IA64AlatEntry),
        VMSTATE_BOOL(fp, IA64AlatEntry),
        VMSTATE_BOOL(valid, IA64AlatEntry),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_ia32_segment = {
    .name = "ia64-ia32-segment",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(selector, SegmentCache),
        VMSTATE_UINT64(base, SegmentCache),
        VMSTATE_UINT32(limit, SegmentCache),
        VMSTATE_UINT32(flags, SegmentCache),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_ia32_fpreg = {
    .name = "ia64-ia32-fpreg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(d.low, FPReg),
        VMSTATE_UINT16(d.high, FPReg),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_ia32_xmm = {
    .name = "ia64-ia32-xmm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(ZMM_Q(0), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(1), ZMMReg),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ia64_firmware_debug_rse = {
    .name = "ia64-firmware-debug-rse",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(pgr, IA64FirmwareDebugRseState,
                             IA64_STACKED_GR_COUNT),
        VMSTATE_UINT64_ARRAY(pgr_nat, IA64FirmwareDebugRseState, 2),
        VMSTATE_UINT64_ARRAY(gr_dirty, IA64FirmwareDebugRseState, 2),
        VMSTATE_UINT64(bsp, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(bspstore, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(rnat, IA64FirmwareDebugRseState),
        VMSTATE_UINT32(bol, IA64FirmwareDebugRseState),
        VMSTATE_INT32(dirty, IA64FirmwareDebugRseState),
        VMSTATE_INT32(dirty_nat, IA64FirmwareDebugRseState),
        VMSTATE_INT32(clean, IA64FirmwareDebugRseState),
        VMSTATE_INT32(clean_nat, IA64FirmwareDebugRseState),
        VMSTATE_INT32(invalid, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(rnat_addr, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(rnat_defined, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(load_rnat, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(load_rnat_addr, IA64FirmwareDebugRseState),
        VMSTATE_UINT64(load_rnat_defined, IA64FirmwareDebugRseState),
        VMSTATE_BOOL(load_rnat_valid, IA64FirmwareDebugRseState),
        VMSTATE_STRUCT_ARRAY(rnat_shadow, IA64FirmwareDebugRseState,
                             IA64_RSE_RNAT_SHADOW_COUNT, 1,
                             vmstate_ia64_rnat_shadow_entry,
                             IA64RnatShadowEntry),
        VMSTATE_UINT8(rnat_shadow_count, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_sof, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_sol, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_sor, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_rrb_gr, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_rrb_fr, IA64FirmwareDebugRseState),
        VMSTATE_UINT8(cfm_rrb_pr, IA64FirmwareDebugRseState),
        VMSTATE_BOOL(cfle, IA64FirmwareDebugRseState),
        VMSTATE_BOOL_V(completion_pending, IA64FirmwareDebugRseState, 2),
        VMSTATE_BOOL_V(completion_demoted, IA64FirmwareDebugRseState, 2),
        VMSTATE_UINT64_V(completion_psr, IA64FirmwareDebugRseState, 2),
        VMSTATE_UINT64_V(completion_source_ip,
                         IA64FirmwareDebugRseState, 2),
        VMSTATE_UINT8_V(completion_source_slot,
                        IA64FirmwareDebugRseState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void ia64_migration_reset_fp_status(CPUIA64State *env)
{
    CPUX86State *xenv = &env->ia32;

    memset(&env->fp.fp_status, 0, sizeof(env->fp.fp_status));
    set_float_2nan_prop_rule(float_2nan_prop_ab, &env->fp.fp_status);
    set_float_3nan_prop_rule(float_3nan_prop_abc, &env->fp.fp_status);
    set_float_infzeronan_rule(float_infzeronan_dnan_never |
                              float_infzeronan_suppress_invalid,
                              &env->fp.fp_status);
    set_float_default_nan_pattern(0b11000000, &env->fp.fp_status);
    set_float_rounding_mode(float_round_nearest_even, &env->fp.fp_status);

    memset(&xenv->fp_status, 0, sizeof(xenv->fp_status));
    memset(&xenv->mmx_status, 0, sizeof(xenv->mmx_status));
    memset(&xenv->sse_status, 0, sizeof(xenv->sse_status));
    cpu_init_fp_statuses(xenv);
    update_fp_status(xenv);
    update_mxcsr_status(xenv);
}

static int ia64_cpu_pre_save(void *opaque)
{
    IA64CPU *cpu = opaque;

    g_assert(!cpu->env.alat_state.write_active);
    ia64_sync_rotating_fr(&cpu->env);
    ia64_itc_sync(&cpu->env);
    return 0;
}

static int ia64_cpu_post_load(void *opaque, int version_id)
{
    IA64CPU *cpu = opaque;
    CPUIA64State *env = &cpu->env;
    IA64CPUClass *icc = IA64_CPU_GET_CLASS(cpu);
    bool has_clean_partition =
        icc->rse_has_clean_partition;
    unsigned int i;

    if (version_id < 4) {
        /* Version 3 used CR.IFA itself as the delivery operand. */
        env->exception_state.fault_addr = env->cr_ifa;
    }
    if (version_id < 5) {
        env->interrupt.sapic_xtp = IA64_SAPIC_XTP_DISABLE;
    }
    if (version_id < 6) {
        env->interrupt.sapic_pmi_pending = 0;
        env->interrupt.sapic_init_pending = false;
    }
    if (version_id < 7) {
        env->pal.pal_mc_log_valid = false;
        env->pal.pal_cmc_pending = false;
        env->pal.pal_mca_pending = false;
        env->pal.pal_mca_active = false;
    }
    if (version_id < 8) {
        cpu->mca_rse_valid = false;
    }
    if (version_id < 10) {
        env->interrupt.sapic_init_reason = 0;
        env->pal.pal_init_active = false;
        env->pal.pal_init_entry = 0;
        env->pal.pal_init_gp = 0;
    }

    /* Migration stops vCPUs only at instruction boundaries. */
    env->rse.rse_access = false;

    if (env->mmu.tlb_data_count > IA64_TLB_MAX ||
        env->mmu.tlb_inst_count > IA64_TLB_MAX ||
        env->mmu.tlb_data_l1_count > IA64_DTLB1_MAX ||
        !ia64_rse_migration_state_valid(env, has_clean_partition) ||
        (cpu->firmware_debug.rse_valid &&
         !ia64_firmware_debug_rse_migration_state_valid(
             &cpu->firmware_debug.rse, has_clean_partition)) ||
        (cpu->mca_rse_valid &&
         !ia64_firmware_debug_rse_migration_state_valid(
             &cpu->mca_rse, has_clean_partition)) ||
        (env->interrupt.sapic_xtp & ~IA64_SAPIC_XTP_WRITABLE_MASK) != 0 ||
        (env->interrupt.sapic_pmi_pending & ~UINT16_C(0xf)) != 0 ||
        ((env->pal.pal_mca_active || env->pal.pal_init_active) &&
         !cpu->mca_rse_valid) ||
        (env->pal.pal_mca_active && env->pal.pal_init_active) ||
        (env->pal.pal_mca_pending &&
         env->pal.pal_mca_pending_record_id == 0) ||
        (env->pal.pal_mca_active &&
         env->pal.pal_mca_active_record_id == 0)) {
        return -EINVAL;
    }

    /* Every insertion path assigns a legal, nonzero page size first. */
    for (i = 0; i < IA64_TLB_MAX; i++) {
        if ((env->mmu.tlb_data[i].valid &&
             env->mmu.tlb_data[i].ps == 0) ||
            (env->mmu.tlb_inst[i].valid &&
             env->mmu.tlb_inst[i].ps == 0)) {
            return -EINVAL;
        }
    }
    for (i = 0; i < IA64_DTLB1_MAX; i++) {
        if (env->mmu.tlb_data_l1[i].valid &&
            env->mmu.tlb_data_l1[i].ps == 0) {
            return -EINVAL;
        }
    }

    memset(env->mmu.tlb_data_micro, 0,
           sizeof(env->mmu.tlb_data_micro));
    memset(env->mmu.tlb_inst_micro, 0,
           sizeof(env->mmu.tlb_inst_micro));
    memset(&env->mmu.code_tlb_ed, 0, sizeof(env->mmu.code_tlb_ed));
    memset(env->mmu.tlb_data_l1_lookup, 0,
           sizeof(env->mmu.tlb_data_l1_lookup));
    env->mmu.tlb_data_l1_last = 0;
    for (i = 0; i < IA64_TLB_MAX; i++) {
        env->mmu.tlb_data[i].micro_generation = 0;
        env->mmu.tlb_inst[i].micro_generation = 0;
    }
    env->mmu.tlb_data_generation = 1;
    env->mmu.tlb_inst_generation = 1;

    /*
     * The host softmmu TLB is rebuilt after migration.  Any entries recorded
     * solely so a later PSR suppression transition could flush them are
     * therefore already gone.
     */
    env->exception_state.suppressed_tlb_count = 0;
    env->exception_state.suppressed_tlb_overflow = false;

    /* Reset ALAT entries with the host-local write generation. */
    memset(env->alat_state.alat, 0, sizeof(env->alat_state.alat));
    env->alat_state.alat_full = cpu->alat_full;
    env->alat_state.write_active = false;
    env->alat_state.write_observed = false;
    env->alat_state.write_generation = 0;
    ia64_alat_invala(env);
    env->pal.pal_proc_feature_status &= icc->pal_proc_feature_available;

    env->fp.transaction.active = false;
    env->fp.rotating_fr_materialized_rrb = env->cfm_rrb_fr;
    env->ia32_sse_instruction_active = false;
    ia64_migration_reset_fp_status(env);

    tlb_flush(CPU(cpu));
    ia64_sapic_update_interrupt(env);
    return 0;
}

const VMStateDescription vmstate_ia64_cpu = {
    .name = "cpu",
    .version_id = 10,
    .minimum_version_id = 3,
    .pre_save = ia64_cpu_pre_save,
    .post_load = ia64_cpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, IA64CPU, 1, vmstate_cpu_common, CPUState),
        VMSTATE_UINT64_EQUAL_V(semantic_profile_id, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(semantic_profile_abi, IA64CPU, 9),
        VMSTATE_UINT8_EQUAL_V(migration_alat_full, IA64CPU, 9),
        VMSTATE_UINT64_EQUAL_V(firmware_compat_flags, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(socket_id, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(core_id, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(thread_id, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(cores_per_socket, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(threads_per_core, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(package_base, IA64CPU, 9),
        VMSTATE_UINT32_EQUAL_V(package_cpus, IA64CPU, 9),

        /* Native IA-64 register state. */
        VMSTATE_UINT64_ARRAY(env.gr, IA64CPU, IA64_GR_COUNT),
        VMSTATE_UINT64_ARRAY(env.pr, IA64CPU, IA64_PR_COUNT),
        VMSTATE_UINT64_ARRAY(env.br, IA64CPU, IA64_BR_COUNT),
        VMSTATE_UINT64(env.ip, IA64CPU),
        VMSTATE_UINT64(env.last_successful_bundle, IA64CPU),
        VMSTATE_UINT64(env.psr, IA64CPU),
        VMSTATE_UINT64_ARRAY(env.nat, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.banked_gr, IA64CPU, 16),
        VMSTATE_UINT16(env.banked_nat, IA64CPU),

        /* Restart-visible exception state. */
        VMSTATE_UINT64(env.exception_state.fault_ip, IA64CPU),
        VMSTATE_UINT64_V(env.exception_state.fault_addr, IA64CPU, 4),
        VMSTATE_UINT64(env.exception_state.fault_imm, IA64CPU),
        VMSTATE_UINT64(env.exception_state.fault_tmpl, IA64CPU),
        VMSTATE_UINT32(env.exception_state.exception, IA64CPU),
        VMSTATE_UINT32(env.exception_state.fault_exception, IA64CPU),
        VMSTATE_UINT32(env.exception_state.fault_slot, IA64CPU),
        VMSTATE_BOOL(env.exception_state.ia32_trap, IA64CPU),
        VMSTATE_BOOL(env.exception_state.ia32_transition_trap, IA64CPU),
        VMSTATE_BOOL_V(env.exception_state.native_completion_trap, IA64CPU,
                       3),
        VMSTATE_UINT64_V(env.exception_state.psr_before_insn, IA64CPU, 3),
        VMSTATE_BOOL(env.exception_state.psr_ic_inflight, IA64CPU),
        VMSTATE_UINT64(env.exception_state.psr_suppression_before_insn,
                       IA64CPU),

        VMSTATE_UINT64_ARRAY(env.cr, IA64CPU, IA64_CR_COUNT),
        VMSTATE_UINT64_ARRAY(env.msr, IA64CPU, IA64_MSR_COUNT),
        VMSTATE_UINT64_ARRAY(env.pmc, IA64CPU, IA64_PMC_COUNT),
        VMSTATE_UINT64_ARRAY(env.pmd, IA64CPU, IA64_PMD_COUNT),
        VMSTATE_UINT64_ARRAY(env.pkr, IA64CPU, IA64_PKR_COUNT),
        VMSTATE_UINT64_ARRAY(env.dbr, IA64CPU, IA64_DBR_COUNT),
        VMSTATE_UINT64_ARRAY(env.ibr, IA64CPU, IA64_IBR_COUNT),
        VMSTATE_UINT64_ARRAY(env.dahr, IA64CPU, 8),
        VMSTATE_UINT8(env.ia32_data_breakpoints, IA64CPU),
        VMSTATE_UINT64_ARRAY(env.rr, IA64CPU, IA64_RR_COUNT),
        VMSTATE_UINT64_ARRAY(env.ar, IA64CPU, IA64_AR_COUNT),
        VMSTATE_UINT8(env.cfm_sof, IA64CPU),
        VMSTATE_UINT8(env.cfm_sol, IA64CPU),
        VMSTATE_UINT8(env.cfm_sor, IA64CPU),
        VMSTATE_UINT8(env.cfm_rrb_gr, IA64CPU),
        VMSTATE_UINT8(env.cfm_rrb_fr, IA64CPU),
        VMSTATE_UINT8(env.cfm_rrb_pr, IA64CPU),

        /* Translation-register and Merced DTLB1 contents. */
        VMSTATE_STRUCT_ARRAY(env.mmu.tlb_data, IA64CPU, IA64_TLB_MAX, 1,
                             vmstate_ia64_tlb_entry, IA64TlbEntry),
        VMSTATE_STRUCT_ARRAY(env.mmu.tlb_inst, IA64CPU, IA64_TLB_MAX, 1,
                             vmstate_ia64_tlb_entry, IA64TlbEntry),
        VMSTATE_STRUCT_ARRAY(env.mmu.tlb_data_l1, IA64CPU, IA64_DTLB1_MAX,
                             1, vmstate_ia64_tlb_entry, IA64TlbEntry),
        VMSTATE_UINT64_ARRAY(env.mmu.tlb_data_l1_age, IA64CPU,
                             IA64_DTLB1_MAX),
        VMSTATE_UINT64(env.mmu.tlb_data_l1_clock, IA64CPU),
        VMSTATE_UINT16(env.mmu.tlb_data_count, IA64CPU),
        VMSTATE_UINT16(env.mmu.tlb_inst_count, IA64CPU),
        VMSTATE_UINT8(env.mmu.tlb_data_l1_count, IA64CPU),
        VMSTATE_UINT16(env.mmu.tlb_data_replace, IA64CPU),
        VMSTATE_UINT16(env.mmu.tlb_inst_replace, IA64CPU),
        VMSTATE_UINT16(env.mmu.pending_purge_data_count, IA64CPU),
        VMSTATE_UINT16(env.mmu.pending_purge_inst_count, IA64CPU),

        /* Local SAPIC and interval timer. */
        VMSTATE_UINT8(env.interrupt.pending_extint, IA64CPU),
        VMSTATE_UINT8_V(env.interrupt.sapic_xtp, IA64CPU, 5),
        VMSTATE_UINT16_V(env.interrupt.sapic_pmi_pending, IA64CPU, 6),
        VMSTATE_BOOL_V(env.interrupt.sapic_init_pending, IA64CPU, 6),
        VMSTATE_UINT8_V(env.interrupt.sapic_init_reason, IA64CPU, 10),
        VMSTATE_BOOL(env.interrupt.pal_halt_wake, IA64CPU),
        VMSTATE_UINT64_ARRAY(env.interrupt.sapic_irr, IA64CPU, 4),
        VMSTATE_UINT64_ARRAY(env.interrupt.sapic_isr, IA64CPU, 4),
        VMSTATE_INT64(env.interrupt.itc_last_ns, IA64CPU),
        VMSTATE_UINT32(env.interrupt.itc_fraction, IA64CPU),
        VMSTATE_UINT64(env.interrupt.itc_tick_debt, IA64CPU),
        VMSTATE_UINT64(env.interrupt.itm_armed_value, IA64CPU),
        VMSTATE_UINT64(env.interrupt.itm_last_match, IA64CPU),
        VMSTATE_BOOL(env.interrupt.itm_armed, IA64CPU),
        VMSTATE_BOOL(env.interrupt.itm_last_match_valid, IA64CPU),
        VMSTATE_TIMER_PTR(itm_timer, IA64CPU),

        /* PAL, RSE, ALAT and floating-point state. */
        VMSTATE_BOOL(env.pal.pal_mc_expected, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_bus_feature_status, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_proc_feature_status, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_mc_save_addr, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_pmi_entry, IA64CPU),
        VMSTATE_BOOL(env.pal.pal_proc_copy_valid, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_proc_copy_addr, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_interrupt_block_addr, IA64CPU),
        VMSTATE_UINT64(env.pal.pal_io_block_addr, IA64CPU),
        VMSTATE_BOOL_V(env.pal.pal_mc_log_valid, IA64CPU, 7),
        VMSTATE_BOOL_V(env.pal.pal_cmc_pending, IA64CPU, 7),
        VMSTATE_BOOL_V(env.pal.pal_mca_pending, IA64CPU, 7),
        VMSTATE_BOOL_V(env.pal.pal_mca_active, IA64CPU, 7),
        VMSTATE_BOOL_V(env.pal.pal_init_active, IA64CPU, 10),
        VMSTATE_UINT8_V(env.pal.pal_mc_severity, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_error_map, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_state_parameter, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_status, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_address, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_information, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mc_ip, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mca_entry, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mca_gp, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mca_pending_record_id, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_mca_active_record_id, IA64CPU, 7),
        VMSTATE_UINT64_V(env.pal.pal_init_entry, IA64CPU, 10),
        VMSTATE_UINT64_V(env.pal.pal_init_gp, IA64CPU, 10),

        VMSTATE_UINT64_ARRAY(env.rse.rse_pgr, IA64CPU,
                             IA64_STACKED_GR_COUNT),
        VMSTATE_UINT64_ARRAY(env.rse.rse_pgr_nat, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.rse.rse_gr_dirty, IA64CPU, 2),
        VMSTATE_UINT32(env.rse.rse_bol, IA64CPU),
        VMSTATE_INT32(env.rse.rse_dirty, IA64CPU),
        VMSTATE_INT32(env.rse.rse_dirty_nat, IA64CPU),
        VMSTATE_INT32(env.rse.rse_clean, IA64CPU),
        VMSTATE_INT32(env.rse.rse_clean_nat, IA64CPU),
        VMSTATE_INT32(env.rse.rse_invalid, IA64CPU),
        VMSTATE_BOOL(env.rse.rse_cfle, IA64CPU),
        VMSTATE_BOOL_V(env.rse.rse_completion_pending, IA64CPU, 3),
        VMSTATE_BOOL_V(env.rse.rse_completion_demoted, IA64CPU, 3),
        VMSTATE_UINT64_V(env.rse.rse_completion_psr, IA64CPU, 3),
        VMSTATE_UINT64_V(env.rse.rse_completion_source_ip, IA64CPU, 3),
        VMSTATE_UINT8_V(env.rse.rse_completion_source_slot, IA64CPU, 3),
        VMSTATE_UINT64(env.rse.rse_rnat_addr, IA64CPU),
        VMSTATE_UINT64(env.rse.rse_rnat_defined, IA64CPU),
        VMSTATE_UINT64(env.rse.rse_load_rnat, IA64CPU),
        VMSTATE_UINT64(env.rse.rse_load_rnat_addr, IA64CPU),
        VMSTATE_UINT64(env.rse.rse_load_rnat_defined, IA64CPU),
        VMSTATE_BOOL(env.rse.rse_load_rnat_valid, IA64CPU),
        VMSTATE_STRUCT(env.rse.rse_writeback_rnat, IA64CPU, 2,
                       vmstate_ia64_rnat_writeback_image,
                       IA64RnatWritebackImage),
        VMSTATE_STRUCT_ARRAY(env.rse.rse_rnat_shadow, IA64CPU,
                             IA64_RSE_RNAT_SHADOW_COUNT, 1,
                             vmstate_ia64_rnat_shadow_entry,
                             IA64RnatShadowEntry),
        VMSTATE_UINT8(env.rse.rse_rnat_shadow_count, IA64CPU),

        VMSTATE_BOOL(env.instruction_group_start, IA64CPU),
        VMSTATE_STRUCT_ARRAY(env.alat_state.alat, IA64CPU,
                             IA64_ALAT_ENTRIES, 1, vmstate_ia64_alat_entry,
                             IA64AlatEntry),
        VMSTATE_UINT64(env.bundles_retired, IA64CPU),
        VMSTATE_UINT64_ARRAY(env.fp.fr, IA64CPU, IA64_FR_COUNT),
        VMSTATE_UINT64_ARRAY(env.fp.fr_nat, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.fp.fr_sig, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.fp.fr_ext_mant, IA64CPU, IA64_FR_COUNT),
        VMSTATE_UINT32_ARRAY(env.fp.fr_ext_exp, IA64CPU, IA64_FR_COUNT),
        VMSTATE_UINT64_ARRAY(env.fp.fr_ext_sign, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.fp.fr_ext_valid, IA64CPU, 2),
        VMSTATE_UINT64_ARRAY(env.fp.fr_int_value, IA64CPU, IA64_FR_COUNT),
        VMSTATE_UINT64_ARRAY(env.fp.fr_int_origin, IA64CPU, 2),
        VMSTATE_BOOL(env.fp.rotating_fr_live, IA64CPU),
        VMSTATE_UINT64(env.fp.fpswa_result_low, IA64CPU),
        VMSTATE_UINT64(env.fp.fpswa_result_high, IA64CPU),
        VMSTATE_UINT64(env.fp.fpswa_flags, IA64CPU),
        VMSTATE_UINT8(env.fp.fpswa_dest_fr, IA64CPU),
        VMSTATE_UINT8(env.fp.fpswa_dest_pr, IA64CPU),
        VMSTATE_UINT8(env.fp.fpswa_sf, IA64CPU),
        VMSTATE_BOOL(env.fp.fpswa_pending, IA64CPU),
        VMSTATE_BOOL(env.fp.fpswa_fpa, IA64CPU),

        /* Private IA-32 execution-engine state. */
        VMSTATE_UINT64_ARRAY(env.ia32.regs, IA64CPU, CPU_NB_REGS),
        VMSTATE_UINT64(env.ia32.eip, IA64CPU),
        VMSTATE_UINT64(env.ia32.eflags, IA64CPU),
        VMSTATE_UINT64(env.ia32.cc_dst, IA64CPU),
        VMSTATE_UINT64(env.ia32.cc_src, IA64CPU),
        VMSTATE_UINT64(env.ia32.cc_src2, IA64CPU),
        VMSTATE_UINT32(env.ia32.cc_op, IA64CPU),
        VMSTATE_INT32(env.ia32.df, IA64CPU),
        VMSTATE_UINT32(env.ia32.hflags, IA64CPU),
        VMSTATE_UINT32(env.ia32.hflags2, IA64CPU),
        VMSTATE_STRUCT_ARRAY(env.ia32.segs, IA64CPU, 6, 1,
                             vmstate_ia64_ia32_segment, SegmentCache),
        VMSTATE_STRUCT(env.ia32.ldt, IA64CPU, 1,
                       vmstate_ia64_ia32_segment, SegmentCache),
        VMSTATE_STRUCT(env.ia32.tr, IA64CPU, 1,
                       vmstate_ia64_ia32_segment, SegmentCache),
        VMSTATE_STRUCT(env.ia32.gdt, IA64CPU, 1,
                       vmstate_ia64_ia32_segment, SegmentCache),
        VMSTATE_STRUCT(env.ia32.idt, IA64CPU, 1,
                       vmstate_ia64_ia32_segment, SegmentCache),
        VMSTATE_UINT64_ARRAY(env.ia32.cr, IA64CPU, 5),
        VMSTATE_INT32(env.ia32.a20_mask, IA64CPU),
        VMSTATE_UINT32(env.ia32.fpstt, IA64CPU),
        VMSTATE_UINT16(env.ia32.fpus, IA64CPU),
        VMSTATE_UINT16(env.ia32.fpuc, IA64CPU),
        VMSTATE_UINT8_ARRAY(env.ia32.fptags, IA64CPU, 8),
        VMSTATE_STRUCT_ARRAY(env.ia32.fpregs, IA64CPU, 8, 1,
                             vmstate_ia64_ia32_fpreg, FPReg),
        VMSTATE_UINT16(env.ia32.fpop, IA64CPU),
        VMSTATE_UINT16(env.ia32.fpcs, IA64CPU),
        VMSTATE_UINT16(env.ia32.fpds, IA64CPU),
        VMSTATE_UINT64(env.ia32.fpip, IA64CPU),
        VMSTATE_UINT64(env.ia32.fpdp, IA64CPU),
        VMSTATE_UINT32(env.ia32.mxcsr, IA64CPU),
        VMSTATE_STRUCT_ARRAY(env.ia32.xmm_regs, IA64CPU, CPU_NB_REGS, 1,
                             vmstate_ia64_ia32_xmm, ZMMReg),
        VMSTATE_UINT32(env.ia32.sysenter_cs, IA64CPU),
        VMSTATE_UINT64(env.ia32.sysenter_esp, IA64CPU),
        VMSTATE_UINT64(env.ia32.sysenter_eip, IA64CPU),
        VMSTATE_UINT64(env.ia32.star, IA64CPU),
        VMSTATE_UINT64(env.ia32.vm_hsave, IA64CPU),
        VMSTATE_UINT64(env.ia32.tsc_offset, IA64CPU),
        VMSTATE_UINT64(env.ia32.tsc_aux, IA64CPU),
        VMSTATE_UINT64(env.ia32.xcr0, IA64CPU),
        VMSTATE_UINT64_ARRAY(env.ia32.dr, IA64CPU, 8),
        VMSTATE_INT32(env.ia32.error_code, IA64CPU),
        VMSTATE_INT32(env.ia32.exception_is_int, IA64CPU),
        VMSTATE_UINT64(env.ia32.exception_next_eip, IA64CPU),
        VMSTATE_INT32(env.ia32.old_exception, IA64CPU),
        VMSTATE_INT32(env.ia32.exception_nr, IA64CPU),
        VMSTATE_UINT8(env.ia32.soft_interrupt, IA64CPU),
        VMSTATE_UINT8(env.ia32.has_error_code, IA64CPU),

        /* Firmware debug callback bridge. */
        VMSTATE_UINT8_ARRAY(firmware_debug.context, IA64CPU,
                            IA64_FW_DEBUG_CONTEXT_SIZE),
        VMSTATE_STRUCT(firmware_debug.rse, IA64CPU, 1,
                       vmstate_ia64_firmware_debug_rse,
                       IA64FirmwareDebugRseState),
        VMSTATE_STRUCT(firmware_debug.rse.writeback_rnat, IA64CPU, 2,
                       vmstate_ia64_rnat_writeback_image,
                       IA64RnatWritebackImage),
        VMSTATE_UINT16(firmware_debug.vector, IA64CPU),
        VMSTATE_BOOL(firmware_debug.context_valid, IA64CPU),
        VMSTATE_BOOL(firmware_debug.handler_active, IA64CPU),
        VMSTATE_BOOL(firmware_debug.rse_valid, IA64CPU),
        VMSTATE_STRUCT(mca_rse, IA64CPU, 8,
                       vmstate_ia64_firmware_debug_rse,
                       IA64RSEContextState),
        VMSTATE_STRUCT(mca_rse.writeback_rnat, IA64CPU, 8,
                       vmstate_ia64_rnat_writeback_image,
                       IA64RnatWritebackImage),
        VMSTATE_BOOL_V(mca_rse_valid, IA64CPU, 8),
        VMSTATE_BOOL(boot_info_pending, IA64CPU),
        VMSTATE_END_OF_LIST()
    }
};
