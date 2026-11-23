/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.Compare

structure Instruction where
  qp : Pred
  target : Reg
  base : Reg
  source : Reg
  width : Width
  release : Bool

structure Inputs where
  cell : CellInput
  ccv : Word
  restartHelper : Bool
  parallel : Bool

inductive Route where
  | loadOnly | serial | atomic | restartAtomic
  deriving DecidableEq

structure Effect where
  address : Word
  before : List Byte
  committed : Option (List Byte)
  order : TraceOrder.Order

structure Attempt where
  effect : Effect
  route : Route
  store : Option (List Byte)

structure AttemptResult where
  regs : Registers
  attempt : Option Attempt
  exit : Exit

structure ProjectedResult where
  regs : Registers
  access : Option Effect
  exit : Exit

def AttemptResult.project (r : AttemptResult) : ProjectedResult :=
  ⟨r.regs, r.attempt.map Attempt.effect, r.exit⟩

def stop (s : Registers) (why : Exit) : AttemptResult := ⟨s, none, why⟩

def architecture (i : Instruction) (s : Registers) (m : Inputs) : ProjectedResult :=
  if !predicate s i.qp then ⟨s, none, .nullified⟩
  else if !mayWrite s i.target then ⟨s, none, .illegalRegister⟩
  else if getNat s i.base then ⟨s, none, .addressNaT⟩
  else if getNat s i.source then ⟨s, none, .dataNaT⟩
  else if !m.cell.preflight then ⟨s, none, .preflight⟩
  else if !m.cell.primitiveReturns then ⟨s, none, .primitiveAbort⟩
  else
    let address := getGR s i.base
    let old := unpack i.width.bytes m.cell.guestBig m.cell.bytes
    let committed := if old = m.ccv then
      some (pack i.width.bytes m.cell.guestBig (getGR s i.source)) else none
    let updated := setNaT (setGR s i.target old) i.target false
    ⟨updated, some ⟨address, (List.range i.width.bytes).map m.cell.bytes,
      committed, if i.release then .release else .acquire⟩, .success⟩

def low (width : Width) (value : Word) : Word :=
  (value.truncate (8 * width.bytes)).zeroExtend 64

def runtimeLoadOnly (w : Width) (ccv : Word) : Bool :=
  match w with
  | .w1 => decide ((0xff : Word) < ccv)
  | .w2 => decide ((0xffff : Word) < ccv)
  | .w4 => decide ((0xffffffff : Word) < ccv)
  | .w8 => false

def usesLoadOnly (w : Width) (m : Inputs) : Bool :=
  if m.restartHelper then runtimeLoadOnly w m.ccv else sourceInlineLoadOnly w m.ccv

def route (w : Width) (m : Inputs) : Route :=
  if usesLoadOnly w m then .loadOnly
  else if m.restartHelper then .restartAtomic
  else if m.parallel then .atomic
  else .serial

-- The serial TCG lowering emits a store even on comparison failure,
-- using the old load value. A failed portable CAS commits no store.
def source (i : Instruction) (s : Registers) (m : Inputs) : AttemptResult :=
  if !predicate s i.qp then stop s .nullified
  else if !tcgMayWrite s i.target then stop s .illegalRegister
  else if getNat s i.base then stop s .addressNaT
  else if getNat s i.source then stop s .dataNaT
  else if !m.cell.preflight then stop s .preflight
  else if !m.cell.primitiveReturns then stop s .primitiveAbort
  else
    let chosen := route i.width m
    let layout := tcgLayout (compareDescriptor i.width) m.cell.hostBig m.cell.guestBig
    let cell := {m.cell with hasMemBswap := m.cell.hasMemBswap &&
      (chosen == .serial || chosen == .loadOnly)}
    let old := emittedLoadValue i.width layout cell
    let hit := !usesLoadOnly i.width m && old == low i.width m.ccv
    let update := emittedStoreValue i.width layout cell (sourceGR s i.source)
    let before := (List.range layout.1).map m.cell.bytes
    let committed := if hit then some update else none
    let kept := emittedStoreValue i.width layout cell old
    let store := if chosen = .serial then some (if hit then update else kept) else committed
    let updated := setNaT (setGR s i.target old) i.target false
    let effect := ⟨sourceGR s i.base, before, committed, if i.release then .release else .acquire⟩
    ⟨updated, some ⟨effect, chosen, store⟩, .success⟩

theorem descriptor (w : Width) :
    compareDescriptor w = loadDescriptor w ∧ compareDescriptor w = storeDescriptor w false := by
  cases w <;> exact ⟨rfl, rfl⟩

