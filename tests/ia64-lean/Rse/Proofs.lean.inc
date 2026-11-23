/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem collectBit_matches (addr : Word) : collectBitC addr = collectBit addr := by
  simp only [collectBitC, collectBit] <;> bv_decide

theorem collectWord_matches (addr : Word) : collectWordC addr = collectWord addr := by
  simp only [collectWordC, collectWord] <;> bv_decide

theorem definedNaTBits_matches (index : U32) : definedNaTBitsC index = definedNaTBits index := by
  simp only [definedNaTBitsC, definedNaTBits] <;> bv_decide

theorem natCountSlow_matches (slots : U32) : natCountSlowC slots = natCount slots := by
  rfl

theorem natCount_matches (slots : U32) : natCountC slots = natCount slots := by
  unfold natCountC
  split
  · unfold natCount
    bv_decide
  · exact natCountSlow_matches slots

theorem natGrow_matches (addr : Word) (n : U32) : natGrowC addr n = natGrow addr n := by
  simp only [natGrowC, natGrow, collectBit_matches, natCount_matches]

theorem natShrink_matches (addr : Word) (n : U32) : natShrinkC addr n = natShrink addr n := by
  simp only [natShrinkC, natShrink, collectBit_matches, natCount_matches]

theorem payloadWordsBelow_matches (addr : Word) (words : U32) :
    payloadWordsBelowC addr words = payloadWordsBelow addr words := by
  simp only [payloadWordsBelowC, payloadWordsBelow, collectBit_matches, collectBit]
  bv_decide

theorem advance_matches (addr : Word) (n : U32)
    (aligned : addr &&& 7 = 0) (payload : collectBit addr < 63) (frame : n ≤ 96) :
    advanceC addr n = advance addr n := by
  simp only [advanceC, advance, natGrow_matches, natGrow, natCount, collectBit] at *
  bv_decide

theorem retreat_matches (addr : Word) (n : U32)
    (aligned : addr &&& 7 = 0) (payload : collectBit addr < 63) (frame : n ≤ 96) :
    retreatC addr n = retreat addr n := by
  simp only [retreatC, retreat, natShrink_matches, natShrink, natCount, collectBit] at *
  bv_decide

theorem frameMarker_matches (sof sol : BitVec 7) (sor : BitVec 4)
    (gr fr : BitVec 7) (pr : BitVec 6) :
    frameMarkerC sof sol sor gr fr pr = frameMarker sof sol sor gr fr pr := by
  simp only [frameMarkerC, frameMarker] <;> bv_decide

theorem rotateCount_matches (sof sor : BitVec 8) :
    rotateCountC sof sor = rotateCount sof sor := by
  simp only [rotateCountC, rotateCount] <;> bv_decide

theorem virtToPhys_matches (sof sor rrb : BitVec 8) (bol v : U32)
    (base : bol < 96) (logical : v < 96)
    (rename : rrb.zeroExtend 32 < rotateCount sof sor ∨ rotateCount sof sor = 0) :
    virtToPhysC sof sor rrb bol v = virtToPhys sof sor rrb bol v := by
  simp only [virtToPhysC, virtToPhys, rotateCount_matches, rotateCount] at *
  bv_decide

theorem physToVirt_matches (sof sor rrb : BitVec 8) (bol p : U32)
    (base : bol < 96) (physical : p < 96)
    (rename : rrb.zeroExtend 32 < rotateCount sof sor ∨ rotateCount sof sor = 0) :
    physToVirtC sof sor rrb bol p = physToVirt sof sor rrb bol p := by
  simp only [physToVirtC, physToVirt, rotateCount_matches, rotateCount] at *
  bv_decide

theorem wrapSlow_matches (idx : Int) : wrapSlowC idx = wrapPhys idx := by
  have h0 := Int.emod_nonneg idx (by decide : (96 : Int) ≠ 0)
  have h1 := Int.emod_lt_of_pos idx (by decide : (0 : Int) < 96)
  unfold wrapSlowC wrapPhys
  simp only [show (128 - 32 : Int) = 96 by decide, Int.tmod_eq_emod,
             Int.dvd_iff_emod_eq_zero]
  by_cases hc : 0 ≤ idx ∨ idx % 96 = 0
  · simp [hc, show ¬ idx % 96 < 0 by omega]
  · simp only [hc, if_false]
    have h2 : idx % 96 - 96 < 0 := by omega
    simp only [show ((96 : Int).natAbs : Int) = 96 by decide, h2, if_true]
    omega

theorem wrapPhys_matches (idx : Int) : wrapPhysC idx = wrapPhys idx := by
  unfold wrapPhysC
  simp only [show (128 - 32 : Int) = 96 by decide]
  split
  · unfold wrapPhys
    split
    · omega
    · split <;> omega
  · exact wrapSlow_matches idx

theorem rotatePrefix_matches (bits : BitVec 128) (n : U32) (low : 0 < n) (high : n ≤ 96) :
    rotatePrefixC bits n = rotatePrefix bits n := by
  simp only [rotatePrefixC, rotatePrefix]
  bv_decide

theorem wrapSlow32_matches (idx : BitVec 32) :
    wrapSlowC idx.toInt = wrapPhys idx.toInt := wrapSlow_matches idx.toInt

theorem wrapPhys32_matches (idx : BitVec 32) :
    wrapPhysC idx.toInt = wrapPhys idx.toInt := wrapPhys_matches idx.toInt
