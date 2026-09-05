/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PCI configuration request helpers.
 */

#ifndef IA64_FIRMWARE_FW_PCI_CONFIG_H
#define IA64_FIRMWARE_FW_PCI_CONFIG_H

#include "fw-base.h"
#include "hw/ia64/ia64_platform_abi.h"

#define FW_PCI_CF8_ADDRESS_PORT 0x0cf8U
#define FW_PCI_CF8_DATA_PORT    0x0cfcU
#define FW_PCI_CF8_CONFIG_SIZE  0x0100U
#define FW_PCI_ECAM_CONFIG_SIZE 0x1000U
#define FW_PCI_ZX1_CONFIG_SIZE    0x0100U
#define FW_PCI_ZX1_SELECTOR_OFFSET 0x0040U
#define FW_PCI_ZX1_DATA_OFFSET     0x0048U

typedef struct FWPciZx1ConfigAccess {
    const IA64PlatformPciRoot *Root;
    UINT64 SelectorAddress;
    UINT64 DataAddress;
    UINT32 Selector;
} FWPciZx1ConfigAccess;

static inline const IA64PlatformPciRoot *
fw_pci_config_find_unique_root(const IA64PlatformPciRoot *Roots,
                               UINTN Count, UINT64 Segment, UINT64 Bus)
{
    const IA64PlatformPciRoot *match = NULL;
    UINTN i;

    if (Roots == NULL || Segment > 0xffffU || Bus > 0xffU) {
        return NULL;
    }

    for (i = 0; i < Count; i++) {
        const IA64PlatformPciRoot *root = &Roots[i];

        if (root->Segment != Segment || Bus < root->Bus ||
            Bus > root->BusEnd) {
            continue;
        }
        if (match != NULL) {
            return NULL;
        }
        match = root;
    }
    return match;
}

static inline BOOLEAN fw_pci_config_request_valid(UINT64 Segment,
                                                   UINT64 Bus,
                                                   UINT64 Device,
                                                   UINT64 Function,
                                                   UINT64 Offset,
                                                   UINTN Size,
                                                   UINT64 ConfigSize)
{
    if (Segment > 0xffffU || Bus > 0xffU || Device > 0x1fU ||
        Function > 7U ||
        (Size != 1U && Size != 2U && Size != 4U) ||
        ConfigSize == 0 || Offset >= ConfigSize ||
        (Offset & (Size - 1U)) != 0) {
        return 0;
    }

    return Size <= ConfigSize - Offset;
}

static inline UINT32 fw_pci_cf8_address(UINT64 Bus, UINT64 Device,
                                         UINT64 Function, UINT64 Offset)
{
    return 0x80000000U | ((UINT32)Bus << 16) |
        ((UINT32)Device << 11) | ((UINT32)Function << 8) |
        ((UINT32)Offset & 0xfcU);
}

static inline UINT64 fw_pci_io_config_address(UINT64 Bus, UINT64 Device,
                                               UINT64 Function,
                                               UINT32 Offset)
{
    UINT64 address = (Function << 8) | (Device << 16) | (Bus << 24);

    if (Offset <= 0xffU) {
        address |= Offset;
    } else {
        address |= (UINT64)Offset << 32;
    }
    return address;
}

static inline BOOLEAN fw_pci_cf8_request_valid(UINT64 Segment, UINT64 Bus,
                                                UINT64 Device,
                                                UINT64 Function,
                                                UINT64 Offset, UINTN Size)
{
    return Segment == 0 &&
        fw_pci_config_request_valid(Segment, Bus, Device, Function, Offset,
                                    Size, FW_PCI_CF8_CONFIG_SIZE);
}

static inline BOOLEAN fw_pci_ecam_address(UINT64 Base, UINT64 Bus,
                                           UINT64 Device, UINT64 Function,
                                           UINT64 Offset, UINT64 *Address)
{
    UINT64 delta;

    if (Address == NULL || Bus > 0xffU || Device > 0x1fU ||
        Function > 7U || Offset >= FW_PCI_ECAM_CONFIG_SIZE) {
        return 0;
    }
    delta = (Bus << 20) | (Device << 15) | (Function << 12) | Offset;
    if (Base > ~(UINT64)0 - delta) {
        return 0;
    }
    *Address = Base + delta;
    return 1;
}

/*
 * Resolve a Mercury indirect configuration transaction.  Bus zero selects
 * the root bus; rejected requests leave the output unchanged.
 */
