/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "services.h"

#define SIG32(A, B, C, D) \
    ((UINT32)(A) | ((UINT32)(B) << 8) | ((UINT32)(C) << 16) | \
     ((UINT32)(D) << 24))

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 loaded_image_device_path_guid[16] =
    IA64_GUID_LOADED_IMAGE_DEVICE_PATH;
static UINT8 hii_package_list_guid[16] = IA64_GUID_HII_PACKAGE_LIST;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 block_io_guid[16] = IA64_GUID_BLOCK_IO;
static UINT8 disk_io_guid[16] = IA64_GUID_DISK_IO;
static UINT8 gop_guid[16] = IA64_GUID_GOP;
static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_io_guid[16] = IA64_GUID_PCI_IO;
static UINT8 tcg_guid[16] = IA64_GUID_TCG;
static UINT8 driver_binding_guid[16] = IA64_GUID_DRIVER_BINDING;
static UINT8 platform_override_guid[16] =
    IA64_GUID_PLATFORM_DRIVER_OVERRIDE;
static UINT8 bus_override_guid[16] =
    IA64_GUID_BUS_SPECIFIC_DRIVER_OVERRIDE;
static UINT8 family_override_guid[16] =
    IA64_GUID_DRIVER_FAMILY_OVERRIDE;
static UINT8 load_file_guid[16] = IA64_GUID_LOAD_FILE;
static UINT8 load_file2_guid[16] = IA64_GUID_LOAD_FILE2;
static UINT8 acpi20_guid[16] = IA64_GUID_ACPI20;
static UINT8 sal_guid[16] = IA64_GUID_SAL;
static UINT8 hcdp_guid[16] = IA64_GUID_HCDP;
static UINT8 smbios_guid[16] = IA64_GUID_SMBIOS;
static UINT8 efi_global_variable_guid[16] = {
    0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
    0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static UINT8 debug_image_guid[16] = {
    0x77, 0x2e, 0x15, 0x49, 0xda, 0x1a, 0x64, 0x47,
    0xb7, 0xa2, 0x7a, 0xfe, 0xfe, 0xd9, 0x5e, 0x8b,
};
static UINT8 variable_guid[16] = {
    0x15, 0x64, 0x72, 0xe2, 0xe2, 0x5c, 0x49, 0x4c,
    0xb7, 0x2c, 0xf3, 0x47, 0x15, 0xe7, 0x75, 0x7a,
};
static UINT8 connection_test_guid[16] = {
    0x49, 0x41, 0x36, 0x34, 0x43, 0x4f, 0x4e, 0x4e,
    0x45, 0x43, 0x54, 0x54, 0x45, 0x53, 0x54, 0x01,
};
static UINT8 start_image_base_guid[16] = {
    0x49, 0x41, 0x36, 0x34, 0x53, 0x54, 0x41, 0x52,
    0x54, 0x42, 0x41, 0x53, 0x45, 0x00, 0x00, 0x01,
};
static UINT8 start_image_change_guid[16] = {
    0x49, 0x41, 0x36, 0x34, 0x53, 0x54, 0x41, 0x52,
    0x54, 0x43, 0x48, 0x41, 0x4e, 0x47, 0x45, 0x01,
};
static CHAR16 variable_name[] = {
    'I', 'A', '6', '4', 'F', 'u', 'n', 'c', 'T', 'e', 's', 't', 0,
};
static CHAR16 conout_dev_name[] = {
    'C', 'o', 'n', 'O', 'u', 't', 'D', 'e', 'v', 0,
};

typedef struct {
    UINT8 Signature[8];
    UINT8 Checksum;
    UINT8 OemId[6];
    UINT8 Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress;
    UINT8 ExtendedChecksum;
    UINT8 Reserved[3];
} __attribute__((packed)) TEST_RSDP;

typedef struct {
    UINT32 Signature;
    UINT32 Length;
    UINT8 Revision;
    UINT8 Checksum;
    UINT8 OemId[6];
    UINT8 OemTableId[8];
    UINT32 OemRevision;
    UINT32 CreatorId;
    UINT32 CreatorRevision;
} __attribute__((packed)) TEST_SDT_HEADER;

typedef struct {
    UINT8 SpaceId;
    UINT8 BitWidth;
    UINT8 BitOffset;
    UINT8 AccessSize;
    UINT64 Address;
} __attribute__((packed)) TEST_GAS;

typedef struct {
    UINT8 Descriptor;
    UINT16 Length;
    UINT8 ResourceType;
    UINT8 GeneralFlags;
    UINT8 TypeSpecificFlags;
    UINT64 Granularity;
    UINT64 Minimum;
    UINT64 Maximum;
    UINT64 Translation;
    UINT64 AddressLength;
} __attribute__((packed)) TEST_QWORD_RESOURCE;

typedef struct {
    UINT32 ImageInfoType;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImageProtocolInstance;
    EFI_HANDLE ImageHandle;
} TEST_DEBUG_IMAGE_INFO_NORMAL;

typedef union {
    UINT32 *ImageInfoType;
    TEST_DEBUG_IMAGE_INFO_NORMAL *NormalImage;
} TEST_DEBUG_IMAGE_INFO;

typedef struct {
    volatile UINT32 UpdateStatus;
    UINT32 TableSize;
    TEST_DEBUG_IMAGE_INFO *Table;
} TEST_DEBUG_IMAGE_INFO_HEADER;

typedef struct {
    UINT64 Status;
    UINT64 Value0;
    UINT64 Value1;
    UINT64 Value2;
} TEST_SAL_RETURN;

typedef TEST_SAL_RETURN (*TEST_SAL_PROC)(UINT64, UINT64, UINT64, UINT64,
                                        UINT64, UINT64, UINT64, UINT64);

typedef struct {
    EFI_MEMORY_DESCRIPTOR *Buffer;
    UINTN MapSize;
    UINTN DescriptorSize;
} TEST_MEMORY_MAP;

typedef struct {
    TEST_MEMORY_MAP MemoryMap;
    TEST_RSDP *Rsdp;
    TEST_SDT_HEADER *Rsdt;
    TEST_SDT_HEADER *Xsdt;
    TEST_SDT_HEADER *Fadt;
    TEST_SDT_HEADER *Madt;
    TEST_SDT_HEADER *Srat;
    TEST_SDT_HEADER *Slit;
    TEST_SDT_HEADER *Mcfg;
    TEST_SDT_HEADER *Hcdp;
    TEST_SDT_HEADER *Dbgp;
    TEST_SDT_HEADER *Ssdt;
    TEST_SDT_HEADER *Dsdt;
    UINT8 *Facs;
    UINTN RootEntryCount;
    BOOLEAN Valid;
} TEST_TABLE_CONTEXT;

typedef struct _TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL
    TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL;

struct _TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL {
    EFI_STATUS (*GetDriver)(TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL *,
                            EFI_HANDLE, EFI_HANDLE *);
    VOID *GetDriverPath;
    VOID *DriverLoaded;
};

typedef struct _TEST_BUS_DRIVER_OVERRIDE_PROTOCOL
    TEST_BUS_DRIVER_OVERRIDE_PROTOCOL;

struct _TEST_BUS_DRIVER_OVERRIDE_PROTOCOL {
    EFI_STATUS (*GetDriver)(TEST_BUS_DRIVER_OVERRIDE_PROTOCOL *,
                            EFI_HANDLE *);
};

typedef struct _TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL
    TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL;

struct _TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL {
    UINT32 (*GetVersion)(TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL *);
};

typedef struct _TEST_LOAD_FILE_PROTOCOL TEST_LOAD_FILE_PROTOCOL;

struct _TEST_LOAD_FILE_PROTOCOL {
    EFI_STATUS (*LoadFile)(TEST_LOAD_FILE_PROTOCOL *, VOID *, BOOLEAN,
                           UINTN *, VOID *);
};

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} __attribute__((packed)) TEST_DEVICE_PATH_NODE;

#define TEST_UART_BASE               0x00000047f0000000ULL
#define TEST_UART_IO_PORT            0x00000000000003f8ULL
#define TEST_UART_SIZE               0x0000000000002000ULL
#define TEST_RTC_BASE                0x00000000ffef0000ULL
#define TEST_RTC_SIZE                0x0000000000002000ULL
#define TEST_NVRAM_BASE              0x00000000fff00000ULL
#define TEST_NVRAM_SIZE              0x0000000000010000ULL
#define TEST_ECAM_BASE               0x0000007ff0000000ULL
#define TEST_ECAM_SIZE               0x0000000010000000ULL
#define TEST_PCI_MMIO_BASE           0x00000000c1000000ULL
#define TEST_PCI_MMIO_SIZE           0x0000000010000000ULL
#define TEST_VGA_ROM_BASE            0x00000000000c0000ULL
#define TEST_VGA_ROM_SIZE            0x0000000000008000ULL
#define TEST_SPARSE_IO_BASE          IA64_TEST_LEGACY_IO_BASE
#define TEST_SPARSE_IO_SIZE          IA64_TEST_LEGACY_IO_SIZE
#define TEST_PM_IO_BASE              0x2000U
#define TEST_HCDP_LENGTH             129U
#define TEST_HCDP_UART_HID           0x0105d041U
#define TEST_HCDP_UART_BASE_FLAGS    0x41U
#define TEST_HCDP_UART_PRIMARY       0x04U
#define TEST_HCDP_UART_CONOUT_INDEX  1U
#define TEST_CONOUT_DEV_SIZE         57U
#define TEST_DEVICE_PATH_PNP0501     0x050141d0U

#define TEST_SAL_GET_STATE_INFO      0x01000001ULL
#define TEST_SAL_GET_STATE_INFO_SIZE 0x01000002ULL
#define TEST_SAL_CLEAR_STATE_INFO    0x01000003ULL
#define TEST_SAL_SUCCESS             0ULL
#define TEST_SAL_NO_INFORMATION      ((UINT64)-5)
#define TEST_SAL_STATE_INFO_MAX_SIZE 512U

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

static UINT16 get_u16(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static VOID zero_bytes(VOID *Address, UINTN Length)
{
    UINT8 *p = (UINT8 *)Address;
    UINTN i;

    for (i = 0; i < Length; i++) {
        p[i] = 0;
    }
}

static BOOLEAN get_memory_map(EFI_SYSTEM_TABLE *SystemTable,
                              TEST_MEMORY_MAP *Map)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    UINTN key = 0;
    UINT32 version = 0;
    EFI_STATUS status;

    Map->Buffer = NULL;
    Map->MapSize = 0;
    Map->DescriptorSize = 0;
    status = bs->GetMemoryMap(&Map->MapSize, NULL, NULL,
                              &Map->DescriptorSize, &version);
    if (status != EFI_BUFFER_TOO_SMALL || Map->MapSize == 0 ||
        Map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return 0;
    }
    Map->MapSize += 8U * Map->DescriptorSize;
    if (bs->AllocatePool(EfiLoaderData, Map->MapSize,
                         (VOID **)&Map->Buffer) != EFI_SUCCESS) {
        Map->Buffer = NULL;
        return 0;
    }
    status = bs->GetMemoryMap(&Map->MapSize, Map->Buffer, &key,
                              &Map->DescriptorSize, &version);
    if (status != EFI_SUCCESS || version != 1U) {
        (void)bs->FreePool(Map->Buffer);
        Map->Buffer = NULL;
        return 0;
    }
    return 1;
}

static VOID put_memory_map(EFI_SYSTEM_TABLE *SystemTable,
                           TEST_MEMORY_MAP *Map)
{
    if (Map->Buffer != NULL) {
        (void)SystemTable->BootServices->FreePool(Map->Buffer);
        Map->Buffer = NULL;
    }
}

static BOOLEAN memory_range_has_type(const TEST_MEMORY_MAP *Map,
                                     UINT64 Address, UINT64 Length,
                                     UINT32 Type, UINT64 RequiredAttributes)
{
    UINT64 range_end = Address + Length;
    UINTN offset;

    if (Map->Buffer == NULL || Length == 0 || range_end < Address) {
        return 0;
    }
    for (offset = 0; offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= Map->MapSize;
         offset += Map->DescriptorSize) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)((const UINT8 *)Map->Buffer +
                                             offset);
        UINT64 descriptor_end = descriptor->PhysicalStart +
            descriptor->NumberOfPages * EFI_PAGE_SIZE;

        if (descriptor_end < descriptor->PhysicalStart) {
            continue;
        }
        if (descriptor->Type == Type &&
            (descriptor->Attribute & RequiredAttributes) ==
                RequiredAttributes &&
            Address >= descriptor->PhysicalStart &&
            range_end <= descriptor_end) {
            return 1;
        }
    }
    return 0;
}

static BOOLEAN memory_range_is_mapped(const TEST_MEMORY_MAP *Map,
                                      UINT64 Address, UINT64 Length)
{
    UINT64 range_end = Address + Length;
    UINTN offset;

    if (Map->Buffer == NULL || Length == 0 || range_end < Address) {
        return 0;
    }
    for (offset = 0; offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= Map->MapSize;
         offset += Map->DescriptorSize) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)((const UINT8 *)Map->Buffer +
                                             offset);
        UINT64 descriptor_end = descriptor->PhysicalStart +
            descriptor->NumberOfPages * EFI_PAGE_SIZE;

        if (descriptor_end >= descriptor->PhysicalStart &&
            Address >= descriptor->PhysicalStart &&
            range_end <= descriptor_end) {
            return 1;
        }
    }
    return 0;
}

static BOOLEAN acpi_reclaim_range(const TEST_MEMORY_MAP *Map,
                                  const VOID *Address, UINTN Length)
{
    UINT64 start = (UINT64)(UINTN)Address;

    return memory_range_has_type(Map, start, Length,
                                 EfiACPIReclaimMemory, 0);
}

static BOOLEAN acpi_nvs_range(const TEST_MEMORY_MAP *Map,
                              const VOID *Address, UINTN Length)
{
    UINT64 start = (UINT64)(UINTN)Address;

    return memory_range_has_type(Map, start, Length, EfiACPIMemoryNVS, 0);
}

