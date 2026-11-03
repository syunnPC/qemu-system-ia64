/*
 * HP zx2000 machine qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/acpi/acpi.h"
#include "hw/display/ati_regs.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define ZX2000_DESCRIPTOR_GPA UINT64_C(0x00300000)
#define ZX2000_HIGH_RAM_BASE  UINT64_C(0x100000000)
#define ZX2000_NVRAM_BASE     UINT64_C(0xfeb00000)
#define ZX2000_SPARSE_IO_BASE UINT64_C(0x00000ffffc000000)
#define ZX2000_ACPI_PM_BASE   UINT64_C(0xff5c0000)
#define ZX2000_UART_BASE      UINT64_C(0xff5e0000)
#define ZX2000_RV100_MMIO_BASE UINT64_C(0x88020000)
#define UART_IER              1U
#define UART_SCR              7U
#define UART_IER_THRI         2U

static const struct {
    uint8_t bus;
    uint8_t bus_end;
    uint8_t rope;
    uint32_t gsi;
    uint64_t config;
    uint64_t io;
} zx2000_roots[] = {
    { 0x00, 0x7f, 0, 16, 0xfed20000, 0x0000 },
    { 0x80, 0x9f, 4, 27, 0xfed28000, 0x8000 },
    { 0xa0, 0xbf, 5, 38, 0xfed2a000, 0xa000 },
    { 0xc0, 0xff, 6, 49, 0xfed2c000, 0xc000 },
};

static const struct {
    unsigned root;
    uint8_t devfn;
    uint8_t type;
    uint8_t pin;
    uint8_t gsi;
    uint32_t id;
    uint16_t command;
} zx2000_devices[] = {
    { 0, PCI_DEVFN(0, 0), IA64_PLATFORM_ONBOARD_GRAPHICS, 1, 16, 0x51591002,
      PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER },
    { 2, PCI_DEVFN(1, 0), IA64_PLATFORM_ONBOARD_OHCI, 1, 38, 0x00351033,
      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER },
    { 2, PCI_DEVFN(1, 1), IA64_PLATFORM_ONBOARD_OHCI, 2, 39, 0x00351033,
      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER },
    { 2, PCI_DEVFN(1, 2), IA64_PLATFORM_ONBOARD_EHCI, 3, 40, 0x00e01033,
      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER },
    { 2, PCI_DEVFN(2, 0), IA64_PLATFORM_ONBOARD_IDE, 1, 43, 0x06491095,
      PCI_COMMAND_IO | PCI_COMMAND_MASTER },
    { 2, PCI_DEVFN(3, 0), IA64_PLATFORM_ONBOARD_NETWORK, 1, 42, 0x100e8086,
      PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER },
};

static QTestState *zx2000_start(const char *options)
{
    const char *firmware = g_getenv("QTEST_IA64_FIRMWARE");
    g_autofree char *quoted = NULL;

    g_assert_nonnull(firmware);
    quoted = g_shell_quote(firmware);
    return qtest_initf("-machine hp-zx2000,nvram=none -m 2G -S "
                       "-display none -serial none -net none -bios %s %s",
                       quoted, options);
}

static uint64_t zx2000_config_select(QTestState *qts, unsigned root,
                                     unsigned devfn, unsigned reg)
{
    uint64_t base = zx2000_roots[root].config;

    qtest_writeq(qts, base + HP_ZX1_IOA_CONFIG_ADDRESS,
                 (uint64_t)devfn << 8 | (reg & 0xfc));
    return base + HP_ZX1_IOA_CONFIG_DATA + (reg & 3);
}

static uint32_t zx2000_config_readl(QTestState *qts, unsigned root,
                                    unsigned devfn, unsigned reg)
{
    return qtest_readl(qts, zx2000_config_select(qts, root, devfn, reg));
}

static uint16_t zx2000_config_readw(QTestState *qts, unsigned root,
                                    unsigned devfn, unsigned reg)
{
    return qtest_readw(qts, zx2000_config_select(qts, root, devfn, reg));
}

static IA64PlatformDescriptor *zx2000_read_descriptor(QTestState *qts,
                                                      uint8_t *storage)
{
    IA64PlatformDescriptor *descriptor = (IA64PlatformDescriptor *)storage;
    uint32_t size;
    uint8_t checksum = 0;
    unsigned i;

    qtest_memread(qts, ZX2000_DESCRIPTOR_GPA, storage, sizeof(*descriptor));
    size = le32_to_cpu(descriptor->TotalSize);
    g_assert_cmpuint(size, >=, sizeof(*descriptor));
    g_assert_cmpuint(size, <=, IA64_PLATFORM_DESC_MAX_SIZE);
    qtest_memread(qts, ZX2000_DESCRIPTOR_GPA, storage, size);
    for (i = 0; i < size; i++) {
        checksum += storage[i];
    }
    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(checksum, ==, 0);
    return descriptor;
}

static void test_hp_zx2000_identity(void)
{
    QTestState *qts = zx2000_start("");
    g_autoptr(QDict) response = qtest_qmp(qts, "{'execute':'query-machines'}");
    QList *machines = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool found = false;

    QLIST_FOREACH_ENTRY(machines, entry) {
        QDict *machine = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(machine, "name"), "hp-zx2000")) {
            g_assert_cmpstr(qdict_get_str(machine, "default-cpu-type"), ==,
                            "mckinley-900-ia64-cpu");
            g_assert_cmpint(qdict_get_int(machine, "cpu-max"), ==, 1);
            g_assert_cmpstr(qdict_get_str(machine, "default-ram-id"), ==,
                            "hp-zx2000.ram");
            found = true;
            break;
        }
    }
    g_assert_true(found);
    qtest_quit(qts);
}

static void zx2000_assert_start_fails(const char *option, const char *value,
                                      const char *message)
{
    const char *firmware = g_getenv("QTEST_IA64_FIRMWARE");
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine",
        "hp-zx2000,nvram=none",
        "-bios",
        firmware,
        "-display",
        "none",
        option,
        value,
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int status;

    g_assert_nonnull(firmware);
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL,
                               &stderr_text, &status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void test_hp_zx2000_constraints(void)
{
    QTestState *qts;

    zx2000_assert_start_fails("-smp", "2", "max CPUs supported");
    zx2000_assert_start_fails("-m", "256M", "at least 512 MiB");
    zx2000_assert_start_fails("-m", "9G", "at most 8 GiB");
    zx2000_assert_start_fails("-cpu", "madison-1500",
                              "hp-zx2000-machine requires");
    qts = zx2000_start("-cpu madison-1400-1.5m");
    qtest_quit(qts);
}

static void test_hp_zx2000_descriptor(void)
{
    uint8_t storage[IA64_PLATFORM_DESC_MAX_SIZE];
    QTestState *qts = zx2000_start("");
    IA64PlatformDescriptor *descriptor = zx2000_read_descriptor(qts, storage);
    const IA64PlatformPciRoot *roots =
        (const IA64PlatformPciRoot *)(storage +
                                      le32_to_cpu(descriptor->PciRootOffset));
    const IA64PlatformIoSapic *sapics =
        (const IA64PlatformIoSapic *)(storage +
                                      le32_to_cpu(descriptor->IoSapicOffset));
    const IA64PlatformPciRoute *routes =
        (const IA64PlatformPciRoute *)(storage +
                                       le32_to_cpu(descriptor->PciRouteOffset));
    unsigned i;
    unsigned j;

    g_assert_cmphex(le32_to_cpu(descriptor->PlatformId), ==,
                    IA64_PLATFORM_ID_HP_ZX2000);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProcessorCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->SocketCount), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->CoresPerSocket), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ThreadsPerCore), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->MaxSockets), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     G_N_ELEMENTS(zx2000_roots));
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==,
                     G_N_ELEMENTS(zx2000_roots));
    g_assert_cmphex(le64_to_cpu(descriptor->ConsoleBase), ==, 0xff5e0000);
    g_assert_cmpuint(le32_to_cpu(descriptor->ConsoleIrq), ==, 45);
    g_assert_cmpuint(le32_to_cpu(descriptor->UartCount), ==, 2);
    g_assert_cmphex(le64_to_cpu(descriptor->Uart[0].Base), ==, 0xff5e0000);
    g_assert_cmphex(le64_to_cpu(descriptor->Uart[1].Base), ==, 0xff5e2000);
    for (i = 0; i < 2; i++) {
        g_assert_cmpuint(le32_to_cpu(descriptor->Uart[i].Gsi), ==, 45 + i);
        g_assert_cmpuint(le32_to_cpu(descriptor->Uart[i].RootIndex), ==, 2);
    }
    g_assert_cmpuint(le32_to_cpu(descriptor->AcpiSciGsi), ==, 47);
    g_assert_cmphex(le64_to_cpu(descriptor->LegacyIoBase), ==,
                    ZX2000_SPARSE_IO_BASE);
    for (i = 0; i < G_N_ELEMENTS(zx2000_roots); i++) {
        uint32_t flags = IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
                         IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO;

        if (i == 0) {
            flags |= IA64_PLATFORM_PCI_ROOT_FLAG_AGP |
                     IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY;
        }
        g_assert_cmpuint(roots[i].Bus, ==, zx2000_roots[i].bus);
        g_assert_cmpuint(roots[i].BusEnd, ==, zx2000_roots[i].bus_end);
        g_assert_cmpuint(le32_to_cpu(roots[i].Rope), ==, zx2000_roots[i].rope);
        g_assert_cmphex(le64_to_cpu(roots[i].ConfigBase), ==,
                        zx2000_roots[i].config);
        g_assert_cmphex(le64_to_cpu(roots[i].IoBase), ==, zx2000_roots[i].io);
        g_assert_cmphex(le64_to_cpu(roots[i].IoSize), ==, 0x2000);
        g_assert_cmphex(le32_to_cpu(roots[i].Flags), ==, flags);
        g_assert_cmphex(le64_to_cpu(sapics[i].Base), ==,
                        zx2000_roots[i].config +
                            IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
        g_assert_cmpuint(le32_to_cpu(sapics[i].GsiBase), ==,
                         zx2000_roots[i].gsi);
        g_assert_cmpuint(sapics[i].Id, ==, zx2000_roots[i].rope);
        g_assert_cmphex(
            qtest_readq(qts, zx2000_roots[i].config + HP_ZX1_IOA_FUNCTION_ID),
            ==, UINT64_C(0x02b00000122e103c));
    }

    g_assert_cmpuint(le32_to_cpu(descriptor->OnboardDeviceCount), ==,
                     G_N_ELEMENTS(zx2000_devices));
    for (i = 0; i < G_N_ELEMENTS(zx2000_devices); i++) {
        bool device_found = false;
        bool route_found = false;
        uint8_t bus = zx2000_roots[zx2000_devices[i].root].bus;

        for (j = 0; j < le32_to_cpu(descriptor->OnboardDeviceCount); j++) {
            const IA64PlatformOnboardDevice *device =
                &descriptor->OnboardDevice[j];

            if (device->Bus == bus &&
                PCI_DEVFN(device->Device, device->Function) ==
                    zx2000_devices[i].devfn) {
                g_assert_cmpuint(device->Type, ==, zx2000_devices[i].type);
                g_assert_cmphex(le32_to_cpu(device->VendorDeviceId), ==,
                                zx2000_devices[i].id);
                device_found = true;
            }
        }
        for (j = 0; j < le32_to_cpu(descriptor->PciRouteCount); j++) {
            if (routes[j].Bus == bus &&
                routes[j].Device == PCI_SLOT(zx2000_devices[i].devfn) &&
                routes[j].Pin == zx2000_devices[i].pin - 1) {
                g_assert_cmpuint(le32_to_cpu(routes[j].Gsi), ==,
                                 zx2000_devices[i].gsi);
                route_found = true;
            }
        }
        g_assert_true(device_found);
        g_assert_true(route_found);
    }
    qtest_quit(qts);
}

static void test_hp_zx2000_pci_layout(void)
{
    QTestState *qts = zx2000_start("");
    uint32_t bars[G_N_ELEMENTS(zx2000_devices)];
    unsigned i;
    unsigned root;
    unsigned slot;

    for (i = 0; i < G_N_ELEMENTS(zx2000_devices); i++) {
        unsigned devfn = zx2000_devices[i].devfn;

        root = zx2000_devices[i].root;
        g_assert_cmphex(zx2000_config_readl(qts, root, devfn, 0), ==,
                        zx2000_devices[i].id);
        g_assert_cmphex(zx2000_config_readw(qts, root, devfn, PCI_COMMAND), ==,
                        zx2000_devices[i].command);
        g_assert_cmphex(
            zx2000_config_readw(qts, root, devfn, PCI_INTERRUPT_LINE), ==,
            zx2000_devices[i].pin << 8 | zx2000_devices[i].gsi);
        bars[i] = zx2000_config_readl(qts, root, devfn, PCI_BASE_ADDRESS_0);
        g_assert_cmphex(bars[i], !=, 0);
    }
    g_assert_cmphex(
        zx2000_config_readl(qts, 2, PCI_DEVFN(3, 0), PCI_SUBSYSTEM_VENDOR_ID),
        ==, 0x1274103c);
    for (root = 0; root < G_N_ELEMENTS(zx2000_roots); root++) {
        for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
            bool present = false;

            for (i = 0; i < G_N_ELEMENTS(zx2000_devices); i++) {
                present |= root == zx2000_devices[i].root &&
                           PCI_DEVFN(slot, 0) == zx2000_devices[i].devfn;
            }
            if (!present) {
                g_assert_cmphex(
                    zx2000_config_readl(qts, root, PCI_DEVFN(slot, 0), 0), ==,
                    UINT32_MAX);
            }
        }
    }
    for (i = 0; i < G_N_ELEMENTS(zx2000_devices); i++) {
        qtest_writel(qts,
                     zx2000_config_select(qts, zx2000_devices[i].root,
                                          zx2000_devices[i].devfn,
                                          PCI_BASE_ADDRESS_0),
                     0);
    }
    qtest_system_reset(qts);
    for (i = 0; i < G_N_ELEMENTS(zx2000_devices); i++) {
        g_assert_cmphex(zx2000_config_readl(qts, zx2000_devices[i].root,
                                            zx2000_devices[i].devfn,
                                            PCI_BASE_ADDRESS_0),
                        ==, bars[i]);
    }
    qtest_quit(qts);
}

static void test_hp_zx2000_ram(void)
{
    static const struct {
        const char *option;
        uint64_t size;
    } sizes[] = {
        { "-m 512M", 512 * MiB },
        { "-m 8G", 8 * GiB },
    };
    unsigned i;

    for (i = 0; i < G_N_ELEMENTS(sizes); i++) {
        uint8_t storage[IA64_PLATFORM_DESC_MAX_SIZE];
        QTestState *qts = zx2000_start(sizes[i].option);
        IA64PlatformDescriptor *descriptor =
            zx2000_read_descriptor(qts, storage);
        const IA64PlatformRamRange *ranges =
            (const IA64PlatformRamRange *)(storage +
                                           le32_to_cpu(
                                               descriptor->RamRangeOffset));
        uint64_t low_size = MIN(sizes[i].size, GiB);

        g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==, sizes[i].size);
        g_assert_cmphex(le64_to_cpu(ranges[0].Base), ==, 0);
        g_assert_cmphex(le64_to_cpu(ranges[0].Size), ==, low_size);
        g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==,
                         sizes[i].size > GiB ? 2 : 1);
        if (sizes[i].size > GiB) {
            uint64_t end = ZX2000_HIGH_RAM_BASE + sizes[i].size - GiB;

            g_assert_cmphex(le64_to_cpu(ranges[1].Base), ==,
                            ZX2000_HIGH_RAM_BASE);
            g_assert_cmphex(le64_to_cpu(ranges[1].Size), ==,
                            sizes[i].size - GiB);
            qtest_writel(qts, end - 4, 0x89abcdef);
            g_assert_cmphex(qtest_readl(qts, end - 4), ==, 0x89abcdef);
        }
        qtest_writel(qts, low_size - 4, 0x12345678);
        g_assert_cmphex(qtest_readl(qts, low_size - 4), ==, 0x12345678);
        qtest_writeb(qts, ZX2000_NVRAM_BASE + 0x80, 0x5a);
        qtest_system_reset(qts);
        g_assert_cmphex(qtest_readb(qts, ZX2000_NVRAM_BASE + 0x80), ==, 0x5a);
        qtest_quit(qts);
    }
}

static void test_hp_zx2000_storage_defaults(void)
{
    QTestState *qts =
        zx2000_start("-drive media=disk,file=null-co://,format=raw "
                     "-drive media=cdrom,file=null-co://,format=raw");
    g_autoptr(QDict) response = qtest_qmp(qts, "{'execute':'query-block'}");
    QList *blocks = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool disk = false;
    bool cdrom = false;

    g_assert_cmpuint(qlist_size(blocks), ==, 2);
    QLIST_FOREACH_ENTRY(blocks, entry) {
        QDict *block = qobject_to(QDict, qlist_entry_obj(entry));
        const char *device = qdict_get_str(block, "device");

        disk |= g_str_equal(device, "ide0-hd0");
        cdrom |= g_str_equal(device, "ide0-cd1");
    }
    g_assert_true(disk);
    g_assert_true(cdrom);
    qtest_quit(qts);
}

static uint64_t zx2000_sparse_io_address(unsigned port)
{
    return ZX2000_SPARSE_IO_BASE + ((uint64_t)(port >> 2) << 12) + (port & 3);
}

static void test_hp_zx2000_int10(void)
{
    const uint64_t ax = zx2000_sparse_io_address(0x1e0);
    const uint64_t execute = zx2000_sparse_io_address(0x1ec);
    const uint64_t data = zx2000_sparse_io_address(0x1ee);
    QTestState *qts = zx2000_start("-nodefaults -vga ati");
    uint8_t response[256];
    unsigned int reset, word;

    for (reset = 0; reset < 2; reset++) {
        if (reset) {
            qtest_system_reset(qts);
        }
        g_assert_cmphex(
            qtest_readl(qts, ZX2000_RV100_MMIO_BASE + BIOS_4_SCRATCH), ==,
            BIOS_4_SCRATCH_CRT1);
        /* The next reset must repeat firmware POST after a guest write. */
        qtest_writel(qts, ZX2000_RV100_MMIO_BASE + BIOS_4_SCRATCH, 0);
        qtest_writew(qts, ax, 0x4f00);
        qtest_writew(qts, execute, 0x4941);
        g_assert_cmphex(qtest_readw(qts, ax), ==, 0x004f);
        g_assert_cmpuint(qtest_readw(qts, execute), ==, sizeof(response) / 2);
        for (word = 0; word < sizeof(response) / 2; word++) {
            stw_le_p(response + word * 2, qtest_readw(qts, data));
        }
        g_assert_cmpmem(response, 4, "VESA", 4);
    }
    qtest_quit(qts);
}

