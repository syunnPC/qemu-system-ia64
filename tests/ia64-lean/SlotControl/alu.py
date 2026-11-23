#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose the native A1–A5 integer bundle decoders and TCG slot effects."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from FullDecode.forms import ALU
from Integer import check as integer
from SlotControl import add, bundle, check
from SlotControl.reify import fixed
from Tcg import check as scalar


# These active native opcode classifiers justify the normalized zero flags
# for the selected integer formats and keep FP, privilege and atomics
# checks out of this projected preparation path.
CLASSIFIERS = {
    "ia64_insn_must_start_group": "e6a9778cf2790e83a486dbc1b9f58121e1bd2fc7b4be80cd4a785b74b5ce1722",
    "ia64_insn_must_end_group": "2c42f939cf9a45b413f750acbf9317b2db59c706e7acb837bba590f65463c52b",
    "ia64_insn_requires_slot2": "09698a1915bb7dd5e63fa04569edf89310894e6c8a599ed65c95e9f6dad04976",
    "ia64_insn_has_invalid_fp_target": "45c0306fd45d9f1874843c03e6a961af33c53e352d6c2f8597629a03bdd51be8",
    "ia64_insn_has_illegal_register": "4efa09e06e7b784d8284540fc5dd9e18bfb7d05e31ae289f3d8cd7e220223449",
    "ia64_insn_has_reserved_ar": "77ae67d6e442b84ec6c568dd09fb882ac68faaf282276f4a167b66b3bbdeb5b6",
    "ia64_insn_has_reserved_mask_field": "f5ac762856557dedc8a216db747d9e76ba97e2fe68e499374af09731c1cb297e",
    "ia64_insn_psr_ic_must_be_clear": "79b33c238d4dc58bd8111efe4323a95a1aaf7383249625833c1dc9017a01309d",
    "ia64_insn_is_privileged": "38dc979633c187b6b6410a89988bb76e18ee51bdafecfb983a6c27ee901a39cd",
    "ia64_insn_needs_16byte_atomics": "423c5dd7cd14046830cd6858252cb66d54b97f64f4da4d1e7762a99af9277303",
    "ia64_gen_check_fp_pair_bank": "a232a426f3fac170472735b1a4b57e81015ce778234b4ea6ff124955d27ec9dd",
}


def require_classification(translator, opcode_table):
    ordinal = {opcode: n for n, opcode in
               enumerate(re.findall(r"IA64_OPCODE\((\w+),", opcode_table))}
    for name, digest in CLASSIFIERS.items():
        body = fixed(translator, name, digest)
        direct = set(re.findall(r"IA64_OP_(\w+)", body))
        integer.require(not direct.intersection(ALU), f"A1–A5 classification changed in {name}")
        for start, end in re.findall(r"opcode>=IA64_OP_(\w+)&&opcode<=IA64_OP_(\w+)", body):
            integer.require(not any(ordinal[start] <= ordinal[opcode] <= ordinal[end] for opcode in ALU),
                            f"integer ALU tag falls inside the {name} opcode range")


