/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.Program

structure Write (Cpu : Type) where
  cpu : Cpu
  slot : Nat
  base : Nat
  bytes : List Byte
  release : Bool

def payload (w : Write Cpu) (address : Nat) : Option Byte :=
  if w.base ≤ address then w.bytes[address - w.base]? else none

def overlap (w : Write Cpu) (base size : Nat) : Bool :=
  decide (w.base < base + size ∧ base < w.base + w.bytes.length)

def newest (address : Nat) : List (Write Cpu) → Option (Write Cpu × Byte)
  | [] => none
  | w :: newer =>
    match payload w address with
    | some byte => some (w, byte)
    | none => newest address newer

theorem newestSource (address : Nat) (writes : List (Write Cpu)) (w : Write Cpu) (byte : Byte)
    (found : newest address writes = some (w, byte)) :
    ∃ later earlier, writes = later ++ w :: earlier ∧ payload w address = some byte ∧
      ∀ other, other ∈ later → payload other address = none := by
  induction writes with
  | nil => contradiction
  | cons first rest ih =>
    cases firstByte : payload first address with
    | none =>
      obtain ⟨later, earlier, split, provides, noneLater⟩ :=
        ih (by simpa [newest, firstByte] using found)
      refine ⟨first :: later, earlier, congrArg (List.cons first) split, provides, ?_⟩
      intro other mem
      simp only [List.mem_cons] at mem
      rcases mem with rfl | member
      · exact firstByte
      · exact noneLater other member
    | some value =>
      simp [newest, firstByte] at found
      obtain ⟨rfl, rfl⟩ := found
      exact ⟨[], rest, rfl, firstByte, by simp⟩

theorem noCover (address : Nat) (writes : List (Write Cpu)) (absent : newest address writes = none) :
    ∀ writer, writer ∈ writes → payload writer address = none := by
  induction writes with
  | nil => simp
  | cons first rest ih =>
    cases got : payload first address with
    | some byte => simp [newest, got] at absent
    | none =>
      intro writer mem
      simp only [List.mem_cons] at mem
      rcases mem with rfl | mem
      · exact got
      · exact ih (by simpa [newest, got] using absent) writer mem

def publishedByte (initial : Nat → Byte) (committed : List (Write Cpu)) (address : Nat) : Byte :=
  ((newest address committed).map Prod.snd).getD (initial address)

def ordinaryByte (initial : Nat → Byte) (committed pending : List (Write Cpu)) (address : Nat) : Byte :=
  ((newest address (pending ++ committed)).map Prod.snd).getD (initial address)

theorem latestOwnElsePublished (initial : Nat → Byte) (committed pending : List (Write Cpu))
    (address : Nat) :
    ordinaryByte initial committed pending address =
      match newest address pending with
      | some (_, byte) => byte
      | none => publishedByte initial committed address := by
  induction pending with
  | nil => rfl
  | cons w tail ih =>
    cases value : payload w address
    · simpa [ordinaryByte, newest, value] using ih
    · simp [ordinaryByte, newest, value]

structure Policy where
  ordinaryBeforeStore : Bool
  releaseBeforeStore : Bool
  fenceBeforeAccess : Bool
  deriving DecidableEq

def storeMask (implicitOrder : BitVec 8) : BitVec 8 :=
  implicitOrder ||| (atomicStoreRequest &&& atomicGuestDefault &&& ~~~implicitOrder)

theorem emittedStoresOrdered (implicitOrder : BitVec 8) :
    (storeMask implicitOrder &&& storeStoreMask) = storeStoreMask := by
  unfold storeMask atomicStoreRequest atomicGuestDefault storeStoreMask
  bv_decide

def tcgPolicy : Policy :=
  ⟨(storeMask 0 &&& storeStoreMask) != 0,
    (releaseMask &&& storeStoreMask) != 0,
    (programFenceMask &&& storeLoadMask) != 0 && (programFenceMask &&& storeStoreMask) != 0⟩

def guestPolicy : Policy := ⟨false, true, true⟩

private theorem sourcePolicy : tcgPolicy = ⟨true, true, true⟩ := by decide

