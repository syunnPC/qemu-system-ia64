/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.DeferredAcquire

open Program (Command Machine Thread)

structure Held where
  op : Load
  seed : CellInput
  regs : Registers
  address : Nat
  slot : Nat

structure State (Cpu : Type) where
  memory : Machine Cpu
  held : Cpu → List Held

def start (m : Machine Cpu) : State Cpu := ⟨m, fun _ => []⟩

def State.update [DecidableEq Cpu] (state : State Cpu) (cpu : Cpu)
    (held : List Held) (memory : Machine Cpu) : State Cpu :=
  ⟨memory, fun other => if other = cpu then held else state.held other⟩

-- Queued loads are issued in instruction order. Their actual read
-- can take place later, subject to register, byte and acquire hazards.
structure Masks where
  acquire : BitVec 8
  beforeLoad : BitVec 8
  beforeStore : BitVec 8

def defaultMask (request implicitOrder : BitVec 8) : BitVec 8 :=
  implicitOrder ||| (request &&& atomicGuestDefault &&& ~~~implicitOrder)

theorem defaultsKeepReadOrder (implicitOrder : BitVec 8) :
    defaultMask atomicLoadRequest implicitOrder &&& loadLoadMask = loadLoadMask ∧
    defaultMask atomicStoreRequest implicitOrder &&& loadStoreMask = loadStoreMask := by
  unfold defaultMask atomicGuestDefault atomicLoadRequest atomicStoreRequest loadLoadMask loadStoreMask
  bv_decide

def sourceMasks : Masks :=
  ⟨acquireMask, defaultMask atomicLoadRequest 0, defaultMask atomicStoreRequest 0⟩

def canAccess (mask : Masks) (write : Bool) (earlier : List Held) : Bool :=
  let pair := if write then loadStoreMask else loadLoadMask
  let before := if write then mask.beforeStore else mask.beforeLoad
  !((!earlier.isEmpty && (before &&& pair) != 0) ||
    (earlier.any (fun h => h.op.acquire) && (mask.acquire &&& pair) != 0))

theorem defaultsBlock (explicit : BitVec 8) (write : Bool) (earlier : List Held)
    (present : earlier.isEmpty = false) :
    canAccess {sourceMasks with acquire := explicit} write earlier = false := by
  cases write <;> simp [canAccess, sourceMasks, present, defaultMask, atomicGuestDefault,
    atomicLoadRequest, atomicStoreRequest, loadLoadMask, loadStoreMask]

theorem emittedAcquireBlocks (write : Bool) (prior : Held) (earlier : List Held)
    (present : prior ∈ earlier) (acquire : prior.op.acquire = true) :
    canAccess ⟨acquireMask, 0, 0⟩ write earlier = false := by
  have active : earlier.any (fun h => h.op.acquire) = true :=
    List.any_eq_true.mpr ⟨prior, present, acquire⟩
  cases write <;> simp [canAccess, active, acquireMask, loadStoreMask, loadLoadMask]

def beforeIndependent (prior : Held) (address size : Nat) (regs : List Reg) : Bool :=
  !regs.contains prior.op.target &&
    (prior.op.update == .none || !regs.contains prior.op.base) &&
    decide (address + size ≤ prior.address ∨ prior.address + prior.op.width.bytes ≤ address)

def loadRegisters (op : Load) : List Reg :=
  match op.update with
  | .register increment => [op.base, op.target, increment]
  | _ => [op.base, op.target]

def compatible (d : State Cpu) (cpu : Cpu) (prior : List Held) (scalar : Scalar) : Bool :=
  let (base, width, registers) := match scalar with
    | .load op => (op.base, op.width, loadRegisters op)
    | .store op => (op.base, op.width, [op.base, op.source])
  let address := (d.memory.phys cpu (getGR (d.memory.threads cpu).regs base)).toNat
  prior.all (fun held => beforeIndependent held address width.bytes registers)

theorem waitsForUpdatedBase (prior : Held) (address size : Nat) (regs : List Reg)
    (update : prior.op.update ≠ .none) (dependent : regs.contains prior.op.base = true) :
    beforeIndependent prior address size regs = false := by
  have changed : (prior.op.update == .none) = false := Bool.eq_false_iff.mpr
    (fun plain => update (beq_iff_eq.mp plain))
  simp only [beforeIndependent, changed, dependent, Bool.not_true, Bool.or_self,
    Bool.and_false, Bool.false_and]

def hold [DecidableEq Cpu] (d : State Cpu) (cpu : Cpu) : Option (State Cpu) :=
  let t := d.memory.threads cpu
  if t.fault.isSome then none else
  match t.next with
  | .memory (.scalar ⟨.load op, seed⟩) :: next =>
    let base := getGR t.regs op.base
    if compatible d cpu (d.held cpu) (.load op) &&
        predicate t.regs op.qp && legalLoad op t.regs && !getNat t.regs op.base &&
        seed.preflight && seed.primitiveReturns && Program.qualified d.memory cpu base op.width
    then
      let address := (d.memory.phys cpu base).toNat
      let queued := ⟨op, seed, t.regs, address, t.issued⟩
      some (d.update cpu (queued :: d.held cpu)
        (d.memory.update cpu {t with next, issued := t.issued + 1}))
    else none
  | _ => none