static void test_hp_zx2000_pll_write_enable(void)
{
    const uint64_t mmio = ZX2000_RV100_MMIO_BASE;
    QTestState *qts = zx2000_start("-nodefaults -vga ati");

    for (unsigned swap = 0; swap < 2; swap++) {
        uint32_t index = PLL_DIV_SEL_MASK | PPLL_REF_DIV;
        uint32_t original;

        qtest_system_reset(qts);
        if (swap) {
            qtest_writel(qts, mmio + CNFG_CNTL,
                         R100_APER_REG_ENDIAN_0 << R100_APER_REG_ENDIAN_SHIFT);
        }
        qtest_writel(qts, mmio + CLOCK_CNTL_INDEX,
                     swap ? bswap32(index) : index);
        original = qtest_readl(qts, mmio + CLOCK_CNTL_DATA);
        qtest_writel(qts, mmio + CLOCK_CNTL_DATA,
                     original ^ (swap ? bswap32(1) : 1));
        g_assert_cmphex(qtest_readl(qts, mmio + CLOCK_CNTL_DATA), ==, original);
        g_assert_cmphex(qtest_readl(qts, mmio + CLOCK_CNTL_INDEX), ==,
                        swap ? bswap32(index) : index);

        index |= PLL_WR_EN;
        qtest_writel(qts, mmio + CLOCK_CNTL_INDEX,
                     swap ? bswap32(index) : index);
        qtest_writel(qts, mmio + CLOCK_CNTL_DATA, swap ? bswap32(60) : 60);
        g_assert_cmphex(qtest_readl(qts, mmio + CLOCK_CNTL_DATA), ==,
                        swap ? bswap32(60) : 60);
    }
    qtest_quit(qts);
}

