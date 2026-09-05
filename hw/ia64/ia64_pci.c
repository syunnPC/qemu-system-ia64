/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 PCI and PCI Express host bridges.
 * Provides a single root bus with ECAM, MMIO, and sparse I/O windows.
 */

#include "qemu/osdep.h"
#include "hw/ia64/ia64_pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/msi.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "hw/core/irq.h"
#include "qom/object.h"

#define TYPE_IA64_PCI_ROOT_BUS  "ia64-pci-root-bus"
#define TYPE_IA64_PCIE_ROOT_BUS "ia64-pcie-root-bus"

typedef struct IA64PCIRootBus {
    PCIBus parent_obj;
    uint8_t first_bus;
} IA64PCIRootBus;

struct IA64PCIState {
    PCIExpressHost parent_obj;

    MemoryRegion pci_mmio;
    MemoryRegion pci_mmio_window;
    MemoryRegion pci_io;
    MemoryRegion pci_io_sparse;
    MemoryRegion ecam_window;
    AddressSpace pci_io_as;
    qemu_irq irq[IA64_PCI_INTX_LINES];
    IA64PCIHostConfig config;
    char root_bus_name[24];
    char root_bus_path[16];
    bool pcie;
    bool system_regions_mapped;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
};

static hwaddr ia64_pci_sparse_io_port(const IA64PCIState *s,
                                      hwaddr encoded)
{
    hwaddr group = encoded >> 12;
    hwaddr low = encoded & 0xfff;

    return s->config.io_bus_base + (group << 2) + (low & 3);
}

static uint64_t ia64_pci_sparse_io_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    IA64PCIState *s = opaque;
    hwaddr port = ia64_pci_sparse_io_port(s, addr);

    if (port < s->config.io_bus_base ||
        port - s->config.io_bus_base >= s->config.io_size ||
        size > s->config.io_size - (port - s->config.io_bus_base)) {
        return ~0ULL;
    }

    switch (size) {
    case 1:
        return address_space_ldub(&s->pci_io_as, port,
                                  MEMTXATTRS_UNSPECIFIED, NULL);
    case 2:
        return address_space_lduw_le(&s->pci_io_as, port,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
    case 4:
        return address_space_ldl_le(&s->pci_io_as, port,
                                    MEMTXATTRS_UNSPECIFIED, NULL);
    default:
        return ~0ULL;
    }
}

