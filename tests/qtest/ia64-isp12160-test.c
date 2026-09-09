/*
 * IA-64 ISP12160 integration qtests
 *
 * Exercises mailbox, queue, and SCSI models with deterministic activation
 * tokens and a fixed topology.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <glib/gstdio.h>

#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/ia64/ia64_isp12160_test.h"
#include "hw/pci/pci.h"
#include "hw/scsi/isp12160.h"
#include "hw/scsi/isp12160_iocb.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"

#define PID_IOREGSEL                    0x00
#define PID_IOWIN                       0x10
#define PID_RTE_BASE                    0x10
#define PID_RTE_DELIVERY_STATUS         BIT(12)
#define PID_RTE_TRIGGER_LEVEL           BIT(15)

#define MAILBOX_COUNT                8U
#define MAILBOX_BYTES                (MAILBOX_COUNT * 2U)
#define MAILBOX_BH_POLL_LIMIT                10000U
#define MAILBOX_TEST_VECTOR                  0x64U
#define SCSI_QUEUE_COUNT                  64U
#define SCSI_REQUEST_DMA                  UINT64_C(0x00600000)
#define SCSI_RESPONSE_DMA                 UINT64_C(0x00700000)
#define SCSI_DATA_DMA                     UINT64_C(0x00800000)

G_STATIC_ASSERT(IA64_ISP12160_TEST_ROOT_INDEX == 1);
G_STATIC_ASSERT(IA64_ISP12160_TEST_PCI_BUS == 0x01);
G_STATIC_ASSERT(IA64_ISP12160_TEST_INTERRUPT_PIN == 1);
G_STATIC_ASSERT(IA64_ISP12160_TEST_PID_PIN == 20);
G_STATIC_ASSERT(ISP12160_QEMU_TOKEN_WORDS * 2 == 8);

static const uint16_t mailbox_qemu_token_words[ISP12160_QEMU_TOKEN_WORDS] = {
    ISP12160_QEMU_MAILBOX_TOKEN_WORD0,
    ISP12160_QEMU_MAILBOX_TOKEN_WORD1,
    ISP12160_QEMU_MAILBOX_TOKEN_WORD2,
    ISP12160_QEMU_MAILBOX_TOKEN_WORD3,
};

static const uint16_t queue_qemu_token_words[ISP12160_QEMU_TOKEN_WORDS] = {
    ISP12160_QEMU_QUEUE_TOKEN_WORD0,
    ISP12160_QEMU_QUEUE_TOKEN_WORD1,
    ISP12160_QEMU_QUEUE_TOKEN_WORD2,
    ISP12160_QEMU_QUEUE_TOKEN_WORD3,
};

static const uint16_t scsi_qemu_token_words[ISP12160_QEMU_TOKEN_WORDS] = {
    ISP12160_QEMU_SCSI_TOKEN_WORD0,
    ISP12160_QEMU_SCSI_TOKEN_WORD1,
    ISP12160_QEMU_SCSI_TOKEN_WORD2,
    ISP12160_QEMU_SCSI_TOKEN_WORD3,
};

static QTestState *mailbox_start_with_options(const char *options)
{
    return qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 2G -nodefaults "
        "-display none -net none -device %s,id=%s %s",
        TYPE_IA64_ISP12160_MAILBOX_QTEST,
        IA64_ISP12160_MAILBOX_QTEST_ID, options ?: "");
}

static QTestState *mailbox_start(void)
{
    return mailbox_start_with_options(NULL);
}

static QTestState *queue_start_with_options(const char *options)
{
    return qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 2G -nodefaults "
        "-display none -net none -device %s,id=%s %s",
        TYPE_IA64_ISP12160_QUEUE_QTEST,
        IA64_ISP12160_QUEUE_QTEST_ID, options ?: "");
}

static QTestState *queue_start(void)
{
    return queue_start_with_options(NULL);
}

static QTestState *scsi_start_with_options(const char *options)
{
    return qtest_initf(
        "-machine ia64-vpc,nvram=none -S -smp 1 -m 2G -nodefaults "
        "-display none -net none -device %s,id=%s %s",
        TYPE_IA64_ISP12160_SCSI_QTEST,
        IA64_ISP12160_SCSI_QTEST_ID, options ?: "");
}

static QTestState *scsi_start(void)
{
    return scsi_start_with_options(NULL);
}

static uint32_t mailbox_config_address(unsigned reg)
{
    return UINT32_C(0x80000000) |
           IA64_ISP12160_TEST_PCI_BUS << 16 |
           PCI_DEVFN(IA64_ISP12160_TEST_PCI_SLOT,
                     IA64_ISP12160_TEST_PCI_FUNCTION) << 8 |
           (reg & 0xfc);
}

static void mailbox_config_select(QTestState *qts, unsigned reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA,
                 mailbox_config_address(reg));
}

static uint8_t mailbox_config_readb(QTestState *qts, unsigned reg)
{
    mailbox_config_select(qts, reg);
    return qtest_readb(qts,
                       IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint16_t mailbox_config_readw(QTestState *qts, unsigned reg)
{
    mailbox_config_select(qts, reg);
    return qtest_readw(qts,
                       IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint32_t mailbox_config_readl(QTestState *qts, unsigned reg)
{
    mailbox_config_select(qts, reg);
    return qtest_readl(qts,
                       IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static void mailbox_config_writew(QTestState *qts, unsigned reg, uint16_t value)
{
    mailbox_config_select(qts, reg);
    qtest_writew(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static void mailbox_config_writel(QTestState *qts, unsigned reg, uint32_t value)
{
    mailbox_config_select(qts, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static uint64_t mailbox_io_pa(uint16_t port)
{
    uint64_t offset = ((uint64_t)(port >> 2) << 12) | (port & 0xfffU);

    return IA64_I2000_460GX_TEST_LEGACY_IO_BASE + offset;
}

static uint16_t mailbox_pio_readw(QTestState *qts, unsigned reg)
{
    return qtest_readw(qts,
                       mailbox_io_pa(IA64_ISP12160_TEST_IO_BASE + reg));
}

static void mailbox_pio_writew(QTestState *qts, unsigned reg, uint16_t value)
{
    qtest_writew(qts,
                 mailbox_io_pa(IA64_ISP12160_TEST_IO_BASE + reg), value);
}

static uint16_t mailbox_mmio_readw(QTestState *qts, unsigned reg)
{
    return qtest_readw(qts, IA64_ISP12160_TEST_MMIO_BASE + reg);
}

static void mailbox_mmio_writew(QTestState *qts, unsigned reg, uint16_t value)
{
    qtest_writew(qts, IA64_ISP12160_TEST_MMIO_BASE + reg, value);
}

static void mailbox_pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN, value);
}

static uint32_t mailbox_pid_read(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    return qtest_readl(qts,
                       IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN);
}

static uint32_t mailbox_pid_rte_low(unsigned pin)
{
    return PID_RTE_BASE + pin * 2;
}

static uint32_t mailbox_pid_rte_high(unsigned pin)
{
    return mailbox_pid_rte_low(pin) + 1;
}

static void mailbox_route_inta_to_pending_pid(QTestState *qts)
{
    const unsigned pin = IA64_ISP12160_TEST_PID_PIN;

    /* An absent destination keeps the level-delivery status set. */
    mailbox_pid_write(qts, mailbox_pid_rte_high(pin), 0x0f000000);
    mailbox_pid_write(qts, mailbox_pid_rte_low(pin),
                 MAILBOX_TEST_VECTOR | PID_RTE_TRIGGER_LEVEL);
}

static void mailbox_program_bars(QTestState *qts, bool bus_master)
{
    uint16_t command = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    if (bus_master) {
        command |= PCI_COMMAND_MASTER;
    }
    mailbox_config_writel(qts, PCI_BASE_ADDRESS_0,
                     IA64_ISP12160_TEST_IO_BASE);
    mailbox_config_writel(qts, PCI_BASE_ADDRESS_1,
                     IA64_ISP12160_TEST_MMIO_BASE);
    mailbox_config_writew(qts, PCI_COMMAND, command);
}

static uint16_t mailbox_wait_completion(QTestState *qts)
{
    unsigned int i;

    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        uint16_t status = mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0);

        if (status != ISP12160_MBS_BUSY) {
            return status;
        }
    }
    g_error("ISP12160 mailbox BH did not complete in %u reads",
            MAILBOX_BH_POLL_LIMIT);
    return UINT16_MAX;
}

static uint16_t mailbox_command(QTestState *qts,
                                   const uint16_t mailbox[MAILBOX_COUNT])
{
    unsigned int i;

    for (i = 0; i < MAILBOX_COUNT; i++) {
        mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + i * 2, mailbox[i]);
    }
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_SET_HOST_INT);
    return mailbox_wait_completion(qts);
}

static void mailbox_assert_completion_latched(QTestState *qts, uint16_t status)
{
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0), ==,
                    status);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_SEMAPHORE), ==,
                    ISP12160_SEMAPHORE_LOCK);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);
}

static void mailbox_ack_completion(QTestState *qts)
{
    mailbox_mmio_writew(qts, ISP12160_REG_SEMAPHORE, 0);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_RISC_INT);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_HOST_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_SEMAPHORE), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    (ISP12160_ISTATUS_RISC_INT |
                     ISP12160_ISTATUS_PCI_INT), ==, 0);
}

static void mailbox_token_bytes(uint8_t *bytes)
{
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(mailbox_qemu_token_words); i++) {
        bytes[i * 2] = mailbox_qemu_token_words[i];
        bytes[i * 2 + 1] = mailbox_qemu_token_words[i] >> 8;
    }
}

static void mailbox_write_token(QTestState *qts, uint64_t address, bool valid)
{
    uint8_t bytes[sizeof(mailbox_qemu_token_words)];

    mailbox_token_bytes(bytes);
    if (!valid) {
        bytes[0] ^= 0xff;
    }
    qtest_memwrite(qts, address, bytes, sizeof(bytes));
}

static void queue_write_token(QTestState *qts, uint64_t address, bool valid)
{
    uint8_t bytes[sizeof(queue_qemu_token_words)];
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(queue_qemu_token_words); i++) {
        bytes[i * 2] = queue_qemu_token_words[i];
        bytes[i * 2 + 1] = queue_qemu_token_words[i] >> 8;
    }
    if (!valid) {
        bytes[0] ^= 0xff;
    }
    qtest_memwrite(qts, address, bytes, sizeof(bytes));
}

static void scsi_write_token(QTestState *qts, uint64_t address, bool valid)
{
    uint8_t bytes[sizeof(scsi_qemu_token_words)];
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(scsi_qemu_token_words); i++) {
        bytes[i * 2] = scsi_qemu_token_words[i];
        bytes[i * 2 + 1] = scsi_qemu_token_words[i] >> 8;
    }
    if (!valid) {
        bytes[0] ^= 0xff;
    }
    qtest_memwrite(qts, address, bytes, sizeof(bytes));
}

