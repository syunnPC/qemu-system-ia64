/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 firmware ISP12160 policy.
 */

#ifndef IA64_FIRMWARE_FW_ISP12160_POLICY_H
#define IA64_FIRMWARE_FW_ISP12160_POLICY_H

#include "fw-base.h"
#include "hw/scsi/isp12160_abi.h"

typedef struct {
    UINT32 Capabilities;
    UINT16 Segment;
    UINT8 Bus;
    UINT8 Device;
    UINT8 Function;
    UINT16 VendorId;
    UINT16 DeviceId;
    UINT16 Class;
    UINT8 MmioBar;
    UINT64 MmioBase;
    UINT32 MmioSize;
    UINT32 Gsi;
    UINT8 InterruptPin;
    UINT16 QueueEntries;
    UINT16 MaxOutstanding;
    UINT16 MaxDataSegments;
    UINT32 RequestQueueBytes;
    UINT32 ResponseQueueBytes;
    UINT32 TokenBytes;
    UINT32 TokenAllocationBytes;
    UINT32 BounceBytes;
    UINT32 DmaAlignment;
    UINT64 DmaApertureBase;
    UINT64 DmaApertureSize;
    BOOLEAN IdentityDma;
} FW_ISP12160_POLICY;

typedef struct {
    UINT64 PhysicalAddress;
    UINT64 DeviceAddress;
    UINT32 Size;
} FW_ISP12160_DMA_REGION;

typedef struct {
    FW_ISP12160_DMA_REGION RequestQueue;
    FW_ISP12160_DMA_REGION ResponseQueue;
    FW_ISP12160_DMA_REGION Token;
    FW_ISP12160_DMA_REGION Bounce;
} FW_ISP12160_DMA_LAYOUT;

static inline BOOLEAN fw_isp12160_policy_init(BOOLEAN FixedI2000Capability,
                                              UINT32 Capabilities,
                                              FW_ISP12160_POLICY *Policy)
{
    FW_ISP12160_POLICY result = { 0 };

    if (Policy == NULL || !FixedI2000Capability ||
        Capabilities != ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES) {
        return 0;
    }

    result.Capabilities = Capabilities;
    result.Segment = ISP12160_QEMU_I2000_SEGMENT;
    result.Bus = ISP12160_QEMU_I2000_BUS;
    result.Device = ISP12160_QEMU_I2000_DEVICE;
    result.Function = ISP12160_QEMU_I2000_FUNCTION;
    result.VendorId = ISP12160_PCI_VENDOR_ID;
    result.DeviceId = ISP12160_PCI_DEVICE_ID;
    result.Class = ISP12160_PCI_CLASS;
    result.MmioBar = ISP12160_PCI_MMIO_BAR;
    result.MmioBase = ISP12160_QEMU_I2000_BAR_ADDRESS;
    result.MmioSize = ISP12160_QEMU_I2000_BAR_SIZE;
    result.Gsi = ISP12160_QEMU_I2000_GSI;
    result.InterruptPin = ISP12160_QEMU_I2000_INTERRUPT_PIN;
    result.QueueEntries = ISP12160_QEMU_I2000_QUEUE_ENTRIES;
    result.MaxOutstanding = ISP12160_QEMU_I2000_MAX_OUTSTANDING;
    result.MaxDataSegments = ISP12160_QEMU_I2000_MAX_DATA_SEGMENTS;
    result.RequestQueueBytes = ISP12160_QEMU_I2000_REQUEST_QUEUE_BYTES;
    result.ResponseQueueBytes = ISP12160_QEMU_I2000_RESPONSE_QUEUE_BYTES;
    result.TokenBytes = ISP12160_QEMU_TOKEN_BYTES;
    result.TokenAllocationBytes = ISP12160_QEMU_I2000_TOKEN_ALLOCATION_BYTES;
    result.BounceBytes = ISP12160_QEMU_I2000_BOUNCE_BYTES;
    result.DmaAlignment = ISP12160_QEMU_I2000_DMA_ALIGNMENT;
    result.DmaApertureBase = ISP12160_QEMU_I2000_DMA_APERTURE_BASE;
    result.DmaApertureSize = ISP12160_QEMU_I2000_DMA_APERTURE_SIZE;
    result.IdentityDma = ISP12160_QEMU_I2000_IDENTITY_DMA;

    *Policy = result;
    return 1;
}

static inline BOOLEAN fw_isp12160_policy_pci_identity_matches(
    const FW_ISP12160_POLICY *Policy, UINT32 VendorDevice, UINT32 ClassRevision)
{
    if (Policy == NULL) {
        return 0;
    }

    return (UINT16)VendorDevice == Policy->VendorId &&
           (UINT16)(VendorDevice >> 16) == Policy->DeviceId &&
           (UINT16)(ClassRevision >> 16) == Policy->Class;
}

