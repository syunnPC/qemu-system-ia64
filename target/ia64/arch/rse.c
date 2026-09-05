/*
 * IA-64 Register Stack Engine and rotating-register architecture logic.
 *
 * The state in IA64RSEState is architected unless its field comment marks it
 * as a derived physical/virtual view.  Helper ABI adapters live in helper/.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/log.h"
#include "cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "ia32/ia32.h"
#include "exec-access.h"
#include "exec/target_page.h"
#include "trace.h"

#define IA64_ROTATING_FR_COUNT (IA64_FR_COUNT - 32)

#define IA64_TRACE_RSE_STATE(env, operation) do { \
    trace_ia64_rse_state(env_cpu(env)->cpu_index, operation, \
                         (env)->ar_bsp, (env)->ar_bspstore, \
                         (env)->rse.rse_dirty, (env)->rse.rse_dirty_nat, \
                         (env)->rse.rse_clean, (env)->rse.rse_clean_nat, \
                         (env)->rse.rse_invalid, (env)->cfm_sof); \
    trace_ia64_rse_rnat_state(env_cpu(env)->cpu_index, operation, \
                              (env)->ar_rnat, \
                              (env)->rse.rse_rnat_addr, \
                              (env)->rse.rse_rnat_defined, \
                              (env)->rse.rse_load_rnat, \
                              (env)->rse.rse_load_rnat_addr, \
                              (env)->rse.rse_load_rnat_defined, \
                              (env)->rse.rse_load_rnat_valid); \
} while (0)

static bool ia64_rse_has_clean_partition(CPUIA64State *env)
{
    return ia64_env_cpu_class(env)->rse_has_clean_partition;
}

static int ia64_rse_mmu_index(CPUIA64State *env)
{
    return env->psr & IA64_PSR_RT ? MMU_IDX_RSE : MMU_PHYS_IDX;
}

static void ia64_rse_access_begin(CPUIA64State *env)
{
    env->rse.rse_access = true;
}

static void ia64_rse_access_end(CPUIA64State *env)
{
    env->rse.rse_access = false;
}

static void ia64_rse_check_data_debug(CPUIA64State *env, uint64_t addr,
                                      bool is_write)
{
    ia64_mmu_check_data_debug(
        env, addr, 8, is_write ? IA64_ISR_W : IA64_ISR_R,
        ia64_rsc_pl(env->ar_rsc), true, ia64_ip_bundle_addr(env->ip),
        (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT);
}

static void ia64_rse_complete_memory_reference(CPUIA64State *env)
{
    bool data_access_suppressed = env->psr & IA64_PSR_DA;

    env->psr &= ~(IA64_PSR_DA | IA64_PSR_DD);
    if (data_access_suppressed) {
        ia64_flush_suppressed_tlb(env);
    }
}

/*
 * RSE backing-store accesses.  The retaddr is threaded from the helper
 * entry point: a non-zero value unwinds to the issuing instruction
 * (alloc/flushrs/loadrs mandatory operations fault on the issuing
 * instruction), while 0 delivers the fault with the already-committed
 * env state (br.ret/rfi mandatory loads fault on the target
 * instruction, SDM Vol.2 6.6).
 */
static void ia64_rse_write_u64(CPUIA64State *env, uint64_t addr,
                               uint64_t value, uintptr_t ra)
{
    int mmu_idx = ia64_rse_mmu_index(env);

    ia64_rse_check_data_debug(env, addr, true);
    ia64_rse_access_begin(env);
    ia64_exec_store_mmuidx(env, addr, value, 8,
                           (env->ar_rsc & IA64_RSC_BE) != 0, mmu_idx, ra);
    ia64_rse_access_end(env);
    ia64_rse_complete_memory_reference(env);
}

static uint64_t ia64_rse_write_collection(CPUIA64State *env, uint64_t addr,
                                          uint64_t value, uint64_t defined,
                                          uint64_t *previous, uintptr_t ra)
{
    int mmu_idx = ia64_rse_mmu_index(env);
    uint64_t stored;

    ia64_rse_check_data_debug(env, addr, true);
    ia64_rse_access_begin(env);
    stored = ia64_exec_rse_store_collection(
        env, addr, value, defined, (env->ar_rsc & IA64_RSC_BE) != 0,
        mmu_idx, previous, ra);
    ia64_rse_access_end(env);
    ia64_rse_complete_memory_reference(env);
    return stored;
}

static uint64_t ia64_rse_read_u64(CPUIA64State *env, uint64_t addr,
                                  uintptr_t ra)
{
    int mmu_idx = ia64_rse_mmu_index(env);
    uint64_t value;

    ia64_rse_check_data_debug(env, addr, false);
    ia64_rse_access_begin(env);
    value = ia64_exec_load_mmuidx(env, addr, 8,
                                  (env->ar_rsc & IA64_RSC_BE) != 0,
                                  mmu_idx, ra);
    ia64_rse_access_end(env);
    ia64_rse_complete_memory_reference(env);
    return value;
}

uint64_t ia64_rse_current_cfm(const CPUIA64State *env)
{
    return env->cfm_sof
        | ((uint64_t)env->cfm_sol << IA64_CFM_SOL_SHIFT)
        | ((uint64_t)env->cfm_sor << IA64_CFM_SOR_SHIFT)
        | ((uint64_t)env->cfm_rrb_gr << IA64_CFM_RRB_GR_SHIFT)
        | ((uint64_t)env->cfm_rrb_fr << IA64_CFM_RRB_FR_SHIFT)
        | ((uint64_t)env->cfm_rrb_pr << IA64_CFM_RRB_PR_SHIFT);
}

static uint64_t ia64_rse_current_pfs(const CPUIA64State *env)
{
    return ia64_rse_current_cfm(env)
        | ((env->ar_ec & 0x3fULL) << IA64_PFS_PEC_SHIFT)
        | ((uint64_t)ia64_psr_cpl(env->psr) << IA64_PFS_PPL_SHIFT);
}

/*
 * ---- Register Stack Engine core ----
 *
 * Direct implementation of the architected register stack model of
 * SDM Vol.2 chapter 6: a circular physical stacked register file
 * partitioned into current frame, dirty, clean and invalid regions,
 * with the register stack backing store in guest memory as the
 * authoritative home of spilled values.  See cpu.h for the state
 * layout and partition invariants.
 */

static G_GNUC_NO_INLINE uint32_t ia64_rse_wrap_phys_slow(int32_t idx)
{
    idx %= (int32_t)IA64_STACKED_GR_COUNT;
    if (idx < 0) {
        idx += IA64_STACKED_GR_COUNT;
    }
    return idx;
}

static inline uint32_t ia64_rse_wrap_phys(int32_t idx)
{
    /*
     * Architected RSE movements are at most one physical-register window
     * in either direction.  Keep arbitrary implementation-state values
     * well defined, but avoid signed division on the normal path.
     */
    if (likely(idx >= -(int32_t)IA64_STACKED_GR_COUNT &&
               idx < 2 * (int32_t)IA64_STACKED_GR_COUNT)) {
        if (idx < 0) {
            idx += IA64_STACKED_GR_COUNT;
        } else if (idx >= IA64_STACKED_GR_COUNT) {
            idx -= IA64_STACKED_GR_COUNT;
        }
        return idx;
    }

    return ia64_rse_wrap_phys_slow(idx);
}

/* BSPSTORE{8:3}: the RNAT bit that collects the register spilled there. */
static inline uint32_t ia64_rse_collect_bit(uint64_t addr)
{
    return (addr >> 3) & 0x3f;
}

/* The NaT collection word that covers a backing-store address. */
static inline uint64_t ia64_rse_collect_word(uint64_t addr)
{
    return (addr & ~0x1ffULL) | (63ULL << 3);
}

/* Low 63 RNAT bits through and including bit. */
static inline uint64_t ia64_rse_rnat_low_mask(uint32_t bit)
{
    return bit >= 62 ? INT64_MAX : (1ULL << (bit + 1)) - 1;
}

static void ia64_rse_rnat_shadow_overlay(const CPUIA64State *env,
                                         uint64_t addr, uint64_t *value,
                                         uint64_t *defined);

static uint64_t ia64_rse_compose_rnat(const CPUIA64State *env,
                                      uint64_t *defined_out)
{
    uint64_t collection_addr = ia64_rse_collect_word(env->ar_bspstore);
    uint64_t value = 0;
    uint64_t defined = 0;

    /*
     * With neither active latch present, materialize the architecturally
     * undefined RNAT value as zero.  Retained shadows remain available to
     * later fill and store operations, but are not sufficient by themselves
     * to select a guest-visible collection.
     */
    if (env->rse.rse_rnat_addr == UINT64_MAX &&
        !env->rse.rse_load_rnat_valid) {
        *defined_out = 0;
        return 0;
    }

    if (env->rse.rse_load_rnat_valid &&
        env->rse.rse_load_rnat_addr == collection_addr) {
        defined = env->rse.rse_load_rnat_defined & INT64_MAX;
        value = env->rse.rse_load_rnat & defined;
    }
    ia64_rse_rnat_shadow_overlay(env, collection_addr, &value, &defined);
    if (env->rse.rse_rnat_addr == collection_addr) {
        uint64_t spill_defined = env->rse.rse_rnat_defined & INT64_MAX;

        value = (value & ~spill_defined) |
                (env->ar_rnat & spill_defined);
        defined |= spill_defined;
    }
    *defined_out = defined & INT64_MAX;
    return value & defined & INT64_MAX;
}

/*
 * Return the architected RNAT collection associated with AR.BSPSTORE.
 *
 * Mandatory fills can move the internal store pointer into a collection
 * already held by the fill-side latch while AR.RNAT still preserves an
 * unsaved partial spill collection elsewhere.  Compose the current
 * collection on read, overlaying only bits with an explicit source.  The SDM
 * leaves all other bits undefined.  This target deterministically
 * materializes them as zero, so a stale backing-store word can never inject
 * NaT state into a different frame.
 */
uint64_t ia64_rse_read_rnat(const CPUIA64State *env)
{
    uint64_t visible =
        ia64_rse_rnat_low_mask(ia64_rse_collect_bit(env->ar_bspstore));
    uint64_t defined;
    uint64_t value = ia64_rse_compose_rnat(env, &defined);

    return value & defined & visible;
}

uint64_t ia64_rse_read_rnat_defined(const CPUIA64State *env)
{
    uint64_t visible =
        ia64_rse_rnat_low_mask(ia64_rse_collect_bit(env->ar_bspstore));
    uint64_t defined;

    (void)ia64_rse_compose_rnat(env, &defined);
    return defined & visible;
}

static void ia64_rse_invalidate_load_rnat(CPUIA64State *env)
{
    env->rse.rse_load_rnat = 0;
    env->rse.rse_load_rnat_addr = 0;
    env->rse.rse_load_rnat_defined = 0;
    env->rse.rse_load_rnat_valid = false;
}

static void ia64_rse_rnat_writeback_clear(CPUIA64State *env,
                                          const char *operation)
{
    IA64RnatWritebackImage *image = &env->rse.rse_writeback_rnat;

    if (image->valid) {
        trace_ia64_rse_rnat_writeback(env_cpu(env)->cpu_index, operation,
                                      env->ip, image->addr, image->value,
                                      image->defined);
    }
    memset(image, 0, sizeof(*image));
}

static int ia64_rse_rnat_shadow_find(const CPUIA64State *env, uint64_t addr)
{
    unsigned i;

    for (i = 0; i < env->rse.rse_rnat_shadow_count; i++) {
        const IA64RnatShadowEntry *entry =
            &env->rse.rse_rnat_shadow[i];

        if (entry->valid && entry->addr == addr) {
            return i;
        }
    }
    return -1;
}

static void ia64_rse_rnat_shadow_delete(CPUIA64State *env, unsigned slot)
{
    unsigned last = --env->rse.rse_rnat_shadow_count;

    if (slot != last) {
        env->rse.rse_rnat_shadow[slot] =
            env->rse.rse_rnat_shadow[last];
    }
    memset(&env->rse.rse_rnat_shadow[last], 0,
           sizeof(env->rse.rse_rnat_shadow[last]));
}

static void ia64_rse_rnat_shadow_clear_all(CPUIA64State *env)
{
    memset(env->rse.rse_rnat_shadow, 0,
           sizeof(env->rse.rse_rnat_shadow));
    env->rse.rse_rnat_shadow_count = 0;
}

static void ia64_rse_rnat_shadow_remove(CPUIA64State *env, uint64_t addr,
                                        const char *operation)
{
    int slot = ia64_rse_rnat_shadow_find(env, addr);

    if (slot >= 0) {
        IA64RnatShadowEntry *entry = &env->rse.rse_rnat_shadow[slot];

        trace_ia64_rse_rnat_shadow(env_cpu(env)->cpu_index, operation,
                                   env->ip, slot, entry->addr, entry->value,
                                   entry->defined);
        ia64_rse_rnat_shadow_delete(env, slot);
    }
}

static void ia64_rse_rnat_shadow_overlay(const CPUIA64State *env,
                                         uint64_t addr, uint64_t *value,
                                         uint64_t *defined)
{
    int slot = ia64_rse_rnat_shadow_find(env, addr);

    if (slot >= 0) {
        const IA64RnatShadowEntry *entry =
            &env->rse.rse_rnat_shadow[slot];
        uint64_t shadow_defined = entry->defined & INT64_MAX;

        *value = (*value & ~shadow_defined) |
                 (entry->value & shadow_defined);
        *defined |= shadow_defined;
    }
}

