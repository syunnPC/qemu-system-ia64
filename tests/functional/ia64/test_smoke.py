#!/usr/bin/env python3
"""IA-64 firmware and EFI application smoke tests."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_el_torito_iso, make_fat_disk


SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "console-output", "console-variables",
}


class Ia64FirmwareSmoke(Ia64FirmwareTest):
    def test_fixed_image_relocated_firmware(self):
        path = Path(self.scratch_file("fixed.img"))
        make_fat_disk(path, app_path("smoke-fixed"))
        vm = self.launch_ia64(
            media=path, machine_options=(
                "firmware-console=serial,nvram=none,firmware-base=0x400000"))
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)
        vm.cmd("system_reset")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)

    def test_fixed_image_firmware_collision(self):
        path = Path(self.scratch_file("collision.img"))
        make_fat_disk(path, app_path("smoke-fixed"))
        vm = self.launch_ia64(
            media=path, machine_options="firmware-console=serial,nvram=none")
        wait_for_console_pattern(self, "Block I/O: LoadImage failed", vm=vm)

    def make_disk(self, name: str = "smoke.img") -> Path:
        path = Path(self.scratch_file(name))
        make_fat_disk(path, app_path("smoke"))
        return path

    def test_cold_boot_and_reset(self):
        vm = self.launch_ia64(
            media=self.make_disk(),
            machine_options="firmware-console=serial,nvram=none")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)
        vm.cmd("system_reset")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)

    def test_vga_console(self):
        vm = self.launch_ia64(
            media=self.make_disk("vga.img"),
            machine_options="firmware-console=vga,nvram=none")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)

    def test_serial_without_vga(self):
        vm = self.launch_ia64(
            media=self.make_disk("serial.img"),
            machine_options="firmware-console=vga,nvram=none",
            extra_args=("-vga", "none"))
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)

    def test_icount_boot(self):
        vm = self.launch_ia64(
            media=self.make_disk("icount.img"),
            machine_options="firmware-console=serial,nvram=none",
            extra_args=("-icount", "shift=3"))
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES, timeout=40.0)

    def test_el_torito_efi_platform(self):
        path = Path(self.scratch_file("efi-platform.iso"))
        make_el_torito_iso(path, app_path("smoke"), platform_id=0xEF)
        vm = self.launch_ia64(
            media=path, optical=True,
            machine_options="firmware-console=serial,nvram=none")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)

    def test_el_torito_legacy_platform(self):
        path = Path(self.scratch_file("legacy-platform.iso"))
        make_el_torito_iso(path, app_path("smoke"), platform_id=0)
        vm = self.launch_ia64(
            media=path, optical=True,
            machine_options="firmware-console=serial,nvram=none")
        self.wait_ia64_suite(vm, "smoke", SMOKE_CASES)


if __name__ == "__main__":
    QemuSystemTest.main()
