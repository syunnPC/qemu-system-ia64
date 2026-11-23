/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TraceOrder.Examples

open Lowering

set_option synthInstance.maxSize 512
set_option maxRecDepth 4096

local instance boolForall (p : Bool → Prop) [DecidablePred p] :
    Decidable (∀ i, p i) :=
  decidable_of_iff (p false ∧ p true) (by
    constructor
    · intro h b
      cases b
      · exact h.1
      · exact h.2
    · intro h
      exact ⟨h false, h true⟩)

local instance finiteExists (p : Fin n → Prop) [DecidablePred p] :
    Decidable (∃ i, p i) :=
  decidable_of_iff (¬ ∀ i, ¬ p i) Classical.not_forall_not

local instance boolExists (p : Bool → Prop) [DecidablePred p] :
    Decidable (∃ i, p i) :=
  decidable_of_iff (¬ ∀ i, ¬ p i) Classical.not_forall_not

def noEdge (_ _ : E) : Prop := False

def memory (cpu : Option Bool) (loc : Bool) (kind : Kind) (order : Order) (value : Nat) :
    Access Bool Bool := ⟨cpu, some loc, kind, order, value⟩

def mpAccess (stale : Bool) (e : Fin 6) : Access Bool Bool :=
  match e.val with
  | 0 => memory none false .store .plain 0
  | 1 => memory none true .store .plain 0
  | 2 => memory (some false) false .store .release 1
  | 3 => memory (some false) true .store .release 1
  | 4 => memory (some true) true .load .acquire 1
  | _ => memory (some true) false .load .plain (if stale then 0 else 1)

def mp (stale : Bool) : Execution (Fin 6) Bool Bool where
  event := mpAccess stale
  po a b := (a = 2 ∧ b = 3) ∨ (a = 4 ∧ b = 5)
  dependency := noEdge
  rf a b := (a = 3 ∧ b = 4) ∨ (a = (if stale then 0 else 2) ∧ b = 5)
  co a b := (a = 0 ∧ b = 2) ∨ (a = 1 ∧ b = 3)
  rmw := noEdge

def sbAccess (e : Fin 7) : Access Bool Bool :=
  match e.val with
  | 0 => memory none false .store .plain 0
  | 1 => memory none true .store .plain 0
  | 2 => memory (some false) false .store .plain 1
  | 3 => memory (some false) true .failedCompare .release 0
  | 4 => memory (some true) true .store .plain 1
  | 5 => ⟨some true, none, .fence, .plain, 0⟩
  | _ => memory (some true) false .load .plain 0

def narrow : Execution (Fin 7) Bool Bool where
  event := sbAccess
  po a b := (a = 2 ∧ b = 3) ∨ (a = 4 ∧ (b = 5 ∨ b = 6)) ∨ (a = 5 ∧ b = 6)
  dependency := noEdge
  rf a b := (a = 1 ∧ b = 3) ∨ (a = 0 ∧ b = 6)
  co a b := (a = 0 ∧ b = 2) ∨ (a = 1 ∧ b = 4)
  rmw := noEdge

def rmwAccess (e : Fin 3) : Access Bool Bool :=
  match e.val with
  | 0 => memory none false .store .plain 0
  | 1 => memory (some false) false .rmwRead .acquire 0
  | _ => memory (some false) false .rmwWrite .acquire 1

def exchange : Execution (Fin 3) Bool Bool where
  event := rmwAccess
  po a b := a = 1 ∧ b = 2
  dependency := noEdge
  rf a b := a = 0 ∧ b = 1
  co a b := a = 0 ∧ b = 2
  rmw a b := a = 1 ∧ b = 2

def visibility (rank : Event → Nat) : Visibility Event Bool :=
  ⟨fun e => 2 * rank e, fun _ e => 2 * rank e + 1⟩

def fusedRank (e : Fin 6) : Nat :=
  match e.val with
  | 2 => 5
  | 3 => 2
  | 4 => 3
  | 5 => 4
  | _ => e.val

-- Within the bulk helper's span there is no separate IR release between
-- its first and later guest byte stores.
def fusedOrder : Edge (Fin 6) := fun a b => a = 4 ∧ b = 5

def narrowRank (e : Fin 7) : Nat :=
  match e.val with
  | 2 => 6
  | 3 => 2
  | 4 => 3
  | 5 => 4
  | 6 => 5
  | _ => e.val

-- Both paths have the same omitted pair edge for this TCG contract.
def narrowOrder := EmittedOrdering threePairDefault narrow noEdge

