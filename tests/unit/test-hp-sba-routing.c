/*
 * HP SBA/LBA routing variant helper tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-sba-routing.h"

static const HPSBARootSpec test_roots[] = {
    { .hpa_offset = 0x30000, .bus_num = 0, .user_attachable = true },
    { .hpa_offset = 0x32000, .bus_num = 1 },
    { .hpa_offset = 0x38000, .bus_num = 2 },
    { .hpa_offset = 0x3c000, .bus_num = 3 },
};

static const HPSBARopeMap test_rope_map[] = {
    { .rope = 0, .root = 0 },
    { .rope = 1, .root = 1 },
    { .rope = 4, .root = 2 },
    { .rope = 6, .root = 3 },
};

static bool test_direct_route(const HPSBARoutingVariant *variant,
                              uint64_t route, unsigned int *root_index)
{
    (void)variant;
    *root_index = (uint32_t)route & 3;
    return true;
}

static bool test_invalid_direct_route(const HPSBARoutingVariant *variant,
                                      uint64_t route, unsigned int *root_index)
{
    (void)route;
    *root_index = variant->root_count;
    return true;
}

static uint64_t test_extend_address(void *opaque, uint64_t address)
{
    uint64_t high = *(uint64_t *)opaque;

    return high | address;
}

typedef struct TestInterrupt {
    uint64_t address;
    uint32_t data;
    unsigned int count;
} TestInterrupt;

static void test_deliver_interrupt(void *opaque, uint64_t address,
                                   uint32_t data)
{
    TestInterrupt *interrupt = opaque;

    interrupt->address = address;
    interrupt->data = data;
    interrupt->count++;
}

static const HPSBARoutingVariant test_variant = {
    .roots = test_roots,
    .root_count = G_N_ELEMENTS(test_roots),
    .rope_map = test_rope_map,
    .rope_map_count = G_N_ELEMENTS(test_rope_map),
    .physical_rope_count = 8,
    .direct_range_count = 4,
    .sba_reg_endianness = DEVICE_LITTLE_ENDIAN,
    .lba_reg_endianness = DEVICE_LITTLE_ENDIAN,
    .config_endianness = DEVICE_LITTLE_ENDIAN,
    .direct_route_lookup = test_direct_route,
    .extend_address = test_extend_address,
    .deliver_interrupt = test_deliver_interrupt,
};

static void test_variant_valid(void)
{
    g_assert_true(hp_sba_routing_variant_valid(&test_variant));
}

static void test_variant_rejects_bad_topology(void)
{
    HPSBARoutingVariant variant = test_variant;
    HPSBARopeMap rope_map[G_N_ELEMENTS(test_rope_map)];
    HPSBARootSpec roots[G_N_ELEMENTS(test_roots)];

    variant.root_count = 0;
    g_assert_false(hp_sba_routing_variant_valid(&variant));

    variant = test_variant;
    variant.sba_reg_endianness = 0;
    g_assert_false(hp_sba_routing_variant_valid(&variant));

    memcpy(rope_map, test_rope_map, sizeof(rope_map));
    variant = test_variant;
    variant.rope_map = rope_map;
    rope_map[3].rope = rope_map[0].rope;
    g_assert_false(hp_sba_routing_variant_valid(&variant));

    memcpy(rope_map, test_rope_map, sizeof(rope_map));
    rope_map[3].root = G_N_ELEMENTS(test_roots);
    g_assert_false(hp_sba_routing_variant_valid(&variant));

    variant = test_variant;
    memcpy(roots, test_roots, sizeof(roots));
    variant.roots = roots;
    roots[3].hpa_offset = roots[0].hpa_offset;
    g_assert_false(hp_sba_routing_variant_valid(&variant));

    memcpy(roots, test_roots, sizeof(roots));
    roots[3].bus_num = roots[0].bus_num;
    g_assert_false(hp_sba_routing_variant_valid(&variant));
}

static void test_variant_allows_bundled_root(void)
{
    static const HPSBARopeMap bundled[] = {
        { .rope = 0, .root = 0 }, { .rope = 1, .root = 1 },
        { .rope = 4, .root = 2 }, { .rope = 5, .root = 2 },
        { .rope = 6, .root = 3 },
    };
    HPSBARoutingVariant variant = test_variant;
    unsigned int rope = UINT_MAX;

    variant.rope_map = bundled;
    variant.rope_map_count = G_N_ELEMENTS(bundled);
    g_assert_true(hp_sba_routing_variant_valid(&variant));
    g_assert_true(hp_sba_routing_rope_for_root(&variant, 2, 0, &rope));
    g_assert_cmpuint(rope, ==, 4);
    g_assert_true(hp_sba_routing_rope_for_root(&variant, 2, 1, &rope));
    g_assert_cmpuint(rope, ==, 5);
}

static void test_rope_lookup(void)
{
    unsigned int value = UINT_MAX;

    g_assert_true(hp_sba_routing_root_for_rope(&test_variant, 4, &value));
    g_assert_cmpuint(value, ==, 2);

    g_assert_true(hp_sba_routing_rope_for_root(&test_variant, 3, 0, &value));
    g_assert_cmpuint(value, ==, 6);

    value = 0xa5;
    g_assert_false(hp_sba_routing_root_for_rope(&test_variant, 2, &value));
    g_assert_cmpuint(value, ==, 0xa5);
    g_assert_false(hp_sba_routing_rope_for_root(&test_variant, 2, 1, &value));
    g_assert_cmpuint(value, ==, 0xa5);
}

static void test_direct_route_lookup(void)
{
    HPSBARoutingVariant variant = test_variant;
    unsigned int root = UINT_MAX;

    g_assert_true(hp_sba_routing_direct_root(&variant, 0, &root));
    g_assert_cmpuint(root, ==, 0);
    g_assert_true(hp_sba_routing_direct_root(&variant, 5, &root));
    g_assert_cmpuint(root, ==, 1);
    g_assert_true(hp_sba_routing_direct_root(
        &variant, UINT64_C(0x1234567800000007), &root));
    g_assert_cmpuint(root, ==, 3);

    variant.direct_route_lookup = test_invalid_direct_route;
    root = 0xa5;
    g_assert_false(hp_sba_routing_direct_root(&variant, 0, &root));
    g_assert_cmpuint(root, ==, 0xa5);
}

static void test_callbacks(void)
{
    TestInterrupt interrupt = { 0 };
    uint64_t high = UINT64_C(0xffffffff00000000);
    uint64_t extended;

    extended = hp_sba_routing_extend_address(&test_variant, &high,
                                             UINT64_C(0x12345678));
    g_assert_cmphex(extended, ==, UINT64_C(0xffffffff12345678));

    hp_sba_routing_deliver_interrupt(&test_variant, &interrupt,
                                     UINT64_C(0xfee01000), 0x1234);
    g_assert_cmpuint(interrupt.count, ==, 1);
    g_assert_cmphex(interrupt.address, ==, UINT64_C(0xfee01000));
    g_assert_cmphex(interrupt.data, ==, 0x1234);
}

static void test_reg64_little_endian(void)
{
    uint64_t reg = UINT64_C(0x1122334455667788);
    uint64_t value = 0;

    g_assert_true(hp_sba_reg64_access_covers_low32(DEVICE_LITTLE_ENDIAN, 0, 4));
    g_assert_false(
        hp_sba_reg64_access_covers_low32(DEVICE_LITTLE_ENDIAN, 4, 4));
    g_assert_true(hp_sba_reg64_access_covers_low32(DEVICE_LITTLE_ENDIAN, 0, 8));
    g_assert_true(hp_sba_reg64_read(DEVICE_LITTLE_ENDIAN, reg, 0, 4, &value));
    g_assert_cmphex(value, ==, UINT32_C(0x55667788));
    g_assert_true(hp_sba_reg64_read(DEVICE_LITTLE_ENDIAN, reg, 4, 4, &value));
    g_assert_cmphex(value, ==, UINT32_C(0x11223344));
    g_assert_true(hp_sba_reg64_write(DEVICE_LITTLE_ENDIAN, &reg, 4, 4,
                                     UINT32_C(0xaabbccdd)));
    g_assert_cmphex(reg, ==, UINT64_C(0xaabbccdd55667788));
}

static void test_reg64_big_endian(void)
{
    uint64_t reg = UINT64_C(0x1122334455667788);
    uint64_t value = 0;

    g_assert_false(hp_sba_reg64_access_covers_low32(DEVICE_BIG_ENDIAN, 0, 4));
    g_assert_true(hp_sba_reg64_access_covers_low32(DEVICE_BIG_ENDIAN, 4, 4));
    g_assert_true(hp_sba_reg64_access_covers_low32(DEVICE_BIG_ENDIAN, 0, 8));
    g_assert_true(hp_sba_reg64_read(DEVICE_BIG_ENDIAN, reg, 0, 4, &value));
    g_assert_cmphex(value, ==, UINT32_C(0x11223344));
    g_assert_true(hp_sba_reg64_read(DEVICE_BIG_ENDIAN, reg, 4, 4, &value));
    g_assert_cmphex(value, ==, UINT32_C(0x55667788));
    g_assert_true(hp_sba_reg64_write(DEVICE_BIG_ENDIAN, &reg, 0, 4,
                                     UINT32_C(0xaabbccdd)));
    g_assert_cmphex(reg, ==, UINT64_C(0xaabbccdd55667788));
}

static void test_reg64_invalid_unchanged(void)
{
    uint64_t reg = UINT64_C(0x1122334455667788);
    uint64_t value = UINT64_C(0xa5a5a5a5a5a5a5a5);

    g_assert_false(hp_sba_reg64_read(DEVICE_LITTLE_ENDIAN, reg, 1, 4, &value));
    g_assert_cmphex(value, ==, UINT64_C(0xa5a5a5a5a5a5a5a5));
    g_assert_false(
        hp_sba_reg64_write(DEVICE_LITTLE_ENDIAN, &reg, 4, 8, UINT64_MAX));
    g_assert_cmphex(reg, ==, UINT64_C(0x1122334455667788));
    g_assert_false(hp_sba_reg64_write(0, &reg, 0, 4, 0));
    g_assert_cmphex(reg, ==, UINT64_C(0x1122334455667788));
    g_assert_false(
        hp_sba_reg64_access_covers_low32(DEVICE_LITTLE_ENDIAN, 1, 4));
    g_assert_false(
        hp_sba_reg64_access_covers_low32(DEVICE_LITTLE_ENDIAN, 4, 8));
    g_assert_false(hp_sba_reg64_access_covers_low32(0, 0, 4));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-sba-routing/variant/valid", test_variant_valid);
    g_test_add_func("/hp-sba-routing/variant/rejects-bad-topology",
                    test_variant_rejects_bad_topology);
    g_test_add_func("/hp-sba-routing/variant/allows-bundled-root",
                    test_variant_allows_bundled_root);
    g_test_add_func("/hp-sba-routing/variant/rope-lookup", test_rope_lookup);
    g_test_add_func("/hp-sba-routing/variant/direct-route",
                    test_direct_route_lookup);
    g_test_add_func("/hp-sba-routing/variant/callbacks", test_callbacks);
    g_test_add_func("/hp-sba-routing/register/little-endian",
                    test_reg64_little_endian);
    g_test_add_func("/hp-sba-routing/register/big-endian",
                    test_reg64_big_endian);
    g_test_add_func("/hp-sba-routing/register/invalid-unchanged",
                    test_reg64_invalid_unchanged);
    return g_test_run();
}
