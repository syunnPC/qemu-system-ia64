/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64OptimizedSlot

open IA64Optimizer IA64Slot IA64Add FullDecodeImplementation

structure ScalarRun (Temp : Type) where
  before : IA64Optimizer.State Temp
  inputs : PayloadRegs Temp
  target : PayloadTarget
  options : Nat → FoldOptions
  pool : ConstantPool Temp
  final : Metadata Temp
  ops : List (Operation Temp)

def ScalarRun.after [DecidableEq Temp] (r : ScalarRun Temp) : IA64Optimizer.State Temp :=
  (run r.before r.ops).1

def operandValues (r : ScalarRun Temp) (s : IA64Tcg.State α) (i : IA64Tcg.Slot) : Prop :=
  r.before r.inputs.x = IA64Tcg.SDM.value s i.r2 ∧
    r.before r.inputs.y = IA64Tcg.SDM.value s i.r3

structure OrdinaryRun [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp) : Prop where
  values : operandValues r s i
  readRegs : ∀ gr, r.before (regs gr) = s.gr gr
  destination : regs i.dst = r.inputs.dst
  unique : ∀ ⦃a b⦄, regs a = regs b → a = b
  scratch : ∀ gr, regs gr ∉ [r.inputs.tmp, r.inputs.tmp2, r.inputs.low, r.inputs.high]
  constants : r.pool.sound r.before
  inputs : PayloadInputs r.before r.inputs i.imm (valueTag tag)
  compiled : Translation r.pool (initialized r.pool.readonly)
    (aPayload r.inputs i.imm r.target r.options (valueTag tag)) r.ops r.final

theorem projectedGR [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp)
    (ready : OrdinaryRun r s i tag regs) :
    (fun gr => r.after (regs gr)) = (IA64Tcg.SDM.operate s i (IA64Alu.operation tag)).gr := by
  funext gr
  by_cases target : gr = i.dst
  · subst gr
    rw [ready.destination]
    have result := optimizedPayload r.pool r.before r.inputs i.imm r.target r.options
      (valueTag tag) ready.inputs ready.constants ready.compiled
    simpa only [IA64Tcg.SDM.operate, IA64Tcg.SDM.commit, IA64Tcg.update, if_pos rfl] using
      result.trans (sdmValue _ _ _ _ tag ready.values.1 ready.values.2)
  · have different : regs gr ≠ r.inputs.dst := by
      rw [← ready.destination]
      exact fun alias => target (ready.unique alias)
    have separated : regs gr ∉ [r.inputs.dst, r.inputs.tmp, r.inputs.tmp2, r.inputs.low, r.inputs.high] := by
      simp only [List.mem_cons, not_or]
      exact ⟨different, by simpa only [List.mem_cons, not_or] using ready.scratch gr⟩
    have untouched := optimizedUntouched r.pool r.before r.inputs i.imm r.target r.options
      (valueTag tag) (regs gr) separated ready.constants ready.compiled
    simpa only [ScalarRun.after, ready.readRegs, IA64Tcg.SDM.operate, IA64Tcg.SDM.commit,
      IA64Tcg.update, if_neg target] using untouched

def optimizedOrdinary [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp) :
    IA64Tcg.Outcome α :=
  IA64Tcg.finish s i (IA64Tcg.prepareProjected s i facts) fun s i =>
    { IA64Alu.ordinaryBody s i facts tag with gr := fun gr => r.after (regs gr) }

theorem ordinaryEffects [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp)
    (sound : IA64Tcg.Sound s i facts)
    (compiled : IA64Tcg.prepareProjected s i facts = .execute → OrdinaryRun r s i tag regs) :
    optimizedOrdinary r s i facts tag regs = IA64Alu.ordinaryStep tag s i facts := by
  unfold optimizedOrdinary IA64Alu.ordinaryStep
  cases h : IA64Tcg.prepareProjected s i facts with
  | fault _ => rfl
  | nullified => rfl
  | execute =>
    simp only [IA64Tcg.finish, projectedGR r s i tag regs (compiled h),
      IA64Alu.ordinaryBody_eq s i facts tag sound]