-- The queue is newest first. In the guest model, plain stores to
-- disjoint ranges can pass. The source model enforces emitted ST_ST.
def publishAllowed (policy : Policy) (candidate : Write Cpu) (earlier : List (Write Cpu)) : Bool :=
  !((policy.ordinaryBeforeStore || (policy.releaseBeforeStore && candidate.release)) &&
    !earlier.isEmpty) &&
    earlier.all (fun w => !overlap w candidate.base candidate.bytes.length)

theorem releaseWaits (candidate old : Write Cpu) (earlier : List (Write Cpu))
    (release : candidate.release = true) (issued : old ∈ earlier) :
    publishAllowed guestPolicy candidate earlier = false := by
  cases earlier with
  | nil => simp at issued
  | cons h t => simp [guestPolicy, publishAllowed, release]

theorem ordinaryStoreWaits (candidate old : Write Cpu) (earlier : List (Write Cpu))
    (issued : old ∈ earlier) : publishAllowed tcgPolicy candidate earlier = false := by
  cases earlier with
  | nil => simp at issued
  | cons h t => simp [sourcePolicy, publishAllowed]

private theorem admittedPublication (candidate : Write Cpu) (earlier : List (Write Cpu))
    (allowed : publishAllowed tcgPolicy candidate earlier = true) :
    publishAllowed guestPolicy candidate earlier = true := by
  cases earlier with
  | nil => simp [publishAllowed]
  | cons h t => simp [publishAllowed, sourcePolicy] at allowed

theorem overlapWaits (policy : Policy) (candidate old : Write Cpu)
    (earlier : List (Write Cpu)) (issued : old ∈ earlier)
    (intersects : overlap old candidate.base candidate.bytes.length = true) :
    publishAllowed policy candidate earlier = false := by
  cases allowed : publishAllowed policy candidate earlier
  · rfl
  · have all : earlier.all (fun w => !overlap w candidate.base candidate.bytes.length) = true :=
      (Bool.and_eq_true _ _ ▸ allowed).2
    have this := List.all_eq_true.mp all old issued
    simp [intersects] at this

inductive Command where
  | memory (request : MemoryRequest)
  | mf

structure Thread (Cpu : Type) where
  regs : Registers
  next : List Command
  issued : Nat
  pending : List (Write Cpu)
  fault : Option Exit

structure Machine (Cpu : Type) where
  phys : Cpu → Word → Word
  initial : Nat → Byte
  threads : Cpu → Thread Cpu
  committed : List (Write Cpu)
  trace : List (Cpu × MemoryRecord)

def start (phys : Cpu → Word → Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : Cpu → List Command) : Machine Cpu :=
  ⟨phys, initial, fun cpu => ⟨regs cpu, programs cpu, 0, [], none⟩, [], []⟩

def Machine.update [DecidableEq Cpu] (m : Machine Cpu) (cpu : Cpu) (thread : Thread Cpu) :
    Machine Cpu :=
  {m with threads := fun other => if other = cpu then thread else m.threads other}

def place (m : Machine Cpu) (cpu : Cpu) (slot : Nat) (address : Word) (bytes : List Byte)
    (release : Bool) : Write Cpu :=
  ⟨cpu, slot, (m.phys cpu address).toNat, bytes, release⟩

def record (m : Machine Cpu) (t : Thread Cpu) (cpu : Cpu) :
    List MemoryRecord → List (Write Cpu) × List (Write Cpu)
  | [] => (t.pending, m.committed)
  | .scalar slot (.write address bytes release) :: _ =>
    (place m cpu slot address bytes release :: t.pending, m.committed)
  | .atomicRead _ _ _ _ :: .atomicWrite slot _ _ effect :: _ =>
    (t.pending, place m cpu slot effect.address effect.after
      (effect.ordering == .release) :: m.committed)
  | _ => (t.pending, m.committed)

def qualified (m : Machine Cpu) (cpu : Cpu) (base : Word) (w : Width) : Bool :=
  let start := (m.phys cpu base).toNat
  (base.toNat % w.bytes == 0) && (start % w.bytes == 0) &&
    (List.range w.bytes).all (fun i => (m.phys cpu (base + BitVec.ofNat 64 i)).toNat == start + i)

