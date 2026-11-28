/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.CompareWide

-- The caller supplies the order of observations within this physical
-- block. No rank compares ordinary observations in different blocks.
structure BlockInput (p : AtomicPrograms Cpu) where
  cpu : Cpu
  timeline : ByteAtomic.BlockSchedule p
  loadAt : Nat
  casAt : List Nat

def blockSnapshot (p : AtomicPrograms Cpu) (h : BlockInput p) (base : Word)
    (position : Nat) : Option (List Byte) :=
  ByteAtomic.snapshot p h.timeline (base.toNat / 16) position (base, 16)

def decodeSnapshot (big : Bool) (bytes : List Byte) : Pair :=
  operand (fun offset => bytes[offset]?.getD 0) big

def blockBase (p : AtomicPrograms Cpu) (h : BlockInput p) (i : Instruction)
    (s : Registers) : Word := p.phys h.cpu (sourceBlock (getGR s i.base))

-- Virtual translation maps the aligned operand to one contiguous
-- physical block. The given prefix contains its preceding accesses.
-- For a serial path there must be no other observer of its two stores.
structure History (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s : Registers) (m : Inputs) : Prop where
  aligned : (blockBase p h i s).toNat % 16 = 0
  contiguous : ∀ offset, offset < 16 →
    (p.phys h.cpu (sourceBlock (getGR s i.base) + BitVec.ofNat 64 offset)).toNat =
      (blockBase p h i s).toNat + offset
  ordered : (h.loadAt :: h.casAt).Pairwise (· ≤ ·)
  exclusiveSerial : m.parallel = false → ∀ other : Cpu, other = h.cpu
  positions : ∀ e, ∃ whereAt, ByteAtomic.occursAt p h.timeline e whereAt ∧
    ∀ other, ByteAtomic.occursAt p h.timeline e other → other = whereAt
  steps : ∀ (block position : Nat) (op : LinearAtomic.CellStep (AtomicEventId p)),
    (h.timeline block)[position]? = some op →
    ∃ cell, TcgMemory.aligned cell ∧ cell.1.toNat / 16 = block ∧
      LinearAtomic.admissibleStep p cell op

-- Positions control which bytes can be passed back by the native CAS.
-- Absolute offsets within the containing physical block are preserved.
def decodeHistory (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s : Registers) (m : Inputs) : Option Inputs := do
  let base := blockBase p h i s
  let old ← blockSnapshot p h base h.loadAt
  let observations ← h.casAt.mapM (fun pos =>
    (blockSnapshot p h base pos).map (decodeSnapshot m.cell.guestBig))
  some { m with
    cell := {m.cell with bytes := fun offset => old[offset]?.getD 0}
    observations := observations }

def replay (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s : Registers) (m : Inputs) : Option Result :=
  (decodeHistory p h i s m).map (attempt i s)

private theorem imageLength (f : A → Option B) (inputs : List A) (results : List B)
    (all : inputs.mapM f = some results) : results.length = inputs.length := by
  induction inputs generalizing results with
  | nil =>
    have eq : [] = results := Option.some.inj all
    simp [← eq]
  | cons x rest ih =>
    cases hd : f x with
    | none => simp [List.mapM_cons, hd] at all
    | some y =>
      cases tl : rest.mapM f with
      | none => simp [List.mapM_cons, hd, tl] at all
      | some ys =>
        have eq : y :: ys = results := by simpa [List.mapM_cons, hd, tl] using all
        simp [← eq, ih ys tl]

