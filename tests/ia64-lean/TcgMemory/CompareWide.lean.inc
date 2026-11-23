/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.CompareWide

-- The pair at this boundary contains decoded values ordered by
-- increasing physical address, or Int128 low/high words as indicated.
abbrev Pair := Word × Word

def orderWords (big : Bool) (words : Pair) : Pair :=
  if big then (words.2, words.1) else words

theorem orderWordsInverse (big : Bool) (words : Pair) :
    orderWords big (orderWords big words) = words := by
  cases big <;> rfl

def selectedHalf (upper : Bool) (words : Pair) : Word :=
  if upper then words.2 else words.1

theorem compareHalf (big upper : Bool) (words : Pair) :
    sourceHalf big upper (orderWords big words) = selectedHalf upper words := by
  cases big <;> cases upper <;> rfl

theorem desiredWords (big : Bool) (value csd : Word) :
    orderWords big (sourceDesired big value csd) = (value, csd) := by
  cases big <;> rfl

def sourceBlock (address : Word) : Word := address &&& ~~~(8 : Word)
def sourceUpper (address : Word) : Bool := (address &&& 8) != 0
def alignedEight (address : Word) : Bool := (address &&& 7) == 0

theorem containingBlock (address : Word) (aligned : alignedEight address = true) :
    sourceBlock address = address - address % 16 ∧
      sourceUpper address = (address % 16 == 8) := by
  simp only [sourceBlock, sourceUpper, alignedEight, beq_iff_eq] at *
  constructor
  · bv_decide
  · rw [Bool.eq_iff_iff]
    simp only [bne_iff_ne, beq_iff_eq]
    bv_decide

-- Each successful CAS is one primitive over the entire observed pair.
-- A failed CAS supplies its observed pair for the next compare. No
-- number of failed attempts implies a later successful attempt.
def retry (select : Pair → Word) (ccv : Word) (expected : Pair) :
    List Pair → Option (Pair × Bool)
  | [] => if select expected = ccv then none else some (expected, false)
  | observed :: remaining =>
    if select expected ≠ ccv then some (expected, false)
    else if observed = expected then some (expected, true)
    else retry select ccv observed remaining

theorem retryTerminal (select : Pair → Word) (ccv : Word) (initial : Pair)
    (observations : List Pair) (snapshot : Pair) (commits : Bool)
    (returns : retry select ccv initial observations = some (snapshot, commits)) :
    snapshot ∈ initial :: observations ∧
      commits = (select snapshot == ccv) ∧
      (commits = true → snapshot ∈ observations) := by
  induction observations generalizing initial with
  | nil =>
    unfold retry at returns
    split at returns
    · cases returns
    · obtain ⟨rfl, rfl⟩ := Prod.mk.inj (Option.some.inj returns)
      simp_all
  | cons first remaining ih =>
    unfold retry at returns
    split at returns
    · obtain ⟨rfl, rfl⟩ := Prod.mk.inj (Option.some.inj returns)
      simp_all
    · split at returns
      · obtain ⟨rfl, rfl⟩ := Prod.mk.inj (Option.some.inj returns)
        simp_all
      · obtain ⟨seen, correct, committed⟩ := ih first returns
        exact ⟨List.mem_cons_of_mem _ seen, correct, fun hit => List.mem_cons_of_mem _ (committed hit)⟩

theorem retryFirstCommit (select : Pair → Word) (ccv : Word) (initial : Pair)
    (observations : List Pair) (snapshot : Pair)
    (returns : retry select ccv initial observations = some (snapshot, true)) :
    ∃ index, observations[index]? = some snapshot ∧
      retry select ccv initial (observations.take (index + 1)) = some (snapshot, true) ∧
      ∀ n, n ≤ index → retry select ccv initial (observations.take n) = none := by
  induction observations generalizing initial with
  | nil =>
    simp only [retry] at returns
    split at returns <;> cases returns
  | cons first later ih =>
    by_cases equal : select initial = ccv
    · by_cases succeeds : first = initial
      · subst first
        refine ⟨0, ?_, ?_, ?_⟩
        · have same : initial = snapshot := by simpa [retry, equal] using returns
          simp [same]
        · simpa [retry, equal] using returns
        · intro n before
          have count : n = 0 := by omega
          simp [count, retry, equal]
      · have recurs : retry select ccv first later = some (snapshot, true) := by
          simpa [retry, equal, succeeds] using returns
        obtain ⟨index, atIndex, firstSuccess, incomplete⟩ := ih first recurs
        refine ⟨index + 1, by simpa using atIndex, ?_, ?_⟩
        · simpa [List.take_succ_cons, retry, equal, succeeds] using firstSuccess
        · intro n bound
          cases n with
          | zero => simp [retry, equal]
          | succ n =>
            simpa [List.take_succ_cons, retry, equal, succeeds] using incomplete n (by omega)
    · simp [retry, equal] at returns

