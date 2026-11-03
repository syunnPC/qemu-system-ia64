/*
 * IA-64 i2000 I/O device qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <glib/gstdio.h>

#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/ia64/ia64_i2000_io_test.h"
#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "hw/isa/lpc47b27_config.h"
#include "hw/pci/pci.h"
#include "libqtest.h"
#include "net/eth.h"
#include "qemu/sockets.h"
#include "qobject/qdict.h"

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_ER_ABRT 0x04
#define ATA_CMD_PACKET 0xa0
#define ATA_CMD_IDENTIFY_PACKET 0xa1
#define I8042_CCMD_SELF_TEST 0xaa
#define I8042_SELF_TEST_OK 0x55

#define PID_IOREGSEL 0x00
#define PID_IOWIN 0x10
#define PID_RTE_BASE 0x10
#define PID_RTE_DELIVERY_STATUS BIT(12)
#define PID_RTE_TRIGGER_LEVEL BIT(15)

/* Fixed guest-visible resources exercised by this test. */
#define I82559_PCI_BUS 0U
#define I82559_MMIO_BASE UINT32_C(0x90010000)
#define I82559_IO_BASE UINT32_C(0x00001000)
#define I82559_FLASH_BASE UINT32_C(0x90100000)

#define E100_SCB_ACK 0x01
#define E100_SCB_STATUS 0x00
#define E100_SCB_COMMAND 0x02
#define E100_SCB_INTMASK 0x03
#define E100_SCB_POINTER 0x04
#define E100_SCB_PORT 0x08
#define E100_SCB_FLASH 0x0c
#define E100_SCB_EEPROM 0x0e
#define E100_SCB_MDI 0x10
#define E100_SWI_REQUEST BIT(1)
#define E100_SWI_ACK BIT(2)
#define E100_ACK_CNA BIT(5)
#define E100_ACK_FR BIT(6)
#define E100_ACK_CX BIT(7)
#define E100_CU_START 0x10U
#define E100_CU_RESUME 0x20U
#define E100_RU_START 0x01U
#define E100_CU_STATE_SUSPENDED BIT(6)
#define E100_RU_STATE_READY BIT(4)
#define E100_CB_COMMAND_MCAST 0x0003U
#define E100_CB_COMMAND_TX 0x0004U
#define E100_CB_COMMAND_EL BIT(15)
#define E100_CB_COMMAND_S BIT(14)
#define E100_CB_COMMAND_I BIT(13)
#define E100_CB_STATUS_C BIT(15)
#define E100_CB_STATUS_OK BIT(13)
#define E100_RFD_STATUS_MULTICAST BIT(1)
#define E100_RFD_STATUS_TYPE BIT(5)
#define E100_RFD_COUNT_F BIT(14)
#define E100_RFD_COUNT_EOF BIT(15)
#define E100_CB_STATUS_COMPLETE \
    (E100_CB_STATUS_C | E100_CB_STATUS_OK)
#define E100_CB_LINK_OFFSET 0x04U
#define E100_TX_TBD_ARRAY_OFFSET 0x08U
#define E100_TX_BYTE_COUNT_OFFSET 0x0cU
#define E100_CB_PAYLOAD_OFFSET 0x10U
#define E100_MCAST_COUNT_OFFSET 0x08U
#define E100_MCAST_LIST_OFFSET 0x0aU
#define E100_RFD_COUNT_OFFSET 0x0cU
#define E100_RFD_SIZE_OFFSET 0x0eU
#define E100_TEST_FRAME_BYTES 64U
#define E100_TEST_POLL_LIMIT 5000U
#define E100_TEST_SOCKET_TIMEOUT_MS 5000
#define E100_TEST_TX_CB UINT64_C(0x00500000)
#define E100_TEST_MCAST_CB UINT64_C(0x00501000)
#define E100_TEST_SUSPEND_CB UINT64_C(0x00502000)
#define E100_TEST_MIG_TX_CB UINT64_C(0x00502100)
#define E100_TEST_MIG_POISON_CB UINT64_C(0x00502200)
#define E100_TEST_RX_RFD UINT64_C(0x00503000)
#define E100_TEST_RESET_RFD UINT64_C(0x00504000)
#define E100_MDI_READY BIT(28)
#define E100_MDI_OPCODE_READ (2U << 26)
#define E100_MDI_PHY_1 (1U << 21)
#define E100_MDI_BMSR 1U
#define E100_MDI_VENDOR_REGISTER 16U
#define E100_MII_BMSR_LINK_STATUS 0x0004U
#define E100_MII_BMSR_AUTONEG_COMPLETE 0x0020U

#define E100_EEPROM_SK BIT(0)
#define E100_EEPROM_CS BIT(1)
#define E100_EEPROM_DI BIT(2)
#define E100_EEPROM_DO BIT(3)
#define E100_EEPROM_OPCODE_EWEN 0U
#define E100_EEPROM_OPCODE_EWDS 0U
#define E100_EEPROM_OPCODE_WRITE 1U
#define E100_EEPROM_OPCODE_READ 2U
#define E100_EEPROM_OPCODE_ERASE 3U
#define E100_EEPROM_EWEN_ADDRESS 0x30U
#define E100_EEPROM_EWDS_ADDRESS 0x00U
#define E100_EEPROM_ADDRESS_BITS 6U
#define E100_EEPROM_MIGRATION_WORD 0x10U
#define E100_EEPROM_MIGRATION_VALUE UINT16_C(0xa55a)

G_STATIC_ASSERT(IA64_I2000_IO_TEST_I82559_PARENT_ROOT == 0);
G_STATIC_ASSERT(IA64_I2000_IO_TEST_I82559_EEPROM_WORDS ==
                BIT(E100_EEPROM_ADDRESS_BITS));
G_STATIC_ASSERT(!IA64_I2000_IO_TEST_I82559_OPTION_ROM_ENABLED);

static QTestState *io_test_start_with_options(const char *options)
{
    return qtest_initf("-machine ia64-vpc,i8042=off,nvram=none "
                       "-S -smp 1 -m 2G -nodefaults "
                       "-display none -net none -device %s,id=%s %s",
                       TYPE_IA64_I2000_IO_QTEST, IA64_I2000_IO_QTEST_ID,
                       options ?: "");
}

static QTestState *io_test_start(void)
{
    return io_test_start_with_options(NULL);
}

