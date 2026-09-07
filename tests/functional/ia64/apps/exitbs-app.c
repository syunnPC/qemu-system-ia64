/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define EFI_MEMORY_RUNTIME 0x8000000000000000ULL
#define IA64_RUNTIME_ALIGNMENT 0x2000ULL
#define VGA_TEXT_FB_BASE 0x00000000000b8000ULL
#define VGA_TEXT_COLUMNS 80U
#define VGA_TEXT_ROWS 25U
#define VBE_DISPI_INDEX_ENABLE 4U
#define IA64_PSR_DT (1ULL << 17)
#define IA64_PSR_RT (1ULL << 27)
#define IA64_PSR_IT (1ULL << 36)
#define RUNTIME_ALIAS_BASE 0xe000000000000000ULL
#define TEST_APPLICATION_BASE 0x04000000ULL
#define TEST_MAPPING_SHIFT 24U
#define TEST_MAPPING_SIZE (1ULL << TEST_MAPPING_SHIFT)
#define TEST_MAPPING_MASK (TEST_MAPPING_SIZE - 1U)
#define TEST_MAPPING_ITIR ((UINT64)TEST_MAPPING_SHIFT << 2)
#define TEST_MAPPING_PTE_WB 0x661ULL
#define TEST_MAPPING_PTE_UC 0x671ULL
#define TEST_RUNTIME_MAPPING_MAX 60U
#define TEST_WAKEUP_ALIAS (RUNTIME_ALIAS_BASE + 0x14000000ULL)
#define TEST_SAL_MC_SET_PARAMS 0x01000005ULL
#define TEST_RAS_WAKEUP_VALUE_PA 0xfe800058ULL

typedef struct {
    UINT64 Base;
    UINT64 PteFlags;
    BOOLEAN Code;
} TEST_RUNTIME_MAPPING;

