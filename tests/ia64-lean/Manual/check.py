#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check selected printed SDM operations against the Lean integer reference."""

import argparse
from hashlib import sha256
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import HOST_CALLS, OPERANDS, emit, require, stripped
from sdm import MANUALS, NATIVE_PAGE, extract_native


# Manual revision 2.3, volume 3. Form selection uses the printed format
# completers. Only the Operation blocks are translated by the extractor.
FAMILIES = {
    "add": (14, ("ADD", "ADD_ONE", "ADDS", "ADDL")),
    "and": (18, ("AND", "AND_IMM")),
    "andcm": (19, ("ANDCM", "ANDCM_IMM")),
    "movl": (187, ("MOVL",)),
    "or": (194, ("OR", "OR_IMM")),
    "shladd": (245, ("SHLADD",)),
    "sub": (256, ("SUB", "SUB_ONE", "SUB_IMM")),
    "sxt": (258, ("SXT1", "SXT2", "SXT4")),
    "xor": (279, ("XOR", "XOR_IMM")),
    "zxt": (280, ("ZXT1", "ZXT2", "ZXT4")),
}


def source_pdf(directories, supplied):
    document, digest = MANUALS[3]
    candidates = [supplied] if supplied else [
        path for folder in directories for path in sorted(folder.glob("*.pdf"))]
    for path in candidates:
        if path.is_file() and sha256(path.read_bytes()).hexdigest() == digest:
            return path
    raise ValueError(f"Missing pinned SDM volume 3 ({document})")


def operation_pages(text):
    pages = {}
    for page in text.split("\f"):
        number = NATIVE_PAGE.search(page)
        header = re.search(r"(?m)^([a-z][a-z0-9]+) — ", page)
        if number is None or header is None or header[1] not in FAMILIES:
            continue
        family = header[1]
        require(family not in pages, f"Duplicate SDM family: {family}")
        require(int(number[1] or number[2]) == FAMILIES[family][0],
                f"Unexpected printed page for {family}")
        operation = re.search(r"(?ms)^Operation:\s*(.*?)^Interruptions:", page)
        require(operation is not None, f"Missing complete Operation block for {family}")
        pages[family] = operation[1]
    require(pages.keys() == FAMILIES.keys(),
            "Missing selected SDM families: " + ", ".join(FAMILIES.keys() - pages.keys()))
    return pages


def line_statement(lines, start):
    require(start < len(lines), "Unterminated manual operation statement")
    line = lines[start]
    branch = re.fullmatch(r"if\s*\((.*)\)", line)
    if branch:
        yes, end = line_statement(lines, start + 1)
        result = f"if ({branch[1]}) {{ {yes} }}"
        if end < len(lines) and lines[end].startswith("else"):
            continuation = lines[end][4:].strip()
            if continuation:
                alternate = lines.copy()
                alternate[end] = continuation
                no, end = line_statement(alternate, end)
            else:
                no, end = line_statement(lines, end + 1)
            result += f" else {{ {no} }}"
        return result, end
    require(re.fullmatch(r"\w+\s*=\s*[^;]+;", line),
            f"Unexpected selected manual operation statement: {line}")
    return line, start + 1


def translate(operation):
    normalized = stripped(operation)
    replacements = {
        "GR[r1].nat": "nat", "GR[r2].nat": "r2nat", "GR[r3].nat": "r3nat",
        "GR[r1]": "value", "GR[r2]": "x", "GR[r3]": "y",
        "register_form": "(register != 0)", "imm14_form": "(form14 != 0)",
        "plus1_form": "(extra != 0)", "minus1_form": "(extra != 0)",
        "||": "|",
    }
    for old, new in replacements.items():
        normalized = normalized.replace(old, new)
    lines = [line.strip() for line in normalized.splitlines() if line.strip()]
    require(lines[:2] == ["if (PR[qp]) {", "check_target_register(r1);"] and lines[-1] == "}",
            "Unsupported qualifying or target legality checks in printed Operation")
    lines = lines[2:-1]
    statements = []
    index = 0
    while index < len(lines):
        statement, index = line_statement(lines, index)
        statements.append(statement)
    require(statements, "Empty selected Operation block")
    return "if (immediate) { " + " ".join(statements) + " }"


