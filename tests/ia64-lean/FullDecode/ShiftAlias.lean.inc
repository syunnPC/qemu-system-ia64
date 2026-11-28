/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace FullDecodeSpec

set_option maxHeartbeats 0

abbrev BundleDecode := Word → Word → Word → BitVec 2 → Observation
abbrev OrdinaryUnits := BitVec 5 → BitVec 2 → UnitCode
abbrev AliasTag := UnitCode → Slot → Word → BitVec 8 → BitVec 5 → Slot → Slot → Bool

def noAliasTag (selected : AliasTag) : Prop :=
  ∀ u raw ip slot code l x, selected u raw ip slot code l x = false

def legalIUnit (units : OrdinaryUnits) (i : UnitCode) (low : Word) (index : BitVec 2) : Prop :=
  validTemplate (templateBits low) = true ∧ index < 3 ∧ units (templateBits low) index = i

theorem decodeWrittenIWord (reserved m i b f l x : UnitCode) (form : Form)
    (decode : BundleDecode) (complete : BundleContract reserved m i b f l x form decode)
    (low high ip : Word) (index : BitVec 2) (raw : Slot)
    (legal : legalIUnit (templateUnit reserved m i b f l x) i low index)
    (atSlot : slotBits low high index = raw)
    (select : formUnit m i b f form i raw ∧ aEncoding form raw) :
    decode low high ip index = aRecord form i raw ip (index.zeroExtend 8) := by
  rw [(complete low high ip index).1]
  have admissible : validTemplate (templateBits low) = true ∧ index < 3 := ⟨legal.1, legal.2.1⟩
  simp only [aBundle, if_pos admissible, aOpcode, atSlot, legal.2.2, if_pos select]

inductive ShiftAlias where
  | shl | shr | shru

def shiftForm : ShiftAlias → Form
  | .shl => .iBitfield .i12
  | .shr => .iBitfield (.i11 true)
  | .shru => .iBitfield (.i11 false)

-- Shift pseudo-ops in the SDM map to I11 and I12 with len6 = 64 - count6.
def shiftWord (shift : ShiftAlias) (qp count : BitVec 6) (dst src : BitVec 7)
    (ignored : BitVec 1) : Slot :=
  let lenMinusOne := 63#6 - count
  match shift with
  | .shl => 5#4 ++ ignored ++ 1#2 ++ 1#1 ++ lenMinusOne ++ 0#1 ++ lenMinusOne ++ src ++ dst ++ qp
  | .shr => 5#4 ++ ignored ++ 1#2 ++ 0#1 ++ lenMinusOne ++ src ++ count ++ 1#1 ++ dst ++ qp
  | .shru => 5#4 ++ ignored ++ 1#2 ++ 0#1 ++ lenMinusOne ++ src ++ count ++ 0#1 ++ dst ++ qp