structure BranchlessRun [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (tag : IA64Alu.Opcode) (extract : Option (BitVec 6)) : Prop where
  values : operandValues r s i
  immediate : needsImmediate (valueTag tag) = true → r.before r.inputs.immediate = i.imm
  ctz : permitsFastExtract i.imm extract
  constants : r.pool.sound r.before
  compiled : ∃ emitted, fastPayload r.inputs i.imm r.target extract r.options (valueTag tag) = some emitted ∧
    Translation r.pool (initialized r.pool.readonly) emitted r.ops r.final

def setFastWord (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (result : Word)
    (outcome : IA64Tcg.Outcome α) : IA64Tcg.Outcome α :=
  let value := IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst) result
  let gr := IA64Tcg.update s.gr i.dst value
  match outcome with
  | .committed state => .committed { state with gr := gr }
  | .nullified state => .nullified { state with gr := gr }
  | fault => fault

theorem branchlessEffects [DecidableEq Temp] (r : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) (tag : IA64Alu.Opcode) (extract : Option (BitVec 6))
    (sound : IA64Tcg.Sound s i facts) (guard : IA64Tcg.FastApplicability i facts)
    (supported : IA64Alu.conditionalFast tag = true) (ready : BranchlessRun r s i tag extract) :
    (IA64Alu.fastStep s i facts tag).map (setFastWord s i (r.after r.inputs.dst)) =
      IA64Alu.fastStep s i facts tag := by
  obtain ⟨_, emitted, optimized⟩ := ready.compiled
  have word := (optimizedBranchlessPayload r.pool r.before r.inputs i.imm r.target extract r.options
    (valueTag tag) ready.immediate ready.ctz ready.constants emitted optimized).trans
    (sdmValue _ _ _ _ tag ready.values.1 ready.values.2)
  change r.after r.inputs.dst = _ at word
  rw [IA64Alu.fastStep_eq s i facts tag sound guard, if_pos supported, word]
  have target : IA64Tcg.SDM.legalTarget s i.dst = true := by
    simp [IA64Tcg.SDM.legalTarget, guard.dstNonzero, guard.dstStatic]
  cases qualified : s.pr i.qp <;>
    simp [IA64Tcg.sdStep, IA64Tcg.SDM.prepare, IA64Tcg.SDM.qualified, IA64Tcg.finish,
      guard.qpNonzero, guard.valid, guard.placement, guard.reserved, guard.feature,
      target, qualified, setFastWord, IA64Tcg.updateSelf, IA64Integer.SDM.qualifiedWrite,
      IA64Tcg.SDM.operate, IA64Tcg.SDM.commit]

def optimizedData [DecidableEq Temp] (ordinary branchless : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp) :
    IA64Tcg.Outcome α :=
  let regular := optimizedOrdinary ordinary s i facts tag regs
  if IA64Alu.fastGuard i facts then
    ((IA64Alu.fastStep s i facts tag).map (setFastWord s i (branchless.after branchless.inputs.dst))).getD regular
  else regular

theorem dataEffects [DecidableEq Temp] (ordinary branchless : ScalarRun Temp) (s : IA64Tcg.State α)
    (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) (tag : IA64Alu.Opcode) (regs : IA64Tcg.Reg → Temp)
    (extract : Option (BitVec 6)) (sound : IA64Tcg.Sound s i facts)
    (normal : ¬ (IA64Alu.fastGuard i facts ∧ IA64Alu.conditionalFast tag = true) →
      IA64Tcg.prepareProjected s i facts = .execute → OrdinaryRun ordinary s i tag regs)
    (fast : IA64Alu.fastGuard i facts → IA64Alu.conditionalFast tag = true →
      BranchlessRun branchless s i tag extract) :
    optimizedData ordinary branchless s i facts tag regs = IA64Alu.chosenData tag s i facts := by
  by_cases gate : IA64Alu.fastGuard i facts
  · have guard := IA64Alu.guardAccepted i facts gate
    by_cases selected : IA64Alu.conditionalFast tag = true
    · rw [optimizedData, IA64Alu.chosenData, if_pos gate, if_pos gate,
        branchlessEffects branchless s i facts tag extract sound guard selected (fast gate selected)]
      rw [IA64Alu.fastStep_eq s i facts tag sound guard, if_pos selected]
      rfl
    · rw [optimizedData, IA64Alu.chosenData, if_pos gate, if_pos gate,
        IA64Alu.fastStep_eq s i facts tag sound guard, if_neg selected]
      exact ordinaryEffects ordinary s i facts tag regs sound (normal fun both => selected both.2)
  · rw [optimizedData, IA64Alu.chosenData, if_neg gate, if_neg gate]
    exact ordinaryEffects ordinary s i facts tag regs sound (normal fun both => gate both.1)

def optimizedBundle [DecidableEq Temp] (ordinary branchless : ScalarRun Temp) (tag : IA64Alu.Opcode)
    (low high ip : Word) (index : BitVec 2) (s : Joined α) (facts : IA64Tcg.Facts)
    (context : Context) (regs : IA64Tcg.Reg → Temp) : Boundary α :=
  let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
  emittedSlot s i (optimizedData ordinary branchless s.data i facts tag regs)
    {context with stopAfter := cStop (FullDecodeSpec.templateBits low) index}

theorem optimizedBundleRefines [DecidableEq Temp]
    (ordinary branchless : ScalarRun Temp) (tag : IA64Alu.Opcode) (low high ip : Word) (index : BitVec 2)
    (s : Joined α) (facts : IA64Tcg.Facts) (context : Context) (regs : IA64Tcg.Reg → Temp)
    (extract : Option (BitVec 6)) (present : IA64Alu.selected tag low high ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)) facts)
    (entry : EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index))
      {context with stopAfter := cStop (FullDecodeSpec.templateBits low) index})
    (normal :
      let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
      ¬ (IA64Alu.fastGuard i facts ∧ IA64Alu.conditionalFast tag = true) →
      IA64Tcg.prepareProjected s.data i facts = .execute → OrdinaryRun ordinary s.data i tag regs)
    (fast :
      let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
      IA64Alu.fastGuard i facts → IA64Alu.conditionalFast tag = true →
      BranchlessRun branchless s.data i tag extract) :
    ∃ inputs, OperandChoice s.data
      (IA64Alu.sdmRecord tag (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
      optimizedBundle ordinary branchless tag low high ip index s facts context regs =
        IA64Alu.sdmBundleStep tag low high ip index s inputs context := by
  unfold optimizedBundle
  dsimp
  rw [dataEffects ordinary branchless _ _ facts tag regs extract sound normal fast]
  exact IA64Alu.bundleRefines tag low high ip index s facts context present sound entry

end IA64OptimizedSlot
