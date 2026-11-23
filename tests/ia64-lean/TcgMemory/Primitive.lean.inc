/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

def lowByte (value : Word) (index : Nat) : Word :=
  (value >>> (8 * index)) &&& 0xff

-- TCG bswap IR reverses the low byte lanes. Narrow loads request a
-- zero-extended result and narrow stores consume only the written lanes.
def bswapPayload (w : Width) (value : Word) : Word :=
  (List.range w.bytes).foldl (fun result i =>
    result ||| (lowByte value i <<< (8 * (w.bytes - 1 - i)))) 0

def emittedLoadValue (w : Width) (layout : Nat × Bool) (m : CellInput) : Word :=
  if loadNeedsSoftwareSwap layout.2 m.hostBig m.hasMemBswap then
    bswapPayload (loadSwapWidth w) (unpack layout.1 m.hostBig m.bytes)
  else unpack layout.1 layout.2 m.bytes

def emittedStoreValue (w : Width) (layout : Nat × Bool) (m : CellInput)
    (value : Word) : List Byte :=
  if storeNeedsSoftwareSwap layout.2 m.hostBig m.hasMemBswap then
    pack layout.1 m.hostBig (bswapPayload (storeSwapWidth w) value)
  else pack layout.1 layout.2 value

inductive HelperStep where
  | lookup
  | access

structure AccessOutcome where
  returned : Bool
  touched : Bool
  deriving DecidableEq

-- A store effect survives a later abort. Reading/committing after a
-- non-returning lookup is impossible. The lookup classifier is external.
def runHelper (lookupReturns : Bool) : List HelperStep → AccessOutcome
  | [] => ⟨true, false⟩
  | .lookup :: tail => if lookupReturns then runHelper lookupReturns tail else ⟨false, false⟩
  | .access :: tail => {runHelper lookupReturns tail with touched := true}

def primitiveOutcome (plan : List HelperStep) (m : CellInput) : AccessOutcome :=
  if m.useSlowHelper then runHelper m.primitiveReturns plan
  else ⟨m.primitiveReturns, m.primitiveReturns⟩

end TcgMemory
