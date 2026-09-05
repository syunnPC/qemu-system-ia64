/*
 * IA-64 virtual platform machine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"
#include "libqos/generic-pcihost.h"
#include "libqos/pci.h"
#include "exec/memattrs.h"
#include "hw/display/bochs-vbe.h"
#include "hw/display/vga_regs.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "hw/pci/pcie_regs.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/ia64/ia64_ras.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/ia64/ia64_zx2_pcie_test.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/net/e1000_regs.h"
#include "hw/pci-host/hp-zx1-iommu.h"
#include "hw/pci-host/hp-zx1-mio-regs.h"
#include "hw/pci-host/hp-zx2-mio-regs.h"

#define TEST_FIRMWARE_ENV             "QTEST_IA64_FIRMWARE"
#define IA64_PCI_CONFIG_BASE         0x0000007ff0000000ULL
#define IA64_PIB_BASE                0x00000000fee00000ULL
#define IA64_PCIE_ROOT_SLOT          7U
#define IA64_PCIE_ROOT_DEVFN         QPCI_DEVFN(IA64_PCIE_ROOT_SLOT, 0)
#define IA64_PCIE_SECONDARY_BUS      1U
#define IA64_PCIE_ENDPOINT_DEVFN     QPCI_DEVFN(0, 0)
#define IA64_PCIE_ENDPOINT_MMIO      0x00000000c2000000ULL
#define IA64_PCIE_ROOT_MMIO          0x00000000c2100000ULL
#define IA64_PCIE_INTX_GSI           19U
#define IA64_PCIE_ROOT_VENDOR        0x1b36U
#define IA64_PCIE_EDU_VENDOR         0x1234U
#define IA64_ZX2_PCIE_MIO_BASE       0x00000000b1000000ULL
#define IA64_ZX2_PCIE_IOMMU_REG(offset) \
    (IA64_ZX2_PCIE_MIO_BASE + 0x1000 + (offset))
#define IA64_ZX2_PCIE_IOMMU_IBASE    0x0000000040000000ULL
#define IA64_ZX2_PCIE_IOMMU_IMASK    0x00000000f0000000ULL
#define IA64_ZX2_PCIE_PDIR           0x0000000001000000ULL
#define IA64_ZX2_PCIE_TARGET         0x0000000002000000ULL
#define IA64_ZX2_PCIE_PTE_VALID      0x8000000000000000ULL
#define IA64_ACPI_PM_IO_BASE         0x00002000ULL
#define IA64_ACPI_PM1_EVT_EN_OFFSET  0x02ULL
#define IA64_ACPI_PM1_CNT_OFFSET     0x04ULL
#define IA64_ACPI_PM_RESET_OFFSET    0x0cULL
#define IA64_ACPI_PM_RESET_VALUE     0x01U
#define IA64_RTC_BASE                0x00000000ffef0000ULL
#define IA64_WATCHDOG_BASE           0x00000000ffee0000ULL
#define IA64_WATCHDOG_CODE_OFFSET    0x08ULL
#define IA64_NVRAM_BASE              0x00000000fff00000ULL
#define IA64_NVRAM_SIZE              (64 * KiB)
#define IA64_NVRAM_EXTENDED_FILE_SIZE (512 * KiB)
#define IA64_NVRAM_COMMIT_OFFSET     (IA64_NVRAM_SIZE - 8)
#define IA64_NVRAM_COMMIT_MAGIC      0x54494d4d4f43564eULL
#define IA64_IOSAPIC_BASE            0x0000000080110000ULL
#define IA64_IOSAPIC_IOREGSEL        0x00ULL
#define IA64_IOSAPIC_IOWIN           0x10ULL
#define IA64_IOSAPIC_EOI             0x40ULL
#define IA64_IOSAPIC_RTE_BASE        0x10U
#define IA64_IOSAPIC_RTE_LOWEST      BIT(8)
#define IA64_IOSAPIC_RTE_DELIVERY    BIT(12)
#define IA64_IOSAPIC_RTE_REMOTE_IRR  BIT(14)
#define IA64_IOSAPIC_RTE_LEVEL       BIT(15)
#define IA64_IOSAPIC_RTE_MASKED      BIT(16)
#define IA64_TEST_RAM_SIZE           (256 * MiB)
#define IA64_INT10_ROM_BASE          0x000c0000ULL
#define IA64_INT10_ROM_SIZE          0x00000800U
#define IA64_INT10_VECTOR_ADDR       0x00000040ULL
#define IA64_INT10_ROM_PCIR_OFFSET   0x0020U
#define IA64_INT10_ROM_ATI_SIGNATURE_OFFSET 0x0074U
#define IA64_INT10_ROM_ATI_HEADER_OFFSET 0x0080U
#define IA64_INT10_ROM_ATI_HEADER_SIZE 0x0060U
#define IA64_INT10_ROM_ATI_RAGE128_HEADER_SIZE 0x004aU
#define IA64_INT10_ROM_ATI_INIT_OFFSET 0x00e0U
#define IA64_INT10_ROM_ATI_INIT_READ_SIZE 10U
#define IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET 0x00f0U
#define IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE 12U
#define IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET 0x00f0U
#define IA64_INT10_ROM_ATI_RAGE128_MISC_SIZE 15U
#define IA64_INT10_ROM_ATI_MISC_OFFSET 0x00fcU
#define IA64_INT10_ROM_ATI_MISC_SIZE 2U
#define IA64_INT10_ROM_ATI_CONNECTOR_OFFSET 0x02e0U
#define IA64_INT10_ROM_ATI_CONNECTOR_SIZE 6U
#define IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET 0x02e0U
#define IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE 30U
#define IA64_INT10_ROM_ATI_PLL_OFFSET 0x0300U
#define IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET 0x0383U
#define IA64_INT10_ROM_ATI_MEM_REGION_SIZE 106U
#define IA64_INT10_ROM_HANDLER_OFFSET 0x0100U
#define IA64_INT10_HANDLER_SIZE      116U
#define IA64_INT10_ROM_OEM_OFFSET    0x0180U
#define IA64_INT10_ROM_MODES_OFFSET  0x01d0U
#define IA64_INT10_IO_BASE           0x01e0U
#define IA64_INT10_TRIGGER           0x4941U
#define IA64_VBE2_SIGNATURE          0x32454256U
#define IA64_VBE_IO_INDEX            0x01ceU
#define IA64_VBE_IO_DATA             0x01d0U
#define IA64_VGA_FB_BASE             0x00000000c4000000ULL
#define IA64_VGA_MMIO_BASE           0x00000000c8000000ULL
#define IA64_VGA_LARGE_FB_BASE       0x00000000c8000000ULL
#define IA64_VGA_LARGE_MMIO_BASE     0x00000000d0000000ULL
#define IA64_VGA_LEGACY_BASE         0x00000000000a0000ULL
#define IA64_ATI_BIOS_0_SCRATCH      0x0010U
#define IA64_ATI_VENDOR_ID           0x1002U
#define IA64_ATI_RAGE128_DEVICE_ID   0x5046U
#define IA64_ATI_RV100_DEVICE_ID     0x5159U
#define IA64_ATI_ES1000_DEVICE_ID    0x515eU
#define IA64_ATI_TEST_ROM_BASE       0x00000000d0020000ULL
#define IA64_BDA_VIDEO_MODE          0x00000449ULL
#define IA64_BDA_VIDEO_COLUMNS       0x0000044aULL
#define IA64_BDA_VIDEO_PAGE_SIZE     0x0000044cULL
#define IA64_BDA_VIDEO_ROWS          0x00000484ULL
#define IA64_BDA_CHARACTER_HEIGHT    0x00000485ULL
#define IA64_BDA_VIDEO_CONTROL       0x00000487ULL

enum TestInt10Register {
    TEST_INT10_AX,
    TEST_INT10_BX,
    TEST_INT10_CX,
    TEST_INT10_DX,
    TEST_INT10_DI,
    TEST_INT10_ES,
    TEST_INT10_EXEC,
    TEST_INT10_DATA,
};

typedef struct TestInt10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
    uint32_t input_signature;
} TestInt10Registers;

#define IA64_LSI_MMIO_BASE           0x00000000c1030000ULL
#define IA64_LSI_SCRIPT_ADDR         0x08000000U
#define IA64_LSI_MSGOUT_ADDR         0x08010000U
#define IA64_LSI_CDB_ADDR            0x08010010U
#define IA64_LSI_STATUS_ADDR         0x08010020U
#define IA64_LSI_COMPLETE_ADDR       0x08010030U
#define IA64_LSI_REG_DSTAT           0x0c
#define IA64_LSI_REG_ISTAT0          0x14
#define IA64_LSI_REG_DSP             0x2c
#define IA64_LSI_REG_SIST0           0x42
#define IA64_LSI_REG_SIST1           0x43
#define IA64_LSI_ISTAT0_DIP          0x01
#define IA64_LSI_ISTAT0_INTF         0x04
#define IA64_LSI_DSTAT_SIR           0x04
#define IA64_LSI_PHASE_CMD           2
#define IA64_LSI_PHASE_ST            3
#define IA64_LSI_PHASE_MO            6
#define IA64_LSI_PHASE_MI            7
#define IA64_LSI_SCRIPT_SELECT       0x40000008U
#define IA64_LSI_SCRIPT_DISCONNECT   0x48000000U
#define IA64_LSI_SCRIPT_INTERRUPT    0x98080000U
#define IA64_LSI_SCRIPT_MOVE(phase, count) \
    (((phase) << 24) | (count))

#define IA64_E1000_MMIO_BASE         0x00000000c1040000ULL
#define IA64_E1000_IO_BASE           0x0000c400U
#define IA64_E1000_SLOT              6U
#define IA64_E1000_GSI               18U
#define IA64_E1000_TX_DESC_ADDR      0x08020000U
#define IA64_E1000_TX_BUFFER_ADDR    0x08021000U
#define IA64_E1000_RX_DESC_ADDR      0x08022000U
#define IA64_E1000_RX_BUFFER_ADDR    0x08023000U
#define IA64_E1000_RING_SIZE         128U
#define IA64_E1000_TEST_TIMEOUT_MS   5000

typedef struct ExpectedPCIDevice {
    unsigned slot;
    uint16_t vendor;
    uint16_t device;
    uint16_t command;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint32_t bars[6];
} ExpectedPCIDevice;

static const ExpectedPCIDevice expected_e1000 = {
    .slot = IA64_E1000_SLOT,
    .vendor = PCI_VENDOR_ID_INTEL,
    .device = E1000_DEV_ID_82540EM,
    .command = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
    .irq_line = IA64_E1000_GSI,
    .irq_pin = 1,
    .bars = {
        [0] = IA64_E1000_MMIO_BASE,
        [1] = IA64_E1000_IO_BASE | PCI_BASE_ADDRESS_SPACE_IO,
    },
};

static uint32_t iosapic_read(QTestState *qts, uint32_t reg);
static void iosapic_write(QTestState *qts, uint32_t reg, uint32_t value);

static QTestState *ia64_vpc_start(const char *extra_args)
{
    return qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S %s",
                       extra_args ?: "");
}

/* These tests intentionally verify the default persistent-NVRAM handoff. */
static QTestState *ia64_vpc_handoff_start(const char *machine,
                                          const char *extra_args)
{
    return qtest_initf("-machine %s -m 256M -S %s",
                       machine, extra_args ?: "");
}

static QTestState *ia64_vpc_nvram_start(const char *quoted_path)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine ia64-vpc,nvram=%s -m 256M -S -bios %s",
                       quoted_path, firmware);
}

static uint64_t ia64_sparse_io_offset(uint32_t port)
{
    return ((uint64_t)(port >> 2) << 12) | (port & 0xfff);
}

static void int10_outw(QTestState *qts, uint16_t port, uint16_t value)
{
    qtest_writew(qts, IA64_LEGACY_IO_PORT_PA(port), value);
}

static uint16_t int10_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, IA64_LEGACY_IO_PORT_PA(port));
}

static size_t int10_call(QTestState *qts, TestInt10Registers *regs,
                         uint8_t *response, size_t response_size)
{
    static const size_t register_offsets[] = {
        [TEST_INT10_AX] = offsetof(TestInt10Registers, ax),
        [TEST_INT10_BX] = offsetof(TestInt10Registers, bx),
        [TEST_INT10_CX] = offsetof(TestInt10Registers, cx),
        [TEST_INT10_DX] = offsetof(TestInt10Registers, dx),
        [TEST_INT10_DI] = offsetof(TestInt10Registers, di),
        [TEST_INT10_ES] = offsetof(TestInt10Registers, es),
    };
    size_t word_count;
    size_t i;

    for (i = TEST_INT10_AX; i <= TEST_INT10_ES; i++) {
        uint16_t value;

        memcpy(&value, (uint8_t *)regs + register_offsets[i],
               sizeof(value));
        int10_outw(qts, IA64_INT10_IO_BASE + i * 2, value);
    }
    if (regs->input_signature != 0) {
        int10_outw(qts, IA64_INT10_IO_BASE + TEST_INT10_DATA * 2,
                   (uint16_t)regs->input_signature);
        int10_outw(qts, IA64_INT10_IO_BASE + TEST_INT10_DATA * 2,
                   (uint16_t)(regs->input_signature >> 16));
    }
    int10_outw(qts, IA64_INT10_IO_BASE + TEST_INT10_EXEC * 2,
               IA64_INT10_TRIGGER);
    word_count = int10_inw(qts,
                           IA64_INT10_IO_BASE + TEST_INT10_EXEC * 2);
    g_assert_cmpuint(word_count * 2, <=, response_size);
    for (i = 0; i < word_count; i++) {
        stw_le_p(response + i * 2, int10_inw(
            qts, IA64_INT10_IO_BASE + TEST_INT10_DATA * 2));
    }
    for (i = TEST_INT10_AX; i <= TEST_INT10_ES; i++) {
        uint16_t value = int10_inw(qts,
                                   IA64_INT10_IO_BASE + i * 2);

        memcpy((uint8_t *)regs + register_offsets[i], &value,
               sizeof(value));
    }
    return word_count * 2;
}

static uint32_t int10_far_to_linear(uint32_t pointer)
{
    return (pointer >> 16) * 16 + (pointer & 0xffff);
}

static uint16_t test_vbe_read(QTestState *qts, uint16_t index)
{
    qtest_writew(qts, IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_INDEX), index);
    return qtest_readw(qts, IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_DATA));
}

static uint8_t test_vga_indexed_read(QTestState *qts, uint16_t index_port,
                                     uint16_t data_port, uint8_t index)
{
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(index_port), index);
    return qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(data_port));
}

static void test_assert_ppm_pixel(const char *filename, unsigned width,
                                  unsigned height, unsigned x, unsigned y,
                                  uint8_t red, uint8_t green, uint8_t blue)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    const uint8_t *pixel;
    char *end;
    unsigned actual_width;
    unsigned actual_height;
    unsigned maximum;
    gsize length;

    g_assert_true(g_file_get_contents(filename, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_true(g_str_has_prefix(contents, "P6\n"));
    actual_width = g_ascii_strtoull(contents + 3, &end, 10);
    actual_height = g_ascii_strtoull(end, &end, 10);
    maximum = g_ascii_strtoull(end, &end, 10);
    g_assert_cmpuint(actual_width, ==, width);
    g_assert_cmpuint(actual_height, ==, height);
    g_assert_cmpuint(maximum, ==, 255);
    g_assert_cmpuint(x, <, width);
    g_assert_cmpuint(y, <, height);
    g_assert_cmpuint(length - (end - contents), >,
                     (gsize)width * height * 3);
    g_assert_true(g_ascii_isspace(*end));
    if (*end++ == '\r' && *end == '\n') {
        end++;
    }
    pixel = (const uint8_t *)end + ((gsize)y * width + x) * 3;
    g_assert_cmphex(pixel[0], ==, red);
    g_assert_cmphex(pixel[1], ==, green);
    g_assert_cmphex(pixel[2], ==, blue);
}

static void assert_ati_combios(const uint8_t *rom, uint16_t device,
                               uint8_t memory_mb)
{
    static const uint8_t expected_rage128_header[] = {
        0x02, 0xa0, 0x01, 0x01, 0x03, 0x01, 0x4a, 0x00,
    };
    static const char expected_rage128_misc[] = "R128AGP SGS1UN";
    static const uint8_t
        expected_rage128_crt[IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE] = {
        0x12, 0x00, 0x80, 0x00, 0x00, 0x00, 0x63, 0x4f,
        0x51, 0x8c, 0x0c, 0x02, 0xdf, 0x01, 0xe9, 0x01,
        0x82, 0x00, 0xd6, 0x09,
    };
    static const uint8_t expected_connector[] = {
        0x11, 0x11, 0x00, 0x23, 0x00, 0x00,
    };
    static const uint8_t radeon_init_fields[] = { 0x46, 0x4e, 0x52 };
    static const uint8_t radeon_only_fields[] = {
        0x46, 0x48, 0x4e, 0x50, 0x52, 0x5e,
    };
    static const uint8_t clock_fields[] = { 0x0e, 0x1a, 0x26 };
    uint8_t zero[IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE] = { 0 };
    uint8_t expected_mem[IA64_INT10_ROM_ATI_MEM_REGION_SIZE] = { 0 };
    uint16_t header = lduw_le_p(rom + 0x48);
    bool rage128 = device == IA64_ATI_RAGE128_DEVICE_ID;
    bool es1000 = device == IA64_ATI_ES1000_DEVICE_ID;
    uint16_t pll;
    size_t i;

    g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_SIGNATURE_OFFSET, 10,
                    "761295520", 10);
    g_assert_cmphex(header, ==, IA64_INT10_ROM_ATI_HEADER_OFFSET);
    if (rage128) {
        g_assert_cmpmem(rom + header, sizeof(expected_rage128_header),
                        expected_rage128_header,
                        sizeof(expected_rage128_header));
        g_assert_cmphex(lduw_le_p(rom + header + 6), ==,
                        IA64_INT10_ROM_ATI_RAGE128_HEADER_SIZE);
    } else {
        g_assert_cmphex(rom[header], ==, 8);
        g_assert_cmphex(rom[header + 1], ==, 0xa0);
        g_assert_cmpmem(rom + header + 2, 4, zero, 4);
        g_assert_cmphex(lduw_le_p(rom + header + 6), ==,
                        IA64_INT10_ROM_ATI_HEADER_SIZE);
    }
    g_assert_cmphex(lduw_le_p(rom + header + 0x0c), ==,
                    IA64_INT10_ROM_ATI_INIT_OFFSET);
    if (!rage128) {
        for (i = 0; i < G_N_ELEMENTS(radeon_init_fields); i++) {
            g_assert_cmphex(lduw_le_p(rom + header +
                                      radeon_init_fields[i]), ==,
                        IA64_INT10_ROM_ATI_INIT_OFFSET);
        }
    }
    g_assert_cmphex(rom[IA64_INT10_ROM_ATI_INIT_OFFSET - 1], ==, 0);
    g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_INIT_OFFSET,
                    IA64_INT10_ROM_ATI_INIT_READ_SIZE, zero,
                    IA64_INT10_ROM_ATI_INIT_READ_SIZE);
    if (rage128) {
        g_assert_cmphex(lduw_le_p(rom + header + 0x14), ==,
                        IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET);
        g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET,
                        IA64_INT10_ROM_ATI_RAGE128_MISC_SIZE,
                        expected_rage128_misc,
                        sizeof(expected_rage128_misc));
        g_assert_cmphex(lduw_le_p(rom + header + 0x2e), ==,
                        IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET);
        g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET,
                        IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE,
                        expected_rage128_crt,
                        sizeof(expected_rage128_crt));
        for (i = 0; i < G_N_ELEMENTS(radeon_only_fields); i++) {
            g_assert_cmphex(lduw_le_p(rom + header +
                                      radeon_only_fields[i]), ==, 0);
        }
    } else {
        g_assert_cmphex(lduw_le_p(rom + header + 0x14), ==,
                        IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET);
        g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET,
                        IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE, zero,
                        IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE);
        g_assert_cmphex(lduw_le_p(rom + header + 0x2e), ==, 0);
        g_assert_cmphex(lduw_le_p(rom + header + 0x5e), ==,
                        IA64_INT10_ROM_ATI_MISC_OFFSET);
        g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_MISC_OFFSET,
                        IA64_INT10_ROM_ATI_MISC_SIZE, zero,
                        IA64_INT10_ROM_ATI_MISC_SIZE);
        g_assert_cmphex(lduw_le_p(rom + header + 0x50), ==,
                        IA64_INT10_ROM_ATI_CONNECTOR_OFFSET);
        g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_CONNECTOR_OFFSET,
                        IA64_INT10_ROM_ATI_CONNECTOR_SIZE,
                        expected_connector, sizeof(expected_connector));
        g_assert_cmphex(lduw_le_p(rom + header + 0x48), ==,
                        IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET);
    }

    expected_mem[0] = 3;
    expected_mem[3] = memory_mb;
    expected_mem[4] = 0x25;
    expected_mem[6] = 1;
    expected_mem[8] = 0xff;
    g_assert_cmpmem(rom + IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET - 3,
                    sizeof(expected_mem), expected_mem,
                    sizeof(expected_mem));

    pll = lduw_le_p(rom + header + 0x30);
    g_assert_cmphex(pll, ==, IA64_INT10_ROM_ATI_PLL_OFFSET);
    g_assert_cmphex(rom[pll], ==, rage128 ? 6 : 0x0a);
    g_assert_cmphex(rom[pll + 1], ==, rage128 ? 0x32 : 0x46);
    g_assert_cmphex(rom[pll + 2], ==, 3);
    g_assert_cmphex(rom[pll + 3], ==, rage128 ? 2 : 3);
    g_assert_cmpuint(lduw_le_p(rom + pll + 0x04), ==,
                     rage128 ? 0x0600 : (es1000 ? 0x05ee : 0x05a6));
    g_assert_cmpuint(lduw_le_p(rom + pll + 0x06), ==,
                     rage128 ? 0x05f8 : (es1000 ? 0x05e6 : 0x059e));
    g_assert_cmpuint(lduw_le_p(rom + pll + 0x08), ==,
                     rage128 ? 12000 : (es1000 ? 20000 : 16600));
    g_assert_cmpuint(lduw_le_p(rom + pll + 0x0a), ==,
                     rage128 ? 12000 : (es1000 ? 20000 : 16600));
    g_assert_cmphex(rom[pll + 0x0c], ==, 3);
    g_assert_cmphex(rom[pll + 0x0d], ==, 12);
    for (i = 0; i < G_N_ELEMENTS(clock_fields); i++) {
        size_t offset = pll + clock_fields[i];

        g_assert_cmpuint(lduw_le_p(rom + offset), ==,
                         rage128 ? 2950 : 2700);
        g_assert_cmpuint(lduw_le_p(rom + offset + 2), ==,
                         rage128 ? (i == 0 ? 65 : 29) :
                                   (i == 0 ? 60 : 12));
        g_assert_cmpuint(ldl_le_p(rom + offset + 4), ==,
                         rage128 ? 12500 : (i == 0 ? 12000 : 20000));
        g_assert_cmpuint(ldl_le_p(rom + offset + 8), ==,
                         rage128 ? (i == 0 ? 40000 : 26041) :
                                   (i == 0 ? 35000 : 40000));
    }
    if (!rage128) {
        g_assert_cmphex(rom[pll + 0x32], ==, 1);
        g_assert_cmphex(rom[pll + 0x33], ==, 0x12);
        g_assert_cmpuint(lduw_le_p(rom + pll + 0x34), ==, 2700);
        g_assert_cmpuint(ldl_le_p(rom + pll + 0x36), ==, 40);
        g_assert_cmpuint(ldl_le_p(rom + pll + 0x3a), ==, 3000);
        g_assert_cmpuint(ldl_le_p(rom + pll + 0x3e), ==, 12000);
        g_assert_cmpuint(ldl_le_p(rom + pll + 0x42), ==, 35000);
    }
}