static BOOLEAN valid_sdt(const TEST_MEMORY_MAP *Map,
                         const TEST_SDT_HEADER *Header, UINT32 Signature,
                         UINT8 MinimumRevision, UINT32 MinimumLength)
{
    UINT32 length;

    if (!acpi_reclaim_range(Map, Header, sizeof(*Header))) {
        return 0;
    }
    length = get_u32((const UINT8 *)Header + 4);
    if (get_u32(Header) != Signature || length < MinimumLength ||
        length > 0x10000U || Header->Revision < MinimumRevision ||
        !acpi_reclaim_range(Map, Header, length)) {
        return 0;
    }
    return ia64_checksum8(Header, length) == 0;
}

static BOOLEAN assign_root_table(TEST_TABLE_CONTEXT *Context,
                                 TEST_SDT_HEADER *Header)
{
    TEST_SDT_HEADER **slot = NULL;

    switch (get_u32(Header)) {
    case SIG32('F', 'A', 'C', 'P'):
        slot = &Context->Fadt;
        break;
    case SIG32('A', 'P', 'I', 'C'):
        slot = &Context->Madt;
        break;
    case SIG32('S', 'R', 'A', 'T'):
        slot = &Context->Srat;
        break;
    case SIG32('S', 'L', 'I', 'T'):
        slot = &Context->Slit;
        break;
    case SIG32('M', 'C', 'F', 'G'):
        slot = &Context->Mcfg;
        break;
    case SIG32('H', 'C', 'D', 'P'):
        slot = &Context->Hcdp;
        break;
    case SIG32('D', 'B', 'G', 'P'):
        slot = &Context->Dbgp;
        break;
    case SIG32('S', 'S', 'D', 'T'):
        slot = &Context->Ssdt;
        break;
    default:
        /* Unknown, checksum-valid SDTs are legal root-table entries. */
        return 1;
    }
    if (*slot != NULL) {
        return 0;
    }
    *slot = Header;
    return 1;
}

static BOOLEAN init_table_context(EFI_SYSTEM_TABLE *SystemTable,
                                  TEST_TABLE_CONTEXT *Context)
{
    static const UINT8 rsdp_signature[8] = {
        'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ',
    };
    UINT32 rsdt_length;
    UINT32 xsdt_length;
    UINTN rsdt_count;
    UINTN xsdt_count;
    UINTN i;

    zero_bytes(Context, sizeof(*Context));
    if (!get_memory_map(SystemTable, &Context->MemoryMap)) {
        return 0;
    }
    Context->Rsdp = (TEST_RSDP *)find_config_table(SystemTable, acpi20_guid);
    if (!acpi_reclaim_range(&Context->MemoryMap, Context->Rsdp,
                            sizeof(*Context->Rsdp)) ||
        !ia64_bytes_equal(Context->Rsdp->Signature, rsdp_signature,
                          sizeof(rsdp_signature)) ||
        Context->Rsdp->Revision < 2U ||
        get_u32((UINT8 *)Context->Rsdp + 20) < sizeof(*Context->Rsdp) ||
        get_u32((UINT8 *)Context->Rsdp + 20) > 4096U ||
        ia64_checksum8(Context->Rsdp, 20) != 0 ||
        ia64_checksum8(Context->Rsdp,
                       get_u32((UINT8 *)Context->Rsdp + 20)) != 0) {
        return 0;
    }

    Context->Rsdt = (TEST_SDT_HEADER *)(UINTN)
        get_u32((UINT8 *)Context->Rsdp + 16);
    Context->Xsdt = (TEST_SDT_HEADER *)(UINTN)
        get_u64((UINT8 *)Context->Rsdp + 24);
    if (!valid_sdt(&Context->MemoryMap, Context->Rsdt,
                   SIG32('R', 'S', 'D', 'T'), 1, sizeof(TEST_SDT_HEADER)) ||
        !valid_sdt(&Context->MemoryMap, Context->Xsdt,
                   SIG32('X', 'S', 'D', 'T'), 1, sizeof(TEST_SDT_HEADER))) {
        return 0;
    }
    rsdt_length = get_u32((UINT8 *)Context->Rsdt + 4);
    xsdt_length = get_u32((UINT8 *)Context->Xsdt + 4);
    if (((rsdt_length - sizeof(TEST_SDT_HEADER)) & 3U) != 0 ||
        ((xsdt_length - sizeof(TEST_SDT_HEADER)) & 7U) != 0) {
        return 0;
    }
    rsdt_count = (rsdt_length - sizeof(TEST_SDT_HEADER)) / 4U;
    xsdt_count = (xsdt_length - sizeof(TEST_SDT_HEADER)) / 8U;
    if (rsdt_count != xsdt_count || xsdt_count < 7U) {
        return 0;
    }
    Context->RootEntryCount = xsdt_count;
    for (i = 0; i < xsdt_count; i++) {
        UINT64 address = get_u64((UINT8 *)Context->Xsdt +
                                 sizeof(TEST_SDT_HEADER) + i * 8U);
        UINT32 rsdt_address = get_u32((UINT8 *)Context->Rsdt +
                                      sizeof(TEST_SDT_HEADER) + i * 4U);
        TEST_SDT_HEADER *header = (TEST_SDT_HEADER *)(UINTN)address;

        if (address == 0 || (UINT32)address != rsdt_address ||
            !acpi_reclaim_range(&Context->MemoryMap, header,
                                sizeof(*header)) ||
            !valid_sdt(&Context->MemoryMap, header, get_u32(header), 1,
                       sizeof(TEST_SDT_HEADER)) ||
            !assign_root_table(Context, header)) {
            return 0;
        }
    }
    if (Context->Fadt == NULL || Context->Madt == NULL ||
        Context->Srat == NULL || Context->Slit == NULL ||
        Context->Mcfg == NULL || Context->Hcdp == NULL ||
        Context->Ssdt == NULL) {
        return 0;
    }

    if (get_u32((UINT8 *)Context->Fadt + 4) < 148U) {
        return 0;
    }
    Context->Facs = (UINT8 *)(UINTN)get_u64((UINT8 *)Context->Fadt + 132);
    Context->Dsdt = (TEST_SDT_HEADER *)(UINTN)
        get_u64((UINT8 *)Context->Fadt + 140);
    if (Context->Facs == NULL || Context->Dsdt == NULL ||
        ((UINTN)Context->Facs & 63U) != 0 ||
        !acpi_nvs_range(&Context->MemoryMap, Context->Facs, 8) ||
        get_u32(Context->Facs) != SIG32('F', 'A', 'C', 'S') ||
        get_u32(Context->Facs + 4) < 64U ||
        !acpi_nvs_range(&Context->MemoryMap, Context->Facs,
                        get_u32(Context->Facs + 4)) ||
        !valid_sdt(&Context->MemoryMap, Context->Dsdt,
                   SIG32('D', 'S', 'D', 'T'), 2,
                   sizeof(TEST_SDT_HEADER))) {
        return 0;
    }
    Context->Valid = 1;
    return 1;
}

static VOID release_table_context(EFI_SYSTEM_TABLE *SystemTable,
                                  TEST_TABLE_CONTEXT *Context)
{
    put_memory_map(SystemTable, &Context->MemoryMap);
    Context->Valid = 0;
}

static BOOLEAN test_memory_services(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    EFI_PHYSICAL_ADDRESS pages = 0;
    VOID *pool = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS status;
    BOOLEAN ok = 0;

    status = bs->GetMemoryMap(&map_size, NULL, NULL, &descriptor_size,
                              &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || map_size == 0 ||
        descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return 0;
    }
    map_size += 4U * descriptor_size;
    if (bs->AllocatePool(EfiLoaderData, map_size, (VOID **)&map) !=
        EFI_SUCCESS) {
        return 0;
    }
    status = bs->GetMemoryMap(&map_size, map, &map_key, &descriptor_size,
                              &descriptor_version);
    if (status != EFI_SUCCESS || descriptor_version != 1U) {
        goto out;
    }
    if (bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 2, &pages) !=
            EFI_SUCCESS ||
        pages == 0 || bs->FreePages(pages, 2) != EFI_SUCCESS) {
        goto out;
    }
    pages = 0;
    if (bs->AllocatePool(EfiLoaderData, 37, &pool) != EFI_SUCCESS ||
        pool == NULL || bs->FreePool(pool) != EFI_SUCCESS) {
        goto out;
    }
    pool = NULL;
    ok = 1;
out:
    if (pool != NULL) {
        (void)bs->FreePool(pool);
    }
    (void)bs->FreePool(map);
    return ok;
}

static BOOLEAN test_event_services(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_EVENT event = NULL;
    EFI_STATUS status;

    if (bs->CreateEvent(0, TPL_APPLICATION, NULL, NULL, &event) !=
            EFI_SUCCESS ||
        event == NULL) {
        return 0;
    }
    status = bs->CheckEvent(event);
    if (status != EFI_NOT_READY || bs->SignalEvent(event) != EFI_SUCCESS ||
        bs->CheckEvent(event) != EFI_SUCCESS ||
        bs->CheckEvent(event) != EFI_NOT_READY ||
        bs->CloseEvent(event) != EFI_SUCCESS) {
        return 0;
    }
    return 1;
}

static BOOLEAN test_protocol_services(EFI_HANDLE ImageHandle,
                                      EFI_SYSTEM_TABLE *SystemTable,
                                      EFI_LOADED_IMAGE_PROTOCOL **Loaded)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    VOID *opened = NULL;
    VOID *located = NULL;

    if (bs->HandleProtocol(ImageHandle, loaded_image_guid,
                           (VOID **)&loaded) != EFI_SUCCESS ||
        loaded == NULL || loaded->DeviceHandle == NULL ||
        bs->OpenProtocol(ImageHandle, loaded_image_guid, &opened,
                         ImageHandle, NULL,
                         EFI_OPEN_PROTOCOL_GET_PROTOCOL) != EFI_SUCCESS ||
        opened != loaded ||
        bs->CloseProtocol(ImageHandle, loaded_image_guid,
                          ImageHandle, NULL) != EFI_SUCCESS ||
        bs->LocateProtocol(device_path_guid, NULL, &located) != EFI_SUCCESS ||
        located == NULL) {
        return 0;
    }
    *Loaded = loaded;
    return 1;
}