static inline BOOLEAN fw_pci_zx1_config_prepare(
    const IA64PlatformPciRoot *Roots, UINTN RootCount, UINT64 Segment,
    UINT64 Bus, UINT64 Device, UINT64 Function, UINT64 Offset, UINTN Size,
    FWPciZx1ConfigAccess *Access)
{
    const IA64PlatformPciRoot *root;
    FWPciZx1ConfigAccess result;
    UINT32 selector_bus;

    if (Access == NULL ||
        !fw_pci_config_request_valid(Segment, Bus, Device, Function, Offset,
                                     Size, FW_PCI_ZX1_CONFIG_SIZE)) {
        return 0;
    }

    root = fw_pci_config_find_unique_root(Roots, RootCount, Segment, Bus);
    if (root == NULL ||
        root->ConfigType != IA64_PLATFORM_PCI_CONFIG_ZX1_LBA ||
        root->ConfigBase == 0 ||
        (root->ConfigBase &
         (IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE - 1U)) != 0 ||
        root->ConfigBase >
            (1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS) -
            IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE) {
        return 0;
    }

    selector_bus = Bus == root->Bus ? 0 : (UINT32)Bus;
    result.Root = root;
    result.SelectorAddress = root->ConfigBase +
                             FW_PCI_ZX1_SELECTOR_OFFSET;
    result.DataAddress = root->ConfigBase + FW_PCI_ZX1_DATA_OFFSET +
                         (Offset & 3U);
    result.Selector = (selector_bus << 16) | ((UINT32)Device << 11) |
        ((UINT32)Function << 8) | ((UINT32)Offset & 0xfcU);
    *Access = result;
    return 1;
}

static inline BOOLEAN fw_pci_root_config_decode(
    UINT64 Address, UINTN Size, UINTN Count, BOOLEAN Fifo,
    UINT64 ConfigSize, BOOLEAN Extended, UINT64 *Bus, UINT64 *Device,
    UINT64 *Function, UINT64 *Offset)
{
    UINT64 register_offset = Address & 0xffU;
    UINT64 extended_offset = Address >> 32;
    UINT64 span;

    if (Bus == NULL || Device == NULL || Function == NULL || Offset == NULL ||
        (!Extended && extended_offset != 0)) {
        return 0;
    }
    *Function = (Address >> 8) & 0xffU;
    *Device = (Address >> 16) & 0xffU;
    *Bus = (Address >> 24) & 0xffU;
    if (extended_offset != 0) {
        if (register_offset != 0) {
            return 0;
        }
        register_offset = extended_offset;
    }
    *Offset = register_offset;
    if (!fw_pci_config_request_valid(0, *Bus, *Device, *Function, *Offset,
                                     Size, ConfigSize)) {
        return 0;
    }
    if (Fifo || Count == 0) {
        return 1;
    }
    if (Count > ~(UINT64)0 / Size) {
        return 0;
    }
    span = Size * (UINT64)Count;
    return span <= ConfigSize - *Offset;
}

static inline UINTN fw_pci_cf8_data_port(UINT64 Offset)
{
    return FW_PCI_CF8_DATA_PORT + (UINTN)(Offset & 3U);
}

static inline BOOLEAN fw_pci_transfer_buffer_valid(UINTN Buffer,
                                                    UINTN Count,
                                                    UINTN Size,
                                                    BOOLEAN Fill)
{
    UINTN span;

    if (Size == 0 || Count > ~(UINTN)0 / Size) {
        return 0;
    }
    if (Count == 0) {
        span = 0;
    } else if (Fill) {
        span = Size;
    } else {
        span = Size * (UINTN)Count;
    }
    /* Keep the loop's one-past pointer representable as well as its bytes. */
    return span == 0 || Buffer <= ~(UINTN)0 - span;
}

static inline BOOLEAN fw_pci_io_access_size_valid(UINTN Size)
{
    /* IA-64 sparse port translation represents one 1/2/4-byte group. */
    return Size == 1U || Size == 2U || Size == 4U;
}

/* Validate access size, alignment, and count arithmetic before BAR lookup. */
static inline BOOLEAN fw_pci_access_span_valid(UINT64 Address, UINTN Size,
                                                UINTN Count, BOOLEAN Fifo)
{
    UINT64 access_count;

    if ((Size != 1U && Size != 2U && Size != 4U && Size != 8U) ||
        (Address & (Size - 1U)) != 0) {
        return 0;
    }
    access_count = Fifo && Count != 0 ? 1U : (UINT64)Count;
    return access_count == 0 ||
           access_count <= (~(UINT64)0 - Address) / Size;
}

static inline BOOLEAN fw_pci_aperture_contains(UINT64 Base, UINT64 Length,
                                               UINT64 Address, UINTN Size,
                                               UINTN Count, BOOLEAN Fifo)
{
    UINT64 offset;

    if (Length == 0 ||
        (Size != 1U && Size != 2U && Size != 4U && Size != 8U) ||
        (Address & (Size - 1U)) != 0 || Address < Base) {
        return 0;
    }
    offset = Address - Base;
    if (offset >= Length || Size > Length - offset) {
        return 0;
    }
    return Fifo || Count <= (Length - offset) / Size;
}

#endif /* IA64_FIRMWARE_FW_PCI_CONFIG_H */