private theorem oldFits (w : Width) (m : CellInput) :
    low w (unpack w.bytes m.guestBig m.bytes) = unpack w.bytes m.guestBig m.bytes := by
  cases w <;> cases m.guestBig <;>
    simp [low, Width.bytes, unpack, significance, List.range, List.range.loop, List.foldl] <;>
    simp (disch := decide) only [BitVec.ushiftRight_eq_extractLsb'_of_lt,
      BitVec.shiftLeft_eq_concat_of_lt] <;> bv_decide

private theorem fullWidthComparison (w : Width) (m : Inputs) :
    let old := unpack w.bytes m.cell.guestBig m.cell.bytes
    (!usesLoadOnly w m && (old == low w m.ccv)) = (old == m.ccv) := by
  dsimp only
  have fits := oldFits w m.cell
  generalize unpack w.bytes m.cell.guestBig m.cell.bytes = old at fits ⊢
  rw [Bool.eq_iff_iff]
  simp only [Bool.and_eq_true, Bool.not_eq_true, beq_iff_eq]
  cases w <;> cases restart : m.restartHelper <;>
    simp [usesLoadOnly, low, runtimeLoadOnly, sourceInlineLoadOnly, Width.bytes, restart] at * <;>
    bv_decide

private theorem readValue (w : Width) (m : CellInput) (swap : Bool) :
    emittedLoadValue w (tcgLayout (compareDescriptor w) m.hostBig m.guestBig)
      {m with hasMemBswap := swap} = unpack w.bytes m.guestBig m.bytes := by
  rw [(descriptor w).1]
  exact emittedLoadBytes w {m with hasMemBswap := swap}

private theorem writeBytes (w : Width) (m : CellInput) (swap : Bool) (value : Word) :
    emittedStoreValue w (tcgLayout (compareDescriptor w) m.hostBig m.guestBig)
      {m with hasMemBswap := swap} value = pack w.bytes m.guestBig value := by
  rw [(descriptor w).2]
  exact emittedStoreBytes w false {m with hasMemBswap := swap} value

private theorem layoutSize (w : Width) (m : CellInput) :
    (tcgLayout (compareDescriptor w) m.hostBig m.guestBig).1 = w.bytes := by
  rw [(descriptor w).1, loadLayout]

private theorem roundTrip (w : Width) (m : CellInput) :
    pack w.bytes m.guestBig (unpack w.bytes m.guestBig m.bytes) =
      (List.range w.bytes).map m.bytes := by
  cases w <;> cases big : m.guestBig <;>
    simp [big, pack, unpack, significance, Width.bytes, List.range, List.range.loop,
      List.map, List.foldl, List.cons.injEq] <;>
    simp (disch := decide) only [BitVec.ushiftRight_eq_extractLsb'_of_lt,
      BitVec.shiftLeft_eq_concat_of_lt] <;> bv_decide

theorem compareMatches (i : Instruction) (s : Registers) (m : Inputs) :
    (source i s m).project = architecture i s m := by
  have projectIf {p : Prop} [Decidable p] (a b : AttemptResult) :
      (if p then a else b).project = if p then a.project else b.project := by
    split <;> rfl
  simp only [source, architecture, legalTarget, readValue, writeBytes, layoutSize, sourceRegister,
    fullWidthComparison, beq_iff_eq, projectIf]
  simp only [stop, AttemptResult.project, Option.map]

theorem rejectsBeforeAttempt (i : Instruction) (s : Registers) (m : Inputs)
    (stops : (source i s m).exit ≠ .success) :
    (source i s m).regs = s ∧ (source i s m).attempt = none := by
  unfold source at *
  iterate 6
    (split at stops <;> rename_i guard <;>
      simp only [guard, Bool.false_eq_true, if_true, if_false, stop] <;>
      try exact ⟨True.intro, True.intro⟩)
  exact (stops rfl).elim

-- Distinguish an ordinary load from a failed CAS and from a serial
-- expansion that issues a physically redundant WB store.
theorem failureRoutes (i : Instruction) (s : Registers) (m : Inputs) (a : Attempt)
    (run : (source i s m).attempt = some a) :
    (a.route = .loadOnly → a.effect.committed = none ∧ a.store = none) ∧
    (a.effect.committed = none → a.store =
      if a.route = .serial then some a.effect.before else none) := by
  unfold source at run
  iterate 6
    split at run
    · cases run
  have found : some _ = some a := run
  cases Option.some.inj found
  simp only [readValue, writeBytes, layoutSize, roundTrip]
  by_cases only : usesLoadOnly i.width m
  · simp [only, route]
  · cases restart : m.restartHelper <;> cases parallel : m.parallel <;>
      simp [only, route, restart, parallel] <;> (intro h; simp [h])

