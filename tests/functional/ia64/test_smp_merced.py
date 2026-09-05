#!/usr/bin/env python3
"""Minimal Merced AP boot rendezvous without translation-cache pressure."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import QemuSystemTest

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_fat_disk


SMP_MERCED_BASE_CASES = {
    "sal-ap-wake", "merced-rendezvous", "merced-rendezvous-return",
}
SMP_MERCED_ZERO_CASES = SMP_MERCED_BASE_CASES | {
    "zero-alat-check-reload",
}
SMP_MERCED_FULL_CASES = SMP_MERCED_BASE_CASES | {
    "full-alat-smp-store-ordering",
}


class Ia64SmpMerced(Ia64FirmwareTest):
    def run_merced_ap_rendezvous(self, name: str, smp: int | str,
                                 memory: str = "512M",
                                 alat: str = "zero"):
        disk = Path(self.scratch_file(f"smp-merced-{name}.img"))
        nvram = self.make_nvram(f"smp-merced-{name}.nvram")
        make_fat_disk(disk, app_path("smp-merced"))
        vm = self.launch_ia64(
            name=name, media=disk, smp=smp, memory=memory,
            machine_options=(f"firmware-console=serial,nvram={nvram},"
                             f"alat={alat}"),
            extra_args=("-cpu", "merced",
                        "-accel", "tcg,thread=multi"))
        cases = (SMP_MERCED_FULL_CASES if alat == "full"
                 else SMP_MERCED_ZERO_CASES)
        result = self.wait_ia64_suite(
            vm, "smp-merced", cases, timeout=180.0)
        self.assertSetEqual(set(result.cases), cases)

    def test_2_socket_full_alat(self):
        self.run_merced_ap_rendezvous(
            "2s-full-alat", 2, alat="full")

    def test_2_socket_zero_alat(self):
        self.run_merced_ap_rendezvous("2s-zero-alat", 2)

    def test_64_socket_rendezvous(self):
        self.run_merced_ap_rendezvous("64s", 64, memory="128M")

    def test_4_socket_rendezvous(self):
        self.run_merced_ap_rendezvous("4s", 4)

    def test_8_socket_2_core_rendezvous(self):
        self.run_merced_ap_rendezvous(
            "8s2c", "16,sockets=8,cores=2,threads=1")

    def test_1_socket_8_core_rendezvous(self):
        self.run_merced_ap_rendezvous(
            "1s8c", "8,sockets=1,cores=8,threads=1")

    def test_4_socket_8_core_rendezvous(self):
        self.run_merced_ap_rendezvous(
            "4s8c", "32,sockets=4,cores=8,threads=1")


if __name__ == "__main__":
    QemuSystemTest.main()
