/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.LinearAtomic

example (p : AtomicPrograms Cpu) (s : Schedule p) (admitted : Admitted p s)
    (wide narrow : Nat) :
    ¬((s ((16 : Word), 8))[wide]?.isSome = true ∧
      (s ((16 : Word), 4))[narrow]?.isSome = true) := by
  rintro ⟨first, second⟩
  have overlaps : ¬separate ((16 : Word), 8) ((16 : Word), 4) := by
    unfold separate
    decide
  exact overlaps (admitted.compatibleCells _ _ wide narrow first second)

private def interleavedExample : AtomicPrograms Bool :=
  { regs := fun _ => exampleRegs
    requests := fun cpu =>
      let old : Byte := if cpu then 16 else 1
      let cell := {exampleCell with guestBig := cpu, bytes := fun n => if n = 0 then old else 0}
      if cpu then
        [.semaphore ⟨0, 6, 4, .fetchAdd .w4 .plus1 true⟩ cell true]
      else
        [.semaphore ⟨0, 5, 4, .exchange .w4 4⟩ cell true]
    phys := fun _ a => a
    initial := [⟨16, [1, 0, 0, 0]⟩] }

private def initEvent : AtomicEventId interleavedExample := .initial ⟨0, by decide⟩
private def exchangeReadEvent : AtomicEventId interleavedExample := .access false ⟨0, by decide⟩
private def exchangeWriteEvent : AtomicEventId interleavedExample := .access false ⟨1, by decide⟩
private def addReadEvent : AtomicEventId interleavedExample := .access true ⟨0, by decide⟩
private def addWriteEvent : AtomicEventId interleavedExample := .access true ⟨1, by decide⟩
private def exampleLoc : Location := (16, 4)

private def histories : Schedule interleavedExample :=
  fun cell => if cell = exampleLoc then [
    .store initEvent, .atomic exchangeReadEvent exchangeWriteEvent, .atomic addReadEvent addWriteEvent
  ] else []

example : readsFrom interleavedExample histories exchangeWriteEvent addReadEvent ∧
    paired interleavedExample histories addReadEvent addWriteEvent := by
  exact ⟨⟨exampleLoc, 2, 1, rfl, rfl⟩, ⟨exampleLoc, 2, rfl⟩⟩

private theorem addReplaysExchange : readMatches interleavedExample addReadEvent exchangeWriteEvent := by
  refine ⟨⟨0, 6, 4, .fetchAdd .w4 .plus1 true⟩, exampleRegs,
    {exampleCell with guestBig := true, bytes := fun n => if n = 0 then 16 else 0},
    true, rfl, rfl, rfl, rfl, rfl, rfl⟩

example : ¬readMatches interleavedExample addReadEvent initEvent := by
  intro stale
  have unequal : (atomicEventLabel interleavedExample addReadEvent).value ≠
      (atomicEventLabel interleavedExample initEvent).value := by decide
  exact unequal (readMatchesValue _ _ _ stale)

-- The early read observed the initial value. The exchange wins before
-- its CAS attempt, so the first compare fails and the reloaded value
-- is used at the successful position. Both observations are checked
-- against the cell history above.
private theorem addChecksAttempts :
    checkedAttempts interleavedExample histories exampleLoc 2 addReadEvent addWriteEvent := by
  refine ⟨false, 1, [2], 1, [16, 16], rfl, by decide, rfl, rfl, ?_, rfl⟩
  intro size bound
  have casesOfSize : size = 0 ∨ size = 1 := by simp at bound <;> omega
  rcases casesOfSize with rfl | rfl <;> rfl

private theorem placements {cell : Location} {pos : Nat}
    {op : CellStep (AtomicEventId interleavedExample)}
    (placed : (histories cell)[pos]? = some op) :
    cell = exampleLoc ∧
      ((pos = 0 ∧ op = .store initEvent) ∨
       (pos = 1 ∧ op = .atomic exchangeReadEvent exchangeWriteEvent) ∨
       (pos = 2 ∧ op = .atomic addReadEvent addWriteEvent)) := by
  have here : cell = exampleLoc := by
    by_cases same : cell = exampleLoc
    · exact same
    · simp [histories, same] at placed
  subst cell
  have bound : pos < 3 := by
    exact (List.getElem?_eq_some_iff.mp placed).1
  have choice : pos = 0 ∨ pos = 1 ∨ pos = 2 := by omega
  rcases choice with rfl | rfl | rfl
  · exact ⟨rfl, Or.inl ⟨rfl, (Option.some.inj placed).symm⟩⟩
  · exact ⟨rfl, Or.inr (Or.inl ⟨rfl, (Option.some.inj placed).symm⟩)⟩
  · exact ⟨rfl, Or.inr (Or.inr ⟨rfl, (Option.some.inj placed).symm⟩)⟩

private theorem onlyCell {cell : Location} {pos : Nat}
    (found : (histories cell)[pos]?.isSome = true) : cell = exampleLoc := by
  cases h : (histories cell)[pos]? with
  | none => simp [h] at found
  | some _ => exact (placements h).1

private theorem readPositions {cell : Location} {pos : Nat}
    {e : AtomicEventId interleavedExample} (found : readAt (histories cell) pos = some e) :
    cell = exampleLoc ∧ ((pos = 1 ∧ e = exchangeReadEvent) ∨ (pos = 2 ∧ e = addReadEvent)) := by
  cases observed : (histories cell)[pos]? with
  | none => simp [readAt, observed] at found
  | some op =>
    rcases placements observed with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · simp [readAt, observed, CellStep.read] at found
    · exact ⟨rfl, Or.inl ⟨rfl, (Option.some.inj found).symm⟩⟩
    · exact ⟨rfl, Or.inr ⟨rfl, (Option.some.inj found).symm⟩⟩

