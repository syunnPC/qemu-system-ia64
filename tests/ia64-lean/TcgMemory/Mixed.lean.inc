/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

def byteAt (cell : InitialCell) (physical : Nat) : Option Nat :=
  if cell.address.toNat ≤ physical then
    cell.bytes[physical - cell.address.toNat]?.map BitVec.toNat
  else none

def physicalCell (p : Programs Cpu) : EventId p → InitialCell
  | .initial index => p.initial[index]
  | .access cpu index =>
    match (traces p tcgStep cpu).accesses[index] with
    | .read address bytes _ | .write address bytes _ => ⟨p.phys cpu address, bytes⟩

def byteImage (p : Programs Cpu) : TraceOrder.Forward.Image (EventId p) Nat where
  value i physical := byteAt (physicalCell p i) physical
  initial physical := (p.initial.findSome? (fun cell => byteAt cell physical)).getD 0

def AlignedFootprints (p : Programs Cpu) : Prop :=
  (∀ i : EventId p, let cell := physicalCell p i
    aligned (cell.address, cell.bytes.length)) ∧
  (∀ (i j : Fin p.initial.length) address a b,
    byteAt p.initial[i] address = some a → byteAt p.initial[j] address = some b → a = b)

-- The visibility witness assigns one time to the entire read and one
-- publication time per observer to every byte of a write. Those are
-- premises of the model. Byte selection may still combine a forwarded
-- low byte with a published high byte of one naturally aligned load.
theorem composedWbBytes (p : Programs Cpu)
    (rf co dependency : TraceOrder.Edge (EventId p))
    (visibility : TraceOrder.Visibility (EventId p) Cpu)
    (cells : AlignedFootprints p)
    (accepted : TraceOrder.Forward.IRExecution (graph p rf co dependency) visibility
      (byteImage p) (scalarOrdering (graph p rf co dependency))) :
    (∀ cpu, traces p tcgStep cpu = traces p specStep cpu) ∧
      AlignedFootprints p ∧
      TraceOrder.Forward.GuestExecution (graph p rf co dependency) visibility (byteImage p) :=
  ⟨fun cpu => threadMatches (p.regs cpu) _, cells,
    TraceOrder.Forward.refinesGuest
      (scalarGuestEdges (graph p rf co dependency) (memoryLabelOrdinary p)) accepted⟩

theorem scalarWbComposedProjection :
    ((∀ (l : Load) (s : Registers) (m : CellInput), tcgLoad l s m = specLoad l s m) ∧
      (∀ (st : Store) (s : Registers) (m : CellInput), tcgStore st s m = specStore st s m) ∧
      ((acquireMask &&& loadLoadMask) = loadLoadMask ∧
        (acquireMask &&& loadStoreMask) = loadStoreMask) ∧
      ((releaseMask &&& loadStoreMask) = loadStoreMask ∧
        (releaseMask &&& storeStoreMask) = storeStoreMask)) ∧ ComposedWb ∧
    AlignedForwardContract ∧
    (∀ {Cpu : Type} (p : Programs Cpu) (rf co dependency : TraceOrder.Edge (EventId p))
      (visibility : TraceOrder.Visibility (EventId p) Cpu),
      let x := graph p rf co dependency
      AlignedFootprints p →
      TraceOrder.Forward.IRExecution x visibility (byteImage p) (scalarOrdering x) →
      (∀ cpu, traces p tcgStep cpu = traces p specStep cpu) ∧ AlignedFootprints p ∧
        TraceOrder.Forward.GuestExecution x visibility (byteImage p)) :=
  ⟨scalarWbProjection, composedWb, composedWbForwarding, composedWbBytes⟩

private def mixedRequests : Programs Bool :=
  { regs := fun cpu =>
      ⟨fun r => if r = 4 then 64 else if cpu then 0x77 else 0xffff,
        fun _ => false, fun _ => false, fun _ => false, 0⟩
    body := fun cpu =>
      if cpu then [
        ⟨.store ⟨0, 5, 4, .w1, true, none⟩, exampleCell⟩,
        ⟨.load ⟨0, 6, 4, .w2, true, .none⟩,
          {exampleCell with guestBig := false, bytes := fun i => if i = 0 then 0x77 else 0}⟩,
        ⟨.load ⟨0, 7, 4, .w2, false, .none⟩,
          {exampleCell with guestBig := false, bytes := fun i => if i = 0 then 0x77 else 0xff}⟩]
      else [⟨.store ⟨0, 5, 4, .w2, true, none⟩, exampleCell⟩]
    phys := fun _ address => address
    initial := [] }

private def wideStore : EventId mixedRequests := .access false ⟨0, by decide⟩
private def narrowStore : EventId mixedRequests := .access true ⟨0, by decide⟩
private def localLoad : EventId mixedRequests := .access true ⟨1, by decide⟩
private def laterLoad : EventId mixedRequests := .access true ⟨2, by decide⟩

example :
    (byteImage mixedRequests).value wideStore 64 = some 0xff ∧
    (byteImage mixedRequests).value wideStore 65 = some 0xff ∧
    (byteImage mixedRequests).value narrowStore 64 = some 0x77 ∧
    (byteImage mixedRequests).value narrowStore 65 = none ∧
    (byteImage mixedRequests).value localLoad 64 = some 0x77 ∧
    (byteImage mixedRequests).value localLoad 65 = some 0 ∧
    (byteImage mixedRequests).value laterLoad 64 = some 0x77 ∧
    (byteImage mixedRequests).value laterLoad 65 = some 0xff ∧
    (traces mixedRequests tcgStep true).regs.gr 6 = 0x0077 ∧
    (traces mixedRequests tcgStep true).regs.gr 7 = 0xff77 := by decide

end TcgMemory
