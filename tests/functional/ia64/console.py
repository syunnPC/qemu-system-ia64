"""QEMU launch helpers for IA-64 firmware functional tests."""

# SPDX-License-Identifier: GPL-2.0-or-later

import logging
from pathlib import Path

from qemu_test import QemuSystemTest

from ia64.efi_build import firmware_path
from ia64.protocol import wait_for_suite


SOURCE_ROOT = Path(__file__).resolve().parents[3]


class Ia64FirmwareTest(QemuSystemTest):
    """Base class that boots the in-tree IA-64 firmware and test media."""

    def make_nvram(self, name: str = "nvram.bin") -> Path:
        path = Path(self.scratch_file(name))
        path.write_bytes(bytes(64 * 1024))
        return path

    def launch_ia64(self, *, name: str = "default", media: Path | None = None,
                    optical: bool = False, machine_options: str = "",
                    machine_type: str = "ia64-vpc",
                    memory: str = "512M", smp: int | str = 1,
                    extra_args: tuple[str, ...] = (),
                    drive_args: tuple[str, ...] | None = None):
        vm = self.get_vm(name=name)
        machine = machine_type
        if "nvram=" not in machine_options:
            machine += ",nvram=none"
        if machine_options:
            machine += "," + machine_options
        vm.set_machine(machine)
        vm.set_console()
        # QEMUMachine starts with ``-vga none`` for generic headless tests.
        # Keep the display backend headless, but restore this machine's
        # guest-visible default adapter so firmware graphics and PCI paths
        # are exercised.
        vm.add_args("-vga", "ati", "-smp", str(smp), "-m", memory,
                    "-bios", str(firmware_path()),
                    "-monitor", "none", "-L", str(SOURCE_ROOT / "pc-bios"))
        if drive_args is not None:
            vm.add_args(*drive_args)
        elif media is not None:
            drive = f"file={media},format=raw"
            if optical:
                drive += ",media=cdrom,readonly=on"
            vm.add_args("-drive", drive)
        if extra_args:
            vm.add_args(*extra_args)
        vm.launch()
        return vm

    def wait_ia64_suite(self, vm, suite: str, required_cases,
                        timeout: float = 25.0, on_case=None):
        result = wait_for_suite(
            vm.console_socket, suite, required_cases, timeout,
            on_case=on_case, process_alive=vm.is_running)
        logger = logging.getLogger("console")
        for line in result.raw_console.replace("\r", "").splitlines():
            logger.debug(line)
        self.assertTrue(vm.is_running(), "QEMU exited after IA64TEST DONE")
        return result
