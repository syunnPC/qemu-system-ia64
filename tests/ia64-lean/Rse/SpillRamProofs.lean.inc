/- SPDX-License-Identifier: GPL-2.0-or-later -/

private theorem collectionImage_matches (spill saved : RnatBits) (old : Word) :
    mergedCollectionC spill saved old = collectionImage spill saved old := by
  cases spill
  cases saved
  simp only [mergedCollectionC, collectionImage, fullRnatMaskC]
  split <;> bv_decide

private theorem updateRnat_matches (latch : RnatBits) (bit : U32) (tag : Bool) :
    updateRnatC latch bit tag = replaceRnatBit latch bit tag := by
  cases latch
  cases tag <;> simp only [updateRnatC, replaceRnatBit, Bool.false_eq_true, if_false, if_true,
    RnatBits.mk.injEq, BitVec.or_zero, and_true] <;> bv_decide

private theorem collectionSlot_aligned (addr : Word)
    (aligned : addr &&& 7 = 0) (last : collectBit addr = 63) :
    collectWord addr = addr := by
  simp only [collectBit, collectWord] at *
  bv_decide

private theorem spillCommit_matches (s : SpillCounters) (clean : Bool) (kind : SpillWord) :
    spillCommitC s clean kind = spillPrefix s clean [kind] := by
  cases clean <;> cases kind <;> simp [spillCommitC, spillPrefix, payloadCount]

private theorem spillBigEndian_matches (i : SpillRamInput) :
    spillBigEndianC i = i.bigEndian := by
  simp only [spillBigEndianC, SpillRamInput.bigEndian]
  bv_decide

theorem spillRamAttempt_matches (i : SpillRamInput) (s : SpillRamState)
    (aligned : s.counters.bspstore &&& 7 = 0) :
    spillRamAttemptC i s = spillRamAttempt i s := by
  simp only [spillRamAttemptC, spillRamAttempt, collectBit_matches, collectWord_matches, spillBigEndian_matches]
  by_cases denied : i.storeAllowed s.counters.bspstore = false
  · simp only [if_pos denied]
  · simp only [if_neg denied]
    by_cases last : collectBit s.counters.bspstore = 63
    · simp only [if_pos last, collectionSlot_aligned _ aligned last, collectionImage_matches,
        spillCommit_matches]
    · simp only [if_neg last, wrapPhys_matches, updateRnat_matches, spillCommit_matches]

private theorem spillRamAttempt_commit (i : SpillRamInput) (s next : SpillRamState)
    (k : SpillWord) (write : SpillWrite)
    (success : spillRamAttempt i s = .committed k write next) :
    write.address = s.counters.bspstore ∧ next.counters.bspstore = s.counters.bspstore + 8 ∧
    next.ram = spillWriteImage s.ram write := by
  simp only [spillRamAttempt] at success
  by_cases denied : i.storeAllowed s.counters.bspstore = false
  · simp only [if_pos denied] at success
    exact SpillAttempt.noConfusion success
  · simp only [if_neg denied] at success
    by_cases last : collectBit s.counters.bspstore = 63
    · simp only [if_pos last] at success
      cases success
      exact ⟨rfl, rfl, rfl⟩
    · simp only [if_neg last] at success
      generalize hp : i.physical (wrapPhys (i.bol - s.counters.dirty)) = slot at success
      cases slot
      cases success
      exact ⟨rfl, rfl, rfl⟩

private theorem spillRamAttempt_fault (i : SpillRamInput) (s : SpillRamState) (addr : Word)
    (failed : spillRamAttempt i s = .fault addr) : addr = s.counters.bspstore := by
  simp only [spillRamAttempt] at failed
  by_cases denied : i.storeAllowed s.counters.bspstore = false
  · simpa only [if_pos denied, SpillAttempt.fault.injEq] using failed.symm
  · simp only [if_neg denied] at failed
    by_cases last : collectBit s.counters.bspstore = 63
    · simp only [if_pos last] at failed
      exact SpillAttempt.noConfusion failed
    · simp only [if_neg last] at failed
      generalize hp : i.physical (wrapPhys (i.bol - s.counters.dirty)) = slot at failed
      cases slot
      exact SpillAttempt.noConfusion failed

private theorem spillRamAttempt_alignment (i : SpillRamInput) (s next : SpillRamState)
    (k : SpillWord) (write : SpillWrite)
    (aligned : s.counters.bspstore &&& 7 = 0)
    (success : spillRamAttempt i s = .committed k write next) :
    next.counters.bspstore &&& 7 = 0 := by
  rw [(spillRamAttempt_commit i s next k write success).2.1]
  generalize addrEq : s.counters.bspstore = addr at *
  bv_decide

