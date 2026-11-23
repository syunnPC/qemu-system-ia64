/- SPDX-License-Identifier: GPL-2.0-or-later -/
namespace FullDecodeSpec

set_option maxHeartbeats 0

inductive Relation where
  | eq | ne | lt | le | gt | ge | ltu | leu | gtu | geu

def relation (rel : Relation) (x y : BitVec n) : Bool :=
  match rel with
  | .eq => x == y
  | .ne => x != y
  | .lt => x.slt y
  | .le => x.sle y
  | .gt => y.slt x
  | .ge => y.sle x
  | .ltu => x.ult y
  | .leu => x.ule y
  | .gtu => y.ult x
  | .geu => y.ule x

structure RegisterAlias where
  major : BitVec 4
  swapSources : Bool
  swapTargets : Bool

def registerAlias (rel : Relation) : RegisterAlias :=
  match rel with
  | .eq  => ⟨14, false, false⟩
  | .ne  => ⟨14, false, true⟩
  | .lt  => ⟨12, false, false⟩
  | .le  => ⟨12, true,  true⟩
  | .gt  => ⟨12, true,  false⟩
  | .ge  => ⟨12, false, true⟩
  | .ltu => ⟨13, false, false⟩
  | .leu => ⟨13, true,  true⟩
  | .gtu => ⟨13, true,  false⟩
  | .geu => ⟨13, false, true⟩

def canonical (major : BitVec 4) (x y : BitVec n) : Bool :=
  if major = 12 then x.slt y else if major = 13 then x.ult y else x == y

def swap (exchange : Bool) (a b : α) : α × α :=
  if exchange then (b, a) else (a, b)

def registerResult (rel : Relation) (a b : BitVec n) : Bool × Bool :=
  let alias := registerAlias rel
  let (x, y) := swap alias.swapSources a b
  let value := canonical alias.major x y
  swap alias.swapTargets value (!value)

private theorem swap_complement (exchange value : Bool) :
    (swap exchange value (!value)).2 = !(swap exchange value (!value)).1 := by
  cases exchange <;> cases value <;> rfl

theorem registerAlias64 (rel : Relation) (a b : BitVec 64) :
    (registerResult rel a b).1 = relation rel a b := by
  cases rel <;> dsimp only [registerResult, registerAlias, swap, canonical, relation] <;>
    (first | rfl | bv_decide)

theorem registerAlias32 (rel : Relation) (a b : BitVec 32) :
    (registerResult rel a b).1 = relation rel a b := by
  cases rel <;> dsimp only [registerResult, registerAlias, swap, canonical, relation] <;>
    (first | rfl | bv_decide)

theorem registerOtherTarget (rel : Relation) (a b : BitVec n) :
    (registerResult rel a b).2 = !(registerResult rel a b).1 := by
  dsimp only [registerResult]
  exact swap_complement _ _