static inline BOOLEAN
fw_isp12160_policy_bar_matches(const FW_ISP12160_POLICY *Policy,
                               UINT32 BarValue)
{
    if (Policy == NULL || Policy->MmioBase > 0xffffffffU) {
        return 0;
    }

    /* The fixed BAR is non-prefetchable, 32-bit memory. */
    return (BarValue & 0x0fU) == 0 &&
           (BarValue & ~0x0fU) == (UINT32)Policy->MmioBase;
}

static inline BOOLEAN
fw_isp12160_policy_interrupt_matches(const FW_ISP12160_POLICY *Policy,
                                     UINT8 InterruptPin, UINT32 Gsi)
{
    return Policy != NULL && InterruptPin == Policy->InterruptPin &&
           Gsi == Policy->Gsi;
}

static inline BOOLEAN fw_isp12160_dma_range_contains(UINT64 Base, UINT64 Length,
                                                     UINT64 Address,
                                                     UINT64 Size)
{
    UINT64 offset;

    if (Length == 0 || Size == 0 || Address < Base) {
        return 0;
    }
    offset = Address - Base;
    return offset < Length && Size <= Length - offset;
}

static inline BOOLEAN
fw_isp12160_dma_region_valid(const FW_ISP12160_POLICY *Policy,
                             const FW_ISP12160_DMA_REGION *Region,
                             UINT32 ExpectedSize)
{
    if (Policy == NULL || Region == NULL || ExpectedSize == 0 ||
        Region->Size != ExpectedSize || Policy->DmaAlignment == 0 ||
        (Policy->DmaAlignment & (Policy->DmaAlignment - 1U)) != 0 ||
        (Region->PhysicalAddress & (Policy->DmaAlignment - 1U)) != 0 ||
        !Policy->IdentityDma ||
        Region->PhysicalAddress != Region->DeviceAddress) {
        return 0;
    }

    return fw_isp12160_dma_range_contains(
        Policy->DmaApertureBase, Policy->DmaApertureSize,
        Region->PhysicalAddress, Region->Size);
}

static inline BOOLEAN
fw_isp12160_dma_regions_overlap(const FW_ISP12160_DMA_REGION *Left,
                                const FW_ISP12160_DMA_REGION *Right)
{
    UINT64 left_end;
    UINT64 right_end;

    if (Left == NULL || Right == NULL || Left->Size == 0 || Right->Size == 0 ||
        Left->PhysicalAddress > ~(UINT64)0 - Left->Size ||
        Right->PhysicalAddress > ~(UINT64)0 - Right->Size) {
        return 1;
    }
    left_end = Left->PhysicalAddress + Left->Size;
    right_end = Right->PhysicalAddress + Right->Size;
    return Left->PhysicalAddress < right_end &&
           Right->PhysicalAddress < left_end;
}

static inline BOOLEAN
fw_isp12160_dma_layout_valid(const FW_ISP12160_POLICY *Policy,
                             const FW_ISP12160_DMA_LAYOUT *Layout)
{
    if (Policy == NULL || Layout == NULL ||
        !fw_isp12160_dma_region_valid(Policy, &Layout->RequestQueue,
                                      Policy->RequestQueueBytes) ||
        !fw_isp12160_dma_region_valid(Policy, &Layout->ResponseQueue,
                                      Policy->ResponseQueueBytes) ||
        !fw_isp12160_dma_region_valid(Policy, &Layout->Token,
                                      Policy->TokenAllocationBytes) ||
        !fw_isp12160_dma_region_valid(Policy, &Layout->Bounce,
                                      Policy->BounceBytes)) {
        return 0;
    }

    return !fw_isp12160_dma_regions_overlap(&Layout->RequestQueue,
                                            &Layout->ResponseQueue) &&
           !fw_isp12160_dma_regions_overlap(&Layout->RequestQueue,
                                            &Layout->Token) &&
           !fw_isp12160_dma_regions_overlap(&Layout->RequestQueue,
                                            &Layout->Bounce) &&
           !fw_isp12160_dma_regions_overlap(&Layout->ResponseQueue,
                                            &Layout->Token) &&
           !fw_isp12160_dma_regions_overlap(&Layout->ResponseQueue,
                                            &Layout->Bounce) &&
           !fw_isp12160_dma_regions_overlap(&Layout->Token, &Layout->Bounce);
}

static inline BOOLEAN
fw_isp12160_request_fits_policy(const FW_ISP12160_POLICY *Policy,
                                UINTN Outstanding, UINTN DataSegments,
                                UINT64 TransferBytes)
{
    if (Policy == NULL || Outstanding >= Policy->MaxOutstanding ||
        DataSegments > Policy->MaxDataSegments ||
        TransferBytes > Policy->BounceBytes) {
        return 0;
    }

    return (DataSegments == 0) == (TransferBytes == 0);
}

#endif /* IA64_FIRMWARE_FW_ISP12160_POLICY_H */
