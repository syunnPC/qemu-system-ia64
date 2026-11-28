#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose the projected scalar optimizer with the complete A-format slot routes."""

import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from FullDecode.forms import ALU
from Integer.check import require
from Optimizer import add_cond, check, live_copy, payload
from SlotControl import alu


def generate(root):
    optimizer = check.load(root, check.SOURCE)
    live_copy.check_source(root, optimizer)
    add_cond.check_source(root, optimizer)
    require(set(ALU) == set(payload.DIRECT), "A-format optimizer and decode cases disagree")
    value_tags = "\n".join(f"  | .{name} => .{name}" for name in ALU)
    glue = f"""
namespace IA64OptimizedSlot

def valueTag : IA64Alu.Opcode → IA64Optimizer.AValue
{value_tags}

theorem sdmValue (temp : IA64Optimizer.State Temp) (r : IA64Optimizer.PayloadRegs Temp)
    (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (tag : IA64Alu.Opcode)
    (x : temp r.x = IA64Tcg.SDM.value s i.r2) (y : temp r.y = IA64Tcg.SDM.value s i.r3) :
    IA64Optimizer.sdmPayload temp r i.imm (valueTag tag) =
      (IA64Alu.operation tag).value (IA64Tcg.SDM.value s i.r2) (IA64Tcg.SDM.value s i.r3) i.imm := by
  rw [← x, ← y]
  cases tag <;> rfl

end IA64OptimizedSlot
"""
    return "\n\n".join([
        alu.generate(root),
        payload.generate(root, standalone=False),
        glue,
        (HERE / "Slot.lean.inc").read_text(encoding="utf-8"),
        (HERE / "LiveAdd.lean.inc").read_text(encoding="utf-8"),
        (HERE / "SlotAllocation.lean.inc").read_text(encoding="utf-8"),
        (HERE / "AddCond.lean.inc").read_text(encoding="utf-8"),
        (HERE / "RegisterSlot.lean.inc").read_text(encoding="utf-8"),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--qemu", type=Path)
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.qemu:
        from Optimizer import slot_live

        qemu = args.qemu.resolve()
        program += slot_live.generate(slot_live.capture(qemu))
        program += "\n" + live_copy.generate(live_copy.capture(qemu))
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = check.verify(program)
    require(result.returncode == 0, result.stdout + result.stderr)
    print("TCG optimizer: composed the ordinary and branchless ALU payloads with slot boundaries")
    if args.qemu:
        print("TCG optimizer: verified live ADD results, copy propagation, NaT and predicate effects")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None
