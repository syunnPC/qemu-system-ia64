#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify register-stack backing-store arithmetic and register mapping."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Decode.check import ROOT, c_function, expression, get_match, run_lean, uncomment
from Rse.frames import frame_updates
from Rse.registers import register_updates
from Rse.spill import spill_updates
from Rse.spill_ram import ram_updates


def body(source, name, signature=None):
    params, text = c_function(source, name)
    compact = lambda value: re.sub(r"\s+", "", value)
    if signature is not None:
        get_match(re.escape(compact(signature)), compact(params), name + " parameters", full=True)
    return compact(text)


def extract(source, name, pattern, signature=None):
    return get_match(pattern, body(source, name, signature), name, full=True).groups()


def expr(value, names=None, *, narrow=False):
    term = expression(value, names or {})
    return term.replace(" : Word", " : U32") if narrow else term


def macro(source, name):
    return get_match(rf"^#define {name}\s+([^\n]+)", source, name)[1]


def scalar(source, name, signature, aliases=None, *, narrow=False):
    (value,) = extract(source, name, r"return([^;]+);", signature)
    return expr(value, aliases, narrow=narrow)


def addresses(source):
    bit = scalar(source, "ia64_rse_collect_bit", "uint64_t addr", {"addr": "addr"})
    word = scalar(source, "ia64_rse_collect_word", "uint64_t addr", {"addr": "addr"})
    bound, full, partial = extract(source, "ia64_rse_rnat_low_mask",
                                   r"returnbit>=([^?]+)\?([^:]+):([^;]+);", "uint32_t bit")
    bit_names = {"bit": "(bit.zeroExtend 64)", "INT64_MAX": "(9223372036854775807 : Word)"}
    (divisor,) = extract(source, "ia64_rse_nat_word_count_slow",
                         r"returntotal/([^;]+);", "uint32_t total")
    fast_limit, first, second = extract(
        source, "ia64_rse_nat_word_count",
        r"if\(likely\(total<([^()]+)\)\)\{return\(total>=([^()]+)\)"
        r"\+\(total>=([^()]+)\);\}returnia64_rse_nat_word_count_slow\(total\);",
        "uint32_t total",
    )
    moves = []
    for name, lean_name in (("grow", "natGrowC"), ("shrink", "natShrinkC")):
        (total,) = extract(
            source, f"ia64_rse_nat_words_{name}",
            r"uint32_ttotal=([^;]+);returnia64_rse_nat_word_count\(total\);",
            "uint64_t addr, uint32_t nregs",
        )
        total = total.replace("ia64_rse_collect_bit(addr)", "slot")
        translated = expr(total, {"slot": "(collectBitC addr)", "nregs": "registers"}, narrow=True)
        moves.append(f"def {lean_name} (addr : Word) (registers : U32) : U32 := natCountC {translated}")
    first_nat, fallback, crossed, divisor_below, result = extract(
        source, "ia64_rse_register_words_below",
        r"uint32_tfirst_nat=([^;]+);uint32_tnat_words;"
        r"nat_words=words<first_nat\?([^:]+):(.+)/([^;]+);return([^;]+);",
        "uint64_t addr, uint32_t words",
    )
    below_names = {"first_nat": "firstNat", "words": "words", "nat_words": "natWords",
                   "slot": "(collectBitC addr)"}
    # Preserve the parentheses around the difference being divided.
    init, delta = get_match(r"([^+]+)\+\((.+)\)", crossed, "RNAT interval count", full=True).groups()
    first_nat = first_nat.replace("ia64_rse_collect_bit(addr)", "slot")

    return f"""
def collectBitC (addr : Word) : U32 := ({bit}).zeroExtend 32
def collectWordC (addr : Word) : Word := {word}
def definedNaTBitsC (bit : U32) : Word :=
  if {expr(bound, narrow=True)} ≤ bit then {expr(full, bit_names)} else {expr(partial, bit_names)}

def natCountSlowC (total : U32) : U32 := total / {expr(divisor, narrow=True)}
def natCountC (total : U32) : U32 :=
  if total < {expr(fast_limit, narrow=True)} then
    (if {expr(first, narrow=True)} ≤ total then 1 else 0) +
    (if {expr(second, narrow=True)} ≤ total then 1 else 0)
  else natCountSlowC total

{chr(10).join(moves)}

def payloadWordsBelowC (addr : Word) (words : U32) : U32 :=
  let firstNat := {expr(first_nat, below_names, narrow=True)}
  let natWords := if words < firstNat then {expr(fallback, narrow=True)} else
    {expr(init, narrow=True)} + {expr(delta, below_names, narrow=True)} /
                             {expr(divisor_below, narrow=True)}
  {expr(result, below_names, narrow=True)}
"""


