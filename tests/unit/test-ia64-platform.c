/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 platform descriptor validation tests.
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_platform.h"
#include "qemu/bswap.h"
#include "qemu/units.h"

typedef union IA64PlatformTestDescriptor {
    uint64_t alignment;
    uint8_t bytes[IA64_PLATFORM_DESC_MAX_SIZE];
} IA64PlatformTestDescriptor;

#define TEST_ZX1_CONFIG_BASE 0x0000000400000000ULL
#define TEST_LEGACY_IO_BASE  0x0000000ffc000000ULL
#define TEST_CONSOLE_BASE    (TEST_LEGACY_IO_BASE + 0x003f8000ULL)

G_STATIC_ASSERT(sizeof(IA64PlatformI2000Profile) == 88);
G_STATIC_ASSERT(offsetof(IA64PlatformI2000Profile,
                         Isp12160Capabilities) == 80);

static IA64PlatformDescriptor *test_descriptor_init(
    IA64PlatformTestDescriptor *storage, uint32_t platform_id)
{
    IA64PlatformDescriptor *descriptor = (void *)storage->bytes;
    uint32_t ram_offset = sizeof(*descriptor);
    uint32_t root_offset = ram_offset + 2 * sizeof(IA64PlatformRamRange);
    uint32_t sapic_offset = root_offset + sizeof(IA64PlatformPciRoot);
    uint32_t route_offset = sapic_offset + sizeof(IA64PlatformIoSapic);
    uint32_t total_size = route_offset + sizeof(IA64PlatformPciRoute);
    IA64PlatformRamRange *ram;
    IA64PlatformPciRoot *root;
    IA64PlatformIoSapic *sapic;
    IA64PlatformPciRoute *route;

    memset(storage, 0, sizeof(*storage));
    descriptor->Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC);
    descriptor->FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION);
    descriptor->HeaderSize = cpu_to_le32(sizeof(*descriptor));
    descriptor->TotalSize = cpu_to_le32(total_size);
    descriptor->PlatformId = cpu_to_le32(platform_id);
    descriptor->Flags = cpu_to_le32(
        IA64_PLATFORM_FLAG_NO_MCFG |
        IA64_PLATFORM_FLAG_QEMU_EXTENSION |
        (platform_id == IA64_PLATFORM_ID_HP_I2000 ?
         IA64_PLATFORM_FLAG_FAMILY_HP_I2000 |
         IA64_PLATFORM_FLAG_PCI_CF8 :
         IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
         IA64_PLATFORM_FLAG_PCI_ZX1_LBA |
         IA64_PLATFORM_FLAG_SPARSE_IO |
         IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC |
         IA64_PLATFORM_FLAG_ACPI_PM));
    descriptor->RamSize = cpu_to_le64(2 * GiB);
    descriptor->LowRamEnd = cpu_to_le64(1 * GiB);
    descriptor->FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE);
    descriptor->FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE);
    descriptor->ProcessorCount = cpu_to_le32(2);
    descriptor->SocketCount = cpu_to_le32(2);
    descriptor->CoresPerSocket = cpu_to_le32(1);
    descriptor->ThreadsPerCore = cpu_to_le32(1);
    descriptor->PhysicalAddressBits = cpu_to_le32(
        platform_id == IA64_PLATFORM_ID_HP_I2000 ?
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS :
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS);
    descriptor->MaxSockets = cpu_to_le32(256);
    descriptor->MaxCoresPerSocket = cpu_to_le32(256);
    descriptor->MaxThreadsPerCore = cpu_to_le32(256);
    descriptor->MaxPciRoots = cpu_to_le32(IA64_PLATFORM_MAX_PCI_ROOTS);
    descriptor->PciRootIdentity = cpu_to_le32(
        platform_id == IA64_PLATFORM_ID_HP_I2000 ?
        IA64_PLATFORM_PCI_ROOT_IDENTITY_GENERIC :
        IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX);
    descriptor->NumaNodeCount = cpu_to_le32(1);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(2);
    descriptor->NumaNode[0].RamRangeMask = cpu_to_le32(3);
    descriptor->NumaNode[0].Distance[0] = 10;
    descriptor->RamRangeOffset = cpu_to_le32(ram_offset);
    descriptor->RamRangeCount = cpu_to_le32(2);
    descriptor->RamRangeEntrySize =
        cpu_to_le32(sizeof(IA64PlatformRamRange));
    descriptor->PciRootOffset = cpu_to_le32(root_offset);
    descriptor->PciRootCount = cpu_to_le32(1);
    descriptor->PciRootEntrySize =
        cpu_to_le32(sizeof(IA64PlatformPciRoot));
    descriptor->IoSapicOffset = cpu_to_le32(sapic_offset);
    descriptor->IoSapicCount = cpu_to_le32(1);
    descriptor->IoSapicEntrySize =
        cpu_to_le32(sizeof(IA64PlatformIoSapic));
    descriptor->PciRouteOffset = cpu_to_le32(route_offset);
    descriptor->PciRouteCount = cpu_to_le32(1);
    descriptor->PciRouteEntrySize =
        cpu_to_le32(sizeof(IA64PlatformPciRoute));
    descriptor->LegacyIoBase = cpu_to_le64(TEST_LEGACY_IO_BASE);
    descriptor->LegacyIoSize = cpu_to_le64(64 * MiB);
    descriptor->LocalSapicBase = cpu_to_le64(0xfee00000ULL);
    descriptor->LocalSapicSize = cpu_to_le64(2 * MiB);
    descriptor->ConsoleBase = cpu_to_le64(TEST_CONSOLE_BASE);
    descriptor->ConsoleRegisterStride = cpu_to_le32(1);
    descriptor->ConsoleClockHz = cpu_to_le32(1843200);
    descriptor->ConsoleIrq = cpu_to_le32(4);
    descriptor->NvramBase = cpu_to_le64(0xfff00000);
    descriptor->NvramSize = cpu_to_le64(64 * KiB);
    descriptor->RtcBase = cpu_to_le64(0xffef0000);
    descriptor->RtcSize = cpu_to_le64(8 * KiB);
    descriptor->RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE);
    descriptor->RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE);
    ram = (void *)(storage->bytes + ram_offset);
    ram[0].Size = cpu_to_le64(1 * GiB);
    ram[1].Base = cpu_to_le64(4 * GiB);
    ram[1].Size = cpu_to_le64(1 * GiB);
    root = (void *)(storage->bytes + root_offset);
    root->Segment = cpu_to_le16(0);
    root->Bus = 0;
    if (platform_id != IA64_PLATFORM_ID_HP_I2000) {
        root->ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
        root->ConfigBase = cpu_to_le64(TEST_ZX1_CONFIG_BASE);
    } else {
        root->ConfigType = IA64_PLATFORM_PCI_CONFIG_CF8_CFC;
    }
    root->IoBase = cpu_to_le64(0x1000);
    root->IoSize = cpu_to_le64(0x1000);
    root->Mmio32Base = cpu_to_le64(0x80000000);
    root->Mmio32Size = cpu_to_le64(0x10000000);
    root->DmaBase = cpu_to_le64(0);
    root->DmaSize = cpu_to_le64(2 * GiB);
    sapic = (void *)(storage->bytes + sapic_offset);
    sapic->Base = cpu_to_le64(0xfec00000);
    sapic->GsiBase = cpu_to_le32(0);
    sapic->RedirectionEntries = cpu_to_le32(64);
    sapic->Version = cpu_to_le32(0x21);
    sapic->Id = 0;
    route = (void *)(storage->bytes + route_offset);
    route->Segment = cpu_to_le16(0);
    route->Bus = 0;
    route->Device = 1;
    route->Pin = 0;
    route->Gsi = cpu_to_le32(16);
    ia64_platform_desc_finalize(descriptor, sizeof(*storage));
    return descriptor;
}

