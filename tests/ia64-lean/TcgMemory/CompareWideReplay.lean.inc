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
  ordered : (h.loadAt :: h.casAt).Pairwise (· ≤ ·)
  exclusiveSerial : m.parallel = false → ∀ other : Cpu, other = h.cpu
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

theorem observationsMatch (p : AtomicPrograms Cpu) (h : BlockInput p)
    (i : Instruction) (s : Registers) (m loaded : Inputs)
    (decoded : decodeHistory p h i s m = some loaded) :
    ∃ bytes, blockSnapshot p h (blockBase p h i s) h.loadAt = some bytes ∧
      firstPair loaded = decodeSnapshot m.cell.guestBig bytes ∧
      ∀ n (inside : n < h.casAt.length), ∃ seen,
        blockSnapshot p h (blockBase p h i s) h.casAt[n] = some seen ∧
          loaded.observations[n]? = some (decodeSnapshot m.cell.guestBig seen) := by
  unfold decodeHistory at decoded
  let image := blockSnapshot p h (blockBase p h i s)
  let element := fun pos => (image pos).map (decodeSnapshot m.cell.guestBig)
  change ((image h.loadAt).bind fun old => (h.casAt.mapM element).map fun seen =>
    { m with
      cell := {m.cell with bytes := fun offset => old[offset]?.getD 0}
      observations := seen }) = some loaded at decoded
  cases initial : image h.loadAt with
  | none => simp [initial] at decoded
  | some bytes =>
    cases attempts : h.casAt.mapM element with
    | none => simp [initial, attempts] at decoded
    | some observations =>
      simp only [initial, attempts, Option.bind_some, Option.map_some, Option.some.injEq] at decoded
      subst loaded
      refine ⟨bytes, initial, rfl, ?_⟩
      intro n inside
      obtain ⟨result, step, stored⟩ := ByteAtomic.mapMValue element h.casAt observations attempts n inside
      cases available : image h.casAt[n] with
      | none => simp [element, available] at step
      | some seen =>
        have expected : decodeSnapshot m.cell.guestBig seen = result := by
          simpa [element, available] using step
        exact ⟨seen, available, expected.symm ▸ stored⟩

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
