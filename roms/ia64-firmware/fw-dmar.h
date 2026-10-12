/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_FW_DMAR_H
#define IA64_FW_DMAR_H
#include "hw/ia64/intel_7500_abi.h"

#define FW_DMAR_SIZE 88U

static inline void fw_dmar_le(unsigned char *p, unsigned long long value,
                               unsigned size)
{
    unsigned i;
    for (i = 0; i < size; i++) {
        p[i] = value >> (8 * i);
    }
}

static inline int fw_build_dmar(unsigned char *p, unsigned capacity,
                                 unsigned long long base)
{
    unsigned i;
    unsigned char sum = 0;
    const unsigned char oem[6] = { 'Q', 'E', 'M', 'U', ' ', ' ' };
    const unsigned char table[8] = { 'I', 'A', '6', '4', 'V', 'T', 'D', ' ' };

    if (capacity < FW_DMAR_SIZE || (base & 0xfff) ||
        base > (1ULL << INTEL_7500_HAW) - 0x2000ULL) {
        return 0;
    }
    for (i = 0; i < FW_DMAR_SIZE; i++) {
        p[i] = 0;
    }
    p[0] = 'D'; p[1] = 'M'; p[2] = 'A'; p[3] = 'R';
    fw_dmar_le(p + 4, FW_DMAR_SIZE, 4);
    p[8] = 1;
    for (i = 0; i < 6; i++) {
        p[10 + i] = oem[i];
    }
    for (i = 0; i < 8; i++) {
        p[16 + i] = table[i];
    }
    fw_dmar_le(p + 24, 1, 4);
    p[28] = 'Q'; p[29] = 'E'; p[30] = 'M'; p[31] = 'U';
    fw_dmar_le(p + 32, 1, 4);
    p[36] = INTEL_7500_HAW - 1;
    /* IR support flag remains clear until Itanium interrupt remapping works. */
    fw_dmar_le(p + 50, 24, 2);
    fw_dmar_le(p + 56, base + INTEL_7500_UNIT_SIZE, 8);
    p[64] = 2; /* PCI sub-hierarchy */
    p[65] = 8;
    p[70] = INTEL_7500_SECONDARY_SLOT;
    fw_dmar_le(p + 74, 16, 2);
    p[76] = 1; /* INCLUDE_PCI_ALL, last DRHD in segment 0. */
    fw_dmar_le(p + 80, base, 8);
    for (i = 0; i < FW_DMAR_SIZE; i++) {
        sum += p[i];
    }
    p[9] = (unsigned char)-sum;
    return 1;
}
#endif
