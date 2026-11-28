/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64OptimizedSlot

open IA64Optimizer
local notation "Word" => BitVec 64

def slotKind (temp : SlotTemp) : Nat :=
  if temp.val < 128 then 2 else if temp.val < 136 then 1 else 4

def selectCopy (m : Metadata SlotTemp) (origin cursor candidate : SlotTemp) : Nat → SlotTemp
  | 0 => candidate
  | fuel + 1 =>
    if cursor = origin then candidate
    else selectCopy m origin (m.next cursor) (pickCopy slotKind candidate cursor) fuel

private theorem selectCopy_read (m : Metadata SlotTemp) (s : State SlotTemp)
    (sound : m.sound s) (origin cursor candidate : SlotTemp)
    (atOrigin : s cursor = s origin) (initial : s candidate = s origin) (fuel : Nat) :
    s (selectCopy m origin cursor candidate fuel) = s origin := by
  induction fuel generalizing cursor candidate with
  | zero => exact initial
  | succ fuel ih =>
    rw [selectCopy]
    split
    · exact initial
    · apply ih
      · exact (sound.2.1 cursor).trans atOrigin
      · unfold pickCopy
        split
        · exact atOrigin
        · exact initial

def preferredSlotCopy (m : Metadata SlotTemp) (src : SlotTemp) : SlotTemp :=
  if (slotPool.readonly src).isSome then src
  else selectCopy m src (m.next src) src (136 + 2 ^ 64)

private theorem preferredSlotCopy_read (m : Metadata SlotTemp) (s : State SlotTemp)
    (valid : m.sound s) (src : SlotTemp) : s (preferredSlotCopy m src) = s src := by
  unfold preferredSlotCopy
  split
  · rfl
  · exact selectCopy_read m s valid src _ src (valid.2.1 src) rfl _

def conditionPreference (m : Masks) : Nat :=
  if m.known then if m.mayOne = 0 then 2 else 3 else 0

inductive AddWrite where
  | move (source : SlotTemp)
  | movcond (equal : Bool) (whenTrue whenFalse : SlotTemp)
  | oneBit (equal negate : Bool)

def condTest (equal : Bool) (pred : Word) : Bool :=
  if equal then pred == 0 else pred != 0

def AddWrite.value (s : State SlotTemp) (pred : Word) : AddWrite → Word
  | .move src => s src
  | .movcond equal yes no => s (if condTest equal pred then yes else no)
  | .oneBit equal negate => if condTest equal pred then if negate then ~~~0 else 1 else 0

def flipCondition (m : Metadata SlotTemp) (dst yes no : SlotTemp) : Bool :=
  conditionPreference (m.masks no) > conditionPreference (m.masks yes) ||
    (conditionPreference (m.masks no) == conditionPreference (m.masks yes) && dst == yes)

def foldAddWrite (m : Metadata SlotTemp) (dst src : SlotTemp) : AddWrite :=
  let yes := preferredSlotCopy m src
  let no := preferredSlotCopy m dst
  if emittedScan m yes no (136 + 2 ^ 64) = some true then .move yes
  else
    let swap := flipCondition m dst yes no
    let (left, right) := if swap then (no, yes) else (yes, no)
    let t := m.masks left
    let f := m.masks right
    if t.known && f.known then
      if t.mayOne = 1 ∧ f.mayOne = 0 then .oneBit swap false
      else if t.mayOne = 0 ∧ f.mayOne = 1 then .oneBit (!swap) false
      else if t.mayOne = ~~~0#64 ∧ f.mayOne = 0 then .oneBit swap true
      else if t.mayOne = 0 ∧ f.mayOne = ~~~0#64 then .oneBit (!swap) true
      else .movcond swap left right
    else .movcond swap left right

