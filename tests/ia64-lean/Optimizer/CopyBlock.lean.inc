/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

structure ConstantPool (Temp : Type) where
  readonly : Temp → Option Word
  address : Word → Option Temp
  entry : ∀ v t, address v = some t → readonly t = some v

def ConstantPool.sound (pool : ConstantPool Temp) (s : State Temp) : Prop :=
  ∀ t v, pool.readonly t = some v → s t = v

theorem readonlyPreserved [DecidableEq Temp] (pool : ConstantPool Temp)
    (s : State Temp) (dst : Temp) (v : Word) (sound : pool.sound s)
    (writable : pool.readonly dst = none) : pool.sound (write s dst v) := by
  intro t expected present
  have different : t ≠ dst := by
    intro same
    simp [same, writable] at present
  simpa [write, different] using sound t expected present

structure FoldOptions where
  swap : Bool
  notOpcode : Bool
  extractOpcode : Bool
  length : Word

structure FoldInput (Temp : Type) where
  opcode : Opcode
  dst : Temp
  lhs : Temp
  rhs : Temp
  options : FoldOptions

def FoldInput.x (i : FoldInput Temp) : Temp := if i.options.swap then i.rhs else i.lhs
def FoldInput.y (i : FoldInput Temp) : Temp := if i.options.swap then i.lhs else i.rhs

structure Permitted (pool : ConstantPool Temp) (m : Metadata Temp) (i : FoldInput Temp) : Prop where
  writable : pool.readonly i.dst = none
  commute : i.options.swap = true → commutative i.opcode = true
  extract : i.options.extractOpcode = true →
    TrailingZeros (~~~(m.masks i.y).mayOne) i.options.length

def useFacts (m : Metadata Temp) (i : FoldInput Temp) (alias : Bool) : Info :=
  ⟨m.masks i.x, m.masks i.y, alias, i.options.notOpcode, i.options.extractOpcode, i.options.length⟩

def translatedMove [DecidableEq Temp] (m : Metadata Temp) (dst src : Temp) (fuel : Nat) :
    Option (Metadata Temp × Operation Temp) :=
  (emittedScan m dst src fuel).map (emittedMove m dst src)

theorem translatedMoveSound [DecidableEq Temp] (m out : Metadata Temp) (s : State Temp)
    (dst src : Temp) (fuel : Nat) (op : Operation Temp) (sound : m.sound s)
    (emitted : translatedMove m dst src fuel = some (out, op)) :
    out.sound (op.run s).1 ∧ op.run s = (write s dst (s src), []) := by
  cases answer : emittedScan m dst src fuel with
  | none => simp [translatedMove, answer] at emitted
  | some erase =>
    have equal : emittedMove m dst src erase = (out, op) := by
      simpa [translatedMove, answer] using emitted
    have proof := emittedMoveSound m s dst src fuel erase sound answer
    simpa only [equal] using proof

def writeResult [DecidableEq Temp] (m : Metadata Temp) (dst : Temp) (bits : Masks) : Metadata Temp :=
  let killed := emittedKill m dst
  { killed with masks := setMasks m.masks dst bits }

theorem resultSound [DecidableEq Temp] (m : Metadata Temp) (s : State Temp)
    (dst : Temp) (bits : Masks) (v : Word) (sound : m.sound s) (encoded : bits.sound v) :
    (writeResult m dst bits).sound (write s dst v) := by
  have after := unlinkSound m s dst v sound
  refine ⟨after.1, after.2.1, ?_⟩
  intro t
  by_cases isDest : t = dst
  · subst t
    simpa [writeResult, setMasks, write] using encoded
  · simpa [writeResult, setMasks, write, isDest] using sound.2.2 t

def translatedResult [DecidableEq Temp] (pool : ConstantPool Temp) (m : Metadata Temp)
    (dst x y : Temp) (fuel : Nat) (r : Rewrite) : Option (Metadata Temp × Operation Temp) :=
  match r.expression with
  | .first => translatedMove m dst x fuel
  | .literal v => do
    let literalTemp ← pool.address v
    translatedMove m dst literalTemp fuel
  | _ => some (writeResult m dst r.result, .replace r.expression dst x y)

