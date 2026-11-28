/- SPDX-License-Identifier: GPL-2.0-or-later -/

structure SlotInput where
  low : TcgMemory.Word
  high : TcgMemory.Word
  address : TcgMemory.Word
  index : BitVec 2
  cell : CellInput

def raw (slot : SlotInput) : Slot := slotBits slot.low slot.high slot.index

-- Ordinary M1/M2/M3 loads and M4/M5 stores (Vol. 3 Figure 4-13
-- and §4.4.1, with the format names corrected by Intel 248699-014).
-- Locality hints do not affect these accesses. The .bias form is excluded.
def inScope (bits : Slot) : Prop :=
  (bits.extractLsb' 37 4 = 4 ∧ bits.extractLsb' 27 1 = 0 ∨ bits.extractLsb' 37 4 = 5) ∧
    ¬ (16 ≤ bits.extractLsb' 30 6 ∧ bits.extractLsb' 30 6 ≤ 19)

instance (bits : Slot) : Decidable (inScope bits) := by unfold inScope; infer_instance

def width : Tag → Width
  | .ld1 | .st1 | .st1rel => .w1
  | .ld2 | .st2 | .st2rel => .w2
  | .ld4 | .st4 | .st4rel => .w4
  | .ld8 | .st8 | .st8rel => .w8

def isStore : Tag → Bool
  | .ld1 | .ld2 | .ld4 | .ld8 => false
  | _ => true

def sizeCode : Width → BitVec 6
  | .w1 => 0
  | .w2 => 1
  | .w4 => 2
  | .w8 => 3

def isRelease : Tag → Bool
  | .st1rel | .st2rel | .st4rel | .st8rel => true
  | _ => false

def matchesMode (tag : Tag) (bits : Slot) : Prop :=
  let x6 := bits.extractLsb' 30 6
  let n := sizeCode (width tag)
  if isStore tag then
    (bits.extractLsb' 37 4 = 5 ∨ bits.extractLsb' 36 1 = 0) ∧
      x6 = n + (if isRelease tag then 52 else 48)
  else x6 = n ∨ x6 = n + 20

instance (tag : Tag) (bits : Slot) : Decidable (matchesMode tag bits) := by
  unfold matchesMode
  split <;> infer_instance

def isAcquire (tag : Tag) (bits : Slot) : Bool :=
  !isStore tag && bits.extractLsb' 30 6 == sizeCode (width tag) + 20

def validMemorySlot (s : SlotInput) : Prop :=
  let template := templateBits s.low
  validTemplate template = true ∧ s.index < 3 ∧
    templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit template s.index = mUnit

instance (s : SlotInput) : Decidable (validMemorySlot s) := by unfold validMemorySlot; infer_instance

def regValue (r : BitVec 7) : Reg := ⟨r.toNat, r.isLt⟩
def predValue (p : BitVec 6) : Pred := ⟨p.toNat, p.isLt⟩

def immediateUpdate (bits : Slot) : Bool := bits.extractLsb' 37 4 == 5

def registerUpdate (tag : Tag) (bits : Slot) : Bool :=
  !isStore tag && bits.extractLsb' 37 4 == 4 && bits.getLsbD 36

def sdIncrement (tag : Tag) (bits : Slot) : Word :=
  let low := if isStore tag then 6 else 13
  (bits.extractLsb' 36 1 ++ bits.extractLsb' 27 1 ++ bits.extractLsb' low 7).signExtend 64

def rawUpdate (tag : Tag) (bits : Slot) : Update :=
  if registerUpdate tag bits then .register (regValue (bits.extractLsb' 13 7))
  else if immediateUpdate bits then .immediate (sdIncrement tag bits)
  else .none

def storeUpdate : Update → Option Word
  | .immediate d => some d
  | _ => none

structure Plan where
  scalar : Scalar
  layout : Nat × Bool
  explicitBefore : BitVec 8
  explicitAfter : BitVec 8
  defaultBefore : BitVec 8

private structure DecodedFields where
  qp : BitVec 6
  r1 : BitVec 7
  r2 : BitVec 7
  r3 : BitVec 7
  acquire : Bool
  release : Bool

private def recordFields (record : Observation) : DecodedFields :=
  ⟨record.extractLsb' 218 6, record.extractLsb' 210 7, record.extractLsb' 202 7,
    record.extractLsb' 194 7, record.getLsbD 14, record.getLsbD 13⟩

private def lowerFields (tag : Tag) (cell : CellInput) (fields : DecodedFields)
    (update : Update) : Plan :=
  let qp := predValue fields.qp
  let base := regValue fields.r3
  let w := width tag
  if isStore tag then
    let release := fields.release
    ⟨.store ⟨qp, regValue fields.r2, base, w, release, storeUpdate update⟩,
      tcgLayout (storeDescriptor w release) cell.hostBig cell.guestBig,
      if release then releaseMask else 0, 0, DeferredAcquire.sourceMasks.beforeStore⟩
  else
    let acquire := fields.acquire
    ⟨.load ⟨qp, regValue fields.r1, base, w, acquire, update⟩,
      tcgLayout (loadDescriptor w) cell.hostBig cell.guestBig,
      0, if acquire then DeferredAcquire.sourceMasks.acquire else 0,
      DeferredAcquire.sourceMasks.beforeLoad⟩

def recordUpdate (wire : Observation) : Update :=
  if wire.getLsbD 16 then .register (regValue (wire.extractLsb' 202 7))
  else if wire.getLsbD 15 then .immediate (wire.extractLsb' 82 64)
  else .none

def sourceRecordPlan (tag : Tag) (cell : CellInput) (record : Observation) : Plan :=
  lowerFields tag cell (recordFields record) (recordUpdate record)

def sourceCandidate (s : SlotInput) (tag : Tag) : Option Plan :=
  if inScope (raw s) then
    let record := decode tag s.low s.high s.address s.index
    if record.getLsbD 394 then
      some (sourceRecordPlan tag s.cell record)
    else none
  else none

def architecturalPlan (s : SlotInput) (tag : Tag) : Plan :=
  let bits := raw s
  let qp := predValue (bits.extractLsb' 0 6)
  let w := width tag
  let layout := (w.bytes, if w = .w1 then s.cell.hostBig else s.cell.guestBig)
  -- The endian flag for a single byte is immaterial. The expected
  -- pair masks specify the TCG contract used by the scheduling model.
  if isStore tag then
    let release := isRelease tag
    ⟨.store ⟨qp, regValue (bits.extractLsb' 13 7), regValue (bits.extractLsb' 20 7),
        w, release, if immediateUpdate bits then some (sdIncrement tag bits) else none⟩,
      layout, if release then 0x2c else 0, 0, 0x0c⟩
  else
    let acquire := isAcquire tag bits
    ⟨.load ⟨qp, regValue (bits.extractLsb' 6 7), regValue (bits.extractLsb' 20 7),
        w, acquire, rawUpdate tag bits⟩,
      layout, 0, if acquire then 0x15 else 0, 0x01⟩

def architecturalCandidate (s : SlotInput) (tag : Tag) : Option Plan :=
  if inScope (raw s) ∧ validMemorySlot s ∧ matchesMode tag (raw s) then
    some (architecturalPlan s tag)
  else none

def architecturalMasks : DeferredAcquire.Masks := ⟨0x15, 0x01, 0x0c⟩

theorem schedulingMasks : DeferredAcquire.sourceMasks = architecturalMasks := rfl

private theorem packedFields (head : BitVec 169) (qp r1 r2 r3 : BitVec 8) (tail : BitVec 194) :
    recordFields (head ++ qp ++ r1 ++ r2 ++ r3 ++ tail) =
      ⟨qp.extractLsb' 0 6, r1.extractLsb' 0 7, r2.extractLsb' 0 7, r3.extractLsb' 0 7,
        tail.getLsbD 14, tail.getLsbD 13⟩ := by
  have q : (head ++ qp ++ r1 ++ r2 ++ r3 ++ tail).extractLsb' 218 6 =
      qp.extractLsb' 0 6 := by bv_decide
  have d : (head ++ qp ++ r1 ++ r2 ++ r3 ++ tail).extractLsb' 210 7 =
      r1.extractLsb' 0 7 := by bv_decide
  have s : (head ++ qp ++ r1 ++ r2 ++ r3 ++ tail).extractLsb' 202 7 =
      r2.extractLsb' 0 7 := by bv_decide
  have b : (head ++ qp ++ r1 ++ r2 ++ r3 ++ tail).extractLsb' 194 7 =
      r3.extractLsb' 0 7 := by bv_decide
  simp only [recordFields, q, d, s, b, BitVec.getLsbD_append]
  rfl

private theorem recordProjection (form : Form) (bits : Slot) (address : Word) (index : BitVec 8) :
    recordFields (aRecord form mUnit bits address index) =
      ⟨(aQualifier form bits).extractLsb' 0 6, (aR1 form bits).extractLsb' 0 7,
        (aR2 form bits).extractLsb' 0 7, (aR3 form bits).extractLsb' 0 7,
        (aRecordTail form bits).getLsbD 14, (aRecordTail form bits).getLsbD 13⟩ :=
  packedFields (1#1 ++ mUnit.zeroExtend 32 ++ bits.zeroExtend 64 ++ address ++ index)
    (aQualifier form bits) (aR1 form bits) (aR2 form bits) (aR3 form bits) (aRecordTail form bits)

private theorem recordLowBit (form : Form) (bits : Slot) (address : Word) (index : BitVec 8)
    (bit : Nat) (low : bit < 194) :
    (aRecord form mUnit bits address index).getLsbD bit =
      (aRecordTail form bits).getLsbD bit := by
  simp only [aRecord, BitVec.getLsbD_append, low, if_true]

private theorem recordImmediate (form : Form) (bits : Slot) (address : Word) (index : BitVec 8) :
    (aRecord form mUnit bits address index).extractLsb' 82 64 =
      (aRecordTail form bits).extractLsb' 82 64 := by
  dsimp only [aRecord]
  generalize aRecordTail form bits = tail
  bv_decide

private theorem qpField (p : BitVec 6) : (p.zeroExtend 8).extractLsb' 0 6 = p := by bv_decide
private theorem grField (r : BitVec 7) : (r.zeroExtend 8).extractLsb' 0 7 = r := by bv_decide

private theorem tailFlags (tag : Tag) (bits : Slot) :
    let tail := aRecordTail (cForm tag) bits
    tail.getLsbD 16 = registerUpdate tag bits ∧ tail.getLsbD 15 = immediateUpdate bits ∧
      tail.getLsbD 14 = isAcquire tag bits ∧ tail.getLsbD 13 = isRelease tag := by
  cases tag <;> simp [aRecordTail, cForm, updateByRegister, updateByImmediate, registerUpdate,
    immediateUpdate,
    memAcquire, memRelease, hasExtension, isAcquire, isStore, isRelease, sizeCode, width,
    BitVec.getLsbD_append] <;> bv_decide

private theorem slotEncoding (s : SlotInput) (tag : Tag) (scope : inScope (raw s)) :
    aBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (cForm tag)
        s.low s.high s.index ↔ validMemorySlot s ∧ matchesMode tag (raw s) := by
  have shape := scope.1
  have notBias := scope.2
  have modes : aEncoding (cForm tag) (raw s) ↔ matchesMode tag (raw s) := by
    cases tag <;> simp [aEncoding, cForm, hasExtension,
      matchesMode, isStore, isRelease, sizeCode, width] <;> bv_decide
  have unit (u : UnitCode) : formUnit mUnit iUnit bUnit fUnit (cForm tag) u (raw s) ↔
      u = mUnit := by cases tag <;> rfl
  change (validTemplate (templateBits s.low) = true ∧ s.index < 3 ∧
    formUnit mUnit iUnit bUnit fUnit (cForm tag)
      (templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (templateBits s.low) s.index)
      (raw s) ∧ aEncoding (cForm tag) (raw s)) ↔ _
  rw [modes, unit]
  unfold validMemorySlot
  exact ⟨fun ⟨t, i, u, m⟩ => ⟨⟨t, i, u⟩, m⟩,
    fun ⟨⟨t, i, u⟩, m⟩ => ⟨t, i, u, m⟩⟩

private def operandR1 (store : Bool) (bits : Slot) : BitVec 7 :=
  if store then (if bits.extractLsb' 37 4 = 4 then bits.extractLsb' 6 7 else 0)
  else bits.extractLsb' 6 7

private def operandR2 (store : Bool) (bits : Slot) : BitVec 7 :=
  if store then bits.extractLsb' 13 7
  else if bits.extractLsb' 37 4 = 4 then bits.extractLsb' 13 7 else 0

private def rawFields (s : SlotInput) (tag : Tag) : DecodedFields :=
  let bits := raw s
  ⟨bits.extractLsb' 0 6, operandR1 (isStore tag) bits, operandR2 (isStore tag) bits,
    bits.extractLsb' 20 7, isAcquire tag bits, isRelease tag⟩

private def OrdinaryFields (form : Form) (store : Bool) (bits : Slot) : Prop :=
    (aQualifier form bits).extractLsb' 0 6 = bits.extractLsb' 0 6 ∧
    (aR1 form bits).extractLsb' 0 7 = operandR1 store bits ∧
    (aR2 form bits).extractLsb' 0 7 = operandR2 store bits ∧
    (aR3 form bits).extractLsb' 0 7 = bits.extractLsb' 20 7

private theorem loadFields (bits : Slot) (primary : BitVec 6) (bias acquire : Option (BitVec 6)) :
    OrdinaryFields (.load primary bias acquire) false bits := by
  refine ⟨qpField _, grField _, ?_, grField _⟩
  change (if bits.extractLsb' 37 4 = 4 then reg bits 13 7 else 0).extractLsb' 0 7 =
    if bits.extractLsb' 37 4 = 4 then bits.extractLsb' 13 7 else 0
  by_cases major : bits.extractLsb' 37 4 = 4
  · rw [if_pos major, if_pos major]
    exact grField _
  · rw [if_neg major, if_neg major]
    rfl

private theorem storeFields (bits : Slot) (mode : BitVec 6) (release : Bool) :
    OrdinaryFields (.store mode release) true bits := by
  refine ⟨qpField _, ?_, grField _, grField _⟩
  change (if bits.extractLsb' 37 4 = 4 then reg bits 6 7 else 0).extractLsb' 0 7 =
    if bits.extractLsb' 37 4 = 4 then bits.extractLsb' 6 7 else 0
  by_cases major : bits.extractLsb' 37 4 = 4
  · rw [if_pos major, if_pos major]
    exact grField _
  · rw [if_neg major, if_neg major]
    rfl

private theorem ordinaryFields (bits : Slot) (tag : Tag) :
    OrdinaryFields (cForm tag) (isStore tag) bits := by
  cases tag
  case ld1 | ld2 | ld4 | ld8 => exact loadFields bits _ _ _
  case st1 | st1rel | st2 | st2rel | st4 | st4rel | st8 | st8rel =>
    exact storeFields bits _ _

private theorem operandFields (s : SlotInput) (tag : Tag) :
    recordFields (aRecord (cForm tag) mUnit (raw s) s.address (s.index.zeroExtend 8)) =
      rawFields s tag := by
  obtain ⟨qp, r1, r2, r3⟩ := ordinaryFields (raw s) tag
  obtain ⟨_, _, acq, rel⟩ := tailFlags tag (raw s)
  rw [recordProjection, qp, r1, r2, r3, acq, rel]
  rfl

private theorem actualLayout (s : SlotInput) (tag : Tag) :
    lowerFields tag s.cell (rawFields s tag) (rawUpdate tag (raw s)) = architecturalPlan s tag := by
  unfold lowerFields
  rw [schedulingMasks]
  cases tag <;> simp only [architecturalPlan, rawFields, isStore, isRelease, operandR1, operandR2,
    registerUpdate, rawUpdate, Bool.not_true, Bool.false_and, Bool.not_false, Bool.true_and,
    loadLayout, storeLayout, architecturalMasks, width, acquireMask, releaseMask,
    Bool.false_eq_true, if_true, if_false]
  all_goals split <;> rfl

private theorem loadTailImmediate (bits : Slot) (mode : BitVec 6) (bias aq : Option (BitVec 6)) :
    (aRecordTail (.load mode bias aq) bits).extractLsb' 82 64 =
      aImm (.load mode bias aq) bits := by
  dsimp only [aRecordTail]
  generalize aImm (.load mode bias aq) bits = value
  bv_decide

private theorem storeTailImmediate (bits : Slot) (mode : BitVec 6) (rl : Bool) :
    (aRecordTail (.store mode rl) bits).extractLsb' 82 64 = aImm (.store mode rl) bits := by
  dsimp only [aRecordTail]
  generalize aImm (.store mode rl) bits = value
  bv_decide

private theorem tailImmediate (tag : Tag) (bits : Slot) (selected : immediateUpdate bits = true) :
    (aRecordTail (cForm tag) bits).extractLsb' 82 64 = sdIncrement tag bits := by
  have major : bits.extractLsb' 37 4 = 5 := by simpa [immediateUpdate] using selected
  cases tag <;> simp only [cForm, loadTailImmediate, storeTailImmediate, aImm, major, if_true,
    imm9, sdIncrement, isStore, Bool.false_eq_true, if_false]

private theorem recordUpdateMatches (s : SlotInput) (tag : Tag) :
    recordUpdate (aRecord (cForm tag) mUnit (raw s) s.address (s.index.zeroExtend 8)) =
      rawUpdate tag (raw s) := by
  obtain ⟨uReg, uImm, _, _⟩ := tailFlags tag (raw s)
  unfold recordUpdate rawUpdate
  rw [recordLowBit _ _ _ _ 16 (by decide), recordLowBit _ _ _ _ 15 (by decide), uReg, uImm]
  split
  · rename_i active
    have major : (raw s).extractLsb' 37 4 = 4 := by
      unfold registerUpdate at active
      simp only [Bool.and_eq_true, beq_iff_eq] at active
      exact active.1.2
    have r2 := congrArg DecodedFields.r2 (operandFields s tag)
    change (aRecord (cForm tag) mUnit (raw s) s.address (s.index.zeroExtend 8)).extractLsb' 202 7 =
      operandR2 (isStore tag) (raw s) at r2
    rw [r2]
    simp [operandR2, major]
  · split
    · rename_i active
      rw [recordImmediate, tailImmediate tag (raw s) active]
    · rfl

private theorem recordPlan (s : SlotInput) (tag : Tag) :
    sourceRecordPlan tag s.cell (aRecord (cForm tag) mUnit (raw s) s.address (s.index.zeroExtend 8)) =
      architecturalPlan s tag := by
  unfold sourceRecordPlan
  rw [operandFields, recordUpdateMatches]
  exact actualLayout s tag

theorem candidatesMatch (s : SlotInput) (tag : Tag) :
    sourceCandidate s tag = architecturalCandidate s tag := by
  unfold sourceCandidate architecturalCandidate
  by_cases scope : inScope (raw s)
  · have ⟨record, present⟩ := decodedRecord tag s.low s.high s.address s.index
    rw [if_pos scope]
    by_cases fits : validMemorySlot s ∧ matchesMode tag (raw s)
    · have enabled := (slotEncoding s tag scope).mpr fits
      have picked := present.mpr enabled
      obtain ⟨template, index, unit⟩ := fits.1
      have lifted := (slotEncoding s tag scope).mpr fits
      obtain ⟨_, _, form, encoding⟩ := lifted
      have accepted : inScope (raw s) ∧ validMemorySlot s ∧ matchesMode tag (raw s) :=
        ⟨scope, fits⟩
      rw [if_pos accepted]
      have selected : decode tag s.low s.high s.address s.index =
          aRecord (cForm tag) mUnit (raw s) s.address (s.index.zeroExtend 8) := by
        rw [record]
        simp only [aBundle, template, index, and_self, if_true, aOpcode, form, encoding, unit]
        cases tag <;> rfl
      rw [selected] at picked ⊢
      simp only [picked, recordPlan, if_true]
    · have absent := Bool.eq_false_iff.mpr (fun picked =>
        fits ((slotEncoding s tag scope).mp (present.mp picked)))
      have rejected : ¬ (inScope (raw s) ∧ validMemorySlot s ∧ matchesMode tag (raw s)) :=
        fun accepted => fits accepted.2
      rw [if_neg rejected]
      simp only [absent, Bool.false_eq_true, if_false]
  · simp [scope]

def cannotFuseFill (unitAt : BitVec 2 → UnitCode) (plan : Plan) : Bool :=
  match plan.scalar with
  | .load _ => true
  | .store st =>
    let fill := st.width == .w1 && st.qp == 0 && st.source != st.base && st.base != 0 &&
      st.update == some 1
    !fill || [0#2, 1#2, 2#2].all (fun index => unitAt index != bUnit)

def architecturalUnit (s : SlotInput) : BitVec 2 → UnitCode :=
  templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (templateBits s.low)

def sourcePlan (s : SlotInput) : Option Plan :=
  (tags.findSome? (sourceCandidate s)).filter
    (cannotFuseFill (cUnit ((cTemplate s.low).extractLsb' 0 5)))

def guestPlan (s : SlotInput) : Option Plan :=
  (tags.findSome? (architecturalCandidate s)).filter (cannotFuseFill (architecturalUnit s))

theorem rawPlanMatches (s : SlotInput) : sourcePlan s = guestPlan s := by
  have eq : sourceCandidate s = architecturalCandidate s := funext (candidatesMatch s)
  have narrow (code : BitVec 5) : (code.zeroExtend 8).extractLsb' 0 5 = code := by bv_decide
  have units : cUnit ((cTemplate s.low).extractLsb' 0 5) = architecturalUnit s := by
    funext index
    simp only [bundleTemplate_matches, narrow, slotUnit_matches, architecturalUnit]
  unfold sourcePlan guestPlan
  rw [eq, units]

-- A candidate byte-fill store is admitted only in a bundle without any
-- B slot. The fused translator route needs a same-bundle BR.CLOOP.
theorem ordinaryFillPath (s : SlotInput) (p : Plan) (selector : sourcePlan s = some p) :
    cannotFuseFill (architecturalUnit s) p = true := by
  rw [rawPlanMatches] at selector
  exact (Option.filter_eq_some.mp selector).2

def memopWidth : Nat → Option Width
  | 1 => some .w1
  | 2 => some .w2
  | 4 => some .w4
  | 8 => some .w8
  | _ => none

def command (plan : Plan) (seed : CellInput) : Option Program.Command := do
  let width ← memopWidth plan.layout.1
  let cell := {seed with guestBig := if width = .w1 then seed.guestBig else plan.layout.2}
  let op : Scalar := match plan.scalar with
    | .load ld => .load {ld with width, acquire := plan.explicitAfter &&& 0x05 != 0}
    | .store st => .store {st with width, release := plan.explicitBefore &&& 0x0c != 0}
  some (.memory (.scalar ⟨op, cell⟩))

def emittedCommand (s : SlotInput) : Option Program.Command :=
  (sourcePlan s).bind (fun plan => command plan s.cell)

def guestCommand (s : SlotInput) : Option Program.Command :=
  (guestPlan s).map (fun plan => .memory (.scalar ⟨plan.scalar, s.cell⟩))

private theorem commandFromPlan (s : SlotInput) (tag : Tag) :
    command (architecturalPlan s tag) s.cell =
      some (.memory (.scalar ⟨(architecturalPlan s tag).scalar, s.cell⟩)) := by
  have acquire (flag : Bool) : ((if flag then 0x15#8 else 0) &&& 0x05#8 != 0#8) = flag := by
    cases flag <;> rfl
  cases tag <;>
    simp [command, architecturalPlan, acquire, isStore, isRelease, width, Width.bytes, memopWidth]
  all_goals exact acquire _

theorem rawCommandMatches (s : SlotInput) : emittedCommand s = guestCommand s := by
  unfold emittedCommand guestCommand
  rw [rawPlanMatches]
  cases parsed : guestPlan s with
  | none => rfl
  | some plan =>
    have picked : tags.findSome? (architecturalCandidate s) = some plan :=
      (Option.filter_eq_some.mp parsed).1
    obtain ⟨tag, _, recognized⟩ := List.exists_of_findSome?_eq_some picked
    unfold architecturalCandidate at recognized
    split at recognized
    · injection recognized with eq
      subst plan
      exact commandFromPlan s tag
    · contradiction

def baseRegister : Scalar → Reg
  | .load ld => ld.base
  | .store st => st.base

def effectAddress : Effect → Word
  | .read address _ _ | .write address _ _ => address

theorem scalarAddress (r : Request) (regs : Registers) :
    (tcgStep regs r).effect.map effectAddress =
      if (tcgStep regs r).exit = .success then some (getGR regs (baseRegister r.op)) else none := by
  cases r with
  | mk op cell =>
    cases op <;> simp only [tcgStep, loadValueState, storeValueState, specLoad, specStore]
    all_goals repeat first | split <;> simp_all [stop, baseRegister, effectAddress] | rfl

theorem decodedScalarResult (s : SlotInput) (r : Request) (regs : Registers)
    (emitted : emittedCommand s = some (.memory (.scalar r))) :
    guestCommand s = some (.memory (.scalar r)) ∧ tcgStep regs r = specStep regs r ∧
    (tcgStep regs r).effect.map effectAddress =
      (if (tcgStep regs r).exit = .success then some (getGR regs (baseRegister r.op)) else none) ∧
    ((tcgStep regs r).exit ≠ .success →
      (tcgStep regs r).regs = regs ∧ (tcgStep regs r).effect = none) := by
  refine ⟨(rawCommandMatches s).symm.trans emitted, stepMatches regs r, scalarAddress r regs, ?_⟩
  cases r with
  | mk op cell =>
    cases op with
    | load l => exact loadAbort l regs cell
    | store st => exact storeAbort st regs cell

def initialMachine [DecidableEq Cpu] (parse : SlotInput → Option Program.Command)
    (phys : Cpu → TcgMemory.Word → TcgMemory.Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List SlotInput)) : Option (Program.Machine Cpu) := do
  let decoded ← programs.mapM fun (cpu, slots) => (slots.mapM parse).map (Prod.mk cpu)
  let next cpu := (decoded.lookup cpu).getD []
  some (Program.start phys initial regs next)

def replay [DecidableEq Cpu] (mask : DeferredAcquire.Masks) (parse : SlotInput → Option Program.Command)
    (phys : Cpu → TcgMemory.Word → TcgMemory.Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List SlotInput)) (actions : List (DeferredAcquire.Action Cpu)) :
    Option (DeferredAcquire.State Cpu) :=
  (initialMachine parse phys initial regs programs).bind (fun m =>
    DeferredAcquire.run mask (DeferredAcquire.start m) actions)

theorem rawProgramMatches {Cpu : Type} [DecidableEq Cpu]
    (phys : Cpu → TcgMemory.Word → TcgMemory.Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List SlotInput)) (action : List (DeferredAcquire.Action Cpu)) :
    replay DeferredAcquire.sourceMasks emittedCommand phys initial regs programs action =
      replay architecturalMasks guestCommand phys initial regs programs action := by
  rw [schedulingMasks, show emittedCommand = guestCommand from funext rawCommandMatches]

theorem rawProgramRefines {Cpu : Type} [DecidableEq Cpu]
    (phys : Cpu → TcgMemory.Word → TcgMemory.Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List SlotInput)) (steps : List (Program.Schedule Cpu))
    (final : Program.Machine Cpu)
    (source : (initialMachine emittedCommand phys initial regs programs).bind
      (fun m => Program.sourceRun m steps) = some final) :
    (initialMachine guestCommand phys initial regs programs).bind
      (fun m => Program.guestRun m steps) = some final := by
  rw [show emittedCommand = guestCommand from funext rawCommandMatches] at source
  cases ready : initialMachine guestCommand phys initial regs programs with
  | none => simp [ready] at source
  | some m => exact Program.programRefines m final steps (by simpa [ready] using source)

def Contract : Prop :=
    DeferredAcquire.Contract ∧
    (∀ slot, sourcePlan slot = guestPlan slot) ∧
    (∀ slot plan, sourcePlan slot = some plan → cannotFuseFill (architecturalUnit slot) plan = true) ∧
    (∀ slot, emittedCommand slot = guestCommand slot) ∧
    DeferredAcquire.sourceMasks = architecturalMasks ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (phys : Cpu → TcgMemory.Word → TcgMemory.Word)
      (initial : Nat → Byte) (regs : Cpu → Registers) (programs : List (Cpu × List SlotInput))
      (actions : List (DeferredAcquire.Action Cpu)),
      replay DeferredAcquire.sourceMasks emittedCommand phys initial regs programs actions =
        replay architecturalMasks guestCommand phys initial regs programs actions) ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (phys : Cpu → TcgMemory.Word → TcgMemory.Word)
      (initial : Nat → Byte) (regs : Cpu → Registers) (programs : List (Cpu × List SlotInput))
      (steps : List (Program.Schedule Cpu)) (final : Program.Machine Cpu),
      (initialMachine emittedCommand phys initial regs programs).bind
        (fun m => Program.sourceRun m steps) = some final →
      (initialMachine guestCommand phys initial regs programs).bind
        (fun m => Program.guestRun m steps) = some final) ∧
    (∀ (s : SlotInput) (r : Request) (regs : Registers),
      emittedCommand s = some (.memory (.scalar r)) →
      guestCommand s = some (.memory (.scalar r)) ∧ tcgStep regs r = specStep regs r ∧
      (tcgStep regs r).effect.map effectAddress =
        (if (tcgStep regs r).exit = .success then some (getGR regs (baseRegister r.op)) else none) ∧
      ((tcgStep regs r).exit ≠ .success →
        (tcgStep regs r).regs = regs ∧ (tcgStep regs r).effect = none)) ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (mask : DeferredAcquire.Masks)
      (d after : DeferredAcquire.State Cpu) (cpu : Cpu) (index : Nat)
      (held : DeferredAcquire.Held),
      (d.held cpu)[index]? = some held →
      DeferredAcquire.complete mask d cpu index = some after →
      (after.memory.threads cpu).regs =
        DeferredAcquire.completeRegisters held.op (d.memory.threads cpu).regs
          (specLoad held.op held.regs (DeferredAcquire.readInputs d.memory cpu held)).regs) ∧
    (∀ (prior : DeferredAcquire.Held) (address size : Nat) (regs : List Reg),
      prior.op.update ≠ .none → regs.contains prior.op.base = true →
      DeferredAcquire.beforeIndependent prior address size regs = false) ∧
    (∀ (op : Load) (current result : Registers) (r : Reg), r ≠ op.target → r ≠ op.base →
      (DeferredAcquire.completeRegisters op current result).gr r = current.gr r ∧
      (DeferredAcquire.completeRegisters op current result).nat r = current.nat r ∧
      (DeferredAcquire.completeRegisters op current result).dirty r = current.dirty r)

theorem contract : Contract :=
  ⟨DeferredAcquire.contract, rawPlanMatches, ordinaryFillPath, rawCommandMatches, schedulingMasks,
    rawProgramMatches, rawProgramRefines, decodedScalarResult, DeferredAcquire.completionWritesGuestRegisters,
    DeferredAcquire.waitsForUpdatedBase, DeferredAcquire.completePreservesOther⟩

private def bundle (mode : BitVec 6) (base reg : BitVec 7) (hint : BitVec 2) : SlotInput :=
  let payload := 4#4 ++ 0#1 ++ mode ++ hint ++ 0#1 ++ base ++
    (if 48 ≤ mode then reg ++ 0#7 else 0#7 ++ reg) ++ 0#6
  ⟨payload.zeroExtend 64 <<< (5 : Nat), 0, 0, 0, Program.cell⟩

private def messagePassing : List (Bool × List SlotInput) :=
  [(false, [bundle 0x32 4 6 0, bundle 0x34 5 7 3]),
   (true, [bundle 0x14 5 10 2, bundle 0x02 4 12 1])]

private def physical (cpu : Bool) (addr : TcgMemory.Word) := if cpu then addr - 0x1000 else addr

private def runLitmus :=
  replay DeferredAcquire.sourceMasks emittedCommand physical (fun _ => 0)
    Program.localRegs messagePassing

example : (runLitmus (DeferredAcquire.issueLoads ++ [.finish true 0])).isNone = true := by
  unfold runLitmus
  rw [rawProgramMatches]
  rfl

example : (runLitmus (DeferredAcquire.issueLoads ++ DeferredAcquire.deliver ++
    [.finish true 1, .finish true 0])).map DeferredAcquire.registersSeen =
      some (1, 0x12345678) := by
  unfold runLitmus
  rw [rawProgramMatches]
  rfl

private def inOrderSteps : List (Program.Schedule Bool) :=
  [.issue false, .issue false, .publish false 1, .publish false 0, .issue true, .issue true]

example : ∃ final,
    (initialMachine emittedCommand physical (fun _ => 0) Program.localRegs messagePassing).bind
      (fun m => Program.sourceRun m inOrderSteps) = some final ∧
    (initialMachine guestCommand physical (fun _ => 0) Program.localRegs messagePassing).bind
      (fun m => Program.guestRun m inOrderSteps) = some final ∧
    (final.threads true).regs.gr 10 = 1 ∧ (final.threads true).regs.gr 12 = 0x12345678 := by
  let final := ((initialMachine guestCommand physical (fun _ => 0) Program.localRegs
    messagePassing).bind (fun m => Program.sourceRun m inOrderSteps)).get (by rfl)
  have src : (initialMachine emittedCommand physical (fun _ => 0) Program.localRegs
      messagePassing).bind (fun m => Program.sourceRun m inOrderSteps) = some final := by
    rw [show emittedCommand = guestCommand from funext rawCommandMatches]
    rfl
  exact ⟨final, src, rawProgramRefines _ _ _ _ _ _ src, rfl, rfl⟩

private def postBundle (major : BitVec 4) (m : BitVec 1) (mode : BitVec 6) (mid : BitVec 1)
    (base a b : BitVec 7) (qp : BitVec 6) : SlotInput :=
  let payload := major ++ m ++ mode ++ 0#2 ++ mid ++ base ++ b ++ a ++ qp
  ⟨payload.zeroExtend 64 <<< (5 : Nat), 0, 0, 0, Program.cell⟩

private def regLoad (mode : BitVec 6) (base dst inc : BitVec 7) (qp : BitVec 6) :=
  postBundle 4 1 mode 0 base dst inc qp

private def immMemory (mode : BitVec 6) (base data : BitVec 7) (increment : BitVec 9)
    (qp : BitVec 6) :=
  let low := increment.extractLsb' 0 7
  postBundle 5 (increment.extractLsb' 8 1) mode (increment.extractLsb' 7 1) base
    (if 48 ≤ mode then low else data) (if 48 ≤ mode then data else low) qp

private def updateRegs (cpu : Bool) : Registers :=
  let init := Program.localRegs cpu
  { init with gr := fun r => if cpu && r = 10 then 4 else init.gr r
              nat := fun r => cpu && r = 10 }

private def releaseUpdates : List SlotInput :=
  [immMemory 0x32 4 4 4 0, immMemory 0x34 5 7 (-8) 0]

private def loadUpdates : List SlotInput :=
  [regLoad 0x14 5 10 10 0, immMemory 0x02 4 12 (-4) 0]

private def updateRun (programs : List (Bool × List SlotInput)) :=
  replay DeferredAcquire.sourceMasks emittedCommand physical (fun _ => 0) updateRegs programs

private def observedUpdates (d : DeferredAcquire.State Bool) :=
  let writer := (d.memory.threads false).regs
  let reader := (d.memory.threads true).regs
  (writer.gr 4, writer.gr 5, reader.gr 10, reader.gr 12,
    reader.gr 5, reader.gr 4, reader.nat 5, reader.nat 10)

-- ST4 uses the old base as both address and data. LD1.ACQ uses the old
-- NaT-marked destination as its increment. Both deferred loads retain
-- their old read address and complete their own base update.
example : (updateRun [(false, releaseUpdates), (true, loadUpdates)]
    (DeferredAcquire.issueLoads ++ DeferredAcquire.deliver ++
      [.finish true 1, .finish true 0])).map observedUpdates =
      some (20, 16, 1, 16, 0x101c, 0x100c, true, false) := by
  unfold updateRun
  rw [rawProgramMatches]
  rfl

-- The second access is to disjoint bytes, but its register increment
-- would read a pending writeback of the first load's base.
example : (updateRun [(true, [regLoad 0x14 5 10 10 0, regLoad 0x02 4 12 5 0])]
    DeferredAcquire.issueLoads).isNone = true := by
  unfold updateRun
  rw [rawProgramMatches]
  rfl

private def stoppedUpdate (d : DeferredAcquire.State Bool) :=
  let t := d.memory.threads false
  (t.regs.gr 4, t.regs.nat 4, t.fault, t.pending.isEmpty, d.memory.trace.isEmpty)

-- Target/base alias is illegal when qualified and nullified when p1 is
-- false. A qualified store that aborts in the primitive leaves its old
-- base and does not enter either the trace or the pending-store queue.
example : (updateRun [(false, [regLoad 0x00 4 4 4 0])] [.execute false]).map stoppedUpdate =
      some (16, false, some .illegalRegister, true, true) := by
  unfold updateRun
  rw [rawProgramMatches]
  rfl

example : (updateRun [(false, [regLoad 0x00 4 4 4 1])] [.execute false]).map stoppedUpdate =
      some (16, false, none, true, true) := by
  unfold updateRun
  rw [rawProgramMatches]
  rfl

private def abortingStore : SlotInput :=
  let s := immMemory 0x36 4 4 (-4) 0
  {s with cell := {s.cell with primitiveReturns := false}}

example : (updateRun [(false, [abortingStore])] [.execute false]).map stoppedUpdate =
      some (16, false, some .primitiveAbort, true, true) := by
  unfold updateRun
  rw [rawProgramMatches]
  rfl

private def fillStore := immMemory 0x34 4 7 1 0

example : (sourcePlan fillStore).isSome = true := by
  rw [rawPlanMatches]
  rfl

-- MIB admits a same-bundle BR.CLOOP in slot 2. That TB shape is kept
-- outside the ordinary proof even when the selected memory slot agrees.
example : (sourcePlan {fillStore with low := fillStore.low ||| 0x10}).isNone = true := by
  rw [rawPlanMatches]
  rfl

end TcgMemory.Encoded
