/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64.Floating

abbrev Word := BitVec 64
abbrev Register := BitVec 82

-- SDM Vol. 1 §5.1.2, Figures 5-4 and 5-5. The exponent bias of a
-- register is 65535. A binary32 or binary64 denormal uses the exponent
-- of the smallest normal with its explicit integer bit clear.
def load (exponentBits fractionBits : Nat) (raw : BitVec (1 + exponentBits + fractionBits)) :
    Register :=
  let exp := raw.extractLsb' fractionBits exponentBits
  let frac := raw.extractLsb' 0 fractionBits
  let sign := raw.extractLsb' (exponentBits + fractionBits) 1
  let bias : BitVec 17 := 65535 - BitVec.ofNat 17 (2 ^ (exponentBits - 1) - 1)
  let e : BitVec 17 :=
    if exp == -1 then -1
    else if exp == 0 then (if frac == 0 then 0 else bias + 1)
    else bias + exp.zeroExtend 17
  sign ++ e ++ (if exp == 0 then 0 else 1 : BitVec 1) ++
    ((frac.zeroExtend 63) <<< (63 - fractionBits))

def load32 (raw : BitVec 32) : Register := load 8 23 raw
def load64 (raw : Word) : Register := load 11 52 raw

def sign (fp : Register) : BitVec 1 := fp.extractLsb' 81 1
def exponent (fp : Register) : BitVec 17 := fp.extractLsb' 64 17
def significand (fp : Register) : Word := fp.extractLsb' 0 64

-- Figures 5-7 and 5-8 select exponent bits, mask them by the explicit
-- integer bit, and truncate the fraction to the interchange format.
def store (exponentBits fractionBits : Nat) (fp : Register) :
    BitVec (1 + exponentBits + fractionBits) :=
  let exp := exponent fp
  let high := exp.extractLsb' 16 1
  let low := exp.extractLsb' 0 (exponentBits - 1)
  let frac := fp.extractLsb' (63 - fractionBits) fractionBits
  let output := if fp.getLsbD 63 then (high ++ low).zeroExtend exponentBits else 0
  sign fp ++ output ++ frac

def store32 (fp : Register) : BitVec 32 := store 8 23 fp
def store64 (fp : Register) : Word := store 11 52 fp

-- Figures 5-9 and 5-10. The high 46 bits in the spill format are zero.
def spill (fp : Register) : BitVec 128 := fp.zeroExtend 128
def fill (raw : BitVec 128) : Register := raw.extractLsb' 0 82

-- The compact cache admits canonical zeros and exactly representable
-- binary64 normals. See Vol. 1 Table 5-2 for the architected ranges.
def compact (fp : Register) : BitVec 65 :=
  let exp := exponent fp
  let aligned := fp.extractLsb' 0 11 = 0
  let normal := 0x0fc01 ≤ exp ∧ exp ≤ 0x103fe ∧ fp.getLsbD 63 = true ∧ aligned
  if normal ∨ (exp = 0 ∧ significand fp = 0) then
    (1 : BitVec 1) ++ store64 fp
  else 0

theorem loadStore32 (raw : BitVec 32) : store32 (load32 raw) = raw := by
  simp only [load32, load, store32, store, exponent, sign]
  bv_decide

theorem loadStore64 (raw : Word) : store64 (load64 raw) = raw := by
  simp only [load64, load, store64, store, exponent, sign]
  bv_decide

theorem fillSpill (fp : Register) : fill (spill fp) = fp := by
  simp only [spill, fill]
  bv_decide

-- SDM Vol. 1 §5.1.2, register representation and fixed f0/f1 values.
def fixedZero : Register := 0
def fixedOne : Register := (0 : BitVec 1) ++ (65535 : BitVec 17) ++ (1 <<< 63 : Word)
def natVal : Register := (0 : BitVec 1) ++ (0x1fffe : BitVec 17) ++ (0 : Word)
def integerRegister (sig : Word) : Register :=
  (0 : BitVec 1) ++ (0x1003e : BitVec 17) ++ sig

inductive MergeForm where
  | negativeSign | sign | signExp
  deriving DecidableEq

-- SDM Vol. 3 pp. 3:80–81, Figures 2-8 through 2-10.
def mergeFields (form : MergeForm) (a b : Register) : Register :=
  match form with
  | .negativeSign => (~~~sign a) ++ exponent b ++ significand b
  | .sign => sign a ++ exponent b ++ significand b
  | .signExp => sign a ++ exponent a ++ significand b

def merge (form : MergeForm) (a b : Register) : Register :=
  if a = natVal ∨ b = natVal then natVal else mergeFields form a b

-- An operand's authoritative storage after register selection. Ordinary
-- values carry an untagged binary64 word. Extended inputs retain all bits.
inductive Source where
  | zero | one | binary (raw : Word) | integer (sig : Word)
  | extended (value : Register) | nat
  deriving DecidableEq

def sourceValue (source : Source) : Register :=
  match source with
  | .zero => fixedZero
  | .one => fixedOne
  | .binary raw => load64 raw
  | .integer sig => integerRegister sig
  | .extended value => value
  | .nat => natVal

-- The cache records the canonical NaTVal using its dedicated tag.
def canonicalSource (source : Source) : Prop :=
  match source with
  | .extended value => value ≠ natVal
  | _ => True

def binaryNormal (raw : Word) : Prop :=
  raw.extractLsb' 52 11 ≠ 0 ∧ raw.extractLsb' 52 11 ≠ 0x7ff

