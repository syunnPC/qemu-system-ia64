/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

inductive Scalar where
  | load (op : Load)
  | store (op : Store)

structure Request where
  op : Scalar
  cell : CellInput

abbrev Step := Registers → Request → Result

def specStep (s : Registers) (r : Request) : Result :=
  match r.op with
  | .load l => specLoad l s r.cell
  | .store st => specStore st s r.cell

def tcgStep (s : Registers) (r : Request) : Result :=
  match r.op with
  | .load l => tcgLoad l s r.cell
  | .store st => tcgStore st s r.cell

structure ThreadTrace where
  regs : Registers
  accesses : List Effect
  fault : Option Exit

def run (step : Step) (s : Registers) : List Request → ThreadTrace
  | [] => ⟨s, [], none⟩
  | request :: tail =>
    let next := step s request
    if next.exit == .success || next.exit == .nullified then
      let rest := run step next.regs tail
      {rest with accesses := next.effect.toList ++ rest.accesses}
    else
      ⟨next.regs, next.effect.toList, some next.exit⟩

theorem stepMatches (s : Registers) (request : Request) :
    tcgStep s request = specStep s request := by
  rcases request with ⟨op, cell⟩
  cases op
  · exact loadValueState _ _ _
  · exact storeValueState _ _ _

theorem threadMatches (s : Registers) (program : List Request) :
    run tcgStep s program = run specStep s program := by
  induction program generalizing s with
  | nil => rfl
  | cons request tail ih =>
    simp [run, stepMatches, ih]

abbrev Location := Word × Nat

structure InitialCell where
  address : Word
  bytes : List Byte

structure Programs (Cpu : Type) where
  regs : Cpu → Registers
  body : Cpu → List Request
  phys : Cpu → Word → Word
  initial : List InitialCell

def traces (p : Programs Cpu) (step : Step) (cpu : Cpu) : ThreadTrace :=
  run step (p.regs cpu) (p.body cpu)

inductive EventId (p : Programs Cpu) where
  | initial (index : Fin p.initial.length)
  | access (cpu : Cpu) (index : Fin (traces p tcgStep cpu).accesses.length)

def position (p : Programs Cpu) : EventId p → Option (Cpu × Nat)
  | .initial _ => none
  | .access cpu index => some (cpu, index.val)

def memoryLabel (p : Programs Cpu) : EventId p → TraceOrder.Access Cpu Location
  | .initial i =>
    let cell := p.initial[i]
    let value := unpack cell.bytes.length false (fun j => cell.bytes[j]?.getD 0)
    ⟨none, some (cell.address, cell.bytes.length), .store, .plain, value.toNat⟩
  | .access cpu i =>
    let raw := event cpu (traces p tcgStep cpu).accesses[i]
    {raw with location := raw.location.map (fun (addr, size) => (p.phys cpu addr, size))}

-- Edges choose coherence and publication independently of per-vCPU
-- execution order. Read-from must agree on canonical bytes by WellFormed.
def programOrder (p : Programs Cpu) (a b : EventId p) : Prop :=
  ∃ cpu i j, position p a = some (cpu, i) ∧ position p b = some (cpu, j) ∧ i < j

def graph (p : Programs Cpu) (rf co dependency : TraceOrder.Edge (EventId p)) :
    TraceOrder.Execution (EventId p) Cpu Location :=
  ⟨memoryLabel p, programOrder p, dependency, rf, co, fun _ _ => False⟩

def Ordinary (v : TraceOrder.Access Cpu Location) : Prop :=
  (v.kind = .load ∧ v.order ≠ .release) ∨ (v.kind = .store ∧ v.order ≠ .acquire)

theorem memoryLabelOrdinary (p : Programs Cpu) (i : EventId p) :
    Ordinary (memoryLabel p i) := by
  cases i with
  | initial index =>
    simp [Ordinary, memoryLabel]
  | access cpu index =>
    cases h : (traces p tcgStep cpu).accesses[index] with
    | read address bytes acquire =>
      cases acquire <;> simp [Ordinary, memoryLabel, h, event, readOrder]
    | write address bytes release =>
      cases release <;> simp [Ordinary, memoryLabel, h, event, writeOrder]

