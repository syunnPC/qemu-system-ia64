/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64ExceptionEntry

open IA64Add FullDecodeSpec FullDecodeImplementation
set_option maxRecDepth 16384
set_option maxHeartbeats 2000000

theorem frame_fault_code (dst : IA64Tcg.Reg) : cIntegerTargetFault dst = 2 := by
  simp [cIntegerTargetFault]

def beforeFromBoundary (cfg : Before) (faulted : Joined α) : Before :=
  { cfg with psr := faulted.control.psr, lastSuccessfulBundle := faulted.control.lastSuccess }

def emittedAluEntry (tag : IA64Alu.Opcode) (lo hi ip : Word) (index : BitVec 2)
    (state : Joined α) (facts : IA64Tcg.Facts) (ctx : Context) (cfg : Before) :
    Option Observation :=
  match IA64Alu.sourceBundleStep tag lo hi ip index state facts ctx with
  | .fault stopped .illegal address raw =>
    let before := beforeFromBoundary cfg stopped
    let decoded := IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index)
    let request : IA64Slot.FaultRequest := ⟨cIntegerTargetFault decoded.dst, address, raw⟩
    (cDispatch before (cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request))).map
      (observe before)
  | _ => none

private theorem retirement_not_fault (s stopped : Joined α) (bundle : Word)
    (slot : IA64Slot.Slot) (executed : Bool) (c : Context) (address : IA64Slot.Address)
    (raw : Word) :
    referenceRetire s bundle slot executed c ≠ .fault stopped .illegal address raw := by
  intro impossible
  dsimp only [referenceRetire] at impossible
  cases h : c.virtualAddress <;> rw [h] at impossible <;>
    simp only [Bool.false_eq_true, ite_false, ite_true] at impossible <;>
    split at impossible <;> first | contradiction | (split at impossible <;> contradiction)

private theorem reference_fault_record (tag : IA64Alu.Opcode) (s stopped : Joined α)
    (i : IA64Tcg.Slot) (c : Context) (inputs : Operands)
    (address : IA64Slot.Address) (raw : Word)
    (faulted : referenceSlot s i (IA64Alu.sdmData tag s.data i inputs) c =
      .fault stopped .illegal address raw) :
    IA64Tcg.SDM.prepare s.data i = .fault .illegal ∧
      address = ⟨i.ip, (realSlot i.ri).number⟩ ∧ raw = i.raw ∧
      stopped.control.lastSuccess = s.control.lastSuccess := by
  dsimp only [referenceSlot] at faulted
  by_cases debug : IA64Slot.SDM.instructionDebug (setEntry s.control (realSlot i.ri)).psr = true
  · rw [if_pos debug] at faulted
    contradiction
  · rw [if_neg debug] at faulted
    unfold IA64Alu.sdmData at faulted
    cases prep : IA64Tcg.SDM.prepare s.data i with
    | execute =>
      rw [prep] at faulted
      exact False.elim (retirement_not_fault _ _ _ _ _ _ _ _ faulted)
    | nullified =>
      rw [prep] at faulted
      exact False.elim (retirement_not_fault _ _ _ _ _ _ _ _ faulted)
    | fault kind =>
      rw [prep] at faulted
      dsimp only [IA64Tcg.finish, faultBoundary] at faulted
      cases faulted
      refine ⟨rfl, rfl, rfl, ?_⟩
      unfold savedAtEntry setEntry
      split <;> rfl

private theorem prepare_is_target (tag : IA64Alu.Opcode) (s : IA64Tcg.State α)
    (raw : Slot) (ip : Word) (index : BitVec 2)
    (issued : IA64Tcg.SDM.prepare s (IA64Alu.sdmRecord tag raw ip index) = .fault .illegal) :
    IA64Tcg.SDM.legalTarget s (IA64Alu.sdmRecord tag raw ip index).dst = false := by
  let i := IA64Alu.sdmRecord tag raw ip index
  change IA64Tcg.SDM.prepare s i = _ at issued
  change IA64Tcg.SDM.legalTarget s i.dst = false
  have clean : i.malformed = false ∧ i.illegalPlacement = false ∧
      i.reservedField = false ∧ i.missingFeature = false := ⟨rfl, rfl, rfl, rfl⟩
  cases q : IA64Tcg.SDM.qualified s i.qp <;> cases t : IA64Tcg.SDM.legalTarget s i.dst <;>
    simp [IA64Tcg.SDM.prepare, clean.1, clean.2.1, clean.2.2.1, clean.2.2.2, q, t] at issued ⊢

