/*
 * AMD IOMMU event logging tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/iommu-testdev.h"
#include "qobject/qdict.h"

/* AMD IOMMU Specification, 48882 Rev. 3.09, sections 2.5.3, 3.2 and 3.4. */
#define IOMMU_BASE          0xfed80000
#define REG_DEVTAB          0x0000
#define REG_CMDBASE         0x0008
#define REG_EVTBASE         0x0010
#define REG_CONTROL         0x0018
#define REG_FEATURES        0x0030
#define REG_CMDHEAD         0x2000
#define REG_CMDTAIL         0x2008
#define REG_EVTHEAD         0x2010
#define REG_EVTTAIL         0x2018
#define REG_STATUS          0x2020
#define CONTROL_ENABLE     (1 << 0)
#define CONTROL_EVTLOG     (1 << 2)
#define CONTROL_EVTINT     (1 << 3)
#define CONTROL_CMD        (1 << 12)
#define STATUS_OVERFLOW    (1 << 0)
#define STATUS_EVTINT      (1 << 1)
#define DEVTAB             0x100000
#define PAGE_TABLE         0x140000
#define PAGE_DIRECTORY     0x141000
#define EVENT_LOG          0x150000
#define DMA_ADDR           0x1000
#define DMA_TARGET         0x160000
#define COMMAND_BUFFER     0x170000
#define DOMAIN_ID          0xabcd
#define DEVICE_ID          0x1200
#define ENTRY_READ         (1ULL << 61)
#define ENTRY_WRITE        (1ULL << 62)
#define ENTRY_RW           (ENTRY_READ | ENTRY_WRITE)
#define EVENT_IOPF         (2U << 28)
#define EVENT_PR           (1U << 20)
#define EVENT_RW           (1U << 21)
#define EVENT_PE           (1U << 22)

typedef struct AMDVIFixture {
    QTestState *qts;
    QPCIBus *bus;
    QPCIDevice *dev;
    QPCIBar bar;
} AMDVIFixture;

typedef struct FaultTest {
    const char *name;
    uint64_t pte;
    bool write;
    uint32_t expected;
    unsigned check;
    uint64_t dte_perms;
    uint64_t pde_perms;
} FaultTest;

static void save_device(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **result = data;

    *result = dev;
}

static QPCIDevice *find_iommu(QPCIBus *bus)
{
    QPCIDevice *dev = NULL;

    qpci_device_foreach(bus, 0x1022, 0x1419, save_device, &dev);
    g_assert(dev);
    return dev;
}

static void reg_write(AMDVIFixture *f, unsigned reg, uint64_t value)
{
    qtest_writeq(f->qts, IOMMU_BASE + reg, value);
}

static uint64_t reg_read(AMDVIFixture *f, unsigned reg)
{
    return qtest_readq(f->qts, IOMMU_BASE + reg);
}

static void fixture_setup_device(AMDVIFixture *f, bool notifier)
{
    QPCIDevice *bridge;
    QPCIDevice *iommu;
    uint8_t cap;

    f->qts = qtest_initf("-machine q35 -nodefaults "
                        "-device amd-iommu,dma-remap=on "
                        "-device pcie-root-port,id=bridge,chassis=1,addr=2 "
                        "-device pcie-root-port,id=late,chassis=2,addr=3 "
                        "-device %s,bus=bridge,addr=0",
                        notifier ? "iommu-testdev" :
                        "edu,dma_mask=0xffffffffffffffff");
    f->bus = qpci_new_pc(f->qts, NULL);
    iommu = find_iommu(f->bus);
    cap = qpci_find_capability(iommu, 0x0f, 0);
    g_assert(cap);
    qpci_config_writel(iommu, cap + 4, IOMMU_BASE | 1);
    g_free(iommu);
    bridge = qpci_device_find(f->bus, 2 << 3);
    g_assert_nonnull(bridge);
    qpci_config_writeb(bridge, PCI_PRIMARY_BUS, 0);
    qpci_config_writeb(bridge, PCI_SECONDARY_BUS, DEVICE_ID >> 8);
    qpci_config_writeb(bridge, PCI_SUBORDINATE_BUS, DEVICE_ID >> 8);
    f->dev = qpci_device_find(f->bus, DEVICE_ID);
    g_assert_nonnull(f->dev);
    f->bar = qpci_iomap(f->dev, 0, NULL);
    qpci_config_writew(bridge, PCI_MEMORY_BASE, f->bar.addr >> 16);
    qpci_config_writew(bridge, PCI_MEMORY_LIMIT, f->bar.addr >> 16);
    qpci_device_enable(bridge);
    qpci_device_enable(f->dev);
    if (!notifier) {
        g_assert_cmphex(qpci_io_readl(f->dev, f->bar, 0) & 0xffff, ==, 0x00ed);
    }
    g_free(bridge);

    /* The Device Table covers the endpoint's nonzero bus number. */
    reg_write(f, REG_DEVTAB, DEVTAB | 0x25);
    qtest_writeq(f->qts, DEVTAB + DEVICE_ID * 32,
                 PAGE_TABLE | ENTRY_RW | (1 << 9) | 3);
    qtest_writeq(f->qts, DEVTAB + DEVICE_ID * 32 + 8, DOMAIN_ID);
    reg_write(f, REG_EVTBASE, EVENT_LOG | (8ULL << 56));
    reg_write(f, REG_CMDBASE, COMMAND_BUFFER | (8ULL << 56));
    reg_write(f, REG_CONTROL,
               CONTROL_ENABLE | CONTROL_EVTLOG | CONTROL_CMD);
}

