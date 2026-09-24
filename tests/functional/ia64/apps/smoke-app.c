/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_io_guid[16] = IA64_GUID_PCI_IO;

static VOID print_device_path(IA64_TEST_CONTEXT *Context, const UINT8 *Path)
{
    static const char digits[] = "0123456789abcdef";
    char text[513];
    UINTN offset = 0;
    UINTN i;

    while (offset + 4U <= sizeof(text) / 2U) {
        UINTN length = Path[offset + 2U] |
            ((UINTN)Path[offset + 3U] << 8);
        BOOLEAN end = Path[offset] == 0x7f && Path[offset + 1U] == 0xff;

        if (length < 4U || length > sizeof(text) / 2U - offset) {
            return;
        }
        for (i = offset; i < offset + length; i++) {
            text[2U * i] = digits[Path[i] >> 4];
            text[2U * i + 1U] = digits[Path[i] & 0xf];
        }
        offset += length;
        if (end) {
            text[2U * offset] = 0;
            ia64_test_write(Context, "EFI boot device path: ");
            ia64_test_write(Context, text);
            ia64_test_write(Context, "\n");
            return;
        }
    }
}

static BOOLEAN storage_node_valid(EFI_SYSTEM_TABLE *SystemTable,
                                  EFI_HANDLE Controller, const UINT8 *Node)
{
    static const UINT8 sas_guid[16] = {
        0xb4, 0xdd, 0x87, 0xd4, 0x8b, 0x00, 0xd9, 0x11,
        0xaf, 0xdc, 0x00, 0x10, 0x83, 0xff, 0xca, 0x4d,
    };
    EFI_PCI_IO_PROTOCOL *pci = NULL;
    UINT32 id = 0;
    UINT64 address;
    UINTN i;

    if (Node == NULL || Node[0] != 3 ||
        SystemTable->BootServices->HandleProtocol(
            Controller, pci_io_guid, (VOID **)&pci) != EFI_SUCCESS ||
        pci->Pci.Read(pci, EfiPciWidthUint32, 0, 1, &id) != EFI_SUCCESS) {
        return 0;
    }
    if (id != 0x00541000U) {
        return Node[1] == 2;
    }
    if (Node[1] != 10 || Node[2] != 44 || Node[3] != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(sas_guid); i++) {
        if (Node[4 + i] != sas_guid[i]) {
            return 0;
        }
    }
    for (i = 20; i < 24; i++) {
        if (Node[i] != 0) {
            return 0;
        }
    }
    ia64_copy(&address, Node + 24, sizeof(address));
    return address != 0;
}

static BOOLEAN system_table_crc_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    UINT8 copy[256];
    UINT32 crc = 0;
    UINT32 expected;
    UINTN size;

    if (SystemTable == NULL || SystemTable->BootServices == NULL ||
        SystemTable->BootServices->CalculateCrc32 == NULL ||
        SystemTable->Hdr.HeaderSize > sizeof(copy) ||
        SystemTable->Hdr.HeaderSize < sizeof(EFI_TABLE_HEADER)) {
        return 0;
    }
    size = SystemTable->Hdr.HeaderSize;
    expected = SystemTable->Hdr.CRC32;
    ia64_copy(copy, SystemTable, size);
    ((EFI_TABLE_HEADER *)copy)->CRC32 = 0;
    return SystemTable->BootServices->CalculateCrc32(copy, size, &crc) ==
               EFI_SUCCESS &&
           crc == expected;
}

