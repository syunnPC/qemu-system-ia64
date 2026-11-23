/- SPDX-License-Identifier: GPL-2.0-or-later -/
namespace RseSpec

abbrev RegisterCell := Word × Bool

-- A register reintroduced after deallocation has unspecified data and NaT.
def allocRegister (oldSize newSize destination reg : Nat) (pfs : Word)
    (old : RegisterCell) : Option RegisterCell :=
  if destination = reg then some (pfs, false)
  else if reg < 32 + min oldSize newSize then some old
  else none

def registerRefines (actual : RegisterCell) (expected : Option RegisterCell) : Prop :=
  ∀ value, expected = some value → actual = value

structure AlatTag where
  fp : Bool
  reg : Nat
  address : Word
  size : Nat

def alatFrameRefines (old next : Option AlatTag) : Prop :=
  next = none ∨ next = old

end RseSpec

namespace RseSpec

-- COVER preserves the current register
-- values and their NaT bits in their physical slots before moving BOF.
def coverSavedRegister (sof sor rrb : BitVec 8) (bol v : U32) (current : RegisterCell) :
    U32 × RegisterCell :=
  (virtToPhys sof sor rrb bol v, current)

end RseSpec
