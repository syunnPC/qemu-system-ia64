/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Tcg
open IA64Integer
local notation "Word" => BitVec 64

def sourceKnownNat (f : Facts) (r : Reg) : Option Bool :=
  if r = 0 then some false else f.nat r

def sourcePredicate (s : State α) (i : Slot) (f : Facts) : Bool :=
  if i.qp = 0 then true else f.predicate.getD (s.pr i.qp)

theorem sourcePredicate_eq (s : State α) (i : Slot) (f : Facts)
    (sound : Sound s i f) : sourcePredicate s i f = SDM.qualified s i.qp := by
  by_cases zero : i.qp = 0
  · simp [sourcePredicate, SDM.qualified, zero]
  cases fact : f.predicate with
  | none => simp [sourcePredicate, SDM.qualified, zero, fact]
  | some b =>
    rw [sound.1 b fact]
    simp [sourcePredicate, zero, fact]

def sourceFrame (s : State α) (i : Slot) (f : Facts) : Bool :=
  if i.dst = 0 then false
  else if i.dst.val < 32 then true
  else if i.dst.val - 32 + 1 ≤ f.checkedSof then true
  else decide (i.dst.val - 32 < s.sof)

theorem sourceFrame_eq (s : State α) (i : Slot) (f : Facts)
    (sound : Sound s i f) : sourceFrame s i f = SDM.legalTarget s i.dst := by
  have bound := sound.2.2.2
  unfold sourceFrame SDM.legalTarget
  by_cases zero : i.dst = 0
  · simp [zero]
  by_cases staticReg : i.dst.val < 32
  · simp [zero, staticReg]
  have target : (i.dst.val - 32 < s.sof) = (i.dst.val < 32 + s.sof) := propext (by omega)
  by_cases checked : i.dst.val - 32 + 1 ≤ f.checkedSof
  · have valid : i.dst.val < 32 + s.sof := by omega
    simp [zero, staticReg, checked, valid]
  · simp [zero, staticReg, checked, target]

def sourceNatInput (s : State α) (f : Facts) (r : Reg) : Bool :=
  (sourceKnownNat f r).getD (SDM.nat s r)

theorem sourceNatInput_eq (s : State α) (i : Slot) (f : Facts) (r : Reg)
    (sound : Sound s i f) : sourceNatInput s f r = SDM.nat s r := by
  by_cases zero : r = 0
  · simp [sourceNatInput, sourceKnownNat, SDM.nat, zero]
  cases fact : f.nat r with
  | none => simp [sourceNatInput, sourceKnownNat, zero, fact]
  | some b =>
    rw [sound.2.1 r b fact]
    simp [sourceNatInput, sourceKnownNat, zero, fact]

def sourceCopy (s : State α) (f : Facts) (dst src : Reg) : Bool :=
  if dst = src then SDM.nat s dst else sourceNatInput s f src

def sourcePair (s : State α) (f : Facts) (dst r2 r3 : Reg) : Bool :=
  if r2 = r3 then sourceCopy s f dst r2
  else if dst = r2 then SDM.nat s dst || sourceNatInput s f r3
  else if dst = r3 then SDM.nat s dst || sourceNatInput s f r2
  else sourceNatInput s f r2 || sourceNatInput s f r3

theorem sourceCopy_eq (s : State α) (i : Slot) (f : Facts) (dst src : Reg)
    (sound : Sound s i f) : sourceCopy s f dst src = SDM.nat s src := by
  by_cases alias : dst = src
  · simp [sourceCopy, alias]
  · simp [sourceCopy, alias, sourceNatInput_eq _ i _ _ sound]

theorem sourcePair_eq (s : State α) (i : Slot) (f : Facts) (dst r2 r3 : Reg)
    (sound : Sound s i f) :
    sourcePair s f dst r2 r3 = (SDM.nat s r2 || SDM.nat s r3) := by
  by_cases sameSource : r2 = r3
  · simp [sourcePair, sameSource, sourceCopy_eq _ i _ _ _ sound]
  · by_cases left : dst = r2
    · simp [sourcePair, sameSource, left, sourceNatInput_eq _ i _ _ sound]
    · by_cases right : dst = r3
      · simp [sourcePair, sameSource, Ne.symm sameSource, left, right,
              sourceNatInput_eq _ i _ _ sound, Bool.or_comm]
      · simp [sourcePair, sameSource, left, right, sourceNatInput_eq _ i _ _ sound]

