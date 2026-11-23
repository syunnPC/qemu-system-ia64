#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify IA-64 local SAPIC and PSR mask operations."""

import argparse
from itertools import product
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "Decode"))
from check import ROOT, c_function, expression, get_match, run_lean, uncomment

IRR_FUNCTIONS = (
    ("bool", "sapic_vector_active"),
    ("int", "sapic_vector_priority"),
    ("int", "sapic_vector_priority_class"),
    ("bool", "sapic_vector_unmasked"),
    ("int", "sapic_find_highest_normal"),
    ("int", "sapic_find_irr"),
    ("int", "sapic_find_isr"),
    ("int", "ia64_sapic_accept"),
    ("void", "ia64_sapic_eoi"),
    ("int", "ia64_sapic_get_ivr"),
)
SAPIC_CONSTANTS = ("IA64_CR_SAPIC_TPR", "IA64_SPURIOUS_VECTOR", "IA64_TPR_MIC_MASK",
                   "IA64_TPR_MMI")


def macro(source, name):
    if name == "IA64_CR_SAPIC_TPR":
        return get_match(rf"^\s*{name}\s*=\s*([^,]+),", source, name)[1]
    return get_match(rf"^#define {name}\s+([^\n]+)", source, name)[1]


def compact(value):
    return re.sub(r"\s+", "", value)


def body(source, name):
    return compact(c_function(source, name)[1])


def signed_value(text, names):
    if text.startswith("-"):
        return f"(-{expression(text[1:], names)})"
    return expression(text, names)


def project_sapic(source, header):
    names = {name: expression(macro(header, name), {}) for name in SAPIC_CONSTANTS}
    names["vector"] = "v"
    nmi, nmi_rank, ext, ext_rank, base, normal, ignored = get_match(
        r"if\(vector==([^()]+)\)\{return([^;]+);\}"
        r"if\(vector==([^()]+)\)\{return([^;]+);\}"
        r"if\(vector>=([^()]+)\)\{return([^;]+);\}return([^;]+);",
        body(source, "sapic_vector_priority"), "SAPIC priority", full=True,
    ).groups()
    c_base, c_class, c_ignored = get_match(
        r"returnvector>=([^?]+)\?([^:]+):([^;]+);",
        body(source, "sapic_vector_priority_class"), "SAPIC class", full=True,
    ).groups()
    n, e, ext_masked, class_masked, floor = get_match(
        r"uint64_ttpr=env->cr\[IA64_CR_SAPIC_TPR\];intisr=sapic_find_isr\(env\);"
        r"intvector_priority=sapic_vector_priority\(vector\);"
        r"intvector_class=sapic_vector_priority_class\(vector\);"
        r"if\(vector_priority<=sapic_vector_priority\(isr\)\)\{returnfalse;\}"
        r"if\(vector==([^()]+)\)\{returntrue;\}"
        r"if\(vector==([^()]+)\)\{return!\(([^()]*)\);\}"
        r"return!\(([^()]*)\)&&\(\(uint64_t\)vector_class>\((.+)\)\);",
        body(source, "sapic_vector_unmasked"), "SAPIC masking", full=True,
    ).groups()
    names["tpr"] = "tpr"
    return f"""
def cRank (pending : Vector) : Word :=
  let v := pending.zeroExtend 64
  if v = {expression(nmi, names)} then {signed_value(nmi_rank, names)}
  else if v = {expression(ext, names)} then {signed_value(ext_rank, names)}
  else if {expression(base, names)} ≤ v then {signed_value(normal, names)}
  else {signed_value(ignored, names)}

def cClass (pending : Vector) : Word :=
  let v := pending.zeroExtend 64
  if {expression(c_base, names)} ≤ v then {signed_value(c_class, names)}
  else {signed_value(c_ignored, names)}

def cPermitted (pending inService : Vector) (tpr : Word) : Prop :=
  let v := pending.zeroExtend 64
  (cRank pending).sle (cRank inService) = false ∧
    (v = {expression(n, names)} ∨
      (v ≠ {expression(n, names)} ∧
       ((v = {expression(e, names)} ∧ {expression(ext_masked, names)} = 0) ∨
        (v ≠ {expression(e, names)} ∧ {expression(class_masked, names)} = 0 ∧
         {expression(floor, names)} < cClass pending))))

"""