static void test_int10_rom(void)
{
    uint8_t rom[IA64_INT10_ROM_SIZE];
    uint8_t zero[IA64_INT10_ROM_SIZE] = { 0 };
    uint8_t vector[4];
    uint32_t vector_linear;
    unsigned checksum = 0;
    QTestState *qts = ia64_vpc_start(NULL);
    size_t i;

    qtest_memread(qts, IA64_INT10_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(rom[0], ==, 0x55);
    g_assert_cmphex(rom[1], ==, 0xaa);
    g_assert_cmphex(rom[2], ==, IA64_INT10_ROM_SIZE / 512);
    g_assert_cmphex(lduw_le_p(rom + 0x0d), ==,
                    IA64_INT10_ROM_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(rom + 0x13), ==,
                    IA64_INT10_ROM_BASE >> 4);
    g_assert_cmphex(lduw_le_p(rom + 0x18), ==,
                    IA64_INT10_ROM_PCIR_OFFSET);
    g_assert_cmpmem(rom + IA64_INT10_ROM_PCIR_OFFSET, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 4),
                    ==, 0x1002);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 6),
                    ==, 0x5046);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x10),
                    ==, IA64_INT10_ROM_SIZE / 512);
    g_assert_cmpmem(rom + 0x60, 19, "QEMU IA64 VBE INT10", 19);
    assert_ati_combios(rom, IA64_ATI_RAGE128_DEVICE_ID, 16);
    g_assert_cmpmem(rom + IA64_INT10_ROM_OEM_OFFSET, 13,
                    "QEMU IA64 VBE", 13);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_MODES_OFFSET),
                    ==, 0x111);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_MODES_OFFSET + 13 * 2),
                    ==, 0xffff);
    g_assert_cmphex(rom[IA64_INT10_ROM_HANDLER_OFFSET], ==, 0x55);
    g_assert_cmphex(rom[IA64_INT10_ROM_HANDLER_OFFSET + 1], ==, 0x89);
    g_assert_cmphex(rom[IA64_INT10_ROM_HANDLER_OFFSET +
                       IA64_INT10_HANDLER_SIZE], ==, 0);
    for (i = 0; i < sizeof(rom); i++) {
        checksum += rom[i];
    }
    g_assert_cmphex(checksum & 0xff, ==, 0);

    qtest_memread(qts, IA64_INT10_VECTOR_ADDR, vector, sizeof(vector));
    g_assert_cmphex(lduw_le_p(vector), ==, IA64_INT10_ROM_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(vector + 2), ==,
                    IA64_INT10_ROM_BASE >> 4);
    vector_linear = lduw_le_p(vector + 2) * 16 + lduw_le_p(vector);
    g_assert_cmphex(vector_linear, ==,
                    IA64_INT10_ROM_BASE + IA64_INT10_ROM_HANDLER_OFFSET);

    qtest_memwrite(qts, IA64_INT10_ROM_BASE, zero, sizeof(zero));
    qtest_memwrite(qts, IA64_INT10_VECTOR_ADDR, zero, sizeof(vector));
    qtest_system_reset(qts);
    qtest_memread(qts, IA64_INT10_ROM_BASE, rom, sizeof(rom));
    qtest_memread(qts, IA64_INT10_VECTOR_ADDR, vector, sizeof(vector));
    g_assert_cmphex(rom[0], ==, 0x55);
    g_assert_cmphex(rom[1], ==, 0xaa);
    g_assert_cmphex(lduw_le_p(vector), ==, IA64_INT10_ROM_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(vector + 2), ==,
                    IA64_INT10_ROM_BASE >> 4);
    qtest_quit(qts);
}

static void assert_ati_model_int10_rom(const char *args, uint16_t device)
{
    uint8_t rom[IA64_INT10_ROM_SIZE];
    unsigned int checksum = 0;
    QTestState *qts = ia64_vpc_start(args);
    size_t i;

    qtest_memread(qts, IA64_INT10_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 4), ==,
                    IA64_ATI_VENDOR_ID);
    g_assert_cmphex(lduw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 6), ==,
                    device);
    assert_ati_combios(rom, device, 16);
    for (i = 0; i < sizeof(rom); i++) {
        checksum += rom[i];
    }
    g_assert_cmphex(checksum & 0xff, ==, 0);
    qtest_quit(qts);
}

static void test_int10_rv100_rom(void)
{
    assert_ati_model_int10_rom(
        "-vga ati -global ati-vga.model=rv100", IA64_ATI_RV100_DEVICE_ID);
}

static void test_int10_es1000_rom(void)
{
    assert_ati_model_int10_rom(
        "-vga ati -global ati-vga.model=es1000", IA64_ATI_ES1000_DEVICE_ID);
}

static void test_int10_vbe_for_device(const char *extra_args)
{
    uint8_t response[512];
    TestInt10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
    };
    uint32_t memory_size;
    uint32_t max_width;
    uint32_t modes_linear;
    unsigned checksum = 0;
    size_t length;
    size_t i;
    QTestState *qts = ia64_vpc_start(extra_args);

    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmpmem(response, 4, "VESA", 4);

    regs = (TestInt10Registers) {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = IA64_VBE2_SIGNATURE,
    };
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmpmem(response, 4, "VESA", 4);
    g_assert_cmphex(lduw_le_p(response + 4), ==, 0x0300);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 256);
    memory_size = (uint32_t)lduw_le_p(response + 18) * (64 * KiB);
    modes_linear = int10_far_to_linear(ldl_le_p(response + 14));
    g_assert_cmphex(modes_linear,
                    ==, IA64_INT10_ROM_BASE + IA64_INT10_ROM_MODES_OFFSET);
    g_assert_cmphex(qtest_readw(qts, modes_linear), ==, 0x111);
    g_assert_cmphex(int10_far_to_linear(ldl_le_p(response + 6)),
                    ==, IA64_INT10_ROM_BASE + IA64_INT10_ROM_OEM_OFFSET);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f01;
    regs.cx = 0x144;
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(lduw_le_p(response) & 0x80, !=, 0);
    g_assert_cmphex(lduw_le_p(response + 16), ==, 4096);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 1024);
    g_assert_cmphex(lduw_le_p(response + 20), ==, 768);
    g_assert_cmphex(response[25], ==, 32);
    g_assert_cmphex(response[28], ==, 0);
    g_assert_cmphex((uint32_t)ldl_le_p(response + 40), ==, 0xc4000000U);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f02;
    regs.bx = 0xc143;
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 800);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 600);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_ENABLE) & 0xc1,
                    ==, 0xc1);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f03;
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 0xc143);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f05;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x034f);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f06;
    regs.bx = 0;
    regs.cx = 801;
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);
    g_assert_cmphex(regs.dx, ==, memory_size / 3232);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f06;
    regs.bx = 2;
    regs.cx = 3201;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f06;
    regs.cx = VBE_DISPI_MAX_XRES + 1;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x024f);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f06;
    regs.bx = 1;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / 600 / 4) & ~7U;
    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f06;
    regs.bx = 3;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, max_width * 4);
    g_assert_cmphex(regs.cx, ==, max_width);
    g_assert_cmphex(regs.dx, ==, memory_size / (max_width * 4));

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f15;
    regs.bx = 1;
    length = int10_call(qts, &regs,
                        response, sizeof(response));
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[0], ==, 0x00);
    g_assert_cmphex(response[1], ==, 0xff);
    for (i = 0; i < length; i++) {
        checksum += response[i];
    }
    g_assert_cmphex(checksum & 0xff, ==, 0);
    qtest_quit(qts);
}

static void test_int10_vbe(void)
{
    test_int10_vbe_for_device(NULL);
}

static void test_int10_vbe_std(void)
{
    test_int10_vbe_for_device("-vga std");
}

static bool int10_mode_list_contains(QTestState *qts, uint16_t expected)
{
    uint64_t address = IA64_INT10_ROM_BASE + IA64_INT10_ROM_MODES_OFFSET;
    size_t i;

    for (i = 0; i < (IA64_INT10_ROM_SIZE - IA64_INT10_ROM_MODES_OFFSET) / 2;
         i++, address += 2) {
        uint16_t mode = qtest_readw(qts, address);

        if (mode == expected) {
            return true;
        }
        if (mode == 0xffff) {
            return false;
        }
    }
    g_assert_not_reached();
}

static void assert_edid_checksum(const uint8_t *edid)
{
    unsigned int checksum = 0;
    size_t i;

    for (i = 0; i < 128; i++) {
        checksum += edid[i];
    }
    g_assert_cmphex(checksum & 0xff, ==, 0);
}

static size_t int10_read_edid_block(QTestState *qts, uint16_t block,
                                    TestInt10Registers *regs,
                                    uint8_t response[128])
{
    *regs = (TestInt10Registers) {
        .ax = 0x4f15,
        .bx = 1,
        .dx = block,
    };
    return int10_call(qts, regs, response, 128);
}

static void test_int10_vbe_4k_for_device(const char *extra_args,
                                         uint16_t mode_number)
{
    uint8_t response[512];
    TestInt10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = IA64_VBE2_SIGNATURE,
    };
    QTestState *qts = ia64_vpc_start(extra_args);
    size_t length;

    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 512);
    g_assert_true(int10_mode_list_contains(qts, mode_number));

    regs = (TestInt10Registers) {
        .ax = 0x4f01,
        .cx = mode_number,
    };
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(lduw_le_p(response + 16), ==, 3840 * 4);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 3840);
    g_assert_cmphex(lduw_le_p(response + 20), ==, 2160);
    g_assert_cmphex(response[25], ==, 32);

    regs = (TestInt10Registers) {
        .ax = 0x4f02,
        .bx = 0x4000 | mode_number,
    };
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 3840);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 2160);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);

    length = int10_read_edid_block(qts, 0, &regs, response);
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[126], ==, 2);
    assert_edid_checksum(response);

    length = int10_read_edid_block(qts, 2, &regs, response);
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[0], ==, 0x70);
    g_assert_cmpuint(lduw_le_p(response + 12) + 1, ==, 3840);
    g_assert_cmpuint(lduw_le_p(response + 20) + 1, ==, 2160);
    assert_edid_checksum(response);

    length = int10_read_edid_block(qts, 3, &regs, response);
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x014f);
    qtest_quit(qts);
}

static void test_int10_vbe_4k(void)
{
    test_int10_vbe_4k_for_device(
        "-vga ati "
        "-global ati-vga.xres=3840 -global ati-vga.yres=2160 "
        "-global ati-vga.vgamem_mb=32",
        0x181);
}

static void test_int10_vbe_4k_std(void)
{
    test_int10_vbe_4k_for_device(
        "-vga std "
        "-global VGA.xres=3840 -global VGA.yres=2160 "
        "-global VGA.vgamem_mb=32",
        0x181);
}

static void test_int10_vbe_native_mode(void)
{
    uint8_t response[256];
    TestInt10Registers regs = {
        .ax = 0x4f01,
        .cx = 0x1f2,
    };
    QTestState *qts = ia64_vpc_start(
        "-vga ati "
        "-global ati-vga.xres=1928 -global ati-vga.yres=1080");
    size_t length;

    g_assert_true(int10_mode_list_contains(qts, 0x1f2));
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 1928);
    g_assert_cmphex(lduw_le_p(response + 20), ==, 1080);
    g_assert_cmphex(response[25], ==, 32);
    qtest_quit(qts);
}

static void test_int10_vbe_maximum(void)
{
    QTestState *qts = ia64_vpc_start(
        "-vga ati "
        "-global ati-vga.xres=1920 -global ati-vga.yres=1080 "
        "-global ati-vga.xmax=3840 -global ati-vga.ymax=2160 "
        "-global ati-vga.vgamem_mb=32");

    g_assert_true(int10_mode_list_contains(qts, 0x169));
    g_assert_true(int10_mode_list_contains(qts, 0x181));
    g_assert_false(int10_mode_list_contains(qts, 0x184));
    qtest_quit(qts);
}

static void test_int10_vbe_5k_edid(void)
{
    uint8_t response[512];
    TestInt10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = IA64_VBE2_SIGNATURE,
    };
    QTestState *qts = ia64_vpc_start(
        "-vga ati "
        "-global ati-vga.xres=5120 -global ati-vga.yres=2880 "
        "-global ati-vga.vgamem_mb=64");
    size_t length;

    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 1024);
    g_assert_true(int10_mode_list_contains(qts, 0x187));

    regs = (TestInt10Registers) {
        .ax = 0x4f01,
        .cx = 0x187,
    };
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(lduw_le_p(response + 16), ==, 5120 * 4);
    g_assert_cmphex(lduw_le_p(response + 18), ==, 5120);
    g_assert_cmphex(lduw_le_p(response + 20), ==, 2880);
    g_assert_cmphex(response[25], ==, 32);

    regs = (TestInt10Registers) {
        .ax = 0x4f02,
        .bx = 0x4187,
    };
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 5120);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 2880);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);

    length = int10_read_edid_block(qts, 0, &regs, response);
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[126], ==, 2);
    assert_edid_checksum(response);

    length = int10_read_edid_block(qts, 1, &regs, response);
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    assert_edid_checksum(response);

    length = int10_read_edid_block(qts, 2, &regs, response);
    g_assert_cmpuint(length, ==, 128);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[0], ==, 0x70);
    g_assert_cmpuint(lduw_le_p(response + 12) + 1, ==, 5120);
    g_assert_cmpuint(lduw_le_p(response + 20) + 1, ==, 2880);
    assert_edid_checksum(response);

    length = int10_read_edid_block(qts, 3, &regs, response);
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x014f);
    qtest_quit(qts);
}

static void assert_vga_start_fails(const char *global_property,
                                   const char *message)
{
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", "ia64-vpc,nvram=none",
        "-vga", "ati",
        "-global", global_property,
        "-display", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void test_int10_vbe_invalid_properties(void)
{
    assert_vga_start_fails("ati-vga.xres=1366",
                           "not a multiple of 8 required by Bochs VBE");
    assert_vga_start_fails("ati-vga.xmax=3840",
                           "xmax and ymax must be set together");
    assert_vga_start_fails("ati-vga.vgamem_mb=128",
                           "not addressable through the IA-64 framebuffer");
    assert_vga_start_fails("ati-vga.x-linear-aper-size=100663296",
                           "must be a power of two");

    /* Use a complete property set for validation that happens after realize. */
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", "ia64-vpc,nvram=none",
        "-vga", "ati",
        "-global", "ati-vga.xres=3840",
        "-global", "ati-vga.yres=2160",
        "-global", "ati-vga.vgamem_mb=16",
        "-display", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, "set vgamem_mb=32 or larger"));
}

