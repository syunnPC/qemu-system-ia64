#!/usr/bin/env python3
"""Firmware boot test for the HP Integrity rx2660 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import re
import struct
from pathlib import Path

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.acpi import assert_pci_windows, io_window, memory_window
from ia64.efi_build import app_path, build_root
from ia64.media import make_fat_disk
from ia64.protocol import wait_for_suite


ACPI_RECLAIM_BASE = 0x00800000
ACPI_RECLAIM_TABLE_BASE = 0x00802000
ACPI_RECLAIM_END = 0x00820000
ACPI_HEADER_SIZE = 36
SBA0_PATH = b"\x5c\x2e_SB_SBA0"
PCI0_PATH = b"\x5c\x2e_SB_PCI0"
SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "controller-device-path", "console-output",
    "console-variables",
    "low-memory-allocation",
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
        self.assertEqual(rsdp[9:15], b"HP    ")
        self.assertEqual(sum(rsdp[:20]) & 0xff, 0)
        self.assertEqual(sum(rsdp) & 0xff, 0)

        xsdt_address = struct.unpack_from("<Q", rsdp, 24)[0]
        self.assertGreaterEqual(xsdt_address, ACPI_RECLAIM_BASE)
        self.assertLess(xsdt_address, ACPI_RECLAIM_END)
        xsdt = self.read_sdt(vm, xsdt_address)
        self.assertEqual(xsdt[:4], b"XSDT")
        self.assertEqual(xsdt[10:16], b"HP    ")
        self.assertEqual(sum(xsdt) & 0xff, 0)

        tables = {}
        for offset in range(ACPI_HEADER_SIZE, len(xsdt), 8):
            address = struct.unpack_from("<Q", xsdt, offset)[0]

            self.assertGreaterEqual(address, ACPI_RECLAIM_BASE)
            self.assertLess(address, ACPI_RECLAIM_END)
            table = self.read_sdt(vm, address)
            self.assertEqual(table[10:16], b"HP    ")
            self.assertEqual(sum(table) & 0xff, 0)
            tables[table[:4]] = table
        return tables

    def assert_rx2660_acpi(self, vm, *, graphics=True):
        tables = self.acpi_tables(vm)

        self.assertTrue({b"FACP", b"APIC", b"SRAT", b"SLIT", b"HCDP",
                         b"SSDT", b"SPCR"} <= tables.keys())
        self.assertNotIn(b"MCFG", tables)

        # SPCR revision 1 field offsets.
        spcr = tables[b"SPCR"]
        self.assertEqual(len(spcr), 80)
        self.assertEqual(spcr[8], 1)
        self.assertEqual(sum(spcr) & 0xff, 0)
        self.assertEqual(spcr[36:44], bytes((0, 0, 0, 0, 0, 8, 0, 0)))
        self.assertEqual(struct.unpack_from("<Q", spcr, 44)[0], 0x88033000)
        self.assertEqual(spcr[52:54], bytes((4, 0)))  # I/O SAPIC
        self.assertEqual(struct.unpack_from("<I", spcr, 54)[0], 16)
        self.assertEqual(spcr[58:64], bytes((7, 0, 1, 0, 0, 0)))
        self.assertEqual(struct.unpack_from("<HH", spcr, 64),
                         (0x1048, 0x103c))
        self.assertEqual(spcr[68:71], bytes((0, 1, 2)))
        self.assertEqual(struct.unpack_from("<I", spcr, 71)[0], 1)
        self.assertEqual(spcr[75:80], bytes(5))

        rsdp = self.read_physical(vm, ACPI_RECLAIM_TABLE_BASE, 36)
        rsdt = self.read_sdt(vm, struct.unpack_from("<I", rsdp, 16)[0])
        self.assertEqual(sum(rsdt) & 0xff, 0)
        rsdt_tables = {
            self.read_sdt(vm, struct.unpack_from("<I", rsdt, offset)[0])[:4]
            for offset in range(ACPI_HEADER_SIZE, len(rsdt), 4)
        }
        self.assertEqual(rsdt_tables, tables.keys())

        hcdp = tables[b"HCDP"]
        self.assertEqual(hcdp[44:48], bytes((0, 0, 1, 2)))
        self.assertEqual(struct.unpack_from("<Q", hcdp, 60)[0], 0x88033000)
        self.assertEqual(struct.unpack_from("<HHII", hcdp, 68),
                         (0x1048, 0x103c, 16, 1843200))
        self.assertEqual(hcdp[81], 0xc2 if graphics else 0xc6)
        self.assertEqual(struct.unpack_from("<H", hcdp, 82)[0],
                         1 if graphics else 0)

        uart_count = struct.unpack_from("<I", hcdp, 36)[0]
        device_offset = ACPI_HEADER_SIZE + 4 + 48 * uart_count
        if graphics:
            device_type, flags, device_length, efi_index = struct.unpack_from(
                "<BBHH", hcdp, device_offset)
            self.assertEqual((device_type, flags, efi_index), (0x0a, 1, 0))
            self.assertEqual(device_offset + device_length, len(hcdp))
            interface_offset = device_offset + 6
            interface_length = struct.unpack_from(
                "<H", hcdp, interface_offset + 2)[0]
            self.assertEqual(hcdp[interface_offset], 1)
            self.assertEqual(interface_length, 34)
            vga_offset = interface_offset + interface_length
            self.assertEqual(hcdp[vga_offset], 2)

            resource = struct.Struct("<BH5B6Q")
            resource_offset = vga_offset + 1
            for kind, type_flags, minimum, maximum, attributes in (
                (0, 1, 0xa0000, 0xbffff, 1),
                (1, 3, 0x3b0, 0x3df, 0),
            ):
                self.assertEqual(
                    resource.unpack_from(hcdp, resource_offset),
                    (0x8b, 53, kind, 0x0d, type_flags, 1, 0,
                     0, minimum, maximum, 0, maximum - minimum + 1, attributes),
                )
                resource_offset += resource.size
            self.assertEqual(resource_offset, device_offset + device_length)
        else:
            self.assertEqual(device_offset, len(hcdp))

        fadt = tables[b"FACP"]
        dsdt = self.read_sdt(vm, struct.unpack_from("<Q", fadt, 140)[0])
        self.assertEqual(dsdt[:4], b"DSDT")
        self.assertEqual(dsdt[10:16], b"HP    ")
        self.assertEqual(sum(dsdt) & 0xff, 0)
        aml = dsdt[ACPI_HEADER_SIZE:]
        self.assertIn(b"SBA0", aml)
        for root in (b"PCI0", b"PCI1", b"PCI2", b"PCI3", b"PCI4"):
            self.assertIn(root, aml)
        self.assertEqual(aml.count(b"_PRT"), 2)

        windows = []
        for index, (ports, mmio32, mmio64) in enumerate((
            (0x0000, 0x80000000, 0x80004000000),
            (0x4000, 0xA0000000, 0x80204000000),
            (0x2000, 0xB0000000, 0x80304000000),
            (0x6000, 0xE0000000, 0x80604000000),
            (0x8000, 0xF0000000, 0x80704000000),
        )):
            root = [io_window(ports, 0x2000)]
            if graphics and index == 0:
                root.append(memory_window(0xA0000, 0x60000))
            root += [memory_window(mmio32, 0xE000000 if index == 4
                                   else 0x10000000),
                     memory_window(mmio64, 0xFC000000, width=8)]
            windows.append(root)
        assert_pci_windows(self, aml, windows)

        ssdt_aml = tables[b"SSDT"][ACPI_HEADER_SIZE:]
        self.assertEqual(ssdt_aml.count(SBA0_PATH), 1)
        self.assertNotIn(PCI0_PATH, ssdt_aml)

    def run_firmware(self, layout="whole", *, fat32=False, nvram=False,
                     graphics=True):
        firmware = (build_root() / "roms" / "ia64-firmware" /
                    "ia64-firmware.bin")
        disk = Path(self.scratch_file("rx2660-sas.img"))

        self.assertTrue(firmware.is_file(),
                        f"IA-64 firmware was not built: {firmware}")
        make_fat_disk(disk, app_path("smoke"), layout=layout, fat32=fat32)

        self.require_accelerator("tcg")
        vm = self.get_vm()
        nvram_path = "none"
        if nvram:
            nvram_path = self.scratch_file("rx2660.nvram")
            Path(nvram_path).write_bytes(bytes(512 * 1024))
        vm.set_machine(f"hp-rx2660,nvram={nvram_path}")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "1,sockets=1,cores=1,threads=1",
            "-display", "none",
            "-vga", "ati" if graphics else "none",
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
            ("Disk Partitions:       " +
             ("0000000000000000" if layout == "whole" else
              "0000000000000001") + " child handle(s)").encode("ascii"),
            b"Block I/O Protocol:   installed (SCSI disk, LSI SAS1068 "
            b"Fusion-MPT polling)",
            b"BOOT path:            rx2660 LSI SAS1068 disk, FAT resolver",
            b"ACPI MCFG (PCIe):     suppressed",
            b"PCI Root Bridge I/O:  published",
        ):
            with self.subTest(output=expected.decode("ascii")):
                self.assertIn(expected, output)
        self.assert_rx2660_acpi(vm, graphics=graphics)
        self.assertEqual(
            struct.unpack("<QQ", self.read_physical(vm, 0xFED01300, 16)),
            (0x40000000, 0xFFFFFFFFC0000000),
        )
        if nvram:
            wait_for_console_pattern(
                self, "Boot Manager:        trying Boot0000\r\n", vm=vm)
        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 30.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

    def test_firmware_ready(self):
        self.run_firmware()

    def test_serial_console(self):
        self.run_firmware(graphics=False)

    def test_gpt_disk_boot(self):
        self.run_firmware("gpt")

    def test_gpt_fat32_disk_boot(self):
        self.run_firmware("gpt", fat32=True)

    def test_boot_order_disk_boot(self):
        self.run_firmware("gpt", nvram=True)

if __name__ == "__main__":
    QemuSystemTest.main()