static CHAR16 boot0000_name[] = {
    'B', 'o', 'o', 't', '0', '0', '0', '0', 0,
};
static UINT8 efi_global_variable_guid[16] = {
    0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
    0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static UINT64 wakeup_word;

__asm__(
".text\n"
".align 16\n"
".global test_call_virtual_procedure\n"
".type test_call_virtual_procedure, @function\n"
".proc test_call_virtual_procedure\n"
"test_call_virtual_procedure:\n"
"    .prologue\n"
"    .save ar.pfs, r39\n"
"    alloc r39 = ar.pfs, 7, 8, 8, 0\n"
"    .save rp, r40\n"
"    mov r40 = b0\n"
"    mov r41 = gp\n"
"    mov r42 = psr\n"
"    movl r14 = (1 << 44)\n"
"    ;;\n"
"    or r42 = r42, r14\n"
"    mov r14 = r32\n"
"    ;;\n"
"    ld8 r43 = [r14], 8\n"
"    ;;\n"
"    ld8 r44 = [r14]\n"
"    mov r47 = r34\n"
"    mov r48 = r35\n"
"    mov r49 = r36\n"
"    mov r50 = r37\n"
"    mov r51 = r38\n"
"    mov r52 = r0\n"
"    mov r53 = r0\n"
"    mov r54 = r0\n"
"    rsm psr.ic\n"
"    ;;\n"
"    srlz.d\n"
"    ;;\n"
"    movl r14 = 1f\n"
"    ;;\n"
"    mov cr.ipsr = r33\n"
"    mov cr.iip = r14\n"
"    mov cr.ifs = r0\n"
"    ;;\n"
"    rfi\n"
"    ;;\n"
"1:\n"
"    srlz.i\n"
"    ;;\n"
"    mov gp = r44\n"
"    mov b6 = r43\n"
"    ;;\n"
"    br.call.sptk.many b0 = b6\n"
"    ;;\n"
"    mov r45 = r8\n"
"    mov gp = r41\n"
"    rsm psr.ic\n"
"    ;;\n"
"    srlz.d\n"
"    ;;\n"
"    movl r14 = 2f\n"
"    ;;\n"
"    mov cr.ipsr = r42\n"
"    mov cr.iip = r14\n"
"    mov cr.ifs = r0\n"
"    ;;\n"
"    rfi\n"
"    ;;\n"
"2:\n"
"    srlz.i\n"
"    ;;\n"
"    mov r8 = r45\n"
"    mov b0 = r40\n"
"    mov ar.pfs = r39\n"
"    br.ret.sptk.many b0\n"
".endp test_call_virtual_procedure\n");

static UINT64 read_psr(void)
{
    UINT64 value;

    __asm__ volatile ("mov %0=psr" : "=r"(value));
    /* The calling convention fixes bank 1; mov-from-PSR omits its bit. */
    return value | (1ULL << 44);
}

static UINT64 exchange_iva(UINT64 Value)
{
    UINT64 original;

    __asm__ volatile (
        "mov %0=cr.iva;;\n\t"
        "mov cr.iva=%1;;\n\t"
        "srlz.i;;"
        : "=&r"(original)
        : "r"(Value)
        : "memory");
    return original;
}

static UINT64 mapping_pte(UINT64 Physical, UINT64 PteFlags)
{
    return (Physical & ~TEST_MAPPING_MASK) | PteFlags;
}

static void install_instruction_mapping(UINT64 Virtual, UINT64 Physical,
                                        UINT64 PteFlags)
{
    UINT64 pte = mapping_pte(Physical, PteFlags);

    __asm__ volatile (
        "rsm psr.ic;;\n\t"
        "srlz.d;;\n\t"
        "mov cr.ifa=%0\n\t"
        "mov cr.itir=%1;;\n\t"
        "itc.i %2;;\n\t"
        "srlz.i;;\n\t"
        "ssm psr.ic;;\n\t"
        "srlz.d;;"
        :
        : "r"(Virtual), "r"(TEST_MAPPING_ITIR), "r"(pte)
        : "memory");
}

static void install_data_mapping(UINT64 Virtual, UINT64 Physical,
                                 UINT64 PteFlags)
{
    UINT64 pte = mapping_pte(Physical, PteFlags);

    __asm__ volatile (
        "rsm psr.ic;;\n\t"
        "srlz.d;;\n\t"
        "mov cr.ifa=%0\n\t"
        "mov cr.itir=%1;;\n\t"
        "itc.d %2;;\n\t"
        "srlz.d;;\n\t"
        "ssm psr.ic;;\n\t"
        "srlz.d;;"
        :
        : "r"(Virtual), "r"(TEST_MAPPING_ITIR), "r"(pte)
        : "memory");
}

static BOOLEAN prepare_runtime_virtual_map(EFI_MEMORY_DESCRIPTOR *Map,
                                           UINTN MapSize,
                                           UINTN DescriptorSize)
{
    BOOLEAN firmware_runtime_seen = 0;
    UINTN offset;

    for (offset = 0; offset < MapSize; offset += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + offset);
        UINT64 bytes;
        UINT64 end;

        if ((descriptor->Attribute & EFI_MEMORY_RUNTIME) == 0) {
            continue;
        }
        if (descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages >
                (~(UINT64)0) / EFI_PAGE_SIZE) {
            return 0;
        }
        bytes = descriptor->NumberOfPages * EFI_PAGE_SIZE;
        end = descriptor->PhysicalStart + bytes;
        if (end < descriptor->PhysicalStart) {
            return 0;
        }
        if (descriptor->PhysicalStart < TEST_MAPPING_SIZE) {
            if (end > TEST_MAPPING_SIZE ||
                (descriptor->Attribute &
                 (EFI_MEMORY_UC | EFI_MEMORY_WB)) != EFI_MEMORY_WB) {
                return 0;
            }
            descriptor->VirtualStart =
                RUNTIME_ALIAS_BASE + descriptor->PhysicalStart;
            firmware_runtime_seen = 1;
        } else {
            descriptor->VirtualStart = descriptor->PhysicalStart;
        }
    }
    return firmware_runtime_seen;
}

static BOOLEAN runtime_mapping_pte_flags(UINT64 Attributes,
                                         UINT64 *PteFlags)
{
    UINT64 cacheability = Attributes & (EFI_MEMORY_UC | EFI_MEMORY_WB);

    if (cacheability == EFI_MEMORY_WB) {
        *PteFlags = TEST_MAPPING_PTE_WB;
        return 1;
    }
    if (cacheability == EFI_MEMORY_UC) {
        *PteFlags = TEST_MAPPING_PTE_UC;
        return 1;
    }
    return 0;
}

