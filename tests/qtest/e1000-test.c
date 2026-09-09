/*
 * QTest testcase for e1000 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "hw/net/e1000_regs.h"
#include "hw/net/mii.h"
#include "hw/pci/pci_regs.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QE1000 QE1000;

struct QE1000 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static const char *models[] = {
    "e1000",
    "e1000-82540em",
    "e1000-82543gc",
    "e1000-82544gc",
    "e1000-82545em",
};

static void *e1000_get_driver(void *obj, const char *interface)
{
    QE1000 *e1000 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &e1000->dev;
    }

    fprintf(stderr, "%s not present in e1000\n", interface);
    g_assert_not_reached();
}

static void *e1000_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QE1000 *e1000 = g_new0(QE1000, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&e1000->dev, bus, addr);
    e1000->obj.get_driver = e1000_get_driver;

    return &e1000->obj;
}

static void e1000_completion_delays(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QPCIDevice *dev = &((QE1000 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;
    uint64_t txring = guest_alloc(alloc, 128);
    uint64_t rxring = guest_alloc(alloc, 128);
    uint64_t packet = guest_alloc(alloc, 64);
    uint64_t received = guest_alloc(alloc, 8 * 2048);
    uint8_t descriptor[16] = { 0 };

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    /* PHY loopback, broadcast receive, and eight receive descriptors. */
    qpci_io_writel(dev, bar, 0x20, 0x04204000);
    qpci_io_writel(dev, bar, 0x2800, rxring);
    qpci_io_writel(dev, bar, 0x2808, 128);
    qpci_io_writel(dev, bar, 0x2818, 7);
    qpci_io_writel(dev, bar, 0x100, 0x04008002);
    qpci_io_writel(dev, bar, 0x3800, txring);
    qpci_io_writel(dev, bar, 0x3808, 128);
    qpci_io_writel(dev, bar, 0x400, 2);
    qtest_memset(qts, packet, 0xff, 60);
    qtest_memset(qts, txring, 0, 128);
    for (unsigned int i = 0; i < 8; i++) {
        stq_le_p(descriptor, received + i * 2048);
        qtest_memwrite(qts, rxring + i * 16, descriptor, 16);
    }
    /* RCTL defers incoming packets briefly while the queue settles. */
    qtest_clock_step(qts, 1000000000);
    qpci_io_writel(dev, bar, 0xd8, UINT32_MAX);
    qpci_io_readl(dev, bar, 0xc0);
    qpci_io_writel(dev, bar, 0xd0, 0x81); /* TXDW and RXT0 only */
    qpci_io_writel(dev, bar, 0x2820, 100); /* RDTR */
    qpci_io_writel(dev, bar, 0x282c, 150); /* RADV */
    qpci_io_writel(dev, bar, 0x3820, 100); /* TIDV */
    qpci_io_writel(dev, bar, 0x382c, 150); /* TADV */

    for (unsigned int i = 0; i < 2; i++) {
        memset(descriptor, 0, sizeof(descriptor));
        stq_le_p(descriptor, packet);
        stl_le_p(descriptor + 8, 60 | 0x8b000000U); /* EOP, IFCS, RS, IDE */
        qtest_memwrite(qts, txring + i * 16, descriptor, 16);
        qpci_io_writel(dev, bar, 0x3818, i + 1);
        g_assert_cmphex(qtest_readb(qts, txring + i * 16 + 12) & 1, ==, 1);
        g_assert_cmphex(qtest_readb(qts, rxring + i * 16 + 12) & 1, ==, 1);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, 0);
        /* ICR stays clear; accessing it must not cancel pending timers. */
        g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) &
                        (E1000_ICR_TXDW | E1000_ICR_RXT0), ==, 0);
        qpci_io_writel(dev, bar, E1000_ICR,
                       E1000_ICR_TXDW | E1000_ICR_RXT0);
        if (!i) {
            qtest_clock_step(qts, 80 * 1024);
        }
    }
    /* Relative delay restarted, but absolute delay caps it at 150 ticks. */
    qtest_clock_step(qts, 69 * 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, 0);
    qtest_clock_step(qts, 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0xc0) & 0x81, ==, 0x81);

    /* Let the previous notification's ITR window expire. */
    qtest_clock_step(qts, 1000000);
    /* RDTR.FPD expires only the receive timer. */
    qtest_memwrite(qts, txring + 32, descriptor, 16);
    qpci_io_writel(dev, bar, E1000_TDT, 3);
    qpci_io_writel(dev, bar, E1000_RDTR, 100 | E1000_RDTR_FPD);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_RDTR), ==, 100);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) &
                    (E1000_ICR_RXT0 | E1000_ICR_TXDW), ==, E1000_ICR_RXT0);
    qtest_clock_step(qts, 100 * 1024);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) &
                    (E1000_ICR_RXT0 | E1000_ICR_TXDW), ==, E1000_ICR_TXDW);

    /* A pending completion is canceled by a controller reset. */
    qtest_memwrite(qts, txring + 48, descriptor, 16);
    qpci_io_writel(dev, bar, 0x3818, 4);
    qpci_io_writel(dev, bar, 0, 1U << 26);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, 0);
    guest_free(alloc, txring);
    guest_free(alloc, rxring);
    guest_free(alloc, packet);
    guest_free(alloc, received);
}

