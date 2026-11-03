/*
 * Intel 460GX DMA PCI integration test host
 *
 * Its control registers expose DMA transactions and translation results to
 * qtest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_dma.h"
#include "hw/ia64/intel_460gx_dma_test.h"
#include "hw/ia64/intel_460gx_root.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/memory.h"

struct Intel460GXDMATestHostState {
    PCIHostState parent_obj;

    MemoryRegion pci_mem;
    MemoryRegion pci_mem_window;
    MemoryRegion pci_target;
    MemoryRegion pci_io;
    MemoryRegion ecam;
    MemoryRegion control;
    MemoryRegion test_ram;
    uint8_t pci_target_bytes[INTEL_460GX_DMA_TEST_PCI_TARGET_SIZE];

    Intel460GXDMA *dma;
    PCIDevice *testdev;
    uint64_t ecam_base;
    uint64_t mmio_base;
    uint64_t control_base;
    uint64_t ram_base;
    uint64_t iova_base;
    uint16_t first_bus;
    uint32_t pending_command;
    uint32_t status;
    bool deny_all;
    bool pci_target_mapped;
    bool system_regions_mapped;
};

static uint64_t intel_460gx_dma_test_pci_target_read(void *opaque, hwaddr addr,
                                                     unsigned size)
{
    uint8_t *bytes = opaque;
    uint64_t value = 0;

    memcpy(&value, bytes + addr, size);
    return value;
}

static void intel_460gx_dma_test_pci_target_write(void *opaque, hwaddr addr,
                                                  uint64_t value, unsigned size)
{
    uint8_t *bytes = opaque;

    memcpy(bytes + addr, &value, size);
}

static const MemoryRegionOps intel_460gx_dma_test_pci_target_ops = {
    .read = intel_460gx_dma_test_pci_target_read,
    .write = intel_460gx_dma_test_pci_target_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static bool test_range_valid(uint64_t base, uint64_t size, uint64_t align)
{
    return base != UINT64_MAX && !(base & (align - 1)) &&
           size <= UINT64_MAX - base;
}

static bool test_ranges_overlap(uint64_t first_base, uint64_t first_size,
                                uint64_t second_base, uint64_t second_size)
{
    return first_base < second_base + second_size &&
           second_base < first_base + first_size;
}

static bool test_system_range_is_free(uint64_t base, uint64_t size,
                                      const char *name, Error **errp)
{
    MemoryRegionSection section =
        memory_region_find(get_system_memory(), base, size);

    if (!section.mr) {
        return true;
    }

    error_setg(errp,
               "460GX DMA test host %s window overlaps system region '%s'",
               name, memory_region_name(section.mr));
    memory_region_unref(section.mr);
    return false;
}

static bool intel_460gx_dma_test_validate_bases(Intel460GXDMATestHostState *s,
                                                Error **errp)
{
    const uint64_t bases[] = {
        s->ecam_base,
        s->mmio_base,
        s->control_base,
        s->ram_base,
    };
    const uint64_t sizes[] = {
        INTEL_460GX_DMA_TEST_ECAM_SIZE,
        INTEL_460GX_DMA_TEST_MMIO_SIZE,
        INTEL_460GX_DMA_TEST_CONTROL_SIZE,
        INTEL_460GX_DMA_TEST_RAM_SIZE,
    };
    const char *const names[] = {
        "ECAM",
        "PCI MMIO",
        "control",
        "RAM",
    };
    const uint64_t aligns[] = {
        INTEL_460GX_DMA_TEST_ECAM_SIZE,
        INTEL_460GX_DMA_TEST_MMIO_SIZE,
        INTEL_460GX_DMA_TEST_CONTROL_SIZE,
        INTEL_460GX_DMA_TEST_RAM_SIZE,
    };
    size_t i;
    size_t j;

    if (s->first_bus > UINT8_MAX) {
        error_setg(errp,
                   "460GX DMA test host x-test-first-bus is outside 0..255");
        return false;
    }

    if (s->mmio_base >= (UINT64_C(1) << 32) ||
        INTEL_460GX_DMA_TEST_MMIO_SIZE > (UINT64_C(1) << 32) - s->mmio_base) {
        error_setg(errp, "460GX DMA test host PCI MMIO window exceeds its "
                         "32-bit BAR address space");
        return false;
    }

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        if (!test_range_valid(bases[i], sizes[i], aligns[i])) {
            error_setg(
                errp,
                "460GX DMA test host has an invalid or unaligned test window");
            return false;
        }
        for (j = 0; j < i; j++) {
            if (test_ranges_overlap(bases[i], sizes[i], bases[j], sizes[j])) {
                error_setg(errp, "460GX DMA host apertures overlap");
                return false;
            }
        }
        if (!test_system_range_is_free(bases[i], sizes[i], names[i], errp)) {
            return false;
        }
    }

    return true;
}

static PCIDevice *
intel_460gx_dma_test_ecam_device(Intel460GXDMATestHostState *s, hwaddr addr,
                                 uint32_t *reg)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    uint16_t bus = s->first_bus + extract64(addr, 20, 8);
    uint8_t devfn = extract64(addr, 12, 8);

    *reg = addr & 0xfff;
    if (bus > UINT8_MAX) {
        return NULL;
    }
    return pci_find_device(phb->bus, bus, devfn);
}

static uint64_t intel_460gx_dma_test_ecam_read(void *opaque, hwaddr addr,
                                               unsigned size)
{
    Intel460GXDMATestHostState *s = opaque;
    PCIDevice *pdev;
    uint32_t reg;

    pdev = intel_460gx_dma_test_ecam_device(s, addr, &reg);
    if (!pdev) {
        return UINT64_MAX;
    }

    return pci_host_config_read_common(pdev, reg, pci_config_size(pdev), size);
}

static void intel_460gx_dma_test_ecam_write(void *opaque, hwaddr addr,
                                            uint64_t value, unsigned size)
{
    Intel460GXDMATestHostState *s = opaque;
    PCIDevice *pdev;
    uint32_t reg;

    pdev = intel_460gx_dma_test_ecam_device(s, addr, &reg);
    if (pdev) {
        pci_host_config_write_common(pdev, reg, pci_config_size(pdev), value,
                                     size);
    }
}

static const MemoryRegionOps intel_460gx_dma_test_ecam_ops = {
    .read = intel_460gx_dma_test_ecam_read,
    .write = intel_460gx_dma_test_ecam_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void
intel_460gx_dma_test_remove_pci_device(Intel460GXDMATestHostState *s)
{
    if (!s->testdev) {
        return;
    }

    if (qdev_is_realized(DEVICE(s->testdev))) {
        qdev_unrealize(DEVICE(s->testdev));
    }
    object_unparent(OBJECT(s->testdev));
    object_unref(OBJECT(s->testdev));
    s->testdev = NULL;
}

static bool intel_460gx_dma_test_destroy_dma(Intel460GXDMATestHostState *s,
                                             Error **errp)
{
    if (!s->dma) {
        return true;
    }
    if (!intel_460gx_dma_destroy(s->dma, errp)) {
        return false;
    }

    s->dma = NULL;
    return true;
}

static void intel_460gx_dma_test_cleanup_bh(void *opaque)
{
    Intel460GXDMATestHostState *s = opaque;
    Error *local_err = NULL;

    switch (s->pending_command) {
    case INTEL_460GX_DMA_TEST_CMD_TRY_ACTIVE_DESTROY:
        if (intel_460gx_dma_test_destroy_dma(s, &local_err)) {
            s->status = INTEL_460GX_DMA_TEST_STATUS_ACTIVE_DESTROY_UNEXPECTED;
        } else {
            error_free(local_err);
            s->status = INTEL_460GX_DMA_TEST_STATUS_ACTIVE_DESTROY_REFUSED;
        }
        break;
    case INTEL_460GX_DMA_TEST_CMD_CLEANUP:
        intel_460gx_dma_test_remove_pci_device(s);

        /*
         * Finish the PCI bus-master AddressSpace and its alias before
         * destroying the DMA AddressSpace that owns the alias target.
         */
        drain_call_rcu();
        if (!intel_460gx_dma_test_destroy_dma(s, &local_err)) {
            error_free(local_err);
            s->status = INTEL_460GX_DMA_TEST_STATUS_CLEANUP_FAILED;
            break;
        }
        drain_call_rcu();
        s->status = INTEL_460GX_DMA_TEST_STATUS_CLEANUP_DONE;
        break;
    default:
        s->status = INTEL_460GX_DMA_TEST_STATUS_BAD_COMMAND;
        break;
    }
}

