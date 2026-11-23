#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the IA-64 region JIT's scalar integer IR against SDM operations."""

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))

from Integer.check import BITFIELD, HOST_CALLS, OPERANDS, arguments, branch_end
from Integer.check import COMPARE_OPCODE, cases, compact, emit, expr, function, matching, require
from Integer.check import stripped


@dataclass(frozen=True)
class Term:
    word: str
    safe: str = "True"


LLVM_OPERATIONS = {
    "LLVMBuildAdd": "+", "LLVMBuildSub": "-", "LLVMBuildAnd": "&&&",
    "LLVMBuildOr": "|||", "LLVMBuildXor": "^^^", "LLVMBuildLShr": ">>>",
    "LLVMBuildShl": "<<<", "LLVMBuildAShr": None,
}
LLVM_SHIFTS = {"LLVMBuildShl", "LLVMBuildLShr", "LLVMBuildAShr"}
EXTRA_OPERANDS = {"MIX4_L": "x y", "MIX4_R": "x y"}
PROJECTIONS = {
    "GETF_SIG": "floating-point significand bridge",
    "LD1": "memory path", "LD2": "memory path", "LD4": "memory path", "LD8": "memory path",
    "ST1": "memory path", "ST2": "memory path", "ST4": "memory path", "ST8": "memory path",
    "NOP": "null operation",
}


def read(relative):
    return stripped((ROOT / relative).read_text())


def body_is(source, name, digest):
    require(sha256(compact(function(source, name)).encode()).hexdigest() == digest,
            f"Changed LLVM operand/type context: {name}")


def conditions(*terms):
    filtered = [term for term in terms if term != "True"]
    return "(" + " ∧ ".join(filtered) + ")" if filtered else "True"


def select(condition, yes, no):
    if condition.word == "True":
        return yes
    if condition.word == "False":
        return no
    yes_safe = f"({condition.word} → {yes.safe})" if yes.safe != "True" else "True"
    no_safe = f"(¬{condition.word} → {no.safe})" if no.safe != "True" else "True"
    return Term(f"(if {condition.word} then {yes.word} else {no.word})",
                conditions(condition.safe, yes_safe, no_safe))


def ternary(source):
    depth = 0
    start = None
    nested = 0
    for at, character in enumerate(source):
        if character == "(":
            depth += 1
        if character == ")":
            depth -= 1
        if depth:
            continue
        if character == "?":
            if start is None:
                start = at
            else:
                nested += 1
        if character == ":" and start is not None:
            if nested:
                nested -= 1
            else:
                return source[:start], source[start + 1:at], source[at + 1:]
    return None


