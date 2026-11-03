/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 firmware SCSI bounds policy.
 */

#ifndef IA64_FIRMWARE_FW_SCSI_POLICY_H
#define IA64_FIRMWARE_FW_SCSI_POLICY_H

#include "fw-base.h"

#define FW_SCSI_LSI53C895A_VENDOR_DEVICE_ID 0x00121000U

static inline BOOLEAN fw_scsi_lsi53c895a_id_supported(UINT32 VendorDevice)
{
    return VendorDevice == FW_SCSI_LSI53C895A_VENDOR_DEVICE_ID;
}

static inline BOOLEAN fw_scsi_capacity_10_valid(UINT32 LastLba,
                                                UINT32 BlockSize,
                                                UINT32 BounceBytes,
                                                UINT64 CapacityLimitBytes)
{
    if (BlockSize == 0 || BounceBytes == 0 || BlockSize > BounceBytes ||
        CapacityLimitBytes <= 1U) {
        return 0;
    }

    /* READ(10) capacity stays below this limit. */
    return (UINT64)LastLba + 1U <= (CapacityLimitBytes - 1U) / BlockSize;
}

static inline BOOLEAN fw_scsi_transfer_bytes(UINT32 BlockSize,
                                             UINT32 BlockCount,
                                             UINT32 BounceBytes,
                                             UINT32 *TransferBytes)
{
    UINT32 result;

    if (TransferBytes == NULL || BlockSize == 0 || BlockCount == 0 ||
        BounceBytes == 0 || BlockCount > BounceBytes / BlockSize) {
        return 0;
    }

    result = BlockSize;
    result *= BlockCount;
    *TransferBytes = result;
    return 1;
}

#endif /* IA64_FIRMWARE_FW_SCSI_POLICY_H */
