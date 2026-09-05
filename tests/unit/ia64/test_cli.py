#!/usr/bin/env python3
"""Behavior tests for IA-64-related character-device CLI ownership."""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile

from process import connect_tcp, connect_unix, terminate_process


def test_cpu_models(qemu: str) -> None:
    result = subprocess.run(
        [qemu, "-cpu", "help"],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=8)
    lines = [line.strip() for line in result.stdout.splitlines()]
    models = [line for line in lines
              if line and line != "Available CPUs:"]
    expected = [
        "deerfield", "itanium", "itanium2",
        "madison-1.5m", "madison-3m", "madison-4m", "madison-6m",
        "madison-9m", "madison", "madison-zx6000",
        "mckinley-1000", "mckinley-900", "mckinley", "merced",
        "montecito-9010", "montecito-9015", "montecito-9020",
        "montecito-9030", "montecito-9040", "montecito-9050", "montecito",
        "montvale-9110n", "montvale-9120n", "montvale-9130m",
        "montvale-9140m", "montvale-9140n", "montvale-9150m",
        "montvale-9150n", "montvale-9152m", "montvale",
    ]
    if result.returncode != 0 or models != expected:
        raise RuntimeError(
            f"unexpected CPU model list: {models!r}\n{result.stdout}")


def test_cpu_model_names_are_exact(qemu: str) -> None:
    for model in ("ia64-cpu", "merced-ia64-cpu", "generic", "max"):
        result = subprocess.run([
            qemu, "-machine", "none", "-display", "none",
            "-monitor", "none", "-serial", "none", "-cpu", model,
        ], check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=8)
        if result.returncode == 0 or \
                f"unable to find CPU model '{model}'" not in result.stdout:
            raise RuntimeError(
                f"unexpectedly accepted CPU model {model!r}\n"
                f"{result.stdout}")


def test_duplicate_debug_port(qemu: str) -> None:
    result = subprocess.run([
        qemu, "-machine", "none", "-display", "none", "-monitor", "none",
        "-serial", "none", "-debug-port", "none", "-debug-port", "none",
    ], check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=8)
    if result.returncode == 0 or \
            "only one -debug-port option is supported" not in result.stdout:
        raise RuntimeError("multiple -debug-port options were not rejected\n" +
                           result.stdout)


def test_nographic_debug_stdio(qemu: str) -> None:
    with tempfile.TemporaryDirectory(prefix="ia64-cli-") as tmpdir:
        qmp_path = os.path.join(tmpdir, "qmp.sock")
        proc = subprocess.Popen([
            qemu, "-machine", "none", "-S", "-nographic",
            "-monitor", "none", "-serial", "none",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
            "-debug-port", "stdio",
        ], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        try:
            with connect_unix(proc, qmp_path, 3.0, "QMP socket"):
                pass
        finally:
            terminate_process(proc)


def test_debug_tcp_server(qemu: str) -> None:
    probe = socket.socket()
    try:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    finally:
        probe.close()

    proc = subprocess.Popen([
        qemu, "-machine", "none", "-S", "-display", "none",
        "-monitor", "none", "-serial", "none",
        "-debug-port", f"tcp:127.0.0.1:{port},server=on,wait=off",
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        with connect_tcp(proc, "127.0.0.1", port, 3.0,
                         "debug TCP server"):
            pass
    finally:
        terminate_process(proc)


def main() -> int:
    if len(sys.argv) != 2:
        print("Bail out! usage: test_cli.py QEMU_SYSTEM_IA64")
        return 1
    tests = [
        ("CPU model list is exact", test_cpu_models),
        ("CPU model names are exact", test_cpu_model_names_are_exact),
        ("duplicate debug-port rejected", test_duplicate_debug_port),
        ("debug-port owns nographic stdio", test_nographic_debug_stdio),
        ("debug-port TCP server accepts", test_debug_tcp_server),
    ]
    print("TAP version 13")
    print(f"1..{len(tests)}")
    failed = 0
    for index, (name, function) in enumerate(tests, 1):
        try:
            function(sys.argv[1])
            print(f"ok {index} - {name}")
        except Exception as exc:
            failed += 1
            print(f"not ok {index} - {name}")
            for line in str(exc).splitlines():
                print(f"# {line}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