class Lift:
    def __init__(self, opcode_numbers, register_shift):
        self.opcode_numbers = opcode_numbers
        self.register_shift = register_shift

    def host_predicate(self, text, env):
        text = text.strip()
        candidates = [operand.strip() for operand in text.split("||")]
        if all(re.fullmatch(r"op\s*==\s*IA64_OP_\w+", term) for term in candidates):
            result = any(env["op"].word == self.opcode_numbers[term.split("==")[1].strip()]
                         for term in candidates)
            return Term(str(result))
        if text.startswith("!") and not text.startswith("!="):
            return Term(f"({self.value(text[1:], env).word} = 0#64)")
        return self.value(text, env)

    def host_expression(self, text, env):
        tokens = {key: value.word for key, value in env.items()}
        tokens.update(self.opcode_numbers)
        text = re.sub(r"\b([0-9]+)u\b", r"\1", text)
        return Term(expr(text, tokens))

    def value(self, text, env):
        text = text.strip()
        choice = ternary(text)
        if choice:
            predicate, yes, no = choice
            return select(self.host_predicate(predicate, env),
                          self.value(yes, env), self.value(no, env))
        if text in env:
            return env[text]
        call = re.fullmatch(r"(LLVM\w+|extend_field|register_shift)\s*\((.*)\)",
                            text, re.S)
        if not call:
            return self.host_expression(text, env)
        operation, content = call.groups()
        operands = arguments(content)
        if operation in {"LLVMConstInt", "LLVMConstNull"}:
            require(operands[0] == "i64", f"Unexpected LLVM scalar type: {text}")
            if operation == "LLVMConstNull":
                require(len(operands) == 1, f"Unexpected LLVM null signature: {text}")
                return Term("0#64")
            require(len(operands) == 3 and operands[2] == "0",
                    f"Unexpected LLVM constant signature: {text}")
            return self.value(operands[1], env)
        require(operands[0] == "b", f"Unexpected LLVM builder: {text}")
        if operation == "register_shift":
            helper = dict(env)
            for name, value in zip(("op", "count", "value"), operands[1:], strict=True):
                helper[name] = self.value(value, env)
            return self.execute(self.register_shift, helper)
        if operation == "extend_field":
            data, width, sign = [self.value(item, env) for item in operands[1:]]
            return Term(f"(extendField {data.word} {width.word} {sign.word})",
                        conditions(data.safe, width.safe, sign.safe,
                                   f"(0#64 < {width.word} ∧ {width.word} ≤ 64#64)"))
        require(operands[-1] == '""', f"Unexpected LLVM metadata argument: {text}")
        if operation == "LLVMBuildICmp":
            require(operands[1] == "LLVMIntUGT", f"Unexpected LLVM comparison: {text}")
            a, b = [self.value(item, env) for item in operands[2:-1]]
            return Term(f"({b.word} < {a.word})", conditions(a.safe, b.safe))
        data = [self.value(item, env) for item in operands[1:-1]]
        if operation == "LLVMBuildSelect":
            return select(*data)
        if operation == "LLVMBuildNot":
            [arg] = data
            return Term(f"(~~~{arg.word})", arg.safe)
        require(operation in LLVM_OPERATIONS, f"Unsupported LLVM value: {text}")
        left, right = data
        operator = LLVM_OPERATIONS[operation]
        result = (f"({left.word} {operator} {right.word})" if operator is not None
                  else f"({left.word}.sshiftRight' {right.word})")
        safety = conditions(left.safe, right.safe,
                            f"({right.word} < 64#64)" if operation in LLVM_SHIFTS
                            else "True")
        return Term(result, safety)

    def execute(self, source, env, terminal=True):
        while source.strip():
            source = source.lstrip()
            if source.startswith("{"):
                end = matching(source, 0, "{", "}")
                source = source[1:end] + source[end + 1:]
                continue
            branch = re.match(r"if\s*\(", source)
            if branch:
                start = branch.end() - 1
                end = matching(source, start, "(", ")")
                cond = self.host_predicate(source[start + 1:end], env)
                rest = source[end + 1:].lstrip()
                require(rest.startswith("{"), "LLVM source branch must be braced")
                end = matching(rest, 0, "{", "}")
                yes = rest[1:end]
                rest = rest[end + 1:].lstrip()
                no = ""
                if rest.startswith("else"):
                    rest = rest[4:].lstrip()
                    if rest.startswith("if"):
                        end = branch_end(rest)
                        no = rest[:end]
                        rest = rest[end:]
                    else:
                        require(rest.startswith("{"), "LLVM else body must be braced")
                        end = matching(rest, 0, "{", "}")
                        no = rest[1:end]
                        rest = rest[end + 1:]
                if cond.word == "True":
                    return self.execute(yes + rest, dict(env), terminal)
                if cond.word == "False":
                    return self.execute(no + rest, dict(env), terminal)
                return select(cond, self.execute(yes + rest, dict(env), terminal),
                              self.execute(no + rest, dict(env), terminal))
            statement, separator, source = source.partition(";")
            require(separator, f"Unsupported LLVM statement: {statement}")
            statement = statement.strip()
            returned = re.fullmatch(r"return\s+(.+)", statement, re.S)
            if returned:
                require(terminal, "Unexpected return in shared LLVM prefix")
                return self.value(returned[1], env)
            if compact(statement) in {"LLVMTypeRefi64=LLVMTypeOf(rhs)",
                                      "LLVMTypeRefi64=LLVMTypeOf(value)",
                                      "Ia64Opcodeop=in->opcode",
                                      "uint64_timm=in->operands.integer.immediate"}:
                continue
            assignment = re.fullmatch(r"(?:(?:LLVMValueRef|uint64_t|unsigned)\s+)?"
                                      r"(\w+)\s*=\s*([^=].*)", statement, re.S)
            require(assignment is not None, f"Unsupported LLVM assignment: {statement}")
            env[assignment[1]] = self.value(assignment[2], env)
        require(not terminal, "LLVM emitter path returned no value")