static BOOLEAN install_runtime_test_mappings(EFI_MEMORY_DESCRIPTOR *Map,
                                             UINTN MapSize,
                                             UINTN DescriptorSize)
{
    TEST_RUNTIME_MAPPING mappings[TEST_RUNTIME_MAPPING_MAX];
    UINTN mapping_count = 0;
    UINTN offset;

    for (offset = 0; offset < MapSize; offset += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + offset);
        UINT64 pte_flags;
        UINT64 bytes;
        UINT64 end;
        UINT64 base;
        UINT64 last;

        if ((descriptor->Attribute & EFI_MEMORY_RUNTIME) == 0 ||
            descriptor->PhysicalStart < TEST_MAPPING_SIZE) {
            continue;
        }
        if (descriptor->VirtualStart != descriptor->PhysicalStart ||
            descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages >
                (~(UINT64)0) / EFI_PAGE_SIZE ||
            !runtime_mapping_pte_flags(descriptor->Attribute,
                                       &pte_flags)) {
            return 0;
        }
        bytes = descriptor->NumberOfPages * EFI_PAGE_SIZE;
        end = descriptor->PhysicalStart + bytes;
        if (end <= descriptor->PhysicalStart) {
            return 0;
        }
        base = descriptor->PhysicalStart & ~TEST_MAPPING_MASK;
        last = (end - 1U) & ~TEST_MAPPING_MASK;
        for (;;) {
            UINTN index;

            for (index = 0; index < mapping_count; index++) {
                if (mappings[index].Base == base) {
                    break;
                }
            }
            if (index < mapping_count) {
                if (mappings[index].PteFlags != pte_flags) {
                    return 0;
                }
                mappings[index].Code |=
                    descriptor->Type == EfiRuntimeServicesCode;
            } else {
                if (mapping_count >= TEST_RUNTIME_MAPPING_MAX) {
                    return 0;
                }
                mappings[mapping_count].Base = base;
                mappings[mapping_count].PteFlags = pte_flags;
                mappings[mapping_count].Code =
                    descriptor->Type == EfiRuntimeServicesCode;
                mapping_count++;
            }
            if (base == last) {
                break;
            }
            base += TEST_MAPPING_SIZE;
        }
    }

    install_instruction_mapping(TEST_APPLICATION_BASE,
                                TEST_APPLICATION_BASE,
                                TEST_MAPPING_PTE_WB);
    install_data_mapping(TEST_APPLICATION_BASE, TEST_APPLICATION_BASE,
                         TEST_MAPPING_PTE_WB);
    install_instruction_mapping(RUNTIME_ALIAS_BASE, 0,
                                TEST_MAPPING_PTE_WB);
    install_data_mapping(RUNTIME_ALIAS_BASE, 0, TEST_MAPPING_PTE_WB);
    install_data_mapping(TEST_WAKEUP_ALIAS, TEST_APPLICATION_BASE,
                         TEST_MAPPING_PTE_WB);
    for (offset = 0; offset < mapping_count; offset++) {
        install_data_mapping(mappings[offset].Base,
                             mappings[offset].Base,
                             mappings[offset].PteFlags);
        if (mappings[offset].Code) {
            install_instruction_mapping(mappings[offset].Base,
                                        mappings[offset].Base,
                                        mappings[offset].PteFlags);
        }
    }
    return 1;
}

static UINT16 vbe_read(UINT16 Index)
{
    volatile UINT16 *index =
        (volatile UINT16 *)(UINTN)IA64_TEST_IO_PORT_PA(0x1ceU);
    volatile UINT16 *data =
        (volatile UINT16 *)(UINTN)IA64_TEST_IO_PORT_PA(0x1d0U);

    *index = Index;
    return *data;
}

static BOOLEAN legacy_text_handoff_is_ready(void)
{
    volatile UINT16 *text =
        (volatile UINT16 *)(UINTN)VGA_TEXT_FB_BASE;

    return vbe_read(VBE_DISPI_INDEX_ENABLE) == 0 &&
           text[0] == 0x0720U &&
           text[VGA_TEXT_COLUMNS * VGA_TEXT_ROWS - 1U] == 0x0720U;
}

