/*
 * QTest testcase for eepro100 NIC
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
#include "qemu/units.h"
#include "hw/pci/pci_regs.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QEEPRO100 QEEPRO100;

struct QEEPRO100 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

typedef struct E100Model {
    const char *name;
    uint16_t device_id;
    uint8_t revision;
    uint64_t io_size;
    uint64_t flash_size;
    bool mem_prefetch;
} E100Model;

enum {
    E100_SCB_STATUS = 0,
    E100_SCB_COMMAND = 2,
    E100_SCB_POINTER = 4,
    E100_SCB_PORT = 8,
    E100_EEPROM_SEMAPHORE = 30,
    E100_CU_START = 0x10,
    E100_CU_RESUME = 0x20,
    E100_CU_HP_START = 0x30,
    E100_CU_STATSADDR = 0x40,
    E100_CU_LOAD_BASE = 0x60,
    E100_CU_DUMPSTATS = 0x70,
    E100_CU_STATIC_RESUME = 0xa0,
    E100_CU_HP_RESUME = 0xb0,
    E100_RU_RESERVED = 0x08,
    E100_CU_STATE_MASK = 0xc0,
    E100_CU_STATE_IDLE = 0x00,
    E100_CU_STATE_SUSPENDED = 0x40,
    E100_CU_STATE_LPQ_ACTIVE = 0x80,
    E100_CB_STATUS_COMPLETE = 0xa000,
    E100_CB_COMMAND_CONFIGURE = 2,
    E100_CB_COMMAND_S = 0x4000,
    E100_CB_COMMAND_EL = 0x8000,
    E100_CB_SIZE = 16,
    E100_STATS_STANDARD_SIZE = 64,
    E100_STATS_EXTENDED_SIZE = 76,
    E100_STATS_TCO_SIZE = 80,
    E100_STATS_COMPLETE_DUMP_RESET = 0xa007,
};

static const E100Model models[] = {
    { "i82550",  0x1229, 0x0e, 64, 128 * KiB, false },
    { "i82551",  0x1209, 0x0f, 64, 128 * KiB, false },
    { "i82557a", 0x1229, 0x01, 32, 1 * MiB,   true  },
    { "i82557b", 0x1229, 0x02, 32, 1 * MiB,   true  },
    { "i82557c", 0x1229, 0x03, 32, 1 * MiB,   true  },
    { "i82558a", 0x1229, 0x04, 32, 1 * MiB,   true  },
    { "i82558b", 0x1229, 0x05, 32, 1 * MiB,   true  },
    { "i82559a", 0x1229, 0x06, 64, 1 * MiB,   false },
    { "i82559b", 0x1229, 0x07, 64, 1 * MiB,   false },
    { "i82559c", 0x1229, 0x08, 64, 128 * KiB, false },
    { "i82559er", 0x1209, 0x09, 64, 128 * KiB, true },
    { "i82562",  0x1209, 0x0e, 64, 0,         false },
    { "i82801",  0x2449, 0x03, 64, 0,         false },
};

static void *eepro100_get_driver(void *obj, const char *interface)
{
    QEEPRO100 *eepro100 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &eepro100->dev;
    }

    fprintf(stderr, "%s not present in eepro100\n", interface);
    g_assert_not_reached();
}

static void *eepro100_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QEEPRO100 *eepro100 = g_new0(QEEPRO100, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&eepro100->dev, bus, addr);
    eepro100->obj.get_driver = eepro100_get_driver;

    return &eepro100->obj;
}

static void eepro100_bar_layout(void *obj, void *data,
                                QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    const E100Model *expected = data;
    QPCIBar bar;
    uint32_t value;
    uint64_t size;

    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==,
                    expected->device_id);
    g_assert_cmphex(qpci_config_readb(dev, PCI_REVISION_ID), ==,
                    expected->revision);
    value = qpci_config_readl(dev, PCI_BASE_ADDRESS_0);
    g_assert_cmphex(value & PCI_BASE_ADDRESS_SPACE_IO, ==, 0);
    g_assert_cmphex(value & PCI_BASE_ADDRESS_MEM_PREFETCH, ==,
                    expected->mem_prefetch ?
                    PCI_BASE_ADDRESS_MEM_PREFETCH : 0);

    bar = qpci_iomap(dev, 0, &size);
    g_assert_cmphex(size, ==, 4 * KiB);
    qpci_iounmap(dev, bar);

    value = qpci_config_readl(dev, PCI_BASE_ADDRESS_1);
    g_assert_cmphex(value & PCI_BASE_ADDRESS_SPACE_IO, ==,
                    PCI_BASE_ADDRESS_SPACE_IO);
    bar = qpci_iomap(dev, 1, &size);
    g_assert_cmphex(size, ==, expected->io_size);
    qpci_iounmap(dev, bar);

    if (expected->flash_size) {
        value = qpci_config_readl(dev, PCI_BASE_ADDRESS_2);
        g_assert_cmphex(value & PCI_BASE_ADDRESS_MEM_PREFETCH, ==, 0);
        bar = qpci_iomap(dev, 2, &size);
        g_assert_cmphex(size, ==, expected->flash_size);
        qpci_iounmap(dev, bar);
    } else {
        qpci_config_writel(dev, PCI_BASE_ADDRESS_2, UINT32_MAX);
        g_assert_cmphex(qpci_config_readl(dev, PCI_BASE_ADDRESS_2), ==, 0);
    }
}

static void eepro100_flash_aperture(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QPCIBar csr;
    QPCIBar flash;
    const uint32_t pointer = 0x12345678;

    qpci_device_enable(dev);
    csr = qpci_iomap(dev, 0, NULL);
    flash = qpci_iomap(dev, 2, NULL);

    qpci_io_writel(dev, csr, E100_SCB_POINTER, pointer);
    g_assert_cmphex(qpci_io_readl(dev, csr, E100_SCB_POINTER), ==, pointer);
    g_assert_cmphex(qpci_io_readl(dev, flash, E100_SCB_POINTER), ==, 0);

    qpci_io_writel(dev, flash, E100_SCB_POINTER, UINT32_MAX);
    qpci_io_writel(dev, flash, E100_SCB_PORT, 0);
    g_assert_cmphex(qpci_io_readl(dev, csr, E100_SCB_POINTER), ==, pointer);

    qpci_iounmap(dev, flash);
    qpci_iounmap(dev, csr);
}

static void eepro100_extended_commands(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;
    uint64_t cb_address;
    uint16_t cb[4] = {
        0,
        cpu_to_le16(E100_CB_COMMAND_S),
        0,
        0,
    };

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    g_assert_cmphex(qpci_io_readw(dev, bar, E100_EEPROM_SEMAPHORE), ==, 0);
    qpci_io_writew(dev, bar, E100_EEPROM_SEMAPHORE, BIT(7));
    g_assert_cmphex(qpci_io_readw(dev, bar, E100_EEPROM_SEMAPHORE), ==,
                    BIT(7));
    qpci_io_writew(dev, bar, E100_EEPROM_SEMAPHORE, 0);

    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_RU_RESERVED);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_COMMAND), ==, 0);

    cb_address = guest_alloc(alloc, sizeof(cb));
    g_assert_cmpuint(cb_address, <=, UINT32_MAX);
    qtest_memwrite(qts, cb_address, cb, sizeof(cb));
    qpci_io_writel(dev, bar, E100_SCB_POINTER, cb_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_START);
    qtest_memread(qts, cb_address, cb, sizeof(cb));
    g_assert_cmphex(le16_to_cpu(cb[0]), ==, E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    guest_free(alloc, cb_address);
}

static void eepro100_configure_stats_size(QTestState *qts,
                                          QPCIDevice *dev, QPCIBar bar,
                                          QGuestAllocator *alloc,
                                          uint8_t config_byte_6,
                                          uint64_t expected_size)
{
    uint8_t cb[8 + 22] = { 0 };
    uint8_t stats[E100_STATS_TCO_SIZE + sizeof(uint32_t)] = { 0 };
    uint64_t cb_address = guest_alloc(alloc, sizeof(cb));
    uint64_t stats_address = guest_alloc(alloc, sizeof(stats));
    uint32_t completion;

    g_assert_cmpuint(cb_address, <=, UINT32_MAX);
    g_assert_cmpuint(stats_address, <=, UINT32_MAX);

    stw_le_p(&cb[2], E100_CB_COMMAND_EL | E100_CB_COMMAND_CONFIGURE);
    cb[8] = 22;
    cb[8 + 6] = config_byte_6 | BIT(1);
    qtest_memwrite(qts, cb_address, cb, sizeof(cb));
    qpci_io_writel(dev, bar, E100_SCB_POINTER, cb_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);

    qtest_memread(qts, cb_address, cb, sizeof(cb));
    g_assert_cmphex(lduw_le_p(&cb[0]), ==, E100_CB_STATUS_COMPLETE);

    qtest_memwrite(qts, stats_address, stats, sizeof(stats));
    qpci_io_writel(dev, bar, E100_SCB_POINTER, stats_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_STATSADDR);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_DUMPSTATS);

    qtest_memread(qts, stats_address, stats, sizeof(stats));
    completion = ldl_le_p(&stats[expected_size]);
    g_assert_cmphex(completion, ==, E100_STATS_COMPLETE_DUMP_RESET);

    guest_free(alloc, stats_address);
    guest_free(alloc, cb_address);
}

static void eepro100_stats_size(void *obj, void *data,
                                QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    eepro100_configure_stats_size(qts, dev, bar, alloc,
                                  BIT(2) | BIT(5), E100_STATS_TCO_SIZE);
    eepro100_configure_stats_size(qts, dev, bar, alloc,
                                  BIT(5), E100_STATS_STANDARD_SIZE);
    eepro100_configure_stats_size(qts, dev, bar, alloc,
                                  0, E100_STATS_EXTENDED_SIZE);
}

static uint16_t eepro100_read_cb_status(QTestState *qts, uint64_t address)
{
    uint8_t status[sizeof(uint16_t)];

    qtest_memread(qts, address, status, sizeof(status));
    return lduw_le_p(status);
}

static void eepro100_wait_cb_complete(QTestState *qts, uint64_t address)
{
    unsigned i;

    for (i = 0; i < 1000; i++) {
        if (eepro100_read_cb_status(qts, address) ==
            E100_CB_STATUS_COMPLETE) {
            return;
        }
    }
    g_error("eepro100 CB at 0x%" PRIx64 " did not complete", address);
}

static void eepro100_write_cb_command(QTestState *qts, uint64_t address,
                                      uint16_t command)
{
    uint8_t value[sizeof(command)];

    stw_le_p(value, command);
    qtest_memwrite(qts, address + 2, value, sizeof(value));
}

static void eepro100_write_cb_link(QTestState *qts, uint64_t address,
                                   uint32_t link)
{
    uint8_t value[sizeof(link)];

    stl_le_p(value, link);
    qtest_memwrite(qts, address + 4, value, sizeof(value));
}

static void eepro100_cu_queue_offsets(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;
    uint8_t lp_suspend[E100_CB_SIZE] = { 0 };
    uint8_t lp_configure[8 + 22] = { 0 };
    uint8_t lp_poison[E100_CB_SIZE] = { 0 };
    uint8_t hp_suspend[E100_CB_SIZE] = { 0 };
    uint8_t hp_next[E100_CB_SIZE] = { 0 };
    uint8_t hp_poison[E100_CB_SIZE] = { 0 };
    uint8_t poison_after[sizeof(lp_poison)];
    uint64_t lp_suspend_address = guest_alloc(alloc, sizeof(lp_suspend));
    uint64_t lp_configure_address = guest_alloc(alloc, sizeof(lp_configure));
    uint64_t lp_poison_address = guest_alloc(alloc, sizeof(lp_poison));
    uint64_t hp_suspend_address = guest_alloc(alloc, sizeof(hp_suspend));
    uint64_t hp_next_address = guest_alloc(alloc, sizeof(hp_next));
    uint64_t hp_poison_address = guest_alloc(alloc, sizeof(hp_poison));

    g_assert_cmpuint(lp_suspend_address, <=, UINT32_MAX);
    g_assert_cmpuint(lp_configure_address, <=, UINT32_MAX);
    g_assert_cmpuint(lp_poison_address, <=, UINT32_MAX);
    g_assert_cmpuint(hp_suspend_address, <=, UINT32_MAX);
    g_assert_cmpuint(hp_next_address, <=, UINT32_MAX);
    g_assert_cmpuint(hp_poison_address, <=, UINT32_MAX);

    stw_le_p(&lp_suspend[2], E100_CB_COMMAND_S);
    stl_le_p(&lp_suspend[4], lp_configure_address);
    /* Once HPQ is in use, both queues must terminate with S, not EL. */
    stw_le_p(&lp_configure[2],
             E100_CB_COMMAND_S | E100_CB_COMMAND_CONFIGURE);
    lp_configure[8] = 22;
    stw_le_p(&lp_poison[2], E100_CB_COMMAND_S);

    stw_le_p(&hp_suspend[2], E100_CB_COMMAND_S);
    stl_le_p(&hp_suspend[4], hp_next_address);
    stw_le_p(&hp_next[2], E100_CB_COMMAND_S);
    stw_le_p(&hp_poison[2], E100_CB_COMMAND_S);

    qtest_memwrite(qts, lp_suspend_address, lp_suspend, sizeof(lp_suspend));
    qtest_memwrite(qts, lp_configure_address, lp_configure,
                   sizeof(lp_configure));
    qtest_memwrite(qts, lp_poison_address, lp_poison, sizeof(lp_poison));
    qtest_memwrite(qts, hp_suspend_address, hp_suspend, sizeof(hp_suspend));
    qtest_memwrite(qts, hp_next_address, hp_next, sizeof(hp_next));
    qtest_memwrite(qts, hp_poison_address, hp_poison, sizeof(hp_poison));

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_io_writel(dev, bar, E100_SCB_POINTER, lp_suspend_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);
    g_assert_cmphex(eepro100_read_cb_status(qts, lp_suspend_address), ==,
                    E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(eepro100_read_cb_status(qts, lp_configure_address), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    qpci_io_writel(dev, bar, E100_SCB_POINTER, hp_suspend_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_START);
    g_assert_cmphex(eepro100_read_cb_status(qts, hp_suspend_address), ==,
                    E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(eepro100_read_cb_status(qts, hp_next_address), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    /* Resume must leave a queue suspended while its previous S remains set. */
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(qts, lp_configure_address), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(qts, hp_next_address), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    /* Resume re-reads only S, not the previous CB's modified link. */
    eepro100_write_cb_link(qts, lp_suspend_address, lp_poison_address);
    eepro100_write_cb_command(qts, lp_suspend_address, 0);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(qts, lp_configure_address), ==,
                    E100_CB_STATUS_COMPLETE);
    qtest_memread(qts, lp_poison_address, poison_after,
                  sizeof(poison_after));
    g_assert_cmpmem(poison_after, sizeof(poison_after),
                    lp_poison, sizeof(lp_poison));
    g_assert_cmphex(eepro100_read_cb_status(qts, hp_next_address), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    eepro100_write_cb_link(qts, hp_suspend_address, hp_poison_address);
    eepro100_write_cb_command(qts, hp_suspend_address, 0);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(qts, hp_next_address), ==,
                    E100_CB_STATUS_COMPLETE);
    qtest_memread(qts, hp_poison_address, poison_after,
                  sizeof(poison_after));
    g_assert_cmpmem(poison_after, sizeof(poison_after),
                    hp_poison, sizeof(hp_poison));
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    guest_free(alloc, hp_poison_address);
    guest_free(alloc, hp_next_address);
    guest_free(alloc, hp_suspend_address);
    guest_free(alloc, lp_poison_address);
    guest_free(alloc, lp_configure_address);
    guest_free(alloc, lp_suspend_address);
}

static void eepro100_cu_unstarted_hp_resume(void *obj, void *data,
                                            QGuestAllocator *alloc)
{
    enum {
        GUARD_OFFSET = 0,
        LP_SUSPEND_OFFSET = 0x10,
        LP_NEXT_OFFSET = 0x20,
        ARENA_SIZE = 0x30,
    };
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;
    uint8_t arena[ARENA_SIZE] = { 0 };
    uint8_t guard_after[E100_CB_SIZE];
    uint64_t arena_address = guest_alloc(alloc, sizeof(arena));

    g_assert_cmpuint(arena_address + sizeof(arena), <=, UINT32_MAX);

    stw_le_p(&arena[GUARD_OFFSET + 2], E100_CB_COMMAND_EL);
    stw_le_p(&arena[LP_SUSPEND_OFFSET + 2], E100_CB_COMMAND_S);
    stl_le_p(&arena[LP_SUSPEND_OFFSET + 4], LP_NEXT_OFFSET);
    stw_le_p(&arena[LP_NEXT_OFFSET + 2], E100_CB_COMMAND_S);
    qtest_memwrite(qts, arena_address, arena, sizeof(arena));

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_io_writel(dev, bar, E100_SCB_POINTER, arena_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_LOAD_BASE);

    /* Neither queue has a valid continuation after reset/load-base. */
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_RESUME);
    qtest_memread(qts, arena_address + GUARD_OFFSET, guard_after,
                  sizeof(guard_after));
    g_assert_cmpmem(guard_after, sizeof(guard_after),
                    &arena[GUARD_OFFSET], sizeof(guard_after));
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_IDLE);

    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_RESUME);
    qtest_memread(qts, arena_address + GUARD_OFFSET, guard_after,
                  sizeof(guard_after));
    g_assert_cmpmem(guard_after, sizeof(guard_after),
                    &arena[GUARD_OFFSET], sizeof(guard_after));
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_IDLE);

    qpci_io_writel(dev, bar, E100_SCB_POINTER, LP_SUSPEND_OFFSET);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);
    g_assert_cmphex(eepro100_read_cb_status(qts,
                    arena_address + LP_SUSPEND_OFFSET), ==,
                    E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(eepro100_read_cb_status(qts,
                    arena_address + LP_NEXT_OFFSET), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    /* The HP queue has no saved continuation. */
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_RESUME);
    qtest_memread(qts, arena_address + GUARD_OFFSET, guard_after,
                  sizeof(guard_after));
    g_assert_cmpmem(guard_after, sizeof(guard_after),
                    &arena[GUARD_OFFSET], sizeof(guard_after));
    g_assert_cmphex(eepro100_read_cb_status(qts,
                    arena_address + LP_NEXT_OFFSET), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    guest_free(alloc, arena_address);
}

static void eepro100_cu_long_cbl(void *obj, void *data,
                                 QGuestAllocator *alloc)
{
    enum { CB_COUNT = 33 };
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    uint8_t cbl[CB_COUNT * E100_CB_SIZE] = { 0 };
    uint64_t cbl_address = guest_alloc(alloc, sizeof(cbl));
    QPCIBar bar;
    unsigned i;

    g_assert_cmpuint(cbl_address + sizeof(cbl), <=, UINT32_MAX);
    for (i = 0; i < CB_COUNT - 1; i++) {
        stl_le_p(&cbl[i * E100_CB_SIZE + 4],
                 cbl_address + (i + 1) * E100_CB_SIZE);
    }
    stw_le_p(&cbl[(CB_COUNT - 1) * E100_CB_SIZE + 2],
             E100_CB_COMMAND_S);
    qtest_memwrite(qts, cbl_address, cbl, sizeof(cbl));

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qpci_io_writel(dev, bar, E100_SCB_POINTER, cbl_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);

    eepro100_wait_cb_complete(qts,
                              cbl_address +
                              (CB_COUNT - 1) * E100_CB_SIZE);
    for (i = 0; i < CB_COUNT; i++) {
        g_assert_cmphex(eepro100_read_cb_status(
                            qts, cbl_address + i * E100_CB_SIZE), ==,
                        E100_CB_STATUS_COMPLETE);
    }
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    guest_free(alloc, cbl_address);
}

static void eepro100_cu_static_resume(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    enum {
        IDLE_HEAD_OFFSET = 0,
        IDLE_NEXT_OFFSET = 0x10,
        SUSPEND_HEAD_OFFSET = 0x20,
        SUSPEND_NEXT_OFFSET = 0x30,
        ARENA_SIZE = 0x40,
    };
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    uint8_t arena[ARENA_SIZE] = { 0 };
    uint64_t arena_address = guest_alloc(alloc, sizeof(arena));
    QPCIBar bar;

    g_assert_cmpuint(arena_address + sizeof(arena), <=, UINT32_MAX);
    stw_le_p(&arena[IDLE_HEAD_OFFSET + 2], E100_CB_COMMAND_EL);
    stl_le_p(&arena[IDLE_HEAD_OFFSET + 4],
             arena_address + IDLE_NEXT_OFFSET);
    stw_le_p(&arena[IDLE_NEXT_OFFSET + 2], E100_CB_COMMAND_S);
    stw_le_p(&arena[SUSPEND_HEAD_OFFSET + 2], E100_CB_COMMAND_S);
    stl_le_p(&arena[SUSPEND_HEAD_OFFSET + 4],
             arena_address + SUSPEND_NEXT_OFFSET);
    stw_le_p(&arena[SUSPEND_NEXT_OFFSET + 2], E100_CB_COMMAND_S);
    qtest_memwrite(qts, arena_address, arena, sizeof(arena));

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_io_writel(dev, bar, E100_SCB_POINTER,
                   arena_address + IDLE_HEAD_OFFSET);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);
    g_assert_cmphex(eepro100_read_cb_status(
                        qts, arena_address + IDLE_HEAD_OFFSET), ==,
                    E100_CB_STATUS_COMPLETE);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(
                        qts, arena_address + IDLE_NEXT_OFFSET), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_IDLE);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_STATIC_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(
                        qts, arena_address + IDLE_NEXT_OFFSET), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_IDLE);

    qpci_io_writel(dev, bar, E100_SCB_POINTER,
                   arena_address + SUSPEND_HEAD_OFFSET);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_STATIC_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(
                        qts, arena_address + SUSPEND_NEXT_OFFSET), ==, 0);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    eepro100_write_cb_command(qts,
                              arena_address + SUSPEND_HEAD_OFFSET, 0);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_STATIC_RESUME);
    g_assert_cmphex(eepro100_read_cb_status(
                        qts, arena_address + SUSPEND_NEXT_OFFSET), ==,
                    E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_SUSPENDED);

    guest_free(alloc, arena_address);
}

