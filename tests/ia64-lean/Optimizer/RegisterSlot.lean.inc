/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64OptimizedSlot

open IA64Optimizer
local notation "Word" => BitVec 64

inductive RegisterOp where
  | add | sub | and | andcm | or | xor

def RegisterOp.tag : RegisterOp → IA64Alu.Opcode
  | .add => .ADD
  | .sub => .SUB
  | .and => .AND
  | .andcm => .ANDCM
  | .or => .OR
  | .xor => .XOR

def compileSlot (m : Metadata SlotTemp) :
    List (ScalarInput SlotTemp) → Option (Metadata SlotTemp × List (Operation SlotTemp))
  | [] => some (m, [])
  | input :: rest => do
    let (after, op) ← translate slotPool m (136 + 2 ^ 64) input
    let (final, ops) ← compileSlot after rest
    pure (final, op :: ops)

private theorem compileSlotRefines (s : State SlotTemp) (m : Metadata SlotTemp)
    (sound : m.sound s) (constants : slotPool.sound s) (program : List (ScalarInput SlotTemp))
    (allowed : ∀ input ∈ program, AlwaysAllowed slotPool input) :
    ∃ final ops, compileSlot m program = some (final, ops) ∧ Translation slotPool m program ops final := by
  induction program generalizing s m with
  | nil => exact ⟨m, [], rfl, .end_ m⟩
  | cons input rest ih =>
    obtain ⟨next, op, emitted⟩ := translationStepExists slotPool m sound.1
      (fun word => ⟨literalTemp word, rfl⟩) input
    have permit := allowed input List.mem_cons_self m
    have step := stepPreserves slotPool m next s _ input op sound constants permit emitted
    obtain ⟨final, ops, produced, refined⟩ := ih (op.run s).1 next step.1 step.2.1
      (fun other member => allowed other (List.mem_cons_of_mem _ member))
    exact ⟨final, op :: ops, by simp [compileSlot, emitted, produced], .next permit emitted refined⟩

def registerTarget (nativeNot : Bool) : PayloadTarget := { allocatingTarget with notOpcode := nativeNot }

