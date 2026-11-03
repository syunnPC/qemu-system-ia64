/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Pure unit tests for IA-64 RSE migration-state validation.
 */

#include "qemu/osdep.h"
#include "target/ia64/rse-migration.h"

typedef const char *(*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

static char failure[192];

static void init_reset_state(CPUIA64State *env)
{
    memset(env, 0, sizeof(*env));
    env->cfm_sof = IA64_STACKED_GR_COUNT;
    env->ar_bsp = 0x1000;
    env->ar_bspstore = 0x1000;
    env->rse.rse_rnat_addr = UINT64_MAX;
}

static void init_boot_handoff_state(CPUIA64State *env)
{
    init_reset_state(env);
    env->cfm_sof = 0;
    env->rse.rse_invalid = IA64_STACKED_GR_COUNT;
}

static void init_dirty_state(CPUIA64State *env)
{
    init_reset_state(env);
    env->cfm_sof = 16;
    env->rse.rse_bol = 63;
    env->rse.rse_dirty = 63;
    env->rse.rse_dirty_nat = 1;
    env->rse.rse_invalid = 17;
    env->ar_bsp = 0x1200;
    env->ar_bspstore = 0x1000;
}

static void init_clean_state(CPUIA64State *env)
{
    init_reset_state(env);
    env->cfm_sof = 16;
    env->rse.rse_clean = 63;
    env->rse.rse_clean_nat = 1;
    env->rse.rse_invalid = 17;
    env->ar_bsp = 0x1200;
    env->ar_bspstore = 0x1200;
}

static void init_firmware_state(IA64FirmwareDebugRseState *state)
{
    memset(state, 0, sizeof(*state));
    state->cfm_sof = IA64_STACKED_GR_COUNT;
    state->bsp = 0x1000;
    state->bspstore = 0x1000;
    state->rnat_addr = UINT64_MAX;
}

static const char *expect_live(const char *label, const CPUIA64State *env,
                               bool has_clean_partition, bool expected)
{
    bool actual = ia64_rse_migration_state_valid(env, has_clean_partition);

    if (actual == expected) {
        return NULL;
    }
    snprintf(failure, sizeof(failure), "%s: expected %s state", label,
             expected ? "valid" : "invalid");
    return failure;
}

static const char *expect_firmware(const char *label,
                                   const IA64FirmwareDebugRseState *state,
                                   bool has_clean_partition, bool expected)
{
    bool actual = ia64_firmware_debug_rse_migration_state_valid(
        state, has_clean_partition);

    if (actual == expected) {
        return NULL;
    }
    snprintf(failure, sizeof(failure), "%s: expected %s state", label,
             expected ? "valid" : "invalid");
    return failure;
}

#define EXPECT_LIVE(label, clean, expected) do {                         \
    const char *error_ = expect_live(label, &env, clean, expected);       \
    if (error_ != NULL) {                                                 \
        return error_;                                                    \
    }                                                                     \
} while (0)

#define RESET_AND_REJECT(label, mutation) do {                            \
    init_reset_state(&env);                                               \
    mutation;                                                             \
    EXPECT_LIVE(label, true, false);                                      \
} while (0)

static const char *test_valid_partition_states(void)
{
    CPUIA64State env;

    init_reset_state(&env);
    EXPECT_LIVE("architectural reset", false, true);

    init_boot_handoff_state(&env);
    EXPECT_LIVE("synthetic boot handoff", false, true);

    init_dirty_state(&env);
    EXPECT_LIVE("complete dirty partition", false, true);

    init_clean_state(&env);
    EXPECT_LIVE("complete clean partition", true, true);

    init_reset_state(&env);
    env.rse.rse_dirty = -2;
    env.rse.rse_dirty_nat = -1;
    env.rse.rse_invalid = 2;
    env.ar_bspstore = env.ar_bsp + 24;
    env.rse.rse_completion_pending = true;
    env.rse.rse_completion_source_ip = 0x2000;
    env.rse.rse_completion_source_slot = 2;
    EXPECT_LIVE("incomplete br.ret frame", false, true);

    /* Completion metadata is permitted to remain stale after retirement. */
    env.rse.rse_completion_pending = false;
    env.rse.rse_completion_source_ip = 0x2001;
    env.rse.rse_completion_source_slot = 3;
    EXPECT_LIVE("retired completion metadata", false, true);
    return NULL;
}

