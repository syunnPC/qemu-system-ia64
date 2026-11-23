#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Apply the initialized-block proof to emitted IA-64 A-format arithmetic."""

import argparse
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer import check as integer
from Optimizer import check, copy, extras
from SlotControl.reify import function, recognize

SOURCE = "target/ia64/translate/gen-integer.c"
TRANSLATOR = "target/ia64/translate/translate.c"
TCG = "tcg/tcg-op.c"
DIRECT = (
    "ADD", "ADD_ONE", "SUB", "SUB_ONE", "AND", "OR", "XOR",
    "ADDS", "ADDL", "SUB_IMM", "AND_IMM", "OR_IMM", "XOR_IMM",
    "ANDCM", "ANDCM_IMM", "SHLADD", "SHLADDP4", "ADDP4", "ADDP4_IMM",
)
BRANCHLESS = {
    "ADDS", "ADDL", "AND_IMM", "OR_IMM", "XOR_IMM", "ADD", "SUB", "AND", "OR", "XOR",
}
OPERANDS = {
    "cpu_gr[op->destination]": "r.dst",
    "ia64_gr_src(op->source1)": "r.x",
    "ia64_gr_src(op->source2)": "r.y",
    "tmp": "r.tmp",
    "tmp2": "r.tmp2",
    "low": "r.low",
    "high": "r.high",
    "imm": "r.immediate",
    "op->immediate": "r.immediate",
    "1": "r.one",
}


def pre_emission(tcg):
    recognize(tcg, "tcg_gen_op2_i64", """
        tcg_gen_op2(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2));
    """)
    recognize(tcg, "tcg_gen_op3_i64", """
        tcg_gen_op3(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2), tcgv_i64_arg(a3));
    """)
    recognize(tcg, "tcg_gen_op4ii_i64", """
        return tcg_gen_op4(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2), a3, a4);
    """)
    for opcode in ("add", "sub", "and", "or", "xor", "shl", "shr"):
        recognize(tcg, f"tcg_gen_{opcode}_i64",
                  f"tcg_gen_op3_i64(INDEX_op_{opcode}, ret, arg1, arg2);")
    recognize(tcg, "tcg_gen_movi_i64", "tcg_gen_mov_i64(ret, tcg_constant_i64(arg));")
    recognize(tcg, "tcg_gen_mov_i64", "if (ret != arg) { tcg_gen_op2_i64(INDEX_op_mov, ret, arg); }")
    recognize(tcg, "tcg_gen_addi_i64", """
        if (arg2 == 0) {
            tcg_gen_mov_i64(ret, arg1);
        } else {
            tcg_gen_add_i64(ret, arg1, tcg_constant_i64(arg2));
        }
    """)
    recognize(tcg, "tcg_gen_subi_i64", "tcg_gen_addi_i64(ret, arg1, -arg2);")
    for opcode in ("shl", "shr"):
        recognize(tcg, f"tcg_gen_{opcode}i_i64", f"""
            tcg_debug_assert(arg2 >= 0 && arg2 < 64);
            if (arg2 == 0) {{
                tcg_gen_mov_i64(ret, arg1);
            }} else {{
                tcg_gen_{opcode}_i64(ret, arg1, tcg_constant_i64(arg2));
            }}
        """)
    recognize(tcg, "tcg_gen_not_i64", """
        if (tcg_op_supported(INDEX_op_not, TCG_TYPE_I64, 0)) {
            tcg_gen_op2_i64(INDEX_op_not, ret, arg);
        } else {
            tcg_gen_xori_i64(ret, arg, -1);
        }
    """)
    recognize(tcg, "tcg_gen_xori_i64", """
        if (arg2 == 0) {
            tcg_gen_mov_i64(ret, arg1);
        } else if (arg2 == -1 && tcg_op_supported(INDEX_op_not, TCG_TYPE_I64, 0)) {
            tcg_gen_op2_i64(INDEX_op_not, ret, arg1);
        } else {
            tcg_gen_xor_i64(ret, arg1, tcg_constant_i64(arg2));
        }
    """)
    recognize(tcg, "tcg_gen_ori_i64", """
        if (arg2 == -1) {
            tcg_gen_movi_i64(ret, -1);
        } else if (arg2 == 0) {
            tcg_gen_mov_i64(ret, arg1);
        } else {
            tcg_gen_or_i64(ret, arg1, tcg_constant_i64(arg2));
        }
    """)
    recognize(tcg, "tcg_gen_ext32u_i64", "tcg_gen_extract_i64(ret, arg, 0, 32);")
    recognize(tcg, "tcg_gen_andi_i64", """
        switch (arg2) {
        case 0:
            tcg_gen_movi_i64(ret, 0);
            return;
        case -1:
            tcg_gen_mov_i64(ret, arg1);
            return;
        default:
            if (!(arg2 & (arg2 + 1))) {
                unsigned len = ctz64(~arg2);
                if (TCG_TARGET_extract_valid(TCG_TYPE_I64, 0, len)) {
                    tcg_gen_extract_i64(ret, arg1, 0, len);
                    return;
                }
            }
            break;
        }
        tcg_gen_and_i64(ret, arg1, tcg_constant_i64(arg2));
    """)
    recognize(tcg, "tcg_gen_extract_i64", """
        uint64_t mask;
        tcg_debug_assert(ofs < 64);
        tcg_debug_assert(len > 0);
        tcg_debug_assert(len <= 64);
        tcg_debug_assert(ofs + len <= 64);
        if (ofs + len == 64) {
            tcg_gen_shri_i64(ret, arg, 64 - len);
            return;
        }
        if (TCG_TARGET_extract_valid(TCG_TYPE_I64, ofs, len)) {
            tcg_gen_op4ii_i64(INDEX_op_extract, ret, arg, ofs, len);
            return;
        }
        mask = (1ull << len) - 1;
        if (ofs == 0) {
            tcg_gen_andi_i64(ret, arg, mask);
            return;
        }
        if (TCG_TARGET_extract_valid(TCG_TYPE_I64, 0, ofs + len)) {
            tcg_gen_op4ii_i64(INDEX_op_extract, ret, arg, 0, ofs + len);
            tcg_gen_shri_i64(ret, ret, ofs);
            return;
        }
        if (tcg_op_imm_match(INDEX_op_and, TCG_TYPE_I64, mask)) {
            tcg_gen_shri_i64(ret, arg, ofs);
            tcg_gen_andi_i64(ret, ret, mask);
        } else {
            tcg_gen_shli_i64(ret, arg, 64 - len - ofs);
            tcg_gen_shri_i64(ret, ret, 64 - len);
        }
    """)


