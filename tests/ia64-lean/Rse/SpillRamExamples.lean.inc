/- SPDX-License-Identifier: GPL-2.0-or-later -/

private def crossingInput : SpillRamInput :=
  { bol := 1
    physical := fun p => if p = 94 then (0xdead, true) else (0xbeef, false)
    hasClean := true
    rsc := 0x10
    storeAllowed := fun addr => addr != 512
    interruptAfter := fun _ => false }

private def crossingStart : SpillRamState :=
  { counters := ⟨496, 3, 1, 0, 0, 0⟩
    latch := ⟨1, 1⟩
    writeback := some (504, ⟨2, 3⟩)
    ram := fun _ => 0x0400000000000080 }

example : spillRamStart crossingStart crossingInput 2 := by unfold spillRamStart; decide

example :
    let result := spillRamTrace crossingInput crossingStart 2 3
    result.writes = [⟨496, 0xdead, true⟩, ⟨504, 0x4000000000000007, true⟩] ∧
    result.writes.map SpillWrite.bytes = [[0, 0, 0, 0, 0, 0, 0xde, 0xad],
      [0x40, 0, 0, 0, 0, 0, 0, 7]] ∧
    result.stop = .fault 512 ∧ result.state.counters.bspstore = 512 ∧
    result.state.ram 496 = 0xadde000000000000 ∧ result.state.ram 504 = 0x0700000000000040 ∧
    result.state.ram 512 = crossingStart.ram 512 := by decide

def liveSpillFrame : FrameState :=
  { cfm := ⟨0, 0, 0, 0, 0, 0⟩
    psr := 0
    ifs := 0x8000000000003060
    pfs := 0
    bsp := 0x100500
    bspstore := 0x1001f0
    rnat := 0
    bol := 0
    invalid := 0
    clean := 0
    dirty := 96
    cleanNat := 0
    dirtyNat := 2 }

def liveSpillStart : SpillRamState :=
  { counters := ⟨0x1001f0, 96, 2, 0, 0, 0⟩
    latch := ⟨0, 0⟩
    writeback := none
    ram := fun addr => if addr = 0x1001f8 then 4 else 0 }

def liveSpillInput : SpillRamInput :=
  { bol := 0
    physical := fun p => (if p = 0 then 0x1111 else if p = 1 then 0x2222 else 0, false)
    hasClean := true
    rsc := 0
    storeAllowed := fun _ => true
    interruptAfter := fun _ => false }

example : liveSpillFrame.complete := by unfold FrameState.complete Frame.valid; decide

example : spillRamStart liveSpillStart liveSpillInput (mandatorySpillNeed liveSpillFrame 2) := by
  unfold spillRamStart
  decide

def liveSpillHistory : SpillRamHistory :=
  { boundCollection := none
    fillCollection := none
    shadowed := fun _ => false
    writableRam := fun _ => true
    physicalAddr := id
    readImage := liveSpillStart.ram
    storeImage := liveSpillStart.ram
    deniedBytes := fun _ => 0 }

example : liveSpillHistory.valid liveSpillStart liveSpillInput 3 := by
  refine ⟨fun _ => ⟨rfl, rfl⟩, ?_, ?_⟩
  · intro offset _
    exact ⟨fun _ => rfl, fun _ => rfl, rfl, rfl, fun _ => ⟨Option.noConfusion, rfl⟩⟩
  · intro left right hl hr ne
    have small n (hn : n < 3) : n = 0 ∨ n = 1 ∨ n = 2 := by omega
    rcases small left hl with l | l | l <;>
      rcases small right hr with r | r | r <;> subst left <;> subst right
    all_goals first | exact (ne rfl).elim | decide

example : liveSpillStart.describesFrame liveSpillFrame liveSpillInput := by
  unfold SpillRamState.describesFrame
  decide
