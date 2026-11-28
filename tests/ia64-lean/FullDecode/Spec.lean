/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace FullDecodeSpec

abbrev Word := BitVec 64
abbrev UnitCode := BitVec 3
abbrev Slot := BitVec 41
abbrev Observation := BitVec 395

def slotBits (low high : Word) (slot : BitVec 2) : Slot :=
  let bundle := high ++ low
  if slot = 0 then bundle.extractLsb' 5 41
  else if slot = 1 then bundle.extractLsb' 46 41
  else if slot = 2 then bundle.extractLsb' 87 41
  else 0

def templateBits (low : Word) : BitVec 5 := low.extractLsb' 0 5

-- Unit letters carry their source ABI codes as parameters. The SDM
-- table supplies the mapping to template rows independently.
def templateUnit (reserved m i b f l x : UnitCode) (code : BitVec 5) (slot : BitVec 2) :
    UnitCode :=
  let row := code.extractLsb' 1 4
  if slot = 0 then
    if row = 11 then b
    else if row = 3 ∨ row = 10 ∨ row = 13 ∨ row = 15 then reserved else m
  else if slot = 1 then
    if row = 0 ∨ row = 1 ∨ row = 8 then i
    else if row = 2 then l
    else if row = 4 ∨ row = 5 ∨ row = 7 ∨ row = 12 then m
    else if row = 6 ∨ row = 14 then f
    else if row = 9 ∨ row = 11 then b
    else reserved
  else if slot = 2 then
    if row = 0 ∨ row = 1 ∨ row = 4 ∨ row = 5 ∨ row = 6 then i
    else if row = 2 then x
    else if row = 7 then f
    else if row = 8 ∨ row = 9 ∨ row = 11 ∨ row = 12 ∨ row = 14 then b
    else reserved
  else reserved

def validTemplate (code : BitVec 5) : Bool :=
  let row := code.extractLsb' 1 4
  decide (row ≠ 3 ∧ row ≠ 10 ∧ row ≠ 13 ∧ row ≠ 15)

-- An A-unit instruction can occupy either an M or an I slot.
-- A1/A2 bit 36 is ignored. ve is an opcode extension.
inductive CompareFormat where
  | a6 | a7 | a8
  deriving DecidableEq

inductive IntegerSevenFormat where
  | i1 | i2 | i3 | i4 | i9
  deriving DecidableEq

inductive IntegerBitfieldFormat where
  | i10 | i11 (signed : Bool) | i12 | i13 | i14 | i15

inductive IntegerTestFormat where
  | i16 | i17 | i30

inductive IntegerMiscFormat where
  | hint | check | toBranch | fromBranch | toPredicates | rotatingPredicates
  | fromPredicates | fromIp | toAr | immediateAr | fromAr
  | extend (x6 : BitVec 6)

inductive FpMergeAlias where
  | absolute | negate | negateAbsolute

inductive BranchFormat where
  | relative (btype : BitVec 3)
  | indirect (x6 : BitVec 6) (btype : BitVec 3)
  | call (direct : Bool)
  | predict
  | system (x6 : BitVec 6)
  | virtualSwitch
  | hint

inductive MemorySystemFormat where
  | noOperands | rse | invalidateEntry
  | cacheFlush (instruction : Bool)
  | hint | toDahr | mask
  | toControl | fromControl | toStatus (onlyLow : Bool) | fromStatus
  | toIndexed | fromIndexed | toRegion | fromRegion
  | translation (release : Bool)

inductive FloatingArithmetic where
  | fma | fms | fnma | fpma | fpms | fpnma | fadd | fsub | fmpy | fnorm
  deriving DecidableEq

inductive Form where
  | a1 (x4 x2b : BitVec 4)
  | a2 (x4 : BitVec 4)
  | a3 (x4 x2b : BitVec 4)
  | a4 (x2a : BitVec 2)
  | a5
  | cmp (format : CompareFormat) (major : BitVec 4) (x2 : BitVec 2)
        (ta : BitVec 1) (c : Option (BitVec 1))
  | a9 (size : BitVec 2) (x4 : BitVec 4) (x2bLo x2bHi : BitVec 2)
  | a10 (x4 : BitVec 4)
  | iMajorSeven (format : IntegerSevenFormat) (size x2a x2b : BitVec 2)
        (x2c : Option (BitVec 2))
  | iShift (size : BitVec 2) (left unsigned : Bool)
  | iBitfield (format : IntegerBitfieldFormat)
  | iPredicate (format : IntegerTestFormat) (nonzero : Bool)
  | iMisc (format : IntegerMiscFormat)
  | load (x6 : BitVec 6) (bias acquire : Option (BitVec 6))
  | store (x6 : BitVec 6) (release : Bool)
  | semaphore (acquire : BitVec 6) (release : Option (BitVec 6)) (fetchadd : Bool)
  | wideLoad
  | wideStore
  | fpLoad (width : BitVec 2) (fill : Bool)
  | fpPair (width : BitVec 2)
  | fpStore (x6 : BitVec 6)
  | fpTransfer (major : BitVec 4) (x6 : BitVec 6)
  | f1 (arith : FloatingArithmetic)
  | f2 (selector : BitVec 2) (multiplyAlias : Bool)
  | f3
  | f4
  | fApprox (parallel sqrt : Bool)
  | f8 (parallel : Bool) (first last : BitVec 6)
  | f10 (parallel : Bool) (unsigned : Option Bool)
  | f11
  | fStatus (x6 : BitVec 6)
  | f9 (major : BitVec 4) (x6 : BitVec 6)
  | fpMergeAlias (alias : FpMergeAlias)
  | fClass
  | branch (format : BranchFormat)
  | mSystem (major : BitVec 4) (x6 : BitVec 6) (format : MemorySystemFormat)
  | mAllocate
  | prefetchCount

def aUnit (m i u : UnitCode) : Prop := u = m ∨ u = i