theorem translatedResultSound [DecidableEq Temp] (pool : ConstantPool Temp)
    (m out : Metadata Temp) (s : State Temp) (dst x y : Temp)
    (fuel : Nat) (r : Rewrite) (op : Operation Temp)
    (sound : m.sound s) (constants : pool.sound s)
    (bits : r.result.sound (r.expression.value (s x) (s y)))
    (emitted : translatedResult pool m dst x y fuel r = some (out, op)) :
    out.sound (op.run s).1 ∧ op.run s = (write s dst (r.expression.value (s x) (s y)), []) := by
  cases r with
  | mk expression mask =>
    cases expression with
    | first => exact translatedMoveSound _ _ _ _ _ _ _ sound emitted
    | literal val =>
      cases lookup : pool.address val with
      | none => simp [translatedResult, lookup] at emitted
      | some literalTemp =>
        have value := constants literalTemp val (pool.entry val literalTemp lookup)
        have chosen : translatedMove m dst literalTemp fuel = some (out, op) := by
          simpa [translatedResult, lookup] using emitted
        have check := translatedMoveSound _ _ _ _ _ _ _ sound chosen
        simpa only [value] using check
    | binary _ | negSecond | complementFirst | addImmediate _ | extractFirst _ | shiftLeft | shiftRight =>
      simp only [translatedResult, Option.some.injEq, Prod.mk.injEq] at emitted
      obtain ⟨rfl, rfl⟩ := emitted
      exact ⟨resultSound _ _ _ _ _ sound bits, rfl⟩

def translatedFold [DecidableEq Temp] (pool : ConstantPool Temp)
    (m : Metadata Temp) (i : FoldInput Temp) (fuel : Nat) :
    Option (Metadata Temp × Operation Temp) := do
  let equal ← emittedScan m i.x i.y fuel
  translatedResult pool m i.dst i.x i.y fuel (rewrite i.opcode (useFacts m i equal))

theorem translatedFoldSound [DecidableEq Temp] (pool : ConstantPool Temp)
    (m out : Metadata Temp) (s : State Temp) (i : FoldInput Temp) (fuel : Nat)
    (op : Operation Temp) (sound : m.sound s) (constants : pool.sound s)
    (permit : Permitted pool m i) (emitted : translatedFold pool m i fuel = some (out, op)) :
    out.sound (op.run s).1 ∧
      op.run s = (Operation.binary i.opcode i.dst i.lhs i.rhs).run s := by
  cases copied : emittedScan m i.x i.y fuel with
  | none => simp [translatedFold, copied] at emitted
  | some equal =>
    let f := useFacts m i equal
    let r := rewrite i.opcode f
    have reported := emittedScanSound m i.x i.y fuel equal copied
    have same : equal = true → s i.x = s i.y := by
      intro ok
      subst equal
      exact (scannedCopies m s sound i.y i.x reported).symm
    have inputs : f.sound (s i.x) (s i.y) :=
      ⟨sound.2.2 i.x, sound.2.2 i.y, same, permit.extract⟩
    have fold := rewriteSound i.opcode f (s i.x) (s i.y) inputs
    have recorded : translatedResult pool m i.dst i.x i.y fuel r = some (out, op) := by
      simpa [translatedFold, copied, f, r] using emitted
    have res := translatedResultSound pool m out s i.dst i.x i.y fuel r op sound constants
      (by rw [fold.1]; exact fold.2) recorded
    have permutation : eval i.opcode (s i.x) (s i.y) = eval i.opcode (s i.lhs) (s i.rhs) := by
      cases swap : i.options.swap
      · simp [FoldInput.x, FoldInput.y, swap]
      · simp only [FoldInput.x, FoldInput.y, swap, if_true]
        exact permutationSafe i.opcode (s i.lhs) (s i.rhs) true (fun _ => permit.commute swap)
    exact ⟨res.1, by rw [res.2, fold.1, permutation]; rfl⟩

inductive ScalarInput (Temp : Type) where
  | fold (i : FoldInput Temp)
  | extra (op : Extra) (dst src arg : Temp)
  | move (dst src : Temp)
  | observe (id : Nat) (value : Temp)

def ScalarInput.unoptimized : ScalarInput Temp → Operation Temp
  | .fold i => .binary i.opcode i.dst i.lhs i.rhs
  | .extra op dst src arg => .replace op.expression dst src arg
  | .move dst src => .replace .first dst src src
  | .observe id value => .observe id value

def AllowedInput (pool : ConstantPool Temp) (m : Metadata Temp) : ScalarInput Temp → Prop
  | .fold i => Permitted pool m i
  | .extra _ dst _ _ => pool.readonly dst = none
  | .move dst _ => pool.readonly dst = none
  | .observe .. => True

