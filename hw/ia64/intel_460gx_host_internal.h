/*
 * Intel 460GX configuration routing engine -- internal definitions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_HOST_INTERNAL_H
#define HW_IA64_INTEL_460GX_HOST_INTERNAL_H

#include "hw/ia64/intel_460gx_host.h"

#define INTEL_460GX_CONFIG_ADDRESS_MASK UINT32_C(0x80fffffc)
#define INTEL_460GX_CONFIG_ENABLE       UINT32_C(0x80000000)
#define INTEL_460GX_CONFIG_SPACE_SIZE   256

typedef struct Intel460GXConfigTarget {
    const Intel460GXConfigTargetOps *ops;
    void *opaque;
} Intel460GXConfigTarget;

typedef struct Intel460GXDownstreamRoute {
    PCIBus *bus;
    uint8_t first_bus;
    uint8_t last_bus;
    uint8_t reset_first_bus;
    uint8_t reset_last_bus;
    bool attached;
} Intel460GXDownstreamRoute;

typedef struct Intel460GXHostCore {
    uint32_t config_address;
    uint32_t chipset_present;
    uint8_t cbn;

    uint8_t reset_cbn;
    uint32_t reset_chipset_present;

    Intel460GXConfigTarget bootstrap_sac[8];
    Intel460GXConfigTarget chipset[32][8];
    Intel460GXDownstreamRoute downstream[INTEL_460GX_DOWNSTREAM_PORTS];
} Intel460GXHostCore;

bool intel_460gx_chipset_device_valid(unsigned device);

void intel_460gx_host_core_init(Intel460GXHostCore *core, uint8_t cbn,
                                uint32_t chipset_present);
void intel_460gx_host_core_reset(Intel460GXHostCore *core);
uint32_t intel_460gx_host_core_address_read(const Intel460GXHostCore *core,
                                            unsigned size);
void intel_460gx_host_core_address_write(Intel460GXHostCore *core,
                                         uint32_t value, unsigned size);
uint32_t intel_460gx_host_core_data_read(Intel460GXHostCore *core,
                                         unsigned data_offset,
                                         unsigned size);
void intel_460gx_host_core_data_write(Intel460GXHostCore *core,
                                      unsigned data_offset, uint32_t value,
                                      unsigned size);

bool intel_460gx_host_core_register_bootstrap(
    Intel460GXHostCore *core, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp);
bool intel_460gx_host_core_register_chipset(
    Intel460GXHostCore *core, unsigned device, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp);
bool intel_460gx_host_core_apply_decoded_update(
    Intel460GXHostCore *core,
    const Intel460GXDecodedStateUpdate *update, Error **errp);
bool intel_460gx_host_core_set_present(Intel460GXHostCore *core,
                                       unsigned device, bool present,
                                       Error **errp);
bool intel_460gx_host_core_attach_downstream(
    Intel460GXHostCore *core, unsigned port, PCIBus *bus,
    uint8_t first_bus, uint8_t last_bus, Error **errp);
bool intel_460gx_host_core_set_downstream_range(
    Intel460GXHostCore *core, unsigned port,
    uint8_t first_bus, uint8_t last_bus, Error **errp);
bool intel_460gx_host_core_validate_downstream(
    const Intel460GXHostCore *core, Error **errp);

#endif
