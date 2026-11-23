#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify emitted and optimized TCG for an ordinary and predicated IA-64 ADD."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Decode.check import run_lean
from HostLitmus.replay import trace_program
from ia64.encoding import (
    addl,
    br_cond,
    ld8_fill_postinc,
    mov_gr_pr,
    mov_m_imm_ar,
    nop_i,
    nop_m,
    raw_bundle,
)

ADD_IP = 0x100
EXIT_IP = 0x110
ADD_GRS = {"r8": "old", "r9": "left", "r10": "right", "nat": "tags", "p2": "pred"}
BINARY = {
    "add": "+", "sub": "-", "and": "&&&", "or": "|||", "xor": "^^^",
    "shl": "<<<", "shr": ">>>",
}


def fixture(qualifier, pending_nat, pred):
    # SDM rev. 2.3 Vol. 3 §4.2.1.1, A1 with extensions x2a=ve=x4=x2b=0.
    # Slot 1 of MIB carries add r8 = r9, r10, qualified by p0 or p2.
    add = (8 << 37) | (10 << 20) | (9 << 13) | (8 << 6) | qualifier
    instructions = [
        (0x10, 0x00, mov_m_imm_ar(36, pending_nat), addl(4, 0x200, 0), addl(2, 4 * pred, 0)),
        (0x20, 0x00, ld8_fill_postinc(9, 4, 0), addl(8, 0x11, 0), addl(10, 0x40, 0)),
        (0x30, 0x11, nop_m(), mov_gr_pr(2, 4), br_cond(0x30, ADD_IP)),
        (ADD_IP, 0x11, nop_m(), add, br_cond(ADD_IP, EXIT_IP)),
        (EXIT_IP, 0x11, nop_m(), nop_i(), br_cond(EXIT_IP, EXIT_IP)),
        raw_bundle(0x200, 0x25, 0),
    ]
    executes = qualifier == 0 or pred
    expected = {
        "ip": EXIT_IP,
        "r9_nat": int(pending_nat),
        "r8_nat": int(executes and pending_nat),
    }
    if not executes or not pending_nat:
        expected["r8"] = 0x65 if executes else 0x11
    return instructions, expected


def operations(trace, address=ADD_IP):
    pair = re.compile(r"^OP:\n(.*?)^OP after optimization and liveness analysis:\n"
                      r"(.*?)(?=^OP:\n|\Z)", re.M | re.S)
    marker = re.compile(r"^\s*----\s+([0-9a-fA-F]+)[^\n]*$", re.M)
    blocks = []
    for before, after in pair.findall(trace):
        marks = [list(marker.finditer(block)) for block in (before, after)]
        if not any(found and int(found[0][1], 16) == address for found in marks):
            continue
        if any(len(found) != 1 or int(found[0][1], 16) != address for found in marks):
            raise ValueError(f"expected a single-bundle TCG block at {address:#x}")
        blocks.append((before[marks[0][0].end():], after[marks[1][0].end():]))
    if len(blocks) != 1:
        raise ValueError(f"expected one emitted and optimized TCG pair at {address:#x}")

    for stage, block in zip(("emitted", "optimized"), blocks[0], strict=True):
        # Bundle slot bookkeeping brackets the slot 1 arithmetic between
        # the PSR.ri updates 0 -> 1 -> 2. Match both before projection.
        lines = [line.split()[:2] for line in block.splitlines() if line.strip()]
        enter = ["xor_i64", "psr,psr,$0x20000000000"]
        leave = ["xor_i64", "psr,psr,$0x60000000000"]
        if lines[0] != enter or lines.count(leave) != 1:
            raise ValueError(f"{stage}: unexpected boundaries for slot 1 ADD")
        end = lines.index(leave)
        if end <= 1:
            raise ValueError(f"{stage}: empty TCG slice")
        yield stage, lines[1:end]


def imm(value):
    if not re.fullmatch(r"\$0x[0-9a-fA-F]+", value):
        raise ValueError(f"expected TCG immediate, got {value}")
    result = int(value[1:], 16)
    if result >= 1 << 64:
        raise ValueError(f"immediate wider than i64: {value}")
    return result


