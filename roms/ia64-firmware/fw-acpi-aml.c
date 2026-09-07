/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded AML construction for the freestanding IA-64 firmware.
 */

#include "fw-acpi-aml.h"

#define FW_AML_SCOPE_OP                 0x10U
#define FW_AML_BUFFER_OP                0x11U
#define FW_AML_PACKAGE_OP               0x12U
#define FW_AML_EXT_OP_PREFIX            0x5bU
#define FW_AML_DEVICE_OP                0x82U
#define FW_AML_NAME_OP                  0x08U
#define FW_AML_BYTE_PREFIX              0x0aU
#define FW_AML_WORD_PREFIX              0x0bU
#define FW_AML_DWORD_PREFIX             0x0cU
#define FW_AML_QWORD_PREFIX             0x0eU
#define FW_AML_ROOT_CHAR                0x5cU

#define FW_AML_IO_TAG                   0x47U
#define FW_AML_MEMORY32_FIXED_TAG       0x86U
#define FW_AML_VENDOR_LONG_TAG          0x84U
#define FW_AML_DWORD_ADDRESS_TAG        0x87U
#define FW_AML_WORD_ADDRESS_TAG         0x88U
#define FW_AML_QWORD_ADDRESS_TAG        0x8aU
#define FW_AML_END_TAG                  0x79U

#define FW_AML_RESOURCE_MEMORY          0U
#define FW_AML_RESOURCE_IO              1U
#define FW_AML_RESOURCE_BUS             2U
#define FW_AML_RESOURCE_PRODUCER_FIXED  0x0cU
#define FW_AML_MEMORY_READ_WRITE        0x01U
#define FW_AML_IO_ENTIRE_RANGE          0x03U
#define FW_AML_IO_TYPE_TRANSLATION      0x10U
#define FW_AML_IO_SPARSE_TRANSLATION    0x20U

#define FW_AML_FRAME_PACKAGE            0U
#define FW_AML_FRAME_RESOURCE_TEMPLATE  1U
#define FW_AML_MAX_INTEGER_SIZE         9U

#define FW_AML_HP_CCSR_SUBTYPE          2U
#define FW_AML_HP_CCSR_DATA_SIZE        16U

static const UINT8 fw_aml_hp_ccsr_uuid[16] = {
    0xf9U, 0xadU, 0xe9U, 0x69U, 0x4fU, 0x92U, 0x5fU, 0xabU,
    0xf6U, 0x4aU, 0x24U, 0xd2U, 0x01U, 0x37U, 0x0eU, 0xadU,
};

static BOOLEAN fw_aml_fail(FWAcpiAmlBuilder *builder)
{
    if (builder != NULL) {
        builder->Failed = 1;
    }
    return 0;
}

void fw_acpi_aml_builder_init(FWAcpiAmlBuilder *builder, UINT8 *buffer,
                              UINTN capacity)
{
    UINTN i;

    if (builder == NULL) {
        return;
    }
    builder->Buffer = buffer;
    builder->Capacity = capacity;
    builder->Length = 0;
    builder->Depth = 0;
    builder->Failed = buffer == NULL;
    for (i = 0; i < FW_ACPI_AML_MAX_PACKAGE_DEPTH; i++) {
        builder->Frame[i].LengthOffset = 0;
        builder->Frame[i].DataOffset = 0;
        builder->Frame[i].Kind = FW_AML_FRAME_PACKAGE;
    }
}

BOOLEAN fw_acpi_aml_builder_ok(const FWAcpiAmlBuilder *builder)
{
    return builder != NULL && !builder->Failed;
}

UINTN fw_acpi_aml_builder_length(const FWAcpiAmlBuilder *builder)
{
    return builder == NULL ? 0 : builder->Length;
}

BOOLEAN fw_acpi_aml_builder_finish(FWAcpiAmlBuilder *builder, UINTN *length)
{
    if (length != NULL) {
        *length = 0;
    }
    if (builder == NULL || length == NULL) {
        return fw_aml_fail(builder);
    }
    if (builder->Failed || builder->Depth != 0) {
        return fw_aml_fail(builder);
    }
    *length = builder->Length;
    return 1;
}

static BOOLEAN fw_aml_reserve(FWAcpiAmlBuilder *builder, UINTN size,
                              UINT8 **bytes)
{
    if (bytes != NULL) {
        *bytes = NULL;
    }
    if (builder == NULL || builder->Failed || bytes == NULL ||
        builder->Buffer == NULL || builder->Length > builder->Capacity ||
        size > builder->Capacity - builder->Length) {
        return fw_aml_fail(builder);
    }
    *bytes = builder->Buffer + builder->Length;
    builder->Length += size;
    return 1;
}

static void fw_aml_write_le16(UINT8 *bytes, UINT16 value)
{
    bytes[0] = (UINT8)value;
    bytes[1] = (UINT8)(value >> 8);
}

static void fw_aml_write_le32(UINT8 *bytes, UINT32 value)
{
    bytes[0] = (UINT8)value;
    bytes[1] = (UINT8)(value >> 8);
    bytes[2] = (UINT8)(value >> 16);
    bytes[3] = (UINT8)(value >> 24);
}

static void fw_aml_write_le64(UINT8 *bytes, UINT64 value)
{
    UINTN i;

    for (i = 0; i < 8U; i++) {
        bytes[i] = (UINT8)(value >> (i * 8U));
    }
}

static BOOLEAN fw_aml_name_lead(CHAR8 character)
{
    return character == '_' ||
        (character >= 'A' && character <= 'Z');
}

static BOOLEAN fw_aml_name_char(CHAR8 character)
{
    return fw_aml_name_lead(character) ||
        (character >= '0' && character <= '9');
}

