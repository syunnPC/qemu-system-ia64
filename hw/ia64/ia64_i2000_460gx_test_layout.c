/*
 * IA-64 i2000 460GX integration-test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "qapi/error.h"

static const IA64I2000460GXTestRoot fixed_roots[] = {
    {
        .segment = 0,
        .config_mechanism = 1,
        .first_bus = 0x00,
        .last_bus = 0x00,
        .host_port = 0,
        .intx_base = 16,
        .io_base = 0x0000,
        .io_size = 0x4000,
        .pci_mmio32_base = UINT64_C(0x90000000),
        .cpu_mmio32_base = UINT64_C(0x90000000),
        .mmio32_size = UINT64_C(0x10000000),
        .dma_base = IA64_I2000_460GX_TEST_DMA_BASE,
        .dma_size = IA64_I2000_460GX_TEST_DMA_SIZE,
        .dma_target_offset = IA64_I2000_460GX_TEST_DMA_BASE,
    },
    {
        .segment = 0,
        .config_mechanism = 1,
        .first_bus = 0x01,
        .last_bus = 0x01,
        .host_port = 1,
        .intx_base = 20,
        .io_base = 0x4000,
        .io_size = 0x4000,
        .pci_mmio32_base = UINT64_C(0xa0000000),
        .cpu_mmio32_base = UINT64_C(0xa0000000),
        .mmio32_size = UINT64_C(0x10000000),
        .dma_base = IA64_I2000_460GX_TEST_DMA_BASE,
        .dma_size = IA64_I2000_460GX_TEST_DMA_SIZE,
        .dma_target_offset = IA64_I2000_460GX_TEST_DMA_BASE,
    },
    {
        .segment = 0,
        .config_mechanism = 1,
        .first_bus = 0x02,
        .last_bus = 0x02,
        .host_port = 2,
        .intx_base = 24,
        .io_base = 0x8000,
        .io_size = 0x4000,
        .pci_mmio32_base = UINT64_C(0xb0000000),
        .cpu_mmio32_base = UINT64_C(0xb0000000),
        .mmio32_size = UINT64_C(0x10000000),
        .dma_base = IA64_I2000_460GX_TEST_DMA_BASE,
        .dma_size = IA64_I2000_460GX_TEST_DMA_SIZE,
        .dma_target_offset = IA64_I2000_460GX_TEST_DMA_BASE,
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(fixed_roots) ==
                IA64_I2000_460GX_TEST_ROOT_COUNT);
G_STATIC_ASSERT(IA64_I2000_460GX_TEST_CONSOLE_CANDIDATE_GSI <
                IA64_I2000_460GX_TEST_LEGACY_PIN_COUNT);

static bool fixture_range_valid(const IA64I2000460GXTestRange *range,
                                const char *name, Error **errp)
{
    if (range->size == 0 || range->base > UINT64_MAX - range->size) {
        error_setg(errp, "i2000 460GX test %s range is empty or overflows",
                   name);
        return false;
    }
    return true;
}

static uint64_t fixture_range_end(const IA64I2000460GXTestRange *range)
{
    return range->base + range->size;
}

static bool fixture_range_contains(const IA64I2000460GXTestRange *outer,
                                   const IA64I2000460GXTestRange *inner)
{
    return inner->base >= outer->base &&
           fixture_range_end(inner) <= fixture_range_end(outer);
}

static bool fixture_ranges_overlap(const IA64I2000460GXTestRange *a,
                                   const IA64I2000460GXTestRange *b)
{
    return a->base < fixture_range_end(b) && b->base < fixture_range_end(a);
}

static bool fixture_range_matches(const IA64I2000460GXTestRange *range,
                                  uint64_t base, uint64_t size,
                                  const char *name, Error **errp)
{
    if (range->base != base || range->size != size) {
        error_setg(errp,
                   "i2000 460GX test %s range does not match the fixed layout",
                   name);
        return false;
    }
    return true;
}

static bool fixture_root_matches(const IA64I2000460GXTestRoot *root,
                                 const IA64I2000460GXTestRoot *expected,
                                 unsigned index, Error **errp)
{
    if (root->segment != expected->segment ||
        root->config_mechanism != expected->config_mechanism ||
        root->first_bus != expected->first_bus ||
        root->last_bus != expected->last_bus ||
        root->host_port != expected->host_port ||
        root->intx_base != expected->intx_base ||
        root->io_base != expected->io_base ||
        root->io_size != expected->io_size ||
        root->pci_mmio32_base != expected->pci_mmio32_base ||
        root->cpu_mmio32_base != expected->cpu_mmio32_base ||
        root->mmio32_size != expected->mmio32_size ||
        root->mmio64_base != expected->mmio64_base ||
        root->mmio64_size != expected->mmio64_size ||
        root->dma_base != expected->dma_base ||
        root->dma_size != expected->dma_size ||
        root->dma_target_offset != expected->dma_target_offset) {
        error_setg(errp,
                   "i2000 460GX test root %u does not match the fixed layout",
                   index);
        return false;
    }
    return true;
}

uint64_t ia64_i2000_460gx_test_sparse_io_pa(uint16_t port)
{
    uint64_t offset = ((uint64_t)(port >> 2) << 12) | (port & 0xfffU);

    return IA64_I2000_460GX_TEST_LEGACY_IO_BASE + offset;
}

void ia64_i2000_460gx_test_layout_init(IA64I2000460GXTestLayout *layout)
{
    unsigned i;

    g_assert(layout != NULL);
    *layout = (IA64I2000460GXTestLayout) {
        .ram = {
            .base = IA64_I2000_460GX_TEST_RAM_BASE,
            .size = IA64_I2000_460GX_TEST_RAM_SIZE,
        },
        .firmware = {
            .base = IA64_I2000_460GX_TEST_FIRMWARE_BASE,
            .size = IA64_I2000_460GX_TEST_FIRMWARE_SIZE,
        },
        .descriptor_rom = {
            .base = IA64_I2000_460GX_TEST_DESC_ROM_BASE,
            .size = IA64_I2000_460GX_TEST_DESC_ROM_SIZE,
        },
        .descriptor_envelope = {
            .base = IA64_I2000_460GX_TEST_DESC_ROM_BASE,
            .size = IA64_I2000_460GX_TEST_DESC_ENVELOPE_SIZE,
        },
        .pid_decode = {
            .base = IA64_I2000_460GX_TEST_PID_BASE,
            .size = IA64_I2000_460GX_TEST_PID_SIZE,
        },
        .pid_envelope = {
            .base = IA64_I2000_460GX_TEST_PID_BASE,
            .size = IA64_I2000_460GX_TEST_PID_ENVELOPE_SIZE,
        },
        .pib = {
            .base = IA64_I2000_460GX_TEST_PIB_BASE,
            .size = IA64_I2000_460GX_TEST_PIB_SIZE,
        },
        .legacy_io = {
            .base = IA64_I2000_460GX_TEST_LEGACY_IO_BASE,
            .size = IA64_I2000_460GX_TEST_LEGACY_IO_SIZE,
        },
        .cf8_pa = IA64_I2000_460GX_TEST_CF8_PA,
        .cfc_pa = IA64_I2000_460GX_TEST_CFC_PA,
        .chipset_present = IA64_I2000_460GX_TEST_CHIPSET_PRESENT,
        .cbn = IA64_I2000_460GX_TEST_CBN,
        .pid_id = IA64_I2000_460GX_TEST_PID_ID,
        .pid_pin_count = IA64_I2000_460GX_TEST_PID_PIN_COUNT,
        .legacy_pin_count = IA64_I2000_460GX_TEST_LEGACY_PIN_COUNT,
        .cf8_io_root = IA64_I2000_460GX_TEST_CF8_IO_ROOT,
    };

    for (i = 0; i < G_N_ELEMENTS(fixed_roots); i++) {
        layout->roots[i] = fixed_roots[i];
    }
}

bool ia64_i2000_460gx_test_layout_validate(
    const IA64I2000460GXTestLayout *layout, Error **errp)
{
    const IA64I2000460GXTestRange *fixed_ranges[8];
    static const char *const fixed_range_names[] = {
        "RAM",
        "firmware",
        "descriptor ROM",
        "descriptor envelope",
        "PID decode",
        "PID envelope",
        "PIB",
        "legacy I/O",
    };
    unsigned i;
    unsigned j;

    if (layout == NULL) {
        error_setg(errp, "i2000 460GX test layout is NULL");
        return false;
    }

    fixed_ranges[0] = &layout->ram;
    fixed_ranges[1] = &layout->firmware;
    fixed_ranges[2] = &layout->descriptor_rom;
    fixed_ranges[3] = &layout->descriptor_envelope;
    fixed_ranges[4] = &layout->pid_decode;
    fixed_ranges[5] = &layout->pid_envelope;
    fixed_ranges[6] = &layout->pib;
    fixed_ranges[7] = &layout->legacy_io;

    for (i = 0; i < G_N_ELEMENTS(fixed_ranges); i++) {
        if (!fixture_range_valid(fixed_ranges[i], fixed_range_names[i], errp)) {
            return false;
        }
    }

    if (!fixture_range_contains(&layout->ram, &layout->firmware) ||
        !fixture_range_contains(&layout->ram, &layout->descriptor_envelope) ||
        !fixture_range_contains(&layout->descriptor_envelope,
                                &layout->descriptor_rom) ||
        fixture_ranges_overlap(&layout->firmware,
                               &layout->descriptor_envelope)) {
        error_setg(errp, "i2000 460GX test RAM, firmware, and descriptor "
                         "reservations are inconsistent");
        return false;
    }
    if (!fixture_range_contains(&layout->pid_envelope, &layout->pid_decode) ||
        fixture_ranges_overlap(&layout->pid_envelope, &layout->pib)) {
        error_setg(errp,
                   "i2000 460GX test PID and PIB ranges are inconsistent");
        return false;
    }
    if (fixture_ranges_overlap(&layout->ram, &layout->pid_envelope) ||
        fixture_ranges_overlap(&layout->ram, &layout->pib) ||
        fixture_ranges_overlap(&layout->ram, &layout->legacy_io) ||
        fixture_ranges_overlap(&layout->pid_envelope, &layout->legacy_io) ||
        fixture_ranges_overlap(&layout->pib, &layout->legacy_io)) {
        error_setg(errp, "i2000 460GX test fixed CPU resources overlap");
        return false;
    }
    if (layout->cf8_io_root >= IA64_I2000_460GX_TEST_ROOT_COUNT) {
        error_setg(errp, "i2000 460GX test CF8 I/O root is invalid");
        return false;
    }
    if (layout->pid_pin_count == 0 ||
        layout->legacy_pin_count > layout->pid_pin_count) {
        error_setg(errp, "i2000 460GX test PID pin partition is invalid");
        return false;
    }

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        const IA64I2000460GXTestRoot *root = &layout->roots[i];
        IA64I2000460GXTestRange pci_mmio = {
            .base = root->pci_mmio32_base,
            .size = root->mmio32_size,
        };
        IA64I2000460GXTestRange cpu_mmio = {
            .base = root->cpu_mmio32_base,
            .size = root->mmio32_size,
        };
        IA64I2000460GXTestRange dma = {
            .base = root->dma_base,
            .size = root->dma_size,
        };
        uint64_t io_end = (uint64_t)root->io_base + root->io_size;

        if (root->segment != 0 || root->config_mechanism != 1 ||
            root->first_bus > root->last_bus ||
            root->host_port >= IA64_I2000_460GX_TEST_HOST_PORT_COUNT ||
            root->io_size == 0 || io_end > UINT64_C(0x10000)) {
            error_setg(errp, "i2000 460GX test root %u layout is invalid", i);
            return false;
        }
        if (!fixture_range_valid(&pci_mmio, "root PCI MMIO32", errp) ||
            !fixture_range_valid(&cpu_mmio, "root CPU MMIO32", errp)) {
            return false;
        }
        if (root->pci_mmio32_base != root->cpu_mmio32_base ||
            root->mmio64_base != 0 || root->mmio64_size != 0) {
            error_setg(
                errp, "i2000 460GX test root %u MMIO apertures are invalid", i);
            return false;
        }
        if (!fixture_range_valid(&dma, "root DMA", errp)) {
            return false;
        }
        if (root->intx_base < layout->legacy_pin_count ||
            (unsigned)root->intx_base + IA64_I2000_460GX_TEST_PCI_INTX_COUNT >
                layout->pid_pin_count) {
            error_setg(errp, "i2000 460GX test root %u INTx range is invalid",
                       i);
            return false;
        }
        if (fixture_ranges_overlap(&cpu_mmio, &layout->ram) ||
            fixture_ranges_overlap(&cpu_mmio, &layout->pid_envelope) ||
            fixture_ranges_overlap(&cpu_mmio, &layout->pib) ||
            fixture_ranges_overlap(&cpu_mmio, &layout->legacy_io)) {
            error_setg(errp,
                       "i2000 460GX test root %u CPU MMIO32 collides with "
                       "a fixed resource",
                       i);
            return false;
        }
        if (root->dma_base != layout->ram.base ||
            root->dma_size != layout->ram.size ||
            root->dma_target_offset != root->dma_base - layout->ram.base ||
            root->dma_target_offset > layout->ram.size - root->dma_size) {
            error_setg(errp,
                       "i2000 460GX test root %u DMA alias is not the "
                       "identity RAM view",
                       i);
            return false;
        }

        for (j = 0; j < i; j++) {
            const IA64I2000460GXTestRoot *other = &layout->roots[j];
            IA64I2000460GXTestRange other_pci_mmio = {
                .base = other->pci_mmio32_base,
                .size = other->mmio32_size,
            };
            IA64I2000460GXTestRange other_cpu_mmio = {
                .base = other->cpu_mmio32_base,
                .size = other->mmio32_size,
            };

            if (root->first_bus <= other->last_bus &&
                other->first_bus <= root->last_bus) {
                error_setg(errp,
                           "i2000 460GX test roots %u and %u overlap buses", j,
                           i);
                return false;
            }
            if ((uint64_t)root->io_base <
                    (uint64_t)other->io_base + other->io_size &&
                (uint64_t)other->io_base < io_end) {
                error_setg(errp, "i2000 460GX test roots %u and %u overlap I/O",
                           j, i);
                return false;
            }
            if (fixture_ranges_overlap(&pci_mmio, &other_pci_mmio) ||
                fixture_ranges_overlap(&cpu_mmio, &other_cpu_mmio)) {
                error_setg(errp,
                           "i2000 460GX test roots %u and %u overlap MMIO", j,
                           i);
                return false;
            }
            if (root->host_port == other->host_port) {
                error_setg(errp,
                           "i2000 460GX test roots %u and %u share a host "
                           "port",
                           j, i);
                return false;
            }
            if (root->intx_base <
                    other->intx_base + IA64_I2000_460GX_TEST_PCI_INTX_COUNT &&
                other->intx_base <
                    root->intx_base + IA64_I2000_460GX_TEST_PCI_INTX_COUNT) {
                error_setg(errp,
                           "i2000 460GX test roots %u and %u overlap INTx", j,
                           i);
                return false;
            }
        }
    }

    if (layout->cf8_pa != ia64_i2000_460gx_test_sparse_io_pa(
                              IA64_I2000_460GX_TEST_CF8_PORT) ||
        layout->cfc_pa != ia64_i2000_460gx_test_sparse_io_pa(
                              IA64_I2000_460GX_TEST_CFC_PORT) ||
        layout->cf8_pa < layout->legacy_io.base ||
        layout->cfc_pa < layout->legacy_io.base ||
        layout->cf8_pa >= fixture_range_end(&layout->legacy_io) ||
        layout->cfc_pa > fixture_range_end(&layout->legacy_io) - 4) {
        error_setg(errp,
                   "i2000 460GX test CF8/CFC sparse addresses are invalid");
        return false;
    }

    if (!fixture_range_matches(&layout->ram, IA64_I2000_460GX_TEST_RAM_BASE,
                               IA64_I2000_460GX_TEST_RAM_SIZE, "RAM", errp) ||
        !fixture_range_matches(
            &layout->firmware, IA64_I2000_460GX_TEST_FIRMWARE_BASE,
            IA64_I2000_460GX_TEST_FIRMWARE_SIZE, "firmware", errp) ||
        !fixture_range_matches(
            &layout->descriptor_rom, IA64_I2000_460GX_TEST_DESC_ROM_BASE,
            IA64_I2000_460GX_TEST_DESC_ROM_SIZE, "descriptor ROM", errp) ||
        !fixture_range_matches(&layout->descriptor_envelope,
                               IA64_I2000_460GX_TEST_DESC_ROM_BASE,
                               IA64_I2000_460GX_TEST_DESC_ENVELOPE_SIZE,
                               "descriptor envelope", errp) ||
        !fixture_range_matches(
            &layout->pid_decode, IA64_I2000_460GX_TEST_PID_BASE,
            IA64_I2000_460GX_TEST_PID_SIZE, "PID decode", errp) ||
        !fixture_range_matches(
            &layout->pid_envelope, IA64_I2000_460GX_TEST_PID_BASE,
            IA64_I2000_460GX_TEST_PID_ENVELOPE_SIZE, "PID envelope", errp) ||
        !fixture_range_matches(&layout->pib, IA64_I2000_460GX_TEST_PIB_BASE,
                               IA64_I2000_460GX_TEST_PIB_SIZE, "PIB", errp) ||
        !fixture_range_matches(
            &layout->legacy_io, IA64_I2000_460GX_TEST_LEGACY_IO_BASE,
            IA64_I2000_460GX_TEST_LEGACY_IO_SIZE, "legacy I/O", errp)) {
        return false;
    }

    if (layout->cf8_pa != IA64_I2000_460GX_TEST_CF8_PA ||
        layout->cfc_pa != IA64_I2000_460GX_TEST_CFC_PA ||
        layout->cbn != IA64_I2000_460GX_TEST_CBN ||
        layout->chipset_present != IA64_I2000_460GX_TEST_CHIPSET_PRESENT ||
        layout->pid_id != IA64_I2000_460GX_TEST_PID_ID ||
        layout->pid_pin_count != IA64_I2000_460GX_TEST_PID_PIN_COUNT ||
        layout->legacy_pin_count != IA64_I2000_460GX_TEST_LEGACY_PIN_COUNT ||
        layout->cf8_io_root != IA64_I2000_460GX_TEST_CF8_IO_ROOT) {
        error_setg(errp, "i2000 460GX test host, PID, or CF8 I/O "
                         "settings do not match the fixed layout");
        return false;
    }

    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        if (!fixture_root_matches(&layout->roots[i], &fixed_roots[i], i,
                                  errp)) {
            return false;
        }
    }

    return true;
}
