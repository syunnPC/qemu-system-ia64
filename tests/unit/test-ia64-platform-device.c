/*
 * IA-64 platform descriptor device tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev.h"
#include "hw/ia64/ia64_platform.h"
#include "hw/ia64/ia64_ras_abi.h"
#include "migration/vmstate.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#define TYPE_TEST_PLATFORM_MEMORY_REGION \
    "test-ia64-platform-device-memory-region"

#define TEST_GPA (4 * IA64_PLATFORM_DESC_ALIGNMENT)
#define TEST_MAX_SECTIONS 3
#define TEST_LEGACY_IO_BASE 0x0000000ffc000000ULL

typedef struct TestMapSection {
    hwaddr base;
    uint64_t size;
    MemoryRegion *mr;
    bool readonly;
} TestMapSection;

typedef struct TestDescriptorFixture {
    IA64PlatformDescriptor header;
    IA64PlatformRamRange ram[2];
    IA64PlatformPciRoot root;
    IA64PlatformIoSapic sapic;
    IA64PlatformPciRoute route;
    IA64PlatformDescriptorArrays arrays;
} TestDescriptorFixture;

static MemoryRegion test_sysmem;
static TestMapSection test_sections[TEST_MAX_SECTIONS];
static size_t test_section_count;
static TestMapSection *test_post_add_obstacle;
static MemoryRegion *test_mapped_rom;
static hwaddr test_mapped_gpa;
static uint8_t *test_rom_ptr;
static int test_add_count;
static int test_del_count;
static int test_unregister_count;
static int test_finalize_count;

static void test_memory_region_finalize(Object *obj)
{
    MemoryRegion *mr = (MemoryRegion *)obj;

    g_assert_null(test_mapped_rom);
    g_clear_pointer(&test_rom_ptr, g_free);
    g_clear_pointer((char **)&mr->name, g_free);
    test_finalize_count++;
}

static const TypeInfo test_memory_region_info = {
    .name = TYPE_TEST_PLATFORM_MEMORY_REGION,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(MemoryRegion),
    .instance_finalize = test_memory_region_finalize,
};

static void test_register_types(void)
{
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
    test_add_count = 0;
    test_del_count = 0;
    test_unregister_count = 0;
    test_finalize_count = 0;
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

MemoryRegionSection memory_region_find(MemoryRegion *mr,
                                       hwaddr addr, uint64_t size)
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

        if (test_post_add_obstacle &&
            addr >= test_post_add_obstacle->base &&
            addr < test_post_add_obstacle->base +
                   test_post_add_obstacle->size) {
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
    (void)mr;
    return false;
}

bool memory_region_init_rom(MemoryRegion *mr, Object *owner,
                            const char *name, uint64_t size, Error **errp)
{
    (void)errp;
    g_assert_cmpuint(size, ==, IA64_PLATFORM_DESC_MAX_SIZE);
    g_assert_null(test_rom_ptr);

    object_initialize(mr, sizeof(*mr), TYPE_TEST_PLATFORM_MEMORY_REGION);
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
                                         MemoryRegion *subregion,
                                         int priority)
{
    g_assert_true(mr == &test_sysmem);
    g_assert_null(test_mapped_rom);
    g_assert_cmpint(priority, >, 0);
    test_mapped_rom = subregion;
    test_mapped_gpa = offset;
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

/* Define VMSD dependencies for this unit test. */
const VMStateInfo vmstate_info_uint8_equal = {
    .name = "test uint8 equal",
};

const VMStateInfo vmstate_info_uint64_equal = {
    .name = "test uint64 equal",
};

static Object *test_parent_new(void)
{
    Object *parent = object_new(TYPE_CONTAINER);

    object_property_add_child(object_get_root(), "ia64-platform-device-test",
                              parent);
    return parent;
}

static void test_parent_destroy(Object *parent)
{
    object_unparent(parent);
    object_unref(parent);
}

