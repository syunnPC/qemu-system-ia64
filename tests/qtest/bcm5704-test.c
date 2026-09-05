/*
 * Broadcom BCM5701/BCM5704 PCI tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"

#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci.h"
#include "libqos/generic-pcihost.h"
#include "libqos/pci.h"
#include "libqtest.h"

#define IA64_PCI_CONFIG_BASE UINT64_C(0x0000007ff0000000)
#define BCM5701_TEST_SLOT 7U
#define BCM5704_TEST_SLOT 8U
#define BCM57XX_TEST_ROM_BASE (IA64_PCI_MMIO_BASE + 0x02000000)

#define BCM57XX_TEST_PCIX_CAP 0x40
#define BCM57XX_TEST_MISC_HOST_CTRL 0x68
#define BCM57XX_TEST_MISC_HOST_CTRL_RW_MASK 0x000003feU
#define BCM57XX_TEST_PCIX_COMMAND_RW_MASK \
    (PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO | PCI_X_CMD_MAX_READ | \
     PCI_X_CMD_MAX_SPLIT)
#define BCM5704_TEST_PCIX_STATUS_CAPS \
    (PCI_X_STATUS_64BIT | PCI_X_STATUS_133MHZ | (2U << 21) | (1U << 26))

static void bcm5704_qpci_init(QGenericPCIBus *gbus, QTestState *qts)
{
    qpci_init_generic(gbus, qts, NULL, false);
    gbus->ecam_alloc_ptr = IA64_PCI_CONFIG_BASE;
    gbus->bus.mmio_alloc_ptr = IA64_PCI_MMIO_BASE + 0x01000000;
    gbus->bus.mmio_limit = IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE;
}

static void bcm57xx_assert_identity(QPCIDevice *dev, uint16_t device_id,
                                    uint8_t revision,
                                    uint16_t subsystem_vendor_id,
                                    uint16_t subsystem_id,
                                    bool multifunction)
{
    uint8_t header_type;

    g_assert_nonnull(dev);
    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==,
                    BCM57XX_PCI_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==,
                    device_id);
    g_assert_cmphex(qpci_config_readb(dev, PCI_REVISION_ID), ==,
                    revision);
    g_assert_cmphex(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_NETWORK_ETHERNET);
    g_assert_cmphex(qpci_config_readb(dev, PCI_INTERRUPT_PIN), ==, 1);
    if (subsystem_vendor_id) {
        g_assert_cmphex(qpci_config_readw(dev, PCI_SUBSYSTEM_VENDOR_ID), ==,
                        subsystem_vendor_id);
        g_assert_cmphex(qpci_config_readw(dev, PCI_SUBSYSTEM_ID), ==,
                        subsystem_id);
    }

    header_type = qpci_config_readb(dev, PCI_HEADER_TYPE);
    if (multifunction) {
        g_assert_cmphex(header_type & PCI_HEADER_TYPE_MULTI_FUNCTION, !=, 0);
    } else {
        g_assert_cmphex(header_type & PCI_HEADER_TYPE_MULTI_FUNCTION, ==, 0);
    }
}

static void bcm57xx_assert_config_surface(QPCIDevice *dev,
                                          uint16_t chiprev_id,
                                          uint32_t pcix_status)
{
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_CAP_LIST, ==, PCI_STATUS_CAP_LIST);
    g_assert_cmphex(qpci_config_readb(dev, PCI_CAPABILITY_LIST), ==,
                    BCM57XX_TEST_PCIX_CAP);
    g_assert_cmphex(qpci_config_readb(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_CAP_LIST_ID), ==, PCI_CAP_ID_PCIX);
    g_assert_cmphex(qpci_config_readb(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_CAP_LIST_NEXT), ==, 0x48);
    g_assert_cmphex(qpci_config_readb(dev, 0x48), ==, PCI_CAP_ID_PM);
    g_assert_cmphex(qpci_config_readw(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_CMD), ==, 0);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_MISC_HOST_CTRL), ==,
                    (uint32_t)chiprev_id << 16);
}

static void bcm57xx_mutate_config_surface(QPCIDevice *dev,
                                          uint16_t chiprev_id,
                                          uint32_t pcix_status)
{
    qpci_config_writew(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_CMD, UINT16_MAX);
    g_assert_cmphex(qpci_config_readw(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_CMD), ==,
                    BCM57XX_TEST_PCIX_COMMAND_RW_MASK);

    qpci_config_writel(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_STATUS, 0);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);
    qpci_config_writel(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_STATUS,
                       UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);

    qpci_config_writel(dev, BCM57XX_TEST_MISC_HOST_CTRL, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_MISC_HOST_CTRL), ==,
                    ((uint32_t)chiprev_id << 16) |
                    BCM57XX_TEST_MISC_HOST_CTRL_RW_MASK);
}

static uint32_t bcm57xx_probe_rom_size(QPCIDevice *dev)
{
    uint32_t saved_rom = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    uint32_t rom_mask;

    qpci_config_writel(dev, PCI_ROM_ADDRESS, UINT32_MAX);
    rom_mask = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    qpci_config_writel(dev, PCI_ROM_ADDRESS, saved_rom);

    rom_mask &= PCI_ROM_ADDRESS_MASK;
    return ~rom_mask + 1;
}

static void bcm57xx_assert_apertures(QTestState *qts, QPCIDevice *dev,
                                     uint64_t rom_base)
{
    QPCIBar bar;
    uint64_t bar_size;
    uint32_t bar_value;
    uint32_t saved_rom;

    bar_value = qpci_config_readl(dev, PCI_BASE_ADDRESS_0);
    g_assert_cmphex(bar_value & PCI_BASE_ADDRESS_SPACE, ==,
                    PCI_BASE_ADDRESS_SPACE_MEMORY);
    g_assert_cmphex(bar_value & PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_64);

    bar = qpci_iomap(dev, 0, &bar_size);
    g_assert_cmpuint(bar_size, ==, BCM57XX_MMIO_SIZE);
    qpci_device_enable(dev);
    g_assert_cmphex(qpci_io_readw(dev, bar, 0), ==, BCM57XX_PCI_VENDOR_ID);
    g_assert_cmphex(qpci_io_readw(dev, bar, 2), ==,
                    qpci_config_readw(dev, PCI_DEVICE_ID));
    g_assert_cmphex(qpci_io_readl(dev, bar, 4), ==,
                    qpci_config_readl(dev, PCI_COMMAND));

    qpci_io_writel(dev, bar, 0x6800, 0x12345678);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x6800), ==, 0x12345678);

    g_assert_cmpuint(bcm57xx_probe_rom_size(dev), ==, BCM57XX_ROM_SIZE);
    saved_rom = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    qpci_config_writel(dev, PCI_ROM_ADDRESS,
                       rom_base | PCI_ROM_ADDRESS_ENABLE);
    g_assert_cmphex(qtest_readl(qts, rom_base), ==, UINT32_MAX);
    qtest_writel(qts, rom_base, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, rom_base), ==, UINT32_MAX);
    qpci_config_writel(dev, PCI_ROM_ADDRESS, saved_rom);
}

static void test_bcm57xx_enumeration(void)
{
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *bcm5701;
    QPCIDevice *functions[2];
    unsigned int function;

    qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 4G -nodefaults -bios none -S "
        "-device bcm5701,bus=pci,addr=7.0,mac=52:54:00:57:01:00 "
        "-device bcm5704,bus=pci,addr=8.0,multifunction=on,"
        "mac=52:54:00:57:04:00 "
        "-device bcm5704,bus=pci,addr=8.1,mac=52:54:00:57:04:01");
    bcm5704_qpci_init(&gbus, qts);

    bcm5701 = qpci_device_find(&gbus.bus,
                               QPCI_DEVFN(BCM5701_TEST_SLOT, 0));
    bcm57xx_assert_identity(bcm5701, BCM5701_PCI_DEVICE_ID,
                            BCM5701_PCI_REVISION,
                            BCM5701_PCI_SUBSYSTEM_VENDOR_ID,
                            BCM5701_PCI_SUBSYSTEM_ID, false);
    bcm57xx_assert_config_surface(bcm5701, 0x0105, 0);

    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        functions[function] = qpci_device_find(
            &gbus.bus, QPCI_DEVFN(BCM5704_TEST_SLOT, function));
        bcm57xx_assert_identity(functions[function], BCM5704_PCI_DEVICE_ID,
                                BCM5704_PCI_REVISION,
                                BCM5704_PCI_SUBSYSTEM_VENDOR_ID,
                                BCM5704_PCI_SUBSYSTEM_ID, function == 0);
        bcm57xx_assert_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
    }

    bcm57xx_assert_apertures(qts, bcm5701, BCM57XX_TEST_ROM_BASE);
    bcm57xx_assert_apertures(qts, functions[0],
                             BCM57XX_TEST_ROM_BASE + BCM57XX_ROM_SIZE);

    bcm57xx_mutate_config_surface(bcm5701, 0x0105, 0);
    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        bcm57xx_mutate_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
    }

    qtest_system_reset(qts);
    bcm57xx_assert_identity(bcm5701, BCM5701_PCI_DEVICE_ID,
                            BCM5701_PCI_REVISION,
                            BCM5701_PCI_SUBSYSTEM_VENDOR_ID,
                            BCM5701_PCI_SUBSYSTEM_ID, false);
    bcm57xx_assert_config_surface(bcm5701, 0x0105, 0);
    g_free(bcm5701);
    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        bcm57xx_assert_identity(functions[function], BCM5704_PCI_DEVICE_ID,
                                BCM5704_PCI_REVISION,
                                BCM5704_PCI_SUBSYSTEM_VENDOR_ID,
                                BCM5704_PCI_SUBSYSTEM_ID, function == 0);
        bcm57xx_assert_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
        g_free(functions[function]);
    }
    qtest_quit(qts);
}

static void bcm57xx_sram_write(QPCIDevice *dev, uint32_t addr, uint32_t value)
{
    qpci_config_writel(dev, 0x7c, addr);
    qpci_config_writel(dev, 0x84, value);
}

static uint32_t bcm57xx_sram_read(QPCIDevice *dev, uint32_t addr)
{
    qpci_config_writel(dev, 0x7c, addr);
    return qpci_config_readl(dev, 0x84);
}

static uint16_t bcm57xx_mii_read(QPCIDevice *dev, QPCIBar bar, unsigned reg)
{
    uint32_t v;

    qpci_io_writel(dev, bar, 0x44c, 0x28200000 | (reg << 16));
    v = qpci_io_readl(dev, bar, 0x44c);
    g_assert_cmphex(v & 0x30000000, ==, 0);
    return v;
}

static void bcm57xx_desc_word(QTestState *qts, uint64_t addr, uint32_t value,
                              bool big_endian)
{
    uint8_t buf[4];

    if (big_endian) {
        stl_be_p(buf, value);
    } else {
        stl_le_p(buf, value);
    }
    qtest_memwrite(qts, addr, buf, sizeof(buf));
}

static uint32_t bcm57xx_read_desc(QTestState *qts, uint64_t addr, bool be)
{
    uint8_t buf[4];

    qtest_memread(qts, addr, buf, sizeof(buf));
    return be ? ldl_be_p(buf) : ldl_le_p(buf);
}

static void test_bcm57xx_datapath(gconstpointer opaque)
{
    bool big_endian = GPOINTER_TO_INT(opaque) & 1;
    const char *model = GPOINTER_TO_INT(opaque) & 2 ? "bcm5704" : "bcm5701";
    g_autofree char *cmd = g_strdup_printf(
        "-machine ia64-vpc,nvram=none -m 4G -nodefaults -bios none -S "
        "-device %s,bus=pci,addr=7.0,mac=52:54:00:57:01:00", model);
    QTestState *qts = qtest_init(cmd);
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;
    uint8_t packet[60], received[64];
    const uint64_t txring = 0x100000, rxring = 0x110000;
    const uint64_t retr = 0x120000, status = 0x130000;
    const uint64_t txbuf = 0x140000, rxbuf = 0x150000;
    unsigned i;
    uint32_t ctrl;

    bcm5704_qpci_init(&gbus, qts);
    dev = qpci_device_find(&gbus.bus, QPCI_DEVFN(7, 0));
    bar = qpci_iomap(dev, 0, NULL);
    qpci_device_enable(dev);
    qpci_config_writew(dev, PCI_COMMAND,
                       qpci_config_readw(dev, PCI_COMMAND) |
                       PCI_COMMAND_MASTER);

    /* PHY identity, link state, and the reset mailbox handshake. */
    g_assert_cmphex(bcm57xx_mii_read(dev, bar, 2), ==, 0x20);
    g_assert_cmphex(bcm57xx_mii_read(dev, bar, 1) & 0x24, ==, 0x24);
    qpci_io_writel(dev, bar, 0x44c, 0x24208000);
    g_assert_cmphex(bcm57xx_mii_read(dev, bar, 0) & 0x8000, ==, 0);
    bcm57xx_sram_write(dev, 0xb50, 0x4b657654);
    qpci_io_writel(dev, bar, 0x6804, 1);
    g_assert_cmphex(bcm57xx_sram_read(dev, 0xb50), ==, ~0x4b657654U);
    g_assert_cmphex(bcm57xx_sram_read(dev, 0xc14), ==, 0x484b5254);
    g_assert_cmphex(bcm57xx_sram_read(dev, 0xc18), ==, 0x00570100);
    qpci_io_writel(dev, bar, 0x6838, 0x8200007c);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x6838) & 0x42000000,
                    ==, 0x40000000);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x683c), ==, 0x54520000);
    qpci_io_writel(dev, bar, 0x7020, 2);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x7020) & 0x2200, ==, 0x2200);
    qpci_io_writel(dev, bar, 0x7020, 0x20);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x7020), ==, 0);
    qpci_io_writel(dev, bar, 0x700c, 0x7c);
    qpci_io_writel(dev, bar, 0x7000, 0x190);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x7000) & 0x18, ==, 8);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x7010), ==, 0x00005254);
    qpci_config_writew(dev, 0x4c, 3);
    g_assert_cmphex(qpci_config_readw(dev, 0x4c) & 3, ==, 3);
    qpci_config_writew(dev, 0x4c, 0);

    qpci_io_writel(dev, bar, 0x6800, 0x20034 | (big_endian ? 2 : 0));
    /* Host send ring and host receive return ring, four entries each. */
    bcm57xx_sram_write(dev, 0x100, 0);
    bcm57xx_sram_write(dev, 0x104, txring);
    bcm57xx_sram_write(dev, 0x108, 4 << 16);
    bcm57xx_sram_write(dev, 0x200, 0);
    bcm57xx_sram_write(dev, 0x204, retr);
    bcm57xx_sram_write(dev, 0x208, 4 << 16);
    qpci_io_writel(dev, bar, 0x2450, 0);
    qpci_io_writel(dev, bar, 0x2454, rxring);
    qpci_io_writel(dev, bar, 0x2458, 1536 << 16);
    qpci_io_writel(dev, bar, 0x3c38, 0);
    qpci_io_writel(dev, bar, 0x3c3c, status);
    qpci_io_writel(dev, bar, 0x3c30, 0);
    qpci_io_writel(dev, bar, 0x3c34, 0x180000);
    qpci_io_writel(dev, bar, 0x3c28, 1);
    qpci_io_writel(dev, bar, 0x3c00, 0x102);
    /* MAC loopback is independent of any host network backend. */
    qpci_io_writel(dev, bar, 0x400, 0x10);
    qpci_io_writel(dev, bar, 0x45c, 2);
    qpci_io_writel(dev, bar, 0x468, 2);

    for (i = 0; i < sizeof(packet); i++) {
        packet[i] = i;
    }
    memcpy(packet, "\x52\x54\x00\x57\x01\x00", 6);
    packet[12] = 0x88;
    packet[13] = 0xb5;
    qtest_memwrite(qts, txbuf, packet, sizeof(packet));
    qtest_memset(qts, rxbuf, 0xa5, 128);
    bcm57xx_desc_word(qts, rxring, 0, big_endian);
    bcm57xx_desc_word(qts, rxring + 4, rxbuf, big_endian);
    bcm57xx_desc_word(qts, rxring + 8, 1536, big_endian);
    bcm57xx_desc_word(qts, rxring + 28, 0xdeadbeef, big_endian);
    qpci_io_writel(dev, bar, 0x26c, 1);
    /* A packet split over two descriptors must retain the first fragment. */
    bcm57xx_desc_word(qts, txring, 0, big_endian);
    bcm57xx_desc_word(qts, txring + 4, txbuf, big_endian);
    bcm57xx_desc_word(qts, txring + 8, 20 << 16, big_endian);
    bcm57xx_desc_word(qts, txring + 16, 0, big_endian);
    bcm57xx_desc_word(qts, txring + 20, txbuf + 20, big_endian);
    bcm57xx_desc_word(qts, txring + 24, (40 << 16) | 4, big_endian);
    qpci_io_writel(dev, bar, 0x304, 1);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x3c80), ==, 0);
    qpci_io_writel(dev, bar, 0x304, 2);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x3c80), ==, 1);
    g_assert_cmphex(bcm57xx_read_desc(qts, status + 16, big_endian),
                    ==, 0x20001);
    g_assert_cmphex(bcm57xx_read_desc(qts, retr + 8, big_endian), ==, 64);
    g_assert_cmphex(bcm57xx_read_desc(qts, retr + 28, big_endian),
                    ==, 0xdeadbeef);
    qtest_memread(qts, rxbuf, received, sizeof(received));
    g_assert_cmpmem(received, sizeof(packet), packet, sizeof(packet));
    g_assert_cmphex(qtest_readb(qts, rxbuf + 64), ==, 0xa5);

    /* Mask/unmask preserves the pending INTx, mailbox acknowledges it. */
    ctrl = qpci_config_readl(dev, 0x68);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 0);
    qpci_config_writel(dev, 0x68, ctrl | 2);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 2);
    qpci_config_writel(dev, 0x68, ctrl & ~2U);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 0);
    qpci_io_writel(dev, bar, 0x204, 1);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 2);
    qpci_io_writel(dev, bar, 0x204, 0);

    /* Tagged rearm must report completions newer than the acknowledged tag. */
    qpci_config_writel(dev, 0x68, ctrl | 0x200);
    ctrl = bcm57xx_read_desc(qts, status + 4, big_endian);
    qpci_io_writel(dev, bar, 0x204, 1);
    qpci_io_writel(dev, bar, 0x3c00, 0x10a);
    qpci_io_writel(dev, bar, 0x204, ctrl << 24);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 0);
    ctrl = bcm57xx_read_desc(qts, status + 4, big_endian);
    qpci_io_writel(dev, bar, 0x204, ctrl << 24);
    g_assert_cmphex(qpci_config_readl(dev, 0x70) & 2, ==, 2);
    /* Host statistics carry packet and octet counts in high/low pairs. */
    g_assert_cmphex(bcm57xx_read_desc(qts, 0x180104, big_endian), ==, 60);
    g_assert_cmphex(bcm57xx_read_desc(qts, 0x180304, big_endian), ==, 60);
    g_assert_cmphex(bcm57xx_read_desc(qts, 0x1803dc, big_endian), ==, 1);

    /* A descriptor exceeding the frame buffer must not DMA beyond it. */
    bcm57xx_desc_word(qts, txring + 32, 0, big_endian);
    bcm57xx_desc_word(qts, txring + 36, txbuf, big_endian);
    bcm57xx_desc_word(qts, txring + 40, 0xffff0004, big_endian);
    qpci_io_writel(dev, bar, 0x304, 3);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x4804) & 8, ==, 8);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x3cc0), ==, 2);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x3c80), ==, 1);
    g_free(dev);
    qtest_quit(qts);
}

