/*
 * ISP12160 IOCB parser tests
 *
 * The entries exercise valid and rejected IOCB byte layouts.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/scsi/isp12160_iocb.h"
#include "qemu/bswap.h"

#define TEST_ENTRIES 2U
#define TEST_SEGMENTS 7U

static void build_no_data(uint8_t entries[TEST_ENTRIES]
                                         [ISP12160_IOCB_ENTRY_BYTES])
{
    memset(entries, 0, TEST_ENTRIES * ISP12160_IOCB_ENTRY_BYTES);
    entries[0][0] = ISP12160_IOCB_COMMAND_A64_TYPE;
    entries[0][1] = 1;
    stl_le_p(entries[0] + 4, 0x12345678);
    entries[0][8] = 2;
    entries[0][9] = 3;
    stw_le_p(entries[0] + 10, 6);
    stw_le_p(entries[0] + 16, 30);
    entries[0][20] = 0x00;
}

static void set_segment(uint8_t *entry, size_t offset,
                        uint64_t address, uint32_t length)
{
    stq_le_p(entry + offset, address);
    stl_le_p(entry + offset + 8, length);
}

static void set_segment_32(uint8_t *entry, size_t offset,
                           uint32_t address, uint32_t length)
{
    stl_le_p(entry + offset, address);
    stl_le_p(entry + offset + 4, length);
}

static void build_seven_segments(
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES])
{
    unsigned int i;

    build_no_data(entries);
    entries[0][1] = TEST_ENTRIES;
    entries[0][8] = 31;
    entries[0][9] = 0x80 | 15;
    stw_le_p(entries[0] + 12,
             ISP12160_IOCB_CONTROL_SIMPLE_TAG |
             ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE);
    stw_le_p(entries[0] + 18, TEST_SEGMENTS);
    entries[0][20] = 0x28;
    set_segment(entries[0], 40, UINT64_C(0x1020304050607000), 0x100);
    set_segment(entries[0], 52, UINT64_C(0x2030405060708000), 0x200);

    entries[1][0] = ISP12160_IOCB_CONTINUE_A64_TYPE;
    entries[1][1] = 1;
    entries[1][2] = 1;
    for (i = 0; i < ISP12160_IOCB_CONTINUE_A64_SEGMENTS; i++) {
        set_segment(entries[1], 4 + i * 12,
                    UINT64_C(0x3000000000000000) + i * 0x1000,
                    (i + 3) * 0x100);
    }
}

static void assert_rejected(
    const uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES],
    size_t entry_count, size_t capacity)
{
    ISP12160IOCBCommand command;
    ISP12160IOCBCommand expected_command;
    ISP12160IOCBSegment segments[TEST_SEGMENTS];
    ISP12160IOCBSegment expected_segments[TEST_SEGMENTS];
    Error *err = NULL;

    memset(&command, 0xa5, sizeof(command));
    memset(segments, 0x5a, sizeof(segments));
    expected_command = command;
    memcpy(expected_segments, segments, sizeof(segments));

    g_assert_false(isp12160_iocb_parse_a64(
        entries[0], entry_count, &command, segments, capacity, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_assert_cmpmem(&command, sizeof(command),
                    &expected_command, sizeof(expected_command));
    g_assert_cmpmem(segments, sizeof(segments),
                    expected_segments, sizeof(expected_segments));
}

static void test_no_data(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment untouched = {
        .address = UINT64_MAX,
        .length = UINT32_MAX,
    };
    Error *err = NULL;

    build_no_data(entries);
    memset(entries[0] + ISP12160_IOCB_A64_CDB_OFFSET + 6, 0xa5,
           ISP12160_IOCB_CDB_BYTES - 6);
    memset(entries[0] + ISP12160_IOCB_A64_RESERVED1_OFFSET, 0x5a,
           ISP12160_IOCB_A64_RESERVED1_BYTES);
    set_segment(entries[0], 40, 0x1000, 0x100);
    g_assert_true(isp12160_iocb_parse_a64(
        entries[0], 1, &command, &untouched, 1, &err));
    g_assert_null(err);
    g_assert_cmpuint(command.handle, ==, 0x12345678);
    g_assert_cmpuint(command.entry_count, ==, 1);
    g_assert_cmpuint(command.channel, ==, 0);
    g_assert_cmpuint(command.target, ==, 3);
    g_assert_cmpuint(command.lun, ==, 2);
    g_assert_cmpuint(command.cdb_length, ==, 6);
    g_assert_cmpuint(command.cdb[0], ==, 0x00);
    g_assert_cmpuint(command.timeout, ==, 30);
    g_assert_cmpuint(command.direction, ==,
                     ISP12160_IOCB_DIRECTION_NONE);
    g_assert_cmpuint(command.segment_count, ==, 0);
    g_assert_cmpuint(command.transfer_length, ==, 0);
    g_assert_cmphex(untouched.address, ==, UINT64_MAX);
    g_assert_cmphex(untouched.length, ==, UINT32_MAX);
}

static void test_data_unknown(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment segments[TEST_SEGMENTS];
    Error *err = NULL;

    build_no_data(entries);
    stw_le_p(entries[0] + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET,
             ISP12160_IOCB_CONTROL_DATA_UNKNOWN);
    g_assert_true(isp12160_iocb_parse_a64(
        entries[0], 1, &command, NULL, 0, &err));
    g_assert_null(err);
    g_assert_cmpuint(command.direction, ==,
                     ISP12160_IOCB_DIRECTION_UNKNOWN);
    g_assert_cmpuint(command.segment_count, ==, 0);
    g_assert_cmpuint(command.transfer_length, ==, 0);

    build_seven_segments(entries);
    stw_le_p(entries[0] + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET,
             ISP12160_IOCB_CONTROL_DATA_UNKNOWN);
    g_assert_true(isp12160_iocb_parse_a64(
        entries[0], TEST_ENTRIES, &command, segments,
        G_N_ELEMENTS(segments), &err));
    g_assert_null(err);
    g_assert_cmpuint(command.direction, ==,
                     ISP12160_IOCB_DIRECTION_UNKNOWN);
    g_assert_cmpuint(command.segment_count, ==, TEST_SEGMENTS);
    g_assert_cmphex(command.transfer_length, ==, 0x1c00);
}

static void test_continuation(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment segments[TEST_SEGMENTS];
    Error *err = NULL;

    build_seven_segments(entries);
    g_assert_true(isp12160_iocb_parse_a64(
        entries[0], TEST_ENTRIES, &command, segments,
        G_N_ELEMENTS(segments), &err));
    g_assert_null(err);
    g_assert_cmpuint(command.entry_count, ==, TEST_ENTRIES);
    g_assert_cmpuint(command.channel, ==, 1);
    g_assert_cmpuint(command.target, ==, 15);
    g_assert_cmpuint(command.lun, ==, 31);
    g_assert_cmpuint(command.direction, ==,
                     ISP12160_IOCB_DIRECTION_FROM_DEVICE);
    g_assert_cmpuint(command.segment_count, ==, TEST_SEGMENTS);
    g_assert_cmphex(command.transfer_length, ==, 0x1c00);
    g_assert_cmphex(segments[0].address, ==,
                    UINT64_C(0x1020304050607000));
    g_assert_cmphex(segments[1].address, ==,
                    UINT64_C(0x2030405060708000));
    g_assert_cmphex(segments[6].address, ==,
                    UINT64_C(0x3000000000004000));
    g_assert_cmphex(segments[6].length, ==, 0x700);
}

static void test_32bit_continuation(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES] = { 0 };
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment segments[5];
    Error *err = NULL;
    unsigned int i;

    entries[0][0] = ISP12160_IOCB_COMMAND_TYPE;
    entries[0][1] = 2;
    stl_le_p(entries[0] + 4, 0x87654321);
    entries[0][8] = 6;
    entries[0][9] = 0x80 | 14;
    stw_le_p(entries[0] + 10, 10);
    stw_le_p(entries[0] + 12,
             ISP12160_IOCB_CONTROL_SIMPLE_TAG |
             ISP12160_IOCB_CONTROL_DATA_TO_DEVICE);
    stw_le_p(entries[0] + 16, 15);
    stw_le_p(entries[0] + 18, G_N_ELEMENTS(segments));
    entries[0][20] = 0x2a;
    for (i = 0; i < ISP12160_IOCB_COMMAND_SEGMENTS; i++) {
        set_segment_32(entries[0], ISP12160_IOCB_SEGMENT0_OFFSET +
                       i * ISP12160_IOCB_SEGMENT_STRIDE,
                       0x10000000 + i * 0x1000, (i + 1) * 0x100);
    }
    entries[1][0] = ISP12160_IOCB_CONTINUE_TYPE;
    entries[1][1] = 1;
    set_segment_32(entries[1], ISP12160_IOCB_CONTINUE_SEGMENT0_OFFSET,
                   0x20000000, 0x500);

    g_assert_true(isp12160_iocb_parse_32(
        entries[0], TEST_ENTRIES, &command, segments,
        G_N_ELEMENTS(segments), &err));
    g_assert_null(err);
    g_assert_cmphex(command.handle, ==, 0x87654321);
    g_assert_cmpuint(command.channel, ==, 1);
    g_assert_cmpuint(command.target, ==, 14);
    g_assert_cmpuint(command.lun, ==, 6);
    g_assert_cmpuint(command.direction, ==,
                     ISP12160_IOCB_DIRECTION_TO_DEVICE);
    g_assert_cmpuint(command.segment_count, ==, G_N_ELEMENTS(segments));
    g_assert_cmphex(command.transfer_length, ==, 0xf00);
    g_assert_cmphex(segments[0].address, ==, 0x10000000);
    g_assert_cmphex(segments[4].address, ==, 0x20000000);
    g_assert_cmphex(segments[4].length, ==, 0x500);

    set_segment_32(entries[1], ISP12160_IOCB_CONTINUE_SEGMENT0_OFFSET,
                   0xffffff00, 0x200);
    g_assert_false(isp12160_iocb_parse_32(
        entries[0], TEST_ENTRIES, &command, segments,
        G_N_ELEMENTS(segments), &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_header_and_shape_rejected(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];

    build_no_data(entries);
    entries[0][0] = 1;
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    entries[0][1] = 2;
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    entries[0][3] = 1;
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    entries[0][14] = 1;
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    stw_le_p(entries[0] + 10, ISP12160_IOCB_CDB_BYTES + 1);
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    stw_le_p(entries[0] + 12, 1U << 7);
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    entries[0][8] = 32;
    assert_rejected(entries, 1, TEST_SEGMENTS);

    build_no_data(entries);
    entries[0][9] = 16;
    assert_rejected(entries, 1, TEST_SEGMENTS);
}

static void test_segments_rejected(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];

    build_seven_segments(entries);
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS - 1);

    build_seven_segments(entries);
    entries[1][0] = 2;
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS);

    build_seven_segments(entries);
    entries[1][1] = 2;
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS);

    build_seven_segments(entries);
    entries[1][3] = 1;
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS);

    build_seven_segments(entries);
    stl_le_p(entries[0] + 48, 0);
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS);

    build_seven_segments(entries);
    stq_le_p(entries[0] + 40, UINT64_MAX);
    stl_le_p(entries[0] + 48, 2);
    assert_rejected(entries, TEST_ENTRIES, TEST_SEGMENTS);

    build_no_data(entries);
    stw_le_p(entries[0] + 12,
             ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE);
    assert_rejected(entries, 1, TEST_SEGMENTS);
}

static void test_invalid_arguments(void)
{
    uint8_t entries[TEST_ENTRIES][ISP12160_IOCB_ENTRY_BYTES];
    ISP12160IOCBCommand command;
    Error *err = NULL;

    build_no_data(entries);
    g_assert_false(isp12160_iocb_parse_a64(
        NULL, 1, &command, NULL, 0, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_status_entry(void)
{
    ISP12160IOCBStatus status = {
        .handle = 0x78563412,
        .residual_length = 0x10203040,
        .scsi_status = 2,
        .completion_status = ISP12160_IOCB_CS_DATA_UNDERRUN,
        .state_flags = ISP12160_IOCB_SF_GOT_BUS |
                       ISP12160_IOCB_SF_GOT_TARGET |
                       ISP12160_IOCB_SF_SENT_CDB |
                       ISP12160_IOCB_SF_GOT_STATUS |
                       ISP12160_IOCB_SF_GOT_SENSE,
        .status_flags = 0x1234,
        .time = 7,
        .sense_length = 3,
        .sense = { 0x70, 0x00, 0x05 },
    };
    uint8_t entry[ISP12160_IOCB_ENTRY_BYTES];
    Error *err = NULL;

    memset(entry, 0xa5, sizeof(entry));
    g_assert_true(isp12160_iocb_build_status(
        entry, sizeof(entry), &status, &err));
    g_assert_null(err);
    g_assert_cmpuint(entry[0], ==, ISP12160_IOCB_STATUS_TYPE);
    g_assert_cmpuint(entry[1], ==, 1);
    g_assert_cmphex(ldl_le_p(entry + 4), ==, status.handle);
    g_assert_cmphex(lduw_le_p(entry + 8), ==, status.scsi_status);
    g_assert_cmphex(lduw_le_p(entry + 10), ==,
                    status.completion_status);
    g_assert_cmphex(lduw_le_p(entry + 12), ==, status.state_flags);
    g_assert_cmphex(lduw_le_p(entry + 14), ==, status.status_flags);
    g_assert_cmphex(lduw_le_p(entry + 16), ==, status.time);
    g_assert_cmphex(lduw_le_p(entry + 18), ==, status.sense_length);
    g_assert_cmphex(ldl_le_p(entry + 20), ==, status.residual_length);
    g_assert_true(!memcmp(entry + 32, status.sense, status.sense_length));
    for (size_t i = 24; i < 32; i++) {
        g_assert_cmpuint(entry[i], ==, 0);
    }
    for (size_t i = 32 + status.sense_length; i < sizeof(entry); i++) {
        g_assert_cmpuint(entry[i], ==, 0);
    }
}

static void test_status_rejected(void)
{
    ISP12160IOCBStatus status = { 0 };
    uint8_t entry[ISP12160_IOCB_ENTRY_BYTES];
    uint8_t expected[ISP12160_IOCB_ENTRY_BYTES];
    Error *err = NULL;

    memset(entry, 0xa5, sizeof(entry));
    memcpy(expected, entry, sizeof(entry));
    status.sense_length = ISP12160_IOCB_SENSE_BYTES + 1;
    g_assert_false(isp12160_iocb_build_status(
        entry, sizeof(entry), &status, &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    g_assert_cmpmem(entry, sizeof(entry), expected, sizeof(expected));

    status.sense_length = 0;
    status.state_flags = 1;
    g_assert_false(isp12160_iocb_build_status(
        entry, sizeof(entry), &status, &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    g_assert_cmpmem(entry, sizeof(entry), expected, sizeof(expected));

    status.state_flags = 0;
    status.completion_status = UINT16_MAX;
    g_assert_false(isp12160_iocb_build_status(
        entry, sizeof(entry), &status, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_assert_cmpmem(entry, sizeof(entry), expected, sizeof(expected));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/isp12160-iocb/no-data", test_no_data);
    g_test_add_func("/isp12160-iocb/data-unknown", test_data_unknown);
    g_test_add_func("/isp12160-iocb/continuation", test_continuation);
    g_test_add_func("/isp12160-iocb/32bit-continuation",
                    test_32bit_continuation);
    g_test_add_func("/isp12160-iocb/header-shape-rejected",
                    test_header_and_shape_rejected);
    g_test_add_func("/isp12160-iocb/segments-rejected",
                    test_segments_rejected);
    g_test_add_func("/isp12160-iocb/invalid-arguments",
                    test_invalid_arguments);
    g_test_add_func("/isp12160-iocb/status-entry", test_status_entry);
    g_test_add_func("/isp12160-iocb/status-rejected",
                    test_status_rejected);

    return g_test_run();
}
