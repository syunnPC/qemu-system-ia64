/*
 * PCI storage DMA through the IA-64 physical address map.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu/bswap.h"
#include "hw/ia64/ia64_pci.h"
#include "hw/pci/pci_regs.h"
#include "libqtest.h"

#define ABAR            0xc1020000
#define PORT            (ABAR + 0x100)
#define CMD_PORT        0x1000
#define BMDMA_PORT      0x1200
#define GUARD_SIZE      32
#define READ_LBA        64
#define WRITE_LBA       1024
#define AHCI_GHC_IE     (1U << 1)
#define AHCI_GHC_AE     (1U << 31)
#define AHCI_PXCMD_ST   (1U << 0)
#define AHCI_PXCMD_FRE  (1U << 4)
#define AHCI_PXCMD_FR   (1U << 14)
#define AHCI_PXCMD_CR   (1U << 15)
#define AHCI_PORT_IRQ_MASK  0xfdc000ffU
#define AHCI_PORT_SERR_MASK 0x07ef0f03U

typedef struct StorageDMA {
    QTestState *qts;
    bool ahci;
    uint64_t clb;
    uint64_t fis;
    uint64_t table;
    uint64_t buffer[2];
    size_t length[2];
    size_t total;
} StorageDMA;

static uint64_t sparse_io(uint16_t port)
{
    return IA64_PCI_IO_BASE + ((uint64_t)(port >> 2) << 12) +
           (port & 0xfff);
}

static void pio_writeb(StorageDMA *s, uint16_t port, uint8_t value)
{
    qtest_writeb(s->qts, sparse_io(port), value);
}

static uint8_t pio_readb(StorageDMA *s, uint16_t port)
{
    return qtest_readb(s->qts, sparse_io(port));
}

static void wait_dma(StorageDMA *s)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (s->ahci ? (qtest_readl(s->qts, PORT + 0x38) & 1) :
                     !(pio_readb(s, BMDMA_PORT + 2) & 4)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(s->qts, 1000000);
        g_usleep(1000);
    }
}

static void ahci_wait_port(StorageDMA *s, unsigned int offset,
                           uint32_t mask, uint32_t value)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while ((qtest_readl(s->qts, PORT + offset) & mask) != value) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(s->qts, 1000000);
        g_usleep(1000);
    }
}

static void setup_ahci(StorageDMA *s, uint64_t config)
{
    uint32_t command;

    s->ahci = true;
    /* Exercise all four kinds of DMA addresses above the 32-bit boundary. */
    s->clb = 0x100010000ULL;
    s->fis = 0x100020000ULL;
    s->table = 0x100030000ULL;
    s->buffer[0] = 0x100400ffeULL;
    s->buffer[1] = 0x100900006ULL;
    s->length[0] = 65534;
    s->length[1] = 65538;

    g_assert_cmphex(qtest_readl(s->qts, config), ==, 0x29228086);
    qtest_writel(s->qts, config + PCI_BASE_ADDRESS_5, ABAR);
    g_assert_cmphex(qtest_readl(s->qts, ABAR) & (1U << 31), !=, 0);
    qtest_writel(s->qts, ABAR + 4, AHCI_GHC_AE);
    command = qtest_readl(s->qts, PORT + 0x18) & ~AHCI_PXCMD_ST;
    qtest_writel(s->qts, PORT + 0x18, command);
    ahci_wait_port(s, 0x18, AHCI_PXCMD_CR, 0);
    command &= ~AHCI_PXCMD_FRE;
    qtest_writel(s->qts, PORT + 0x18, command);
    ahci_wait_port(s, 0x18, AHCI_PXCMD_FR, 0);
    qtest_memset(s->qts, s->clb, 0, 1024);
    qtest_memset(s->qts, s->fis, 0, 256);
    qtest_writel(s->qts, PORT, s->clb);
    qtest_writel(s->qts, PORT + 4, s->clb >> 32);
    qtest_writel(s->qts, PORT + 8, s->fis);
    qtest_writel(s->qts, PORT + 12, s->fis >> 32);
    command |= AHCI_PXCMD_FRE;
    qtest_writel(s->qts, PORT + 0x18, command);
    qtest_writel(s->qts, PORT + 0x30, AHCI_PORT_SERR_MASK); /* SERR */
    ahci_wait_port(s, 0x28, 0xf, 3); /* SSTS.DET */
    ahci_wait_port(s, 0x20, 0x89, 0); /* TFD: BSY, DRQ, ERR */
    ahci_wait_port(s, 0x18, AHCI_PXCMD_ST | AHCI_PXCMD_CR |
                   AHCI_PXCMD_FRE | AHCI_PXCMD_FR,
                   AHCI_PXCMD_FRE | AHCI_PXCMD_FR);
    qtest_writel(s->qts, PORT + 0x10, AHCI_PORT_IRQ_MASK); /* IS */
    qtest_writel(s->qts, ABAR + 8, 1); /* IS.IPS */
    g_assert_cmphex(qtest_readl(s->qts, PORT + 0x10), ==, 0);
    g_assert_cmphex(qtest_readl(s->qts, ABAR + 8) & 1, ==, 0);
    qtest_writel(s->qts, PORT + 0x14, AHCI_PORT_IRQ_MASK); /* IE */
    qtest_writel(s->qts, ABAR + 4, AHCI_GHC_AE | AHCI_GHC_IE);
    qtest_writel(s->qts, PORT + 0x18, command | AHCI_PXCMD_ST);
}

