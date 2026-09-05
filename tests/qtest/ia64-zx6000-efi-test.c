/*
 * IA-64 zx6000 EFI integration machine qtest
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_platform.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/ia64/ia64_zx6000_zx1_test.h"
#include "hw/ia64/ia64_zx6000_zx1_test_layout.h"
#include "hw/ia64/ia64_zx6000_efi_test.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-io-sapic.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define TEST_FIRMWARE_ENV "QTEST_IA64_FIRMWARE"
#define ZX6000_EFI_TEST_UART_INPUT_CLOCK_HZ UINT32_C(1843200)
#define ZX6000_EFI_TEST_IO_SAPIC_VERSION    UINT32_C(0x000a0020)

#define ZX6000_EFI_TEST_ROOT0_BAR           UINT32_C(0x00100000)
#define ZX6000_EFI_TEST_ROOT1_BAR           UINT32_C(0x00200000)
#define ZX6000_EFI_TEST_ROOT0_VECTOR        UINT32_C(0x51)
#define ZX6000_EFI_TEST_ROOT1_VECTOR        UINT32_C(0x62)
#define ZX6000_EFI_TEST_RSDP_BASE           UINT64_C(0x00802000)
#define ZX6000_EFI_TEST_ECAM0_BASE          UINT64_C(0x0000000500000000)
#define ZX6000_EFI_TEST_ECAM1_BASE          UINT64_C(0x0000000600000000)
#define ZX6000_EFI_TEST_MCFG_SIZE           76U

typedef union TestDescriptorStorage {
    uint64_t alignment;
    uint8_t bytes[IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE];
} TestDescriptorStorage;

static const uint64_t zx6000_efi_config_base[] = {
    IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE,
    IA64_ZX6000_ZX1_TEST_IOA1_CSR_BASE,
};

static const uint64_t zx6000_efi_cpu_mmio_base[] = {
    IA64_ZX6000_ZX1_TEST_ROOT0_CPU_MMIO_BASE,
    IA64_ZX6000_ZX1_TEST_ROOT1_CPU_MMIO_BASE,
};

static const uint8_t zx6000_efi_first_bus[] = {
    IA64_ZX6000_ZX1_TEST_ROOT0_FIRST_BUS,
    IA64_ZX6000_ZX1_TEST_ROOT1_FIRST_BUS,
};

static const uint8_t zx6000_efi_last_bus[] = {
    IA64_ZX6000_ZX1_TEST_ROOT0_LAST_BUS,
    IA64_ZX6000_ZX1_TEST_ROOT1_LAST_BUS,
};

static const uint32_t zx6000_efi_rope[] = { 0, 2 };

static const uint32_t zx6000_efi_gsi_base[] = {
    IA64_ZX6000_EFI_TEST_ROOT0_GSI_BASE,
    IA64_ZX6000_EFI_TEST_ROOT1_GSI_BASE,
};

G_STATIC_ASSERT(sizeof(IA64PlatformDescriptor) == 1112);
G_STATIC_ASSERT(sizeof(IA64PlatformRamRange) == 16);
G_STATIC_ASSERT(sizeof(IA64PlatformPciRoot) == 112);
G_STATIC_ASSERT(sizeof(IA64PlatformIoSapic) == 24);
G_STATIC_ASSERT(sizeof(IA64PlatformPciRoute) == 16);
G_STATIC_ASSERT(sizeof(TestDescriptorStorage) ==
                IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_config_base) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_cpu_mmio_base) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_first_bus) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_last_bus) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_rope) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_gsi_base) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);

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
                     IA64_ZX6000_EFI_TEST_TOPOLOGY_CHILD,
                     TYPE_IA64_ZX6000_EFI_TEST_TOPOLOGY);
    assert_qom_child(qts, IA64_ZX6000_EFI_TEST_TOPOLOGY_QOM_PATH,
                     IA64_ZX6000_EFI_TEST_FIXTURE_CHILD,
                     TYPE_IA64_ZX6000_ZX1_TEST);
    assert_qom_child(qts, "/machine", "platform-descriptor",
                     TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE);
}

static void assert_descriptor(QTestState *qts, unsigned int cpu_count)
{
    TestDescriptorStorage storage;
    const IA64PlatformDescriptor *descriptor =
        (const IA64PlatformDescriptor *)storage.bytes;
    const IA64PlatformRamRange *ram;
    const IA64PlatformPciRoot *roots;
    const IA64PlatformIoSapic *sapics;
    const IA64PlatformPciRoute *routes;
    const uint32_t ram_offset = sizeof(*descriptor);
    const uint32_t root_offset =
        ram_offset + sizeof(IA64PlatformRamRange);
    const uint32_t sapic_offset = root_offset +
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT *
            sizeof(IA64PlatformPciRoot);
    const uint32_t route_offset = sapic_offset +
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT *
            sizeof(IA64PlatformIoSapic);
    unsigned int i;

    qtest_memread(qts, IA64_ZX6000_EFI_TEST_DESCRIPTOR_GPA,
                  storage.bytes, sizeof(storage.bytes));
    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->FormatRevision), ==,
                     IA64_PLATFORM_DESC_REVISION);
    g_assert_cmpuint(le32_to_cpu(descriptor->HeaderSize), ==,
                     sizeof(*descriptor));
    g_assert_cmpuint(le32_to_cpu(descriptor->TotalSize), ==,
                     IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_ZX6000);
    g_assert_cmphex(le32_to_cpu(descriptor->Flags), ==,
                    IA64_PLATFORM_FLAG_NO_MCFG |
                    IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                    IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
                    IA64_PLATFORM_FLAG_PCI_ZX1_LBA |
                    IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC);
    g_assert_cmpuint(descriptor_checksum(storage.bytes,
                                         sizeof(storage.bytes)), ==, 0);

    g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==,
                    IA64_ZX6000_ZX1_TEST_RAM_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->LowRamEnd), ==,
                    IA64_ZX6000_ZX1_TEST_RAM_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->FirmwareBase), ==,
                    IA64_PLATFORM_FIRMWARE_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->FirmwareSize), ==,
                    IA64_PLATFORM_FIRMWARE_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProcessorCount), ==, cpu_count);
    g_assert_cmpuint(le32_to_cpu(descriptor->SocketCount), ==, cpu_count);
    g_assert_cmpuint(le32_to_cpu(descriptor->CoresPerSocket), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ThreadsPerCore), ==, 1);
    g_assert_cmphex(le64_to_cpu(descriptor->LegacyIoBase), ==,
                    IA64_ZX6000_EFI_TEST_LEGACY_IO_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->LegacyIoSize), ==,
                    IA64_PLATFORM_MIN_LEGACY_IO_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->LocalSapicBase), ==,
                    IA64_ZX6000_ZX1_TEST_PIB_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->LocalSapicSize), ==,
                    IA64_ZX6000_EFI_TEST_LOCAL_SAPIC_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->ConsoleBase), ==,
                    IA64_ZX6000_EFI_TEST_CONSOLE_BASE);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleRegisterStride), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleClockHz), ==,
                     ZX6000_EFI_TEST_UART_INPUT_CLOCK_HZ);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleIrq), ==,
                     IA64_ZX6000_EFI_TEST_CONSOLE_GSI);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramBase), ==,
                    IA64_ZX6000_EFI_TEST_NVRAM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramSize), ==,
                    IA64_ZX6000_EFI_TEST_NVRAM_SIZE);
    g_assert_cmphex(le64_to_cpu(descriptor->RtcBase), ==,
                    IA64_ZX6000_EFI_TEST_RTC_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->RtcSize), ==,
                    IA64_ZX6000_EFI_TEST_RTC_SIZE);

    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeOffset), ==,
                     ram_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeEntrySize), ==,
                     sizeof(IA64PlatformRamRange));
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootOffset), ==,
                     root_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootEntrySize), ==,
                     sizeof(IA64PlatformPciRoot));
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicOffset), ==,
                     sapic_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==,
                     IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicEntrySize), ==,
                     sizeof(IA64PlatformIoSapic));
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteOffset), ==,
                     route_offset);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==,
                     IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteEntrySize), ==,
                     sizeof(IA64PlatformPciRoute));
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileOffset), ==, 0);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileCount), ==, 0);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileEntrySize), ==, 0);

    ram = (const IA64PlatformRamRange *)(storage.bytes + ram_offset);
    g_assert_cmphex(le64_to_cpu(ram->Base), ==,
                    IA64_ZX6000_ZX1_TEST_RAM_BASE);
    g_assert_cmphex(le64_to_cpu(ram->Size), ==,
                    IA64_ZX6000_ZX1_TEST_RAM_SIZE);

    roots = (const IA64PlatformPciRoot *)(storage.bytes + root_offset);
    sapics = (const IA64PlatformIoSapic *)(storage.bytes + sapic_offset);
    routes = (const IA64PlatformPciRoute *)(storage.bytes + route_offset);
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        g_assert_cmpuint(le16_to_cpu(roots[i].Segment), ==, 0);
        g_assert_cmpuint(roots[i].Bus, ==, zx6000_efi_first_bus[i]);
        g_assert_cmpuint(roots[i].BusEnd, ==, zx6000_efi_last_bus[i]);
        g_assert_cmpuint(roots[i].ConfigType, ==,
                         IA64_PLATFORM_PCI_CONFIG_ZX1_LBA);
        g_assert_cmphex(le32_to_cpu(roots[i].Flags), ==,
                        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
        g_assert_cmphex(le64_to_cpu(roots[i].ConfigBase), ==,
                        zx6000_efi_config_base[i]);
        g_assert_cmphex(le64_to_cpu(roots[i].IoBase), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].IoSize), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32Base), ==,
                        IA64_ZX6000_ZX1_TEST_PCI_MMIO_BASE);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32Size), ==,
                        IA64_ZX6000_ZX1_TEST_MMIO_SIZE);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64Base), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64Size), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].DmaBase), ==,
                        IA64_ZX6000_EFI_TEST_IDENTITY_DMA_BASE);
        g_assert_cmphex(le64_to_cpu(roots[i].DmaSize), ==,
                        IA64_ZX6000_EFI_TEST_IDENTITY_DMA_SIZE);
        g_assert_cmpuint(le32_to_cpu(roots[i].Rope), ==, zx6000_efi_rope[i]);
        g_assert_cmphex(le64_to_cpu(roots[i].IoTranslationOffset), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio32TranslationOffset), ==,
                        zx6000_efi_cpu_mmio_base[i]);
        g_assert_cmphex(le64_to_cpu(roots[i].Mmio64TranslationOffset), ==,
                        0);

        g_assert_cmphex(le64_to_cpu(sapics[i].Base), ==,
                        zx6000_efi_config_base[i] +
                        IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
        g_assert_cmpuint(le32_to_cpu(sapics[i].GsiBase), ==,
                         zx6000_efi_gsi_base[i]);
        g_assert_cmpuint(le32_to_cpu(sapics[i].RedirectionEntries), ==,
                         IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT);
        g_assert_cmphex(le32_to_cpu(sapics[i].Version), ==,
                        ZX6000_EFI_TEST_IO_SAPIC_VERSION);
        g_assert_cmpuint(sapics[i].Id, ==, i);

        g_assert_cmpuint(le16_to_cpu(routes[i].Segment), ==, 0);
        g_assert_cmpuint(routes[i].Bus, ==, zx6000_efi_first_bus[i]);
        g_assert_cmpuint(routes[i].Device, ==,
                         IA64_ZX6000_EFI_TEST_PROBE_SLOT);
        g_assert_cmpuint(routes[i].Pin, ==, 0);
        g_assert_cmpuint(le32_to_cpu(routes[i].Gsi), ==,
                         zx6000_efi_gsi_base[i]);
        g_assert_cmphex(le32_to_cpu(routes[i].Flags), ==, 0);
    }
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
    g_assert_cmpuint(size, <=, IA64_PLATFORM_FIRMWARE_SIZE);
    actual = g_malloc(size);
    qtest_memread(qts, IA64_PLATFORM_FIRMWARE_BASE, actual, size);
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
                    IA64_PLATFORM_FIRMWARE_BASE);
    g_assert_cmphex(parse_gr(registers, 8), ==,
                    IA64_ZX6000_EFI_TEST_DESCRIPTOR_GPA);
    g_assert_cmphex(parse_gr(registers, 9), ==,
                    IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE);
    g_assert_cmphex(parse_gr(registers, 10), ==,
                    IA64_PLATFORM_ID_HP_ZX6000);
    if (cpu_index == 0) {
        g_assert_cmphex(parse_gr(registers, 12), ==,
                        IA64_ZX6000_ZX1_TEST_RAM_SIZE - 16);
    }
    halted = strstr(registers, "HALTED: ");
    g_assert_nonnull(halted);
    g_assert_cmpuint(g_ascii_strtoull(halted + strlen("HALTED: "),
                                     NULL, 10), ==, powered_off);
}

static uint32_t zx6000_efi_config_selector(uint8_t bus, unsigned int reg)
{
    return (uint32_t)bus << 16 |
        PCI_DEVFN(IA64_ZX6000_EFI_TEST_PROBE_SLOT, 0) << 8 |
        (reg & 0xfc);
}

static void zx6000_efi_config_select(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int reg)
{
    g_assert_cmpuint(root, <, IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    qtest_writeq(qts, zx6000_efi_config_base[root] + HP_ZX1_IOA_CONFIG_ADDRESS,
                 zx6000_efi_config_selector(bus, reg));
}

static uint8_t zx6000_efi_config_readb(QTestState *qts, unsigned int root,
                               uint8_t bus, unsigned int reg)
{
    zx6000_efi_config_select(qts, root, bus, reg);
    return qtest_readb(qts, zx6000_efi_config_base[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint16_t zx6000_efi_config_readw(QTestState *qts, unsigned int root,
                                uint8_t bus, unsigned int reg)
{
    zx6000_efi_config_select(qts, root, bus, reg);
    return qtest_readw(qts, zx6000_efi_config_base[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint32_t zx6000_efi_config_readl(QTestState *qts, unsigned int root,
                                uint8_t bus, unsigned int reg)
{
    zx6000_efi_config_select(qts, root, bus, reg);
    return qtest_readl(qts, zx6000_efi_config_base[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static void zx6000_efi_config_writew(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int reg, uint16_t value)
{
    zx6000_efi_config_select(qts, root, bus, reg);
    qtest_writew(qts, zx6000_efi_config_base[root] +
                 HP_ZX1_IOA_CONFIG_DATA + (reg & 3), value);
}

static void zx6000_efi_config_writel(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int reg, uint32_t value)
{
    zx6000_efi_config_select(qts, root, bus, reg);
    qtest_writel(qts, zx6000_efi_config_base[root] +
                 HP_ZX1_IOA_CONFIG_DATA + (reg & 3), value);
}

static void assert_config_isolation(QTestState *qts)
{
    const uint32_t root0_selector =
        zx6000_efi_config_selector(0, PCI_VENDOR_ID);
    const uint32_t root1_selector =
        zx6000_efi_config_selector(0, PCI_DEVICE_ID);
    unsigned int root;

    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==, 0);
    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==, 0);
    zx6000_efi_config_select(qts, 0, 0, PCI_VENDOR_ID);
    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==,
                    root0_selector);
    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==, 0);
    zx6000_efi_config_select(qts, 1, 0, PCI_DEVICE_ID);
    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==,
                    root0_selector);
    g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_CONFIG_ADDRESS), ==,
                    root1_selector);

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        g_assert_cmphex(zx6000_efi_config_readw(qts, root, 0,
                                               PCI_VENDOR_ID), ==,
                        IOMMU_TESTDEV_VENDOR_ID);
        g_assert_cmphex(zx6000_efi_config_readb(qts, root, 0,
                                       PCI_VENDOR_ID + 1), ==,
                        IOMMU_TESTDEV_VENDOR_ID >> 8);
        g_assert_cmphex(zx6000_efi_config_readw(qts, root, 0,
                                               PCI_DEVICE_ID), ==,
                        IOMMU_TESTDEV_DEVICE_ID);

        /* The descriptor bus names the root; Mercury type 0 uses bus zero. */
        g_assert_cmphex(zx6000_efi_config_readl(
                            qts, root, zx6000_efi_first_bus[root],
                            PCI_VENDOR_ID), ==, UINT32_MAX);
    }

    zx6000_efi_config_writel(qts, 0, 0, PCI_BASE_ADDRESS_0,
                             ZX6000_EFI_TEST_ROOT0_BAR);
    zx6000_efi_config_writel(qts, 1, 0, PCI_BASE_ADDRESS_0,
                             ZX6000_EFI_TEST_ROOT1_BAR);
    zx6000_efi_config_writew(qts, 0, 0, PCI_COMMAND, PCI_COMMAND_MEMORY);
    zx6000_efi_config_writew(qts, 1, 0, PCI_COMMAND, PCI_COMMAND_MASTER);

    g_assert_cmphex(zx6000_efi_config_readl(qts, 0, 0, PCI_BASE_ADDRESS_0), ==,
                    ZX6000_EFI_TEST_ROOT0_BAR);
    g_assert_cmphex(zx6000_efi_config_readl(qts, 1, 0, PCI_BASE_ADDRESS_0), ==,
                    ZX6000_EFI_TEST_ROOT1_BAR);
    g_assert_cmphex(zx6000_efi_config_readw(qts, 0, 0, PCI_COMMAND), ==,
                    PCI_COMMAND_MEMORY);
    g_assert_cmphex(zx6000_efi_config_readw(qts, 1, 0, PCI_COMMAND), ==,
                    PCI_COMMAND_MASTER);
}

