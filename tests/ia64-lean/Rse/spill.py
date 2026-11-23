# SPDX-License-Identifier: GPL-2.0-or-later
"""Project mandatory store dispatch and register-stack counter commits."""

from Rse.frames import function, require


def spill_updates(source):
    require(function(source, "ia64_rse_new_frame"), """
        growth -= env->rse.rse_clean;
        while (growth > 0) {
            growth -= ia64_rse_store_one(env, ra);
            ia64_rse_interrupt_window(env);
        }
        env->rse.rse_invalid = 0;
        env->rse.rse_clean = 0;
        env->rse.rse_clean_nat = 0;
    """, "ALLOC mandatory store and interrupt window")
    store = function(source, "ia64_rse_store_one")
    require(store, """
        env->ar_bspstore = bspstore + 8;
        env->rse.rse_rnat_addr = UINT64_MAX;
        env->rse.rse_rnat_defined = 0;
        env->rse.rse_dirty_nat--;
        if (ia64_rse_has_clean_partition(env)) {
            env->rse.rse_clean_nat++;
        }
        return 0;
    """, "RNAT store commit increments BSPSTORE without consuming a GR")
    require(store, """
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
    """, "payload write precedes partition bookkeeping")
    require(store, """
        collection = ia64_rse_write_collection(env, bspstore, value, defined, &previous, ra);
        trace_ia64_rse_rnat_store(env_cpu(env)->cpu_index, env->ip, bspstore,
                                 defined, previous, collection);
    """, "RNAT write precedes RNAT bookkeeping")
    return """
def spillCommitC (state : SpillCounters) (hasClean : Bool) :
    SpillWord → SpillCounters
  | .payload =>
    { state with
      bspstore := state.bspstore + 8
      dirty := state.dirty - 1
      clean := if hasClean then state.clean + 1 else state.clean
      invalid := if hasClean then state.invalid else state.invalid + 1 }
  | .collection =>
    { state with
      bspstore := state.bspstore + 8
      dirtyNat := state.dirtyNat - 1
      cleanNat := if hasClean then state.cleanNat + 1 else state.cleanNat }

def spillRetireC (state : SpillCounters) : SpillResult :=
  .retire { state with invalid := 0, clean := 0, cleanNat := 0 }

def spillRunC (hasClean : Bool) (state : SpillCounters) (needed : Nat) :
    List SpillEvent → SpillResult
  | [] => if needed = 0 then spillRetireC state else .waiting state
  | .dataFault :: _ => if needed = 0 then spillRetireC state else .dataFault state
  | .store kind interrupt :: tail =>
    if needed = 0 then spillRetireC state
    else
      let next := spillCommitC state hasClean kind
      let remaining := match kind with
        | .payload => needed - 1
        | .collection => needed
      if interrupt then .interrupt next else spillRunC hasClean next remaining tail
"""
