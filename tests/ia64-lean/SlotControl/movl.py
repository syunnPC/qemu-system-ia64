#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the legal X2 MOVL path through the MLX instruction boundary."""

import argparse
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import compact, require, switches
from SlotControl import add, check
from SlotControl.reify import fixed, function

DECODE = HERE.parent / "FullDecode"
TRANSLATOR = "target/ia64/translate/translate.c"
GENERATOR = "target/ia64/translate/gen-integer.c"

CONTROL = {
    "ia64_insn_must_start_group": "e6a9778cf2790e83a486dbc1b9f58121e1bd2fc7b4be80cd4a785b74b5ce1722",
    "ia64_insn_must_end_group": "2c42f939cf9a45b413f750acbf9317b2db59c706e7acb837bba590f65463c52b",
    "ia64_insn_requires_slot2": "09698a1915bb7dd5e63fa04569edf89310894e6c8a599ed65c95e9f6dad04976",
    "ia64_insn_has_invalid_fp_target": "45c0306fd45d9f1874843c03e6a961af33c53e352d6c2f8597629a03bdd51be8",
    "ia64_insn_has_illegal_register": "4efa09e06e7b784d8284540fc5dd9e18bfb7d05e31ae289f3d8cd7e220223449",
    "ia64_insn_has_reserved_ar": "77ae67d6e442b84ec6c568dd09fb882ac68faaf282276f4a167b66b3bbdeb5b6",
    "ia64_insn_has_reserved_mask_field": "f5ac762856557dedc8a216db747d9e76ba97e2fe68e499374af09731c1cb297e",
    "ia64_insn_is_empty_hint": "0281e83828b3593d20de4957132bb2941f25479da3a7f121af9bfeee212e6838",
    "ia64_insn_writes_gr_r1": "bd78788433b765d5d182dd911fff0cc99054f8a553dff2f8ee7eb8ef0622003a",
    "ia64_insn_generator": "647af414fdea11fd4290cf0aafe3264da1bc524c1be77de5f382127727e11aa6",
    "ia64_insn_psr_ic_must_be_clear": "79b33c238d4dc58bd8111efe4323a95a1aaf7383249625833c1dc9017a01309d",
    "ia64_insn_is_privileged": "38dc979633c187b6b6410a89988bb76e18ee51bdafecfb983a6c27ee901a39cd",
    "ia64_insn_fp_read_sets": "32fdfb44f4ba8b3e9ddac789eab820ea2c294399bf33aedb94381f3e82e4ef75",
    "ia64_insn_fp_write_sets": "8d22cfe1fd2a2b2d2d0221de8d36a7c408d8fb0c92b1b1692e7b19333fa17506",
    "ia64_prepare_self_counted_loop": "7edbe09aa728a0627867c677ec2aecb4ce66041d48d023d920ea129a82f4bf76",
    "ia64_analyze_self_counted_loop": "19f85170ddbb2cb196909d4c049af609e8bc34ae4cd8c76991f03f6bdef4535e",
    "ia64_compare_has_equal_targets": "2b32a72af4bc2466047ac4ef6969a0fff9c47275e7b2ce5772a6d6e82d230cfb",
    "ia64_load_base_update_has_same_target": "5350f53841cc9db7275023cd9a6e8369b79a6acddef1b4991cd3027cbddc2402",
}


def check_route(root):
    translator = check.load(root, TRANSLATOR)
    for name, digest in CONTROL.items():
        fixed(translator, name, digest)
    table = compact(check.load(root, "target/ia64/decode/opcode.inc"))
    require(table.count("IA64_OPCODE(MOVL,INTEGER,true,CLEAR)") == 1,
            "MOVL no longer uses the integer GR write/NaT-clear route")
    integer = function(check.load(root, GENERATOR), "ia64_gen_integer")
    begin, end, _ = switches(integer)[0]
    require(compact(integer[:begin]) ==
            "constIA64IntegerOperands*op=&insn->operands.integer;TCGv_i64tmp;switch(insn->opcode)"
            and compact(integer[end + 1:]) == "returnIA64_GEN_CONTINUE;",
            "MOVL integer dispatch entry or exit changed")
    header = compact(check.load(root, "target/ia64/decode/operand.h"))
    for name, fields, immediate in (
        ("IA64DecodedOperands", "r1 r2 r3 p1 p2 b1 b2 sf fp_precision", "imm"),
        ("IA64IntegerOperands", "destination source1 source2 predicate1 predicate2 branch1 "
                               "branch2 status_field precision", "immediate"),
        ("IA64CommonOperands", "destination source1 source2 auxiliary1 auxiliary2 branch1 "
                              "branch2 status_field precision", "immediate"),
    ):
        members = "".join(f"uint8_t{field};" for field in fields.split())
        require(f"typedefstruct{name}{{{members}int64_t{immediate};}}{name};" in header,
                f"changed operand layout {name}")
    union = header.split("typedefunionIA64Operands{", 1)[1].split("}IA64Operands;", 1)[0]
    require(all(view in union for view in ("IA64DecodedOperandsdecoder;",
                                          "IA64IntegerOperandsinteger;", "IA64CommonOperandscommon;")),
            "missing shared MOVL operand views")


def lift_legal_movl(root, work):
    path = sys.path[:]
    try:
        sys.path.insert(0, str(DECODE))
        lift = runpy.run_path(str(DECODE / "check.py"))
    finally:
        sys.path[:] = path
    source, _ = lift["generate"](root, work, ("MOVL",), legal_movl_only=True)
    return source.replace("import Std.Tactic.BVDecide\n", "")


def generate(root):
    check_route(root)
    with tempfile.TemporaryDirectory(prefix="ia64-movl-decode-") as tmp:
        lifted = lift_legal_movl(root.resolve(), Path(tmp))
    return "\n\n".join([
        add.generate(root, check.generate(root), opcodes=("ADD", "MOVL")),
        lifted, (HERE.parent / "Tcg" / "Facts.lean.inc").read_text(encoding="utf-8"),
        (HERE / "Movl.lean.inc").read_text(encoding="utf-8"),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = check.lean(program)
    require(result.returncode == 0, result.stdout + result.stderr)
    print("MOVL: legal X2 bundle, integer write, NaT and MLX completion")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, IndexError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
