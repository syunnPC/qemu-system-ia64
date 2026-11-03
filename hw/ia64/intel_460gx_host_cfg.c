/*
 * Intel 460GX PCI configuration routing engine
 *
 * Implements PCI configuration mechanism #1, the bus-0/device-10h bootstrap
 * target, and programmable bus routing.  CBN and chipset-presence state enter
 * through the decoded-state API.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_host_internal.h"
#include "hw/pci/pci_host.h"
#include "qapi/error.h"

#define INTEL_460GX_BUS(value)      extract32((value), 16, 8)
#define INTEL_460GX_DEVICE(value)   extract32((value), 11, 5)
#define INTEL_460GX_FUNCTION(value) extract32((value), 8, 3)
#define INTEL_460GX_REGISTER(value) extract32((value), 0, 8)

/* Unattached routes use a reversed bus range. */
#define INTEL_460GX_NO_BUS_FIRST 1
#define INTEL_460GX_NO_BUS_LAST  0

static uint32_t all_ones(unsigned size)
{
    return size == 4 ? UINT32_MAX : MAKE_64BIT_MASK(0, size * 8);
}

uint32_t intel_460gx_chipset_device_mask(void)
{
    /* CBN device numbers implemented by the chipset model. */
    return BIT(0x00) | BIT(0x01) | BIT(0x04) | BIT(0x05) | BIT(0x06) |
           MAKE_64BIT_MASK(0x10, 8);
}

bool intel_460gx_chipset_device_valid(unsigned device)
{
    return device < 32 && (intel_460gx_chipset_device_mask() & BIT(device));
}

void intel_460gx_host_core_init(Intel460GXHostCore *core, uint8_t cbn,
                                uint32_t chipset_present)
{
    unsigned i;

    core->reset_cbn = cbn;
    core->reset_chipset_present =
        chipset_present & intel_460gx_chipset_device_mask();

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXDownstreamRoute *route = &core->downstream[i];

        if (!route->attached) {
            route->first_bus = INTEL_460GX_NO_BUS_FIRST;
            route->last_bus = INTEL_460GX_NO_BUS_LAST;
            route->reset_first_bus = INTEL_460GX_NO_BUS_FIRST;
            route->reset_last_bus = INTEL_460GX_NO_BUS_LAST;
        }
    }
    intel_460gx_host_core_reset(core);
}

void intel_460gx_host_core_reset(Intel460GXHostCore *core)
{
    unsigned i;

    core->config_address = 0;
    core->cbn = core->reset_cbn;
    core->chipset_present = core->reset_chipset_present;

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXDownstreamRoute *route = &core->downstream[i];

        if (route->attached) {
            route->first_bus = route->reset_first_bus;
            route->last_bus = route->reset_last_bus;
        } else {
            route->first_bus = INTEL_460GX_NO_BUS_FIRST;
            route->last_bus = INTEL_460GX_NO_BUS_LAST;
        }
    }
}

uint32_t intel_460gx_host_core_address_read(const Intel460GXHostCore *core,
                                            unsigned size)
{
    if (size != 4) {
        return all_ones(size);
    }
    return core->config_address;
}

void intel_460gx_host_core_address_write(Intel460GXHostCore *core,
                                         uint32_t value, unsigned size)
{
    if (size == 4) {
        core->config_address = value & INTEL_460GX_CONFIG_ADDRESS_MASK;
    }
}

static bool data_access_valid(unsigned data_offset, unsigned size)
{
    return (size == 1 || size == 2 || size == 4) && data_offset < 4 &&
           size <= 4 - data_offset;
}

static Intel460GXConfigTarget *find_internal_target(Intel460GXHostCore *core,
                                                    uint8_t bus, uint8_t device,
                                                    uint8_t function)
{
    if (bus == 0 && device == INTEL_460GX_BOOTSTRAP_SAC_DEVICE) {
        return &core->bootstrap_sac[function];
    }

    if (bus != core->cbn || !(core->chipset_present & BIT(device))) {
        return NULL;
    }
    return &core->chipset[device][function];
}

static Intel460GXDownstreamRoute *
find_downstream_route(Intel460GXHostCore *core, uint8_t bus)
{
    unsigned i;

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXDownstreamRoute *route = &core->downstream[i];

        if (route->attached && bus >= route->first_bus &&
            bus <= route->last_bus) {
            return route;
        }
    }
    return NULL;
}

uint32_t intel_460gx_host_core_data_read(Intel460GXHostCore *core,
                                         unsigned data_offset, unsigned size)
{
    Intel460GXConfigTarget *target;
    Intel460GXDownstreamRoute *route;
    uint32_t address;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t reg;

    if (!data_access_valid(data_offset, size) ||
        !(core->config_address & INTEL_460GX_CONFIG_ENABLE)) {
        return all_ones(size);
    }

    address = core->config_address | data_offset;
    bus = INTEL_460GX_BUS(address);
    device = INTEL_460GX_DEVICE(address);
    function = INTEL_460GX_FUNCTION(address);
    reg = INTEL_460GX_REGISTER(address);

    target = find_internal_target(core, bus, device, function);
    if (target != NULL) {
        if (target->ops == NULL || target->ops->read == NULL) {
            return all_ones(size);
        }
        return target->ops->read(target->opaque, reg, size) & all_ones(size);
    }

    /* CBN is wholly reserved for chipset targets, present or otherwise. */
    if (bus == core->cbn ||
        (bus == 0 && device == INTEL_460GX_BOOTSTRAP_SAC_DEVICE)) {
        return all_ones(size);
    }

    route = find_downstream_route(core, bus);
    if (route == NULL) {
        return all_ones(size);
    }
    return pci_data_read(route->bus, address, size) & all_ones(size);
}

