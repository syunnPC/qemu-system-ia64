/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace FullDecodeSpec

inductive LongForm where
  | movl | brlCond | brlCall

def mlx (code : BitVec 5) : Prop := code = 4 ∨ code = 5

instance (code : BitVec 5) : Decidable (mlx code) := by unfold mlx; infer_instance

def longEncoding (form : LongForm) (x : Slot) : Prop :=
  match form with
  | .movl => x.extractLsb' 37 4 = 6 ∧ x.getLsbD 20 = false
  | .brlCond => x.extractLsb' 37 4 = 12 ∧ x.extractLsb' 6 3 = 0
  | .brlCall => x.extractLsb' 37 4 = 13

instance (form : LongForm) (x : Slot) : Decidable (longEncoding form x) := by
  cases form <;> unfold longEncoding <;> infer_instance

def longImm (form : LongForm) (l x : Slot) : Word :=
  match form with
  | .movl => x.extractLsb' 36 1 ++ l ++ x.extractLsb' 21 1 ++
      x.extractLsb' 22 5 ++ x.extractLsb' 27 9 ++ x.extractLsb' 13 7
  | _ => (x.extractLsb' 36 1 ++ l.extractLsb' 2 39 ++ x.extractLsb' 13 20).signExtend 64 <<< 4

def longReg (form : LongForm) (x : Slot) : BitVec 8 :=
  match form with
  | .movl => (x.extractLsb' 6 7).zeroExtend 8
  | _ => 0

def longLink (form : LongForm) (x : Slot) : BitVec 8 :=
  match form with
  | .brlCall => (x.extractLsb' 6 3).zeroExtend 8
  | _ => 0

def longRecord (lu xu : UnitCode) (form : LongForm) (l x : Slot) (address : Word) :
    Observation :=
  let (unit, raw) := match form with
    | .movl => (lu, l)
    | _ => (xu, x)
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ address ++ 1#8 ++
    (x.extractLsb' 0 6).zeroExtend 8 ++ longReg form x ++ 0#32 ++ longLink form x ++
    0#24 ++ longImm form l x ++ 2#82

def longBundleEncoding (form : LongForm) (low high : Word) (slot : BitVec 2) : Prop :=
  mlx (templateBits low) ∧ slot = 1 ∧ longEncoding form (slotBits low high 2)

instance (form : LongForm) (low high : Word) (slot : BitVec 2) :
    Decidable (longBundleEncoding form low high slot) := by
  unfold longBundleEncoding
  infer_instance

def aLongBundle (lu xu : UnitCode) (form : LongForm) (low high address : Word)
    (slot : BitVec 2) : Observation :=
  if longBundleEncoding form low high slot then
    longRecord lu xu form (slotBits low high 1) (slotBits low high 2) address else 0

abbrev LongDecode := Word → Word → Word → BitVec 2 → Observation

def LongBundleContract (lu xu : UnitCode) (form : LongForm) (decode : LongDecode) : Prop :=
  ∀ low high address slot,
    decode low high address slot = aLongBundle lu xu form low high address slot ∧
    ((decode low high address slot).getLsbD 394 = true ↔
      longBundleEncoding form low high slot)

def LongPairContract (lu xu : UnitCode) (form : LongForm) (decode : LongDecode)
    (completion : Word → Word → Word → BitVec 2 → BitVec 2)
    (stop : Word → Word → Word → BitVec 2 → Bool) : Prop :=
  ∀ low high address slot, longBundleEncoding form low high slot →
    decode low high address slot = aLongBundle lu xu form low high address slot ∧
    decode low high address 2 = 0 ∧ completion low high address slot = 2 ∧
    stop low high address slot = decide (templateBits low = 5)

def branchHints (x : Slot) : BitVec 4 :=
  x.extractLsb' 33 2 ++ x.extractLsb' 12 1 ++ x.extractLsb' 35 1

def branchTarget (l x : Slot) (address : Word) : Word :=
  address + (x.extractLsb' 36 1 ++ l.extractLsb' 2 39 ++ x.extractLsb' 13 20 ++ 0#4)

def LongBranchContract (lu xu : UnitCode) (form : LongForm) (decode : LongDecode)
    (completion : Word → Word → Word → BitVec 2 → BitVec 2)
    (stop : Word → Word → Word → BitVec 2 → Bool)
    (request : Word → Word → Word → BitVec 2 → Word → Bool → Option (Word × Slot × BitVec 8))
    (target : Observation → Word) (hints : Observation → BitVec 4) : Prop :=
  LongBundleContract lu xu form decode ∧
  LongPairContract lu xu form decode completion stop ∧
  (∀ low high address slot, longBundleEncoding form low high slot →
    target (decode low high address slot) =
      branchTarget (slotBits low high 1) (slotBits low high 2) address ∧
    hints (decode low high address slot) = branchHints (slotBits low high 2)) ∧
  ∀ low high address slot cpuid4 qualifies, templateBits low = 5 →
    request low high address slot cpuid4 qualifies =
      if longBundleEncoding form low high slot ∧ cpuid4.getLsbD 0 = false
        then some (address, slotBits low high 2, 1) else none

theorem longRecordPresent (lu xu : UnitCode) (form : LongForm) (l x : Slot) (address : Word) :
    (longRecord lu xu form l x address).getLsbD 394 = true := by
  cases form <;> dsimp only [longRecord] <;> bv_decide

theorem aLongBundlePresent (lu xu : UnitCode) (form : LongForm)
    (low high address : Word) (slot : BitVec 2) :
    (aLongBundle lu xu form low high address slot).getLsbD 394 = true ↔
      longBundleEncoding form low high slot := by
  by_cases h : longBundleEncoding form low high slot <;>
    simp [aLongBundle, h, longRecordPresent]

end FullDecodeSpec
