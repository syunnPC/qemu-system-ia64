/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.LinearAtomic

-- A position is local to one aligned WB cell. It does not order
-- ordinary observations at other cells or other virtual mappings.
inductive CellStep (E : Type) where
  | load (read : E)
  | store (write : E)
  | atomic (read write : E)

def CellStep.read : CellStep E → Option E
  | .load r | .atomic r _ => some r
  | _ => none

def CellStep.write : CellStep E → Option E
  | .store w | .atomic _ w => some w
  | _ => none

def readAt (steps : List (CellStep E)) (position : Nat) : Option E :=
  steps[position]?.bind CellStep.read

def writeAt (steps : List (CellStep E)) (position : Nat) : Option E :=
  steps[position]?.bind CellStep.write

-- This is the cell-state recurrence. A read takes the old state.
-- A store or atomic step installs a new value at a single position.
def stateBefore (writes : Nat → Option E) : Nat → Option (Nat × E)
  | 0 => none
  | position + 1 =>
    match writes position with
    | some event => some (position, event)
    | none => stateBefore writes position

theorem stateBeforeIsLatest (writes : Nat → Option E) (bound source : Nat) (writer : E)
    (found : stateBefore writes bound = some (source, writer)) :
    source < bound ∧ writes source = some writer ∧
      ∀ other contender, other < bound → writes other = some contender → other ≤ source := by
  induction bound with
  | zero => simp [stateBefore] at found
  | succ previous ih =>
    cases step : writes previous with
    | none =>
      have matchPrevious : stateBefore writes previous = some (source, writer) := by
        simpa [stateBefore, step] using found
      obtain ⟨oldBefore, write, latest⟩ := ih matchPrevious
      refine ⟨Nat.lt_trans oldBefore (Nat.lt_succ_self _), write, ?_⟩
      intro other contender within event
      have less : other < previous := by
        by_cases earlier : other < previous
        · exact earlier
        · have same : other = previous := by omega
          subst other
          rw [step] at event
          cases event
      exact latest other contender less event
    | some event =>
      have same : previous = source ∧ event = writer := by
        simpa [stateBefore, step] using found
      rcases same with ⟨rfl, rfl⟩
      exact ⟨Nat.lt_succ_self _, step, fun other _ lt _ => Nat.le_of_lt_succ lt⟩

abbrev Schedule (p : AtomicPrograms Cpu) :=
  Location → List (CellStep (AtomicEventId p))

def readsFrom (p : AtomicPrograms Cpu) (s : Schedule p) (writer reader : AtomicEventId p) : Prop :=
  ∃ cell pos source, readAt (s cell) pos = some reader ∧
    stateBefore (writeAt (s cell)) pos = some (source, writer)

def coherence (p : AtomicPrograms Cpu) (s : Schedule p) (earlier later : AtomicEventId p) : Prop :=
  ∃ cell a b, writeAt (s cell) a = some earlier ∧ writeAt (s cell) b = some later ∧ a < b

def paired (p : AtomicPrograms Cpu) (s : Schedule p) (read write : AtomicEventId p) : Prop :=
  ∃ (cell : Location) (pos : Nat), (s cell)[pos]? = some (.atomic read write)

def graph (p : AtomicPrograms Cpu) (s : Schedule p)
    (dependency : TraceOrder.Edge (AtomicEventId p)) :
    TraceOrder.Execution (AtomicEventId p) Cpu Location :=
  ⟨atomicEventLabel p, atomicProgramOrder p, dependency, readsFrom p s, coherence p s,
    paired p s⟩

-- Each certificate replays actual projected events. The allowed serial
-- alternative is restricted to a single vCPU plus the WB-only domain.
-- Other observers of a serial expansion require a separate exclusivity proof.
def admissibleStep (p : AtomicPrograms Cpu) (cell : Location) :
    CellStep (AtomicEventId p) → Prop
  | .load e => (atomicEventLabel p e).kind = .load ∧ (atomicEventLabel p e).location = some cell
  | .store e => (atomicEventLabel p e).kind = .store ∧ (atomicEventLabel p e).location = some cell
  | .atomic r w =>
    issuedPair p r w ∧ (atomicEventLabel p r).kind = .rmwRead ∧
      (atomicEventLabel p w).kind = .rmwWrite ∧
      (atomicEventLabel p r).location = some cell ∧ (atomicEventLabel p w).location = some cell ∧
      ((∃ op, parallelOp p r = some op) ∨ ∀ first second : Cpu, first = second)