static void zx2000_route_irq(QTestState *qts, unsigned input, uint8_t vector)
{
    uint64_t base = zx2000_roots[2].config;

    qtest_writel(qts, base + HP_ZX1_IOA_IOREGSEL,
                 HP_IO_SAPIC_RTE_BASE + input * 2 + 1);
    qtest_writel(qts, base + HP_ZX1_IOA_IOWIN, 0);
    qtest_writel(qts, base + HP_ZX1_IOA_IOREGSEL,
                 HP_IO_SAPIC_RTE_BASE + input * 2);
    qtest_writel(qts, base + HP_ZX1_IOA_IOWIN,
                 HP_IO_SAPIC_RTE_TRIGGER | HP_IO_SAPIC_RTE_POLARITY | vector);
}

static void zx2000_assert_cpu_irq(QTestState *qts, uint8_t vector)
{
    unsigned attempt;

    g_test_message("Waiting for interrupt vector 0x%02x", vector);
    for (attempt = 0; attempt < 1000; attempt++) {
        g_autofree char *registers = qtest_hmp(qts, "info registers");
        const char *line = strstr(registers, "SAPIC IRR:");
        uint64_t irr[4];

        g_assert_nonnull(line);
        g_assert_cmpint(sscanf(line,
                               "SAPIC IRR: %" SCNx64 " %" SCNx64 " %" SCNx64
                               " %" SCNx64,
                               &irr[0], &irr[1], &irr[2], &irr[3]),
                        ==, 4);
        if (irr[vector / 64] & BIT_ULL(vector % 64)) {
            return;
        }
        g_usleep(1000);
    }
    g_assert_not_reached();
}