static QTestState *io_test_start_with_socket(const char *options, int *host_fd)
{
    QTestState *qts;
    int sockets[2];

    g_assert_cmpint(qemu_socketpair(PF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    qemu_clear_cloexec(sockets[1]);
    qts = qtest_initf("-machine ia64-vpc,i8042=off,nvram=none "
                      "-smp 1 -m 2G -nodefaults "
                      "-display none -netdev socket,fd=%d,id=i2000net "
                      "-device %s,id=%s,%s=i2000net %s",
                      sockets[1], TYPE_IA64_I2000_IO_QTEST,
                      IA64_I2000_IO_QTEST_ID,
                      IA64_I2000_IO_TEST_PROP_I82559_NETDEV, options ?: "");
    close(sockets[1]);
    *host_fd = sockets[0];
    return qts;
}

static bool io_test_socket_receive_all(int fd, void *buffer, size_t length)
{
    uint8_t *next = buffer;

    while (length) {
        GPollFD poll_fd = {
            .fd = fd,
            .events = G_IO_IN,
        };
        ssize_t received;

        if (g_poll(&poll_fd, 1, E100_TEST_SOCKET_TIMEOUT_MS) != 1 ||
            !(poll_fd.revents & G_IO_IN)) {
            return false;
        }
        received = recv(fd, next, length, 0);
        if (received <= 0) {
            return false;
        }
        next += received;
        length -= received;
    }
    return true;
}

static void io_test_socket_send_frame(int fd, const uint8_t *frame,
                                      size_t length)
{
    uint32_t framed_length = htonl(length);

    g_assert_cmpint(qemu_write_full(fd, &framed_length, sizeof(framed_length)),
                    ==, sizeof(framed_length));
    g_assert_cmpint(qemu_write_full(fd, frame, length), ==, length);
}

static void io_test_i82559_make_frame(uint8_t frame[E100_TEST_FRAME_BYTES],
                                      const uint8_t destination[ETH_ALEN],
                                      uint8_t tag)
{
    static const uint8_t source[ETH_ALEN] = {
        0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc,
    };
    unsigned int i;

    memcpy(frame, destination, ETH_ALEN);
    memcpy(frame + ETH_ALEN, source, ETH_ALEN);
    frame[12] = 0x08;
    frame[13] = 0x00;
    for (i = 14; i < E100_TEST_FRAME_BYTES; i++) {
        frame[i] = tag + i;
    }
}

/* Ethernet CRC selects the multicast-hash bit tested by the device. */
static unsigned int
io_test_i82559_multicast_hash(const uint8_t address[ETH_ALEN])
{
    uint32_t crc = UINT32_MAX;
    unsigned int byte;

    for (byte = 0; byte < ETH_ALEN; byte++) {
        uint8_t value = address[byte];
        unsigned int bit;

        for (bit = 0; bit < 8; bit++) {
            bool carry = !!(crc & BIT(31)) ^ !!(value & BIT(0));

            crc <<= 1;
            value >>= 1;
            if (carry) {
                crc = (crc ^ UINT32_C(0x04c11db6)) | 1;
            }
        }
    }
    return (crc >> 2) & 0x3f;
}

static void
io_test_i82559_prepare_tx(QTestState *qts, uint64_t address, uint16_t command,
                          uint32_t link,
                          const uint8_t frame[E100_TEST_FRAME_BYTES])
{
    qtest_memset(qts, address, 0,
                 E100_CB_PAYLOAD_OFFSET + E100_TEST_FRAME_BYTES);
    qtest_writew(qts, address + 2, command);
    qtest_writel(qts, address + E100_CB_LINK_OFFSET, link);
    qtest_writel(qts, address + E100_TX_TBD_ARRAY_OFFSET, UINT32_MAX);
    qtest_writew(qts, address + E100_TX_BYTE_COUNT_OFFSET,
                 E100_TEST_FRAME_BYTES);
    qtest_memwrite(qts, address + E100_CB_PAYLOAD_OFFSET, frame,
                   E100_TEST_FRAME_BYTES);
}

static void io_test_i82559_prepare_multicast(QTestState *qts, uint64_t address,
                                             const uint8_t multicast[ETH_ALEN])
{
    qtest_memset(qts, address, 0, E100_CB_PAYLOAD_OFFSET);
    qtest_writew(qts, address + 2, E100_CB_COMMAND_MCAST | E100_CB_COMMAND_EL);
    qtest_writew(qts, address + E100_MCAST_COUNT_OFFSET, ETH_ALEN);
    qtest_memwrite(qts, address + E100_MCAST_LIST_OFFSET, multicast, ETH_ALEN);
}

static void io_test_i82559_prepare_rfd(QTestState *qts, uint64_t address,
                                       uint32_t link)
{
    qtest_memset(qts, address, 0,
                 E100_CB_PAYLOAD_OFFSET + E100_TEST_FRAME_BYTES);
    qtest_writew(qts, address + 2, E100_CB_COMMAND_S);
    qtest_writel(qts, address + E100_CB_LINK_OFFSET, link);
    qtest_writew(qts, address + E100_RFD_SIZE_OFFSET, E100_TEST_FRAME_BYTES);
}

static void io_test_i82559_issue_command(QTestState *qts, uint32_t pointer,
                                         uint8_t command)
{
    qtest_writel(qts, I82559_MMIO_BASE + E100_SCB_POINTER, pointer);
    qtest_writeb(qts, I82559_MMIO_BASE + E100_SCB_COMMAND, command);
}

static uint16_t io_test_i82559_wait_descriptor(QTestState *qts,
                                               uint64_t address)
{
    unsigned int i;

    for (i = 0; i < E100_TEST_POLL_LIMIT; i++) {
        uint16_t status = qtest_readw(qts, address);

        if (status & E100_CB_STATUS_C) {
            return status;
        }
        qtest_clock_step(qts, 1000);
        g_usleep(1000);
    }
    g_error("i82559 descriptor at 0x%" PRIx64 " did not complete", address);
    return 0;
}

static void io_test_i82559_dispatch_packets(QTestState *qts)
{
    unsigned int i;

    for (i = 0; i < 50; i++) {
        qtest_clock_step(qts, 1000);
        g_usleep(1000);
    }
}

static uint64_t io_test_io_pa(uint16_t port)
{
    uint64_t offset = ((uint64_t)(port >> 2) << 12) | (port & 0xfffU);

    return IA64_I2000_460GX_TEST_LEGACY_IO_BASE + offset;
}

static uint8_t io_test_inb(QTestState *qts, uint16_t port)
{
    return qtest_readb(qts, io_test_io_pa(port));
}

static uint16_t io_test_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, io_test_io_pa(port));
}

static void io_test_outb(QTestState *qts, uint16_t port, uint8_t value)
{
    qtest_writeb(qts, io_test_io_pa(port), value);
}

static void io_test_outw(QTestState *qts, uint16_t port, uint16_t value)
{
    qtest_writew(qts, io_test_io_pa(port), value);
}

static void io_test_pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN, value);
}

static uint32_t io_test_pid_read(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOREGSEL, reg);
    return qtest_readl(qts, IA64_I2000_460GX_TEST_PID_BASE + PID_IOWIN);
}

static uint32_t io_test_pid_rte_low(unsigned pin)
{
    return PID_RTE_BASE + pin * 2;
}

static uint32_t io_test_pid_rte_high(unsigned pin)
{
    return io_test_pid_rte_low(pin) + 1;
}

static void io_test_pic_init(QTestState *qts, uint8_t master_mask,
                             uint8_t slave_mask)
{
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE, 0x11);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE, 0x11);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1, 0x20);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1, 0x28);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1, 0x04);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1, 0x02);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1, 0x01);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1, 0x01);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1, master_mask);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1, slave_mask);
}

static uint32_t io_test_config_address_at(unsigned bus, unsigned slot,
                                          unsigned function, unsigned reg)
{
    return UINT32_C(0x80000000) | (bus & 0xff) << 16 |
           PCI_DEVFN(slot, function) << 8 | (reg & 0xfc);
}

static void io_test_config_select_at(QTestState *qts, unsigned bus,
                                     unsigned slot, unsigned function,
                                     unsigned reg)
{
    qtest_writel(qts, IA64_I2000_460GX_TEST_CF8_PA,
                 io_test_config_address_at(bus, slot, function, reg));
}

