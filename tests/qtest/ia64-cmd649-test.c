/*
 * CMD PCI-649 IDE controller tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <glib/gstdio.h>

#include "hw/ia64/ia64_pci.h"
#include "hw/ide/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "libqos/generic-pcihost.h"
#include "libqos/pci.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define CMD649_SLOT                 5
#define CMD649_QOM_PATH             "/machine/peripheral/cmd649"
#define CMD649_CFR                  0x50
#define CMD649_CNTRL                0x51
#define CMD649_ARTTIM0              0x53
#define CMD649_ARTTIM1              0x55
#define CMD649_ARTTIM23             0x57
#define CMD649_BRST                 0x59
#define CMD649_PM_CAP               0x60
#define CMD649_MRDMODE              0x71
#define CMD649_UDIDETCR0            0x73
#define CMD649_DTPR0                0x74
#define CMD649_BMIDECSR             0x79
#define CMD649_UDIDETCR1            0x7b
#define CMD649_DTPR1                0x7c

typedef struct CMD649Fixture {
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
} CMD649Fixture;

static uint64_t cmd649_sparse_io_address(uint32_t port)
{
    return IA64_PCI_IO_BASE + ((uint64_t)(port >> 2) << 12) +
        (port & 0xfff);
}

static uint8_t cmd649_pio_readb(QPCIBus *bus, uint32_t addr)
{
    return qtest_readb(bus->qts, cmd649_sparse_io_address(addr));
}

static uint32_t cmd649_pio_readl(QPCIBus *bus, uint32_t addr)
{
    return qtest_readl(bus->qts, cmd649_sparse_io_address(addr));
}

static void cmd649_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t value)
{
    qtest_writeb(bus->qts, cmd649_sparse_io_address(addr), value);
}

static void cmd649_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t value)
{
    qtest_writew(bus->qts, cmd649_sparse_io_address(addr), value);
}

static void cmd649_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t value)
{
    qtest_writel(bus->qts, cmd649_sparse_io_address(addr), value);
}

static CMD649Fixture *cmd649_start(const char *properties)
{
    CMD649Fixture *f = g_new0(CMD649Fixture, 1);

    f->qts = qtest_initf("-machine ia64-vpc,nvram=none "
                         "-m 256M -S -nodefaults "
                         "-display none "
                         "-device cmd649-ide,id=cmd649,addr=%d%s",
                         CMD649_SLOT, properties ?: "");
    qpci_init_generic(&f->gbus, f->qts, NULL, false);
    f->gbus.ecam_alloc_ptr = IA64_PCI_CONFIG_BASE;
    f->gbus.bus.pio_alloc_ptr = 0x1000;
    f->gbus.bus.pio_readb = cmd649_pio_readb;
    f->gbus.bus.pio_readl = cmd649_pio_readl;
    f->gbus.bus.pio_writeb = cmd649_pio_writeb;
    f->gbus.bus.pio_writew = cmd649_pio_writew;
    f->gbus.bus.pio_writel = cmd649_pio_writel;
    f->dev = qpci_device_find(&f->gbus.bus,
                              QPCI_DEVFN(CMD649_SLOT, 0));
    g_assert_nonnull(f->dev);
    return f;
}

static void cmd649_stop(CMD649Fixture *f)
{
    g_free(f->dev);
    qtest_quit(f->qts);
    g_free(f);
}

static void test_cmd649_config(void)
{
    CMD649Fixture *f = cmd649_start(NULL);

    g_assert_cmphex(qpci_config_readw(f->dev, PCI_VENDOR_ID), ==,
                    PCI_VENDOR_ID_CMD);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_DEVICE_ID), ==,
                    PCI_DEVICE_ID_CMD_649);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_REVISION_ID), ==, 0x02);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_CLASS_PROG), ==, 0x8f);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_STORAGE_IDE);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    PCI_VENDOR_ID_CMD);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_SUBSYSTEM_ID), ==,
                    PCI_DEVICE_ID_CMD_649);

    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CFR), ==, 0x40);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CNTRL), ==, 0xec);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_ARTTIM0), ==, 0x80);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_ARTTIM1), ==, 0xc0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_ARTTIM23), ==, 0x8c);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_BRST), ==, 0x40);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_UDIDETCR0), ==, 0xf0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_UDIDETCR1), ==, 0xf0);

    g_assert_cmphex(qpci_config_readb(f->dev, PCI_CAPABILITY_LIST), ==,
                    CMD649_PM_CAP);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_PM_CAP), ==,
                    PCI_CAP_ID_PM);
    g_assert_cmphex(qpci_config_readw(f->dev,
                                     CMD649_PM_CAP + PCI_PM_PMC), ==,
                    0x0622);
    g_assert_cmphex(qpci_config_readw(f->dev,
                                     CMD649_PM_CAP + PCI_PM_CTRL), ==,
                    0x6000);
    g_assert_cmphex(qpci_config_readb(f->dev,
                                     CMD649_PM_CAP + PCI_PM_DATA_REGISTER),
                    ==, 0xf0);

    qpci_config_writeb(f->dev, PCI_CLASS_PROG, 0x00);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_CLASS_PROG), ==, 0x8a);
    qpci_config_writeb(f->dev, PCI_CLASS_PROG, 0xff);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_CLASS_PROG), ==, 0x8f);

    qpci_config_writeb(f->dev, CMD649_CNTRL, 0x00);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CNTRL), ==, 0x20);
    qpci_config_writeb(f->dev, CMD649_CNTRL, 0xff);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CNTRL), ==, 0xec);
    qpci_config_writeb(f->dev, 0x5a, 0xff);
    g_assert_cmphex(qpci_config_readb(f->dev, 0x5a), ==, 0x00);

    qpci_config_writeb(f->dev, CMD649_BRST, 0xa5);
    qtest_system_reset(f->qts);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_BRST), ==, 0x40);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CNTRL), ==, 0xec);
    cmd649_stop(f);
}

static void test_cmd649_bmdma_aliases(void)
{
    CMD649Fixture *f = cmd649_start(",primary-cable80=on,"
                                    "secondary-cable80=on");
    uint64_t size;
    QPCIBar bar;

    bar = qpci_iomap(f->dev, 4, &size);
    g_assert_true(bar.is_io);
    g_assert_cmpuint(size, ==, 16);
    qpci_device_enable(f->dev);

    qpci_config_writeb(f->dev, CMD649_MRDMODE, 0x32);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE), ==, 0x30);
    g_assert_cmphex(qpci_io_readb(f->dev, bar, 1), ==, 0x30);
    qpci_io_writeb(f->dev, bar, 1, 0x10);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE), ==, 0x10);

    g_assert_cmphex(qpci_io_readb(f->dev, bar, 9), ==, 0x03);
    qpci_io_writeb(f->dev, bar, 9, 0xa0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_BMIDECSR), ==, 0xa3);

    qpci_config_writeb(f->dev, CMD649_UDIDETCR0, 0x25);
    g_assert_cmphex(qpci_io_readb(f->dev, bar, 3), ==, 0x25);
    qpci_io_writeb(f->dev, bar, 11, 0x6a);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_UDIDETCR1), ==, 0x6a);

    qpci_config_writel(f->dev, CMD649_DTPR0, 0x1234567f);
    g_assert_cmphex(qpci_config_readl(f->dev, CMD649_DTPR0), ==,
                    0x1234567c);
    g_assert_cmphex(qpci_io_readl(f->dev, bar, 4), ==, 0x1234567c);
    qpci_io_writel(f->dev, bar, 12, 0xaabbccdf);
    g_assert_cmphex(qpci_config_readl(f->dev, CMD649_DTPR1), ==,
                    0xaabbccdc);

    cmd649_stop(f);
}

static void test_cmd649_irq_and_modes(void)
{
    CMD649Fixture *f = cmd649_start(NULL);
    uint32_t native_bar0;
    uint32_t native_bar2;

    qpci_iomap(f->dev, 0, NULL);
    qpci_iomap(f->dev, 1, NULL);
    qpci_iomap(f->dev, 2, NULL);
    qpci_iomap(f->dev, 3, NULL);
    qpci_device_enable(f->dev);
    native_bar0 = qpci_config_readl(f->dev, PCI_BASE_ADDRESS_0);
    native_bar2 = qpci_config_readl(f->dev, PCI_BASE_ADDRESS_2);

    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 0, 1);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CFR) & 0x04, ==,
                    0x04);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x04, ==,
                    0x04);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);

    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 0, 0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CFR) & 0x04, ==, 0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x04, ==, 0);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, 0);

    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 0, 1);

    qpci_config_writeb(f->dev, CMD649_MRDMODE, 0x10);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE), ==, 0x14);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, 0);
    qpci_config_writeb(f->dev, CMD649_MRDMODE, 0x00);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
    qpci_config_writeb(f->dev, CMD649_CFR, 0x04);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x04, ==, 0);
    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 0, 0);

    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 1, 1);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_ARTTIM23) & 0x10, ==,
                    0x10);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x08, ==,
                    0x08);
    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 1, 0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_ARTTIM23) & 0x10, ==, 0);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x08, ==, 0);

    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 1, 1);
    qpci_config_writeb(f->dev, CMD649_ARTTIM23, 0x10);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_MRDMODE) & 0x08, ==, 0);
    qtest_set_irq_in(f->qts, CMD649_QOM_PATH, NULL, 1, 0);

    qpci_config_writeb(f->dev, PCI_CLASS_PROG, 0x8e);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_0), ==, 0);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_2), ==,
                    native_bar2);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_INTERRUPT_PIN), ==, 1);

    qpci_config_writeb(f->dev, PCI_CLASS_PROG, 0x8a);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_0), ==, 0);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_2), ==, 0);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_INTERRUPT_PIN), ==, 0);

    qpci_config_writeb(f->dev, PCI_CLASS_PROG, 0x8f);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_0), ==,
                    native_bar0);
    g_assert_cmphex(qpci_config_readl(f->dev, PCI_BASE_ADDRESS_2), ==,
                    native_bar2);
    g_assert_cmphex(qpci_config_readb(f->dev, PCI_INTERRUPT_PIN), ==, 1);
    cmd649_stop(f);
}

static void test_cmd649_primary_only(void)
{
    CMD649Fixture *f = cmd649_start(",secondary=0,primary-cable80=on");

    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_CNTRL), ==, 0xe4);
    g_assert_cmphex(qpci_config_readb(f->dev, CMD649_BMIDECSR), ==, 0x01);
    cmd649_stop(f);
}

static void cmd649_packet(CMD649Fixture *f, QPCIBar ide, uint8_t opcode,
                          bool dma)
{
    unsigned i;

    qpci_io_writeb(f->dev, ide, 6, 0xb0); /* Select the ATAPI slave. */
    qpci_io_writeb(f->dev, ide, 1, dma);
    qpci_io_writeb(f->dev, ide, 4, 8);
    qpci_io_writeb(f->dev, ide, 5, 0);
    qpci_io_writeb(f->dev, ide, 7, 0xa0); /* PACKET */
    g_assert_cmphex(qpci_io_readb(f->dev, ide, 7) & 0x88, ==, 0x08);
    for (i = 0; i < 6; i++) {
        qpci_io_writew(f->dev, ide, 0, i ? 0 : opcode);
    }
}

