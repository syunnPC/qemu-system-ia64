/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

inductive Extra where
  | complement | low (count : BitVec 6) | shl | shr

def Extra.expression : Extra → Expression
  | .complement => .complementFirst
  | .low count => .extractFirst (count.zeroExtend 64)
  | .shl => .shiftLeft
  | .shr => .shiftRight

end IA64Optimizer
