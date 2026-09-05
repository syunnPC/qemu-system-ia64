/*
 * IA-64 zx6000 ZX1 integration test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_zx6000_zx1_test.h"
#include "hw/ia64/ia64_zx6000_zx1_test_layout.h"
#include "hw/ia64/ia64_pci.h"
#include "hw/ia64/ia64_zx2_pcie_test.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/msi.h"
#include "hw/pci-host/hp-zx1-ioa.h"
#include "hw/pci-host/hp-zx1-mio.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/qtest.h"
#include "system/reset.h"

#define ZX1_TEST_DELIVERY_RESULT_MASK \
    (MEMTX_ERROR | MEMTX_DECODE_ERROR | MEMTX_ACCESS_ERROR)
#define TYPE_ZX1_TEST_IOMMU_TESTDEV \
    TYPE_IA64_ZX6000_ZX1_TEST ".iommu-testdev"
#define ZX1_TEST_MSI_CAP_OFFSET UINT8_C(0x50)
#define ZX2_PCIE_TEST_MIO_BASE UINT64_C(0xb1000000)
#define ZX2_PCIE_TEST_ROPES    (UINT16_C(1) << 15)

typedef struct ZX1TestRouteBaseline {
    uint32_t packed;
} ZX1TestRouteBaseline;

typedef struct ZX1TestRootBaseline {
    uint8_t mode;
    ZX1TestRouteBaseline routes[IA64_ZX6000_ZX1_TEST_SLOT_COUNT];
} ZX1TestRootBaseline;

typedef struct ZX1TestDeliveryContext {
    IA64ZX6000ZX1TestState *fixture;
    unsigned int root;
} ZX1TestDeliveryContext;

struct IA64ZX6000ZX1TestState {
    DeviceState parent_obj;

    IA64ZX6000ZX1TestLayout layout;
    ZX1TestRootBaseline layout_baseline[
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    MemoryRegion *ram;

    HPZX1MIOState *mio;
    HPZX1IOAState *ioas[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    MemoryRegion root_mmio[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    ZX1TestDeliveryContext delivery_context[
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT];

    uint64_t delivery_count[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    uint64_t last_address[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    uint32_t last_data[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    uint32_t last_result[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];

    bool root_mmio_initialized[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    bool roots_attached[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    bool system_regions_mapped;
    bool reset_registered;
};

typedef struct IA64ZX6000ZX1QTestState {
    DeviceState parent_obj;

    IA64ZX6000ZX1TestState *fixture;
    PCIDevice *probes[IA64_ZX6000_ZX1_TEST_ROOT_COUNT]
                     [IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT];
} IA64ZX6000ZX1QTestState;

typedef struct IA64ZX2PCIeQTestState {
    DeviceState parent_obj;

    IA64PCIState *host;
    IA64PCIState *second_host;
    HPZX1MIOState *mio;
    DeviceState *cpu;
    PCIDevice *root_port;
    PCIDevice *probe;
    PCIDevice *second_probe;
    MemoryRegion ram;
    bool owns_host;
    bool ram_mapped;
    bool root_attached;
    bool second_root_attached;
    bool mio_mapped;
} IA64ZX2PCIeQTestState;

#define IA64_ZX2_PCIE_QTEST(obj) \
    OBJECT_CHECK(IA64ZX2PCIeQTestState, (obj), TYPE_IA64_ZX2_PCIE_QTEST)

static void (*zx1_test_probe_parent_realize)(PCIDevice *pdev, Error **errp);
static PCIUnregisterFunc *zx1_test_probe_parent_exit;

#define IA64_ZX6000_ZX1_QTEST(obj) \
    OBJECT_CHECK(IA64ZX6000ZX1QTestState, (obj), \
                 TYPE_IA64_ZX6000_ZX1_QTEST)

static bool zx1_test_system_range_is_free(uint64_t base, uint64_t size,
                                    const char *name, Error **errp)
{
    MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                      base, size);

    if (!section.mr) {
        return true;
    }

    error_setg(errp,
               "zx6000 zx1 test %s range overlaps system region '%s'",
               name, memory_region_name(section.mr));
    memory_region_unref(section.mr);
    return false;
}

static bool zx1_test_system_range_is_region(uint64_t base, uint64_t size,
                                      MemoryRegion *expected,
                                      uint64_t expected_offset)
{
    MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                      base, size);
    bool valid = section.mr == expected &&
                 section.offset_within_address_space == base &&
                 int128_get64(section.size) == size &&
                 section.offset_within_region == expected_offset &&
                 !section.readonly;

    if (section.mr) {
        memory_region_unref(section.mr);
    }
    return valid;
}

static bool zx1_test_system_range_is_ram(IA64ZX6000ZX1TestState *s,
                                   uint64_t base, uint64_t size,
                                   Error **errp)
{
    if (!zx1_test_system_range_is_region(base, size, s->ram,
                                   base - s->layout.ram.base)) {
        error_setg(errp,
                   "zx6000 zx1 test requires its linked RAM to be flat "
                   "at [0x%" PRIx64 ", 0x%" PRIx64 ")",
                   base, base + size);
        return false;
    }
    return true;
}

static bool zx1_test_system_pib_is_present(uint64_t base, uint64_t size,
                                     Error **errp)
{
    MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                      base, size);
    bool valid = section.mr &&
                 section.offset_within_address_space == base &&
                 int128_get64(section.size) == size &&
                 !memory_region_is_ram(section.mr) &&
                 !memory_region_is_ram_device(section.mr) &&
                 !memory_region_is_rom(section.mr) && !section.readonly;

    if (section.mr) {
        memory_region_unref(section.mr);
    }
    if (!valid) {
        error_setg(errp,
                   "zx6000 zx1 test requires the parent board's non-RAM "
                   "shared PIB range [0x%" PRIx64 ", 0x%" PRIx64 ")",
                   base, base + size);
    }
    return valid;
}

static bool zx1_test_validate_parent_resources(IA64ZX6000ZX1TestState *s,
                                         Error **errp)
{
    unsigned int root;

    if (!s->ram) {
        error_setg(errp, "zx6000 zx1 test requires the '%s' link",
                   IA64_ZX6000_ZX1_TEST_PROP_RAM);
        return false;
    }
    if (!memory_region_is_ram(s->ram) ||
        memory_region_is_ram_device(s->ram) ||
        memory_region_is_rom(s->ram) ||
        memory_region_is_protected(s->ram) ||
        memory_region_size(s->ram) != s->layout.ram.size) {
        error_setg(errp,
                   "zx6000 zx1 test requires exactly 512 MiB of "
                   "ordinary writable RAM");
        return false;
    }

    /* The MIO walks this same parent-owned RAM for the PDIR and targets. */
    if (!zx1_test_system_range_is_ram(s, s->layout.ram.base,
                                s->layout.ram.size, errp) ||
        !zx1_test_system_pib_is_present(s->layout.pib.base,
                                  s->layout.pib.size, errp) ||
        !zx1_test_system_range_is_free(s->layout.mio.base,
                                 s->layout.mio.size, "MIO CSR", errp)) {
        return false;
    }

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        const IA64ZX6000ZX1TestRoot *layout_root =
            &s->layout.roots[root];
        g_autofree char *csr_name =
            g_strdup_printf("Mercury %u CSR", root);
        g_autofree char *mmio_name =
            g_strdup_printf("Mercury %u CPU MMIO", root);

        if (!zx1_test_system_range_is_free(layout_root->ioa_csr.base,
                                     layout_root->ioa_csr.size,
                                     csr_name, errp) ||
            !zx1_test_system_range_is_free(layout_root->cpu_mmio.base,
                                     layout_root->cpu_mmio.size,
                                     mmio_name, errp)) {
            return false;
        }
    }
    return true;
}