static void eepro100_cu_circular_reset(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    uint8_t cb[E100_CB_SIZE] = { 0 };
    uint8_t zero_status[sizeof(uint16_t)] = { 0 };
    uint64_t cb_address = guest_alloc(alloc, sizeof(cb));
    QPCIBar bar;

    g_assert_cmpuint(cb_address + sizeof(cb), <=, UINT32_MAX);
    stl_le_p(&cb[4], cb_address);
    qtest_memwrite(qts, cb_address, cb, sizeof(cb));

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    qpci_io_writel(dev, bar, E100_SCB_POINTER, cb_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);

    /* The active circular CBL leaves the control plane responsive. */
    qtest_qmp_assert_success(qts, "{'execute':'query-status'}");
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_LPQ_ACTIVE);

    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    qtest_memwrite(qts, cb_address, zero_status, sizeof(zero_status));
    qtest_qmp_assert_success(qts, "{'execute':'query-status'}");
    qtest_qmp_assert_success(qts, "{'execute':'query-status'}");
    g_assert_cmphex(eepro100_read_cb_status(qts, cb_address), ==, 0);

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    eepro100_wait_cb_complete(qts, cb_address);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_LPQ_ACTIVE);

    qpci_io_writel(dev, bar, E100_SCB_PORT, 2);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, E100_CU_STATE_IDLE);
    qtest_qmp_assert_success(qts, "{'execute':'query-status'}");

    guest_free(alloc, cb_address);
}

