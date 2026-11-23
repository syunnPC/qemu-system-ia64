#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Relate native integer memory width and compare selectors to the SDM."""

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from fused import FUSED_BODIES, check_fused_release_loop, check_source
from source import body, compact, require

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
TARGET = Path("target/ia64")
MEMORY = TARGET / "arch/memory.c"
TRANSLATOR = TARGET / "translate/translate.c"
CPU = TARGET / "cpu.c"
CPU_HEADER = TARGET / "cpu.h"
TRANSLATOR_HEADER = TARGET / "translate/translate.h"
MEMOP = Path("include/exec/memop.h")


def capture(pattern, source, detail):
    match = re.fullmatch(pattern, source)
    require(match is not None, f"unrecognized implementation for {detail}")
    return match.groups()


def macro(source, name, pattern):
    found = re.search(rf"^#define\s+{name}\s+(.+)$", source, flags=re.MULTILINE)
    require(found is not None, f"missing macro {name}")
    return capture(pattern, compact(found[1]), name)


def widths(source):
    (rows,) = capture(
        r"switch\(opcode\)\{((?:(?:caseIA64_OP_\w+:)+returnMO_\w+;)+)"
        r"default:break;\}g_assert_not_reached\(\);",
        body(source, "ia64_memop_for_opcode"), "opcode width table",
    )
    actual = {}
    for cases, descriptor in re.findall(
        r"((?:caseIA64_OP_\w+:)+)return(MO_\w+);", rows
    ):
        for name in re.findall(r"caseIA64_OP_(\w+):", cases):
            require(name not in actual, f"duplicate width row for {name}")
            actual[name] = descriptor
    expected = {}
    # SDM Vol. 3 ld/st, cmpxchg, fetchadd and xchg, by size completer.
    names_by_size = {
        size: [f"LD{size}{suffix}" for suffix in ("", "S", "A", "SA", "C_CLR", "C_NC")]
        + [f"ST{size}", f"ST{size}REL", f"XCHG{size}", f"CMPXCHG{size}"]
        for size in (1, 2, 4, 8)
    }
    names_by_size[4] += ["FETCHADD4"]
    names_by_size[8] += ["FETCHADD8", "LD8FILL", "ST8SPILL"]
    for size, names in names_by_size.items():
        for name in names:
            expected[name] = size
    require(actual.keys() == expected.keys(),
            "integer memory descriptor coverage changed: "
            + ", ".join(sorted(actual.keys() ^ expected.keys())))
    return actual, expected


def descriptor_constants(source):
    uncommented = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    numeric = {}
    for key, value in re.findall(r"(?m)^\s*MO_(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)\s*,",
                                 uncommented):
        numeric[f"MO_{key}"] = int(value, 0)
    for key, value in re.findall(r"(?m)^\s*MO_(\w+)\s*=\s*(MO_\w+)\s*,",
                                 uncommented):
        if value in numeric:
            numeric[f"MO_{key}"] = numeric[value]
    for key, value in re.findall(r"(?m)^\s*MO_(LEU\w+)\s*=\s*MO_LE\s*\|\s*(MO_\w+)\s*,",
                                 uncommented):
        numeric[f"MO_{key}"] = numeric[value]
    require("#ifHOST_BIG_ENDIANMO_LE=MO_BSWAP,MO_BE=0,#elseMO_LE=0,MO_BE=MO_BSWAP,#endif"
            in compact(uncommented), "unsupported host endian descriptor constants")
    return numeric


def endian_selection(source, name, flag, constants):
    size, byte, clear_byte, clear_wide, be, le = capture(
        rf"if\(\(memop&(MO_\w+)\)==(MO_\w+)\)\{{returnmemop&~(MO_\w+);\}}"
        rf"return\(memop&~(MO_\w+)\)\|"
        rf"\({re.escape(flag)}\?(MO_\w+):(MO_\w+)\);",
        body(source, name), name,
    )

    def value(token):
        if token in ("MO_LE", "MO_BE"):
            host_order = "hostBig" if token == "MO_LE" else "!hostBig"
            return f"(if {host_order} then {constants['MO_BSWAP']}#32 else 0#32)"
        return f"{constants[token]}#32"

    return (
        f"if (descriptor &&& {value(size)}) == {value(byte)} then\n"
        f"  descriptor &&& ~~~{value(clear_byte)}\n"
        f"else (descriptor &&& ~~~{value(clear_wide)}) |||\n"
        f"  (if guestBig then {value(be)} else {value(le)})\n"
    )


