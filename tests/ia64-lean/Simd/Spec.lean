/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace IA64Simd

/- Intel Itanium Architecture SDM 2.3, volume 3 (323207-001).
   Each rule applies independently to every lane of the result. -/
namespace SDM

def add (x y : BitVec lane) : BitVec lane := x + y
def sub (x y : BitVec lane) : BitVec lane := x - y
def equal (x y : BitVec lane) : BitVec lane :=
  if x = y then BitVec.allOnes lane else 0

-- Pavg (3:201–202) retains the carry and sets bit zero from the two low sum bits.
def average (x y : BitVec lane) : BitVec lane :=
  let sum := x.zeroExtend (lane + 1) + y.zeroExtend (lane + 1)
  let shifted := (sum >>> 1).truncate lane
  if sum.getLsbD 0 then shifted ||| 1 else shifted

def averageRaz (x y : BitVec lane) : BitVec lane :=
  let sum := x.zeroExtend (lane + 1) + y.zeroExtend (lane + 1)
  ((sum + 1) >>> 1).truncate lane

end SDM
end IA64Simd
