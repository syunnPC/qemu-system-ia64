/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.ByteAtomic

open LinearAtomic (CellStep readAt writeAt stateBefore stateBeforeIsLatest payload
  replayReadOn replayReadBytes admissibleStep hostEndianAt)

-- An aligned scalar of size 1, 2, 4 or 8 has every byte in its
-- naturally aligned 8-byte physical block. Different blocks have
-- independent histories.
theorem alignedByteBlock (cell : Location) (valid : aligned cell)
    (offset : Nat) (inRange : offset < cell.2) :
    (cell.1.toNat + offset) / 8 = cell.1.toNat / 8 := by
  rcases valid with ⟨size, naturally, _⟩
  simp only [List.mem_cons, List.not_mem_nil, or_false] at size
  rcases size with one | two | four | eight
  · simp [one] at inRange
    omega
  · rw [two] at naturally inRange
    omega
  · rw [four] at naturally inRange
    omega
  · rw [eight] at naturally inRange
    omega

abbrev BlockSchedule (p : AtomicPrograms Cpu) :=
  Nat → List (CellStep (AtomicEventId p))

def occursAt (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (e : AtomicEventId p) (whereAt : Nat × Nat) : Prop :=
  readAt (s whereAt.1) whereAt.2 = some e ∨ writeAt (s whereAt.1) whereAt.2 = some e

def payloadByte (p : AtomicPrograms Cpu) (e : AtomicEventId p) (physical : Nat) : Option Byte := do
  let (address, size) ← (atomicEventLabel p e).location
  if address.toNat ≤ physical ∧ physical < address.toNat + size then
    (payload p e)[physical - address.toNat]?
  else none

def writeAtByte (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (block physical position : Nat) : Option (AtomicEventId p) := do
  let writer ← writeAt (s block) position
  let _ ← payloadByte p writer physical
  some writer

def previousByte (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (block position physical : Nat) : Option Byte := do
  let (_, writer) ← stateBefore (writeAtByte p s block physical) position
  payloadByte p writer physical

def snapshot (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (block position : Nat) (location : Location) : Option (List Byte) :=
  (List.range location.2).mapM (fun offset =>
    previousByte p s block position (location.1.toNat + offset))

def rawHost (bytes : List Byte) (big : Bool) : Word :=
  unpack bytes.length big (fun offset => bytes[offset]?.getD 0)

def checkedAttempts (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (block position : Nat) (location : Location) (r w : AtomicEventId p) : Prop :=
  match atomicEventRecord p r with
  | some (.atomicRead _ (.fetchAdd width increment _) .reverseHelper _) =>
    ∃ (big : Bool) (initial : Nat) (failed : List Nat) (oldRaw : Word)
      (observations : List Word),
      hostEndianAt p r = some big ∧
      ((initial :: failed) ++ [position]).Pairwise (· ≤ ·) ∧
      (snapshot p s block initial location).map (fun bytes => rawHost bytes big) = some oldRaw ∧
      (failed ++ [position]).mapM (fun k =>
        (snapshot p s block k location).map (fun bytes => rawHost bytes big)) = some observations ∧
      (∀ n, n < observations.length →
        reverseFetchAddAttempts width increment oldRaw (observations.take n) = none) ∧
      reverseFetchAddAttempts width increment oldRaw observations =
        some (rawHost (payload p r) big, rawHost (payload p w) big)
  | _ => True

-- A block step is a whole write, load, or indivisible read/update.
-- For overlapping widths, this scope assumes the TCG operation has
-- one indivisible byte-range transition at this common position.
-- Parallel helper primitives supply their C11 contract. A serial
-- expansion is only admitted for a single-vCPU WB execution.
structure Admitted (p : AtomicPrograms Cpu) (s : BlockSchedule p) : Prop where
  steps : ∀ (block position : Nat) (op : CellStep (AtomicEventId p)),
    (s block)[position]? = some op →
    ∃ cell, aligned cell ∧ cell.1.toNat / 8 = block ∧ admissibleStep p cell op
  eventPositions : ∀ e, ∃ whereAt, occursAt p s e whereAt ∧
    ∀ other, occursAt p s e other → other = whereAt
  initialBefore : ∀ i cpu access block earlier later,
    writeAt (s block) earlier = some (.initial i) →
    occursAt p s (.access cpu access) (block, later) → earlier < later
  initialAgreement : ∀ i j physical a b,
    payloadByte p (.initial i) physical = some a →
    payloadByte p (.initial j) physical = some b → a = b
  readSnapshots : ∀ block position e, readAt (s block) position = some e →
    ∃ cell bytes, (atomicEventLabel p e).location = some cell ∧
      snapshot p s block position cell = some bytes ∧ replayReadOn p e bytes
  reverseAttempts : ∀ (block position : Nat) r w,
    (s block)[position]? = some (.atomic r w) →
    ∃ cell, (atomicEventLabel p r).location = some cell ∧
      checkedAttempts p s block position cell r w

theorem mapMValue (g : A → Option B) (inputs : List A) (outputs : List B)
    (ready : inputs.mapM g = some outputs) (index : Nat) (bound : index < inputs.length) :
    ∃ result, g inputs[index] = some result ∧ outputs[index]? = some result := by
  induction inputs generalizing index outputs with
  | nil => simp at bound
  | cons i rest ih =>
    cases hi : g i with
    | none => simp [List.mapM_cons, hi] at ready
    | some first =>
      cases hr : rest.mapM g with
      | none => simp [List.mapM_cons, hi, hr] at ready
      | some other =>
        have eq : first :: other = outputs := by simpa [List.mapM_cons, hi, hr] using ready
        subst outputs
        cases index with
        | zero => exact ⟨first, hi, rfl⟩
        | succ n =>
          obtain ⟨value, input, output⟩ := ih other hr n (by simpa using bound)
          exact ⟨value, input, output⟩

theorem labelByteCount (p : AtomicPrograms Cpu) (e : AtomicEventId p) (cell : Location)
    (found : (atomicEventLabel p e).location = some cell) : (payload p e).length = cell.2 := by
  cases e with
  | initial i => cases Option.some.inj found; rfl
  | access cpu i =>
    cases record : (atomicTrace p cpu).accesses[i] with
    | scalar _ effect =>
      cases effect <;> simp [atomicEventLabel, record, event] at found <;> cases found <;>
        simp [payload, record]
    | atomicRead _ _ _ _ =>
      simp [atomicEventLabel, record, atomicLabels, event] at found
      cases found
      simp [payload, record]
    | atomicWrite _ _ _ _ =>
      simp [atomicEventLabel, record, atomicLabels, event] at found
      cases found
      simp [payload, record]

theorem writeCoversByte (p : AtomicPrograms Cpu) (s : BlockSchedule p) (writer : AtomicEventId p)
    (block position offset : Nat) (cell : Location)
    (written : writeAt (s block) position = some writer)
    (location : (atomicEventLabel p writer).location = some cell)
    (bound : offset < cell.2) :
    writeAtByte p s block (cell.1.toNat + offset) position = some writer := by
  have payloadBound : offset < (payload p writer).length := by
    simpa [labelByteCount p writer cell location] using bound
  have index : cell.1.toNat + offset - cell.1.toNat = offset := by omega
  simp [writeAtByte, written, payloadByte, location, bound, index,
    List.getElem?_eq_getElem payloadBound]

theorem atomicBytes (p : AtomicPrograms Cpu) (s : BlockSchedule p)
    (accepted : Admitted p s) (block position : Nat) (r w : AtomicEventId p)
    (placed : (s block)[position]? = some (.atomic r w)) :
    ∃ cell, aligned cell ∧ cell.1.toNat / 8 = block ∧
      ∀ offset, offset < cell.2 →
        ∃ previous writer value,
          let physical := cell.1.toNat + offset
          physical / 8 = block ∧
          (payload p r)[offset]? = some value ∧ payloadByte p writer physical = some value ∧
          previous < position ∧ writeAtByte p s block physical previous = some writer ∧
          (∀ later contender, later < position →
            writeAtByte p s block physical later = some contender → later ≤ previous) ∧
          stateBefore (writeAtByte p s block physical) (position + 1) = some (position, w) := by
  obtain ⟨cell, valid, inBlock, _, _, _, readLoc, writeLoc, _⟩ :=
    accepted.steps block position (.atomic r w) placed
  have read : readAt (s block) position = some r := by
    simp [readAt, placed, CellStep.read]
  have written : writeAt (s block) position = some w := by
    simp [writeAt, placed, CellStep.write]
  obtain ⟨foundCell, bytes, found, assembled, verified⟩ :=
    accepted.readSnapshots block position r read
  have e : cell = foundCell := Option.some.inj (readLoc.symm.trans found)
  subst foundCell
  refine ⟨cell, valid, inBlock, ?_⟩
  intro offset bound
  obtain ⟨value, earlier, byte⟩ := mapMValue _ (List.range cell.2) bytes assembled offset
    (by simpa using bound)
  simp only [List.getElem_range] at earlier
  rw [← replayReadBytes p r bytes verified] at byte
  cases old : stateBefore (writeAtByte p s block (cell.1.toNat + offset)) position with
  | none => simp [previousByte, old] at earlier
  | some prev =>
    rcases prev with ⟨previous, writer⟩
    have stored : payloadByte p writer (cell.1.toNat + offset) = some value := by
      simpa [previousByte, old] using earlier
    obtain ⟨before, write, latest⟩ := stateBeforeIsLatest _ _ _ _ old
    refine ⟨previous, writer, value, (alignedByteBlock cell valid offset bound).trans inBlock,
      byte, stored, before, write, latest, ?_⟩
    simp [stateBefore, writeCoversByte p s w block position offset cell written writeLoc bound]

end TcgMemory.ByteAtomic

namespace TcgMemory

theorem alignedAtomicContract : AtomicCoreContract ∧
    (LinearAtomic.AtomicLinearityContract ∧
      ∀ {Cpu : Type} (p : AtomicPrograms Cpu) (s : ByteAtomic.BlockSchedule p),
        ByteAtomic.Admitted p s → ∀ (block position : Nat) (r w : AtomicEventId p),
        (s block)[position]? = some (.atomic r w) →
        ∃ cell, aligned cell ∧ cell.1.toNat / 8 = block ∧
          ∀ offset, offset < cell.2 →
            ∃ previous writer value,
              let physical := cell.1.toNat + offset
              physical / 8 = block ∧
              (LinearAtomic.payload p r)[offset]? = some value ∧
                ByteAtomic.payloadByte p writer physical = some value ∧ previous < position ∧
                ByteAtomic.writeAtByte p s block physical previous = some writer ∧
                (∀ later contender, later < position →
                  ByteAtomic.writeAtByte p s block physical later = some contender →
                  later ≤ previous) ∧
                LinearAtomic.stateBefore (ByteAtomic.writeAtByte p s block physical)
                  (position + 1) = some (position, w)) :=
  ⟨atomicCoreContract, LinearAtomic.linearAtomicContract, ByteAtomic.atomicBytes⟩

end TcgMemory