static void fixture_setup(AMDVIFixture *f)
{
    fixture_setup_device(f, false);
}

static void fixture_teardown(AMDVIFixture *f)
{
    g_free(f->dev);
    qpci_free_pc(f->bus);
    qtest_quit(f->qts);
}

static void send_command_data(AMDVIFixture *f, uint64_t command, uint64_t data)
{
    uint64_t tail = reg_read(f, REG_CMDTAIL);

    qtest_writeq(f->qts, COMMAND_BUFFER + tail, command);
    qtest_writeq(f->qts, COMMAND_BUFFER + tail + 8, data);
    reg_write(f, REG_CMDTAIL, (tail + 16) % 4096);
    g_assert_cmphex(reg_read(f, REG_CMDHEAD), ==, (tail + 16) % 4096);
}

static void send_command(AMDVIFixture *f, uint64_t command)
{
    send_command_data(f, command, 0);
}

static void run_dma_address(AMDVIFixture *f, uint64_t addr, bool write)
{
    /* EDU's DMA engine transfers one byte between guest RAM and its buffer. */
    qpci_io_writeq(f->dev, f->bar, 0x80, write ? 0x40000 : addr);
    qpci_io_writeq(f->dev, f->bar, 0x88, write ? addr : 0x40000);
    qpci_io_writeq(f->dev, f->bar, 0x90, 1);
    qpci_io_writeq(f->dev, f->bar, 0x98, write ? 3 : 1);
    qtest_clock_step(f->qts, 100000000);
    g_assert_cmphex(qpci_io_readq(f->dev, f->bar, 0x98) & 1, ==, 0);
}

static void run_dma(AMDVIFixture *f, bool write)
{
    run_dma_address(f, DMA_ADDR, write);
}

static void check_testdev_dma(AMDVIFixture *f, uint64_t addr, uint64_t target,
                              bool fault)
{
    qtest_writel(f->qts, target, 0xa5a5a5a5);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_GVA_LO, addr);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_GVA_HI, addr >> 32);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_GPA_LO, target);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_GPA_HI, target >> 32);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_LEN, 4);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);
    qpci_io_writel(f->dev, f->bar, ITD_REG_DMA_TRIGGERING, 1);
    qpci_io_readl(f->dev, f->bar, ITD_REG_DMA_TRIGGERING);
    g_assert_cmphex(qpci_io_readl(f->dev, f->bar, ITD_REG_DMA_RESULT), ==,
                    fault ? ITD_DMA_ERR_TX_FAIL : 0);
    g_assert_cmphex(qtest_readl(f->qts, target), ==,
                    fault ? 0xa5a5a5a5 : ITD_DMA_WRITE_VAL);
    g_assert_cmphex(reg_read(f, REG_EVTTAIL), ==, fault ? 16 : 0);
    if (fault) {
        g_assert_cmphex(qtest_readl(f->qts, EVENT_LOG), ==, DEVICE_ID);
        g_assert_cmphex(qtest_readl(f->qts, EVENT_LOG + 4), ==,
                        EVENT_IOPF | EVENT_PR | EVENT_RW | DOMAIN_ID);
        g_assert_cmphex(qtest_readq(f->qts, EVENT_LOG + 8), ==, addr);
    }
}

typedef struct WalkTest {
    const char *name;
    unsigned level;
    unsigned next;
    uint64_t addr;
    bool fault;
} WalkTest;

static void test_walk_levels(const void *opaque)
{
    const WalkTest *test = opaque;
    const uint64_t root = 0x142000;
    unsigned index = (test->addr >> (12 + 9 * (test->level - 1))) & 511;
    AMDVIFixture f;

    fixture_setup_device(&f, true);
    /* Section 2.2.3 permits skipped levels only for zero skipped bits. */
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 root | ENTRY_RW | (test->level << 9) | 3);
    qtest_writeq(f.qts, root + index * 8,
                 PAGE_TABLE | ENTRY_RW | (test->next << 9) | 1);
    qtest_writeq(f.qts, PAGE_TABLE + 8, 0x400000 | ENTRY_RW | 1);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    qtest_writel(f.qts, 0x401080, 0x11223344);
    check_testdev_dma(&f, test->addr, 0x400080, test->fault);
    g_assert_cmphex(qtest_readl(f.qts, 0x401080), ==, 0x11223344);
    fixture_teardown(&f);
}

typedef struct LeafTest {
    const char *name;
    unsigned level;
    uint64_t pte;
    uint64_t addr;
    uint64_t target;
    bool fault;
} LeafTest;

