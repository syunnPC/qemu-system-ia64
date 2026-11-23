#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the scalar TCG bodies of optimized IA-64 packed integer operations."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))

from Integer.check import cases, compact, emit, function, require, stripped


LANE_WIDTHS = (8, 16, 32)
AVERAGE_WIDTHS = (8, 16)


def repeated_bit_body(body, bits, index):
    loop = re.fullmatch(
        r"uint64_tresult=(\d+);"
        r"for\(unsignedlane=(\d+);lane<(\d+);lane\+=bits\)"
        r"\{result\|=UINT64_C\((\d+)\)<<\(lane\+bit\);\}"
        r"returnresult;", compact(body))
    require(loop is not None, "Unsupported repeated-bit mask generator")
    result, start, limit, unit = map(int, loop.groups())
    for lane in range(start, limit, bits):
        require(lane + index < 64, "Repeated-bit mask uses an invalid shift")
        result |= unit << (lane + index)
    return str(result)


def lift_simd(function_name, source, bits, mode):
    mask = function(source, "ia64_simd_repeated_bit")
    body = function(source, function_name)
    body = re.sub(r"ia64_simd_repeated_bit\(bits,\s*(0|bits\s*-\s*1)\)",
                  lambda match: repeated_bit_body(
                      mask, bits, 0 if match[1] == "0" else bits - 1),
                  body)
    body = re.sub(r"\bunsigned\b(?=\s+[a-z_]+\s*[=;])", "uint64_t", body)
    body = re.sub(r"\bround_away_from_zero\b", "immediate", body)
    values = {"bits": f"{bits}#64", "immediate": str(mode).lower(),
              "a": "x", "b": "y", "result": "old"}
    emit(body, values)
    return values["result"]


def lift_wrap(source, gvec, tcg_header, bits, subtract):
    body = function(source, "ia64_gen_packed_wrap")
    switch = re.fullmatch(r"\s*switch\s*\(bits\)\s*\{(.*)\}\s*", body, re.S)
    require(switch is not None, "Unsupported packed wrapping emitter")
    switch_body = re.sub(r"^(    case )(\d+):", r"\1IA64_OP_\2:",
                         switch[1], flags=re.M)
    dispatch = cases(switch_body)
    require(set(dispatch) == {"8", "16", "32", "default"} and
            compact(dispatch["default"]) == "g_assert_not_reached();",
            "Unexpected packed wrapping opcode-width dispatch")
    branch = re.fullmatch(r"if\(subtract\)\{(.*)\}else\{(.*)\}break;",
                          compact(dispatch[str(bits)]))
    require(branch is not None, "Unsupported packed wrapping dispatch")
    selected = re.fullmatch(r"(tcg_gen_vec_(?:add|sub)\d+_i64)\(result,a,b\);",
                            branch[1 if subtract else 2])
    require(selected is not None, "Unsupported packed wrapping TCG call")
    body = function(gvec, selected[1])
    helper = re.fullmatch(r"TCGv_i64m=tcg_constant_i64\(dup_const\(MO_(\d+),"
                          r"(0x[0-9a-f]+)\)\);(gen_(?:add|sub)v_mask)\(d,a,b,m\);",
                          compact(body))
    values = {"a": "x", "b": "y", "d": "old"}
    if helper:
        width, immediate, name = helper.groups()
        width = int(width)
        const_pattern = (rf"\(VECE\)\s*==\s*MO_{width}\s*\?\s*"
                         rf"(0x[\da-fA-F]+)ull\s*\*\s*\(uint{width}_t\)\(C\)")
        constant = re.search(const_pattern, tcg_header)
        require(constant is not None, "Unsupported dup_const lane constant")
        duplicated = int(constant[1], 16) * (int(immediate, 16) % (1 << width))
        values["m"] = f"{duplicated % (1 << 64)}#64"
        body = function(gvec, name)
    emit(body, values)
    return values["d"]


def proof(name, width, rule, actual):
    actual = actual.replace("TCG.deposit", "IA64Integer.TCG.deposit")
    packed = []
    for lane in reversed(range(64 // width)):
        extract = f"extractLsb' {lane * width} {width}"
        packed.append(f"(SDM.{rule} (x.{extract}) (y.{extract}))")
    return (
        f"theorem {name}_{width} (x y old : BitVec 64) :\n"
        f"    {actual} =\n      "
        + " ++ ".join(packed)
        + f" := by\n  simp only [SDM.{rule}, IA64Integer.TCG.deposit] <;> bv_decide"
    )


def generate(source, gvec, tcg_header):
    proofs = []
    for width in LANE_WIDTHS:
        for subtract, rule in ((False, "add"), (True, "sub")):
            actual = lift_wrap(source, gvec, tcg_header, width, subtract)
            proofs.append(proof(rule, width, rule, actual))
        actual = lift_simd("ia64_gen_packed_equal", source, width, False)
        proofs.append(proof("equal", width, "equal", actual))
        if width not in AVERAGE_WIDTHS:
            continue
        for round_up, rule in ((False, "average"), (True, "averageRaz")):
            actual = lift_simd("ia64_gen_packed_average", source, width, round_up)
            proofs.append(proof(rule, width, rule, actual))
    return "\n\n".join(proofs)


def run_lean(proofs):
    specification = (HERE / "Spec.lean").read_text(encoding="utf-8")
    integer = (HERE.parent / "Integer/Spec.lean").read_text(encoding="utf-8")
    sources = [integer, specification.replace("import Std.Tactic.BVDecide", ""),
               "namespace IA64Simd", "set_option linter.unusedVariables false",
               "set_option maxRecDepth 4096", "set_option maxHeartbeats 2000000",
               proofs, "end IA64Simd"]
    with tempfile.TemporaryDirectory(prefix="ia64-lean-simd-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text("\n".join(sources), encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true",
                               str(module)], cwd=HERE.parent, capture_output=True,
                              text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check that a change to the average rounding fails")
    parser.add_argument("--coverage", action="store_true",
                        help="report partial packed-integer opcode coverage as JSON")
    args = parser.parse_args()
    if args.coverage:
        scope = {}
        for width in LANE_WIDTHS:
            size = width // 8
            for operation in ("PADD", "PSUB"):
                scope[f"tcg:{operation}{size}"] = "Packed wrapping TCG value projection"
            scope[f"tcg:PCMP{size}_EQ"] = "Scalar packed equality TCG value projection"
        for width in AVERAGE_WIDTHS:
            scope[f"tcg:PAVG{width // 8}"] = "Packed average and average .raz TCG value projections"
        print(json.dumps(scope, sort_keys=True))
        return
    source = stripped((ROOT / "target/ia64/translate/gen-simd.c").read_text())
    gvec = stripped((ROOT / "tcg/tcg-op-gvec.c").read_text())
    tcg_header = stripped((ROOT / "include/tcg/tcg.h").read_text())
    baseline = run_lean(generate(source, gvec, tcg_header))
    require(baseline.returncode == 0, baseline.stdout + baseline.stderr)
    print("SIMD: 13 packed integer TCG value theorems passed")
    if args.mutation_test:
        mutant, count = re.subn(r"tcg_gen_or_i64\(result, result, differing\);",
                                "tcg_gen_xor_i64(result, result, differing);",
                                source)
        require(count == 1, "Could not locate packed average mutation target")
        mutated = run_lean(generate(mutant, gvec, tcg_header))
        require(mutated.returncode != 0 and "counterexample" in mutated.stdout,
                "Lean did not reject mutated packed average:\n"
                + mutated.stdout + mutated.stderr)
        print("SIMD: Lean rejected the packed average rounding mutation")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
