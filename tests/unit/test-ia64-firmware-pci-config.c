/*
 * Host-side tests for the freestanding IA-64 firmware PCI config helpers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-pci-config.h"

static int test_unique_root_lookup(void)
{
    IA64PlatformPciRoot roots[3] = { 0 };

    roots[0].Segment = 0;
    roots[0].Bus = 0x00;
    roots[0].BusEnd = 0x0f;
    roots[1].Segment = 0;
    roots[1].Bus = 0x10;
    roots[1].BusEnd = 0x1f;
    roots[2].Segment = 1;
    roots[2].Bus = 0x00;
    roots[2].BusEnd = 0xff;

    if (fw_pci_config_find_unique_root(roots, 3, 0, 0x00) != &roots[0] ||
        fw_pci_config_find_unique_root(roots, 3, 0, 0x1f) != &roots[1] ||
        fw_pci_config_find_unique_root(roots, 3, 1, 0xff) != &roots[2] ||
        fw_pci_config_find_unique_root(roots, 3, 0, 0x20) != NULL ||
        fw_pci_config_find_unique_root(roots, 3, 2, 0) != NULL ||
        fw_pci_config_find_unique_root(roots, 3, 0x10000, 0) != NULL ||
        fw_pci_config_find_unique_root(roots, 3, 0, 0x100) != NULL ||
        fw_pci_config_find_unique_root(NULL, 3, 0, 0) != NULL) {
        return 1;
    }

    roots[1].Bus = 0x0f;
    if (fw_pci_config_find_unique_root(roots, 3, 0, 0x0f) != NULL) {
        return 1;
    }
    return 0;
}

static int test_request_validation(void)
{
    if (!fw_pci_config_request_valid(0, 0, 0, 0, 0, 1,
                                     FW_PCI_CF8_CONFIG_SIZE) ||
        !fw_pci_config_request_valid(0, 0xff, 0x1f, 7, 0xfe, 2,
                                     FW_PCI_CF8_CONFIG_SIZE) ||
        !fw_pci_config_request_valid(0, 0xff, 0x1f, 7, 0xfc, 4,
                                     FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0x10000, 0, 0, 0, 0, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0x100, 0, 0, 0, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0x20, 0, 0, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 8, 0, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 0, 0,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 0, 8,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 1, 2,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 2, 4,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 0xff, 2,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 0x100, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, ~(UINT64)0, 1,
                                    FW_PCI_CF8_CONFIG_SIZE) ||
        fw_pci_config_request_valid(0, 0, 0, 0, 0, 1, 0)) {
        return 1;
    }
    return 0;
}

static int test_cf8_encoding(void)
{
    if (!fw_pci_cf8_request_valid(0, 0xff, 0x1f, 7, 0xfc, 4) ||
        fw_pci_cf8_request_valid(1, 0, 0, 0, 0, 1) ||
        fw_pci_cf8_request_valid(0, 0, 0, 0, 0x100, 1) ||
        fw_pci_cf8_address(0, 0, 0, 0) != 0x80000000U ||
        fw_pci_cf8_address(0xab, 0x1d, 6, 0x7f) != 0x80abee7cU ||
        fw_pci_cf8_data_port(0) != FW_PCI_CF8_DATA_PORT ||
        fw_pci_cf8_data_port(1) != FW_PCI_CF8_DATA_PORT + 1U ||
        fw_pci_cf8_data_port(2) != FW_PCI_CF8_DATA_PORT + 2U ||
        fw_pci_cf8_data_port(3) != FW_PCI_CF8_DATA_PORT + 3U ||
        fw_pci_cf8_data_port(0xff) != FW_PCI_CF8_DATA_PORT + 3U) {
        return 1;
    }
    return 0;
}

static int test_ecam_encoding(void)
{
    const UINT64 base = 0x0000000500000000ULL;
    UINT64 address = 0;

    if (!fw_pci_ecam_address(base, 0x20, 0x1d, 6, 0x7f, &address) ||
        address != base + (0x20ULL << 20) + (0x1dULL << 15) +
                   (6ULL << 12) + 0x7fU ||
        !fw_pci_ecam_address(base, 0xff, 0x1f, 7, 0xfff, &address) ||
        address != base + 0xfffffffU ||
        !fw_pci_ecam_address(0, 1, 0, 0, 0, &address) ||
        address != IA64_PLATFORM_PCI_ECAM_BUS_SIZE ||
        fw_pci_ecam_address(base, 0x100, 0, 0, 0, &address) ||
        fw_pci_ecam_address(base, 0, 0x20, 0, 0, &address) ||
        fw_pci_ecam_address(base, 0, 0, 8, 0, &address) ||
        fw_pci_ecam_address(base, 0, 0, 0, 0x1000, &address) ||
        fw_pci_ecam_address(~(UINT64)0 - 0xffffffeU,
                            0xff, 0x1f, 7, 0xfff, &address) ||
        fw_pci_ecam_address(base, 0, 0, 0, 0, NULL)) {
        return 1;
    }
    return 0;
}

static int test_zx1_config_encoding(void)
{
    IA64PlatformPciRoot roots[2] = { 0 };
    FWPciZx1ConfigAccess access;

    roots[0].Segment = 2;
    roots[0].Bus = 0x20;
    roots[0].BusEnd = 0x2f;
    roots[0].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    roots[0].ConfigBase = 0x0000000400000000ULL;
    roots[1].Segment = 3;
    roots[1].Bus = 0x40;
    roots[1].BusEnd = 0x4f;
    roots[1].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    roots[1].ConfigBase = 0x0000000400002000ULL;

    if (!fw_pci_zx1_config_prepare(roots, 2, 2, 0x20, 0x1d, 6,
                                   0x7f, 1, &access) ||
        access.Root != &roots[0] ||
        access.SelectorAddress != roots[0].ConfigBase +
                                  FW_PCI_ZX1_SELECTOR_OFFSET ||
        access.DataAddress != roots[0].ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 3U ||
        access.Selector != (0x1dU << 11 | 6U << 8 | 0x7cU)) {
        return 1;
    }

    if (!fw_pci_zx1_config_prepare(roots, 2, 2, 0x2a, 0x1f, 7,
                                   0xfe, 2, &access) ||
        access.Root != &roots[0] ||
        access.SelectorAddress != roots[0].ConfigBase +
                                  FW_PCI_ZX1_SELECTOR_OFFSET ||
        access.DataAddress != roots[0].ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 2U ||
        access.Selector != (0x2aU << 16 | 0x1fU << 11 |
                            7U << 8 | 0xfcU)) {
        return 1;
    }

    if (!fw_pci_zx1_config_prepare(roots, 2, 3, 0x40, 0, 0,
                                   0xfc, 4, &access) ||
        access.Root != &roots[1] ||
        access.SelectorAddress != roots[1].ConfigBase +
                                  FW_PCI_ZX1_SELECTOR_OFFSET ||
        access.DataAddress != roots[1].ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET ||
        access.Selector != 0xfcU) {
        return 1;
    }

    return 0;
}

static int test_zx1_config_lanes(void)
{
    IA64PlatformPciRoot root = { 0 };
    FWPciZx1ConfigAccess access;

    root.BusEnd = 0xff;
    root.ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    root.ConfigBase = 0x0000000400000000ULL;

    if (!fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   0, 1, &access) ||
        access.DataAddress != root.ConfigBase + FW_PCI_ZX1_DATA_OFFSET ||
        !fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   1, 1, &access) ||
        access.DataAddress != root.ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 1U ||
        !fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   2, 2, &access) ||
        access.DataAddress != root.ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 2U ||
        !fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   3, 1, &access) ||
        access.DataAddress != root.ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 3U ||
        !fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   0xff, 1, &access) ||
        access.DataAddress != root.ConfigBase +
                              FW_PCI_ZX1_DATA_OFFSET + 3U ||
        access.Selector != 0xfcU ||
        !fw_pci_zx1_config_prepare(&root, 1, 0, 0, 0, 0,
                                   0xfc, 4, &access) ||
        access.DataAddress != root.ConfigBase + FW_PCI_ZX1_DATA_OFFSET ||
        access.Selector != 0xfcU) {
        return 1;
    }
    return 0;
}

static int zx1_config_rejected(const IA64PlatformPciRoot *roots,
                               UINTN root_count, UINT64 segment,
                               UINT64 bus, UINT64 device, UINT64 function,
                               UINT64 offset, UINTN size)
{
    const IA64PlatformPciRoot *sentinel_root =
        (const IA64PlatformPciRoot *)(UINTN)1;
    FWPciZx1ConfigAccess access = {
        .Root = sentinel_root,
        .SelectorAddress = 2,
        .DataAddress = 3,
        .Selector = 4,
    };

    return !fw_pci_zx1_config_prepare(roots, root_count, segment, bus,
                                      device, function, offset, size,
                                      &access) &&
        access.Root == sentinel_root && access.SelectorAddress == 2 &&
        access.DataAddress == 3 && access.Selector == 4;
}

static int test_zx1_config_rejections(void)
{
    IA64PlatformPciRoot roots[2] = { 0 };
    FWPciZx1ConfigAccess access;

    roots[0].Segment = 1;
    roots[0].Bus = 0x20;
    roots[0].BusEnd = 0x2f;
    roots[0].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    roots[0].ConfigBase = 0x0000000400000000ULL;

    if (!zx1_config_rejected(NULL, 1, 1, 0x20, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x1f, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x30, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 2, 0x20, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 0x10000, 0x20, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x100, 0, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0x20, 0, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 8, 0, 1) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 0, 0) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 0, 3) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 0, 8) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 1, 2) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 2, 4) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 0xff, 2) ||
        !zx1_config_rejected(roots, 1, 1, 0x20, 0, 0, 0x100, 1) ||
        fw_pci_zx1_config_prepare(roots, 1, 1, 0x20, 0, 0, 0, 1,
                                  NULL)) {
        return 1;
    }

    roots[1] = roots[0];
    roots[1].Bus = 0x2f;
    roots[1].BusEnd = 0x3f;
    roots[1].ConfigBase = 0x0000000400002000ULL;
    if (!zx1_config_rejected(roots, 2, 1, 0x2f, 0, 0, 0, 1)) {
        return 1;
    }

    roots[1].Bus = 0x30;
    roots[0].ConfigType = IA64_PLATFORM_PCI_CONFIG_CF8_CFC;
    if (!zx1_config_rejected(roots, 2, 1, 0x20, 0, 0, 0, 1)) {
        return 1;
    }
    roots[0].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    roots[0].ConfigBase = 0;
    if (!zx1_config_rejected(roots, 2, 1, 0x20, 0, 0, 0, 1)) {
        return 1;
    }
    roots[0].ConfigBase = 0x0000000400000001ULL;
    if (!zx1_config_rejected(roots, 2, 1, 0x20, 0, 0, 0, 1)) {
        return 1;
    }
    roots[0].ConfigBase =
        (1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS) -
        IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE;
    if (!fw_pci_zx1_config_prepare(roots, 2, 1, 0x20, 0, 0,
                                   0xfc, 4, &access)) {
        return 1;
    }
    roots[0].ConfigBase =
        1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS;
    if (!zx1_config_rejected(roots, 2, 1, 0x20, 0, 0, 0, 1)) {
        return 1;
    }
    roots[0].ConfigBase = ~(UINT64)0 &
        ~((UINT64)IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE - 1U);
    if (!zx1_config_rejected(roots, 2, 1, 0x20, 0, 0, 0, 1)) {
        return 1;
    }

    return 0;
}

static int test_pci_io_config_encoding(void)
{
    UINT64 bus;
    UINT64 device;
    UINT64 function;
    UINT64 offset;
    UINT64 address = fw_pci_io_config_address(0xab, 0x1d, 6, 0x7f);

    if (address != ((0xabULL << 24) | (0x1dULL << 16) |
                    (6ULL << 8) | 0x7fU) ||
        !fw_pci_root_config_decode(
            address, 1, 1, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        bus != 0xab || device != 0x1d || function != 6 || offset != 0x7f) {
        return 1;
    }

    address = fw_pci_io_config_address(0xab, 0x1d, 6, 0x101);
    if ((address & 0xffU) != 0 || address >> 32 != 0x101 ||
        !fw_pci_root_config_decode(
            address, 1, 1, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        bus != 0xab || device != 0x1d || function != 6 || offset != 0x101) {
        return 1;
    }
    return 0;
}

static int test_root_config_decode(void)
{
    UINT64 bus;
    UINT64 device;
    UINT64 function;
    UINT64 offset;
    UINT64 address = (0xabULL << 24) | (0x1dULL << 16) |
        (6ULL << 8) | 0xf8U;

    if (!fw_pci_root_config_decode(
            address, 4, 2, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        bus != 0xab || device != 0x1d || function != 6 || offset != 0xf8 ||
        fw_pci_root_config_decode(
            address + 4U, 4, 2, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        !fw_pci_root_config_decode(
            address + 4U, 4, ~(UINTN)0, 1, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            address | (1ULL << 32), 4, 1, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            (0x20ULL << 16), 4, 1, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            (8ULL << 8), 4, 1, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            2, 4, 1, 0, FW_PCI_CF8_CONFIG_SIZE, 0,
            &bus, &device, &function, &offset) ||
        !fw_pci_root_config_decode(
            0xffcULL << 32, 4, 1, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        offset != 0xffc ||
        fw_pci_root_config_decode(
            0xffcULL << 32, 4, 2, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            (0x100ULL << 32) | 1U, 1, 1, 0,
            FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            0, 8, 1, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            &bus, &device, &function, &offset) ||
        fw_pci_root_config_decode(
            0, 1, 1, 0, FW_PCI_ECAM_CONFIG_SIZE, 1,
            NULL, &device, &function, &offset)) {
        return 1;
    }
    return 0;
}

static int test_transfer_buffer_bounds(void)
{
    UINTN limit = ~(UINTN)0;

    if (!fw_pci_transfer_buffer_valid(0x1000, 4, 4, 0) ||
        !fw_pci_transfer_buffer_valid(limit - 16, 4, 4, 0) ||
        !fw_pci_transfer_buffer_valid(limit - 4, 4, 4, 1) ||
        fw_pci_transfer_buffer_valid(limit - 3, 4, 4, 1) ||
        fw_pci_transfer_buffer_valid(limit - 15, 4, 4, 0) ||
        fw_pci_transfer_buffer_valid(0x1000, limit, 2, 0) ||
        fw_pci_transfer_buffer_valid(0x1000, 1, 0, 0)) {
        return 1;
    }
    return 0;
}

static int test_aperture_bounds(void)
{
    UINTN limit = ~(UINTN)0;

    if (!fw_pci_aperture_contains(0x1000, 0x100, 0x1000, 4, 64, 0) ||
        !fw_pci_aperture_contains(0x1000, 0x100, 0x10ff, 1, limit, 1) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x0fff, 1, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1100, 1, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1001, 2, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1002, 4, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1004, 8, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x10ff, 2, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1000, 4, 65, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1000, 0, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0x100, 0x1000, 3, 1, 0) ||
        fw_pci_aperture_contains(0x1000, 0, 0x1000, 1, 1, 0)) {
        return 1;
    }
    return 0;
}

static int test_io_access_sizes(void)
{
    if (!fw_pci_io_access_size_valid(1) ||
        !fw_pci_io_access_size_valid(2) ||
        !fw_pci_io_access_size_valid(4) ||
        fw_pci_io_access_size_valid(0) ||
        fw_pci_io_access_size_valid(3) ||
        fw_pci_io_access_size_valid(8)) {
        return 1;
    }
    return 0;
}

static int test_access_span_bounds(void)
{
    UINT64 limit = ~(UINT64)0;
    UINTN count_limit = ~(UINTN)0;

    if (!fw_pci_access_span_valid(0x1000, 4, 64, 0) ||
        !fw_pci_access_span_valid(0x1000, 4, count_limit, 1) ||
        !fw_pci_access_span_valid(limit - 15U, 8, 1, 0) ||
        !fw_pci_access_span_valid(limit, 1, 0, 0) ||
        fw_pci_access_span_valid(0x1001, 2, 1, 0) ||
        fw_pci_access_span_valid(0x1002, 4, 1, 0) ||
        fw_pci_access_span_valid(0x1004, 8, 1, 0) ||
        fw_pci_access_span_valid(limit, 1, 1, 0) ||
        fw_pci_access_span_valid(limit - 3U, 4, 1, 0) ||
        fw_pci_access_span_valid(0x1000, 8, count_limit, 0) ||
        fw_pci_access_span_valid(0x1000, 0, 1, 0) ||
        fw_pci_access_span_valid(0x1000, 3, 1, 0)) {
        return 1;
    }
    return 0;
}

int main(void)
{
    return test_unique_root_lookup() || test_request_validation() ||
        test_cf8_encoding() || test_ecam_encoding() ||
        test_zx1_config_encoding() ||
        test_zx1_config_lanes() || test_zx1_config_rejections() ||
        test_pci_io_config_encoding() || test_root_config_decode() ||
        test_transfer_buffer_bounds() ||
        test_aperture_bounds() || test_io_access_sizes() ||
        test_access_span_bounds();
}