def move_bsp(source):
    steps = []
    for func, name, operator, variable, count_call in (
        ("ia64_rse_preserve_frame", "advanceC", "+", "nregs", "grow"),
        ("ia64_rse_restore_frame", "retreatC", "-", "preserved", "shrink"),
    ):
        text = body(source, func)
        val = "nats" if count_call == "grow" else "preserved_nats"
        get_match(
            rf"{val}=ia64_rse_nat_words_{count_call}\(env->ar_bsp,{variable}\);",
            text, func + " RNAT count",
        )
        (args, scale) = get_match(
            rf"env->ar_bsp{re.escape(operator)}=\(uint64_t\)\(([^()]+)\)\*([^;]+);",
            text, func + " BSP update",
        ).groups()
        if count_call == "grow":
            get_match(r"if\(nregs==0\)\{return;\}", text, func + " zero case")
        args = expr(args, {variable: "registers", val: "nats"}, narrow=True)
        count_func = "natGrowC" if count_call == "grow" else "natShrinkC"
        formula = f"addr {operator} (({args}).zeroExtend 64 * {expr(scale)})"
        if count_call == "grow":
            formula = f"if registers = 0 then addr else {formula}"
        steps.append(f"""
def {name} (addr : Word) (registers : U32) : Word :=
  let nats := {count_func} addr registers
  {formula}
""")
    return "\n".join(steps)


def frame(source, header):
    fields = ("sof", "sol", "sor", "rrb_gr", "rrb_fr", "rrb_pr")
    names = {}
    for field in fields:
        get_match(rf"\buint8_t\s+cfm_{field};", header, "CFM field type")
        names[field] = f"({field}.zeroExtend 64)"
        if field != "sof":
            shift = f"IA64_CFM_{field.upper()}_SHIFT"
            names[shift] = expr(macro(header, shift))
    (value,) = extract(source, "ia64_rse_current_cfm", r"return([^;]+);",
                       "const CPUIA64State *env")
    value = expr(value.replace("env->cfm_", ""), names)
    return f"""
def frameMarkerC (sof sol : BitVec 7) (sor : BitVec 4) (rrb_gr rrb_fr : BitVec 7)
    (rrb_pr : BitVec 6) : Word := {value}
"""