static void zx6000_efi_sapic_select(QTestState *qts, unsigned int root,
                            uint32_t reg)
{
    g_assert_cmpuint(root, <, IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    qtest_writel(qts, zx6000_efi_config_base[root] + HP_ZX1_IOA_IOREGSEL, reg);
}

static uint32_t zx6000_efi_sapic_read(QTestState *qts, unsigned int root,
                              uint32_t reg)
{
    zx6000_efi_sapic_select(qts, root, reg);
    return qtest_readl(qts, zx6000_efi_config_base[root] + HP_ZX1_IOA_IOWIN);
}

static void zx6000_efi_sapic_write(QTestState *qts, unsigned int root,
                           uint32_t reg, uint32_t value)
{
    zx6000_efi_sapic_select(qts, root, reg);
    qtest_writel(qts, zx6000_efi_config_base[root] + HP_ZX1_IOA_IOWIN, value);
}

static void assert_sapic_isolation(QTestState *qts)
{
    const uint32_t rte0 = HP_IO_SAPIC_RTE_BASE;

    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_IOREGSEL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_IOREGSEL), ==, 0);
    zx6000_efi_sapic_select(qts, 0, rte0);
    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_IOREGSEL), ==, rte0);
    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_IOREGSEL), ==, 0);
    zx6000_efi_sapic_select(qts, 1, 1);
    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[0] +
                               HP_ZX1_IOA_IOREGSEL), ==, rte0);
    g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[1] +
                               HP_ZX1_IOA_IOREGSEL), ==, 1);

    g_assert_cmphex(zx6000_efi_sapic_read(qts, 0, 1), ==,
                    ZX6000_EFI_TEST_IO_SAPIC_VERSION);
    g_assert_cmphex(zx6000_efi_sapic_read(qts, 1, 1), ==,
                    ZX6000_EFI_TEST_IO_SAPIC_VERSION);
    g_assert_cmphex(zx6000_efi_sapic_read(qts, 0, rte0), ==,
                    HP_IO_SAPIC_RTE_MASK);
    g_assert_cmphex(zx6000_efi_sapic_read(qts, 1, rte0), ==,
                    HP_IO_SAPIC_RTE_MASK);

    zx6000_efi_sapic_write(qts, 0, rte0,
                   HP_IO_SAPIC_RTE_MASK | ZX6000_EFI_TEST_ROOT0_VECTOR);
    zx6000_efi_sapic_write(qts, 1, rte0,
                   HP_IO_SAPIC_RTE_MASK | ZX6000_EFI_TEST_ROOT1_VECTOR);
    g_assert_cmphex(zx6000_efi_sapic_read(qts, 0, rte0), ==,
                    HP_IO_SAPIC_RTE_MASK | ZX6000_EFI_TEST_ROOT0_VECTOR);
    g_assert_cmphex(zx6000_efi_sapic_read(qts, 1, rte0), ==,
                    HP_IO_SAPIC_RTE_MASK | ZX6000_EFI_TEST_ROOT1_VECTOR);
}