static uint8_t io_test_config_readb_at(QTestState *qts, unsigned bus,
                                       unsigned slot, unsigned function,
                                       unsigned reg)
{
    io_test_config_select_at(qts, bus, slot, function, reg);
    return qtest_readb(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint16_t io_test_config_readw_at(QTestState *qts, unsigned bus,
                                        unsigned slot, unsigned function,
                                        unsigned reg)
{
    io_test_config_select_at(qts, bus, slot, function, reg);
    return qtest_readw(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static uint32_t io_test_config_readl_at(QTestState *qts, unsigned bus,
                                        unsigned slot, unsigned function,
                                        unsigned reg)
{
    io_test_config_select_at(qts, bus, slot, function, reg);
    return qtest_readl(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3));
}

static void io_test_config_writew_at(QTestState *qts, unsigned bus,
                                     unsigned slot, unsigned function,
                                     unsigned reg, uint16_t value)
{
    io_test_config_select_at(qts, bus, slot, function, reg);
    qtest_writew(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static void io_test_config_writel_at(QTestState *qts, unsigned bus,
                                     unsigned slot, unsigned function,
                                     unsigned reg, uint32_t value)
{
    io_test_config_select_at(qts, bus, slot, function, reg);
    qtest_writel(qts, IA64_I2000_460GX_TEST_CFC_PA + (reg & 3), value);
}

static uint8_t io_test_config_readb(QTestState *qts, unsigned function,
                                    unsigned reg)
{
    return io_test_config_readb_at(qts, 0, IA64_I2000_IO_TEST_PCI_SLOT,
                                   function, reg);
}

static uint16_t io_test_config_readw(QTestState *qts, unsigned function,
                                     unsigned reg)
{
    return io_test_config_readw_at(qts, 0, IA64_I2000_IO_TEST_PCI_SLOT,
                                   function, reg);
}

static uint32_t io_test_config_readl(QTestState *qts, unsigned function,
                                     unsigned reg)
{
    return io_test_config_readl_at(qts, 0, IA64_I2000_IO_TEST_PCI_SLOT,
                                   function, reg);
}

static void io_test_config_writel(QTestState *qts, unsigned function,
                                  unsigned reg, uint32_t value)
{
    io_test_config_writel_at(qts, 0, IA64_I2000_IO_TEST_PCI_SLOT, function, reg,
                             value);
}

static uint8_t io_test_i82559_config_readb(QTestState *qts, unsigned reg)
{
    return io_test_config_readb_at(qts, I82559_PCI_BUS,
                                   IA64_I2000_IO_TEST_I82559_SLOT,
                                   IA64_I2000_IO_TEST_I82559_FUNCTION, reg);
}

static uint16_t io_test_i82559_config_readw(QTestState *qts, unsigned reg)
{
    return io_test_config_readw_at(qts, I82559_PCI_BUS,
                                   IA64_I2000_IO_TEST_I82559_SLOT,
                                   IA64_I2000_IO_TEST_I82559_FUNCTION, reg);
}

static uint32_t io_test_i82559_config_readl(QTestState *qts, unsigned reg)
{
    return io_test_config_readl_at(qts, I82559_PCI_BUS,
                                   IA64_I2000_IO_TEST_I82559_SLOT,
                                   IA64_I2000_IO_TEST_I82559_FUNCTION, reg);
}

static void io_test_i82559_config_writew(QTestState *qts, unsigned reg,
                                         uint16_t value)
{
    io_test_config_writew_at(qts, I82559_PCI_BUS,
                             IA64_I2000_IO_TEST_I82559_SLOT,
                             IA64_I2000_IO_TEST_I82559_FUNCTION, reg, value);
}

static void io_test_i82559_config_writel(QTestState *qts, unsigned reg,
                                         uint32_t value)
{
    io_test_config_writel_at(qts, I82559_PCI_BUS,
                             IA64_I2000_IO_TEST_I82559_SLOT,
                             IA64_I2000_IO_TEST_I82559_FUNCTION, reg, value);
}

static void io_test_i82559_program_bars(QTestState *qts)
{
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_0, I82559_MMIO_BASE);
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_1, I82559_IO_BASE);
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_2, I82559_FLASH_BASE);
    io_test_i82559_config_writew(qts, PCI_COMMAND,
                                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                                     PCI_COMMAND_MASTER);
}

static void io_test_i82559_eeprom_set_lines(QTestState *qts, uint8_t lines)
{
    qtest_writew(qts, I82559_MMIO_BASE + E100_SCB_EEPROM, lines);
}

static void io_test_i82559_eeprom_clock_out(QTestState *qts, bool bit)
{
    uint8_t lines = E100_EEPROM_CS | (bit ? E100_EEPROM_DI : 0);

    io_test_i82559_eeprom_set_lines(qts, lines);
    io_test_i82559_eeprom_set_lines(qts, lines | E100_EEPROM_SK);
    io_test_i82559_eeprom_set_lines(qts, lines);
}

static bool io_test_i82559_eeprom_clock_in(QTestState *qts)
{
    uint32_t combined;
    bool bit;

    io_test_i82559_eeprom_set_lines(qts, E100_EEPROM_CS);
    io_test_i82559_eeprom_set_lines(qts, E100_EEPROM_CS | E100_EEPROM_SK);
    combined = qtest_readl(qts, I82559_MMIO_BASE + E100_SCB_FLASH);
    bit = qtest_readw(qts, I82559_MMIO_BASE + E100_SCB_EEPROM) & E100_EEPROM_DO;
    g_assert_cmphex((combined >> 16) & E100_EEPROM_DO, ==,
                    bit ? E100_EEPROM_DO : 0);
    io_test_i82559_eeprom_set_lines(qts, E100_EEPROM_CS);
    return bit;
}

static void io_test_i82559_eeprom_command(QTestState *qts, unsigned opcode,
                                          unsigned address)
{
    int bit;

    g_assert_cmpuint(opcode, <, 4);
    g_assert_cmpuint(address, <, IA64_I2000_IO_TEST_I82559_EEPROM_WORDS);

    io_test_i82559_eeprom_set_lines(qts, 0);
    io_test_i82559_eeprom_set_lines(qts, E100_EEPROM_CS);
    io_test_i82559_eeprom_clock_out(qts, false);
    io_test_i82559_eeprom_clock_out(qts, true);
    io_test_i82559_eeprom_clock_out(qts, opcode & BIT(1));
    io_test_i82559_eeprom_clock_out(qts, opcode & BIT(0));
    for (bit = E100_EEPROM_ADDRESS_BITS - 1; bit >= 0; bit--) {
        io_test_i82559_eeprom_clock_out(qts, address & BIT(bit));
    }
}

static void io_test_i82559_eeprom_end_command(QTestState *qts)
{
    io_test_i82559_eeprom_set_lines(qts, 0);
}

static uint16_t io_test_i82559_eeprom_read_word(QTestState *qts,
                                                unsigned address)
{
    uint16_t value = 0;
    unsigned bit;

    io_test_i82559_eeprom_command(qts, E100_EEPROM_OPCODE_READ, address);
    for (bit = 0; bit < 16; bit++) {
        value = value << 1 | io_test_i82559_eeprom_clock_in(qts);
    }
    io_test_i82559_eeprom_end_command(qts);
    return value;
}

static void io_test_i82559_eeprom_simple_command(QTestState *qts,
                                                 unsigned opcode,
                                                 unsigned address)
{
    io_test_i82559_eeprom_command(qts, opcode, address);
    io_test_i82559_eeprom_end_command(qts);
}

static void io_test_i82559_eeprom_write_word(QTestState *qts, unsigned address,
                                             uint16_t value)
{
    int bit;

    /* The EEPROM commits erase/write operations when CS falls. */
    io_test_i82559_eeprom_simple_command(qts, E100_EEPROM_OPCODE_EWEN,
                                         E100_EEPROM_EWEN_ADDRESS);
    io_test_i82559_eeprom_simple_command(qts, E100_EEPROM_OPCODE_ERASE,
                                         address);
    io_test_i82559_eeprom_command(qts, E100_EEPROM_OPCODE_WRITE, address);
    for (bit = 15; bit >= 0; bit--) {
        io_test_i82559_eeprom_clock_out(qts, value & BIT(bit));
    }
    io_test_i82559_eeprom_end_command(qts);
    io_test_i82559_eeprom_simple_command(qts, E100_EEPROM_OPCODE_EWDS,
                                         E100_EEPROM_EWDS_ADDRESS);
}

static uint16_t io_test_i82559_eeprom_checksum(QTestState *qts)
{
    uint32_t sum = 0;
    unsigned word;

    for (word = 0; word < IA64_I2000_IO_TEST_I82559_EEPROM_WORDS; word++) {
        sum += io_test_i82559_eeprom_read_word(qts, word);
    }
    return sum & UINT16_MAX;
}

static uint16_t io_test_i82559_read_mdi(QTestState *qts, unsigned reg)
{
    uint32_t mdi = E100_MDI_OPCODE_READ | E100_MDI_PHY_1 | (reg << 16);

    g_assert_cmpuint(reg, <, 32);
    qtest_writel(qts, I82559_MMIO_BASE + E100_SCB_MDI, mdi);
    mdi = qtest_readl(qts, I82559_MMIO_BASE + E100_SCB_MDI);
    g_assert_cmphex(mdi & E100_MDI_READY, ==, E100_MDI_READY);
    return mdi;
}

static void io_test_i82559_route_intx(QTestState *qts, uint8_t vector)
{
    unsigned pin = IA64_I2000_IO_TEST_I82559_PID_PIN;

    /* An absent destination keeps delivery pending. */
    io_test_pid_write(qts, io_test_pid_rte_high(pin), 0x0f000000);
    io_test_pid_write(qts, io_test_pid_rte_low(pin),
                      vector | PID_RTE_TRIGGER_LEVEL);
}

static void io_test_i82559_assert_swi(QTestState *qts)
{
    qtest_writeb(qts, I82559_MMIO_BASE + E100_SCB_INTMASK, E100_SWI_REQUEST);
}

static void io_test_i82559_assert_irq_state(QTestState *qts, uint8_t causes)
{
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK) & causes,
                    ==, causes);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT,
                    ==, PCI_STATUS_INTERRUPT);
    g_assert_cmphex(
        io_test_pid_read(
            qts, io_test_pid_rte_low(IA64_I2000_IO_TEST_I82559_PID_PIN)) &
            PID_RTE_DELIVERY_STATUS,
        !=, 0);
}