def calls(name, source):
    match = re.fullmatch(r"((?:(?:tcg_gen_\w+_i64|ia64_gen_addp4_result)\([\w(),>\[\]-]+\);)+)", source)
    integer.require(match is not None, f"unsupported pure A-format arithmetic for {name}")
    return [(opcode, args.split(",")) for opcode, args in re.findall(r"(\w+)\(([^;]+)\);", match[1])]


def body(name, source, pointer, fast=False):
    code = integer.compact(source)
    integer.require(code.endswith("break;"), f"changed value dispatch for {name}")
    code = code.removesuffix("break;")
    lookup = OPERANDS.copy()
    if fast:
        lookup["result"] = "r.dst"
    for scratch in ("tmp", "tmp2"):
        allocation = f"{scratch}=tcg_temp_new_i64();"
        if code.startswith(allocation):
            code = code.removeprefix(allocation)
        else:
            lookup.pop(scratch)
    raw = []
    for subroutine, args in calls(name, code):
        if subroutine == "ia64_gen_addp4_result":
            integer.require(len(args) == 3, f"unexpected pointer emitter arguments for {name}")
            bindings = dict(zip(("dst", "src1", "src3"), args))
            raw.extend((instr, [bindings.get(arg, arg) for arg in vals]) for instr, vals in pointer)
        else:
            raw.append((subroutine, args))
    words = []
    for pos, (subroutine, raw_args) in enumerate(raw):
        opcode = subroutine.removeprefix("tcg_gen_").removesuffix("_i64")
        args = [f"(r.small {int(arg)}#64)" if arg.isdecimal() and arg != "1" else lookup[arg]
                for arg in raw_args]
        def fold(kind, dst, a, b):
            return f".fold ⟨.{kind}, {dst}, {a}, {b}, opts {pos}⟩"
        if opcode == "movi":
            require_arity = len(args) == 2
            text = f".move {args[0]} {args[1]}"
        elif opcode == "not":
            dst, src = args
            require_arity = len(args) == 2
            text = (f"if target.notOpcode then .extra .complement {dst} {src} {src} "
                    f"else {fold('xor', dst, src, 'r.minusOne')}")
        elif opcode == "ext32u":
            dst, src = args
            require_arity = len(args) == 2
            text = (f"if target.extract32 then .extra (.low 32) {dst} {src} {src} "
                    f"else {fold('and', dst, src, '(r.small 4294967295#64)')}")
        elif opcode == "andi":
            dst, src, mask = args
            require_arity = len(args) == 3
            if fast and raw_args[-1] == "op->immediate":
                text = (f"if imm = 0#64 then .move {dst} r.immediate "
                        f"else if imm = ~~~0#64 then .move {dst} {src} "
                        f"else if imm &&& (imm + 1) = 0 then "
                        f"  (match extract with "
                        f"  | some n => .extra (.low n) {dst} {src} {src} "
                        f"  | none => {fold('and', dst, src, mask)}) "
                        f"else {fold('and', dst, src, mask)}")
            else:
                integer.require(raw_args[-1] == "3", "unsupported pointer AND immediate")
                text = (f"if target.extract2 then .extra (.low 2) {dst} {src} {src} "
                        f"else {fold('and', dst, src, mask)}")
        elif opcode in ("ori", "xori"):
            dst, src, imm_temp = args
            require_arity = len(args) == 3
            integer.require(fast and raw_args[-1] == "op->immediate", f"unsupported {opcode} immediate")
            if opcode == "ori":
                text = (f"if imm = ~~~0#64 then .move {dst} {imm_temp} "
                        f"else if imm = 0#64 then .move {dst} {src} "
                        f"else {fold('or', dst, src, imm_temp)}")
            else:
                text = (f"if imm = 0#64 then .move {dst} {src} "
                        f"else if imm = ~~~0#64 ∧ target.notOpcode = true "
                        f"then .extra .complement {dst} {src} {src} else {fold('xor', dst, src, imm_temp)}")
        elif opcode in ("shli", "shri"):
            dst, src, count = args
            require_arity = len(args) == 3
            kind = opcode[:-1]
            if raw_args[-1] == "op->immediate":
                text = f"if imm = 0#64 then .move {dst} {src} else .extra .{kind} {dst} {src} {count}"
            else:
                integer.require(0 < int(raw_args[-1]) < 64, f"invalid constant shift for {name}")
                text = f".extra .{kind} {dst} {src} {count}"
        else:
            require_arity = len(args) == 3
            output, a, b = args
            if opcode == "subi":
                integer.require(b == "r.one", "unexpected immediate in native decrement")
                opcode, b = "add", "r.minusOne"
            if opcode == "addi":
                integer.require(b in {"r.one", "r.immediate"}, "unexpected native add immediate")
                actual = "1#64" if b == "r.one" else "imm"
                text = (f"if {actual} = 0#64 then .move {output} {a} "
                        f"else {fold('add', output, a, b)}")
            else:
                integer.require(opcode in {"add", "sub", "and", "or", "xor"},
                                f"unsupported TCG generator in {name}: {opcode}")
                text = fold(opcode, output, a, b)
        integer.require(require_arity, f"unexpected argument count in {name}")
        words.append(text)
    return "[" + ", ".join(words) + "]"


