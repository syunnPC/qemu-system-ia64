/*
 * IA-64 zx6000 ZX1 qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/memattrs.h"
#include "hw/ia64/hp_zx6000.h"
#include "hw/ia64/ia64_ras_abi.h"
#include "hw/ia64/ia64_zx6000_zx1_test.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "hw/pci-host/hp-zx1-mio-regs.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"

#define ZX1_TEST_ROOT_COUNT                  2U
#define ZX1_TEST_PROBES_PER_ROOT             2U
#define ZX1_TEST_INTX_PINS                   4U
#define ZX1_TEST_GPIO_LINE(root, probe, pin) \
    ((((root) * ZX1_TEST_PROBES_PER_ROOT + (probe)) * \
      ZX1_TEST_INTX_PINS) + (pin))
#define ZX1_TEST_RAM_SIZE                    UINT64_C(0x20000000)
#define ZX1_TEST_MIO_BASE                    UINT64_C(0xfed00000)
#define ZX1_TEST_MIO_IOC_FUNCTION_OFFSET     UINT64_C(0x1000)
#define ZX1_TEST_IOA0_BASE                   UINT64_C(0xfed20000)
#define ZX1_TEST_IOA1_BASE                   UINT64_C(0xfed22000)
#define ZX1_TEST_ROOT0_MMIO_BASE             UINT64_C(0x90000000)
#define ZX1_TEST_ROOT1_MMIO_BASE             UINT64_C(0xa0000000)
#define ZX1_TEST_ROOT0_FIRST_BUS             UINT8_C(0x20)
#define ZX1_TEST_ROOT1_FIRST_BUS             UINT8_C(0x40)
#define ZX1_TEST_ROOT0_LAST_BUS              UINT8_C(0x2f)
#define ZX1_TEST_ROOT1_LAST_BUS              UINT8_C(0x4f)

#define ZX1_TEST_MIO_F0_ID_OFFSET            UINT64_C(0x0000)
#define ZX1_TEST_MIO_LMMIO_DIR_BASE0         UINT64_C(0x0300)
#define ZX1_TEST_MIO_F1_ID_OFFSET            UINT64_C(0x1000)
#define ZX1_TEST_MIO_ROPE_CONFIG             UINT64_C(0x1040)

#define ZX1_TEST_IOC_IOMMU_IBASE             UINT64_C(0x300)
#define ZX1_TEST_IOC_IOMMU_IMASK             UINT64_C(0x308)
#define ZX1_TEST_IOC_IOMMU_PCOM              UINT64_C(0x310)
#define ZX1_TEST_IOC_IOMMU_TCNFG             UINT64_C(0x318)
#define ZX1_TEST_IOC_IOMMU_PDIR_BASE         UINT64_C(0x320)

#define ZX1_TEST_IOA_FUNCTION_ID             UINT64_C(0x0000)
#define ZX1_TEST_IOA_CONFIG_ADDRESS          UINT64_C(0x0040)
#define ZX1_TEST_IOA_CONFIG_DATA             UINT64_C(0x0048)
#define ZX1_TEST_IOA_BUS_NUMBER              UINT64_C(0x0058)
#define ZX1_TEST_IOA_STATUS_CONTROL          UINT64_C(0x0108)
#define ZX1_TEST_IOA_MSI_BASE                UINT64_C(0x0280)
#define ZX1_TEST_IOA_MSI_MASK                UINT64_C(0x0288)
#define ZX1_TEST_IOA_IOREGSEL                UINT64_C(0x0800)
#define ZX1_TEST_IOA_IOWIN                   UINT64_C(0x0810)
#define ZX1_TEST_IOA_IOEOI                   UINT64_C(0x0840)
#define ZX1_TEST_IOA_SIC_RESET_FUNCTION      UINT8_C(0x01)

#define ZX1_TEST_SAPIC_RTE_BASE              UINT32_C(0x10)
#define ZX1_TEST_SAPIC_RTE_TRIGGER           UINT32_C(0x00008000)
#define ZX1_TEST_SAPIC_RTE_MASK              UINT32_C(0x00010000)
#define ZX1_TEST_IOPDIR_VALID_BIT            UINT64_C(0x8000000000000000)

#define ZX1_TEST_IOMMU_BASE                  UINT64_C(0x40000000)
#define ZX1_TEST_IOMMU_SIZE                  UINT64_C(0x10000000)
#define ZX1_TEST_IOMMU_IBASE_RESET           UINT64_C(0x40000000)
#define ZX1_TEST_IOMMU_IMASK_RESET           UINT64_C(0xf0000000)
#define ZX1_TEST_IOMMU_PCOM_RESET            UINT64_C(0)
#define ZX1_TEST_IOMMU_TCNFG_RESET           UINT64_C(0)
#define ZX1_TEST_PDIR_BASE                   UINT64_C(0x01000000)
#define ZX1_TEST_PAGE_SIZE                   UINT64_C(0x1000)
#define ZX1_TEST_PAGE_SHIFT                  12U
#define ZX1_TEST_INVALID_GUARD_IOVA          UINT64_C(0x10000000)
#define ZX1_TEST_INVALID_GUARD_IMASK         UINT64_C(0xfff00000)
#define ZX1_TEST_TARGET_BASE                 UINT64_C(0x02000000)
#define ZX1_TEST_TARGET_PAGES                18U

#define ZX1_TEST_BAR0_OFFSET                 UINT32_C(0x00100000)
#define ZX1_TEST_BAR1_OFFSET                 UINT32_C(0x00200000)
#define ZX1_TEST_DMA_LEN                     4U
#define ZX1_TEST_DMA_SENTINEL                UINT32_C(0xa5a5a5a5)
#define ZX1_TEST_SAPIC_MESSAGE_ASSERT        UINT32_C(0x00004000)
#define ZX1_TEST_SAPIC_MESSAGE_TRIGGER       UINT32_C(0x00008000)
#define ZX1_TEST_MSI_CAP_OFFSET              0x50U
#define ZX1_TEST_MSI_ADDRESS                 UINT64_C(0xfee00000)
#define ZX1_TEST_MSI_BASE_RESET              UINT64_C(0xfee00001)
#define ZX1_TEST_MSI_MASK_RESET              UINT64_C(0x00000ffffff00000)
#define ZX1_TEST_MSI_MASK_64K                UINT64_C(0x00000fffffff0000)

static const uint64_t zx1_test_ioa_base[ZX1_TEST_ROOT_COUNT] = {
    ZX1_TEST_IOA0_BASE,
    ZX1_TEST_IOA1_BASE,
};

static const uint64_t zx1_test_root_mmio_base[ZX1_TEST_ROOT_COUNT] = {
    ZX1_TEST_ROOT0_MMIO_BASE,
    ZX1_TEST_ROOT1_MMIO_BASE,
};

static const uint8_t zx1_test_first_bus[ZX1_TEST_ROOT_COUNT] = {
    ZX1_TEST_ROOT0_FIRST_BUS,
    ZX1_TEST_ROOT1_FIRST_BUS,
};

static const uint8_t zx1_test_last_bus[ZX1_TEST_ROOT_COUNT] = {
    ZX1_TEST_ROOT0_LAST_BUS,
    ZX1_TEST_ROOT1_LAST_BUS,
};

static const uint32_t zx1_test_bar_offset[ZX1_TEST_PROBES_PER_ROOT] = {
    ZX1_TEST_BAR0_OFFSET,
    ZX1_TEST_BAR1_OFFSET,
};

static const unsigned int zx1_test_probe_slot[ZX1_TEST_PROBES_PER_ROOT] = {
    1U,
    2U,
};

static const char *const
zx1_test_delivery_count_property[ZX1_TEST_ROOT_COUNT] = {
    IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_0,
    IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_1,
};

static const char *const zx1_test_last_address_property[ZX1_TEST_ROOT_COUNT] = {
    IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_0,
    IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_1,
};

static const char *const zx1_test_last_data_property[ZX1_TEST_ROOT_COUNT] = {
    IA64_ZX6000_ZX1_TEST_LAST_DATA_0,
    IA64_ZX6000_ZX1_TEST_LAST_DATA_1,
};

static const char *const zx1_test_last_result_property[ZX1_TEST_ROOT_COUNT] = {
    IA64_ZX6000_ZX1_TEST_LAST_RESULT_0,
    IA64_ZX6000_ZX1_TEST_LAST_RESULT_1,
};

G_STATIC_ASSERT(ZX1_TEST_ROOT_COUNT * ZX1_TEST_PROBES_PER_ROOT ==
                IA64_ZX6000_ZX1_TEST_PROBE_COUNT);
G_STATIC_ASSERT(ZX1_TEST_PROBES_PER_ROOT ==
                IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT);
G_STATIC_ASSERT(ZX1_TEST_INTX_PINS == PCI_NUM_PINS);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_PROBE_SLOT(0) == 1U);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_PROBE_SLOT(1) == 2U);
G_STATIC_ASSERT(ZX1_TEST_GPIO_LINE(0, 0, 0) ==
                IA64_ZX6000_ZX1_TEST_GPIO_LINE(0, 0, 0));
G_STATIC_ASSERT(ZX1_TEST_GPIO_LINE(0, 1, 3) ==
                IA64_ZX6000_ZX1_TEST_GPIO_LINE(0, 1, 3));
G_STATIC_ASSERT(ZX1_TEST_GPIO_LINE(1, 0, 0) ==
                IA64_ZX6000_ZX1_TEST_GPIO_LINE(1, 0, 0));
G_STATIC_ASSERT(ZX1_TEST_GPIO_LINE(1, 1, 3) ==
                IA64_ZX6000_ZX1_TEST_GPIO_LINE(1, 1, 3));
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_MSI_GPIO_LINE(1, 1) == 3U);
G_STATIC_ASSERT(ZX1_TEST_TARGET_PAGES >= 18);
G_STATIC_ASSERT(ZX1_TEST_IOMMU_SIZE == UINT64_C(0x10000000));

static QTestState *zx1_test_start(const char *extra_args)
{
    return qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 512M -nodefaults "
        "-display none -net none -device %s,id=%s %s",
        TYPE_IA64_ZX6000_ZX1_QTEST,
        IA64_ZX6000_ZX1_QTEST_ID, extra_args ?: "");
}

static uint64_t zx1_test_iommu_register(uint64_t offset)
{
    return ZX1_TEST_MIO_BASE + ZX1_TEST_MIO_IOC_FUNCTION_OFFSET + offset;
}

static uint32_t zx1_test_config_selector(uint8_t bus, unsigned int probe,
                                   unsigned int reg)
{
    unsigned int slot;

    g_assert_cmpuint(probe, <, ZX1_TEST_PROBES_PER_ROOT);
    slot = zx1_test_probe_slot[probe];
    return (uint32_t)bus << 16 | PCI_DEVFN(slot, 0) << 8 | (reg & 0xfc);
}

static void zx1_test_config_select(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int probe,
                             unsigned int reg)
{
    g_assert_cmpuint(root, <, ZX1_TEST_ROOT_COUNT);
    qtest_writeq(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_ADDRESS,
                 zx1_test_config_selector(bus, probe, reg));
}

static uint8_t zx1_test_config_readb(QTestState *qts, unsigned int root,
                               uint8_t bus, unsigned int probe,
                               unsigned int reg)
{
    zx1_test_config_select(qts, root, bus, probe, reg);
    return qtest_readb(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_DATA +
                       (reg & 3));
}

static uint16_t zx1_test_config_readw(QTestState *qts, unsigned int root,
                                uint8_t bus, unsigned int probe,
                                unsigned int reg)
{
    zx1_test_config_select(qts, root, bus, probe, reg);
    return qtest_readw(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_DATA +
                       (reg & 3));
}

static uint32_t zx1_test_config_readl(QTestState *qts, unsigned int root,
                                uint8_t bus, unsigned int probe,
                                unsigned int reg)
{
    zx1_test_config_select(qts, root, bus, probe, reg);
    return qtest_readl(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_DATA +
                       (reg & 3));
}

static void zx1_test_config_writew(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int probe,
                             unsigned int reg, uint16_t value)
{
    zx1_test_config_select(qts, root, bus, probe, reg);
    qtest_writew(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_DATA +
                 (reg & 3), value);
}

static void zx1_test_config_writel(QTestState *qts, unsigned int root,
                             uint8_t bus, unsigned int probe,
                             unsigned int reg, uint32_t value)
{
    zx1_test_config_select(qts, root, bus, probe, reg);
    qtest_writel(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_CONFIG_DATA +
                 (reg & 3), value);
}

static uint64_t zx1_test_configure_probe(QTestState *qts, unsigned int root,
                                   unsigned int probe)
{
    uint32_t bar = zx1_test_bar_offset[probe];

    zx1_test_config_writel(qts, root, 0, probe, PCI_BASE_ADDRESS_0, bar);
    zx1_test_config_writew(qts, root, 0, probe, PCI_COMMAND,
                     PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(zx1_test_config_readl(qts, root, 0, probe,
                                   PCI_BASE_ADDRESS_0), ==, bar);
    g_assert_cmphex(zx1_test_config_readw(qts, root, 0, probe, PCI_COMMAND), ==,
                    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    return zx1_test_root_mmio_base[root] + bar;
}

static uint64_t zx1_test_probe_mmio(unsigned int root, unsigned int probe)
{
    g_assert_cmpuint(root, <, ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(probe, <, ZX1_TEST_PROBES_PER_ROOT);
    return zx1_test_root_mmio_base[root] + zx1_test_bar_offset[probe];
}

static uint32_t zx1_test_dma_trigger(QTestState *qts, unsigned int root,
                               unsigned int probe, uint64_t iova,
                               uint64_t gpa)
{
    uint64_t mmio = zx1_test_probe_mmio(root, probe);

    qtest_writel(qts, mmio + ITD_REG_DMA_GVA_LO, iova);
    qtest_writel(qts, mmio + ITD_REG_DMA_GVA_HI, iova >> 32);
    qtest_writel(qts, mmio + ITD_REG_DMA_GPA_LO, gpa);
    qtest_writel(qts, mmio + ITD_REG_DMA_GPA_HI, gpa >> 32);
    qtest_writel(qts, mmio + ITD_REG_DMA_LEN, ZX1_TEST_DMA_LEN);
    qtest_writel(qts, mmio + ITD_REG_DMA_ATTRS, 0);
    qtest_writel(qts, mmio + ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);
    qtest_readl(qts, mmio + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, mmio + ITD_REG_DMA_RESULT);
}

static void zx1_test_expect_dma_success(QTestState *qts, unsigned int root,
                                  unsigned int probe, uint64_t iova,
                                  uint64_t target)
{
    uint64_t mmio = zx1_test_probe_mmio(root, probe);

    qtest_writel(qts, target, ZX1_TEST_DMA_SENTINEL);
    g_assert_cmphex(zx1_test_dma_trigger(qts, root, probe, iova, target),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, mmio + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(qts, target), ==, ITD_DMA_WRITE_VAL);
}

static void zx1_test_expect_dma_blocked_without_fallback(
    QTestState *qts, unsigned int root, unsigned int probe,
    uint64_t iova, uint64_t translated_target)
{
    uint64_t mmio = zx1_test_probe_mmio(root, probe);

    g_assert_cmphex(iova, !=, translated_target);
    qtest_writel(qts, iova, ZX1_TEST_DMA_SENTINEL);
    qtest_writel(qts, translated_target, ZX1_TEST_DMA_SENTINEL);
    g_assert_cmphex(zx1_test_dma_trigger(qts, root, probe, iova,
                                  translated_target), ==,
                    ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, mmio + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readl(qts, iova), ==, ZX1_TEST_DMA_SENTINEL);
    g_assert_cmphex(qtest_readl(qts, translated_target), ==,
                    ZX1_TEST_DMA_SENTINEL);
}

static uint64_t zx1_test_page_iova(unsigned int page)
{
    return ZX1_TEST_IOMMU_BASE + (uint64_t)page * ZX1_TEST_PAGE_SIZE;
}

static uint64_t zx1_test_page_target(unsigned int page)
{
    g_assert_cmpuint(page, <, ZX1_TEST_TARGET_PAGES);
    return ZX1_TEST_TARGET_BASE + (uint64_t)page * ZX1_TEST_PAGE_SIZE;
}

static void zx1_test_write_pte(QTestState *qts, unsigned int page,
                         uint64_t target, bool valid)
{
    uint64_t pte = target;

    g_assert_cmphex(target & (ZX1_TEST_PAGE_SIZE - 1), ==, 0);
    if (valid) {
        pte |= ZX1_TEST_IOPDIR_VALID_BIT;
    }
    qtest_writeq(qts, ZX1_TEST_PDIR_BASE + (uint64_t)page * sizeof(uint64_t),
                 pte);
}

static void zx1_test_enable_iommu(QTestState *qts)
{
    qtest_writeb(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE), 1);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE)), ==,
                    ZX1_TEST_IOMMU_IBASE_RESET | 1);
}

static void zx1_test_purge_page(QTestState *qts, uint64_t iova)
{
    g_assert_cmphex(iova & (ZX1_TEST_PAGE_SIZE - 1), ==, 0);
    qtest_writeq(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_PCOM),
                 iova | ZX1_TEST_PAGE_SHIFT);
}

static uint64_t zx1_test_qom_get_uint(QTestState *qts, const char *property)
{
    QDict *response = qtest_qmp(
        qts, "{'execute':'qom-get','arguments':"
        "{'path':%s,'property':%s}}",
        IA64_ZX6000_ZX1_TEST_FIXTURE_QOM_PATH, property);
    uint64_t value;

    g_assert_true(qdict_haskey(response, "return"));
    value = qnum_get_uint(qobject_to(QNum, qdict_get(response, "return")));
    qobject_unref(response);
    return value;
}

static uint64_t zx1_test_delivery_count(QTestState *qts, unsigned int root)
{
    g_assert_cmpuint(root, <, ZX1_TEST_ROOT_COUNT);
    return zx1_test_qom_get_uint(qts, zx1_test_delivery_count_property[root]);
}

static void zx1_test_sapic_select(QTestState *qts, unsigned int root,
                            uint32_t reg)
{
    qtest_writel(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_IOREGSEL, reg);
}

static uint32_t zx1_test_sapic_read(QTestState *qts, unsigned int root,
                              uint32_t reg)
{
    zx1_test_sapic_select(qts, root, reg);
    return qtest_readl(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_IOWIN);
}

static void zx1_test_sapic_write(QTestState *qts, unsigned int root,
                           uint32_t reg, uint32_t value)
{
    zx1_test_sapic_select(qts, root, reg);
    qtest_writel(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_IOWIN, value);
}

static uint32_t zx1_test_rte_low(unsigned int input)
{
    return ZX1_TEST_SAPIC_RTE_BASE + input * 2;
}

static void zx1_test_program_level_rte(QTestState *qts, unsigned int root,
                                 unsigned int input, uint8_t vector)
{
    zx1_test_sapic_write(qts, root, zx1_test_rte_low(input) + 1, 0);
    zx1_test_sapic_write(qts, root, zx1_test_rte_low(input),
                   vector | ZX1_TEST_SAPIC_RTE_TRIGGER);
}

static void zx1_test_sapic_eoi(QTestState *qts, unsigned int root,
                         uint8_t vector)
{
    qtest_writel(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_IOEOI, vector);
}

static void zx1_test_set_intx(QTestState *qts, unsigned int root,
                        unsigned int probe, unsigned int pin, bool level)
{
    unsigned int line;

    g_assert_cmpuint(root, <, ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(probe, <, ZX1_TEST_PROBES_PER_ROOT);
    g_assert_cmpuint(pin, <, PCI_NUM_PINS);
    line = ZX1_TEST_GPIO_LINE(root, probe, pin);
    qtest_set_irq_in(qts, IA64_ZX6000_ZX1_QTEST_QOM_PATH,
                     IA64_ZX6000_ZX1_TEST_GPIO_INTX, line, level);
}

static void zx1_test_program_msi(QTestState *qts, unsigned int root,
                           unsigned int probe, uint64_t address,
                           uint16_t data)
{
    uint16_t flags = zx1_test_config_readw(qts, root, 0, probe,
                                     ZX1_TEST_MSI_CAP_OFFSET + PCI_MSI_FLAGS);

    g_assert_cmphex(zx1_test_config_readb(qts, root, 0, probe,
                                   ZX1_TEST_MSI_CAP_OFFSET), ==,
                    PCI_CAP_ID_MSI);
    g_assert_cmphex(flags & PCI_MSI_FLAGS_64BIT, ==,
                    PCI_MSI_FLAGS_64BIT);
    zx1_test_config_writel(qts, root, 0, probe,
                     ZX1_TEST_MSI_CAP_OFFSET + PCI_MSI_ADDRESS_LO,
                     address);
    zx1_test_config_writel(qts, root, 0, probe,
                     ZX1_TEST_MSI_CAP_OFFSET + PCI_MSI_ADDRESS_HI,
                     address >> 32);
    zx1_test_config_writew(qts, root, 0, probe,
                     ZX1_TEST_MSI_CAP_OFFSET + PCI_MSI_DATA_64, data);
    zx1_test_config_writew(qts, root, 0, probe,
                     ZX1_TEST_MSI_CAP_OFFSET + PCI_MSI_FLAGS,
                     flags | PCI_MSI_FLAGS_ENABLE);
}

static void zx1_test_trigger_msi(QTestState *qts, unsigned int root,
                           unsigned int probe)
{
    unsigned int line;

    g_assert_cmpuint(root, <, ZX1_TEST_ROOT_COUNT);
    g_assert_cmpuint(probe, <, ZX1_TEST_PROBES_PER_ROOT);
    line = IA64_ZX6000_ZX1_TEST_MSI_GPIO_LINE(root, probe);
    qtest_set_irq_in(qts, IA64_ZX6000_ZX1_QTEST_QOM_PATH,
                     IA64_ZX6000_ZX1_TEST_GPIO_MSI, line, true);
    qtest_set_irq_in(qts, IA64_ZX6000_ZX1_QTEST_QOM_PATH,
                     IA64_ZX6000_ZX1_TEST_GPIO_MSI, line, false);
}

static bool zx1_test_sapic_irr_has_vector(QTestState *qts, uint8_t vector)
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

static bool zx1_test_sapic_irr_wait_for_vector(QTestState *qts, uint8_t vector)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000; attempt++) {
        if (zx1_test_sapic_irr_has_vector(qts, vector)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static void test_csr_and_indirect_config(void)
{
    QTestState *qts = zx1_test_start(NULL);
    unsigned int root;

    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_MIO_BASE +
                               ZX1_TEST_MIO_F0_ID_OFFSET), ==,
                    UINT64_C(0x1229103c));
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_MIO_BASE +
                               ZX1_TEST_MIO_F1_ID_OFFSET), ==,
                    UINT64_C(0x122a103c));
    g_assert_cmphex(qtest_readq(
                        qts, ZX1_TEST_MIO_BASE +
                             ZX1_TEST_MIO_LMMIO_DIR_BASE0), ==,
                    UINT64_C(0x80000000));
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE)), ==,
                    ZX1_TEST_IOMMU_IBASE_RESET);

    for (root = 0; root < ZX1_TEST_ROOT_COUNT; root++) {
        uint64_t expected_bus = (uint64_t)zx1_test_last_bus[root] << 8 |
                                zx1_test_first_bus[root];

        g_assert_cmphex(qtest_readq(
                            qts, zx1_test_ioa_base[root] +
                                 ZX1_TEST_IOA_FUNCTION_ID), ==,
                        UINT64_C(0x02b00000122e103c));
        g_assert_cmphex(qtest_readq(
                            qts, zx1_test_ioa_base[root] +
                                 ZX1_TEST_IOA_BUS_NUMBER), ==,
                        expected_bus);

        /* Mercury bus zero is the only selector for this root device. */
        g_assert_cmphex(zx1_test_config_readw(qts, root, 0, 0,
                                       PCI_VENDOR_ID), ==,
                        IOMMU_TESTDEV_VENDOR_ID);
        g_assert_cmphex(zx1_test_config_readb(qts, root, 0, 0,
                                       PCI_VENDOR_ID + 1), ==,
                        IOMMU_TESTDEV_VENDOR_ID >> 8);
        g_assert_cmphex(zx1_test_config_readw(qts, root, 0, 0,
                                       PCI_DEVICE_ID), ==,
                        IOMMU_TESTDEV_DEVICE_ID);

        /* A type-1 selector naming first_bus must not alias bus-zero type 0. */
        g_assert_cmphex(zx1_test_config_readl(qts, root,
                                             zx1_test_first_bus[root], 0,
                                             PCI_VENDOR_ID), ==, UINT32_MAX);
    }
    qtest_quit(qts);
}