def hostEndianAt (p : AtomicPrograms Cpu) (read : AtomicEventId p) : Option Bool :=
  match read, atomicEventRecord p read with
  | .access cpu _, some (.atomicRead slot _ _ _) =>
    match (p.requests cpu)[slot]? with
    | some (.semaphore _ input _) => some input.hostBig
    | _ => none
  | _, _ => none

def rawAtEvent (p : AtomicPrograms Cpu) (width : Width) (hostBig : Bool)
    (e : AtomicEventId p) : Word :=
  let little := BitVec.ofNat 64 (atomicEventLabel p e).value
  if hostBig then bswapPayload width little else little

def rawBefore (p : AtomicPrograms Cpu) (s : Schedule p) (cell : Location)
    (width : Width) (hostBig : Bool) (pos : Nat) : Option Word :=
  (stateBefore (writeAt (s cell)) pos).map (fun (_, e) => rawAtEvent p width hostBig e)

def requestAt (p : AtomicPrograms Cpu) (event : AtomicEventId p) (slot : Nat) :
    Option MemoryRequest :=
  match event with
  | .access cpu _ => (p.requests cpu)[slot]?
  | .initial _ => none

def registersBefore (p : AtomicPrograms Cpu) (event : AtomicEventId p) (slot : Nat) :
    Option Registers :=
  match event with
  | .initial _ => none
  | .access cpu _ =>
    let earlierSlots := runMemory tcgStep tcgAtomic (p.regs cpu) 0 ((p.requests cpu).take slot)
    if earlierSlots.fault.isNone then some earlierSlots.regs else none

def payload (p : AtomicPrograms Cpu) (event : AtomicEventId p) : List Byte :=
  match event with
  | .initial i => p.initial[i].bytes
  | .access cpu i =>
    match (atomicTrace p cpu).accesses[i] with
    | .scalar _ (.read _ bytes _) | .scalar _ (.write _ bytes _) => bytes
    | .atomicRead _ _ _ effect => effect.before
    | .atomicWrite _ _ _ effect => effect.after

def rawLittle (bytes : List Byte) : Nat :=
  (unpack bytes.length false (fun i => bytes[i]?.getD 0)).toNat

theorem payloadIsLabelValue (p : AtomicPrograms Cpu) (e : AtomicEventId p) :
    (atomicEventLabel p e).value = rawLittle (payload p e) := by
  cases e with
  | initial _ => rfl
  | access cpu index =>
    cases record : (atomicTrace p cpu).accesses[index] with
    | scalar _ eff => cases eff <;> simp [atomicEventLabel, payload, record, event, rawLittle]
    | atomicRead _ _ _ _ => simp [atomicEventLabel, payload, record, atomicLabels, event, rawLittle]
    | atomicWrite _ _ _ _ => simp [atomicEventLabel, payload, record, atomicLabels, event, rawLittle]

def fromState (input : CellInput) (previous : List Byte) : CellInput :=
  {input with bytes := fun i => previous[i]?.getD 0}

theorem atomicReadUsesSnapshot (i : AtomicInstruction) (regs : Registers) (m : CellInput)
    (parallel : Bool) (previous : List Byte) (effect : AtomicEffect)
    (length : previous.length = i.op.width.bytes)
    (committed : (tcgAtomic i regs (fromState m previous) parallel).effect = some effect) :
    effect.before = previous := by
  rw [atomicMatches] at committed
  unfold atomicSpec at committed
  repeat (first | split at committed <;> try contradiction | contradiction)
  all_goals dsimp at committed
  all_goals
    have packet := Option.some.inj committed
    rw [← packet]
    change (List.range i.op.width.bytes).map (fun n => previous[n]?.getD 0) = previous
    rw [← length]
    apply List.ext_getElem (by simp)
    intro n _ hn
    simp [List.getElem?_eq_getElem hn]

-- The RMW case executes the selected helper projection on the
-- actual preceding cell bytes. The projected pre-registers come
-- from running the preceding slots of this vCPU.
-- Ordinary read values still have an explicit replay check.
def replayReadOn (p : AtomicPrograms Cpu) (read : AtomicEventId p) (bytes : List Byte) : Prop :=
  match atomicEventRecord p read with
  | some (.atomicRead slot op path effect) =>
    ∃ (i : AtomicInstruction) (regs : Registers) (context : CellInput) (parallel : Bool),
      requestAt p read slot = some (.semaphore i context parallel) ∧
      registersBefore p read slot = some regs ∧
      i.op = op ∧ chosenAtomicPath i context parallel = path ∧
      bytes.length = i.op.width.bytes ∧
      (tcgAtomic i regs (fromState context bytes) parallel).effect = some effect
  | _ => payload p read = bytes

