/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IDE presentation policy for the i2000 firmware profile.
 */

#ifndef IA64_FIRMWARE_FW_IDE_POLICY_H
#define IA64_FIRMWARE_FW_IDE_POLICY_H

#include "fw-base.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"

typedef struct {
    UINT16 Segment;
    UINT8 Bus;
    UINT8 Device;
    UINT8 Function;
    UINT16 VendorId;
    UINT16 DeviceId;
    UINT16 Class;
    UINT8 ProgIf;
    UINT16 CommandPort;
    UINT8 CommandSize;
    UINT16 ControlPort;
    UINT8 ControlSize;
    UINT8 Irq;
    UINT8 UnitMask;
} FW_IDE_POLICY;

/* ATA task-file address limits and IDENTIFY capacity-field limits. */
#define FW_ATA_LBA28_MAX_LBA 0x000000000fffffffULL
#define FW_ATA_LBA48_MAX_LBA 0x0000ffffffffffffULL
#define FW_ATA_IDENTIFY_LBA28_MAX_SECTORS 0x000000000fffffffULL
#define FW_ATA_IDENTIFY_LBA48_MAX_SECTORS 0x0000ffffffffffffULL
#define FW_ATA_IDENTIFY_CAPABILITIES_WORD 49U
#define FW_ATA_IDENTIFY_VALIDITY_WORD     53U
#define FW_ATA_IDENTIFY_UDMA_WORD         88U
#define FW_ATA_IDENTIFY_DMA_SUPPORTED     (1U << 8)
#define FW_ATA_IDENTIFY_UDMA_VALID        (1U << 2)
#define FW_IDE_I2000_MAX_UDMA_MODE        2U

static inline UINT64 fw_ata_command_max_lba(BOOLEAN Lba48)
{
    return Lba48 ? FW_ATA_LBA48_MAX_LBA : FW_ATA_LBA28_MAX_LBA;
}

static inline UINT64 fw_ata_clamp_identify_sector_count(UINT64 Sectors,
                                                        BOOLEAN Lba48)
{
    UINT64 maximum = Lba48 ? FW_ATA_IDENTIFY_LBA48_MAX_SECTORS
                           : FW_ATA_IDENTIFY_LBA28_MAX_SECTORS;

    return Sectors < maximum ? Sectors : maximum;
}

static inline BOOLEAN fw_ide_i2000_select_udma(const UINT16 *Identify,
                                               UINT8 *Mode)
{
    UINT16 supported;

    if (Identify == NULL || Mode == NULL ||
        (Identify[FW_ATA_IDENTIFY_CAPABILITIES_WORD] &
         FW_ATA_IDENTIFY_DMA_SUPPORTED) == 0 ||
        (Identify[FW_ATA_IDENTIFY_VALIDITY_WORD] &
         FW_ATA_IDENTIFY_UDMA_VALID) == 0) {
        return 0;
    }

    supported = Identify[FW_ATA_IDENTIFY_UDMA_WORD] &
                ((1U << (FW_IDE_I2000_MAX_UDMA_MODE + 1U)) - 1U);
    if ((supported & (1U << 2)) != 0) {
        *Mode = 2;
    } else if ((supported & (1U << 1)) != 0) {
        *Mode = 1;
    } else if ((supported & 1U) != 0) {
        *Mode = 0;
    } else {
        return 0;
    }
    return 1;
}

static inline BOOLEAN fw_ide_policy_port_range_valid(UINT16 Port, UINT8 Size)
{
    return Size != 0 && (UINT32)Port + Size <= 0x10000U;
}

static inline BOOLEAN
fw_i2000_ide_policy_init(const IA64PlatformI2000Profile *Profile,
                         FW_IDE_POLICY *Policy)
{
    FW_IDE_POLICY result = { 0 };

    if (Profile == NULL || Policy == NULL ||
        Profile->ProfileType != IA64_PLATFORM_PROFILE_TYPE_HP_I2000 ||
        Profile->ProfileRevision != IA64_PLATFORM_I2000_PROFILE_REVISION ||
        Profile->Length != sizeof(*Profile) ||
        Profile->Flags != IA64_I2000_PROFILE_REQUIRED_FLAGS ||
        Profile->IdeSegment != IA64_I2000_PROFILE_IDE_SEGMENT ||
        Profile->IdeBus != IA64_I2000_PROFILE_IDE_BUS ||
        Profile->IdeDevice != IA64_I2000_PROFILE_IDE_DEVICE ||
        Profile->IdeFunction != IA64_I2000_PROFILE_IDE_FUNCTION ||
        Profile->IdeVendorId != IA64_I2000_PROFILE_IDE_VENDOR_ID ||
        Profile->IdeDeviceId != IA64_I2000_PROFILE_IDE_DEVICE_ID ||
        Profile->IdeClass != IA64_I2000_PROFILE_IDE_CLASS ||
        Profile->IdeProgIf != IA64_I2000_PROFILE_IDE_PROG_IF ||
        Profile->IdeCommandPort != IA64_I2000_PROFILE_IDE_COMMAND_PORT ||
        Profile->IdeCommandSize != IA64_I2000_PROFILE_IDE_COMMAND_SIZE ||
        Profile->IdeControlPort != IA64_I2000_PROFILE_IDE_CONTROL_PORT ||
        Profile->IdeControlSize != IA64_I2000_PROFILE_IDE_CONTROL_SIZE ||
        Profile->IdeIrq != IA64_I2000_PROFILE_IDE_IRQ ||
        Profile->IdeUnitMask == 0 || Profile->IdeDevice >= 32U ||
        Profile->IdeFunction >= 8U ||
        !fw_ide_policy_port_range_valid(Profile->IdeCommandPort,
                                        Profile->IdeCommandSize) ||
        !fw_ide_policy_port_range_valid(Profile->IdeControlPort,
                                        Profile->IdeControlSize) ||
        (Profile->IdeUnitMask & ~0x03U) != 0) {
        return 0;
    }

    result.Segment = Profile->IdeSegment;
    result.Bus = Profile->IdeBus;
    result.Device = Profile->IdeDevice;
    result.Function = Profile->IdeFunction;
    result.VendorId = Profile->IdeVendorId;
    result.DeviceId = Profile->IdeDeviceId;
    result.Class = Profile->IdeClass;
    result.ProgIf = Profile->IdeProgIf;
    result.CommandPort = Profile->IdeCommandPort;
    result.CommandSize = Profile->IdeCommandSize;
    result.ControlPort = Profile->IdeControlPort;
    result.ControlSize = Profile->IdeControlSize;
    result.Irq = Profile->IdeIrq;
    result.UnitMask = Profile->IdeUnitMask;
    *Policy = result;
    return 1;
}

static inline BOOLEAN
fw_ide_policy_pci_identity_matches(const FW_IDE_POLICY *Policy,
                                   UINT32 VendorDevice, UINT32 ClassRevision)
{
    if (Policy == NULL) {
        return 0;
    }

    return (UINT16)VendorDevice == Policy->VendorId &&
           (UINT16)(VendorDevice >> 16) == Policy->DeviceId &&
           (UINT16)(ClassRevision >> 16) == Policy->Class &&
           (UINT8)(ClassRevision >> 8) == Policy->ProgIf;
}

static inline BOOLEAN fw_ide_policy_unit_enabled(const FW_IDE_POLICY *Policy,
                                                 UINTN Unit)
{
    return Policy != NULL && Unit < 2U &&
           (Policy->UnitMask & (1U << Unit)) != 0;
}

#endif /* IA64_FIRMWARE_FW_IDE_POLICY_H */
