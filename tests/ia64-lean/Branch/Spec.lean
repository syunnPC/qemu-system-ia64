/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Branch

local notation "Word" => BitVec 64

-- Intel Itanium SDM 2.3 vol. 3 (323207-001), br pp. 3:20–28,
-- brl pp. 3:30–32, and the B1/B2/B3 and X3/X4 encoding tables.
namespace SDM

def shortDisp (raw : Word) : Word :=
  (raw.extractLsb' 36 1 ++ raw.extractLsb' 13 20).signExtend 64 <<< 4

def longDisp (l x : Word) : Word :=
  ((x.extractLsb' 36 1 ++ l.extractLsb' 2 39 ++ x.extractLsb' 13 20)
    ++ 0#4)

def directTarget (ip displacement : Word) : Word := (ip + displacement) &&& ~~~15#64
def indirectTarget (br : Word) : Word := br &&& ~~~15#64

def nextCount (lc : Word) : Bool × Word :=
  if lc = 0 then (false, lc) else (true, lc - 1)

def cloop (lc : Nat) : Bool × Nat :=
  if lc = 0 then (false, 0) else (true, lc - 1)

-- The starting point is the branch immediately after the current loop-body
-- store. Each further store is reached only after a taken branch.
def afterTakenStores : Nat → Nat → Nat
  | 0, lc => lc
  | count + 1, lc => afterTakenStores count (cloop lc).2

theorem repeatedCount (count lc : Nat) (enough : count ≤ lc) :
    afterTakenStores count lc = lc - count := by
  induction count generalizing lc with
  | zero => rfl
  | succ count ih =>
      have nonzero : lc ≠ 0 := by omega
      simp only [afterTakenStores, cloop, if_neg nonzero]
      rw [ih (lc - 1) (by omega)]
      omega

theorem eachPriorBranchTaken (count lc : Nat) (fits : count < lc) :
    (cloop (afterTakenStores count lc)).1 = true := by
  rw [repeatedCount count lc (by omega)]
  have nonzero : lc - count ≠ 0 := by omega
  simp [cloop, nonzero]

def countedPipeline (exit : Bool) (lc ec : Word) : Bool × Word × Word × Bool × Bool :=
  let kernel := lc != 0
  let continueKernel := kernel || 1#64 < ec
  (if exit then !continueKernel else continueKernel,
   if kernel then lc - 1 else lc,
   if !kernel && ec != 0 then ec - 1 else ec,
   kernel, kernel || ec != 0)

def whilePipeline (exit qp : Bool) (ec : Word) : Bool × Word × Bool × Bool :=
  let continueKernel := qp || 1#64 < ec
  (if exit then !continueKernel else continueKernel,
   if !qp && ec != 0 then ec - 1 else ec,
   false, qp || ec != 0)

end SDM
end IA64Branch
