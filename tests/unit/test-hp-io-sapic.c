/*
 * HP I/O SAPIC helper tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-io-sapic.h"

typedef struct DeliveryLog {
    HPIOSAPICMessage messages[16];
    unsigned int count;
} DeliveryLog;

static unsigned int rte_low(unsigned int entry)
{
    return HP_IO_SAPIC_RTE_BASE + 2 * entry;
}

static void record_delivery(void *opaque, const HPIOSAPICMessage *message)
{
    DeliveryLog *log = opaque;

    g_assert_cmpuint(log->count, <, G_N_ELEMENTS(log->messages));
    log->messages[log->count++] = *message;
}

static void test_astro_registers(void)
{
    uint64_t regs[HP_IO_SAPIC_ASTRO_REG_COUNT] = { 0 };
    uint32_t selector = 0;
    uint64_t value = 0;

    g_assert_cmpuint(hp_io_sapic_astro_policy.variant, ==,
                     HP_IO_SAPIC_VARIANT_ASTRO);
    g_assert_cmpuint(hp_io_sapic_astro_policy.input_count, ==, 8);
    g_assert_cmpuint(hp_io_sapic_astro_policy.entry_count, ==, 8);
    g_assert_cmpuint(hp_io_sapic_astro_policy.register_count, ==,
                     HP_IO_SAPIC_ASTRO_REG_COUNT);
    g_assert_cmphex(hp_io_sapic_astro_policy.version, ==, UINT32_C(0x00200001));

    hp_io_sapic_select_write(&hp_io_sapic_astro_policy, &selector,
                             UINT64_C(0x100000010));
    g_assert_cmphex(selector, ==, 0x10);
    g_assert_cmphex(
        hp_io_sapic_select_read(&hp_io_sapic_astro_policy, selector), ==, 0x10);

    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_astro_policy, selector,
                                           regs, G_N_ELEMENTS(regs),
                                           UINT64_C(0x1122334455667788)));
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_astro_policy, selector,
                                          regs, G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0x1122334455667788));

    regs[1] = UINT64_C(0xaabbccddeeff0011);
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_astro_policy, 1, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0x00200001));
    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_astro_policy, 1, regs,
                                           G_N_ELEMENTS(regs),
                                           UINT64_C(0xdeadbeefcafef00d)));
    g_assert_cmphex(regs[1], ==, UINT64_C(0xdeadbeefcafef00d));
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_astro_policy, 1, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0x00200001));

    value = UINT64_C(0xfedcba9876543210);
    g_assert_false(hp_io_sapic_window_read(&hp_io_sapic_astro_policy,
                                           HP_IO_SAPIC_ASTRO_REG_COUNT, regs,
                                           G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0xfedcba9876543210));
    g_assert_false(hp_io_sapic_window_write(&hp_io_sapic_astro_policy,
                                            HP_IO_SAPIC_ASTRO_REG_COUNT, regs,
                                            G_N_ELEMENTS(regs), UINT64_MAX));
}

static void test_astro_reset(void)
{
    uint64_t regs[HP_IO_SAPIC_ASTRO_REG_COUNT];
    const uint64_t destination = UINT64_C(0xa0ff0000);
    uint32_t selector = UINT32_C(0xdeadbeef);
    uint32_t ilr = UINT32_MAX;
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(regs); i++) {
        regs[i] = UINT64_C(0x5aa5000000000000) + i;
    }

    g_assert_true(hp_io_sapic_reset(&hp_io_sapic_astro_policy, &selector, regs,
                                    G_N_ELEMENTS(regs), &ilr, destination));
    g_assert_cmphex(selector, ==, UINT32_C(0xdeadbeef));
    g_assert_cmphex(ilr, ==, 0);
    g_assert_cmphex(regs[0], ==, UINT64_C(0x5aa5000000000000));
    g_assert_cmphex(regs[1], ==, UINT64_C(0x5aa5000000000001));

    for (i = 0; i < hp_io_sapic_astro_policy.entry_count; i++) {
        g_assert_cmphex(regs[rte_low(i)], ==,
                        HP_IO_SAPIC_RTE_POLARITY | HP_IO_SAPIC_RTE_TRIGGER |
                            HP_IO_SAPIC_RTE_MASK | (i + 2));
        g_assert_cmphex(regs[rte_low(i) + 1], ==, destination);
    }
}

static void test_astro_pending_and_delivery(void)
{
    uint64_t regs[HP_IO_SAPIC_ASTRO_REG_COUNT] = { 0 };
    uint64_t zx1_regs[HP_IO_SAPIC_ZX1_REG_COUNT] = { 0 };
    DeliveryLog log = { 0 };
    uint32_t selector = 0;
    uint32_t ilr = 0;

    g_assert_true(hp_io_sapic_reset(&hp_io_sapic_astro_policy, &selector, regs,
                                    G_N_ELEMENTS(regs), &ilr,
                                    UINT64_C(0xa0ff0000)));
    regs[rte_low(0)] = 0x2b;

    g_assert_true(hp_io_sapic_set_input(&hp_io_sapic_astro_policy, regs,
                                        G_N_ELEMENTS(regs), &ilr, 0, true,
                                        record_delivery, &log));
    g_assert_cmphex(ilr, ==, 1U << (0x2b & 7));
    g_assert_cmpuint(log.count, ==, 1);
    g_assert_cmphex(log.messages[0].address, ==, UINT64_C(0xfffa0000));
    g_assert_cmphex(log.messages[0].data, ==, 0x2b);

    g_assert_false(hp_io_sapic_set_input(&hp_io_sapic_astro_policy, regs,
                                         G_N_ELEMENTS(regs), &ilr, 0, true,
                                         record_delivery, &log));
    g_assert_cmpuint(log.count, ==, 1);

    g_assert_false(hp_io_sapic_set_input(&hp_io_sapic_astro_policy, regs,
                                         G_N_ELEMENTS(regs), &ilr, 0, false,
                                         record_delivery, &log));
    g_assert_cmphex(ilr, ==, 0);

    regs[rte_low(0)] |= HP_IO_SAPIC_RTE_MASK;
    ilr = 1U << (0x2b & 7);
    g_assert_false(hp_io_sapic_set_input(&hp_io_sapic_astro_policy, regs,
                                         G_N_ELEMENTS(regs), &ilr, 0, true,
                                         record_delivery, &log));
    g_assert_cmphex(ilr, ==, 0);

    g_assert_false(hp_io_sapic_set_input(&hp_io_sapic_zx1_policy, zx1_regs,
                                         G_N_ELEMENTS(zx1_regs), &ilr, 0, true,
                                         record_delivery, &log));
}

static void test_astro_eoi(void)
{
    uint64_t regs[HP_IO_SAPIC_ASTRO_REG_COUNT] = { 0 };
    uint32_t ilr = UINT32_C(0xff);

    regs[rte_low(0)] = 7;
    regs[rte_low(1)] = 0x22;
    regs[rte_low(2)] = 0x22;
    hp_io_sapic_eoi(&hp_io_sapic_astro_policy, regs, G_N_ELEMENTS(regs), &ilr,
                    0x62, 0, NULL, NULL);
    g_assert_cmphex(ilr, ==, UINT32_C(0xf9));

    /* Astro pending uses vector low bits, while its EOI clears input bits. */
    ilr = 1U << 7;
    hp_io_sapic_eoi(&hp_io_sapic_astro_policy, regs, G_N_ELEMENTS(regs), &ilr,
                    7, 0, NULL, NULL);
    g_assert_cmphex(ilr, ==, 1U << 7);
}

