/- SPDX-License-Identifier: GPL-2.0-or-later -/

-- The translator checks the template, iterates the available slots, then
-- applies fixup in the MLX pair. Ordinary fixup is proved to be identity.
-- This query projects the candidate at a physical slot of the bundle.
def cBundle
    (single : UnitCode → Slot → Word → BitVec 8 → Observation)
    (paired : BitVec 5 → Slot → Slot → Slot → Word → Observation)
    (low high address : Word) (index : BitVec 2) : Observation :=
  let code := (cTemplate low).setWidth 5
  let bits (n : BitVec 2) := (cSlot low high n).setWidth 41
  let l := bits 1
  let x := bits 2
  if cDefined code = true ∧ index < 3 then
    if (code = 4 ∨ code = 5) ∧ index = 1 then paired code l (bits 0) x address
    else if (code = 4 ∨ code = 5) ∧ index = 2 ∧ cPairSkip code l (bits 0) x address = true then 0
    else single (cUnit code index) (bits index) address (index.zeroExtend 8)
  else 0

theorem noOrdinaryInPair (form : Form) (code : BitVec 5) (index : BitVec 2) (raw : Slot)
    (long : code = 4 ∨ code = 5) (inside : index = 1 ∨ index = 2) :
    ¬ formUnit mUnit iUnit bUnit fUnit form
      (templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit code index) raw := by
  cases form
  case iMisc format =>
    cases format <;>
      dsimp only [formUnit, templateUnit, mUnit, iUnit, bUnit, fUnit, lUnit, xUnit, reservedUnit] <;>
      bv_decide
  all_goals
    dsimp only [formUnit, aUnit, templateUnit, mUnit, iUnit, bUnit, fUnit, lUnit, xUnit, reservedUnit]
    bv_decide

theorem cBundle_matches (form : Form)
    (single : UnitCode → Slot → Word → BitVec 8 → Observation)
    (paired : BitVec 5 → Slot → Slot → Slot → Word → Observation)
    (single_matches : ∀ unit raw address slot,
      single unit raw address slot = aOpcode mUnit iUnit bUnit fUnit form unit raw address slot)
    (paired_absent : ∀ code l m x address, (code = 4 ∨ code = 5) → paired code l m x address = 0)
    (low high address : Word) (index : BitVec 2) :
    cBundle single paired low high address index =
      aBundle reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit form low high address index := by
  have template_eq : (cTemplate low).setWidth 5 = templateBits low := by
    rw [bundleTemplate_matches]
    simp
  have slot_eq (n : BitVec 2) : (cSlot low high n).setWidth 41 = slotBits low high n := by
    rw [bundleSlot_matches]
    simp
  have absent (isMLX : templateBits low = 4 ∨ templateBits low = 5)
      (inside : index = 1 ∨ index = 2) :
      aOpcode mUnit iUnit bUnit fUnit form
        (templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (templateBits low) index)
        (slotBits low high index) address (index.zeroExtend 8) = 0 := by
    unfold aOpcode
    split <;> rename_i select
    · exact False.elim (noOrdinaryInPair form (templateBits low) index _ isMLX inside select.1)
    · rfl
  dsimp only [cBundle, aBundle]
  rw [template_eq, definedTemplate_matches]
  simp only [slot_eq, slotUnit_matches]
  split <;> rename_i admissible
  · split <;> rename_i pairedSlot
    · rw [paired_absent _ _ _ _ _ pairedSlot.1, absent pairedSlot.1 (.inl pairedSlot.2)]
    · split <;> rename_i skipped
      · rw [absent skipped.1 (.inr skipped.2.1)]
      · apply single_matches
  · rfl