def shiftRecord (shift : ShiftAlias) (i : UnitCode) (raw : Slot) (ip : Word) (index : BitVec 2)
    (qp count : BitVec 6) (dst src : BitVec 7) : Observation :=
  let (r2, r3) := match shift with
    | .shl => (src.zeroExtend 8, 0#8)
    | _ => (0#8, src.zeroExtend 8)
  1#1 ++ i.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    qp.zeroExtend 8 ++ dst.zeroExtend 8 ++ r2 ++ r3 ++ 0#48 ++
    ((64#7 - count.zeroExtend 7) ++ count).zeroExtend 64 ++ 2#82

def shiftCountAndLength (record : Observation) : Nat × Nat :=
  ((record.extractLsb' 82 6).toNat, (record.extractLsb' 88 7).toNat)

def bitfieldOutput (shift : ShiftAlias) (src : Word) (pos len : Nat) (index : Fin 64) : Bool :=
  match shift with
  | .shl => if pos ≤ index.val ∧ index.val - pos < len then src.getLsbD (index.val - pos) else false
  | .shr => if index.val < len then src.getLsbD (pos + index.val) else src.getLsbD (pos + len - 1)
  | .shru => if index.val < len then src.getLsbD (pos + index.val) else false

def shiftValue (shift : ShiftAlias) (src : Word) (count : Nat) : Word :=
  match shift with
  | .shl => src <<< count
  | .shr => src.sshiftRight count
  | .shru => src >>> count

def ShiftAliasContract (units : OrdinaryUnits) (i : UnitCode) (shift : ShiftAlias)
    (decode : BundleDecode) (hasAliasTag : AliasTag) : Prop :=
  noAliasTag hasAliasTag ∧
  ∀ low high ip index qp count dst src ignored,
    legalIUnit units i low index →
    slotBits low high index = shiftWord shift qp count dst src ignored →
    let record := decode low high ip index
    record = shiftRecord shift i (shiftWord shift qp count dst src ignored) ip index qp count dst src ∧
    shiftCountAndLength record = (count.toNat, 64 - count.toNat) ∧
    ∀ value outputBit,
      bitfieldOutput shift value (shiftCountAndLength record).1 (shiftCountAndLength record).2 outputBit =
        (shiftValue shift value count.toNat).getLsbD outputBit.val

private theorem shiftWordFields (shift : ShiftAlias) (m i b f : UnitCode)
    (qp count : BitVec 6) (dst src : BitVec 7) (ignored : BitVec 1) :
    let raw := shiftWord shift qp count dst src ignored
    formUnit m i b f (shiftForm shift) i raw ∧ aEncoding (shiftForm shift) raw := by
  cases shift <;> dsimp only [shiftWord, shiftForm, formUnit, aEncoding, iBitfieldEncoding] <;> bv_decide

private theorem shiftWordRecord (shift : ShiftAlias) (i : UnitCode) (ip : Word) (index : BitVec 2)
    (qp count : BitVec 6) (dst src : BitVec 7) (ignored : BitVec 1) :
    let raw := shiftWord shift qp count dst src ignored
    aRecord (shiftForm shift) i raw ip (index.zeroExtend 8) =
      shiftRecord shift i raw ip index qp count dst src := by
  cases shift <;>
    dsimp only [shiftWord, shiftForm, aRecord, aRecordTail, aQualifier, aR1, aR2, aR3,
      aPred, aBranchRegister, aStatusField, aFpPrecision, aImm, bitfieldImm, packBitfield,
      aPredUpdate, updateByRegister, updateByImmediate, memAcquire, memRelease, aUnc,
      aClearP2, aCheckFp, fpLoadAttributes, reg, shiftRecord] <;> bv_decide

private theorem shiftRecordParameters (shift : ShiftAlias) (i : UnitCode) (raw : Slot) (ip : Word)
    (index : BitVec 2) (qp count : BitVec 6) (dst src : BitVec 7) :
    shiftCountAndLength (shiftRecord shift i raw ip index qp count dst src) =
      (count.toNat, 64 - count.toNat) := by
  have h : (64#7 - count.zeroExtend 7).toNat = 64 - count.toNat := by
    have bound := count.isLt
    simp only [BitVec.toNat_sub, BitVec.toNat_setWidth, BitVec.toNat_ofNat, Nat.reducePow, Nat.reduceMod]
    omega
  have fields : let record := shiftRecord shift i raw ip index qp count dst src
      record.extractLsb' 82 6 = count ∧ record.extractLsb' 88 7 = 64#7 - count.zeroExtend 7 := by
    cases shift <;> dsimp only [shiftRecord] <;> bv_decide
  simp only [shiftCountAndLength, fields.1, fields.2, h]

private theorem bitfieldIsShift (shift : ShiftAlias) (src : Word) (count : Nat) (index : Fin 64)
    (countBound : count < 64) :
    bitfieldOutput shift src count (64 - count) index =
      (shiftValue shift src count).getLsbD index.val := by
  have ⟨i, iBound⟩ := index
  cases shift
  · simp only [bitfieldOutput, shiftValue, BitVec.getLsbD_shiftLeft, iBound, decide_true, Bool.true_and]
    by_cases h : i < count
    · simp [h, show ¬ count ≤ i by omega]
    · simp [h, show count ≤ i by omega, show i - count < 64 - count by omega]
  · simp only [bitfieldOutput, shiftValue, BitVec.getLsbD_sshiftRight]
    by_cases h : i < 64 - count
    · simp [h, show count + i < 64 by omega, show ¬ 64 ≤ i by omega]
    · simp [h, show ¬ count + i < 64 by omega, show ¬ 64 ≤ i by omega,
        show count + (64 - count) - 1 = 63 by omega, BitVec.msb_eq_getLsbD_last]
  · simp only [bitfieldOutput, shiftValue, BitVec.getLsbD_ushiftRight]
    by_cases h : i < 64 - count
    · simp [h]
    · simp [h, BitVec.getLsbD_ge src (count + i) (by omega)]

theorem shiftAliasOfCanonical (reserved m i b f l x : UnitCode) (shift : ShiftAlias)
    (decode : BundleDecode) (hasAliasTag : AliasTag) (absent : noAliasTag hasAliasTag)
    (complete : BundleContract reserved m i b f l x (shiftForm shift) decode) :
    ShiftAliasContract (templateUnit reserved m i b f l x) i shift decode hasAliasTag := by
  refine ⟨absent, ?_⟩
  intro low high ip index qp count dst src ignored legal atSlot
  have eq := decodeWrittenIWord reserved m i b f l x _ decode complete low high ip index _ legal atSlot
    (shiftWordFields shift m i b f qp count dst src ignored)
  rw [shiftWordRecord] at eq
  have pair := shiftRecordParameters shift i (shiftWord shift qp count dst src ignored)
    ip index qp count dst src
  refine ⟨eq, eq ▸ pair, ?_⟩
  intro value outputBit
  rw [eq, pair]
  exact bitfieldIsShift shift value count.toNat outputBit count.isLt

end FullDecodeSpec
