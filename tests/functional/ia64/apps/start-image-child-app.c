/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "common/services.h"

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 start_image_change_guid[16] = {
    0x49, 0x41, 0x36, 0x34, 0x53, 0x54, 0x41, 0x52,
    0x54, 0x43, 0x48, 0x41, 0x4e, 0x47, 0x45, 0x01,
};

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    TEST_START_IMAGE_CHILD_OPTIONS *options;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_HANDLE controller;
    EFI_STATUS status;

    if (SystemTable == NULL || SystemTable->BootServices == NULL ||
        SystemTable->BootServices->HandleProtocol(
            ImageHandle, loaded_image_guid, (VOID **)&loaded) != EFI_SUCCESS ||
        loaded == NULL ||
        loaded->LoadOptionsSize != sizeof(TEST_START_IMAGE_CHILD_OPTIONS) ||
        loaded->LoadOptions == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    options = loaded->LoadOptions;
    if (options->Signature != START_IMAGE_CHILD_SIGNATURE ||
        options->Controller == NULL || options->Interface == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    controller = options->Controller;
    if (options->ReinstallProtocol != NULL) {
        status = SystemTable->BootServices->ReinstallProtocolInterface(
            controller, options->ReinstallProtocol,
            options->Interface, options->Interface);
    } else {
        status = SystemTable->BootServices->InstallProtocolInterface(
            &controller, start_image_change_guid, EFI_NATIVE_INTERFACE,
            options->Interface);
    }
    if (status == EFI_SUCCESS && options->ConnectReady != NULL) {
        *options->ConnectReady = 1;
    }
    if (status == EFI_SUCCESS && options->UseExit) {
        return SystemTable->BootServices->Exit(ImageHandle, EFI_SUCCESS,
                                                0, NULL);
    }
    return status;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