theorem alu_slot_illegal_enters (tag : IA64Alu.Opcode) (lo hi ip : Word) (index : BitVec 2)
    (state faulted : Joined α) (facts : IA64Tcg.Facts) (ctx : Context) (cfg : Before)
    (address : IA64Slot.Address) (raw : Word)
    (present : IA64Alu.selected tag lo hi ip index)
    (sound : IA64Tcg.Sound state.data (IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index)) facts)
    (entry : EntryFacts state (IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index))
      {ctx with stopAfter := cStop (templateBits lo) index})
    (issued : IA64Alu.sourceBundleStep tag lo hi ip index state facts ctx =
      .fault faulted .illegal address raw)
    (native : nativeOrdinary (beforeFromBoundary cfg faulted)) (alignedIp : ip &&& 15 = 0)
    (alignedIVA : cfg.iva &&& 0x7fff = 0)
    (vector : SDM.collecting faulted.control.psr = true ∨ cfg.iva ≠ 0) :
    let input := IA64Alu.sdmRecord tag (slotBits lo hi index) ip index
    let prior := { beforeFromBoundary cfg faulted with lastSuccessfulBundle := state.control.lastSuccess }
    let fault : Fault := ⟨input.ip, (realSlot input.ri).number.setWidth 32, input.raw⟩
    IA64Tcg.SDM.legalTarget state.data input.dst = false ∧
      emittedAluEntry tag lo hi ip index state facts ctx cfg = some (SDM.illegalEntry prior fault) := by
  let i := IA64Alu.sdmRecord tag (slotBits lo hi index) ip index
  obtain ⟨inputs, _, eq⟩ := IA64Alu.bundleRefines tag lo hi ip index state facts ctx present sound entry
  have ref := eq.symm.trans issued
  change referenceSlot state i (IA64Alu.sdmData tag state.data i inputs) _ = _ at ref
  obtain ⟨illegal, location, bits, history⟩ := reference_fault_record tag state faulted i _ inputs address raw ref
  dsimp only
  refine ⟨prepare_is_target tag state.data (slotBits lo hi index) ip index illegal, ?_⟩
  unfold emittedAluEntry
  rw [issued]
  let request : IA64Slot.FaultRequest :=
    ⟨cIntegerTargetFault (IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index)).dst, address, raw⟩
  have code : request.code = 2 := frame_fault_code _
  have valid : request.address.slot < 3 := by
    dsimp only [request]
    rw [location]
    change (realSlot i.ri).number < 3
    cases realSlot i.ri <;> decide
  have bundleAligned : request.address.bundle &&& 15 = 0 := by
    dsimp only [request]
    rw [location]
    exact alignedIp
  change (cDispatch (beforeFromBoundary cfg faulted)
    (cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request))).map
      (observe (beforeFromBoundary cfg faulted)) = _
  rw [slot_request_enters (beforeFromBoundary cfg faulted) request code native bundleAligned
        alignedIVA valid vector]
  dsimp only [request, fromSlotRequest, beforeFromBoundary]
  rw [location, bits, history] <;> rfl

def expectedStopped (s : Joined α) (i : IA64Tcg.Slot) (c : Context) : Joined α :=
  { s with control := { savedAtEntry (setEntry s.control (realSlot i.ri)) c.trackSuppression
                       with ip := i.ip } }

def sdmTargetFault (s : Joined α) (i : IA64Tcg.Slot) : Prop :=
  IA64Slot.SDM.instructionDebug (setEntry s.control (realSlot i.ri)).psr = false ∧
  IA64Tcg.SDM.qualified s.data i.qp = true ∧
  IA64Tcg.SDM.legalTarget s.data i.dst = false

theorem alu_slot_illegal_deliverable (tag : IA64Alu.Opcode) (lo hi ip : Word) (index : BitVec 2)
    (state : Joined α) (facts : IA64Tcg.Facts) (ctx : Context) (cfg : Before)
    (present : IA64Alu.selected tag lo hi ip index)
    (sound : IA64Tcg.Sound state.data (IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index)) facts)
    (entry : EntryFacts state (IA64Alu.emittedRecord (IA64Alu.decode tag lo hi ip index))
      {ctx with stopAfter := cStop (templateBits lo) index}) :
    let i := IA64Alu.sdmRecord tag (slotBits lo hi index) ip index
    let stopped := expectedStopped state i ctx
    let before := beforeFromBoundary cfg stopped
    sdmTargetFault state i → nativeOrdinary before → ip &&& 15 = 0 →
    cfg.iva &&& 0x7fff = 0 → (SDM.collecting before.psr = true ∨ cfg.iva ≠ 0) →
    IA64Alu.sourceBundleStep tag lo hi ip index state facts ctx =
      .fault stopped .illegal ⟨i.ip, (realSlot i.ri).number⟩ i.raw ∧
    emittedAluEntry tag lo hi ip index state facts ctx cfg =
      some (SDM.illegalEntry {before with lastSuccessfulBundle := state.control.lastSuccess}
        ⟨i.ip, (realSlot i.ri).number.setWidth 32, i.raw⟩) := by
  intro i stopped before condition native alignedIp alignedIVA vector
  obtain ⟨debug, qualified, target⟩ := condition
  have clean : i.malformed = false ∧ i.illegalPlacement = false ∧
      i.reservedField = false ∧ i.missingFeature = false := ⟨rfl, rfl, rfl, rfl⟩
  have prep : IA64Tcg.SDM.prepare state.data i = .fault .illegal := by
    simp [IA64Tcg.SDM.prepare, clean.1, clean.2.1, clean.2.2.1, clean.2.2.2, qualified, target]
  obtain ⟨inputs, _, eq⟩ := IA64Alu.bundleRefines tag lo hi ip index state facts ctx present sound entry
  have issued : IA64Alu.sourceBundleStep tag lo hi ip index state facts ctx =
      .fault stopped .illegal ⟨i.ip, (realSlot i.ri).number⟩ i.raw := by
    rw [eq]
    change referenceSlot state i (IA64Alu.sdmData tag state.data i inputs) _ = _
    dsimp only [referenceSlot, IA64Alu.sdmData]
    rw [debug]
    simp only [Bool.false_eq_true, ite_false, prep]
    rfl
  exact ⟨issued, (alu_slot_illegal_enters tag lo hi ip index state stopped facts ctx cfg
    ⟨i.ip, (realSlot i.ri).number⟩ i.raw present sound entry issued native alignedIp alignedIVA vector).2⟩