static void mailbox_assert_token_memory(QTestState *qts, uint64_t address)
{
    uint8_t expected[sizeof(mailbox_qemu_token_words)];
    uint8_t actual[sizeof(mailbox_qemu_token_words)];

    mailbox_token_bytes(expected);
    qtest_memread(qts, address, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
}

static void mailbox_load_mailboxes(uint16_t mailbox[MAILBOX_COUNT],
                              uint16_t command, uint16_t risc_address,
                              uint64_t dma_address, uint16_t words)
{
    memset(mailbox, 0, MAILBOX_BYTES);
    mailbox[0] = command;
    mailbox[1] = risc_address;
    mailbox[2] = dma_address >> 16;
    mailbox[3] = dma_address;
    mailbox[4] = words;
    mailbox[6] = dma_address >> 48;
    mailbox[7] = dma_address >> 32;
}

static void queue_activate(QTestState *qts)
{
    uint16_t mailbox[MAILBOX_COUNT];

    queue_write_token(qts, IA64_ISP12160_TEST_TOKEN_DMA, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      ISP12160_QEMU_ACTIVATION_RISC_ADDR,
                      IA64_ISP12160_TEST_TOKEN_DMA,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = ISP12160_QEMU_ACTIVATION_RISC_ADDR;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
}

static void scsi_activate(QTestState *qts)
{
    uint16_t mailbox[MAILBOX_COUNT];

    scsi_write_token(qts, IA64_ISP12160_TEST_TOKEN_DMA, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      ISP12160_QEMU_ACTIVATION_RISC_ADDR,
                      IA64_ISP12160_TEST_TOKEN_DMA,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = ISP12160_QEMU_ACTIVATION_RISC_ADDR;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
}

static void queue_mailboxes(uint16_t mailbox[MAILBOX_COUNT],
                               uint16_t command, uint16_t count,
                               uint16_t index, uint64_t dma_address)
{
    memset(mailbox, 0, MAILBOX_BYTES);
    mailbox[0] = command;
    mailbox[1] = count;
    mailbox[2] = dma_address >> 16;
    mailbox[3] = dma_address;
    if (command == ISP12160_MBC_INIT_REQUEST_QUEUE ||
        command == ISP12160_MBC_INIT_REQUEST_QUEUE_A64) {
        mailbox[4] = index;
    } else {
        mailbox[5] = index;
    }
    mailbox[6] = dma_address >> 48;
    mailbox[7] = dma_address >> 32;
}

static void scsi_prepare_queues(QTestState *qts, uint16_t request_index,
                              uint16_t response_index)
{
    uint16_t mailbox[MAILBOX_COUNT];

    mailbox_program_bars(qts, true);
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);
    scsi_activate(qts);
    qtest_memset(qts, SCSI_REQUEST_DMA, 0,
                 SCSI_QUEUE_COUNT * ISP12160_QUEUE_ENTRY_BYTES);
    qtest_memset(qts, SCSI_RESPONSE_DMA, 0,
                 SCSI_QUEUE_COUNT * ISP12160_QUEUE_ENTRY_BYTES);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       SCSI_QUEUE_COUNT, request_index, SCSI_REQUEST_DMA);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_RESPONSE_QUEUE_A64,
                       SCSI_QUEUE_COUNT, response_index, SCSI_RESPONSE_DMA);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
}

static void scsi_build_no_data(uint8_t entry[ISP12160_QUEUE_ENTRY_BYTES],
                             uint32_t handle, uint8_t channel,
                             uint8_t target, uint8_t lun)
{
    memset(entry, 0, ISP12160_QUEUE_ENTRY_BYTES);
    entry[0] = ISP12160_IOCB_COMMAND_A64_TYPE;
    entry[1] = 1;
    stl_le_p(entry + 4, handle);
    entry[8] = lun;
    entry[9] = target | (channel << 7);
    stw_le_p(entry + 10, 6);
    entry[20] = 0x00; /* TEST UNIT READY */
}

static void scsi_build_verify_no_data(
    uint8_t entry[ISP12160_QUEUE_ENTRY_BYTES], uint32_t handle,
    uint8_t target)
{
    scsi_build_no_data(entry, handle, 0, target, 0);
    stw_le_p(entry + ISP12160_IOCB_A64_CDB_LENGTH_OFFSET, 10);
    stw_le_p(entry + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET,
             ISP12160_IOCB_CONTROL_DATA_UNKNOWN);
    entry[ISP12160_IOCB_A64_CDB_OFFSET] = 0x2f; /* VERIFY(10) */
    entry[ISP12160_IOCB_A64_CDB_OFFSET + 8] = 1;
}

static void scsi_build_marker(uint8_t entry[ISP12160_QUEUE_ENTRY_BYTES],
                              uint8_t channel, uint8_t target, uint8_t lun,
                              uint8_t modifier)
{
    memset(entry, 0, ISP12160_QUEUE_ENTRY_BYTES);
    entry[0] = ISP12160_IOCB_MARKER_TYPE;
    entry[1] = 1;
    entry[ISP12160_IOCB_MARKER_LUN_OFFSET] = lun;
    entry[ISP12160_IOCB_MARKER_TARGET_OFFSET] = target | (channel << 7);
    entry[ISP12160_IOCB_MARKER_MODIFIER_OFFSET] = modifier;
}

static uint16_t scsi_wait_live_index(QTestState *qts, unsigned int mailbox,
                                   uint16_t previous)
{
    unsigned int i;

    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        uint16_t value = mailbox_mmio_readw(
            qts, ISP12160_REG_MAILBOX0 + mailbox * 2);

        if (value != previous) {
            return value;
        }
    }
    g_error("ISP12160 SCSI live mailbox %u did not advance", mailbox);
    return UINT16_MAX;
}

static void scsi_fill_response_ring(QTestState *qts)
{
    uint8_t requests[SCSI_QUEUE_COUNT - 1]
                    [ISP12160_QUEUE_ENTRY_BYTES];
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(requests); i++) {
        scsi_build_no_data(requests[i], 0x80000000U + i, 0, 15, 0);
    }
    qtest_memwrite(qts, SCSI_REQUEST_DMA, requests, sizeof(requests));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8,
                   SCSI_QUEUE_COUNT - 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==,
                    SCSI_QUEUE_COUNT - 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    SCSI_QUEUE_COUNT - 1);
}

static void scsi_read_status(QTestState *qts, uint16_t index,
                           uint8_t entry[ISP12160_QUEUE_ENTRY_BYTES])
{
    qtest_memread(qts,
                  SCSI_RESPONSE_DMA +
                  (uint64_t)index * ISP12160_QUEUE_ENTRY_BYTES,
                  entry, ISP12160_QUEUE_ENTRY_BYTES);
}

static void scsi_set_segment(uint8_t *entry, size_t offset,
                           uint64_t address, uint32_t length)
{
    stq_le_p(entry + offset, address);
    stl_le_p(entry + offset + 8, length);
}

static void scsi_build_rw_three_segments(
    uint8_t entries[2][ISP12160_QUEUE_ENTRY_BYTES], uint32_t handle,
    bool write, uint64_t data_base)
{
    memset(entries, 0, 2 * ISP12160_QUEUE_ENTRY_BYTES);
    entries[0][0] = ISP12160_IOCB_COMMAND_A64_TYPE;
    entries[0][1] = 2;
    stl_le_p(entries[0] + 4, handle);
    entries[0][9] = 3;
    stw_le_p(entries[0] + 10, 10);
    stw_le_p(entries[0] + 12,
             write ? ISP12160_IOCB_CONTROL_DATA_TO_DEVICE :
                     ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE);
    stw_le_p(entries[0] + 18, 3);
    entries[0][20] = write ? 0x2a : 0x28; /* WRITE(10) / READ(10) */
    entries[0][27] = 0;
    entries[0][28] = 1;
    scsi_set_segment(entries[0], 40, data_base, 128);
    scsi_set_segment(entries[0], 52, data_base + 0x1000, 128);

    entries[1][0] = ISP12160_IOCB_CONTINUE_A64_TYPE;
    entries[1][1] = 1;
    scsi_set_segment(entries[1], 4, data_base + 0x2000, 256);
}

static void scsi_build_rw_32(uint8_t entry[ISP12160_QUEUE_ENTRY_BYTES],
                             uint32_t handle, bool write,
                             uint32_t data_address)
{
    memset(entry, 0, ISP12160_QUEUE_ENTRY_BYTES);
    entry[0] = ISP12160_IOCB_COMMAND_TYPE;
    entry[1] = 1;
    stl_le_p(entry + 4, handle);
    entry[9] = 3;
    stw_le_p(entry + 10, 10);
    stw_le_p(entry + 12,
             write ? ISP12160_IOCB_CONTROL_DATA_TO_DEVICE :
                     ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE);
    stw_le_p(entry + 18, 1);
    entry[20] = write ? 0x2a : 0x28;
    entry[28] = 1;
    stl_le_p(entry + ISP12160_IOCB_SEGMENT0_OFFSET, data_address);
    stl_le_p(entry + ISP12160_IOCB_SEGMENT0_OFFSET + 4, 512);
}

static void scsi_consume_disk_unit_attention(QTestState *qts, uint8_t target)
{
    uint8_t warmup[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_build_no_data(warmup, 0x01020304, 0, target, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, warmup, sizeof(warmup));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_RISC_INT);
    qtest_memset(qts, SCSI_RESPONSE_DMA, 0, ISP12160_QUEUE_ENTRY_BYTES);
}

static void test_pci_bars_alias_and_reset(void)
{
    QTestState *qts = mailbox_start();
    uint32_t io_bar_mask = ~(uint32_t)(ISP12160_REG_SIZE - 1);
    uint32_t mmio_bar_mask = ~(uint32_t)(ISP12160_MMIO_BAR_SIZE - 1);

    g_assert_cmphex(mailbox_config_readw(qts, PCI_VENDOR_ID), ==,
                    ISP12160_PCI_VENDOR_ID);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_DEVICE_ID), ==,
                    ISP12160_PCI_DEVICE_ID);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_STORAGE_SCSI);
    g_assert_cmphex(mailbox_config_readb(qts, PCI_REVISION_ID), ==, 0x06);
    g_assert_cmphex(mailbox_config_readb(qts, PCI_CLASS_PROG), ==, 0);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    ISP12160_PCI_VENDOR_ID);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_SUBSYSTEM_ID), ==, 0x0007);
    g_assert_cmphex(mailbox_config_readb(qts, PCI_INTERRUPT_PIN), ==,
                    IA64_ISP12160_TEST_INTERRUPT_PIN);
    g_assert_cmphex(mailbox_config_readb(qts, PCI_HEADER_TYPE), ==,
                    PCI_HEADER_TYPE_NORMAL);

    mailbox_config_writew(qts, PCI_COMMAND, 0);
    mailbox_config_writel(qts, PCI_BASE_ADDRESS_0, UINT32_MAX);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    io_bar_mask | PCI_BASE_ADDRESS_SPACE_IO);
    mailbox_config_writel(qts, PCI_BASE_ADDRESS_1, UINT32_MAX);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    mmio_bar_mask);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_ROM_ADDRESS), ==, 0);
    mailbox_config_writel(qts, PCI_ROM_ADDRESS, UINT32_MAX);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_ROM_ADDRESS), ==, 0);

    mailbox_program_bars(qts, true);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    IA64_ISP12160_TEST_IO_BASE |
                    PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    IA64_ISP12160_TEST_MMIO_BASE);

    /* Both PCI BARs are aliases of the same QEMU 16-bit register file. */
    mailbox_pio_writew(qts, ISP12160_REG_CFG1, 0x1234);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_CFG1), ==, 0x1234);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 7 * 2, 0xa55a);
    g_assert_cmphex(mailbox_pio_readw(qts,
                                ISP12160_REG_MAILBOX0 + 7 * 2), ==,
                    0xa55a);

    mailbox_pio_writew(qts, ISP12160_REG_ICTRL, ISP12160_ICTRL_RESET);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ICTRL), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_CFG1), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0), ==,
                    ISP12160_MBS_FIRMWARE_ALIVE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==,
                    ISP12160_PRODUCT_ID_1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==,
                    ISP12160_PRODUCT_ID_2A);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==,
                    ISP12160_PRODUCT_ID_3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    ISP12160_PRODUCT_ID_4);

    qtest_quit(qts);
}

