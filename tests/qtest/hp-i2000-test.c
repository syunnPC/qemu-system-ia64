/*
 * HP i2000 machine qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/memattrs.h"
#include "hw/audio/cs4281.h"
#include "hw/display/bochs-vbe.h"
#include "hw/ia64/hp_i2000.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/scsi/isp12160_abi.h"
#include "hw/southbridge/intel_82468gx.h"
#include "hw/usb/uhci-regs.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define TEST_FIRMWARE_ENV "QTEST_IA64_FIRMWARE"
#define HP_I2000_LOW_DESCRIPTOR_SIZE  952U
#define HP_I2000_HIGH_DESCRIPTOR_SIZE 968U
#define HP_I2000_RAGE128_ROM_BASE     UINT64_C(0x000c0000)
#define HP_I2000_QUADRO2_BMP_OFFSET   UINT64_C(0x00000600)
#define HP_I2000_RAGE128_ROM_SIZE     0x0800U
#define HP_I2000_RAGE128_PCIR_OFFSET  0x0020U
#define HP_I2000_INT10_HANDLER_OFFSET 0x0100U
#define HP_I2000_INT10_MODE_LIST_OFFSET 0x01d0U
#define HP_I2000_INT10_VECTOR_ADDR    UINT64_C(0x00000040)
#define HP_I2000_INT10_IO_BASE        0x01e0U
#define HP_I2000_INT10_IO_EXEC        (HP_I2000_INT10_IO_BASE + 0x0cU)
#define HP_I2000_INT10_IO_DATA        (HP_I2000_INT10_IO_BASE + 0x0eU)
#define HP_I2000_INT10_TRIGGER        0x4941U
#define HP_I2000_VBE2_SIGNATURE       UINT32_C(0x32454256)
#define HP_I2000_RAGE128_FB_BASE      UINT64_C(0xe8000000)
#define HP_I2000_RAGE128_MMIO_BASE    UINT64_C(0xe7000000)
#define HP_I2000_RAGE128_OLD_FB_BASE  UINT64_C(0x90000000)
#define HP_I2000_RAGE128_OLD_MMIO_BASE UINT64_C(0x94000000)
#define HP_I2000_RAGE128_CONFIG_APER_0_BASE 0x0100U
#define HP_I2000_QUADRO2_FB_BASE      UINT64_C(0xe8000000)
#define HP_I2000_QUADRO2_MMIO_BASE    UINT64_C(0xe7000000)
#define HP_I2000_QUADRO2_VRAM_SIZE    (64 * MiB)
#define HP_I2000_QUADRO2_PRAMIN       UINT64_C(0x00700000)
#define HP_I2000_QUADRO2_OBJECT_CACHE 128U
#define HP_I2000_QUADRO2_PMC_BOOT_0   UINT64_C(0x00000000)
#define HP_I2000_QUADRO2_PFB_FIFO     UINT64_C(0x0010020c)
#define HP_I2000_QUADRO2_PFB_CFG0     UINT64_C(0x00100200)
#define HP_I2000_QUADRO2_PFB_CFG1     UINT64_C(0x00100204)
#define HP_I2000_QUADRO2_PEXTDEV_BOOT UINT64_C(0x00101000)
#define HP_I2000_QUADRO2_PMC_INTR     UINT64_C(0x00000100)
#define HP_I2000_QUADRO2_PMC_INTR_EN  UINT64_C(0x00000140)
#define HP_I2000_QUADRO2_PMC_ENABLE   UINT64_C(0x00000200)
#define HP_I2000_QUADRO2_PFIFO_INTR_EN UINT64_C(0x00002140)
#define HP_I2000_QUADRO2_PFIFO_INTR   UINT64_C(0x00002100)
#define HP_I2000_QUADRO2_PFIFO_RAMHT  UINT64_C(0x00002210)
#define HP_I2000_QUADRO2_PFIFO_RAMFC  UINT64_C(0x00002214)
#define HP_I2000_QUADRO2_PFIFO_MODE   UINT64_C(0x00002504)
#define HP_I2000_QUADRO2_PFIFO_DMA_PUSH UINT64_C(0x00003220)
#define HP_I2000_QUADRO2_PFIFO_DMA_STATE UINT64_C(0x00003228)
#define HP_I2000_QUADRO2_PFIFO_DMA_GET UINT64_C(0x00003244)
#define HP_I2000_QUADRO2_PFIFO_DMA_ERROR_INVALID_METHOD (2U << 29)
#define HP_I2000_QUADRO2_PFIFO_DMA_ERROR_INVALID_COMMAND (4U << 29)
#define HP_I2000_QUADRO2_PFIFO_DMA_PUSH_STATUS BIT(12)
#define HP_I2000_QUADRO2_PGRAPH_INTR UINT64_C(0x00400100)
#define HP_I2000_QUADRO2_PGRAPH_NSTATUS UINT64_C(0x00400104)
#define HP_I2000_QUADRO2_PGRAPH_NSOURCE UINT64_C(0x00400108)
#define HP_I2000_QUADRO2_PGRAPH_STATUS UINT64_C(0x00400700)
#define HP_I2000_QUADRO2_PGRAPH_FIFO_ACCESS UINT64_C(0x00400720)
#define HP_I2000_QUADRO2_PGRAPH_INTR_ERROR BIT(20)
#define HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION BIT(26)
#define HP_I2000_QUADRO2_PGRAPH_NSOURCE_ILLEGAL_METHOD BIT(6)
#define HP_I2000_QUADRO2_USER         UINT64_C(0x00800000)
#define HP_I2000_QUADRO2_USER_DMA_PUT UINT64_C(0x00000040)
#define HP_I2000_QUADRO2_USER_DMA_GET UINT64_C(0x00000044)
#define HP_I2000_QUADRO2_PCRTC_INTR   UINT64_C(0x00600100)
#define HP_I2000_QUADRO2_PCRTC_INTR_EN UINT64_C(0x00600140)
#define HP_I2000_QUADRO2_PCRTC_START  UINT64_C(0x00600800)
#define HP_I2000_QUADRO2_CRTC_INDEX   UINT64_C(0x006013d4)
#define HP_I2000_QUADRO2_CRTC_DATA    UINT64_C(0x006013d5)
#define HP_I2000_QUADRO2_INTR_PCRTC   BIT(24)
#define HP_I2000_QUADRO2_RAMHT_VALID  BIT(31)
#define HP_I2000_QUADRO2_RAMHT_GRAPHICS BIT(16)
#define HP_I2000_VGA_PLANAR_SIZE      (256 * KiB)
#define HP_I2000_VGA_LEGACY_BASE      UINT64_C(0x000a0000)
#define HP_I2000_BDA_VIDEO_MODE       UINT64_C(0x00000449)
#define HP_I2000_BDA_VIDEO_COLUMNS    UINT64_C(0x0000044a)
#define HP_I2000_BDA_VIDEO_PAGE_SIZE  UINT64_C(0x0000044c)
#define HP_I2000_BDA_VIDEO_PAGE_START UINT64_C(0x0000044e)
#define HP_I2000_BDA_VIDEO_ROWS       UINT64_C(0x00000484)
#define HP_I2000_BDA_CHARACTER_HEIGHT UINT64_C(0x00000485)
#define HP_I2000_BDA_VIDEO_CONTROL    UINT64_C(0x00000487)
#define HP_I2000_VBE_INDEX_PORT       0x01ceU
#define HP_I2000_VBE_DATA_PORT        0x01d0U
#define HP_I2000_VBE_ENABLE_INDEX     0x0004U
#define HP_I2000_VGA_MISC_READ_PORT   0x03ccU
#define HP_I2000_VGA_MISC_WRITE_PORT  0x03c2U
#define HP_I2000_VGA_SEQ_INDEX_PORT   0x03c4U
#define HP_I2000_VGA_SEQ_DATA_PORT    0x03c5U
#define HP_I2000_VGA_CRTC_INDEX_PORT  0x03d4U
#define HP_I2000_VGA_CRTC_DATA_PORT   0x03d5U
#define HP_I2000_VGA_GFX_INDEX_PORT   0x03ceU
#define HP_I2000_VGA_GFX_DATA_PORT    0x03cfU
#define HP_I2000_VGA_ATTR_INDEX_PORT  0x03c0U
#define HP_I2000_VGA_STATUS_PORT      0x03daU
#define HP_I2000_PIC_MASTER_COMMAND   0x20U
#define HP_I2000_PIC_MASTER_DATA      0x21U
#define HP_I2000_PIC_SLAVE_COMMAND    0xa0U
#define HP_I2000_PIC_SLAVE_DATA       0xa1U
#define HP_I2000_I8042_SELF_TEST      0xaaU
#define HP_I2000_I8042_SELF_TEST_OK   0x55U
#define HP_I2000_I8042_WRITE_AUX_OBUF 0xd3U
#define HP_I2000_I8042_STATUS_OBF      BIT(0)
#define HP_I2000_I8042_STATUS_AUX_OBF  BIT(5)
#define HP_I2000_PID_BASE              UINT64_C(0xfec00000)
#define HP_I2000_PID_IOREGSEL          0x00U
#define HP_I2000_PID_IOWIN             0x10U
#define HP_I2000_PID_RTE_BASE          0x10U
#define HP_I2000_DMA_TEST_SLOT         6U
#define HP_I2000_DMA_TEST_LEN          4U
#define HP_I2000_DMA_TEST_SENTINEL     UINT32_C(0xa5a5a5a5)
#define HP_I2000_DMA_TEST_LOW_RAM      UINT64_C(0x00040000)
#define HP_I2000_ISP12160_IO_BAR       UINT32_C(0x00005000)
#define HP_I2000_CS4281_BA1            UINT32_C(0x98000000)
#define HP_I2000_CS4281_BA0            UINT32_C(0x98010000)
#define HP_I2000_CS4281_HISR           0x0000U
#define HP_I2000_CS4281_HICR           0x0008U
#define HP_I2000_CS4281_HIMR           0x000cU
#define HP_I2000_CS4281_HDSR0          0x00f0U
#define HP_I2000_CS4281_HDSR1          0x00f4U
#define HP_I2000_CS4281_HDSR2          0x00f8U
#define HP_I2000_CS4281_DCA0           0x0110U
#define HP_I2000_CS4281_DCC0           0x0114U
#define HP_I2000_CS4281_DBA0           0x0118U
#define HP_I2000_CS4281_DBC0           0x011cU
#define HP_I2000_CS4281_DCA1           0x0120U
#define HP_I2000_CS4281_DCC1           0x0124U
#define HP_I2000_CS4281_DBA1           0x0128U
#define HP_I2000_CS4281_DBC1           0x012cU
#define HP_I2000_CS4281_DCA2           0x0130U
#define HP_I2000_CS4281_DCC2           0x0134U
#define HP_I2000_CS4281_DBA2           0x0138U
#define HP_I2000_CS4281_DBC2           0x013cU
#define HP_I2000_CS4281_DMR2           0x0160U
#define HP_I2000_CS4281_DCR2           0x0164U
#define HP_I2000_CS4281_DMR0           0x0150U
#define HP_I2000_CS4281_DCR0           0x0154U
#define HP_I2000_CS4281_DMR1           0x0158U
#define HP_I2000_CS4281_DCR1           0x015cU
#define HP_I2000_CS4281_FCR0           0x0180U
#define HP_I2000_CS4281_FCR1           0x0184U
#define HP_I2000_CS4281_FCR2           0x0188U
#define HP_I2000_CS4281_CWPR           0x03e0U
#define HP_I2000_CS4281_EPPMC          0x03e4U
#define HP_I2000_CS4281_SPMC           0x03ecU
#define HP_I2000_CS4281_CFLR           0x03f0U
#define HP_I2000_CS4281_CLKCR1         0x0400U
#define HP_I2000_CS4281_SERMC          0x0420U
#define HP_I2000_CS4281_SERC1          0x0428U
#define HP_I2000_CS4281_SERC2          0x042cU
#define HP_I2000_CS4281_ACCTL          0x0460U
#define HP_I2000_CS4281_ACSTS          0x0464U
#define HP_I2000_CS4281_ACOSV          0x0468U
#define HP_I2000_CS4281_ACCAD          0x046cU
#define HP_I2000_CS4281_ACCDA          0x0470U
#define HP_I2000_CS4281_ACISV          0x0474U
#define HP_I2000_CS4281_ACSAD          0x0478U
#define HP_I2000_CS4281_ACSDA          0x047cU
#define HP_I2000_CS4281_ACSTS2         0x04e4U
#define HP_I2000_CS4281_SSPM           0x0740U
#define HP_I2000_CS4281_SRCSA          0x075cU
#define HP_I2000_CS4281_PPLVC          0x0760U
#define HP_I2000_CS4281_ACCTL_TC       BIT(6)
#define HP_I2000_CS4281_ACCTL_CRW      BIT(4)
#define HP_I2000_CS4281_ACCTL_DCV      BIT(3)
#define HP_I2000_CS4281_ACCTL_VFRM     BIT(2)
#define HP_I2000_CS4281_ACCTL_ESYN     BIT(1)
#define HP_I2000_CS4281_DMR_DMA        BIT(29)
#define HP_I2000_CS4281_DMR_MONO       BIT(17)
#define HP_I2000_CS4281_DMR_SIZE8      BIT(16)
#define HP_I2000_CS4281_DMR_AUTO       BIT(4)
#define HP_I2000_CS4281_DMR_TR_WRITE   (1U << 2)
#define HP_I2000_CS4281_DMR_TR_READ    (2U << 2)
#define HP_I2000_CS4281_DCR_HTCIE      BIT(17)
#define HP_I2000_CS4281_DCR_TCIE       BIT(16)
#define HP_I2000_CS4281_FCR_FEN        BIT(31)
#define HP_I2000_CS4281_FCR_PLAYBACK   (0U << 16 | 1U << 24)
#define HP_I2000_CS4281_FCR_CAPTURE    (10U << 16 | 11U << 24)
#define HP_I2000_CS4281_FCR_UNROUTED   (31U << 16 | 31U << 24)
#define HP_I2000_CS4281_HDSR_DHTC      BIT(17)
#define HP_I2000_CS4281_HDSR_DTC       BIT(16)
#define HP_I2000_CS4281_HDSR_DRUN      BIT(15)
#define HP_I2000_CS4281_HDSR_RQ        BIT(7)
#define HP_I2000_CS4281_HISR_DMAI       BIT(18)
#define HP_I2000_CS4281_HISR_DMA0       BIT(8)
#define HP_I2000_CS4281_HISR_DMA1       BIT(9)
#define HP_I2000_I82559_MMIO_BAR       UINT32_C(0x95000000)
#define HP_I2000_I82559_IO_BAR         UINT32_C(0x00001000)
#define HP_I2000_I82559_FLASH_BAR      UINT32_C(0x95100000)
#define HP_I2000_I82559_FLASH_BAR_SIZE UINT32_C(0x00020000)
#define HP_I2000_I82559_SCB_ACK        0x01U
#define HP_I2000_RAGE128_IO_BAR        UINT16_C(0xc000)
#define HP_I2000_RAGE128_BIOS_SCRATCH  UINT16_C(0x0010)

typedef struct HPI2000Int10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
    uint32_t input_signature;
} HPI2000Int10Registers;

static void hp_i2000_assert_ppm_pixel(const char *filename, unsigned width,
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

static void hp_i2000_quadro2_crtc_write(QTestState *qts, uint8_t index,
                                        uint8_t value)
{
    qtest_writeb(qts, HP_I2000_QUADRO2_MMIO_BASE +
                       HP_I2000_QUADRO2_CRTC_INDEX, index);
    qtest_writeb(qts, HP_I2000_QUADRO2_MMIO_BASE +
                       HP_I2000_QUADRO2_CRTC_DATA, value);
}

static uint8_t hp_i2000_quadro2_crtc_read(QTestState *qts, uint8_t index)
{
    qtest_writeb(qts, HP_I2000_QUADRO2_MMIO_BASE +
                       HP_I2000_QUADRO2_CRTC_INDEX, index);
    return qtest_readb(qts, HP_I2000_QUADRO2_MMIO_BASE +
                            HP_I2000_QUADRO2_CRTC_DATA);
}

static void hp_i2000_quadro2_ddc_lines(QTestState *qts, bool clock,
                                       bool data)
{
    hp_i2000_quadro2_crtc_write(qts, 0x3f,
                                BIT(0) | (clock ? BIT(5) : 0) |
                                (data ? BIT(4) : 0));
}

static void hp_i2000_quadro2_ddc_start(QTestState *qts)
{
    hp_i2000_quadro2_ddc_lines(qts, true, true);
    hp_i2000_quadro2_ddc_lines(qts, true, false);
    hp_i2000_quadro2_ddc_lines(qts, false, false);
}

static void hp_i2000_quadro2_ddc_stop(QTestState *qts)
{
    hp_i2000_quadro2_ddc_lines(qts, false, false);
    hp_i2000_quadro2_ddc_lines(qts, true, false);
    hp_i2000_quadro2_ddc_lines(qts, true, true);
}

static bool hp_i2000_quadro2_ddc_send(QTestState *qts, uint8_t value)
{
    unsigned int i;

    for (i = 0; i < 8; i++) {
        bool bit = value & (0x80U >> i);

        hp_i2000_quadro2_ddc_lines(qts, false, bit);
        hp_i2000_quadro2_ddc_lines(qts, true, bit);
        hp_i2000_quadro2_ddc_lines(qts, false, bit);
    }
    hp_i2000_quadro2_ddc_lines(qts, false, true);
    hp_i2000_quadro2_ddc_lines(qts, true, true);
    value = hp_i2000_quadro2_crtc_read(qts, 0x3e);
    hp_i2000_quadro2_ddc_lines(qts, false, true);
    return !(value & BIT(3));
}

static uint8_t hp_i2000_quadro2_ddc_read(QTestState *qts, bool acknowledge)
{
    uint8_t value = 0;
    unsigned int i;

    for (i = 0; i < 8; i++) {
        hp_i2000_quadro2_ddc_lines(qts, false, true);
        hp_i2000_quadro2_ddc_lines(qts, true, true);
        value = (value << 1) |
                !!(hp_i2000_quadro2_crtc_read(qts, 0x3e) & BIT(3));
        hp_i2000_quadro2_ddc_lines(qts, false, true);
    }
    hp_i2000_quadro2_ddc_lines(qts, false, !acknowledge);
    hp_i2000_quadro2_ddc_lines(qts, true, !acknowledge);
    hp_i2000_quadro2_ddc_lines(qts, false, true);
    return value;
}

static uint32_t hp_i2000_quadro2_dma_header(uint32_t method,
                                            unsigned int subchannel,
                                            unsigned int count)
{
    g_assert_cmphex(method & 3, ==, 0);
    g_assert_cmpuint(subchannel, <, 8);
    g_assert_cmpuint(count, <, 0x800);
    return method | (subchannel << 13) | (count << 18);
}

static uint32_t hp_i2000_quadro2_ramht_hash(uint32_t handle,
                                            unsigned int channel,
                                            unsigned int bits)
{
    uint32_t hash = 0;
    uint32_t mask = (1U << bits) - 1;

    while (handle) {
        hash ^= handle & mask;
        handle >>= bits;
    }
    return (hash ^ (channel << (bits - 4))) & mask;
}

static void hp_i2000_quadro2_ramht_insert(QTestState *qts,
                                          uint32_t ramht_offset,
                                          unsigned int ramht_bits,
                                          unsigned int channel,
                                          uint32_t handle,
                                          uint32_t engine,
                                          uint32_t instance)
{
    uint32_t entries = 1U << ramht_bits;
    uint32_t slot = hp_i2000_quadro2_ramht_hash(handle, channel,
                                                ramht_bits);
    unsigned int i;

    for (i = 0; i < entries; i++, slot = (slot + 1) & (entries - 1)) {
        uint64_t address = HP_I2000_QUADRO2_MMIO_BASE +
                           HP_I2000_QUADRO2_PRAMIN + ramht_offset + slot * 8;

        if (!(qtest_readl(qts, address + 4) &
              HP_I2000_QUADRO2_RAMHT_VALID)) {
            qtest_writel(qts, address, handle);
            qtest_writel(qts, address + 4,
                         HP_I2000_QUADRO2_RAMHT_VALID |
                         engine |
                         (channel << 24) | (instance >> 4));
            return;
        }
    }
    g_assert_not_reached();
}

static QTestState *hp_i2000_start(const char *memory)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none "
                       "-m %s -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s",
                       memory, firmware);
}

static QTestState *hp_i2000_start_with_dma_testdev(void)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none "
                       "-m 4G -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s "
                       "-device %s,bus=pci,addr=%u",
                       firmware, TYPE_IOMMU_TESTDEV,
                       HP_I2000_DMA_TEST_SLOT);
}

static QTestState *hp_i2000_start_with_machine_options(
    const char *machine_options, const char *options)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *nvram_options = strstr(machine_options, "nvram=") ?
        "" : ",nvram=none";

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000%s%s "
                       "-m 2G -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s %s",
                       nvram_options, machine_options, firmware, options);
}

static QTestState *hp_i2000_start_with_options(const char *options)
{
    return hp_i2000_start_with_machine_options("", options);
}

static QTestState *hp_i2000_start_with_storage(const char *storage)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       firmware, storage);
}

static QTestState *hp_i2000_start_defaults_with_machine_options(
    const char *machine_options, const char *options)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *nvram_options = strstr(machine_options, "nvram=") ?
        "" : ",nvram=none";

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000%s%s -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       nvram_options, machine_options, firmware, options);
}

static QTestState *hp_i2000_start_defaults(const char *options)
{
    return hp_i2000_start_defaults_with_machine_options("", options);
}

static void hp_i2000_assert_block_devices(QTestState *qts,
                                          const char *const *expected,
                                          size_t expected_count)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-block'}");
    QList *blocks = qdict_get_qlist(response, "return");
    size_t i;

    g_assert_cmpuint(qlist_size(blocks), ==, expected_count);
    for (i = 0; i < expected_count; i++) {
        QListEntry *entry;
        bool found = false;

        QLIST_FOREACH_ENTRY(blocks, entry) {
            QDict *block = qobject_to(QDict, qlist_entry_obj(entry));

            if (g_str_equal(qdict_get_str(block, "device"), expected[i])) {
                found = true;
                break;
            }
        }
        g_assert_true(found);
    }
}

static unsigned int hp_i2000_count_unattached_children(
    QTestState *qts, const char *qom_type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':"
             " {'path':'/machine/unattached'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;
    unsigned int count = 0;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            count++;
        }
    }
    return count;
}

static char *hp_i2000_find_unattached_child(QTestState *qts,
                                             const char *qom_type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':"
             " {'path':'/machine/unattached'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    return NULL;
}

static void hp_i2000_assert_start_fails_with_machine(
    const char *machine, const char *option, const char *value,
    const char *message)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", machine,
        "-bios", firmware,
        option, value,
        "-display", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_nonnull(firmware);
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void hp_i2000_assert_start_fails(const char *option,
                                         const char *value,
                                         const char *message)
{
    hp_i2000_assert_start_fails_with_machine(
        "hp-i2000,nvram=none", option, value, message);
}

static uint8_t hp_i2000_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

static void hp_i2000_config_select(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        devfn << 8 | (reg & 0xfc);

    qtest_writel(qts, HP_I2000_CF8_PA, address);
}

static uint8_t hp_i2000_config_readb(QTestState *qts, uint8_t bus,
                                      unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readb(qts, HP_I2000_CFC_PA + (reg & 3));
}

static uint16_t hp_i2000_config_readw(QTestState *qts, uint8_t bus,
                                       unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readw(qts, HP_I2000_CFC_PA + (reg & 3));
}

static uint32_t hp_i2000_config_readl(QTestState *qts, uint8_t bus,
                                       unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readl(qts, HP_I2000_CFC_PA + (reg & 3));
}

static void hp_i2000_config_writeb(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint8_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writeb(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static void hp_i2000_config_writew(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint16_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writew(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static void hp_i2000_config_writel(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint32_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writel(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static void hp_i2000_cs4281_init(QTestState *qts)
{
    uint64_t ba0 = HP_I2000_CS4281_BA0;

    qtest_writel(qts, ba0 + HP_I2000_CS4281_EPPMC, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_CWPR, 0x4281);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_SERC1), ==,
                    3);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_SERC2), ==,
                    3);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SSPM, 0x7e);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_CLKCR1, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SERMC, 0);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_SERMC), ==,
                    3);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SPMC, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SPMC, 1);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SERMC, 0x00010003);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_CLKCR1, BIT(4));
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_CLKCR1), ==,
                    BIT(25) | BIT(24) | BIT(4));
    qtest_writel(qts, ba0 + HP_I2000_CS4281_CLKCR1, BIT(5) | BIT(4));
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_CLKCR1), ==,
                    BIT(25) | BIT(24) | BIT(5) | BIT(4));

    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL,
                 HP_I2000_CS4281_ACCTL_ESYN);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSTS), ==, 1);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL,
                 HP_I2000_CS4281_ACCTL_VFRM |
                 HP_I2000_CS4281_ACCTL_ESYN);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACISV), ==, 3);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACOSV, 3);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_SRCSA, 0x0b0a0100);
}

static uint16_t hp_i2000_cs4281_codec_read(QTestState *qts, uint8_t reg)
{
    uint64_t ba0 = HP_I2000_CS4281_BA0;
    uint32_t control = HP_I2000_CS4281_ACCTL_CRW |
        HP_I2000_CS4281_ACCTL_DCV |
        HP_I2000_CS4281_ACCTL_VFRM |
        HP_I2000_CS4281_ACCTL_ESYN;
    uint16_t value;

    (void)qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSDA);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCAD, reg);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCDA, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL, control);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACCTL), ==,
                    control & ~HP_I2000_CS4281_ACCTL_DCV);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSTS), ==, 3);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSAD), ==,
                    reg & 0x7e);
    value = qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSDA);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSTS), ==, 1);
    return value;
}

static void hp_i2000_cs4281_codec_write(QTestState *qts, uint8_t reg,
                                         uint16_t value)
{
    uint64_t ba0 = HP_I2000_CS4281_BA0;
    uint32_t control = HP_I2000_CS4281_ACCTL_DCV |
        HP_I2000_CS4281_ACCTL_VFRM |
        HP_I2000_CS4281_ACCTL_ESYN;

    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCAD, reg);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCDA, value);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL, control);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACCTL), ==,
                    control & ~HP_I2000_CS4281_ACCTL_DCV);
}

static uint32_t hp_i2000_cs4281_wait_hisr(QTestState *qts, uint32_t mask)
{
    uint32_t value = 0;
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        qtest_clock_step(qts, 1000000);
        value = qtest_readl(qts, HP_I2000_CS4281_BA0 +
                                 HP_I2000_CS4281_HISR);
        if ((value & mask) == mask) {
            return value;
        }
    }
    g_error("timed out waiting for CS4281 HISR mask 0x%08x (0x%08x)",
            mask, value);
}

static uint32_t hp_i2000_dma_testdev_trigger(QTestState *qts,
                                             uint64_t mmio_base,
                                             uint64_t dma_address,
                                             uint64_t physical_address)
{
    uint32_t attrs = ITD_ATTRS_SET_SPACE(
        0, ITD_ATTRS_SPACE_NONSECURE);

    qtest_writel(qts, mmio_base + ITD_REG_DMA_GVA_LO,
                 (uint32_t)dma_address);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GVA_HI,
                 (uint32_t)(dma_address >> 32));
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GPA_LO,
                 (uint32_t)physical_address);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GPA_HI,
                 (uint32_t)(physical_address >> 32));
    qtest_writel(qts, mmio_base + ITD_REG_DMA_LEN,
                 HP_I2000_DMA_TEST_LEN);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_ATTRS, attrs);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_DBELL,
                 ITD_DMA_DBELL_ARM);
    g_assert_cmphex(qtest_readl(qts, mmio_base + ITD_REG_DMA_RESULT), ==,
                    ITD_DMA_RESULT_BUSY);
    (void)qtest_readl(qts, mmio_base + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, mmio_base + ITD_REG_DMA_RESULT);
}

static uint64_t hp_i2000_sparse_io_address(uint16_t port)
{
    return HP_I2000_LEGACY_IO_BASE +
        ((uint64_t)(port >> 2) << 12) + (port & 0xfffU);
}

static uint8_t hp_i2000_inb(QTestState *qts, uint16_t port)
{
    return qtest_readb(qts, hp_i2000_sparse_io_address(port));
}

static void hp_i2000_outb(QTestState *qts, uint16_t port, uint8_t value)
{
    qtest_writeb(qts, hp_i2000_sparse_io_address(port), value);
}

static uint16_t hp_i2000_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, hp_i2000_sparse_io_address(port));
}

static void hp_i2000_outw(QTestState *qts, uint16_t port, uint16_t value)
{
    qtest_writew(qts, hp_i2000_sparse_io_address(port), value);
}

static uint32_t hp_i2000_pid_rte_low(unsigned int pin)
{
    return HP_I2000_PID_RTE_BASE + pin * 2U;
}

static void hp_i2000_pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, HP_I2000_PID_BASE + HP_I2000_PID_IOREGSEL, reg);
    qtest_writel(qts, HP_I2000_PID_BASE + HP_I2000_PID_IOWIN, value);
}

static bool hp_i2000_sapic_irr_has_vector(QTestState *qts, uint8_t vector)
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

static bool hp_i2000_sapic_irr_wait_for_vector(QTestState *qts,
                                                uint8_t vector)
{
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        if (hp_i2000_sapic_irr_has_vector(qts, vector)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static uint8_t hp_i2000_vga_indexed_read(QTestState *qts,
                                         uint16_t index_port,
                                         uint16_t data_port, uint8_t index)
{
    hp_i2000_outb(qts, index_port, index);
    return hp_i2000_inb(qts, data_port);
}

static void hp_i2000_assert_int10_rom_device(QTestState *qts,
                                              uint16_t vendor,
                                              uint16_t device)
{
    uint8_t rom[HP_I2000_RAGE128_ROM_SIZE];
    uint8_t vector[4];
    uint16_t pcir;

    qtest_memread(qts, HP_I2000_RAGE128_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom), ==, 0xaa55);
    g_assert_cmpuint(rom[2] * 512U, ==, sizeof(rom));
    g_assert_cmphex(rom[3], !=, 0xcb);
    pcir = lduw_le_p(rom + 0x18);
    g_assert_cmphex(pcir, ==, HP_I2000_RAGE128_PCIR_OFFSET);
    g_assert_cmpmem(rom + pcir, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + pcir + 4), ==, vendor);
    g_assert_cmphex(lduw_le_p(rom + pcir + 6), ==, device);
    g_assert_cmpuint(lduw_le_p(rom + pcir + 0x10) * 512U, ==,
                     sizeof(rom));
    g_assert_cmphex(hp_i2000_checksum(rom, sizeof(rom)), ==, 0);
    g_assert_cmphex(rom[HP_I2000_INT10_HANDLER_OFFSET], !=, 0xcb);

    qtest_memread(qts, HP_I2000_INT10_VECTOR_ADDR, vector, sizeof(vector));
    g_assert_cmphex(lduw_le_p(vector), ==,
                    HP_I2000_INT10_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(vector + 2), ==,
                    HP_I2000_RAGE128_ROM_BASE >> 4);
}

static void hp_i2000_assert_int10_rom(QTestState *qts)
{
    hp_i2000_assert_int10_rom_device(qts, 0x1002, 0x5046);
}

static void hp_i2000_int10_write_request(
    QTestState *qts, const HPI2000Int10Registers *regs)
{
    const uint16_t values[] = {
        regs->ax, regs->bx, regs->cx, regs->dx, regs->di, regs->es,
    };
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(values); i++) {
        hp_i2000_outw(qts, HP_I2000_INT10_IO_BASE + i * 2, values[i]);
    }
    if (regs->input_signature != 0) {
        hp_i2000_outw(qts, HP_I2000_INT10_IO_DATA,
                      (uint16_t)regs->input_signature);
        hp_i2000_outw(qts, HP_I2000_INT10_IO_DATA,
                      (uint16_t)(regs->input_signature >> 16));
    }
}

static void hp_i2000_int10_read_result(QTestState *qts,
                                       HPI2000Int10Registers *regs)
{
    regs->ax = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE);
    regs->bx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 2);
    regs->cx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 4);
    regs->dx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 6);
    regs->di = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 8);
    regs->es = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 10);
}

static size_t hp_i2000_int10_call(QTestState *qts,
                                  HPI2000Int10Registers *regs,
                                  uint8_t *response, size_t response_size)
{
    size_t word_count;
    size_t i;

    hp_i2000_int10_write_request(qts, regs);
    hp_i2000_outw(qts, HP_I2000_INT10_IO_EXEC,
                  HP_I2000_INT10_TRIGGER);
    word_count = hp_i2000_inw(qts, HP_I2000_INT10_IO_EXEC);
    g_assert_cmpuint(word_count * 2, <=, response_size);
    for (i = 0; i < word_count; i++) {
        stw_le_p(response + i * 2,
                 hp_i2000_inw(qts, HP_I2000_INT10_IO_DATA));
    }
    hp_i2000_int10_read_result(qts, regs);
    return word_count * 2;
}

static void hp_i2000_int10_set_mode(QTestState *qts, uint16_t ax)
{
    HPI2000Int10Registers regs = { .ax = ax };

    g_assert_cmpuint(hp_i2000_int10_call(qts, &regs, NULL, 0), ==, 0);
    g_assert_cmphex(regs.ax, ==, ax);
}

static void hp_i2000_assert_mode12(QTestState *qts, bool no_clear)
{
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT,
                  HP_I2000_VBE_ENABLE_INDEX);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_VBE_DATA_PORT), ==, 0);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_VGA_MISC_READ_PORT), ==,
                    0xe3);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_SEQ_INDEX_PORT,
                        HP_I2000_VGA_SEQ_DATA_PORT, 2), ==, 0x0f);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_SEQ_INDEX_PORT,
                        HP_I2000_VGA_SEQ_DATA_PORT, 4), ==, 0x06);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 1), ==, 0x4f);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 0x12), ==, 0xdf);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 0x13), ==, 0x28);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_GFX_INDEX_PORT,
                        HP_I2000_VGA_GFX_DATA_PORT, 6), ==, 0x05);

    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_MODE), ==, 0x12);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_COLUMNS), ==, 80);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_PAGE_SIZE), ==,
                    0xa000);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_PAGE_START), ==, 0);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_ROWS), ==, 29);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_CHARACTER_HEIGHT), ==, 16);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_CONTROL), ==,
                    no_clear ? 0xe0 : 0x60);
}

static uint32_t hp_i2000_int10_far_to_linear(uint32_t pointer)
{
    return (pointer >> 16) * 16 + (pointer & 0xffff);
}

static bool hp_i2000_int10_mode_list_contains(QTestState *qts,
                                               uint32_t address,
                                               uint16_t expected)
{
    size_t i;

    for (i = 0; i < HP_I2000_RAGE128_ROM_SIZE / 2; i++, address += 2) {
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

static void hp_i2000_assert_int10_vbe(QTestState *qts,
                                       uint32_t framebuffer_base)
{
    uint8_t response[512];
    HPI2000Int10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = HP_I2000_VBE2_SIGNATURE,
    };
    uint32_t memory_size;
    uint32_t max_width;
    uint32_t modes;
    size_t length;

    length = hp_i2000_int10_call(qts, &regs,
                                 response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmpmem(response, 4, "VESA", 4);
    memory_size = (uint32_t)lduw_le_p(response + 18) * (64 * KiB);
    modes = hp_i2000_int10_far_to_linear(ldl_le_p(response + 14));
    g_assert_cmphex(modes, ==, HP_I2000_RAGE128_ROM_BASE +
                    HP_I2000_INT10_MODE_LIST_OFFSET);
    g_assert_true(hp_i2000_int10_mode_list_contains(qts, modes, 0x111));

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f01,
        .cx = 0x0111,
    };
    length = hp_i2000_int10_call(qts, &regs,
                                 response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[28], ==, 0);
    g_assert_cmphex((uint32_t)ldl_le_p(response + 40), ==,
                    framebuffer_base);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f02,
        .bx = 0xc143,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);

    regs = (HPI2000Int10Registers) { .ax = 0x4f03 };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 0xc143);

    regs = (HPI2000Int10Registers) { .ax = 0x4f05 };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x034f);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .cx = 801,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);
    g_assert_cmphex(regs.dx, ==, memory_size / 3232);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 2,
        .cx = 3201,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .cx = VBE_DISPI_MAX_XRES + 1,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x024f);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 1,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / 600 / 4) & ~7U;
    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 3,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, max_width * 4);
    g_assert_cmphex(regs.cx, ==, max_width);
    g_assert_cmphex(regs.dx, ==, memory_size / (max_width * 4));
}

static void hp_i2000_activate_i8042(QTestState *qts)
{
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_ENTER_KEY);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_LDN_SELECT_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_LDN);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_I8042_KBD_IRQ_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_KBD_IRQ);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_I8042_MOUSE_IRQ_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_MOUSE_IRQ);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_SIO_ACTIVE_VALUE);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_EXIT_KEY);
}

static void hp_i2000_assert_device(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, uint16_t vendor,
                                    uint16_t device)
{
    g_assert_cmphex(hp_i2000_config_readl(qts, bus, devfn, 0), ==,
                    (uint32_t)device << 16 | vendor);
}

static void hp_i2000_assert_identity(QTestState *qts, uint8_t bus,
                                      unsigned int devfn, uint16_t vendor,
                                      uint16_t device, uint8_t revision,
                                      uint16_t class_id,
                                      uint16_t subsystem_vendor,
                                      uint16_t subsystem)
{
    hp_i2000_assert_device(qts, bus, devfn, vendor, device);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, bus, devfn, PCI_REVISION_ID), ==, revision);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, bus, devfn, PCI_CLASS_DEVICE), ==, class_id);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, bus, devfn, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    subsystem_vendor);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, bus, devfn, PCI_SUBSYSTEM_ID), ==, subsystem);
}

static void hp_i2000_assert_default_usb_hid_absent(QTestState *qts)
{
    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-kbd"), ==, 0);
    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-mouse"), ==, 0);
    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-tablet"), ==, 0);
    hp_i2000_assert_device(qts, 0, PCI_DEVFN(3, 2),
                           INTEL_82468GX_IFB_VENDOR_ID,
                           INTEL_82468GX_IFB_USB_DEVICE_ID);
}

static void hp_i2000_assert_descriptor(QTestState *qts, uint64_t ram_size,
                                        uint32_t expected_size,
                                        bool nvram_persistent)
{
    uint8_t storage[HP_I2000_HIGH_DESCRIPTOR_SIZE] = { 0 };
    IA64PlatformDescriptor *descriptor =
        (IA64PlatformDescriptor *)storage;
    const IA64PlatformRamRange *ranges;
    const IA64PlatformPciRoot *roots;
    const IA64PlatformPciRoute *routes;
    const IA64PlatformI2000Profile *profile;
    uint64_t high_size = ram_size - HP_I2000_LOW_RAM_LIMIT;
    unsigned int root;

    qtest_memread(qts, HP_I2000_DESCRIPTOR_GPA, storage, sizeof(*descriptor));
    g_assert_cmpuint(le32_to_cpu(descriptor->TotalSize), ==, expected_size);
    qtest_memread(qts, HP_I2000_DESCRIPTOR_GPA, storage, expected_size);

    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_I2000);
    g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==, ram_size);
    g_assert_cmphex(le64_to_cpu(descriptor->LowRamEnd), ==,
                    HP_I2000_LOW_RAM_LIMIT);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==,
                     high_size ? 2 : 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     HP_I2000_PCI_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==, 5);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramBase), ==,
                    IA64_I2000_PROFILE_NVRAM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramSize), ==,
                    IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(
        le32_to_cpu(descriptor->Flags) &
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT,
        ==, nvram_persistent ? IA64_PLATFORM_FLAG_NVRAM_PERSISTENT : 0);
    g_assert_cmphex(
        le32_to_cpu(descriptor->Flags) &
            ~IA64_PLATFORM_FLAG_NVRAM_PERSISTENT,
        ==, IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS |
            IA64_PLATFORM_FLAG_IDE_DMA);
    g_assert_cmphex(ia64_platform_firmware_compat_flags(
                        le32_to_cpu(descriptor->PlatformId),
                        le32_to_cpu(descriptor->Flags)), ==,
                    IA64_FW_COMPAT_ALL_MASK);
    g_assert_cmpuint(hp_i2000_checksum(storage, expected_size), ==, 0);

    ranges = (const IA64PlatformRamRange *)(
        storage + le32_to_cpu(descriptor->RamRangeOffset));
    g_assert_cmphex(le64_to_cpu(ranges[0].Base), ==, 0);
    g_assert_cmphex(le64_to_cpu(ranges[0].Size), ==,
                    HP_I2000_LOW_RAM_LIMIT);
    if (high_size) {
        g_assert_cmphex(le64_to_cpu(ranges[1].Base), ==,
                        HP_I2000_HIGH_RAM_BASE);
        g_assert_cmphex(le64_to_cpu(ranges[1].Size), ==, high_size);
    }

    roots = (const IA64PlatformPciRoot *)(
        storage + le32_to_cpu(descriptor->PciRootOffset));
    for (root = 0; root < HP_I2000_PCI_ROOT_COUNT; root++) {
        g_assert_cmphex(le32_to_cpu(roots[root].Flags), ==,
                        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
                        (root == 3 ?
                         IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY : 0));
        g_assert_cmphex(le64_to_cpu(roots[root].DmaBase), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[root].DmaSize), ==,
                        HP_I2000_LOW_RAM_LIMIT);
    }

    routes = (const IA64PlatformPciRoute *)(
        storage + le32_to_cpu(descriptor->PciRouteOffset));
    g_assert_cmpuint(le16_to_cpu(routes[0].Segment), ==, 0);
    g_assert_cmpuint(routes[0].Bus, ==, 3);
    g_assert_cmpuint(routes[0].Device, ==, 0);
    g_assert_cmpuint(routes[0].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[0].Gsi), ==, 28);
    g_assert_cmpuint(le16_to_cpu(routes[2].Segment), ==, 0);
    g_assert_cmpuint(routes[2].Bus, ==, 0);
    g_assert_cmpuint(routes[2].Device, ==, 4);
    g_assert_cmpuint(routes[2].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[2].Gsi), ==, 16);
    g_assert_cmpuint(le16_to_cpu(routes[3].Segment), ==, 0);
    g_assert_cmpuint(routes[3].Bus, ==, 0);
    g_assert_cmpuint(routes[3].Device, ==, 3);
    g_assert_cmpuint(routes[3].Pin, ==, 3);
    g_assert_cmpuint(le32_to_cpu(routes[3].Gsi), ==, 19);
    g_assert_cmpuint(le16_to_cpu(routes[4].Segment), ==, 0);
    g_assert_cmpuint(routes[4].Bus, ==, 1);
    g_assert_cmpuint(routes[4].Device, ==, 0);
    g_assert_cmpuint(routes[4].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[4].Gsi), ==, 20);

    profile = (const IA64PlatformI2000Profile *)(
        storage + le32_to_cpu(descriptor->ProfileOffset));
    g_assert_cmpuint(le32_to_cpu(profile->ProfileType), ==,
                     IA64_PLATFORM_PROFILE_TYPE_HP_I2000);
    g_assert_cmpuint(profile->IdeProgIf, ==,
                     IA64_I2000_PROFILE_IDE_PROG_IF);
}

static void test_hp_i2000_machine_identity(void)
{
    QTestState *qts = hp_i2000_start("2G");
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-machines'}");
    QList *machines = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool found = false;

    QLIST_FOREACH_ENTRY(machines, entry) {
        QDict *machine = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(machine, "name"), "hp-i2000")) {
            g_assert_cmpstr(qdict_get_str(machine, "default-cpu-type"), ==,
                            "merced-ia64-cpu");
            g_assert_cmpint(qdict_get_int(machine, "cpu-max"), ==, 2);
            g_assert_cmpstr(qdict_get_str(machine, "default-ram-id"), ==,
                            "hp-i2000.ram");
            found = true;
            break;
        }
    }
    g_assert_true(found);
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, false);
    qtest_quit(qts);
}

static void test_hp_i2000_default_usb_input(void)
{
    QTestState *qts = hp_i2000_start_defaults("");
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-mice'}");
    QList *mice = qdict_get_qlist(response, "return");
    QListEntry *entry;
    g_autofree char *keyboard =
        hp_i2000_find_unattached_child(qts, "usb-kbd");
    g_autofree char *tablet =
        hp_i2000_find_unattached_child(qts, "usb-tablet");
    bool current_tablet = false;
    uint32_t bar;
    uint16_t io_base;

    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-kbd"), ==, 1);
    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-mouse"), ==, 0);
    g_assert_cmpuint(
        hp_i2000_count_unattached_children(qts, "usb-tablet"), ==, 1);
    g_assert_nonnull(keyboard);
    g_assert_nonnull(tablet);
    g_assert_false(qtest_qom_get_bool(qts, keyboard, "msos-desc"));
    g_assert_false(qtest_qom_get_bool(qts, tablet, "msos-desc"));
    QLIST_FOREACH_ENTRY(mice, entry) {
        QDict *mouse = qobject_to(QDict, qlist_entry_obj(entry));

        if (qdict_get_bool(mouse, "current")) {
            g_assert_cmpstr(qdict_get_str(mouse, "name"), ==,
                            "QEMU HID Tablet");
            g_assert_true(qdict_get_bool(mouse, "absolute"));
            current_tablet = true;
        }
    }
    g_assert_true(current_tablet);

    bar = hp_i2000_config_readl(qts, 0, PCI_DEVFN(3, 2),
                                PCI_BASE_ADDRESS_4);
    io_base = bar & PCI_BASE_ADDRESS_IO_MASK;
    g_assert_cmphex(hp_i2000_inw(qts, io_base + UHCI_USBPORTSC1) &
                    UHCI_PORT_CCS, ==, UHCI_PORT_CCS);
    g_assert_cmphex(hp_i2000_inw(qts, io_base + UHCI_USBPORTSC2) &
                    UHCI_PORT_CCS, ==, UHCI_PORT_CCS);
    qtest_quit(qts);

    qts = hp_i2000_start_defaults("-nodefaults");
    hp_i2000_assert_default_usb_hid_absent(qts);
    qtest_quit(qts);

    qts = hp_i2000_start_defaults_with_machine_options(",usb=off", "");
    hp_i2000_assert_default_usb_hid_absent(qts);
    qtest_quit(qts);
}

static void test_hp_i2000_constraints(void)
{
    g_assert_cmphex(HP_I2000_MAX_RAM_SIZE, ==, 16 * GiB);
    hp_i2000_assert_start_fails("-m", "1G", "at least 2 GiB");
}

static void test_hp_i2000_storage_defaults(void)
{
    static const char *const automatic[] = {
        "scsi0-hd0", "ide0-cd0",
    };
    static const char *const explicit_topology[] = {
        "ide0-hd0", "ide0-cd1", "ide1-cd0", "ide1-hd1",
        "scsi0-cd6",
    };
    static const char *const cdrom_shortcut[] = {
        "ide1-cd0",
    };
    QTestState *qts = hp_i2000_start_with_storage("");

    hp_i2000_assert_block_devices(qts, NULL, 0);
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage(
        "-drive media=disk,file=null-co://,format=raw "
        "-drive media=cdrom,file=null-co://,format=raw");
    hp_i2000_assert_block_devices(qts, automatic,
                                  G_N_ELEMENTS(automatic));
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage(
        "-drive if=ide,bus=0,unit=0,media=disk,file=null-co://,format=raw "
        "-drive if=ide,bus=0,unit=1,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=0,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=1,media=disk,file=null-co://,format=raw "
        "-drive if=scsi,bus=0,unit=6,media=cdrom,file=null-co://,format=raw");
    hp_i2000_assert_block_devices(qts, explicit_topology,
                                  G_N_ELEMENTS(explicit_topology));
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage("-cdrom null-co://");
    hp_i2000_assert_block_devices(qts, cdrom_shortcut,
                                  G_N_ELEMENTS(cdrom_shortcut));
    qtest_quit(qts);
}

static void test_hp_i2000_ram_descriptor(void)
{
    QTestState *qts = hp_i2000_start("3G");

    hp_i2000_assert_descriptor(qts, 3 * GiB,
                               HP_I2000_HIGH_DESCRIPTOR_SIZE, false);
    qtest_writel(qts, HP_I2000_HIGH_RAM_BASE, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_HIGH_RAM_BASE), ==,
                    0x12345678);
    qtest_quit(qts);

    qts = hp_i2000_start("8G");
    hp_i2000_assert_descriptor(qts, 8 * GiB,
                               HP_I2000_HIGH_DESCRIPTOR_SIZE, false);
    qtest_writel(qts, HP_I2000_HIGH_RAM_BASE + 6 * GiB - 4,
                 0x89abcdef);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_HIGH_RAM_BASE + 6 * GiB - 4), ==,
                    0x89abcdef);
    qtest_quit(qts);
}

static void test_hp_i2000_pci_dma_ram_map(void)
{
    static const struct {
        uint8_t bus;
        uint64_t mmio_base;
    } roots[] = {
        { 0x00, UINT64_C(0x98000000) },
        { 0x01, UINT64_C(0xa8000000) },
        { 0x02, UINT64_C(0xb8000000) },
        { 0x03, UINT64_C(0xe6000000) },
    };
    const unsigned int devfn = PCI_DEVFN(HP_I2000_DMA_TEST_SLOT, 0);
    const uint64_t high_ram = HP_I2000_HIGH_RAM_BASE;
    uint64_t mmio_base = 0;
    uint8_t bus = 0;
    QTestState *qts;
    uint32_t result;
    unsigned int root;

    if (!qtest_has_device(TYPE_IOMMU_TESTDEV)) {
        g_test_skip("iommu-testdev is unavailable");
        return;
    }

    qts = hp_i2000_start_with_dma_testdev();
    for (root = 0; root < G_N_ELEMENTS(roots); root++) {
        if (hp_i2000_config_readl(qts, roots[root].bus, devfn, 0) ==
            ((uint32_t)IOMMU_TESTDEV_DEVICE_ID << 16 |
             IOMMU_TESTDEV_VENDOR_ID)) {
            bus = roots[root].bus;
            mmio_base = roots[root].mmio_base;
            break;
        }
    }
    g_assert_cmpuint(root, <, G_N_ELEMENTS(roots));
    hp_i2000_config_writel(qts, bus, devfn, PCI_BASE_ADDRESS_0,
                           mmio_base);
    hp_i2000_config_writew(qts, bus, devfn, PCI_COMMAND,
                           PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    qtest_writel(qts, HP_I2000_DMA_TEST_LOW_RAM,
                 HP_I2000_DMA_TEST_SENTINEL);
    result = hp_i2000_dma_testdev_trigger(
        qts, mmio_base, HP_I2000_DMA_TEST_LOW_RAM,
        HP_I2000_DMA_TEST_LOW_RAM);
    g_assert_cmphex(result, ==, 0);
    g_assert_cmphex(qtest_readl(
                        qts, mmio_base + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_DMA_TEST_LOW_RAM), ==,
                    ITD_DMA_WRITE_VAL);

    qtest_writel(qts, high_ram, HP_I2000_DMA_TEST_SENTINEL);
    result = hp_i2000_dma_testdev_trigger(
        qts, mmio_base, high_ram, high_ram);
    g_assert_cmphex(result, ==, 0);
    g_assert_cmphex(qtest_readl(
                        qts, mmio_base + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(qts, high_ram), ==, ITD_DMA_WRITE_VAL);

    /* The RAM hole starts at 2 GiB; that boundary must stay unmapped. */
    qtest_writel(qts, high_ram, HP_I2000_DMA_TEST_SENTINEL);
    result = hp_i2000_dma_testdev_trigger(
        qts, mmio_base, HP_I2000_LOW_RAM_LIMIT, high_ram);
    g_assert_cmphex(result, ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(
                        qts, mmio_base + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readl(qts, high_ram), ==,
                    HP_I2000_DMA_TEST_SENTINEL);
    qtest_quit(qts);
}