def compare_bounds(memory, translator):
    part = body(memory, "ia64_cmpxchg_compare_representable")
    (rows,) = capture(
        r"switch\(size\)\{((?:case\d+:return(?:cmp<=UINT\d+_MAX|true);)+)"
        r"default:g_assert_not_reached\(\);\}", part, "runtime CCV guard",
    )
    runtime_limits = {}
    for size, bound in re.findall(r"case(\d+):return(cmp<=UINT\d+_MAX|true);", rows):
        runtime_limits[int(size)] = ((1 << int(re.search(r"UINT(\d+)", bound)[1])) - 1
                                     if bound != "true" else (1 << 64) - 1)
    require(set(runtime_limits) == {1, 2, 4, 8}, "unsupported CCV widths")
    fast = body(translator, "ia64_gen_atomic_cmpxchg_i64")
    match = re.search(
        r"if\(size<(\d+)\)\{load_only=gen_new_label\(\);"
        r"tcg_gen_brcondi_i64\(TCG_COND_GTU,cmp,"
        r"\((\d+)ULL<<\(size\*(\d+)\)\)-(\d+),load_only\);\}",
        fast,
    )
    require(match is not None, "unrecognized TCG CCV guard")
    limit, base, shift, subtract = map(int, match.groups())
    require(max(runtime_limits) <= 8 and (limit - 1) * shift < 64,
            "unrecognized shift range in TCG CCV guard")
    require(
        "if(load_only){tcg_gen_br(done);gen_set_label(load_only);"
        "tcg_gen_qemu_ld_i64(result,addr,mmu_idx,memop);}" in fast,
        "missing TCG read-only mismatch path",
    )
    require("if(ctx->restart.exit_after_bundle){gen_helper_cmpxchg(result,tcg_env,"
            "addr,cmp,value,tcg_constant_i32(size));return;}" in fast,
            "missing TCG helper mismatch path")
    runtime = body(memory, "ia64_memory_cmpxchg")
    require("boolcmp_representable=ia64_cmpxchg_compare_representable(cmp,size);"
            in runtime, "runtime CCV guard is unused")
    for size, load in ((1, "ia64_exec_load_data(env,addr,1,false,ra)"),
                       (2, "ia64_lduw_data_ra(env,addr,ra)"),
                       (4, "ia64_ldl_data_ra(env,addr,ra)")):
        require(re.search(
            rf"case{size}:if\(cmp_representable\)\{{[^{{}}]*?"
            rf"old=ia64_exec_cmpxchg\(env,addr,cmp,val,{size},[^{{}}]+?\);"
            rf"\}}else\{{old={re.escape(load)};\}}returnold;",
            runtime,
        ), f"runtime selector does not guard the {size}-byte atomic")
    fast_limits = {size: (base << (shift * size)) - subtract
                   if size < limit else (1 << 64) - 1 for size in runtime_limits}
    return runtime_limits, fast_limits


def translation(root):
    memop = descriptor_constants((root / MEMOP).read_text())
    memory = (root / MEMORY).read_text()
    translator = (root / TRANSLATOR).read_text()
    cpu = (root / CPU_HEADER).read_text()
    translator_header = (root / TRANSLATOR_HEADER).read_text()

    actual, expected = widths(memory)
    (width_mask,) = capture(r"return1u<<\(memop&(MO_\w+)\);",
                            body(memory, "ia64_memop_size"), "descriptor width")
    (psr_shift,) = macro(cpu, "IA64_PSR_BE", r"\(1ULL<<(\d+)\)")
    capture(r"return\(env->psr&IA64_PSR_BE\)!=0;",
            body(memory, "ia64_data_big_endian"), "PSR.be")
    (tb_shift,) = macro(translator_header, "IA64_TB_FLAG_BE", r"\(1u<<(\d+)\)")
    require(tb_shift == psr_shift, "translation block endian bit differs from PSR.be")
    require("((uint32_t)psr&(IA64_TB_FLAG_BE|IA64_TB_FLAG_PSR_IC|IA64_TB_FLAG_DT))"
            in body((root / CPU).read_text(), "ia64_get_tb_cpu_state"),
            "missing PSR.be in the native translation block key")
    require("ctx->memory.be_data=ctx->base.tb->flags&IA64_TB_FLAG_BE;"
            in body(translator, "ia64_tr_init_disas_context"), "missing TB endian read")
    capture(r"returnia64_memop_with_endian\(memop,ctx->memory.be_data\);",
            body(translator, "ia64_data_memop"), "TCG endian dispatch")

    text = (HERE / "Spec.lean").read_text(encoding="utf-8")
    text += f"""
namespace MemoryImplementation

def memopSize (descriptor : BitVec 32) : BitVec 32 :=
  1#32 <<< (descriptor &&& {memop[width_mask]}#32)

theorem memopSize_matches (descriptor : BitVec 32) :
    memopSize descriptor = MemorySpec.descriptorBytes descriptor := by
  unfold memopSize MemorySpec.descriptorBytes
  bv_decide

def psrEndian (psr : BitVec 64) : Bool :=
  (psr &&& {(1 << int(psr_shift))}#64) != 0#64

theorem psrEndian_matches (psr : BitVec 64) :
    psrEndian psr = MemorySpec.dataBigEndian psr := by
  unfold psrEndian MemorySpec.dataBigEndian
  bv_decide
"""
    for name, source, function, flag in (
        ("runtimeDescriptor", memory, "ia64_runtime_data_memop", "ia64_data_big_endian(env)"),
        ("tcgDescriptor", translator, "ia64_memop_with_endian", "big_endian"),
    ):
        text += f"""
def {name} (descriptor : BitVec 32) (hostBig guestBig : Bool) : BitVec 32 :=
  {endian_selection(source, function, flag, memop)}
theorem {name}_matches (descriptor : BitVec 32) (hostBig guestBig : Bool) :
    {name} descriptor hostBig guestBig =
      MemorySpec.dataDescriptor descriptor hostBig guestBig := by
  unfold {name} MemorySpec.dataDescriptor MemorySpec.descriptorBytes
  bv_decide
"""
    for name, descriptor in actual.items():
        text += (f"example : memopSize {memop[descriptor]}#32 = "
                 f"{expected[name]}#32 := by decide\n")
    text += """
theorem runtimePsrDescriptor_matches (descriptor : BitVec 32) (hostBig : Bool)
    (psr : BitVec 64) :
    runtimeDescriptor descriptor hostBig (psrEndian psr) =
      MemorySpec.dataDescriptor descriptor hostBig (MemorySpec.dataBigEndian psr) := by
  rw [psrEndian_matches, runtimeDescriptor_matches]
"""
    for bounds in compare_bounds(memory, translator):
        for log_bytes, size in enumerate((1, 2, 4, 8)):
            text += f"""
example (ccv : BitVec 64) :
    ccv.toNat ≤ {bounds[size]} ↔ MemorySpec.cmpxchgPossible {log_bytes} ccv := by
  simp only [MemorySpec.cmpxchgPossible, exists_eq_right]
  omega
"""
    return text + "\nend MemoryImplementation\n"


