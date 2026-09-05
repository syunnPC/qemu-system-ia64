/*
 * Intel 460GX Programmable Interrupt Device qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/bitops.h"
#include "qemu/units.h"
#include "libqtest.h"
#include "hw/ia64/intel_460gx_pid.h"

#define PID_TEST_BASE                  UINT64_C(0x80200000)
#define PID_QOM_PATH                   "/machine/peripheral/pid0"
#define IA64_PIB_BASE                  UINT64_C(0xfee00000)
#define IA64_PIB_XTP                   (IA64_PIB_BASE + UINT64_C(0x1e0008))

#define PID_IOREGSEL                   0x00
#define PID_IOWIN                      0x10
#define PID_EOI                        0x40

#define PID_REG_ID                     0x00
#define PID_REG_VERSION                0x01
#define PID_REG_ARB_ID                 0x02
#define PID_RTE_BASE                   0x10

#define PID_RTE_VECTOR                 UINT32_C(0x000000ff)
#define PID_RTE_REDIRECTION_HINT       BIT(8)
#define PID_RTE_RESERVED_11            BIT(11)
#define PID_RTE_DELIVERY_STATUS        BIT(12)
#define PID_RTE_POLARITY_LOW           BIT(13)
#define PID_RTE_REMOTE_IRR             BIT(14)
#define PID_RTE_TRIGGER_LEVEL          BIT(15)
#define PID_RTE_MASKED                 BIT(16)
#define PID_RTE_FLUSH_ENABLE           BIT(17)

#define PID_DELIVERY_MODE(value)       ((value) << 8)
#define PID_DELIVERY_NMI               PID_DELIVERY_MODE(4)
#define PID_DELIVERY_EXTINT            PID_DELIVERY_MODE(7)

#define SAPIC_STATE_IRR                BIT(8)
#define SAPIC_STATE_ISR                BIT(9)
#define SAPIC_STATE_PMI                BIT(10)
#define SAPIC_STATE_INIT               BIT(11)

static QTestState *pid_start_cpus(const char *device_properties,
                                  const char *extra_args, unsigned int cpus)
{
    return qtest_initf("-machine ia64-vpc,nvram=none "
                       "-m 256M -smp %u -S "
                       "-device %s,id=pid0,x-test-mmio-base=0x%" PRIx64
                       "%s %s",
                       cpus, TYPE_INTEL_460GX_PID, PID_TEST_BASE,
                       device_properties ?: "", extra_args ?: "");
}

static QTestState *pid_start(const char *device_properties,
                             const char *extra_args)
{
    return pid_start_cpus(device_properties, extra_args, 1);
}

static QTestState *pid_start_with_wiring(const char *extra_args,
                                         uint8_t initial_id,
                                         uint32_t legacy_pin,
                                         uint64_t test_mmio_base)
{
    return qtest_initf("-machine ia64-vpc,nvram=none "
                       "-m 256M -smp 1 -S "
                       "-device %s,id=pid0,initial-id=%u,legacy-pin=%u,"
                       "x-test-mmio-base=0x%" PRIx64 " %s",
                       TYPE_INTEL_460GX_PID, initial_id, legacy_pin,
                       test_mmio_base, extra_args ?: "");
}

static void pid_select(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, PID_TEST_BASE + PID_IOREGSEL, reg);
}

static uint32_t pid_read(QTestState *qts, uint32_t reg)
{
    pid_select(qts, reg);
    return qtest_readl(qts, PID_TEST_BASE + PID_IOWIN);
}

static void pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    pid_select(qts, reg);
    qtest_writel(qts, PID_TEST_BASE + PID_IOWIN, value);
}

static uint32_t pid_rte_low(unsigned pin)
{
    return PID_RTE_BASE + pin * 2;
}

static uint32_t pid_rte_high(unsigned pin)
{
    return pid_rte_low(pin) + 1;
}

static bool sapic_irr_has_vector(QTestState *qts, uint8_t vector)
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

static bool sapic_irr_wait_for_vector(QTestState *qts, uint8_t vector)
{
    unsigned attempt;

    for (attempt = 0; attempt < 1000; attempt++) {
        if (sapic_irr_has_vector(qts, vector)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static void pid_pulse(QTestState *qts, const char *name, unsigned pin)
{
    qtest_set_irq_in(qts, PID_QOM_PATH, name, pin, 1);
    qtest_set_irq_in(qts, PID_QOM_PATH, name, pin, 0);
}

static void test_registers_and_negative(void)
{
    QTestState *qts = pid_start(NULL, NULL);
    static const uint8_t reserved_registers[] = {
        0x03, 0x0f, 0x90, 0xff,
    };
    unsigned i;

    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_IOREGSEL), ==, 0);
    g_assert_cmphex(pid_read(qts, PID_REG_VERSION), ==, 0x003f0021);

    /* VERSION is read-only and ID implements only ID[3:0] plus SAPIC DT. */
    pid_write(qts, PID_REG_VERSION, 0xffffffff);
    g_assert_cmphex(pid_read(qts, PID_REG_VERSION), ==, 0x003f0021);
    pid_write(qts, PID_REG_ID, 0xffffffff);
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x0f008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x0f000000);

    for (i = 0; i < G_N_ELEMENTS(reserved_registers); i++) {
        pid_write(qts, reserved_registers[i], 0xffffffff);
        g_assert_cmphex(pid_read(qts, reserved_registers[i]), ==, 0);
    }

    /* IOREGSEL implements only bits 7:0. */
    qtest_writel(qts, PID_TEST_BASE + PID_IOREGSEL, 0xdeadbe90);
    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_IOREGSEL), ==,
                    0x90);
    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_IOWIN), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_EOI), ==, 0);

    /* Reserved RTE bits are hard zero and status/RIRR are read-only. */
    pid_write(qts, pid_rte_low(0), 0xffffffff);
    g_assert_cmphex(pid_read(qts, pid_rte_low(0)), ==, 0x0003a7ff);
    pid_write(qts, pid_rte_high(0), 0xffffffff);
    g_assert_cmphex(pid_read(qts, pid_rte_high(0)), ==, 0xffff0000);

    qtest_quit(qts);
}