def sample (m : Machine Cpu) (t : Thread Cpu) (cpu : Cpu) :
    MemoryRequest → Option MemoryRequest
  | .scalar r =>
    let (baseReg, width) := match r.op with
      | .load l => (l.base, l.width)
      | .store s => (s.base, s.width)
    let base := getGR t.regs baseReg
    if qualified m cpu base width then
      let bytes := fun i => ordinaryByte m.initial m.committed t.pending
        ((m.phys cpu base).toNat + i)
      some (.scalar {r with cell := {r.cell with bytes}})
    else none
  | .semaphore i cell parallel =>
    let base := getGR t.regs i.base
    -- This projection admits a parallel helper after the issuer has
    -- drained earlier stores. It reads the current common image.
    if parallel && t.pending.isEmpty && qualified m cpu base i.op.width then
      let bytes := fun k => publishedByte m.initial m.committed ((m.phys cpu base).toNat + k)
      some (.semaphore i {cell with bytes} true)
    else none

def issue [DecidableEq Cpu] (policy : Policy) (scalar : Step) (atomic : SemaphoreStep)
    (m : Machine Cpu) (cpu : Cpu) : Option (Machine Cpu) := do
  let t := m.threads cpu
  if t.fault.isSome then none else
  match t.next with
  | [] => none
  | .mf :: next =>
    if policy.fenceBeforeAccess && !t.pending.isEmpty then none
    else some (m.update cpu {t with next, issued := t.issued + 1})
  | .memory request :: next => do
    let actual ← sample m t cpu request
    let (regs, exit, effects) := memoryStep scalar atomic t.regs t.issued actual
    let fault := if exit == .success || exit == .nullified then none else some exit
    let (pending, committed) := record m t cpu effects
    let changed := m.update cpu ⟨regs, next, t.issued + 1, pending, fault⟩
    some { changed with
      committed
      trace := m.trace ++ effects.map (fun e => (cpu, e)) }

def publish [DecidableEq Cpu] (policy : Policy) (m : Machine Cpu) (cpu : Cpu)
    (index : Nat) : Option (Machine Cpu) := do
  let t := m.threads cpu
  let w ← t.pending[index]?
  if publishAllowed policy w (t.pending.drop (index + 1)) then
    let changed := m.update cpu {t with pending := t.pending.eraseIdx index}
    some {changed with committed := w :: m.committed}
  else none

inductive Schedule (Cpu : Type) where
  | issue (actor : Cpu)
  | publish (actor : Cpu) (pendingIndex : Nat)

def replay [DecidableEq Cpu] (policy : Policy) (scalar : Step) (atomic : SemaphoreStep) :
    Machine Cpu → List (Schedule Cpu) → Option (Machine Cpu)
  | m, [] => some m
  | m, event :: later =>
    let step := match event with
      | .issue cpu => issue policy scalar atomic m cpu
      | .publish cpu index => publish policy m cpu index
    step.bind (fun next => replay policy scalar atomic next later)

def sourceRun [DecidableEq Cpu] : Machine Cpu → List (Schedule Cpu) → Option (Machine Cpu) :=
  replay tcgPolicy tcgStep tcgAtomic

def guestRun [DecidableEq Cpu] : Machine Cpu → List (Schedule Cpu) → Option (Machine Cpu) :=
  replay guestPolicy specStep (fun i s m _ => atomicSpec i s m)

private theorem issueRefines [DecidableEq Cpu] (m : Machine Cpu) (cpu : Cpu) :
    issue tcgPolicy tcgStep tcgAtomic m cpu =
      issue guestPolicy specStep (fun i s m _ => atomicSpec i s m) m cpu := by
  have scalar : tcgStep = specStep := by funext s request; exact stepMatches s request
  have atomic : tcgAtomic = (fun i s m _ => atomicSpec i s m) := by
    funext i s m parallel
    exact atomicMatches i s m parallel
  have fenceOrder : tcgPolicy.fenceBeforeAccess = guestPolicy.fenceBeforeAccess := rfl
  simp only [issue, scalar, atomic, fenceOrder]

