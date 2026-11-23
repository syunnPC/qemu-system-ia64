/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

def instructionSlot : Option MemoryRecord → Option Nat
  | some (.scalar slot _) | some (.atomicRead slot _ _ _) | some (.atomicWrite slot _ _ _) =>
    some slot
  | none => none

def helperContract (op : AtomicOperand) (path : AtomicPath) : Bool :=
  match path, op with
  | .serial, _ => false
  | _, .exchange .. => exchangeSeqCst
  | .nativeHelper, .fetchAdd .. => fetchAddSeqCst
  | .reverseHelper, .fetchAdd .. => compareExchangeSeqCst

-- QEMU's portable C11 RMW contract gives acquire to the read phase
-- and release to the write phase. It is not a full surrounding barrier.
def helperAfterMask : Option MemoryRecord → BitVec 8
  | some (.atomicRead _ op path _) =>
    if helperContract op path then loadLoadMask ||| loadStoreMask else 0
  | _ => 0

def helperBeforeMask : Option MemoryRecord → BitVec 8
  | some (.atomicWrite _ op path _) =>
    if helperContract op path then loadStoreMask ||| storeStoreMask else 0
  | _ => 0

def reverseLeadingMask : Option MemoryRecord → BitVec 8
  | some (.atomicRead _ (.fetchAdd ..) .reverseHelper _)
  | some (.atomicWrite _ (.fetchAdd ..) .reverseHelper _) => atomicReversePrefixMask
  | _ => 0

def memoryRequestMask : Option MemoryRecord → BitVec 8
  | some (.scalar _ (.read ..)) | some (.atomicRead _ _ .serial _) => atomicLoadRequest
  | some (.scalar _ (.write ..)) | some (.atomicWrite _ _ .serial _) => atomicStoreRequest
  | _ => 0

def effectiveAtomicMask (p : AtomicPrograms Cpu) (implicitOrder : BitVec 8)
    (a b : AtomicEventId p) : BitVec 8 :=
  let left := atomicEventRecord p a
  let right := atomicEventRecord p b
  let qualifiers :=
    (if (atomicEventLabel p a).order = .acquire then acquireMask else 0) |||
      (if (atomicEventLabel p b).order = .release then releaseMask else 0)
  let defaultRequest := memoryRequestMask right &&& atomicGuestDefault &&& ~~~implicitOrder
  implicitOrder ||| defaultRequest ||| qualifiers ||| helperAfterMask left ||| helperBeforeMask right |||
    reverseLeadingMask right

def atomicObservationOrder (p : AtomicPrograms Cpu) (implicitOrder : BitVec 8)
    (a b : AtomicEventId p) : Prop :=
  issuedPair p a b ∨
    (atomicProgramOrder p a b ∧
     instructionSlot (atomicEventRecord p a) ≠ instructionSlot (atomicEventRecord p b) ∧
     (effectiveAtomicMask p implicitOrder a b &&&
       atomicPairMask (atomicEventLabel p a).kind (atomicEventLabel p b).kind) ≠ 0)

-- This includes all source-derived pairs for the selected helper
-- phases. Other phase pairs must be checked with the actual qualifier
-- and default masks before claiming coverage of an entire graph.
theorem helperReadsOrderFollowing (op : AtomicOperand) (path : AtomicPath)
    (slot : Nat) (effect : AtomicEffect) (following : TraceOrder.Kind)
    (active : helperContract op path = true) :
    (helperAfterMask (some (.atomicRead slot op path effect)) &&&
      atomicPairMask .rmwRead following) ≠ 0 := by
  cases following <;>
    simp [helperAfterMask, atomicPairMask, TraceOrder.Kind.write, active,
      loadLoadMask, loadStoreMask]

theorem helperWritesOrderPreceding (op : AtomicOperand) (path : AtomicPath)
    (slot : Nat) (effect : AtomicEffect) (preceding : TraceOrder.Kind)
    (active : helperContract op path = true) :
    (helperBeforeMask (some (.atomicWrite slot op path effect)) &&&
      atomicPairMask preceding .rmwWrite) ≠ 0 := by
  cases preceding <;>
    simp [helperBeforeMask, atomicPairMask, TraceOrder.Kind.write, active,
      loadStoreMask, storeStoreMask]

theorem helperOrdersCurrentOperations (op : AtomicOperand) (path : AtomicPath) :
    helperContract op path = decide (path ≠ .serial) := by
  cases op <;> cases path <;> rfl