-- CHK_S and the three GR/AR transfer tags have I-unit and M-unit
-- encodings. Major and x6 identify the unit on those shared rows.
def formUnit (m i b f : UnitCode) (form : Form) (unit : UnitCode) (raw : Slot) : Prop :=
  match form with
  | .load .. | .store .. | .semaphore .. | .wideLoad | .wideStore
  | .fpLoad .. | .fpPair .. | .fpStore .. | .fpTransfer ..
  | .mSystem .. | .mAllocate | .prefetchCount => unit = m
  | .iMajorSeven .. | .iShift .. | .iBitfield .. | .iPredicate .. => unit = i
  | .iMisc .check | .iMisc .toAr | .iMisc .fromAr =>
    unit = (if raw.extractLsb' 37 4 = 1 then m else i)
  | .iMisc .immediateAr => unit = (if raw.extractLsb' 27 6 = 0x28 then m else i)
  | .iMisc .. => unit = i
  | .f1 .. | .f2 .. | .f3 | .f4 | .fApprox .. | .f8 .. | .f10 .. | .f11 | .fStatus ..
  | .f9 .. | .fpMergeAlias .. | .fClass => unit = f
  | .branch .. => unit = b
  | _ => aUnit m i unit

instance (m i b f : UnitCode) (form : Form) (unit : UnitCode) (raw : Slot) :
    Decidable (formUnit m i b f form unit raw) := by
  cases form <;> try (dsimp only [formUnit, aUnit]; infer_instance)
  case iMisc format => cases format <;> dsimp only [formUnit] <;> infer_instance

def branchEncoding (format : BranchFormat) (raw : Slot) : Prop :=
  let major := raw.extractLsb' 37 4
  let x6 := raw.extractLsb' 27 6
  let btype := raw.extractLsb' 6 3
  let qp := raw.extractLsb' 0 6
  match format with
  | .relative kind => major = 4 ∧ btype = kind ∧ (5 ≤ kind → qp = 0)
  | .indirect ext kind => major = 0 ∧ x6 = ext ∧ btype = kind ∧ (kind = 1 → qp = 0)
  | .call direct => major = (if direct then 5 else 1)
  | .predict => major = 7 ∨ (major = 2 ∧ (x6 = 0x10 ∨ x6 = 0x11))
  | .system ext => major = 0 ∧ x6 = ext ∧ qp = 0
  | .virtualSwitch => major = 0 ∧ (x6 = 0x18 ∨ x6 = 0x19) ∧ qp = 0
  | .hint => major = 2 ∧ x6 = 1

instance (format : BranchFormat) (raw : Slot) :
    Decidable (branchEncoding format raw) := by
  cases format <;> dsimp only [branchEncoding] <;> infer_instance

def memorySystemEncoding (format : MemorySystemFormat) (x6 : BitVec 6) (raw : Slot) : Prop :=
  let ext := raw.extractLsb' 27 6
  match format with
  | .rse => ext = x6 ∧ raw.extractLsb' 0 6 = 0
  | .hint => ext = x6 ∧ raw.getLsbD 26 = true ∧ raw.extractLsb' 10 2 = 0
  | .toDahr => ext = x6 ∧ raw.getLsbD 26 = true ∧ raw.extractLsb' 10 2 = 1
  | .mask => raw.extractLsb' 27 4 = x6.setWidth 4
  | .invalidateEntry => ext = x6 ∨ ext = (x6 + 1)
  | .cacheFlush instruction => ext = x6 ∧ raw.getLsbD 36 = instruction
  | _ => ext = x6

instance (format : MemorySystemFormat) (x6 : BitVec 6) (raw : Slot) :
    Decidable (memorySystemEncoding format x6 raw) := by
  cases format <;> dsimp only [memorySystemEncoding] <;> infer_instance

-- Bit 36 is tb in A6/A7 and the immediate sign in A8.
-- Bits 19:13 are literal zero in A7.
def compareEncoding (format : CompareFormat) (major : BitVec 4) (x2 : BitVec 2)
    (ta : BitVec 1) (c : Option (BitVec 1)) (raw : Slot) : Prop :=
  raw.extractLsb' 37 4 = major ∧ raw.extractLsb' 34 2 = x2 ∧ raw.extractLsb' 33 1 = ta ∧
    (match c with | none => True | some z => raw.extractLsb' 12 1 = z) ∧
    match format with
    | .a6 => raw.extractLsb' 36 1 = 0
    | .a7 => raw.extractLsb' 36 1 = 1 ∧ raw.extractLsb' 13 7 = 0
    | .a8 => True

instance (format : CompareFormat) (major : BitVec 4) (x2 : BitVec 2)
    (ta : BitVec 1) (c : Option (BitVec 1)) (raw : Slot) :
    Decidable (compareEncoding format major x2 ta c raw) := by
  cases format <;> cases c <;> dsimp only [compareEncoding] <;> infer_instance

def hasExtension (encoded : BitVec 6) (extension : Option (BitVec 6)) : Prop :=
  match extension with | none => False | some x6 => encoded = x6

instance (encoded : BitVec 6) (extension : Option (BitVec 6)) :
    Decidable (hasExtension encoded extension) := by
  cases extension <;> dsimp only [hasExtension] <;> infer_instance

-- Tables 4-34–4-38 assign normal, speculative, advanced, combined
-- speculative/advanced, and both checking completers for FP loads.
def fpLoadRow (row : BitVec 4) : Prop := row ≤ 3 ∨ row = 8 ∨ row = 9

instance (row : BitVec 4) : Decidable (fpLoadRow row) := by unfold fpLoadRow; infer_instance

def iMajorSeven (size : BitVec 2) (raw : Slot) : Prop :=
  raw.extractLsb' 37 4 = 7 ∧
    (raw.extractLsb' 36 1 ++ raw.extractLsb' 33 1) = size ∧ raw.extractLsb' 32 1 = 0

instance (size : BitVec 2) (raw : Slot) : Decidable (iMajorSeven size raw) := by
  unfold iMajorSeven
  infer_instance

def iCell (x2a x2b x2c : BitVec 2) (raw : Slot) : Prop :=
  raw.extractLsb' 34 2 = x2a ∧ raw.extractLsb' 28 2 = x2b ∧ raw.extractLsb' 30 2 = x2c

instance (x2a x2b x2c : BitVec 2) (raw : Slot) : Decidable (iCell x2a x2b x2c raw) := by
  unfold iCell
  infer_instance

-- Table 4-74: MUX1 has five assigned mbtype4 values. The low register
-- field of I9 is a literal zero. The adjacent white fields are ignored.
def mux1Type (raw : Slot) : Prop :=
  let mbt4c := raw.extractLsb' 20 4
  mbt4c = 0 ∨ (8 ≤ mbt4c ∧ mbt4c ≤ 11)

def iSevenFields (format : IntegerSevenFormat) (x2c : Option (BitVec 2)) (raw : Slot) : Prop :=
  (match x2c with | none => True | some c => raw.extractLsb' 30 2 = c) ∧
    match format with
    | .i3 => mux1Type raw
    | .i9 => raw.extractLsb' 13 7 = 0
    | _ => True

instance (format : IntegerSevenFormat) (x2c : Option (BitVec 2)) (raw : Slot) :
    Decidable (iSevenFields format x2c raw) := by
  cases format <;> cases x2c <;> dsimp only [iSevenFields, mux1Type] <;> infer_instance

-- I5/I7 use a register count. I6/I8 add one fixed-count cell when
-- za:zb selects packed halfwords or words. Size 3 has only variable
-- shifts at major seven (scalar immediate shifts have other forms).
def iShiftCells (size : BitVec 2) (left unsigned : Bool) (raw : Slot) : Prop :=
  if left then
    iCell 0 0 1 raw ∨ (size ≠ 3 ∧ iCell 3 1 1 raw)
  else
    iCell 0 (if unsigned then 0 else 2) 0 raw ∨
      (size ≠ 3 ∧ iCell 1 (if unsigned then 1 else 3) 0 raw)

instance (size : BitVec 2) (left unsigned : Bool) (raw : Slot) :
    Decidable (iShiftCells size left unsigned raw) := by
  unfold iShiftCells
  infer_instance

-- Tables 4-21 and 4-22. Bit 36 in I10–I12 and bit 13 in I14 are
-- white, ignored fields in the operand-format diagrams (§4.1).
def iBitfieldEncoding (format : IntegerBitfieldFormat) (raw : Slot) : Prop :=
  let major := raw.extractLsb' 37 4
  let x := raw.extractLsb' 33 1
  let x2 := raw.extractLsb' 34 2
  match format with
  | .i15 => major = 4
  | .i10 => major = 5 ∧ x2 = 3 ∧ x = 0
  | .i11 signed => major = 5 ∧ x2 = 1 ∧ x = 0 ∧ raw.getLsbD 13 = signed
  | .i12 => major = 5 ∧ x2 = 1 ∧ x = 1 ∧ raw.getLsbD 26 = false
  | .i13 => major = 5 ∧ x2 = 1 ∧ x = 1 ∧ raw.getLsbD 26 = true
  | .i14 => major = 5 ∧ x2 = 3 ∧ x = 1

instance (format : IntegerBitfieldFormat) (raw : Slot) : Decidable (iBitfieldEncoding format raw) := by
  cases format <;> dsimp only [iBitfieldEncoding] <;> infer_instance

-- Table 4-23: normal and .unc always test zero. The c bit selects
-- test-nonzero only for .and, .or, and .or.andcm.
def iTestParallel (raw : Slot) : Bool := raw.getLsbD 33 || raw.getLsbD 36

def iTestEncoding (format : IntegerTestFormat) (nonzero : Bool) (raw : Slot) : Prop :=
  raw.extractLsb' 37 4 = 5 ∧ raw.extractLsb' 34 2 = 0 ∧
    (iTestParallel raw && raw.getLsbD 12) = nonzero ∧
    match format with
    | .i16 => raw.getLsbD 13 = false
    | .i17 => raw.getLsbD 13 = true ∧ raw.getLsbD 19 = false
    | .i30 => raw.getLsbD 13 = true ∧ raw.getLsbD 19 = true ∧ raw.extractLsb' 20 7 = 0

instance (format : IntegerTestFormat) (nonzero : Bool) (raw : Slot) :
    Decidable (iTestEncoding format nonzero raw) := by
  cases format <;> dsimp only [iTestEncoding] <;> infer_instance

-- Tables 4-24–4-27. I21's normal and return forms differ in branch
-- prediction hints, and both use the same architectural GR/BR data.
def iMiscEncoding (format : IntegerMiscFormat) (raw : Slot) : Prop :=
  let major := raw.extractLsb' 37 4
  let x3 := raw.extractLsb' 33 3
  let x6 := raw.extractLsb' 27 6
  match format with
  | .hint => major = 0 ∧ x3 = 0 ∧ x6 = 1 ∧ raw.getLsbD 26 = true
  | .toBranch => major = 0 ∧ x3 = 7
  | .toPredicates => major = 0 ∧ x3 = 3
  | .rotatingPredicates => major = 0 ∧ x3 = 2
  | .fromBranch => major = 0 ∧ x3 = 0 ∧ x6 = 0x31
  | .fromPredicates => major = 0 ∧ x3 = 0 ∧ x6 = 0x33
  | .fromIp => major = 0 ∧ x3 = 0 ∧ x6 = 0x30
  | .toAr => (major = 0 ∨ major = 1) ∧ x3 = 0 ∧ x6 = 0x2a
  | .immediateAr => major = 0 ∧ x3 = 0 ∧ (x6 = 0x0a ∨ x6 = 0x28)
  | .fromAr => x3 = 0 ∧ (major = 0 ∧ x6 = 0x32 ∨ major = 1 ∧ x6 = 0x22)
  | .check => (major = 0 ∧ x3 = 1) ∨ (major = 1 ∧ (x3 = 1 ∨ x3 = 3))
  | .extend ext => major = 0 ∧ x3 = 0 ∧ x6 = ext

instance (format : IntegerMiscFormat) (raw : Slot) :
    Decidable (iMiscEncoding format raw) := by
  cases format <;> dsimp only [iMiscEncoding] <;> infer_instance

-- The Vol. 3 FPABS, FPNEG, and FPNEGABS pseudo-instructions are
-- encoded by particular F9 input registers. The two negation
-- spellings overlap when both registers are f0 and use FPNEGABS.
def fpMergeAliasEncoding (alias : FpMergeAlias) (raw : Slot) : Prop :=
  let fa := raw.extractLsb' 13 7
  let fb := raw.extractLsb' 20 7
  let x6 := raw.extractLsb' 27 6
  match alias with
  | .absolute => x6 = 0x10 ∧ fa = 0
  | .negate => x6 = 0x11 ∧ fa = fb ∧ fa ≠ 0
  | .negateAbsolute => x6 = 0x11 ∧ fa = 0

instance (alias : FpMergeAlias) (raw : Slot) : Decidable (fpMergeAliasEncoding alias raw) := by
  cases alias <;> dsimp only [fpMergeAliasEncoding] <;> infer_instance

-- F1 constant-register aliases retain their physical operand encodings.
def f1Encoding (arith : FloatingArithmetic) (raw : Slot) : Prop :=
  let major := raw.extractLsb' 37 4
  let x := raw.extractLsb' 36 1
  let addFamily := major = 8 ∨ major = 9 ∧ x = 0
  let subFamily := major = 10 ∨ major = 11 ∧ x = 0
  let addendZero := raw.extractLsb' 13 7 = 0
  let multiplierOne := raw.extractLsb' 27 7 = 1
  match arith with
  | .fma => addFamily ∧ ¬ addendZero ∧ ¬ multiplierOne
  | .fms => subFamily ∧ ¬ multiplierOne
  | .fnma => major = 12 ∨ major = 13 ∧ x = 0
  | .fpma => major = 9 ∧ x = 1
  | .fpms => major = 11 ∧ x = 1
  | .fpnma => major = 13 ∧ x = 1
  | .fadd => addFamily ∧ multiplierOne ∧ ¬ addendZero
  | .fsub => subFamily ∧ multiplierOne ∧ ¬ addendZero
  | .fmpy => addFamily ∧ addendZero ∧ ¬ multiplierOne
  | .fnorm => (addFamily ∨ subFamily) ∧ multiplierOne ∧ addendZero

def aEncoding (form : Form) (raw : Slot) : Prop :=
  let major := raw.extractLsb' 37 4
  let x2a := raw.extractLsb' 34 2
  let ve := raw.extractLsb' 33 1
  let x4 := raw.extractLsb' 29 4
  let x2b := (raw.extractLsb' 27 2).zeroExtend 4
  match form with
  | .a1 h l | .a3 h l => major = 8 ∧ x2a = 0 ∧ ve = 0 ∧ x4 = h ∧ x2b = l
  | .a2 h => major = 8 ∧ x2a = 0 ∧ ve = 0 ∧ x4 = h
  | .a4 ext => major = 8 ∧ x2a = ext ∧ ve = 0
  | .a5 => major = 9
  | .cmp format op x2 ta c => compareEncoding format op x2 ta c raw
  | .a9 size h lo hi =>
    let za := raw.extractLsb' 36 1
    let ext := raw.extractLsb' 27 2
    major = 8 ∧ x2a = 1 ∧ (za ++ ve) = size ∧ x4 = h ∧ lo ≤ ext ∧ ext ≤ hi
  | .a10 h =>
    major = 8 ∧ x2a = 1 ∧ raw.extractLsb' 36 1 = 0 ∧ ve = 1 ∧ x4 = h ∧
      raw.extractLsb' 27 2 ≤ 2
  | .iMajorSeven format size a b c =>
    iMajorSeven size raw ∧ x2a = a ∧ raw.extractLsb' 28 2 = b ∧ iSevenFields format c raw
  | .iShift size left unsigned => iMajorSeven size raw ∧ iShiftCells size left unsigned raw
  | .iBitfield format => iBitfieldEncoding format raw
  | .iPredicate format nonzero => iTestEncoding format nonzero raw
  | .iMisc format => iMiscEncoding format raw
  -- Tables 4-30–4-33 and §§4.4.1 and 4.4.3. Neither assigned nor
  -- unused cache-locality hints may affect functional behavior (p. 3:300).
  | .load primary bias acquire =>
    let x6 := raw.extractLsb' 30 6
    (major = 4 ∧ raw.extractLsb' 27 1 = 0 ∨ major = 5) ∧
      (x6 = primary ∨ hasExtension x6 bias ∨ hasExtension x6 acquire)
  | .store x6 _ =>
    (major = 4 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 0 ∨ major = 5) ∧
      raw.extractLsb' 30 6 = x6
  | .semaphore acquire release _ =>
    let x6 := raw.extractLsb' 30 6
    major = 4 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 1 ∧
      (x6 = acquire ∨ hasExtension x6 release)
  | .wideLoad =>
    let x6 := raw.extractLsb' 30 6
    major = 4 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 1 ∧
      (x6 = 40 ∨ x6 = 44)
  | .wideStore =>
    let x6 := raw.extractLsb' 30 6
    major = 4 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 1 ∧
      (x6 = 48 ∨ x6 = 52)
  | .fpLoad width fill =>
    (major = 6 ∧ raw.extractLsb' 27 1 = 0 ∨ major = 7) ∧
      raw.extractLsb' 30 2 = width ∧
      (if fill then raw.extractLsb' 32 4 = 6 else fpLoadRow (raw.extractLsb' 32 4))
  | .fpPair width =>
    major = 6 ∧ raw.extractLsb' 27 1 = 1 ∧
      raw.extractLsb' 30 2 = width ∧ fpLoadRow (raw.extractLsb' 32 4)
  | .fpStore x6 =>
    (major = 6 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 0 ∨ major = 7) ∧
      raw.extractLsb' 30 6 = x6
  -- §4.4.4, M18 and M19. M must be zero for transfers.
  | .fpTransfer op x6 =>
    major = op ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 27 1 = 1 ∧
      raw.extractLsb' 30 6 = x6
  -- Tables 4-59–4-61 and 4-67. White fields are ignored and
  -- F5 includes both ta values in its single decoder tag.
  | .f9 op x6 =>
    major = op ∧ ve = 0 ∧ raw.extractLsb' 27 6 = x6 ∧
    (op = 1 ∧ (x6 = 0x10 ∨ x6 = 0x11) → raw.extractLsb' 13 7 ≠ 0) ∧
    (op = 1 ∧ x6 = 0x11 → raw.extractLsb' 13 7 ≠ raw.extractLsb' 20 7)
  | .fpMergeAlias alias => major = 1 ∧ ve = 0 ∧ fpMergeAliasEncoding alias raw
  | .f1 arith => f1Encoding arith raw
  | .f2 selector alias =>
    major = 14 ∧ raw.extractLsb' 36 1 = 1 ∧ raw.extractLsb' 34 2 = selector ∧
    (selector = 2 → if alias then raw.extractLsb' 13 7 = 0 else raw.extractLsb' 13 7 ≠ 0)
  | .f3 => major = 14 ∧ raw.extractLsb' 36 1 = 0
  | .f4 => major = 4
  | .fApprox parallel sqrt =>
    major = (if parallel then 1 else 0) ∧ ve = 1 ∧ raw.getLsbD 36 = sqrt
  | .f8 parallel first last =>
    let ext := raw.extractLsb' 27 6
    major = (if parallel then 1 else 0) ∧ ve = 0 ∧ first ≤ ext ∧ ext ≤ last
  | .f10 parallel unsigned =>
    major = (if parallel then 1 else 0) ∧ ve = 0 ∧ raw.extractLsb' 29 4 = 6 ∧
      (match unsigned with
       | some expected => raw.getLsbD 27 = expected
       | none => True)
  | .f11 => major = 0 ∧ ve = 0 ∧ raw.extractLsb' 27 6 = 0x1c
  | .fStatus x6 => major = 0 ∧ ve = 0 ∧ raw.extractLsb' 27 6 = x6
  | .fClass => major = 5
  | .branch format => branchEncoding format raw
  | .mSystem op x6 format =>
    major = op ∧ raw.extractLsb' 33 3 = 0 ∧ memorySystemEncoding format x6 raw
  | .mAllocate => major = 1 ∧ raw.extractLsb' 33 3 = 6
  | .prefetchCount =>
    major = 6 ∧ raw.extractLsb' 36 1 = 0 ∧ raw.extractLsb' 30 6 = 0x2c ∧
      raw.extractLsb' 27 1 = 0 ∧ raw.extractLsb' 19 1 = 1

instance (m i u : UnitCode) : Decidable (aUnit m i u) := by unfold aUnit; infer_instance
instance (form : Form) (raw : Slot) : Decidable (aEncoding form raw) := by
  cases form <;> simp only [aEncoding]
  case f1 arith => cases arith <;> dsimp [f1Encoding] <;> infer_instance
  case f10 parallel unsigned => cases unsigned <;> infer_instance
  all_goals infer_instance

def imm8 (raw : Slot) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 13 7).signExtend 64

def imm14 (raw : Slot) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 27 6 ++ raw.extractLsb' 13 7).signExtend 64

def imm22 (raw : Slot) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 22 5 ++ raw.extractLsb' 27 9 ++
   raw.extractLsb' 13 7).signExtend 64

def reg (raw : Slot) (low width : Nat) : BitVec 8 :=
  (raw.extractLsb' low width).zeroExtend 8

def prefetchCount (raw : Slot) : Word := (raw.extractLsb' 6 6).zeroExtend 64 + 1

def prefetchStride (raw : Slot) : Word :=
  (raw.extractLsb' 13 5).signExtend 64 <<< 6

def imm9 (raw : Slot) (low : Nat) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 27 1 ++ raw.extractLsb' low 7).signExtend 64

def inc3 (raw : Slot) : Word :=
  let code := raw.extractLsb' 13 2
  let magnitude : Word := if code = 3 then 1 else (1 : Word) <<< (4 - code.zeroExtend 64)
  if raw.getLsbD 15 then 0 - magnitude else magnitude

def iRegisterCount (raw : Slot) : Prop := raw.extractLsb' 34 2 = 0

instance (raw : Slot) : Decidable (iRegisterCount raw) := by
  unfold iRegisterCount
  infer_instance

-- Left scalar shifts use the same decoded record layout as right
-- shifts: r2 carries the count and r3 carries the operand to shift.
-- The packed instructions keep the I7 raw field order.
def iShiftR2 (size : BitVec 2) (left : Bool) (raw : Slot) : BitVec 8 :=
  if left ∧ size = 3 then reg raw 20 7
  else if left ∨ iRegisterCount raw then reg raw 13 7
  else 0

def iShiftR3 (size : BitVec 2) (left : Bool) (raw : Slot) : BitVec 8 :=
  if left ∧ size = 3 then reg raw 13 7
  else if left ∧ ¬ iRegisterCount raw then 0
  else reg raw 20 7

def iShiftImmediate (size : BitVec 2) (left : Bool) (raw : Slot) : Word :=
  if size = 3 then 0
  else if iRegisterCount raw then -1
  else if left then 31 - (raw.extractLsb' 20 5).zeroExtend 64
  else (raw.extractLsb' 14 5).zeroExtend 64

def multiplyShiftCount (raw : Slot) : Word :=
  let code := raw.extractLsb' 30 2
  if code = 0 then 0 else if code = 1 then 7 else if code = 2 then 15 else 16

-- Semantic positions and lengths from Table 4-74 use the translator's
-- packed operand layout, with raw immediate bits in the high field.
def packBitfield (position : BitVec 6) (length : BitVec 7) (source : BitVec 8) : Word :=
  (source ++ length ++ position).zeroExtend 64

def bitfieldImm (format : IntegerBitfieldFormat) (raw : Slot) : Word :=
  let length := (raw.extractLsb' 27 6).zeroExtend 7 + 1
  let cpos6c := raw.extractLsb' 20 6
  let pos6b := raw.extractLsb' 14 6
  let sign := raw.extractLsb' 36 1
  match format with
  | .i10 => (raw.extractLsb' 27 6).zeroExtend 64
  | .i11 _ => packBitfield pos6b length 0
  | .i12 => packBitfield (63 - cpos6c) length 0
  | .i13 => packBitfield (63 - cpos6c) length (sign ++ raw.extractLsb' 13 7)
  | .i14 => packBitfield (63 - pos6b) length (sign.zeroExtend 8)
  | .i15 => packBitfield (63 - raw.extractLsb' 31 6) ((raw.extractLsb' 27 4).zeroExtend 7 + 1) 0

def iMiscImm (format : IntegerMiscFormat) (raw : Slot) : Word :=
  let sign := raw.extractLsb' 36 1
  match format with
  | .hint => (sign ++ raw.extractLsb' 6 20).zeroExtend 64
  | .check =>
    -- The record retains the signed bundle displacement. The execution
    -- stage adds the current IP to obtain the SDM's target25.
    (sign ++ raw.extractLsb' 20 13 ++ raw.extractLsb' 6 7 ++ 0#4).signExtend 64
  | .toPredicates => (sign ++ raw.extractLsb' 24 8 ++ raw.extractLsb' 6 7 ++ 0#1).signExtend 64
  | .rotatingPredicates => (sign ++ raw.extractLsb' 6 27 ++ 0#16).signExtend 64
  | .immediateAr => imm8 raw
  | _ => 0

-- QEMU's decoded record, with non-opcode fields packed high to low.
-- presence, unit, raw, address, slot, qp, register operands r1/r2/r3,
-- unused p/b/status/precision operands, imm, class, predicate update,
-- decoder flags, valid and nonnull context. A fully absent record is 0.
def aR2 (form : Form) (raw : Slot) : BitVec 8 :=
  match form with
  | .a1 .. | .a2 .. | .cmp .a6 .. | .a9 .. | .a10 .. | .f9 .. | .fClass => reg raw 13 7
  | .f1 .fsub | .f1 .fmpy => reg raw 20 7
  | .f1 .. | .f2 .. | .f3 | .f4 | .fApprox _ false | .f8 .. | .f10 .. | .f11
  | .fStatus .. => reg raw 13 7
  | .iMajorSeven .i9 .. => 0
  | .iMajorSeven .. => reg raw 13 7
  | .iShift size left _ => iShiftR2 size left raw
  | .iBitfield .i10 | .iBitfield .i12 | .iBitfield .i15 => reg raw 13 7
  | .iMisc .check => reg raw 13 7
  | .iMisc .toBranch => reg raw 6 3
  | .iMisc .fromBranch | .iMisc .fromPredicates => reg raw 13 3
  | .iMisc .toAr | .iMisc .immediateAr | .iMisc .fromAr => reg raw 20 7
  | .fpMergeAlias .. => reg raw 20 7
  | .mSystem _ _ .toControl | .mSystem _ _ .fromControl
  | .mSystem _ _ .toRegion | .mSystem _ _ .fromRegion => reg raw 20 7
  | .mSystem _ _ (.translation _) => reg raw 13 7
  -- The no-update M2 decoder keeps the ignored raw field in this
  -- record slot. The update selector determines when it is an operand.
  | .load .. => if raw.extractLsb' 37 4 = 4 then reg raw 13 7 else 0
  | .store .. | .semaphore _ _ false | .wideStore | .fpPair .. | .fpStore ..
  | .fpTransfer .. => reg raw 13 7
  | .fpLoad .. =>
    if raw.extractLsb' 37 4 = 6 ∧ raw.extractLsb' 36 1 = 1 then reg raw 13 7 else 0
  | _ => 0

def aR3 (form : Form) (raw : Slot) : BitVec 8 :=
  match form with
  | .f1 .fsub => reg raw 13 7
  | .f1 .fmpy => reg raw 27 7
  | .a5 => reg raw 20 2
  | .fpTransfer .. | .iMajorSeven .i3 .. | .iMajorSeven .i4 .. => 0
  | .iShift size left _ => iShiftR3 size left raw
  | .iBitfield .i12 | .iBitfield .i13 | .iPredicate .i30 _ | .fpMergeAlias .. | .fClass
  | .branch .. => 0
  | .mSystem _ _ (.cacheFlush _) | .mSystem _ _ .toIndexed
  | .mSystem _ _ .fromIndexed | .mSystem _ _ (.translation _) => reg raw 20 7
  | .mSystem .. | .mAllocate => 0
  | .iMisc (.extend _) => reg raw 20 7
  | .iMisc .. => 0
  | _ => reg raw 20 7

def aImm (form : Form) (raw : Slot) : Word :=
  match form with
  | .a1 .. => 0
  | .a2 .. | .a10 .. => (raw.extractLsb' 27 2).zeroExtend 64 + 1
  | .a3 .. => imm8 raw
  | .a4 .. => imm14 raw
  | .a5 => imm22 raw
  | .cmp .a8 .. => imm8 raw
  | .cmp .. => 0
  | .a9 _ x4 .. =>
    if x4 = 0 ∨ x4 = 1 then (raw.extractLsb' 27 2).zeroExtend 64
    else if x4 = 2 then (raw.extractLsb' 27 2 - 2).zeroExtend 64
    else 0
  | .iMajorSeven .i1 .. => multiplyShiftCount raw
  | .iMajorSeven .i3 .. => (raw.extractLsb' 20 4).zeroExtend 64
  | .iMajorSeven .i4 .. => (raw.extractLsb' 20 8).zeroExtend 64
  | .iMajorSeven .. => 0
  | .iShift size left _ => iShiftImmediate size left raw
  | .iBitfield format => bitfieldImm format raw
  | .iPredicate .i16 _ => (raw.extractLsb' 14 6).zeroExtend 64
  | .iPredicate .i17 _ => 0
  | .iPredicate .i30 _ => 32 + (raw.extractLsb' 14 5).zeroExtend 64
  | .iMisc format => iMiscImm format raw
  | .load .. => if raw.extractLsb' 37 4 = 5 then imm9 raw 13 else 0
  | .store .. => if raw.extractLsb' 37 4 = 5 then imm9 raw 6 else 0
  | .semaphore _ _ fetchadd => if fetchadd then inc3 raw else 0
  | .wideLoad | .wideStore | .fpTransfer .. | .fpMergeAlias .. => 0
  | .fpLoad .. => if raw.extractLsb' 37 4 = 7 then imm9 raw 13 else 0
  | .fpStore .. => if raw.extractLsb' 37 4 = 7 then imm9 raw 6 else 0
  | .fpPair width => if raw.extractLsb' 36 1 = 1 then (if width = 2 then 8 else 16) else 0
  -- F5: Table 4-74. The paired F9 record also retains three low
  -- opcode-extension bits as implementation metadata.
  | .f1 .. | .f2 .. | .f3 => 0
  | .f4 => (raw.extractLsb' 33 1 ++ raw.extractLsb' 36 1).zeroExtend 64
  | .f8 true .. => (raw.extractLsb' 27 3).zeroExtend 64
  | .f10 .. => (raw.extractLsb' 27 2).zeroExtend 64
  | .fStatus x6 =>
    if x6 = 8 then (raw.extractLsb' 36 1 ++ raw.extractLsb' 6 20 ++ 0#4).signExtend 64
    else 0
  | .fApprox .. | .f8 false .. | .f11 => 0
  | .fClass => (raw.extractLsb' 20 7 ++ raw.extractLsb' 33 2).zeroExtend 64
  | .branch (.relative _) | .branch (.call true) =>
    (raw.extractLsb' 36 1 ++ raw.extractLsb' 13 20 ++ 0#4).signExtend 64
  | .branch .virtualSwitch => (raw.extractLsb' 27 1).zeroExtend 64
  | .branch .hint => (raw.extractLsb' 36 1 ++ raw.extractLsb' 6 20).zeroExtend 64
  | .branch .. => 0
  | .mSystem _ _ .hint =>
    (raw.extractLsb' 36 1 ++ raw.extractLsb' 12 14 ++ raw.extractLsb' 6 4).zeroExtend 64
  | .mSystem _ _ .toDahr =>
    (raw.extractLsb' 36 1 ++ raw.extractLsb' 12 11 ++ raw.extractLsb' 6 4).zeroExtend 64
  | .mSystem _ _ .mask => (raw.extractLsb' 36 1 ++ raw.extractLsb' 31 2 ++
      raw.extractLsb' 6 21).zeroExtend 64
  | .mSystem _ _ (.toStatus true) => 1
  | .mSystem .. => 0
  | .mAllocate =>
    (raw.extractLsb' 27 4 ++ raw.extractLsb' 20 7 ++ raw.extractLsb' 13 7).zeroExtend 64
  | .prefetchCount => 0
  | .f9 op _ => if op = 1 then (raw.extractLsb' 27 3).zeroExtend 64 else 0

def aR1 (form : Form) (raw : Slot) : BitVec 8 :=
  match form with
  | .cmp .. | .iPredicate .. | .wideStore | .fpStore .. | .fClass | .branch .. => 0
  | .iMisc .hint | .iMisc .check | .iMisc .rotatingPredicates | .iMisc .immediateAr => 0
  | .iMisc .toBranch | .iMisc .toPredicates | .iMisc .toAr => reg raw 13 7
  | .mSystem _ _ .toControl | .mSystem _ _ (.toStatus _)
  | .mSystem _ _ .toRegion | .mSystem _ _ .toIndexed => reg raw 13 7
  | .mSystem _ _ .fromStatus | .mSystem _ _ .fromControl | .mSystem _ _ .fromRegion
  | .mSystem _ _ .fromIndexed | .mSystem _ _ .invalidateEntry => reg raw 6 7
  | .mSystem _ _ .toDahr => reg raw 23 3
  | .mSystem .. | .prefetchCount => 0
  -- M6 likewise keeps the ignored raw field in the unused r1 slot.
  | .store .. => if raw.extractLsb' 37 4 = 4 then reg raw 6 7 else 0
  | _ => reg raw 6 7

def aPred (form : Form) (raw : Slot) (low : Nat) : BitVec 8 :=
  match form with
  | .cmp .. | .iPredicate .. | .fClass => reg raw low 6
  | .f1 .fma | .f1 .fms | .f1 .fnma | .f1 .fpma | .f1 .fpms | .f1 .fpnma
  | .f2 .. | .f3 => if low = 6 then reg raw 27 7 else 0
  | .f1 .. => if low = 6 then reg raw 27 6 else 0
  | .f4 => reg raw low 6
  | .fApprox parallel sqrt => if low == 27 || (!parallel && !sqrt) then reg raw 27 6 else 0
  | .f8 parallel .. | .f10 parallel _ =>
    if low = 6 then (if parallel then reg raw 34 2 else reg raw 27 6) else 0
  | .f11 => if low = 6 then reg raw 27 6 else 0
  | .fStatus .. => if low = 6 then reg raw 34 2 else 0
  -- QEMU's generic F-unit decoder writes these raw fields into p1.
  -- F9 operands and selection are independent of the unused field.
  | .f9 op _ => if low = 6 then (if op = 1 then reg raw 34 2 else reg raw 27 6) else 0
  | _ => 0

def aUnc (form : Form) (raw : Slot) : BitVec 1 :=
  match form with
  | .cmp .a6 _ _ _ none | .cmp .a8 _ _ _ none | .fClass | .f4 => raw.extractLsb' 12 1
  | .iPredicate .. => if iTestParallel raw then 0 else raw.extractLsb' 12 1
  | _ => 0

-- Decoder ABI codes for normal/unc, AND, OR and OR.ANDCM, in
-- Table 4-23's order of ta:tb predicate update selectors.
def aPredUpdate (form : Form) (raw : Slot) : BitVec 32 :=
  match form with
  | .iPredicate .. => (raw.extractLsb' 33 1 ++ raw.extractLsb' 36 1).zeroExtend 32
  | _ => 0

def aCheckFp (form : Form) (raw : Slot) : BitVec 1 :=
  match form with
  | .iMisc .check => if raw.extractLsb' 33 3 = 3 then 1 else 0
  | .mSystem _ _ .invalidateEntry => raw.extractLsb' 27 1
  | _ => 0

def aClearP2 (form : Form) : BitVec 1 :=
  match form with
  | .fApprox .. => 1
  | _ => 0

def aStatusField (form : Form) (raw : Slot) : BitVec 8 :=
  -- The shared F-unit decoder retains bits 35:34 for non-status operands too.
  match form with
  | .f1 .. | .f2 .. | .f3 | .f4 | .fApprox .. | .f8 .. | .f10 .. | .f11 | .fStatus ..
  | .f9 .. | .fpMergeAlias .. | .fClass => reg raw 34 2
  | _ => 0

-- Paired arithmetic keeps the major's precision code in its ABI slot.
def aFpPrecision (form : Form) (raw : Slot) : BitVec 8 :=
  match form with
  | .f1 .. =>
    if raw.extractLsb' 37 1 = 1 then 2 else (raw.extractLsb' 36 1).zeroExtend 8
  | _ => 0

def aQualifier (form : Form) (raw : Slot) : BitVec 8 :=
  match form with
  | .branch .predict => 0
  | _ => reg raw 0 6

def aBranchRegister (form : Form) (raw : Slot) (link : Bool) : BitVec 8 :=
  match form, link with
  | .branch (.call _), true => reg raw 6 3
  | .branch (.call false), false | .branch (.indirect ..), false => reg raw 13 3
  | _, _ => 0

def updateByRegister (form : Form) (raw : Slot) : BitVec 1 :=
  match form with
  | .load .. => if raw.extractLsb' 37 4 = 4 then raw.extractLsb' 36 1 else 0
  | .fpLoad .. => if raw.extractLsb' 37 4 = 6 then raw.extractLsb' 36 1 else 0
  | _ => 0

def updateByImmediate (form : Form) (raw : Slot) : BitVec 1 :=
  match form with
  | .load .. | .store .. => if raw.extractLsb' 37 4 = 5 then 1 else 0
  | .fpLoad .. | .fpStore .. => if raw.extractLsb' 37 4 = 7 then 1 else 0
  | .fpPair .. => raw.extractLsb' 36 1
  | _ => 0

def memAcquire (form : Form) (raw : Slot) : BitVec 1 :=
  let x6 := raw.extractLsb' 30 6
  match form with
  | .load _ _ acquire => if hasExtension x6 acquire then 1 else 0
  | .semaphore acquire _ _ => if acquire = x6 then 1 else 0
  | .wideLoad => if x6 = 44 then 1 else 0
  | _ => 0

def memRelease (form : Form) (raw : Slot) : BitVec 1 :=
  match form with
  | .store _ release => if release then 1 else 0
  | .semaphore _ release _ => if hasExtension (raw.extractLsb' 30 6) release then 1 else 0
  | .wideStore => if raw.extractLsb' 30 6 = 52 then 1 else 0
  | .mSystem _ _ (.translation true) => 1
  | _ => 0

def fpLoadAttributes (form : Form) (raw : Slot) : BitVec 4 :=
  let row := raw.extractLsb' 32 4
  match form with
  | .fpLoad .. | .fpPair .. =>
      (if row = 1 ∨ row = 3 then 1#1 else 0#1) ++
      (if row = 2 ∨ row = 3 then 1#1 else 0#1) ++
      (if row = 8 ∨ row = 9 then 1#1 else 0#1) ++
      (if row = 8 then 1#1 else 0#1)
  | _ => 0

-- Separate the fixed-width prefix and tail so consumers can normalize
-- immediate and flag fields without unfolding the symbolic prefix.
def aRecordTail (form : Form) (raw : Slot) : BitVec 194 :=
  match form with
  -- A1–A5 use only the ordinary register fields and immediate.
  | .a1 .. | .a2 .. | .a3 .. | .a4 .. | .a5 => 0#48 ++ aImm form raw ++ 2#82
  | _ =>
    aPred form raw 6 ++ aPred form raw 27 ++ aBranchRegister form raw true ++
      aBranchRegister form raw false ++ aStatusField form raw ++ aFpPrecision form raw ++ aImm form raw ++
      0#32 ++ aPredUpdate form raw ++ 0#1 ++ updateByRegister form raw ++ updateByImmediate form raw ++
      memAcquire form raw ++ memRelease form raw ++ aUnc form raw ++
      aClearP2 form ++ aCheckFp form raw ++ fpLoadAttributes form raw ++ 0#4 ++ 1#1 ++ 0#1

def aRecord (form : Form) (unit : UnitCode) (raw : Slot) (address : Word) (slot : BitVec 8) :
    Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ address ++ slot ++
    aQualifier form raw ++ aR1 form raw ++ aR2 form raw ++ aR3 form raw ++ aRecordTail form raw

def aOpcode (m i b f : UnitCode) (form : Form) (unit : UnitCode) (raw : Slot)
    (address : Word) (slot : BitVec 8) : Observation :=
  if formUnit m i b f form unit raw ∧ aEncoding form raw then aRecord form unit raw address slot else 0

def aBundle (reserved m i b f l x : UnitCode) (form : Form)
    (low high address : Word) (index : BitVec 2) : Observation :=
  let code := templateBits low
  if validTemplate code = true ∧ index < 3 then
    aOpcode m i b f form (templateUnit reserved m i b f l x code index)
      (slotBits low high index) address (index.zeroExtend 8)
  else 0

def aBundleEncoding (reserved m i b f l x : UnitCode) (form : Form)
    (low high : Word) (index : BitVec 2) : Prop :=
  let code := templateBits low
  validTemplate code = true ∧ index < 3 ∧
    formUnit m i b f form (templateUnit reserved m i b f l x code index) (slotBits low high index) ∧
      aEncoding form (slotBits low high index)

-- Type shared by the individually registered, implementation-bound proofs.
def BundleContract (reserved m i b f l x : UnitCode) (form : Form)
    (decode : Word → Word → Word → BitVec 2 → Observation) : Prop :=
  ∀ low high address index,
    decode low high address index = aBundle reserved m i b f l x form low high address index ∧
    ((decode low high address index).getLsbD 394 = true ↔
      aBundleEncoding reserved m i b f l x form low high index)

-- SDM Vol. 1 Figure 3-12/Table 3-8, and Vol. 3 operation clauses
-- for LD16, ST16, CMP8XCHG16. Predicate value is PR[qp] after lookup.
def aoUnsupported (cpuid4 : Word) (qualifies : Bool) : Bool :=
  qualifies && !cpuid4.getLsbD 2

def AoBundleContract (reserved m i b f l x : UnitCode) (form : Form)
    (decode : Word → Word → Word → BitVec 2 → Observation)
    (fault : Word → Word → Word → BitVec 2 → Word → Bool → Bool) : Prop :=
  BundleContract reserved m i b f l x form decode ∧
    ∀ low high address index cpuid4 qualifies,
      fault low high address index cpuid4 qualifies =
        ((aBundle reserved m i b f l x form low high address index).getLsbD 394 &&
          aoUnsupported cpuid4 qualifies)

-- SDM Vol. 1 Table 3-8 assigns CZ and X2, and Vol. 3 CLZ/MPY4
-- describe Illegal Operation for the corresponding absent feature.
inductive IntegerFeature where | cz | x2

def integerUnsupported (feature : IntegerFeature) (cpuid4 : Word) (qualifies : Bool) : Bool :=
  qualifies && !cpuid4.getLsbD (match feature with | .cz => 32 | .x2 => 33)

def IntegerFeatureBundleContract (reserved m i b f l x : UnitCode) (form : Form)
    (feature : IntegerFeature)
    (decode : Word → Word → Word → BitVec 2 → Observation)
    (fault : Word → Word → Word → BitVec 2 → Word → Bool → Bool) : Prop :=
  BundleContract reserved m i b f l x form decode ∧
  ∀ low high address index cpuid4 qualifies,
    fault low high address index cpuid4 qualifies =
      ((aBundle reserved m i b f l x form low high address index).getLsbD 394 &&
        integerUnsupported feature cpuid4 qualifies)

theorem aRecord_present (form : Form) (unit : UnitCode) (raw : Slot)
    (address : Word) (slot : BitVec 8) :
    (aRecord form unit raw address slot).getLsbD 394 = true := by
  dsimp only [aRecord]
  bv_decide

theorem aBundle_present (reserved m i b f l x : UnitCode) (form : Form)
    (low high address : Word) (index : BitVec 2) :
    (aBundle reserved m i b f l x form low high address index).getLsbD 394 = true ↔
      aBundleEncoding reserved m i b f l x form low high index := by
  dsimp only [aBundle, aBundleEncoding, aOpcode]
  split <;> rename_i outer
  · split <;> rename_i inner
    · rw [aRecord_present]
      exact ⟨fun _ => ⟨outer.1, outer.2, inner.1, inner.2⟩, fun _ => rfl⟩
    · constructor
      · intro impossible
        simp at impossible
      · intro encoded
        exact False.elim (inner ⟨encoded.2.2.1, encoded.2.2.2⟩)
  · constructor
    · intro impossible
      simp at impossible
    · intro encoded
      exact False.elim (outer ⟨encoded.1, encoded.2.1⟩)

end FullDecodeSpec
