# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare hot IA-64 regions with TCG across code updates and data faults."""

import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SOURCE / "tests/unit"))
from ia64.encoding_constants import (
    IA64_IMPL_PA_BITS,
    IA64_ISR_CODE_UI,
    IA64_ISR_EI_SHIFT,
    IA64_LOWER_PRIV_TRANSFER_VECTOR,
    IA64_UNALIGNED_VECTOR,
)
from ia64.qmp import QmpClient
from ia64.state import parse_state

VECTORS = {
    "fault": IA64_UNALIGNED_VECTOR,
    "boundary": IA64_LOWER_PRIV_TRANSFER_VECTOR,
}
PHYS_LIMIT = 1 << IA64_IMPL_PA_BITS


def check(
    qemu,
    build,
    case,
    region,
    terminal,
    fault,
    alat,
    accel="tcg",
    library=None,
    cache_limit=1 << 30,
    expect_jit=None,
    fallback=None,
    record_stats=True,
    machine="itanium2-vpc",
    compiler_threads=1,
):
    if expect_jit is None:
        expect_jit = accel.partition(",")[0] == "tcg-llvm"
    with tempfile.TemporaryDirectory() as temp:
        stats = Path(temp) / "stats"
        if record_stats:
            accel += ",llvm-stats=" + str(stats).replace(",", ",,")
        if library is not None:
            accel += ",llvm-library=" + str(library).replace(",", ",,")
        binary = build / f"region-{case}.bin"
        command = [
            qemu,
            "-L",
            str(SOURCE / "pc-bios"),
            "-accel",
            accel,
            "-machine",
            f"{machine},firmware=none,nvram=none,alat={alat}",
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
        if fallback:
            command.extend(["-accel", fallback])
        if case == "boundary":
            command.extend(
                [
                    "-global",
                    f"ia64-pcihost.mmio-cpu-base={PHYS_LIMIT - 4096:#x}",
                    "-global",
                    "ia64-pcihost.mmio-size=4096",
                    "-device",
                    "pci-testdev,bus=pci,addr=1d.0,membar=4096,membar-backed=on",
                ]
            )
        if case in VECTORS:
            vector = build / "region-vector.bin"
            command.extend(
                [
                    "-device",
                    f"loader,addr={VECTORS[case]:#x},file={vector},force-raw=on",
                ]
            )

        with subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            cwd=temp,
            text=True,
        ) as proc:
            try:
                qmp = QmpClient(proc.stdout, proc.stdin)
                end = time.monotonic() + 15
                while True:
                    qmp.execute("cont")
                    if case == "parallel":
                        while time.monotonic() < end:
                            remaining = int(
                                qmp.hmp("xp /1gx 0x3000000").split()[-1], 16
                            )
                            if 0 < remaining <= 128_800:
                                break
                            if not remaining and int(
                                qmp.hmp("xp /1gx 0x3000008").split()[-1], 16
                            ):
                                break
                            time.sleep(0.001)
                        else:
                            raise RuntimeError(
                                "parallel compilation workload timed out"
                            )
                        break
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
                if case == "parallel":
                    expected = "shutdown with compiler jobs submitted"
                elif fault is not None:
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
                elif case == "boundary":
                    expected = (
                        PHYS_LIMIT,
                        PHYS_LIMIT - 16,
                        IA64_ISR_CODE_UI | (2 << IA64_ISR_EI_SHIFT),
                        8 * 2_000_002,
                    )
                    actual = tuple(state.gr[i] for i in (27, 31, 28, 23))
                    assert actual == expected, (actual, expected)
                elif case == "interrupt":
                    trips = state.gr[9]
                    assert trips > 2_000_000
                    expected = (1, 0xEF, 8 * trips, 16 * trips, 24 * trips)
                    actual = tuple(state.gr[i] for i in (5, 6, 10, 11, 12))
                    assert actual == expected, (actual, expected)
                elif case == "guard":
                    expected = (2_000_000, 32_000_000, 32_000_000)
                    assert tuple(state.gr[i] for i in (4, 16, 17)) == expected
                elif case == "merced":
                    expected = (90, 20_000_030, 20_000_030)
                    actual = tuple(state.gr[i] for i in (31, 16, 17))
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
        assert proc.returncode == 0, proc.returncode
        if not record_stats:
            assert not list(Path(temp).iterdir())
            print(alat, case, "LLVM statistics disabled", expected)
        elif not expect_jit:
            assert not stats.exists()
            print(alat, case, "TCG", expected)
        else:
            lines = stats.read_text().splitlines()
            fields = lines[0].split()
            limit = int(fields[fields.index("cache-limit") + 1])
            used = int(fields[fields.index("cache-used") + 1])
            assert limit == cache_limit
            assert 0 <= used <= limit
            runs = int(lines[1].split()[1])
            if case == "parallel":
                assert int(fields[fields.index("submitted") + 1]) > 1, fields
                peak = int(fields[fields.index("peak-compiling") + 1])
                assert 0 < peak <= compiler_threads, fields
                print(case, expected, peak)
                return
            if cache_limit == 1:
                assert used == runs == 0
                print(alat, case, "TCG with one-byte LLVM cache", expected)
                return
            assert used > 0
            samples = [line.split() for line in lines[2:]]
            if case == "smc":
                assert sum(int(row[4], 16) == region for row in samples) >= 2, samples
            elif case in ("alat", "boundary", "merced", "guard", "interrupt"):
                assert any(int(row[4], 16) == region for row in samples), samples
                if case == "guard":
                    assert int(lines[1].split()[5]) > 0, lines
                elif case == "merced":
                    # The arithmetic tail shares a key across both PSR.dt modes.
                    assert int(fields[fields.index("keys") + 1]) == 2, fields
            elif case == "fault":
                assert int(lines[1].split()[4]) >= 1, lines
            assert runs > 0, (runs, samples)
            print(alat, case, runs, "native region executions")


