#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise finite trace refinement on the source-projected memory selector."""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent


def main():
    with tempfile.TemporaryDirectory(prefix="ia64-refinement-") as temp:
        module = Path(temp) / "RefinementProof.lean"
        subprocess.run(
            [sys.executable, "Memory/check.py", "--emit-lean", str(module)],
            cwd=PROJECT, check=True,
        )
        with module.open("a", encoding="utf-8") as output:
            for name in ("Spec.lean", "MemoryExample.lean"):
                output.write("\n" + (HERE / name).read_text(encoding="utf-8"))
        subprocess.run(["lake", "env", "lean", str(module)], cwd=PROJECT, check=True)
    print("PASS finite trace refinement for the source-projected endian selector")


if __name__ == "__main__":
    main()
