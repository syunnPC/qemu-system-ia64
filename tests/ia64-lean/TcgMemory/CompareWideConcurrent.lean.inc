/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.CompareWide

private theorem pendingPrefixes (select : Pair → Word) (ccv : Word) (initial : Pair)
    (seen : List Pair) (pending : retry select ccv initial seen = none) (n : Nat) :
    retry select ccv initial (seen.take n) = none := by
  induction seen generalizing initial n with
  | nil => simpa using pending
  | cons observed other ih =>
    by_cases matchesCCV : select initial = ccv
    · by_cases unchanged : observed = initial
      · simp [retry, matchesCCV, unchanged] at pending
      · have pendingNext : retry select ccv observed other = none := by
          simpa [retry, matchesCCV, unchanged] using pending
        cases n with
        | zero => simp [retry, matchesCCV]
        | succ n =>
          simpa [List.take_succ_cons, retry, matchesCCV, unchanged] using ih observed pendingNext n
    · simp [retry, matchesCCV] at pending

private theorem pendingRetry (i : Instruction) (s : Registers) (m : Inputs)
    (native : m.parallel = true) (held : attempt i s m = .pending s) :
    let big := m.cell.guestBig
    let select := sourceHalf big (sourceUpper (getGR s i.base))
    retry select m.ccv (orderWords big (firstPair m)) (m.observations.map (orderWords big)) = none := by
  unfold attempt at held
  cases gate : beforePrimitive i s m
  case some fault => simp [gate] at held
  simp only [gate, afterPrimitive, native, if_true, sourceRegister] at held
  split at held
  · assumption
  · cases held

-- The next CAS sample is read from the current physical block.
def sampleCAS (m : Inputs) (current : Nat → Byte) : Inputs :=
  {m with observations := m.observations ++ [operand current m.cell.guestBig]}

theorem commitUsesCurrent (i : Instruction) (s after : Registers) (m : Inputs)
    (e : Effect) (current : Nat → Byte) (native : m.parallel = true)
    (waiting : attempt i s m = .pending s)
    (retired : attempt i s (sampleCAS m current) = .retired after e)
    (written : e.store.isSome = true) :
    architectureAt i s m (operand current m.cell.guestBig) = .retired after e := by
  obtain ⟨selected, _, agreement, success⟩ := retiresAtSnapshot i s after (sampleCAS m current) e retired
  obtain ⟨index, first⟩ := success native written
  have atIndex : (m.observations ++ [operand current m.cell.guestBig])[index]? = some selected := first.1
  have atMost : index ≤ m.observations.length := by
    have existsAt := (List.getElem?_eq_some_iff.mp atIndex).1
    simp at existsAt
    omega
  have last : index = m.observations.length := by
    by_cases earlier : index < m.observations.length
    · let big := m.cell.guestBig
      let raw := m.observations.map (orderWords big)
      have prior := pendingRetry i s m native waiting
      have result := first.2.1
      change retry (sourceHalf big (sourceUpper (getGR s i.base))) m.ccv (orderWords big (firstPair m))
        (((m.observations ++ [operand current big]).map (orderWords big)).take (index + 1)) =
          some (orderWords big selected, true) at result
      rw [List.map_append, List.take_append_of_le_length
        (show index + 1 ≤ (m.observations.map (orderWords big)).length by simpa using Nat.succ_le_of_lt earlier)]
        at result
      have unresolved := pendingPrefixes _ _ _ raw prior (index + 1)
      rw [unresolved] at result
      cases result
    · omega
  rw [last, List.getElem?_append_right (Nat.le_refl _), Nat.sub_self] at atIndex
  have equal := Option.some.inj atIndex
  exact equal ▸ agreement

structure Waiting where
  instruction : Instruction
  regs : Registers
  inputs : Inputs
  parallel : inputs.parallel = true
  pending : attempt instruction regs inputs = .pending regs

private theorem pendingRegisters (i : Instruction) (s held : Registers) (m : Inputs)
    (result : attempt i s m = .pending held) : s = held := by
  unfold attempt at result
  cases gate : beforePrimitive i s m
  case some fault => simp [gate] at result
  simp only [gate, afterPrimitive] at result
  split at result <;> cases result
  rfl

def begin (i : Instruction) (s : Registers) (m : Inputs) (current : Nat → Byte) : Option Waiting :=
  let initial := {m with cell := {m.cell with bytes := current}, observations := []}
  if parallel : initial.parallel = true then
    match pending : attempt i s initial with
    | .pending held =>
      some ⟨i, s, initial, parallel, (pendingRegisters i s held initial pending).symm ▸ pending⟩
    | _ => none
  else none

