/- SPDX-License-Identifier: GPL-2.0-or-later -/

private theorem lowMultiplyAdd (a b addend : Word) :
    ((a.signExtend 128 : BitVec 128) * b.signExtend 128 + addend.zeroExtend 128).setWidth 64 =
      ((a.zeroExtend 128 : BitVec 128) * b.zeroExtend 128 + addend.zeroExtend 128).setWidth 64 := by
  rw [BitVec.setWidth_add _ _ (by decide : 64 ≤ 128),
      BitVec.setWidth_add _ _ (by decide : 64 ≤ 128),
      BitVec.setWidth_mul _ _ (by decide : 64 ≤ 128),
      BitVec.setWidth_mul _ _ (by decide : 64 ≤ 128)]
  have narrow (x : Word) : (x.signExtend 128).setWidth 64 = (x.zeroExtend 128).setWidth 64 := by
    bv_decide
  rw [narrow a, narrow b]

private theorem lowMultiplyAddExtract (a b addend : Word) :
    ((a.signExtend 128 : BitVec 128) * b.signExtend 128 + addend.zeroExtend 128).extractLsb' 0 64 =
      ((a.zeroExtend 128 : BitVec 128) * b.zeroExtend 128 + addend.zeroExtend 128).extractLsb' 0 64 := by
  simpa only [BitVec.setWidth_eq_extractLsb' (by decide : 64 ≤ 128)] using lowMultiplyAdd a b addend

private theorem splitShift (word : BitVec 128) :
    (word >>> 64).setWidth 64 = word.extractLsb' 64 64 := by
  bv_decide

private theorem addWordParts (nativeCarry : Bool) (word : BitVec 128) (addend : Word) :
    cTcgXmaAdd nativeCarry (word.extractLsb' 0 64) (word.extractLsb' 64 64) addend =
      ((word + addend.zeroExtend 128).extractLsb' 0 64,
       (word + addend.zeroExtend 128).extractLsb' 64 64) := by
  cases nativeCarry <;> simp only [cTcgXmaAdd, Bool.false_eq_true, if_false, if_true, Prod.mk.injEq]
  all_goals bv_decide

theorem xmaTcgPayload (form : XmaForm) (nativeCarry : Bool) (a b addend : Word) :
    cTcgXmaPayload (cXmaMode form) nativeCarry a b addend = xmaPayload form a b addend := by
  cases form <;> simp [cXmaMode, cTcgXmaPayload, xmaPayload, addWordParts]
  exact (lowMultiplyAddExtract a b addend).symm

theorem xmaHelperPayload (form : XmaForm) (a b addend : Word) :
    cHelperXmaPayload (cXmaMode form) a b addend = xmaPayload form a b addend := by
  cases form <;> simp [cXmaMode, cHelperXmaPayload, xmaPayload, splitShift]
  rw [BitVec.setWidth_eq_extractLsb' (by decide : 64 ≤ 128)]
  exact (lowMultiplyAddExtract a b addend).symm

theorem signedXmaCDefined (a b addend : Word) :
    -(2^127 : Int) ≤ a.toInt * b.toInt + addend.toNat ∧
      a.toInt * b.toInt + addend.toNat < (2^127 : Int) := by
  have absInput (x : Word) : x.toInt.natAbs ≤ 2^63 := by
    have lo := BitVec.le_toInt x
    have hi := BitVec.toInt_lt (x := x)
    change -(2^63 : Int) ≤ x.toInt at lo
    change x.toInt < (2^63 : Int) at hi
    omega
  have product : (a.toInt * b.toInt).natAbs ≤ 2^63 * 2^63 := by
    rw [Int.natAbs_mul]
    exact Nat.mul_le_mul (absInput a) (absInput b)
  have addendBound := addend.isLt
  change addend.toNat < 2^64 at addendBound
  omega

structure XmaOperands where
  destination : BitVec 7
  addend : BitVec 7
  left : BitVec 7
  right : BitVec 7

structure XmaCache where
  sources : BitVec 7 → Source
  sig : BitVec 128
  known : BitVec 128
  knownSig : BitVec 128

def cXmaRead (cache : XmaCache) (reg : BitVec 7) : Source :=
  if reg = 0 then .zero else if reg = 1 then .one else cache.sources reg

def sourceSigTag : Source → Bool
  | .integer _ => true
  | _ => false

structure XmaCacheSound (cache : XmaCache) : Prop where
  canonical : ∀ r, canonicalSource (cXmaRead cache r)
  current : ∀ r, 1 < r → cache.sig.getLsbD r.toNat = sourceSigTag (cXmaRead cache r)
  known : ∀ r : BitVec 7, 1 < r → cache.known.getLsbD r.toNat = true →
    cache.knownSig.getLsbD r.toNat = cache.sig.getLsbD r.toNat

def cXmaAddend (form : XmaForm) (reg : BitVec 7) : BitVec 7 :=
  if cXmaMode form = 3 then 0 else reg

def cXmaRegisterBit (reg : BitVec 7) : BitVec 128 :=
  if 1 < reg then 1#128 <<< reg.toNat else 0#128

def cXmaMask (form : XmaForm) (ops : XmaOperands) : BitVec 128 :=
  cXmaRegisterBit ops.left ||| cXmaRegisterBit ops.right |||
    cXmaRegisterBit (cXmaAddend form ops.addend)

def cXmaResolvedSig (cache : XmaCache) : BitVec 128 :=
  (cache.sig &&& ~~~cache.known) ||| (cache.knownSig &&& cache.known)

def cXmaFast (form : XmaForm) (cache : XmaCache) (ops : XmaOperands) : Bool :=
  let mask := cXmaMask form ops
  ((cXmaResolvedSig cache &&& mask) ^^^ mask) == 0#128

private theorem registerMaskBit (reg : BitVec 7) (i : Nat) :
    (cXmaRegisterBit reg).getLsbD i = true ↔ 1 < reg ∧ i = reg.toNat := by
  unfold cXmaRegisterBit
  have bound : reg.toNat < 128 := reg.isLt
  by_cases eligible : 1 < reg
  · rw [if_pos eligible, BitVec.getLsbD_shiftLeft, BitVec.getLsbD_one]
    simp only [eligible, true_and, Bool.and_eq_true, not_decide_eq_true, decide_eq_true_eq]
    omega
  · rw [if_neg eligible, BitVec.getLsbD_zero]
    simp only [Bool.false_eq_true, eligible, false_and]

private theorem requestMaskBit (form : XmaForm) (ops : XmaOperands) (i : Nat) :
    (cXmaMask form ops).getLsbD i = true ↔
      (1 < ops.left ∧ i = ops.left.toNat) ∨
      (1 < ops.right ∧ i = ops.right.toNat) ∨
      (1 < cXmaAddend form ops.addend ∧ i = (cXmaAddend form ops.addend).toNat) := by
  simp only [cXmaMask, BitVec.getLsbD_or, Bool.or_eq_true, registerMaskBit, or_assoc]

private theorem resolvedSigBit (cache : XmaCache) (sound : XmaCacheSound cache)
    (reg : BitVec 7) (eligible : 1 < reg) :
    (cXmaResolvedSig cache).getLsbD reg.toNat = sourceSigTag (cXmaRead cache reg) := by
  have bound : reg.toNat < 128 := reg.isLt
  simp only [cXmaResolvedSig, BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_not,
    _root_.decide_eq_true bound, Bool.true_and]
  cases known : cache.known.getLsbD reg.toNat
  · simp only [Bool.not_false, Bool.and_true, Bool.and_false, Bool.or_false, sound.current reg eligible]
  · simp only [Bool.not_true, Bool.and_true, Bool.and_false, Bool.false_or,
      sound.known reg eligible known, sound.current reg eligible]

private theorem maskedBitTest (sig mask : Bool) :
    ((sig && mask) ^^ mask) = false ↔ mask = true → sig = true := by
  cases sig <;> cases mask <;> decide

private theorem maskedSigTest (sig mask : BitVec 128) :
    (sig &&& mask) ^^^ mask = 0#128 ↔
      ∀ i, i < 128 → mask.getLsbD i = true → sig.getLsbD i = true := by
  constructor
  · intro h i _ bit
    apply (maskedBitTest _ _).mp _ bit
    simpa only [BitVec.getLsbD_xor, BitVec.getLsbD_and, BitVec.getLsbD_zero] using
      congrArg (fun bits : BitVec 128 => bits.getLsbD i) h
  · intro h
    apply BitVec.eq_of_getLsbD_eq
    intro i lt
    simp only [BitVec.getLsbD_xor, BitVec.getLsbD_and, BitVec.getLsbD_zero]
    exact (maskedBitTest _ _).mpr (h i lt)

def xmaDirectInput (cache : XmaCache) (reg : BitVec 7) : Prop :=
  1 < reg → sourceSigTag (cXmaRead cache reg) = true

theorem xmaFastGate (form : XmaForm) (cache : XmaCache) (ops : XmaOperands)
    (sound : XmaCacheSound cache) :
    cXmaFast form cache ops = true ↔
      xmaDirectInput cache ops.left ∧ xmaDirectInput cache ops.right ∧
      xmaDirectInput cache (cXmaAddend form ops.addend) := by
  simp only [cXmaFast, beq_iff_eq, maskedSigTest]
  constructor
  · intro accepted
    refine ⟨?_, ?_, ?_⟩
    all_goals
      intro eligible
      rw [← resolvedSigBit cache sound _ eligible]
      apply accepted _ (by exact BitVec.isLt _)
      rw [requestMaskBit]
    · exact .inl ⟨eligible, rfl⟩
    · exact .inr (.inl ⟨eligible, rfl⟩)
    · exact .inr (.inr ⟨eligible, rfl⟩)
  · rintro ⟨left, right, addend⟩ i _ needed
    rcases (requestMaskBit form ops i).mp needed with ⟨eligible, hit⟩ | ⟨eligible, hit⟩ | ⟨eligible, hit⟩
    · rw [hit, resolvedSigBit cache sound _ eligible]
      exact left eligible
    · rw [hit, resolvedSigBit cache sound _ eligible]
      exact right eligible
    · rw [hit, resolvedSigBit cache sound _ eligible]
      exact addend eligible

private theorem tagHasSig (src : Source) (tagged : sourceSigTag src = true) :
    cBitwiseFast src false := by
  cases src <;> simp_all [sourceSigTag, cBitwiseFast]

private theorem directSource (cache : XmaCache) (reg : BitVec 7) (fast : xmaDirectInput cache reg) :
    cBitwiseFast (cXmaRead cache reg) false := by
  by_cases eligible : 1 < reg
  · exact tagHasSig _ (fast eligible)
  · have fixed : reg = 0 ∨ reg = 1 := by bv_decide
    rcases fixed with rfl | rfl <;> simp [cXmaRead, cBitwiseFast]

def cXmaHelper (form : XmaForm) (a b addend : Source) : Register :=
  if cIsNaT a ∨ cIsNaT b ∨ cIsNaT addend then cSpillResult (cSpill cNatRegister)
  else cIntegerRegister
    (cHelperXmaPayload (cXmaMode form) (cHelperSig a) (cHelperSig b) (cHelperSig addend))

def cXmaValue (form : XmaForm) (cache : XmaCache) (ops : XmaOperands)
    (nativeCarry : Bool) : Register :=
  let a := cXmaRead cache ops.left
  let b := cXmaRead cache ops.right
  let addend := cXmaRead cache (cXmaAddend form ops.addend)
  if cXmaFast form cache ops then cIntegerRegister
    (cTcgXmaPayload (cXmaMode form) nativeCarry (cFastSig a) (cFastSig b) (cFastSig addend))
  else cXmaHelper form a b addend

private theorem xmaEffectiveAddend (form : XmaForm) (cache : XmaCache) (reg : BitVec 7) :
    sourceValue (cXmaRead cache (cXmaAddend form reg)) =
      (if form = .unsignedMultiply then fixedZero else sourceValue (cXmaRead cache reg)) := by
  cases form <;> simp [cXmaAddend, cXmaMode, cXmaRead, sourceValue]

theorem xmaValue (form : XmaForm) (cache : XmaCache) (ops : XmaOperands)
    (nativeCarry : Bool) (sound : XmaCacheSound cache) :
    cXmaValue form cache ops nativeCarry =
      xmaRegister form (sourceValue (cXmaRead cache ops.left)) (sourceValue (cXmaRead cache ops.right))
        (sourceValue (cXmaRead cache ops.addend)) := by
  unfold cXmaValue xmaRegister
  rw [← xmaEffectiveAddend form cache ops.addend]
  by_cases selected : cXmaFast form cache ops = true
  · rw [if_pos selected]
    obtain ⟨left, right, addend⟩ := (xmaFastGate form cache ops sound).mp selected
    have notNaT (reg : BitVec 7) (fast : xmaDirectInput cache reg) :
        sourceValue (cXmaRead cache reg) ≠ natVal :=
      fun nat => cFastIsNotNaT _ false (directSource cache reg fast)
        ((cIsNaT_matches _ (sound.canonical reg)).mpr nat)
    rw [cInteger_matches, xmaTcgPayload,
        cFastSig_matches _ _ (directSource cache _ left),
        cFastSig_matches _ _ (directSource cache _ right),
        cFastSig_matches _ _ (directSource cache _ addend)]
    simp [notNaT _ left, notNaT _ right, notNaT _ addend]
  · rw [if_neg selected]
    unfold cXmaHelper
    simp only [cIsNaT_matches _ (sound.canonical _)]
    split
    · rw [cSpillResult_matches, cNaT_matches, spillBits, fillSpill]
    · rw [cInteger_matches, xmaHelperPayload, cHelperSig_matches, cHelperSig_matches, cHelperSig_matches]

def cXmaWrite (form : XmaForm) (cache : XmaCache) (ops : XmaOperands) (s : FpWriteState)
    (nativeCarry : Bool) (knownSets : BitVec 32) (knownLive : Bool) : FpWriteState :=
  cProjectedWrite s ops.destination (cXmaValue form cache ops nativeCarry)
    (cXmaFast form cache ops) knownSets knownLive

theorem xmaWrite (form : XmaForm) (cache : XmaCache) (ops : XmaOperands) (s : FpWriteState)
    (nativeCarry : Bool) (knownSets : BitVec 32) (knownLive : Bool)
    (sound : XmaCacheSound cache) (writable : 1 < ops.destination)
    (views : ∀ r, sourceValue (cXmaRead cache r) = s.fr r)
    (knownPsr : (knownSets &&& fpModifiedSet ops.destination) ≠ 0 →
      s.psr &&& fpModifiedBit ops.destination = fpModifiedBit ops.destination)
    (knownHigh : knownLive = true → s.live = true) :
    let result := cXmaWrite form cache ops s nativeCarry knownSets knownLive
    (∀ index, result.fr index =
      (if index = ops.destination then
         xmaRegister form (s.fr ops.left) (s.fr ops.right) (s.fr ops.addend) else s.fr index)) ∧
    result.psr = markFpModified ops.destination s.psr ∧
    result.live = (s.live || decide (32 ≤ ops.destination)) := by
  simp only [cXmaWrite, cProjectedWrite]
  have movable : ¬ ops.destination ≤ 1 := by bv_decide
  rw [if_neg movable, xmaValue form cache ops nativeCarry sound, views, views, views]
  split
  · rw [tcgMark_matches _ _ _ _ _ knownPsr knownHigh]
    exact ⟨fun _ => rfl, rfl, rfl⟩
  · rw [helperMark_matches]
    exact ⟨fun _ => rfl, rfl, rfl⟩

private def xmaExampleCache : XmaCache :=
  ⟨fun reg => if reg = 2 then .integer (~~~0) else .nat, 4, 0, 0⟩

example :
    let before : FpWriteState := ⟨fun reg => sourceValue (cXmaRead xmaExampleCache reg), 0, false⟩
    let result := cXmaWrite .low xmaExampleCache ⟨2, 0, 2, 1⟩ before false 0 false
    result.fr 2 = integerRegister (1 <<< 63) ∧ result.fr 1 = fixedOne ∧ result.psr = 16 := by
  decide

example :
    cXmaValue .unsignedMultiply xmaExampleCache ⟨2, 0, 1, 1⟩ true =
      integerRegister (1 <<< 62) ∧
    cXmaValue .unsignedHigh xmaExampleCache ⟨2, 3, 1, 1⟩ false = natVal := by
  decide

example : cTcgXmaPayload (cXmaMode .signedHigh) false (~~~0) 1 1 = 0 ∧
    cTcgXmaPayload (cXmaMode .unsignedHigh) true (~~~0) 1 1 = 1 := by
  decide