static void e1000_tx_delay_reserved(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QPCIDevice *dev = &((QE1000 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    uint64_t txring = guest_alloc(alloc, 128);
    uint64_t packet = guest_alloc(alloc, 64);
    uint8_t descriptor[16] = { 0 };
    QPCIBar bar;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST);
    qpci_io_writel(dev, bar, E1000_TDBAL, txring);
    qpci_io_writel(dev, bar, E1000_TDLEN, 128);
    qpci_io_writel(dev, bar, E1000_TCTL, E1000_TCTL_EN);
    qpci_io_writel(dev, bar, E1000_TIDV, 1000);
    qpci_io_writel(dev, bar, E1000_IMS, E1000_IMS_TXDW);
    qtest_memset(qts, packet, 0xff, 60);
    qtest_memset(qts, txring, 0, 128);
    stq_le_p(descriptor, packet);
    stl_le_p(descriptor + 8, 60 | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
              E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE);
    qtest_memwrite(qts, txring, descriptor, sizeof(descriptor));
    qpci_io_writel(dev, bar, E1000_TDT, 1);
    g_assert_cmphex(qtest_readb(qts, txring + 12) & E1000_TXD_STAT_DD,
                    ==, E1000_TXD_STAT_DD);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, 0);

    /* 8254x TIDV bits 31:16 are reserved and must not expire the timer. */
    qpci_io_writel(dev, bar, E1000_TIDV, 1000 | BIT(31));
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_TIDV), ==, 1000);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                    ==, 0);
    qtest_clock_step(qts, 999 * 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, 0);
    qtest_clock_step(qts, 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                    ==, E1000_ICR_TXDW);

    qtest_memwrite(qts, txring + 16, descriptor, sizeof(descriptor));
    qpci_io_writel(dev, bar, E1000_TDT, 2);
    qtest_clock_step(qts, 999 * 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, 0);
    qtest_clock_step(qts, 1024);
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) & PCI_STATUS_INTERRUPT,
                    ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                    ==, E1000_ICR_TXDW);
    guest_free(alloc, txring);
    guest_free(alloc, packet);
}

