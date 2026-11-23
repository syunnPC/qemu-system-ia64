/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TraceOrder.ForwardExamples

open Forward
open Lowering

set_option synthInstance.maxSize 256
set_option maxRecDepth 4096

abbrev E := Fin 7
abbrev Cpu := Fin 2
abbrev Cell := Fin 3

local instance finiteExists (p : Fin n → Prop) [DecidablePred p] :
    Decidable (∃ i, p i) :=
  decidable_of_iff (¬ ∀ i, ¬ p i) Classical.not_forall_not

local instance boolExists (p : Bool → Prop) [DecidablePred p] :
    Decidable (∃ i, p i) :=
  decidable_of_iff (p false ∨ p true) (by
    constructor
    · rintro (h | h)
      · exact ⟨false, h⟩
      · exact ⟨true, h⟩
    · rintro ⟨b, h⟩
      cases b
      · exact Or.inl h
      · exact Or.inr h)

def access (stale : Bool) (e : E) : Access Cpu Cell :=
  match e.val with
  | 0 => ⟨none, some 0, .store, .plain, 0⟩
  | 1 => ⟨some 0, some 0, .store, .plain, 1⟩
  | 2 => ⟨some 0, some 1, .store, .plain, 1⟩
  | 3 => ⟨some 0, some 1, .load, .plain, 1⟩
  | 4 => ⟨some 0, some 2, .store, .release, 1⟩
  | 5 => ⟨some 1, some 2, .load, .acquire, 1⟩
  | _ => ⟨some 1, some 0, .load, .plain, if stale then 0 else 1⟩

-- CPU 0 stores data and scratch, reads its own scratch, and enters the
-- release branch. CPU 1 enters the data-read branch after its acquire.
-- The edges 3→4 and 5→6 express the conditional execution.
def branchGraph (stale : Bool) : Execution E Cpu Cell where
  event := access stale
  po a b := (1 ≤ a ∧ a < b ∧ b ≤ 4) ∨ (5 ≤ a ∧ a < b)
  dependency a b := (a = 3 ∧ b = 4) ∨ (a = 5 ∧ b = 6)
  rf a b := (a = 2 ∧ b = 3) ∨ (a = 4 ∧ b = 5) ∨
    (a = (if stale then 0 else 1) ∧ b = 6)
  co a b := a = 0 ∧ b = 1
  rmw := fun _ _ => False

def branchImage (stale : Bool) : Image E Cell :=
  ⟨fun e c => if (access stale e).location = some c then some (access stale e).value else none,
    fun _ => 0⟩

def clock : Visibility E Cpu :=
  ⟨fun e => match e.val with
      | 0 => 0 | 1 => 2 | 2 => 4 | 3 => 6 | 4 => 8 | 5 => 17 | _ => 18,
    fun p e => match e.val with
      | 0 => 1 | 1 => 9 + p.val | 2 => 11 + p.val | 4 => 13 + p.val | _ => 20⟩

def takeBranch (value : Nat) : Bool := value != 0

def conditionalTail (value : Nat) (next : E) : List E :=
  if takeBranch value then [next] else []

