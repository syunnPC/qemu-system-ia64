/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define I2000_IMAGE_BASE              0x0000000008000000ULL
#define I2000_CONVENTIONAL_BASE       0x0000000000820000ULL
#define I2000_CONVENTIONAL_END        0x0000000005000000ULL
#define I2000_FIRMWARE_BASE           0x0000000000100000ULL
#define I2000_FIRMWARE_END            0x0000000000300000ULL
#define I2000_FIRMWARE_APERTURE_BASE  0x00000000ff000000ULL
#define I2000_FIRMWARE_APERTURE_END   0x0000000100000000ULL
#define I2000_DIRECT_ALIAS_OFFSET     0x0000000080000000ULL
#define IA64_REGION7_BASE             0xe000000000000000ULL
#define I2000_DIRECT_ALIAS_RR         ((1ULL << 8) | (13ULL << 2) | 1ULL)
#define I2000_DIRECT_ALIAS_ITIR       (13ULL << 2)
#define I2000_DIRECT_ALIAS_VALUE      0x4d65726365644149ULL

#define SAL_DESCRIPTOR_ENTRYPOINT     0U
#define SAL_DESCRIPTOR_MEMORY         1U
#define SAL_DESCRIPTOR_FEATURES       2U
#define SAL_DESCRIPTOR_TR             3U
#define SAL_DESCRIPTOR_AP_WAKE        5U
#define SAL_MEMORY_ATTRIBUTE_WB       0U
#define SAL_MEMORY_ATTRIBUTE_UC       4U
#define SAL_PAGE_ACCESS_RX            1U
#define SAL_PAGE_ACCESS_RW            2U
#define SAL_MEMORY_SUPPORTS_WB        (1U << 0)
#define SAL_MEMORY_SUPPORTS_UC        (1U << 1)
#define SAL_MEMORY_TYPE_REGULAR       0U
#define SAL_MEMORY_TYPE_FIRMWARE      4U
#define SAL_MEMORY_USAGE_UNSPECIFIED  0U
#define SAL_MEMORY_USAGE_PAL_CODE     1U
#define SAL_MEMORY_USAGE_BOOT_CODE    2U
#define SAL_MEMORY_USAGE_RUNTIME_CODE 4U
#define SAL_MEMORY_USAGE_RUNTIME_DATA 5U
#define SAL_GET_STATE_INFO_SIZE       0x01000002ULL
#define SAL_STATE_INFO_MAX_SIZE       512U
#define SAL_SUCCESS                   0ULL
#define SAL_TABLE_LENGTH              0x170U
#define SAL_TABLE_ENTRY_COUNT         9U
#define SAL_MEMORY_DESCRIPTOR_COUNT   5U

#define ACPI_SDT_HEADER_SIZE          36U
#define ACPI_MADT_HEADER_SIZE         44U
#define ACPI_MAX_TABLE_SIZE           4096U
#define ACPI_FADT_SIGNATURE           0x50434146U
#define ACPI_MADT_SIGNATURE           0x43495041U
#define ACPI_SSDT_SIGNATURE           0x54445353U
#define ACPI_DSDT_SIGNATURE           0x54445344U
#define ACPI_FADT_DSDT_OFFSET         40U
#define ACPI_FADT_X_DSDT_OFFSET       140U
#define I2000_DSDT_AML_SIZE           1186U

typedef struct {
    UINT32 Signature;
    UINT32 Length;
    UINT16 Revision;
    UINT16 EntryCount;
    UINT8 Checksum;
    UINT8 Reserved0[7];
    UINT16 SalAVersion;
    UINT16 SalBVersion;
    UINT8 OemId[32];
    UINT8 ProductId[32];
    UINT8 Reserved1[8];
} __attribute__((packed)) TEST_SAL_HEADER;

typedef struct {
    UINT8 Type;
    UINT8 Reserved0[7];
    UINT64 PalProc;
    UINT64 SalProc;
    UINT64 SalGp;
    UINT8 Reserved1[16];
} __attribute__((packed)) TEST_SAL_ENTRYPOINT;

