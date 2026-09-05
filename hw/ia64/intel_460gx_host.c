/*
 * Intel 460GX PCI configuration mechanism.  The board supplies the PCI
 * identity, SAC/xXB registers, and downstream buses.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_host.h"
#include "hw/ia64/intel_460gx_host_internal.h"
#include "hw/ia64/intel_460gx_root.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

#define INTEL_460GX_TEST_IO_UNMAPPED UINT64_MAX

struct Intel460GXHostState {
    SysBusDevice parent_obj;

    Intel460GXHostCore core;
    MemoryRegion conf_io;
    MemoryRegion data_io;

    uint16_t initial_cbn;
    uint32_t initial_chipset_present;
    uint64_t test_io_base;
};

static uint64_t host_conf_read(void *opaque, hwaddr addr, unsigned size)
{
    Intel460GXHostState *s = opaque;

    (void)addr;
    return intel_460gx_host_core_address_read(&s->core, size);
}

static void host_conf_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    Intel460GXHostState *s = opaque;

    (void)addr;
    intel_460gx_host_core_address_write(&s->core, value, size);
}

static bool host_conf_access_valid(void *opaque, hwaddr addr, unsigned size,
                                   bool is_write, MemTxAttrs attrs)
{
    (void)opaque;
    (void)is_write;
    (void)attrs;
    return addr == 0 && size == 4;
}

static const MemoryRegionOps host_conf_ops = {
    .read = host_conf_read,
    .write = host_conf_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = host_conf_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t host_data_read(void *opaque, hwaddr addr, unsigned size)
{
    Intel460GXHostState *s = opaque;

    return intel_460gx_host_core_data_read(&s->core, addr, size);
}

static void host_data_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    Intel460GXHostState *s = opaque;

    intel_460gx_host_core_data_write(&s->core, addr, value, size);
}

static bool host_data_access_valid(void *opaque, hwaddr addr, unsigned size,
                                   bool is_write, MemTxAttrs attrs)
{
    (void)opaque;
    (void)is_write;
    (void)attrs;
    return (size == 1 || size == 2 || size == 4) &&
           addr < 4 && size <= 4 - addr;
}

static const MemoryRegionOps host_data_ops = {
    .read = host_data_read,
    .write = host_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
        .accepts = host_data_access_valid,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void intel_460gx_host_reset(DeviceState *dev)
{
    Intel460GXHostState *s = INTEL_460GX_HOST(dev);

    intel_460gx_host_core_reset(&s->core);
    for (unsigned int i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if (s->core.downstream[i].attached) {
            intel_460gx_numbered_root_bus_set_number(
                s->core.downstream[i].bus,
                s->core.downstream[i].first_bus);
        }
    }
}

static int intel_460gx_host_pre_load(void *opaque)
{
    Intel460GXHostState *s = opaque;

    /* Reset supplies downstream ranges absent from v1 streams. */
    intel_460gx_host_core_reset(&s->core);
    return 0;
}

static bool intel_460gx_host_post_load(void *opaque, int version_id,
                                       Error **errp)
{
    Intel460GXHostState *s = opaque;

    (void)version_id;

    if (s->core.config_address & ~INTEL_460GX_CONFIG_ADDRESS_MASK ||
        s->core.chipset_present & ~intel_460gx_chipset_device_mask()) {
        error_setg(errp, "460GX host migration state has reserved bits set");
        return false;
    }
    if (!intel_460gx_host_core_validate_downstream(&s->core, errp)) {
        return false;
    }
    for (unsigned int i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if (s->core.downstream[i].attached) {
            intel_460gx_numbered_root_bus_set_number(
                s->core.downstream[i].bus,
                s->core.downstream[i].first_bus);
        }
    }
    return true;
}