static void test_zx1_registers_and_reset(void)
{
    uint64_t regs[HP_IO_SAPIC_ZX1_REG_COUNT];
    uint32_t selector = UINT32_MAX;
    uint32_t ilr = UINT32_MAX;
    uint64_t value;
    unsigned int i;

    memset(regs, 0xa5, sizeof(regs));
    g_assert_cmpuint(hp_io_sapic_zx1_policy.variant, ==,
                     HP_IO_SAPIC_VARIANT_ZX1);
    g_assert_cmpuint(hp_io_sapic_zx1_policy.input_count, ==, 10);
    g_assert_cmpuint(hp_io_sapic_zx1_policy.entry_count, ==, 11);
    g_assert_cmpuint(hp_io_sapic_zx1_policy.register_count, ==,
                     HP_IO_SAPIC_ZX1_REG_COUNT);
    g_assert_cmphex(hp_io_sapic_zx1_policy.version, ==, UINT32_C(0x000a0020));

    g_assert_true(hp_io_sapic_reset(&hp_io_sapic_zx1_policy, &selector, regs,
                                    G_N_ELEMENTS(regs), &ilr, UINT64_MAX));
    g_assert_cmphex(selector, ==, 0);
    g_assert_cmphex(ilr, ==, 0);
    for (i = 0; i < hp_io_sapic_zx1_policy.entry_count; i++) {
        g_assert_cmphex(regs[rte_low(i)], ==, HP_IO_SAPIC_RTE_MASK);
        g_assert_cmphex(regs[rte_low(i) + 1], ==, 0);
    }

    hp_io_sapic_select_write(&hp_io_sapic_zx1_policy, &selector,
                             UINT64_C(0x123456789abcdef0));
    g_assert_cmphex(selector, ==, 0xf0);
    g_assert_cmphex(
        hp_io_sapic_select_read(&hp_io_sapic_zx1_policy, UINT32_C(0xabcdef01)),
        ==, 1);

    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy, 1, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0x000a0020));
    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_zx1_policy, 1, regs,
                                           G_N_ELEMENTS(regs), UINT64_MAX));
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy, 1, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0x000a0020));

    regs[2] = UINT64_MAX;
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy, 2, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, 0);
    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_zx1_policy, 2, regs,
                                           G_N_ELEMENTS(regs), UINT64_MAX));
    g_assert_cmphex(regs[2], ==, UINT64_MAX);

    regs[rte_low(0)] = HP_IO_SAPIC_RTE_STATUS;
    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_zx1_policy, rte_low(0),
                                           regs, G_N_ELEMENTS(regs),
                                           UINT64_MAX));
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy, rte_low(0),
                                          regs, G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==,
                    HP_IO_SAPIC_RTE_STATUS | HP_IO_SAPIC_RTE_VECTOR |
                        HP_IO_SAPIC_RTE_DELIVERY | HP_IO_SAPIC_RTE_POLARITY |
                        HP_IO_SAPIC_RTE_TRIGGER | HP_IO_SAPIC_RTE_MASK);

    g_assert_true(hp_io_sapic_window_write(&hp_io_sapic_zx1_policy,
                                           rte_low(0) + 1, regs,
                                           G_N_ELEMENTS(regs), UINT64_MAX));
    g_assert_true(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy,
                                          rte_low(0) + 1, regs,
                                          G_N_ELEMENTS(regs), &value));
    g_assert_cmphex(value, ==, UINT64_C(0xffff0000));

    g_assert_false(hp_io_sapic_window_read(&hp_io_sapic_zx1_policy,
                                           HP_IO_SAPIC_ZX1_REG_COUNT, regs,
                                           G_N_ELEMENTS(regs), &value));
}

