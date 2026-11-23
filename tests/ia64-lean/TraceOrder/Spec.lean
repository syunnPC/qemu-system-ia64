/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std

set_option warningAsError true

namespace TraceOrder

abbrev Edge (Event : Type) := Event → Event → Prop

inductive Path {Event : Type} (edge : Edge Event) : Edge Event
  | single {a b} : edge a b → Path edge a b
  | extend {a b c} : Path edge a b → edge b c → Path edge a c

def Acyclic {Event : Type} (edge : Edge Event) : Prop :=
  ∀ event, ¬ Path edge event event

theorem Path.map {Event : Type} {before after : Edge Event}
    (includes : ∀ a b, before a b → after a b) {a b : Event} (path : Path before a b) :
    Path after a b := by
  induction path with
  | single edge => exact .single (includes _ _ edge)
  | extend _ edge ih => exact .extend ih (includes _ _ edge)

theorem acyclicOfRanks {Event : Type} (edge : Edge Event) (rank : Event → Nat)
    (order : ∀ a b, edge a b → rank a < rank b) : Acyclic edge := by
  intro event path
  have lt : ∀ {a b}, Path edge a b → rank a < rank b := by
    intro a b p
    induction p with
    | single h => exact order _ _ h
    | extend _ h ih => exact Nat.lt_trans ih (order _ _ h)
  exact Nat.lt_irrefl _ (lt path)

inductive Kind where
  | load | store | fence | rmwRead | rmwWrite | failedCompare
  deriving DecidableEq

inductive Order where
  | plain | acquire | release
  deriving DecidableEq

def Kind.read : Kind → Bool
  | .load | .rmwRead | .failedCompare => true
  | _ => false

def Kind.write : Kind → Bool
  | .store | .rmwWrite => true
  | _ => false

def Kind.ordinary : Kind → Bool
  | .load | .store => true
  | _ => false

def Kind.rmw : Kind → Bool
  | .rmwRead | .rmwWrite => true
  | _ => false

structure Access (Cpu Loc : Type) where
  cpu : Option Cpu
  location : Option Loc
  kind : Kind
  order : Order
  value : Nat

structure Execution (Event Cpu Loc : Type) where
  event : Event → Access Cpu Loc
  po : Edge Event
  dependency : Edge Event
  rf : Edge Event
  co : Edge Event
  rmw : Edge Event

variable {Event Cpu Loc : Type}

def SameLocation (x : Execution Event Cpu Loc) (a b : Event) : Prop :=
  (x.event a).location.isSome = true ∧ (x.event a).location = (x.event b).location

def ReadsFrom (x : Execution Event Cpu Loc) (read : Event) : Prop :=
  ∃ write, x.rf write read ∧ ∀ other, x.rf other read → other = write

def FromRead (x : Execution Event Cpu Loc) (read later : Event) : Prop :=
  ∃ earlier, x.rf earlier read ∧ x.co earlier later

def LocalMemoryOrder (x : Execution Event Cpu Loc) (a b : Event) : Prop :=
  x.po a b ∧ SameLocation x a b ∧
    ((x.event a).kind.write = true ∨ (x.event b).kind.write = true)

-- Each location is one aligned WB cell. The location abstraction gives
-- both identical-address conflicts and disjoint-address independence.
structure WellFormed (x : Execution Event Cpu Loc) : Prop where
  poActor : ∀ a b, x.po a b →
    (x.event a).cpu.isSome = true ∧ (x.event a).cpu = (x.event b).cpu
  poStrict : ∀ a, ¬ x.po a a
  poTransitive : ∀ a b c, x.po a b → x.po b c → x.po a c
  dependencies : ∀ a b, x.dependency a b → x.po a b
  readSources : ∀ r, (x.event r).kind.read = true →
    (x.event r).cpu.isSome = true ∧ ReadsFrom x r
  sourceValue : ∀ w r, x.rf w r → SameLocation x w r ∧
    (x.event w).kind.write = true ∧ (x.event r).kind.read = true ∧
    (x.event w).value = (x.event r).value
  coherenceLocation : ∀ a b, x.co a b →
    SameLocation x a b ∧ (x.event a).kind.write = true ∧ (x.event b).kind.write = true
  coherenceStrict : ∀ a, ¬ x.co a a
  coherenceTransitive : ∀ a b c, x.co a b → x.co b c → x.co a c
  coherenceTotal : ∀ a b, a ≠ b → SameLocation x a b →
    (x.event a).kind.write = true → (x.event b).kind.write = true →
    x.co a b ∨ x.co b a
  initialFirst : ∀ a b, a ≠ b → SameLocation x a b →
    (x.event a).cpu = none → (x.event a).kind.write = true →
    (x.event b).kind.write = true → x.co a b
  pairWellFormed : ∀ r w, x.rmw r w → SameLocation x r w ∧ x.po r w ∧
    (x.event r).kind = .rmwRead ∧ (x.event w).kind = .rmwWrite
  pairsRead : ∀ r, (x.event r).kind = .rmwRead →
    ∃ w, x.rmw r w ∧ ∀ other, x.rmw r other → other = w
  pairsWrite : ∀ w, (x.event w).kind = .rmwWrite →
    ∃ r, x.rmw r w ∧ ∀ other, x.rmw other w → other = r

