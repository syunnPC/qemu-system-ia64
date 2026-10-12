/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Check DMAR, the EFI memory map, and the secondary bridge.
 * Requires more than 3.75 GiB RAM and edu at device 0 on ioh7500-secondary.
 */

#include "ia64-test.h"

#define TEST_DMAR_SIGNATURE   0x52414d44U
#define TEST_UNIT_SIZE        0x1000ULL
#define TEST_APERTURE_SIZE    (2U * TEST_UNIT_SIZE)
#define TEST_EDU_ID           0x11e81234U
#define TEST_PAGE_SHIFT       12U

static UINT8 acpi20_guid[16] = IA64_GUID_ACPI20;
static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;

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

static const UINT8 *find_dmar(EFI_SYSTEM_TABLE *SystemTable)
{
    const UINT8 *rsdp = NULL;
    const UINT8 *xsdt;
    UINT32 length;
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (ia64_bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                             acpi20_guid, 16)) {
            rsdp = (const UINT8 *)
                SystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    if (rsdp == NULL || !ia64_bytes_equal(rsdp, "RSD PTR ", 8)) {
        return NULL;
    }
    xsdt = (const UINT8 *)(UINTN)get_u64(rsdp + 24);
    length = get_u32(xsdt + 4);
    for (i = 36; i + 8 <= length; i += 8) {
        const UINT8 *table = (const UINT8 *)(UINTN)get_u64(xsdt + i);

        if (get_u32(table) == TEST_DMAR_SIGNATURE) {
            return table;
        }
    }
    return NULL;
}

/* Both DRHDs: unit 1 (bridge scope) first, then unit 0 at VTBAR. */
static BOOLEAN dmar_units(const UINT8 *Dmar, UINT64 *Vtbar)
{
    if (Dmar == NULL || get_u32(Dmar + 4) != 88U || Dmar[8] != 1U ||
        ia64_checksum8(Dmar, 88U) != 0) {
        return 0;
    }
    *Vtbar = get_u64(Dmar + 80);
    return get_u64(Dmar + 56) == *Vtbar + TEST_UNIT_SIZE;
}

typedef struct {
    EFI_MEMORY_DESCRIPTOR *Buffer;
    UINTN MapSize;
    UINTN DescriptorSize;
} TEST_MEMORY_MAP;

static BOOLEAN get_memory_map(EFI_BOOT_SERVICES *Bs, TEST_MEMORY_MAP *Map)
{
    UINTN key = 0;
    UINT32 version = 0;

    Map->Buffer = NULL;
    Map->MapSize = 0;
    Map->DescriptorSize = 0;
    if (Bs->GetMemoryMap(&Map->MapSize, NULL, NULL, &Map->DescriptorSize,
                         &version) != EFI_BUFFER_TOO_SMALL ||
        Map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return 0;
    }
    Map->MapSize += 8U * Map->DescriptorSize;
    if (Bs->AllocatePool(EfiLoaderData, Map->MapSize,
                         (VOID **)&Map->Buffer) != EFI_SUCCESS) {
        Map->Buffer = NULL;
        return 0;
    }
    return Bs->GetMemoryMap(&Map->MapSize, Map->Buffer, &key,
                            &Map->DescriptorSize, &version) == EFI_SUCCESS;
}

static const EFI_MEMORY_DESCRIPTOR *descriptor(const TEST_MEMORY_MAP *Map,
                                               UINTN Index)
{
    return (const EFI_MEMORY_DESCRIPTOR *)
        ((const UINT8 *)Map->Buffer + Index * Map->DescriptorSize);
}

static UINTN descriptor_count(const TEST_MEMORY_MAP *Map)
{
    return Map->MapSize / Map->DescriptorSize;
}

static UINT64 descriptor_end(const EFI_MEMORY_DESCRIPTOR *Descriptor)
{
    return Descriptor->PhysicalStart +
           (Descriptor->NumberOfPages << TEST_PAGE_SHIFT);
}

static BOOLEAN is_ram_type(UINT32 Type)
{
    return Type <= EfiACPIMemoryNVS && Type != 0U;
}

/* Exactly one UC MMIO descriptor covers the aperture; nothing else may. */
static BOOLEAN aperture_described(const TEST_MEMORY_MAP *Map, UINT64 Vtbar)
{
    UINTN covering = 0;
    UINTN i;

    for (i = 0; i < descriptor_count(Map); i++) {
        const EFI_MEMORY_DESCRIPTOR *d = descriptor(Map, i);
        UINT64 end = descriptor_end(d);

        if (end <= Vtbar || d->PhysicalStart >= Vtbar + TEST_APERTURE_SIZE) {
            continue;
        }
        if (d->Type != EfiMemoryMappedIO ||
            (d->Attribute & EFI_MEMORY_UC) == 0 ||
            d->PhysicalStart > Vtbar ||
            end < Vtbar + TEST_APERTURE_SIZE) {
            return 0;
        }
        covering++;
    }
    return covering == 1U;
}

static BOOLEAN page_is_ram(const TEST_MEMORY_MAP *Map, UINT64 Address)
{
    UINTN i;

    for (i = 0; i < descriptor_count(Map); i++) {
        const EFI_MEMORY_DESCRIPTOR *d = descriptor(Map, i);

        if (Address >= d->PhysicalStart && Address < descriptor_end(d)) {
            return is_ram_type(d->Type);
        }
    }
    return 0;
}

static BOOLEAN secondary_bus_visible(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    UINT32 buses = 0;
    UINT32 id = 0;

    if (SystemTable->BootServices->LocateProtocol(pci_root_guid, NULL,
                                                  (VOID **)&root) !=
            EFI_SUCCESS || root == NULL ||
        root->Pci.Read(root, EfiPciWidthUint32, (7ULL << 16) | 0x18U, 1,
                       &buses) != EFI_SUCCESS ||
        root->Pci.Read(root, EfiPciWidthUint32, 1ULL << 24, 1, &id) !=
            EFI_SUCCESS) {
        return 0;
    }
    return (buses & 0xffffffU) == 0x010100U && id == TEST_EDU_ID;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "vtd",
    };
    TEST_MEMORY_MAP map;
    UINT64 vtbar = 0;
    BOOLEAN have_map;
    BOOLEAN have_dmar;

    (void)ImageHandle;
    have_dmar = dmar_units(find_dmar(SystemTable), &vtbar);
    ia64_test_check(&context, "dmar-units", have_dmar, EFI_NOT_FOUND,
                    "drhd-bases");
    have_map = get_memory_map(SystemTable->BootServices, &map);
    ia64_test_check(&context, "aperture-mmio",
                    have_dmar && have_map && aperture_described(&map, vtbar),
                    EFI_DEVICE_ERROR, "single-uc-mmio-descriptor");
    ia64_test_check(&context, "ram-around-aperture",
                    have_dmar && have_map &&
                        page_is_ram(&map, vtbar - EFI_PAGE_SIZE) &&
                        page_is_ram(&map, vtbar + TEST_APERTURE_SIZE),
                    EFI_DEVICE_ERROR, "ram-below-and-above");
    ia64_test_check(&context, "secondary-bus",
                    secondary_bus_visible(SystemTable), EFI_DEVICE_ERROR,
                    "bridge-numbers-and-endpoint");
    if (map.Buffer != NULL) {
        (void)SystemTable->BootServices->FreePool(map.Buffer);
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
