/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64OptimizedSlot

open IA64Optimizer
local notation "Word" => BitVec 64

-- Allocate distinct constant and scratch identifiers without imposing any GR
-- disjointness on the decoded source and destination operands.
abbrev SlotTemp := Fin (136 + 2 ^ 64)

def registerTemp (r : IA64Tcg.Reg) : SlotTemp :=
  ⟨r.val, by have := r.isLt; omega⟩

def scratchTemp (n : Fin 8) : SlotTemp :=
  ⟨128 + n.val, by have := n.isLt; omega⟩

def literalTemp (v : Word) : SlotTemp :=
  ⟨136 + v.toNat, Nat.add_lt_add_left v.isLt _⟩

def slotPool : ConstantPool SlotTemp where
  readonly t := if 136 ≤ t.val then some (BitVec.ofNat 64 (t.val - 136)) else none
  address v := some (literalTemp v)
  entry := by
    intro v t lookup
    have same := Option.some.inj lookup
    subst t
    simp [literalTemp, Nat.add_sub_cancel_left, BitVec.ofNat_toNat]

def slotBefore (s : IA64Tcg.State α) (t : SlotTemp) : Word :=
  if gr : t.val < 128 then s.gr ⟨t.val, gr⟩ else (slotPool.readonly t).getD 0

def operandTemp (r : IA64Tcg.Reg) : SlotTemp :=
  if r = 0 then literalTemp 0 else registerTemp r

