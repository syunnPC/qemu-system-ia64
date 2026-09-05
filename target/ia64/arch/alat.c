/*
 * IA-64 Advanced Load Address Table architecture operations.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "system/physmem.h"
#include "arch/arch.h"

#define IA64_ROTATING_FR_BASE 32

static void ia64_alat_invalidate_entry(CPUIA64State *env,
                                       IA64AlatEntry *entry)
{
    if (!entry->valid) {
        return;
    }

    entry->valid = false;
    if (env->alat_state.alat_active_count > 0) {
        env->alat_state.alat_active_count--;
    }
}

static void ia64_alat_clear(CPUIA64State *env)
{
    int i;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }
    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        env->alat_state.alat[i].valid = false;
    }
    env->alat_state.alat_active_count = 0;
}

/*
 * ALAT loads/checks sample each vCPU's store sequence.  The CPU set is fixed
 * during execution and the sequences survive resets, so their sum changes
 * on every write until 64-bit wraparound.  Migration discards ALAT entries;
 * these host-local sequences are not migrated.
 */
static uint64_t ia64_alat_cpu_generation(bool *active)
{
    CPUState *cs;
    uint64_t generation = 0;

    CPU_FOREACH(cs) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
        uint64_t sequence = qatomic_load_acquire(&cpu->alat_write_sequence);

        generation += sequence;
        if (active) {
            *active |= sequence & 1;
        }
    }
    return generation;
}

static uint64_t ia64_alat_memory_generation(void)
{
    return physical_memory_write_generation() +
           ia64_alat_cpu_generation(NULL);
}

static bool ia64_alat_memory_generation_changed(uint64_t generation)
{
    uint64_t external;
    uint64_t current;
    bool active = false;

    /* Keep the preceding advanced load before its closing sequence sample. */
    smp_rmb();
    external = physical_memory_write_generation();
    current = external + ia64_alat_cpu_generation(&active);
    return active || current != generation ||
           physical_memory_write_generation_changed(external);
}

/* Own stores use PA overlap; other RAM-write generations clear the ALAT. */
static bool ia64_alat_sync_memory_writes(CPUIA64State *env)
{
    uint64_t generation = ia64_alat_memory_generation();

    if (generation != env->alat_state.memory_write_generation) {
        ia64_alat_clear(env);
        env->alat_state.memory_write_generation = generation;
        return true;
    }
    return false;
}

static bool ia64_alat_memory_window_changed(CPUIA64State *env,
                                             uint64_t generation)
{
    if (!ia64_alat_memory_generation_changed(generation)) {
        return false;
    }

    ia64_alat_clear(env);
    env->alat_state.memory_write_generation =
        ia64_alat_memory_generation();
    return true;
}

void ia64_invalidate_alat_reg_range(CPUIA64State *env,
                                    uint32_t first, uint32_t last,
                                    bool fp)
{
    uint32_t i;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            env->alat_state.alat[i].fp == fp &&
            env->alat_state.alat[i].reg >= first &&
            env->alat_state.alat[i].reg < last) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
    }
}

static bool ia64_ranges_overlap(uint64_t start, uint64_t size,
                                uint64_t other_start, uint64_t other_size)
{
    uint64_t end;
    uint64_t other_end;

    if (size == 0 || other_size == 0) {
        return false;
    }

    end = start + size - 1;
    if (end < start) {
        end = UINT64_MAX;
    }
    other_end = other_start + other_size - 1;
    if (other_end < other_start) {
        other_end = UINT64_MAX;
    }

    return start <= other_end && other_start <= end;
}

void ia64_invalidate_alat_phys_range(CPUIA64State *env,
                                     uint64_t pa, uint64_t size)
{
    uint32_t i;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }
    if (ia64_alat_sync_memory_writes(env)) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            ia64_ranges_overlap(pa, size, env->alat_state.alat[i].phys_addr,
                                env->alat_state.alat[i].size)) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
    }
}

void ia64_invalidate_stacked_alat(CPUIA64State *env)
{
    ia64_invalidate_alat_reg_range(env, IA64_STACKED_GR_BASE, IA64_GR_COUNT,
                                   false);
}

void ia64_invalidate_rotating_fp_alat(CPUIA64State *env)
{
    ia64_invalidate_alat_reg_range(env, IA64_ROTATING_FR_BASE, IA64_FR_COUNT,
                                   true);
}

/* ---- Advanced Load Address Table check ---- */