/* This firmware namespace uses an optional root prefix and one NameSeg. */
static BOOLEAN fw_aml_namestring(const CHAR8 *name, UINT8 bytes[5],
                                 UINTN *length)
{
    UINTN input = 0;
    UINTN output = 0;
    UINTN segment = 0;

    if (name == NULL || bytes == NULL || length == NULL) {
        return 0;
    }
    if (name[input] == (CHAR8)FW_AML_ROOT_CHAR) {
        bytes[output++] = FW_AML_ROOT_CHAR;
        input++;
    }
    while (segment < 4U && name[input] != '\0') {
        CHAR8 character = name[input++];

        if ((segment == 0 && !fw_aml_name_lead(character)) ||
            (segment != 0 && !fw_aml_name_char(character))) {
            return 0;
        }
        bytes[output++] = (UINT8)character;
        segment++;
    }
    if (segment == 0 || name[input] != '\0') {
        return 0;
    }
    while (segment++ < 4U) {
        bytes[output++] = '_';
    }
    *length = output;
    return 1;
}

static BOOLEAN fw_aml_begin_named_package(FWAcpiAmlBuilder *builder,
                                          const UINT8 *opcode,
                                          UINTN opcode_size,
                                          const CHAR8 *name)
{
    UINT8 namestring[5];
    UINTN name_size;
    UINTN i;
    UINT8 *bytes;
    FWAcpiAmlFrame *frame;

    if (builder == NULL || builder->Failed ||
        !fw_aml_namestring(name, namestring, &name_size) ||
        builder->Depth >= FW_ACPI_AML_MAX_PACKAGE_DEPTH ||
        opcode == NULL || opcode_size == 0 ||
        !fw_aml_reserve(builder, opcode_size + 4U + name_size, &bytes)) {
        return fw_aml_fail(builder);
    }
    for (i = 0; i < opcode_size; i++) {
        bytes[i] = opcode[i];
    }
    frame = &builder->Frame[builder->Depth++];
    frame->LengthOffset = builder->Length - name_size - 4U;
    frame->DataOffset = 0;
    frame->Kind = FW_AML_FRAME_PACKAGE;
    for (i = 0; i < 4U; i++) {
        builder->Buffer[frame->LengthOffset + i] = 0;
    }
    for (i = 0; i < name_size; i++) {
        builder->Buffer[frame->LengthOffset + 4U + i] = namestring[i];
    }
    return 1;
}

BOOLEAN fw_acpi_aml_scope_begin(FWAcpiAmlBuilder *builder,
                                const CHAR8 *name)
{
    static const UINT8 opcode[] = { FW_AML_SCOPE_OP };

    return fw_aml_begin_named_package(builder, opcode, sizeof(opcode), name);
}

BOOLEAN fw_acpi_aml_device_begin(FWAcpiAmlBuilder *builder,
                                 const CHAR8 *name)
{
    static const UINT8 opcode[] = {
        FW_AML_EXT_OP_PREFIX, FW_AML_DEVICE_OP
    };

    return fw_aml_begin_named_package(builder, opcode, sizeof(opcode), name);
}

BOOLEAN fw_acpi_aml_package_begin(FWAcpiAmlBuilder *builder,
                                  UINT8 element_count)
{
    UINT8 *bytes;
    FWAcpiAmlFrame *frame;
    UINTN i;

    if (builder == NULL || builder->Failed ||
        builder->Depth >= FW_ACPI_AML_MAX_PACKAGE_DEPTH ||
        !fw_aml_reserve(builder, 6U, &bytes)) {
        return fw_aml_fail(builder);
    }
    bytes[0] = FW_AML_PACKAGE_OP;
    for (i = 1; i < 5U; i++) {
        bytes[i] = 0;
    }
    bytes[5] = element_count;
    frame = &builder->Frame[builder->Depth++];
    frame->LengthOffset = builder->Length - 5U;
    frame->DataOffset = 0;
    frame->Kind = FW_AML_FRAME_PACKAGE;
    return 1;
}

static UINTN fw_aml_pkg_length_size(UINTN body_size, UINTN *package_size)
{
    static const UINT32 maximum[] = {
        0x3fU, 0x0fffU, 0x000fffffU, 0x0fffffffU
    };
    UINTN size;

    if (package_size == NULL) {
        return 0;
    }
    for (size = 1; size <= FW_ARRAY_SIZE(maximum); size++) {
        if (body_size <= (UINTN)maximum[size - 1U] - size) {
            *package_size = body_size + size;
            return size;
        }
    }
    return 0;
}

static void fw_aml_write_pkg_length(UINT8 *bytes, UINTN size, UINTN value)
{
    UINTN i;

    if (size == 1U) {
        bytes[0] = (UINT8)value;
        return;
    }
    bytes[0] = (UINT8)(((size - 1U) << 6) | (value & 0x0fU));
    for (i = 1; i < size; i++) {
        bytes[i] = (UINT8)(value >> (4U + (i - 1U) * 8U));
    }
}

