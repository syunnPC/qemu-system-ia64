/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64ExceptionEntry

open IA64Add FullDecodeSpec FullDecodeImplementation IA64OptimizedSlot

namespace SDM

def addStepData (s : IA64Tcg.State α) (i : IA64Tcg.Slot) : IA64Tcg.State α :=
  if IA64Tcg.SDM.qualified s i.qp then
    IA64Tcg.SDM.commit s i.dst
      (IA64Integer.SDM.ADD (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3))
      (IA64Tcg.SDM.nat s i.r2 || IA64Tcg.SDM.nat s i.r3)
  else s

end SDM

private theorem entryBits (s : IA64Slot.State) (slot : IA64Slot.Slot) (n : Nat)
    (low : n = 24 ∨ n = 34 ∨ n = 36 ∨ n = 40) :
    (setEntry s slot).psr.getLsbD n = s.psr.getLsbD n := by
  rcases low with rfl | rfl | rfl | rfl <;>
    cases slot <;>
    unfold setEntry IA64Slot.SDM.withRI IA64Slot.Slot.number <;>
    generalize s.psr = psr <;> bv_decide

def stepAddCompleted (s : Joined α) (i : IA64Tcg.Slot) : Joined α :=
  let entered := setEntry s.control (realSlot i.ri)
  ⟨SDM.addStepData s.data i, {entered with lastSuccess := IA64Slot.SDM.success entered i.ip}⟩

theorem optimized_add_single_step_deliverable (low high ip : Word) (index : BitVec 2)
    (s : Joined α) (facts : IA64Tcg.Facts) (ctx : Context) (cfg : Before)
    (present : IA64Alu.selected .ADD low high ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts)
    (entry : EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
      {ctx with stopAfter := cStop (templateBits low) index}) :
    let i := IA64Alu.sdmRecord .ADD (slotBits low high index) ip index
    let completed := stepAddCompleted s i
    let executed := IA64Tcg.SDM.qualified s.data i.qp
    i.dst ≠ 0 → i.dst.val < 32 → i.r2.val < 32 → i.r3.val < 32 →
    s.control.psr.getLsbD 24 = false → ctx.trackSuppression = false →
    s.control.psr.getLsbD 40 = true →
    (ctx.trapDynamic = false → ctx.singleStepFlag = s.control.psr.getLsbD 40) →
    (if s.control.psr.getLsbD 36 then ctx.vaImplemented else ctx.paImplemented) = true →
    nativeOrdinary (beforeFromBoundary cfg s) →
    ip &&& 15 = 0 → cfg.iva &&& 0x7fff = 0 → cfg.cfle = false →
    sourceOptimizedAddBundle low high ip index s facts ctx =
      .native completed executed (IA64Slot.SDM.trapPayload completed.control ip (realSlot i.ri) false) ∧
    sequentialStepObservation cfg (sourceOptimizedAddBundle low high ip index s facts ctx)
      ctx.vaImplemented ctx.paImplemented =
      some (completed.data, executed, SDM.singleStepEntry (beforeFromBoundary cfg completed) ip (realSlot i.ri)) := by
  intro i completed executed nonzero dst src2 src3 debug track step known addressed native
    alignedIp alignedIva frame
  obtain ⟨input, choice, related⟩ := sourceOptimizedAddBundleRefines low high ip index s facts ctx
    present sound entry
  change OperandChoice s.data i input at choice
  obtain ⟨left, natLeft⟩ := choice.1 (by omega)
  obtain ⟨right, natRight⟩ := choice.2 (by omega)
  have target : IA64Tcg.SDM.legalTarget s.data i.dst = true := by
    simp [IA64Tcg.SDM.legalTarget, dst, nonzero]
  let entered := setEntry s.control (realSlot i.ri)
  have debugOff : IA64Slot.SDM.instructionDebug entered.psr = false := by
    have bit := entryBits s.control (realSlot i.ri) 24 (Or.inl rfl)
    dsimp only [entered, IA64Slot.SDM.instructionDebug]
    rw [← bit] at debug
    generalize (setEntry s.control (realSlot i.ri)).psr = psr at debug ⊢
    bv_decide
  have needed : (ctx.trapDynamic || ctx.singleStepFlag) = true := by
    cases dynamic : ctx.trapDynamic <;> simp [dynamic, known, step]
  have stepLive : completed.control.psr.getLsbD 40 = true :=
    (entryBits s.control (realSlot i.ri) 40 (by simp)).trans step
  have mode : completed.control.psr.getLsbD 36 = s.control.psr.getLsbD 36 :=
    entryBits s.control (realSlot i.ri) 36 (by simp)
  have liveNative : nativeOrdinary (beforeFromBoundary cfg completed) :=
    ⟨(entryBits s.control (realSlot i.ri) 34 (by simp)).trans native.1, native.2⟩
  have issued :
      sourceOptimizedAddBundle low high ip index s facts ctx =
        .native completed executed (IA64Slot.SDM.trapPayload completed.control ip (realSlot i.ri) false) := by
    rw [related]
    change referenceSlot s i (IA64Alu.sdmData .ADD s.data i input) _ = _
    dsimp only [referenceSlot]
    rw [if_neg (by simp [show IA64Slot.SDM.instructionDebug
      (setEntry s.control (realSlot i.ri)).psr = false from debugOff])]
    have clean : i.malformed = false ∧ i.illegalPlacement = false ∧ i.reservedField = false ∧
        i.missingFeature = false := ⟨rfl, rfl, rfl, rfl⟩
    have ipEq : i.ip = ip := rfl
    cases qualified : IA64Tcg.SDM.qualified s.data i.qp <;>
      simp [IA64Alu.sdmData, IA64Tcg.SDM.prepare, clean.1, clean.2.1, clean.2.2.1, clean.2.2.2,
        qualified, IA64Tcg.finish, target, referenceRetire, savedAtEntry, track, needed,
        completed, stepAddCompleted, SDM.addStepData, executed, ipEq, left, right, natLeft, natRight,
        IA64Alu.operation, IA64Tcg.alu_ADD, IA64Integer.SDM.sourceNaT, IA64Slot.SDM.trapPayload,
        IA64Slot.SDM.successor, IA64Slot.SDM.completionSlot]
  refine ⟨issued, ?_⟩
  rw [issued]
  exact sequential_step_observed cfg completed executed ip (realSlot i.ri) _ _
    stepLive (by rw [mode]; exact addressed) liveNative alignedIp alignedIva frame

