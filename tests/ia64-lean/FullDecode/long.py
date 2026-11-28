# SPDX-License-Identifier: GPL-2.0-or-later

LONG_FORMS = {"MOVL": ".movl", "BRL_COND": ".brlCond", "BRL_CALL": ".brlCall"}
LONG_REFUTATIONS = {"MOVL": (0x100000005, 0x6000080100000000)}


def long_samples():
    low, high = LONG_REFUTATIONS["MOVL"]
    bundle = high << 64 | low
    slot_mask = (1 << 41) - 1
    return [dict(code=low & 31, l=(bundle >> 5) & slot_mask,
                 raw=(bundle >> 46) & slot_mask, x=bundle >> 87, address=0)]


def long_completion(mask, merced):
    completion = """
def cLongCompletion (low high address : Word) (slot : BitVec 2) : BitVec 2 :=
  let code := (cTemplate low).setWidth 5
  let bits (index : BitVec 2) := (cSlot low high index).setWidth 41
  if cPairSkip code (bits 1) (bits 0) (bits 2) address = true ∧ slot = 1 then 2 else slot

def cLongStop (low high address : Word) (slot : BitVec 2) : Bool :=
  cStop ((cTemplate low).setWidth 5) (cLongCompletion low high address slot)
"""
    if mask is None:
        return completion
    return completion + f"""

def cLbReject (cpuid4 : Word) (_qualifies : Bool) : Bool :=
  decide ((cpuid4 &&& {mask}) = 0)

theorem lbReject_matches (cpuid4 : Word) (qualifies : Bool) :
    cLbReject cpuid4 qualifies = !cpuid4.getLsbD 0 := by
  dsimp only [cLbReject]
  bv_decide

def cMercedFeatures : Word := {merced}

theorem mercedLbAbsent : cMercedFeatures.getLsbD 0 = false := by decide
"""


def long_proof(opcode, *, legal_only=False):
    form = LONG_FORMS[opcode]
    common = f"""
def bundle{opcode} := cBundle c{opcode} cMlx{opcode}

theorem {opcode}_pair_matches (code : BitVec 5) (l m x : Slot) (address : Word)
    (pair : mlx code) (encoded : longEncoding {form} x) :
    cMlx{opcode} code l m x address = longRecord lUnit xUnit {form} l x address := by
  dsimp only [cMlx{opcode}, longRecord, longImm, longReg, longLink, lUnit, xUnit]
  dsimp only [mlx, longEncoding] at pair encoded
  bv_decide

theorem {opcode}_pair_skips (code : BitVec 5) (l m x : Slot) (address : Word)
    (pair : mlx code) (encoded : longEncoding {form} x) :
    cPairSkip code l m x address = true := by
  dsimp only [cPairSkip]
  dsimp only [mlx, longEncoding] at pair encoded
  bv_decide

theorem {opcode}_legal : LongPairContract lUnit xUnit {form} bundle{opcode} cLongCompletion cLongStop :=
  pairMatchesAtBundle {form} c{opcode} cMlx{opcode} {opcode}_pair_matches {opcode}_pair_skips
"""
    if legal_only:
        return common
    if opcode in LONG_REFUTATIONS:
        low, high = LONG_REFUTATIONS[opcode]
        return common + f"""
theorem {opcode}_observed :
    (bundle{opcode} {low} {high} 0 1).getLsbD 394 = true ∧
    (bundle{opcode} {low} {high} 0 1).extractLsb' 0 82 = 2 ∧
    (bundle{opcode} {low} {high} 0 1).extractLsb' 218 8 = 0 ∧
    (bundle{opcode} {low} {high} 0 1).extractLsb' 210 8 = 8 ∧
    ¬ longBundleEncoding {form} {low} {high} 1 := by decide

theorem {opcode}_refuted : ¬ LongBundleContract lUnit xUnit {form} bundle{opcode} := by
  intro equivalent
  exact {opcode}_observed.2.2.2.2 ((equivalent {low} {high} 0 1).2.mp {opcode}_observed.1)
"""

    return common + f"""
theorem {opcode}_bundle_matches (low high address : Word) (slot : BitVec 2) :
    bundle{opcode} low high address slot = aLongBundle lUnit xUnit {form} low high address slot := by
  unfold bundle{opcode}
  apply longBundleMatches
  · intro code l m x address pair
    by_cases encoded : longEncoding {form} x
    · simpa only [encoded, if_true] using {opcode}_pair_matches code l m x address pair encoded
    · dsimp only [cMlx{opcode}, longEncoding, longRecord, longImm, longReg, longLink, xUnit]
      dsimp only [mlx] at pair
      dsimp only [longEncoding] at encoded
      bv_decide
  · intros
    simp only [c{opcode}, p{opcode}]
    rfl

def lbRequest{opcode} (low high address : Word) (slot : BitVec 2)
    (cpuid4 : Word) (qualifies : Bool) : Option (Word × Slot × BitVec 8) :=
  let record := bundle{opcode} low high address slot
  if record.getLsbD 394 && cLbReject cpuid4 qualifies then
    some (cLongAddress record, cLongRaw record, cLongFaultSlot record)
  else none

theorem {opcode}_withFeature :
    LongBranchContract lUnit xUnit {form} bundle{opcode} cLongCompletion cLongStop lbRequest{opcode}
      cLongBranchTarget cLongBranchHints := by
  refine ⟨?_, {opcode}_legal, ?_, ?_⟩
  · intro low high address slot
    rw [{opcode}_bundle_matches]
    exact ⟨rfl, aLongBundlePresent _ _ _ _ _ _ _⟩
  · intro low high address slot encoded
    rw [{opcode}_bundle_matches]
    simp only [aLongBundle, encoded, if_true]
    exact longBranchObservation {form} _ _ _ trivial
  · intro low high address slot cpuid4 qualifies _stopped
    simp only [lbRequest{opcode}, {opcode}_bundle_matches, lbReject_matches]
    by_cases encoded : longBundleEncoding {form} low high slot
    · have fields := longBranchFields {form} (slotBits low high 1) (slotBits low high 2) address trivial
      simp only [aLongBundle, encoded, if_true, longRecordPresent, Bool.true_and, true_and,
        fields.1, fields.2.1, fields.2.2.1]
      cases cpuid4.getLsbD 0 <;> rfl
    · simp [aLongBundle, encoded]

theorem {opcode}_merced (low high address : Word) (slot : BitVec 2) (qualifies : Bool)
    (stopped : templateBits low = 5) (encoded : longBundleEncoding {form} low high slot) :
    lbRequest{opcode} low high address slot cMercedFeatures qualifies =
      some (address, slotBits low high 2, 1) := by
  rw [{opcode}_withFeature.2.2.2 _ _ _ _ _ _ stopped]
  rw [if_pos ⟨encoded, mercedLbAbsent⟩]
"""