static void ia64_rse_rnat_shadow_merge(CPUIA64State *env, uint64_t addr,
                                       uint64_t value, uint64_t defined,
                                       bool incoming_newer,
                                       const char *operation)
{
    IA64RnatShadowEntry *entry;
    int slot;

    defined &= INT64_MAX;
    value &= defined;
    if (addr == UINT64_MAX || defined == 0) {
        return;
    }

    slot = ia64_rse_rnat_shadow_find(env, addr);
    if (slot < 0) {
        if (env->rse.rse_rnat_shadow_count ==
            IA64_RSE_RNAT_SHADOW_COUNT) {
            /*
             * The physical stacked-register window bounds the number of
             * live partial collections.  Keep execution deterministic if
             * that invariant is broken and emit the displaced address.
             */
            slot = 0;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ia64 RSE RNAT shadow overflow at ip=%016" PRIx64
                          ", replacing collection %016" PRIx64 "\n",
                          env->ip, env->rse.rse_rnat_shadow[slot].addr);
        } else {
            slot = env->rse.rse_rnat_shadow_count++;
        }
    }

    entry = &env->rse.rse_rnat_shadow[slot];
    if (entry->valid && entry->addr == addr) {
        if (incoming_newer) {
            entry->value = (entry->value & ~defined) | value;
            entry->defined |= defined;
        } else {
            uint64_t missing = defined & ~entry->defined;

            entry->value |= value & missing;
            entry->defined |= missing;
        }
    } else {
        entry->value = value;
        entry->addr = addr;
        entry->defined = defined;
        entry->valid = true;
    }
    entry->value &= entry->defined & INT64_MAX;
    entry->defined &= INT64_MAX;
    trace_ia64_rse_rnat_shadow(env_cpu(env)->cpu_index, operation, env->ip,
                               slot, entry->addr, entry->value,
                               entry->defined);
}

static void ia64_rse_rnat_shadow_stash(CPUIA64State *env)
{
    ia64_rse_rnat_shadow_merge(env, env->rse.rse_rnat_addr,
                               env->ar_rnat,
                               env->rse.rse_rnat_defined, true, "stash");
}

/*
 * Mandatory fills can alternate between RNAT collections while registers
 * from each collection are still resident in the physical stack.  Preserve
 * the displaced fill-side image just like a suspended spill-side image; a
 * later fill or spill must not depend on whichever collection happened to
 * use the one-entry load latch most recently.
 *
 * A matching spill latch may coexist with the saved fill image.  Its
 * explicitly defined bits are newer and overlay the shadow; retaining the
 * older holes is necessary because internal pointer movement can later
 * detach or clip the spill latch before all backed registers are refilled.
 */
static void ia64_rse_load_rnat_stash(CPUIA64State *env,
                                     uint64_t next_addr)
{
    if (!env->rse.rse_load_rnat_valid ||
        env->rse.rse_load_rnat_addr == next_addr) {
        return;
    }
    ia64_rse_rnat_shadow_merge(env, env->rse.rse_load_rnat_addr,
                               env->rse.rse_load_rnat,
                               env->rse.rse_load_rnat_defined, false,
                               "fill-stash");
}

static bool ia64_rse_rnat_shadow_take(CPUIA64State *env, uint64_t addr,
                                      uint64_t *value, uint64_t *defined)
{
    int slot = ia64_rse_rnat_shadow_find(env, addr);
    IA64RnatShadowEntry *entry;

    if (slot < 0) {
        *value = 0;
        *defined = 0;
        return false;
    }

    entry = &env->rse.rse_rnat_shadow[slot];
    *defined = entry->defined & INT64_MAX;
    *value = entry->value & *defined;
    trace_ia64_rse_rnat_shadow(env_cpu(env)->cpu_index, "restore", env->ip,
                               slot, entry->addr, *value, *defined);
    ia64_rse_rnat_shadow_delete(env, slot);
    return true;
}

static void ia64_rse_rnat_shadow_clip(CPUIA64State *env, uint64_t addr,
                                      uint64_t keep)
{
    int slot = ia64_rse_rnat_shadow_find(env, addr);
    IA64RnatShadowEntry *entry;

    if (slot < 0) {
        return;
    }
    entry = &env->rse.rse_rnat_shadow[slot];
    entry->defined &= keep & INT64_MAX;
    entry->value &= entry->defined;
    if (entry->defined == 0) {
        ia64_rse_rnat_shadow_delete(env, slot);
    }
}

/* Keep only NaT bits for register words in [first, last). */
static void ia64_rse_rnat_shadow_retain_range(CPUIA64State *env,
                                              uint64_t first,
                                              uint64_t last)
{
    unsigned slot = 0;

    while (slot < env->rse.rse_rnat_shadow_count) {
        IA64RnatShadowEntry *entry =
            &env->rse.rse_rnat_shadow[slot];
        uint64_t base = entry->addr & ~0x1ffULL;
        uint64_t lower = MAX(first, base);
        uint64_t upper = MIN(last, entry->addr);
        uint64_t keep = 0;

        if (lower < upper) {
            uint32_t low_bit = (lower - base) >> 3;
            uint32_t high_bit = (upper - base) >> 3;
            uint64_t below_low = low_bit == 0 ? 0 :
                                 (1ULL << low_bit) - 1;
            uint64_t below_high = high_bit == 63 ? INT64_MAX :
                                  (1ULL << high_bit) - 1;

            keep = below_high & ~below_low;
        }
        entry->defined &= keep;
        entry->value &= entry->defined;
        if (entry->defined == 0) {
            ia64_rse_rnat_shadow_delete(env, slot);
        } else {
            slot++;
        }
    }
}

/*
 * SDM Vol.2 6.5.2 and 6.5.4 make AR.RNAT undefined at loadrs, while SDM
 * Vol.3 models the collection used by rse_load as a separate NaT dispersal
 * register.  Retain only explicitly known bits for the partial collection as
 * internal RSE state.  Any value materialized by mov-from-RNAT remains within
 * the architecturally undefined result permitted after loadrs.
 *
 * Do not seed this writeback-only image from rse_load_rnat: fill-visible
 * dispersal state is staged separately before loadrs detaches the active
 * latch, while an eventual store-side RMW can consult the then-current memory
 * image directly.
 */
static void ia64_rse_rnat_writeback_capture_loadrs(CPUIA64State *env)
{
    IA64RnatWritebackImage *image = &env->rse.rse_writeback_rnat;
    uint64_t collection_addr = ia64_rse_collect_word(env->ar_bspstore);
    uint64_t keep =
        ia64_rse_rnat_low_mask(ia64_rse_collect_bit(env->ar_bspstore));
    uint64_t value = 0;
    uint64_t defined = 0;

    if (image->valid && image->addr == collection_addr) {
        defined = image->defined & INT64_MAX;
        value = image->value & defined;
    } else if (image->valid) {
        ia64_rse_rnat_writeback_clear(env, "loadrs-replace");
    }

    ia64_rse_rnat_shadow_overlay(env, collection_addr, &value, &defined);
    if (env->rse.rse_rnat_addr == collection_addr) {
        uint64_t spill_defined = env->rse.rse_rnat_defined & INT64_MAX;

        value = (value & ~spill_defined) |
                (env->ar_rnat & spill_defined);
        defined |= spill_defined;
    }

    defined &= keep & INT64_MAX;
    value &= defined;
    if (defined == 0) {
        ia64_rse_rnat_writeback_clear(env, "loadrs-empty");
        return;
    }

    image->value = value;
    image->addr = collection_addr;
    image->defined = defined;
    image->valid = true;
    trace_ia64_rse_rnat_writeback(env_cpu(env)->cpu_index,
                                  "loadrs-capture", env->ip, image->addr,
                                  image->value, image->defined);
}

static void ia64_rse_rnat_bind(CPUIA64State *env, uint64_t collection_addr)
{
    uint64_t value = 0;
    uint64_t defined = 0;
    uint64_t shadow_value;
    uint64_t shadow_defined;

    if (env->rse.rse_rnat_addr == collection_addr) {
        return;
    }
    trace_ia64_rse_rnat_rebind(env_cpu(env)->cpu_index, env->ip,
                               env->rse.rse_rnat_addr, env->ar_rnat,
                               env->rse.rse_rnat_defined, collection_addr);
    ia64_rse_rnat_shadow_stash(env);
    /*
     * A completed collection obtained by a mandatory fill remains the
     * authoritative backing-store image for bits below BSPSTORE.  When the
     * store pointer later re-enters that collection part-way through, seed
     * the spill latch from the matching fill-side value so the eventual
     * collection store cannot erase the untouched prefix.
     *
     * A shadow entry contains newer, explicitly accumulated spill bits and
     * therefore overlays the fill-side image.
     */
    if (env->rse.rse_load_rnat_valid &&
        env->rse.rse_load_rnat_addr == collection_addr) {
        defined = env->rse.rse_load_rnat_defined & INT64_MAX;
        value = env->rse.rse_load_rnat & defined;
    }
    if (ia64_rse_rnat_shadow_take(env, collection_addr, &shadow_value,
                                  &shadow_defined)) {
        value = (value & ~shadow_defined) |
                (shadow_value & shadow_defined);
        defined |= shadow_defined;
    }
    env->ar_rnat = value;
    env->rse.rse_rnat_addr = collection_addr;
    env->rse.rse_rnat_defined = defined;
}

/*
 * Software has supplied AR.RNAT for the partial spill collection containing
 * AR.BSPSTORE.  Track the full collection address as well as RNATBitIndex:
 * the six-bit index repeats every 512 bytes and cannot identify a collection
 * by itself.
 */
void ia64_rse_rnat_reloaded(CPUIA64State *env)
{
    uint32_t bit = ia64_rse_collect_bit(env->ar_bspstore);
    uint64_t collection_addr = ia64_rse_collect_word(env->ar_bspstore);

    /*
     * An explicit RNAT write is the software authority for this context,
     * including the SDM 6.10 backing-store edit sequence.  A physical image
     * retained across an earlier loadrs must not override it later.
     */
    ia64_rse_rnat_writeback_clear(env, "rnat-reload");

    /*
     * A fill-side value for this collection predates the architected write.
     * Do not let it reappear if internal pointer movement later exposes a
     * bit above the write-time RNATBitIndex.
     */
    if (env->rse.rse_load_rnat_valid &&
        env->rse.rse_load_rnat_addr == collection_addr) {
        ia64_rse_invalidate_load_rnat(env);
    }
    if (env->rse.rse_rnat_addr != collection_addr) {
        ia64_rse_rnat_shadow_stash(env);
    }
    ia64_rse_rnat_shadow_remove(env, collection_addr, "reload-discard");
    env->rse.rse_rnat_addr = collection_addr;
    env->rse.rse_rnat_defined = ia64_rse_rnat_low_mask(bit);
    env->ar_rnat &= env->rse.rse_rnat_defined;
}

/*
 * mov-to-BSPSTORE and loadrs make AR.RNAT undefined
 * (SDM Vol.2 6.5.2 and 6.5.4).
 * Detach the architected spill latch and invalidate the active fill latch.
 * A loadrs caller may first retain explicitly known partial collections as
 * the implementation's separate NaT dispersal state.  This state remains a
 * fill source independent of the architecturally undefined AR.RNAT value.
 *
 * SDM Vol.2 6.5.3 sets BSPSTORE and RSE.BspLoad to the supplied address and
 * empties the clean partition.  Section 6.10 also uses a same-value BSPSTORE
 * rewrite when software changes coherent backing-store memory before
 * restoring RNAT.  Consequently mov-to-BSPSTORE discards all cached
 * collection state, including the writeback-only image.  loadrs has distinct
 * effects and retains only the internal dispersal and writeback state needed
 * by subsequent fills or collection stores.
 */
static void ia64_rse_rnat_detach(CPUIA64State *env, const char *site,
                                 bool discard_writeback)
{
    env->ar_rnat = 0;
    env->rse.rse_rnat_addr = UINT64_MAX;
    env->rse.rse_rnat_defined = 0;
    ia64_rse_invalidate_load_rnat(env);
    if (discard_writeback) {
        ia64_rse_rnat_shadow_clear_all(env);
        ia64_rse_rnat_writeback_clear(env, site);
    }
}

void ia64_rse_rnat_undefined(CPUIA64State *env, const char *site)
{
    ia64_rse_rnat_detach(env, site, true);
}

static void ia64_rse_rnat_move_bspstore(CPUIA64State *env, uint64_t bspstore)
{
    uint64_t collection_addr = ia64_rse_collect_word(bspstore);
    uint32_t bit = ia64_rse_collect_bit(bspstore);
    uint64_t keep = ia64_rse_rnat_low_mask(bit);

    /*
     * This is internal br.ret/rfi partition movement, not architected
     * mov-to-BSPSTORE.  It must not make RNAT undefined or discard the
     * partial spill collection while mandatory fills walk below it.  It
     * also must not make formerly undefined high bits valid when an
     * incomplete frame temporarily moves BSPSTORE upward.
     */
    if (env->rse.rse_rnat_addr == collection_addr) {
        env->rse.rse_rnat_defined &= keep;
        env->ar_rnat &= env->rse.rse_rnat_defined;
    }
    ia64_rse_rnat_shadow_clip(env, collection_addr, keep);
    env->ar_bspstore = bspstore;
}

/*
 * NaT collection words emitted when a backing-store pointer advances
 * over nregs registers: (addr{8:3} + nregs) / 63 (SDM Vol.2 table 6-2,
 * e.g. the br.call row's AR[BSP] update).
 */
static G_GNUC_NO_INLINE uint32_t ia64_rse_nat_word_count_slow(uint32_t total)
{
    return total / 63;
}

