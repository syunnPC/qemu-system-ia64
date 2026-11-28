/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Branch.B1

open IA64Slot

structure Instruction where
  address : Word
  slot : Slot
  qualifier : BitVec 6
  displacement : Word

inductive Transfer where
  | chain (index : BitVec 8)
  | lookup
  | mainLoop
  deriving DecidableEq

inductive Continuation where
  | next (startGroup : Bool)
  | taken (faultSlot : Word) (route : Transfer)
  | helper (event : Event)

inductive Boundary where
  | next (startGroup : Bool)
  | taken (faultSlot : Word)
  | helper (event : Event)
  deriving DecidableEq

structure Result where
  state : State
  resume : Continuation

def Result.project (a : Result) : State × Boundary :=
  (a.state, match a.resume with
   | .next start => .next start
   | .taken faultSlot _ => .taken faultSlot
   | .helper event => .helper event)

structure Context where
  ia32 : Bool
  llvm : Bool
  trackSuppression : Bool
  ri : Option Word
  ic : Option Bool
  record : Bool
  predZero : Word
  predOne : Word
  debugDynamic : Bool
  debug : Bool
  nativeDynamic : Bool
  singleStep : Bool
  takenBranch : Bool
  virtual : Bool
  vaImplemented : Word → Bool
  paImplemented : Word → Bool
  stop : Bool
  chainSlot : BitVec 8
  natSafe : Bool
  natsKnownClear : Bool
  anyNat : Bool
  natClearKey : Bool
  deferIRQ : Bool
  chainDisabled : Bool
  firstIp : Word

def predValue (prs : BitVec 6 → Word) (p : BitVec 6) : Bool :=
  IA64Slot.SDM.qualified (p.zeroExtend 64) (prs p)

def short (raw : FullDecodeSpec.Slot) : Word :=
  IA64Branch.SDM.shortDisp (raw.zeroExtend 64)

def slotOf (index : BitVec 2) : Slot :=
  if index = 0 then .first else if index = 1 then .second else .third

def sdmInstruction (raw : FullDecodeSpec.Slot) (ip : Word) (index : BitVec 2) : Instruction :=
  ⟨ip, slotOf index, raw.extractLsb' 0 6, short raw⟩

def cInstruction (wire : FullDecodeSpec.Observation) : Instruction :=
  ⟨wire.extractLsb' 234 64, slotOf (wire.extractLsb' 226 2),
   wire.extractLsb' 218 6, wire.extractLsb' 82 64⟩

def sdmStop (code : BitVec 5) (index : BitVec 2) : Bool :=
  if index = 2 then code.getLsbD 0
  else if index = 1 then code.extractLsb' 1 4 == 1
  else if index = 0 then code.extractLsb' 1 4 == 5
  else false

def sdmBranchTrap (psr : Word) : Bool := psr.getLsbD 26

def implemented (c : Context) (target : Word) : Bool :=
  if c.virtual then c.vaImplemented target else c.paImplemented target

def requested (c : Context) (before : State) (taken : Bool) (dest : Word) : Bool :=
  c.nativeDynamic || IA64Slot.SDM.singleStep before.psr ||
    (taken && sdmBranchTrap before.psr) || !implemented c dest

def archSuccess (before : State) (ip : Word) : State :=
  { before with lastSuccess := IA64Slot.SDM.success before ip }

def sdmRun (c : Context) (s : State) (prs : BitVec 6 → Word) (i : Instruction) :
    State × Boundary :=
  let issue : Address := ⟨i.address, i.slot.number⟩
  if IA64Slot.SDM.instructionDebug s.psr then (s, .helper (.debug issue))
  else
    let completed := archSuccess s i.address
    if predValue prs i.qualifier then
      let target := IA64Branch.SDM.directTarget i.address i.displacement
      if requested c s true target then
        (completed, .helper (.native ⟨⟨target, 0⟩, issue, 4 * i.slot.number, 1, 0, s.psr⟩))
      else
        ({ completed with ip := target, psr := IA64Slot.SDM.withRI s.psr 0,
                           groupStart := true }, .taken i.slot.number)
    else
      let next := IA64Slot.SDM.successor i.address i.slot false
      if requested c s false next.bundle then
        (completed, .helper (.native (IA64Slot.SDM.trapPayload s i.address i.slot false)))
      else
        (IA64Slot.SDM.restart completed i.address i.slot false, .next c.stop)

structure Sound (c : Context) (s : State) (prs : BitVec 6 → Word) (i : Instruction) : Prop where
  ia64 : c.ia32 = false
  nativeTCG : c.llvm = false
  ordinarySuppression : c.trackSuppression = false
  ip : s.ip = i.address
  aligned : i.address &&& 15#64 = 0
  ri : IA64Slot.SDM.ri s.psr = i.slot.number
  riFact : RISound s c.ri
  icFact : ∀ b, c.ic = some b → IA64Slot.SDM.collecting s.psr = b
  shared : c.record = false → IA64Slot.SDM.collecting s.psr = true →
    s.lastSuccess = i.address &&& ~~~15#64
  debug : c.debugDynamic = false → c.debug = IA64Slot.SDM.instructionDebug s.psr
  ss : c.singleStep = IA64Slot.SDM.singleStep s.psr
  tb : c.takenBranch = sdmBranchTrap s.psr
  zero : c.predZero.getLsbD i.qualifier.toNat = true → predValue prs i.qualifier = false
  one : c.predOne.getLsbD i.qualifier.toNat = true → predValue prs i.qualifier = true

def B1Contract
    (decoder : Word → Word → Word → BitVec 2 → FullDecodeSpec.Observation)
    (emitter : Context → State → (BitVec 6 → Word) → Instruction → Result) : Prop :=
  ∀ low high ip index c s prs,
    FullDecodeSpec.aBundleEncoding FullDecodeImplementation.reservedUnit
      FullDecodeImplementation.mUnit FullDecodeImplementation.iUnit FullDecodeImplementation.bUnit
      FullDecodeImplementation.fUnit FullDecodeImplementation.lUnit FullDecodeImplementation.xUnit
      (.branch (.relative 0)) low high index →
    let i := sdmInstruction (FullDecodeSpec.slotBits low high index) ip index
    let code := FullDecodeSpec.templateBits low
    Sound c s prs i →
    (emitter {c with stop := FullDecodeImplementation.cStop code index}
      s prs (cInstruction (decoder low high ip index))).project =
      sdmRun {c with stop := sdmStop code index} s prs i

end IA64Branch.B1
