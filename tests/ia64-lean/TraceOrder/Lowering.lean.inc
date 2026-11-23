/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TraceOrder.Lowering

abbrev Role := IA64Ordering.Access
abbrev Mask := BitVec 8

def isLoad : Role → Bool
  | .load => true
  | .store => false

def roleOf (k : Kind) : Role :=
  if k.write then .store else .load

def pairMask : Role → Role → Mask
  | .load, .load => ldLd
  | .store, .load => stLd
  | .load, .store => ldSt
  | .store, .store => stSt

-- TCG filters default requests by its selected target's ordering
-- contract. This projection leaves that contract parametric. Only the
-- low four TCG IR pair bits are taken as explicit ordering guarantees.
def requiredDefault (implicitOrder : Mask) (request : Mask) : Mask :=
  let missing := request &&& guestDefault &&& ~~~implicitOrder
  if missing = 0 then 0 else missing ||| sc

def loweredPair (implicitOrder : Mask) (before after : Role) (acq rel mf : Bool) : Bool :=
  let emitted := requiredDefault implicitOrder (if isLoad after then defaultLoad else defaultStore)
    ||| (if acq then acquire else 0) ||| (if rel then release else 0) ||| (if mf then fence else 0)
  ((implicitOrder ||| emitted) &&& pairMask before after) != 0

-- Ordinary ld.acq/st.rel use only their architecturally available
-- qualifiers. Semaphore behavior is a separate hypothesis below.
theorem ordinaryPairSound (implicitOrder : Mask) (a b : Role) (acq rel mf : Bool) :
    ((isLoad a && acq) || (!isLoad b && rel) || mf) = true →
    loweredPair implicitOrder a b acq rel mf = true := by
  cases a <;> cases b <;> cases acq <;> cases rel <;> cases mf <;>
    unfold loweredPair requiredDefault pairMask <;>
    simp only [isLoad, acquire, release, fence, guestDefault, defaultLoad, defaultStore,
      ldLd, stLd, ldSt, stSt, allPairs, sc] <;> bv_decide

def Ordinary (e : Access Cpu Loc) : Prop :=
  (e.kind = .load ∧ e.order ≠ .release) ∨ (e.kind = .store ∧ e.order ≠ .acquire)

def Admitted (x : Execution Event Cpu Loc) : Prop :=
  ∀ e, (x.event e).kind ≠ .fence →
    Ordinary (x.event e) ∨ (x.event e).kind.rmw = true

def EmittedOrdering (implicitOrder : Mask) (x : Execution Event Cpu Loc) (semaphoreEdges : Edge Event)
    (a b : Event) : Prop :=
  semaphoreEdges a b ∨
    (x.po a b ∧ (x.event a).kind ≠ .fence ∧ (x.event b).kind ≠ .fence ∧
      ∃ mf : Bool, (mf = true → FenceBetween x a b) ∧
        loweredPair implicitOrder (roleOf (x.event a).kind) (roleOf (x.event b).kind)
          ((x.event a).order == .acquire) ((x.event b).order == .release) mf = true)

theorem ordinaryCovers (implicitOrder : Mask) (x : Execution Event Cpu Loc) (a b : Event)
    (oa : Ordinary (x.event a)) (ob : Ordinary (x.event b))
    (wanted : GuestOrder x a b) : EmittedOrdering implicitOrder x extra a b := by
  rcases wanted with ⟨po, fa, fb, acq | rel | mf⟩
  · refine Or.inr ⟨po, fa, fb, false, (fun invalid => nomatch invalid), ?_⟩
    apply ordinaryPairSound
    rcases oa with ⟨load, _⟩ | ⟨_, invalid⟩
    · simp [roleOf, isLoad, load, Kind.write, acq]
    · exact False.elim (invalid acq)
  · refine Or.inr ⟨po, fa, fb, false, (fun invalid => nomatch invalid), ?_⟩
    apply ordinaryPairSound
    rcases ob with ⟨_, invalid⟩ | ⟨store, _⟩
    · exact False.elim (invalid rel)
    · simp [roleOf, isLoad, store, Kind.write, rel]
  · refine Or.inr ⟨po, fa, fb, true, (fun _ => mf), ?_⟩
    apply ordinaryPairSound
    simp

