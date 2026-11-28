/- SPDX-License-Identifier: GPL-2.0-or-later -/

private theorem longTemplate (code : BitVec 5) (slot : BitVec 2)
    (pair : mlx code ∧ slot = 1) : validTemplate code = true ∧ slot < 3 := by
  dsimp only [mlx, validTemplate] at *
  bv_decide

private theorem pairMatchesAtBundle (form : LongForm)
    (single : UnitCode → Slot → Word → BitVec 8 → Observation)
    (paired : BitVec 5 → Slot → Slot → Slot → Word → Observation)
    (pair_matches : ∀ code l m x address, mlx code → longEncoding form x →
      paired code l m x address = longRecord lUnit xUnit form l x address)
    (pair_skips : ∀ code l m x address, mlx code → longEncoding form x →
      cPairSkip code l m x address = true) :
    LongPairContract lUnit xUnit form (cBundle single paired) cLongCompletion cLongStop := by
  intro low high address slot legal
  have templ : (cTemplate low).setWidth 5 = templateBits low := by
    rw [bundleTemplate_matches]; simp
  have bits (n : BitVec 2) : (cSlot low high n).setWidth 41 = slotBits low high n := by
    rw [bundleSlot_matches]; simp
  obtain ⟨code, rfl, selected⟩ := legal
  have bounded := longTemplate (templateBits low) 1 ⟨code, rfl⟩
  have skip := pair_skips _ (slotBits low high 1) (slotBits low high 0) _ address code selected
  have complete : cLongCompletion low high address 1 = 2 := by
    unfold cLongCompletion
    rw [templ]
    simp only [bits]
    exact if_pos ⟨skip, by decide⟩
  have isMlx : templateBits low = 4 ∨ templateBits low = 5 := code
  refine ⟨?_, ?_, complete, ?_⟩
  · have admitted : longBundleEncoding form low high 1 := ⟨code, rfl, selected⟩
    simp only [cBundle, aLongBundle, templ, definedTemplate_matches, bits, if_pos bounded,
      if_pos admitted, isMlx, true_and, if_true]
    exact pair_matches _ _ _ _ _ code selected
  · have notOne : ¬ ((templateBits low = 4 ∨ templateBits low = 5) ∧ (2 : BitVec 2) = 1) :=
      fun h => (by decide : (2 : BitVec 2) ≠ 1) h.2
    have two : validTemplate (templateBits low) = true ∧ (2 : BitVec 2) < 3 :=
      ⟨bounded.1, by decide⟩
    simp only [cBundle, templ, definedTemplate_matches, bits, if_pos two, if_neg notOne]
    simp only [isMlx, true_and, skip, if_true]
  · rw [cLongStop, templ, complete]
    generalize templateBits low = t at code ⊢
    dsimp only [cStop, mlx] at *
    bv_decide

private theorem longBundleMatches (form : LongForm)
    (single : UnitCode → Slot → Word → BitVec 8 → Observation)
    (paired : BitVec 5 → Slot → Slot → Slot → Word → Observation)
    (pair_matches : ∀ code l m x address, mlx code →
      paired code l m x address =
        if longEncoding form x then longRecord lUnit xUnit form l x address else 0)
    (other_absent : ∀ code l m x address index,
      validTemplate code = true → index < 3 →
      ¬ (mlx code ∧ index = 1) →
      ¬ (mlx code ∧ index = 2 ∧ cPairSkip code l m x address = true) →
      single (templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit code index)
        (if index = 0 then m else if index = 1 then l else x) address (index.zeroExtend 8) = 0)
    (low high address : Word) (index : BitVec 2) :
    cBundle single paired low high address index = aLongBundle lUnit xUnit form low high address index := by
  have templ : (cTemplate low).setWidth 5 = templateBits low := by
    rw [bundleTemplate_matches]; simp
  have bits (n : BitVec 2) : (cSlot low high n).setWidth 41 = slotBits low high n := by
    rw [bundleSlot_matches]; simp
  simp only [cBundle, aLongBundle, longBundleEncoding, templ, definedTemplate_matches, bits,
    slotUnit_matches]
  split <;> rename_i bounds
  · split <;> rename_i pair
    · rw [pair_matches _ _ _ _ _ pair.1]
      have isMlx : mlx (templateBits low) := pair.1
      simp [isMlx, pair.2]
    · have absent : ¬ (mlx (templateBits low) ∧ index = 1 ∧ longEncoding form (slotBits low high 2)) :=
        fun h => pair ⟨h.1, h.2.1⟩
      rw [if_neg absent]
      split <;> rename_i skip
      · rfl
      · have raw : slotBits low high index =
            (if index = 0 then slotBits low high 0 else if index = 1 then slotBits low high 1
             else slotBits low high 2) := by
          dsimp only [slotBits]
          bv_decide
        rw [raw]
        exact other_absent _ _ _ _ _ _ bounds.1 bounds.2 pair skip
  · rw [if_neg (fun h => bounds (longTemplate _ _ ⟨h.1, h.2.1⟩))]

def cLongAddress (record : Observation) : Word := record.extractLsb' 234 64
def cLongImmediate (record : Observation) : Word := record.extractLsb' 82 64
def cLongRaw (record : Observation) : Slot := record.extractLsb' 298 41
def cLongFaultSlot (record : Observation) : BitVec 8 := record.extractLsb' 226 8

def cLongBranchTarget (record : Observation) : Word := cLongAddress record + cLongImmediate record

def cLongBranchHints (record : Observation) : BitVec 4 :=
  branchHints (cLongRaw record)

private theorem longBranchFields (form : LongForm) (l x : Slot) (address : Word)
    (branch : match form with | .movl => False | _ => True) :
    let record := longRecord lUnit xUnit form l x address
    cLongAddress record = address ∧ cLongRaw record = x ∧ cLongFaultSlot record = 1 ∧
    cLongImmediate record = x.extractLsb' 36 1 ++ l.extractLsb' 2 39 ++ x.extractLsb' 13 20 ++ 0#4 := by
  cases form
  · contradiction
  all_goals
    dsimp only [cLongAddress, cLongRaw, cLongFaultSlot, cLongImmediate, longRecord, longImm, longReg, longLink]
    bv_decide

private theorem longBranchObservation (form : LongForm) (l x : Slot) (address : Word)
    (branch : match form with | .movl => False | _ => True) :
    cLongBranchTarget (longRecord lUnit xUnit form l x address) = branchTarget l x address ∧
    cLongBranchHints (longRecord lUnit xUnit form l x address) = branchHints x := by
  obtain ⟨ip, raw, _, disp⟩ := longBranchFields form l x address branch
  constructor
  · unfold cLongBranchTarget branchTarget
    rw [ip, disp]
  · unfold cLongBranchHints
    rw [raw]