static inline uint32_t ia64_rse_nat_word_count(uint32_t total)
{
    /*
     * An architected frame has at most 96 stacked registers, so an RSE
     * pointer movement crosses no more than two 63-register collections.
     * Retain division for out-of-range internal state to preserve the
     * helper's full uint32_t semantics.
     */
    if (likely(total < 3 * 63)) {
        return (total >= 63) + (total >= 2 * 63);
    }
    return ia64_rse_nat_word_count_slow(total);
}

uint32_t ia64_rse_nat_words_grow(uint64_t addr, uint32_t nregs)
{
    uint32_t total = ia64_rse_collect_bit(addr) + nregs;

    return ia64_rse_nat_word_count(total);
}

/*
 * NaT collection words crossed when a backing-store pointer retreats
 * over nregs registers: (62 - addr{8:3} + nregs) / 63 (SDM Vol.2
 * table 6-2, e.g. the br.ret row's AR[BSP] update).
 */
static inline uint32_t ia64_rse_nat_words_shrink(uint64_t addr, uint32_t nregs)
{
    uint32_t total = 62 - ia64_rse_collect_bit(addr) + nregs;

    return ia64_rse_nat_word_count(total);
}

/* Register-value words in [addr - words * 8, addr) in the backing store. */
static uint32_t ia64_rse_register_words_below(uint64_t addr, uint32_t words)
{
    uint32_t first_nat = ia64_rse_collect_bit(addr) + 1;
    uint32_t nat_words;

    /*
     * Walking backward, the first RNAT slot is first_nat words below addr;
     * subsequent RNAT slots occur every 64 backing-store words.
     */
    nat_words = words < first_nat ? 0 : 1 + (words - first_nat) / 64;
    return words - nat_words;
}

/*
 * Map a virtual stacked register index [0, sof) to its physical index:
 * registers inside the rotating region are renamed by CFM.rrb.gr
 * modulo the region size before the bottom-of-frame bias is applied
 * (register rotation, SDM Vol.1 4.5.3).
 */
static uint32_t ia64_rse_rotating_gr_count(const CPUIA64State *env)
{
    uint32_t count = (uint32_t)env->cfm_sor << 3;

    /*
     * Architected writers reject a rotating region larger than SOF.  Treat
     * malformed implementation state as having no rotating GRs so mapping
     * and physical rotation remain consistent and cannot address outside
     * the frame.
     */
    return count <= env->cfm_sof && count <= IA64_STACKED_GR_COUNT ?
           count : 0;
}

static uint32_t ia64_rse_virt_to_phys(const CPUIA64State *env, uint32_t v)
{
    uint32_t sor_regs = ia64_rse_rotating_gr_count(env);
    uint32_t p;

    if (v < sor_regs) {
        v += env->cfm_rrb_gr;
        if (v >= sor_regs) {
            v -= sor_regs;
        }
    }
    p = env->rse.rse_bol + v;
    return p < IA64_STACKED_GR_COUNT ? p : p - IA64_STACKED_GR_COUNT;
}

/* Inverse of ia64_rse_virt_to_phys. */
static uint32_t ia64_rse_phys_to_virt(const CPUIA64State *env, uint32_t p)
{
    uint32_t off = p >= env->rse.rse_bol ?
                   p - env->rse.rse_bol :
                   p + IA64_STACKED_GR_COUNT - env->rse.rse_bol;
    uint32_t sor_regs = ia64_rse_rotating_gr_count(env);

    if (off < sor_regs) {
        off += sor_regs - env->cfm_rrb_gr;
        if (off >= sor_regs) {
            off -= sor_regs;
        }
    }
    return off;
}

static bool ia64_rse_pgr_nat_get(const CPUIA64State *env, uint32_t p)
{
    return (env->rse.rse_pgr_nat[p / 64] >> (p % 64)) & 1;
}

static void ia64_rse_pgr_nat_set(CPUIA64State *env, uint32_t p, bool nat,
                                 const char *operation, uint64_t source)
{
    uint64_t mask = 1ULL << (p % 64);
    bool old_nat = (env->rse.rse_pgr_nat[p / 64] & mask) != 0;

    if (old_nat != nat) {
        trace_ia64_rse_pgr_nat_update(env_cpu(env)->cpu_index, operation,
                                      env->ip, p, source, old_nat, nat);
    }

    if (nat) {
        env->rse.rse_pgr_nat[p / 64] |= mask;
    } else {
        env->rse.rse_pgr_nat[p / 64] &= ~mask;
    }
}

static void ia64_copy_bit_range(uint64_t dst[2], uint32_t dst_bit,
                                const uint64_t src[2], uint32_t src_bit,
                                uint32_t count)
{
    __uint128_t source;
    __uint128_t target;
    __uint128_t mask;

    if (count == 0) {
        return;
    }

    source = ((__uint128_t)src[1] << 64) | src[0];
    target = ((__uint128_t)dst[1] << 64) | dst[0];
    mask = count == 128 ? ~(__uint128_t)0 :
                          (((__uint128_t)1 << count) - 1);
    mask <<= dst_bit;
    target = (target & ~mask) |
             (((source >> src_bit) << dst_bit) & mask);
    dst[0] = target;
    dst[1] = target >> 64;
}

static inline QEMU_ALWAYS_INLINE void
ia64_clear_bit_range(uint64_t bits[2], uint32_t first, uint32_t count)
{
    uint32_t word = first / 64;
    uint32_t shift = first % 64;
    uint32_t n = MIN(count, 64 - shift);
    uint64_t mask;

    if (count == 0) {
        return;
    }

    mask = n == 64 ? UINT64_MAX : ((1ULL << n) - 1) << shift;
    bits[word] &= ~mask;
    count -= n;
    if (count != 0) {
        mask = count == 64 ? UINT64_MAX : (1ULL << count) - 1;
        bits[word + 1] &= ~mask;
    }
}

/*
 * Copy a directly mapped, NaT-free frame into the physical file.  Keep this
 * path separate from the general mapper below: the latter needs snapshots,
 * rotation state, tracing arguments, and a stack protector even when none of
 * those facilities is used.  Normal compiler-generated call frames satisfy
 * these conditions overwhelmingly often.
 */
static G_GNUC_NO_INLINE void
ia64_rse_sync_frame_out_direct(CPUIA64State *env, uint64_t dirty0,
                               uint64_t dirty1, uint32_t sof, uint32_t bol)
{
    env->rse.rse_gr_dirty[0] = 0;
    env->rse.rse_gr_dirty[1] = 0;

    /* Ignore implementation-state dirt outside the architected frame. */
    if (sof < 64) {
        dirty0 &= sof == 0 ? 0 : (1ULL << sof) - 1;
        dirty1 = 0;
    } else {
        dirty1 &= (1ULL << (sof - 64)) - 1;
    }

    while (dirty0 != 0) {
        uint32_t bit = ctz64(dirty0);

        dirty0 &= dirty0 - 1;
        env->rse.rse_pgr[bol + bit] =
            env->gr[IA64_STACKED_GR_BASE + bit];
    }
    while (dirty1 != 0) {
        uint32_t bit = ctz64(dirty1);

        dirty1 &= dirty1 - 1;
        env->rse.rse_pgr[bol + 64 + bit] =
            env->gr[IA64_STACKED_GR_BASE + 64 + bit];
    }
}

/* Copy dirty registers from the virtual view into the physical file. */
static G_GNUC_NO_INLINE void
ia64_rse_sync_frame_out_slow(CPUIA64State *env, uint64_t dirty0,
                             uint64_t dirty1)
{
    uint64_t dirty[2] = { dirty0, dirty1 };
    uint64_t nat[2] = { env->nat[0], env->nat[1] };
    uint32_t sof = env->cfm_sof;
    uint32_t sor_regs = ia64_rse_rotating_gr_count(env);
    uint32_t rrb_gr = env->cfm_rrb_gr;
    uint32_t bol = env->rse.rse_bol;
    bool nat_files_clear =
        ((nat[0] >> IA64_STACKED_GR_BASE) | nat[1] |
         env->rse.rse_pgr_nat[0] | env->rse.rse_pgr_nat[1]) == 0;
    uint32_t word;

    env->rse.rse_gr_dirty[0] = 0;
    env->rse.rse_gr_dirty[1] = 0;
    /*
     * Dirty bits outside the current frame were ignored by the loops below.
     * Mask them once so the hot loops need neither to visit nor test them.
     */
    if (sof < 64) {
        dirty[0] &= sof == 0 ? 0 : (1ULL << sof) - 1;
        dirty[1] = 0;
    } else {
        dirty[1] &= (1ULL << (sof - 64)) - 1;
    }

    /*
     * Most frames neither rotate their GRs nor wrap around the physical
     * register ring.  With no NaT state to update, their mapping is a simple
     * base-plus-index copy; select that case once per frame instead of
     * repeating rotation and wrap checks for every dirty register.
     */
    if (nat_files_clear && (sor_regs == 0 || rrb_gr == 0) &&
        bol + sof <= IA64_STACKED_GR_COUNT) {
        for (word = 0; word < 2; word++) {
            while (dirty[word] != 0) {
                uint32_t bit = ctz64(dirty[word]);
                uint32_t v = word * 64 + bit;

                dirty[word] &= dirty[word] - 1;
                env->rse.rse_pgr[bol + v] =
                    env->gr[IA64_STACKED_GR_BASE + v];
            }
        }
        return;
    }

    if (nat_files_clear) {
        for (word = 0; word < 2; word++) {
            while (dirty[word] != 0) {
                uint32_t bit = ctz64(dirty[word]);
                uint32_t v = word * 64 + bit;
                uint32_t p = v;

                dirty[word] &= dirty[word] - 1;
                if (v < sor_regs) {
                    p += rrb_gr;
                    if (p >= sor_regs) {
                        p -= sor_regs;
                    }
                }
                p += bol;
                if (p >= IA64_STACKED_GR_COUNT) {
                    p -= IA64_STACKED_GR_COUNT;
                }
                env->rse.rse_pgr[p] =
                    env->gr[IA64_STACKED_GR_BASE + v];
            }
        }
        return;
    }

    for (word = 0; word < 2; word++) {
        while (dirty[word] != 0) {
            uint32_t bit = ctz64(dirty[word]);
            uint32_t v = word * 64 + bit;

            dirty[word] &= dirty[word] - 1;
            uint32_t p = v;
            uint32_t reg = IA64_STACKED_GR_BASE + v;

            if (v < sor_regs) {
                p += rrb_gr;
                if (p >= sor_regs) {
                    p -= sor_regs;
                }
            }
            p += bol;
            if (p >= IA64_STACKED_GR_COUNT) {
                p -= IA64_STACKED_GR_COUNT;
            }
            env->rse.rse_pgr[p] = env->gr[reg];
            bool reg_nat = v < 32 ?
                (nat[0] >> (IA64_STACKED_GR_BASE + v)) & 1 :
                (nat[1] >> (v - 32)) & 1;

            if (reg_nat) {
                trace_ia64_rse_pgr_sync_out(
                    env_cpu(env)->cpu_index, env->ip, reg, p,
                    env->gr[reg], reg_nat);
            }
            ia64_rse_pgr_nat_set(env, p, reg_nat, "sync-out", reg);
        }
    }
}

/* Keep the overwhelmingly common clean-frame check at each call site. */
static inline QEMU_ALWAYS_INLINE void
ia64_rse_sync_frame_out(CPUIA64State *env)
{
    uint64_t dirty0 = env->rse.rse_gr_dirty[0];
    uint64_t dirty1 = env->rse.rse_gr_dirty[1];

    if (unlikely(dirty0 | dirty1)) {
        uint32_t sof = env->cfm_sof;
        uint32_t bol = env->rse.rse_bol;

        /*
         * With no stacked NaTs, no active GR rotation, and no physical-file
         * wrap, virtual register v maps exactly to physical register bol+v.
         * This is the same first branch as the general helper, selected here
         * so its large uncommon frame is never entered on the usual path.
         * Malformed SOR state deliberately falls back to the defensive
         * normalizer in ia64_rse_rotating_gr_count().
         */
        if (likely(((env->nat[0] >> IA64_STACKED_GR_BASE) |
                    env->nat[1] | env->rse.rse_pgr_nat[0] |
                    env->rse.rse_pgr_nat[1]) == 0 &&
                   (env->cfm_sor == 0 || env->cfm_rrb_gr == 0) &&
                   bol + sof <= IA64_STACKED_GR_COUNT)) {
            ia64_rse_sync_frame_out_direct(env, dirty0, dirty1, sof, bol);
            return;
        }
        ia64_rse_sync_frame_out_slow(env, dirty0, dirty1);
    }
}

static void ia64_rse_sync_frame_in_range(CPUIA64State *env, uint32_t first,
                                         uint32_t count)
{
    uint32_t end = MIN(first + count, (uint32_t)env->cfm_sof);
    uint32_t i;

    if (first >= end) {
        return;
    }

    if (env->cfm_sor == 0 || env->cfm_rrb_gr == 0) {
        uint32_t p = env->rse.rse_bol + first;
        uint32_t total = end - first;
        uint32_t first_span;
        uint32_t second_span;

        if (p >= IA64_STACKED_GR_COUNT) {
            p -= IA64_STACKED_GR_COUNT;
        }
        first_span = MIN(total, IA64_STACKED_GR_COUNT - p);
        second_span = total - first_span;
        memcpy(&env->gr[IA64_STACKED_GR_BASE + first], &env->rse.rse_pgr[p],
               first_span * sizeof(env->rse.rse_pgr[0]));
        if (second_span != 0) {
            memcpy(&env->gr[IA64_STACKED_GR_BASE + first + first_span],
                   env->rse.rse_pgr,
                   second_span * sizeof(env->rse.rse_pgr[0]));
        }
        if (likely((env->rse.rse_pgr_nat[0] | env->rse.rse_pgr_nat[1]) == 0)) {
            ia64_clear_bit_range(env->nat, IA64_STACKED_GR_BASE + first,
                                 total);
        } else {
            ia64_copy_bit_range(env->nat, IA64_STACKED_GR_BASE + first,
                                env->rse.rse_pgr_nat, p, first_span);
            if (second_span != 0) {
                ia64_copy_bit_range(
                    env->nat, IA64_STACKED_GR_BASE + first + first_span,
                    env->rse.rse_pgr_nat, 0, second_span);
            }
        }
        return;
    }

    for (i = first; i < end; i++) {
        uint32_t p = ia64_rse_virt_to_phys(env, i);

        env->gr[IA64_STACKED_GR_BASE + i] = env->rse.rse_pgr[p];
        ia64_gr_nat_set(env, IA64_STACKED_GR_BASE + i,
                        ia64_rse_pgr_nat_get(env, p));
    }
}

