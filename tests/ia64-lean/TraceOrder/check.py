#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check cacheable memory-order graphs with host masks lifted from TCG."""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(PROJECT))
sys.path.insert(0, str(PROJECT / "Memory"))

from Memory.fused import check_decoder, check_source
from Ordering.check import (
    cases,
    check_helper_fallback,
    check_optimizer,
    check_translator_paths,
    check_witness_bodies,
    compact,
    conventional_masks,
    define,
    function,
    mask_value,
    one_barrier,
    read,
    require,
    switches,
    verify_parallel_scope,
)


def ir_mappings(system):
    check_witness_bodies()
    translator = read("target/ia64/translate/translate.c")
    check_translator_paths(translator, read("target/ia64/translate/gen-memory.c"))
    check_optimizer(read("tcg/optimize.c"))
    verify_parallel_scope()
    values = {name: int(number, 16) for name, number in re.findall(
        r"(TCG_(?:MO|BAR)_\w+)\s*=\s*(0x[\da-fA-F]+)", read("include/tcg/tcg-mo.h"))}
    values |= {name: mask_value(define(translator, name), values)
               for name in ("IA64_TCG_MO_ACQUIRE", "IA64_TCG_MO_RELEASE")}
    default = re.search(r"\.guest_default_memory_order\s*=\s*([^,]+)",
                        read("target/ia64/cpu.c"))
    require(default is not None, "Missing IA-64 TCG default ordering contract")
    requests = conventional_masks(read("tcg/tcg-op-ldst.c"), values)
    mf = cases(switches(function(system, "ia64_gen_system"))[0][2])["MF"]
    match = re.fullmatch(r"tcg_gen_mb\(([^()]+)\);break;", compact(mf))
    require(match is not None, "Unsupported mf emission")
    return {
        "ldLd": values["TCG_MO_LD_LD"], "stLd": values["TCG_MO_ST_LD"],
        "ldSt": values["TCG_MO_LD_ST"], "stSt": values["TCG_MO_ST_ST"],
        "allPairs": values["TCG_MO_ALL"], "sc": values["TCG_BAR_SC"],
        "guestDefault": mask_value(default[1], values),
        "acquire": one_barrier(translator, "ia64_gen_memory_acquire", "mem_acquire", values),
        "release": one_barrier(translator, "ia64_gen_memory_release", "mem_release", values),
        "defaultLoad": requests["ld"],
        "defaultStore": requests["st"],
        "runtimeLoad": check_helper_fallback(values),
        "fence": mask_value(match[1], values),
    }


def generate(system):
    mappings = ir_mappings(system)
    result = [
        "import TraceOrder.Forward",
        "import Ordering.Spec",
        "import Memory.Fused",
        "set_option warningAsError true",
        "namespace TraceOrder.Lowering",
    ]
    for name, value in mappings.items():
        result.append(f"def {name} : BitVec 8 := {value}")
    result.append("end TraceOrder.Lowering")
    for file in ("Lowering.lean.inc", "Examples.lean.inc", "ForwardExamples.lean.inc",
                 "ByteExamples.lean.inc"):
        result.append((HERE / file).read_text(encoding="utf-8"))
    return "\n".join(result) + "\n"


def run(source):
    with tempfile.TemporaryDirectory(prefix="ia64-trace-order-") as temporary:
        module = Path(temporary) / "TraceProof.lean"
        module.write_text(source, encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", str(module)], cwd=PROJECT,
                              check=False, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    system = read("target/ia64/translate/gen-system.c")
    subprocess.run(["lake", "build", "+TraceOrder.Forward", "+Ordering.Spec", "+Memory.Fused"],
                   cwd=PROJECT, check=True, stdout=subprocess.DEVNULL)
    source = generate(system)
    emulator_source = PROJECT.parents[1]
    check_source(emulator_source)
    check_decoder(emulator_source)
    if args.emit_lean:
        args.emit_lean.write_text(source, encoding="utf-8")
        return
    result = run(source)
    if result.returncode != 0:
        parser.exit(1, result.stdout + result.stderr)
    print("TraceOrder: finite coherent-WB graphs compose with emitted TCG pair masks")
    print("TraceOrder: pending own stores preserve the published release/acquire rules")
    print("TraceOrder: overlapping widths share one read time and one publication time per write")
    print("COUNTEREXAMPLE ordering:tcg-narrow-cmpxchg-rel")
    if args.mutation_test:
        fence = "tcg_gen_mb(TCG_MO_ALL);"
        if system.count(fence) != 1:
            parser.exit(1, "TraceOrder: could not locate mf barrier for mutation\n")
        weakened = run(generate(system.replace(fence, "tcg_gen_mb(TCG_MO_LD_LD);")))
        if weakened.returncode == 0 or "prover found a counterexample" not in weakened.stdout:
            parser.exit(1, "TraceOrder: weakened mf did not falsify the pair theorem\n"
                        + weakened.stdout + weakened.stderr)
        print("TraceOrder: Lean rejected an mf with missing load/store pair edges")
        correct = ("queuedImage 0 3 1 1 := by\n"
                   "  exact .forwarded 2 (by queue_decide) (by queue_decide) rfl")
        incorrect = correct.replace("0 3 1 1", "0 3 1 2").replace(".forwarded 2", ".forwarded 1")
        require(source.count(correct) == 1, "could not locate youngest-store proof for mutation")
        older = run(source.replace(correct, incorrect))
        if older.returncode == 0 or "is false" not in older.stdout:
            parser.exit(1, "TraceOrder: selecting the older pending store passed\n"
                        + older.stdout + older.stderr)
        print("TraceOrder: Lean rejected selecting an older pending same-vCPU store")
        expected = ("theorem earlyHigh : IRValue overlap clock image 1 2 1 0 :=\n"
                    "  .initial (by byte_decide) (by byte_decide)")
        premature = ("theorem earlyHigh : IRValue overlap clock image 1 2 1 0xff :=\n"
                     "  .published 0 (by byte_decide) (by byte_decide) rfl")
        require(source.count(expected) == 1, "could not locate mixed-width rule for mutation")
        early = run(source.replace(expected, premature))
        if early.returncode == 0 or "is false" not in early.stdout:
            parser.exit(1, "TraceOrder: using a not-yet-published high byte passed\n"
                        + early.stdout + early.stderr)
        print("TraceOrder: Lean rejected reading a high byte before its write publishes")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