static void test_bcm57xx_dma_queues(void)
{
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 4G -nodefaults -bios none -S "
        "-device bcm5701,bus=pci,addr=7.0");
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;
    uint32_t i;

    bcm5704_qpci_init(&gbus, qts);
    dev = qpci_device_find(&gbus.bus, QPCI_DEVFN(7, 0));
    bar = qpci_iomap(dev, 0, NULL);
    qpci_device_enable(dev);
    qpci_config_writew(dev, PCI_COMMAND,
                       qpci_config_readw(dev, PCI_COMMAND) |
                       PCI_COMMAND_MASTER);
    qpci_io_writel(dev, bar, 0x6800, 0x34);
    for (i = 0; i < 256; i += 4) {
        qtest_writel(qts, 0x100000 + i, i ^ 0x12345678);
    }
    bcm57xx_sram_write(dev, 0x2000, 0);
    bcm57xx_sram_write(dev, 0x2004, 0x100000);
    bcm57xx_sram_write(dev, 0x2008, 0x2100);
    bcm57xx_sram_write(dev, 0x200c, 0x0d020100);
    qpci_io_writel(dev, bar, 0x5c28, 0x2000);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x5cd8), ==, 0x2000);
    for (i = 0; i < 256; i += 4) {
        g_assert_cmphex(bcm57xx_sram_read(dev, 0x2100 + i), ==, i ^ 0x12345678);
    }
    bcm57xx_sram_write(dev, 0x2004, 0x110000);
    bcm57xx_sram_write(dev, 0x200c, 0x10070100);
    qpci_io_writel(dev, bar, 0x5c78, 0x2000);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x5d08), ==, 0x2000);
    for (i = 0; i < 256; i += 4) {
        g_assert_cmphex(qtest_readl(qts, 0x110000 + i), ==, i ^ 0x12345678);
    }
    g_free(dev);
    qtest_quit(qts);
}