def eligible(eligibility):
    body = function(eligibility, "ia64_llvm_insn_supported")
    marker = re.search(r"switch\s*\(insn->opcode\)\s*\{", body)
    require(marker is not None, "Missing LLVM eligibility switch")
    prefix = compact(body[:marker.start()])
    require(prefix == "constIA64MemoryOperands*mem=&insn->operands.memory;"
            "constIA64IntegerOperands*integer=&insn->operands.integer;"
            "if(!insn->valid||insn->qp||insn->mem_acquire||insn->mem_release){returnfalse;}"
            "if(insn->encoding_class!=IA64_ENCODING_DEFINED)"
            "{returninsn->encoding_class==IA64_ENCODING_IGNORED&&insn->opcode==IA64_OP_NOP;}",
            "Unexpected LLVM eligibility guard")
    start = marker.end() - 1
    selected = cases(body[start + 1:matching(body, start, "{", "}")])
    alu = {opcode for opcode, case in selected.items()
           if compact(case) == "returninsn->operands.common.destination!=0;"}
    comparison = {opcode for opcode, case in selected.items()
                  if compact(case) == "returninsn->pred_update==IA64_PRED_UPDATE_NORMAL&&"
                     "integer->predicate1!=integer->predicate2;"}
    require(set(selected) == alu | comparison | (set(PROJECTIONS) - {"GETF_SIG"}) | {"default"},
            "Unexpected opcode in LLVM eligibility")
    return alu, comparison


def decoder_helpers(decoder):
    HOST_CALLS["ia64_sign_extend"] = "sourceSignExtend"
    output = []
    for source_name, lean_name, parameters in (
        ("ia64_sign_extend", "sourceSignExtend", "value width"),
        ("ia64_low_mask", "sourceLowMask", "len"),
        ("ia64_bitfield_effective_len", "sourceEffectiveLen", "pos len"),
        ("ia64_deposit_mask", "sourceDepositMask", "pos len"),
    ):
        values = {name: name for name in parameters.split()}
        body = function(decoder, source_name).replace("unsigned ", "uint64_t ")
        emit(body, values)
        output.append(f"def {lean_name} ({parameters} : Word) : Word := {values['return']}")
    return "\n".join(output)


def compare_proofs(frontend, default_case, eligible_ops):
    require(sha256(compact(default_case).encode()).hexdigest() ==
            "9a97318da442f50ffa634688d1d9b4e072d0b35b711ee58e643dd20c660d6987",
            "Unexpected LLVM comparison lowering")
    contents = re.search(r"static const Compare compares\[IA64_OP_COUNT\]\s*=\s*\{"
                         r"(.*?)\};", frontend, re.S)
    require(contents is not None, "Missing LLVM frontend compare table")
    rows = re.findall(r"\[IA64_OP_(\w+)\]\s*=\s*\{(.*?)\},", contents[1])
    require({opcode for opcode, _ in rows} == eligible_ops and
            len(rows) == len(eligible_ops), "Unexpected LLVM-eligible compare entry")
    for opcode, initializer in rows:
        specification = COMPARE_OPCODE.fullmatch(opcode)
        require(specification is not None, f"No SDM compare form for {opcode}")
        width, relation, parallel, immediate = specification.groups()
        require(not parallel, f"Unexpected LLVM parallel compare: {opcode}")
        fields = [term.strip() for term in initializer.split(",")]
        require(len(fields) <= 3 and all(compact(term) in {".word=true", ".immediate=true"}
                                         for term in fields[1:]),
                f"Unexpected compare initializer: {initializer}")
        x = "imm" if ".immediate=true" in map(compact, fields[1:]) else "x"
        y = "y"
        if ".word=true" in map(compact, fields[1:]):
            x, y = f"({x}.truncate 32)", f"({y}.truncate 32)"
        comparisons = {"LLVMIntEQ": f"{x} == {y}",
                       "LLVMIntSLT": f"{x}.slt {y}",
                       "LLVMIntULT": f"decide ({x} < {y})"}
        require(fields[0] in comparisons, f"Unexpected LLVM comparator: {fields[0]}")
        left = "imm" if immediate else "x"
        right = "y"
        if width:
            left, right = f"({left}.extractLsb' 0 32)", "(y.extractLsb' 0 32)"
        target = f"SDM.cmp{relation.capitalize()}"
        yield (f"theorem llvmCompare_{opcode} (x y imm : Word) :\n"
               f"    ({comparisons[fields[0]]}) = {target} {left} {right} := by\n"
               f"  simp only [{target}] <;> bv_decide")


