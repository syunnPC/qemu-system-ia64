/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem allocRegister_matches (oldSize newSize destination reg : Nat) (pfs : Word)
    (old loaded : RegisterCell) (target : destination ≠ 0)
    (_inFrame : destination < 32 + newSize) :
    registerRefines (allocRegisterC oldSize newSize destination reg pfs old loaded)
      (allocRegister oldSize newSize destination reg pfs old) := by
  intro value required
  unfold allocRegister at required
  unfold allocRegisterC
  by_cases dest : destination = reg
  · rw [if_pos ⟨target, dest⟩]
    simpa [dest] using required
  · simp only [if_neg dest] at required
    simp only [show ¬ (destination ≠ 0 ∧ destination = reg) from fun h => dest h.2, if_false]
    split at required
    next retained =>
      have notLoaded : ¬ (newSize > oldSize ∧ 32 + oldSize ≤ reg ∧ reg < 32 + newSize) := by
        simp only [Nat.min_def] at retained
        split at retained <;> omega
      simpa [notLoaded] using required
    next => contradiction

theorem allocDirty_matches (oldSize newSize : Nat) (invalid : Int) (rrb : BitVec 7)
    (destination reg : Nat) (oldDirty : Bool) :
    (allocDirtyC oldSize newSize invalid rrb destination reg oldDirty = true) =
      (32 ≤ reg ∧ reg < 128 ∧ destination = reg ∨
        (oldSize ≤ newSize ∧ (newSize : Int) - oldSize ≤ invalid ∧ rrb = 0) ∧ oldDirty = true) := by
  simp only [allocDirtyC, Bool.or_eq_true, decide_eq_true_eq]
  have synchronized :
      (¬ ((newSize : Int) - oldSize < 0 ∨ invalid < (newSize : Int) - oldSize ∨ rrb ≠ 0)) =
        (oldSize ≤ newSize ∧ (newSize : Int) - oldSize ≤ invalid ∧ rrb = 0) := by
    by_cases h : rrb = 0 <;> simp [h] <;> omega
  rw [← synchronized]
  split <;> rename_i h
  · simp only [h, not_true_eq_false, Bool.false_eq_true, false_and, or_false]
  · simp only [h, not_false_eq_true, true_and]

theorem alatFrame_matches (isCover frLive : Bool) (oldRrb : BitVec 7) (entries : Nat)
    (old : Option AlatTag) :
    alatFrameRefines old (alatFrameC isCover frLive oldRrb entries old) := by
  unfold alatFrameRefines alatFrameC
  by_cases inactive : entries = 0
  · rw [if_pos inactive]
    exact Or.inr rfl
  · rw [if_neg inactive]
    cases old with
    | none => exact Or.inl rfl
    | some entry =>
      change (if _ then none else some entry) = none ∨
        (if _ then none else some entry) = some entry
      split <;> rename_i h
      · exact Or.inl rfl
      · exact Or.inr rfl

theorem coverSavedRegister_matches (sof sor rrb : BitVec 8) (bol v : U32)
    (dirty natFilesClear : Bool) (current : RegisterCell) (physical : U32 → RegisterCell)
    (base : bol < 96) (logical : v < 96) (_inFrame : v < sof.zeroExtend 32)
    (rename : rrb.zeroExtend 32 < rotateCount sof sor ∨ rotateCount sof sor = 0)
    (clean : dirty = false → physical (virtToPhys sof sor rrb bol v) = current)
    (tags : natFilesClear = true →
      (physical (virtToPhys sof sor rrb bol v)).2 = false ∧ current.2 = false) :
    coverSavedRegisterC sof sor rrb bol v dirty natFilesClear current physical =
      coverSavedRegister sof sor rrb bol v current := by
  simp only [coverSavedRegisterC, coverSavedRegister,
    virtToPhys_matches _ _ _ _ _ base logical rename]
  cases dirty <;> cases natFilesClear <;> simp [clean, tags]
  exact Prod.ext rfl (tags rfl).2.symm

theorem directCoverIndex_matches (bol v : Nat) (base : bol < 96) (logical : v < 96) :
    directCoverIndexC bol v = (bol + v) % 96 := by
  unfold directCoverIndexC
  split <;> dsimp only <;> split <;> omega

theorem allocRegisterAndDirty_matches (oldSize newSize : Nat) (invalid : Int)
    (rrb : BitVec 7) (destination reg : Nat) (pfs : Word) (old loaded : RegisterCell)
    (oldDirty : Bool) (target : destination ≠ 0) (inFrame : destination < 32 + newSize) :
    registerRefines (allocRegisterC oldSize newSize destination reg pfs old loaded)
      (allocRegister oldSize newSize destination reg pfs old) ∧
    ((allocDirtyC oldSize newSize invalid rrb destination reg oldDirty = true) ↔
      (32 ≤ reg ∧ reg < 128 ∧ destination = reg ∨
        (oldSize ≤ newSize ∧ (newSize : Int) - oldSize ≤ invalid ∧ rrb = 0) ∧ oldDirty = true)) :=
  ⟨allocRegister_matches _ _ _ _ _ _ _ target inFrame,
    Iff.of_eq (allocDirty_matches _ _ _ _ _ _ _)⟩
