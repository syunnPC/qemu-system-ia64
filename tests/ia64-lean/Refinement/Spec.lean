/- SPDX-License-Identifier: GPL-2.0-or-later -/
set_option warningAsError true

namespace IA64Refinement

-- Trace composition comes from the observation model. A concurrent trace
-- can be an event graph with partial-order and read-from edges.
structure TraceOps (Trace : Type) where
  empty : Trace
  join : Trace → Trace → Trace
  emptyLeft : ∀ t, join empty t = t
  emptyRight : ∀ t, join t empty = t
  assoc : ∀ a b c, join (join a b) c = join a (join b c)

structure Machine (State Trace Outcome : Type) where
  step : State → Trace → State → Prop
  outcome : State → Outcome → Prop

inductive Run {State Trace Outcome : Type} (ops : TraceOps Trace)
    (machine : Machine State Trace Outcome) : State → Trace → State → Prop
  | zero (state : State) : Run ops machine state ops.empty state
  | more {start middle finish first rest}
      (step : machine.step start first middle)
      (tail : Run ops machine middle rest finish) :
      Run ops machine start (ops.join first rest) finish

theorem Run.single {State Trace Outcome : Type} {ops : TraceOps Trace}
    {m : Machine State Trace Outcome} {start finish effect}
    (step : m.step start effect finish) : Run ops m start effect finish := by
  simpa only [ops.emptyRight] using Run.more step (Run.zero (ops := ops) (machine := m) finish)

theorem Run.compose {State Trace Outcome : Type} {ops : TraceOps Trace}
    {m : Machine State Trace Outcome} {start middle finish first rest}
    (pre : Run ops m start first middle) (suffix : Run ops m middle rest finish) :
    Run ops m start (ops.join first rest) finish := by
  induction pre with
  | zero => simpa only [ops.emptyLeft] using suffix
  | more step _ ih => simpa only [ops.assoc] using Run.more step (ih suffix)

-- For each possible translated step, the guest can match its observation.
-- Ready guest states require an implementation step.
structure Simulation {Guest Host Trace Outcome : Type}
    (ops : TraceOps Trace) (guest : Machine Guest Trace Outcome)
    (host : Machine Host Trace Outcome) where
  related : Guest → Host → Prop
  step : ∀ {g h h' t}, related g h → host.step h t h' →
    ∃ g', Run ops guest g t g' ∧ related g' h'
  outcome : ∀ {g h result}, related g h → host.outcome h result → guest.outcome g result
  progress : ∀ {g h}, related g h → (∃ t g', guest.step g t g') →
    ∃ t h', host.step h t h'

theorem Simulation.finite {Guest Host Trace Outcome : Type}
    {ops : TraceOps Trace} {guest : Machine Guest Trace Outcome}
    {host : Machine Host Trace Outcome} (sim : Simulation ops guest host)
    {g h h' t} (initial : sim.related g h) (run : Run ops host h t h') :
    ∃ g', Run ops guest g t g' ∧ sim.related g' h' := by
  induction run generalizing g with
  | zero => exact ⟨g, .zero g, initial⟩
  | more step _ ih =>
    obtain ⟨middle, pre, rel⟩ := sim.step initial step
    obtain ⟨finish, post, rel⟩ := ih rel
    exact ⟨finish, pre.compose post, rel⟩

-- The admitted guest inputs and actual translated entry relation are
-- supplied by the contract. Each admitted input must have an entry, and
-- the simulation must relate every such entry.
structure CompleteEntry {Guest Host Trace Outcome : Type}
    {ops : TraceOps Trace} {guest : Machine Guest Trace Outcome}
    {host : Machine Host Trace Outcome} (sim : Simulation ops guest host)
    (admitted : Guest → Prop) (starts : Guest → Host → Prop) where
  existsEntry : ∀ g, admitted g → ∃ h, starts g h
  initial : ∀ {g h}, admitted g → starts g h → sim.related g h

def ImplementationRefines {Guest Host Trace Outcome : Type}
    (ops : TraceOps Trace) (guest : Machine Guest Trace Outcome)
    (host : Machine Host Trace Outcome)
    (admitted : Guest → Prop) (starts : Guest → Host → Prop) : Prop :=
  ∃ sim : Simulation ops guest host, Nonempty (CompleteEntry sim admitted starts)

theorem CompleteEntry.observedOutcome {Guest Host Trace Outcome : Type}
    {ops : TraceOps Trace} {guest : Machine Guest Trace Outcome}
    {host : Machine Host Trace Outcome} {sim : Simulation ops guest host}
    {admitted : Guest → Prop} {starts : Guest → Host → Prop}
    (entry : CompleteEntry sim admitted starts)
    {g h h' t result} (supported : admitted g) (actual : starts g h)
    (run : Run ops host h t h') (outcome : host.outcome h' result) :
    ∃ g', Run ops guest g t g' ∧ guest.outcome g' result := by
  obtain ⟨g', matched, agree⟩ := sim.finite (entry.initial supported actual) run
  exact ⟨g', matched, sim.outcome agree outcome⟩

end IA64Refinement