static void test_leaf_encoding(const void *opaque)
{
    const LeafTest *test = opaque;
    AMDVIFixture f;

    fixture_setup_device(&f, true);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 PAGE_TABLE | ENTRY_RW | (test->level << 9) | 3);
    /* Table 14 requires repeated entries for NextLevel=7 pages. */
    qtest_writeq(f.qts, PAGE_TABLE, test->pte | ENTRY_RW | 1);
    qtest_writeq(f.qts, PAGE_TABLE + 8, test->pte | ENTRY_RW | 1);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    check_testdev_dma(&f, test->addr, test->target, test->fault);
    fixture_teardown(&f);
}

static void test_walk_notifier(void)
{
    const uint64_t root = 0x142000;
    AMDVIFixture f;

    fixture_setup_device(&f, true);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 root | ENTRY_RW | (3 << 9) | 3);
    qtest_writeq(f.qts, root, PAGE_TABLE | ENTRY_RW | (1 << 9) | 1);
    qtest_writeq(f.qts, PAGE_TABLE + 8, DMA_TARGET | ENTRY_RW | 1);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_GVA_LO, DMA_ADDR);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_LEN, 0x400000);
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP0), ==, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_BAD0), ==, 0);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 3);

    /* Rewalking the skipped range must preserve its valid low addresses. */
    send_command_data(&f, (3ULL << 60) | ((uint64_t)DOMAIN_ID << 32),
                      0x7ffffffffffff000ULL | 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP0), ==, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 3);

    /* A malformed replacement must remove the old external mapping. */
    qtest_writeq(f.qts, root, PAGE_TABLE | ENTRY_RW | (3 << 9) | 1);
    send_command_data(&f, (3ULL << 60) | ((uint64_t)DOMAIN_ID << 32),
                      DMA_ADDR);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP0), ==, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 0);

    qtest_writeq(f.qts, root, 0x400000 | ENTRY_RW | 1);
    send_command_data(&f, (3ULL << 60) | ((uint64_t)DOMAIN_ID << 32),
                      DMA_ADDR);
    /* A level-3 leaf requires 1 GiB alignment, not 4 KiB alignment. */
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP0), ==, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 0);
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, 0);
    fixture_teardown(&f);
}

static void test_large_page(const void *opaque)
{
    unsigned config = GPOINTER_TO_UINT(opaque);
    unsigned level = config >> 4;
    unsigned restriction = (config >> 2) & 3;
    bool cached = config & 2;
    bool write = config & 1;
    uint64_t restricted = write ? ENTRY_READ : ENTRY_WRITE;
    uint64_t root = level == 2 ? PAGE_DIRECTORY : 0x142000;
    uint64_t root_perms = restriction == 1 ? restricted : ENTRY_RW;
    uint64_t leaf_perms = restriction == 3 ? restricted : ENTRY_RW;
    uint64_t source = level == 2 ? 0x201080 : 0x40201080;
    uint64_t target = level == 2 ? 0x401080 : 0x201080;
    uint64_t sink = level == 2 ? 0x400000 : 0;
    uint64_t wrong_target = level == 2 ? 0x400080 : 0x1080;
    unsigned mode = level;
    AMDVIFixture f;

    fixture_setup(&f);
    /* Table 15: NextLevel=0 leaves in tables 2 and 3 map 2 MiB and 1 GiB. */
    qtest_writeq(f.qts, root + 8,
                 (level == 2 ? 0x400000 : 0) | leaf_perms | 1);
    /* A separate 4 KiB mapping makes DMA reads observable through the sink. */
    qtest_writeq(f.qts, PAGE_TABLE, DMA_TARGET | ENTRY_RW | 1);
    if (level == 2) {
        qtest_writeq(f.qts, root + 16,
                     PAGE_TABLE | ENTRY_RW | (1 << 9) | 1);
    } else {
        qtest_writeq(f.qts, root,
                     PAGE_DIRECTORY | ENTRY_RW | (2 << 9) | 1);
        qtest_writeq(f.qts, PAGE_DIRECTORY,
                     PAGE_TABLE | ENTRY_RW | (1 << 9) | 1);
    }
    if (restriction == 2) {
        uint64_t parent = 0x143000;

        qtest_writeq(f.qts, parent, root | restricted | (level << 9) | 1);
        root = parent;
        mode++;
    }
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 root | root_perms | (mode << 9) | 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    qtest_writeb(f.qts, target, 0x55);
    qtest_writeb(f.qts, wrong_target, 0x66);
    qtest_writeb(f.qts, DMA_TARGET, 0x77);

    if (restriction) {
        uint32_t flags = EVENT_IOPF | EVENT_PR | EVENT_PE;

        if (cached) {
            run_dma_address(&f, source, !write);
            g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
            qtest_writeb(f.qts, target, 0xa5);
        }
        run_dma_address(&f, source, write);
        if (write) {
            flags |= EVENT_RW;
            g_assert_cmphex(qtest_readb(f.qts, target), ==,
                            cached ? 0xa5 : 0x55);
        }
        g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
        g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG), ==, DEVICE_ID);
        g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG + 4), ==,
                        flags | DOMAIN_ID);
        g_assert_cmphex(qtest_readq(f.qts, EVENT_LOG + 8), ==, source);
    } else {
        run_dma_address(&f, source, write);
        if (write) {
            g_assert_cmphex(qtest_readb(f.qts, target), ==, 0);
        } else {
            run_dma_address(&f, sink, true);
            g_assert_cmphex(qtest_readb(f.qts, DMA_TARGET), ==, 0x55);
        }
        g_assert_cmphex(qtest_readb(f.qts, wrong_target), ==, 0x66);

        /* Reuse the cached leaf from a different offset in the same 4 KiB. */
        qtest_writeb(f.qts, target + 0x40, 0x7b);
        qtest_writeb(f.qts, wrong_target + 0x40, 0x88);
        run_dma_address(&f, source + 0x40, write);
        if (write) {
            g_assert_cmphex(qtest_readb(f.qts, target + 0x40), ==, 0);
        } else {
            run_dma_address(&f, sink, true);
            g_assert_cmphex(qtest_readb(f.qts, DMA_TARGET), ==, 0x7b);
        }
        g_assert_cmphex(qtest_readb(f.qts, wrong_target + 0x40), ==, 0x88);
        g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
    }
    fixture_teardown(&f);
}