private def steppingAddState : Joined _root_.Unit :=
  { addState with
    data := { addState.data with gr := fun r => if r = 1 then 0x55 else if r = 2 then 0x2b else 0 }
    control := { addState.control with psr := 0x10000002000 } }

private def steppingAddContext : Context :=
  { addContext with singleStepFlag := true }

private def steppingAddConfig : Before := { addBefore with cfle := false }

example :
    (sequentialStepObservation steppingAddConfig
      (sourceOptimizedAddBundle (addFixture 8) 0 0x100 0 steppingAddState addFacts steppingAddContext)
      true true).map (fun (data, executed, entered) => (data.gr 8, executed, entered.ip, entered.isr, entered.saved)) =
    some (0x80, true, 0xe000, 8, some ⟨0x100, 0x30000002000, 0x100, false⟩) := by
  have present : IA64Alu.selected .ADD (addFixture 8) 0 0x100 0 := by
    apply ((IA64Alu.decodeComplete .ADD) (addFixture 8) 0 0x100 0).2.mpr
    unfold aBundleEncoding
    decide
  have sound : IA64Tcg.Sound steppingAddState.data
      (IA64Alu.emittedRecord (IA64Alu.decode .ADD (addFixture 8) 0 0x100 0)) addFacts := by
    simp [IA64Tcg.Sound, addFacts, steppingAddState, addState]
  have entry : EntryFacts steppingAddState
      (IA64Alu.emittedRecord (IA64Alu.decode .ADD (addFixture 8) 0 0x100 0))
      {steppingAddContext with stopAfter := cStop (templateBits (addFixture 8)) 0} := by
    refine ⟨?_, ?_, ?_, ?_, ?_⟩
    · rw [IA64Alu.decodedRecord_eq .ADD _ _ _ _ present] <;> rfl
    · simp [IA64Slot.RISound, steppingAddContext, addContext]
    · simp [steppingAddContext, addContext]
    · simp [steppingAddContext, addContext]
    · simp [steppingAddContext, addContext]
  have delivered := optimized_add_single_step_deliverable (addFixture 8) 0 0x100 0
    steppingAddState addFacts steppingAddContext steppingAddConfig present sound entry
    (by decide) (by decide) (by decide) (by decide)
    rfl rfl rfl (fun _ => rfl) rfl ⟨rfl, rfl, rfl⟩ rfl rfl rfl
  change (sequentialStepObservation steppingAddConfig
    (sourceOptimizedAddBundle (addFixture 8) 0 0x100 0 steppingAddState addFacts steppingAddContext)
    steppingAddContext.vaImplemented steppingAddContext.paImplemented).map _ = _
  rw [delivered.2] <;> rfl

