/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.ByteAtomic

open LinearAtomic (CellStep readAt writeAt stateBefore payload replayReadOn replayReadBytes
  admissibleStep)

private def original : List Byte := [1, 0, 0, 0, 0, 0, 0, 0]
private def assembled : List Byte := [1, 0, 0, 0, 0xdd, 0xcc, 0xbb, 0xaa]

private def sampleRegs (narrow : Bool) : Registers :=
  {exampleRegs with gr := fun r =>
    if r = 4 then (if narrow then 0x1014 else 16)
    else if narrow then 0xaabbccdd else 0x1122334455667788}

private def exchange : AtomicInstruction := ⟨0, 5, 4, .exchange .w8 6⟩
private def store : Store := ⟨0, 6, 4, .w4, false, none⟩

private def exchangeCell : CellInput :=
  {exampleCell with guestBig := false, bytes := fun n => assembled[n]?.getD 0}

private def program : AtomicPrograms Bool :=
  ⟨sampleRegs,
    fun narrow =>
      if narrow then [.scalar ⟨.store store, {exampleCell with guestBig := false}⟩]
      else [.semaphore exchange exchangeCell true],
    fun narrow a => if narrow then a - 0x1000 else a,
    [⟨16, original⟩]⟩

private def init : AtomicEventId program := .initial ⟨0, by decide⟩
private def patch : AtomicEventId program := .access true ⟨0, by decide⟩
private def readWide : AtomicEventId program := .access false ⟨0, by decide⟩
private def writeWide : AtomicEventId program := .access false ⟨1, by decide⟩

private def schedule : BlockSchedule program :=
  fun block => if block = 2 then [.store init, .store patch, .atomic readWide writeWide] else []

private theorem placement {block position : Nat} {op : CellStep (AtomicEventId program)}
    (found : (schedule block)[position]? = some op) :
    block = 2 ∧
      ((position = 0 ∧ op = .store init) ∨ (position = 1 ∧ op = .store patch) ∨
        (position = 2 ∧ op = .atomic readWide writeWide)) := by
  have here : block = 2 := by
    by_cases same : block = 2
    · exact same
    · simp [schedule, same] at found
  subst block
  have bound : position < 3 := (List.getElem?_eq_some_iff.mp found).1
  have choice : position = 0 ∨ position = 1 ∨ position = 2 := by omega
  rcases choice with rfl | rfl | rfl
  · exact ⟨rfl, Or.inl ⟨rfl, (Option.some.inj found).symm⟩⟩
  · exact ⟨rfl, Or.inr (Or.inl ⟨rfl, (Option.some.inj found).symm⟩)⟩
  · exact ⟨rfl, Or.inr (Or.inr ⟨rfl, (Option.some.inj found).symm⟩)⟩

private theorem examplePositions {e : AtomicEventId program} {whereAt : Nat × Nat}
    (found : occursAt program schedule e whereAt) :
    (whereAt = (2, 0) ∧ e = init) ∨ (whereAt = (2, 1) ∧ e = patch) ∨
      (whereAt = (2, 2) ∧ (e = readWide ∨ e = writeWide)) := by
  rcases whereAt with ⟨block, pos⟩
  cases present : (schedule block)[pos]? with
  | none => simp [occursAt, readAt, writeAt, present] at found
  | some op =>
    rcases placement present with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩ <;>
      simp [occursAt, readAt, writeAt, present, CellStep.read, CellStep.write] at found
    · exact Or.inl ⟨rfl, found.symm⟩
    · exact Or.inr (Or.inl ⟨rfl, found.symm⟩)
    · exact Or.inr (Or.inr ⟨rfl, found.imp Eq.symm Eq.symm⟩)

private theorem oneInitial (i : Fin program.initial.length) : i = ⟨0, by decide⟩ := by
  have bound : i.val < 1 := i.isLt
  exact Fin.eq_of_val_eq (show i.val = 0 by omega)

private theorem exampleEvents (e : AtomicEventId program) :
    e = init ∨ e = patch ∨ e = readWide ∨ e = writeWide := by
  cases e with
  | initial i => rw [oneInitial i]; exact Or.inl rfl
  | access cpu i =>
    cases cpu
    · have bound : i.val < 2 := i.isLt
      have choice : i.val = 0 ∨ i.val = 1 := by omega
      rcases choice with h | h
      · rw [show i = ⟨0, by decide⟩ from Fin.eq_of_val_eq h]
        exact Or.inr (Or.inr (Or.inl rfl))
      · rw [show i = ⟨1, by decide⟩ from Fin.eq_of_val_eq h]
        exact Or.inr (Or.inr (Or.inr rfl))
    · have bound : i.val < 1 := i.isLt
      rw [show i = ⟨0, by decide⟩ from Fin.eq_of_val_eq (show i.val = 0 by omega)]
      exact Or.inr (Or.inl rfl)

private theorem uniqueOccurrence {e : AtomicEventId program} {a b : Nat × Nat}
    (first : occursAt program schedule e a) (second : occursAt program schedule e b) : a = b := by
  rcases examplePositions first with ⟨rfl, h⟩ | ⟨rfl, h⟩ | ⟨rfl, h | h⟩ <;>
    rcases examplePositions second with ⟨rfl, k⟩ | ⟨rfl, k⟩ | ⟨rfl, k | k⟩ <;>
    first | rfl | cases (h.symm.trans k)