static void hp_i2000_cs4281_dma_transfer(QTestState *qts, unsigned channel,
                                         bool capture, bool interrupt)
{
    const uint64_t ba0 = HP_I2000_CS4281_BA0;
    const uint32_t dma_base = 0x40000 + channel * 0x1000;
    const unsigned dma_reg = channel * 0x10;
    const unsigned control_reg = channel * 8;
    const unsigned fifo_reg = channel * 4;
    const uint32_t direction = capture ? HP_I2000_CS4281_DMR_TR_WRITE :
                                        HP_I2000_CS4281_DMR_TR_READ;
    const uint32_t irq_mask = HP_I2000_CS4281_HISR_DMAI | BIT(8 + channel);
    uint8_t samples[64 * 4];
    uint8_t result[sizeof(samples)];
    uint32_t hdsr;
    unsigned i;

    memset(samples, 0xa5, sizeof(samples));
    qtest_memwrite(qts, dma_base, samples, sizeof(samples));
    qtest_writel(qts, dma_base - 4, 0x12345678);
    qtest_writel(qts, dma_base + sizeof(samples), 0x87654321);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR0 + control_reg, 0);
    (void)qtest_readl(qts, ba0 + HP_I2000_CS4281_HDSR0 + fifo_reg);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBA0 + dma_reg, dma_base);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBC0 + dma_reg, 63);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DCR0 + control_reg,
                 interrupt ? HP_I2000_CS4281_DCR_HTCIE |
                             HP_I2000_CS4281_DCR_TCIE : 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_FCR0 + fifo_reg,
                 HP_I2000_CS4281_FCR_FEN | HP_I2000_CS4281_FCR_UNROUTED);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_HIMR, 0x7fffffff & ~irq_mask);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_HICR, 3);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR0 + control_reg,
                 HP_I2000_CS4281_DMR_DMA | direction);

    /* DMA cannot consume an unassigned serial slot. */
    qtest_clock_step(qts, 50000000);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCA0 + dma_reg),
                    ==, dma_base);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HISR) & irq_mask,
                    ==, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_FCR0 + fifo_reg,
                 HP_I2000_CS4281_FCR_FEN |
                 (capture ? HP_I2000_CS4281_FCR_CAPTURE :
                            HP_I2000_CS4281_FCR_PLAYBACK));

    /* Pending half-count status must not stop transfer to terminal count. */
    for (i = 0; i < 1000; i++) {
        qtest_clock_step(qts, 1000000);
        if (!(qtest_readl(qts, ba0 + HP_I2000_CS4281_DMR0 + control_reg) &
              HP_I2000_CS4281_DMR_DMA)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, 1000);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCA0 + dma_reg),
                    ==, dma_base + sizeof(samples));
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCC0 + dma_reg),
                    ==, 0);
    if (interrupt) {
        hp_i2000_cs4281_wait_hisr(qts, irq_mask);
    }
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HISR) & irq_mask,
                    ==, interrupt ? irq_mask : 0);
    g_assert_cmphex(hp_i2000_config_readw(qts, 0, PCI_DEVFN(4, 0),
                                         PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, interrupt ? PCI_STATUS_INTERRUPT : 0);
    hdsr = qtest_readl(qts, ba0 + HP_I2000_CS4281_HDSR0 + fifo_reg);
    g_assert_cmphex(hdsr, ==,
                    HP_I2000_CS4281_HDSR_DHTC | HP_I2000_CS4281_HDSR_DTC);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HISR) & irq_mask,
                    ==, 0);
    qtest_memread(qts, dma_base, result, sizeof(result));
    if (capture) {
        memset(samples, 0, sizeof(samples));
    }
    g_assert_cmpmem(result, sizeof(result), samples, sizeof(samples));
    g_assert_cmphex(qtest_readl(qts, dma_base - 4), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, dma_base + sizeof(samples)), ==,
                    0x87654321);
}