static void ia64_pci_sparse_io_write(void *opaque, hwaddr addr, uint64_t data,
                                     unsigned size)
{
    IA64PCIState *s = opaque;
    hwaddr port = ia64_pci_sparse_io_port(s, addr);

    if (port < s->config.io_bus_base ||
        port - s->config.io_bus_base >= s->config.io_size ||
        size > s->config.io_size - (port - s->config.io_bus_base)) {
        return;
    }

    switch (size) {
    case 1:
        address_space_stb(&s->pci_io_as, port, data,
                          MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        address_space_stw_le(&s->pci_io_as, port, data,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        address_space_stl_le(&s->pci_io_as, port, data,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ia64_pci_sparse_io_ops = {
    .read = ia64_pci_sparse_io_read,
    .write = ia64_pci_sparse_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static int ia64_pci_route_intx_output(uint8_t devfn, int irq_num)
{
    return (PCI_SLOT(devfn) + irq_num) % IA64_PCI_INTX_LINES;
}

int ia64_pci_route_intx_gsi(uint8_t devfn, int irq_num)
{
    return IA64_PCI_INTX_GSI_BASE + ia64_pci_route_intx_output(devfn, irq_num);
}

int ia64_pci_host_route_intx_gsi(const IA64PCIState *host,
                                 uint8_t devfn, int irq_num)
{
    return host->config.gsi_base +
        ia64_pci_route_intx_output(devfn, irq_num);
}

static int ia64_pci_map_irq(PCIDevice *d, int irq_num)
{
    return ia64_pci_route_intx_output(d->devfn, irq_num);
}

static void ia64_pci_set_irq(void *opaque, int irq_num, int level)
{
    IA64PCIState *s = opaque;

    if (irq_num < IA64_PCI_INTX_LINES) {
        qemu_set_irq(s->irq[irq_num], level);
    }
}

static int ia64_pci_root_bus_num(PCIBus *bus)
{
    return ((IA64PCIRootBus *)bus)->first_bus;
}

static void ia64_pci_root_bus_class_init(ObjectClass *klass, const void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(klass);

    (void)data;
    pbc->bus_num = ia64_pci_root_bus_num;
}

static bool ia64_pci_host_config_valid(const IA64PCIHostConfig *config,
                                       Error **errp)
{
    uint64_t bus_count;
    uint64_t config_offset;

    if (config->last_bus < config->first_bus) {
        error_setg(errp, "IA-64 PCI host has an inverted bus range");
        return false;
    }
    bus_count = (uint64_t)config->last_bus - config->first_bus + 1;
    if (config->ecam_size != bus_count * PCIE_MMCFG_SIZE_MIN) {
        error_setg(errp,
                   "IA-64 PCI host ECAM size does not match its bus range");
        return false;
    }
    config_offset = (uint64_t)config->first_bus * PCIE_MMCFG_SIZE_MIN;
    if ((config->ecam_base & (PCIE_MMCFG_SIZE_MIN - 1)) ||
        config->ecam_base > UINT64_MAX - config_offset ||
        config->ecam_base + config_offset >
            UINT64_MAX - config->ecam_size) {
        error_setg(errp, "IA-64 PCI host ECAM range is invalid");
        return false;
    }
    if (!config->mmio_size ||
        config->mmio_cpu_base > UINT64_MAX - config->mmio_size ||
        config->mmio_bus_base > UINT64_MAX - config->mmio_size) {
        error_setg(errp, "IA-64 PCI host MMIO range is invalid");
        return false;
    }
    if (!config->io_size || config->io_size > UINT64_MAX >> 10 ||
        config->io_cpu_base > UINT64_MAX - (config->io_size << 10) ||
        config->io_bus_base > UINT64_MAX - config->io_size) {
        error_setg(errp, "IA-64 PCI host sparse I/O range is invalid");
        return false;
    }
    if (config->gsi_base > UINT32_MAX - IA64_PCI_INTX_LINES) {
        error_setg(errp, "IA-64 PCI host GSI range is invalid");
        return false;
    }
    return true;
}

bool ia64_pci_host_configure(IA64PCIState *host,
                             const IA64PCIHostConfig *config,
                             Error **errp)
{
    if (!host || !config) {
        error_setg(errp, "IA-64 PCI host configuration is required");
        return false;
    }
    if (qdev_is_realized(DEVICE(host))) {
        error_setg(errp,
                   "IA-64 PCI host configuration must precede realization");
        return false;
    }
    if (!ia64_pci_host_config_valid(config, errp)) {
        return false;
    }
    host->config = *config;
    return true;
}

bool ia64_pcie_host_set_fault_notifier(IA64PCIState *host,
                                       IA64ChipsetFaultNotify notify,
                                       void *opaque, Error **errp)
{
    if (!host || !notify) {
        error_setg(errp, "IA-64 PCIe fault setup requires a host and callback");
        return false;
    }
    if (qdev_is_realized(DEVICE(host))) {
        error_setg(errp, "IA-64 PCIe fault setup must precede host realization");
        return false;
    }
    if (host->fault_notify) {
        error_setg(errp, "IA-64 PCIe fault callback is already configured");
        return false;
    }
    host->fault_notify = notify;
    host->fault_opaque = opaque;
    return true;
}

static void ia64_pcie_root_port_aer_notify(PCIDevice *dev,
                                            const PCIEAERMsg *msg)
{
    PCIBus *root = pci_device_root_bus(dev);
    IA64PCIState *host = (IA64PCIState *)object_dynamic_cast(
        OBJECT(BUS(root)->parent), TYPE_IA64_PCIE_HOST_BRIDGE);
    IA64ChipsetFault fault;
    uint16_t pcie_flags;
    uint32_t slot_cap;

    if (!host || !host->fault_notify) {
        return;
    }
    /*
     * Root Control enables platform System Error notifications independently
     * of Root Error Command, which controls native AER interrupts.  The
     * correctable/nonfatal/fatal enable bits have the same encoding as the
     * corresponding message severity bits.  AER status is logged either way.
     */
    if (!(pci_get_word(dev->config + dev->exp.exp_cap + PCI_EXP_RTCTL) &
          msg->severity)) {
        return;
    }
    fault = (IA64ChipsetFault) {
        .source = IA64_CHIPSET_FAULT_PCIE,
        .reason = IA64_CHIPSET_FAULT_AER,
        .segment = host->config.segment,
        .bus = msg->source_id >> 8,
        .severity = msg->severity == PCI_ERR_ROOT_CMD_COR_EN ?
            IA64_RAS_SEVERITY_CORRECTED :
            msg->severity == PCI_ERR_ROOT_CMD_FATAL_EN ?
            IA64_RAS_SEVERITY_FATAL : IA64_RAS_SEVERITY_RECOVERABLE,
        .requester = msg->source_id,
        .status = msg->severity,
        .information = msg->severity,
    };
    pcie_flags = pci_get_word(dev->config + dev->exp.exp_cap +
                              PCI_EXP_FLAGS);
    slot_cap = pci_get_long(dev->config + dev->exp.exp_cap +
                            PCI_EXP_SLTCAP);
    fault.pcie.valid = true;
    fault.pcie.port_type =
        (pcie_flags & PCI_EXP_FLAGS_TYPE) >> PCI_EXP_FLAGS_TYPE_SHIFT;
    fault.pcie.version = (pcie_flags & PCI_EXP_FLAGS_VERS) << 8;
    fault.pcie.command_status = pci_get_long(dev->config + PCI_COMMAND);
    pci_set_word(&fault.pcie.device_id[0],
                 pci_get_word(dev->config + PCI_VENDOR_ID));
    pci_set_word(&fault.pcie.device_id[2],
                 pci_get_word(dev->config + PCI_DEVICE_ID));
    memcpy(&fault.pcie.device_id[4], dev->config + PCI_CLASS_PROG, 3);
    fault.pcie.device_id[7] = PCI_FUNC(dev->devfn);
    fault.pcie.device_id[8] = PCI_SLOT(dev->devfn);
    pci_set_word(&fault.pcie.device_id[9], host->config.segment);
    fault.pcie.device_id[11] = dev->config[PCI_PRIMARY_BUS];
    fault.pcie.device_id[12] = dev->config[PCI_SECONDARY_BUS];
    pci_set_word(&fault.pcie.device_id[13],
                 ((slot_cap & PCI_EXP_SLTCAP_PSN) >>
                  PCI_EXP_SLTCAP_PSN_SHIFT) << 3);
    fault.pcie.bridge_control_status =
        pci_get_word(dev->config + PCI_SEC_STATUS) |
        (uint32_t)pci_get_word(dev->config + PCI_BRIDGE_CONTROL) << 16;
    memcpy(fault.pcie.capability, dev->config + dev->exp.exp_cap,
           sizeof(fault.pcie.capability));
    fault.pcie.capability[1] = 0;
    memcpy(fault.pcie.aer, dev->config + dev->exp.aer_cap,
           sizeof(fault.pcie.aer));
    host->fault_notify(host->fault_opaque, &fault);
}

static void ia64_pcie_root_port_class_init(ObjectClass *klass,
                                            const void *data)
{
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    (void)data;
    pc->aer_notify = ia64_pcie_root_port_aer_notify;
}

static const TypeInfo ia64_pcie_root_port_info = {
    .name = TYPE_IA64_PCIE_ROOT_PORT,
    .parent = "pcie-root-port",
    .class_init = ia64_pcie_root_port_class_init,
};

void ia64_pci_host_get_config(const IA64PCIState *host,
                              IA64PCIHostConfig *config)
{
    g_return_if_fail(host != NULL);
    g_return_if_fail(config != NULL);
    *config = host->config;
}

static void ia64_pci_realize(DeviceState *dev, Error **errp)
{
    IA64PCIState *s = IA64_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);
    uint64_t ecam_offset;

    if (!ia64_pci_host_config_valid(&s->config, errp)) {
        return;
    }

    /*
     * PCI BAR values on this platform are identity-mapped CPU physical
     * addresses.  Keep the PCI bus address space large enough to contain the
     * advertised low MMIO window at its bus address, then expose only that
     * window into system memory.
     */
    memory_region_init(&s->pci_mmio, OBJECT(dev), "pci-mmio",
                       s->config.mmio_bus_base + s->config.mmio_size);
    memory_region_init_alias(&s->pci_mmio_window, OBJECT(dev),
                             "pci-mmio-window", &s->pci_mmio,
                             s->config.mmio_bus_base,
                             s->config.mmio_size);
    memory_region_init(&s->pci_io, OBJECT(dev), "pci-io",
                       s->config.io_bus_base + s->config.io_size);
    address_space_init(&s->pci_io_as, &s->pci_io, "ia64-pci-io");
    memory_region_init_io(&s->pci_io_sparse, OBJECT(dev),
                          &ia64_pci_sparse_io_ops, s, "pci-io-sparse",
                          s->config.io_size << 10);
    /*
     * A port device may issue another port transaction while servicing a
     * request.  The sparse bridge only translates addresses and therefore
     * permits that nested transaction to reach the independent port bus.
     */
    s->pci_io_sparse.disable_reentrancy_guard = true;
    qdev_init_gpio_out(dev, s->irq, IA64_PCI_INTX_LINES);

    if (s->config.segment == 0 && s->config.first_bus == 0) {
        snprintf(s->root_bus_name, sizeof(s->root_bus_name), "pci");
    } else {
        snprintf(s->root_bus_name, sizeof(s->root_bus_name), "pci.%04x.%02x",
                 s->config.segment, s->config.first_bus);
    }
    snprintf(s->root_bus_path, sizeof(s->root_bus_path), "%04x:%02x",
             s->config.segment, s->config.first_bus);
    phb->bus = pci_register_root_bus(dev, s->root_bus_name,
                                     ia64_pci_set_irq, ia64_pci_map_irq, s,
                                     &s->pci_mmio, &s->pci_io,
                                     PCI_DEVFN(0, 0), 4,
                                     s->pcie ? TYPE_IA64_PCIE_ROOT_BUS :
                                               TYPE_IA64_PCI_ROOT_BUS);
    ((IA64PCIRootBus *)phb->bus)->first_bus = s->config.first_bus;
    if (s->pcie) {
        phb->bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
        msi_nonbroken = true;
    }

    memory_region_add_subregion_overlap(get_system_memory(),
                                        s->config.mmio_cpu_base,
                                        &s->pci_mmio_window, 1);
    memory_region_add_subregion(get_system_memory(), s->config.io_cpu_base,
                                &s->pci_io_sparse);
    pcie_host_mmcfg_init(pex, PCIE_MMCFG_SIZE_MAX);
    ecam_offset = (uint64_t)s->config.first_bus * PCIE_MMCFG_SIZE_MIN;
    memory_region_init_alias(&s->ecam_window, OBJECT(dev), "pci-ecam-window",
                             &pex->mmio, ecam_offset, s->config.ecam_size);
    memory_region_add_subregion(get_system_memory(),
                                s->config.ecam_base + ecam_offset,
                                &s->ecam_window);
    s->system_regions_mapped = true;
}

static void ia64_pci_unrealize(DeviceState *dev)
{
    IA64PCIState *s = IA64_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);

    if (s->system_regions_mapped) {
        memory_region_del_subregion(get_system_memory(), &s->ecam_window);
        memory_region_del_subregion(get_system_memory(),
                                    &s->pci_io_sparse);
        memory_region_del_subregion(get_system_memory(),
                                    &s->pci_mmio_window);
        s->system_regions_mapped = false;
    }
    if (phb->bus) {
        pci_unregister_root_bus(phb->bus);
        phb->bus = NULL;
    }
    address_space_destroy(&s->pci_io_as);
}

static const char *ia64_pci_root_bus_path(PCIHostState *host,
                                          PCIBus *root_bus)
{
    IA64PCIState *s = IA64_PCI_HOST_BRIDGE(host);

    g_assert(root_bus == host->bus);
    return s->root_bus_path;
}

static const Property ia64_pci_properties[] = {
    DEFINE_PROP_UINT16(IA64_PCI_HOST_PROP_SEGMENT, IA64PCIState,
                       config.segment, 0),
    DEFINE_PROP_UINT8(IA64_PCI_HOST_PROP_FIRST_BUS, IA64PCIState,
                      config.first_bus, 0),
    DEFINE_PROP_UINT8(IA64_PCI_HOST_PROP_LAST_BUS, IA64PCIState,
                      config.last_bus, UINT8_MAX),
    DEFINE_PROP_UINT64(IA64_PCI_HOST_PROP_ECAM_BASE, IA64PCIState,
                       config.ecam_base, IA64_PCI_CONFIG_BASE),
    DEFINE_PROP_SIZE(IA64_PCI_HOST_PROP_ECAM_SIZE, IA64PCIState,
                     config.ecam_size, IA64_PCI_CONFIG_SIZE),
    DEFINE_PROP_UINT64(IA64_PCI_HOST_PROP_MMIO_CPU_BASE, IA64PCIState,
                       config.mmio_cpu_base, IA64_PCI_MMIO_BASE),
    DEFINE_PROP_UINT64(IA64_PCI_HOST_PROP_MMIO_BUS_BASE, IA64PCIState,
                       config.mmio_bus_base, IA64_PCI_MMIO_BASE),
    DEFINE_PROP_SIZE(IA64_PCI_HOST_PROP_MMIO_SIZE, IA64PCIState,
                     config.mmio_size, IA64_PCI_MMIO_SIZE),
    DEFINE_PROP_UINT64(IA64_PCI_HOST_PROP_IO_CPU_BASE, IA64PCIState,
                       config.io_cpu_base, IA64_PCI_IO_BASE),
    DEFINE_PROP_UINT64(IA64_PCI_HOST_PROP_IO_BUS_BASE, IA64PCIState,
                       config.io_bus_base, 0),
    DEFINE_PROP_SIZE(IA64_PCI_HOST_PROP_IO_SIZE, IA64PCIState,
                     config.io_size, IA64_PCI_IO_SIZE),
    DEFINE_PROP_UINT32(IA64_PCI_HOST_PROP_GSI_BASE, IA64PCIState,
                       config.gsi_base, IA64_PCI_INTX_GSI_BASE),
};

static bool ia64_pci_get_iommu_attached(Object *obj, Error **errp)
{
    PCIBus *bus = ia64_pci_host_bus(IA64_PCI_HOST_BRIDGE(obj));

    (void)errp;
    return bus && bus->iommu_ops != NULL;
}

static bool ia64_pci_get_iommu_per_bus(Object *obj, Error **errp)
{
    PCIBus *bus = ia64_pci_host_bus(IA64_PCI_HOST_BRIDGE(obj));

    (void)errp;
    return bus && bus->iommu_per_bus;
}

static void ia64_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    dc->realize = ia64_pci_realize;
    dc->unrealize = ia64_pci_unrealize;
    device_class_set_props(dc, ia64_pci_properties);
    hc->root_bus_path = ia64_pci_root_bus_path;
    object_class_property_add_bool(klass, "iommu-attached",
                                   ia64_pci_get_iommu_attached, NULL);
    object_class_property_add_bool(klass, "iommu-per-bus",
                                   ia64_pci_get_iommu_per_bus, NULL);
}

static const TypeInfo ia64_pci_info = {
    .name          = TYPE_IA64_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(IA64PCIState),
    .class_init    = ia64_pci_class_init,
};

static const TypeInfo ia64_pci_root_bus_info = {
    .name          = TYPE_IA64_PCI_ROOT_BUS,
    .parent        = TYPE_PCI_BUS,
    .instance_size = sizeof(IA64PCIRootBus),
    .class_init    = ia64_pci_root_bus_class_init,
};

static const TypeInfo ia64_pcie_root_bus_info = {
    .name          = TYPE_IA64_PCIE_ROOT_BUS,
    .parent        = TYPE_PCIE_BUS,
    .instance_size = sizeof(IA64PCIRootBus),
    .class_init    = ia64_pci_root_bus_class_init,
};

static void ia64_pcie_init(Object *obj)
{
    IA64_PCI_HOST_BRIDGE(obj)->pcie = true;
}

static void ia64_pcie_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "IA-64 PCI Express host bridge";
}

static const TypeInfo ia64_pcie_info = {
    .name          = TYPE_IA64_PCIE_HOST_BRIDGE,
    .parent        = TYPE_IA64_PCI_HOST_BRIDGE,
    .instance_init = ia64_pcie_init,
    .class_init    = ia64_pcie_class_init,
};

PCIBus *ia64_pci_host_bus(IA64PCIState *host)
{
    return host ? PCI_HOST_BRIDGE(host)->bus : NULL;
}

MemoryRegion *ia64_pci_host_memory(IA64PCIState *host)
{
    return host ? &host->pci_mmio : NULL;
}

MemoryRegion *ia64_pci_host_io(IA64PCIState *host)
{
    return host ? &host->pci_io : NULL;
}

static bool ia64_pci_bus_empty(PCIBus *bus)
{
    unsigned int devfn;

    for (devfn = 0; devfn < PCI_DEVFN_MAX; devfn++) {
        if (bus->devices[devfn]) {
            return false;
        }
    }
    return true;
}

bool ia64_pci_host_attach_iommu(IA64PCIState *host,
                                const PCIIOMMUOps *ops, void *opaque,
                                Error **errp)
{
    PCIBus *bus = ia64_pci_host_bus(host);

    if (!bus || !qdev_is_realized(DEVICE(host))) {
        error_setg(errp, "IA-64 PCI host must be realized before IOMMU attachment");
        return false;
    }
    if (!ops || !ops->get_address_space) {
        error_setg(errp, "IA-64 PCI host requires complete IOMMU operations");
        return false;
    }
    if (!ia64_pci_bus_empty(bus)) {
        error_setg(errp,
                   "IA-64 PCI host requires IOMMU attachment before devices");
        return false;
    }
    if (bus->iommu_ops || bus->iommu_opaque) {
        error_setg(errp, "IA-64 PCI root bus already has an IOMMU");
        return false;
    }
    if (pci_bus_is_express(bus)) {
        pci_setup_iommu_per_bus(bus, ops, opaque);
    } else {
        pci_setup_iommu(bus, ops, opaque);
    }
    return true;
}

bool ia64_pci_host_detach_iommu(IA64PCIState *host,
                                const PCIIOMMUOps *ops, void *opaque,
                                Error **errp)
{
    PCIBus *bus = ia64_pci_host_bus(host);

    if (!bus || bus->iommu_ops != ops || bus->iommu_opaque != opaque) {
        error_setg(errp, "IA-64 PCI root bus does not own the selected IOMMU");
        return false;
    }
    if (!ia64_pci_bus_empty(bus)) {
        error_setg(errp,
                   "IA-64 PCI host cannot detach its IOMMU while devices remain");
        return false;
    }
    bus->iommu_ops = NULL;
    bus->iommu_opaque = NULL;
    bus->iommu_per_bus = false;
    return true;
}

static void ia64_pci_register_types(void)
{
    type_register_static(&ia64_pci_root_bus_info);
    type_register_static(&ia64_pcie_root_bus_info);
    type_register_static(&ia64_pci_info);
    type_register_static(&ia64_pcie_info);
    type_register_static(&ia64_pcie_root_port_info);
}
type_init(ia64_pci_register_types)
