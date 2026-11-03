/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI runtime-resource policy.
 */

#ifndef IA64_FIRMWARE_FW_RUNTIME_POLICY_H
#define IA64_FIRMWARE_FW_RUNTIME_POLICY_H

#include "fw-base.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"

typedef struct {
    BOOLEAN VariableServicesAvailable;
    BOOLEAN TimeServicesAvailable;
} FW_RUNTIME_POLICY;

#define FW_RUNTIME_PAGE_SHIFT 12U

static inline BOOLEAN fw_runtime_virtual_range_valid(UINT64 PhysicalStart,
                                                     UINT64 VirtualStart,
                                                     UINT64 NumberOfPages)
{
    UINT64 size;

    if (NumberOfPages == 0 ||
        NumberOfPages > (~(UINT64)0 >> FW_RUNTIME_PAGE_SHIFT)) {
        return 0;
    }
    size = NumberOfPages << FW_RUNTIME_PAGE_SHIFT;
    return PhysicalStart <= ~(UINT64)0 - size &&
           VirtualStart <= ~(UINT64)0 - size;
}

static inline BOOLEAN fw_runtime_policy_init(BOOLEAN FixedI2000,
                                             UINT32 ProfileFlags,
                                             FW_RUNTIME_POLICY *Policy)
{
    FW_RUNTIME_POLICY result = {
        .VariableServicesAvailable = 1,
        .TimeServicesAvailable = 1,
    };

    if (Policy == NULL) {
        return 0;
    }
    if (FixedI2000) {
        if (ProfileFlags != IA64_I2000_PROFILE_REQUIRED_FLAGS) {
            return 0;
        }
        result.VariableServicesAvailable =
            (ProfileFlags & IA64_I2000_PROFILE_FLAG_EFI_VARS_UNAVAILABLE) == 0;
        result.TimeServicesAvailable =
            (ProfileFlags & IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE) == 0;
    } else if (ProfileFlags != 0) {
        return 0;
    }

    *Policy = result;
    return 1;
}

#endif /* IA64_FIRMWARE_FW_RUNTIME_POLICY_H */
