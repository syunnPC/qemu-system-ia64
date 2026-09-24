#!/usr/bin/env python3
"""Firmware boot tests for the HP i2000 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
from subprocess import run

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.efi_build import app_path
from ia64.media import make_el_torito_iso, make_fat_disk
from ia64.protocol import wait_for_suite


SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "console-output", "console-variables",
    "low-memory-allocation",
}

GRAPHICS_CASES = {
    "protocols", "protocol-list", "pci-location", "pci-dma",
    "pci-attributes", "pci-bars", "device-path", "gop", "vbe-mode", "uga",
    "framebuffer-io", "memory-map",
}

INPUT_CASES = {
    "text-input-ex", "ready-basic", "read-key-stroke",
    "ready-modifier", "modifier-key", "modifier-state", "ready-extended",
    "extended-scan-code",
}

LOADER_CASES = {
    "image-placement", "low-memory-map", "runtime-map",
    "firmware-aperture", "sal-entrypoint", "sal-memory-descriptors",
    "sal-call", "sal-chipset-config", "direct-alias", "automatic-allocation",
    "address-allocation", "acpi-topology", "acpi-console",
}

PCI_ROOT_CASES = {
    "system-table", "protocols", "device-paths", "config-access",
    "host-enumeration",
}

RUNTIME_CASES = {"boot-variables", "get-time", "set-time", "fadt-reset"}

SMP_MERCED_CASES = {
    "sal-ap-wake", "merced-rendezvous", "merced-rendezvous-return",
    "zero-alat-check-reload",
}


class HPI2000Boot(QemuSystemTest):
    @staticmethod
    def send_keys(vm, qcodes):
        vm.cmd("send-key", keys=[
            {"type": "qcode", "data": qcode} for qcode in qcodes
        ], hold_time=50)

    def media_path(self, name: str) -> Path:
        configured = os.environ.get("IA64_TEST_MEDIA_DIR")
        if not configured:
            return Path(self.scratch_file(name))

        directory = Path(configured)
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"hp-i2000-{os.getpid()}-{name}"
        self.addCleanup(path.unlink, missing_ok=True)
        return path

    def test_firmware_ready(self):
        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "quadro2",
            "-net", "none",
        )
        vm.launch()

        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm,
        )
        self.assertIn(b"PCI Root Bridge I/O:  published", output)
        self.assertIn(b"PCI Host Bridge:       published", output)
        self.assertIn(b"ACPI MCFG (PCIe):     suppressed", output)
        self.assertIn(b"ACPI SSDT (CPU/UART/PS2): published", output)
        self.assertIn(b"SCSI controller:      ISP12160 polling", output)
        self.assertIn(b" bmdma=0x", output)
        self.assertIn(b"Console In:           Serial/PS2/USB ready", output)
        self.assertIn(b"NVRAM Variables:      enabled", output)
        self.assertIn(b"EFI Time Services:    enabled", output)
        self.assertIn(
            b"ResetSystem:          enabled (shutdown unavailable)", output
        )
        self.assertIn(b"Firmware flags:       0x000000000000001B", output)
        self.assertIn(b"GOP/UGA VGA text console ready", output)
        self.assertIn(b"Graphics Output:      GOP/UGA VGA BGRx", output)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

    def test_scsi_disk_boot(self):
        self.run_scsi_disk_boot("none")

    def test_scsi_disk_boot_with_graphics(self):
        self.run_scsi_disk_boot("quadro2")

    def run_scsi_disk_boot(self, vga):
        self.require_accelerator("tcg")
        path = self.media_path(f"isp12160-{vga}.img")
        make_fat_disk(path, app_path("smoke"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw",
            "-vga", vga,
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertIn("SCSI controller:      ISP12160 polling",
                      result.raw_console)
        self.assertTrue(vm.is_running(), "QEMU exited after SCSI disk boot")

    def test_default_optical_boot(self):
        self.require_accelerator("tcg")
        trace_help = run([self.qemu_bin, "-d", "trace:help"],
                         capture_output=True, encoding="utf8")
        if (trace_help.returncode == 1 and
                trace_help.stdout.startswith("Log items (comma separated):")):
            self.skipTest("requires the log tracing backend")
        trace_help.check_returncode()
        if "ide_atapi_cmd_read" not in trace_help.stdout.splitlines():
            self.skipTest("requires the ide_atapi_cmd_read trace event")

        disk = self.media_path("blank-scsi.img")
        optical = self.media_path("optical.iso")
        trace = Path(self.scratch_file("ide.trace"))
        with disk.open("wb") as image:
            image.truncate(16 * 1024 * 1024)
        make_el_torito_iso(optical, app_path("smoke"), platform_id=0xEF)

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={disk},format=raw",
            "-drive",
            f"file={optical},format=raw,media=cdrom,readonly=on",
            "-d", "trace:ide_atapi_cmd_read",
            "-D", str(trace),
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertIn("read dma", trace.read_text())
        self.assertTrue(vm.is_running(), "QEMU exited after optical boot")

    def test_pci_root_protocols(self):
        self.require_accelerator("tcg")
        path = self.media_path("pci-root.img")
        make_fat_disk(path, app_path("i2000-pci-root"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "quadro2",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-pci-root", PCI_ROOT_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after PCI root protocol test")

    def test_runtime_services(self):
        self.require_accelerator("tcg")
        path = self.media_path("runtime.img")
        make_fat_disk(path, app_path("i2000-runtime"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-runtime", RUNTIME_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        event = vm.event_wait("RESET", timeout=10.0)
        self.assertTrue(event["data"]["guest"])
        self.assertEqual(event["data"]["reason"], "guest-reset")
        self.assertTrue(vm.is_running(),
                        "QEMU exited after runtime service test")

    def test_graphics_protocols(self):
        self.require_accelerator("tcg")
        path = self.media_path("graphics.img")
        make_fat_disk(path, app_path("graphics"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "quadro2",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "graphics", GRAPHICS_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after graphics protocol test")

    def test_keyboard_input(self):
        self.require_accelerator("tcg")
        path = self.media_path("input.img")
        make_fat_disk(path, app_path("input"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()
        sent = set()

        def respond(case):
            if not case.passed or case.case_id in sent:
                return
            if case.case_id == "ready-basic":
                self.send_keys(vm, ("x",))
            elif case.case_id == "ready-modifier":
                self.send_keys(vm, ("shift", "a"))
            elif case.case_id == "ready-extended":
                self.send_keys(vm, ("up",))
            else:
                return
            sent.add(case.case_id)

        result = wait_for_suite(
            vm.console_socket, "input", INPUT_CASES, 45.0,
            on_case=respond, process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertEqual(
            sent, {"ready-basic", "ready-modifier", "ready-extended"})
        self.assertTrue(vm.is_running(),
                        "QEMU exited after keyboard input test")

    def test_loader_memory_layout(self):
        self.require_accelerator("tcg")
        path = self.media_path("loader.img")
        make_fat_disk(path, app_path("i2000-loader"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "2",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-loader", LOADER_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after loader memory-layout test")

    def test_smp_rendezvous(self):
        self.require_accelerator("tcg")
        path = self.media_path("smp-merced.img")
        make_fat_disk(path, app_path("smp-merced"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg,thread=multi",
            "-cpu", "merced",
            "-m", "4G",
            "-smp", "2",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smp-merced", SMP_MERCED_CASES, 180.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertSetEqual(set(result.cases), SMP_MERCED_CASES)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after SMP rendezvous test")


if __name__ == "__main__":
    QemuSystemTest.main()
