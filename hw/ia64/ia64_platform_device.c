/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 platform descriptor device.
 */

#include "qemu/osdep.h"

#include "hw/core/qdev.h"
#include "hw/ia64/ia64_platform.h"
#include "migration/vmstate.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/memory.h"

typedef union IA64PlatformDescriptorStorage {
    uint64_t alignment;
    uint8_t bytes[IA64_PLATFORM_DESC_MAX_SIZE];
} IA64PlatformDescriptorStorage;

struct IA64PlatformDescriptorDevice {
    DeviceState parent_obj;

    MemoryRegion rom;
    IA64PlatformDescriptorStorage storage;
    char *rom_name;
    hwaddr gpa;
    size_t descriptor_size;
    uint32_t platform_id;
    bool configured;
    bool installed;
};

static void
ia64_platform_desc_device_unmap(IA64PlatformDescriptorDevice *device)
{
    DeviceState *owner = DEVICE(device);

    if (!device->installed) {
        return;
    }
    device->installed = false;
    memory_region_del_subregion(get_system_memory(), &device->rom);
    vmstate_unregister_ram(&device->rom, owner);
    object_unparent(OBJECT(&device->rom));
}

static void ia64_platform_desc_device_realize(DeviceState *dev, Error **errp)
{
    IA64PlatformDescriptorDevice *device = IA64_PLATFORM_DESCRIPTOR_DEVICE(dev);

    if (!device->configured || device->rom_name == NULL || device->installed) {
        error_setg(errp, "unconfigured IA-64 platform descriptor device");
        return;
    }
    if (!ia64_platform_desc_install_rom(
            &device->rom, OBJECT(device), device->rom_name, device->gpa,
            device->storage.bytes, device->descriptor_size, errp)) {
        return;
    }
    device->installed = true;
}

static void ia64_platform_desc_device_unrealize(DeviceState *dev)
{
    ia64_platform_desc_device_unmap(IA64_PLATFORM_DESCRIPTOR_DEVICE(dev));
}

static void ia64_platform_desc_device_finalize(Object *obj)
{
    IA64PlatformDescriptorDevice *device = IA64_PLATFORM_DESCRIPTOR_DEVICE(obj);

    g_assert(!device->installed);
    g_clear_pointer(&device->rom_name, g_free);
}

/* Reject migration when immutable descriptor bytes or placement differ. */
static const VMStateDescription vmstate_ia64_platform_desc_device = {
    .name = TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT64_EQUAL(gpa, IA64PlatformDescriptorDevice),
            VMSTATE_ARRAY(storage.bytes, IA64PlatformDescriptorDevice,
                          IA64_PLATFORM_DESC_MAX_SIZE, 0,
                          vmstate_info_uint8_equal, uint8_t),
            VMSTATE_END_OF_LIST() },
};

static void ia64_platform_desc_device_class_init(ObjectClass *oc,
                                                 const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->realize = ia64_platform_desc_device_realize;
    dc->unrealize = ia64_platform_desc_device_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_ia64_platform_desc_device;
}

static const TypeInfo ia64_platform_desc_device_type_info = {
    .name = TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IA64PlatformDescriptorDevice),
    .instance_finalize = ia64_platform_desc_device_finalize,
    .class_init = ia64_platform_desc_device_class_init,
};

static void ia64_platform_desc_device_register_types(void)
{
    type_register_static(&ia64_platform_desc_device_type_info);
}

type_init(ia64_platform_desc_device_register_types)

    IA64PlatformDescriptorDevice *ia64_platform_desc_device_create(
        Object *parent, const char *name, hwaddr gpa,
        const IA64PlatformDescriptor *header,
        const IA64PlatformDescriptorArrays *arrays, Error **errp)
{
    DeviceState *dev;
    IA64PlatformDescriptorDevice *device;
    g_autofree char *canonical_path = NULL;

    if (parent == NULL || name == NULL || name[0] == '\0') {
        error_setg(errp, "invalid IA-64 platform descriptor owner or name");
        return NULL;
    }

    dev = qdev_new(TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE);
    device = IA64_PLATFORM_DESCRIPTOR_DEVICE(dev);
    if (!ia64_platform_desc_build(device->storage.bytes,
                                  sizeof(device->storage.bytes), header, arrays,
                                  &device->descriptor_size, errp)) {
        object_unref(OBJECT(device));
        return NULL;
    }
    if (!ia64_platform_desc_mapping_in_ram(
            (IA64PlatformDescriptor *)device->storage.bytes, gpa,
            device->descriptor_size)) {
        error_setg(errp, "invalid IA-64 platform descriptor placement");
        object_unref(OBJECT(device));
        return NULL;
    }

    device->gpa = gpa;
    device->platform_id = le32_to_cpu(
        ((IA64PlatformDescriptor *)device->storage.bytes)->PlatformId);
    device->configured = true;

    if (!object_property_try_add_child(parent, name, OBJECT(device), errp)) {
        object_unref(OBJECT(device));
        return NULL;
    }
    canonical_path = object_get_canonical_path(OBJECT(device));
    if (canonical_path == NULL) {
        error_setg(errp,
                   "IA-64 platform descriptor owner is not in the QOM tree");
        object_unparent(OBJECT(device));
        object_unref(OBJECT(device));
        return NULL;
    }
    device->rom_name = g_strdup_printf("%s.rom", canonical_path);

    if (!qdev_realize(dev, NULL, errp)) {
        object_unparent(OBJECT(device));
        object_unref(OBJECT(device));
        return NULL;
    }

    /* The parent child property owns the sole reference. */
    object_unref(OBJECT(device));
    return device;
}

void ia64_platform_desc_device_destroy(IA64PlatformDescriptorDevice *device)
{
    if (device != NULL) {
        object_unparent(OBJECT(device));
    }
}

bool ia64_platform_desc_device_get_firmware_args(
    const IA64PlatformDescriptorDevice *device, IA64PlatformFirmwareArgs *args)
{
    IA64PlatformFirmwareArgs result;
    const IA64PlatformDescriptor *descriptor;

    if (device == NULL || args == NULL || !device->installed) {
        return false;
    }
    descriptor = (const IA64PlatformDescriptor *)device->storage.bytes;
    if (device->descriptor_size != le32_to_cpu(descriptor->TotalSize) ||
        device->platform_id != le32_to_cpu(descriptor->PlatformId)) {
        return false;
    }

    result = (IA64PlatformFirmwareArgs) {
        .descriptor_gpa = device->gpa,
        .descriptor_size = device->descriptor_size,
        .firmware_compat_flags = ia64_platform_firmware_compat_flags(
            device->platform_id, le32_to_cpu(descriptor->Flags)),
        .platform_id = device->platform_id,
    };
    *args = result;
    return true;
}