static uint64_t intel_460gx_dma_test_control_read(void *opaque, hwaddr addr,
                                                  unsigned size)
{
    Intel460GXDMATestHostState *s = opaque;

    (void)size;
    if (addr == INTEL_460GX_DMA_TEST_REG_STATUS) {
        return s->status;
    }
    /* COMMAND is write-only; all other offsets read as zero. */
    return 0;
}

static void intel_460gx_dma_test_control_write(void *opaque, hwaddr addr,
                                               uint64_t value, unsigned size)
{
    Intel460GXDMATestHostState *s = opaque;

    (void)size;
    if (addr != INTEL_460GX_DMA_TEST_REG_COMMAND ||
        s->status == INTEL_460GX_DMA_TEST_STATUS_BUSY) {
        return;
    }
    if (value != INTEL_460GX_DMA_TEST_CMD_TRY_ACTIVE_DESTROY &&
        value != INTEL_460GX_DMA_TEST_CMD_CLEANUP) {
        s->status = INTEL_460GX_DMA_TEST_STATUS_BAD_COMMAND;
        return;
    }

    s->pending_command = value;
    s->status = INTEL_460GX_DMA_TEST_STATUS_BUSY;
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            intel_460gx_dma_test_cleanup_bh, s);
}

static const MemoryRegionOps intel_460gx_dma_test_control_ops = {
    .read = intel_460gx_dma_test_control_read,
    .write = intel_460gx_dma_test_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void intel_460gx_dma_test_set_irq(void *opaque, int irq_num, int level)
{
    /* This test host does not model interrupt delivery. */
    (void)opaque;
    (void)irq_num;
    (void)level;
}

static int intel_460gx_dma_test_map_irq(PCIDevice *pdev, int irq_num)
{
    /* All INTx lines use the discarded input above. */
    (void)pdev;
    (void)irq_num;
    return 0;
}

static void intel_460gx_dma_test_unmap_system(Intel460GXDMATestHostState *s)
{
    if (!s->system_regions_mapped) {
        return;
    }

    memory_region_del_subregion(get_system_memory(), &s->test_ram);
    memory_region_del_subregion(get_system_memory(), &s->control);
    memory_region_del_subregion(get_system_memory(), &s->pci_mem_window);
    memory_region_del_subregion(get_system_memory(), &s->ecam);
    s->system_regions_mapped = false;
}

static void intel_460gx_dma_test_realize(DeviceState *dev, Error **errp)
{
    Intel460GXDMATestHostState *s = INTEL_460GX_DMA_TEST_HOST(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    Error *local_err = NULL;

    if (!intel_460gx_dma_test_validate_bases(s, errp)) {
        return;
    }

    memory_region_init(&s->pci_mem, OBJECT(dev),
                       TYPE_INTEL_460GX_DMA_TEST_HOST ".pci-mem", UINT64_MAX);
    memory_region_init_alias(&s->pci_mem_window, OBJECT(dev),
                             TYPE_INTEL_460GX_DMA_TEST_HOST ".pci-window",
                             &s->pci_mem, s->mmio_base,
                             INTEL_460GX_DMA_TEST_MMIO_SIZE);
    memory_region_init(&s->pci_io, OBJECT(dev),
                       TYPE_INTEL_460GX_DMA_TEST_HOST ".pci-io", 0x10000);
    memory_region_init_io(
        &s->pci_target, OBJECT(dev), &intel_460gx_dma_test_pci_target_ops,
        s->pci_target_bytes, TYPE_INTEL_460GX_DMA_TEST_HOST ".pci-target",
        INTEL_460GX_DMA_TEST_PCI_TARGET_SIZE);
    memory_region_add_subregion(
        &s->pci_mem, INTEL_460GX_DMA_TEST_PCI_TARGET_BASE, &s->pci_target);
    s->pci_target_mapped = true;
    memory_region_init_io(&s->ecam, OBJECT(dev), &intel_460gx_dma_test_ecam_ops,
                          s, TYPE_INTEL_460GX_DMA_TEST_HOST ".ecam",
                          INTEL_460GX_DMA_TEST_ECAM_SIZE);
    memory_region_init_io(&s->control, OBJECT(dev),
                          &intel_460gx_dma_test_control_ops, s,
                          TYPE_INTEL_460GX_DMA_TEST_HOST ".control",
                          INTEL_460GX_DMA_TEST_CONTROL_SIZE);
    memory_region_init_ram(&s->test_ram, OBJECT(dev),
                           TYPE_INTEL_460GX_DMA_TEST_HOST ".ram",
                           INTEL_460GX_DMA_TEST_RAM_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!intel_460gx_numbered_root_bus_register(
            phb, "dma-test-pci", intel_460gx_dma_test_set_irq,
            intel_460gx_dma_test_map_irq, s, &s->pci_mem, &s->pci_io,
            PCI_DEVFN(0, 0), 1, s->first_bus, &local_err)) {
        goto fail;
    }

    if (s->deny_all) {
        s->dma = intel_460gx_dma_new(0, 0, &local_err);
    } else {
        s->dma =
            intel_460gx_dma_new(0, INTEL_460GX_DMA_ADDRESS_LIMIT, &local_err);
        if (s->dma && !intel_460gx_dma_add_ram_alias(
                          s->dma, s->iova_base, INTEL_460GX_DMA_TEST_ALIAS_SIZE,
                          &s->test_ram, 0, &local_err)) {
            goto fail;
        }
        if (s->dma && !intel_460gx_dma_add_pci_window_alias(
                          s->dma, s->mmio_base, INTEL_460GX_DMA_TEST_MMIO_SIZE,
                          &s->pci_mem, s->mmio_base, &local_err)) {
            goto fail;
        }
    }
    if (!s->dma) {
        goto fail;
    }
    if (!intel_460gx_dma_seal(s->dma, &local_err) ||
        !intel_460gx_dma_attach_root(s->dma, phb->bus, &local_err)) {
        goto fail;
    }

    s->testdev = pci_new(PCI_DEVFN(INTEL_460GX_DMA_TEST_PCI_SLOT, 0),
                         TYPE_IOMMU_TESTDEV);
    if (!qdev_realize(DEVICE(s->testdev), BUS(phb->bus), &local_err)) {
        object_unref(OBJECT(s->testdev));
        s->testdev = NULL;
        drain_call_rcu();
        goto fail;
    }

    memory_region_add_subregion(get_system_memory(), s->ecam_base, &s->ecam);
    memory_region_add_subregion(get_system_memory(), s->mmio_base,
                                &s->pci_mem_window);
    memory_region_add_subregion(get_system_memory(), s->control_base,
                                &s->control);
    memory_region_add_subregion(get_system_memory(), s->ram_base, &s->test_ram);
    s->system_regions_mapped = true;
    s->status = INTEL_460GX_DMA_TEST_STATUS_IDLE;
    return;

fail:
    if (s->dma) {
        Error *destroy_err = NULL;

        if (!intel_460gx_dma_test_destroy_dma(s, &destroy_err)) {
            error_free(destroy_err);
        }
        drain_call_rcu();
    }
    if (!s->dma && phb->bus) {
        pci_unregister_root_bus(phb->bus);
        phb->bus = NULL;
    }
    if (s->pci_target_mapped) {
        memory_region_del_subregion(&s->pci_mem, &s->pci_target);
        s->pci_target_mapped = false;
    }
    error_propagate(errp, local_err);
}

static void intel_460gx_dma_test_unrealize(DeviceState *dev)
{
    Intel460GXDMATestHostState *s = INTEL_460GX_DMA_TEST_HOST(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    Error *local_err = NULL;

    intel_460gx_dma_test_remove_pci_device(s);
    drain_call_rcu();
    if (!intel_460gx_dma_test_destroy_dma(s, &local_err)) {
        error_report_err(local_err);
    } else {
        drain_call_rcu();
    }
    intel_460gx_dma_test_unmap_system(s);
    if (!s->dma && phb->bus) {
        pci_unregister_root_bus(phb->bus);
        phb->bus = NULL;
    }
    if (s->pci_target_mapped) {
        memory_region_del_subregion(&s->pci_mem, &s->pci_target);
        s->pci_target_mapped = false;
    }
}

static const Property intel_460gx_dma_test_properties[] = {
    DEFINE_PROP_UINT64("x-test-ecam-base", Intel460GXDMATestHostState,
                       ecam_base, UINT64_MAX),
    DEFINE_PROP_UINT64("x-test-mmio-base", Intel460GXDMATestHostState,
                       mmio_base, UINT64_MAX),
    DEFINE_PROP_UINT64("x-test-control-base", Intel460GXDMATestHostState,
                       control_base, UINT64_MAX),
    DEFINE_PROP_UINT64("x-test-ram-base", Intel460GXDMATestHostState, ram_base,
                       UINT64_MAX),
    DEFINE_PROP_UINT64("x-test-iova-base", Intel460GXDMATestHostState,
                       iova_base, INTEL_460GX_DMA_TEST_IOVA_BASE),
    DEFINE_PROP_UINT16("x-test-first-bus", Intel460GXDMATestHostState,
                       first_bus, 0),
    DEFINE_PROP_BOOL("x-test-deny-all", Intel460GXDMATestHostState, deny_all,
                     false),
};

static void intel_460gx_dma_test_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Intel 460GX DMA PCI integration test host";
    dc->realize = intel_460gx_dma_test_realize;
    dc->unrealize = intel_460gx_dma_test_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
    device_class_set_props(dc, intel_460gx_dma_test_properties);
}

static const TypeInfo intel_460gx_dma_test_type_info = {
    .name = TYPE_INTEL_460GX_DMA_TEST_HOST,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(Intel460GXDMATestHostState),
    .class_init = intel_460gx_dma_test_class_init,
};

static void intel_460gx_dma_test_register_types(void)
{
    type_register_static(&intel_460gx_dma_test_type_info);
}
type_init(intel_460gx_dma_test_register_types)
