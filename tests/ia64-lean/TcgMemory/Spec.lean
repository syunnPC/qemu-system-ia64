/- SPDX-License-Identifier: GPL-2.0-or-later -/
import TraceOrder.Forward
import Std.Tactic.BVDecide

set_option warningAsError true

namespace TcgMemory

abbrev Word := BitVec 64
abbrev Byte := BitVec 8
abbrev Reg := Fin 128
abbrev Pred := Fin 64

inductive Width where
  | w1 | w2 | w4 | w8
  deriving DecidableEq

def Width.bytes : Width → Nat
  | .w1 => 1
  | .w2 => 2
  | .w4 => 4
  | .w8 => 8

structure Registers where
  gr : Reg → Word
  nat : Reg → Bool
  pr : Pred → Bool
  dirty : Reg → Bool
  sof : Nat

def getGR (s : Registers) (r : Reg) : Word :=
  if r = 0 then 0 else s.gr r

def getNat (s : Registers) (r : Reg) : Bool :=
  if r = 0 then false else s.nat r

def predicate (s : Registers) (p : Pred) : Bool :=
  if p = 0 then true else s.pr p

-- Intel SDM v3 §3, check_target_register.
def mayWrite (s : Registers) (r : Reg) : Bool :=
  decide (r ≠ 0 ∧ (r.val < 32 ∨ r.val - 32 < s.sof))

def setGR (s : Registers) (r : Reg) (value : Word) : Registers :=
  { s with gr := fun i => if i = r then value else s.gr i
           dirty := fun i => if i = r ∧ 32 ≤ r.val then true else s.dirty i }

def setNaT (s : Registers) (r : Reg) (value : Bool) : Registers :=
  { s with nat := fun i => if i = r then value else s.nat i }

inductive Update where
  | none | immediate (value : Word) | register (index : Reg)
  deriving DecidableEq

structure Load where
  qp : Pred
  target : Reg
  base : Reg
  width : Width
  acquire : Bool
  update : Update

structure Store where
  qp : Pred
  source : Reg
  base : Reg
  width : Width
  release : Bool
  update : Option Word

-- One naturally aligned WB cell. Address checks and delivery of
-- interruptions are external. For the single-page slow helper,
-- primitiveReturns gives the qualified TLB lookup result. For a direct
-- primitive it supplies the abstract successful/aborting operation.
structure CellInput where
  guestBig : Bool
  hostBig : Bool
  bytes : Nat → Byte
  preflight : Bool
  primitiveReturns : Bool
  hasMemBswap : Bool
  useSlowHelper : Bool

def significance (bytes index : Nat) (bigEndian : Bool) : Nat :=
  if bigEndian then bytes - 1 - index else index

-- Intel SDM v3 ld and st. The memory projection has an unsigned load
-- and stores just the low bits selected by the width.
def unpack (width : Nat) (big : Bool) (bytes : Nat → Byte) : Word :=
  (List.range width).foldl
    (fun acc i => acc ||| ((bytes i).zeroExtend 64 <<< (8 * significance width i big))) 0

def pack (width : Nat) (big : Bool) (value : Word) : List Byte :=
  (List.range width).map (fun i => (value >>> (8 * significance width i big)).truncate 8)

theorem unpackByte (big : Bool) (bytes : Nat → Byte) :
    unpack 1 big bytes = unpack 1 false bytes := by
  cases big <;> rfl

theorem packByte (big : Bool) (value : Word) :
    pack 1 big value = pack 1 false value := by
  cases big <;> rfl

inductive Effect where
  | read (address : Word) (bytes : List Byte) (acquire : Bool)
  | write (address : Word) (bytes : List Byte) (release : Bool)

def readOrder (aq : Bool) : TraceOrder.Order := if aq then .acquire else .plain
def writeOrder (rl : Bool) : TraceOrder.Order := if rl then .release else .plain

-- Locations identify the single cells in this projection, with their
-- byte layout carried by the memory effect.
def event (cpu : Cpu) : Effect → TraceOrder.Access Cpu (Word × Nat)
  | .read address bytes aq =>
    let little := unpack bytes.length false (fun i => bytes[i]?.getD 0)
    ⟨some cpu, some (address, bytes.length), .load, readOrder aq, little.toNat⟩
  | .write address bytes rl =>
    let little := unpack bytes.length false (fun i => bytes[i]?.getD 0)
    ⟨some cpu, some (address, bytes.length), .store, writeOrder rl, little.toNat⟩

inductive Exit where
  | nullified | illegalRegister | addressNaT | dataNaT | preflight | primitiveAbort | success
  deriving DecidableEq

structure Result where
  regs : Registers
  effect : Option Effect
  exit : Exit

def stop (s : Registers) (why : Exit) : Result := ⟨s, none, why⟩

def legalLoad (l : Load) (s : Registers) : Bool :=
  mayWrite s l.target &&
    (l.update == .none || (mayWrite s l.base && l.base != l.target))

def legalStore (st : Store) (s : Registers) : Bool :=
  st.update.isNone || mayWrite s st.base

-- Intel SDM v3 ld, M2/M3 counting the increment's old value and old NaT,
-- even when the increment register coincides with the load destination.
def specLoad (l : Load) (s : Registers) (m : CellInput) : Result :=
  if !predicate s l.qp then stop s .nullified
  else if !legalLoad l s then stop s .illegalRegister
  else if getNat s l.base then stop s .addressNaT
  else if !m.preflight then stop s .preflight
  else if !m.primitiveReturns then stop s .primitiveAbort
  else
    let address := getGR s l.base
    let value := unpack l.width.bytes m.guestBig m.bytes
    let written := setNaT (setGR s l.target value) l.target false
    let updated := match l.update with
      | .none => written
      | .immediate d => setGR written l.base (address + d)
      | .register i =>
        setNaT (setGR written l.base (address + getGR s i)) l.base
          (getNat s l.base || getNat s i)
    ⟨updated, some (.read address ((List.range l.width.bytes).map m.bytes) l.acquire), .success⟩

-- Intel SDM v3 st, M5/M6. The stored value is taken before any update of
-- the address register, which may also be the instruction's data source.
def specStore (st : Store) (s : Registers) (m : CellInput) : Result :=
  if !predicate s st.qp then stop s .nullified
  else if !legalStore st s then stop s .illegalRegister
  else if getNat s st.base then stop s .addressNaT
  else if getNat s st.source then stop s .dataNaT
  else if !m.preflight then stop s .preflight
  else if !m.primitiveReturns then stop s .primitiveAbort
  else
    let address := getGR s st.base
    let bytes := pack st.width.bytes m.guestBig (getGR s st.source)
    let updated := match st.update with
      | none => s
      | some d => setNaT (setGR s st.base (address + d)) st.base false
    ⟨updated, some (.write address bytes st.release), .success⟩

end TcgMemory