static void cmd649_dma_packet(CMD649Fixture *f, QPCIBar ide, uint8_t opcode)
{
    cmd649_packet(f, ide, opcode, true);
}

static void cmd649_wait_packet(CMD649Fixture *f, QPCIBar ide)
{
    unsigned i;

    for (i = 0; i < 100; i++) {
        qtest_clock_step(f->qts, 1000000);
        if (!(qpci_io_readb(f->dev, ide, 7) & 0x80)) {
            return;
        }
    }
    g_assert_not_reached();
}

/* Start before the CDB, during preparation, or after device completion. */
static void test_cmd649_atapi_completion(gconstpointer opaque)
{
    static const struct {
        uint8_t opcode;
        uint8_t error;
    } commands[] = {
        { 0xff, 0x50 }, /* Unsupported opcode: ILLEGAL REQUEST. */
        { 0x00, 0x20 }, /* TEST UNIT READY: no medium. */
        { 0x1e, 0x00 }, /* ALLOW MEDIUM REMOVAL: successful non-data command. */
    };
    unsigned start = GPOINTER_TO_UINT(opaque);
    CMD649Fixture *f = cmd649_start(" -device ide-cd,bus=cmd649.0,unit=1");
    QPCIBar ide = qpci_iomap(f->dev, 0, NULL);
    QPCIBar control = qpci_iomap(f->dev, 1, NULL);
    QPCIBar bm = qpci_iomap(f->dev, 4, NULL);
    uint8_t buffer[16], actual[16];
    uint32_t prdt[] = { cpu_to_le32(0x100100), cpu_to_le32(0x80000010) };
    unsigned i;

    qpci_device_enable(f->dev);
    memset(buffer, 0xa5, sizeof(buffer));
    qtest_qmp_assert_success(f->qts, "{'execute':'cont'}");
    qtest_memwrite(f->qts, 0x100000, prdt, sizeof(prdt));
    qtest_memwrite(f->qts, 0x100100, buffer, sizeof(buffer));
    qpci_io_writel(f->dev, bm, 4, 0x100000);

    for (i = 0; i < ARRAY_SIZE(commands); i++) {
        if (start == 0) {
            qpci_io_writeb(f->dev, bm, 0, BM_CMD_START | BM_CMD_READ);
        }
        cmd649_dma_packet(f, ide, commands[i].opcode);
        /* Prepare_B has BSY set, DRQ clear, and no completion interrupt. */
        g_assert_cmphex(qpci_io_readb(f->dev, control, 2) & 0x88, ==, 0x80);
        g_assert_cmphex(qpci_io_readb(f->dev, bm, 2) & BM_STATUS_INT, ==, 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);

        if (start == 1) {
            qpci_io_writeb(f->dev, bm, 2, BM_STATUS_INT | BM_STATUS_ERROR);
            qpci_io_writeb(f->dev, bm, 0, BM_CMD_START | BM_CMD_READ);
        }
        qtest_clock_step(f->qts, 100000000);
        g_assert_cmphex(qpci_io_readb(f->dev, control, 2) & 0x89, ==,
                       commands[i].error ? 1 : 0);
        g_assert_cmphex(qpci_io_readb(f->dev, ide, 1), ==, commands[i].error);
        g_assert_cmphex(qpci_io_readb(f->dev, ide, 2) & 7, ==, 3);
        g_assert_cmphex(qpci_io_readb(f->dev, bm, 2), ==,
                       BM_STATUS_INT | (start < 2 ? BM_STATUS_DMAING : 0));
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
        qtest_memread(f->qts, 0x100100, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), buffer, sizeof(buffer));

        /* Neither completion nor acknowledgement consumes a PRD. */
        qpci_io_readb(f->dev, ide, 7);
        qpci_io_writeb(f->dev, bm, 2, BM_STATUS_INT | BM_STATUS_ERROR);
        if (start == 2) {
            qpci_io_writeb(f->dev, bm, 0, BM_CMD_START | BM_CMD_READ);
        }
        g_assert_cmphex(qpci_io_readb(f->dev, bm, 2), ==, BM_STATUS_DMAING);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);
        qpci_io_writeb(f->dev, bm, 0, 0);
        g_assert_cmphex(qpci_io_readb(f->dev, bm, 2), ==, 0);
    }
    cmd649_stop(f);
}