def translate [DecidableEq Temp] (pool : ConstantPool Temp) (m : Metadata Temp)
    (fuel : Nat) : ScalarInput Temp → Option (Metadata Temp × Operation Temp)
  | .fold i => translatedFold pool m i fuel
  | .extra op dst src arg =>
    translatedResult pool m dst src arg fuel (extraRewrite op (m.masks src) (m.masks arg))
  | .move dst src => translatedMove m dst src fuel
  | .observe id value => some (m, .observe id value)

theorem stepPreserves [DecidableEq Temp] (pool : ConstantPool Temp) (m out : Metadata Temp)
    (s : State Temp) (fuel : Nat) (input : ScalarInput Temp) (op : Operation Temp)
    (sound : m.sound s) (constants : pool.sound s) (permit : AllowedInput pool m input)
    (emitted : translate pool m fuel input = some (out, op)) :
    out.sound (op.run s).1 ∧ pool.sound (op.run s).1 ∧
      op.run s = input.unoptimized.run s := by
  cases input with
  | fold i =>
    have one := translatedFoldSound _ _ _ _ i fuel op sound constants permit emitted
    exact ⟨one.1, by rw [one.2]; exact readonlyPreserved _ _ _ _ constants permit.writable, one.2⟩
  | extra kind dst src arg =>
    have fold := extraSound kind (m.masks src) (m.masks arg) (s src) (s arg) (sound.2.2 src) (sound.2.2 arg)
    have res := translatedResultSound pool m out s dst src arg fuel _ op sound constants
      (by rw [fold.1]; exact fold.2) emitted
    rw [fold.1] at res
    exact ⟨res.1, by rw [res.2]; exact readonlyPreserved _ _ _ _ constants permit, res.2⟩
  | move dst src =>
    have one := translatedMoveSound m out s dst src fuel op sound emitted
    exact ⟨one.1, by rw [one.2]; exact readonlyPreserved _ _ _ _ constants permit, one.2⟩
  | observe id value =>
    simp only [translate, Option.some.injEq, Prod.mk.injEq] at emitted
    obtain ⟨rfl, rfl⟩ := emitted
    exact ⟨sound, constants, rfl⟩

/-- Complete returns of the supported scalar subgraph, with enough scan
    fuel for each accepted copy-ring walk. The metadata is threaded from
    initialization through each source-generated update. -/
inductive Translation [DecidableEq Temp] (pool : ConstantPool Temp) :
    Metadata Temp → List (ScalarInput Temp) → List (Operation Temp) → Metadata Temp → Prop where
  | end_ (m : Metadata Temp) : Translation pool m [] [] m
  | next {before after final : Metadata Temp} {fuel : Nat} {input : ScalarInput Temp}
      {op : Operation Temp} {srcs : List (ScalarInput Temp)} {ops : List (Operation Temp)}
      (permit : AllowedInput pool before input)
      (emit : translate pool before fuel input = some (after, op))
      (rest : Translation pool after srcs ops final) :
      Translation pool before (input :: srcs) (op :: ops) final

theorem translationSound [DecidableEq Temp] (pool : ConstantPool Temp)
    {before final : Metadata Temp} {srcs : List (ScalarInput Temp)} {ops : List (Operation Temp)}
    (program : Translation pool before srcs ops final) (s : State Temp)
    (sound : before.sound s) (literals : pool.sound s) :
    run s ops = run s (srcs.map ScalarInput.unoptimized) ∧
      final.sound (run s ops).1 ∧ pool.sound (run s ops).1 := by
  induction program generalizing s with
  | end_ _ => exact ⟨rfl, sound, literals⟩
  | next permit emit tail ih =>
    have a := stepPreserves _ _ _ s _ _ _ sound literals permit emit
    have b := ih _ a.1 a.2.1
    simp only [run, List.map_cons]
    rw [← a.2.2, ← b.1]
    exact ⟨rfl, b.2⟩

theorem initializedScalarBlock [DecidableEq Temp] (pool : ConstantPool Temp)
    {final : Metadata Temp} {srcs : List (ScalarInput Temp)} {ops : List (Operation Temp)}
    (program : Translation pool (initialized pool.readonly) srcs ops final)
    (s : State Temp) (literals : pool.sound s) :
    run s ops = run s (srcs.map ScalarInput.unoptimized) ∧
      final.sound (run s ops).1 :=
  let evidence := translationSound pool program s (initializedSound s pool.readonly literals) literals
  ⟨evidence.1, evidence.2.1⟩

end IA64Optimizer