def readInputs (memory : Machine Cpu) (cpu : Cpu) (held : Held) : CellInput :=
  let byte offset := Program.ordinaryByte memory.initial memory.committed
    (memory.threads cpu).pending (held.address + offset)
  {held.seed with bytes := byte}

def completeRegisters (op : Load) (current result : Registers) : Registers :=
  let target := setNaT (setGR current op.target (result.gr op.target))
    op.target (result.nat op.target)
  match op.update with
  | .none => target
  | .immediate _ | .register _ =>
    setNaT (setGR target op.base (result.gr op.base)) op.base (result.nat op.base)

theorem completePreservesOther (op : Load) (current result : Registers) (r : Reg)
    (target : r ≠ op.target) (base : r ≠ op.base) :
    (completeRegisters op current result).gr r = current.gr r ∧
    (completeRegisters op current result).nat r = current.nat r ∧
    (completeRegisters op current result).dirty r = current.dirty r := by
  cases update : op.update <;> simp [completeRegisters, update, setGR, setNaT, target, base]

def complete [DecidableEq Cpu] (mask : Masks) (d : State Cpu) (cpu : Cpu)
    (index : Nat) : Option (State Cpu) := do
  let queue := d.held cpu
  let pending ← queue[index]?
  if !canAccess mask false (queue.drop (index + 1)) then none else
  let t := d.memory.threads cpu
  let actual := tcgLoad pending.op pending.regs (readInputs d.memory cpu pending)
  if actual.exit != .success then none else
  let regs := completeRegisters pending.op t.regs actual.regs
  let changed := d.memory.update cpu {t with regs}
  let entries := actual.effect.toList.map (fun e => (cpu, MemoryRecord.scalar pending.slot e))
  let memory := {changed with trace := changed.trace ++ entries}
  some (d.update cpu (queue.eraseIdx index) memory)

def executeNow [DecidableEq Cpu] (mask : Masks) (d : State Cpu)
    (cpu : Cpu) : Option (State Cpu) :=
  let allowed := match d.held cpu, (d.memory.threads cpu).next with
    | [], _ => true
    | earlier, .memory (.scalar request) :: _ =>
      let writes := match request.op with
        | .load _ => false
        | .store _ => true
      canAccess mask writes earlier && compatible d cpu earlier request.op
    | _, _ => false
  if allowed then (Program.issue Program.tcgPolicy tcgStep tcgAtomic d.memory cpu).map
    (fun memory => {d with memory}) else none

theorem cannotCompletePastAcquire [DecidableEq Cpu] (d : State Cpu) (cpu : Cpu)
    (index : Nat) (prior : Held) (present : prior ∈ (d.held cpu).drop (index + 1))
    (acquire : prior.op.acquire = true) : complete sourceMasks d cpu index = none := by
  have blocked : canAccess sourceMasks false ((d.held cpu).drop (index + 1)) = false := by
    have active : ((d.held cpu).drop (index + 1)).any (fun h => h.op.acquire) = true :=
      List.any_eq_true.mpr ⟨prior, present, acquire⟩
    simp [canAccess, sourceMasks, acquireMask, loadLoadMask, active]
  unfold complete
  cases request : (d.held cpu)[index]? <;> simp [request, blocked]

theorem cannotExecutePastAcquire [DecidableEq Cpu] (d : State Cpu) (cpu : Cpu)
    (request : Request) (rest : List Command) (prior : Held)
    (next : (d.memory.threads cpu).next = .memory (.scalar request) :: rest)
    (present : prior ∈ d.held cpu) (acquire : prior.op.acquire = true) :
    executeNow sourceMasks d cpu = none := by
  have active : (d.held cpu).any (fun h => h.op.acquire) = true :=
    List.any_eq_true.mpr ⟨prior, present, acquire⟩
  have readBlocked : canAccess sourceMasks false (d.held cpu) = false := by
    simp [canAccess, sourceMasks, acquireMask, loadLoadMask, active]
  have writeBlocked : canAccess sourceMasks true (d.held cpu) = false := by
    simp [canAccess, sourceMasks, acquireMask, loadStoreMask, active]
  cases queue : d.held cpu with
  | nil => simp [queue] at present
  | cons first later =>
    rw [queue] at readBlocked writeBlocked
    cases access : request.op <;> simp [executeNow, next, queue, access, readBlocked, writeBlocked]

theorem readInputsUseCurrent (memory : Machine Cpu) (cpu : Cpu) (held : Held) (offset : Nat) :
    (readInputs memory cpu held).bytes offset =
      Program.ordinaryByte memory.initial memory.committed (memory.threads cpu).pending
        (held.address + offset) := rfl