typedef struct {
    UINT8 Type;
    UINT8 NeedVirtualAddress;
    UINT8 CurrentMemoryAttribute;
    UINT8 PageAccessRights;
    UINT8 SupportedMemoryAttributes;
    UINT8 Reserved0;
    UINT8 MemoryType;
    UINT8 MemoryUsage;
    UINT64 PhysicalAddress;
    UINT32 PageCount;
    UINT32 Reserved1;
    UINT64 OemReserved;
} __attribute__((packed)) TEST_SAL_MEMORY_DESCRIPTOR;

typedef struct {
    UINT64 Status;
    UINT64 Value0;
    UINT64 Value1;
    UINT64 Value2;
} TEST_SAL_RETURN;

typedef TEST_SAL_RETURN (*TEST_SAL_PROC)(
    UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64);

typedef struct {
    EFI_MEMORY_DESCRIPTOR *Buffer;
    UINTN MapSize;
    UINTN DescriptorSize;
} TEST_MEMORY_MAP;

typedef struct {
    const TEST_SAL_HEADER *Header;
    const TEST_SAL_ENTRYPOINT *Entrypoint;
    const TEST_SAL_MEMORY_DESCRIPTOR *Memory;
} TEST_SAL_TABLE;

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 sal_guid[16] = IA64_GUID_SAL;
static UINT8 acpi20_guid[16] = IA64_GUID_ACPI20;
static UINT8 memory_map_storage[0x4000] __attribute__((aligned(16)));
static volatile UINT64 direct_alias_value = I2000_DIRECT_ALIAS_VALUE;

static UINT64 read_region_register(UINT64 Address)
{
    UINT64 value;

    __asm__ volatile ("mov %0=rr[%1];;"
                      : "=r"(value)
                      : "r"(Address)
                      : "memory");
    return value;
}

static VOID write_region_register(UINT64 Address, UINT64 Value)
{
    __asm__ volatile ("mov rr[%0]=%1;;\n\t"
                      "srlz.d;;"
                      :
                      : "r"(Address), "r"(Value)
                      : "memory");
}

static BOOLEAN direct_alias_valid(VOID)
{
    UINT64 physical = (UINT64)(UINTN)&direct_alias_value;
    UINT64 alias;
    UINT64 saved_rr;
    UINT64 value;

    if (physical >= I2000_DIRECT_ALIAS_OFFSET) {
        return 0;
    }
    alias = IA64_REGION7_BASE | I2000_DIRECT_ALIAS_OFFSET | physical;
    saved_rr = read_region_register(alias);
    write_region_register(alias, I2000_DIRECT_ALIAS_RR);
    __asm__ volatile ("ssm psr.dt;;\n\t"
                      "srlz.d;;\n\t"
                      "ld8 %0=[%1];;\n\t"
                      "rsm psr.dt;;\n\t"
                      "srlz.d;;"
                      : "=r"(value)
                      : "r"(alias)
                      : "memory");
    __asm__ volatile ("ptc.l %0,%1;;\n\t"
                      "srlz.d;;"
                      :
                      : "r"(alias), "r"(I2000_DIRECT_ALIAS_ITIR)
                      : "memory");
    write_region_register(alias, saved_rr);
    return value == I2000_DIRECT_ALIAS_VALUE;
}

static BOOLEAN automatic_allocations_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_PHYSICAL_ADDRESS any = 0;
    EFI_PHYSICAL_ADDRESS maximum = ~(UINT64)0;
    EFI_PHYSICAL_ADDRESS low_maximum = 0x000000000fffffffULL;
    BOOLEAN any_allocated = 0;
    BOOLEAN maximum_allocated = 0;
    BOOLEAN low_maximum_allocated = 0;
    BOOLEAN valid;

    any_allocated = bs->AllocatePages(
        AllocateAnyPages, EfiLoaderData, 1, &any) == EFI_SUCCESS;
    maximum_allocated = bs->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 1, &maximum) == EFI_SUCCESS;
    low_maximum_allocated = bs->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 1, &low_maximum) == EFI_SUCCESS;
    valid = any_allocated && any < I2000_DIRECT_ALIAS_OFFSET &&
        maximum_allocated && maximum < I2000_DIRECT_ALIAS_OFFSET &&
        low_maximum_allocated && low_maximum <= 0x000000000fffffffULL;

    if (any_allocated && bs->FreePages(any, 1) != EFI_SUCCESS) {
        valid = 0;
    }
    if (maximum_allocated && bs->FreePages(maximum, 1) != EFI_SUCCESS) {
        valid = 0;
    }
    if (low_maximum_allocated &&
        bs->FreePages(low_maximum, 1) != EFI_SUCCESS) {
        valid = 0;
    }
    return valid;
}