static void test_int10_legacy_for_device(const char *extra_args)
{
    uint8_t response[2];
    uint8_t marker[16];
    uint8_t actual[sizeof(marker)];
    uint8_t zero[sizeof(marker)] = { 0 };
    TestInt10Registers regs;
    QTestState *qts = ia64_vpc_start(extra_args);
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;
    size_t length;

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f02;
    regs.bx = 0x4143;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);

    memset(marker, 0xa5, sizeof(marker));
    qtest_memwrite(qts, IA64_VGA_FB_BASE, marker, sizeof(marker));
    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x0012;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_ENABLE), ==, 0);
    g_assert_cmphex(test_vga_indexed_read(qts, VGA_SEQ_I, VGA_SEQ_D,
                                          VGA_SEQ_MEMORY_MODE), ==, 0x06);
    g_assert_cmphex(test_vga_indexed_read(qts, VGA_CRT_IC, VGA_CRT_DC,
                                          VGA_CRTC_H_DISP), ==, 0x4f);
    g_assert_cmphex(test_vga_indexed_read(qts, VGA_CRT_IC, VGA_CRT_DC,
                                          VGA_CRTC_V_DISP_END), ==, 0xdf);
    g_assert_cmphex(test_vga_indexed_read(qts, VGA_GFX_I, VGA_GFX_D,
                                          VGA_GFX_MISC), ==, 0x05);
    qtest_memread(qts, IA64_VGA_FB_BASE, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    g_assert_cmphex(qtest_readb(qts, IA64_BDA_VIDEO_MODE), ==, 0x12);
    g_assert_cmphex(qtest_readw(qts, IA64_BDA_VIDEO_COLUMNS), ==, 80);
    g_assert_cmphex(qtest_readw(qts, IA64_BDA_VIDEO_PAGE_SIZE), ==,
                    0xa000);
    g_assert_cmphex(qtest_readb(qts, IA64_BDA_VIDEO_ROWS), ==, 29);
    g_assert_cmphex(qtest_readw(qts, IA64_BDA_CHARACTER_HEIGHT), ==, 16);
    g_assert_cmphex(qtest_readb(qts, IA64_BDA_VIDEO_CONTROL), ==, 0x60);

    /* Exercise a planar byte-write path and verify actual scanout. */
    qtest_writeb(qts, IA64_VGA_LEGACY_BASE, 0xff);
    tmpdir = g_dir_make_tmp("ia64-int10-legacy-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "mode12.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    test_assert_ppm_pixel(ppm, 640, 480, 0, 0, 0xff, 0xff, 0xff);
    test_assert_ppm_pixel(ppm, 640, 480, 8, 0, 0, 0, 0);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x0f00;
    regs.bx = 0xabcd;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x5012);
    g_assert_cmphex(regs.bx, ==, 0x00cd);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f02;
    regs.bx = 0x4143;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    memset(marker, 0x5a, sizeof(marker));
    qtest_memwrite(qts, IA64_VGA_FB_BASE, marker, sizeof(marker));

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x0092;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    qtest_memread(qts, IA64_VGA_FB_BASE, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), marker, sizeof(marker));
    g_assert_cmphex(qtest_readb(qts, IA64_BDA_VIDEO_CONTROL), ==, 0xe0);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x0f00;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x5012);

    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x4f02;
    regs.bx = 0x4143;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 800);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 600);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_ENABLE) & 0x41,
                    ==, 0x41);
    memset(&regs, 0, sizeof(regs));
    regs.ax = 0x0f00;
    length = int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x5003);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_int10_legacy(void)
{
    test_int10_legacy_for_device(NULL);
}

static void test_int10_legacy_std(void)
{
    test_int10_legacy_for_device("-vga std");
}

static void test_acpi_reset_register(void)
{
    QTestState *qts = ia64_vpc_start(NULL);

    qtest_writeb(qts,
                 IA64_LEGACY_IO_PORT_PA(IA64_ACPI_PM_IO_BASE +
                                        IA64_ACPI_PM_RESET_OFFSET),
                 IA64_ACPI_PM_RESET_VALUE);
    qtest_qmp_eventwait(qts, "RESET");
    qtest_quit(qts);
}

static void assert_firmware_handoff(QTestState *qts, uint64_t i8042,
                                    uint64_t cpus, uint64_t nvram,
                                    uint64_t sockets, uint64_t cores,
                                    uint64_t threads,
                                    uint64_t compat_flags)
{
    IA64VpcHandoff handoff;
    IA64VpcCompatHandoff compat;

    g_assert_cmpuint(sizeof(handoff), ==, 120);
    qtest_memread(qts, IA64_FW_HANDOFF_ADDR, &handoff, sizeof(handoff));
    g_assert_cmphex(le64_to_cpu(handoff.Magic), ==, IA64_FW_HANDOFF_MAGIC);
    g_assert_cmphex(le64_to_cpu(handoff.Version), ==,
                    IA64_FW_HANDOFF_VERSION);
    g_assert_cmphex(le64_to_cpu(handoff.RamSize), ==, IA64_TEST_RAM_SIZE);
    g_assert_cmphex(le64_to_cpu(handoff.ConsolePolicy), ==,
                    IA64_FW_CONSOLE_VGA);
    g_assert_cmphex(le64_to_cpu(handoff.IdeDmaEnabled), ==, 1);
    g_assert_cmphex(le64_to_cpu(handoff.DebugPortFlags), ==, 0);
    g_assert_cmphex(le64_to_cpu(handoff.DebugPortBase), ==, 0);
    g_assert_cmphex(le64_to_cpu(handoff.I8042Enabled), ==, i8042);
    g_assert_cmphex(le64_to_cpu(handoff.ProcessorCount), ==, cpus);
    g_assert_cmphex(le64_to_cpu(handoff.NvramPersistent), ==, nvram);
    g_assert_cmphex(le64_to_cpu(handoff.SocketCount), ==, sockets);
    g_assert_cmphex(le64_to_cpu(handoff.CoresPerSocket), ==, cores);
    g_assert_cmphex(le64_to_cpu(handoff.ThreadsPerCore), ==, threads);
    g_assert_cmphex(le64_to_cpu(handoff.RasBase), ==,
                    IA64_RAS_HUB_DEFAULT_BASE);
    g_assert_cmphex(le64_to_cpu(handoff.RasSize), ==,
                    IA64_RAS_HUB_SIZE);

    qtest_memread(qts, IA64_FW_COMPAT_HANDOFF_ADDR,
                  &compat, sizeof(compat));
    g_assert_cmphex(le64_to_cpu(compat.Magic), ==,
                    IA64_FW_COMPAT_HANDOFF_MAGIC);
    g_assert_cmphex(le64_to_cpu(compat.Version), ==,
                    IA64_FW_COMPAT_HANDOFF_VERSION);
    g_assert_cmphex(le64_to_cpu(compat.Size), ==, sizeof(compat));
    g_assert_cmphex(le64_to_cpu(compat.Flags), ==, compat_flags);
}

static void test_firmware_handoff_defaults(void)
{
    static const uint8_t expected_v11[sizeof(IA64VpcHandoff)] = {
        0x51, 0x49, 0x41, 0x36, 0x34, 0x52, 0x41, 0x4d, /* Magic */
        0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Version */
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, /* RamSize */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ConsolePolicy */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* IdeDmaEnabled */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* DebugPortFlags */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* DebugPortBase */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* I8042Enabled */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ProcessorCount */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* NvramPersistent */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SocketCount */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* CoresPerSocket */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ThreadsPerCore */
        0x00, 0x00, 0x80, 0xfe, 0x00, 0x00, 0x00, 0x00, /* RasBase */
        0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, /* RasSize */
    };
    uint8_t actual[sizeof(IA64VpcHandoff)];
    QTestState *qts = ia64_vpc_handoff_start("ia64-vpc", NULL);

    assert_firmware_handoff(qts, 0, 1, 1, 1, 1, 1, 0);
    qtest_memread(qts, IA64_FW_HANDOFF_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    expected_v11, sizeof(expected_v11));
    qtest_quit(qts);
}

static void test_firmware_handoff_i8042_on(void)
{
    QTestState *qts =
        ia64_vpc_handoff_start("ia64-vpc,i8042=on", NULL);

    assert_firmware_handoff(qts, 1, 1, 1, 1, 1, 1, 0);
    qtest_quit(qts);
}

static void test_smp_topology(gconstpointer opaque)
{
    uint64_t count = GPOINTER_TO_UINT(opaque);
    g_autofree char *args = g_strdup_printf("-smp %" PRIu64, count);
    QTestState *qts = ia64_vpc_handoff_start("ia64-vpc", args);
    g_autoptr(QDict) response = NULL;
    QList *cpus;

    assert_firmware_handoff(qts, 0, count, 1, count, 1, 1, 0);
    response = qtest_qmp(qts, "{'execute':'query-cpus-fast'}");
    g_assert(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    g_assert_cmpuint(qlist_size(cpus), ==, count);
    qtest_quit(qts);
}

static void test_smp_explicit_topology(void)
{
    QTestState *qts = ia64_vpc_handoff_start(
        "ia64-vpc", "-smp 4,sockets=1,cores=2,threads=2");

    assert_firmware_handoff(qts, 0, 4, 1, 1, 2, 2, 0);
    qtest_quit(qts);
}

typedef struct TestSmpMulticoreTopology {
    const char *name;
    unsigned sockets;
    unsigned cores;
} TestSmpMulticoreTopology;

static const TestSmpMulticoreTopology smp_multicore_topologies[] = {
    { "8-sockets-2-cores", 8, 2 },
    { "1-socket-8-cores", 1, 8 },
    { "4-sockets-8-cores", 4, 8 },
};

static void test_smp_multicore_topology(gconstpointer opaque)
{
    const TestSmpMulticoreTopology *topology = opaque;
    unsigned count = topology->sockets * topology->cores;
    g_autofree char *args = g_strdup_printf(
        "-smp %u,sockets=%u,cores=%u,threads=1",
        count, topology->sockets, topology->cores);
    QTestState *qts = ia64_vpc_handoff_start("ia64-vpc", args);
    g_autoptr(QDict) response = NULL;
    QList *cpus;

    assert_firmware_handoff(qts, 0, count, 1, topology->sockets,
                            topology->cores, 1, 0);
    response = qtest_qmp(qts, "{'execute':'query-cpus-fast'}");
    g_assert(qdict_haskey(response, "return"));
    cpus = qdict_get_qlist(response, "return");
    g_assert_cmpuint(qlist_size(cpus), ==, count);
    qtest_quit(qts);
}

static void test_machine_firmware_profiles(void)
{
    QTestState *qts;

    qts = ia64_vpc_handoff_start("itanium2-vpc", "-cpu merced");
    assert_firmware_handoff(qts, 0, 1, 1, 1, 1, 1, 0);
    qtest_quit(qts);

    qts = ia64_vpc_handoff_start("itanium-vpc", "-cpu montecito");
    assert_firmware_handoff(qts, 1, 1, 1, 1, 1, 1,
                            IA64_FW_COMPAT_ALL_MASK);
    qtest_quit(qts);
}

static void test_machine_default_ram(void)
{
    static const char *const machines[] = {
        "itanium-vpc",
        "itanium2-vpc",
    };
    IA64VpcHandoff handoff;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(machines); i++) {
        QTestState *qts =
            qtest_initf("-machine %s,nvram=none -S", machines[i]);

        qtest_memread(qts, IA64_FW_HANDOFF_ADDR,
                      &handoff, sizeof(handoff));
        g_assert_cmphex(le64_to_cpu(handoff.RamSize), ==, 2 * GiB);
        qtest_quit(qts);
    }
}

static void test_smp_full_alat(void)
{
    g_autoptr(QDict) response = NULL;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,alat=full,nvram=none -smp 2 -S");

    response = qtest_qmp(
        qts, "{'execute':'qom-get','arguments':"
             "{'path':'/machine','property':'alat'}}");
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, "full");
    qtest_quit(qts);
}

static void test_alat_active_writer_window(void)
{
    uint64_t active_alloc_count;
    bool active_alloc_hit;
    bool active_hit;
    bool setup_hit;

    for (unsigned int cpus = 1; cpus <= 2; cpus++) {
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,alat=full,nvram=none -m 4G -smp %u -S "
            "-accel tcg,thread=multi", cpus);

        qtest_ia64_alat_active_writer(qts, &setup_hit, &active_hit,
                                      &active_alloc_count, &active_alloc_hit);
        g_assert_true(setup_hit);
        g_assert_false(active_hit);
        g_assert_cmpuint(active_alloc_count, ==, 0);
        g_assert_false(active_alloc_hit);
        qtest_quit(qts);
    }
}

static void test_alat_smp_writer(void)
{
    bool memory_write_ok;
    bool setup_hit;
    bool smp_hit;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,alat=full,nvram=none -m 4G -smp 2 -S "
        "-accel tcg,thread=multi");

    qtest_ia64_alat_smp_writer(qts, &setup_hit, &memory_write_ok, &smp_hit);
    g_assert_true(setup_hit);
    g_assert_true(memory_write_ok);
    g_assert_false(smp_hit);

    qtest_quit(qts);
}

static void test_machine_defaults_to_zero_alat(void)
{
    static const char *const machines[] = {
        "ia64-vpc", "itanium-vpc", "itanium2-vpc",
        "hp-i2000", "hp-zx6000", "hp-rx2660",
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(machines); i++) {
        g_autoptr(QDict) response = NULL;
        QTestState *qts;

        if (!qtest_has_machine(machines[i])) {
            continue;
        }
        qts = qtest_initf("-machine %s,nvram=none -m 2G -S",
                          machines[i]);
        response = qtest_qmp(
            qts, "{'execute':'qom-get','arguments':"
                 "{'path':'/machine','property':'alat'}}");
        g_assert_cmpstr(qdict_get_str(response, "return"), ==, "zero");
        qtest_quit(qts);
    }
}

static bool rtc_value_is_current(uint64_t value)
{
    int64_t now = time(NULL);

    return value >= now - 5 && value <= now + 5;
}

static void test_rtc_aligned_read(void)
{
    QTestState *qts = ia64_vpc_start(NULL);
    uint64_t before_write;
    uint64_t after_write;
    uint64_t after_reset;

    before_write = qtest_readq(qts, IA64_RTC_BASE);
    g_assert_true(rtc_value_is_current(before_write));

    /* The RTC window is read-only. */
    qtest_writeq(qts, IA64_RTC_BASE, UINT64_MAX);
    after_write = qtest_readq(qts, IA64_RTC_BASE);
    g_assert_true(rtc_value_is_current(after_write));

    qtest_system_reset(qts);
    after_reset = qtest_readq(qts, IA64_RTC_BASE);
    g_assert_true(rtc_value_is_current(after_reset));
    qtest_quit(qts);
}

static void test_nvram_commit_and_restart(void)
{
    const uint64_t test_value = 0x1122334455667788ULL;
    const uint64_t second_value = 0x8877665544332211ULL;
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *contents = NULL;
    g_autofree char *updated_contents = NULL;
    g_autofree char *zero_tail =
        g_malloc0(IA64_NVRAM_EXTENDED_FILE_SIZE - IA64_NVRAM_SIZE);
    g_autoptr(GError) error = NULL;
    gsize length = 0;
    QTestState *qts;

    tmpdir = g_dir_make_tmp("ia64-vpc-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);

    qts = ia64_vpc_nvram_start(quoted_path);
    qtest_writeq(qts, IA64_NVRAM_BASE, test_value);
    qtest_writeq(qts, IA64_NVRAM_BASE + IA64_NVRAM_COMMIT_OFFSET,
                 IA64_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_EXTENDED_FILE_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, test_value);
    g_assert_cmpmem(contents + IA64_NVRAM_SIZE,
                    IA64_NVRAM_EXTENDED_FILE_SIZE - IA64_NVRAM_SIZE,
                    zero_tail,
                    IA64_NVRAM_EXTENDED_FILE_SIZE - IA64_NVRAM_SIZE);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, test_value);
    qtest_writeq(qts, IA64_NVRAM_BASE, second_value);
    qtest_writeq(qts, IA64_NVRAM_BASE + IA64_NVRAM_COMMIT_OFFSET,
                 IA64_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &updated_contents, &length,
                                      &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_EXTENDED_FILE_SIZE);
    g_assert_cmphex(ldq_le_p(updated_contents), ==, second_value);
    g_assert_cmpmem(updated_contents + IA64_NVRAM_SIZE,
                    IA64_NVRAM_EXTENDED_FILE_SIZE - IA64_NVRAM_SIZE,
                    zero_tail,
                    IA64_NVRAM_EXTENDED_FILE_SIZE - IA64_NVRAM_SIZE);

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_nvram_empty_file(void)
{
    const uint64_t test_value = UINT64_C(0x1020304050607080);
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = 0;
    QTestState *qts;

    tmpdir = g_dir_make_tmp("ia64-vpc-empty-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    g_assert_true(g_file_set_contents(path, "", 0, &error));
    g_assert_no_error(error);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 0);
    g_clear_pointer(&contents, g_free);
    qtest_writeq(qts, IA64_NVRAM_BASE, test_value);
    qtest_writeq(qts, IA64_NVRAM_BASE + IA64_NVRAM_COMMIT_OFFSET,
                 IA64_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_EXTENDED_FILE_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, test_value);

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_nvram_legacy_file(void)
{
    const uint64_t initial_value = 0x0123456789abcdefULL;
    const uint64_t committed_value = 0xfedcba9876543210ULL;
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *expected = g_malloc0(IA64_NVRAM_SIZE);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = 0;
    QTestState *qts;

    stq_le_p(expected, initial_value);
    memset(expected + sizeof(initial_value), 0x5a,
           IA64_NVRAM_SIZE - sizeof(initial_value));
    tmpdir = g_dir_make_tmp("ia64-vpc-legacy-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    g_assert_true(g_file_set_contents(path, expected, IA64_NVRAM_SIZE,
                                      &error));
    g_assert_no_error(error);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, initial_value);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_SIZE);
    g_assert_cmpmem(contents, length, expected, IA64_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qtest_writeq(qts, IA64_NVRAM_BASE, committed_value);
    qtest_writeq(qts, IA64_NVRAM_BASE + IA64_NVRAM_COMMIT_OFFSET,
                 IA64_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    stq_le_p(expected, committed_value);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_SIZE);
    g_assert_cmpmem(contents, length, expected, IA64_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, committed_value);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_nvram_extended_file(void)
{
    const uint64_t initial_value = 0x0123456789abcdefULL;
    const uint64_t committed_value = 0xfedcba9876543210ULL;
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *expected =
        g_malloc(IA64_NVRAM_EXTENDED_FILE_SIZE);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = 0;
    QTestState *qts;

    memset(expected, 0xa5, IA64_NVRAM_EXTENDED_FILE_SIZE);
    stq_le_p(expected, initial_value);
    tmpdir = g_dir_make_tmp("ia64-vpc-extended-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    g_assert_true(g_file_set_contents(path, expected,
                                      IA64_NVRAM_EXTENDED_FILE_SIZE,
                                      &error));
    g_assert_no_error(error);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, initial_value);
    qtest_writeq(qts, IA64_NVRAM_BASE, committed_value);
    qtest_writeq(qts, IA64_NVRAM_BASE + IA64_NVRAM_COMMIT_OFFSET,
                 IA64_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    stq_le_p(expected, committed_value);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_NVRAM_EXTENDED_FILE_SIZE);
    g_assert_cmpmem(contents, length,
                    expected, IA64_NVRAM_EXTENDED_FILE_SIZE);

    qts = ia64_vpc_nvram_start(quoted_path);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, committed_value);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void ia64_qpci_init(QGenericPCIBus *gbus, QTestState *qts)
{
    qpci_init_generic(gbus, qts, NULL, false);
    gbus->ecam_alloc_ptr = IA64_PCI_CONFIG_BASE;
    gbus->gpex_pio_base = IA64_LEGACY_IO_BASE;
}

