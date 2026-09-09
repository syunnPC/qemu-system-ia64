#!/usr/bin/env python3
"""Behavior tests for IA-64 CPU selection and character-device CLI ownership."""

from __future__ import annotations

from contextlib import contextmanager
import os
import socket
import subprocess
import sys
import tempfile

from process import connect_tcp, connect_unix, terminate_process
from qmp import QmpClient


CPU_MODELS = [
    "deerfield-1000",
    "madison-1400-1.5m", "madison-1400-4m", "madison-1500",
    "madison-1600-3m", "madison-1600-9m",
    "mckinley-1000", "mckinley-900", "merced-800",
    "montecito-9010", "montecito-9015", "montecito-9020",
    "montecito-9030", "montecito-9040", "montecito-9050",
    "montvale-9110n", "montvale-9120n", "montvale-9130m",
    "montvale-9140m", "montvale-9140n", "montvale-9150m",
    "montvale-9150n", "montvale-9152m",
]
CPU_ALIASES = {
    "merced": "merced-800",
    "mckinley": "mckinley-1000",
    "deerfield": "deerfield-1000",
    "madison": "madison-1600-3m",
    "montecito": "montecito-9050",
    "montvale": "montvale-9150n",
    "itanium": "merced-800",
    "itanium2": "montecito-9050",
}


def test_cpu_models(qemu: str) -> None:
    result = subprocess.run(
        [qemu, "-cpu", "help"],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=8)
    lines = [line.strip() for line in result.stdout.splitlines()]
    expected = ["Available CPUs:", *CPU_MODELS, "CPU aliases:"]
    expected += [f"{alias} (alias for {model})"
                 for alias, model in CPU_ALIASES.items()]
    if result.returncode != 0 or lines != expected:
        raise RuntimeError(
            f"unexpected CPU model list: {lines!r}\n{result.stdout}")


def test_cpu_model_names_are_exact(qemu: str) -> None:
    for model in ("ia64-cpu", "merced-800-ia64-cpu", "generic", "max",
                  "madison-base", "montecito-base", "madison-zx6000",
                  "madison-1.5m", "madison-3m", "madison-4m",
                  "madison-6m", "madison-9m", "madison-1400",
                  "madison-1600"):
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


@contextmanager
def cpu_machine(qemu: str, machine: str, cpu: str | None = None):
    with tempfile.TemporaryDirectory(prefix="ia64-cpu-cli-") as tmpdir:
        qmp_path = os.path.join(tmpdir, "qmp.sock")
        args = [qemu, "-machine", f"{machine},nvram=none", "-S",
                "-bios", "none", "-nodefaults", "-display", "none",
                "-audio", "driver=none",
                "-monitor", "none", "-serial", "none",
                "-qmp", f"unix:{qmp_path},server=on,wait=off"]
        if cpu is not None:
            args += ["-cpu", cpu]
        proc = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            with connect_unix(proc, qmp_path, 3.0, "QMP socket") as sock:
                with sock.makefile("r") as reader, \
                        sock.makefile("w") as writer:
                    yield QmpClient(reader, writer)
        except Exception as exc:
            terminate_process(proc)
            output, _ = proc.communicate()
            raise RuntimeError(
                f"{machine} CPU {cpu or 'default'}: {exc}\n{output}") from exc
        finally:
            terminate_process(proc)


def assert_cpu_type(qmp: QmpClient, model: str) -> None:
    cpus = qmp.execute("query-cpus-fast")
    if len(cpus) != 1:
        raise RuntimeError(f"expected one CPU, got {cpus!r}")
    actual = qmp.execute("qom-get", {"path": cpus[0]["qom-path"],
                                    "property": "type"})
    if actual != f"{model}-ia64-cpu":
        raise RuntimeError(f"expected CPU type {model!r}, got {actual!r}")


def test_cpu_aliases(qemu: str) -> None:
    machines = set()
    for alias, model in CPU_ALIASES.items():
        with cpu_machine(qemu, "itanium2-vpc", alias) as qmp:
            assert_cpu_type(qmp, model)
            if not machines:
                machines = {item["name"]
                            for item in qmp.execute("query-machines")}
    # Board CPU restrictions must apply after alias resolution.
    if "hp-i2000" in machines:
        for alias in ("merced", "itanium"):
            with cpu_machine(qemu, "hp-i2000", alias) as qmp:
                assert_cpu_type(qmp, "merced-800")


def test_machine_cpu_defaults(qemu: str) -> None:
    defaults = {
        "itanium-vpc": "merced-800",
        "itanium2-vpc": "montecito-9050",
        "hp-i2000": "merced-800",
        "hp-zx6000": "madison-1500",
        "hp-rx2660": "montecito-9010",
    }
    with cpu_machine(qemu, "itanium2-vpc") as qmp:
        machines = {item["name"]: item
                    for item in qmp.execute("query-machines")}
    for machine, model in defaults.items():
        if machine not in machines:
            continue
        info = machines[machine]
        if info["default-cpu-type"] != f"{model}-ia64-cpu":
            raise RuntimeError(f"unexpected CPU default: {info!r}")
        with cpu_machine(qemu, machine) as qmp:
            assert_cpu_type(qmp, model)


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
        ("CPU aliases resolve to canonical models", test_cpu_aliases),
        ("machine CPU defaults are canonical", test_machine_cpu_defaults),
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