#ifndef _WIN32
static void eepro100_socket_cleanup(void *opaque)
{
    int *sockets = opaque;

    close(sockets[0]);
    qos_invalidate_command_line();
    close(sockets[1]);
    g_free(sockets);
}

static void *eepro100_socket_setup(GString *command, void *opaque)
{
    int *sockets = g_new(int, 2);

    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    g_string_append_printf(command, " -netdev socket,id=rx,fd=%d", sockets[1]);
    g_test_queue_destroy(eepro100_socket_cleanup, sockets);
    return sockets;
}

static void eepro100_receive_crc(void *obj, void *data, QGuestAllocator *alloc)
{
    QPCIDevice *dev = &((QEEPRO100 *)obj)->dev;
    QTestState *qts = dev->bus->qts;
    int *sockets = data;
    QPCIBar bar;
    uint64_t config = guest_alloc(alloc, 32);
    uint64_t rfd = guest_alloc(alloc, 128);
    uint8_t packet[64];
    uint8_t cb[32] = { 0 };
    uint8_t received[80];

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    stl_be_p(packet, 60);
    for (unsigned int i = 0; i < 60; i++) {
        packet[i + 4] = i;
    }
    memset(packet + 4, 0xff, 6);
    packet[16] = 0;
    packet[17] = 20; /* IEEE 802.3 payload length, followed by padding. */
    for (unsigned int variant = 0; variant < 4; variant++) {
        unsigned int length = variant & 1 ? 34 : 60;
        unsigned int total = length + (variant & 2 ? 4 : 0);
        uint8_t header[16] = { 0 };

        stw_le_p(cb + 2, E100_CB_COMMAND_EL | E100_CB_COMMAND_CONFIGURE);
        cb[8] = 22;
        cb[8 + 15] = 1; /* promiscuous */
        cb[8 + 18] = (variant & 1) | ((variant & 2) << 1);
        qtest_memwrite(qts, config, cb, sizeof(cb));
        qpci_io_writel(dev, bar, E100_SCB_POINTER, config);
        qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_START);
        g_assert_cmphex(qtest_readw(qts, config), ==, 0xa000);
        stw_le_p(header + 2, 0x8000);
        stw_le_p(header + 14, 80);
        qtest_memset(qts, rfd, 0xa5, 128);
        qtest_memwrite(qts, rfd, header, 16);
        qpci_io_writel(dev, bar, E100_SCB_POINTER, rfd);
        qpci_io_writeb(dev, bar, E100_SCB_COMMAND, 1); /* RU_START */
        g_assert_cmpint(write(sockets[0], packet, sizeof(packet)),
                        ==, sizeof(packet));
        for (unsigned int tries = 0; !(qtest_readw(qts, rfd) & 0x8000) &&
             tries < 1000; tries++) {
            qtest_qmp_assert_success(qts, "{'execute':'query-status'}");
        }
        g_assert_cmphex(qtest_readw(qts, rfd) & 0xa080, ==, 0xa000);
        g_assert_cmpuint(qtest_readw(qts, rfd + 12), ==, total);
        qtest_memread(qts, rfd + 16, received, sizeof(received));
        g_assert_cmpmem(received, length, packet + 4, length);
        if (variant & 2) {
            g_assert_cmphex((uint32_t)ldl_le_p(received + length),
                            ==, 0x8a151885);
        }
        g_assert_cmphex(received[total], ==, 0xa5);
    }
    guest_free(alloc, config);
    guest_free(alloc, rfd);
}
#endif