static void io_test_i82559_ack_irq(QTestState *qts, uint8_t causes)
{
    qtest_writeb(qts, I82559_MMIO_BASE + E100_SCB_ACK, causes);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK) & causes,
                    ==, 0);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT,
                    ==, 0);
    g_assert_cmphex(
        io_test_pid_read(
            qts, io_test_pid_rte_low(IA64_I2000_IO_TEST_I82559_PID_PIN)) &
            PID_RTE_DELIVERY_STATUS,
        ==, 0);
}

static void io_test_i82559_assert_swi_state(QTestState *qts)
{
    io_test_i82559_assert_irq_state(qts, E100_SWI_ACK);
}

static void io_test_i82559_ack_swi(QTestState *qts)
{
    io_test_i82559_ack_irq(qts, E100_SWI_ACK);
}

static void sio_enter(QTestState *qts)
{
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, LPC47B27_CONFIG_ENTER_KEY);
}

static void sio_select_ldn(QTestState *qts, uint8_t ldn)
{
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, LPC47B27_CONFIG_CR_LDN);
    io_test_outb(qts, LPC47B27_CONFIG_DATA_PORT, ldn);
}

static void sio_write(QTestState *qts, uint8_t reg, uint8_t value)
{
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, reg);
    io_test_outb(qts, LPC47B27_CONFIG_DATA_PORT, value);
}

static uint8_t sio_read(QTestState *qts, uint8_t reg)
{
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, reg);
    return io_test_inb(qts, LPC47B27_CONFIG_DATA_PORT);
}

static void test_pci_functions(void)
{
    QTestState *qts = io_test_start();

    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_F0_VENDOR_ID);
    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_DEVICE_ID), ==,
                    IA64_I2000_IO_TEST_F0_DEVICE_ID);
    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_CLASS_DEVICE), ==,
                    IA64_I2000_IO_TEST_F0_CLASS);
    g_assert_cmphex(io_test_config_readb(qts, 0, PCI_REVISION_ID), ==,
                    IA64_I2000_IO_TEST_F0_REVISION);
    g_assert_cmphex(io_test_config_readb(qts, 0, PCI_CLASS_PROG), ==,
                    IA64_I2000_IO_TEST_F0_PROG_IF);
    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_F0_SUBSYSTEM_VENDOR_ID);
    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_SUBSYSTEM_ID), ==,
                    IA64_I2000_IO_TEST_F0_SUBSYSTEM_ID);
    g_assert_cmphex(io_test_config_readb(qts, 0, PCI_HEADER_TYPE) &
                        PCI_HEADER_TYPE_MULTI_FUNCTION,
                    !=, 0);

    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_F1_VENDOR_ID);
    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_DEVICE_ID), ==,
                    IA64_I2000_IO_TEST_F1_DEVICE_ID);
    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_CLASS_DEVICE), ==,
                    IA64_I2000_IO_TEST_F1_CLASS);
    g_assert_cmphex(io_test_config_readb(qts, 1, PCI_CLASS_PROG), ==,
                    IA64_I2000_IO_TEST_F1_PROG_IF);
    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_SUBSYSTEM_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_F1_SUBSYSTEM_VENDOR_ID);
    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_SUBSYSTEM_ID), ==,
                    IA64_I2000_IO_TEST_F1_SUBSYSTEM_ID);
    g_assert_cmphex(io_test_config_readw(qts, 2, PCI_VENDOR_ID), ==, 0xffff);

    /* Undefined function-specific config is immutable zero. */
    g_assert_cmphex(io_test_config_readl(qts, 0, 0x40), ==, 0);
    io_test_config_writel(qts, 0, 0x40, UINT32_MAX);
    g_assert_cmphex(io_test_config_readl(qts, 0, 0x40), ==, 0);
    g_assert_cmphex(io_test_config_readl(qts, 1, 0x40), ==, 0);
    io_test_config_writel(qts, 1, 0x40, UINT32_MAX);
    g_assert_cmphex(io_test_config_readl(qts, 1, 0x40), ==, 0);

    /* Both functions expose fixed legacy decode and no BARs. */
    io_test_config_writel(qts, 0, PCI_COMMAND, UINT32_MAX);
    g_assert_cmphex(io_test_config_readw(qts, 0, PCI_COMMAND), ==, 0);
    io_test_config_writel(qts, 1, PCI_COMMAND, UINT32_MAX);
    g_assert_cmphex(io_test_config_readw(qts, 1, PCI_COMMAND), ==, 0);

    qtest_quit(qts);
}

