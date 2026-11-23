/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Add

open FullDecodeSpec FullDecodeImplementation
set_option maxRecDepth 16384
set_option maxHeartbeats 2000000

def addSelected (low high ip : Word) (slot : BitVec 2) : Prop :=
  (bundleADD low high ip slot).getLsbD 394 = true

def wireRI (wire : Observation) : Fin 3 := Fin.ofNat' 3 (wire.extractLsb' 226 2).toNat

def projectedADD (wire : Observation) : IA64Tcg.Slot :=
  ⟨(wire.extractLsb' 210 7).toFin, (wire.extractLsb' 202 7).toFin,
    (wire.extractLsb' 194 7).toFin, (wire.extractLsb' 218 6).toFin,
    0, wire.extractLsb' 234 64, wireRI wire, wire.extractLsb' 298 64,
    false, false, false, false⟩

private def paddedADD (unit : UnitCode) (raw : Slot) (ip : Word) (slot : BitVec 2) :
    Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ slot.zeroExtend 8 ++
    reg raw 0 6 ++ reg raw 6 7 ++ reg raw 13 7 ++ reg raw 20 7 ++ (0#48 ++ 0#64 ++ 2#82)

private theorem aRecordPadded (unit : UnitCode) (raw : Slot) (ip : Word) (slot : BitVec 2) :
    aRecord (.a1 0 0) unit raw ip (slot.zeroExtend 8) = paddedADD unit raw ip slot := rfl

theorem wireADDFields (unit : UnitCode) (raw : Slot) (ip : Word) (slot : BitVec 2) :
    projectedADD (aRecord (.a1 0 0) unit raw ip (slot.zeroExtend 8)) =
      decodedBySDM (raw.zeroExtend 64) ip (Fin.ofNat' 3 slot.toNat) := by
  rw [aRecordPadded]
  let wire := paddedADD unit raw ip slot
  have dst : wire.extractLsb' 210 7 = (raw.zeroExtend 64).extractLsb' 6 7 := by
    simp only [wire, paddedADD, reg]
    bv_decide
  have left : wire.extractLsb' 202 7 = (raw.zeroExtend 64).extractLsb' 13 7 := by
    simp only [wire, paddedADD, reg]
    bv_decide
  have right : wire.extractLsb' 194 7 = (raw.zeroExtend 64).extractLsb' 20 7 := by
    simp only [wire, paddedADD, reg]
    bv_decide
  have qualifier : wire.extractLsb' 218 6 = (raw.zeroExtend 64).extractLsb' 0 6 := by
    simp only [wire, paddedADD, reg]
    bv_decide
  have location : wire.extractLsb' 234 64 = ip := by
    simp only [wire, paddedADD]
    bv_decide
  have rawbits : wire.extractLsb' 298 64 = raw.zeroExtend 64 := by
    simp only [wire, paddedADD]
    bv_decide
  have slotbits : wire.extractLsb' 226 2 = slot := by
    simp only [wire, paddedADD]
    bv_decide
  change projectedADD wire = _
  simp only [projectedADD, decodedBySDM, registerField, wireRI, dst, left, right,
             qualifier, location, rawbits, slotbits]

def templateAddStop (code : BitVec 5) (index : BitVec 2) : Bool :=
  if index = 2 then code.getLsbD 0
  else if index = 1 then code.extractLsb' 1 4 == 1
  else if index = 0 then code.extractLsb' 1 4 == 5
  else false

theorem emittedStop_matches (code : BitVec 5) (index : BitVec 2)
    (valid : validTemplate code = true) :
    cStop code index = templateAddStop code index := by
  simp only [validTemplate, decide_eq_true_eq] at valid
  simp only [cStop, templateAddStop]
  bv_decide

def chosenWireRoute (wire : Observation) (facts : IA64Tcg.Facts) :
    Route (projectedADD wire) facts :=
  if fast : fastGuard (projectedADD wire) facts then
    .branchless ⟨rfl, rfl, rfl, rfl, fast.1, fast.2.1, fast.2.2.1,
                 fast.2.2.2.1, fast.2.2.2.2.1, fast.2.2.2.2.2⟩
  else .ordinary

theorem bundleToNative (low high ip : Word) (index : BitVec 2)
    (selected : addSelected low high ip index) :
    ∃ insn : NativeRegisterAdd,
      insn.raw = slotBits low high index ∧ insn.ip = ip ∧
      insn.ri = Fin.ofNat' 3 index.toNat ∧
      projectedADD (bundleADD low high ip index) =
        emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri := by
  let code := templateBits low
  let raw := slotBits low high index
  let unit := templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit code index
  have contract := ADD_complete low high ip index
  have encoded := contract.2.mp selected
  obtain ⟨valid, inRange, archUnit, ext⟩ := encoded
  have major : raw.extractLsb' 37 4 = 8 := ext.1
  have lowext : raw.extractLsb' 27 9 = 0 := by
    change aEncoding (.a1 0 0) raw at ext
    simp only [aEncoding] at ext
    bv_decide
  let insn : NativeRegisterAdd :=
    ⟨if unit = mUnit then .memory else .integer, raw, ip, Fin.ofNat' 3 index.toNat, major, lowext⟩
  refine ⟨insn, rfl, rfl, rfl, ?_⟩
  have present : aBundle reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (.a1 0 0)
      low high ip index = aRecord (.a1 0 0) unit raw ip (index.zeroExtend 8) := by
    simp only [aBundle, if_pos (And.intro valid inRange), aOpcode, if_pos (And.intro archUnit ext)]
    rfl
  rw [contract.1, present, wireADDFields, emittedDecoded_eq]

/-- The input key is the ADD tag of the actual lifted bundle decoder.
    Register and qualifier selectors below come from its packed output. -/
theorem bundleAddRefines (low high ip : Word) (index : BitVec 2) (s : Joined α)
    (facts : IA64Tcg.Facts) (c : Context)
    (selected : addSelected low high ip index)
    (sound : IA64Tcg.Sound s.data (projectedADD (bundleADD low high ip index)) facts)
    (entry : EntryFacts s (projectedADD (bundleADD low high ip index))
       {c with stopAfter := cStop (templateBits low) index}) :
    let wire := bundleADD low high ip index
    let raw := slotBits low high index
    let archRI := Fin.ofNat' 3 index.toNat
    let guest := decodedBySDM (raw.zeroExtend 64) ip archRI
    ∃ operands, OperandChoice s.data guest operands ∧
      emittedStep s (projectedADD wire) facts (chosenWireRoute wire facts)
        {c with stopAfter := cStop (templateBits low) index} =
      referenceStep s guest operands {c with stopAfter := templateAddStop (templateBits low) index} := by
  let wire := bundleADD low high ip index
  obtain ⟨insn, raw, address, ri, args⟩ := bundleToNative low high ip index selected
  have valid := ((ADD_complete low high ip index).2.mp selected).1
  have stop := emittedStop_matches (templateBits low) index valid
  let actualContext := { c with stopAfter := cStop (templateBits low) index }
  let guest := decodedBySDM (insn.raw.zeroExtend 64) insn.ip insn.ri
  have guard : IA64Tcg.Sound s.data (emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri) facts :=
    args ▸ sound
  have atSlot : EntryFacts s (emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri) actualContext :=
    args ▸ entry
  let observedRoute := chosenWireRoute wire facts
  have evidence := emittedStep_eq s (projectedADD wire) facts observedRoute actualContext sound entry
  obtain ⟨operands, choice, native⟩ := nativeRegisterAdd insn s facts actualContext guard atSlot
  refine ⟨operands, ?_, ?_⟩
  · simpa only [raw, address, ri] using choice
  · change emittedStep s (projectedADD wire) facts observedRoute actualContext = _
    have nativeRef := emittedStep_eq s (emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri)
      facts (chosenRoute (insn.raw.zeroExtend 64) insn.ip insn.ri facts) actualContext guard atSlot
    rw [nativeRef] at native
    rw [evidence, args]
    dsimp only [actualContext] at native ⊢
    simpa only [emittedDecoded_eq, raw, address, ri, stop] using native

end IA64Add