uint64_t ia64_alat_chk_a(CPUIA64State *env, uint64_t va, uint32_t reg)
{
    bool found = false;
    uint64_t generation = 0;
    int i;

    if (env->alat_state.alat_active_count != 0 &&
        !ia64_alat_sync_memory_writes(env)) {
        generation = env->alat_state.memory_write_generation;
        for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
            if (env->alat_state.alat[i].valid &&
                env->alat_state.alat[i].reg == reg) {
                found = true;
                break;
            }
        }
    }
    if (found && !ia64_alat_memory_window_changed(env, generation)) {
        return 0;
    }
    env->cr_ifa = va;
    CPUState *cs = env_cpu(env);
    cs->exception_index = IA64_EXCP_GENERAL;
    cpu_loop_exit(cs);
    return 1;
}

void ia64_alat_invala(CPUIA64State *env)
{
    ia64_alat_clear(env);
    env->alat_state.memory_write_generation =
        ia64_alat_memory_generation();
}

uint64_t ia64_alat_load_begin(CPUIA64State *env)
{
    ia64_alat_sync_memory_writes(env);
    return env->alat_state.memory_write_generation;
}

static void ia64_set_alat(CPUIA64State *env, uint32_t reg, uint64_t addr,
                          uint32_t size, bool fp, uint64_t generation)
{
    uint64_t pa;
    IA64MemorySpeculation spec;
    int free_index = -1;
    int match_index = -1;
    int i;

    /* Reject allocation if RAM changed after the load-generation sample. */
    if (ia64_alat_memory_window_changed(env, generation)) {
        return;
    }

    if (!ia64_data_address_to_phys_attr(env, addr, &pa, &spec) ||
        !ia64_memory_allows_advanced_load(spec)) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (!env->alat_state.alat[i].valid) {
            if (free_index < 0) {
                free_index = i;
            }
        } else if (env->alat_state.alat[i].reg == reg &&
                   env->alat_state.alat[i].fp == fp) {
            if (match_index < 0) {
                match_index = i;
            } else {
                /* A register can name at most one ALAT entry. */
                ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
            }
        }
    }

    i = match_index >= 0 ? match_index : free_index;
    if (i < 0) {
        return;
    }
    if (!env->alat_state.alat[i].valid) {
        env->alat_state.alat_active_count++;
    }
    env->alat_state.alat[i].phys_addr = pa;
    env->alat_state.alat[i].size = size;
    env->alat_state.alat[i].reg = reg;
    env->alat_state.alat[i].fp = fp;
    env->alat_state.alat[i].valid = true;

    /* Close the window around address translation and entry publication. */
    ia64_alat_memory_window_changed(env, generation);
}

void ia64_alat_set(CPUIA64State *env, uint32_t reg, uint64_t addr,
                   uint32_t size, uint64_t generation)
{
    ia64_set_alat(env, reg, addr, size, false, generation);
}

void ia64_alat_set_fp(CPUIA64State *env, uint32_t reg, uint64_t addr,
                      uint32_t size, uint64_t generation)
{
    if (reg > 1) {
        ia64_set_alat(env, reg, addr, size, true, generation);
    }
}

static int ia64_find_alat_reg(CPUIA64State *env, uint32_t reg, bool fp,
                              uint64_t *generation)
{
    int found = -1;
    int i;

    if (env->alat_state.alat_active_count == 0) {
        return -1;
    }
    if (ia64_alat_sync_memory_writes(env)) {
        return -1;
    }
    *generation = env->alat_state.memory_write_generation;

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            env->alat_state.alat[i].reg == reg &&
            env->alat_state.alat[i].fp == fp) {
            found = i;
            break;
        }
    }

    if (found >= 0 &&
        ia64_alat_memory_window_changed(env, *generation)) {
        return -1;
    }
    return found;
}

static bool ia64_alat_matches_addr(CPUIA64State *env,
                                   const IA64AlatEntry *entry,
                                   uint64_t addr, uint32_t size)
{
    uint64_t pa;

    if (entry->size != size) {
        return false;
    }
    if (!ia64_data_address_to_phys(env, addr, &pa)) {
        return false;
    }
    return entry->phys_addr == pa;
}

static uint64_t ia64_check_load_alat(CPUIA64State *env, uint32_t reg,
                                     bool fp, bool verify_addr,
                                     uint64_t addr, uint32_t size,
                                     uint32_t clear)
{
    uint64_t generation;
    int i;

    if (fp && reg <= 1) {
        return 0;
    }

    i = ia64_find_alat_reg(env, reg, fp, &generation);
    if (i < 0) {
        return 0;
    }
    if (verify_addr && !ia64_alat_matches_addr(env, &env->alat_state.alat[i],
                                               addr, size)) {
        if (clear) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
        return 0;
    }
    if (ia64_alat_memory_window_changed(env, generation)) {
        return 0;
    }
    if (clear) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
    return 1;
}

