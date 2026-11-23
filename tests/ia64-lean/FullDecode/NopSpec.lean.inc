/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace FullDecodeSpec

def shortNop (m i b f unit : UnitCode) (raw : Slot) : Bool :=
  let major := raw.extractLsb' 37 4
  let x6 := raw.extractLsb' 27 6
  let y := raw.getLsbD 26
  let x3 := raw.extractLsb' 33 3
  decide ((unit = m ∨ unit = i) ∧ major = 0 ∧ x3 = 0 ∧ x6 = 1 ∧ y = false ∨
    unit = f ∧ major = 0 ∧ raw.getLsbD 33 = false ∧ x6 = 1 ∧ y = false ∨
    unit = b ∧ major = 2 ∧ x6 = 0)

def longNop (x : Slot) : Bool :=
  decide (x.extractLsb' 37 4 = 0 ∧ x.extractLsb' 33 3 = 0 ∧
    x.extractLsb' 27 6 = 1 ∧ x.getLsbD 26 = false)

-- White entries in Tables 4-3 and 4-55 denote ignored operations.
-- The four assigned B-unit major-2 cells are nop, hint and two brp forms.
def ignoredBranch (b unit : UnitCode) (raw : Slot) : Bool :=
  let major := raw.extractLsb' 37 4
  let x6 := raw.extractLsb' 27 6
  decide (unit = b ∧ (major = 3 ∨ major = 6 ∨
    (major = 2 ∧ x6 ≠ 0 ∧ x6 ≠ 1 ∧ x6 ≠ 0x10 ∧ x6 ≠ 0x11)))

-- I18/M48/B9/F16 carry imm21. X5 extends that field
-- with imm41 in the L slot.
def nopImmShort (raw : Slot) : BitVec 21 :=
  raw.extractLsb' 36 1 ++ raw.extractLsb' 6 20

def nopImmLong (l x : Slot) : Word :=
  (l ++ nopImmShort x).zeroExtend 64

-- The decoder uses class 1 and qp=0 for white operations. The
-- generic F-unit record retains sf metadata even for nop.f.
def nopRecord (unit : UnitCode) (raw : Slot) (address : Word) (slot sf : BitVec 8)
    (imm : Word) (ignored : Bool) : Observation :=
  1#1 ++ unit.zeroExtend 32 ++ raw.zeroExtend 64 ++ address ++ slot ++
    (if ignored then 0 else reg raw 0 6) ++ 0#56 ++ sf ++ 0#8 ++ imm ++
    (if ignored then 1#32 else 0#32) ++ 0#48 ++ 2#2

def nopOrdinarySpec (m i b f unit : UnitCode) (raw : Slot) (address : Word)
    (slot : BitVec 8) : Observation :=
  if ignoredBranch b unit raw then nopRecord unit raw address slot 0 0 true
  else if shortNop m i b f unit raw then
    nopRecord unit raw address slot (if unit = f then reg raw 34 2 else 0)
      ((nopImmShort raw).zeroExtend 64) false
  else 0

def nopBundleSpec (reserved m i b f l x : UnitCode) (low high address : Word)
    (index : BitVec 2) : Observation :=
  let code := templateBits low
  if validTemplate code = true ∧ index < 3 then
    if (code = 4 ∨ code = 5) ∧ index = 1 then
      let lSlot := slotBits low high 1
      let xSlot := slotBits low high 2
      if longNop xSlot then
        nopRecord x xSlot address 1 0 (nopImmLong lSlot xSlot) false
      else 0
    else if (code = 4 ∨ code = 5) ∧ index = 2 then 0
    else nopOrdinarySpec m i b f (templateUnit reserved m i b f l x code index)
      (slotBits low high index) address (index.zeroExtend 8)
  else 0

-- Presence is derived entirely from the SDM's assigned nop and white
-- forms. The address input is irrelevant to that bit.
def nopBundleEncoding (reserved m i b f l x : UnitCode) (low high : Word)
    (index : BitVec 2) : Prop :=
  (nopBundleSpec reserved m i b f l x low high 0 index).getLsbD 394 = true

private theorem nopRecord_present (unit : UnitCode) (raw : Slot) (address : Word)
    (slot sf : BitVec 8) (imm : Word) (ignored : Bool) :
    (nopRecord unit raw address slot sf imm ignored).getLsbD 394 = true := by
  dsimp only [nopRecord]
  bv_decide

private theorem nopPresence_if (choose : Prop) [Decidable choose] (yes no : Observation) :
    (if choose then yes else no).getLsbD 394 =
      (if choose then yes.getLsbD 394 else no.getLsbD 394) := by
  split <;> rfl

theorem nopBundlePresence (reserved m i b f l x : UnitCode) (low high address : Word)
    (index : BitVec 2) :
    (nopBundleSpec reserved m i b f l x low high address index).getLsbD 394 =
      (nopBundleSpec reserved m i b f l x low high 0 index).getLsbD 394 := by
  simp only [nopBundleSpec, nopOrdinarySpec, nopPresence_if, nopRecord_present]

end FullDecodeSpec
