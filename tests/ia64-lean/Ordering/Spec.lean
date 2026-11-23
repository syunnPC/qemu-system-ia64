/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Ordering

inductive Access where
  | load
  | store
  deriving DecidableEq, Repr

def accesses : List Access := [.load, .store]

abbrev Mask := BitVec 8

-- Intel Itanium SDM 2.3 vol. 1 §4.4.7 and vol. 2 §4.4.7, table 4-16.
-- This model describes visibility of cacheable write-back memory in one
-- coherence domain. It omits platform acceptance (mf.a), MMIO and page walks.
namespace SDM

inductive Semantics where
  | unordered
  | release
  | acquire
  | fence
  deriving DecidableEq

structure Event where
  access : Access
  ordering : Semantics

def visibleBefore (before after : Event) : Bool :=
  before.ordering == .fence || after.ordering == .fence ||
  before.ordering == .acquire || after.ordering == .release

def requires (before after : Access) (acquire release fence : Bool) : Bool :=
  let first := if fence then Semantics.fence
               else if before == .load && acquire then .acquire else .unordered
  let second := if after == .store && release then Semantics.release else .unordered
  visibleBefore ⟨before, first⟩ ⟨after, second⟩

def plainStoreLoadAllowed : Bool := !requires .store .load false false false

-- Table 4-15 and vol. 3 Table 2-20 apply to the semaphore operation.
-- A failed comparison has a memory read and retains its release completer.
def semaphoreReleaseRequiresStoreLoad : Bool :=
  visibleBefore ⟨.store, .unordered⟩ ⟨.load, .release⟩

end SDM

-- AMD64 APM vol. 1 (24592), Memory Ordering. Applies to ordinary host
-- cacheable loads/stores and a locked instruction used as a fence.
def amd64Natural (before after : Access) : Bool :=
  !(before == .store && after == .load)

-- Armv8-A memory model guide ARM062-948681440-3279, barriers.
-- Domain assumption: every observing vCPU belongs to the inner-shareable domain.
def aarch64InstructionOrders (encoding : UInt32) (before after : Access) : Bool :=
  if encoding = 0xd50339bf then before == .load  -- DMB ISHLD
  else if encoding = 0xd5033abf then before == .store && after == .store
  else encoding == 0xd5033bbf                   -- DMB ISH

-- Four events from two threads and two locations (each written once).
-- Rank inequalities represent a strict-order witness. Inter-thread
-- read-from and from-read edges are assumptions supplied by the litmus.
theorem messagePassing (writeData writeFlag readFlag readData : Nat)
    (release : writeData < writeFlag)
    (readsFlag : writeFlag < readFlag)
    (acquire : readFlag < readData)
    (readsInitialData : readData < writeData) : False := by
  omega

theorem fencedStoreBuffering (writeX readY writeY readX : Nat)
    (fence0 : writeX < readY) (fence1 : writeY < readX)
    (readYInitial : readY < writeY) (readXInitial : readX < writeX) : False := by
  omega

theorem storeBufferingWitness :
    ∃ writeX readY writeY readX : Nat,
      readY < writeY ∧ readX < writeX ∧ readY < writeX ∧ readX < writeY := by
  exact ⟨2, 0, 3, 1, by decide⟩

-- A supported narrow comparison with an out-of-range CCV always fails
-- against the zero-extended read. This says nothing about release ordering.
theorem excessCCVAwaysFails (size ccv observed : Nat)
    (ccvExcess : 2 ^ (8 * size) ≤ ccv)
    (fitsAccess : observed < 2 ^ (8 * size)) : ccv ≠ observed := by
  omega

end IA64Ordering
