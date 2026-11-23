/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

inductive MemoryRequest where
  | scalar (request : Request)
  | semaphore (instruction : AtomicInstruction) (cell : CellInput) (parallel : Bool)

inductive AtomicPath where
  | serial | nativeHelper | reverseHelper
  deriving DecidableEq

def chosenAtomicPath (i : AtomicInstruction) (m : CellInput) (parallel : Bool) : AtomicPath :=
  if !parallel then .serial
  else if (tcgLayout (atomicDescriptor i.op) m.hostBig m.guestBig).2 != m.hostBig then
    .reverseHelper
  else .nativeHelper

-- Both phases are created together with the request's dynamic
-- position. The parallel flag records the emitter route actually
-- selected for this request.
inductive MemoryRecord where
  | scalar (slot : Nat) (effect : Effect)
  | atomicRead (slot : Nat) (op : AtomicOperand) (path : AtomicPath) (effect : AtomicEffect)
  | atomicWrite (slot : Nat) (op : AtomicOperand) (path : AtomicPath) (effect : AtomicEffect)

structure MemoryThread where
  regs : Registers
  accesses : List MemoryRecord
  fault : Option Exit

abbrev SemaphoreStep := AtomicInstruction → Registers → CellInput → Bool → AtomicResult

def memoryStep (scalar : Step) (atomic : SemaphoreStep) (s : Registers) (slot : Nat) :
    MemoryRequest → Registers × Exit × List MemoryRecord
  | .scalar request =>
    let result := scalar s request
    (result.regs, result.exit, result.effect.toList.map (MemoryRecord.scalar slot))
  | .semaphore i m parallel =>
    let result := atomic i s m parallel
    let path := chosenAtomicPath i m parallel
    let records := result.effect.toList.flatMap (fun effect =>
      [.atomicRead slot i.op path effect, .atomicWrite slot i.op path effect])
    (result.regs, result.exit, records)

def runMemory (scalar : Step) (atomic : SemaphoreStep) (s : Registers) (slot : Nat) :
    List MemoryRequest → MemoryThread
  | [] => ⟨s, [], none⟩
  | request :: tail =>
    let (regs, exit, accesses) := memoryStep scalar atomic s slot request
    if exit == .success || exit == .nullified then
      let after := runMemory scalar atomic regs (slot + 1) tail
      {after with accesses := accesses ++ after.accesses}
    else ⟨regs, accesses, some exit⟩

theorem memoryThreadMatches (s : Registers) (slot : Nat) (requests : List MemoryRequest) :
    runMemory tcgStep tcgAtomic s slot requests =
      runMemory specStep (fun i r m _ => atomicSpec i r m) s slot requests := by
  induction requests generalizing s slot with
  | nil => rfl
  | cons head tail ih =>
    cases head <;> simp [runMemory, memoryStep, stepMatches, atomicMatches, ih]

structure AtomicPrograms (Cpu : Type) where
  regs : Cpu → Registers
  requests : Cpu → List MemoryRequest
  phys : Cpu → Word → Word
  initial : List InitialCell

def atomicTrace (p : AtomicPrograms Cpu) (cpu : Cpu) : MemoryThread :=
  runMemory tcgStep tcgAtomic (p.regs cpu) 0 (p.requests cpu)

inductive AtomicEventId (p : AtomicPrograms Cpu) where
  | initial (index : Fin p.initial.length)
  | access (cpu : Cpu) (index : Fin (atomicTrace p cpu).accesses.length)

def atomicEventRecord (p : AtomicPrograms Cpu) : AtomicEventId p → Option MemoryRecord
  | .initial _ => none
  | .access cpu index => some (atomicTrace p cpu).accesses[index]

def parallelOp (p : AtomicPrograms Cpu) (id : AtomicEventId p) : Option AtomicOperand :=
  match atomicEventRecord p id with
  | some (.atomicRead _ op .nativeHelper _) | some (.atomicRead _ op .reverseHelper _)
  | some (.atomicWrite _ op .nativeHelper _) | some (.atomicWrite _ op .reverseHelper _) => some op
  | _ => none