static BOOLEAN address_allocation_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_PHYSICAL_ADDRESS address = 0x0000000004000000ULL;

    if (bs->AllocatePages(AllocateAddress, EfiLoaderData, 1, &address) !=
            EFI_SUCCESS ||
        address != 0x0000000004000000ULL) {
        return 0;
    }
    return bs->FreePages(address, 1) == EFI_SUCCESS;
}

static VOID *find_config_table(EFI_SYSTEM_TABLE *SystemTable,
                               const UINT8 *Guid)
{
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (ia64_bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                             Guid, 16)) {
            return (VOID *)(UINTN)
                SystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static const UINT8 *bytes_find(const UINT8 *Data, UINTN Size,
                               const UINT8 *Value, UINTN ValueSize)
{
    UINTN offset;

    if (ValueSize == 0 || ValueSize > Size) {
        return NULL;
    }
    for (offset = 0; offset <= Size - ValueSize; offset++) {
        if (ia64_bytes_equal(Data + offset, Value, ValueSize)) {
            return Data + offset;
        }
    }
    return NULL;
}

static BOOLEAN bytes_present(const UINT8 *Data, UINTN Size,
                             const UINT8 *Value, UINTN ValueSize)
{
    return bytes_find(Data, Size, Value, ValueSize) != NULL;
}

static BOOLEAN aml_named_byte(const UINT8 *Aml, UINTN Size,
                              const UINT8 Name[4], UINT8 Value)
{
    UINTN offset;

    for (offset = 0; offset + 7U <= Size; offset++) {
        if (Aml[offset] == 0x08U &&
            ia64_bytes_equal(Aml + offset + 1U, Name, 4) &&
            Aml[offset + 5U] == 0x0aU && Aml[offset + 6U] == Value) {
            return 1;
        }
    }
    return 0;
}

static const UINT8 *find_acpi_sdt(EFI_SYSTEM_TABLE *SystemTable,
                                  UINT32 Signature)
{
    static const UINT8 rsdp_signature[8] = {
        'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '
    };
    const UINT8 *rsdp = find_config_table(SystemTable, acpi20_guid);
    const UINT8 *xsdt;
    UINT32 length;
    UINTN count;
    UINTN index;

    if (rsdp == NULL ||
        !ia64_bytes_equal(rsdp, rsdp_signature, sizeof(rsdp_signature)) ||
        get_u32(rsdp + 20U) < 36U || ia64_checksum8(rsdp, 36U) != 0) {
        return NULL;
    }
    xsdt = (const UINT8 *)(UINTN)get_u64(rsdp + 24U);
    if (xsdt == NULL || get_u32(xsdt) != 0x54445358U) {
        return NULL;
    }
    length = get_u32(xsdt + 4U);
    if (length < ACPI_SDT_HEADER_SIZE || length > ACPI_MAX_TABLE_SIZE ||
        ((length - ACPI_SDT_HEADER_SIZE) & 7U) != 0 ||
        ia64_checksum8(xsdt, length) != 0) {
        return NULL;
    }
    count = (length - ACPI_SDT_HEADER_SIZE) / 8U;
    for (index = 0; index < count; index++) {
        const UINT8 *table = (const UINT8 *)(UINTN)get_u64(
            xsdt + ACPI_SDT_HEADER_SIZE + index * 8U);
        UINT32 table_length;

        if (table == NULL || get_u32(table) != Signature) {
            continue;
        }
        table_length = get_u32(table + 4U);
        if (table_length < ACPI_SDT_HEADER_SIZE ||
            table_length > ACPI_MAX_TABLE_SIZE ||
            ia64_checksum8(table, table_length) != 0) {
            return NULL;
        }
        return table;
    }
    return NULL;
}

static BOOLEAN acpi_ssdt_cpus_valid(const UINT8 *Ssdt)
{
    static const UINT8 c0en[4] = { 'C', '0', 'E', 'N' };
    static const UINT8 c1en[4] = { 'C', '1', 'E', 'N' };
    static const UINT8 c2en[4] = { 'C', '2', 'E', 'N' };
    static const UINT8 p2en[4] = { 'P', '2', 'E', 'N' };
    static const UINT8 cpu0[4] = { 'C', 'P', 'U', '0' };
    static const UINT8 cpu1[4] = { 'C', 'P', 'U', '1' };
    static const UINT8 cpu2[4] = { 'C', 'P', 'U', '2' };
    static const UINT8 ps2k[4] = { 'P', 'S', '2', 'K' };
    static const UINT8 ps2m[4] = { 'P', 'S', '2', 'M' };
    UINT32 length = get_u32(Ssdt + 4U);
    const UINT8 *aml = Ssdt + ACPI_SDT_HEADER_SIZE;
    UINTN aml_size = length - ACPI_SDT_HEADER_SIZE;

    return aml_named_byte(aml, aml_size, c0en, 0x0fU) &&
           aml_named_byte(aml, aml_size, c1en, 0x0fU) &&
           aml_named_byte(aml, aml_size, c2en, 0) &&
           aml_named_byte(aml, aml_size, p2en, 0) &&
           bytes_present(aml, aml_size, cpu0, sizeof(cpu0)) &&
           bytes_present(aml, aml_size, cpu1, sizeof(cpu1)) &&
           bytes_present(aml, aml_size, cpu2, sizeof(cpu2)) &&
           bytes_present(aml, aml_size, ps2k, sizeof(ps2k)) &&
           bytes_present(aml, aml_size, ps2m, sizeof(ps2m));
}

static BOOLEAN acpi_madt_cpus_valid(const UINT8 *Madt)
{
    UINT32 length = get_u32(Madt + 4U);
    UINTN offset = ACPI_MADT_HEADER_SIZE;
    UINT8 present = 0;
    UINT8 enabled = 0;

    if (length < ACPI_MADT_HEADER_SIZE || Madt[8U] != 2U ||
        get_u32(Madt + 40U) != 1U) {
        return 0;
    }

    while (offset + 2U <= length) {
        UINTN entry_length = Madt[offset + 1U];

        if (entry_length < 2U || entry_length > length - offset) {
            return 0;
        }
        if (Madt[offset] == 7U && entry_length >= 12U) {
            UINT8 processor = Madt[offset + 2U];
            UINT8 id = Madt[offset + 3U];
            UINT8 eid = Madt[offset + 4U];

            if (processor < 3U) {
                UINT8 bit = 1U << processor;

                if (id != processor || eid != 0 || (present & bit) != 0) {
                    return 0;
                }
                present |= bit;
                if ((get_u32(Madt + offset + 8U) & 1U) != 0) {
                    enabled |= bit;
                }
            }
        }
        offset += entry_length;
    }
    return offset == length && present == 0x07U && enabled == 0x03U;
}

static BOOLEAN acpi_fadt_valid(const UINT8 *Fadt)
{
    UINT32 flags;

    if (get_u32(Fadt + 4U) < 116U) {
        return 0;
    }
    flags = get_u32(Fadt + 112U);
    return Fadt[109U] == 0x03U && Fadt[110U] == 0 &&
           (flags & (1U << 4)) == 0 &&
           (flags & (1U << 5)) != 0 &&
           (flags & (1U << 10)) != 0;
}

static BOOLEAN acpi_dsdt_valid(const UINT8 *Fadt)
{
    static const UINT8 pci0_name[] = { 'P', 'C', 'I', '0' };
    static const UINT8 pci1_name[] = { 'P', 'C', 'I', '1' };
    static const UINT8 pci2_name[] = { 'P', 'C', 'I', '2' };
    static const UINT8 pci3_name[] = { 'P', 'C', 'I', '3' };
    static const UINT8 ifb0_name[] = { 'I', 'F', 'B', '0' };
    static const UINT8 ps2k_name[] = { 'P', 'S', '2', 'K' };
    static const UINT8 ps2m_name[] = { 'P', 'S', '2', 'M' };
    static const UINT8 empty_prt[] = {
        0x08, '_', 'P', 'R', 'T', 0x12, 0x02, 0x00,
    };
    const UINT8 *dsdt;
    const UINT8 *aml;
    const UINT8 *pci0;
    const UINT8 *ifb0;
    const UINT8 *ps2k;
    const UINT8 *ps2m;
    const UINT8 *pci1;
    const UINT8 *pci2;
    const UINT8 *pci3;
    const UINT8 *prt;
    UINT32 fadt_length = get_u32(Fadt + 4U);
    UINT32 dsdt_length;
    UINTN aml_size;
    UINT64 dsdt_address;

    if (fadt_length >= ACPI_FADT_X_DSDT_OFFSET + 8U) {
        dsdt_address = get_u64(Fadt + ACPI_FADT_X_DSDT_OFFSET);
    } else if (fadt_length >= ACPI_FADT_DSDT_OFFSET + 4U) {
        dsdt_address = get_u32(Fadt + ACPI_FADT_DSDT_OFFSET);
    } else {
        return 0;
    }
    dsdt = (const UINT8 *)(UINTN)dsdt_address;
    if (dsdt == NULL || get_u32(dsdt) != ACPI_DSDT_SIGNATURE) {
        return 0;
    }
    dsdt_length = get_u32(dsdt + 4U);
    if (dsdt_length != ACPI_SDT_HEADER_SIZE + I2000_DSDT_AML_SIZE ||
        ia64_checksum8(dsdt, dsdt_length) != 0) {
        return 0;
    }

    aml = dsdt + ACPI_SDT_HEADER_SIZE;
    aml_size = dsdt_length - ACPI_SDT_HEADER_SIZE;
    pci0 = bytes_find(aml, aml_size, pci0_name, sizeof(pci0_name));
    ifb0 = bytes_find(aml, aml_size, ifb0_name, sizeof(ifb0_name));
    ps2k = bytes_find(aml, aml_size, ps2k_name, sizeof(ps2k_name));
    ps2m = bytes_find(aml, aml_size, ps2m_name, sizeof(ps2m_name));
    pci1 = bytes_find(aml, aml_size, pci1_name, sizeof(pci1_name));
    pci2 = bytes_find(aml, aml_size, pci2_name, sizeof(pci2_name));
    pci3 = bytes_find(aml, aml_size, pci3_name, sizeof(pci3_name));
    prt = bytes_find(aml, aml_size, empty_prt, sizeof(empty_prt));
    return pci0 != NULL && ifb0 != NULL && ps2k != NULL && ps2m != NULL &&
           pci1 != NULL && pci2 != NULL && pci3 != NULL && prt != NULL &&
           ifb0 > pci0 && ps2k > ifb0 && ps2m > ps2k &&
           pci1 > ps2m && pci2 > pci1 && prt > pci2 && pci3 > prt;
}

static BOOLEAN acpi_topology_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    const UINT8 *fadt = find_acpi_sdt(SystemTable, ACPI_FADT_SIGNATURE);
    const UINT8 *ssdt = find_acpi_sdt(SystemTable, ACPI_SSDT_SIGNATURE);
    const UINT8 *madt = find_acpi_sdt(SystemTable, ACPI_MADT_SIGNATURE);

    return fadt != NULL && ssdt != NULL && madt != NULL &&
           acpi_fadt_valid(fadt) && acpi_dsdt_valid(fadt) &&
           acpi_ssdt_cpus_valid(ssdt) &&
           acpi_madt_cpus_valid(madt);
}

