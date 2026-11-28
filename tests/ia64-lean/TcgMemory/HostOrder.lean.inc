/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.HostOrder

inductive Host where
  | amd64 | aarch64

def natural : Host → BitVec 8
  | .amd64 => x86Order
  | .aarch64 => armOrder

def defaultMb (host : Host) (request : BitVec 8) : Option (BitVec 8) :=
  let required := request &&& guestOrder &&& ~~~(natural host)
  if required = 0 then none else some (required ||| mbKind)

def effective (host : Host) (request : BitVec 8) : BitVec 8 :=
  natural host ||| ((defaultMb host request).getD 0 &&& pairMask)

def masks (host : Host) : DeferredAcquire.Masks :=
  ⟨acquireMask, effective host loadRequest, effective host storeRequest⟩

theorem emittedOrders :
    natural .amd64 = 0xd ∧
    natural .aarch64 = 0 ∧
    defaultMb .amd64 loadRequest = none ∧ defaultMb .amd64 storeRequest = none ∧
    defaultMb .aarch64 loadRequest = some 0x31 ∧ defaultMb .aarch64 storeRequest = some 0x3c := by
  decide

theorem effectiveOrders (host : Host) :
    effective host loadRequest &&& 0x03 = 0x01 ∧
    effective host storeRequest &&& 0x0c = 0x0c := by
  cases host <;> decide

theorem access_matches (host : Host) (write : Bool) (earlier : List DeferredAcquire.Held) :
    DeferredAcquire.canAccess (masks host) write earlier =
      DeferredAcquire.canAccess DeferredAcquire.sourceMasks write earlier := by
  cases host <;> cases write <;>
    simp [DeferredAcquire.canAccess, DeferredAcquire.sourceMasks, DeferredAcquire.defaultMask,
      masks, effective, defaultMb, natural, x86Order, armOrder, guestOrder, loadRequest,
      storeRequest, mbKind, pairMask, loadLoadMask, loadStoreMask, atomicGuestDefault,
      atomicLoadRequest, atomicStoreRequest]

private theorem complete_matches [DecidableEq Cpu] (host : Host)
    (d : DeferredAcquire.State Cpu) (cpu : Cpu) (index : Nat) :
    DeferredAcquire.complete (masks host) d cpu index =
      DeferredAcquire.complete DeferredAcquire.sourceMasks d cpu index := by
  simp only [DeferredAcquire.complete, access_matches]

private theorem execute_matches [DecidableEq Cpu] (host : Host)
    (d : DeferredAcquire.State Cpu) (cpu : Cpu) :
    DeferredAcquire.executeNow (masks host) d cpu =
      DeferredAcquire.executeNow DeferredAcquire.sourceMasks d cpu := by
  simp only [DeferredAcquire.executeNow, access_matches]

theorem run_matches [DecidableEq Cpu] (host : Host) (s : DeferredAcquire.State Cpu)
    (actions : List (DeferredAcquire.Action Cpu)) :
    DeferredAcquire.run (masks host) s actions =
      DeferredAcquire.run DeferredAcquire.sourceMasks s actions := by
  induction actions generalizing s with
  | nil => rfl
  | cons action rest ih =>
    cases action <;>
      simp only [DeferredAcquire.run, complete_matches, execute_matches, ih]

def Contract : Prop :=
    (natural .amd64 = 0xd ∧
      natural .aarch64 = 0 ∧
      defaultMb .amd64 loadRequest = none ∧ defaultMb .amd64 storeRequest = none ∧
      defaultMb .aarch64 loadRequest = some 0x31 ∧ defaultMb .aarch64 storeRequest = some 0x3c) ∧
    (∀ host, effective host loadRequest &&& 0x03 = 0x01 ∧
      effective host storeRequest &&& 0x0c = 0x0c) ∧
    (∀ host write earlier, DeferredAcquire.canAccess (masks host) write earlier =
      DeferredAcquire.canAccess DeferredAcquire.sourceMasks write earlier) ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (host : Host) (s : DeferredAcquire.State Cpu)
      (actions : List (DeferredAcquire.Action Cpu)),
      DeferredAcquire.run (masks host) s actions =
        DeferredAcquire.run DeferredAcquire.sourceMasks s actions)

theorem verified : Contract :=
  ⟨emittedOrders, effectiveOrders, access_matches, @run_matches⟩

end TcgMemory.HostOrder
