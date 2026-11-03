/*
 * IA-64 platform descriptor ROM mapping tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev.h"
#include "hw/ia64/ia64_platform.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#define TYPE_TEST_ROM_DEVICE "test-ia64-platform-rom-device"
#define TYPE_TEST_ROM_MACHINE "test-ia64-platform-rom-machine"
#define TYPE_TEST_ROM_MEMORY_REGION "test-ia64-platform-rom-memory-region"

#define TEST_GPA IA64_PLATFORM_DESC_ALIGNMENT
#define TEST_MAX_SECTIONS 4

typedef struct TestMapSection {
    hwaddr base;
    uint64_t size;
    MemoryRegion *mr;
    bool readonly;
} TestMapSection;

static MemoryRegion test_sysmem;
static TestMapSection test_sections[TEST_MAX_SECTIONS];
static size_t test_section_count;
static TestMapSection *test_post_add_obstacle;
static MemoryRegion *test_mapped_rom;
static hwaddr test_mapped_gpa;
static uint8_t *test_rom_ptr;
static const MemoryRegion *test_protected_ram;
static int test_add_count;
static int test_del_count;
static int test_unregister_count;
static int test_finalize_count;
static int test_priority;

static const TypeInfo test_device_base_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DeviceState),
    .class_size = sizeof(DeviceClass),
};

static const TypeInfo test_device_info = {
    .name = TYPE_TEST_ROM_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DeviceState),
    .class_size = sizeof(DeviceClass),
};

static const TypeInfo test_machine_info = {
    .name = TYPE_TEST_ROM_MACHINE,
    .parent = TYPE_OBJECT,
};

static void test_memory_region_finalize(Object *obj)
{
    MemoryRegion *mr = (MemoryRegion *)obj;

    g_assert_true(mr == test_mapped_rom || test_mapped_rom == NULL);
    g_clear_pointer(&test_rom_ptr, g_free);
    g_clear_pointer((char **)&mr->name, g_free);
    test_mapped_rom = NULL;
    test_finalize_count++;
}

static const TypeInfo test_memory_region_info = {
    .name = TYPE_TEST_ROM_MEMORY_REGION,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(MemoryRegion),
    .instance_finalize = test_memory_region_finalize,
};

static void test_register_types(void)
{
    if (!object_class_by_name(TYPE_DEVICE)) {
        type_register_static(&test_device_base_info);
    }
    type_register_static(&test_device_info);
    type_register_static(&test_machine_info);
    type_register_static(&test_memory_region_info);
}

type_init(test_register_types)

    static void test_map_reset(void)
{
    g_assert_null(test_mapped_rom);
    g_assert_null(test_rom_ptr);
    memset(&test_sysmem, 0, sizeof(test_sysmem));
    memset(test_sections, 0, sizeof(test_sections));
    test_section_count = 0;
    test_post_add_obstacle = NULL;
    test_protected_ram = NULL;
    test_add_count = 0;
    test_del_count = 0;
    test_unregister_count = 0;
    test_finalize_count = 0;
    test_priority = 0;
}

static TestMapSection *test_map_add(hwaddr base, uint64_t size,
                                    MemoryRegion *mr, bool readonly)
{
    TestMapSection *section;

    g_assert_cmpuint(test_section_count, <, TEST_MAX_SECTIONS);
    section = &test_sections[test_section_count++];
    section->base = base;
    section->size = size;
    section->mr = mr;
    section->readonly = readonly;
    return section;
}

static void test_ram_init(MemoryRegion *mr, const char *name)
{
    memset(mr, 0, sizeof(*mr));
    mr->ram = true;
    mr->name = (char *)name;
    mr->size = int128_make64(IA64_PLATFORM_DESC_MAX_SIZE);
}

static MemoryRegionSection test_make_section(const TestMapSection *map,
                                             hwaddr addr, uint64_t size)
{
    MemoryRegionSection section = { 0 };
    hwaddr start = MAX(addr, map->base);
    uint64_t query_end = addr + size;
    uint64_t map_end = map->base + map->size;
    uint64_t end = MIN(query_end, map_end);

    section.mr = map->mr;
    section.offset_within_address_space = start;
    section.offset_within_region = start - map->base;
    section.size = int128_make64(end - start);
    section.readonly = map->readonly;
    return section;
}

MemoryRegionSection memory_region_find(MemoryRegion *mr, hwaddr addr,
                                       uint64_t size)
{
    uint64_t query_end = addr + size;
    size_t i;

    g_assert_true(mr == &test_sysmem);

    if (test_mapped_rom && addr >= test_mapped_gpa &&
        addr < test_mapped_gpa + IA64_PLATFORM_DESC_MAX_SIZE) {
        TestMapSection rom = {
            .base = test_mapped_gpa,
            .size = IA64_PLATFORM_DESC_MAX_SIZE,
            .mr = test_mapped_rom,
            .readonly = true,
        };

        if (test_post_add_obstacle && addr >= test_post_add_obstacle->base &&
            addr <
                test_post_add_obstacle->base + test_post_add_obstacle->size) {
            return test_make_section(test_post_add_obstacle, addr, size);
        }
        if (test_post_add_obstacle && addr < test_post_add_obstacle->base &&
            query_end > test_post_add_obstacle->base) {
            rom.size = test_post_add_obstacle->base - test_mapped_gpa;
        }
        return test_make_section(&rom, addr, size);
    }

    for (i = 0; i < test_section_count; i++) {
        TestMapSection *map = &test_sections[i];

        if (map->base < query_end && addr < map->base + map->size) {
            return test_make_section(map, addr, size);
        }
    }

    return (MemoryRegionSection) { 0 };
}

MemoryRegion *get_system_memory(void)
{
    return &test_sysmem;
}

void memory_region_unref(MemoryRegion *mr)
{
    g_assert_nonnull(mr);
}

const char *memory_region_name(const MemoryRegion *mr)
{
    return mr->name;
}

bool memory_region_is_ram_device(const MemoryRegion *mr)
{
    return mr->ram_device;
}

bool memory_region_is_protected(const MemoryRegion *mr)
{
    return mr == test_protected_ram;
}

bool memory_region_init_rom(MemoryRegion *mr, Object *owner, const char *name,
                            uint64_t size, Error **errp)
{
    (void)errp;
    g_assert_cmpuint(size, ==, IA64_PLATFORM_DESC_MAX_SIZE);
    g_assert_null(test_rom_ptr);

    object_initialize(mr, sizeof(*mr), TYPE_TEST_ROM_MEMORY_REGION);
    mr->owner = owner;
    mr->name = g_strdup(name);
    mr->size = int128_make64(size);
    mr->ram = true;
    mr->readonly = true;
    test_rom_ptr = g_malloc(size);
    object_property_add_child(owner, "descriptor-rom", OBJECT(mr));
    object_unref(OBJECT(mr));
    return true;
}

void *memory_region_get_ram_ptr(const MemoryRegion *mr)
{
    g_assert_true(mr == test_mapped_rom || test_mapped_rom == NULL);
    return test_rom_ptr;
}

void memory_region_add_subregion_overlap(MemoryRegion *mr, hwaddr offset,
                                         MemoryRegion *subregion, int priority)
{
    g_assert_true(mr == &test_sysmem);
    g_assert_null(test_mapped_rom);
    test_mapped_rom = subregion;
    test_mapped_gpa = offset;
    test_priority = priority;
    subregion->container = mr;
    subregion->addr = offset;
    test_add_count++;
}

void memory_region_del_subregion(MemoryRegion *mr, MemoryRegion *subregion)
{
    g_assert_true(mr == &test_sysmem);
    g_assert_true(subregion == test_mapped_rom);
    subregion->container = NULL;
    test_mapped_rom = NULL;
    test_del_count++;
}

void vmstate_unregister_ram(MemoryRegion *mr, DeviceState *dev)
{
    g_assert_nonnull(mr);
    g_assert_nonnull(dev);
    test_unregister_count++;
}

static Object *test_device_new(void)
{
    return object_new(TYPE_TEST_ROM_DEVICE);
}

static void test_rom_destroy(MemoryRegion *rom, DeviceState *owner)
{
    memory_region_del_subregion(&test_sysmem, rom);
    vmstate_unregister_ram(rom, owner);
    object_unparent(OBJECT(rom));
}

static void test_install_fragmented_ram(void)
{
    uint8_t descriptor[sizeof(IA64PlatformDescriptor)];
    MemoryRegion first_ram;
    MemoryRegion second_ram;
    MemoryRegion rom;
    Object *owner = test_device_new();
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&first_ram, "first-ram");
    test_ram_init(&second_ram, "second-ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE / 2, &first_ram, false);
    test_map_add(TEST_GPA + IA64_PLATFORM_DESC_MAX_SIZE / 2,
                 IA64_PLATFORM_DESC_MAX_SIZE / 2, &second_ram, false);
    memset(descriptor, 0x5a, sizeof(descriptor));

    g_assert_true(ia64_platform_desc_install_rom(&rom, owner, "test-descriptor",
                                                 TEST_GPA, descriptor,
                                                 sizeof(descriptor), &err));
    g_assert_null(err);
    g_assert_cmpint(test_add_count, ==, 1);
    g_assert_cmpint(test_priority, >, 0);
    g_assert_cmpmem(test_rom_ptr, sizeof(descriptor), descriptor,
                    sizeof(descriptor));
    for (size_t i = sizeof(descriptor); i < IA64_PLATFORM_DESC_MAX_SIZE; i++) {
        g_assert_cmpuint(test_rom_ptr[i], ==, 0);
    }

    test_rom_destroy(&rom, (DeviceState *)owner);
    g_assert_cmpint(test_finalize_count, ==, 1);
    object_unref(owner);
}

static void test_reject_gap(void)
{
    uint8_t descriptor[sizeof(IA64PlatformDescriptor)] = { 0 };
    MemoryRegion ram;
    MemoryRegion rom;
    Object *owner = test_device_new();
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE / 2, &ram, false);
    test_map_add(TEST_GPA + IA64_PLATFORM_DESC_MAX_SIZE / 2 + 1,
                 IA64_PLATFORM_DESC_MAX_SIZE / 2 - 1, &ram, false);

    g_assert_false(ia64_platform_desc_install_rom(&rom, owner, "test-gap",
                                                  TEST_GPA, descriptor,
                                                  sizeof(descriptor), &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "no RAM backing"));
    g_assert_cmpint(test_add_count, ==, 0);
    error_free(err);
    object_unref(owner);
}

static void test_assert_backing_rejected(MemoryRegion *backing,
                                         bool effective_readonly,
                                         bool protected)
{
    uint8_t descriptor[sizeof(IA64PlatformDescriptor)] = { 0 };
    MemoryRegion rom;
    Object *owner = test_device_new();
    Error *err = NULL;

    test_map_reset();
    if (protected) {
        test_protected_ram = backing;
    }
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, backing,
                 effective_readonly);
    g_assert_false(ia64_platform_desc_install_rom(&rom, owner, "test-reject",
                                                  TEST_GPA, descriptor,
                                                  sizeof(descriptor), &err));
    g_assert_nonnull(err);
    g_assert_cmpint(test_add_count, ==, 0);
    error_free(err);
    object_unref(owner);
}

static void test_reject_nonordinary_backing(void)
{
    MemoryRegion backing;

    memset(&backing, 0, sizeof(backing));
    backing.name = "mmio";
    test_assert_backing_rejected(&backing, false, false);

    test_ram_init(&backing, "rom");
    backing.readonly = true;
    test_assert_backing_rejected(&backing, true, false);

    test_ram_init(&backing, "ram-device");
    backing.ram_device = true;
    test_assert_backing_rejected(&backing, false, false);

    memset(&backing, 0, sizeof(backing));
    backing.name = "romd";
    backing.rom_device = true;
    backing.romd_mode = true;
    test_assert_backing_rejected(&backing, false, false);

    test_ram_init(&backing, "readonly-alias");
    test_assert_backing_rejected(&backing, true, false);

    test_ram_init(&backing, "protected-ram");
    test_assert_backing_rejected(&backing, false, true);
    test_protected_ram = NULL;
}

static void test_reject_nondevice_owner(void)
{
    uint8_t descriptor[sizeof(IA64PlatformDescriptor)] = { 0 };
    MemoryRegion ram;
    MemoryRegion rom;
    Object *owner = object_new(TYPE_TEST_ROM_MACHINE);
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    g_assert_false(ia64_platform_desc_install_rom(&rom, owner, "test-owner",
                                                  TEST_GPA, descriptor,
                                                  sizeof(descriptor), &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "DeviceState"));
    g_assert_cmpint(test_add_count, ==, 0);
    error_free(err);
    object_unref(owner);
}

static void test_obscured_rom_rolls_back(void)
{
    uint8_t descriptor[sizeof(IA64PlatformDescriptor)] = { 0 };
    MemoryRegion base_ram;
    MemoryRegion high_priority_ram;
    MemoryRegion rom;
    Object *owner = test_device_new();
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&base_ram, "base-ram");
    test_ram_init(&high_priority_ram, "high-priority-ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE / 2, &base_ram, false);
    test_post_add_obstacle =
        test_map_add(TEST_GPA + IA64_PLATFORM_DESC_MAX_SIZE / 2, 0x100,
                     &high_priority_ram, false);
    test_map_add(TEST_GPA + IA64_PLATFORM_DESC_MAX_SIZE / 2 + 0x100,
                 IA64_PLATFORM_DESC_MAX_SIZE / 2 - 0x100, &base_ram, false);

    g_assert_false(ia64_platform_desc_install_rom(&rom, owner, "test-obscured",
                                                  TEST_GPA, descriptor,
                                                  sizeof(descriptor), &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "obscured"));
    g_assert_cmpint(test_add_count, ==, 1);
    g_assert_cmpint(test_del_count, ==, 1);
    g_assert_cmpint(test_unregister_count, ==, 1);
    g_assert_cmpint(test_finalize_count, ==, 1);
    g_assert_null(test_mapped_rom);
    g_assert_null(test_rom_ptr);
    error_free(err);
    object_unref(owner);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ia64/platform-rom/fragmented-ram",
                    test_install_fragmented_ram);
    g_test_add_func("/ia64/platform-rom/gap", test_reject_gap);
    g_test_add_func("/ia64/platform-rom/nonordinary-backing",
                    test_reject_nonordinary_backing);
    g_test_add_func("/ia64/platform-rom/nondevice-owner",
                    test_reject_nondevice_owner);
    g_test_add_func("/ia64/platform-rom/obscured-rollback",
                    test_obscured_rom_rolls_back);
    return g_test_run();
}
