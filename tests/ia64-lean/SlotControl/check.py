#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-lifted native IA-64 control effects at the TCG IR boundary."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import require, stripped
from SlotControl import add
from SlotControl.records import check_callers, lift_nop_decoder
from SlotControl.reify import lift_debug, lift_group, lift_restart, lift_success, lift_traps

TRANSLATOR = "target/ia64/translate/translate.c"
CPU_HEADER = "target/ia64/cpu.h"


def load(root, path):
    return stripped((root / path).read_text(encoding="utf-8"))


def generate(root, translator=None, decoder=None):
    if translator is None:
        translator = load(root, TRANSLATOR)
    header = load(root, CPU_HEADER)
    if decoder is None:
        decoder = load(root, "target/ia64/decode/decode.c")
    check_callers(translator, load(root, "target/ia64/translate/gen-system.c"),
                  load(root, "target/ia64/decode/opcode.inc"), load(root, "target/ia64/helper.h"))
    return "\n\n".join([
        (HERE / "Spec.lean").read_text(encoding="utf-8"), "namespace IA64Slot",
        lift_restart(translator, header), lift_success(translator, header),
        lift_traps(translator, header), lift_debug(translator, header),
        lift_group(translator), lift_nop_decoder(decoder),
        (HERE / "Theorems.lean.inc").read_text(encoding="utf-8"), "end IA64Slot",
    ])


def lean(program):
    with tempfile.TemporaryDirectory(prefix="ia64-slot-control-") as tmp:
        filename = Path(tmp) / "SlotControl.lean"
        filename.write_text(program, encoding="utf-8")
        return subprocess.run(
            ["lake", "env", "lean", "-DwarningAsError=true", str(filename)],
            cwd=HERE.parent, capture_output=True, text=True, check=False,
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--mutation-test", action="store_true")
    parser.add_argument("--qemu", type=Path, help="also prove projections of captured TCG IR")
    parser.add_argument("--build-dir", type=Path, help="build directory matching the QEMU executable")
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.coverage:
        print(json.dumps({
            "tcg:ADD": "Decoded ADD slot effects through retirement or first helper/fault request",
            "tcg:NOP": "M/I NOP recognition and shared restart, IIPA, group and helper-request subeffects",
        }, sort_keys=True))
        return
    shared = program
    program = add.generate(args.source_root, shared)
    if args.qemu:
        from SlotControl.probe import generate as capture

        qemu = args.qemu.resolve()
        fixture = capture(qemu, (args.build_dir or qemu.parent).resolve(), HERE / "offsets.c")
        program += "\n\n" + fixture
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    checked = lean(program)
    require(checked.returncode == 0, checked.stdout + checked.stderr)
    print("Slot control: lifted restart, IIPA, group and native helper request passed")
    print("Slot control: decoded ADD refines SDM through retirement or its first helper request")
    if args.qemu:
        print("Slot control: emitted/optimized decoded NOP, ADD, ADD r0 and single-step probes passed")
    if args.mutation_test:
        translator = load(args.source_root, TRANSLATOR)
        source = translator.replace("tcg_gen_movi_i64(cpu_ip, bundle_ip + 16);",
                                    "tcg_gen_movi_i64(cpu_ip, bundle_ip + 32);", 1)
        require(source != translator, "missing restart address mutation")
        changed = lean(generate(args.source_root, source))
        require(changed.returncode != 0 and "unsolved goals" in changed.stdout,
                "The altered next bundle address passed:\n" + changed.stdout + changed.stderr)
        print("Slot control: Lean rejected advancing by two bundles")
        decoder = load(args.source_root, "target/ia64/decode/decode.c")
        old = "const uint64_t value = 1ULL << 27;"
        source = decoder.replace(old, "const uint64_t value = 2ULL << 27;", 1)
        require(source != decoder, "missing NOP encoding mutation")
        changed = lean(generate(args.source_root, decoder=source))
        require(changed.returncode != 0 and "counterexample" in changed.stdout,
                "The altered NOP opcode mask passed:\n" + changed.stdout + changed.stderr)
        print("Slot control: Lean rejected changing the M-unit NOP encoding")
        operand, count = re.subn(
            r"(insn\.operands\.decoder\.imm = ia64_imm8\(raw\);\s*\}\s*else\s*\{\s*"
            r"insn\.operands\.decoder\.r2 = ia64_bits\(raw, )13(?=, 7\);)",
            r"\g<1>12", decoder,
        )
        require(count == 1, "missing register ADD source-field mutation")
        changed = lean(add.generate(args.source_root, shared, operand))
        require(changed.returncode != 0 and "unsolved goals" in changed.stdout,
                "The altered ADD source field passed:\n" + changed.stdout + changed.stderr)
        print("Slot control: Lean rejected shifting ADD's decoded r2 field")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.stderr or str(error)) from None
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from None
