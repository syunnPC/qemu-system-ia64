/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Optimizer

local notation "Word" => BitVec 64

inductive Opcode where
  | add | sub | and | or | xor
  deriving DecidableEq

def eval {width : Nat} : Opcode → BitVec width → BitVec width → BitVec width
  | .add => (· + ·)
  | .sub => (· - ·)
  | .and => (· &&& ·)
  | .or => (· ||| ·)
  | .xor => (· ^^^ ·)

structure Masks where
  mayOne : Word
  mustOne : Word

def Masks.sound (m : Masks) (value : Word) : Prop :=
  value &&& ~~~m.mayOne = 0 ∧ m.mustOne &&& ~~~value = 0

def Masks.known (m : Masks) : Bool := m.mayOne == m.mustOne

def Masks.constant (value : Word) : Masks := ⟨value, value⟩

def Masks.unknown : Masks := ⟨~~~0#64, 0⟩

theorem constantSound (v : Word) : (Masks.constant v).sound v := by
  simp only [Masks.constant, Masks.sound]
  bv_decide

theorem unknownSound (v : Word) : Masks.unknown.sound v := by
  simp [Masks.unknown, Masks.sound]

theorem readKnown (v : Word) (m : Masks) (sound : m.sound v) (const : m.known = true) :
    v = m.mayOne := by
  simp only [Masks.known, beq_iff_eq] at const
  simp only [Masks.sound] at sound
  bv_decide

def lowMask (count : Word) : Word := (1#64 <<< count) - 1

/-- Contract of the selected C `ctz64` builtin, including its zero case. -/
def TrailingZeros (value count : Word) : Prop :=
  (value = 0 ∧ count = 64) ∨
    (count < 64 ∧ value &&& lowMask count = 0 ∧ (value >>> count) &&& 1 = 1)

theorem extractLowContiguous (mask count : Word)
    (contiguous : mask &&& (mask + 1) = 0)
    (scan : TrailingZeros (~~~mask) count) : mask = lowMask count := by
  rcases scan with ⟨zero, width⟩ | ⟨bound, below, bit⟩
  · subst count
    simp only [lowMask] at *
    bv_decide
  · simp only [lowMask] at *
    bv_decide

inductive Expression where
  | binary (opcode : Opcode)
  | literal (value : Word)
  | first
  | negSecond
  | complementFirst
  | addImmediate (value : Word)
  | extractFirst (count : Word)
  | shiftLeft
  | shiftRight

def Expression.value (first second : Word) : Expression → Word
  | .binary op => eval op first second
  | .literal value => value
  | .first => first
  | .negSecond => -second
  | .complementFirst => ~~~first
  | .addImmediate value => first + value
  | .extractFirst count => first &&& lowMask count
  | .shiftLeft => first <<< (second &&& 63)
  | .shiftRight => first >>> (second &&& 63)

structure Rewrite where
  expression : Expression
  result : Masks

def literal (value : Word) : Rewrite := ⟨.literal value, .constant value⟩
def identity (mask : Masks) : Rewrite := ⟨.first, mask⟩
def keep (op : Opcode) (mask : Masks) : Rewrite := ⟨.binary op, mask⟩

structure Info where
  x : Masks
  y : Masks
  copies : Bool
  hasNot : Bool
  extractSupported : Bool
  extractLength : Word

def Info.sound (facts : Info) (a b : Word) : Prop :=
  facts.x.sound a ∧ facts.y.sound b ∧ (facts.copies = true → a = b) ∧
    (facts.extractSupported = true →
      TrailingZeros (~~~facts.y.mayOne) facts.extractLength)

def State (Temp : Type) := Temp → Word

def write [DecidableEq Temp] (s : State Temp) (dst : Temp) (value : Word) : State Temp :=
  fun temp => if temp = dst then value else s temp

theorem writeSame [DecidableEq Temp] (s : State Temp) (dst : Temp) : write s dst (s dst) = s := by
  funext t
  by_cases alias : t = dst
  · simp [write, alias]
  · simp [write, alias]

inductive Operation (Temp : Type) where
  | binary (op : Opcode) (dst first second : Temp)
  | replace (expr : Expression) (dst first second : Temp)
  | nop
  | observe (id : Nat) (value : Temp)

structure Observation where
  id : Nat
  word : Word

def Operation.run [DecidableEq Temp] (s : State Temp) :
    Operation Temp → State Temp × List Observation
  | .binary op dst x y => (write s dst (eval op (s x) (s y)), [])
  | .replace expr dst x y => (write s dst (expr.value (s x) (s y)), [])
  | .nop => (s, [])
  | .observe id temp => (s, [⟨id, s temp⟩])

def run [DecidableEq Temp] (s : State Temp) :
    List (Operation Temp) → State Temp × List Observation
  | [] => (s, [])
  | op :: rest =>
    let a := op.run s
    let b := run a.1 rest
    (b.1, a.2 ++ b.2)

end IA64Optimizer