def project(name, lines):
    registers = ADD_GRS.copy()

    def operand(value):
        if value.startswith("$"):
            return f"({imm(value)} : Word)"
        if value not in registers:
            raise ValueError(f"{name}: read of unsupported TCG value {value}")
        return registers[value]

    bindings = []
    for index, (opcode, operands) in enumerate(lines):
        dest, *src = operands.split(",")
        if dest not in ("r8", "nat") and not re.fullmatch(r"(?:loc|tmp)\d+", dest):
            raise ValueError(f"{name}: unsupported TCG destination {dest}")
        if opcode.removesuffix("_i64") in BINARY and opcode.endswith("_i64") and len(src) == 2:
            lhs, rhs = (operand(item) for item in src)
            operator = BINARY[opcode.removesuffix("_i64")]
            rhs = f"{lhs} {operator} {rhs}"
        elif opcode == "mov_i64" and len(src) == 1:
            rhs = operand(src[0])
        elif opcode == "deposit_i64" and len(src) == 4:
            left, right = (operand(item) for item in src[:2])
            offset, width = map(imm, src[2:])
            if width == 0 or offset + width > 64:
                raise ValueError(f"{name}: invalid TCG deposit field")
            rhs = f"IA64Integer.TCG.deposit {left} {right} {offset} {width}"
        elif opcode == "movcond_i64" and len(src) == 5:
            lhs, rhs, yes, no = (operand(item) for item in src[:4])
            tests = {"ne": f"{lhs} ≠ {rhs}", "eq": f"{lhs} = {rhs}",
                     "tstne": f"({lhs} &&& {rhs}) ≠ 0"}
            if src[4] not in tests:
                raise ValueError(f"{name}: unsupported TCG condition {src[4]}")
            rhs = f"if {tests[src[4]]} then {yes} else {no}"
        else:
            raise ValueError(f"{name}: unsupported TCG operation {opcode} {operands}")
        registers[dest] = f"t{index}"
        bindings.append(f"  let {registers[dest]} := {rhs}")

    return "\n".join([f"def {name} (old left right pred tags : Word) : Word × Word :=",
                      *bindings, f"  ({registers['r8']}, {registers['nat']})"])


def theorems(name, qualifier, nat_clear):
    inputs = ("old left right pred tags : Word")
    args = "old left right pred tags"
    data = "SDM.ADD left right"
    updated = "SDM.replaceNaT tags 8 (SDM.staticNaT tags 9 || SDM.staticNaT tags 10)"
    if qualifier != 0:
        data = f"if pred = 0 then old else {data}"
        updated = f"if pred = 0 then tags else {updated}"
    hypothesis = "(allClear : tags = 0)" if nat_clear else ""
    return f"""
theorem {name}_value ({inputs}) : ({name} {args}).1 = ({data}) := by
  simp only [{name}, SDM.ADD] <;> bv_decide

theorem {name}_nat ({inputs}) {hypothesis} : ({name} {args}).2 = ({updated}) := by
  simp only [{name}, SDM.replaceNaT, SDM.staticNaT, IA64Integer.TCG.deposit] <;> bv_decide
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path,
                        help="trusted QEMU system IA-64 executable")
    qemu = parser.parse_args().qemu.resolve()
    generated = ["import Integer.Spec", "open IA64Integer", "abbrev Word := BitVec 64",
                 "set_option linter.unusedVariables false"]
    for qualifier, state in ((0, False), (0, True), (2, False), (2, True)):
        # Run both outcomes for the dynamic p2 cases. Do not compare the
        # simulator's concrete values where the ADD result has NaT set.
        for pred in ((False, True) if qualifier else (True,)):
            program, expected = fixture(qualifier, state, pred)
            tag = f"p{qualifier}_nat{int(state)}_pred{int(pred)}"
            trace = trace_program(qemu, program, expected, f"emitted-add-{tag}",
                                  log_flags="op,op_opt")
            for stage, instructions in operations(trace):
                name = f"{stage}_{tag}"
                generated.extend([project(name, instructions),
                                  theorems(name, qualifier, not state)])

    with tempfile.TemporaryDirectory(prefix="ia64-emitted-lean-") as tmp:
        work = Path(tmp)
        (work / "Integer").mkdir()
        run_lean(work, HERE.parent / "Integer/Spec.lean", "-o", str(work / "Integer/Spec.olean"))
        proof = work / "Emitted.lean"
        proof.write_text("\n\n".join(generated) + "\n")
        run_lean(work, proof)
    print("IA-64 ADD refined SDM GR/NaT updates in 12 emitted and optimized TCG slices")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"IA-64 emitted TCG check failed: {exc}") from None
