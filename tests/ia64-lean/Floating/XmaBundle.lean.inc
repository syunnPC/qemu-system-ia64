/- SPDX-License-Identifier: GPL-2.0-or-later -/

set_option maxRecDepth 16384
set_option maxHeartbeats 2000000
set_option exponentiation.threshold 512

open FullDecodeImplementation

def f2Form : XmaForm → FullDecodeSpec.Form
  | .low => .f2 0 false
  | .signedHigh => .f2 3 false
  | .unsignedHigh => .f2 2 false
  | .unsignedMultiply => .f2 2 true

def f2Wire : XmaForm → Word → Word → Word → BitVec 2 → FullDecodeSpec.Observation
  | .low => bundleXMA_L
  | .signedHigh => bundleXMA_H
  | .unsignedHigh => bundleXMA_HU
  | .unsignedMultiply => bundleXMPY_HU

def f2Encoding (form : XmaForm) (low high : Word) (index : BitVec 2) : Prop :=
  FullDecodeSpec.aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
    (f2Form form) low high index

private theorem f2WireContract (form : XmaForm) :
    FullDecodeSpec.BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit
      (f2Form form) (f2Wire form) := by
  cases form
  · exact XMA_L_complete
  · exact XMA_H_complete
  · exact XMA_HU_complete
  · exact XMPY_HU_complete

structure XmaInstruction where
  operands : XmaOperands
  qualifier : BitVec 6
  raw : FullDecodeSpec.Slot
  address : Word
  slot : BitVec 2

def cF2Fields (wire : FullDecodeSpec.Observation) : XmaInstruction :=
  ⟨⟨wire.extractLsb' 210 7, wire.extractLsb' 202 7, wire.extractLsb' 194 7,
      wire.extractLsb' 186 7⟩,
    wire.extractLsb' 218 6, wire.extractLsb' 298 41, wire.extractLsb' 234 64, wire.extractLsb' 226 2⟩

def f2Fields (raw : FullDecodeSpec.Slot) (ip : Word) (index : BitVec 2) : XmaInstruction :=
  ⟨⟨raw.extractLsb' 6 7, raw.extractLsb' 13 7, raw.extractLsb' 20 7, raw.extractLsb' 27 7⟩,
    raw.extractLsb' 0 6, raw, ip, index⟩

