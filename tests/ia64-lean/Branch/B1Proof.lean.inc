/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Branch.B1

open IA64Slot
set_option maxRecDepth 16384
set_option maxHeartbeats 2000000

def cRun (c : Context) (s : State) (prs : BitVec 6 → Word) (i : Instruction) : Result :=
  let issued := ((emitRI c.ri i.slot.number).run s).1
  let (_, debugEvents) := (emitDebug c.debugDynamic c.debug ⟨i.address, i.slot.number⟩).run issued
  match debugEvents with
  | event :: _ => ⟨issued, .helper event⟩
  | [] =>
    let completed := ((emitSuccess c.ic c.record false i.address).run issued).1
    if cTake c prs i.qualifier then
      let target := cTarget i.address i.displacement
      match ((cNativeTaken c issued target i.address i.slot).run completed).2 with
      | event :: _ => ⟨completed, .helper event⟩
      | [] =>
        let group := ((emitGroupAtExit true).run completed).1
        let cleared := ((emitRI none 0).run group).1
        ⟨{cleared with ip := target}, .taken i.slot.number (cTransfer c target)⟩
    else
      let next := IA64Slot.SDM.successor i.address i.slot false
      match ((emitNative completed false c.nativeDynamic c.singleStep c.virtual
          (c.vaImplemented next.bundle) (c.paImplemented next.bundle)
          i.address i.slot false).run completed).2 with
      | event :: _ => ⟨completed, .helper event⟩
      | [] =>
        ⟨((emitAdvance (some i.slot.number) i.address i.slot false).run completed).1,
          .next c.stop⟩