/*
 * Load a directly mapped frame when the physical file contains no NaTs.
 * Keeping this path out of the general mapper avoids its rotation, 128-bit
 * NaT-copy, and large register-save frame on every ordinary br.ret/rfi.
 */
static G_GNUC_NO_INLINE void
ia64_rse_sync_frame_in_direct(CPUIA64State *env, uint32_t sof, uint32_t bol)
{
    uint32_t first = MIN(sof, IA64_STACKED_GR_COUNT - bol);
    uint32_t second = sof - first;

    memcpy(&env->gr[IA64_STACKED_GR_BASE], &env->rse.rse_pgr[bol],
           first * sizeof(env->rse.rse_pgr[0]));
    if (second != 0) {
        memcpy(&env->gr[IA64_STACKED_GR_BASE + first], env->rse.rse_pgr,
               second * sizeof(env->rse.rse_pgr[0]));
    }

    /* All virtual stacked NaTs are known clear, including outside SOF. */
    env->nat[0] &= UINT32_MAX;
    env->nat[1] = 0;
    env->rse.rse_gr_dirty[0] = 0;
    env->rse.rse_gr_dirty[1] = 0;
}

/* Load the virtual view of the current frame from the physical file. */
static G_GNUC_NO_INLINE void
ia64_rse_sync_frame_in_slow(CPUIA64State *env)
{
    uint32_t i;

    if (env->cfm_sor == 0 || env->cfm_rrb_gr == 0) {
        uint32_t first = MIN((uint32_t)env->cfm_sof,
                             IA64_STACKED_GR_COUNT - env->rse.rse_bol);
        uint32_t second = env->cfm_sof - first;

        memcpy(&env->gr[IA64_STACKED_GR_BASE],
               &env->rse.rse_pgr[env->rse.rse_bol],
               first * sizeof(env->rse.rse_pgr[0]));
        if (second != 0) {
            memcpy(&env->gr[IA64_STACKED_GR_BASE + first], env->rse.rse_pgr,
                   second * sizeof(env->rse.rse_pgr[0]));
        }
        if (likely((env->rse.rse_pgr_nat[0] | env->rse.rse_pgr_nat[1]) == 0)) {
            /*
             * With no physical stacked-register NaTs, every bit in the
             * virtual stacked view is known clear, including registers
             * outside the current frame.  Preserve only static GR NaTs.
             */
            env->nat[0] &= UINT32_MAX;
            env->nat[1] = 0;
        } else {
            ia64_copy_bit_range(env->nat, IA64_STACKED_GR_BASE,
                                env->rse.rse_pgr_nat, env->rse.rse_bol, first);
            if (second != 0) {
                ia64_copy_bit_range(env->nat, IA64_STACKED_GR_BASE + first,
                                    env->rse.rse_pgr_nat, 0, second);
            }
        }
        env->rse.rse_gr_dirty[0] = 0;
        env->rse.rse_gr_dirty[1] = 0;
        return;
    }

    for (i = 0; i < env->cfm_sof; i++) {
        uint32_t p = ia64_rse_virt_to_phys(env, i);

        env->gr[IA64_STACKED_GR_BASE + i] = env->rse.rse_pgr[p];
        ia64_gr_nat_set(env, IA64_STACKED_GR_BASE + i,
                        ia64_rse_pgr_nat_get(env, p));
    }
    env->rse.rse_gr_dirty[0] = 0;
    env->rse.rse_gr_dirty[1] = 0;
}

static inline QEMU_ALWAYS_INLINE void
ia64_rse_sync_frame_in(CPUIA64State *env)
{
    if (likely((env->cfm_sor == 0 || env->cfm_rrb_gr == 0) &&
               (env->rse.rse_pgr_nat[0] |
                env->rse.rse_pgr_nat[1]) == 0)) {
        ia64_rse_sync_frame_in_direct(env, env->cfm_sof,
                                      env->rse.rse_bol);
        return;
    }
    ia64_rse_sync_frame_in_slow(env);
}

void ia64_rse_save_context(CPUIA64State *env,
                           IA64RSEContextState *state)
{
    ia64_rse_sync_frame_out(env);
    memcpy(state->pgr, env->rse.rse_pgr, sizeof(state->pgr));
    memcpy(state->pgr_nat, env->rse.rse_pgr_nat, sizeof(state->pgr_nat));
    memcpy(state->gr_dirty, env->rse.rse_gr_dirty,
           sizeof(state->gr_dirty));
    state->bsp = env->ar_bsp;
    state->bspstore = env->ar_bspstore;
    state->rnat = env->ar_rnat;
    state->bol = env->rse.rse_bol;
    state->dirty = env->rse.rse_dirty;
    state->dirty_nat = env->rse.rse_dirty_nat;
    state->clean = env->rse.rse_clean;
    state->clean_nat = env->rse.rse_clean_nat;
    state->invalid = env->rse.rse_invalid;
    state->rnat_addr = env->rse.rse_rnat_addr;
    state->rnat_defined = env->rse.rse_rnat_defined;
    state->load_rnat = env->rse.rse_load_rnat;
    state->load_rnat_addr = env->rse.rse_load_rnat_addr;
    state->load_rnat_defined = env->rse.rse_load_rnat_defined;
    state->load_rnat_valid = env->rse.rse_load_rnat_valid;
    state->writeback_rnat = env->rse.rse_writeback_rnat;
    memcpy(state->rnat_shadow, env->rse.rse_rnat_shadow,
           sizeof(state->rnat_shadow));
    state->rnat_shadow_count = env->rse.rse_rnat_shadow_count;
    state->cfm_sof = env->cfm_sof;
    state->cfm_sol = env->cfm_sol;
    state->cfm_sor = env->cfm_sor;
    state->cfm_rrb_gr = env->cfm_rrb_gr;
    state->cfm_rrb_fr = env->cfm_rrb_fr;
    state->cfm_rrb_pr = env->cfm_rrb_pr;
    state->cfle = env->rse.rse_cfle;
    state->completion_pending = env->rse.rse_completion_pending;
    state->completion_demoted = env->rse.rse_completion_demoted;
    state->completion_psr = env->rse.rse_completion_psr;
    state->completion_source_ip = env->rse.rse_completion_source_ip;
    state->completion_source_slot = env->rse.rse_completion_source_slot;
}

void ia64_rse_restore_context(CPUIA64State *env,
                              const IA64RSEContextState *state)
{
    memcpy(env->rse.rse_pgr, state->pgr, sizeof(state->pgr));
    memcpy(env->rse.rse_pgr_nat, state->pgr_nat, sizeof(state->pgr_nat));
    memcpy(env->rse.rse_gr_dirty, state->gr_dirty,
           sizeof(state->gr_dirty));
    env->ar_bsp = state->bsp;
    env->ar_bspstore = state->bspstore;
    env->ar_rnat = state->rnat;
    env->rse.rse_bol = state->bol;
    env->rse.rse_dirty = state->dirty;
    env->rse.rse_dirty_nat = state->dirty_nat;
    env->rse.rse_clean = state->clean;
    env->rse.rse_clean_nat = state->clean_nat;
    env->rse.rse_invalid = state->invalid;
    env->rse.rse_rnat_addr = state->rnat_addr;
    env->rse.rse_rnat_defined = state->rnat_defined;
    env->rse.rse_load_rnat = state->load_rnat;
    env->rse.rse_load_rnat_addr = state->load_rnat_addr;
    env->rse.rse_load_rnat_defined = state->load_rnat_defined;
    env->rse.rse_load_rnat_valid = state->load_rnat_valid;
    env->rse.rse_writeback_rnat = state->writeback_rnat;
    memcpy(env->rse.rse_rnat_shadow, state->rnat_shadow,
           sizeof(env->rse.rse_rnat_shadow));
    env->rse.rse_rnat_shadow_count = state->rnat_shadow_count;
    env->cfm_sof = state->cfm_sof;
    env->cfm_sol = state->cfm_sol;
    env->cfm_sor = state->cfm_sor;
    env->cfm_rrb_gr = state->cfm_rrb_gr;
    ia64_set_cfm_rrb_fr(env, state->cfm_rrb_fr);
    ia64_set_cfm_rrb_pr(env, state->cfm_rrb_pr);
    env->rse.rse_cfle = state->cfle;
    env->rse.rse_access = false;
    env->rse.rse_completion_pending = state->completion_pending;
    env->rse.rse_completion_demoted = state->completion_demoted;
    env->rse.rse_completion_psr = state->completion_psr;
    env->rse.rse_completion_source_ip = state->completion_source_ip;
    env->rse.rse_completion_source_slot = state->completion_source_slot;
    ia64_rse_sync_frame_in(env);
    ia64_rse_check(env, "context-restore");
}

/*
 * Perform one mandatory RSE store at AR.BSPSTORE, spilling either the
 * oldest dirty register or, when BSPSTORE{8:3} is all ones, the RNAT
 * collection (SDM Vol.2 6.5.2).  Returns 1 when a register was stored,
 * 0 for a NaT collection word.
 */
static int ia64_rse_store_one(CPUIA64State *env, uintptr_t ra)
{
    uint64_t bspstore = env->ar_bspstore;
    uint64_t collection_addr = ia64_rse_collect_word(bspstore);
    uint32_t ncb = ia64_rse_collect_bit(bspstore);

    /*
     * Bind the spill latch before consuming it.  A prior mandatory fill or
     * internal pointer move may have left a value from another 512-byte
     * collection in the implementation state.  No bit follows the latch to
     * a different collection.
     */
    ia64_rse_rnat_bind(env, collection_addr);

    if (ncb == 63) {
        IA64RnatWritebackImage *image =
            &env->rse.rse_writeback_rnat;
        uint64_t spill_defined =
            env->rse.rse_rnat_defined & INT64_MAX;
        uint64_t defined = spill_defined;
        uint64_t value = env->ar_rnat & spill_defined;
        uint64_t previous;
        uint64_t collection;

        /*
         * Writeback precedence is deliberate:
         *
         *   current spill bits > loadrs writeback image > backing memory.
         *
         * Keeping the loadrs image out of ia64_rse_rnat_bind and
         * ia64_rse_fill_collection makes it mechanically impossible for an
         * architecturally undefined bit to become a fill source.
         */
        if (image->valid && image->addr == collection_addr) {
            uint64_t saved_defined = image->defined & INT64_MAX;

            value = (image->value & saved_defined & ~spill_defined) |
                    value;
            defined |= saved_defined;
            trace_ia64_rse_rnat_writeback(
                env_cpu(env)->cpu_index, "store-merge", env->ip,
                image->addr, image->value, image->defined);
        }
        collection = ia64_rse_write_collection(
            env, bspstore, value, defined, &previous, ra);

        trace_ia64_rse_rnat_store(env_cpu(env)->cpu_index, env->ip,
                                  bspstore, defined, previous, collection);

        /*
         * SDM Vol.2 6.5.2 makes some AR.RNAT bits undefined after loadrs or
         * mov-to-BSPSTORE, while 6.10 requires the backing store below
         * BSPSTORE to remain coherent.  The store-side helper resolves both:
         * current spill bits win, loadrs-retained writeback bits fill their
         * holes, all remaining positions retain the actual backing-memory
         * image, and bit 63 is always zero.  None of the retained positions
         * becomes an architectural RNAT or fill source before this complete
         * collection store succeeds.
         */
        if (image->valid && image->addr == collection_addr) {
            ia64_rse_rnat_writeback_clear(env, "store-retire");
        }
        ia64_rse_rnat_shadow_remove(env, collection_addr,
                                    "store-discard");
        ia64_rse_load_rnat_stash(env, collection_addr);
        env->rse.rse_load_rnat = collection;
        env->rse.rse_load_rnat_addr = collection_addr;
        env->rse.rse_load_rnat_defined = INT64_MAX;
        env->rse.rse_load_rnat_valid = true;
        env->ar_rnat = 0;
        env->ar_bspstore = bspstore + 8;
        env->rse.rse_rnat_addr = UINT64_MAX;
        env->rse.rse_rnat_defined = 0;
        env->rse.rse_dirty_nat--;
        if (ia64_rse_has_clean_partition(env)) {
            env->rse.rse_clean_nat++;
        }
        return 0;
    } else {
        uint32_t p = ia64_rse_wrap_phys((int32_t)env->rse.rse_bol -
                                        env->rse.rse_dirty);

        ia64_rse_write_u64(env, bspstore, env->rse.rse_pgr[p], ra);
        if (ia64_rse_pgr_nat_get(env, p)) {
            env->ar_rnat |= 1ULL << ncb;
        } else {
            env->ar_rnat &= ~(1ULL << ncb);
        }
        env->rse.rse_rnat_defined |= 1ULL << ncb;
        env->ar_bspstore = bspstore + 8;
        env->rse.rse_dirty--;
        if (ia64_rse_has_clean_partition(env)) {
            env->rse.rse_clean++;
        } else {
            env->rse.rse_invalid++;
        }
        return 1;
    }
}