macro "forward_decide" : tactic => `(tactic|
  (simp only [branchGraph, branchImage, access, clock, Reader, Own, WriteAt, At,
    Buffered, LocallyVisible, StillLocal, PublishedBefore, LatestPublished, Ordinary,
    GuestOrder, FenceBetween, EmittedOrdering, loweredPair, requiredDefault,
    roleOf, isLoad, pairMask, acquire, release, fence, guestDefault,
    defaultLoad, defaultStore, ldLd, ldSt, stLd, stSt, allPairs, sc,
    WritesRespect, AgreeOnWrites, AgreeOnOverlap, OrderedWrite, Kind.write,
    Visibility.localTime, Visibility.remoteTime];
   native_decide))

theorem scratchForwards : IRValue (branchGraph false) clock (branchImage false) 0 3 1 1 := by
  exact .forwarded 2 (by forward_decide) (by forward_decide) rfl

theorem takenBranches :
    conditionalTail ((branchGraph false).event 3).value 4 = [4] ∧
    conditionalTail ((branchGraph false).event 5).value 6 = [6] ∧
    conditionalTail 0 4 = [] ∧ conditionalTail 0 6 = [] ∧
    (branchGraph false).dependency 3 4 ∧ (branchGraph false).dependency 5 6 := by
  simp [conditionalTail, takeBranch, branchGraph, access]

theorem branchPositive :
    Forward.IRExecution (branchGraph false) clock (branchImage false)
      (EmittedOrdering 0 (branchGraph false) (fun _ _ => False)) := by
  constructor
  · constructor <;> forward_decide
  · forward_decide
  · forward_decide
  · forward_decide
  · forward_decide
  · forward_decide
  · constructor <;> forward_decide
  · intro p r u b reader bytes
    have selected : (r = 3 ∧ p = 0 ∧ u = 1) ∨ (r = 5 ∧ p = 1 ∧ u = 2) ∨
        (r = 6 ∧ p = 1 ∧ u = 0) := by
      exact (by
        have all : ∀ p r u, Reader (branchGraph false) (branchImage false) p r u →
            (r = 3 ∧ p = 0 ∧ u = 1) ∨ (r = 5 ∧ p = 1 ∧ u = 2) ∨
            (r = 6 ∧ p = 1 ∧ u = 0) := by forward_decide
        exact all _ _ _ reader)
    rcases selected with ⟨rfl, rfl, rfl⟩ | ⟨rfl, rfl, rfl⟩ | ⟨rfl, rfl, rfl⟩
    · have h : b = 1 := (Option.some.inj bytes).symm
      subst b
      exact scratchForwards
    · have h : b = 1 := (Option.some.inj bytes).symm
      subst b
      exact .published 4 (by forward_decide) (by forward_decide) rfl
    · have h : b = 1 := (Option.some.inj bytes).symm
      subst b
      exact .published 1 (by forward_decide) (by forward_decide) rfl

theorem branchGuest :
    Forward.GuestExecution (branchGraph false) clock (branchImage false) :=
  ordinaryWbForwarding 0 (branchGraph false) clock (branchImage false)
    (by forward_decide) branchPositive

theorem stalePublishedBranchForbidden (v : Visibility E Cpu)
    (ir : Forward.IRExecution (branchGraph true) v (branchImage true)
      (EmittedOrdering 0 (branchGraph true) (fun _ _ => False)))
    (flag : LatestPublished (branchGraph true) v (branchImage true) 1 4 5 2)
    (stale : LatestPublished (branchGraph true) v (branchImage true) 1 0 6 0) : False := by
  have guest := ordinaryWbForwarding 0 (branchGraph true) v (branchImage true)
    (by forward_decide) ir
  exact releaseCarriesStore (data := (1 : E)) guest (by forward_decide)
    (by forward_decide) (by forward_decide) rfl flag.1 stale
    (ir.coherence 1 0 1 (by forward_decide))

theorem forwardedOwnStoreDoesNotAllowStaleRemote :
    ¬Forward.IRValue (branchGraph true) clock (branchImage true) 1 6 0 0 := by
  intro stale
  cases stale with
  | forwarded other pending _ _ =>
    exact (by
      have absent : ∀ w, ¬Buffered (branchGraph true) clock (branchImage true) 1 w 6 0 :=
        by forward_decide
      exact absent other pending)
  | published old _ latest value =>
    have isInitial : old = 0 := by
      have all : ∀ w, PublishedBefore (branchGraph true) clock (branchImage true) 1 w 6 0 →
          (branchImage true).value w 0 = some 0 → w = 0 := by forward_decide
      exact all _ latest.1 value
    subst old
    exact latest.2 1 (by forward_decide) (by forward_decide)
  | initial _ nonePublished =>
    exact nonePublished 1 (by forward_decide)

def queuedGraph : Execution E Cpu Cell :=
  {branchGraph false with event := fun e =>
    if e = 1 then ⟨some 0, some 1, .store, .plain, 2⟩ else access false e}

def queuedImage : Image E Cell :=
  ⟨fun e u =>
    if (queuedGraph.event e).location = some u then some (queuedGraph.event e).value else none,
    fun _ => 0⟩

macro "queue_decide" : tactic => `(tactic|
  (simp only [queuedGraph, queuedImage, branchGraph, access, clock, Own, WriteAt,
    At, Buffered, Kind.write, Visibility.localTime, Visibility.remoteTime];
   native_decide))

theorem olderScratchStillPending : Buffered queuedGraph clock queuedImage 0 1 3 1 := by
  queue_decide

theorem queuedScratchRead : IRValue queuedGraph clock queuedImage 0 3 1 1 := by
  exact .forwarded 2 (by queue_decide) (by queue_decide) rfl

end TraceOrder.ForwardExamples
