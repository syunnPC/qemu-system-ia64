#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check page rights, probe CPL, Merced lookup and VHPT arithmetic."""

import argparse
import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def compact(text):
    return re.sub(r"\s+|/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def function(source, name):
    # These definitions use the QEMU convention of a column-zero closing brace.
    found = re.search(rf"^([\w *]+\s+{name}\s*\([^;{{}}]*\)\s*{{.*?^}})",
                      source, flags=re.MULTILINE | re.DOTALL)
    if found is None:
        raise ValueError(f"missing MMU definition {name}")
    return found[1]


def body(source, name):
    return compact(function(source, name).split("{", 1)[1][:-1])


def macro(source, name):
    found = re.search(rf"(?m)^#define {name}\b((?:[^\n]*\\\n)*[^\n]*)", source)
    if found is None:
        raise ValueError(f"missing MMU macro {name}")
    return f"#define {name} {found[1].lstrip()}\n"


def recognize(pattern, source, name):
    match = re.fullmatch(pattern, body(source, name))
    if match is None:
        raise ValueError(f"unrecognized source in {name}")
    return match.groups()


def run_probe(header, mmu, work):
    macros = (
        "IA64_TLB_R", "IA64_TLB_W", "IA64_TLB_X", "IA64_TLB_ALL", "IA64_PTE_PRESENT",
        "IA64_PSR_CPL_MASK", "IA64_PSR_CPL_SHIFT", "IA64_DTLB1_MAX",
        "IA64_DTLB1_LOOKUP_SIZE", "IA64_DTLB1_LOOKUP_PAGE_SHIFT",
    )
    mmu_macros = ("IA64_PTE_PL_SHIFT", "IA64_PTE_PL_MASK",
                  "IA64_PTE_AR_SHIFT", "IA64_PTE_AR_MASK")
    header_functions = (
        "ia64_psr_cpl", "ia64_tlb_effective_perm", "ia64_tlb_entry_present",
        "ia64_tlb_entry_translate", "ia64_tlb_match", "ia64_merced_dtlb1_lookup_index",
        "ia64_merced_dtlb1_lookup_page", "ia64_merced_dtlb1_lookup",
    )
    mmu_functions = (
        "ia64_pte_ar", "ia64_pte_pl", "ia64_pte_perm", "ia64_probe_access_level",
    )
    source = "#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n"
    source += "".join(macro(header, name) for name in macros)
    source += "".join(macro(mmu, name) for name in mmu_macros)
    shim, driver = (HERE / "probe.c").read_text(encoding="utf-8").split("/* OBSERVED C */")
    source += shim + "\n"
    source += "\n".join(function(header, name) for name in header_functions) + "\n"
    source += "\n".join(function(mmu, name) for name in mmu_functions) + driver
    translation = work / "Probe.c"
    translation.write_text(source, encoding="utf-8")
    binary = work / "probe"
    cc = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run([*cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(translation), "-o", str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True).splitlines()
    observed = [tuple(map(int, line.split())) for line in output]
    if [len(row) for row in observed] != [512, 64, 512]:
        raise ValueError("incomplete MMU observations")
    return observed


def lift_address(header, mmu):
    offset_mask, base_mask = recognize(
        r"uint64_tpage_offset=va&\(entry->ps-(\d+)\);"
        r"\*pa=\(entry->pa&~\(entry->ps-(\d+)\)\)\|page_offset;"
        r"\*perm=ia64_tlb_entry_present\(entry\)"
        r"\?ia64_tlb_effective_perm\(entry->ar,entry->pl,access_level\):0;",
        header, "ia64_tlb_entry_translate",
    )
    entry_shift, mask_sub, low_mask = recognize(
        r"uint64_tregion=va&\(IA64_REGION_MASK<<IA64_REGION_SHIFT\);"
        r"uint64_toffset;uint64_tmask;uint64_tbase;"
        r"offset=ia64_vhpt_hpn\(env,va\)<<(\d+);"
        r"mask=\(1ULL<<size\)-(\d+);"
        r"base=env->cr_pta&\(\(\(1ULL<<IA64_REGION_SHIFT\)-1\)&~(0x[\da-fA-F]+)ULL\);"
        r"returnregion\|\(\(base&~mask\)\|\(offset&mask\)\);",
        mmu, "ia64_vhpt_short_hash_address",
    )
    (tag_shift,) = recognize(
        r"uint64_thpn=ia64_vhpt_hpn\(env,va\);uint64_trid=ia64_region_rid\(env,va\);"
        r"returnhpn\^\(rid<<(\d+)\);", mmu, "ia64_vhpt_long_tag",
    )
    (long_entry_shift,) = recognize(
        r"uint64_tbase=env->cr_pta&IA64_PTA_BASE_MASK;"
        r"uint64_tentries=1ULL<<\(size-(\d+)\);"
        r"uint64_thpn=ia64_vhpt_hpn\(env,va\);uint64_thash;uint64_toffset;"
        r"uint64_tmask=\(1ULL<<size\)-1;hash=hpn\^ia64_region_rid\(env,va\);"
        r"hash&=entries-1;offset=hash<<5;if\(hash_out\)\{\*hash_out=hash;\}"
        r"return\(base&~mask\)\|\(offset&mask\);", mmu, "ia64_vhpt_long_hash_address",
    )
    region_shift = int(macro(header, "IA64_REGION_SHIFT").split()[2])
    region_bits = int(macro(header, "IA64_REGION_BITS").split()[2])
    if compact(macro(header, "IA64_REGION_MASK")) != (
        "#defineIA64_REGION_MASK((1ULL<<IA64_REGION_BITS)-1)"
    ):
        raise ValueError("unrecognized region mask")
    region = ((1 << region_bits) - 1) << region_shift
    base_mask_vhpt = ((1 << region_shift) - 1) & ~int(low_mask, 16)
    return f"""
namespace IA64Mmu

def cachedAddress (pa va size : BitVec 64) : BitVec 64 :=
  (pa &&& ~~~(size - {base_mask}#64)) ||| (va &&& (size - {offset_mask}#64))

def cShortAddress (pta va hpn : BitVec 64) (logSize : Nat) : BitVec 64 :=
  let mask := (1#64 <<< logSize) - {mask_sub}#64
  let offset := hpn <<< {entry_shift}
  (va &&& {region}#64) ||| (((pta &&& {base_mask_vhpt}#64) &&& ~~~mask) ||| (offset &&& mask))

def cLongTag (hpn : BitVec 49) (rid : BitVec 24) : BitVec 64 :=
  hpn.zeroExtend 64 ^^^ (rid.zeroExtend 64 <<< {tag_shift})

def cLongIndex (hpn : BitVec 49) (rid : BitVec 24) : BitVec (15 - {long_entry_shift}) :=
  (hpn ^^^ rid.zeroExtend 49).truncate (15 - {long_entry_shift})

-- Vol. 2 §4.1.6.2 requires ti = 0 and joint uniqueness of long index/tag.
-- The lowest ten index bits exist even at the minimum accepted table size.
theorem longTagTiClear (hpn : BitVec 49) (rid : BitVec 24) :
    (cLongTag hpn rid).getLsbD 63 = false := by
  unfold cLongTag
  bv_decide

theorem hashAndTagUnique (hp hq : BitVec 49) (rp rq : BitVec 24) :
    cLongIndex hp rp = cLongIndex hq rq →
    cLongTag hp rp = cLongTag hq rq → hp = hq ∧ rp = rq := by
  unfold cLongIndex cLongTag
  bv_decide
"""


def projected_theorems(probes, source):
    for expected, observed in zip(("expectedRights", "expectedProbe", "expectedFastTags"),
                                  probes, strict=True):
        values = ",".join(map(str, observed))
        source += f"\nexample : (#[{values}] : Array Nat) = {expected} := by native_decide\n"
    for shift in (12, 13, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32):
        source += f"""
example (pa va : BitVec 64) :
    cachedAddress pa va {1 << shift}#64 = frameOffset pa va {shift} := by
  unfold cachedAddress frameOffset low
  bv_decide
"""
    for size in range(15, 53):
        source += f"""
example (pta va hpn : BitVec 64) :
    cShortAddress pta va hpn {size} = shortAddress pta va hpn {size} := by
  unfold cShortAddress shortAddress low
  bv_decide
"""
    return source + "\nend IA64Mmu\n"


def check(root, mutation_test):
    header = (root / "target/ia64/cpu.h").read_text(encoding="utf-8")
    mmu = (root / "target/ia64/arch/mmu.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="ia64-mmu-") as temp:
        work = Path(temp)
        spec = (HERE / "Spec.lean").read_text(encoding="utf-8")
        values = run_probe(header, mmu, work)
        module = work / "MmuProof.lean"
        module.write_text(spec + projected_theorems(values, lift_address(header, mmu)),
                          encoding="utf-8")
        lean = ["lake", "env", "lean", str(module)]
        subprocess.run(lean, cwd=HERE.parent, check=True)
        if mutation_test:
            old = "return access_level <= pl ? IA64_TLB_R : 0;"
            if header.count(old) != 1:
                raise ValueError("MMU permissions mutation anchor changed")
            mutation = header.replace(old, old.replace("<=", ">="))
            observed = run_probe(mutation, mmu, work)
            module.write_text(spec + projected_theorems(observed, lift_address(header, mmu)),
                              encoding="utf-8")
            result = subprocess.run(lean, cwd=HERE.parent, check=False, capture_output=True, text=True)
            if result.returncode == 0 or not re.search(r"(?:is|to) false", result.stdout):
                raise ValueError("Lean failed to reject the inverted privilege check:\n"
                                 + result.stdout)
            print("PASS MMU privilege mutation check")
    print("PASS MMU page rights, probe CPL, Merced lookup and symbolic address arithmetic")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    try:
        check(args.source_root, args.mutation_test)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"MMU check failed: {error}\n")


if __name__ == "__main__":
    main()