static void test_i82559(void)
{
    QTestState *qts = io_test_start();

    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_VENDOR_ID), ==,
                    IA64_I2000_IO_TEST_I82559_VENDOR_ID);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_DEVICE_ID), ==,
                    IA64_I2000_IO_TEST_I82559_DEVICE_ID);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_CLASS_DEVICE), ==,
                    IA64_I2000_IO_TEST_I82559_CLASS);
    g_assert_cmphex(io_test_i82559_config_readb(qts, PCI_REVISION_ID), ==,
                    IA64_I2000_IO_TEST_I82559_REVISION);
    g_assert_cmphex(io_test_i82559_config_readb(qts, PCI_CLASS_PROG), ==,
                    IA64_I2000_IO_TEST_I82559_PROG_IF);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_SUBSYSTEM_VENDOR_ID),
                    ==, IA64_I2000_IO_TEST_I82559_SUBSYSTEM_VENDOR_ID);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_SUBSYSTEM_ID), ==,
                    IA64_I2000_IO_TEST_I82559_SUBSYSTEM_ID);
    g_assert_cmphex(io_test_i82559_config_readb(qts, PCI_INTERRUPT_PIN), ==,
                    IA64_I2000_IO_TEST_I82559_INTERRUPT_PIN);
    g_assert_cmphex(io_test_i82559_config_readb(qts, PCI_HEADER_TYPE), ==,
                    PCI_HEADER_TYPE_NORMAL);

    io_test_i82559_config_writew(qts, PCI_COMMAND, 0);
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_0, UINT32_MAX);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    ~(IA64_I2000_IO_TEST_I82559_MMIO_BAR_SIZE - 1));
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_1, UINT32_MAX);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    ~(IA64_I2000_IO_TEST_I82559_IO_BAR_SIZE - 1) |
                        PCI_BASE_ADDRESS_SPACE_IO);
    io_test_i82559_config_writel(qts, PCI_BASE_ADDRESS_2, UINT32_MAX);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_2), ==,
                    ~(IA64_I2000_IO_TEST_I82559_FLASH_BAR_SIZE - 1));

    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_ROM_ADDRESS), ==, 0);
    io_test_i82559_config_writel(qts, PCI_ROM_ADDRESS, UINT32_MAX);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_ROM_ADDRESS), ==, 0);

    io_test_i82559_program_bars(qts);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    I82559_MMIO_BASE);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    I82559_IO_BASE | PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_2), ==,
                    I82559_FLASH_BASE);
    g_assert_cmphex(
        io_test_i82559_config_readw(qts, PCI_COMMAND) &
            (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER),
        ==, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    g_assert_cmphex(
        io_test_i82559_read_mdi(qts, E100_MDI_BMSR) &
            (E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE),
        ==, E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE);
    qtest_qmp_assert_success(qts, "{'execute':'set_link','arguments':"
                                  "{'name':'i82559c.0','up':false}}");
    g_assert_cmphex(
        io_test_i82559_read_mdi(qts, E100_MDI_BMSR) &
            (E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE),
        ==, 0);

    /* A device reset must preserve the externally selected backend link. */
    qtest_system_reset(qts);
    io_test_i82559_program_bars(qts);
    g_assert_cmphex(
        io_test_i82559_read_mdi(qts, E100_MDI_BMSR) &
            (E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE),
        ==, 0);

    qtest_qmp_assert_success(qts, "{'execute':'set_link','arguments':"
                                  "{'name':'i82559c.0','up':true}}");
    g_assert_cmphex(
        io_test_i82559_read_mdi(qts, E100_MDI_BMSR) &
            (E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE),
        ==, E100_MII_BMSR_LINK_STATUS | E100_MII_BMSR_AUTONEG_COMPLETE);
    g_assert_cmphex(io_test_i82559_read_mdi(qts, E100_MDI_VENDOR_REGISTER), ==,
                    0x0003);

    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 0), ==,
                    IA64_I2000_IO_TEST_I82559_MAC_WORD0);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 1), ==,
                    IA64_I2000_IO_TEST_I82559_MAC_WORD1);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 2), ==,
                    IA64_I2000_IO_TEST_I82559_MAC_WORD2);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 3), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_COMPATIBILITY);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 5), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_CONTROLLER);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 6), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_PHY);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 0x0a), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_ID);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 0x0b), ==,
                    IA64_I2000_IO_TEST_I82559_SUBSYSTEM_ID);
    g_assert_cmphex(io_test_i82559_eeprom_read_word(qts, 0x0c), ==,
                    IA64_I2000_IO_TEST_I82559_SUBSYSTEM_VENDOR_ID);
    g_assert_cmphex(io_test_i82559_eeprom_checksum(qts), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM);

    io_test_i82559_route_intx(qts, 0x60);
    io_test_i82559_assert_swi(qts);
    io_test_i82559_assert_swi_state(qts);

    /* A system reset must drop the asserted INTx and clear SCB state. */
    qtest_system_reset(qts);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT,
                    ==, 0);
    io_test_i82559_program_bars(qts);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK) &
                        E100_SWI_ACK,
                    ==, 0);
    g_assert_cmphex(io_test_i82559_eeprom_checksum(qts), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM);

    io_test_i82559_route_intx(qts, 0x61);
    io_test_i82559_assert_swi(qts);
    io_test_i82559_assert_swi_state(qts);
    io_test_i82559_ack_swi(qts);

    qtest_quit(qts);
}

static void test_i82559_tx_rx_multicast_reset(void)
{
    static const uint8_t allowed_multicast[ETH_ALEN] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x01,
    };
    static const uint8_t denied_multicast[ETH_ALEN] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x02,
    };
    static const uint8_t device_mac[ETH_ALEN] = {
        (uint8_t)IA64_I2000_IO_TEST_I82559_MAC_WORD0,
        (uint8_t)(IA64_I2000_IO_TEST_I82559_MAC_WORD0 >> 8),
        (uint8_t)IA64_I2000_IO_TEST_I82559_MAC_WORD1,
        (uint8_t)(IA64_I2000_IO_TEST_I82559_MAC_WORD1 >> 8),
        (uint8_t)IA64_I2000_IO_TEST_I82559_MAC_WORD2,
        (uint8_t)(IA64_I2000_IO_TEST_I82559_MAC_WORD2 >> 8),
    };
    uint8_t tx_frame[E100_TEST_FRAME_BYTES];
    uint8_t allowed_frame[E100_TEST_FRAME_BYTES];
    uint8_t denied_frame[E100_TEST_FRAME_BYTES];
    uint8_t unicast_frame[E100_TEST_FRAME_BYTES];
    uint8_t received[E100_TEST_FRAME_BYTES];
    uint8_t rx_data[E100_TEST_FRAME_BYTES];
    uint32_t framed_length;
    uint16_t status;
    QTestState *qts;
    int socket_fd;

    g_assert_cmpuint(io_test_i82559_multicast_hash(allowed_multicast), !=,
                     io_test_i82559_multicast_hash(denied_multicast));
    io_test_i82559_make_frame(tx_frame, device_mac, 0x10);
    io_test_i82559_make_frame(allowed_frame, allowed_multicast, 0x20);
    io_test_i82559_make_frame(denied_frame, denied_multicast, 0x30);
    io_test_i82559_make_frame(unicast_frame, device_mac, 0x40);

    qts = io_test_start_with_socket(NULL, &socket_fd);
    io_test_i82559_program_bars(qts);
    io_test_i82559_route_intx(qts, 0x63);

    io_test_i82559_prepare_tx(qts, E100_TEST_TX_CB,
                              E100_CB_COMMAND_TX | E100_CB_COMMAND_EL |
                                  E100_CB_COMMAND_I,
                              0, tx_frame);
    io_test_i82559_issue_command(qts, E100_TEST_TX_CB, E100_CU_START);
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_TX_CB);
    g_assert_cmphex(status, ==, E100_CB_STATUS_COMPLETE);
    io_test_i82559_assert_irq_state(qts, E100_ACK_CX | E100_ACK_CNA);
    g_assert_true(io_test_socket_receive_all(socket_fd, &framed_length,
                                             sizeof(framed_length)));
    g_assert_cmpuint(ntohl(framed_length), ==, sizeof(tx_frame));
    g_assert_true(
        io_test_socket_receive_all(socket_fd, received, sizeof(received)));
    g_assert_cmpmem(received, sizeof(received), tx_frame, sizeof(tx_frame));
    io_test_i82559_ack_irq(qts, E100_ACK_CX | E100_ACK_CNA);

    io_test_i82559_prepare_multicast(qts, E100_TEST_MCAST_CB,
                                     allowed_multicast);
    io_test_i82559_issue_command(qts, E100_TEST_MCAST_CB, E100_CU_START);
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_MCAST_CB);
    g_assert_cmphex(status, ==, E100_CB_STATUS_COMPLETE);
    io_test_i82559_assert_irq_state(qts, E100_ACK_CNA);
    io_test_i82559_ack_irq(qts, E100_ACK_CNA);

    io_test_i82559_prepare_rfd(qts, E100_TEST_RX_RFD, E100_TEST_RX_RFD);
    io_test_i82559_issue_command(qts, E100_TEST_RX_RFD, E100_RU_START);
    io_test_socket_send_frame(socket_fd, denied_frame, sizeof(denied_frame));
    io_test_i82559_dispatch_packets(qts);
    g_assert_cmphex(qtest_readw(qts, E100_TEST_RX_RFD), ==, 0);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK), ==, 0);

    io_test_socket_send_frame(socket_fd, allowed_frame, sizeof(allowed_frame));
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_RX_RFD);
    g_assert_cmphex(status, ==,
                    E100_CB_STATUS_COMPLETE | E100_RFD_STATUS_MULTICAST |
                        E100_RFD_STATUS_TYPE);
    g_assert_cmpuint(
        qtest_readw(qts, E100_TEST_RX_RFD + E100_RFD_COUNT_OFFSET), ==,
        sizeof(allowed_frame) | E100_RFD_COUNT_F | E100_RFD_COUNT_EOF);
    qtest_memread(qts, E100_TEST_RX_RFD + E100_CB_PAYLOAD_OFFSET, rx_data,
                  sizeof(rx_data));
    g_assert_cmpmem(rx_data, sizeof(rx_data), allowed_frame,
                    sizeof(allowed_frame));
    io_test_i82559_assert_irq_state(qts, E100_ACK_FR);

    /* Reset clears CU/RU state, the multicast hash, and asserted INTx. */
    qtest_system_reset(qts);
    g_assert_cmphex(io_test_i82559_config_readw(qts, PCI_STATUS) &
                        PCI_STATUS_INTERRUPT,
                    ==, 0);
    io_test_i82559_program_bars(qts);
    io_test_i82559_route_intx(qts, 0x64);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_STATUS), ==,
                    0);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK), ==, 0);

    io_test_i82559_prepare_rfd(qts, E100_TEST_RESET_RFD, E100_TEST_RESET_RFD);
    io_test_i82559_issue_command(qts, E100_TEST_RESET_RFD, E100_RU_START);
    io_test_socket_send_frame(socket_fd, allowed_frame, sizeof(allowed_frame));
    io_test_i82559_dispatch_packets(qts);
    g_assert_cmphex(qtest_readw(qts, E100_TEST_RESET_RFD), ==, 0);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK), ==, 0);

    io_test_socket_send_frame(socket_fd, unicast_frame, sizeof(unicast_frame));
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_RESET_RFD);
    g_assert_cmphex(status, ==, E100_CB_STATUS_COMPLETE | E100_RFD_STATUS_TYPE);
    qtest_memread(qts, E100_TEST_RESET_RFD + E100_CB_PAYLOAD_OFFSET, rx_data,
                  sizeof(rx_data));
    g_assert_cmpmem(rx_data, sizeof(rx_data), unicast_frame,
                    sizeof(unicast_frame));
    io_test_i82559_assert_irq_state(qts, E100_ACK_FR);
    io_test_i82559_ack_irq(qts, E100_ACK_FR);

    qtest_quit(qts);
    close(socket_fd);
}

