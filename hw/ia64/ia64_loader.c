/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded IA-64 PE32+ firmware entry-point parser.
 */

#include "qemu/osdep.h"
#include "hw/ia64/ia64_loader.h"

#define IA64_MZ_HEADER_SIZE          0x40U
#define IA64_MZ_PE_OFFSET            0x3cU
#define IA64_PE_HEADER_SIZE          24U
#define IA64_PE_MACHINE_IA64         0x0200U
#define IA64_PE_OPTIONAL_MAGIC_PE32P 0x020bU
#define IA64_PE_OPTIONAL_ENTRY       16U
#define IA64_PE_OPTIONAL_MIN_SIZE    20U
#define IA64_PE_OPTIONAL_SIZE_OFFSET 20U
#define IA64_PLABEL_SIZE             16U

static uint16_t ia64_loader_lduw(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static uint32_t ia64_loader_ldl(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint64_t ia64_loader_ldq(const uint8_t *p)
{
    return (uint64_t)ia64_loader_ldl(p) | (uint64_t)ia64_loader_ldl(p + 4)
                                              << 32;
}

static bool ia64_loader_range_valid(size_t image_size, size_t offset,
                                    size_t length)
{
    return offset <= image_size && length <= image_size - offset;
}

bool ia64_loader_parse_pe_plabel(const void *image, size_t image_size,
                                 IA64FirmwareEntrypoint *result)
{
    const uint8_t *bytes = image;
    const uint8_t *pe;
    const uint8_t *optional;
    const uint8_t *plabel;
    uint32_t pe_offset;
    uint32_t entry_rva;
    uint16_t optional_size;
    uint64_t entry;

    if (bytes == NULL || result == NULL ||
        !ia64_loader_range_valid(image_size, 0, IA64_MZ_HEADER_SIZE) ||
        ia64_loader_lduw(bytes) != 0x5a4d) {
        return false;
    }

    pe_offset = ia64_loader_ldl(bytes + IA64_MZ_PE_OFFSET);
    if (pe_offset < IA64_MZ_HEADER_SIZE ||
        !ia64_loader_range_valid(image_size, pe_offset, IA64_PE_HEADER_SIZE)) {
        return false;
    }

    pe = bytes + pe_offset;
    if (memcmp(pe, "PE\0\0", 4) != 0 ||
        ia64_loader_lduw(pe + 4) != IA64_PE_MACHINE_IA64) {
        return false;
    }

    optional_size = ia64_loader_lduw(pe + IA64_PE_OPTIONAL_SIZE_OFFSET);
    if (optional_size < IA64_PE_OPTIONAL_MIN_SIZE ||
        !ia64_loader_range_valid(image_size, pe_offset + IA64_PE_HEADER_SIZE,
                                 optional_size)) {
        return false;
    }

    optional = pe + IA64_PE_HEADER_SIZE;
    if (ia64_loader_lduw(optional) != IA64_PE_OPTIONAL_MAGIC_PE32P) {
        return false;
    }

    entry_rva = ia64_loader_ldl(optional + IA64_PE_OPTIONAL_ENTRY);
    if (entry_rva == 0 ||
        !ia64_loader_range_valid(image_size, entry_rva, IA64_PLABEL_SIZE)) {
        return false;
    }

    plabel = bytes + entry_rva;
    entry = ia64_loader_ldq(plabel);
    if (entry == 0 || entry >= 0x100000000ULL) {
        return false;
    }

    result->entry = entry;
    result->global_pointer = ia64_loader_ldq(plabel + 8);
    return true;
}
