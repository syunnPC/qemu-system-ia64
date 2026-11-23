/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

def permitsFastExtract (imm : Word) (extract : Option (BitVec 6)) : Prop :=
  ∀ n, extract = some n → TrailingZeros (~~~imm) (n.zeroExtend 64)

theorem branchlessPayloadValue [DecidableEq Temp] (s : State Temp) (r : PayloadRegs Temp)
    (imm : Word) (target : PayloadTarget) (extract : Option (BitVec 6)) (opt : Nat → FoldOptions)
    (tag : AValue) (literals : needsImmediate tag = true → s r.immediate = imm)
    (ctz : permitsFastExtract imm extract) {srcs : List (ScalarInput Temp)}
    (emitted : fastPayload r imm target extract opt tag = some srcs) :
    (run s (srcs.map ScalarInput.unoptimized)).1 r.dst = sdmPayload s r imm tag := by
  cases tag <;> simp only [fastPayload, Option.some.injEq, reduceCtorEq] at emitted
  all_goals subst srcs
  all_goals try simp only [needsImmediate, Bool.true_eq, true_implies] at literals
  case' ADDS => by_cases zero : imm = 0#64
  case' ADDL => by_cases zero : imm = 0#64
  case' OR_IMM => by_cases zero : imm = 0#64 <;> by_cases ones : imm = ~~~0#64
  case' XOR_IMM => by_cases zero : imm = 0#64 <;> by_cases ones : imm = ~~~0#64 <;> cases haveNot : target.notOpcode
  case' AND_IMM =>
    cases picked : extract <;> by_cases zero : imm = 0#64 <;> by_cases ones : imm = ~~~0#64 <;>
      by_cases contiguous : imm &&& (imm + 1) = 0
    all_goals try have extracted := extractLowContiguous imm _ contiguous (ctz _ picked)
  all_goals
    simp_all [sdmPayload, ScalarInput.unoptimized, run, Operation.run, Expression.value, write,
      Extra.expression, eval, IA64Integer.SDM.ADD, IA64Integer.SDM.SUB, IA64Integer.SDM.AND,
      IA64Integer.SDM.AND_IMM, IA64Integer.SDM.OR, IA64Integer.SDM.OR_IMM, IA64Integer.SDM.XOR,
      IA64Integer.SDM.XOR_IMM, IA64Integer.SDM.ADDS, IA64Integer.SDM.ADDL]
    all_goals try rw [← extracted]
    all_goals bv_decide

theorem optimizedBranchlessPayload [DecidableEq Temp] (pool : ConstantPool Temp) (s : State Temp)
    (r : PayloadRegs Temp) (imm : Word) (target : PayloadTarget) (extract : Option (BitVec 6))
    (opt : Nat → FoldOptions) (tag : AValue) (immediate : needsImmediate tag = true → s r.immediate = imm)
    (ctz : permitsFastExtract imm extract) (constants : pool.sound s)
    {srcs : List (ScalarInput Temp)} {final : Metadata Temp} {ops : List (Operation Temp)}
    (emitted : fastPayload r imm target extract opt tag = some srcs)
    (compiled : Translation pool (initialized pool.readonly) srcs ops final) :
    (run s ops).1 r.dst = sdmPayload s r imm tag :=
  (congrArg (fun result => result.1 r.dst) (initializedScalarBlock pool compiled s constants).1).trans
    (branchlessPayloadValue s r imm target extract opt tag immediate ctz emitted)

end IA64Optimizer