static BOOLEAN fw_aml_package_end_kind(FWAcpiAmlBuilder *builder,
                                       UINT8 kind)
{
    FWAcpiAmlFrame *frame;
    UINTN body_offset;
    UINTN body_size;
    UINTN package_size;
    UINTN encoded_size;
    UINTN i;

    if (builder == NULL || builder->Failed || builder->Depth == 0) {
        return fw_aml_fail(builder);
    }
    frame = &builder->Frame[builder->Depth - 1U];
    body_offset = frame->LengthOffset + 4U;
    if (frame->Kind != kind || body_offset > builder->Length) {
        return fw_aml_fail(builder);
    }
    body_size = builder->Length - body_offset;
    encoded_size = fw_aml_pkg_length_size(body_size, &package_size);
    if (encoded_size == 0) {
        return fw_aml_fail(builder);
    }
    for (i = 0; i < body_size; i++) {
        builder->Buffer[frame->LengthOffset + encoded_size + i] =
            builder->Buffer[body_offset + i];
    }
    builder->Length -= 4U - encoded_size;
    fw_aml_write_pkg_length(builder->Buffer + frame->LengthOffset,
                            encoded_size, package_size);
    builder->Depth--;
    return 1;
}

BOOLEAN fw_acpi_aml_package_end(FWAcpiAmlBuilder *builder)
{
    return fw_aml_package_end_kind(builder, FW_AML_FRAME_PACKAGE);
}

BOOLEAN fw_acpi_aml_name(FWAcpiAmlBuilder *builder, const CHAR8 *name)
{
    UINT8 namestring[5];
    UINTN name_size;
    UINTN i;
    UINT8 *bytes;

    if (builder == NULL || builder->Failed ||
        !fw_aml_namestring(name, namestring, &name_size) ||
        !fw_aml_reserve(builder, 1U + name_size, &bytes)) {
        return fw_aml_fail(builder);
    }
    bytes[0] = FW_AML_NAME_OP;
    for (i = 0; i < name_size; i++) {
        bytes[1U + i] = namestring[i];
    }
    return 1;
}

static UINTN fw_aml_integer_size(UINT64 value)
{
    if (value <= 1U) {
        return 1U;
    }
    if (value <= 0xffU) {
        return 2U;
    }
    if (value <= 0xffffU) {
        return 3U;
    }
    if (value <= 0xffffffffU) {
        return 5U;
    }
    return FW_AML_MAX_INTEGER_SIZE;
}

static void fw_aml_write_integer(UINT8 *bytes, UINT64 value)
{
    if (value <= 1U) {
        bytes[0] = (UINT8)value;
    } else if (value <= 0xffU) {
        bytes[0] = FW_AML_BYTE_PREFIX;
        bytes[1] = (UINT8)value;
    } else if (value <= 0xffffU) {
        bytes[0] = FW_AML_WORD_PREFIX;
        fw_aml_write_le16(bytes + 1U, (UINT16)value);
    } else if (value <= 0xffffffffU) {
        bytes[0] = FW_AML_DWORD_PREFIX;
        fw_aml_write_le32(bytes + 1U, (UINT32)value);
    } else {
        bytes[0] = FW_AML_QWORD_PREFIX;
        fw_aml_write_le64(bytes + 1U, value);
    }
}

BOOLEAN fw_acpi_aml_integer(FWAcpiAmlBuilder *builder, UINT64 value)
{
    UINT8 *bytes;
    UINTN size = fw_aml_integer_size(value);

    if (!fw_aml_reserve(builder, size, &bytes)) {
        return 0;
    }
    fw_aml_write_integer(bytes, value);
    return 1;
}

static BOOLEAN fw_aml_hex_digit(CHAR8 character, UINT8 *value)
{
    if (character >= '0' && character <= '9') {
        *value = (UINT8)(character - '0');
        return 1;
    }
    if (character >= 'A' && character <= 'F') {
        *value = (UINT8)(character - 'A' + 10);
        return 1;
    }
    return 0;
}

static BOOLEAN fw_aml_string_size(const CHAR8 *string, UINTN maximum,
                                  UINTN *size)
{
    UINTN i;

    if (string == NULL || size == NULL) {
        return 0;
    }
    for (i = 0; i <= maximum; i++) {
        if (string[i] == '\0') {
            *size = i;
            return 1;
        }
    }
    return 0;
}

