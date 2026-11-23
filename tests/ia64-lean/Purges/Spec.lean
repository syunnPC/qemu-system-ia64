/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std

namespace IA64Purges

-- Intel Itanium SDM rev. 2.3 vol. 2, table 4-15 and §5.2.2.2.3,
-- and vol. 3, ptc.g/ptc.ga. Model one legal global purge, within a
-- stable coherence domain, with pre-existing matching TC entries.
inductive Entry where
  | present
  | pending
  | removed
  deriving DecidableEq

structure CacheView where
  data : Entry
  inst : Entry
  alatPresent : Bool

def cacheBefore : CacheView := ⟨.present, .present, true⟩

-- No local data/instruction disappearance is required until the
-- corresponding explicit or implicit serialization.
def localSerialize (cache : CacheView) (data inst : Bool) : CacheView :=
  ⟨if data && cache.data == .pending then .removed else cache.data,
   if inst && cache.inst == .pending then .removed else cache.inst,
   cache.alatPresent⟩

def remotelyVisible (cache : CacheView) (globalAlat : Bool) : Prop :=
  cache.data = .removed ∧ cache.inst = .removed ∧
  (globalAlat = true → cache.alatPresent = false)

structure Remote where
  running : Bool
  pendingWork : Bool
  resumed : Bool
  view : CacheView

structure Machine (n : Nat) where
  sourceView : CacheView
  sourceBarrierReturned : Bool
  remote : Fin n → Remote

def updateRemote (state : Machine n) (cpu : Fin n) (change : Remote → Remote) :
    Machine n :=
  { state with remote := fun query =>
      if cpu = query then change (state.remote query) else state.remote query }

def quiescent (state : Machine n) : Prop :=
  ∀ cpu, (state.remote cpu).running = false

-- Per-vCPU FIFO projection of QSIMPLEQ_INSERT_TAIL / REMOVE_HEAD.
-- This order says nothing about a different CPU's queue.
def enqueue (queue : List α) (item : α) : List α := queue ++ [item]
def dequeue (queue : List α) : Option α × List α := (queue.head?, queue.tail)

theorem newWorkFollowsFirst (first posted : α) (rest : List α) :
    (dequeue (enqueue (first :: rest) posted)).1 = some first := rfl

end IA64Purges

namespace IA64Purges

def mark (cache : CacheView) (complete clearAlat : Bool) : CacheView :=
  ⟨if complete then .removed else .pending,
   if complete then .removed else .pending,
   if clearAlat then false else cache.alatPresent⟩

def issued (n : Nat) (complete globalAlat : Bool) (running : Fin n → Bool) : Machine n :=
  ⟨mark cacheBefore complete globalAlat, false,
   fun cpu => ⟨running cpu, true, false, cacheBefore⟩⟩

def remoteWork (remote : Remote) (complete globalAlat : Bool) : Remote :=
  { remote with pendingWork := false, view := mark remote.view complete globalAlat }

-- Scheduler projection. cpu_exec_end can clear running ahead of work-list
-- draining. The source exclusive callback waits for guest execution to stop.
-- Ordinary subsequent remote guest entry is modeled only after draining this
-- work item. This guard assumes a serviced kick or an event-loop drain, no
-- CF_NOIRQ bypass and no guest entry path outside the checked event loop.
inductive Step (complete globalAlat : Bool) {n : Nat} : Machine n → Machine n → Prop
  | stop (state) (cpu : Fin n) :
      Step complete globalAlat state
        (updateRemote state cpu (fun entry => { entry with running := false }))
  | drain (state) (cpu : Fin n) (stopped : (state.remote cpu).running = false)
      (pending : (state.remote cpu).pendingWork = true) :
      Step complete globalAlat state
        (updateRemote state cpu (fun entry => remoteWork entry complete globalAlat))
  | sourceBarrier (state) (hasPeer : n > 0) (idle : quiescent state) :
      Step complete globalAlat state { state with sourceBarrierReturned := true }
  | resume (state) (cpu : Fin n) (drained : (state.remote cpu).pendingWork = false) :
      Step complete globalAlat state
        (updateRemote state cpu (fun entry => { entry with running := true, resumed := true }))
  | sourceSerialize (state) (data inst : Bool) :
      Step complete globalAlat state
        { state with sourceView := localSerialize state.sourceView data inst }

inductive Trace (complete globalAlat : Bool) {n : Nat} (start : Machine n) : Machine n → Prop
  | nil : Trace complete globalAlat start start
  | next {before after} : Trace complete globalAlat start before →
      Step complete globalAlat before after → Trace complete globalAlat start after

def cacheInvariant (state : Machine n) (globalAlat : Bool) : Prop :=
  ∀ cpu, ((state.remote cpu).pendingWork = false →
      remotelyVisible (state.remote cpu).view globalAlat) ∧
      ((state.remote cpu).resumed = true → (state.remote cpu).pendingWork = false)

theorem preservesInvariant (globalAlat : Bool)
    {before after : Machine n}
    (invariant : cacheInvariant before globalAlat)
    (step : Step true globalAlat before after) :
    cacheInvariant after globalAlat := by
  intro query
  cases step with
  | stop cpu =>
      by_cases eq : cpu = query
      · simpa [updateRemote, eq] using invariant query
      · simpa [updateRemote, eq] using invariant query
  | drain cpu stopped pending =>
      by_cases eq : cpu = query
      · subst query
        cases globalAlat <;> simp [updateRemote, remoteWork, mark, remotelyVisible]
      · simpa [updateRemote, eq] using invariant query
  | sourceBarrier peer idle => exact invariant query
  | resume cpu drained =>
      by_cases eq : cpu = query
      · subst query
        simpa [updateRemote, drained] using (invariant cpu).1 drained
      · simpa [updateRemote, eq] using invariant query
  | sourceSerialize data inst => exact invariant query

theorem traceInvariant (sourceComplete globalAlat : Bool)
    (running : Fin n → Bool) (state : Machine n)
    (trace : Trace true globalAlat (issued n sourceComplete globalAlat running) state) :
    cacheInvariant state globalAlat := by
  induction trace with
  | nil =>
      intro cpu
      simp [issued]
  | next _ step ih => exact preservesInvariant globalAlat ih step

theorem resumedPeerHasAppliedWork (sourceComplete globalAlat : Bool)
    (running : Fin n → Bool) (state : Machine n)
    (trace : Trace true globalAlat (issued n sourceComplete globalAlat running) state)
    (cpu : Fin n) (resumed : (state.remote cpu).resumed = true) :
    remotelyVisible (state.remote cpu).view globalAlat := by
  have entry := traceInvariant sourceComplete globalAlat running state trace cpu
  exact entry.1 (entry.2 resumed)

-- Quiescence is about CPU execution. This witness is about a scheduler
-- ordering assertion, and does not by itself exhibit a stale access
-- after a guest instruction observing the architectural fence.
theorem sourceBarrierDoesNotDrainPeers (sourceComplete globalAlat : Bool)
    (cpu : Fin (n + 1)) :
    ∃ state : Machine (n + 1),
      Trace true globalAlat (issued (n + 1) sourceComplete globalAlat (fun _ => false)) state ∧
      state.sourceBarrierReturned = true ∧
      (state.remote cpu).pendingWork = true ∧
      (state.remote cpu).view.data = .present := by
  let initial := issued (n + 1) sourceComplete globalAlat (fun _ => false)
  exact ⟨{ initial with sourceBarrierReturned := true },
         .next .nil (.sourceBarrier initial (by omega) (fun _ => rfl)),
         rfl, rfl, rfl⟩

end IA64Purges