static void trigger_dma(AMDVIFixture *f, bool write)
{
    send_command(f, (2ULL << 60) | DEVICE_ID);
    run_dma(f, write);
}

static void test_command_wrap(void)
{
    AMDVIFixture f;
    unsigned i;

    fixture_setup(&f);
    for (i = 0; i < 255; i++) {
        send_command(&f, (2ULL << 60) | DEVICE_ID);
    }
    send_command_data(&f, (1ULL << 60) | DMA_TARGET | 1, 0xfeedface);
    g_assert_cmphex(qtest_readq(f.qts, DMA_TARGET), ==, 0xfeedface);
    g_assert_cmphex(reg_read(&f, REG_CMDHEAD), ==, 0);
    fixture_teardown(&f);
}

static void test_global_invalidation(void)
{
    AMDVIFixture f;

    fixture_setup(&f);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, 3);
    qtest_writeb(f.qts, DMA_ADDR, 0x55);
    send_command(&f, 8ULL << 60);
    run_dma(&f, true);
    g_assert_cmphex(qtest_readb(f.qts, DMA_ADDR), ==, 0x55);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    fixture_teardown(&f);
}

static void test_enable_device(void)
{
    AMDVIFixture f;

    fixture_setup(&f);
    reg_write(&f, REG_CONTROL, 0);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, 3);
    qtest_writeb(f.qts, DMA_ADDR, 0x55);
    reg_write(&f, REG_CONTROL,
               CONTROL_ENABLE | CONTROL_EVTLOG | CONTROL_CMD);
    run_dma(&f, true);
    g_assert_cmphex(qtest_readb(f.qts, DMA_ADDR), ==, 0x55);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    fixture_teardown(&f);
}

static void test_mode_zero_notifier(const void *opaque)
{
    unsigned count = GPOINTER_TO_UINT(opaque);
    AMDVIFixture f;

    fixture_setup_device(&f, true);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, ENTRY_READ | 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_GVA_LO, DMA_ADDR);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_LEN, 0x400000);
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, (1U << count) - 1);

    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_BAD0), ==, 0);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP0), >, 0);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 1);
    if (count == 2) {
        g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_BAD1),
                         ==, 0);
        g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_MAP1),
                         >, 0);
        g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM1),
                         ==, 1);
    }
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    send_command(&f, 8ULL << 60);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 0);
    if (count == 2) {
        g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM1),
                         ==, 0);
    }
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, 0);
    fixture_teardown(&f);
}

static void test_mode_zero_notifier_transition(void)
{
    AMDVIFixture f;

    fixture_setup_device(&f, true);
    qtest_writeq(f.qts, PAGE_TABLE + (DMA_ADDR >> 12) * 8,
                 DMA_TARGET | ENTRY_RW | 1);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_GVA_LO, DMA_ADDR);
    qpci_io_writel(f.dev, f.bar, ITD_REG_DMA_LEN, 0x400000);
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, 1);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 3);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    send_command(&f, 8ULL << 60);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 0);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32, ENTRY_READ | 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 1);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 PAGE_TABLE | ENTRY_RW | (1 << 9) | 3);
    send_command(&f, (2ULL << 60) | DEVICE_ID);
    g_assert_cmpuint(qpci_io_readl(f.dev, f.bar, ITD_REG_NOTIFIER_PERM0),
                     ==, 3);
    qpci_io_writel(f.dev, f.bar, ITD_REG_NOTIFIER_CONTROL, 0);
    fixture_teardown(&f);
}