static void e1000_tx_completion_ide(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    static const struct {
        uint32_t commands[2];
        bool delayed;
    } test_cases[] = {
        { { E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE, E1000_TXD_CMD_RS }, false },
        { { E1000_TXD_CMD_RS, E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE }, false },
        { { E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE,
            E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE }, true },
        /* Descriptors without status writeback do not affect TXDW timing. */
        { { E1000_TXD_CMD_IDE, E1000_TXD_CMD_RS }, false },
        { { 0, E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE }, true },
        { { E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE, 0 }, true },
    };
    QPCIDevice *dev = &((QE1000 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    uint64_t txring = guest_alloc(alloc, 128);
    uint64_t packet = guest_alloc(alloc, 64);
    QPCIBar bar;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qtest_memset(qts, packet, 0xff, 60);
    for (unsigned int c = 0; c < ARRAY_SIZE(test_cases); c++) {
        qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST);
        qpci_io_writel(dev, bar, E1000_TDBAL, txring);
        qpci_io_writel(dev, bar, E1000_TDLEN, 128);
        qpci_io_writel(dev, bar, E1000_TCTL, E1000_TCTL_EN);
        qpci_io_writel(dev, bar, E1000_TIDV, 1000);
        qpci_io_writel(dev, bar, E1000_IMS, E1000_IMS_TXDW);
        qtest_memset(qts, txring, 0, 128);
        for (unsigned int i = 0; i < 2; i++) {
            uint8_t descriptor[16] = { 0 };

            stq_le_p(descriptor, packet);
            stl_le_p(descriptor + 8, 60 | E1000_TXD_CMD_EOP |
                      E1000_TXD_CMD_IFCS | test_cases[c].commands[i]);
            qtest_memwrite(qts, txring + i * 16, descriptor,
                           sizeof(descriptor));
        }
        qpci_io_writel(dev, bar, E1000_TDT, 2);
        for (unsigned int i = 0; i < 2; i++) {
            g_assert_cmphex(qtest_readb(qts, txring + i * 16 + 12) &
                            E1000_TXD_STAT_DD, ==,
                            test_cases[c].commands[i] & E1000_TXD_CMD_RS ?
                            E1000_TXD_STAT_DD : 0);
        }
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==,
                        test_cases[c].delayed ? 0 : PCI_STATUS_INTERRUPT);
        if (test_cases[c].delayed) {
            qtest_clock_step(qts, 999 * 1024);
            g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                            PCI_STATUS_INTERRUPT, ==, 0);
            qtest_clock_step(qts, 1024);
        }
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
        g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                        ==, E1000_ICR_TXDW);
        qtest_clock_step(qts, 2000 * 1024);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, 0);
    }
    guest_free(alloc, txring);
    guest_free(alloc, packet);
}

static void e1000_tx_pending_completion(void *obj, void *data,
                                        QGuestAllocator *alloc)
{
    static const struct {
        bool delayed;
        bool masked;
    } test_cases[] = {
        { false, false },
        { true, false },
        { false, true },
        { true, true },
    };
    QPCIDevice *dev = &((QE1000 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    uint64_t txring = guest_alloc(alloc, 128);
    uint64_t packet = guest_alloc(alloc, 64);
    QPCIBar bar;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qtest_memset(qts, packet, 0xff, 60);
    for (unsigned int c = 0; c < ARRAY_SIZE(test_cases); c++) {
        qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST);
        qpci_io_writel(dev, bar, E1000_TDBAL, txring);
        qpci_io_writel(dev, bar, E1000_TDLEN, 128);
        qpci_io_writel(dev, bar, E1000_TCTL, E1000_TCTL_EN);
        qpci_io_writel(dev, bar, E1000_TIDV, 1000);
        if (!test_cases[c].masked) {
            qpci_io_writel(dev, bar, E1000_IMS, E1000_IMS_TXDW);
        }
        qtest_memset(qts, txring, 0, 128);
        for (unsigned int i = 0; i < 3; i++) {
            uint8_t descriptor[16] = { 0 };
            uint32_t commands = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
                                E1000_TXD_CMD_RS;

            if (i || test_cases[c].delayed) {
                commands |= E1000_TXD_CMD_IDE;
            }
            stq_le_p(descriptor, packet);
            stl_le_p(descriptor + 8, 60 | commands);
            qtest_memwrite(qts, txring + i * 16, descriptor,
                           sizeof(descriptor));
        }

        /* Leave an immediate or expired completion unacknowledged. */
        qpci_io_writel(dev, bar, E1000_TDT, 1);
        qtest_clock_step(qts, 1000 * 1024);
        g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICS) & E1000_ICR_TXDW,
                        ==, E1000_ICR_TXDW);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==,
                        test_cases[c].masked ? 0 : PCI_STATUS_INTERRUPT);

        /* A later IDE descriptor must not delay the pending cause again. */
        qpci_io_writel(dev, bar, E1000_TDT, 2);
        g_assert_cmphex(qtest_readb(qts, txring + 16 + 12) &
                        E1000_TXD_STAT_DD, ==, E1000_TXD_STAT_DD);
        if (test_cases[c].masked) {
            qpci_io_writel(dev, bar, E1000_IMS, E1000_IMS_TXDW);
        }
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
        g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                        ==, E1000_ICR_TXDW);

        /* Once acknowledged, a new completion starts its own delay. */
        qpci_io_writel(dev, bar, E1000_TDT, 3);
        g_assert_cmphex(qtest_readb(qts, txring + 32 + 12) &
                        E1000_TXD_STAT_DD, ==, E1000_TXD_STAT_DD);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, 0);
        qtest_clock_step(qts, 999 * 1024);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, 0);
        qtest_clock_step(qts, 1024);
        g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
        g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_TXDW,
                        ==, E1000_ICR_TXDW);
    }
    guest_free(alloc, txring);
    guest_free(alloc, packet);
}

