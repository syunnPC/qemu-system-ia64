/*
 * HP LBA PCI configuration-selector helper tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-lba-config.h"

static void test_selector_access_valid(void)
{
    static const struct {
        uint64_t offset;
        unsigned int size;
    } invalid[] = {
        { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 5 }, { 0, 16 }, { 1, 4 },
        { 2, 4 }, { 3, 4 }, { 5, 4 }, { 8, 4 }, { 4, 8 },  { 8, 8 },
    };
    unsigned int i;

    g_assert_true(hp_lba_config_selector_access_valid(0, 4));
    g_assert_true(hp_lba_config_selector_access_valid(4, 4));
    g_assert_true(hp_lba_config_selector_access_valid(0, 8));

    for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_assert_false(hp_lba_config_selector_access_valid(invalid[i].offset,
                                                           invalid[i].size));
    }
}

static void test_selector_read_lanes(void)
{
    const uint64_t selector = UINT64_C(0x1122334455667788);
    uint64_t value;

    g_assert_true(hp_lba_config_selector_read(selector, 0, 4, &value));
    g_assert_cmphex(value, ==, UINT64_C(0x55667788));

    value = 0;
    g_assert_true(hp_lba_config_selector_read(selector, 4, 4, &value));
    g_assert_cmphex(value, ==, UINT64_C(0x11223344));
}

static void test_selector_write_lanes(void)
{
    uint64_t selector = UINT64_C(0x1122334455667788);
    uint32_t effective = UINT32_MAX;

    g_assert_true(hp_lba_config_selector_write(&selector, &effective, 4, 4,
                                               UINT64_C(0xdeadbeefaabbccdd)));
    g_assert_cmphex(selector, ==, UINT64_C(0xaabbccdd55667788));
    g_assert_cmphex(effective, ==, UINT32_C(0x55667788));

    g_assert_true(hp_lba_config_selector_write(&selector, &effective, 0, 4,
                                               UINT64_C(0xdeadbeefeeff0011)));
    g_assert_cmphex(selector, ==, UINT64_C(0xaabbccddeeff0011));
    g_assert_cmphex(effective, ==, UINT32_C(0xeeff0011));
}

static void test_selector_eight_byte(void)
{
    uint64_t selector = UINT64_C(0x1122334455667788);
    uint32_t effective = UINT32_MAX;
    uint64_t value = 0;

    g_assert_true(hp_lba_config_selector_read(selector, 0, 8, &value));
    g_assert_cmphex(value, ==, selector);

    g_assert_true(hp_lba_config_selector_write(&selector, &effective, 0, 8,
                                               UINT64_C(0x5aa55aa500000800)));
    g_assert_cmphex(selector, ==, UINT64_C(0x5aa55aa500000800));
    g_assert_cmphex(effective, ==, UINT32_C(0x800));
}

static void test_selector_decomposed_eight_byte(void)
{
    uint64_t selector = UINT64_MAX;
    uint32_t effective = UINT32_MAX;

    g_assert_true(hp_lba_config_selector_write(&selector, &effective, 0, 4,
                                               UINT32_C(0x00000800)));
    g_assert_true(hp_lba_config_selector_write(&selector, &effective, 4, 4,
                                               UINT32_C(0x5aa55aa5)));
    g_assert_cmphex(selector, ==, UINT64_C(0x5aa55aa500000800));
    g_assert_cmphex(effective, ==, UINT32_C(0x800));
}

static void test_selector_sync_reset_post_load(void)
{
    uint64_t selector = UINT64_C(0x5aa55aa500000800);
    uint32_t effective = UINT32_MAX;

    hp_lba_config_selector_sync(selector, &effective);
    g_assert_cmphex(effective, ==, UINT32_C(0x800));

    selector = 0;
    hp_lba_config_selector_sync(selector, &effective);
    g_assert_cmphex(effective, ==, 0);

    selector = UINT64_C(0x5aa55aa500000800);
    hp_lba_config_selector_sync(selector, &effective);
    g_assert_cmphex(effective, ==, UINT32_C(0x800));
}

static void test_data_lanes(void)
{
    static const uint32_t expected[] = {
        0x800, 0x801, 0x802, 0x803, 0x800, 0x801, 0x802, 0x803,
    };
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(expected); i++) {
        g_assert_cmphex(hp_lba_config_data_address(0x800, i), ==, expected[i]);
    }
    g_assert_cmphex(hp_lba_config_data_address(0x803, 0), ==, 0x803);
    g_assert_cmphex(hp_lba_config_data_address(0x803, 7), ==, 0x803);
}

static void test_selector_invalid_unchanged(void)
{
    uint64_t selector = UINT64_C(0x1122334455667788);
    uint32_t effective = UINT32_C(0xaabbccdd);
    uint64_t value = UINT64_C(0xdeadbeefcafef00d);

    g_assert_false(hp_lba_config_selector_read(selector, 4, 8, &value));
    g_assert_cmphex(value, ==, UINT64_C(0xdeadbeefcafef00d));

    g_assert_false(
        hp_lba_config_selector_write(&selector, &effective, 1, 4, UINT64_MAX));
    g_assert_cmphex(selector, ==, UINT64_C(0x1122334455667788));
    g_assert_cmphex(effective, ==, UINT32_C(0xaabbccdd));

    g_assert_false(hp_lba_config_selector_read(selector, 0, 4, NULL));
    g_assert_false(hp_lba_config_selector_write(NULL, &effective, 0, 4, 0));
    g_assert_false(hp_lba_config_selector_write(&selector, NULL, 0, 4, 0));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-lba-config/selector/access-valid",
                    test_selector_access_valid);
    g_test_add_func("/hp-lba-config/selector/read-lanes",
                    test_selector_read_lanes);
    g_test_add_func("/hp-lba-config/selector/write-lanes",
                    test_selector_write_lanes);
    g_test_add_func("/hp-lba-config/selector/eight-byte",
                    test_selector_eight_byte);
    g_test_add_func("/hp-lba-config/selector/decomposed-eight-byte",
                    test_selector_decomposed_eight_byte);
    g_test_add_func("/hp-lba-config/selector/sync-reset-post-load",
                    test_selector_sync_reset_post_load);
    g_test_add_func("/hp-lba-config/data-lanes", test_data_lanes);
    g_test_add_func("/hp-lba-config/selector/invalid-unchanged",
                    test_selector_invalid_unchanged);
    return g_test_run();
}