def readMatches (p : AtomicPrograms Cpu) (read writer : AtomicEventId p) : Prop :=
  replayReadOn p read (payload p writer)

theorem payloadFromRecord (p : AtomicPrograms Cpu) (read : AtomicEventId p)
    {slot : Nat} {op : AtomicOperand} {path : AtomicPath} {effect : AtomicEffect}
    (record : atomicEventRecord p read = some (.atomicRead slot op path effect)) :
    payload p read = effect.before := by
  cases read with
  | initial _ => simp [atomicEventRecord] at record
  | access cpu index =>
    have phase := Option.some.inj record
    simp [payload, phase]

theorem replayReadBytes (p : AtomicPrograms Cpu) (read : AtomicEventId p) (bytes : List Byte)
    (replayed : replayReadOn p read bytes) : payload p read = bytes := by
  unfold replayReadOn at replayed
  split at replayed
  next slot op path effect h =>
    obtain ⟨i, regs, input, parallel, _, _, _, _, width, replay⟩ := replayed
    rw [payloadFromRecord p read h]
    exact atomicReadUsesSnapshot i regs input parallel bytes effect width replay
  next => exact replayed

theorem readMatchesValue (p : AtomicPrograms Cpu) (read writer : AtomicEventId p)
    (replayed : readMatches p read writer) :
    (atomicEventLabel p read).value = (atomicEventLabel p writer).value := by
  rw [payloadIsLabelValue, payloadIsLabelValue, replayReadBytes p read _ replayed]

-- The reverse endian implementation first reads a candidate, then
-- retries CAS. Every observation comes from a state of this cell.
-- The last attempt is the only one that can change that state.
def checkedAttempts (p : AtomicPrograms Cpu) (s : Schedule p) (cell : Location) (pos : Nat)
    (r w : AtomicEventId p) : Prop :=
  match atomicEventRecord p r with
  | some (.atomicRead _ (.fetchAdd width increment _) .reverseHelper _) =>
    ∃ (hostBig : Bool) (initial : Nat) (failed : List Nat)
      (expected : Word) (observed : List Word),
      hostEndianAt p r = some hostBig ∧
      ((initial :: failed) ++ [pos]).Pairwise (· ≤ ·) ∧
      rawBefore p s cell width.width hostBig initial = some expected ∧
      (failed ++ [pos]).mapM (rawBefore p s cell width.width hostBig) = some observed ∧
      (∀ k, k < observed.length →
        reverseFetchAddAttempts width increment expected (observed.take k) = none) ∧
      reverseFetchAddAttempts width increment expected observed =
        some (rawAtEvent p width.width hostBig r, rawAtEvent p width.width hostBig w)
  | _ => True

structure Admitted (p : AtomicPrograms Cpu) (s : Schedule p) : Prop where
  alignedCells : ∀ (cell : Location) (pos : Nat), (s cell)[pos]?.isSome = true → aligned cell
  compatibleCells : ∀ (a b : Location) (i j : Nat),
    (s a)[i]?.isSome = true → (s b)[j]?.isSome = true → separate a b
  steps : ∀ (cell : Location) (pos : Nat) (op : CellStep (AtomicEventId p)),
    (s cell)[pos]? = some op → admissibleStep p cell op
  reverseAttempts : ∀ (cell : Location) (pos : Nat) r w,
    (s cell)[pos]? = some (.atomic r w) → checkedAttempts p s cell pos r w
  readsOnce : ∀ a b i j e, readAt (s a) i = some e → readAt (s b) j = some e →
    a = b ∧ i = j
  writesOnce : ∀ a b i j e, writeAt (s a) i = some e → writeAt (s b) j = some e →
    a = b ∧ i = j
  coversReads : ∀ e, (atomicEventLabel p e).kind.read = true →
    ∃ cell pos, readAt (s cell) pos = some e
  coversWrites : ∀ e, (atomicEventLabel p e).kind.write = true →
    ∃ cell pos, writeAt (s cell) pos = some e
  readsState : ∀ cell pos e, readAt (s cell) pos = some e →
    ∃ source writer, stateBefore (writeAt (s cell)) pos = some (source, writer) ∧
      readMatches p e writer

theorem pairPositions {p : AtomicPrograms Cpu} {s : Schedule p}
    {cell : Location} {pos : Nat} {r w : AtomicEventId p}
    (placed : (s cell)[pos]? = some (.atomic r w)) :
    readAt (s cell) pos = some r ∧ writeAt (s cell) pos = some w := by
  simp [readAt, writeAt, placed, CellStep.read, CellStep.write]

