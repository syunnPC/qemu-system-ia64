# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare hot IA-64 regions with TCG across code updates and data faults."""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SOURCE / "tests/unit"))
from ia64.qmp import QmpClient
from ia64.state import parse_state


def check(qemu, build, case, region, terminal, fault, alat, library):
    env = dict(os.environ)
    if library is None:
        env.pop("IA64_LLVM_LIBRARY", None)
    else:
        env["IA64_LLVM_LIBRARY"] = str(library)
    with tempfile.TemporaryDirectory() as temp:
        stats = Path(temp) / "stats"
        env["IA64_LLVM_STATS"] = str(stats)
        binary = build / f"region-{case}.bin"
        command = [
            qemu,
            "-L",
            str(SOURCE / "pc-bios"),
            "-machine",
            f"itanium2-vpc,firmware=none,nvram=none,alat={alat}",
            "-m",
            "128M",
            "-S",
            "-qmp",
            "stdio",
            "-display",
            "none",
            "-monitor",
            "none",
            "-serial",
            "none",
            "-nic",
            "none",
            "-device",
            f"loader,addr=0x2000000,file={binary},force-raw=on",
            "-device",
            "loader,addr=0x2000000,cpu-num=0",
        ]
        if fault is not None:
            vector = build / "region-vector.bin"
            command.extend(
                [
                    "-device",
                    f"loader,addr=0x5a00,file={vector},force-raw=on",
                ]
            )

        with subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            env=env,
            text=True,
        ) as proc:
            try:
                qmp = QmpClient(proc.stdout, proc.stdin)
                end = time.monotonic() + 15
                while True:
                    qmp.execute("cont")
                    time.sleep(0.1)
                    qmp.execute("stop")
                    state = parse_state(qmp.hmp("info registers"))
                    if state.ip == terminal:
                        break
                    if time.monotonic() > end:
                        raise RuntimeError(
                            (
                                hex(state.ip or 0),
                                state.exception,
                                state.gr.get(20),
                            )
                        )
                if fault is not None:
                    # The final pass completes six increments before the fault.
                    expected = (
                        38000006,
                        22000006,
                        fault,
                        1,
                        0x3000009,
                        1,
                        2000001,
                        fault,
                    )
                    actual = (
                        state.gr[34],
                        state.gr[35],
                        state.gr[27],
                        (state.gr[26] >> 41) & 3,
                        state.gr[29],
                        (state.gr[28] >> 34) & 1,
                        state.gr[30],
                        state.gr[31],
                    )
                    assert actual == expected, (actual, expected)
                elif case == "alat":
                    expected = (
                        1000000 if alat == "zero" else 0,
                        1000000,
                        1000000,
                        10000000,
                        10000000,
                    )
                    actual = tuple(state.gr[i] for i in (4, 14, 15, 16, 17))
                    assert actual == expected, (actual, expected)
                else:
                    # Rewrite both the region and its linked successor.
                    expected = (
                        6000000,
                        184000000,
                        376000000,
                        564000000,
                        6000000,
                    )
                    assert tuple(state.gr[i] for i in range(34, 39)) == expected
                qmp.execute("quit")
            finally:
                if proc.poll() is None:
                    try:
                        proc.wait(2)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
        if library is not None:
            lines = stats.read_text().splitlines()
            runs = int(lines[1].split()[1])
            samples = [line.split() for line in lines[2:]]
            if case == "smc":
                assert sum(int(row[4], 16) == region for row in samples) >= 2, samples
            elif case == "alat":
                assert any(int(row[4], 16) == region for row in samples), samples
            else:
                assert int(lines[1].split()[4]) >= 1, lines
            assert runs > 0, (runs, samples)
            print(alat, case, runs, "native region executions")
        else:
            print(alat, case, "TCG", expected)


def symbols(elf):
    nm = os.environ.get("CROSS_IA64", "ia64-linux-gnu-") + "nm"
    lines = subprocess.check_output([nm, "-n", elf], text=True).splitlines()
    return {
        fields[2]: int(fields[0], 16)
        for fields in map(str.split, lines)
        if len(fields) == 3
    }


if __name__ == "__main__":
    qemu, build = map(Path, sys.argv[1:])
    vector_end = 0x5A00 + symbols(build / "region-vector.elf")["spin"] - 0x2000000
    for case in ("smc", "fault", "alat"):
        names = symbols(build / f"region-{case}.elf")
        for alat in ("zero", "full"):
            for library in (None, build / "ia64-llvm.so"):
                check(
                    qemu,
                    build,
                    case,
                    names["region"],
                    names.get("done", vector_end),
                    names.get("fault"),
                    alat,
                    library,
                )