static void test_zx1_message_conversion(void)
{
    uint64_t regs[HP_IO_SAPIC_ZX1_REG_COUNT] = { 0 };
    HPIOSAPICMessage message;

    regs[rte_low(3)] = HP_IO_SAPIC_RTE_TRIGGER | 0x100 | 0xab;
    regs[rte_low(3) + 1] = UINT32_C(0x12340000);
    g_assert_true(hp_io_sapic_make_message(&hp_io_sapic_zx1_policy, regs,
                                           G_N_ELEMENTS(regs), 3, &message));
    g_assert_cmphex(message.address, ==, UINT64_C(0xfee12348));
    g_assert_cmphex(message.data, ==, UINT32_C(0x0000c1ab));

    regs[rte_low(3)] = 0x5a;
    g_assert_true(hp_io_sapic_make_message(&hp_io_sapic_zx1_policy, regs,
                                           G_N_ELEMENTS(regs), 3, &message));
    g_assert_cmphex(message.address, ==, UINT64_C(0xfee12340));
    g_assert_cmphex(message.data, ==, UINT32_C(0x0000405a));
}

static void test_zx1_software_interrupt(void)
{
    uint64_t regs[HP_IO_SAPIC_ZX1_REG_COUNT] = { 0 };
    DeliveryLog log = { 0 };
    const unsigned int sw_low = rte_low(10);

    regs[sw_low] = 0x100 | 0x6a;
    regs[sw_low + 1] = UINT32_C(0x23450000);
    g_assert_true(hp_io_sapic_software_interrupt(&hp_io_sapic_zx1_policy, regs,
                                                 G_N_ELEMENTS(regs),
                                                 record_delivery, &log));
    g_assert_cmpuint(log.count, ==, 1);
    g_assert_cmphex(log.messages[0].address, ==, UINT64_C(0xfee23458));
    g_assert_cmphex(log.messages[0].data, ==, UINT32_C(0x0000416a));

    regs[sw_low] |= HP_IO_SAPIC_RTE_MASK;
    g_assert_false(hp_io_sapic_software_interrupt(&hp_io_sapic_zx1_policy, regs,
                                                  G_N_ELEMENTS(regs),
                                                  record_delivery, &log));
    regs[sw_low] = HP_IO_SAPIC_RTE_POLARITY | 0x6a;
    g_assert_false(hp_io_sapic_software_interrupt(&hp_io_sapic_zx1_policy, regs,
                                                  G_N_ELEMENTS(regs),
                                                  record_delivery, &log));
    regs[sw_low] = HP_IO_SAPIC_RTE_TRIGGER | 0x6a;
    g_assert_false(hp_io_sapic_software_interrupt(&hp_io_sapic_zx1_policy, regs,
                                                  G_N_ELEMENTS(regs),
                                                  record_delivery, &log));
    g_assert_cmpuint(log.count, ==, 1);

    g_assert_false(hp_io_sapic_software_interrupt(
        &hp_io_sapic_astro_policy, regs, HP_IO_SAPIC_ASTRO_REG_COUNT,
        record_delivery, &log));
}

