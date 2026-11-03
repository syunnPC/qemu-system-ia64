/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define EFI_SCAN_UP 0x0001U

static UINT8 text_input_ex_guid[16] = IA64_GUID_TEXT_INPUT_EX;

static EFI_STATUS wait_for_key(EFI_BOOT_SERVICES *BootServices, EFI_EVENT Event)
{
    UINTN index = ~(UINTN)0;

    if (Event == NULL) {
        return EFI_NOT_FOUND;
    }
    if (BootServices->WaitForEvent(1, &Event, &index) != EFI_SUCCESS ||
        index != 0) {
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "input",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input = SystemTable->ConIn;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input_ex = NULL;
    EFI_INPUT_KEY key;
    EFI_KEY_DATA key_data;
    EFI_STATUS status;

    (void)ImageHandle;
    status = bs->LocateProtocol(text_input_ex_guid, NULL, (VOID **)&input_ex);
    ia64_test_check(&context, "text-input-ex",
                    status == EFI_SUCCESS && input_ex != NULL, status,
                    "locate-input-ex");
    if (input == NULL || input_ex == NULL) {
        ia64_test_done(&context);
        return EFI_DEVICE_ERROR;
    }

    ia64_test_pass(&context, "ready-basic");
    status = wait_for_key(bs, input->WaitForKey);
    if (status == EFI_SUCCESS) {
        status = input->ReadKeyStroke(input, &key);
    }
    ia64_test_check(&context, "read-key-stroke",
                    status == EFI_SUCCESS && key.ScanCode == 0 &&
                        (key.UnicodeChar == 'x' || key.UnicodeChar == 'X'),
                    status, "expected-x");

    ia64_test_pass(&context, "ready-modifier");
    status = wait_for_key(bs, input_ex->WaitForKeyEx);
    if (status == EFI_SUCCESS) {
        status = input_ex->ReadKeyStrokeEx(input_ex, &key_data);
    }
    ia64_test_check(&context, "modifier-key",
                    status == EFI_SUCCESS && key_data.Key.ScanCode == 0 &&
                        key_data.Key.UnicodeChar == 'A',
                    status, "expected-uppercase-a");
    ia64_test_check(
        &context, "modifier-state",
        status == EFI_SUCCESS &&
            (key_data.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) != 0 &&
            (key_data.KeyState.KeyShiftState &
             (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0,
        status, "expected-valid-shift-state");

    ia64_test_pass(&context, "ready-extended");
    status = wait_for_key(bs, input_ex->WaitForKeyEx);
    if (status == EFI_SUCCESS) {
        status = input_ex->ReadKeyStrokeEx(input_ex, &key_data);
    }
    ia64_test_check(&context, "extended-scan-code",
                    status == EFI_SUCCESS &&
                        key_data.Key.ScanCode == EFI_SCAN_UP &&
                        key_data.Key.UnicodeChar == 0,
                    status, "expected-up-scan");

    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
