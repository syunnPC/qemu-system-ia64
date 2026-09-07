#!/usr/bin/env python3
"""EFI SCSI pass-through paths and commands on MPI controllers."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import QemuSystemTest

from ia64.efi_build import app_path, firmware_path
from ia64.media import file_sha256, make_el_torito_iso, make_fat_disk
from ia64.protocol import wait_for_suite


CASES = {"enumerate", "device-path", "inquiry", "capacity", "autosense",
         "write-read-restore", "reset"}


class Ia64ScsiPassThru(QemuSystemTest):
    def run_controller(self, machine, *, optical=False):
        media = Path(self.scratch_file("boot.img"))
        scratch = Path(self.scratch_file("scratch.img"))
        make_fat_disk(media, app_path("scsi-pass-thru"))
        if optical:
            iso = Path(self.scratch_file("boot.iso"))
            make_el_torito_iso(iso, app_path("scsi-pass-thru"))
            media = iso
        with scratch.open("wb") as stream:
            stream.truncate(16 * 1024 * 1024)
        before = file_sha256(scratch)
        target = 6 if machine == "hp-rx2660" else 8
        vm = self.get_vm()
        vm.set_machine(f"{machine},nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg", "-m", "1G", "-smp", "1",
            "-bios", str(firmware_path()), "-net", "none",
            "-drive", f"file={media},format=raw,if=scsi,index=0" +
            (",media=cdrom,readonly=on" if optical else ""),
            "-drive", f"file={scratch},format=raw,if=none,id=scratch",
            "-device", f"scsi-hd,drive=scratch,bus=scsi.0,scsi-id={target}",
        )
        vm.launch()
        try:
            wait_for_suite(vm.console_socket, "scsi-pass-thru", CASES, 35.0,
                           process_alive=vm.is_running)
        finally:
            vm.shutdown()
        self.assertEqual(before, file_sha256(scratch))

    def test_spi(self):
        self.run_controller("hp-zx6000")

    def test_sas(self):
        self.run_controller("hp-rx2660")

    def test_sas_optical(self):
        self.run_controller("hp-rx2660", optical=True)


if __name__ == "__main__":
    QemuSystemTest.main()