-- The read's source is immediately before a successful RMW's write in
-- coherence order. A failed compare has a read event and no write event.
def AtomicPairs (x : Execution Event Cpu Loc) : Prop :=
  ∀ r w old, x.rmw r w → x.rf old r →
    x.co old w ∧ ¬ ∃ between, x.co old between ∧ x.co between w

def FenceBetween (x : Execution Event Cpu Loc) (a b : Event) : Prop :=
  ∃ f, x.po a f ∧ (x.event f).kind = .fence ∧ x.po f b

-- Intel 251429-001 §§3.3.2–3.3.4 and §4.3, at the access boundary
-- with remote reads and each successful semaphore's two events kept.
def GuestOrder (x : Execution Event Cpu Loc) (a b : Event) : Prop :=
  x.po a b ∧ (x.event a).kind ≠ .fence ∧ (x.event b).kind ≠ .fence ∧
    ((x.event a).order = .acquire ∨ (x.event b).order = .release ∨ FenceBetween x a b)

def FixedEdges (x : Execution Event Cpu Loc) (a b : Event) : Prop :=
  x.rf a b ∨ x.co a b ∨ FromRead x a b ∨ x.rmw a b ∨
    x.dependency a b ∨ LocalMemoryOrder x a b

def Observed (x : Execution Event Cpu Loc) (ordered : Edge Event) : Edge Event :=
  fun a b => FixedEdges x a b ∨ ordered a b

def OrderedWrite (access : Access Cpu Loc) : Prop :=
  access.kind.write = true ∧ (access.order = .release ∨ access.kind = .rmwWrite)

-- These separate timestamps are an observer-indexed publication witness.
-- Local visibility of a write precedes each publication. Each read is
-- remote for its cell and reads the latest publication to its own CPU.
structure Visibility (Event Cpu : Type) where
  localTime : Event → Nat
  remoteTime : Cpu → Event → Nat

structure BasicPublication (x : Execution Event Cpu Loc) (v : Visibility Event Cpu) : Prop where
  publishAfterLocal : ∀ p a, (x.event a).kind.write = true →
    v.localTime a < v.remoteTime p a
  publisherFirst : ∀ p q a, (x.event a).kind.write = true →
    (x.event a).cpu = some p → v.remoteTime p a ≤ v.remoteTime q a
  coRemote : ∀ p a b, x.co a b → v.remoteTime p a < v.remoteTime p b
  readAfterSource : ∀ p w r, x.rf w r → (x.event r).cpu = some p →
    v.remoteTime p w < v.localTime r
  readBeforeOverwrite : ∀ p r w, FromRead x r w → (x.event r).cpu = some p →
    v.localTime r < v.remoteTime p w

def AgreeOnWrites (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (published : Access Cpu Loc → Prop) : Prop :=
  ∀ p q a b, a ≠ b → published (x.event a) → published (x.event b) →
    (v.remoteTime p a < v.remoteTime p b ∨ v.remoteTime p b < v.remoteTime p a) ∧
    (v.remoteTime p a < v.remoteTime p b ↔ v.remoteTime q a < v.remoteTime q b)

def WritesRespect (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (order : Edge Event) : Prop :=
  ∀ p a b, order a b → (x.event a).kind.write = true → (x.event b).kind.write = true →
    v.remoteTime p a < v.remoteTime p b

-- Only the stated coherent WB, no-forwarding projection is compared.
-- The premises isolate TCG visibility, atomicity and retained dependence
-- edges. Their implementation correspondence remains an obligation.
structure IRExecution (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (ordered : Edge Event) : Prop where
  wellFormed : WellFormed x
  publication : BasicPublication x v
  atomic : AtomicPairs x
  commonWrites : AgreeOnWrites x v (fun a => a.kind.write = true)
  orderedWrites : WritesRespect x v ordered
  noCycles : Acyclic (Observed x ordered)

structure GuestExecution (x : Execution Event Cpu Loc) (v : Visibility Event Cpu) : Prop where
  wellFormed : WellFormed x
  publication : BasicPublication x v
  atomic : AtomicPairs x
  agreedReleases : AgreeOnWrites x v OrderedWrite
  orderedWrites : WritesRespect x v (GuestOrder x)
  noCycles : Acyclic (Observed x (GuestOrder x))

theorem refinesGuest (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (ordered : Edge Event) (lowered : ∀ a b, GuestOrder x a b → ordered a b)
    (host : IRExecution x v ordered) : GuestExecution x v := by
  refine ⟨host.wellFormed, host.publication, host.atomic, ?_, ?_, ?_⟩
  · intro p q a b distinct wa wb
    exact host.commonWrites p q a b distinct wa.1 wb.1
  · intro p a b needed wa wb
    exact host.orderedWrites p a b (lowered _ _ needed) wa wb
  · intro e cycle
    exact host.noCycles e (cycle.map (fun a b edge =>
      edge.elim Or.inl (fun need => Or.inr (lowered a b need))))

end TraceOrder