def operand (bytes : Nat → Byte) (big : Bool) : Pair :=
  (unpack 8 big bytes, unpack 8 big (fun i => bytes (i + 8)))

def storedBytes (big : Bool) (words : Pair) : List Byte :=
  pack 8 big words.1 ++ pack 8 big words.2

theorem storeCsdUpper (big : Bool) (source csd : Word) :
    storedBytes big (orderWords big (sourceDesired big source csd)) =
      pack 8 big source ++ pack 8 big csd := by
  rw [desiredWords]
  rfl

structure Instruction where
  qp : Pred
  target : Reg
  base : Reg
  source : Reg
  release : Bool

structure Inputs where
  cell : CellInput
  ccv : Word
  csd : Word
  supported : Bool
  parallel : Bool
  supportsAtomic128 : Bool
  debugPassed : Bool
  observations : List Pair

inductive Fault where
  | nullified | opcodeUnavailable | illegalRegister | addressNaT | dataNaT | debug | misaligned
  | preflight | primitiveAbort | restartExclusive
  deriving DecidableEq

structure Effect where
  address : Word
  block : Word
  old : Word
  store : Option (List Byte)
  order : TraceOrder.Order

inductive Result where
  | stopped (regs : Registers) (fault : Fault)
  | pending (regs : Registers)
  | retired (regs : Registers) (effect : Effect)

-- DBR for the 16-byte block precedes the eight-byte alignment
-- check. The helper checks the write probe before selecting an
-- exclusive retry. Lookup details and exception delivery are external.
def beforePrimitive (i : Instruction) (s : Registers) (m : Inputs) : Option Fault :=
  if !predicate s i.qp then some .nullified
  else if !m.supported then some .opcodeUnavailable
  else if !tcgMayWrite s i.target then some .illegalRegister
  else if getNat s i.base then some .addressNaT
  else if getNat s i.source then some .dataNaT
  else if !m.debugPassed then some .debug
  else if !alignedEight (sourceGR s i.base) then some .misaligned
  else if !m.cell.preflight then some .preflight
  else if m.parallel && !m.supportsAtomic128 then some .restartExclusive
  else if !m.cell.primitiveReturns then some .primitiveAbort
  else none

theorem beforePrimitiveAligned (i : Instruction) (s : Registers) (m : Inputs)
    (enabled : beforePrimitive i s m = none) : alignedEight (getGR s i.base) = true := by
  unfold beforePrimitive at enabled
  rw [sourceRegister] at enabled
  cases align : alignedEight (getGR s i.base)
  · simp only [align, Bool.not_false, if_true] at enabled
    repeat first | contradiction | split at enabled
  · rfl

-- Observations give address-ordered decoded words at each CAS.
-- The serial branch projects the final WB bytes of two separate stores.
def afterPrimitive (i : Instruction) (s : Registers) (m : Inputs)
    (address value : Word) : Result :=
  let big := m.cell.guestBig
  let upper := sourceUpper address
  let before := operand m.cell.bytes big
  let select := sourceHalf big upper
  let outcome := if m.parallel then
    retry select m.ccv (orderWords big before) (m.observations.map (orderWords big))
    else some (orderWords big before, select (orderWords big before) == m.ccv)
  match outcome with
  | none => .pending s
  | some (snapshot, commits) =>
    let old := select snapshot
    let store := if commits then
      some (storedBytes big (orderWords big (sourceDesired big value m.csd))) else none
    .retired (setNaT (setGR s i.target old) i.target false)
      ⟨address, sourceBlock address, old, store, if i.release then .release else .acquire⟩

def attempt (i : Instruction) (s : Registers) (m : Inputs) : Result :=
  match beforePrimitive i s m with
  | some fault => .stopped s fault
  | none => afterPrimitive i s m (sourceGR s i.base) (sourceGR s i.source)