static void assert_pci_device(QPCIBus *bus, const ExpectedPCIDevice *expected)
{
    QPCIDevice *dev = qpci_device_find(bus,
                                       QPCI_DEVFN(expected->slot, 0));
    unsigned bar;

    g_assert_nonnull(dev);
    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==,
                    expected->vendor);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==,
                    expected->device);
    g_assert_cmphex(qpci_config_readw(dev, PCI_COMMAND), ==,
                    expected->command);
    g_assert_cmphex(qpci_config_readb(dev, PCI_INTERRUPT_LINE), ==,
                    expected->irq_line);
    g_assert_cmphex(qpci_config_readb(dev, PCI_INTERRUPT_PIN), ==,
                    expected->irq_pin);
    for (bar = 0; bar < ARRAY_SIZE(expected->bars); bar++) {
        g_assert_cmphex(qpci_config_readl(dev,
                                         PCI_BASE_ADDRESS_0 + bar * 4),
                        ==, expected->bars[bar]);
    }
    g_free(dev);
}

static void count_pci_device(QPCIDevice *dev, int devfn, void *opaque)
{
    unsigned int *count = opaque;

    (void)devfn;
    (*count)++;
    g_free(dev);
}

static unsigned int pci_device_count(QPCIBus *bus, uint16_t vendor,
                                     uint16_t device)
{
    unsigned int count = 0;

    qpci_device_foreach(bus, vendor, device, count_pci_device, &count);
    return count;
}

static void test_pci_itanium_no_default_ahci(void)
{
    QTestState *qts =
        qtest_init("-machine itanium-vpc,nvram=none -m 256M -S");
    QGenericPCIBus gbus;
    unsigned int function;

    ia64_qpci_init(&gbus, qts);
    g_assert_cmpuint(pci_device_count(&gbus.bus, PCI_VENDOR_ID_INTEL,
                                     0x2922), ==, 0);
    for (function = 0; function < 8; function++) {
        QPCIDevice *empty =
            qpci_device_find(&gbus.bus, QPCI_DEVFN(1, function));

        g_assert_null(empty);
    }
    qtest_quit(qts);
}

static void test_pci_default_layout(void)
{
    static const ExpectedPCIDevice devices[] = {
        {
            .slot = 1, .vendor = 0x8086, .device = 0x2922,
            .command = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                       PCI_COMMAND_MASTER,
            .irq_line = 17, .irq_pin = 1,
            .bars = { [4] = 0x0000c101, [5] = 0xc1020000 },
        }, {
            .slot = 2, .vendor = 0x106b, .device = 0x003f,
            .command = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            .irq_line = 18, .irq_pin = 1,
            .bars = { [0] = 0xc1010000 },
        }, {
            .slot = 3, .vendor = 0x8086, .device = 0x7020,
            .command = PCI_COMMAND_IO | PCI_COMMAND_MASTER,
            .irq_line = 18, .irq_pin = 4,
            .bars = { [4] = 0x0000c121 },
        }, {
            .slot = 4, .vendor = 0x1000, .device = 0x0012,
            .command = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                       PCI_COMMAND_MASTER,
            .irq_line = 16, .irq_pin = 1,
            .bars = {
                [0] = 0x0000c201,
                [1] = 0xc1030000,
                [2] = 0xc1032000,
            },
        }, {
            .slot = 5, .vendor = 0x1002, .device = 0x5046,
            .command = PCI_COMMAND_IO | PCI_COMMAND_MEMORY,
            .irq_line = 17, .irq_pin = 1,
            .bars = {
                [0] = 0xc4000008,
                [1] = 0x0000c301,
                [2] = 0xc8000000,
            },
        },
    };
    QTestState *qts = ia64_vpc_start(NULL);
    QGenericPCIBus gbus;
    unsigned i;

    ia64_qpci_init(&gbus, qts);
    for (i = 0; i < 8; i++) {
        QPCIDevice *empty = qpci_device_find(&gbus.bus, QPCI_DEVFN(0, i));

        g_assert_null(empty);
    }
    for (i = 0; i < ARRAY_SIZE(devices); i++) {
        assert_pci_device(&gbus.bus, &devices[i]);
    }
    {
        QPCIDevice *lsi = qpci_device_find(&gbus.bus, QPCI_DEVFN(4, 0));

        g_assert_nonnull(lsi);
        g_assert_cmphex(qpci_config_readw(lsi, PCI_SUBSYSTEM_VENDOR_ID), ==,
                        PCI_VENDOR_ID_LSI_LOGIC);
        g_assert_cmphex(qpci_config_readw(lsi, PCI_SUBSYSTEM_ID), ==,
                        PCI_VENDOR_ID_LSI_LOGIC);
        g_free(lsi);
    }
    assert_pci_device(&gbus.bus, &expected_e1000);
    qtest_quit(qts);
}

static void test_pci_es1000_model(void)
{
    QTestState *qts = ia64_vpc_start(
        "-vga ati -global ati-vga.model=es1000");
    QGenericPCIBus gbus;
    QPCIDevice *vga;
    uint32_t saved_bar;
    uint8_t signature[4];

    ia64_qpci_init(&gbus, qts);
    vga = qpci_device_find(&gbus.bus, QPCI_DEVFN(5, 0));
    g_assert_nonnull(vga);
    g_assert_cmphex(qpci_config_readw(vga, PCI_VENDOR_ID), ==, 0x1002);
    g_assert_cmphex(qpci_config_readw(vga, PCI_DEVICE_ID), ==, 0x515e);
    g_assert_cmphex(qpci_config_readb(vga, PCI_REVISION_ID), ==, 0x02);
    g_assert_cmphex(qpci_config_readw(vga, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_DISPLAY_VGA);
    g_assert_cmphex(qpci_config_readw(vga, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    0x103c);
    g_assert_cmphex(qpci_config_readw(vga, PCI_SUBSYSTEM_ID), ==, 0x31fb);
    g_assert_cmphex(qpci_config_readb(vga, PCI_CACHE_LINE_SIZE), ==, 0x10);
    g_assert_cmphex(qpci_config_readb(vga, PCI_LATENCY_TIMER), ==, 0x40);
    g_assert_cmphex(qpci_config_readb(vga, PCI_MIN_GNT), ==, 0x08);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_0), ==,
                    IA64_VGA_LARGE_FB_BASE |
                    PCI_BASE_ADDRESS_MEM_PREFETCH);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_2), ==,
                    IA64_VGA_LARGE_MMIO_BASE);

    saved_bar = qpci_config_readl(vga, PCI_BASE_ADDRESS_0);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_0, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_0), ==,
                    0xf8000008U);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_0, saved_bar);
    saved_bar = qpci_config_readl(vga, PCI_BASE_ADDRESS_1);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_1, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_1), ==,
                    0xffffff01U);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_1, saved_bar);
    saved_bar = qpci_config_readl(vga, PCI_BASE_ADDRESS_2);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_2, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_2), ==,
                    0xffff0000U);
    qpci_config_writel(vga, PCI_BASE_ADDRESS_2, saved_bar);

    g_assert_cmphex(qpci_config_readb(vga, PCI_CAPABILITY_LIST), ==, 0x50);
    g_assert_cmphex(qpci_config_readb(vga, 0x50), ==, PCI_CAP_ID_PM);
    g_assert_cmphex(qpci_config_readb(vga, 0x51), ==, 0x00);
    g_assert_cmphex(qpci_config_readw(vga, 0x50 + PCI_PM_PMC), ==, 0x0602);
    g_assert_cmphex(qpci_config_readw(vga, 0x50 + PCI_PM_CTRL), ==, 0x0000);

    saved_bar = qpci_config_readl(vga, PCI_ROM_ADDRESS);
    qpci_config_writel(vga, PCI_ROM_ADDRESS, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(vga, PCI_ROM_ADDRESS), ==,
                    0xfffe0001U);
    qpci_config_writel(vga, PCI_ROM_ADDRESS,
                       IA64_ATI_TEST_ROM_BASE | PCI_ROM_ADDRESS_ENABLE);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE), ==, 0xaa55);
    {
        uint16_t pcir = qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + 0x18);

        qtest_memread(qts, IA64_ATI_TEST_ROM_BASE + pcir,
                      signature, sizeof(signature));
        g_assert_cmpmem(signature, sizeof(signature), "PCIR", 4);
        g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + pcir + 4),
                        ==, IA64_ATI_VENDOR_ID);
        g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + pcir + 6),
                        ==, IA64_ATI_ES1000_DEVICE_ID);
    }
    qpci_config_writel(vga, PCI_ROM_ADDRESS, saved_bar);
    g_free(vga);
    qtest_quit(qts);
}

static void test_pci_rv100_model(void)
{
    QTestState *qts = ia64_vpc_start(
        "-vga ati -global ati-vga.model=rv100");
    QGenericPCIBus gbus;
    QPCIDevice *vga;
    uint32_t saved_bar;
    uint16_t pcir;
    uint8_t signature[4];

    ia64_qpci_init(&gbus, qts);
    vga = qpci_device_find(&gbus.bus, QPCI_DEVFN(5, 0));
    g_assert_nonnull(vga);
    g_assert_cmphex(qpci_config_readw(vga, PCI_VENDOR_ID), ==,
                    IA64_ATI_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(vga, PCI_DEVICE_ID), ==,
                    IA64_ATI_RV100_DEVICE_ID);
    g_assert_cmphex(qpci_config_readb(vga, PCI_REVISION_ID), ==, 0x00);
    g_assert_cmphex(qpci_config_readw(vga, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_DISPLAY_VGA);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_0), ==,
                    IA64_VGA_LARGE_FB_BASE |
                    PCI_BASE_ADDRESS_MEM_PREFETCH);
    g_assert_cmphex(qpci_config_readl(vga, PCI_BASE_ADDRESS_2), ==,
                    IA64_VGA_LARGE_MMIO_BASE);
    /* Keep the pre-existing RV100 PCI configuration migration-compatible. */
    g_assert_cmphex(qpci_config_readb(vga, PCI_CAPABILITY_LIST), ==, 0x00);

    saved_bar = qpci_config_readl(vga, PCI_ROM_ADDRESS);
    qpci_config_writel(vga, PCI_ROM_ADDRESS, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(vga, PCI_ROM_ADDRESS), ==,
                    0xffff0001U);
    qpci_config_writel(vga, PCI_ROM_ADDRESS,
                       IA64_ATI_TEST_ROM_BASE | PCI_ROM_ADDRESS_ENABLE);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE), ==, 0xaa55);
    pcir = qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + 0x18);
    qtest_memread(qts, IA64_ATI_TEST_ROM_BASE + pcir,
                  signature, sizeof(signature));
    g_assert_cmpmem(signature, sizeof(signature), "PCIR", 4);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + pcir + 4), ==,
                    IA64_ATI_VENDOR_ID);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_TEST_ROM_BASE + pcir + 6), ==,
                    IA64_ATI_RV100_DEVICE_ID);
    qpci_config_writel(vga, PCI_ROM_ADDRESS, saved_bar);
    g_free(vga);
    qtest_quit(qts);
}

static void test_e1000_resources_survive_reset(void)
{
    QTestState *qts = ia64_vpc_start(NULL);
    QGenericPCIBus gbus;

    ia64_qpci_init(&gbus, qts);
    assert_pci_device(&gbus.bus, &expected_e1000);
    qtest_system_reset(qts);
    assert_pci_device(&gbus.bus, &expected_e1000);
    qtest_quit(qts);
}

static void test_e1000_intx_route(void)
{
    const uint8_t vector = 0x52;
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + IA64_E1000_GSI * 2;
    QTestState *qts = ia64_vpc_start(NULL);

    iosapic_write(qts, rte_low, vector | IA64_IOSAPIC_RTE_LEVEL);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_IMC, UINT32_MAX);
    (void)qtest_readl(qts, IA64_E1000_MMIO_BASE + E1000_ICR);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_IMS,
                 E1000_IMS_TXDW);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_ICS,
                 E1000_ICS_TXDW);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);

    g_assert_cmphex(qtest_readl(qts, IA64_E1000_MMIO_BASE + E1000_ICR) &
                    E1000_ICR_TXDW, !=, 0);
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_EOI, vector);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, ==, 0);
    qtest_quit(qts);
}

static bool e1000_wait_tx_done(QTestState *qts, struct e1000_tx_desc *desc)
{
    int i;

    for (i = 0; i < IA64_E1000_TEST_TIMEOUT_MS; i++) {
        qtest_memread(qts, IA64_E1000_TX_DESC_ADDR, desc, sizeof(*desc));
        if (le32_to_cpu(desc->upper.data) & E1000_TXD_STAT_DD) {
            return true;
        }
        qtest_clock_step(qts, 1000);
        g_usleep(1000);
    }
    return false;
}

static bool e1000_wait_rx_done(QTestState *qts, struct e1000_rx_desc *desc)
{
    int i;

    for (i = 0; i < IA64_E1000_TEST_TIMEOUT_MS; i++) {
        qtest_memread(qts, IA64_E1000_RX_DESC_ADDR, desc, sizeof(*desc));
        if (desc->status & E1000_RXD_STAT_DD) {
            return true;
        }
        qtest_clock_step(qts, 1000);
        g_usleep(1000);
    }
    return false;
}

static bool socket_receive_all(int fd, void *buffer, size_t length)
{
    uint8_t *next = buffer;

    while (length != 0) {
        GPollFD poll_fd = {
            .fd = fd,
            .events = G_IO_IN,
        };
        ssize_t received;

        if (g_poll(&poll_fd, 1, IA64_E1000_TEST_TIMEOUT_MS) != 1 ||
            !(poll_fd.revents & G_IO_IN)) {
            return false;
        }
        received = recv(fd, next, length, 0);
        if (received <= 0) {
            return false;
        }
        next += received;
        length -= received;
    }
    return true;
}

static void test_e1000_packet_transfer(gconstpointer opaque)
{
    const char *model = opaque;
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x00, 0x32,
        0x12, 0x34, 0x00, 0x00, 0x40, 0x11,
        0x00, 0x00, 0x0a, 0x00, 0x02, 0x0f,
        0x0a, 0x00, 0x02, 0x02,
    };
    struct e1000_tx_desc tx_desc = { 0 };
    struct e1000_rx_desc rx_desc = { 0 };
    uint32_t frame_length;
    uint8_t received[sizeof(packet)];
    uint8_t rx_buffer[sizeof(packet)];
    g_autofree char *args = NULL;
    QTestState *qts;
    int sockets[2];

    g_assert_cmpint(qemu_socketpair(PF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    qemu_clear_cloexec(sockets[1]);
    args = g_strdup_printf("-nic socket,fd=%d,model=%s,"
                           "mac=52:54:00:12:34:56", sockets[1], model);
    qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M %s", args);
    close(sockets[1]);

    qtest_memwrite(qts, IA64_E1000_TX_BUFFER_ADDR, packet, sizeof(packet));
    tx_desc.buffer_addr = cpu_to_le64(IA64_E1000_TX_BUFFER_ADDR);
    tx_desc.lower.data = cpu_to_le32(sizeof(packet) | E1000_TXD_CMD_EOP |
                                    E1000_TXD_CMD_IFCS |
                                    E1000_TXD_CMD_RS);
    qtest_memwrite(qts, IA64_E1000_TX_DESC_ADDR,
                   &tx_desc, sizeof(tx_desc));
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TDBAL,
                 IA64_E1000_TX_DESC_ADDR);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TDBAH, 0);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TDLEN,
                 IA64_E1000_RING_SIZE);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TDH, 0);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TCTL,
                 E1000_TCTL_EN | E1000_TCTL_PSP);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_TDT, 1);

    g_assert_true(e1000_wait_tx_done(qts, &tx_desc));
    g_assert_true(socket_receive_all(sockets[0], &frame_length,
                                     sizeof(frame_length)));
    g_assert_cmpuint(ntohl(frame_length), ==, sizeof(packet));
    g_assert_true(socket_receive_all(sockets[0], received, sizeof(received)));
    g_assert_cmpmem(received, sizeof(received), packet, sizeof(packet));

    rx_desc.buffer_addr = cpu_to_le64(IA64_E1000_RX_BUFFER_ADDR);
    qtest_memwrite(qts, IA64_E1000_RX_DESC_ADDR,
                   &rx_desc, sizeof(rx_desc));
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RDBAL,
                 IA64_E1000_RX_DESC_ADDR);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RDBAH, 0);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RDLEN,
                 IA64_E1000_RING_SIZE);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RDH, 0);
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RCTL,
                 E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                 E1000_RCTL_BAM | E1000_RCTL_SECRC);
    frame_length = htonl(sizeof(packet));
    g_assert_cmpint(qemu_write_full(sockets[0], &frame_length,
                                    sizeof(frame_length)), ==,
                    sizeof(frame_length));
    g_assert_cmpint(qemu_write_full(sockets[0], packet, sizeof(packet)), ==,
                    sizeof(packet));
    qtest_writel(qts, IA64_E1000_MMIO_BASE + E1000_RDT, 1);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    g_assert_true(e1000_wait_rx_done(qts, &rx_desc));
    g_assert_cmpuint(le16_to_cpu(rx_desc.length), ==, sizeof(packet));
    qtest_memread(qts, IA64_E1000_RX_BUFFER_ADDR,
                  rx_buffer, sizeof(rx_buffer));
    g_assert_cmpmem(rx_buffer, sizeof(rx_buffer), packet, sizeof(packet));

    qtest_quit(qts);
    close(sockets[0]);
}

static void test_pci_explicit_cmd646_slot0(void)
{
    QTestState *qts = ia64_vpc_start(
        "-device cmd646-ide,secondary=1,addr=0");
    QGenericPCIBus gbus;
    QPCIDevice *dev;

    ia64_qpci_init(&gbus, qts);
    dev = qpci_device_find(&gbus.bus, QPCI_DEVFN(0, 0));
    g_assert_nonnull(dev);
    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==, 0x1095);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==, 0x0646);
    g_assert_cmphex(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_STORAGE_IDE);
    g_free(dev);
    qtest_quit(qts);
}

static void lsi_write_script_insn(QTestState *qts, uint32_t *addr,
                                  uint32_t insn, uint32_t arg)
{
    qtest_writel(qts, *addr, insn);
    qtest_writel(qts, *addr + 4, arg);
    *addr += 8;
}

static bool lsi_run_nodata_command(QTestState *qts, const uint8_t *cdb,
                                   size_t cdb_len, uint8_t *status)
{
    const uint8_t identify = 0x80;
    uint32_t addr = IA64_LSI_SCRIPT_ADDR;
    uint8_t dstat = 0;
    unsigned int i;

    lsi_write_script_insn(qts, &addr, IA64_LSI_SCRIPT_SELECT, 0);
    lsi_write_script_insn(qts, &addr,
                          IA64_LSI_SCRIPT_MOVE(IA64_LSI_PHASE_MO, 1),
                          IA64_LSI_MSGOUT_ADDR);
    lsi_write_script_insn(qts, &addr,
                          IA64_LSI_SCRIPT_MOVE(IA64_LSI_PHASE_CMD,
                                               cdb_len),
                          IA64_LSI_CDB_ADDR);
    lsi_write_script_insn(qts, &addr,
                          IA64_LSI_SCRIPT_MOVE(IA64_LSI_PHASE_ST, 1),
                          IA64_LSI_STATUS_ADDR);
    lsi_write_script_insn(qts, &addr,
                          IA64_LSI_SCRIPT_MOVE(IA64_LSI_PHASE_MI, 1),
                          IA64_LSI_COMPLETE_ADDR);
    lsi_write_script_insn(qts, &addr, IA64_LSI_SCRIPT_DISCONNECT, 0);
    lsi_write_script_insn(qts, &addr, IA64_LSI_SCRIPT_INTERRUPT, 0);

    qtest_memwrite(qts, IA64_LSI_MSGOUT_ADDR, &identify, sizeof(identify));
    qtest_memwrite(qts, IA64_LSI_CDB_ADDR, cdb, cdb_len);
    qtest_writeb(qts, IA64_LSI_STATUS_ADDR, 0xff);
    qtest_writeb(qts, IA64_LSI_COMPLETE_ADDR, 0xff);

    qtest_readb(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_DSTAT);
    qtest_readb(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_SIST0);
    qtest_readb(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_SIST1);
    qtest_writeb(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_ISTAT0,
                 IA64_LSI_ISTAT0_INTF);
    qtest_writel(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_DSP,
                 IA64_LSI_SCRIPT_ADDR);

    for (i = 0; i < 1000; i++) {
        if (qtest_readb(qts, IA64_LSI_MMIO_BASE + IA64_LSI_REG_ISTAT0) &
            IA64_LSI_ISTAT0_DIP) {
            dstat = qtest_readb(qts,
                                IA64_LSI_MMIO_BASE + IA64_LSI_REG_DSTAT);
            if (dstat & IA64_LSI_DSTAT_SIR) {
                break;
            }
        }
        g_usleep(1000);
    }

    *status = qtest_readb(qts, IA64_LSI_STATUS_ADDR);
    return (dstat & IA64_LSI_DSTAT_SIR) != 0;
}

