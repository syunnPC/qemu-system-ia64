/*
 * IA-64 zx6000 ZX1 integration test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_zx6000_zx1_test_layout.h"
#include "hw/pci-host/hp-zx1-iommu.h"
#include "qapi/error.h"
#include "qemu/host-utils.h"

typedef struct ZX1TestNamedRange {
    const IA64ZX6000ZX1TestRange *range;
    const char *name;
} ZX1TestNamedRange;

G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_IOMMU_SIZE /
                IA64_ZX6000_ZX1_TEST_PAGE_SIZE * sizeof(uint64_t) ==
                IA64_ZX6000_ZX1_TEST_PDIR_SIZE);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_TARGET_PAGES >= 18);
/* The intervening CCSR range is reserved. */
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_MIO_BASE +
                2 * IA64_ZX6000_ZX1_TEST_MIO_SIZE ==
                IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE +
                IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE ==
                IA64_ZX6000_ZX1_TEST_IOA1_CSR_BASE);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_ROOT0_BUS_MODE_RESET ==
                HP_ZX1_IOA_BUS_MODE_ROPE_2X_L);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_ROOT1_BUS_MODE_RESET ==
                (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT));
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT ==
                HP_ZX1_IOA_EXTERNAL_INPUTS);
G_STATIC_ASSERT(IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE ==
                HP_ZX1_IOA_CONFIG_APERTURE_SIZE);

static bool zx1_test_range_valid(const IA64ZX6000ZX1TestRange *range,
                                 const char *name, Error **errp)
{
    if (!range->size || range->base > UINT64_MAX - range->size) {
        error_setg(errp, "zx6000 zx1 test %s range is empty or overflows",
                   name);
        return false;
    }
    return true;
}

static uint64_t zx1_test_range_end(const IA64ZX6000ZX1TestRange *range)
{
    return range->base + range->size;
}

static bool zx1_test_range_contains(const IA64ZX6000ZX1TestRange *outer,
                                    const IA64ZX6000ZX1TestRange *inner)
{
    return inner->base >= outer->base &&
           zx1_test_range_end(inner) <= zx1_test_range_end(outer);
}

static bool zx1_test_ranges_overlap(const IA64ZX6000ZX1TestRange *a,
                                    const IA64ZX6000ZX1TestRange *b)
{
    return a->base < zx1_test_range_end(b) && b->base < zx1_test_range_end(a);
}

static bool zx1_test_range_aligned(const IA64ZX6000ZX1TestRange *range,
                                   uint64_t alignment, const char *name,
                                   Error **errp)
{
    if (!is_power_of_2(alignment) || (range->base & (alignment - 1)) ||
        (range->size & (alignment - 1))) {
        error_setg(errp, "zx6000 zx1 test %s range is misaligned", name);
        return false;
    }
    return true;
}

static bool zx1_test_range_power2_aligned(const IA64ZX6000ZX1TestRange *range,
                                          const char *name, Error **errp)
{
    if (!is_power_of_2(range->size) || (range->base & (range->size - 1))) {
        error_setg(errp, "zx6000 zx1 test %s range is not power-of-two aligned",
                   name);
        return false;
    }
    return true;
}

static bool zx1_test_range_matches(const IA64ZX6000ZX1TestRange *range,
                                   uint64_t base, uint64_t size,
                                   const char *name, Error **errp)
{
    if (range->base != base || range->size != size) {
        error_setg(errp,
                   "zx6000 zx1 test %s range does not match the fixed layout",
                   name);
        return false;
    }
    return true;
}

