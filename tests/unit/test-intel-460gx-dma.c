/*
 * Intel 460GX fixed inbound DMA aperture tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "system/memory.h"

#define TYPE_TEST_460GX_PCI_BUS_OBJECT "test-460gx-pci-bus-object"

static const TypeInfo test_pci_bus_object_info = {
    .name = TYPE_TEST_460GX_PCI_BUS_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(PCIBus),
};

static void test_pci_bus_object_register_types(void)
{
    type_register_static(&test_pci_bus_object_info);
}

type_init(test_pci_bus_object_register_types)

static const MemoryRegion *test_protected_ram;
static bool test_bypass_iommu;

/* Local definitions link the policy validator into this unit test. */
void memory_region_init(MemoryRegion *mr, Object *owner, const char *name,
                        uint64_t size)
{
    memset(mr, 0, sizeof(*mr));
    mr->owner = owner;
    mr->name = (char *)name;
    mr->size = int128_make64(size);
}

void memory_region_init_alias(MemoryRegion *mr, Object *owner,
                              const char *name, MemoryRegion *orig,
                              hwaddr offset, uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

void memory_region_init_io(MemoryRegion *mr, Object *owner,
                           const MemoryRegionOps *ops, void *opaque,
                           const char *name, uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
}

void memory_region_add_subregion(MemoryRegion *mr, hwaddr offset,
                                 MemoryRegion *subregion)
{
    g_assert_null(subregion->container);
    subregion->container = mr;
    subregion->addr = offset;
}

void memory_region_add_subregion_overlap(MemoryRegion *mr, hwaddr offset,
                                         MemoryRegion *subregion,
                                         int priority)
{
    memory_region_add_subregion(mr, offset, subregion);
    subregion->priority = priority;
}

void memory_region_del_subregion(MemoryRegion *mr, MemoryRegion *subregion)
{
    g_assert_true(subregion->container == mr);
    subregion->container = NULL;
}

uint64_t memory_region_size(const MemoryRegion *mr)
{
    return int128_get64(mr->size);
}

bool memory_region_is_ram_device(const MemoryRegion *mr)
{
    return mr->ram_device;
}

bool memory_region_is_protected(const MemoryRegion *mr)
{
    return mr == test_protected_ram;
}

void address_space_init(AddressSpace *as, MemoryRegion *root,
                        const char *name)
{
    memset(as, 0, sizeof(*as));
    as->root = root;
    as->name = (char *)name;
}

void address_space_remove_listeners(AddressSpace *as)
{
    (void)as;
}

void address_space_destroy(AddressSpace *as)
{
    as->root = NULL;
}

void pci_setup_iommu(PCIBus *bus, const PCIIOMMUOps *ops, void *opaque)
{
    bus->iommu_ops = ops;
    bus->iommu_opaque = opaque;
}

bool pci_bus_bypass_iommu(PCIBus *bus)
{
    (void)bus;
    return test_bypass_iommu;
}

int pci_bus_num(PCIBus *bus)
{
    (void)bus;
    return 0;
}

static void ram_init(MemoryRegion *ram, uint64_t size)
{
    memset(ram, 0, sizeof(*ram));
    ram->ram = true;
    ram->size = int128_make64(size);
}

static void assert_new_fails(uint64_t base, uint64_t size)
{
    Error *err = NULL;

    g_assert_null(intel_460gx_dma_new(base, size, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void assert_map_fails(Intel460GXDMA *dma, uint64_t dma_base,
                             uint64_t size, MemoryRegion *target,
                             uint64_t target_offset)
{
    Error *err = NULL;

    g_assert_false(intel_460gx_dma_add_ram_alias(
        dma, dma_base, size, target, target_offset, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void assert_pci_map_fails(Intel460GXDMA *dma, uint64_t dma_base,
                                 uint64_t size, MemoryRegion *target,
                                 uint64_t target_offset)
{
    Error *err = NULL;

    g_assert_false(intel_460gx_dma_add_pci_window_alias(
        dma, dma_base, size, target, target_offset, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void assert_attach_fails(Intel460GXDMA *dma, PCIBus *bus)
{
    Error *err = NULL;

    g_assert_false(intel_460gx_dma_attach_root(dma, bus, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void assert_destroy_succeeds(Intel460GXDMA *dma)
{
    Error *err = NULL;

    g_assert_true(intel_460gx_dma_destroy(dma, &err));
    g_assert_null(err);
}

static void assert_destroy_fails(Intel460GXDMA *dma)
{
    Error *err = NULL;

    g_assert_false(intel_460gx_dma_destroy(dma, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_aperture_bounds(void)
{
    MemoryRegion ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    assert_new_fails(1, 0);
    assert_new_fails(INTEL_460GX_DMA_ADDRESS_LIMIT, 1);
    assert_new_fails(INTEL_460GX_DMA_ADDRESS_LIMIT - 1, 2);
    assert_new_fails(UINT64_MAX - 3, 8);

    ram_init(&ram, 1);
    dma = intel_460gx_dma_new(INTEL_460GX_DMA_ADDRESS_LIMIT - 1, 1, &err);
    g_assert_nonnull(dma);
    g_assert_null(err);
    g_assert_cmphex(intel_460gx_dma_aperture_base(dma), ==,
                    INTEL_460GX_DMA_ADDRESS_LIMIT - 1);
    g_assert_cmphex(intel_460gx_dma_aperture_size(dma), ==, 1);
    g_assert_true(intel_460gx_dma_add_ram_alias(
        dma, INTEL_460GX_DMA_ADDRESS_LIMIT - 1, 1, &ram, 0, &err));
    assert_destroy_succeeds(dma);
}

static void test_deny_all(void)
{
    MemoryRegion ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    ram_init(&ram, 0x1000);
    dma = intel_460gx_dma_new(0, 0, &err);
    g_assert_nonnull(dma);
    g_assert_null(err);
    assert_map_fails(dma, 0, 1, &ram, 0);
    g_assert_true(intel_460gx_dma_seal(dma, &err));
    g_assert_true(intel_460gx_dma_is_sealed(dma));
    g_assert_cmpuint(intel_460gx_dma_alias_count(dma), ==, 0);
    assert_destroy_succeeds(dma);
}

static void test_alias_outside_and_overflow(void)
{
    MemoryRegion ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    ram_init(&ram, 0x10000);
    dma = intel_460gx_dma_new(0x1000, 0x2000, &err);
    g_assert_nonnull(dma);
    assert_map_fails(dma, 0xfff, 1, &ram, 0);
    assert_map_fails(dma, 0x2fff, 2, &ram, 0);
    assert_map_fails(dma, UINT64_MAX - 1, 4, &ram, 0);
    assert_map_fails(dma, INTEL_460GX_DMA_ADDRESS_LIMIT - 1, 2, &ram, 0);
    g_assert_cmpuint(intel_460gx_dma_alias_count(dma), ==, 0);
    assert_destroy_succeeds(dma);
}

static void test_alias_overlap(void)
{
    MemoryRegion ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    ram_init(&ram, 0x10000);
    dma = intel_460gx_dma_new(0x1000, 0x2000, &err);
    g_assert_nonnull(dma);
    g_assert_true(intel_460gx_dma_add_ram_alias(
        dma, 0x1000, 0x100, &ram, 0, &err));
    g_assert_true(intel_460gx_dma_add_ram_alias(
        dma, 0x1100, 0x100, &ram, 0x100, &err));
    assert_map_fails(dma, 0x1080, 0x20, &ram, 0x200);
    assert_map_fails(dma, 0x10ff, 2, &ram, 0x200);
    assert_map_fails(dma, 0x1000, 0x200, &ram, 0x200);
    g_assert_cmpuint(intel_460gx_dma_alias_count(dma), ==, 2);
    assert_destroy_succeeds(dma);
}

static void test_target_validation(void)
{
    MemoryRegion ram;
    MemoryRegion nonram = { 0 };
    MemoryRegion pci_memory;
    MemoryRegion ram_device;
    MemoryRegion rom;
    MemoryRegion protected_ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    ram_init(&ram, 0x1000);
    ram_init(&ram_device, 0x1000);
    ram_device.ram_device = true;
    ram_init(&rom, 0x1000);
    rom.readonly = true;
    ram_init(&protected_ram, 0x1000);
    memory_region_init(&pci_memory, NULL, "pci-memory", 0x1000);
    test_protected_ram = &protected_ram;
    dma = intel_460gx_dma_new(0, 0x4000, &err);
    g_assert_nonnull(dma);

    assert_map_fails(dma, 0, 0, &ram, 0);
    assert_map_fails(dma, 0, 1, NULL, 0);
    assert_map_fails(dma, 0, 1, &nonram, 0);
    assert_map_fails(dma, 0, 1, &ram_device, 0);
    assert_map_fails(dma, 0, 1, &rom, 0);
    assert_map_fails(dma, 0, 1, &protected_ram, 0);
    assert_map_fails(dma, 0, 0x101, &ram, 0xf00);
    assert_map_fails(dma, 0, 2, &ram, UINT64_MAX);
    assert_map_fails(dma, 0, 0x100, &ram, 0xf01);

    g_assert_true(intel_460gx_dma_add_ram_alias(
        dma, 0, 0x100, &ram, 0xf00, &err));
    g_assert_null(err);
    assert_pci_map_fails(dma, 0x1000, 0x100, NULL, 0);
    assert_pci_map_fails(dma, 0x1000, 0x100, &ram, 0);
    assert_pci_map_fails(dma, 0x1000, 0x100, &ram_device, 0);
    assert_pci_map_fails(dma, 0x1000, 0x100, &rom, 0);
    assert_pci_map_fails(dma, 0x1000, 0x100, &protected_ram, 0);
    g_assert_true(intel_460gx_dma_add_pci_window_alias(
        dma, 0x1000, 0x100, &pci_memory, 0xf00, &err));
    g_assert_null(err);
    assert_destroy_succeeds(dma);
    test_protected_ram = NULL;
}

static void test_seal(void)
{
    MemoryRegion ram;
    Intel460GXDMA *dma;
    Error *err = NULL;

    ram_init(&ram, 0x2000);
    dma = intel_460gx_dma_new(0, 0x2000, &err);
    g_assert_nonnull(dma);
    g_assert_true(intel_460gx_dma_add_ram_alias(
        dma, 0, 0x1000, &ram, 0, &err));
    g_assert_true(intel_460gx_dma_seal(dma, &err));
    g_assert_true(intel_460gx_dma_seal(dma, &err));
    assert_map_fails(dma, 0x1000, 0x1000, &ram, 0x1000);
    g_assert_cmpuint(intel_460gx_dma_alias_count(dma), ==, 1);
    assert_destroy_succeeds(dma);
}

static void test_two_roots_are_independent(void)
{
    MemoryRegion ram_a;
    MemoryRegion ram_b;
    Intel460GXDMA *root_a;
    Intel460GXDMA *root_b;
    Error *err = NULL;

    ram_init(&ram_a, 0x4000);
    ram_init(&ram_b, 0x4000);
    root_a = intel_460gx_dma_new(0, 0x4000, &err);
    root_b = intel_460gx_dma_new(0, 0x4000, &err);
    g_assert_nonnull(root_a);
    g_assert_nonnull(root_b);
    g_assert_true(intel_460gx_dma_add_ram_alias(
        root_a, 0x1000, 0x1000, &ram_a, 0, &err));
    g_assert_true(intel_460gx_dma_add_ram_alias(
        root_b, 0x1000, 0x1000, &ram_b, 0x1000, &err));
    g_assert_cmpuint(intel_460gx_dma_alias_count(root_a), ==, 1);
    g_assert_cmpuint(intel_460gx_dma_alias_count(root_b), ==, 1);
    g_assert_true(intel_460gx_dma_address_space(root_a) !=
                  intel_460gx_dma_address_space(root_b));
    g_assert_true(intel_460gx_dma_seal(root_a, &err));
    g_assert_true(intel_460gx_dma_seal(root_b, &err));
    assert_destroy_succeeds(root_a);
    assert_destroy_succeeds(root_b);
}

static void test_root_attach_contract(void)
{
    static const PCIIOMMUOps occupied_ops = { 0 };
    const PCIIOMMUOps *attached_ops;
    void *attached_opaque;
    PCIBus bus;
    Intel460GXDMA *dma;
    Error *err = NULL;

    memset(&bus, 0, sizeof(bus));
    object_initialize(&bus, sizeof(bus), TYPE_TEST_460GX_PCI_BUS_OBJECT);
    bus.flags = PCI_BUS_IS_ROOT;

    dma = intel_460gx_dma_new(0, 0, &err);
    g_assert_nonnull(dma);
    assert_attach_fails(dma, &bus);
    g_assert_true(intel_460gx_dma_seal(dma, &err));

    bus.flags = 0;
    assert_attach_fails(dma, &bus);
    bus.flags = PCI_BUS_IS_ROOT;
    test_bypass_iommu = true;
    assert_attach_fails(dma, &bus);
    test_bypass_iommu = false;
    bus.iommu_ops = &occupied_ops;
    assert_attach_fails(dma, &bus);
    bus.iommu_ops = NULL;
    bus.iommu_opaque = &bus;
    assert_attach_fails(dma, &bus);
    bus.iommu_opaque = NULL;
    bus.devices[0] = (PCIDevice *)(uintptr_t)1;
    assert_attach_fails(dma, &bus);
    bus.devices[0] = NULL;

    g_assert_true(intel_460gx_dma_attach_root(dma, &bus, &err));
    g_assert_null(err);
    g_assert_nonnull(bus.iommu_ops);
    g_assert_nonnull(bus.iommu_ops->get_address_space);
    assert_attach_fails(dma, &bus);

    attached_ops = bus.iommu_ops;
    attached_opaque = bus.iommu_opaque;
    bus.devices[0] = (PCIDevice *)(uintptr_t)1;
    assert_destroy_fails(dma);
    g_assert_true(bus.iommu_ops == attached_ops);
    g_assert_true(bus.iommu_opaque == attached_opaque);
    bus.devices[0] = NULL;

    bus.iommu_ops = &occupied_ops;
    assert_destroy_fails(dma);
    g_assert_true(bus.iommu_ops == &occupied_ops);
    g_assert_true(bus.iommu_opaque == attached_opaque);
    bus.iommu_ops = attached_ops;

    bus.iommu_opaque = &bus;
    assert_destroy_fails(dma);
    g_assert_true(bus.iommu_ops == attached_ops);
    g_assert_true(bus.iommu_opaque == &bus);
    bus.iommu_opaque = attached_opaque;

    assert_destroy_succeeds(dma);
    g_assert_null(bus.iommu_ops);
    g_assert_null(bus.iommu_opaque);

    dma = intel_460gx_dma_new(0, 0, &err);
    g_assert_nonnull(dma);
    g_assert_true(intel_460gx_dma_seal(dma, &err));
    g_assert_true(intel_460gx_dma_attach_root(dma, &bus, &err));
    bus.iommu_ops = &occupied_ops;
    bus.iommu_opaque = &bus;
    assert_destroy_succeeds(dma);
    g_assert_true(bus.iommu_ops == &occupied_ops);
    g_assert_true(bus.iommu_opaque == &bus);
    bus.iommu_ops = NULL;
    bus.iommu_opaque = NULL;
    object_unref(OBJECT(&bus));
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/intel-460gx-dma/aperture-bounds",
                    test_aperture_bounds);
    g_test_add_func("/intel-460gx-dma/deny-all", test_deny_all);
    g_test_add_func("/intel-460gx-dma/alias-outside-overflow",
                    test_alias_outside_and_overflow);
    g_test_add_func("/intel-460gx-dma/alias-overlap", test_alias_overlap);
    g_test_add_func("/intel-460gx-dma/target-validation",
                    test_target_validation);
    g_test_add_func("/intel-460gx-dma/seal", test_seal);
    g_test_add_func("/intel-460gx-dma/two-roots",
                    test_two_roots_are_independent);
    g_test_add_func("/intel-460gx-dma/root-attach",
                    test_root_attach_contract);
    return g_test_run();
}
