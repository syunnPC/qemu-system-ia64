/*
 * Intel 460GX DMA PCI-path qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/memattrs.h"
#include "hw/ia64/intel_460gx_dma_test.h"
#include "hw/misc/iommu-testdev.h"
#include "libqos/generic-pcihost.h"
#include "libqos/pci.h"
#include "libqos/qos-iommu-testdev.h"
#include "libqtest.h"

#define DMA_TEST_LEN 4
#define DMA_TEST_SENTINEL UINT32_C(0xa5a5a5a5)

typedef struct Intel460GXDMATestFixture {
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;
} Intel460GXDMATestFixture;

static void dma_test_wait_status(Intel460GXDMATestFixture *f, uint32_t expected)
{
    uint32_t status = INTEL_460GX_DMA_TEST_STATUS_BUSY;

    for (unsigned i = 0; i < 1000; i++) {
        status = qtest_readl(f->qts, INTEL_460GX_DMA_TEST_CONTROL_BASE +
                                         INTEL_460GX_DMA_TEST_REG_STATUS);
        if (status != INTEL_460GX_DMA_TEST_STATUS_BUSY) {
            break;
        }
        g_usleep(1000);
    }

    g_assert_cmphex(status, ==, expected);
}

static void dma_test_command(Intel460GXDMATestFixture *f, uint32_t command,
                             uint32_t expected)
{
    qtest_writel(f->qts,
                 INTEL_460GX_DMA_TEST_CONTROL_BASE +
                     INTEL_460GX_DMA_TEST_REG_COMMAND,
                 command);
    dma_test_wait_status(f, expected);
}

static Intel460GXDMATestFixture *dma_test_start_on_bus(bool deny_all,
                                                       uint8_t first_bus)
{
    Intel460GXDMATestFixture *f = g_new0(Intel460GXDMATestFixture, 1);
    uint64_t bar_size;

    f->qts = qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 256M -nodefaults "
        "-display none -net none "
        "-device %s,id=dma-test-host,x-test-ecam-base=0x%" PRIx64
        ",x-test-mmio-base=0x%" PRIx64 ",x-test-control-base=0x%" PRIx64
        ",x-test-ram-base=0x%" PRIx64 ",x-test-iova-base=0x%" PRIx64
        ",x-test-first-bus=0x%x"
        ",x-test-deny-all=%s",
        TYPE_INTEL_460GX_DMA_TEST_HOST, INTEL_460GX_DMA_TEST_ECAM_BASE,
        INTEL_460GX_DMA_TEST_MMIO_BASE, INTEL_460GX_DMA_TEST_CONTROL_BASE,
        INTEL_460GX_DMA_TEST_RAM_BASE, INTEL_460GX_DMA_TEST_IOVA_BASE,
        first_bus, deny_all ? "on" : "off");

    qpci_init_generic(&f->gbus, f->qts, NULL, false);
    f->gbus.ecam_alloc_ptr = INTEL_460GX_DMA_TEST_ECAM_BASE;
    f->gbus.bus.mmio_alloc_ptr = INTEL_460GX_DMA_TEST_MMIO_BASE;
    f->gbus.bus.mmio_limit = INTEL_460GX_DMA_TEST_PCI_TARGET_BASE;

    f->dev = qpci_device_find(&f->gbus.bus,
                              QPCI_DEVFN(INTEL_460GX_DMA_TEST_PCI_SLOT, 0));
    g_assert_nonnull(f->dev);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_VENDOR_ID), ==,
                    IOMMU_TESTDEV_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_DEVICE_ID), ==,
                    IOMMU_TESTDEV_DEVICE_ID);

    qpci_device_enable(f->dev);
    f->bar = qpci_iomap(f->dev, 0, &bar_size);
    g_assert_false(f->bar.is_io);
    g_assert_cmphex(bar_size, ==, BAR0_SIZE);
    return f;
}

static Intel460GXDMATestFixture *dma_test_start(bool deny_all)
{
    return dma_test_start_on_bus(deny_all, 0);
}

static uint32_t dma_test_trigger(Intel460GXDMATestFixture *f, uint64_t iova)
{
    uint32_t attrs = ITD_ATTRS_SET_SPACE(0, ITD_ATTRS_SPACE_NONSECURE);

    return qos_iommu_testdev_trigger_dma(f->dev, f->bar, iova,
                                         INTEL_460GX_DMA_TEST_RAM_BASE,
                                         DMA_TEST_LEN, attrs);
}

static void dma_test_cleanup(Intel460GXDMATestFixture *f)
{
    dma_test_command(f, INTEL_460GX_DMA_TEST_CMD_CLEANUP,
                     INTEL_460GX_DMA_TEST_STATUS_CLEANUP_DONE);
}

static void dma_test_stop(Intel460GXDMATestFixture *f)
{
    g_free(f->dev);
    qtest_quit(f->qts);
    g_free(f);
}

static void test_mapped_real_pci_path(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(false);
    uint32_t result;

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE, DMA_TEST_SENTINEL);
    result = dma_test_trigger(f, INTEL_460GX_DMA_TEST_IOVA_BASE);

    g_assert_cmphex(result, ==, 0);
    g_assert_cmphex(qos_iommu_testdev_get_memtx_result(f->dev, f->bar), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE), ==,
                    ITD_DMA_WRITE_VAL);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

static void test_deny_all_has_no_system_memory_fallback(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(true);
    uint32_t result;

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE, DMA_TEST_SENTINEL);

    /* This system-memory IOVA is absent from the PCI DMA view. */
    result = dma_test_trigger(f, INTEL_460GX_DMA_TEST_RAM_BASE);

    g_assert_cmphex(result, ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qos_iommu_testdev_get_memtx_result(f->dev, f->bar), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readl(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE), ==,
                    DMA_TEST_SENTINEL);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

