/*
 * Host-side tests for the freestanding IA-64 firmware ISP12160 policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-isp12160-policy.h"

static void init_policy(FW_ISP12160_POLICY *Policy)
{
    (void)fw_isp12160_policy_init(1, ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES,
                                  Policy);
}

static FW_ISP12160_DMA_REGION dma_region(UINT64 Address, UINT32 Size)
{
    FW_ISP12160_DMA_REGION region = {
        .PhysicalAddress = Address,
        .DeviceAddress = Address,
        .Size = Size,
    };

    return region;
}

static void init_dma_layout(const FW_ISP12160_POLICY *Policy,
                            FW_ISP12160_DMA_LAYOUT *Layout)
{
    UINT64 address = Policy->DmaApertureBase;

    Layout->RequestQueue = dma_region(address, Policy->RequestQueueBytes);
    address += Policy->RequestQueueBytes;
    Layout->ResponseQueue = dma_region(address, Policy->ResponseQueueBytes);
    address += Policy->ResponseQueueBytes;
    Layout->Token = dma_region(address, Policy->TokenAllocationBytes);
    address += Policy->TokenAllocationBytes;
    Layout->Bounce = dma_region(address, Policy->BounceBytes);
}

static int test_fixed_policy(void)
{
    FW_ISP12160_POLICY policy;

    if (!fw_isp12160_policy_init(1, ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES,
                                 &policy) ||
        policy.Capabilities != ISP12160_QEMU_I2000_KNOWN_CAPABILITIES ||
        (policy.Capabilities & ISP12160_QEMU_I2000_CAPABILITY_A64_IOCB) == 0 ||
        (policy.Capabilities & ISP12160_QEMU_I2000_CAPABILITY_IDENTITY_DMA) ==
            0 ||
        (policy.Capabilities & ISP12160_QEMU_I2000_CAPABILITY_POLLING) == 0 ||
        (policy.Capabilities &
         ISP12160_QEMU_I2000_CAPABILITY_ONE_OUTSTANDING_ONE_SEGMENT) == 0 ||
        policy.Segment != ISP12160_QEMU_I2000_SEGMENT ||
        policy.Bus != ISP12160_QEMU_I2000_BUS ||
        policy.Device != ISP12160_QEMU_I2000_DEVICE ||
        policy.Function != ISP12160_QEMU_I2000_FUNCTION ||
        policy.VendorId != 0x1077 || policy.DeviceId != 0x1216 ||
        policy.Class != 0x0100 || policy.MmioBar != 1 ||
        policy.MmioBase != 0xa0010000ULL || policy.MmioSize != 0x1000 ||
        policy.Gsi != 20 || policy.InterruptPin != 1 ||
        policy.QueueEntries != 64 || policy.MaxOutstanding != 1 ||
        policy.MaxDataSegments != 1 || policy.RequestQueueBytes != 4096 ||
        policy.ResponseQueueBytes != 4096 || policy.TokenBytes != 8 ||
        policy.TokenAllocationBytes != 4096 || policy.BounceBytes != 65536 ||
        policy.DmaAlignment != 4096 || policy.DmaApertureBase != 0 ||
        policy.DmaApertureSize != 0x80000000ULL || !policy.IdentityDma ||
        policy.DmaApertureBase + policy.DmaApertureSize != 0x80000000ULL) {
        return 1;
    }
    return 0;
}

static int test_capability_rejections(void)
{
    FW_ISP12160_POLICY policy;

    if (fw_isp12160_policy_init(0, ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES,
                                &policy) ||
        fw_isp12160_policy_init(1, 0, &policy) ||
        fw_isp12160_policy_init(
            1, ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES | (1U << 31),
            &policy) ||
        fw_isp12160_policy_init(1, ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES,
                                NULL)) {
        return 1;
    }
    return 0;
}

static int test_pci_contract(void)
{
    FW_ISP12160_POLICY policy;
    UINT32 id = 0x12161077U;
    UINT32 class_revision = 0x0100007fU;

    init_policy(&policy);
    if (!fw_isp12160_policy_pci_identity_matches(&policy, id, class_revision) ||
        fw_isp12160_policy_pci_identity_matches(&policy, id ^ 1U,
                                                class_revision) ||
        fw_isp12160_policy_pci_identity_matches(&policy, id ^ (1U << 16),
                                                class_revision) ||
        fw_isp12160_policy_pci_identity_matches(&policy, id,
                                                class_revision ^ (1U << 16)) ||
        fw_isp12160_policy_pci_identity_matches(NULL, id, class_revision) ||
        !fw_isp12160_policy_bar_matches(&policy, 0xa0010000U) ||
        fw_isp12160_policy_bar_matches(&policy, 0xa0010001U) ||
        fw_isp12160_policy_bar_matches(&policy, 0xa0010008U) ||
        fw_isp12160_policy_bar_matches(&policy, 0xa0020000U) ||
        fw_isp12160_policy_bar_matches(NULL, 0xa0010000U) ||
        !fw_isp12160_policy_interrupt_matches(&policy, 1, 20) ||
        fw_isp12160_policy_interrupt_matches(&policy, 2, 20) ||
        fw_isp12160_policy_interrupt_matches(&policy, 1, 21) ||
        fw_isp12160_policy_interrupt_matches(NULL, 1, 20)) {
        return 1;
    }
    return 0;
}

static int test_dma_layout(void)
{
    FW_ISP12160_POLICY policy;
    FW_ISP12160_DMA_LAYOUT layout;
    UINT64 aperture_end;

    init_policy(&policy);
    init_dma_layout(&policy, &layout);
    aperture_end = policy.DmaApertureBase + policy.DmaApertureSize;

    if (!fw_isp12160_dma_layout_valid(&policy, &layout) ||
        !fw_isp12160_dma_range_contains(
            policy.DmaApertureBase, policy.DmaApertureSize,
            aperture_end - policy.BounceBytes, policy.BounceBytes) ||
        fw_isp12160_dma_range_contains(
            policy.DmaApertureBase, policy.DmaApertureSize, aperture_end, 1) ||
        fw_isp12160_dma_range_contains(policy.DmaApertureBase,
                                       policy.DmaApertureSize,
                                       policy.DmaApertureBase, 0) ||
        fw_isp12160_dma_range_contains(0, ~(UINT64)0, ~(UINT64)0, 2) ||
        fw_isp12160_dma_layout_valid(NULL, &layout) ||
        fw_isp12160_dma_layout_valid(&policy, NULL)) {
        return 1;
    }

    layout.RequestQueue.DeviceAddress++;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    init_dma_layout(&policy, &layout);
    layout.ResponseQueue.PhysicalAddress++;
    layout.ResponseQueue.DeviceAddress++;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    init_dma_layout(&policy, &layout);
    layout.Token.Size = policy.TokenBytes;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    init_dma_layout(&policy, &layout);
    layout.Bounce.PhysicalAddress = policy.DmaApertureBase - 0x1000U;
    layout.Bounce.DeviceAddress = layout.Bounce.PhysicalAddress;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    init_dma_layout(&policy, &layout);
    layout.Bounce.PhysicalAddress = aperture_end;
    layout.Bounce.DeviceAddress = aperture_end;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    init_dma_layout(&policy, &layout);
    layout.ResponseQueue = layout.RequestQueue;
    if (fw_isp12160_dma_layout_valid(&policy, &layout)) {
        return 1;
    }
    return 0;
}

static int test_request_bounds(void)
{
    FW_ISP12160_POLICY policy;

    init_policy(&policy);
    if (!fw_isp12160_request_fits_policy(&policy, 0, 0, 0) ||
        !fw_isp12160_request_fits_policy(&policy, 0, 1, policy.BounceBytes) ||
        fw_isp12160_request_fits_policy(&policy, 1, 1, 512) ||
        fw_isp12160_request_fits_policy(&policy, 0, 2, 512) ||
        fw_isp12160_request_fits_policy(&policy, 0, 1, 0) ||
        fw_isp12160_request_fits_policy(&policy, 0, 0, 512) ||
        fw_isp12160_request_fits_policy(&policy, 0, 1,
                                        (UINT64)policy.BounceBytes + 1U) ||
        fw_isp12160_request_fits_policy(NULL, 0, 0, 0)) {
        return 1;
    }
    return 0;
}

int main(void)
{
    return test_fixed_policy() || test_capability_rejections() ||
           test_pci_contract() || test_dma_layout() || test_request_bounds();
}