static void test_late_device(void)
{
    AMDVIFixture f;
    QPCIDevice *bridge;
    const uint16_t devid = 0x1300;

    fixture_setup(&f);
    bridge = qpci_device_find(f.bus, 3 << 3);
    qpci_config_writeb(bridge, PCI_SECONDARY_BUS, devid >> 8);
    qpci_config_writeb(bridge, PCI_SUBORDINATE_BUS, devid >> 8);
    reg_write(&f, REG_DEVTAB, DEVTAB | 0x27);
    qtest_writeq(f.qts, DEVTAB + devid * 32, (3 << 9) | 3);
    send_command(&f, (2ULL << 60) | devid);
    qtest_qmp_assert_success(f.qts, "{ 'execute': 'device_add', 'arguments': {"
                             "'driver': 'edu', 'id': 'late-edu',"
                             "'bus': 'late', 'addr': '0' } }");
    g_free(f.dev);
    f.dev = qpci_device_find(f.bus, devid);
    g_assert_nonnull(f.dev);
    f.bar = qpci_iomap(f.dev, 0, NULL);
    qpci_config_writew(bridge, PCI_MEMORY_BASE, f.bar.addr >> 16);
    qpci_config_writew(bridge, PCI_MEMORY_LIMIT, f.bar.addr >> 16);
    qpci_device_enable(bridge);
    qpci_device_enable(f.dev);
    qtest_writeb(f.qts, DMA_ADDR, 0x55);
    qpci_io_writeq(f.dev, f.bar, 0x80, 0x40000);
    qpci_io_writeq(f.dev, f.bar, 0x88, DMA_ADDR);
    qpci_io_writeq(f.dev, f.bar, 0x90, 1);
    qpci_io_writeq(f.dev, f.bar, 0x98, 3);
    qtest_clock_step(f.qts, 100000000);
    g_assert_cmphex(qtest_readb(f.qts, DMA_ADDR), ==, 0x55);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG), ==, devid);
    g_free(bridge);
    fixture_teardown(&f);
}

static void test_fault(const void *opaque)
{
    const FaultTest *test = opaque;
    AMDVIFixture f;
    uint32_t dword[4];
    unsigned i;

    fixture_setup(&f);
    if (test->dte_perms) {
        qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                     PAGE_TABLE | test->dte_perms | (1 << 9) | 3);
    }
    if (test->pde_perms) {
        qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                     PAGE_DIRECTORY | ENTRY_RW | (2 << 9) | 3);
        qtest_writeq(f.qts, PAGE_DIRECTORY,
                     PAGE_TABLE | test->pde_perms | (1 << 9) | 1);
    }
    qtest_writeq(f.qts, PAGE_TABLE + (DMA_ADDR >> 12) * 8, test->pte);
    trigger_dma(&f, test->write);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    for (i = 0; i < G_N_ELEMENTS(dword); i++) {
        dword[i] = qtest_readl(f.qts, EVENT_LOG + i * 4);
    }
    g_test_message("event: %08x %08x %08x %08x", dword[0], dword[1],
                   dword[2], dword[3]);

    /* Figure 56 and Table 57 specify each independently checked field. */
    if (test->check == 0) {
        g_assert_cmphex(dword[0], ==, DEVICE_ID);
    } else if (test->check == 1) {
        g_assert_cmphex(dword[1] & 0xffff, ==, DOMAIN_ID);
    } else {
        g_assert_cmphex(dword[1] & 0xffff0000, ==, test->expected);
    }
    g_assert_cmphex(dword[0], ==, DEVICE_ID);
    g_assert_cmphex(dword[1], ==, test->expected | DOMAIN_ID);
    g_assert_cmphex(dword[2], ==, DMA_ADDR);
    g_assert_cmphex(dword[3], ==, 0);
    fixture_teardown(&f);
}

static void test_status(const void *opaque)
{
    bool interrupt = GPOINTER_TO_INT(opaque);
    AMDVIFixture f;

    fixture_setup(&f);
    if (interrupt) {
        reg_write(&f, REG_CONTROL,
                   CONTROL_ENABLE | CONTROL_EVTLOG | CONTROL_EVTINT |
                   CONTROL_CMD);
    }
    trigger_dma(&f, false);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    g_assert_cmphex(reg_read(&f, REG_STATUS) & STATUS_EVTINT, ==,
                    STATUS_EVTINT);
    reg_write(&f, REG_STATUS, STATUS_EVTINT);
    g_assert_cmphex(reg_read(&f, REG_STATUS) & STATUS_EVTINT, ==, 0);
    trigger_dma(&f, true);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 32);
    g_assert_cmphex(reg_read(&f, REG_STATUS) & STATUS_EVTINT, ==,
                    STATUS_EVTINT);
    fixture_teardown(&f);
}

static void test_overflow(const void *opaque)
{
    bool interrupt = GPOINTER_TO_INT(opaque);
    AMDVIFixture f;

    fixture_setup(&f);
    if (interrupt) {
        reg_write(&f, REG_CONTROL,
                   CONTROL_ENABLE | CONTROL_EVTLOG | CONTROL_EVTINT |
                   CONTROL_CMD);
    }
    /* With head at 16 and tail at 0 there is no usable entry. */
    reg_write(&f, REG_EVTHEAD, 16);
    qtest_writeq(f.qts, EVENT_LOG, 0xfeedface);
    trigger_dma(&f, false);
    g_assert_cmphex(reg_read(&f, REG_STATUS) & STATUS_OVERFLOW, ==,
                    STATUS_OVERFLOW);
    g_assert_cmphex(reg_read(&f, REG_STATUS) & STATUS_EVTINT, ==, 0);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
    g_assert_cmphex(qtest_readq(f.qts, EVENT_LOG), ==, 0xfeedface);

    /* Overflow prevents further writes even after the buffer has room. */
    reg_write(&f, REG_EVTHEAD, 0);
    trigger_dma(&f, true);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
    g_assert_cmphex(qtest_readq(f.qts, EVENT_LOG), ==, 0xfeedface);
    fixture_teardown(&f);
}