/*
 * Obtain the NaT collection for a mandatory fill without replacing the
 * architected partial spill collection in AR.RNAT.  RSE.BspLoad can be in a
 * different 512-byte collection from AR.BSPSTORE; conflating the two loses
 * unsaved NaT bits and later attaches them to unrelated stacked registers.
 *
 * The SDM defines this externally through the resulting physical NaT state
 * and the RNAT collection selected by AR.BSPSTORE.  rse_load_rnat is an
 * implementation latch rather than a separate guest-visible register;
 * ia64_rse_read_rnat() composes it into the architectural RNAT view when its
 * collection becomes current.
 */
static uint64_t ia64_rse_fill_collection(CPUIA64State *env, uint64_t addr,
                                         uintptr_t ra, uint64_t *defined_out,
                                         uint32_t *sources_out)
{
    uint64_t collection_addr = ia64_rse_collect_word(addr);
    uint64_t collection = 0;
    uint64_t defined = 0;
    uint32_t sources = 0;

    if (env->rse.rse_load_rnat_valid &&
        env->rse.rse_load_rnat_addr == collection_addr) {
        defined = env->rse.rse_load_rnat_defined & INT64_MAX;
        collection = env->rse.rse_load_rnat & defined;
        sources |= 1;
    } else if (env->ar_bspstore > collection_addr) {
        /*
         * AR.BSPSTORE has advanced past the collection word, so the RSE
         * stored it when BSPSTORE{8:3} was all ones and the backing store
         * holds the architected collection for this group.
         */
        collection = ia64_rse_read_u64(env, collection_addr, ra) &
                     INT64_MAX;
        defined = INT64_MAX;
        sources |= 2;
    }

    if (ia64_rse_rnat_shadow_find(env, collection_addr) >= 0) {
        sources |= 4;
    }
    ia64_rse_rnat_shadow_overlay(env, collection_addr, &collection,
                                 &defined);

    /*
     * SDM Vol.2 6.5.2: "The RSE never saves partial NaT collections to
     * the backing store."  When neither latch nor a completed collection
     * supplies a value, leave it undefined rather than reading whatever
     * previously occupied that memory.  Undefined bits resolve to zero.
     */
    if (env->rse.rse_rnat_addr == collection_addr) {
        uint64_t spill_defined = env->rse.rse_rnat_defined & INT64_MAX;

        sources |= 8;
        /*
         * SDM Vol.2 6.5.2 defines RNAT{RSE.RNATBitIndex:0} and leaves
         * every higher bit undefined in the ordinary partial-collection
         * state.  Exceptional state transitions can leave a sparser set of
         * known bits, so overlay the explicit mask rather than assuming a
         * contiguous range.
         */
        collection = (collection & ~spill_defined) |
                     (env->ar_rnat & spill_defined);
        defined |= spill_defined;
    }
    collection &= defined & INT64_MAX;
    ia64_rse_load_rnat_stash(env, collection_addr);
    env->rse.rse_load_rnat = collection;
    env->rse.rse_load_rnat_addr = collection_addr;
    env->rse.rse_load_rnat_defined = defined;
    env->rse.rse_load_rnat_valid = true;
    *defined_out = defined;
    *sources_out = sources;
    return collection;
}

/*
 * Perform one mandatory RSE load from the first backing-store word
 * below the clean partition, filling either an invalid physical
 * register or reloading the RNAT collection when the load pointer sits
 * on a NaT collection word (SDM Vol.2 6.5.2).  Returns 1 when a
 * register was loaded, 0 for a NaT collection word.  When the load
 * targets the current frame (only possible while the frame is
 * incomplete) the virtual view is updated alongside the physical file
 * so that a fault on a later load leaves a consistent frame.
 */
static int ia64_rse_load_one(CPUIA64State *env, uint64_t bspload,
                             uintptr_t ra)
{
    uint32_t ncb = ia64_rse_collect_bit(bspload);

    if (ncb == 63) {
        uint64_t collection =
            ia64_rse_read_u64(env, bspload, ra) & INT64_MAX;

        ia64_rse_load_rnat_stash(env, ia64_rse_collect_word(bspload));
        env->rse.rse_load_rnat = collection;
        env->rse.rse_load_rnat_addr = ia64_rse_collect_word(bspload);
        env->rse.rse_load_rnat_defined = INT64_MAX;
        env->rse.rse_load_rnat_valid = true;
        env->rse.rse_clean_nat++;
        return 0;
    } else {
        uint64_t value;
        uint64_t collection;
        uint64_t defined;
        bool nat;
        uint32_t sources;
        uint32_t p;
        uint32_t v;

        value = ia64_rse_read_u64(env, bspload, ra);
        p = ia64_rse_wrap_phys(
            (int32_t)env->rse.rse_bol -
            (env->rse.rse_clean + env->rse.rse_dirty + 1));

        collection = ia64_rse_fill_collection(env, bspload, ra, &defined,
                                              &sources);
        nat = (collection >> ncb) & 1;
        v = ia64_rse_phys_to_virt(env, p);
        if (nat) {
            trace_ia64_rse_nat_fill(env_cpu(env)->cpu_index, env->ip, p,
                                    v, env->rse.rse_bol, env->cfm_sof,
                                    bspload, collection, defined, sources);
        }
        env->rse.rse_pgr[p] = value;
        ia64_rse_pgr_nat_set(env, p, nat, "load", bspload);
        env->rse.rse_clean++;
        env->rse.rse_invalid--;

        if (v < env->cfm_sof) {
            env->gr[IA64_STACKED_GR_BASE + v] = value;
            ia64_gr_nat_set(env, IA64_STACKED_GR_BASE + v,
                            ia64_rse_pgr_nat_get(env, p));
        }
        return 1;
    }
}

/*
 * Complete an incomplete frame with mandatory RSE loads after br.ret
 * or rfi (SDM Vol.2 6.8).  env must already be committed to the
 * post-branch state: faults are delivered on the target instruction
 * (ra == 0) with the partitions describing exactly the loads that
 * remain.
 *
 * RSE.CFLE (SDM Vol.2 6.6) is set for the duration of the sequence:
 * br.ret and rfi set it when the frame being returned to is not
 * entirely contained in the physical stacked register file, and it is
 * cleared when the sequence completes.  A load that faults leaves the
 * frame incomplete; interruption delivery then clears CFLE and the
 * handler runs with the incomplete frame until it executes cover or
 * an rfi resumes the sequence.
 */
static void ia64_rse_complete_frame_loads(CPUIA64State *env, uintptr_t ra)
{
    int64_t live;
    uint64_t bspload;

    if (env->rse.rse_dirty >= 0 && env->rse.rse_dirty_nat >= 0) {
        return;
    }

    live = (int64_t)env->rse.rse_clean + env->rse.rse_clean_nat +
           env->rse.rse_dirty + env->rse.rse_dirty_nat;
    bspload = env->ar_bsp - (live + 1) * 8;
    env->rse.rse_cfle = true;

    /*
     * Instruction processing checks for enabled pending external interrupts
     * before it performs a CFLE-enabled mandatory load (SDM Vol.2 section
     * 5.3, steps 3 and 4).  The post-load window below represents the same
     * check after execution restarts at step one; this initial window is the
     * corresponding check before the first backing-store reference.
     */
    ia64_rse_interrupt_window(env);
    while (env->rse.rse_dirty < 0 || env->rse.rse_dirty_nat < 0) {
        if (ia64_rse_load_one(env, bspload, ra)) {
            env->rse.rse_clean--;
            env->rse.rse_dirty++;
        } else {
            env->rse.rse_clean_nat--;
            env->rse.rse_dirty_nat++;
        }
        ia64_rse_rnat_move_bspstore(env, env->ar_bspstore - 8);
        /* load_one added exactly one word to the live partitions. */
        bspload -= 8;
        ia64_rse_interrupt_window(env);
    }
    env->rse.rse_cfle = false;
}

/* br.call/cover: the current frame joins the dirty partition. */
static void ia64_rse_preserve_frame(CPUIA64State *env, uint32_t nregs)
{
    uint32_t nats;

    if (nregs == 0) {
        return;
    }
    nats = ia64_rse_nat_words_grow(env->ar_bsp, nregs);

    env->rse.rse_bol = ia64_rse_wrap_phys(env->rse.rse_bol + nregs);
    env->ar_bsp += (uint64_t)(nregs + nats) * 8;
    env->rse.rse_dirty += nregs;
    env->rse.rse_dirty_nat += nats;
}

/*
 * Grow the current frame for alloc.  New registers come from the
 * invalid partition first; once that is exhausted the oldest clean
 * registers are reused (their backing-store copies remain valid), and
 * only when the whole file is occupied does the RSE issue mandatory
 * stores to make room (SDM Vol.2 6.4).  Restartable: a faulting store
 * leaves the partitions describing the completed portion and the
 * alloc re-executes.
 */
static void ia64_rse_new_frame(CPUIA64State *env, int32_t growth,
                               uintptr_t ra)
{
    if (growth <= env->rse.rse_invalid) {
        env->rse.rse_invalid -= growth;
        return;
    }
    growth -= env->rse.rse_invalid;

    if (growth <= env->rse.rse_clean) {
        env->rse.rse_invalid = 0;
        env->rse.rse_clean -= growth;
        env->rse.rse_clean_nat =
            ia64_rse_nat_words_shrink(
                env->ar_bsp,
                env->rse.rse_clean + env->rse.rse_dirty + 1) -
            env->rse.rse_dirty_nat;
        return;
    }
    growth -= env->rse.rse_clean;

    /*
     * Mandatory stores make room for the remainder.  The invalid and
     * clean partitions are consumed only after the last store
     * completes: a store that faults must leave every register still
     * owned by its partition so that the re-executed alloc recomputes
     * exactly the work that remains.
     */
    while (growth > 0) {
        growth -= ia64_rse_store_one(env, ra);
        ia64_rse_interrupt_window(env);
    }
    env->rse.rse_invalid = 0;
    env->rse.rse_clean = 0;
    env->rse.rse_clean_nat = 0;
}

/*
 * Partition bookkeeping for the frame restored by br.ret or rfi.
 * "preserved" is the number of new-frame registers that lie below the
 * old bottom-of-frame (AR.PFS.pfm.sol for br.ret, CR.IFS.ifm.sof for
 * rfi, per the AR[BSP] rows of SDM Vol.2 table 6-2); "growth" is how
 * far the new frame's top extends beyond the old frame's top;
 * "old_sof" is the returned-from frame's size.  rse_bol must already
 * have been moved down by "preserved" and CFM set to the restored
 * frame marker.
 */
static void ia64_rse_restore_frame(CPUIA64State *env, uint32_t preserved,
                                   int32_t growth, uint32_t old_sof)
{
    int32_t preserved_nats =
        ia64_rse_nat_words_shrink(env->ar_bsp, preserved);
    int32_t missing;
    int32_t missing_nats;

    env->ar_bsp -= (uint64_t)(preserved + preserved_nats) * 8;

    if (growth > env->rse.rse_invalid + env->rse.rse_clean) {
        /*
         * Bad PFS used by branch return (SDM Vol.2 6.5.5): the output
         * area of the frame being returned to does not fit in the
         * physical file.  CFM is forced to zero, the preserved and
         * returned-from registers all join the invalid partition, and
         * the dirty partition shrinks by the preserved registers; the
         * clean partition is left unchanged.
         */
        env->rse.rse_invalid += preserved + old_sof;
        env->rse.rse_dirty -= preserved;
        env->rse.rse_dirty_nat -= preserved_nats;
        env->cfm_sof = 0;
        env->cfm_sol = 0;
        env->cfm_sor = 0;
        env->cfm_rrb_gr = 0;
        ia64_set_cfm_rrb_fr(env, 0);
        ia64_set_cfm_rrb_pr(env, 0);
        return;
    }

    /*
     * Any growth of the frame's top consumes invalid registers first
     * and then the oldest clean registers, whose backing-store copies
     * remain valid.
     */
    if (growth > env->rse.rse_invalid) {
        env->rse.rse_clean -= growth - env->rse.rse_invalid;
        env->rse.rse_clean_nat =
            ia64_rse_nat_words_shrink(
                env->ar_bsp,
                env->rse.rse_clean + env->rse.rse_dirty + 1) -
            env->rse.rse_dirty_nat;
        env->rse.rse_invalid = 0;
    } else {
        env->rse.rse_invalid -= growth;
    }

    /*
     * The preserved registers re-enter the frame from the top of the
     * dirty partition.  Anything beyond it is taken from the clean
     * partition, and anything older than that is no longer in the
     * physical file: the frame becomes incomplete (negative dirty
     * counts, BSPSTORE above BSP) until mandatory loads bring the
     * missing registers back from the backing store (SDM Vol.2 6.8).
     */
    missing = (int32_t)preserved - env->rse.rse_dirty;
    missing_nats = preserved_nats - env->rse.rse_dirty_nat;
    if (missing <= 0) {
        env->rse.rse_dirty -= preserved;
        env->rse.rse_dirty_nat -= preserved_nats;
        return;
    }

    if (missing <= env->rse.rse_clean) {
        env->rse.rse_clean -= missing;
        env->rse.rse_clean_nat -= missing_nats;
        env->rse.rse_dirty = 0;
        env->rse.rse_dirty_nat = 0;
        ia64_rse_rnat_move_bspstore(env, env->ar_bsp);
        return;
    }

    env->rse.rse_dirty = -(missing - env->rse.rse_clean);
    env->rse.rse_dirty_nat = -(missing_nats - env->rse.rse_clean_nat);
    env->rse.rse_clean = 0;
    env->rse.rse_clean_nat = 0;
    ia64_rse_rnat_move_bspstore(
        env, env->ar_bsp -
             (int64_t)(env->rse.rse_dirty + env->rse.rse_dirty_nat) * 8);
}

