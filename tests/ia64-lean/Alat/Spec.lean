/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Lean.Elab.Tactic.Omega

set_option warningAsError true

namespace Alat

abbrev Samples := List (Nat × Nat)

def openingSum (samples : Samples) : Nat :=
  (samples.map Prod.fst).sum

def closingSum (samples : Samples) : Nat :=
  (samples.map Prod.snd).sum

-- A fixed CPU set supplies one opening/closing pair per CPU. The sample
-- times can differ. The external generation can be included as another pair.
theorem sum_mono (samples : Samples) (advance : ∀ p ∈ samples, p.1 ≤ p.2) :
    openingSum samples ≤ closingSum samples := by
  induction samples with
  | nil => decide
  | cons head tail ih =>
    have first := advance head (by simp)
    have rest := ih (fun p pm => advance p (by simp [pm]))
    simp only [openingSum, closingSum, List.map_cons, List.sum_cons] at *
    omega

theorem equal_sum_keeps_pairs (samples : Samples)
    (advance : ∀ p ∈ samples, p.1 ≤ p.2)
    (equalSum : openingSum samples = closingSum samples) :
    ∀ p ∈ samples, p.1 = p.2 := by
  induction samples with
  | nil => simp
  | cons head tail ih =>
    have first := advance head (by simp)
    have tailMono : ∀ p ∈ tail, p.1 ≤ p.2 :=
      fun p pm => advance p (by simp [pm])
    have rest := sum_mono tail tailMono
    simp only [openingSum, closingSum, List.map_cons, List.sum_cons] at equalSum
    have firstEq : head.1 = head.2 := by
      unfold openingSum closingSum at rest
      omega
    have restEq : openingSum tail = closingSum tail := by
      unfold openingSum closingSum
      omega
    intro p member
    rcases List.mem_cons.mp member with rfl | member
    · exact firstEq
    · exact ih tailMono restEq p member

-- uint64_t generation arithmetic accepts equal sums modulo 2^64. This
-- theorem assumes the absolute closing total is below that modulus.
theorem equal_word_sum_keeps_pairs (samples : Samples)
    (advance : ∀ p ∈ samples, p.1 ≤ p.2)
    (noWrap : closingSum samples < 2 ^ 64)
    (equalWords : BitVec.ofNat 64 (openingSum samples) =
      BitVec.ofNat 64 (closingSum samples)) :
    ∀ p ∈ samples, p.1 = p.2 := by
  have openingBound : openingSum samples < 2 ^ 64 :=
    Nat.lt_of_le_of_lt (sum_mono samples advance) noWrap
  have exactSum := congrArg BitVec.toNat equalWords
  rw [BitVec.toNat_ofNat, BitVec.toNat_ofNat,
      Nat.mod_eq_of_lt openingBound, Nat.mod_eq_of_lt noWrap] at exactSum
  exact equal_sum_keeps_pairs samples advance exactSum

theorem accepted_idle_pair (samples : Samples)
    (advance : ∀ p ∈ samples, p.1 ≤ p.2)
    (noWrap : closingSum samples < 2 ^ 64)
    (equalWords : BitVec.ofNat 64 (openingSum samples) =
      BitVec.ofNat 64 (closingSum samples))
    (closingEven : ∀ p ∈ samples, p.2 % 2 = 0) :
    ∀ p ∈ samples, p.1 = p.2 ∧ p.1 % 2 = 0 := by
  intro p hp
  have pairEq := equal_word_sum_keeps_pairs samples advance noWrap equalWords p hp
  exact ⟨pairEq, pairEq ▸ closingEven p hp⟩

-- This applies separately to each CPU's own paired sample interval.
theorem idle_pair_keeps_interval (seq : Nat → Nat)
    (monotone : ∀ first last, first ≤ last → seq first ≤ seq last)
    (opening middle closing : Nat) (opened : opening ≤ middle)
    (closed : middle ≤ closing) (equalEnds : seq opening = seq closing) :
    seq middle = seq opening :=
  Nat.le_antisymm (equalEnds ▸ monotone middle closing closed)
    (monotone opening middle opened)

theorem wrapping_total_alias :
    BitVec.ofNat 64 (openingSum [(0, 2 ^ 63), (0, 2 ^ 63)]) =
    BitVec.ofNat 64 (closingSum [(0, 2 ^ 63), (0, 2 ^ 63)]) := by decide

end Alat
