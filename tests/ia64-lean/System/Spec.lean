/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64.System

abbrev Word := BitVec 64
abbrev Vector := BitVec 8

-- SDM rev. 2.3 Vol. 2 Table 5-8 and §5.8.3.3.
def priority (v : Vector) : Word :=
  if v = 2 then 258 else if v = 0 then 257 else
    if v < 16 then 0 else v.zeroExtend 64 + 1

def permittedCondition (pending inService : Vector) (tpr : Word) : Prop :=
  priority inService < priority pending ∧
    (pending = 2 ∨ (tpr.getLsbD 16 = false ∧
      (pending = 0 ∨ tpr.extractLsb' 4 4 < pending.extractLsb' 4 4)))

instance (pending inService : Vector) (tpr : Word) :
    Decidable (permittedCondition pending inService tpr) := by
  unfold permittedCondition
  infer_instance

def permitted (pending inService : Vector) (tpr : Word) : Bool :=
  decide (permittedCondition pending inService tpr)

abbrev Bitmap := Array Word

-- §5.8.3.2 and §5.8.3.4. IVR clears the pending bit and marks the
-- selected vector in service. EOI retires the highest in-service vector.
def candidates : List Nat := 2 :: 0 :: (List.range 240).reverse.map (· + 16)

def active (bits : Bitmap) (v : Nat) : Bool :=
  bits[v / 64]!.getLsbD (v % 64)

def top (bits : Bitmap) : Nat :=
  (candidates.find? (active bits)).getD 15

def next (irr isr : Bitmap) (tpr : Word) : Nat :=
  let current := BitVec.ofNat 8 (top isr)
  (candidates.find? fun v =>
    active irr v && permitted (BitVec.ofNat 8 v) current tpr).getD 15

def change (bits : Bitmap) (v : Nat) (set : Bool) : Bitmap :=
  bits.mapIdx fun word value =>
    if word = v / 64 then
      let single := (1 : Word) <<< (v % 64)
      if set then value ||| single else value &&& ~~~single
    else value

def checkRow (row : String) : Except String PUnit := do
  let numbers ← (row.splitOn " ").mapM fun s =>
    match s.toNat? with
    | some n => pure n
    | none => throw s!"invalid SAPIC observation {s}"
  let [tpr, eoi, a0, a1, a2, a3, b0, b1, b2, b3, selected,
       n0, n1, n2, n3, m0, m1, m2, m3] := numbers
    | throw "invalid SAPIC observation row"
  let bitmap := Array.map (BitVec.ofNat 64)
  let irr := bitmap #[a0, a1, a2, a3]
  let isr := bitmap #[b0, b1, b2, b3]
  let v := if eoi = 1 then top isr else next irr isr (BitVec.ofNat 64 tpr)
  let expectedIrr := if v ≠ 15 && eoi = 0 then change irr v false else irr
  let expectedIsr := if v = 15 then isr else change isr v (eoi = 0)
  if selected ≠ v || bitmap #[n0, n1, n2, n3] ≠ expectedIrr ||
      bitmap #[m0, m1, m2, m3] ≠ expectedIsr then
    throw s!"SAPIC mismatch, selected {selected}, expected {v}, row {row}"
  pure .unit

def checkRows (args : List String) : IO UInt32 := do
  let [path] := args | throw (IO.userError "expected one SAPIC observation path")
  for row in ← IO.FS.lines path do
    match checkRow row with
    | .ok _ => pure ()
    | .error message => throw (IO.userError message)
  return 0

-- Vol. 3 ssm/rsm and sum/rum instruction descriptions.
def sysMask : Word := 0x00fee03e
def userMask : Word := 0x3e

def changePSR (psr imm mask : Word) (set : Bool) : Word :=
  if set then psr ||| (imm &&& mask) else psr &&& ~~~(imm &&& mask)

def changeUserPSR (psr imm : Word) (set : Bool) : Word :=
  let mask := if psr.getLsbD 20 then userMask &&& ~~~4 else userMask
  changePSR psr imm mask set

end IA64.System
