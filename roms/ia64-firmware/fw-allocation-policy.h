/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI automatic page-allocation policy.
 */

#ifndef IA64_FIRMWARE_FW_ALLOCATION_POLICY_H
#define IA64_FIRMWARE_FW_ALLOCATION_POLICY_H

#include "fw-base.h"
#include "hw/ia64/ia64_firmware_compat.h"

static inline UINT64 fw_auto_allocation_max_address(UINT64 CompatFlags,
                                                    UINT64 MaxAddress)
{
    if ((CompatFlags & IA64_FW_COMPAT_LOADER_DIRECT_ALIAS) != 0 &&
        MaxAddress >= IA64_FW_LOADER_DIRECT_ALIAS_OFFSET) {
        return IA64_FW_LOADER_DIRECT_ALIAS_OFFSET - 1U;
    }
    return MaxAddress;
}

#endif /* IA64_FIRMWARE_FW_ALLOCATION_POLICY_H */