theorem observationsMatch (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s : Registers) (m loaded : Inputs)
    (decoded : decodeHistory p h i s m = some loaded) :
    loaded.parallel = m.parallel ∧ loaded.observations.length = h.casAt.length ∧
    ∃ bytes, blockSnapshot p h (blockBase p h i s) h.loadAt = some bytes ∧
      firstPair loaded = decodeSnapshot m.cell.guestBig bytes ∧
      ∀ n (inside : n < h.casAt.length), ∃ seen,
        blockSnapshot p h (blockBase p h i s) h.casAt[n] = some seen ∧
          loaded.observations[n]? = some (decodeSnapshot m.cell.guestBig seen) := by
  unfold decodeHistory at decoded
  let image := blockSnapshot p h (blockBase p h i s)
  let element := fun pos => (image pos).map (decodeSnapshot m.cell.guestBig)
  change ((image h.loadAt).bind fun old => (h.casAt.mapM element).bind fun seen =>
    some { m with
      cell := {m.cell with bytes := fun offset => old[offset]?.getD 0}
      observations := seen }) = some loaded at decoded
  cases initial : image h.loadAt with
  | none => simp [initial] at decoded
  | some bytes =>
    cases attempts : h.casAt.mapM element with
    | none => simp [initial, attempts] at decoded
    | some observations =>
      rw [initial, attempts] at decoded
      have eq := Option.some.inj decoded
      subst loaded
      refine ⟨rfl, imageLength _ _ _ attempts, bytes, initial, rfl, ?_⟩
      intro n inside
      obtain ⟨result, step, stored⟩ := ByteAtomic.mapMValue element h.casAt observations attempts n inside
      cases available : image h.casAt[n] with
      | none => simp [element, available] at step
      | some seen =>
        have expected : decodeSnapshot m.cell.guestBig seen = result := by
          simpa [element, available] using step
        exact ⟨seen, available, expected.symm ▸ stored⟩

theorem replayCommitsAt (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s after : Registers) (m : Inputs) (e : Effect)
    (valid : History p h i s m) (parallel : m.parallel = true)
    (completed : replay p h i s m = some (.retired after e)) (hit : e.store.isSome = true) :
    ∃ (loaded : Inputs) (index : Nat) (inside : index < h.casAt.length) (oldBytes : List Byte),
      decodeHistory p h i s m = some loaded ∧ h.loadAt ≤ h.casAt[index] ∧
        blockSnapshot p h (blockBase p h i s) h.casAt[index] = some oldBytes ∧
        firstCAS i s loaded index (decodeSnapshot m.cell.guestBig oldBytes) ∧
        architectureAt i s loaded (decodeSnapshot m.cell.guestBig oldBytes) = .retired after e := by
  unfold replay at completed
  cases decoded : decodeHistory p h i s m with
  | none => simp [decoded] at completed
  | some loaded =>
    have done : attempt i s loaded = .retired after e := by simpa [decoded] using completed
    obtain ⟨sameMode, count, _, _, _, source⟩ := observationsMatch p h i s m loaded decoded
    obtain ⟨snapshot, _, matchSpec, committed⟩ := retiresAtSnapshot i s after loaded e done
    obtain ⟨index, first⟩ := committed (sameMode.trans parallel) hit
    have atIndex : loaded.observations[index]? = some snapshot := first.1
    have inside : index < h.casAt.length := count ▸ (List.getElem?_eq_some_iff.mp atIndex).1
    obtain ⟨oldBytes, read, same⟩ := source index inside
    have eq := Option.some.inj (atIndex.symm.trans same)
    subst snapshot
    exact ⟨loaded, index, inside, oldBytes, rfl,
      List.rel_of_pairwise_cons valid.ordered (List.getElem_mem _), read, first, matchSpec⟩

theorem readSource (p : AtomicPrograms Cpu) (h : BlockInput p) (base : Word)
    (position : Nat) (bytes : List Byte)
    (valueAt : blockSnapshot p h base position = some bytes)
    (offset : Nat) (inside : offset < 16) :
    ∃ previous writer value,
      let address := base.toNat + offset
      bytes[offset]? = some value ∧ ByteAtomic.payloadByte p writer address = some value ∧
        previous < position ∧
        ByteAtomic.writeAtByte p h.timeline (base.toNat / 16) address previous = some writer ∧
        (∀ other contender, other < position →
          ByteAtomic.writeAtByte p h.timeline (base.toNat / 16) address other = some contender →
          other ≤ previous) := by
  let previousAt := fun i =>
    ByteAtomic.previousByte p h.timeline (base.toNat / 16) position (base.toNat + i)
  obtain ⟨value, previous, byte⟩ :=
    ByteAtomic.mapMValue previousAt (List.range 16) bytes valueAt offset (by simpa using inside)
  simp only [List.getElem_range] at previous
  let wrote := ByteAtomic.writeAtByte p h.timeline (base.toNat / 16) (base.toNat + offset)
  change (LinearAtomic.stateBefore wrote position).bind
    (fun (_, writer) => ByteAtomic.payloadByte p writer (base.toNat + offset)) = some value
    at previous
  cases found : LinearAtomic.stateBefore wrote position with
  | none => simp [found] at previous
  | some info =>
    obtain ⟨earlier, writer⟩ := info
    obtain ⟨before, write, latest⟩ :=
      LinearAtomic.stateBeforeIsLatest wrote position earlier writer found
    exact ⟨earlier, writer, value, byte, by simpa [found] using previous, before, write, latest⟩

