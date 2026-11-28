/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Branch.B1

open IA64Slot
set_option maxRecDepth 16384
set_option maxHeartbeats 2000000

def suppressionMask : Word :=
  (1#64 <<< 37) ||| (1#64 <<< 38) ||| (1#64 <<< 39) ||| (1#64 <<< 43) ||| (1#64 <<< 45)

structure SuppressionEntry (c : Context) (s : State) (i : Instruction) : Prop where
  native : c.ia32 = false
  tcg : c.llvm = false
  tracked : c.trackSuppression = true
  ip : s.ip = i.address
  ri : IA64Slot.SDM.ri s.psr = i.slot.number
  riFact : RISound s c.ri
  icFact : ∀ b, c.ic = some b → IA64Slot.SDM.collecting s.psr = b
  shared : c.record = false → IA64Slot.SDM.collecting s.psr = true →
    s.lastSuccess = i.address &&& ~~~15#64
  debug : c.debugDynamic = false → c.debug = IA64Slot.SDM.instructionDebug s.psr

def cSnapshotSuppression : Effect :=
  .write .savedSuppression (.band (.input .psr) (.literal cSuppressionMask))

-- Both predicate paths reach the same completion prefix after the debug request guard.
def cSuppressionPrefix (c : Context) (s : State) (i : Instruction) : State × Option Event :=
  let issued := ((emitRI c.ri i.slot.number).run s).1
  let events := ((emitDebug c.debugDynamic c.debug ⟨i.address, i.slot.number⟩).run issued).2
  match events with
  | first :: _ => (issued, some first)
  | [] =>
    let stored := if c.trackSuppression then (cSnapshotSuppression.run issued).1 else issued
    let (noted, events) := (emitSuccess c.ic c.record c.trackSuppression i.address).run stored
    (noted, events.head?)

def suppressionBeforeRequest (s : State) (i : Instruction) : State :=
  { archSuccess s i.address with savedSuppression := s.psr &&& suppressionMask }

def suppressionPrefix (s : State) (i : Instruction) : State × Option Event :=
  if IA64Slot.SDM.instructionDebug s.psr then
    (s, some (.debug ⟨i.address, i.slot.number⟩))
  else
    (suppressionBeforeRequest s i,
     if s.psr &&& suppressionMask != 0 then some .clearSuppression else none)

theorem sourceSuppressionMask : cSuppressionMask = suppressionMask := by decide

theorem trackedPrefixRefines (c : Context) (s : State) (i : Instruction)
    (entry : SuppressionEntry c s i) :
    cSuppressionPrefix c s i = suppressionPrefix s i := by
  have bound : i.slot.number < 4 := by cases i.slot <;> decide
  have actualRI := emittedRI s c.ri i.slot.number entry.riFact bound
  rw [← riUnchanged s.psr i.slot.number entry.ri] at actualRI
  have issue : (emitRI c.ri i.slot.number).run s = (s, []) := actualRI
  have debug := emittedDebug s c.debugDynamic c.debug ⟨i.address, i.slot.number⟩ entry.debug
  let snapshot := {s with savedSuppression := s.psr &&& suppressionMask}
  have snap : (cSnapshotSuppression.run s).1 = snapshot := by
    simp [cSnapshotSuppression, Effect.run, Expr.eval, assign, IA64Slot.read, snapshot,
          sourceSuppressionMask]
  have success := emittedSuccess snapshot c.ic c.record true i.address entry.icFact entry.shared
  simp only [cSuppressionPrefix, suppressionPrefix, issue, debug, Prod.fst, Prod.snd]
  by_cases db : IA64Slot.SDM.instructionDebug s.psr = true
  · simp only [db, if_true]
  · simp only [if_neg db, entry.tracked, if_true, snap, success]
    simp [snapshot, suppressionBeforeRequest, archSuccess, IA64Slot.SDM.success]
    split <;> rfl

open FullDecodeSpec FullDecodeImplementation in
theorem trackedBundlePrefix (low high ip : BitVec 64) (index : BitVec 2) (c : Context) (s : State)
    (encoding : aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
      (.branch (.relative 0)) low high index)
    (entry : SuppressionEntry c s (sdmInstruction (slotBits low high index) ip index)) :
    cSuppressionPrefix c s (cInstruction (bundleBR_COND low high ip index)) =
      suppressionPrefix s (sdmInstruction (slotBits low high index) ip index) := by
  rw [decodedB1 low high ip index encoding]
  exact trackedPrefixRefines c s _ entry

theorem clearRequest_iff (c : Context) (s : State) (i : Instruction)
    (entry : SuppressionEntry c s i) :
    (cSuppressionPrefix c s i).2 = some .clearSuppression ↔
      IA64Slot.SDM.instructionDebug s.psr = false ∧ s.psr &&& suppressionMask ≠ 0 := by
  rw [trackedPrefixRefines c s i entry]
  cases db : IA64Slot.SDM.instructionDebug s.psr <;> simp [suppressionPrefix, db]

theorem clearRequestFacts (c : Context) (s : State) (i : Instruction)
    (entry : SuppressionEntry c s i)
    (requested : (cSuppressionPrefix c s i).2 = some .clearSuppression) :
    let pending := (cSuppressionPrefix c s i).1
    pending.savedSuppression = s.psr &&& suppressionMask ∧
      IA64Slot.SDM.ri pending.psr = i.slot.number ∧ pending.ip = i.address ∧
      pending.lastSuccess = IA64Slot.SDM.success s i.address := by
  rw [trackedPrefixRefines c s i entry] at requested ⊢
  by_cases db : IA64Slot.SDM.instructionDebug s.psr = true
  · simp [suppressionPrefix, db] at requested
  · simp [suppressionPrefix, db, suppressionBeforeRequest, archSuccess, entry.ri, entry.ip]

def cClearReturn (pending : State) : State :=
  let oldMask := pending.savedSuppression &&& cSuppressionMask
  let clearMask := pending.psr &&& oldMask
  { pending with psr := if clearMask = 0 then pending.psr else pending.psr &&& ~~~clearMask,
                 savedSuppression := 0 }

private theorem clearedWord (psr snapshot : Word) (captured : snapshot = psr &&& suppressionMask) :
    (let clearMask := psr &&& (snapshot &&& cSuppressionMask)
     if clearMask = 0 then psr else psr &&& ~~~clearMask) = psr &&& ~~~suppressionMask := by
  rw [captured, sourceSuppressionMask]
  unfold suppressionMask
  bv_decide

theorem returnedSuppression (c : Context) (s : State) (i : Instruction)
    (entry : SuppressionEntry c s i)
    (requested : (cSuppressionPrefix c s i).2 = some .clearSuppression) :
    cClearReturn (cSuppressionPrefix c s i).1 =
      { archSuccess s i.address with psr := s.psr &&& ~~~suppressionMask, savedSuppression := 0 } := by
  rw [trackedPrefixRefines c s i entry] at requested ⊢
  by_cases db : IA64Slot.SDM.instructionDebug s.psr = true
  · simp [suppressionPrefix, db] at requested
  · simp only [suppressionPrefix, if_neg db, Prod.fst]
    exact congrArg (fun psr => { archSuccess s i.address with psr := psr, savedSuppression := 0 })
      (clearedWord s.psr (s.psr &&& suppressionMask) rfl)

theorem clearedRI (psr : Word) :
    IA64Slot.SDM.ri (psr &&& ~~~suppressionMask) = IA64Slot.SDM.ri psr := by
  simp only [IA64Slot.SDM.ri, suppressionMask]
  bv_decide

open FullDecodeSpec FullDecodeImplementation in
theorem trackedBundleReturn (low high ip : BitVec 64) (index : BitVec 2) (c : Context) (s : State)
    (encoding : aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
      (.branch (.relative 0)) low high index) :
    let i := sdmInstruction (slotBits low high index) ip index
    let beforeClear := cSuppressionPrefix c s (cInstruction (bundleBR_COND low high ip index))
    SuppressionEntry c s i →
    beforeClear.2 = some .clearSuppression →
    let returned := cClearReturn beforeClear.1
    returned = { archSuccess s ip with psr := s.psr &&& ~~~suppressionMask, savedSuppression := 0 } ∧
    IA64Slot.SDM.ri returned.psr = i.slot.number := by
  rw [decodedB1 low high ip index encoding]
  intro i beforeClear entry requested returned
  have cleared := returnedSuppression c s i entry requested
  refine ⟨cleared, ?_⟩
  change IA64Slot.SDM.ri (cClearReturn (cSuppressionPrefix c s i).1).psr = _
  rw [cleared]
  exact (clearedRI s.psr).trans entry.ri

open FullDecodeSpec FullDecodeImplementation in
theorem b1RouteRefines :
    B1Contract bundleBR_COND cRun ∧
    (∀ (low high ip : BitVec 64) (index : BitVec 2) (c : Context) (s : State),
      aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
        (.branch (.relative 0)) low high index →
      let i := sdmInstruction (slotBits low high index) ip index
      SuppressionEntry c s i →
      cSuppressionPrefix c s (cInstruction (bundleBR_COND low high ip index)) =
        suppressionPrefix s i) ∧
    (∀ (low high ip : BitVec 64) (index : BitVec 2) (c : Context) (s : State),
      aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
        (.branch (.relative 0)) low high index →
      let i := sdmInstruction (slotBits low high index) ip index
      let beforeClear := cSuppressionPrefix c s (cInstruction (bundleBR_COND low high ip index))
      SuppressionEntry c s i →
      beforeClear.2 = some .clearSuppression →
      let returned := cClearReturn beforeClear.1
      returned = { archSuccess s ip with psr := s.psr &&& ~~~suppressionMask, savedSuppression := 0 } ∧
      IA64Slot.SDM.ri returned.psr = i.slot.number) :=
  ⟨b1BundleRefines, trackedBundlePrefix, trackedBundleReturn⟩

end IA64Branch.B1
