/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Add

open IA64Slot

def decodedBySDM (raw ip : Word) (ri : Fin 3) : IA64Tcg.Slot :=
  ⟨registerField raw 6, registerField raw 13, registerField raw 20,
    (raw.extractLsb' 0 6).toFin, 0, ip, ri, raw, false, false, false, false⟩

theorem emittedDecoded_eq (raw ip : Word) (ri : Fin 3) :
    emittedDecoded raw ip ri = decodedBySDM raw ip ri := by
  have qp : (cDecodedQualifier raw).extractLsb' 0 6 = raw.extractLsb' 0 6 := by
    rw [cDecodedQualifier_matches]
    unfold SDM.qualifier
    bv_decide
  simp only [emittedDecoded, decodedBySDM, qp]

def realSlot (ri : Fin 3) : Slot :=
  if ri.val = 0 then .first else if ri.val = 1 then .second else .third

inductive Route (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) where
  | ordinary
  | branchless (allowed : IA64Tcg.FastApplicability i facts)

def emittedData (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    Route i facts → IA64Tcg.Outcome α
  | .ordinary => IA64Tcg.finish s i (IA64Tcg.prepareProjected s i facts)
      (fun state insn => IA64Tcg.sourceBody_ADD state insn facts)
  | .branchless _ => IA64Tcg.sourceFastStep_ADD s i facts

theorem emittedData_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (route : Route i facts) (sound : IA64Tcg.Sound s i facts) :
    emittedData s i facts route = IA64Tcg.sdStep s i IA64Tcg.alu_ADD := by
  cases route with
  | ordinary =>
    exact IA64Tcg.compose s i IA64Tcg.alu_ADD (fun s i => IA64Tcg.prepareProjected s i facts)
      (fun s i => IA64Tcg.sourceBody_ADD s i facts)
      (IA64Tcg.prepareProjected_eq s i facts sound)
      (fun _ => IA64Tcg.sourceBody_ADD_eq s i facts sound)
  | branchless gate => exact IA64Tcg.fastStep_ADD s i facts sound gate

structure Operands where
  left : Word
  right : Word
  natLeft : Bool
  natRight : Bool

def representedOperands (s : IA64Tcg.State α) (i : IA64Tcg.Slot) : Operands :=
  ⟨IA64Tcg.SDM.value s i.r2, IA64Tcg.SDM.value s i.r3,
   IA64Tcg.SDM.nat s i.r2, IA64Tcg.SDM.nat s i.r3⟩

/-- SDM vol. 1 §4.1.1 leaves an out-of-frame source read undefined. A
    represented input may select such a value and its NaT. -/
def OperandChoice (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (input : Operands) : Prop :=
  (i.r2.val < 32 + s.sof → input.left = IA64Tcg.SDM.value s i.r2 ∧
       input.natLeft = IA64Tcg.SDM.nat s i.r2) ∧
  (i.r3.val < 32 + s.sof → input.right = IA64Tcg.SDM.value s i.r3 ∧
       input.natRight = IA64Tcg.SDM.nat s i.r3)

def referenceData (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (inps : Operands) :
    IA64Tcg.Outcome α :=
  IA64Tcg.finish s i (IA64Tcg.SDM.prepare s i) fun s i =>
    IA64Tcg.SDM.commit s i.dst (IA64Integer.SDM.ADD inps.left inps.right)
                             (inps.natLeft || inps.natRight)

theorem referenceData_represented (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    referenceData s i (representedOperands s i) = IA64Tcg.sdStep s i IA64Tcg.alu_ADD := rfl

structure Context where
  knownRI : Option Word
  knownIC : Option Bool
  record : Bool
  trackSuppression : Bool
  debugDynamic : Bool
  debugFlag : Bool
  trapDynamic : Bool
  singleStepFlag : Bool
  virtualAddress : Bool
  vaImplemented : Bool
  paImplemented : Bool
  stopAfter : Bool

structure Joined (α : Type) where
  data : IA64Tcg.State α
  control : IA64Slot.State

inductive Boundary (α : Type) where
  | debug (atEntry : Joined α) (address : Address)
  | fault (beforeBody : Joined α) (kind : IA64Tcg.Fault) (address : Address) (raw : Word)
  | suppression (afterBody : Joined α) (executed : Bool)
  | native (afterBody : Joined α) (executed : Bool) (call : NativeRequest)
  | retirement (next : Joined α) (executed : Bool) (nextGroup : Bool)

def setEntry (s : IA64Slot.State) (slot : Slot) : IA64Slot.State :=
  { s with psr := SDM.withRI s.psr slot.number }

-- See vol. 2 §5.3, step 7. This is the mask saved before the instruction.
def savedAtEntry (s : IA64Slot.State) (track : Bool) : IA64Slot.State :=
  if track then
    { s with savedSuppression := s.psr &&&
         ((1#64 <<< 37#64) ||| (1#64 <<< 38#64) ||| (1#64 <<< 39#64) |||
          (1#64 <<< 43#64) ||| (1#64 <<< 45#64)) }
  else s

theorem captureSuppression_eq (s : IA64Slot.State) (track : Bool) :
    captureSuppression s track = savedAtEntry s track := by
  cases track <;> rfl

structure EntryFacts (s : Joined α) (i : IA64Tcg.Slot) (c : Context) : Prop where
  atIp : s.control.ip = i.ip
  ri : RISound s.control c.knownRI
  collecting : ∀ v, c.knownIC = some v →
    SDM.collecting (setEntry s.control (realSlot i.ri)).psr = v
  shared : c.record = false → SDM.collecting (setEntry s.control (realSlot i.ri)).psr = true →
    s.control.lastSuccess = i.ip &&& ~~~15#64
  debug : c.debugDynamic = false →
    c.debugFlag = SDM.instructionDebug (setEntry s.control (realSlot i.ri)).psr

def sourceRetire (s : Joined α) (bundle : Word) (slot : Slot) (executed : Bool) (c : Context)
    (long : Bool := false) :
    Boundary α :=
  let noted := (emitSuccess c.knownIC c.record c.trackSuppression bundle).run s.control
  let current := { s with control := noted.1 }
  if .clearSuppression ∈ noted.2 then .suppression current executed
  else
    let native := (emitNative noted.1 false c.trapDynamic c.singleStepFlag c.virtualAddress
                    c.vaImplemented c.paImplemented bundle slot long).run noted.1
    match native.2 with
    | .native call :: _ => .native current executed call
    | _ =>
      let next := (emitAdvance (some slot.number) bundle slot long).run noted.1
      .retirement { s with control := next.1 } executed c.stopAfter

def referenceRetire (s : Joined α) (bundle : Word) (slot : Slot) (executed : Bool) (c : Context)
    (long : Bool := false) :
    Boundary α :=
  let success := { s.control with lastSuccess := SDM.success s.control bundle }
  let current := { s with control := success }
  if c.trackSuppression && s.control.savedSuppression != 0 then .suppression current executed
  else if c.trapDynamic || c.singleStepFlag ||
      !(if c.virtualAddress then c.vaImplemented else c.paImplemented) then
    .native current executed (SDM.trapPayload s.control bundle slot long)
  else .retirement { s with control := SDM.restart success bundle slot long } executed c.stopAfter

theorem sourceRetire_eq (s : Joined α) (bundle : Word) (slot : Slot) (executed : Bool) (c : Context)
    (ip : s.control.ip = bundle) (ri : SDM.ri s.control.psr = slot.number)
    (ic : ∀ v, c.knownIC = some v → SDM.collecting s.control.psr = v)
    (share : c.record = false → SDM.collecting s.control.psr = true →
      s.control.lastSuccess = bundle &&& ~~~15#64) (long : Bool := false) :
    sourceRetire s bundle slot executed c long = referenceRetire s bundle slot executed c long := by
  let success := { s.control with lastSuccess := SDM.success s.control bundle }
  have note := emittedSuccess s.control c.knownIC c.record c.trackSuppression bundle ic share
  have native := emittedNative success s.control false c.trapDynamic c.singleStepFlag
    c.virtualAddress c.vaImplemented c.paImplemented bundle slot long rfl
  have riSound : RISound success (some slot.number) := by
    intro n selected
    exact ri.trans (Option.some.inj selected)
  have advance := emittedAdvance success (some slot.number) bundle slot long riSound ip
  unfold sourceRetire referenceRetire
  rw [note]
  by_cases suppress : (c.trackSuppression && s.control.savedSuppression != 0) = true
  · simp only [if_pos suppress, List.mem_cons_self, if_true]
  · simp only [if_neg suppress, List.not_mem_nil, if_false]
    rw [native]
    by_cases traps : (c.trapDynamic || c.singleStepFlag ||
      !(if c.virtualAddress then c.vaImplemented else c.paImplemented)) = true
    · simp only [if_pos traps]
    · simp only [if_neg traps]
      rw [advance]

theorem slotIndexBound (slot : Slot) : slot.number < 4 := by cases slot <;> decide

theorem psrInstalledRI (s : IA64Slot.State) (slot : Slot) :
    SDM.ri (setEntry s slot).psr = slot.number := by
  have bound := slotIndexBound slot
  simp only [setEntry, SDM.ri, SDM.withRI] at *
  bv_decide

def faultBoundary (s : Joined α) (kind : IA64Tcg.Fault)
    (ip : Word) (ri : Fin 3) (raw : Word) : Boundary α :=
  .fault { s with control := { s.control with ip := ip } } kind ⟨ip, (realSlot ri).number⟩ raw

def emittedSlot (s : Joined α) (i : IA64Tcg.Slot) (body : IA64Tcg.Outcome α)
    (c : Context) (long : Bool := false) : Boundary α :=
  let slot := realSlot i.ri
  let entered := (emitRI c.knownRI slot.number).run s.control |>.1
  if Event.debug ⟨i.ip, slot.number⟩ ∈ ((emitDebug c.debugDynamic c.debugFlag ⟨i.ip, slot.number⟩).run entered).2
  then .debug { s with control := entered } ⟨i.ip, slot.number⟩
  else
    let issued := captureSuppression entered c.trackSuppression
    match body with
    | .fault kind ip ri raw data =>
      .fault ⟨data, emitFaultIP issued ip⟩ kind ⟨ip, (realSlot ri).number⟩ raw
    | .committed data => sourceRetire ⟨data, issued⟩ i.ip slot true c long
    | .nullified data => sourceRetire ⟨data, issued⟩ i.ip slot false c long

def referenceSlot (s : Joined α) (i : IA64Tcg.Slot) (body : IA64Tcg.Outcome α)
    (c : Context) (long : Bool := false) : Boundary α :=
  let slot := realSlot i.ri
  let entered := setEntry s.control slot
  if SDM.instructionDebug entered.psr then .debug { s with control := entered } ⟨i.ip, slot.number⟩
  else
    let issued := savedAtEntry entered c.trackSuppression
    match body with
    | .fault kind ip ri raw data => faultBoundary ⟨data, issued⟩ kind ip ri raw
    | .committed data => referenceRetire ⟨data, issued⟩ i.ip slot true c long
    | .nullified data => referenceRetire ⟨data, issued⟩ i.ip slot false c long

theorem emittedSlot_eq (s : Joined α) (i : IA64Tcg.Slot) (body : IA64Tcg.Outcome α)
    (c : Context) (entry : EntryFacts s i c) (long : Bool := false) :
    emittedSlot s i body c long = referenceSlot s i body c long := by
  let slot := realSlot i.ri
  let entered := setEntry s.control slot
  let afterSave := savedAtEntry entered c.trackSuppression
  have enter : (emitRI c.knownRI (realSlot i.ri).number).run s.control =
      (setEntry s.control (realSlot i.ri), []) :=
    emittedRI s.control c.knownRI slot.number entry.ri (slotIndexBound slot)
  have debug := emittedDebug (setEntry s.control (realSlot i.ri)) c.debugDynamic c.debugFlag
    ⟨i.ip, (realSlot i.ri).number⟩ entry.debug
  have ip : afterSave.ip = i.ip := by
    cases tracking : c.trackSuppression <;> simpa [afterSave, savedAtEntry, tracking] using entry.atIp
  have ri : SDM.ri afterSave.psr = slot.number := by
    cases tracking : c.trackSuppression <;>
      simpa [afterSave, savedAtEntry, tracking] using psrInstalledRI s.control slot
  have ic : ∀ v, c.knownIC = some v → SDM.collecting afterSave.psr = v := by
    cases tracking : c.trackSuppression <;>
      simpa [afterSave, savedAtEntry, tracking] using entry.collecting
  have share : c.record = false → SDM.collecting afterSave.psr = true →
      afterSave.lastSuccess = i.ip &&& ~~~15#64 := by
    cases tracking : c.trackSuppression <;>
      simpa [afterSave, savedAtEntry, tracking] using entry.shared
  have retired (data : IA64Tcg.State α) (executed : Bool) :=
    sourceRetire_eq ⟨data, afterSave⟩ i.ip slot executed c ip ri ic share long
  simp only [emittedSlot, referenceSlot]
  rw [enter, debug]
  by_cases preempt : SDM.instructionDebug (setEntry s.control (realSlot i.ri)).psr = true
  · simp only [if_pos preempt, List.mem_cons_self, if_true]
  · simp only [if_neg preempt, List.not_mem_nil, if_false, captureSuppression_eq]
    cases body with
    | committed _ => exact retired _ true
    | nullified _ => exact retired _ false
    | fault _ _ _ _ _ => rfl

def emittedStep (s : Joined α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (route : Route i facts) (c : Context) : Boundary α :=
  emittedSlot s i (emittedData s.data i facts route) c

def referenceStep (s : Joined α) (i : IA64Tcg.Slot) (operands : Operands) (c : Context) :
    Boundary α :=
  referenceSlot s i (referenceData s.data i operands) c

theorem emittedStep_eq (s : Joined α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (route : Route i facts) (c : Context)
    (sound : IA64Tcg.Sound s.data i facts) (entry : EntryFacts s i c) :
    emittedStep s i facts route c = referenceStep s i (representedOperands s.data i) c := by
  rw [emittedStep, referenceStep, emittedData_eq s.data i facts route sound, referenceData_represented]
  exact emittedSlot_eq _ _ _ _ entry

theorem decodedAddRefines (s : Joined α) (raw ip : Word) (ri : Fin 3)
    (facts : IA64Tcg.Facts) (c : Context)
    (route : Route (emittedDecoded raw ip ri) facts)
    (sound : IA64Tcg.Sound s.data (emittedDecoded raw ip ri) facts)
    (entry : EntryFacts s (emittedDecoded raw ip ri) c) :
    ∃ operands, OperandChoice s.data (decodedBySDM raw ip ri) operands ∧
      emittedStep s (emittedDecoded raw ip ri) facts route c =
        referenceStep s (decodedBySDM raw ip ri) operands c := by
  refine ⟨representedOperands s.data (decodedBySDM raw ip ri), ⟨fun _ => ⟨rfl, rfl⟩, fun _ => ⟨rfl, rfl⟩⟩, ?_⟩
  rw [emittedStep_eq _ _ _ _ _ sound entry, emittedDecoded_eq]

/-- This selector is for already recognized native A1 ADD records. The
    decoded record has no placement, reserved-field or model feature flag. -/
def fastGuard (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) : Prop :=
  i.qp ≠ 0 ∧ facts.predicate = none ∧ i.dst ≠ 0 ∧
  i.dst.val < 32 ∧ i.r2.val < 32 ∧ i.r3.val < 32

instance (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) : Decidable (fastGuard i facts) := by
  unfold fastGuard
  infer_instance

def chosenRoute (raw ip : Word) (ri : Fin 3) (facts : IA64Tcg.Facts) :
    Route (emittedDecoded raw ip ri) facts :=
  if fast : fastGuard (emittedDecoded raw ip ri) facts then
    .branchless ⟨rfl, rfl, rfl, rfl, fast.1, fast.2.1, fast.2.2.1,
                 fast.2.2.2.1, fast.2.2.2.2.1, fast.2.2.2.2.2⟩
  else .ordinary

theorem selectedAddRefines (s : Joined α) (raw ip : Word) (ri : Fin 3)
    (facts : IA64Tcg.Facts) (c : Context)
    (sound : IA64Tcg.Sound s.data (emittedDecoded raw ip ri) facts)
    (entry : EntryFacts s (emittedDecoded raw ip ri) c) :
    ∃ operands, OperandChoice s.data (decodedBySDM raw ip ri) operands ∧
      emittedStep s (emittedDecoded raw ip ri) facts (chosenRoute raw ip ri facts) c =
        referenceStep s (decodedBySDM raw ip ri) operands c :=
  decodedAddRefines s raw ip ri facts c (chosenRoute raw ip ri facts) sound entry

inductive Unit where
  | memory | integer

/-- The native A1 register ADD (Vol. 3 table 4-9). The unnamed bit 36
    may take either value. Bundle legality establishes the selected M/I slot. -/
structure NativeRegisterAdd where
  unit : Unit
  raw : BitVec 41
  ip : Word
  ri : Fin 3
  major : raw.extractLsb' 37 4 = 8
  extensions : raw.extractLsb' 27 9 = 0

theorem nativeRegisterAdd (insn : NativeRegisterAdd) (s : Joined α)
    (facts : IA64Tcg.Facts) (c : Context)
    (sound : IA64Tcg.Sound s.data (emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri) facts)
    (entry : EntryFacts s (emittedDecoded (insn.raw.zeroExtend 64) insn.ip insn.ri) c) :
    let raw := insn.raw.zeroExtend 64
    ∃ operands, OperandChoice s.data (decodedBySDM raw insn.ip insn.ri) operands ∧
      emittedStep s (emittedDecoded raw insn.ip insn.ri) facts
          (chosenRoute raw insn.ip insn.ri facts) c =
        referenceStep s (decodedBySDM raw insn.ip insn.ri) operands c :=
  selectedAddRefines s _ _ _ facts c sound entry

end IA64Add
