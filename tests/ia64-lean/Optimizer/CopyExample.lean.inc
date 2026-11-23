/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

def examplePool : ConstantPool (Fin 4) where
  readonly t := if t = 3 then some 0 else none
  address v := if v = 0 then some 3 else none
  entry := by
    intro v t lookup
    by_cases zero : v = 0#64
    · simp [zero] at lookup
      simp [zero, ← lookup]
    · simp [zero] at lookup

def plainOptions : FoldOptions := ⟨false, false, false, 0⟩

def addAfterCopy : FoldInput (Fin 4) := ⟨.add, 1, 0, 3, plainOptions⟩
def xorCopies : FoldInput (Fin 4) := ⟨.xor, 2, 0, 1, plainOptions⟩

def beforeCopy : Metadata (Fin 4) := initialized examplePool.readonly
def afterCopy : Metadata (Fin 4) := (emittedMove beforeCopy 1 0 false).1
def afterXor : Metadata (Fin 4) := (emittedMove afterCopy 2 3 false).1

def exampleSource : List (ScalarInput (Fin 4)) :=
  [.move 1 0, .fold addAfterCopy, .fold xorCopies, .observe 0 2]

def exampleOptimized : List (Operation (Fin 4)) :=
  [.replace .first 1 0 0, .nop, .replace .first 2 3 3, .observe 0 2]

theorem exampleCompiles : Translation examplePool beforeCopy exampleSource exampleOptimized afterXor := by
  have addOk : Permitted examplePool afterCopy addAfterCopy :=
    ⟨rfl, by simp [addAfterCopy, plainOptions], by simp [addAfterCopy, plainOptions]⟩
  have xorOk : Permitted examplePool afterCopy xorCopies :=
    ⟨rfl, by simp [xorCopies, plainOptions], by simp [xorCopies, plainOptions]⟩
  exact .next (fuel := 4) rfl rfl
    (.next (fuel := 4) addOk rfl
      (.next (fuel := 4) xorOk rfl (.next (fuel := 4) trivial rfl (.end_ afterXor))))

/-- With one materialized zero constant, the first move creates a copy
    ring, ADD by zero emits no write, and XOR of the copies produces zero. -/
theorem examplePreserved (s : State (Fin 4)) (zero : s 3 = 0#64) :
    run s exampleOptimized = run s (exampleSource.map ScalarInput.unoptimized) ∧
    (run s exampleOptimized).2 = [⟨0, 0⟩] := by
  have constants : examplePool.sound s := by
    intro t v loaded
    by_cases constant : t = (3 : Fin 4)
    · simpa [examplePool, constant, zero] using loaded
    · simp [examplePool, constant] at loaded
  have checked := initializedScalarBlock examplePool exampleCompiles s constants
  refine ⟨checked.1, ?_⟩
  simp [exampleOptimized, run, Operation.run, Expression.value, write, zero]

end IA64Optimizer
