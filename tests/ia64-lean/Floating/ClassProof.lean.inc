/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem fclass_fields_matches (raw : BitVec 41) : cF5Fields raw = f5Fields raw := by
  dsimp only [cF5Fields, f5Fields]
  bv_decide

def cClassRaw (fp : Register) (isNat : Bool) (mask : BitVec 9) : Bool × Bool :=
  let words := cSpill fp
  cFclassDecision (words.extractLsb' 0 64) (words.extractLsb' 64 64) isNat mask

theorem classMask_matches (fp : Register) (mask : BitVec 9) :
    (cClassRaw fp (decide (fp = natVal)) mask).2 = classMember fp mask := by
  dsimp only [cClassRaw]
  rw [spillBits]
  dsimp only [cFclassDecision, spill, classMember, classify, natVal, exponent, significand]
  bv_decide

theorem classNaTNonmatch_matches (fp : Register) (mask : BitVec 9) :
    (cClassRaw fp (decide (fp = natVal)) mask).1 =
      (decide (fp = natVal) && !mask.getLsbD 8) := by
  dsimp only [cClassRaw, cFclassDecision, natVal]
  bv_decide

def cClassValuePair (src : Source) (mask : BitVec 9) : Bool × Bool :=
  let words := cReadSpill src
  let (unselectedNaT, member) :=
    cFclassDecision (words.extractLsb' 0 64) (words.extractLsb' 64 64) (decide (cIsNaT src)) mask
  if unselectedNaT then (false, false) else (member, !member)

theorem classValuePair_matches (src : Source) (mask : BitVec 9) (canonical : canonicalSource src) :
    cClassValuePair src mask = classPair (sourceValue src) mask := by
  dsimp only [cClassValuePair]
  have cache := cReadSpill_matches src
  rw [← spillBits] at cache
  rw [cache]
  simp only [cIsNaT_matches src canonical]
  change (if (cClassRaw (sourceValue src) (decide (sourceValue src = natVal)) mask).1
          then (false, false)
          else ((cClassRaw (sourceValue src) (decide (sourceValue src = natVal)) mask).2,
                !(cClassRaw (sourceValue src) (decide (sourceValue src = natVal)) mask).2)) = _
  rw [classMask_matches, classNaTNonmatch_matches]
  unfold classPair
  by_cases nat : sourceValue src = natVal <;> cases bit : mask.getLsbD 8 <;> simp [nat, bit]

def cSetPred (pr : Predicates) (p : BitVec 6) (value : Bool) : Predicates :=
  fun index => if p ≠ 0#6 ∧ p = index then value else pr index

def cSetClassPair (pr : Predicates) (p1 p2 : BitVec 6) (v1 v2 : Bool) (restoreTrue : Bool) :
    Predicates :=
  let updated := cSetPred (cSetPred pr p1 v1) p2 v2
  fun index => if restoreTrue ∧ index = 0#6 then true else updated index

theorem classPairWrite_matches (pr : Predicates) (p1 p2 : BitVec 6) (v1 v2 restoreTrue : Bool)
    (distinct : p1 ≠ p2) (trueRegister : pr 0#6 = true) :
    cSetClassPair pr p1 p2 v1 v2 restoreTrue = classUpdate pr p1 p2 v1 v2 := by
  funext index
  unfold cSetClassPair cSetPred classUpdate
  by_cases zero : index = 0#6
  · subst index
    simp [trueRegister]
  · by_cases first : index = p1
    · subst index
      have different := Ne.symm distinct
      simp [zero, different]
    · by_cases second : index = p2
      · subst index
        simp [zero, first]
      · simp [zero, first, second, Ne.symm first, Ne.symm second]

theorem classDisabled_matches (reg : BitVec 7) (psr : Word) (enabledSets : BitVec 32)
    (sound : (cClassSet reg &&& enabledSets) ≠ 0 → classDisabled reg psr = false) :
    cClassDisabled reg psr enabledSets = classDisabled reg psr := by
  dsimp only [cClassDisabled, cClassSet, classDisabled] at *
  bv_decide

def cClassExecute (src : Source) (mask : BitVec 9) (qp p1 p2 : BitVec 6) (unc : Bool)
    (reg : BitVec 7) (psr : Word) (enabledSets : BitVec 32) (pr : Predicates) :
    ClassFault × Predicates :=
  let qualifies := if qp = 0#6 then true else pr qp
  if unc ∧ p1 = p2 then (.illegal, pr)
  else if qualifies = false then
    if unc then (.none, cSetClassPair pr p1 p2 false false false) else (.none, pr)
  else if p1 = p2 then (.illegal, pr)
  else if cClassDisabled reg psr enabledSets then (.disabled, pr)
  else let pair := cClassValuePair src mask
       (.none, cSetClassPair pr p1 p2 pair.1 pair.2 true)

theorem fclass_qualified (src : Source) (mask : BitVec 9) (qp p1 p2 : BitVec 6) (unc : Bool)
    (reg : BitVec 7) (psr : Word) (enabledSets : BitVec 32) (pr : Predicates)
    (canonical : canonicalSource src) (trueRegister : pr 0#6 = true)
    (sound : (cClassSet reg &&& enabledSets) ≠ 0 → classDisabled reg psr = false) :
    cClassExecute src mask qp p1 p2 unc reg psr enabledSets pr =
      fclass (sourceValue src) mask qp p1 p2 unc reg psr pr := by
  unfold cClassExecute fclass
  rw [classDisabled_matches _ _ _ sound, classValuePair_matches _ _ canonical]
  have gate : (if qp = 0#6 then true else pr qp) = readPredicate pr qp := by
    by_cases zero : qp = 0#6 <;> simp [readPredicate, zero]
  rw [gate]
  by_cases distinct : p1 = p2
  · cases qual : readPredicate pr qp <;> cases unc <;> simp [distinct, qual]
  · simp only [distinct, and_false, if_false]
    cases qual : readPredicate pr qp <;> cases unc
    all_goals simp [qual, classPairWrite_matches _ _ _ _ _ _ distinct trueRegister]
