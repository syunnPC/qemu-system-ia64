/*
 * Host-side tests for the freestanding IA-64 firmware IDE policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-ide-policy.h"

static void init_profile(IA64PlatformI2000Profile *profile)
{
    *profile = (IA64PlatformI2000Profile) {
        .ProfileType = IA64_PLATFORM_PROFILE_TYPE_HP_I2000,
        .ProfileRevision = IA64_PLATFORM_I2000_PROFILE_REVISION,
        .Length = sizeof(*profile),
        .Flags = IA64_I2000_PROFILE_REQUIRED_FLAGS,
        .IdeSegment = IA64_I2000_PROFILE_IDE_SEGMENT,
        .IdeCommandPort = IA64_I2000_PROFILE_IDE_COMMAND_PORT,
        .IdeControlPort = IA64_I2000_PROFILE_IDE_CONTROL_PORT,
        .IdeVendorId = IA64_I2000_PROFILE_IDE_VENDOR_ID,
        .IdeDeviceId = IA64_I2000_PROFILE_IDE_DEVICE_ID,
        .IdeClass = IA64_I2000_PROFILE_IDE_CLASS,
        .IdeBus = IA64_I2000_PROFILE_IDE_BUS,
        .IdeDevice = IA64_I2000_PROFILE_IDE_DEVICE,
        .IdeFunction = IA64_I2000_PROFILE_IDE_FUNCTION,
        .IdeProgIf = IA64_I2000_PROFILE_IDE_PROG_IF,
        .IdeIrq = IA64_I2000_PROFILE_IDE_IRQ,
        .IdeUnitMask = IA64_I2000_PROFILE_IDE_UNIT_MASK,
        .IdeCommandSize = IA64_I2000_PROFILE_IDE_COMMAND_SIZE,
        .IdeControlSize = IA64_I2000_PROFILE_IDE_CONTROL_SIZE,
    };
}

static int test_fixed_policy(void)
{
    IA64PlatformI2000Profile profile;
    FW_IDE_POLICY policy;

    init_profile(&profile);
    if (!fw_i2000_ide_policy_init(&profile, &policy) || policy.Segment != 0 ||
        policy.Bus != 0 || policy.Device != 3 || policy.Function != 1 ||
        policy.VendorId != 0x8086U || policy.DeviceId != 0x7601U ||
        policy.Class != 0x0101U || policy.ProgIf != 0x80U ||
        policy.CommandPort != 0x01f0U || policy.CommandSize != 8 ||
        policy.ControlPort != 0x03f6U || policy.ControlSize != 1 ||
        policy.Irq != 14 || policy.UnitMask != 3 ||
        !fw_ide_policy_unit_enabled(&policy, 0) ||
        !fw_ide_policy_unit_enabled(&policy, 1) ||
        fw_ide_policy_unit_enabled(&policy, 2)) {
        return 1;
    }
    return 0;
}

static int test_pci_identity(void)
{
    IA64PlatformI2000Profile profile;
    FW_IDE_POLICY policy;
    UINT32 id = 0x76018086U;
    UINT32 class_revision = 0x0101807fU;

    init_profile(&profile);
    if (!fw_i2000_ide_policy_init(&profile, &policy) ||
        !fw_ide_policy_pci_identity_matches(&policy, id, class_revision) ||
        fw_ide_policy_pci_identity_matches(&policy, id ^ 1U, class_revision) ||
        fw_ide_policy_pci_identity_matches(&policy, id ^ (1U << 16),
                                           class_revision) ||
        fw_ide_policy_pci_identity_matches(&policy, id,
                                           class_revision ^ (1U << 16)) ||
        fw_ide_policy_pci_identity_matches(&policy, id,
                                           class_revision ^ (1U << 8)) ||
        fw_ide_policy_pci_identity_matches(NULL, id, class_revision)) {
        return 1;
    }
    return 0;
}

static int test_rejections(void)
{
    IA64PlatformI2000Profile profile;
    FW_IDE_POLICY policy;

    init_profile(&profile);
    if (fw_i2000_ide_policy_init(NULL, &policy) ||
        fw_i2000_ide_policy_init(&profile, NULL) ||
        fw_ide_policy_port_range_valid(0, 0) ||
        fw_ide_policy_port_range_valid(0xffffU, 2) ||
        !fw_ide_policy_port_range_valid(0xffffU, 1)) {
        return 1;
    }

    profile.ProfileRevision++;
    if (fw_i2000_ide_policy_init(&profile, &policy)) {
        return 1;
    }
    init_profile(&profile);
    profile.IdeBus++;
    if (fw_i2000_ide_policy_init(&profile, &policy)) {
        return 1;
    }
    init_profile(&profile);
    profile.IdeCommandPort = 0xffffU;
    if (fw_i2000_ide_policy_init(&profile, &policy)) {
        return 1;
    }
    init_profile(&profile);
    profile.IdeUnitMask = 4;
    if (fw_i2000_ide_policy_init(&profile, &policy)) {
        return 1;
    }
    return 0;
}

static int test_ata_capacity_limits(void)
{
    const UINT64 lba28_max = FW_ATA_IDENTIFY_LBA28_MAX_SECTORS;
    const UINT64 lba48_max = FW_ATA_IDENTIFY_LBA48_MAX_SECTORS;

    if (fw_ata_command_max_lba(0) != FW_ATA_LBA28_MAX_LBA ||
        fw_ata_command_max_lba(1) != FW_ATA_LBA48_MAX_LBA ||
        fw_ata_clamp_identify_sector_count(0, 0) != 0 ||
        fw_ata_clamp_identify_sector_count(lba28_max - 1U, 0) !=
            lba28_max - 1U ||
        fw_ata_clamp_identify_sector_count(lba28_max, 0) != lba28_max ||
        fw_ata_clamp_identify_sector_count(lba28_max + 1U, 0) != lba28_max ||
        fw_ata_clamp_identify_sector_count(~(UINT64)0, 0) != lba28_max ||
        fw_ata_clamp_identify_sector_count(0, 1) != 0 ||
        fw_ata_clamp_identify_sector_count(lba48_max - 1U, 1) !=
            lba48_max - 1U ||
        fw_ata_clamp_identify_sector_count(lba48_max, 1) != lba48_max ||
        fw_ata_clamp_identify_sector_count(lba48_max + 1U, 1) != lba48_max ||
        fw_ata_clamp_identify_sector_count(~(UINT64)0, 1) != lba48_max) {
        return 1;
    }
    return 0;
}

static int test_i2000_udma_selection(void)
{
    UINT16 identify[256] = { 0 };
    UINT8 mode = 0xff;

    identify[FW_ATA_IDENTIFY_CAPABILITIES_WORD] = FW_ATA_IDENTIFY_DMA_SUPPORTED;
    identify[FW_ATA_IDENTIFY_VALIDITY_WORD] = FW_ATA_IDENTIFY_UDMA_VALID;
    identify[FW_ATA_IDENTIFY_UDMA_WORD] = 0x003f;
    if (!fw_ide_i2000_select_udma(identify, &mode) || mode != 2) {
        return 1;
    }

    identify[FW_ATA_IDENTIFY_UDMA_WORD] = 0x0003;
    if (!fw_ide_i2000_select_udma(identify, &mode) || mode != 1) {
        return 1;
    }
    identify[FW_ATA_IDENTIFY_UDMA_WORD] = 0x0001;
    if (!fw_ide_i2000_select_udma(identify, &mode) || mode != 0) {
        return 1;
    }
    identify[FW_ATA_IDENTIFY_UDMA_WORD] = 0;
    if (fw_ide_i2000_select_udma(identify, &mode)) {
        return 1;
    }
    identify[FW_ATA_IDENTIFY_UDMA_WORD] = 0x0007;
    identify[FW_ATA_IDENTIFY_VALIDITY_WORD] = 0;
    if (fw_ide_i2000_select_udma(identify, &mode)) {
        return 1;
    }
    identify[FW_ATA_IDENTIFY_VALIDITY_WORD] = FW_ATA_IDENTIFY_UDMA_VALID;
    identify[FW_ATA_IDENTIFY_CAPABILITIES_WORD] = 0;
    return fw_ide_i2000_select_udma(identify, &mode) ||
           fw_ide_i2000_select_udma(NULL, &mode) ||
           fw_ide_i2000_select_udma(identify, NULL);
}

int main(void)
{
    return test_fixed_policy() || test_pci_identity() || test_rejections() ||
           test_ata_capacity_limits() || test_i2000_udma_selection();
}
