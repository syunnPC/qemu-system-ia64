#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prove IA-64 floating register conversions and scalar fmerge value paths."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "Decode"))
from check import ROOT, Expression, c_function, get_match, run_lean, uncomment
from bitwise import LOGICAL, MIX, generate_bitwise
from fclass import generate_fclass
from merge import helper_merge, tcg_merge, validate_cache
from rotation import generate_rotation
from target_write import generate_target_write


class FloatExpression(Expression):
    def atom(self):
        if self.tokens[self.pos:self.pos + 3] == ["(", "uint32_t", ")"]:
            self.pos += 3
            return f"(({self.atom()}).extractLsb' 0 32).zeroExtend 64"
        return super().atom()


def word(value, names):
    value = re.sub(r"UINT(?:32|64)_C\((0x[\da-fA-F]+|\d+)\)", r"\g<1>ULL", value)
    return FloatExpression(value, names, {}).lean()


def compact_c(source):
    return re.sub(r"\s+", "", source)


def body(source, function):
    return compact_c(c_function(source, function)[1])


def assignments(source, sig="*sig", exp="*exp"):
    e, s = get_match(rf"{re.escape(exp)}=([^;]+);{re.escape(sig)}=([^;]+);",
                     source, "register assignment", full=True).groups()
    return e, s


def macro(source, name):
    return get_match(rf"^#define {name}\s+([^\n]+)", source, name)[1]


def c_load(source, width, macros):
    function = f"binary{width}_to_register_format"
    parts = get_match(
        r"uint(?:32|64)_tfrac=([^;]+);uint32_tbinary_exp=([^;]+);\*sign=([^;]+);"
        r"if\(binary_exp==([^()]+)\)\{"
        r"if\(frac==([^()]+)\)\{([^{}]+)\}else\{([^{}]+)\}\}"
        r"elseif\(binary_exp==([^()]+)\)\{([^{}]+)\}else\{([^{}]+)\}",
        body(source, function), function, full=True,
    )
    frac, exp, sign, zero, empty, empty_set, subnormal_set, special, special_set, normal_set = (
        parts.groups()
    )
    names = macros | {name: name for name in ("value", "frac", "binary_exp")}

    def result(c):
        exponent, significand = (word(expr, names) for expr in assignments(c))
        return f"s ++ ({exponent}).extractLsb' 0 17 ++ {significand}"

    return f"""
def cLoad{width} (input : BitVec {width}) : Register :=
  let value := input.zeroExtend 64
  let frac := {word(frac, names)}
  let binary_exp := {word(exp, names)}
  let s := (if {word(sign, names)} == 0 then 0 else 1 : BitVec 1)
  if binary_exp == {word(zero, names)} then
    if frac == {word(empty, names)} then {result(empty_set)}
    else {result(subnormal_set)}
  else if binary_exp == {word(special, names)} then {result(special_set)}
  else {result(normal_set)}
"""


def c_store(source, width, macros):
    function = f"register_format_to_binary{width}"
    start, test, exponent, fraction = get_match(
        rf"uint{width}_tvalue=([^;]+);if\(([^)]+)\)\{{value\|=([^;]+);\}}"
        r"value\|=([^;]+);returnvalue;",
        body(source, function), function, full=True,
    ).groups()
    names = macros | {name: name for name in ("sig", "exp", "signBit")}
    names["sign"] = names["signBit"]
    return f"""
def cStore{width} (fp : Register) : BitVec {width} :=
  let sig := significand fp
  let exp := (exponent fp).zeroExtend 64
  let signBit := (sign fp).zeroExtend 64
  let e := if {word(test, names)} != 0 then {word(exponent, names)} else 0
  ({word(start, names)} ||| e ||| {word(fraction, names)}).extractLsb' 0 {width}
"""


def c_compact(source, macros):
    function = "register_format_to_binary64_exact"
    start, pattern, expected, distance, span, first, second, empty, zero = get_match(
        r"uint64_tresult=([^;]+);"
        r"if\(likely\(\(sig&(.+?)\)==([^&]+)&&"
        r"(.+?)<=(.+?)\)\)\{result\|=([^;]+);result\|=([^;]+);\}"
        r"elseif\(sig==([^&]+)&&exp==([^)]*)\)\{\}else\{returnfalse;\}"
        r"\*value=result;returntrue;",
        body(source, function), function, full=True,
    ).groups()
    names = macros | {"sig": "sig", "exp": "exp", "sign": "signBit"}
    return f"""
def cCompact (fp : Register) : BitVec 65 :=
  let sig := significand fp
  let exp := (exponent fp).zeroExtend 64
  let signBit := (sign fp).zeroExtend 64
  let result := {word(start, names)}
  if (sig &&& {word(pattern, names)}) = {word(expected, names)} ∧
     {word(distance, names)} ≤ {word(span, names)} then
    (1 : BitVec 1) ++ (result ||| {word(first, names)} ||| {word(second, names)})
  else if sig = {word(empty, names)} ∧ exp = {word(zero, names)} then
    (1 : BitVec 1) ++ result
  else 0
"""


