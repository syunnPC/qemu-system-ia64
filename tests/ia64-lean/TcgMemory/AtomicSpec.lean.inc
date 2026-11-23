/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

inductive FetchWidth where
  | w4 | w8
  deriving DecidableEq

def FetchWidth.width : FetchWidth → Width
  | .w4 => .w4
  | .w8 => .w8

inductive Increment where
  | minus16 | minus8 | minus4 | minus1 | plus1 | plus4 | plus8 | plus16
  deriving DecidableEq

def Increment.value : Increment → Word
  | .minus16 => -16 | .minus8 => -8 | .minus4 => -4 | .minus1 => -1
  | .plus1 => 1 | .plus4 => 4 | .plus8 => 8 | .plus16 => 16

inductive AtomicOperand where
  | exchange (width : Width) (source : Reg)
  | fetchAdd (width : FetchWidth) (increment : Increment) (release : Bool)

def AtomicOperand.width : AtomicOperand → Width
  | .exchange w _ => w
  | .fetchAdd w _ _ => w.width

def AtomicOperand.order : AtomicOperand → TraceOrder.Order
  | .exchange _ _ => .acquire
  | .fetchAdd _ _ rel => if rel then .release else .acquire

structure AtomicInstruction where
  qp : Pred
  target : Reg
  base : Reg
  op : AtomicOperand

structure AtomicEffect where
  address : Word
  before : List Byte
  after : List Byte
  ordering : TraceOrder.Order

structure AtomicResult where
  regs : Registers
  effect : Option AtomicEffect
  exit : Exit

def atomicStop (s : Registers) (why : Exit) : AtomicResult := ⟨s, none, why⟩

def atomicDataNat (op : AtomicOperand) (s : Registers) : Bool :=
  match op with
  | .exchange _ src => getNat s src
  | .fetchAdd .. => false

-- SDM v3 XCHG (M16) and FETCHADD (M17). PRE conditions supplied by the
-- cell interface cover aligned WB memory and qualified access. Atomic
-- memory effects are paired at the access boundary.
def atomicSpec (i : AtomicInstruction) (s : Registers) (m : CellInput) : AtomicResult :=
  if !predicate s i.qp then atomicStop s .nullified
  else if !mayWrite s i.target then atomicStop s .illegalRegister
  else if getNat s i.base then atomicStop s .addressNaT
  else if atomicDataNat i.op s then atomicStop s .dataNaT
  else if !m.preflight then atomicStop s .preflight
  else if !m.primitiveReturns then atomicStop s .primitiveAbort
  else
    let address := getGR s i.base
    let width := i.op.width.bytes
    let old := unpack width m.guestBig m.bytes
    let replacement := match i.op with
      | .exchange _ source => getGR s source
      | .fetchAdd _ increment _ => old + increment.value
    let updated := setNaT (setGR s i.target old) i.target false
    ⟨updated, some ⟨address, (List.range width).map m.bytes,
      pack width m.guestBig replacement, i.op.order⟩, .success⟩

end TcgMemory
