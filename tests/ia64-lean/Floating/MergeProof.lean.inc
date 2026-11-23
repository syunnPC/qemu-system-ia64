/- SPDX-License-Identifier: GPL-2.0-or-later -/

set_option maxHeartbeats 0

def cNatRegister : Register := (0 : BitVec 1) ++ cNatExp ++ (0 : Word)
def cIntegerRegister (sig : Word) : Register := (0 : BitVec 1) ++ cIntegerExp ++ sig

def cReadSpill (src : Source) : BitVec 128 :=
  cSpill (match src with
    | .zero => cLoad64 cFixedZero
    | .one => cLoad64 cFixedOne
    | .binary raw => cLoad64 raw
    | .integer sig => cIntegerRegister sig
    | .extended fp => fp
    | .nat => cNatRegister)

/-- QEMU's fill chooses the unique authoritative representation. -/
def cWriteCache (fp : Register) : Source :=
  if sign fp = 0 ∧ exponent fp = cNatExp ∧ significand fp = 0 then .nat
  else if sign fp = 0 ∧ exponent fp = cIntegerExp then .integer (significand fp)
  else if (cCompact fp).getLsbD 64 then .binary ((cCompact fp).extractLsb' 0 64)
  else .extended fp

def cSpillResult (raw : BitVec 128) : Register := sourceValue (cWriteCache (cFill raw))

def cIsNaT (src : Source) : Prop := src = .nat
instance (src : Source) : Decidable (cIsNaT src) := by unfold cIsNaT; infer_instance

def cMergeHelper (form : MergeForm) (a b : Source) : Register :=
  cSpillResult (if cIsNaT a ∨ cIsNaT b then cSpill cNatRegister
    else cSplice form (cReadSpill a) (cReadSpill b))

def cBinarySource (src : Source) : Prop :=
  match src with
  | .zero | .one | .binary _ => True
  | _ => False

instance (src : Source) : Decidable (cBinarySource src) := by
  cases src <;> dsimp only [cBinarySource] <;> infer_instance

def cBinaryRaw (src : Source) : Word :=
  match src with
  | .zero => cFixedZero
  | .one => cFixedOne
  | .binary raw => raw
  | _ => 0

def cDirectAllowed (form : MergeForm) (a b : Source) : Prop :=
  ¬ (form = .signExp ∧ (a = .zero ∨ b = .zero)) ∧
  cBinarySource a ∧ cBinarySource b ∧
    (form = .signExp →
      (a = .one ∨ cBinaryNormal (cBinaryRaw a)) ∧
      (b = .one ∨ cBinaryNormal (cBinaryRaw b)))

instance (form : MergeForm) (a b : Source) : Decidable (cDirectAllowed form a b) := by
  unfold cDirectAllowed
  infer_instance

def cMerge (form : MergeForm) (a b : Source) : Register :=
  if cDirectAllowed form a b then cLoad64 (cDirectMerge form (cBinaryRaw a) (cBinaryRaw b))
  else cMergeHelper form a b

theorem cNaT_matches : cNatRegister = natVal := by decide

theorem cInteger_matches (sig : Word) : cIntegerRegister sig = integerRegister sig := by
  rfl

theorem cFixedZero_matches : load64 cFixedZero = fixedZero := fixedZeroEncoding

theorem cFixedOne_matches : load64 cFixedOne = fixedOne := fixedOneEncoding

theorem compact_roundtrip (fp : Register) (ok : (compact fp).getLsbD 64 = true) :
    load64 ((compact fp).extractLsb' 0 64) = fp := by
  dsimp only [compact, load64, load, store64, store, sign, exponent, significand] at *
  bv_decide

theorem cWriteCache_matches (fp : Register) : sourceValue (cWriteCache fp) = fp := by
  unfold cWriteCache
  split <;> rename_i nat
  · dsimp only [sourceValue, natVal, cNatExp, sign, exponent, significand] at *
    bv_decide
  · split <;> rename_i integer
    · dsimp only [sourceValue, integerRegister, cIntegerExp, sign, exponent, significand] at *
      bv_decide
    · rw [compactBits]
      split <;> rename_i compactOK
      · exact compact_roundtrip fp compactOK
      · rfl

theorem cSpillResult_matches (bits : BitVec 128) : cSpillResult bits = fill bits := by
  exact (cWriteCache_matches _).trans (fillBits _)

theorem cReadSpill_matches (src : Source) : cReadSpill src = spill (sourceValue src) := by
  unfold cReadSpill
  rw [spillBits]
  congr 1
  cases src with
  | zero => exact (loadDouble _).trans cFixedZero_matches
  | one => exact (loadDouble _).trans cFixedOne_matches
  | binary raw => exact loadDouble raw
  | integer sig => exact cInteger_matches sig
  | extended fp => rfl
  | nat => exact cNaT_matches

theorem binaryIsNotNaT (raw : Word) : load64 raw ≠ natVal := by
  dsimp only [load64, load, natVal]
  bv_decide

theorem cIsNaT_matches (src : Source) (canonical : canonicalSource src) :
    cIsNaT src ↔ sourceValue src = natVal := by
  cases src <;> dsimp only [cIsNaT, sourceValue] at *
  · decide
  · decide
  · constructor
    · intro impossible
      cases impossible
    · intro impossible
      exact False.elim (binaryIsNotNaT _ impossible)
  · constructor
    · intro impossible
      cases impossible
    · intro impossible
      dsimp only [integerRegister, natVal] at impossible
      bv_decide
  · simp only [canonicalSource] at canonical
    exact ⟨fun h => Source.noConfusion h, fun h => False.elim (canonical h)⟩
  · exact ⟨fun _ => rfl, fun _ => rfl⟩

theorem cSplice_matches (form : MergeForm) (a b : Register) :
    fill (cSplice form (spill a) (spill b)) = mergeFields form a b := by
  cases form <;> dsimp only [cSplice, fill, spill, mergeFields, sign, exponent, significand] <;>
    bv_decide

theorem cMergeHelper_matches (form : MergeForm) (a b : Source)
    (ha : canonicalSource a) (hb : canonicalSource b) :
    cMergeHelper form a b = merge form (sourceValue a) (sourceValue b) := by
  unfold cMergeHelper merge
  rw [cSpillResult_matches]
  simp only [cIsNaT_matches _ ha, cIsNaT_matches _ hb, cReadSpill_matches]
  split
  · rw [cNaT_matches, spillBits, fillSpill]
  · rw [cSplice_matches]

theorem cNormal_matches (raw : Word) : cBinaryNormal raw ↔ binaryNormal raw := by
  dsimp only [cBinaryNormal, binaryNormal]
  bv_decide

theorem cOnePassesNormal : cBinaryNormal cFixedOne := by decide

theorem cNormalOnFastSource (src : Source)
    (pass : src = .one ∨ cBinaryNormal (cBinaryRaw src)) :
    binaryNormal (cBinaryRaw src) := by
  apply (cNormal_matches _).mp
  rcases pass with fixed | normal
  · rw [fixed]
    exact cOnePassesNormal
  · exact normal

theorem directBinary_matches (form : MergeForm) (a b : Word)
    (normals : form = .signExp → binaryNormal a ∧ binaryNormal b) :
    load64 (cDirectMerge form a b) = merge form (load64 a) (load64 b) := by
  unfold merge
  simp only [binaryIsNotNaT, or_self, if_false]
  cases form with
  | negativeSign =>
    clear normals
    dsimp only [cDirectMerge, mergeFields, load64, load, sign, exponent, significand]
    bv_decide
  | sign =>
    clear normals
    dsimp only [cDirectMerge, mergeFields, load64, load, sign, exponent, significand]
    bv_decide
  | signExp =>
    have ⟨left, right⟩ := normals rfl
    clear normals
    dsimp only [binaryNormal] at left right
    dsimp only [cDirectMerge, mergeFields, load64, load, sign, exponent, significand]
    bv_decide

theorem cBinarySource_matches (src : Source) (direct : cBinarySource src) :
    load64 (cBinaryRaw src) = sourceValue src := by
  cases src <;> dsimp only [cBinarySource, cBinaryRaw, sourceValue] at *
  · exact cFixedZero_matches
  · exact cFixedOne_matches
  all_goals contradiction

/-- Equality of the destination's architected register bits after either
route. Triggering, fault ordering, and PSR updates are outside this value
theorem. -/
theorem fmerge_value (form : MergeForm) (a b : Source)
    (ha : canonicalSource a) (hb : canonicalSource b) :
    cMerge form a b = merge form (sourceValue a) (sourceValue b) := by
  unfold cMerge
  split <;> rename_i route
  · rw [loadDouble]
    rw [← cBinarySource_matches a route.2.1, ← cBinarySource_matches b route.2.2.1]
    apply directBinary_matches
    intro se
    exact ⟨cNormalOnFastSource _ (route.2.2.2 se).1, cNormalOnFastSource _ (route.2.2.2 se).2⟩
  · exact cMergeHelper_matches form a b ha hb

theorem signExpFixedZeroUsesSpill :
    ¬ cDirectAllowed .signExp .zero .one ∧
      cMerge .signExp .zero .one = (1 <<< 63 : Register) := by
  constructor
  · decide
  · rw [fmerge_value .signExp .zero .one trivial trivial]
    decide
