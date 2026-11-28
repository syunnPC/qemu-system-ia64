/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace FullDecodeSpec

set_option maxHeartbeats 0

inductive PredicateUpdateAlias where
  | tbitZero | tbitNonzero | tnatNonzero

def predicateUpdateForm : PredicateUpdateAlias → Form
  | .tbitZero => .iPredicate .i16 false
  | .tbitNonzero => .iPredicate .i16 true
  | .tnatNonzero => .iPredicate .i17 true

def predicateUpdateWord (op : PredicateUpdateAlias) (qp p1 p2 : BitVec 6) (src : BitVec 7)
    (position : BitVec 6) : Slot :=
  let (ta, field, y, c) := match op with
    | .tbitZero => (1#1, position, 0#1, 0#1)
    | .tbitNonzero => (1#1, position, 0#1, 1#1)
    | .tnatNonzero => (0#1, 0#1 ++ position.extractLsb' 0 5, 1#1, 1#1)
  5#4 ++ 1#1 ++ 0#2 ++ ta ++ p2 ++ src ++ field ++ y ++ c ++ p1 ++ qp

def predicateUpdateRecord (op : PredicateUpdateAlias) (i : UnitCode) (raw : Slot) (ip : Word)
    (index : BitVec 2) (qp p1 p2 : BitVec 6) (src : BitVec 7) (position : BitVec 6) : Observation :=
  let (imm, update) := match op with
    | .tnatNonzero => (0#64, 1#32)
    | _ => (position.zeroExtend 64, 3#32)
  1#1 ++ i.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    qp.zeroExtend 8 ++ 0#16 ++ src.zeroExtend 8 ++ p1.zeroExtend 8 ++ p2.zeroExtend 8 ++ 0#32 ++
    imm ++ 0#32 ++ update ++ 2#18

def predicateEffects (p1 p2 : BitVec 6) (v1 v2 : Bool) : Option Bool × Option Bool :=
  (if p1 = 0 then none else some v1, if p2 = 0 then none else some v2)

def interpretUpdate (op : PredicateUpdateAlias) (record : Observation)
    (qualified : Bool) (value : Word) (nat : Bool) : Option Bool × Option Bool :=
  let bit := (value >>> (record.extractLsb' 82 6).toNat).getLsbD 0
  let test := match op with
    | .tbitZero => !bit
    | .tbitNonzero => bit
    | .tnatNonzero => nat
  let mode := record.extractLsb' 18 32
  let p1 := record.extractLsb' 186 6
  let p2 := record.extractLsb' 178 6
  if qualified && (mode == 1) && !test then predicateEffects p1 p2 false false
  else if qualified && (mode == 3) && !nat && test then predicateEffects p1 p2 true false
  else (none, none)

def parallelTestSpec (op : PredicateUpdateAlias) (p1 p2 position : BitVec 6)
    (qualified : Bool) (value : Word) (nat : Bool) : Option Bool × Option Bool :=
  match op with
  | .tbitZero =>
      if qualified && !nat && !value.getLsbD position.toNat then predicateEffects p1 p2 true false
      else (none, none)
  | .tbitNonzero =>
      if qualified && !nat && value.getLsbD position.toNat then predicateEffects p1 p2 true false
      else (none, none)
  | .tnatNonzero =>
      if qualified && !nat then predicateEffects p1 p2 false false else (none, none)

def PredicateUpdateAliasContract (units : OrdinaryUnits) (i : UnitCode)
    (op : PredicateUpdateAlias) (decode : BundleDecode) (hasAliasTag : AliasTag) : Prop :=
  noAliasTag hasAliasTag ∧
  ∀ low high ip index qp p1 p2 src position,
    legalIUnit units i low index →
    slotBits low high index = predicateUpdateWord op qp p1 p2 src position →
    let record := decode low high ip index
    record = predicateUpdateRecord op i (predicateUpdateWord op qp p1 p2 src position)
        ip index qp p1 p2 src position ∧
    ∀ qualified value nat, p1 ≠ p2 →
      interpretUpdate op record qualified value nat = parallelTestSpec op p1 p2 position qualified value nat

private theorem predicateUpdateWordFields (op : PredicateUpdateAlias) (m i b f : UnitCode)
    (qp p1 p2 : BitVec 6) (src : BitVec 7) (position : BitVec 6) :
    let raw := predicateUpdateWord op qp p1 p2 src position
    let form := predicateUpdateForm op
    formUnit m i b f form i raw ∧ aEncoding form raw := by
  cases op <;> dsimp only [predicateUpdateWord, predicateUpdateForm, formUnit, aEncoding,
    iTestEncoding, iTestParallel] <;> bv_decide

private theorem predicateUpdateWordRecord (op : PredicateUpdateAlias) (i : UnitCode)
    (ip : Word) (index : BitVec 2) (qp p1 p2 : BitVec 6) (src : BitVec 7) (position : BitVec 6) :
    let raw := predicateUpdateWord op qp p1 p2 src position
    aRecord (predicateUpdateForm op) i raw ip (index.zeroExtend 8) =
      predicateUpdateRecord op i raw ip index qp p1 p2 src position := by
  cases op <;>
    dsimp only [predicateUpdateWord, predicateUpdateForm, predicateUpdateRecord, aRecord, aRecordTail,
      aQualifier, aR1, aR2, aR3, aPred, aBranchRegister, aStatusField, aFpPrecision, aImm,
      aPredUpdate, updateByRegister, updateByImmediate, memAcquire, memRelease, aUnc, iTestParallel,
      aClearP2, aCheckFp, fpLoadAttributes, reg] <;> bv_decide

private theorem canonicalParallelUpdate (op : PredicateUpdateAlias) (i : UnitCode)
    (raw : Slot) (ip : Word) (index : BitVec 2) (qp p1 p2 : BitVec 6) (src : BitVec 7) (pos : BitVec 6)
    (qualified : Bool) (value : Word) (nat : Bool) :
    interpretUpdate op (predicateUpdateRecord op i raw ip index qp p1 p2 src pos) qualified value nat =
      parallelTestSpec op p1 p2 pos qualified value nat := by
  have fields : let record := predicateUpdateRecord op i raw ip index qp p1 p2 src pos
      record.extractLsb' 186 6 = p1 ∧ record.extractLsb' 178 6 = p2 ∧
      (record.extractLsb' 18 32, record.extractLsb' 82 6) =
        match op with | .tnatNonzero => (1, 0) | _ => (3, pos) := by
    cases op <;> dsimp only [predicateUpdateRecord] <;> refine ⟨?_, ?_, Prod.ext ?_ ?_⟩ <;> bv_decide
  have mode := congrArg Prod.fst fields.2.2
  have imm := congrArg Prod.snd fields.2.2
  dsimp only at mode imm
  unfold interpretUpdate
  cases op <;>
    simp only [fields.1, fields.2.1, mode, imm, parallelTestSpec, BitVec.getLsbD_ushiftRight, Nat.add_zero] <;>
    cases qualified <;> cases nat <;> rfl

theorem predicateUpdateAliasOfCanonical (reserved m i b f l x : UnitCode)
    (op : PredicateUpdateAlias) (decode : BundleDecode) (hasAliasTag : AliasTag)
    (absent : noAliasTag hasAliasTag)
    (complete : BundleContract reserved m i b f l x (predicateUpdateForm op) decode) :
    PredicateUpdateAliasContract (templateUnit reserved m i b f l x) i op decode hasAliasTag := by
  refine ⟨absent, ?_⟩
  intro low high ip index qp p1 p2 src position legal atSlot
  have eq := decodeWrittenIWord reserved m i b f l x _ decode complete low high ip index _ legal atSlot
    (predicateUpdateWordFields op m i b f qp p1 p2 src position)
  rw [predicateUpdateWordRecord] at eq
  refine ⟨eq, ?_⟩
  intro qualified value nat _
  rw [eq]
  exact canonicalParallelUpdate op i _ ip index qp p1 p2 src position qualified value nat

end FullDecodeSpec