-- The plain-load path initiating the excess-CCV .rel ordering
-- refutation in TraceOrder retains the high bits in this guard.
theorem loadOnlyExactlyOversized (w : Width) (m : Inputs) :
    route w m = .loadOnly ↔ low w m.ccv ≠ m.ccv := by
  have guard : usesLoadOnly w m = decide (low w m.ccv ≠ m.ccv) := by
    rw [Bool.eq_iff_iff]
    cases w <;> cases restart : m.restartHelper <;>
      simp [usesLoadOnly, runtimeLoadOnly, sourceInlineLoadOnly, low, Width.bytes, restart] <;>
      bv_decide
  unfold route
  rw [guard]
  by_cases oversized : low w m.ccv ≠ m.ccv
  · simp [oversized]
  · cases restart : m.restartHelper <;> cases parallel : m.parallel <;>
      simp [oversized, restart, parallel]

private def alias : Instruction := ⟨0, 4, 4, 4, .w1, true⟩
private def aliasCell : CellInput :=
  {exampleCell with guestBig := false, bytes := fun _ => 0xab}

example :
    let r := source alias exampleRegs ⟨aliasCell, 0xab, false, true⟩
    r.regs.gr 4 = 0xab ∧ r.attempt.map (fun a => a.effect.address) = some 16 ∧
      r.attempt.bind Attempt.store = some [16] := ⟨rfl, rfl, rfl⟩

example :
    let r := source alias exampleRegs ⟨aliasCell, 0x1ab, false, true⟩
    r.regs.gr 4 = 0xab ∧ r.attempt.map Attempt.route = some .loadOnly ∧
      r.attempt.bind Attempt.store = none := ⟨rfl, rfl, rfl⟩

example :
    let r := source alias exampleRegs ⟨aliasCell, 0xaa, false, false⟩
    r.attempt.bind (fun a => a.effect.committed) = none ∧ r.attempt.bind Attempt.store = some [0xab] :=
  ⟨rfl, rfl⟩

example :
    let r := source alias exampleRegs ⟨aliasCell, 0xaa, true, true⟩
    r.attempt.map Attempt.route = some .restartAtomic ∧ r.attempt.bind Attempt.store = none :=
  ⟨rfl, rfl⟩

example :
    (source {alias with qp := 7} exampleRegs ⟨{aliasCell with primitiveReturns := false},
      0xab, false, true⟩).exit = .nullified := rfl

example :
    (source alias exampleRegs ⟨{aliasCell with preflight := false},
      0x1ab, false, true⟩).attempt = none := rfl

example :
    (source alias exampleRegs ⟨{aliasCell with primitiveReturns := false},
      0xab, false, true⟩).attempt = none := rfl

end TcgMemory.Compare

namespace TcgMemory.Compare

def replay (i : Instruction) (s : Registers) (m : Inputs) (bytes : List Byte) : AttemptResult :=
  source i s {m with cell := {m.cell with bytes := fun offset => bytes[offset]?.getD 0}}

private theorem sourceReads (i : Instruction) (s : Registers) (m : Inputs) (a : Attempt)
    (issued : (source i s m).attempt = some a) :
    a.effect.before = (List.range i.width.bytes).map m.cell.bytes := by
  unfold source at issued
  iterate 6
    (split at issued <;> try contradiction)
  have found : some _ = some a := issued
  cases Option.some.inj found
  exact congrArg (fun n => (List.range n).map m.cell.bytes) (layoutSize i.width m.cell)