static void test_shared_iommu_pcom_and_invalid_pte(void)
{
    const uint64_t iova = zx1_test_page_iova(0);
    const uint64_t old_target = zx1_test_page_target(0);
    const uint64_t new_target = zx1_test_page_target(1);
    const uint64_t guard_target = zx1_test_page_target(2);
    QTestState *qts = zx1_test_start(NULL);

    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_configure_probe(qts, 1, 0);
    zx1_test_write_pte(qts, 0, old_target, true);
    zx1_test_enable_iommu(qts);

    zx1_test_expect_dma_success(qts, 0, 0, iova, old_target);

    /* Both roots share the IOC cache, so this PTE update remains stale. */
    zx1_test_write_pte(qts, 0, new_target, true);
    zx1_test_expect_dma_success(qts, 1, 0, iova, old_target);

    /* A high-dword-only PCOM write updates the latch, but is not a command. */
    qtest_writel(qts,
                 zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_PCOM) + 4,
                 UINT32_C(0xa5a55a5a));
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_PCOM)), ==,
                    UINT64_C(0xa5a55a5a00000000));
    zx1_test_expect_dma_success(qts, 0, 0, iova, old_target);

    /* A complete PCOM page command refreshes the shared mapping. */
    zx1_test_purge_page(qts, iova);
    zx1_test_expect_dma_success(qts, 1, 0, iova, new_target);

    /*
     * Use a mapped guard IOVA so an invalid PTE cannot resolve through
     * identity translation.
     */
    qtest_writeq(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IMASK),
                 ZX1_TEST_INVALID_GUARD_IMASK);
    qtest_writeq(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE),
                 ZX1_TEST_INVALID_GUARD_IOVA | 1);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IMASK)), ==,
                    ZX1_TEST_INVALID_GUARD_IMASK);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE)), ==,
                    ZX1_TEST_INVALID_GUARD_IOVA | 1);

    zx1_test_write_pte(qts, 0, guard_target, true);
    zx1_test_expect_dma_success(qts, 0, 0, ZX1_TEST_INVALID_GUARD_IOVA,
                          guard_target);
    zx1_test_write_pte(qts, 0, guard_target, false);
    zx1_test_purge_page(qts, ZX1_TEST_INVALID_GUARD_IOVA);
    zx1_test_expect_dma_blocked_without_fallback(
        qts, 0, 0, ZX1_TEST_INVALID_GUARD_IOVA, guard_target);
    qtest_quit(qts);
}

