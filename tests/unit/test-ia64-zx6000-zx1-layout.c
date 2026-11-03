/*
 * IA-64 zx6000 ZX1 integration layout tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_zx6000_zx1_test_layout.h"
#include "qapi/error.h"

static IA64ZX6000ZX1TestLayout fixture_layout(void)
{
    IA64ZX6000ZX1TestLayout layout;

    ia64_zx6000_zx1_test_layout_init(&layout);
    return layout;
}

static void assert_invalid(const IA64ZX6000ZX1TestLayout *layout)
{
    IA64ZX6000ZX1TestLayout before = *layout;
    Error *err = NULL;

    g_assert_false(ia64_zx6000_zx1_test_layout_validate(layout, &err));
    g_assert_nonnull(err);
    g_assert_cmpmem(layout, sizeof(*layout), &before, sizeof(before));
    error_free(err);
}

static void test_fixed_layout(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();
    Error *err = NULL;
    unsigned int pin;
    unsigned int root;
    unsigned int slot;

    g_assert_true(ia64_zx6000_zx1_test_layout_validate(&layout, &err));
    g_assert_null(err);

    g_assert_cmphex(layout.ram.base, ==, UINT64_C(0));
    g_assert_cmphex(layout.ram.size, ==, UINT64_C(0x20000000));
    g_assert_cmphex(layout.mio.base, ==, UINT64_C(0xfed00000));
    g_assert_cmphex(layout.mio.size, ==, UINT64_C(0x10000));
    g_assert_cmphex(layout.pib.base, ==, UINT64_C(0xfee00000));
    g_assert_cmphex(layout.pib.size, ==, UINT64_C(0x100000));

    g_assert_cmphex(layout.iommu.aperture.base, ==, UINT64_C(0x40000000));
    g_assert_cmphex(layout.iommu.aperture.size, ==, UINT64_C(0x10000000));
    g_assert_cmphex(layout.iommu.ibase_reset, ==, UINT64_C(0x40000000));
    g_assert_cmphex(layout.iommu.imask_reset, ==, UINT64_C(0xf0000000));
    g_assert_cmphex(layout.iommu.pcom_reset, ==, 0);
    g_assert_cmphex(layout.iommu.tcnfg_reset, ==, 0);
    g_assert_cmphex(layout.iommu.pdir_base_reset, ==, UINT64_C(0x01000000));
    g_assert_cmphex(layout.iommu.pdir.base, ==, UINT64_C(0x01000000));
    g_assert_cmphex(layout.iommu.pdir.size, ==, UINT64_C(0x00080000));
    g_assert_cmphex(layout.iommu.test_target.base, ==, UINT64_C(0x02000000));
    g_assert_cmphex(layout.iommu.test_target.size, ==,
                    UINT64_C(18) * UINT64_C(0x1000));
    g_assert_cmphex(layout.iommu.pdir.size, ==,
                    layout.iommu.aperture.size / UINT64_C(0x1000) *
                        sizeof(uint64_t));

    g_assert_cmphex(layout.roots[0].ioa_csr.base, ==, UINT64_C(0xfed20000));
    g_assert_cmphex(layout.roots[1].ioa_csr.base, ==, UINT64_C(0xfed22000));
    g_assert_cmphex(layout.roots[0].ioa_csr.size, ==, UINT64_C(0x2000));
    g_assert_cmphex(layout.roots[1].ioa_csr.size, ==, UINT64_C(0x2000));
    g_assert_cmphex(layout.roots[0].pci_mmio.base, ==, 0);
    g_assert_cmphex(layout.roots[1].pci_mmio.base, ==, 0);
    g_assert_cmphex(layout.roots[0].pci_mmio.size, ==, UINT64_C(0x01000000));
    g_assert_cmphex(layout.roots[1].pci_mmio.size, ==, UINT64_C(0x01000000));
    g_assert_cmphex(layout.roots[0].cpu_mmio.base, ==, UINT64_C(0x90000000));
    g_assert_cmphex(layout.roots[1].cpu_mmio.base, ==, UINT64_C(0xa0000000));

    g_assert_cmpuint(layout.roots[0].mode, ==, HP_ZX1_IOA_MODE_PCI);
    g_assert_cmpuint(layout.roots[1].mode, ==, HP_ZX1_IOA_MODE_PCIX);
    g_assert_cmphex(layout.roots[0].rope_mask, ==, UINT32_C(0x01));
    g_assert_cmphex(layout.roots[1].rope_mask, ==, UINT32_C(0x0c));
    g_assert_cmphex(layout.roots[0].bus_mode_reset, ==, UINT64_C(0x20));
    g_assert_cmphex(layout.roots[1].bus_mode_reset, ==, UINT64_C(0x2000));
    g_assert_cmpuint(layout.roots[0].first_bus, ==, 0x20);
    g_assert_cmpuint(layout.roots[0].last_bus, ==, 0x2f);
    g_assert_cmpuint(layout.roots[1].first_bus, ==, 0x40);
    g_assert_cmpuint(layout.roots[1].last_bus, ==, 0x4f);

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        for (slot = 0; slot < IA64_ZX6000_ZX1_TEST_SLOT_COUNT; slot++) {
            for (pin = 0; pin < IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT; pin++) {
                g_assert_cmpuint(layout.roots[root].intx_route[slot][pin], ==,
                                 pin);
            }
        }
    }
}

static void test_null_layout(void)
{
    Error *err = NULL;

    g_assert_false(ia64_zx6000_zx1_test_layout_validate(NULL, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_range_validation(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.mio.base = UINT64_MAX;
    layout.mio.size = 2;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.mio.base++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].ioa_csr.base = UINT64_C(0xfed14001);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].pci_mmio.size = 0;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].pci_mmio.base = 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].cpu_mmio.size >>= 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.aperture.base = UINT64_MAX;
    layout.iommu.aperture.size = 2;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.pdir.base++;
    layout.iommu.pdir_base_reset++;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].cpu_mmio.base = UINT64_C(0x0004000000000000);
    assert_invalid(&layout);
}

static void test_cpu_resource_collisions(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.roots[0].ioa_csr.base = layout.mio.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].ioa_csr.base =
        layout.roots[0].ioa_csr.base + UINT64_C(0x1000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.mio.base = layout.pib.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].cpu_mmio.base = layout.roots[0].cpu_mmio.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.ram.size = UINT64_C(0x91000000);
    assert_invalid(&layout);
}

static void test_ram_reservations(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.iommu.pdir.base = UINT64_C(0x30000000);
    layout.iommu.pdir_base_reset = layout.iommu.pdir.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.test_target.base = UINT64_C(0x30000000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.test_target.base = layout.iommu.pdir.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.pdir.size -= IA64_ZX6000_ZX1_TEST_PAGE_SIZE;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.test_target.size =
        UINT64_C(17) * IA64_ZX6000_ZX1_TEST_PAGE_SIZE;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.pdir_base_reset += sizeof(uint64_t);
    assert_invalid(&layout);
}

static void test_bus_and_rope_partitions(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.roots[1].first_bus = layout.roots[0].last_bus;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].first_bus = 0x30;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].rope_mask = UINT32_C(0x04);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].rope_mask = 0;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].rope_mask = UINT32_C(0x100);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].rope_mask = UINT32_C(0x1c);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].bus_mode_reset &= ~UINT64_C(0x20);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].bus_mode_reset |= HP_ZX1_IOA_BUS_MODE_ROPE_2X_L;
    assert_invalid(&layout);
}

static void test_modes_and_routes(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.roots[0].mode = (HPZX1IOAMode)99;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].mode = HP_ZX1_IOA_MODE_PCIX;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].intx_route[31][3] = 10;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].intx_route[0][0] = 4;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[1].mode = HP_ZX1_IOA_MODE_AGP;
    layout.roots[1].bus_mode_reset = HP_ZX1_IOA_BUS_MODE_AGP;
    layout.roots[1].intx_route[0][0] = 7;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].mode = HP_ZX1_IOA_MODE_AGP;
    layout.roots[0].bus_mode_reset =
        HP_ZX1_IOA_BUS_MODE_AGP | HP_ZX1_IOA_BUS_MODE_ROPE_2X_L;
    assert_invalid(&layout);
}

static void test_iommu_encoding(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    layout.iommu.ibase_reset |= 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.imask_reset = UINT64_C(0xe0000000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.tcnfg_reset = 4;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.tcnfg_reset = 1;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.ibase_reset = UINT64_C(0x50000000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.aperture.base = UINT64_C(0x0004000000000000);
    layout.iommu.ibase_reset = layout.iommu.aperture.base;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.pcom_reset = UINT64_C(0x1000);
    assert_invalid(&layout);
}

static void test_fixed_layout_mutations(void)
{
    IA64ZX6000ZX1TestLayout layout = fixture_layout();

    /* Each mutation remains structurally safe but is outside the fixture. */
    layout.mio.base = UINT64_C(0xfec00000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].last_bus--;
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].rope_mask = UINT32_C(0x02);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.roots[0].cpu_mmio.base = UINT64_C(0x92000000);
    assert_invalid(&layout);

    layout = fixture_layout();
    layout.iommu.test_target.base = UINT64_C(0x03000000);
    assert_invalid(&layout);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ia64/zx6000-zx1-layout/fixed", test_fixed_layout);
    g_test_add_func("/ia64/zx6000-zx1-layout/null", test_null_layout);
    g_test_add_func("/ia64/zx6000-zx1-layout/ranges", test_range_validation);
    g_test_add_func("/ia64/zx6000-zx1-layout/cpu-collisions",
                    test_cpu_resource_collisions);
    g_test_add_func("/ia64/zx6000-zx1-layout/ram-reservations",
                    test_ram_reservations);
    g_test_add_func("/ia64/zx6000-zx1-layout/bus-rope-partitions",
                    test_bus_and_rope_partitions);
    g_test_add_func("/ia64/zx6000-zx1-layout/modes-routes",
                    test_modes_and_routes);
    g_test_add_func("/ia64/zx6000-zx1-layout/iommu-encoding",
                    test_iommu_encoding);
    g_test_add_func("/ia64/zx6000-zx1-layout/fixed-layout-mutations",
                    test_fixed_layout_mutations);

    return g_test_run();
}
