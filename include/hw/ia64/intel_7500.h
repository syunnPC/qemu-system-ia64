/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_IA64_INTEL_7500_H
#define HW_IA64_INTEL_7500_H

#include "hw/core/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/ia64/intel_7500_abi.h"

#define TYPE_INTEL_7500_IOMMU "intel-7500-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(Intel7500State, INTEL_7500_IOMMU)

/* Install before realizing endpoints; secondary bridge is at root slot 7. */
bool intel_7500_create(Object *parent, PCIBus *bus, hwaddr base,
                       bool coherent, Error **errp);
#endif