static void test_shared_iotlb_eviction_notifier(void)
{
    QTestState *qts = zx1_test_start(NULL);
    unsigned int page;

    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_configure_probe(qts, 1, 0);
    for (page = 0; page <= 16; page++) {
        zx1_test_write_pte(qts, page, zx1_test_page_target(page), true);
    }
    zx1_test_enable_iommu(qts);

    /* Alternate roots while filling all sixteen shared frontend slots. */
    for (page = 0; page < 16; page++) {
        zx1_test_expect_dma_success(qts, page & 1, 0, zx1_test_page_iova(page),
                              zx1_test_page_target(page));
    }

    /* Updating the PTE alone leaves all sixteen cached entries intact. */
    zx1_test_write_pte(qts, 0, zx1_test_page_target(17), true);
    zx1_test_expect_dma_success(qts, 0, 0, zx1_test_page_iova(0),
                          zx1_test_page_target(0));

    /* Page 16 evicts slot zero; its notifier must discard page zero. */
    zx1_test_expect_dma_success(qts, 0, 0, zx1_test_page_iova(16),
                          zx1_test_page_target(16));
    zx1_test_expect_dma_success(qts, 0, 0, zx1_test_page_iova(0),
                          zx1_test_page_target(17));
    qtest_quit(qts);
}

