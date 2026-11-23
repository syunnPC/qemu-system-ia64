/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Integer.Spec

namespace IA64Tcg

abbrev Reg := Fin 128
abbrev Predicate := Fin 64
local notation "Word" => BitVec 64

/-- The current architectural virtual GR view. TCG stores packed NaT words
    for these registers. The RSE materialization that produces this view,
    exception delivery and the surrounding bundle are separate stages. -/
structure State (Other : Type) where
  gr : Reg → Word
  natWord : Nat → Word
  pr : Predicate → Bool
  dirty : Reg → Bool
  sof : Nat
  other : Other

structure Slot where
  dst : Reg
  r2 : Reg
  r3 : Reg
  qp : Predicate
  imm : Word
  ip : Word
  ri : Fin 3
  raw : Word
  malformed : Bool
  illegalPlacement : Bool
  reservedField : Bool
  missingFeature : Bool

inductive Fault where
  | illegal
  | reservedField
  deriving DecidableEq

inductive Decision where
  | execute
  | nullified
  | fault (cause : Fault)
  deriving DecidableEq

inductive Outcome (Other : Type) where
  | committed (state : State Other)
  | nullified (state : State Other)
  | fault (cause : Fault) (ip : Word) (ri : Fin 3) (raw : Word) (before : State Other)

/-- Operation semantics after decoding. The operand mask describes which
    architectural values and NaTs are read. -/
structure Alu where
  value : Word → Word → Word → Word
  usesR2 : Bool
  usesR3 : Bool

def update [DecidableEq α] (old : α → β) (index : α) (value : β) : α → β :=
  fun i => if i = index then value else old i

def wordIndex (r : Reg) : Nat := r.val / 64
def bitIndex (r : Reg) : Word := BitVec.ofNat 64 (r.val % 64)
def regWord (r : Reg) : Word := BitVec.ofNat 64 r.val

namespace SDM

-- Intel Itanium SDM rev. 2.3, vol. 1 GR/PR definitions and vol. 3
-- preconditions for native ALU operations and check_target_register.
def value (s : State α) (r : Reg) : Word := if r = 0 then 0 else s.gr r

def nat (s : State α) (r : Reg) : Bool :=
  r != 0 && ((s.natWord (wordIndex r) >>> bitIndex r) &&& 1#64) != 0

def qualified (s : State α) (qp : Predicate) : Bool := qp == 0 || s.pr qp

def legalTarget (s : State α) (dst : Reg) : Bool :=
  dst != 0 && (dst.val < 32 || dst.val < 32 + s.sof)

/-- Out-of-frame source reads have an undefined architectural result
    (SDM vol. 1 §4.1.1). The exact state-equality contract is stated for
    executions whose read operands are defined. -/
def definedSources (s : State α) (i : Slot) (op : Alu) : Prop :=
  (op.usesR2 = true → i.r2.val < 32 + s.sof) ∧
  (op.usesR3 = true → i.r3.val < 32 + s.sof)

/-- The decoder supplies encoding validity and placement/field/model
    classifications. The latter three checks are predicate-qualified for
    this family. A malformed encoding has no valid qualifier. -/
def prepare (s : State α) (i : Slot) : Decision :=
  if i.malformed then .fault .illegal
  else if !qualified s i.qp then .nullified
  else if i.illegalPlacement then .fault .illegal
  else if i.reservedField then .fault .reservedField
  else if i.missingFeature || !legalTarget s i.dst then .fault .illegal
  else .execute

def commit (s : State α) (dst : Reg) (result : Word) (resultNaT : Bool) : State α :=
  { s with
    gr := update s.gr dst result
    natWord := update s.natWord (wordIndex dst)
      (IA64Integer.SDM.replaceNaT (s.natWord (wordIndex dst)) (bitIndex dst) resultNaT)
    dirty := if dst.val < 32 then s.dirty else update s.dirty dst true }

def operate (s : State α) (i : Slot) (op : Alu) : State α :=
  commit s i.dst (op.value (value s i.r2) (value s i.r3) i.imm)
    (IA64Integer.SDM.sourceNaT op.usesR2 op.usesR3 (nat s i.r2) (nat s i.r3))

end SDM

/-- The exception effect is a request with the original bundle/slot/raw
    fields. The guest delivery step is deliberately outside this relation. -/
def finish (s : State α) (i : Slot) (decision : Decision)
    (execute : State α → Slot → State α) : Outcome α :=
  match decision with
  | .execute => .committed (execute s i)
  | .nullified => .nullified s
  | .fault cause => .fault cause i.ip i.ri i.raw s

def sdStep (s : State α) (i : Slot) (op : Alu) : Outcome α :=
  finish s i (SDM.prepare s i) (fun state insn => SDM.operate state insn op)

theorem compose (s : State α) (i : Slot) (op : Alu)
    (prepare : State α → Slot → Decision) (execute : State α → Slot → State α)
    (ready : prepare s i = SDM.prepare s i)
    (effect : prepare s i = .execute → execute s i = SDM.operate s i op) :
    finish s i (prepare s i) execute = sdStep s i op := by
  unfold sdStep
  rw [← ready]
  cases h : prepare s i with
  | execute => simp [finish, effect h]
  | nullified => rfl
  | fault _ => rfl

structure Facts where
  predicate : Option Bool
  nat : Reg → Option Bool
  dirty : Reg → Bool
  checkedSof : Nat

def Sound (s : State α) (i : Slot) (f : Facts) : Prop :=
  (∀ b, f.predicate = some b → SDM.qualified s i.qp = b) ∧
  (∀ r b, f.nat r = some b → SDM.nat s r = b) ∧
  (∀ r, f.dirty r = true → s.dirty r = true) ∧
  f.checkedSof ≤ s.sof

theorem updateOther [DecidableEq α] (old : α → β) (index query : α) (v : β)
    (distinct : query ≠ index) : update old index v query = old query := by
  simp [update, distinct]

theorem commitOtherGr (s : State α) (dst other : Reg) (word : Word) (nat : Bool)
    (distinct : other ≠ dst) :
    (SDM.commit s dst word nat).gr other = s.gr other :=
  updateOther _ _ _ _ distinct

theorem commitControl (s : State α) (dst : Reg) (word : Word) (nat : Bool) :
    (SDM.commit s dst word nat).pr = s.pr ∧
    (SDM.commit s dst word nat).sof = s.sof ∧
    (SDM.commit s dst word nat).other = s.other :=
  ⟨rfl, rfl, rfl⟩

end IA64Tcg