def generate(frontend):
    lower = read("contrib/ia64-llvm/lower.c")
    for name, digest in (
        ("lift_slot", "5ca66571734a6d8c8c2be51786320dacc8687a0db9ef561e460c90271c308a6b"),
        ("read_gr", "6636c4af5b60b96e7f1495988902d858131ff79de63018aff8630bb00d7182af"),
        ("write_gr", "582e3a085787157983da286ae7869794701bdd1bb826ee0a1b701cce8b58b152"),
    ):
        body_is(lower, name, digest)
    body_is(frontend, "extend_field",
            "d71ec237c93bf415bc1a5b77bb12c6ab3afa3d687f48dba0acc226891e569d0e")
    eligible_ops, compare_ops = eligible(read("contrib/ia64-llvm/insn.h"))
    body = function(frontend, "ia64_llvm_lift_integer")
    marker = re.search(r"switch\s*\(op\)\s*\{", body)
    require(marker is not None, "Missing LLVM scalar operation switch")
    start = marker.end() - 1
    selected = cases(body[start + 1:matching(body, start, "{", "}")])
    covered = eligible_ops - PROJECTIONS.keys()
    require(covered == set(selected) - {"default"}, "Unsupported eligible LLVM scalar opcode")
    opcode_names = re.findall(r"^IA64_OPCODE\((\w+),", read("target/ia64/decode/opcode.inc"), re.M)
    numbers = {f"IA64_OP_{op}": f"{idx}#64" for idx, op in enumerate(opcode_names)}
    lift = Lift(numbers, function(frontend, "register_shift"))
    helpers = decoder_helpers(read("target/ia64/decode/decode.c"))
    proofs = [helpers, SEMANTICS]
    for opcode in sorted(covered):
        env = {"op": Term(numbers[f"IA64_OP_{opcode}"]),
               "lhs": Term("x"), "rhs": Term("y"), "imm": Term("imm"),
               "true": Term("true"), "false": Term("false")}
        lift.execute(body[:marker.start()], env, terminal=False)
        actual = lift.execute(selected[opcode], env)
        args = "(x y imm : Word)"
        expected = (f"SDM.{opcode} {OPERANDS[opcode]}" if opcode in OPERANDS
                    else f"{opcode} {EXTRA_OPERANDS[opcode]}")
        setup = ""
        preprocess = ""
        if opcode in BITFIELD:
            args += (" (pos len fill : Word) (posInRange : pos < 64#64)"
                     f" (lenInRange : 0#64 < len ∧ len ≤ {16 if opcode == 'DEP' else 64}#64)")
            packed = "pos ||| (len <<< 6#64)"
            if opcode in {"DEP_IMM", "DEPZ_IMM"}:
                args += f" (fillInRange : fill < {2 if opcode == 'DEP_IMM' else 256}#64)"
                packed += " ||| (fill <<< 13#64)"
            setup = f"let imm := {packed};\n    "
        elif opcode in {"SHL_IMM", "SHR_IMM", "SHRU_IMM", "SHLADD", "SHLADDP4", "SHRP_IMM"}:
            args += " (decodedCount : imm < 64#64)"
            preprocess = "  have demask : imm &&& 63#64 = imm := by bv_decide\n"
        names = [f"SDM.{opcode}" if opcode in OPERANDS else opcode,
                 "extendField", "sourceSignExtend", "sourceDepositMask",
                 "sourceEffectiveLen", "sourceLowMask", "BitVec.add_comm",
                 "BitVec.and_comm", "BitVec.or_comm", "BitVec.xor_comm"]
        dependencies = {
            "ADDL": ["SDM.ADDS"], "ADDP4_IMM": ["SDM.ADDP4"], "SHLADDP4": ["SDM.ADDP4"],
            "AND_IMM": ["SDM.AND"], "ANDCM_IMM": ["SDM.ANDCM"], "OR_IMM": ["SDM.OR"],
            "XOR_IMM": ["SDM.XOR"], "SUB_IMM": ["SDM.SUB"], "SHL_IMM": ["SDM.SHL"],
            "SHR_IMM": ["SDM.SHR"], "SHRU_IMM": ["SDM.SHRU"],
        }
        names.extend(dependencies.get(opcode, []))
        if opcode in BITFIELD:
            names.extend(["SDM.DEP", "SDM.DEPZ", "SDM.fieldShift", "SDM.lowField"])
        if preprocess:
            names.append("demask")
        proofs.append(
            f"theorem llvmValue_{opcode} {args} :\n    {setup}{actual.word} = {expected} := by\n"
            f"{preprocess}  try simp only [{', '.join(names)}]\n  all_goals bv_decide")
        if actual.safe != "True":
            proofs.append(
                f"theorem llvmDefined_{opcode} {args} :\n    {setup}{actual.safe} := by\n"
                "  try simp only [sourceEffectiveLen, sourceLowMask, sourceDepositMask]\n"
                "  all_goals bv_decide")
    proofs.extend(compare_proofs(frontend, selected["default"], compare_ops))
    scope = {f"llvm:{op}": "Scalar LLVM value and applicable shift-definedness projection"
             for op in covered}
    scope.update({f"llvm:{op}": "LLVM comparison relation projection for eligible normal form"
                  for op in compare_ops})
    return "\n\n".join(proofs), scope


