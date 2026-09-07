#!/usr/bin/env python3
"""External EFI client coverage for IA-64 boot and runtime services."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import QemuSystemTest

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_fat_disk


SERVICE_CASES = {
    "memory-services", "event-services", "protocol-services",
    "multiple-protocol-services",
    "controller-services", "image-services", "image-section-extents",
    "image-section-bounds", "start-image-connect",
    "memory-primitives", "time-services", "variable-services",
    "block-disk-protocols", "pci-root-io", "pci-io",
    "graphics-output", "tcg-no-tpm", "sal-state-info-no-log",
    "sal-rse-byte-order",
}
EXITBS_CASES = {
    "memory-map", "exit-boot-services", "system-table-handoff",
    "system-table-crc",
    "runtime-pointer-ranges", "runtime-function-ranges",
    "configuration-table-ranges", "configuration-tables-preserved",
    "legacy-text-handoff", "convert-pointer-reserved-bits",
    "runtime-get-time",
    "set-virtual-address-map", "runtime-virtual-boot-variable",
    "sal-wakeup-address",
}


class Ia64EfiServices(Ia64FirmwareTest):
    def test_services(self):
        disk = Path(self.scratch_file("services.img"))
        nvram = self.make_nvram()
        make_fat_disk(
            disk, app_path("services"),
            extra_boot_files=((b"START   EFI",
                               app_path("start-image-child")),))
        vm = self.launch_ia64(
            media=disk,
            machine_options=(
                f"firmware-console=serial,nvram={nvram}"))
        result = self.wait_ia64_suite(
            vm, "services", SERVICE_CASES, timeout=35.0)
        self.assertSetEqual(set(result.cases), SERVICE_CASES)

    def test_exit_boot_services(self):
        disk = Path(self.scratch_file("exitbs.img"))
        make_fat_disk(disk, app_path("exitbs"))
        vm = self.launch_ia64(
            media=disk,
            machine_options="firmware-console=vga,nvram=none")
        self.wait_ia64_suite(vm, "exitbs", EXITBS_CASES, timeout=35.0)


if __name__ == "__main__":
    QemuSystemTest.main()