static void test_lsi_async_nodata_command(void)
{
    const uint8_t test_unit_ready[6] = { 0 };
    const uint8_t synchronize_cache[10] = { 0x35 };
    QTestState *qts;
    uint8_t status;
    unsigned int i;

    qts = ia64_vpc_start(
        "-blockdev driver=null-co,read-zeroes=on,"
                  "node-name=disk0,size=1048576 "
        "-device scsi-hd,drive=disk0,bus=scsi.0,scsi-id=0");

    /* Consume the initial unit attention before testing async completion. */
    g_assert_true(lsi_run_nodata_command(qts, test_unit_ready,
                                         sizeof(test_unit_ready), &status));
    for (i = 0; i < 8; i++) {
        g_assert_true(lsi_run_nodata_command(qts, synchronize_cache,
                                             sizeof(synchronize_cache),
                                             &status));
        g_assert_cmpuint(status, ==, 0);
    }
    qtest_quit(qts);
}

static void iosapic_select(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_IOREGSEL, reg);
}

static uint32_t iosapic_read(QTestState *qts, uint32_t reg)
{
    iosapic_select(qts, reg);
    return qtest_readl(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_IOWIN);
}

static void iosapic_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    iosapic_select(qts, reg);
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_IOWIN, value);
}

static char *find_unattached_child(QTestState *qts, const char *qom_type)
{
    g_autoptr(QDict) response = NULL;
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children;
    QListEntry *entry;

    response = qtest_qmp(qts,
                         "{'execute':'qom-list','arguments':"
                         " {'path':'/machine/unattached'}}");
    g_assert(qdict_haskey(response, "return"));
    children = qdict_get_qlist(response, "return");
    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }

    g_error("QOM child of type %s was not found", qom_type);
    return NULL;
}

static unsigned count_unattached_children(QTestState *qts,
                                          const char *qom_type)
{
    g_autoptr(QDict) response = NULL;
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children;
    QListEntry *entry;
    unsigned count = 0;

    response = qtest_qmp(qts,
                         "{'execute':'qom-list','arguments':"
                         " {'path':'/machine/unattached'}}");
    g_assert(qdict_haskey(response, "return"));
    children = qdict_get_qlist(response, "return");
    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            count++;
        }
    }
    return count;
}

static void test_profile_default_input(void)
{
    QTestState *qts;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S");

    g_assert_cmpuint(count_unattached_children(qts, "usb-kbd"), ==, 1);
    g_assert_cmpuint(count_unattached_children(qts, "usb-tablet"), ==, 1);
    g_assert_cmpuint(count_unattached_children(qts, "usb-mouse"), ==, 0);
    qtest_quit(qts);

    qts = qtest_init("-machine itanium-vpc,nvram=none -m 256M -S");
    g_assert_cmpuint(count_unattached_children(qts, "usb-kbd"), ==, 0);
    g_assert_cmpuint(count_unattached_children(qts, "usb-tablet"), ==, 0);
    qtest_quit(qts);
}

static uint64_t ras_record_bank(unsigned int cpu, unsigned int type);
static void assert_ras_record(QTestState *qts, uint64_t bank,
                              unsigned int severity);
static void clear_ras_record(QTestState *qts, uint64_t bank);

static void test_ras_cpu_online_mask(void)
{
    const uint64_t address =
        IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_CPU_ONLINE;

    for (unsigned int cpus = 1; cpus <= 2; cpus++) {
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -smp %u -S", cpus);
        uint64_t present = BIT_ULL(cpus) - 1;

        g_assert_cmphex(qtest_readq(qts, address), ==, 0);
        /* The mailbox only exposes CPUs belonging to this machine. */
        qtest_writeq(qts, address, UINT64_MAX);
        g_assert_cmphex(qtest_readq(qts, address), ==, present);
        qtest_writeq(qts, address, 0);
        g_assert_cmphex(qtest_readq(qts, address), ==, present);
        qtest_quit(qts);
    }
}

static void test_ras_hub_rendezvous(void)
{
    const uint64_t base = IA64_RAS_HUB_DEFAULT_BASE;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -smp 2 -S");

    g_assert_cmphex(qtest_readq(qts, base + IA64_RAS_REG_MAGIC), ==,
                    IA64_RAS_HUB_MAGIC);
    g_assert_cmphex(qtest_readq(qts, base + IA64_RAS_REG_REVISION), ==,
                    IA64_RAS_HUB_REVISION);
    g_assert_cmphex(qtest_readq(qts, base + IA64_RAS_REG_CAPABILITIES), ==,
                    IA64_RAS_CAP_MCA | IA64_RAS_CAP_CMC |
                    IA64_RAS_CAP_CPE | IA64_RAS_CAP_RENDEZVOUS |
                    IA64_RAS_CAP_SAL_RECORDS | IA64_RAS_CAP_INIT |
                    IA64_RAS_CAP_MEMORY_WAKEUP);
    g_assert_cmpuint(qtest_readq(qts,
                                base + IA64_RAS_REG_MAX_RECORD_SIZE), ==,
                     IA64_RAS_MAX_RECORD_SIZE);

    qtest_writeq(qts, base + IA64_RAS_REG_CPU_ONLINE, BIT_ULL(0));
    qtest_writeq(qts, base + IA64_RAS_REG_CPU_ONLINE, BIT_ULL(1));
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_VECTOR, 0xe0);
    qtest_writeq(qts, base + IA64_RAS_REG_WAKEUP_MECHANISM, 1);
    qtest_writeq(qts, base + IA64_RAS_REG_WAKEUP_VALUE, 0xe1);
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_BEGIN, 0);

    g_assert_cmpuint(qtest_readq(qts,
                                base + IA64_RAS_REG_RENDEZVOUS_ACTIVE), ==,
                     1);
    g_assert_cmphex(qtest_readq(qts,
                               base + IA64_RAS_REG_RENDEZVOUS_REQUIRED), ==,
                    BIT_ULL(1));
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_ARRIVED,
                 BIT_ULL(1) | BIT_ULL(2));
    g_assert_cmphex(qtest_readq(qts,
                               base + IA64_RAS_REG_RENDEZVOUS_ARRIVED), ==,
                    BIT_ULL(1));

    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_RELEASE, 1);
    g_assert_cmpuint(qtest_readq(qts,
                                base + IA64_RAS_REG_RENDEZVOUS_ACTIVE), ==,
                     0);
    g_assert_cmpuint(qtest_readq(qts,
                                base + IA64_RAS_REG_RENDEZVOUS_REQUIRED), ==,
                     0);
    g_assert_cmpuint(qtest_readq(qts,
                                base + IA64_RAS_REG_RENDEZVOUS_ARRIVED), ==,
                     0);

    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_VECTOR, 0);
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_BEGIN, 0);
    g_assert_cmpuint(qtest_readq(
                         qts, base + IA64_RAS_REG_RENDEZVOUS_FALLBACK), ==,
                     1);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 1, 0, 0, 0, 0) & BIT(11), ==,
                    BIT(11));
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "accept-init", 1, 0, 0, 0, 0), ==, 1);
    qtest_writeq(qts, base + IA64_RAS_REG_INIT_CAPTURE, 1);
    assert_ras_record(qts, ras_record_bank(
                          1, IA64_RAS_RECORD_TYPE_INIT),
                      IA64_RAS_SAL_STATUS_RECOVERABLE);
    clear_ras_record(qts, ras_record_bank(
                         1, IA64_RAS_RECORD_TYPE_INIT));
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_RELEASE, 1);

    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_VECTOR, 0xe0);
    qtest_writeq(qts, base + IA64_RAS_REG_WAKEUP_MECHANISM, 2);
    qtest_writeq(qts, base + IA64_RAS_REG_WAKEUP_VALUE, 0x00200000);
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_BEGIN, 0);
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_ARRIVED, BIT_ULL(1));
    qtest_writeq(qts, base + IA64_RAS_REG_RENDEZVOUS_RELEASE, 1);
    g_assert_cmphex(qtest_readq(qts, 0x00200000), ==, 2);
    g_assert_cmphex(qtest_readq(
                        qts, base + IA64_RAS_REG_WAKEUP_PENDING), ==,
                    BIT_ULL(1));
    qtest_writeq(qts, 0x00200000, 0);
    qtest_writeq(qts, base + IA64_RAS_REG_WAKEUP_ACK, BIT_ULL(1));
    g_assert_cmphex(qtest_readq(
                        qts, base + IA64_RAS_REG_WAKEUP_PENDING), ==, 0);
    qtest_quit(qts);
}

static void test_ras_min_state_restore(void)
{
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-accel tcg,thread=single");

    g_assert_cmphex(qtest_ia64_ras_min_state(qts), ==, 0x1ffff);
    qtest_quit(qts);
}

static uint64_t ras_record_bank(unsigned int cpu, unsigned int type)
{
    return IA64_RAS_HUB_DEFAULT_BASE +
        ia64_ras_record_bank_offset(cpu, type);
}

static void assert_ras_record(QTestState *qts, uint64_t bank,
                              unsigned int severity)
{
    uint64_t status = qtest_readq(
        qts, bank + IA64_RAS_RECORD_REG_STATUS);
    uint64_t header = qtest_readq(
        qts, bank + IA64_RAS_RECORD_DATA + 8);

    g_assert_cmphex(status & IA64_RAS_RECORD_STATUS_PRESENT, ==,
                    IA64_RAS_RECORD_STATUS_PRESENT);
    g_assert_cmpuint(qtest_readq(qts,
                                bank + IA64_RAS_RECORD_REG_LENGTH), >, 0);
    g_assert_cmpuint((header >> 16) & 0xff, ==, severity);
}

static void clear_ras_record(QTestState *qts, uint64_t bank)
{
    qtest_writeq(qts, bank + IA64_RAS_RECORD_REG_CLEAR,
                 IA64_RAS_RECORD_CLEAR_VALUE);
    g_assert_cmphex(qtest_readq(qts,
                               bank + IA64_RAS_RECORD_REG_STATUS), ==, 0);
}

static void test_ras_fault_injection(void)
{
    const uint8_t cmc_vector = 0x52;
    const uint8_t cpe_vector = 0x53;
    const uint64_t cmc_bank = ras_record_bank(
        0, IA64_RAS_RECORD_TYPE_CMC);
    const uint64_t mca_bank = ras_record_bank(
        0, IA64_RAS_RECORD_TYPE_MCA);
    const uint64_t cpe_bank = ras_record_bank(
        0, IA64_RAS_RECORD_TYPE_CPE);
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-accel tcg,thread=single");

    qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_CORRECTED,
        0x1020304050607080ULL, 0x00123456789ab000ULL,
        0x8877665544332211ULL, cmc_vector);
    assert_ras_record(qts, cmc_bank, IA64_RAS_SAL_STATUS_CORRECTED);
    g_assert_cmphex(qtest_readq(
                    qts, cmc_bank + IA64_RAS_RECORD_DATA + 64), ==,
                    BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(2) | BIT_ULL(12));
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 72), ==,
                    BIT_ULL(24));
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 80) &
                    BIT_ULL(61), ==, BIT_ULL(61));
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 96), ==,
                    BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(3) | BIT_ULL(4));
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 104), ==,
                    0x1020304050607080ULL);
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 112), ==,
                    0x8877665544332211ULL);
    g_assert_cmphex(qtest_readq(
                        qts, cmc_bank + IA64_RAS_RECORD_DATA + 128), ==,
                    0x00123456789ab000ULL);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 0, cmc_vector, 0, 0, 0) & BIT(8),
                    ==, BIT(8));
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "ras-state", 0, 0, 0, 0, 0), ==, BIT(3));
    clear_ras_record(qts, cmc_bank);

    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_MCA_ENTRY,
                 0x00100000);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_CPU_ONLINE,
                 BIT_ULL(0));
    qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_RECOVERABLE,
        0x1111222233334444ULL, 0x0056789abcdef000ULL,
        0xaabbccddeeff0011ULL, 0);
    assert_ras_record(qts, mca_bank, IA64_RAS_SAL_STATUS_RECOVERABLE);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "ras-state", 0, 0, 0, 0, 0), ==,
                    BIT(0) | BIT(3));

    qtest_ia64_ras_inject_chipset(
        qts, IA64_CHIPSET_FAULT_MEMORY_CORRECTED,
        IA64_RAS_SEVERITY_CORRECTED, 0x0000aaaabbbb0000ULL,
        0x1234, 0x5678, 0x90ab);
    assert_ras_record(qts, cpe_bank, IA64_RAS_SAL_STATUS_CORRECTED);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 0, cpe_vector, 0, 0, 0) & BIT(8),
                    ==, 0);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_CPE_VECTOR,
                 cpe_vector);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 0, cpe_vector, 0, 0, 0) & BIT(8),
                    ==, BIT(8));

    clear_ras_record(qts, cpe_bank);
    clear_ras_record(qts, mca_bank);
    qtest_quit(qts);
}

static void test_ras_queue_backpressure(void)
{
    const uint64_t bank = ras_record_bank(0, IA64_RAS_RECORD_TYPE_CMC);
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-accel tcg,thread=single");
    unsigned int i;
    uint64_t record_id;

    for (i = 0; i < IA64_RAS_RECORD_DEPTH; i++) {
        g_assert_true(qtest_ia64_ras_inject_processor(
            qts, 0, IA64_RAS_SEVERITY_CORRECTED, i + 1,
            0x100000 + i * 0x1000, 0x200000 + i, 0x54));
    }
    g_assert_false(qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_CORRECTED, 0x100,
        0x300000, 0x400000, 0x54));
    g_assert_cmphex(qtest_readq(qts, bank + IA64_RAS_RECORD_REG_STATUS),
                    ==, IA64_RAS_RECORD_STATUS_PRESENT |
                        IA64_RAS_RECORD_STATUS_MORE |
                        IA64_RAS_RECORD_STATUS_OVERFLOW);

    for (i = 0; i < IA64_RAS_RECORD_DEPTH; i++) {
        qtest_writeq(qts, bank + IA64_RAS_RECORD_REG_CLEAR,
                     IA64_RAS_RECORD_CLEAR_VALUE);
        g_assert_cmphex(qtest_readq(
                            qts, bank + IA64_RAS_RECORD_REG_STATUS) &
                        IA64_RAS_RECORD_STATUS_PRESENT, ==,
                        i + 1 < IA64_RAS_RECORD_DEPTH ?
                            IA64_RAS_RECORD_STATUS_PRESENT : 0);
    }
    g_assert_true(qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_CORRECTED, 0x101,
        0x500000, 0x600000, 0x54));
    record_id = qtest_readq(qts, bank + IA64_RAS_RECORD_DATA);
    qtest_system_reset(qts);
    assert_ras_record(qts, bank, IA64_RAS_SAL_STATUS_CORRECTED);
    g_assert_cmphex(qtest_readq(qts, bank + IA64_RAS_RECORD_DATA), ==,
                    record_id);
    clear_ras_record(qts, bank);
    g_assert_true(qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_CORRECTED, 0x102,
        0x700000, 0x800000, 0x54));
    g_assert_cmpuint(qtest_readq(qts, bank + IA64_RAS_RECORD_DATA), >,
                     record_id);
    qtest_quit(qts);
}

static void test_iosapic_level_remote_irr(void)
{
    const unsigned pin = 23;
    const uint8_t vector = 0x51;
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + pin * 2;
    QTestState *qts = ia64_vpc_start(NULL);
    g_autofree char *iosapic_path =
        find_unattached_child(qts, "ia64-iosapic");
    uint32_t rte;

    /* Delivery status and Remote IRR are read-only guest-visible bits. */
    iosapic_write(qts, rte_low,
                  vector | IA64_IOSAPIC_RTE_LEVEL |
                  IA64_IOSAPIC_RTE_DELIVERY |
                  IA64_IOSAPIC_RTE_REMOTE_IRR);
    rte = iosapic_read(qts, rte_low);
    g_assert_cmphex(rte & (IA64_IOSAPIC_RTE_DELIVERY |
                          IA64_IOSAPIC_RTE_REMOTE_IRR), ==, 0);

    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    rte = iosapic_read(qts, rte_low);
    g_assert_cmphex(rte & IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    g_assert_cmphex(rte & IA64_IOSAPIC_RTE_DELIVERY, ==, 0);

    /* EOI while the level remains asserted immediately redelivers it. */
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_EOI, vector);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);

    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 0);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_EOI, vector);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, ==, 0);
    qtest_quit(qts);
}

static void test_iosapic_shared_vector_eoi(void)
{
    const unsigned first_pin = 22;
    const unsigned second_pin = 23;
    const uint8_t vector = 0x52;
    const uint32_t first_rte =
        IA64_IOSAPIC_RTE_BASE + first_pin * 2;
    const uint32_t second_rte =
        IA64_IOSAPIC_RTE_BASE + second_pin * 2;
    QTestState *qts = ia64_vpc_start(NULL);
    g_autofree char *iosapic_path =
        find_unattached_child(qts, "ia64-iosapic");

    /*
     * One EOI vector identifies every matching level-triggered RTE.  Shared
     * vectors must not leave a later pin's Remote IRR permanently set.
     */
    iosapic_write(qts, first_rte, vector | IA64_IOSAPIC_RTE_LEVEL);
    iosapic_write(qts, second_rte, vector | IA64_IOSAPIC_RTE_LEVEL);
    qtest_set_irq_in(qts, iosapic_path, NULL, first_pin, 1);
    qtest_set_irq_in(qts, iosapic_path, NULL, second_pin, 1);
    g_assert_cmphex(iosapic_read(qts, first_rte) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    g_assert_cmphex(iosapic_read(qts, second_rte) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);

    qtest_set_irq_in(qts, iosapic_path, NULL, first_pin, 0);
    qtest_set_irq_in(qts, iosapic_path, NULL, second_pin, 0);
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_EOI, vector);
    g_assert_cmphex(iosapic_read(qts, first_rte) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, ==, 0);
    g_assert_cmphex(iosapic_read(qts, second_rte) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, ==, 0);
    qtest_quit(qts);
}

static bool sapic_irr_has_vector(QTestState *qts, uint8_t vector)
{
    g_autofree char *registers = qtest_hmp(qts, "info registers");
    const char *line = strstr(registers, "SAPIC IRR:");
    uint64_t irr[4];

    g_assert_nonnull(line);
    g_assert_cmpint(sscanf(line, "SAPIC IRR: %" SCNx64 " %" SCNx64
                          " %" SCNx64 " %" SCNx64,
                          &irr[0], &irr[1], &irr[2], &irr[3]), ==, 4);
    return (irr[vector / 64] & BIT_ULL(vector % 64)) != 0;
}

