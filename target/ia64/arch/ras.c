/*
 * IA-64 processor minimal-state handling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "exec-access.h"
#include "qemu/main-loop.h"

#define IA64_MIN_STATE_QWORDS 128

typedef struct IA64MinimalStateArea {
    uint64_t nat_bits;
    uint64_t static_gr[15];
    uint64_t bank0_gr[16];
    uint64_t bank1_gr[16];
    uint64_t pr;
    uint64_t br0;
    uint64_t rsc;
    uint64_t iip;
    uint64_t ipsr;
    uint64_t ifs;
    uint64_t xip;
    uint64_t xpsr;
    uint64_t xfs;
    uint64_t br1;
    uint64_t reserved[70];
} IA64MinimalStateArea;

static bool ia64_min_state_address_valid(const CPUIA64State *env,
                                         uint64_t address)
{
    uint64_t pa = ia64_physical_address(address);
    uint64_t limit = UINT64_C(1) <<
        ia64_env_cpu_class_const(env)->phys_addr_bits;

    return address != 0 && (address & 0x1ff) == 0 &&
           ia64_pa_is_implemented(env, address) &&
           sizeof(IA64MinimalStateArea) <= limit &&
           pa <= limit - sizeof(IA64MinimalStateArea);
}

static uint64_t ia64_min_state_nat_bits(const CPUIA64State *env)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 1; i < 16; i++) {
        value |= (uint64_t)ia64_gr_nat_get(env, i) << i;
    }
    for (i = 0; i < 16; i++) {
        bool bank0_nat;
        bool bank1_nat;

        if (env->psr & IA64_PSR_BN) {
            bank0_nat = (env->banked_nat >> i) & 1;
            bank1_nat = ia64_gr_nat_get(env, 16 + i);
        } else {
            bank0_nat = ia64_gr_nat_get(env, 16 + i);
            bank1_nat = (env->banked_nat >> i) & 1;
        }
        value |= (uint64_t)bank0_nat << (16 + i);
        value |= (uint64_t)bank1_nat << (32 + i);
    }
    return value;
}

static void ia64_min_state_collect_banks(const CPUIA64State *env,
                                         IA64MinimalStateArea *state)
{
    unsigned int i;

    for (i = 0; i < 16; i++) {
        if (env->psr & IA64_PSR_BN) {
            state->bank0_gr[i] = env->banked_gr[i];
            state->bank1_gr[i] = env->gr[16 + i];
        } else {
            state->bank0_gr[i] = env->gr[16 + i];
            state->bank1_gr[i] = env->banked_gr[i];
        }
    }
}

static void ia64_min_state_to_le(IA64MinimalStateArea *state)
{
    uint64_t *word = (uint64_t *)state;
    unsigned int i;

    for (i = 0; i < IA64_MIN_STATE_QWORDS; i++) {
        word[i] = cpu_to_le64(word[i]);
    }
}

static void ia64_min_state_from_le(IA64MinimalStateArea *state)
{
    uint64_t *word = (uint64_t *)state;
    unsigned int i;

    for (i = 0; i < IA64_MIN_STATE_QWORDS; i++) {
        word[i] = le64_to_cpu(word[i]);
    }
}

bool ia64_ras_save_min_state(CPUIA64State *env, uint64_t address)
{
    IA64MinimalStateArea state = { 0 };
    unsigned int i;

    QEMU_BUILD_BUG_ON(sizeof(state) != 1024);
    if (!ia64_min_state_address_valid(env, address)) {
        return false;
    }

    state.nat_bits = ia64_min_state_nat_bits(env);
    for (i = 0; i < 15; i++) {
        state.static_gr[i] = env->gr[i + 1];
    }
    ia64_min_state_collect_banks(env, &state);
    state.pr = ia64_system_read_pr(env);
    state.br0 = env->br[IA64_BR_RETURN_LINK];
    state.rsc = env->ar_rsc;
    state.iip = env->ip;
    state.ipsr = env->psr;
    state.ifs = IA64_IFS_V | ia64_rse_current_cfm(env);
    if (env->psr & IA64_PSR_IC) {
        state.xip = state.iip;
        state.xpsr = state.ipsr;
        state.xfs = state.ifs;
    } else {
        state.xip = env->cr_iip;
        state.xpsr = env->cr_ipsr;
        state.xfs = env->cr_ifs;
    }
    state.br1 = env->br[IA64_BR_STATIC0];

    ia64_min_state_to_le(&state);
    return ia64_exec_physical_rw(ia64_physical_address(address), &state,
                                 sizeof(state), true);
}

static uint64_t ia64_ras_context_cfm(const IA64RSEContextState *state)
{
    return state->cfm_sof |
        ((uint64_t)state->cfm_sol << IA64_CFM_SOL_SHIFT) |
        ((uint64_t)state->cfm_sor << IA64_CFM_SOR_SHIFT) |
        ((uint64_t)state->cfm_rrb_gr << IA64_CFM_RRB_GR_SHIFT) |
        ((uint64_t)state->cfm_rrb_fr << IA64_CFM_RRB_FR_SHIFT) |
        ((uint64_t)state->cfm_rrb_pr << IA64_CFM_RRB_PR_SHIFT);
}

bool ia64_ras_restore_min_state(IA64CPU *cpu, uint64_t address,
                                bool new_context)
{
    CPUIA64State *env = &cpu->env;
    IA64MinimalStateArea state;
    uint64_t cfm;
    uint64_t old_psr;
    unsigned int i;

    if (!ia64_min_state_address_valid(env, address) ||
        !ia64_exec_physical_rw(ia64_physical_address(address), &state,
                               sizeof(state), false)) {
        return false;
    }
    ia64_min_state_from_le(&state);
    cfm = state.ifs & IA64_IFS_IFM_MASK;
    if (!(state.ifs & IA64_IFS_V) ||
        (state.ifs & ~(IA64_IFS_V | IA64_IFS_IFM_MASK)) != 0 ||
        !ia64_cfm_frame_fields_valid(cfm & IA64_CFM_SOF_MASK,
            (cfm & IA64_CFM_SOL_MASK) >> IA64_CFM_SOL_SHIFT,
            (cfm & IA64_CFM_SOR_MASK) >> IA64_CFM_SOR_SHIFT)) {
        return false;
    }

    if (!new_context) {
        if (!cpu->mca_rse_valid ||
            cfm != ia64_ras_context_cfm(&cpu->mca_rse)) {
            return false;
        }
        ia64_rse_restore_context(env, &cpu->mca_rse);
    }

    for (i = 0; i < 15; i++) {
        env->gr[i + 1] = state.static_gr[i];
        ia64_gr_nat_set(env, i + 1, (state.nat_bits >> (i + 1)) & 1);
    }

    old_psr = env->psr;
    ia64_set_psr(env, state.ipsr);
    for (i = 0; i < 16; i++) {
        bool bank0_nat = (state.nat_bits >> (16 + i)) & 1;
        bool bank1_nat = (state.nat_bits >> (32 + i)) & 1;

        if (state.ipsr & IA64_PSR_BN) {
            env->banked_gr[i] = state.bank0_gr[i];
            env->gr[16 + i] = state.bank1_gr[i];
            ia64_gr_nat_set(env, 16 + i, bank1_nat);
            if (bank0_nat) {
                env->banked_nat |= 1U << i;
            } else {
                env->banked_nat &= ~(1U << i);
            }
        } else {
            env->gr[16 + i] = state.bank0_gr[i];
            env->banked_gr[i] = state.bank1_gr[i];
            ia64_gr_nat_set(env, 16 + i, bank0_nat);
            if (bank1_nat) {
                env->banked_nat |= 1U << i;
            } else {
                env->banked_nat &= ~(1U << i);
            }
        }
    }
    ia64_system_write_pr(env, state.pr | 1, UINT64_MAX);
    env->br[IA64_BR_RETURN_LINK] = state.br0;
    if (!new_context) {
        env->br[IA64_BR_STATIC0] = state.br1;
    }
    env->ar_rsc = state.rsc;
    env->ip = state.iip;
    if (!(state.ipsr & IA64_PSR_IC)) {
        env->cr_iip = state.xip;
        env->cr_ipsr = state.xpsr;
        env->cr_ifs = state.xfs;
    }
    env->instruction_group_start = true;
    ia64_flush_on_pk_change(env, old_psr);
    ia64_tlb_serialize(env, 1, 1);
    if (new_context) {
        ia64_rse_return_from_min_state(env, cfm);
    }
    cpu->mca_rse_valid = false;
    return true;
}

bool ia64_ras_enter_pmi(CPUIA64State *env, uint8_t vector)
{
    uint64_t old_psr = env->psr;
    uint64_t old_rsc = env->ar_rsc;
    uint64_t old_b0 = env->br[IA64_BR_RETURN_LINK];
    uint64_t old_b1 = env->br[IA64_BR_STATIC0];
    uint64_t old_pr = ia64_system_read_pr(env);
    uint64_t save_address = env->pal.pal_mc_save_addr;
    unsigned int reg;

    if (env->pal.pal_pmi_entry == 0 ||
        (save_address != 0 &&
         !ia64_ras_save_min_state(env, save_address))) {
        return false;
    }

    ia64_set_psr(env, IA64_PSR_BN | IA64_PSR_MC);
    env->ar_rsc = 0;
    env->gr[IA64_RAS_GR_PMI_VECTOR] = vector;
    env->gr[IA64_RAS_GR_PMI_MIN_STATE] = save_address;
    env->gr[IA64_RAS_GR_PMI_RSC] = old_rsc;
    env->gr[IA64_RAS_GR_PMI_B0] = old_b0;
    env->gr[IA64_RAS_GR_PMI_B1] = old_b1;
    env->gr[IA64_RAS_GR_PMI_PREDICATES] = old_pr;
    for (reg = IA64_RAS_GR_PMI_VECTOR;
         reg <= IA64_RAS_GR_PMI_PREDICATES; reg++) {
        ia64_gr_nat_set(env, reg, false);
    }
    env->ip = ia64_physical_address(env->pal.pal_pmi_entry);
    env->instruction_group_start = true;
    env_cpu(env)->halted = 0;
    ia64_flush_on_pk_change(env, old_psr);
    ia64_tlb_serialize(env, 1, 1);
    return true;
}

bool ia64_ras_enter_mca(IA64CPU *cpu)
{
    CPUIA64State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint64_t old_psr = env->psr;
    uint64_t save_address = env->pal.pal_mc_save_addr;
    uint64_t record_id;
    uint8_t severity;

    /* SDM Vol.2 11.3.1: retain masked MCAs until PSR.mc becomes zero. */
    if (!env->pal.pal_mca_pending || (env->psr & IA64_PSR_MC)) {
        return false;
    }
    if (env->pal.pal_mca_active) {
        return false;
    }
    if (!cpu->boot_info_valid || !env->pal.pal_mca_entry ||
        !ia64_ras_save_min_state(env, save_address)) {
        env->pal.pal_mca_pending = false;
        ia64_sapic_update_interrupt(env);
        return false;
    }

    record_id = env->pal.pal_mca_pending_record_id;
    severity = env->pal.pal_mc_severity;
    env->pal.pal_mca_pending = false;
    env->pal.pal_mca_active = true;
    env->pal.pal_mca_active_record_id = record_id;

    ia64_rse_save_context(env, &cpu->mca_rse);
    cpu->mca_rse_valid = true;
    ia64_rse_cover(env);
    ia64_set_psr(env, IA64_PSR_BN | IA64_PSR_MC |
                 (old_psr & (IA64_PSR_MFL | IA64_PSR_MFH | IA64_PSR_PK)));
    env->ar_rsc &= ~IA64_RSC_MODE;
    env->gr[IA64_RAS_GR_SAL_MIN_STATE] =
        save_address + sizeof(IA64MinimalStateArea);
    env->gr[IA64_RAS_GR_PAL_MIN_STATE] = save_address;
    env->gr[IA64_RAS_GR_PROCESSOR_STATE] =
        env->pal.pal_mc_state_parameter;
    env->gr[IA64_RAS_GR_PALE_RETURN] = 0;
    env->gr[IA64_RAS_GR_SALE_ENTRY_STATE] = 1;
    for (unsigned int reg = IA64_RAS_GR_SAL_MIN_STATE;
         reg <= IA64_RAS_GR_SALE_ENTRY_STATE; reg++) {
        ia64_gr_nat_set(env, reg, false);
    }
    env->gr[IA64_GR_GLOBAL_POINTER] = env->pal.pal_mca_gp;
    env->gr[IA64_GR_STACK_POINTER] = cpu->boot_info.stack_pointer;
    env->gr[IA64_GR_RETURN0] = record_id;
    env->gr[IA64_GR_RETURN1] = severity;
    env->gr[IA64_GR_RETURN2] = save_address;
    ia64_gr_nat_set(env, IA64_GR_GLOBAL_POINTER, false);
    ia64_gr_nat_set(env, IA64_GR_STACK_POINTER, false);
    ia64_gr_nat_set(env, IA64_GR_RETURN0, false);
    ia64_gr_nat_set(env, IA64_GR_RETURN1, false);
    ia64_gr_nat_set(env, IA64_GR_RETURN2, false);
    env->ip = ia64_physical_address(env->pal.pal_mca_entry);
    env->instruction_group_start = true;
    cs->halted = 0;
    ia64_flush_on_pk_change(env, old_psr);
    ia64_tlb_serialize(env, 1, 1);
    ia64_sapic_update_interrupt(env);
    return true;
}