macro "graph_decide" : tactic => `(tactic|
  (simp only [mp, narrow, exchange, mpAccess, sbAccess, rmwAccess, memory,
    noEdge, SameLocation, ReadsFrom, FromRead, AtomicPairs, Visibility.localTime,
    Visibility.remoteTime, AgreeOnWrites, WritesRespect, LocalMemoryOrder,
    Observed, FixedEdges, GuestOrder, FenceBetween, Admitted, Ordinary,
    Kind.read, Kind.write, Kind.rmw, visibility, narrowRank, fusedRank,
    fusedOrder, narrowOrder, EmittedOrdering];
   native_decide))

theorem mpWellFormed (stale : Bool) : WellFormed (mp stale) := by
  cases stale <;> constructor <;> graph_decide

theorem narrowWellFormed : WellFormed narrow := by
  constructor <;> graph_decide

theorem exchangeWellFormed : WellFormed exchange := by
  constructor <;> graph_decide

theorem mpPositive :
    GuestExecution (mp false) (visibility Fin.val) := by
  apply coherentWbRefinement 0 _ _ noEdge
  · graph_decide
  · graph_decide
  · constructor
    · exact mpWellFormed false
    · constructor <;> graph_decide
    · graph_decide
    · graph_decide
    · graph_decide
    · apply acyclicOfRanks _ Fin.val
      graph_decide

theorem exchangePositive :
    GuestExecution exchange (visibility Fin.val) := by
  apply coherentWbRefinement 0 _ _ exchange.rmw
  · graph_decide
  · graph_decide
  · constructor
    · exact exchangeWellFormed
    · constructor <;> graph_decide
    · graph_decide
    · graph_decide
    · graph_decide
    · apply acyclicOfRanks _ Fin.val
      graph_decide

theorem fusedAdmitted :
    IRExecution (mp true) (visibility fusedRank) fusedOrder := by
  constructor
  · exact mpWellFormed true
  · constructor <;> graph_decide
  · graph_decide
  · graph_decide
  · graph_decide
  · apply acyclicOfRanks _ fusedRank
    graph_decide

theorem narrowAdmitted :
    IRExecution narrow (visibility narrowRank) narrowOrder := by
  constructor
  · exact narrowWellFormed
  · constructor <;> graph_decide
  · graph_decide
  · graph_decide
  · graph_decide
  · apply acyclicOfRanks _ narrowRank
    graph_decide

theorem noFusedGuest : ¬ (∃ v, GuestExecution (mp true) v) := by
  intro ⟨_, guest⟩
  have w0w1 : Observed (mp true) (GuestOrder (mp true)) 2 3 := by graph_decide
  have w1r1 : Observed (mp true) (GuestOrder (mp true)) 3 4 := by graph_decide
  have r1r0 : Observed (mp true) (GuestOrder (mp true)) 4 5 := by graph_decide
  have r0w0 : Observed (mp true) (GuestOrder (mp true)) 5 2 := by graph_decide
  exact guest.noCycles 2 (.extend (.extend (.extend (.single w0w1) w1r1) r1r0) r0w0)

theorem noNarrowGuest : ¬ (∃ v, GuestExecution narrow v) := by
  intro ⟨_, guest⟩
  have wr : Observed narrow (GuestOrder narrow) 2 3 := by graph_decide
  have rw : Observed narrow (GuestOrder narrow) 3 4 := by graph_decide
  have mw : Observed narrow (GuestOrder narrow) 4 6 := by graph_decide
  have fr : Observed narrow (GuestOrder narrow) 6 2 := by graph_decide
  exact guest.noCycles 2 (.extend (.extend (.extend (.single wr) rw) mw) fr)

theorem noFusedRefinement :
    ¬ ((∃ w, FusedRelease.bulkOrdering w ∧ FusedRelease.messagePassingFailure w) →
      (∃ v, IRExecution (mp true) v fusedOrder) → (∃ v, GuestExecution (mp true) v)) := by
  intro assumed
  exact noFusedGuest (assumed FusedRelease.bulk_admits_messagePassingFailure
    ⟨visibility fusedRank, fusedAdmitted⟩)

theorem noNarrowRefinement :
    ¬ ((narrowInlineEdge threePairDefault = false ∧ narrowHelperEdge threePairDefault = false) →
      (∃ v, IRExecution narrow v narrowOrder) → (∃ v, GuestExecution narrow v)) := by
  intro assumed
  exact noNarrowGuest (assumed tcgNarrowReleaseHole ⟨visibility narrowRank, narrowAdmitted⟩)

end TraceOrder.Examples