static void test_lpc_dynamic_decode(void)
{
    QTestState *qts = io_test_start();
    uint8_t uart_hole;
    uint8_t i8042_data_hole;

    g_assert_cmphex(io_test_inb(qts, LPC47B27_CONFIG_INDEX_PORT), ==, 0xff);
    uart_hole = io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7);
    i8042_data_hole = io_test_inb(qts, IA64_I2000_IO_TEST_I8042_DATA_BASE);
    io_test_outb(qts, IA64_I2000_IO_TEST_UART_BASE + 7, 0xc3);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7), ==,
                    uart_hole);

    sio_enter(qts);
    g_assert_cmphex(sio_read(qts, LPC47B27_CONFIG_CR_DEVICE_ID), ==,
                    LPC47B27_CONFIG_DEVICE_ID);

    sio_select_ldn(qts, LPC47B27_CONFIG_LDN_SERIAL1);
    sio_write(qts, LPC47B27_CONFIG_CR_BASE_MSB, 0x03);
    sio_write(qts, LPC47B27_CONFIG_CR_BASE_LSB, 0xf8);
    sio_write(qts, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 4);
    sio_write(qts, LPC47B27_CONFIG_CR_ACTIVATE, 1);
    io_test_outb(qts, IA64_I2000_IO_TEST_UART_BASE + 7, 0x5a);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7), ==,
                    0x5a);

    sio_write(qts, LPC47B27_CONFIG_CR_BASE_MSB, 0x02);
    sio_write(qts, LPC47B27_CONFIG_CR_BASE_LSB, 0xf8);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7), ==,
                    uart_hole);
    io_test_outb(qts, 0x02f8 + 7, 0xa5);
    g_assert_cmphex(io_test_inb(qts, 0x02f8 + 7), ==, 0xa5);

    sio_select_ldn(qts, LPC47B27_CONFIG_LDN_KEYBOARD);
    sio_write(qts, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 1);
    sio_write(qts, LPC47B27_CONFIG_CR_SECONDARY_IRQ, 12);
    sio_write(qts, LPC47B27_CONFIG_CR_ACTIVATE, 1);
    io_test_outb(qts, IA64_I2000_IO_TEST_I8042_COMMAND_BASE,
                 I8042_CCMD_SELF_TEST);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_I8042_DATA_BASE), ==,
                    I8042_SELF_TEST_OK);
    sio_write(qts, LPC47B27_CONFIG_CR_ACTIVATE, 0);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_I8042_DATA_BASE), ==,
                    i8042_data_hole);

    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, LPC47B27_CONFIG_EXIT_KEY);
    g_assert_cmphex(io_test_inb(qts, LPC47B27_CONFIG_DATA_PORT), ==, 0xff);
    qtest_quit(qts);
}

static void test_pic_rtc_and_reset(void)
{
    QTestState *qts = io_test_start();

    /* Pair-local PIC and ELCR decodes are in root-0 ISA I/O. */
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1, 0x5a);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1, 0xa5);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_PIC_MASTER_BASE + 1),
                    ==, 0x5a);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_PIC_SLAVE_BASE + 1), ==,
                    0xa5);

    /* Bank 1 is independent SRAM and does not expose clock registers. */
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE, 0x20);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE + 1, 0x11);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE, 0x20);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1, 0x22);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE, 0x20);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE + 1), ==,
                    0x11);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE, 0x20);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1), ==,
                    0x22);

    qtest_system_reset(qts);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE, 0x20);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1), ==,
                    0x22);
    qtest_quit(qts);
}

static void test_primary_atapi_pio(void)
{
    QTestState *qts = io_test_start();
    uint16_t identify[256];
    uint8_t status;
    unsigned i;

    g_assert_cmphex(io_test_config_readb(qts, 1, PCI_CLASS_PROG), ==,
                    IA64_I2000_IO_TEST_F1_PROG_IF);
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7,
                 ATA_CMD_IDENTIFY_PACKET);
    status = io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7);
    g_assert_cmphex(status & ATA_SR_BSY, ==, 0);
    g_assert_cmphex(status & ATA_SR_DRQ, !=, 0);
    for (i = 0; i < G_N_ELEMENTS(identify); i++) {
        identify[i] = io_test_inw(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE);
    }
    g_assert_cmphex(identify[0] & 0x8000, !=, 0);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7) &
                        ATA_SR_DRQ,
                    ==, 0);
    qtest_quit(qts);
}

static void test_atapi_dma_abort_and_legacy_irq(void)
{
    static const uint8_t inquiry[12] = {
        0x12, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0,
    };
    QTestState *qts = io_test_start();
    uint8_t status;
    unsigned i;

    /* Leave delivery pending by targeting an absent local SAPIC ID. */
    io_test_pid_write(qts, io_test_pid_rte_high(0), 0x0f000000);
    io_test_pid_write(qts, io_test_pid_rte_low(0), 0x50);

    /* Unmask only the slave cascade and the IDE device's IRQ 14. */
    io_test_pic_init(qts, 0xfb, 0xbf);

    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 6, 0);
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 1, 1);
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 4, 0);
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 5, 0x08);
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7, ATA_CMD_PACKET);
    status = io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7);
    g_assert_cmphex(status & (ATA_SR_BSY | ATA_SR_DRQ | ATA_SR_ERR), ==,
                    ATA_SR_DRQ);

    for (i = 0; i < G_N_ELEMENTS(inquiry); i += 2) {
        io_test_outw(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE,
                     inquiry[i] | inquiry[i + 1] << 8);
    }

    status = io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7);
    g_assert_cmphex(status & (ATA_SR_BSY | ATA_SR_DRQ | ATA_SR_ERR), ==,
                    ATA_SR_ERR);
    g_assert_cmphex(status & ATA_SR_DRDY, !=, 0);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 1) &
                        ATA_ER_ABRT,
                    ==, ATA_ER_ABRT);

    /* IDE IRQ14 -> slave PIC -> master cascade -> PID legacy pin 0. */
    g_assert_cmphex(io_test_pid_read(qts, io_test_pid_rte_low(0)) &
                        PID_RTE_DELIVERY_STATUS,
                    !=, 0);
    qtest_quit(qts);
}