def pairMask (a b : TraceOrder.Kind) : BitVec 8 :=
  match a, b with
  | .load, .load => loadLoadMask
  | .load, .store => loadStoreMask
  | .store, .load => storeLoadMask
  | .store, .store => storeStoreMask
  | _, _ => 0

-- These edges need only explicit emitted acquire/release barriers.
-- Default TCG pair requests may impose additional edges.
def scalarOrdering (x : TraceOrder.Execution Event Cpu Location) (a b : Event) : Prop :=
  let pair := pairMask (x.event a).kind (x.event b).kind
  x.po a b ∧ ((x.event a).order = .acquire ∧ (acquireMask &&& pair) ≠ 0 ∨
             (x.event b).order = .release ∧ (releaseMask &&& pair) ≠ 0)

theorem scalarGuestPair (x : TraceOrder.Execution Event Cpu Location)
    (a b : Event) (oa : Ordinary (x.event a)) (ob : Ordinary (x.event b))
    (noFence : ∀ e, (x.event e).kind ≠ .fence)
    (ordered : TraceOrder.GuestOrder x a b) : scalarOrdering x a b := by
  rcases ordered with ⟨po, _, _, acq | rel | fence⟩
  · refine ⟨po, Or.inl ⟨acq, ?_⟩⟩
    rcases oa with ⟨load, _⟩ | ⟨_, impossible⟩
    · rcases ob with ⟨later, _⟩ | ⟨later, _⟩ <;>
        rw [load, later] <;> decide
    · exact False.elim (impossible acq)
  · refine ⟨po, Or.inr ⟨rel, ?_⟩⟩
    rcases ob with ⟨_, impossible⟩ | ⟨store, _⟩
    · exact False.elim (impossible rel)
    · rcases oa with ⟨earlier, _⟩ | ⟨earlier, _⟩ <;>
        rw [earlier, store] <;> decide
  · rcases fence with ⟨index, _, kind, _⟩
    exact (noFence index kind).elim

theorem Ordinary.noFence {label : TraceOrder.Access Cpu Location} (o : Ordinary label) :
    label.kind ≠ .fence := by
  rcases o with ⟨kind, _⟩ | ⟨kind, _⟩ <;> rw [kind] <;> decide

theorem scalarGuestEdges (x : TraceOrder.Execution Event Cpu Location)
    (ordinary : ∀ e, Ordinary (x.event e)) (a b : Event)
    (ordered : TraceOrder.GuestOrder x a b) : scalarOrdering x a b :=
  scalarGuestPair x a b (ordinary a) (ordinary b) (fun e => (ordinary e).noFence) ordered

def aligned (loc : Location) : Prop :=
  loc.2 ∈ [1, 2, 4, 8] ∧ loc.1.toNat % loc.2 = 0 ∧ loc.1.toNat + loc.2 ≤ 2 ^ 64

def separate (a b : Location) : Prop :=
  a = b ∨ a.1.toNat + a.2 ≤ b.1.toNat ∨ b.1.toNat + b.2 ≤ a.1.toNat

def WholeCells (x : TraceOrder.Execution Event Cpu Location) : Prop :=
  (∀ e loc, (x.event e).location = some loc → aligned loc) ∧
  (∀ e f a b, (x.event e).location = some a → (x.event f).location = some b →
    separate a b)

-- The independent program order and exact observed byte labels come
-- from the composed lifted instructions. The witness still supplies
-- coherence, visibility and qualified physical mappings.
def ComposedWb : Prop :=
  ∀ {Cpu : Type} (p : Programs Cpu) (rf co dependency : TraceOrder.Edge (EventId p))
    (visibility : TraceOrder.Visibility (EventId p) Cpu),
    let x := graph p rf co dependency
    WholeCells x →
    TraceOrder.IRExecution x visibility (scalarOrdering x) →
    (∀ cpu, traces p tcgStep cpu = traces p specStep cpu) ∧
      WholeCells x ∧ TraceOrder.GuestExecution x visibility

theorem composedWb : ComposedWb := by
  intro Cpu p rf co dependency visibility x cells accepted
  refine ⟨fun cpu => threadMatches (p.regs cpu) _, cells, ?_⟩
  exact TraceOrder.refinesGuest x visibility (scalarOrdering x)
    (scalarGuestEdges x (memoryLabelOrdinary p)) accepted