static void test_mailbox_and_qemu_token(void)
{
    const uint64_t dma = IA64_ISP12160_TEST_TOKEN_DMA;
    const uint16_t risc = IA64_ISP12160_TEST_TOKEN_RISC_ADDR;
    const uint16_t boundary_risc = UINT16_C(0xfffc);
    QTestState *qts = mailbox_start();
    uint16_t mailbox[MAILBOX_COUNT] = { 0 };
    uint16_t status;
    unsigned int i;

    mailbox_program_bars(qts, true);

    mailbox[0] = ISP12160_MBC_MAILBOX_TEST;
    mailbox[1] = 0xaaaa;
    mailbox[2] = 0x5555;
    mailbox[3] = 0xaa55;
    mailbox[4] = 0x55aa;
    mailbox[5] = 0xa5a5;
    mailbox[6] = 0x5a5a;
    mailbox[7] = 0x2525;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    for (i = 1; i < MAILBOX_COUNT; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + i * 2), ==,
                        mailbox[i]);
    }
    mailbox_assert_completion_latched(qts, status);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = UINT16_MAX;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_INVALID_COMMAND);
    mailbox_ack_completion(qts);

    mailbox_write_token(qts, dma, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, risc, dma,
                      ISP12160_QEMU_TOKEN_WORDS - 1);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    mailbox_program_bars(qts, false);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, risc, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_HOST_INTERFACE_ERR);
    mailbox_ack_completion(qts);
    mailbox_program_bars(qts, true);

    /* The 2 GiB PCI hole is outside the DMA aperture. */
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, risc,
                      IA64_I2000_460GX_TEST_RAM_SIZE,
                      ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_HOST_INTERFACE_ERR);
    mailbox_ack_completion(qts);

    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM, risc,
                      UINT64_MAX - 3, ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, 0xfffd, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    /* 0xfffc is the last valid four-word RISC-RAM start address. */
    mailbox_write_token(qts, dma, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      boundary_risc, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = boundary_risc - 1;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = boundary_risc;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==, 1);
    mailbox_ack_completion(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_PAUSE_RISC);
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_ERR);
    mailbox_ack_completion(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_RELEASE_RISC);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==, 1);
    mailbox_ack_completion(qts);

    /* A rejected replacement must invalidate the previously valid token. */
    mailbox_write_token(qts, dma, false);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, risc, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_TEST_FAILED);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    mailbox[1] = boundary_risc;
    status = mailbox_command(qts, mailbox);
    g_assert_cmphex(status, ==, ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    qtest_quit(qts);
}

static void test_queue_token_and_execute_iocb_boundary(void)
{
    const uint64_t dma = IA64_ISP12160_TEST_TOKEN_DMA;
    uint16_t mailbox[MAILBOX_COUNT];
    QTestState *qts = queue_start();

    mailbox_program_bars(qts, true);

    /* QUEUE rejects the MAILBOX activation token. */
    mailbox_write_token(qts, dma, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      ISP12160_QEMU_ACTIVATION_RISC_ADDR, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_TEST_FAILED);
    mailbox_ack_completion(qts);

    queue_activate(qts);
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==, 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==, 0);
    mailbox_ack_completion(qts);

    /* The mailbox command set rejects EXECUTE_IOCB. */
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_EXECUTE_IOCB;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_INVALID_COMMAND);
    mailbox_ack_completion(qts);
    qtest_quit(qts);

    /* Conversely, MAILBOX must not acquire QUEUE capability through its token. */
    qts = mailbox_start();
    mailbox_program_bars(qts, true);
    queue_write_token(qts, dma, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      ISP12160_QEMU_ACTIVATION_RISC_ADDR, dma,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_TEST_FAILED);
    mailbox_ack_completion(qts);
    qtest_quit(qts);
}

static void test_queue_init_and_reset(void)
{
    const uint64_t request_base = UINT64_C(0x00600010);
    const uint64_t response_base = UINT64_C(0x1234000000700000);
    const uint64_t overflowing_base =
        UINT64_MAX & ~(uint64_t)(ISP12160_QUEUE_BASE_ALIGNMENT - 1);
    uint16_t mailbox[MAILBOX_COUNT];
    QTestState *qts = queue_start();

    mailbox_program_bars(qts, true);
    queue_activate(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES - 1, 0, request_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES, 0, request_base + 1);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES,
                       ISP12160_QUEUE_MIN_ENTRIES, request_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES, 0, overflowing_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE,
                       ISP12160_QUEUE_MIN_ENTRIES, 0,
                       UINT32_MAX &
                       ~(uint64_t)(ISP12160_QUEUE_BASE_ALIGNMENT - 1));
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    mailbox_program_bars(qts, false);
    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES, 0, request_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_HOST_INTERFACE_ERR);
    mailbox_ack_completion(qts);
    mailbox_program_bars(qts, true);

    /* The 32-bit command ignores stale high mailbox words. */
    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE,
                       SCSI_QUEUE_COUNT, 3, request_base);
    mailbox[6] = UINT16_MAX;
    mailbox[7] = UINT16_MAX;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    /* A64 uses mb6:mb7 as bits 63:32 and accepts the maximum queue count. */
    queue_mailboxes(mailbox, ISP12160_MBC_INIT_RESPONSE_QUEUE_A64,
                       ISP12160_QUEUE_MAX_ENTRIES,
                       ISP12160_QUEUE_MAX_ENTRIES - 1, response_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    /* mb4/5 expose the latched result until completion is acknowledged. */
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_MAILBOX_TEST;
    mailbox[4] = 0x55aa;
    mailbox[5] = 0xa5a5;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    0x55aa);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    0xa5a5);
    mailbox_ack_completion(qts);

    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    ISP12160_QUEUE_MAX_ENTRIES - 1);

    /* Writes update the host side only; QUEUE has no IOCB consumer/producer. */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 4);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    ISP12160_QUEUE_MAX_ENTRIES - 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_SEMAPHORE), ==, 0);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES, 0, request_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_ERR);
    mailbox_ack_completion(qts);

    qtest_system_reset(qts);
    mailbox_program_bars(qts, true);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    ISP12160_PRODUCT_ID_4);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       ISP12160_QUEUE_MIN_ENTRIES, 0, request_base);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_ERR);
    mailbox_ack_completion(qts);
    qtest_quit(qts);
}

static void mailbox_assert_irq_state(QTestState *qts)
{
    mailbox_assert_completion_latched(qts, ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(mailbox_pid_read(
                        qts, mailbox_pid_rte_low(
                                 IA64_ISP12160_TEST_PID_PIN)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
}

static void mailbox_assert_irq_cleared(QTestState *qts)
{
    g_assert_cmphex(mailbox_config_readw(qts, PCI_STATUS) &
                    PCI_STATUS_INTERRUPT, ==, 0);
    g_assert_cmphex(mailbox_pid_read(
                        qts, mailbox_pid_rte_low(
                                 IA64_ISP12160_TEST_PID_PIN)) &
                    PID_RTE_DELIVERY_STATUS, ==, 0);
}

static void test_irq_semaphore_and_pid20(void)
{
    QTestState *qts = mailbox_start();
    uint16_t mailbox[MAILBOX_COUNT] = { 0 };

    mailbox_program_bars(qts, true);
    mailbox_route_inta_to_pending_pid(qts);

    /* Neither interrupt-control enable bit is sufficient alone. */
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT);
    mailbox[0] = ISP12160_MBC_NOP;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_completion_latched(qts, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_irq_cleared(qts);
    mailbox_ack_completion(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_RISC);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_completion_latched(qts, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_irq_cleared(qts);
    mailbox_ack_completion(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);

    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_PCI_INT, ==, 0);
    mailbox_assert_irq_state(qts);

    /* A machine reset must deassert an already asserted PCI INTx. */
    qtest_system_reset(qts);
    mailbox_assert_irq_cleared(qts);

    /* Reprogram PCI/PID state after reset and assert INTx again. */
    mailbox_program_bars(qts, true);
    mailbox_route_inta_to_pending_pid(qts);
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_irq_state(qts);
    mailbox_ack_completion(qts);
    mailbox_assert_irq_cleared(qts);
    qtest_quit(qts);
}

static void test_reset_risc_cancels_pending_mailbox(void)
{
    QTestState *qts = mailbox_start();

    mailbox_program_bars(qts, true);
    mailbox_route_inta_to_pending_pid(qts);
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0, ISP12160_MBC_NOP);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 2, 0xdead);

    /* The qtest device performs SET_HOST_INT then RESET_RISC before its BH. */
    qtest_set_irq_in(qts, IA64_ISP12160_MAILBOX_QTEST_QOM_PATH,
                     IA64_ISP12160_TEST_GPIO_CANCEL_MAILBOX, 0, 1);
    qtest_set_irq_in(qts, IA64_ISP12160_MAILBOX_QTEST_QOM_PATH,
                     IA64_ISP12160_TEST_GPIO_CANCEL_MAILBOX, 0, 0);

    g_assert_cmphex(mailbox_wait_completion(qts), ==,
                    ISP12160_MBS_FIRMWARE_ALIVE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==,
                    ISP12160_PRODUCT_ID_1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==,
                    ISP12160_PRODUCT_ID_2A);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==,
                    ISP12160_PRODUCT_ID_3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    ISP12160_PRODUCT_ID_4);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_SEMAPHORE), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS), ==, 0);
    mailbox_assert_irq_cleared(qts);

    qtest_quit(qts);
}

static void mailbox_wait_for_migration_complete(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 120 * G_TIME_SPAN_SECOND;

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

static void mailbox_migration_file_cleanup(gpointer opaque)
{
    char *path = opaque;

    g_unlink(path);
    g_free(path);
}

static void scsi_wait_for_runstate(QTestState *qts, const char *expected)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-status'}");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, expected)) {
            qobject_unref(result);
            return;
        }
        if (g_get_monotonic_time() >= deadline) {
            g_error("runstate did not reach '%s' (last status '%s')",
                    expected, status);
        }
        qobject_unref(result);
        g_usleep(1000);
    }
}