theorem spillRamTrace_commits (i : SpillRamInput) (s : SpillRamState) (needed fuel : Nat) :
    let out := spillRamTrace i s needed fuel
    consecutiveSpillWrites s.counters.bspstore out.writes ∧
    out.state.ram = applySpillWrites s.ram out.writes ∧
    (∀ addr, out.stop = .fault addr → addr = out.state.counters.bspstore) := by
  induction fuel generalizing s needed with
  | zero =>
    by_cases done : needed = 0 <;>
      simp [spillRamTrace, done, retireSpillRam, consecutiveSpillWrites, applySpillWrites]
  | succ fuel ih =>
    simp only [spillRamTrace]
    by_cases done : needed = 0
    · simp [done, retireSpillRam, consecutiveSpillWrites, applySpillWrites]
    · simp only [if_neg done]
      cases attempt : spillRamAttempt i s with
      | fault addr =>
        simp [consecutiveSpillWrites, applySpillWrites, spillRamAttempt_fault i s addr attempt]
      | committed kind write next =>
        obtain ⟨addr, ptr, mem⟩ := spillRamAttempt_commit i s next kind write attempt
        by_cases interrupt : i.interruptAfter write.address = true
        · simp only [if_pos interrupt]
          simp [consecutiveSpillWrites, applySpillWrites, addr, mem]
        · simp only [if_neg interrupt, consecutiveSpillWrites, applySpillWrites, List.foldl_cons]
          obtain ⟨addresses, bytes, stop⟩ := ih next (remainingSpills needed kind)
          exact ⟨⟨addr, ptr ▸ addresses⟩, mem ▸ bytes, stop⟩

private theorem spillRemaining_matches (needed : Nat) (kind : SpillWord) :
    (match kind with | .payload => needed - 1 | .collection => needed) =
      remainingSpills needed kind := by cases kind <;> rfl

private theorem spillRamTrace_aligned (i : SpillRamInput) (s : SpillRamState)
    (needed fuel : Nat) (aligned : s.counters.bspstore &&& 7 = 0) :
    spillRamTraceC i s needed fuel = spillRamTrace i s needed fuel := by
  induction fuel generalizing s needed with
  | zero => rfl
  | succ fuel ih =>
    simp only [spillRamTraceC, spillRamTrace, spillRamAttempt_matches i s aligned]
    by_cases done : needed = 0
    · simp only [if_pos done]
    · simp only [if_neg done]
      cases attempt : spillRamAttempt i s with
      | fault addr => rfl
      | committed kind write next =>
        simp only [spillRemaining_matches]
        rw [ih next _ (spillRamAttempt_alignment i s next kind write aligned attempt)]

theorem spillRamTrace_matches (i : SpillRamInput) (s : SpillRamState) (needed fuel : Nat)
    (initial : spillRamStart s i needed) :
    spillRamTraceC i s needed fuel = spillRamTrace i s needed fuel :=
  spillRamTrace_aligned i s needed fuel initial.1

theorem allocMandatorySpill_matches (f : FrameState) (sof sol : BitVec 7) (sor : BitVec 4)
    (s : SpillRamState) (i : SpillRamInput) (h : SpillRamHistory) (fuel : Nat) (dst : BitVec 7)
    (qp : BitVec 6) (slot : BitVec 2) (startsGroup stopAfterSlot0 : Bool)
    (_site : allocSite qp slot startsGroup stopAfterSlot0)
    (_frame : f.complete) (_correspondence : s.describesFrame f i)
    (_resize : ({ f.cfm with sof := sof, sol := sol, sor := sor } : Frame).valid)
    (_target : dst ≠ 0 ∧ dst.toNat < 32 + sof.toNat)
    (_rotation : sor = f.cfm.sor ∨ f.cfm.gr = 0 ∧ f.cfm.fr = 0 ∧ f.cfm.pr = 0)
    (_ram : h.valid s i fuel)
    (setup : spillRamStart s i (mandatorySpillNeed f sof)) :
    let actual := spillRamTraceC i s (mandatorySpillNeedC f (packAllocC sof sol sor)) fuel
    actual = spillRamTrace i s (mandatorySpillNeed f sof) fuel ∧
      consecutiveSpillWrites s.counters.bspstore actual.writes ∧
      actual.state.ram = applySpillWrites s.ram actual.writes ∧
      (∀ addr, actual.stop = .fault addr → addr = actual.state.counters.bspstore) := by
  have needed : mandatorySpillNeedC f (packAllocC sof sol sor) = mandatorySpillNeed f sof := by
    simp only [mandatorySpillNeedC, allocFields_roundtrip, mandatorySpillNeed]
    congr 1 <;> omega
  simp only [needed, spillRamTrace_matches i s _ fuel setup, true_and]
  exact spillRamTrace_commits i s _ fuel
