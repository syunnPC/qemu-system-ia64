/- SPDX-License-Identifier: GPL-2.0-or-later -/

def cHelperSig (src : Source) : Word :=
  match src with
  | .zero => cHelperSigZero
  | .one => cHelperSigOne
  | .binary raw => cHelperBinarySig raw
  | .integer sig => sig
  | .extended fp => significand fp
  | .nat => 0

def cFastSig (src : Source) : Word :=
  match src with
  | .zero => cFastSigZero
  | .one => cFastSigOne
  | .binary raw => cFastSignificand raw
  | .integer sig => sig
  | .extended _ | .nat => 0

def cBitwiseFast (src : Source) (orphan : Bool) : Prop :=
  match src with
  | .zero | .one | .integer _ => True
  | .binary _ => orphan = false
  | .extended _ | .nat => False

instance (src : Source) (orphan : Bool) : Decidable (cBitwiseFast src orphan) := by
  cases src <;> dsimp only [cBitwiseFast] <;> infer_instance

def cBitwiseHelper (form : BitwiseForm) (a b : Source) : Register :=
  if cIsNaT a ∨ cIsNaT b then cSpillResult (cSpill cNatRegister)
  else cIntegerRegister (cHelperBits form (cHelperSig a) (cHelperSig b))

def cBitwise (form : BitwiseForm) (a b : Source) (aOrphan bOrphan sameRegister : Bool) :
    Register :=
  if cBitwiseFast a aOrphan ∧ cBitwiseFast b bOrphan then
    let left := cFastSig a
    let right := if sameRegister then left else cFastSig b
    cIntegerRegister (cTcgBits form left right)
  else cBitwiseHelper form a b

private theorem sigInteger (sig : Word) : significand (integerRegister sig) = sig := by
  dsimp only [significand, integerRegister]
  bv_decide

theorem cHelperSig_matches (src : Source) : cHelperSig src = significand (sourceValue src) := by
  cases src <;> dsimp only [cHelperSig, sourceValue]
  · decide
  · decide
  · dsimp only [cHelperBinarySig, significand, load64, load]
    bv_decide
  · exact (sigInteger _).symm
  · decide

theorem cFastSig_matches (src : Source) (orphan : Bool) (fast : cBitwiseFast src orphan) :
    cFastSig src = significand (sourceValue src) := by
  cases src <;> dsimp only [cFastSig, sourceValue, cBitwiseFast] at *
  · decide
  · decide
  · apply fastSignificand
  · exact (sigInteger _).symm

theorem cFastIsNotNaT (src : Source) (orphan : Bool) (fast : cBitwiseFast src orphan) :
    ¬ cIsNaT src := by
  intro nat
  rw [show src = .nat from nat] at fast
  exact fast

theorem cTcgBits_matches (form : BitwiseForm) (a b : Word) :
    cTcgBits form a b = bitwisePayload form a b := by
  cases form <;> dsimp only [cTcgBits, bitwisePayload] <;> bv_decide

theorem cHelperBits_matches (form : BitwiseForm) (a b : Word) :
    cHelperBits form a b = bitwisePayload form a b := by
  cases form <;> dsimp only [cHelperBits, bitwisePayload] <;> bv_decide

theorem cBitwiseHelper_matches (form : BitwiseForm) (a b : Source)
    (ha : canonicalSource a) (hb : canonicalSource b) :
    cBitwiseHelper form a b = bitwiseRegister form (sourceValue a) (sourceValue b) := by
  unfold cBitwiseHelper bitwiseRegister
  simp only [cIsNaT_matches _ ha, cIsNaT_matches _ hb]
  split
  · rw [cSpillResult_matches, cNaT_matches, spillBits, fillSpill]
  · rw [cInteger_matches, cHelperBits_matches, cHelperSig_matches, cHelperSig_matches]

theorem f9_bitwise_value (form : BitwiseForm) (a b : Source) (aOrphan bOrphan sameRegister : Bool)
    (ha : canonicalSource a) (hb : canonicalSource b) (same : sameRegister = true → a = b) :
    cBitwise form a b aOrphan bOrphan sameRegister =
      bitwiseRegister form (sourceValue a) (sourceValue b) := by
  unfold cBitwise
  split <;> rename_i fast
  · have leftNotNaT := fun n => cFastIsNotNaT a aOrphan fast.1 ((cIsNaT_matches _ ha).mpr n)
    have rightNotNaT := fun n => cFastIsNotNaT b bOrphan fast.2 ((cIsNaT_matches _ hb).mpr n)
    have noNaT : ¬ (sourceValue a = natVal ∨ sourceValue b = natVal) :=
      fun nat => nat.elim leftNotNaT rightNotNaT
    dsimp only
    rw [cInteger_matches, cTcgBits_matches, bitwiseRegister, if_neg noNaT]
    have selected : (if sameRegister then cFastSig a else cFastSig b) = cFastSig b := by
      split <;> rename_i branch
      · rw [same branch]
      · rfl
    rw [selected, cFastSig_matches a aOrphan fast.1, cFastSig_matches b bOrphan fast.2]
  · exact cBitwiseHelper_matches form a b ha hb