def lift_payload(root, source, translator):
    pre_emission(check.load(root, TCG))
    emitter = function(source, "ia64_gen_native_integer_write")
    (start, _, found), = integer.switches(emitter)
    prefix = integer.compact(emitter[:start])
    integer.require(prefix ==
                    "constIA64IntegerOperands*op=&insn->operands.integer;TCGv_i64tmp;TCGv_i64tmp2;"
                    "if(op->destination==0){return;}switch(insn->opcode)",
                    "unrecognized native integer predicate on entering the value emitter")
    branches = integer.cases(found)
    pointer = function(source, "ia64_gen_addp4_result")
    prefix = "TCGv_i64low=tcg_temp_new_i64();TCGv_i64high=tcg_temp_new_i64();"
    pointer = integer.compact(pointer)
    integer.require(pointer.startswith(prefix), "changed pointer scratch allocation")
    pointer = calls("ia64_gen_addp4_result", pointer.removeprefix(prefix))
    ordinary = function(source, "ia64_gen_integer")
    start, end, general = integer.switches(ordinary)[0]
    integer.require(integer.compact(ordinary[end + 1:]) == "returnIA64_GEN_CONTINUE;",
                    "changed integer dispatcher exit")
    general = integer.cases(general)
    integer.require(integer.compact(ordinary[:start]) ==
                    "constIA64IntegerOperands*op=&insn->operands.integer;TCGv_i64tmp;switch(insn->opcode)",
                    "changed ordinary dispatcher entry")
    for name in DIRECT:
        if name not in {"ADDP4", "ADDP4_IMM"}:
            integer.require(integer.compact(general[name]) ==
                            "ia64_gen_native_integer_write(insn);ia64_gen_native_integer_nat(insn);break;",
                            f"changed integer value dispatch for {name}")
    for name, arity in (("ADDP4", "2"), ("ADDP4_IMM", "1")):
        nat_sources = "op->source1,op->source2" if arity == "2" else "op->source2"
        text = integer.compact(general[name])
        match = re.fullmatch(r"if\(op->destination!=0\){"
                             r"(TCGv_i64imm=tcg_constant_i64\(op->immediate\);)?"
                             r"(ia64_gen_addp4_result\([^;]+;)"
                             rf"ia64_gen_gr_nat_from_{arity}\(insn,op->destination,{nat_sources}\);"
                             r"}break;", text)
        integer.require(match is not None and bool(match[1]) == (arity == "1"),
                        f"changed integer dispatch for {name}")
        branches[name] = match[2] + "break;"
    constructors = "\n".join(f"  | {name}" for name in DIRECT)
    cases = "\n".join(f"  | .{name} => {body(name, branches[name], pointer)}" for name in DIRECT)
    expected = []
    actual = {"x": "(s r.x)", "y": "(s r.y)", "imm": "imm"}
    for name in DIRECT:
        operands = " ".join(actual[x] for x in integer.OPERANDS[name].split())
        expected.append(f"  | .{name} => IA64Integer.SDM.{name} {operands}")
    expected = "\n".join(expected)
    _, fast, supported, _ = integer.fast_sections(translator)
    integer.require(supported == BRANCHLESS, "changed branchless arithmetic value coverage")
    fast_cases = "\n".join(f"  | .{name} => some {body(name, fast[name], (), fast=True)}"
                           for name in sorted(supported))
    return f"""
namespace IA64Optimizer
local notation "Word" => BitVec 64
inductive AValue where
{constructors}

def aPayload (r : PayloadRegs Temp) (imm : Word) (target : PayloadTarget) (opts : Nat → FoldOptions) :
    AValue → List (ScalarInput Temp)
{cases}

def sdmPayload (s : State Temp) (r : PayloadRegs Temp) (imm : Word) : AValue → Word
{expected}

def fastPayload (r : PayloadRegs Temp) (imm : Word) (target : PayloadTarget)
    (extract : Option (BitVec 6)) (opts : Nat → FoldOptions) :
    AValue → Option (List (ScalarInput Temp))
{fast_cases}
  | _ => none
end IA64Optimizer
"""