bool ia64_ras_enter_init(IA64CPU *cpu)
{
    CPUIA64State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint64_t old_psr = env->psr;
    uint64_t save_address = env->pal.pal_mc_save_addr;
    uint8_t reason;

    if (!env->interrupt.sapic_init_pending || env->pal.pal_init_active ||
        !cpu->boot_info_valid || !env->pal.pal_init_entry ||
        !ia64_ras_save_min_state(env, save_address)) {
        return false;
    }
    reason = env->interrupt.sapic_init_reason;
    ia64_sapic_accept_init(env);
    env->pal.pal_init_active = true;
    ia64_rse_save_context(env, &cpu->mca_rse);
    cpu->mca_rse_valid = true;
    ia64_rse_cover(env);
    ia64_set_psr(env, IA64_PSR_BN | IA64_PSR_MC |
                 (old_psr & (IA64_PSR_MFL | IA64_PSR_MFH | IA64_PSR_PK)));
    env->ar_rsc &= ~IA64_RSC_MODE;
    env->gr[IA64_GR_GLOBAL_POINTER] = env->pal.pal_init_gp;
    env->gr[IA64_GR_STACK_POINTER] = cpu->boot_info.stack_pointer;
    env->gr[IA64_GR_RETURN0] = reason;
    env->gr[IA64_GR_RETURN1] = save_address;
    ia64_gr_nat_set(env, IA64_GR_GLOBAL_POINTER, false);
    ia64_gr_nat_set(env, IA64_GR_STACK_POINTER, false);
    ia64_gr_nat_set(env, IA64_GR_RETURN0, false);
    ia64_gr_nat_set(env, IA64_GR_RETURN1, false);
    env->ip = ia64_physical_address(env->pal.pal_init_entry);
    env->instruction_group_start = true;
    cs->halted = 0;
    ia64_flush_on_pk_change(env, old_psr);
    ia64_tlb_serialize(env, 1, 1);
    ia64_sapic_update_interrupt(env);
    return true;
}