private theorem publishRefines [DecidableEq Cpu] (m next : Machine Cpu) (cpu : Cpu) (index : Nat)
    (h : publish tcgPolicy m cpu index = some next) :
    publish guestPolicy m cpu index = some next := by
  unfold publish at *
  cases pending : (m.threads cpu).pending[index]? with
  | none => simp [pending] at h
  | some candidate =>
    have eligible : publishAllowed tcgPolicy candidate ((m.threads cpu).pending.drop (index + 1)) =
        true := by
      cases permitted : publishAllowed tcgPolicy candidate ((m.threads cpu).pending.drop (index + 1))
      · simp [pending, permitted] at h
      · rfl
    simpa [pending, eligible, admittedPublication _ _ eligible] using h

-- Each issue consumes the next instruction of that vCPU. The
-- projection assumes in-order issue, common coherent publication,
-- qualified RAM and an indivisible successful atomic helper.
theorem programRefines [DecidableEq Cpu] (initial final : Machine Cpu) (schedule : List (Schedule Cpu))
    (executed : sourceRun initial schedule = some final) : guestRun initial schedule = some final := by
  induction schedule generalizing initial with
  | nil => exact executed
  | cons action rest ih =>
    cases action with
    | issue cpu =>
      change (issue tcgPolicy tcgStep tcgAtomic initial cpu).bind
        (fun next => sourceRun next rest) = some final at executed
      change (issue guestPolicy specStep (fun i s m _ => atomicSpec i s m) initial cpu).bind
        (fun next => guestRun next rest) = some final
      rw [← issueRefines initial cpu]
      cases issued : issue tcgPolicy tcgStep tcgAtomic initial cpu with
      | none => simp [issued] at executed
      | some next => exact ih next (by simpa [issued] using executed)
    | publish cpu index =>
      change (publish tcgPolicy initial cpu index).bind
        (fun next => sourceRun next rest) = some final at executed
      change (publish guestPolicy initial cpu index).bind
        (fun next => guestRun next rest) = some final
      cases writes : publish tcgPolicy initial cpu index with
      | none => simp [writes] at executed
      | some next =>
        have nextRun := ih next (by simpa [writes] using executed)
        simpa [publishRefines initial next cpu index writes] using nextRun

theorem fenceDrains [DecidableEq Cpu] (m : Machine Cpu) (cpu : Cpu) (next : List Command)
    (fence : (m.threads cpu).next = .mf :: next)
    (pending : (m.threads cpu).pending.isEmpty = false) :
    issue tcgPolicy tcgStep tcgAtomic m cpu = none := by
  simp [issue, fence, pending, sourcePolicy]

theorem atomicSamplesPublished (m : Machine Cpu) (t : Thread Cpu) (cpu : Cpu)
    (i : AtomicInstruction) (input actual : CellInput) (parallel : Bool)
    (accepted : sample m t cpu (.semaphore i input parallel) = some (.semaphore i actual true)) :
    ∀ offset, actual.bytes offset =
      publishedByte m.initial m.committed ((m.phys cpu (getGR t.regs i.base)).toNat + offset) := by
  simp only [sample] at accepted
  split at accepted
  · cases accepted
    intro offset
    rfl
  · contradiction

theorem ordinarySamplesOwn (m : Machine Cpu) (cpu : Cpu)
    (i : Load) (input actual : CellInput)
    (accepted : sample m (m.threads cpu) cpu (.scalar ⟨.load i, input⟩) =
      some (.scalar ⟨.load i, actual⟩)) :
    ∀ offset,
      let physical := (m.phys cpu (getGR (m.threads cpu).regs i.base)).toNat + offset
      actual.bytes offset =
        match newest physical (m.threads cpu).pending with
        | some (_, byte) => byte
        | none => publishedByte m.initial m.committed physical := by
  simp only [sample] at accepted
  split at accepted
  · cases accepted
    intro offset
    apply latestOwnElsePublished
  · contradiction