static void test_fixture_init(TestDescriptorFixture *fixture)
{
    IA64PlatformDescriptor *header;
    IA64PlatformRamRange *ram;
    IA64PlatformPciRoot *root;
    IA64PlatformIoSapic *sapic;
    IA64PlatformPciRoute *route;

    memset(fixture, 0, sizeof(*fixture));
    header = &fixture->header;
    ram = fixture->ram;
    root = &fixture->root;
    sapic = &fixture->sapic;
    route = &fixture->route;

    header->Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC);
    header->FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION);
    header->PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_I2000);
    header->Flags = cpu_to_le32(IA64_PLATFORM_FLAG_NO_MCFG |
                                IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                                IA64_PLATFORM_FLAG_FAMILY_HP_I2000 |
                                IA64_PLATFORM_FLAG_PCI_CF8);
    header->RamSize = cpu_to_le64(2 * GiB);
    header->LowRamEnd = cpu_to_le64(1 * GiB);
    header->FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE);
    header->FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE);
    header->ProcessorCount = cpu_to_le32(1);
    header->SocketCount = cpu_to_le32(1);
    header->CoresPerSocket = cpu_to_le32(1);
    header->ThreadsPerCore = cpu_to_le32(1);
    header->PhysicalAddressBits = cpu_to_le32(
        IA64_PLATFORM_I2000_PHYS_ADDR_BITS);
    header->MaxSockets = cpu_to_le32(2);
    header->MaxCoresPerSocket = cpu_to_le32(1);
    header->MaxThreadsPerCore = cpu_to_le32(1);
    header->MaxPciRoots = cpu_to_le32(1);
    header->PciRootIdentity = cpu_to_le32(
        IA64_PLATFORM_PCI_ROOT_IDENTITY_GENERIC);
    header->NumaNodeCount = cpu_to_le32(1);
    header->NumaNode[0].ProcessorCount = cpu_to_le32(1);
    header->NumaNode[0].RamRangeMask = cpu_to_le32(3);
    header->NumaNode[0].Distance[0] = 10;
    header->LegacyIoBase = cpu_to_le64(TEST_LEGACY_IO_BASE);
    header->LegacyIoSize = cpu_to_le64(64 * MiB);
    header->LocalSapicBase = cpu_to_le64(0xfee00000ULL);
    header->LocalSapicSize = cpu_to_le64(2 * MiB);
    header->ConsoleBase = cpu_to_le64(TEST_LEGACY_IO_BASE + 0x003f8000ULL);
    header->ConsoleRegisterStride = cpu_to_le32(1);
    header->ConsoleClockHz = cpu_to_le32(1843200);
    header->ConsoleIrq = cpu_to_le32(4);
    header->NvramBase = cpu_to_le64(0xfff00000ULL);
    header->NvramSize = cpu_to_le64(64 * KiB);
    header->RtcBase = cpu_to_le64(0xffef0000ULL);
    header->RtcSize = cpu_to_le64(8 * KiB);
    header->RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE);
    header->RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE);

    ram[0].Size = cpu_to_le64(1 * GiB);
    ram[1].Base = cpu_to_le64(4 * GiB);
    ram[1].Size = cpu_to_le64(1 * GiB);

    root->BusEnd = 0x0f;
    root->ConfigType = IA64_PLATFORM_PCI_CONFIG_CF8_CFC;
    root->IoSize = cpu_to_le64(64 * KiB);
    root->Mmio32Base = cpu_to_le64(0x80000000ULL);
    root->Mmio32Size = cpu_to_le64(256 * MiB);
    root->DmaSize = cpu_to_le64(2 * GiB);

    sapic->Base = cpu_to_le64(0xfec00000ULL);
    sapic->RedirectionEntries = cpu_to_le32(64);
    sapic->Version = cpu_to_le32(0x21);

    route->Device = 1;
    route->Gsi = cpu_to_le32(16);

    fixture->arrays = (IA64PlatformDescriptorArrays) {
        .ram_ranges = ram,
        .ram_range_count = G_N_ELEMENTS(fixture->ram),
        .pci_roots = root,
        .pci_root_count = 1,
        .io_sapics = sapic,
        .io_sapic_count = 1,
        .pci_routes = route,
        .pci_route_count = 1,
    };
}