static bool sapic_irr_wait_for_vector(QTestState *qts, uint8_t vector)
{
    int i;

    /*
     * External interrupt injection is queued on the destination vCPU.  The
     * qtest IRQ command completes when the IOSAPIC has raised the request,
     * which can be just before the vCPU has updated its Local SAPIC IRR.
     */
    for (i = 0; i < 1000; i++) {
        if (sapic_irr_has_vector(qts, vector)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static uint64_t ia64_pcie_config_address(unsigned int bus, unsigned int devfn,
                                         unsigned int offset)
{
    return IA64_PCI_CONFIG_BASE + ((uint64_t)bus << 20) +
           ((uint64_t)devfn << 12) + offset;
}

static uint8_t ia64_pcie_find_capability(QTestState *qts, unsigned int bus,
                                         unsigned int devfn, uint8_t id)
{
    uint64_t config = ia64_pcie_config_address(bus, devfn, 0);
    uint8_t offset = qtest_readb(qts, config + PCI_CAPABILITY_LIST) & ~3U;
    unsigned int count;

    for (count = 0; offset && count < 48; count++) {
        if (qtest_readb(qts, config + offset + PCI_CAP_LIST_ID) == id) {
            return offset;
        }
        offset = qtest_readb(qts, config + offset + PCI_CAP_LIST_NEXT) & ~3U;
    }
    return 0;
}

static uint16_t ia64_pcie_find_extended_capability(
    QTestState *qts, unsigned int bus, unsigned int devfn, uint16_t id)
{
    uint64_t config = ia64_pcie_config_address(bus, devfn, 0);
    uint16_t offset = PCI_CFG_SPACE_SIZE;
    unsigned int count;

    for (count = 0; offset && count < 256; count++) {
        uint32_t header = qtest_readl(qts, config + offset);

        if (PCI_EXT_CAP_ID(header) == id) {
            return offset;
        }
        offset = PCI_EXT_CAP_NEXT(header);
        if (offset && (offset < PCI_CFG_SPACE_SIZE || (offset & 3))) {
            return 0;
        }
    }
    return 0;
}

static QTestState *ia64_pcie_start(void)
{
    return qtest_init(
        "-machine itanium2-vpc,pcie=on,nvram=none -m 256M -S "
        "-device ia64-pcie-root-port,id=rp,bus=pci,addr=7.0,"
        "chassis=1,slot=7");
}

static void ia64_pcie_configure_root_port(QTestState *qts)
{
    uint64_t config = ia64_pcie_config_address(0, IA64_PCIE_ROOT_DEVFN, 0);
    uint8_t pcie_cap = ia64_pcie_find_capability(
        qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_CAP_ID_EXP);
    uint16_t slot_control;

    g_assert_cmphex(pcie_cap, !=, 0);
    slot_control = qtest_readw(qts, config + pcie_cap + PCI_EXP_SLTCTL);
    slot_control &= ~(PCI_EXP_SLTCTL_PCC | PCI_EXP_SLTCTL_PIC);
    slot_control |= PCI_EXP_SLTCTL_PWR_IND_ON;
    qtest_writew(qts, config + pcie_cap + PCI_EXP_SLTCTL, slot_control);

    qtest_writeb(qts, config + PCI_PRIMARY_BUS, 0);
    qtest_writeb(qts, config + PCI_SECONDARY_BUS, IA64_PCIE_SECONDARY_BUS);
    qtest_writeb(qts, config + PCI_SUBORDINATE_BUS,
                 IA64_PCIE_SECONDARY_BUS);
    qtest_writew(qts, config + PCI_MEMORY_BASE,
                 IA64_PCIE_ENDPOINT_MMIO >> 16);
    qtest_writew(qts, config + PCI_MEMORY_LIMIT,
                 IA64_PCIE_ENDPOINT_MMIO >> 16);
    qtest_writew(qts, config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(qtest_readb(qts, config + PCI_SECONDARY_BUS), ==,
                    IA64_PCIE_SECONDARY_BUS);
    g_assert_cmphex(qtest_readb(qts, config + PCI_SUBORDINATE_BUS), ==,
                    IA64_PCIE_SECONDARY_BUS);
}

static void test_pcie_ecam_aer_hotplug_msix(void)
{
    const uint8_t vector = 0x61;
    QTestState *qts = ia64_pcie_start();
    uint64_t root = ia64_pcie_config_address(0, IA64_PCIE_ROOT_DEVFN, 0);
    uint64_t endpoint = ia64_pcie_config_address(
        IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN, 0);
    uint8_t pcie_cap;
    uint8_t msix_cap;
    uint32_t table;
    uint64_t table_address;
    uint16_t slot_control;
    uint16_t slot_status;

    g_assert_cmphex(qtest_readw(qts, root + PCI_VENDOR_ID), ==,
                    IA64_PCIE_ROOT_VENDOR);
    g_assert_cmphex(PCI_EXT_CAP_ID(qtest_readl(qts, root + 0x100)),
                    ==, PCI_EXT_CAP_ID_ERR);
    g_assert_cmphex(qtest_readl(qts, root + 0xffc), ==, 0);

    pcie_cap = ia64_pcie_find_capability(qts, 0, IA64_PCIE_ROOT_DEVFN,
                                         PCI_CAP_ID_EXP);
    msix_cap = ia64_pcie_find_capability(qts, 0, IA64_PCIE_ROOT_DEVFN,
                                         PCI_CAP_ID_MSIX);
    g_assert_cmphex(pcie_cap, !=, 0);
    g_assert_cmphex(msix_cap, !=, 0);

    qtest_writel(qts, root + PCI_BASE_ADDRESS_0, IA64_PCIE_ROOT_MMIO);
    ia64_pcie_configure_root_port(qts);
    table = qtest_readl(qts, root + msix_cap + PCI_MSIX_TABLE);
    g_assert_cmphex(table & PCI_MSIX_TABLE_BIR, ==, 0);
    table_address = IA64_PCIE_ROOT_MMIO +
                    (table & PCI_MSIX_TABLE_OFFSET);
    qtest_writel(qts, table_address, IA64_PIB_BASE);
    qtest_writel(qts, table_address + 4, 0);
    qtest_writel(qts, table_address + 8, vector);
    qtest_writel(qts, table_address + 12, 0);
    qtest_writew(qts, root + msix_cap + PCI_MSIX_FLAGS,
                 qtest_readw(qts, root + msix_cap + PCI_MSIX_FLAGS) |
                 PCI_MSIX_FLAGS_ENABLE);
    qtest_writew(qts, root + pcie_cap + PCI_EXP_SLTSTA, UINT16_MAX);
    qtest_writew(qts, root + pcie_cap + PCI_EXP_SLTCTL,
                 PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_HPIE);

    qtest_qmp_device_add(qts, "edu", "edu-hot",
                         "{'bus':'rp','addr':'0x0'}");

    slot_status = qtest_readw(qts, root + pcie_cap + PCI_EXP_SLTSTA);
    g_assert_cmphex(slot_status & PCI_EXP_SLTSTA_PDS, !=, 0);
    g_assert_cmphex(slot_status & PCI_EXP_SLTSTA_PDC, !=, 0);
    g_assert_cmphex(qtest_readw(qts, endpoint + PCI_VENDOR_ID), ==,
                    IA64_PCIE_EDU_VENDOR);
    g_assert_true(sapic_irr_wait_for_vector(qts, vector));
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "accept", 0, 0, 0, 0, 0), ==, vector);
    qtest_ia64_sapic(qts, "eoi", 0, 0, 0, 0, 0);
    qtest_writew(qts, root + pcie_cap + PCI_EXP_SLTSTA,
                 PCI_EXP_SLTSTA_PDC);
    g_assert_cmphex(qtest_readw(qts, root + pcie_cap + PCI_EXP_SLTSTA) &
                    PCI_EXP_SLTSTA_PDC, ==, 0);

    qtest_qmp_device_del_send(qts, "edu-hot");
    slot_control = qtest_readw(qts, root + pcie_cap + PCI_EXP_SLTCTL);
    slot_control &= ~PCI_EXP_SLTCTL_PIC;
    qtest_writew(qts, root + pcie_cap + PCI_EXP_SLTCTL,
                 slot_control | PCI_EXP_SLTCTL_PWR_OFF |
                 PCI_EXP_SLTCTL_PWR_IND_OFF);
    g_assert_cmphex(qtest_readw(qts, endpoint + PCI_VENDOR_ID), ==,
                    UINT16_MAX);
    g_assert_cmphex(qtest_readw(qts, root + pcie_cap + PCI_EXP_SLTSTA) &
                    PCI_EXP_SLTSTA_PDS, ==, 0);
    slot_status = qtest_readw(qts, root + pcie_cap + PCI_EXP_SLTSTA);
    g_assert_cmphex(slot_status & PCI_EXP_SLTSTA_PDS, ==, 0);
    g_assert_cmphex(slot_status & PCI_EXP_SLTSTA_PDC, !=, 0);
    g_assert_true(sapic_irr_wait_for_vector(qts, vector));
    qtest_system_reset_nowait(qts);
    qtest_qmp_eventwait(qts, "DEVICE_DELETED");

    qtest_quit(qts);
}

static void test_pcie_intx_and_msi(void)
{
    const uint8_t intx_vector = 0x62;
    const uint8_t msi_vector = 0x63;
    QTestState *qts = ia64_pcie_start();
    uint64_t endpoint = ia64_pcie_config_address(
        IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN, 0);
    uint8_t msi_cap;
    uint16_t msi_flags;
    uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + IA64_PCIE_INTX_GSI * 2;

    ia64_pcie_configure_root_port(qts);
    qtest_qmp_device_add(qts, "edu", "edu-irq",
                         "{'bus':'rp','addr':'0x0'}");
    qtest_writel(qts, endpoint + PCI_BASE_ADDRESS_0,
                 IA64_PCIE_ENDPOINT_MMIO);
    qtest_writew(qts, endpoint + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(qtest_readl(qts, endpoint + PCI_BASE_ADDRESS_0), ==,
                    IA64_PCIE_ENDPOINT_MMIO);
    g_assert_cmphex(qtest_readl(qts, IA64_PCIE_ENDPOINT_MMIO), ==,
                    0x010000edU);

    iosapic_write(qts, rte_low, intx_vector);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + 0x60, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, intx_vector));
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + 0x64, 1);

    msi_cap = ia64_pcie_find_capability(qts, IA64_PCIE_SECONDARY_BUS,
                                        IA64_PCIE_ENDPOINT_DEVFN,
                                        PCI_CAP_ID_MSI);
    g_assert_cmphex(msi_cap, !=, 0);
    msi_flags = qtest_readw(qts, endpoint + msi_cap + PCI_MSI_FLAGS);
    g_assert_cmphex(msi_flags & PCI_MSI_FLAGS_64BIT, !=, 0);
    qtest_writel(qts, endpoint + msi_cap + PCI_MSI_ADDRESS_LO,
                 IA64_PIB_BASE + 8);
    qtest_writel(qts, endpoint + msi_cap + PCI_MSI_ADDRESS_HI, 0);
    qtest_writew(qts, endpoint + msi_cap + PCI_MSI_DATA_64, msi_vector);
    qtest_writew(qts, endpoint + msi_cap + PCI_MSI_FLAGS,
                 msi_flags | PCI_MSI_FLAGS_ENABLE);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + 0x60, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, msi_vector));

    qtest_quit(qts);
}

static void assert_pcie_ras_record(QTestState *qts, uint64_t bank,
                                   unsigned int severity, uint64_t root,
                                   uint8_t pcie_cap, uint16_t aer_cap,
                                   uint16_t source, bool corrected)
{
    uint16_t flags = qtest_readw(qts, root + pcie_cap + PCI_EXP_FLAGS);
    uint32_t slot_cap = qtest_readl(qts, root + pcie_cap + PCI_EXP_SLTCAP);
    uint32_t class_revision = qtest_readl(qts, root + PCI_CLASS_REVISION);
    uint64_t identity0;
    uint64_t identity1;
    uint64_t aer_source;

    assert_ras_record(qts, bank, severity);
    g_assert_cmpuint(qtest_readq(qts,
                                bank + IA64_RAS_RECORD_REG_LENGTH), ==, 304);
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 40), ==,
                    UINT64_C(0x11dcd44109f42430));
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 48), ==,
                    UINT64_C(0x669a0c200008ff95));
    g_assert_cmpuint(qtest_readq(
                         qts, bank + IA64_RAS_RECORD_DATA + 56) >> 32,
                     ==, 264);
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 64), ==,
                    BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(2) | BIT_ULL(3) |
                    BIT_ULL(5) | BIT_ULL(6) | BIT_ULL(7));
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 72), ==,
                    ((uint64_t)(flags & PCI_EXP_FLAGS_VERS) << 40) |
                    ((flags & PCI_EXP_FLAGS_TYPE) >>
                     PCI_EXP_FLAGS_TYPE_SHIFT));
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 80) &
                    UINT64_C(0xffffffff), ==,
                    qtest_readl(qts, root + PCI_COMMAND));

    identity0 = qtest_readl(qts, root + PCI_VENDOR_ID) |
        ((uint64_t)(class_revision >> 8) << 32) |
        ((uint64_t)PCI_FUNC(IA64_PCIE_ROOT_DEVFN) << 56);
    identity1 = PCI_SLOT(IA64_PCIE_ROOT_DEVFN) |
        ((uint64_t)qtest_readb(qts, root + PCI_PRIMARY_BUS) << 24) |
        ((uint64_t)qtest_readb(qts, root + PCI_SECONDARY_BUS) << 32) |
        ((uint64_t)(((slot_cap & PCI_EXP_SLTCAP_PSN) >>
                     PCI_EXP_SLTCAP_PSN_SHIFT) << 3) << 40);
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 88), ==,
                    identity0);
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 96), ==,
                    identity1);
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 112) &
                    UINT64_C(0xffffffff), ==,
                    qtest_readw(qts, root + PCI_SEC_STATUS) |
                    ((uint64_t)qtest_readw(
                         qts, root + PCI_BRIDGE_CONTROL) << 16));
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 176) >> 32, ==,
                    qtest_readl(qts, root + aer_cap));
    g_assert_cmphex(qtest_readq(
                        qts, bank + IA64_RAS_RECORD_DATA + 224) >> 32, ==,
                    qtest_readl(qts, root + aer_cap + PCI_ERR_ROOT_STATUS));
    aer_source = qtest_readq(qts,
                             bank + IA64_RAS_RECORD_DATA + 232);
    g_assert_cmphex(corrected ? aer_source & UINT16_MAX : aer_source >> 16,
                    ==, source);
}

static void test_pcie_aer_delivery(void)
{
    const uint8_t vector = 0x64;
    const uint8_t cpe_vector = 0x65;
    const uint16_t endpoint_requester =
        (IA64_PCIE_SECONDARY_BUS << 8) | IA64_PCIE_ENDPOINT_DEVFN;
    const uint64_t mca_bank =
        ras_record_bank(0, IA64_RAS_RECORD_TYPE_MCA);
    const uint64_t cpe_bank =
        ras_record_bank(0, IA64_RAS_RECORD_TYPE_CPE);
    g_autofree char *response = NULL;
    QTestState *qts = ia64_pcie_start();
    uint64_t root = ia64_pcie_config_address(0, IA64_PCIE_ROOT_DEVFN, 0);
    uint64_t endpoint = ia64_pcie_config_address(
        IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN, 0);
    uint8_t endpoint_pcie_cap;
    uint8_t root_pcie_cap;
    uint8_t root_msix_cap;
    uint16_t endpoint_aer_cap;
    uint16_t root_aer_cap;
    uint32_t table;
    uint64_t table_address;
    uint32_t root_status;

    ia64_pcie_configure_root_port(qts);
    qtest_qmp_device_add(qts, "pcie-pci-bridge", "aer-endpoint",
                         "{'bus':'rp','addr':'0x0'}");

    endpoint_pcie_cap = ia64_pcie_find_capability(
        qts, IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN,
        PCI_CAP_ID_EXP);
    endpoint_aer_cap = ia64_pcie_find_extended_capability(
        qts, IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN,
        PCI_EXT_CAP_ID_ERR);
    root_aer_cap = ia64_pcie_find_extended_capability(
        qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_EXT_CAP_ID_ERR);
    root_pcie_cap = ia64_pcie_find_capability(
        qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_CAP_ID_EXP);
    root_msix_cap = ia64_pcie_find_capability(
        qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_CAP_ID_MSIX);
    g_assert_cmphex(endpoint_pcie_cap, !=, 0);
    g_assert_cmphex(endpoint_aer_cap, !=, 0);
    g_assert_cmphex(root_aer_cap, !=, 0);
    g_assert_cmphex(root_pcie_cap, !=, 0);
    g_assert_cmphex(root_msix_cap, !=, 0);

    qtest_writel(qts, root + PCI_BASE_ADDRESS_0, IA64_PCIE_ROOT_MMIO);
    table = qtest_readl(qts, root + root_msix_cap + PCI_MSIX_TABLE);
    table_address = IA64_PCIE_ROOT_MMIO +
                    (table & PCI_MSIX_TABLE_OFFSET);
    qtest_writel(qts, table_address, IA64_PIB_BASE);
    qtest_writel(qts, table_address + 4, 0);
    qtest_writel(qts, table_address + 8, vector);
    qtest_writel(qts, table_address + 12, 0);
    qtest_writew(qts, root + root_msix_cap + PCI_MSIX_FLAGS,
                 qtest_readw(qts, root + root_msix_cap + PCI_MSIX_FLAGS) |
                 PCI_MSIX_FLAGS_ENABLE);

    qtest_writew(qts, endpoint + endpoint_pcie_cap + PCI_EXP_DEVCTL,
                 qtest_readw(qts, endpoint + endpoint_pcie_cap +
                             PCI_EXP_DEVCTL) |
                 PCI_EXP_DEVCTL_NFERE);
    qtest_writew(qts, root + root_pcie_cap + PCI_EXP_DEVCTL,
                 qtest_readw(qts, root + root_pcie_cap + PCI_EXP_DEVCTL) |
                 PCI_EXP_DEVCTL_NFERE);
    qtest_writew(qts, root + PCI_BRIDGE_CONTROL,
                 qtest_readw(qts, root + PCI_BRIDGE_CONTROL) |
                 PCI_BRIDGE_CTL_SERR);
    qtest_writel(qts, endpoint + endpoint_aer_cap + PCI_ERR_UNCOR_MASK,
                 qtest_readl(qts, endpoint + endpoint_aer_cap +
                             PCI_ERR_UNCOR_MASK) & ~PCI_ERR_UNC_DLP);
    qtest_writel(qts, endpoint + endpoint_aer_cap + PCI_ERR_UNCOR_SEVER,
                 qtest_readl(qts, endpoint + endpoint_aer_cap +
                             PCI_ERR_UNCOR_SEVER) & ~PCI_ERR_UNC_DLP);
    qtest_writel(qts, root + root_aer_cap + PCI_ERR_ROOT_COMMAND,
                 PCI_ERR_ROOT_CMD_NONFATAL_EN);
    qtest_writew(qts, root + root_pcie_cap + PCI_EXP_RTCTL,
                 PCI_EXP_RTCTL_SENFEE);

    response = qtest_hmp(qts,
                         "pcie_aer_inject_error aer-endpoint DLP");
    g_assert_true(g_str_has_prefix(response, "OK id: aer-endpoint"));
    g_assert_cmphex(qtest_readl(qts, endpoint + endpoint_aer_cap +
                               PCI_ERR_UNCOR_STATUS) & PCI_ERR_UNC_DLP,
                    ==, PCI_ERR_UNC_DLP);
    root_status = qtest_readl(qts, root + root_aer_cap +
                             PCI_ERR_ROOT_STATUS);
    g_assert_cmphex(root_status & (PCI_ERR_ROOT_UNCOR_RCV |
                                  PCI_ERR_ROOT_NONFATAL_RCV),
                    ==, PCI_ERR_ROOT_UNCOR_RCV |
                        PCI_ERR_ROOT_NONFATAL_RCV);
    g_assert_cmphex(qtest_readw(qts, root + root_aer_cap +
                               PCI_ERR_ROOT_ERR_SRC + sizeof(uint16_t)),
                    ==, endpoint_requester);
    g_assert_true(sapic_irr_wait_for_vector(qts, vector));
    assert_pcie_ras_record(qts, mca_bank,
                           IA64_RAS_SAL_STATUS_RECOVERABLE, root,
                           root_pcie_cap, root_aer_cap,
                           endpoint_requester, false);
    clear_ras_record(qts, mca_bank);

    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_CPE_VECTOR,
                 cpe_vector);
    qtest_writew(qts, endpoint + endpoint_pcie_cap + PCI_EXP_DEVCTL,
                 qtest_readw(qts, endpoint + endpoint_pcie_cap +
                             PCI_EXP_DEVCTL) |
                 PCI_EXP_DEVCTL_CERE);
    qtest_writew(qts, root + root_pcie_cap + PCI_EXP_DEVCTL,
                 qtest_readw(qts, root + root_pcie_cap + PCI_EXP_DEVCTL) |
                 PCI_EXP_DEVCTL_CERE);
    qtest_writel(qts, endpoint + endpoint_aer_cap + PCI_ERR_COR_MASK,
                 qtest_readl(qts, endpoint + endpoint_aer_cap +
                             PCI_ERR_COR_MASK) & ~PCI_ERR_COR_BAD_DLLP);
    qtest_writel(qts, root + root_aer_cap + PCI_ERR_ROOT_COMMAND,
                 PCI_ERR_ROOT_CMD_NONFATAL_EN | PCI_ERR_ROOT_CMD_COR_EN);
    qtest_writew(qts, root + root_pcie_cap + PCI_EXP_RTCTL,
                 PCI_EXP_RTCTL_SENFEE | PCI_EXP_RTCTL_SECEE);

    g_clear_pointer(&response, g_free);
    response = qtest_hmp(qts,
                         "pcie_aer_inject_error aer-endpoint BAD_DLLP");
    g_assert_true(g_str_has_prefix(response, "OK id: aer-endpoint"));
    g_assert_cmphex(qtest_readl(qts, endpoint + endpoint_aer_cap +
                               PCI_ERR_COR_STATUS) & PCI_ERR_COR_BAD_DLLP,
                    ==, PCI_ERR_COR_BAD_DLLP);
    root_status = qtest_readl(qts, root + root_aer_cap +
                             PCI_ERR_ROOT_STATUS);
    g_assert_cmphex(root_status & PCI_ERR_ROOT_COR_RCV, ==,
                    PCI_ERR_ROOT_COR_RCV);
    g_assert_cmphex(qtest_readw(qts, root + root_aer_cap +
                               PCI_ERR_ROOT_ERR_SRC), ==,
                    endpoint_requester);
    assert_pcie_ras_record(qts, cpe_bank,
                           IA64_RAS_SAL_STATUS_CORRECTED, root,
                           root_pcie_cap, root_aer_cap,
                           endpoint_requester, true);
    g_assert_true(sapic_irr_wait_for_vector(qts, cpe_vector));
    clear_ras_record(qts, cpe_bank);

    qtest_quit(qts);
}