theorem publishOnlyWhenAllowed [DecidableEq Cpu] (m next : Machine Cpu) (cpu : Cpu)
    (index : Nat) (w : Write Cpu) (atIndex : (m.threads cpu).pending[index]? = some w)
    (step : publish tcgPolicy m cpu index = some next) :
    publishAllowed tcgPolicy w ((m.threads cpu).pending.drop (index + 1)) = true ∧
      next.committed = w :: m.committed := by
  simp only [publish, atIndex] at step
  cases allowed : publishAllowed tcgPolicy w ((m.threads cpu).pending.drop (index + 1))
  case false => simp [allowed] at step
  case true =>
    simp [allowed] at step
    subst next
    exact ⟨rfl, rfl⟩

end TcgMemory.Program

namespace TcgMemory.Program

def Contract : Prop :=
  (∀ {Cpu : Type} [DecidableEq Cpu] (m final : Machine Cpu) (steps : List (Schedule Cpu)),
    sourceRun m steps = some final → guestRun m steps = some final) ∧
  (∀ implicitOrder, (storeMask implicitOrder &&& storeStoreMask) = storeStoreMask) ∧
  (∀ {Cpu : Type} (address : Nat) (writes : List (Write Cpu)) (w : Write Cpu) (byte : Byte),
    newest address writes = some (w, byte) →
    ∃ later earlier, writes = later ++ w :: earlier ∧ payload w address = some byte ∧
      ∀ other, other ∈ later → payload other address = none) ∧
  (∀ {Cpu : Type} (m : Machine Cpu) (cpu : Cpu) (i : Load) (input actual : CellInput),
    sample m (m.threads cpu) cpu (.scalar ⟨.load i, input⟩) = some (.scalar ⟨.load i, actual⟩) →
    ∀ offset,
      let address := (m.phys cpu (getGR (m.threads cpu).regs i.base)).toNat + offset
      actual.bytes offset = match newest address (m.threads cpu).pending with
        | some (_, byte) => byte
        | none => publishedByte m.initial m.committed address) ∧
  (∀ {Cpu : Type} (m : Machine Cpu) (t : Thread Cpu) (cpu : Cpu) (i : AtomicInstruction)
    (input actual : CellInput) (parallel : Bool),
    sample m t cpu (.semaphore i input parallel) = some (.semaphore i actual true) →
    ∀ offset, actual.bytes offset =
      publishedByte m.initial m.committed ((m.phys cpu (getGR t.regs i.base)).toNat + offset)) ∧
  (∀ {Cpu : Type} (candidate old : Write Cpu) (earlier : List (Write Cpu)),
    old ∈ earlier → publishAllowed tcgPolicy candidate earlier = false) ∧
  (∀ {Cpu : Type} (candidate old : Write Cpu) (earlier : List (Write Cpu)),
    candidate.release = true → old ∈ earlier → publishAllowed guestPolicy candidate earlier = false) ∧
  (∀ {Cpu : Type} (policy : Policy) (candidate old : Write Cpu) (earlier : List (Write Cpu)),
    old ∈ earlier → overlap old candidate.base candidate.bytes.length = true →
    publishAllowed policy candidate earlier = false) ∧
  (∀ {Cpu : Type} [DecidableEq Cpu] (m next : Machine Cpu) (cpu : Cpu)
    (index : Nat) (w : Write Cpu),
    (m.threads cpu).pending[index]? = some w → publish tcgPolicy m cpu index = some next →
    publishAllowed tcgPolicy w ((m.threads cpu).pending.drop (index + 1)) = true ∧
      next.committed = w :: m.committed) ∧
  (∀ {Cpu : Type} [DecidableEq Cpu] (m : Machine Cpu) (cpu : Cpu) (next : List Command),
    (m.threads cpu).next = .mf :: next → (m.threads cpu).pending.isEmpty = false →
    issue tcgPolicy tcgStep tcgAtomic m cpu = none)

theorem contract : Contract :=
  ⟨programRefines, emittedStoresOrdered, newestSource, ordinarySamplesOwn, atomicSamplesPublished,
    ordinaryStoreWaits, releaseWaits, overlapWaits, publishOnlyWhenAllowed, fenceDrains⟩

end TcgMemory.Program
