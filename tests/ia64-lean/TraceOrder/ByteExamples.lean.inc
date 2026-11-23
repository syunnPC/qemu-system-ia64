/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TraceOrder.ByteExamples

open Forward
open Lowering

set_option synthInstance.maxSize 256
set_option maxRecDepth 4096

abbrev E := Fin 4
abbrev Cpu := Fin 2
abbrev Physical := Fin 2

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

def access (e : E) : Access Cpu (Fin 2) :=
  match e.val with
  | 0 => ⟨some 0, some 0, .store, .release, 0xffff⟩
  | 1 => ⟨some 1, some 1, .store, .release, 0x77⟩
  | 2 => ⟨some 1, some 0, .load, .acquire, 0x0077⟩
  | _ => ⟨some 1, some 0, .load, .plain, 0xff77⟩

-- Read sources and coherence use shared byte coverage in this graph.
-- The whole-cell relation fields have no overlap information.
def overlap : Execution E Cpu (Fin 2) where
  event := access
  po a b := 1 ≤ a ∧ a < b
  dependency := fun _ _ => False
  rf := fun _ _ => False
  co := fun _ _ => False
  rmw := fun _ _ => False

def image : Image E Physical :=
  ⟨fun e byte =>
    match e.val with
    | 0 => some 0xff
    | 1 => if byte = 0 then some 0x77 else none
    | 2 => some (if byte = 0 then 0x77 else 0)
    | _ => some (if byte = 0 then 0x77 else 0xff),
    fun _ => 0⟩

-- Each read has one time regardless of byte. Each observer has one
-- publication time per write, independent of the size of that write.
def clock : Visibility E Cpu :=
  ⟨fun e => match e.val with | 0 => 1 | 1 => 2 | 2 => 4 | _ => 12,
    fun p e =>
      if e = 0 then 7 + p.val else 9 + (1 - p.val)⟩

macro "byte_decide" : tactic => `(tactic|
  (simp only [overlap, image, access, clock, Reader, Own, WriteAt, At,
    Buffered, LocallyVisible, StillLocal, PublishedBefore, LatestPublished, Ordinary,
    GuestOrder, FenceBetween, EmittedOrdering, loweredPair, requiredDefault,
    roleOf, isLoad, pairMask, acquire, release, fence, guestDefault,
    defaultLoad, defaultStore, ldLd, ldSt, stLd, stSt, allPairs, sc,
    WritesRespect, AgreeOnWrites, AgreeOnOverlap, OrderedWrite, Kind.write,
    Visibility.localTime, Visibility.remoteTime];
   native_decide))

theorem localLow : IRValue overlap clock image 1 2 0 0x77 :=
  .forwarded 1 (by byte_decide) (by byte_decide) rfl

theorem earlyHigh : IRValue overlap clock image 1 2 1 0 :=
  .initial (by byte_decide) (by byte_decide)

theorem lateLow : IRValue overlap clock image 1 3 0 0x77 :=
  .published 1 (by byte_decide) (by byte_decide) rfl

theorem lateHigh : IRValue overlap clock image 1 3 1 0xff :=
  .published 0 (by byte_decide) (by byte_decide) rfl

theorem mixedPositive :
    Forward.IRExecution overlap clock image (EmittedOrdering 0 overlap (fun _ _ => False)) := by
  constructor
  · constructor <;> byte_decide
  · byte_decide
  · byte_decide
  · byte_decide
  · byte_decide
  · byte_decide
  · constructor <;> byte_decide
  · intro p r u b reader bytes
    have which : p = 1 ∧ (r = 2 ∨ r = 3) := by
      have all : ∀ p r u, Reader overlap image p r u → p = 1 ∧ (r = 2 ∨ r = 3) := by
        byte_decide
      exact all _ _ _ reader
    have units : u = 0 ∨ u = 1 := by
      exact (by
        have all : ∀ u : Physical, u = 0 ∨ u = 1 := by native_decide
        exact all u)
    rcases which with ⟨rfl, rfl | rfl⟩ <;> rcases units with rfl | rfl
    · have value : b = 0x77 := (Option.some.inj bytes).symm
      subst b
      exact localLow
    · have value : b = 0 := (Option.some.inj bytes).symm
      subst b
      exact earlyHigh
    · have value : b = 0x77 := (Option.some.inj bytes).symm
      subst b
      exact lateLow
    · have value : b = 0xff := (Option.some.inj bytes).symm
      subst b
      exact lateHigh

theorem mixedGuest : Forward.GuestExecution overlap clock image :=
  ordinaryWbForwarding 0 overlap clock image (by byte_decide) mixedPositive

example :
    clock.remoteTime 0 0 < clock.remoteTime 0 1 ↔
      clock.remoteTime 1 0 < clock.remoteTime 1 1 :=
  (mixedGuest.agreedOverlap 0 1 0 1 0 (by decide)
    (by byte_decide) (by byte_decide)).2

-- The high byte cannot come from CPU 0's write at the earlier read's
-- time, even though the low byte has arrived through local forwarding.
theorem noEarlyRemoteHigh : ¬ IRValue overlap clock image 1 2 1 0xff := by
  intro selected
  cases selected with
  | forwarded w here _ _ =>
    have absent : ∀ w, ¬Buffered overlap clock image 1 w 2 1 := by byte_decide
    exact absent w here
  | published w _ here _ =>
    have absent : ∀ w, ¬PublishedBefore overlap clock image 1 w 2 1 := by byte_decide
    exact absent w here.1

end TraceOrder.ByteExamples
