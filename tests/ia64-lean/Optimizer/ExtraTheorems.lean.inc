/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

theorem extraConstant (kind : Extra) (a b : Word) :
    cExtraConstant kind a b = kind.expression.value a b := by
  cases kind <;> simp only [cExtraConstant, Extra.expression, Expression.value, lowMask]
  all_goals bv_decide

theorem extraMaskSound (kind : Extra) (m : Masks) (a b : Word) (known : m.sound a) :
    (extraMasks kind m b).sound (kind.expression.value a b) := by
  cases kind <;> simp only [extraMasks, cExtraConstant, Extra.expression, Expression.value,
    Masks.sound, lowMask] at *
  all_goals bv_decide

theorem recordExtraSound (kind : Extra) (m : Masks) (a b affected : Word)
    (bits : m.sound (kind.expression.value a b))
    (unchanged : affected = 0 → kind.expression.value a b = a) :
    Agreement (recordExtra kind m affected) a b (kind.expression.value a b) := by
  unfold recordExtra
  split
  · exact resultConstant _ _ _ _ ‹_› bits
  split
  · have eq := unchanged ‹_›
    rw [eq] at bits ⊢
    exact identityMatches _ _ _ bits
  · exact ⟨rfl, bits⟩

theorem extraSound (kind : Extra) (ma mb : Masks) (a b : Word)
    (first : ma.sound a) (second : mb.sound b) :
    Agreement (extraRewrite kind ma mb) a b (kind.expression.value a b) := by
  have low (len : BitVec 6) : extraAffected (.low len) ma = 0 → (Extra.low len).expression.value a b = a := by
    simp only [extraAffected, cExtraConstant, Extra.expression, Expression.value, Masks.sound, lowMask] at first ⊢
    bv_decide
  have unary (k : Extra) (irrelevant : ∀ x y z, k.expression.value x y = k.expression.value x z)
      (unchanged : extraAffected k ma = 0 → k.expression.value a b = a) :
      Agreement (unaryRewrite k ma) a b (k.expression.value a b) := by
    unfold unaryRewrite
    split
    · rw [← readKnown a ma first ‹_›, extraConstant, irrelevant a 0 b]
      exact literalMatches _ _ _
    · apply recordExtraSound _ _ _ _ _ _ unchanged
      rw [irrelevant a b 0]
      exact extraMaskSound k ma a 0 first
  have binary (k : Extra) (zero : ∀ w, k.expression.value 0 w = 0)
      (identityElem : ∀ w, k.expression.value w 0 = w) :
      Agreement (shiftRewrite k ma mb) a b (k.expression.value a b) := by
    unfold shiftRewrite
    split
    · rename_i both
      rw [← readKnown a ma first (Bool.and_eq_true_iff.mp both).1,
          ← readKnown b mb second (Bool.and_eq_true_iff.mp both).2, extraConstant]
      exact literalMatches _ _ _
    split
    · rw [knownValue ma a _ first ‹_›, zero]
      exact literalMatches _ _ _
    split
    · rw [knownValue mb b _ second ‹_›, identityElem]
      exact identityMatches _ _ _ first
    split
    · rw [← readKnown b mb second ‹_›]
      exact recordExtraSound _ _ _ _ _ (extraMaskSound k ma a b first)
        (fun bad => False.elim ((by decide : ~~~0#64 ≠ 0) bad))
    · exact ⟨rfl, unknownSound _⟩
  cases kind with
  | complement =>
    exact unary .complement (fun _ _ _ => rfl) (fun bad => False.elim ((by decide : ~~~0#64 ≠ 0) bad))
  | low len => exact unary (.low len) (fun _ _ _ => rfl) (low len)
  | shl =>
    apply binary .shl <;> intro w <;> simp [Extra.expression, Expression.value]
  | shr =>
    apply binary .shr <;> intro w <;> simp [Extra.expression, Expression.value]

end IA64Optimizer
