/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64OptimizedSlotLive

open IA64Optimizer
local notation "Word" => BitVec 64

inductive AddTemp where
  | output | source | scratch | zero
  deriving DecidableEq

def addPool : ConstantPool AddTemp where
  readonly | .zero => some 0 | _ => none
  address v := if v = 0 then some .zero else none
  entry := by
    intro v t h
    split at h
    · rename_i zero
      subst v
      have eq := Option.some.inj h
      subst t
      rfl
    · contradiction

def addEntry (old source : Word) : State AddTemp
  | .output => old
  | .source => source
  | _ => 0

theorem addEntry_literals (old source : Word) : addPool.sound (addEntry old source) := by
  intro temp v lookup
  cases temp <;> simp [addPool] at lookup
  simpa only [addEntry] using lookup

def addMoveInput (dst src : AddTemp) : ScalarInput AddTemp :=
  .fold ⟨.add, dst, src, .zero, plainOptions⟩

-- Both roles come from the raw TCG arguments. GR aliases share the
-- same role and the no-write case is decided by the optimizer's scan.
theorem addZeroTrace (dst src : AddTemp)
    (input : addPool.readonly dst = none) (dynamic : addPool.readonly src = none) :
    Translation addPool (initialized addPool.readonly) [addMoveInput dst src]
      [if dst = src then .nop else .replace .first dst src src]
      (emittedMove (initialized addPool.readonly) dst src (decide (dst = src))).1 := by
  have permit : AllowedInput addPool (initialized addPool.readonly) (addMoveInput dst src) :=
    ⟨input, by simp [addMoveInput, plainOptions], by simp [addMoveInput, plainOptions]⟩
  apply Translation.next (fuel := 4)
    (after := (emittedMove (initialized addPool.readonly) dst src (decide (dst = src))).1) permit
  · cases dst <;> cases src <;> simp [addPool] at input dynamic <;> rfl
  · exact .end_ _

def addTempKind : AddTemp → Nat
  | .scratch => 1
  | .output | .source => 2
  | .zero => 4

def capturedCopy (dst src : AddTemp) : Metadata AddTemp :=
  (emittedMove (initialized addPool.readonly) dst src (decide (dst = src))).1

theorem capturedCopy_value (dst src : AddTemp) (old source : Word)
    (input : addPool.readonly dst = none) (dynamic : addPool.readonly src = none)
    (visits : List Nat) (kind : AddTemp → Nat) :
    let m := capturedCopy dst src
    let op := if dst = src then Operation.nop else .replace .first dst src src
    let next := (op.run (addEntry old source)).1
    next (visits.foldl (fun current n => pickCopy kind current (follow m dst n)) dst) =
      addEntry old source src := by
  let e := addEntry old source
  let code : Operation AddTemp := if dst = src then .nop else .replace .first dst src src
  let m := capturedCopy dst src
  have witness := addZeroTrace dst src input dynamic
  have preserved := initializedScalarBlock addPool witness e (addEntry_literals old source)
  have valid : m.sound (code.run e).1 := by
    simpa only [run, capturedCopy, code, m] using preserved.2
  have observed := copyPreference m (code.run e).1 valid kind dst dst rfl visits
  dsimp only
  rw [show (code.run e).1
    (List.foldl (fun current n => pickCopy kind current (follow m dst n)) dst visits) =
      (code.run e).1 dst from observed]
  have eval : (code.run e).1 dst = e src := by
    by_cases alias : dst = src
    · simp [code, alias, Operation.run]
    · simp [code, alias, Operation.run, Expression.value, write]
  exact eval

end IA64OptimizedSlotLive

namespace IA64OptimizedSlotLive

open IA64Optimizer
local notation "Word" => BitVec 64

def preferredAddValue (dst src : AddTemp) (old source : Word) (visits : List Nat) : Word :=
  let m := capturedCopy dst src
  let op := if dst = src then Operation.nop else .replace .first dst src src
  let next := (op.run (addEntry old source)).1
  next (visits.foldl (fun current n => pickCopy addTempKind current (follow m dst n)) dst)

theorem preferredAddValue_correct (dst src : AddTemp) (old source : Word)
    (output : addPool.readonly dst = none) (input : addPool.readonly src = none)
    (visits : List Nat) :
    preferredAddValue dst src old source visits = addEntry old source src :=
  capturedCopy_value dst src old source output input visits addTempKind

def observeAddCopy (branchless alias : Bool) (old source pred tags : Word) : Word × Word :=
  let dst := if branchless then AddTemp.scratch else AddTemp.output
  let src := if alias then AddTemp.output else AddTemp.source
  let value := preferredAddValue dst src old source (if branchless then [1] else [])
  let natBit := if alias then 8 else 9
  let shifted := ((tags >>> natBit) &&& 1) <<< (8 : Nat)
  let cleared := tags &&& 0xfffffffffffffeff
  let newNat := if branchless then cleared ||| shifted
    else if alias then tags
    else if tags &&& 0x200 = 0 then cleared else tags ||| 0x100
  if branchless = true ∧ pred = 0 then (old, tags) else (value, newNat)

theorem observeAddCopy_matches (branchless alias : Bool) (old source pred tags : Word) :
    observeAddCopy branchless alias old source pred tags =
      (if branchless = true ∧ pred = 0 then old
       else IA64Integer.SDM.ADD (if alias then old else source) 0,
       if branchless = true ∧ pred = 0 then tags else
         IA64Integer.SDM.replaceNaT tags 8 (IA64Integer.SDM.staticNaT tags (if alias then 8 else 9))) := by
  have rewriteValue (dst src : AddTemp) (output : addPool.readonly dst = none)
      (input : addPool.readonly src = none) (visits : List Nat) :=
    preferredAddValue_correct dst src old source output input visits
  cases branchless <;> cases alias <;> by_cases skip : pred = 0 <;>
    simp only [observeAddCopy, Bool.false_eq_true, not_false_eq_true, if_false, if_true,
      false_and, true_and, if_pos, if_neg, skip,
      rewriteValue .output .source (by decide) (by decide) [],
      rewriteValue .output .output (by decide) (by decide) [],
      rewriteValue .scratch .source (by decide) (by decide) [1],
      rewriteValue .scratch .output (by decide) (by decide) [1], addEntry]
  all_goals apply Prod.ext <;>
    simp [IA64Integer.SDM.ADD, IA64Integer.SDM.staticNaT, IA64Integer.SDM.replaceNaT] <;>
    bv_decide

end IA64OptimizedSlotLive
