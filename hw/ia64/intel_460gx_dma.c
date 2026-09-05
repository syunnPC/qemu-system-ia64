/*
 * Intel 460GX per-root inbound PCI DMA aperture
 *
 * A 36-bit address-space container denies unmapped transactions.  RAM aliases
 * expose only the inbound ranges supplied by the machine.
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

#define TYPE_INTEL_460GX_DMA "intel-460gx-dma-aperture"

typedef struct Intel460GXDMAAlias {
    MemoryRegion mr;
    uint64_t dma_base;
    uint64_t size;
} Intel460GXDMAAlias;

typedef struct Intel460GXDMARequesterAlias {
    MemoryRegion mr;
} Intel460GXDMARequesterAlias;

typedef struct Intel460GXDMARequester {
    Intel460GXDMA *dma;
    MemoryRegion container;
    MemoryRegion fault;
    AddressSpace address_space;
    GPtrArray *aliases;
    uint8_t devfn;
} Intel460GXDMARequester;

struct Intel460GXDMA {
    Object parent_obj;

    MemoryRegion container;
    AddressSpace address_space;
    GPtrArray *aliases;
    Intel460GXDMARequester *requesters[PCI_DEVFN_MAX];
    PCIBus *bus;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
    uint64_t aperture_base;
    uint64_t aperture_size;
    bool initialized;
    bool sealed;
};

typedef struct Intel460GXDMAClass {
    ObjectClass parent_class;
} Intel460GXDMAClass;

DECLARE_INSTANCE_CHECKER(Intel460GXDMA, INTEL_460GX_DMA,
                         TYPE_INTEL_460GX_DMA)

OBJECT_DEFINE_TYPE(Intel460GXDMA, intel_460gx_dma,
                   INTEL_460GX_DMA, OBJECT)

static bool dma_range_end(uint64_t base, uint64_t size, uint64_t *end)
{
    if (size > UINT64_MAX - base) {
        return false;
    }

    *end = base + size;
    return true;
}

static bool dma_aperture_validate(uint64_t base, uint64_t size,
                                  Error **errp)
{
    if (base == 0 && size == 0) {
        return true;
    }
    if (size == 0) {
        error_setg(errp, "460GX DMA aperture has a zero size at 0x%" PRIx64,
                   base);
        return false;
    }
    if (base >= INTEL_460GX_DMA_ADDRESS_LIMIT ||
        size > INTEL_460GX_DMA_ADDRESS_LIMIT - base) {
        error_setg(errp,
                   "460GX DMA aperture [0x%" PRIx64 ", +0x%" PRIx64
                   ") exceeds the 36-bit address space",
                   base, size);
        return false;
    }

    return true;
}

static bool dma_alias_range_validate(const Intel460GXDMA *dma,
                                     uint64_t dma_base, uint64_t size,
                                     MemoryRegion *target,
                                     uint64_t target_offset,
                                     Error **errp)
{
    uint64_t aperture_end = 0;
    uint64_t dma_end;
    uint64_t target_size;
    size_t i;

    if (!dma || !dma->initialized) {
        error_setg(errp, "460GX DMA view is not initialized");
        return false;
    }
    if (dma->sealed) {
        error_setg(errp, "460GX DMA view is already sealed");
        return false;
    }
    if (!target) {
        error_setg(errp, "460GX DMA alias has no target");
        return false;
    }
    if (size == 0) {
        error_setg(errp, "460GX DMA alias has zero size");
        return false;
    }
    if (!dma_range_end(dma_base, size, &dma_end) ||
        dma_end > INTEL_460GX_DMA_ADDRESS_LIMIT) {
        error_setg(errp,
                   "460GX DMA alias [0x%" PRIx64 ", +0x%" PRIx64
                   ") exceeds the 36-bit address space",
                   dma_base, size);
        return false;
    }
    if (!dma_range_end(dma->aperture_base, dma->aperture_size,
                       &aperture_end) ||
        dma_base < dma->aperture_base || dma_end > aperture_end) {
        error_setg(errp,
                   "460GX DMA alias [0x%" PRIx64 ", 0x%" PRIx64
                   ") is outside aperture [0x%" PRIx64 ", 0x%" PRIx64
                   ")",
                   dma_base, dma_end, dma->aperture_base, aperture_end);
        return false;
    }

    target_size = memory_region_size(target);
    if (target_offset > UINT64_MAX - size || target_offset > target_size ||
        size > target_size - target_offset) {
        error_setg(errp,
                   "460GX DMA RAM target [0x%" PRIx64 ", +0x%" PRIx64
                   ") exceeds target size 0x%" PRIx64,
                   target_offset, size, target_size);
        return false;
    }

    for (i = 0; i < dma->aliases->len; i++) {
        const Intel460GXDMAAlias *alias = g_ptr_array_index(dma->aliases, i);
        uint64_t alias_end = alias->dma_base + alias->size;

        if (dma_base < alias_end && alias->dma_base < dma_end) {
            error_setg(errp,
                       "460GX DMA alias [0x%" PRIx64 ", 0x%" PRIx64
                       ") overlaps [0x%" PRIx64 ", 0x%" PRIx64 ")",
                       dma_base, dma_end, alias->dma_base, alias_end);
            return false;
        }
    }

    return true;
}

static bool dma_alias_add(Intel460GXDMA *dma, uint64_t dma_base,
                          uint64_t size, MemoryRegion *target,
                          uint64_t target_offset, Error **errp)
{
    Intel460GXDMAAlias *alias;
    g_autofree char *name = NULL;

    if (!dma_alias_range_validate(dma, dma_base, size, target,
                                  target_offset, errp)) {
        return false;
    }

    alias = g_new0(Intel460GXDMAAlias, 1);
    alias->dma_base = dma_base;
    alias->size = size;
    name = g_strdup_printf("intel-460gx-dma-alias-%u", dma->aliases->len);
    memory_region_init_alias(&alias->mr, OBJECT(dma), name, target,
                             target_offset, size);
    memory_region_add_subregion(&dma->container, dma_base, &alias->mr);
    g_ptr_array_add(dma->aliases, alias);

    return true;
}

Intel460GXDMA *intel_460gx_dma_new(uint64_t aperture_base,
                                  uint64_t aperture_size,
                                  Error **errp)
{
    Intel460GXDMA *dma;

    if (!dma_aperture_validate(aperture_base, aperture_size, errp)) {
        return NULL;
    }

    dma = INTEL_460GX_DMA(object_new(TYPE_INTEL_460GX_DMA));
    dma->aperture_base = aperture_base;
    dma->aperture_size = aperture_size;
    memory_region_init(&dma->container, OBJECT(dma),
                       "intel-460gx-dma-container",
                       INTEL_460GX_DMA_ADDRESS_LIMIT);
    address_space_init(&dma->address_space, &dma->container,
                       "intel-460gx-dma");
    dma->initialized = true;

    return dma;
}

bool intel_460gx_dma_add_ram_alias(Intel460GXDMA *dma,
                                   uint64_t dma_base,
                                   uint64_t size,
                                   MemoryRegion *target,
                                   uint64_t target_offset,
                                   Error **errp)
{
    if (target == NULL || !memory_region_is_ram(target) ||
        memory_region_is_ram_device(target) || memory_region_is_rom(target) ||
        memory_region_is_protected(target)) {
        error_setg(errp, "460GX DMA RAM alias target is not ordinary writable RAM");
        return false;
    }

    return dma_alias_add(dma, dma_base, size, target, target_offset, errp);
}

bool intel_460gx_dma_add_pci_window_alias(Intel460GXDMA *dma,
                                          uint64_t dma_base,
                                          uint64_t size,
                                          MemoryRegion *pci_memory,
                                          uint64_t pci_offset,
                                          Error **errp)
{
    if (pci_memory == NULL || memory_region_is_ram(pci_memory) ||
        memory_region_is_ram_device(pci_memory) ||
        memory_region_is_rom(pci_memory) ||
        memory_region_is_protected(pci_memory)) {
        error_setg(errp,
                   "460GX DMA PCI window target is not a PCI memory region");
        return false;
    }

    return dma_alias_add(dma, dma_base, size, pci_memory, pci_offset, errp);
}

bool intel_460gx_dma_seal(Intel460GXDMA *dma, Error **errp)
{
    if (!dma || !dma->initialized) {
        error_setg(errp, "460GX DMA view is not initialized");
        return false;
    }

    dma->sealed = true;
    return true;
}

static bool dma_bus_is_empty(const PCIBus *bus)
{
    unsigned devfn;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        if (bus->devices[devfn]) {
            return false;
        }
    }

    return true;
}

static void dma_report_fault(Intel460GXDMARequester *requester,
                             hwaddr addr, unsigned size, bool is_write)
{
    Intel460GXDMA *dma = requester->dma;
    IA64ChipsetFault fault;

    if (!dma->fault_notify) {
        return;
    }
    fault = (IA64ChipsetFault) {
        .source = IA64_CHIPSET_FAULT_460GX,
        .reason = IA64_CHIPSET_FAULT_DECODE,
        .bus = pci_bus_num(dma->bus),
        .severity = IA64_RAS_SEVERITY_RECOVERABLE,
        .requester = PCI_BUILD_BDF(pci_bus_num(dma->bus),
                                   requester->devfn),
        .address = addr,
        .status = BIT(3),
        .information = (uint64_t)is_write << 8 | size,
    };
    dma->fault_notify(dma->fault_opaque, &fault);
}

static MemTxResult dma_fault_read(void *opaque, hwaddr addr,
                                  uint64_t *data, unsigned size,
                                  MemTxAttrs attrs)
{
    Intel460GXDMARequester *requester = opaque;

    *data = UINT64_MAX;
    dma_report_fault(requester, addr, size, false);
    return MEMTX_DECODE_ERROR;
}

static MemTxResult dma_fault_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size,
                                   MemTxAttrs attrs)
{
    Intel460GXDMARequester *requester = opaque;

    dma_report_fault(requester, addr, size, true);
    return MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps dma_fault_ops = {
    .read_with_attrs = dma_fault_read,
    .write_with_attrs = dma_fault_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static Intel460GXDMARequester *dma_requester_new(Intel460GXDMA *dma,
                                                  int devfn)
{
    Intel460GXDMARequester *requester;
    g_autofree char *name = NULL;
    size_t i;

    requester = g_new0(Intel460GXDMARequester, 1);
    requester->dma = dma;
    requester->devfn = devfn;
    requester->aliases = g_ptr_array_new();
    name = g_strdup_printf("intel-460gx-dma-%02x-container", devfn);
    memory_region_init(&requester->container, OBJECT(dma), name,
                       INTEL_460GX_DMA_ADDRESS_LIMIT);
    name = g_strdup_printf("intel-460gx-dma-%02x-fault", devfn);
    memory_region_init_io(&requester->fault, OBJECT(dma), &dma_fault_ops,
                          requester, name, INTEL_460GX_DMA_ADDRESS_LIMIT);
    memory_region_add_subregion_overlap(&requester->container, 0,
                                        &requester->fault, -1);

    for (i = 0; i < dma->aliases->len; i++) {
        Intel460GXDMAAlias *source = g_ptr_array_index(dma->aliases, i);
        Intel460GXDMARequesterAlias *alias =
            g_new0(Intel460GXDMARequesterAlias, 1);

        name = g_strdup_printf("intel-460gx-dma-%02x-alias-%zu", devfn, i);
        memory_region_init_alias(&alias->mr, OBJECT(dma), name,
                                 &source->mr, 0, source->size);
        memory_region_add_subregion(&requester->container, source->dma_base,
                                    &alias->mr);
        g_ptr_array_add(requester->aliases, alias);
    }

    name = g_strdup_printf("intel-460gx-dma-%02x", devfn);
    address_space_init(&requester->address_space, &requester->container,
                       name);
    return requester;
}

static void dma_requester_destroy(Intel460GXDMARequester *requester)
{
    size_t i;

    address_space_remove_listeners(&requester->address_space);
    address_space_destroy(&requester->address_space);
    for (i = 0; i < requester->aliases->len; i++) {
        Intel460GXDMARequesterAlias *alias =
            g_ptr_array_index(requester->aliases, i);

        memory_region_del_subregion(&requester->container, &alias->mr);
        object_unparent(OBJECT(&alias->mr));
        g_free(alias);
    }
    g_ptr_array_free(requester->aliases, true);
    memory_region_del_subregion(&requester->container, &requester->fault);
    object_unparent(OBJECT(&requester->fault));
    object_unparent(OBJECT(&requester->container));
    g_free(requester);
}

static AddressSpace *dma_get_address_space(PCIBus *bus, void *opaque,
                                           int devfn)
{
    Intel460GXDMA *dma = opaque;

    g_assert(bus == dma->bus);
    g_assert(devfn >= 0 && devfn < PCI_DEVFN_MAX);
    if (!dma->requesters[devfn]) {
        dma->requesters[devfn] = dma_requester_new(dma, devfn);
    }
    return &dma->requesters[devfn]->address_space;
}

static const PCIIOMMUOps dma_iommu_ops = {
    .get_address_space = dma_get_address_space,
};

bool intel_460gx_dma_attach_root(Intel460GXDMA *dma, PCIBus *bus,
                                Error **errp)
{
    if (!dma || !dma->initialized) {
        error_setg(errp, "460GX DMA view is not initialized");
        return false;
    }
    if (!bus) {
        error_setg(errp, "460GX DMA view has no PCI root bus");
        return false;
    }
    if (!dma->sealed) {
        error_setg(errp, "460GX DMA view must be sealed before attachment");
        return false;
    }
    if (dma->bus) {
        error_setg(errp, "460GX DMA view is already attached");
        return false;
    }
    if (!pci_bus_is_root(bus)) {
        error_setg(errp, "460GX DMA view can only attach to a PCI root bus");
        return false;
    }
    if (pci_bus_bypass_iommu(bus)) {
        error_setg(errp,
                   "460GX DMA view cannot attach to a PCI bus that bypasses IOMMU operations");
        return false;
    }
    if (bus->iommu_ops || bus->iommu_opaque) {
        error_setg(errp,
                   "PCI root bus already has DMA/IOMMU ownership state");
        return false;
    }
    if (!dma_bus_is_empty(bus)) {
        error_setg(errp,
                   "460GX DMA view must attach before PCI devices are realized");
        return false;
    }

    pci_setup_iommu(bus, &dma_iommu_ops, dma);
    object_ref(OBJECT(bus));
    dma->bus = bus;
    return true;
}

void intel_460gx_dma_set_fault_notify(Intel460GXDMA *dma,
                                      IA64ChipsetFaultNotify notify,
                                      void *opaque)
{
    g_return_if_fail(dma != NULL);
    dma->fault_notify = notify;
    dma->fault_opaque = opaque;
}

AddressSpace *intel_460gx_dma_address_space(Intel460GXDMA *dma)
{
    return dma && dma->initialized ? &dma->address_space : NULL;
}

uint64_t intel_460gx_dma_aperture_base(const Intel460GXDMA *dma)
{
    return dma ? dma->aperture_base : 0;
}

uint64_t intel_460gx_dma_aperture_size(const Intel460GXDMA *dma)
{
    return dma ? dma->aperture_size : 0;
}

size_t intel_460gx_dma_alias_count(const Intel460GXDMA *dma)
{
    return dma && dma->aliases ? dma->aliases->len : 0;
}

bool intel_460gx_dma_is_sealed(const Intel460GXDMA *dma)
{
    return dma && dma->sealed;
}

bool intel_460gx_dma_destroy(Intel460GXDMA *dma, Error **errp)
{
    size_t i;

    if (!dma) {
        return true;
    }

    if (dma->bus) {
        bool owns_ops = dma->bus->iommu_ops == &dma_iommu_ops;
        bool owns_opaque = dma->bus->iommu_opaque == dma;

        if (!dma_bus_is_empty(dma->bus)) {
            error_setg(errp,
                       "460GX DMA view cannot be destroyed while PCI devices remain realized");
            return false;
        }
        if (owns_ops != owns_opaque) {
            error_setg(errp,
                       "460GX DMA view has a partial PCI bus IOMMU ownership conflict");
            return false;
        }
        if (owns_ops) {
            dma->bus->iommu_ops = NULL;
            dma->bus->iommu_opaque = NULL;
        }
        object_unref(OBJECT(dma->bus));
        dma->bus = NULL;
    }

    for (i = 0; i < ARRAY_SIZE(dma->requesters); i++) {
        if (dma->requesters[i]) {
            dma_requester_destroy(dma->requesters[i]);
            dma->requesters[i] = NULL;
        }
    }

    for (i = 0; i < dma->aliases->len; i++) {
        Intel460GXDMAAlias *alias = g_ptr_array_index(dma->aliases, i);

        memory_region_del_subregion(&dma->container, &alias->mr);
        object_unparent(OBJECT(&alias->mr));
        g_free(alias);
    }
    g_ptr_array_set_size(dma->aliases, 0);

    address_space_remove_listeners(&dma->address_space);
    address_space_destroy(&dma->address_space);
    object_unparent(OBJECT(&dma->container));
    dma->initialized = false;
    object_unref(OBJECT(dma));
    return true;
}

static void intel_460gx_dma_init(Object *obj)
{
    Intel460GXDMA *dma = INTEL_460GX_DMA(obj);

    dma->aliases = g_ptr_array_new();
}

static void intel_460gx_dma_finalize(Object *obj)
{
    Intel460GXDMA *dma = INTEL_460GX_DMA(obj);

    g_assert(!dma->initialized);
    g_assert(!dma->bus);
    g_assert(dma->aliases->len == 0);
    g_ptr_array_free(dma->aliases, true);
}

static void intel_460gx_dma_class_init(ObjectClass *klass, const void *data)
{
    (void)klass;
    (void)data;
}
