#!/usr/bin/env python3
"""Firmware boot test for the HP Integrity rx2660 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import re
import struct
from pathlib import Path

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.efi_build import app_path, build_root
from ia64.media import make_fat_disk
from ia64.protocol import wait_for_suite


ACPI_RECLAIM_BASE = 0x00800000
ACPI_RECLAIM_TABLE_BASE = 0x00802000
ACPI_RECLAIM_END = 0x00820000
ACPI_HEADER_SIZE = 36
RX2660_LEGACY_IO_BASE = 0x00000FFFFC000000
SBA0_PATH = b"\x5c\x2e_SB_SBA0"
PCI0_PATH = b"\x5c\x2e_SB_PCI0"
SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "controller-device-path", "console-output",
}


class HPRx2660Boot(QemuSystemTest):
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

    def acpi_tables(self, vm):
        rsdp = self.read_physical(vm, ACPI_RECLAIM_TABLE_BASE, 36)

        self.assertEqual(rsdp[:8], b"RSD PTR ")
        self.assertEqual(sum(rsdp[:20]) & 0xff, 0)
        self.assertEqual(sum(rsdp) & 0xff, 0)

        xsdt_address = struct.unpack_from("<Q", rsdp, 24)[0]
        self.assertGreaterEqual(xsdt_address, ACPI_RECLAIM_BASE)
        self.assertLess(xsdt_address, ACPI_RECLAIM_END)
        xsdt = self.read_sdt(vm, xsdt_address)
        self.assertEqual(xsdt[:4], b"XSDT")
        self.assertEqual(sum(xsdt) & 0xff, 0)

        tables = {}
        for offset in range(ACPI_HEADER_SIZE, len(xsdt), 8):
            address = struct.unpack_from("<Q", xsdt, offset)[0]

            self.assertGreaterEqual(address, ACPI_RECLAIM_BASE)
            self.assertLess(address, ACPI_RECLAIM_END)
            table = self.read_sdt(vm, address)
            self.assertEqual(sum(table) & 0xff, 0)
            tables[table[:4]] = table
        return tables

    def assert_rx2660_acpi(self, vm):
        tables = self.acpi_tables(vm)

        self.assertTrue({b"FACP", b"APIC", b"SRAT", b"SLIT", b"HCDP",
                         b"SSDT"} <= tables.keys())
        self.assertNotIn(b"MCFG", tables)

        fadt = tables[b"FACP"]
        dsdt = self.read_sdt(vm, struct.unpack_from("<Q", fadt, 140)[0])
        self.assertEqual(dsdt[:4], b"DSDT")
        self.assertEqual(sum(dsdt) & 0xff, 0)
        aml = dsdt[ACPI_HEADER_SIZE:]
        self.assertIn(b"SBA0", aml)
        for root in (b"PCI0", b"PCI1", b"PCI2", b"PCI3", b"PCI4"):
            self.assertIn(root, aml)
        self.assertEqual(aml.count(b"_PRT"), 5)

        def qword_io(minimum, maximum, length):
            return (
                b"\x8a\x2b\x00\x01\x0c\x33" +
                struct.pack("<QQQQQ", 0, minimum, maximum,
                            RX2660_LEGACY_IO_BASE, length)
            )

        def dword_memory(minimum, maximum, translation, length):
            return (
                b"\x87\x17\x00\x00\x0c\x01" +
                struct.pack("<IIIII", 0, minimum, maximum,
                            translation, length)
            )

        self.assertEqual(aml.count(qword_io(0x0000, 0x1FFF, 0x2000)), 1)
        for minimum in range(0x2000, 0xA000, 0x2000):
            self.assertEqual(
                aml.count(qword_io(minimum, minimum + 0x1FFF, 0x2000)), 1
            )
        self.assertEqual(
            aml.count(dword_memory(0x000A0000, 0x000FFFFF,
                                   0, 0x00060000)),
            1,
        )

        ssdt_aml = tables[b"SSDT"][ACPI_HEADER_SIZE:]
        self.assertEqual(ssdt_aml.count(SBA0_PATH), 2)
        self.assertNotIn(PCI0_PATH, ssdt_aml)

    def test_firmware_ready(self):
        firmware = (build_root() / "roms" / "ia64-firmware" /
                    "ia64-firmware.bin")
        disk = Path(self.scratch_file("rx2660-sas.img"))

        self.assertTrue(firmware.is_file(),
                        f"IA-64 firmware was not built: {firmware}")
        make_fat_disk(disk, app_path("smoke"))

        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine("hp-rx2660,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "1,sockets=1,cores=1,threads=1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
            "-bios", str(firmware),
            "-drive", f"file={disk},format=raw,if=scsi,index=0",
        )
        vm.launch()

        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm,
        )
        for expected in (
            b"Memory Map:           high RAM ranges=0000000000000001",
            b"SCSI controller:      LSI SAS1068 MPT",
            b"SCSI device:          target 0000000000000000 disk media",
            b"Disk Partitions:       0000000000000000 child handle(s)",
            b"Block I/O Protocol:   installed (SCSI disk, LSI SAS1068 "
            b"Fusion-MPT polling)",
            b"BOOT path:            rx2660 LSI SAS1068 disk, FAT resolver",
            b"ACPI MCFG (PCIe):     suppressed",
            b"PCI Root Bridge I/O:  published",
        ):
            with self.subTest(output=expected.decode("ascii")):
                self.assertIn(expected, output)
        self.assert_rx2660_acpi(vm)
        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 30.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

if __name__ == "__main__":
    QemuSystemTest.main()