static void test_all_redirection_entries(void)
{
    uint32_t expected_low[INTEL_460GX_PID_NUM_PINS];
    uint32_t expected_high[INTEL_460GX_PID_NUM_PINS];
    QTestState *qts = pid_start(NULL, NULL);
    unsigned pin;

    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        uint8_t id = 0x80 | pin;
        uint8_t eid = 0x40 ^ pin;
        uint32_t low = (0x20 + pin) | PID_RTE_MASKED |
                       PID_RTE_FLUSH_ENABLE;
        uint32_t high = ((uint32_t)id << 24) |
                        ((uint32_t)eid << 16);

        if (pin & 1) {
            low |= PID_RTE_REDIRECTION_HINT;
        }
        if (pin % 3 == 0) {
            low |= PID_RTE_TRIGGER_LEVEL;
        } else if (pin % 3 == 1) {
            low |= PID_RTE_POLARITY_LOW;
        }

        expected_low[pin] = low;
        expected_high[pin] = high;
        pid_write(qts, pid_rte_low(pin), low | BIT(31) | BIT(19));
        pid_write(qts, pid_rte_high(pin), high | 0x55aa);
    }

    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        g_assert_cmphex(pid_read(qts, pid_rte_low(pin)), ==,
                        expected_low[pin]);
        g_assert_cmphex(pid_read(qts, pid_rte_high(pin)), ==,
                        expected_high[pin]);
    }

    qtest_quit(qts);
}

static void test_reset(void)
{
    QTestState *qts = pid_start(",initial-id=7", NULL);
    unsigned pin;

    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x07008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x07000000);

    pid_write(qts, PID_REG_ID, 0x0f000000);
    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        pid_write(qts, pid_rte_low(pin), 0x40 + pin);
        pid_write(qts, pid_rte_high(pin), 0xa55a0000);
    }
    pid_select(qts, 0x8f);

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_IOREGSEL), ==, 0);
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x07008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x07000000);
    g_assert_cmphex(pid_read(qts, PID_REG_VERSION), ==, 0x003f0021);
    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        g_assert_cmphex(pid_read(qts, pid_rte_low(pin)), ==,
                        PID_RTE_MASKED);
        g_assert_cmphex(pid_read(qts, pid_rte_high(pin)), ==, 0);
    }

    qtest_quit(qts);
}