static uint64_t ia64_ras_processor_state_parameter(
    CPUIA64State *env, uint8_t severity)
{
    uint64_t value = BIT_ULL(5) | BIT_ULL(6) | BIT_ULL(7) |
        BIT_ULL(8) | BIT_ULL(12) | BIT_ULL(13) | BIT_ULL(14) |
        BIT_ULL(17) | BIT_ULL(20) | BIT_ULL(24) | BIT_ULL(25) |
        BIT_ULL(26) | BIT_ULL(27) | BIT_ULL(29) | BIT_ULL(30) |
        BIT_ULL(31) | BIT_ULL(61);

    if (severity == 2) {
        value |= BIT_ULL(18);
    }
    if (env->pal.pal_mc_expected) {
        value |= BIT_ULL(19);
    }
    return value;
}

void ia64_ras_update_cmc(CPUIA64State *env)
{
    uint64_t cmcv = env->cr[IA64_CR_CMCV];
    uint8_t vector = cmcv;

    if (!env->pal.pal_cmc_pending || (cmcv & IA64_VECTOR_MASKED) ||
        !ia64_external_interrupt_vector_valid(vector)) {
        return;
    }
    env->pal.pal_cmc_pending = false;
    ia64_sapic_set_irq(env_cpu(env), vector);
}