def c_spill(source, macros):
    pack = get_match(r"\*high=([^;]+);$", body(source, "ia64_fpreg_to_spill"),
                     "ia64_fpreg_to_spill high word")[1]
    truncate, exponent, sign = get_match(
        r"high&=([^;]+);exp=([^;]+);sign=([^;]+);",
        body(source, "ia64_fpreg_from_spill"), "ia64_fpreg_from_spill fields",
    ).groups()
    return f"""
def cSpill (fp : Register) : BitVec 128 :=
  let exp := (exponent fp).zeroExtend 64
  let signBit := (sign fp).zeroExtend 64
  {word(pack, macros | {"exp": "exp", "sign": "signBit"})} ++ significand fp

def cFill (raw : BitVec 128) : Register :=
  let high := raw.extractLsb' 64 64 &&& {word(truncate, macros)}
  let signBit := (if {word(sign, {"high": "high"})} == 0 then 0 else 1 : BitVec 1)
  signBit ++ ({word(exponent, {"high": "high"})}).extractLsb' 0 17 ++ raw.extractLsb' 0 64
"""


def c_tcg_significand(source, function, operand, name):
    # Project the arithmetic between selection of an untagged cache source
    # and the later tag check or packed sign operation.
    frac, align, exp, zero, place = get_match(
        rf"tcg_gen_andi_i64\(value,ia64_fr_binary_src\({re.escape(operand)}\),([^;]+)\);"
        r"tcg_gen_shli_i64\(value,value,([^;]+)\);(?:exponent=tcg_temp_new_i64\(\);)?"
        rf"tcg_gen_andi_i64\(exponent,ia64_fr_binary_src\({re.escape(operand)}\),([^;]+)\);"
        r"tcg_gen_setcondi_i64\(TCG_COND_NE,exponent,exponent,([^;]+)\);"
        r"tcg_gen_shli_i64\(exponent,exponent,([^;]+)\);"
        r"tcg_gen_or_i64\(value,value,exponent\);",
        body(source, function), function,
    ).groups()
    frac, align, exp, zero, place = (word(expr, {}) for expr in (frac, align, exp, zero, place))
    return f"""
def {name} (raw : Word) : Word :=
  let payload := (raw &&& {frac}) <<< {align}
  let integer : Word := if (raw &&& {exp}) != {zero} then 1 else 0
  payload ||| (integer <<< {place})
"""


def c_fixed(source, cpu_header):
    fixed_zero, fixed_one = get_match(
        r"if\(reg==IA64_FR_ZERO_INDEX\)\{returntcg_constant_i64\(([^;]+)\);\}"
        r"if\(reg==IA64_FR_ONE_INDEX\)\{returntcg_constant_i64\(([^;]+)\);\}"
        r"returncpu_fr\[reg\];",
        body(source, "ia64_fr_binary_src"), "ia64_fr_binary_src", full=True,
    ).groups()
    return f"""
def cFixedZero : Word := {word(fixed_zero, {})}
def cFixedOne : Word := {word(fixed_one, {"IA64_FR_ONE": word(macro(cpu_header, "IA64_FR_ONE"), {})})}
"""


THEOREMS = (
    ("loadSingle", "(raw : BitVec 32)", "cLoad32 raw = load32 raw"),
    ("loadDouble", "(raw : Word)", "cLoad64 raw = load64 raw"),
    ("storeSingle", "(fp : Register)", "cStore32 fp = store32 fp"),
    ("storeDouble", "(fp : Register)", "cStore64 fp = store64 fp"),
    ("spillBits", "(fp : Register)", "cSpill fp = spill fp"),
    ("fillBits", "(raw : BitVec 128)", "cFill raw = fill raw"),
    ("compactBits", "(fp : Register)", "cCompact fp = compact fp"),
    ("fastSignificand", "(raw : Word)", "cFastSignificand raw = significand (load64 raw)"),
    ("copySignificand", "(raw : Word)", "cCopySignificand raw = significand (load64 raw)"),
    ("fixedZeroEncoding", "", "load64 cFixedZero = (0 : Register)"),
    ("fixedOneEncoding", "", "load64 cFixedOne = (65535 : BitVec 18) ++ (1 <<< 63 : Word)"),
)


