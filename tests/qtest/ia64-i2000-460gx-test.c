/*
 * IA-64 i2000 460GX integration qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/memattrs.h"
#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"

#define PID_IOREGSEL 0x00
#define PID_IOWIN 0x10
#define PID_RTE_BASE 0x10
#define PID_RTE_DELIVERY_STATUS BIT(12)

#define DMA_TEST_LEN 4U
#define DMA_TEST_SENTINEL UINT32_C(0xa5a5a5a5)
#define DMA_TEST_LOW_RAM UINT64_C(0x00040000)

static const uint8_t fixture_first_bus[] = { 0x00, 0x01, 0x02 };
static const uint64_t fixture_mmio_base[] = {
    UINT64_C(0x90000000),
    UINT64_C(0xa0000000),
    UINT64_C(0xb0000000),
};

G_STATIC_ASSERT(G_N_ELEMENTS(fixture_first_bus) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);
G_STATIC_ASSERT(G_N_ELEMENTS(fixture_mmio_base) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);

static QTestState *fixture_start_with_options(const char *options)
{
    /*
     * The qtest device uses this machine's flat RAM and mapped PIB.
     * Teardown removes only probes and fixture regions.
     */
    return qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 2G -nodefaults "
        "-display none -net none -device %s,id=%s%s",
        TYPE_IA64_I2000_460GX_QTEST, IA64_I2000_460GX_QTEST_ID, options ?: "");
}

static QTestState *fixture_start(void)
{
    return fixture_start_with_options(NULL);
}

static uint32_t fixture_config_address(uint8_t bus, unsigned reg)
{
    return UINT32_C(0x80000000) | (uint32_t)bus << 16 |
           PCI_DEVFN(IA64_I2000_460GX_TEST_PCI_SLOT, 0) << 8 | (reg & 0xfc);
}

static void fixture_config_select(QTestState *qts, uint8_t bus, unsigned reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA,
                 fixture_config_address(bus, reg));
}

