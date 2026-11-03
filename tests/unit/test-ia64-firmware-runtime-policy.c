/*
 * Host-side tests for the freestanding IA-64 firmware runtime policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-runtime-policy.h"

static int test_generic_policy(void)
{
    FW_RUNTIME_POLICY policy;

    return !fw_runtime_policy_init(0, 0, &policy) ||
           !policy.VariableServicesAvailable || !policy.TimeServicesAvailable;
}

static int test_fixed_i2000_policy(void)
{
    FW_RUNTIME_POLICY policy;

    return !fw_runtime_policy_init(1, IA64_I2000_PROFILE_REQUIRED_FLAGS,
                                   &policy) ||
           !policy.VariableServicesAvailable || !policy.TimeServicesAvailable;
}

static int test_rejections(void)
{
    FW_RUNTIME_POLICY policy;

    return fw_runtime_policy_init(0, 0, NULL) ||
           fw_runtime_policy_init(0, IA64_I2000_PROFILE_REQUIRED_FLAGS,
                                  &policy) ||
           fw_runtime_policy_init(
               1,
               IA64_I2000_PROFILE_REQUIRED_FLAGS ^
                   IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE,
               &policy);
}

static int test_virtual_ranges(void)
{
    const UINT64 pages = 2;
    const UINT64 size = pages << FW_RUNTIME_PAGE_SHIFT;

    return !fw_runtime_virtual_range_valid(0x2000, 0x80002000, pages) ||
           !fw_runtime_virtual_range_valid(~(UINT64)0 - size, ~(UINT64)0 - size,
                                           pages) ||
           fw_runtime_virtual_range_valid(0, 0, 0) ||
           fw_runtime_virtual_range_valid(
               0, 0, (~(UINT64)0 >> FW_RUNTIME_PAGE_SHIFT) + 1U) ||
           fw_runtime_virtual_range_valid(~(UINT64)0 - size + 1U, 0x80002000,
                                          pages) ||
           fw_runtime_virtual_range_valid(0x2000, ~(UINT64)0 - size + 1U,
                                          pages);
}

int main(void)
{
    return test_generic_policy() || test_fixed_i2000_policy() ||
           test_rejections() || test_virtual_ranges();
}
