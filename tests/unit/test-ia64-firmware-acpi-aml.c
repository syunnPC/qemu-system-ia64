/*
 * Host-side tests for the freestanding IA-64 firmware AML emitter.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-acpi-aml.h"
#include "ssdt-platform-devices.h"

#define CHECK(condition) do { if (!(condition)) { return 1; } } while (0)

#define HUGE_RESOURCE_COUNT 87382U
#define ZX6000_PLATFORM_MMIO_BASE 0xff5c0000ULL
#define ZX6000_PLATFORM_MMIO_SIZE 0x00002000ULL

static UINT8 huge_aml[HUGE_RESOURCE_COUNT * 12U + 64U];

static UINT8 ssdt_aml[IA64_SSDT_AML_SIZE];

static UINT16 read_le16(const UINT8 *bytes)
{
    return (UINT16)bytes[0] | (UINT16)bytes[1] << 8;
}

static UINT32 read_le32(const UINT8 *bytes)
{
    return (UINT32)bytes[0] | (UINT32)bytes[1] << 8 |
        (UINT32)bytes[2] << 16 | (UINT32)bytes[3] << 24;
}

static UINT64 read_le64(const UINT8 *bytes)
{
    UINT64 value = 0;
    UINTN i;

    for (i = 0; i < 8U; i++) {
        value |= (UINT64)bytes[i] << (i * 8U);
    }
    return value;
}

static void fill_bytes(UINT8 *bytes, UINTN size, UINT8 value)
{
    UINTN i;

    for (i = 0; i < size; i++) {
        bytes[i] = value;
    }
}

static BOOLEAN bytes_equal(const UINT8 *left, const UINT8 *right,
                           UINTN size)
{
    UINTN i;

    for (i = 0; i < size; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN hp_ccsr_resource(const UINT8 *data, UINT64 base,
                                UINT64 length)
{
    static const UINT8 uuid[16] = {
        0xf9, 0xad, 0xe9, 0x69, 0x4f, 0x92, 0x5f, 0xab,
        0xf6, 0x4a, 0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad,
    };

    return data[0] == 0x84U && read_le16(data + 1U) == 33U &&
        data[3] == 2U && bytes_equal(data + 4U, uuid, sizeof(uuid)) &&
        read_le64(data + 20U) == base &&
        read_le64(data + 28U) == length;
}

static BOOLEAN pkg_length(const UINT8 *aml, UINTN aml_size, UINTN offset,
                          UINTN *encoded_size, UINTN *package_size)
{
    UINT8 lead;
    UINTN following;
    UINTN value;
    UINTN i;

    if (aml == NULL || encoded_size == NULL || package_size == NULL ||
        offset >= aml_size) {
        return 0;
    }
    lead = aml[offset];
    following = lead >> 6;
    if (following == 0) {
        value = lead & 0x3fU;
    } else {
        if (following > 3U || following > aml_size - offset - 1U) {
            return 0;
        }
        value = lead & 0x0fU;
        for (i = 0; i < following; i++) {
            value |= (UINTN)aml[offset + 1U + i] << (4U + i * 8U);
        }
    }
    *encoded_size = following + 1U;
    *package_size = value;
    return value >= *encoded_size && value <= aml_size - offset;
}

static BOOLEAN aml_integer(const UINT8 *aml, UINTN aml_size, UINTN *offset,
                           UINT64 *value)
{
    UINT8 opcode;

    if (aml == NULL || offset == NULL || value == NULL ||
        *offset >= aml_size) {
        return 0;
    }
    opcode = aml[(*offset)++];
    if (opcode <= 1U) {
        *value = opcode;
        return 1;
    }
    if (opcode == 0x0aU && *offset < aml_size) {
        *value = aml[(*offset)++];
        return 1;
    }
    if (opcode == 0x0bU && *offset <= aml_size - 2U) {
        *value = read_le16(aml + *offset);
        *offset += 2U;
        return 1;
    }
    if (opcode == 0x0cU && *offset <= aml_size - 4U) {
        *value = read_le32(aml + *offset);
        *offset += 4U;
        return 1;
    }
    if (opcode == 0x0eU && *offset <= aml_size - 8U) {
        *value = read_le64(aml + *offset);
        *offset += 8U;
        return 1;
    }
    return 0;
}

static UINTN find_name(const UINT8 *aml, UINTN aml_size,
                       const CHAR8 name[5], UINTN start)
{
    UINTN i;

    for (i = start; i + 5U <= aml_size; i++) {
        if (aml[i] == 0x08U && aml[i + 1U] == (UINT8)name[0] &&
            aml[i + 2U] == (UINT8)name[1] &&
            aml[i + 3U] == (UINT8)name[2] &&
            aml[i + 4U] == (UINT8)name[3]) {
            return i + 5U;
        }
    }
    return ~(UINTN)0;
}

static UINTN count_bytes(const UINT8 *aml, UINTN aml_size,
                         const UINT8 *needle, UINTN needle_size)
{
    UINTN count = 0;
    UINTN i;

    if (needle_size == 0 || needle_size > aml_size) {
        return 0;
    }
    for (i = 0; i <= aml_size - needle_size; i++) {
        if (bytes_equal(aml + i, needle, needle_size)) {
            count++;
        }
    }
    return count;
}

static int test_ssdt_legacy_device_parent(void)
{
    static const UINT8 pci0_path[] = {
        0x5cU, 0x2eU, '_', 'S', 'B', '_', 'P', 'C', 'I', '0'
    };
    static const UINT8 sba0_path[] = {
        0x5cU, 0x2eU, '_', 'S', 'B', '_', 'S', 'B', 'A', '0'
    };
    static const CHAR8 sba0[4] = { 'S', 'B', 'A', '0' };
    static const CHAR8 bad_parent[4] = { 's', 'b', 'a', '0' };
    UINTN i;

    for (i = 0; i < sizeof(ssdt_aml); i++) {
        ssdt_aml[i] = mSsdtAmlTemplate[i];
    }
    CHECK(count_bytes(ssdt_aml, sizeof(ssdt_aml),
                      pci0_path, sizeof(pci0_path)) == 1U);
    CHECK(fw_acpi_ssdt_reparent_legacy_devices(
        ssdt_aml, sizeof(ssdt_aml), sba0));
    CHECK(count_bytes(ssdt_aml, sizeof(ssdt_aml),
                      pci0_path, sizeof(pci0_path)) == 0);
    CHECK(count_bytes(ssdt_aml, sizeof(ssdt_aml),
                      sba0_path, sizeof(sba0_path)) == 1U);
    CHECK(!fw_acpi_ssdt_reparent_legacy_devices(
        ssdt_aml, sizeof(ssdt_aml), sba0));
    CHECK(!fw_acpi_ssdt_reparent_legacy_devices(
        ssdt_aml, sizeof(ssdt_aml), bad_parent));
    return 0;
}

static BOOLEAN resource_template(const UINT8 *aml, UINTN aml_size,
                                 UINTN value_offset, UINTN *data_offset,
                                 UINTN *data_size)
{
    UINTN encoded_size;
    UINTN package_size;
    UINTN package_offset;
    UINTN package_end;
    UINTN offset;
    UINT64 declared_size;

    if (value_offset >= aml_size || aml[value_offset] != 0x11U) {
        return 0;
    }
    package_offset = value_offset + 1U;
    if (!pkg_length(aml, aml_size, package_offset,
                    &encoded_size, &package_size)) {
        return 0;
    }
    package_end = package_offset + package_size;
    offset = package_offset + encoded_size;
    if (!aml_integer(aml, package_end, &offset, &declared_size) ||
        declared_size > package_end - offset ||
        declared_size != package_end - offset) {
        return 0;
    }
    *data_offset = offset;
    *data_size = (UINTN)declared_size;
    return 1;
}

static BOOLEAN prt_entry(const UINT8 *aml, UINTN aml_size,
                         UINTN value_offset, UINT64 expected_address,
                         UINT64 expected_pin, UINT64 expected_gsi)
{
    UINTN outer_encoding;
    UINTN outer_size;
    UINTN outer_end;
    UINTN inner_encoding;
    UINTN inner_size;
    UINTN inner_end;
    UINTN offset;
    UINT64 value;

    if (value_offset >= aml_size || aml[value_offset] != 0x12U ||
        !pkg_length(aml, aml_size, value_offset + 1U,
                    &outer_encoding, &outer_size)) {
        return 0;
    }
    outer_end = value_offset + 1U + outer_size;
    offset = value_offset + 1U + outer_encoding;
    if (offset >= outer_end || aml[offset++] != 1U ||
        offset >= outer_end || aml[offset++] != 0x12U ||
        !pkg_length(aml, outer_end, offset, &inner_encoding, &inner_size)) {
        return 0;
    }
    inner_end = offset + inner_size;
    offset += inner_encoding;
    if (offset >= inner_end || aml[offset++] != 4U ||
        !aml_integer(aml, inner_end, &offset, &value) ||
        value != expected_address ||
        !aml_integer(aml, inner_end, &offset, &value) ||
        value != expected_pin ||
        !aml_integer(aml, inner_end, &offset, &value) || value != 0 ||
        !aml_integer(aml, inner_end, &offset, &value) ||
        value != expected_gsi || offset != inner_end ||
        inner_end != outer_end) {
        return 0;
    }
    return 1;
}

static int test_named_objects(void)
{
    static const UINT8 pnp0a03[] = { 0x0c, 0x41, 0xd0, 0x0a, 0x03 };
    UINT8 aml[256];
    FWAcpiAmlBuilder builder;
    UINTN length;
    UINTN scope_encoding;
    UINTN scope_size;
    UINTN scope_body;
    UINTN device_offset;
    UINTN device_encoding;
    UINTN device_size;
    UINTN offset;
    UINT64 value;

    fw_acpi_aml_builder_init(&builder, aml, sizeof(aml));
    CHECK(fw_acpi_aml_scope_begin(&builder, "\\_SB"));
    CHECK(fw_acpi_aml_device_begin(&builder, "PCI0"));
    CHECK(fw_acpi_aml_name(&builder, "_HID"));
    CHECK(fw_acpi_aml_eisa_id(&builder, "PNP0A03"));
    CHECK(fw_acpi_aml_name(&builder, "_UID"));
    CHECK(fw_acpi_aml_integer(&builder, 0x1122334455667788ULL));
    CHECK(fw_acpi_aml_name(&builder, "PKG0"));
    CHECK(fw_acpi_aml_package_begin(&builder, 2));
    CHECK(fw_acpi_aml_integer(&builder, 0));
    CHECK(fw_acpi_aml_integer(&builder, 1));
    CHECK(fw_acpi_aml_package_end(&builder));
    CHECK(fw_acpi_aml_package_end(&builder));
    CHECK(fw_acpi_aml_package_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));

    CHECK(aml[0] == 0x10U &&
          pkg_length(aml, length, 1, &scope_encoding, &scope_size) &&
          1U + scope_size == length);
    scope_body = 1U + scope_encoding;
    CHECK(scope_body + 5U < length && aml[scope_body] == 0x5cU &&
          bytes_equal(aml + scope_body + 1U,
                      (const UINT8 *)"_SB_", 4U));
    device_offset = scope_body + 5U;
    CHECK(aml[device_offset] == 0x5bU &&
          aml[device_offset + 1U] == 0x82U &&
          pkg_length(aml, length, device_offset + 2U,
                     &device_encoding, &device_size) &&
          device_offset + 2U + device_size == length);
    offset = device_offset + 2U + device_encoding;
    CHECK(bytes_equal(aml + offset, (const UINT8 *)"PCI0", 4U));
    offset = find_name(aml, length, "_HID", offset + 4U);
    CHECK(offset != ~(UINTN)0 && offset <= length - sizeof(pnp0a03) &&
          bytes_equal(aml + offset, pnp0a03, sizeof(pnp0a03)));
    offset = find_name(aml, length, "_UID", offset);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0x1122334455667788ULL);
    offset = find_name(aml, length, "PKG0", offset);
    CHECK(offset != ~(UINTN)0 && aml[offset] == 0x12U);
    return 0;
}

static int test_pkg_length_compaction(void)
{
    UINT8 aml[128];
    FWAcpiAmlBuilder builder;
    UINTN length;
    UINTN i;

    fw_acpi_aml_builder_init(&builder, aml, sizeof(aml));
    CHECK(fw_acpi_aml_package_begin(&builder, 61));
    for (i = 0; i < 61U; i++) {
        CHECK(fw_acpi_aml_integer(&builder, 0));
    }
    CHECK(fw_acpi_aml_package_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));
    CHECK(length == 64U && aml[0] == 0x12U && aml[1] == 0x3fU &&
          aml[2] == 61U);

    fw_acpi_aml_builder_init(&builder, aml, sizeof(aml));
    CHECK(fw_acpi_aml_package_begin(&builder, 62));
    for (i = 0; i < 62U; i++) {
        CHECK(fw_acpi_aml_integer(&builder, 0));
    }
    CHECK(fw_acpi_aml_package_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));
    CHECK(length == 66U && aml[0] == 0x12U && aml[1] == 0x41U &&
          aml[2] == 0x04U && aml[3] == 62U);
    return 0;
}

static int test_large_pkg_lengths(void)
{
    UINT8 aml[4200];
    FWAcpiAmlBuilder builder;
    UINTN length;
    UINTN encoded_size;
    UINTN package_size;
    UINTN i;

    fw_acpi_aml_builder_init(&builder, aml, sizeof(aml));
    CHECK(fw_acpi_aml_resource_template_begin(&builder));
    for (i = 0; i < 342U; i++) {
        CHECK(fw_acpi_aml_memory32_fixed(&builder, 1, (UINT32)i, 1));
    }
    CHECK(fw_acpi_aml_resource_template_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));
    CHECK(pkg_length(aml, length, 1, &encoded_size, &package_size) &&
          encoded_size == 3U && package_size + 1U == length);

    fw_acpi_aml_builder_init(&builder, huge_aml, sizeof(huge_aml));
    CHECK(fw_acpi_aml_resource_template_begin(&builder));
    for (i = 0; i < HUGE_RESOURCE_COUNT; i++) {
        CHECK(fw_acpi_aml_memory32_fixed(&builder, 1, (UINT32)i, 1));
    }
    CHECK(fw_acpi_aml_resource_template_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));
    CHECK(pkg_length(huge_aml, length, 1, &encoded_size, &package_size) &&
          encoded_size == 4U && package_size + 1U == length);
    return 0;
}

static int test_resource_descriptors(void)
{
    UINT8 aml[512];
    FWAcpiAmlBuilder builder;
    UINTN length;
    UINTN data_offset;
    UINTN data_size;
    const UINT8 *data;

    fw_acpi_aml_builder_init(&builder, aml, sizeof(aml));
    CHECK(fw_acpi_aml_resource_template_begin(&builder));
    CHECK(fw_acpi_aml_memory32_fixed(&builder, 1,
                                     0xfed00000U, 0x10000U));
    CHECK(fw_acpi_aml_io(&builder, 1, 0x1004U, 0x1004U, 1U, 0x14U));
    CHECK(fw_acpi_aml_word_bus_number(&builder, 0, 0x20, 0x2f, 0, 0x10));
    CHECK(fw_acpi_aml_dword_memory(&builder, 0, 0xa0000U, 0xfffffU,
                                   0, 0x60000U));
    CHECK(fw_acpi_aml_qword_memory(&builder, 0, 0, 0xffffff,
                                   0x90000000, 0x1000000));
    CHECK(fw_acpi_aml_qword_io(&builder, 0, 0x1000, 0x1fff,
                               0 - 0x1000ULL, 0x1000));
    CHECK(fw_acpi_aml_qword_io_to_memory(
        &builder, 0, 0, ZX6000_PLATFORM_MMIO_BASE,
        ZX6000_PLATFORM_MMIO_BASE + ZX6000_PLATFORM_MMIO_SIZE - 1U,
        0, ZX6000_PLATFORM_MMIO_SIZE));
    CHECK(fw_acpi_aml_qword_io_to_memory(
        &builder, 1, 0, 0, 0x1fff, 0x00000ffffc000000ULL,
        0x2000));
    CHECK(fw_acpi_aml_resource_template_end(&builder));
    CHECK(fw_acpi_aml_builder_finish(&builder, &length));
    CHECK(resource_template(aml, length, 0, &data_offset, &data_size));
    CHECK(data_size == 248U);
    data = aml + data_offset;

    CHECK(data[0] == 0x86U && read_le16(data + 1U) == 9U &&
          data[3] == 1U && read_le32(data + 4U) == 0xfed00000U &&
          read_le32(data + 8U) == 0x10000U);
    data += 12U;
    CHECK(data[0] == 0x47U && data[1] == 1U &&
          read_le16(data + 2U) == 0x1004U &&
          read_le16(data + 4U) == 0x1004U && data[6] == 1U &&
          data[7] == 0x14U);
    data += 8U;
    CHECK(data[0] == 0x88U && read_le16(data + 1U) == 13U &&
          data[3] == 2U && data[4] == 0x0cU && data[5] == 0 &&
          read_le16(data + 6U) == 0 &&
          read_le16(data + 8U) == 0x20U &&
          read_le16(data + 10U) == 0x2fU &&
          read_le16(data + 12U) == 0 &&
          read_le16(data + 14U) == 0x10U);
    data += 16U;
    CHECK(data[0] == 0x87U && read_le16(data + 1U) == 23U &&
          data[3] == 0 && data[4] == 0x0cU && data[5] == 1U &&
          read_le32(data + 6U) == 0 &&
          read_le32(data + 10U) == 0xa0000U &&
          read_le32(data + 14U) == 0xfffffU &&
          read_le32(data + 18U) == 0 &&
          read_le32(data + 22U) == 0x60000U);
    data += 26U;
    CHECK(data[0] == 0x8aU && read_le16(data + 1U) == 43U &&
          data[3] == 0 && data[4] == 0x0cU && data[5] == 1U &&
          read_le64(data + 6U) == 0 &&
          read_le64(data + 14U) == 0 &&
          read_le64(data + 22U) == 0xffffffU &&
          read_le64(data + 30U) == 0x90000000U &&
          read_le64(data + 38U) == 0x1000000U);
    data += 46U;
    CHECK(data[0] == 0x8aU && read_le16(data + 1U) == 43U &&
          data[3] == 1U && data[4] == 0x0cU && data[5] == 3U &&
          read_le64(data + 14U) == 0x1000U &&
          read_le64(data + 22U) == 0x1fffU &&
          read_le64(data + 30U) == 0 - 0x1000ULL &&
          read_le64(data + 38U) == 0x1000U);
    data += 46U;
    CHECK(data[0] == 0x8aU && read_le16(data + 1U) == 43U &&
          data[3] == 1U && data[4] == 0x0cU && data[5] == 0x13U &&
          read_le64(data + 14U) == ZX6000_PLATFORM_MMIO_BASE &&
          read_le64(data + 22U) ==
              ZX6000_PLATFORM_MMIO_BASE + ZX6000_PLATFORM_MMIO_SIZE - 1U &&
          read_le64(data + 30U) == 0 &&
          read_le64(data + 38U) == ZX6000_PLATFORM_MMIO_SIZE);
    data += 46U;
    CHECK(data[0] == 0x8aU && read_le16(data + 1U) == 43U &&
          data[3] == 1U && data[4] == 0x0cU && data[5] == 0x33U &&
          read_le64(data + 14U) == 0 &&
          read_le64(data + 22U) == 0x1fffU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x2000U);
    data += 46U;
    CHECK(data[0] == 0x79U && data[1] == 0);
    return 0;
}

static int test_zx6000_namespace(void)
{
    static const UINT8 hwp0001[] = { 0x0c, 0x22, 0xf0, 0x00, 0x01 };
    static const UINT8 hwp0002[] = { 0x0c, 0x22, 0xf0, 0x00, 0x02 };
    static const UINT8 hwp0003[] = { 0x0c, 0x22, 0xf0, 0x00, 0x03 };
    static const UINT8 pnp0a03[] = { 0x0c, 0x41, 0xd0, 0x0a, 0x03 };
    static const UINT8 pnp0a05[] = { 0x0c, 0x41, 0xd0, 0x0a, 0x05 };
    static const UINT8 pnp0c02[] = { 0x0c, 0x41, 0xd0, 0x0c, 0x02 };
    UINT8 aml[FW_ACPI_ZX6000_DSDT_AML_CAPACITY];
    IA64PlatformPciRoot roots[2] = { 0 };
    IA64PlatformPciRoute routes[2] = { 0 };
    UINTN length;
    UINTN offset;
    UINTN data_offset;
    UINTN data_size;
    UINTN scope_encoding;
    UINTN scope_size;
    UINTN s5_encoding;
    UINTN s5_size;
    UINT64 value;
    const UINT8 *data;

    roots[0].Segment = 0;
    roots[0].Bus = 0x20;
    roots[0].BusEnd = 0x2f;
    roots[0].ConfigBase = 0xfed20000U;
    roots[0].Flags = IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO;
    roots[0].IoBase = 0;
    roots[0].IoSize = 0x2000U;
    roots[0].IoTranslationOffset = 0x00000ffffc000000ULL;
    roots[0].Mmio32Base = 0;
    roots[0].Mmio32Size = 0x01000000U;
    roots[0].Mmio32TranslationOffset = 0x90000000U;
    roots[1].Segment = 0;
    roots[1].Bus = 0x40;
    roots[1].BusEnd = 0x4f;
    roots[1].ConfigBase = 0xfed22000U;
    roots[1].Flags = IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO |
                     IA64_PLATFORM_PCI_ROOT_FLAG_AGP |
                     IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY;
    roots[1].IoBase = 0x2000U;
    roots[1].IoSize = 0x2000U;
    roots[1].IoTranslationOffset = 0x00000ffffc000000ULL;
    roots[1].Mmio32Base = 0;
    roots[1].Mmio32Size = 0x01000000U;
    roots[1].Mmio32TranslationOffset = 0xa0000000U;
    routes[0].Segment = 0;
    routes[0].Bus = 0x20;
    routes[0].Device = 1;
    routes[0].Pin = 0;
    routes[0].Gsi = 0;
    routes[1].Segment = 0;
    routes[1].Bus = 0x40;
    routes[1].Device = 1;
    routes[1].Pin = 0;
    routes[1].Gsi = 16;

    CHECK(fw_acpi_build_zx6000_dsdt(aml, sizeof(aml), roots, 2,
                                    routes, 2,
                                    ZX6000_PLATFORM_MMIO_BASE,
                                    ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(length < sizeof(aml) && aml[0] == 0x08U &&
          bytes_equal(aml + 1U, (const UINT8 *)"_S5_", 4U) &&
          aml[5] == 0x12U &&
          pkg_length(aml, length, 6, &s5_encoding, &s5_size));
    offset = 6U + s5_size;
    CHECK(offset < length && aml[offset] == 0x10U &&
          pkg_length(aml, length, offset + 1U,
                     &scope_encoding, &scope_size) &&
          offset + 1U + scope_size == length);
    offset += 1U + scope_encoding;
    CHECK(offset + 5U < length && aml[offset] == 0x5cU &&
          bytes_equal(aml + offset + 1U, (const UINT8 *)"_SB_", 4U));

    CHECK(count_bytes(aml, length, hwp0001, sizeof(hwp0001)) == 1U &&
          count_bytes(aml, length, hwp0002, sizeof(hwp0002)) == 1U &&
          count_bytes(aml, length, hwp0003, sizeof(hwp0003)) == 1U &&
          count_bytes(aml, length, pnp0a03, sizeof(pnp0a03)) == 2U &&
          count_bytes(aml, length, pnp0a05, sizeof(pnp0a05)) == 1U &&
          count_bytes(aml, length, pnp0c02, sizeof(pnp0c02)) == 1U &&
          count_bytes(aml, length, (const UINT8 *)"MBRD", 4U) == 1U &&
          count_bytes(aml, length, (const UINT8 *)"SBA0", 4U) == 1U &&
          count_bytes(aml, length, (const UINT8 *)"PCI0", 4U) == 1U &&
          count_bytes(aml, length, (const UINT8 *)"PCI1", 4U) == 1U &&
          find_name(aml, length, "_FIX", 0) == ~(UINTN)0);

    offset = find_name(aml, length, "_UID", 0);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0);
    offset = find_name(aml, length, "_UID", offset);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0);
    offset = find_name(aml, length, "_UID", offset);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0);
    offset = find_name(aml, length, "_UID", offset);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0x100U);
    CHECK(find_name(aml, length, "_UID", offset) == ~(UINTN)0);

    offset = find_name(aml, length, "_BBN", 0);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0x20U);
    offset = find_name(aml, length, "_BBN", offset);
    CHECK(offset != ~(UINTN)0 && aml_integer(aml, length, &offset, &value) &&
          value == 0x40U);

    offset = find_name(aml, length, "_CRS", 0);
    CHECK(offset != ~(UINTN)0 &&
          resource_template(aml, length, offset, &data_offset, &data_size) &&
          data_size == 46U);
    data = aml + data_offset;
    CHECK(data[0] == 0x86U && read_le16(data + 1U) == 9U &&
          data[3] == 1U &&
          read_le32(data + 4U) == ZX6000_PLATFORM_MMIO_BASE &&
          read_le32(data + 8U) == ZX6000_PLATFORM_MMIO_SIZE);
    data += 12U;
    CHECK(data[0] == 0x47U && data[1] == 1U &&
          read_le16(data + 2U) == IA64_PLATFORM_ACPI_PM1_EVT_OFFSET &&
          read_le16(data + 4U) == IA64_PLATFORM_ACPI_PM1_EVT_OFFSET &&
          data[6] == 1U && data[7] == 4U);
    data += 8U;
    CHECK(data[0] == 0x47U && data[1] == 1U &&
          read_le16(data + 2U) == IA64_PLATFORM_ACPI_PM1_CNT_OFFSET &&
          read_le16(data + 4U) == IA64_PLATFORM_ACPI_PM1_CNT_OFFSET &&
          data[6] == 1U && data[7] == 2U);
    data += 8U;
    CHECK(data[0] == 0x47U && data[1] == 1U &&
          read_le16(data + 2U) == IA64_PLATFORM_ACPI_PM_TMR_OFFSET &&
          read_le16(data + 4U) == IA64_PLATFORM_ACPI_PM_TMR_OFFSET &&
          data[6] == 1U && data[7] == 4U);
    data += 8U;
    CHECK(data[0] == 0x47U && data[1] == 1U &&
          read_le16(data + 2U) == IA64_PLATFORM_ACPI_GPE0_STS_OFFSET &&
          read_le16(data + 4U) == IA64_PLATFORM_ACPI_GPE0_STS_OFFSET &&
          data[6] == 1U &&
          data[7] == IA64_PLATFORM_ACPI_GPE0_LENGTH &&
          data[8] == 0x79U && data[9] == 0);

    offset = find_name(aml, length, "_CRS", offset);
    CHECK(offset != ~(UINTN)0 &&
          resource_template(aml, length, offset, &data_offset, &data_size) &&
          data_size == 50U);
    data = aml + data_offset;
    CHECK(hp_ccsr_resource(data, FW_ACPI_ZX1_SBA_CSR_BASE,
                           FW_ACPI_ZX1_SBA_CSR_SIZE));
    data += 36U;
    CHECK(data[0] == 0x86U && read_le32(data + 4U) ==
          FW_ACPI_ZX1_SBA_CSR_BASE && read_le32(data + 8U) ==
          FW_ACPI_ZX1_SBA_CSR_SIZE && data[12] == 0x79U);

    offset = find_name(aml, length, "_CRS", offset);
    CHECK(offset != ~(UINTN)0 &&
          resource_template(aml, length, offset, &data_offset, &data_size) &&
          data_size == 238U);
    data = aml + data_offset;
    CHECK(hp_ccsr_resource(data, roots[0].ConfigBase,
                           IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE));
    data += 36U;
    CHECK(data[0] == 0x88U && read_le16(data + 8U) == 0x20U &&
          read_le16(data + 10U) == 0x2fU &&
          read_le16(data + 12U) == 0 &&
          read_le16(data + 14U) == 0x10U);
    data += 16U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0 &&
          read_le64(data + 22U) == 0x1cdU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x1ceU);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x1d2U &&
          read_le64(data + 22U) == 0x3afU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x1deU);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x3e0U &&
          read_le64(data + 22U) == 0x1fffU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x1c20U);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 0 &&
          read_le64(data + 14U) == 0 &&
          read_le64(data + 22U) == 0xffffffU &&
          read_le64(data + 30U) == 0x90000000U &&
          read_le64(data + 38U) == 0x01000000U &&
          data[46] == 0x79U && data[47] == 0);

    offset = find_name(aml, length, "_CRS", offset);
    CHECK(offset != ~(UINTN)0 &&
          resource_template(aml, length, offset, &data_offset, &data_size) &&
          data_size == 264U);
    data = aml + data_offset;
    CHECK(hp_ccsr_resource(data, roots[1].ConfigBase,
                           IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE));
    data += 36U;
    CHECK(read_le16(data + 8U) == 0x40U &&
          read_le16(data + 10U) == 0x4fU);
    data += 16U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x2000U &&
          read_le64(data + 22U) == 0x3fffU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x2000U);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x1ceU &&
          read_le64(data + 22U) == 0x1d1U &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x04U);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x3b0U &&
          read_le64(data + 22U) == 0x3dfU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x30U);
    data += 46U;
    CHECK(data[0] == 0x87U && read_le16(data + 1U) == 23U &&
          data[3] == 0 && data[4] == 0x0cU && data[5] == 1U &&
          read_le32(data + 10U) == 0xa0000U &&
          read_le32(data + 14U) == 0xfffffU &&
          read_le32(data + 18U) == 0 &&
          read_le32(data + 22U) == 0x60000U);
    data += 26U;
    CHECK(data[0] == 0x8aU && data[3] == 0 &&
          read_le64(data + 14U) == 0 &&
          read_le64(data + 22U) == 0xffffffU &&
          read_le64(data + 30U) == 0xa0000000U &&
          read_le64(data + 38U) == 0x01000000U);

    offset = find_name(aml, length, "_PRT", 0);
    CHECK(offset != ~(UINTN)0 &&
          prt_entry(aml, length, offset, 0x0001ffffU, 0, 0));
    offset = find_name(aml, length, "_PRT", offset);
    CHECK(offset != ~(UINTN)0 &&
          prt_entry(aml, length, offset, 0x0001ffffU, 0, 16));
    return 0;
}

static int test_optional_root_apertures(void)
{
    static const UINT8 pnp0c02[] = { 0x0c, 0x41, 0xd0, 0x0c, 0x02 };
    UINT8 aml[1024];
    IA64PlatformPciRoot root = { 0 };
    UINTN length;
    UINTN offset;
    UINTN data_offset;
    UINTN data_size;
    const UINT8 *data;

    root.BusEnd = 0;
    root.IoBase = 0x1000U;
    root.IoSize = 0x1000U;
    root.Flags = IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO;
    root.IoTranslationOffset = 0x00000ffffc000000ULL;
    root.Mmio64Base = 0x100000000ULL;
    root.Mmio64Size = 0x200000U;
    root.Mmio64TranslationOffset = 0 - 0x100000000ULL;
    CHECK(fw_acpi_build_zx6000_dsdt(aml, sizeof(aml), &root, 1,
                                    NULL, 0, 0, 0, &length));
    CHECK(count_bytes(aml, length, pnp0c02, sizeof(pnp0c02)) == 0 &&
          find_name(aml, length, "_FIX", 0) == ~(UINTN)0);

    offset = find_name(aml, length, "_CRS", 0);
    CHECK(offset != ~(UINTN)0);
    offset = find_name(aml, length, "_CRS", offset);
    CHECK(offset != ~(UINTN)0 &&
          resource_template(aml, length, offset, &data_offset, &data_size) &&
          data_size == 110U);
    data = aml + data_offset;
    CHECK(data[0] == 0x88U);
    data += 16U;
    CHECK(data[0] == 0x8aU && data[3] == 1U && data[5] == 0x33U &&
          read_le64(data + 14U) == 0x1000U &&
          read_le64(data + 22U) == 0x1fffU &&
          read_le64(data + 30U) == 0x00000ffffc000000ULL &&
          read_le64(data + 38U) == 0x1000U);
    data += 46U;
    CHECK(data[0] == 0x8aU && data[3] == 0 && data[5] == 1U &&
          read_le64(data + 14U) == 0x100000000ULL &&
          read_le64(data + 22U) == 0x1001fffffULL &&
          read_le64(data + 30U) == 0 - 0x100000000ULL &&
          read_le64(data + 38U) == 0x200000U &&
          data[46] == 0x79U && data[47] == 0);

    offset = find_name(aml, length, "_PRT", 0);
    CHECK(offset == ~(UINTN)0);
    return 0;
}

static int test_maximum_descriptor_arrays(void)
{
    UINT8 aml[FW_ACPI_ZX6000_DSDT_AML_CAPACITY + 1U];
    IA64PlatformPciRoot roots[IA64_PLATFORM_MAX_PCI_ROOTS] = { 0 };
    IA64PlatformPciRoute routes[IA64_PLATFORM_MAX_PCI_ROUTES] = { 0 };
    UINTN length;
    UINTN failed_length = 1;
    UINTN root;
    UINTN i;

    for (i = 0; i < FW_ARRAY_SIZE(roots); i++) {
        roots[i].Bus = (UINT8)(i * 0x10U);
        roots[i].BusEnd = (UINT8)(roots[i].Bus + 0x0fU);
        roots[i].Mmio32Base = (UINT64)i << 24;
        roots[i].Mmio32Size = 0x01000000U;
        roots[i].Mmio32TranslationOffset = (UINT64)i << 32;
    }
    for (i = 0; i < FW_ARRAY_SIZE(routes); i++) {
        root = i % FW_ARRAY_SIZE(roots);
        routes[i].Bus = roots[root].Bus;
        routes[i].Device = (UINT8)(i / FW_ARRAY_SIZE(roots));
        routes[i].Pin = (UINT8)(i & 3U);
        routes[i].Gsi = (UINT32)i;
    }
    CHECK(fw_acpi_build_zx6000_dsdt(
        aml, FW_ACPI_ZX6000_DSDT_AML_CAPACITY,
        roots, FW_ARRAY_SIZE(roots), routes, FW_ARRAY_SIZE(routes),
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE,
        &length));
    CHECK(length > 0 && length < FW_ACPI_ZX6000_DSDT_AML_CAPACITY);

    fill_bytes(aml, sizeof(aml), 0xa5U);
    CHECK(!fw_acpi_build_zx6000_dsdt(
        aml, length - 1U, roots, FW_ARRAY_SIZE(roots),
        routes, FW_ARRAY_SIZE(routes),
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE,
        &failed_length));
    CHECK(failed_length == 0 && aml[length - 1U] == 0xa5U &&
          aml[length] == 0xa5U);
    return 0;
}

static int test_failure_paths(void)
{
    UINT8 storage[10];
    FWAcpiAmlBuilder builder;
    IA64PlatformPciRoot roots[2] = { 0 };
    IA64PlatformPciRoot *root = &roots[0];
    IA64PlatformPciRoute route = { 0 };
    UINTN length = 1;
    UINTN before;

    fill_bytes(storage, sizeof(storage), 0x5aU);
    fw_acpi_aml_builder_init(&builder, storage, 8);
    CHECK(fw_acpi_aml_name(&builder, "_HID"));
    CHECK(!fw_acpi_aml_eisa_id(&builder, "PNP0A03"));
    before = fw_acpi_aml_builder_length(&builder);
    CHECK(!fw_acpi_aml_integer(&builder, 0) &&
          !fw_acpi_aml_builder_ok(&builder) &&
          fw_acpi_aml_builder_length(&builder) == before &&
          storage[8] == 0x5aU && storage[9] == 0x5aU);

    root->BusEnd = 0;
    route.Bus = 1;
    CHECK(!fw_acpi_build_zx6000_dsdt(storage, sizeof(storage),
                                     root, 1, &route, 1,
                                     ZX6000_PLATFORM_MMIO_BASE,
                                     ZX6000_PLATFORM_MMIO_SIZE, &length) &&
          length == 0);
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root,
        IA64_PLATFORM_MAX_PCI_ROOTS + 1U, NULL, 0,
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, &route,
        IA64_PLATFORM_MAX_PCI_ROUTES + 1U,
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        ZX6000_PLATFORM_MMIO_BASE, 0, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        0, ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE / 2U,
        &length));
    roots[0].Flags = IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY;
    roots[1].Flags = IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY;
    roots[1].Bus = 1;
    roots[1].BusEnd = 1;
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), roots, 2, NULL, 0,
        ZX6000_PLATFORM_MMIO_BASE, ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        ZX6000_PLATFORM_MMIO_BASE + 1U, ZX6000_PLATFORM_MMIO_SIZE,
        &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        0x100000000ULL, ZX6000_PLATFORM_MMIO_SIZE, &length));
    CHECK(!fw_acpi_build_zx6000_dsdt(
        storage, sizeof(storage), root, 1, NULL, 0,
        0xfffff000ULL, ZX6000_PLATFORM_MMIO_SIZE, &length));
    return 0;
}

int main(void)
{
    return test_ssdt_legacy_device_parent() || test_named_objects() ||
        test_pkg_length_compaction() ||
        test_large_pkg_lengths() || test_resource_descriptors() ||
        test_zx6000_namespace() || test_optional_root_apertures() ||
        test_maximum_descriptor_arrays() || test_failure_paths();
}
