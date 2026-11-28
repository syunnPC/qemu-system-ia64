/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

def ConstantPool.total (pool : ConstantPool Temp) : Prop :=
  ∀ word, ∃ temp, pool.address word = some temp

private theorem moveExists (m : Metadata (Fin count)) (valid : m.valid) (dst src : Fin count) :
    ∃ out op, translatedMove m dst src count = some (out, op) := by
  obtain ⟨answer, scan⟩ := finiteScanTerminates m valid dst src
  exact ⟨(emittedMove m dst src answer).1, (emittedMove m dst src answer).2,
    by simp [translatedMove, scan]⟩

private theorem resultExists (pool : ConstantPool (Fin count)) (m : Metadata (Fin count))
    (valid : m.valid) (available : pool.total) (dst x y : Fin count) (rewrite : Rewrite) :
    ∃ out op, translatedResult pool m dst x y count rewrite = some (out, op) := by
  cases rewrite with
  | mk expr masks =>
    cases expr with
    | first => exact moveExists m valid dst x
    | literal value =>
      obtain ⟨temp, addr⟩ := available value
      obtain ⟨out, op, emit⟩ := moveExists m valid dst temp
      exact ⟨out, op, by simp [translatedResult, addr, emit]⟩
    | binary _ | negSecond | complementFirst | addImmediate _ | extractFirst _ | shiftLeft | shiftRight =>
      exact ⟨_, _, rfl⟩

theorem translationStepExists (pool : ConstantPool (Fin count)) (m : Metadata (Fin count))
    (valid : m.valid) (available : pool.total) (input : ScalarInput (Fin count)) :
    ∃ out op, translate pool m count input = some (out, op) := by
  cases input with
  | fold i =>
    obtain ⟨answer, scan⟩ := finiteScanTerminates m valid i.x i.y
    obtain ⟨out, op, emit⟩ := resultExists pool m valid available i.dst i.x i.y
      (rewrite i.opcode (useFacts m i answer))
    exact ⟨out, op, by simp [translate, translatedFold, scan, emit]⟩
  | extra kind dst src arg =>
    exact resultExists pool m valid available dst src arg
      (extraRewrite kind (m.masks src) (m.masks arg))
  | move dst src => exact moveExists m valid dst src
  | observe id value => exact ⟨m, .observe id value, rfl⟩

-- A state-independent legality check suffices even as copy and mask
-- information is threaded through the optimizer.
def AlwaysAllowed (pool : ConstantPool Temp) (input : ScalarInput Temp) : Prop :=
  ∀ m, AllowedInput pool m input

theorem translationExists (pool : ConstantPool (Fin count)) (available : pool.total)
    (m : Metadata (Fin count)) (s : State (Fin count))
    (sound : m.sound s) (literals : pool.sound s) (srcs : List (ScalarInput (Fin count)))
    (allowed : ∀ input ∈ srcs, AlwaysAllowed pool input) :
    ∃ ops final, Translation pool m srcs ops final := by
  induction srcs generalizing m s with
  | nil => exact ⟨[], m, .end_ m⟩
  | cons input rest ih =>
    obtain ⟨next, op, emit⟩ := translationStepExists pool m sound.1 available input
    have permit := allowed input List.mem_cons_self m
    have step := stepPreserves pool m next s count input op sound literals permit emit
    obtain ⟨ops, final, tail⟩ := ih next (op.run s).1 step.1 step.2.1
      (fun candidate member => allowed candidate (List.mem_cons_of_mem _ member))
    exact ⟨op :: ops, final, .next permit emit tail⟩

end IA64Optimizer
