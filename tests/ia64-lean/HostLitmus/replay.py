#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check selected IA-64 TCG ordering paths in a two-vCPU QEMU run."""

import argparse
from pathlib import Path
import re
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/unit"))

from ia64 import runner
from ia64.cases_memory_nat import (
    test_cloop_fill_st1_cross_page_range,
    test_cmpxchg4_full_ar_ccv_compare,
)
from ia64.encoding import (
    cmpxchg4_acq,
    cmpxchg_rel,
    run_program,
    st1_postinc,
    store_mem_postinc,
)


def trace_program(qemu, bundles, expected, name, *, log_flags="op"):
    with tempfile.TemporaryDirectory(prefix="ia64-tcg-trace-") as tmp:
        log = Path(tmp) / "op.log"
        base_command = runner._command

        def trace_command(executable, program, **options):
            return base_command(executable, program, **options) + [
                "-accel", "tcg,thread=multi", "-d", log_flags, "-D", str(log),
            ]

        with patch.object(runner, "_command", trace_command):
            run_program(str(qemu), bundles, alat="zero", expected=expected,
                        smp="2", name=name)
        return log.read_text(encoding="utf-8")


def replay(qemu, case, address, old, new, *, expected=None):
    if sum(bundle[0] == address and bundle[2] == old for bundle in case.bundles) != 1:
        raise ValueError(f"{case.name}: expected bundle to patch at {address:#x}")
    bundles = [(bundle[0], bundle[1], new, *bundle[3:]) if bundle[0] == address
               else bundle for bundle in case.bundles]
    return trace_program(qemu, bundles, case.expected if expected is None else expected,
                         case.name)


def check_replays(qemu):
    narrow = replay(qemu, test_cmpxchg4_full_ar_ccv_compare, 0x50,
                    cmpxchg4_acq(10, 3, 6), cmpxchg_rel(2, 10, 3, 6),
                    expected={"ip": 0x80, "r8": 0x40, "r10": 0x40})
    rel = r"^\s*mb rel:rw\+ww\s*$"
    narrow_form = (
        rel + r".*?\bbrcond_i64\s+[^\n]*\$0xffffffff,gtu,(\$L\d+)\b"
        r".*?\bcall\s+atomic_cmpxchg\w*"
        r".*?\bset_label\s+\1\b\s*\n\s*qemu_ld_i64\s+r10,"
    )
    if not re.search(narrow_form, narrow, re.M | re.S):
        raise ValueError("cmpxchg4.rel: expected release, excess-CCV branch and load-only TCG arm")
    print("MTTCG cmpxchg4.rel: observed release mask and excess-CCV load branch")

    fused = replay(qemu, test_cloop_fill_st1_cross_page_range, 0x40,
                   st1_postinc(2, 4, 1), store_mem_postinc(0x34, 2, 4, 1))
    if not re.search(rel + r".*?\bcall\s+cloop_fill_st1\b", fused, re.M | re.S):
        raise ValueError("fused st1.rel loop: expected release mask before fill helper")
    print("MTTCG fused st1.rel loop: observed release mask before fill helper")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu", type=Path, help="QEMU system IA-64 executable")
    try:
        check_replays(parser.parse_args().qemu.resolve())
    except (ValueError, OSError, RuntimeError) as exc:
        raise SystemExit(f"Host litmus replay failed: {exc}") from None