def registerRun (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (dst : SlotTemp)
    (target : PayloadTarget) (program : List (ScalarInput SlotTemp)) : ScalarRun SlotTemp :=
  let result := (compileSlot (initialized slotPool.readonly) program).getD (initialized slotPool.readonly, [])
  ⟨slotBefore s, allocatingRegs i dst, target, fun _ => plainOptions, slotPool, result.1, result.2⟩

private theorem registerRunRefines (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (dst : SlotTemp)
    (target : PayloadTarget) (program : List (ScalarInput SlotTemp))
    (allowed : ∀ input ∈ program, AlwaysAllowed slotPool input) :
    let r := registerRun s i dst target program
    Translation r.pool (initialized r.pool.readonly) program r.ops r.final := by
  have constants := literalSound s
  obtain ⟨final, ops, produced, proof⟩ := compileSlotRefines (slotBefore s)
    (initialized slotPool.readonly) (initializedSound _ _ constants) constants program allowed
  simpa only [registerRun, produced, Option.getD_some] using proof

private theorem writableGR (r : IA64Tcg.Reg) : slotPool.readonly (registerTemp r) = none := by
  have bound : ¬136 ≤ r.val := by have := r.isLt; omega
  simp [slotPool, registerTemp, bound]

private theorem operandNotScratch (r : IA64Tcg.Reg) (x : Fin 8) :
    operandTemp r ≠ scratchTemp x := by
  by_cases zero : r = 0
  · simp only [operandTemp, if_pos zero]
    intro eq
    have h := congrArg Fin.val eq
    change 136 + (0#64).toNat = 128 + x.val at h
    have := x.isLt
    omega
  · simpa only [operandTemp, if_neg zero] using registerNotScratch r x

def ordinaryRegisterRun (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (op : RegisterOp) (nativeNot : Bool) :
    ScalarRun SlotTemp :=
  let dst := registerTemp i.dst
  let target := registerTarget nativeNot
  registerRun s i dst target (aPayload (allocatingRegs i dst) i.imm target (fun _ => plainOptions) (valueTag op.tag))

private theorem ordinaryRegisterReady (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (op : RegisterOp)
    (nativeNot : Bool) : OrdinaryRun (ordinaryRegisterRun s i op nativeNot) s i op.tag registerTemp := by
  refine ⟨⟨operandTempRead s i.r2, operandTempRead s i.r3⟩, registerTempRead s, rfl, ?_, ?_,
    literalSound s, ?_, registerRunRefines _ _ _ _ _ ?_⟩
  · intro a b same
    exact Fin.ext (congrArg (fun t : SlotTemp => t.val) same)
  · intro reg
    simp [ordinaryRegisterRun, registerRun, allocatingRegs, registerNotScratch]
  · cases op <;> constructor <;>
      simp [ordinaryRegisterRun, registerRun, allocatingRegs, RegisterOp.tag, valueTag,
        needsImmediate, writesImmediateScratch, usesPointer, operandNotScratch, literalTempRead]
  · intro input member m
    cases op <;> cases nativeNot <;>
      simp [RegisterOp.tag, valueTag, aPayload, registerTarget, allocatingRegs] at member
    all_goals rcases member with rfl | rfl
    all_goals change AllowedInput slotPool m _
    all_goals first
      | exact ⟨writableGR _, by simp [plainOptions], by simp [plainOptions]⟩
      | exact ⟨rfl, by simp [plainOptions], by simp [plainOptions]⟩
      | rfl

def branchlessRegisterRun (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (op : RegisterOp) (nativeNot : Bool) :
    ScalarRun SlotTemp :=
  let dst := scratchTemp 4
  let target := registerTarget nativeNot
  registerRun s i dst target
    ((fastPayload (allocatingRegs i dst) i.imm target none (fun _ => plainOptions) (valueTag op.tag)).getD [])

private theorem branchlessRegisterReady (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (op : RegisterOp) (nativeNot : Bool) (fast : IA64Alu.conditionalFast op.tag = true) :
    BranchlessRun (branchlessRegisterRun s i op nativeNot) s i op.tag none := by
  let r := branchlessRegisterRun s i op nativeNot
  refine ⟨⟨operandTempRead s i.r2, operandTempRead s i.r3⟩, ?_, ?_, literalSound s, ?_⟩
  · cases op <;> simp [RegisterOp.tag, valueTag, needsImmediate]
  · simp [permitsFastExtract]
  · cases op <;> simp [RegisterOp.tag, IA64Alu.conditionalFast] at fast
    all_goals refine ⟨_, rfl, registerRunRefines _ _ _ _ _ ?_⟩
    all_goals
      intro input member m
      simp [RegisterOp.tag, valueTag, fastPayload, allocatingRegs] at member
      subst input
      exact ⟨rfl, by simp [plainOptions], by simp [plainOptions]⟩

def optimizedRegisterWrite (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (op : RegisterOp) (nativeNot : Bool) : Word :=
  let r := branchlessRegisterRun s i op nativeNot
  (foldAddWrite r.final (registerTemp i.dst) r.inputs.dst).value r.after
    (predicateWord (s.pr i.qp))

private theorem branchlessRegisterFootprint (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (op : RegisterOp) (nativeNot : Bool) (program : List (ScalarInput SlotTemp))
    (emitted : let r := branchlessRegisterRun s i op nativeNot
      fastPayload r.inputs i.imm r.target none r.options (valueTag op.tag) = some program) :
    ∀ insn ∈ program, insn.destination = some (scratchTemp 4) := by
  cases op <;>
    simp [branchlessRegisterRun, registerRun, fastPayload, RegisterOp.tag, valueTag] at emitted
  all_goals subst program
  all_goals
    intro insn member
    simp [List.mem_singleton] at member
    subst insn
    rfl

private theorem branchlessRegisterWord (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (op : RegisterOp) (nativeNot : Bool) (fast : IA64Alu.conditionalFast op.tag = true) :
    let r := branchlessRegisterRun s i op nativeNot
    r.after r.inputs.dst = (IA64Alu.operation op.tag).value
      (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) i.imm := by
  let r := branchlessRegisterRun s i op nativeNot
  have ready := branchlessRegisterReady s i op nativeNot fast
  obtain ⟨emitted, he, ho⟩ := ready.compiled
  exact (optimizedBranchlessPayload r.pool r.before r.inputs i.imm r.target none r.options
    (valueTag op.tag) ready.immediate ready.ctz ready.constants he ho).trans
    (sdmValue _ _ _ _ op.tag ready.values.1 ready.values.2)

theorem optimizedRegisterWrite_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
    (op : RegisterOp) (nativeNot : Bool) (fast : IA64Alu.conditionalFast op.tag = true) :
    optimizedRegisterWrite s i op nativeNot =
      IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst)
        ((IA64Alu.operation op.tag).value (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) i.imm) := by
  let r := branchlessRegisterRun s i op nativeNot
  have ready := branchlessRegisterReady s i op nativeNot fast
  obtain ⟨emitted, he, ho⟩ := ready.compiled
  have sound := translationSound r.pool ho r.before (initializedSound _ _ ready.constants) ready.constants
  have old : r.after (registerTemp i.dst) = s.gr i.dst := by
    have before := untouchedRun r.before (registerTemp i.dst) emitted
      (fun insn member => by
        rw [branchlessRegisterFootprint s i op nativeNot emitted he insn member]
        simpa only [ne_eq, Option.some.injEq, eq_comm] using registerNotScratch i.dst 4)
    exact (congrArg (fun a => a.1 (registerTemp i.dst)) sound.1).trans
      (before.trans (registerTempRead s i.dst))
  have afterSound : r.final.sound r.after := sound.2.1
  change (foldAddWrite r.final (registerTemp i.dst) r.inputs.dst).value r.after
      (predicateWord (s.pr i.qp)) = _
  rw [foldAddWrite_correct r.final r.after afterSound, old, branchlessRegisterWord s i op nativeNot fast]
  cases s.pr i.qp <;> rfl

def sourceOptimizedRegister (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (op : RegisterOp) (nativeNot : Bool) : IA64Tcg.Outcome α :=
  let ordinary := optimizedOrdinary (ordinaryRegisterRun s i op nativeNot) s i facts op.tag registerTemp
  if IA64Alu.fastGuard i facts then
    ((IA64Alu.fastStep s i facts op.tag).map
      (commitOptimizedWord s i (optimizedRegisterWrite s i op nativeNot))).getD ordinary
  else ordinary

private theorem sourceOptimizedRegister_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (op : RegisterOp) (nativeNot : Bool) (sound : IA64Tcg.Sound s i facts) :
    sourceOptimizedRegister s i facts op nativeNot =
      optimizedData (ordinaryRegisterRun s i op nativeNot)
        (branchlessRegisterRun s i op nativeNot) s i facts op.tag registerTemp := by
  let r := branchlessRegisterRun s i op nativeNot
  dsimp only [sourceOptimizedRegister, optimizedData]
  by_cases gate : IA64Alu.fastGuard i facts
  · rw [if_pos gate, if_pos gate]
    by_cases fast : IA64Alu.conditionalFast op.tag = true
    · have result : r.after r.inputs.dst = _ := branchlessRegisterWord s i op nativeNot fast
      rw [optimizedRegisterWrite_eq s i op nativeNot fast, ← result]
      have matchValue (outcome : IA64Tcg.Outcome α) :
          commitOptimizedWord s i
            (IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst) (r.after r.inputs.dst)) outcome =
          setFastWord s i (r.after r.inputs.dst) outcome := by
        cases outcome <;> rfl
      rw [funext matchValue]
    · rw [IA64Alu.fastStep_eq s i facts op.tag sound (IA64Alu.guardAccepted i facts gate), if_neg fast]
      rfl
  · rw [if_neg gate, if_neg gate]

def sourceOptimizedRegisterBundle (op : RegisterOp) (nativeNot : Bool) (lo hi ip : Word)
    (index : BitVec 2) (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (ctx : IA64Add.Context) :
    IA64Add.Boundary α :=
  let i := IA64Alu.emittedRecord (IA64Alu.decode op.tag lo hi ip index)
  IA64Add.emittedSlot s i (sourceOptimizedRegister s.data i facts op nativeNot)
    {ctx with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits lo) index}

theorem sourceOptimizedRegisterBundleRefines (op : RegisterOp) (nativeNot : Bool) (lo hi ip : Word)
    (index : BitVec 2) (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (ctx : IA64Add.Context)
    (present : IA64Alu.selected op.tag lo hi ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode op.tag lo hi ip index)) facts)
    (entry : IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode op.tag lo hi ip index))
      {ctx with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits lo) index}) :
    ∃ inputs, IA64Add.OperandChoice s.data
      (IA64Alu.sdmRecord op.tag (FullDecodeSpec.slotBits lo hi index) ip index) inputs ∧
      sourceOptimizedRegisterBundle op nativeNot lo hi ip index s facts ctx =
        IA64Alu.sdmBundleStep op.tag lo hi ip index s inputs ctx := by
  let i := IA64Alu.emittedRecord (IA64Alu.decode op.tag lo hi ip index)
  have eq : sourceOptimizedRegisterBundle op nativeNot lo hi ip index s facts ctx =
    optimizedBundle (ordinaryRegisterRun s.data i op nativeNot) (branchlessRegisterRun s.data i op nativeNot)
      op.tag lo hi ip index s facts ctx registerTemp := by
    simp only [sourceOptimizedRegisterBundle, optimizedBundle, i, sourceOptimizedRegister_eq _ _ _ _ _ sound]
  rw [eq]
  exact optimizedBundleRefines _ _ op.tag lo hi ip index s facts ctx registerTemp none present sound entry
    (fun _ _ => ordinaryRegisterReady _ _ op nativeNot)
    (fun _ fast => branchlessRegisterReady _ _ op nativeNot fast)

theorem optimizedSlotCoverage :
    (∀ {Temp α : Type} [DecidableEq Temp]
      (ordinary branchless : IA64OptimizedSlot.ScalarRun Temp) (tag : IA64Alu.Opcode)
      (low high ip : BitVec 64) (index : BitVec 2)
      (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context)
      (regs : IA64Tcg.Reg → Temp) (extract : Option (BitVec 6)),
      IA64Alu.selected tag low high ip index →
      IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)) facts →
      IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index))
        {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      (let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
       ¬ (IA64Alu.fastGuard i facts ∧ IA64Alu.conditionalFast tag = true) →
       IA64Tcg.prepareProjected s.data i facts = .execute →
       IA64OptimizedSlot.OrdinaryRun ordinary s.data i tag regs) →
      (let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
       IA64Alu.fastGuard i facts → IA64Alu.conditionalFast tag = true →
       IA64OptimizedSlot.BranchlessRun branchless s.data i tag extract) →
      ∃ inputs, IA64Add.OperandChoice s.data
        (IA64Alu.sdmRecord tag (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
        IA64OptimizedSlot.optimizedBundle ordinary branchless tag low high ip index s facts context regs =
          IA64Alu.sdmBundleStep tag low high ip index s inputs context) ∧
    (∀ {α : Type} (low high ip : BitVec 64) (index : BitVec 2)
      (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context),
      IA64Alu.selected .ADD low high ip index →
      IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts →
      IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
        {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      ∃ (ordinary branchless : IA64OptimizedSlot.ScalarRun IA64OptimizedSlot.SlotTemp), ∃ inputs,
        IA64Add.OperandChoice s.data
          (IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
        IA64OptimizedSlot.optimizedBundle ordinary branchless .ADD low high ip index
          s facts context IA64OptimizedSlot.registerTemp =
          IA64Alu.sdmBundleStep .ADD low high ip index s inputs context) ∧
    (∀ {α : Type} (low high ip : BitVec 64) (index : BitVec 2)
      (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context),
      IA64Alu.selected .ADD low high ip index →
      IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts →
      IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
        {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      ∃ inputs, IA64Add.OperandChoice s.data
        (IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
        IA64OptimizedSlot.sourceOptimizedAddBundle low high ip index s facts context =
          IA64Alu.sdmBundleStep .ADD low high ip index s inputs context) ∧
    (∀ {α : Type} (state : IA64Tcg.State α) (i : IA64Tcg.Slot),
      IA64OptimizedSlot.optimizedAddWrite state i =
        IA64Integer.SDM.qualifiedWrite (state.pr i.qp) (state.gr i.dst)
          (IA64Integer.SDM.ADD (IA64Tcg.SDM.value state i.r2) (IA64Tcg.SDM.value state i.r3))) ∧
    (∀ {α : Type} (op : IA64OptimizedSlot.RegisterOp) (nativeNot : Bool)
      (low high ip : BitVec 64) (index : BitVec 2)
      (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context),
      IA64Alu.selected op.tag low high ip index →
      IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode op.tag low high ip index)) facts →
      IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode op.tag low high ip index))
        {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      ∃ inputs, IA64Add.OperandChoice s.data
        (IA64Alu.sdmRecord op.tag (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
        IA64OptimizedSlot.sourceOptimizedRegisterBundle op nativeNot low high ip index s facts context =
          IA64Alu.sdmBundleStep op.tag low high ip index s inputs context) ∧
    (∀ {α : Type} (s : IA64Tcg.State α) (i : IA64Tcg.Slot)
      (op : IA64OptimizedSlot.RegisterOp) (nativeNot : Bool),
      IA64Alu.conditionalFast op.tag = true →
      IA64OptimizedSlot.optimizedRegisterWrite s i op nativeNot =
        IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst)
          ((IA64Alu.operation op.tag).value (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) i.imm)) ∧
    (∀ (dst src : IA64OptimizedSlotLive.AddTemp),
      IA64OptimizedSlotLive.addPool.readonly dst = none →
      IA64OptimizedSlotLive.addPool.readonly src = none →
      IA64Optimizer.Translation IA64OptimizedSlotLive.addPool
        (IA64Optimizer.initialized IA64OptimizedSlotLive.addPool.readonly)
        [IA64OptimizedSlotLive.addMoveInput dst src]
        [if dst = src then .nop else .replace .first dst src src]
        (IA64OptimizedSlotLive.capturedCopy dst src) ∧
      ∀ (old source : BitVec 64) (visits : List Nat)
          (kind : IA64OptimizedSlotLive.AddTemp → Nat),
        let m := IA64OptimizedSlotLive.capturedCopy dst src
        let op := if dst = src then IA64Optimizer.Operation.nop else .replace .first dst src src
        let next := (op.run (IA64OptimizedSlotLive.addEntry old source)).1
        next (visits.foldl (fun current n =>
          IA64Optimizer.pickCopy kind current (IA64Optimizer.follow m dst n)) dst) =
          IA64OptimizedSlotLive.addEntry old source src) ∧
    (∀ (branchless alias : Bool) (old source pred tags : BitVec 64),
      IA64OptimizedSlotLive.observeAddCopy branchless alias old source pred tags =
        (if branchless = true ∧ pred = 0 then old
         else IA64Integer.SDM.ADD (if alias then old else source) 0,
         if branchless = true ∧ pred = 0 then tags else
           IA64Integer.SDM.replaceNaT tags 8 (IA64Integer.SDM.staticNaT tags (if alias then 8 else 9)))) :=
  ⟨@optimizedBundleRefines, @allocatedAddBundleRefines, @sourceOptimizedAddBundleRefines,
    @optimizedAddWrite_eq, @sourceOptimizedRegisterBundleRefines, @optimizedRegisterWrite_eq,
    fun dst src output input =>
    ⟨IA64OptimizedSlotLive.addZeroTrace dst src output input,
      fun old source visits kind =>
        IA64OptimizedSlotLive.capturedCopy_value dst src old source output input visits kind⟩,
    @IA64OptimizedSlotLive.observeAddCopy_matches⟩

end IA64OptimizedSlot