static BOOLEAN low_memory_allocation_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    static const EFI_PHYSICAL_ADDRESS reserved[] = { 0, 0x10000 };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_PHYSICAL_ADDRESS address;
    /* Force guest RAM accesses for the allocation read/write check. */
    volatile UINT8 *bytes;
    EFI_STATUS status;
    BOOLEAN valid;
    UINTN i;

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        address = reserved[i];
        status = bs->AllocatePages(AllocateAddress, EfiLoaderData, 1,
                                    &address);
        if (status == EFI_SUCCESS) {
            (void)bs->FreePages(address, 1);
            return 0;
        }
        if (status != EFI_NOT_FOUND) {
            return 0;
        }
    }

    address = EFI_PAGE_SIZE;
    status = bs->AllocatePages(AllocateAddress, EfiLoaderData, 1, &address);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    if (address != EFI_PAGE_SIZE) {
        (void)bs->FreePages(address, 1);
        return 0;
    }
    bytes = (UINT8 *)(UINTN)address;
    bytes[0] = 0xa5;
    bytes[EFI_PAGE_SIZE - 1U] = 0x5a;
    valid = bytes[0] == 0xa5 && bytes[EFI_PAGE_SIZE - 1U] == 0x5a;
    if (bs->FreePages(address, 1) != EFI_SUCCESS) {
        return 0;
    }

    address = 2U * EFI_PAGE_SIZE - 1U;
    status = bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                &address);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    valid = valid && address == EFI_PAGE_SIZE;
    return bs->FreePages(address, 1) == EFI_SUCCESS && valid;
}

typedef struct {
    UINT32 Serial;
    UINT32 Graphics;
    UINT32 Primary;
} CONSOLE_SELECTION;

static UINT16 console_get_u16(const UINT8 *Data)
{
    return Data[0] | ((UINT16)Data[1] << 8);
}

static BOOLEAN console_selection_add(CONSOLE_SELECTION *Selection,
                                      UINT16 Index, BOOLEAN Serial,
                                      BOOLEAN Primary)
{
    UINT32 bit;

    if (Index == 0xffffU) {
        return !Primary;
    }
    if (Index >= 32U) {
        return 0;
    }
    bit = 1U << Index;
    if ((Selection->Serial | Selection->Graphics) & bit) {
        return 0;
    }
    if (Serial) {
        Selection->Serial |= bit;
    } else {
        Selection->Graphics |= bit;
    }
    if (Primary) {
        Selection->Primary |= bit;
    }
    return 1;
}

static BOOLEAN hcdp_console_selection(EFI_SYSTEM_TABLE *SystemTable,
                                      CONSOLE_SELECTION *Selection)
{
    static const UINT8 hcdp_guid[16] = IA64_GUID_HCDP;
    const UINT8 *hcdp = NULL;
    UINT32 length, uart_count, selected;
    UINTN i, offset;
    UINT8 checksum = 0;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &SystemTable->ConfigurationTable[i];

        if (ia64_bytes_equal(table->VendorGuid, hcdp_guid, sizeof(hcdp_guid))) {
            hcdp = (const UINT8 *)table->VendorTable;
            break;
        }
    }
    if (hcdp == NULL || !ia64_bytes_equal(hcdp, "HCDP", 4U) || hcdp[8] != 3) {
        return 0;
    }
    ia64_copy(&length, hcdp + 4U, sizeof(length));
    if (length < 40U || length > 8192U) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        checksum += hcdp[i];
    }
    ia64_copy(&uart_count, hcdp + 36U, sizeof(uart_count));
    if (checksum != 0 || uart_count > (length - 40U) / 48U) {
        return 0;
    }
    /* Revision 3 has fixed UART entries followed by variable-length devices. */
    for (i = 0; i < uart_count; i++) {
        const UINT8 *uart = hcdp + 40U + i * 48U;

        if (uart[0] == 0 &&
            !console_selection_add(Selection, console_get_u16(uart + 42U),
                                   1, (uart[41U] & 4U) != 0)) {
            return 0;
        }
    }
    offset = 40U + uart_count * 48U;
    while (offset < length) {
        const UINT8 *device = hcdp + offset;
        UINT16 size;

        if (length - offset < 6U) {
            return 0;
        }
        size = console_get_u16(device + 2U);
        if (size < 6U || size > length - offset) {
            return 0;
        }
        if (device[0] == 0x0aU &&
            !console_selection_add(Selection, console_get_u16(device + 4U),
                                   0, (device[1] & 1U) != 0)) {
            return 0;
        }
        offset += size;
    }
    selected = Selection->Serial | Selection->Graphics;
    return Selection->Primary != 0 &&
        (Selection->Primary & (Selection->Primary - 1U)) == 0 &&
        (selected & (selected + 1U)) == 0 &&
        (!(Selection->Primary & Selection->Graphics) || Selection->Serial != 0);
}

