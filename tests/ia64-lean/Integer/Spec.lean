/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Integer

local notation "Word" => BitVec 64

/- Intel Itanium Architecture Software Developer's Manual, rev. 2.3,
   volume 3 (323207-001).
   Values here are architectural operands after instruction-field decoding.
   An immediate arithmetic operand is already sign-extended. x and y denote
   r2 and r3 except for shl, whose decoder stores its count in source1. -/
namespace SDM

def ADD (x y : Word) : Word := x + y                         -- 3:14
def ADD_ONE (x y : Word) : Word := x + y + 1
def ADDS (y imm : Word) : Word := imm + y
def ADDL := ADDS

def ADDP4 (x y : Word) : Word :=                             -- 3:15
  (0#1 ++ y.extractLsb' 30 2 ++ 0#29 ++ (x + y).extractLsb' 0 32)
def ADDP4_IMM (y imm : Word) : Word := ADDP4 imm y

def AND (x y : Word) : Word := x &&& y                       -- 3:18
def ANDCM (x y : Word) : Word := x &&& ~~~y                  -- 3:19
def AND_IMM (y imm : Word) : Word := AND imm y
def ANDCM_IMM (y imm : Word) : Word := ANDCM imm y

def OR (x y : Word) : Word := x ||| y                        -- 3:194
def OR_IMM (y imm : Word) : Word := OR imm y
def XOR (x y : Word) : Word := x ^^^ y                       -- 3:279
def XOR_IMM (y imm : Word) : Word := XOR imm y

def SUB (x y : Word) : Word := x - y                         -- 3:256
def SUB_ONE (x y : Word) : Word := x - y - 1
def SUB_IMM (y imm : Word) : Word := SUB imm y

def SHL (count value : Word) : Word := value <<< count       -- 3:244
def SHL_IMM (value count : Word) : Word := SHL count value
def SHLADD (x y count : Word) : Word := (x <<< count) + y     -- 3:245
def SHLADDP4 (x y count : Word) : Word := ADDP4 (x <<< count) y -- 3:246
def SHR (count value : Word) : Word :=                       -- 3:247
  value.sshiftRight' (if 63#64 < count then 63 else count)
def SHRU (count value : Word) : Word := value >>> count
def SHR_IMM (value count : Word) : Word := SHR count value
def SHRU_IMM (value count : Word) : Word := SHRU count value
def SHRP_IMM (x y count : Word) : Word :=                     -- 3:248
  (((x ++ y) >>> count.zeroExtend 128).extractLsb' 0 64)

def SXT1 (y : Word) : Word := (y.extractLsb' 0 8).signExtend 64 -- 3:258
def SXT2 (y : Word) : Word := (y.extractLsb' 0 16).signExtend 64
def SXT4 (y : Word) : Word := (y.extractLsb' 0 32).signExtend 64
def ZXT1 (y : Word) : Word := (y.extractLsb' 0 8).zeroExtend 64 -- 3:280
def ZXT2 (y : Word) : Word := (y.extractLsb' 0 16).zeroExtend 64
def ZXT4 (y : Word) : Word := (y.extractLsb' 0 32).zeroExtend 64

def MOVL (imm : Word) : Word := imm                           -- 3:187

def MPY4 (x y : Word) : Word :=                               -- 3:188
  (x.extractLsb' 0 32).zeroExtend 64 * (y.extractLsb' 0 32).zeroExtend 64
def MPYSHL4 (x y : Word) : Word :=                            -- 3:189
  ((x.extractLsb' 32 32).zeroExtend 64 * (y.extractLsb' 0 32).zeroExtend 64) <<< 32

def countOnes (y : Word) : Nat :=                              -- 3:216
  (List.range 64).countP y.getLsbD
def countLeadingZeros (y : Word) : Nat :=                     -- 3:38
  ((List.range 64).reverse.takeWhile (fun i => !y.getLsbD i)).length
def POPCNT (y : Word) : Word := BitVec.ofNat 64 (countOnes y)
def CLZ (y : Word) : Word := BitVec.ofNat 64 (countLeadingZeros y)

def lowField (length : Word) : Word := ~~~0#64 >>> (64#64 - length)

def DEP (x y pos len : Word) : Word :=                       -- 3:51–52
  let mask := (lowField len) <<< pos
  (y &&& ~~~mask) ||| ((x <<< pos) &&& mask)
def DEP_IMM (y pos len fill : Word) : Word :=
  DEP (if fill = 0 then 0 else ~~~0#64) y pos len
def DEPZ (x pos len : Word) : Word := DEP x 0 pos len
def DEPZ_IMM (pos len raw : Word) : Word :=
  DEPZ ((raw.extractLsb' 0 8).signExtend 64) pos len

def fieldShift (pos len : Word) : Word :=                    -- 3:54
  if pos + len > 64#64 then pos else 64#64 - len
def EXTRU (y pos len : Word) : Word :=
  (y >>> pos) &&& lowField (64#64 - fieldShift pos len)
def EXTR (y pos len : Word) : Word :=
  ((y >>> pos) <<< fieldShift pos len).sshiftRight' (fieldShift pos len)

/- All of these instructions qualify their register writes with PR[qp].
   NaT propagation in the above register forms is Boolean disjunction of
   the source NaTs. Each immediate operand supplies a clear NaT. -/
def qualifiedWrite (qp : Bool) (old new : α) : α :=
  if qp then new else old

def sourceNaT (readsR2 readsR3 r2NaT r3NaT : Bool) : Bool :=
  (readsR2 && r2NaT) || (readsR3 && r3NaT)

def staticNaT (natWord register : Word) : Bool :=
  register != 0 && ((natWord >>> register) &&& 1#64) != 0

def natBit (bit : Bool) : Word := if bit then 1 else 0

def replaceNaT (natWord dst : Word) (new : Bool) : Word :=
  if new then natWord ||| (1#64 <<< dst) else natWord &&& ~~~(1#64 <<< dst)

def validNatFact (fact : Word) (nat : Bool) : Prop :=
  fact = ~~~0#64 ∨ fact = natBit nat

-- Cmp/cmp4, vol. 3, Tables 2-15–2-18. The callers here use a true
-- qualifying predicate, distinct predicate destinations and a valid form.
inductive CompareType where
  | normal
  | parallelAnd
  | parallelOr
  | parallelOrAndcm

def comparePredicateUpdate (form : CompareType) (relation sourceNaT p1 p2 : Bool) :
    Bool × Bool :=
  match form with
  | .normal => if sourceNaT then (false, false) else (relation, !relation)
  | .parallelAnd => if sourceNaT || !relation then (false, false) else (p1, p2)
  | .parallelOr => if !sourceNaT && relation then (true, true) else (p1, p2)
  | .parallelOrAndcm => if !sourceNaT && relation then (true, false) else (p1, p2)

def compareWrites (form : CompareType) (relation sourceNaT p1 p2 : Bool)
    (i1 i2 : Word) : Word × Word :=
  let (new1, new2) := comparePredicateUpdate form relation sourceNaT p1 p2
  (if i1 = 0 then natBit p1 else natBit new1,
   if i2 = 0 then natBit p2 else natBit new2)

def cmpEq (x y : BitVec width) : Bool := x == y
def cmpNe (x y : BitVec width) : Bool := x != y
def cmpLt (x y : BitVec width) : Bool := x.slt y
def cmpLe (x y : BitVec width) : Bool := x.sle y
def cmpGt (x y : BitVec width) : Bool := y.slt x
def cmpGe (x y : BitVec width) : Bool := y.sle x
def cmpLtu (x y : BitVec width) : Bool := x < y
def cmpLeu (x y : BitVec width) : Bool := x ≤ y
def cmpGtu (x y : BitVec width) : Bool := y < x
def cmpGeu (x y : BitVec width) : Bool := y ≤ x

end SDM

/- Interpret the TCG operators immediately after translator emission.
   Standard TCG arithmetic and bit operators work on whole 64-bit words. -/
namespace TCG

def conditionEQ (x y : Word) : Bool := x == y
def conditionNE (x y : Word) : Bool := x != y
def conditionGTU (x y : Word) : Bool := y < x
def conditionTSTNE (x y : Word) : Bool := (x &&& y) != 0
def select (condition : Bool) (yes no : Word) : Word :=
  if condition then yes else no

theorem fstIf (test : Prop) [Decidable test] (a b : α × β) :
    (if test then a else b).1 = (if test then a.1 else b.1) := by
  split <;> rfl

theorem sndIf (test : Prop) [Decidable test] (a b : α × β) :
    (if test then a else b).2 = (if test then a.2 else b.2) := by
  split <;> rfl

def deposit (base value offset width : Word) : Word :=
  let mask := ((1#64 <<< width) - 1) <<< offset
  (base &&& ~~~mask) ||| ((value <<< offset) &&& mask)

def signExtend8 (x : Word) : Word := (x.truncate 8).signExtend 64
def signExtend16 (x : Word) : Word := (x.truncate 16).signExtend 64
def signExtend32 (x : Word) : Word := (x.truncate 32).signExtend 64
def zeroExtend8 (x : Word) : Word := (x.truncate 8).zeroExtend 64
def zeroExtend16 (x : Word) : Word := (x.truncate 16).zeroExtend 64
def zeroExtend32 (x : Word) : Word := (x.truncate 32).zeroExtend 64
def populationCount (x : Word) : Word := BitVec.ofNat 64 (SDM.countOnes x)
def leadingZeros (x zeroResult : Word) : Word :=
  if x = 0 then zeroResult else BitVec.ofNat 64 (SDM.countLeadingZeros x)

end TCG
end IA64Integer