-- Only applies once every required guest edge is covered by the
-- actual per-instruction masks. An uncovered edge is not filled in.
theorem atomicGraphWithCoveredEdges (p : AtomicPrograms Cpu)
    (rf co dependency : TraceOrder.Edge (AtomicEventId p))
    (v : TraceOrder.Visibility (AtomicEventId p) Cpu) (implicitOrder : BitVec 8)
    (coverage : ∀ a b, TraceOrder.GuestOrder (atomicGraph p rf co dependency) a b →
      atomicObservationOrder p implicitOrder a b)
    (ir : TraceOrder.IRExecution (atomicGraph p rf co dependency) v
      (atomicObservationOrder p implicitOrder)) :
    TraceOrder.GuestExecution (atomicGraph p rf co dependency) v :=
  TraceOrder.refinesGuest _ _ _ coverage ir

def AtomicCoreContract : Prop :=
    (∀ i s m parallel, tcgAtomic i s m parallel = atomicSpec i s m) ∧
    (∀ i s m parallel, (tcgAtomic i s m parallel).exit ≠ .success →
      (tcgAtomic i s m parallel).regs = s ∧ (tcgAtomic i s m parallel).effect = none) ∧
    (∀ op path, helperContract op path = decide (path ≠ .serial)) ∧
    (∀ (op : AtomicOperand) (path : AtomicPath) (slot : Nat) (effect : AtomicEffect)
      (other : TraceOrder.Kind), helperContract op path = true →
        (helperAfterMask (some (.atomicRead slot op path effect)) &&&
          atomicPairMask .rmwRead other) ≠ 0 ∧
        (helperBeforeMask (some (.atomicWrite slot op path effect)) &&&
          atomicPairMask other .rmwWrite) ≠ 0) ∧
    (∀ (width : FetchWidth) (increment : Increment) (initialRaw : Word)
      (observations : List Word) (old next : Word),
      reverseFetchAddAttempts width increment initialRaw observations = some (old, next) →
        old ∈ observations ∧
        next = bswapPayload width.width (bswapPayload width.width old + increment.value)) ∧
    (∀ s slot requests, runMemory tcgStep tcgAtomic s slot requests =
      runMemory specStep (fun i r m _ => atomicSpec i r m) s slot requests)

theorem atomicCoreContract : AtomicCoreContract :=
  ⟨atomicMatches, atomicAbort, helperOrdersCurrentOperations,
    fun _ _ _ _ _ active =>
      ⟨helperReadsOrderFollowing _ _ _ _ _ active, helperWritesOrderPreceding _ _ _ _ _ active⟩,
    reverseFetchAddCommits, memoryThreadMatches⟩

private def threeDefaultPairs : BitVec 8 := loadLoadMask ||| loadStoreMask ||| storeStoreMask

private def aroundSemaphore (op : AtomicOperand) (reverse : Bool) : AtomicPrograms Unit :=
  ⟨fun _ => exampleRegs,
    fun _ => [
      .scalar ⟨.store ⟨0, 6, 4, .w1, false, none⟩, exampleCell⟩,
      .semaphore ⟨0, 5, 4, op⟩ {exampleCell with guestBig := reverse} true,
      .scalar ⟨.load ⟨0, 7, 4, .w1, false, .none⟩, exampleCell⟩],
    fun _ addr => addr, []⟩

private def exchangeProgram := aroundSemaphore exchangeExample.op false
private def nativeReleaseProgram := aroundSemaphore fetchExample.op false
private def reverseReleaseProgram := aroundSemaphore fetchExample.op true

private def pairPos {p : AtomicPrograms Unit} (n : Nat)
    (bound : n < (atomicTrace p ()).accesses.length) : AtomicEventId p :=
  .access () ⟨n, bound⟩

private def maskAt {p : AtomicPrograms Unit}
    (a b : AtomicEventId p) : BitVec 8 :=
  effectiveAtomicMask p threeDefaultPairs a b &&&
    atomicPairMask (atomicEventLabel p a).kind (atomicEventLabel p b).kind

-- These equations characterize the source mask in the indicated
-- contract. A zero is an unresolved abstract edge, not an execution.
example : maskAt (pairPos (p := exchangeProgram) 1 (by decide)) (pairPos 3 (by decide)) =
    loadLoadMask := by decide
example : maskAt (pairPos (p := exchangeProgram) 2 (by decide)) (pairPos 3 (by decide)) =
    0 := by decide
example : maskAt (pairPos (p := nativeReleaseProgram) 0 (by decide)) (pairPos 1 (by decide)) =
    0 := by decide
example : maskAt (pairPos (p := nativeReleaseProgram) 0 (by decide)) (pairPos 2 (by decide)) =
    storeStoreMask := by decide
example : maskAt (pairPos (p := reverseReleaseProgram) 0 (by decide)) (pairPos 1 (by decide)) =
    storeLoadMask := by decide

example :
    helperAfterMask (atomicEventRecord (oneExchange false) (exchangeRead false)) = 0 ∧
      helperBeforeMask (atomicEventRecord (oneExchange false) (exchangeWrite false)) = 0 :=
  ⟨rfl, rfl⟩

end TcgMemory