static unsigned int zx1_test_popcount(uint32_t value)
{
    unsigned int count = 0;

    while (value) {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

static unsigned int zx1_test_mode_inputs(HPZX1IOAMode mode)
{
    return mode == HP_ZX1_IOA_MODE_AGP ? IA64_ZX6000_ZX1_TEST_AGP_INPUT_COUNT
                                       : IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT;
}

static bool zx1_test_validate_root(const IA64ZX6000ZX1TestRoot *root,
                                   unsigned int index, Error **errp)
{
    uint64_t bus_operation;
    unsigned int ropes;
    unsigned int inputs;
    unsigned int pin;
    unsigned int slot;
    bool agp_mode;
    bool dual_rope;

    if ((unsigned int)root->mode > HP_ZX1_IOA_MODE_AGP) {
        error_setg(errp, "zx6000 zx1 test root %u mode is invalid", index);
        return false;
    }
    if (root->first_bus > root->last_bus) {
        error_setg(errp, "zx6000 zx1 test root %u bus range is reversed",
                   index);
        return false;
    }
    if (!root->rope_mask ||
        (root->rope_mask &
         ~((UINT32_C(1) << IA64_ZX6000_ZX1_TEST_ROPE_COUNT) - 1))) {
        error_setg(errp, "zx6000 zx1 test root %u rope mask is invalid", index);
        return false;
    }

    ropes = zx1_test_popcount(root->rope_mask);
    if (ropes > 2 || (root->mode == HP_ZX1_IOA_MODE_AGP && ropes != 2)) {
        error_setg(errp, "zx6000 zx1 test root %u rope bundle is invalid",
                   index);
        return false;
    }

    agp_mode = root->bus_mode_reset & HP_ZX1_IOA_BUS_MODE_AGP;
    dual_rope = !(root->bus_mode_reset & HP_ZX1_IOA_BUS_MODE_ROPE_2X_L);
    bus_operation = root->bus_mode_reset & HP_ZX1_IOA_BUS_MODE_BUS_MASK;
    if (agp_mode != (root->mode == HP_ZX1_IOA_MODE_AGP) ||
        dual_rope != (ropes == 2) ||
        (root->mode == HP_ZX1_IOA_MODE_PCI && bus_operation != 0) ||
        (root->mode == HP_ZX1_IOA_MODE_PCIX && bus_operation == 0)) {
        error_setg(errp,
                   "zx6000 zx1 test root %u bus-mode straps conflict "
                   "with its mode or rope bundle",
                   index);
        return false;
    }

    inputs = zx1_test_mode_inputs(root->mode);
    for (slot = 0; slot < IA64_ZX6000_ZX1_TEST_SLOT_COUNT; slot++) {
        for (pin = 0; pin < IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT; pin++) {
            uint8_t input = root->intx_route[slot][pin];

            if (input >= inputs) {
                error_setg(errp,
                           "zx6000 zx1 test root %u slot %u pin %u "
                           "uses invalid input %u",
                           index, slot, pin, input);
                return false;
            }
            if (input != pin) {
                error_setg(errp,
                           "zx6000 zx1 test root %u slot %u pin %u "
                           "does not aggregate to the matching input",
                           index, slot, pin);
                return false;
            }
        }
    }

    if (root->pci_mmio.size != root->cpu_mmio.size) {
        error_setg(errp,
                   "zx6000 zx1 test root %u PCI and CPU MMIO sizes "
                   "differ",
                   index);
        return false;
    }
    if (!zx1_test_range_power2_aligned(&root->pci_mmio, "root PCI MMIO",
                                       errp) ||
        !zx1_test_range_power2_aligned(&root->cpu_mmio, "root CPU MMIO",
                                       errp)) {
        return false;
    }
    return true;
}

static bool zx1_test_validate_iommu(const IA64ZX6000ZX1TestLayout *layout,
                                    Error **errp)
{
    const IA64ZX6000ZX1TestIOMMU *iommu = &layout->iommu;
    HPZX1IOMMUWindow window;
    unsigned int page_shift;
    uint64_t min_target_size;
    uint64_t page_count;
    uint64_t required_pdir_size;

    if (!hp_zx1_iommu_decode_tcnfg(iommu->tcnfg_reset, &page_shift) ||
        !hp_zx1_iommu_decode_window(iommu->ibase_reset, iommu->imask_reset,
                                    page_shift, &window)) {
        error_setg(errp, "zx6000 zx1 test IOMMU reset encoding is invalid");
        return false;
    }
    if (window.enabled || window.base != iommu->aperture.base ||
        window.aperture_size != iommu->aperture.size ||
        (UINT64_C(1) << page_shift) != IA64_ZX6000_ZX1_TEST_PAGE_SIZE) {
        error_setg(errp,
                   "zx6000 zx1 test IOMMU disabled reset state is invalid");
        return false;
    }
    if (iommu->pdir_base_reset != iommu->pdir.base ||
        (iommu->pdir_base_reset & (sizeof(uint64_t) - 1))) {
        error_setg(errp, "zx6000 zx1 test IOMMU PDIR base is invalid");
        return false;
    }

    page_count = window.aperture_size >> page_shift;
    if (!page_count || page_count > UINT64_MAX / sizeof(uint64_t)) {
        error_setg(errp, "zx6000 zx1 test IOMMU page count overflows PDIR");
        return false;
    }
    required_pdir_size = page_count * sizeof(uint64_t);
    if (iommu->pdir.size < required_pdir_size ||
        iommu->pdir.size % sizeof(uint64_t)) {
        error_setg(errp, "zx6000 zx1 test PDIR cannot cover the aperture");
        return false;
    }
    if (zx1_test_range_end(&iommu->aperture) - 1 > HP_ZX1_IOMMU_PHYS_MASK ||
        zx1_test_range_end(&iommu->pdir) - 1 > HP_ZX1_IOMMU_PHYS_MASK ||
        zx1_test_range_end(&iommu->test_target) - 1 > HP_ZX1_IOMMU_PHYS_MASK) {
        error_setg(errp, "zx6000 zx1 test IOMMU storage exceeds 50-bit "
                         "physical addressing");
        return false;
    }

    min_target_size = IA64_ZX6000_ZX1_TEST_TARGET_PAGES << page_shift;
    if (iommu->test_target.size < min_target_size) {
        error_setg(errp,
                   "zx6000 zx1 test IOMMU test target has fewer than "
                   "%u pages",
                   IA64_ZX6000_ZX1_TEST_TARGET_PAGES);
        return false;
    }
    return true;
}

static bool
zx1_test_validate_fixed_layout(const IA64ZX6000ZX1TestLayout *layout,
                               Error **errp)
{
    static const struct {
        uint64_t csr_base;
        uint64_t cpu_mmio_base;
        uint32_t rope_mask;
        HPZX1IOAMode mode;
        uint64_t bus_mode_reset;
        uint8_t first_bus;
        uint8_t last_bus;
    } expected_roots[] = {
        {
            .csr_base = IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE,
            .cpu_mmio_base = IA64_ZX6000_ZX1_TEST_ROOT0_CPU_MMIO_BASE,
            .rope_mask = IA64_ZX6000_ZX1_TEST_ROOT0_ROPE_MASK,
            .mode = HP_ZX1_IOA_MODE_PCI,
            .bus_mode_reset = IA64_ZX6000_ZX1_TEST_ROOT0_BUS_MODE_RESET,
            .first_bus = IA64_ZX6000_ZX1_TEST_ROOT0_FIRST_BUS,
            .last_bus = IA64_ZX6000_ZX1_TEST_ROOT0_LAST_BUS,
        },
        {
            .csr_base = IA64_ZX6000_ZX1_TEST_IOA1_CSR_BASE,
            .cpu_mmio_base = IA64_ZX6000_ZX1_TEST_ROOT1_CPU_MMIO_BASE,
            .rope_mask = IA64_ZX6000_ZX1_TEST_ROOT1_ROPE_MASK,
            .mode = HP_ZX1_IOA_MODE_PCIX,
            .bus_mode_reset = IA64_ZX6000_ZX1_TEST_ROOT1_BUS_MODE_RESET,
            .first_bus = IA64_ZX6000_ZX1_TEST_ROOT1_FIRST_BUS,
            .last_bus = IA64_ZX6000_ZX1_TEST_ROOT1_LAST_BUS,
        },
    };
    G_STATIC_ASSERT(G_N_ELEMENTS(expected_roots) ==
                    IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    unsigned int i;

    if (!zx1_test_range_matches(&layout->ram, IA64_ZX6000_ZX1_TEST_RAM_BASE,
                                IA64_ZX6000_ZX1_TEST_RAM_SIZE, "RAM", errp) ||
        !zx1_test_range_matches(&layout->mio, IA64_ZX6000_ZX1_TEST_MIO_BASE,
                                IA64_ZX6000_ZX1_TEST_MIO_SIZE, "MIO", errp) ||
        !zx1_test_range_matches(&layout->pib, IA64_ZX6000_ZX1_TEST_PIB_BASE,
                                IA64_ZX6000_ZX1_TEST_PIB_SIZE, "PIB", errp) ||
        !zx1_test_range_matches(
            &layout->iommu.aperture, IA64_ZX6000_ZX1_TEST_IOMMU_BASE,
            IA64_ZX6000_ZX1_TEST_IOMMU_SIZE, "IOMMU aperture", errp) ||
        !zx1_test_range_matches(&layout->iommu.pdir,
                                IA64_ZX6000_ZX1_TEST_PDIR_BASE,
                                IA64_ZX6000_ZX1_TEST_PDIR_SIZE, "PDIR", errp) ||
        !zx1_test_range_matches(
            &layout->iommu.test_target, IA64_ZX6000_ZX1_TEST_TARGET_BASE,
            IA64_ZX6000_ZX1_TEST_TARGET_SIZE, "IOMMU test target", errp)) {
        return false;
    }
    if (layout->iommu.ibase_reset != IA64_ZX6000_ZX1_TEST_IOMMU_IBASE_RESET ||
        layout->iommu.imask_reset != IA64_ZX6000_ZX1_TEST_IOMMU_IMASK_RESET ||
        layout->iommu.pcom_reset != IA64_ZX6000_ZX1_TEST_IOMMU_PCOM_RESET ||
        layout->iommu.tcnfg_reset != IA64_ZX6000_ZX1_TEST_IOMMU_TCNFG_RESET ||
        layout->iommu.pdir_base_reset != IA64_ZX6000_ZX1_TEST_PDIR_BASE) {
        error_setg(errp, "zx6000 zx1 test IOMMU reset values do not match the "
                         "fixed layout");
        return false;
    }

    for (i = 0; i < G_N_ELEMENTS(expected_roots); i++) {
        const IA64ZX6000ZX1TestRoot *root = &layout->roots[i];

        if (!zx1_test_range_matches(&root->ioa_csr, expected_roots[i].csr_base,
                                    IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE,
                                    "Mercury CSR", errp) ||
            !zx1_test_range_matches(
                &root->pci_mmio, IA64_ZX6000_ZX1_TEST_PCI_MMIO_BASE,
                IA64_ZX6000_ZX1_TEST_MMIO_SIZE, "root PCI MMIO", errp) ||
            !zx1_test_range_matches(
                &root->cpu_mmio, expected_roots[i].cpu_mmio_base,
                IA64_ZX6000_ZX1_TEST_MMIO_SIZE, "root CPU MMIO", errp)) {
            return false;
        }
        if (root->mode != expected_roots[i].mode ||
            root->rope_mask != expected_roots[i].rope_mask ||
            root->bus_mode_reset != expected_roots[i].bus_mode_reset ||
            root->first_bus != expected_roots[i].first_bus ||
            root->last_bus != expected_roots[i].last_bus) {
            error_setg(
                errp, "zx6000 zx1 test root %u does not match the fixed layout",
                i);
            return false;
        }
    }
    return true;
}

void ia64_zx6000_zx1_test_layout_init(IA64ZX6000ZX1TestLayout *layout)
{
    unsigned int root;
    unsigned int pin;
    unsigned int slot;

    g_assert(layout != NULL);
    *layout = (IA64ZX6000ZX1TestLayout) {
        .ram = {
            IA64_ZX6000_ZX1_TEST_RAM_BASE,
            IA64_ZX6000_ZX1_TEST_RAM_SIZE,
        },
        .mio = {
            IA64_ZX6000_ZX1_TEST_MIO_BASE,
            IA64_ZX6000_ZX1_TEST_MIO_SIZE,
        },
        .pib = {
            IA64_ZX6000_ZX1_TEST_PIB_BASE,
            IA64_ZX6000_ZX1_TEST_PIB_SIZE,
        },
        .iommu = {
            .aperture = {
                IA64_ZX6000_ZX1_TEST_IOMMU_BASE,
                IA64_ZX6000_ZX1_TEST_IOMMU_SIZE,
            },
            .pdir = {
                IA64_ZX6000_ZX1_TEST_PDIR_BASE,
                IA64_ZX6000_ZX1_TEST_PDIR_SIZE,
            },
            .test_target = {
                IA64_ZX6000_ZX1_TEST_TARGET_BASE,
                IA64_ZX6000_ZX1_TEST_TARGET_SIZE,
            },
            .ibase_reset = IA64_ZX6000_ZX1_TEST_IOMMU_IBASE_RESET,
            .imask_reset = IA64_ZX6000_ZX1_TEST_IOMMU_IMASK_RESET,
            .pcom_reset = IA64_ZX6000_ZX1_TEST_IOMMU_PCOM_RESET,
            .tcnfg_reset = IA64_ZX6000_ZX1_TEST_IOMMU_TCNFG_RESET,
            .pdir_base_reset = IA64_ZX6000_ZX1_TEST_PDIR_BASE,
        },
        .roots = {
            {
                .ioa_csr = {
                    IA64_ZX6000_ZX1_TEST_IOA0_CSR_BASE,
                    IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE,
                },
                .pci_mmio = {
                    IA64_ZX6000_ZX1_TEST_PCI_MMIO_BASE,
                    IA64_ZX6000_ZX1_TEST_MMIO_SIZE,
                },
                .cpu_mmio = {
                    IA64_ZX6000_ZX1_TEST_ROOT0_CPU_MMIO_BASE,
                    IA64_ZX6000_ZX1_TEST_MMIO_SIZE,
                },
                .rope_mask = IA64_ZX6000_ZX1_TEST_ROOT0_ROPE_MASK,
                .mode = HP_ZX1_IOA_MODE_PCI,
                .bus_mode_reset =
                    IA64_ZX6000_ZX1_TEST_ROOT0_BUS_MODE_RESET,
                .first_bus = IA64_ZX6000_ZX1_TEST_ROOT0_FIRST_BUS,
                .last_bus = IA64_ZX6000_ZX1_TEST_ROOT0_LAST_BUS,
            },
            {
                .ioa_csr = {
                    IA64_ZX6000_ZX1_TEST_IOA1_CSR_BASE,
                    IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE,
                },
                .pci_mmio = {
                    IA64_ZX6000_ZX1_TEST_PCI_MMIO_BASE,
                    IA64_ZX6000_ZX1_TEST_MMIO_SIZE,
                },
                .cpu_mmio = {
                    IA64_ZX6000_ZX1_TEST_ROOT1_CPU_MMIO_BASE,
                    IA64_ZX6000_ZX1_TEST_MMIO_SIZE,
                },
                .rope_mask = IA64_ZX6000_ZX1_TEST_ROOT1_ROPE_MASK,
                .mode = HP_ZX1_IOA_MODE_PCIX,
                .bus_mode_reset =
                    IA64_ZX6000_ZX1_TEST_ROOT1_BUS_MODE_RESET,
                .first_bus = IA64_ZX6000_ZX1_TEST_ROOT1_FIRST_BUS,
                .last_bus = IA64_ZX6000_ZX1_TEST_ROOT1_LAST_BUS,
            },
        },
    };

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        for (slot = 0; slot < IA64_ZX6000_ZX1_TEST_SLOT_COUNT; slot++) {
            for (pin = 0; pin < IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT; pin++) {
                layout->roots[root].intx_route[slot][pin] = pin;
            }
        }
    }
}

bool ia64_zx6000_zx1_test_layout_validate(const IA64ZX6000ZX1TestLayout *layout,
                                          Error **errp)
{
    ZX1TestNamedRange cpu_resources[3 + 2 * IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    unsigned int resource_count = 0;
    unsigned int i;
    unsigned int j;

    if (!layout) {
        error_setg(errp, "zx6000 zx1 test layout is NULL");
        return false;
    }

    cpu_resources[resource_count++] =
        (ZX1TestNamedRange) { &layout->ram, "RAM" };
    cpu_resources[resource_count++] =
        (ZX1TestNamedRange) { &layout->mio, "MIO" };
    cpu_resources[resource_count++] =
        (ZX1TestNamedRange) { &layout->pib, "PIB" };
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        cpu_resources[resource_count++] =
            (ZX1TestNamedRange) { &layout->roots[i].ioa_csr, "Mercury CSR" };
        cpu_resources[resource_count++] =
            (ZX1TestNamedRange) { &layout->roots[i].cpu_mmio, "root CPU MMIO" };
    }

    for (i = 0; i < resource_count; i++) {
        if (!zx1_test_range_valid(cpu_resources[i].range, cpu_resources[i].name,
                                  errp)) {
            return false;
        }
        if (zx1_test_range_end(cpu_resources[i].range) - 1 >
            HP_ZX1_IOMMU_PHYS_MASK) {
            error_setg(errp, "zx6000 zx1 test %s exceeds 50-bit addressing",
                       cpu_resources[i].name);
            return false;
        }
        for (j = 0; j < i; j++) {
            if (zx1_test_ranges_overlap(cpu_resources[j].range,
                                        cpu_resources[i].range)) {
                error_setg(errp, "zx6000 zx1 test %s overlaps %s",
                           cpu_resources[j].name, cpu_resources[i].name);
                return false;
            }
        }
    }

    if (!zx1_test_range_valid(&layout->iommu.aperture, "IOMMU aperture",
                              errp) ||
        !zx1_test_range_valid(&layout->iommu.pdir, "PDIR", errp) ||
        !zx1_test_range_valid(&layout->iommu.test_target, "IOMMU test target",
                              errp) ||
        !zx1_test_range_aligned(&layout->ram, IA64_ZX6000_ZX1_TEST_PAGE_SIZE,
                                "RAM", errp) ||
        !zx1_test_range_aligned(&layout->mio, IA64_ZX6000_ZX1_TEST_MIO_SIZE,
                                "MIO", errp) ||
        !zx1_test_range_aligned(&layout->pib, IA64_ZX6000_ZX1_TEST_PIB_SIZE,
                                "PIB", errp) ||
        !zx1_test_range_power2_aligned(&layout->iommu.aperture,
                                       "IOMMU aperture", errp) ||
        !zx1_test_range_aligned(&layout->iommu.pdir,
                                IA64_ZX6000_ZX1_TEST_PAGE_SIZE, "PDIR", errp) ||
        !zx1_test_range_aligned(&layout->iommu.test_target,
                                IA64_ZX6000_ZX1_TEST_PAGE_SIZE,
                                "IOMMU test target", errp)) {
        return false;
    }

    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        const IA64ZX6000ZX1TestRoot *root = &layout->roots[i];

        if (!zx1_test_range_valid(&root->pci_mmio, "root PCI MMIO", errp) ||
            !zx1_test_range_aligned(&root->ioa_csr,
                                    IA64_ZX6000_ZX1_TEST_IOA_CSR_SIZE,
                                    "Mercury CSR", errp) ||
            !zx1_test_validate_root(root, i, errp)) {
            return false;
        }
        for (j = 0; j < i; j++) {
            const IA64ZX6000ZX1TestRoot *other = &layout->roots[j];

            if (root->first_bus <= other->last_bus &&
                other->first_bus <= root->last_bus) {
                error_setg(errp,
                           "zx6000 zx1 test roots %u and %u overlap buses", j,
                           i);
                return false;
            }
            if (root->rope_mask & other->rope_mask) {
                error_setg(errp,
                           "zx6000 zx1 test roots %u and %u overlap ropes", j,
                           i);
                return false;
            }
        }
    }

    if (!zx1_test_range_contains(&layout->ram, &layout->iommu.pdir) ||
        !zx1_test_range_contains(&layout->ram, &layout->iommu.test_target) ||
        zx1_test_ranges_overlap(&layout->iommu.pdir,
                                &layout->iommu.test_target)) {
        error_setg(errp, "zx6000 zx1 test PDIR/test target RAM reservations "
                         "are invalid");
        return false;
    }
    if (!zx1_test_validate_iommu(layout, errp)) {
        return false;
    }
    return zx1_test_validate_fixed_layout(layout, errp);
}