def registerWord (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (r2 r3 : BitVec 7) : Slot :=
  let alias := registerAlias rel
  let (src2, src3) := swap alias.swapSources r2 r3
  let (dst1, dst2) := swap alias.swapTargets p1 p2
  alias.major ++ 0#1 ++ 0#1 ++ width ++ 0#1 ++ dst2 ++ src3 ++ src2 ++ unc ++ dst1 ++ qp

theorem registerWordOperands (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (r2 r3 : BitVec 7) :
    let alias := registerAlias rel
    let (src2, src3) := swap alias.swapSources r2 r3
    let (dst1, dst2) := swap alias.swapTargets p1 p2
    let form := Form.cmp .a6 alias.major (width.zeroExtend 2) 0 none
    let raw := registerWord rel width unc qp p1 p2 r2 r3
    aEncoding form raw ∧ reg raw 0 6 = qp.zeroExtend 8 ∧
      aR2 form raw = src2.zeroExtend 8 ∧ aR3 form raw = src3.zeroExtend 8 ∧
      aPred form raw 6 = dst1.zeroExtend 8 ∧ aPred form raw 27 = dst2.zeroExtend 8 ∧
      aUnc form raw = unc := by
  cases rel <;>
    dsimp only [registerWord, registerAlias, swap, aEncoding, compareEncoding,
               aR2, aR3, aPred, aUnc, reg] <;> bv_decide

def immediateAlias (rel : Relation) : Bool × Bool :=
  match rel with
  | .eq | .lt | .ltu => (false, false)
  | .ne | .ge | .geu => (false, true)
  | .le | .leu => (true, false)
  | .gt | .gtu => (true, true)

-- Base is the decoded, sign-extended imm8. The added one models the
-- written assembler operand for LE and GT, valid also at the unsigned
-- wrap point where the written immediate is 2^n.
def immediateResult (rel : Relation) (base y : BitVec n) : Bool × Bool :=
  let value := canonical (registerAlias rel).major base y
  swap (immediateAlias rel).2 value (!value)

def immediateReference (rel : Relation) (base y : BitVec n) : Bool :=
  match rel with
  | .le => (base + 1).sle y
  | .gt => y.slt (base + 1)
  | .leu => base != BitVec.allOnes n && (base + 1).ule y
  | .gtu => base == BitVec.allOnes n || y.ult (base + 1)
  | _ => relation rel base y

theorem immediateAlias64 (rel : Relation) (imm : BitVec 8) (y : BitVec 64) :
    (immediateResult rel (imm.signExtend 64) y).1 = immediateReference rel (imm.signExtend 64) y := by
  cases rel <;> dsimp only [immediateResult, immediateReference, registerAlias,
                            immediateAlias, canonical, swap, relation] <;>
    (first | rfl | bv_decide)

theorem immediateAlias32 (rel : Relation) (imm : BitVec 8) (y : BitVec 32) :
    (immediateResult rel (imm.signExtend 32) y).1 = immediateReference rel (imm.signExtend 32) y := by
  cases rel <;> dsimp only [immediateResult, immediateReference, registerAlias,
                            immediateAlias, canonical, swap, relation] <;>
    (first | rfl | bv_decide)

theorem immediateOtherTarget (rel : Relation) (base y : BitVec n) :
    (immediateResult rel base y).2 = !(immediateResult rel base y).1 :=
  swap_complement _ _

def immediateWord (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (base : BitVec 8) (r3 : BitVec 7) : Slot :=
  let (dst1, dst2) := swap (immediateAlias rel).2 p1 p2
  (registerAlias rel).major ++ base.extractLsb' 7 1 ++ 1#1 ++ width ++ 0#1 ++
    dst2 ++ r3 ++ base.extractLsb' 0 7 ++ unc ++ dst1 ++ qp

theorem immediateWordOperands (rel : Relation) (width unc : BitVec 1) (qp p1 p2 : BitVec 6)
    (base : BitVec 8) (r3 : BitVec 7) :
    let (dst1, dst2) := swap (immediateAlias rel).2 p1 p2
    let form := Form.cmp .a8 (registerAlias rel).major (1#1 ++ width) 0 none
    let raw := immediateWord rel width unc qp p1 p2 base r3
    aEncoding form raw ∧ reg raw 0 6 = qp.zeroExtend 8 ∧ aImm form raw = base.signExtend 64 ∧
      aR3 form raw = r3.zeroExtend 8 ∧ aPred form raw 6 = dst1.zeroExtend 8 ∧
      aPred form raw 27 = dst2.zeroExtend 8 ∧ aUnc form raw = unc := by
  cases rel <;>
    dsimp only [immediateWord, registerAlias, immediateAlias, swap, aEncoding,
               compareEncoding, aImm, imm8, aR3, aPred, aUnc, reg] <;> bv_decide

inductive Inequality where
  | lt | le | gt | ge

def reverseInequality (op : Inequality) : Inequality :=
  match op with
  | .lt => .gt | .le => .ge | .gt => .lt | .ge => .le

def inequality (op : Inequality) (x y : BitVec n) : Bool :=
  match op with
  | .lt => x.slt y | .le => x.sle y | .gt => y.slt x | .ge => y.sle x

theorem parallelReverse (op : Inequality) (a b : BitVec n) :
    inequality op a b = inequality (reverseInequality op) b a := by
  cases op <;> rfl

-- Complementing a condition keeps each update action.
-- the and.orcm alias also exchanges its predicate destinations.
inductive ParallelUpdate where
  | and | or | orAndcm

def parallelMajor (update : ParallelUpdate) : BitVec 4 :=
  match update with | .and => 12 | .or => 13 | .orAndcm => 14

def inequalityCode (op : Inequality) : BitVec 1 × BitVec 1 :=
  match op with
  | .lt => (1, 1) | .le => (0, 1) | .gt => (0, 0) | .ge => (1, 0)

def parallelWord (op : Inequality) (update : ParallelUpdate) (zeroOnRight : Bool)
    (width : BitVec 1) (qp p1 p2 : BitVec 6) (r : BitVec 7) : Slot :=
  let encoded := if zeroOnRight then reverseInequality op else op
  let (ta, c) := inequalityCode encoded
  parallelMajor update ++ 1#1 ++ 0#1 ++ width ++ ta ++ p2 ++ r ++ 0#7 ++ c ++ p1 ++ qp

theorem parallelWordOperands (op : Inequality) (update : ParallelUpdate) (zeroOnRight : Bool)
    (width : BitVec 1) (qp p1 p2 : BitVec 6) (r : BitVec 7) :
    let encoded := if zeroOnRight then reverseInequality op else op
    let (ta, c) := inequalityCode encoded
    let form := Form.cmp .a7 (parallelMajor update) (width.zeroExtend 2) ta (some c)
    let raw := parallelWord op update zeroOnRight width qp p1 p2 r
    aEncoding form raw ∧ reg raw 0 6 = qp.zeroExtend 8 ∧ aR2 form raw = 0 ∧
      aR3 form raw = r.zeroExtend 8 ∧ aPred form raw 6 = p1.zeroExtend 8 ∧
      aPred form raw 27 = p2.zeroExtend 8 ∧ aUnc form raw = 0 := by
  dsimp only [parallelWord, aEncoding, compareEncoding, aR2, aR3, aPred, aUnc, reg]
  generalize parallelMajor update = major
  generalize inequalityCode (if zeroOnRight = true then reverseInequality op else op) = code
  rcases code with ⟨ta, c⟩
  dsimp only
  bv_decide

def updates (op : ParallelUpdate) (result : Bool) : Option Bool × Option Bool :=
  match op with
  | .and => if result then (none, none) else (some false, some false)
  | .or => if result then (some true, some true) else (none, none)
  | .orAndcm => if result then (some true, some false) else (none, none)

def complementUpdates (op : ParallelUpdate) (result : Bool) : Option Bool × Option Bool :=
  match op with
  | .and => if result then (some false, some false) else (none, none)
  | .or => if result then (none, none) else (some true, some true)
  | .orAndcm => if result then (none, none) else (some false, some true)

theorem parallelComplement (op : ParallelUpdate) (result : Bool) :
    complementUpdates op result = swap (match op with | .orAndcm => true | _ => false)
      (updates op (!result)).1 (updates op (!result)).2 := by
  cases op <;> cases result <;> rfl

end FullDecodeSpec