theorem pairedUniqueWrite {p : AtomicPrograms Cpu} {s : Schedule p}
    {r w other : AtomicEventId p}
    (accepted : Admitted p s) (first : paired p s r w) (second : paired p s r other) :
    w = other := by
  obtain ⟨cell, position, left⟩ := first
  obtain ⟨cell', position', right⟩ := second
  obtain ⟨rfl, rfl⟩ := accepted.readsOnce cell cell' position position' r
    (pairPositions left).1 (pairPositions right).1
  rw [left] at right
  cases right
  rfl

theorem pairedUniqueRead {p : AtomicPrograms Cpu} {s : Schedule p}
    {r w other : AtomicEventId p}
    (accepted : Admitted p s) (first : paired p s r w) (second : paired p s other w) :
    r = other := by
  obtain ⟨cell, position, left⟩ := first
  obtain ⟨cell', position', right⟩ := second
  obtain ⟨rfl, rfl⟩ := accepted.writesOnce cell cell' position position' w
    (pairPositions left).2 (pairPositions right).2
  rw [left] at right
  cases right
  rfl

theorem pairedReadExists {p : AtomicPrograms Cpu} {s : Schedule p}
    (accepted : Admitted p s) (r : AtomicEventId p)
    (kind : (atomicEventLabel p r).kind = .rmwRead) :
    ∃ w, paired p s r w ∧ ∀ other, paired p s r other → other = w := by
  have isRead : (atomicEventLabel p r).kind.read = true := by simp [kind, TraceOrder.Kind.read]
  obtain ⟨cell, pos, observed⟩ := accepted.coversReads r isRead
  cases h : (s cell)[pos]? with
  | none => simp [readAt, h] at observed
  | some action =>
    have ok := accepted.steps cell pos action h
    cases action with
    | load e =>
      have same : e = r := by simpa [readAt, h, CellStep.read] using observed
      subst e
      simp [admissibleStep, kind] at ok
    | store _ => simp [readAt, h, CellStep.read] at observed
    | atomic e w =>
      have same : e = r := by simpa [readAt, h, CellStep.read] using observed
      subst e
      have pairedAt : paired p s r w := ⟨cell, pos, h⟩
      exact ⟨w, pairedAt, fun _ other => pairedUniqueWrite accepted other pairedAt⟩

theorem pairedWriteExists {p : AtomicPrograms Cpu} {s : Schedule p}
    (accepted : Admitted p s) (w : AtomicEventId p)
    (kind : (atomicEventLabel p w).kind = .rmwWrite) :
    ∃ r, paired p s r w ∧ ∀ other, paired p s other w → other = r := by
  have isWrite : (atomicEventLabel p w).kind.write = true := by simp [kind, TraceOrder.Kind.write]
  obtain ⟨cell, pos, observed⟩ := accepted.coversWrites w isWrite
  cases h : (s cell)[pos]? with
  | none => simp [writeAt, h] at observed
  | some action =>
    have ok := accepted.steps cell pos action h
    cases action with
    | load _ => simp [writeAt, h, CellStep.write] at observed
    | store e =>
      have same : e = w := by simpa [writeAt, h, CellStep.write] using observed
      subst e
      simp [admissibleStep, kind] at ok
    | atomic r e =>
      have same : e = w := by simpa [writeAt, h, CellStep.write] using observed
      subst e
      have pairedAt : paired p s r w := ⟨cell, pos, h⟩
      exact ⟨r, pairedAt, fun _ other => pairedUniqueRead accepted other pairedAt⟩

theorem linkedAtomicPairs {p : AtomicPrograms Cpu} {s : Schedule p}
    (accepted : Admitted p s) (dependency : TraceOrder.Edge (AtomicEventId p)) :
    TraceOrder.AtomicPairs (graph p s dependency) := by
  intro r w old rmw rf
  obtain ⟨cell, pos, placed⟩ := rmw
  obtain ⟨fromCell, readPos, writerPos, observed, selected⟩ := rf
  obtain ⟨rfl, rfl⟩ := accepted.readsOnce cell fromCell pos readPos r
    (pairPositions placed).1 observed
  obtain ⟨before, oldAt, latest⟩ := stateBeforeIsLatest _ _ _ _ selected
  have write := (pairPositions placed).2
  refine ⟨⟨cell, writerPos, pos, oldAt, write, before⟩, ?_⟩
  rintro ⟨between, ⟨c1, wr, mid1, lower, step1, beforeMid⟩,
    ⟨c2, mid2, last, step2, upper, afterMid⟩⟩
  obtain ⟨rfl, rfl⟩ := accepted.writesOnce cell c1 writerPos wr old oldAt lower
  obtain ⟨rfl, rfl⟩ := accepted.writesOnce cell c2 pos last w write upper
  obtain ⟨_, rfl⟩ := accepted.writesOnce cell cell mid1 mid2 between step1 step2
  exact (Nat.not_le_of_lt beforeMid) (latest mid1 between afterMid step1)