private theorem analyticAddWrite (s : State SlotTemp) (pred : Word) (polarity : Bool)
    (left right : SlotTemp) (t f : Masks) (ts : t.sound (s left)) (fs : f.sound (s right)) :
    (if t.known && f.known then
      if t.mayOne = 1 ∧ f.mayOne = 0 then AddWrite.oneBit polarity false
      else if t.mayOne = 0 ∧ f.mayOne = 1 then .oneBit (!polarity) false
      else if t.mayOne = ~~~0#64 ∧ f.mayOne = 0 then .oneBit polarity true
      else if t.mayOne = 0 ∧ f.mayOne = ~~~0#64 then .oneBit (!polarity) true
      else .movcond polarity left right
    else AddWrite.movcond polarity left right).value s pred =
    (AddWrite.movcond polarity left right).value s pred := by
  by_cases ht : t.known = true <;> by_cases hf : f.known = true <;> simp only [ht, hf, Bool.and_self,
    Bool.false_and, Bool.and_false, ite_true, Bool.false_eq_true, ite_false]
  have tRead := readKnown _ _ ts ht
  have fRead := readKnown _ _ fs hf
  split
  · rename_i h
    cases polarity <;> by_cases zero : pred = 0#64 <;>
      simp [AddWrite.value, condTest, zero, tRead, fRead, h.1, h.2]
  · split
    · rename_i h
      cases polarity <;> by_cases zero : pred = 0#64 <;>
        simp [AddWrite.value, condTest, zero, tRead, fRead, h.1, h.2]
    · split
      · rename_i h
        cases polarity <;> by_cases zero : pred = 0#64 <;>
          simp [AddWrite.value, condTest, zero, tRead, fRead, h.1, h.2]
      · split
        · rename_i h
          cases polarity <;> by_cases zero : pred = 0#64 <;>
            simp [AddWrite.value, condTest, zero, tRead, fRead, h.1, h.2]
        · rfl

theorem foldAddWrite_correct (m : Metadata SlotTemp) (s : State SlotTemp) (sound : m.sound s)
    (dst src : SlotTemp) (pred : Word) :
    (foldAddWrite m dst src).value s pred = if pred = 0 then s dst else s src := by
  dsimp only [foldAddWrite]
  split
  · rename_i copied
    have same := scannedCopies m s sound _ _ (emittedScanSound m _ _ _ true copied)
    simp only [AddWrite.value, preferredSlotCopy_read m s sound] at same ⊢
    split
    · exact same.symm
    · rfl
  · cases flip : flipCondition m dst (preferredSlotCopy m src) (preferredSlotCopy m dst) <;>
      simp only [flip, ite_true, Bool.false_eq_true, ite_false] <;>
      rw [analyticAddWrite s pred _ _ _ _ _ (sound.2.2 _) (sound.2.2 _)] <;>
      by_cases zero : pred = 0#64 <;> simp [AddWrite.value, condTest, zero, preferredSlotCopy_read m s sound]

def predicateWord (qp : Bool) : Word := if qp then 1 else 0

def optimizedAddWrite (s : IA64Tcg.State α) (i : IA64Tcg.Slot) : Word :=
  let r := closedAddRun s i (scratchTemp 4)
  let op := foldAddWrite r.final (registerTemp i.dst) r.inputs.dst
  op.value r.after (predicateWord (s.pr i.qp))

theorem optimizedAddWrite_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    optimizedAddWrite s i =
      IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst)
        (IA64Integer.SDM.ADD (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3)) := by
  let r := closedAddRun s i (scratchTemp 4)
  have source := closedAddPlan s i (scratchTemp 4) rfl
  have initial := literalSound s
  have valid := translationSound r.pool source r.before (initializedSound _ _ initial) initial
  have old : r.after (registerTemp i.dst) = s.gr i.dst := by
    have eq := congrArg (fun a => a.1 (registerTemp i.dst)) valid.1
    have projected : r.after (registerTemp i.dst) = r.before (registerTemp i.dst) := by
      simpa [run, ScalarInput.unoptimized, addSource, Operation.run, ScalarRun.after, write,
        registerNotScratch] using eq
    exact projected.trans (registerTempRead s i.dst)
  have value : r.after r.inputs.dst =
      IA64Integer.SDM.ADD (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) := by
    have branch := closedBranchlessAdd s i
    obtain ⟨emitted, he, ho⟩ := branch.compiled
    exact (optimizedBranchlessPayload r.pool r.before r.inputs i.imm r.target none r.options
      (valueTag .ADD) branch.immediate branch.ctz branch.constants he ho).trans
      (sdmValue _ _ _ _ .ADD branch.values.1 branch.values.2)
  change (foldAddWrite r.final (registerTemp i.dst) r.inputs.dst).value r.after
    (predicateWord (s.pr i.qp)) = _
  have afterSound : r.final.sound r.after := valid.2.1
  rw [foldAddWrite_correct r.final r.after afterSound, old, value]
  cases s.pr i.qp <;> rfl

