/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64ExceptionEntry

abbrev Word := BitVec 64

structure Saved where
  iip : Word
  ipsr : Word
  iipa : Word
  ifsValid : Bool
  deriving DecidableEq

structure Before where
  psr : Word
  dcr : Word
  iva : Word
  lastSuccessfulBundle : Word
  saved : Saved
  isr : Word
  cfle : Bool
  icInFlight : Bool
  ia32Transition : Bool
  nativeCompletionTrap : Bool

structure Fault where
  bundle : Word
  slot : BitVec 32
  raw : Word

structure Pending where
  ip : Word
  faultIp : Word
  faultImm : Word
  faultSlot : BitVec 32
  faultException : BitVec 32
  exception : BitVec 32
  exceptionIndex : BitVec 32
  deriving DecidableEq

structure Entry where
  ip : Word
  psr : Word
  isr : Word
  saved : Saved
  cfle : Bool
  vectorEntered : Bool
  deriving DecidableEq

structure Observation where
  ip : Word
  psr : Word
  isr : Word
  saved : Option Saved
  cfle : Bool
  vectorEntered : Bool
  deriving DecidableEq

def observe (before : Before) (entry : Entry) : Observation :=
  ⟨entry.ip, entry.psr, entry.isr, if before.icInFlight then none else some entry.saved,
    entry.cfle, entry.vectorEntered⟩

def nativeOrdinary (before : Before) : Prop :=
  before.psr.getLsbD 34 = false ∧ before.ia32Transition = false ∧
    before.nativeCompletionTrap = false

namespace SDM

-- Vol. 2 §5.5.1 and Table 3-2. The preserved bits are
-- up, mfl, mfh, pk, dt, rt, mc, and it.
def interruptionPsr (psr dcr : Word) : Word :=
  let keep : Word := 0x1808028034
  (psr &&& keep) ||| (if dcr.getLsbD 1 then 2 else 0) |||
    (if dcr.getLsbD 0 then 0x200000 else 0)

def collecting (psr : Word) : Bool := psr.getLsbD 13

def withRestartSlot (psr : Word) (slot : BitVec 2) : Word :=
  (psr.extractLsb' 43 21) ++ slot ++ (psr.extractLsb' 0 41)

-- Vol. 2 §5.5.1, Table 8-2 and §8.3 General Exception vector.
-- Ordinary ALU Illegal Operation has no non-access instruction code.
def illegalIsr (slot : BitVec 2) (nested : Bool) : Word :=
  0#21 ++ slot ++ 0#1 ++ (if nested then 1#1 else 0#1) ++ 0#39

def savedState (before : Before) (fault : Fault) : Saved :=
  if collecting before.psr then
    ⟨fault.bundle, withRestartSlot before.psr (fault.slot.setWidth 2),
      before.lastSuccessfulBundle, false⟩
  else before.saved

-- In-flight collection can save a snapshot or leave it untouched. The
-- saved fields are observed only when §5.5.1 assigns them a value.
def illegalEntry (before : Before) (fault : Fault) : Observation :=
  ⟨before.iva + 0x5400,
    interruptionPsr before.psr before.dcr,
    illegalIsr (fault.slot.setWidth 2) (!collecting before.psr || before.icInFlight),
    if before.icInFlight then none else some (savedState before fault),
    false, true⟩

end SDM

end IA64ExceptionEntry