static void setup_bmdma(StorageDMA *s, uint64_t config, const char *device)
{
    uint8_t prd[16] = { 0 };
    uint16_t device_id = !strcmp(device, "cmd646-ide") ? 0x0646 : 0x0649;

    s->table = 0x80210000;
    s->buffer[0] = 0x80220000;
    s->buffer[1] = 0x80310000;
    s->length[0] = 65536;
    s->length[1] = 4096;

    g_assert_cmphex(qtest_readl(s->qts, config), ==,
                   ((uint32_t)device_id << 16) | 0x1095);
    qtest_writel(s->qts, config + PCI_BASE_ADDRESS_0, CMD_PORT | 1);
    qtest_writel(s->qts, config + PCI_BASE_ADDRESS_1, 0x1101);
    qtest_writel(s->qts, config + PCI_BASE_ADDRESS_4, BMDMA_PORT | 1);

    /* A zero count encodes 64 KiB; only the final PRD carries EOT. */
    stl_le_p(prd, s->buffer[0]);
    stl_le_p(prd + 8, s->buffer[1]);
    stl_le_p(prd + 12, 0x80000000 | s->length[1]);
    qtest_memwrite(s->qts, s->table, prd, sizeof(prd));
}

static void transfer_ahci(StorageDMA *s, bool write, uint64_t lba)
{
    uint8_t table[128 + 2 * 16] = { 0 };
    uint8_t header[32] = { 0 };

    qtest_writel(s->qts, PORT + 0x10, AHCI_PORT_IRQ_MASK);
    qtest_writel(s->qts, ABAR + 8, 1);
    table[0] = 0x27; /* Register H2D FIS */
    table[1] = 0x80;
    table[2] = write ? 0x35 : 0x25; /* WRITE/READ DMA EXT */
    table[4] = lba;
    table[5] = lba >> 8;
    table[6] = lba >> 16;
    table[7] = 0x40;
    table[8] = lba >> 24;
    table[9] = lba >> 32;
    table[10] = lba >> 40;
    stw_le_p(table + 12, s->total / 512);
    for (int i = 0; i < 2; i++) {
        stq_le_p(table + 128 + i * 16, s->buffer[i]);
        stl_le_p(table + 140 + i * 16,
                 0x80000000 | (s->length[i] - 1));
    }
    stl_le_p(header, (2 << 16) | 5 | (write ? 0x40 : 0));
    stq_le_p(header + 8, s->table);
    qtest_memwrite(s->qts, s->table, table, sizeof(table));
    qtest_memwrite(s->qts, s->clb, header, sizeof(header));
    qtest_writel(s->qts, PORT + 0x38, 1);
    wait_dma(s);

    g_assert_cmphex(qtest_readl(s->qts, PORT + 0x10) & 0xff800000, ==, 0);
    g_assert_cmphex(qtest_readl(s->qts, PORT + 0x20) & 1, ==, 0);
    g_assert_cmpuint(qtest_readl(s->qts, s->clb + 4), ==, s->total);
    g_assert_cmphex(qtest_readb(s->qts, s->fis + 0x40), ==, 0x34);
}