static void test_capability(const void *opaque)
{
    unsigned test = GPOINTER_TO_UINT(opaque);
    QTestState *qts = qtest_init("-machine q35 -nodefaults -device amd-iommu");
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    QPCIDevice *dev = find_iommu(bus);
    uint8_t cap = qpci_find_capability(dev, 0x0f, 0);
    uint32_t low, high;
    const uint64_t relocated = 0x1fed84000ULL;

    g_assert(cap);
    low = qpci_config_readl(dev, cap + 4);
    high = qpci_config_readl(dev, cap + 8);
    switch (test) {
    case 0:
        g_assert_cmphex((qpci_config_readl(dev, cap) >> 19) & 0x1f, ==, 1);
        break;
    case 1: {
        uint8_t msi = qpci_find_capability(dev, PCI_CAP_ID_MSI, 0);

        g_assert(msi);
        g_assert_cmphex(qpci_find_capability(dev, PCI_CAP_ID_MSI, msi), ==, 0);
        break;
    }
    case 2:
        g_assert_cmphex(low & 1, ==, 0);
        g_assert_cmphex(qtest_readq(qts, IOMMU_BASE + REG_FEATURES), ==, 0);
        break;
    case 3:
        qpci_config_writeb(dev, cap + 4, low | 1);
        g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==, low | 1);
        g_assert_cmphex(qtest_readq(qts, IOMMU_BASE + REG_FEATURES), !=, 0);
        /* Once enabled, all subsequent Base register writes are ignored. */
        qpci_config_writel(dev, cap + 4, 0);
        qpci_config_writel(dev, cap + 8, 0xffffffff);
        g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==, low | 1);
        g_assert_cmphex(qpci_config_readl(dev, cap + 8), ==, high);
        break;
    case 4:
        qpci_config_writel(dev, cap + 8, relocated >> 32);
        qpci_config_writel(dev, cap + 4, (uint32_t)relocated | 1);
        g_assert_cmphex(qpci_config_readl(dev, cap + 8), ==, relocated >> 32);
        g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==,
                        (uint32_t)relocated | 1);
        g_assert_cmphex(qtest_readq(qts, relocated + REG_FEATURES), !=, 0);
        g_assert_cmphex(qtest_readq(qts, IOMMU_BASE + REG_FEATURES), ==, 0);
        break;
    case 5:
        qpci_config_writel(dev, cap + 4, low | 1);
        qtest_qmp_assert_success(qts, "{ 'execute': 'system_reset' }");
        qtest_qmp_eventwait(qts, "RESET");
        g_assert_cmphex(qpci_config_readl(dev, cap + 4) & 1, ==, 0);
        g_assert_cmphex(qtest_readq(qts, IOMMU_BASE + REG_FEATURES), ==, 0);
        qpci_config_writel(dev, cap + 4, low | 1);
        g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==, low | 1);
        break;
    default:
        g_assert_not_reached();
    }
    g_free(dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
}

static void test_base_migration(void)
{
    const char *args = "-machine q35 -nodefaults -m 16M "
                       "-device AMDVI-PCI,id=iommu,addr=1 "
                       "-device amd-iommu,pci-id=iommu";
    const uint64_t relocated = 0x1fed84000ULL;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *qts = qtest_init(args);
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    QPCIDevice *dev = find_iommu(bus);
    uint8_t cap = qpci_find_capability(dev, 0x0f, 0);
    int fd = g_file_open_tmp("amdvi-migration-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qpci_config_writel(dev, cap + 8, relocated >> 32);
    qpci_config_writel(dev, cap + 4, (uint32_t)relocated | 1);
    qtest_writeq(qts, relocated + REG_DEVTAB, DEVTAB | 0x25);
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'migrate-set-capabilities', 'arguments': {"
        "'capabilities': [ { 'capability': 'events', 'state': true } ] } }");
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    for (;;) {
        QDict *event = qtest_qmp_eventwait_ref(qts, "MIGRATION");
        const char *status = qdict_get_str(qdict_get_qdict(event, "data"),
                                           "status");
        bool completed = !strcmp(status, "completed");

        g_assert_cmpstr(status, !=, "failed");
        qobject_unref(event);
        if (completed) {
            break;
        }
    }
    g_free(dev);
    qpci_free_pc(bus);
    qtest_quit(qts);

    qts = qtest_initf("%s -incoming %s", args, uri);
    bus = qpci_new_pc(qts, NULL);
    dev = find_iommu(bus);
    cap = qpci_find_capability(dev, 0x0f, 0);
    g_assert_cmphex(qpci_config_readl(dev, cap + 8), ==, relocated >> 32);
    g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==,
                    (uint32_t)relocated | 1);
    g_assert_cmphex(qtest_readq(qts, relocated + REG_DEVTAB), ==,
                    DEVTAB | 0x25);
    qpci_config_writel(dev, cap + 4, 0);
    g_assert_cmphex(qpci_config_readl(dev, cap + 4), ==,
                    (uint32_t)relocated | 1);
    g_free(dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
    unlink(path);
}

static void test_mode_zero(const void *opaque)
{
    unsigned config = GPOINTER_TO_UINT(opaque);
    bool write = config & 1;
    unsigned perms = config >> 1;
    bool allowed = perms & (write ? 2 : 1);
    AMDVIFixture f;

    fixture_setup(&f);
    /* Table 7: Mode=000b keeps IR/IW access control and uses SPA=GPA. */
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 ((uint64_t)perms << 61) | 3);
    qtest_writeb(f.qts, DMA_ADDR, 0x55);
    trigger_dma(&f, write);
    if (allowed) {
        g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
        if (write) {
            g_assert_cmphex(qtest_readb(f.qts, DMA_ADDR), ==, 0);
        }
    } else {
        uint32_t flags = EVENT_IOPF | EVENT_PR | EVENT_PE;

        if (write) {
            flags |= EVENT_RW;
        }
        g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
        g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG), ==, DEVICE_ID);
        g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG + 4), ==,
                        flags | DOMAIN_ID);
        g_assert_cmphex(qtest_readq(f.qts, EVENT_LOG + 8), ==, DMA_ADDR);
        g_assert_cmphex(qtest_readb(f.qts, DMA_ADDR), ==, 0x55);
    }
    fixture_teardown(&f);
}