def wholeCellImage (p : Programs Cpu) : TraceOrder.Forward.Image (EventId p) Location where
  value i loc :=
    let label := memoryLabel p i
    if label.location == some loc then some label.value else none
  initial loc :=
    match p.initial.find? (fun cell => (cell.address, cell.bytes.length) == loc) with
    | some cell => (unpack cell.bytes.length false (fun i => cell.bytes[i]?.getD 0)).toNat
    | none => 0

theorem composedWbForwarding (p : Programs Cpu)
    (rf co dependency : TraceOrder.Edge (EventId p))
    (visibility : TraceOrder.Visibility (EventId p) Cpu)
    (cells : WholeCells (graph p rf co dependency))
    (accepted : TraceOrder.Forward.IRExecution (graph p rf co dependency) visibility
      (wholeCellImage p) (scalarOrdering (graph p rf co dependency))) :
    (∀ cpu, traces p tcgStep cpu = traces p specStep cpu) ∧
      WholeCells (graph p rf co dependency) ∧
      TraceOrder.Forward.GuestExecution (graph p rf co dependency) visibility (wholeCellImage p) :=
  ⟨fun cpu => threadMatches (p.regs cpu) _, cells,
    TraceOrder.Forward.refinesGuest
      (scalarGuestEdges (graph p rf co dependency) (memoryLabelOrdinary p)) accepted⟩

def AlignedForwardContract : Prop :=
  ∀ {Cpu : Type} (p : Programs Cpu) (rf co dependency : TraceOrder.Edge (EventId p))
    (visibility : TraceOrder.Visibility (EventId p) Cpu),
    let x := graph p rf co dependency
    WholeCells x →
    TraceOrder.Forward.IRExecution x visibility (wholeCellImage p) (scalarOrdering x) →
    (∀ cpu, traces p tcgStep cpu = traces p specStep cpu) ∧
      WholeCells x ∧ TraceOrder.Forward.GuestExecution x visibility (wholeCellImage p)

-- A release store at virtual 16 supplies the same physical cell that
-- the second CPU's acquire load reaches at virtual 32. Their PSR.be
-- modes differ, so their GR values differ while read-from bytes agree.
private def twoCpu : Programs Bool :=
  { regs := fun cpu =>
      ⟨fun r => if r = 4 then (if cpu then 32 else 16) else 0x1234,
        fun _ => false, fun _ => false, fun _ => false, 0⟩
    body := fun cpu =>
      if cpu then
        [⟨.load ⟨0, 5, 4, .w2, true, .none⟩, { exampleCell with guestBig := false }⟩]
      else
        [⟨.store ⟨0, 6, 4, .w2, true, none⟩, exampleCell⟩,
         ⟨.store ⟨2, 6, 4, .w2, false, none⟩, exampleCell⟩]
    phys := fun _ _ => 64
    initial := [] }

private def releaseEvent : EventId twoCpu := .access false ⟨0, by decide⟩
private def acquireEvent : EventId twoCpu := .access true ⟨0, by decide⟩

private theorem twoCpuEventCases (e : EventId twoCpu) :
    e = releaseEvent ∨ e = acquireEvent := by
  cases e with
  | initial i => exact Fin.elim0 i
  | access cpu i =>
    cases cpu
    · left
      have h : i = ⟨0, by decide⟩ := Fin.eq_of_val_eq (Nat.eq_zero_of_le_zero (Nat.le_of_lt_succ i.isLt))
      cases h
      rfl
    · right
      have h : i = ⟨0, by decide⟩ := Fin.eq_of_val_eq (Nat.eq_zero_of_le_zero (Nat.le_of_lt_succ i.isLt))
      cases h
      rfl

private def readFrom (a b : EventId twoCpu) : Prop :=
  a = releaseEvent ∧ b = acquireEvent

private def noEdge (_ _ : EventId twoCpu) : Prop := False

private def exampleGraph := graph twoCpu readFrom noEdge noEdge

private theorem onlyReleaseWrite (e : EventId twoCpu)
    (write : (exampleGraph.event e).kind.write = true) : e = releaseEvent := by
  rcases twoCpuEventCases e with release | acquire
  · exact release
  · rw [acquire] at write
    cases write

