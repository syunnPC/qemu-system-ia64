/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64ExceptionEntry

open IA64Add

namespace SDM

def stepHistory (psr last bundle : Word) : Word :=
  if collecting psr then bundle else last

def singleStepEntry (before : Before) (bundle : Word) (slot : IA64Slot.Slot) : Observation :=
  let next := IA64Slot.SDM.successor bundle slot false
  let collect := collecting before.psr
  let saved := if collect then
      ⟨next.bundle, withRestartSlot before.psr (next.slot.setWidth 2), bundle, false⟩
    else before.saved
  ⟨before.iva + 0x6000, interruptionPsr before.psr before.dcr,
    (illegalIsr (slot.number.setWidth 2) (!collect || before.icInFlight) ||| 8) |||
      (if before.cfle then 1#64 <<< 38 else 0),
    if before.icInFlight then none else some saved, false, true⟩

end SDM

theorem sequential_single_step (before : Before) (control : IA64Slot.State) (bundle : Word)
    (slot : IA64Slot.Slot) (va pa : Bool)
    (step : control.psr.getLsbD 40 = true)
    (implemented : (if control.psr.getLsbD 36 then va else pa) = true)
    (bundleAligned : bundle &&& 15 = 0) :
    let call := IA64Slot.SDM.trapPayload control bundle slot false
    let next := IA64Slot.SDM.successor bundle slot false
    let live := { before with
      psr := SDM.withRestartSlot before.psr (next.slot.setWidth 2)
      isr := 8
      lastSuccessfulBundle := SDM.stepHistory control.psr before.lastSuccessfulBundle bundle
      nativeCompletionTrap := true }
    cCompletion before call va pa =
      some (live, ⟨next.bundle, next.bundle, bundle, slot.number.setWidth 32, 38, 38, 38⟩) := by
  have align : bundle &&& ~~~15#64 = bundle := by bv_decide
  have ss : control.psr &&& 1099511627776#64 ≠ 0 := by
    have h := step
    generalize control.psr = psr at h ⊢
    bv_decide
  have mode : (control.psr &&& 68719476736#64 = 0) =
      (control.psr.getLsbD 36 = false) := by
    generalize control.psr = psr
    bv_decide
  have ic : (control.psr &&& 8192#64 = 0) = (SDM.collecting control.psr = false) := by
    unfold SDM.collecting
    generalize control.psr = psr
    bv_decide
  cases va <;> cases pa <;> cases it : control.psr.getLsbD 36 <;> cases slot <;>
    simp [it] at implemented <;>
    simp [cCompletion, IA64Slot.SDM.trapPayload, IA64Slot.SDM.successor, IA64Slot.SDM.completionSlot,
      IA64Slot.Slot.number, cSingleStepNumber, SDM.stepHistory, raise_packet_with_code,
      ss, mode, it, ic, align] <;>
    first | rfl | (unfold SDM.withRestartSlot; generalize before.psr = psr; bv_decide)

private theorem step_vector (before : Before) (bundle history : Word) (slot : IA64Slot.Slot)
    (bundleAligned : bundle &&& 15 = 0) (ivaAligned : before.iva &&& 0x7fff = 0)
    (completeFrame : before.cfle = false) :
    let next := IA64Slot.SDM.successor bundle slot false
    let live := { before with psr := SDM.withRestartSlot before.psr (next.slot.setWidth 2),
                              isr := 8, lastSuccessfulBundle := history, nativeCompletionTrap := true }
    let p : Pending := ⟨next.bundle, next.bundle, bundle, slot.number.setWidth 32, 38, 38, 38⟩
    cStepVector live p = SDM.singleStepEntry before bundle slot := by
  have slotValid : slot.number.setWidth 32 < 3 := by cases slot <;> decide
  have nextAligned : (IA64Slot.SDM.successor bundle slot false).bundle &&& 15 = 0 := by
    cases slot <;> simp [IA64Slot.SDM.successor] <;> bv_decide
  have collect (n : BitVec 2) :
      SDM.collecting (SDM.withRestartSlot before.psr n) = SDM.collecting before.psr := by
    unfold SDM.collecting SDM.withRestartSlot
    generalize before.psr = psr
    bv_decide
  have handler (n : BitVec 2) :
      SDM.interruptionPsr (SDM.withRestartSlot before.psr n) before.dcr =
        SDM.interruptionPsr before.psr before.dcr := by
    unfold SDM.interruptionPsr SDM.withRestartSlot
    generalize before.psr = psr
    generalize before.dcr = dcr
    bv_decide
  have vector : ((before.iva &&& 18446744073709518848#64) ||| 24576#64) = before.iva + 24576#64 := by
    generalize before.iva = iva at ivaAligned ⊢
    bv_decide
  dsimp [cStepVector, SDM.singleStepEntry, observe]
  rw [collect_matches, collect, handlerPsr_matches, handler, iip_matches _ nextAligned,
    isr_matches _ _ _ slotValid]
  cases slot <;> simp [IA64Slot.Slot.number, vector, completeFrame] <;>
    exact BitVec.or_comm (8#64) _

def sequentialStepObservation (cfg : Before) (boundary : Boundary α) (va pa : Bool) :
    Option (IA64Tcg.State α × Bool × Observation) :=
  match boundary with
  | .native completed executed call =>
    let before := beforeFromBoundary cfg completed
    (cCompletion before call va pa).bind fun (live, pending) =>
      if pending.exceptionIndex = cSingleStepNumber ∧
          before.psr.getLsbD 34 = false ∧ before.ia32Transition = false then
        some (completed.data, executed, cStepVector live pending)
      else none
  | _ => none

theorem sequential_step_observed (cfg : Before) (completed : Joined α) (executed : Bool)
    (bundle : Word) (slot : IA64Slot.Slot) (va pa : Bool)
    (step : completed.control.psr.getLsbD 40 = true)
    (implemented : (if completed.control.psr.getLsbD 36 then va else pa) = true)
    (native : nativeOrdinary (beforeFromBoundary cfg completed))
    (bundleAligned : bundle &&& 15 = 0) (ivaAligned : cfg.iva &&& 0x7fff = 0)
    (completeFrame : cfg.cfle = false) :
    sequentialStepObservation cfg
      (.native completed executed (IA64Slot.SDM.trapPayload completed.control bundle slot false)) va pa =
      some (completed.data, executed, SDM.singleStepEntry (beforeFromBoundary cfg completed) bundle slot) := by
  dsimp only [sequentialStepObservation]
  rw [sequential_single_step _ _ _ _ _ _ step implemented bundleAligned]
  dsimp only [Option.bind]
  rw [if_pos ⟨rfl, native.1, native.2.1⟩]
  rw [step_vector (beforeFromBoundary cfg completed) bundle _ slot bundleAligned ivaAligned completeFrame]

private theorem retire_request (s completed : Joined α) (bundle : Word) (slot : IA64Slot.Slot)
    (executed emitted : Bool) (ctx : Context) (request : IA64Slot.NativeRequest)
    (issued : referenceRetire s bundle slot executed ctx = .native completed emitted request) :
    request = IA64Slot.SDM.trapPayload completed.control bundle slot false := by
  dsimp only [referenceRetire] at issued
  by_cases suppressed : (ctx.trackSuppression && s.control.savedSuppression != 0) = true
  · rw [if_pos suppressed] at issued
    contradiction
  · rw [if_neg suppressed] at issued
    by_cases needed : (ctx.trapDynamic || ctx.singleStepFlag ||
        !(if ctx.virtualAddress then ctx.vaImplemented else ctx.paImplemented)) = true
    · rw [if_pos needed] at issued
      cases issued
      rfl
    · rw [if_neg needed] at issued
      contradiction

private theorem reference_request (s completed : Joined α) (i : IA64Tcg.Slot)
    (outcome : IA64Tcg.Outcome α) (ctx : Context) (executed : Bool)
    (request : IA64Slot.NativeRequest)
    (issued : referenceSlot s i outcome ctx = .native completed executed request) :
    request = IA64Slot.SDM.trapPayload completed.control i.ip (realSlot i.ri) false := by
  dsimp only [referenceSlot] at issued
  by_cases debug : IA64Slot.SDM.instructionDebug (setEntry s.control (realSlot i.ri)).psr = true
  · rw [if_pos debug] at issued
    contradiction
  · rw [if_neg debug] at issued
    cases outcome
    · exact retire_request _ _ _ _ _ _ _ _ issued
    · exact retire_request _ _ _ _ _ _ _ _ issued
    · cases issued

open FullDecodeSpec FullDecodeImplementation IA64OptimizedSlot

theorem optimized_alu_sequential_step_enters [DecidableEq Temp]
    (ordinary branchless : ScalarRun Temp) (tag : IA64Alu.Opcode) (low high ip : Word)
    (index : BitVec 2) (state completed : Joined α) (facts : IA64Tcg.Facts)
    (ctx : Context) (regs : IA64Tcg.Reg → Temp) (extract : Option (BitVec 6)) (cfg : Before)
    (executed : Bool) (request : IA64Slot.NativeRequest)
    (present : IA64Alu.selected tag low high ip index)
    (sound : IA64Tcg.Sound state.data (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)) facts)
    (entry : EntryFacts state (IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index))
      {ctx with stopAfter := cStop (templateBits low) index})
    (normal :
      let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
      ¬ (IA64Alu.fastGuard i facts ∧ IA64Alu.conditionalFast tag = true) →
      IA64Tcg.prepareProjected state.data i facts = .execute → OrdinaryRun ordinary state.data i tag regs)
    (fast :
      let i := IA64Alu.emittedRecord (IA64Alu.decode tag low high ip index)
      IA64Alu.fastGuard i facts → IA64Alu.conditionalFast tag = true →
      BranchlessRun branchless state.data i tag extract)
    (issued : optimizedBundle ordinary branchless tag low high ip index state facts ctx regs =
      .native completed executed request)
    (step : completed.control.psr.getLsbD 40 = true)
    (implemented : (if completed.control.psr.getLsbD 36 then ctx.vaImplemented else ctx.paImplemented) = true)
    (native : nativeOrdinary (beforeFromBoundary cfg completed))
    (bundleAligned : ip &&& 15 = 0) (ivaAligned : cfg.iva &&& 0x7fff = 0)
    (completeFrame : cfg.cfle = false) :
    let i := IA64Alu.sdmRecord tag (slotBits low high index) ip index
    (∃ inputs, OperandChoice state.data i inputs ∧
      IA64Alu.sdmBundleStep tag low high ip index state inputs ctx = .native completed executed request) ∧
    sequentialStepObservation cfg
      (optimizedBundle ordinary branchless tag low high ip index state facts ctx regs)
      ctx.vaImplemented ctx.paImplemented =
      some (completed.data, executed, SDM.singleStepEntry (beforeFromBoundary cfg completed) ip (realSlot i.ri)) := by
  obtain ⟨inputs, operands, related⟩ := optimizedBundleRefines ordinary branchless tag low high ip index
    state facts ctx regs extract present sound entry normal fast
  have reference := related.symm.trans issued
  refine ⟨⟨inputs, operands, reference⟩, ?_⟩
  rw [issued]
  unfold IA64Alu.sdmBundleStep at reference
  rw [reference_request _ _ _ _ _ _ _ reference]
  exact sequential_step_observed cfg completed executed ip _ _ _ step implemented native bundleAligned ivaAligned completeFrame

private def stepState : Joined _root_.Unit :=
  ⟨⟨fun _ => 0, fun _ => 0, fun _ => false, fun _ => false, 0, ()⟩,
    ⟨0x100, 0x50000002000, 0x0f0, 0, 0, false⟩⟩

private def stepConfig : Before :=
  ⟨0, 0, 0x8000, 0, ⟨0, 0, 0, true⟩, 0, false, false, false, false⟩

example :
    (sequentialStepObservation stepConfig
      (.native stepState true (IA64Slot.SDM.trapPayload stepState.control 0x100 .third false))
      true true).map (fun (_, _, result) => (result.ip, result.isr, result.saved)) =
    some (0xe000, 0x40000000008, some ⟨0x110, 0x10000002000, 0x100, false⟩) := by
  rw [sequential_step_observed stepConfig stepState true 0x100 .third true true
    rfl rfl ⟨rfl, rfl, rfl⟩ rfl rfl rfl] <;> rfl

end IA64ExceptionEntry