-- Independent one-step SDM effect at a linearization point.
-- On the serial branch the initial read is that point only under
-- exclusive execution of both stores.
def architectureAt (i : Instruction) (s : Registers) (m : Inputs) (snapshot : Pair) : Result :=
  let address := getGR s i.base
  let big := m.cell.guestBig
  let old := selectedHalf (address % 16 == 8) snapshot
  let store := if old = m.ccv then
    some (pack 8 big (getGR s i.source) ++ pack 8 big m.csd) else none
  .retired (setNaT (setGR s i.target old) i.target false)
    ⟨address, address - address % 16, old, store, if i.release then .release else .acquire⟩

def firstPair (m : Inputs) : Pair := operand m.cell.bytes m.cell.guestBig

theorem retiresAtSnapshot (i : Instruction) (s after : Registers) (m : Inputs) (effect : Effect)
    (completed : attempt i s m = .retired after effect) :
    ∃ snapshot, snapshot ∈ firstPair m :: m.observations ∧
      architectureAt i s m snapshot = .retired after effect ∧
      (m.parallel = true → effect.store.isSome = true → snapshot ∈ m.observations) := by
  unfold attempt at completed
  cases enabled : beforePrimitive i s m
  case some fault => simp [enabled] at completed
  simp only [enabled, sourceRegister, afterPrimitive] at completed
  have aligned := beforePrimitiveAligned i s m enabled
  let address := getGR s i.base
  let big := m.cell.guestBig
  let select := sourceHalf big (sourceUpper address)
  let initial := orderWords big (firstPair m)
  have lower := containingBlock address aligned
  have chosen (snapshot : Pair) :
      select snapshot = selectedHalf (address % 16 == 8) (orderWords big snapshot) := by
    rw [← lower.2]
    simpa [select, orderWordsInverse] using compareHalf big (sourceUpper address)
      (orderWords big snapshot)
  have finish (snapshot : Pair) (commits : Bool) :
      commits = (select snapshot == m.ccv) →
      Result.retired (setNaT (setGR s i.target (select snapshot)) i.target false)
        ⟨address, sourceBlock address, select snapshot,
          if commits then
            some (storedBytes big (orderWords big (sourceDesired big (getGR s i.source) m.csd)))
          else none, if i.release then .release else .acquire⟩ =
        architectureAt i s m (orderWords big snapshot) := by
    intro correct
    simp only [architectureAt, lower.1, storeCsdUpper, correct, beq_iff_eq]
    rw [chosen snapshot]
  cases mode : m.parallel
  · simp only [mode, Bool.false_eq_true, if_false] at completed
    refine ⟨firstPair m, List.mem_cons_self, ?_, by simp [mode]⟩
    simpa [initial, orderWordsInverse] using
      (finish initial (select initial == m.ccv) rfl).symm.trans completed
  · simp only [mode, if_true] at completed
    change (match retry select m.ccv initial (m.observations.map (orderWords big)) with
      | none => _
      | some (pair, commits) => _) = _ at completed
    split at completed
    · cases completed
    · rename_i pair commits monitor
      obtain ⟨seen, correct, committed⟩ := retryTerminal _ _ _ _ _ _ monitor
      have seenPhysical : orderWords big pair ∈ firstPair m :: m.observations := by
        have visible := List.mem_map_of_mem (f := orderWords big) seen
        simpa [initial, List.map_map, Function.comp_def, orderWordsInverse] using visible
      refine ⟨orderWords big pair, seenPhysical, (finish pair commits correct).symm.trans completed, ?_⟩
      intro _ written
      have yes : commits = true := by
        cases completed
        simpa using written
      have visible := List.mem_map_of_mem (f := orderWords big) (committed yes)
      simpa [List.map_map, Function.comp_def, orderWordsInverse] using visible

-- The selected eight bytes are decoded in guest byte order even
-- when the underlying primitive loads both halves of the block.
theorem returnedHalfBytes (big upper : Bool) (bytes : Nat → Byte) :
    sourceHalf big upper (orderWords big (operand bytes big)) =
      unpack 8 big (fun offset => bytes (if upper then offset + 8 else offset)) := by
  rw [compareHalf]
  cases upper <;> rfl

