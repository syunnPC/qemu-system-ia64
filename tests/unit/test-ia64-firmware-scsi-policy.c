/*
 * Host-side tests for the freestanding IA-64 firmware SCSI bounds policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-scsi-policy.h"

#define TEST_BOUNCE_BYTES  (64U * 1024U)
#define TEST_CAPACITY_LIMIT (1ULL << 41)

static int test_lsi_identity(void)
{
    if (!fw_scsi_lsi53c895a_id_supported(FW_SCSI_LSI53C895A_VENDOR_DEVICE_ID) ||
        fw_scsi_lsi53c895a_id_supported(0x00131000U) ||
        fw_scsi_lsi53c895a_id_supported(0x12161077U) ||
        fw_scsi_lsi53c895a_id_supported(0xffffffffU)) {
        return 1;
    }
    return 0;
}

static int test_capacity_bounds(void)
{
    UINT32 maximum_last_lba = (UINT32)((TEST_CAPACITY_LIMIT - 1U) / 512U - 1U);

    if (!fw_scsi_capacity_10_valid(maximum_last_lba, 512, TEST_BOUNCE_BYTES,
                                   TEST_CAPACITY_LIMIT) ||
        fw_scsi_capacity_10_valid(maximum_last_lba + 1U, 512, TEST_BOUNCE_BYTES,
                                  TEST_CAPACITY_LIMIT) ||
        !fw_scsi_capacity_10_valid(0, TEST_BOUNCE_BYTES, TEST_BOUNCE_BYTES,
                                   TEST_CAPACITY_LIMIT) ||
        fw_scsi_capacity_10_valid(0, TEST_BOUNCE_BYTES + 1U, TEST_BOUNCE_BYTES,
                                  TEST_CAPACITY_LIMIT) ||
        fw_scsi_capacity_10_valid(0, 0, TEST_BOUNCE_BYTES,
                                  TEST_CAPACITY_LIMIT) ||
        fw_scsi_capacity_10_valid(0, 512, 0, TEST_CAPACITY_LIMIT) ||
        fw_scsi_capacity_10_valid(0, 512, TEST_BOUNCE_BYTES, 1)) {
        return 1;
    }
    return 0;
}

static int test_transfer_bounds(void)
{
    UINT32 bytes = 0;

    if (!fw_scsi_transfer_bytes(512, 128, TEST_BOUNCE_BYTES, &bytes) ||
        bytes != TEST_BOUNCE_BYTES ||
        fw_scsi_transfer_bytes(512, 129, TEST_BOUNCE_BYTES, &bytes) ||
        !fw_scsi_transfer_bytes(TEST_BOUNCE_BYTES, 1, TEST_BOUNCE_BYTES,
                                &bytes) ||
        bytes != TEST_BOUNCE_BYTES ||
        fw_scsi_transfer_bytes(TEST_BOUNCE_BYTES + 1U, 1, TEST_BOUNCE_BYTES,
                               &bytes) ||
        !fw_scsi_transfer_bytes(~(UINT32)0, 1, ~(UINT32)0, &bytes) ||
        bytes != ~(UINT32)0 ||
        fw_scsi_transfer_bytes(0x80000000U, 2, ~(UINT32)0, &bytes) ||
        fw_scsi_transfer_bytes(1, ~(UINT32)0, TEST_BOUNCE_BYTES, &bytes) ||
        fw_scsi_transfer_bytes(512, 0, TEST_BOUNCE_BYTES, &bytes) ||
        fw_scsi_transfer_bytes(0, 1, TEST_BOUNCE_BYTES, &bytes) ||
        fw_scsi_transfer_bytes(512, 1, 0, &bytes) ||
        fw_scsi_transfer_bytes(512, 1, TEST_BOUNCE_BYTES, NULL)) {
        return 1;
    }
    return 0;
}

int main(void)
{
    return test_lsi_identity() || test_capacity_bounds() ||
           test_transfer_bounds();
}