def register_map(source, header):
    aliases = {}
    for name in ("IA64_GR_COUNT", "IA64_STACKED_GR_BASE", "IA64_STACKED_GR_COUNT"):
        aliases[name] = expr(macro(header, name), aliases, narrow=True)
    aliases |= {"sor": "(sor.zeroExtend 32)", "sof": "(sof.zeroExtend 32)",
                "rrb": "(rrb.zeroExtend 32)", "v": "v", "bol": "bol", "off": "off",
                "p": "p", "size": "size", "sor_regs": "size", "count": "size"}
    count, limit, max_size = extract(
        source, "ia64_rse_rotating_gr_count",
        r"uint32_tcount=\(uint32_t\)([^;]+);"
        r"returncount<=env->cfm_sof&&count<=([^?]+)\?count:([^;]+);",
        "const CPUIA64State *env",
    )
    sum_v, minus_v, bias, pmax, rebase = extract(
        source, "ia64_rse_virt_to_phys",
        r"uint32_tsor_regs=ia64_rse_rotating_gr_count\(env\);uint32_tp;"
        r"if\(v<sor_regs\)\{v\+=([^;]+);if\(v>=sor_regs\)\{v-=([^;]+);\}\}"
        r"p=([^;]+);returnp<([^?]+)\?p:([^;]+);",
        "const CPUIA64State *env,uint32_t v",
    )
    above, wrapped, inverse, undo = extract(
        source, "ia64_rse_phys_to_virt",
        r"uint32_toff=p>=env->rse.rse_bol\?([^:]+):([^;]+);"
        r"uint32_tsor_regs=ia64_rse_rotating_gr_count\(env\);"
        r"if\(off<sor_regs\)\{off\+=([^;]+);"
        r"if\(off>=sor_regs\)\{off-=([^;]+);\}\}returnoff;",
        "const CPUIA64State *env,uint32_t p",
    )

    def term(value):
        value = (value.replace("env->cfm_sor", "sor").replace("env->cfm_rrb_gr", "rrb")
                 .replace("env->rse.rse_bol", "bol"))
        return expr(value, aliases, narrow=True)

    return f"""
def rotateCountC (sof sor : BitVec 8) : U32 :=
  let size := {term(count)}
  if size ≤ sof.zeroExtend 32 ∧ size ≤ {term(limit)} then size else {term(max_size)}

def virtToPhysC (sof sor rrb : BitVec 8) (bol v : U32) : U32 :=
  let size := rotateCountC sof sor
  let v := if v < size then
    let v := v + {term(sum_v)}
    if size ≤ v then v - {term(minus_v)} else v
  else v
  let p := {term(bias)}
  if p < {term(pmax)} then p else {term(rebase)}

def physToVirtC (sof sor rrb : BitVec 8) (bol p : U32) : U32 :=
  let off := if bol ≤ p then {term(above)} else {term(wrapped)}
  let size := rotateCountC sof sor
  if off < size then
    let off := off + {term(inverse)}
    if size ≤ off then off - {term(undo)} else off
  else off
"""


def wrap(source, header):
    aliases = {}
    # The operands of the C fast path are signed and small; the fallback
    # C remainder truncates the quotient toward zero.
    for name in ("IA64_GR_COUNT", "IA64_STACKED_GR_BASE", "IA64_STACKED_GR_COUNT"):
        token = macro(header, name)
        for key, value in aliases.items():
            token = token.replace(key, f"({value})")
        get_match(r"[0-9 ()+-]+", token, "physical register count", full=True)
        aliases[name] = token
    names = aliases["IA64_STACKED_GR_COUNT"]
    (modulus, correction) = extract(
        source, "ia64_rse_wrap_phys_slow",
        r"idx%=\(int32_t\)([^;]+);if\(idx<0\)\{idx\+=([^;]+);\}returnidx;",
        "int32_t idx",
    )
    extent, ceiling, add, test, sub = extract(
        source, "ia64_rse_wrap_phys",
        r"if\(likely\(idx>=-\(int32_t\)([^&]+)&&idx<2\*\(int32_t\)([^)]+)\)\)\{"
        r"if\(idx<0\)\{idx\+=([^;]+);\}elseif\(idx>=([^)]*)\)\{idx-=([^;]+);\}"
        r"returnidx;\}returnia64_rse_wrap_phys_slow\(idx\);",
        "int32_t idx",
    )

    def number(value):
        expanded = value.replace("IA64_STACKED_GR_COUNT", names)
        return get_match(r"[0-9 ()+-]+", expanded, "physical wrap operand", full=True)[0]

    return f"""
def wrapSlowC (idx : Int) : Int :=
  let idx := idx.tmod ({number(modulus)})
  if idx < 0 then idx + ({number(correction)}) else idx

def wrapPhysC (idx : Int) : Int :=
  if -({number(extent)}) ≤ idx ∧ idx < 2 * ({number(ceiling)}) then
    if idx < 0 then idx + ({number(add)})
    else if ({number(test)}) ≤ idx then idx - ({number(sub)}) else idx
  else wrapSlowC idx
"""


