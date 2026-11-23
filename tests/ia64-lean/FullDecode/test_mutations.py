#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that independent proofs reject decoder selector and routing mutations."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

from check import HERE, ROOT
from ledger import SOURCES

DECODER = "target/ia64/decode/decode.c"
MUTATIONS = {
    "alu": (DECODER, "{ 0x0, 0, IA64_OP_ADD, false }", "{ 0x0, 0, IA64_OP_SUB, false }"),
    "predicate": (DECODER, ".qp = ia64_bits(raw, 0, 6),", ".qp = ia64_bits(raw, 1, 6),"),
    "compare": (DECODER, """ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 34, 2) <= 1 &&
        ia64_bits(raw, 13, 7) != 0) {""", """
        ia64_bits(raw, 36, 1) == 1 && ia64_bits(raw, 34, 2) <= 1 &&
        ia64_bits(raw, 13, 7) == 0) {"""),
    "packed-shift": (DECODER, "size_code == 1 && x4 == 4 && x2b <= 2",
     "size_code == 1 && x4 == 4 && x2b <= 1"),
    "load-order": (DECODER, "return (x6a >= 0x14 && x6a <= 0x17)",
                   "return (x6a >= 0x15 && x6a <= 0x17)"),
    "wide-atomic-feature": ("target/ia64/cpu.h", "IA64_CPUID4_AO   (1ULL << 2)",
                            "IA64_CPUID4_AO   (1ULL << 3)"),
    "i7-fixed-count": (DECODER, "imm = 31 - ia64_bits(raw, 20, 5);",
                       "imm = 30 - ia64_bits(raw, 20, 5);"),
    "i7-reserved-selector": (DECODER, "if (major == 0 || major == 1 || major == 2 || major == 3 ||\n"
                             "            major == 5 || major == 6 || major == 7 || major == 0xa ||",
                             "if (major == 0 || major == 1 || major == 2 || major == 3 ||\n"
                             "            major == 5 || major == 6 || major == 0xa ||"),
    "clz-feature": ("target/ia64/cpu.h", "IA64_CPUID4_CZ   (1ULL << 32)",
                    "IA64_CPUID4_CZ   (1ULL << 33)"),
    "extract-length": (DECODER, "(pos_sign >> 1) | ((ia64_bits(raw, 27, 6) + 1) << 6);",
                       "(pos_sign >> 1) | (ia64_bits(raw, 27, 6) << 6);"),
    "test-predicate-sense": (DECODER, "is_tf     ? (nz_form && (bit33 || bit36) ? IA64_OP_TF_NZ",
                             "is_tf     ? (nz_form && (bit33 && bit36) ? IA64_OP_TF_NZ"),
    "move-pr-mask": (DECODER, "(ia64_bits(raw, 24, 8) << 8) |",
                     "(ia64_bits(raw, 24, 8) << 7) |"),
    "fclass-mask": (DECODER, "(ia64_bits(raw, 20, 7) << 2) | ia64_bits(raw, 33, 2);",
                    "(ia64_bits(raw, 20, 7) << 2) | ia64_bits(raw, 33, 1);"),
    "paired-tag": (DECODER, "? IA64_OP_HINT_X : IA64_OP_NOP", "? IA64_OP_HINT_X : IA64_OP_ADD"),
    "template": ("target/ia64/decoder.c", "[0x00] = { true,", "[0x00] = { false,"),
}

FOCUSED = {
    "i7-fixed-count": "PSHL2",
    "i7-reserved-selector": "POPCNT",
    "clz-feature": "CLZ",
    "extract-length": "EXTR",
    "test-predicate-sense": "TF_NZ",
    "move-pr-mask": "MOV_GRPR",
    "fclass-mask": "FCLASS",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", choices=MUTATIONS)
    for name in parser.parse_args().only or MUTATIONS:
        changed, original, replacement = MUTATIONS[name]
        with tempfile.TemporaryDirectory(prefix="ia64-full-decode-mutation-") as tmp:
            source = Path(tmp)
            for relative in SOURCES:
                text = (ROOT / relative).read_text(encoding="utf-8")
                if relative == changed:
                    if original not in text:
                        raise ValueError(f"{relative}: missing mutation anchor {original!r}")
                    text = text.replace(original, replacement, 1)
                copy = source / relative
                copy.parent.mkdir(parents=True, exist_ok=True)
                copy.write_text(text, encoding="utf-8")
            command = [sys.executable, "-B", str(HERE / "check.py"), "--source-root", str(source)]
            if name in FOCUSED:
                proof = source / "Check.lean"
                subprocess.run(command + ["--emit-opcode", FOCUSED[name],
                                          "--emit-lean", str(proof)], check=True)
                command = ["lake", "env", "lean", "-DwarningAsError=true", str(proof)]
            run = subprocess.run(command, cwd=HERE.parent, text=True, capture_output=True)
            if run.returncode != 1 or "The prover found a counterexample" not in run.stdout:
                raise ValueError(f"mutation did not yield a Lean counterexample for {original!r}\n"
                                 + run.stdout + run.stderr)
            print(f"Lean rejected {name}: replacing {original} with {replacement}")


if __name__ == "__main__":
    main()