def sourceDirty (s : State α) (f : Facts) (dst : Reg) : Reg → Bool :=
  if dst.val < 32 || f.dirty dst then s.dirty else update s.dirty dst true

theorem sourceDirty_eq (s : State α) (i : Slot) (f : Facts) (dst : Reg)
    (sound : Sound s i f) :
    sourceDirty s f dst = (if dst.val < 32 then s.dirty else update s.dirty dst true) := by
  funext r
  by_cases staticReg : dst.val < 32
  · simp [sourceDirty, staticReg]
  cases dirty : f.dirty dst
  · simp [sourceDirty, staticReg, dirty]
  · have known := sound.2.2.1 dst dirty
    by_cases alias : r = dst
    · simp [sourceDirty, staticReg, dirty, update, alias, known]
    · simp [sourceDirty, staticReg, dirty, update, alias]

def sourceCommit (s : State α) (f : Facts) (dst : Reg) (val : Word) (nat : Bool) :
    State α :=
  { s with
    gr := update s.gr dst val
    natWord := update s.natWord (wordIndex dst)
      (IA64Integer.SDM.replaceNaT (s.natWord (wordIndex dst)) (bitIndex dst) nat)
    dirty := sourceDirty s f dst }

theorem sourceCommit_eq (s : State α) (i : Slot) (f : Facts) (dst : Reg)
    (val : Word) (nat : Bool) (sound : Sound s i f) :
    sourceCommit s f dst val nat = SDM.commit s dst val nat := by
  unfold sourceCommit SDM.commit
  rw [sourceDirty_eq _ i _ _ sound]

