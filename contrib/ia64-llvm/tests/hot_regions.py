# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the built-in IA-64 JIT with a guest that needs no cross assembler."""

import argparse
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SOURCE / "tests/unit"))
from ia64.encoding_branch import br_cond
from ia64.encoding_common import bundle_words, nop_i, nop_m
from ia64.encoding_constants import IA64_PSR_IC
from ia64.encoding_integer import adds, cmp_eq_imm, movl_mlx
from ia64.encoding_system import srlz_d, ssm
from ia64.qmp import QmpClient
from ia64.state import parse_state

ENTRY = 0x2000000
REGION_SIZE = 256


def bundle(*slots):
    return struct.pack("<QQ", *bundle_words(*slots))


def branch(pc, target, predicate=0):
    return bundle(0x11, nop_m(), nop_i(), br_cond(pc, target, predicate))


def guest(cold, hot, rounds):
    # Each region increments r16 eight times. Two immediates distinguish its
    # machine code even when there are more regions than fit in one immediate.
    image = bytearray(bundle(0x01, ssm(IA64_PSR_IC), nop_i(), nop_i()))
    image += bundle(0x01, srlz_d(), adds(16, 0, 0), adds(17, 0, 0))
    image += bundle(*movl_mlx(20, rounds))
    image += branch(ENTRY + len(image), ENTRY + REGION_SIZE)
    image += bytes(REGION_SIZE - len(image))
    tail = bundle(0x01, adds(16, 1, 16), adds(17, 1, 17), adds(18, 1, 18)) * 7
    tail += branch(128, REGION_SIZE)
    for index in range(cold + hot):
        first = bundle(
            0x01, adds(16, 1, 16), adds(17, index & 8191, 17), adds(18, index >> 13, 18)
        )
        image += (first + tail).ljust(REGION_SIZE, b"\0")
    image += bundle(0x01, nop_m(), adds(20, -1, 20), nop_i())
    image += bundle(0x01, nop_m(), cmp_eq_imm(7, 6, 0, 20), nop_i())
    image += branch(ENTRY + len(image), ENTRY + REGION_SIZE * (1 + cold), 6)
    done = ENTRY + len(image)
    image += branch(done, done)
    return image, done


def check_hot_regions(qemu, *, stress=False, library=None):
    cold, hot, rounds = (32769, 2050, 4096) if stress else (0, 1, 20_000_000)
    image, done = guest(cold, hot, rounds)
    accel = "tcg-llvm,llvm-stats=stats"
    if stress:
        # Force several full TB flushes while the one-shot regions execute.
        accel += ",tb-size=4"
    if library is not None:
        accel += ",llvm-library=" + str(library).replace(",", ",,")
    with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
        work = Path(tmp)
        (work / "guest.bin").write_bytes(image)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(30)
        port = listener.getsockname()[1]
        command = [
            qemu,
            "-accel",
            accel,
            "-machine",
            "itanium2-vpc,firmware=none,nvram=none",
            "-m",
            "128M",
            "-nodefaults",
            "-display",
            "none",
            "-S",
            "-qmp",
            f"tcp:127.0.0.1:{port}",
            "-device",
            f"loader,file=guest.bin,addr={ENTRY},force-raw=on",
            "-device",
            f"loader,addr={ENTRY},cpu-num=0",
        ]
        with subprocess.Popen(command, cwd=work) as proc:
            try:
                conn, _ = listener.accept()
                with (
                    conn,
                    conn.makefile("r", encoding="utf-8") as reader,
                    conn.makefile("w", encoding="utf-8") as writer,
                ):
                    qmp = QmpClient(reader, writer)
                    deadline = time.monotonic() + 60
                    while True:
                        qmp.execute("cont")
                        time.sleep(0.1)
                        qmp.execute("stop")
                        state = parse_state(qmp.hmp("info registers"))
                        if state.ip == done:
                            break
                        if time.monotonic() >= deadline:
                            raise RuntimeError(f"guest timed out at {state.ip:#x}")
                    assert state.gr[16] == 8 * (cold + hot * rounds), state.gr[16]
                    expected = sum((i & 8191) + 7 for i in range(cold))
                    expected += rounds * sum(
                        (i & 8191) + 7 for i in range(cold, cold + hot)
                    )
                    assert state.gr[17] == expected, state.gr[17]
                    qmp.quit()
                    status = proc.wait(timeout=10)
                    if status:
                        raise subprocess.CalledProcessError(status, command)
            finally:
                if proc.poll() is None:
                    proc.kill()
        header, totals, *_ = (work / "stats").read_text().splitlines()
        fields = header.split()
        stats = dict(zip(fields[::2], map(int, fields[1::2])))
        runs = int(totals.split()[1])
        assert stats["submitted"] == hot, header
        assert stats["compiled"] > 0 and runs > 0, (header, totals)
        if stress:
            assert stats["keys"] < cold, header
        print(f"JIT submissions {hot}, native executions {runs}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu", type=Path)
    parser.add_argument("--stress", action="store_true")
    args = parser.parse_args()
    check_hot_regions(args.qemu.resolve(), stress=args.stress)
