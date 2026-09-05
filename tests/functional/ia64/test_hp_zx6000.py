#!/usr/bin/env python3
"""Firmware boot tests for the HP zx6000 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import struct
from pathlib import Path

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.efi_build import app_path
from ia64.media import make_el_torito_iso, make_fat_disk
from ia64.protocol import wait_for_suite


SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "console-output",
}

MPT_SMOKE_CASES = SMOKE_CASES | {"controller-device-path"}

GRAPHICS_CASES = {
    "protocols", "protocol-list", "pci-location", "pci-dma",
    "pci-attributes", "pci-bars", "device-path", "gop", "vbe-mode", "uga",
    "framebuffer-io", "memory-map", "pci-vga-attributes",
    "pci-vga-root-attributes", "pci-vga-root-resources",
    "pci-vga-passthrough", "pci-controller-paths",
}

INPUT_CASES = {
    "text-input-ex", "ready-basic", "read-key-stroke",
    "ready-modifier", "modifier-key", "modifier-state", "ready-extended",
    "extended-scan-code",
}

ACPI_RECLAIM_TABLE_BASE = 0x00802000
ACPI_HEADER_SIZE = 36
ZX6000_ACPI_PM_BASE = 0xFF5C0000
ZX6000_ACPI_SCI_GSI = 23
ZX6000_LEGACY_IO_BASE = 0x00000FFFFC000000


class HPZx6000Boot(QemuSystemTest):
    @staticmethod
    def read_physical(vm, address, size):
        data = bytearray()

        while len(data) < size:
            count = min(size - len(data), 256)
            current = address + len(data)
            output = vm.cmd(
                "human-monitor-command",
                command_line=f"xp /{count}bx 0x{current:x}",
            )
            chunk = bytearray()
            for line in output.splitlines():
                _, separator, values = line.partition(":")
                if separator:
                    chunk.extend(
                        int(value, 16)
                        for value in re.findall(r"0x([0-9a-fA-F]{2})\b",
                                                values)
                    )
            if len(chunk) != count:
                raise AssertionError(
                    f"physical read at 0x{current:x} returned "
                    f"{len(chunk)} of {count} bytes"
                )
            data.extend(chunk)
        return bytes(data)

    @classmethod
    def read_sdt(cls, vm, address):
        header = cls.read_physical(vm, address, ACPI_HEADER_SIZE)
        length = struct.unpack_from("<I", header, 4)[0]

        if length < ACPI_HEADER_SIZE or length > 8192:
            raise AssertionError(
                f"invalid ACPI table length {length} at 0x{address:x}"
            )
        return cls.read_physical(vm, address, length)

    def assert_gas(self, fadt, offset, width, address, space_id=0):
        self.assertEqual(fadt[offset], space_id)
        self.assertEqual(fadt[offset + 1], width)
        self.assertEqual(fadt[offset + 2:offset + 4], bytes(2))
        self.assertEqual(struct.unpack_from("<Q", fadt, offset + 4)[0],
                         address)

    def assert_acpi_pm_fadt(self, vm):
        rsdp = self.read_physical(vm, ACPI_RECLAIM_TABLE_BASE, 36)
        xsdt = self.read_sdt(vm, struct.unpack_from("<Q", rsdp, 24)[0])
        fadt = None

        for offset in range(ACPI_HEADER_SIZE, len(xsdt), 8):
            table = self.read_sdt(
                vm, struct.unpack_from("<Q", xsdt, offset)[0]
            )
            if table[:4] == b"FACP":
                fadt = table
                break
        self.assertIsNotNone(fadt)
        self.assertEqual(sum(fadt) & 0xff, 0)
        self.assertEqual(struct.unpack_from("<H", fadt, 46)[0],
                         ZX6000_ACPI_SCI_GSI)
        self.assertEqual(struct.unpack_from("<I", fadt, 56)[0], 0x1008)
        self.assertEqual(struct.unpack_from("<I", fadt, 64)[0], 0x100C)
        self.assertEqual(struct.unpack_from("<I", fadt, 76)[0], 0x1004)
        self.assertEqual(struct.unpack_from("<I", fadt, 80)[0], 0x1010)
        self.assertEqual((fadt[88], fadt[89], fadt[91], fadt[92]),
                         (4, 2, 4, 8))
        self.assert_gas(fadt, 148, 32, 0x1008, space_id=1)
        self.assert_gas(fadt, 172, 16, 0x100C, space_id=1)
        self.assert_gas(fadt, 208, 32, 0x1004, space_id=1)
        self.assert_gas(fadt, 220, 64, 0x1010, space_id=1)

        dsdt = self.read_sdt(vm, struct.unpack_from("<Q", fadt, 140)[0])
        self.assertEqual(dsdt[:4], b"DSDT")
        self.assertEqual(sum(dsdt) & 0xff, 0)
        self.assertEqual(dsdt.count(b"\x0c\x22\xf0\x00\x02"), 5)
        self.assertEqual(dsdt.count(b"\x0c\x22\xf0\x00\x03"), 1)
        self.assertEqual(dsdt.count(b"MBRD"), 1)
        self.assertEqual(dsdt.count(b"\x0c\x41\xd0\x0c\x02"), 1)
        self.assertEqual(dsdt.count(b"_FIX"), 0)
        motherboard_resources = (
            b"\x86\x09\x00\x01" +
            struct.pack("<II", ZX6000_ACPI_PM_BASE, 0x2000)
        )
        motherboard_resources += b"".join(
            b"\x47\x01" + struct.pack("<HHBB", port, port, 1, length)
            for port, length in (
                (0x1008, 4), (0x100C, 2), (0x1004, 4), (0x1010, 8),
            )
        )
        motherboard_resources += b"\x79\x00"
        self.assertEqual(dsdt.count(motherboard_resources), 1)

        def qword_io(type_flags, minimum, maximum, translation, length):
            return (
                b"\x8a\x2b\x00\x01\x0c" + bytes([type_flags]) +
                struct.pack("<QQQQQ", 0, minimum, maximum,
                            translation, length)
            )

        def dword_memory(minimum, maximum, translation, length):
            return (
                b"\x87\x17\x00\x00\x0c\x01" +
                struct.pack("<IIIII", 0, minimum, maximum,
                            translation, length)
            )

        root0_io = (
            (0x0000, 0x01CD, 0x01CE),
            (0x01D2, 0x03AF, 0x01DE),
            (0x03E0, 0x1FFF, 0x1C20),
        )
        for minimum, maximum, length in root0_io:
            sparse = qword_io(
                0x33, minimum, maximum,
                ZX6000_LEGACY_IO_BASE, length,
            )
            self.assertEqual(dsdt.count(sparse), 1)

        for minimum in range(0x2000, 0xC000, 0x2000):
            sparse = qword_io(
                0x33, minimum, minimum + 0x1FFF,
                ZX6000_LEGACY_IO_BASE, 0x2000,
            )
            self.assertEqual(dsdt.count(sparse), 1)

        for minimum, maximum, length in (
            (0x01CE, 0x01D1, 0x04),
            (0x03B0, 0x03DF, 0x30),
        ):
            legacy_vga_io = qword_io(
                0x33, minimum, maximum,
                ZX6000_LEGACY_IO_BASE, length,
            )
            self.assertEqual(dsdt.count(legacy_vga_io), 1)
        legacy_vga_memory = dword_memory(
            0x000A0000, 0x000FFFFF, 0, 0x00060000,
        )
        self.assertEqual(dsdt.count(legacy_vga_memory), 1)

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
        path = directory / f"hp-zx6000-{os.getpid()}-{name}"
        self.addCleanup(path.unlink, missing_ok=True)
        return path

    def test_firmware_ready(self):
        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine("hp-zx6000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-smp", "2",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
        )
        vm.launch()

        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm,
        )
        self.assertIn(b"PCI Root Bridge I/O:  published", output)
        self.assertIn(b"ACPI MCFG (PCIe):     suppressed", output)
        self.assertIn(b"SCSI controller:      LSI53C1030 MPT", output)
        self.assertIn(b"Console In:           Serial/USB ready", output)
        self.assertIn(b"GOP/UGA VGA text console ready", output)
        self.assertIn(b"Graphics Output:      GOP/UGA VGA BGRx", output)
        self.assert_acpi_pm_fadt(vm)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

    def test_default_optical_boot(self):
        self.require_accelerator("tcg")
        path = self.media_path("optical.iso")
        make_el_torito_iso(path, app_path("smoke"), platform_id=0xEF)

        vm = self.get_vm()
        vm.set_machine("hp-zx6000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "512M",
            "-display", "none",
            "-net", "none",
            "-drive",
            f"file={path},format=raw,media=cdrom,readonly=on",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 40.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited after optical boot")

    def test_scsi_disk_boot(self):
        self.require_accelerator("tcg")
        path = self.media_path("mpt-disk.img")
        make_fat_disk(path, app_path("smoke"))

        vm = self.get_vm()
        vm.set_machine("hp-zx6000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "512M",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", MPT_SMOKE_CASES, 40.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited after SCSI disk boot")

    def test_graphics_protocols(self):
        self.require_accelerator("tcg")
        path = self.media_path("graphics.iso")
        make_el_torito_iso(path, app_path("graphics"), platform_id=0xEF)

        vm = self.get_vm()
        vm.set_machine("hp-zx6000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "512M",
            "-smp", "1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
            "-drive",
            f"file={path},format=raw,if=ide,bus=0,unit=0,"
            "media=cdrom,readonly=on",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "graphics", GRAPHICS_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after graphics protocol test")

    def test_usb_keyboard_input(self):
        self.require_accelerator("tcg")
        path = self.media_path("input.iso")
        make_el_torito_iso(path, app_path("input"), platform_id=0xEF)

        vm = self.get_vm()
        vm.set_machine("hp-zx6000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "512M",
            "-smp", "1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
            "-drive",
            f"file={path},format=raw,if=ide,bus=0,unit=0,"
            "media=cdrom,readonly=on",
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
                        "QEMU exited after USB keyboard input test")


if __name__ == "__main__":
    QemuSystemTest.main()