-- A parallel success is one indivisible replacement. Serial has the
-- same final image under exclusive execution of its two stores.
def memoryAfter (base : Nat) (store : Option (List Byte)) (before : Nat → Option Byte)
    (address : Nat) : Option Byte :=
  match store with
  | none => before address
  | some bytes =>
    if base ≤ address ∧ address < base + 16 then bytes[address - base]? else before address

theorem completeStore (i : Instruction) (s after : Registers) (m : Inputs) (e : Effect)
    (done : attempt i s m = .retired after e) {bytes : List Byte}
    (stores : e.store = some bytes) :
    bytes = pack 8 m.cell.guestBig (getGR s i.source) ++ pack 8 m.cell.guestBig m.csd ∧
      bytes.length = 16 ∧
      (∀ base offset before, offset < 16 →
        memoryAfter base e.store before (base + offset) = bytes[offset]?) := by
  obtain ⟨_, _, specified, _⟩ := retiresAtSnapshot i s after m e done
  have stores' := congrArg (fun
    | Result.retired _ e => e.store
    | _ => none) specified
  change _ = e.store at stores'
  rw [stores] at stores'
  have whole : bytes = pack 8 m.cell.guestBig (getGR s i.source) ++ pack 8 m.cell.guestBig m.csd := by
    unfold architectureAt at stores'
    dsimp at stores'
    split at stores'
    · exact (Option.some.inj stores').symm
    · cases stores'
  have length : bytes.length = 16 := by
    rw [whole]
    simp [pack]
  refine ⟨whole, length, ?_⟩
  intro base offset before inside
  have covers : base ≤ base + offset ∧ base + offset < base + 16 := by omega
  have index : base + offset - base = offset := by omega
  simp [memoryAfter, stores, covers, index]

theorem memoryOutside (base address : Nat) (stores : Option (List Byte))
    (before : Nat → Option Byte) (outside : address < base ∨ base + 16 ≤ address) :
    memoryAfter base stores before address = before address := by
  cases stores
  · rfl
  · have excludes : ¬ (base ≤ address ∧ address < base + 16) := by omega
    simp [memoryAfter, excludes]

end TcgMemory.CompareWide

namespace TcgMemory.CompareWide

open LinearAtomic (CellStep readAt stateBefore writeAt)

private def overlapRegisters (cpu : Bool) : Registers :=
  {exampleRegs with gr := fun r => if r = 4 then (if cpu then 0x1010 else 24)
    else if r = 6 then 0xaabbccdd else exampleRegs.gr r}

private def overlapProgram : AtomicPrograms Bool :=
  ⟨overlapRegisters, fun cpu =>
    if cpu then [.scalar ⟨.store ⟨0, 6, 4, .w4, false, none⟩, exampleWideCell⟩] else [],
    fun cpu virt => if cpu then virt - 0x1000 else virt,
    [⟨16, [0, 0, 0, 0, 0, 0, 0, 0xa1]⟩, ⟨24, [0, 0, 0, 0, 0, 0, 0, 0xb2]⟩]⟩

private def initLower : AtomicEventId overlapProgram := .initial ⟨0, by decide⟩
private def initUpper : AtomicEventId overlapProgram := .initial ⟨1, by decide⟩
private def narrowStore : AtomicEventId overlapProgram := .access true ⟨0, by decide⟩

private def overlapTimeline : BlockInput overlapProgram :=
  ⟨false, fun block =>
    if block = 1 then [.store initLower, .store initUpper, .store narrowStore] else [],
    2, [3, 3]⟩

private theorem placement {block position : Nat} {op : CellStep (AtomicEventId overlapProgram)}
    (found : (overlapTimeline.timeline block)[position]? = some op) : block = 1 ∧
    ((position = 0 ∧ op = .store initLower) ∨ (position = 1 ∧ op = .store initUpper) ∨
      (position = 2 ∧ op = .store narrowStore)) := by
  have selected : block = 1 := by
    by_cases same : block = 1
    · exact same
    · simp [overlapTimeline, same] at found
  subst block
  have bound : position < 3 := (List.getElem?_eq_some_iff.mp found).1
  have whereAt : position = 0 ∨ position = 1 ∨ position = 2 := by omega
  rcases whereAt with rfl | rfl | rfl
  · exact ⟨rfl, Or.inl ⟨rfl, (Option.some.inj found).symm⟩⟩
  · exact ⟨rfl, Or.inr (Or.inl ⟨rfl, (Option.some.inj found).symm⟩)⟩
  · exact ⟨rfl, Or.inr (Or.inr ⟨rfl, (Option.some.inj found).symm⟩)⟩

private theorem placedEvent {e : AtomicEventId overlapProgram} {pos : Nat × Nat}
    (event : ByteAtomic.occursAt overlapProgram overlapTimeline.timeline e pos) :
    (pos = (1, 0) ∧ e = initLower) ∨ (pos = (1, 1) ∧ e = initUpper) ∨
      (pos = (1, 2) ∧ e = narrowStore) := by
  obtain ⟨block, ordinal⟩ := pos
  cases found : (overlapTimeline.timeline block)[ordinal]? with
  | none => simp [ByteAtomic.occursAt, readAt, writeAt, found] at event
  | some op =>
    rcases placement found with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩ <;>
      simp [ByteAtomic.occursAt, readAt, writeAt, CellStep.read, CellStep.write, found] at event
    · exact Or.inl ⟨rfl, event.symm⟩
    · exact Or.inr (Or.inl ⟨rfl, event.symm⟩)
    · exact Or.inr (Or.inr ⟨rfl, event.symm⟩)

private theorem allEvents (e : AtomicEventId overlapProgram) :
    e = initLower ∨ e = initUpper ∨ e = narrowStore := by
  cases e with
  | initial index =>
    have bound : index.val < 2 := index.isLt
    have indices : index.val = 0 ∨ index.val = 1 := by omega
    rcases indices with lo | hi
    · rw [show index = ⟨0, by decide⟩ from Fin.eq_of_val_eq lo]
      exact Or.inl rfl
    · rw [show index = ⟨1, by decide⟩ from Fin.eq_of_val_eq hi]
      exact Or.inr (Or.inl rfl)
  | access cpu index =>
    cases cpu
    · exact Fin.elim0 index
    · have bound : index.val < 1 := index.isLt
      rw [show index = ⟨0, by decide⟩ from Fin.eq_of_val_eq (show index.val = 0 by omega)]
      exact Or.inr (Or.inr rfl)

private theorem validOverlap : History overlapProgram overlapTimeline exampleInstruction
    (overlapRegisters false) exampleInputs := by
  constructor
  · decide
  · intro offset bound
    change ((16 : Word) + BitVec.ofNat 64 offset).toNat = 16 + offset
    simp only [BitVec.toNat_add, BitVec.toNat_ofNat, BitVec.toNat_ofNatLT]
    rw [show (16 : Word).toNat = 16 from rfl]
    omega
  · decide
  · intro no
    cases no
  · intro e
    have atPosition : ∃ whereAt, ByteAtomic.occursAt overlapProgram overlapTimeline.timeline e whereAt := by
      rcases allEvents e with rfl | rfl | rfl
      · exact ⟨(1, 0), Or.inr rfl⟩
      · exact ⟨(1, 1), Or.inr rfl⟩
      · exact ⟨(1, 2), Or.inr rfl⟩
    obtain ⟨pos, found⟩ := atPosition
    refine ⟨pos, found, ?_⟩
    intro other also
    rcases placedEvent found with ⟨rfl, a⟩ | ⟨rfl, a⟩ | ⟨rfl, a⟩ <;>
      rcases placedEvent also with ⟨rfl, b⟩ | ⟨rfl, b⟩ | ⟨rfl, b⟩ <;>
      first | rfl | cases (a.symm.trans b)
  · intro _ _ _ found
    rcases placement found with ⟨rfl, ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩ | ⟨rfl, rfl⟩⟩
    · exact ⟨(16, 8), by unfold aligned; decide, rfl, rfl, rfl⟩
    · exact ⟨(24, 8), by unfold aligned; decide, rfl, rfl, rfl⟩
    · exact ⟨(16, 4), by unfold aligned; decide, rfl, rfl, rfl⟩

private def expectedWide : Effect :=
  ⟨24, 16, 0xb2, some (pack 8 true 24 ++ pack 8 true 0x1122334455667788), .acquire⟩

-- The first whole-block CAS loses to the other vCPU's lower-half ST4.
example : History overlapProgram overlapTimeline exampleInstruction (overlapRegisters false) exampleInputs ∧
    replay overlapProgram overlapTimeline exampleInstruction (overlapRegisters false) exampleInputs =
      some (.retired (setNaT (setGR (overlapRegisters false) 4 0xb2) 4 false) expectedWide) :=
  ⟨validOverlap, rfl⟩

example :
    replay overlapProgram {overlapTimeline with casAt := [3]} exampleInstruction
      (overlapRegisters false) exampleInputs = some (.pending (overlapRegisters false)) := rfl

example :
    stateBefore (ByteAtomic.writeAtByte overlapProgram overlapTimeline.timeline 1 16) 3 =
      some (2, narrowStore) ∧
    stateBefore (ByteAtomic.writeAtByte overlapProgram overlapTimeline.timeline 1 31) 3 =
      some (1, initUpper) ∧
    ByteAtomic.previousByte overlapProgram overlapTimeline.timeline 1 3 16 = some 0xaa ∧
    ByteAtomic.previousByte overlapProgram overlapTimeline.timeline 1 3 31 = some 0xb2 :=
  ⟨rfl, rfl, rfl, rfl⟩

example : (List.range 16).map (fun offset =>
    memoryAfter 16 expectedWide.store (fun _ => some 0) (16 + offset)) =
      [some 0, some 0, some 0, some 0, some 0, some 0, some 0, some 24,
       some 0x11, some 0x22, some 0x33, some 0x44, some 0x55, some 0x66, some 0x77, some 0x88] := rfl

end TcgMemory.CompareWide

namespace TcgMemory.CompareWide

def Contract : Prop :=
  (∀ big upper bytes,
    sourceHalf big upper (orderWords big (operand bytes big)) =
      unpack 8 big (fun offset => bytes (if upper then offset + 8 else offset))) ∧
  (∀ i s m, beforePrimitive i s m = none →
    attempt i s {m with observations := [firstPair m]} = architectureAt i s m (firstPair m)) ∧
  (∀ {Cpu : Type} (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s after : Registers) (m : Inputs) (e : Effect),
    History p h i s m → m.parallel = true →
    replay p h i s m = some (.retired after e) → e.store.isSome = true →
    ∃ (loaded : Inputs) (index : Nat) (inside : index < h.casAt.length) (oldBytes : List Byte),
      decodeHistory p h i s m = some loaded ∧ h.loadAt ≤ h.casAt[index] ∧
        blockSnapshot p h (blockBase p h i s) h.casAt[index] = some oldBytes ∧
        firstCAS i s loaded index (decodeSnapshot m.cell.guestBig oldBytes) ∧
        architectureAt i s loaded (decodeSnapshot m.cell.guestBig oldBytes) = .retired after e) ∧
  (∀ {Cpu : Type} (p : AtomicPrograms Cpu) (h : BlockInput p) (base : Word)
    (position : Nat) (bytes : List Byte),
    blockSnapshot p h base position = some bytes →
    ∀ offset, offset < 16 → ∃ previous writer value,
      let address := base.toNat + offset
      bytes[offset]? = some value ∧ ByteAtomic.payloadByte p writer address = some value ∧
        previous < position ∧
        ByteAtomic.writeAtByte p h.timeline (base.toNat / 16) address previous = some writer ∧
        (∀ other contender, other < position →
          ByteAtomic.writeAtByte p h.timeline (base.toNat / 16) address other = some contender →
          other ≤ previous)) ∧
  (∀ i s after m e bytes, attempt i s m = .retired after e → e.store = some bytes →
    bytes = pack 8 m.cell.guestBig (getGR s i.source) ++ pack 8 m.cell.guestBig m.csd ∧
      bytes.length = 16 ∧
      ∀ base offset before, offset < 16 →
        memoryAfter base e.store before (base + offset) = bytes[offset]?) ∧
  (∀ base address stores before, address < base ∨ base + 16 ≤ address →
    memoryAfter base stores before address = before address)

theorem contract : Contract :=
  ⟨returnedHalfBytes, atStablePrimitive, replayCommitsAt, readSource,
    fun i s after m e _ done writes => completeStore i s after m e done writes, memoryOutside⟩

end TcgMemory.CompareWide