static void test_hp_i2000_cs4281_dma(void)
{
    QTestState *qts = hp_i2000_start("2G");
    const uint64_t ba0 = HP_I2000_CS4281_BA0;
    const uint32_t dma_base = 0x50000;
    uint8_t samples[64 * 4];
    uint8_t result[sizeof(samples)];
    unsigned channel;
    unsigned i;

    hp_i2000_cs4281_init(qts);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    for (channel = 0; channel < 4; channel++) {
        hp_i2000_cs4281_dma_transfer(qts, channel, false, true);
        hp_i2000_cs4281_dma_transfer(qts, channel, true, true);
    }
    hp_i2000_cs4281_dma_transfer(qts, 2, false, false);
    hp_i2000_cs4281_dma_transfer(qts, 3, true, false);

    /* Automatic reload continues across unacknowledged period interrupts. */
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBA0 + 3 * 0x10, dma_base);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBC0 + 3 * 0x10, 63);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DCR0 + 3 * 8,
                 HP_I2000_CS4281_DCR_HTCIE | HP_I2000_CS4281_DCR_TCIE);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR0 + 3 * 8,
                 HP_I2000_CS4281_DMR_DMA | HP_I2000_CS4281_DMR_AUTO |
                 HP_I2000_CS4281_DMR_TR_WRITE);
    memset(samples, 0xa5, sizeof(samples));
    for (i = 0; i < 2; i++) {
        qtest_memwrite(qts, dma_base, samples, sizeof(samples));
        qtest_clock_step(qts, 50000000);
        qtest_memread(qts, dma_base, result, sizeof(result));
        g_assert_cmpmem(result, sizeof(result),
                        (uint8_t[sizeof(result)]) { 0 }, sizeof(result));
    }
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DMR0 + 3 * 8) &
                    HP_I2000_CS4281_DMR_DMA, ==, HP_I2000_CS4281_DMR_DMA);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HDSR0 + 3 * 4) &
                    (HP_I2000_CS4281_HDSR_DHTC | HP_I2000_CS4281_HDSR_DTC),
                    ==, HP_I2000_CS4281_HDSR_DHTC | HP_I2000_CS4281_HDSR_DTC);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR0 + 3 * 8, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_cs4281_handoff(void)
{
    QTestState *qts = hp_i2000_start_with_options("-display vnc=none");
    const uint64_t ba0 = HP_I2000_CS4281_BA0;
    unsigned channel, i;

    hp_i2000_cs4281_init(qts);
    for (channel = 0; channel < 4; channel++) {
        bool capture = channel & 1;
        bool mono = channel >= 2;
        uint32_t dma_base = 0x40000 + channel * 0x1000;
        unsigned bytes = 64 * (mono ? 1 : 4);

        qtest_memset(qts, dma_base, 0xa5, bytes);
        qtest_writel(qts, dma_base - 4, 0x12345678);
        qtest_writel(qts, dma_base + bytes, 0x87654321);
        qtest_writel(qts, ba0 + HP_I2000_CS4281_DBA0 + channel * 0x10,
                     dma_base);
        qtest_writel(qts, ba0 + HP_I2000_CS4281_DBC0 + channel * 0x10, 63);
        qtest_writel(qts, ba0 + HP_I2000_CS4281_DCR0 + channel * 8,
                     HP_I2000_CS4281_DCR_HTCIE | HP_I2000_CS4281_DCR_TCIE);
        qtest_writel(qts, ba0 + HP_I2000_CS4281_FCR0 + channel * 4,
                     HP_I2000_CS4281_FCR_FEN |
                     (capture ? (10U << 16) | ((mono ? 31U : 11U) << 24) :
                                HP_I2000_CS4281_FCR_PLAYBACK));
        qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR0 + channel * 8,
                     HP_I2000_CS4281_DMR_DMA |
                     (capture ? HP_I2000_CS4281_DMR_TR_WRITE :
                                HP_I2000_CS4281_DMR_TR_READ) |
                     (mono ? HP_I2000_CS4281_DMR_MONO |
                             HP_I2000_CS4281_DMR_SIZE8 : 0));
    }

    /* Each direction hands off from stereo 16-bit to mono 8-bit PCM. */
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    for (i = 0; i < 1000; i++) {
        uint32_t running = 0;

        qtest_clock_step(qts, 1000000);
        for (channel = 0; channel < 4; channel++) {
            running |= qtest_readl(qts, ba0 + HP_I2000_CS4281_DMR0 +
                                        channel * 8);
        }
        if (!(running & HP_I2000_CS4281_DMR_DMA)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, 1000);
    for (channel = 0; channel < 4; channel++) {
        uint32_t dma_base = 0x40000 + channel * 0x1000;
        unsigned bytes = 64 * (channel >= 2 ? 1 : 4);
        uint8_t expected[256], result[256];

        g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCA0 +
                                         channel * 0x10), ==, dma_base + bytes);
        g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HDSR0 +
                                         channel * 4), ==,
                        HP_I2000_CS4281_HDSR_DHTC | HP_I2000_CS4281_HDSR_DTC);
        memset(expected, (channel & 1) ? 0 : 0xa5, bytes);
        qtest_memread(qts, dma_base, result, bytes);
        g_assert_cmpmem(result, bytes, expected, bytes);
        g_assert_cmphex(qtest_readl(qts, dma_base - 4), ==, 0x12345678);
        g_assert_cmphex(qtest_readl(qts, dma_base + bytes), ==, 0x87654321);
    }
    qtest_system_reset(qts);
    hp_i2000_cs4281_init(qts);
    hp_i2000_cs4281_dma_transfer(qts, 2, false, true);
    hp_i2000_cs4281_dma_transfer(qts, 3, true, true);
    qtest_quit(qts);
}