def commitOptimizedWord (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (word : Word) :
    IA64Tcg.Outcome α → IA64Tcg.Outcome α
  | .committed after => .committed { after with gr := IA64Tcg.update s.gr i.dst word }
  | .nullified after => .nullified { after with gr := IA64Tcg.update s.gr i.dst word }
  | .fault e ip ri raw before => .fault e ip ri raw before

def sourceOptimizedAdd (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    IA64Tcg.Outcome α :=
  let ordinary := optimizedOrdinary (closedAddRun s i (registerTemp i.dst)) s i facts .ADD registerTemp
  if IA64Alu.fastGuard i facts then
    ((IA64Alu.fastStep s i facts .ADD).map (commitOptimizedWord s i (optimizedAddWrite s i))).getD ordinary
  else ordinary

theorem sourceOptimizedAdd_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    sourceOptimizedAdd s i facts = optimizedData
      (closedAddRun s i (registerTemp i.dst)) (closedAddRun s i (scratchTemp 4)) s i facts .ADD registerTemp := by
  let r := closedAddRun s i (scratchTemp 4)
  have ready := closedBranchlessAdd s i
  obtain ⟨emitted, he, ho⟩ := ready.compiled
  have result : r.after r.inputs.dst =
      IA64Integer.SDM.ADD (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) :=
    (optimizedBranchlessPayload r.pool r.before r.inputs i.imm r.target none r.options
      (valueTag .ADD) ready.immediate ready.ctz ready.constants he ho).trans
      (sdmValue _ _ _ _ .ADD ready.values.1 ready.values.2)
  dsimp only [sourceOptimizedAdd, optimizedData]
  rw [optimizedAddWrite_eq]
  have matchValue (outcome : IA64Tcg.Outcome α) :
      commitOptimizedWord s i
        (IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst) (r.after r.inputs.dst)) outcome =
        setFastWord s i (r.after r.inputs.dst) outcome := by
    cases outcome <;> rfl
  rw [← result]
  rw [funext matchValue]

def sourceOptimizedAddBundle (low high ip : Word) (index : BitVec 2) (s : IA64Add.Joined α)
    (facts : IA64Tcg.Facts) (context : IA64Add.Context) : IA64Add.Boundary α :=
  let i := IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)
  IA64Add.emittedSlot s i (sourceOptimizedAdd s.data i facts)
    {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index}

theorem sourceOptimizedAddBundleRefines (low high ip : Word) (index : BitVec 2)
    (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context)
    (present : IA64Alu.selected .ADD low high ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts)
    (entry : IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
      {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index}) :
    ∃ inputs, IA64Add.OperandChoice s.data
      (IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
      sourceOptimizedAddBundle low high ip index s facts context =
        IA64Alu.sdmBundleStep .ADD low high ip index s inputs context := by
  have eq : sourceOptimizedAddBundle low high ip index s facts context =
      closedAddBundle low high ip index s facts context := by
    simp only [sourceOptimizedAddBundle, closedAddBundle, optimizedBundle, sourceOptimizedAdd_eq]
  rw [eq]
  exact closedAddBundleRefines low high ip index s facts context present sound entry

end IA64OptimizedSlot
