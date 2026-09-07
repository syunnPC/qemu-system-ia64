#!/usr/bin/env python3
"""IA-64 firmware storage, partition, and optical boot tests."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import struct

from qemu_test import QemuSystemTest

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import (crc32, file_sha256, make_el_torito_iso, make_fat_disk,
                        make_udf_bridge_iso)


COMMON_CASES = {
    "loaded-protocols", "block-media", "media-layout", "bulk-read",
    "block-error-contracts", "disk-read", "simple-filesystem",
    "file-protocol-contracts", "unicode-collation",
}


class Ia64Storage(Ia64FirmwareTest):
    def run_scenario(self, name, media, *, optical=False,
                     machine_options="", drive_args=None, ide_mode=None,
                     required_cases=()):
        media = Path(media)
        before_data = media.read_bytes()
        before = file_sha256(media)
        extra_args = ()
        if ide_mode is not None:
            extra_args = ("-trace", "enable=bmdma_cmd_writeb")
        vm = self.launch_ia64(
            name=name, media=media, optical=optical,
            machine_options=(
                "firmware-console=serial,nvram=none" +
                ("," + machine_options if machine_options else "")),
            drive_args=drive_args, extra_args=extra_args)
        required = set(COMMON_CASES)
        required.add("read-only-media" if optical else "write-read-restore")
        required.update(required_cases)
        try:
            self.wait_ia64_suite(vm, "storage", required, timeout=40.0)
        finally:
            try:
                vm.shutdown()
            finally:
                after = file_sha256(media)
                if after != before:
                    # Preserve the scratch fixture even when a failing guest
                    # transfer only completed part of its write.
                    media.write_bytes(before_data)
                self.assertEqual(
                    before, after,
                    f"{name}: media was not restored exactly")
        if ide_mode is not None:
            trace = vm.get_log() or ""
            if ide_mode == "dma":
                self.assertIn("bmdma_cmd_writeb val: 0x00000009", trace)
                self.assertIn("bmdma_cmd_writeb val: 0x00000001", trace)
            else:
                self.assertNotIn("bmdma_cmd_writeb", trace)

    def run_scsi_layout(self, layout, *, fat32=False):
        app = app_path("storage")
        suffix = "-fat32" if fat32 else ""
        media = Path(self.scratch_file(f"scsi-{layout}{suffix}.img"))
        extra_files = (() if layout == "whole" or fat32 else
                       ((b"START   EFI", app_path("start-image-child")),))
        make_fat_disk(media, app, layout=layout, fat32=fat32,
                      extra_boot_files=extra_files)
        required = []
        if layout != "whole":
            required.extend(("logical-partition-handle",
                             "partition-driver-contracts"))
            if not fat32:
                required.append("short-form-hard-drive-path")
        if fat32:
            required.append("fat32-filesystem")
        self.run_scenario(f"scsi-{layout}{suffix}", media,
                          required_cases=required)

    def run_ide(self, mode):
        app = app_path("storage")
        media = Path(self.scratch_file(f"ide-{mode}.img"))
        make_fat_disk(media, app)
        drive_args = (
            "-drive", f"file={media},format=raw,if=none,id=testdisk",
            "-device", "cmd646-ide,id=ide,secondary=1,addr=0",
            "-device", "ide-hd,drive=testdisk,bus=ide.0,unit=0",
        )
        self.run_scenario(
            f"ide-{mode}", media,
            machine_options=("firmware-ide-dma=off"
                             if mode == "pio" else ""),
            drive_args=drive_args, ide_mode=mode)

    def run_ahci(self):
        app = app_path("storage")
        media = Path(self.scratch_file("ahci.img"))
        make_fat_disk(
            media, app, layout="gpt",
            extra_boot_files=((b"START   EFI",
                               app_path("start-image-child")),))
        drive_args = (
            "-drive", f"file={media},format=raw,if=ide,index=0",
        )
        self.run_scenario(
            "ahci", media, drive_args=drive_args,
            required_cases=("logical-partition-handle",
                            "short-form-hard-drive-path",
                            "partition-driver-contracts"))

    def run_empty_cd(self, transport):
        app = app_path("storage")
        media = Path(self.scratch_file(f"{transport}-empty-cd.img"))
        make_fat_disk(media, app)
        drive_args = [
            "-drive", f"file={media},format=raw,if=scsi,index=0",
        ]
        if transport == "scsi":
            drive_args.extend((
                "-device", "scsi-cd,bus=scsi.0,scsi-id=1",
            ))
        elif transport == "ahci":
            drive_args.extend((
                "-device", "ide-cd,bus=ide.0,unit=0",
            ))
        elif transport == "ide":
            drive_args.extend((
                "-device", "cmd646-ide,id=ide,secondary=1,addr=0",
                "-device", "ide-cd,bus=ide.0,unit=0",
            ))
        else:
            raise ValueError(f"unknown empty-media transport: {transport}")
        self.run_scenario(
            f"{transport}-empty-cd", media, drive_args=tuple(drive_args),
            required_cases=("empty-removable-media",))

    def run_optical(self, name, builder, *, udf=False):
        media = Path(self.scratch_file(name + ".iso"))
        builder(media)
        self.run_scenario(
            name, media, optical=True,
            required_cases=("udf-filesystem",) if udf else ())

    def test_scsi_whole_disk(self):
        self.run_scsi_layout("whole")

    def test_scsi_gpt_esp(self):
        self.run_scsi_layout("gpt")

    def test_scsi_gpt_fat32(self):
        self.run_scsi_layout("gpt", fat32=True)

    def run_gpt_invalid_entries(self, esp_first):
        name = "gpt-invalid-" + ("after-esp" if esp_first else "before-esp")
        media = Path(self.scratch_file(name + ".img"))
        make_fat_disk(
            media, app_path("storage"), layout="gpt",
            extra_boot_files=((b"START   EFI", app_path("start-image-child")),))
        data = bytearray(media.read_bytes())
        esp = data[1024:1152]
        first_usable, last_usable = struct.unpack_from("<QQ", data, 512 + 40)
        entries = []
        for index, (first, last) in enumerate((
                (0, first_usable - 1), (first_usable + 1, first_usable),
                (last_usable + 1, last_usable + 1))):
            entry = bytearray(esp)
            entry[16] ^= index + 1
            struct.pack_into("<QQ", entry, 32, first, last)
            entries.append(entry)
        missing_signature = bytearray(esp)
        missing_signature[16:32] = bytes(16)
        entries.append(missing_signature)
        entries.insert(0 if esp_first else len(entries), esp)
        table = b"".join(entries).ljust(128 * 128, b"\0")
        for offset in (512, len(data) - 512):
            table_lba = struct.unpack_from("<Q", data, offset + 72)[0]
            data[table_lba * 512:table_lba * 512 + len(table)] = table
            struct.pack_into("<I", data, offset + 88, crc32(table))
            struct.pack_into("<I", data, offset + 16, 0)
            struct.pack_into("<I", data, offset + 16,
                             crc32(data[offset:offset + 92]))
        media.write_bytes(data)
        self.run_scenario(
            name, media,
            required_cases=("logical-partition-handle",
                            "short-form-hard-drive-path",
                            "partition-driver-contracts"))

    def test_scsi_gpt_invalid_entries_before_esp(self):
        self.run_gpt_invalid_entries(False)

    def test_scsi_gpt_invalid_entries_after_esp(self):
        self.run_gpt_invalid_entries(True)

    def test_scsi_mbr_fat(self):
        self.run_scsi_layout("mbr")

    def test_scsi_mbr_fallback(self):
        self.run_scsi_layout("mbr-fallback")

    def test_cmd646_ide_dma(self):
        self.run_ide("dma")

    def test_cmd646_ide_pio(self):
        self.run_ide("pio")

    def test_ahci(self):
        self.run_ahci()

    def test_scsi_empty_cd(self):
        self.run_empty_cd("scsi")

    def test_ahci_empty_cd(self):
        self.run_empty_cd("ahci")

    def test_ide_empty_cd(self):
        self.run_empty_cd("ide")

    def test_el_torito_efi_platform(self):
        self.run_optical(
            "eltorito-efi",
            lambda path: make_el_torito_iso(
                path, app_path("storage"), platform_id=0xEF))

    def test_el_torito_legacy_platform(self):
        self.run_optical(
            "eltorito-legacy",
            lambda path: make_el_torito_iso(
                path, app_path("storage"), platform_id=0))

    def test_udf_bridge_filesystem(self):
        self.run_optical(
            "udf-bridge",
            lambda path: make_udf_bridge_iso(path, app_path("storage")),
            udf=True)


if __name__ == "__main__":
    QemuSystemTest.main()