static void test_zx1_level_eoi_redelivery(void)
{
    uint64_t regs[HP_IO_SAPIC_ZX1_REG_COUNT] = { 0 };
    DeliveryLog log = { 0 };
    uint32_t ilr = 1U << 2;
    unsigned int deliveries;

    regs[rte_low(2)] = HP_IO_SAPIC_RTE_TRIGGER | 0x55;
    regs[rte_low(2) + 1] = UINT32_C(0x34560000);
    deliveries =
        hp_io_sapic_eoi(&hp_io_sapic_zx1_policy, regs, G_N_ELEMENTS(regs), &ilr,
                        0x55, 1U << 2, record_delivery, &log);
    g_assert_cmpuint(deliveries, ==, 1);
    g_assert_cmphex(ilr, ==, 1U << 2);
    g_assert_cmpuint(log.count, ==, 1);
    g_assert_cmphex(log.messages[0].address, ==, UINT64_C(0xfee34560));
    g_assert_cmphex(log.messages[0].data, ==, UINT32_C(0x0000c055));

    deliveries =
        hp_io_sapic_eoi(&hp_io_sapic_zx1_policy, regs, G_N_ELEMENTS(regs), &ilr,
                        0x55, 0, record_delivery, &log);
    g_assert_cmpuint(deliveries, ==, 0);
    g_assert_cmphex(ilr, ==, 0);

    regs[rte_low(2)] |= HP_IO_SAPIC_RTE_MASK;
    ilr = 1U << 2;
    deliveries =
        hp_io_sapic_eoi(&hp_io_sapic_zx1_policy, regs, G_N_ELEMENTS(regs), &ilr,
                        0x55, 1U << 2, record_delivery, &log);
    g_assert_cmpuint(deliveries, ==, 0);
    g_assert_cmphex(ilr, ==, 0);

    regs[rte_low(2)] = 0x55;
    ilr = 1U << 2;
    deliveries =
        hp_io_sapic_eoi(&hp_io_sapic_zx1_policy, regs, G_N_ELEMENTS(regs), &ilr,
                        0x55, 1U << 2, record_delivery, &log);
    g_assert_cmpuint(deliveries, ==, 0);
    g_assert_cmphex(ilr, ==, 0);
    g_assert_cmpuint(log.count, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-io-sapic/astro/registers", test_astro_registers);
    g_test_add_func("/hp-io-sapic/astro/reset", test_astro_reset);
    g_test_add_func("/hp-io-sapic/astro/pending-and-delivery",
                    test_astro_pending_and_delivery);
    g_test_add_func("/hp-io-sapic/astro/eoi", test_astro_eoi);
    g_test_add_func("/hp-io-sapic/zx1/registers-and-reset",
                    test_zx1_registers_and_reset);
    g_test_add_func("/hp-io-sapic/zx1/message-conversion",
                    test_zx1_message_conversion);
    g_test_add_func("/hp-io-sapic/zx1/software-interrupt",
                    test_zx1_software_interrupt);
    g_test_add_func("/hp-io-sapic/zx1/level-eoi-redelivery",
                    test_zx1_level_eoi_redelivery);
    return g_test_run();
}
