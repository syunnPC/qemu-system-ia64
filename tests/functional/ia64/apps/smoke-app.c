/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_io_guid[16] = IA64_GUID_PCI_IO;

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
    UINTN i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        UINT8 path[256];
        UINTN size = sizeof(path);
        UINTN offset = 0;
        UINT32 attributes = 0;
        UINT32 expected = EFI_VARIABLE_BOOTSERVICE_ACCESS |
            EFI_VARIABLE_RUNTIME_ACCESS |
            ((i & 1U) ? 0 : EFI_VARIABLE_NON_VOLATILE);
        BOOLEAN serial = 0;
        BOOLEAN ended = 0;

        if (SystemTable->RuntimeServices->GetVariable(
                names[i], global_guid, &attributes, &size, path) !=
                EFI_SUCCESS || attributes != expected) {
            return 0;
        }
        while (size - offset >= 4U) {
            UINTN length = path[offset + 2U] |
                ((UINTN)path[offset + 3U] << 8);

            if (length < 4U || length > size - offset) {
                return 0;
            }
            if (path[offset] == 3 && path[offset + 1U] == 14) {
                if (length != 19U) {
                    return 0;
                }
                serial = 1;
            }
            if (path[offset] == 0x7f) {
                if (length != 4U || (path[offset + 1U] != 1 &&
                                    path[offset + 1U] != 0xff)) {
                    return 0;
                }
                ended = path[offset + 1U] == 0xff;
            }
            offset += length;
            if (ended) {
                break;
            }
        }
        if (!ended || offset != size ||
            ((i == 0 || i == 1 || i == 3) && !serial)) {
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