static void test_unmapped_alias_boundary_is_denied(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(false);
    uint32_t result;

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE, DMA_TEST_SENTINEL);

    /* The aperture continues here, but the approved RAM alias does not. */
    result = dma_test_trigger(f, INTEL_460GX_DMA_TEST_IOVA_BASE +
                                     INTEL_460GX_DMA_TEST_ALIAS_SIZE);

    g_assert_cmphex(result, ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qos_iommu_testdev_get_memtx_result(f->dev, f->bar), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readl(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE), ==,
                    DMA_TEST_SENTINEL);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

static void test_pci_window_target(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(false);
    uint32_t attrs = ITD_ATTRS_SET_SPACE(0, ITD_ATTRS_SPACE_NONSECURE);
    uint32_t result;

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_PCI_TARGET_BASE,
                 DMA_TEST_SENTINEL);
    result = qos_iommu_testdev_trigger_dma(
        f->dev, f->bar, INTEL_460GX_DMA_TEST_PCI_TARGET_BASE,
        INTEL_460GX_DMA_TEST_PCI_TARGET_BASE, DMA_TEST_LEN, attrs);

    g_assert_cmphex(qtest_readl(f->qts, INTEL_460GX_DMA_TEST_PCI_TARGET_BASE),
                    ==, ITD_DMA_WRITE_VAL);
    g_assert_cmphex(result, ==, 0);
    g_assert_cmphex(qos_iommu_testdev_get_memtx_result(f->dev, f->bar), ==,
                    MEMTX_OK);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

static void test_active_destroy_is_atomic(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(false);

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE, DMA_TEST_SENTINEL);
    g_assert_cmphex(dma_test_trigger(f, INTEL_460GX_DMA_TEST_IOVA_BASE), ==, 0);

    dma_test_command(f, INTEL_460GX_DMA_TEST_CMD_TRY_ACTIVE_DESTROY,
                     INTEL_460GX_DMA_TEST_STATUS_ACTIVE_DESTROY_REFUSED);

    qtest_writel(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE, DMA_TEST_SENTINEL);
    g_assert_cmphex(dma_test_trigger(f, INTEL_460GX_DMA_TEST_IOVA_BASE), ==, 0);
    g_assert_cmphex(qos_iommu_testdev_get_memtx_result(f->dev, f->bar), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(f->qts, INTEL_460GX_DMA_TEST_RAM_BASE), ==,
                    ITD_DMA_WRITE_VAL);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

static void test_rcu_teardown(void)
{
    Intel460GXDMATestFixture *f = dma_test_start(false);
    uint64_t config_addr;

    g_assert_cmphex(dma_test_trigger(f, INTEL_460GX_DMA_TEST_IOVA_BASE), ==, 0);
    dma_test_cleanup(f);

    config_addr = INTEL_460GX_DMA_TEST_ECAM_BASE +
                  (QPCI_DEVFN(INTEL_460GX_DMA_TEST_PCI_SLOT, 0) << 12);
    g_assert_cmphex(qtest_readw(f->qts, config_addr + PCI_VENDOR_ID), ==,
                    UINT16_MAX);
    dma_test_stop(f);
}

static void test_numbered_root(void)
{
    Intel460GXDMATestFixture *f = dma_test_start_on_bus(false, 0x20);

    /* The relative ECAM window requires root bus 0x20 for device discovery. */
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_VENDOR_ID), ==,
                    IOMMU_TESTDEV_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_DEVICE_ID), ==,
                    IOMMU_TESTDEV_DEVICE_ID);

    dma_test_cleanup(f);
    dma_test_stop(f);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/intel-460gx-dma/pci/mapped", test_mapped_real_pci_path);
    qtest_add_func("/intel-460gx-dma/pci/deny-no-fallback",
                   test_deny_all_has_no_system_memory_fallback);
    qtest_add_func("/intel-460gx-dma/pci/unmapped-boundary",
                   test_unmapped_alias_boundary_is_denied);
    qtest_add_func("/intel-460gx-dma/pci/pci-window-target",
                   test_pci_window_target);
    qtest_add_func("/intel-460gx-dma/pci/active-destroy-atomic",
                   test_active_destroy_is_atomic);
    qtest_add_func("/intel-460gx-dma/pci/rcu-teardown", test_rcu_teardown);
    qtest_add_func("/intel-460gx-dma/pci/numbered-root", test_numbered_root);

    return g_test_run();
}
