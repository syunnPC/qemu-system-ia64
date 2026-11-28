/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace RseSpec

structure RnatBits where
  value : Word
  defined : Word

structure SpillWrite where
  address : Word
  value : Word
  bigEndian : Bool
  deriving DecidableEq

def spillByteSwap (word : Word) : Word :=
  word.extractLsb' 0 8 ++ word.extractLsb' 8 8 ++ word.extractLsb' 16 8 ++ word.extractLsb' 24 8 ++
  word.extractLsb' 32 8 ++ word.extractLsb' 40 8 ++ word.extractLsb' 48 8 ++ word.extractLsb' 56 8

def spillRamWord (value : Word) (bigEndian : Bool) : Word :=
  if bigEndian then spillByteSwap value else value

def SpillWrite.bytes (write : SpillWrite) : List (BitVec 8) :=
  let word := spillRamWord write.value write.bigEndian
  (List.range 8).map (fun i => word.extractLsb' (8 * i) 8)

structure SpillRamState where
  counters : SpillCounters
  latch : RnatBits
  writeback : Option (Word × RnatBits)
  ram : Word → Word

structure SpillRamInput where
  bol : Int
  physical : Int → RegisterCell
  hasClean : Bool
  rsc : Word
  storeAllowed : Word → Bool
  interruptAfter : Word → Bool

def SpillRamInput.bigEndian (i : SpillRamInput) : Bool := i.rsc.getLsbD 4

structure SpillRamHistory where
  boundCollection : Option Word
  fillCollection : Option Word
  shadowed : Word → Bool
  writableRam : Word → Bool
  physicalAddr : Word → Word
  readImage : Word → Word
  storeImage : Word → Word
  deniedBytes : Word → Nat

def spillAddress (s : SpillRamState) (offset : Nat) : Word :=
  s.counters.bspstore + BitVec.ofNat 64 (8 * offset)

-- RAM words pack address-ordered bytes from low to high significance.
-- The history covers the issuing helper's accesses. Partial RNAT stores
-- read an ordinary RAM image that stays stable until the store.
def SpillRamHistory.valid (h : SpillRamHistory) (s : SpillRamState)
    (i : SpillRamInput) (fuel : Nat) : Prop :=
  (h.boundCollection ≠ some (collectWord s.counters.bspstore) →
    s.latch.value = 0 ∧ s.latch.defined = 0) ∧
  (∀ offset : Nat, offset < fuel →
    let addr := spillAddress s offset
    (i.storeAllowed addr = true → h.writableRam addr = true) ∧
    (i.storeAllowed addr = false → h.deniedBytes addr = 0) ∧
    h.readImage addr = s.ram addr ∧ h.storeImage addr = h.readImage addr ∧
    (h.boundCollection ≠ some (collectWord addr) →
      h.fillCollection ≠ some (collectWord addr) ∧ h.shadowed (collectWord addr) = false)) ∧
  (∀ left right : Nat, left < fuel → right < fuel → left ≠ right →
    h.physicalAddr (spillAddress s left) ≠ h.physicalAddr (spillAddress s right))

inductive SpillAttempt where
  | committed (kind : SpillWord) (write : SpillWrite) (state : SpillRamState)
  | fault (address : Word)

inductive SpillStop where
  | retired
  | fault (address : Word)
  | interrupt (address : Word)
  | fuelExhausted
  deriving DecidableEq

structure SpillTrace where
  writes : List SpillWrite
  stop : SpillStop
  state : SpillRamState

def spillRamStart (s : SpillRamState) (i : SpillRamInput) (needed : Nat) : Prop :=
  s.counters.bspstore &&& 7 = 0 ∧
  0 ≤ i.bol ∧ i.bol < 96 ∧
  0 < needed ∧ (needed : Int) ≤ s.counters.dirty ∧ s.counters.dirty ≤ 96 ∧
  0 ≤ s.counters.dirtyNat ∧ s.counters.dirtyNat ≤ 2 ∧
  0 ≤ s.counters.clean ∧ 0 ≤ s.counters.invalid

def savedAt (s : SpillRamState) (addr : Word) : RnatBits :=
  match s.writeback with
  | some (tag, data) => if tag = addr then data else ⟨0, 0⟩
  | none => ⟨0, 0⟩

def consumeWriteback (s : SpillRamState) (addr : Word) : Option (Word × RnatBits) :=
  s.writeback.filter (fun image => image.1 != addr)

-- Bit 63 is reserved. Other bits are selected by the newest defined source.
def collectionImage (spill saved : RnatBits) (old : Word) : Word :=
  let lower : BitVec 63 :=
    ((spill.value &&& spill.defined) |||
      (saved.value &&& saved.defined &&& ~~~spill.defined) |||
      (old &&& ~~~(spill.defined ||| saved.defined))).extractLsb' 0 63
  (0 : BitVec 1) ++ lower

def replaceRnatBit (latch : RnatBits) (n : U32) (tag : Bool) : RnatBits :=
  let bit := (1 : Word) <<< n.toNat
  { value := (latch.value &&& ~~~bit) ||| (if tag then bit else 0)
    defined := latch.defined ||| bit }

def commitRamWord (s : SpillRamState) (target : Word) (value : Word) : Word → Word :=
  fun addr => if addr = target then value else s.ram addr

def consecutiveSpillWrites : Word → List SpillWrite → Prop
  | _, [] => True
  | addr, write :: rest => write.address = addr ∧ consecutiveSpillWrites (addr + 8) rest

def spillWriteImage (ram : Word → Word) (write : SpillWrite) : Word → Word :=
  fun addr => if addr = write.address then spillRamWord write.value write.bigEndian else ram addr

def applySpillWrites (ram : Word → Word) (writes : List SpillWrite) : Word → Word :=
  writes.foldl spillWriteImage ram

def spillRamAttempt (i : SpillRamInput) (s : SpillRamState) : SpillAttempt :=
  let addr := s.counters.bspstore
  let bit := collectBit addr
  if i.storeAllowed addr = false then .fault addr
  else if bit = 63 then
    let value := collectionImage s.latch (savedAt s addr) (spillRamWord (s.ram addr) i.bigEndian)
    .committed .collection ⟨addr, value, i.bigEndian⟩
      { counters := spillPrefix s.counters i.hasClean [.collection]
        latch := ⟨0, 0⟩
        writeback := consumeWriteback s addr
        ram := commitRamWord s addr (spillRamWord value i.bigEndian) }
  else
    let (value, tag) := i.physical (wrapPhys (i.bol - s.counters.dirty))
    .committed .payload ⟨addr, value, i.bigEndian⟩
      { s with
        counters := spillPrefix s.counters i.hasClean [.payload]
        latch := replaceRnatBit s.latch bit tag
        ram := commitRamWord s addr (spillRamWord value i.bigEndian) }

def remainingSpills (needed : Nat) (kind : SpillWord) : Nat :=
  if kind = .payload then needed - 1 else needed

def retireSpillRam (s : SpillRamState) : SpillRamState :=
  { s with counters := { s.counters with invalid := 0, clean := 0, cleanNat := 0 } }

def spillRamTrace (i : SpillRamInput) (s : SpillRamState) (needed : Nat) : Nat → SpillTrace
  | 0 =>
    if needed = 0 then ⟨[], .retired, retireSpillRam s⟩ else ⟨[], .fuelExhausted, s⟩
  | fuel + 1 =>
    if needed = 0 then ⟨[], .retired, retireSpillRam s⟩ else
    match spillRamAttempt i s with
    | .fault addr => ⟨[], .fault addr, s⟩
    | .committed kind write next =>
      if i.interruptAfter write.address then ⟨[write], .interrupt write.address, next⟩
      else
        let later := spillRamTrace i next (remainingSpills needed kind) fuel
        { later with writes := write :: later.writes }

def mandatorySpillNeed (s : FrameState) (newSof : BitVec 7) : Nat :=
  ((newSof.toNat : Int) - (s.cfm.sof.toNat + s.invalid + s.clean)).toNat

def SpillRamState.describesFrame (ram : SpillRamState) (f : FrameState)
    (i : SpillRamInput) : Prop :=
  ram.counters.bspstore = f.bspstore ∧ ram.counters.dirty = f.dirty ∧
  ram.counters.dirtyNat = f.dirtyNat.toNat ∧ ram.counters.invalid = f.invalid ∧
  ram.counters.clean = f.clean ∧ ram.counters.cleanNat = f.cleanNat.toNat ∧
  i.bol = f.bol

end RseSpec