static BOOLEAN get_memory_map(EFI_SYSTEM_TABLE *SystemTable,
                              TEST_MEMORY_MAP *Map)
{
    UINTN map_key = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS status;

    Map->Buffer = (EFI_MEMORY_DESCRIPTOR *)memory_map_storage;
    Map->MapSize = sizeof(memory_map_storage);
    Map->DescriptorSize = 0;
    status = SystemTable->BootServices->GetMemoryMap(
        &Map->MapSize, Map->Buffer, &map_key, &Map->DescriptorSize,
        &descriptor_version);
    return status == EFI_SUCCESS && descriptor_version == 1U &&
           Map->DescriptorSize >= sizeof(EFI_MEMORY_DESCRIPTOR);
}

static BOOLEAN memory_range_is_exact(const TEST_MEMORY_MAP *Map,
                                     UINT64 Address, UINT64 Length,
                                     UINT32 Type, UINT64 Attributes)
{
    UINT64 range_end = Address + Length;
    UINTN offset;
    UINTN overlap_count = 0;
    BOOLEAN found = 0;

    if (Length == 0 || range_end < Address) {
        return 0;
    }
    for (offset = 0;
         offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= Map->MapSize;
         offset += Map->DescriptorSize) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)
                ((const UINT8 *)Map->Buffer + offset);
        UINT64 descriptor_end = descriptor->PhysicalStart +
            descriptor->NumberOfPages * EFI_PAGE_SIZE;

        if (descriptor_end < descriptor->PhysicalStart ||
            descriptor_end <= Address ||
            descriptor->PhysicalStart >= range_end) {
            continue;
        }
        overlap_count++;
        found = descriptor->PhysicalStart == Address &&
                descriptor_end == range_end && descriptor->Type == Type &&
                descriptor->VirtualStart == 0 &&
                descriptor->Attribute == Attributes;
    }
    return found && overlap_count == 1;
}