typedef struct IA64RasCpuRecordRequest {
    uint64_t status;
    uint64_t address;
    uint64_t information;
    uint8_t severity;
} IA64RasCpuRecordRequest;

static void ia64_cpu_record_machine_check_work(CPUState *cs,
                                                run_on_cpu_data data)
{
    IA64RasCpuRecordRequest *request = data.host_ptr;
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;

    if (env->pal.pal_mc_log_valid) {
        env->pal.pal_mc_state_parameter |= BIT_ULL(4);
    } else {
        env->pal.pal_mc_log_valid = true;
        env->pal.pal_mc_status = request->status;
        env->pal.pal_mc_address = request->address;
        env->pal.pal_mc_information = request->information;
        env->pal.pal_mc_ip = env->ip;
    }
    env->pal.pal_mc_severity = request->severity;
    env->pal.pal_mc_error_map = BIT_ULL(24) |
        ((uint64_t)(cpu->thread_id & 0xf) << 4) |
        (cpu->core_id & 0xf);
    env->pal.pal_mc_state_parameter |=
        ia64_ras_processor_state_parameter(env, request->severity);
    if (request->severity == 2) {
        env->pal.pal_cmc_pending = true;
        ia64_ras_update_cmc(env);
    }
    g_free(request);
}

void ia64_cpu_record_machine_check(CPUState *cs, uint8_t severity,
                                   uint64_t status, uint64_t address,
                                   uint64_t information)
{
    IA64RasCpuRecordRequest *request;

    if (!cs || severity > 2) {
        return;
    }
    request = g_new(IA64RasCpuRecordRequest, 1);
    *request = (IA64RasCpuRecordRequest) {
        .status = status,
        .address = address,
        .information = information,
        .severity = severity,
    };
    if (qemu_cpu_is_self(cs)) {
        ia64_cpu_record_machine_check_work(
            cs, RUN_ON_CPU_HOST_PTR(request));
    } else {
        async_safe_run_on_cpu(cs, ia64_cpu_record_machine_check_work,
                              RUN_ON_CPU_HOST_PTR(request));
    }
}