void ia64_alat_invalidate_reg(CPUIA64State *env, uint32_t reg)
{
    uint64_t generation;
    int i = ia64_find_alat_reg(env, reg, false, &generation);

    if (i >= 0) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
}

void ia64_alat_invalidate_fp_reg(CPUIA64State *env, uint32_t reg)
{
    uint64_t generation;
    int i = ia64_find_alat_reg(env, reg, true, &generation);

    if (i >= 0) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
}

uint64_t ia64_alat_check_load(CPUIA64State *env, uint32_t reg,
                                uint32_t clear)
{
    return ia64_check_load_alat(env, reg, false, false, 0, 0, clear);
}

uint64_t ia64_alat_check_load_addr(CPUIA64State *env, uint32_t reg,
                                     uint64_t addr, uint32_t size,
                                     uint32_t clear)
{
    return ia64_check_load_alat(env, reg, false, true, addr, size, clear);
}

uint64_t ia64_alat_check_load_fp(CPUIA64State *env, uint32_t reg,
                                   uint32_t clear)
{
    return ia64_check_load_alat(env, reg, true, false, 0, 0, clear);
}

uint64_t ia64_alat_check_load_fp_addr(CPUIA64State *env, uint32_t reg,
                                        uint64_t addr, uint32_t size,
                                        uint32_t clear)
{
    return ia64_check_load_alat(env, reg, true, true, addr, size, clear);
}

void ia64_alat_notify_store(CPUIA64State *env)
{
    if (!env->alat_state.alat_full) {
        return;
    }

    g_assert(!env->alat_state.write_active);
    physical_memory_write_generation_advance();
    ia64_alat_invala(env);
}

void ia64_alat_write_begin(CPUIA64State *env)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(env_cpu(env));

    g_assert(!env->alat_state.write_active);
    env->alat_state.write_generation =
        env->alat_state.memory_write_generation;
    env->alat_state.write_observed = env->alat_state.alat_full;
    env->alat_state.write_active = true;
    if (env->alat_state.write_observed) {
        uint64_t sequence = qatomic_read(&cpu->alat_write_sequence);

        g_assert(!(sequence & 1));
        qatomic_set(&cpu->alat_write_sequence, sequence + 1);
        /* Publish the odd sequence before the faultable RAM store. */
        smp_wmb();
    }
}

static bool ia64_alat_write_take_token(CPUIA64State *env)
{
    bool observed;

    g_assert(env->alat_state.write_active);
    observed = env->alat_state.write_observed;
    env->alat_state.write_active = false;
    env->alat_state.write_observed = false;
    if (observed) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(env_cpu(env));
        uint64_t sequence = qatomic_read(&cpu->alat_write_sequence);

        g_assert(sequence & 1);
        /* Publish completion only after the RAM store is visible. */
        qatomic_store_release(&cpu->alat_write_sequence, sequence + 1);
    }
    return observed;
}

static void ia64_alat_write_finish(CPUIA64State *env, uint64_t addr,
                                   uint32_t size, bool precise)
{
    uint64_t generation = env->alat_state.write_generation;
    bool observed = ia64_alat_write_take_token(env);
    uint64_t expected = generation + 2;

    if (observed && env->alat_state.alat_active_count != 0) {
        if (!precise ||
            env->alat_state.memory_write_generation != generation ||
            ia64_alat_memory_generation_changed(expected)) {
            ia64_alat_invala(env);
            return;
        }

        /* The expected generation includes this local store. */
        env->alat_state.memory_write_generation = expected;
        ia64_invalidate_alat_store(env, addr, size);
    }
}

void ia64_alat_write_end(CPUIA64State *env, uint64_t addr, uint32_t size)
{
    ia64_alat_write_finish(env, addr, size, true);
}

void ia64_alat_write_cancel(CPUIA64State *env)
{
    if (ia64_alat_write_take_token(env)) {
        /* No RAM changed, so retain entries while accounting for our scope. */
        env->alat_state.memory_write_generation += 2;
    }
}

void ia64_alat_write_abort(CPUIA64State *env)
{
    if (env->alat_state.write_active) {
        /* Faulted stores clear the local ALAT. */
        ia64_alat_write_finish(env, 0, 0, false);
    }
}

void ia64_invalidate_alat_store(CPUIA64State *env, uint64_t addr,
                                uint32_t size)
{
    uint64_t pa;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }
    if (!ia64_data_address_to_phys(env, addr, &pa)) {
        return;
    }

    ia64_invalidate_alat_phys_range(env, pa, size);
}