private theorem exampleAdmitted : Admitted program schedule := by
  constructor
  · intro _ _ _ chosen
    rcases placement chosen with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · exact ⟨(16, 8), by unfold aligned; decide, rfl, rfl, rfl⟩
    · exact ⟨(20, 4), by unfold aligned; decide, rfl, rfl, rfl⟩
    · refine ⟨(16, 8), by unfold aligned; decide, rfl, ?_, rfl, rfl, rfl, rfl, Or.inl ⟨exchange.op, rfl⟩⟩
      exact ⟨⟨false, 0, 1, rfl, rfl, by decide⟩, rfl, rfl, rfl, rfl⟩
  · intro e
    have existsAt : ∃ pos, occursAt program schedule e pos := by
      rcases exampleEvents e with rfl | rfl | rfl | rfl
      · exact ⟨(2, 0), Or.inr rfl⟩
      · exact ⟨(2, 1), Or.inr rfl⟩
      · exact ⟨(2, 2), Or.inl rfl⟩
      · exact ⟨(2, 2), Or.inr rfl⟩
    obtain ⟨pos, found⟩ := existsAt
    exact ⟨pos, found, fun _ h => uniqueOccurrence h found⟩
  · intro i cpu access block earlier later initWrite accessAt
    rcases examplePositions (Or.inr initWrite : occursAt program schedule (.initial i)
        (block, earlier)) with ⟨place, _⟩ | ⟨_, bad⟩ | ⟨_, bad | bad⟩
    · rcases examplePositions accessAt with ⟨_, bad⟩ | ⟨found, _⟩ | ⟨found, _⟩
      · cases bad
      · have h := congrArg Prod.snd place
        have k := congrArg Prod.snd found
        simp only at h k
        omega
      · have h := congrArg Prod.snd place
        have k := congrArg Prod.snd found
        simp only at h k
        omega
    · cases bad
    · cases bad
    · cases bad
  · intro i j _ a b left right
    rw [oneInitial i] at left
    rw [oneInitial j] at right
    exact Option.some.inj (left.symm.trans right)
  · intro block pos e observed
    cases present : (schedule block)[pos]? with
    | none => simp [readAt, present] at observed
    | some op =>
      rcases placement present with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
      · simp [readAt, present, CellStep.read] at observed
      · simp [readAt, present, CellStep.read] at observed
      · have same : readWide = e := Option.some.inj observed
        subst e
        exact ⟨(16, 8), assembled, rfl, rfl,
          exchange, sampleRegs false, exchangeCell, true, rfl, rfl, rfl, rfl, rfl, rfl⟩
  · intro _ _ _ _ paired
    rcases placement paired with ⟨rfl, ⟨rfl, bad⟩ | ⟨rfl, bad⟩ | ⟨rfl, yes⟩⟩
    · cases bad
    · cases bad
    · cases yes
      exact ⟨(16, 8), rfl, True.intro⟩

-- CPU 1's narrow store supplies the high bytes of CPU 0's old
-- exchange value. CPU 0's single successful exchange then writes
-- both halves at the same block position.
example :
    (atomicTrace program false).regs.gr 5 = 0xaabbccdd00000001 ∧
    stateBefore (writeAtByte program schedule 2 16) 2 = some (0, init) ∧
    stateBefore (writeAtByte program schedule 2 20) 2 = some (1, patch) ∧
    stateBefore (writeAtByte program schedule 2 16) 3 = some (2, writeWide) ∧
    stateBefore (writeAtByte program schedule 2 20) 3 = some (2, writeWide) :=
  ⟨rfl, rfl, rfl, rfl, rfl⟩

example : ∃ cell, aligned cell ∧ cell.1.toNat / 8 = 2 ∧
    ∀ offset, offset < cell.2 →
      ∃ previous writer value,
        let physical := cell.1.toNat + offset
        physical / 8 = 2 ∧
        (payload program readWide)[offset]? = some value ∧
          payloadByte program writer physical = some value ∧
          previous < 2 ∧ writeAtByte program schedule 2 physical previous = some writer ∧
          (∀ later contender, later < 2 →
            writeAtByte program schedule 2 physical later = some contender → later ≤ previous) ∧
          stateBefore (writeAtByte program schedule 2 physical) 3 = some (2, writeWide) :=
  atomicBytes program schedule exampleAdmitted 2 2 readWide writeWide rfl

example : ¬replayReadOn program readWide original := by
  intro stale
  have wrong : payload program readWide ≠ original := by decide
  exact wrong (replayReadBytes program readWide original stale)

private def repeatedStore : BlockSchedule program :=
  fun block =>
    if block = 2 then [.store init, .store patch, .store patch, .atomic readWide writeWide]
    else []

example : ¬Admitted program repeatedStore := by
  intro accepted
  obtain ⟨_, _, unique⟩ := accepted.eventPositions patch
  have first := unique (2, 1) (Or.inr rfl)
  have second := unique (2, 2) (Or.inr rfl)
  have impossible : ((2, 1) : Nat × Nat) = (2, 2) := first.trans second.symm
  cases impossible

end TcgMemory.ByteAtomic