static void transfer_bmdma(StorageDMA *s, bool write, uint64_t lba)
{
    pio_writeb(s, BMDMA_PORT, 0);
    pio_writeb(s, BMDMA_PORT + 2, 6);
    qtest_writel(s->qts, sparse_io(BMDMA_PORT + 4), s->table);
    pio_writeb(s, CMD_PORT + 6, 0xe0 | ((lba >> 24) & 15));
    pio_writeb(s, CMD_PORT + 2, s->total / 512);
    pio_writeb(s, CMD_PORT + 3, lba);
    pio_writeb(s, CMD_PORT + 4, lba >> 8);
    pio_writeb(s, CMD_PORT + 5, lba >> 16);
    pio_writeb(s, CMD_PORT + 7, write ? 0xca : 0xc8);
    pio_writeb(s, BMDMA_PORT, write ? 1 : 9);
    wait_dma(s);

    g_assert_cmphex(pio_readb(s, BMDMA_PORT + 2) & 3, ==, 0);
    g_assert_cmphex(pio_readb(s, CMD_PORT + 7) & 0x89, ==, 0);
    pio_writeb(s, BMDMA_PORT, 0);
}

static void transfer(StorageDMA *s, bool write, uint64_t lba)
{
    if (s->ahci) {
        transfer_ahci(s, write, lba);
    } else {
        transfer_bmdma(s, write, lba);
    }
}

static void check_buffers(StorageDMA *s, const uint8_t *expected)
{
    size_t offset = 0;

    for (int i = 0; i < 2; i++) {
        g_autofree uint8_t *data = g_malloc(s->length[i] + 2 * GUARD_SIZE);

        qtest_memread(s->qts, s->buffer[i] - GUARD_SIZE, data,
                      s->length[i] + 2 * GUARD_SIZE);
        for (int j = 0; j < GUARD_SIZE; j++) {
            g_assert_cmphex(data[j], ==, 0xa5);
            g_assert_cmphex(data[GUARD_SIZE + s->length[i] + j], ==, 0xa5);
        }
        g_assert_cmpmem(data + GUARD_SIZE, s->length[i],
                        expected + offset, s->length[i]);
        offset += s->length[i];
    }
}