inductive BitwiseForm where
  | band | bandComplement | bor | bxor | mixLR | mixR | mixL
  deriving DecidableEq

-- SDM Vol. 3 pp. 3:59–60, 3:83–84, 3:94, 3:142. The mix
-- instructions concatenate halves of the architected significands.
def bitwisePayload (form : BitwiseForm) (a b : Word) : Word :=
  match form with
  | .band => a &&& b
  | .bandComplement => a &&& ~~~b
  | .bor => a ||| b
  | .bxor => a ^^^ b
  | .mixLR => a.extractLsb' 32 32 ++ b.extractLsb' 0 32
  | .mixR => a.extractLsb' 0 32 ++ b.extractLsb' 0 32
  | .mixL => a.extractLsb' 32 32 ++ b.extractLsb' 32 32

def bitwiseRegister (form : BitwiseForm) (a b : Register) : Register :=
  if a = natVal ∨ b = natVal then natVal
  else integerRegister (bitwisePayload form (significand a) (significand b))

-- SDM Vol. 2 §3, PSR floating-point modified bits; Vol. 3
-- fp_update_psr. The low bank is f2–f31 and the high bank f32–f127.
def fpModifiedBit (dest : BitVec 7) : Word := if 32 ≤ dest then 32 else 16
def fpModifiedSet (dest : BitVec 7) : BitVec 32 := if 32 ≤ dest then 2 else 1
def markFpModified (dest : BitVec 7) (psr : Word) : Word := psr ||| fpModifiedBit dest

-- SDM Vol. 1 §4.5.1, rotating floating-point register renaming.
def physicalFr (index base : BitVec 7) : BitVec 7 :=
  if index < 32 then index else
    (((index.zeroExtend 9 - 32 + base.zeroExtend 9) % 96) + 32).setWidth 7

inductive FpClass where
  | zero | unnormalized | normal | infinity | signalingNaN | quietNaN | nat | unsupported
  deriving DecidableEq

-- SDM Vol. 1 §5.1.3, Table 5-2. Partition every register encoding.
def classify (fp : Register) : FpClass :=
  let e := exponent fp
  let sig := significand fp
  let fraction := sig.extractLsb' 0 63
  if fp = natVal then .nat
  else if e = 0 then
    if sig = 0 then .zero else .unnormalized
  else if e = 0x1ffff then
    if sig.getLsbD 63 = false then .unsupported
    else if fraction = 0 then .infinity
    else if fraction.getLsbD 62 then .quietNaN else .signalingNaN
  else if sig.getLsbD 63 then .normal else .unnormalized

-- SDM Vol. 3 FCLASS, Tables 2-24 and 2-25.
def classMember (fp : Register) (mask : BitVec 9) : Bool :=
  let signSelected := if fp.getLsbD 81 then mask.getLsbD 1 else mask.getLsbD 0
  match classify fp with
  | .zero => signSelected && mask.getLsbD 2
  | .unnormalized => signSelected && mask.getLsbD 3
  | .normal => signSelected && mask.getLsbD 4
  | .infinity => signSelected && mask.getLsbD 5
  | .signalingNaN => mask.getLsbD 6
  | .quietNaN => mask.getLsbD 7
  | .nat => mask.getLsbD 8
  | .unsupported => false

def classPair (fp : Register) (mask : BitVec 9) : Bool × Bool :=
  if fp = natVal ∧ mask.getLsbD 8 = false then (false, false)
  else let member := classMember fp mask
       (member, !member)

abbrev Predicates := BitVec 6 → Bool

def readPredicate (pr : Predicates) (index : BitVec 6) : Bool :=
  decide (index = 0#6) || pr index

def classUpdate (pr : Predicates) (p1 p2 : BitVec 6) (a b : Bool) : Predicates :=
  fun index => if index = 0#6 then true
    else if index = p1 then a else if index = p2 then b else pr index

inductive ClassFault where
  | none | illegal | disabled
  deriving DecidableEq

-- SDM Vol. 2 §3, PSR.dfl/dfh. The constant registers remain accessible.
def classDisabled (reg : BitVec 7) (psr : Word) : Bool :=
  if 32 ≤ reg then psr.getLsbD 19
  else if 2 ≤ reg then psr.getLsbD 18 else false

-- SDM Vol. 3 §4.6.3.2 and Table 4-74, F5 operand formation.
def f5Fields (raw : BitVec 41) : BitVec 35 :=
  raw.extractLsb' 0 6 ++ raw.extractLsb' 6 6 ++ raw.extractLsb' 27 6 ++
    raw.extractLsb' 13 7 ++ (raw.extractLsb' 20 7 ++ raw.extractLsb' 33 2) ++ raw.extractLsb' 12 1

def fclass (fp : Register) (mask : BitVec 9) (qp p1 p2 : BitVec 6) (unc : Bool)
    (reg : BitVec 7) (psr : Word) (pr : Predicates) : ClassFault × Predicates :=
  if readPredicate pr qp then
    if p1 = p2 then (.illegal, pr)
    else if classDisabled reg psr then (.disabled, pr)
    else let pair := classPair fp mask
         (.none, classUpdate pr p1 p2 pair.1 pair.2)
  else if unc then
    if p1 = p2 then (.illegal, pr)
    else (.none, classUpdate pr p1 p2 false false)
  else (.none, pr)

end IA64.Floating