def make_theorem(family, opcode, source):
    reg = not (opcode.endswith("_IMM") or opcode in {"ADDS", "ADDL", "MOVL"})
    extra = opcode in {"ADD_ONE", "SUB_ONE"}
    width = {"ADDS": 14, "ADDL": 22}.get(opcode, 8)
    xsz = int(opcode[-1]) if family in {"sxt", "zxt"} else 0
    values = {
        "value": "old", "nat": "oldNaT", "x": "x", "y": "y",
        "imm8": "raw", "imm14": "raw", "imm22": "raw", "imm64": "raw",
        "count2": "raw", "xsz": f"{xsz}#64", "immediate": "qp",
        "r2nat": "(SDM.natBit n2)", "r3nat": "(SDM.natBit n3)",
        "register": f"{int(reg)}#64", "form14": f"{int(width == 14)}#64",
        "extra": f"{int(extra)}#64",
    }
    emit(source, values)
    expected_operands = OPERANDS[opcode]
    if opcode == "MOVL" or family == "shladd":
        expected_operands = expected_operands.replace("imm", "raw")
    else:
        expected_operands = expected_operands.replace(
            "imm", f"((raw.extractLsb' 0 {width}).signExtend 64)")
    reads2 = str("x" in OPERANDS[opcode].split()).lower()
    reads3 = str("y" in OPERANDS[opcode].split()).lower()
    related = {
        "ADDL": "SDM.ADDS", "AND_IMM": "SDM.AND", "ANDCM_IMM": "SDM.ANDCM",
        "OR_IMM": "SDM.OR", "XOR_IMM": "SDM.XOR", "SUB_IMM": "SDM.SUB",
    }
    names = [f"SDM.{opcode}", "manualSignExtend", "manualZeroExtend",
             "SDM.qualifiedWrite", "SDM.sourceNaT", "SDM.natBit", "BitVec.and_comm",
             "BitVec.or_comm", "BitVec.add_comm", "BitVec.xor_comm"]
    if opcode in related:
        names.append(related[opcode])
    body = []
    for field, expected in (
        ("value", f"SDM.qualifiedWrite qp old (SDM.{opcode} {expected_operands})"),
        ("nat", f"SDM.qualifiedWrite qp oldNaT "
                f"(SDM.natBit (SDM.sourceNaT {reads2} {reads3} n2 n3))"),
    ):
        body.append(
            f"theorem printed_{opcode}_{field} (x y raw old oldNaT : Word) (qp n2 n3 : Bool) :\n"
            f"    {values[field]} = {expected} := by\n"
            f"  simp only [{', '.join(names)}] <;> bv_decide"
        )
    return "\n\n".join(body)


def generate(pages):
    HOST_CALLS.update({"sign_ext": "manualSignExtend", "zero_ext": "manualZeroExtend"})
    definitions = [
        "def manualSignExtend (x width : Word) : Word :=\n"
        "  let count := 64#64 - width\n"
        "  (x <<< count).sshiftRight' count",
        "def manualZeroExtend (x width : Word) : Word :=\n"
        "  x &&& ((1#64 <<< width) - 1)",
    ]
    for family, operation in pages.items():
        source = translate(operation)
        definitions.extend(make_theorem(family, opcode, source) for opcode in FAMILIES[family][1])
    return "\n\n".join(definitions)


def run_lean(proofs):
    spec = (HERE.parent / "Integer/Spec.lean").read_text()
    header = ("\nnamespace IA64Integer\nlocal notation \"Word\" => BitVec 64\n"
              "set_option linter.unusedVariables false\n")
    with tempfile.TemporaryDirectory(prefix="ia64-lean-manual-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(spec + header + proofs + "\nend IA64Integer\n")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdm-dir", type=Path, action="append", default=[])
    parser.add_argument("--volume3", type=Path)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check that changing an extracted addition to subtraction fails")
    args = parser.parse_args()
    path = source_pdf(args.sdm_dir, args.volume3)
    text = subprocess.check_output(["pdftotext", "-layout", str(path), "-"], encoding="utf-8")
    pages = operation_pages(text)
    checked = run_lean(generate(pages))
    require(checked.returncode == 0, checked.stdout + checked.stderr)
    all_families = {key for key, _ in extract_native(text) if key.startswith("sdm-native:")}
    forms = sum(len(opcodes) for _, opcodes in FAMILIES.values())
    locations = ", ".join(f"3:{page}" for page, _ in FAMILIES.values())
    print(f"Manual: {len(pages)}/{len(all_families)} native reference families, "
          f"{forms} forms, value and NaT projections ({locations})")
    if args.mutation_test:
        mutant = pages["add"].replace("tmp_src + GR[r3];", "tmp_src - GR[r3];")
        require(mutant != pages["add"], "Could not locate printed ADD mutation target")
        checked = run_lean(generate({"add": mutant}))
        require(checked.returncode != 0 and "counterexample" in checked.stdout,
                "Lean did not reject the changed printed ADD operation\n"
                + checked.stdout + checked.stderr)
        print("Manual: Lean rejected a change to the extracted ADD expression")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