static BOOLEAN test_multiple_protocol_services(EFI_SYSTEM_TABLE *SystemTable)
{
    static UINT8 marker1_guid[16] = {
        0x4d, 0x55, 0x4c, 0x54, 0x49, 0x50, 0x52, 0x4f,
        0x54, 0x4f, 0x43, 0x4f, 0x4c, 0x00, 0x00, 0x01,
    };
    static UINT8 marker2_guid[16] = {
        0x4d, 0x55, 0x4c, 0x54, 0x49, 0x50, 0x52, 0x4f,
        0x54, 0x4f, 0x43, 0x4f, 0x4c, 0x00, 0x00, 0x02,
    };
    struct {
        TEST_DEVICE_PATH_NODE Pci;
        UINT8 Function;
        UINT8 Device;
        TEST_DEVICE_PATH_NODE End;
    } __attribute__((packed, aligned(8))) path1 = {
        { 0x01, 0x01, 6 }, 0, 30, { 0x7f, 0xff, 4 },
    }, path2 = {
        { 0x01, 0x01, 6 }, 0, 30, { 0x7f, 0xff, 4 },
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_HANDLE handle = NULL;
    EFI_HANDLE path_handle = NULL;
    EFI_HANDLE duplicate = NULL;
    UINT8 interface1 = 1;
    UINT8 interface2 = 2;
    UINT8 wrong_interface = 3;
    VOID *located1 = NULL;
    VOID *located2 = NULL;
    BOOLEAN handle_installed = 0;
    BOOLEAN path_installed = 0;
    BOOLEAN ok = 0;

    if (bs->InstallMultipleProtocolInterfaces(
            &duplicate, marker1_guid, &interface1,
            marker1_guid, &interface2, NULL) != EFI_INVALID_PARAMETER ||
        duplicate != NULL) {
        return 0;
    }
    if (bs->InstallMultipleProtocolInterfaces(
            &handle, marker1_guid, &interface1,
            marker2_guid, &interface2, NULL) != EFI_SUCCESS ||
        handle == NULL) {
        goto out;
    }
    handle_installed = 1;

    if (bs->UninstallMultipleProtocolInterfaces(
            handle, marker1_guid, &interface1,
            marker2_guid, &wrong_interface, NULL) !=
            EFI_INVALID_PARAMETER ||
        bs->HandleProtocol(handle, marker1_guid, &located1) != EFI_SUCCESS ||
        located1 != &interface1 ||
        bs->HandleProtocol(handle, marker2_guid, &located2) != EFI_SUCCESS ||
        located2 != &interface2) {
        goto out;
    }

    if (bs->InstallMultipleProtocolInterfaces(
            &path_handle, device_path_guid, &path1,
            marker1_guid, &interface1, NULL) != EFI_SUCCESS ||
        path_handle == NULL) {
        goto out;
    }
    path_installed = 1;
    if (bs->InstallMultipleProtocolInterfaces(
            &duplicate, device_path_guid, &path2, NULL) !=
            EFI_ALREADY_STARTED || duplicate != NULL) {
        goto out;
    }
    ok = 1;

out:
    if (path_installed &&
        bs->UninstallMultipleProtocolInterfaces(
            path_handle, marker1_guid, &interface1,
            device_path_guid, &path1, NULL) != EFI_SUCCESS) {
        ok = 0;
    }
    if (handle_installed &&
        bs->UninstallMultipleProtocolInterfaces(
            handle, marker1_guid, &interface1,
            marker2_guid, &interface2, NULL) != EFI_SUCCESS) {
        ok = 0;
    }
    return ok;
}

#define TEST_CONNECT_DRIVER_COUNT 8U

static EFI_HANDLE test_connect_driver_handles[TEST_CONNECT_DRIVER_COUNT];
static EFI_DRIVER_BINDING_PROTOCOL
    test_connect_bindings[TEST_CONNECT_DRIVER_COUNT];
static TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL test_family_overrides[2];
static EFI_HANDLE test_primary_controller;
static EFI_HANDLE test_secondary_controller;
static UINT8 test_start_order[TEST_CONNECT_DRIVER_COUNT + 1U];
static UINTN test_start_count;

static UINTN test_binding_index(EFI_DRIVER_BINDING_PROTOCOL *This)
{
    UINTN i;

    for (i = 0; i < TEST_CONNECT_DRIVER_COUNT; i++) {
        if (This == &test_connect_bindings[i]) {
            return i;
        }
    }
    return TEST_CONNECT_DRIVER_COUNT;
}

static EFI_STATUS test_driver_supported(EFI_DRIVER_BINDING_PROTOCOL *This,
                                        EFI_HANDLE ControllerHandle,
                                        VOID *RemainingDevicePath)
{
    UINTN index = test_binding_index(This);

    if (RemainingDevicePath != NULL || index >= TEST_CONNECT_DRIVER_COUNT) {
        return EFI_UNSUPPORTED;
    }
    if ((ControllerHandle == test_primary_controller && index < 7U) ||
        (ControllerHandle == test_secondary_controller && index == 7U)) {
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

static EFI_STATUS test_driver_start(EFI_DRIVER_BINDING_PROTOCOL *This,
                                    EFI_HANDLE ControllerHandle,
                                    VOID *RemainingDevicePath)
{
    UINTN index = test_binding_index(This);

    if (test_driver_supported(This, ControllerHandle,
                              RemainingDevicePath) != EFI_SUCCESS ||
        test_start_count >= sizeof(test_start_order)) {
        return EFI_DEVICE_ERROR;
    }
    test_start_order[test_start_count++] = (UINT8)index;
    return index == 7U ? EFI_ALREADY_STARTED : EFI_SUCCESS;
}

static EFI_STATUS test_driver_stop(EFI_DRIVER_BINDING_PROTOCOL *This,
                                   EFI_HANDLE ControllerHandle,
                                   UINTN NumberOfChildren,
                                   EFI_HANDLE *ChildHandleBuffer)
{
    (void)This;
    (void)ControllerHandle;
    (void)NumberOfChildren;
    (void)ChildHandleBuffer;
    return EFI_SUCCESS;
}

static EFI_STATUS test_platform_get_driver(
    TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL *This,
    EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle)
{
    (void)This;
    if (DriverImageHandle == NULL ||
        ControllerHandle != test_primary_controller) {
        return EFI_NOT_FOUND;
    }
    if (*DriverImageHandle == NULL) {
        *DriverImageHandle = test_connect_driver_handles[1];
        return EFI_SUCCESS;
    }
    return *DriverImageHandle == test_connect_driver_handles[1] ?
        EFI_NOT_FOUND : EFI_INVALID_PARAMETER;
}

static EFI_STATUS test_bus_get_driver(
    TEST_BUS_DRIVER_OVERRIDE_PROTOCOL *This,
    EFI_HANDLE *DriverImageHandle)
{
    (void)This;
    if (DriverImageHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (*DriverImageHandle == NULL) {
        *DriverImageHandle = test_connect_driver_handles[4];
        return EFI_SUCCESS;
    }
    return *DriverImageHandle == test_connect_driver_handles[4] ?
        EFI_NOT_FOUND : EFI_INVALID_PARAMETER;
}

static UINT32 test_family_get_version(
    TEST_DRIVER_FAMILY_OVERRIDE_PROTOCOL *This)
{
    return This == &test_family_overrides[0] ? 10U : 20U;
}

static BOOLEAN test_controller_services(EFI_SYSTEM_TABLE *SystemTable)
{
    static const UINT8 expected_order[] = { 0, 1, 3, 2, 4, 6, 5 };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    TEST_PLATFORM_DRIVER_OVERRIDE_PROTOCOL platform = {
        test_platform_get_driver, NULL, NULL,
    };
    TEST_BUS_DRIVER_OVERRIDE_PROTOCOL bus = { test_bus_get_driver };
    EFI_HANDLE platform_handle = NULL;
    EFI_HANDLE explicit_drivers[2];
    UINT8 primary_token = 1;
    UINT8 secondary_token = 2;
    BOOLEAN family_installed[2] = { 0, 0 };
    BOOLEAN platform_installed = 0;
    BOOLEAN bus_installed = 0;
    BOOLEAN primary_installed = 0;
    BOOLEAN secondary_installed = 0;
    BOOLEAN cleanup_ok = 1;
    BOOLEAN ok = 0;
    UINTN installed_drivers = 0;
    UINTN i;
    EFI_STATUS status;

    zero_bytes(test_connect_driver_handles,
               sizeof(test_connect_driver_handles));
    zero_bytes(test_connect_bindings, sizeof(test_connect_bindings));
    zero_bytes(test_start_order, sizeof(test_start_order));
    test_start_count = 0;
    test_primary_controller = NULL;
    test_secondary_controller = NULL;
    test_family_overrides[0].GetVersion = test_family_get_version;
    test_family_overrides[1].GetVersion = test_family_get_version;

    status = bs->InstallProtocolInterface(
        &test_primary_controller, connection_test_guid,
        EFI_NATIVE_INTERFACE, &primary_token);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    primary_installed = 1;
    status = bs->InstallProtocolInterface(
        &test_secondary_controller, connection_test_guid,
        EFI_NATIVE_INTERFACE, &secondary_token);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    secondary_installed = 1;

    for (i = 0; i < TEST_CONNECT_DRIVER_COUNT; i++) {
        EFI_HANDLE handle = NULL;

        test_connect_bindings[i].Supported = test_driver_supported;
        test_connect_bindings[i].Start = test_driver_start;
        test_connect_bindings[i].Stop = test_driver_stop;
        test_connect_bindings[i].Version = 0x100U + (UINT32)i;
        status = bs->InstallProtocolInterface(
            &handle, driver_binding_guid, EFI_NATIVE_INTERFACE,
            &test_connect_bindings[i]);
        if (status != EFI_SUCCESS) {
            goto out;
        }
        test_connect_driver_handles[i] = handle;
        test_connect_bindings[i].ImageHandle = handle;
        test_connect_bindings[i].DriverBindingHandle = handle;
        installed_drivers++;
    }

    for (i = 0; i < 2U; i++) {
        status = bs->InstallProtocolInterface(
            &test_connect_driver_handles[2U + i], family_override_guid,
            EFI_NATIVE_INTERFACE, &test_family_overrides[i]);
        if (status != EFI_SUCCESS) {
            goto out;
        }
        family_installed[i] = 1;
    }
    status = bs->InstallProtocolInterface(
        &test_primary_controller, bus_override_guid,
        EFI_NATIVE_INTERFACE, &bus);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    bus_installed = 1;
    status = bs->InstallProtocolInterface(
        &platform_handle, platform_override_guid,
        EFI_NATIVE_INTERFACE, &platform);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    platform_installed = 1;

    explicit_drivers[0] = test_connect_driver_handles[0];
    explicit_drivers[1] = NULL;
    status = bs->ConnectController(test_primary_controller,
                                   explicit_drivers, NULL, 0);
    if (status != EFI_SUCCESS ||
        test_start_count != sizeof(expected_order) ||
        !ia64_bytes_equal(test_start_order, expected_order,
                          sizeof(expected_order))) {
        goto out;
    }

    test_start_count = 0;
    explicit_drivers[0] = test_connect_driver_handles[7];
    status = bs->ConnectController(test_secondary_controller,
                                   explicit_drivers, NULL, 0);
    if (status != EFI_NOT_FOUND || test_start_count != 1U ||
        test_start_order[0] != 7U ||
        bs->DisconnectController(test_secondary_controller,
                                 NULL, NULL) != EFI_SUCCESS ||
        bs->DisconnectController(test_secondary_controller,
                                 test_connect_driver_handles[0], NULL) !=
            EFI_SUCCESS ||
        bs->DisconnectController(test_secondary_controller,
                                 (EFI_HANDLE)(UINTN)1, NULL) !=
            EFI_INVALID_PARAMETER ||
        bs->DisconnectController(test_secondary_controller,
                                 platform_handle, NULL) !=
            EFI_INVALID_PARAMETER ||
        bs->DisconnectController(test_secondary_controller, NULL,
                                 (EFI_HANDLE)(UINTN)1) !=
            EFI_INVALID_PARAMETER) {
        goto out;
    }
    ok = 1;

out:
    if (platform_installed &&
        bs->UninstallProtocolInterface(platform_handle,
                                       platform_override_guid,
                                       &platform) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (bus_installed &&
        bs->UninstallProtocolInterface(test_primary_controller,
                                       bus_override_guid,
                                       &bus) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    for (i = 2U; i > 0; i--) {
        UINTN index = i - 1U;

        if (family_installed[index] &&
            bs->UninstallProtocolInterface(
                test_connect_driver_handles[2U + index],
                family_override_guid, &test_family_overrides[index]) !=
                EFI_SUCCESS) {
            cleanup_ok = 0;
        }
    }
    while (installed_drivers != 0) {
        installed_drivers--;
        if (bs->UninstallProtocolInterface(
                test_connect_driver_handles[installed_drivers],
                driver_binding_guid,
                &test_connect_bindings[installed_drivers]) != EFI_SUCCESS) {
            cleanup_ok = 0;
        }
    }
    if (secondary_installed &&
        bs->UninstallProtocolInterface(test_secondary_controller,
                                       connection_test_guid,
                                       &secondary_token) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (primary_installed &&
        bs->UninstallProtocolInterface(test_primary_controller,
                                       connection_test_guid,
                                       &primary_token) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    test_primary_controller = NULL;
    test_secondary_controller = NULL;
    return ok && cleanup_ok;
}

static TEST_LOAD_FILE_PROTOCOL test_load_file_protocols[2];
static UINTN test_load_file_calls[2];
static BOOLEAN test_load_file_policy;
static BOOLEAN test_load_file_policy_ok;
static BOOLEAN test_load_file2_not_found;

static EFI_STATUS test_load_file_callback(TEST_LOAD_FILE_PROTOCOL *This,
                                          VOID *FilePath,
                                          BOOLEAN BootPolicy,
                                          UINTN *BufferSize, VOID *Buffer)
{
    TEST_DEVICE_PATH_NODE *node = (TEST_DEVICE_PATH_NODE *)FilePath;
    UINTN index;
    UINTN i;

    if (This == &test_load_file_protocols[0]) {
        index = 0;
    } else if (This == &test_load_file_protocols[1]) {
        index = 1;
    } else {
        return EFI_INVALID_PARAMETER;
    }
    test_load_file_calls[index]++;
    if (BootPolicy != test_load_file_policy) {
        test_load_file_policy_ok = 0;
    }
    if (node == NULL || node->Type != 0x04U || node->SubType != 0x04U ||
        node->Length < sizeof(*node)) {
        return EFI_INVALID_PARAMETER;
    }
    if (index == 1U && test_load_file2_not_found) {
        return EFI_NOT_FOUND;
    }
    if (BufferSize == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (Buffer == NULL || *BufferSize < 64U) {
        *BufferSize = 64U;
        return EFI_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < 64U; i++) {
        ((UINT8 *)Buffer)[i] = 0;
    }
    *BufferSize = 64U;
    return EFI_SUCCESS;
}

static BOOLEAN test_image_services(EFI_HANDLE ImageHandle,
                                   EFI_SYSTEM_TABLE *SystemTable)
{
    struct {
        TEST_DEVICE_PATH_NODE Pci;
        UINT8 Function;
        UINT8 Device;
        TEST_DEVICE_PATH_NODE End;
    } __attribute__((packed, aligned(8))) provider_path = {
        { 0x01, 0x01, 6 }, 0, 31, { 0x7f, 0xff, 4 },
    };
    struct {
        TEST_DEVICE_PATH_NODE Pci;
        UINT8 Function;
        UINT8 Device;
        TEST_DEVICE_PATH_NODE File;
        CHAR16 Name[2];
        TEST_DEVICE_PATH_NODE End;
    } __attribute__((packed, aligned(8))) file_path = {
        { 0x01, 0x01, 6 }, 0, 31,
        { 0x04, 0x04, 8 }, { 'x', 0 }, { 0x7f, 0xff, 4 },
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_HANDLE provider = NULL;
    EFI_HANDLE image = (EFI_HANDLE)(UINTN)1;
    UINT8 invalid_image[1] = { 0 };
    UINT8 truncated_file_header[68] __attribute__((aligned(8)));
    UINT8 truncated_optional_header[88] __attribute__((aligned(8)));
    BOOLEAN path_installed = 0;
    BOOLEAN load_file_installed = 0;
    BOOLEAN load_file2_installed = 0;
    BOOLEAN cleanup_ok = 1;
    BOOLEAN ok = 0;

    zero_bytes(truncated_file_header, sizeof(truncated_file_header));
    truncated_file_header[0] = 'M';
    truncated_file_header[1] = 'Z';
    truncated_file_header[60] = 64U;
    truncated_file_header[64] = 'P';
    truncated_file_header[65] = 'E';
    zero_bytes(truncated_optional_header, sizeof(truncated_optional_header));
    truncated_optional_header[0] = 'M';
    truncated_optional_header[1] = 'Z';
    truncated_optional_header[60] = 64U;
    truncated_optional_header[64] = 'P';
    truncated_optional_header[65] = 'E';
    truncated_optional_header[68] = 0x00U;
    truncated_optional_header[69] = 0x02U;
    truncated_optional_header[84] = 0xf0U;

    if (bs->LoadImage(0, NULL, NULL, invalid_image,
                      sizeof(invalid_image), &image) !=
            EFI_INVALID_PARAMETER || image != NULL ||
        bs->LoadImage(0, ImageHandle, NULL, NULL, 0, &image) !=
            EFI_NOT_FOUND || image != NULL ||
        bs->LoadImage(0, ImageHandle, NULL, invalid_image, 0, &image) !=
            EFI_INVALID_PARAMETER || image != NULL ||
        bs->LoadImage(0, ImageHandle, NULL, invalid_image,
                      sizeof(invalid_image), NULL) != EFI_INVALID_PARAMETER ||
        bs->LoadImage(0, ImageHandle, NULL, truncated_file_header,
                      sizeof(truncated_file_header), &image) !=
            EFI_LOAD_ERROR || image != NULL ||
        bs->LoadImage(0, ImageHandle, NULL, truncated_optional_header,
                      sizeof(truncated_optional_header), &image) !=
            EFI_LOAD_ERROR || image != NULL) {
        return 0;
    }

    test_load_file_protocols[0].LoadFile = test_load_file_callback;
    test_load_file_protocols[1].LoadFile = test_load_file_callback;
    if (bs->InstallProtocolInterface(&provider, device_path_guid,
                                     EFI_NATIVE_INTERFACE,
                                     &provider_path) != EFI_SUCCESS) {
        goto out;
    }
    path_installed = 1;
    if (bs->InstallProtocolInterface(&provider, load_file2_guid,
                                     EFI_NATIVE_INTERFACE,
                                     &test_load_file_protocols[1]) !=
            EFI_SUCCESS) {
        goto out;
    }
    load_file2_installed = 1;
    if (bs->InstallProtocolInterface(&provider, load_file_guid,
                                     EFI_NATIVE_INTERFACE,
                                     &test_load_file_protocols[0]) !=
            EFI_SUCCESS) {
        goto out;
    }
    load_file_installed = 1;

    zero_bytes(test_load_file_calls, sizeof(test_load_file_calls));
    test_load_file_policy = 0;
    test_load_file_policy_ok = 1;
    test_load_file2_not_found = 0;
    image = (EFI_HANDLE)(UINTN)1;
    if (bs->LoadImage(0, ImageHandle, &file_path, NULL, 0, &image) !=
            EFI_LOAD_ERROR || image != NULL ||
        test_load_file_calls[0] != 0 || test_load_file_calls[1] != 2U ||
        !test_load_file_policy_ok) {
        goto out;
    }

    zero_bytes(test_load_file_calls, sizeof(test_load_file_calls));
    test_load_file2_not_found = 1;
    image = (EFI_HANDLE)(UINTN)1;
    if (bs->LoadImage(0, ImageHandle, &file_path, NULL, 0, &image) !=
            EFI_LOAD_ERROR || image != NULL ||
        test_load_file_calls[0] != 2U || test_load_file_calls[1] != 1U ||
        !test_load_file_policy_ok) {
        goto out;
    }

    zero_bytes(test_load_file_calls, sizeof(test_load_file_calls));
    test_load_file_policy = 1;
    test_load_file_policy_ok = 1;
    test_load_file2_not_found = 0;
    image = (EFI_HANDLE)(UINTN)1;
    if (bs->LoadImage(1, ImageHandle, &file_path, NULL, 0, &image) !=
            EFI_LOAD_ERROR || image != NULL ||
        test_load_file_calls[0] != 2U || test_load_file_calls[1] != 0 ||
        !test_load_file_policy_ok) {
        goto out;
    }
    ok = 1;

out:
    if (load_file_installed &&
        bs->UninstallProtocolInterface(provider, load_file_guid,
                                       &test_load_file_protocols[0]) !=
            EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (load_file2_installed &&
        bs->UninstallProtocolInterface(provider, load_file2_guid,
                                       &test_load_file_protocols[1]) !=
            EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (path_installed &&
        bs->UninstallProtocolInterface(provider, device_path_guid,
                                       &provider_path) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    return ok && cleanup_ok;
}

#define START_IMAGE_CHILD_SIGNATURE 0x4941363453544152ULL

typedef struct {
    UINT64 Signature;
    EFI_HANDLE Controller;
    VOID *Interface;
    BOOLEAN UseExit;
} TEST_START_IMAGE_CHILD_OPTIONS;

static EFI_HANDLE test_start_image_controller;
static UINTN test_start_image_start_count;
static EFI_BOOT_SERVICES *test_start_image_bs;

static EFI_STATUS test_start_image_supported(
    EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
    VOID *RemainingDevicePath)
{
    VOID *interface = NULL;

    (void)This;
    (void)RemainingDevicePath;
    if (Controller != test_start_image_controller) {
        return EFI_UNSUPPORTED;
    }
    return test_start_image_bs != NULL &&
           test_start_image_bs->HandleProtocol(
               Controller, start_image_change_guid, &interface) ==
               EFI_SUCCESS &&
           interface != NULL ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static EFI_STATUS test_start_image_start(EFI_DRIVER_BINDING_PROTOCOL *This,
                                         EFI_HANDLE Controller,
                                         VOID *RemainingDevicePath)
{
    (void)This;
    (void)RemainingDevicePath;
    if (Controller != test_start_image_controller) {
        return EFI_UNSUPPORTED;
    }
    test_start_image_start_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS test_start_image_stop(EFI_DRIVER_BINDING_PROTOCOL *This,
                                        EFI_HANDLE Controller,
                                        UINTN NumberOfChildren,
                                        EFI_HANDLE *ChildHandleBuffer)
{
    (void)This;
    (void)Controller;
    (void)NumberOfChildren;
    (void)ChildHandleBuffer;
    return EFI_SUCCESS;
}

static BOOLEAN test_start_image_connect(EFI_HANDLE ImageHandle,
                                        EFI_SYSTEM_TABLE *SystemTable)
{
    struct {
        TEST_DEVICE_PATH_NODE File;
        CHAR16 Name[20];
        TEST_DEVICE_PATH_NODE End;
    } __attribute__((packed, aligned(8))) path = {
        { 0x04, 0x04, sizeof(TEST_DEVICE_PATH_NODE) + 20U * sizeof(CHAR16) },
        {
            '\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', '\\',
            'S', 'T', 'A', 'R', 'T', '.', 'E', 'F', 'I', 0,
        },
        { 0x7f, 0xff, sizeof(TEST_DEVICE_PATH_NODE) },
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_DRIVER_BINDING_PROTOCOL binding;
    TEST_START_IMAGE_CHILD_OPTIONS options;
    EFI_HANDLE driver = NULL;
    EFI_HANDLE child = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    UINTN base_interface = 1;
    UINTN change_interface = 2;
    BOOLEAN base_installed = 0;
    BOOLEAN binding_installed = 0;
    BOOLEAN change_installed = 0;
    BOOLEAN cleanup_ok = 1;
    BOOLEAN ok = 0;
    UINTN pass;
    EFI_STATUS status;

    test_start_image_controller = NULL;
    test_start_image_start_count = 0;
    test_start_image_bs = bs;
    if (bs->InstallProtocolInterface(&test_start_image_controller,
                                     start_image_base_guid,
                                     EFI_NATIVE_INTERFACE,
                                     &base_interface) != EFI_SUCCESS) {
        goto out;
    }
    base_installed = 1;
    zero_bytes(&binding, sizeof(binding));
    binding.Supported = test_start_image_supported;
    binding.Start = test_start_image_start;
    binding.Stop = test_start_image_stop;
    binding.Version = 1;
    binding.ImageHandle = ImageHandle;
    if (bs->InstallProtocolInterface(&driver, driver_binding_guid,
                                     EFI_NATIVE_INTERFACE,
                                     &binding) != EFI_SUCCESS) {
        goto out;
    }
    binding_installed = 1;
    binding.DriverBindingHandle = driver;

    options.Signature = START_IMAGE_CHILD_SIGNATURE;
    options.Controller = test_start_image_controller;
    options.Interface = &change_interface;
    for (pass = 0; pass < 2U; pass++) {
        VOID *installed_change = NULL;
        VOID *loaded_device_path = NULL;
        VOID *hii_package_list = NULL;

        options.UseExit = pass != 0;
        child = NULL;
        loaded = NULL;
        status = bs->LoadImage(0, ImageHandle, &path, NULL, 0, &child);
        if (status != EFI_SUCCESS || child == NULL ||
            bs->HandleProtocol(child, loaded_image_guid,
                               (VOID **)&loaded) != EFI_SUCCESS ||
            loaded == NULL ||
            bs->HandleProtocol(child, loaded_image_device_path_guid,
                               &loaded_device_path) != EFI_SUCCESS ||
            loaded_device_path == NULL || loaded_device_path == &path ||
            !ia64_bytes_equal(loaded_device_path, &path, sizeof(path)) ||
            loaded->FilePath != loaded_device_path ||
            bs->HandleProtocol(child, hii_package_list_guid,
                               &hii_package_list) != EFI_SUCCESS ||
            hii_package_list == NULL ||
            get_u32((UINT8 *)hii_package_list + 16U) != 24U ||
            get_u32((UINT8 *)hii_package_list + 20U) != 0xdf000004U) {
            goto out;
        }
        loaded->LoadOptions = &options;
        loaded->LoadOptionsSize = sizeof(options);
        status = bs->StartImage(child, NULL, NULL);
        if (status != EFI_SUCCESS ||
            test_start_image_start_count != pass + 1U ||
            bs->HandleProtocol(test_start_image_controller,
                               start_image_change_guid,
                               &installed_change) != EFI_SUCCESS ||
            installed_change != &change_interface) {
            goto out;
        }
        change_installed = 1;
        if (bs->UninstallProtocolInterface(test_start_image_controller,
                                           start_image_change_guid,
                                           &change_interface) != EFI_SUCCESS) {
            goto out;
        }
        change_installed = 0;
    }
    ok = 1;

out:
    if (change_installed &&
        bs->UninstallProtocolInterface(test_start_image_controller,
                                       start_image_change_guid,
                                       &change_interface) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (binding_installed &&
        bs->UninstallProtocolInterface(driver, driver_binding_guid,
                                       &binding) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    if (base_installed &&
        bs->UninstallProtocolInterface(test_start_image_controller,
                                       start_image_base_guid,
                                       &base_interface) != EFI_SUCCESS) {
        cleanup_ok = 0;
    }
    test_start_image_controller = NULL;
    test_start_image_bs = NULL;
    return ok && cleanup_ok;
}

static BOOLEAN test_memory_primitives(EFI_SYSTEM_TABLE *SystemTable)
{
    static UINT8 input[] = "123456789";
    UINT8 source[32];
    UINT8 destination[32];
    UINT32 crc = 0;
    UINTN i;

    if (SystemTable->BootServices->CalculateCrc32(
            input, sizeof(input) - 1U, &crc) != EFI_SUCCESS ||
        crc != 0xcbf43926U) {
        return 0;
    }
    for (i = 0; i < sizeof(source); i++) {
        source[i] = (UINT8)(i * 7U + 3U);
        destination[i] = 0;
    }
    SystemTable->BootServices->CopyMem(destination, source, sizeof(source));
    if (!ia64_bytes_equal(destination, source, sizeof(source))) {
        return 0;
    }
    SystemTable->BootServices->SetMem(destination, sizeof(destination), 0xa5);
    for (i = 0; i < sizeof(destination); i++) {
        if (destination[i] != 0xa5U) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN test_time_services(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_RUNTIME_SERVICES *rs = SystemTable->RuntimeServices;
    EFI_TIME_CAPABILITIES capabilities;
    EFI_TIME now;
    EFI_TIME wake;
    BOOLEAN enabled = 0;
    BOOLEAN pending = 0;

    zero_bytes(&capabilities, sizeof(capabilities));
    if (rs->GetTime(&now, &capabilities) != EFI_SUCCESS ||
        now.Year < 1998U || now.Year > 9999U ||
        now.Month < 1U || now.Month > 12U ||
        now.Day < 1U || now.Day > 31U || now.Hour > 23U ||
        now.Minute > 59U || now.Second > 59U ||
        now.Nanosecond > 999999999U ||
        (now.TimeZone != 2047 &&
         (now.TimeZone < -1440 || now.TimeZone > 1440)) ||
        (now.Daylight & ~3U) != 0 || capabilities.Resolution == 0 ||
        capabilities.SetsToZero > 1U ||
        rs->GetTime(NULL, NULL) != EFI_INVALID_PARAMETER ||
        rs->GetWakeupTime(NULL, &pending, &wake) != EFI_INVALID_PARAMETER ||
        rs->GetWakeupTime(&enabled, NULL, &wake) != EFI_INVALID_PARAMETER ||
        rs->GetWakeupTime(&enabled, &pending, NULL) != EFI_INVALID_PARAMETER ||
        rs->GetWakeupTime(&enabled, &pending, &wake) != EFI_SUCCESS ||
        rs->SetWakeupTime(1, NULL) != EFI_INVALID_PARAMETER ||
        rs->SetWakeupTime(1, &now) != EFI_SUCCESS ||
        rs->GetWakeupTime(&enabled, &pending, &wake) != EFI_SUCCESS ||
        !enabled || !pending ||
        !ia64_bytes_equal(&wake, &now, sizeof(now)) ||
        rs->SetWakeupTime(0, NULL) != EFI_SUCCESS ||
        rs->GetWakeupTime(&enabled, &pending, &wake) != EFI_SUCCESS ||
        enabled || pending) {
        return 0;
    }
    return 1;
}

static BOOLEAN variable_name_matches(const CHAR16 *Name, const CHAR16 *Want)
{
    UINTN index = 0;

    while (Name[index] != 0 && Want[index] != 0) {
        if (Name[index] != Want[index]) {
            return 0;
        }
        index++;
    }
    return Name[index] == Want[index];
}

static BOOLEAN test_console_output_device_paths(EFI_RUNTIME_SERVICES *Rs)
{
    UINT8 path[TEST_CONOUT_DEV_SIZE];
    UINT32 attributes = 0;
    UINTN size = sizeof(path);

    return Rs->GetVariable(conout_dev_name, efi_global_variable_guid,
                           &attributes, &size, path) == EFI_SUCCESS &&
           attributes == (EFI_VARIABLE_BOOTSERVICE_ACCESS |
                          EFI_VARIABLE_RUNTIME_ACCESS) &&
           size == sizeof(path) &&
           path[0] == 0x02U && path[1] == 0x01U &&
           get_u16(path + 2U) == 12U &&
           get_u32(path + 4U) == 0x0a0341d0U &&
           get_u32(path + 8U) == 0 &&
           path[12U] == 0x01U && path[13U] == 0x01U &&
           get_u16(path + 14U) == 6U &&
           path[16U] == 0 && path[17U] == 5U &&
           path[18U] == 0x7fU && path[19U] == 0x01U &&
           get_u16(path + 20U) == 4U &&
           path[22U] == 0x02U && path[23U] == 0x01U &&
           get_u16(path + 24U) == 12U &&
           get_u32(path + 26U) == TEST_DEVICE_PATH_PNP0501 &&
           get_u32(path + 30U) == 0 &&
           path[34U] == 0x03U && path[35U] == 0x0eU &&
           get_u16(path + 36U) == 19U &&
           get_u32(path + 38U) == 0 &&
           get_u64(path + 42U) == 115200U &&
           path[50U] == 8U && path[51U] == 1U && path[52U] == 1U &&
           path[53U] == 0x7fU && path[54U] == 0xffU &&
           get_u16(path + 55U) == 4U;
}

static BOOLEAN test_variable_services(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_RUNTIME_SERVICES *rs = SystemTable->RuntimeServices;
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    const UINT32 requested_attributes =
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS;
    static UINT8 value[] = { 0x49, 0x41, 0x36, 0x34, 0x10, 0x20 };
    UINT8 readback[sizeof(value)];
    CHAR16 next_name[80];
    UINT8 next_guid[16];
    UINT32 attributes = 0;
    UINT32 high_count = 0;
    UINT32 next_high_count = 0;
    UINT64 monotonic_count = 0;
    UINT64 next_monotonic_count = 0;
    UINT64 maximum = 0;
    UINT64 remaining = 0;
    UINT64 maximum_variable = 0;
    UINTN size = sizeof(readback);
    UINTN name_size;
    UINTN i;
    EFI_STATUS status;
    BOOLEAN found = 0;
    BOOLEAN deleted = 0;
    BOOLEAN ok = 0;

    if (!test_console_output_device_paths(rs) ||
        rs->SetVariable(
            variable_name, variable_guid, requested_attributes,
            sizeof(value), value) != EFI_SUCCESS ||
        rs->GetVariable(variable_name, variable_guid, &attributes, &size,
                        readback) != EFI_SUCCESS ||
        size != sizeof(value) ||
        !ia64_bytes_equal(value, readback, sizeof(value)) ||
        attributes != requested_attributes ||
        rs->QueryVariableInfo(
            requested_attributes,
            &maximum, &remaining, &maximum_variable) != EFI_SUCCESS ||
        maximum == 0 || remaining > maximum || maximum_variable == 0 ||
        bs->GetNextMonotonicCount(&monotonic_count) != EFI_SUCCESS ||
        bs->GetNextMonotonicCount(&next_monotonic_count) != EFI_SUCCESS ||
        next_monotonic_count != monotonic_count + 1U ||
        rs->GetNextHighMonotonicCount(&high_count) != EFI_SUCCESS ||
        rs->GetNextHighMonotonicCount(&next_high_count) != EFI_SUCCESS ||
        next_high_count != high_count + 1U) {
        goto out;
    }

    for (i = 0; i < sizeof(next_name) / sizeof(next_name[0]); i++) {
        next_name[i] = 0;
    }
    for (i = 0; i < sizeof(next_guid); i++) {
        next_guid[i] = 0;
    }
    for (i = 0; i < 256U; i++) {
        name_size = sizeof(next_name);
        status = rs->GetNextVariableName(&name_size, next_name, next_guid);
        if (status != EFI_SUCCESS || next_name[0] == 0) {
            goto out;
        }
        if (ia64_bytes_equal(next_guid, variable_guid,
                             sizeof(variable_guid)) &&
            variable_name_matches(next_name, variable_name)) {
            found = 1;
            break;
        }
    }
    if (!found ||
        rs->SetVariable(variable_name, variable_guid, 0, 0, NULL) !=
            EFI_SUCCESS) {
        goto out;
    }
    deleted = 1;
    size = sizeof(readback);
    attributes = 0;
    if (rs->GetVariable(variable_name, variable_guid, &attributes, &size,
                        readback) != EFI_NOT_FOUND) {
        goto out;
    }
    ok = 1;
out:
    if (!deleted) {
        (void)rs->SetVariable(variable_name, variable_guid, 0, 0, NULL);
    }
    return ok;
}

static BOOLEAN test_block_disk_protocols(EFI_SYSTEM_TABLE *SystemTable,
                                         EFI_LOADED_IMAGE_PROTOCOL *Loaded)
{
    EFI_BLOCK_IO_PROTOCOL *block = NULL;
    EFI_DISK_IO_PROTOCOL *disk = NULL;
    UINT8 block_data[512];
    UINT8 disk_data[64];

    if (Loaded == NULL || Loaded->DeviceHandle == NULL ||
        SystemTable->BootServices->HandleProtocol(
            Loaded->DeviceHandle, block_io_guid,
            (VOID **)&block) != EFI_SUCCESS ||
        SystemTable->BootServices->HandleProtocol(
            Loaded->DeviceHandle, disk_io_guid,
            (VOID **)&disk) != EFI_SUCCESS ||
        block == NULL || block->Media == NULL || disk == NULL ||
        !block->Media->MediaPresent || block->Media->BlockSize != 512U ||
        block->ReadBlocks(block, block->Media->MediaId, 0,
                          sizeof(block_data), block_data) != EFI_SUCCESS ||
        disk->ReadDisk(disk, block->Media->MediaId, 3,
                       sizeof(disk_data), disk_data) != EFI_SUCCESS) {
        return 0;
    }
    return 1;
}

static BOOLEAN test_pci_root_io(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    UINT32 device_id = 0;

    return SystemTable->BootServices->LocateProtocol(
               pci_root_guid, NULL, (VOID **)&root) == EFI_SUCCESS &&
           root != NULL && root->Pci.Read != NULL &&
           root->SegmentNumber == 0 &&
           root->Pci.Read(root, EfiPciWidthUint32, 1ULL << 16, 1,
                          &device_id) == EFI_SUCCESS &&
           device_id == 0x29228086U;
}

static BOOLEAN test_pci_root_resources(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    VOID *resources = NULL;
    const TEST_QWORD_RESOURCE *bus;
    const TEST_QWORD_RESOURCE *io;
    const TEST_QWORD_RESOURCE *memory;
    const UINT8 *end;

    if (SystemTable->BootServices->LocateProtocol(
            pci_root_guid, NULL, (VOID **)&root) != EFI_SUCCESS ||
        root == NULL || root->Configuration == NULL ||
        root->Configuration(root, &resources) != EFI_SUCCESS ||
        resources == NULL) {
        return 0;
    }
    bus = (const TEST_QWORD_RESOURCE *)resources;
    io = (const TEST_QWORD_RESOURCE *)((const UINT8 *)bus + sizeof(*bus));
    memory = (const TEST_QWORD_RESOURCE *)((const UINT8 *)io + sizeof(*io));
    end = (const UINT8 *)memory + sizeof(*memory);

    return bus->Descriptor == 0x8aU && get_u16(&bus->Length) == 0x2bU &&
           bus->ResourceType == 2U && get_u64(&bus->Granularity) == 32U &&
           get_u64(&bus->Minimum) == 0 &&
           get_u64(&bus->Maximum) == 255U &&
           get_u64(&bus->Translation) == 0 &&
           get_u64(&bus->AddressLength) == 256U &&
           io->Descriptor == 0x8aU && get_u16(&io->Length) == 0x2bU &&
           io->ResourceType == 1U && get_u64(&io->Granularity) == 32U &&
           get_u64(&io->Minimum) == 0 &&
           get_u64(&io->Maximum) == 0xffffU &&
           get_u64(&io->Translation) == 0 &&
           get_u64(&io->AddressLength) == 0x10000U &&
           memory->Descriptor == 0x8aU &&
           get_u16(&memory->Length) == 0x2bU &&
           memory->ResourceType == 0U &&
           get_u64(&memory->Granularity) == 64U &&
           get_u64(&memory->Minimum) == TEST_PCI_MMIO_BASE &&
           get_u64(&memory->Maximum) ==
               TEST_PCI_MMIO_BASE + TEST_PCI_MMIO_SIZE - 1U &&
           get_u64(&memory->Translation) == 0 &&
           get_u64(&memory->AddressLength) == TEST_PCI_MMIO_SIZE &&
           end[0] == 0x79U && end[1] == 0;
}

static BOOLEAN test_pci_io_protocol(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_PCI_IO_PROTOCOL *pci = NULL;
    UINTN segment = ~(UINTN)0;
    UINTN bus = ~(UINTN)0;
    UINTN device = ~(UINTN)0;
    UINTN function = ~(UINTN)0;
    UINT32 identifier = 0;
    UINT32 expected;

    if (SystemTable->BootServices->LocateProtocol(
            pci_io_guid, NULL, (VOID **)&pci) != EFI_SUCCESS ||
        pci == NULL || pci->GetLocation == NULL || pci->Pci.Read == NULL ||
        pci->GetLocation(pci, &segment, &bus, &device, &function) !=
            EFI_SUCCESS || segment != 0 || bus != 0 || function != 0) {
        return 0;
    }
    switch (device) {
    case 0:
        expected = 0x06461095U;
        break;
    case 1:
        expected = 0x29228086U;
        break;
    case 2:
        expected = 0x003f106bU;
        break;
    case 3:
        expected = 0x70208086U;
        break;
    case 4:
        expected = 0x00121000U;
        break;
    case 5:
        expected = 0x50461002U;
        break;
    default:
        return 0;
    }
    return pci->Pci.Read(pci, EfiPciWidthUint32, 0, 1, &identifier) ==
               EFI_SUCCESS &&
           identifier == expected;
}

static BOOLEAN test_gop_protocol(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
    UINTN info_size = 0;
    BOOLEAN ok;

    if (SystemTable->BootServices->LocateProtocol(
            gop_guid, NULL, (VOID **)&gop) != EFI_SUCCESS ||
        gop == NULL || gop->QueryMode == NULL ||
        gop->QueryMode(gop, 0, &info_size, &info) != EFI_SUCCESS) {
        return 0;
    }
    ok = info != NULL && info_size >= sizeof(*info) &&
         info->HorizontalResolution == 640U &&
         info->VerticalResolution == 400U &&
         info->PixelsPerScanLine == 640U;
    if (info != NULL) {
        (void)SystemTable->BootServices->FreePool(info);
    }
    return ok;
}

static BOOLEAN test_tcg_protocol(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_TCG_PROTOCOL *tcg = NULL;

    return SystemTable->BootServices->LocateProtocol(
               tcg_guid, NULL, (VOID **)&tcg) == EFI_NOT_FOUND &&
           tcg == NULL;
}

static const UINT8 *find_bytes(const UINT8 *Buffer, UINTN Length,
                               const UINT8 *Needle, UINTN NeedleLength,
                               UINTN Start)
{
    UINTN i;
    UINTN j;

    if (NeedleLength == 0 || NeedleLength > Length || Start > Length) {
        return NULL;
    }
    for (i = Start; i + NeedleLength <= Length; i++) {
        for (j = 0; j < NeedleLength; j++) {
            if (Buffer[i + j] != Needle[j]) {
                break;
            }
        }
        if (j == NeedleLength) {
            return Buffer + i;
        }
    }
    return NULL;
}

static BOOLEAN aml_package(const UINT8 *LengthField, const UINT8 *Limit,
                           const UINT8 **Content, const UINT8 **End)
{
    UINT8 lead;
    UINTN follow;
    UINTN encoded;
    UINTN length;
    UINTN i;

    if (LengthField >= Limit) {
        return 0;
    }
    lead = LengthField[0];
    follow = lead >> 6;
    encoded = 1U + follow;
    if ((UINTN)(Limit - LengthField) < encoded) {
        return 0;
    }
    if (follow == 0) {
        length = lead & 0x3fU;
    } else {
        length = lead & 0x0fU;
        for (i = 0; i < follow; i++) {
            length |= (UINTN)LengthField[1U + i] << (4U + 8U * i);
        }
    }
    if (length < encoded || (UINTN)(Limit - LengthField) < length) {
        return 0;
    }
    *Content = LengthField + encoded;
    *End = LengthField + length;
    return 1;
}

static BOOLEAN aml_integer(const UINT8 **Position, const UINT8 *Limit,
                           UINT64 *Value)
{
    const UINT8 *p = *Position;

    if (p >= Limit) {
        return 0;
    }
    switch (*p++) {
    case 0x00:
        *Value = 0;
        break;
    case 0x01:
        *Value = 1;
        break;
    case 0x0a:
        if (p + 1U > Limit) {
            return 0;
        }
        *Value = *p++;
        break;
    case 0x0b:
        if (p + 2U > Limit) {
            return 0;
        }
        *Value = get_u16(p);
        p += 2;
        break;
    case 0x0c:
        if (p + 4U > Limit) {
            return 0;
        }
        *Value = get_u32(p);
        p += 4;
        break;
    case 0x0e:
        if (p + 8U > Limit) {
            return 0;
        }
        *Value = get_u64(p);
        p += 8;
        break;
    case 0xff:
        *Value = ~(UINT64)0;
        break;
    default:
        return 0;
    }
    *Position = p;
    return 1;
}

static BOOLEAN aml_named_buffer(const UINT8 *Aml, UINTN AmlLength,
                                const UINT8 Name[4], UINTN Start,
                                const UINT8 **Buffer, UINTN *BufferLength)
{
    UINTN i;

    for (i = Start; i + 7U <= AmlLength; i++) {
        const UINT8 *content;
        const UINT8 *end;
        const UINT8 *position;
        UINT64 declared_length;

        if (Aml[i] != 0x08U || Aml[i + 5U] != 0x11U ||
            !ia64_bytes_equal(Aml + i + 1U, Name, 4)) {
            continue;
        }
        if (!aml_package(Aml + i + 6U, Aml + AmlLength,
                         &content, &end)) {
            return 0;
        }
        position = content;
        if (!aml_integer(&position, end, &declared_length) ||
            declared_length > (UINT64)(end - position)) {
            return 0;
        }
        *Buffer = position;
        *BufferLength = (UINTN)declared_length;
        return 1;
    }
    return 0;
}

static BOOLEAN aml_named_byte(const UINT8 *Aml, UINTN AmlLength,
                              const UINT8 Name[4], UINT8 Value)
{
    UINTN i;

    for (i = 0; i + 7U <= AmlLength; i++) {
        if (Aml[i] == 0x08U &&
            ia64_bytes_equal(Aml + i + 1U, Name, 4) &&
            Aml[i + 5U] == 0x0aU && Aml[i + 6U] == Value) {
            return 1;
        }
    }
    return 0;
}

static BOOLEAN test_dsdt_crs(const TEST_TABLE_CONTEXT *Context)
{
    static const UINT8 crs_name[4] = { '_', 'C', 'R', 'S' };
    const UINT8 *aml;
    UINTN aml_length;
    const UINT8 *resources;
    UINTN resource_length;
    UINTN offset = 0;
    BOOLEAN bus = 0;
    BOOLEAN io = 0;
    BOOLEAN legacy_memory = 0;
    BOOLEAN rom_memory = 0;
    BOOLEAN memory = 0;
    BOOLEAN uart_window = 0;
    BOOLEAN end_tag = 0;

    if (!Context->Valid) {
        return 0;
    }
    aml = (const UINT8 *)Context->Dsdt + sizeof(TEST_SDT_HEADER);
    aml_length = get_u32((const UINT8 *)Context->Dsdt + 4) -
                 sizeof(TEST_SDT_HEADER);
    if (!aml_named_buffer(aml, aml_length, crs_name, 0,
                          &resources, &resource_length)) {
        return 0;
    }
    while (offset < resource_length) {
        const UINT8 *descriptor = resources + offset;
        UINTN length;

        if ((descriptor[0] & 0x80U) != 0) {
            if (offset + 3U > resource_length) {
                return 0;
            }
            length = get_u16(descriptor + 1U);
            if (length > resource_length - offset - 3U) {
                return 0;
            }
            if (descriptor[0] == 0x88U && length == 13U &&
                descriptor[3] == 2U && get_u16(descriptor + 6U) == 0 &&
                get_u16(descriptor + 8U) == 0 &&
                get_u16(descriptor + 10U) == 255U &&
                get_u16(descriptor + 12U) == 0 &&
                get_u16(descriptor + 14U) == 256U) {
                bus = 1;
            } else if (descriptor[0] == 0x8aU && length == 43U &&
                       descriptor[3] == 1U &&
                       get_u64(descriptor + 6U) == 0 &&
                       get_u64(descriptor + 14U) == 0 &&
                       get_u64(descriptor + 22U) == 0xffffU &&
                       get_u64(descriptor + 30U) == 0 &&
                       get_u64(descriptor + 38U) == 0x10000U) {
                io = 1;
            } else if (descriptor[0] == 0x87U && length == 23U &&
                       descriptor[3] == 0U &&
                       get_u32(descriptor + 6U) == 0 &&
                       get_u32(descriptor + 10U) == 0x000a0000U &&
                       get_u32(descriptor + 14U) == 0x000bffffU &&
                       get_u32(descriptor + 18U) == 0 &&
                       get_u32(descriptor + 22U) == 0x00020000U) {
                legacy_memory = 1;
            } else if (descriptor[0] == 0x87U && length == 23U &&
                       descriptor[3] == 0U && descriptor[4] == 0x0cU &&
                       descriptor[5] == 0x02U &&
                       get_u32(descriptor + 6U) == 0 &&
                       get_u32(descriptor + 10U) == TEST_VGA_ROM_BASE &&
                       get_u32(descriptor + 14U) ==
                           TEST_VGA_ROM_BASE + TEST_VGA_ROM_SIZE - 1U &&
                       get_u32(descriptor + 18U) == 0 &&
                       get_u32(descriptor + 22U) == TEST_VGA_ROM_SIZE) {
                rom_memory = 1;
            } else if (descriptor[0] == 0x8aU && length == 43U &&
                       descriptor[3] == 0U &&
                       get_u64(descriptor + 6U) == 0 &&
                       get_u64(descriptor + 14U) == TEST_PCI_MMIO_BASE &&
                       get_u64(descriptor + 22U) ==
                           TEST_PCI_MMIO_BASE + TEST_PCI_MMIO_SIZE - 1U &&
                       get_u64(descriptor + 30U) == 0 &&
                       get_u64(descriptor + 38U) == TEST_PCI_MMIO_SIZE) {
                memory = 1;
            } else if (descriptor[0] == 0x8aU && length == 43U &&
                       descriptor[3] == 0U && descriptor[4] == 0x0cU &&
                       get_u64(descriptor + 6U) == 0 &&
                       get_u64(descriptor + 14U) == TEST_UART_BASE &&
                       get_u64(descriptor + 22U) == TEST_UART_BASE + 7U &&
                       get_u64(descriptor + 30U) == 0 &&
                       get_u64(descriptor + 38U) == 8U) {
                uart_window = 1;
            }
            offset += 3U + length;
        } else {
            length = descriptor[0] & 7U;
            if (length > resource_length - offset - 1U) {
                return 0;
            }
            if ((descriptor[0] >> 3) == 0x0fU && length == 1U) {
                end_tag = descriptor[1] == 0;
            }
            offset += 1U + length;
        }
    }
    return bus && io && legacy_memory && rom_memory && memory &&
           uart_window && end_tag;
}

static BOOLEAN test_ssdt_legacy_crs(const TEST_TABLE_CONTEXT *Context)
{
    static const UINT8 sb_name[4] = { '_', 'S', 'B', '_' };
    static const UINT8 pci0_name[4] = { 'P', 'C', 'I', '0' };
    static const UINT8 uart_name[4] = { 'U', 'A', 'R', '0' };
    static const UINT8 uart_enabled_name[4] = { 'U', '0', 'E', 'N' };
    static const UINT8 ps2_enabled_name[4] = { 'P', '2', 'E', 'N' };
    static const UINT8 keyboard_name[4] = { 'P', 'S', '2', 'K' };
    static const UINT8 mouse_name[4] = { 'P', 'S', '2', 'M' };
    static const UINT8 crs_name[4] = { '_', 'C', 'R', 'S' };
    static const UINT8 keyboard_resources[] = {
        0x47, 0x01, 0x60, 0x00, 0x60, 0x00, 0x01, 0x01,
        0x47, 0x01, 0x64, 0x00, 0x64, 0x00, 0x01, 0x01,
        0x22, 0x02, 0x00, 0x79, 0x00,
    };
    static const UINT8 mouse_resources[] = {
        0x22, 0x00, 0x10, 0x79, 0x00,
    };
    static const UINT8 uart_resources[] = {
        0x47, 0x01, 0xf8, 0x03, 0xf8, 0x03, 0x01, 0x08,
        0x22, 0x10, 0x00, 0x79, 0x00,
    };
    const UINT8 *aml;
    UINTN aml_length;
    const UINT8 *keyboard;
    const UINT8 *mouse;
    const UINT8 *uart;
    const UINT8 *keyboard_crs;
    const UINT8 *mouse_crs;
    const UINT8 *uart_crs;
    const UINT8 *scope_content;
    const UINT8 *scope_end;
    UINTN keyboard_crs_length;
    UINTN mouse_crs_length;
    UINTN uart_crs_length;
    UINTN scope_offset;
    BOOLEAN under_pci0 = 0;

    if (!Context->Valid) {
        return 0;
    }
    aml = (const UINT8 *)Context->Ssdt + sizeof(TEST_SDT_HEADER);
    aml_length = get_u32((const UINT8 *)Context->Ssdt + 4) -
                 sizeof(TEST_SDT_HEADER);
    keyboard = find_bytes(aml, aml_length, keyboard_name,
                          sizeof(keyboard_name), 0);
    mouse = find_bytes(aml, aml_length, mouse_name, sizeof(mouse_name), 0);
    uart = find_bytes(aml, aml_length, uart_name, sizeof(uart_name), 0);
    if (keyboard == NULL || mouse == NULL || uart == NULL ||
        find_bytes(aml, aml_length, pci0_name, sizeof(pci0_name), 0) == NULL ||
        !aml_named_byte(aml, aml_length, uart_enabled_name, 0x0fU) ||
        !aml_named_byte(aml, aml_length, ps2_enabled_name, 0) ||
        !aml_named_buffer(aml, aml_length, crs_name,
                          (UINTN)(uart - aml), &uart_crs,
                          &uart_crs_length) ||
        !aml_named_buffer(aml, aml_length, crs_name,
                          (UINTN)(keyboard - aml), &keyboard_crs,
                          &keyboard_crs_length) ||
        !aml_named_buffer(aml, aml_length, crs_name,
                          (UINTN)(mouse - aml), &mouse_crs,
                          &mouse_crs_length) ||
        uart_crs_length != sizeof(uart_resources) ||
        keyboard_crs_length != sizeof(keyboard_resources) ||
        mouse_crs_length != sizeof(mouse_resources) ||
        !ia64_bytes_equal(uart_crs, uart_resources,
                          sizeof(uart_resources)) ||
        !ia64_bytes_equal(keyboard_crs, keyboard_resources,
                          sizeof(keyboard_resources)) ||
        !ia64_bytes_equal(mouse_crs, mouse_resources,
                          sizeof(mouse_resources))) {
        return 0;
    }
    for (scope_offset = 0; scope_offset + 2U < aml_length;
         scope_offset++) {
        if (aml[scope_offset] != 0x10U ||
            !aml_package(aml + scope_offset + 1U, aml + aml_length,
                         &scope_content, &scope_end)) {
            continue;
        }
        if (scope_content + 10U <= scope_end &&
            scope_content[0] == 0x5cU &&
            scope_content[1] == 0x2eU &&
            ia64_bytes_equal(scope_content + 2U,
                             sb_name, sizeof(sb_name)) &&
            ia64_bytes_equal(scope_content + 6U,
                             pci0_name, sizeof(pci0_name)) &&
            find_bytes(scope_content + 10U,
                       (UINTN)(scope_end - scope_content - 10U),
                       uart_name, sizeof(uart_name), 0) != NULL &&
            find_bytes(scope_content + 10U,
                       (UINTN)(scope_end - scope_content - 10U),
                       keyboard_name, sizeof(keyboard_name), 0) != NULL &&
            find_bytes(scope_content + 10U,
                       (UINTN)(scope_end - scope_content - 10U),
                       mouse_name, sizeof(mouse_name), 0) != NULL) {
            under_pci0 = 1;
            break;
        }
    }
    return under_pci0;
}

static BOOLEAN test_dsdt_prt(const TEST_TABLE_CONTEXT *Context)
{
    static const UINT8 prt_name[4] = { '_', 'P', 'R', 'T' };
    const UINT8 *aml;
    UINTN aml_length;
    UINTN i;
    const UINT8 *content = NULL;
    const UINT8 *end = NULL;
    const UINT8 *position;
    UINT32 seen = 0;
    UINTN entry_count = 0;

    if (!Context->Valid) {
        return 0;
    }
    aml = (const UINT8 *)Context->Dsdt + sizeof(TEST_SDT_HEADER);
    aml_length = get_u32((const UINT8 *)Context->Dsdt + 4) -
                 sizeof(TEST_SDT_HEADER);
    for (i = 0; i + 7U <= aml_length; i++) {
        if (aml[i] == 0x08U && aml[i + 5U] == 0x12U &&
            ia64_bytes_equal(aml + i + 1U, prt_name, 4)) {
            if (!aml_package(aml + i + 6U, aml + aml_length,
                             &content, &end)) {
                return 0;
            }
            break;
        }
    }
    if (content == NULL || content >= end || *content != 28U) {
        return 0;
    }
    position = content + 1U;
    while (position < end) {
        const UINT8 *inner;
        const UINT8 *inner_end;
        const UINT8 *field;
        UINT64 address;
        UINT64 pin;
        UINT64 source;
        UINT64 gsi;
        UINTN device;

        if (*position++ != 0x12U ||
            !aml_package(position, end, &inner, &inner_end) ||
            inner >= inner_end || *inner++ != 4U) {
            return 0;
        }
        field = inner;
        if (!aml_integer(&field, inner_end, &address) ||
            !aml_integer(&field, inner_end, &pin) ||
            !aml_integer(&field, inner_end, &source) ||
            !aml_integer(&field, inner_end, &gsi) || field != inner_end ||
            (address & 0xffffU) != 0xffffU || pin > 3U || source != 0) {
            return 0;
        }
        device = (UINTN)(address >> 16);
        if (device > 6U || gsi != 16U + ((device + pin) & 3U)) {
            return 0;
        }
        seen |= 1U << (device * 4U + (UINTN)pin);
        entry_count++;
        position = inner_end;
    }
    return entry_count == 28U && seen == 0x0fffffffU;
}

static BOOLEAN gas_matches(const UINT8 *Gas, UINT8 SpaceId, UINT8 Width,
                           UINT64 Address)
{
    return Gas[0] == SpaceId && Gas[1] == Width && Gas[2] == 0 &&
           Gas[3] == 0 && get_u64(Gas + 4) == Address;
}

static BOOLEAN test_configuration_tables(EFI_SYSTEM_TABLE *SystemTable,
                                         const TEST_TABLE_CONTEXT *Context)
{
    VOID *sal = find_config_table(SystemTable, sal_guid);
    VOID *smbios = find_config_table(SystemTable, smbios_guid);
    VOID *debug = find_config_table(SystemTable, debug_image_guid);
    UINTN i;
    UINTN j;

    if (SystemTable->ConfigurationTable == NULL) {
        return 0;
    }
    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        for (j = i + 1U; j < SystemTable->NumberOfTableEntries; j++) {
            if (ia64_bytes_equal(
                    SystemTable->ConfigurationTable[i].VendorGuid,
                    SystemTable->ConfigurationTable[j].VendorGuid, 16)) {
                return 0;
            }
        }
    }

    return Context->Valid && SystemTable->NumberOfTableEntries >= 6U &&
           find_config_table(SystemTable, acpi20_guid) == Context->Rsdp &&
           find_config_table(SystemTable, hcdp_guid) == Context->Hcdp &&
           sal != NULL && smbios != NULL && debug != NULL &&
           memory_range_is_mapped(&Context->MemoryMap, (UINTN)sal, 16) &&
           memory_range_is_mapped(&Context->MemoryMap, (UINTN)smbios, 31) &&
           memory_range_is_mapped(&Context->MemoryMap, (UINTN)debug, 16);
}

static BOOLEAN test_acpi_root_tables(const TEST_TABLE_CONTEXT *Context,
                                     BOOLEAN RequireDbgp)
{
    return Context->Valid && Context->RootEntryCount >= 7U &&
           Context->Rsdp != NULL && Context->Rsdt != NULL &&
           Context->Xsdt != NULL && Context->Fadt != NULL &&
           Context->Madt != NULL && Context->Srat != NULL &&
           Context->Slit != NULL && Context->Mcfg != NULL &&
           Context->Hcdp != NULL && Context->Ssdt != NULL &&
           Context->Facs != NULL && Context->Dsdt != NULL &&
           (!RequireDbgp || Context->Dbgp != NULL);
}

static BOOLEAN test_acpi_table_bounds(const TEST_TABLE_CONTEXT *Context)
{
    if (!Context->Valid ||
        !valid_sdt(&Context->MemoryMap, Context->Fadt,
                   SIG32('F', 'A', 'C', 'P'), 3, 244U) ||
        !valid_sdt(&Context->MemoryMap, Context->Madt,
                   SIG32('A', 'P', 'I', 'C'), 1, 72U) ||
        Context->Madt->Revision != 1U ||
        !valid_sdt(&Context->MemoryMap, Context->Srat,
                   SIG32('S', 'R', 'A', 'T'), 1, 104U) ||
        !valid_sdt(&Context->MemoryMap, Context->Slit,
                   SIG32('S', 'L', 'I', 'T'), 1, 45U) ||
        !valid_sdt(&Context->MemoryMap, Context->Mcfg,
                   SIG32('M', 'C', 'F', 'G'), 1, 60U) ||
        !valid_sdt(&Context->MemoryMap, Context->Hcdp,
                   SIG32('H', 'C', 'D', 'P'), 3, 92U) ||
        !valid_sdt(&Context->MemoryMap, Context->Ssdt,
                   SIG32('S', 'S', 'D', 'T'), 2, 36U) ||
        !valid_sdt(&Context->MemoryMap, Context->Dsdt,
                   SIG32('D', 'S', 'D', 'T'), 2, 36U)) {
        return 0;
    }
    return Context->Dbgp == NULL ||
           valid_sdt(&Context->MemoryMap, Context->Dbgp,
                     SIG32('D', 'B', 'G', 'P'), 1, 52U);
}

static BOOLEAN test_acpi_fadt_links_gas(const TEST_TABLE_CONTEXT *Context)
{
    const UINT8 *fadt;
    UINT32 flags;

    if (!Context->Valid ||
        get_u32((const UINT8 *)Context->Fadt + 4) < 244U) {
        return 0;
    }
    fadt = (const UINT8 *)Context->Fadt;
    flags = get_u32(fadt + 112U);
    return get_u32(fadt + 36U) == (UINT32)(UINTN)Context->Facs &&
           get_u32(fadt + 40U) == (UINT32)(UINTN)Context->Dsdt &&
           get_u64(fadt + 132U) == (UINT64)(UINTN)Context->Facs &&
           get_u64(fadt + 140U) == (UINT64)(UINTN)Context->Dsdt &&
           get_u16(fadt + 46U) == 9U &&
           get_u16(fadt + 109U) == 1U &&
           gas_matches(fadt + 116U, 1, 8, TEST_PM_IO_BASE + 0x0cU) &&
           fadt[128U] == 1U &&
           gas_matches(fadt + 148U, 1, 32, TEST_PM_IO_BASE) &&
           gas_matches(fadt + 172U, 1, 16, TEST_PM_IO_BASE + 4U) &&
           gas_matches(fadt + 208U, 1, 32, TEST_PM_IO_BASE + 8U) &&
           (flags & (1U << 0)) != 0 && (flags & (1U << 5)) != 0 &&
           (flags & (1U << 10)) != 0 && (flags & (1U << 13)) != 0 &&
           (flags & (1U << 4)) == 0;
}

static void test_acpi_cpu_names(UINTN Index, UINT8 Processor[4],
                                UINT8 Enabled[4])
{
    if (Index < 10U) {
        Processor[0] = 'C';
        Processor[1] = 'P';
        Processor[2] = 'U';
        Processor[3] = (UINT8)('0' + Index);
        Enabled[0] = 'C';
        Enabled[1] = (UINT8)('0' + Index);
        Enabled[2] = 'E';
        Enabled[3] = 'N';
        return;
    }
    Processor[0] = 'C';
    Processor[1] = 'P';
    Processor[2] = (UINT8)('0' + Index / 10U);
    Processor[3] = (UINT8)('0' + Index % 10U);
    Enabled[0] = 'E';
    Enabled[1] = (UINT8)('0' + Index / 100U);
    Enabled[2] = (UINT8)('0' + (Index / 10U) % 10U);
    Enabled[3] = (UINT8)('0' + Index % 10U);
}

static BOOLEAN test_acpi_topology(const TEST_TABLE_CONTEXT *Context)
{
    const UINT8 *madt;
    const UINT8 *srat;
    const UINT8 *ssdt;
    const UINT8 *slit;
    UINT8 processor_name[4];
    UINT8 enabled_name[4];
    UINTN madt_length;
    UINTN srat_length;
    UINTN ssdt_length;
    UINTN offset;
    UINT64 madt_present = 0;
    UINT64 madt_enabled = 0;
    UINT64 srat_present = 0;
    UINT64 srat_enabled = 0;
    BOOLEAN iosapic = 0;
    BOOLEAN low_memory = 0;

    if (!Context->Valid) {
        return 0;
    }
    ssdt = (const UINT8 *)Context->Ssdt;
    ssdt_length = get_u32(ssdt + 4U);
    if (ssdt_length < sizeof(TEST_SDT_HEADER)) {
        return 0;
    }
    for (offset = 0; offset < 64U; offset++) {
        test_acpi_cpu_names(offset, processor_name, enabled_name);
        if (find_bytes(ssdt + sizeof(TEST_SDT_HEADER),
                       ssdt_length - sizeof(TEST_SDT_HEADER),
                       processor_name, sizeof(processor_name), 0) == NULL ||
            !aml_named_byte(ssdt + sizeof(TEST_SDT_HEADER),
                            ssdt_length - sizeof(TEST_SDT_HEADER),
                            enabled_name, offset < 4U ? 0x0fU : 0)) {
            return 0;
        }
    }
    madt = (const UINT8 *)Context->Madt;
    madt_length = get_u32(madt + 4U);
    if (get_u32(madt + 36U) != 0xfee00000U || madt_length < 44U) {
        return 0;
    }
    for (offset = 44U; offset + 2U <= madt_length; ) {
        UINTN length = madt[offset + 1U];

        if (length < 2U || length > madt_length - offset) {
            return 0;
        }
        if (madt[offset] == 7U && length >= 12U) {
            UINTN id = madt[offset + 3U];
            UINT32 flags = get_u32(madt + offset + 8U);

            if (id >= 64U || madt[offset + 2U] != id ||
                madt[offset + 4U] != 0 ||
                (flags & 1U) != (id < 4U ? 1U : 0) ||
                (madt_present & (1ULL << id)) != 0) {
                return 0;
            }
            madt_present |= 1ULL << id;
            if ((flags & 1U) != 0) {
                madt_enabled |= 1ULL << id;
            }
        } else if (madt[offset] == 6U && length >= 16U) {
            iosapic = get_u32(madt + offset + 4U) == 0 &&
                get_u64(madt + offset + 8U) == 0x80110000U;
        }
        offset += length;
    }
    if (offset != madt_length || madt_present != ~(UINT64)0 ||
        madt_enabled != 0x0fU || !iosapic) {
        return 0;
    }

    srat = (const UINT8 *)Context->Srat;
    srat_length = get_u32(srat + 4U);
    if (get_u32(srat + 36U) != 1U) {
        return 0;
    }
    for (offset = 48U; offset + 2U <= srat_length; ) {
        UINTN length = srat[offset + 1U];

        if (length < 2U || length > srat_length - offset) {
            return 0;
        }
        if (srat[offset] == 1U && length >= 40U) {
            UINT64 base = get_u32(srat + offset + 8U) |
                ((UINT64)get_u32(srat + offset + 12U) << 32);
            UINT64 size = get_u32(srat + offset + 16U) |
                ((UINT64)get_u32(srat + offset + 20U) << 32);
            UINT32 flags = get_u32(srat + offset + 28U);

            if (base == 0 && size != 0 && (flags & 1U) != 0) {
                low_memory = 1;
            }
        } else if (srat[offset] == 0U && length >= 16U) {
            UINTN id = srat[offset + 3U];
            UINT32 flags = get_u32(srat + offset + 4U);

            if (id >= 64U || srat[offset + 8U] != 0 ||
                (flags & 1U) != (id < 4U ? 1U : 0) ||
                (srat_present & (1ULL << id)) != 0) {
                return 0;
            }
            srat_present |= 1ULL << id;
            if ((flags & 1U) != 0) {
                srat_enabled |= 1ULL << id;
            }
        }
        offset += length;
    }
    slit = (const UINT8 *)Context->Slit;
    return offset == srat_length && low_memory &&
           srat_present == ~(UINT64)0 && srat_enabled == 0x0fU &&
           get_u64(slit + 36U) == 1U && slit[44U] == 10U;
}

static BOOLEAN test_acpi_console_tables(const TEST_TABLE_CONTEXT *Context)
{
    const UINT8 *hcdp;

    if (!Context->Valid ||
        get_u32((const UINT8 *)Context->Hcdp + 4U) != TEST_HCDP_LENGTH) {
        return 0;
    }
    hcdp = (const UINT8 *)Context->Hcdp;
    if (get_u32(hcdp + 36U) != 1U || hcdp[40U] != 0 ||
        hcdp[41U] != 8U || hcdp[42U] != 1U || hcdp[43U] != 1U ||
        get_u32(hcdp + 44U) != 0 ||
        get_u64(hcdp + 48U) != 115200U ||
        !gas_matches(hcdp + 56U, 1, 8, TEST_UART_IO_PORT) ||
        get_u32(hcdp + 68U) != TEST_HCDP_UART_HID ||
        get_u32(hcdp + 72U) != 4U ||
        get_u32(hcdp + 76U) != 115200U || hcdp[80U] != 0x02U ||
        (hcdp[81U] & (UINT8)~TEST_HCDP_UART_PRIMARY) !=
            TEST_HCDP_UART_BASE_FLAGS ||
        get_u16(hcdp + 82U) != TEST_HCDP_UART_CONOUT_INDEX ||
        get_u32(hcdp + 84U) != 0 ||
        hcdp[88U] != 0x0aU || (hcdp[89U] & (UINT8)~1U) != 0 ||
        get_u16(hcdp + 90U) != 41U || get_u16(hcdp + 92U) != 0 ||
        hcdp[94U] != 1U || get_u16(hcdp + 96U) != 34U ||
        hcdp[99U] != 0 || hcdp[100U] != 5U || hcdp[101U] != 0) {
        return 0;
    }
    if (Context->Dbgp != NULL) {
        const UINT8 *dbgp = (const UINT8 *)Context->Dbgp;

        if (dbgp[36U] != 0 || dbgp[37U] != 0 || dbgp[38U] != 0 ||
            dbgp[39U] != 0 ||
            !gas_matches(dbgp + 40U, 0, 8, TEST_UART_BASE + 0x1000U)) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN test_acpi_mcfg(const TEST_TABLE_CONTEXT *Context)
{
    const UINT8 *mcfg;

    if (!Context->Valid || get_u32((const UINT8 *)Context->Mcfg + 4U) < 60U) {
        return 0;
    }
    mcfg = (const UINT8 *)Context->Mcfg;
    return get_u64(mcfg + 36U) == 0 &&
           get_u64(mcfg + 44U) == TEST_ECAM_BASE &&
           get_u16(mcfg + 52U) == 0 && mcfg[54U] == 0 &&
           mcfg[55U] == 255U && get_u32(mcfg + 56U) == 0;
}

static BOOLEAN test_platform_memory_descriptors(
    const TEST_TABLE_CONTEXT *Context)
{
    const TEST_MEMORY_MAP *map = &Context->MemoryMap;

    return Context->Valid &&
           memory_range_has_type(map, TEST_UART_BASE, TEST_UART_SIZE,
                                 EfiMemoryMappedIO, EFI_MEMORY_UC) &&
           /*
            * SAL 3.0 Table 3-6 maps SAL firmware address space, including
            * the RTC and NVRAM device pages, to EfiRuntimeServicesData.
            * Checking the device subranges here also verifies that the
            * single firmware-space descriptor covers both runtime devices.
            */
           memory_range_has_type(map, TEST_RTC_BASE, TEST_RTC_SIZE,
                                 EfiRuntimeServicesData,
                                 EFI_MEMORY_UC | EFI_MEMORY_RUNTIME) &&
           memory_range_has_type(map, TEST_NVRAM_BASE, TEST_NVRAM_SIZE,
                                 EfiRuntimeServicesData,
                                 EFI_MEMORY_UC | EFI_MEMORY_RUNTIME) &&
           memory_range_has_type(map, TEST_ECAM_BASE, TEST_ECAM_SIZE,
                                 EfiMemoryMappedIO,
                                 EFI_MEMORY_UC | EFI_MEMORY_RUNTIME) &&
           memory_range_has_type(map, TEST_SPARSE_IO_BASE,
                                 TEST_SPARSE_IO_SIZE,
                                 EfiMemoryMappedIOPortSpace,
                                 EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
}

static BOOLEAN test_debug_image_info(EFI_HANDLE ImageHandle,
                                     EFI_LOADED_IMAGE_PROTOCOL *Loaded,
                                     EFI_SYSTEM_TABLE *SystemTable,
                                     const TEST_TABLE_CONTEXT *Context)
{
    TEST_DEBUG_IMAGE_INFO_HEADER *header =
        (TEST_DEBUG_IMAGE_INFO_HEADER *)find_config_table(
            SystemTable, debug_image_guid);
    UINT32 status;
    UINTN count;
    UINTN i;
    BOOLEAN found = 0;

    if (!Context->Valid || Loaded == NULL || header == NULL ||
        !memory_range_is_mapped(&Context->MemoryMap, (UINTN)header,
                                sizeof(*header))) {
        return 0;
    }
    status = header->UpdateStatus;
    count = header->TableSize;
    if ((status & 1U) != 0 || (status & 2U) == 0 || count < 2U ||
        count > 9U || header->Table == NULL ||
        !memory_range_is_mapped(&Context->MemoryMap, (UINTN)header->Table,
                                count * sizeof(*header->Table))) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        TEST_DEBUG_IMAGE_INFO_NORMAL *normal = header->Table[i].NormalImage;

        if (normal == NULL ||
            !memory_range_is_mapped(&Context->MemoryMap, (UINTN)normal,
                                    sizeof(*normal)) ||
            normal->ImageInfoType != 1U ||
            normal->LoadedImageProtocolInstance == NULL ||
            normal->ImageHandle == NULL) {
            return 0;
        }
        if (normal->ImageHandle == ImageHandle &&
            normal->LoadedImageProtocolInstance == Loaded) {
            found = 1;
        }
    }
    return found && (header->UpdateStatus & 1U) == 0 &&
           header->TableSize == count;
}

static BOOLEAN test_sal_state_info_no_log(EFI_SYSTEM_TABLE *SystemTable)
{
    UINT8 *sal = (UINT8 *)find_config_table(SystemTable, sal_guid);
    TEST_MEMORY_MAP map;
    volatile UINT64 descriptor[2] __attribute__((aligned(16)));
    TEST_SAL_PROC procedure;
    TEST_SAL_RETURN size;
    TEST_SAL_RETURN info;
    TEST_SAL_RETURN clear;
    UINT8 record[TEST_SAL_STATE_INFO_MAX_SIZE]
        __attribute__((aligned(16)));
    BOOLEAN ok = 0;

    if (!get_memory_map(SystemTable, &map)) {
        return 0;
    }
    if (sal == NULL ||
        !memory_range_is_mapped(&map, (UINTN)sal, 144U) ||
        get_u32(sal) != SIG32('S', 'S', 'T', '_') ||
        get_u32(sal + 4U) < 144U || get_u16(sal + 10U) < 1U ||
        sal[96U] != 0U || get_u64(sal + 112U) == 0 ||
        get_u64(sal + 120U) == 0) {
        goto out;
    }
    descriptor[0] = get_u64(sal + 112U);
    descriptor[1] = get_u64(sal + 120U);
    procedure = (TEST_SAL_PROC)(UINTN)&descriptor[0];
    size = procedure(TEST_SAL_GET_STATE_INFO_SIZE, 0, 0, 0, 0, 0, 0, 0);
    info = procedure(TEST_SAL_GET_STATE_INFO, 3, 0, (UINTN)record,
                     0, 0, 0, 0);
    clear = procedure(TEST_SAL_CLEAR_STATE_INFO, 1, 0, 0, 0, 0, 0, 0);
    ok = size.Status == TEST_SAL_SUCCESS &&
         size.Value0 == sizeof(record) &&
         size.Value1 == 0 && size.Value2 == 0 &&
         info.Status == TEST_SAL_NO_INFORMATION && info.Value0 == 0 &&
         info.Value1 == 0 && info.Value2 == 0 &&
         clear.Status == TEST_SAL_SUCCESS && clear.Value0 == 0 &&
         clear.Value1 == 0 && clear.Value2 == 0;
out:
    put_memory_map(SystemTable, &map);
    return ok;
}

static BOOLEAN test_sal_smbios_tables(EFI_SYSTEM_TABLE *SystemTable,
                                      const TEST_TABLE_CONTEXT *Context)
{
    UINT8 *sal = (UINT8 *)find_config_table(SystemTable, sal_guid);
    UINT8 *smbios = (UINT8 *)find_config_table(SystemTable, smbios_guid);
    UINT8 *table;
    UINT32 sal_length;
    UINT64 stack_pointer;
    UINTN table_length;
    UINTN structure_count;
    UINTN expected_count;
    UINTN processor_count = 0;
    UINTN offset = 0;
    BOOLEAN end_table = 0;
    BOOLEAN processor_topology = 1;

    __asm__ volatile ("mov %0 = r12;;" : "=r"(stack_pointer));

    if (!Context->Valid || sal == NULL || smbios == NULL ||
        !memory_range_has_type(&Context->MemoryMap, stack_pointer, 16U,
                               EfiRuntimeServicesData,
                               EFI_MEMORY_RUNTIME) ||
        !memory_range_is_mapped(&Context->MemoryMap, (UINTN)sal, 8U) ||
        get_u32(sal) != SIG32('S', 'S', 'T', '_')) {
        return 0;
    }
    sal_length = get_u32(sal + 4U);
    if (sal_length < 144U || sal_length > 4096U ||
        !memory_range_is_mapped(&Context->MemoryMap, (UINTN)sal, sal_length) ||
        get_u16(sal + 8U) < 0x300U || get_u16(sal + 10U) < 1U ||
        ia64_checksum8(sal, sal_length) != 0 ||
        !ia64_bytes_equal(smbios, "_SM_", 4) || smbios[5] != 0x1fU ||
        ia64_checksum8(smbios, smbios[5]) != 0 ||
        !ia64_bytes_equal(smbios + 16U, "_DMI_", 5) ||
        ia64_checksum8(smbios + 16U, 15U) != 0) {
        return 0;
    }
    table_length = get_u16(smbios + 22U);
    table = (UINT8 *)(UINTN)get_u32(smbios + 24U);
    expected_count = get_u16(smbios + 28U);
    if (table_length == 0 || table == NULL || expected_count == 0 ||
        !memory_range_is_mapped(&Context->MemoryMap, (UINTN)table,
                                table_length)) {
        return 0;
    }
    for (structure_count = 0; offset < table_length; structure_count++) {
        UINTN formatted_length;

        if (table_length - offset < 4U) {
            return 0;
        }
        formatted_length = table[offset + 1U];
        if (formatted_length < 4U || formatted_length > table_length - offset) {
            return 0;
        }
        if (table[offset] == 127U) {
            end_table = 1;
        } else if (table[offset] == 4U) {
            if (formatted_length < 42U || processor_count >= 4U ||
                get_u16(table + offset + 2U) != 0x0400U + processor_count ||
                table[offset + 24U] != 0x41U ||
                table[offset + 35U] != 1U ||
                table[offset + 36U] != 1U ||
                table[offset + 37U] != 1U ||
                get_u16(table + offset + 38U) != 0x0004U) {
                processor_topology = 0;
            }
            processor_count++;
        }
        offset += formatted_length;
        while (offset + 1U < table_length &&
               (table[offset] != 0 || table[offset + 1U] != 0)) {
            offset++;
        }
        if (offset + 1U >= table_length) {
            return 0;
        }
        offset += 2U;
    }
    return offset == table_length && structure_count == expected_count &&
           end_table && processor_count == 4U && processor_topology;
}

EFI_STATUS ia64_services_main(EFI_HANDLE ImageHandle,
                              EFI_SYSTEM_TABLE *SystemTable,
                              BOOLEAN TablesOnly)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = TablesOnly ? "tables" : "services",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    TEST_TABLE_CONTEXT tables;
    BOOLEAN tables_initialized;

    if (TablesOnly) {
        (void)SystemTable->BootServices->HandleProtocol(
            ImageHandle, loaded_image_guid, (VOID **)&loaded);
        tables_initialized = init_table_context(SystemTable, &tables);
        ia64_test_check(&context, "acpi-root-tables",
                        tables_initialized &&
                            test_acpi_root_tables(&tables, 1),
                        EFI_DEVICE_ERROR, "rsdp-rsdt-xsdt-dbgp-links");
        ia64_test_check(&context, "configuration-tables",
                        tables_initialized &&
                            test_configuration_tables(SystemTable, &tables),
                        EFI_DEVICE_ERROR, "configuration-guids");
        ia64_test_check(&context, "acpi-table-bounds",
                        tables_initialized && test_acpi_table_bounds(&tables),
                        EFI_DEVICE_ERROR, "revision-length-memory-type");
        ia64_test_check(&context, "acpi-fadt-links-gas",
                        tables_initialized &&
                            test_acpi_fadt_links_gas(&tables),
                        EFI_DEVICE_ERROR, "facs-dsdt-sci-gas");
        ia64_test_check(&context, "acpi-topology",
                        tables_initialized && test_acpi_topology(&tables),
                        EFI_DEVICE_ERROR, "madt-srat-slit");
        ia64_test_check(&context, "acpi-mcfg",
                        tables_initialized && test_acpi_mcfg(&tables),
                        EFI_DEVICE_ERROR, "pci-ecam-allocation");
        ia64_test_check(&context, "acpi-console-tables",
                        tables_initialized &&
                            test_acpi_console_tables(&tables),
                        EFI_DEVICE_ERROR, "hcdp-dbgp-uart");
        ia64_test_check(&context, "acpi-aml-crs",
                        tables_initialized && test_dsdt_crs(&tables) &&
                            test_ssdt_legacy_crs(&tables),
                        EFI_DEVICE_ERROR, "pci-window-legacy-input-policy");
        ia64_test_check(&context, "acpi-pci-routing",
                        tables_initialized && test_dsdt_prt(&tables),
                        EFI_DEVICE_ERROR, "prt-intx-gsi-swizzle");
        ia64_test_check(&context, "platform-memory-descriptors",
                        tables_initialized &&
                            test_platform_memory_descriptors(&tables),
                        EFI_DEVICE_ERROR, "uart-rtc-nvram-ecam-sparse-io");
        ia64_test_check(&context, "pci-root-resources",
                        test_pci_root_resources(SystemTable), EFI_DEVICE_ERROR,
                        "bus-io-memory-windows");
        ia64_test_check(&context, "debug-image-info",
                        tables_initialized &&
                            test_debug_image_info(ImageHandle, loaded,
                                                  SystemTable, &tables),
                        EFI_DEVICE_ERROR, "normal-loaded-image-entry");
        ia64_test_check(&context, "sal-smbios-tables",
                        tables_initialized &&
                            test_sal_smbios_tables(SystemTable, &tables),
                        EFI_DEVICE_ERROR, "sal-smbios-checksum");
        release_table_context(SystemTable, &tables);
    } else {
        ia64_test_check(&context, "memory-services",
                        test_memory_services(SystemTable), EFI_DEVICE_ERROR,
                        "allocate-map-free");
        ia64_test_check(&context, "event-services",
                        test_event_services(SystemTable), EFI_DEVICE_ERROR,
                        "event-behavior");
        ia64_test_check(
            &context, "protocol-services",
            test_protocol_services(ImageHandle, SystemTable, &loaded),
            EFI_DEVICE_ERROR, "protocol-behavior");
        ia64_test_check(&context, "multiple-protocol-services",
                        test_multiple_protocol_services(SystemTable),
                        EFI_DEVICE_ERROR,
                        "transaction-duplicate-device-path");
        ia64_test_check(&context, "controller-services",
                        test_controller_services(SystemTable),
                        EFI_DEVICE_ERROR,
                        "driver-precedence-disconnect");
        ia64_test_check(&context, "image-services",
                        test_image_services(ImageHandle, SystemTable),
                        EFI_DEVICE_ERROR,
                        "load-file-fallback-parameter");
        {
            BOOLEAN start_connect_ok =
                test_start_image_connect(ImageHandle, SystemTable);

            ia64_test_check(
                &context, "start-image-connect", start_connect_ok,
                EFI_DEVICE_ERROR,
                "modified-handle-auto-connect");
        }
        ia64_test_check(&context, "memory-primitives",
                        test_memory_primitives(SystemTable), EFI_DEVICE_ERROR,
                        "crc-copy-set");
        ia64_test_check(&context, "time-services",
                        test_time_services(SystemTable), EFI_DEVICE_ERROR,
                        "time-wakeup");
        ia64_test_check(&context, "variable-services",
                        test_variable_services(SystemTable), EFI_DEVICE_ERROR,
                        "variable-services");
        ia64_test_check(&context, "block-disk-protocols",
                        test_block_disk_protocols(SystemTable, loaded),
                        EFI_DEVICE_ERROR, "block-disk-io");
        ia64_test_check(&context, "pci-root-io",
                        test_pci_root_io(SystemTable), EFI_DEVICE_ERROR,
                        "pci-config-read");
        ia64_test_check(&context, "pci-io",
                        test_pci_io_protocol(SystemTable), EFI_DEVICE_ERROR,
                        "pci-location-config-read");
        ia64_test_check(&context, "graphics-output",
                        test_gop_protocol(SystemTable), EFI_DEVICE_ERROR,
                        "gop-query-mode");
        ia64_test_check(&context, "tcg-no-tpm",
                        test_tcg_protocol(SystemTable), EFI_DEVICE_ERROR,
                        "tcg-status-hash");
        ia64_test_check(&context, "sal-state-info-no-log",
                        test_sal_state_info_no_log(SystemTable),
                        EFI_DEVICE_ERROR, "size-empty-clear");
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
