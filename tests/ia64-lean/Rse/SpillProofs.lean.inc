/- SPDX-License-Identifier: GPL-2.0-or-later -/

private theorem spillPrefix_cons (state : SpillCounters) (hasClean : Bool)
    (kind : SpillWord) (tail : List SpillWord) :
    spillPrefix (spillCommitC state hasClean kind) hasClean tail =
      spillPrefix state hasClean (kind :: tail) := by
  cases hasClean <;> cases kind <;>
    simp [spillPrefix, spillCommitC, payloadCount, Nat.mul_succ, BitVec.ofNat_add] <;>
    constructor <;> first | bv_decide | omega

theorem spillFaultPrefix_matches (state : SpillCounters) (hasClean : Bool)
    (needed : Nat) (committed : List SpillWord) (more : List SpillEvent)
    (unfinished : payloadCount committed < needed) :
    spillRunC hasClean state needed
      (committed.map (fun kind => SpillEvent.store kind false) ++ .dataFault :: more) =
      .dataFault (spillPrefix state hasClean committed) := by
  induction committed generalizing state needed with
  | nil =>
    have nonzero : needed ≠ 0 := by simpa [payloadCount] using Nat.ne_zero_of_lt unfinished
    simp [spillRunC, nonzero, spillPrefix, payloadCount]
  | cons kind tail ih =>
    have nonzero : needed ≠ 0 := Nat.ne_zero_of_lt unfinished
    simp only [List.map_cons, List.cons_append, spillRunC, if_neg nonzero, Bool.false_eq_true]
    rw [← spillPrefix_cons state hasClean kind tail]
    cases kind <;> apply ih <;> simp [payloadCount] at unfinished ⊢ <;> omega

theorem spillInterruptPrefix_matches (state : SpillCounters) (hasClean : Bool)
    (needed : Nat) (committed : List SpillWord) (last : SpillWord) (more : List SpillEvent)
    (unfinished : payloadCount committed < needed) :
    spillRunC hasClean state needed
      (committed.map (fun kind => SpillEvent.store kind false) ++ .store last true :: more) =
      .interrupt (spillPrefix state hasClean (committed ++ [last])) := by
  induction committed generalizing state needed with
  | nil =>
    have nonzero : needed ≠ 0 := Nat.ne_zero_of_lt unfinished
    simp only [List.map_nil, List.nil_append, spillRunC, if_neg nonzero, if_true]
    rw [← spillPrefix_cons state hasClean last []]
    simp [spillPrefix, payloadCount]
  | cons kind tail ih =>
    have nonzero : needed ≠ 0 := Nat.ne_zero_of_lt unfinished
    simp only [List.map_cons, List.cons_append, spillRunC, if_neg nonzero, Bool.false_eq_true]
    rw [← spillPrefix_cons state hasClean kind (tail ++ [last])]
    cases kind <;> apply ih <;> simp [payloadCount] at unfinished ⊢ <;> omega

theorem spillRetirePrefix_matches (state : SpillCounters) (hasClean : Bool)
    (prior : List SpillWord) (more : List SpillEvent) :
    spillRunC hasClean state (payloadCount prior + 1)
      (prior.map (fun kind => SpillEvent.store kind false) ++ .store .payload false :: more) =
      .retire { spillPrefix state hasClean (prior ++ [.payload]) with
        invalid := 0, clean := 0, cleanNat := 0 } := by
  induction prior generalizing state with
  | nil =>
    cases more <;> simp [spillRunC, spillRetireC, spillPrefix, spillCommitC, payloadCount]
    rename_i head tail
    cases head <;> simp [spillRunC, spillRetireC, spillCommitC, spillPrefix, payloadCount]
  | cons kind rest ih =>
    simp only [List.map_cons, List.cons_append, Nat.add_eq_zero_iff, Nat.one_ne_zero,
      and_false, spillRunC, if_false, Bool.false_eq_true]
    rw [← spillPrefix_cons state hasClean kind (rest ++ [SpillWord.payload])]
    cases kind <;> simp only [payloadCount, Nat.add_sub_cancel] <;> exact ih _
