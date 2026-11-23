/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Std.Tactic.BVDecide

namespace RseSpec

abbrev Word := BitVec 64
abbrev U32 := BitVec 32

-- Index 63 is the aligned RNAT slot for the entire 512-byte region.
def collectBit (addr : Word) : U32 := (addr.extractLsb' 3 6).zeroExtend 32

def collectWord (addr : Word) : Word :=
  addr.extractLsb' 9 55 ++ (63 : BitVec 6) ++ (0 : BitVec 3)

def definedNaTBits (index : U32) : Word :=
  (0 : BitVec 1) ++ ~~~((~~~(0 : BitVec 63)) <<<
    (if 62 ≤ index then 63 else (index + 1).zeroExtend 63))

def natCount (slots : U32) : U32 := slots / 63

def natGrow (addr : Word) (registers : U32) : U32 :=
  natCount (collectBit addr + registers)

def natShrink (addr : Word) (registers : U32) : U32 :=
  natCount (62 - collectBit addr + registers)

def advance (addr : Word) (registers : U32) : Word :=
  let words := (registers.zeroExtend 64) + (natGrow addr registers).zeroExtend 64
  ((addr.extractLsb' 3 61) + words.zeroExtend 61) ++ (0 : BitVec 3)

def retreat (addr : Word) (registers : U32) : Word :=
  let words := (registers.zeroExtend 64) + (natShrink addr registers).zeroExtend 64
  ((addr.extractLsb' 3 61) - words.zeroExtend 61) ++ (0 : BitVec 3)

-- There are floor((words + 63 - phase) / 64) RNAT slots below the
-- pointer. Each is the final word of its collection.
def payloadWordsBelow (addr : Word) (words : U32) : U32 :=
  let phase := (collectBit addr).zeroExtend 64
  let collectionCount := (words.zeroExtend 64 + (63 - phase)) / 64
  words - collectionCount.zeroExtend 32

def frameMarker (sof sol : BitVec 7) (sor : BitVec 4) (gr fr : BitVec 7)
    (pr : BitVec 6) : Word :=
  (pr ++ fr ++ gr ++ sor ++ sol ++ sof).zeroExtend 64

def rotateCount (sof sor : BitVec 8) : U32 :=
  let size := (sor ++ (0 : BitVec 3)).zeroExtend 32
  if size ≤ sof.zeroExtend 32 ∧ size ≤ 96 then size else 0

def virtToPhys (sof sor rebasing : BitVec 8) (bol v : U32) : U32 :=
  let size := rotateCount sof sor
  let offset := if v < size then (v + rebasing.zeroExtend 32) % size else v
  (bol + offset) % 96

def physToVirt (sof sor rebasing : BitVec 8) (bol p : U32) : U32 :=
  let size := rotateCount sof sor
  let offset := (p + 96 - bol) % 96
  if offset < size then (offset + size - rebasing.zeroExtend 32) % size else offset

def wrapPhys (index : Int) : Int := index % 96

-- Moving up one logical register wraps the last bit of the rotating
-- interval to its first position.
def rotatePrefix (bits : BitVec 128) (count : U32) : BitVec 128 :=
  let width := count.zeroExtend 128
  let lowBits := ((1 : BitVec 128) <<< width) - 1
  let rotated := (bits <<< 1) ||| ((bits >>> (width - 1)) &&& 1)
  (bits &&& ~~~lowBits) ||| (rotated &&& lowBits)

end RseSpec

namespace RseSpec

structure Frame where
  sof : BitVec 7
  sol : BitVec 7
  sor : BitVec 4
  gr : BitVec 7
  fr : BitVec 7
  pr : BitVec 6
  deriving DecidableEq

structure FrameState where
  cfm : Frame
  psr : Word
  ifs : Word
  pfs : Word
  bsp : Word
  bspstore : Word
  rnat : Word
  bol : Int
  invalid : Int
  clean : Int
  dirty : Int
  cleanNat : U32
  dirtyNat : U32

def Frame.valid (f : Frame) : Prop :=
  f.sof.toNat ≤ 96 ∧ f.sol.toNat ≤ f.sof.toNat ∧ f.sor.toNat * 8 ≤ f.sof.toNat

-- A complete resident frame has no outstanding loads and all four partitions
-- fit the physical file. The backing-store address names a payload slot.
def FrameState.complete (s : FrameState) : Prop :=
  s.cfm.valid ∧
  0 ≤ s.invalid ∧ 0 ≤ s.clean ∧ 0 ≤ s.dirty ∧
  (s.cfm.sof.toNat : Int) + s.invalid + s.clean + s.dirty = 96 ∧
  s.cleanNat ≤ 2 ∧ s.dirtyNat ≤ 2 ∧
  s.bsp &&& 7 = 0 ∧ collectBit s.bsp < 63 ∧
  0 ≤ s.bol ∧ s.bol < 96

def allocSite (qp : BitVec 6) (slot : BitVec 2) (startsGroup stopAfterSlot0 : Bool) : Prop :=
  qp = 0 ∧ startsGroup = true ∧ (slot = 0 ∨ slot = 1 ∧ stopAfterSlot0 = true)

def coverSite (s : FrameState) (hasVirtualization endsGroup : Bool) : Prop :=
  endsGroup = true ∧
  (hasVirtualization = false ∨ s.psr.extractLsb' 32 2 ≠ 0 ∨ s.psr.getLsbD 46 = false)

-- A resident resize consumes invalid slots before
-- reclaiming clean slots. It leaves BOF, BSP, BSPSTORE and RNAT unchanged.
def allocResident (s : FrameState) (sof sol : BitVec 7) (sor : BitVec 4) : FrameState :=
  let growth : Int := (sof.toNat : Int) - s.cfm.sof.toNat
  let consumedClean := max 0 (growth - s.invalid)
  let clean := s.clean - consumedClean
  { s with
    cfm := { s.cfm with sof := sof, sol := sol, sor := sor }
    invalid := max 0 (s.invalid - growth)
    clean := clean
    cleanNat := if consumedClean = 0 then s.cleanNat else
      natShrink s.bsp (BitVec.ofInt 32 (clean + s.dirty + 1)) - s.dirtyNat }

def allocDestination (s : FrameState) : Word × Bool := (s.pfs, false)

-- Moving the whole frame into the dirty partition crosses RNAT slots without
-- changing the backing-store flush pointer or its current RNAT accumulator.
def coverFrame (s : FrameState) : FrameState :=
  let n := s.cfm.sof.zeroExtend 32
  let f := s.cfm
  { s with
    cfm := ⟨0, 0, 0, 0, 0, 0⟩
    ifs := if s.psr.getLsbD 13 then s.ifs else
      frameMarker f.sof f.sol f.sor f.gr f.fr f.pr ||| ((1 : Word) <<< 63)
    bsp := advance s.bsp n
    bol := wrapPhys (s.bol + (n.toNat : Int))
    dirty := s.dirty + (n.toNat : Int)
    dirtyNat := s.dirtyNat + natGrow s.bsp n }

end RseSpec