def atomicEventLabel (p : AtomicPrograms Cpu) :
    AtomicEventId p → TraceOrder.Access Cpu Location
  | .initial i =>
    let cell := p.initial[i]
    let value := unpack cell.bytes.length false (fun j => cell.bytes[j]?.getD 0)
    ⟨none, some (cell.address, cell.bytes.length), .store, .plain, value.toNat⟩
  | .access cpu index =>
    let label := match (atomicTrace p cpu).accesses[index] with
      | .scalar _ effect => event cpu effect
      | .atomicRead _ _ _ effect => (atomicLabels cpu effect).1
      | .atomicWrite _ _ _ effect => (atomicLabels cpu effect).2
    {label with location := label.location.map (fun (addr, size) => (p.phys cpu addr, size))}

def atomicPosition (p : AtomicPrograms Cpu) : AtomicEventId p → Option (Cpu × Nat)
  | .initial _ => none
  | .access cpu index => some (cpu, index.val)

def atomicProgramOrder (p : AtomicPrograms Cpu) (a b : AtomicEventId p) : Prop :=
  ∃ cpu i j, atomicPosition p a = some (cpu, i) ∧
    atomicPosition p b = some (cpu, j) ∧ i < j

def issuedPair (p : AtomicPrograms Cpu) (a b : AtomicEventId p) : Prop :=
  atomicProgramOrder p a b ∧
    match atomicEventRecord p a, atomicEventRecord p b with
    | some (.atomicRead i opA parallelA effectA), some (.atomicWrite j opB parallelB effectB) =>
      i = j ∧ opA = opB ∧ parallelA = parallelB ∧ effectA = effectB
    | _, _ => False

def atomicGraph (p : AtomicPrograms Cpu) (rf co dependency : TraceOrder.Edge (AtomicEventId p)) :
    TraceOrder.Execution (AtomicEventId p) Cpu Location :=
  ⟨atomicEventLabel p, atomicProgramOrder p, dependency, rf, co, issuedPair p⟩

theorem atomicEventNoFence (p : AtomicPrograms Cpu) (id : AtomicEventId p) :
    (atomicEventLabel p id).kind ≠ .fence := by
  cases id with
  | initial index => simp [atomicEventLabel]
  | access cpu index =>
    cases record : (atomicTrace p cpu).accesses[index] with
    | scalar _ eff => cases eff <;> simp [atomicEventLabel, record, event]
    | atomicRead _ _ _ _ => simp [atomicEventLabel, record, atomicLabels]
    | atomicWrite _ _ _ _ => simp [atomicEventLabel, record, atomicLabels]

private def oneExchange (parallel : Bool) : AtomicPrograms Unit :=
  ⟨fun _ => exampleRegs, fun _ => [.semaphore exchangeExample exampleCell parallel],
    fun _ addr => addr, []⟩

private def exchangeRead (parallel : Bool) : AtomicEventId (oneExchange parallel) :=
  .access () ⟨0, by cases parallel <;> decide⟩

private def exchangeWrite (parallel : Bool) : AtomicEventId (oneExchange parallel) :=
  .access () ⟨1, by cases parallel <;> decide⟩

-- The labels alone do not authorize helper ordering. The two dynamic
-- requests give the same labels and pair but only one uses the helper.
example :
    atomicEventLabel (oneExchange true) (exchangeRead true) =
      atomicEventLabel (oneExchange false) (exchangeRead false) ∧
    atomicEventLabel (oneExchange true) (exchangeWrite true) =
      atomicEventLabel (oneExchange false) (exchangeWrite false) ∧
    parallelOp (oneExchange true) (exchangeRead true) = some exchangeExample.op ∧
    parallelOp (oneExchange true) (exchangeWrite true) = some exchangeExample.op ∧
    parallelOp (oneExchange false) (exchangeRead false) = none ∧
    parallelOp (oneExchange false) (exchangeWrite false) = none :=
  ⟨rfl, rfl, rfl, rfl, rfl, rfl⟩

example : issuedPair (oneExchange true) (exchangeRead true) (exchangeWrite true) := by
  constructor
  · exact ⟨(), 0, 1, rfl, rfl, by decide⟩
  · exact ⟨rfl, rfl, rfl, rfl⟩

end TcgMemory