static void test_intx_aggregation_eoi_and_function_reset(void)
{
    const unsigned int root = 0;
    const unsigned int input = 0;
    const uint8_t vector = 0x60;
    const uint32_t expected_data = ZX1_TEST_SAPIC_MESSAGE_ASSERT |
                                   ZX1_TEST_SAPIC_MESSAGE_TRIGGER | vector;
    QTestState *qts = zx1_test_start(NULL);
    unsigned int probe;

    zx1_test_configure_probe(qts, root, 0);
    zx1_test_configure_probe(qts, root, 1);
    zx1_test_program_level_rte(qts, root, input, vector);
    g_assert_false(zx1_test_sapic_irr_has_vector(qts, vector));

    zx1_test_set_intx(qts, root, 0, input, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 1);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_address_property[root]), ==,
                    UINT64_C(0xfee00000));
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_data_property[root]), ==,
                    expected_data);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_result_property[root]), ==,
                    MEMTX_OK);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, vector));

    /* Two PCI sources aggregate on one Mercury input without a new edge. */
    zx1_test_set_intx(qts, root, 1, input, true);
    zx1_test_set_intx(qts, root, 0, input, false);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 1);

    /* The remaining asserted source causes level redelivery on EOI. */
    zx1_test_sapic_eoi(qts, root, vector);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 2);
    zx1_test_set_intx(qts, root, 1, input, false);
    zx1_test_sapic_eoi(qts, root, vector);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 2);

    /* RF resets root devices and masks each RTE, preserving other fields. */
    zx1_test_set_intx(qts, root, 0, input, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 3);
    qtest_writeb(qts, zx1_test_ioa_base[root] + ZX1_TEST_IOA_STATUS_CONTROL,
                 ZX1_TEST_IOA_SIC_RESET_FUNCTION);
    for (probe = 0; probe < ZX1_TEST_PROBES_PER_ROOT; probe++) {
        g_assert_cmphex(zx1_test_config_readl(qts, root, 0, probe,
                                       PCI_BASE_ADDRESS_0), ==, 0);
        g_assert_cmphex(zx1_test_config_readw(qts, root, 0, probe,
                                       PCI_COMMAND), ==, 0);
    }
    g_assert_cmphex(zx1_test_sapic_read(qts, root,
                                       zx1_test_rte_low(input)), ==,
                    vector | ZX1_TEST_SAPIC_RTE_TRIGGER |
                    ZX1_TEST_SAPIC_RTE_MASK);

    /* Unmasking after RF does not deliver a stale subordinate IRQ. */
    zx1_test_sapic_write(qts, root, zx1_test_rte_low(input),
                   vector | ZX1_TEST_SAPIC_RTE_TRIGGER);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 3);
    zx1_test_set_intx(qts, root, 0, input, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 4);
    zx1_test_set_intx(qts, root, 0, input, false);
    qtest_quit(qts);
}