-- The compare is the next access to this physical footprint. The
-- supplied coherent WB history contains the writes it can observe.
-- This replay validates the input bytes without giving extra ordering
-- edges to a failed CAS, serial access, or oversized-CCV plain load.
theorem replayByteSource (p : AtomicPrograms Cpu) (history : ByteAtomic.BlockSchedule p)
    (physical : Word) (position : Nat)
    (i : Instruction) (s : Registers) (m : Inputs) (bytes : List Byte) (a : Attempt)
    (valid : aligned (physical, i.width.bytes))
    (assembled : ByteAtomic.snapshot p history (physical.toNat / 8) position
      (physical, i.width.bytes) = some bytes)
    (attempted : (replay i s m bytes).attempt = some a) (offset : Nat) (bound : offset < i.width.bytes) :
    ∃ previous writer value,
      let address := physical.toNat + offset
      address / 8 = physical.toNat / 8 ∧
      a.effect.before[offset]? = some value ∧
        ByteAtomic.payloadByte p writer address = some value ∧ previous < position ∧
        ByteAtomic.writeAtByte p history (physical.toNat / 8) address previous = some writer ∧
        (∀ other contender, other < position →
          ByteAtomic.writeAtByte p history (physical.toNat / 8) address other = some contender →
          other ≤ previous) := by
  have shape := sourceReads i s {m with cell := {m.cell with bytes := fun j => bytes[j]?.getD 0}} a
    attempted
  obtain ⟨value, previous, byte⟩ :=
    ByteAtomic.mapMValue _ (List.range i.width.bytes) bytes assembled offset (by simpa using bound)
  simp only [List.getElem_range] at previous
  let locate := ByteAtomic.writeAtByte p history (physical.toNat / 8) (physical.toNat + offset)
  change (LinearAtomic.stateBefore locate position).bind
    (fun (_, writer) => ByteAtomic.payloadByte p writer (physical.toNat + offset)) = some value
    at previous
  cases found : LinearAtomic.stateBefore locate position with
  | none => simp [found] at previous
  | some info =>
    obtain ⟨earlier, writer⟩ := info
    obtain ⟨earlierThan, written, latest⟩ :=
      LinearAtomic.stateBeforeIsLatest locate position earlier writer found
    have stored : ByteAtomic.payloadByte p writer (physical.toNat + offset) = some value := by
      simpa [found] using previous
    have rangeByte : (List.range i.width.bytes)[offset]? = some offset := by
      rw [List.getElem?_eq_getElem (by simpa using bound), List.getElem_range]
    refine ⟨earlier, writer, value, ByteAtomic.alignedByteBlock _ valid offset bound,
      ?_, stored, earlierThan, written, latest⟩
    rw [shape, List.getElem?_map, rangeByte]
    simp [byte]

def Contract : Prop :=
  (∀ i s m, (source i s m).project = architecture i s m) ∧
  (∀ i s m, (source i s m).exit ≠ .success →
    (source i s m).regs = s ∧ (source i s m).attempt = none) ∧
  (∀ i s m a, (source i s m).attempt = some a →
    (a.route = .loadOnly → a.effect.committed = none ∧ a.store = none) ∧
    (a.effect.committed = none →
      a.store = if a.route = .serial then some a.effect.before else none)) ∧
  (∀ w m, route w m = .loadOnly ↔ low w m.ccv ≠ m.ccv) ∧
  (∀ {Cpu : Type} (p : AtomicPrograms Cpu) (history : ByteAtomic.BlockSchedule p)
    (physical : Word) (position : Nat) (i : Instruction) (s : Registers) (m : Inputs)
    (bytes : List Byte) (a : Attempt),
    aligned (physical, i.width.bytes) →
    ByteAtomic.snapshot p history (physical.toNat / 8) position (physical, i.width.bytes) =
      some bytes → (replay i s m bytes).attempt = some a →
    ∀ offset, offset < i.width.bytes → ∃ previous writer value,
      let address := physical.toNat + offset
      address / 8 = physical.toNat / 8 ∧
        a.effect.before[offset]? = some value ∧ ByteAtomic.payloadByte p writer address = some value ∧
        previous < position ∧
        ByteAtomic.writeAtByte p history (physical.toNat / 8) address previous = some writer ∧
        (∀ other contender, other < position →
          ByteAtomic.writeAtByte p history (physical.toNat / 8) address other = some contender →
          other ≤ previous))

theorem contract : Contract :=
  ⟨compareMatches, rejectsBeforeAttempt, failureRoutes, loadOnlyExactlyOversized, replayByteSource⟩

end TcgMemory.Compare

namespace TcgMemory.ByteAtomic

private def compareRequest : Compare.Instruction := ⟨0, 4, 4, 4, .w8, true⟩

-- The two aliases use different virtual addresses. CMPXCHG8 reads
-- the upper four bytes written by the other vCPU's ST4 and preserves
-- the initial writer's lower four bytes in the compared operand.
example :
    let observed := snapshot program schedule 2 2 (16, 8)
    let loaded := observed.map (Compare.replay compareRequest (sampleRegs false)
      ⟨exchangeCell, 0xaabbccdd00000001, false, true⟩)
    loaded.bind (fun r => r.attempt.bind Compare.Attempt.store) =
      some [16, 0, 0, 0, 0, 0, 0, 0] := rfl

example :
    let observed := snapshot program schedule 2 2 (16, 8)
    let loaded := observed.map (Compare.replay compareRequest (sampleRegs false)
      ⟨exchangeCell, 1, false, true⟩)
    loaded.bind (fun r => r.attempt.bind Compare.Attempt.store) = none := rfl

end TcgMemory.ByteAtomic