def project_psr(root, header):
    arch = uncomment((root / "target/ia64/arch/system.c").read_text())
    translate = uncomment((root / "target/ia64/translate/translate.c").read_text())
    gen = compact(uncomment((root / "target/ia64/translate/gen-system.c").read_text()))
    helper = uncomment((root / "target/ia64/helper/helper-system.c").read_text())
    names = {name: expression(macro(header, name), {})
             for name in ("IA64_PSR_UM_WRITABLE_MASK", "IA64_PSR_SP", "IA64_PSR_UP")}
    definitions = []
    for prefix in ("ssm", "rsm"):
        cap = prefix.upper()
        get_match(
            rf"ia64_system_{prefix}\(env,imm\);",
            body(helper, f"helper_{prefix}"), f"{prefix} helper route", full=True,
        )
        mask = get_match(
            rf"caseIA64_OP_{cap}:ia64_gen_check_reserved_bits\(insn,tcg_constant_i64"
            rf"\(op->immediate\),([^;]+)\);ia64_gen_check_virtualization\(insn\);"
            rf"gen_helper_{prefix}\(tcg_env,tcg_constant_i64\(op->immediate\)\);"
            r"ia64_request_exit_after_bundle\(ctx\);break;",
            gen, f"{prefix} translator route",
        )[1]
        result = get_match(
            r"uint64_told_psr=env->psr;ia64_set_psr\(env,([^;]+)\);"
            r"ia64_flush_on_pk_change\(env,old_psr\);",
            body(arch, f"ia64_system_{prefix}"), f"{prefix} PSR update", full=True,
        )[1]
        definitions.append(f"def c{cap}Mask : Word := {expression(mask, {})}")
        result = expression(result.replace("env->psr", "psr"), {"psr": "psr", "imm": "imm"})
        definitions.append(f"def c{cap} (psr imm : Word) : Word := {result}")

    privileged = body(translate, "ia64_insn_is_privileged")
    get_match(r"caseIA64_OP_SSM:caseIA64_OP_RSM:(?:caseIA64_OP_\w+:)*returntrue;",
              privileged, "system mask privilege gate")
    get_match(r"if\(ia64_insn_is_privileged\(insn\)\)\{"
              r"ia64_gen_check_privileged\(insn\);\}",
              compact(translate), "translation privilege check")
    get_match(r"env->psr=value;", body(arch, "ia64_set_psr"), "PSR register update")

    # The user-mask writers project the data update. The reserved-bit and
    # predicate checks precede these TCG sequences.
    get_match(
        r"caseIA64_OP_RUM:caseIA64_OP_SUM_UM:returninsn->operands.common.immediate&~0x3eULL;",
        body(translate, "ia64_insn_has_reserved_mask_field"), "user mask reserved check",
    )
    write_mask = ("TCGv_i64new_psr=tcg_temp_new_i64();TCGv_i64new_um=tcg_temp_new_i64();"
                  "TCGv_i64secure=tcg_temp_new_i64();TCGv_i64old_up=tcg_temp_new_i64();"
                  "TCGLabel*done=gen_new_label();"
                  "tcg_gen_andi_i64(new_psr,cpu_psr,~IA64_PSR_UM_WRITABLE_MASK);"
                  "tcg_gen_andi_i64(new_um,value,IA64_PSR_UM_WRITABLE_MASK);"
                  "tcg_gen_or_i64(new_psr,new_psr,new_um);"
                  "tcg_gen_andi_i64(secure,cpu_psr,IA64_PSR_SP);"
                  "tcg_gen_brcondi_i64(TCG_COND_EQ,secure,0,done);"
                  "tcg_gen_andi_i64(new_psr,new_psr,~IA64_PSR_UP);"
                  "tcg_gen_andi_i64(old_up,cpu_psr,IA64_PSR_UP);"
                  "tcg_gen_or_i64(new_psr,new_psr,old_up);gen_set_label(done);"
                  "tcg_gen_mov_i64(cpu_psr,new_psr);")
    get_match(re.escape(write_mask), body(translate, "ia64_gen_write_user_mask"),
              "TCG user mask writer", full=True)
    for name in ("SUM_UM", "RUM"):
        op = "or" if name == "SUM_UM" else "and"
        mask, value = get_match(
            rf"caseIA64_OP_{name}:\{{uint64_tmask=([^;]+);"
            r"TCGv_i64value=tcg_temp_new_i64\(\);if\(mask==0\)\{break;\}"
            rf"tcg_gen_{op}i_i64\(value,cpu_psr,([^;]+)\);"
            r"ia64_gen_write_user_mask\(value\);",
            gen, f"{name} TCG data operation",
        ).groups()
        updated = expression(value, {"mask": "mask"})
        user_mask = names["IA64_PSR_UM_WRITABLE_MASK"]
        up = names["IA64_PSR_UP"]
        sp = names["IA64_PSR_SP"]
        mask = expression(mask.replace("op->immediate", "imm"), names | {"imm": "imm"})
        operator = "|||" if name == "SUM_UM" else "&&&"
        definitions.append(f"""
def c{name} (psr imm : Word) : Word :=
  let mask := {mask}
  if mask = 0 then psr else
    let value := psr {operator} {updated}
    let merged := (psr &&& ~~~{user_mask}) ||| (value &&& {user_mask})
    if psr &&& {sp} = 0 then merged
    else (merged &&& ~~~{up}) ||| (psr &&& {up})
""")
    return "\n".join(definitions)


