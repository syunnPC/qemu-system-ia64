#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-check IA-64 TCG ordering mappings with herd7 7.58 host models."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
TESTS = {
    "amd64-narrow-rel": "Sometimes",
    "amd64-mf": "Never",
    "amd64-locked-zero": "Never",
    "amd64-mp": "Never",
    "aarch64-narrow-rel": "Never",
    "aarch64-sb": "Sometimes",
    "aarch64-mf": "Never",
    "aarch64-mp": "Never",
    "aarch64-mp-unfenced": "Sometimes",
}
MODELS = {
    "X86_64": "x86tso-mixed.cat",
    "AArch64": "aarch64.cat",
}


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    if result.returncode or result.stderr.strip():
        raise ValueError(
            f"{Path(command[0]).name} exited with {result.returncode}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def check_mappings():
    source = HERE.parent / "Ordering" / "check.py"
    masks = json.loads(run([sys.executable, "-B", str(source), "--host-mappings"]))
    # The fixture instruction sequences assume these lowered operations.
    # Reuse the Ordering extractor to check the assumptions against the C
    # paths before asking herd7 to evaluate the independent host models.
    mb = masks["allPairs"]
    arm_dmb = {0xd5033bbf: "ISH", 0xd50339bf: "ISHLD", 0xd5033abf: "ISHST"}

    def default_request(host, mask):
        return masks[mask] & masks["guestDefault"] & ~masks[f"{host}Default"]

    operations = ("defaultLoad", "defaultStore", "acquire", "release", "fence")
    expected = {
        "amd64": (None, None, None, None, "locked"),
        "aarch64": ("ISHLD", "ISH", "ISHLD", "ISH", "ISH"),
    }
    for host, reference in expected.items():
        requests = [default_request(host, operation) if operation.startswith("default")
                    else masks[operation] & mb for operation in operations]
        if host == "amd64":
            actual = tuple("locked" if request & masks["amd64FenceTrigger"] else None
                           for request in requests)
        else:
            actual = tuple(arm_dmb[masks["armTable"][request]] if request else None
                           for request in requests)
        if actual != reference:
            raise ValueError(f"{host} mappings changed. {operations}: {reference} → {actual}")

    if (default_request("amd64", "runtimeLoad") != 0 or
            default_request("aarch64", "runtimeLoad") == 0):
        raise ValueError("narrow helper load ordering changed")

    amd64_pairs = masks["ldLd"] | masks["ldSt"] | masks["stSt"]
    if masks["amd64Default"] != amd64_pairs or masks["aarch64Default"] != 0:
        raise ValueError("host default ordering changed")


def check_test(command, label, expected):
    path = HERE / f"{label}.litmus"
    arch, name = path.read_text(encoding="utf-8").splitlines()[0].split()
    output = run([*command, "-model", MODELS[arch], str(path)])
    observations = re.findall(r"^Observation (\S+) (\S+) (\d+) (\d+)$", output, re.M)
    if len(observations) != 1 or f"Test {name} Allowed" not in output.splitlines():
        raise ValueError(f"{label}: expected one allowed herd7 test\n{output}")
    observed, verdict, positive, negative = observations[0]
    positive = int(positive)
    negative = int(negative)
    if (observed != name or verdict != expected or negative <= 0 or
            (verdict == "Never" and positive != 0) or
            (verdict == "Sometimes" and positive <= 0)):
        raise ValueError(f"{label}: expected {expected}\n{output}")
    print(f"{label}: {verdict} ({positive} matching, {negative} other)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--herd7", default=os.environ.get("HERD7", "herd7"),
                        help="herd7 executable (defaults to PATH)")
    parser.add_argument("--libdir", type=Path, default=os.environ.get("HERDLIB"),
                        help="alternate installation directory for herd7 7.58 models")
    args = parser.parse_args()
    if args.libdir is not None and not all(
        (args.libdir / filename).is_file() for filename in MODELS.values()
    ):
        raise ValueError(f"herd7 models missing from {args.libdir}")
    version = run([args.herd7, "-version"]).strip()
    if re.fullmatch(r"7\.58(?:,.*)?", version) is None:
        raise ValueError(f"herd7 7.58 required, found {version}")
    command = [args.herd7, "-exit", "true"]
    if args.libdir is not None:
        command += ["-set-libdir", str(args.libdir.resolve())]

    check_mappings()
    for label, expected in TESTS.items():
        check_test(command, label, expected)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, IndexError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"Host litmus check failed: {exc}") from None
