/*
 * Intel 82466GX Integrated Hot-Plug Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_IHPC_H
#define HW_IA64_INTEL_460GX_IHPC_H

#include "hw/ia64/ia64_ras.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"

#define TYPE_INTEL_82466GX_IHPC "intel-82466gx-ihpc"
OBJECT_DECLARE_SIMPLE_TYPE(Intel82466GXIHPCState, INTEL_82466GX_IHPC)

#define INTEL_82466GX_IHPC_MAX_SLOTS 6U

#define INTEL_82466GX_IHPC_PROP_FIRST_SLOT "first-slot"
#define INTEL_82466GX_IHPC_PROP_SLOT_COUNT "slot-count"

#define INTEL_82466GX_IHPC_GPIO_SLOT_PRESENT "slot-present"
#define INTEL_82466GX_IHPC_GPIO_SLOT_SWITCH "slot-switch"
#define INTEL_82466GX_IHPC_GPIO_POWER_FAULT "power-fault"

void intel_82466gx_ihpc_set_fault_notify(
    Intel82466GXIHPCState *s, IA64ChipsetFaultNotify notify, void *opaque);

#endif /* HW_IA64_INTEL_460GX_IHPC_H */