static void test_cmd649_atapi_reset(void)
{
    CMD649Fixture *f = cmd649_start(" -device ide-cd,bus=cmd649.0,unit=1");
    QPCIBar ide = qpci_iomap(f->dev, 0, NULL);
    QPCIBar control = qpci_iomap(f->dev, 1, NULL);
    QPCIBar bm = qpci_iomap(f->dev, 4, NULL);
    unsigned i;

    qpci_device_enable(f->dev);
    qtest_qmp_assert_success(f->qts, "{'execute':'cont'}");
    for (i = 0; i < 2; i++) {
        cmd649_dma_packet(f, ide, 0xff);
        if (i == 0) {
            qpci_io_writeb(f->dev, ide, 7, 0x08); /* DEVICE RESET */
        } else {
            qpci_io_writeb(f->dev, control, 2, 4); /* SRST */
            qpci_io_writeb(f->dev, control, 2, 0);
        }
        /* Acknowledge reset diagnostics before checking for a stale timer. */
        qpci_io_readb(f->dev, ide, 7);
        qpci_io_writeb(f->dev, bm, 2, BM_STATUS_INT | BM_STATUS_ERROR);
        qtest_clock_step(f->qts, 100000000);
        g_assert_cmphex(qpci_io_readb(f->dev, bm, 2) & BM_STATUS_INT, ==, 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);
        cmd649_dma_packet(f, ide, 0x1e);
        cmd649_wait_packet(f, ide);
        g_assert_cmphex(qpci_io_readb(f->dev, ide, 7) & 0x89, ==, 0);
        qpci_io_writeb(f->dev, bm, 2, BM_STATUS_INT);
    }
    cmd649_stop(f);
}