/* br.ia and rfi-to-IA-32: only the current frame stays valid. */
static void ia64_rse_invalidate_non_current(CPUIA64State *env)
{
    env->rse.rse_dirty = 0;
    env->rse.rse_dirty_nat = 0;
    env->rse.rse_clean = 0;
    env->rse.rse_clean_nat = 0;
    env->rse.rse_invalid = IA64_STACKED_GR_COUNT - env->cfm_sof;
    /*
     * Dropping the dirty partition empties the backing store's dirty
     * region, so AR.BSPSTORE meets AR.BSP.  br.ia rejects a non-empty
     * store beforehand, but an rfi to an IA-32 target arrives here with
     * whatever the interrupted context left, and leaving BSPSTORE
     * behind would both break BSPSTORE == BSP - 8*ndirty and leave
     * AR.RNAT collecting for a group it no longer addresses.
     */
    ia64_rse_rnat_move_bspstore(env, env->ar_bsp);
}

/*
 * Calls, returns, and allocs invalidate only stacked-register ALAT entries.
 * Compiled code normally has no live ALAT entry, so mirror the callee's
 * empty-table check here and avoid a function call on that common path.
 */
static inline void ia64_rse_invalidate_stacked_alat(CPUIA64State *env)
{
    if (unlikely(env->alat_state.alat_active_count != 0)) {
        ia64_invalidate_stacked_alat(env);
    }
}

/* Reset the three rotation bases without calling setters that are no-ops. */
static inline void ia64_rse_reset_rotations(CPUIA64State *env)
{
    env->cfm_rrb_gr = 0;
    if (unlikely(env->cfm_rrb_fr != 0)) {
        ia64_set_cfm_rrb_fr(env, 0);
    }
    if (unlikely(env->cfm_rrb_pr != 0)) {
        ia64_set_cfm_rrb_pr(env, 0);
    }
}

/*
 * Common CFM/BOF update for br.ret and rfi.  Writes the restored frame
 * marker, moves BOF down by "preserved", adjusts the partitions, and
 * performs any mandatory loads.  The caller must have committed PSR
 * and IP to the post-branch state first (mandatory load faults are
 * delivered on the target instruction).
 */
static void ia64_rse_return_to_frame(CPUIA64State *env, uint64_t pfm,
                                     uint32_t preserved)
{
    uint32_t old_sof = env->cfm_sof;
    uint32_t new_sof = pfm & IA64_CFM_SOF_MASK;
    uint32_t new_sol = (pfm & IA64_CFM_SOL_MASK) >> IA64_CFM_SOL_SHIFT;
    uint32_t new_rrb_fr = (pfm & IA64_CFM_RRB_FR_MASK) >>
                          IA64_CFM_RRB_FR_SHIFT;
    uint32_t new_rrb_pr = (pfm & IA64_CFM_RRB_PR_MASK) >>
                          IA64_CFM_RRB_PR_SHIFT;
    int32_t growth = (int32_t)new_sof - (int32_t)preserved -
                     (int32_t)old_sof;

    ia64_rse_sync_frame_out(env);

    env->cfm_sof = new_sof;
    env->cfm_sol = new_sol;
    env->cfm_sor = (pfm & IA64_CFM_SOR_MASK) >> IA64_CFM_SOR_SHIFT;
    env->cfm_rrb_gr = (pfm & IA64_CFM_RRB_GR_MASK) >> IA64_CFM_RRB_GR_SHIFT;
    if (unlikely(new_rrb_fr != env->cfm_rrb_fr ||
                 new_rrb_fr >= IA64_ROTATING_FR_COUNT)) {
        ia64_set_cfm_rrb_fr(env, new_rrb_fr);
    }
    if (unlikely(new_rrb_pr != env->cfm_rrb_pr ||
                 new_rrb_pr >= IA64_PR_COUNT - IA64_PR_ROTATING_BASE)) {
        ia64_set_cfm_rrb_pr(env, new_rrb_pr);
    }
    env->rse.rse_bol = ia64_rse_wrap_phys((int32_t)env->rse.rse_bol -
                                      (int32_t)preserved);

    ia64_rse_restore_frame(env, preserved, growth, old_sof);
    ia64_rse_sync_frame_in(env);
    ia64_rse_invalidate_stacked_alat(env);
    ia64_rse_complete_frame_loads(env, 0);
    ia64_rse_check(env, "return");
}

void ia64_rse_return_from_min_state(CPUIA64State *env, uint64_t cfm)
{
    ia64_rse_return_to_frame(env, cfm, cfm & IA64_CFM_SOF_MASK);
}

#ifdef CONFIG_DEBUG_TCG
void ia64_rse_delivery_check(CPUIA64State *env, int excp)
{
    char site[32];

    snprintf(site, sizeof(site), "delivery excp=%d", excp);
    ia64_rse_check(env, site);
}
#endif /* CONFIG_DEBUG_TCG */

/*
 * Internal consistency checks for the register stack model.  Each RSE
 * operation must leave the four partitions covering the physical file
 * exactly once, the backing-store pointers in their architected
 * relationship, and the NaT-collection word counts matching the
 * partition boundaries.  A violation indicates an emulator bug; log it
 * once with enough state to reconstruct the failure.
 */
#ifdef CONFIG_DEBUG_TCG
void ia64_rse_check(CPUIA64State *env, const char *site)
{
    const IA64RnatWritebackImage *writeback =
        &env->rse.rse_writeback_rnat;
    static unsigned reported;
    unsigned shadow_count = env->rse.rse_rnat_shadow_count;
    unsigned i;
    unsigned j;
    int64_t total = (int64_t)env->cfm_sof + env->rse.rse_dirty +
                    env->rse.rse_clean + env->rse.rse_invalid;
    uint64_t expected_bspstore = env->ar_bsp -
        (int64_t)(env->rse.rse_dirty + env->rse.rse_dirty_nat) * 8;
    bool bad = total != IA64_STACKED_GR_COUNT ||
               env->ar_bspstore != expected_bspstore ||
               env->rse.rse_clean < 0 || env->rse.rse_invalid < 0 ||
               env->rse.rse_bol >= IA64_STACKED_GR_COUNT;

    bad |= !ia64_cfm_frame_fields_valid(env->cfm_sof, env->cfm_sol,
                                        env->cfm_sor);
    bad |= env->cfm_sor ?
           env->cfm_rrb_gr >= ((uint32_t)env->cfm_sor << 3) :
           env->cfm_rrb_gr != 0;
    bad |= env->cfm_rrb_fr >= IA64_ROTATING_FR_COUNT ||
           env->cfm_rrb_pr >= IA64_PR_COUNT - IA64_PR_ROTATING_BASE;
    bad |= (env->ar_rnat | env->rse.rse_rnat_defined |
            env->rse.rse_load_rnat |
            env->rse.rse_load_rnat_defined) & ~INT64_MAX;
    bad |= env->ar_rnat & ~env->rse.rse_rnat_defined;
    bad |= env->rse.rse_load_rnat &
           ~env->rse.rse_load_rnat_defined;
    if (env->rse.rse_rnat_addr == UINT64_MAX) {
        bad |= env->rse.rse_rnat_defined != 0;
    } else {
        bad |= ia64_rse_collect_word(env->rse.rse_rnat_addr) !=
               env->rse.rse_rnat_addr;
    }
    if (env->rse.rse_load_rnat_valid) {
        bad |= ia64_rse_collect_word(env->rse.rse_load_rnat_addr) !=
               env->rse.rse_load_rnat_addr;
    } else {
        bad |= env->rse.rse_load_rnat != 0 ||
               env->rse.rse_load_rnat_addr != 0 ||
               env->rse.rse_load_rnat_defined != 0;
    }
    bad |= (writeback->value | writeback->defined) & ~INT64_MAX;
    bad |= writeback->value & ~writeback->defined;
    if (writeback->valid) {
        bad |= writeback->defined == 0;
        bad |= ia64_rse_collect_word(writeback->addr) != writeback->addr;
    } else {
        bad |= writeback->value != 0 || writeback->addr != 0 ||
               writeback->defined != 0;
    }
    bad |= shadow_count > IA64_RSE_RNAT_SHADOW_COUNT;
    for (i = 0; i < IA64_RSE_RNAT_SHADOW_COUNT; i++) {
        const IA64RnatShadowEntry *entry =
            &env->rse.rse_rnat_shadow[i];

        if (i >= shadow_count) {
            bad |= entry->valid;
            bad |= entry->value != 0 || entry->addr != 0 ||
                   entry->defined != 0;
            continue;
        }
        bad |= !entry->valid;
        bad |= (entry->value | entry->defined) & ~INT64_MAX;
        bad |= entry->value & ~entry->defined;
        bad |= entry->defined == 0;
        bad |= ia64_rse_collect_word(entry->addr) != entry->addr;
        for (j = i + 1;
             j < MIN(shadow_count, IA64_RSE_RNAT_SHADOW_COUNT); j++) {
            bad |= env->rse.rse_rnat_shadow[j].valid &&
                   env->rse.rse_rnat_shadow[j].addr == entry->addr;
        }
    }

    if (!bad && !ia64_rse_has_clean_partition(env)) {
        bad |= env->rse.rse_clean != 0 || env->rse.rse_clean_nat != 0;
    }

    if (!bad && env->rse.rse_dirty >= 0) {
        /* NaT collection words live at addresses 0x1f8 mod 0x200. */
        bad |= env->rse.rse_dirty_nat !=
               (int32_t)((int64_t)(env->ar_bsp >> 9) -
                         (int64_t)(env->ar_bspstore >> 9));
    }
    if (!bad && env->rse.rse_clean >= 0 && env->rse.rse_dirty >= 0) {
        uint64_t bspload = env->ar_bspstore -
            (int64_t)(env->rse.rse_clean + env->rse.rse_clean_nat) * 8;

        bad |= env->rse.rse_clean_nat !=
               (int32_t)((int64_t)(env->ar_bspstore >> 9) -
                         (int64_t)(bspload >> 9));
    }

    if (bad && qatomic_fetch_inc(&reported) < 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ia64 rse inconsistency at %s: excp=%u ip=%016" PRIx64
                      " sof=%u sol=%u sor=%u rrb=%u bol=%u dirty=%d/%d"
                      " clean=%d/%d invalid=%d bsp=%016" PRIx64
                      " bspstore=%016" PRIx64 " rnat=%016" PRIx64
                      "@%016" PRIx64 "/%016" PRIx64
                      " load-rnat=%016" PRIx64 "@%016" PRIx64
                      "/%016" PRIx64 "/%u"
                      " writeback=%016" PRIx64 "@%016" PRIx64
                      "/%016" PRIx64 "/%u"
                      " shadow=%u cfle=%d\n",
                      site, env->exception_state.exception, env->ip,
                      env->cfm_sof, env->cfm_sol,
                      env->cfm_sor, env->cfm_rrb_gr, env->rse.rse_bol,
                      env->rse.rse_dirty, env->rse.rse_dirty_nat,
                      env->rse.rse_clean,
                      env->rse.rse_clean_nat, env->rse.rse_invalid, env->ar_bsp,
                      env->ar_bspstore, env->ar_rnat,
                      env->rse.rse_rnat_addr,
                      env->rse.rse_rnat_defined,
                      env->rse.rse_load_rnat,
                      env->rse.rse_load_rnat_addr,
                      env->rse.rse_load_rnat_defined,
                      env->rse.rse_load_rnat_valid,
                      writeback->value, writeback->addr,
                      writeback->defined, writeback->valid, shadow_count,
                      env->rse.rse_cfle);
    }
}
#endif /* CONFIG_DEBUG_TCG */

