/*
 * IA-64 i2000 460GX integration-test layout tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "qapi/error.h"

static IA64I2000460GXTestLayout fixture_layout(void)
{
    IA64I2000460GXTestLayout layout;

    ia64_i2000_460gx_test_layout_init(&layout);
    return layout;
}

static void assert_invalid(IA64I2000460GXTestLayout *layout)
{
    Error *err = NULL;

    g_assert_false(ia64_i2000_460gx_test_layout_validate(layout, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_fixed_layout(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();
    Error *err = NULL;

    g_assert_true(ia64_i2000_460gx_test_layout_validate(&layout, &err));
    g_assert_null(err);

    g_assert_cmphex(layout.ram.base, ==, 0);
    g_assert_cmphex(layout.ram.size, ==, UINT64_C(0x80000000));
    g_assert_cmphex(layout.firmware.base, ==, UINT64_C(0x00100000));
    g_assert_cmphex(layout.firmware.size, ==, UINT64_C(0x00200000));
    g_assert_cmphex(layout.descriptor_rom.base, ==, UINT64_C(0x00300000));
    g_assert_cmphex(layout.descriptor_rom.size, ==, UINT64_C(0x1000));
    g_assert_cmphex(layout.descriptor_envelope.size, ==, UINT64_C(0x2000));

    g_assert_cmphex(layout.roots[0].pci_mmio32_base, ==, UINT64_C(0x90000000));
    g_assert_cmphex(layout.roots[1].pci_mmio32_base, ==, UINT64_C(0xa0000000));
    g_assert_cmphex(layout.roots[2].pci_mmio32_base, ==, UINT64_C(0xb0000000));
    g_assert_cmpuint(layout.roots[0].first_bus, ==, 0x00);
    g_assert_cmpuint(layout.roots[0].last_bus, ==, 0x00);
    g_assert_cmpuint(layout.roots[1].first_bus, ==, 0x01);
    g_assert_cmpuint(layout.roots[1].last_bus, ==, 0x01);
    g_assert_cmpuint(layout.roots[2].first_bus, ==, 0x02);
    g_assert_cmpuint(layout.roots[2].last_bus, ==, 0x02);
    g_assert_cmpuint(layout.roots[0].io_base, ==, 0x0000);
    g_assert_cmpuint(layout.roots[1].io_base, ==, 0x4000);
    g_assert_cmpuint(layout.roots[2].io_base, ==, 0x8000);
    g_assert_cmpuint(layout.roots[0].io_size, ==, 0x4000);
    g_assert_cmpuint(layout.roots[1].io_size, ==, 0x4000);
    g_assert_cmpuint(layout.roots[2].io_size, ==, 0x4000);
    g_assert_cmpuint(layout.roots[0].host_port, ==, 0);
    g_assert_cmpuint(layout.roots[1].host_port, ==, 1);
    g_assert_cmpuint(layout.roots[2].host_port, ==, 2);
    g_assert_cmpuint(layout.roots[0].intx_base, ==, 16);
    g_assert_cmpuint(layout.roots[1].intx_base, ==, 20);
    g_assert_cmpuint(layout.roots[2].intx_base, ==, 24);
    g_assert_cmphex(layout.roots[0].cpu_mmio32_base, ==,
                    layout.roots[0].pci_mmio32_base);
    g_assert_cmphex(layout.roots[0].mmio64_size, ==, 0);

    g_assert_cmphex(layout.pid_decode.base, ==, UINT64_C(0xfec00000));
    g_assert_cmphex(layout.pib.base, ==, UINT64_C(0xfee00000));
    g_assert_cmphex(layout.legacy_io.base, ==, UINT64_C(0x0000000ffc000000));
    g_assert_cmphex(layout.cf8_pa, ==, UINT64_C(0x0000000ffc33ecf8));
    g_assert_cmphex(layout.cfc_pa, ==, UINT64_C(0x0000000ffc33fcfc));
    g_assert_cmphex(layout.roots[0].dma_base, ==, 0);
    g_assert_cmphex(layout.roots[0].dma_size, ==, UINT64_C(0x80000000));
    g_assert_cmphex(layout.roots[0].dma_target_offset, ==, 0);
    g_assert_cmpuint(layout.cbn, ==, 0xff);
    g_assert_cmphex(layout.chipset_present, ==, 0);
    g_assert_cmpuint(layout.pid_id, ==, 0);
    g_assert_cmpuint(layout.pid_pin_count, ==, 64);
    g_assert_cmpuint(layout.legacy_pin_count, ==, 16);
    g_assert_cmpuint(IA64_I2000_460GX_TEST_CONSOLE_CANDIDATE_GSI, ==, 4);
    g_assert_cmpuint(layout.cf8_io_root, ==, 0);
}

static void test_sparse_io_mapping(void)
{
    g_assert_cmphex(ia64_i2000_460gx_test_sparse_io_pa(0), ==,
                    IA64_I2000_460GX_TEST_LEGACY_IO_BASE);
    g_assert_cmphex(
        ia64_i2000_460gx_test_sparse_io_pa(IA64_I2000_460GX_TEST_CF8_PORT), ==,
        IA64_I2000_460GX_TEST_CF8_PA);
    g_assert_cmphex(
        ia64_i2000_460gx_test_sparse_io_pa(IA64_I2000_460GX_TEST_CFC_PORT), ==,
        IA64_I2000_460GX_TEST_CFC_PA);
    g_assert_cmphex(ia64_i2000_460gx_test_sparse_io_pa(UINT16_MAX), ==,
                    IA64_I2000_460GX_TEST_LEGACY_IO_BASE +
                        IA64_I2000_460GX_TEST_LEGACY_IO_SIZE - 1);
}

static void test_null_layout(void)
{
    Error *err = NULL;

    g_assert_false(ia64_i2000_460gx_test_layout_validate(NULL, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_ram_and_reservations(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();

    layout.ram.size--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.firmware.size++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.descriptor_rom.size = layout.descriptor_envelope.size + 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.descriptor_envelope.size--;
    assert_invalid(&layout);
}

static void test_fixed_resources(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();

    layout.pid_decode.base++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.pib.size--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.legacy_io.base = layout.pib.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.legacy_io.base++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.legacy_io.base = UINT64_MAX;
    layout.legacy_io.size = 2;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.cf8_pa++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.cfc_pa++;
    assert_invalid(&layout);
}

static void test_root_partitions(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();

    layout.roots[1].first_bus = layout.roots[0].last_bus;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].io_base =
        layout.roots[0].io_base + layout.roots[0].io_size - 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].pci_mmio32_base = layout.roots[0].pci_mmio32_base;
    layout.roots[1].cpu_mmio32_base = layout.roots[0].cpu_mmio32_base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].host_port = layout.roots[0].host_port;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].intx_base = layout.roots[0].intx_base + 3;
    assert_invalid(&layout);
}

static void test_root_apertures(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();

    layout.roots[0].cpu_mmio32_base = layout.roots[0].pci_mmio32_base + 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].pci_mmio32_base = UINT64_C(0x70000000);
    layout.roots[0].cpu_mmio32_base = UINT64_C(0x70000000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].pci_mmio32_base = UINT64_MAX;
    layout.roots[0].cpu_mmio32_base = UINT64_MAX;
    layout.roots[0].mmio32_size = 2;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].mmio64_base = UINT64_C(0x100000000);
    layout.roots[0].mmio64_size = UINT64_C(0x1000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].dma_base++;
    layout.roots[0].dma_size--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].dma_target_offset++;
    assert_invalid(&layout);
}

static void test_fixed_host_layout(void)
{
    IA64I2000460GXTestLayout layout = fixture_layout();

    /* This remains structurally valid but differs from the fixed map. */
    layout.roots[0].last_bus--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.cbn--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.chipset_present = 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.pid_id = 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.cf8_io_root = 1;
    assert_invalid(&layout);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ia64/i2000-460gx-layout/fixed", test_fixed_layout);
    g_test_add_func("/ia64/i2000-460gx-layout/sparse-io",
                    test_sparse_io_mapping);
    g_test_add_func("/ia64/i2000-460gx-layout/null", test_null_layout);
    g_test_add_func("/ia64/i2000-460gx-layout/ram-reservations",
                    test_ram_and_reservations);
    g_test_add_func("/ia64/i2000-460gx-layout/fixed-resources",
                    test_fixed_resources);
    g_test_add_func("/ia64/i2000-460gx-layout/root-partitions",
                    test_root_partitions);
    g_test_add_func("/ia64/i2000-460gx-layout/root-apertures",
                    test_root_apertures);
    g_test_add_func("/ia64/i2000-460gx-layout/fixed-host",
                    test_fixed_host_layout);

    return g_test_run();
}
