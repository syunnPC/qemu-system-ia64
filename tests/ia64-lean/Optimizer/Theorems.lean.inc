/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

def Agreement (output : Rewrite) (a b value : Word) : Prop :=
  output.expression.value a b = value ∧ output.result.sound value

theorem constantOperation (op : Opcode) (a b : Word) : cConstant op a b = eval op a b := by
  cases op <;> (simp only [cConstant, eval] <;> first | rfl | bv_decide)

def int32Constant (op : Opcode) (a b : BitVec 32) : Word :=
  ((cConstant op (a.signExtend 64) (b.signExtend 64)).extractLsb' 0 32).signExtend 64

theorem constantOperation32 (op : Opcode) (a b : BitVec 32) :
    int32Constant op a b = (eval op a b).signExtend 64 := by
  cases op <;> simp only [int32Constant, cConstant, eval] <;> bv_decide

theorem knownValue (m : Masks) (a value : Word)
    (sound : m.sound a) (known : isConstant m value = true) : a = value := by
  simp only [isConstant, Bool.and_eq_true, beq_iff_eq] at known
  exact (readKnown a m sound known.1).trans known.2

theorem literalMatches (word a b : Word) : Agreement (literal word) a b word :=
  ⟨rfl, constantSound word⟩

theorem identityMatches (m : Masks) (a b : Word) (bound : m.sound a) :
    Agreement (identity m) a b a := ⟨rfl, bound⟩

theorem keepMatches (op : Opcode) (m : Masks) (a b : Word) (bound : m.sound (eval op a b)) :
    Agreement (keep op m) a b (eval op a b) := ⟨rfl, bound⟩

theorem constantsMatch (op : Opcode) (f : Info) (a b : Word)
    (sound : Info.sound f a b) (seen : constOperands f = true) :
    Agreement (bothConstants op f) a b (eval op a b) := by
  simp only [constOperands, Bool.and_eq_true] at seen
  have x := readKnown a f.x sound.1 seen.1
  have y := readKnown b f.y sound.2.1 seen.2
  rw [x, y, ← constantOperation]
  exact literalMatches _ _ _

theorem resultConstant (m : Masks) (a b result : Word) (known : m.known = true)
    (bound : m.sound result) : Agreement (literal m.mustOne) a b result := by
  have value := readKnown result m bound known
  simp only [Masks.known, beq_iff_eq] at known
  rw [value, known]
  exact literalMatches _ _ _

theorem andMaskSound (f : Info) (a b : Word) (x : f.x.sound a) (y : f.y.sound b) :
    (andResult f).sound (a &&& b) := by
  simp only [andResult, Masks.sound] at *
  bv_decide

theorem orMaskSound (f : Info) (a b : Word) (x : f.x.sound a) (y : f.y.sound b) :
    (orResult f).sound (a ||| b) := by
  simp only [orResult, Masks.sound] at *
  bv_decide

theorem xorMaskSound (f : Info) (a b : Word) (x : f.x.sound a) (y : f.y.sound b) :
    (xorResult f).sound (a ^^^ b) := by
  simp only [xorResult, Masks.sound] at *
  bv_decide

theorem notMaskSound (m : Masks) (a : Word) (x : m.sound a) :
    Masks.sound ⟨~~~m.mustOne, ~~~m.mayOne⟩ (~~~a) := by
  simp only [Masks.sound] at *
  bv_decide

theorem andUnaffected (f : Info) (a b : Word) (x : f.x.sound a) (y : f.y.sound b)
    (none : andAffected f = 0) : a &&& b = a := by
  simp only [andAffected, Masks.sound] at *
  bv_decide

theorem orUnaffected (f : Info) (a b : Word) (x : f.x.sound a) (y : f.y.sound b)
    (none : orAffected f = 0) : a ||| b = a := by
  simp only [orAffected, Masks.sound] at *
  bv_decide

theorem addFold (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (foldAdd f) a b (a + b) := by
  unfold foldAdd
  split
  · exact constantsMatch .add f a b sound (by assumption)
  · split
    · have zero := knownValue f.y b 0 sound.2.1 (by assumption)
      simpa [zero] using identityMatches f.x a b sound.1
    · exact keepMatches .add _ a b (unknownSound _)

theorem subFold (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (foldSub f) a b (a - b) := by
  unfold foldSub
  split
  · exact constantsMatch .sub f a b sound (by assumption)
  · split
    · have same := sound.2.2.1 (by assumption)
      simpa [same] using literalMatches 0 a b
    · split
      · have zero := knownValue f.y b 0 sound.2.1 (by assumption)
        simpa [zero] using identityMatches f.x a b sound.1
      · split
        · have zero := knownValue f.x a 0 sound.1 (by assumption)
          exact ⟨by simp [Expression.value, zero], unknownSound _⟩
        · split
          · have right := readKnown b f.y sound.2.1 (by assumption)
            refine ⟨?_, unknownSound _⟩
            simp only [Expression.value, right]
            bv_decide
          · exact keepMatches .sub _ a b (unknownSound _)

theorem andFold (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (foldAnd f) a b (a &&& b) := by
  have mask := andMaskSound f a b sound.1 sound.2.1
  unfold foldAnd
  split
  · exact constantsMatch .and f a b sound (by assumption)
  · split
    · exact resultConstant _ a b _ (by assumption) mask
    · split
      · have same := andUnaffected f a b sound.1 sound.2.1 (by assumption)
        simpa [same] using identityMatches f.x a b sound.1
      · split
        · rename_i const
          split
          · rename_i extract
            have rhs := readKnown b f.y sound.2.1 const
            have maskEqual := extractLowContiguous f.y.mayOne f.extractLength extract.1
              (sound.2.2.2 extract.2)
            exact ⟨by simp [Expression.value, rhs, maskEqual], mask⟩
          · exact keepMatches .and _ a b mask
        · split
          · have same := sound.2.2.1 (by assumption)
            simpa [same] using identityMatches f.x a b sound.1
          · exact keepMatches .and _ a b mask

theorem orFold (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (foldOr f) a b (a ||| b) := by
  have mask := orMaskSound f a b sound.1 sound.2.1
  unfold foldOr
  split
  · exact constantsMatch .or f a b sound (by assumption)
  · split
    · have zero := knownValue f.y b 0 sound.2.1 (by assumption)
      simpa [zero] using identityMatches f.x a b sound.1
    · split
      · have same := sound.2.2.1 (by assumption)
        simpa [same] using identityMatches f.x a b sound.1
      · split
        · exact resultConstant _ a b _ (by assumption) mask
        · split
          · have same := orUnaffected f a b sound.1 sound.2.1 (by assumption)
            simpa [same] using identityMatches f.x a b sound.1
          · exact keepMatches .or _ a b mask

theorem xorFold (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (foldXor f) a b (a ^^^ b) := by
  have mask := xorMaskSound f a b sound.1 sound.2.1
  unfold foldXor
  split
  · exact constantsMatch .xor f a b sound (by assumption)
  · split
    · have same := sound.2.2.1 (by assumption)
      simpa [same] using literalMatches 0 a b
    · split
      · have zero := knownValue f.y b 0 sound.2.1 (by assumption)
        simpa [zero] using identityMatches f.x a b sound.1
      · split
        · rename_i on
          simp only [Bool.and_eq_true] at on
          have ones := knownValue f.y b (~~~0#64) sound.2.1 on.1
          have diff : a ^^^ b = ~~~a := by bv_decide
          rw [diff]
          exact ⟨rfl, notMaskSound f.x a sound.1⟩
        · split
          · exact resultConstant _ a b _ (by assumption) mask
          · exact keepMatches .xor _ a b mask

def rewrite (op : Opcode) (f : Info) : Rewrite :=
  match op with
  | .add => foldAdd f
  | .sub => foldSub f
  | .and => foldAnd f
  | .or => foldOr f
  | .xor => foldXor f

theorem rewriteSound (op : Opcode) (f : Info) (a b : Word) (sound : f.sound a b) :
    Agreement (rewrite op f) a b (eval op a b) := by
  cases op with
  | add => exact addFold f a b sound
  | sub => exact subFold f a b sound
  | and => exact andFold f a b sound
  | or => exact orFold f a b sound
  | xor => exact xorFold f a b sound

end IA64Optimizer
