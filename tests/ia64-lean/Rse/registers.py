# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the register-file and ALAT projections of resident frame operations."""

from Decode.check import uncomment
from Rse.frames import function, require


def register_updates(root, source, header):
    require(function(header, "ia64_rse_mark_gr_dirty"), """
        if (reg >= IA64_STACKED_GR_BASE && reg < IA64_GR_COUNT) {
            uint32_t bit = reg - IA64_STACKED_GR_BASE;
            env->rse.rse_gr_dirty[bit / 64] |= 1ULL << (bit % 64);
        }
    """, "ALLOC stacked destination dirtiness", full=True)
    require(function(source, "ia64_rse_load_linear"), """
        uint32_t p = env->rse.rse_bol + start;
        uint64_t *dst = &env->gr[IA64_STACKED_GR_BASE + start];
        uint32_t first;
        if (p >= IA64_STACKED_GR_COUNT) { p -= IA64_STACKED_GR_COUNT; }
        first = MIN(count, IA64_STACKED_GR_COUNT - p);
        memcpy(dst, &env->rse.rse_pgr[p], first * sizeof(*dst));
        if (first < count) {
            memcpy(dst + first, env->rse.rse_pgr, (count - first) * sizeof(*dst));
        }
    """, "ALLOC linear load range", full=True)
    require(function(source, "ia64_rse_sync_frame_in_range"), """
        span = MIN(span, IA64_STACKED_GR_COUNT - p);
        memcpy(&env->gr[IA64_STACKED_GR_BASE + first], &env->rse.rse_pgr[p],
               span * sizeof(env->rse.rse_pgr[0]));
        if (likely(nat_clear)) {
            ia64_clear_bit_range(env->nat, IA64_STACKED_GR_BASE + first, span);
        } else {
            ia64_copy_bit_range(env->nat, IA64_STACKED_GR_BASE + first,
                                env->rse.rse_pgr_nat, p, span);
        }
        first += span;
    """, "ALLOC rotated load range")
    for variant in ("direct", "slow"):
        require(function(source, f"ia64_rse_sync_frame_out_{variant}"),
                "env->rse.rse_gr_dirty[0] = 0; env->rse.rse_gr_dirty[1] = 0;",
                f"frame writeback clears {variant} dirty bits")
    require(function(source, "ia64_rse_sync_frame_out"), """
        uint64_t dirty0 = env->rse.rse_gr_dirty[0];
        uint64_t dirty1 = env->rse.rse_gr_dirty[1];
        if (unlikely(dirty0 | dirty1)) {
            uint32_t sof = env->cfm_sof;
            uint32_t bol = env->rse.rse_bol;
            if (likely(((env->nat[0] >> IA64_STACKED_GR_BASE) | env->nat[1] |
                        env->rse.rse_pgr_nat[0] | env->rse.rse_pgr_nat[1]) == 0 &&
                       (env->cfm_sor == 0 || env->cfm_rrb_gr == 0))) {
                ia64_rse_sync_frame_out_direct(env, dirty0, dirty1, sof, bol);
                return;
            }
            ia64_rse_sync_frame_out_slow(env, dirty0, dirty1);
        }
    """, "COVER writeback dispatch", full=True)
    require(function(source, "ia64_rse_pgr_nat_set"), """
        uint64_t mask = 1ULL << (p % 64);
        bool old_nat = (env->rse.rse_pgr_nat[p / 64] & mask) != 0;
        if (old_nat != nat) {
            trace_ia64_rse_pgr_nat_update(env_cpu(env)->cpu_index, operation, env->ip,
                                          p, source, old_nat, nat);
        }
        if (nat) { env->rse.rse_pgr_nat[p / 64] |= mask; }
        else { env->rse.rse_pgr_nat[p / 64] &= ~mask; }
    """, "COVER physical NaT bit writer", full=True)
    require(function(source, "ia64_rse_sync_frame_out_slow"), """
        if (v < sor_regs) {
            p += rrb_gr;
            if (p >= sor_regs) { p -= sor_regs; }
        }
        p += bol;
        if (p >= IA64_STACKED_GR_COUNT) { p -= IA64_STACKED_GR_COUNT; }
        env->rse.rse_pgr[p] = env->gr[reg];
        bool reg_nat = v < 32 ? (nat[0] >> (IA64_STACKED_GR_BASE + v)) & 1
                              : (nat[1] >> (v - 32)) & 1;
    """, "COVER dirty register and NaT index")
    require(function(source, "ia64_rse_sync_frame_out_slow"),
            'ia64_rse_pgr_nat_set(env, p, reg_nat, "sync-out", reg);',
            "COVER physical NaT write")
    require(function(source, "ia64_rse_sync_frame_out_direct"), """
        ia64_rse_copy_dirty_word(env, bol, 0, dirty0);
        if (dirty1) {
            ia64_rse_copy_dirty_word(env, (bol + 64) % IA64_STACKED_GR_COUNT, 64, dirty1);
        }
    """, "COVER direct dirty words")
    require(function(source, "ia64_rse_copy_dirty_word"), """
        const uint64_t *src = &env->gr[IA64_STACKED_GR_BASE + v];
        uint32_t tail = IA64_STACKED_GR_COUNT - p;
        if (tail < 64 && (dirty >> tail)) {
            ia64_rse_copy_dirty(env->rse.rse_pgr, src + tail, dirty >> tail);
            dirty &= (UINT64_C(1) << tail) - 1;
        }
        ia64_rse_copy_dirty(&env->rse.rse_pgr[p], src, dirty);
    """, "COVER direct ring split", full=True)
    require(function(source, "ia64_rse_copy_dirty"), """
        while (dirty) {
            unsigned bit = ctz64(dirty);
            uint64_t shifted = dirty >> bit;
            if ((shifted & 15) == 15) {
                unsigned count = cto64(shifted);
                memcpy(dst + bit, src + bit, count * sizeof(*dst));
                dirty &= count == 64 ? 0 : ~(((1ULL << count) - 1) << bit);
            } else {
                dst[bit] = src[bit];
                dirty &= dirty - 1;
            }
        }
    """, "COVER copies selected dirty register data", full=True)
    require(function(source, "ia64_rse_invalidate_stacked_alat"), """
        if (unlikely(env->alat_state.alat_active_count != 0)) {
            ia64_invalidate_stacked_alat(env);
        }
    """, "stacked ALAT invalidation gate", full=True)

    alat = uncomment((root / "target/ia64/arch/alat.c").read_text(encoding="utf-8"))
    require(function(alat, "ia64_invalidate_stacked_alat"), """
        ia64_invalidate_alat_reg_range(env, IA64_STACKED_GR_BASE, IA64_GR_COUNT, false);
    """, "stacked ALAT invalidation bounds", full=True)
    require(function(alat, "ia64_invalidate_rotating_fp_alat"), """
        ia64_invalidate_alat_reg_range(env, IA64_ROTATING_FR_BASE, IA64_FR_COUNT, true);
    """, "rotating ALAT invalidation bounds", full=True)
    require(function(alat, "ia64_invalidate_alat_reg_range"), """
        uint32_t occupied = env->alat_state.alat_occupied;
        while (occupied) {
            unsigned i = ctz32(occupied);
            occupied &= occupied - 1;
            if (env->alat_state.alat[i].fp == fp &&
                env->alat_state.alat[i].reg >= first &&
                env->alat_state.alat[i].reg < last) {
                ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
            }
        }
    """, "ALAT occupied entry scan", full=True)
    require(function(alat, "ia64_alat_invalidate_entry"), """
        if (!entry->valid) { return; }
        entry->valid = false;
        env->alat_state.alat_reg_slot[entry->fp][entry->reg] = 0;
        env->alat_state.alat_occupied &=
            ~(UINT32_C(1) << (entry - env->alat_state.alat));
        if (env->alat_state.alat_active_count > 0) {
            env->alat_state.alat_active_count--;
        }
    """, "ALAT invalidation clears existing entry", full=True)
    fp = uncomment((root / "target/ia64/arch/fp.c").read_text(encoding="utf-8"))
    require(function(fp, "ia64_set_cfm_rrb_fr"), """
        env->cfm_rrb_fr = new_rrb;
        if (likely(!env->fp.rotating_fr_live)) {
            env->fp.rotating_fr_materialized_rrb = new_rrb;
            return;
        }
        ia64_invalidate_rotating_fp_alat(env);
    """, "COVER live floating rotation ALAT invalidation")

    return """
-- The supplied loaded cell abstracts the C physical register and NaT read
-- when ALLOC grows. It is unobservable through registerRefines until written.
def allocRegisterC (oldSize newSize destination reg : Nat) (pfs : Word)
    (old loaded : RegisterCell) : RegisterCell :=
  let resized :=
    if newSize > oldSize ∧ 32 + oldSize ≤ reg ∧ reg < 32 + newSize then loaded else old
  if destination ≠ 0 ∧ destination = reg then (pfs, false) else resized

def allocDirtyC (oldSize newSize : Nat) (invalid : Int) (rrb : BitVec 7)
    (destination reg : Nat) (oldDirty : Bool) : Bool :=
  let growth : Int := (newSize : Int) - oldSize
  let afterSync := if growth < 0 ∨ invalid < growth ∨ rrb ≠ 0 then false else oldDirty
  decide (32 ≤ reg ∧ reg < 128 ∧ destination = reg) || afterSync

def directCoverIndexC (bol v : Nat) : Nat :=
  let p := if v < 64 then bol else (bol + 64) % 96
  let bit := v % 64
  let tail := 96 - p
  if bit < tail then p + bit else bit - tail

def coverSavedRegisterC (sof sor rrb : BitVec 8) (bol v : U32)
    (dirty natFilesClear : Bool) (current : RegisterCell) (physical : U32 → RegisterCell) :
    U32 × RegisterCell :=
  let p := virtToPhysC sof sor rrb bol v
  let saved := if dirty then
    (current.1, if natFilesClear then (physical p).2 else current.2)
  else physical p
  (p, saved)

def alatFrameC (isCover frLive : Bool) (oldRrb : BitVec 7) (active : Nat)
    (tag : Option AlatTag) : Option AlatTag :=
  if active = 0 then tag else
    match tag with
    | none => none
    | some entry =>
      if 32 ≤ entry.reg ∧ entry.reg < 128 ∧
        (entry.fp = false ∨ isCover = true ∧ frLive = true ∧ oldRrb ≠ 0)
      then none else tag
"""
