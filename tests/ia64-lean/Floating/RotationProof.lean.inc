/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem fpRebaseAddress (index old current : BitVec 7)
    (oldOK : old < 96) (newOK : current < 96) :
    physicalFr (cRebaseIndex index old current) old = physicalFr index current := by
  dsimp only [cRebaseIndex, physicalFr]
  bv_decide

theorem fpRebaseRead {α : Type} (physical : BitVec 7 → α) (index old current : BitVec 7)
    (oldOK : old < 96) (newOK : current < 96) :
    physical (physicalFr (cRebaseIndex index old current) old) =
      physical (physicalFr index current) :=
  congrArg physical (fpRebaseAddress index old current oldOK newOK)

theorem fpRebaseTagBit (tags : BitVec 128) (index shift : BitVec 7) (valid : shift < 96) :
    ((cRebaseTags tags shift) >>> (index.zeroExtend 128)).getLsbD 0 =
      (tags >>> ((physicalFr index shift).zeroExtend 128)).getLsbD 0 := by
  dsimp only [cRebaseTags, physicalFr]
  bv_decide