static void test_msi_root_ranges(void)
{
    const uint8_t root0_vector = 0x74;
    const uint8_t root1_vector = 0x75;
    const uint8_t blocked_vector = 0x76;
    QTestState *qts = zx1_test_start(NULL);

    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_configure_probe(qts, 1, 0);

    /* Put the shared IOC below the MSI overlay on an invalid-PTE aperture. */
    qtest_writeq(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IMASK),
                 ZX1_TEST_INVALID_GUARD_IMASK);
    qtest_writeq(qts, zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE),
                 ZX1_TEST_MSI_ADDRESS | 1);
    zx1_test_write_pte(qts, 0, zx1_test_page_target(0), false);

    zx1_test_program_msi(qts, 0, 0, ZX1_TEST_MSI_ADDRESS, root0_vector);
    zx1_test_trigger_msi(qts, 0, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 1);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 1), ==, 0);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_address_property[0]), ==,
                    ZX1_TEST_MSI_ADDRESS);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_data_property[0]), ==,
                    root0_vector);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_result_property[0]), ==,
                    MEMTX_OK);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, root0_vector));

    /* The same DMA address is decoded by the IOA belonging to root one. */
    zx1_test_program_msi(qts, 1, 0, ZX1_TEST_MSI_ADDRESS, root1_vector);
    zx1_test_trigger_msi(qts, 1, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 1);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 1), ==, 1);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, root1_vector));

    /* Disabling root zero exposes the invalid shared-IOMMU mapping below. */
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + ZX1_TEST_IOA_MSI_BASE, 0);
    zx1_test_program_msi(qts, 0, 0, ZX1_TEST_MSI_ADDRESS, blocked_vector);
    zx1_test_trigger_msi(qts, 0, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 1);
    g_assert_false(zx1_test_sapic_irr_has_vector(qts, blocked_vector));

    /* A legal 64 KiB reprogramming is active without affecting root one. */
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + ZX1_TEST_IOA_MSI_BASE,
                 ZX1_TEST_MSI_BASE_RESET);
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + ZX1_TEST_IOA_MSI_MASK,
                 ZX1_TEST_MSI_MASK_64K);
    zx1_test_program_msi(qts, 0, 0, ZX1_TEST_MSI_ADDRESS, 0x78);
    zx1_test_trigger_msi(qts, 0, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, 0x78));

    zx1_test_program_msi(qts, 0, 0, ZX1_TEST_MSI_ADDRESS + UINT64_C(0x10000),
                   0x79);
    zx1_test_trigger_msi(qts, 0, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);

    /* System reset restores the default window; PCI MSI is reprogrammed. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                               ZX1_TEST_IOA_MSI_BASE), ==,
                    ZX1_TEST_MSI_BASE_RESET);
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                               ZX1_TEST_IOA_MSI_MASK), ==,
                    ZX1_TEST_MSI_MASK_RESET);
    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_program_msi(qts, 0, 0, ZX1_TEST_MSI_ADDRESS, 0x7a);
    zx1_test_trigger_msi(qts, 0, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 1);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, 0x7a));
    qtest_quit(qts);
}

static void test_system_reset_baseline(void)
{
    const uint8_t vector = 0x62;
    QTestState *qts = zx1_test_start(NULL);
    unsigned int root;

    for (root = 0; root < ZX1_TEST_ROOT_COUNT; root++) {
        zx1_test_configure_probe(qts, root, 0);
        zx1_test_program_level_rte(qts, root, 0, vector + root);
        zx1_test_set_intx(qts, root, 0, 0, true);
    }
    qtest_writeq(qts, ZX1_TEST_MIO_BASE + ZX1_TEST_MIO_ROPE_CONFIG,
                 UINT64_C(0x1234));
    zx1_test_write_pte(qts, 0, zx1_test_page_target(0), true);
    zx1_test_enable_iommu(qts);
    zx1_test_expect_dma_success(qts, 0, 0, zx1_test_page_iova(0),
                          zx1_test_page_target(0));

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_MIO_BASE +
                               ZX1_TEST_MIO_F0_ID_OFFSET), ==,
                    UINT64_C(0x1229103c));
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_MIO_BASE +
                               ZX1_TEST_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0x00ff));
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IBASE)), ==,
                    ZX1_TEST_IOMMU_IBASE_RESET);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_IMASK)), ==,
                    ZX1_TEST_IOMMU_IMASK_RESET);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_PCOM)), ==,
                    ZX1_TEST_IOMMU_PCOM_RESET);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(ZX1_TEST_IOC_IOMMU_TCNFG)), ==,
                    ZX1_TEST_IOMMU_TCNFG_RESET);
    g_assert_cmphex(qtest_readq(
                        qts,
                        zx1_test_iommu_register(
                            ZX1_TEST_IOC_IOMMU_PDIR_BASE)), ==,
                    ZX1_TEST_PDIR_BASE);

    for (root = 0; root < ZX1_TEST_ROOT_COUNT; root++) {
        uint64_t expected_bus = (uint64_t)zx1_test_last_bus[root] << 8 |
                                zx1_test_first_bus[root];

        g_assert_cmphex(qtest_readq(
                            qts, zx1_test_ioa_base[root] +
                                 ZX1_TEST_IOA_BUS_NUMBER), ==,
                        expected_bus);
        g_assert_cmphex(qtest_readq(
                            qts, zx1_test_ioa_base[root] +
                                 ZX1_TEST_IOA_CONFIG_ADDRESS), ==, 0);
        g_assert_cmphex(zx1_test_sapic_read(qts, root, zx1_test_rte_low(0)), ==,
                        ZX1_TEST_SAPIC_RTE_MASK);
        g_assert_cmphex(zx1_test_config_readl(qts, root, 0, 0,
                                       PCI_BASE_ADDRESS_0), ==, 0);
        g_assert_cmphex(zx1_test_config_readw(qts, root, 0, 0,
                                       PCI_COMMAND), ==, 0);
        g_assert_cmpuint(zx1_test_delivery_count(qts, root), ==, 0);
        g_assert_cmphex(zx1_test_qom_get_uint(
                            qts, zx1_test_last_address_property[root]), ==, 0);
        g_assert_cmphex(zx1_test_qom_get_uint(
                            qts, zx1_test_last_data_property[root]), ==, 0);
        g_assert_cmphex(zx1_test_qom_get_uint(
                            qts, zx1_test_last_result_property[root]), ==,
                        UINT32_MAX);
    }

    /* Reset must also clear the shared cache and every asserted PCI input. */
    zx1_test_write_pte(qts, 0, zx1_test_page_target(1), true);
    zx1_test_enable_iommu(qts);
    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_expect_dma_success(qts, 0, 0, zx1_test_page_iova(0),
                          zx1_test_page_target(1));
    zx1_test_program_level_rte(qts, 0, 0, vector);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 0);
    zx1_test_set_intx(qts, 0, 0, 0, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 1);
    zx1_test_set_intx(qts, 0, 0, 0, false);
    qtest_quit(qts);
}

