#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Follow the actual A1–A5 Illegal Operation branch across the TCG helper."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from ExceptionEntry import check
from SlotControl import alu


def source_fault_code(root):
    translator = check.uncomment((root / "target/ia64/translate/translate.c").read_text(encoding="utf-8"))
    header = check.uncomment((root / "target/ia64/cpu.h").read_text(encoding="utf-8"))
    body = check.body(translator, "ia64_gen_check_gr_in_frame")
    calls = r"ia64_gen_raise_exception\((IA64_EXCP_\w+),insn->address,insn->raw,insn->slot\)"
    zero, = check.get_match(r"if\(reg==0\)\{" + calls + r";return;\}", body,
                           "A1–A5 target register zero request").groups()
    frame, = check.get_match(
        re.escape("tcg_gen_brcondi_i32(TCG_COND_GTU,cfm_sof,reg-IA64_STACKED_GR_BASE,valid);")
        + calls + re.escape(";gen_set_label(valid);"), body, "A1–A5 out-of-frame request",
    ).groups()
    if len(re.findall(r"ia64_gen_raise_exception\(", body)) != 2:
        raise ValueError("unexpected destination register exception request")
    check.expect(
        check.body(translator, "ia64_gen_prepare_insn"),
        "elseif(ia64_insn_writes_gr_r1(insn)){"
        "ia64_gen_check_gr_in_frame(insn,insn->operands.common.destination);}",
        "scalar ALU destination reaches the frame check", full=False,
    )
    codes = [int(check.get_match(rf"\b{name}\s*=\s*(\d+)\s*,", header,
                                 f"{name} enum value")[1]) for name in (zero, frame)]
    return f"""
namespace IA64ExceptionEntry

def cIntegerTargetFault (dst : IA64Tcg.Reg) : Word := if dst.val = 0 then {codes[0]} else {codes[1]}

end IA64ExceptionEntry
"""


def generate(root):
    upstream = alu.generate(root)
    entry = re.sub(r"^import [^\n]+\n", "", check.generate(root, include_slot_spec=False), flags=re.M)
    return "\n\n".join([upstream, entry, source_fault_code(root),
                        (HERE / "AluProof.lean.inc").read_text(encoding="utf-8")])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=check.ROOT)
    parser.add_argument("--emit-lean", type=Path)
    options = parser.parse_args()
    try:
        src = generate(options.source_root.resolve())
        if options.emit_lean:
            options.emit_lean.write_text(src, encoding="utf-8")
            return
        with tempfile.TemporaryDirectory(prefix="ia64-exception-alu-") as tmp:
            work = Path(tmp)
            proof = work / "Check.lean"
            proof.write_text(src, encoding="utf-8")
            check.run_lean(work, proof, "-DwarningAsError=true")
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"A1–A5 exception entry check failed: {exc}\n")
    print("IA-64 A1–A5 ordinary Illegal Operation bundle route through vector entry verified")


if __name__ == "__main__":
    main()