theorem optimized_alu_single_step_coverage :
    (∀ {Temp α : Type} [DecidableEq Temp]
      (ordinary branchless : IA64OptimizedSlot.ScalarRun Temp)
      (tag : IA64Alu.Opcode) (low high ip : BitVec 64) (index : BitVec 2)
      (state completed : IA64Add.Joined α) (facts : IA64Tcg.Facts)
      (ctx : IA64Add.Context) (regs : IA64Tcg.Reg → Temp) (extract : Option (BitVec 6))
      (cfg : IA64ExceptionEntry.Before) (executed : Bool) (request : IA64Slot.NativeRequest),
      IA64Alu.selected tag low high ip index →
      IA64Tcg.Sound state.data (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)) facts →
      IA64Add.EntryFacts state (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index))
        {ctx with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      (let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
       ¬ (IA64Alu.fastGuard i facts ∧ IA64Alu.conditionalFast tag = true) →
       IA64Tcg.prepareProjected state.data i facts = .execute →
       IA64OptimizedSlot.OrdinaryRun ordinary state.data i tag regs) →
      (let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
       IA64Alu.fastGuard i facts → IA64Alu.conditionalFast tag = true →
       IA64OptimizedSlot.BranchlessRun branchless state.data i tag extract) →
      IA64OptimizedSlot.optimizedBundle ordinary branchless tag low high ip index state facts ctx regs =
        .native completed executed request →
      completed.control.psr.getLsbD 40 = true →
      (if completed.control.psr.getLsbD 36 then ctx.vaImplemented else ctx.paImplemented) = true →
      IA64ExceptionEntry.nativeOrdinary (IA64ExceptionEntry.beforeFromBoundary cfg completed) →
      ip &&& 15 = 0 → cfg.iva &&& 0x7fff = 0 → cfg.cfle = false →
      let i := IA64Alu.sdmRecord tag (FullDecodeSpec.slotBits low high index) ip index
      (∃ inputs, IA64Add.OperandChoice state.data i inputs ∧
        IA64Alu.sdmBundleStep tag low high ip index state inputs ctx = .native completed executed request) ∧
      IA64ExceptionEntry.sequentialStepObservation cfg
        (IA64OptimizedSlot.optimizedBundle ordinary branchless tag low high ip index state facts ctx regs)
        ctx.vaImplemented ctx.paImplemented =
        some (completed.data, executed,
          IA64ExceptionEntry.SDM.singleStepEntry (IA64ExceptionEntry.beforeFromBoundary cfg completed)
            ip (IA64Add.realSlot i.ri))) ∧
    (∀ {α : Type} (low high ip : BitVec 64) (index : BitVec 2)
      (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (ctx : IA64Add.Context)
      (cfg : IA64ExceptionEntry.Before),
      IA64Alu.selected .ADD low high ip index →
      IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts →
      IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
        {ctx with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index} →
      let i := IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index
      let completed := IA64ExceptionEntry.stepAddCompleted s i
      let executed := IA64Tcg.SDM.qualified s.data i.qp
      i.dst ≠ 0 → i.dst.val < 32 → i.r2.val < 32 → i.r3.val < 32 →
      s.control.psr.getLsbD 24 = false → ctx.trackSuppression = false →
      s.control.psr.getLsbD 40 = true →
      (ctx.trapDynamic = false → ctx.singleStepFlag = s.control.psr.getLsbD 40) →
      (if s.control.psr.getLsbD 36 then ctx.vaImplemented else ctx.paImplemented) = true →
      IA64ExceptionEntry.nativeOrdinary (IA64ExceptionEntry.beforeFromBoundary cfg s) →
      ip &&& 15 = 0 → cfg.iva &&& 0x7fff = 0 → cfg.cfle = false →
      IA64OptimizedSlot.sourceOptimizedAddBundle low high ip index s facts ctx =
        .native completed executed
          (IA64Slot.SDM.trapPayload completed.control ip (IA64Add.realSlot i.ri) false) ∧
      IA64ExceptionEntry.sequentialStepObservation cfg
        (IA64OptimizedSlot.sourceOptimizedAddBundle low high ip index s facts ctx)
        ctx.vaImplemented ctx.paImplemented =
        some (completed.data, executed, IA64ExceptionEntry.SDM.singleStepEntry
          (IA64ExceptionEntry.beforeFromBoundary cfg completed) ip (IA64Add.realSlot i.ri))) :=
  ⟨@optimized_alu_sequential_step_enters, @optimized_add_single_step_deliverable⟩

end IA64ExceptionEntry