static uint32_t bcm57xx_test_csum(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;

    while (len > 1) {
        sum += (p[0] << 8) | p[1];
        len -= 2;
        p += 2;
    }
    if (len) {
        sum += p[0] << 8;
    }
    return sum;
}

static void test_bcm57xx_tso(void)
{
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 4G -nodefaults -bios none -S "
        "-device bcm5701,bus=pci,addr=7.0,mac=52:54:00:57:01:00");
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;
    uint8_t packet[4054] = { 0 }, frame[1536], other[60];
    unsigned i, sent = 0;

    bcm5704_qpci_init(&gbus, qts);
    dev = qpci_device_find(&gbus.bus, QPCI_DEVFN(7, 0));
    bar = qpci_iomap(dev, 0, NULL);
    qpci_device_enable(dev);
    qpci_config_writew(dev, PCI_COMMAND,
                       qpci_config_readw(dev, PCI_COMMAND) |
                       PCI_COMMAND_MASTER);
    qpci_io_writel(dev, bar, 0x6800, 0x20034);
    for (i = 0; i < 2; i++) {
        bcm57xx_sram_write(dev, 0x100 + i * 16, 0);
        bcm57xx_sram_write(dev, 0x104 + i * 16, 0x100000 + i * 0x1000);
        bcm57xx_sram_write(dev, 0x108 + i * 16, 4 << 16);
    }
    bcm57xx_sram_write(dev, 0x200, 0);
    bcm57xx_sram_write(dev, 0x204, 0x120000);
    bcm57xx_sram_write(dev, 0x208, 8 << 16);
    qpci_io_writel(dev, bar, 0x2454, 0x110000);
    qpci_io_writel(dev, bar, 0x2458, 1536 << 16);
    qpci_io_writel(dev, bar, 0x3c3c, 0x130000);
    qpci_io_writel(dev, bar, 0x3c00, 0x102);
    qpci_io_writel(dev, bar, 0x400, 0x10);
    qpci_io_writel(dev, bar, 0x45c, 2);
    qpci_io_writel(dev, bar, 0x468, 2);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, 0x110000 + i * 32, 0);
        qtest_writel(qts, 0x110004 + i * 32, 0x150000 + i * 0x1000);
        qtest_writel(qts, 0x110008 + i * 32, (i << 16) | 1536);
        qtest_writel(qts, 0x11001c + i * 32, 0xaa00 + i);
    }
    qpci_io_writel(dev, bar, 0x26c, 4);
    memcpy(packet, "\x52\x54\x00\x57\x01\x00", 6);
    memcpy(packet + 6, "\x52\x54\x00\x57\x01\x01", 6);
    stw_be_p(packet + 12, 0x0800);
    packet[14] = 0x45;
    stw_be_p(packet + 16, 1440);
    stw_be_p(packet + 18, 0x1000);
    packet[22] = 64;
    packet[23] = 6;
    stl_be_p(packet + 26, 0x0a000201);
    stl_be_p(packet + 30, 0x0a000202);
    stw_be_p(packet + 34, 10000);
    stw_be_p(packet + 36, 80);
    stl_be_p(packet + 38, 0x100);
    packet[46] = 0x50;
    packet[47] = 0x99; /* CWR, ACK, PSH, FIN */
    for (i = 54; i < sizeof(packet); i++) {
        packet[i] = i;
    }
    qtest_memwrite(qts, 0x140000, packet, sizeof(packet));
    qtest_writel(qts, 0x100000, 0);
    qtest_writel(qts, 0x100004, 0x140000);
    qtest_writel(qts, 0x100008, (54 << 16) | 0x301);
    qtest_writel(qts, 0x10000c, 1400 << 16);
    qtest_writel(qts, 0x100010, 0);
    qtest_writel(qts, 0x100014, 0x140036);
    qtest_writel(qts, 0x100018, (4000 << 16) | 4);
    /* Leave ring0's first fragment pending while ring1 sends a full packet. */
    qpci_io_writel(dev, bar, 0x304, 1);
    memcpy(other, packet, sizeof(other));
    stw_be_p(other + 12, 0x88b5);
    qtest_memwrite(qts, 0x142000, other, sizeof(other));
    qtest_writel(qts, 0x101000, 0);
    qtest_writel(qts, 0x101004, 0x142000);
    qtest_writel(qts, 0x101008, (60 << 16) | 4);
    qpci_io_writel(dev, bar, 0x30c, 1);
    qtest_memread(qts, 0x150000, frame, sizeof(other));
    g_assert_cmpmem(frame, sizeof(other), other, sizeof(other));
    qpci_io_writel(dev, bar, 0x304, 2);
    g_assert_cmphex(qtest_readl(qts, 0x130010), ==, 0x00020004);
    g_assert_cmphex(qtest_readl(qts, 0x130014), ==, 0x00010000);
    for (i = 0; i < 3; i++) {
        unsigned n = MIN(1400, 4000 - sent);
        uint32_t sum;

        qtest_memread(qts, 0x151000 + i * 0x1000, frame, n + 54);
        g_assert_cmphex(qtest_readl(qts, 0x120028 + i * 32) & 0xffff,
                        ==, n + 58);
        g_assert_cmphex(qtest_readl(qts, 0x12003c + i * 32), ==, 0xaa01 + i);
        g_assert_cmphex(lduw_be_p(frame + 16), ==, n + 40);
        g_assert_cmphex(lduw_be_p(frame + 18), ==, 0x1000 + i);
        g_assert_cmphex(ldl_be_p(frame + 38), ==, 0x100 + sent);
        g_assert_cmphex(frame[47], ==, i == 0 ? 0x90 : i == 2 ? 0x19 : 0x10);
        g_assert_cmpmem(frame + 54, n, packet + 54 + sent, n);
        sum = bcm57xx_test_csum(frame + 14, 20);
        while (sum >> 16) {
            sum = (sum & 0xffff) + (sum >> 16);
        }
        g_assert_cmphex(sum, ==, 0xffff);
        sum = bcm57xx_test_csum(frame + 26, 8) + 6 + n + 20 +
              bcm57xx_test_csum(frame + 34, n + 20);
        while (sum >> 16) {
            sum = (sum & 0xffff) + (sum >> 16);
        }
        g_assert_cmphex(sum, ==, 0xffff);
        sent += n;
    }
    g_free(dev);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm57xx/enumeration", test_bcm57xx_enumeration);
    qtest_add_func("/bcm57xx/diagnostic-dma", test_bcm57xx_dma_queues);
    qtest_add_func("/bcm57xx/tso-interleaved-rings", test_bcm57xx_tso);
    qtest_add_data_func("/bcm57xx/5701-datapath-le", GINT_TO_POINTER(0),
                        test_bcm57xx_datapath);
    qtest_add_data_func("/bcm57xx/5701-datapath-be", GINT_TO_POINTER(1),
                        test_bcm57xx_datapath);
    qtest_add_data_func("/bcm57xx/5704-datapath-le", GINT_TO_POINTER(2),
                        test_bcm57xx_datapath);
    qtest_add_data_func("/bcm57xx/5704-datapath-be", GINT_TO_POINTER(3),
                        test_bcm57xx_datapath);
    return g_test_run();
}