static void test_hp_zx2000_pdh(void)
{
    const uint64_t enable =
        ZX2000_ACPI_PM_BASE + IA64_PLATFORM_ACPI_PM1_EVT_OFFSET + 2;
    const uint64_t control =
        ZX2000_ACPI_PM_BASE + IA64_PLATFORM_ACPI_PM1_CNT_OFFSET;
    const uint64_t timer =
        ZX2000_ACPI_PM_BASE + IA64_PLATFORM_ACPI_PM_TMR_OFFSET;
    const uint64_t io_enable =
        zx2000_sparse_io_address(IA64_PLATFORM_ACPI_PM1_EVT_OFFSET + 2);
    const uint64_t io_control =
        zx2000_sparse_io_address(IA64_PLATFORM_ACPI_PM1_CNT_OFFSET);
    const uint64_t io_timer =
        zx2000_sparse_io_address(IA64_PLATFORM_ACPI_PM_TMR_OFFSET);
    QTestState *qts = zx2000_start("");
    uint32_t first_timer;
    unsigned i;

    g_assert_cmphex(qtest_readw(qts, control), ==, ACPI_BITMASK_SCI_ENABLE);
    qtest_writew(qts, io_control, 0);
    g_assert_cmphex(qtest_readw(qts, control), ==, 0);
    qtest_writew(qts, control, ACPI_BITMASK_SCI_ENABLE);
    g_assert_cmphex(qtest_readw(qts, io_control), ==, ACPI_BITMASK_SCI_ENABLE);
    qtest_writew(qts, io_enable, ACPI_BITMASK_POWER_BUTTON_ENABLE);
    g_assert_cmphex(qtest_readw(qts, enable), ==,
                    ACPI_BITMASK_POWER_BUTTON_ENABLE);
    first_timer = qtest_readl(qts, io_timer);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readl(qts, io_timer), !=, first_timer);
    g_assert_cmphex(qtest_readl(qts, io_timer), ==, qtest_readl(qts, timer));

    zx2000_route_irq(qts, 9, 0xdd);
    qtest_qmp_assert_success(qts, "{'execute':'system_powerdown'}");
    zx2000_assert_cpu_irq(qts, 0xdd);

    for (i = 0; i < 2; i++) {
        uint64_t base = ZX2000_UART_BASE + i * 0x2000;

        qtest_writeb(qts, base + UART_SCR, 0xa0 + i);
        g_assert_cmphex(qtest_readb(qts, base + UART_SCR), ==, 0xa0 + i);
        zx2000_route_irq(qts, 7 + i, 0xde + i);
        qtest_writeb(qts, base + UART_IER, UART_IER_THRI);
        zx2000_assert_cpu_irq(qts, 0xde + i);
        qtest_writeb(qts, base + UART_IER, 0);
    }
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readw(qts, io_enable), ==, 0);
    g_assert_cmphex(qtest_readw(qts, io_control), ==, ACPI_BITMASK_SCI_ENABLE);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hp-zx2000/identity", test_hp_zx2000_identity);
    qtest_add_func("/hp-zx2000/constraints", test_hp_zx2000_constraints);
    qtest_add_func("/hp-zx2000/descriptor", test_hp_zx2000_descriptor);
    qtest_add_func("/hp-zx2000/pci-layout", test_hp_zx2000_pci_layout);
    qtest_add_func("/hp-zx2000/ram", test_hp_zx2000_ram);
    qtest_add_func("/hp-zx2000/storage-defaults",
                   test_hp_zx2000_storage_defaults);
    qtest_add_func("/hp-zx2000/int10", test_hp_zx2000_int10);
    qtest_add_func("/hp-zx2000/pll-write-enable",
                   test_hp_zx2000_pll_write_enable);
    qtest_add_func("/hp-zx2000/pdh", test_hp_zx2000_pdh);
    return g_test_run();
}
