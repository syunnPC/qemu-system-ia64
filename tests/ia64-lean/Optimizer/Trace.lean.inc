/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

def commutative (op : Opcode) : Bool := op != .sub

theorem permutationSafe (op : Opcode) (a b : Word) (swap : Bool)
    (allowed : swap = true → commutative op = true) :
    eval op (if swap then b else a) (if swap then a else b) = eval op a b := by
  cases sw : swap
  · rfl
  · have comm := allowed sw
    cases op <;> simp [commutative] at comm <;> simp [eval, sw] <;> bv_decide

def moveExpression : Expression → Bool
  | .literal _ | .first => true
  | _ => false

def lower (expr : Expression) (dst lhs rhs : Temp) (elide : Bool) : Operation Temp :=
  if elide && moveExpression expr then .nop else .replace expr dst lhs rhs

theorem lowerRewrite [DecidableEq Temp] (s : State Temp) (op : Opcode) (dst lhs rhs : Temp)
    (swap elide : Bool) (facts : Info)
    (allowed : swap = true → commutative op = true)
    (sound : facts.sound (s (if swap then rhs else lhs)) (s (if swap then lhs else rhs)))
    (copy : elide = true →
      (rewrite op facts).expression.value (s (if swap then rhs else lhs))
        (s (if swap then lhs else rhs)) = s dst) :
    let x := if swap then rhs else lhs
    let y := if swap then lhs else rhs
    (lower (rewrite op facts).expression dst x y elide).run s =
      (Operation.binary op dst lhs rhs).run s := by
  let x := if swap then rhs else lhs
  let y := if swap then lhs else rhs
  have semantic := rewriteSound op facts (s x) (s y) sound
  have swapCorrect := permutationSafe op (s lhs) (s rhs) swap allowed
  have perm : eval op (s x) (s y) = eval op (s lhs) (s rhs) := by
    cases swap <;> exact swapCorrect
  have value := semantic.1.trans perm
  simp only [lower]
  split
  · rename_i removed
    simp only [Bool.and_eq_true] at removed
    have held := copy removed.1
    have unchanged : eval op (s lhs) (s rhs) = s dst := value.symm.trans held
    simp [Operation.run, unchanged, writeSame]
  · simp [Operation.run, value, x, y]

/-- Finite sequential IR projection, with each fold justified at its
    current input state. Untouched observations may be interleaved. -/
inductive Rewrites [DecidableEq Temp] :
    State Temp → List (Operation Temp) → List (Operation Temp) → Prop where
  | nil (s) : Rewrites s [] []
  | cons (s : State Temp) (before after : Operation Temp) (p q : List (Operation Temp))
      (same : before.run s = after.run s) (rest : Rewrites (before.run s).1 p q) :
      Rewrites s (before :: p) (after :: q)

theorem tracePreservation [DecidableEq Temp] {s : State Temp} {p q : List (Operation Temp)}
    (proof : Rewrites s p q) : run s p = run s q := by
  induction proof with
  | nil _ => rfl
  | cons _ _ _ _ _ same _ ih =>
    simp only [run]
    rw [← same, ih]

def MaskState (Temp : Type) := Temp → Masks

def SoundMasks {Temp : Type} (memory : MaskState Temp) (s : State Temp) : Prop :=
  ∀ t, (memory t).sound (s t)

def setMasks [DecidableEq Temp] (memory : MaskState Temp) (dst : Temp) (mask : Masks) :
    MaskState Temp :=
  fun t => if t = dst then mask else memory t

theorem retainedMasks [DecidableEq Temp] (memory : MaskState Temp) (s : State Temp)
    (dst lhs rhs : Temp) (op : Opcode) (f : Info) (known : SoundMasks memory s)
    (inputs : f.sound (s lhs) (s rhs)) :
    SoundMasks (setMasks memory dst (rewrite op f).result)
      (Operation.binary op dst lhs rhs |>.run s |>.1) := by
  intro t
  by_cases alias : t = dst
  · subst t
    simp only [setMasks, if_pos rfl, Operation.run, write]
    exact (rewriteSound op f (s lhs) (s rhs) inputs).2
  · simpa [setMasks, alias, Operation.run, write] using known t

-- A mask-induced copy, an adjacent observation, and an alias-induced
-- constant all preserve the values in the original sequential order.
theorem chosenForms :
    (rewrite .and ⟨Masks.unknown, Masks.constant (~~~0#64), false, false, false, 0⟩).expression = .first ∧
    (rewrite .xor ⟨Masks.unknown, Masks.unknown, true, false, false, 0⟩).expression = .literal 0 := by
  exact ⟨rfl, rfl⟩

theorem copyThenAlias (s : State (Fin 4)) (ones : s 2 = ~~~0#64) :
    run s [.binary .and 0 1 2, .observe 0 0, .binary .xor 0 0 0, .observe 1 0] =
    run s [.replace .first 0 1 2, .observe 0 0, .replace (.literal 0) 0 0 0, .observe 1 0] := by
  have first : (Operation.binary .and (0 : Fin 4) 1 2).run s = (Operation.replace .first 0 1 2).run s := by
    have value : s 1 &&& s 2 = s 1 := by bv_decide
    simp [Operation.run, Expression.value, eval, value]
  have third (t : State (Fin 4)) :
      (Operation.binary .xor (0 : Fin 4) 0 0).run t =
        (Operation.replace (.literal 0) 0 0 0).run t := by
    simp [Operation.run, Expression.value, eval]
  apply tracePreservation
  exact .cons _ _ _ _ _ first
    (.cons _ _ _ _ _ rfl (.cons _ _ _ _ _ (third _) (.cons _ _ _ _ _ rfl (.nil _))))

end IA64Optimizer
