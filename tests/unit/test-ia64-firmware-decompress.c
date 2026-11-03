/*
 * Host-side test for the freestanding IA-64 EFI decompression module.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-decompress.h"

#define TEST_SCRATCH_SIZE (64U * 1024U)

void fw_set_mem(VOID *buffer, UINTN size, UINT8 value)
{
    UINT8 *bytes = buffer;

    while (size-- != 0) {
        *bytes++ = value;
    }
}

int main(void)
{
    static const UINT8 compressed[] = {
        0x49, 0x00, 0x00, 0x00, 0x22, 0x01, 0x00, 0x00, 0x00, 0x3d, 0x4b, 0x92,
        0x8e, 0x1c, 0x7e, 0x3f, 0xe8, 0x06, 0xe5, 0x20, 0x01, 0x90, 0xf9, 0x1a,
        0x03, 0x2f, 0xd4, 0x89, 0x07, 0x7c, 0xb6, 0xc6, 0xa6, 0x58, 0xad, 0x26,
        0x24, 0x00, 0x10, 0x21, 0x67, 0x1d, 0x0a, 0x48, 0x63, 0xa9, 0xaa, 0x33,
        0x70, 0x3b, 0x29, 0xa8, 0x70, 0x63, 0x92, 0x74, 0x67, 0x2f, 0x10, 0xdc,
        0x2d, 0x05, 0xe8, 0xf0, 0xa6, 0xaa, 0xec, 0xb6, 0xeb, 0xf0, 0xc7, 0x2d,
        0x76, 0x39, 0x26, 0xff, 0x5e, 0x1f, 0xe4, 0x00, 0x00,
    };
    static const UINT8 expected_half[] =
        "EFI decompression protocol test vector. "
        "EFI decompression protocol test vector.\n"
        "0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\n";
    static UINT64 scratch[TEST_SCRATCH_SIZE / sizeof(UINT64)];
    static UINT8 output[290];
    UINT32 destination_size = 0;
    UINT32 scratch_size = 0;
    UINTN i;

    if (fw_decompress_protocol.GetInfo(
            &fw_decompress_protocol, (VOID *)compressed, sizeof(compressed),
            &destination_size, &scratch_size) != EFI_SUCCESS ||
        destination_size != sizeof(output) || scratch_size > sizeof(scratch) ||
        fw_decompress_protocol.Decompress(
            &fw_decompress_protocol, (VOID *)compressed, sizeof(compressed),
            output, sizeof(output), scratch, scratch_size) != EFI_SUCCESS) {
        return 1;
    }
    for (i = 0; i < sizeof(output); i++) {
        if (output[i] != expected_half[i % (sizeof(expected_half) - 1U)]) {
            return 1;
        }
    }
    if (fw_decompress_protocol.GetInfo(
            &fw_decompress_protocol, (VOID *)compressed,
            sizeof(compressed) - 1U, &destination_size,
            &scratch_size) != EFI_INVALID_PARAMETER ||
        fw_decompress_protocol.Decompress(
            &fw_decompress_protocol, (VOID *)compressed,
            sizeof(compressed) - 1U, output, sizeof(output), scratch,
            sizeof(scratch)) != EFI_INVALID_PARAMETER ||
        fw_decompress_protocol.Decompress(
            &fw_decompress_protocol, (VOID *)compressed, sizeof(compressed),
            output, sizeof(output) - 1U, scratch,
            sizeof(scratch)) != EFI_INVALID_PARAMETER) {
        return 1;
    }
    return 0;
}