def dispatch(root):
    translator = scalar.source(root, "translate/translate.c")
    require_classification(translator, scalar.source(root, "decode/opcode.inc"))
    _, _, fast, _ = integer.fast_sections(translator)
    integer.require(ALU.keys() <= scalar.OPCODES.keys() and fast <= ALU.keys(),
                    "the A1–A5 decoder and scalar IR coverage disagree")
    constructors = "\n".join(f"  | {name}" for name in ALU)

    def alternatives(expression, names=ALU):
        return "\n".join(f"  | .{name} => {expression(name)}" for name in names)

    def cases(theorem):
        return "\n".join(f"  | {name} => exact {theorem(name)}" for name in ALU)

    formats = alternatives(ALU.__getitem__)
    decode = alternatives(lambda name: f"bundle{name}")
    spec = alternatives(lambda name: f"IA64Tcg.alu_{name}")
    bodies = alternatives(lambda name: f"IA64Tcg.sourceBody_{name} s i facts")
    selected = alternatives(lambda name: f"some (IA64Tcg.sourceFastStep_{name} s i facts)",
                            sorted(fast))
    branchless = alternatives(lambda name: str(name in fast).lower())
    decode_proofs = cases(lambda name: f"{name}_complete")
    source_proofs = cases(lambda name: f"IA64Tcg.sourceBody_{name}_eq s i facts sound")
    fast_proofs = cases(lambda name: f"congrArg some (IA64Tcg.fastStep_{name} s i facts sound gate)"
                        if name in fast else "rfl")
    return f"""
namespace IA64Alu
open FullDecodeSpec FullDecodeImplementation

inductive Opcode where
{constructors}

def format : Opcode → Form
{formats}

def decode : Opcode → Word → Word → Word → BitVec 2 → Observation
{decode}

def operation : Opcode → IA64Tcg.Alu
{spec}

def conditionalFast : Opcode → Bool
{branchless}

def ordinaryBody (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    Opcode → IA64Tcg.State α
{bodies}

def fastStep (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts) :
    Opcode → Option (IA64Tcg.Outcome α)
{selected}
  | _ => none

theorem decodeComplete (tag : Opcode) :
    BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit (format tag) (decode tag) := by
  cases tag with
{decode_proofs}

theorem ordinaryBody_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (tag : Opcode) (sound : IA64Tcg.Sound s i facts) :
    ordinaryBody s i facts tag = IA64Tcg.SDM.operate s i (operation tag) := by
  cases tag with
{source_proofs}

theorem fastStep_eq (s : IA64Tcg.State α) (i : IA64Tcg.Slot) (facts : IA64Tcg.Facts)
    (tag : Opcode) (sound : IA64Tcg.Sound s i facts) (gate : IA64Tcg.FastApplicability i facts) :
    fastStep s i facts tag =
      if conditionalFast tag then some (IA64Tcg.sdStep s i (operation tag)) else none := by
  cases tag with
{fast_proofs}

end IA64Alu
"""


def generate(root):
    return "\n\n".join([
        add.generate(root, check.generate(root), opcodes=ALU),
        bundle.lift_decoder(root, ALU),
        (HERE / "Bundle.lean.inc").read_text(encoding="utf-8"),
        dispatch(root),
        (HERE / "Alu.lean.inc").read_text(encoding="utf-8"),
    ])


def shortened_immediate(root, work):
    relative = Path("decode/decode.c")
    source = root / "target/ia64"
    overlay = work / "target/ia64"
    overlay.mkdir(parents=True)
    for part in relative.parts:
        for entry in source.iterdir():
            if entry.name != part:
                (overlay / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
        source /= part
        overlay /= part
        if source.is_dir():
            overlay.mkdir()
    text = source.read_text(encoding="utf-8")
    mutant, count = re.subn(r"(ia64_bits\(raw, 36, 1\) << 13\),\s*)14\);",
                            r"\g<1>13);", text)
    integer.require(count == 1, "missing A4 sign-extension mutation anchor")
    overlay.write_text(mutant, encoding="utf-8")
    return work


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    if args.coverage:
        print(json.dumps({f"tcg:{name}": "Native A1–A5 bundle through projected retirement or first request"
                          for name in ALU}, sort_keys=True))
        return
    program = generate(args.source_root)
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = check.lean(program)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    print(f"Slot control: {len(ALU)} A1–A5 opcodes refine through retirement or first helper request")
    if args.mutation_test:
        with tempfile.TemporaryDirectory(prefix="ia64-alu-decoder-mutation-") as temp:
            mutant = generate(shortened_immediate(args.source_root, Path(temp)))
        result = check.lean(mutant)
        if result.returncode != 1 or "counterexample" not in result.stdout.lower():
            raise SystemExit("The altered A4 immediate was not rejected in Lean.\n"
                             + result.stdout + result.stderr)
        print("Slot control: Lean rejected shortening A4's signed immediate")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