theorem choosePredicate (c : Context) (s : State) (prs : BitVec 6 → Word)
    (i : Instruction) (sound : Sound c s prs i) :
    cTake c prs i.qualifier = predValue prs i.qualifier := by
  unfold cTake
  split
  · exact (sound.zero (by assumption)).symm
  · split
    · exact (sound.one (by assumption)).symm
    · simp only [predValue, IA64Slot.SDM.qualified, IA64Slot.SDM.qualifier]
      have h (qp : BitVec 6) : ((qp.zeroExtend 64).extractLsb' 0 6).zeroExtend 64 =
          qp.zeroExtend 64 := by bv_decide
      rw [h]
      by_cases qp : i.qualifier = 0
      · simp [qp]
      · have ext : i.qualifier.setWidth 64 ≠ 0 := by
          have hqp := qp
          bv_decide
        simp only [if_neg qp, decide_eq_false qp, Bool.false_or, BitVec.zeroExtend, beq_eq_false_iff_ne.mpr ext]

theorem architecturalDisplacementAligned (raw : FullDecodeSpec.Slot) :
    short raw &&& 15#64 = 0 := by
  simp only [short, IA64Branch.SDM.shortDisp]
  bv_decide

theorem nativeTakenRequest (c : Context) (s completed : State) (i : Instruction) (target : Word)
    (prs : BitVec 6 → Word) (sound : Sound c s prs i) :
    ((cNativeTaken c s target i.address i.slot).run completed).2 =
      if requested c s true target then
        [.native ⟨⟨target, 0⟩, ⟨i.address, i.slot.number⟩, 4 * i.slot.number, 1, 0, s.psr⟩]
      else [] := by
  simp only [requested, Bool.true_and, ← sound.ss, ← sound.tb, cNativeTaken]
  cases i.slot <;> split <;> rfl

theorem runRefines (c : Context) (s : State) (prs : BitVec 6 → Word)
    (i : Instruction) (sound : Sound c s prs i)
    (dispAlign : i.displacement &&& 15#64 = 0) :
    (cRun c s prs i).project = sdmRun c s prs i := by
  have slotBounds : i.slot.number < 4 := by cases i.slot <;> decide
  have ri := emittedRI s c.ri i.slot.number sound.riFact slotBounds
  rw [← riUnchanged s.psr i.slot.number sound.ri] at ri
  have issue : (emitRI c.ri i.slot.number).run s = (s, []) := ri
  have note := emittedSuccess s c.ic c.record false i.address sound.icFact sound.shared
  have debug := emittedDebug s c.debugDynamic c.debug ⟨i.address, i.slot.number⟩ sound.debug
  have target : cTarget i.address i.displacement = IA64Branch.SDM.directTarget i.address i.displacement :=
    IA64Branch.directTarget_BR_COND i.address i.displacement sound.aligned dispAlign
  have selected := choosePredicate c s prs i sound
  let completed := archSuccess s i.address
  have noteState : ((emitSuccess c.ic c.record false i.address).run s).1 = completed :=
    congrArg Prod.fst note
  have still : completed.psr = s.psr := rfl
  have current : completed.ip = i.address := sound.ip
  have tracked : RISound completed (some i.slot.number) := by
    intro n hn
    have hn' : i.slot.number = n := Option.some.inj hn
    exact sound.ri.trans hn'
  let next := IA64Slot.SDM.successor i.address i.slot false
  have sequential := emittedNative completed s false c.nativeDynamic c.singleStep c.virtual
      (c.vaImplemented next.bundle) (c.paImplemented next.bundle) i.address i.slot false still
  have advance := emittedAdvance completed (some i.slot.number) i.address i.slot false tracked current
  have clear := emittedRI {completed with groupStart := true} none 0 (by simp [RISound]) (by decide)
  simp only [cRun, sdmRun, issue, Prod.fst, debug]
  by_cases db : IA64Slot.SDM.instructionDebug s.psr = true
  · simp [db, Result.project]
  · simp only [Bool.not_eq_true] at db
    simp only [db, Bool.false_eq_true, if_false, Prod.snd, noteState]
    rw [selected, target]
    by_cases taken : predValue prs i.qualifier = true
    · simp only [taken, if_true]
      rw [nativeTakenRequest c s completed i _ prs sound]
      by_cases trap : requested c s true (IA64Branch.SDM.directTarget i.address i.displacement) = true
      · simp only [trap, if_true, Result.project]
        rfl
      · simp only [if_neg trap, emittedGroup, Prod.fst, clear, Result.project]
        rfl
    · simp only [if_neg taken]
      rw [sequential]
      have eq : requested c s false next.bundle =
          (c.nativeDynamic || c.singleStep ||
          !(if c.virtual then c.vaImplemented next.bundle else c.paImplemented next.bundle)) := by
        simp [requested, implemented, sound.ss]
      rw [eq]
      by_cases trap : (c.nativeDynamic || c.singleStep ||
          !(if c.virtual then c.vaImplemented next.bundle else c.paImplemented next.bundle)) = true
      · simp only [trap, if_true, Prod.snd, Result.project]
        rfl
      · simp only [if_neg trap, Prod.snd, Result.project]
        rw [advance]

private def padded (unit : FullDecodeSpec.UnitCode) (raw : FullDecodeSpec.Slot)
    (ip : Word) (index : BitVec 2) : FullDecodeSpec.Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    (raw.extractLsb' 0 6).zeroExtend 8 ++ 0#8 ++ 0#8 ++ 0#8 ++
    (0#48 ++ short raw ++ 2#82)

private theorem paddedTail (immediate : Word) :
    0#8 ++ 0#8 ++ 0#8 ++ 0#8 ++ 0#8 ++ 0#8 ++ immediate ++
      0#32 ++ 0#32 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++
      0#1 ++ 0#1 ++ 0#4 ++ 0#4 ++ 1#1 ++ 0#1 =
    0#48 ++ immediate ++ 2#82 := by
  bv_decide

private theorem paddedRecord (unit : FullDecodeSpec.UnitCode) (raw : FullDecodeSpec.Slot)
    (ip : Word) (index : BitVec 2) :
    FullDecodeSpec.aRecord (.branch (.relative 0)) unit raw ip (index.zeroExtend 8) =
      padded unit raw ip index := by
  have immediate : FullDecodeSpec.aImm (.branch (.relative 0)) raw = short raw := by
    simp only [FullDecodeSpec.aImm, short, IA64Branch.SDM.shortDisp]
    bv_decide
  have tail : FullDecodeSpec.aRecordTail (.branch (.relative 0)) raw =
      0#48 ++ FullDecodeSpec.aImm (.branch (.relative 0)) raw ++ 2#82 := by
    simp only [FullDecodeSpec.aRecordTail, FullDecodeSpec.aPred, FullDecodeSpec.aBranchRegister,
      FullDecodeSpec.aStatusField, FullDecodeSpec.aPredUpdate, FullDecodeSpec.updateByRegister,
      FullDecodeSpec.updateByImmediate, FullDecodeSpec.memAcquire, FullDecodeSpec.memRelease,
      FullDecodeSpec.aUnc, FullDecodeSpec.aCheckFp, FullDecodeSpec.fpLoadAttributes]
    exact paddedTail _
  unfold FullDecodeSpec.aRecord padded
  rw [tail, immediate] <;> rfl

private theorem paddedFields (unit : FullDecodeSpec.UnitCode) (raw : FullDecodeSpec.Slot)
    (ip : Word) (index : BitVec 2) :
    cInstruction (FullDecodeSpec.aRecord (.branch (.relative 0)) unit raw ip
      (index.zeroExtend 8)) = sdmInstruction raw ip index := by
  rw [paddedRecord]
  let wire := padded unit raw ip index
  have addr : wire.extractLsb' 234 64 = ip := by
    simp only [wire, padded]
    bv_decide
  have slot : wire.extractLsb' 226 2 = index := by
    simp only [wire, padded]
    bv_decide
  have qp : wire.extractLsb' 218 6 = raw.extractLsb' 0 6 := by
    simp only [wire, padded]
    bv_decide
  have disp : wire.extractLsb' 82 64 = short raw := by
    simp only [wire, padded]
    bv_decide
  change cInstruction wire = _
  simp only [cInstruction, sdmInstruction, addr, slot, qp, disp]

open FullDecodeSpec FullDecodeImplementation in
theorem decodedB1 (low high ip : BitVec 64) (index : BitVec 2)
    (enc : aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
      (.branch (.relative 0)) low high index) :
    cInstruction (bundleBR_COND low high ip index) = sdmInstruction (slotBits low high index) ip index := by
  rw [(BR_COND_complete low high ip index).1]
  obtain ⟨valid, bound, unit, form⟩ := enc
  simp only [aBundle, aOpcode, if_pos (And.intro valid bound), if_pos (And.intro unit form)]
  apply paddedFields

theorem b1BundleRefines : B1Contract FullDecodeImplementation.bundleBR_COND cRun := by
  intro low high ip index c s prs enc i code sound
  open FullDecodeSpec FullDecodeImplementation in
    have stop : cStop code index = sdmStop code index := by
      have valid := enc.1
      dsimp only [validTemplate] at valid
      simp only [cStop, sdmStop]
      bv_decide
  rw [decodedB1 low high ip index enc, stop]
  apply runRefines
  · cases sound
    constructor <;> assumption
  · apply architecturalDisplacementAligned

private def exampleContext : Context :=
  { ia32 := false, llvm := false, trackSuppression := false
    ri := some 1, ic := some true, record := true, predZero := 0, predOne := 0
    debugDynamic := false, debug := false, nativeDynamic := false
    singleStep := false, takenBranch := false, virtual := false
    vaImplemented := fun _ => true, paImplemented := fun _ => true
    stop := false, chainSlot := 0, natSafe := true, natsKnownClear := false
    anyNat := false, natClearKey := true, deferIRQ := false
    chainDisabled := false, firstIp := 0x1000 }

private def exampleBefore : State :=
  ⟨0x1000, (1#64 <<< 41) ||| (1#64 <<< 13), 0xff0, 0, 0, false⟩

private def exampleEncoding : Instruction :=
  cInstruction (FullDecodeImplementation.bundleBR_COND 0x1001400000000012 0x200000 0x1000 1)

example :
    FullDecodeSpec.aBundleEncoding FullDecodeImplementation.reservedUnit
      FullDecodeImplementation.mUnit FullDecodeImplementation.iUnit FullDecodeImplementation.bUnit
      FullDecodeImplementation.fUnit FullDecodeImplementation.lUnit FullDecodeImplementation.xUnit
      (.branch (.relative 0)) 0x1001400000000012 0x200000 1 := by
  unfold FullDecodeSpec.aBundleEncoding
  decide

example :
    (cRun exampleContext exampleBefore (fun p => if p = 5 then 1 else 0) exampleEncoding).resume =
      .taken 1 (.chain 0) := rfl

example :
    (cRun {exampleContext with anyNat := true} exampleBefore
      (fun p => if p = 5 then 1 else 0) exampleEncoding).resume = .taken 1 .lookup := rfl

example :
    (cRun {exampleContext with chainDisabled := true} exampleBefore
      (fun p => if p = 5 then 1 else 0) exampleEncoding).resume = .taken 1 .lookup := rfl

example :
    (cRun {exampleContext with deferIRQ := true} exampleBefore
      (fun p => if p = 5 then 1 else 0) exampleEncoding).resume = .taken 1 .mainLoop := rfl

example :
    (cRun {exampleContext with chainDisabled := true} exampleBefore
      (fun p => if p = 5 then 1 else 0) exampleEncoding).project =
      (⟨0x1020, 1#64 <<< 13, 0x1000, 0, 0, true⟩, .taken 1) := by decide

example :
    (cRun exampleContext exampleBefore (fun _ => 0) exampleEncoding).project =
      (⟨0x1000, (2#64 <<< 41) ||| (1#64 <<< 13), 0x1000, 0, 0, false⟩, .next false) := by decide

end IA64Branch.B1