private theorem twoCpuOrderEmpty (a b : EventId twoCpu) : ¬exampleGraph.po a b := by
  rintro ⟨cpu, i, j, before, after, earlier⟩
  rcases twoCpuEventCases a with rfl | rfl <;>
    rcases twoCpuEventCases b with rfl | rfl <;>
    simp [position, releaseEvent, acquireEvent] at before after <;>
    omega

private def exampleVisibility : TraceOrder.Visibility (EventId twoCpu) Bool :=
  ⟨fun e => if (position twoCpu e).map Prod.fst == some false then 0 else 2,
    fun _ _ => 1⟩

private theorem twoCpuAccepted :
    TraceOrder.IRExecution exampleGraph exampleVisibility (scalarOrdering exampleGraph) := by
  constructor
  · constructor
    · intro a b po
      exact (twoCpuOrderEmpty a b po).elim
    · intro a
      exact twoCpuOrderEmpty a a
    · intro a b _ po _
      exact (twoCpuOrderEmpty a b po).elim
    · intro _ _ impossible
      exact impossible.elim
    · intro r read
      rcases twoCpuEventCases r with rfl | rfl
      · cases read
      · exact ⟨rfl, releaseEvent, ⟨rfl, rfl⟩, fun _ rf => rf.1⟩
    · rintro _ _ ⟨rfl, rfl⟩
      exact ⟨⟨rfl, rfl⟩, rfl, rfl, rfl⟩
    · intro _ _ impossible
      exact impossible.elim
    · intro _ impossible
      exact impossible.elim
    · intro _ _ _ impossible _
      exact impossible.elim
    · intro a b distinct _ writeA writeB
      exact (distinct ((onlyReleaseWrite a writeA).trans
        (onlyReleaseWrite b writeB).symm)).elim
    · intro a b _ _ initial _
      rcases twoCpuEventCases a with rfl | rfl <;> cases initial
    · intro _ _ impossible
      exact impossible.elim
    · intro r kind
      rcases twoCpuEventCases r with rfl | rfl <;> cases kind
    · intro w kind
      rcases twoCpuEventCases w with rfl | rfl <;> cases kind
  · constructor
    · intro _ a write
      rcases twoCpuEventCases a with rfl | rfl
      · change 0 < 1
        decide
      · cases write
    · intro _ _ _ _ _
      exact Nat.le_refl _
    · intro _ _ _ impossible
      exact impossible.elim
    · rintro _ _ _ ⟨rfl, rfl⟩ _
      change 1 < 2
      decide
    · rintro _ _ _ ⟨_, _, impossible⟩ _
      exact impossible.elim
  · intro _ _ _ impossible
    exact impossible.elim
  · intro _ _ a b distinct writeA writeB
    exact (distinct ((onlyReleaseWrite a writeA).trans
      (onlyReleaseWrite b writeB).symm)).elim
  · intro _ a b order _ _
    exact (twoCpuOrderEmpty a b order.1).elim
  · apply TraceOrder.acyclicOfRanks _ exampleVisibility.localTime
    intro a b observed
    rcases observed with (rf | co | fromRead | rmw | dependency | localOrder) | order
    · rcases rf with ⟨rfl, rfl⟩
      decide
    · exact co.elim
    · rcases fromRead with ⟨_, _, co⟩
      exact co.elim
    · exact rmw.elim
    · exact dependency.elim
    · exact (twoCpuOrderEmpty a b localOrder.1).elim
    · exact (twoCpuOrderEmpty a b order.1).elim

private theorem twoCpuCells : WholeCells exampleGraph := by
  constructor
  · intro e loc physical
    have isCell : (exampleGraph.event e).location = some (64, 2) := by
      rcases twoCpuEventCases e with rfl | rfl <;> rfl
    rw [isCell] at physical
    cases physical
    unfold aligned
    decide
  · intro a b locA locB ha hb
    have same : (exampleGraph.event a).location = (exampleGraph.event b).location := by
      rcases twoCpuEventCases a with rfl | rfl <;> rcases twoCpuEventCases b with rfl | rfl <;> rfl
    rw [ha, hb] at same
    exact Or.inl (Option.some.inj same)

example : TraceOrder.GuestExecution exampleGraph exampleVisibility ∧
    (traces twoCpu tcgStep true).regs.gr 5 = 0x3412 :=
  ⟨(composedWb twoCpu readFrom noEdge noEdge exampleVisibility twoCpuCells twoCpuAccepted).2.2, rfl⟩

end TcgMemory