static void test_cmd649_masked_completion(gconstpointer opaque)
{
    bool dma = GPOINTER_TO_INT(opaque);
    CMD649Fixture *f = cmd649_start(" -device ide-cd,bus=cmd649.0,unit=1");
    QPCIBar ide = qpci_iomap(f->dev, 0, NULL);
    QPCIBar control = qpci_iomap(f->dev, 1, NULL);
    unsigned i;

    qpci_device_enable(f->dev);
    qtest_qmp_assert_success(f->qts, "{'execute':'cont'}");
    for (i = 0; i < 2; i++) {
        qpci_io_writeb(f->dev, control, 2, 2); /* nIEN */
        cmd649_packet(f, ide, i ? 0xff : 0x1e, dma);
        qtest_clock_step(f->qts, 100000000);
        g_assert_cmphex(qpci_io_readb(f->dev, control, 2) & 0x89, ==,
                       i ? 1 : 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);

        /* Alternate Status preserves completion while INTRQ is masked. */
        qpci_io_writeb(f->dev, control, 2, 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
        qpci_io_writeb(f->dev, control, 2, 2);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);
        qpci_io_writeb(f->dev, control, 2, 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);

        /* Reading Status acknowledges even while INTRQ is masked. */
        qpci_io_writeb(f->dev, control, 2, 2);
        qpci_io_readb(f->dev, ide, 7);
        qpci_io_writeb(f->dev, control, 2, 0);
        g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                       PCI_STATUS_INTERRUPT, ==, 0);
    }

    /* A controller reset cancels an unacknowledged completion. */
    qpci_io_writeb(f->dev, control, 2, 2);
    cmd649_packet(f, ide, 0x1e, dma);
    qtest_clock_step(f->qts, 100000000);
    qtest_system_reset(f->qts);
    control = qpci_iomap(f->dev, 1, NULL);
    qpci_device_enable(f->dev);
    qpci_io_writeb(f->dev, control, 2, 0);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                   PCI_STATUS_INTERRUPT, ==, 0);
    cmd649_stop(f);
}