static bool scsi_block_io_error_event(QTestState *qts, const char *name,
                                    QDict *event, void *opaque)
{
    bool *seen = opaque;

    (void)qts;
    (void)event;
    if (!strcmp(name, "BLOCK_IO_ERROR")) {
        *seen = true;
        return true;
    }
    return false;
}

static void scsi_wait_for_active_read_io_error(QTestState *qts,
                                               bool *block_io_error_seen,
                                               uint16_t request_consumer,
                                               uint16_t response_producer)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-status'}");
        const char *runstate = qdict_get_str(result, "status");
        uint16_t current_request_consumer = mailbox_mmio_readw(
            qts, ISP12160_REG_MAILBOX0 + 8);
        uint16_t current_response_producer = mailbox_mmio_readw(
            qts, ISP12160_REG_MAILBOX0 + 10);

        if (!strcmp(runstate, "io-error")) {
            qobject_unref(result);
            g_assert_true(*block_io_error_seen);
            return;
        }
        if (current_request_consumer != request_consumer ||
            current_response_producer != response_producer) {
            uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

            scsi_read_status(qts, response_producer, status);
            g_error("unexpected queue progress before BLOCK_IO_ERROR: "
                    "runstate=%s "
                    "request-consumer=%u response-producer=%u event=%d "
                    "type=0x%02x handle=0x%08x scsi=0x%04x "
                    "completion=0x%04x residual=%u",
                    runstate, current_request_consumer,
                    current_response_producer,
                    *block_io_error_seen, status[0],
                    (uint32_t)ldl_le_p(status + 4),
                    lduw_le_p(status + 8), lduw_le_p(status + 10),
                    (uint32_t)ldl_le_p(status + 20));
        }
        if (g_get_monotonic_time() >= deadline) {
            g_error("active READ did not raise BLOCK_IO_ERROR: runstate=%s "
                    "request-consumer=%u response-producer=%u event=%d",
                    runstate, current_request_consumer,
                    current_response_producer,
                    *block_io_error_seen);
        }
        qobject_unref(result);
        g_usleep(1000);
    }
}

static void mailbox_prepare_running_asserted(QTestState *qts)
{
    uint16_t mailbox[MAILBOX_COUNT];

    mailbox_program_bars(qts, true);
    mailbox_route_inta_to_pending_pid(qts);
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);
    mailbox_write_token(qts, IA64_ISP12160_TEST_TOKEN_DMA, true);

    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM,
                      IA64_ISP12160_TEST_TOKEN_RISC_ADDR,
                      IA64_ISP12160_TEST_TOKEN_DMA,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = IA64_ISP12160_TEST_TOKEN_RISC_ADDR;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_irq_state(qts);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_PAUSE_RISC);
    mailbox_assert_irq_state(qts);
}

static void test_sequential_file_migration(void)
{
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-mailbox-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    QTestState *qts;
    uint16_t mailbox[MAILBOX_COUNT] = { 0 };
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    qts = mailbox_start();
    mailbox_prepare_running_asserted(qts);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    /* Destroy the source before constructing the deferred destination. */
    qts = mailbox_start_with_options("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);

    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    IA64_ISP12160_TEST_IO_BASE |
                    PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(mailbox_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    IA64_ISP12160_TEST_MMIO_BASE);
    g_assert_cmphex(mailbox_config_readw(qts, PCI_COMMAND) &
                    (PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                     PCI_COMMAND_MASTER), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                    PCI_COMMAND_MASTER);
    mailbox_assert_token_memory(qts, IA64_ISP12160_TEST_TOKEN_DMA);
    mailbox_assert_irq_state(qts);

    mailbox_ack_completion(qts);
    mailbox_assert_irq_cleared(qts);

    /* Paused state is migrated together with the asserted completion. */
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_ERR);
    mailbox_ack_completion(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_RELEASE_RISC);
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==, 1);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    mailbox[1] = IA64_ISP12160_TEST_TOKEN_RISC_ADDR;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    qtest_quit(qts);
}

static void queue_prepare_queues_asserted(QTestState *qts)
{
    uint16_t mailbox[MAILBOX_COUNT];

    mailbox_program_bars(qts, true);
    mailbox_route_inta_to_pending_pid(qts);
    mailbox_mmio_writew(qts, ISP12160_REG_ICTRL,
                   ISP12160_ICTRL_ENABLE_INT |
                   ISP12160_ICTRL_ENABLE_RISC);
    queue_activate(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       255, 3, UINT64_C(0x0000000100600000));
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_RESPONSE_QUEUE,
                       63, 7, UINT64_C(0x00700000));
    mailbox[6] = 0xa5a5;
    mailbox[7] = 0x5a5a;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);

    /* Migrate both host-owned indices independently of the live read side. */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 11);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 13);
    mailbox_assert_irq_state(qts);
}

static void test_queue_sequential_file_migration(void)
{
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-queue-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    uint16_t mailbox[MAILBOX_COUNT];
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    qts = queue_start();
    queue_prepare_queues_asserted(qts);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    qts = queue_start_with_options("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);

    mailbox_assert_irq_state(qts);
    mailbox_ack_completion(qts);
    mailbox_assert_irq_cleared(qts);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==, 7);

    /* A migrated valid queue cannot be replaced. */
    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                       255, 0, UINT64_C(0x0000000100600000));
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_ERR);
    mailbox_ack_completion(qts);

    qtest_system_reset(qts);
    mailbox_program_bars(qts, true);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    ISP12160_PRODUCT_ID_4);
    qtest_quit(qts);
}

static void test_scsi_token_variant_separation(void)
{
    QTestState *qts = scsi_start();
    uint16_t mailbox[MAILBOX_COUNT];

    mailbox_program_bars(qts, true);
    queue_write_token(qts, IA64_ISP12160_TEST_TOKEN_DMA, true);
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM_A64_ROM,
                      ISP12160_QEMU_ACTIVATION_RISC_ADDR,
                      IA64_ISP12160_TEST_TOKEN_DMA,
                      ISP12160_QEMU_TOKEN_WORDS);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_TEST_FAILED);
    mailbox_ack_completion(qts);

    scsi_activate(qts);
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==, 2);
    mailbox_ack_completion(qts);
    qtest_quit(qts);
}

static void test_scsi_native_firmware(void)
{
    static const uint16_t first[] = { 1, 1, 1, 1, 1, 1 };
    static const uint16_t second[] = { 2, 2, 2, 2, UINT16_C(0xfff3) };
    uint16_t mailbox[MAILBOX_COUNT];
    QTestState *qts = scsi_start();

    mailbox_program_bars(qts, true);
    qtest_memwrite(qts, SCSI_DATA_DMA, first, sizeof(first));
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, 0x1000,
                           SCSI_DATA_DMA, G_N_ELEMENTS(first));
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_VERIFY_CHECKSUM;
    mailbox[1] = 0x1000;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_PARAM_ERR);
    mailbox_ack_completion(qts);

    qtest_memwrite(qts, SCSI_DATA_DMA, second, sizeof(second));
    mailbox_load_mailboxes(mailbox, ISP12160_MBC_LOAD_RAM, 0x1006,
                           SCSI_DATA_DMA, G_N_ELEMENTS(second));
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_EXECUTE_FIRMWARE;
    mailbox[1] = 0x1000;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_ABOUT_FIRMWARE;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==,
                    ISP12160_NATIVE_FIRMWARE_MAJOR);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==,
                    ISP12160_NATIVE_FIRMWARE_MINOR);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==,
                    ISP12160_NATIVE_FIRMWARE_PATCH);
    mailbox_ack_completion(qts);

    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_SET_PCI_CONTROL;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_REQUEST_QUEUE_A64,
                    64, 0, SCSI_REQUEST_DMA + 0x10);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);

    queue_mailboxes(mailbox, ISP12160_MBC_INIT_RESPONSE_QUEUE_A64,
                    16, 0, SCSI_RESPONSE_DMA + 0x10);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
    qtest_quit(qts);
}

static void test_scsi_mailbox_lock_backpressure(void)
{
    QTestState *qts = scsi_start();
    uint16_t mailbox[MAILBOX_COUNT] = {
        [0] = ISP12160_MBC_NOP,
        [1] = 0x1234,
    };

    mailbox_program_bars(qts, true);
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_assert_completion_latched(qts, ISP12160_MBS_COMMAND_COMPLETE);

    /*
     * Both the mailbox write and command strobe must be ignored until the
     * latched completion is acknowledged.  Otherwise stale output words
     * become an INVALID_COMMAND request in a later BH.
     */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0,
                   ISP12160_MBC_MAILBOX_TEST);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 2, 0xbeef);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_SET_HOST_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==,
                    0x1234);
    mailbox_assert_completion_latched(qts, ISP12160_MBS_COMMAND_COMPLETE);

    mailbox_ack_completion(qts);
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = ISP12160_MBC_MAILBOX_TEST;
    g_assert_cmphex(mailbox_command(qts, mailbox), ==,
                    ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
    qtest_quit(qts);
}

static void scsi_wait_index_equals(QTestState *qts, unsigned int mailbox,
                                 uint16_t expected)
{
    unsigned int i;

    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        if (mailbox_mmio_readw(qts,
                          ISP12160_REG_MAILBOX0 + mailbox * 2) == expected) {
            return;
        }
    }
    g_error("ISP12160 SCSI live mailbox %u did not reach %u",
            mailbox, expected);
}

static void test_scsi_dual_channel_no_data(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,node-name=scsid0 "
        "-blockdev driver=null-co,read-zeroes=on,node-name=scsid1 "
        "-device scsi-hd,drive=scsid0,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=2,lun=0 "
        "-device scsi-hd,drive=scsid1,bus=isp12160-scsi.0,"
        "channel=1,scsi-id=2,lun=0");
    uint8_t requests[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t statuses[2][ISP12160_QUEUE_ENTRY_BYTES];
    bool saw_first = false;
    bool saw_second = false;
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(requests[0], 0x10203040, 0, 2, 0);
    scsi_build_no_data(requests[1], 0x50607080, 1, 2, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, requests, sizeof(requests));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);
    scsi_wait_index_equals(qts, 5, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 2);

    scsi_read_status(qts, 0, statuses[0]);
    scsi_read_status(qts, 1, statuses[1]);
    for (i = 0; i < G_N_ELEMENTS(statuses); i++) {
        uint32_t handle = ldl_le_p(statuses[i] + 4);

        g_assert_cmphex(statuses[i][0], ==, ISP12160_IOCB_STATUS_TYPE);
        g_assert_cmphex(lduw_le_p(statuses[i] + 10), ==,
                        ISP12160_IOCB_CS_COMPLETE);
        saw_first |= handle == 0x10203040;
        saw_second |= handle == 0x50607080;
    }
    g_assert_true(saw_first);
    g_assert_true(saw_second);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 2);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_RISC_INT);
    qtest_quit(qts);
}