static const VMStateDescription vmstate_intel_460gx_downstream_route = {
    .name = TYPE_INTEL_460GX_HOST "/downstream-route",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        /* PCI bus pointer and attachment topology are destination wiring. */
        VMSTATE_UINT8(first_bus, Intel460GXDownstreamRoute),
        VMSTATE_UINT8(last_bus, Intel460GXDownstreamRoute),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_intel_460gx_downstream_reset = {
    .name = TYPE_INTEL_460GX_HOST "/downstream-reset",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        /* Reject a destination whose guest-visible reset wiring differs. */
        VMSTATE_UINT8_EQUAL(reset_first_bus, Intel460GXDownstreamRoute),
        VMSTATE_UINT8_EQUAL(reset_last_bus, Intel460GXDownstreamRoute),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_intel_460gx_host = {
    .name = TYPE_INTEL_460GX_HOST,
    .version_id = 3,
    .minimum_version_id = 1,
    .pre_load = intel_460gx_host_pre_load,
    .post_load_errp = intel_460gx_host_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(core.config_address, Intel460GXHostState),
        VMSTATE_UINT8(core.cbn, Intel460GXHostState),
        VMSTATE_UINT32(core.chipset_present, Intel460GXHostState),
        /*
         * v1/v2 did not carry the reset wiring.  v3 streams reject a
         * destination whose QOM-supplied reset state differs from source.
         */
        VMSTATE_UINT16_EQUAL_V(initial_cbn, Intel460GXHostState, 3),
        VMSTATE_UINT32_EQUAL_V(initial_chipset_present,
                               Intel460GXHostState, 3),
        VMSTATE_STRUCT_ARRAY(core.downstream, Intel460GXHostState,
                             INTEL_460GX_DOWNSTREAM_PORTS, 3,
                             vmstate_intel_460gx_downstream_reset,
                             Intel460GXDownstreamRoute),
        VMSTATE_STRUCT_ARRAY(core.downstream, Intel460GXHostState,
                             INTEL_460GX_DOWNSTREAM_PORTS, 2,
                             vmstate_intel_460gx_downstream_route,
                             Intel460GXDownstreamRoute),
        VMSTATE_END_OF_LIST()
    },
};

static void intel_460gx_host_realize(DeviceState *dev, Error **errp)
{
    Intel460GXHostState *s = INTEL_460GX_HOST(dev);
    uint32_t invalid_present;

    if (s->initial_cbn > UINT8_MAX) {
        error_setg(errp, "x-initial-cbn is required and must be 0..255");
        return;
    }
    invalid_present = s->initial_chipset_present &
                      ~intel_460gx_chipset_device_mask();
    if (invalid_present != 0) {
        error_setg(errp,
                   "x-initial-chipset-present includes reserved CBN devices "
                   "(mask 0x%08x)", invalid_present);
        return;
    }

    intel_460gx_host_core_init(&s->core, s->initial_cbn,
                               s->initial_chipset_present);

    if (s->test_io_base != INTEL_460GX_TEST_IO_UNMAPPED) {
        if ((s->test_io_base & 3) || s->test_io_base > UINT64_MAX - 7) {
            error_setg(errp,
                       "x-test-io-base must be dword aligned and leave "
                       "room for two four-byte windows");
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, s->test_io_base);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, s->test_io_base + 4);
    }
}

static void intel_460gx_host_init(Object *obj)
{
    Intel460GXHostState *s = INTEL_460GX_HOST(obj);

    memory_region_init_io(&s->conf_io, obj, &host_conf_ops, s,
                          TYPE_INTEL_460GX_HOST ".config-address", 4);
    memory_region_init_io(&s->data_io, obj, &host_data_ops, s,
                          TYPE_INTEL_460GX_HOST ".config-data", 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->conf_io);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->data_io);
}

static const Property intel_460gx_host_properties[] = {
    DEFINE_PROP_UINT16("x-initial-cbn", Intel460GXHostState, initial_cbn,
                       UINT16_MAX),
    DEFINE_PROP_UINT32("x-initial-chipset-present", Intel460GXHostState,
                       initial_chipset_present, 0),
    DEFINE_PROP_UINT64("x-test-io-base", Intel460GXHostState, test_io_base,
                       INTEL_460GX_TEST_IO_UNMAPPED),
};

static void intel_460gx_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Intel 460GX PCI configuration host core";
    dc->realize = intel_460gx_host_realize;
    device_class_set_legacy_reset(dc, intel_460gx_host_reset);
    device_class_set_props(dc, intel_460gx_host_properties);
    dc->vmsd = &vmstate_intel_460gx_host;
}