static void test_storage_dma(gconstpointer opaque)
{
    const char *device = opaque;
    bool ahci = !strcmp(device, "ich9-ahci");
    g_autofree char *path = NULL;
    g_autofree char *devices = NULL;
    g_autofree uint8_t *expected = NULL;
    g_autofree uint8_t *actual = NULL;
    StorageDMA s = { 0 };
    uint64_t config = IA64_PCI_CONFIG_BASE + ((ahci ? 1 : 5) << 15);
    size_t offset = 0;
    int fd = g_file_open_tmp("ia64-storage-dma-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 16 * 1024 * 1024), ==, 0);
    devices = ahci ? g_strdup("-device ide-hd,drive=disk,bus=ide.0") :
        g_strdup_printf("-device %s,id=storage,addr=5,secondary=1 "
                        "-device ide-hd,drive=disk,bus=storage.0", device);
    s.qts = qtest_initf("-machine itanium2-vpc,nvram=none "
                        "-m 4G -smp 4 -S -nodefaults -display none "
                        "-drive if=none,id=disk,file=%s,format=raw %s",
                        path, devices);
    qtest_writew(s.qts, config + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    if (ahci) {
        setup_ahci(&s, config);
    } else {
        setup_bmdma(&s, config, device);
    }
    s.total = s.length[0] + s.length[1];
    expected = g_malloc(s.total);
    actual = g_malloc(s.total);
    for (size_t i = 0; i < s.total; i++) {
        expected[i] = ((i * 0x9e3779b9U) >> 13) ^ (i >> 8) ^ 0x35;
    }
    g_assert_cmpint(lseek(fd, READ_LBA * 512, SEEK_SET), ==, READ_LBA * 512);
    g_assert_cmpint(write(fd, expected, s.total), ==, s.total);
    for (int i = 0; i < 2; i++) {
        qtest_memset(s.qts, s.buffer[i] - GUARD_SIZE, 0xa5,
                     s.length[i] + 2 * GUARD_SIZE);
        qtest_memset(s.qts, s.buffer[i], 0, s.length[i]);
    }
    transfer(&s, false, READ_LBA);
    check_buffers(&s, expected);

    for (size_t i = 0; i < s.total; i++) {
        expected[i] ^= 0x5a;
    }
    for (int i = 0; i < 2; i++) {
        qtest_memwrite(s.qts, s.buffer[i], expected + offset, s.length[i]);
        offset += s.length[i];
    }
    transfer(&s, true, WRITE_LBA);
    g_assert_cmpint(lseek(fd, WRITE_LBA * 512, SEEK_SET), ==, WRITE_LBA * 512);
    g_assert_cmpint(read(fd, actual, s.total), ==, s.total);
    g_assert_cmpmem(actual, s.total, expected, s.total);
    for (int i = 0; i < 2; i++) {
        qtest_memset(s.qts, s.buffer[i], 0, s.length[i]);
    }
    transfer(&s, false, WRITE_LBA);
    check_buffers(&s, expected);

    qtest_quit(s.qts);
    close(fd);
    g_unlink(path);
}

static void atapi_get_configuration(StorageDMA *s, uint8_t rt,
                                    uint16_t feature, uint16_t allocation,
                                    bool dma)
{
    uint8_t table[128 + 16] = { 0 };
    uint8_t header[32] = { 0 };

    qtest_writel(s->qts, PORT + 0x10, AHCI_PORT_IRQ_MASK);
    qtest_writel(s->qts, ABAR + 8, 1);
    table[0] = 0x27; /* Register H2D FIS */
    table[1] = 0x80;
    table[2] = 0xa0; /* PACKET */
    table[3] = dma; /* PACKET DMA feature */
    table[6] = 8; /* Byte Count Limit: 2048 */
    table[64] = 0x46; /* GET CONFIGURATION */
    table[65] = rt;
    stw_be_p(table + 66, feature);
    stw_be_p(table + 71, allocation);
    if (allocation) {
        stq_le_p(table + 128, s->buffer[0]);
        stl_le_p(table + 140, allocation - 1);
    }
    stl_le_p(header, (allocation ? 1U << 16 : 0) | 0x20 | 5);
    stq_le_p(header + 8, s->table);
    qtest_memwrite(s->qts, s->table, table, sizeof(table));
    qtest_memwrite(s->qts, s->clb, header, sizeof(header));
    qtest_writel(s->qts, PORT + 0x38, 1);
}

static void test_atapi_get_configuration(void)
{
    static const struct {
        uint8_t rt;
        uint16_t feature;
        uint16_t allocation;
    } queries[] = {
        { 0, 0, 20 }, { 1, 0, 20 }, { 2, 0, 20 },
        { 0, 0x2a, 16 }, { 1, 0x2a, 16 }, { 2, 0x2a, 8 },
        { 2, 0x2b, 8 }, { 2, 0xffff, 8 },
        { 2, 0x2a, 4 }, { 2, 0, 4 }, { 2, 0x2a, 0 },
    };
    static const uint8_t profiles[] = {
        0, 0, 0, 16, 0, 0, 0, 16, /* Header: DVD-ROM current profile */
        0, 0, 3, 8,               /* Persistent, current Profile List */
        0, 16, 1, 0,             /* DVD-ROM, current */
        0, 8, 0, 0,              /* CD-ROM */
    };
    static const uint8_t header_only[] = { 0, 0, 0, 4, 0, 0, 0, 16 };
    g_autofree char *path = NULL;
    StorageDMA s = { 0 };
    uint64_t config = IA64_PCI_CONFIG_BASE + (1 << 15);
    int fd = g_file_open_tmp("ia64-atapi-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    /* A medium large enough for the emulated drive to report DVD-ROM. */
    g_assert_cmpint(ftruncate(fd, 1024 * 1024 * 1024), ==, 0);
    s.qts = qtest_initf("-machine itanium2-vpc,nvram=none "
                        "-m 4G -smp 1 -S -nodefaults -display none "
                        "-drive if=none,id=cd,file=%s,format=raw,readonly=on "
                        "-device ide-cd,drive=cd,bus=ide.0", path);
    qtest_writew(s.qts, config + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    setup_ahci(&s, config);

    for (size_t i = 0; i < G_N_ELEMENTS(queries); i++) {
        const uint8_t *expected = queries[i].feature ? header_only : profiles;
        size_t length = queries[i].feature ? sizeof(header_only) :
                                            sizeof(profiles);
        uint8_t data[sizeof(profiles) + GUARD_SIZE];

        g_test_message("GET CONFIGURATION: RT=%u, feature=0x%04x, length=%u",
                       queries[i].rt, queries[i].feature,
                       queries[i].allocation);
        length = MIN(length, queries[i].allocation);
        qtest_memset(s.qts, s.buffer[0], 0xa5, sizeof(data));
        atapi_get_configuration(&s, queries[i].rt, queries[i].feature,
                                queries[i].allocation, true);
        wait_dma(&s);
        g_assert_cmphex(qtest_readl(s.qts, PORT + 0x10) & 0xff800000, ==, 0);
        g_assert_cmphex(qtest_readl(s.qts, PORT + 0x20) & 1, ==, 0);
        g_assert_cmpuint(qtest_readl(s.qts, s.clb + 4), ==, length);
        qtest_memread(s.qts, s.buffer[0], data, sizeof(data));
        g_assert_cmpmem(data, length, expected, length);
        for (size_t j = length; j < sizeof(data); j++) {
            g_assert_cmphex(data[j], ==, 0xa5);
        }
    }

    /* RT=3 is reserved and must still fail with ILLEGAL REQUEST. */
    g_test_message("GET CONFIGURATION: reserved RT=3");
    atapi_get_configuration(&s, 3, 0, 8, false);
    ahci_wait_port(&s, 0x10, 1U << 30, 1U << 30); /* IS.TFES */
    g_assert_cmphex(qtest_readl(s.qts, PORT + 0x20) & 0xf001, ==, 0x5001);
    g_assert_cmpuint(qtest_readl(s.qts, s.clb + 4), ==, 0);

    qtest_quit(s.qts);
    close(fd);
    g_unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (qtest_has_device("cmd646-ide")) {
        qtest_add_data_func("/storage-dma/cmd646", "cmd646-ide",
                            test_storage_dma);
    }
    if (qtest_has_device("cmd649-ide")) {
        qtest_add_data_func("/storage-dma/cmd649", "cmd649-ide",
                            test_storage_dma);
    }
    if (qtest_has_device("ich9-ahci")) {
        qtest_add_data_func("/storage-dma/ahci-high-memory", "ich9-ahci",
                            test_storage_dma);
        qtest_add_func("/storage-dma/atapi-get-configuration",
                       test_atapi_get_configuration);
    }
    return g_test_run();
}