static uint16_t fixture_config_readw(QTestState *qts, uint8_t bus, unsigned reg)
{
    fixture_config_select(qts, bus, reg);
    return qtest_readw(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static void fixture_config_writew(QTestState *qts, uint8_t bus, unsigned reg,
                                  uint16_t value)
{
    fixture_config_select(qts, bus, reg);
    qtest_writew(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static void fixture_config_writel(QTestState *qts, uint8_t bus, unsigned reg,
                                  uint32_t value)
{
    fixture_config_select(qts, bus, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static uint64_t fixture_qom_get_uint(QTestState *qts, const char *path,
                                     const char *property)
{
    QDict *response = qtest_qmp(qts,
                                "{'execute':'qom-get','arguments':"
                                "{'path':%s,'property':%s}}",
                                path, property);
    uint64_t value;

    g_assert_true(qdict_haskey(response, "return"));
    value = qnum_get_uint(qobject_to(QNum, qdict_get(response, "return")));
    qobject_unref(response);
    return value;
}

static void fixture_pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN, value);
}

static uint32_t fixture_pid_read(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    return qtest_readl(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN);
}

static uint32_t fixture_pid_rte_low(unsigned pin)
{
    return PID_RTE_BASE + pin * 2;
}

static uint32_t fixture_pid_rte_high(unsigned pin)
{
    return fixture_pid_rte_low(pin) + 1;
}

static void test_map_and_cf8(void)
{
    QTestState *qts = fixture_start();
    uint32_t latch;
    unsigned i;

    g_assert_false(
        qtest_qom_get_bool(qts, IA64_I2000_460GX_TEST_DEVICE_QOM_PATH,
                           IA64_I2000_460GX_TEST_DESCRIPTOR_INSTALLED));

    /* Seed the explicit root-0 CF8 I/O backing. */
    qtest_writeb(qts, IA64_I2000_460GX_TEST_CF8_PA, 0x5a);
    qtest_writew(qts, IA64_I2000_460GX_TEST_CF8_PA + 2, 0xa55a);
    g_assert_cmphex(qtest_readb(qts, IA64_I2000_460GX_TEST_CF8_PA), ==, 0x5a);

    latch = fixture_config_address(fixture_first_bus[0], PCI_VENDOR_ID);
    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA, latch);
    g_assert_cmphex(qtest_readl(qts, IA64_I2000_460GX_TEST_CF8_PA), ==, latch);

    /* Byte/word cycles reach backing storage and never alter the latch. */
    qtest_writeb(qts, IA64_I2000_460GX_TEST_CF8_PA + 1, 0x11);
    qtest_writew(qts, IA64_I2000_460GX_TEST_CF8_PA + 2, 0x2233);
    g_assert_cmphex(qtest_readb(qts, IA64_I2000_460GX_TEST_CF8_PA + 1), ==,
                    0x11);
    g_assert_cmphex(qtest_readw(qts, IA64_I2000_460GX_TEST_CF8_PA + 2), ==,
                    0x2233);
    g_assert_cmphex(qtest_readl(qts, IA64_I2000_460GX_TEST_CF8_PA), ==, latch);
    g_assert_cmpuint(
        fixture_qom_get_uint(qts, IA64_I2000_460GX_TEST_DEVICE_QOM_PATH,
                             IA64_I2000_460GX_TEST_CF8_SUBDWORD_COUNT),
        ==, 7);

    /* CF8 dword and CFC accesses reach every numbered root. */
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        g_assert_cmphex(
            fixture_config_readw(qts, fixture_first_bus[i], PCI_VENDOR_ID), ==,
            IOMMU_TESTDEV_VENDOR_ID);
        g_assert_cmphex(
            fixture_config_readw(qts, fixture_first_bus[i], PCI_DEVICE_ID), ==,
            IOMMU_TESTDEV_DEVICE_ID);
    }
    qtest_quit(qts);
}

static void test_all_raw_intx_connections(void)
{
    QTestState *qts = fixture_start();
    unsigned line;

    for (line = 0; line < IA64_I2000_460GX_TEST_ROOT_COUNT *
                              IA64_I2000_460GX_TEST_PCI_INTX_COUNT;
         line++) {
        unsigned pin = IA64_I2000_460GX_TEST_LEGACY_PIN_COUNT + line;
        uint32_t vector = 0x40 + line;

        /* Destination ID 15 keeps delivery status set. */
        fixture_pid_write(qts, fixture_pid_rte_high(pin), 0x0f000000);
        fixture_pid_write(qts, fixture_pid_rte_low(pin), vector);
        qtest_set_irq_in(qts, IA64_I2000_460GX_QTEST_QOM_PATH,
                         IA64_I2000_460GX_TEST_GPIO_INTX, line, 1);
        g_assert_cmphex(fixture_pid_read(qts, fixture_pid_rte_low(pin)) &
                            PID_RTE_DELIVERY_STATUS,
                        !=, 0);
        qtest_set_irq_in(qts, IA64_I2000_460GX_QTEST_QOM_PATH,
                         IA64_I2000_460GX_TEST_GPIO_INTX, line, 0);
    }
    qtest_quit(qts);
}

static void test_named_legacy_connection(void)
{
    QTestState *qts = fixture_start_with_options(",legacy-pin=0");
    const unsigned pin = 0;

    /* Destination ID 15 keeps the edge-delivery status set. */
    fixture_pid_write(qts, fixture_pid_rte_high(pin), 0x0f000000);
    fixture_pid_write(qts, fixture_pid_rte_low(pin), 0x50);
    qtest_set_irq_in(qts, IA64_I2000_460GX_QTEST_QOM_PATH,
                     IA64_I2000_460GX_TEST_GPIO_LEGACY, 0, 1);
    g_assert_cmphex(fixture_pid_read(qts, fixture_pid_rte_low(pin)) &
                        PID_RTE_DELIVERY_STATUS,
                    !=, 0);
    qtest_set_irq_in(qts, IA64_I2000_460GX_QTEST_QOM_PATH,
                     IA64_I2000_460GX_TEST_GPIO_LEGACY, 0, 0);
    qtest_quit(qts);
}

static uint32_t fixture_dma_trigger(QTestState *qts, uint64_t mmio,
                                    uint64_t iova, uint64_t gpa)
{
    qtest_writel(qts, mmio + ITD_REG_DMA_GVA_LO, iova);
    qtest_writel(qts, mmio + ITD_REG_DMA_GVA_HI, iova >> 32);
    qtest_writel(qts, mmio + ITD_REG_DMA_GPA_LO, gpa);
    qtest_writel(qts, mmio + ITD_REG_DMA_GPA_HI, gpa >> 32);
    qtest_writel(qts, mmio + ITD_REG_DMA_LEN, DMA_TEST_LEN);
    qtest_writel(qts, mmio + ITD_REG_DMA_ATTRS, 0);
    qtest_writel(qts, mmio + ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);
    qtest_readl(qts, mmio + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, mmio + ITD_REG_DMA_RESULT);
}

static void test_all_root_dma_apertures(void)
{
    QTestState *qts = fixture_start();
    const uint64_t denied =
        IA64_I2000_460GX_TEST_DMA_BASE + IA64_I2000_460GX_TEST_DMA_SIZE;
    unsigned i;

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        fixture_config_writel(qts, fixture_first_bus[i], PCI_BASE_ADDRESS_0,
                              fixture_mmio_base[i]);
        fixture_config_writew(qts, fixture_first_bus[i], PCI_COMMAND,
                              PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

        qtest_writel(qts, DMA_TEST_LOW_RAM, DMA_TEST_SENTINEL);
        g_assert_cmphex(fixture_dma_trigger(qts, fixture_mmio_base[i],
                                            DMA_TEST_LOW_RAM, DMA_TEST_LOW_RAM),
                        ==, 0);
        g_assert_cmphex(
            qtest_readl(qts, fixture_mmio_base[i] + ITD_REG_DMA_MEMTX_RESULT),
            ==, MEMTX_OK);
        g_assert_cmphex(qtest_readl(qts, DMA_TEST_LOW_RAM), ==,
                        ITD_DMA_WRITE_VAL);

        qtest_writel(qts, DMA_TEST_LOW_RAM, DMA_TEST_SENTINEL);
        g_assert_cmphex(fixture_dma_trigger(qts, fixture_mmio_base[i], denied,
                                            DMA_TEST_LOW_RAM),
                        ==, ITD_DMA_ERR_TX_FAIL);
        g_assert_cmphex(
            qtest_readl(qts, fixture_mmio_base[i] + ITD_REG_DMA_MEMTX_RESULT),
            ==, MEMTX_DECODE_ERROR);
        g_assert_cmphex(qtest_readl(qts, DMA_TEST_LOW_RAM), ==,
                        DMA_TEST_SENTINEL);
    }
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("i2000-460gx-test/map-cf8", test_map_and_cf8);
    qtest_add_func("i2000-460gx-test/intx", test_all_raw_intx_connections);
    qtest_add_func("i2000-460gx-test/legacy", test_named_legacy_connection);
    qtest_add_func("i2000-460gx-test/dma", test_all_root_dma_apertures);

    return g_test_run();
}
