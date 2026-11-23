/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Slot

abbrev Word := BitVec 64

inductive Slot where
  | first | second | third
  deriving DecidableEq

def Slot.number : Slot → Word
  | .first => 0
  | .second => 1
  | .third => 2

structure Address where
  bundle : Word
  slot : Word
  deriving DecidableEq

structure State where
  ip : Word
  psr : Word
  lastSuccess : Word
  savedPsr : Word
  savedSuppression : Word
  groupStart : Bool
  deriving DecidableEq

structure NativeRequest where
  target : Address
  source : Address
  packedSlots : Word
  conditions : Word
  syndrome : Word
  priorPsr : Word
  deriving DecidableEq

structure FaultRequest where
  code : Word
  address : Address
  raw : Word
  deriving DecidableEq

namespace SDM

-- Rev. 2.3 vol. 2 §3.3.2 defines PSR.ri in bits 42:41 and PSR.ic in bit 13.
-- §5.5.3 gives the native successor across an ordinary or an MLX slot.
def ri (psr : Word) : Word := (psr >>> 41#64) &&& 3

def withRI (psr index : Word) : Word :=
  (psr &&& ~~~(3#64 <<< 41#64)) ||| (index <<< 41#64)

def successor (bundle : Word) (slot : Slot) (long : Bool) : Address :=
  match slot with
  | .first => ⟨bundle, 1⟩
  | .second => if long then ⟨bundle + 16, 0⟩ else ⟨bundle, 2⟩
  | .third => ⟨bundle + 16, 0⟩

def completionSlot (slot : Slot) (long : Bool) : Word :=
  if slot = .second ∧ long then 2 else slot.number

def collecting (psr : Word) : Bool := (psr &&& (1#64 <<< 13#64)) != 0

-- Vol. 2 §3.3.5.6. The eventual CR22 write is made on interruption.
-- The `recorded` case permits sharing the update within the same bundle.
def success (before : State) (bundle : Word) : Word :=
  if collecting before.psr then bundle &&& ~~~15#64 else before.lastSuccess

def restart (before : State) (bundle : Word) (slot : Slot) (long : Bool) : State :=
  let next := successor bundle slot long
  { before with ip := next.bundle, psr := withRI before.psr next.slot }

def trapPayload (before : State) (bundle : Word) (slot : Slot) (long : Bool) :
    NativeRequest :=
  let next := successor bundle slot long
  let completed := completionSlot slot long
  -- Packed helper argument, with destination in bits 1:0 and source in 3:2.
  ⟨next, ⟨bundle, completed⟩, 4 * completed + next.slot, 0, 0, before.psr⟩

def singleStep (psr : Word) : Bool := (psr &&& (1#64 <<< 40#64)) != 0

def instructionDebug (psr : Word) : Bool := (psr &&& (1#64 <<< 24#64)) != 0

-- Vol. 3 tables 4-26 and 4-46 and the I18/M48 encoding diagrams.
def nopMI (raw : Word) : Bool :=
  raw.extractLsb' 37 4 == 0 && raw.extractLsb' 33 3 == 0 &&
    raw.extractLsb' 27 6 == 1 && raw.extractLsb' 26 1 == 0

def qualifier (raw : Word) : Word := (raw.extractLsb' 0 6).zeroExtend 64

def qualified (raw predicate : Word) : Bool := qualifier raw == 0 || predicate != 0

def aluDestination (raw : Word) : Word := (raw.extractLsb' 6 7).zeroExtend 64

def aluSource1 (raw : Word) : Word := (raw.extractLsb' 13 7).zeroExtend 64

def aluSource2 (raw : Word) : Word := (raw.extractLsb' 20 7).zeroExtend 64

-- Vol. 3 "add" and check_target_register. The exception number is QEMU's ABI.
def addR0Fault (raw bundle : Word) (slot : Slot) (predicate : Word) :
    Option FaultRequest :=
  if qualified raw predicate && aluDestination raw == 0 then
    some ⟨2, ⟨bundle, slot.number⟩, raw⟩
  else none

end SDM

/-- Effects at translation-block boundaries. This is a test-side TCG
    projection of the selected globals and helper call arguments. -/
inductive Register where
  | ip | psr | history | savedPsr | savedSuppression | group
  deriving DecidableEq

inductive Expr where
  | input (r : Register)
  | literal (v : Word)
  | band (left right : Expr)
  | bor (left right : Expr)
  | bxor (left right : Expr)

inductive Effect where
  | write (dst : Register) (expr : Expr)
  | ifNonzero (expr : Expr) (effect : Effect)
  | clearSuppression
  | debug (address : Address)
  | native (request : NativeRequest)
  | sequence (a b : Effect)
  | nop

inductive Event where
  | clearSuppression
  | debug (address : Address)
  | native (request : NativeRequest)
  deriving DecidableEq

def read (s : State) : Register → Word
  | .ip => s.ip
  | .psr => s.psr
  | .history => s.lastSuccess
  | .savedPsr => s.savedPsr
  | .savedSuppression => s.savedSuppression
  | .group => if s.groupStart then 1 else 0

def Expr.eval (s : State) : Expr → Word
  | .input r => read s r
  | .literal value => value
  | .band a b => a.eval s &&& b.eval s
  | .bor a b => a.eval s ||| b.eval s
  | .bxor a b => a.eval s ^^^ b.eval s

def assign (s : State) (r : Register) (v : Word) : State :=
  match r with
  | .ip => { s with ip := v }
  | .psr => { s with psr := v }
  | .history => { s with lastSuccess := v }
  | .savedPsr => { s with savedPsr := v }
  | .savedSuppression => { s with savedSuppression := v }
  | .group => { s with groupStart := v != 0 }

/-- Helper calls are observed at the TCG boundary, before helper execution.
    Advancing beyond any helper requires its separate return contract. -/
def Effect.run (s : State) : Effect → State × List Event
  | .write dst e => (assign s dst (e.eval s), [])
  | .ifNonzero e yes => if e.eval s != 0 then yes.run s else (s, [])
  | .clearSuppression => (s, [.clearSuppression])
  | .debug addr => (s, [.debug addr])
  | .native t => (s, [.native t])
  | .sequence first next =>
    let a := first.run s
    let b := next.run a.1
    (b.1, a.2 ++ b.2)
  | .nop => (s, [])

end IA64Slot