static void cmd649_wait_migration(QTestState *qts)
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
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelled");
        qobject_unref(result);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_cmd649_masked_migration(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/cmd649-irq-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    CMD649Fixture *f = cmd649_start(" -device ide-cd,bus=cmd649.0,unit=1");
    QPCIBar ide = qpci_iomap(f->dev, 0, NULL);
    QPCIBar control = qpci_iomap(f->dev, 1, NULL);
    int fd = g_mkstemp(path);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qpci_device_enable(f->dev);
    qpci_io_writeb(f->dev, control, 2, 2);
    cmd649_packet(f, ide, 0x1e, false);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                   PCI_STATUS_INTERRUPT, ==, 0);
    qtest_qmp_assert_success(
        f->qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    cmd649_wait_migration(f->qts);
    cmd649_stop(f);

    f = cmd649_start(" -device ide-cd,bus=cmd649.0,unit=1 -incoming defer");
    qtest_qmp_assert_success(
        f->qts, "{'execute':'migrate-incoming','arguments':"
                "{'uri':%s,'exit-on-error':false}}", uri);
    cmd649_wait_migration(f->qts);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                   PCI_STATUS_INTERRUPT, ==, 0);
    qpci_io_writeb(f->dev, control, 2, 0);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                   PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
    qpci_io_readb(f->dev, ide, 7);
    g_assert_cmphex(qpci_config_readw(f->dev, PCI_STATUS) &
                   PCI_STATUS_INTERRUPT, ==, 0);
    cmd649_stop(f);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/cmd649/config", test_cmd649_config);
    qtest_add_func("/cmd649/bmdma-aliases", test_cmd649_bmdma_aliases);
    qtest_add_func("/cmd649/irq-and-modes", test_cmd649_irq_and_modes);
    qtest_add_func("/cmd649/primary-only", test_cmd649_primary_only);
    qtest_add_data_func("/cmd649/atapi-completion/dma-started",
                       GUINT_TO_POINTER(0), test_cmd649_atapi_completion);
    qtest_add_data_func("/cmd649/atapi-completion/dma-preparing",
                       GUINT_TO_POINTER(1), test_cmd649_atapi_completion);
    qtest_add_data_func("/cmd649/atapi-completion/dma-stopped",
                       GUINT_TO_POINTER(2), test_cmd649_atapi_completion);
    qtest_add_func("/cmd649/atapi-completion/reset", test_cmd649_atapi_reset);
    qtest_add_data_func("/cmd649/masked-completion/pio",
                       GINT_TO_POINTER(false), test_cmd649_masked_completion);
    qtest_add_data_func("/cmd649/masked-completion/dma",
                       GINT_TO_POINTER(true), test_cmd649_masked_completion);
    qtest_add_func("/cmd649/masked-completion/migration",
                   test_cmd649_masked_migration);
    return g_test_run();
}
