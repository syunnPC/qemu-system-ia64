/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Movl

open IA64Add (Joined Context Boundary EntryFacts)
open FullDecodeSpec FullDecodeImplementation

def projected (wire : Observation) : IA64Tcg.Slot :=
  ⟨(wire.extractLsb' 210 7).toFin, 0, 0, (wire.extractLsb' 218 6).toFin,
    wire.extractLsb' 82 64, wire.extractLsb' 234 64,
    Fin.ofNat' 3 (wire.extractLsb' 226 2).toNat,
    wire.extractLsb' 298 64, !wire.getLsbD 1, false, false, false⟩

def architectural (low high ip : Word) : IA64Tcg.Slot :=
  let l := slotBits low high 1
  let x := slotBits low high 2
  ⟨(x.extractLsb' 6 7).toFin, 0, 0, (x.extractLsb' 0 6).toFin,
    longImm .movl l x, ip, 1, l.zeroExtend 64, false, false, false, false⟩

private theorem recordFields (l x : Slot) (ip : Word) :
    let wire := longRecord lUnit xUnit .movl l x ip
    wire.extractLsb' 210 7 = x.extractLsb' 6 7 ∧
    wire.extractLsb' 218 6 = x.extractLsb' 0 6 ∧
    wire.extractLsb' 82 64 = longImm .movl l x ∧
    wire.extractLsb' 234 64 = ip ∧ wire.extractLsb' 226 2 = 1 ∧
    wire.extractLsb' 298 64 = l.zeroExtend 64 ∧ wire.getLsbD 1 = true := by
  dsimp only [longRecord, longReg, longLink]
  generalize longImm .movl l x = immediate
  bv_decide

theorem wireMatches (low high ip : Word) (index : BitVec 2)
    (legal : longBundleEncoding .movl low high index) :
    projected (bundleMOVL low high ip index) = architectural low high ip := by
  rw [(MOVL_legal low high ip index legal).1, aLongBundle, if_pos legal]
  obtain ⟨dst, qp, immediate, address, slot, raw, valid⟩ :=
    recordFields (slotBits low high 1) (slotBits low high 2) ip
  simp only [projected, architectural, dst, qp, immediate, address, slot, raw, valid, Bool.not_true]
  rfl

def skipsX (low high ip : Word) : Bool :=
  let form := (cTemplate low).setWidth 5
  let raw (index : BitVec 2) := (cSlot low high index).setWidth 41
  cPairSkip form (raw 1) (raw 0) (raw 2) ip

theorem decodedSkipsX (low high ip : Word) (index : BitVec 2)
    (legal : longBundleEncoding .movl low high index) : skipsX low high ip = true := by
  have template : (cTemplate low).setWidth 5 = templateBits low := by
    rw [bundleTemplate_matches]
    simp
  have slot (n : BitVec 2) : (cSlot low high n).setWidth 41 = slotBits low high n := by
    rw [bundleSlot_matches]
    simp
  simp only [skipsX, template, slot]
  exact MOVL_pair_skips _ _ _ _ _ legal.1 legal.2.2