BOOLEAN fw_acpi_aml_eisa_id(FWAcpiAmlBuilder *builder, const CHAR8 *id)
{
    UINT32 value;
    UINT8 digit[4];
    UINT8 *bytes;
    UINTN id_size;
    UINTN i;

    if (builder == NULL || builder->Failed ||
        !fw_aml_string_size(id, 7U, &id_size) || id_size != 7U ||
        id[0] < 'A' || id[0] > 'Z' ||
        id[1] < 'A' || id[1] > 'Z' ||
        id[2] < 'A' || id[2] > 'Z') {
        return fw_aml_fail(builder);
    }
    for (i = 0; i < FW_ARRAY_SIZE(digit); i++) {
        if (!fw_aml_hex_digit(id[3U + i], &digit[i])) {
            return fw_aml_fail(builder);
        }
    }
    value = ((UINT32)(id[0] - '@') << 26) |
        ((UINT32)(id[1] - '@') << 21) |
        ((UINT32)(id[2] - '@') << 16) |
        ((UINT32)digit[0] << 12) | ((UINT32)digit[1] << 8) |
        ((UINT32)digit[2] << 4) | digit[3];
    if (!fw_aml_reserve(builder, 5U, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_DWORD_PREFIX;
    bytes[1] = (UINT8)(value >> 24);
    bytes[2] = (UINT8)(value >> 16);
    bytes[3] = (UINT8)(value >> 8);
    bytes[4] = (UINT8)value;
    return 1;
}

BOOLEAN fw_acpi_aml_resource_template_begin(FWAcpiAmlBuilder *builder)
{
    UINT8 *bytes;
    FWAcpiAmlFrame *frame;
    UINTN i;

    if (builder == NULL || builder->Failed ||
        builder->Depth >= FW_ACPI_AML_MAX_PACKAGE_DEPTH ||
        !fw_aml_reserve(builder, 1U + 4U + FW_AML_MAX_INTEGER_SIZE,
                        &bytes)) {
        return fw_aml_fail(builder);
    }
    bytes[0] = FW_AML_BUFFER_OP;
    for (i = 1; i < 1U + 4U + FW_AML_MAX_INTEGER_SIZE; i++) {
        bytes[i] = 0;
    }
    frame = &builder->Frame[builder->Depth++];
    frame->LengthOffset = builder->Length - 4U - FW_AML_MAX_INTEGER_SIZE;
    frame->DataOffset = builder->Length;
    frame->Kind = FW_AML_FRAME_RESOURCE_TEMPLATE;
    return 1;
}

BOOLEAN fw_acpi_aml_resource_template_end(FWAcpiAmlBuilder *builder)
{
    FWAcpiAmlFrame *frame;
    UINT8 *end_tag;
    UINTN integer_offset;
    UINTN integer_size;
    UINTN data_size;
    UINTN i;

    if (builder == NULL || builder->Failed || builder->Depth == 0) {
        return fw_aml_fail(builder);
    }
    frame = &builder->Frame[builder->Depth - 1U];
    if (frame->Kind != FW_AML_FRAME_RESOURCE_TEMPLATE ||
        frame->DataOffset < FW_AML_MAX_INTEGER_SIZE ||
        frame->DataOffset > builder->Length ||
        !fw_aml_reserve(builder, 2U, &end_tag)) {
        return fw_aml_fail(builder);
    }
    end_tag[0] = FW_AML_END_TAG;
    end_tag[1] = 0;
    data_size = builder->Length - frame->DataOffset;
    integer_size = fw_aml_integer_size(data_size);
    integer_offset = frame->DataOffset - FW_AML_MAX_INTEGER_SIZE;
    for (i = 0; i < data_size; i++) {
        builder->Buffer[integer_offset + integer_size + i] =
            builder->Buffer[frame->DataOffset + i];
    }
    builder->Length -= FW_AML_MAX_INTEGER_SIZE - integer_size;
    fw_aml_write_integer(builder->Buffer + integer_offset, data_size);
    return fw_aml_package_end_kind(builder,
                                   FW_AML_FRAME_RESOURCE_TEMPLATE);
}

BOOLEAN fw_acpi_aml_memory32_fixed(FWAcpiAmlBuilder *builder,
                                   BOOLEAN read_write, UINT32 base,
                                   UINT32 size)
{
    UINT8 *bytes;

    if (!fw_aml_reserve(builder, 12U, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_MEMORY32_FIXED_TAG;
    fw_aml_write_le16(bytes + 1U, 9U);
    bytes[3] = read_write ? 1U : 0U;
    fw_aml_write_le32(bytes + 4U, base);
    fw_aml_write_le32(bytes + 8U, size);
    return 1;
}

BOOLEAN fw_acpi_aml_io(FWAcpiAmlBuilder *builder, BOOLEAN decode16,
                       UINT16 minimum, UINT16 maximum, UINT8 alignment,
                       UINT8 length)
{
    UINT8 *bytes;

    if (maximum < minimum || length == 0 ||
        !fw_aml_reserve(builder, 8U, &bytes)) {
        return fw_aml_fail(builder);
    }
    bytes[0] = FW_AML_IO_TAG;
    bytes[1] = decode16 ? 1U : 0U;
    fw_aml_write_le16(bytes + 2U, minimum);
    fw_aml_write_le16(bytes + 4U, maximum);
    bytes[6] = alignment;
    bytes[7] = length;
    return 1;
}

/*
 * HP CCSR locates HWP0001/HWP0002/HWP0003 CSR apertures.  Its Large Vendor
 * descriptor contains a subtype, UUID, and little-endian base and length.
 */
static BOOLEAN fw_aml_hp_ccsr(FWAcpiAmlBuilder *builder, UINT64 base,
                              UINT64 length)
{
    UINT8 *bytes;
    UINTN i;

    if (!fw_aml_reserve(builder, 3U + 1U + sizeof(fw_aml_hp_ccsr_uuid) +
                        FW_AML_HP_CCSR_DATA_SIZE, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_VENDOR_LONG_TAG;
    fw_aml_write_le16(bytes + 1U,
                      1U + sizeof(fw_aml_hp_ccsr_uuid) +
                      FW_AML_HP_CCSR_DATA_SIZE);
    bytes[3] = FW_AML_HP_CCSR_SUBTYPE;
    for (i = 0; i < sizeof(fw_aml_hp_ccsr_uuid); i++) {
        bytes[4U + i] = fw_aml_hp_ccsr_uuid[i];
    }
    fw_aml_write_le64(bytes + 20U, base);
    fw_aml_write_le64(bytes + 28U, length);
    return 1;
}

BOOLEAN fw_acpi_aml_word_bus_number(FWAcpiAmlBuilder *builder,
                                    UINT16 granularity, UINT16 minimum,
                                    UINT16 maximum, UINT16 translation,
                                    UINT16 length)
{
    UINT8 *bytes;

    if (!fw_aml_reserve(builder, 16U, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_WORD_ADDRESS_TAG;
    fw_aml_write_le16(bytes + 1U, 13U);
    bytes[3] = FW_AML_RESOURCE_BUS;
    bytes[4] = FW_AML_RESOURCE_PRODUCER_FIXED;
    bytes[5] = 0;
    fw_aml_write_le16(bytes + 6U, granularity);
    fw_aml_write_le16(bytes + 8U, minimum);
    fw_aml_write_le16(bytes + 10U, maximum);
    fw_aml_write_le16(bytes + 12U, translation);
    fw_aml_write_le16(bytes + 14U, length);
    return 1;
}

static BOOLEAN fw_aml_dword_address(FWAcpiAmlBuilder *builder,
                                    UINT8 resource_type, UINT8 type_flags,
                                    UINT32 granularity, UINT32 minimum,
                                    UINT32 maximum, UINT32 translation,
                                    UINT32 length)
{
    UINT8 *bytes;

    if (!fw_aml_reserve(builder, 26U, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_DWORD_ADDRESS_TAG;
    fw_aml_write_le16(bytes + 1U, 23U);
    bytes[3] = resource_type;
    bytes[4] = FW_AML_RESOURCE_PRODUCER_FIXED;
    bytes[5] = type_flags;
    fw_aml_write_le32(bytes + 6U, granularity);
    fw_aml_write_le32(bytes + 10U, minimum);
    fw_aml_write_le32(bytes + 14U, maximum);
    fw_aml_write_le32(bytes + 18U, translation);
    fw_aml_write_le32(bytes + 22U, length);
    return 1;
}

BOOLEAN fw_acpi_aml_dword_memory(FWAcpiAmlBuilder *builder,
                                 UINT32 granularity, UINT32 minimum,
                                 UINT32 maximum, UINT32 translation,
                                 UINT32 length)
{
    return fw_aml_dword_address(builder, FW_AML_RESOURCE_MEMORY,
                                FW_AML_MEMORY_READ_WRITE, granularity,
                                minimum, maximum, translation, length);
}

static BOOLEAN fw_aml_qword_address(FWAcpiAmlBuilder *builder,
                                    UINT8 resource_type, UINT8 type_flags,
                                    UINT64 granularity, UINT64 minimum,
                                    UINT64 maximum, UINT64 translation,
                                    UINT64 length)
{
    UINT8 *bytes;

    if (!fw_aml_reserve(builder, 46U, &bytes)) {
        return 0;
    }
    bytes[0] = FW_AML_QWORD_ADDRESS_TAG;
    fw_aml_write_le16(bytes + 1U, 43U);
    bytes[3] = resource_type;
    bytes[4] = FW_AML_RESOURCE_PRODUCER_FIXED;
    bytes[5] = type_flags;
    fw_aml_write_le64(bytes + 6U, granularity);
    fw_aml_write_le64(bytes + 14U, minimum);
    fw_aml_write_le64(bytes + 22U, maximum);
    fw_aml_write_le64(bytes + 30U, translation);
    fw_aml_write_le64(bytes + 38U, length);
    return 1;
}

BOOLEAN fw_acpi_aml_qword_memory(FWAcpiAmlBuilder *builder,
                                 UINT64 granularity, UINT64 minimum,
                                 UINT64 maximum, UINT64 translation,
                                 UINT64 length)
{
    return fw_aml_qword_address(builder, FW_AML_RESOURCE_MEMORY,
                                FW_AML_MEMORY_READ_WRITE, granularity,
                                minimum, maximum, translation, length);
}

BOOLEAN fw_acpi_aml_qword_io(FWAcpiAmlBuilder *builder,
                             UINT64 granularity, UINT64 minimum,
                             UINT64 maximum, UINT64 translation,
                             UINT64 length)
{
    return fw_aml_qword_address(builder, FW_AML_RESOURCE_IO,
                                FW_AML_IO_ENTIRE_RANGE, granularity,
                                minimum, maximum, translation, length);
}

BOOLEAN fw_acpi_aml_qword_io_to_memory(FWAcpiAmlBuilder *builder,
                                       BOOLEAN sparse_translation,
                                       UINT64 granularity, UINT64 minimum,
                                       UINT64 maximum, UINT64 translation,
                                       UINT64 length)
{
    UINT8 flags = FW_AML_IO_ENTIRE_RANGE | FW_AML_IO_TYPE_TRANSLATION;

    if (sparse_translation) {
        flags |= FW_AML_IO_SPARSE_TRANSLATION;
    }
    return fw_aml_qword_address(builder, FW_AML_RESOURCE_IO, flags,
                                granularity, minimum, maximum, translation,
                                length);
}

BOOLEAN fw_acpi_ssdt_reparent_legacy_devices(UINT8 *aml, UINTN length,
                                             const CHAR8 parent[4])
{
    static const UINT8 pci0_path[] = {
        FW_AML_ROOT_CHAR, 0x2eU, '_', 'S', 'B', '_', 'P', 'C', 'I', '0'
    };
    UINTN matches = 0;
    UINTN i;
    UINTN j;

    if (aml == NULL || parent == NULL || length < sizeof(pci0_path) ||
        !fw_aml_name_lead(parent[0])) {
        return 0;
    }
    for (j = 1; j < 4U; j++) {
        if (!fw_aml_name_char(parent[j])) {
            return 0;
        }
    }
    for (i = 0; i <= length - sizeof(pci0_path); i++) {
        for (j = 0; j < sizeof(pci0_path); j++) {
            if (aml[i + j] != pci0_path[j]) {
                break;
            }
        }
        if (j == sizeof(pci0_path)) {
            matches++;
        }
    }
    if (matches != 1U) {
        return 0;
    }
    for (i = 0; i <= length - sizeof(pci0_path); i++) {
        for (j = 0; j < sizeof(pci0_path); j++) {
            if (aml[i + j] != pci0_path[j]) {
                break;
            }
        }
        if (j == sizeof(pci0_path)) {
            for (j = 0; j < 4U; j++) {
                aml[i + 6U + j] = (UINT8)parent[j];
            }
        }
    }
    return 1;
}

static BOOLEAN fw_aml_range_valid(UINT64 base, UINT64 size)
{
    return size == 0 || base <= ~(UINT64)0 - (size - 1U);
}

static BOOLEAN fw_aml_memory32_range_valid(UINT64 base, UINT64 size)
{
    if (size == 0) {
        return base == 0;
    }
    return base != 0 && size == IA64_PLATFORM_ACPI_PM_SIZE &&
        (base & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) == 0 &&
        base <= 0xffffffffULL && base <= 0x100000000ULL - size;
}

static BOOLEAN fw_aml_zx6000_inputs_valid(
    const IA64PlatformPciRoot *roots, UINTN root_count,
    const IA64PlatformPciRoute *routes, UINTN route_count,
    UINT64 platform_mmio_base, UINT64 platform_mmio_size)
{
    UINTN vga_root_count = 0;
    UINTN i;
    UINTN j;

    if (roots == NULL || root_count == 0 ||
        root_count > IA64_PLATFORM_MAX_PCI_ROOTS ||
        route_count > IA64_PLATFORM_MAX_PCI_ROUTES ||
        (route_count != 0 && routes == NULL) ||
        !fw_aml_memory32_range_valid(platform_mmio_base,
                                     platform_mmio_size)) {
        return 0;
    }
    for (i = 0; i < root_count; i++) {
        const IA64PlatformPciRoot *root = &roots[i];

        if (root->BusEnd < root->Bus ||
            (root->Flags & ~IA64_PLATFORM_PCI_ROOT_KNOWN_FLAGS) != 0 ||
            ((root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO) != 0 ?
             root->IoSize == 0 || root->IoTranslationOffset == 0 :
             root->IoTranslationOffset != 0) ||
            !fw_aml_range_valid(root->IoBase, root->IoSize) ||
            !fw_aml_range_valid(root->Mmio32Base, root->Mmio32Size) ||
            !fw_aml_range_valid(root->Mmio64Base, root->Mmio64Size)) {
            return 0;
        }
        if ((root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY) != 0) {
            vga_root_count++;
        }
        for (j = 0; j < i; j++) {
            if (root->Segment == roots[j].Segment &&
                root->Bus == roots[j].Bus) {
                return 0;
            }
        }
    }
    if (vga_root_count > 1U) {
        return 0;
    }
    for (i = 0; i < route_count; i++) {
        const IA64PlatformPciRoute *route = &routes[i];
        UINTN matches = 0;

        if (route->Device > 0x1fU || route->Pin > 3U ||
            route->Reserved0 != 0 || route->Reserved1 != 0 ||
            route->Flags != 0) {
            return 0;
        }
        for (j = 0; j < root_count; j++) {
            if (route->Segment == roots[j].Segment &&
                route->Bus == roots[j].Bus) {
                matches++;
            }
        }
        if (matches != 1U) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN fw_aml_name_integer(FWAcpiAmlBuilder *builder,
                                   const CHAR8 *name, UINT64 value)
{
    return fw_acpi_aml_name(builder, name) &&
        fw_acpi_aml_integer(builder, value);
}

static BOOLEAN fw_aml_name_eisa_id(FWAcpiAmlBuilder *builder,
                                   const CHAR8 *name, const CHAR8 *id)
{
    return fw_acpi_aml_name(builder, name) &&
        fw_acpi_aml_eisa_id(builder, id);
}

static void fw_aml_root_name(UINTN index, CHAR8 name[5])
{
    static const CHAR8 digit[] = "0123456789ABCDEF";

    name[0] = 'P';
    name[1] = 'C';
    name[2] = 'I';
    name[3] = digit[index];
    name[4] = '\0';
}

UINT32 fw_acpi_zx6000_root_uid(UINTN root_index)
{
    /* The zx6000 ACPI root UIDs omit 0x500. */
    return (UINT32)((root_index < 5U ? root_index : root_index + 1U) << 8);
}

UINT32 fw_acpi_hp_root_uid(const IA64PlatformPciRoot *root,
                           UINTN root_index, UINTN root_count)
{
    if (root_count == 5U) {
        return root->Rope << 8;
    }
    return fw_acpi_zx6000_root_uid(root_index);
}

static UINTN fw_aml_root_route_count(
    const IA64PlatformPciRoot *root,
    const IA64PlatformPciRoute *routes, UINTN route_count)
{
    UINTN count = 0;
    UINTN i;

    for (i = 0; i < route_count; i++) {
        if (routes[i].Segment == root->Segment &&
            routes[i].Bus == root->Bus) {
            count++;
        }
    }
    return count;
}

typedef struct {
    UINT64 Base;
    UINT64 Size;
} FWLegacyVgaIoRange;

static const FWLegacyVgaIoRange fw_legacy_vga_io_ranges[] = {
    { 0x1ceU, 0x04U },
    { 0x3b0U, 0x30U },
};

static BOOLEAN fw_aml_root_io_range(FWAcpiAmlBuilder *builder,
                                    const IA64PlatformPciRoot *root,
                                    UINT64 base, UINT64 size)
{
    UINT64 maximum = base + size - 1U;

    return (root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO) != 0 ?
        fw_acpi_aml_qword_io_to_memory(
            builder, 1, 0, base, maximum,
            root->IoTranslationOffset, size) :
        fw_acpi_aml_qword_io(
            builder, 0, base, maximum,
            root->IoTranslationOffset, size);
}

static BOOLEAN fw_aml_root_io_resources(FWAcpiAmlBuilder *builder,
                                        const IA64PlatformPciRoot *root,
                                        BOOLEAN has_legacy_vga)
{
    BOOLEAN owner =
        (root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY) != 0;
    BOOLEAN native_owner = owner;
    UINT64 cursor = root->IoBase;
    UINT64 end = root->IoBase + root->IoSize;
    UINTN i;

    for (i = 0; native_owner &&
                i < FW_ARRAY_SIZE(fw_legacy_vga_io_ranges); i++) {
        UINT64 base = fw_legacy_vga_io_ranges[i].Base;
        UINT64 range_end = base + fw_legacy_vga_io_ranges[i].Size;

        native_owner = base >= cursor && range_end <= end;
    }
    if (!has_legacy_vga || native_owner) {
        return root->IoSize == 0 ||
            fw_aml_root_io_range(builder, root, root->IoBase, root->IoSize);
    }
    for (i = 0; i < FW_ARRAY_SIZE(fw_legacy_vga_io_ranges); i++) {
        UINT64 base = fw_legacy_vga_io_ranges[i].Base;
        UINT64 range_end = base + fw_legacy_vga_io_ranges[i].Size;

        if (cursor < end && cursor < base) {
            UINT64 fragment_end = end < base ? end : base;

            if (!fw_aml_root_io_range(builder, root, cursor,
                                      fragment_end - cursor)) {
                return 0;
            }
            cursor = fragment_end;
        }
        if (cursor < end && cursor < range_end && end > base) {
            cursor = end < range_end ? end : range_end;
        }
    }
    if (cursor < end &&
        !fw_aml_root_io_range(builder, root, cursor, end - cursor)) {
        return 0;
    }
    if (owner) {
        for (i = 0; i < FW_ARRAY_SIZE(fw_legacy_vga_io_ranges); i++) {
            if (!fw_aml_root_io_range(
                    builder, root, fw_legacy_vga_io_ranges[i].Base,
                    fw_legacy_vga_io_ranges[i].Size)) {
                return 0;
            }
        }
    }
    return 1;
}

static BOOLEAN fw_aml_root_resources(FWAcpiAmlBuilder *builder,
                                     const IA64PlatformPciRoot *root,
                                     BOOLEAN has_legacy_vga)
{
    UINT64 maximum;

    if (!fw_acpi_aml_name(builder, "_CRS") ||
        !fw_acpi_aml_resource_template_begin(builder) ||
        (root->ConfigBase != 0 &&
         !fw_aml_hp_ccsr(builder, root->ConfigBase,
                         IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE)) ||
        !fw_acpi_aml_word_bus_number(
            builder, 0, root->Bus, root->BusEnd, 0,
            (UINT16)((UINT16)root->BusEnd - root->Bus + 1U))) {
        return 0;
    }
    if (!fw_aml_root_io_resources(builder, root, has_legacy_vga)) {
        return 0;
    }
    /* The legacy VGA aperture is a 32-bit memory resource. */
    if ((root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY) != 0 &&
        !fw_acpi_aml_dword_memory(builder, 0, 0xa0000U, 0xfffffU,
                                  0, 0x60000U)) {
        return 0;
    }
    if (root->Mmio32Size != 0) {
        maximum = root->Mmio32Base + root->Mmio32Size - 1U;
        if (!fw_acpi_aml_qword_memory(
                builder, 0, root->Mmio32Base, maximum,
                root->Mmio32TranslationOffset, root->Mmio32Size)) {
            return 0;
        }
    }
    if (root->Mmio64Size != 0) {
        maximum = root->Mmio64Base + root->Mmio64Size - 1U;
        if (!fw_acpi_aml_qword_memory(
                builder, 0, root->Mmio64Base, maximum,
                root->Mmio64TranslationOffset, root->Mmio64Size)) {
            return 0;
        }
    }
    return fw_acpi_aml_resource_template_end(builder);
}

static BOOLEAN fw_aml_root_prt(FWAcpiAmlBuilder *builder,
                               const IA64PlatformPciRoot *root,
                               const IA64PlatformPciRoute *routes,
                               UINTN route_count)
{
    UINTN count = fw_aml_root_route_count(root, routes, route_count);
    UINTN i;

    if (count == 0) {
        return 1;
    }

    if (!fw_acpi_aml_name(builder, "_PRT") ||
        !fw_acpi_aml_package_begin(builder, (UINT8)count)) {
        return 0;
    }
    for (i = 0; i < route_count; i++) {
        const IA64PlatformPciRoute *route = &routes[i];
        UINT64 address;

        if (route->Segment != root->Segment || route->Bus != root->Bus) {
            continue;
        }
        address = ((UINT64)route->Device << 16) | 0xffffU;
        if (!fw_acpi_aml_package_begin(builder, 4) ||
            !fw_acpi_aml_integer(builder, address) ||
            !fw_acpi_aml_integer(builder, route->Pin) ||
            !fw_acpi_aml_integer(builder, 0) ||
            !fw_acpi_aml_integer(builder, route->Gsi) ||
            !fw_acpi_aml_package_end(builder)) {
            return 0;
        }
    }
    return fw_acpi_aml_package_end(builder);
}

static BOOLEAN fw_aml_zx6000_acpi_pm_resource(FWAcpiAmlBuilder *builder,
                                              UINT64 base, UINT64 size)
{
    return fw_acpi_aml_device_begin(builder, "MBRD") &&
        fw_aml_name_eisa_id(builder, "_HID", "PNP0C02") &&
        fw_aml_name_integer(builder, "_UID", 0) &&
        fw_acpi_aml_name(builder, "_CRS") &&
        fw_acpi_aml_resource_template_begin(builder) &&
        fw_acpi_aml_memory32_fixed(builder, 1, (UINT32)base,
                                   (UINT32)size) &&
        fw_acpi_aml_io(builder, 1,
                       IA64_PLATFORM_ACPI_PM1_EVT_OFFSET,
                       IA64_PLATFORM_ACPI_PM1_EVT_OFFSET, 1, 4) &&
        fw_acpi_aml_io(builder, 1,
                       IA64_PLATFORM_ACPI_PM1_CNT_OFFSET,
                       IA64_PLATFORM_ACPI_PM1_CNT_OFFSET, 1, 2) &&
        fw_acpi_aml_io(builder, 1,
                       IA64_PLATFORM_ACPI_PM_TMR_OFFSET,
                       IA64_PLATFORM_ACPI_PM_TMR_OFFSET, 1, 4) &&
        fw_acpi_aml_io(builder, 1,
                       IA64_PLATFORM_ACPI_GPE0_STS_OFFSET,
                       IA64_PLATFORM_ACPI_GPE0_STS_OFFSET, 1,
                       IA64_PLATFORM_ACPI_GPE0_LENGTH) &&
        fw_acpi_aml_resource_template_end(builder) &&
        fw_acpi_aml_package_end(builder);
}

static BOOLEAN fw_aml_zx6000_root(
    FWAcpiAmlBuilder *builder, const IA64PlatformPciRoot *root,
    UINTN root_index, UINTN root_count, BOOLEAN has_legacy_vga,
    const IA64PlatformPciRoute *routes, UINTN route_count)
{
    CHAR8 name[5];

    fw_aml_root_name(root_index, name);
    return fw_acpi_aml_device_begin(builder, name) &&
        fw_aml_name_eisa_id(
            builder, "_HID",
            (root->Flags & IA64_PLATFORM_PCI_ROOT_FLAG_AGP) != 0 ?
            "HWP0003" : "HWP0002") &&
        fw_aml_name_eisa_id(builder, "_CID", "PNP0A03") &&
        fw_aml_name_integer(builder, "_UID",
                            fw_acpi_hp_root_uid(root, root_index,
                                                root_count)) &&
        fw_aml_name_integer(builder, "_SEG", root->Segment) &&
        fw_aml_name_integer(builder, "_BBN", root->Bus) &&
        fw_aml_name_integer(builder, "_CCA", 1) &&
        fw_aml_root_resources(builder, root, has_legacy_vga) &&
        fw_aml_root_prt(builder, root, routes, route_count) &&
        fw_acpi_aml_package_end(builder);
}

BOOLEAN fw_acpi_build_zx6000_dsdt(
    UINT8 *buffer, UINTN capacity,
    const IA64PlatformPciRoot *roots, UINTN root_count,
    const IA64PlatformPciRoute *routes, UINTN route_count,
    UINT64 platform_mmio_base, UINT64 platform_mmio_size,
    UINTN *length)
{
    FWAcpiAmlBuilder builder;
    BOOLEAN has_legacy_vga = 0;
    UINTN i;

    if (length != NULL) {
        *length = 0;
    }

    for (i = 0; i < root_count; i++) {
        if ((roots[i].Flags &
             IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY) != 0) {
            has_legacy_vga = 1;
        }
    }
    if (buffer == NULL || length == NULL ||
        !fw_aml_zx6000_inputs_valid(roots, root_count,
                                    routes, route_count,
                                    platform_mmio_base,
                                    platform_mmio_size)) {
        return 0;
    }

    fw_acpi_aml_builder_init(&builder, buffer, capacity);
    if (!fw_acpi_aml_name(&builder, "_S5") ||
        !fw_acpi_aml_package_begin(&builder, 4) ||
        !fw_acpi_aml_integer(&builder, 0) ||
        !fw_acpi_aml_integer(&builder, 0) ||
        !fw_acpi_aml_integer(&builder, 0) ||
        !fw_acpi_aml_integer(&builder, 0) ||
        !fw_acpi_aml_package_end(&builder) ||
        !fw_acpi_aml_scope_begin(&builder, "\\_SB") ||
        (platform_mmio_size != 0 &&
         !fw_aml_zx6000_acpi_pm_resource(&builder, platform_mmio_base,
                                         platform_mmio_size)) ||
        !fw_acpi_aml_device_begin(&builder, "SBA0") ||
        !fw_aml_name_eisa_id(&builder, "_HID", "HWP0001") ||
        /* PNP0A05 enables enumeration of the nested HWP0002 PCI roots. */
        !fw_aml_name_eisa_id(&builder, "_CID", "PNP0A05") ||
        !fw_aml_name_integer(&builder, "_UID", 0) ||
        !fw_aml_name_integer(&builder, "_CCA", 1) ||
        !fw_acpi_aml_name(&builder, "_CRS") ||
        !fw_acpi_aml_resource_template_begin(&builder) ||
        !fw_aml_hp_ccsr(&builder, FW_ACPI_ZX1_SBA_CSR_BASE,
                        FW_ACPI_ZX1_SBA_CSR_SIZE) ||
        !fw_acpi_aml_memory32_fixed(&builder, 1,
                                    FW_ACPI_ZX1_SBA_CSR_BASE,
                                    FW_ACPI_ZX1_SBA_CSR_SIZE) ||
        !fw_acpi_aml_resource_template_end(&builder)) {
        return 0;
    }
    for (i = 0; i < root_count; i++) {
        if (!fw_aml_zx6000_root(&builder, &roots[i], i, root_count,
                                has_legacy_vga,
                                routes, route_count)) {
            return 0;
        }
    }
    if (!fw_acpi_aml_package_end(&builder) ||
        !fw_acpi_aml_package_end(&builder)) {
        return 0;
    }
    return fw_acpi_aml_builder_finish(&builder, length);
}