static void test_pcie_aer_notification_controls(void)
{
    static const struct {
        const char *error;
        uint16_t enable;
        uint32_t received;
        unsigned int severity;
        unsigned int type;
    } cases[] = {
        { "BAD_DLLP", PCI_EXP_RTCTL_SECEE, PCI_ERR_ROOT_COR_RCV,
          IA64_RAS_SAL_STATUS_CORRECTED, IA64_RAS_RECORD_TYPE_CPE },
        { "COMP_TIME", PCI_EXP_RTCTL_SENFEE, PCI_ERR_ROOT_NONFATAL_RCV,
          IA64_RAS_SAL_STATUS_RECOVERABLE, IA64_RAS_RECORD_TYPE_MCA },
        { "COMP_TIME", PCI_EXP_RTCTL_SEFEE, PCI_ERR_ROOT_FATAL_RCV,
          IA64_RAS_SAL_STATUS_FATAL, IA64_RAS_RECORD_TYPE_MCA },
    };
    const uint8_t vector = 0x66;
    unsigned int i, mode;

    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        for (mode = 0; mode < 5; mode++) {
            QTestState *qts = ia64_pcie_start();
            uint64_t root = ia64_pcie_config_address(
                0, IA64_PCIE_ROOT_DEVFN, 0);
            uint8_t pcie_cap = ia64_pcie_find_capability(
                qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_CAP_ID_EXP);
            uint8_t msix_cap = ia64_pcie_find_capability(
                qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_CAP_ID_MSIX);
            uint16_t aer_cap = ia64_pcie_find_extended_capability(
                qts, 0, IA64_PCIE_ROOT_DEVFN, PCI_EXT_CAP_ID_ERR);
            bool native = (mode & 1) || mode == 4;
            bool platform = mode == 2 || mode == 3;
            uint16_t enable = cases[i].enable;
            uint16_t control = platform ? enable : 0;
            uint64_t bank = ras_record_bank(0, cases[i].type);
            uint64_t table;
            g_autofree char *command = g_strdup_printf(
                "pcie_aer_inject_error rp %s", cases[i].error);
            g_autofree char *response = NULL;

            g_test_message("%s severity=%u notification-mode=%u",
                           cases[i].error, cases[i].severity, mode);
            ia64_pcie_configure_root_port(qts);
            qtest_writel(qts, root + PCI_BASE_ADDRESS_0, IA64_PCIE_ROOT_MMIO);
            table = IA64_PCIE_ROOT_MMIO +
                (qtest_readl(qts, root + msix_cap + PCI_MSIX_TABLE) &
                 PCI_MSIX_TABLE_OFFSET);
            qtest_writel(qts, table, IA64_PIB_BASE);
            qtest_writel(qts, table + 4, 0);
            qtest_writel(qts, table + 8, vector);
            qtest_writel(qts, table + 12, 0);
            qtest_writew(qts, root + msix_cap + PCI_MSIX_FLAGS,
                         qtest_readw(qts, root + msix_cap + PCI_MSIX_FLAGS) |
                         PCI_MSIX_FLAGS_ENABLE);

            qtest_writew(qts, root + pcie_cap + PCI_EXP_DEVCTL,
                         PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE |
                         PCI_EXP_DEVCTL_FERE);
            qtest_writew(qts, root + PCI_BRIDGE_CONTROL, PCI_BRIDGE_CTL_SERR);
            qtest_writel(qts, root + aer_cap + PCI_ERR_COR_MASK, 0);
            qtest_writel(qts, root + aer_cap + PCI_ERR_UNCOR_MASK, 0);
            qtest_writel(qts, root + aer_cap + PCI_ERR_UNCOR_SEVER,
                         i == 2 ? PCI_ERR_UNC_COMP_TIME : 0);
            if (mode == 4) {
                /* Enabling a different severity must not send an MCA/CPE. */
                control = cases[(i + 1) % G_N_ELEMENTS(cases)].enable;
            }
            qtest_writew(qts, root + pcie_cap + PCI_EXP_RTCTL, control);
            qtest_writel(qts, root + aer_cap + PCI_ERR_ROOT_COMMAND,
                         native ? enable : 0);

            response = qtest_hmp(qts, "%s", command);
            g_assert_true(g_str_has_prefix(response, "OK id: rp"));
            g_assert_cmphex(qtest_readl(qts, root + aer_cap +
                                       PCI_ERR_ROOT_STATUS) & cases[i].received,
                            ==, cases[i].received);
            if (platform) {
                assert_ras_record(qts, bank, cases[i].severity);
            } else {
                g_assert_cmpuint(qtest_readq(qts, bank +
                                            IA64_RAS_RECORD_REG_LENGTH), ==, 0);
            }
            if (native) {
                g_assert_true(sapic_irr_wait_for_vector(qts, vector));
            } else {
                g_assert_false(sapic_irr_has_vector(qts, vector));
            }
            qtest_quit(qts);
        }
    }
}

static uint32_t ia64_zx2_pcie_dma_trigger(QTestState *qts, uint64_t iova,
                                          uint64_t target)
{
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_GVA_LO, iova);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_GVA_HI,
                 iova >> 32);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_GPA_LO, target);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_GPA_HI,
                 target >> 32);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_LEN,
                 sizeof(uint32_t));
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_ATTRS, 0);
    qtest_writel(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_DBELL,
                 ITD_DMA_DBELL_ARM);
    qtest_readl(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, IA64_PCIE_ENDPOINT_MMIO + ITD_REG_DMA_RESULT);
}

static void test_pcie_zx2_iommu(void)
{
    const uint64_t iova = IA64_ZX2_PCIE_IOMMU_IBASE;
    QTestState *qts;
    uint64_t root;
    uint64_t endpoint;
    uint64_t group_control;

    if (!qtest_has_device(TYPE_IA64_ZX2_PCIE_QTEST)) {
        g_test_skip("zx2 PCIe IOMMU test device is unavailable");
        return;
    }
    qts = qtest_init(
        "-machine none -nodefaults -S "
        "-device " TYPE_IA64_ZX2_PCIE_QTEST);
    root = ia64_pcie_config_address(0, IA64_PCIE_ROOT_DEVFN, 0);
    endpoint = ia64_pcie_config_address(
        IA64_PCIE_SECONDARY_BUS, IA64_PCIE_ENDPOINT_DEVFN, 0);

    ia64_pcie_configure_root_port(qts);
    qtest_writel(qts, endpoint + PCI_BASE_ADDRESS_0,
                 IA64_PCIE_ENDPOINT_MMIO);
    qtest_writew(qts, endpoint + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(qtest_readw(qts, endpoint + PCI_VENDOR_ID), ==,
                    IOMMU_TESTDEV_VENDOR_ID);
    g_assert_cmphex(qtest_readl(qts, root + PCI_CLASS_REVISION) >> 8, ==,
                    PCI_CLASS_BRIDGE_PCI << 8);
    g_assert_cmphex(qtest_readw(
                        qts, IA64_ZX2_PCIE_SECOND_ECAM_BASE +
                        (IA64_ZX2_PCIE_SECOND_DEVFN << 12) + PCI_VENDOR_ID),
                    ==, IOMMU_TESTDEV_VENDOR_ID);

    g_assert_cmphex(qtest_readq(qts, IA64_ZX2_PCIE_MIO_BASE +
                               HP_ZX2_MIO_GROUP_ROPES(3)) & BIT_ULL(15),
                    ==, BIT_ULL(15));
    group_control = qtest_readq(qts, IA64_ZX2_PCIE_MIO_BASE +
                               HP_ZX2_MIO_GROUP_CONTROL(3));
    g_assert_cmphex(group_control, ==,
                    HP_ZX2_MIO_GROUP_ENABLE |
                    (UINT64_C(3) << HP_ZX2_MIO_GROUP_CONTEXT_SHIFT));

    qtest_writeq(qts, IA64_ZX2_PCIE_PDIR,
                 IA64_ZX2_PCIE_TARGET | IA64_ZX2_PCIE_PTE_VALID);
    qtest_writeq(qts, IA64_ZX2_PCIE_MIO_BASE +
                 HP_ZX2_MIO_IOMMU_SELECT, 3);
    qtest_writeq(qts,
                 IA64_ZX2_PCIE_IOMMU_REG(HP_ZX1_IOC_IOMMU_IMASK),
                 IA64_ZX2_PCIE_IOMMU_IMASK);
    qtest_writeq(qts,
                 IA64_ZX2_PCIE_IOMMU_REG(HP_ZX1_IOC_IOMMU_IBASE),
                 IA64_ZX2_PCIE_IOMMU_IBASE | 1);
    qtest_writeq(qts,
                 IA64_ZX2_PCIE_IOMMU_REG(HP_ZX1_IOC_IOMMU_TCNFG), 0);
    qtest_writeq(qts,
                 IA64_ZX2_PCIE_IOMMU_REG(HP_ZX1_IOC_IOMMU_PDIR_BASE),
                 IA64_ZX2_PCIE_PDIR);

    qtest_writel(qts, IA64_ZX2_PCIE_TARGET, UINT32_C(0xa5a5a5a5));
    g_assert_cmphex(ia64_zx2_pcie_dma_trigger(
                        qts, iova, IA64_ZX2_PCIE_TARGET), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_PCIE_ENDPOINT_MMIO +
                               ITD_REG_DMA_MEMTX_RESULT), ==, MEMTX_OK);
    g_assert_cmphex(qtest_readl(qts, IA64_ZX2_PCIE_TARGET), ==,
                    ITD_DMA_WRITE_VAL);

    qtest_writeq(qts, IA64_ZX2_PCIE_PDIR, IA64_ZX2_PCIE_TARGET);
    qtest_writeq(qts, IA64_ZX2_PCIE_IOMMU_REG(HP_ZX1_IOC_IOMMU_PCOM),
                 iova | 12);
    qtest_writel(qts, IA64_ZX2_PCIE_TARGET, UINT32_C(0xa5a5a5a5));
    g_assert_cmphex(ia64_zx2_pcie_dma_trigger(
                        qts, iova, IA64_ZX2_PCIE_TARGET), ==,
                    ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, IA64_PCIE_ENDPOINT_MMIO +
                               ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readq(qts, IA64_ZX2_PCIE_MIO_BASE +
                               HP_ZX2_MIO_ERROR_STATUS), ==,
                    HP_ZX1_MIO_ERROR_VALID | HP_ZX1_MIO_ERROR_IOMMU);
    g_assert_cmphex(qtest_readq(qts, IA64_ZX2_PCIE_MIO_BASE +
                               HP_ZX2_MIO_ERROR_ADDRESS), ==, iova);

    qtest_quit(qts);
}

static void test_iosapic_edge_requires_input(void)
{
    const unsigned pin = 21;
    const uint8_t vector = 0x53;
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + pin * 2;
    QTestState *qts = ia64_vpc_start(NULL);
    g_autofree char *iosapic_path =
        find_unattached_child(qts, "ia64-iosapic");

    /*
     * Programming an unmasked edge RTE is configuration, not an interrupt
     * request.  Only an input edge may set the destination Local SAPIC IRR.
     */
    g_assert_false(sapic_irr_has_vector(qts, vector));
    iosapic_write(qts, rte_low, vector | BIT(11));
    g_assert_cmphex(iosapic_read(qts, rte_low) & BIT(11), ==, 0);
    g_assert_false(sapic_irr_has_vector(qts, vector));

    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, vector));
    qtest_quit(qts);
}

static void test_iosapic_masked_edge_is_ignored(void)
{
    const unsigned pin = 20;
    const uint8_t vector = 0x54;
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + pin * 2;
    QTestState *qts = ia64_vpc_start(NULL);
    g_autofree char *iosapic_path =
        find_unattached_child(qts, "ia64-iosapic");

    iosapic_write(qts, rte_low, vector | IA64_IOSAPIC_RTE_MASKED);
    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 0);
    iosapic_write(qts, rte_low, vector);
    g_assert_false(sapic_irr_has_vector(qts, vector));
    qtest_quit(qts);
}

static void test_iosapic_lowest_priority(void)
{
    const unsigned pin = 22;
    const uint8_t vector = 0x52;
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + pin * 2;
    QTestState *qts = ia64_vpc_start(NULL);
    g_autofree char *iosapic_path =
        find_unattached_child(qts, "ia64-iosapic");

    iosapic_write(qts, rte_low,
                  vector | IA64_IOSAPIC_RTE_LOWEST |
                  IA64_IOSAPIC_RTE_LEVEL);
    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);

    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 0);
    qtest_writel(qts, IA64_IOSAPIC_BASE + IA64_IOSAPIC_EOI, vector);
    qtest_quit(qts);
}

static void test_sparse_io_pm_register(void)
{
    const uint32_t port = IA64_ACPI_PM_IO_BASE + IA64_ACPI_PM1_CNT_OFFSET;
    const uint64_t sparse = IA64_LEGACY_IO_BASE +
                            ia64_sparse_io_offset(port);
    const uint64_t alias = sparse ^ 0x400;
    QTestState *qts = ia64_vpc_start(NULL);

    g_assert_cmphex(sparse, ==, 0x00000ffffc801004ULL);

    qtest_writew(qts, sparse, 0);
    g_assert_cmphex(qtest_readw(qts, sparse) & 1, ==, 0);

    qtest_writew(qts, alias, 1);
    g_assert_cmphex(qtest_readw(qts, sparse) & 1, ==, 1);

    qtest_writew(qts, sparse, 0);
    g_assert_cmphex(qtest_readw(qts, alias) & 1, ==, 0);
    qtest_quit(qts);
}

