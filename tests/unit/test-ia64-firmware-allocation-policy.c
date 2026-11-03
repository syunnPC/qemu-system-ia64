/*
 * Host-side tests for the IA-64 firmware allocation policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-allocation-policy.h"

static int test_direct_alias_limit(void)
{
    UINT64 flags = IA64_FW_COMPAT_LOADER_DIRECT_ALIAS;

    return fw_auto_allocation_max_address(flags, ~(UINT64)0) !=
               IA64_FW_LOADER_DIRECT_ALIAS_OFFSET - 1U ||
           fw_auto_allocation_max_address(flags,
                                          IA64_FW_LOADER_DIRECT_ALIAS_OFFSET) !=
               IA64_FW_LOADER_DIRECT_ALIAS_OFFSET - 1U ||
           fw_auto_allocation_max_address(flags, 0x3fffffffU) != 0x3fffffffU;
}

static int test_unrestricted_limit(void)
{
    return fw_auto_allocation_max_address(0, ~(UINT64)0) != ~(UINT64)0 ||
           fw_auto_allocation_max_address(IA64_FW_COMPAT_FIXED_LOADER_WINDOWS,
                                          0xffffffffU) != 0xffffffffU;
}

int main(void)
{
    return test_direct_alias_limit() || test_unrestricted_limit();
}
