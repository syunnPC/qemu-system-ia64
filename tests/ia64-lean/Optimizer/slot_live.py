# SPDX-License-Identifier: GPL-2.0-or-later
"""Check aliased ADD from the selected ordinary and branchless TCG slots."""

import re

from Emitted.check import ADD_IP, EXIT_IP, fixture, operations, project
from HostLitmus.replay import trace_program
from ia64.encoding import alloc_m, br_cond, nop_i
from Integer.check import require


def aliased_add(right, nat, pred):
    program, expected = fixture(2, nat, pred)
    program[3] = (*program[3][:3], (8 << 37) | (right << 20) | (8 << 13) | (8 << 6) | 2,
                  program[3][4])
    if right == 32:
        program = [(0x10, 0x01, alloc_m(6, 1, 1, 0, 0), nop_i(), nop_i())] + [
            (bundle[0] + 16, *bundle[1:]) if bundle[0] < ADD_IP else bundle
            for bundle in program
        ]
        # Fill r32 rather than the original static r9 and branch at the new IP.
        address, template, load, old, other = program[2]
        program[2] = address, template, (load & ~(0x7f << 6)) | (32 << 6), old, other
        program[3] = (*program[3][:4], br_cond(program[3][0], ADD_IP))
        expected["r32_nat"] = expected.pop("r9_nat")
    if pred and not nat:
        expected["r8"] = 0x36
    require(expected["ip"] == EXIT_IP, "missing aliased ADD terminal state")
    return program, expected


def project_alias(name, lines, right, pending):
    if right == 32:
        predicate = re.fullmatch(r"p2,\$0x0,eq,(\$L\d+)", lines[0][1])
        require(lines[0][0] == "brcond_i64" and predicate is not None and
                tuple(lines[-1]) == ("set_label", predicate[1]),
                f"{name}: expected the ordinary predicate skip and join")
        instructions = [
            (op, re.sub(r"\br32\b", "r9", args))
            for op, args in lines[1:-1]
        ]
        execute = f"if pred = 0 then (old, tags) else {name}Data old right 0 pred tags"
    else:
        require(any(op == "movcond_i64" and args.startswith("r8,p2,") for op, args in lines),
                f"{name}: expected branchless conditional destination write")
        instructions = lines
        execute = f"{name}Data old right 0 pred tags"
    source = project(f"{name}Data", instructions)
    incoming = "" if pending else "(clear : tags = 0)"
    return f"""
{source}

theorem {name} (old right pred tags : Word) {incoming} :
    ({execute}) =
      (if pred = 0 then old else SDM.ADD old right,
       if pred = 0 then tags else
         SDM.replaceNaT tags 8 (SDM.staticNaT tags 8 || SDM.staticNaT tags {right})) := by
  by_cases zero : pred = 0 <;>
    (first | simp only [if_pos zero] | simp only [if_neg zero]) <;>
    apply Prod.ext
  all_goals simp [{name}Data, zero, SDM.ADD, SDM.replaceNaT, SDM.staticNaT,
    IA64Integer.TCG.deposit] <;> bv_decide
"""


def capture(qemu):
    results = []
    for right in (9, 32):
        for nat in (0, 1):
            for pred in (0, 1):
                bundles, expected = aliased_add(right, nat, pred)
                tag = f"aliasAdd_r{right}_nat{nat}_pred{pred}"
                trace = trace_program(qemu, bundles, expected, tag, log_flags="op,op_opt")
                for stage, ops in operations(trace):
                    results.append((f"{stage}_{tag}", ops, right, bool(nat)))
    return results


def generate(captures):
    return "\n".join([
        "namespace IA64OptimizedSlotLive", "open IA64Integer", "abbrev Word := BitVec 64",
        "set_option linter.unusedVariables false",
        *(project_alias(name, ops, right, pending) for name, ops, right, pending in captures),
        "end IA64OptimizedSlotLive",
    ])