static BOOLEAN console_graphics_path(EFI_SYSTEM_TABLE *SystemTable, VOID *Path)
{
    EFI_PCI_IO_PROTOCOL *pci = NULL;
    EFI_HANDLE handle = NULL;
    VOID *remaining = Path;
    UINT32 class_revision = 0;

    return SystemTable->BootServices->LocateDevicePath(
               pci_io_guid, &remaining, &handle) == EFI_SUCCESS &&
        SystemTable->BootServices->HandleProtocol(
            handle, pci_io_guid, (VOID **)&pci) == EFI_SUCCESS &&
        pci != NULL && ((const UINT8 *)remaining)[0] == 0x7f &&
        pci->Pci.Read(pci, EfiPciWidthUint32, 8, 1, &class_revision) ==
            EFI_SUCCESS && (class_revision >> 24) == 3;
}

static BOOLEAN console_variables_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    static UINT8 global_guid[16] = {
        0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
        0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
    };
    static CHAR16 names[][10] = {
        { 'C', 'o', 'n', 'I', 'n', 0 },
        { 'C', 'o', 'n', 'I', 'n', 'D', 'e', 'v', 0 },
        { 'C', 'o', 'n', 'O', 'u', 't', 0 },
        { 'C', 'o', 'n', 'O', 'u', 't', 'D', 'e', 'v', 0 },
        { 'E', 'r', 'r', 'O', 'u', 't', 0 },
        { 'E', 'r', 'r', 'O', 'u', 't', 'D', 'e', 'v', 0 },
    };
    CONSOLE_SELECTION selected = { 0, 0, 0 };
    UINTN i;

    if (!hcdp_console_selection(SystemTable, &selected)) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        UINT8 path[256];
        UINTN size = sizeof(path);
        UINTN offset = 0, instance_start = 0, instance = 0;
        UINT32 serial_paths = 0, graphics_paths = 0;
        UINT32 attributes = 0;
        UINT32 expected = EFI_VARIABLE_BOOTSERVICE_ACCESS |
            EFI_VARIABLE_RUNTIME_ACCESS |
            ((i & 1U) ? 0 : EFI_VARIABLE_NON_VOLATILE);
        BOOLEAN serial = 0;
        BOOLEAN keyboard = 0;
        BOOLEAN ended = 0;

        if (SystemTable->RuntimeServices->GetVariable(
                names[i], global_guid, &attributes, &size, path) !=
                EFI_SUCCESS || attributes != expected || size > sizeof(path)) {
            return 0;
        }
        while (size - offset >= 4U) {
            UINTN length = console_get_u16(path + offset + 2U);

            if (length < 4U || length > size - offset) {
                return 0;
            }
            if (path[offset] == 3 && path[offset + 1U] == 14) {
                if (length != 19U) {
                    return 0;
                }
                serial = 1;
            }
            if (path[offset] == 3 && path[offset + 1U] == 5) {
                if (length != 6U) {
                    return 0;
                }
                keyboard = 1;
            }
            if (path[offset] == 2 && path[offset + 1U] == 1 &&
                length == 12U && path[offset + 4U] == 0xd0 &&
                path[offset + 5U] == 0x41 && path[offset + 6U] == 0x03 &&
                path[offset + 7U] == 0x03) {
                keyboard = 1;
            }
            if (path[offset] == 0x7f) {
                UINT8 end_type = path[offset + 1U];

                if (length != 4U || (end_type != 1 && end_type != 0xff) ||
                    instance_start == offset || instance >= 32U) {
                    return 0;
                }
                if (serial) {
                    serial_paths |= 1U << instance;
                } else if (i >= 2) {
                    path[offset + 1U] = 0xff;
                    if (!console_graphics_path(SystemTable,
                                               path + instance_start)) {
                        return 0;
                    }
                    path[offset + 1U] = end_type;
                    graphics_paths |= 1U << instance;
                } else if (!keyboard) {
                    return 0;
                }
                instance++;
                instance_start = offset + length;
                serial = keyboard = 0;
                ended = end_type == 0xff;
            }
            offset += length;
            if (ended) {
                break;
            }
        }
        if (!ended || offset != size ||
            ((i & 1U) && serial_paths == 0) ||
            ((i == 2 || i == 4) &&
             (serial_paths != selected.Serial ||
              graphics_paths != selected.Graphics))) {
            return 0;
        }
    }
    return 1;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "smoke",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    VOID *device_path = NULL;
    VOID *remaining = NULL;
    EFI_HANDLE root_handle = NULL;
    EFI_HANDLE controller_handle = NULL;
    UINTN columns = 0;
    UINTN rows = 0;
    EFI_STATUS status;

    ia64_test_pass(&context, "entry");
    ia64_test_check(
        &context, "system-table",
        SystemTable != NULL &&
            SystemTable->Hdr.Signature == EFI_SYSTEM_TABLE_SIGNATURE &&
            SystemTable->BootServices != NULL &&
            SystemTable->BootServices->Hdr.Signature ==
                EFI_BOOT_SERVICES_SIGNATURE &&
            SystemTable->RuntimeServices != NULL &&
            SystemTable->RuntimeServices->Hdr.Signature ==
                EFI_RUNTIME_SERVICES_SIGNATURE &&
            system_table_crc_valid(SystemTable),
        EFI_DEVICE_ERROR, "signature-or-crc");

    ia64_test_check(&context, "low-memory-allocation",
                    low_memory_allocation_valid(SystemTable),
                    EFI_DEVICE_ERROR, "low-page-or-reserved-memory");

    status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, loaded_image_guid, (VOID **)&loaded);
    ia64_test_check(&context, "loaded-image",
                    status == EFI_SUCCESS && loaded != NULL &&
                        loaded->DeviceHandle != NULL &&
                        loaded->ImageBase != NULL && loaded->ImageSize != 0,
                    status, "handle-protocol");

    if (loaded != NULL && loaded->DeviceHandle != NULL) {
        status = SystemTable->BootServices->HandleProtocol(
            loaded->DeviceHandle, device_path_guid, &device_path);
    } else {
        status = EFI_NOT_FOUND;
    }
    ia64_test_check(&context, "device-path",
                    status == EFI_SUCCESS && device_path != NULL,
                    status, "device-path-protocol");
    if (status == EFI_SUCCESS && device_path != NULL) {
        print_device_path(&context, device_path);
    }

    remaining = device_path;
    if (remaining != NULL &&
        SystemTable->BootServices->LocateDevicePath != NULL) {
        status = SystemTable->BootServices->LocateDevicePath(
            pci_root_guid, &remaining, &root_handle);
    } else {
        status = EFI_NOT_FOUND;
    }
    ia64_test_check(
        &context, "root-device-path",
        status == EFI_SUCCESS && root_handle != NULL &&
            remaining != NULL && remaining != device_path &&
            ((UINT8 *)remaining)[0] == 0x01 &&
            ((UINT8 *)remaining)[1] == 0x01,
        status, "pci-root-prefix");

    remaining = device_path;
    if (remaining != NULL &&
        SystemTable->BootServices->LocateDevicePath != NULL) {
        status = SystemTable->BootServices->LocateDevicePath(
            pci_io_guid, &remaining, &controller_handle);
        if (status == EFI_SUCCESS) {
            ia64_test_check(
                &context, "controller-device-path",
                controller_handle != NULL && remaining != NULL &&
                    remaining != device_path &&
                    storage_node_valid(SystemTable, controller_handle,
                                        remaining),
                status, "pci-controller-prefix");
        }
    }

    ia64_test_check(&context, "console-output",
                    SystemTable != NULL && SystemTable->ConOut != NULL &&
                        SystemTable->ConOut->OutputString != NULL &&
                        SystemTable->ConOut->QueryMode != NULL &&
                        SystemTable->ConOut->QueryMode(
                            SystemTable->ConOut, 0, &columns, &rows) ==
                            EFI_SUCCESS &&
                        columns == 80 && rows == 25,
                    EFI_DEVICE_ERROR, "conout-geometry");
    ia64_test_check(&context, "console-variables",
                    console_variables_valid(SystemTable),
                    EFI_DEVICE_ERROR, "console-device-paths");
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