static void test_hp_i2000_cs4281(void)
{
    const unsigned int devfn = PCI_DEVFN(4, 0);
    const uint64_t ba0 = HP_I2000_CS4281_BA0;
    const uint64_t ba1 = HP_I2000_CS4281_BA1;
    const uint32_t dma_base = 0x00040000;
    uint16_t status;

    QTestState *qts = hp_i2000_start("2G");

    g_assert_cmphex(hp_i2000_config_readw(qts, 0, devfn, PCI_COMMAND), ==,
                    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    status = hp_i2000_config_readw(qts, 0, devfn, PCI_STATUS);
    g_assert_cmphex(status & (PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_MASK),
                    ==, PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_MEDIUM);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_0), ==,
                    HP_I2000_CS4281_BA0);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_1), ==,
                    HP_I2000_CS4281_BA1);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_CAPABILITY_LIST), ==, 0x40);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, devfn, 0x40), ==,
                    PCI_CAP_ID_PM);
    g_assert_cmphex(hp_i2000_config_readw(qts, 0, devfn, 0x42), ==,
                    0x7e21);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_INTERRUPT_LINE), ==, 16);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_INTERRUPT_PIN), ==, 1);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, devfn, PCI_MIN_GNT), ==,
                    4);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, devfn, PCI_MAX_LAT), ==,
                    0x18);
    hp_i2000_config_writeb(qts, 0, devfn, PCI_CACHE_LINE_SIZE, 0xff);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_CACHE_LINE_SIZE), ==, 0);
    hp_i2000_config_writeb(qts, 0, devfn, PCI_LATENCY_TIMER, 0xff);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_LATENCY_TIMER), ==, 0xf8);
    hp_i2000_config_writeb(qts, 0, devfn, PCI_LATENCY_TIMER, 0);

    hp_i2000_config_writel(qts, 0, devfn, PCI_BASE_ADDRESS_0, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_0), ==,
                    ~(uint32_t)(CS4281_BA0_SIZE - 1));
    hp_i2000_config_writel(qts, 0, devfn, PCI_BASE_ADDRESS_1, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_1), ==,
                    ~(uint32_t)(CS4281_BA1_SIZE - 1));
    hp_i2000_config_writel(qts, 0, devfn, PCI_BASE_ADDRESS_0,
                           HP_I2000_CS4281_BA0);
    hp_i2000_config_writel(qts, 0, devfn, PCI_BASE_ADDRESS_1,
                           HP_I2000_CS4281_BA1);

    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HIMR), ==,
                    0x7fffffff);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_CFLR), ==, 1);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_CWPR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_SPMC), ==, 0);

    /* E4h..FFh stay protected until CWPR contains the documented key. */
    hp_i2000_config_writel(qts, 0, devfn, 0xec, 1);
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, devfn, 0xec), ==, 0);
    hp_i2000_config_writel(qts, 0, devfn, 0xe0, 0x4281);
    hp_i2000_config_writel(qts, 0, devfn, 0xe4, UINT32_MAX);
    hp_i2000_config_writel(qts, 0, devfn, 0xf8, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, devfn, 0xe4), ==, 0);
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, devfn, 0xf8), ==, 0);
    hp_i2000_config_writel(qts, 0, devfn, 0xec, 1);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_SPMC), ==, 1);
    hp_i2000_config_writel(qts, 0, devfn, 0xec, 0);

    hp_i2000_cs4281_init(qts);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x00), ==, 0x1990);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x28), ==, 0x0200);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x7c), ==, 0x4352);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x7e), ==, 0x5911);
    hp_i2000_cs4281_codec_write(qts, 0x02, 0);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x02), ==, 0);

    /* An absent secondary codec completes commands without returning data. */
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCAD, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_ACCTL,
                 HP_I2000_CS4281_ACCTL_TC |
                 HP_I2000_CS4281_ACCTL_CRW |
                 HP_I2000_CS4281_ACCTL_DCV |
                 HP_I2000_CS4281_ACCTL_VFRM |
                 HP_I2000_CS4281_ACCTL_ESYN);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACCTL) &
                    HP_I2000_CS4281_ACCTL_DCV, ==, 0);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_ACSTS2), ==, 0);

    qtest_writel(qts, ba1 + 0x40, 0x5a4281a5);
    g_assert_cmphex(qtest_readl(qts, ba1 + 0x40), ==, 0x5a4281a5);

    hp_i2000_pid_write(qts, hp_i2000_pid_rte_low(16), 0x61);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    hp_i2000_cs4281_dma_transfer(qts, 0, false, true);
    g_assert_true(hp_i2000_sapic_irr_wait_for_vector(qts, 0x61));
    hp_i2000_cs4281_dma_transfer(qts, 1, true, true);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");

    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBA2, dma_base);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DBC2, 0x3f);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR2,
                 HP_I2000_CS4281_DMR_AUTO |
                 HP_I2000_CS4281_DMR_TR_READ);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DCR2, 0);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_FCR2,
                 HP_I2000_CS4281_FCR_FEN | HP_I2000_CS4281_FCR_PLAYBACK);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_DMR2,
                 HP_I2000_CS4281_DMR_DMA |
                 HP_I2000_CS4281_DMR_AUTO |
                 HP_I2000_CS4281_DMR_TR_READ);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCA2), ==,
                    dma_base);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCC2), ==,
                    0x3f);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HDSR2) &
                    (HP_I2000_CS4281_HDSR_DRUN |
                     HP_I2000_CS4281_HDSR_RQ), ==,
                    HP_I2000_CS4281_HDSR_DRUN |
                    HP_I2000_CS4281_HDSR_RQ);
    qtest_writel(qts, ba0 + HP_I2000_CS4281_HICR, 3);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HISR), ==,
                    BIT(31));

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_HIMR), ==,
                    0x7fffffff);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCA2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ba0 + HP_I2000_CS4281_DCC2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ba1 + 0x40), ==, 0);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_0), ==,
                    HP_I2000_CS4281_BA0);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_1), ==,
                    HP_I2000_CS4281_BA1);
    qtest_quit(qts);
}