static void test_level_polarity_and_eoi(void)
{
    const unsigned high_pin = 62;
    const unsigned low_pin = 63;
    const uint8_t vector = 0x50;
    QTestState *qts = pid_start(NULL, NULL);

    pid_write(qts, pid_rte_low(high_pin),
              vector | PID_RTE_TRIGGER_LEVEL);
    pid_write(qts, pid_rte_low(low_pin),
              vector | PID_RTE_TRIGGER_LEVEL | PID_RTE_POLARITY_LOW);

    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     high_pin, 1);
    g_assert_cmphex(pid_read(qts, pid_rte_low(high_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR);
    g_assert_cmphex(pid_read(qts, pid_rte_low(low_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     low_pin, 1);
    g_assert_cmphex(pid_read(qts, pid_rte_low(low_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR);

    /* A shared-vector EOI resamples both asserted pins and redelivers. */
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(high_pin)) &
                    PID_RTE_REMOTE_IRR, !=, 0);
    g_assert_cmphex(pid_read(qts, pid_rte_low(low_pin)) &
                    PID_RTE_REMOTE_IRR, !=, 0);

    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     high_pin, 0);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     low_pin, 0);
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(high_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);
    g_assert_cmphex(pid_read(qts, pid_rte_low(low_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    qtest_quit(qts);
}

static void test_sapic_delivery_and_hint(void)
{
    const uint8_t fixed_vector = 0x51;
    const uint8_t hint_vector = 0x52;
    QTestState *qts = pid_start(NULL, NULL);

    pid_write(qts, pid_rte_low(0), fixed_vector);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 0);
    g_assert_true(sapic_irr_wait_for_vector(qts, fixed_vector));

    pid_write(qts, pid_rte_low(1),
              hint_vector | PID_RTE_REDIRECTION_HINT);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 1);
    /* In SAPIC mode bit 8 is a redirection hint; delivery remains fixed. */
    g_assert_true(sapic_irr_wait_for_vector(qts, hint_vector));

    pid_write(qts, pid_rte_low(2), 0x7f | PID_DELIVERY_NMI);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 2);
    g_assert_true(sapic_irr_wait_for_vector(qts, 2));

    pid_write(qts, pid_rte_low(3), 0x7f | PID_DELIVERY_EXTINT);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 3);
    g_assert_true(sapic_irr_wait_for_vector(qts, 0));

    qtest_quit(qts);
}

