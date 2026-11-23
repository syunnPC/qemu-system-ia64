#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify source-lifted TCG scalar folding and sequential observation preservation."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import require, stripped
from Optimizer import reify

SOURCE = "tcg/optimize.c"
UTILS = "include/qemu/host-utils.h"


def load(root, path):
    return stripped((root / path).read_text(encoding="utf-8"))


def generate(root, source=None):
    code = reify.generate(source if source is not None else load(root, SOURCE), load(root, UTILS))
    return "\n\n".join([
        (HERE / "Spec.lean").read_text(encoding="utf-8"), code,
        (HERE / "Theorems.lean.inc").read_text(encoding="utf-8"),
        (HERE / "Trace.lean.inc").read_text(encoding="utf-8"),
    ])


def verify(program):
    with tempfile.TemporaryDirectory(prefix="ia64-optimize-proof-") as temp:
        module = Path(temp) / "Fold.lean"
        module.write_text(program, encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, check=False, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    parser.add_argument("--qemu", type=Path, help="also check existing emitted and optimized ADD IR probes")
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = verify(program)
    require(result.returncode == 0, result.stdout + result.stderr)
    print("TCG optimizer: checked i64 add/sub/and/or/xor local rewrites and sequential observations")
    if args.mutation_test:
        source = load(args.source_root, SOURCE)
        mutants = (
            (r"(case INDEX_op_add:\s+return )x \+ y;", r"\1x - y;", "constant addition"),
            (r"a_mask = t1->z_mask & ~t2->o_mask;", "a_mask = 0;", "AND affected-bit mask"),
        )
        for match, substitute, name in mutants:
            changed, count = re.subn(match, substitute, source, count=1)
            require(count == 1, f"missing optimizer mutation for {name}")
            result = verify(generate(args.source_root, changed))
            require(result.returncode == 1 and "counterexample" in result.stdout.lower(),
                    f"Lean did not reject {name}\n" + result.stdout + result.stderr)
            print(f"TCG optimizer: Lean rejected changing the {name}")
    if args.qemu:
        subprocess.run([sys.executable, "SlotControl/check.py", "--qemu", str(args.qemu.resolve())],
                       cwd=HERE.parent, check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
