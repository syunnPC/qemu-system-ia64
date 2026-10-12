/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Shared with the freestanding IA-64 firmware; no hosted includes.
 */
#ifndef HW_IA64_INTEL_7500_ABI_H
#define HW_IA64_INTEL_7500_ABI_H
#define INTEL_7500_VTBAR 0x00000000fed90000ULL
#define INTEL_7500_UNIT_SIZE 0x1000U
#define INTEL_7500_HAW 51U
#define INTEL_7500_SECONDARY_SLOT 7U
#define IA64_VTD_HANDOFF_ADDR 0x00000000000ff140ULL
#define IA64_VTD_HANDOFF_MAGIC 0x4454563436414951ULL /* QIA64VTD */
#define IA64_VTD_HANDOFF_VERSION 1ULL

typedef struct __attribute__((packed)) IA64VtdHandoff {
    unsigned long long Magic;
    unsigned long long Version;
    unsigned long long Vtbar;
} IA64VtdHandoff;
#endif