THEOREMS = """
theorem cRank_correct (v : Vector) : cRank v + 1 = priority v := by
  simp only [cRank, priority]
  bv_decide

theorem cPermitted_correct (pending inService : Vector) (tpr : Word) :
    cPermitted pending inService tpr ↔ permittedCondition pending inService tpr := by
  simp only [cPermitted, cClass, cRank, permittedCondition, priority]
  bv_decide

theorem cSystemMasks : cSSMMask = sysMask ∧ cRSMMask = sysMask := by decide

theorem cSSM_correct (psr imm : Word) (legal : imm &&& ~~~cSSMMask = 0) :
    cSSM psr imm = changePSR psr imm sysMask true := by
  simp only [cSSM, changePSR, sysMask]
  unfold cSSMMask at legal
  bv_decide

theorem cRSM_correct (psr imm : Word) (legal : imm &&& ~~~cRSMMask = 0) :
    cRSM psr imm = changePSR psr imm sysMask false := by
  simp only [cRSM, changePSR, sysMask]
  unfold cRSMMask at legal
  bv_decide

theorem cSUM_correct (psr imm : Word) :
    cSUM_UM psr imm = changeUserPSR psr imm true := by
  simp only [cSUM_UM, changeUserPSR, changePSR, userMask]
  bv_decide

theorem cRUM_correct (psr imm : Word) :
    cRUM psr imm = changeUserPSR psr imm false := by
  simp only [cRUM, changeUserPSR, changePSR, userMask]
  bv_decide
"""


def bitmap(vectors):
    words = [0] * 4
    for v in vectors:
        words[v // 64] |= 1 << (v % 64)
    return words


def sapic_samples():
    for vector, mic, mmi in product(range(256), range(16), range(2)):
        yield (mic << 4) | (mmi << 16), 0, bitmap([vector]), bitmap([])
    edge = (0, 2, 15, 16, 17, 31, 32, 63, 64, 127, 128, 191, 192, 239, 240, 255)
    for pending, current, (mic, mmi), eoi in product(
        edge, edge, ((0, 0), (1, 0), (7, 0), (15, 0), (0, 1)), (0, 1),
    ):
        yield (mic << 4) | (mmi << 16), eoi, bitmap([16, pending]), bitmap([current])
    for valid in (range(256), range(16, 256), range(256, 0, -2), range(1, 256, 2)):
        vr = tuple(v for v in valid if v < 256)
        for mic, mmi, eoi in product(range(16), range(2), (0, 1)):
            yield (mic << 4) | (mmi << 16), eoi, bitmap(vr), bitmap(vr[1::2])


def check_sapic_c(work, source, header):
    lines = [f"#define {name} {macro(header, name)}" for name in SAPIC_CONSTANTS]
    lines.append("static int sapic_find_isr(CPUIA64State *env);")
    for ctype, name in IRR_FUNCTIONS:
        params, implementation = c_function(source, name)
        lines.append(f"{ctype} {name}({params}) {{\n{implementation}\n}}")
    (work / "sapic.c").write_text("\n".join(lines) + "\n")
    executable = work / "observe"
    cc = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run([*cc, "-std=c11", "-O2", "-I", str(work),
                    str(HERE / "observe.c"), "-o", str(executable)], check=True)
    rows = [(tpr, eoi, *irr, *isr) for tpr, eoi, irr, isr in sapic_samples()]
    stdin = "".join(f"{eoi} {tpr:x} " + " ".join(f"{v:x}" for v in bits) + "\n"
                    for tpr, eoi, *bits in rows)
    outputs = subprocess.run([str(executable)], input=stdin, text=True, check=True,
                             capture_output=True).stdout.splitlines()
    data = work / "observations.txt"
    data.write_text("".join(" ".join(map(str, row)) + f" {seen}\n"
                            for row, seen in zip(rows, outputs, strict=True)))
    profile = work / "SAPICObserve.lean"
    profile.write_text("import System.Spec\ndef main := IA64.System.checkRows\n")
    run_lean(work, profile, "--run", script_args=(str(data),))
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    options = parser.parse_args()
    root = options.source_root.resolve()
    source = uncomment((root / "target/ia64/arch/interrupt.c").read_text())
    header = uncomment((root / "target/ia64/cpu.h").read_text())
    with tempfile.TemporaryDirectory(prefix="ia64-system-lean-") as tmp:
        work = Path(tmp)
        (work / "System").mkdir()
        run_lean(work, HERE / "Spec.lean", "-o", str(work / "System/Spec.olean"))
        proof = work / "SystemSource.lean"
        proof.write_text("import System.Spec\nopen IA64.System\n" +
                         project_sapic(source, header) + project_psr(root, header) + THEOREMS)
        run_lean(work, proof)
        count = check_sapic_c(work, source, header)
    print(f"IA-64 SAPIC priority, masks and PSR updates verified ({count} C IVR/EOI states)")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"IA-64 system check failed: {exc}") from None