static void test_scsi_response_full_backpressure(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t sentinel[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t actual[ISP12160_QUEUE_ENTRY_BYTES];
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    scsi_fill_response_ring(qts);
    memset(sentinel, 0xa5, sizeof(sentinel));
    qtest_memwrite(qts,
                   SCSI_RESPONSE_DMA +
                   (SCSI_QUEUE_COUNT - 1) * ISP12160_QUEUE_ENTRY_BYTES,
                   sentinel, sizeof(sentinel));

    scsi_build_no_data(request, 0xabcdef01, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA +
                   (SCSI_QUEUE_COUNT - 1) * ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 0);
    for (i = 0; i < 100; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + 8), ==,
                        SCSI_QUEUE_COUNT - 1);
    }
    qtest_memread(qts,
                  SCSI_RESPONSE_DMA +
                  (SCSI_QUEUE_COUNT - 1) * ISP12160_QUEUE_ENTRY_BYTES,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), sentinel, sizeof(sentinel));

    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);
    g_assert_cmphex(scsi_wait_live_index(
                        qts, 5, SCSI_QUEUE_COUNT - 1), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 0);
    scsi_read_status(qts, SCSI_QUEUE_COUNT - 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0xabcdef01));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

static void test_scsi_response_during_irq_ack(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint16_t command;

    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(request, 0x11223344, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);

    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
    scsi_build_no_data(request, 0x55667788, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);
    mailbox_mmio_writew(qts, ISP12160_REG_SEMAPHORE,
                        ISP12160_SEMAPHORE_LOCK);
    mailbox_config_writew(qts, PCI_COMMAND, command);

    mailbox_mmio_writew(qts, ISP12160_REG_SEMAPHORE, 0);
    scsi_wait_index_equals(qts, 4, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);
    scsi_read_status(qts, 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x55667788));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

static void test_scsi_response_after_queue_snapshot(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint16_t command;
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(request, 0x11223344, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);

    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
    scsi_build_no_data(request, 0x55667788, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);
    mailbox_config_writew(qts, PCI_COMMAND, command);

    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        scsi_read_status(qts, 1, status);
        if (status[0] == ISP12160_IOCB_STATUS_TYPE &&
            ldl_le_p(status + 4) == UINT32_C(0x55667788)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, MAILBOX_BH_POLL_LIMIT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);

    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    2);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 2);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==, 0);

    scsi_build_no_data(request, 0x99aabbcc, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 2 * ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        scsi_read_status(qts, 2, status);
        if (status[0] == ISP12160_IOCB_STATUS_TYPE &&
            ldl_le_p(status + 4) == UINT32_C(0x99aabbcc)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, MAILBOX_BH_POLL_LIMIT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_RESET_RISC);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==, 0);
    qtest_quit(qts);
}

static void test_scsi_response_irq_migration(void)
{
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-response-irq-migration.XXXXXX",
        g_get_tmp_dir());
    g_autofree char *uri = NULL;
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    QTestState *qts;
    unsigned int i;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    qts = scsi_start();
    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(request, 0x52495251, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    for (i = 0; i < MAILBOX_BH_POLL_LIMIT; i++) {
        scsi_read_status(qts, 0, status);
        if (status[0] == ISP12160_IOCB_STATUS_TYPE &&
            ldl_le_p(status + 4) == UINT32_C(0x52495251)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, MAILBOX_BH_POLL_LIMIT);

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    qts = scsi_start_with_options("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==,
                    ISP12160_ISTATUS_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==,
                    1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS) &
                    ISP12160_ISTATUS_RISC_INT, ==, 0);
    qtest_quit(qts);
}

static void test_scsi_bus_master_reenable(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint16_t command;
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
    scsi_build_no_data(request, 0x33445566, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    for (i = 0; i < 100; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + 8), ==, 0);
    }

    mailbox_config_writew(qts, PCI_COMMAND, command);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 1);
    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x33445566));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

static void test_scsi_index_update_validation(void)
{
    QTestState *qts = scsi_start();
    uint8_t requests[10][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint16_t command;
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    for (i = 0; i < G_N_ELEMENTS(requests); i++) {
        scsi_build_no_data(requests[i], 0x90000000U + i, 0, 15, 0);
    }
    qtest_memwrite(qts, SCSI_REQUEST_DMA, requests, sizeof(requests));

    /* Hold two published entries in the request ring. */
    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);

    /* Reject a rewind/over-capacity wrap leap and an out-of-range producer. */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 0);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, SCSI_QUEUE_COUNT);
    for (i = 0; i < 100; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + 8), ==, 0);
    }

    mailbox_config_writew(qts, PCI_COMMAND, command);
    scsi_wait_index_equals(qts, 5, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 2);

    /* Only two responses exist, so consuming through index ten is invalid. */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 10);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, SCSI_QUEUE_COUNT);

    /* A rejected consumer update leaves room for all eight new responses. */
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 10);
    scsi_wait_index_equals(qts, 5, 10);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 10);

    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x90000000));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    scsi_read_status(qts, 9, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x90000009));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

static void test_scsi_sg_read_write_ring_wrap(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "node-name=scsirw "
        "-device scsi-hd,drive=scsirw,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t warmup[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t pattern[512];
    uint8_t actual[512];
    unsigned int i;

    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = i ^ 0x5a;
    }
    qtest_memwrite(qts, SCSI_DATA_DMA, pattern, 128);
    qtest_memwrite(qts, SCSI_DATA_DMA + 0x1000, pattern + 128, 128);
    qtest_memwrite(qts, SCSI_DATA_DMA + 0x2000, pattern + 256, 256);

    /* Consume the test SCSI disk's initial power-on unit attention. */
    scsi_prepare_queues(qts, SCSI_QUEUE_COUNT - 2, SCSI_QUEUE_COUNT - 2);
    scsi_build_no_data(warmup, 0x01020304, 0, 3, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA +
                   (SCSI_QUEUE_COUNT - 2) * ISP12160_QUEUE_ENTRY_BYTES,
                   warmup, sizeof(warmup));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8,
                   SCSI_QUEUE_COUNT - 1);
    g_assert_cmphex(scsi_wait_live_index(
                        qts, 5, SCSI_QUEUE_COUNT - 2), ==,
                    SCSI_QUEUE_COUNT - 1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10,
                   SCSI_QUEUE_COUNT - 1);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_RISC_INT);

    scsi_build_rw_three_segments(entries, 0x13572468, true, SCSI_DATA_DMA);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA +
                   (SCSI_QUEUE_COUNT - 1) * ISP12160_QUEUE_ENTRY_BYTES,
                   entries[0], sizeof(entries[0]));
    qtest_memwrite(qts, SCSI_REQUEST_DMA, entries[1], sizeof(entries[1]));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(
                        qts, 5, SCSI_QUEUE_COUNT - 1), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 1);
    scsi_read_status(qts, SCSI_QUEUE_COUNT - 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x13572468));
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);

    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 0);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_CLEAR_RISC_INT);
    qtest_memset(qts, SCSI_DATA_DMA + 0x3000, 0xa5, 0x3000);
    scsi_build_rw_three_segments(entries, 0x24681357, false,
                               SCSI_DATA_DMA + 0x3000);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   entries, sizeof(entries));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x24681357));
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);

    qtest_memread(qts, SCSI_DATA_DMA + 0x3000, actual, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x4000, actual + 128, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x5000, actual + 256, 256);
    for (i = 0; i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0);
    }
    qtest_quit(qts);
}

static void test_scsi_32bit_iocb(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "node-name=scsi32 "
        "-device scsi-hd,drive=scsi32,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t data[512];
    unsigned int i;

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    qtest_memset(qts, SCSI_DATA_DMA, 0xa5, sizeof(data));
    scsi_build_rw_32(request, 0x10203040, false,
                     (uint32_t)SCSI_DATA_DMA);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    scsi_read_status(qts, 1, status);
    g_assert_cmphex(ldl_le_p(status + 4), ==, 0x10203040);
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);
    qtest_memread(qts, SCSI_DATA_DMA, data, sizeof(data));
    for (i = 0; i < sizeof(data); i++) {
        g_assert_cmphex(data[i], ==, 0);
    }
    qtest_quit(qts);
}

static void test_scsi_data_unknown(gconstpointer opaque)
{
    bool write = *(const bool *)opaque;
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "node-name=scsiunknown "
        "-device scsi-hd,drive=scsiunknown,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    scsi_build_rw_three_segments(entries, 0x554e4b4e, write,
                                 SCSI_DATA_DMA);
    stw_le_p(entries[0] + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET,
             ISP12160_IOCB_CONTROL_DATA_UNKNOWN);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   entries, sizeof(entries));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    scsi_read_status(qts, 1, status);
    g_assert_cmphex(ldl_le_p(status + 4), ==, 0x554e4b4e);
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);
    g_assert_cmphex(ldl_le_p(status + 20), ==, 0);
    qtest_quit(qts);
}

static const bool data_unknown_read;
static const bool data_unknown_write = true;

static void test_scsi_verify_no_data(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "node-name=scsiverify "
        "-device scsi-hd,drive=scsiverify,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    scsi_build_verify_no_data(request, 0x56455249, 3);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 2);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    scsi_read_status(qts, 1, status);
    g_assert_cmphex(ldl_le_p(status + 4), ==, 0x56455249);
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);
    g_assert_cmphex(ldl_le_p(status + 20), ==, 0);
    qtest_quit(qts);
}

static void test_scsi_data_dma_fault(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "node-name=scsifault "
        "-device scsi-hd,drive=scsifault,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    scsi_build_rw_three_segments(entries, 0xfeed1234, false,
                               UINT64_C(0x90000000));
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   entries, sizeof(entries));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    scsi_read_status(qts, 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0xfeed1234));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_DMA_ERROR);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 20), ==, 512);
    qtest_quit(qts);
}

/* Root 1 rejects controller-MMIO DMA without changing controller state. */
static void test_scsi_self_mmio_data_dma(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "latency-ns=100000000,node-name=scsiself "
        "-device scsi-hd,drive=scsiself,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    const uint16_t ictrl = ISP12160_ICTRL_ENABLE_INT |
                           ISP12160_ICTRL_ENABLE_RISC;

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    scsi_build_rw_three_segments(
        entries, 0x5e1fd00d, false,
        IA64_ISP12160_TEST_MMIO_BASE + ISP12160_REG_ICTRL);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   entries, sizeof(entries));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);

    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ICTRL), ==, ictrl);
    scsi_read_status(qts, 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x5e1fd00d));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_DMA_ERROR);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 20), ==, 512);
    qtest_quit(qts);
}

static void test_scsi_pending_status_migration(void)
{
    const char *disk_options =
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "latency-ns=100000000,node-name=scsimigrate "
        "-device scsi-hd,drive=scsimigrate,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0";
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-pending-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    g_autofree char *incoming_options = NULL;
    uint8_t read_entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t blocked[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    QTestState *qts;
    uint16_t command;
    unsigned int i;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);
    incoming_options = g_strdup_printf("-incoming defer %s", disk_options);

    qts = scsi_start_with_options(disk_options);
    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);

    scsi_build_rw_three_segments(read_entries, 0x4d494731, false,
                               SCSI_DATA_DMA);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   read_entries, sizeof(read_entries));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    scsi_wait_index_equals(qts, 4, 3);

    /*
     * Stop DMA while the delayed READ is active.  Publishing one more command
     * sets dma_stalled; the READ then completes as a buffered DMA_ERROR status.
     */
    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
    scsi_build_no_data(blocked, 0x4d494732, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   blocked, sizeof(blocked));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 4);
    for (i = 0; i < 100; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    }
    g_usleep(500 * 1000);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==, 1);

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    qts = scsi_start_with_options(incoming_options);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);

    g_assert_cmphex(mailbox_config_readw(qts, PCI_COMMAND) & PCI_COMMAND_MASTER,
                    ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==, 1);

    command = mailbox_config_readw(qts, PCI_COMMAND);
    mailbox_config_writew(qts, PCI_COMMAND, command | PCI_COMMAND_MASTER);
    scsi_wait_index_equals(qts, 5, 3);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 4);

    scsi_read_status(qts, 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x4d494731));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_DMA_ERROR);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 20), ==, 512);
    scsi_read_status(qts, 2, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x4d494732));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