static IA64PlatformDescriptor *test_i2000_descriptor_init(
    IA64PlatformTestDescriptor *storage, size_t *descriptor_size)
{
    IA64PlatformTestDescriptor template_storage;
    IA64PlatformDescriptor *template = test_descriptor_init(
        &template_storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformDescriptor header = *template;
    IA64PlatformRamRange ram[2];
    IA64PlatformPciRoot roots[2];
    IA64PlatformIoSapic sapic = *(IA64PlatformIoSapic *)(
        template_storage.bytes + le32_to_cpu(template->IoSapicOffset));
    IA64PlatformPciRoute routes[2];
    IA64PlatformI2000Profile profile;
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = ram,
        .ram_range_count = G_N_ELEMENTS(ram),
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = &sapic,
        .io_sapic_count = 1,
        .pci_routes = routes,
        .pci_route_count = G_N_ELEMENTS(routes),
        .profiles = &profile,
        .profile_count = 1,
    };
    Error *err = NULL;
    uint64_t console_offset =
        (((uint64_t)IA64_I2000_PROFILE_UART_PORT >> 2) << 12) |
        ((uint64_t)IA64_I2000_PROFILE_UART_PORT & 0xfffULL);

    memcpy(ram, template_storage.bytes +
           le32_to_cpu(template->RamRangeOffset), sizeof(ram));
    roots[0] = *(IA64PlatformPciRoot *)(
        template_storage.bytes + le32_to_cpu(template->PciRootOffset));
    routes[0] = *(IA64PlatformPciRoute *)(
        template_storage.bytes + le32_to_cpu(template->PciRouteOffset));
    header.Flags = cpu_to_le32(IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS);
    header.ConsoleBase = cpu_to_le64(TEST_LEGACY_IO_BASE + console_offset);
    header.ConsoleIrq = 0;
    header.NvramBase = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_BASE);
    header.NvramSize = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_SIZE);
    header.RtcBase = 0;
    header.RtcSize = 0;
    roots[0].IoBase = 0;
    roots[0].IoSize = cpu_to_le64(0x4000);
    roots[1] = roots[0];
    roots[1].Bus = ISP12160_QEMU_I2000_BUS;
    roots[1].BusEnd = ISP12160_QEMU_I2000_BUS;
    roots[1].IoBase = cpu_to_le64(0x4000);
    roots[1].Mmio32Base = cpu_to_le64(0xa0000000);
    roots[1].DmaBase = cpu_to_le64(
        ISP12160_QEMU_I2000_DMA_APERTURE_BASE);
    roots[1].DmaSize = cpu_to_le64(
        ISP12160_QEMU_I2000_DMA_APERTURE_SIZE);
    routes[1] = (IA64PlatformPciRoute) {
        .Segment = cpu_to_le16(ISP12160_QEMU_I2000_SEGMENT),
        .Bus = ISP12160_QEMU_I2000_BUS,
        .Device = ISP12160_QEMU_I2000_DEVICE,
        .Pin = ISP12160_QEMU_I2000_INTERRUPT_PIN - 1U,
        .Gsi = cpu_to_le32(ISP12160_QEMU_I2000_GSI),
    };
    ia64_platform_i2000_profile_init(&profile);

    g_assert_true(ia64_platform_desc_build(
        storage, sizeof(*storage), &header, &arrays,
        descriptor_size, &err));
    g_assert_null(err);
    return (IA64PlatformDescriptor *)storage->bytes;
}

static void assert_invalid(IA64PlatformDescriptor *descriptor,
                           uint32_t platform_id)
{
    Error *err = NULL;

    g_assert_false(ia64_platform_desc_validate(
        descriptor, IA64_PLATFORM_DESC_MAX_SIZE, platform_id, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void assert_valid(IA64PlatformDescriptor *descriptor,
                         uint32_t platform_id)
{
    Error *err = NULL;

    g_assert_true(ia64_platform_desc_validate(
        descriptor, IA64_PLATFORM_DESC_MAX_SIZE, platform_id, &err));
    g_assert_null(err);
}

static void test_valid(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    Error *err = NULL;

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    g_assert_true(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_null(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    g_assert_true(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_null(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    g_assert_true(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_RX2660, &err));
    g_assert_null(err);
}

static void test_console_gsi_zero(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);

    /* GSI zero is owned by the declared I/O SAPIC, not a sentinel. */
    descriptor->ConsoleIrq = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_legacy_io_pal_contract(void)
{
    const uint64_t size = IA64_PLATFORM_MIN_LEGACY_IO_SIZE;

    g_assert_true(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS, TEST_LEGACY_IO_BASE, size));
    g_assert_true(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS,
        (1ULL << IA64_PLATFORM_I2000_PHYS_ADDR_BITS) - size, size));
    g_assert_false(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS,
        1ULL << IA64_PLATFORM_I2000_PHYS_ADDR_BITS, size));
    g_assert_true(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
        (1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS) - size, size));
    g_assert_false(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
        1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS, size));
    g_assert_true(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
        (1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS) - size, size));
    g_assert_false(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
        1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS, size));
    g_assert_false(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS, 0xfc000000ULL, size));
    g_assert_false(ia64_platform_legacy_io_valid(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS, TEST_LEGACY_IO_BASE,
        size + IA64_PLATFORM_RESOURCE_ALIGNMENT));
    g_assert_false(ia64_platform_legacy_io_valid(
        0, TEST_LEGACY_IO_BASE, size));
}

