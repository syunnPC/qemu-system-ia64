#!/usr/bin/env python3
"""No-media boot test for the IA-64 zx6000 EFI test machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import re
import struct

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.efi_build import build_root


ZX6000_EFI_TEST_MACHINE = "x-ia64-zx6000-efi-test"
ACPI_RECLAIM_BASE = 0x00800000
ACPI_RECLAIM_TABLE_BASE = 0x00802000
ACPI_RECLAIM_END = 0x00820000
ACPI_HEADER_SIZE = 36
ACPI_DSDT_AML_CAPACITY = 8192
ZX6000_EFI_TEST_FIRMWARE_OUTPUT = (
    b"Console Out:          Serial 16550",
    b"Graphics Output:      not present",
    b"Block I/O Protocol:   not published (no boot controller)",
    b"LocateHandle:         enabled (HP PCI root handles)",
    b"SetVirtualAddressMap/ConvertPointer: enabled",
    b"NVRAM Variables:      enabled",
    b"EFI Time Services:    enabled",
    b"ResetSystem:          unavailable",
    b"SAL System Table:     published",
    b"ACPI MADT (SAPIC):    published",
    b"ACPI MCFG (PCIe):     suppressed",
    b"ACPI HCDP/PCDP:       published",
    b"ACPI SSDT (CPU/UART/PS2): published",
    b"PCI Root Bridge I/O:  published",
    b"PCI Host Bridge:       published",
    b"BOOT path:            no boot controller",
    b"Firmware ready.",
)


class Ia64Zx6000EfiTest(QemuSystemTest):
    """Boot firmware and validate the zx6000 platform tables."""

    @staticmethod
    def _read_physical(vm, address, size):
        data = bytearray()

        while len(data) < size:
            chunk_size = min(size - len(data), 256)
            chunk_address = address + len(data)
            output = vm.cmd(
                "human-monitor-command",
                command_line=f"xp /{chunk_size}bx 0x{chunk_address:x}",
            )
            chunk = bytearray()
            for line in output.splitlines():
                _, separator, values = line.partition(":")
                if separator:
                    chunk.extend(
                        int(value, 16)
                        for value in re.findall(
                            r"0x([0-9a-fA-F]{2})\b", values
                        )
                    )
            if len(chunk) != chunk_size:
                raise AssertionError(
                    f"physical read at 0x{chunk_address:x} returned "
                    f"{len(chunk)} of {chunk_size} bytes"
                )
            data.extend(chunk)
        return bytes(data)

    @classmethod
    def _read_sdt(cls, vm, address):
        header = cls._read_physical(vm, address, ACPI_HEADER_SIZE)
        length = struct.unpack_from("<I", header, 4)[0]
        maximum_length = ACPI_HEADER_SIZE + ACPI_DSDT_AML_CAPACITY

        if length < ACPI_HEADER_SIZE or length > maximum_length:
            raise AssertionError(
                f"invalid ACPI table length {length} at 0x{address:x}"
            )
        return cls._read_physical(vm, address, length)

    def _assert_acpi_tables(self, vm):
        rsdp = self._read_physical(vm, ACPI_RECLAIM_TABLE_BASE, 36)

        self.assertEqual(rsdp[:8], b"RSD PTR ")
        self.assertEqual(rsdp[15], 2)
        self.assertEqual(struct.unpack_from("<I", rsdp, 20)[0], len(rsdp))
        self.assertEqual(sum(rsdp[:20]) & 0xff, 0)
        self.assertEqual(sum(rsdp) & 0xff, 0)

        xsdt_address = struct.unpack_from("<Q", rsdp, 24)[0]
        self.assertGreaterEqual(xsdt_address, ACPI_RECLAIM_BASE)
        self.assertLess(xsdt_address, ACPI_RECLAIM_END)
        xsdt = self._read_sdt(vm, xsdt_address)
        self.assertEqual(xsdt[:4], b"XSDT")
        self.assertEqual(sum(xsdt) & 0xff, 0)
        self.assertEqual((len(xsdt) - ACPI_HEADER_SIZE) % 8, 0)

        tables = {}
        for offset in range(ACPI_HEADER_SIZE, len(xsdt), 8):
            address = struct.unpack_from("<Q", xsdt, offset)[0]

            self.assertGreaterEqual(address, ACPI_RECLAIM_BASE)
            self.assertLess(address, ACPI_RECLAIM_END)
            table = self._read_sdt(vm, address)
            self.assertEqual(sum(table) & 0xff, 0)
            tables[table[:4]] = table

        self.assertTrue({b"FACP", b"APIC", b"SRAT", b"SLIT", b"HCDP",
                         b"SSDT"}
                        <= tables.keys())
        self.assertNotIn(b"MCFG", tables)
        ssdt_aml = tables[b"SSDT"][ACPI_HEADER_SIZE:]
        for value in (
            b"\x08C0EN\x0a\x0f",
            b"\x08C1EN\x0a\x00",
            b"\x08P2EN\x0a\x00",
            b"CPU0",
            b"CPU1",
        ):
            self.assertIn(value, ssdt_aml)
        fadt = tables[b"FACP"]
        self.assertEqual(struct.unpack_from("<H", fadt, 46)[0], 0)
        self.assertEqual(struct.unpack_from("<H", fadt, 109)[0], 0)
        self.assertNotEqual(struct.unpack_from("<I", fadt, 112)[0] & (1 << 5),
                            0)
        self.assertEqual(fadt[148:160], bytes(12))
        self.assertEqual(fadt[172:184], bytes(12))
        self.assertEqual(fadt[208:220], bytes(12))
        dsdt_address = struct.unpack_from("<I", fadt, 40)[0]
        self.assertGreaterEqual(dsdt_address, ACPI_RECLAIM_BASE)
        self.assertLess(dsdt_address, ACPI_RECLAIM_END)
        dsdt = self._read_sdt(vm, dsdt_address)
        self.assertEqual(dsdt[:4], b"DSDT")
        self.assertEqual(sum(dsdt) & 0xff, 0)

        aml = dsdt[ACPI_HEADER_SIZE:]
        for name in (b"_SB_", b"SBA0", b"PCI0", b"PCI1", b"_HID",
                     b"_CID", b"_UID", b"_SEG", b"_BBN", b"_CCA",
                     b"_CRS", b"_PRT"):
            self.assertIn(name, aml)
        self.assertEqual(aml.count(b"_PRT"), 2)
        self.assertEqual(aml.count(b"\x0c\x22\xf0\x00\x01"), 1)
        self.assertEqual(aml.count(b"\x0c\x22\xf0\x00\x02"), 2)
        self.assertEqual(aml.count(b"\x0c\x41\xd0\x0a\x03"), 2)

        root0_bus = bytes.fromhex(
            "88 0d 00 02 0c 00 00 00 20 00 2f 00 00 00 10 00"
        )
        root1_bus = bytes.fromhex(
            "88 0d 00 02 0c 00 00 00 40 00 4f 00 00 00 10 00"
        )
        root0_mmio = struct.pack(
            "<BHBBBQQQQQ", 0x8a, 43, 0, 0x0c, 1, 0, 0,
            0x00ffffff, 0x90000000, 0x01000000
        )
        root1_mmio = struct.pack(
            "<BHBBBQQQQQ", 0x8a, 43, 0, 0x0c, 1, 0, 0,
            0x00ffffff, 0xa0000000, 0x01000000
        )
        for descriptor in (root0_bus, root1_bus, root0_mmio, root1_mmio):
            self.assertEqual(aml.count(descriptor), 1)

    def test_no_media_firmware_ready(self):
        firmware = (build_root() / "roms" / "ia64-firmware" /
                    "ia64-firmware.bin")

        self.assertTrue(firmware.is_file(),
                        f"IA-64 firmware was not built: {firmware}")
        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine(ZX6000_EFI_TEST_MACHINE)
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-nodefaults",
            "-smp", "1",
            "-m", "512M",
            "-bios", str(firmware),
        )
        vm.launch()

        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm)
        for expected in ZX6000_EFI_TEST_FIRMWARE_OUTPUT:
            with self.subTest(output=expected.decode("ascii")):
                self.assertIn(expected, output)
        self._assert_acpi_tables(vm)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")


if __name__ == "__main__":
    QemuSystemTest.main()