static DeviceState *zx1_test_add_child(DeviceState *parent, const char *name,
                                 const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void zx1_test_remove_child(DeviceState **child)
{
    if (!*child) {
        return;
    }
    if (qdev_is_realized(*child)) {
        qdev_unrealize(*child);
    }
    object_unparent(OBJECT(*child));
    *child = NULL;
}

static bool zx1_test_message_in_pib(const IA64ZX6000ZX1TestState *s,
                              uint64_t address)
{
    const IA64ZX6000ZX1TestRange *pib = &s->layout.pib;

    return !(address & (sizeof(uint64_t) - 1)) &&
           address >= pib->base &&
           address <= pib->base + pib->size - sizeof(uint64_t);
}

static void zx1_test_deliver(void *opaque, const HPIOSAPICMessage *message)
{
    ZX1TestDeliveryContext *context = opaque;
    IA64ZX6000ZX1TestState *s = context->fixture;
    unsigned int root = context->root;
    MemTxResult result = MEMTX_DECODE_ERROR;

    g_assert(root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT);

    s->last_address[root] = message->address;
    s->last_data[root] = message->data;
    if (zx1_test_message_in_pib(s, message->address)) {
        address_space_stq_le(&address_space_memory, message->address,
                             message->data, MEMTXATTRS_UNSPECIFIED,
                             &result);
    }
    s->last_result[root] = result;
    s->delivery_count[root]++;
}

static bool zx1_test_create_mio(IA64ZX6000ZX1TestState *s, Error **errp)
{
    const IA64ZX6000ZX1TestIOMMU *layout_iommu = &s->layout.iommu;
    HPZX1MIOIOMMUResetConfig config = {
        .ibase = layout_iommu->ibase_reset,
        .imask = layout_iommu->imask_reset,
        .pcom = layout_iommu->pcom_reset,
        .tcnfg = layout_iommu->tcnfg_reset,
        .pdir_base = layout_iommu->pdir_base_reset,
    };
    DeviceState *child = zx1_test_add_child(DEVICE(s), "mio", TYPE_HP_ZX1_MIO);

    s->mio = HP_ZX1_MIO(child);
    if (!hp_zx1_mio_configure_iommu_reset(s->mio, &config, errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(child), errp)) {
        return false;
    }
    if (memory_region_size(sysbus_mmio_get_region(
            SYS_BUS_DEVICE(child), 0)) != s->layout.mio.size) {
        error_setg(errp, "zx6000 zx1 test MIO CSR size mismatch");
        return false;
    }
    return true;
}

static bool zx1_test_create_ioa(IA64ZX6000ZX1TestState *s, unsigned int root,
                          Error **errp)
{
    const IA64ZX6000ZX1TestRoot *layout_root = &s->layout.roots[root];
    ZX1TestDeliveryContext *context = &s->delivery_context[root];
    HPZX1IOASetup setup = {
        .mode = layout_root->mode,
        .rope_mask = layout_root->rope_mask,
        .secondary_bus = layout_root->first_bus,
        .subordinate_bus = layout_root->last_bus,
        /* The test layout starts with PCI reset deasserted. */
        .pci_reset_asserted = false,
        .bus_mode_reset = layout_root->bus_mode_reset,
        .slave_control_reset_straps = 0,
        .error_configuration_reset_straps = 0,
        .deliver = zx1_test_deliver,
        .delivery_opaque = context,
    };
    g_autofree char *child_name = g_strdup_printf("ioa%u", root);
    g_autofree char *alias_name = g_strdup_printf(
        TYPE_IA64_ZX6000_ZX1_TEST ".root%u-mmio", root);
    DeviceState *child;

    memcpy(setup.intx_route, layout_root->intx_route,
           sizeof(setup.intx_route));
    context->fixture = s;
    context->root = root;

    child = zx1_test_add_child(DEVICE(s), child_name, TYPE_HP_ZX1_IOA);
    s->ioas[root] = HP_ZX1_IOA(child);
    if (!hp_zx1_ioa_setup(s->ioas[root], &setup, errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(child), errp)) {
        return false;
    }
    if (memory_region_size(sysbus_mmio_get_region(
            SYS_BUS_DEVICE(child), 0)) != layout_root->ioa_csr.size) {
        error_setg(errp,
                   "zx6000 zx1 test Mercury %u CSR size mismatch", root);
        return false;
    }

    if (!hp_zx1_mio_attach_ioa(s->mio, s->ioas[root], errp)) {
        return false;
    }
    s->roots_attached[root] = true;

    memory_region_init_alias(&s->root_mmio[root], OBJECT(s), alias_name,
                             hp_zx1_ioa_pci_mem(s->ioas[root]),
                             layout_root->pci_mmio.base,
                             layout_root->pci_mmio.size);
    s->root_mmio_initialized[root] = true;
    return true;
}

static void zx1_test_map_system(IA64ZX6000ZX1TestState *s)
{
    unsigned int root;

    memory_region_transaction_begin();
    memory_region_add_subregion(
        get_system_memory(), s->layout.mio.base,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->mio), 0));
    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        memory_region_add_subregion(
            get_system_memory(), s->layout.roots[root].ioa_csr.base,
            sysbus_mmio_get_region(SYS_BUS_DEVICE(s->ioas[root]), 0));
        memory_region_add_subregion(
            get_system_memory(), s->layout.roots[root].cpu_mmio.base,
            &s->root_mmio[root]);
    }
    memory_region_transaction_commit();
    s->system_regions_mapped = true;
}