static void test_revision_and_platform(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);

    descriptor->FormatRevision = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->FormatRevision = cpu_to_le32(
        IA64_PLATFORM_DESC_REVISION + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->Flags |=
        cpu_to_le32(IA64_PLATFORM_FLAG_FIRMWARE_COMPAT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->Flags |=
        cpu_to_le32(IA64_PLATFORM_FLAG_FIRMWARE_COMPAT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_range_and_overlap(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->PciRootOffset = descriptor->IoSapicOffset;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->PciRouteCount = cpu_to_le32(UINT32_MAX);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_checksum(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    Error *err = NULL;

    descriptor->Checksum = cpu_to_le32(
        le32_to_cpu(descriptor->Checksum) ^ 1U);
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "checksum"));
    error_free(err);
}

static void test_topology(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->CoresPerSocket = cpu_to_le32(2);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(1);
    descriptor->SocketCount = cpu_to_le32(1);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(8);
    descriptor->SocketCount = cpu_to_le32(2);
    descriptor->CoresPerSocket = cpu_to_le32(2);
    descriptor->ThreadsPerCore = cpu_to_le32(2);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(8);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(7);
    descriptor->CoresPerSocket = cpu_to_le32(2);
    descriptor->ThreadsPerCore = cpu_to_le32(2);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(7);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(3);
    descriptor->SocketCount = cpu_to_le32(3);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(3);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(6);
    descriptor->CoresPerSocket = cpu_to_le32(3);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(6);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(6);
    descriptor->ThreadsPerCore = cpu_to_le32(3);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(6);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = cpu_to_le32(257);
    descriptor->SocketCount = cpu_to_le32(257);
    descriptor->NumaNode[0].ProcessorCount = cpu_to_le32(257);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_RX2660);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_RX2660);
    descriptor->ProcessorCount = 0;
    descriptor->SocketCount = 0;
    descriptor->NumaNode[0].ProcessorCount = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_RX2660);
}

