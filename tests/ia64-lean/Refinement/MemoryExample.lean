/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Refinement.MemoryExample

structure Input where
  descriptor : BitVec 32
  hostBig : Bool
  psr : BitVec 64

def architectural (input : Input) : BitVec 32 :=
  MemorySpec.dataDescriptor input.descriptor input.hostBig
    (MemorySpec.dataBigEndian input.psr)

def translated (input : Input) (tcg : Bool) : BitVec 32 :=
  let endian := MemoryImplementation.psrEndian input.psr
  if tcg then MemoryImplementation.tcgDescriptor input.descriptor input.hostBig endian
  else MemoryImplementation.runtimeDescriptor input.descriptor input.hostBig endian

theorem translated_matches (input : Input) (tcg : Bool) :
    translated input tcg = architectural input := by
  cases tcg
  · exact MemoryImplementation.runtimePsrDescriptor_matches
      input.descriptor input.hostBig input.psr
  · unfold translated architectural
    simp only [if_true]
    rw [MemoryImplementation.psrEndian_matches, MemoryImplementation.tcgDescriptor_matches]

-- This observation is the typed memory descriptor at the call boundary.
-- A whole-machine contract must use the architecturally visible effects.
abbrev Observation := List (BitVec 32)

def traceOps : TraceOps Observation where
  empty := []
  join := List.append
  emptyLeft := List.nil_append
  emptyRight := List.append_nil
  assoc := List.append_assoc

inductive Guest where
  | ready (input : Input)
  | complete (value : BitVec 32)

inductive Host where
  | queued (input : Input) (tcg : Bool)
  | complete (value : BitVec 32) (tcg : Bool)

inductive GuestStep : Guest → Observation → Guest → Prop
  | select (input : Input) :
    GuestStep (.ready input) [architectural input] (.complete (architectural input))

inductive HostStep : Host → Observation → Host → Prop
  | select (input : Input) (tcg : Bool) :
    HostStep (.queued input tcg) [translated input tcg] (.complete (translated input tcg) tcg)

inductive GuestResult : Guest → BitVec 32 → Prop
  | returned (value : BitVec 32) : GuestResult (.complete value) value

inductive HostResult : Host → BitVec 32 → Prop
  | returned (value : BitVec 32) (tcg : Bool) : HostResult (.complete value tcg) value

def guestMachine : Machine Guest Observation (BitVec 32) := ⟨GuestStep, GuestResult⟩
def hostMachine : Machine Host Observation (BitVec 32) := ⟨HostStep, HostResult⟩

inductive Related : Guest → Host → Prop
  | ready (input : Input) (tcg : Bool) : Related (.ready input) (.queued input tcg)
  | complete (value : BitVec 32) (tcg : Bool) : Related (.complete value) (.complete value tcg)

def descriptorRefinement : Simulation traceOps guestMachine hostMachine where
  related := Related
  step := by
    intro g h h' t rel hostStep
    cases hostStep with
    | select input tcg =>
      cases rel
      refine ⟨.complete (translated input tcg), ?_, .complete (translated input tcg) tcg⟩
      rw [translated_matches]
      exact Run.single (GuestStep.select input)
  outcome := by
    intro g h result rel observed
    cases observed with
    | returned _ _ =>
      cases rel
      exact .returned _
  progress := by
    intro g h rel canStep
    obtain ⟨t, next, step⟩ := canStep
    cases step with
    | select input =>
      cases rel with
      | ready _ tcg => exact ⟨[translated input tcg], _, HostStep.select input tcg⟩

def admitted : Guest → Prop
  | .ready _ => True
  | .complete _ => False

inductive Starts : Guest → Host → Prop
  | either (input : Input) (tcg : Bool) : Starts (.ready input) (.queued input tcg)

def completeDescriptorEntry : CompleteEntry descriptorRefinement admitted Starts where
  existsEntry := by
    intro g input
    cases g with
    | ready arguments => exact ⟨.queued arguments false, .either arguments false⟩
    | complete _ => exact False.elim input
  initial := by
    intro g h _ starts
    cases starts
    exact .ready _ _

theorem endianSelectorRefines :
    ImplementationRefines traceOps guestMachine hostMachine admitted Starts :=
  ⟨descriptorRefinement, ⟨completeDescriptorEntry⟩⟩

theorem finiteDescriptorResult (input : Input) (tcg : Bool) (result : BitVec 32)
    (endState : Host) (observation : Observation)
    (run : Run traceOps hostMachine (.queued input tcg) observation endState)
    (outcome : HostResult endState result) :
    ∃ g, Run traceOps guestMachine (.ready input) observation g ∧ GuestResult g result :=
  completeDescriptorEntry.observedOutcome True.intro (.either input tcg) run outcome

end IA64Refinement.MemoryExample