static const TypeInfo intel_460gx_host_type_info = {
    .name = TYPE_INTEL_460GX_HOST,
    .parent = TYPE_DYNAMIC_SYS_BUS_DEVICE,
    .instance_size = sizeof(Intel460GXHostState),
    .instance_init = intel_460gx_host_init,
    .class_init = intel_460gx_host_class_init,
};

static void intel_460gx_host_register_types(void)
{
    type_register_static(&intel_460gx_host_type_info);
}
type_init(intel_460gx_host_register_types)

MemoryRegion *intel_460gx_host_conf_region(Intel460GXHostState *s)
{
    return &s->conf_io;
}

MemoryRegion *intel_460gx_host_data_region(Intel460GXHostState *s)
{
    return &s->data_io;
}

bool intel_460gx_host_apply_decoded_update(
    Intel460GXHostState *s, const Intel460GXDecodedStateUpdate *update,
    Error **errp)
{
    unsigned int i;

    if (!intel_460gx_host_core_apply_decoded_update(&s->core, update,
                                                     errp)) {
        return false;
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if ((update->route_mask & BIT(i)) &&
            s->core.downstream[i].attached) {
            intel_460gx_numbered_root_bus_set_number(
                s->core.downstream[i].bus,
                s->core.downstream[i].first_bus);
        }
    }
    return true;
}

bool intel_460gx_host_register_bootstrap_sac(
    Intel460GXHostState *s, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp)
{
    return intel_460gx_host_core_register_bootstrap(&s->core, function, ops,
                                                     opaque, errp);
}

bool intel_460gx_host_register_chipset_target(
    Intel460GXHostState *s, unsigned device, unsigned function,
    const Intel460GXConfigTargetOps *ops, void *opaque, Error **errp)
{
    return intel_460gx_host_core_register_chipset(&s->core, device, function,
                                                   ops, opaque, errp);
}

void intel_460gx_host_set_cbn(Intel460GXHostState *s, uint8_t cbn)
{
    Intel460GXDecodedStateUpdate update = {
        .has_cbn = true,
        .cbn = cbn,
    };

    intel_460gx_host_apply_decoded_update(s, &update, &error_abort);
}

uint8_t intel_460gx_host_get_cbn(const Intel460GXHostState *s)
{
    return s->core.cbn;
}

bool intel_460gx_host_set_chipset_present(Intel460GXHostState *s,
                                           unsigned device, bool present,
                                           Error **errp)
{
    return intel_460gx_host_core_set_present(&s->core, device, present,
                                              errp);
}

bool intel_460gx_host_configure_chipset_present(Intel460GXHostState *s,
                                                 uint32_t present,
                                                 Error **errp)
{
    uint32_t invalid = present & ~intel_460gx_chipset_device_mask();

    if (invalid) {
        error_setg(errp,
                   "460GX chipset wiring includes reserved CBN devices "
                   "(mask 0x%08x)", invalid);
        return false;
    }

    s->initial_chipset_present = present;
    if (qdev_is_realized(DEVICE(s))) {
        s->core.reset_chipset_present = present;
        s->core.chipset_present = present;
    }
    return true;
}

uint32_t intel_460gx_host_get_chipset_present(
    const Intel460GXHostState *s)
{
    return qdev_is_realized(DEVICE(s)) ? s->core.chipset_present :
                                        s->initial_chipset_present;
}

bool intel_460gx_host_attach_downstream_bus(Intel460GXHostState *s,
                                             unsigned port, PCIBus *bus,
                                             uint8_t first_bus,
                                             uint8_t last_bus,
                                             Error **errp)
{
    return intel_460gx_host_core_attach_downstream(&s->core, port, bus,
                                                    first_bus, last_bus,
                                                    errp);
}

bool intel_460gx_host_set_downstream_bus_range(Intel460GXHostState *s,
                                                unsigned port,
                                                uint8_t first_bus,
                                                uint8_t last_bus,
                                                Error **errp)
{
    return intel_460gx_host_core_set_downstream_range(&s->core, port,
                                                       first_bus, last_bus,
                                                       errp);
}