private theorem writePositions {cell : Location} {pos : Nat}
    {e : AtomicEventId interleavedExample} (found : writeAt (histories cell) pos = some e) :
    cell = exampleLoc ∧ ((pos = 0 ∧ e = initEvent) ∨
      (pos = 1 ∧ e = exchangeWriteEvent) ∨ (pos = 2 ∧ e = addWriteEvent)) := by
  cases observed : (histories cell)[pos]? with
  | none => simp [writeAt, observed] at found
  | some op =>
    rcases placements observed with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · exact ⟨rfl, Or.inl ⟨rfl, (Option.some.inj found).symm⟩⟩
    · exact ⟨rfl, Or.inr (Or.inl ⟨rfl, (Option.some.inj found).symm⟩)⟩
    · exact ⟨rfl, Or.inr (Or.inr ⟨rfl, (Option.some.inj found).symm⟩)⟩

private theorem exampleEvents (e : AtomicEventId interleavedExample) :
    e = initEvent ∨ e = exchangeReadEvent ∨ e = exchangeWriteEvent ∨
      e = addReadEvent ∨ e = addWriteEvent := by
  cases e with
  | initial i =>
    have one : i.val < 1 := i.isLt
    have only : i = ⟨0, by decide⟩ := Fin.eq_of_val_eq (show i.val = 0 by omega)
    cases only
    exact Or.inl rfl
  | access cpu i =>
    cases cpu
    · have two : i.val < 2 := i.isLt
      have position : i.val = 0 ∨ i.val = 1 := by omega
      rcases position with zero | one
      · have same : i = ⟨0, by decide⟩ := Fin.eq_of_val_eq zero
        cases same
        exact Or.inr (Or.inl rfl)
      · have same : i = ⟨1, by decide⟩ := Fin.eq_of_val_eq one
        cases same
        exact Or.inr (Or.inr (Or.inl rfl))
    · have two : i.val < 2 := i.isLt
      have position : i.val = 0 ∨ i.val = 1 := by omega
      rcases position with zero | one
      · have same : i = ⟨0, by decide⟩ := Fin.eq_of_val_eq zero
        cases same
        exact Or.inr (Or.inr (Or.inr (Or.inl rfl)))
      · have same : i = ⟨1, by decide⟩ := Fin.eq_of_val_eq one
        cases same
        exact Or.inr (Or.inr (Or.inr (Or.inr rfl)))

private theorem exampleAdmitted : Admitted interleavedExample histories := by
  constructor
  · intro cell pos present
    rw [onlyCell present]
    unfold aligned
    decide
  · intro a b i j present present'
    exact Or.inl ((onlyCell present).trans (onlyCell present').symm)
  · intro _ _ _ h
    rcases placements h with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · exact ⟨rfl, rfl⟩
    · exact ⟨⟨⟨false, 0, 1, rfl, rfl, by decide⟩, rfl, rfl, rfl, rfl⟩,
        rfl, rfl, rfl, rfl, Or.inl ⟨.exchange .w4 4, rfl⟩⟩
    · exact ⟨⟨⟨true, 0, 1, rfl, rfl, by decide⟩, rfl, rfl, rfl, rfl⟩,
        rfl, rfl, rfl, rfl, Or.inl ⟨.fetchAdd .w4 .plus1 true, rfl⟩⟩
  · intro _ _ _ _ step
    rcases placements step with ⟨rfl, ⟨rfl, no⟩ | ⟨rfl, yes⟩ | ⟨rfl, yes⟩⟩
    · cases no
    · cases yes
      trivial
    · cases yes
      exact addChecksAttempts
  · intro _ _ _ _ _ first second
    rcases readPositions first with ⟨rfl, ⟨rfl, left⟩ | ⟨rfl, left⟩⟩ <;>
      rcases readPositions second with ⟨rfl, ⟨rfl, right⟩ | ⟨rfl, right⟩⟩ <;>
      first | exact ⟨rfl, rfl⟩ | cases (left.symm.trans right)
  · intro _ _ _ _ _ first second
    rcases writePositions first with ⟨rfl, ⟨rfl, left⟩ | ⟨rfl, left⟩ | ⟨rfl, left⟩⟩ <;>
      rcases writePositions second with ⟨rfl, ⟨rfl, right⟩ | ⟨rfl, right⟩ | ⟨rfl, right⟩⟩ <;>
      first | exact ⟨rfl, rfl⟩ | cases (left.symm.trans right)
  · intro e read
    rcases exampleEvents e with rfl | rfl | rfl | rfl | rfl
    · cases read
    · exact ⟨exampleLoc, 1, rfl⟩
    · cases read
    · exact ⟨exampleLoc, 2, rfl⟩
    · cases read
  · intro e write
    rcases exampleEvents e with rfl | rfl | rfl | rfl | rfl
    · exact ⟨exampleLoc, 0, rfl⟩
    · cases write
    · exact ⟨exampleLoc, 1, rfl⟩
    · cases write
    · exact ⟨exampleLoc, 2, rfl⟩
  · intro _ _ _ observed
    rcases readPositions observed with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · refine ⟨0, initEvent, rfl, ?_⟩
      exact ⟨⟨0, 5, 4, .exchange .w4 4⟩, exampleRegs,
        {exampleCell with guestBig := false, bytes := fun n => if n = 0 then 1 else 0},
        true, rfl, rfl, rfl, rfl, rfl, rfl⟩
    · exact ⟨1, exchangeWriteEvent, rfl, addReplaysExchange⟩

example : TraceOrder.AtomicPairs (graph interleavedExample histories (fun _ _ => False)) :=
  (linearAtomicContract interleavedExample histories exampleAdmitted).1 (fun _ _ => False)

end TcgMemory.LinearAtomic