typedef struct IA64RasMcaRequest {
    uint64_t entry;
    uint64_t gp;
    uint64_t record_id;
    uint8_t severity;
} IA64RasMcaRequest;

static void ia64_cpu_request_mca_work(CPUState *cs, run_on_cpu_data data)
{
    IA64RasMcaRequest *request = data.host_ptr;
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;

    if (request->record_id != env->pal.pal_mca_active_record_id &&
        (!env->pal.pal_mca_pending ||
         request->record_id != env->pal.pal_mca_pending_record_id)) {
        env->pal.pal_mca_entry = request->entry;
        env->pal.pal_mca_gp = request->gp;
        env->pal.pal_mca_pending_record_id = request->record_id;
        env->pal.pal_mc_severity = request->severity;
        env->pal.pal_mca_pending = true;
        ia64_sapic_update_interrupt(env);
    }
    g_free(request);
}

void ia64_cpu_request_mca(CPUState *cs, uint64_t entry, uint64_t gp,
                          uint64_t record_id, uint8_t severity)
{
    IA64RasMcaRequest *request;

    if (!cs || !entry || !record_id || severity > 2) {
        return;
    }
    request = g_new(IA64RasMcaRequest, 1);
    *request = (IA64RasMcaRequest) {
        .entry = entry,
        .gp = gp,
        .record_id = record_id,
        .severity = severity,
    };
    if (qemu_cpu_is_self(cs)) {
        ia64_cpu_request_mca_work(cs, RUN_ON_CPU_HOST_PTR(request));
    } else {
        async_safe_run_on_cpu(cs, ia64_cpu_request_mca_work,
                              RUN_ON_CPU_HOST_PTR(request));
    }
}

typedef struct IA64RasInitEntry {
    uint64_t entry;
    uint64_t gp;
} IA64RasInitEntry;

static void ia64_cpu_set_init_entry_work(CPUState *cs, run_on_cpu_data data)
{
    IA64RasInitEntry *registration = data.host_ptr;
    CPUIA64State *env = cpu_env(cs);

    env->pal.pal_init_entry = registration->entry;
    env->pal.pal_init_gp = registration->gp;
    g_free(registration);
}

void ia64_cpu_set_init_entry(CPUState *cs, uint64_t entry, uint64_t gp)
{
    IA64RasInitEntry *registration;

    if (!cs) {
        return;
    }
    registration = g_new(IA64RasInitEntry, 1);
    registration->entry = entry;
    registration->gp = gp;
    if (qemu_cpu_is_self(cs)) {
        ia64_cpu_set_init_entry_work(
            cs, RUN_ON_CPU_HOST_PTR(registration));
    } else {
        async_safe_run_on_cpu(cs, ia64_cpu_set_init_entry_work,
                              RUN_ON_CPU_HOST_PTR(registration));
    }
}