static void ia64_rse_complete_pending_trap(CPUIA64State *env)
{
    uint32_t conditions;
    uint64_t source_ip;
    uint8_t source_slot;
    uint8_t target_slot;

    if (!env->rse.rse_completion_pending) {
        return;
    }

    conditions = IA64_NATIVE_TRAP_TAKEN |
        (env->rse.rse_completion_demoted ? IA64_NATIVE_TRAP_LOWER : 0);
    source_ip = env->rse.rse_completion_source_ip;
    source_slot = env->rse.rse_completion_source_slot;
    target_slot = (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT;

    /*
     * Clear the marker only after all mandatory target-frame loads have
     * completed.  If a load faults, control never reaches this helper and
     * the handler's rfi can resume the same completion sequence.
     */
    env->rse.rse_completion_pending = false;
    env->exception_state.psr_suppression_before_insn =
        env->rse.rse_completion_psr & IA64_PSR_FAULT_SUPPRESS_MASK;
    ia64_system_clear_psr_fault_suppression(env);
    ia64_check_native_traps(
        env, env->ip, source_ip,
        IA64_NATIVE_TRAP_SLOTS(target_slot, source_slot), conditions, 0,
        env->rse.rse_completion_psr);
}

void ia64_rse_resume_incomplete_frame(CPUIA64State *env)
{
    /*
     * An rfi with IFS.v clear resumes the mandatory loads of the frame
     * interrupted after br.ret or rfi (SDM Vol.2 section 6.8).
     */
    ia64_rse_complete_frame_loads(env, 0);
    ia64_rse_complete_pending_trap(env);
    ia64_rse_check(env, "rfi-resume");
}

void ia64_rfi(CPUIA64State *env, uint64_t fault_ip, uint32_t fault_slot)
{
    uint64_t old_psr = env->psr;
    uint64_t ipsr = env->cr_ipsr;
    uint64_t raw_iip = env->cr_iip;
    uint64_t iip = (ipsr & IA64_PSR_IS) ?
                   (raw_iip & UINT32_MAX) :
                   ia64_ip_bundle_addr(raw_iip);
    uint64_t ifs = env->cr_ifs;
    bool unimplemented_ia32_target =
        (ipsr & IA64_PSR_IS) &&
        ((ipsr & IA64_PSR_IT) ? !ia64_va_is_implemented(env, raw_iip) :
                                !ia64_pa_is_implemented(env, raw_iip));

    /*
     * Montecito has no native IA-32 execution engine.  An OS must use its
     * IA-32 execution layer instead of restoring PSR.is.  Keep the rfi
     * itself as the faulting instruction and do not commit the target PSR,
     * IP, or RSE state when that transition is requested.
     */
    if ((ipsr & IA64_PSR_IS) &&
        !ia64_env_cpu_class(env)->has_native_ia32) {
        ia64_raise_disabled_isa_transition(env, fault_ip, fault_slot);
    }

    env->exception_state.exception = IA64_EXCP_NONE;
    env->exception_state.fault_ip = 0;
    env->exception_state.fault_addr = 0;
    env->exception_state.fault_imm = 0;
    env->exception_state.fault_slot = 0;
    env->instruction_group_start = true;

    /*
     * rfi restores PSR and IP before the RSE moves the frame: mandatory
     * RSE loads are delivered on the target instruction and use the
     * restored PSR state (SDM Vol.2 6.6).  CR[IPSR], CR[IIP] and
     * CR[IFS] are consumed, not modified.
     */
    if (ipsr & IA64_PSR_IS) {
        ipsr &= ~(IA64_PSR_DA | IA64_PSR_DD |
                  IA64_PSR_IA | IA64_PSR_ED);
    }
    ia64_set_psr(env, ipsr);
    env->ip = iip;
    ia64_tlb_serialize(env, 1, 1);
    ia64_flush_on_pk_change(env, old_psr);

    if (ipsr & IA64_PSR_IS) {
        /* Return to IA-32: the register stack is left empty. */
        ia64_rse_sync_frame_out(env);
        env->cfm_sof = 0;
        env->cfm_sol = 0;
        env->cfm_sor = 0;
        env->cfm_rrb_gr = 0;
        ia64_set_cfm_rrb_fr(env, 0);
        ia64_set_cfm_rrb_pr(env, 0);
        ia64_rse_invalidate_non_current(env);
        ia64_alat_invala(env);
        ia64_ia32_enter(env);
        if (unimplemented_ia32_target) {
            /*
             * This is an IA-64 trap on the completed rfi, not a fault on
             * the first IA-32 instruction.  Preserve all 64 target bits.
             */
            env->cr_isr = IA64_ISR_CODE_UI;
            env->exception_state.ia32_transition_trap = true;
            ia64_raise_exception(env, IA64_EXCP_UNIMPL_INST_ADDR,
                                 raw_iip, fault_ip, fault_slot);
        }
        return;
    }

    if (ifs & IA64_IFS_V) {
        ia64_rse_return_to_frame(env, ifs & IA64_IFS_IFM_MASK,
                                 ifs & IA64_CFM_SOF_MASK);
        /* A handler may execute cover before returning to a faulted br.ret. */
        ia64_rse_complete_pending_trap(env);
    } else if (env->rse.rse_completion_pending ||
               env->rse.rse_dirty < 0 || env->rse.rse_dirty_nat < 0) {
        /*
         * SDM Vol.2 6.8: an interruption taken during the mandatory
         * loads of a br.ret/rfi leaves the current frame incomplete.
         * Returning with IFS.v = 0 resumes the original sequence of
         * mandatory loads for the still-incomplete frame.
         */
        ia64_rse_resume_incomplete_frame(env);
    }
}

bool ia64_gr_nat_get(const CPUIA64State *env, uint32_t reg)
{
    if (reg == 0) {
        return false;
    }

    return (env->nat[reg / 64] >> (reg % 64)) & 1;
}

void ia64_gr_nat_set(CPUIA64State *env, uint32_t reg, bool nat)
{
    if (reg == 0) {
        return;
    }

    if (nat) {
        env->nat[reg / 64] |= (1ULL << (reg % 64));
    } else {
        env->nat[reg / 64] &= ~(1ULL << (reg % 64));
    }
}

/* Rotate the low count bits left by one, preserving every higher bit. */
static inline QEMU_ALWAYS_INLINE void
ia64_rotate_low_bits_left_one(uint64_t bits[2], uint32_t count)
{
    uint64_t old0 = bits[0];
    uint64_t mask;
    uint64_t carry;

    if (count <= 64) {
        mask = count == 64 ? UINT64_MAX : (1ULL << count) - 1;
        carry = (old0 >> (count - 1)) & 1;
        bits[0] = (old0 & ~mask) | (((old0 << 1) | carry) & mask);
        return;
    }

    count -= 64;
    mask = (1ULL << count) - 1;
    carry = (bits[1] >> (count - 1)) & 1;
    bits[0] = (old0 << 1) | carry;
    bits[1] = (bits[1] & ~mask) |
              (((bits[1] << 1) | (old0 >> 63)) & mask);
}

static void ia64_rotate_rotating_gr_right(CPUIA64State *env)
{
    uint32_t count = ia64_rse_rotating_gr_count(env);
    uint64_t nat[2];
    uint64_t last;

    if (count == 0) {
        return;
    }

    last = env->gr[IA64_STACKED_GR_BASE + count - 1];
    memmove(&env->gr[IA64_STACKED_GR_BASE + 1],
            &env->gr[IA64_STACKED_GR_BASE],
            (count - 1) * sizeof(*env->gr));
    env->gr[IA64_STACKED_GR_BASE] = last;

    /*
     * Dirty bits name the current logical stacked registers.  Rotate them
     * with that view so ctop need not commit every software-pipeline stage
     * to the physical RSE file.  The usual call, cover and spill boundaries
     * synchronize the writes; migration carries this dirty bitmap as-is.
     */
    /* Pack stacked GR32..GR127 into one low-96-bit view. */
    nat[0] = (env->nat[0] >> 32) | (env->nat[1] << 32);
    nat[1] = env->nat[1] >> 32;
    ia64_rotate_low_bits_left_one(nat, count);
    env->nat[0] = (env->nat[0] & UINT32_MAX) | (nat[0] << 32);
    env->nat[1] = (nat[0] >> 32) | (nat[1] << 32);
    ia64_rotate_low_bits_left_one(env->rse.rse_gr_dirty, count);

    ia64_invalidate_alat_reg_range(env, IA64_STACKED_GR_BASE,
                                   IA64_STACKED_GR_BASE + count, false);
}

static void ia64_rotate_loop_regs(CPUIA64State *env)
{
    uint32_t rotating_gr_count = ia64_rse_rotating_gr_count(env);

    ia64_rse_check(env, "ctop");
    ia64_rotate_rotating_gr_right(env);
    if (rotating_gr_count != 0) {
        env->cfm_rrb_gr = env->cfm_rrb_gr ?
                          env->cfm_rrb_gr - 1 : rotating_gr_count - 1;
    }
    ia64_set_cfm_rrb_fr(env, env->cfm_rrb_fr ?
                             env->cfm_rrb_fr - 1 :
                             IA64_ROTATING_FR_COUNT - 1);
    ia64_rotate_cfm_rrb_pr_right(env);
}

void ia64_rse_br_call(CPUIA64State *env, uint32_t b_reg,
                         uint64_t next_ip, uint64_t target)
{
    uint64_t pfs = ia64_rse_current_pfs(env);
    uint32_t old_sof = env->cfm_sof;
    uint32_t sol = env->cfm_sol;
    uint32_t outputs = old_sof > sol ? old_sof - sol : 0;
    bool move_outputs = env->cfm_rrb_gr == 0;

    ia64_rse_sync_frame_out(env);
    ia64_rse_preserve_frame(env, sol);
    if (move_outputs && outputs != 0 && sol != 0) {
        memmove(&env->gr[IA64_STACKED_GR_BASE],
                &env->gr[IA64_STACKED_GR_BASE + sol],
                outputs * sizeof(*env->gr));
        /*
         * The output frame moves toward lower logical registers.  Copy its
         * NaT bits from a snapshot so the overlapping ranges cannot affect
         * one another.  When the entire stacked NaT view is clear the
         * destination is already the required all-zero value.
         */
        if (unlikely((env->nat[0] >> IA64_STACKED_GR_BASE) | env->nat[1])) {
            ia64_copy_bit_range(env->nat, IA64_STACKED_GR_BASE,
                                env->nat, IA64_STACKED_GR_BASE + sol,
                                outputs);
        }
    }
    env->cfm_sof = outputs;
    env->cfm_sol = 0;
    env->cfm_sor = 0;
    ia64_rse_reset_rotations(env);
    if (!move_outputs) {
        ia64_rse_sync_frame_in(env);
    }
    ia64_rse_invalidate_stacked_alat(env);

    env->ar_pfs = pfs;
    env->br[b_reg] = next_ip;
    ia64_system_reset_dahr(env);
    env->ip = ia64_ip_bundle_addr(target);
    env->psr &= ~IA64_PSR_RI_MASK;
    ia64_rse_check(env, "br.call");
    IA64_TRACE_RSE_STATE(env, "br.call");
}

void ia64_rse_br_ia(CPUIA64State *env, uint32_t b_reg,
                  uint64_t fault_ip, uint32_t fault_slot)
{
    uint64_t target = env->br[b_reg];
    uint64_t trap_code;
    IA64Exception trap;

    if (env->ar_bspstore != env->ar_bsp) {
        ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip, 0,
                               fault_slot);
        return;
    }

    if ((env->psr & IA64_PSR_DI) ||
        !ia64_env_cpu_class(env)->has_native_ia32) {
        ia64_raise_disabled_isa_transition(env, fault_ip, fault_slot);
    }

    /*
     * Perform the architectural IA-64 to IA-32 transition: IP takes
     * BR[b1]{31:0} with byte granularity, PSR.is is set, and only the
     * current (zero-size) frame stays valid.  The native IA-32 execution
     * engine then imports the architected register image before dispatching
     * an IA-32 TB.
     */
    env->ip = target & UINT32_MAX;
    env->psr |= IA64_PSR_IS;
    env->psr &= ~(IA64_PSR_DA | IA64_PSR_DD | IA64_PSR_ID | IA64_PSR_IA |
                  IA64_PSR_ED | IA64_PSR_RI_MASK);
    ia64_rse_sync_frame_out(env);
    env->cfm_sof = 0;
    env->cfm_sol = 0;
    env->cfm_sor = 0;
    ia64_rse_reset_rotations(env);
    ia64_rse_invalidate_non_current(env);
    ia64_alat_invala(env);
    ia64_ia32_enter(env);

    trap_code = ((env->psr & IA64_PSR_IT) ?
                 !ia64_va_is_implemented(env, target) :
                 !ia64_pa_is_implemented(env, target)) ?
                 IA64_ISR_CODE_UI : 0;
    trap_code |= (env->psr & IA64_PSR_TB) ? IA64_ISR_CODE_TB : 0;
    trap_code |= (env->psr & IA64_PSR_SS) ? IA64_ISR_CODE_SS : 0;
    if (!trap_code) {
        return;
    }

    if (trap_code & IA64_ISR_CODE_UI) {
        trap = IA64_EXCP_UNIMPL_INST_ADDR;
    } else if (trap_code & IA64_ISR_CODE_TB) {
        trap = IA64_EXCP_TAKEN_BRANCH;
    } else {
        trap = IA64_EXCP_SINGLE_STEP;
    }
    env->cr_isr = trap_code;
    env->exception_state.ia32_transition_trap = true;
    ia64_raise_exception(env, trap, target, fault_ip, fault_slot);
}





void ia64_rse_pop_return_frame(CPUIA64State *env, uint64_t pfs)
{
    /*
     * Restore EC before enabling any mandatory target-frame loads.  A fill
     * fault is delivered on the target instruction, whose handler must see
     * the caller's PFS.pec value (SDM Vol.3 br pseudocode).
     */
    env->ar_ec = (pfs & IA64_PFS_PEC_MASK) >> IA64_PFS_PEC_SHIFT;
    ia64_rse_return_to_frame(env, pfs & IA64_PFS_PFM_MASK,
                             (pfs & IA64_CFM_SOL_MASK) >>
                             IA64_CFM_SOL_SHIFT);
}