void intel_460gx_host_core_data_write(Intel460GXHostCore *core,
                                      unsigned data_offset, uint32_t value,
                                      unsigned size)
{
    Intel460GXConfigTarget *target;
    Intel460GXDownstreamRoute *route;
    uint32_t address;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t reg;

    if (!data_access_valid(data_offset, size) ||
        !(core->config_address & INTEL_460GX_CONFIG_ENABLE)) {
        return;
    }

    address = core->config_address | data_offset;
    bus = INTEL_460GX_BUS(address);
    device = INTEL_460GX_DEVICE(address);
    function = INTEL_460GX_FUNCTION(address);
    reg = INTEL_460GX_REGISTER(address);

    target = find_internal_target(core, bus, device, function);
    if (target != NULL) {
        if (target->ops != NULL && target->ops->write != NULL) {
            target->ops->write(target->opaque, reg, value, size);
        }
        return;
    }

    if (bus == core->cbn ||
        (bus == 0 && device == INTEL_460GX_BOOTSTRAP_SAC_DEVICE)) {
        return;
    }

    route = find_downstream_route(core, bus);
    if (route != NULL) {
        pci_data_write(route->bus, address, value, size);
    }
}

static bool target_ops_valid(const Intel460GXConfigTargetOps *ops, Error **errp)
{
    if (ops == NULL || (ops->read == NULL && ops->write == NULL)) {
        error_setg(errp,
                   "configuration target requires a read or write callback");
        return false;
    }
    return true;
}

bool intel_460gx_host_core_register_bootstrap(
    Intel460GXHostCore *core, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp)
{
    Intel460GXConfigTarget *target;

    if (function >= 8) {
        error_setg(errp, "bootstrap SAC function %u is outside 0..7", function);
        return false;
    }
    if (!target_ops_valid(ops, errp)) {
        return false;
    }
    target = &core->bootstrap_sac[function];
    if (target->ops != NULL) {
        error_setg(errp, "bootstrap SAC function %u is already registered",
                   function);
        return false;
    }
    target->ops = ops;
    target->opaque = opaque;
    return true;
}

bool intel_460gx_host_core_register_chipset(
    Intel460GXHostCore *core, unsigned device, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp)
{
    Intel460GXConfigTarget *target;

    if (!intel_460gx_chipset_device_valid(device)) {
        error_setg(errp, "CBN device %02x is reserved", device);
        return false;
    }
    if (function >= 8) {
        error_setg(errp, "CBN device %02x function %u is outside 0..7", device,
                   function);
        return false;
    }
    if (!target_ops_valid(ops, errp)) {
        return false;
    }
    target = &core->chipset[device][function];
    if (target->ops != NULL) {
        error_setg(errp, "CBN device %02x function %u is already registered",
                   device, function);
        return false;
    }
    target->ops = ops;
    target->opaque = opaque;
    return true;
}

bool intel_460gx_host_core_apply_decoded_update(
    Intel460GXHostCore *core, const Intel460GXDecodedStateUpdate *update,
    Error **errp)
{
    Intel460GXHostCore candidate;
    uint32_t invalid_present;
    unsigned i;

    if (update == NULL) {
        error_setg(errp, "460GX decoded-state update is NULL");
        return false;
    }

    candidate = *core;
    if (update->has_cbn) {
        candidate.cbn = update->cbn;
    }
    if (update->has_chipset_present) {
        candidate.chipset_present = update->chipset_present;
    }

    invalid_present =
        candidate.chipset_present & ~intel_460gx_chipset_device_mask();
    if (invalid_present != 0) {
        error_setg(errp,
                   "460GX decoded chipset-present state includes reserved "
                   "CBN devices (mask 0x%08x)",
                   invalid_present);
        return false;
    }

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXDownstreamRoute *route;

        if (!(update->route_mask & BIT(i))) {
            continue;
        }
        route = &candidate.downstream[i];
        if (!route->attached) {
            error_setg(errp, "460GX downstream port %u is not attached", i);
            return false;
        }
        route->first_bus = update->routes[i].first_bus;
        route->last_bus = update->routes[i].last_bus;
    }

    if (!intel_460gx_host_core_validate_downstream(&candidate, errp)) {
        return false;
    }

    /* Commit only the decoded state; attachment and reset wiring are fixed. */
    core->cbn = candidate.cbn;
    core->chipset_present = candidate.chipset_present;
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        core->downstream[i].first_bus = candidate.downstream[i].first_bus;
        core->downstream[i].last_bus = candidate.downstream[i].last_bus;
    }
    return true;
}