SEMANTICS = """
-- LLVM LangRef, shl/lshr/ashr and trunc/sext/zext. The shift-safety
-- theorems independently require every selected shift's count to be <64.
def extendField (value width : Word) (sign : Bool) : Word :=
  if width = 64 then value
  else if sign then
    let shift := 64#64 - width
    (value <<< shift).sshiftRight' shift
  else value &&& ((1#64 <<< width) - 1)

-- Intel Itanium SDM rev. 2.3 vol. 3, mix, 3:169–170.
def MIX4_L (x y : Word) : Word := x.extractLsb' 32 32 ++ y.extractLsb' 32 32
def MIX4_R (x y : Word) : Word := x.extractLsb' 0 32 ++ y.extractLsb' 0 32
"""


def run_lean(theorems):
    spec = (HERE.parent / "Integer/Spec.lean").read_text()
    source = [spec, "namespace IA64Integer", 'local notation "Word" => BitVec 64',
              "set_option linter.unusedVariables false", "set_option maxRecDepth 4096",
              "set_option maxHeartbeats 4000000", theorems, "end IA64Integer"]
    with tempfile.TemporaryDirectory(prefix="ia64-lean-llvm-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text("\n".join(source))
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check that removing the LLVM shift clamp is rejected")
    parser.add_argument("--coverage", action="store_true",
                        help="report partial LLVM opcode projection coverage as JSON")
    args = parser.parse_args()
    frontend = read("contrib/ia64-llvm/frontend.c")
    proofs, covered = generate(frontend)
    if args.coverage:
        print(json.dumps(covered, sort_keys=True))
        return
    baseline = run_lean(proofs)
    require(baseline.returncode == 0, baseline.stdout + baseline.stderr)
    print(f"LLVM: {len(covered)} eligible scalar IR projections and applicable shift guards passed")
    if args.mutation_test:
        mutant = frontend.replace("count = LLVMBuildSelect(b, large, max, count, \"\");",
                                    "count = count;")
        require(mutant != frontend, "Could not locate LLVM shift clamp mutation target")
        generated = generate(mutant)[0]
        match = re.search(r"^theorem llvmDefined_SHR\b.*?(?=\n\ntheorem)",
                          generated, re.M | re.S)
        require(match is not None, "Could not isolate the LLVM arithmetic-shift safety theorem")
        populated = generated[:generated.index("theorem llvmValue_")]
        mutated = run_lean(populated + match[0])
        require(mutated.returncode != 0 and "counterexample" in mutated.stdout,
                "Lean did not reject removal of LLVM shift clamp:\n"
                + mutated.stdout + mutated.stderr)
        print("LLVM: Lean rejected removal of the variable-shift clamp")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