private def addFixture (dst : Word) : Word :=
  -- MII, slot zero, add r[dst]=r1,r2, p0 (Vol. 3 A1 and Table 4-9).
  ((8 <<< 37) ||| (2 <<< 20) ||| (1 <<< 13) ||| (dst <<< 6)) <<< 5

private def addState : Joined _root_.Unit :=
  ⟨⟨fun _ => 0, fun _ => 0, fun _ => false, fun _ => false, 0, ()⟩,
    ⟨0x100, 0x2000, 0x0f0, 0, 0, true⟩⟩

private def addFacts : IA64Tcg.Facts :=
  ⟨none, fun _ => none, fun _ => false, 0⟩

private def addContext : Context :=
  ⟨none, none, true, false, true, false, false, false, false, true, true, false⟩

private def addBefore : Before :=
  ⟨0, 0, 0x8000, 0, ⟨0, 0, 0, true⟩, 0, true, false, false, false⟩

private theorem fixture_vectors (dst : Word) (choice : dst = 0 ∨ dst = 32) :
    (emittedAluEntry .ADD (addFixture dst) 0 0x100 0 addState addFacts addContext addBefore).map
      (fun observed => (observed.ip, observed.isr, observed.saved)) =
    some (0xd400, 0, some ⟨0x100, 0x2000, 0x0f0, false⟩) := by
  have present : IA64Alu.selected .ADD (addFixture dst) 0 0x100 0 := by
    apply ((IA64Alu.decodeComplete .ADD) (addFixture dst) 0 0x100 0).2.mpr
    rcases choice with rfl | rfl <;> unfold aBundleEncoding <;> decide
  have sound : IA64Tcg.Sound addState.data
      (IA64Alu.emittedRecord (IA64Alu.decode .ADD (addFixture dst) 0 0x100 0)) addFacts := by
    simp [IA64Tcg.Sound, addFacts, addState]
  have entry : EntryFacts addState
      (IA64Alu.emittedRecord (IA64Alu.decode .ADD (addFixture dst) 0 0x100 0))
      {addContext with stopAfter := cStop (templateBits (addFixture dst)) 0} := by
    refine ⟨?_, ?_, ?_, ?_, ?_⟩
    · rw [IA64Alu.decodedRecord_eq .ADD _ _ _ _ present] <;> rfl
    · simp [IA64Slot.RISound, addContext]
    · simp [addContext]
    · simp [addContext]
    · simp [addContext]
  have condition : sdmTargetFault addState
      (IA64Alu.sdmRecord .ADD (slotBits (addFixture dst) 0 0) 0x100 0) := by
    rcases choice with rfl | rfl <;> exact ⟨rfl, rfl, rfl⟩
  rcases choice with rfl | rfl
  all_goals
    have delivered := alu_slot_illegal_deliverable .ADD _ 0 0x100 0
      addState addFacts addContext addBefore present sound entry condition
      (by unfold nativeOrdinary; decide)
      rfl rfl (Or.inr (by decide))
    rw [delivered.2]
    rfl

example :
    (emittedAluEntry .ADD (addFixture 0) 0 0x100 0 addState addFacts addContext addBefore).map
      (fun observed => (observed.ip, observed.isr, observed.saved)) =
    some (0xd400, 0, some ⟨0x100, 0x2000, 0x0f0, false⟩) :=
  fixture_vectors 0 (Or.inl rfl)

example :
    (emittedAluEntry .ADD (addFixture 32) 0 0x100 0 addState addFacts addContext addBefore).map
      (fun observed => (observed.ip, observed.isr, observed.saved)) =
    some (0xd400, 0, some ⟨0x100, 0x2000, 0x0f0, false⟩) :=
  fixture_vectors 32 (Or.inr rfl)

end IA64ExceptionEntry
