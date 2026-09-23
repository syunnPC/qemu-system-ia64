/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded IA-64 firmware image parsing helpers.
 */

#ifndef HW_IA64_LOADER_H
#define HW_IA64_LOADER_H

#include "qapi/error.h"

typedef struct IA64FirmwareSegment {
    uint64_t offset;
    uint64_t size;
} IA64FirmwareSegment;

typedef struct IA64FirmwareElf {
    uint64_t base;
    uint64_t size;
    uint64_t alignment;
    uint64_t entry;
    uint64_t global_pointer;
    uint64_t pal_entry;
    uint8_t *data;
    GArray *segments;
} IA64FirmwareElf;

bool ia64_firmware_elf_load(const void *image, size_t image_size,
                            uint64_t load_base, IA64FirmwareElf *result,
                            Error **errp);
void ia64_firmware_elf_clear(IA64FirmwareElf *image);

typedef struct IA64FirmwareEntrypoint {
    uint64_t entry;
    uint64_t global_pointer;
} IA64FirmwareEntrypoint;

bool ia64_loader_parse_pe_plabel(const void *image, size_t image_size,
                                 IA64FirmwareEntrypoint *result);

#endif /* HW_IA64_LOADER_H */