static void eepro100_register_nodes(void)
{
    int i;
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    QOSGraphEdgeOptions ia64_opts = { .extra_device_opts = "addr=07.0" };

    add_qpci_address(&ia64_opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(7, 0) });
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });
    for (i = 0; i < ARRAY_SIZE(models); i++) {
        QOSGraphTestOptions bar_opts = { .arg = (void *)&models[i] };

        qos_node_create_driver(models[i].name, eepro100_create);
        qos_node_consumes(models[i].name, "pci-bus", &opts);
        qos_node_consumes(models[i].name, "ia64-pci-bus", &ia64_opts);
        qos_node_produces(models[i].name, "pci-device");
        qos_add_test("bar-layout", models[i].name, eepro100_bar_layout,
                     &bar_opts);
    }

#ifndef _WIN32
    QOSGraphTestOptions receive_opts = {
        .before = eepro100_socket_setup,
        .edge.extra_device_opts = "netdev=rx",
    };

    qos_add_test("receive-crc", "i82550", eepro100_receive_crc, &receive_opts);
    qos_add_test("receive-crc", "i82559c", eepro100_receive_crc, &receive_opts);
#endif

    qos_add_test("flash-aperture", "i82559er",
                 eepro100_flash_aperture, NULL);
    qos_add_test("extended-commands", "i82559c",
                 eepro100_extended_commands, NULL);
    qos_add_test("stats-size", "i82559c", eepro100_stats_size, NULL);
    qos_add_test("cu-queue-offsets", "i82559c",
                 eepro100_cu_queue_offsets, NULL);
    qos_add_test("cu-unstarted-hp-resume", "i82559c",
                 eepro100_cu_unstarted_hp_resume, NULL);
    qos_add_test("cu-long-cbl", "i82559c",
                 eepro100_cu_long_cbl, NULL);
    qos_add_test("cu-static-resume", "i82559c",
                 eepro100_cu_static_resume, NULL);
    qos_add_test("cu-circular-reset", "i82559c",
                 eepro100_cu_circular_reset, NULL);
}

libqos_init(eepro100_register_nodes);