def symbols(elf):
    nm = os.environ.get("CROSS_IA64", "ia64-linux-gnu-") + "nm"
    lines = subprocess.check_output([nm, "-n", elf], text=True).splitlines()
    return {
        fields[2]: int(fields[0], 16)
        for fields in map(str.split, lines)
        if len(fields) == 3
    }


def start_paused(qemu, accel):
    return subprocess.run(
        [
            qemu,
            "-accel",
            accel,
            "-machine",
            "none",
            "-display",
            "none",
            "-nodefaults",
            "-S",
            "-qmp",
            "stdio",
        ],
        input='{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n',
        capture_output=True,
        check=False,
        text=True,
        timeout=10,
    )


def has_builtin(qemu):
    result = start_paused(qemu, "tcg-llvm")
    if result.returncode and "no built-in IA-64 LLVM backend" in result.stderr:
        return False
    result.check_returncode()
    return True


if __name__ == "__main__":
    qemu, build = (Path(arg).resolve() for arg in sys.argv[1:])
    accelerators = subprocess.check_output([qemu, "-accel", "help"], text=True)
    assert {"tcg", "tcg-llvm"} <= set(accelerators.splitlines()), accelerators
    assert "llvm-jit=" not in subprocess.check_output([qemu, "-help"], text=True)
    for name in ("tcg", "tcg-llvm"):
        removed = start_paused(qemu, name + ",llvm-jit=off")
        assert removed.returncode and "llvm-jit' not found" in removed.stderr, removed
    builtin = has_builtin(qemu)
    default_library = None if builtin else build / "ia64-llvm.so"
    vector_offset = symbols(build / "region-vector.elf")["spin"] - 0x2000000
    cases = ["smc", "fault", "alat", "guard", "interrupt"]
    devices = subprocess.check_output([qemu, "-device", "help"], text=True)
    if 'name "pci-testdev"' in devices:
        cases.append("boundary")
    else:
        print("Physical address boundary test requires pci-testdev and was skipped")
    for case in cases:
        names = symbols(build / f"region-{case}.elf")
        terminal = VECTORS[case] + vector_offset if case in VECTORS else names["done"]
        for alat in ("zero", "full"):
            for accel, library in (("tcg", None), ("tcg-llvm", default_library)):
                check(
                    qemu,
                    build,
                    case,
                    names["region"],
                    terminal,
                    names.get("fault"),
                    alat,
                    accel,
                    library,
                )
    names = symbols(build / "region-alat.elf")
    args = (qemu, build, "alat", names["region"], names["done"], None, "zero")
    merced = symbols(build / "region-merced.elf")
    parallel = symbols(build / "region-parallel.elf")
    for accel, count in (("tcg-llvm", 1), ("tcg-llvm,llvm-thread=4", 4)):
        check(
            qemu,
            build,
            "parallel",
            parallel["region"],
            parallel["done"],
            None,
            "zero",
            accel,
            default_library,
            machine="itanium-vpc",
            compiler_threads=count,
        )
    for accel in ("tcg", "tcg-llvm,llvm-thread=4"):
        check(
            qemu,
            build,
            "merced",
            merced["region"],
            merced["done"],
            None,
            "full",
            accel,
            default_library,
            machine="itanium-vpc",
        )
    check(*args, "tcg-llvm", default_library, machine="itanium-vpc")
    check(
        *args,
        "tcg-llvm,thread=single,llvm-thread=4",
        default_library,
        machine="itanium-vpc",
    )
    for count in ("0", "2147483648"):
        invalid = start_paused(qemu, "tcg-llvm,llvm-thread=" + count)
        assert invalid.returncode and "llvm-thread" in invalid.stderr, invalid
    check(*args, "tcg", build / "absent.so")
    check(
        *args,
        "tcg-llvm",
        build / "absent.so",
        fallback="tcg",
        expect_jit=False,
    )
    check(
        *args,
        "tcg-llvm,cachesize=1",
        build / "ia64-llvm.so",
        cache_limit=1,
    )
    check(
        *args,
        "tcg-llvm,thread=single,cachesize=4M",
        build / "ia64-llvm.so",
        cache_limit=4 << 20,
    )
    check(*args, "tcg-llvm", build / "ia64-llvm.so", record_stats=False)
    with tempfile.TemporaryDirectory() as temp:
        executable = Path(temp) / qemu.name
        shutil.copy(qemu, executable)
        plugin_dir = executable.parent / "ia64-llvm"
        plugin_dir.mkdir()
        if builtin:
            check(executable, *args[1:], "tcg-llvm,cachesize=1G")
            (plugin_dir / "ia64-llvm.so").write_bytes(b"invalid shared library")
            check(executable, *args[1:], "tcg-llvm,cachesize=1G")
        else:
            (plugin_dir / "ia64-llvm.so").symlink_to(build / "ia64-llvm.so")
            assert not has_builtin(executable)
