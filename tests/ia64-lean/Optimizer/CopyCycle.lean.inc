/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

private theorem boundedNodup (xs : List Nat) (n : Nat)
    (distinct : xs.Nodup) (bound : ∀ x ∈ xs, x < n) : xs.length ≤ n := by
  induction n generalizing xs with
  | zero =>
    cases xs with
    | nil => simp
    | cons x xs => have impossible := bound x (by simp); omega
  | succ n ih =>
    by_cases found : n ∈ xs
    · have smaller := ih (xs.erase n) (distinct.erase n) (by
        intro x hx
        have membership := distinct.mem_erase_iff.mp hx
        have limit := bound x membership.2
        have ne := membership.1
        omega)
      have decreased := List.length_erase_of_mem found
      omega
    · have smaller := ih xs distinct (by
        intro x hx
        have limit := bound x hx
        have ne : x ≠ n := fun eq => found (eq ▸ hx)
        omega)
      omega

private theorem collisionMakesCycle (m : Metadata Temp) (valid : m.valid) (origin : Temp)
    (first second : Nat) (order : first < second)
    (collision : follow m origin first = follow m origin second) :
    ∃ hops, 0 < hops ∧ hops ≤ second ∧ follow m origin hops = origin := by
  induction first generalizing second with
  | zero => exact ⟨second, order, Nat.le_refl _, collision.symm⟩
  | succ first ih =>
    cases second with
    | zero => omega
    | succ second =>
      have beforeCollision : follow m origin first = follow m origin second := by
        have both := congrArg m.prev collision
        simpa only [follow, valid.1] using both
      obtain ⟨hops, pos, bound, returns⟩ := ih second (by omega) beforeCollision
      exact ⟨hops, pos, by omega, returns⟩

theorem finiteCopyCycle (m : Metadata (Fin count)) (valid : m.valid) (origin : Fin count) :
    ∃ hops, 0 < hops ∧ hops ≤ count ∧ follow m origin hops = origin := by
  apply Classical.byContradiction
  intro absent
  let visit (i : Nat) := (follow m origin i).val
  let candidates := List.range (count + 1)
  have ordered : candidates.Pairwise (· < ·) := List.pairwise_lt_range
  have noCollision : (candidates.map visit).Nodup := by
    apply List.pairwise_map.mpr
    apply ordered.imp_of_mem
    intro i j _ membership less collision
    have bound : j < count + 1 := List.mem_range.mp (show j ∈ List.range (count + 1) from membership)
    have eq : follow m origin i = follow m origin j := Fin.ext collision
    obtain ⟨hops, pos, fit, echo⟩ := collisionMakesCycle m valid origin i j less eq
    exact absent ⟨hops, pos, by omega, echo⟩
  have impossible := boundedNodup (candidates.map visit) count noCollision (by
    intro x h
    obtain ⟨i, _, rfl⟩ := List.mem_map.mp h
    exact (follow m origin i).isLt)
  have impossible' : count + 1 ≤ count := by simpa [candidates] using impossible
  omega

theorem returnsBeforeExhaustion [DecidableEq Temp] (m : Metadata Temp)
    (start target current : Temp) (hops fuel : Nat) (enough : hops < fuel)
    (cycle : follow m current hops = start) :
    ∃ answer, scanTail m start target current fuel = some answer := by
  induction hops generalizing current fuel with
  | zero =>
    have eq : current = start := cycle
    cases fuel with
    | zero => omega
    | succ rest => exact ⟨false, by simp [scanTail, eq]⟩
  | succ hops ih =>
    cases fuel with
    | zero => omega
    | succ fuel =>
      by_cases atStart : current = start
      · exact ⟨false, by simp [scanTail, atStart]⟩
      · by_cases found : current = target
        · have ne : target ≠ start := found ▸ atStart
          exact ⟨true, by simp [scanTail, found, ne]⟩
        · obtain ⟨answer, returned⟩ := ih (m.next current) fuel (by omega)
            ((followPush m current hops).trans cycle)
          exact ⟨answer, by simp [scanTail, atStart, found, returned]⟩

theorem finiteScanTerminates (m : Metadata (Fin count)) (valid : m.valid)
    (start target : Fin count) :
    ∃ answer, emittedScan m start target count = some answer := by
  by_cases same : start = target
  · exact ⟨true, by simp [emittedScan, same]⟩
  · by_cases single : m.next start = start ∨ m.next target = target
    · exact ⟨false, by simp [emittedScan, same, single]⟩
    · obtain ⟨hops, pos, fit, cycle⟩ := finiteCopyCycle m valid start
      cases hops with
      | zero => omega
      | succ hops =>
        obtain ⟨answer, ran⟩ := returnsBeforeExhaustion m start target (m.next start)
          hops count (by omega) ((followPush m start hops).trans cycle)
        exact ⟨answer, by simp [emittedScan, same, single, ran]⟩

end IA64Optimizer