static const char *test_frame_and_rotation_rejection(void)
{
    CPUIA64State env;

    RESET_AND_REJECT("SOF overflow", env.cfm_sof = 97);

    init_boot_handoff_state(&env);
    env.cfm_sol = 1;
    EXPECT_LIVE("SOL exceeds SOF", true, false);

    RESET_AND_REJECT("SOR exceeds SOF", env.cfm_sor = 13);
    RESET_AND_REJECT("RRB.GR without rotating region", env.cfm_rrb_gr = 1);

    init_reset_state(&env);
    env.cfm_sor = 1;
    env.cfm_rrb_gr = 8;
    EXPECT_LIVE("RRB.GR exceeds rotating region", true, false);

    RESET_AND_REJECT("RRB.FR overflow", env.cfm_rrb_fr = 96);
    RESET_AND_REJECT("RRB.PR overflow", env.cfm_rrb_pr = 48);
    RESET_AND_REJECT("BOL overflow", env.rse.rse_bol = IA64_STACKED_GR_COUNT);
    RESET_AND_REJECT("partition total", env.rse.rse_invalid = 1);
    RESET_AND_REJECT("negative clean partition", env.rse.rse_clean = -1);
    RESET_AND_REJECT("physical NaT high bits",
                     env.rse.rse_pgr_nat[1] = 1ULL << 32);
    RESET_AND_REJECT("dirty-view high bits",
                     env.rse.rse_gr_dirty[1] = 1ULL << 32);
    return NULL;
}

static const char *test_backing_store_rejection(void)
{
    CPUIA64State env;

    init_dirty_state(&env);
    env.ar_bspstore += 8;
    EXPECT_LIVE("BSPSTORE relation", true, false);

    init_dirty_state(&env);
    env.ar_bsp |= 1;
    EXPECT_LIVE("BSP alignment", true, false);

    init_dirty_state(&env);
    env.ar_bspstore |= 1;
    EXPECT_LIVE("BSPSTORE alignment", true, false);

    init_reset_state(&env);
    env.cfm_sof = 16;
    env.rse.rse_dirty = 64;
    env.rse.rse_dirty_nat = 0;
    env.rse.rse_invalid = 16;
    env.ar_bsp = 0x1200;
    env.ar_bspstore = 0x1000;
    EXPECT_LIVE("dirty RNAT count", true, false);

    init_reset_state(&env);
    env.cfm_sof = 16;
    env.rse.rse_clean = 64;
    env.rse.rse_invalid = 16;
    env.ar_bsp = 0x1200;
    env.ar_bspstore = 0x1200;
    EXPECT_LIVE("clean RNAT count", true, false);

    init_clean_state(&env);
    EXPECT_LIVE("unsupported clean partition", false, false);

    RESET_AND_REJECT("dirty RNAT bound", env.rse.rse_dirty_nat = 3);
    RESET_AND_REJECT("clean RNAT bound", env.rse.rse_clean_nat = 3);
    return NULL;
}

