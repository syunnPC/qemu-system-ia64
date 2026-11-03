/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Validation of IA-64 RSE state received through migration.
 */

#include "qemu/osdep.h"
#include "rse-migration.h"

typedef struct IA64RSEMigrationView {
    uint64_t bsp;
    uint64_t bspstore;
    uint64_t rnat;
    uint64_t rnat_addr;
    uint64_t rnat_defined;
    uint64_t load_rnat;
    uint64_t load_rnat_addr;
    uint64_t load_rnat_defined;
    const IA64RnatWritebackImage *writeback_rnat;
    const IA64RnatShadowEntry *rnat_shadow;
    const uint64_t *pgr_nat;
    const uint64_t *gr_dirty;
    uint32_t bol;
    int32_t dirty;
    int32_t dirty_nat;
    int32_t clean;
    int32_t clean_nat;
    int32_t invalid;
    uint8_t rnat_shadow_count;
    uint8_t cfm_sof;
    uint8_t cfm_sol;
    uint8_t cfm_sor;
    uint8_t cfm_rrb_gr;
    uint8_t cfm_rrb_fr;
    uint8_t cfm_rrb_pr;
    bool load_rnat_valid;
    bool completion_pending;
    uint64_t completion_source_ip;
    uint8_t completion_source_slot;
} IA64RSEMigrationView;

static bool ia64_rnat_collection_address_valid(uint64_t addr)
{
    return (addr & 0x1ff) == 0x1f8;
}

static bool ia64_rnat_writeback_image_valid(const IA64RnatWritebackImage *image)
{
    if (!image->valid) {
        return image->value == 0 && image->addr == 0 && image->defined == 0;
    }

    return image->defined != 0 &&
           ((image->value | image->defined) & ~INT64_MAX) == 0 &&
           (image->value & ~image->defined) == 0 &&
           ia64_rnat_collection_address_valid(image->addr);
}

static bool ia64_rse_migration_view_valid(const IA64RSEMigrationView *state,
                                          bool has_clean_partition)
{
    int64_t partition_total;
    int64_t dirty_words;
    uint64_t expected_bspstore;
    unsigned int i;
    unsigned int j;

    if (!ia64_cfm_frame_fields_valid(state->cfm_sof, state->cfm_sol,
                                     state->cfm_sor) ||
        state->bol >= IA64_STACKED_GR_COUNT ||
        state->dirty < -(int32_t)IA64_STACKED_GR_COUNT ||
        state->dirty > IA64_STACKED_GR_COUNT || state->clean < 0 ||
        state->clean > IA64_STACKED_GR_COUNT || state->invalid < 0 ||
        state->invalid > IA64_STACKED_GR_COUNT || state->dirty_nat < -2 ||
        state->dirty_nat > 2 || state->clean_nat < 0 || state->clean_nat > 2 ||
        (state->bsp & 7) != 0 || (state->bspstore & 7) != 0) {
        return false;
    }

    if (state->cfm_sor != 0) {
        if (state->cfm_rrb_gr >= ((uint32_t)state->cfm_sor << 3)) {
            return false;
        }
    } else if (state->cfm_rrb_gr != 0) {
        return false;
    }
    if (state->cfm_rrb_fr >= IA64_FR_COUNT - 32 ||
        state->cfm_rrb_pr >= IA64_PR_COUNT - IA64_PR_ROTATING_BASE) {
        return false;
    }

    /* The second words describe physical stacked registers 64 through 95. */
    if ((state->pgr_nat[1] | state->gr_dirty[1]) & ~(uint64_t)UINT32_MAX) {
        return false;
    }

    partition_total =
        (int64_t)state->cfm_sof + state->dirty + state->clean + state->invalid;
    if (partition_total != IA64_STACKED_GR_COUNT) {
        return false;
    }
    dirty_words = (int64_t)state->dirty + state->dirty_nat;
    if (dirty_words >= 0) {
        expected_bspstore = state->bsp - (uint64_t)dirty_words * 8;
    } else {
        expected_bspstore = state->bsp + (uint64_t)-dirty_words * 8;
    }
    if (state->bspstore != expected_bspstore) {
        return false;
    }
    if (!has_clean_partition && (state->clean != 0 || state->clean_nat != 0)) {
        return false;
    }

    if (state->dirty >= 0) {
        int32_t expected_dirty_nat = (int32_t)((int64_t)(state->bsp >> 9) -
                                               (int64_t)(state->bspstore >> 9));

        if (state->dirty_nat != expected_dirty_nat) {
            return false;
        }
        uint64_t bspload =
            state->bspstore - (uint64_t)(state->clean + state->clean_nat) * 8;
        int32_t expected_clean_nat = (int32_t)((int64_t)(state->bspstore >> 9) -
                                               (int64_t)(bspload >> 9));

        if (state->clean_nat != expected_clean_nat) {
            return false;
        }
    }

    if ((state->rnat | state->rnat_defined | state->load_rnat |
         state->load_rnat_defined) &
        ~INT64_MAX) {
        return false;
    }
    if ((state->rnat & ~state->rnat_defined) != 0 ||
        (state->load_rnat & ~state->load_rnat_defined) != 0) {
        return false;
    }
    if (state->rnat_addr == UINT64_MAX) {
        if (state->rnat != 0 || state->rnat_defined != 0) {
            return false;
        }
    } else if (!ia64_rnat_collection_address_valid(state->rnat_addr)) {
        return false;
    }
    if (state->load_rnat_valid) {
        if (!ia64_rnat_collection_address_valid(state->load_rnat_addr)) {
            return false;
        }
    } else if (state->load_rnat != 0 || state->load_rnat_addr != 0 ||
               state->load_rnat_defined != 0) {
        return false;
    }
    if (!ia64_rnat_writeback_image_valid(state->writeback_rnat) ||
        state->rnat_shadow_count > IA64_RSE_RNAT_SHADOW_COUNT) {
        return false;
    }

    for (i = 0; i < IA64_RSE_RNAT_SHADOW_COUNT; i++) {
        const IA64RnatShadowEntry *entry = &state->rnat_shadow[i];

        if (i >= state->rnat_shadow_count) {
            if (entry->valid || entry->value != 0 || entry->addr != 0 ||
                entry->defined != 0) {
                return false;
            }
            continue;
        }
        if (!entry->valid || entry->defined == 0 ||
            ((entry->value | entry->defined) & ~INT64_MAX) != 0 ||
            (entry->value & ~entry->defined) != 0 ||
            !ia64_rnat_collection_address_valid(entry->addr)) {
            return false;
        }
        for (j = i + 1; j < state->rnat_shadow_count; j++) {
            if (state->rnat_shadow[j].addr == entry->addr) {
                return false;
            }
        }
    }

    if (state->completion_pending &&
        (state->completion_source_slot > 2 ||
         (state->completion_source_ip & ~IA64_IP_BUNDLE_MASK) != 0)) {
        return false;
    }
    return true;
}