def check(source, *, expected_diagnostic=None):
    with tempfile.TemporaryDirectory(prefix="ia64-memory-") as temporary:
        path = Path(temporary) / "MemoryProof.lean"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            ["lake", "env", "lean", str(path)], cwd=HERE.parent,
            capture_output=expected_diagnostic is not None, text=True,
            check=expected_diagnostic is None,
        )
        if expected_diagnostic is not None:
            require(result.returncode != 0 and expected_diagnostic in result.stdout,
                    "Lean did not reject the memory mutation: " + result.stdout.strip())


def mutation_test(root):
    with tempfile.TemporaryDirectory(prefix="ia64-memory-mutations-") as temporary:
        changed = Path(temporary)
        for name in (MEMORY, TRANSLATOR, CPU, CPU_HEADER, TRANSLATOR_HEADER, MEMOP,
                     *(TARGET / name for name in FUSED_BODIES)):
            destination = changed / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / name, destination)
        path = changed / MEMORY
        original = path.read_text(encoding="utf-8")
        for before, after, diagnostic in (
            ("return MO_LEUW;", "return MO_LEUL;", "is false"),
            ("? MO_BE : MO_LE", "? MO_LE : MO_BE", "counterexample"),
            ("cmp <= UINT16_MAX", "cmp <= UINT8_MAX", "omega could not prove"),
        ):
            require(original.count(before) == 1, f"memory mutation anchor changed: {before}")
            path.write_text(original.replace(before, after), encoding="utf-8")
            check(translation(changed), expected_diagnostic=diagnostic)
        path = changed / TRANSLATOR
        original = path.read_text(encoding="utf-8")
        before = "    return (insn->opcode == IA64_OP_ST1 ||"
        require(original.count(before) == 1, "fused release mutation anchor changed")
        after = "    if (insn->opcode == IA64_OP_ST1REL) { return false; }\n" + before
        path.write_text(original.replace(before, after), encoding="utf-8")
        try:
            check_source(changed)
        except ValueError as error:
            require("ia64_is_fill_st1_postinc changed" in str(error),
                    "wrong failure for the fused release early-return mutation")
        else:
            raise ValueError("fused release witness accepted a disabled release path")
    print("PASS width, endian, CCV and fused-release source mutation checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    try:
        source = translation(args.source_root)
        if args.emit_lean:
            args.emit_lean.write_text(source, encoding="utf-8")
        else:
            check(source)
            print("PASS integer memory descriptor widths, both endian selectors and CCV guards",
                  flush=True)
            check_fused_release_loop(args.source_root, check)
            if args.mutation_test:
                mutation_test(args.source_root)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Memory check failed: {error}\n")


if __name__ == "__main__":
    main()