static void test_create_success(void)
{
    TestDescriptorFixture fixture;
    MemoryRegion ram;
    Object *parent;
    IA64PlatformDescriptorDevice *device;
    IA64PlatformFirmwareArgs args = { 0 };
    IA64PlatformDescriptor *descriptor;
    IA64PlatformPciRoot *stored_root;
    DeviceClass *dc;
    const VMStateField *fields;
    Error *err = NULL;
    size_t i;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    test_fixture_init(&fixture);
    parent = test_parent_new();

    device = ia64_platform_desc_device_create(
        parent, "descriptor", TEST_GPA, &fixture.header, &fixture.arrays,
        &err);
    g_assert_nonnull(device);
    g_assert_null(err);
    g_assert_cmpint(test_add_count, ==, 1);

    dc = DEVICE_CLASS(object_class_by_name(
        TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE));
    g_assert_false(dc->user_creatable);
    g_assert_false(dc->hotpluggable);
    g_assert_nonnull(dc->vmsd);
    g_assert_cmpint(dc->vmsd->version_id, ==, 1);
    fields = dc->vmsd->fields;
    g_assert_cmpstr(fields[0].name, ==, "gpa");
    g_assert_true(fields[0].info == &vmstate_info_uint64_equal);
    g_assert_cmpstr(fields[1].name, ==, "storage.bytes");
    g_assert_true(fields[1].info == &vmstate_info_uint8_equal);
    g_assert_cmpint(fields[1].num, ==, IA64_PLATFORM_DESC_MAX_SIZE);
    g_assert_cmpuint(fields[1].flags & VMS_ARRAY, !=, 0);

    g_assert_true(ia64_platform_desc_device_get_firmware_args(device, &args));
    descriptor = (IA64PlatformDescriptor *)test_rom_ptr;
    g_assert_cmphex(args.descriptor_gpa, ==, TEST_GPA);
    g_assert_cmpuint(args.descriptor_size, ==,
                     le32_to_cpu(descriptor->TotalSize));
    g_assert_cmphex(args.firmware_compat_flags, ==, 0);
    g_assert_cmpuint(args.platform_id, ==, IA64_PLATFORM_ID_HP_I2000);
    g_assert_true(ia64_platform_desc_validate(
        descriptor, args.descriptor_size, args.platform_id, &err));
    g_assert_null(err);

    for (i = args.descriptor_size; i < IA64_PLATFORM_DESC_MAX_SIZE; i++) {
        g_assert_cmpuint(test_rom_ptr[i], ==, 0);
    }

    /* The creator must not retain pointers to caller-owned inputs. */
    fixture.header.PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_ZX6000);
    fixture.root.BusEnd = 0xff;
    stored_root = (IA64PlatformPciRoot *)(test_rom_ptr +
        le32_to_cpu(descriptor->PciRootOffset));
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_I2000);
    g_assert_cmpuint(stored_root->BusEnd, ==, 0x0f);

    ia64_platform_desc_device_destroy(device);
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_del_count, ==, 1);
    g_assert_cmpint(test_unregister_count, ==, 1);
    g_assert_cmpint(test_finalize_count, ==, 1);
    g_assert_null(test_mapped_rom);
    g_assert_null(test_rom_ptr);
    test_parent_destroy(parent);
}

static void test_invalid_descriptor(void)
{
    TestDescriptorFixture fixture;
    MemoryRegion ram;
    Object *parent;
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    test_fixture_init(&fixture);
    fixture.header.ConsoleClockHz = cpu_to_le32(
        IA64_PLATFORM_UART_MIN_CLOCK_HZ - 1U);
    parent = test_parent_new();

    g_assert_null(ia64_platform_desc_device_create(
        parent, "descriptor", TEST_GPA, &fixture.header, &fixture.arrays,
        &err));
    g_assert_nonnull(err);
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_add_count, ==, 0);
    g_assert_cmpint(test_finalize_count, ==, 0);
    error_free(err);
    test_parent_destroy(parent);
}

