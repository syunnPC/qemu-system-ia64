#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the ALAT writer protocol and staggered generation sums."""

import argparse
import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

ALAT = Path("target/ia64/arch/alat.c")
CPU = Path("target/ia64/cpu.h")
ATOMIC = Path("include/qemu/atomic.h")
PHYSICAL = Path("system/physmem.c")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def compact(text):
    return re.sub(r"\s+|/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def body(source, name):
    match = re.search(rf"\b{name}\s*\([^;{{}}]*\)\s*{{(.*?)^}}", source,
                      flags=re.MULTILINE | re.DOTALL)
    require(match is not None, f"missing function {name}")
    return compact(match[1])


# Bind reader and token completion control flow, including early returns.
# Writer increments are lifted separately, so a wrong increment reaches Lean.
READER_BODIES = {
    "ia64_alat_cpu_generation":
        "3efb78a8ce1aa92a43db2f1da1f4d3d86cd0d0a13a7da251dbb8a48c3a3e1a62",
    "ia64_alat_memory_generation":
        "e8233bd3ea3e7c0d408a2b3bba910656e516438b24caa69f5c46e8c4970525d9",
    "ia64_alat_memory_generation_changed":
        "083f70f4f926990ab851f3b24eea106987c9c231eb3e78d4580a0daba45f8aba",
    "ia64_alat_sync_memory_writes":
        "3e4bc6a31a2bbc5413432cc3f9e9c74eeaf356f1baeaa1fc82f1851c3f3503ff",
    "ia64_alat_memory_window_changed":
        "d143141a91d54d7c9557910dc1b57f04ba53409777a3241c2d016acc30f0918a",
    "ia64_alat_load_begin":
        "5eac6c04486292770a926a5aabff118d84ba395cccb4647b72ac196e4038ba40",
}
PHYSICAL_BODIES = {
    "physical_memory_write_generation":
        "43c5ee70d29ef35c7017e56219d0c6362a65a7c7fbf33be8fed13a78fb58f6f5",
    "physical_memory_write_generation_changed":
        "eb2964a0a19174446df31ce0b7013807b6a2ea15289b41bd1ceb3c80bb88cf85",
}


def verify_bodies(source, expected):
    for name, digest in expected.items():
        require(hashlib.sha256(body(source, name).encode()).hexdigest() == digest,
                f"ALAT generation model needs review: {name} changed")


def number(pattern, contents, name):
    match = re.fullmatch(pattern, body(contents, name))
    require(match is not None, f"unrecognized ALAT {name}")
    return int(match[1])


def load_sources(root):
    return {path: (root / path).read_text(encoding="utf-8")
            for path in (ALAT, CPU, ATOMIC, PHYSICAL)}


def translate(sources):
    source = sources[ALAT]
    verify_bodies(source, READER_BODIES)
    verify_bodies(sources[PHYSICAL], PHYSICAL_BODIES)
    require("uint64_talat_write_sequence;" in compact(sources[CPU]),
            "ALAT sequence is no longer a 64-bit unsigned counter")
    atomic = compact(sources[ATOMIC].replace("\\\n", ""))
    for expression in (
        "#definesmp_wmb()smp_mb_release()",
        "#definesmp_rmb()smp_mb_acquire()",
        "#definesmp_mb_release()({barrier();__atomic_thread_fence(__ATOMIC_RELEASE);})",
        "#definesmp_mb_acquire()({barrier();__atomic_thread_fence(__ATOMIC_ACQUIRE);})",
        "__atomic_load(ptr,&_val,__ATOMIC_ACQUIRE);_val;})",
        "__atomic_store_n(ptr,i,__ATOMIC_RELEASE);}while(0)",
    ):
        require(expression in atomic, "ALAT atomic primitive definitions changed")

    opened = number(
        r"IA64CPU\*cpu=ia64_cpu_from_cpu_state\(env_cpu\(env\)\);"
        r"g_assert\(!env->alat_state.write_active\);"
        r"env->alat_state.write_generation=env->alat_state.memory_write_generation;"
        r"env->alat_state.write_observed=env->alat_state.alat_full;"
        r"env->alat_state.write_active=true;"
        r"if\(env->alat_state.write_observed\)\{"
        r"uint64_tsequence=qatomic_read\(&cpu->alat_write_sequence\);"
        r"g_assert\(!\(sequence&1\)\);"
        r"qatomic_set\(&cpu->alat_write_sequence,sequence\+(\d+)\);smp_wmb\(\);\}",
        source, "ia64_alat_write_begin",
    )
    closed = number(
        r"boolobserved;g_assert\(env->alat_state.write_active\);"
        r"observed=env->alat_state.write_observed;"
        r"env->alat_state.write_active=false;env->alat_state.write_observed=false;"
        r"if\(observed\)\{IA64CPU\*cpu=ia64_cpu_from_cpu_state\(env_cpu\(env\)\);"
        r"uint64_tsequence=qatomic_read\(&cpu->alat_write_sequence\);"
        r"g_assert\(sequence&1\);"
        r"qatomic_store_release\(&cpu->alat_write_sequence,sequence\+(\d+)\);\}"
        r"returnobserved;",
        source, "ia64_alat_write_take_token",
    )
    ended = number(
        r"uint64_tgeneration=env->alat_state.write_generation;"
        r"boolobserved=ia64_alat_write_take_token\(env\);"
        r"uint64_texpected=generation\+(\d+);"
        r"if\(observed&&env->alat_state.alat_active_count!=0\)\{"
        r"if\(!precise\|\|env->alat_state.memory_write_generation!=generation\|\|"
        r"ia64_alat_memory_generation_changed\(env,expected\)\)\{"
        r"ia64_alat_invala\(env\);return;\}"
        r"env->alat_state.memory_write_generation=expected;"
        r"ia64_invalidate_alat_store\(env,addr,size\);\}",
        source, "ia64_alat_write_finish",
    )
    canceled = number(
        r"if\(ia64_alat_write_take_token\(env\)\)\{"
        r"env->alat_state.memory_write_generation\+=([0-9]+);\}",
        source, "ia64_alat_write_cancel",
    )
    require(body(source, "ia64_alat_write_end")
            == "ia64_alat_write_finish(env,addr,size,true);",
            "ALAT completed-store dispatch changed")
    require(body(source, "ia64_alat_write_abort")
            == "if(env->alat_state.write_active){ia64_alat_write_finish(env,0,0,false);}",
            "ALAT abort dispatch changed")
    return (HERE / "Spec.lean").read_text(encoding="utf-8") + f"""

namespace Alat

def sourceBegin (sequence : Nat) : Nat := sequence + {opened}
def sourceFinish (sequence : Nat) : Nat := sequence + {closed}
def sourceExpectedAfterStore (generation : Nat) : Nat := generation + {ended}
def sourceExpectedAfterCancel (generation : Nat) : Nat := generation + {canceled}

theorem sourceBeginOpens (sequence : Nat) (idle : sequence % 2 = 0) :
    (sourceBegin sequence) % 2 = 1 := by
  unfold sourceBegin
  omega

theorem sourceFinishCloses (sequence : Nat) (idle : sequence % 2 = 0) :
    (sourceFinish (sourceBegin sequence)) % 2 = 0 := by
  unfold sourceFinish sourceBegin
  omega

theorem bothCompletionPathsAccountForOwnScope (sequence otherGeneration : Nat) :
    sourceExpectedAfterStore (otherGeneration + sequence) =
      otherGeneration + sourceFinish (sourceBegin sequence) ∧
    sourceExpectedAfterCancel (otherGeneration + sequence) =
      otherGeneration + sourceFinish (sourceBegin sequence) := by
  unfold sourceBegin sourceFinish sourceExpectedAfterStore sourceExpectedAfterCancel
  omega

end Alat
"""


def check(source, expected_diagnostic=None):
    with tempfile.TemporaryDirectory(prefix="ia64-alat-") as temporary:
        module = Path(temporary) / "AlatProof.lean"
        module.write_text(source, encoding="utf-8")
        result = subprocess.run(
            ["lake", "env", "lean", str(module)], cwd=HERE.parent,
            check=expected_diagnostic is None,
            capture_output=expected_diagnostic is not None, text=True,
        )
        if expected_diagnostic is not None:
            require(result.returncode != 0 and expected_diagnostic in result.stdout,
                    "Lean did not reject the ALAT protocol mutation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    try:
        sources = load_sources(args.source_root)
        check(translate(sources))
        print("PASS ALAT writer begin/end/cancel and staggered sums "
              "(monotone paired samples and no 64-bit total wrap)")
        if args.mutation_test:
            anchor = "qatomic_set(&cpu->alat_write_sequence, sequence + 1);"
            require(sources[ALAT].count(anchor) == 1, "ALAT mutation anchor changed")
            sources[ALAT] = sources[ALAT].replace(anchor, anchor.replace("+ 1", "+ 2"))
            check(translate(sources), "omega could not prove")
            print("PASS ALAT writer increment mutation check")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ALAT check failed: {error}\n")


if __name__ == "__main__":
    main()