static void test_scsi_active_read_migration(gconstpointer opaque)
{
    bool ordered = GPOINTER_TO_INT(opaque);
    bool unit_attention = GPOINTER_TO_INT(opaque) == 2;
    unsigned request_index = ordered ? 5 : 3;
    unsigned response_index = ordered ? 4 : 3;
    unsigned first_response = unit_attention ? 0 : 1;
    const char *destination_disk_options =
        "-blockdev driver=raw,node-name=scsiactive,"
        "file.driver=null-co,file.read-zeroes=on,file.size=1048576 "
        "-device scsi-hd,drive=scsiactive,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0,rerror=stop";
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-active-read-migration.XXXXXX",
        g_get_tmp_dir());
    char *debug_path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-active-read-blkdebug.XXXXXX",
        g_get_tmp_dir());
    g_autofree char *source_disk_options = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *incoming_options = NULL;
    uint8_t read_entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t marker[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t following[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t actual[512];
    QTestState *qts;
    FILE *debug_file;
    bool block_io_error_seen = false;
    unsigned int i;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    fd = g_mkstemp(debug_path);
    g_assert_cmpint(fd, >=, 0);
    debug_file = fdopen(fd, "w");
    g_assert_nonnull(debug_file);
    /*
     * Keep injection armed across harmless realize-time probe reads.  Only
     * the source has this blkdebug node; the destination uses clean null-co.
     */
    g_assert_cmpint(fprintf(debug_file,
                           "[inject-error]\n"
                           "event = \"read_aio\"\n"
                           "errno = \"5\"\n"
                           "state = \"1\"\n"
                           "immediately = \"off\"\n"
                           "once = \"off\"\n"), >, 0);
    g_assert_cmpint(fclose(debug_file), ==, 0);
    g_test_queue_destroy(mailbox_migration_file_cleanup, debug_path);

    source_disk_options = g_strdup_printf(
        "-blockdev driver=raw,node-name=scsiactive,"
        "file.driver=blkdebug,file.config=%s,"
        "file.image.driver=null-co,file.image.read-zeroes=on,"
        "file.image.size=1048576 "
        "-device scsi-hd,drive=scsiactive,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0,rerror=stop",
        debug_path);
    incoming_options = g_strdup_printf("-incoming defer %s",
                                       destination_disk_options);

    qts = scsi_start_with_options(source_disk_options);
    scsi_prepare_queues(qts, 0, 0);
    if (unit_attention) {
        uint16_t mb[MAILBOX_COUNT] = {
            ISP12160_MBC_SET_DEVICE_QUEUE, 3 << 8, 8, 1,
        };

        g_assert_cmphex(mailbox_command(qts, mb),
                        ==, ISP12160_MBS_COMMAND_COMPLETE);
        mailbox_ack_completion(qts);
        /* Capture the initial UA, then let the HEAD read hold this TUR. */
        scsi_build_no_data(marker, 0x4d494744, 0, 3, 0);
        stw_le_p(marker + 12, ISP12160_IOCB_CONTROL_ORDERED_TAG);
        qtest_memwrite(qts, SCSI_REQUEST_DMA, marker, sizeof(marker));
    } else {
        scsi_consume_disk_unit_attention(qts, 3);
    }
    qtest_memset(qts, SCSI_DATA_DMA, 0xa5, 128);
    qtest_memset(qts, SCSI_DATA_DMA + 0x1000, 0xa5, 128);
    qtest_memset(qts, SCSI_DATA_DMA + 0x2000, 0xa5, 256);

    scsi_build_rw_three_segments(read_entries, 0x4d494741, false,
                               SCSI_DATA_DMA);
    stw_le_p(read_entries[0] + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET,
             ISP12160_IOCB_CONTROL_DATA_UNKNOWN |
             (unit_attention ? ISP12160_IOCB_CONTROL_HEAD_TAG : 0));
    scsi_build_marker(marker, 0, 0, 0, ISP12160_IOCB_MARKER_SYNC_ALL);
    scsi_build_no_data(following, 0x4d494742, 0, ordered ? 3 : 15, 0);
    if (ordered) {
        scsi_build_no_data(marker, 0x4d494743, 0, 3, 0);
        stw_le_p(marker + 12, ISP12160_IOCB_CONTROL_ORDERED_TAG);
        stw_le_p(marker + 16, 5);
        stw_le_p(following + 12, ISP12160_IOCB_CONTROL_SIMPLE_TAG);
    }
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   read_entries, sizeof(read_entries));
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   marker, sizeof(marker));
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 4 * ISP12160_QUEUE_ENTRY_BYTES,
                   following, sizeof(following));

    /* The injected read error must stop a running VM to mark the retry. */
    qtest_qmp_set_event_callback(qts, scsi_block_io_error_event,
                                 &block_io_error_seen);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    scsi_wait_for_runstate(qts, "running");
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 5);
    scsi_wait_index_equals(qts, 4, request_index);
    scsi_wait_for_active_read_io_error(qts, &block_io_error_seen,
                                       request_index, first_response);
    qtest_qmp_set_event_callback(qts, NULL, NULL);

    /* The IOCB is consumed, but the stopped READ remains active and silent. */
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8),
                    ==, request_index);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10),
                    ==, first_response);
    scsi_read_status(qts, first_response, status);
    g_assert_cmphex(status[0], ==, 0);
    qtest_memread(qts, SCSI_DATA_DMA, actual, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x1000, actual + 128, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x2000, actual + 256, 256);
    for (i = 0; i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0xa5);
    }

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    /* Source is destroyed before the destination is constructed. */
    qts = scsi_start_with_options(incoming_options);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);

    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8),
                    ==, request_index);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10),
                    ==, first_response);
    scsi_read_status(qts, first_response, status);
    g_assert_cmphex(status[0], ==, 0);

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    scsi_wait_index_equals(qts, 5, response_index);
    for (i = 0; i < 100; i++) {
        g_assert_cmphex(mailbox_mmio_readw(
                            qts, ISP12160_REG_MAILBOX0 + 10),
                        ==, response_index);
    }
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 5);
    scsi_read_status(qts, first_response, status);
    g_assert_cmphex(status[0], ==, ISP12160_IOCB_STATUS_TYPE);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x4d494741));
    g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_COMPLETE);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 20), ==, 0);
    if (unit_attention) {
        scsi_read_status(qts, 1, status);
        g_assert_cmphex((uint32_t)ldl_le_p(status + 4), ==, 0x4d494744);
        g_assert_cmphex(lduw_le_p(status + 10), ==, ISP12160_IOCB_CS_COMPLETE);
        g_assert_cmphex(lduw_le_p(status + 8) & 0xff, ==, 2);
        g_assert_cmpuint(lduw_le_p(status + 18), >=, 14);
        g_assert_cmphex(status[34] & 0x0f, ==, 6); /* UNIT ATTENTION */
        g_assert_cmphex(status[44], ==, 0x29);    /* Power on/reset */
        g_assert_cmphex(status[45], ==, 0);
    }
    if (ordered) {
        scsi_read_status(qts, 2, status);
        g_assert_cmphex((uint32_t)ldl_le_p(status + 4), ==, 0x4d494743);
        g_assert_cmphex(lduw_le_p(status + 10), ==, ISP12160_IOCB_CS_COMPLETE);
    }
    scsi_read_status(qts, response_index - 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x4d494742));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ordered ? ISP12160_IOCB_CS_COMPLETE :
                              ISP12160_IOCB_CS_INCOMPLETE);

    qtest_memread(qts, SCSI_DATA_DMA, actual, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x1000, actual + 128, 128);
    qtest_memread(qts, SCSI_DATA_DMA + 0x2000, actual + 256, 256);
    for (i = 0; i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0);
    }
    qtest_quit(qts);
}

typedef struct SCSIMarkerScopeTest {
    uint8_t modifier;
    uint8_t channel;
    uint8_t target;
    uint8_t lun;
    bool blocks;
} SCSIMarkerScopeTest;

static const SCSIMarkerScopeTest marker_scope_id_lun_match = {
    ISP12160_IOCB_MARKER_SYNC_ID_LUN, 0, 3, 0, true,
};
static const SCSIMarkerScopeTest marker_scope_id_match = {
    ISP12160_IOCB_MARKER_SYNC_ID, 0, 3, 7, true,
};
static const SCSIMarkerScopeTest marker_scope_all_match = {
    ISP12160_IOCB_MARKER_SYNC_ALL, 0, 15, 7, true,
};
static const SCSIMarkerScopeTest marker_scope_id_lun_miss = {
    ISP12160_IOCB_MARKER_SYNC_ID_LUN, 0, 3, 1, false,
};
static const SCSIMarkerScopeTest marker_scope_id_miss = {
    ISP12160_IOCB_MARKER_SYNC_ID, 0, 2, 0, false,
};
static const SCSIMarkerScopeTest marker_scope_all_miss = {
    ISP12160_IOCB_MARKER_SYNC_ALL, 1, 3, 0, false,
};

static void test_scsi_marker_active_barrier(gconstpointer opaque)
{
    const SCSIMarkerScopeTest *test = opaque;
    char *debug_path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-marker-blkdebug.XXXXXX",
        g_get_tmp_dir());
    g_autofree char *disk_options = NULL;
    uint8_t read_entries[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t marker[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t following[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    QTestState *qts;
    FILE *debug_file;
    bool block_io_error_seen = false;
    int fd;

    fd = g_mkstemp(debug_path);
    g_assert_cmpint(fd, >=, 0);
    debug_file = fdopen(fd, "w");
    g_assert_nonnull(debug_file);
    g_assert_cmpint(fprintf(debug_file,
                           "[inject-error]\n"
                           "event = \"read_aio\"\n"
                           "errno = \"5\"\n"
                           "state = \"1\"\n"
                           "immediately = \"off\"\n"
                           "once = \"off\"\n"), >, 0);
    g_assert_cmpint(fclose(debug_file), ==, 0);
    g_test_queue_destroy(mailbox_migration_file_cleanup, debug_path);

    disk_options = g_strdup_printf(
        "-blockdev driver=raw,node-name=scsimarker,"
        "file.driver=blkdebug,file.config=%s,"
        "file.image.driver=null-co,file.image.read-zeroes=on,"
        "file.image.size=1048576 "
        "-device scsi-hd,drive=scsimarker,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0,rerror=stop",
        debug_path);
    qts = scsi_start_with_options(disk_options);
    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);

    scsi_build_rw_three_segments(read_entries, 0x4d4b5231, false,
                                 SCSI_DATA_DMA);
    scsi_build_marker(marker, test->channel, test->target, test->lun,
                      test->modifier);
    scsi_build_no_data(following, 0x4d4b5232, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   read_entries, sizeof(read_entries));
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   marker, sizeof(marker));
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 4 * ISP12160_QUEUE_ENTRY_BYTES,
                   following, sizeof(following));

    qtest_qmp_set_event_callback(qts, scsi_block_io_error_event,
                                 &block_io_error_seen);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    scsi_wait_for_runstate(qts, "running");
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 5);
    scsi_wait_index_equals(qts, 4, test->blocks ? 3 : 5);
    scsi_wait_index_equals(qts, 5, test->blocks ? 1 : 2);
    scsi_wait_for_active_read_io_error(qts, &block_io_error_seen,
                                       test->blocks ? 3 : 5,
                                       test->blocks ? 1 : 2);
    qtest_qmp_set_event_callback(qts, NULL, NULL);

    scsi_read_status(qts, 1, status);
    if (test->blocks) {
        g_assert_cmphex(status[0], ==, 0);
    } else {
        g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                         UINT32_C(0x4d4b5232));
    }
    qtest_quit(qts);
}