bool intel_460gx_host_core_set_present(Intel460GXHostCore *core,
                                       unsigned device, bool present,
                                       Error **errp)
{
    Intel460GXDecodedStateUpdate update = { 0 };

    if (!intel_460gx_chipset_device_valid(device)) {
        error_setg(errp, "CBN device %02x is reserved", device);
        return false;
    }
    update.has_chipset_present = true;
    update.chipset_present =
        deposit32(core->chipset_present, device, 1, present);
    return intel_460gx_host_core_apply_decoded_update(core, &update, errp);
}

bool intel_460gx_host_core_attach_downstream(Intel460GXHostCore *core,
                                             unsigned port, PCIBus *bus,
                                             uint8_t first_bus,
                                             uint8_t last_bus, Error **errp)
{
    Intel460GXDownstreamRoute *route;
    unsigned i;

    if (port >= INTEL_460GX_DOWNSTREAM_PORTS) {
        error_setg(errp, "460GX downstream port %u is outside 0..7", port);
        return false;
    }
    if (bus == NULL) {
        error_setg(errp, "460GX downstream port %u requires a PCI bus", port);
        return false;
    }
    if (first_bus > last_bus) {
        error_setg(errp, "460GX downstream port %u has reversed bus range",
                   port);
        return false;
    }

    route = &core->downstream[port];
    if (route->attached) {
        error_setg(errp, "460GX downstream port %u is already attached", port);
        return false;
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXDownstreamRoute *other = &core->downstream[i];

        if (other->attached && first_bus <= other->last_bus &&
            last_bus >= other->first_bus) {
            error_setg(errp,
                       "460GX downstream bus range %u..%u overlaps port %u",
                       first_bus, last_bus, i);
            return false;
        }
        if (other->attached && first_bus <= other->reset_last_bus &&
            last_bus >= other->reset_first_bus) {
            error_setg(errp,
                       "460GX downstream reset bus range %u..%u "
                       "overlaps port %u",
                       first_bus, last_bus, i);
            return false;
        }
    }

    route->bus = bus;
    route->first_bus = first_bus;
    route->last_bus = last_bus;
    route->reset_first_bus = first_bus;
    route->reset_last_bus = last_bus;
    route->attached = true;
    return true;
}

bool intel_460gx_host_core_set_downstream_range(Intel460GXHostCore *core,
                                                unsigned port,
                                                uint8_t first_bus,
                                                uint8_t last_bus, Error **errp)
{
    Intel460GXDecodedStateUpdate update = { 0 };

    if (port >= INTEL_460GX_DOWNSTREAM_PORTS) {
        error_setg(errp, "460GX downstream port %u is outside 0..7", port);
        return false;
    }
    update.route_mask = BIT(port);
    update.routes[port].first_bus = first_bus;
    update.routes[port].last_bus = last_bus;
    return intel_460gx_host_core_apply_decoded_update(core, &update, errp);
}

bool intel_460gx_host_core_validate_downstream(const Intel460GXHostCore *core,
                                               Error **errp)
{
    unsigned i;
    unsigned j;

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        const Intel460GXDownstreamRoute *route = &core->downstream[i];

        if (!route->attached) {
            if (route->bus != NULL ||
                route->first_bus != INTEL_460GX_NO_BUS_FIRST ||
                route->last_bus != INTEL_460GX_NO_BUS_LAST ||
                route->reset_first_bus != INTEL_460GX_NO_BUS_FIRST ||
                route->reset_last_bus != INTEL_460GX_NO_BUS_LAST) {
                error_setg(errp,
                           "460GX downstream port %u range state does not "
                           "match destination wiring",
                           i);
                return false;
            }
            continue;
        }
        if (route->bus == NULL) {
            error_setg(
                errp, "460GX downstream port %u has no destination PCI bus", i);
            return false;
        }
        if (route->first_bus > route->last_bus) {
            error_setg(errp, "460GX downstream port %u has reversed bus range",
                       i);
            return false;
        }
        if (route->reset_first_bus > route->reset_last_bus) {
            error_setg(errp,
                       "460GX downstream port %u has reversed reset bus "
                       "range",
                       i);
            return false;
        }

        for (j = i + 1; j < INTEL_460GX_DOWNSTREAM_PORTS; j++) {
            const Intel460GXDownstreamRoute *other = &core->downstream[j];

            if (!other->attached) {
                continue;
            }
            if (route->first_bus <= other->last_bus &&
                route->last_bus >= other->first_bus) {
                error_setg(errp,
                           "460GX downstream bus ranges on ports %u and %u "
                           "overlap",
                           i, j);
                return false;
            }
            if (route->reset_first_bus <= other->reset_last_bus &&
                route->reset_last_bus >= other->reset_first_bus) {
                error_setg(errp,
                           "460GX downstream reset bus ranges on ports %u "
                           "and %u overlap",
                           i, j);
                return false;
            }
        }
    }
    return true;
}