static const char *test_rnat_latches(void)
{
    CPUIA64State env;

    init_reset_state(&env);
    env.rse.rse_rnat_addr = 0x11f8;
    env.rse.rse_rnat_defined = 3;
    env.ar_rnat = 1;
    env.rse.rse_load_rnat_valid = true;
    env.rse.rse_load_rnat_addr = 0x13f8;
    env.rse.rse_load_rnat_defined = 6;
    env.rse.rse_load_rnat = 2;
    env.rse.rse_writeback_rnat.valid = true;
    env.rse.rse_writeback_rnat.addr = 0x15f8;
    env.rse.rse_writeback_rnat.defined = 4;
    env.rse.rse_writeback_rnat.value = 4;
    env.rse.rse_rnat_shadow_count = 2;
    env.rse.rse_rnat_shadow[0].valid = true;
    env.rse.rse_rnat_shadow[0].addr = 0x17f8;
    env.rse.rse_rnat_shadow[0].defined = 1;
    env.rse.rse_rnat_shadow[0].value = 1;
    env.rse.rse_rnat_shadow[1].valid = true;
    env.rse.rse_rnat_shadow[1].addr = 0x19f8;
    env.rse.rse_rnat_shadow[1].defined = 2;
    EXPECT_LIVE("complete RNAT latch image", true, true);

    env.rse.rse_rnat_shadow[0].addr = env.rse.rse_rnat_addr;
    EXPECT_LIVE("dispersal shadow below active spill latch", true, true);
    env.rse.rse_rnat_shadow[0].addr = 0x17f8;

    env.ar_rnat |= 4;
    EXPECT_LIVE("RNAT value outside defined mask", true, false);
    env.ar_rnat = 1;

    env.rse.rse_load_rnat |= 8;
    EXPECT_LIVE("load RNAT outside defined mask", true, false);
    env.rse.rse_load_rnat = 2;

    env.rse.rse_writeback_rnat.value |= 8;
    EXPECT_LIVE("writeback RNAT outside defined mask", true, false);
    env.rse.rse_writeback_rnat.value = 4;

    env.rse.rse_rnat_shadow[1].addr = 0x17f8;
    EXPECT_LIVE("duplicate RNAT shadow", true, false);
    env.rse.rse_rnat_shadow[1].addr = 0x19f8;

    env.rse.rse_rnat_shadow[2].valid = true;
    EXPECT_LIVE("nonzero RNAT shadow tail", true, false);

    init_reset_state(&env);
    env.rse.rse_rnat_defined = 1;
    EXPECT_LIVE("detached RNAT residue", true, false);

    init_reset_state(&env);
    env.rse.rse_rnat_addr = 0x11f8;
    env.rse.rse_rnat_defined = 1ULL << 63;
    EXPECT_LIVE("RNAT bit 63", true, false);

    init_reset_state(&env);
    env.rse.rse_rnat_addr = 0x1100;
    EXPECT_LIVE("misaligned RNAT collection", true, false);

    init_reset_state(&env);
    env.rse.rse_load_rnat = 1;
    EXPECT_LIVE("invalid load-latch residue", true, false);

    init_reset_state(&env);
    env.rse.rse_writeback_rnat.valid = true;
    env.rse.rse_writeback_rnat.addr = 0x11f8;
    EXPECT_LIVE("empty valid writeback", true, false);

    init_reset_state(&env);
    env.rse.rse_rnat_shadow_count = IA64_RSE_RNAT_SHADOW_COUNT + 1;
    EXPECT_LIVE("RNAT shadow count overflow", true, false);
    return NULL;
}

static const char *test_completion_and_firmware_state(void)
{
    CPUIA64State env;
    IA64FirmwareDebugRseState firmware;
    const char *error;

    init_reset_state(&env);
    env.rse.rse_completion_pending = true;
    env.rse.rse_completion_source_ip = 0x2000;
    env.rse.rse_completion_source_slot = 3;
    EXPECT_LIVE("completion source slot", true, false);

    env.rse.rse_completion_source_slot = 2;
    env.rse.rse_completion_source_ip = 0x2001;
    EXPECT_LIVE("completion source IP", true, false);

    init_firmware_state(&firmware);
    error = expect_firmware("firmware reset snapshot", &firmware, false, true);
    if (error != NULL) {
        return error;
    }

    firmware.cfm_sof = 97;
    error = expect_firmware("firmware SOF overflow", &firmware, false, false);
    if (error != NULL) {
        return error;
    }

    init_firmware_state(&firmware);
    firmware.rnat_shadow_count = 1;
    firmware.rnat_shadow[0].valid = true;
    firmware.rnat_shadow[0].addr = 0x11f8;
    firmware.rnat_shadow[0].defined = 1;
    error = expect_firmware("firmware RNAT snapshot", &firmware, false, true);
    if (error != NULL) {
        return error;
    }
    firmware.rnat_shadow[0].value = 2;
    return expect_firmware("firmware corrupt RNAT snapshot", &firmware, false,
                           false);
}

int main(void)
{
    static const TestCase tests[] = {
        { "valid partition states", test_valid_partition_states },
        { "frame and rotation rejection", test_frame_and_rotation_rejection },
        { "backing-store rejection", test_backing_store_rejection },
        { "RNAT latch validation", test_rnat_latches },
        { "completion and firmware state", test_completion_and_firmware_state },
    };
    unsigned int i;
    int status = 0;

    printf("TAP version 13\n");
    printf("1..%zu\n", G_N_ELEMENTS(tests));
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const char *error = tests[i].fn();

        if (error == NULL) {
            printf("ok %u - %s\n", i + 1, tests[i].name);
        } else {
            status = 1;
            printf("not ok %u - %s\n", i + 1, tests[i].name);
            printf("# %s\n", error);
        }
    }
    return status;
}