static void test_rootless_parent(void)
{
    TestDescriptorFixture fixture;
    MemoryRegion ram;
    Object *parent;
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    test_fixture_init(&fixture);
    parent = object_new(TYPE_CONTAINER);

    g_assert_null(ia64_platform_desc_device_create(
        parent, "descriptor", TEST_GPA, &fixture.header, &fixture.arrays,
        &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "QOM tree"));
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_add_count, ==, 0);
    g_assert_cmpint(test_finalize_count, ==, 0);
    error_free(err);
    object_unref(parent);
}

static void test_unbacked_mapping(void)
{
    TestDescriptorFixture fixture;
    Object *parent;
    Error *err = NULL;

    test_map_reset();
    test_fixture_init(&fixture);
    parent = test_parent_new();

    g_assert_null(ia64_platform_desc_device_create(
        parent, "descriptor", TEST_GPA, &fixture.header, &fixture.arrays,
        &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "no RAM backing"));
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_add_count, ==, 0);
    g_assert_cmpint(test_del_count, ==, 0);
    g_assert_cmpint(test_unregister_count, ==, 0);
    g_assert_cmpint(test_finalize_count, ==, 0);
    error_free(err);
    test_parent_destroy(parent);
}

static void test_efi_unaligned_ram_rejected(void)
{
    const hwaddr gpa = 1 * GiB - 2 * IA64_PLATFORM_DESC_ALIGNMENT;
    const uint64_t low_ram_end =
        1 * GiB - IA64_PLATFORM_DESC_ALIGNMENT;
    TestDescriptorFixture fixture;
    MemoryRegion ram;
    Object *parent;
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_map_add(gpa, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    test_fixture_init(&fixture);
    fixture.header.LowRamEnd = cpu_to_le64(low_ram_end);
    fixture.header.RamSize = cpu_to_le64(low_ram_end + 1 * GiB);
    fixture.ram[0].Size = cpu_to_le64(low_ram_end);
    parent = test_parent_new();

    /* A 4 KiB-only RAM end cannot be represented in the IA-64 EFI map. */
    g_assert_null(ia64_platform_desc_device_create(
        parent, "descriptor", gpa, &fixture.header, &fixture.arrays, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "RAM"));
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_add_count, ==, 0);
    g_assert_cmpint(test_finalize_count, ==, 0);
    error_free(err);
    test_parent_destroy(parent);
}

static void test_obscured_mapping_rollback(void)
{
    TestDescriptorFixture fixture;
    MemoryRegion ram;
    MemoryRegion obstacle;
    Object *parent;
    Error *err = NULL;

    test_map_reset();
    test_ram_init(&ram, "ram");
    test_ram_init(&obstacle, "obstacle");
    test_map_add(TEST_GPA, IA64_PLATFORM_DESC_MAX_SIZE, &ram, false);
    test_post_add_obstacle = test_map_add(
        TEST_GPA + IA64_PLATFORM_DESC_MAX_SIZE / 2, 0x100,
        &obstacle, false);
    test_fixture_init(&fixture);
    parent = test_parent_new();

    g_assert_null(ia64_platform_desc_device_create(
        parent, "descriptor", TEST_GPA, &fixture.header, &fixture.arrays,
        &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "obscured"));
    g_assert_null(object_resolve_path_component(parent, "descriptor"));
    g_assert_cmpint(test_add_count, ==, 1);
    g_assert_cmpint(test_del_count, ==, 1);
    g_assert_cmpint(test_unregister_count, ==, 1);
    g_assert_cmpint(test_finalize_count, ==, 1);
    g_assert_null(test_mapped_rom);
    g_assert_null(test_rom_ptr);
    error_free(err);
    test_parent_destroy(parent);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ia64/platform-device/create-success",
                    test_create_success);
    g_test_add_func("/ia64/platform-device/invalid-descriptor",
                    test_invalid_descriptor);
    g_test_add_func("/ia64/platform-device/rootless-parent",
                    test_rootless_parent);
    g_test_add_func("/ia64/platform-device/unbacked-mapping",
                    test_unbacked_mapping);
    g_test_add_func("/ia64/platform-device/efi-unaligned-ram",
                    test_efi_unaligned_ram_rejected);
    g_test_add_func("/ia64/platform-device/obscured-rollback",
                    test_obscured_mapping_rollback);
    return g_test_run();
}