-- Successful semaphore paths supply their architectural edges and the
-- read/write pairing as part of the TCG execution hypothesis. The ordinary part
-- discharges its ordering edges from the extracted TCG pair masks.
theorem coherentWbRefinement (implicitOrder : Mask) (x : Execution Event Cpu Loc)
    (v : Visibility Event Cpu) (semaphoreEdges : Edge Event) (supported : Admitted x)
    (semaphoreOrdering : ∀ a b, GuestOrder x a b →
      ((x.event a).kind.rmw = true ∨ (x.event b).kind.rmw = true) → semaphoreEdges a b)
    (host : IRExecution x v (EmittedOrdering implicitOrder x semaphoreEdges)) : GuestExecution x v := by
  apply refinesGuest x v (EmittedOrdering implicitOrder x semaphoreEdges) ?_ host
  intro a b wanted
  rcases supported a wanted.2.1 with oa | rmwA
  · rcases supported b wanted.2.2.1 with ob | rmwB
    · exact ordinaryCovers implicitOrder x a b oa ob wanted
    · exact Or.inl (semaphoreOrdering a b wanted (Or.inr rmwB))
  · exact Or.inl (semaphoreOrdering a b wanted (Or.inl rmwA))

theorem ordinaryWbForwarding {U : Type} (implicitOrder : Mask) (x : Execution Event Cpu Loc)
    (v : Visibility Event Cpu) (m : Forward.Image Event U)
    (ordinary : ∀ e, Ordinary (x.event e))
    (ir : Forward.IRExecution x v m (EmittedOrdering implicitOrder x (fun _ _ => False))) :
    Forward.GuestExecution x v m := by
  apply Forward.refinesGuest ?_ ir
  intro a b ordered
  exact ordinaryCovers implicitOrder x a b (ordinary a) (ordinary b) ordered

theorem observationGraph :
    (∀ {Event Cpu Loc : Type} (implicitOrder : Mask)
      (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
      (semEdges : Edge Event), Admitted x →
      (∀ a b, GuestOrder x a b →
        ((x.event a).kind.rmw = true ∨ (x.event b).kind.rmw = true) → semEdges a b) →
      IRExecution x v (EmittedOrdering implicitOrder x semEdges) → GuestExecution x v) ∧
    (∀ {Event Cpu Loc U : Type} (implicitOrder : Mask)
      (x : Execution Event Cpu Loc) (v : Visibility Event Cpu) (m : Forward.Image Event U),
      (∀ e, Ordinary (x.event e)) →
      Forward.IRExecution x v m (EmittedOrdering implicitOrder x (fun _ _ => False)) →
      Forward.GuestExecution x v m) :=
  ⟨coherentWbRefinement, ordinaryWbForwarding⟩

-- A failed release compare still carries .rel. This is the source
-- condition along both excess-CCV paths checked by Ordering/check.py.
def narrowInlineEdge (implicitOrder : Mask) : Bool :=
  loweredPair implicitOrder .store .load false true false

def narrowHelperEdge (implicitOrder : Mask) : Bool :=
  (implicitOrder ||| release) &&& stLd != 0 || requiredDefault implicitOrder runtimeLoad != 0

-- This TCG contract supplies the other three default pairs but no
-- store-before-load edge. The host machine code is outside this theorem.
def threePairDefault : Mask := ldLd ||| ldSt ||| stSt

theorem tcgNarrowReleaseHole :
    narrowInlineEdge threePairDefault = false ∧
    narrowHelperEdge threePairDefault = false := by decide

end TraceOrder.Lowering
