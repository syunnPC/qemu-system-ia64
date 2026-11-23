/- SPDX-License-Identifier: GPL-2.0-or-later -/

def bundleNOP := cBundle cNOP cMlxNOP

theorem nopShort_matches (unit : UnitCode) (raw : Slot) (address : Word) (slot : BitVec 8)
    (admitted : ignoredBranch bUnit unit raw || shortNop mUnit iUnit bUnit fUnit unit raw) :
    cNOP unit raw address slot = nopOrdinarySpec mUnit iUnit bUnit fUnit unit raw address slot := by
  dsimp only [cNOP, pNOP, oNOP, nopOrdinarySpec, ignoredBranch, shortNop, nopRecord, nopImmShort,
             reg, mUnit, iUnit, bUnit, fUnit] at *
  bv_decide

theorem nopPaired_matches (code : BitVec 5) (l m x : Slot) (address : Word)
    (paired : code = 4 ∨ code = 5) (admitted : longNop x) :
    cMlxNOP code l m x address = nopRecord xUnit x address 1 0 (nopImmLong l x) false := by
  dsimp only [cMlxNOP, longNop, nopRecord, nopImmLong, nopImmShort, reg, xUnit] at *
  bv_decide

theorem nopPaired_skips (code : BitVec 5) (l m x : Slot) (address : Word)
    (paired : code = 4 ∨ code = 5) (admitted : longNop x) :
    cPairSkip code l m x address = true := by
  dsimp only [cPairSkip, longNop] at *
  bv_decide

private theorem nopOrdinaryPresent (unit : UnitCode) (raw : Slot) (address : Word) (slot : BitVec 8)
    (admitted : (nopOrdinarySpec mUnit iUnit bUnit fUnit unit raw address slot).getLsbD 394 = true) :
    ignoredBranch bUnit unit raw || shortNop mUnit iUnit bUnit fUnit unit raw := by
  unfold nopOrdinarySpec at admitted
  by_cases ignored : ignoredBranch bUnit unit raw = true
  · simp [ignored]
  · cases explicit : shortNop mUnit iUnit bUnit fUnit unit raw
    · simp only [if_neg ignored, explicit, Bool.false_eq_true, ite_false] at admitted
      contradiction
    · simp [explicit]

/-- Every independently assigned short or long NOP and every quiescent
white B-unit encoding decodes to its expected record in a legal slot. -/
theorem NOP_allowed (low high address : Word) (index : BitVec 2)
    (admitted : nopBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit low high index) :
    bundleNOP low high address index =
      nopBundleSpec reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit low high address index := by
  have template_eq : (cTemplate low).setWidth 5 = templateBits low := by
    rw [bundleTemplate_matches]
    simp
  have slot_eq (n : BitVec 2) : (cSlot low high n).setWidth 41 = slotBits low high n := by
    rw [bundleSlot_matches]
    simp
  dsimp only [nopBundleEncoding, nopBundleSpec] at admitted
  dsimp only [bundleNOP, cBundle, nopBundleSpec]
  rw [template_eq, definedTemplate_matches]
  simp only [slot_eq, slotUnit_matches]
  by_cases legal : validTemplate (templateBits low) = true ∧ index < 3
  · simp only [legal, true_and, and_true, if_true] at admitted ⊢
    by_cases long : (templateBits low = 4 ∨ templateBits low = 5) ∧ index = 1
    · simp only [long, true_and, and_true, if_true] at admitted ⊢
      cases valid : longNop (slotBits low high 2)
      · simp only [valid, Bool.false_eq_true, ite_false] at admitted
        contradiction
      · simpa using nopPaired_matches (templateBits low) _ _ _ _ long.1 valid
    · simp only [long, if_false] at admitted ⊢
      by_cases skip : (templateBits low = 4 ∨ templateBits low = 5) ∧ index = 2
      · simp only [skip, true_and, and_true, if_true] at admitted
        contradiction
      · simp only [skip, if_false] at admitted ⊢
        have noSourceSkip : ¬((templateBits low = 4 ∨ templateBits low = 5) ∧ index = 2 ∧
            cPairSkip (templateBits low) (slotBits low high 1) (slotBits low high 0)
              (slotBits low high 2) address = true) := fun h => skip ⟨h.1, h.2.1⟩
        rw [if_neg noSourceSkip]
        exact nopShort_matches _ _ _ _ (nopOrdinaryPresent _ _ _ _ admitted)
  · simp only [legal, if_false]

theorem NOP_admitted (low high address : Word) (index : BitVec 2)
    (admitted : nopBundleEncoding reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit low high index) :
    (bundleNOP low high address index).getLsbD 394 = true := by
  rw [NOP_allowed low high address index admitted, nopBundlePresence]
  exact admitted