static UINT8 sal_guid[16] = IA64_GUID_SAL;
static UINT8 smbios_guid[16] = IA64_GUID_SMBIOS;
static UINT8 gop_guid[16] = IA64_GUID_GOP;
static UINT8 debug_image_guid[16] = {
    0x77, 0x2e, 0x15, 0x49, 0xda, 0x1a, 0x64, 0x47,
    0xb7, 0xa2, 0x7a, 0xfe, 0xfe, 0xd9, 0x5e, 0x8b,
};

static UINT32 crc32_bytes(const UINT8 *Data, UINTN Size)
{
    UINT32 crc = 0xffffffffU;
    UINTN index;

    for (index = 0; index < Size; index++) {
        UINTN bit;

        crc ^= Data[index];
        for (bit = 0; bit < 8U; bit++) {
            crc = (crc >> 1) ^
                  ((crc & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return ~crc;
}

static BOOLEAN system_table_crc_is_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_SYSTEM_TABLE copy;
    UINT32 expected;

    if (SystemTable == NULL ||
        SystemTable->Hdr.HeaderSize < sizeof(EFI_TABLE_HEADER) ||
        SystemTable->Hdr.HeaderSize > sizeof(copy)) {
        return 0;
    }
    ia64_copy(&copy, SystemTable, sizeof(copy));
    expected = copy.Hdr.CRC32;
    copy.Hdr.CRC32 = 0;
    return crc32_bytes((const UINT8 *)&copy, copy.Hdr.HeaderSize) == expected;
}

static EFI_STATUS get_final_memory_map(EFI_BOOT_SERVICES *BootServices,
                                       EFI_MEMORY_DESCRIPTOR **Map,
                                       UINTN *MapSize, UINTN *MapKey,
                                       UINTN *DescriptorSize,
                                       UINT32 *DescriptorVersion)
{
    EFI_STATUS status;
    UINTN attempt;

    *Map = NULL;
    *MapSize = 0;
    for (attempt = 0; attempt < 4U; attempt++) {
        UINTN required = *MapSize;
        EFI_MEMORY_DESCRIPTOR *new_map = NULL;

        status = BootServices->GetMemoryMap(
            &required, *Map, MapKey, DescriptorSize, DescriptorVersion);
        if (status == EFI_SUCCESS) {
            *MapSize = required;
            return EFI_SUCCESS;
        }
        if (status != EFI_BUFFER_TOO_SMALL) {
            return status;
        }
        if (*Map != NULL) {
            (void)BootServices->FreePool(*Map);
            *Map = NULL;
        }
        required += 8U * (*DescriptorSize != 0 ? *DescriptorSize :
                         sizeof(EFI_MEMORY_DESCRIPTOR));
        if (BootServices->AllocatePool(EfiLoaderData, required,
                                       (VOID **)&new_map) != EFI_SUCCESS) {
            return EFI_OUT_OF_RESOURCES;
        }
        *Map = new_map;
        *MapSize = required;
    }
    return EFI_OUT_OF_RESOURCES;
}

static BOOLEAN runtime_map_is_aligned(EFI_MEMORY_DESCRIPTOR *Map,
                                      UINTN MapSize, UINTN DescriptorSize)
{
    UINTN offset;
    UINTN runtime_count = 0;

    if (DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        MapSize % DescriptorSize != 0) {
        return 0;
    }
    for (offset = 0; offset < MapSize; offset += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + offset);
        UINT64 size = descriptor->NumberOfPages * EFI_PAGE_SIZE;

        if ((descriptor->Attribute & EFI_MEMORY_RUNTIME) == 0) {
            continue;
        }
        runtime_count++;
        if ((descriptor->PhysicalStart & (IA64_RUNTIME_ALIGNMENT - 1U)) != 0 ||
            (size & (IA64_RUNTIME_ALIGNMENT - 1U)) != 0) {
            return 0;
        }
    }
    return runtime_count != 0;
}

static BOOLEAN memory_map_contains(EFI_MEMORY_DESCRIPTOR *Map,
                                   UINTN MapSize, UINTN DescriptorSize,
                                   const VOID *Address, UINTN Size,
                                   UINT64 RequiredAttributes)
{
    UINT64 start = (UINT64)(UINTN)Address;
    UINT64 end = start + Size;
    UINTN offset;

    if (Map == NULL || Address == NULL || Size == 0 || end < start ||
        DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        MapSize % DescriptorSize != 0) {
        return 0;
    }
    for (offset = 0; offset < MapSize; offset += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + offset);
        UINT64 pages = descriptor->NumberOfPages;
        UINT64 descriptor_size;
        UINT64 descriptor_end;

        if (pages > (~(UINT64)0) / EFI_PAGE_SIZE) {
            continue;
        }
        descriptor_size = pages * EFI_PAGE_SIZE;
        descriptor_end = descriptor->PhysicalStart + descriptor_size;
        if ((descriptor->Attribute & RequiredAttributes) ==
                RequiredAttributes &&
            descriptor_end >= descriptor->PhysicalStart &&
            start >= descriptor->PhysicalStart && end <= descriptor_end) {
            return 1;
        }
    }
    return 0;
}

static BOOLEAN runtime_procedure_is_mapped(EFI_MEMORY_DESCRIPTOR *Map,
                                           UINTN MapSize,
                                           UINTN DescriptorSize,
                                           const VOID *Procedure)
{
    const UINT64 *descriptor;

    /* IA-64 EFI function pointers address a two-word procedure descriptor. */
    if (!memory_map_contains(Map, MapSize, DescriptorSize, Procedure,
                             2U * sizeof(UINT64), EFI_MEMORY_RUNTIME)) {
        return 0;
    }
    descriptor = (const UINT64 *)Procedure;
    return descriptor[0] != 0 && descriptor[1] != 0 &&
           memory_map_contains(
               Map, MapSize, DescriptorSize,
               (const VOID *)(UINTN)descriptor[0], 1,
               EFI_MEMORY_RUNTIME) &&
           memory_map_contains(
               Map, MapSize, DescriptorSize,
               (const VOID *)(UINTN)descriptor[1], 1,
               EFI_MEMORY_RUNTIME);
}

static BOOLEAN runtime_service_procedures_are_mapped(
    EFI_MEMORY_DESCRIPTOR *Map, UINTN MapSize, UINTN DescriptorSize,
    EFI_RUNTIME_SERVICES *RuntimeServices)
{
#define RUNTIME_PROCEDURE(Field) \
    runtime_procedure_is_mapped( \
        Map, MapSize, DescriptorSize, \
        (const VOID *)(UINTN)RuntimeServices->Field)

    return RuntimeServices != NULL &&
           RUNTIME_PROCEDURE(GetTime) &&
           RUNTIME_PROCEDURE(SetTime) &&
           RUNTIME_PROCEDURE(GetWakeupTime) &&
           RUNTIME_PROCEDURE(SetWakeupTime) &&
           RUNTIME_PROCEDURE(SetVirtualAddressMap) &&
           RUNTIME_PROCEDURE(ConvertPointer) &&
           RUNTIME_PROCEDURE(GetVariable) &&
           RUNTIME_PROCEDURE(GetNextVariableName) &&
           RUNTIME_PROCEDURE(SetVariable) &&
           RUNTIME_PROCEDURE(GetNextHighMonotonicCount) &&
           RUNTIME_PROCEDURE(ResetSystem) &&
           RUNTIME_PROCEDURE(QueryVariableInfo);

#undef RUNTIME_PROCEDURE
}

static BOOLEAN configuration_table_ranges_are_mapped(
    EFI_MEMORY_DESCRIPTOR *Map, UINTN MapSize, UINTN DescriptorSize,
    EFI_CONFIGURATION_TABLE *Tables, UINTN Count)
{
    UINTN index;
    BOOLEAN sal_seen = 0;
    BOOLEAN smbios_seen = 0;
    BOOLEAN debug_seen = 0;

    if (Tables == NULL || Count == 0 ||
        Count > (~(UINTN)0) / sizeof(*Tables) ||
        !memory_map_contains(Map, MapSize, DescriptorSize, Tables,
                             Count * sizeof(*Tables),
                             EFI_MEMORY_RUNTIME)) {
        return 0;
    }
    for (index = 0; index < Count; index++) {
        const VOID *vendor_table =
            (const VOID *)(UINTN)Tables[index].VendorTable;
        UINTN minimum_size = 1;
        BOOLEAN must_be_runtime = 0;

        if (ia64_bytes_equal(Tables[index].VendorGuid, sal_guid, 16)) {
            sal_seen = 1;
            must_be_runtime = 1;
            minimum_size = 144;
        } else if (ia64_bytes_equal(Tables[index].VendorGuid,
                                    smbios_guid, 16)) {
            smbios_seen = 1;
            must_be_runtime = 1;
            minimum_size = 31;
        } else if (ia64_bytes_equal(Tables[index].VendorGuid,
                                    debug_image_guid, 16)) {
            debug_seen = 1;
            must_be_runtime = 1;
            minimum_size = 16;
        }
        if (!memory_map_contains(
                Map, MapSize, DescriptorSize, vendor_table, minimum_size,
                must_be_runtime ? EFI_MEMORY_RUNTIME : 0)) {
            return 0;
        }
        if (ia64_bytes_equal(Tables[index].VendorGuid, sal_guid, 16)) {
            const UINT8 *sal = vendor_table;
            UINTN entry_offset;

            if (sal[96] != 0) {
                return 0;
            }
            /* PAL/SAL entry points and SAL GP require runtime mappings. */
            for (entry_offset = 104; entry_offset <= 120; entry_offset += 8) {
                UINT64 address = *(const UINT64 *)(sal + entry_offset);

                if (!memory_map_contains(Map, MapSize, DescriptorSize,
                                          (const VOID *)(UINTN)address, 1,
                                          EFI_MEMORY_RUNTIME)) {
                    return 0;
                }
            }
        }
    }
    return sal_seen && smbios_seen && debug_seen;
}

static BOOLEAN test_sal_wakeup_address(EFI_CONFIGURATION_TABLE *Tables,
                                       UINTN Count)
{
    UINT64 descriptor[2] __attribute__((aligned(16)));
    /* Read the wakeup registration latched by the RAS device. */
    volatile UINT64 *registered = (VOID *)(UINTN)TEST_RAS_WAKEUP_VALUE_PA;
    UINT64 physical = (UINTN)&wakeup_word;
    UINT64 alias = TEST_WAKEUP_ALIAS + physical - TEST_APPLICATION_BASE;
    UINT64 target_psr = read_psr() | IA64_PSR_DT | IA64_PSR_RT | IA64_PSR_IT;
    UINTN i;

    for (i = 0; i < Count; i++) {
        const UINT8 *sal;
        EFI_STATUS status;

        if (!ia64_bytes_equal(Tables[i].VendorGuid, sal_guid, 16)) {
            continue;
        }
        sal = (const VOID *)(UINTN)Tables[i].VendorTable;
        descriptor[0] = *(const UINT64 *)(sal + 112U);
        descriptor[1] = *(const UINT64 *)(sal + 120U);
        status = test_call_virtual_procedure(
            descriptor, read_psr(), TEST_SAL_MC_SET_PARAMS, 2, 2, physical, 0);
        if (status != 0 || *registered != physical) {
            return 0;
        }
        descriptor[0] += RUNTIME_ALIAS_BASE;
        descriptor[1] += RUNTIME_ALIAS_BASE;
        status = test_call_virtual_procedure(
            descriptor, target_psr, TEST_SAL_MC_SET_PARAMS, 2, 2, alias, 0);
        if (status != 0 || *registered != physical) {
            return 0;
        }
        status = test_call_virtual_procedure(
            descriptor, target_psr, TEST_SAL_MC_SET_PARAMS, 2, 2, alias + 1, 0);
        if (status != (UINT64)-2 || *registered != physical) {
            return 0;
        }
        status = test_call_virtual_procedure(
            descriptor, target_psr, TEST_SAL_MC_SET_PARAMS, 2, 1, 0xff, 0);
        return status == 0 && *registered == 0xff;
    }
    return 0;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "exitbs",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_BOOT_SERVICES *boot_services = SystemTable->BootServices;
    EFI_RUNTIME_SERVICES *runtime_services = SystemTable->RuntimeServices;
    EFI_CONFIGURATION_TABLE *configuration_table =
        SystemTable->ConfigurationTable;
    UINTN configuration_count = SystemTable->NumberOfTableEntries;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_TIME time;
    EFI_STATUS status;
    EFI_STATUS virtual_variable_status = EFI_NOT_READY;
    BOOLEAN sal_wakeup_ok = 0;
    const UINT64 *physical_get_variable_descriptor;
    UINT64 virtual_get_variable_descriptor[2] __attribute__((aligned(16)));
    UINTN boot0000_size = 0;
    UINT32 boot0000_attributes = 0;
    BOOLEAN virtual_map_ready;
    VOID *convert_address;
    VOID *original_address;
    BOOLEAN aligned;
    BOOLEAN runtime_pointers_mapped;
    BOOLEAN runtime_functions_mapped;
    BOOLEAN configuration_tables_mapped;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    BOOLEAN graphics_mode_selected;

    /*
     * Match a loader such as GRUB gfxterm: explicitly select a GOP graphics
     * mode, then rely on the firmware's IA-64 ExitBootServices handoff to
     * restore the legacy text mode used by vgacon.
     */
    graphics_mode_selected =
        boot_services->LocateProtocol(gop_guid, NULL, (VOID **)&gop) ==
            EFI_SUCCESS &&
        gop != NULL && gop->SetMode != NULL &&
        gop->SetMode(gop, 1U) == EFI_SUCCESS &&
        vbe_read(VBE_DISPI_INDEX_ENABLE) != 0;

    status = get_final_memory_map(boot_services, &map, &map_size, &map_key,
                                  &descriptor_size, &descriptor_version);
    aligned = status == EFI_SUCCESS && descriptor_version == 1U &&
              runtime_map_is_aligned(map, map_size, descriptor_size);
    if (!aligned) {
        ia64_test_fail(&context, "memory-map", status,
                       "runtime-map-alignment");
        ia64_test_done(&context);
        return EFI_DEVICE_ERROR;
    }

    /*
     * Nothing which can call a Boot Service may occur between the final
     * GetMemoryMap and ExitBootServices.  In particular, defer all
     * structured console output until the application has switched to its
     * direct UART path; OutputString is allowed to invalidate map_key.
     */
    runtime_pointers_mapped =
        runtime_services != NULL &&
        memory_map_contains(
            map, map_size, descriptor_size, runtime_services,
            sizeof(EFI_TABLE_HEADER), EFI_MEMORY_RUNTIME) &&
        runtime_services->Hdr.HeaderSize >= sizeof(EFI_TABLE_HEADER) &&
        runtime_services->Hdr.HeaderSize <= EFI_PAGE_SIZE &&
        memory_map_contains(
            map, map_size, descriptor_size, runtime_services,
            runtime_services->Hdr.HeaderSize, EFI_MEMORY_RUNTIME) &&
        memory_map_contains(
            map, map_size, descriptor_size, SystemTable,
            sizeof(*SystemTable), EFI_MEMORY_RUNTIME);
    runtime_functions_mapped = runtime_pointers_mapped &&
        runtime_service_procedures_are_mapped(
            map, map_size, descriptor_size, runtime_services);
    configuration_tables_mapped = configuration_table_ranges_are_mapped(
        map, map_size, descriptor_size, configuration_table,
        configuration_count);

    status = boot_services->ExitBootServices(ImageHandle, map_key);
    if (status != EFI_SUCCESS) {
        ia64_test_fail(&context, "exit-boot-services", status,
                       "exit-boot-services");
        ia64_test_done(&context);
        return status;
    }

    context.DirectUart = 1;
    ia64_test_pass(&context, "memory-map");
    ia64_test_check(
        &context, "runtime-pointer-ranges", runtime_pointers_mapped,
        EFI_DEVICE_ERROR, "runtime-range-attributes");
    ia64_test_check(
        &context, "runtime-function-ranges", runtime_functions_mapped,
        EFI_DEVICE_ERROR, "runtime-procedure-descriptors");
    ia64_test_check(
        &context, "configuration-table-ranges",
        configuration_tables_mapped,
        EFI_DEVICE_ERROR, "configuration-table-ranges");
    ia64_test_pass(&context, "exit-boot-services");
    ia64_test_check(
        &context, "system-table-handoff",
        SystemTable->BootServices == NULL && SystemTable->ConsoleInHandle ==
            NULL && SystemTable->ConIn == NULL &&
            SystemTable->ConsoleOutHandle == NULL && SystemTable->ConOut ==
            NULL && SystemTable->StandardErrorHandle == NULL &&
            SystemTable->StdErr == NULL &&
            SystemTable->RuntimeServices == runtime_services,
        EFI_DEVICE_ERROR, "boot-fields-not-cleared");
    ia64_test_check(
        &context, "system-table-crc",
        system_table_crc_is_valid(SystemTable),
        EFI_DEVICE_ERROR, "system-table-crc-after-exit");
    ia64_test_check(
        &context, "configuration-tables-preserved",
        configuration_count != 0 &&
            SystemTable->NumberOfTableEntries == configuration_count &&
            SystemTable->ConfigurationTable == configuration_table,
        EFI_DEVICE_ERROR, "configuration-table-changed");
    ia64_test_check(
        &context, "legacy-text-handoff",
        graphics_mode_selected && legacy_text_handoff_is_ready(),
        EFI_DEVICE_ERROR, "vga-not-in-legacy-text-mode");
    convert_address = runtime_services;
    original_address = convert_address;
    status = runtime_services->ConvertPointer(2U, &convert_address);
    ia64_test_check(
        &context, "convert-pointer-reserved-bits",
        status == EFI_INVALID_PARAMETER &&
            convert_address == original_address,
        status, "reserved-debug-disposition-accepted");
    status = runtime_services->GetTime(&time, NULL);
    ia64_test_check(&context, "runtime-get-time",
                    status == EFI_SUCCESS && time.Year >= 1998U,
                    status, "get-time-after-exit");

    physical_get_variable_descriptor =
        (const UINT64 *)(UINTN)runtime_services->GetVariable;
    virtual_map_ready = prepare_runtime_virtual_map(
        map, map_size, descriptor_size);
    if (virtual_map_ready) {
        virtual_map_ready = install_runtime_test_mappings(
            map, map_size, descriptor_size);
    }
    status = virtual_map_ready ?
        runtime_services->SetVirtualAddressMap(
            map_size, descriptor_size, descriptor_version, map) :
        EFI_INVALID_PARAMETER;
    ia64_test_check(&context, "set-virtual-address-map",
                    status == EFI_SUCCESS,
                    status, "nonidentity-runtime-map");
    if (status == EFI_SUCCESS) {
        UINT64 saved_iva;
        UINT64 target_psr =
            (read_psr() | IA64_PSR_DT | IA64_PSR_IT) & ~IA64_PSR_RT;

        virtual_get_variable_descriptor[0] =
            physical_get_variable_descriptor[0];
        virtual_get_variable_descriptor[1] =
            physical_get_variable_descriptor[1];
        saved_iva = exchange_iva(TEST_APPLICATION_BASE);
        virtual_variable_status = test_call_virtual_procedure(
            virtual_get_variable_descriptor, target_psr,
            (UINTN)boot0000_name, (UINTN)efi_global_variable_guid,
            (UINTN)&boot0000_attributes, (UINTN)&boot0000_size, 0);
        (void)exchange_iva(saved_iva);
        if (configuration_tables_mapped) {
            sal_wakeup_ok = test_sal_wakeup_address(
                configuration_table, configuration_count);
        }
    }
    ia64_test_check(
        &context, "runtime-virtual-boot-variable",
        virtual_variable_status == EFI_BUFFER_TOO_SMALL &&
            boot0000_size > sizeof(UINT32) + sizeof(UINT16),
        virtual_variable_status, "boot0000-after-virtual-map");
    ia64_test_check(&context, "sal-wakeup-address", sal_wakeup_ok,
                    EFI_DEVICE_ERROR, "physical-virtual-wakeup-registration");
    ia64_test_done(&context);

    for (;;) {
        __asm__ volatile ("hint @pause" ::: "memory");
    }
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