def generate(root):
    target = root / "target/ia64"
    fpreg, header, translate, fpgen, cpu_header, fp, fp_helper, operands, decode = [
        uncomment((target / relative).read_text())
        for relative in ("fpreg.c", "fpreg.h", "translate/translate.c",
                         "translate/gen-fp.c", "cpu.h", "arch/fp.c", "helper/helper-fp.c",
                         "decode/operand.h", "decode/decode.c")
    ]
    macros = {}
    for name in ("IA64_FP_SINGLE_EXP_BASE", "IA64_FP_SINGLE_FRAC_MASK",
                 "IA64_FP_DOUBLE_EXP_BASE", "IA64_FP_DOUBLE_MAX_EXP",
                 "IA64_FP_DOUBLE_FRAC_MASK", "IA64_FP_DOUBLE_LOW_MASK"):
        macros[name] = word(macro(fpreg, name), {})
    for name in ("IA64_FP_REG_SPECIAL_EXP", "IA64_FP_SIGNIFICAND_INTEGER_BIT",
                 "IA64_FP_SPILL_EXP_SIGN_MASK"):
        macros[name] = word(macro(header, name), {})
    definitions = [
        c_load(fpreg, 32, macros), c_load(fpreg, 64, macros),
        c_store(fpreg, 32, macros), c_store(fpreg, 64, macros),
        c_compact(fpreg, macros), c_spill(fpreg, macros),
        c_tcg_significand(fpgen, "ia64_gen_fp_significand", "reg", "cFastSignificand"),
        c_tcg_significand(fpgen, "ia64_gen_fp_copy", "op->source1", "cCopySignificand"),
        c_fixed(translate, cpu_header),
    ]
    unfolds = re.findall(r"^def (\w+)", "".join(definitions), flags=re.M)
    unfolds += ["load", "load32", "load64", "store", "store32", "store64",
                "spill", "fill", "compact", "sign", "significand", "exponent"]
    validate_cache(fpreg, header, translate, fp, body, cpu_header)
    return (
        (HERE / "Spec.lean").read_text(encoding="utf-8") +
        "\nopen IA64.Floating\n" + "\n".join(definitions)
        + "\n".join(f"theorem {name} {args} : {goal} := by\n"
                    f"  simp only [{', '.join(unfolds)}] <;> bv_decide\n"
                    for name, args, goal in THEOREMS)
        + tcg_merge(fpgen, body, word) + helper_merge(fp, fp_helper, header, body, macro, word)
        + (HERE / "MergeProof.lean.inc").read_text(encoding="utf-8")
        + generate_bitwise(translate, fpgen, fp, fp_helper, header, body, macro, word)
        + (HERE / "BitwiseProof.lean.inc").read_text(encoding="utf-8")
        + generate_target_write(header, translate, cpu_header, body, macro, word)
        + (HERE / "TargetProof.lean.inc").read_text(encoding="utf-8")
        + generate_rotation(fp, translate, fp_helper, cpu_header, operands,
                            [entry[0] for entry in (*LOGICAL, *MIX)], body, macro)
        + (HERE / "RotationProof.lean.inc").read_text(encoding="utf-8")
        + generate_fclass(translate, fpgen, fp, fp_helper, header, cpu_header, decode,
                         operands, body, macro, word)
        + (HERE / "ClassProof.lean.inc").read_text(encoding="utf-8")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--emit-lean", type=Path)
    mode.add_argument("--coverage", action="store_true",
                      help="list focused evidence without running Lean")
    args = parser.parse_args()
    if args.coverage:
        bits = "all 82 result bits from the source-specific direct TCG and helper paths"
        inputs = "writable destination and materialized, canonical FP source views"
        coverage = {
            "tcg:FMERGE": f"scalar negative sign, {bits}, assuming {inputs}",
            "tcg:FMERGE_S": f"scalar sign, {bits}, assuming {inputs}",
            "tcg:FMERGE_SE": (
                f"scalar sign/exponent, {bits} with the binary64 normal gate, assuming {inputs}"
            ),
            "tcg:FCLASS": (
                "F5 operand fields, classification over all 82-bit inputs, "
                "predicate updates, and projected equal-target/disabled-FP checks"
            ),
        }
        for opcode, *_ in (*LOGICAL, *MIX):
            coverage[f"tcg:{opcode}"] = f"F9 logical/mix {bits}, assuming {inputs}"
        print(json.dumps(coverage, sort_keys=True))
        return
    text = generate(args.source_root.resolve())
    if args.emit_lean:
        args.emit_lean.write_text(text, encoding="utf-8")
        return
    with tempfile.TemporaryDirectory(prefix="ia64-floating-lean-") as tmp:
        work = Path(tmp)
        proof = work / "FloatingSource.lean"
        proof.write_text(text, encoding="utf-8")
        run_lean(work, proof, "-DwarningAsError=true")
    print("IA-64 floating register conversions and FCLASS, scalar merge, and F9 paths verified")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"IA-64 floating check failed: {exc}") from None
