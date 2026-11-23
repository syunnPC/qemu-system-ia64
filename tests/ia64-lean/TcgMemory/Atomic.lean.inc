/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

def atomicDescriptor : AtomicOperand → BitVec 32
  | .exchange w _ => exchangeDescriptor w
  | .fetchAdd w _ _ => fetchAddDescriptor w.width

theorem atomicDescriptorMatches (op : AtomicOperand) :
    atomicDescriptor op = loadDescriptor op.width ∧
      atomicDescriptor op = storeDescriptor op.width false := by
  cases op with
  | exchange w _ => cases w <;> exact ⟨rfl, rfl⟩
  | fetchAdd w _ _ => cases w <;> exact ⟨rfl, rfl⟩

def sourceAtomicValue (op : AtomicOperand) (s : Registers) (old : Word)
    (parallel reverseEndian : Bool) : Word :=
  match op with
  | .exchange _ source => sourceGR s source
  | .fetchAdd _ increment _ =>
    if parallel && reverseEndian then reverseAdd old increment.value
    else old + increment.value

def atomicReturnsOld (op : AtomicOperand) (parallel : Bool) : Bool :=
  if parallel then true
  else match op with
    | .exchange .. => serialExchangeReturnsOld
    | .fetchAdd .. => serialAddReturnsOld