theorem sourceValueFromState {p : AtomicPrograms Cpu} {s : Schedule p}
    (accepted : Admitted p s) (e : AtomicEventId p)
    (isRead : (atomicEventLabel p e).kind.read = true) :
    ∃ writer, readsFrom p s writer e ∧
      (atomicEventLabel p e).value = (atomicEventLabel p writer).value := by
  obtain ⟨cell, pos, read⟩ := accepted.coversReads e isRead
  obtain ⟨source, writer, before, value⟩ := accepted.readsState cell pos e read
  exact ⟨writer, ⟨cell, pos, source, read, before⟩, readMatchesValue p e writer value⟩

theorem readSourceUnique {p : AtomicPrograms Cpu} {s : Schedule p}
    {reader left right : AtomicEventId p}
    (accepted : Admitted p s) (first : readsFrom p s left reader)
    (second : readsFrom p s right reader) : left = right := by
  obtain ⟨cell, pos, source, observed, found⟩ := first
  obtain ⟨cell', pos', source', observed', found'⟩ := second
  obtain ⟨rfl, rfl⟩ := accepted.readsOnce cell cell' pos pos' reader observed observed'
  exact congrArg Prod.snd (Option.some.inj (found.symm.trans found'))

def parallelRead (p : AtomicPrograms Cpu) (read : AtomicEventId p) : Prop :=
  (atomicEventLabel p read).kind = .rmwRead ∧ ∃ op, parallelOp p read = some op

-- The portable SC helper order is a separate witness for successful
-- parallel RMW calls. It must agree with their per-cell coherence
-- and same-vCPU order. No ordinary event receives a rank constraint.
structure HelperOrder (p : AtomicPrograms Cpu) (s : Schedule p) where
  rank : AtomicEventId p → Nat
  unique : ∀ a b, parallelRead p a → parallelRead p b → rank a = rank b → a = b
  sameCell : ∀ r w laterRead laterWrite, parallelRead p r → parallelRead p laterRead →
    paired p s r w → paired p s laterRead laterWrite →
    coherence p s w laterWrite → rank r < rank laterRead
  localOrder : ∀ a b, parallelRead p a → parallelRead p b →
    atomicProgramOrder p a b → rank a < rank b

theorem HelperOrder.total {p : AtomicPrograms Cpu} {s : Schedule p} (order : HelperOrder p s)
    {a b : AtomicEventId p} (first : parallelRead p a) (second : parallelRead p b)
    (distinct : a ≠ b) : order.rank a < order.rank b ∨ order.rank b < order.rank a := by
  have different : order.rank a ≠ order.rank b :=
    fun same => distinct (order.unique a b first second same)
  omega

def AtomicLinearityContract : Prop :=
  ∀ {Cpu : Type} (p : AtomicPrograms Cpu) (s : Schedule p), Admitted p s →
    (∀ dependency, TraceOrder.AtomicPairs (graph p s dependency)) ∧
    (∀ e, (atomicEventLabel p e).kind.read = true →
      ∃ w, readsFrom p s w e ∧
        (atomicEventLabel p e).value = (atomicEventLabel p w).value ∧
        ∀ other, readsFrom p s other e → other = w) ∧
    (∀ r, (atomicEventLabel p r).kind = .rmwRead →
      ∃ w, paired p s r w ∧ ∀ other, paired p s r other → other = w) ∧
    (∀ w, (atomicEventLabel p w).kind = .rmwWrite →
      ∃ r, paired p s r w ∧ ∀ other, paired p s other w → other = r)

theorem linearAtomicContract : AtomicLinearityContract := by
  intro Cpu p s accepted
  refine ⟨linkedAtomicPairs accepted, ?_, pairedReadExists accepted, pairedWriteExists accepted⟩
  intro reader isRead
  obtain ⟨writer, source, same⟩ := sourceValueFromState accepted reader isRead
  exact ⟨writer, source, same, fun _ alternative => readSourceUnique accepted alternative source⟩

end TcgMemory.LinearAtomic