-- A primitive that observes the same full block at load and CAS
-- retires for every byte image and register operand. No history or
-- memory timing argument is needed for this single-primitive case.
theorem atStablePrimitive (i : Instruction) (s : Registers) (m : Inputs)
    (enabled : beforePrimitive i s m = none) :
    attempt i s {m with observations := [firstPair m]} = architectureAt i s m (firstPair m) := by
  let supplied := {m with observations := [firstPair m]}
  let address := sourceGR s i.base
  let big := m.cell.guestBig
  let value := sourceGR s i.source
  let initial := orderWords big (firstPair m)
  let select := sourceHalf big (sourceUpper address)
  have canStart : beforePrimitive i s supplied = none := enabled
  have returns : ∃ after effect, attempt i s supplied = .retired after effect := by
    rw [attempt, canStart]
    change ∃ after effect, afterPrimitive i s supplied address value = .retired after effect
    have decision (parallel : Bool) :
        (if parallel then retry select m.ccv initial [initial]
          else some (initial, select initial == m.ccv)) =
          some (initial, select initial == m.ccv) := by
      cases parallel
      · rfl
      · by_cases matchCCV : select initial = m.ccv <;> simp [retry, matchCCV]
    unfold afterPrimitive
    change ∃ (after : Registers) (effect : Effect),
      (match (if m.parallel then retry select m.ccv initial [initial]
        else some (initial, select initial == m.ccv)) with
        | none => _
        | some (pair, commits) => _) = Result.retired after effect
    rw [decision]
    exact ⟨_, _, rfl⟩
  obtain ⟨after, effect, complete⟩ := returns
  obtain ⟨snapshot, wasSeen, same, _⟩ := retiresAtSnapshot i s after supplied effect complete
  have this : snapshot = firstPair m := by simpa [supplied, firstPair] using wasSeen
  subst snapshot
  exact complete.trans same.symm

private def exampleInstruction : Instruction := ⟨0, 4, 4, 4, false⟩

private def exampleWideCell : CellInput :=
  { exampleCell with
    guestBig := true
    bytes := fun n => if n = 7 then 0xa1 else if n = 15 then 0xb2 else 0 }

private def wideRegs (base : Word) : Registers :=
  {exampleRegs with gr := fun r => if r = 4 then base else exampleRegs.gr r}

private def exampleInputs : Inputs :=
  ⟨exampleWideCell, 0xb2, 0x1122334455667788, true, true, true, true,
    [(0xc3, 0xb2), (0xc3, 0xb2)]⟩

-- Another store changed the lower half between the load and the
-- first CAS. The upper compare still matches at the successful CAS.
example : attempt exampleInstruction (wideRegs 24) exampleInputs =
    .retired (setNaT (setGR (wideRegs 24) 4 0xb2) 4 false)
      ⟨24, 16, 0xb2, some (pack 8 true 24 ++ pack 8 true 0x1122334455667788), .acquire⟩ := rfl

example : attempt exampleInstruction (wideRegs 24)
    {exampleInputs with observations := [(0xc3, 0xb2)]} = .pending (wideRegs 24) := rfl

example : attempt exampleInstruction (wideRegs 16) exampleInputs =
    .retired (setNaT (setGR (wideRegs 16) 4 0xa1) 4 false)
      ⟨16, 16, 0xa1, none, .acquire⟩ := rfl

example : attempt exampleInstruction (wideRegs 24)
    {exampleInputs with parallel := false} =
      .retired (setNaT (setGR (wideRegs 24) 4 0xb2) 4 false)
        ⟨24, 16, 0xb2, some (pack 8 true 24 ++ pack 8 true 0x1122334455667788), .acquire⟩ := rfl

example : attempt exampleInstruction (wideRegs 24)
    {exampleInputs with supportsAtomic128 := false} = .stopped (wideRegs 24) .restartExclusive := rfl

example : attempt exampleInstruction (wideRegs 25) exampleInputs =
    .stopped (wideRegs 25) .misaligned := rfl

example : attempt exampleInstruction (wideRegs 25) {exampleInputs with debugPassed := false} =
    .stopped (wideRegs 25) .debug := rfl

example : attempt exampleInstruction (wideRegs 24)
    { exampleInputs with
      supportsAtomic128 := false
      cell := {exampleWideCell with primitiveReturns := false} } =
      .stopped (wideRegs 24) .restartExclusive := rfl

end TcgMemory.CompareWide