static UINT64 sal_memory_end(const TEST_SAL_MEMORY_DESCRIPTOR *Descriptor)
{
    return Descriptor->PhysicalAddress +
        (UINT64)Descriptor->PageCount * EFI_PAGE_SIZE;
}

static BOOLEAN sal_memory_metadata(
    const TEST_SAL_MEMORY_DESCRIPTOR *Descriptor, UINT8 NeedVirtualAddress,
    UINT8 CurrentMemoryAttribute, UINT8 PageAccessRights,
    UINT8 SupportedMemoryAttributes, UINT8 MemoryType, UINT8 MemoryUsage)
{
    return Descriptor->Type == SAL_DESCRIPTOR_MEMORY &&
           Descriptor->NeedVirtualAddress == NeedVirtualAddress &&
           Descriptor->CurrentMemoryAttribute == CurrentMemoryAttribute &&
           Descriptor->PageAccessRights == PageAccessRights &&
           Descriptor->SupportedMemoryAttributes ==
               SupportedMemoryAttributes &&
           Descriptor->Reserved0 == 0 &&
           Descriptor->MemoryType == MemoryType &&
           Descriptor->MemoryUsage == MemoryUsage &&
           Descriptor->PageCount != 0 && Descriptor->Reserved1 == 0 &&
           Descriptor->OemReserved == 0;
}