def allocatingRegs (i : IA64Tcg.Slot) (destination : SlotTemp) : PayloadRegs SlotTemp :=
  { dst := destination
    x := operandTemp i.r2
    y := operandTemp i.r3
    tmp := scratchTemp 0
    tmp2 := scratchTemp 1
    low := scratchTemp 2
    high := scratchTemp 3
    immediate := literalTemp i.imm
    one := literalTemp 1
    minusOne := literalTemp (~~~0#64)
    small := literalTemp }

def allocatingTarget : PayloadTarget := ⟨false, false, false⟩

theorem registerTempRead (s : IA64Tcg.State α) (r : IA64Tcg.Reg) :
    slotBefore s (registerTemp r) = s.gr r := by
  simp [slotBefore, registerTemp, r.isLt]

theorem literalTempRead (s : IA64Tcg.State α) (v : Word) :
    slotBefore s (literalTemp v) = v := by
  have low : ¬136 + v.toNat < 128 := by omega
  simp [slotBefore, slotPool, literalTemp, low, Nat.add_sub_cancel_left, BitVec.ofNat_toNat]

theorem operandTempRead (s : IA64Tcg.State α) (r : IA64Tcg.Reg) :
    slotBefore s (operandTemp r) = IA64Tcg.SDM.value s r := by
  by_cases zero : r = 0
  · simp [operandTemp, IA64Tcg.SDM.value, zero, literalTempRead]
  · simp [operandTemp, IA64Tcg.SDM.value, zero, registerTempRead]

theorem literalSound (s : IA64Tcg.State α) : slotPool.sound (slotBefore s) := by
  intro t v lookup
  have low : ¬t.val < 128 := by
    intro h
    have bound : ¬136 ≤ t.val := by omega
    simp [slotPool, bound] at lookup
  simp [slotBefore, low, lookup]

theorem registerNotScratch (r : IA64Tcg.Reg) (idx : Fin 8) :
    registerTemp r ≠ scratchTemp idx := by
  intro h
  have eq := congrArg Fin.val h
  have fit := r.isLt
  change r.val = 128 + idx.val at eq
  omega

def addSource (i : IA64Tcg.Slot) (dst : SlotTemp) : ScalarInput SlotTemp :=
  .fold ⟨.add, dst, operandTemp i.r2, operandTemp i.r3, plainOptions⟩

def addCompiled (i : IA64Tcg.Slot) (dst : SlotTemp) :
    Option (Metadata SlotTemp × Operation SlotTemp) :=
  translate slotPool (initialized slotPool.readonly) (136 + 2 ^ 64) (addSource i dst)

def closedAddRun (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (dst : SlotTemp) : ScalarRun SlotTemp :=
  let result := (addCompiled i dst).getD (initialized slotPool.readonly, .nop)
  ⟨slotBefore s, allocatingRegs i dst, allocatingTarget, fun _ => plainOptions, slotPool, result.1, [result.2]⟩

theorem closedAddPlan (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (dst : SlotTemp)
    (writable : slotPool.readonly dst = none) :
    let r := closedAddRun s i dst
    Translation r.pool (initialized r.pool.readonly) [addSource i dst] r.ops r.final := by
  obtain ⟨final, op, compiled⟩ := translationStepExists slotPool
    (initialized slotPool.readonly) (initializedSound _ _ (literalSound s)).1
    (fun word => ⟨literalTemp word, rfl⟩) (addSource i dst)
  change addCompiled i dst = some (final, op) at compiled
  simpa only [closedAddRun, compiled, Option.getD_some] using
    (Translation.next (pool := slotPool) (input := addSource i dst)
      (show AllowedInput slotPool (initialized slotPool.readonly) (addSource i dst) from
        ⟨writable, by simp [addSource, plainOptions], by simp [addSource, plainOptions]⟩)
      compiled (.end_ final))

theorem closedOrdinaryAdd (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    OrdinaryRun (closedAddRun s i (registerTemp i.dst)) s i .ADD registerTemp := by
  have bound : ¬136 ≤ i.dst.val := by have := i.dst.isLt; omega
  have writable : slotPool.readonly (registerTemp i.dst) = none := by
    simp [slotPool, registerTemp, bound]
  refine ⟨⟨operandTempRead s i.r2, operandTempRead s i.r3⟩, registerTempRead s, rfl, ?_, ?_,
    literalSound s, ?_, closedAddPlan s i _ writable⟩
  · intro a b same
    exact Fin.ext (congrArg (fun t : SlotTemp => t.val) same)
  · intro reg
    simp [closedAddRun, allocatingRegs, registerNotScratch]
  · constructor <;> simp [valueTag, needsImmediate, writesImmediateScratch, usesPointer]

theorem closedBranchlessAdd (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    BranchlessRun (closedAddRun s i (scratchTemp 4)) s i .ADD none := by
  refine ⟨⟨operandTempRead s i.r2, operandTempRead s i.r3⟩, ?_, ?_, literalSound s,
    ⟨[addSource i (scratchTemp 4)], rfl, closedAddPlan s i _ rfl⟩⟩
  · simp [valueTag, needsImmediate]
  · simp [permitsFastExtract]

theorem ordinaryAddAllocated (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    ∃ r : ScalarRun SlotTemp, OrdinaryRun r s i .ADD registerTemp ∧
      (r.inputs.dst = r.inputs.x ↔ i.dst = i.r2 ∧ i.r2 ≠ 0) ∧
      (r.inputs.dst = r.inputs.y ↔ i.dst = i.r3 ∧ i.r3 ≠ 0) := by
  refine ⟨closedAddRun s i (registerTemp i.dst), closedOrdinaryAdd s i, ?_, ?_⟩
  all_goals
    change registerTemp i.dst = operandTemp _ ↔ _
    simp only [operandTemp]
    split <;> rename_i z
    · have diff : registerTemp i.dst ≠ literalTemp 0 := by
        intro h
        have eq := congrArg Fin.val h
        have fit := i.dst.isLt
        change i.dst.val = 136 at eq
        omega
      simpa [z] using diff
    · simp [z, registerTemp, Fin.mk.injEq, Fin.ext_iff]

theorem branchlessAddAllocated (s : IA64Tcg.State α) (i : IA64Tcg.Slot) :
    ∃ r : ScalarRun SlotTemp, BranchlessRun r s i .ADD none :=
  ⟨_, closedBranchlessAdd s i⟩

def closedAddBundle (low high ip : Word) (index : BitVec 2) (s : IA64Add.Joined α)
    (facts : IA64Tcg.Facts) (context : IA64Add.Context) : IA64Add.Boundary α :=
  let i := IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)
  optimizedBundle (closedAddRun s.data i (registerTemp i.dst)) (closedAddRun s.data i (scratchTemp 4))
    .ADD low high ip index s facts context registerTemp

theorem closedAddBundleRefines (low high ip : Word) (index : BitVec 2)
    (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context)
    (present : IA64Alu.selected .ADD low high ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts)
    (entry : IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
      {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index}) :
    ∃ inputs, IA64Add.OperandChoice s.data
      (IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
      closedAddBundle low high ip index s facts context =
        IA64Alu.sdmBundleStep .ADD low high ip index s inputs context := by
  exact optimizedBundleRefines _ _ .ADD low high ip index s facts context registerTemp none
    present sound entry (fun _ _ => closedOrdinaryAdd _ _) (fun _ _ => closedBranchlessAdd _ _)

theorem allocatedAddBundleRefines (low high ip : Word) (index : BitVec 2)
    (s : IA64Add.Joined α) (facts : IA64Tcg.Facts) (context : IA64Add.Context)
    (present : IA64Alu.selected .ADD low high ip index)
    (sound : IA64Tcg.Sound s.data (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)) facts)
    (entry : IA64Add.EntryFacts s (IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index))
      {context with stopAfter := FullDecodeImplementation.cStop (FullDecodeSpec.templateBits low) index}) :
    ∃ (ordinary branchless : ScalarRun SlotTemp), ∃ inputs,
      IA64Add.OperandChoice s.data
        (IA64Alu.sdmRecord .ADD (FullDecodeSpec.slotBits low high index) ip index) inputs ∧
      optimizedBundle ordinary branchless .ADD low high ip index s facts context registerTemp =
        IA64Alu.sdmBundleStep .ADD low high ip index s inputs context := by
  let i := IA64Alu.emittedRecord (IA64Alu.decode .ADD low high ip index)
  obtain ⟨ordinary, sr, _, _⟩ := ordinaryAddAllocated s.data i
  obtain ⟨branchless, br⟩ := branchlessAddAllocated s.data i
  obtain ⟨inputs, operands, eq⟩ := optimizedBundleRefines ordinary branchless .ADD low high ip index
    s facts context registerTemp none present sound entry (fun _ _ => sr) (fun _ _ => br)
  exact ⟨ordinary, branchless, inputs, operands, eq⟩

end IA64OptimizedSlot