theorem replacePackedBit (word index : Word)
    (range : index < 64#64) :
    IA64Integer.SDM.replaceNaT word index (((word >>> index) &&& 1#64) != 0) = word := by
  simp only [IA64Integer.SDM.replaceNaT]
  bv_decide

theorem packedSameWordSources (word i j : Word)
    (left : i < 64#64) (right : j < 64#64) :
    ((word &&& ((1#64 <<< i) ||| (1#64 <<< j))) != 0) =
      ((((word >>> i) &&& 1#64) != 0) || (((word >>> j) &&& 1#64) != 0)) := by
  bv_decide

end IA64Tcg

namespace IA64Tcg

local notation "Word" => BitVec 64

theorem regWord_toNat (r : Reg) : (regWord r).toNat = r.val := by
  rw [regWord, BitVec.toNat_ofNat]
  exact Nat.mod_eq_of_lt (by have bound := r.isLt; omega)

theorem regWord_bound (r : Reg) (b : r.val < 32) : regWord r < 32#64 := by
  change (regWord r).toNat < 32
  simpa [regWord_toNat] using b

theorem regWord_nonzero (r : Reg) (nonzero : r ≠ 0) : 0#64 < regWord r := by
  change 0 < (regWord r).toNat
  rw [regWord_toNat]
  exact Nat.pos_of_ne_zero (fun h => nonzero (Fin.ext h))

theorem static_wordIndex (r : Reg) (static : r.val < 32) : wordIndex r = 0 := by
  unfold wordIndex
  omega

theorem static_bitIndex (r : Reg) (static : r.val < 32) : bitIndex r = regWord r := by
  simp [bitIndex, regWord, Nat.mod_eq_of_lt (by omega : r.val < 64)]

theorem static_nat (s : State α) (r : Reg) (small : r.val < 32) :
    IA64Integer.SDM.staticNaT (s.natWord 0) (regWord r) = SDM.nat s r := by
  by_cases zero : r = 0
  · subst r
    simp [IA64Integer.SDM.staticNaT, SDM.nat, regWord]
  · have wordNonzero : regWord r ≠ 0#64 := by
      intro equal
      have takesZero := congrArg BitVec.toNat equal
      rw [regWord_toNat] at takesZero
      exact zero (Fin.ext takesZero)
    have left : (regWord r != 0) = true := bne_iff_ne.mpr wordNonzero
    have right : (r != 0) = true := bne_iff_ne.mpr zero
    simp [IA64Integer.SDM.staticNaT, SDM.nat, left, right, wordNonzero,
          static_wordIndex r small, static_bitIndex r small]

def factWord (fact : Option Bool) : Word :=
  fact.elim (~~~0#64) IA64Integer.SDM.natBit

theorem sound_static_fact (s : State α) (i : Slot) (f : Facts) (r : Reg)
    (small : r.val < 32) (sound : Sound s i f) :
    IA64Integer.SDM.validNatFact (factWord (sourceKnownNat f r))
      (IA64Integer.SDM.staticNaT (s.natWord 0) (regWord r)) := by
  rw [static_nat s r small]
  unfold sourceKnownNat
  split
  · next zero =>
    simp [factWord, IA64Integer.SDM.validNatFact, SDM.nat, zero]
  · cases fact : f.nat r with
    | none => simp [factWord, fact, IA64Integer.SDM.validNatFact]
    | some b => simp [factWord, fact, IA64Integer.SDM.validNatFact, sound.2.1 r b fact]

theorem predWord (b : Bool) : (IA64Integer.SDM.natBit b != 0) = b := by
  cases b <;> decide

structure FastApplicability (i : Slot) (f : Facts) : Prop where
  valid : i.malformed = false
  placement : i.illegalPlacement = false
  reserved : i.reservedField = false
  feature : i.missingFeature = false
  qpNonzero : i.qp ≠ 0
  unknownPredicate : f.predicate = none
  dstNonzero : i.dst ≠ 0
  dstStatic : i.dst.val < 32
  r2Static : i.r2.val < 32
  r3Static : i.r3.val < 32

def sourceFastState (s : State α) (i : Slot) (word natWord : Word) : State α :=
  { s with gr := update s.gr i.dst word, natWord := update s.natWord 0 natWord }

def sourceFastOutcome (s : State α) (i : Slot) (word natWord : Word) (q : Bool) : Outcome α :=
  if q then .committed (sourceFastState s i word natWord)
  else .nullified (sourceFastState s i word natWord)

theorem updateSelf [DecidableEq α] (values : α → β) (i : α) :
    update values i (values i) = values := by
  funext j
  by_cases same : j = i
  · simp [update, same]
  · simp [update, same]

theorem fastEffectsCompose (s : State α) (i : Slot) (f : Facts) (op : Alu)
    (fast : FastApplicability i f) :
    sourceFastOutcome s i
      (IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.gr i.dst)
        (op.value (SDM.value s i.r2) (SDM.value s i.r3) i.imm))
      (IA64Integer.SDM.qualifiedWrite (s.pr i.qp) (s.natWord 0)
        (IA64Integer.SDM.replaceNaT (s.natWord 0) (regWord i.dst)
          (IA64Integer.SDM.sourceNaT op.usesR2 op.usesR3 (SDM.nat s i.r2) (SDM.nat s i.r3))))
      (s.pr i.qp) = sdStep s i op := by
  cases qp : s.pr i.qp
  · simp [sourceFastOutcome, sourceFastState, sdStep, SDM.prepare, finish,
          SDM.qualified, fast.qpNonzero, qp, fast.valid, IA64Integer.SDM.qualifiedWrite,
          updateSelf]
  · have fit : SDM.legalTarget s i.dst = true := by
      simp [SDM.legalTarget, fast.dstNonzero, fast.dstStatic]
    simp [sourceFastOutcome, sourceFastState, sdStep, SDM.prepare, finish, SDM.operate,
          SDM.commit, SDM.qualified, fast.qpNonzero, qp, fast.valid, fast.reserved,
          fast.placement, fast.feature, fast.dstStatic, fit, IA64Integer.SDM.qualifiedWrite,
          static_wordIndex i.dst fast.dstStatic, static_bitIndex i.dst fast.dstStatic]

end IA64Tcg
