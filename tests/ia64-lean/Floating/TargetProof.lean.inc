/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem cTcgSet_matches (dest : BitVec 7) :
    cTcgSet dest = fpModifiedSet dest := by
  dsimp only [cTcgSet, fpModifiedSet] <;> bv_decide

theorem helperMark_matches (dest : BitVec 7) (psr : Word) (live : Bool) :
    cHelperMark dest psr live = (markFpModified dest psr, live || decide (32 ≤ dest)) := by
  dsimp only [cHelperMark, markFpModified, fpModifiedBit]
  rw [Prod.mk.injEq]
  bv_decide

theorem tcgMark_matches (dest : BitVec 7) (psr : Word) (live : Bool)
    (knownSets : BitVec 32) (knownLive : Bool)
    (knownPsr : (knownSets &&& fpModifiedSet dest) ≠ 0 → psr &&& fpModifiedBit dest = fpModifiedBit dest)
    (knownHigh : knownLive = true → live = true) :
    cTcgMark dest psr live knownSets knownLive =
      (markFpModified dest psr, live || decide (32 ≤ dest)) := by
  dsimp only [cTcgMark, markFpModified]
  rw [cTcgSet_matches, Prod.mk.injEq]
  dsimp only [fpModifiedBit, fpModifiedSet] at *
  bv_decide

structure FpWriteState where
  fr : BitVec 7 → Register
  psr : Word
  live : Bool

def cProjectedWrite (s : FpWriteState) (dest : BitVec 7) (value : Register)
    (fast : Bool) (knownSets : BitVec 32) (knownLive : Bool) : FpWriteState :=
  if dest ≤ 1 then s else
  let (psr, live) :=
    if fast then cTcgMark dest s.psr s.live knownSets knownLive
    else cHelperMark dest s.psr s.live
  ⟨fun index => if index = dest then value else s.fr index, psr, live⟩

theorem f9_bitwise_write (form : BitwiseForm) (s : FpWriteState) (dest index : BitVec 7)
    (a b : Source) (aOrphan bOrphan sameRegister knownLive : Bool) (knownSets : BitVec 32)
    (writable : 1 < dest) (ha : canonicalSource a) (hb : canonicalSource b)
    (same : sameRegister = true → a = b)
    (knownPsr : (knownSets &&& fpModifiedSet dest) ≠ 0 →
      s.psr &&& fpModifiedBit dest = fpModifiedBit dest)
    (knownHigh : knownLive = true → s.live = true) :
    let value := cBitwise form a b aOrphan bOrphan sameRegister
    let direct := decide (cBitwiseFast a aOrphan ∧ cBitwiseFast b bOrphan)
    let result := cProjectedWrite s dest value direct knownSets knownLive
    result.fr index =
      (if index = dest then bitwiseRegister form (sourceValue a) (sourceValue b) else s.fr index) ∧
    result.psr = markFpModified dest s.psr ∧ result.live = (s.live || decide (32 ≤ dest)) := by
  dsimp only [cProjectedWrite]
  have valid : ¬ dest ≤ 1 := by bv_decide
  rw [if_neg valid, f9_bitwise_value form a b aOrphan bOrphan sameRegister ha hb same]
  split
  · rw [tcgMark_matches _ _ _ _ _ knownPsr knownHigh]
    exact ⟨rfl, rfl, rfl⟩
  · rw [helperMark_matches]
    exact ⟨rfl, rfl, rfl⟩