theorem completionAppendsGuestRead [DecidableEq Cpu] (mask : Masks) (d after : State Cpu)
    (cpu : Cpu) (index : Nat) (held : Held) (issued : (d.held cpu)[index]? = some held)
    (completed : complete mask d cpu index = some after) :
    after.memory.trace = d.memory.trace ++
      (specLoad held.op held.regs (readInputs d.memory cpu held)).effect.toList.map
        (fun e => (cpu, MemoryRecord.scalar held.slot e)) := by
  let outcome := tcgLoad held.op held.regs (readInputs d.memory cpu held)
  simp only [complete, issued] at completed
  cases allowed : canAccess mask false ((d.held cpu).drop (index + 1))
  case false => simp [allowed] at completed
  by_cases ok : outcome.exit = .success
  · simp [allowed, outcome, ok] at completed
    subst after
    rw [← loadValueState held.op held.regs (readInputs d.memory cpu held)]
    rfl
  · simp [allowed, outcome, ok] at completed

theorem completionWritesGuestRegisters [DecidableEq Cpu] (mask : Masks) (d after : State Cpu)
    (cpu : Cpu) (index : Nat) (held : Held) (issued : (d.held cpu)[index]? = some held)
    (completed : complete mask d cpu index = some after) :
    (after.memory.threads cpu).regs =
      completeRegisters held.op (d.memory.threads cpu).regs
        (specLoad held.op held.regs (readInputs d.memory cpu held)).regs := by
  simp only [complete, issued, loadValueState] at completed
  cases allowed : canAccess mask false ((d.held cpu).drop (index + 1))
  case false => simp [allowed] at completed
  by_cases ok : (specLoad held.op held.regs (readInputs d.memory cpu held)).exit = .success
  · simp [allowed, ok] at completed
    subst after
    simp [State.update, Program.Machine.update]
  · simp [allowed, ok] at completed

inductive Action (Cpu : Type) where
  | delay (cpu : Cpu)
  | finish (cpu : Cpu) (index : Nat)
  | execute (cpu : Cpu)
  | publish (cpu : Cpu) (index : Nat)

def run [DecidableEq Cpu] (mask : Masks) : State Cpu → List (Action Cpu) → Option (State Cpu)
  | state, [] => some state
  | state, action :: rest =>
    let next := match action with
      | .delay cpu => hold state cpu
      | .finish cpu index => complete mask state cpu index
      | .execute cpu => executeNow mask state cpu
      | .publish cpu index => (Program.publish Program.tcgPolicy state.memory cpu index).map
          (fun memory => {state with memory})
    next.bind (fun state => run mask state rest)

end TcgMemory.DeferredAcquire

namespace TcgMemory.DeferredAcquire

def Contract : Prop :=
  (∀ implicitOrder, defaultMask atomicLoadRequest implicitOrder &&& loadLoadMask = loadLoadMask ∧
    defaultMask atomicStoreRequest implicitOrder &&& loadStoreMask = loadStoreMask) ∧
  (∀ (write : Bool) (prior : Held) (earlier : List Held),
    prior ∈ earlier → prior.op.acquire = true → canAccess ⟨acquireMask, 0, 0⟩ write earlier = false) ∧
  (∀ (explicit : BitVec 8) (write : Bool) (earlier : List Held),
    earlier.isEmpty = false → canAccess {sourceMasks with acquire := explicit} write earlier = false) ∧
  (∀ {Cpu : Type} [DecidableEq Cpu] (d : State Cpu) (cpu : Cpu) (index : Nat) (prior : Held),
    prior ∈ (d.held cpu).drop (index + 1) → prior.op.acquire = true →
    complete sourceMasks d cpu index = none) ∧
  (∀ {Cpu : Type} [DecidableEq Cpu] (d : State Cpu) (cpu : Cpu) (request : Request)
    (rest : List Program.Command) (prior : Held),
    (d.memory.threads cpu).next = .memory (.scalar request) :: rest → prior ∈ d.held cpu →
    prior.op.acquire = true → executeNow sourceMasks d cpu = none) ∧
  (∀ {Cpu : Type} [DecidableEq Cpu] (mask : Masks) (d after : State Cpu) (cpu : Cpu)
    (index : Nat) (held : Held),
    (d.held cpu)[index]? = some held → complete mask d cpu index = some after →
    after.memory.trace = d.memory.trace ++
      (specLoad held.op held.regs (readInputs d.memory cpu held)).effect.toList.map
        (fun e => (cpu, MemoryRecord.scalar held.slot e))) ∧
  (∀ {Cpu : Type} (memory : Program.Machine Cpu) (cpu : Cpu) (held : Held) (offset : Nat),
    (readInputs memory cpu held).bytes offset =
      Program.ordinaryByte memory.initial memory.committed (memory.threads cpu).pending
        (held.address + offset))

theorem contract : Contract :=
  ⟨defaultsKeepReadOrder, emittedAcquireBlocks, defaultsBlock, cannotCompletePastAcquire,
    cannotExecutePastAcquire, completionAppendsGuestRead, readInputsUseCurrent⟩

end TcgMemory.DeferredAcquire