def emittedData (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    IA64Tcg.Outcome α :=
  IA64Tcg.finish s i (IA64Tcg.prepareProjected s i facts)
    (fun state insn => IA64Tcg.sourceBody_MOVL state insn facts)

-- SDM Vol. 3, p. 3:187. The frame check is predicate-qualified and
-- uses the virtual GR view at the instruction's entry.
def architecturalData (s : IA64Tcg.State α) (i : IA64Tcg.Slot) : IA64Tcg.Outcome α :=
  if !IA64Tcg.SDM.qualified s i.qp then .nullified s
  else if !IA64Tcg.SDM.legalTarget s i.dst then .fault .illegal i.ip i.ri i.raw s
  else .committed (IA64Tcg.SDM.commit s i.dst i.imm false)

theorem dataMatches (low high ip : Word) (s : IA64Tcg.State α) (facts : IA64Tcg.Facts)
    (sound : IA64Tcg.Sound s (architectural low high ip) facts) :
    emittedData s (architectural low high ip) facts =
      architecturalData s (architectural low high ip) := by
  let i := architectural low high ip
  have body : IA64Tcg.sourceBody_MOVL s i facts = IA64Tcg.SDM.commit s i.dst i.imm false := by
    simpa [IA64Tcg.SDM.operate, IA64Tcg.alu_MOVL, IA64Integer.SDM.MOVL, IA64Integer.SDM.sourceNaT]
      using IA64Tcg.sourceBody_MOVL_eq s i facts sound
  change emittedData s i facts = architecturalData s i
  unfold emittedData architecturalData
  rw [IA64Tcg.prepareProjected_eq s i facts sound]
  have shape : i.malformed = false ∧ i.illegalPlacement = false ∧
      i.reservedField = false ∧ i.missingFeature = false := ⟨rfl, rfl, rfl, rfl⟩
  simp only [IA64Tcg.SDM.prepare, shape.1, shape.2.1, shape.2.2.1, shape.2.2.2,
    Bool.false_eq_true, if_false, Bool.false_or]
  cases qualified : IA64Tcg.SDM.qualified s i.qp <;> cases target : IA64Tcg.SDM.legalTarget s i.dst <;>
    simp [IA64Tcg.finish, qualified, target, body]

theorem visibleWrite (low high ip : Word) (index : BitVec 2)
    (s : IA64Tcg.State α) (facts : IA64Tcg.Facts)
    (legal : longBundleEncoding .movl low high index)
    (sound : IA64Tcg.Sound s (projected (bundleMOVL low high ip index)) facts)
    (qualifies : IA64Tcg.SDM.qualified s (architectural low high ip).qp = true)
    (target : IA64Tcg.SDM.legalTarget s (architectural low high ip).dst = true) :
    ∃ result, emittedData s (projected (bundleMOVL low high ip index)) facts = .committed result ∧
      result.gr (architectural low high ip).dst = longImm .movl (slotBits low high 1) (slotBits low high 2) ∧
      IA64Tcg.SDM.nat result (architectural low high ip).dst = false := by
  rw [wireMatches _ _ _ _ legal] at sound ⊢
  rw [dataMatches _ _ _ _ _ sound]
  refine ⟨IA64Tcg.SDM.commit s (architectural low high ip).dst (architectural low high ip).imm false,
    ?_, ?_, IA64Tcg.nat_commit_at _ _ _ false (IA64Tcg.legalNonzero _ _ target)⟩
  · simp [architecturalData, qualifies, target]
  · simp [IA64Tcg.SDM.commit, IA64Tcg.update, architectural]

structure Completion (α : Type) where
  reportedSlot : Fin 3
  physicalSlot : BitVec 2
  consumedX : Bool
  stop : Bool
  outcome : Boundary α

def sourceBundleStep (low high ip : Word) (index : BitVec 2) (s : Joined α)
    (facts : IA64Tcg.Facts) (c : Context) : Completion α :=
  let i := projected (bundleMOVL low high ip index)
  let skip := skipsX low high ip
  let stop := cLongStop low high ip index
  ⟨i.ri, cLongCompletion low high ip index, skip, stop,
    IA64Add.emittedSlot s i (emittedData s.data i facts) {c with stopAfter := stop} skip⟩

def architecturalBundleStep (low high ip : Word) (s : Joined α) (c : Context) : Completion α :=
  let i := architectural low high ip
  let stop := decide (templateBits low = 5)
  ⟨1, 2, true, stop,
    IA64Add.referenceSlot s i (architecturalData s.data i) {c with stopAfter := stop} true⟩

theorem bundleRefines (low high ip : Word) (index : BitVec 2) (s : Joined α)
    (facts : IA64Tcg.Facts) (c : Context)
    (legal : longBundleEncoding .movl low high index)
    (sound : IA64Tcg.Sound s.data (projected (bundleMOVL low high ip index)) facts)
    (entry : EntryFacts s (projected (bundleMOVL low high ip index))
      {c with stopAfter := cLongStop low high ip index}) :
    bundleMOVL low high ip 2 = 0 ∧
      sourceBundleStep low high ip index s facts c = architecturalBundleStep low high ip s c := by
  obtain ⟨_, absent, completion, stop⟩ := MOVL_legal low high ip index legal
  have fields := wireMatches low high ip index legal
  rw [fields] at sound entry
  rw [stop] at entry
  refine ⟨absent, ?_⟩
  simp only [sourceBundleStep, architecturalBundleStep, fields, completion, stop,
    decodedSkipsX _ _ _ _ legal, dataMatches _ _ _ _ _ sound,
    IA64Add.emittedSlot_eq _ _ _ _ entry true]
  rfl

theorem verified :
    (∀ {α : Type} (low high ip : BitVec 64) (index : BitVec 2)
        (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (c : IA64Add.Context),
      let decoded := IA64Movl.projected (FullDecodeImplementation.bundleMOVL low high ip index)
      FullDecodeSpec.longBundleEncoding .movl low high index →
      IA64Tcg.Sound s.data decoded facts →
      IA64Add.EntryFacts s decoded
        {c with stopAfter := FullDecodeImplementation.cLongStop low high ip index} →
      FullDecodeImplementation.bundleMOVL low high ip 2 = 0 ∧
        IA64Movl.sourceBundleStep low high ip index s facts c =
          IA64Movl.architecturalBundleStep low high ip s c) ∧
    (∀ {α : Type} (low high ip : BitVec 64) (index : BitVec 2)
        (s : IA64Tcg.State α) (facts : IA64Tcg.Facts),
      let decoded := IA64Movl.projected (FullDecodeImplementation.bundleMOVL low high ip index)
      let spec := IA64Movl.architectural low high ip
      FullDecodeSpec.longBundleEncoding .movl low high index →
      IA64Tcg.Sound s decoded facts →
      IA64Tcg.SDM.qualified s spec.qp = true →
      IA64Tcg.SDM.legalTarget s spec.dst = true →
      ∃ result, IA64Movl.emittedData s decoded facts = .committed result ∧
        result.gr spec.dst = FullDecodeSpec.longImm .movl
          (FullDecodeSpec.slotBits low high 1) (FullDecodeSpec.slotBits low high 2) ∧
        IA64Tcg.SDM.nat result spec.dst = false) :=
  ⟨@bundleRefines, @visibleWrite⟩

end IA64Movl
