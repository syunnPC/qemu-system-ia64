/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Alu

open FullDecodeSpec FullDecodeImplementation
open IA64Add
set_option maxRecDepth 16384
set_option maxHeartbeats 2000000

def selected (tag : Opcode) (low high ip : Word) (index : BitVec 2) : Prop :=
  (decode tag low high ip index).getLsbD 394 = true

def emittedRecord (wire : Observation) : IA64Tcg.Slot :=
  { projectedADD wire with imm := wire.extractLsb' 82 64 }

def register (value : BitVec 8) : IA64Tcg.Reg := (value.extractLsb' 0 7).toFin
def predicate (value : BitVec 8) : IA64Tcg.Predicate := (value.extractLsb' 0 6).toFin

def sdmRecord (tag : Opcode) (raw : Slot) (ip : Word) (index : BitVec 2) : IA64Tcg.Slot :=
  let instruction := format tag
  ⟨register (aR1 instruction raw), register (aR2 instruction raw), register (aR3 instruction raw),
    predicate (reg raw 0 6), aImm instruction raw, ip, Fin.ofNat' 3 index.toNat,
    raw.zeroExtend 64, false, false, false, false⟩

private def packedRecord (u : UnitCode) (raw : Slot) (ip : Word) (index : BitVec 2)
    (p r1 r2 r3 : BitVec 8) (imm : Word) : Observation :=
  1#1 ++ u.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    p ++ r1 ++ r2 ++ r3 ++ (0#48 ++ imm ++ 2#82)

private theorem packedRecord_eq (tag : Opcode) (unit : UnitCode) (raw : Slot)
    (ip : Word) (index : BitVec 2) :
    aRecord (format tag) unit raw ip (index.zeroExtend 8) =
      packedRecord unit raw ip index (reg raw 0 6)
        (aR1 (format tag) raw) (aR2 (format tag) raw) (aR3 (format tag) raw)
        (aImm (format tag) raw) := by
  have tail : aRecordTail (format tag) raw = 0#48 ++ aImm (format tag) raw ++ 2#82 := by
    cases tag <;> simp only [format, aRecordTail]
  simp only [aRecord, packedRecord, tail]

private theorem unpackRecord (u : UnitCode) (raw : Slot) (ip : Word) (index : BitVec 2)
    (p r1 r2 r3 : BitVec 8) (imm : Word) :
    emittedRecord (packedRecord u raw ip index p r1 r2 r3 imm) =
      ⟨register r1, register r2, register r3, predicate p, imm, ip, Fin.ofNat' 3 index.toNat,
       raw.zeroExtend 64, false, false, false, false⟩ := by
  let wire := packedRecord u raw ip index p r1 r2 r3 imm
  have dst : wire.extractLsb' 210 7 = r1.extractLsb' 0 7 := by
    simp only [wire, packedRecord]
    bv_decide
  have left : wire.extractLsb' 202 7 = r2.extractLsb' 0 7 := by
    simp only [wire, packedRecord]
    bv_decide
  have right : wire.extractLsb' 194 7 = r3.extractLsb' 0 7 := by
    simp only [wire, packedRecord]
    bv_decide
  have qp : wire.extractLsb' 218 6 = p.extractLsb' 0 6 := by
    simp only [wire, packedRecord]
    bv_decide
  have address : wire.extractLsb' 234 64 = ip := by
    simp only [wire, packedRecord]
    bv_decide
  have rawbits : wire.extractLsb' 298 64 = raw.zeroExtend 64 := by
    simp only [wire, packedRecord]
    bv_decide
  have ri : wire.extractLsb' 226 2 = index := by
    simp only [wire, packedRecord]
    bv_decide
  have immediate : wire.extractLsb' 82 64 = imm := by
    simp only [wire, packedRecord]
    bv_decide
  change emittedRecord wire = _
  simp only [emittedRecord, projectedADD, register, predicate, wireRI,
             dst, left, right, qp, address, rawbits, ri, immediate]

theorem decodedRecord_eq (tag : Opcode) (low high ip : Word) (index : BitVec 2)
    (present : selected tag low high ip index) :
    emittedRecord (decode tag low high ip index) = sdmRecord tag (slotBits low high index) ip index := by
  have contract := decodeComplete tag low high ip index
  obtain ⟨valid, inRange, unit, ext⟩ := contract.2.mp present
  rw [contract.1]
  simp only [aBundle, if_pos (And.intro valid inRange), aOpcode, if_pos (And.intro unit ext),
             packedRecord_eq, unpackRecord, sdmRecord]

def fastGuard (i : IA64Tcg.Slot) (f : IA64Tcg.Facts) : Prop :=
  i.malformed = false ∧ i.illegalPlacement = false ∧ i.reservedField = false ∧
    i.missingFeature = false ∧ IA64Add.fastGuard i f

instance (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) : Decidable (fastGuard i facts) := by
  unfold fastGuard IA64Add.fastGuard
  infer_instance

theorem guardAccepted (i : IA64Tcg.Slot) (f : IA64Tcg.Facts) (gate : fastGuard i f) :
    IA64Tcg.FastApplicability i f := by
  obtain ⟨valid, placement, reserved, feature, qp, unknown, dst, static, l, r⟩ := gate
  exact ⟨valid, placement, reserved, feature, qp, unknown, dst, static, l, r⟩

def ordinaryStep (tag : Opcode) (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (facts : IA64Tcg.Facts) : IA64Tcg.Outcome α :=
  IA64Tcg.finish s i (IA64Tcg.prepareProjected s i facts) fun s i => ordinaryBody s i facts tag

def chosenData (tag : Opcode) (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (facts : IA64Tcg.Facts) : IA64Tcg.Outcome α :=
  if fastGuard i facts then (fastStep s i facts tag).getD (ordinaryStep tag s i facts)
  else ordinaryStep tag s i facts

theorem chosenData_eq (tag : Opcode) (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (facts : IA64Tcg.Facts) (sound : IA64Tcg.Sound s i facts) :
    chosenData tag s i facts = IA64Tcg.sdStep s i (operation tag) := by
  have ordinary : ordinaryStep tag s i facts = IA64Tcg.sdStep s i (operation tag) :=
    IA64Tcg.compose s i (operation tag) (fun s i => IA64Tcg.prepareProjected s i facts)
      (fun s i => ordinaryBody s i facts tag) (IA64Tcg.prepareProjected_eq s i facts sound)
      (fun _ => ordinaryBody_eq s i facts tag sound)
  unfold chosenData
  split
  · rename_i gate
    rw [fastStep_eq s i facts tag sound (guardAccepted i facts gate)]
    cases conditionalFast tag <;> first | exact ordinary | rfl
  · exact ordinary

def sdmData (tag : Opcode) (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (inputs : Operands) : IA64Tcg.Outcome α :=
  let op := operation tag
  IA64Tcg.finish s i (IA64Tcg.SDM.prepare s i) fun s i =>
    IA64Tcg.SDM.commit s i.dst (op.value inputs.left inputs.right i.imm)
      (IA64Integer.SDM.sourceNaT op.usesR2 op.usesR3 inputs.natLeft inputs.natRight)

theorem sdmData_represented (tag : Opcode) (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    sdmData tag s i (representedOperands s i) = IA64Tcg.sdStep s i (operation tag) := rfl

def sourceBundleStep (tag : Opcode) (low high ip : Word) (index : BitVec 2)
    (s : Joined α) (facts : IA64Tcg.Facts) (ctx : Context) : Boundary α :=
  let i := emittedRecord (decode tag low high ip index)
  emittedSlot s i (chosenData tag s.data i facts)
    {ctx with stopAfter := cStop (templateBits low) index}

def sdmBundleStep (tag : Opcode) (low high ip : Word) (index : BitVec 2)
    (s : Joined α) (inputs : Operands) (ctx : Context) : Boundary α :=
  let i := sdmRecord tag (slotBits low high index) ip index
  referenceSlot s i (sdmData tag s.data i inputs)
    {ctx with stopAfter := templateAddStop (templateBits low) index}

theorem bundleRefines (tag : Opcode) (low high ip : Word) (index : BitVec 2)
    (s : Joined α) (facts : IA64Tcg.Facts) (ctx : Context)
    (present : selected tag low high ip index)
    (sound : IA64Tcg.Sound s.data (emittedRecord (decode tag low high ip index)) facts)
    (entry : EntryFacts s (emittedRecord (decode tag low high ip index))
      {ctx with stopAfter := cStop (templateBits low) index}) :
    ∃ inputs, OperandChoice s.data (sdmRecord tag (slotBits low high index) ip index) inputs ∧
      sourceBundleStep tag low high ip index s facts ctx =
        sdmBundleStep tag low high ip index s inputs ctx := by
  let target := sdmRecord tag (slotBits low high index) ip index
  refine ⟨representedOperands s.data target,
    ⟨fun _ => ⟨rfl, rfl⟩, fun _ => ⟨rfl, rfl⟩⟩, ?_⟩
  let actual := emittedRecord (decode tag low high ip index)
  have data := chosenData_eq tag s.data actual facts sound
  have retire := emittedSlot_eq s actual (chosenData tag s.data actual facts)
    {ctx with stopAfter := cStop (templateBits low) index} entry
  have decoded := decodedRecord_eq tag low high ip index present
  have templ := ((decodeComplete tag low high ip index).2.mp present).1
  have stopped := emittedStop_matches (templateBits low) index templ
  simp only [sourceBundleStep, sdmBundleStep, sdmData_represented, target]
  change emittedSlot s actual (chosenData tag s.data actual facts) _ = _
  rw [retire, data]
  dsimp only [actual]
  rw [decoded, stopped]

end IA64Alu
