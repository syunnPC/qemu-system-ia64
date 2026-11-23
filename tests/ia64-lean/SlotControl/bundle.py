#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose the lifted 128-bit ADD decode with its checked slot effects."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from SlotControl import add, check


def lift_decoder(root, names):
    with tempfile.TemporaryDirectory(prefix="ia64-add-decoder-") as temp:
        decoder = Path(temp) / "Decoder.lean"
        select = [argument for name in names for argument in ("--emit-opcode", name)]
        subprocess.run([
            sys.executable, "FullDecode/check.py", "--source-root", str(root.resolve()),
            "--emit-lean", str(decoder), *select,
        ], cwd=HERE.parent, check=True)
        return decoder.read_text(encoding="utf-8").replace("import Std.Tactic.BVDecide\n", "")


def generate(root):
    return "\n\n".join([
        add.generate(root, check.generate(root)), lift_decoder(root, ("ADD",)),
        (HERE / "Bundle.lean.inc").read_text(encoding="utf-8"),
    ])


def changed_stop(root, work):
    original = root / "target/ia64"
    target = work / "target/ia64"
    target.mkdir(parents=True)
    for entry in original.iterdir():
        if entry.name == "decoder.c":
            text = entry.read_text(encoding="utf-8")
            text, count = re.subn(
                r'(\[0x02\] = \{ true,\s*"MII",\s*\{[^}]+\},\s*\{ false, )true(?=, false \})',
                r"\g<1>false", text,
            )
            if count != 1:
                raise ValueError("missing stop-bit mutation in the MII template")
            (target / entry.name).write_text(text, encoding="utf-8")
        else:
            (target / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    return work


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
    result = check.lean(program)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    print("Slot control: the actual 128-bit ADD decoder record refines the slot contract")
    if args.mutation_test:
        with tempfile.TemporaryDirectory(prefix="ia64-add-bundle-mutation-") as temp:
            mutant = generate(changed_stop(args.source_root, Path(temp)))
        result = check.lean(mutant)
        if result.returncode != 1 or "counterexample" not in result.stdout.lower():
            raise SystemExit("The altered template stop was not rejected in Lean.\n"
                             + result.stdout + result.stderr)
        print("Slot control: Lean rejected removing the internal MII stop")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
