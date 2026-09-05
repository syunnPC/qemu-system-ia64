#!/usr/bin/env python3
"""IA-64 SAL machine-check and INIT lifecycle tests."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import select
import socket
import tempfile
import time

from qemu_test import QemuSystemTest

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_fat_disk
from ia64.protocol import ProtocolParser


RAS_CASES = {
    "sal-entry", "os-init-register", "os-mca-register",
    "ap-rendezvous-register", "ap-online",
    "mca-wakeup-register",
    "os-init-primary", "os-init-state", "init-record", "init-clear",
    "mca-injection-ready", "os-mca-return", "os-mca-state",
    "os-mca-gr11", "os-init-secondary", "mca-record-lifecycle",
}
MCA_RETURN_CONTROL_PA = 0x01800000
MCA_RETURN_OBSERVED_PA = MCA_RETURN_CONTROL_PA + 8
MCA_PRESERVATION_READY_PA = MCA_RETURN_CONTROL_PA + 16


class Ia64Ras(Ia64FirmwareTest):
    @staticmethod
    def _wait_stopped(vm):
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if vm.cmd("query-status")["status"] == "paused":
                return
            time.sleep(0.01)
        raise AssertionError("QEMU did not stop")

    @staticmethod
    def _qtest_command(connection, command):
        connection.sendall(command.encode("ascii") + b"\n")
        response = b""
        while not response.endswith(b"\n"):
            data = connection.recv(4096)
            if not data:
                raise AssertionError("qtest connection closed")
            response += data
        return response.decode("ascii").strip()

    def _wait_case(self, vm, case_id, timeout=60.0):
        parser = ProtocolParser("ras")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not vm.is_running():
                raise AssertionError("QEMU exited before the RAS test case")
            remaining = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select(
                [vm.console_socket], [], [], min(0.1, remaining))
            if not readable:
                continue
            data = vm.console_socket.recv(4096)
            if not data:
                raise AssertionError("console closed before the RAS test case")
            for case in parser.feed(data):
                if case.case_id == case_id:
                    self.assertTrue(case.passed)
                    return
        raise AssertionError(
            f"timed out waiting for RAS case {case_id!r}\n"
            f"{parser.result.raw_console[-4000:]}")

    def _wait_preservation_ready(self, connection):
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            value = self._qtest_command(
                connection, f"readq 0x{MCA_PRESERVATION_READY_PA:x}")
            if value == "OK 0x0000000000000001":
                return
            time.sleep(0.001)
        self.fail("guest did not enter the register-preservation probe")

    def _run_os_mca_action(self, return_value, *, expect_reset):
        suffix = str(-return_value)
        encoded_return = return_value & ((1 << 64) - 1)
        expected_return = f"OK 0x{encoded_return:016x}"
        disk = Path(self.scratch_file(f"ras-mca-{suffix}.img"))
        nvram = self.make_nvram(f"ras-mca-{suffix}.nvram")
        qtest_dir = tempfile.TemporaryDirectory(prefix="ia64-ras-mca-")
        qtest_path = Path(qtest_dir.name) / "qtest"
        make_fat_disk(disk, app_path("ras"))

        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(qtest_path))
        listener.listen(1)
        listener.settimeout(10.0)
        try:
            vm = self.launch_ia64(
                media=disk, smp=2, memory="1G",
                machine_options=f"firmware-console=serial,nvram={nvram}",
                extra_args=("-accel", "tcg,thread=multi", "-qtest",
                            f"unix:{qtest_path}"))
            connection, _ = listener.accept()
            connection.settimeout(10.0)
            try:
                self._wait_case(vm, "mca-injection-ready")
                self._wait_preservation_ready(connection)
                vm.cmd("stop")
                self._wait_stopped(vm)
                halt_state = self._qtest_command(
                    connection, "ia64-sapic halt-state 0 0 0 0 0")
                self.assertEqual(halt_state, "OK 0")
                response = self._qtest_command(
                    connection,
                    f"writeq 0x{MCA_RETURN_CONTROL_PA:x} "
                    f"0x{encoded_return:x}")
                self.assertEqual(response, "OK")
                response = self._qtest_command(
                    connection,
                    "ia64-ras-inject processor 0 1 "
                    "0x1111222233334444 0x56789abcdef000 "
                    "0xaabbccddeeff0011 0")
                self.assertEqual(response, "OK 1")
                vm.cmd("cont")
                if expect_reset:
                    event = vm.event_wait("RESET", timeout=10.0)
                    self.assertTrue(event["data"]["guest"])
                    self.assertEqual(event["data"]["reason"], "guest-reset")
                else:
                    deadline = time.monotonic() + 10.0
                    while True:
                        vm.cmd("stop")
                        self._wait_stopped(vm)
                        observed = self._qtest_command(
                            connection,
                            f"readq 0x{MCA_RETURN_OBSERVED_PA:x}")
                        halt_state = self._qtest_command(
                            connection, "ia64-sapic halt-state 0 0 0 0 0")
                        if (observed == expected_return and
                                int(halt_state.removeprefix("OK "), 0) & 2):
                            break
                        if time.monotonic() >= deadline:
                            self.fail(f"OS_MCA return {return_value} did not halt")
                        vm.cmd("cont")
                        time.sleep(0.01)
            finally:
                connection.close()
        finally:
            listener.close()
            qtest_dir.cleanup()

    def test_sal_lifecycle(self):
        disk = Path(self.scratch_file("ras.img"))
        nvram = self.make_nvram("ras.nvram")
        qtest_dir = tempfile.TemporaryDirectory(prefix="ia64-ras-")
        qtest_path = Path(qtest_dir.name) / "qtest"
        make_fat_disk(disk, app_path("ras"))

        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(qtest_path))
        listener.listen(1)
        listener.settimeout(10.0)
        try:
            vm = self.launch_ia64(
                media=disk, smp=2, memory="1G",
                machine_options=f"firmware-console=serial,nvram={nvram}",
                extra_args=("-accel", "tcg,thread=multi", "-qtest",
                            f"unix:{qtest_path}"))
            connection, _ = listener.accept()
            connection.settimeout(10.0)
            try:
                def inject_mca(case):
                    if case.case_id != "mca-injection-ready":
                        return
                    self._wait_preservation_ready(connection)
                    vm.cmd("stop")
                    self._wait_stopped(vm)
                    online = self._qtest_command(
                        connection, "readq 0xfe800060")
                    self.assertEqual(online, "OK 0x0000000000000003")
                    response = self._qtest_command(
                        connection,
                        "ia64-ras-inject processor 0 1 "
                        "0x1111222233334444 0x56789abcdef000 "
                        "0xaabbccddeeff0011 0")
                    self.assertEqual(response, "OK 1")
                    vm.cmd("cont")

                result = self.wait_ia64_suite(
                    vm, "ras", RAS_CASES, timeout=90.0,
                    on_case=inject_mca)
                self.assertSetEqual(set(result.cases), RAS_CASES)
                vm.cmd("stop")
                self._wait_stopped(vm)
                ras_state = self._qtest_command(
                    connection, "ia64-sapic ras-state 0 0 0 0 0")
                self.assertEqual(ras_state, "OK 0")
                vm.cmd("cont")
            finally:
                connection.close()
        finally:
            listener.close()
            qtest_dir.cleanup()

    def test_os_mca_warm_reset(self):
        self._run_os_mca_action(-1, expect_reset=True)

    def test_os_mca_cold_reset(self):
        self._run_os_mca_action(-2, expect_reset=True)

    def test_os_mca_halt(self):
        self._run_os_mca_action(-3, expect_reset=False)


if __name__ == "__main__":
    QemuSystemTest.main()