static void test_policy_limits(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->PhysicalAddressBits = cpu_to_le32(31);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->MaxSockets = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->MaxPciRoots = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->PciRootIdentity = cpu_to_le32(
        IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_onboard_policy(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    IA64PlatformOnboardDevice *device = &descriptor->OnboardDevice[0];

    descriptor->OnboardDeviceCount = cpu_to_le32(1);
    device->Segment = cpu_to_le16(0);
    device->Bus = 0;
    device->Device = 2;
    device->Function = 0;
    device->Type = IA64_PLATFORM_ONBOARD_GRAPHICS;
    device->Bar = 0;
    device->VendorDeviceId = cpu_to_le32(0x51591002);
    device->ClassCode = cpu_to_le32(0x030000);
    device->BarSize = cpu_to_le64(16 * MiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    device->Bus = 1;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    device->Bus = 0;
    device->Bar = UINT8_MAX;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_numa_policy(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    IA64PlatformNumaNode *node = descriptor->NumaNode;

    descriptor->NumaNodeCount = cpu_to_le32(2);
    node[0].ProcessorCount = cpu_to_le32(1);
    node[0].RamRangeMask = cpu_to_le32(1);
    node[0].Distance[0] = 10;
    node[0].Distance[1] = 20;
    node[1].ProximityDomain = cpu_to_le32(1);
    node[1].ProcessorStart = cpu_to_le32(1);
    node[1].ProcessorCount = cpu_to_le32(1);
    node[1].RamRangeMask = cpu_to_le32(2);
    node[1].Distance[0] = 20;
    node[1].Distance[1] = 10;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    node[1].Distance[0] = 21;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    node[1].Distance[0] = 20;
    node[1].RamRangeMask = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_console_clock(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);

    descriptor->ConsoleClockHz = cpu_to_le32(
        IA64_PLATFORM_UART_MIN_CLOCK_HZ - 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor->ConsoleClockHz = cpu_to_le32(
        IA64_PLATFORM_UART_MIN_CLOCK_HZ);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_array_alignment(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->PciRootOffset = cpu_to_le32(
        le32_to_cpu(descriptor->PciRootOffset) + 4U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_empty_array_zero_encoding(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);

    descriptor->PciRouteOffset = 0;
    descriptor->PciRouteCount = 0;
    /* Empty arrays use an all-zero triple. */
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor->PciRouteEntrySize = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_mapping_alignment(void)
{
    g_assert_true(ia64_platform_desc_mapping_valid(
        IA64_PLATFORM_DESC_ALIGNMENT, sizeof(IA64PlatformDescriptor)));
    g_assert_false(ia64_platform_desc_mapping_valid(
        0, sizeof(IA64PlatformDescriptor)));
    g_assert_false(ia64_platform_desc_mapping_valid(
        IA64_PLATFORM_DESC_ALIGNMENT + 1U,
        sizeof(IA64PlatformDescriptor)));
}

static void test_mapping_efi_rounding_overflow(void)
{
    const hwaddr gpa = UINT64_MAX -
        IA64_PLATFORM_RESOURCE_ALIGNMENT + 1U;
    const hwaddr safe_gpa = gpa - IA64_PLATFORM_DESC_ALIGNMENT;

    g_assert_true(ia64_platform_desc_mapping_valid(
        safe_gpa, sizeof(IA64PlatformDescriptor)));
    g_assert_cmphex(gpa & (IA64_PLATFORM_DESC_ALIGNMENT - 1U), ==, 0);
    g_assert_cmphex(gpa, <=,
                    UINT64_MAX - IA64_PLATFORM_DESC_MAX_SIZE);
    g_assert_false(ia64_platform_desc_mapping_valid(
        gpa, sizeof(IA64PlatformDescriptor)));
}

static void test_mapping_in_declared_ram(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    size_t size = le32_to_cpu(descriptor->TotalSize);

    g_assert_true(ia64_platform_desc_mapping_in_ram(
        descriptor, 0x4000, size));
    g_assert_true(ia64_platform_desc_mapping_in_ram(
        descriptor, 4 * GiB, size));
    g_assert_false(ia64_platform_desc_mapping_in_ram(
        descriptor, 2 * GiB, size));
    g_assert_false(ia64_platform_desc_mapping_in_ram(
        descriptor, 1 * GiB, size));
    g_assert_false(ia64_platform_desc_mapping_in_ram(
        descriptor, 0x4000, size - 1U));
}

static void test_ram_ranges(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformRamRange *ram = (void *)(
        storage.bytes + le32_to_cpu(descriptor->RamRangeOffset));

    descriptor->RamRangeOffset = 0;
    descriptor->RamRangeCount = 0;
    descriptor->RamRangeEntrySize = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->RamRangeCount = cpu_to_le32(
        IA64_PLATFORM_MAX_RAM_RANGES + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->RamRangeOffset = descriptor->PciRootOffset;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->RamRangeEntrySize = cpu_to_le32(
        sizeof(IA64PlatformRamRange) - 8U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[0].Base = cpu_to_le64(IA64_PLATFORM_DESC_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[1].Base = cpu_to_le64(1 * GiB - IA64_PLATFORM_DESC_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[1].Base = cpu_to_le64(4 * GiB + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[1].Size = cpu_to_le64(1 * GiB - IA64_PLATFORM_DESC_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[1].Base = descriptor->NvramBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    ram[1].Base = cpu_to_le64(UINT64_MAX - 0xfffULL);
    ram[1].Size = cpu_to_le64(0x2000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->Reserved1 = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_root_translations(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformPciRoot *root = (void *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));

    root->Mmio32TranslationOffset = cpu_to_le64(0x10000000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32TranslationOffset = cpu_to_le64(0 - 0x10000000ULL);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32TranslationOffset = cpu_to_le64(0 - 0x50000000ULL);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32Base = cpu_to_le64(0x8000);
    root->Mmio32Size = cpu_to_le64(0x2000);
    root->Mmio32TranslationOffset = cpu_to_le64(0 - 0x10000ULL);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32Base = 0;
    root->Mmio32Size = 0;
    root->Mmio64Base = cpu_to_le64(1ULL << 63);
    root->Mmio64Size = cpu_to_le64(0x2000);
    root->Mmio64TranslationOffset = cpu_to_le64(INT64_MAX & ~0x1fffULL);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio64TranslationOffset = cpu_to_le64(0x2000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32TranslationOffset = cpu_to_le64(0x1000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->IoBase = descriptor->LegacyIoBase;
    root->IoSize = cpu_to_le64(0x1000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->IoTranslationOffset = cpu_to_le64(0x2000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_identity_dma_aperture(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformRamRange *ram = (void *)(
        storage.bytes + le32_to_cpu(descriptor->RamRangeOffset));
    IA64PlatformPciRoot *root = (void *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));

    root->Flags = cpu_to_le32(1U << 31);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Flags = cpu_to_le32(
        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
    root->DmaSize = cpu_to_le64(1 * GiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ram = (void *)(storage.bytes +
                   le32_to_cpu(descriptor->RamRangeOffset));
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    ram[1].Base = cpu_to_le64(1 * GiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    root->Flags = cpu_to_le32(
        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Flags = cpu_to_le32(
        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
    root->DmaBase = 0;
    root->DmaSize = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_sparse_io_translation(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    IA64PlatformPciRoot *root;

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Flags = cpu_to_le32(IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO);
    root->IoTranslationOffset = descriptor->LegacyIoBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    root->IoTranslationOffset = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->IoTranslationOffset = descriptor->LegacyIoBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Flags = cpu_to_le32(IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO);
    root->IoBase = 0;
    root->IoSize = 0;
    root->IoTranslationOffset = descriptor->LegacyIoBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Flags = cpu_to_le32(IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO);
    root->IoTranslationOffset = descriptor->LegacyIoBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_u64_overflow(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);

    descriptor->NvramBase = cpu_to_le64(
        UINT64_MAX - (64 * KiB) + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_platform_control(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    const uint64_t control_base = 0xffee0000;

    descriptor->ControlBase = cpu_to_le64(control_base);
    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ResetControlOffset = cpu_to_le32(0);
    descriptor->PoweroffControlOffset = cpu_to_le32(8);
    descriptor->ControlValue = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE / 2U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->ControlBase = cpu_to_le64(control_base);
    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ResetControlOffset = cpu_to_le32(8);
    descriptor->PoweroffControlOffset = cpu_to_le32(8);
    descriptor->ControlValue = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->ControlBase = cpu_to_le64(control_base);
    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ResetControlOffset = cpu_to_le32(0);
    descriptor->PoweroffControlOffset = cpu_to_le32(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ControlValue = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->ControlBase = cpu_to_le64(control_base);
    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ResetControlOffset = cpu_to_le32(0);
    descriptor->PoweroffControlOffset = cpu_to_le32(8);
    descriptor->ControlValue = cpu_to_le32(0x100);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->ControlBase = descriptor->NvramBase;
    descriptor->ControlSize = cpu_to_le64(
        IA64_PLATFORM_MIN_CONTROL_SIZE);
    descriptor->ResetControlOffset = cpu_to_le32(0);
    descriptor->PoweroffControlOffset = cpu_to_le32(8);
    descriptor->ControlValue = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->Reserved3 = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_acpi_pm_resource(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    const uint64_t acpi_pm_base = 0xffed0000;

    descriptor->AcpiPmBase = cpu_to_le64(acpi_pm_base);
    descriptor->AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE);
    descriptor->AcpiSciGsi = cpu_to_le32(5);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->AcpiPmSize = 0;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->AcpiPmBase = cpu_to_le64(acpi_pm_base);
    descriptor->AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE / 2U);
    descriptor->AcpiSciGsi = cpu_to_le32(5);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->AcpiPmBase = cpu_to_le64(acpi_pm_base);
    descriptor->AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE);
    descriptor->AcpiSciGsi = cpu_to_le32(64);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->AcpiPmBase = descriptor->NvramBase;
    descriptor->AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE);
    descriptor->AcpiSciGsi = cpu_to_le32(5);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->AcpiPmBase = cpu_to_le64(acpi_pm_base);
    descriptor->AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE);
    descriptor->AcpiSciGsi = cpu_to_le32(5);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->Reserved4 = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_io_sapic_ids_and_adjacency(void)
{
    IA64PlatformTestDescriptor template_storage;
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *template = test_descriptor_init(
        &template_storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformDescriptor *descriptor;
    IA64PlatformDescriptor header = *template;
    IA64PlatformRamRange ram[2];
    IA64PlatformPciRoot root = *(IA64PlatformPciRoot *)(
        template_storage.bytes + le32_to_cpu(template->PciRootOffset));
    IA64PlatformIoSapic sapics[2];
    IA64PlatformPciRoute route = *(IA64PlatformPciRoute *)(
        template_storage.bytes + le32_to_cpu(template->PciRouteOffset));
    IA64PlatformDescriptorArrays arrays = {
        .pci_roots = &root,
        .pci_root_count = 1,
        .io_sapics = sapics,
        .io_sapic_count = G_N_ELEMENTS(sapics),
        .pci_routes = &route,
        .pci_route_count = 1,
    };
    IA64PlatformIoSapic *built_sapics;
    Error *err = NULL;
    size_t size = 0;

    memcpy(ram, template_storage.bytes +
           le32_to_cpu(template->RamRangeOffset), sizeof(ram));
    arrays.ram_ranges = ram;
    arrays.ram_range_count = G_N_ELEMENTS(ram);
    sapics[0] = *(IA64PlatformIoSapic *)(
        template_storage.bytes + le32_to_cpu(template->IoSapicOffset));
    sapics[0].Id = 3;
    sapics[1] = sapics[0];
    sapics[1].Base = cpu_to_le64(
        le64_to_cpu(sapics[0].Base) + IA64_PLATFORM_IO_SAPIC_SIZE);
    sapics[1].GsiBase = cpu_to_le32(64);
    sapics[1].Id = 12;

    g_assert_true(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_null(err);
    g_assert_true(ia64_platform_desc_validate(
        (IA64PlatformDescriptor *)storage.bytes, size,
        IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_null(err);

    descriptor = (IA64PlatformDescriptor *)storage.bytes;
    built_sapics = (IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    g_assert_cmpuint(built_sapics[0].Id, ==, 3);
    g_assert_cmpuint(built_sapics[1].Id, ==, 12);

    built_sapics[1].Id = built_sapics[0].Id;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    built_sapics[1].Id = IA64_PLATFORM_IO_SAPIC_MAX_ID + 1U;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    built_sapics[1].Id = 12;
    built_sapics[1].Reserved[1] = 1;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_io_sapic_efi_rounding_overflow(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformIoSapic *sapic = (IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    const uint64_t base = UINT64_MAX -
        IA64_PLATFORM_RESOURCE_ALIGNMENT + 1U;

    /* The 4 KiB PID fits, but its 8 KiB EFI-rounded end does not. */
    g_assert_cmphex(base & (IA64_PLATFORM_IO_SAPIC_ALIGNMENT - 1U),
                    ==, 0);
    g_assert_cmphex(base, <=,
                    UINT64_MAX - IA64_PLATFORM_IO_SAPIC_SIZE);
    sapic->Base = cpu_to_le64(base);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_efi_ram_alignment(void)
{
    const uint64_t low_ram_end =
        1 * GiB - IA64_PLATFORM_DESC_ALIGNMENT;
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformRamRange *ram = (void *)(
        storage.bytes + le32_to_cpu(descriptor->RamRangeOffset));

    descriptor->LowRamEnd = cpu_to_le64(low_ram_end);
    descriptor->RamSize = cpu_to_le64(low_ram_end + 1 * GiB);
    ram[0].Size = cpu_to_le64(low_ram_end);

    /* IA-64 EFI pages are 8 KiB; ordinary 4 KiB alignment is insufficient. */
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_build_alias_rejected(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformTestDescriptor before;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    IA64PlatformDescriptor header = *descriptor;
    IA64PlatformRamRange ram[2];
    IA64PlatformPciRoot root = *(IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    IA64PlatformIoSapic sapic = *(IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    IA64PlatformPciRoute route = *(IA64PlatformPciRoute *)(
        storage.bytes + le32_to_cpu(descriptor->PciRouteOffset));
    IA64PlatformDescriptorArrays arrays = {
        .pci_roots = &root,
        .pci_root_count = 1,
        .io_sapics = &sapic,
        .io_sapic_count = 1,
        .pci_routes = &route,
        .pci_route_count = 1,
    };
    const IA64PlatformPciRoot *saved_roots;
    Error *err = NULL;
    size_t size = 0;

    memcpy(ram, storage.bytes + le32_to_cpu(descriptor->RamRangeOffset),
           sizeof(ram));
    arrays.ram_ranges = ram;
    arrays.ram_range_count = G_N_ELEMENTS(ram);
    before = storage;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), descriptor, &arrays, &size, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "alias"));
    g_assert_cmpmem(storage.bytes, sizeof(storage.bytes),
                    before.bytes, sizeof(before.bytes));
    g_assert_cmpuint(size, ==, 0);
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    header = *descriptor;
    arrays.pci_roots = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    before = storage;
    err = NULL;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "alias"));
    g_assert_cmpmem(storage.bytes, sizeof(storage.bytes),
                    before.bytes, sizeof(before.bytes));
    g_assert_cmpuint(size, ==, 0);
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    header = *descriptor;
    arrays.pci_roots = &root;
    saved_roots = arrays.pci_roots;
    before = storage;
    err = NULL;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays,
        (size_t *)&arrays, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "alias"));
    g_assert_cmpmem(storage.bytes, sizeof(storage.bytes),
                    before.bytes, sizeof(before.bytes));
    g_assert_true(arrays.pci_roots == saved_roots);
    error_free(err);
}

static void test_build_combined_max_rejected(void)
{
    IA64PlatformTestDescriptor baseline;
    IA64PlatformTestDescriptor output;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &baseline, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformDescriptor header = *descriptor;
    IA64PlatformRamRange ram[IA64_PLATFORM_MAX_RAM_RANGES] = { 0 };
    IA64PlatformPciRoot roots[IA64_PLATFORM_MAX_PCI_ROOTS] = { 0 };
    IA64PlatformIoSapic sapics[IA64_PLATFORM_MAX_IO_SAPICS] = { 0 };
    IA64PlatformPciRoute routes[IA64_PLATFORM_MAX_PCI_ROUTES] = { 0 };
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = ram,
        .ram_range_count = G_N_ELEMENTS(ram),
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = sapics,
        .io_sapic_count = G_N_ELEMENTS(sapics),
        .pci_routes = routes,
        .pci_route_count = G_N_ELEMENTS(routes),
    };
    Error *err = NULL;
    size_t size = 0;

    /* The per-array maxima are not a promise that all fit simultaneously. */
    g_assert_false(ia64_platform_desc_build(
        &output, sizeof(output), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "too large"));
    error_free(err);
}

static void test_config_backend(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformPciRoot *root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    Error *err = NULL;

    root->ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    root->ConfigBase = cpu_to_le64(TEST_ZX1_CONFIG_BASE);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "does not match"));
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root->Segment = cpu_to_le16(1);
    err = NULL;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "root identity"));
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root->ConfigType = IA64_PLATFORM_PCI_CONFIG_CF8_CFC;
    root->ConfigBase = 0;
    err = NULL;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "does not match"));
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root->ConfigBase = cpu_to_le64(4 * GiB);
    err = NULL;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_nonnull(err);
    error_free(err);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root->ConfigBase = cpu_to_le64(
        (1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS) -
        IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE);
    err = NULL;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_true(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_null(err);

    root->ConfigBase = cpu_to_le64(
        1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_false(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "root identity"));
    error_free(err);
}

static void test_ecam_config_backend(void)
{
    const uint8_t first_bus = 0x20;
    const uint8_t last_bus = 0x2f;
    const uint64_t ecam_base = 0x0000000500000000ULL;
    const uint64_t address_limit =
        1ULL << IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS;
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    IA64PlatformPciRoot *root;
    IA64PlatformIoSapic *sapic;
    IA64PlatformPciRoute *route;

    g_assert_cmphex(ia64_platform_pci_config_size(
                        IA64_PLATFORM_PCI_CONFIG_ECAM,
                        first_bus, last_bus),
                    ==, 16 * MiB);
    g_assert_cmphex(ia64_platform_pci_config_offset(
                        IA64_PLATFORM_PCI_CONFIG_ECAM, first_bus),
                    ==, 32 * MiB);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    sapic = (IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    route = (IA64PlatformPciRoute *)(
        storage.bytes + le32_to_cpu(descriptor->PciRouteOffset));
    descriptor->Flags = cpu_to_le32(
        (le32_to_cpu(descriptor->Flags) &
         ~(IA64_PLATFORM_FLAG_NO_MCFG | IA64_PLATFORM_FLAG_PCI_ZX1_LBA)) |
        IA64_PLATFORM_FLAG_PCI_ECAM);
    root->Segment = cpu_to_le16(7);
    root->Bus = first_bus;
    root->BusEnd = last_bus;
    root->ConfigType = IA64_PLATFORM_PCI_CONFIG_ECAM;
    root->ConfigBase = cpu_to_le64(ecam_base);
    route->Segment = root->Segment;
    route->Bus = root->Bus;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    sapic->Base = cpu_to_le64(ecam_base);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    sapic->Base = cpu_to_le64(
        ecam_base + ia64_platform_pci_config_offset(
            root->ConfigType, root->Bus));
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    sapic->Base = cpu_to_le64(0xfec00000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->Flags = cpu_to_le32(
        le32_to_cpu(descriptor->Flags) | IA64_PLATFORM_FLAG_NO_MCFG);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->Flags = cpu_to_le32(
        le32_to_cpu(descriptor->Flags) & ~IA64_PLATFORM_FLAG_NO_MCFG);
    root->ConfigBase = cpu_to_le64(
        ecam_base + IA64_PLATFORM_PCI_ECAM_BUS_SIZE);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    root->ConfigBase = cpu_to_le64(
        address_limit - IA64_PLATFORM_PCI_ECAM_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->PhysicalAddressBits = cpu_to_le32(44);
    root->ConfigBase = cpu_to_le64(ecam_base);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    root->ConfigBase = cpu_to_le64(1ULL << 44);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor->PhysicalAddressBits = cpu_to_le32(
        IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS);

    root->ConfigBase = cpu_to_le64(
        address_limit);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    root->ConfigBase = cpu_to_le64(
        0x80000000ULL - (uint64_t)first_bus *
        IA64_PLATFORM_PCI_ECAM_BUS_SIZE);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_ZX6000);
    descriptor->Flags = cpu_to_le32(
        le32_to_cpu(descriptor->Flags) & ~IA64_PLATFORM_FLAG_NO_MCFG);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    descriptor->Flags = cpu_to_le32(
        le32_to_cpu(descriptor->Flags) & ~IA64_PLATFORM_FLAG_NO_MCFG);
    root->ConfigType = IA64_PLATFORM_PCI_CONFIG_ECAM;
    root->ConfigBase = cpu_to_le64(ecam_base);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_zx1_embedded_io_sapic(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_ZX6000);
    IA64PlatformPciRoot *root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    IA64PlatformIoSapic *sapic = (IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    uint64_t config_base = le64_to_cpu(root->ConfigBase);
    Error *err = NULL;

    sapic->Base = cpu_to_le64(
        config_base + IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    g_assert_true(ia64_platform_desc_validate(
        descriptor, sizeof(storage), IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_null(err);

    sapic->Base = cpu_to_le64(
        config_base + IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET - 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    sapic->Base = cpu_to_le64(
        config_base + IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);

    /* A normally aligned but non-Mercury overlap stays forbidden. */
    sapic->Base = cpu_to_le64(config_base + 0x1000U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_ZX6000);
}

static void test_build_multi_root(void)
{
    IA64PlatformTestDescriptor template_storage;
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor header = *test_descriptor_init(
        &template_storage, IA64_PLATFORM_ID_HP_ZX6000);
    IA64PlatformRamRange ram[2] = {
        { .Size = cpu_to_le64(1 * GiB) },
        {
            .Base = cpu_to_le64(4 * GiB),
            .Size = cpu_to_le64(1 * GiB),
        },
    };
    IA64PlatformPciRoot roots[2] = { 0 };
    IA64PlatformIoSapic sapics[2] = { 0 };
    IA64PlatformPciRoute routes[2] = { 0 };
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = ram,
        .ram_range_count = G_N_ELEMENTS(ram),
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = sapics,
        .io_sapic_count = G_N_ELEMENTS(sapics),
        .pci_routes = routes,
        .pci_route_count = G_N_ELEMENTS(routes),
    };
    IA64PlatformDescriptor *descriptor = (void *)storage.bytes;
    Error *err = NULL;
    size_t size = 0;

    roots[0].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
    roots[0].ConfigBase = cpu_to_le64(TEST_ZX1_CONFIG_BASE);
    roots[0].IoBase = cpu_to_le64(0x1000);
    roots[0].IoSize = cpu_to_le64(0x1000);
    roots[0].Mmio32Base = cpu_to_le64(0x80000000);
    roots[0].Mmio32Size = cpu_to_le64(0x10000000);
    roots[0].DmaSize = cpu_to_le64(2 * GiB);
    roots[1] = roots[0];
    roots[1].Segment = cpu_to_le16(1);
    roots[1].ConfigBase = cpu_to_le64(
        TEST_ZX1_CONFIG_BASE + IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE);
    roots[1].Rope = cpu_to_le32(1);
    roots[1].IoBase = cpu_to_le64(0x2000);
    roots[1].Mmio32Base = cpu_to_le64(0x90000000);
    sapics[0].Base = cpu_to_le64(0xfec00000);
    sapics[0].RedirectionEntries = cpu_to_le32(64);
    sapics[0].Version = cpu_to_le32(0x21);
    sapics[0].Id = 2;
    sapics[1] = sapics[0];
    sapics[1].Base = cpu_to_le64(0xfec02000);
    sapics[1].GsiBase = cpu_to_le32(64);
    sapics[1].Id = 9;
    routes[0].Device = 1;
    routes[0].Gsi = cpu_to_le32(16);
    routes[1] = routes[0];
    routes[1].Segment = cpu_to_le16(1);
    routes[1].Gsi = cpu_to_le32(72);

    g_assert_true(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_null(err);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==, 2);
    g_assert_cmpuint(size, ==, le32_to_cpu(descriptor->TotalSize));
    g_assert_true(ia64_platform_desc_validate(
        descriptor, size, IA64_PLATFORM_ID_HP_ZX6000, &err));
    g_assert_null(err);

    roots[1].IoBase = roots[0].IoBase;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    roots[1].IoBase = cpu_to_le64(0x2000);

    roots[1].Mmio32TranslationOffset = cpu_to_le64(0 - 0x10000000ULL);
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    roots[1].Mmio32TranslationOffset = 0;

    roots[1].ConfigBase = roots[0].ConfigBase;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    roots[1].ConfigBase = cpu_to_le64(
        TEST_ZX1_CONFIG_BASE + IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE);

    roots[1].Segment = roots[0].Segment;
    g_assert_false(ia64_platform_desc_build(
        &storage, sizeof(storage), &header, &arrays, &size, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_resources_and_routes(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor = test_descriptor_init(
        &storage, IA64_PLATFORM_ID_HP_I2000);
    IA64PlatformPciRoot *root = (void *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    IA64PlatformPciRoute *route = (void *)(
        storage.bytes + le32_to_cpu(descriptor->PciRouteOffset));

    descriptor->NvramSize = cpu_to_le64(4 * KiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->LegacyIoBase = cpu_to_le64(
        le64_to_cpu(descriptor->LegacyIoBase) + 2 * MiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->LegacyIoSize = cpu_to_le64(
        IA64_PLATFORM_MIN_LEGACY_IO_SIZE +
        IA64_PLATFORM_DESC_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->ConsoleIrq = cpu_to_le32(256);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->Flags |= cpu_to_le32(1U << 31);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    route = (void *)(storage.bytes +
                     le32_to_cpu(descriptor->PciRouteOffset));
    root->Bus = 0x20;
    root->BusEnd = 0x3f;
    route->Bus = 0x21;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->PciRootEntrySize =
        cpu_to_le32(sizeof(IA64PlatformPciRoot) + 8);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ((IA64PlatformIoSapic *)(storage.bytes +
        le32_to_cpu(descriptor->IoSapicOffset)))->Base =
        descriptor->NvramBase;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    ((IA64PlatformIoSapic *)(storage.bytes +
        le32_to_cpu(descriptor->IoSapicOffset)))->Base =
        cpu_to_le64(4 * GiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    root = (void *)(storage.bytes +
                    le32_to_cpu(descriptor->PciRootOffset));
    root->Mmio32Base = descriptor->NvramBase;
    root->Mmio32Size = cpu_to_le64(IA64_PLATFORM_RESOURCE_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_descriptor_init(&storage,
                                      IA64_PLATFORM_ID_HP_I2000);
    descriptor->ConsoleBase = cpu_to_le64(
        le64_to_cpu(descriptor->LegacyIoBase) +
        le64_to_cpu(descriptor->LegacyIoSize) - 4);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static IA64PlatformI2000Profile *test_i2000_profile(
    IA64PlatformTestDescriptor *storage,
    const IA64PlatformDescriptor *descriptor)
{
    return (IA64PlatformI2000Profile *)(
        storage->bytes + le32_to_cpu(descriptor->ProfileOffset));
}

static void test_i2000_profile_valid(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    IA64PlatformI2000Profile *profile;
    Error *err = NULL;
    size_t size = 0;

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileEntrySize), ==,
                     sizeof(*profile));
    g_assert_cmpuint(le32_to_cpu(profile->ProfileType), ==,
                     IA64_PLATFORM_PROFILE_TYPE_HP_I2000);
    g_assert_cmpuint(le32_to_cpu(profile->Flags), ==,
                     IA64_I2000_PROFILE_REQUIRED_FLAGS);
    g_assert_true(le32_to_cpu(profile->Flags) &
                  IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT);
    g_assert_false(le32_to_cpu(profile->Flags) &
                   IA64_I2000_PROFILE_FLAG_ACPI_PM_UNAVAILABLE);
    g_assert_false(le32_to_cpu(profile->Flags) &
                   IA64_I2000_PROFILE_FLAG_RESET_UNAVAILABLE);
    g_assert_cmphex(le32_to_cpu(profile->Isp12160Capabilities), ==,
                    ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES);
    g_assert_false(le32_to_cpu(profile->Flags) &
                   IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE);
    g_assert_cmpuint(profile->IdeUnitMask, ==,
                     IA64_I2000_PROFILE_IDE_PRIMARY_MASTER_UNIT_MASK);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramBase), ==,
                    IA64_I2000_PROFILE_NVRAM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramSize), ==,
                    IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmpuint(le64_to_cpu(descriptor->RtcBase), ==, 0);
    g_assert_cmpuint(le64_to_cpu(descriptor->RtcSize), ==, 0);
    g_assert_cmphex(ia64_platform_firmware_compat_flags(
                        IA64_PLATFORM_ID_HP_I2000,
                        le32_to_cpu(descriptor->Flags)), ==,
                    IA64_FW_COMPAT_ALL_MASK);
    g_assert_cmphex(ia64_platform_firmware_compat_flags(
                        IA64_PLATFORM_ID_HP_ZX6000,
                        (le32_to_cpu(descriptor->Flags) &
                         ~IA64_PLATFORM_FLAG_FAMILY_MASK) |
                        IA64_PLATFORM_FLAG_FAMILY_HP_ZX), ==, 0);
    g_assert_true(ia64_platform_desc_validate(
        descriptor, size, IA64_PLATFORM_ID_HP_I2000, &err));
    g_assert_null(err);

    descriptor->Flags |= cpu_to_le32(IA64_PLATFORM_FLAG_IDE_DMA);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

static void test_i2000_runtime_resources(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    size_t size = 0;
    unsigned int i;

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->Flags |= cpu_to_le32(
        IA64_PLATFORM_FLAG_NVRAM_PERSISTENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_valid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    for (i = 0; i < 4; i++) {
        descriptor = test_descriptor_init(&storage,
                                          IA64_PLATFORM_ID_HP_I2000);
        switch (i) {
        case 0:
            descriptor->NvramBase = 0;
            break;
        case 1:
            descriptor->NvramSize = 0;
            break;
        case 2:
            descriptor->RtcBase = 0;
            break;
        case 3:
            descriptor->RtcSize = 0;
            break;
        default:
            g_assert_not_reached();
        }
        ia64_platform_desc_finalize(descriptor, sizeof(storage));
        assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
    }

    for (i = 0; i < 4; i++) {
        descriptor = test_i2000_descriptor_init(&storage, &size);
        switch (i) {
        case 0:
            descriptor->NvramBase = cpu_to_le64(
                IA64_I2000_PROFILE_NVRAM_BASE +
                IA64_PLATFORM_RESOURCE_ALIGNMENT);
            break;
        case 1:
            descriptor->NvramSize = cpu_to_le64(64 * KiB);
            break;
        case 2:
            descriptor->RtcBase = cpu_to_le64(0xffef0000);
            break;
        case 3:
            descriptor->RtcSize = cpu_to_le64(8 * KiB);
            break;
        default:
            g_assert_not_reached();
        }
        ia64_platform_desc_finalize(descriptor, sizeof(storage));
        assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
    }
}

static void test_i2000_profile_rejected(void)
{
    IA64PlatformTestDescriptor storage;
    IA64PlatformDescriptor *descriptor;
    IA64PlatformI2000Profile *profile;
    IA64PlatformPciRoot *root;
    IA64PlatformPciRoute *routes;
    size_t size = 0;

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ProfileCount = cpu_to_le32(2);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ProfileEntrySize = cpu_to_le32(sizeof(*profile) + 8U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ProfileOffset = descriptor->TotalSize;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ProfileOffset = descriptor->PciRootOffset;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->Reserved2 = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->NvramBase = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_BASE);
    descriptor->NvramSize = cpu_to_le64(64 * KiB);
    descriptor->RtcBase = cpu_to_le64(0xffef0000);
    descriptor->RtcSize = cpu_to_le64(8 * KiB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    profile->Flags ^= cpu_to_le32(
        IA64_I2000_PROFILE_FLAG_CONSOLE_POLL_ONLY);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    profile->Flags ^= cpu_to_le32(
        IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    profile->Isp12160Capabilities ^= cpu_to_le32(
        ISP12160_QEMU_I2000_CAPABILITY_A64_IOCB);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    profile->Reserved3 = cpu_to_le32(1);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    profile = test_i2000_profile(&storage, descriptor);
    profile->UartBaseLsbValue ^= 1U;
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->Flags &= cpu_to_le32(~IA64_PLATFORM_FLAG_PS2_PRESENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->Flags &=
        cpu_to_le32(~IA64_PLATFORM_FLAG_FIRMWARE_COMPAT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ConsoleBase = cpu_to_le64(
        le64_to_cpu(descriptor->ConsoleBase) + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    descriptor->ConsoleIrq = cpu_to_le32(4);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    routes = (IA64PlatformPciRoute *)(
        storage.bytes + le32_to_cpu(descriptor->PciRouteOffset));
    routes[1].Gsi = cpu_to_le32(ISP12160_QEMU_I2000_GSI + 1U);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root[1].DmaBase = cpu_to_le64(
        ISP12160_QEMU_I2000_DMA_APERTURE_BASE +
        ISP12160_QEMU_I2000_DMA_ALIGNMENT);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);

    descriptor = test_i2000_descriptor_init(&storage, &size);
    root = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    root->IoBase = cpu_to_le64(0x1000);
    root->IoSize = cpu_to_le64(0x1000);
    ia64_platform_desc_finalize(descriptor, sizeof(storage));
    assert_invalid(descriptor, IA64_PLATFORM_ID_HP_I2000);
}

typedef struct I2000SuperIoWrite {
    unsigned int port;
    uint8_t value;
} I2000SuperIoWrite;

typedef struct I2000SuperIoLog {
    I2000SuperIoWrite writes[32];
    size_t count;
} I2000SuperIoLog;

static void test_i2000_superio_write(void *opaque,
                                        unsigned int logical_port,
                                        unsigned char value)
{
    I2000SuperIoLog *log = opaque;

    g_assert_cmpuint(log->count, <, G_N_ELEMENTS(log->writes));
    log->writes[log->count].port = logical_port;
    log->writes[log->count].value = value;
    log->count++;
}

static void test_i2000_superio_sequence(void)
{
    static const I2000SuperIoWrite expected[] = {
        { 0x2e, 0x55 },
        { 0x2e, 0x07 }, { 0x2f, 0x04 },
        { 0x2e, 0x60 }, { 0x2f, 0x03 },
        { 0x2e, 0x61 }, { 0x2f, 0xf8 },
        { 0x2e, 0x70 }, { 0x2f, 0x04 },
        { 0x2e, 0xf0 }, { 0x2f, 0x00 },
        { 0x2e, 0x30 }, { 0x2f, 0x01 },
        { 0x2e, 0x07 }, { 0x2f, 0x07 },
        { 0x2e, 0x70 }, { 0x2f, 0x01 },
        { 0x2e, 0x72 }, { 0x2f, 0x0c },
        { 0x2e, 0x30 }, { 0x2f, 0x01 },
        { 0x2e, 0xaa },
    };
    IA64PlatformI2000Profile profile;
    I2000SuperIoLog log = { 0 };

    ia64_platform_i2000_profile_init(&profile);
    ia64_i2000_profile_superio_emit(&profile,
                               test_i2000_superio_write, &log);
    g_assert_cmpuint(log.count, ==, G_N_ELEMENTS(expected));
    g_assert_cmpmem(log.writes, log.count * sizeof(log.writes[0]),
                    expected, sizeof(expected));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ia64/platform/valid", test_valid);
    g_test_add_func("/ia64/platform/console-gsi-zero",
                    test_console_gsi_zero);
    g_test_add_func("/ia64/platform/legacy-io-pal",
                    test_legacy_io_pal_contract);
    g_test_add_func("/ia64/platform/revision-and-id",
                    test_revision_and_platform);
    g_test_add_func("/ia64/platform/range-and-overlap",
                    test_range_and_overlap);
    g_test_add_func("/ia64/platform/checksum", test_checksum);
    g_test_add_func("/ia64/platform/topology", test_topology);
    g_test_add_func("/ia64/platform/policy-limits", test_policy_limits);
    g_test_add_func("/ia64/platform/onboard-policy", test_onboard_policy);
    g_test_add_func("/ia64/platform/numa-policy", test_numa_policy);
    g_test_add_func("/ia64/platform/console-clock", test_console_clock);
    g_test_add_func("/ia64/platform/array-alignment",
                    test_array_alignment);
    g_test_add_func("/ia64/platform/empty-array-zero-encoding",
                    test_empty_array_zero_encoding);
    g_test_add_func("/ia64/platform/mapping-alignment",
                    test_mapping_alignment);
    g_test_add_func("/ia64/platform/mapping-efi-rounding-overflow",
                    test_mapping_efi_rounding_overflow);
    g_test_add_func("/ia64/platform/mapping-in-declared-ram",
                    test_mapping_in_declared_ram);
    g_test_add_func("/ia64/platform/ram-ranges", test_ram_ranges);
    g_test_add_func("/ia64/platform/root-translations",
                    test_root_translations);
    g_test_add_func("/ia64/platform/sparse-io-translation",
                    test_sparse_io_translation);
    g_test_add_func("/ia64/platform/identity-dma-aperture",
                    test_identity_dma_aperture);
    g_test_add_func("/ia64/platform/u64-overflow", test_u64_overflow);
    g_test_add_func("/ia64/platform/control", test_platform_control);
    g_test_add_func("/ia64/platform/acpi-pm", test_acpi_pm_resource);
    g_test_add_func("/ia64/platform/io-sapic-ids-and-adjacency",
                    test_io_sapic_ids_and_adjacency);
    g_test_add_func("/ia64/platform/io-sapic-efi-rounding-overflow",
                    test_io_sapic_efi_rounding_overflow);
    g_test_add_func("/ia64/platform/efi-ram-alignment",
                    test_efi_ram_alignment);
    g_test_add_func("/ia64/platform/build-alias-rejected",
                    test_build_alias_rejected);
    g_test_add_func("/ia64/platform/build-combined-max-rejected",
                    test_build_combined_max_rejected);
    g_test_add_func("/ia64/platform/config-backend",
                    test_config_backend);
    g_test_add_func("/ia64/platform/ecam-config-backend",
                    test_ecam_config_backend);
    g_test_add_func("/ia64/platform/zx1-embedded-io-sapic",
                    test_zx1_embedded_io_sapic);
    g_test_add_func("/ia64/platform/build-multi-root",
                    test_build_multi_root);
    g_test_add_func("/ia64/platform/resources-and-routes",
                    test_resources_and_routes);
    g_test_add_func("/ia64/platform/i2000-profile-valid",
                    test_i2000_profile_valid);
    g_test_add_func("/ia64/platform/i2000-runtime-resources",
                    test_i2000_runtime_resources);
    g_test_add_func("/ia64/platform/i2000-profile-rejected",
                    test_i2000_profile_rejected);
    g_test_add_func("/ia64/platform/i2000-superio-sequence",
                    test_i2000_superio_sequence);
    return g_test_run();
}
