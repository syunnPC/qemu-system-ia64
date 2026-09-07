#!/usr/bin/env python3
"""IA-64 firmware boot manager and interactive EFI shell tests."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import struct
import uuid

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_fat_disk


class Ia64BootShell(Ia64FirmwareTest):
    def _partition_boot_fixture(self, signature_matches):
        disk = Path(self.scratch_file("partition-boot.img"))
        fallback = Path(self.scratch_file("invalid.efi"))
        fallback.write_bytes(b"invalid image")
        make_fat_disk(disk, fallback, layout="gpt",
                      extra_boot_files=((b"LOADER  EFI", app_path("smoke")),))
        data = disk.read_bytes()
        signature = bytearray(data[1024 + 16:1024 + 32])
        if not signature_matches:
            signature[0] ^= 0xff
        start, last = struct.unpack_from("<QQ", data, 1024 + 32)
        hardware = struct.pack("<BBHIII", 2, 2, 64,
                               0x0a0341d0, 0x1234, 0) + bytes(48)
        partition = struct.pack("<BBHIQQ16sBB6x", 4, 1, 48, 1,
                                start, last - start + 1, signature, 2, 2)
        name = "\\EFI\\BOOT\\LOADER.EFI\0".encode("utf-16le")
        path = hardware + partition + struct.pack("<BBH", 4, 4, len(name) + 4)
        path += name + bytes((0x7f, 0xff, 4, 0))
        description = "Stored disk entry\0".encode("utf-16le")
        option = struct.pack("<IH", 1, len(path)) + description + path
        variables = (("Boot0001", option), ("BootOrder", b"\x01\x00"))
        nvram = self.make_nvram("partition-boot.nvram")
        store = bytearray(nvram.read_bytes())
        struct.pack_into("<8sII", store, 0, b"IVARSTOR", 1, len(variables))
        guid = uuid.UUID("8be4df61-93ca-11d2-aa0d-00e098032b8c").bytes_le
        for index, (key, value) in enumerate(variables):
            offset = 16 + index * 1192
            encoded = (key + "\0").encode("utf-16le")
            store[offset:offset + len(encoded)] = encoded
            struct.pack_into("<Q", store, offset + 128, len(encoded))
            store[offset + 136:offset + 152] = guid
            store[offset + 152:offset + 152 + len(value)] = value
            struct.pack_into("<QIBB", store, offset + 1176,
                             len(value), 7, 1, 0)
        nvram.write_bytes(store)
        return disk, nvram

    def test_boot_option_partition_recovery(self):
        disk, nvram = self._partition_boot_fixture(True)
        vm = self.launch_ia64(
            media=disk,
            machine_options=f"firmware-console=serial,nvram={nvram}")
        wait_for_console_pattern(
            self, "IA64TEST suite=smoke status=DONE",
            failure_message="Disk boot failed", vm=vm)

    def test_boot_option_partition_signature_mismatch(self):
        disk, nvram = self._partition_boot_fixture(False)
        vm = self.launch_ia64(
            media=disk,
            machine_options=f"firmware-console=serial,nvram={nvram}")
        wait_for_console_pattern(
            self, "Disk boot failed",
            failure_message="IA64TEST suite=smoke", vm=vm)

    @staticmethod
    def _send_key(vm, qcode):
        vm.cmd("send-key", keys=[{"type": "qcode", "data": qcode}],
               hold_time=50)

    def _open_shell(self, vm, key):
        wait_for_console_pattern(self, "Press F2, F12, or Delete", vm=vm)
        self._send_key(vm, key)
        wait_for_console_pattern(self, "IA-64 EFI shell", vm=vm)

    def _command(self, vm, command, expected):
        vm.console_socket.sendall((command + "\r").encode("ascii"))
        return wait_for_console_pattern(self, expected, vm=vm)

    def test_shell_commands_and_persistence(self):
        disk = Path(self.scratch_file("shell.img"))
        nvram = self.make_nvram("shell.nvram")
        make_fat_disk(disk, app_path("smoke"))

        vm = self.launch_ia64(
            name="shell-first", media=disk,
            machine_options=f"firmware-console=serial,nvram={nvram}")
        self._open_shell(vm, "f2")
        self._command(vm, "info", "NVRAM backing:  persistent")
        self._command(vm, "map", "fs0:")
        self._command(vm, r"ls fs0:\EFI\BOOT", "BOOTIA64.EFI")
        self._command(vm, "date 2024-02-29", "2024-02-29")
        self._command(vm, "time 12:34:56", "12:34:56")
        self._command(vm, "bootorder Boot0000",
                      "BootOrder saved to persistent NVRAM")
        self._command(vm, "bootnext Boot0000",
                      "BootNext saved to persistent NVRAM")
        self._command(vm, r"cd fs0:\EFI\BOOT", r"fs0:\EFI\BOOT>")
        self._command(vm, "pwd", r"fs0:\EFI\BOOT")
        self._command(vm, "run BOOTIA64.EFI",
                      "IA64TEST suite=smoke status=DONE")
        wait_for_console_pattern(self, r"fs0:\EFI\BOOT>", vm=vm)
        vm.shutdown()

        contents = nvram.read_bytes()
        self.assertIn("BootOrder".encode("utf-16le") + b"\0\0", contents)
        self.assertIn(b"IRT64OFT", contents)

        vm = self.launch_ia64(
            name="shell-second", media=disk,
            machine_options=f"firmware-console=serial,nvram={nvram}")
        self._open_shell(vm, "f12")
        self._command(vm, "date", "2024-02-29")
        self._command(vm, "time", "12:34:")
        self._command(vm, "bootorder", "BootOrder: Boot0000")
        self._command(vm, "bootnext", "BootNext: Boot0000")
        vm.console_socket.sendall(b"exit\r")
        wait_for_console_pattern(
            self, "IA64TEST suite=smoke status=DONE", vm=vm)
        vm.shutdown()

        vm = self.launch_ia64(
            name="shell-third", media=disk,
            machine_options=f"firmware-console=serial,nvram={nvram}")
        self._open_shell(vm, "f2")
        self._command(vm, "bootnext", "BootNext is not set")
        vm.shutdown()

    def test_delete_hotkey_and_device_boot(self):
        disk = Path(self.scratch_file("device-boot.img"))
        make_fat_disk(disk, app_path("smoke"))
        vm = self.launch_ia64(
            name="delete-hotkey", media=disk,
            machine_options=(
                "i8042=off,firmware-console=serial,nvram=none"))
        self._open_shell(vm, "delete")
        self._command(vm, "boot fs0:",
                      "IA64TEST suite=smoke status=DONE")


if __name__ == "__main__":
    QemuSystemTest.main()
