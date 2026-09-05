/*
 * Intel 460GX configuration host core
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_HOST_H
#define HW_IA64_INTEL_460GX_HOST_H

#include "hw/core/sysbus.h"

#define TYPE_INTEL_460GX_HOST "intel-460gx-host-core"
OBJECT_DECLARE_SIMPLE_TYPE(Intel460GXHostState, INTEL_460GX_HOST)

#define INTEL_460GX_BOOTSTRAP_SAC_DEVICE 0x10
#define INTEL_460GX_DOWNSTREAM_PORTS 8

typedef struct PCIBus PCIBus;

/* Configuration-space targets are supplied through these callbacks. */
typedef struct Intel460GXConfigTargetOps {
    uint32_t (*read)(void *opaque, uint16_t offset, unsigned size);
    void (*write)(void *opaque, uint16_t offset, uint32_t value,
                  unsigned size);
} Intel460GXConfigTargetOps;

typedef struct Intel460GXBusRange {
    uint8_t first_bus;
    uint8_t last_bus;
} Intel460GXBusRange;

/*
 * One decoded SAC/xXB register transaction.  Fields without their has_* flag
 * and routes without their route_mask bit are left unchanged.  Applying an
 * update validates the complete candidate state and either commits every
 * requested field or leaves the host unchanged.
 */
typedef struct Intel460GXDecodedStateUpdate {
    bool has_cbn;
    uint8_t cbn;
    bool has_chipset_present;
    uint32_t chipset_present;
    uint8_t route_mask;
    Intel460GXBusRange routes[INTEL_460GX_DOWNSTREAM_PORTS];
} Intel460GXDecodedStateUpdate;

uint32_t intel_460gx_chipset_device_mask(void);

MemoryRegion *intel_460gx_host_conf_region(Intel460GXHostState *s);
MemoryRegion *intel_460gx_host_data_region(Intel460GXHostState *s);

bool intel_460gx_host_apply_decoded_update(
    Intel460GXHostState *s, const Intel460GXDecodedStateUpdate *update,
    Error **errp);

bool intel_460gx_host_register_bootstrap_sac(
    Intel460GXHostState *s, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp);
bool intel_460gx_host_register_chipset_target(
    Intel460GXHostState *s, unsigned device, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp);

/* CBN and chipset-present are decoded state supplied through these setters. */
void intel_460gx_host_set_cbn(Intel460GXHostState *s, uint8_t cbn);
uint8_t intel_460gx_host_get_cbn(const Intel460GXHostState *s);
bool intel_460gx_host_set_chipset_present(Intel460GXHostState *s,
                                           unsigned device, bool present,
                                           Error **errp);
bool intel_460gx_host_configure_chipset_present(Intel460GXHostState *s,
                                                 uint32_t present,
                                                 Error **errp);
uint32_t intel_460gx_host_get_chipset_present(
    const Intel460GXHostState *s);

/*
 * Attach one of eight xXB bus positions (CBN devices 10h..17h).  The supplied
 * bus range is its reset baseline.
 */
bool intel_460gx_host_attach_downstream_bus(Intel460GXHostState *s,
                                             unsigned port, PCIBus *bus,
                                             uint8_t first_bus,
                                             uint8_t last_bus,
                                             Error **errp);

/* Update an attached xXB from its decoded bus-number state. */
bool intel_460gx_host_set_downstream_bus_range(Intel460GXHostState *s,
                                                unsigned port,
                                                uint8_t first_bus,
                                                uint8_t last_bus,
                                                Error **errp);

#endif