private def paddedF2 (unit : FullDecodeSpec.UnitCode) (raw : FullDecodeSpec.Slot)
    (ip : Word) (index : BitVec 2) : FullDecodeSpec.Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    FullDecodeSpec.reg raw 0 6 ++ FullDecodeSpec.reg raw 6 7 ++ FullDecodeSpec.reg raw 13 7 ++
    FullDecodeSpec.reg raw 20 7 ++ (FullDecodeSpec.reg raw 27 7 ++ 0#24 ++
      FullDecodeSpec.reg raw 34 2 ++ 0#8 ++ 0#64 ++ 2#82)

private theorem paddedF2Tail (fr4 status : BitVec 8) :
    fr4 ++ 0#8 ++ 0#8 ++ 0#8 ++ status ++ 0#8 ++ 0#64 ++ 0#32 ++ 0#32 ++
      0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#4 ++ 0#4 ++ 1#1 ++ 0#1 =
    fr4 ++ 0#24 ++ status ++ 0#8 ++ 0#64 ++ 2#82 := by bv_decide

private theorem f2Record (selector : BitVec 2) (alias : Bool) (unit : FullDecodeSpec.UnitCode)
    (raw : FullDecodeSpec.Slot) (ip : Word) (index : BitVec 2) :
    FullDecodeSpec.aRecord (.f2 selector alias) unit raw ip (index.zeroExtend 8) =
      paddedF2 unit raw ip index := by
  have tail : FullDecodeSpec.aRecordTail (.f2 selector alias) raw =
      FullDecodeSpec.reg raw 27 7 ++ 0#24 ++ FullDecodeSpec.reg raw 34 2 ++ 0#8 ++ 0#64 ++ 2#82 := by
    simp only [FullDecodeSpec.aRecordTail, FullDecodeSpec.aPred, FullDecodeSpec.aBranchRegister,
      FullDecodeSpec.aStatusField, FullDecodeSpec.aFpPrecision, FullDecodeSpec.aImm,
      FullDecodeSpec.aPredUpdate, FullDecodeSpec.updateByRegister, FullDecodeSpec.updateByImmediate,
      FullDecodeSpec.memAcquire, FullDecodeSpec.memRelease, FullDecodeSpec.aUnc,
      FullDecodeSpec.aClearP2, FullDecodeSpec.aCheckFp, FullDecodeSpec.fpLoadAttributes, if_true]
    exact paddedF2Tail _ _
  unfold FullDecodeSpec.aRecord paddedF2
  rw [tail] <;> rfl

private theorem f2PackedFields (unit : FullDecodeSpec.UnitCode) (raw : FullDecodeSpec.Slot)
    (ip : Word) (index : BitVec 2) :
    cF2Fields (paddedF2 unit raw ip index) = f2Fields raw ip index := by
  let wire := paddedF2 unit raw ip index
  have dst : wire.extractLsb' 210 7 = raw.extractLsb' 6 7 := by
    simp only [wire, paddedF2, FullDecodeSpec.reg]
    bv_decide
  have addend : wire.extractLsb' 202 7 = raw.extractLsb' 13 7 := by
    simp only [wire, paddedF2, FullDecodeSpec.reg]
    bv_decide
  have left : wire.extractLsb' 194 7 = raw.extractLsb' 20 7 := by
    simp only [wire, paddedF2, FullDecodeSpec.reg]
    bv_decide
  have right : wire.extractLsb' 186 7 = raw.extractLsb' 27 7 := by
    simp only [wire, paddedF2, FullDecodeSpec.reg]
    bv_decide
  have qp : wire.extractLsb' 218 6 = raw.extractLsb' 0 6 := by
    simp only [wire, paddedF2, FullDecodeSpec.reg]
    bv_decide
  have bits : wire.extractLsb' 298 41 = raw := by
    simp only [wire, paddedF2]
    bv_decide
  have address : wire.extractLsb' 234 64 = ip := by
    simp only [wire, paddedF2]
    bv_decide
  have slot : wire.extractLsb' 226 2 = index := by
    simp only [wire, paddedF2]
    bv_decide
  change cF2Fields wire = _
  simp only [cF2Fields, f2Fields, dst, addend, left, right, qp, bits, address, slot]

private theorem f2DecodedFields (form : XmaForm) (low high ip : Word) (index : BitVec 2)
    (encoding : f2Encoding form low high index) :
    cF2Fields (f2Wire form low high ip index) =
      f2Fields (FullDecodeSpec.slotBits low high index) ip index := by
  rw [(f2WireContract form low high ip index).1]
  obtain ⟨valid, bounded, unit, fields⟩ := encoding
  simp only [FullDecodeSpec.aBundle, FullDecodeSpec.aOpcode,
    if_pos (And.intro valid bounded), if_pos (And.intro unit fields)]
  cases form <;> simp only [f2Form, f2Record, f2PackedFields]

def cDecodeF2 (low high ip : Word) (index : BitVec 2) : Option (XmaForm × XmaInstruction) :=
  let select form := f2Wire form low high ip index
  if (select .low).getLsbD 394 then some (.low, cF2Fields (select .low))
  else if (select .signedHigh).getLsbD 394 then some (.signedHigh, cF2Fields (select .signedHigh))
  else if (select .unsignedHigh).getLsbD 394 then some (.unsignedHigh, cF2Fields (select .unsignedHigh))
  else if (select .unsignedMultiply).getLsbD 394 then some (.unsignedMultiply, cF2Fields (select .unsignedMultiply))
  else none

private theorem f2Unique (a b : XmaForm) (low high : Word) (index : BitVec 2)
    (ha : f2Encoding a low high index) (hb : f2Encoding b low high index) : a = b := by
  obtain ⟨_, _, _, first⟩ := ha
  obtain ⟨_, _, _, second⟩ := hb
  cases a <;> cases b <;> try rfl
  all_goals
    simp only [f2Form, FullDecodeSpec.aEncoding] at first second
    bv_decide

theorem f2BundleDecoded (form : XmaForm) (low high ip : Word) (index : BitVec 2)
    (encoding : f2Encoding form low high index) :
    cDecodeF2 low high ip index =
      some (form, f2Fields (FullDecodeSpec.slotBits low high index) ip index) := by
  have same (tag : XmaForm) : (f2Wire tag low high ip index).getLsbD 394 = true ↔ tag = form := by
    rw [(f2WireContract tag low high ip index).2]
    constructor
    · exact fun enc => f2Unique tag form low high index enc encoding
    · rintro rfl
      exact encoding
  unfold cDecodeF2
  dsimp only
  simp only [same]
  cases form <;> simp [f2DecodedFields _ low high ip index encoding]

structure XmaContext where
  predicates : Predicates
  knownZero : Word
  knownOne : Word
  enabledSets : BitVec 32
  nativeCarry : Bool
  writtenSets : BitVec 32
  knownLive : Bool

structure XmaEntryFacts (c : XmaContext) (s : FpWriteState) : Prop where
  zero : ∀ q : BitVec 6, c.knownZero.getLsbD q.toNat = true → readPredicate c.predicates q = false
  one : ∀ q : BitVec 6, c.knownOne.getLsbD q.toNat = true → readPredicate c.predicates q = true
  lowEnabled : c.enabledSets.getLsbD 0 = true → s.psr.getLsbD 18 = false
  highEnabled : c.enabledSets.getLsbD 1 = true → s.psr.getLsbD 19 = false

structure XmaFaultRequest where
  code : Word
  address : Word
  immediate : Word
  slot : BitVec 2
  isr : Option Word
  deriving DecidableEq

inductive XmaAdmission where
  | nullified | fault (request : XmaFaultRequest) | dispatch
  deriving DecidableEq

def cXmaQualifier (c : XmaContext) (qp : BitVec 6) : Bool :=
  if c.knownZero.getLsbD qp.toNat then false
  else if c.knownOne.getLsbD qp.toNat then true
  else if qp = 0 then true else c.predicates qp

def cXmaBanks (ops : XmaOperands) : BitVec 32 :=
  cClassSet ops.addend ||| cClassSet ops.left ||| cClassSet ops.right ||| cClassSet ops.destination

def cPrepareF2 (c : XmaContext) (psr : Word) (instruction : XmaInstruction) : XmaAdmission :=
  if cXmaQualifier c instruction.qualifier then
    if cXmaFixedDestination instruction.operands.destination then
      .fault ⟨cXmaIllegalCode, instruction.address, instruction.raw.zeroExtend 64, instruction.slot, none⟩
    else
      let disabled := cFpDisabledBits (cXmaBanks instruction.operands) psr c.enabledSets
      if disabled = 0 then .dispatch
      else .fault ⟨cXmaDisabledCode, instruction.address, 0, instruction.slot, some (disabled >>> (18 : Nat))⟩
  else .nullified

def f2LowBank (ops : XmaOperands) : Bool :=
  let inLow (r : BitVec 7) := decide (2 ≤ r ∧ r < 32)
  inLow ops.destination || inLow ops.addend || inLow ops.left || inLow ops.right

def f2HighBank (ops : XmaOperands) : Bool :=
  let inHigh (r : BitVec 7) := decide (32 ≤ r)
  inHigh ops.destination || inHigh ops.addend || inHigh ops.left || inHigh ops.right

def f2DisabledCode (ops : XmaOperands) (psr : Word) : Word :=
  (if psr.getLsbD 18 && f2LowBank ops then 1 else 0) |||
    (if psr.getLsbD 19 && f2HighBank ops then 2 else 0)

def prepareF2 (predicates : Predicates) (psr : Word) (instruction : XmaInstruction) : XmaAdmission :=
  if !readPredicate predicates instruction.qualifier then .nullified
  else if instruction.operands.destination ≤ 1 then
    .fault ⟨2, instruction.address, instruction.raw.zeroExtend 64, instruction.slot, none⟩
  else
    let isr := f2DisabledCode instruction.operands psr
    if isr = 0 then .dispatch
    else .fault ⟨31, instruction.address, 0, instruction.slot, some isr⟩

private theorem f2Qualified (c : XmaContext) (s : FpWriteState) (qp : BitVec 6)
    (sound : XmaEntryFacts c s) : cXmaQualifier c qp = readPredicate c.predicates qp := by
  unfold cXmaQualifier
  split
  · exact (sound.zero qp (by assumption)).symm
  · split
    · exact (sound.one qp (by assumption)).symm
    · by_cases fixed : qp = 0#6 <;> simp [readPredicate, fixed]

private theorem f2Disabled (ops : XmaOperands) (psr : Word) (enabled : BitVec 32)
    (low : enabled.getLsbD 0 = true → psr.getLsbD 18 = false)
    (high : enabled.getLsbD 1 = true → psr.getLsbD 19 = false) :
    let actual := cFpDisabledBits (cXmaBanks ops) psr enabled
    (actual = 0 ↔ f2DisabledCode ops psr = 0) ∧
      actual >>> (18 : Nat) = f2DisabledCode ops psr := by
  have shift (x : Word) : x >>> (18 : Nat) = 0#18 ++ x.extractLsb' 18 46 := by
    exact BitVec.ushiftRight_eq_extractLsb'_of_lt (by decide : 18 < 64)
  dsimp only
  rw [shift]
  dsimp only [cFpDisabledBits, cXmaBanks, cClassSet, f2DisabledCode, f2LowBank, f2HighBank]
  bv_decide

theorem f2Admission (c : XmaContext) (s : FpWriteState) (instruction : XmaInstruction)
    (sound : XmaEntryFacts c s) :
    cPrepareF2 c s.psr instruction = prepareF2 c.predicates s.psr instruction := by
  have disabled := f2Disabled instruction.operands s.psr c.enabledSets sound.lowEnabled sound.highEnabled
  unfold cPrepareF2 prepareF2
  rw [f2Qualified c s instruction.qualifier sound]
  cases q : readPredicate c.predicates instruction.qualifier
  · rfl
  · simp only [Bool.not_true, Bool.false_eq_true, if_false, q, if_true, cXmaFixedDestination,
      decide_eq_true_eq, cXmaIllegalCode, cXmaDisabledCode]
    by_cases dest : instruction.operands.destination ≤ 1
    · simp only [dest, if_true]
    · simp only [dest, if_false, disabled.1, disabled.2]

inductive XmaResult where
  | nullified (state : FpWriteState)
  | fault (request : XmaFaultRequest) (state : FpWriteState)
  | completed (state : FpWriteState)

def cF2Bundle (low high ip : Word) (index : BitVec 2) (c : XmaContext)
    (cache : XmaCache) (s : FpWriteState) : Option XmaResult :=
  (cDecodeF2 low high ip index).map fun (form, instruction) =>
    match cPrepareF2 c s.psr instruction with
    | .nullified => .nullified s
    | .fault request => .fault request s
    | .dispatch => .completed
        (cXmaWrite form cache instruction.operands s c.nativeCarry c.writtenSets c.knownLive)

def f2Writeback (form : XmaForm) (ops : XmaOperands) (s : FpWriteState) : FpWriteState :=
  ⟨fun index => if index = ops.destination then
      xmaRegister form (s.fr ops.left) (s.fr ops.right) (s.fr ops.addend) else s.fr index,
    markFpModified ops.destination s.psr, s.live || decide (32 ≤ ops.destination)⟩

def f2Step (form : XmaForm) (instruction : XmaInstruction) (c : XmaContext)
    (s : FpWriteState) : XmaResult :=
  match prepareF2 c.predicates s.psr instruction with
  | .nullified => .nullified s
  | .fault request => .fault request s
  | .dispatch => .completed (f2Writeback form instruction.operands s)

private theorem f2Writable (predicates : Predicates) (psr : Word) (instruction : XmaInstruction)
    (admitted : prepareF2 predicates psr instruction = .dispatch) : 1 < instruction.operands.destination := by
  unfold prepareF2 at admitted
  split at admitted
  · contradiction
  · split at admitted
    · contradiction
    · exact BitVec.not_le.mp (by assumption)

structure XmaWriteFacts (c : XmaContext) (cache : XmaCache) (s : FpWriteState)
    (destination : BitVec 7) : Prop where
  cacheValid : XmaCacheSound cache
  currentView : ∀ register, sourceValue (cXmaRead cache register) = s.fr register
  written : (c.writtenSets &&& fpModifiedSet destination) ≠ 0 →
    s.psr &&& fpModifiedBit destination = fpModifiedBit destination
  live : c.knownLive = true → s.live = true

private theorem fpWriteEq (a b : FpWriteState)
    (fr : a.fr = b.fr) (psr : a.psr = b.psr) (live : a.live = b.live) : a = b := by
  cases a <;> cases b <;> simp_all

-- The selected native slot has been reached, with its FP source view materialized.
theorem f2BundleRefines (form : XmaForm) (low high ip : Word) (index : BitVec 2)
    (c : XmaContext) (cache : XmaCache) (s : FpWriteState)
    (encoding : f2Encoding form low high index) (entry : XmaEntryFacts c s) :
    let instruction := f2Fields (FullDecodeSpec.slotBits low high index) ip index
    (prepareF2 c.predicates s.psr instruction = .dispatch →
      XmaWriteFacts c cache s instruction.operands.destination) →
    cF2Bundle low high ip index c cache s =
      some (f2Step form instruction c s) := by
  intro instruction writeFacts
  have record := f2BundleDecoded form low high ip index encoding
  unfold cF2Bundle f2Step
  rw [record]
  simp only [Option.map_some, f2Admission c s _ entry]
  change some (match prepareF2 c.predicates s.psr instruction with
    | .nullified => XmaResult.nullified s
    | .fault fault => XmaResult.fault fault s
    | .dispatch => XmaResult.completed (cXmaWrite form cache instruction.operands s
        c.nativeCarry c.writtenSets c.knownLive)) = _
  cases admission : prepareF2 c.predicates s.psr instruction
  · rfl
  · rfl
  · have writable := f2Writable c.predicates s.psr instruction admission
    have facts := writeFacts admission
    obtain ⟨fr, psr, live⟩ := xmaWrite form cache instruction.operands s c.nativeCarry
      c.writtenSets c.knownLive facts.cacheValid writable facts.currentView
      facts.written facts.live
    let written := cXmaWrite form cache instruction.operands s c.nativeCarry c.writtenSets c.knownLive
    have equal : written = f2Writeback form instruction.operands s :=
      fpWriteEq _ _ (funext fr) psr live
    change some (XmaResult.completed written) = some (.completed (f2Writeback form instruction.operands s))
    rw [equal]

private def f2ExampleContext : XmaContext :=
  ⟨fun _ => false, 0, 1, 0, false, 0, false⟩

private def f2ExampleCache : XmaCache :=
  ⟨fun _ => .nat, 0, 0, 0⟩

private def f2ExampleState : FpWriteState :=
  ⟨fun r => sourceValue (cXmaRead f2ExampleCache r), 0, false⟩

example :
    (match cF2Bundle 0x2000000000000c 0x760204 0x1000 1
        f2ExampleContext f2ExampleCache f2ExampleState with
     | some (.completed s) => some (s.fr 2, s.psr)
     | _ => none) = some (integerRegister (1#64 <<< (62 : Nat)), 16) := by decide

private def f2ExampleFault (low high : Word) : Option XmaFaultRequest :=
  match cF2Bundle low high 0x1000 1 f2ExampleContext f2ExampleCache
      { f2ExampleState with psr := (1 <<< 18) ||| (1 <<< 19) } with
  | some (.fault request _) => some request
  | _ => none

example :
    f2ExampleFault 0x201000000000000c 0x770280 = some ⟨2, 0x1000, 0x1dc0a008040, 1, none⟩ ∧
    f2ExampleFault 0x202000000000000c 0x770280 = some ⟨31, 0x1000, 0, 1, some 3⟩ ∧
    (match cF2Bundle 0x201140000000000c 0x770280 0x1000 1
        f2ExampleContext f2ExampleCache f2ExampleState with
     | some (.nullified _) => true
     | _ => false) = true := by decide