void ia64_rse_br_ret(CPUIA64State *env, uint32_t b_reg,
                     uint64_t source_ip, uint32_t source_slot)
{
    uint64_t old_psr = env->psr;
    uint64_t pfs = env->ar_pfs;
    uint64_t target = env->br[b_reg];
    uint8_t ppl = (pfs & IA64_PFS_PPL_MASK) >> IA64_PFS_PPL_SHIFT;
    bool demoted = ia64_psr_cpl(env->psr) < ppl;

    /*
     * Commit the branch target (slot 0) and demoted privilege level
     * before the frame restore: mandatory RSE load faults are
     * delivered on the target instruction (SDM Vol.2 6.6).
     */
    env->ip = ia64_ip_bundle_addr(target);
    env->psr &= ~IA64_PSR_RI_MASK;
    if (demoted) {
        ia64_set_psr(env, (env->psr & ~IA64_PSR_CPL_MASK) |
                          ((uint64_t)ppl << IA64_PSR_CPL_SHIFT));
    }
    env->rse.rse_completion_pending = true;
    env->rse.rse_completion_demoted = demoted;
    env->rse.rse_completion_psr = old_psr;
    env->rse.rse_completion_source_ip = ia64_ip_bundle_addr(source_ip);
    env->rse.rse_completion_source_slot = source_slot & 3;

    /*
     * A br.ret has successfully completed before target-frame loads begin.
     * If a mandatory fill faults, or an external interrupt wins the initial
     * target processing window, IIPA must therefore identify this branch.
     * The translated retirement path cannot record it when the helper exits
     * during that sequence.  Use the pre-instruction collection state: unlike
     * br.ret, an rfi entered with PSR.ic clear does not establish last_IP even
     * when it restores IPSR.ic (SDM Vol.2 section 3.3.5.6).
     */
    if (env->rse.rse_completion_psr & IA64_PSR_IC) {
        env->last_successful_bundle = env->rse.rse_completion_source_ip;
    }
    ia64_rse_pop_return_frame(env, pfs);
    ia64_rse_complete_pending_trap(env);
    IA64_TRACE_RSE_STATE(env, "br.ret");
}

void ia64_rse_alloc(CPUIA64State *env, uint32_t r1, uint32_t pfm,
                    uint64_t fault_ip, uint32_t slot, uintptr_t ra)
{
    uint32_t old_sof = env->cfm_sof;
    uint32_t new_sof = pfm & 0x7f;
    uint32_t new_sol = (pfm >> 7) & 0x7f;
    uint32_t new_sor = (pfm >> 14) & 0x0f;
    int32_t growth = (int32_t)new_sof - (int32_t)env->cfm_sof;

    /*
     * Translation rejects these immediate combinations before calling the
     * helper.  Keep the architectural entry point defensive as well: an
     * invalid CFM must never reach register mapping or rotation.
     */
    if (!ia64_cfm_frame_fields_valid(new_sof, new_sol, new_sor)) {
        env->cr_isr = 0;
        ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip, 0, slot);
    }

    /*
     * SDM Vol.2 6.6: alloc raises a Reserved Register/Field fault when
     * it changes the rotating-region size while any RRB is non-zero.
     * The RRBs themselves are not modified by alloc.
     */
    if (new_sor != env->cfm_sor &&
        (env->cfm_rrb_gr || env->cfm_rrb_fr || env->cfm_rrb_pr)) {
        env->cr_isr = 0x30;
        ia64_raise_exception(env, IA64_EXCP_RESERVED_REG_FIELD,
                               fault_ip, 0, slot);
    }

    ia64_rse_sync_frame_out(env);
    ia64_rse_new_frame(env, growth, ra);
    env->cfm_sof = new_sof;
    env->cfm_sol = new_sol;
    env->cfm_sor = new_sor;
    if (new_sof > old_sof) {
        ia64_rse_sync_frame_in_range(env, old_sof, new_sof - old_sof);
    }
    ia64_rse_invalidate_stacked_alat(env);

    if (r1 != 0) {
        env->gr[r1] = env->ar_pfs;
        ia64_gr_nat_set(env, r1, false);
        ia64_rse_mark_gr_dirty(env, r1);
    }
    ia64_rse_check(env, "alloc");
    IA64_TRACE_RSE_STATE(env, "alloc");
}

void ia64_rse_cover(CPUIA64State *env)
{
    if (!(env->psr & IA64_PSR_IC)) {
        env->cr_ifs = IA64_IFS_V | ia64_rse_current_cfm(env);
    }
    ia64_rse_sync_frame_out(env);
    ia64_rse_preserve_frame(env, env->cfm_sof);
    env->cfm_sof = 0;
    env->cfm_sol = 0;
    env->cfm_sor = 0;
    ia64_rse_reset_rotations(env);
    ia64_rse_invalidate_stacked_alat(env);
    ia64_rse_check(env, "cover");
    IA64_TRACE_RSE_STATE(env, "cover");
}

void ia64_rse_flush(CPUIA64State *env, uintptr_t ra)
{

    /*
     * Spill every dirty register and intervening NaT collection
     * (SDM Vol.2 6.5.4).  Each completed store updates BSPSTORE and
     * the partitions, so a faulting store restarts cleanly on the
     * issuing instruction.
     */
    while (env->rse.rse_dirty + env->rse.rse_dirty_nat > 0) {
        ia64_rse_store_one(env, ra);
        ia64_rse_interrupt_window(env);
    }
    ia64_rse_check(env, "flushrs");
    IA64_TRACE_RSE_STATE(env, "flushrs");
}

void ia64_rse_load(CPUIA64State *env, uint64_t fault_ip, uint64_t raw,
                   uint32_t slot, uintptr_t ra)
{
    uint64_t loadrs_bytes = ((env->ar_rsc >> IA64_RSC_LOADRS_SHIFT) &
                             IA64_RSC_LOADRS_MASK) & ~7ULL;
    int32_t words = loadrs_bytes >> 3;
    int32_t words_to_load;

    if ((env->ar_rsc & IA64_RSC_MODE) != 0 ||
        (env->cfm_sof != 0 && loadrs_bytes != 0)) {
        ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip, raw, slot);
    }
    if (ia64_rse_register_words_below(env->ar_bsp, words) >
        IA64_STACKED_GR_COUNT) {
        /*
         * This Illegal Operation fault precedes every mandatory backing-store
         * access and must not expose a lower-priority translation fault.
         */
        ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip, raw, slot);
    }

    /*
     * SDM Vol.2 6.5.4: ensure the backing store between BSP and the
     * tear point is present and dirty in the physical file; everything
     * below the tear point becomes invalid.
     */
    words_to_load = words - (env->rse.rse_clean + env->rse.rse_clean_nat +
                             env->rse.rse_dirty + env->rse.rse_dirty_nat);
    if (words_to_load >= 0) {
        int64_t live;
        uint64_t bspload;

        env->rse.rse_dirty_nat += env->rse.rse_clean_nat;
        env->rse.rse_dirty += env->rse.rse_clean;
        env->rse.rse_clean = 0;
        env->rse.rse_clean_nat = 0;
        env->ar_bspstore = env->ar_bsp -
            (int64_t)(env->rse.rse_dirty + env->rse.rse_dirty_nat) * 8;
        live = (int64_t)env->rse.rse_clean + env->rse.rse_clean_nat +
               env->rse.rse_dirty + env->rse.rse_dirty_nat;
        bspload = env->ar_bsp - (live + 1) * 8;
        while (words_to_load > 0) {
            if (env->rse.rse_dirty == IA64_STACKED_GR_COUNT &&
                ia64_rse_collect_bit(bspload) != 63) {
                /* More registers than fit in the physical file. */
                ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip,
                                       raw, slot);
            }
            if (ia64_rse_load_one(env, bspload, ra)) {
                env->rse.rse_dirty++;
                env->rse.rse_clean--;
            } else {
                env->rse.rse_dirty_nat++;
                env->rse.rse_clean_nat--;
            }
            env->ar_bspstore = env->ar_bsp -
                (int64_t)(env->rse.rse_dirty + env->rse.rse_dirty_nat) * 8;
            /* load_one added exactly one word to the live partitions. */
            bspload -= 8;
            words_to_load--;
            ia64_rse_interrupt_window(env);
        }
    } else {
        uint64_t tear = env->ar_bsp - loadrs_bytes;

        env->rse.rse_dirty_nat = (int32_t)((int64_t)(env->ar_bsp >> 9) -
                                       (int64_t)(tear >> 9));
        env->rse.rse_dirty = words - env->rse.rse_dirty_nat;
        env->ar_bspstore = env->ar_bsp -
            (int64_t)(env->rse.rse_dirty + env->rse.rse_dirty_nat) * 8;
        env->rse.rse_clean = 0;
        env->rse.rse_clean_nat = 0;
        env->rse.rse_invalid = IA64_STACKED_GR_COUNT -
                           (env->cfm_sof + env->rse.rse_dirty);
    }
    /*
     * SDM Vol.2 6.5.4 makes AR.RNAT undefined and places BSPSTORE and
     * RSE.BspLoad at the tear point.  SDM Vol.3 defines rse_load as taking
     * NaT bits from a dispersal register that need not be AR.RNAT.  The
     * partial collection containing the tear point has no complete memory
     * image, so retain its explicitly known bits as that internal dispersal
     * state, together with collections in the dirty range up to BSP.  A
     * mov-from-RNAT remains architecturally undefined after detachment.
     */
    ia64_rse_rnat_writeback_capture_loadrs(env);
    ia64_rse_rnat_shadow_stash(env);
    ia64_rse_load_rnat_stash(env, UINT64_MAX);
    ia64_rse_rnat_shadow_retain_range(env,
                                      env->ar_bspstore & ~0x1ffULL,
                                      env->ar_bsp);
    ia64_rse_rnat_detach(env, "loadrs", false);
    ia64_rse_check(env, "loadrs");
    IA64_TRACE_RSE_STATE(env, "loadrs");
}

/* ---- Loop branch helpers ---- */

bool ia64_rse_br_cexit(CPUIA64State *env)
{
    uint64_t lc = env->ar_lc;
    uint64_t ec = env->ar_ec;
    bool active = lc != 0 || ec > 1;

    if (lc != 0) {
        env->ar_lc = lc - 1;
        env->pr[IA64_PR_LAST] = 1;
        ia64_rotate_loop_regs(env);
    } else if (ec != 0) {
        env->ar_ec = ec - 1;
        env->pr[IA64_PR_LAST] = 0;
        ia64_rotate_loop_regs(env);
    } else {
        env->pr[IA64_PR_LAST] = 0;
    }

    return !active;
}

bool ia64_rse_br_ctop(CPUIA64State *env)
{
    uint64_t lc = env->ar_lc;
    uint64_t ec = env->ar_ec;
    bool active = lc != 0 || ec > 1;

    if (lc != 0) {
        env->ar_lc = lc - 1;
        env->pr[IA64_PR_LAST] = 1;
        ia64_rotate_loop_regs(env);
    } else if (ec != 0) {
        env->ar_ec = ec - 1;
        env->pr[IA64_PR_LAST] = 0;
        ia64_rotate_loop_regs(env);
    } else {
        env->pr[IA64_PR_LAST] = 0;
    }

    return active;
}

static bool ia64_update_while_loop(CPUIA64State *env, uint32_t qp)
{
    bool kernel_active = env->pr[qp & 63];
    bool pipeline_active = kernel_active || env->ar_ec > 1;

    if (kernel_active) {
        env->pr[IA64_PR_LAST] = 0;
        ia64_rotate_loop_regs(env);
    } else if (env->ar_ec != 0) {
        env->ar_ec--;
        env->pr[IA64_PR_LAST] = 0;
        ia64_rotate_loop_regs(env);
    } else {
        env->pr[IA64_PR_LAST] = 0;
    }

    return pipeline_active;
}

bool ia64_rse_br_wexit(CPUIA64State *env, uint32_t qp)
{
    return !ia64_update_while_loop(env, qp);
}

bool ia64_rse_br_wtop(CPUIA64State *env, uint32_t qp)
{
    return ia64_update_while_loop(env, qp);
}

void ia64_rse_clrrrb(CPUIA64State *env, uint32_t predicate_only)
{
    /*
     * clrrrb resets the rename bases (SDM Vol.2 table 6-2 notes; Vol.3
     * clrrrb).  The stacked physical registers do not move, so the
     * virtual view is re-derived under the new mapping.
     */
    ia64_rse_sync_frame_out(env);
    if (predicate_only) {
        ia64_set_cfm_rrb_pr(env, 0);
    } else {
        env->cfm_rrb_gr = 0;
        ia64_set_cfm_rrb_fr(env, 0);
        ia64_set_cfm_rrb_pr(env, 0);
    }
    ia64_rse_sync_frame_in(env);
    ia64_invalidate_stacked_alat(env);
    ia64_rse_check(env, "clrrrb");
}


uint64_t ia64_rse_cloop_zero_st1(CPUIA64State *env, uint32_t base_reg,
                                 uint32_t mmu_idx, uint32_t max_stores,
                                 uintptr_t ra)
{
    uint64_t lc = env->ar[IA64_AR_LC];
    uint64_t done = 0;
    uint64_t limit = MIN(lc, (uint64_t)max_stores);

    if (lc == 0 || limit == 0) {
        return 0;
    }

    while (done < limit) {
        uint64_t addr = env->gr[base_reg];
        uint64_t page_left = TARGET_PAGE_SIZE - (addr & (TARGET_PAGE_SIZE - 1));
        uint64_t span = MIN(limit - done, page_left);
        void *host = NULL;

        /*
         * The helper is called from br.cloop after the current loop-body store.
         * Each future store is reached by a taken branch first, so LC has
         * already been decremented before a faulting store is observed.
         */
        env->ar[IA64_AR_LC] = lc - done - 1;
        env->gr[base_reg] = addr;

        if (ia64_exec_probe_host(env, addr, (int)span, MMU_DATA_STORE,
                                 mmu_idx, &host, ra)) {
            ia64_alat_write_begin(env);
            memset(host, 0, span);
            ia64_alat_write_end(env, addr, (uint32_t)span);
            env->gr[base_reg] = addr + span;
            done += span;
            env->ar[IA64_AR_LC] = lc - done;
            continue;
        }

        ia64_exec_store_mmuidx(env, addr, 0, 1, false, mmu_idx, ra);
        env->gr[base_reg] = addr + 1;
        done++;
        env->ar[IA64_AR_LC] = lc - done;
    }

    if (done < lc) {
        env->ar[IA64_AR_LC] = lc - done - 1;
        return 1;
    }
    return 0;
}
