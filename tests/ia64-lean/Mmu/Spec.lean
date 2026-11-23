/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

set_option warningAsError true
set_option maxRecDepth 4096

namespace IA64Mmu

-- Intel Itanium SDM 2.3 Vol. 2, Table 4-4. Rows within each access
-- rights value are PL 0 to 3, and each row is CPL 0 to 3. The low bits
-- encode readable, writable, and executable, in that order.
def permissions : Array (Array (Array Nat)) := #[
  #[#[1,0,0,0], #[1,1,0,0], #[1,1,1,0], #[1,1,1,1]],
  #[#[5,0,0,0], #[5,5,0,0], #[5,5,5,0], #[5,5,5,5]],
  #[#[3,0,0,0], #[3,3,0,0], #[3,3,3,0], #[3,3,3,3]],
  #[#[7,0,0,0], #[7,7,0,0], #[7,7,7,0], #[7,7,7,7]],
  #[#[3,0,0,0], #[3,1,0,0], #[3,3,1,0], #[3,3,3,1]],
  #[#[7,0,0,0], #[7,5,0,0], #[7,5,5,0], #[7,5,5,5]],
  #[#[3,0,0,0], #[3,7,0,0], #[3,3,7,0], #[3,3,3,7]],
  #[#[5,4,4,4], #[5,4,4,4], #[5,4,4,4], #[5,4,4,4]]
]

def expectedRights : Array Nat := Id.run do
  let mut answers := #[]
  for ar in [:8] do
    for pl in [:4] do
      for cpl in [:4] do
        for present in [:2] do
          let perm := if present == 1 then permissions[ar]![pl]![cpl]! else 0
          answers := answers.push perm |>.push perm
  return answers

-- Vol. 3 probe: an explicit privilege operand can request a less
-- privileged check. It cannot gain permissions of a more privileged ring.
def expectedProbe : Array Nat := Id.run do
  let mut answers := #[]
  for current in [:4] do
    for requested in [:16] do
      answers := answers.push (max current (requested % 4))
  return answers

-- Vol. 2 §4.1.1.7: page alignment chooses the translated frame number
-- from the PTE and preserves the page offset from the virtual address.
def low (value : BitVec 64) (width : Nat) : BitVec 64 :=
  (value <<< (64 - width)) >>> (64 - width)

def frameOffset (frame va : BitVec 64) (logBytes : Nat) : BitVec 64 :=
  ((frame >>> logBytes) <<< logBytes) ||| low va logBytes

-- On a Merced lookup candidate, a validated hint can return a slot.
-- Invalid hints leave the tag alone. A current tag with a stale backing
-- entry is invalidated and misses. The rest of replacement is out of scope.
def expectedFastTags : Array Nat := Id.run do
  let mut answers := #[]
  for flags in [:256] do
    let tagValid := flags.testBit 0
    let tagMatched := tagValid && flags.testBit 1 && flags.testBit 2 && !flags.testBit 3
    let entryMatched := flags.testBit 4 && !flags.testBit 5 &&
      !flags.testBit 6 && !flags.testBit 7
    answers := answers.push (if tagMatched && entryMatched then 1 else 0)
      |>.push (if tagValid && (!tagMatched || entryMatched) then 1 else 0)
  return answers

-- Vol. 2, Figure 4-14. The 8-byte entry offset occupies the low table
-- bits, while the high region bits come from the queried virtual address.
def shortAddress (pta va hpn : BitVec 64) (logSize : Nat) : BitVec 64 :=
  ((va >>> 61) <<< 61) ||| low ((pta >>> logSize) <<< logSize) 61 |||
    low (hpn * 8#64) logSize

end IA64Mmu