static BOOLEAN sal_table_init(EFI_SYSTEM_TABLE *SystemTable,
                              TEST_SAL_TABLE *Table)
{
    const TEST_SAL_HEADER *header = find_config_table(SystemTable, sal_guid);
    const UINT8 *entries;
    const UINT8 *trailer;

    if (header == NULL || header->Signature != 0x5f545353U ||
        header->Length != SAL_TABLE_LENGTH ||
        header->Revision < 0x0300U ||
        header->EntryCount != SAL_TABLE_ENTRY_COUNT ||
        ia64_checksum8(header, header->Length) != 0) {
        return 0;
    }
    entries = (const UINT8 *)(header + 1);
    Table->Header = header;
    Table->Entrypoint = (const TEST_SAL_ENTRYPOINT *)entries;
    Table->Memory = (const TEST_SAL_MEMORY_DESCRIPTOR *)(
        entries + sizeof(*Table->Entrypoint));
    trailer = (const UINT8 *)(Table->Memory + SAL_MEMORY_DESCRIPTOR_COUNT);
    return Table->Entrypoint->Type == SAL_DESCRIPTOR_ENTRYPOINT &&
           trailer[0] == SAL_DESCRIPTOR_FEATURES &&
           trailer[16] == SAL_DESCRIPTOR_TR &&
           trailer[48] == SAL_DESCRIPTOR_AP_WAKE;
}