static void test_mode_zero_cached(const void *opaque)
{
    bool write = GPOINTER_TO_INT(opaque);
    AMDVIFixture f;
    uint32_t flags = EVENT_IOPF | EVENT_PR | EVENT_PE;

    fixture_setup(&f);
    qtest_writeq(f.qts, DEVTAB + DEVICE_ID * 32,
                 (write ? ENTRY_WRITE : ENTRY_READ) | 3);
    trigger_dma(&f, write);
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 0);
    /* Check the opposite access after caching the identity mapping. */
    trigger_dma(&f, !write);
    if (!write) {
        flags |= EVENT_RW;
    }
    g_assert_cmphex(reg_read(&f, REG_EVTTAIL), ==, 16);
    g_assert_cmphex(qtest_readl(f.qts, EVENT_LOG + 4), ==, flags | DOMAIN_ID);
    fixture_teardown(&f);
}

int main(int argc, char **argv)
{
    static const WalkTest walks[] = {
        { "skip-level-2", 3, 1, 0x1080, false },
        { "skip-levels-3-2", 4, 1, (1ULL << 39) | 0x1080, false },
        { "skip-levels-4-2", 5, 1, (1ULL << 48) | 0x1080, false },
        { "skip-levels-5-2", 6, 1, (1ULL << 57) | 0x1080, false },
        { "nonzero-skipped-bit-21", 3, 1, 0x201080, true },
        { "nonzero-skipped-bit-30", 4, 1, 0x40001080, true },
        { "same-level", 2, 2, 0x1080, true },
        { "ascending-level", 2, 3, 0x1080, true },
        { "nonleaf-level-1", 1, 1, 0x1080, true },
    };
    static const LeafTest leaves[] = {
        { "2m-aligned", 2, 0x400000, 0x1080, 0x401080, false },
        { "2m-unaligned", 2, 0x401000, 0x1080, 0x401080, true },
        { "1g-aligned", 3, 0, 0x1080, 0x1080, false },
        { "1g-unaligned-4k", 3, 0x1000, 0x1080, 0x1080, true },
        { "1g-unaligned-2m", 3, 0x200000, 0x1080, 0x1080, true },
        { "8k", 1, 0x400000 | (7 << 9), 0x1080, 0x401080, false },
        { "4m", 2, 0x5ff000 | (7 << 9), 0x201080, 0x601080, false },
        { "level-1-oversize", 1, 0x4ff000 | (7 << 9),
          0x1080, 0x401080, true },
        { "level-2-undersize", 2, 0x400000 | (7 << 9),
          0x1080, 0x401080, true },
        { "level-2-default-size", 2, 0x4ff000 | (7 << 9),
          0x1080, 0x401080, true },
    };
    static const FaultTest faults[] = {
        { "device-id", 0, false, EVENT_IOPF, 0 },
        { "domain-id", 0, false, EVENT_IOPF, 1 },
        { "read-not-present", 0, false, EVENT_IOPF, 2 },
        { "write-not-present", 0, true, EVENT_IOPF | EVENT_RW, 2 },
        { "read-permission", DMA_TARGET | ENTRY_WRITE | 1, false,
          EVENT_IOPF | EVENT_PR | EVENT_PE, 2 },
        { "write-permission", DMA_TARGET | ENTRY_READ | 1, true,
          EVENT_IOPF | EVENT_PR | EVENT_PE | EVENT_RW, 2 },
        { "dte-read-permission", DMA_TARGET | ENTRY_RW | 1, false,
          EVENT_IOPF | EVENT_PR | EVENT_PE, 2, ENTRY_WRITE },
        { "dte-write-permission", DMA_TARGET | ENTRY_RW | 1, true,
          EVENT_IOPF | EVENT_PR | EVENT_PE | EVENT_RW, 2, ENTRY_READ },
        { "pde-read-permission", DMA_TARGET | ENTRY_RW | 1, false,
          EVENT_IOPF | EVENT_PR | EVENT_PE, 2, 0, ENTRY_WRITE },
        { "pde-write-permission", DMA_TARGET | ENTRY_RW | 1, true,
          EVENT_IOPF | EVENT_PR | EVENT_PE | EVENT_RW, 2, 0, ENTRY_READ },
    };
    unsigned i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < G_N_ELEMENTS(walks); i++) {
        g_autofree char *path = g_strdup_printf("/amd-iommu/walk/%s",
                                                walks[i].name);

        qtest_add_data_func(path, &walks[i], test_walk_levels);
    }
    for (i = 0; i < G_N_ELEMENTS(leaves); i++) {
        g_autofree char *path = g_strdup_printf("/amd-iommu/leaf/%s",
                                                leaves[i].name);

        qtest_add_data_func(path, &leaves[i], test_leaf_encoding);
    }
    qtest_add_func("/amd-iommu/walk/notifier", test_walk_notifier);
    for (i = 0; i < G_N_ELEMENTS(faults); i++) {
        g_autofree char *path = g_strdup_printf("/amd-iommu/fault/%s",
                                                faults[i].name);
        qtest_add_data_func(path, &faults[i], test_fault);
    }
    qtest_add_data_func("/amd-iommu/status/masked", GINT_TO_POINTER(false),
                        test_status);
    qtest_add_data_func("/amd-iommu/status/enabled", GINT_TO_POINTER(true),
                        test_status);
    qtest_add_data_func("/amd-iommu/overflow/masked", GINT_TO_POINTER(false),
                        test_overflow);
    qtest_add_data_func("/amd-iommu/overflow/enabled", GINT_TO_POINTER(true),
                        test_overflow);
    qtest_add_data_func("/amd-iommu/capability/revision", GUINT_TO_POINTER(0),
                        test_capability);
    qtest_add_data_func("/amd-iommu/capability/msi", GUINT_TO_POINTER(1),
                        test_capability);
    qtest_add_data_func("/amd-iommu/capability/base-disabled",
                        GUINT_TO_POINTER(2), test_capability);
    qtest_add_data_func("/amd-iommu/capability/base-lock", GUINT_TO_POINTER(3),
                        test_capability);
    qtest_add_data_func("/amd-iommu/capability/base-relocate",
                        GUINT_TO_POINTER(4), test_capability);
    qtest_add_data_func("/amd-iommu/capability/base-reset", GUINT_TO_POINTER(5),
                        test_capability);
    qtest_add_func("/amd-iommu/capability/base-migration", test_base_migration);
    qtest_add_data_func("/amd-iommu/mode-zero/notifier-ram",
                        GUINT_TO_POINTER(1), test_mode_zero_notifier);
    qtest_add_data_func("/amd-iommu/mode-zero/notifier-overlap",
                        GUINT_TO_POINTER(2), test_mode_zero_notifier);
    qtest_add_func("/amd-iommu/mode-zero/notifier-transition",
                   test_mode_zero_notifier_transition);
    qtest_add_func("/amd-iommu/late-device", test_late_device);
    qtest_add_func("/amd-iommu/command-wrap", test_command_wrap);
    qtest_add_func("/amd-iommu/global-invalidation", test_global_invalidation);
    qtest_add_func("/amd-iommu/enable-device", test_enable_device);
    for (unsigned level = 2; level <= 3; level++) {
        static const char *const restrictions[] = {
            "allowed", "dte", "ancestor", "leaf",
        };

        for (unsigned r = 0; r < G_N_ELEMENTS(restrictions); r++) {
            for (unsigned access = 0; access < (r ? 4 : 2); access++) {
                unsigned config = (level << 4) | (r << 2) | access;
                g_autofree char *path = g_strdup_printf(
                    "/amd-iommu/large-page/level-%u/%s-%s%s", level,
                    restrictions[r], access & 1 ? "write" : "read",
                    access & 2 ? "-cached" : "");

                qtest_add_data_func(path, GUINT_TO_POINTER(config),
                                    test_large_page);
            }
        }
    }
    for (i = 0; i < 8; i++) {
        g_autofree char *path = g_strdup_printf("/amd-iommu/mode-zero/%s-%u",
                                                i & 1 ? "write" : "read",
                                                i >> 1);
        qtest_add_data_func(path, GUINT_TO_POINTER(i), test_mode_zero);
    }
    qtest_add_data_func("/amd-iommu/mode-zero/cached-read",
                        GINT_TO_POINTER(false), test_mode_zero_cached);
    qtest_add_data_func("/amd-iommu/mode-zero/cached-write",
                        GINT_TO_POINTER(true), test_mode_zero_cached);
    return g_test_run();
}
