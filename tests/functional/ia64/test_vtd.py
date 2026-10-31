#!/usr/bin/env python3
"""Guest-visible firmware state of the 7500 VT-d machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import QemuSystemTest

from ia64.console import Ia64FirmwareTest
from ia64.efi_build import app_path
from ia64.media import make_fat_disk


VTD_CASES = {
    "dmar-units", "aperture-mmio", "ram-around-aperture", "secondary-bus",
}


class Ia64Vtd(Ia64FirmwareTest):
    def test_memory_map_and_secondary_bus(self):
        """With 4 GiB, RAM surrounds VTBAR and the aperture is MMIO."""
        self.require_device("edu")
        disk = Path(self.scratch_file("vtd.img"))
        make_fat_disk(disk, app_path("vtd"))
        vm = self.launch_ia64(
            media=disk, machine_type="itanium2-vpc-iommu", memory="4G",
            machine_options="firmware-console=serial",
            extra_args=("-device", "edu,bus=ioh7500-secondary,addr=0"))
        result = self.wait_ia64_suite(vm, "vtd", VTD_CASES, timeout=60.0)
        self.assertSetEqual(set(result.cases), VTD_CASES)


if __name__ == "__main__":
    QemuSystemTest.main()
