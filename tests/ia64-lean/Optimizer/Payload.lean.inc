/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

structure PayloadRegs (Temp : Type) where
  dst : Temp
  x : Temp
  y : Temp
  tmp : Temp
  tmp2 : Temp
  low : Temp
  high : Temp
  immediate : Temp
  one : Temp
  minusOne : Temp
  small : BitVec 64 → Temp

structure PayloadTarget where
  notOpcode : Bool
  extract2 : Bool
  extract32 : Bool

end IA64Optimizer