static void test_savevm_state(void)
{
    const uint64_t iova = zx1_test_page_iova(0);
    const uint64_t old_target = zx1_test_page_target(0);
    const uint64_t new_target = zx1_test_page_target(1);
    const uint64_t saved_rope = UINT64_C(0x21);
    const uint8_t vector0 = 0x68;
    const uint8_t vector1 = 0x69;
    const uint32_t saved_selector = zx1_test_rte_low(1) + 1;
    uint64_t error_control;
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for zx1 test internal "
                    "snapshot testing");
        return;
    }

    tmpdir = g_dir_make_tmp("ia64-zx6000-zx1-test-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);
    qts = zx1_test_start(args);

    zx1_test_configure_probe(qts, 0, 0);
    zx1_test_configure_probe(qts, 0, 1);
    zx1_test_configure_probe(qts, 1, 0);
    qtest_writeq(qts, ZX1_TEST_IOA1_BASE + ZX1_TEST_IOA_MSI_MASK,
                 ZX1_TEST_MSI_MASK_64K);
    zx1_test_program_msi(qts, 1, 0, ZX1_TEST_MSI_ADDRESS, 0x6a);
    zx1_test_write_pte(qts, 0, old_target, true);
    zx1_test_enable_iommu(qts);
    zx1_test_expect_dma_success(qts, 0, 0, iova, old_target);

    /* Save a PTE/cache disagreement to require IOTLB migration. */
    zx1_test_write_pte(qts, 0, new_target, true);
    qtest_writeq(qts, ZX1_TEST_MIO_BASE + ZX1_TEST_MIO_ROPE_CONFIG, saved_rope);
    zx1_test_program_level_rte(qts, 0, 0, vector0);
    zx1_test_program_level_rte(qts, 0, 1, vector1);
    zx1_test_set_intx(qts, 0, 0, 0, true);
    zx1_test_set_intx(qts, 0, 1, 1, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);
    zx1_test_sapic_select(qts, 0, saved_selector);

    /* Preserve the hardware error log and an armed CE across save/load. */
    error_control = qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                               HP_ZX1_IOA_STATUS_CONTROL) &
        (HP_ZX1_IOA_SIC_FORWARD_VGA | HP_ZX1_IOA_SIC_HARD_FAIL);
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                 error_control | HP_ZX1_IOA_SIC_CLEAR_ENABLE);
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                 error_control | HP_ZX1_IOA_SIC_CLEAR_LOG);
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_CONFIG_ADDRESS, 0x7800);
    g_assert_cmphex(qtest_readl(qts, ZX1_TEST_IOA0_BASE +
                                HP_ZX1_IOA_CONFIG_DATA), ==, UINT32_MAX);
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                 error_control | HP_ZX1_IOA_SIC_CLEAR_ENABLE);

    response = qtest_hmp(qts, "savevm zx1-test-state");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    /* Change every migrated state category before loading. */
    qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                 error_control | HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                HP_ZX1_IOA_ERROR_STATUS), ==, 0);
    zx1_test_purge_page(qts, iova);
    zx1_test_expect_dma_success(qts, 0, 0, iova, new_target);
    qtest_writeq(qts, ZX1_TEST_MIO_BASE + ZX1_TEST_MIO_ROPE_CONFIG, 0);
    zx1_test_sapic_select(qts, 0, 0);
    zx1_test_set_intx(qts, 0, 0, 0, false);
    zx1_test_set_intx(qts, 0, 1, 1, false);
    zx1_test_sapic_eoi(qts, 0, vector0);
    zx1_test_sapic_eoi(qts, 0, vector1);
    zx1_test_config_writel(qts, 0, 0, 0, PCI_BASE_ADDRESS_0,
                     UINT32_C(0x00300000));
    zx1_test_config_writew(qts, 0, 0, 0, PCI_COMMAND, 0);
    qtest_writeq(qts, ZX1_TEST_IOA1_BASE + ZX1_TEST_IOA_MSI_BASE, 0);
    zx1_test_set_intx(qts, 0, 0, 0, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 3);

    response = qtest_hmp(qts, "loadvm zx1-test-state");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    /* Post-load restores state but never replays an interrupt delivery. */
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                HP_ZX1_IOA_ERROR_STATUS), ==, 0x40c);
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                    UINT64_C(0x4000000080000000));
    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                HP_ZX1_IOA_STATUS_CONTROL) &
                    HP_ZX1_IOA_SIC_CLEAR_ENABLE, ==,
                    HP_ZX1_IOA_SIC_CLEAR_ENABLE);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_address_property[0]), ==,
                    UINT64_C(0xfee00000));
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_data_property[0]), ==,
                    ZX1_TEST_SAPIC_MESSAGE_ASSERT |
                    ZX1_TEST_SAPIC_MESSAGE_TRIGGER | vector1);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_result_property[0]), ==, MEMTX_OK);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 1), ==, 0);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_address_property[1]), ==, 0);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_data_property[1]), ==, 0);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_result_property[1]), ==, UINT32_MAX);

    /* Root-one MSI configuration and IOA mapping return without replay. */
    zx1_test_trigger_msi(qts, 1, 0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 1), ==, 1);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_address_property[1]), ==,
                    ZX1_TEST_MSI_ADDRESS);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_data_property[1]), ==, 0x6a);
    g_assert_cmphex(zx1_test_qom_get_uint(
                        qts, zx1_test_last_result_property[1]), ==, MEMTX_OK);
    g_assert_true(zx1_test_sapic_irr_wait_for_vector(qts, 0x6a));

    /* Restored PCI irq_state lets deassertion reach the IOA before EOI. */
    zx1_test_set_intx(qts, 0, 0, 0, false);
    zx1_test_sapic_eoi(qts, 0, vector0);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);

    g_assert_cmphex(qtest_readq(qts, ZX1_TEST_MIO_BASE +
                               ZX1_TEST_MIO_ROPE_CONFIG), ==, saved_rope);
    g_assert_cmphex(qtest_readl(qts, zx1_test_ioa_base[0] +
                               ZX1_TEST_IOA_IOREGSEL), ==, saved_selector);
    g_assert_cmphex(zx1_test_config_readl(qts, 0, 0, 0,
                                   PCI_BASE_ADDRESS_0), ==,
                    ZX1_TEST_BAR0_OFFSET);
    g_assert_cmphex(zx1_test_config_readw(qts, 0, 0, 0, PCI_COMMAND), ==,
                    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* The migrated cache must remain stale relative to the restored PTE. */
    zx1_test_expect_dma_success(qts, 0, 0, iova, old_target);

    /* Restored asserted+in-service state suppresses edges until an EOI. */
    zx1_test_set_intx(qts, 0, 1, 1, false);
    zx1_test_set_intx(qts, 0, 1, 1, true);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 2);
    zx1_test_sapic_eoi(qts, 0, vector1);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 3);

    zx1_test_set_intx(qts, 0, 1, 1, false);
    zx1_test_sapic_eoi(qts, 0, vector1);
    g_assert_cmpuint(zx1_test_delivery_count(qts, 0), ==, 3);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_zx6000_chipset_fault_records(void)
{
    const uint64_t ras_bank = IA64_RAS_HUB_DEFAULT_BASE +
        ia64_ras_record_bank_offset(0, IA64_RAS_RECORD_TYPE_MCA);
    const uint64_t invalid_mio_offset = UINT64_C(0xfff8);
    const uint32_t absent_selectors[] = {
        (uint32_t)PCI_DEVFN(0, 0) << 8,
        (uint32_t)PCI_DEVFN(1, 3) << 8,
        (uint32_t)PCI_DEVFN(PCI_SLOT_MAX - 1, 0) << 8,
    };
    const uint32_t bus_addresses[] = { 0x10000, 0x20300, 0 };
    unsigned int i;
    QTestState *qts = qtest_init(
        "-machine hp-zx6000,nvram=none,firmware=none "
        "-m 1G -smp 1 -S -display none -serial none -monitor none "
        "-net none");

    while (qtest_readq(qts, ras_bank + IA64_RAS_RECORD_REG_LENGTH)) {
        qtest_writeq(qts, ras_bank + IA64_RAS_RECORD_REG_CLEAR,
                     IA64_RAS_RECORD_CLEAR_VALUE);
    }
    g_assert_cmpuint(qtest_readq(qts, ras_bank +
                                IA64_RAS_RECORD_REG_LENGTH), ==, 0);
    qtest_writeq(qts, HP_ZX6000_MIO_BASE + HP_ZX1_MIO_ERROR_CONFIG,
                 HP_ZX1_MIO_ERROR_CONFIG_NOTIFY);
    qtest_readq(qts, HP_ZX6000_MIO_BASE + invalid_mio_offset);
    g_assert_cmphex(qtest_readq(qts, HP_ZX6000_MIO_BASE +
                                HP_ZX1_MIO_ERROR_STATUS), ==,
                    HP_ZX1_MIO_ERROR_VALID |
                    HP_ZX1_MIO_ERROR_CSR_DECODE);
    g_assert_cmphex(qtest_readq(qts, HP_ZX6000_MIO_BASE +
                                HP_ZX1_MIO_ERROR_ADDRESS), ==,
                    invalid_mio_offset);
    g_assert_cmphex(qtest_readq(qts, ras_bank +
                                IA64_RAS_RECORD_REG_STATUS) &
                    IA64_RAS_RECORD_STATUS_PRESENT, ==,
                    IA64_RAS_RECORD_STATUS_PRESENT);

    while (qtest_readq(qts, ras_bank + IA64_RAS_RECORD_REG_LENGTH)) {
        qtest_writeq(qts, ras_bank + IA64_RAS_RECORD_REG_CLEAR,
                     IA64_RAS_RECORD_CLEAR_VALUE);
    }
    qtest_writeq(qts, HP_ZX6000_MIO_BASE + HP_ZX1_MIO_ERROR_STATUS,
                 HP_ZX1_MIO_ERROR_STATUS_W1C);
    g_assert_cmpuint(qtest_readq(qts, ras_bank +
                                IA64_RAS_RECORD_REG_LENGTH), ==, 0);

    /* PCI enumeration may probe empty slots/functions without raising MCA. */
    for (i = 0; i < G_N_ELEMENTS(absent_selectors); i++) {
        qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                     HP_ZX1_IOA_SIC_CLEAR_ENABLE);
        qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_STATUS_CONTROL,
                     HP_ZX1_IOA_SIC_CLEAR_LOG);
        qtest_writeq(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_CONFIG_ADDRESS,
                     absent_selectors[i]);
        g_assert_cmphex(qtest_readl(qts, ZX1_TEST_IOA0_BASE +
                                    HP_ZX1_IOA_CONFIG_DATA), ==, UINT32_MAX);
        g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                    HP_ZX1_IOA_ERROR_STATUS), ==,
                        0x40c);
        g_assert_cmphex(qtest_readq(qts, ZX1_TEST_IOA0_BASE +
                                    HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                        UINT64_C(0x4000000000000000) | bus_addresses[i]);
        g_assert_cmpuint(qtest_readq(qts, ras_bank +
                                     IA64_RAS_RECORD_REG_LENGTH), ==, 0);

        qtest_writel(qts, ZX1_TEST_IOA0_BASE + HP_ZX1_IOA_CONFIG_DATA,
                     UINT32_MAX);
        g_assert_cmpuint(qtest_readq(qts, ras_bank +
                                     IA64_RAS_RECORD_REG_LENGTH), ==, 0);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ia64-zx6000-zx1-test/csr-indirect-config",
                   test_csr_and_indirect_config);
    qtest_add_func("/ia64-zx6000-zx1-test/shared-iommu-pcom-invalid-pte",
                   test_shared_iommu_pcom_and_invalid_pte);
    qtest_add_func("/ia64-zx6000-zx1-test/shared-iotlb-eviction",
                   test_shared_iotlb_eviction_notifier);
    qtest_add_func("/ia64-zx6000-zx1-test/intx-eoi-function-reset",
                   test_intx_aggregation_eoi_and_function_reset);
    qtest_add_func("/ia64-zx6000-zx1-test/msi-root-ranges",
                   test_msi_root_ranges);
    qtest_add_func("/ia64-zx6000-zx1-test/system-reset-baseline",
                   test_system_reset_baseline);
    qtest_add_func("/ia64-zx6000-zx1-test/savevm-state",
                   test_savevm_state);
    qtest_add_func("/ia64-zx6000-zx1-test/chipset-fault-records",
                   test_zx6000_chipset_fault_records);

    return g_test_run();
}
