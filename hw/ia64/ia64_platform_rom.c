/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Read-only mapping for the IA-64 firmware descriptor.
 */

#include "qemu/osdep.h"

#include "hw/core/qdev.h"
#include "hw/ia64/ia64_platform.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#define IA64_PLATFORM_DESC_ROM_PRIORITY 1

static bool ia64_platform_desc_range_is_writable_ram(MemoryRegion *sysmem,
                                                     hwaddr gpa, Error **errp)
{
    hwaddr cursor = gpa;
    uint64_t remaining = IA64_PLATFORM_DESC_MAX_SIZE;

    while (remaining) {
        MemoryRegionSection section =
            memory_region_find(sysmem, cursor, remaining);
        uint64_t section_size;

        if (!section.mr || section.offset_within_address_space != cursor) {
            if (section.mr) {
                memory_region_unref(section.mr);
            }
            error_setg(errp,
                       "IA-64 firmware-assist descriptor has no RAM backing "
                       "at 0x%" HWADDR_PRIx,
                       cursor);
            return false;
        }

        section_size = int128_get64(section.size);
        if (section_size == 0 || section_size > remaining) {
            error_setg(errp,
                       "invalid effective memory section at 0x%" HWADDR_PRIx,
                       cursor);
            memory_region_unref(section.mr);
            return false;
        }
        if (!memory_region_is_ram(section.mr) || section.readonly ||
            memory_region_is_rom(section.mr) ||
            memory_region_is_ram_device(section.mr) ||
            memory_region_is_romd(section.mr) ||
            memory_region_is_protected(section.mr)) {
            error_setg(errp,
                       "IA-64 firmware-assist descriptor is not backed by "
                       "ordinary writable RAM ('%s') at 0x%" HWADDR_PRIx,
                       memory_region_name(section.mr), cursor);
            memory_region_unref(section.mr);
            return false;
        }

        memory_region_unref(section.mr);
        cursor += section_size;
        remaining -= section_size;
    }

    return true;
}

static bool ia64_platform_desc_rom_is_active(MemoryRegion *sysmem,
                                             MemoryRegion *region, hwaddr gpa,
                                             Error **errp)
{
    hwaddr cursor = gpa;
    uint64_t remaining = IA64_PLATFORM_DESC_MAX_SIZE;

    while (remaining) {
        MemoryRegionSection section =
            memory_region_find(sysmem, cursor, remaining);
        uint64_t section_size;

        if (!section.mr || section.offset_within_address_space != cursor) {
            if (section.mr) {
                memory_region_unref(section.mr);
            }
            error_setg(errp,
                       "IA-64 firmware-assist ROM is not active at "
                       "0x%" HWADDR_PRIx,
                       cursor);
            return false;
        }

        section_size = int128_get64(section.size);
        if (section_size == 0 || section_size > remaining ||
            section.mr != region || !section.readonly ||
            !memory_region_is_rom(section.mr) ||
            section.offset_within_region != cursor - gpa) {
            error_setg(errp,
                       "IA-64 firmware-assist ROM is obscured at "
                       "0x%" HWADDR_PRIx,
                       cursor);
            memory_region_unref(section.mr);
            return false;
        }

        memory_region_unref(section.mr);
        cursor += section_size;
        remaining -= section_size;
    }

    return true;
}

bool ia64_platform_desc_install_rom(MemoryRegion *region, Object *owner,
                                    const char *name, hwaddr gpa,
                                    const void *descriptor,
                                    size_t descriptor_size, Error **errp)
{
    MemoryRegion *sysmem;
    DeviceState *owner_dev;
    void *rom_ptr;

    if (region == NULL || owner == NULL || name == NULL || descriptor == NULL ||
        !ia64_platform_desc_mapping_valid(gpa, descriptor_size)) {
        error_setg(errp, "invalid IA-64 firmware-assist descriptor mapping");
        return false;
    }
    owner_dev = (DeviceState *)object_dynamic_cast(owner, TYPE_DEVICE);
    if (!owner_dev) {
        error_setg(errp,
                   "IA-64 firmware-assist ROM owner must be a DeviceState");
        return false;
    }

    sysmem = get_system_memory();
    if (!ia64_platform_desc_range_is_writable_ram(sysmem, gpa, errp)) {
        return false;
    }
    if (!memory_region_init_rom(region, owner, name,
                                IA64_PLATFORM_DESC_MAX_SIZE, errp)) {
        return false;
    }
    rom_ptr = memory_region_get_ram_ptr(region);
    memset(rom_ptr, 0, IA64_PLATFORM_DESC_MAX_SIZE);
    memcpy(rom_ptr, descriptor, descriptor_size);
    memory_region_add_subregion_overlap(sysmem, gpa, region,
                                        IA64_PLATFORM_DESC_ROM_PRIORITY);
    if (!ia64_platform_desc_rom_is_active(sysmem, region, gpa, errp)) {
        memory_region_del_subregion(sysmem, region);
        vmstate_unregister_ram(region, owner_dev);
        object_unparent(OBJECT(region));
        return false;
    }
    return true;
}