static void zx1_test_unmap_system(IA64ZX6000ZX1TestState *s)
{
    int root;

    if (!s->system_regions_mapped) {
        return;
    }

    memory_region_transaction_begin();
    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        memory_region_del_subregion(get_system_memory(),
                                    &s->root_mmio[root]);
        memory_region_del_subregion(
            get_system_memory(),
            sysbus_mmio_get_region(SYS_BUS_DEVICE(s->ioas[root]), 0));
    }
    memory_region_del_subregion(
        get_system_memory(),
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->mio), 0));
    memory_region_transaction_commit();
    s->system_regions_mapped = false;
}

static void zx1_test_detach_roots(IA64ZX6000ZX1TestState *s)
{
    int root;

    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        bool detached;

        if (!s->roots_attached[root]) {
            continue;
        }
        /* PCI children are removed before fixture teardown. */
        detached = hp_zx1_mio_detach_pci_root(
            s->mio, hp_zx1_ioa_bus(s->ioas[root]), &error_abort);
        g_assert(detached);
        s->roots_attached[root] = false;
    }
}

static void zx1_test_destroy_aliases(IA64ZX6000ZX1TestState *s)
{
    int root;

    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        if (s->root_mmio_initialized[root]) {
            object_unparent(OBJECT(&s->root_mmio[root]));
            s->root_mmio_initialized[root] = false;
        }
    }
}

static void zx1_test_cleanup(IA64ZX6000ZX1TestState *s)
{
    DeviceState *child;
    int root;

    if (s->reset_registered) {
        qemu_unregister_resettable(OBJECT(s));
        s->reset_registered = false;
    }
    zx1_test_detach_roots(s);
    zx1_test_unmap_system(s);
    zx1_test_destroy_aliases(s);

    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        if (s->ioas[root]) {
            child = DEVICE(s->ioas[root]);
            zx1_test_remove_child(&child);
            s->ioas[root] = NULL;
        }
    }
    if (s->mio) {
        child = DEVICE(s->mio);
        zx1_test_remove_child(&child);
        s->mio = NULL;
    }
}

static bool zx1_test_layout_baseline_valid(IA64ZX6000ZX1TestState *s,
                                     Error **errp)
{
    unsigned int root;
    unsigned int pin;
    unsigned int slot;

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        const IA64ZX6000ZX1TestRoot *layout_root =
            &s->layout.roots[root];

        if (s->layout_baseline[root].mode != layout_root->mode) {
            error_setg(errp,
                       "zx6000 zx1 test migration changed root %u mode",
                       root);
            return false;
        }
        for (slot = 0; slot < IA64_ZX6000_ZX1_TEST_SLOT_COUNT; slot++) {
            uint32_t packed = 0;

            for (pin = 0;
                 pin < IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT; pin++) {
                packed |= (uint32_t)layout_root->intx_route[slot][pin]
                          << (pin * 8);
            }
            if (s->layout_baseline[root].routes[slot].packed != packed) {
                error_setg(errp,
                           "zx6000 zx1 test migration changed root %u "
                           "INTx routes", root);
                return false;
            }
        }
    }
    return true;
}

static bool zx1_test_delivery_state_valid(IA64ZX6000ZX1TestState *s,
                                    Error **errp)
{
    unsigned int root;

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        if (!s->delivery_count[root]) {
            if (s->last_address[root] || s->last_data[root] ||
                s->last_result[root] !=
                    IA64_ZX6000_ZX1_TEST_DELIVERY_NOT_RUN) {
                error_setg(errp,
                           "zx6000 zx1 test migration has inconsistent "
                           "empty delivery state for root %u", root);
                return false;
            }
        } else if (!zx1_test_message_in_pib(s, s->last_address[root]) ||
                   (s->last_result[root] & ~ZX1_TEST_DELIVERY_RESULT_MASK)) {
            error_setg(errp,
                       "zx6000 zx1 test migration has invalid last "
                       "delivery for root %u", root);
            return false;
        }
    }
    return true;
}

static bool zx1_test_realized_topology_valid(IA64ZX6000ZX1TestState *s,
                                       Error **errp)
{
    unsigned int root;

    if (!s->mio || !qdev_is_realized(DEVICE(s->mio)) ||
        !hp_zx1_mio_iommu_address_space(s->mio) ||
        !s->system_regions_mapped ||
        !zx1_test_system_range_is_region(
            s->layout.mio.base, s->layout.mio.size,
            sysbus_mmio_get_region(SYS_BUS_DEVICE(s->mio), 0), 0)) {
        error_setg(errp,
                   "zx6000 zx1 test migration destination has no MIO "
                   "topology");
        return false;
    }

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        const IA64ZX6000ZX1TestRoot *layout_root =
            &s->layout.roots[root];
        PCIBus *bus;

        if (!s->ioas[root] || !qdev_is_realized(DEVICE(s->ioas[root])) ||
            !s->roots_attached[root] ||
            !s->root_mmio_initialized[root] ||
            !memory_region_is_mapped(&s->root_mmio[root]) ||
            memory_region_size(&s->root_mmio[root]) !=
                layout_root->cpu_mmio.size) {
            error_setg(errp,
                       "zx6000 zx1 test migration destination lacks "
                       "Mercury root %u", root);
            return false;
        }
        bus = hp_zx1_ioa_bus(s->ioas[root]);
        if (!bus || pci_bus_num(bus) != layout_root->first_bus ||
            !zx1_test_system_range_is_region(
                layout_root->ioa_csr.base, layout_root->ioa_csr.size,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(s->ioas[root]), 0),
                0)) {
            error_setg(errp,
                       "zx6000 zx1 test migration destination has "
                       "invalid Mercury root %u mappings", root);
            return false;
        }
    }
    return true;
}

