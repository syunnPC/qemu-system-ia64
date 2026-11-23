/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64.Decode

abbrev Word := BitVec 64

-- SDM Vol. 1 Figure 3-15 and Vol. 3 Figure 4-1.
def bundle (low high : Word) : BitVec 128 := high ++ low

def templateCode (low : Word) : BitVec 5 := low.extractLsb' 0 5

def slot0 (low high : Word) : BitVec 41 := (bundle low high).extractLsb' 5 41
def slot1 (low high : Word) : BitVec 41 := (bundle low high).extractLsb' 46 41
def slot2 (low high : Word) : BitVec 41 := (bundle low high).extractLsb' 87 41

inductive Unit where
  | reserved | m | i | b | f | l | x
  deriving DecidableEq, Repr, Inhabited

structure Template where
  defined : Bool
  name : String
  units : Unit × Unit × Unit
  stops : Bool × Bool × Bool
  deriving DecidableEq, Repr, Inhabited

-- SDM Vol. 1 Table 3-10. For defined encodings the odd member of each
-- pair places a stop after slot 2.
def template (n : Nat) : Template :=
  let last := n % 2 == 1
  match n / 2 with
  | 0  => ⟨true, "MII", (.m, .i, .i), (false, false, last)⟩
  | 1  => ⟨true, "MII", (.m, .i, .i), (false, true, last)⟩
  | 2  => ⟨true, "MLX", (.m, .l, .x), (false, false, last)⟩
  | 4  => ⟨true, "MMI", (.m, .m, .i), (false, false, last)⟩
  | 5  => ⟨true, "MMI", (.m, .m, .i), (true, false, last)⟩
  | 6  => ⟨true, "MFI", (.m, .f, .i), (false, false, last)⟩
  | 7  => ⟨true, "MMF", (.m, .m, .f), (false, false, last)⟩
  | 8  => ⟨true, "MIB", (.m, .i, .b), (false, false, last)⟩
  | 9  => ⟨true, "MBB", (.m, .b, .b), (false, false, last)⟩
  | 11 => ⟨true, "BBB", (.b, .b, .b), (false, false, last)⟩
  | 12 => ⟨true, "MMB", (.m, .m, .b), (false, false, last)⟩
  | 14 => ⟨true, "MFB", (.m, .f, .b), (false, false, last)⟩
  | _  => ⟨false, "reserved", (.reserved, .reserved, .reserved),
             (false, false, false)⟩

-- SDM Vol. 3 Tables 4-4 and 4-70. Displacements are relative to IP.
def major (raw : Word) : Word := (raw.extractLsb' 37 4).zeroExtend 64

def imm21 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 6 20).zeroExtend 64

def imm14 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 27 6 ++ raw.extractLsb' 13 7).signExtend 64

def imm8 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 13 7).signExtend 64

def mask17 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 24 8 ++ raw.extractLsb' 6 7 ++
    (0 : BitVec 1)).signExtend 64

def imm44 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 6 27 ++ (0 : BitVec 16)).signExtend 64

def imm24 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 31 2 ++ raw.extractLsb' 6 21).zeroExtend 64

def imm22 (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 22 5 ++ raw.extractLsb' 27 9 ++
    raw.extractLsb' 13 7).signExtend 64

def branchDisplacement (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 13 20 ++ (0 : BitVec 4)).signExtend 64

def checkDisplacement (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 20 13 ++ raw.extractLsb' 6 7 ++
    (0 : BitVec 4)).signExtend 64

def imm62 (l x : Word) : Word :=
  (l.extractLsb' 0 41 ++ x.extractLsb' 36 1 ++ x.extractLsb' 6 20).zeroExtend 64

def imm64 (l x : Word) : Word :=
  x.extractLsb' 36 1 ++ l.extractLsb' 0 41 ++ x.extractLsb' 21 1 ++
    x.extractLsb' 22 5 ++ x.extractLsb' 27 9 ++ x.extractLsb' 13 7

def longDisplacement (l x : Word) : Word :=
  x.extractLsb' 36 1 ++ l.extractLsb' 2 39 ++ x.extractLsb' 13 20 ++ (0 : BitVec 4)

def field (raw : Word) (low width : Nat) : Word :=
  (raw.extractLsb' low width).zeroExtend 64

def loadInc (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 27 1 ++ raw.extractLsb' 13 7).signExtend 64

def storeInc (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 27 1 ++ raw.extractLsb' 6 7).signExtend 64

def fetchaddInc (raw : Word) : Word :=
  let magnitude := match (raw.extractLsb' 13 2).toNat with
    | 0 => 16 | 1 => 8 | 2 => 4 | _ => 1
  if raw.getLsbD 15 then -magnitude else magnitude

def fpCheckDisplacement (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 6 20 ++ (0 : BitVec 4)).signExtend 64

def checkRow (checks : Array (Word → Word → Array Word)) (row : String) :
    Except String PUnit := do
  let numbers ← (row.splitOn " ").mapM fun s =>
    match s.toNat? with
    | some n => pure n
    | none => throw s!"invalid observation number {s}"
  let idx :: raw :: left :: fields := numbers | throw "invalid observation row"
  let some expected := checks[idx]? | throw "unknown observation check"
  let actual := fields.toArray.map (BitVec.ofNat 64)
  let spec := expected (BitVec.ofNat 64 raw) (BitVec.ofNat 64 left)
  if actual != spec then
    throw s!"case {idx}, raw={raw}, L={left}, expected {spec}, got {actual}"
  pure .unit

def checkRows (checks : Array (Word → Word → Array Word)) (args : List String) :
    IO UInt32 := do
  let [path] := args | throw (IO.userError "expected one decoder observation path")
  for row in ← IO.FS.lines path do
    match checkRow checks row with
    | .ok _ => pure ()
    | .error message => throw (IO.userError message)
  return 0

end IA64.Decode