static uint16_t e1000_read_phy(QPCIDevice *dev, QPCIBar bar, unsigned int reg)
{
    uint32_t mdic;

    qpci_io_writel(dev, bar, E1000_MDIC, E1000_MDIC_OP_READ |
                   (1U << E1000_MDIC_PHY_SHIFT) |
                   (reg << E1000_MDIC_REG_SHIFT));
    mdic = qpci_io_readl(dev, bar, E1000_MDIC);
    g_assert_cmphex(mdic & (E1000_MDIC_ERROR | E1000_MDIC_READY), ==,
                    E1000_MDIC_READY);
    return mdic & E1000_MDIC_DATA_MASK;
}

static void e1000_mac_reset_autoneg(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QPCIDevice *dev = &((QE1000 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    const uint16_t link_up = MII_BMSR_LINK_ST | MII_BMSR_AN_COMP;
    const uint32_t restart = E1000_MDIC_OP_WRITE |
                             (1U << E1000_MDIC_PHY_SHIFT) |
                             (MII_BMCR << E1000_MDIC_REG_SHIFT) |
                             MII_BMCR_AUTOEN | MII_BMCR_ANRESTART;
    QPCIBar bar;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qpci_io_writel(dev, bar, E1000_MDIC, restart);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & link_up, ==, 0);
    qtest_clock_step(qts, 200000000);
    qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMCR), ==, MII_BMCR_AUTOEN);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & link_up, ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_STATUS) & E1000_STATUS_LU,
                    ==, 0);

    /* MAC reset preserves the original 500 ms negotiation deadline. */
    qpci_io_writel(dev, bar, E1000_IMS, E1000_IMS_LSC);
    qtest_clock_step(qts, 299000000);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & link_up, ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_LSC, ==, 0);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & link_up, ==, link_up);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_STATUS) & E1000_STATUS_LU,
                    ==, E1000_STATUS_LU);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_LSC,
                    ==, E1000_ICR_LSC);

    /* A completed negotiation must not restart on another MAC reset. */
    qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST);
    qtest_clock_step(qts, 1000000000);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & link_up, ==, link_up);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_LSC, ==, 0);

    /* An explicit PHY reset still cancels a pending negotiation. */
    qpci_io_writel(dev, bar, E1000_MDIC, restart);
    qpci_io_writel(dev, bar, E1000_CTRL, E1000_CTRL_RST | E1000_CTRL_PHY_RST);
    qtest_clock_step(qts, 1000000000);
    g_assert_cmphex(e1000_read_phy(dev, bar, MII_BMSR) & MII_BMSR_AN_COMP,
                    ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, bar, E1000_ICR) & E1000_ICR_LSC, ==, 0);
    qpci_iounmap(dev, bar);
}

static void e1000_register_nodes(void)
{
    int i;
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    QOSGraphEdgeOptions ia64_opts = { .extra_device_opts = "addr=07.0" };

    add_qpci_address(&ia64_opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(7, 0) });
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    for (i = 0; i < ARRAY_SIZE(models); i++) {
        qos_node_create_driver(models[i], e1000_create);
        qos_node_consumes(models[i], "pci-bus", &opts);
        qos_node_consumes(models[i], "ia64-pci-bus", &ia64_opts);
        qos_node_produces(models[i], "pci-device");
        qos_add_test("completion-delays", models[i],
                     e1000_completion_delays, NULL);
        qos_add_test("tx-delay-reserved", models[i],
                     e1000_tx_delay_reserved, NULL);
        qos_add_test("tx-completion-ide", models[i],
                     e1000_tx_completion_ide, NULL);
        qos_add_test("tx-pending-completion", models[i],
                     e1000_tx_pending_completion, NULL);
        qos_add_test("mac-reset-autoneg", models[i],
                     e1000_mac_reset_autoneg, NULL);
    }
}

libqos_init(e1000_register_nodes);