bool ia64_rse_migration_state_valid(const CPUIA64State *env,
                                    bool has_clean_partition)
{
    const IA64RSEState *rse = &env->rse;
    const IA64RSEMigrationView state = {
        .bsp = env->ar_bsp,
        .bspstore = env->ar_bspstore,
        .rnat = env->ar_rnat,
        .rnat_addr = rse->rse_rnat_addr,
        .rnat_defined = rse->rse_rnat_defined,
        .load_rnat = rse->rse_load_rnat,
        .load_rnat_addr = rse->rse_load_rnat_addr,
        .load_rnat_defined = rse->rse_load_rnat_defined,
        .writeback_rnat = &rse->rse_writeback_rnat,
        .rnat_shadow = rse->rse_rnat_shadow,
        .pgr_nat = rse->rse_pgr_nat,
        .gr_dirty = rse->rse_gr_dirty,
        .bol = rse->rse_bol,
        .dirty = rse->rse_dirty,
        .dirty_nat = rse->rse_dirty_nat,
        .clean = rse->rse_clean,
        .clean_nat = rse->rse_clean_nat,
        .invalid = rse->rse_invalid,
        .rnat_shadow_count = rse->rse_rnat_shadow_count,
        .cfm_sof = env->cfm_sof,
        .cfm_sol = env->cfm_sol,
        .cfm_sor = env->cfm_sor,
        .cfm_rrb_gr = env->cfm_rrb_gr,
        .cfm_rrb_fr = env->cfm_rrb_fr,
        .cfm_rrb_pr = env->cfm_rrb_pr,
        .load_rnat_valid = rse->rse_load_rnat_valid,
        .completion_pending = rse->rse_completion_pending,
        .completion_source_ip = rse->rse_completion_source_ip,
        .completion_source_slot = rse->rse_completion_source_slot,
    };

    return ia64_rse_migration_view_valid(&state, has_clean_partition);
}

bool ia64_firmware_debug_rse_migration_state_valid(
    const IA64FirmwareDebugRseState *rse, bool has_clean_partition)
{
    const IA64RSEMigrationView state = {
        .bsp = rse->bsp,
        .bspstore = rse->bspstore,
        .rnat = rse->rnat,
        .rnat_addr = rse->rnat_addr,
        .rnat_defined = rse->rnat_defined,
        .load_rnat = rse->load_rnat,
        .load_rnat_addr = rse->load_rnat_addr,
        .load_rnat_defined = rse->load_rnat_defined,
        .writeback_rnat = &rse->writeback_rnat,
        .rnat_shadow = rse->rnat_shadow,
        .pgr_nat = rse->pgr_nat,
        .gr_dirty = rse->gr_dirty,
        .bol = rse->bol,
        .dirty = rse->dirty,
        .dirty_nat = rse->dirty_nat,
        .clean = rse->clean,
        .clean_nat = rse->clean_nat,
        .invalid = rse->invalid,
        .rnat_shadow_count = rse->rnat_shadow_count,
        .cfm_sof = rse->cfm_sof,
        .cfm_sol = rse->cfm_sol,
        .cfm_sor = rse->cfm_sor,
        .cfm_rrb_gr = rse->cfm_rrb_gr,
        .cfm_rrb_fr = rse->cfm_rrb_fr,
        .cfm_rrb_pr = rse->cfm_rrb_pr,
        .load_rnat_valid = rse->load_rnat_valid,
        .completion_pending = rse->completion_pending,
        .completion_source_ip = rse->completion_source_ip,
        .completion_source_slot = rse->completion_source_slot,
    };

    return ia64_rse_migration_view_valid(&state, has_clean_partition);
}