static void test_savevm_restores_platform_state(const void *opaque)
{
    const char *machine = opaque;
    const uint64_t ram_addr = 0x00300000;
    const uint64_t saved_ram = 0x0123456789abcdefULL;
    const uint64_t changed_ram = 0xfedcba9876543210ULL;
    const uint64_t saved_nvram = 0x1020304050607080ULL;
    const uint64_t changed_nvram = 0x8877665544332211ULL;
    const uint64_t saved_watchdog = 0xa5a55a5ac3c33c3cULL;
    const uint64_t changed_watchdog = 0x55aa55aa66996699ULL;
    const uint16_t saved_pm_enable = 0x0100;
    const uint16_t changed_pm_enable = 0x0400;
    const uint32_t saved_vram = 0x00112233;
    const uint32_t changed_vram = 0x00aabbcc;
    const uint32_t saved_ati_scratch = 0x13579bdf;
    const uint32_t changed_ati_scratch = 0x2468ace0;
    const unsigned pin = 23;
    const uint8_t saved_vector = 0x55;
    const uint8_t changed_vector = 0x56;
    const uint8_t saved_rendezvous_vector = 0xe0;
    const uint8_t saved_cpe_vector = 0x67;
    const uint64_t wakeup_address = 0x00200000;
    const uint64_t mca_bank =
        ras_record_bank(0, IA64_RAS_RECORD_TYPE_MCA);
    const uint64_t cpe_bank =
        ras_record_bank(0, IA64_RAS_RECORD_TYPE_CPE);
    const uint32_t rte_low = IA64_IOSAPIC_RTE_BASE + pin * 2;
    const uint64_t pm_enable_addr =
        IA64_LEGACY_IO_BASE +
        ia64_sparse_io_offset(IA64_ACPI_PM_IO_BASE +
                              IA64_ACPI_PM1_EVT_EN_OFFSET);
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *iosapic_path = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    uint8_t int10_response[2];
    TestInt10Registers int10_regs;
    uint64_t saved_mca_record_id;
    uint64_t saved_cpe_record_id;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for internal snapshot testing");
        return;
    }

    tmpdir = g_dir_make_tmp("ia64-vpc-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 64));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=scsi",
                           quoted_disk_path);

    qts = qtest_initf("-machine %s,nvram=none -m 256M -smp 4 -S %s",
                      machine, args);
    iosapic_path = find_unattached_child(qts, "ia64-iosapic");

    qtest_writeq(qts, ram_addr, saved_ram);
    qtest_writeq(qts, IA64_NVRAM_BASE, saved_nvram);
    qtest_writeq(qts, IA64_WATCHDOG_BASE + IA64_WATCHDOG_CODE_OFFSET,
                 saved_watchdog);
    qtest_writew(qts, pm_enable_addr, saved_pm_enable);
    int10_regs = (TestInt10Registers) {
        .ax = 0x4f02,
        .bx = 0x4143,
    };
    g_assert_cmpuint(int10_call(qts, &int10_regs,
                                int10_response, sizeof(int10_response)), ==, 0);
    g_assert_cmphex(int10_regs.ax, ==, 0x004f);
    qtest_writel(qts, IA64_VGA_FB_BASE, saved_vram);
    qtest_writel(qts, IA64_VGA_MMIO_BASE + IA64_ATI_BIOS_0_SCRATCH,
                 saved_ati_scratch);
    iosapic_write(qts, rte_low,
                  saved_vector | IA64_IOSAPIC_RTE_LEVEL);
    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, saved_vector));
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_CPU_ONLINE, 0xf);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_VECTOR,
                 saved_rendezvous_vector);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_WAKEUP_MECHANISM, 2);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_WAKEUP_VALUE,
                 wakeup_address);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_BEGIN, 0);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_ARRIVED, BIT_ULL(1));
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_RELEASE, 1);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_WAKEUP_ACK, BIT_ULL(1));
    g_assert_cmphex(qtest_readq(qts, wakeup_address), ==, 3);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_BEGIN, 0);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                 IA64_RAS_REG_RENDEZVOUS_ARRIVED, BIT_ULL(1));
    g_assert_cmpint(qtest_ia64_sapic(qts, "accept", 2, 0, 0, 0, 0), ==,
                    saved_rendezvous_vector);
    qtest_ia64_sapic(qts, "eoi", 2, 0, 0, 0, 0);

    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_MCA_ENTRY,
                 0x00100000);
    g_assert_true(qtest_ia64_ras_inject_processor(
        qts, 0, IA64_RAS_SEVERITY_RECOVERABLE,
        0x1111222233334444ULL, 0x0056789abcdef000ULL,
        0xaabbccddeeff0011ULL, 0));
    saved_mca_record_id = qtest_readq(
        qts, mca_bank + IA64_RAS_RECORD_REG_ID);
    qtest_writeq(qts, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_REG_CPE_VECTOR,
                 saved_cpe_vector);
    g_assert_true(qtest_ia64_ras_inject_chipset(
        qts, IA64_CHIPSET_FAULT_PROTOCOL, IA64_RAS_SEVERITY_CORRECTED,
        0x0000aaaabbbb0000ULL, 0x1234, 0x5678, 0x90ab));
    g_assert_true(sapic_irr_wait_for_vector(qts, saved_cpe_vector));
    saved_cpe_record_id = qtest_readq(
        qts, cpe_bank + IA64_RAS_RECORD_REG_ID);

    response = qtest_hmp(qts, "savevm platform-state");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    /*
     * Reset first so that the CPU's Local SAPIC state differs as well as
     * the memory-mapped machine and IOSAPIC state.
     */
    qtest_system_reset(qts);
    qtest_writeq(qts, ram_addr, changed_ram);
    qtest_writeq(qts, IA64_NVRAM_BASE, changed_nvram);
    qtest_writeq(qts, IA64_WATCHDOG_BASE + IA64_WATCHDOG_CODE_OFFSET,
                 changed_watchdog);
    qtest_writew(qts, pm_enable_addr, changed_pm_enable);
    int10_regs = (TestInt10Registers) {
        .ax = 0x4f02,
        .bx = 0x4144,
    };
    g_assert_cmpuint(int10_call(qts, &int10_regs,
                                int10_response, sizeof(int10_response)), ==, 0);
    g_assert_cmphex(int10_regs.ax, ==, 0x004f);
    qtest_writel(qts, IA64_VGA_FB_BASE, changed_vram);
    qtest_writel(qts, IA64_VGA_MMIO_BASE + IA64_ATI_BIOS_0_SCRATCH,
                 changed_ati_scratch);
    iosapic_write(qts, rte_low,
                  changed_vector | IA64_IOSAPIC_RTE_LEVEL);
    qtest_set_irq_in(qts, iosapic_path, NULL, pin, 1);
    g_assert_false(sapic_irr_has_vector(qts, saved_vector));
    g_assert_true(sapic_irr_wait_for_vector(qts, changed_vector));
    g_assert_cmpuint(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                                IA64_RAS_REG_RENDEZVOUS_ACTIVE), ==, 0);
    clear_ras_record(qts, mca_bank);
    clear_ras_record(qts, cpe_bank);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "ras-state", 0, 0, 0, 0, 0), ==, 0);

    response = qtest_hmp(qts, "loadvm platform-state");
    g_assert_cmpstr(response, ==, "");

    g_assert_cmphex(qtest_readq(qts, ram_addr), ==, saved_ram);
    g_assert_cmphex(qtest_readq(qts, IA64_NVRAM_BASE), ==, saved_nvram);
    g_assert_cmphex(qtest_readq(qts, IA64_WATCHDOG_BASE +
                               IA64_WATCHDOG_CODE_OFFSET),
                    ==, saved_watchdog);
    g_assert_cmphex(qtest_readw(qts, pm_enable_addr), ==, saved_pm_enable);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 800);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 600);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);
    g_assert_cmphex(test_vbe_read(qts, VBE_DISPI_INDEX_ENABLE) & 0x41,
                    ==, 0x41);
    g_assert_cmphex(qtest_readl(qts, IA64_VGA_FB_BASE), ==, saved_vram);
    g_assert_cmphex(qtest_readl(qts,
                               IA64_VGA_MMIO_BASE +
                               IA64_ATI_BIOS_0_SCRATCH),
                    ==, saved_ati_scratch);
    g_assert_cmphex(iosapic_read(qts, rte_low) & 0xff, ==, saved_vector);
    g_assert_cmphex(iosapic_read(qts, rte_low) &
                    IA64_IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    g_assert_true(sapic_irr_has_vector(qts, saved_vector));
    g_assert_false(sapic_irr_has_vector(qts, changed_vector));
    g_assert_cmphex(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                               IA64_RAS_REG_CPU_ONLINE), ==, 0xf);
    g_assert_cmphex(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                               IA64_RAS_REG_RENDEZVOUS_VECTOR), ==,
                    saved_rendezvous_vector);
    g_assert_cmpuint(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                                IA64_RAS_REG_RENDEZVOUS_ACTIVE), ==, 1);
    g_assert_cmphex(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                               IA64_RAS_REG_RENDEZVOUS_REQUIRED), ==, 0xe);
    g_assert_cmphex(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                               IA64_RAS_REG_RENDEZVOUS_ARRIVED), ==,
                    BIT_ULL(1));
    g_assert_cmphex(qtest_readq(qts, IA64_RAS_HUB_DEFAULT_BASE +
                               IA64_RAS_REG_WAKEUP_PENDING), ==, 0xc);
    g_assert_cmphex(qtest_readq(qts, wakeup_address), ==, 3);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 2, saved_rendezvous_vector,
                        0, 0, 0) & BIT(8), ==, BIT(8));
    assert_ras_record(qts, mca_bank, IA64_RAS_SAL_STATUS_RECOVERABLE);
    g_assert_cmphex(qtest_readq(qts, mca_bank + IA64_RAS_RECORD_REG_ID),
                    ==, saved_mca_record_id);
    assert_ras_record(qts, cpe_bank, IA64_RAS_SAL_STATUS_CORRECTED);
    g_assert_cmphex(qtest_readq(qts, cpe_bank + IA64_RAS_RECORD_REG_ID),
                    ==, saved_cpe_record_id);
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "ras-state", 0, 0, 0, 0, 0), ==,
                    BIT(0) | BIT(3));
    g_assert_cmphex(qtest_ia64_sapic(
                        qts, "state", 0, saved_cpe_vector, 0, 0, 0) &
                    BIT(8), ==, BIT(8));

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

typedef struct TestCPUProfileMigration {
    const char *source_machine;
    const char *destination_machine;
    const char *source;
    const char *destination;
    const char *source_extra;
    const char *destination_extra;
    bool compatible;
} TestCPUProfileMigration;

static char *wait_for_migration_terminal(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 60 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-migrate'}");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, "completed") || !strcmp(status, "failed") ||
            !strcmp(status, "cancelled")) {
            char *terminal = g_strdup(status);

            qobject_unref(result);
            return terminal;
        }
        qobject_unref(result);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_cpu_profile_migration(const void *opaque)
{
    const TestCPUProfileMigration *test = opaque;
    g_autofree char *path = g_strdup_printf(
        "%s/ia64-cpu-profile-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    g_autofree char *status = NULL;
    g_autofree char *args = NULL;
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    args = g_strdup_printf(
        "-machine %s,nvram=none -m 256M -S -nodefaults -cpu %s %s",
        test->source_machine ?: "ia64-vpc", test->source,
        test->source_extra ?: "");
    qts = qtest_init(args);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    status = wait_for_migration_terminal(qts);
    g_assert_cmpstr(status, ==, "completed");
    g_clear_pointer(&status, g_free);
    qtest_quit(qts);

    g_clear_pointer(&args, g_free);
    args = g_strdup_printf(
        "-machine %s,nvram=none -m 256M -S -nodefaults -cpu %s %s "
        "-incoming defer",
        test->destination_machine ?: "ia64-vpc", test->destination,
        test->destination_extra ?: "");
    qts = qtest_init(args);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    status = wait_for_migration_terminal(qts);
    g_assert_cmpstr(status, ==, test->compatible ? "completed" : "failed");
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_stale_victim_speculative_load(void)
{
    const uint64_t target_va = 0x00008000;
    const uint64_t old_pa = 8 * MiB;
    const uint64_t new_pa = 12 * MiB;
    const uint64_t old_value = 0;
    const uint64_t new_value = 0x0000000076875e80ULL;
    QTestState *qts;
    uint64_t value;
    bool probe_succeeded;

    qts = qtest_init("-machine itanium-vpc,nvram=none -m 256M -S "
                     "-accel tcg,thread=single");
    qtest_writeq(qts, old_pa, old_value);
    qtest_writeq(qts, new_pa, new_value);

    /*
     * A direct-only no-fill lookup misses the seeded target victim.  Its
     * cold modeled probe then accepts new_pa, but the following load promotes
     * old_pa and returns zero.  A victim-aware lookup repairs the conflict
     * before the load and therefore returns the modeled page's sentinel.
     */
    value = qtest_ia64_stale_victim_load(qts, target_va, old_pa, new_pa,
                                         &probe_succeeded);
    g_assert_true(probe_succeeded);
    g_assert_cmphex(value, ==, new_value);
    g_assert_cmphex(value, !=, old_value);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    static const unsigned cpu_counts[] = { 1, 2, 4, 8, 16, 32, 64 };
    static const TestCPUProfileMigration cpu_profile_migrations[] = {
        {
            .source = "merced",
            .destination = "itanium",
            .compatible = true,
        }, {
            .source = "madison-9m",
            .destination = "madison",
            .compatible = false,
        }, {
            .source = "montecito",
            .destination = "montecito",
            .source_extra = "-smp cpus=2,sockets=2,cores=1,threads=1",
            .destination_extra =
                "-smp cpus=2,sockets=1,cores=2,threads=1",
            .compatible = false,
        }, {
            .source_machine = "ia64-vpc,alat=zero",
            .destination_machine = "ia64-vpc,alat=full",
            .source = "montecito",
            .destination = "montecito",
            .compatible = false,
        }, {
            .source_machine = "itanium-vpc",
            .destination_machine = "itanium2-vpc",
            .source = "merced",
            .destination = "merced",
            .compatible = false,
        }, {
            .source = "montvale-9150m",
            .destination = "montvale-9152m",
            .compatible = true,
        }, {
            .source = "montvale-9140m",
            .destination = "montvale-9140n",
            .compatible = false,
        },
    };
    unsigned i;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ia64-vpc/acpi-reset-register",
                   test_acpi_reset_register);
    qtest_add_func("/ia64-vpc/vga/int10-rom", test_int10_rom);
    qtest_add_func("/ia64-vpc/vga/int10-rom-rv100",
                   test_int10_rv100_rom);
    qtest_add_func("/ia64-vpc/vga/int10-rom-es1000",
                   test_int10_es1000_rom);
    qtest_add_func("/ia64-vpc/vga/int10-vbe", test_int10_vbe);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-std", test_int10_vbe_std);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-4k", test_int10_vbe_4k);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-4k-std",
                   test_int10_vbe_4k_std);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-native",
                   test_int10_vbe_native_mode);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-maximum",
                   test_int10_vbe_maximum);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-5k-edid",
                   test_int10_vbe_5k_edid);
    qtest_add_func("/ia64-vpc/vga/int10-vbe-invalid-properties",
                   test_int10_vbe_invalid_properties);
    qtest_add_func("/ia64-vpc/vga/int10-legacy", test_int10_legacy);
    qtest_add_func("/ia64-vpc/vga/int10-legacy-std",
                   test_int10_legacy_std);
    qtest_add_func("/ia64-vpc/firmware-handoff/defaults",
                   test_firmware_handoff_defaults);
    qtest_add_func("/ia64-vpc/firmware-handoff/i8042-on",
                   test_firmware_handoff_i8042_on);
    qtest_add_func("/ia64-vpc/firmware-handoff/machine-profiles",
                   test_machine_firmware_profiles);
    qtest_add_func("/ia64-vpc/firmware-handoff/default-ram",
                   test_machine_default_ram);
    for (i = 0; i < G_N_ELEMENTS(cpu_counts); i++) {
        unsigned cpus = cpu_counts[i];
        g_autofree char *path =
            g_strdup_printf("/ia64-vpc/smp/topology/%u", cpus);

        qtest_add_data_func(path, GUINT_TO_POINTER(cpus), test_smp_topology);
    }
    qtest_add_func("/ia64-vpc/smp/explicit-topology",
                   test_smp_explicit_topology);
    for (i = 0; i < G_N_ELEMENTS(smp_multicore_topologies); i++) {
        const TestSmpMulticoreTopology *topology =
            &smp_multicore_topologies[i];
        g_autofree char *path = g_strdup_printf(
            "/ia64-vpc/smp/multicore/%s", topology->name);

        qtest_add_data_func(path, topology, test_smp_multicore_topology);
    }
    qtest_add_func("/ia64-vpc/smp/full-alat", test_smp_full_alat);
    qtest_add_func("/ia64-vpc/alat/active-writer-window",
                   test_alat_active_writer_window);
    qtest_add_func("/ia64-vpc/alat/smp-writer", test_alat_smp_writer);
    qtest_add_func("/ia64-machine/alat/default-zero",
                   test_machine_defaults_to_zero_alat);
    qtest_add_data_func("/ia64-vpc/migration/cpu-profile-alias",
                        &cpu_profile_migrations[0],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/cpu-profile-mismatch",
                        &cpu_profile_migrations[1],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/cpu-topology-mismatch",
                        &cpu_profile_migrations[2],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/alat-mode-mismatch",
                        &cpu_profile_migrations[3],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/firmware-flags-mismatch",
                        &cpu_profile_migrations[4],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/montvale-equivalent-profile",
                        &cpu_profile_migrations[5],
                        test_cpu_profile_migration);
    qtest_add_data_func("/ia64-vpc/migration/montvale-frequency-mismatch",
                        &cpu_profile_migrations[6],
                        test_cpu_profile_migration);
    qtest_add_func("/ia64-vpc/input/profile-defaults",
                   test_profile_default_input);
    qtest_add_func("/ia64-vpc/ras/rendezvous",
                   test_ras_hub_rendezvous);
    qtest_add_func("/ia64-vpc/ras/cpu-online-mask",
                   test_ras_cpu_online_mask);
    qtest_add_func("/ia64-vpc/ras/min-state-restore",
                   test_ras_min_state_restore);
    qtest_add_func("/ia64-vpc/ras/fault-injection",
                   test_ras_fault_injection);
    qtest_add_func("/ia64-vpc/ras/queue-backpressure",
                   test_ras_queue_backpressure);
    qtest_add_func("/ia64-vpc/rtc/aligned-read", test_rtc_aligned_read);
    qtest_add_func("/ia64-vpc/nvram/commit-and-restart",
                   test_nvram_commit_and_restart);
    qtest_add_func("/ia64-vpc/nvram/empty-file",
                   test_nvram_empty_file);
    qtest_add_func("/ia64-vpc/nvram/legacy-file",
                   test_nvram_legacy_file);
    qtest_add_func("/ia64-vpc/nvram/extended-file",
                   test_nvram_extended_file);
    qtest_add_func("/ia64-vpc/pci/default-layout", test_pci_default_layout);
    qtest_add_func("/ia64-vpc/pcie/ecam-aer-hotplug-msix",
                   test_pcie_ecam_aer_hotplug_msix);
    qtest_add_func("/ia64-vpc/pcie/intx-and-msi",
                   test_pcie_intx_and_msi);
    qtest_add_func("/ia64-vpc/pcie/aer-delivery",
                   test_pcie_aer_delivery);
    qtest_add_func("/ia64-vpc/pcie/aer-notification-controls",
                   test_pcie_aer_notification_controls);
    qtest_add_func("/ia64-vpc/pcie/zx2-iommu",
                   test_pcie_zx2_iommu);
    qtest_add_func("/ia64-vpc/pci/es1000-model", test_pci_es1000_model);
    qtest_add_func("/ia64-vpc/pci/rv100-model", test_pci_rv100_model);
    qtest_add_func("/ia64-vpc/pci/itanium-no-default-ahci",
                   test_pci_itanium_no_default_ahci);
    qtest_add_func("/ia64-vpc/pci/explicit-cmd646-slot0",
                   test_pci_explicit_cmd646_slot0);
    qtest_add_func("/ia64-vpc/network/resources-survive-reset",
                   test_e1000_resources_survive_reset);
    qtest_add_func("/ia64-vpc/network/intx-route",
                   test_e1000_intx_route);
    qtest_add_data_func("/ia64-vpc/network/packet-transfer/82540em",
                        "e1000", test_e1000_packet_transfer);
    qtest_add_data_func("/ia64-vpc/network/packet-transfer/82543gc",
                        "e1000-82543gc", test_e1000_packet_transfer);
    qtest_add_func("/ia64-vpc/lsi/async-nodata-command",
                   test_lsi_async_nodata_command);
    qtest_add_func("/ia64-vpc/iosapic/level-remote-irr",
                   test_iosapic_level_remote_irr);
    qtest_add_func("/ia64-vpc/iosapic/shared-vector-eoi",
                   test_iosapic_shared_vector_eoi);
    qtest_add_func("/ia64-vpc/iosapic/edge-requires-input",
                   test_iosapic_edge_requires_input);
    qtest_add_func("/ia64-vpc/iosapic/masked-edge-is-ignored",
                   test_iosapic_masked_edge_is_ignored);
    qtest_add_func("/ia64-vpc/iosapic/lowest-priority",
                   test_iosapic_lowest_priority);
    qtest_add_func("/ia64-vpc/sparse-io/pm-register",
                   test_sparse_io_pm_register);
    qtest_add_data_func("/ia64-vpc/savevm/itanium-platform-state",
                        "itanium-vpc",
                        test_savevm_restores_platform_state);
    qtest_add_data_func("/ia64-vpc/savevm/itanium2-platform-state",
                        "itanium2-vpc",
                        test_savevm_restores_platform_state);
    qtest_add_func("/ia64-vpc/mmu/stale-victim-speculative-load",
                   test_stale_victim_speculative_load);

    return g_test_run();
}