static BOOLEAN sal_memory_descriptors_valid(const TEST_SAL_TABLE *Table)
{
    const TEST_SAL_ENTRYPOINT *entry = Table->Entrypoint;
    const TEST_SAL_MEMORY_DESCRIPTOR *memory = Table->Memory;
    UINT64 pal_end = sal_memory_end(&memory[0]);
    UINT64 boot_end = sal_memory_end(&memory[1]);
    UINT64 code_end = sal_memory_end(&memory[2]);
    UINT64 data_end = sal_memory_end(&memory[3]);

    return sal_memory_metadata(
               &memory[0], 0, SAL_MEMORY_ATTRIBUTE_WB,
               SAL_PAGE_ACCESS_RX, SAL_MEMORY_SUPPORTS_WB,
               SAL_MEMORY_TYPE_REGULAR, SAL_MEMORY_USAGE_PAL_CODE) &&
           sal_memory_metadata(
               &memory[1], 0, SAL_MEMORY_ATTRIBUTE_WB,
               SAL_PAGE_ACCESS_RX, SAL_MEMORY_SUPPORTS_WB,
               SAL_MEMORY_TYPE_REGULAR, SAL_MEMORY_USAGE_BOOT_CODE) &&
           sal_memory_metadata(
               &memory[2], 1, SAL_MEMORY_ATTRIBUTE_WB,
               SAL_PAGE_ACCESS_RX, SAL_MEMORY_SUPPORTS_WB,
               SAL_MEMORY_TYPE_REGULAR, SAL_MEMORY_USAGE_RUNTIME_CODE) &&
           sal_memory_metadata(
               &memory[3], 1, SAL_MEMORY_ATTRIBUTE_WB,
               SAL_PAGE_ACCESS_RW, SAL_MEMORY_SUPPORTS_WB,
               SAL_MEMORY_TYPE_REGULAR, SAL_MEMORY_USAGE_RUNTIME_DATA) &&
           sal_memory_metadata(
               &memory[4], 1, SAL_MEMORY_ATTRIBUTE_UC,
               SAL_PAGE_ACCESS_RW, SAL_MEMORY_SUPPORTS_UC,
               SAL_MEMORY_TYPE_FIRMWARE,
               SAL_MEMORY_USAGE_UNSPECIFIED) &&
           memory[0].PhysicalAddress ==
               (entry->PalProc & ~(UINT64)(EFI_PAGE_SIZE - 1U)) &&
           memory[0].PageCount == 1 &&
           pal_end == memory[1].PhysicalAddress &&
           boot_end == memory[2].PhysicalAddress &&
           code_end == memory[3].PhysicalAddress &&
           memory[0].PhysicalAddress >= I2000_FIRMWARE_BASE &&
           data_end <= I2000_FIRMWARE_END &&
           memory[4].PhysicalAddress == I2000_FIRMWARE_APERTURE_BASE &&
           sal_memory_end(&memory[4]) == I2000_FIRMWARE_APERTURE_END;
}

static BOOLEAN sal_entrypoint_valid(const TEST_SAL_TABLE *Table)
{
    const TEST_SAL_ENTRYPOINT *entry = Table->Entrypoint;
    UINT64 code_start = Table->Memory[2].PhysicalAddress;
    UINT64 code_end = sal_memory_end(&Table->Memory[2]);

    return entry->PalProc != 0 && entry->SalProc >= code_start &&
           entry->SalProc < code_end && entry->SalGp >= code_start &&
           entry->SalGp < code_end;
}