def generate(root, source=None, standalone=True, *, optimizer_source=None, translator=None):
    if source is None:
        source = check.load(root, SOURCE)
    if translator is None:
        translator = check.load(root, TRANSLATOR)
    optimizer = copy.generate(root, optimizer_source)
    reference = ""
    if standalone:
        reference = (integer.HERE / "Spec.lean").read_text(encoding="utf-8").replace(
            "import Std.Tactic.BVDecide\n", "")
    else:
        optimizer = optimizer.replace("import Std.Tactic.BVDecide\n", "", 1)
    return "\n\n".join([
        optimizer, reference,
        (HERE / "Payload.lean.inc").read_text(encoding="utf-8"),
        lift_payload(root, source, translator),
        (HERE / "PayloadTheorems.lean.inc").read_text(encoding="utf-8"),
        (HERE / "PayloadFootprint.lean.inc").read_text(encoding="utf-8"),
        (HERE / "FastPayload.lean.inc").read_text(encoding="utf-8"),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = check.verify(program)
    integer.require(result.returncode == 0, result.stdout + result.stderr)
    _, _, fast, _ = integer.fast_sections(check.load(args.source_root, TRANSLATOR))
    print(f"TCG optimizer: {len(DIRECT)} ordinary and {len(fast)} branchless IA-64 A-format payloads preserve SDM values")
    if args.mutation_test:
        mutations = (
            (SOURCE, "source", r"(case IA64_OP_ADD_ONE:\s*tmp = tcg_temp_new_i64\(\);\s*tcg_gen_)add(?=_i64\()",
             r"\1sub", "subtraction in the ADD_ONE emitter"),
            (SOURCE, "source", r"tcg_gen_shri_i64\(high, src3, 30\);",
             "tcg_gen_shri_i64(high, src3, 61);", "the wrong ADDP4 region bits"),
            (TRANSLATOR, "translator", r"(case IA64_OP_ADD:\s*tcg_gen_)add(?=_i64\(result,)",
             r"\1sub", "subtraction in the branchless ADD emitter"),
            (check.SOURCE, "optimizer_source", r"return \(uint64_t\)x << \(y & 63\);",
             "return (uint64_t)x >> (y & 63);", "the wrong shift fold direction"),
        )
        for path, arg, pattern, replacement, description in mutations:
            mutant, count = re.subn(pattern, replacement, check.load(args.source_root, path), count=1)
            integer.require(count == 1, f"missing source mutation for {description}")
            rejected = check.verify(generate(args.source_root, **{arg: mutant}))
            integer.require(rejected.returncode == 1 and "counterexample" in rejected.stdout.lower(),
                            f"Lean did not reject {description}:\n" + rejected.stdout + rejected.stderr)
            print(f"TCG optimizer: Lean rejected {description}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None