-- This is a single physical block. A CAS samples and updates that
-- block in one step. Ordinary accesses elsewhere get no order here.
structure Block (Cpu : Type) where
  base : Word
  phys : Cpu → Word → Word
  bytes : Nat → Byte
  waiting : Cpu → Option Waiting

def mappedBlock (block : Block Cpu) (cpu : Cpu) (active : Waiting) : Bool :=
  let virtual := sourceBlock (getGR active.regs active.instruction.base)
  (block.base.toNat % 16 == 0) &&
    (List.range 16).all (fun offset =>
      (block.phys cpu (virtual + BitVec.ofNat 64 offset)).toNat == block.base.toNat + offset)

def replace (before : Nat → Byte) (store : Option (List Byte)) (offset : Nat) : Byte :=
  (memoryAfter 0 store (fun n => some (before n)) offset).getD (before offset)

def step [DecidableEq Cpu] (block : Block Cpu) (cpu : Cpu) : Block Cpu × Option (Registers × Effect) :=
  match block.waiting cpu with
  | none => (block, none)
  | some old =>
    if !mappedBlock block cpu old then (block, none) else
    let loaded := sampleCAS old.inputs block.bytes
    match result : attempt old.instruction old.regs loaded with
    | .stopped _ _ => (block, none)
    | .pending held =>
      let regs := pendingRegisters old.instruction old.regs held loaded result
      let next : Waiting := ⟨old.instruction, old.regs, loaded, old.parallel, regs.symm ▸ result⟩
      ({block with waiting := fun other => if cpu = other then some next else block.waiting other}, none)
    | .retired after e =>
      ({ block with
         bytes := replace block.bytes e.store
         waiting := fun other => if cpu = other then none else block.waiting other },
        some (after, e))

theorem stepCommit [DecidableEq Cpu] (block : Block Cpu) (cpu : Cpu)
    (after : Registers) (e : Effect)
    (completed : (step block cpu).2 = some (after, e)) (writes : e.store.isSome = true) :
    ∃ current, block.waiting cpu = some current ∧ mappedBlock block cpu current = true ∧
      architectureAt current.instruction current.regs current.inputs
        (operand block.bytes current.inputs.cell.guestBig) = .retired after e ∧
      (step block cpu).1.bytes = replace block.bytes e.store := by
  unfold step at completed
  cases pending : block.waiting cpu with
  | none => simp [pending] at completed
  | some current =>
    cases mapH : mappedBlock block cpu current
    case false => simp [pending, mapH] at completed
    simp only [pending, mapH, Bool.not_true, Bool.false_eq_true, if_false] at completed
    split at completed <;> try contradiction
    rename_i updated effect sampled
    obtain ⟨rfl, rfl⟩ := Prod.mk.inj (Option.some.inj completed)
    refine ⟨current, rfl, mapH, ?_, ?_⟩
    · exact commitUsesCurrent current.instruction current.regs updated current.inputs effect block.bytes
        current.parallel current.pending sampled writes
    · unfold step
      simp only [pending, mapH, Bool.not_true, Bool.false_eq_true, if_false]
      split <;> rename_i same
      · rw [sampled] at same
        cases same
      · rw [sampled] at same
        cases same
      · obtain ⟨rfl, rfl⟩ := Result.retired.inj (same.symm.trans sampled)
        rfl

private def racingBlock : Block Bool :=
  ⟨16, fun _ virtual => virtual, exampleWideCell.bytes, fun actor =>
    begin exampleInstruction (wideRegs 24)
      {exampleInputs with csd := if actor then 0x44 else 0xaa} exampleWideCell.bytes⟩

private def firstStep := step racingBlock true
private def secondStep := step firstStep.1 false

example : firstStep.2.map Prod.snd =
    some ⟨24, 16, 0xb2, some (pack 8 true 24 ++ pack 8 true 0x44), .acquire⟩ := rfl

-- The loser reads the 0x44 from the winner's replacement and does
-- not commit its own replacement. Both started from the 0xb2 image.
example : secondStep.2.map Prod.snd = some ⟨24, 16, 0x44, none, .acquire⟩ ∧
    secondStep.1.bytes 15 = 0x44 := ⟨rfl, rfl⟩

theorem concurrentContract : Contract ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (block : Block Cpu) (cpu : Cpu) (after : Registers) (e : Effect),
      (step block cpu).2 = some (after, e) → e.store.isSome = true →
      ∃ current, block.waiting cpu = some current ∧ mappedBlock block cpu current = true ∧
        architectureAt current.instruction current.regs current.inputs
          (operand block.bytes current.inputs.cell.guestBig) = .retired after e ∧
        (step block cpu).1.bytes = replace block.bytes e.store) :=
  ⟨contract, stepCommit⟩

end TcgMemory.CompareWide