static void scsi_assert_completion(QTestState *qts, unsigned index,
                                  uint32_t handle, uint16_t completion)
{
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_read_status(qts, index, status);
    g_assert_cmphex((uint32_t)ldl_le_p(status + 4), ==, handle);
    g_assert_cmphex(lduw_le_p(status + 10), ==, completion);
}

static void test_scsi_task_ordering(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "latency-ns=500000000,node-name=scsislow "
        "-device scsi-hd,drive=scsislow,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t read[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t commands[3][ISP12160_QUEUE_ENTRY_BYTES];
    static const uint16_t flags[] = {
        ISP12160_IOCB_CONTROL_ORDERED_TAG,
        ISP12160_IOCB_CONTROL_SIMPLE_TAG,
        ISP12160_IOCB_CONTROL_HEAD_TAG,
    };
    int64_t deadline;

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    scsi_build_rw_three_segments(read, 1, false, SCSI_DATA_DMA);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   read, sizeof(read));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    scsi_wait_index_equals(qts, 4, 3);
    for (unsigned i = 0; i < 3; i++) {
        scsi_build_no_data(commands[i], i + 2, 0, 3, 0);
        stw_le_p(commands[i] + 12, flags[i]);
    }
    qtest_memwrite(qts, SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   commands, sizeof(commands));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 6);
    scsi_wait_index_equals(qts, 4, 6);
    scsi_wait_index_equals(qts, 5, 2);
    /* HEAD passes a waiting ORDERED command while the earlier read is busy. */
    scsi_assert_completion(qts, 1, 4, ISP12160_IOCB_CS_COMPLETE);
    deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10) != 5 &&
           g_get_monotonic_time() < deadline) {
        g_usleep(1000);
    }
    scsi_wait_index_equals(qts, 5, 5);
    scsi_assert_completion(qts, 2, 1, ISP12160_IOCB_CS_COMPLETE);
    scsi_assert_completion(qts, 3, 2, ISP12160_IOCB_CS_COMPLETE);
    scsi_assert_completion(qts, 4, 3, ISP12160_IOCB_CS_COMPLETE);
    qtest_quit(qts);
}

static void test_scsi_queue_timeout(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "latency-ns=2000000000,node-name=scsislow "
        "-device scsi-hd,drive=scsislow,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t read[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t command[ISP12160_QUEUE_ENTRY_BYTES];
    uint16_t mb[MAILBOX_COUNT] = {
        ISP12160_MBC_SET_DEVICE_QUEUE, 3 << 8, 8, 1,
    };

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    g_assert_cmphex(mailbox_command(qts, mb),
                    ==, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
    mb[0] = ISP12160_MBC_GET_DEVICE_QUEUE;
    g_assert_cmphex(mailbox_command(qts, mb),
                    ==, ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmpuint(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4), ==, 8);
    g_assert_cmpuint(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6), ==, 1);
    mailbox_ack_completion(qts);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    scsi_build_rw_three_segments(read, 1, false, SCSI_DATA_DMA);
    stw_le_p(read[0] + 16, 1);
    scsi_build_no_data(command, 2, 0, 3, 0);
    stw_le_p(command + 16, 1);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   read, sizeof(read));
    qtest_memwrite(qts, SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   command, sizeof(command));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 4);
    scsi_wait_index_equals(qts, 4, 4);
    /* Execution throttle holds the TUR, and its queue wait also times out. */
    g_assert_cmpuint(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10),
                     ==, 1);
    qtest_clock_step(qts, 999999999);
    g_assert_cmpuint(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10),
                     ==, 1);
    qtest_clock_step(qts, 1);
    scsi_wait_index_equals(qts, 5, 3);
    scsi_assert_completion(qts, 1, 1, ISP12160_IOCB_CS_TIMEOUT);
    scsi_assert_completion(qts, 2, 2, ISP12160_IOCB_CS_TIMEOUT);
    qtest_quit(qts);
}

static void test_scsi_deferred_request_sense(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        bool unit_attention = scenario & 1;
        bool descriptor = scenario & 2;
        unsigned first = unit_attention ? 0 : 1;
        unsigned sense_len = descriptor ? 8 : 18;
        QTestState *qts = scsi_start_with_options(
            "-blockdev driver=null-co,read-zeroes=on,size=1048576,node-name=disk "
            "-device scsi-hd,drive=disk,bus=isp12160-scsi.0,"
            "channel=0,scsi-id=3,lun=0");
        uint8_t commands[3][ISP12160_QUEUE_ENTRY_BYTES];
        uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
        uint8_t sense[18];

        g_test_message("%s sense, %s", descriptor ? "descriptor" : "fixed",
                       unit_attention ? "unit attention" : "invalid opcode");
        scsi_prepare_queues(qts, 0, 0);
        if (!unit_attention) {
            scsi_consume_disk_unit_attention(qts, 3);
        }
        scsi_build_no_data(commands[0], 1, 0, 3, 0);
        commands[0][20] = unit_attention ? 0x00 : 0xff;
        stw_le_p(commands[0] + 12, ISP12160_IOCB_CONTROL_DISABLE_AUTOSENSE);
        for (unsigned i = 1; i < ARRAY_SIZE(commands); i++) {
            scsi_build_no_data(commands[i], i + 1, 0, 3, 0);
            commands[i][20] = 0x03; /* REQUEST SENSE */
            commands[i][21] = descriptor;
            commands[i][24] = sense_len;
            stw_le_p(commands[i] + 12, ISP12160_IOCB_CONTROL_ORDERED_TAG |
                     ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE);
            stw_le_p(commands[i] + 18, 1);
            scsi_set_segment(commands[i], 40,
                             SCSI_DATA_DMA + (i - 1) * 0x100, sense_len);
        }
        qtest_memset(qts, SCSI_DATA_DMA, 0xaa, 0x200);
        /* Construct all requests before the first command produces sense. */
        qtest_memwrite(qts,
                       SCSI_REQUEST_DMA + first * ISP12160_QUEUE_ENTRY_BYTES,
                       commands, sizeof(commands));
        mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, first + 3);
        scsi_wait_index_equals(qts, 5, first + 3);
        scsi_read_status(qts, first, status);
        g_assert_cmphex(lduw_le_p(status + 8), ==, 2); /* CHECK CONDITION */
        g_assert_cmpuint(lduw_le_p(status + 18), ==, 0); /* No autosense. */
        for (unsigned i = 1; i < ARRAY_SIZE(commands); i++) {
            unsigned key = i == 1 ? (unit_attention ? 6 : 5) : 0;
            unsigned asc = i == 1 ? (unit_attention ? 0x29 : 0x20) : 0;

            scsi_assert_completion(qts, first + i, i + 1,
                                   ISP12160_IOCB_CS_COMPLETE);
            scsi_read_status(qts, first + i, status);
            g_assert_cmphex(lduw_le_p(status + 8), ==, 0);
            qtest_memread(qts, SCSI_DATA_DMA + (i - 1) * 0x100,
                          sense, sense_len);
            g_assert_cmphex(sense[0] & 0x7f, ==, descriptor ? 0x72 : 0x70);
            g_assert_cmphex(sense[descriptor ? 1 : 2] & 0xf, ==, key);
            g_assert_cmphex(sense[descriptor ? 2 : 12], ==, asc);
        }
        qtest_quit(qts);
    }
}

static void test_scsi_autosense_controls(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,node-name=disk "
        "-device scsi-hd,drive=disk,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint16_t mb[MAILBOX_COUNT] = {
        ISP12160_MBC_SET_INITIATOR_ID, 0x86,
    };
    uint8_t command[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    g_assert_cmphex(mailbox_command(qts, mb),
                    ==, ISP12160_MBS_COMMAND_COMPLETE);
    mailbox_ack_completion(qts);
    mb[0] = ISP12160_MBC_GET_INITIATOR_ID;
    g_assert_cmphex(mailbox_command(qts, mb),
                    ==, ISP12160_MBS_COMMAND_COMPLETE);
    g_assert_cmpuint(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 2), ==, 6);
    mailbox_ack_completion(qts);

    for (unsigned i = 0; i < 4; i++) {
        bool sense = !(i & 1) && !(i & 2);

        memset(mb, 0, sizeof(mb));
        mb[0] = ISP12160_MBC_SET_TARGET_PARAMETERS;
        mb[1] = 3 << 8;
        mb[2] = i & 2 ? 0xf800 : 0xfc00;
        mb[3] = 0x190c;
        mb[6] = 2;
        g_assert_cmphex(mailbox_command(qts, mb), ==,
                        ISP12160_MBS_COMMAND_COMPLETE);
        mailbox_ack_completion(qts);
        mb[0] = ISP12160_MBC_GET_TARGET_PARAMETERS;
        g_assert_cmphex(mailbox_command(qts, mb), ==,
                        ISP12160_MBS_COMMAND_COMPLETE);
        g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 4),
                        ==, mb[2]);
        g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 6),
                        ==, 0x190c);
        g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 12),
                        ==, 2);
        mailbox_ack_completion(qts);
        scsi_build_no_data(command, i + 1, 0, 3, 0);
        command[20] = 0xff; /* Unsupported CDB returns CHECK CONDITION. */
        stw_le_p(command + 12, i & 1 ?
                 ISP12160_IOCB_CONTROL_DISABLE_AUTOSENSE : 0);
        qtest_memwrite(qts,
                       SCSI_REQUEST_DMA + (i + 1) * ISP12160_QUEUE_ENTRY_BYTES,
                       command, sizeof(command));
        mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, i + 2);
        scsi_wait_index_equals(qts, 5, i + 2);
        scsi_read_status(qts, i + 1, status);
        g_assert_cmphex(lduw_le_p(status + 8), ==, 2);
        g_assert_cmpuint(!!lduw_le_p(status + 18), ==, sense);
        g_assert_cmpuint(!!(lduw_le_p(status + 12) &
                           ISP12160_IOCB_SF_GOT_SENSE), ==, sense);
        mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, i + 2);
        mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                            ISP12160_HC_CLEAR_RISC_INT);
    }
    qtest_quit(qts);
}