static void test_mask_destination_and_route(void)
{
    const uint8_t masked_vector = 0x53;
    const uint8_t wrong_dest_vector = 0x54;
    const uint8_t reserved_dest_vector = 0x55;
    const uint8_t low_edge_vector = 0x56;
    const uint8_t masked_level_vector = 0x59;
    const uint8_t pending_edge_vector = 0x5a;
    const uint8_t pending_level_vector = 0x5b;
    const uint8_t transition_vector = 0x5d;
    const uint8_t accepted_level_vector = 0x5e;
    const uint8_t masked_edge_to_level_vector = 0x60;
    const uint8_t masked_level_to_edge_vector = 0x61;
    QTestState *qts = pid_start(NULL, NULL);

    pid_write(qts, pid_rte_low(4), masked_vector | PID_RTE_MASKED);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 4);
    pid_write(qts, pid_rte_low(4), masked_vector);
    g_assert_false(sapic_irr_has_vector(qts, masked_vector));

    /* Masked level entries neither deliver nor latch Delivery Status. */
    pid_write(qts, pid_rte_low(8), masked_level_vector |
              PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 8, 1);
    g_assert_false(sapic_irr_has_vector(qts, masked_level_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(8)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    /* Unmasking resamples the asserted level and performs fixed delivery. */
    pid_write(qts, pid_rte_low(8),
              masked_level_vector | PID_RTE_TRIGGER_LEVEL);
    g_assert_true(sapic_irr_wait_for_vector(qts, masked_level_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(8)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR);

    pid_write(qts, pid_rte_high(5), 0xaa550000);
    pid_write(qts, pid_rte_low(5),
              wrong_dest_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 5, 1);
    g_assert_false(sapic_irr_has_vector(qts, wrong_dest_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(5)) & PID_RTE_REMOTE_IRR,
                    ==, 0);

    pid_write(qts, pid_rte_low(6),
              reserved_dest_vector | PID_RTE_RESERVED_11);
    g_assert_cmphex(pid_read(qts, pid_rte_low(6)) & PID_RTE_RESERVED_11,
                    ==, 0);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 6);
    g_assert_true(sapic_irr_wait_for_vector(qts, reserved_dest_vector));

    pid_write(qts, pid_rte_low(7),
              low_edge_vector | PID_RTE_POLARITY_LOW);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 7, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, low_edge_vector));
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 7, 0);

    /* An undeliverable edge remains pending across mask/destination writes. */
    pid_write(qts, pid_rte_high(9), 0xaa550000);
    pid_write(qts, pid_rte_low(9), pending_edge_vector);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 9);
    g_assert_false(sapic_irr_has_vector(qts, pending_edge_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(9)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
    pid_write(qts, pid_rte_low(9),
              pending_edge_vector | PID_RTE_MASKED);
    g_assert_cmphex(pid_read(qts, pid_rte_low(9)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
    pid_write(qts, pid_rte_high(9), 0);
    g_assert_false(sapic_irr_has_vector(qts, pending_edge_vector));
    pid_write(qts, pid_rte_low(9), pending_edge_vector);
    g_assert_true(sapic_irr_wait_for_vector(qts, pending_edge_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(9)) &
                    PID_RTE_DELIVERY_STATUS, ==, 0);

    /* A level latched before masking is held even if the pin deasserts. */
    pid_write(qts, pid_rte_high(10), 0xaa550000);
    pid_write(qts, pid_rte_low(10),
              pending_level_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 10, 1);
    g_assert_false(sapic_irr_has_vector(qts, pending_level_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(10)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS);
    pid_write(qts, pid_rte_low(10), pending_level_vector |
              PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 10, 0);
    g_assert_cmphex(pid_read(qts, pid_rte_low(10)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
    pid_write(qts, pid_rte_high(10), 0);
    pid_write(qts, pid_rte_low(10),
              pending_level_vector | PID_RTE_TRIGGER_LEVEL);
    g_assert_true(sapic_irr_wait_for_vector(qts, pending_level_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(10)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_REMOTE_IRR);

    /* Changing an undelivered edge to level leaves the level latch clear. */
    pid_write(qts, pid_rte_high(11), 0xaa550000);
    pid_write(qts, pid_rte_low(11), transition_vector);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 11);
    g_assert_cmphex(pid_read(qts, pid_rte_low(11)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
    pid_write(qts, pid_rte_low(11),
              transition_vector | PID_RTE_TRIGGER_LEVEL);
    g_assert_cmphex(pid_read(qts, pid_rte_low(11)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);
    pid_write(qts, pid_rte_high(11), 0);
    g_assert_false(sapic_irr_has_vector(qts, transition_vector));

    /* Masking an accepted level retains RIRR, not a second pending latch. */
    pid_write(qts, pid_rte_low(12),
              accepted_level_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 12, 1);
    g_assert_true(sapic_irr_wait_for_vector(qts, accepted_level_vector));
    pid_write(qts, pid_rte_low(12), accepted_level_vector |
              PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    g_assert_cmphex(pid_read(qts, pid_rte_low(12)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_REMOTE_IRR);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 12, 0);
    g_assert_cmphex(pid_read(qts, pid_rte_low(12)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_REMOTE_IRR);
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, accepted_level_vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(12)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);
    pid_write(qts, pid_rte_low(12), accepted_level_vector |
              PID_RTE_TRIGGER_LEVEL);
    g_assert_cmphex(pid_read(qts, pid_rte_low(12)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    /* Changing an accepted level to edge never synthesizes an edge. */
    pid_write(qts, pid_rte_low(13),
              (accepted_level_vector + 1) | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 13, 1);
    g_assert_cmphex(pid_read(qts, pid_rte_low(13)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR);
    pid_write(qts, pid_rte_low(13), accepted_level_vector + 1);
    g_assert_cmphex(pid_read(qts, pid_rte_low(13)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    /* A request latched before masking survives edge-to-level rewriting. */
    pid_write(qts, pid_rte_high(14), 0xaa550000);
    pid_write(qts, pid_rte_low(14), masked_edge_to_level_vector);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 14);
    pid_write(qts, pid_rte_low(14),
              masked_edge_to_level_vector | PID_RTE_MASKED);
    pid_write(qts, pid_rte_low(14), masked_edge_to_level_vector |
              PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    g_assert_cmphex(pid_read(qts, pid_rte_low(14)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS);
    pid_write(qts, pid_rte_high(14), 0);
    pid_write(qts, pid_rte_low(14),
              masked_edge_to_level_vector | PID_RTE_TRIGGER_LEVEL);
    g_assert_true(sapic_irr_wait_for_vector(qts,
                                            masked_edge_to_level_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(14)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_REMOTE_IRR);

    /* The same mask guarantee holds when a pending level becomes an edge. */
    pid_write(qts, pid_rte_high(15), 0xaa550000);
    pid_write(qts, pid_rte_low(15),
              masked_level_to_edge_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 15, 1);
    pid_write(qts, pid_rte_low(15), masked_level_to_edge_vector |
              PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, 15, 0);
    pid_write(qts, pid_rte_low(15),
              masked_level_to_edge_vector | PID_RTE_MASKED);
    g_assert_cmphex(pid_read(qts, pid_rte_low(15)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_DELIVERY_STATUS);
    pid_write(qts, pid_rte_high(15), 0);
    pid_write(qts, pid_rte_low(15), masked_level_to_edge_vector);
    g_assert_true(sapic_irr_wait_for_vector(qts,
                                            masked_level_to_edge_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(15)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==, 0);

    qtest_quit(qts);
}

static int64_t sapic_cpu_operation(QTestState *qts, const char *operation,
                                   unsigned int cpu, uint8_t value)
{
    return qtest_ia64_sapic(qts, operation, cpu, value, 0, 0, 0);
}

static int64_t sapic_deliver(QTestState *qts, unsigned int destination_mode,
                             uint8_t id, uint8_t eid,
                             unsigned int delivery_mode, bool redirect,
                             uint8_t vector)
{
    return qtest_ia64_sapic(qts, "deliver", destination_mode,
                            ((uint16_t)id << 8) | eid, delivery_mode,
                            redirect, vector);
}

static uint64_t sapic_cpu_state(QTestState *qts, unsigned int cpu,
                                uint8_t vector)
{
    return sapic_cpu_operation(qts, "state", cpu, vector);
}

static void test_smp_delivery_accept_and_eoi(void)
{
    const uint8_t reserved_vector = 0x62;
    const uint8_t priority_vector = 0x63;
    const uint8_t disabled_vector = 0x64;
    const uint8_t level_vector = 0x65;
    const uint8_t no_redirect_vector = 0x66;
    const unsigned int level_pin = 12;
    QTestState *qts = pid_start_cpus(NULL, NULL, 4);
    uint64_t state;
    unsigned int cpu;

    pid_write(qts, pid_rte_high(0), UINT32_C(0x05000000));
    pid_write(qts, pid_rte_low(0),
              reserved_vector | PID_RTE_RESERVED_11);
    g_assert_cmphex(pid_read(qts, pid_rte_low(0)) & PID_RTE_RESERVED_11,
                    ==, 0);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 0);
    for (cpu = 0; cpu < 4; cpu++) {
        state = sapic_cpu_state(qts, cpu, reserved_vector);
        g_assert_cmphex(state & SAPIC_STATE_IRR, ==, 0);
    }

    g_assert_cmphex(sapic_cpu_operation(qts, "xtp", 0, 8), ==, 8);
    g_assert_cmphex(sapic_cpu_operation(qts, "xtp", 1, 0x80), ==, 0x80);
    g_assert_cmphex(sapic_cpu_operation(qts, "xtp", 2, 2), ==, 2);
    g_assert_cmphex(sapic_cpu_operation(qts, "xtp", 3, 2), ==, 2);
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "pib-write", 0, IA64_PIB_XTP, 1, 6, 0), ==, 1);
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "pib-read", 0, IA64_PIB_XTP, 1, 0, 0), ==, 6);
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "pib-write", 0,
                        IA64_PIB_BASE + (UINT64_C(1) << 12), 8,
                        priority_vector, 0), ==, 1);
    g_assert_cmphex(sapic_cpu_state(qts, 1, priority_vector) &
                    SAPIC_STATE_IRR, ==, SAPIC_STATE_IRR);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept", 1, 0), ==,
                    priority_vector);
    sapic_cpu_operation(qts, "eoi", 1, 0);
    g_assert_cmpint(qtest_ia64_sapic(
                        qts, "pib-write", 0,
                        IA64_PIB_BASE + (UINT64_C(1) << 12) + 8, 8,
                        disabled_vector, 0), ==, 1);
    for (cpu = 0; cpu < 4; cpu++) {
        g_assert_cmphex(sapic_cpu_state(qts, cpu, disabled_vector) &
                        SAPIC_STATE_IRR, ==, 0);
    }

    pid_write(qts, pid_rte_low(1),
              priority_vector | PID_RTE_REDIRECTION_HINT);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 1);
    g_assert_cmphex(sapic_cpu_state(qts, 0, priority_vector) &
                    SAPIC_STATE_IRR, ==, 0);
    g_assert_cmphex(sapic_cpu_state(qts, 1, priority_vector) &
                    SAPIC_STATE_IRR, ==, 0);
    g_assert_cmphex(sapic_cpu_state(qts, 2, priority_vector) &
                    SAPIC_STATE_IRR, ==, SAPIC_STATE_IRR);
    g_assert_cmphex(sapic_cpu_state(qts, 3, priority_vector) &
                    SAPIC_STATE_IRR, ==, 0);
    cpu = 2;
    g_assert_cmpint(sapic_cpu_operation(qts, "accept", cpu, 0), ==,
                    priority_vector);
    sapic_cpu_operation(qts, "eoi", cpu, 0);
    sapic_cpu_operation(qts, "xtp", 2, 0x82);
    pid_write(qts, pid_rte_low(2),
              disabled_vector | PID_RTE_REDIRECTION_HINT);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 2);
    g_assert_cmphex(sapic_cpu_state(qts, 2, disabled_vector) &
                    SAPIC_STATE_IRR, ==, 0);
    g_assert_cmphex(sapic_cpu_state(qts, 3, disabled_vector) &
                    SAPIC_STATE_IRR, ==, SAPIC_STATE_IRR);

    sapic_cpu_operation(qts, "xtp", 0, 0x88);
    sapic_cpu_operation(qts, "xtp", 3, 0x82);
    pid_write(qts, pid_rte_low(3),
              no_redirect_vector | PID_RTE_REDIRECTION_HINT);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_IRQ, 3);
    g_assert_cmphex(sapic_cpu_state(qts, 0, no_redirect_vector) &
                    SAPIC_STATE_IRR, ==, SAPIC_STATE_IRR);
    g_assert_cmphex(sapic_cpu_state(qts, 3, no_redirect_vector) &
                    SAPIC_STATE_IRR, ==, 0);

    g_assert_cmpint(sapic_deliver(qts, 0, 1, 0, 2, false, 0), ==, 1);
    g_assert_cmphex(sapic_cpu_state(qts, 1, 0) & SAPIC_STATE_PMI, ==,
                    SAPIC_STATE_PMI);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-pmi", 1, 0), ==, 0);
    g_assert_cmphex(sapic_cpu_state(qts, 1, 0) & SAPIC_STATE_PMI, ==, 0);
    g_assert_cmpint(sapic_deliver(qts, 0, 1, 0, 2, false, 3), ==, 1);
    g_assert_cmpint(sapic_deliver(qts, 0, 1, 0, 2, false, 0), ==, 1);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-pmi", 1, 0), ==, 3);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-pmi", 1, 0), ==, 0);
    g_assert_cmpint(sapic_deliver(qts, 0, 1, 0, 2, false, 4), ==, 0);

    g_assert_cmpint(sapic_deliver(qts, 0, 3, 0, 5, false, 0xff), ==, 1);
    g_assert_cmphex(sapic_cpu_state(qts, 3, 0) & SAPIC_STATE_INIT, ==,
                    SAPIC_STATE_INIT);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-init", 3, 0), ==, 1);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-init", 3, 0), ==, 0);

    pid_write(qts, pid_rte_high(level_pin), UINT32_C(0x01000000));
    pid_write(qts, pid_rte_low(level_pin),
              level_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     level_pin, 1);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept", 1, 0), ==,
                    level_vector);
    g_assert_cmphex(sapic_cpu_state(qts, 1, level_vector) &
                    (SAPIC_STATE_IRR | SAPIC_STATE_ISR), ==,
                    SAPIC_STATE_ISR);
    sapic_cpu_operation(qts, "eoi", 1, 0);
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, level_vector);
    g_assert_cmphex(sapic_cpu_state(qts, 1, level_vector) &
                    (SAPIC_STATE_IRR | SAPIC_STATE_ISR), ==,
                    SAPIC_STATE_IRR);

    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     level_pin, 0);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept", 1, 0), ==,
                    level_vector);
    sapic_cpu_operation(qts, "eoi", 1, 0);
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, level_vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(level_pin)) &
                    PID_RTE_REMOTE_IRR, ==, 0);

    qtest_quit(qts);
}

