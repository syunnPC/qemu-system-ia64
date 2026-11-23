/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

def ScalarInput.destination : ScalarInput Temp → Option Temp
  | .fold input => some input.dst
  | .extra _ dst _ _ | .move dst _ => some dst
  | .observe .. => none

theorem untouchedInput [DecidableEq Temp] (s : State Temp) (t : Temp) (i : ScalarInput Temp)
    (separate : i.destination ≠ some t) : (i.unoptimized.run s).1 t = s t := by
  cases i <;> simp only [ScalarInput.destination, ScalarInput.unoptimized, Operation.run, write,
    ne_eq, Option.some.injEq] at *
  all_goals first
    | rfl
    | split
      · rename_i equal
        exact False.elim (separate equal.symm)
      · rfl

theorem untouchedRun [DecidableEq Temp] (s : State Temp) (t : Temp) (srcs : List (ScalarInput Temp))
    (separate : ∀ i ∈ srcs, i.destination ≠ some t) :
    (run s (srcs.map ScalarInput.unoptimized)).1 t = s t := by
  induction srcs generalizing s with
  | nil => rfl
  | cons curr rest ih =>
    simp only [List.map_cons, run]
    rw [ih _ (fun i present => separate i (List.mem_cons_of_mem _ present))]
    exact untouchedInput s t curr (separate curr (List.mem_cons_self))

theorem payloadDestination (r : PayloadRegs Temp) (imm : BitVec 64)
    (target : PayloadTarget) (options : Nat → FoldOptions) (tag : AValue)
    (input : ScalarInput Temp) (member : input ∈ aPayload r imm target options tag) :
    ∃ dst ∈ [r.dst, r.tmp, r.tmp2, r.low, r.high], input.destination = some dst := by
  cases tag <;> simp only [aPayload, List.mem_cons, List.not_mem_nil, or_false] at member
  all_goals
    repeat' (rcases member with member | member)
    all_goals try subst input
    all_goals try split
    all_goals (refine ⟨_, ?_, rfl⟩; simp)

theorem optimizedUntouched [DecidableEq Temp] (pool : ConstantPool Temp) (s : State Temp)
    (r : PayloadRegs Temp) (imm : BitVec 64) (target : PayloadTarget)
    (options : Nat → FoldOptions) (tag : AValue) (t : Temp)
    (separate : t ∉ [r.dst, r.tmp, r.tmp2, r.low, r.high])
    (literals : pool.sound s) {final : Metadata Temp} {ops : List (Operation Temp)}
    (compiled : Translation pool (initialized pool.readonly) (aPayload r imm target options tag) ops final) :
    (run s ops).1 t = s t := by
  rw [(initializedScalarBlock pool compiled s literals).1]
  apply untouchedRun
  intro input present equal
  obtain ⟨dst, footprint, set⟩ := payloadDestination r imm target options tag input present
  rw [equal, Option.some.injEq] at set
  exact separate (set ▸ footprint)

end IA64Optimizer