static void io_test_wait_for_migration_complete(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 120 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result =
            qtest_qmp_assert_success_ref(qts, "{'execute':'query-migrate'}");
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

static void io_test_migration_file_cleanup(gpointer opaque)
{
    char *path = opaque;

    g_unlink(path);
    g_free(path);
}

static void test_io_test_migration(void)
{
    char *path = g_strdup_printf("%s/ia64-i2000-io-test-migration.XXXXXX",
                                 g_get_tmp_dir());
    g_autofree char *uri = NULL;
    QTestState *qts;
    uint8_t old_uart_hole;
    uint16_t pit_count;
    uint8_t status;
    unsigned i;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(io_test_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    qts = io_test_start();
    old_uart_hole = io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7);

    io_test_pid_write(qts, io_test_pid_rte_high(0), 0x0f000000);
    io_test_pid_write(qts, io_test_pid_rte_low(0), 0x51);
    io_test_pic_init(qts, 0xeb, 0xff);

    sio_enter(qts);
    sio_select_ldn(qts, LPC47B27_CONFIG_LDN_SERIAL1);
    sio_write(qts, LPC47B27_CONFIG_CR_BASE_MSB, 0x02);
    sio_write(qts, LPC47B27_CONFIG_CR_BASE_LSB, 0xf8);
    sio_write(qts, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 4);
    sio_write(qts, LPC47B27_CONFIG_CR_ACTIVATE, 1);
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, LPC47B27_CONFIG_EXIT_KEY);
    io_test_outb(qts, 0x02f8 + 7, 0xa5);
    io_test_outb(qts, 0x02f8 + 1, 0x02);

    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE, 0x34);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1, 0x6d);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE, 0x20);
    io_test_outb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE + 1, 0x4b);

    /* Channel 2 is gated off, leaving a stopped PIT count. */
    io_test_outb(qts, IA64_I2000_IO_TEST_PIT_BASE + 3, 0xb0);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIT_BASE + 2, 0x34);
    io_test_outb(qts, IA64_I2000_IO_TEST_PIT_BASE + 2, 0x12);

    /* Migrate an ATAPI PIO data phase after consuming eight words. */
    io_test_outb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7,
                 ATA_CMD_IDENTIFY_PACKET);
    status = io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7);
    g_assert_cmphex(status & ATA_SR_DRQ, !=, 0);
    for (i = 0; i < 8; i++) {
        io_test_inw(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE);
    }

    /*
     * Exercise the 93xx write protocol, then let the NIC's software reset
     * regenerate its checksum without discarding the modified word.
     */
    io_test_i82559_program_bars(qts);
    io_test_i82559_eeprom_write_word(qts, E100_EEPROM_MIGRATION_WORD,
                                     E100_EEPROM_MIGRATION_VALUE);
    g_assert_cmphex(
        io_test_i82559_eeprom_read_word(qts, E100_EEPROM_MIGRATION_WORD), ==,
        E100_EEPROM_MIGRATION_VALUE);
    qtest_writel(qts, I82559_MMIO_BASE + E100_SCB_PORT, 0);
    g_assert_cmphex(
        io_test_i82559_eeprom_read_word(qts, E100_EEPROM_MIGRATION_WORD), ==,
        E100_EEPROM_MIGRATION_VALUE);
    g_assert_cmphex(io_test_i82559_eeprom_checksum(qts), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM);

    io_test_i82559_route_intx(qts, 0x62);
    io_test_i82559_assert_swi(qts);
    io_test_i82559_assert_swi_state(qts);

    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1), ==,
                    0x6d);
    g_assert_cmphex(io_test_pid_read(qts, io_test_pid_rte_low(0)) &
                        PID_RTE_DELIVERY_STATUS,
                    !=, 0);

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    io_test_wait_for_migration_complete(qts);
    qtest_quit(qts);

    qts = io_test_start_with_options("-incoming defer");
    qtest_qmp_assert_success(qts,
                             "{'execute':'migrate-incoming','arguments':"
                             "{'uri':%s,'exit-on-error':false}}",
                             uri);
    io_test_wait_for_migration_complete(qts);

    /* PCI decode, EEPROM contents, and the asserted SWI all migrate. */
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_0), ==,
                    I82559_MMIO_BASE);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    I82559_IO_BASE | PCI_BASE_ADDRESS_SPACE_IO);
    g_assert_cmphex(io_test_i82559_config_readl(qts, PCI_BASE_ADDRESS_2), ==,
                    I82559_FLASH_BASE);
    g_assert_cmphex(
        io_test_i82559_config_readw(qts, PCI_COMMAND) &
            (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER),
        ==, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(
        io_test_i82559_eeprom_read_word(qts, E100_EEPROM_MIGRATION_WORD), ==,
        E100_EEPROM_MIGRATION_VALUE);
    g_assert_cmphex(io_test_i82559_eeprom_checksum(qts), ==,
                    IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM);
    io_test_i82559_assert_swi_state(qts);
    io_test_i82559_ack_swi(qts);

    /* Both selected RTC indices and their independent SRAM bytes migrate. */
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK0_BASE + 1), ==,
                    0x4b);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_RTC_BANK1_BASE + 1), ==,
                    0x6d);

    io_test_outb(qts, IA64_I2000_IO_TEST_PIT_BASE + 3, 0x80);
    pit_count = io_test_inb(qts, IA64_I2000_IO_TEST_PIT_BASE + 2);
    pit_count |= io_test_inb(qts, IA64_I2000_IO_TEST_PIT_BASE + 2) << 8;
    g_assert_cmphex(pit_count, ==, 0x1234);

    for (i = 8; i < 256; i++) {
        io_test_inw(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE);
    }
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_IDE_COMMAND_BASE + 7) &
                        ATA_SR_DRQ,
                    ==, 0);
    g_assert_cmphex(io_test_inb(qts, IA64_I2000_IO_TEST_UART_BASE + 7), ==,
                    old_uart_hole);
    g_assert_cmphex(io_test_inb(qts, 0x02f8 + 7), ==, 0xa5);

    sio_enter(qts);
    sio_select_ldn(qts, LPC47B27_CONFIG_LDN_SERIAL1);
    g_assert_cmphex(sio_read(qts, LPC47B27_CONFIG_CR_ACTIVATE), ==, 1);
    g_assert_cmphex(sio_read(qts, LPC47B27_CONFIG_CR_BASE_MSB), ==, 0x02);
    g_assert_cmphex(sio_read(qts, LPC47B27_CONFIG_CR_BASE_LSB), ==, 0xf8);
    g_assert_cmphex(sio_read(qts, LPC47B27_CONFIG_CR_PRIMARY_IRQ), ==, 4);
    io_test_outb(qts, LPC47B27_CONFIG_INDEX_PORT, LPC47B27_CONFIG_EXIT_KEY);

    g_assert_cmphex(io_test_inb(qts, 0x02f8 + 2) & 0x0f, ==, 0x02);
    g_assert_cmphex(io_test_pid_read(qts, io_test_pid_rte_low(0)) &
                        PID_RTE_DELIVERY_STATUS,
                    !=, 0);
    qtest_quit(qts);
}