static bool zx1_test_post_load(void *opaque, int version_id, Error **errp)
{
    IA64ZX6000ZX1TestState *s = opaque;

    if (version_id != 1) {
        error_setg(errp,
                   "zx6000 zx1 test migration version %d is invalid",
                   version_id);
        return false;
    }
    if (!ia64_zx6000_zx1_test_layout_validate(&s->layout, errp) ||
        !zx1_test_layout_baseline_valid(s, errp) ||
        !zx1_test_system_range_is_ram(s, s->layout.ram.base,
                                s->layout.ram.size, errp) ||
        !zx1_test_system_pib_is_present(s->layout.pib.base,
                                  s->layout.pib.size, errp) ||
        !zx1_test_realized_topology_valid(s, errp) ||
        !zx1_test_delivery_state_valid(s, errp)) {
        return false;
    }

    /* Child post-load code restores latches; never replay a delivery here. */
    return true;
}

static bool zx1_test_pre_save(void *opaque, Error **errp)
{
    IA64ZX6000ZX1TestState *s = opaque;

    return ia64_zx6000_zx1_test_layout_validate(&s->layout, errp) &&
           zx1_test_layout_baseline_valid(s, errp) &&
           zx1_test_system_range_is_ram(s, s->layout.ram.base,
                                  s->layout.ram.size, errp) &&
           zx1_test_system_pib_is_present(s->layout.pib.base,
                                    s->layout.pib.size, errp) &&
           zx1_test_realized_topology_valid(s, errp) &&
           zx1_test_delivery_state_valid(s, errp);
}

static void zx1_test_realize(DeviceState *dev, Error **errp)
{
    IA64ZX6000ZX1TestState *s = IA64_ZX6000_ZX1_TEST(dev);
    Error *local_err = NULL;
    unsigned int root;

    /* Finish every fallible range check before constructing any child. */
    if (!ia64_zx6000_zx1_test_layout_validate(&s->layout, &local_err) ||
        !zx1_test_layout_baseline_valid(s, &local_err) ||
        !zx1_test_validate_parent_resources(s, &local_err) ||
        !zx1_test_create_mio(s, &local_err)) {
        goto fail;
    }
    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        if (!zx1_test_create_ioa(s, root, &local_err)) {
            goto fail;
        }
    }

    zx1_test_map_system(s);
    /* Register the busless builder so its delivery state participates in reset. */
    qemu_register_resettable(OBJECT(s));
    s->reset_registered = true;
    return;

fail:
    zx1_test_cleanup(s);
    error_propagate(errp, local_err);
}

static void zx1_test_unrealize(DeviceState *dev)
{
    zx1_test_cleanup(IA64_ZX6000_ZX1_TEST(dev));
}

static void zx1_test_reset(DeviceState *dev)
{
    IA64ZX6000ZX1TestState *s = IA64_ZX6000_ZX1_TEST(dev);
    unsigned int root;

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        s->delivery_count[root] = 0;
        s->last_address[root] = 0;
        s->last_data[root] = 0;
        s->last_result[root] =
            IA64_ZX6000_ZX1_TEST_DELIVERY_NOT_RUN;
    }
}

static void zx1_test_init_layout_baseline(IA64ZX6000ZX1TestState *s)
{
    unsigned int root;
    unsigned int pin;
    unsigned int slot;

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        const IA64ZX6000ZX1TestRoot *layout_root =
            &s->layout.roots[root];

        s->layout_baseline[root].mode = layout_root->mode;
        for (slot = 0; slot < IA64_ZX6000_ZX1_TEST_SLOT_COUNT; slot++) {
            uint32_t packed = 0;

            for (pin = 0;
                 pin < IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT; pin++) {
                packed |= (uint32_t)layout_root->intx_route[slot][pin]
                          << (pin * 8);
            }
            s->layout_baseline[root].routes[slot].packed = packed;
        }
    }
}