static void test_hp_i2000_pci_layout_and_reset(void)
{
    QTestState *qts = hp_i2000_start("2G");
    static const uint8_t expander_device[] = { 0x10, 0x12, 0x13, 0x14 };
    static const uint16_t expander_id[] = { 0x84cb, 0x84e6,
                                            0x84e6, 0x84ea };
    static const uint8_t expander_revision[] = { 0x05, 0x07,
                                                 0x07, 0x02 };
    static const uint8_t sac_function_mask[] = {
        BIT(0) | BIT(1) | BIT(2),
        BIT(2) | BIT(3),
    };
    unsigned int function;
    unsigned int expander;

    hp_i2000_assert_identity(qts, 0, PCI_DEVFN(0, 0),
                             0x8086, 0x123d, 0x01,
                             PCI_CLASS_SYSTEM_PIC, 0, 0);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(0, 0), PCI_CLASS_PROG), ==, 0x20);
    for (function = 0; function < INTEL_82468GX_IFB_FUNCTIONS;
         function++) {
        hp_i2000_assert_identity(
            qts, 0, PCI_DEVFN(3, function),
            INTEL_82468GX_IFB_VENDOR_ID,
            INTEL_82468GX_IFB_LPC_DEVICE_ID + function, 0x01,
            function == 0 ? PCI_CLASS_BRIDGE_ISA :
            function == 1 ? PCI_CLASS_STORAGE_IDE :
            function == 2 ? PCI_CLASS_SERIAL_USB :
                            PCI_CLASS_SERIAL_SMBUS,
            0, 0);
    }
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(3, 1), PCI_CLASS_PROG), ==,
                    IA64_I2000_PROFILE_IDE_PROG_IF);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 1), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(3, 1), PCI_BASE_ADDRESS_4), ==,
                    IA64_I2000_PROFILE_IDE_BMDMA_PORT |
                    PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 1),
                        INTEL_82468GX_IFB_IDETIM_PRIMARY), ==,
                    INTEL_82468GX_IFB_IDETIM_DECODE);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 1),
                        INTEL_82468GX_IFB_IDETIM_SECONDARY), ==, 0);
    hp_i2000_assert_identity(qts, 0, PCI_DEVFN(4, 0),
                             0x1013, 0x6005, 0x01,
                             PCI_CLASS_MULTIMEDIA_AUDIO, 0x8086, 0x4253);
    hp_i2000_assert_identity(qts, 0, PCI_DEVFN(5, 0),
                             0x8086, 0x1229, 0x08,
                             PCI_CLASS_NETWORK_ETHERNET, 0x8086, 0x3400);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(5, 0), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                    PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_0), ==,
                    HP_I2000_I82559_MMIO_BAR);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_1), ==,
                    HP_I2000_I82559_IO_BAR | PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_2), ==,
                    HP_I2000_I82559_FLASH_BAR);
    hp_i2000_config_writel(qts, 0, PCI_DEVFN(5, 0),
                           PCI_BASE_ADDRESS_2, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_2), ==,
                    ~(HP_I2000_I82559_FLASH_BAR_SIZE - 1));
    hp_i2000_config_writel(qts, 0, PCI_DEVFN(5, 0),
                           PCI_BASE_ADDRESS_2,
                           HP_I2000_I82559_FLASH_BAR);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(5, 0), PCI_INTERRUPT_LINE), ==,
                    16);
    hp_i2000_assert_identity(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        0x1077, 0x1216, 0x06, PCI_CLASS_STORAGE_SCSI, 0x1077, 0x0007);
    for (function = 0; function < 2; function++) {
        hp_i2000_assert_identity(qts, 1 + function, PCI_DEVFN(0x0f, 0),
                                 0x8086, 0x123f, 0x01,
                                 PCI_CLASS_SYSTEM_PCI_HOTPLUG,
                                 0x8086, 0x123f);
    }

    for (expander = 0; expander < 2; expander++) {
        for (function = 0; function < 8; function++) {
            if (sac_function_mask[expander] & BIT(function)) {
                hp_i2000_assert_identity(qts, 4,
                                         PCI_DEVFN(expander, function),
                                         0x8086, 0x84e0, 0x03,
                                         PCI_CLASS_BRIDGE_HOST,
                                         0x8086, 0x84e0);
            } else {
                g_assert_cmphex(hp_i2000_config_readl(
                                    qts, 4,
                                    PCI_DEVFN(expander, function),
                                    PCI_VENDOR_ID), ==, UINT32_MAX);
            }
        }
    }
    hp_i2000_assert_identity(qts, 4, PCI_DEVFN(4, 0),
                             0x8086, 0x84e1, 0x03,
                             PCI_CLASS_BRIDGE_HOST, 0x8086, 0x84e1);
    for (expander = 0; expander < 8; expander++) {
        hp_i2000_assert_identity(qts, 4, PCI_DEVFN(0x10 + expander, 0),
                                 0x8086, 0x84e0, 0x03,
                                 PCI_CLASS_BRIDGE_HOST, 0x8086, 0x84e0);
    }
    for (expander = 0; expander < G_N_ELEMENTS(expander_device);
         expander++) {
        hp_i2000_assert_identity(
            qts, 4, PCI_DEVFN(expander_device[expander], 1),
            0x8086, expander_id[expander], expander_revision[expander],
            PCI_CLASS_BRIDGE_HOST, 0x8086, expander_id[expander]);
    }
    hp_i2000_assert_identity(qts, 4, PCI_DEVFN(0x14, 2),
                             0x8086, 0x84e2, 0x02,
                             PCI_CLASS_BRIDGE_OTHER, 0x8086, 0x84e2);

    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 0), PCI_COMMAND), ==, 0x0007);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 2), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(3, 2),
                        PCI_BASE_ADDRESS_4), ==, 0x00001101);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(3, 2),
                        PCI_INTERRUPT_LINE), ==, 19);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_INTERRUPT_LINE), ==,
                    ISP12160_QEMU_I2000_GSI);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_0), ==,
                    HP_I2000_ISP12160_IO_BAR |
                    PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_1), ==,
                    ISP12160_QEMU_I2000_BAR_ADDRESS);
    hp_i2000_config_writel(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_0, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_0), ==,
                    ~(uint32_t)(ISP12160_REG_SIZE - 1U) |
                    PCI_BASE_ADDRESS_SPACE_IO);
    hp_i2000_config_writel(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_1, UINT32_MAX);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_1), ==,
                    ~(uint32_t)(ISP12160_MMIO_BAR_SIZE - 1U));
    hp_i2000_config_writel(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_0,
        HP_I2000_ISP12160_IO_BAR | PCI_BASE_ADDRESS_SPACE_IO);
    hp_i2000_config_writel(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_1, ISP12160_QEMU_I2000_BAR_ADDRESS);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(3, 0), PCI_COMMAND, 0x0108);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(3, 1), PCI_COMMAND, 0);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(5, 0), PCI_COMMAND, 0);
    hp_i2000_config_writel(qts, 0, PCI_DEVFN(5, 0),
                           PCI_BASE_ADDRESS_0, 0);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(3, 1),
                           INTEL_82468GX_IFB_IDETIM_PRIMARY, 0);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 0), PCI_COMMAND), ==, 0x010f);
    hp_i2000_config_writel(
        qts, ISP12160_QEMU_I2000_BUS,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_1, 0);
    qtest_writeb(qts, HP_I2000_CF8_PA + 1, 0x5a);
    qtest_writew(qts, HP_I2000_CF8_PA + 2, 0xa55a);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0x5a);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0xa55a);
    qtest_system_reset(qts);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 0), PCI_COMMAND), ==, 0x0007);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 1), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(3, 1), PCI_BASE_ADDRESS_4), ==,
                    IA64_I2000_PROFILE_IDE_BMDMA_PORT |
                    PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(3, 1),
                        INTEL_82468GX_IFB_IDETIM_PRIMARY), ==,
                    INTEL_82468GX_IFB_IDETIM_DECODE);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, ISP12160_QEMU_I2000_BUS,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_1), ==,
                    ISP12160_QEMU_I2000_BAR_ADDRESS);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(5, 0), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                    PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_0), ==,
                    HP_I2000_I82559_MMIO_BAR);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_acpi_pm(void)
{
    QTestState *qts = hp_i2000_start("2G");
    const unsigned int ifb = PCI_DEVFN(3, 0);
    const uint16_t base = IA64_I2000_PROFILE_ACPI_PM_IO_BASE;
    uint64_t cnt = hp_i2000_sparse_io_address(base + 4U);
    uint64_t timer = hp_i2000_sparse_io_address(base + 8U);
    uint64_t gpe = hp_i2000_sparse_io_address(base + 0x0cU);
    uint32_t first;
    uint32_t second;

    g_assert_cmphex(hp_i2000_config_readl(qts, 0, ifb, 0x40), ==,
                    base | 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x45), ==, 0U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 0U);
    qtest_writew(qts, cnt, 1U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 1U);
    g_assert_cmphex(qtest_readw(qts, gpe), ==, 0x0800U);
    qtest_writew(qts, gpe, 0x0800U);
    g_assert_cmphex(qtest_readw(qts, gpe), ==, 0U);

    first = qtest_readl(qts, timer) & 0x00ffffffU;
    qtest_clock_step(qts, 1000000);
    second = qtest_readl(qts, timer) & 0x00ffffffU;
    g_assert_cmpuint(second, !=, first);

    hp_i2000_config_writeb(qts, 0, ifb, 0x44, 0U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 0U);
    qtest_system_reset(qts);
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, ifb, 0x40), ==,
                    base | 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 1U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 0U);
    qtest_quit(qts);
}

static void test_hp_i2000_pib_inta(void)
{
    QTestState *qts = hp_i2000_start("2G");

    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x11);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_COMMAND, 0x11);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x20);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x28);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x04);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x02);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x01);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x01);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0xfd);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0xff);

    hp_i2000_activate_i8042(qts);

    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_SELF_TEST);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x0a);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==,
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ));

    g_assert_cmphex(qtest_readb(qts, HP_I2000_PIB_INTA_PA), ==,
                    0x20U + IA64_I2000_PROFILE_I8042_KBD_IRQ);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x0b);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==,
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ));

    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    HP_I2000_I8042_SELF_TEST_OK);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x20);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_isa_pid_fanout(void)
{
    const uint8_t keyboard_vector = 0x51U;
    const uint8_t mouse_vector = 0x5cU;
    QTestState *qts = hp_i2000_start("2G");

    /* Exercise the i8042's keyboard and auxiliary IRQ outputs independently. */
    hp_i2000_activate_i8042(qts);

    hp_i2000_pid_write(
        qts, hp_i2000_pid_rte_low(IA64_I2000_PROFILE_I8042_KBD_IRQ),
        keyboard_vector);
    g_assert_false(hp_i2000_sapic_irr_has_vector(qts, keyboard_vector));
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_SELF_TEST);
    g_assert_true(hp_i2000_sapic_irr_wait_for_vector(qts, keyboard_vector));
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    HP_I2000_I8042_SELF_TEST_OK);

    hp_i2000_pid_write(
        qts, hp_i2000_pid_rte_low(IA64_I2000_PROFILE_I8042_MOUSE_IRQ),
        mouse_vector);
    g_assert_false(hp_i2000_sapic_irr_has_vector(qts, mouse_vector));
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_WRITE_AUX_OBUF);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_DATA_PORT, 0xa5U);
    g_assert_true(hp_i2000_sapic_irr_wait_for_vector(qts, mouse_vector));
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT) &
                    (HP_I2000_I8042_STATUS_OBF |
                     HP_I2000_I8042_STATUS_AUX_OBF), ==,
                    HP_I2000_I8042_STATUS_OBF |
                    HP_I2000_I8042_STATUS_AUX_OBF);
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    0xa5U);
    qtest_quit(qts);
}

static void test_hp_i2000_i8042_reset(void)
{
    QTestState *qts = hp_i2000_start("2G");
    g_autoptr(QDict) event = NULL;
    QDict *data;

    hp_i2000_activate_i8042(qts);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  IA64_I2000_PROFILE_I8042_RESET_COMMAND);
    event = qtest_qmp_eventwait_ref(qts, "RESET");
    data = qdict_get_qdict(event, "data");
    g_assert_true(qdict_get_bool(data, "guest"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-reset");
    qtest_quit(qts);
}

static void hp_i2000_assert_rage128(QTestState *qts)
{
    uint8_t rom[HP_I2000_RAGE128_ROM_SIZE];
    unsigned int devfn = PCI_DEVFN(0, 0);
    const uint32_t framebuffer_marker = 0x1280cafe;
    uint64_t scratch = hp_i2000_sparse_io_address(
        HP_I2000_RAGE128_IO_BAR + HP_I2000_RAGE128_BIOS_SCRATCH);

    hp_i2000_assert_device(qts, 3, devfn, 0x1002, 0x5046);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_CLASS_DEVICE), ==, 0x0300);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_0), ==, 0xe8000008);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_1), ==, 0x0000c001);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_2), ==, 0xe7000000);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_ROM_ADDRESS), ==, 0);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_INTERRUPT_LINE), ==, 28);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_INTERRUPT_PIN), ==, 1);

    qtest_writel(qts, HP_I2000_RAGE128_FB_BASE, framebuffer_marker);
    qtest_writel(qts, HP_I2000_RAGE128_OLD_FB_BASE, ~framebuffer_marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_RAGE128_FB_BASE), ==,
                    framebuffer_marker);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_RAGE128_MMIO_BASE +
                             HP_I2000_RAGE128_CONFIG_APER_0_BASE), ==,
                    HP_I2000_RAGE128_FB_BASE);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_RAGE128_OLD_MMIO_BASE +
                             HP_I2000_RAGE128_CONFIG_APER_0_BASE), !=,
                    HP_I2000_RAGE128_FB_BASE);

    qtest_writel(qts, scratch, 0x1280cafe);
    g_assert_cmphex(qtest_readl(qts, scratch), ==, 0x1280cafe);

    qtest_memread(qts, HP_I2000_RAGE128_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom), ==, 0xaa55);
    g_assert_cmpuint(rom[2] * 512U, ==, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom + 0x18), ==,
                    HP_I2000_RAGE128_PCIR_OFFSET);
    g_assert_cmpmem(rom + HP_I2000_RAGE128_PCIR_OFFSET, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 4),
                    ==, 0x1002);
    g_assert_cmphex(lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 6),
                    ==, 0x5046);
    g_assert_cmphex(
        lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 0x10) * 512U,
        ==, sizeof(rom));
    g_assert_cmphex(rom[HP_I2000_RAGE128_PCIR_OFFSET + 0x15], ==, 0x80);
    g_assert_cmphex(hp_i2000_checksum(rom, sizeof(rom)), ==, 0);
}

static void test_hp_i2000_graphics_defaults(void)
{
    QTestState *qts = hp_i2000_start_defaults("");

    hp_i2000_assert_device(qts, 3, PCI_DEVFN(0, 0), 0x10de, 0x0153);
    qtest_quit(qts);
}

static void test_hp_i2000_graphics_options(void)
{
    QTestState *qts = hp_i2000_start("2G");

    g_assert_cmphex(hp_i2000_config_readl(qts, 3, PCI_DEVFN(0, 0), 0), ==,
                    0xffffffffU);
    qtest_quit(qts);

    qts = hp_i2000_start_defaults("-vga none");
    g_assert_cmphex(hp_i2000_config_readl(qts, 3, PCI_DEVFN(0, 0), 0), ==,
                    0xffffffffU);
    qtest_quit(qts);

    qts = hp_i2000_start_with_options("-vga ati");
    hp_i2000_assert_rage128(qts);
    qtest_quit(qts);
}

