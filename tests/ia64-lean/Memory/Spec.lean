/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

set_option warningAsError true

namespace MemorySpec

-- The low three bits of the QEMU memory descriptor encode log2(bytes).
def descriptorBytes (descriptor : BitVec 32) : BitVec 32 :=
  1#32 <<< (0#29 ++ descriptor.extractLsb' 0 3)

-- Intel SDM Vol. 2 §3.3.2 and Vol. 3 §3 mem_read use PSR.be for data.
def dataBigEndian (psr : BitVec 64) : Bool :=
  psr.getLsbD 1

-- The bytes in a multi-byte datum run in host order exactly when the
-- CPU and host have the same endian setting. A byte has no endian order.
-- Every other descriptor bit remains fixed.
def dataDescriptor (descriptor : BitVec 32) (hostBig guestBig : Bool) : BitVec 32 :=
  let swap := descriptorBytes descriptor != 1#32 && hostBig != guestBig
  descriptor &&& 0xffffffef#32 |||
    (if swap then 16#32 else 0#32)

-- Intel SDM Vol. 3 Table 2-19 and §3 mem_xchg_cond compare zero-extended
-- memory against the full 64-bit CCV. This projection models only the
-- comparison, before memory ordering, alignment or fault side effects.
def cmpxchgPossible (logBytes : Nat) (ccv : BitVec 64) : Prop :=
  ∃ value : BitVec 64, value.toNat < 2 ^ (8 * 2 ^ logBytes) ∧ value = ccv

end MemorySpec