static void zx1_test_init(Object *obj)
{
    static const char *const count_properties[] = {
        IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_0,
        IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_1,
    };
    static const char *const address_properties[] = {
        IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_0,
        IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_1,
    };
    static const char *const data_properties[] = {
        IA64_ZX6000_ZX1_TEST_LAST_DATA_0,
        IA64_ZX6000_ZX1_TEST_LAST_DATA_1,
    };
    static const char *const result_properties[] = {
        IA64_ZX6000_ZX1_TEST_LAST_RESULT_0,
        IA64_ZX6000_ZX1_TEST_LAST_RESULT_1,
    };
    IA64ZX6000ZX1TestState *s = IA64_ZX6000_ZX1_TEST(obj);
    unsigned int root;

    ia64_zx6000_zx1_test_layout_init(&s->layout);
    zx1_test_init_layout_baseline(s);
    zx1_test_reset(DEVICE(s));

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        object_property_add_uint64_ptr(obj, count_properties[root],
                                       &s->delivery_count[root],
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint64_ptr(obj, address_properties[root],
                                       &s->last_address[root],
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint32_ptr(obj, data_properties[root],
                                       &s->last_data[root],
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint32_ptr(obj, result_properties[root],
                                       &s->last_result[root],
                                       OBJ_PROP_FLAG_READ);
    }
}

static const VMStateDescription vmstate_zx1_test_range = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST "/range",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_EQUAL(base, IA64ZX6000ZX1TestRange),
        VMSTATE_UINT64_EQUAL(size, IA64ZX6000ZX1TestRange),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_zx1_test_iommu_layout = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST "/iommu-layout",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(aperture, IA64ZX6000ZX1TestIOMMU, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(pdir, IA64ZX6000ZX1TestIOMMU, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(test_target, IA64ZX6000ZX1TestIOMMU, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_UINT64_EQUAL(ibase_reset, IA64ZX6000ZX1TestIOMMU),
        VMSTATE_UINT64_EQUAL(imask_reset, IA64ZX6000ZX1TestIOMMU),
        VMSTATE_UINT64_EQUAL(pcom_reset, IA64ZX6000ZX1TestIOMMU),
        VMSTATE_UINT64_EQUAL(tcnfg_reset, IA64ZX6000ZX1TestIOMMU),
        VMSTATE_UINT64_EQUAL(pdir_base_reset, IA64ZX6000ZX1TestIOMMU),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_zx1_test_root_layout = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST "/root-layout",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ioa_csr, IA64ZX6000ZX1TestRoot, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(pci_mmio, IA64ZX6000ZX1TestRoot, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(cpu_mmio, IA64ZX6000ZX1TestRoot, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_UINT32_EQUAL(rope_mask, IA64ZX6000ZX1TestRoot),
        VMSTATE_UINT64_EQUAL(bus_mode_reset, IA64ZX6000ZX1TestRoot),
        VMSTATE_UINT8_EQUAL(first_bus, IA64ZX6000ZX1TestRoot),
        VMSTATE_UINT8_EQUAL(last_bus, IA64ZX6000ZX1TestRoot),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_zx1_test_route_baseline = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST "/route-layout",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(packed, ZX1TestRouteBaseline),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_zx1_test_root_baseline = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST "/root-baseline",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_EQUAL(mode, ZX1TestRootBaseline),
        VMSTATE_STRUCT_ARRAY(routes, ZX1TestRootBaseline,
                             IA64_ZX6000_ZX1_TEST_SLOT_COUNT, 1,
                             vmstate_zx1_test_route_baseline,
                             ZX1TestRouteBaseline),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_zx1_test = {
    .name = TYPE_IA64_ZX6000_ZX1_TEST,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save_errp = zx1_test_pre_save,
    .post_load_errp = zx1_test_post_load,
    .fields = (const VMStateField[]) {
        /* Fixed layout values must match at the destination. */
        VMSTATE_STRUCT(layout.ram, IA64ZX6000ZX1TestState, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(layout.mio, IA64ZX6000ZX1TestState, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(layout.pib, IA64ZX6000ZX1TestState, 1,
                       vmstate_zx1_test_range, IA64ZX6000ZX1TestRange),
        VMSTATE_STRUCT(layout.iommu, IA64ZX6000ZX1TestState, 1,
                       vmstate_zx1_test_iommu_layout,
                       IA64ZX6000ZX1TestIOMMU),
        VMSTATE_STRUCT_ARRAY(layout.roots, IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT, 1,
                             vmstate_zx1_test_root_layout,
                             IA64ZX6000ZX1TestRoot),
        /* Packed mirrors make enum mode and every route byte equal fields. */
        VMSTATE_STRUCT_ARRAY(layout_baseline,
                             IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT, 1,
                             vmstate_zx1_test_root_baseline,
                             ZX1TestRootBaseline),

        VMSTATE_UINT64_ARRAY(delivery_count,
                             IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT),
        VMSTATE_UINT64_ARRAY(last_address,
                             IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT),
        VMSTATE_UINT32_ARRAY(last_data,
                             IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT),
        VMSTATE_UINT32_ARRAY(last_result,
                             IA64ZX6000ZX1TestState,
                             IA64_ZX6000_ZX1_TEST_ROOT_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static const Property zx1_test_properties[] = {
    DEFINE_PROP_LINK(IA64_ZX6000_ZX1_TEST_PROP_RAM,
                     IA64ZX6000ZX1TestState, ram,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void zx1_test_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 zx6000 ZX1 integration test";
    dc->realize = zx1_test_realize;
    dc->unrealize = zx1_test_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_zx1_test;
    device_class_set_legacy_reset(dc, zx1_test_reset);
    device_class_set_props(dc, zx1_test_properties);
}

PCIBus *ia64_zx6000_zx1_test_root_bus(
    IA64ZX6000ZX1TestState *fixture, unsigned int index)
{
    if (!fixture || index >= IA64_ZX6000_ZX1_TEST_ROOT_COUNT ||
        !fixture->ioas[index]) {
        return NULL;
    }
    return hp_zx1_ioa_bus(fixture->ioas[index]);
}

PCIDevice *ia64_zx6000_zx1_test_create_test_probe(
    IA64ZX6000ZX1TestState *fixture, unsigned int root,
    unsigned int slot, Error **errp)
{
    PCIBus *bus = ia64_zx6000_zx1_test_root_bus(fixture, root);
    PCIDevice *probe;

    if (bus == NULL || slot >= PCI_SLOT_MAX) {
        error_setg(errp, "invalid zx6000 zx1 test probe placement");
        return NULL;
    }
    probe = pci_new(PCI_DEVFN(slot, 0), TYPE_ZX1_TEST_IOMMU_TESTDEV);
    if (!qdev_realize(DEVICE(probe), BUS(bus), errp)) {
        object_unref(OBJECT(probe));
        return NULL;
    }
    pci_config_set_interrupt_pin(probe->config, 1);
    return probe;
}

void ia64_zx6000_zx1_test_destroy_test_probe(PCIDevice *probe)
{
    if (probe == NULL) {
        return;
    }
    if (qdev_is_realized(DEVICE(probe))) {
        qdev_unrealize(DEVICE(probe));
    }
    object_unparent(OBJECT(probe));
    object_unref(OBJECT(probe));
    drain_call_rcu();
}

qemu_irq ia64_zx6000_zx1_test_io_sapic_input(
    IA64ZX6000ZX1TestState *fixture, unsigned int root,
    unsigned int input)
{
    if (fixture == NULL || root >= IA64_ZX6000_ZX1_TEST_ROOT_COUNT ||
        input >= IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT ||
        fixture->ioas[root] == NULL) {
        return NULL;
    }
    return qdev_get_gpio_in_named(DEVICE(fixture->ioas[root]),
                                  HP_ZX1_IOA_GPIO_INTX, input);
}

static void zx1_qtest_remove_probes(IA64ZX6000ZX1QTestState *s)
{
    int probe;
    int root;

    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        for (probe = IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT - 1;
             probe >= 0; probe--) {
            PCIDevice *pdev = s->probes[root][probe];

            if (!pdev) {
                continue;
            }
            if (qdev_is_realized(DEVICE(pdev))) {
                qdev_unrealize(DEVICE(pdev));
            }
            object_unparent(OBJECT(pdev));
            object_unref(OBJECT(pdev));
            s->probes[root][probe] = NULL;
        }
    }
    /* Retire every probe-owned bus-master AddressSpace before MIO detach. */
    drain_call_rcu();
}

static void zx1_qtest_remove_fixture(IA64ZX6000ZX1QTestState *s)
{
    DeviceState *fixture;

    if (!s->fixture) {
        return;
    }
    fixture = DEVICE(s->fixture);
    zx1_test_remove_child(&fixture);
    s->fixture = NULL;
}

static void zx1_qtest_set_intx(void *opaque, int line, int level)
{
    IA64ZX6000ZX1QTestState *s = opaque;
    unsigned int device;
    unsigned int probe;
    unsigned int root;
    unsigned int pin;
    PCIDevice *pdev;

    if (line < 0) {
        return;
    }
    device = line / IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT;
    pin = line % IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT;
    root = device / IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT;
    probe = device % IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT;
    if (root >= IA64_ZX6000_ZX1_TEST_ROOT_COUNT) {
        return;
    }

    pdev = s->probes[root][probe];
    if (!pdev) {
        return;
    }

    pci_config_set_interrupt_pin(pdev->config, pin + 1);
    pci_set_irq(pdev, !!level);
}

static void zx1_qtest_trigger_msi(void *opaque, int line, int level)
{
    IA64ZX6000ZX1QTestState *s = opaque;
    unsigned int probe;
    unsigned int root;
    PCIDevice *pdev;

    if (!level || line < 0 ||
        line >= IA64_ZX6000_ZX1_TEST_PROBE_COUNT) {
        return;
    }
    root = line / IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT;
    probe = line % IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT;
    pdev = s->probes[root][probe];
    if (pdev && msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    }
}

static void zx1_qtest_realize(DeviceState *dev, Error **errp)
{
    IA64ZX6000ZX1QTestState *s = IA64_ZX6000_ZX1_QTEST(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *fixture;
    Error *local_err = NULL;
    unsigned int probe;
    unsigned int root;

    if (!qtest_enabled()) {
        error_setg(errp, "%s is available only under qtest",
                   TYPE_IA64_ZX6000_ZX1_QTEST);
        return;
    }
    if (!machine->ram) {
        error_setg(errp,
                   "%s requires parent-machine RAM and does not own "
                   "RAM or PIB", TYPE_IA64_ZX6000_ZX1_QTEST);
        return;
    }

    fixture = zx1_test_add_child(dev, IA64_ZX6000_ZX1_TEST_FIXTURE_CHILD,
                           TYPE_IA64_ZX6000_ZX1_TEST);
    s->fixture = IA64_ZX6000_ZX1_TEST(fixture);
    if (!object_property_set_link(OBJECT(fixture),
                                  IA64_ZX6000_ZX1_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(fixture, NULL, &local_err)) {
        goto fail;
    }

    /* Both empty roots are attached to the shared MIO before this point. */
    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        PCIBus *bus = ia64_zx6000_zx1_test_root_bus(s->fixture, root);

        if (!bus) {
            error_setg(&local_err,
                       "zx6000 zx1 test root %u is unavailable", root);
            goto fail;
        }
        for (probe = 0;
             probe < IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT; probe++) {
            PCIDevice *pdev = pci_new(
                PCI_DEVFN(IA64_ZX6000_ZX1_TEST_PROBE_SLOT(probe), 0),
                TYPE_ZX1_TEST_IOMMU_TESTDEV);

            if (!qdev_realize(DEVICE(pdev), BUS(bus), &local_err)) {
                object_unref(OBJECT(pdev));
                goto fail;
            }
            s->probes[root][probe] = pdev;
            pci_config_set_interrupt_pin(pdev->config, 1);
        }
    }
    return;

fail:
    zx1_qtest_remove_probes(s);
    zx1_qtest_remove_fixture(s);
    error_propagate(errp, local_err);
}

static void zx1_qtest_unrealize(DeviceState *dev)
{
    IA64ZX6000ZX1QTestState *s = IA64_ZX6000_ZX1_QTEST(dev);

    zx1_qtest_remove_probes(s);
    zx1_qtest_remove_fixture(s);
}

static void zx1_qtest_init(Object *obj)
{
    qdev_init_gpio_in_named(
        DEVICE(obj), zx1_qtest_set_intx, IA64_ZX6000_ZX1_TEST_GPIO_INTX,
        IA64_ZX6000_ZX1_TEST_PROBE_COUNT *
        IA64_ZX6000_ZX1_TEST_INTX_PIN_COUNT);
    qdev_init_gpio_in_named(
        DEVICE(obj), zx1_qtest_trigger_msi,
        IA64_ZX6000_ZX1_TEST_GPIO_MSI,
        IA64_ZX6000_ZX1_TEST_PROBE_COUNT);
}

static void zx1_qtest_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 zx6000 zx1 qtest device";
    dc->realize = zx1_qtest_realize;
    dc->unrealize = zx1_qtest_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
}

static int zx2_pcie_qtest_find_host(Object *child, void *opaque)
{
    IA64PCIState **host = opaque;

    if (object_dynamic_cast(child, TYPE_IA64_PCIE_HOST_BRIDGE)) {
        *host = IA64_PCI_HOST_BRIDGE(child);
        return 1;
    }
    return 0;
}

static void zx2_pcie_qtest_unrealize(DeviceState *dev)
{
    IA64ZX2PCIeQTestState *s = IA64_ZX2_PCIE_QTEST(dev);
    Error *local_err = NULL;
    DeviceState *mio;

    if (s->probe) {
        qdev_unrealize(DEVICE(s->probe));
        object_unparent(OBJECT(s->probe));
        object_unref(OBJECT(s->probe));
        s->probe = NULL;
    }
    if (s->root_port) {
        qdev_unrealize(DEVICE(s->root_port));
        object_unparent(OBJECT(s->root_port));
        object_unref(OBJECT(s->root_port));
        s->root_port = NULL;
    }
    if (s->second_probe) {
        qdev_unrealize(DEVICE(s->second_probe));
        object_unparent(OBJECT(s->second_probe));
        object_unref(OBJECT(s->second_probe));
        s->second_probe = NULL;
    }
    drain_call_rcu();
    if (s->root_attached) {
        bool detached = hp_zx1_mio_detach_pci_root(
            s->mio, ia64_pci_host_bus(s->host), &local_err);

        g_assert(detached);
        g_assert_null(local_err);
        s->root_attached = false;
    }
    if (s->second_root_attached) {
        bool detached = hp_zx1_mio_detach_pci_root(
            s->mio, ia64_pci_host_bus(s->second_host), &local_err);

        g_assert(detached);
        g_assert_null(local_err);
        s->second_root_attached = false;
    }
    if (s->mio_mapped) {
        memory_region_del_subregion(
            get_system_memory(),
            sysbus_mmio_get_region(SYS_BUS_DEVICE(s->mio), 0));
        s->mio_mapped = false;
    }
    if (s->mio) {
        mio = DEVICE(s->mio);
        zx1_test_remove_child(&mio);
        s->mio = NULL;
    }
    if (s->owns_host) {
        DeviceState *host = DEVICE(s->host);

        zx1_test_remove_child(&host);
        s->owns_host = false;
    }
    if (s->second_host) {
        DeviceState *host = DEVICE(s->second_host);

        zx1_test_remove_child(&host);
        s->second_host = NULL;
    }
    if (s->ram_mapped) {
        memory_region_del_subregion(get_system_memory(), &s->ram);
        s->ram_mapped = false;
    }
    if (s->cpu) {
        zx1_test_remove_child(&s->cpu);
    }
    s->host = NULL;
}

static void zx2_pcie_qtest_realize(DeviceState *dev, Error **errp)
{
    IA64ZX2PCIeQTestState *s = IA64_ZX2_PCIE_QTEST(dev);
    HPZX1MIOIOMMUResetConfig iommu_reset = { 0 };
    IA64PCIHostConfig second_config = {
        .segment = 1,
        .first_bus = 0x80,
        .last_bus = 0x80,
        .ecam_base = UINT64_C(0x0000008000000000),
        .ecam_size = 1 * MiB,
        .mmio_cpu_base = UINT64_C(0xb2000000),
        .mmio_bus_base = UINT64_C(0x90000000),
        .mmio_size = 1 * MiB,
        .io_cpu_base = UINT64_C(0x00000ffff8000000),
        .io_bus_base = UINT64_C(0x8000),
        .io_size = UINT64_C(0x100),
        .gsi_base = 80,
    };
    IA64PCIHostConfig observed_config;
    DeviceState *mio;
    PCIBus *root;
    PCIBus *downstream;
    Error *local_err = NULL;

    if (!qtest_enabled()) {
        error_setg(errp, "%s is available only under qtest",
                   TYPE_IA64_ZX2_PCIE_QTEST);
        return;
    }
    s->cpu = zx1_test_add_child(dev, "cpu", "itanium2-ia64-cpu");
    if (!qdev_realize(s->cpu, NULL, &local_err)) {
        goto fail;
    }
    object_child_foreach_recursive(OBJECT(qdev_get_machine()),
                                   zx2_pcie_qtest_find_host, &s->host);
    if (!s->host) {
        DeviceState *host = zx1_test_add_child(
            dev, "host", TYPE_IA64_PCIE_HOST_BRIDGE);

        s->host = IA64_PCI_HOST_BRIDGE(host);
        s->owns_host = true;
        if (!sysbus_realize(SYS_BUS_DEVICE(host), &local_err)) {
            goto fail;
        }
    } else if (!qdev_is_realized(DEVICE(s->host))) {
        error_setg(&local_err, "%s found an unrealized IA-64 PCIe host",
                   TYPE_IA64_ZX2_PCIE_QTEST);
        goto fail;
    }
    root = ia64_pci_host_bus(s->host);
    if (!root || !pci_bus_is_express(root)) {
        error_setg(&local_err, "%s requires a PCIe root bus",
                   TYPE_IA64_ZX2_PCIE_QTEST);
        goto fail;
    }

    if (s->owns_host) {
        if (!memory_region_init_ram(&s->ram, OBJECT(s),
                                    TYPE_IA64_ZX2_PCIE_QTEST ".ram",
                                    64 * MiB, &local_err)) {
            goto fail;
        }
        memory_region_add_subregion(get_system_memory(), 0, &s->ram);
        s->ram_mapped = true;
    }

    mio = zx1_test_add_child(dev, "mio", TYPE_HP_ZX2_MIO);
    s->mio = HP_ZX1_MIO(mio);
    if (!hp_zx1_mio_configure_iommu_reset(s->mio, &iommu_reset,
                                           &local_err) ||
        !sysbus_realize(SYS_BUS_DEVICE(mio), &local_err)) {
        goto fail;
    }
    memory_region_add_subregion(
        get_system_memory(), ZX2_PCIE_TEST_MIO_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->mio), 0));
    s->mio_mapped = true;
    if (!hp_zx2_mio_attach_pci_root(s->mio, root,
                                    ZX2_PCIE_TEST_ROPES, &local_err)) {
        goto fail;
    }
    s->root_attached = true;
    if (!root->iommu_per_bus) {
        error_setg(&local_err,
                   "zx2 PCIe root did not install a root-private IOMMU");
        goto fail;
    }
    s->second_host = IA64_PCI_HOST_BRIDGE(zx1_test_add_child(
        dev, "second-host", TYPE_IA64_PCIE_HOST_BRIDGE));
    if (!ia64_pci_host_configure(s->second_host, &second_config,
                                 &local_err) ||
        !sysbus_realize(SYS_BUS_DEVICE(s->second_host), &local_err)) {
        goto fail;
    }
    ia64_pci_host_get_config(s->second_host, &observed_config);
    if (memcmp(&observed_config, &second_config, sizeof(second_config))) {
        error_setg(&local_err,
                   "second IA-64 PCIe host did not retain its root descriptor values");
        goto fail;
    }
    root = ia64_pci_host_bus(s->second_host);
    if (pci_bus_num(root) != second_config.first_bus) {
        error_setg(&local_err,
                   "second IA-64 PCIe host exposed the wrong first bus");
        goto fail;
    }
    if (hp_zx2_mio_attach_pci_root(s->mio, root,
                                   ZX2_PCIE_TEST_ROPES, &local_err)) {
        hp_zx1_mio_detach_pci_root(s->mio, root, NULL);
        error_setg(&local_err,
                   "zx2 MIOC accepted duplicate rope ownership");
        goto fail;
    }
    if (!local_err || !strstr(error_get_pretty(local_err), "overlaps")) {
        if (local_err) {
            error_prepend(&local_err,
                          "unexpected duplicate zx2 rope rejection: ");
        } else {
            error_setg(&local_err,
                       "zx2 MIOC rejected duplicate ropes without an error");
        }
        goto fail;
    }
    error_free(local_err);
    local_err = NULL;
    if (!hp_zx2_mio_attach_pci_root(
            s->mio, root, UINT16_C(1) << 14, &local_err)) {
        goto fail;
    }
    s->second_root_attached = true;
    if (!root->iommu_per_bus) {
        error_setg(&local_err,
                   "second zx2 PCIe root did not install a root-private IOMMU");
        goto fail;
    }
    s->second_probe = pci_new(IA64_ZX2_PCIE_SECOND_DEVFN,
                              TYPE_IOMMU_TESTDEV);
    if (!qdev_realize(DEVICE(s->second_probe), BUS(root), &local_err)) {
        object_unref(OBJECT(s->second_probe));
        s->second_probe = NULL;
        goto fail;
    }
    root = ia64_pci_host_bus(s->host);

    s->root_port = pci_new(PCI_DEVFN(7, 0), TYPE_IA64_PCIE_ROOT_PORT);
    qdev_prop_set_uint8(DEVICE(s->root_port), "chassis", 1);
    qdev_prop_set_uint16(DEVICE(s->root_port), "slot", 7);
    if (!qdev_realize(DEVICE(s->root_port), BUS(root), &local_err)) {
        object_unref(OBJECT(s->root_port));
        s->root_port = NULL;
        goto fail;
    }
    downstream = pci_bridge_get_sec_bus(PCI_BRIDGE(s->root_port));
    s->probe = pci_new(PCI_DEVFN(0, 0), TYPE_IOMMU_TESTDEV);
    if (!qdev_realize(DEVICE(s->probe), BUS(downstream), &local_err)) {
        object_unref(OBJECT(s->probe));
        s->probe = NULL;
        goto fail;
    }
    return;

fail:
    zx2_pcie_qtest_unrealize(dev);
    error_propagate(errp, local_err);
}

static void zx2_pcie_qtest_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 zx2 PCIe IOMMU qtest device";
    dc->realize = zx2_pcie_qtest_realize;
    dc->unrealize = zx2_pcie_qtest_unrealize;
    dc->user_creatable = true;
    dc->hotpluggable = false;
}

static void zx1_test_probe_realize(PCIDevice *pdev, Error **errp)
{
    Error *local_err = NULL;

    zx1_test_probe_parent_realize(pdev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msi_init(pdev, ZX1_TEST_MSI_CAP_OFFSET, 1, true, false,
                 &local_err) < 0) {
        if (zx1_test_probe_parent_exit) {
            zx1_test_probe_parent_exit(pdev);
        }
        error_propagate(errp, local_err);
    }
}

static void zx1_test_probe_exit(PCIDevice *pdev)
{
    msi_uninit(pdev);
    if (zx1_test_probe_parent_exit) {
        zx1_test_probe_parent_exit(pdev);
    }
}

static void zx1_test_probe_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    (void)data;
    /*
     * This subtype migrates generic PCI configuration and irq_state.  Its DMA
     * registers reset on the destination.  Loading irq_state does not call
     * pci_set_irq(), so migration does not replay a SAPIC delivery.
     */
    dc->desc = "zx6000 zx1 test IOMMU probe";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_pci_device;
    zx1_test_probe_parent_realize = pc->realize;
    zx1_test_probe_parent_exit = pc->exit;
    pc->realize = zx1_test_probe_realize;
    pc->exit = zx1_test_probe_exit;
}

static const TypeInfo zx1_test_types[] = {
    {
        .name = TYPE_ZX1_TEST_IOMMU_TESTDEV,
        .parent = TYPE_IOMMU_TESTDEV,
        .class_init = zx1_test_probe_class_init,
    },
    {
        .name = TYPE_IA64_ZX6000_ZX1_TEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64ZX6000ZX1TestState),
        .instance_init = zx1_test_init,
        .class_init = zx1_test_class_init,
    }, {
        .name = TYPE_IA64_ZX6000_ZX1_QTEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64ZX6000ZX1QTestState),
        .instance_init = zx1_qtest_init,
        .class_init = zx1_qtest_class_init,
    }, {
        .name = TYPE_IA64_ZX2_PCIE_QTEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64ZX2PCIeQTestState),
        .class_init = zx2_pcie_qtest_class_init,
    },
};

DEFINE_TYPES(zx1_test_types)