static void assert_mutable_reset_state(QTestState *qts)
{
    unsigned int root;

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        g_assert_cmphex(qtest_readq(qts, zx6000_efi_config_base[root] +
                                   HP_ZX1_IOA_CONFIG_ADDRESS), ==, 0);
        g_assert_cmphex(qtest_readl(qts, zx6000_efi_config_base[root] +
                                   HP_ZX1_IOA_IOREGSEL), ==, 0);
        g_assert_cmphex(zx6000_efi_sapic_read(qts, root,
                                     HP_IO_SAPIC_RTE_BASE), ==,
                        HP_IO_SAPIC_RTE_MASK);
        g_assert_cmphex(zx6000_efi_config_readl(qts, root, 0,
                                       PCI_BASE_ADDRESS_0), ==, 0);
        g_assert_cmphex(zx6000_efi_config_readw(qts, root, 0,
                                               PCI_COMMAND), ==, 0);
    }
}

static void test_machine_configuration(void)
{
    const char *firmware_path = g_getenv(TEST_FIRMWARE_ENV);
    g_autofree char *quoted_firmware = NULL;
    g_autoptr(QDict) response = NULL;
    QList *cpus;
    QTestState *qts;

    g_assert_nonnull(firmware_path);
    quoted_firmware = g_shell_quote(firmware_path);
    qts = qtest_initf(
        "-machine x-ia64-zx6000-efi-test -S "
        "-smp 2,sockets=2,cores=1,threads=1 -m 512M -nodefaults "
        "-display none -net none -serial null -bios %s",
        quoted_firmware);

    response = qtest_qmp(qts, "{'execute':'query-cpus-fast'}");
    g_assert_true(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    g_assert_cmpuint(qlist_size(cpus), ==, 2);
    assert_firmware_loaded(qts, firmware_path);
    assert_descriptor(qts, 2);
    assert_topology_ownership(qts);
    assert_cpu_handoff(qts, 0, false);
    assert_cpu_handoff(qts, 1, true);
    assert_config_isolation(qts);
    assert_sapic_isolation(qts);

    qtest_system_reset(qts);
    assert_descriptor(qts, 2);
    assert_cpu_handoff(qts, 0, false);
    assert_cpu_handoff(qts, 1, true);
    assert_mutable_reset_state(qts);

    qtest_quit(qts);
}

static uint64_t wait_for_acpi_xsdt(QTestState *qts)
{
    uint8_t signature[8];
    unsigned int attempt;

    for (attempt = 0; attempt < 30000; attempt++) {
        qtest_memread(qts, ZX6000_EFI_TEST_RSDP_BASE,
                      signature, sizeof(signature));
        if (memcmp(signature, "RSD PTR ", sizeof(signature)) == 0) {
            return qtest_readq(qts, ZX6000_EFI_TEST_RSDP_BASE + 24U);
        }
        g_usleep(1000);
    }
    g_error("firmware did not publish the ACPI RSDP");
    return 0;
}

static uint64_t find_acpi_table(QTestState *qts, uint64_t xsdt,
                                const char signature[4])
{
    uint32_t length = qtest_readl(qts, xsdt + 4U);
    uint32_t offset;

    g_assert_cmpuint(length, >=, 36U);
    g_assert_cmpuint((length - 36U) % 8U, ==, 0);
    for (offset = 36U; offset < length; offset += 8U) {
        uint64_t table = qtest_readq(qts, xsdt + offset);
        char actual[4];

        qtest_memread(qts, table, actual, sizeof(actual));
        if (memcmp(actual, signature, sizeof(actual)) == 0) {
            return table;
        }
    }
    return 0;
}

static void test_firmware_ecam_mcfg(void)
{
    const uint64_t ecam_base[] = {
        ZX6000_EFI_TEST_ECAM0_BASE,
        ZX6000_EFI_TEST_ECAM1_BASE,
    };
    const char *firmware_path = g_getenv(TEST_FIRMWARE_ENV);
    g_autofree char *quoted_firmware = NULL;
    TestDescriptorStorage storage;
    IA64PlatformDescriptor *descriptor =
        (IA64PlatformDescriptor *)storage.bytes;
    IA64PlatformPciRoot *roots;
    uint8_t mcfg[ZX6000_EFI_TEST_MCFG_SIZE];
    uint64_t xsdt;
    uint64_t table;
    QTestState *qts;
    unsigned int i;

    g_assert_nonnull(firmware_path);
    quoted_firmware = g_shell_quote(firmware_path);
    qts = qtest_initf(
        "-machine x-ia64-zx6000-efi-test,x-pci-ecam=on "
        "-S -smp 1 -m 512M "
        "-nodefaults -display none -net none -serial null -accel tcg "
        "-bios %s",
        quoted_firmware);

    qtest_memread(qts, IA64_ZX6000_EFI_TEST_DESCRIPTOR_GPA,
                  storage.bytes, sizeof(storage.bytes));
    roots = (IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    g_assert_cmphex(le32_to_cpu(descriptor->Flags), ==,
                    IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                    IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
                    IA64_PLATFORM_FLAG_PCI_ECAM);
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        g_assert_cmpuint(roots[i].ConfigType, ==,
                         IA64_PLATFORM_PCI_CONFIG_ECAM);
        g_assert_cmpuint(le16_to_cpu(roots[i].Segment), ==, i);
        g_assert_cmphex(le64_to_cpu(roots[i].ConfigBase), ==,
                        ecam_base[i]);
    }
    g_assert_cmpuint(descriptor_checksum(
                         storage.bytes,
                         le32_to_cpu(descriptor->TotalSize)),
                     ==, 0);

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    xsdt = wait_for_acpi_xsdt(qts);
    table = find_acpi_table(qts, xsdt, "MCFG");
    g_assert_cmphex(table, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, table + 4U), ==, sizeof(mcfg));
    qtest_memread(qts, table, mcfg, sizeof(mcfg));
    g_assert_cmpuint(descriptor_checksum(mcfg, sizeof(mcfg)), ==, 0);
    g_assert_cmphex(ldq_le_p(mcfg + 36U), ==, 0);
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        const unsigned int offset = 44U + i * 16U;

        g_assert_cmphex(ldq_le_p(mcfg + offset), ==, ecam_base[i]);
        g_assert_cmpuint(lduw_le_p(mcfg + offset + 8U), ==, i);
        g_assert_cmpuint(mcfg[offset + 10U], ==, zx6000_efi_first_bus[i]);
        g_assert_cmpuint(mcfg[offset + 11U], ==, zx6000_efi_last_bus[i]);
        g_assert_cmphex(ldl_le_p(mcfg + offset + 12U), ==, 0);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("zx6000-efi/machine-configuration",
                   test_machine_configuration);
    qtest_add_func("zx6000-efi/firmware-ecam-mcfg",
                   test_firmware_ecam_mcfg);
    return g_test_run();
}
