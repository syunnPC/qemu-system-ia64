/*
 * IA-64 i2000 EFI test machine qtest
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <glib/gstdio.h>

#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/ia64/ia64_i2000_efi_test.h"
#include "hw/ia64/ia64_i2000_io_test.h"
#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/pci/pci.h"
#include "hw/scsi/isp12160_abi.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define TEST_FIRMWARE_ENV "QTEST_IA64_FIRMWARE"
#define TEST_CD_SIZE      (32 * KiB)

typedef union TestDescriptorStorage {
    uint64_t alignment;
    uint8_t bytes[IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE];
} TestDescriptorStorage;

static const uint8_t root_first_bus[] = { 0x00, 0x01, 0x02 };
static const uint8_t root_last_bus[] = { 0x00, 0x01, 0x02 };
static const uint64_t root_io_base[] = { 0x0000, 0x4000, 0x8000 };
static const uint64_t root_mmio_base[] = {
    UINT64_C(0x90000000),
    UINT64_C(0xa0000000),
    UINT64_C(0xb0000000),
};

G_STATIC_ASSERT(sizeof(IA64PlatformDescriptor) == 1112);
G_STATIC_ASSERT(sizeof(IA64PlatformRamRange) == 16);
G_STATIC_ASSERT(sizeof(IA64PlatformPciRoot) == 112);
G_STATIC_ASSERT(sizeof(IA64PlatformIoSapic) == 24);
G_STATIC_ASSERT(sizeof(IA64PlatformPciRoute) == 16);
G_STATIC_ASSERT(sizeof(IA64PlatformI2000Profile) == 88);
G_STATIC_ASSERT(offsetof(IA64PlatformI2000Profile,
                         Isp12160Capabilities) == 80);
G_STATIC_ASSERT(ISP12160_PCI_MMIO_BAR == 1);
G_STATIC_ASSERT(sizeof(TestDescriptorStorage) ==
                IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE);
G_STATIC_ASSERT(G_N_ELEMENTS(root_first_bus) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(root_last_bus) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(root_io_base) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(root_mmio_base) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);

static uint64_t sparse_io_pa(uint16_t port)
{
    return IA64_I2000_460GX_TEST_LEGACY_IO_BASE +
        ((uint64_t)(port >> 2) << 12) + (port & 0xfffU);
}

static uint8_t descriptor_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

static uint16_t pci_config_readw(QTestState *qts, uint8_t bus,
                                 unsigned int device,
                                 unsigned int function,
                                 unsigned int reg)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        PCI_DEVFN(device, function) << 8 | (reg & 0xfc);

    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA, address);
    return qtest_readw(qts,
                       IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint8_t pci_config_readb(QTestState *qts, uint8_t bus,
                                unsigned int device,
                                unsigned int function,
                                unsigned int reg)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        PCI_DEVFN(device, function) << 8 | (reg & 0xfc);

    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA, address);
    return qtest_readb(qts,
                       IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint32_t pci_config_readl(QTestState *qts, uint8_t bus,
                                 unsigned int device,
                                 unsigned int function,
                                 unsigned int reg)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        PCI_DEVFN(device, function) << 8 | (reg & 0xfc);

    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA, address);
    return qtest_readl(qts, IA64_I2000_460GX_TEST_CFC_PA);
}

static void pci_config_writel(QTestState *qts, uint8_t bus,
                              unsigned int device,
                              unsigned int function,
                              unsigned int reg, uint32_t value)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        PCI_DEVFN(device, function) << 8 | (reg & 0xfc);

    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA, address);
    qtest_writel(qts, IA64_I2000_460GX_TEST_CFC_PA, value);
}

static void assert_qom_child(QTestState *qts, const char *parent_path,
                             const char *name, const char *type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':{'path':%s}}",
        parent_path);
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);
    QList *children;
    QListEntry *entry;

    g_assert_true(qdict_haskey(response, "return"));
    children = qdict_get_qlist(response, "return");
    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "name"), name)) {
            g_assert_cmpstr(qdict_get_str(child, "type"), ==, child_type);
            return;
        }
    }
    g_error("QOM child %s/%s was not found", parent_path, name);
}

static void assert_topology_ownership(QTestState *qts)
{
    assert_qom_child(qts, "/machine",
                     IA64_I2000_EFI_TEST_TOPOLOGY_CHILD,
                     TYPE_IA64_I2000_EFI_TEST_TOPOLOGY);
    assert_qom_child(qts, IA64_I2000_EFI_TEST_TOPOLOGY_QOM_PATH,
                     IA64_I2000_EFI_TEST_460GX_TEST_CHILD,
                     TYPE_IA64_I2000_460GX_TEST);
    assert_qom_child(qts, IA64_I2000_EFI_TEST_TOPOLOGY_QOM_PATH,
                     IA64_I2000_EFI_TEST_IO_TEST_CHILD,
                     TYPE_IA64_I2000_IO_TEST);
}

static void assert_descriptor(QTestState *qts, unsigned int cpu_count)
{
    TestDescriptorStorage storage;
    const IA64PlatformDescriptor *descriptor =
        (const IA64PlatformDescriptor *)storage.bytes;
    const IA64PlatformRamRange *ram;
    const IA64PlatformPciRoot *roots;
    const IA64PlatformIoSapic *sapic;
    const IA64PlatformPciRoute *routes;
    const IA64PlatformI2000Profile *profile;
    const uint32_t ram_offset = sizeof(*descriptor);
    const uint32_t root_offset =
        ram_offset + sizeof(IA64PlatformRamRange);
    const uint32_t sapic_offset = root_offset +
        IA64_I2000_460GX_TEST_ROOT_COUNT *
            sizeof(IA64PlatformPciRoot);
    const uint32_t route_offset =
        sapic_offset + sizeof(IA64PlatformIoSapic);
    const uint32_t profile_offset =
        route_offset + 2 * sizeof(IA64PlatformPciRoute);
    unsigned int i;

    qtest_memread(qts, IA64_I2000_EFI_TEST_DESCRIPTOR_GPA,
                  storage.bytes, sizeof(storage.bytes));
    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->FormatRevision), ==,
                     IA64_PLATFORM_DESC_REVISION);
    g_assert_cmpuint(le32_to_cpu(descriptor->HeaderSize), ==,
                     sizeof(*descriptor));
    g_assert_cmpuint(le32_to_cpu(descriptor->TotalSize), ==,
                     IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_I2000);
    g_assert_cmphex(le32_to_cpu(descriptor->Flags), ==,
                    IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS);
    g_assert_cmpuint(descriptor_checksum(storage.bytes,
                                         sizeof(storage.bytes)), ==, 0);

    g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==, 2 * GiB);
    g_assert_cmphex(le64_to_cpu(descriptor->LowRamEnd), ==, 2 * GiB);
    g_assert_cmphex(le64_to_cpu(descriptor->FirmwareBase), ==,
                    IA64_I2000_460GX_TEST_FIRMWARE_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->FirmwareSize), ==,
                    IA64_I2000_460GX_TEST_FIRMWARE_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProcessorCount), ==, cpu_count);
    g_assert_cmpuint(le32_to_cpu(descriptor->SocketCount), ==, cpu_count);
    g_assert_cmpuint(le32_to_cpu(descriptor->CoresPerSocket), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ThreadsPerCore), ==, 1);
    g_assert_cmphex(le64_to_cpu(descriptor->LegacyIoBase), ==,
                    IA64_I2000_460GX_TEST_LEGACY_IO_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->LegacyIoSize), ==,
                    IA64_I2000_460GX_TEST_LEGACY_IO_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->LocalSapicBase), ==,
                    IA64_I2000_460GX_TEST_PIB_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->LocalSapicSize), ==,
                    IA64_I2000_460GX_TEST_PIB_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->ConsoleBase), ==,
                    sparse_io_pa(IA64_I2000_IO_TEST_UART_BASE));
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleRegisterStride), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleClockHz), ==,
                     IA64_I2000_IO_TEST_UART_INPUT_CLOCK_HZ);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleIrq), ==, 0);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramBase), ==,
                    IA64_I2000_PROFILE_NVRAM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramSize), ==,
                    IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->RtcBase), ==, 0);
    g_assert_cmphex(le64_to_cpu(descriptor->RtcSize), ==, 0);

    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeOffset), ==,
                     ram_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeEntrySize), ==,
                     sizeof(IA64PlatformRamRange));
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootOffset), ==,
                     root_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     IA64_I2000_460GX_TEST_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootEntrySize), ==,
                     sizeof(IA64PlatformPciRoot));
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicOffset), ==,
                     sapic_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicEntrySize), ==,
                     sizeof(IA64PlatformIoSapic));
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteOffset), ==,
                     route_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteEntrySize), ==,
                     sizeof(IA64PlatformPciRoute));
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileOffset), ==,
                     profile_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileEntrySize), ==,
                     sizeof(IA64PlatformI2000Profile));

    ram = (const IA64PlatformRamRange *)(storage.bytes + ram_offset);
    g_assert_cmphex(le64_to_cpu(ram->Base), ==, 0);
    g_assert_cmphex(le64_to_cpu(ram->Size), ==, 2 * GiB);

    roots = (const IA64PlatformPciRoot *)(storage.bytes + root_offset);
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        g_assert_cmpuint(le16_to_cpu(roots[i].Segment), ==, 0);
        g_assert_cmpuint(roots[i].Bus, ==, root_first_bus[i]);
        g_assert_cmpuint(roots[i].BusEnd, ==, root_last_bus[i]);
        g_assert_cmpuint(roots[i].ConfigType, ==,
                         IA64_PLATFORM_PCI_CONFIG_CF8_CFC);
        g_assert_cmphex(le32_to_cpu(roots[i].Flags), ==,
                        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
        g_assert_cmphex(le64_to_cpu(roots[i].ConfigBase), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].IoBase), ==, root_io_base[i]);
        g_assert_cmphex(le64_to_cpu(roots[i].IoSize), ==, 0x4000);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32Base), ==,
                        root_mmio_base[i]);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32Size), ==,
                        UINT64_C(0x10000000));
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64Base), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64Size), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].DmaBase), ==,
                        IA64_I2000_460GX_TEST_DMA_BASE);
        g_assert_cmphex(le64_to_cpu(roots[i].DmaSize), ==,
                        IA64_I2000_460GX_TEST_DMA_SIZE);
        g_assert_cmpuint(le32_to_cpu(roots[i].Rope), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].IoTranslationOffset), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32TranslationOffset), ==,
                        0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64TranslationOffset), ==,
                        0);
    }

    sapic = (const IA64PlatformIoSapic *)(storage.bytes + sapic_offset);
    g_assert_cmphex(le64_to_cpu(sapic->Base), ==,
                    IA64_I2000_460GX_TEST_PID_BASE);
    g_assert_cmpuint(le32_to_cpu(sapic->GsiBase), ==, 0);
    g_assert_cmpuint(le32_to_cpu(sapic->RedirectionEntries), ==, 64);
    g_assert_cmpuint(le32_to_cpu(sapic->Version), ==, 0x21);
    g_assert_cmpuint(sapic->Id, ==, 0);

    routes = (const IA64PlatformPciRoute *)(storage.bytes + route_offset);
    g_assert_cmpuint(le16_to_cpu(routes[0].Segment), ==, 0);
    g_assert_cmpuint(routes[0].Bus, ==, 0);
    g_assert_cmpuint(routes[0].Device, ==,
                     IA64_I2000_IO_TEST_I82559_SLOT);
    g_assert_cmpuint(routes[0].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[0].Gsi), ==,
                     IA64_I2000_IO_TEST_I82559_PID_PIN);
    g_assert_cmphex(le32_to_cpu(routes[0].Flags), ==, 0);
    g_assert_cmpuint(le16_to_cpu(routes[1].Segment), ==,
                     ISP12160_QEMU_I2000_SEGMENT);
    g_assert_cmpuint(routes[1].Bus, ==, ISP12160_QEMU_I2000_BUS);
    g_assert_cmpuint(routes[1].Device, ==, ISP12160_QEMU_I2000_DEVICE);
    g_assert_cmpuint(routes[1].Pin, ==,
                     ISP12160_QEMU_I2000_INTERRUPT_PIN - 1U);
    g_assert_cmpuint(le32_to_cpu(routes[1].Gsi), ==,
                     ISP12160_QEMU_I2000_GSI);
    g_assert_cmphex(le32_to_cpu(routes[1].Flags), ==, 0);

    profile = (const IA64PlatformI2000Profile *)(
        storage.bytes + profile_offset);
    g_assert_cmpuint(le32_to_cpu(profile->ProfileType), ==,
                     IA64_PLATFORM_PROFILE_TYPE_HP_I2000);
    g_assert_cmpuint(le32_to_cpu(profile->ProfileRevision), ==,
                     IA64_PLATFORM_I2000_PROFILE_REVISION);
    g_assert_cmpuint(le32_to_cpu(profile->Length), ==, sizeof(*profile));
    g_assert_cmphex(le32_to_cpu(profile->Flags), ==,
                    IA64_I2000_PROFILE_REQUIRED_FLAGS);
    g_assert_true(le32_to_cpu(profile->Flags) &
                  IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT);
    g_assert_cmphex(le32_to_cpu(profile->Isp12160Capabilities), ==,
                    ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES);
    g_assert_cmpuint(le16_to_cpu(profile->UartPort), ==,
                     IA64_I2000_PROFILE_UART_PORT);
    g_assert_cmpuint(profile->UartIrq, ==, IA64_I2000_PROFILE_UART_IRQ);
    g_assert_cmpuint(le16_to_cpu(profile->IdeSegment), ==,
                     IA64_I2000_PROFILE_IDE_SEGMENT);
    g_assert_cmpuint(profile->IdeBus, ==, IA64_I2000_PROFILE_IDE_BUS);
    g_assert_cmpuint(profile->IdeDevice, ==, IA64_I2000_PROFILE_IDE_DEVICE);
    g_assert_cmpuint(profile->IdeFunction, ==,
                     IA64_I2000_PROFILE_IDE_FUNCTION);
    g_assert_cmpuint(profile->IdeUnitMask, ==,
                     IA64_I2000_PROFILE_IDE_PRIMARY_MASTER_UNIT_MASK);

    g_assert_true(qtest_qom_get_bool(
        qts, IA64_I2000_EFI_TEST_460GX_TEST_QOM_PATH,
        IA64_I2000_460GX_TEST_DESCRIPTOR_INSTALLED));
}

static void assert_firmware_loaded(QTestState *qts,
                                   const char *firmware_path)
{
    g_autofree char *expected = NULL;
    g_autofree uint8_t *actual = NULL;
    g_autoptr(GError) err = NULL;
    gsize size;

    g_assert_true(g_file_get_contents(firmware_path, &expected, &size, &err));
    g_assert_no_error(err);
    g_assert_cmpuint(size, >, 0);
    g_assert_cmpuint(size, <=, IA64_I2000_460GX_TEST_FIRMWARE_SIZE);
    actual = g_malloc(size);
    qtest_memread(qts, IA64_I2000_460GX_TEST_FIRMWARE_BASE, actual, size);
    g_assert_cmpmem(actual, size, expected, size);
}

static uint64_t parse_hex_field(const char *registers, const char *field)
{
    const char *value = strstr(registers, field);

    g_assert_nonnull(value);
    value += strlen(field);
    return g_ascii_strtoull(value, NULL, 16);
}

static uint64_t parse_gr(const char *registers, unsigned int reg)
{
    g_autofree char *field = g_strdup_printf("r%-3u 0x", reg);

    return parse_hex_field(registers, field);
}

static char *cpu_registers(QTestState *qts, unsigned int cpu_index)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'human-monitor-command',"
        "'arguments':{'command-line':'info registers',"
        "'cpu-index':%u}}", cpu_index);
    const char *registers;

    g_assert_true(qdict_haskey(response, "return"));
    registers = qdict_get_try_str(response, "return");
    g_assert_nonnull(registers);
    return g_strdup(registers);
}

static bool cpu_start_powered_off(QTestState *qts,
                                  unsigned int cpu_index)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-cpus-fast'}");
    QList *cpus;
    QListEntry *entry;

    g_assert_true(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    QLIST_FOREACH_ENTRY(cpus, entry) {
        QDict *cpu = qobject_to(QDict, qlist_entry_obj(entry));

        if (qdict_get_int(cpu, "cpu-index") == cpu_index) {
            return qtest_qom_get_bool(qts, qdict_get_str(cpu, "qom-path"),
                                      "start-powered-off");
        }
    }
    g_error("CPU %u was not found", cpu_index);
    return false;
}

static void assert_cpu_handoff(QTestState *qts, unsigned int cpu_index,
                               bool powered_off)
{
    g_autofree char *registers = cpu_registers(qts, cpu_index);
    const char *halted;

    g_assert_cmpint(cpu_start_powered_off(qts, cpu_index), ==,
                    powered_off);
    g_assert_cmphex(parse_hex_field(registers, "IP: 0x"), ==,
                    IA64_I2000_460GX_TEST_FIRMWARE_BASE);
    g_assert_cmphex(parse_gr(registers, 8), ==,
                    IA64_I2000_EFI_TEST_DESCRIPTOR_GPA);
    g_assert_cmphex(parse_gr(registers, 9), ==,
                    IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE);
    g_assert_cmphex(parse_gr(registers, 10), ==,
                    IA64_PLATFORM_ID_HP_I2000);
    if (cpu_index == 0) {
        g_assert_cmphex(parse_gr(registers, 12), ==,
                        IA64_I2000_460GX_TEST_RAM_SIZE - 16);
    }
    halted = strstr(registers, "HALTED: ");
    g_assert_nonnull(halted);
    g_assert_cmpuint(g_ascii_strtoull(halted + strlen("HALTED: "),
                                     NULL, 10), ==, powered_off);
}

static void assert_io_test_pci_functions(QTestState *qts)
{
    uint32_t isp_bar;
    uint32_t isp_bar_mask;

    g_assert_cmphex(pci_config_readw(
                        qts, 0, IA64_I2000_IO_TEST_PCI_SLOT,
                        IA64_I2000_IO_TEST_F0_FUNCTION,
                        PCI_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_F0_VENDOR_ID);
    g_assert_cmphex(pci_config_readw(
                        qts, 0, IA64_I2000_IO_TEST_PCI_SLOT,
                        IA64_I2000_IO_TEST_F1_FUNCTION,
                        PCI_DEVICE_ID), ==,
                    IA64_I2000_IO_TEST_F1_DEVICE_ID);
    g_assert_cmphex(pci_config_readw(
                        qts, 0, IA64_I2000_IO_TEST_I82559_SLOT,
                        IA64_I2000_IO_TEST_I82559_FUNCTION,
                        PCI_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_I82559_VENDOR_ID);

    g_assert_cmphex(pci_config_readw(
                        qts, ISP12160_QEMU_I2000_BUS,
                        ISP12160_QEMU_I2000_DEVICE,
                        ISP12160_QEMU_I2000_FUNCTION,
                        PCI_VENDOR_ID), ==, ISP12160_PCI_VENDOR_ID);
    g_assert_cmphex(pci_config_readw(
                        qts, ISP12160_QEMU_I2000_BUS,
                        ISP12160_QEMU_I2000_DEVICE,
                        ISP12160_QEMU_I2000_FUNCTION,
                        PCI_DEVICE_ID), ==, ISP12160_PCI_DEVICE_ID);
    g_assert_cmphex(pci_config_readw(
                        qts, ISP12160_QEMU_I2000_BUS,
                        ISP12160_QEMU_I2000_DEVICE,
                        ISP12160_QEMU_I2000_FUNCTION,
                        PCI_CLASS_DEVICE), ==, ISP12160_PCI_CLASS);
    g_assert_cmpuint(pci_config_readb(
                         qts, ISP12160_QEMU_I2000_BUS,
                         ISP12160_QEMU_I2000_DEVICE,
                         ISP12160_QEMU_I2000_FUNCTION,
                         PCI_INTERRUPT_PIN), ==,
                     ISP12160_QEMU_I2000_INTERRUPT_PIN);

    isp_bar = pci_config_readl(
        qts, ISP12160_QEMU_I2000_BUS, ISP12160_QEMU_I2000_DEVICE,
        ISP12160_QEMU_I2000_FUNCTION,
        PCI_BASE_ADDRESS_0 + ISP12160_PCI_MMIO_BAR * 4U);
    g_assert_cmphex(isp_bar, ==, ISP12160_QEMU_I2000_BAR_ADDRESS);
    pci_config_writel(
        qts, ISP12160_QEMU_I2000_BUS, ISP12160_QEMU_I2000_DEVICE,
        ISP12160_QEMU_I2000_FUNCTION,
        PCI_BASE_ADDRESS_0 + ISP12160_PCI_MMIO_BAR * 4U, UINT32_MAX);
    isp_bar_mask = pci_config_readl(
        qts, ISP12160_QEMU_I2000_BUS, ISP12160_QEMU_I2000_DEVICE,
        ISP12160_QEMU_I2000_FUNCTION,
        PCI_BASE_ADDRESS_0 + ISP12160_PCI_MMIO_BAR * 4U);
    g_assert_cmphex(isp_bar_mask & PCI_BASE_ADDRESS_MEM_MASK, ==,
                    ~(ISP12160_QEMU_I2000_BAR_SIZE - 1U));
    pci_config_writel(
        qts, ISP12160_QEMU_I2000_BUS, ISP12160_QEMU_I2000_DEVICE,
        ISP12160_QEMU_I2000_FUNCTION,
        PCI_BASE_ADDRESS_0 + ISP12160_PCI_MMIO_BAR * 4U, isp_bar);
}

static void test_machine_configuration(void)
{
    const char *firmware_path = g_getenv(TEST_FIRMWARE_ENV);
    g_autofree char *quoted_firmware = NULL;
    g_autofree char *cd_path = NULL;
    g_autofree char *quoted_cd = NULL;
    g_autofree char *cd_contents = g_malloc0(TEST_CD_SIZE);
    g_autoptr(GError) err = NULL;
    g_autoptr(QDict) response = NULL;
    QList *cpus;
    QTestState *qts;
    int fd;

    g_assert_nonnull(firmware_path);
    quoted_firmware = g_shell_quote(firmware_path);
    fd = g_file_open_tmp("qemu-i2000-io-test-cd-XXXXXX.img", &cd_path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(cd_path, cd_contents,
                                      TEST_CD_SIZE, &err));
    g_assert_no_error(err);
    quoted_cd = g_shell_quote(cd_path);

    qts = qtest_initf(
        "-machine x-ia64-i2000-efi-test -S "
        "-smp 2,sockets=2,cores=1,threads=1 -m 2G -nodefaults "
        "-display none -net none -serial null -bios %s "
        "-drive if=ide,index=0,media=cdrom,readonly=on,format=raw,file=%s",
        quoted_firmware, quoted_cd);

    response = qtest_qmp(qts, "{'execute':'query-cpus-fast'}");
    g_assert_true(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    g_assert_cmpuint(qlist_size(cpus), ==, 2);
    assert_firmware_loaded(qts, firmware_path);
    assert_descriptor(qts, 2);
    assert_topology_ownership(qts);
    assert_io_test_pci_functions(qts);
    assert_cpu_handoff(qts, 0, false);
    assert_cpu_handoff(qts, 1, true);

    qtest_system_reset(qts);
    assert_descriptor(qts, 2);
    assert_io_test_pci_functions(qts);
    assert_cpu_handoff(qts, 0, false);
    assert_cpu_handoff(qts, 1, true);

    qtest_quit(qts);
    g_assert_cmpint(g_remove(cd_path), ==, 0);
}

static void test_one_cpu(void)
{
    QTestState *qts = qtest_init(
        "-machine x-ia64-i2000-efi-test -S -smp 1 -m 2G "
        "-nodefaults -display none -net none -serial none");
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-cpus-fast'}");
    QList *cpus;

    g_assert_true(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    g_assert_cmpuint(qlist_size(cpus), ==, 1);
    assert_descriptor(qts, 1);
    assert_topology_ownership(qts);
    assert_cpu_handoff(qts, 0, false);
    qtest_quit(qts);
}

static void assert_start_rejected(const char *options)
{
    g_autofree char *args = g_strdup_printf(
        "-machine x-ia64-i2000-efi-test -S -nodefaults "
        "-display none -net none -serial none %s", options);
    QTestState *qts = qtest_init_ext(NULL, args, NULL, false);

    qtest_set_expected_status(qts, EXIT_FAILURE);
    qtest_wait_qemu(qts);
    qtest_quit(qts);
}

static void test_reject_invalid_ram(void)
{
    assert_start_rejected("-smp 1 -m 1G");
}

static void test_reject_invalid_cpu(void)
{
    assert_start_rejected("-smp 1 -m 2G -cpu montecito");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("i2000-efi/machine-configuration",
                   test_machine_configuration);
    qtest_add_func("i2000-efi/one-cpu", test_one_cpu);
    qtest_add_func("i2000-efi/reject-invalid-ram",
                   test_reject_invalid_ram);
    qtest_add_func("i2000-efi/reject-invalid-cpu",
                   test_reject_invalid_cpu);
    return g_test_run();
}