static BOOLEAN sal_call_valid(const TEST_SAL_TABLE *Table)
{
    volatile UINT64 descriptor[2] __attribute__((aligned(16)));
    TEST_SAL_PROC procedure;
    TEST_SAL_RETURN result;

    descriptor[0] = Table->Entrypoint->SalProc;
    descriptor[1] = Table->Entrypoint->SalGp;
    procedure = (TEST_SAL_PROC)(UINTN)&descriptor[0];
    result = procedure(SAL_GET_STATE_INFO_SIZE, 0, 0, 0, 0, 0, 0, 0);
    return result.Status == SAL_SUCCESS &&
           result.Value0 == SAL_STATE_INFO_MAX_SIZE &&
           result.Value1 == 0 && result.Value2 == 0;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "i2000-loader",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    TEST_MEMORY_MAP map;
    TEST_SAL_TABLE sal = { 0 };
    BOOLEAN have_map;
    BOOLEAN have_sal;
    UINT64 runtime_start = 0;
    UINT64 runtime_end = 0;
    EFI_STATUS status;

    status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, loaded_image_guid, (VOID **)&loaded);
    have_map = get_memory_map(SystemTable, &map);
    have_sal = sal_table_init(SystemTable, &sal);
    if (have_sal) {
        runtime_start = sal.Memory[2].PhysicalAddress;
        runtime_end = sal_memory_end(&sal.Memory[3]);
    }

    ia64_test_check(&context, "image-placement",
                    status == EFI_SUCCESS && loaded != NULL &&
                        (UINTN)loaded->ImageBase == I2000_IMAGE_BASE,
                    status, "loader-image-base");
    ia64_test_check(
        &context, "low-memory-map",
        have_map && memory_range_is_exact(
            &map, I2000_CONVENTIONAL_BASE,
            I2000_CONVENTIONAL_END - I2000_CONVENTIONAL_BASE,
            EfiConventionalMemory, EFI_MEMORY_WB),
        EFI_DEVICE_ERROR, "conventional-memory-through-80mib");
    ia64_test_check(
        &context, "runtime-map",
        have_map && have_sal && runtime_end > runtime_start &&
            memory_range_is_exact(
                &map, runtime_start, runtime_end - runtime_start,
                EfiRuntimeServicesCode,
                EFI_MEMORY_WB | EFI_MEMORY_RUNTIME),
        EFI_DEVICE_ERROR, "combined-runtime-code-data");
    ia64_test_check(
        &context, "firmware-aperture",
        have_map && memory_range_is_exact(
            &map, I2000_FIRMWARE_APERTURE_BASE,
            I2000_FIRMWARE_APERTURE_END - I2000_FIRMWARE_APERTURE_BASE,
            EfiRuntimeServicesData,
            EFI_MEMORY_UC | EFI_MEMORY_RUNTIME),
        EFI_DEVICE_ERROR, "sal-firmware-address-space");
    ia64_test_check(&context, "sal-entrypoint",
                    have_sal && sal_entrypoint_valid(&sal),
                    EFI_DEVICE_ERROR, "sal-procedure-code-gp");
    ia64_test_check(&context, "sal-memory-descriptors",
                    have_sal && sal_memory_descriptors_valid(&sal),
                    EFI_DEVICE_ERROR, "five-entry-memory-descriptor-table");
    ia64_test_check(&context, "sal-call",
                    have_sal && sal_entrypoint_valid(&sal) &&
                        sal_call_valid(&sal),
                    EFI_DEVICE_ERROR, "get-state-info-size");
    ia64_test_check(&context, "direct-alias", direct_alias_valid(),
                    EFI_DEVICE_ERROR, "region-seven-loader-alias");
    ia64_test_check(&context, "automatic-allocation",
                    automatic_allocations_valid(SystemTable),
                    EFI_DEVICE_ERROR, "automatic-allocation-below-alias");
    ia64_test_check(&context, "address-allocation",
                    address_allocation_valid(SystemTable),
                    EFI_DEVICE_ERROR, "explicit-address-preserved");
    ia64_test_check(&context, "acpi-topology",
                    acpi_topology_valid(SystemTable),
                    EFI_DEVICE_ERROR, "madt-ssdt-processor-map");
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