-- Parallel calls select an endian-specific helper. The serial path
-- emits load/op/store with the same descriptor and is atomic only
-- under exclusive execution of the translated operation.
def tcgAtomic (i : AtomicInstruction) (s : Registers) (m : CellInput)
    (parallel : Bool) : AtomicResult :=
  if !predicate s i.qp then atomicStop s .nullified
  else if !tcgMayWrite s i.target then atomicStop s .illegalRegister
  else if getNat s i.base then atomicStop s .addressNaT
  else if atomicDataNat i.op s then atomicStop s .dataNaT
  else if !m.preflight then atomicStop s .preflight
  else if !m.primitiveReturns then atomicStop s .primitiveAbort
  else
    let address := sourceGR s i.base
    let layout := tcgLayout (atomicDescriptor i.op) m.hostBig m.guestBig
    let old := emittedLoadValue i.op.width layout {m with hasMemBswap := !parallel && m.hasMemBswap}
    let replacement := sourceAtomicValue i.op s old parallel (layout.2 != m.hostBig)
    let result := if atomicReturnsOld i.op parallel then old
      else (replacement.extractLsb' 0 (layout.1 * 8)).zeroExtend 64
    let written := emittedStoreValue i.op.width layout
      {m with hasMemBswap := !parallel && m.hasMemBswap} replacement
    let updated := setNaT (setGR s i.target result) i.target false
    ⟨updated, some ⟨address, (List.range layout.1).map m.bytes, written, i.op.order⟩, .success⟩

theorem atomicReadBytes (op : AtomicOperand) (m : CellInput) (parallel : Bool) :
    emittedLoadValue op.width (tcgLayout (atomicDescriptor op) m.hostBig m.guestBig)
      {m with hasMemBswap := !parallel && m.hasMemBswap} =
      unpack op.width.bytes m.guestBig m.bytes := by
  rw [(atomicDescriptorMatches op).1]
  exact emittedLoadBytes op.width {m with hasMemBswap := !parallel && m.hasMemBswap}

theorem atomicWriteBytes (op : AtomicOperand) (m : CellInput) (parallel : Bool) (replacement : Word) :
    emittedStoreValue op.width (tcgLayout (atomicDescriptor op) m.hostBig m.guestBig)
      {m with hasMemBswap := !parallel && m.hasMemBswap} replacement =
      pack op.width.bytes m.guestBig replacement := by
  rw [(atomicDescriptorMatches op).2]
  exact emittedStoreBytes op.width false {m with hasMemBswap := !parallel && m.hasMemBswap} replacement

theorem atomicWidth (op : AtomicOperand) (m : CellInput) :
    (tcgLayout (atomicDescriptor op) m.hostBig m.guestBig).1 = op.width.bytes := by
  rw [(atomicDescriptorMatches op).1, loadLayout]

theorem atomicMatches (i : AtomicInstruction) (s : Registers) (m : CellInput)
    (parallel : Bool) : tcgAtomic i s m parallel = atomicSpec i s m := by
  simp only [tcgAtomic, atomicSpec, legalTarget, sourceRegister, atomicReadBytes,
    atomicWriteBytes, atomicWidth]
  cases h : i.op <;> cases parallel <;>
    simp [h, atomicReturnsOld, sourceAtomicValue, sourceRegister, serialExchangeReturnsOld,
      serialAddReturnsOld, reverseAdd]

theorem atomicAbort (i : AtomicInstruction) (s : Registers) (m : CellInput) (parallel : Bool)
    (failed : (tcgAtomic i s m parallel).exit ≠ .success) :
    (tcgAtomic i s m parallel).regs = s ∧ (tcgAtomic i s m parallel).effect = none := by
  rw [atomicMatches] at *
  unfold atomicSpec
  unfold atomicSpec at failed
  repeat first | split <;> simp_all [atomicStop] | rfl

-- The reverse-endian FETCHADD helper retries from the actually
-- observed value after a failed compare. This finite projection
-- records only its successful attempt and does not prove progress.
def casRetry (update : Word → Word) (expected : Word) : List Word → Option (Word × Word)
  | [] => none
  | observed :: tail =>
    if observed = expected then some (observed, update observed)
    else casRetry update observed tail

theorem casCommit (update : Word → Word) (expected : Word) (observations : List Word)
    (old next : Word) (commit : casRetry update expected observations = some (old, next)) :
    old ∈ observations ∧ next = update old := by
  induction observations generalizing expected with
  | nil => simp [casRetry] at commit
  | cons first tail ih =>
    by_cases hit : first = expected
    · simp [casRetry, hit] at commit
      rcases commit with ⟨same, value⟩
      exact ⟨by simp [hit, same], by simpa [same] using value.symm⟩
    · have result := ih first (by simpa [casRetry, hit] using commit)
      exact ⟨by simp [result.1], result.2⟩

example : casRetry (fun n => n + 1) 0 [7, 7] = some (7, 8) := by decide
example : casRetry (fun n => n + 1) 0 [7] = none := by decide

def reverseFetchAddAttempts (width : FetchWidth) (increment : Increment)
    (initialRaw : Word) (observations : List Word) : Option (Word × Word) :=
  let w := width.width
  casRetry (fun old => bswapPayload w (reverseAdd (bswapPayload w old) increment.value))
    initialRaw observations

theorem reverseFetchAddCommits (width : FetchWidth) (increment : Increment)
    (initialRaw : Word) (observations : List Word) (old next : Word)
    (commits : reverseFetchAddAttempts width increment initialRaw observations = some (old, next)) :
    old ∈ observations ∧
    next = bswapPayload width.width (bswapPayload width.width old + increment.value) := by
  unfold reverseFetchAddAttempts reverseAdd at commits
  exact casCommit _ _ _ _ _ commits

def atomicLabels (cpu : Cpu) (access : AtomicEffect) :
    TraceOrder.Access Cpu Location × TraceOrder.Access Cpu Location :=
  let first := event cpu (.read access.address access.before false)
  let second := event cpu (.write access.address access.after false)
  ({first with kind := .rmwRead, order := access.ordering},
   {second with kind := .rmwWrite, order := access.ordering})

theorem atomicLabelsPaired (cpu : Cpu) (effect : AtomicEffect)
    (sameWidth : effect.before.length = effect.after.length) :
    let labels := atomicLabels cpu effect
    labels.1.location = labels.2.location ∧ labels.1.cpu = labels.2.cpu ∧
      labels.1.kind = .rmwRead ∧ labels.2.kind = .rmwWrite :=
  ⟨by simp [atomicLabels, event, sameWidth], rfl, rfl, rfl⟩

theorem atomicEffectPreservesWidth (i : AtomicInstruction) (s : Registers) (m : CellInput)
    (parallel : Bool) (effect : AtomicEffect)
    (success : (tcgAtomic i s m parallel).effect = some effect) :
    effect.before.length = effect.after.length := by
  rw [atomicMatches] at success
  unfold atomicSpec at success
  repeat (first | split at success <;> try contradiction | contradiction)
  all_goals dsimp at success
  all_goals
    have equality := Option.some.inj success
    rw [← equality]
    simp [pack]

def atomicPairMask (a b : TraceOrder.Kind) : BitVec 8 :=
  if a.write then (if b.write then storeStoreMask else storeLoadMask)
  else (if b.write then loadStoreMask else loadLoadMask)

private def exchangeExample : AtomicInstruction := ⟨0, 5, 4, .exchange .w2 4⟩
private def fetchExample : AtomicInstruction := ⟨0, 5, 4, .fetchAdd .w4 .minus1 true⟩

example :
    let result := tcgAtomic exchangeExample exampleRegs exampleCell true
    result.exit = .success ∧ result.regs.gr 5 = 0x1234 ∧
      result.effect.map AtomicEffect.after = some [0, 0x10] := by decide

example :
    let zeroCell := {exampleCell with bytes := fun _ => 0}
    let result := tcgAtomic fetchExample exampleRegs zeroCell false
    result.exit = .success ∧ result.regs.gr 5 = 0 ∧
      result.effect.map AtomicEffect.after = some [0xff, 0xff, 0xff, 0xff] := by decide

example :
    reverseFetchAddAttempts .w4 .plus1 0 [0x01000000, 0x01000000] =
      some (0x01000000, 0x02000000) := by decide

example :
    (tcgAtomic fetchExample exampleRegs {exampleCell with preflight := false} true).effect = none :=
  rfl

end TcgMemory