def rotate(source):
    split, full, full_mask, limit_mask, carry_lo, low_short, rebase, high_mask, carry_hi, low_long, high = (
        extract(source, "ia64_rotate_low_bits_left_one",
                r"uint64_told0=bits\[0\];uint64_tmask;uint64_tcarry;"
                r"if\(count<=([^)]*)\)\{mask=count==([^?]+)\?([^:]+):([^;]+);"
                r"carry=([^;]+);bits\[0\]=([^;]+);return;\}"
                r"count-=([^;]+);mask=([^;]+);carry=([^;]+);"
                r"bits\[0\]=([^;]+);bits\[1\]=([^;]+);",
                "uint64_t bits[2], uint32_t count")
    )
    names = {"count": "n", "old0": "old0", "high": "old1", "mask": "mask", "carry": "carry",
             "UINT64_MAX": "(18446744073709551615 : Word)"}

    def term(value):
        return expr(value.replace("bits[1]", "high"), names)

    return f"""
def rotatePrefixC (bits : BitVec 128) (count : U32) : BitVec 128 :=
  let old0 := bits.extractLsb' 0 64
  let old1 := bits.extractLsb' 64 64
  let n := count.zeroExtend 64
  if n ≤ {term(split)} then
    let mask := if n = {term(full)} then {term(full_mask)} else {term(limit_mask)}
    let carry := {term(carry_lo)}
    old1 ++ {term(low_short)}
  else
    let n := n - {term(rebase)}
    let mask := {term(high_mask)}
    let carry := {term(carry_hi)}
    {term(high)} ++ {term(low_long)}
"""


def generate(root):
    source = uncomment((root / "target/ia64/arch/rse.c").read_text(encoding="utf-8"))
    header = uncomment((root / "target/ia64/cpu.h").read_text(encoding="utf-8"))
    spec = (HERE / "Spec.lean").read_text(encoding="utf-8")
    proofs = (HERE / "Proofs.lean.inc").read_text(encoding="utf-8")
    spec += (HERE / "RegisterSpec.lean.inc").read_text(encoding="utf-8")
    spec += (HERE / "SpillSpec.lean.inc").read_text(encoding="utf-8")
    spec += (HERE / "SpillRamSpec.lean.inc").read_text(encoding="utf-8")
    return spec + "\nnamespace RseImplementation\nopen RseSpec\n" + "\n".join([
        addresses(source), move_bsp(source), frame(source, header),
        register_map(source, header), wrap(source, header), rotate(source), proofs,
        frame_updates(root, source, header), (HERE / "Frame.lean.inc").read_text(encoding="utf-8"),
        register_updates(root, source, header),
        (HERE / "RegisterProofs.lean.inc").read_text(encoding="utf-8"),
        spill_updates(source), (HERE / "SpillProofs.lean.inc").read_text(encoding="utf-8"),
        ram_updates(root, source, header), (HERE / "SpillRamProofs.lean.inc").read_text(encoding="utf-8"),
        (HERE / "SpillRamExamples.lean.inc").read_text(encoding="utf-8"),
    ]) + "\nend RseImplementation\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--qemu", type=Path, help="trusted QEMU executable for live TCG slices")
    args = parser.parse_args()
    try:
        source = generate(args.source_root)
        if args.qemu:
            from Rse.live import generate as generate_live

            source += generate_live(args.qemu.resolve())
        if args.emit_lean:
            args.emit_lean.write_text(source, encoding="utf-8")
        else:
            with tempfile.TemporaryDirectory(prefix="ia64-rse-lean-") as tmp:
                work = Path(tmp)
                proof = work / "Implementation.lean"
                proof.write_text(source, encoding="utf-8")
                run_lean(work, proof)
            print("IA-64 RSE arithmetic, rotation, and resident frame transitions and mandatory spill prefixes verified")
            if args.qemu:
                print("IA-64 ALLOC and COVER frame theorems instantiated at emitted and optimized TCG calls")
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"IA-64 RSE check failed: {exc}\n")


if __name__ == "__main__":
    main()
