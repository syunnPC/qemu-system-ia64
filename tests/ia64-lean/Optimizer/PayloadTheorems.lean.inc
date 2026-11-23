/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

def needsImmediate : AValue → Bool
  | .ADDS | .ADDL | .SUB_IMM | .AND_IMM | .OR_IMM | .XOR_IMM | .ANDCM_IMM
  | .SHLADD | .SHLADDP4 | .ADDP4_IMM => true
  | _ => false

def writesImmediateScratch : AValue → Bool
  | .SUB_IMM | .AND_IMM | .OR_IMM | .XOR_IMM | .ANDCM_IMM | .SHLADD | .SHLADDP4 => true
  | _ => false

def usesPointer : AValue → Bool
  | .SHLADDP4 | .ADDP4 | .ADDP4_IMM => true
  | _ => false

structure PointerInputs (s : State Temp) (r : PayloadRegs Temp) : Prop where
  source : r.y ≠ r.low
  sum : r.low ≠ r.high
  c30 : s (r.small 30) = 30
  c30Low : r.small 30 ≠ r.low
  c3 : s (r.small 3) = 3
  c3Low : r.small 3 ≠ r.low
  c3High : r.small 3 ≠ r.high
  c61 : s (r.small 61) = 61
  c61Low : r.small 61 ≠ r.low
  c61High : r.small 61 ≠ r.high
  cMask : s (r.small 4294967295) = 4294967295
  cMaskLow : r.small 4294967295 ≠ r.low

structure PayloadInputs (s : State Temp) (r : PayloadRegs Temp) (imm : Word) (tag : AValue) : Prop where
  scratch : writesImmediateScratch tag = true → r.y ≠ r.tmp
  oneFresh : tag = .ADD_ONE → r.one ≠ r.tmp
  minusFresh : tag = .SUB_ONE → r.minusOne ≠ r.tmp
  immediate : needsImmediate tag = true → s r.immediate = imm
  one : tag = .ADD_ONE → s r.one = 1
  minusOne : tag = .SUB_ONE ∨ tag = .ANDCM ∨ tag = .ANDCM_IMM → s r.minusOne = ~~~0#64
  cmSource : tag = .ANDCM → r.x ≠ r.tmp
  cmScratch : tag = .ANDCM_IMM → r.tmp ≠ r.tmp2
  cmLiteral : tag = .ANDCM_IMM → r.minusOne ≠ r.tmp
  shiftCount : tag = .SHLADD ∨ tag = .SHLADDP4 → imm < 64
  pointer : usesPointer tag = true → PointerInputs s r
  pointerShift : tag = .SHLADDP4 →
    r.small 30 ≠ r.tmp ∧ r.small 3 ≠ r.tmp ∧ r.small 61 ≠ r.tmp ∧ r.small 4294967295 ≠ r.tmp

theorem legalShiftMask (count : Word) (valid : count < 64) : count &&& 63 = count := by
  bv_decide

set_option maxHeartbeats 1200000 in
theorem payloadValue [DecidableEq Temp] (s : State Temp) (r : PayloadRegs Temp)
    (imm : Word) (target : PayloadTarget) (opt : Nat → FoldOptions)
    (tag : AValue) (known : PayloadInputs s r imm tag) :
    (run s ((aPayload r imm target opt tag).map ScalarInput.unoptimized)).1 r.dst =
      sdmPayload s r imm tag := by
  have immediate := known.immediate
  have one := known.one
  have minusOne := known.minusOne
  have scratch := known.scratch
  have oneFresh := known.oneFresh
  have minusFresh := known.minusFresh
  have cmSource := known.cmSource
  have cmScratch := known.cmScratch
  have cmLiteral := known.cmLiteral
  have shiftCount := known.shiftCount
  have ptrShift := known.pointerShift
  have ptr := known.pointer
  clear known
  cases tag
  case' ADDS => by_cases zero : imm = 0#64
  case' ADDL => by_cases zero : imm = 0#64
  case' ANDCM => cases haveNot : target.notOpcode
  case' ANDCM_IMM => cases haveNot : target.notOpcode
  case' SHLADD => by_cases zero : imm = 0#64
  case' SHLADDP4 =>
    cases haveExtract2 : target.extract2 <;> cases haveExtract32 : target.extract32 <;>
      by_cases zero : imm = 0#64
  case' ADDP4 => cases haveExtract2 : target.extract2 <;> cases haveExtract32 : target.extract32
  case' ADDP4_IMM => cases haveExtract2 : target.extract2 <;> cases haveExtract32 : target.extract32
  all_goals
    try simp [needsImmediate, writesImmediateScratch, usesPointer] at *
    try obtain ⟨pSrc, pSum, p30, p30Low, p3, p3Low, p3High, p61, p61Low, p61High,
      pMask, pMaskLow⟩ := ptr
    try obtain ⟨t30, t3, t61, tMask⟩ := ptrShift
    try
      have mask := congrArg BitVec.toNat (legalShiftMask imm shiftCount)
      simp at mask
    simp_all [aPayload, sdmPayload, ScalarInput.unoptimized, run, Operation.run, Expression.value,
      write, eval, Extra.expression, lowMask,
      IA64Integer.SDM.ADD, IA64Integer.SDM.ADD_ONE, IA64Integer.SDM.SUB,
      IA64Integer.SDM.SUB_ONE, IA64Integer.SDM.AND, IA64Integer.SDM.OR, IA64Integer.SDM.XOR,
      IA64Integer.SDM.ADDS, IA64Integer.SDM.ADDL, IA64Integer.SDM.SUB_IMM,
      IA64Integer.SDM.AND_IMM, IA64Integer.SDM.OR_IMM, IA64Integer.SDM.XOR_IMM,
      IA64Integer.SDM.ANDCM, IA64Integer.SDM.ANDCM_IMM, IA64Integer.SDM.SHLADD,
      IA64Integer.SDM.SHLADDP4, IA64Integer.SDM.ADDP4, IA64Integer.SDM.ADDP4_IMM]
    all_goals bv_decide

theorem optimizedPayload [DecidableEq Temp] (pool : ConstantPool Temp) (s : State Temp)
    (r : PayloadRegs Temp) (imm : Word) (target : PayloadTarget) (opt : Nat → FoldOptions) (tag : AValue)
    (known : PayloadInputs s r imm tag) (constants : pool.sound s)
    {final : Metadata Temp} {ops : List (Operation Temp)}
    (compiled : Translation pool (initialized pool.readonly) (aPayload r imm target opt tag) ops final) :
    (run s ops).1 r.dst = sdmPayload s r imm tag :=
  (congrArg (fun result => result.1 r.dst)
    (initializedScalarBlock pool compiled s constants).1).trans (payloadValue s r imm target opt tag known)

end IA64Optimizer