static void test_hp_i2000_ati_i82559_mmio(void)
{
    QTestState *qts = hp_i2000_start_with_options("-vga ati");

    /* A root-3 framebuffer must not shadow root-0 NIC MMIO. */
    qtest_writeb(qts, HP_I2000_I82559_MMIO_BAR +
                      HP_I2000_I82559_SCB_ACK, UINT8_MAX);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_I82559_MMIO_BAR +
                                    HP_I2000_I82559_SCB_ACK), ==, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_quadro2(void)
{
    static const uint8_t edid_header[] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    };
    const unsigned int devfn = PCI_DEVFN(0, 0);
    const uint32_t marker = 0x1234abcd;
    const uint32_t scanout_offset = 0x1000;
    const uint32_t ramht_offset = 0x10000;
    const uint32_t ramfc_offset = 0x18000;
    const uint32_t push_dma_instance = 0x19000;
    const uint32_t vram_dma_instance = 0x19010;
    const uint32_t surface_instance = 0x19020;
    const uint32_t rectangle_instance = 0x19030;
    const uint32_t blit_instance = 0x19040;
    const uint32_t rop_instance = 0x19050;
    const uint32_t pattern_instance = 0x19060;
    const uint32_t scaled_instance = 0x19070;
    const uint32_t fb_dma_instance = 0x19080;
    const uint32_t clip_instance = 0x19090;
    const uint32_t line_instance = 0x190a0;
    const uint32_t nv4_scaled_instance = 0x190b0;
    const uint32_t alternate_surface_instance = 0x190c0;
    const uint32_t colliding_rop_instance = 0x19820;
    const uint32_t cache_filler_instance = 0x20000;
    const uint32_t push_offset = 0x20000;
    const uint32_t source_offset = 0x400000;
    const uint32_t dest_offset = 0x500000;
    const uint32_t scaled_source_offset = 0x410000;
    const uint32_t scaled_dest_offset = 0x510000;
    const uint32_t overlap_offset = 0x520000;
    const uint32_t scaled_unsigned_dest_offset = 0x530000;
    const uint32_t scaled_unsigned_x = 0x8000;
    const uint32_t pitch = 256;
    const uint32_t dma_handle = 0x80000001;
    const uint32_t surface_handle = 0x80000002;
    const uint32_t rectangle_handle = 0x80000003;
    const uint32_t blit_handle = 0x80000004;
    const uint32_t rop_handle = 0x80000005;
    const uint32_t pattern_handle = 0x80000006;
    const uint32_t scaled_handle = 0x80000007;
    const uint32_t nvidiafb_surface_handle = 0x80000010;
    const uint32_t nvidiafb_rop_handle = 0x80000011;
    const uint32_t nvidiafb_pattern_handle = 0x80000012;
    const uint32_t nvidiafb_clip_handle = 0x80000013;
    const uint32_t nvidiafb_line_handle = 0x80000014;
    const uint32_t nvidiafb_blit_handle = 0x80000015;
    const uint32_t nvidiafb_gdi_handle = 0x80000016;
    const uint32_t nvidiafb_scaled_handle = 0x80000017;
    const uint32_t alternate_surface_handle = 0x80000018;
    const uint32_t colliding_rop_handle = 0x80000019;
    const uint32_t cache_filler_handle = 0x90000000;
    const uint32_t mono_background = 0x00112233;
    const uint32_t mono_foreground = 0x00aabbcc;
    const uint32_t transparent_foreground = 0x7faabbcc;
    const uint32_t rop_pattern = 0xf0f0f0f0;
    const uint32_t rop_source = 0xcccccccc;
    const uint32_t rop_destination = 0xaaaaaaaa;
    const uint32_t line_color = 0x00010203;
    const uint32_t cpoly_color0 = 0x00030405;
    const uint32_t cpoly_color1 = 0x00060708;
    uint32_t push[2048];
    unsigned int push_count = 0;
    uint8_t edid_actual[sizeof(edid_header)];
    uint8_t bmp_header[8];
    uint8_t bmp_checksum = 0;
    uint32_t pmc_enable;
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts = hp_i2000_start_with_options("-vga quadro2");
    unsigned int i;
    unsigned int x;
    unsigned int y;

    hp_i2000_assert_device(qts, 3, devfn, 0x10de, 0x0153);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_REVISION_ID), ==, 0xa4);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_CLASS_DEVICE), ==, 0x0300);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_SUBSYSTEM_VENDOR_ID), ==, 0x10de);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_SUBSYSTEM_ID), ==, 0x006d);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 3, devfn, PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_0), ==, 0xe7000000);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_1), ==, 0xe8000008);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_INTERRUPT_LINE), ==, 28);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_INTERRUPT_PIN), ==, 1);

    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 3, devfn, PCI_CAPABILITY_LIST), ==, 0x60);
    g_assert_cmphex(hp_i2000_config_readb(qts, 3, devfn, 0x60), ==,
                    PCI_CAP_ID_PM);
    g_assert_cmphex(hp_i2000_config_readb(qts, 3, devfn, 0x61), ==, 0x44);
    g_assert_cmphex(hp_i2000_config_readb(qts, 3, devfn, 0x44), ==,
                    PCI_CAP_ID_AGP);
    g_assert_cmphex(hp_i2000_config_readb(qts, 3, devfn, 0x46), ==, 0x20);
    g_assert_cmphex(hp_i2000_config_readl(qts, 3, devfn, 0x48), ==,
                    0x1f000017);

    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_QUADRO2_MMIO_BASE +
                             HP_I2000_QUADRO2_PMC_BOOT_0), ==, 0x015000a4);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_QUADRO2_MMIO_BASE +
                             HP_I2000_QUADRO2_PFB_FIFO), ==, 0x04000000);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_QUADRO2_MMIO_BASE +
                             HP_I2000_QUADRO2_PFB_CFG1), ==, 0x15);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_QUADRO2_MMIO_BASE +
                             HP_I2000_QUADRO2_PEXTDEV_BOOT), ==, 0x50);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFB_CFG0, marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFB_CFG0), ==, marker);

    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE +
                      HP_I2000_QUADRO2_VRAM_SIZE - 16, marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PRAMIN), ==, marker);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + 4, ~marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    HP_I2000_QUADRO2_VRAM_SIZE - 12), ==,
                    ~marker);

    /*
     * Exercise the NV10 RAMHT/RAMFC DMA-pusher path with the same object
     * classes used by native 2D drivers: surfaces, pattern, ROP, clip, line,
     * GDI rectangle/text, scaled image, and image blit.
     */
    qtest_memset(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + ramht_offset, 0, 4096);
    qtest_memset(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + ramfc_offset, 0, 32);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + push_dma_instance,
                 0x00003002);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + push_dma_instance + 4,
                 0x7fff);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + push_dma_instance + 8,
                 push_offset | 2);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + push_dma_instance + 12, 2);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + vram_dma_instance,
                 0x0000303d);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + vram_dma_instance + 4,
                 HP_I2000_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + vram_dma_instance + 8, 0);
    /* Native nvidiafb uses a separate classless linear framebuffer DMA. */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance,
                 0x00003000);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance + 4,
                 HP_I2000_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance + 8, 2);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance + 12, 2);

    /* Match the NV10+ graphics-object seeds installed by nvidiafb. */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + surface_instance,
                 0x01008062);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + surface_instance + 8,
                 (fb_dma_instance >> 4) | (fb_dma_instance << 12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + rectangle_instance,
                 0x0100804a);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + rectangle_instance + 4, 2);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + blit_instance,
                 0x0100809f);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + blit_instance + 8,
                 (surface_instance >> 4) | (surface_instance << 12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + rop_instance,
                 0x01008043);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + pattern_instance,
                 0x01008044);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + pattern_instance + 4, 2);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + clip_instance,
                 0x01008019);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + line_instance,
                 0x0100a05c);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + scaled_instance, 0x89);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + nv4_scaled_instance,
                 0x01018077);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + nv4_scaled_instance + 8,
                 (surface_instance >> 4) | (surface_instance << 12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + alternate_surface_instance,
                 0x01008062);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN +
                      alternate_surface_instance + 8,
                 (fb_dma_instance >> 4) | (fb_dma_instance << 12));

    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, dma_handle, 0,
                                  vram_dma_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, surface_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  surface_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  rectangle_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  rectangle_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, blit_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  blit_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, rop_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  rop_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, pattern_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  pattern_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0, scaled_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  scaled_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_surface_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  surface_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_rop_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  rop_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_pattern_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  pattern_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_clip_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  clip_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_line_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  line_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_blit_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  blit_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_gdi_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  rectangle_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  nvidiafb_scaled_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  nv4_scaled_instance);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  alternate_surface_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  alternate_surface_instance);

    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + ramfc_offset + 0x0c,
                 push_dma_instance >> 4);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + ramfc_offset + 0x14,
                 0x00086078);

    for (i = 0; i < 8; i++) {
        push[push_count++] = 0;
    }
    /*
     * Replay nvidiafb's NVResetGraphics object bindings and format setup.
     * LINE_FORMAT is intentionally between RECT_FORMAT and ROP_SET: an
     * unsupported class 0x5c method would raise PGRAPH ILLEGAL_METHOD here
     * before the driver reaches its ROP and clipping setup.
     */
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 0, 1);
    push[push_count++] = nvidiafb_surface_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 1, 1);
    push[push_count++] = nvidiafb_pattern_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 2, 1);
    push[push_count++] = nvidiafb_rop_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 3, 1);
    push[push_count++] = nvidiafb_clip_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 4, 1);
    push[push_count++] = nvidiafb_line_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 5, 1);
    push[push_count++] = nvidiafb_blit_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 6, 1);
    push[push_count++] = nvidiafb_gdi_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 7, 1);
    push[push_count++] = nvidiafb_scaled_handle;

    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 0, 4);
    push[push_count++] = 6;
    push[push_count++] = (pitch << 16) | pitch;
    push[push_count++] = 0;
    push[push_count++] = 0;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 1, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 6, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 4, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x310, 1, 4);
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 1);
    push[push_count++] = 0xcc;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 3, 2);
    push[push_count++] = 0;
    push[push_count++] = (4U << 16) | 64U;

    /* Cover every ROP3 truth table with all P/S/D combinations per pixel. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x310, 1, 4);
    push[push_count++] = 0;
    push[push_count++] = rop_pattern;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    for (i = 0; i <= UINT8_MAX; i++) {
        push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 1);
        push[push_count++] = i;
        push[push_count++] = hp_i2000_quadro2_dma_header(0x3fc, 6, 3);
        push[push_count++] = rop_source;
        push[push_count++] = ((i & 63U) << 16) | (8U + (i >> 6));
        push[push_count++] = (1U << 16) | 1U;
    }
    push[push_count++] = hp_i2000_quadro2_dma_header(0x310, 1, 4);
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 1);
    push[push_count++] = 0xcc;

    /*
     * NV4 GDI's one-color C upload leaves zero bits transparent.  The first
     * upload also checks that bits beyond the final five pixels are ignored.
     */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x7ec, 6, 5);
    push[push_count++] = (4U << 16);
    push[push_count++] = (5U << 16) | 64U;
    push[push_count++] = transparent_foreground;
    push[push_count++] = (1U << 16) | 5U;
    push[push_count++] = (4U << 16) | 2U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x800, 6, 1);
    push[push_count++] = BIT(31) | BIT(4) | BIT(2) | BIT(0);

    /* CGA6 reverses bits in each byte; 0x9fc is the last C data alias. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x304, 6, 1);
    push[push_count++] = 1;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x7ec, 6, 5);
    push[push_count++] = (5U << 16) | 2U;
    push[push_count++] = (6U << 16) | 8U;
    push[push_count++] = transparent_foreground;
    push[push_count++] = (1U << 16) | 8U;
    push[push_count++] = 5U << 16;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x9fc, 6, 1);
    push[push_count++] = BIT(1);
    push[push_count++] = hp_i2000_quadro2_dma_header(0x304, 6, 1);
    push[push_count++] = 2;

    /* nvidiafb reverses each source byte, yielding LE bitmap bits here. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0xbe4, 6, 7);
    push[push_count++] = (1U << 16) | 2U;
    push[push_count++] = (3U << 16) | 7U;
    push[push_count++] = mono_background;
    push[push_count++] = mono_foreground;
    push[push_count++] = (2U << 16) | 32U;
    push[push_count++] = (2U << 16) | 32U;
    push[push_count++] = (1U << 16) | 2U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0xc00, 6, 2);
    push[push_count++] = 0x15;
    push[push_count++] = 0x0e;

    /* GDI inherits the currently bound pattern and ROP context objects. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x310, 1, 4);
    push[push_count++] = 0;
    push[push_count++] = 0x00ff00ff;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 1);
    push[push_count++] = 0xca;
    push[push_count++] = hp_i2000_quadro2_dma_header(0xbe4, 6, 7);
    push[push_count++] = (1U << 16) | 10U;
    push[push_count++] = (2U << 16) | 11U;
    push[push_count++] = mono_background;
    push[push_count++] = mono_foreground;
    push[push_count++] = (1U << 16) | 32U;
    push[push_count++] = (1U << 16) | 32U;
    push[push_count++] = (1U << 16) | 10U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0xc00, 6, 1);
    push[push_count++] = 1;

    /* Transparent pixels bypass the ROP; foreground still uses ROP/pattern. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x7ec, 6, 5);
    push[push_count++] = (6U << 16) | 10U;
    push[push_count++] = (7U << 16) | 12U;
    push[push_count++] = mono_foreground;
    push[push_count++] = (1U << 16) | 2U;
    push[push_count++] = (6U << 16) | 10U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x800, 6, 1);
    push[push_count++] = 1;

    /*
     * Replay the class 0x5c sequence used by Xorg's NV solid-line hooks.
     * Native objects inherit the bound surface, clip, pattern, and ROP.
     */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 3, 2);
    push[push_count++] = (3U << 16) | 42U;
    push[push_count++] = (1U << 16) | 6U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x304, 4, 1);
    push[push_count++] = mono_foreground;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 2);
    push[push_count++] = (3U << 16) | 40U;
    push[push_count++] = (3U << 16) | 50U;

    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 3, 2);
    push[push_count++] = 0;
    push[push_count++] = (4U << 16) | 64U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 1);
    push[push_count++] = 0xcc;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x304, 4, 1);
    push[push_count++] = line_color;

    /* A LIN excludes its second endpoint. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 2);
    push[push_count++] = 20U;
    push[push_count++] = 24U;

    /* Xorg includes the last pixel by submitting a one-pixel second LIN. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 4);
    push[push_count++] = 26U;
    push[push_count++] = 30U;
    push[push_count++] = 30U;
    push[push_count++] = (1U << 16) | 30U;

    /* Exercise the documented minor-axis tie direction. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 2);
    push[push_count++] = 34U;
    push[push_count++] = (2U << 16) | 38U;

    /* The caller's second endpoint stays excluded after raster reordering. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 2);
    push[push_count++] = (2U << 16) | 56U;
    push[push_count++] = 52U;

    /* LINE32 accepts separate, sign-extended X/Y coordinate words. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x4f0, 4, 4);
    push[push_count++] = -2;
    push[push_count++] = 3;
    push[push_count++] = 4;
    push[push_count++] = 3;

    /* POLYLINE continues from a seed vertex and retains each endpoint. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 1);
    push[push_count++] = 8;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x500, 4, 1);
    push[push_count++] = 12;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x540, 4, 1);
    push[push_count++] = (3U << 16) | 12U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x57c, 4, 1);
    push[push_count++] = (3U << 16) | 16U;

    /* POLYLINE32 has the same continuation rule with separate words. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x480, 4, 2);
    push[push_count++] = 58;
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x5f8, 4, 2);
    push[push_count++] = 58;
    push[push_count++] = 0;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x5f8, 4, 2);
    push[push_count++] = 62;
    push[push_count++] = 0;

    /* CPOLYLINE applies each color before drawing to the paired point. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 4, 1);
    push[push_count++] = (3U << 16) | 40U;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x678, 4, 2);
    push[push_count++] = cpoly_color0;
    push[push_count++] = 40;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x678, 4, 2);
    push[push_count++] = cpoly_color1;
    push[push_count++] = 44;

    push[push_count++] = hp_i2000_quadro2_dma_header(0, 0, 1);
    push[push_count++] = surface_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x184, 0, 2);
    push[push_count++] = dma_handle;
    push[push_count++] = dma_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 0, 4);
    push[push_count++] = 7;
    push[push_count++] = (pitch << 16) | pitch;
    push[push_count++] = source_offset;
    push[push_count++] = source_offset;

    push[push_count++] = hp_i2000_quadro2_dma_header(0, 2, 1);
    push[push_count++] = pattern_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 2, 8);
    push[push_count++] = 3;
    push[push_count++] = 2;
    push[push_count++] = 0;
    push[push_count++] = 1;
    push[push_count++] = 0;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;
    push[push_count++] = UINT32_MAX;

    push[push_count++] = hp_i2000_quadro2_dma_header(0, 3, 1);
    push[push_count++] = rop_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 3, 1);
    push[push_count++] = 0xcc;

    push[push_count++] = hp_i2000_quadro2_dma_header(0, 1, 1);
    push[push_count++] = rectangle_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x188, 1, 2);
    push[push_count++] = pattern_handle;
    push[push_count++] = rop_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x198, 1, 1);
    push[push_count++] = surface_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x2fc, 1, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 1, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x3fc, 1, 1);
    push[push_count++] = 0x00ff0000;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 1, 2);
    push[push_count++] = (3U << 16) | 2;
    push[push_count++] = (5U << 16) | 3;

    push[push_count++] = hp_i2000_quadro2_dma_header(0x30c, 0, 1);
    push[push_count++] = dest_offset;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 4, 1);
    push[push_count++] = blit_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x18c, 4, 2);
    push[push_count++] = pattern_handle;
    push[push_count++] = rop_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x19c, 4, 1);
    push[push_count++] = surface_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x2fc, 4, 1);
    push[push_count++] = 3;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 4, 3);
    push[push_count++] = (2U << 16) | 3;
    push[push_count++] = (4U << 16) | 7;
    push[push_count++] = (3U << 16) | 5;

    push[push_count++] = hp_i2000_quadro2_dma_header(0x30c, 0, 1);
    push[push_count++] = scaled_dest_offset;
    push[push_count++] = hp_i2000_quadro2_dma_header(0, 5, 1);
    push[push_count++] = scaled_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x184, 5, 1);
    push[push_count++] = dma_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x198, 5, 1);
    push[push_count++] = surface_handle;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x2fc, 5, 9);
    push[push_count++] = 1;
    push[push_count++] = 4;
    push[push_count++] = 3;
    push[push_count++] = (6U << 16) | 10;
    push[push_count++] = (4U << 16) | 4;
    push[push_count++] = (6U << 16) | 10;
    push[push_count++] = (4U << 16) | 4;
    push[push_count++] = 1U << 19;
    push[push_count++] = 1U << 19;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 5, 4);
    push[push_count++] = (2U << 16) | 2;
    push[push_count++] = 8 | 0x00010000;
    push[push_count++] = scaled_source_offset;
    push[push_count++] = 0;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x30c, 0, 1);
    push[push_count++] = dest_offset;

    /* An overlapping same-surface blit must behave like a directional copy. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 0, 4);
    push[push_count++] = 7;
    push[push_count++] = (pitch << 16) | pitch;
    push[push_count++] = overlap_offset;
    push[push_count++] = overlap_offset;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 4, 3);
    push[push_count++] = 0;
    push[push_count++] = 1;
    push[push_count++] = (1U << 16) | 4;

    /* xy16 output coordinates are unsigned, including bit 15. */
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 0, 4);
    push[push_count++] = 1;
    push[push_count++] = (UINT32_C(0xffff) << 16) | pitch;
    push[push_count++] = scaled_unsigned_dest_offset;
    push[push_count++] = scaled_unsigned_dest_offset;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x2fc, 5, 9);
    push[push_count++] = 1;
    push[push_count++] = 4;
    push[push_count++] = 3;
    push[push_count++] = scaled_unsigned_x;
    push[push_count++] = (1U << 16) | 1;
    push[push_count++] = scaled_unsigned_x;
    push[push_count++] = (1U << 16) | 1;
    push[push_count++] = 1U << 20;
    push[push_count++] = 1U << 20;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x400, 5, 4);
    push[push_count++] = (1U << 16) | 2;
    push[push_count++] = 8 | 0x00010000;
    push[push_count++] = scaled_source_offset;
    push[push_count++] = 0;
    push[push_count++] = hp_i2000_quadro2_dma_header(0x300, 0, 4);
    push[push_count++] = 7;
    push[push_count++] = (pitch << 16) | pitch;
    push[push_count++] = source_offset;
    push[push_count++] = dest_offset;
    g_assert_cmpuint(push_count, <=, ARRAY_SIZE(push));

    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE, 0x5a, pitch * 12);
    for (i = 0; i <= UINT8_MAX; i++) {
        qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE +
                          (8U + (i >> 6)) * pitch + (i & 63U) * 4,
                     rop_destination);
    }
    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE + source_offset, 0,
                 pitch * 8);
    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE + dest_offset, 0,
                 pitch * 8);
    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE + scaled_dest_offset, 0,
                 pitch * 12);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + scaled_source_offset,
                 0xffff0000);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + scaled_source_offset + 4,
                 0xff00ff00);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + scaled_source_offset + 8,
                 0xff0000ff);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + scaled_source_offset + 12,
                 0xffffffff);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + overlap_offset,
                 0x11111111);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + overlap_offset + 4,
                 0x22222222);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + overlap_offset + 8,
                 0x33333333);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + overlap_offset + 12,
                 0x44444444);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + overlap_offset + 16,
                 0x55555555);
    qtest_writeb(qts, HP_I2000_QUADRO2_FB_BASE +
                      scaled_unsigned_dest_offset + scaled_unsigned_x, 0);
    for (i = 0; i < push_count; i++) {
        qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + i * 4,
                     push[i]);
    }
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_RAMHT,
                 0x03000000 | (ramht_offset >> 8));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_RAMFC,
                 ramfc_offset >> 8);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_MODE, 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_DMA_PUSH, 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT,
                 push_count * 4);

    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER +
                                    HP_I2000_QUADRO2_USER_DMA_GET), ==,
                    push_count * 4);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_DMA_GET), ==,
                    push_count * 4);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_DMA_STATE) &
                    0xe0000000, ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) &
                    (BIT(0) | BIT(12)), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_INTR) & BIT(0),
                    ==, 0);
    /* P, S, and D enumerate all eight truth-table inputs in every byte. */
    for (i = 0; i <= UINT8_MAX; i++) {
        uint32_t expected = i | (i << 8) | (i << 16) | (i << 24);

        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         (8U + (i >> 6)) * pitch +
                                         (i & 63U) * 4), ==, expected);
    }
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 8; x++) {
            uint32_t expected = 0x5a5a5a5a;

            if (y >= 1 && y <= 2 && x >= 2 && x < 7) {
                uint32_t bitmap = y == 1 ? 0x15 : 0x0e;

                expected = bitmap & BIT(x - 2) ?
                           mono_foreground : mono_background;
            } else if (y == 3 && x < 4) {
                expected = line_color;
            }
            g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                             y * pitch + x * 4), ==,
                            expected);
        }
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + pitch +
                                    10 * 4), ==, 0x5aaa5acc);
    for (x = 0; x < 8; x++) {
        uint32_t expected = x == 2 || x == 4 || x == 6 ?
                            transparent_foreground : 0x5a5a5a5a;

        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         4 * pitch + x * 4), ==, expected);
    }
    for (x = 0; x < 8; x++) {
        uint32_t expected = x == 6 ?
                            transparent_foreground : 0x5a5a5a5a;

        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         5 * pitch + x * 4), ==, expected);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    6 * pitch + 10 * 4), ==, 0x5aaa5acc);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    6 * pitch + 11 * 4), ==, 0x5a5a5a5a);
    for (x = 40; x < 50; x++) {
        uint32_t expected = 0x5a5a5a5a;

        if (x == 40) {
            expected = cpoly_color0;
        } else if (x >= 42 && x < 48) {
            expected = 0x5aaa5acc;
        }

        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         3 * pitch + x * 4), ==, expected);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    19 * 4), ==, 0x5a5a5a5a);
    for (x = 20; x < 24; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         x * 4), ==, line_color);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    24 * 4), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    25 * 4), ==, 0x5a5a5a5a);
    for (x = 26; x <= 30; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         x * 4), ==, line_color);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    31 * 4), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    34 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + pitch +
                                    35 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + pitch +
                                    36 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 2 * pitch +
                                    37 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 2 * pitch +
                                    38 * 4), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    52 * 4), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + pitch +
                                    53 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + pitch +
                                    54 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 2 * pitch +
                                    55 * 4), ==, line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 2 * pitch +
                                    56 * 4), ==, line_color);
    for (x = 0; x < 4; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         3 * pitch + x * 4), ==,
                        line_color);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    3 * pitch + 4 * 4), ==, 0x5a5a5a5a);
    for (x = 8; x < 12; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + x * 4),
                        ==, line_color);
    }
    for (y = 0; y < 3; y++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         y * pitch + 12 * 4), ==,
                        line_color);
    }
    for (x = 12; x < 16; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         3 * pitch + x * 4), ==,
                        line_color);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    3 * pitch + 16 * 4), ==, 0x5a5a5a5a);
    for (y = 1; y <= 3; y++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         y * pitch + 58 * 4), ==,
                        line_color);
    }
    for (x = 58; x < 62; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + x * 4),
                        ==, line_color);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 62 * 4),
                    ==, 0x5a5a5a5a);
    for (y = 1; y <= 3; y++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                         y * pitch + 40 * 4), ==,
                        cpoly_color0);
    }
    for (x = 40; x < 44; x++) {
        g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + x * 4),
                        ==, cpoly_color1);
    }
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 44 * 4),
                    ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    source_offset + 2 * pitch + 3 * 4), ==,
                    0x00ff0000);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + dest_offset +
                                    4 * pitch + 7 * 4), ==, 0x00ff0000);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    scaled_dest_offset + 6 * pitch + 10 * 4),
                    ==, 0xffff0000);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    scaled_dest_offset + 6 * pitch + 13 * 4),
                    ==, 0xff00ff00);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    scaled_dest_offset + 9 * pitch + 10 * 4),
                    ==, 0xff0000ff);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    scaled_dest_offset + 9 * pitch + 13 * 4),
                    ==, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    overlap_offset + 4), ==, 0x11111111);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    overlap_offset + 8), ==, 0x22222222);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    overlap_offset + 12), ==, 0x33333333);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    overlap_offset + 16), ==, 0x44444444);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_QUADRO2_FB_BASE +
                                    scaled_unsigned_dest_offset +
                                    scaled_unsigned_x), ==, 76);

    /* NV15 accepts the old NV4 jump encoding and skips the intervening word. */
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x800,
                 0x20000808);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x804,
                 0xffffffff);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x808,
                 hp_i2000_quadro2_dma_header(0x50, 0, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x80c,
                 marker);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_GET, 0x800);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT, 0x810);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER +
                                    HP_I2000_QUADRO2_USER_DMA_GET), ==,
                    0x810);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER + 0x48), ==,
                    marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(12),
                    ==, 0);

    /* The low-two-bits new jump is NV1A+ and must fault on Quadro2/NV15. */
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x820,
                 0x00000825);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_GET, 0x820);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT, 0x824);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                    HP_I2000_QUADRO2_PFIFO_DMA_STATE) &
                    0xe0000000U, ==,
                    HP_I2000_QUADRO2_PFIFO_DMA_ERROR_INVALID_COMMAND);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_DMA_PUSH) &
                    HP_I2000_QUADRO2_PFIFO_DMA_PUSH_STATUS, ==,
                    HP_I2000_QUADRO2_PFIFO_DMA_PUSH_STATUS);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(12),
                    ==, BIT(12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_INTR, BIT(12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_DMA_STATE, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_DMA_PUSH, 1);

    /* Only OBJECT and REF_CNT are built in before NV1A. */
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x840,
                 hp_i2000_quadro2_dma_header(0x54, 0, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x844, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_GET, 0x840);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT, 0x848);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                    HP_I2000_QUADRO2_PFIFO_DMA_STATE) &
                    0xe0000000U, ==,
                    HP_I2000_QUADRO2_PFIFO_DMA_ERROR_INVALID_METHOD);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(12),
                    ==, BIT(12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_INTR, BIT(12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_DMA_STATE, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_DMA_PUSH, 1);

    /* Class 0x5c's method arrays end at CPOLYLINE[15].XY (0x67c). */
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x860,
                 hp_i2000_quadro2_dma_header(0, 4, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x864,
                 nvidiafb_line_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x868,
                 hp_i2000_quadro2_dma_header(0x680, 4, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x86c, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_GET, 0x860);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT, 0x870);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER +
                                    HP_I2000_QUADRO2_USER_DMA_GET), ==,
                    0x870);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_DMA_STATE) &
                    0xe0000000U, ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(12),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_INTR) &
                    HP_I2000_QUADRO2_PGRAPH_INTR_ERROR, ==,
                    HP_I2000_QUADRO2_PGRAPH_INTR_ERROR);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSTATUS) &
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION, ==,
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSOURCE) &
                    HP_I2000_QUADRO2_PGRAPH_NSOURCE_ILLEGAL_METHOD, ==,
                    HP_I2000_QUADRO2_PGRAPH_NSOURCE_ILLEGAL_METHOD);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PGRAPH_INTR,
                 HP_I2000_QUADRO2_PGRAPH_INTR_ERROR);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSTATUS) &
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION, ==,
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSOURCE), ==, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PGRAPH_FIFO_ACCESS, 1);

    /* The one-color C data aliases end at 0x9fc. */
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x880,
                 hp_i2000_quadro2_dma_header(0, 6, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x884,
                 nvidiafb_gdi_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x888,
                 hp_i2000_quadro2_dma_header(0xa00, 6, 1));
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + push_offset + 0x88c, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_GET, 0x880);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER +
                      HP_I2000_QUADRO2_USER_DMA_PUT, 0x890);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER +
                                    HP_I2000_QUADRO2_USER_DMA_GET), ==,
                    0x890);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(12),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_INTR) &
                    HP_I2000_QUADRO2_PGRAPH_INTR_ERROR, ==,
                    HP_I2000_QUADRO2_PGRAPH_INTR_ERROR);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSTATUS) &
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION, ==,
                    HP_I2000_QUADRO2_PGRAPH_NSTATUS_PROTECTION);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PGRAPH_NSOURCE) &
                    HP_I2000_QUADRO2_PGRAPH_NSOURCE_ILLEGAL_METHOD, ==,
                    HP_I2000_QUADRO2_PGRAPH_NSOURCE_ILLEGAL_METHOD);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PGRAPH_INTR,
                 HP_I2000_QUADRO2_PGRAPH_INTR_ERROR);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PGRAPH_FIFO_ACCESS, 1);

    /* PIO submissions share the same bound object state. */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_MODE, 0);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER + 0x2010), ==,
                    0x1ffc);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_USER + 0x2000), ==, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000, rectangle_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000 + 0x3fc, 0x0000ff00);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000 + 0x400, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000 + 0x404,
                 (1U << 16) | 1);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + dest_offset),
                    ==, 0x0000ff00);

    /*
     * A PGRAPH-only reset retains PFIFO's subchannel bindings.  Reprogram
     * the lost engine state without rebinding either graphics object.  Keep
     * a second surface on a higher-numbered subchannel so rehydration cannot
     * accidentally replace the most recently bound active surface.
     */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xe000,
                 alternate_surface_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER, nvidiafb_surface_handle);
    pmc_enable = qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                  HP_I2000_QUADRO2_PMC_ENABLE);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PMC_ENABLE,
                 pmc_enable & ~BIT(12));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PMC_ENABLE, pmc_enable);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x300, 6);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x304,
                 (pitch << 16) | pitch);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x308, source_offset);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x30c, dest_offset);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x2fc, 3);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x3fc, marker);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x400, 1U << 16);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x404,
                 (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    dest_offset + 4), ==, marker);

    /* A single MMIO submission cannot monopolize the main loop. */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000 + 0x400, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x2000 + 0x404,
                 (2048U << 16) | 4096);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PFIFO_INTR) & BIT(0),
                    ==, BIT(0));
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_INTR, BIT(0));

    /*
     * A sparse negative-slope line may have an out-of-VRAM lower-right
     * bounding corner even though every pixel it writes is in VRAM.
     */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance + 4,
                 2 * HP_I2000_QUADRO2_VRAM_SIZE - 1);
    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE +
                      HP_I2000_QUADRO2_VRAM_SIZE - 20, 0, 20);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x300, 6);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x304, (8U << 16) | 8U);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x30c,
                 HP_I2000_QUADRO2_VRAM_SIZE - 20);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x6000,
                 nvidiafb_clip_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x6000 + 0x300, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x6000 + 0x304,
                 (3U << 16) | 2U);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x4000,
                 nvidiafb_rop_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x4000 + 0x300, 0xcc);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x8000,
                 nvidiafb_line_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x8000 + 0x2fc, 3);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x8000 + 0x304, line_color);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x8000 + 0x400, 2U << 16);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x8000 + 0x404, 2U);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    HP_I2000_QUADRO2_VRAM_SIZE - 4), ==,
                    line_color);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    HP_I2000_QUADRO2_VRAM_SIZE - 8), ==,
                    line_color);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + fb_dma_instance + 4,
                 HP_I2000_QUADRO2_VRAM_SIZE - 1);

    /* Cache eviction must not replace an object bound on this channel. */
    for (i = 0; i < 2 * HP_I2000_QUADRO2_OBJECT_CACHE; i++) {
        uint32_t handle = cache_filler_handle + i;
        uint32_t instance = cache_filler_instance + i * 16;

        qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                          HP_I2000_QUADRO2_PRAMIN + instance, 0x01008043);
        hp_i2000_quadro2_ramht_insert(
            qts, ramht_offset, 9, 0, handle,
            HP_I2000_QUADRO2_RAMHT_GRAPHICS, instance);
        qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                          HP_I2000_QUADRO2_USER + 0xe000, handle);
    }
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PRAMIN + colliding_rop_instance,
                 0x01008043);
    hp_i2000_quadro2_ramht_insert(qts, ramht_offset, 9, 0,
                                  colliding_rop_handle,
                                  HP_I2000_QUADRO2_RAMHT_GRAPHICS,
                                  colliding_rop_instance);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER, nvidiafb_surface_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xe000,
                 colliding_rop_handle);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x300, 6);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x304,
                 (pitch << 16) | pitch);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x30c, dest_offset);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x6000 + 0x300, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x6000 + 0x304,
                 (4U << 16) | 64U);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x4000 + 0x300, 0xcc);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x2fc, 3);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x3fc, marker);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x400, 2U << 16);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0xc000 + 0x404,
                 (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE +
                                    dest_offset + 8), ==, marker);

    hp_i2000_assert_int10_rom_device(qts, 0x10de, 0x0153);
    hp_i2000_assert_int10_vbe(qts, HP_I2000_QUADRO2_FB_BASE);
    qtest_memread(qts, HP_I2000_RAGE128_ROM_BASE +
                       HP_I2000_QUADRO2_BMP_OFFSET,
                  bmp_header, sizeof(bmp_header));
    g_assert_cmpmem(bmp_header, 5, "\xff\x7f" "NV\0", 5);
    for (i = 0; i < ARRAY_SIZE(bmp_header); i++) {
        bmp_checksum += bmp_header[i];
    }
    g_assert_cmpuint(bmp_checksum, ==, 0);
    g_assert_cmpuint(qtest_readl(qts, HP_I2000_RAGE128_ROM_BASE +
                                     HP_I2000_QUADRO2_BMP_OFFSET + 67), ==,
                     350000);
    g_assert_cmpuint(qtest_readl(qts, HP_I2000_RAGE128_ROM_BASE +
                                     HP_I2000_QUADRO2_BMP_OFFSET + 71), ==,
                     128000);

    /* Read the EDID through the NV15 extended-CRTC DDC GPIO pins. */
    hp_i2000_outb(qts, HP_I2000_VGA_MISC_WRITE_PORT, 0x01);
    hp_i2000_quadro2_ddc_start(qts);
    g_assert_true(hp_i2000_quadro2_ddc_send(qts, 0xa0));
    g_assert_true(hp_i2000_quadro2_ddc_send(qts, 0));
    hp_i2000_quadro2_ddc_start(qts);
    g_assert_true(hp_i2000_quadro2_ddc_send(qts, 0xa1));
    for (i = 0; i < ARRAY_SIZE(edid_actual); i++) {
        edid_actual[i] = hp_i2000_quadro2_ddc_read(
            qts, i + 1 < ARRAY_SIZE(edid_actual));
    }
    hp_i2000_quadro2_ddc_stop(qts);
    g_assert_cmpmem(edid_actual, sizeof(edid_actual), edid_header,
                    sizeof(edid_header));

    /* PCRTC vblank is routed through PMC and uses write-one-to-clear. */
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PMC_INTR_EN, 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PCRTC_INTR_EN, 1);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PCRTC_INTR) & 1,
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PMC_INTR) &
                    HP_I2000_QUADRO2_INTR_PCRTC,
                    ==, HP_I2000_QUADRO2_INTR_PCRTC);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PCRTC_INTR, 1);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PMC_INTR) &
                    HP_I2000_QUADRO2_INTR_PCRTC, ==, 0);
    /* Unrelated interrupt masks must not stop the PCRTC timer. */
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_INTR_EN, 0);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND / 60 + 1);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                    HP_I2000_QUADRO2_PCRTC_INTR) & 1,
                    ==, 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PCRTC_INTR, 1);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PCRTC_INTR_EN, 0);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");

    /*
     * Seed ordinary VGA packed-pixel registers through VBE, then leave VBE
     * and select the framebuffer solely through NV15 native registers.
     */
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT, VBE_DISPI_INDEX_XRES);
    hp_i2000_outw(qts, HP_I2000_VBE_DATA_PORT, 640);
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT, VBE_DISPI_INDEX_YRES);
    hp_i2000_outw(qts, HP_I2000_VBE_DATA_PORT, 480);
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT, VBE_DISPI_INDEX_BPP);
    hp_i2000_outw(qts, HP_I2000_VBE_DATA_PORT, 32);
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT, VBE_DISPI_INDEX_ENABLE);
    hp_i2000_outw(qts, HP_I2000_VBE_DATA_PORT,
                  VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED |
                  VBE_DISPI_NOCLEARMEM);
    hp_i2000_outb(qts, HP_I2000_VGA_SEQ_INDEX_PORT, 0);
    hp_i2000_outb(qts, HP_I2000_VGA_SEQ_DATA_PORT, 1);
    hp_i2000_outb(qts, HP_I2000_VGA_SEQ_DATA_PORT, 3);
    hp_i2000_quadro2_crtc_write(qts, 0x19, 0x20);
    hp_i2000_quadro2_crtc_write(qts, 0x42, 0);
    hp_i2000_quadro2_crtc_write(qts, 0x28, 3);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PCRTC_START, scanout_offset);
    hp_i2000_inb(qts, HP_I2000_VGA_STATUS_PORT);
    hp_i2000_outb(qts, HP_I2000_VGA_ATTR_INDEX_PORT, 0x20);
    qtest_memset(qts, HP_I2000_QUADRO2_FB_BASE + scanout_offset, 0,
                 640 * 480 * 4);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + scanout_offset,
                 0x00ff0000);

    tmpdir = g_dir_make_tmp("hp-i2000-quadro2-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "native.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    hp_i2000_assert_ppm_pixel(ppm, 640, 480, 0, 0, 0xff, 0, 0);

    hp_i2000_config_writel(qts, 3, devfn, PCI_BASE_ADDRESS_0, 0);
    hp_i2000_config_writew(qts, 3, devfn, PCI_COMMAND, 0);
    qtest_system_reset(qts);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 3, devfn, PCI_BASE_ADDRESS_0), ==, 0xe7000000);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_QUADRO2_MMIO_BASE +
                             HP_I2000_QUADRO2_PMC_BOOT_0), ==, 0x015000a4);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_hp_i2000_int10(void)
{
    uint8_t first_marker[16];
    uint8_t last_marker[16];
    uint8_t actual[16];
    uint8_t zero[16] = { 0 };
    uint64_t last = HP_I2000_RAGE128_FB_BASE +
        HP_I2000_VGA_PLANAR_SIZE - sizeof(last_marker);
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts = hp_i2000_start_with_options("-vga ati");

    hp_i2000_assert_int10_rom(qts);
    hp_i2000_assert_int10_vbe(qts, HP_I2000_RAGE128_FB_BASE);

    memset(first_marker, 0xa5, sizeof(first_marker));
    memset(last_marker, 0x5a, sizeof(last_marker));
    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0012);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));

    qtest_writeb(qts, HP_I2000_VGA_LEGACY_BASE, 0xff);
    tmpdir = g_dir_make_tmp("hp-i2000-mode12-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "mode12.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    hp_i2000_assert_ppm_pixel(ppm, 640, 480, 0, 0,
                              0xff, 0xff, 0xff);
    hp_i2000_assert_ppm_pixel(ppm, 640, 480, 8, 0, 0, 0, 0);

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0092);
    hp_i2000_assert_mode12(qts, true);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    first_marker, sizeof(first_marker));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    last_marker, sizeof(last_marker));

    qtest_writeb(qts, HP_I2000_RAGE128_ROM_BASE, 0);
    qtest_writel(qts, HP_I2000_INT10_VECTOR_ADDR, 0);
    hp_i2000_outw(qts, HP_I2000_INT10_IO_BASE, 0xffff);
    qtest_system_reset(qts);
    hp_i2000_assert_int10_rom(qts);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE), ==, 0);

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0012);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_hp_i2000_nvram(void)
{
    const uint64_t initial_value = UINT64_C(0x1122334455667788);
    const uint64_t committed_value = UINT64_C(0x8877665544332211);
    const uint64_t volatile_value = UINT64_C(0xa5a5a5a55a5a5a5a);
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *machine_options = NULL;
    g_autofree char *machine_name = NULL;
    g_autofree uint8_t *prefix =
        g_malloc0(IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_autofree char *contents = NULL;
    g_autofree char *oversized = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(QDict) response = NULL;
    gsize length = 0;
    QTestState *qts;

    qts = hp_i2000_start("2G");
    response = qtest_qmp(
        qts, "{'execute':'qom-get','arguments':"
             "{'path':'/machine','property':'nvram'}}");
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, "none");
    qobject_unref(response);
    response = NULL;
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, false);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==, 0);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==,
                    initial_value);
    qtest_quit(qts);

    qts = hp_i2000_start("2G");
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==, 0);
    qtest_quit(qts);

    tmpdir = g_dir_make_tmp("hp-i2000-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    machine_options = g_strdup_printf(",nvram=%s", quoted_path);
    machine_name = g_strdup_printf("hp-i2000,nvram=%s", path);

    /* A missing backing file is created only by an explicit commit. */
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, initial_value);
    g_assert_cmphex((uint8_t)contents[length - 1], ==, 0);
    g_clear_pointer(&contents, g_free);

    /* An existing empty file also remains empty until a commit. */
    g_assert_true(g_file_set_contents(path, "", 0, &error));
    g_assert_no_error(error);
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 0);
    g_clear_pointer(&contents, g_free);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, initial_value);
    g_clear_pointer(&contents, g_free);

    /* A legacy 64 KiB file must remain 64 KiB across load and commit. */
    stq_le_p(prefix, initial_value);
    stq_le_p(prefix + sizeof(initial_value), ~initial_value);
    g_assert_true(g_file_set_contents(
        path, (const char *)prefix, IA64_PLATFORM_MIN_NVRAM_SIZE, &error));
    g_assert_no_error(error);
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, true);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==,
                    initial_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             sizeof(initial_value)), ==,
                    ~initial_value);
    g_assert_cmphex(qtest_readb(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_I2000_PROFILE_NVRAM_SIZE - 1U), ==, 0);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_assert_cmpmem(contents, IA64_PLATFORM_MIN_NVRAM_SIZE,
                    prefix, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80,
                 committed_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80,
                 volatile_value);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    committed_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80), ==,
                    volatile_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE);
    stq_le_p(prefix + 0x80, committed_value);
    g_assert_cmpmem(contents, IA64_PLATFORM_MIN_NVRAM_SIZE,
                    prefix, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    committed_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80), ==, 0);
    qtest_quit(qts);

    oversized = g_malloc0(IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    memset(oversized, 0x7d, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_assert_true(g_file_set_contents(
        path, oversized, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U, &error));
    g_assert_no_error(error);
    hp_i2000_assert_start_fails_with_machine(
        machine_name, "-m", "2G", "must be 65536 or 524288 bytes");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_assert_cmpmem(contents, length, oversized,
                    IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_clear_pointer(&contents, g_free);
    g_clear_pointer(&oversized, g_free);

    oversized = g_malloc0(IA64_I2000_PROFILE_NVRAM_SIZE + 1U);
    g_assert_true(g_file_set_contents(
        path, oversized, IA64_I2000_PROFILE_NVRAM_SIZE + 1U, &error));
    g_assert_no_error(error);
    hp_i2000_assert_start_fails_with_machine(
        machine_name, "-m", "2G", "exceeds 524288 bytes");

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void hp_i2000_wait_for_migration(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 60 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-migrate'}");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, "completed")) {
            qobject_unref(result);
            return;
        }
        if (!strcmp(status, "failed") || !strcmp(status, "cancelled")) {
            g_error("migration entered terminal status '%s'", status);
        }
        qobject_unref(result);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_hp_i2000_quadro2_migration(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/hp-i2000-quadro2-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    const uint32_t register_marker = 0x51a7c0de;
    const uint32_t vram_marker = 0x4e563135;
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    qts = hp_i2000_start_with_options("-vga quadro2");
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFB_CFG0, register_marker);
    qtest_writel(qts, HP_I2000_QUADRO2_FB_BASE + 0x1000, vram_marker);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_PFIFO_MODE, 0);
    qtest_writel(qts, HP_I2000_QUADRO2_MMIO_BASE +
                      HP_I2000_QUADRO2_USER + 0x50, register_marker);
    hp_i2000_outb(qts, HP_I2000_VGA_MISC_WRITE_PORT, 0x01);
    hp_i2000_quadro2_ddc_start(qts);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    hp_i2000_wait_for_migration(qts);
    qtest_quit(qts);

    qts = hp_i2000_start_with_options("-vga quadro2 -incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    hp_i2000_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_MMIO_BASE +
                                     HP_I2000_QUADRO2_PFB_CFG0), ==,
                    register_marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_QUADRO2_FB_BASE + 0x1000),
                    ==, vram_marker);
    hp_i2000_quadro2_ddc_stop(qts);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_hp_i2000_migration(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/hp-i2000-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    HPI2000Int10Registers int10_request = {
        .ax = 0x0012,
        .bx = 0x1357,
        .cx = 0x2468,
        .dx = 0x369a,
        .di = 0x47bc,
        .es = 0x58de,
    };
    HPI2000Int10Registers int10_result = { 0 };
    uint8_t first_marker[16];
    uint8_t last_marker[16];
    uint8_t actual[16];
    uint8_t zero[16] = { 0 };
    uint64_t last = HP_I2000_RAGE128_FB_BASE +
        HP_I2000_VGA_PLANAR_SIZE - sizeof(last_marker);
    const uint32_t cs4281_register_marker = 0x0042815a;
    const uint32_t cs4281_fifo_marker = 0xa581245a;
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    memset(first_marker, 0xa5, sizeof(first_marker));
    memset(last_marker, 0x5a, sizeof(last_marker));

    qts = hp_i2000_start_with_options("-vga ati");
    qtest_writeb(qts, HP_I2000_CF8_PA + 1, 0x5a);
    qtest_writew(qts, HP_I2000_CF8_PA + 2, 0xa55a);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80,
                 UINT64_C(0x123456789abcdef0));
    hp_i2000_cs4281_init(qts);
    hp_i2000_cs4281_codec_write(qts, 0x02, 0x0011);
    qtest_writel(qts, HP_I2000_CS4281_BA0 + HP_I2000_CS4281_PPLVC,
                 cs4281_register_marker);
    qtest_writel(qts, HP_I2000_CS4281_BA1 + 0x40,
                 cs4281_fifo_marker);
    qtest_writel(qts, HP_I2000_CS4281_BA0 + HP_I2000_CS4281_DBA2,
                 0x00045678);
    qtest_writel(qts, HP_I2000_CS4281_BA0 + HP_I2000_CS4281_DBC2,
                 0x123);
    hp_i2000_int10_write_request(qts, &int10_request);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    hp_i2000_wait_for_migration(qts);
    qtest_quit(qts);

    qts = hp_i2000_start_with_options("-vga ati -incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    hp_i2000_wait_for_migration(qts);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0x5a);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0xa55a);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    UINT64_C(0x123456789abcdef0));
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_CS4281_BA0 +
                             HP_I2000_CS4281_PPLVC), ==,
                    cs4281_register_marker);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_CS4281_BA1 + 0x40), ==,
                    cs4281_fifo_marker);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_CS4281_BA0 +
                             HP_I2000_CS4281_DBA2), ==,
                    0x00045678);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_CS4281_BA0 +
                             HP_I2000_CS4281_DBC2), ==,
                    0x123);
    g_assert_cmphex(hp_i2000_cs4281_codec_read(qts, 0x02), ==, 0x0011);

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    /* Execution copies the migrated request into the readable result bank. */
    hp_i2000_outw(qts, HP_I2000_INT10_IO_EXEC,
                  HP_I2000_INT10_TRIGGER);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_INT10_IO_EXEC), ==, 0);
    hp_i2000_int10_read_result(qts, &int10_result);
    g_assert_cmphex(int10_result.ax, ==, int10_request.ax);
    g_assert_cmphex(int10_result.bx, ==, int10_request.bx);
    g_assert_cmphex(int10_result.cx, ==, int10_request.cx);
    g_assert_cmphex(int10_result.dx, ==, int10_request.dx);
    g_assert_cmphex(int10_result.di, ==, int10_request.di);
    g_assert_cmphex(int10_result.es, ==, int10_request.es);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hp-i2000/machine-identity",
                   test_hp_i2000_machine_identity);
    qtest_add_func("/hp-i2000/default-usb-input",
                   test_hp_i2000_default_usb_input);
    qtest_add_func("/hp-i2000/constraints", test_hp_i2000_constraints);
    qtest_add_func("/hp-i2000/storage-defaults",
                   test_hp_i2000_storage_defaults);
    qtest_add_func("/hp-i2000/ram-descriptor",
                   test_hp_i2000_ram_descriptor);
    qtest_add_func("/hp-i2000/pci-dma-ram-map",
                   test_hp_i2000_pci_dma_ram_map);
    qtest_add_func("/hp-i2000/cs4281", test_hp_i2000_cs4281);
    qtest_add_func("/hp-i2000/cs4281-dma", test_hp_i2000_cs4281_dma);
    qtest_add_func("/hp-i2000/cs4281-handoff", test_hp_i2000_cs4281_handoff);
    qtest_add_func("/hp-i2000/pci-layout-reset",
                   test_hp_i2000_pci_layout_and_reset);
    qtest_add_func("/hp-i2000/acpi-pm", test_hp_i2000_acpi_pm);
    qtest_add_func("/hp-i2000/pib-inta", test_hp_i2000_pib_inta);
    qtest_add_func("/hp-i2000/isa-pid-fanout",
                   test_hp_i2000_isa_pid_fanout);
    qtest_add_func("/hp-i2000/i8042-reset", test_hp_i2000_i8042_reset);
    qtest_add_func("/hp-i2000/graphics-defaults",
                   test_hp_i2000_graphics_defaults);
    qtest_add_func("/hp-i2000/graphics-options",
                   test_hp_i2000_graphics_options);
    qtest_add_func("/hp-i2000/ati-i82559-mmio",
                   test_hp_i2000_ati_i82559_mmio);
    qtest_add_func("/hp-i2000/quadro2", test_hp_i2000_quadro2);
    qtest_add_func("/hp-i2000/int10", test_hp_i2000_int10);
    qtest_add_func("/hp-i2000/nvram", test_hp_i2000_nvram);
    qtest_add_func("/hp-i2000/quadro2-migration",
                   test_hp_i2000_quadro2_migration);
    qtest_add_func("/hp-i2000/migration", test_hp_i2000_migration);
    return g_test_run();
}