static void test_scsi_reset_active_request(void)
{
    QTestState *qts = scsi_start_with_options(
        "-blockdev driver=null-co,read-zeroes=on,size=1048576,"
        "latency-ns=100000000,node-name=scsislow "
        "-device scsi-hd,drive=scsislow,bus=isp12160-scsi.0,"
        "channel=0,scsi-id=3,lun=0");
    uint8_t first[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t second[2][ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t response[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_consume_disk_unit_attention(qts, 3);
    scsi_build_rw_three_segments(first, 0xcafef00d, false, SCSI_DATA_DMA);
    scsi_build_rw_three_segments(second, 0xcafef11d, false,
                               SCSI_DATA_DMA + 0x3000);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   first, sizeof(first));
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA + 3 * ISP12160_QUEUE_ENTRY_BYTES,
                   second, sizeof(second));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 5);
    scsi_wait_index_equals(qts, 4, 5);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 10), ==, 1);

    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                   ISP12160_HC_RESET_RISC);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0), ==,
                    ISP12160_MBS_FIRMWARE_ALIVE);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    ISP12160_PRODUCT_ID_4);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_ISTATUS), ==, 0);
    for (size_t entry = 1; entry <= 2; entry++) {
        qtest_memread(qts,
                      SCSI_RESPONSE_DMA +
                      entry * ISP12160_QUEUE_ENTRY_BYTES,
                      response, sizeof(response));
        for (size_t i = 0; i < sizeof(response); i++) {
            g_assert_cmphex(response[i], ==, 0);
        }
    }
    qtest_quit(qts);
}

static void test_scsi_malformed_entry(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES] = { 0 };
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    request[0] = 0x7f;
    request[1] = 1;
    stl_le_p(request + 4, 0xdeadbeef);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0xdeadbeef));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INVALID_ENTRY_TYPE);
    qtest_quit(qts);
}

static void test_scsi_native_iocbs(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t marker[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(request, 0x0badf00d, 0, 15, 0);
    stw_le_p(request + 16, 1);
    stw_le_p(request + 12,
             ISP12160_IOCB_CONTROL_NO_DISCONNECT |
             ISP12160_IOCB_CONTROL_ORDERED_TAG |
             ISP12160_IOCB_CONTROL_DISABLE_AUTOSENSE);
    memset(request + ISP12160_IOCB_A64_SEGMENT0_OFFSET, 0xa5,
           ISP12160_IOCB_A64_SEGMENT_STRIDE);
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x0badf00d));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);

    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);
    mailbox_mmio_writew(qts, ISP12160_REG_SEMAPHORE, 0);
    mailbox_mmio_writew(qts, ISP12160_REG_HOST_COMMAND,
                        ISP12160_HC_CLEAR_RISC_INT);

    scsi_build_marker(marker, 0, 0, 0, ISP12160_IOCB_MARKER_SYNC_ALL);
    scsi_build_no_data(request, 0xc001d00d, 0, 15, 0);
    qtest_memwrite(qts, SCSI_REQUEST_DMA + ISP12160_QUEUE_ENTRY_BYTES,
                   marker, sizeof(marker));
    qtest_memwrite(qts, SCSI_REQUEST_DMA + 2 * ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 3);
    scsi_wait_index_equals(qts, 4, 3);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 1), ==, 2);
    scsi_read_status(qts, 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0xc001d00d));
    qtest_quit(qts);
}

static void test_scsi_impossible_entry_count(void)
{
    QTestState *qts = scsi_start();
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];

    scsi_prepare_queues(qts, 0, 0);
    scsi_build_no_data(request, 0x11223344, 0, 0, 0);
    request[1] = SCSI_QUEUE_COUNT;
    qtest_memwrite(qts, SCSI_REQUEST_DMA, request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 1);
    g_assert_cmphex(scsi_wait_live_index(qts, 5, 0), ==, 1);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 1);
    scsi_read_status(qts, 0, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x11223344));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INVALID_ENTRY_TYPE);
    qtest_quit(qts);
}

static void test_scsi_blocked_response_migration(void)
{
    char *path = g_strdup_printf(
        "%s/ia64-isp12160-scsi-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    uint8_t request[ISP12160_QUEUE_ENTRY_BYTES];
    uint8_t status[ISP12160_QUEUE_ENTRY_BYTES];
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(mailbox_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    qts = scsi_start();
    scsi_prepare_queues(qts, 0, 0);
    scsi_fill_response_ring(qts);
    scsi_build_no_data(request, 0x76543210, 0, 15, 0);
    qtest_memwrite(qts,
                   SCSI_REQUEST_DMA +
                   (SCSI_QUEUE_COUNT - 1) * ISP12160_QUEUE_ENTRY_BYTES,
                   request, sizeof(request));
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 8, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    SCSI_QUEUE_COUNT - 1);

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    mailbox_wait_for_migration_complete(qts);
    qtest_quit(qts);

    qts = scsi_start_with_options("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    mailbox_wait_for_migration_complete(qts);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==,
                    SCSI_QUEUE_COUNT - 1);
    mailbox_mmio_writew(qts, ISP12160_REG_MAILBOX0 + 10, 1);
    g_assert_cmphex(scsi_wait_live_index(
                        qts, 5, SCSI_QUEUE_COUNT - 1), ==, 0);
    g_assert_cmphex(mailbox_mmio_readw(qts, ISP12160_REG_MAILBOX0 + 8), ==, 0);
    scsi_read_status(qts, SCSI_QUEUE_COUNT - 1, status);
    g_assert_cmpuint((uint32_t)ldl_le_p(status + 4), ==,
                     UINT32_C(0x76543210));
    g_assert_cmphex(lduw_le_p(status + 10), ==,
                    ISP12160_IOCB_CS_INCOMPLETE);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("isp12160-mailbox/pci-bars-reset",
                   test_pci_bars_alias_and_reset);
    qtest_add_func("isp12160-mailbox/mailbox-token",
                   test_mailbox_and_qemu_token);
    qtest_add_func("isp12160-mailbox/irq-pid20",
                   test_irq_semaphore_and_pid20);
    qtest_add_func("isp12160-mailbox/reset-cancels-pending",
                   test_reset_risc_cancels_pending_mailbox);
    qtest_add_func("isp12160-mailbox/sequential-file-migration",
                   test_sequential_file_migration);
    qtest_add_func("isp12160-queue/token-execute-iocb-boundary",
                   test_queue_token_and_execute_iocb_boundary);
    qtest_add_func("isp12160-queue/queue-init-reset",
                   test_queue_init_and_reset);
    qtest_add_func("isp12160-queue/sequential-file-migration",
                   test_queue_sequential_file_migration);
    qtest_add_func("isp12160-scsi/token-variant-separation",
                   test_scsi_token_variant_separation);
    qtest_add_func("isp12160-scsi/native-firmware",
                   test_scsi_native_firmware);
    qtest_add_func("isp12160-scsi/mailbox-lock-backpressure",
                   test_scsi_mailbox_lock_backpressure);
    qtest_add_func("isp12160-scsi/dual-channel-no-data",
                   test_scsi_dual_channel_no_data);
    qtest_add_func("isp12160-scsi/response-full-backpressure",
                   test_scsi_response_full_backpressure);
    qtest_add_func("isp12160-scsi/response-during-irq-ack",
                   test_scsi_response_during_irq_ack);
    qtest_add_func("isp12160-scsi/response-after-queue-snapshot",
                   test_scsi_response_after_queue_snapshot);
    qtest_add_func("isp12160-scsi/response-irq-migration",
                   test_scsi_response_irq_migration);
    qtest_add_func("isp12160-scsi/bus-master-reenable",
                   test_scsi_bus_master_reenable);
    qtest_add_func("isp12160-scsi/index-update-validation",
                   test_scsi_index_update_validation);
    qtest_add_func("isp12160-scsi/sg-read-write-ring-wrap",
                   test_scsi_sg_read_write_ring_wrap);
    qtest_add_func("isp12160-scsi/32bit-iocb",
                   test_scsi_32bit_iocb);
    qtest_add_data_func("isp12160-scsi/data-unknown/read",
                        &data_unknown_read, test_scsi_data_unknown);
    qtest_add_data_func("isp12160-scsi/data-unknown/write",
                        &data_unknown_write, test_scsi_data_unknown);
    qtest_add_func("isp12160-scsi/verify-no-data",
                   test_scsi_verify_no_data);
    qtest_add_func("isp12160-scsi/data-dma-fault",
                   test_scsi_data_dma_fault);
    qtest_add_func("isp12160-scsi/self-mmio-data-dma",
                   test_scsi_self_mmio_data_dma);
    qtest_add_func("isp12160-scsi/pending-status-migration",
                   test_scsi_pending_status_migration);
    qtest_add_data_func("isp12160-scsi/active-read-migration",
                        GINT_TO_POINTER(0),
                        test_scsi_active_read_migration);
    qtest_add_data_func("isp12160-scsi/ordered-read-migration",
                        GINT_TO_POINTER(1),
                        test_scsi_active_read_migration);
    qtest_add_data_func("isp12160-scsi/deferred-unit-attention-migration",
                        GINT_TO_POINTER(2),
                        test_scsi_active_read_migration);
    qtest_add_data_func("isp12160-scsi/marker/id-lun-match",
                        &marker_scope_id_lun_match,
                        test_scsi_marker_active_barrier);
    qtest_add_data_func("isp12160-scsi/marker/id-match",
                        &marker_scope_id_match,
                        test_scsi_marker_active_barrier);
    qtest_add_data_func("isp12160-scsi/marker/all-match",
                        &marker_scope_all_match,
                        test_scsi_marker_active_barrier);
    qtest_add_data_func("isp12160-scsi/marker/id-lun-miss",
                        &marker_scope_id_lun_miss,
                        test_scsi_marker_active_barrier);
    qtest_add_data_func("isp12160-scsi/marker/id-miss",
                        &marker_scope_id_miss,
                        test_scsi_marker_active_barrier);
    qtest_add_data_func("isp12160-scsi/marker/all-miss",
                        &marker_scope_all_miss,
                        test_scsi_marker_active_barrier);
    qtest_add_func("isp12160-scsi/reset-active-request",
                   test_scsi_reset_active_request);
    qtest_add_func("isp12160-scsi/malformed-entry",
                   test_scsi_malformed_entry);
    qtest_add_func("isp12160-scsi/native-iocbs",
                   test_scsi_native_iocbs);
    qtest_add_func("isp12160-scsi/impossible-entry-count",
                   test_scsi_impossible_entry_count);
    qtest_add_func("isp12160-scsi/blocked-response-migration",
                   test_scsi_blocked_response_migration);

    qtest_add_func("isp12160-scsi/task-ordering", test_scsi_task_ordering);
    qtest_add_func("isp12160-scsi/queue-timeout", test_scsi_queue_timeout);
    qtest_add_func("isp12160-scsi/deferred-request-sense",
                   test_scsi_deferred_request_sense);
    qtest_add_func("isp12160-scsi/autosense-controls",
                   test_scsi_autosense_controls);
    return g_test_run();
}
