#!/usr/bin/env python3
"""Firmware boot tests for the HP zx2000 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import re
import struct

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.acpi import (assert_pci_windows, assert_zx2000_uarts,
                       io_window, memory_window)
from ia64.efi_build import app_path
from ia64.media import make_el_torito_iso, make_fat_disk
from ia64.protocol import wait_for_suite


SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "console-output", "console-variables",
    "low-memory-allocation",
}


class HPZx2000Boot(QemuSystemTest):
    @staticmethod
    def read_physical(vm, address, size):
        data = bytearray()
        while len(data) < size:
            count = min(size - len(data), 256)
            output = vm.cmd(
                "human-monitor-command",
                command_line=f"xp /{count}bx 0x{address + len(data):x}",
            )
            chunk = bytearray()
            for line in output.splitlines():
                _, separator, values = line.partition(":")
                if separator:
                    chunk.extend(int(value, 16) for value in re.findall(
                        r"0x([0-9a-fA-F]{2})\b", values))
            if len(chunk) != count:
                raise AssertionError("incomplete physical memory read")
            data.extend(chunk)
        return bytes(data)

    def read_acpi_table(self, vm, address):
        header = self.read_physical(vm, address, 36)
        size = struct.unpack_from("<I", header, 4)[0]
        self.assertGreaterEqual(size, 36)
        self.assertLessEqual(size, 8192)
        table = self.read_physical(vm, address, size)
        self.assertEqual(sum(table) & 0xff, 0)
        self.assertEqual(table[10:16], b"HP    ")
        self.assertEqual(table[16:24], b"zx2000  ")
        return table

    def assert_acpi_identity(self, vm):
        rsdp = self.read_physical(vm, 0x00802000, 36)
        self.assertEqual(rsdp[:8], b"RSD PTR ")
        self.assertEqual(sum(rsdp) & 0xff, 0)
        xsdt = self.read_acpi_table(vm, struct.unpack_from("<Q", rsdp, 24)[0])
        self.assertEqual(xsdt[:4], b"XSDT")
        tables = {}
        for offset in range(36, len(xsdt), 8):
            address = struct.unpack_from("<Q", xsdt, offset)[0]
            table = self.read_acpi_table(vm, address)
            tables[table[:4]] = table
        self.assertNotIn(b"MCFG", tables)
        fadt = tables[b"FACP"]
        self.assertEqual(fadt[45], 3)
        self.assertEqual(struct.unpack_from("<H", fadt, 46)[0], 47)
        dsdt = self.read_acpi_table(vm, struct.unpack_from("<Q", fadt, 140)[0])
        self.assertEqual(dsdt[:4], b"DSDT")
        roots = []
        for index, (ports, base, size) in enumerate((
            (0x0000, 0x80000000, 0x40000000),
            (0x8000, 0xC0000000, 0x10000000),
            (0xA000, 0xD0000000, 0x10000000),
            (0xC000, 0xE0000000, 0x10000000),
        )):
            root = [io_window(ports, 0x2000)]
            if index == 0:
                root.append(memory_window(0xA0000, 0x60000))
            root.append(memory_window(base, size))
            if index == 2:
                root.extend((memory_window(0xFF5E0000, 8),
                             memory_window(0xFF5E2000, 8)))
            roots.append(root)
        assert_pci_windows(self, dsdt[36:], roots)
        assert_zx2000_uarts(self, tables[b"SSDT"][36:])
        hcdp = tables[b"HCDP"]
        self.assertEqual(struct.unpack_from("<I", hcdp, 36)[0], 1)
        self.assertEqual(struct.unpack_from("<Q", hcdp, 60)[0], 0xFF5E0000)
        self.assertEqual(struct.unpack_from("<I", hcdp, 68)[0], 0x0105D041)
        self.assertEqual(struct.unpack_from("<I", hcdp, 44)[0], 0)
        self.assertEqual(struct.unpack_from("<I", hcdp, 72)[0], 45)
        self.assertEqual(hcdp[81] & 0xC3, 0x40,
                         "HCDP UART must use a non-PCI, level, active-high IRQ")

    def launch_machine(self, *args):
        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine("hp-zx2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "512M",
            "-display", "none",
            "-net", "none",
            *args,
        )
        vm.launch()
        return vm

    def assert_smoke(self, vm, unit=0):
        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 40.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited after EFI boot")
        match = re.search(r"EFI boot device path: ([0-9a-f]+)",
                          result.raw_console)
        self.assertIsNotNone(match, "EFI app did not report its boot path")
        path = bytes.fromhex(match[1])
        prefix = (struct.pack("<BBHII", 2, 1, 12, 0x000222F0, 0x500) +
                  struct.pack("<BBHBB", 1, 1, 6, 0, 2) +
                  struct.pack("<BBHBBH", 3, 1, 8, 0, unit, 0))
        self.assertEqual(path[:len(prefix)], prefix)

    def test_firmware_ready(self):
        vm = self.launch_machine("-vga", "ati")
        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm,
        )
        self.assertIn(b"zx2000 CMD649 disk/optical + FAT resolver", output)
        self.assertIn(b"PCI Root Bridge I/O:  published", output)
        self.assertIn(b"ACPI MCFG (PCIe):     suppressed", output)
        self.assertIn(b"Console In:           Serial/USB ready", output)
        self.assertIn(b"Graphics Output:      GOP/UGA VGA BGRx", output)
        self.assertNotIn(b"LSI53C1030", output)
        self.assert_acpi_identity(vm)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

    def test_default_disk_boot(self):
        path = Path(self.scratch_file("disk.img"))
        make_fat_disk(path, app_path("smoke"))
        vm = self.launch_machine("-drive", f"file={path},format=raw")
        self.assert_smoke(vm)

    def test_default_optical_boot(self):
        path = Path(self.scratch_file("optical.iso"))
        make_el_torito_iso(path, app_path("smoke"), platform_id=0xEF)
        vm = self.launch_machine(
            "-drive", f"file={path},format=raw,media=cdrom,readonly=on",
        )
        self.assert_smoke(vm)

    def test_disk_slave_boot_with_optical_master(self):
        disk = Path(self.scratch_file("disk.img"))
        make_fat_disk(disk, app_path("smoke"), layout="gpt")
        optical = Path(self.scratch_file("empty-optical.iso"))
        with optical.open("wb") as stream:
            stream.truncate(1024 * 1024)
        vm = self.launch_machine(
            "-drive", f"file={optical},format=raw,media=cdrom,readonly=on",
            "-drive", f"file={disk},format=raw",
            "-vga", "none",
        )
        self.assert_smoke(vm, unit=1)

    def optical_boot_with_disk(self, optical_unit):
        disk = Path(self.scratch_file("empty-disk.img"))
        with disk.open("wb") as stream:
            stream.truncate(8 * 1024 * 1024)
        optical = Path(self.scratch_file("optical.iso"))
        make_el_torito_iso(optical, app_path("smoke"), platform_id=0xEF)
        vm = self.launch_machine(
            "-drive", f"file={disk},format=raw,index={1 - optical_unit}",
            "-drive", f"file={optical},format=raw,media=cdrom,readonly=on,"
                      f"index={optical_unit}",
        )
        self.assert_smoke(vm, unit=optical_unit)

    def test_disk_master_optical_slave_boot(self):
        self.optical_boot_with_disk(1)

    def test_optical_master_disk_slave_boot(self):
        self.optical_boot_with_disk(0)


if __name__ == "__main__":
    QemuSystemTest.main()
