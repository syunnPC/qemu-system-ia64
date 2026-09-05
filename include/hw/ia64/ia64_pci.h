/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_PCI_H
#define HW_IA64_PCI_H

#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/ia64/ia64_ras.h"
#include "hw/pci/pci.h"
#include "qom/object.h"

#define TYPE_IA64_PCI_HOST_BRIDGE "ia64-pcihost"
#define TYPE_IA64_PCIE_HOST_BRIDGE "ia64-pciehost"
#define TYPE_IA64_PCIE_ROOT_PORT "ia64-pcie-root-port"
OBJECT_DECLARE_SIMPLE_TYPE(IA64PCIState, IA64_PCI_HOST_BRIDGE)

#define IA64_PCI_IO_BASE          IA64_LEGACY_IO_BASE
#define IA64_PCI_IO_SIZE          IA64_LEGACY_IO_PORTS_SIZE
#define IA64_PCI_IO_SPARSE_SIZE   IA64_LEGACY_IO_BLOCK_SIZE
#define IA64_PCI_CONFIG_BASE      0x0000007ff0000000ULL
#define IA64_PCI_CONFIG_SIZE      0x0000000010000000ULL

#if (IA64_PCI_IO_BASE & (IA64_PCI_IO_SPARSE_SIZE - 1)) != 0
#error "IA64_PCI_IO_BASE must be aligned for sparse I/O port addresses"
#endif

#define IA64_PCI_INTX_GSI_BASE 16
#define IA64_PCI_INTX_LINES    4

#define IA64_PCI_HOST_PROP_SEGMENT       "segment"
#define IA64_PCI_HOST_PROP_FIRST_BUS     "first-bus"
#define IA64_PCI_HOST_PROP_LAST_BUS      "last-bus"
#define IA64_PCI_HOST_PROP_ECAM_BASE     "ecam-base"
#define IA64_PCI_HOST_PROP_ECAM_SIZE     "ecam-size"
#define IA64_PCI_HOST_PROP_MMIO_CPU_BASE "mmio-cpu-base"
#define IA64_PCI_HOST_PROP_MMIO_BUS_BASE "mmio-bus-base"
#define IA64_PCI_HOST_PROP_MMIO_SIZE     "mmio-size"
#define IA64_PCI_HOST_PROP_IO_CPU_BASE   "io-cpu-base"
#define IA64_PCI_HOST_PROP_IO_BUS_BASE   "io-bus-base"
#define IA64_PCI_HOST_PROP_IO_SIZE       "io-size"
#define IA64_PCI_HOST_PROP_GSI_BASE      "gsi-base"

typedef struct IA64PCIHostConfig {
    uint16_t segment;
    uint8_t first_bus;
    uint8_t last_bus;
    uint64_t ecam_base;
    uint64_t ecam_size;
    uint64_t mmio_cpu_base;
    uint64_t mmio_bus_base;
    uint64_t mmio_size;
    uint64_t io_cpu_base;
    uint64_t io_bus_base;
    uint64_t io_size;
    uint32_t gsi_base;
} IA64PCIHostConfig;

int ia64_pci_route_intx_gsi(uint8_t devfn, int irq_num);
int ia64_pci_host_route_intx_gsi(const IA64PCIState *host,
                                 uint8_t devfn, int irq_num);
PCIBus *ia64_pci_host_bus(IA64PCIState *host);
MemoryRegion *ia64_pci_host_memory(IA64PCIState *host);
MemoryRegion *ia64_pci_host_io(IA64PCIState *host);
bool ia64_pci_host_configure(IA64PCIState *host,
                             const IA64PCIHostConfig *config,
                             Error **errp);
bool ia64_pcie_host_set_fault_notifier(IA64PCIState *host,
                                       IA64ChipsetFaultNotify notify,
                                       void *opaque, Error **errp);
void ia64_pci_host_get_config(const IA64PCIState *host,
                              IA64PCIHostConfig *config);
bool ia64_pci_host_attach_iommu(IA64PCIState *host,
                                const PCIIOMMUOps *ops, void *opaque,
                                Error **errp);
bool ia64_pci_host_detach_iommu(IA64PCIState *host,
                                const PCIIOMMUOps *ops, void *opaque,
                                Error **errp);

#endif
