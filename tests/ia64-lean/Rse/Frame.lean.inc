/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem allocFields_roundtrip (a b : BitVec 7) (c : BitVec 4) :
    allocFieldsC (packAllocC a b c) = (a, b, c) := by
  simp only [allocFieldsC, packAllocC, Prod.mk.injEq]
  bv_decide

theorem allocResident_matches (s : FrameState) (sof sol : BitVec 7) (sor : BitVec 4)
    (r : BitVec 7) (old : Word) (nat : Bool) (qp : BitVec 6) (slot : BitVec 2)
    (startsGroup stopAfterSlot0 : Bool)
    (_entry : allocSite qp slot startsGroup stopAfterSlot0)
    (_history : s.complete)
    (_frame : ({ s.cfm with sof := sof, sol := sol, sor := sor } : Frame).valid)
    (nonzero : r ≠ 0) (_target : r.toNat < 32 + sof.toNat)
    (_rotation : sor = s.cfm.sor ∨ s.cfm.gr = 0 ∧ s.cfm.fr = 0 ∧ s.cfm.pr = 0)
    (_resident : (sof.toNat : Int) - s.cfm.sof.toNat ≤ s.invalid + s.clean) :
    allocResidentC s (packAllocC sof sol sor) r old nat =
      (allocResident s sof sol sor, allocDestination s) := by
  simp only [allocResidentC, allocFields_roundtrip, allocResident, allocDestination,
    natShrink_matches, if_pos nonzero]
  let growth : Int := (sof.toNat : Int) - s.cfm.sof.toNat
  by_cases h : growth ≤ s.invalid
  · have used : max 0 (growth - s.invalid) = 0 := Int.max_eq_left (by omega)
    have remaining : max 0 (s.invalid - growth) = s.invalid - growth :=
      Int.max_eq_right (by omega)
    simp only [growth] at h used remaining
    simp [h, used, remaining]
  · have used : max 0 (growth - s.invalid) = growth - s.invalid :=
      Int.max_eq_right (by omega)
    have remaining : max 0 (s.invalid - growth) = 0 := Int.max_eq_left (by omega)
    have consumed : growth - s.invalid ≠ 0 := by omega
    simp only [growth] at h used remaining consumed
    simp [h, used, remaining, consumed]

private theorem ic_matches (psr : Word) :
    (psr &&& icMaskC = 0) = (¬ psr.getLsbD 13 = true) := by
  unfold icMaskC
  bv_decide

private theorem noGrowNat (bsp : Word) (payload : collectBit bsp < 63) :
    natGrow bsp 0 = 0 := by
  simp only [natGrow, natCount, BitVec.add_zero, collectBit] at *
  bv_decide

theorem coverFrame_matches (s : FrameState) (hasVirtualization endsGroup : Bool)
    (_site : coverSite s hasVirtualization endsGroup) (history : s.complete) :
    coverFrameC s = coverFrame s := by
  obtain ⟨frame, _, _, _, _, _, _, aligned, payload, bolLow, bolHigh⟩ := history
  let n : U32 := s.cfm.sof.zeroExtend 32
  have fit : n ≤ 96 := by
    have size := frame.1
    change n.toNat ≤ (96 : U32).toNat
    simpa [n, BitVec.toNat_setWidth, Nat.mod_eq_of_lt
      (show s.cfm.sof.toNat < 2 ^ 32 from by
        have := s.cfm.sof.isLt
        omega)] using size
  have ic : (s.psr &&& icMaskC = 0) = (¬ s.psr.getLsbD 13 = true) :=
    ic_matches s.psr
  have validBit : ifsValidC = (1 : Word) <<< 63 := by decide
  have advanceEq := advance_matches s.bsp n aligned payload fit
  have preserveNat (h : n = 0) : natGrow s.bsp n = 0 := by
    rw [h]
    exact noGrowNat s.bsp payload
  have sameBsp (h : n = 0) : advance s.bsp n = s.bsp := by
    rw [← advanceEq, advanceC, if_pos h]
  by_cases h : n = 0
  · have hn : n.toNat = 0 := by simp [h]
    have hw : wrapPhys s.bol = s.bol := by unfold wrapPhys; omega
    simp only [coverFrameC, coverFrame, show s.cfm.sof.zeroExtend 32 = n from rfl,
      frameMarker_matches, ic, validBit, ite_not, BitVec.or_comm, if_pos h, hn, Int.ofNat_zero,
      Int.add_zero, hw, preserveNat h, BitVec.add_zero, sameBsp h]
    congr 1
    exact (BitVec.add_zero s.dirtyNat).symm
  · simp only [coverFrameC, coverFrame, show s.cfm.sof.zeroExtend 32 = n from rfl,
      frameMarker_matches, ic, validBit, ite_not, BitVec.or_comm, if_neg h, advanceEq,
      wrapPhys_matches, natGrow_matches]