static void test_named_legacy_input(void)
{
    const uint8_t vector = 0x57;
    QTestState *qts = pid_start(",legacy-pin=63", NULL);

    pid_write(qts, pid_rte_low(63), vector);
    pid_pulse(qts, INTEL_460GX_PID_GPIO_LEGACY, 0);
    g_assert_true(sapic_irr_wait_for_vector(qts, vector));

    qtest_quit(qts);
}

static void test_migration_state(void)
{
    const unsigned pin = 63;
    const unsigned pending_pin = 62;
    const uint8_t vector = 0x58;
    const uint8_t pending_vector = 0x5c;
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for migration-state snapshot");
        return;
    }

    tmpdir = g_dir_make_tmp("intel-460gx-pid-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);
    qts = pid_start(",initial-id=9", args);

    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x09008000);
    pid_write(qts, PID_REG_ID, 0x06000000);
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x06008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x06000000);

    pid_write(qts, pid_rte_high(pin), 0);
    pid_write(qts, pid_rte_low(pin), vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, pin, 1);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pin)) & PID_RTE_REMOTE_IRR,
                    !=, 0);

    pid_write(qts, pid_rte_high(pending_pin), 0xaa550000);
    pid_write(qts, pid_rte_low(pending_pin),
              pending_vector | PID_RTE_TRIGGER_LEVEL);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     pending_pin, 1);
    pid_write(qts, pid_rte_low(pending_pin),
              pending_vector | PID_RTE_TRIGGER_LEVEL | PID_RTE_MASKED);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ,
                     pending_pin, 0);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pending_pin)) &
                    PID_RTE_DELIVERY_STATUS, !=, 0);
    g_assert_cmphex(sapic_cpu_operation(qts, "xtp", 0, 0x86), ==, 0x86);
    g_assert_cmpint(sapic_deliver(qts, 0, 0, 0, 2, false, 0), ==, 1);
    g_assert_cmpint(sapic_deliver(qts, 0, 0, 0, 2, false, 3), ==, 1);
    g_assert_cmpint(sapic_deliver(qts, 0, 0, 0, 5, false, 0xff), ==, 1);
    g_assert_cmpint(sapic_cpu_operation(qts, "ras-arm", 0, 0), ==, 1);
    g_assert_cmphex(sapic_cpu_operation(qts, "ras-state", 0, 0), ==,
                    BIT(1) | BIT(3));
    pid_select(qts, pid_rte_high(pin));

    response = qtest_hmp(qts, "savevm pid-state");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qtest_system_reset(qts);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pin)), ==, PID_RTE_MASKED);
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x09008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x09000000);
    g_assert_cmphex(sapic_cpu_state(qts, 0, 0) &
                    (UINT64_C(0xff) | SAPIC_STATE_PMI | SAPIC_STATE_INIT),
                    ==, 0x80);
    g_assert_cmphex(sapic_cpu_operation(qts, "ras-state", 0, 0), ==, 0);

    response = qtest_hmp(qts, "loadvm pid-state");
    g_assert_cmpstr(response, ==, "");
    g_assert_cmphex(qtest_readl(qts, PID_TEST_BASE + PID_IOREGSEL), ==,
                    pid_rte_high(pin));
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x06008000);
    g_assert_cmphex(pid_read(qts, PID_REG_ARB_ID), ==, 0x06000000);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pin)) &
                    (PID_RTE_VECTOR | PID_RTE_TRIGGER_LEVEL |
                     PID_RTE_REMOTE_IRR), ==,
                    vector | PID_RTE_TRIGGER_LEVEL | PID_RTE_REMOTE_IRR);
    g_assert_cmphex(sapic_cpu_state(qts, 0, 0) &
                    (UINT64_C(0xff) | SAPIC_STATE_PMI | SAPIC_STATE_INIT),
                    ==, 0x86 | SAPIC_STATE_PMI | SAPIC_STATE_INIT);
    g_assert_cmphex(sapic_cpu_operation(qts, "ras-state", 0, 0), ==,
                    BIT(1) | BIT(3));
    g_assert_cmpint(sapic_cpu_operation(qts, "ras-resume", 0, 0), ==, 1);
    g_assert_cmphex(sapic_cpu_operation(qts, "ras-state", 0, 0), ==,
                    BIT(3));
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-pmi", 0, 0), ==, 3);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-pmi", 0, 0), ==, 0);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-init", 0, 0), ==, 1);
    g_assert_cmpint(sapic_cpu_operation(qts, "accept-init", 0, 0), ==, 0);

    pid_write(qts, pid_rte_high(pending_pin), 0);
    pid_write(qts, pid_rte_low(pending_pin),
              pending_vector | PID_RTE_TRIGGER_LEVEL);
    g_assert_true(sapic_irr_wait_for_vector(qts, pending_vector));
    g_assert_cmphex(pid_read(qts, pid_rte_low(pending_pin)) &
                    (PID_RTE_DELIVERY_STATUS | PID_RTE_REMOTE_IRR), ==,
                    PID_RTE_REMOTE_IRR);

    /* irq_level[] is migrated: EOI while asserted must redeliver. */
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pin)) & PID_RTE_REMOTE_IRR,
                    !=, 0);
    qtest_set_irq_in(qts, PID_QOM_PATH, INTEL_460GX_PID_GPIO_IRQ, pin, 0);
    qtest_writel(qts, PID_TEST_BASE + PID_EOI, vector);
    g_assert_cmphex(pid_read(qts, pid_rte_low(pin)) & PID_RTE_REMOTE_IRR,
                    ==, 0);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_migration_wiring_baseline(void)
{
    const uint8_t initial_id = 9;
    const uint32_t legacy_pin = 63;
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for migration-state snapshot");
        return;
    }

    tmpdir = g_dir_make_tmp("intel-460gx-pid-wiring-savevm-XXXXXX",
                            &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);

    qts = pid_start_with_wiring(args, initial_id, legacy_pin,
                                PID_TEST_BASE);
    response = qtest_hmp(qts, "savevm pid-wiring-baseline");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    qts = pid_start_with_wiring(args, initial_id - 1U, legacy_pin,
                                PID_TEST_BASE);
    response = qtest_hmp(qts, "loadvm pid-wiring-baseline");
    g_assert_nonnull(strstr(response, "error while loading state"));
    g_assert_nonnull(strstr(response, TYPE_INTEL_460GX_PID));
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    qts = pid_start_with_wiring(args, initial_id, legacy_pin - 1U,
                                PID_TEST_BASE);
    response = qtest_hmp(qts, "loadvm pid-wiring-baseline");
    g_assert_nonnull(strstr(response, "error while loading state"));
    g_assert_nonnull(strstr(response, TYPE_INTEL_460GX_PID));
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    qts = pid_start_with_wiring(args, initial_id, legacy_pin,
                                PID_TEST_BASE + 0x1000);
    response = qtest_hmp(qts, "loadvm pid-wiring-baseline");
    g_assert_nonnull(strstr(response, "error while loading state"));
    g_assert_nonnull(strstr(response, TYPE_INTEL_460GX_PID));
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    qts = pid_start_with_wiring(args, initial_id, legacy_pin,
                                PID_TEST_BASE);
    response = qtest_hmp(qts, "loadvm pid-wiring-baseline");
    g_assert_cmpstr(response, ==, "");
    qtest_system_reset(qts);
    g_assert_cmphex(pid_read(qts, PID_REG_ID), ==, 0x09008000);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/intel-460gx-pid/registers-and-negative",
                   test_registers_and_negative);
    qtest_add_func("/intel-460gx-pid/all-64-redirection-entries",
                   test_all_redirection_entries);
    qtest_add_func("/intel-460gx-pid/reset", test_reset);
    qtest_add_func("/intel-460gx-pid/level-polarity-and-eoi",
                   test_level_polarity_and_eoi);
    qtest_add_func("/intel-460gx-pid/sapic-delivery-and-hint",
                   test_sapic_delivery_and_hint);
    qtest_add_func("/intel-460gx-pid/mask-destination-and-route",
                   test_mask_destination_and_route);
    qtest_add_func("/intel-460gx-pid/smp-delivery-accept-and-eoi",
                   test_smp_delivery_accept_and_eoi);
    qtest_add_func("/intel-460gx-pid/named-legacy-input",
                   test_named_legacy_input);
    qtest_add_func("/intel-460gx-pid/migration-state",
                   test_migration_state);
    qtest_add_func("/intel-460gx-pid/migration-wiring-baseline",
                   test_migration_wiring_baseline);

    return g_test_run();
}
