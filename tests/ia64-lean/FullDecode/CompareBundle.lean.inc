/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace FullDecodeSpec

set_option maxHeartbeats 0

def compareRegForm (rel : Relation) (width : BitVec 1) : Form :=
  .cmp .a6 (registerAlias rel).major (width.zeroExtend 2) 0 none

def compareImmForm (rel : Relation) (width : BitVec 1) : Form :=
  .cmp .a8 (registerAlias rel).major (1#1 ++ width) 0 none

def compareRecord (unit : UnitCode) (raw : Slot) (ip : Word) (index : BitVec 2)
    (qp p1 p2 : BitVec 6) (r2 r3 : BitVec 7) (imm : Word) (unc : BitVec 1) : Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ ip ++ index.zeroExtend 8 ++
    qp.zeroExtend 8 ++ 0#8 ++ r2.zeroExtend 8 ++ r3.zeroExtend 8 ++
    (p1.zeroExtend 8 ++ p2.zeroExtend 8 ++ 0#8 ++ 0#8 ++ 0#8 ++ 0#8 ++ imm ++
      0#32 ++ 0#32 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ 0#1 ++ unc ++
      0#1 ++ 0#1 ++ 0#4 ++ 0#4 ++ 1#1 ++ 0#1)

abbrev CompareBundle := Word → Word → Word → BitVec 2 → Observation

def CompareAliasContract (reserved m i b f l x : UnitCode) (rel : Relation) (width : BitVec 1)
    (regDecode immDecode : CompareBundle)
    (hasAliasTag : UnitCode → Slot → Word → BitVec 8 → BitVec 5 → Slot → Slot → Bool) : Prop :=
  let unit := fun low index => templateUnit reserved m i b f l x (templateBits low) index
  let legal := fun low index => validTemplate (templateBits low) = true ∧ index < 3 ∧ aUnit m i (unit low index)
  (∀ u raw ip slot code lSlot xSlot, hasAliasTag u raw ip slot code lSlot xSlot = false) ∧
  (∀ low high ip index qp p1 p2 unc r2 r3,
    legal low index →
    slotBits low high index = registerWord rel width unc qp p1 p2 r2 r3 →
    let alias := registerAlias rel
    let (src2, src3) := swap alias.swapSources r2 r3
    let (dst1, dst2) := swap alias.swapTargets p1 p2
    regDecode low high ip index =
      compareRecord (unit low index) (registerWord rel width unc qp p1 p2 r2 r3)
        ip index qp dst1 dst2 src2 src3 0 unc) ∧
  (∀ low high ip index qp p1 p2 unc imm r3,
    legal low index →
    slotBits low high index = immediateWord rel width unc qp p1 p2 imm r3 →
    let (dst1, dst2) := swap (immediateAlias rel).2 p1 p2
    immDecode low high ip index =
      compareRecord (unit low index) (immediateWord rel width unc qp p1 p2 imm r3)
        ip index qp dst1 dst2 0 r3 (imm.signExtend 64) unc) ∧
  (∀ lhs rhs : Word,
    if width = 0 then
      registerResult rel lhs rhs = (relation rel lhs rhs, !relation rel lhs rhs)
    else
      let x := lhs.setWidth 32
      let y := rhs.setWidth 32
      registerResult rel x y = (relation rel x y, !relation rel x y)) ∧
  (∀ (imm : BitVec 8) (rhs : Word),
    if width = 0 then
      let base := imm.signExtend 64
      immediateResult rel base rhs = (immediateReference rel base rhs, !immediateReference rel base rhs)
    else
      let base := imm.signExtend 32
      let y := rhs.setWidth 32
      immediateResult rel base y = (immediateReference rel base y, !immediateReference rel base y))

private theorem compareRegRecord (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (r2 r3 : BitVec 7) (unit : UnitCode) (ip : Word) (index : BitVec 2) :
    let alias := registerAlias rel
    let (src2, src3) := swap alias.swapSources r2 r3
    let (dst1, dst2) := swap alias.swapTargets p1 p2
    let raw := registerWord rel width unc qp p1 p2 r2 r3
    aRecord (compareRegForm rel width) unit raw ip (index.zeroExtend 8) =
      compareRecord unit raw ip index qp dst1 dst2 src2 src3 0 unc := by
  have h := registerWordOperands rel width unc qp p1 p2 r2 r3
  dsimp only [compareRegForm, aRecord, aRecordTail, aQualifier, aR1, aBranchRegister,
    aStatusField, aFpPrecision, aImm, aPredUpdate, updateByRegister, updateByImmediate,
    memAcquire, memRelease, aClearP2, aCheckFp, fpLoadAttributes, compareRecord]
  obtain ⟨_, qpEq, r2Eq, r3Eq, p1Eq, p2Eq, uncEq⟩ := h
  rw [qpEq, r2Eq, r3Eq, p1Eq, p2Eq, uncEq]
  rfl

private theorem compareImmRecord (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (imm : BitVec 8) (r3 : BitVec 7) (unit : UnitCode) (ip : Word) (index : BitVec 2) :
    let (dst1, dst2) := swap (immediateAlias rel).2 p1 p2
    let raw := immediateWord rel width unc qp p1 p2 imm r3
    aRecord (compareImmForm rel width) unit raw ip (index.zeroExtend 8) =
      compareRecord unit raw ip index qp dst1 dst2 0 r3 (imm.signExtend 64) unc := by
  have h := immediateWordOperands rel width unc qp p1 p2 imm r3
  dsimp only [compareImmForm, aRecord, aRecordTail, aQualifier, aR1, aR2, aBranchRegister,
    aStatusField, aFpPrecision, aPredUpdate, updateByRegister, updateByImmediate,
    memAcquire, memRelease, aClearP2, aCheckFp, fpLoadAttributes, compareRecord]
  obtain ⟨_, qpEq, immEq, r3Eq, p1Eq, p2Eq, uncEq⟩ := h
  rw [qpEq, immEq, r3Eq, p1Eq, p2Eq, uncEq]
  rfl

theorem compareAliasOfCanonical (reserved m i b f l x : UnitCode) (rel : Relation) (width : BitVec 1)
    (regDecode immDecode : CompareBundle)
    (hasAliasTag : UnitCode → Slot → Word → BitVec 8 → BitVec 5 → Slot → Slot → Bool)
    (absent : ∀ u raw ip slot code lSlot xSlot, hasAliasTag u raw ip slot code lSlot xSlot = false)
    (regProof : BundleContract reserved m i b f l x (compareRegForm rel width) regDecode)
    (immProof : BundleContract reserved m i b f l x (compareImmForm rel width) immDecode) :
    CompareAliasContract reserved m i b f l x rel width regDecode immDecode hasAliasTag := by
  refine ⟨absent, ?_, ?_, ?_, ?_⟩
  · intro low high ip index qp p1 p2 unc r2 r3 legal raw
    rw [(regProof low high ip index).1]
    have enc := (registerWordOperands rel width unc qp p1 p2 r2 r3).1
    have valid : validTemplate (templateBits low) = true ∧ index < 3 := ⟨legal.1, legal.2.1⟩
    simp only [aBundle, if_pos valid, aOpcode, raw]
    have selected : formUnit m i b f (compareRegForm rel width)
        (templateUnit reserved m i b f l x (templateBits low) index)
        (registerWord rel width unc qp p1 p2 r2 r3) ∧
        aEncoding (compareRegForm rel width) (registerWord rel width unc qp p1 p2 r2 r3) :=
      ⟨legal.2.2, enc⟩
    rw [if_pos selected]
    exact compareRegRecord rel width unc qp p1 p2 r2 r3 _ ip index
  · intro low high ip index qp p1 p2 unc imm r3 legal raw
    rw [(immProof low high ip index).1]
    have enc := (immediateWordOperands rel width unc qp p1 p2 imm r3).1
    have valid : validTemplate (templateBits low) = true ∧ index < 3 := ⟨legal.1, legal.2.1⟩
    simp only [aBundle, if_pos valid, aOpcode, raw]
    have selected : formUnit m i b f (compareImmForm rel width)
        (templateUnit reserved m i b f l x (templateBits low) index)
        (immediateWord rel width unc qp p1 p2 imm r3) ∧
        aEncoding (compareImmForm rel width) (immediateWord rel width unc qp p1 p2 imm r3) :=
      ⟨legal.2.2, enc⟩
    rw [if_pos selected]
    exact compareImmRecord rel width unc qp p1 p2 imm r3 _ ip index
  · intro lhs rhs
    split
    · exact Prod.ext (registerAlias64 rel lhs rhs)
        ((registerOtherTarget rel lhs rhs).trans (congrArg not (registerAlias64 rel lhs rhs)))
    · exact Prod.ext (registerAlias32 rel (lhs.setWidth 32) (rhs.setWidth 32))
        ((registerOtherTarget rel _ _).trans (congrArg not (registerAlias32 rel _ _)))
  · intro imm rhs
    split
    · exact Prod.ext (immediateAlias64 rel imm rhs)
        ((immediateOtherTarget rel _ _).trans (congrArg not (immediateAlias64 rel imm rhs)))
    · exact Prod.ext (immediateAlias32 rel imm (rhs.setWidth 32))
        ((immediateOtherTarget rel _ _).trans (congrArg not (immediateAlias32 rel imm _)))

end FullDecodeSpec