static void test_i82559_traffic_migration(void)
{
    static const uint8_t allowed_multicast[ETH_ALEN] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x01,
    };
    static const uint8_t denied_multicast[ETH_ALEN] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x02,
    };
    uint8_t tx_frame[E100_TEST_FRAME_BYTES];
    uint8_t allowed_frame[E100_TEST_FRAME_BYTES];
    uint8_t denied_frame[E100_TEST_FRAME_BYTES];
    uint8_t received[E100_TEST_FRAME_BYTES];
    uint8_t rx_data[E100_TEST_FRAME_BYTES];
    char *path = g_strdup_printf(
        "%s/ia64-i2000-io-test-i82559-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    uint32_t framed_length;
    uint16_t status;
    QTestState *qts;
    int socket_fd;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(io_test_migration_file_cleanup, path);
    uri = g_strdup_printf("file:%s", path);

    io_test_i82559_make_frame(tx_frame, allowed_multicast, 0x50);
    io_test_i82559_make_frame(allowed_frame, allowed_multicast, 0x60);
    io_test_i82559_make_frame(denied_frame, denied_multicast, 0x70);

    qts = io_test_start_with_socket(NULL, &socket_fd);
    io_test_i82559_program_bars(qts);
    io_test_i82559_route_intx(qts, 0x65);

    io_test_i82559_prepare_multicast(qts, E100_TEST_MCAST_CB,
                                     allowed_multicast);
    io_test_i82559_issue_command(qts, E100_TEST_MCAST_CB, E100_CU_START);
    g_assert_cmphex(io_test_i82559_wait_descriptor(qts, E100_TEST_MCAST_CB), ==,
                    E100_CB_STATUS_COMPLETE);
    io_test_i82559_ack_irq(qts, E100_ACK_CNA);

    io_test_i82559_prepare_tx(qts, E100_TEST_MIG_TX_CB,
                              E100_CB_COMMAND_TX | E100_CB_COMMAND_EL |
                                  E100_CB_COMMAND_I,
                              0, tx_frame);
    qtest_memset(qts, E100_TEST_SUSPEND_CB, 0, E100_CB_PAYLOAD_OFFSET);
    qtest_writew(qts, E100_TEST_SUSPEND_CB + 2, E100_CB_COMMAND_S);
    qtest_writel(qts, E100_TEST_SUSPEND_CB + E100_CB_LINK_OFFSET,
                 E100_TEST_MIG_TX_CB);
    io_test_i82559_issue_command(qts, E100_TEST_SUSPEND_CB, E100_CU_START);
    g_assert_cmphex(io_test_i82559_wait_descriptor(qts, E100_TEST_SUSPEND_CB),
                    ==, E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(qtest_readw(qts, E100_TEST_MIG_TX_CB), ==, 0);

    io_test_i82559_prepare_rfd(qts, E100_TEST_RX_RFD, E100_TEST_RX_RFD);
    io_test_i82559_issue_command(qts, E100_TEST_RX_RFD, E100_RU_START);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_STATUS) &
                        (E100_CU_STATE_SUSPENDED | E100_RU_STATE_READY),
                    ==, E100_CU_STATE_SUSPENDED | E100_RU_STATE_READY);
    io_test_i82559_assert_irq_state(qts, E100_ACK_CNA);

    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    io_test_wait_for_migration_complete(qts);
    qtest_quit(qts);
    close(socket_fd);

    qts = io_test_start_with_socket("-incoming defer", &socket_fd);
    qtest_qmp_assert_success(qts,
                             "{'execute':'migrate-set-parameters','arguments':"
                             "{'announce-rounds':0}}");
    qtest_qmp_assert_success(qts,
                             "{'execute':'migrate-incoming','arguments':"
                             "{'uri':%s,'exit-on-error':false}}",
                             uri);
    io_test_wait_for_migration_complete(qts);

    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_STATUS) &
                        (E100_CU_STATE_SUSPENDED | E100_RU_STATE_READY),
                    ==, E100_CU_STATE_SUSPENDED | E100_RU_STATE_READY);
    io_test_i82559_assert_irq_state(qts, E100_ACK_CNA);
    io_test_i82559_ack_irq(qts, E100_ACK_CNA);

    /* Migration preserves the saved link instead of rereading the CB. */
    qtest_memset(qts, E100_TEST_MIG_POISON_CB, 0, E100_CB_PAYLOAD_OFFSET);
    qtest_writew(qts, E100_TEST_MIG_POISON_CB + 2, E100_CB_COMMAND_S);
    qtest_writel(qts, E100_TEST_SUSPEND_CB + E100_CB_LINK_OFFSET,
                 E100_TEST_MIG_POISON_CB);
    qtest_writew(qts, E100_TEST_SUSPEND_CB + 2, 0);
    qtest_writeb(qts, I82559_MMIO_BASE + E100_SCB_COMMAND, E100_CU_RESUME);
    g_assert_cmphex(qtest_readw(qts, E100_TEST_MIG_POISON_CB), ==, 0);
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_MIG_TX_CB);
    g_assert_cmphex(status, ==, E100_CB_STATUS_COMPLETE);
    io_test_i82559_assert_irq_state(qts, E100_ACK_CX | E100_ACK_CNA);
    g_assert_true(io_test_socket_receive_all(socket_fd, &framed_length,
                                             sizeof(framed_length)));
    g_assert_cmpuint(ntohl(framed_length), ==, sizeof(tx_frame));
    g_assert_true(
        io_test_socket_receive_all(socket_fd, received, sizeof(received)));
    g_assert_cmpmem(received, sizeof(received), tx_frame, sizeof(tx_frame));
    io_test_i82559_ack_irq(qts, E100_ACK_CX | E100_ACK_CNA);

    io_test_socket_send_frame(socket_fd, denied_frame, sizeof(denied_frame));
    io_test_i82559_dispatch_packets(qts);
    g_assert_cmphex(qtest_readw(qts, E100_TEST_RX_RFD), ==, 0);
    g_assert_cmphex(qtest_readb(qts, I82559_MMIO_BASE + E100_SCB_ACK), ==, 0);

    io_test_socket_send_frame(socket_fd, allowed_frame, sizeof(allowed_frame));
    status = io_test_i82559_wait_descriptor(qts, E100_TEST_RX_RFD);
    g_assert_cmphex(status, ==,
                    E100_CB_STATUS_COMPLETE | E100_RFD_STATUS_MULTICAST |
                        E100_RFD_STATUS_TYPE);
    g_assert_cmpuint(
        qtest_readw(qts, E100_TEST_RX_RFD + E100_RFD_COUNT_OFFSET), ==,
        sizeof(allowed_frame) | E100_RFD_COUNT_F | E100_RFD_COUNT_EOF);
    qtest_memread(qts, E100_TEST_RX_RFD + E100_CB_PAYLOAD_OFFSET, rx_data,
                  sizeof(rx_data));
    g_assert_cmpmem(rx_data, sizeof(rx_data), allowed_frame,
                    sizeof(allowed_frame));
    io_test_i82559_assert_irq_state(qts, E100_ACK_FR);
    io_test_i82559_ack_irq(qts, E100_ACK_FR);

    qtest_quit(qts);
    close(socket_fd);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("i2000-io-test/pci", test_pci_functions);
    qtest_add_func("i2000-io-test/i82559", test_i82559);
    qtest_add_func("i2000-io-test/i82559-tx-rx-multicast-reset",
                   test_i82559_tx_rx_multicast_reset);
    qtest_add_func("i2000-io-test/lpc", test_lpc_dynamic_decode);
    qtest_add_func("i2000-io-test/pic-rtc-reset", test_pic_rtc_and_reset);
    qtest_add_func("i2000-io-test/atapi-pio", test_primary_atapi_pio);
    qtest_add_func("i2000-io-test/atapi-dma-abort",
                   test_atapi_dma_abort_and_legacy_irq);
    qtest_add_func("i2000-io-test/migration", test_io_test_migration);
    qtest_add_func("i2000-io-test/i82559-traffic-migration",
                   test_i82559_traffic_migration);

    return g_test_run();
}
