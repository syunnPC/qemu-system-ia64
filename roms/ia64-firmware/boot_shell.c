/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Interactive pre-boot manager and EFI application shell.
 */

#include "fw-boot-shell.h"

#define FW_SHELL_LINE_MAX       256U
#define FW_SHELL_ARG_MAX        16U
#define FW_SHELL_PATH_MAX       256U
#define FW_SHELL_DEVICE_PATH_MAX 1024U
#define FW_SHELL_BOOT_ORDER_MAX 16U
#define FW_SHELL_HOTKEY_POLLS   300U
#define FW_SHELL_HOTKEY_POLL_US 10000U

typedef struct {
    EFI_HANDLE handle;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *simple_fs;
} FW_SHELL_FILE_SYSTEM;

static FW_SHELL_FILE_SYSTEM *mShellFileSystems;
static UINTN mShellFileSystemCount;
static UINTN mShellCurrentFileSystem;
static CHAR8 mShellCurrentDirectory[FW_SHELL_PATH_MAX] = { '\\', 0 };

static BOOLEAN fw_shell_ascii_equal_ci(const CHAR8 *Left, const CHAR8 *Right)
{
    UINTN i = 0;

    if (Left == NULL || Right == NULL) {
        return 0;
    }
    while (Left[i] != 0 && Right[i] != 0) {
        if (fw_ascii_upper((UINT8)Left[i]) != fw_ascii_upper((UINT8)Right[i])) {
            return 0;
        }
        i++;
    }
    return Left[i] == 0 && Right[i] == 0;
}

static BOOLEAN fw_shell_ascii_starts_ci(const CHAR8 *String,
                                        const CHAR8 *Prefix)
{
    UINTN i;

    if (String == NULL || Prefix == NULL) {
        return 0;
    }
    for (i = 0; Prefix[i] != 0; i++) {
        if (String[i] == 0 || fw_ascii_upper((UINT8)String[i]) !=
                                  fw_ascii_upper((UINT8)Prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static UINTN fw_shell_ascii_length(const CHAR8 *String)
{
    UINTN length = 0;

    if (String != NULL) {
        while (String[length] != 0) {
            length++;
        }
    }
    return length;
}

static void fw_shell_puts(const CHAR8 *String)
{
    efi_conout_ascii(String);
}

static void fw_shell_put_char(CHAR8 Character)
{
    CHAR8 text[2];

    text[0] = Character;
    text[1] = 0;
    fw_shell_puts(text);
}

static void fw_shell_put_char16(const CHAR16 *String, UINTN Maximum)
{
    UINTN i;

    if (String == NULL) {
        return;
    }
    for (i = 0; i < Maximum && String[i] != 0; i++) {
        fw_shell_put_char(
            String[i] >= 0x20U && String[i] <= 0x7eU ? (CHAR8)String[i] : '?');
    }
}

static void fw_shell_put_uint64(UINT64 Value)
{
    CHAR8 digits[21];
    UINTN count = 0;

    do {
        digits[count++] = (CHAR8)('0' + Value % 10U);
        Value /= 10U;
    } while (Value != 0 && count < FW_ARRAY_SIZE(digits));

    while (count != 0) {
        fw_shell_put_char(digits[--count]);
    }
}

static void fw_shell_put_hex(UINT64 Value, UINTN Digits)
{
    static const CHAR8 hex[] = "0123456789ABCDEF";
    UINTN digit;

    if (Digits == 0 || Digits > 16U) {
        Digits = 16U;
    }
    for (digit = Digits; digit != 0; digit--) {
        fw_shell_put_char(hex[(Value >> ((digit - 1U) * 4U)) & 0xfU]);
    }
}

static void fw_shell_put_status(EFI_STATUS Status)
{
    fw_shell_puts(efi_status_name(Status));
    fw_shell_puts(" (0x");
    fw_shell_put_hex(Status, 16U);
    fw_shell_put_char(')');
}

static void fw_shell_put_two_digits(UINT8 Value)
{
    fw_shell_put_char((CHAR8)('0' + Value / 10U));
    fw_shell_put_char((CHAR8)('0' + Value % 10U));
}

static void fw_shell_put_four_digits(UINT16 Value)
{
    fw_shell_put_char((CHAR8)('0' + (Value / 1000U) % 10U));
    fw_shell_put_char((CHAR8)('0' + (Value / 100U) % 10U));
    fw_shell_put_char((CHAR8)('0' + (Value / 10U) % 10U));
    fw_shell_put_char((CHAR8)('0' + Value % 10U));
}

static BOOLEAN fw_shell_hotkey(const EFI_INPUT_KEY *Key)
{
    return Key != NULL &&
           (Key->ScanCode == EFI_SCAN_F2 || Key->ScanCode == EFI_SCAN_F12 ||
            Key->ScanCode == EFI_SCAN_DELETE || Key->UnicodeChar == 0x7fU);
}

BOOLEAN fw_boot_shell_hotkey_window(VOID)
{
    EFI_INPUT_KEY key;
    UINTN poll;

    fw_shell_puts("\r\nPress F2, F12, or Delete within 3 seconds for "
                  "the EFI shell.\r\n");
    for (poll = 0; poll < FW_SHELL_HOTKEY_POLLS; poll++) {
        while (fw_console_read_key(&key) == EFI_SUCCESS) {
            if (fw_shell_hotkey(&key)) {
                fw_shell_puts("Opening EFI shell...\r\n");
                return 1;
            }
        }
        (void)bs_stall(FW_SHELL_HOTKEY_POLL_US);
    }
    fw_shell_puts("Continuing normal boot.\r\n");
    return 0;
}

static UINTN fw_shell_read_line(CHAR8 *Line, UINTN Capacity)
{
    EFI_INPUT_KEY key;
    UINTN length = 0;

    if (Line == NULL || Capacity == 0) {
        return 0;
    }
    Line[0] = 0;
    for (;;) {
        if (fw_console_read_key(&key) != EFI_SUCCESS) {
            (void)bs_stall(1000U);
            continue;
        }
        if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            fw_shell_puts("\r\n");
            Line[length] = 0;
            return length;
        }
        if (key.UnicodeChar == '\b' || key.UnicodeChar == 0x7fU) {
            if (length != 0) {
                length--;
                Line[length] = 0;
                fw_shell_puts("\b \b");
            }
            continue;
        }
        if (key.ScanCode == EFI_SCAN_ESC) {
            while (length != 0) {
                length--;
                fw_shell_puts("\b \b");
            }
            Line[0] = 0;
            continue;
        }
        if (key.UnicodeChar >= 0x20U && key.UnicodeChar <= 0x7eU &&
            length + 1U < Capacity) {
            Line[length++] = (CHAR8)key.UnicodeChar;
            Line[length] = 0;
            fw_shell_put_char((CHAR8)key.UnicodeChar);
        }
    }
}

static UINTN fw_shell_split_line(CHAR8 *Line, CHAR8 **Arguments, UINTN Capacity)
{
    CHAR8 *source = Line;
    CHAR8 *destination = Line;
    UINTN count = 0;

    if (Line == NULL || Arguments == NULL || Capacity == 0) {
        return 0;
    }
    while (*source != 0) {
        CHAR8 quote = 0;

        while (*source == ' ' || *source == '\t') {
            source++;
        }
        if (*source == 0 || count >= Capacity) {
            break;
        }
        Arguments[count++] = destination;
        if (*source == '\'' || *source == '"') {
            quote = *source++;
        }
        while (*source != 0) {
            if (quote != 0) {
                if (*source == quote) {
                    source++;
                    break;
                }
            } else if (*source == ' ' || *source == '\t') {
                break;
            }
            *destination++ = *source++;
        }
        {
            CHAR8 *next = source;

            while (*next == ' ' || *next == '\t') {
                next++;
            }
            *destination++ = 0;
            source = next;
        }
    }
    return count;
}

static UINTN fw_shell_refresh_file_systems(void)
{
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;
    UINTN index;
    EFI_STATUS status;

    mShellFileSystemCount = 0;
    status = bs_locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
                                     (void *)mSimpleFileSystemProtocolGuid,
                                     NULL, &handle_count, &handles);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    if (mShellFileSystems != NULL) {
        (void)bs_free_pool(mShellFileSystems);
        mShellFileSystems = NULL;
    }
    status = bs_allocate_pool(EfiBootServicesData,
                              handle_count * sizeof(*mShellFileSystems),
                              (VOID **)&mShellFileSystems);
    if (status != EFI_SUCCESS) {
        (void)bs_free_pool(handles);
        fw_shell_puts("Cannot allocate the file system map.\r\n");
        return 0;
    }
    for (index = 0; index < handle_count; index++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *simple_fs = NULL;

        if (bs_handle_protocol(handles[index],
                               (void *)mSimpleFileSystemProtocolGuid,
                               (VOID **)&simple_fs) != EFI_SUCCESS ||
            simple_fs == NULL || simple_fs->OpenVolume == NULL) {
            continue;
        }
        mShellFileSystems[mShellFileSystemCount].handle = handles[index];
        mShellFileSystems[mShellFileSystemCount].simple_fs = simple_fs;
        mShellFileSystemCount++;
    }
    (void)bs_free_pool(handles);
    if (mShellCurrentFileSystem >= mShellFileSystemCount) {
        mShellCurrentFileSystem = 0;
        mShellCurrentDirectory[0] = '\\';
        mShellCurrentDirectory[1] = 0;
    }
    return mShellFileSystemCount;
}

static BOOLEAN fw_shell_parse_fs_prefix(const CHAR8 *Text, UINTN *Index,
                                        const CHAR8 **Remaining)
{
    UINTN value = 0;
    UINTN position = 2;
    BOOLEAN have_digit = 0;

    if (!fw_shell_ascii_starts_ci(Text, "fs")) {
        return 0;
    }
    while (Text[position] >= '0' && Text[position] <= '9') {
        have_digit = 1;
        if (value > (~(UINTN)0 - (UINTN)(Text[position] - '0')) / 10U) {
            return 0;
        }
        value = value * 10U + (UINTN)(Text[position] - '0');
        position++;
    }
    if (!have_digit || Text[position] != ':') {
        return 0;
    }
    if (Index != NULL) {
        *Index = value;
    }
    if (Remaining != NULL) {
        *Remaining = Text + position + 1U;
    }
    return 1;
}

static BOOLEAN fw_shell_append_component(CHAR8 *Path, UINTN *Length,
                                         const CHAR8 *Component,
                                         UINTN ComponentLength)
{
    UINTN i;

    if (*Length > 1U) {
        if (*Length + 1U >= FW_SHELL_PATH_MAX) {
            return 0;
        }
        Path[(*Length)++] = '\\';
    }
    if (ComponentLength >= FW_SHELL_PATH_MAX - *Length) {
        return 0;
    }
    for (i = 0; i < ComponentLength; i++) {
        Path[(*Length)++] = Component[i];
    }
    Path[*Length] = 0;
    return 1;
}

static void fw_shell_pop_component(CHAR8 *Path, UINTN *Length)
{
    if (*Length <= 1U) {
        return;
    }
    while (*Length > 1U && Path[*Length - 1U] != '\\') {
        (*Length)--;
    }
    if (*Length > 1U) {
        (*Length)--;
    }
    Path[*Length] = 0;
}

static EFI_STATUS fw_shell_canonical_path(const CHAR8 *Base, const CHAR8 *Input,
                                          CHAR8 *Output)
{
    CHAR8 combined[FW_SHELL_PATH_MAX * 2U];
    UINTN combined_length = 0;
    UINTN output_length = 1;
    UINTN position = 0;
    UINTN i;

    if (Base == NULL || Input == NULL || Output == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (Input[0] != '\\' && Input[0] != '/') {
        for (i = 0; Base[i] != 0; i++) {
            if (combined_length + 1U >= FW_ARRAY_SIZE(combined)) {
                return EFI_BAD_BUFFER_SIZE;
            }
            combined[combined_length++] = Base[i];
        }
        if (combined_length == 0 || combined[combined_length - 1U] != '\\') {
            combined[combined_length++] = '\\';
        }
    }
    for (i = 0; Input[i] != 0; i++) {
        if (combined_length + 1U >= FW_ARRAY_SIZE(combined)) {
            return EFI_BAD_BUFFER_SIZE;
        }
        combined[combined_length++] = Input[i] == '/' ? '\\' : Input[i];
    }
    combined[combined_length] = 0;

    Output[0] = '\\';
    Output[1] = 0;
    while (position < combined_length) {
        UINTN start;
        UINTN length;

        while (position < combined_length && combined[position] == '\\') {
            position++;
        }
        start = position;
        while (position < combined_length && combined[position] != '\\') {
            position++;
        }
        length = position - start;
        if (length == 0 || (length == 1U && combined[start] == '.')) {
            continue;
        }
        if (length == 2U && combined[start] == '.' &&
            combined[start + 1U] == '.') {
            fw_shell_pop_component(Output, &output_length);
            continue;
        }
        if (!fw_shell_append_component(Output, &output_length, combined + start,
                                       length)) {
            return EFI_BAD_BUFFER_SIZE;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS fw_shell_resolve_path(const CHAR8 *Specification,
                                        UINTN *FileSystemIndex, CHAR16 *Path,
                                        UINTN PathCapacity)
{
    CHAR8 normalized[FW_SHELL_PATH_MAX];
    const CHAR8 *input = Specification;
    const CHAR8 *remaining = NULL;
    const CHAR8 *base = mShellCurrentDirectory;
    UINTN index = mShellCurrentFileSystem;
    UINTN i;
    EFI_STATUS status;

    if (FileSystemIndex == NULL || Path == NULL || PathCapacity == 0) {
        return EFI_INVALID_PARAMETER;
    }
    if (input == NULL || input[0] == 0) {
        input = mShellCurrentDirectory;
        base = "\\";
    } else if (fw_shell_parse_fs_prefix(input, &index, &remaining)) {
        if (index >= mShellFileSystemCount) {
            return EFI_NOT_FOUND;
        }
        input = remaining[0] == 0 ? "\\" : remaining;
        base = "\\";
    }
    status = fw_shell_canonical_path(base, input, normalized);
    if (status != EFI_SUCCESS) {
        return status;
    }
    for (i = 0; normalized[i] != 0; i++) {
        if (i + 1U >= PathCapacity) {
            return EFI_BAD_BUFFER_SIZE;
        }
        Path[i] = (CHAR16)(UINT8)normalized[i];
    }
    Path[i] = 0;
    *FileSystemIndex = index;
    return EFI_SUCCESS;
}

static void fw_shell_char16_to_ascii(const CHAR16 *Input, CHAR8 *Output,
                                     UINTN Capacity)
{
    UINTN i;

    if (Output == NULL || Capacity == 0) {
        return;
    }
    for (i = 0; i + 1U < Capacity && Input != NULL && Input[i] != 0; i++) {
        Output[i] = Input[i] <= 0x7fU ? (CHAR8)Input[i] : '?';
    }
    Output[i] = 0;
}

static EFI_STATUS fw_shell_open_path(UINTN FileSystemIndex, const CHAR16 *Path,
                                     EFI_FILE_HANDLE *Root,
                                     EFI_FILE_HANDLE *File)
{
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE file = NULL;
    EFI_STATUS status;

    if (FileSystemIndex >= mShellFileSystemCount || Path == NULL ||
        Root == NULL || File == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Root = NULL;
    *File = NULL;
    status = mShellFileSystems[FileSystemIndex].simple_fs->OpenVolume(
        mShellFileSystems[FileSystemIndex].simple_fs, &root);
    if (status != EFI_SUCCESS || root == NULL) {
        return status == EFI_SUCCESS ? EFI_VOLUME_CORRUPTED : status;
    }
    if (Path[0] == '\\' && Path[1] == 0) {
        file = root;
    } else {
        status = root->Open(root, &file, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
        if (status != EFI_SUCCESS || file == NULL) {
            (void)root->Close(root);
            return status == EFI_SUCCESS ? EFI_NOT_FOUND : status;
        }
    }
    *Root = root;
    *File = file;
    return EFI_SUCCESS;
}

static void fw_shell_close_path(EFI_FILE_HANDLE Root, EFI_FILE_HANDLE File)
{
    if (File != NULL && File != Root) {
        (void)File->Close(File);
    }
    if (Root != NULL) {
        (void)Root->Close(Root);
    }
}

static void fw_shell_print_fs_name(UINTN Index)
{
    fw_shell_puts("fs");
    fw_shell_put_uint64(Index);
    fw_shell_put_char(':');
}

static void fw_shell_map(void)
{
    static UINT8 info_buffer[1024];
    UINTN index;

    fw_shell_refresh_file_systems();
    if (mShellFileSystemCount == 0) {
        fw_shell_puts("No readable file systems were found.\r\n");
        return;
    }
    fw_shell_puts("Mapping  Volume label                 Size\r\n");
    for (index = 0; index < mShellFileSystemCount; index++) {
        EFI_FILE_HANDLE root = NULL;
        UINTN info_size = sizeof(info_buffer);
        EFI_STATUS status;

        fw_shell_print_fs_name(index);
        fw_shell_puts(index == mShellCurrentFileSystem ? "*  " : "   ");
        status = mShellFileSystems[index].simple_fs->OpenVolume(
            mShellFileSystems[index].simple_fs, &root);
        if (status == EFI_SUCCESS && root != NULL &&
            root->GetInfo(root, (void *)mFileSystemInfoGuid, &info_size,
                          info_buffer) == EFI_SUCCESS) {
            FW_EFI_FILE_SYSTEM_INFO *info =
                (FW_EFI_FILE_SYSTEM_INFO *)info_buffer;
            UINTN label_length = fw_char16_bounded_len(info->VolumeLabel, 24U);
            UINTN padding;

            fw_shell_put_char16(info->VolumeLabel, label_length);
            padding = 28U - (label_length < 28U ? label_length : 28U);
            while (padding-- != 0) {
                fw_shell_put_char(' ');
            }
            fw_shell_put_uint64(info->VolumeSize / (1024U * 1024U));
            fw_shell_puts(" MiB");
        } else {
            fw_shell_puts("<unavailable>");
        }
        fw_shell_puts("\r\n");
        if (root != NULL) {
            (void)root->Close(root);
        }
    }
}

static EFI_STATUS fw_shell_list(const CHAR8 *Specification)
{
    static UINT8 info_buffer[1024];
    CHAR16 path[FW_SHELL_PATH_MAX];
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE file = NULL;
    FW_EFI_FILE_INFO *info = (FW_EFI_FILE_INFO *)info_buffer;
    UINTN fs_index;
    UINTN info_size = sizeof(info_buffer);
    EFI_STATUS status;

    status = fw_shell_resolve_path(Specification, &fs_index, path,
                                   FW_ARRAY_SIZE(path));
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_shell_open_path(fs_index, path, &root, &file);
    if (status != EFI_SUCCESS) {
        return status;
    }
    status =
        file->GetInfo(file, (void *)mFileInfoGuid, &info_size, info_buffer);
    if (status != EFI_SUCCESS) {
        fw_shell_close_path(root, file);
        return status;
    }

    fw_shell_print_fs_name(fs_index);
    fw_shell_put_char16(path, FW_ARRAY_SIZE(path));
    fw_shell_puts("\r\n");
    if ((info->Attribute & EFI_FILE_DIRECTORY) == 0) {
        fw_shell_put_uint64(info->FileSize);
        fw_shell_puts("  ");
        fw_shell_put_char16(info->FileName, 64U);
        fw_shell_puts("\r\n");
        fw_shell_close_path(root, file);
        return EFI_SUCCESS;
    }

    status = file->SetPosition(file, 0);
    if (status != EFI_SUCCESS) {
        fw_shell_close_path(root, file);
        return status;
    }
    for (;;) {
        info_size = sizeof(info_buffer);
        status = file->Read(file, &info_size, info_buffer);
        if (status != EFI_SUCCESS || info_size == 0) {
            break;
        }
        if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
            fw_shell_puts("<DIR>       ");
        } else {
            fw_shell_put_uint64(info->FileSize);
            fw_shell_puts("  ");
        }
        fw_shell_put_char16(info->FileName, 64U);
        fw_shell_puts("\r\n");
    }
    fw_shell_close_path(root, file);
    return status;
}

static EFI_STATUS fw_shell_change_directory(const CHAR8 *Specification)
{
    static UINT8 info_buffer[1024];
    CHAR16 path[FW_SHELL_PATH_MAX];
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE file = NULL;
    FW_EFI_FILE_INFO *info = (FW_EFI_FILE_INFO *)info_buffer;
    UINTN fs_index;
    UINTN info_size = sizeof(info_buffer);
    EFI_STATUS status;

    if (Specification == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = fw_shell_resolve_path(Specification, &fs_index, path,
                                   FW_ARRAY_SIZE(path));
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_shell_open_path(fs_index, path, &root, &file);
    if (status != EFI_SUCCESS) {
        return status;
    }
    status =
        file->GetInfo(file, (void *)mFileInfoGuid, &info_size, info_buffer);
    if (status == EFI_SUCCESS && (info->Attribute & EFI_FILE_DIRECTORY) == 0) {
        status = EFI_NOT_FOUND;
    }
    fw_shell_close_path(root, file);
    if (status == EFI_SUCCESS) {
        mShellCurrentFileSystem = fs_index;
        fw_shell_char16_to_ascii(path, mShellCurrentDirectory,
                                 sizeof(mShellCurrentDirectory));
    }
    return status;
}

static EFI_STATUS fw_shell_make_file_device_path(UINTN FileSystemIndex,
                                                 const CHAR16 *Path,
                                                 UINT8 *FullPath,
                                                 UINTN FullPathSize)
{
    UINT8 file_path[sizeof(FW_DEVICE_PATH_NODE) +
                    FW_SHELL_PATH_MAX * sizeof(CHAR16) +
                    sizeof(FW_DEVICE_PATH_NODE)];
    FW_DEVICE_PATH_NODE *file_node = (FW_DEVICE_PATH_NODE *)file_path;
    FW_DEVICE_PATH_NODE *end;
    CHAR16 *destination;
    UINTN chars = 0;
    UINTN i;

    if (FileSystemIndex >= mShellFileSystemCount || Path == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    while (chars < FW_SHELL_PATH_MAX && Path[chars] != 0) {
        chars++;
    }
    if (chars == FW_SHELL_PATH_MAX ||
        sizeof(*file_node) + (chars + 1U) * sizeof(CHAR16) > 0xffffU) {
        return EFI_BAD_BUFFER_SIZE;
    }
    fw_set_mem(file_path, sizeof(file_path), 0);
    file_node->Type = 0x04U;
    file_node->SubType = 0x04U;
    file_node->Length =
        (UINT16)(sizeof(*file_node) + (chars + 1U) * sizeof(CHAR16));
    destination = (CHAR16 *)(VOID *)(file_path + sizeof(*file_node));
    for (i = 0; i <= chars; i++) {
        destination[i] = Path[i];
    }
    end = (FW_DEVICE_PATH_NODE *)(VOID *)(file_path + file_node->Length);
    end->Type = 0x7fU;
    end->SubType = 0xffU;
    end->Length = sizeof(*end);
    return fw_build_file_device_path(mShellFileSystems[FileSystemIndex].handle,
                                     file_node, FullPath, FullPathSize);
}

static EFI_STATUS fw_shell_set_image_arguments(EFI_HANDLE Image,
                                               UINTN ArgumentCount,
                                               CHAR8 **Arguments,
                                               VOID **Allocation)
{
    CHAR16 options[FW_SHELL_LINE_MAX];
    UINTN length = 0;
    UINTN argument;

    *Allocation = NULL;
    for (argument = 0; argument < ArgumentCount; argument++) {
        UINTN character;

        if (argument != 0) {
            if (length + 1U >= FW_ARRAY_SIZE(options)) {
                return EFI_BAD_BUFFER_SIZE;
            }
            options[length++] = ' ';
        }
        for (character = 0; Arguments[argument][character] != 0; character++) {
            if (length + 1U >= FW_ARRAY_SIZE(options)) {
                return EFI_BAD_BUFFER_SIZE;
            }
            options[length++] = (CHAR16)(UINT8)Arguments[argument][character];
        }
    }
    if (length == 0) {
        return EFI_SUCCESS;
    }
    options[length++] = 0;
    return fw_copy_loaded_image_load_options(
        Image, options, (UINT32)(length * sizeof(CHAR16)), Allocation);
}

static EFI_STATUS fw_shell_execute_path(const CHAR8 *Specification,
                                        UINTN ArgumentCount, CHAR8 **Arguments,
                                        BOOLEAN BootPolicy)
{
    static UINT8 full_path[FW_SHELL_DEVICE_PATH_MAX];
    static CHAR16 path[FW_SHELL_PATH_MAX];
    EFI_HANDLE image = NULL;
    VOID *load_options = NULL;
    UINTN fs_index;
    EFI_STATUS status;

    status = fw_shell_resolve_path(Specification, &fs_index, path,
                                   FW_ARRAY_SIZE(path));
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_shell_make_file_device_path(fs_index, path, full_path,
                                            sizeof(full_path));
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_load_image(BootPolicy, full_path, &image);
    if (status != EFI_SUCCESS) {
        return status;
    }
    if (ArgumentCount != 0) {
        status = fw_shell_set_image_arguments(image, ArgumentCount, Arguments,
                                              &load_options);
    }
    /*
     * Booting optical media from the shell uses empty load options, matching
     * standard OS-agnostic EFI removable-media boot (no injected payload).
     */
    if (status != EFI_SUCCESS) {
        (void)fw_unload_image(image);
        return status;
    }

    if (BootPolicy) {
        (void)fw_set_watchdog_timeout(300);
    }
    fw_set_sal_loader_handoff_pending(BootPolicy);
    status = fw_start_image(image);
    fw_set_sal_loader_handoff_pending(0);
    if (BootPolicy) {
        (void)fw_set_watchdog_timeout(0);
    }
    if (!fw_boot_services_exited()) {
        if (load_options != NULL) {
            (void)fw_release_loaded_image_load_options(image, load_options);
        }
        (void)fw_unload_image(image);
    }
    return status;
}

static BOOLEAN fw_shell_hex_digit(CHAR8 Character, UINT8 *Value)
{
    if (Character >= '0' && Character <= '9') {
        *Value = Character - '0';
        return 1;
    }
    Character = (CHAR8)fw_ascii_upper((UINT8)Character);
    if (Character >= 'A' && Character <= 'F') {
        *Value = Character - 'A' + 10U;
        return 1;
    }
    return 0;
}

static BOOLEAN fw_shell_parse_boot_number(const CHAR8 *Text, UINT16 *Number)
{
    UINTN position = 0;
    UINTN digits = 0;
    UINT16 value = 0;
    UINT8 digit;

    if (Text == NULL || Number == NULL) {
        return 0;
    }
    if (fw_shell_ascii_starts_ci(Text, "Boot")) {
        position = 4;
    } else if (Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X')) {
        position = 2;
    }
    while (Text[position] != 0 && digits < 4U) {
        if (!fw_shell_hex_digit(Text[position], &digit)) {
            return 0;
        }
        value = (UINT16)((value << 4) | digit);
        position++;
        digits++;
    }
    if (Text[position] != 0 || digits == 0) {
        return 0;
    }
    *Number = value;
    return 1;
}

static BOOLEAN fw_shell_boot_option_exists(UINT16 Number)
{
    CHAR16 name[9];
    UINT8 data[NVRAM_VAR_DATA_MAX];
    UINTN size = sizeof(data);
    UINT32 attributes;

    fw_boot_option_name(Number, name);
    return rs_get_variable(name, (void *)mEfiGlobalVariableGuid, &attributes,
                           &size, data) == EFI_SUCCESS;
}

static void fw_shell_print_boot_option(UINT16 Number)
{
    static UINT8 option[NVRAM_VAR_DATA_MAX];
    CHAR16 name[9];
    UINTN size = sizeof(option);
    UINTN description_size;
    UINTN chars;
    UINT32 option_attributes;
    UINT32 attributes = 0;
    EFI_STATUS status;

    fw_shell_puts("Boot");
    fw_shell_put_hex(Number, 4U);
    fw_shell_puts("  ");
    fw_boot_option_name(Number, name);
    status = rs_get_variable(name, (void *)mEfiGlobalVariableGuid, &attributes,
                             &size, option);
    if (status != EFI_SUCCESS) {
        fw_shell_puts("<missing>\r\n");
        return;
    }
    description_size = fw_load_option_description_size(option, size);
    if (description_size == 0) {
        fw_shell_puts("<invalid load option>\r\n");
        return;
    }
    chars = description_size / sizeof(CHAR16);
    if (chars != 0) {
        chars--;
    }
    fw_shell_put_char16((const CHAR16 *)(const VOID *)(option + sizeof(UINT32) +
                                                       sizeof(UINT16)),
                        chars);
    fw_copy_mem(&option_attributes, option, sizeof(option_attributes));
    if ((option_attributes & 1U) == 0) {
        fw_shell_puts(" [inactive]");
    }
    fw_shell_puts("\r\n");
}

static EFI_STATUS fw_shell_get_boot_order(UINT16 *Order, UINTN *Count)
{
    static CHAR16 name[] = { 'B', 'o', 'o', 't', 'O', 'r', 'd', 'e', 'r', 0 };
    UINTN size;
    UINTN count;
    UINT32 attributes = 0;
    EFI_STATUS status;

    if (Order == NULL || Count == NULL || *Count > FW_SHELL_BOOT_ORDER_MAX) {
        return EFI_INVALID_PARAMETER;
    }
    count = *Count;
    size = count * sizeof(UINT16);
    status = rs_get_variable(name, (void *)mEfiGlobalVariableGuid, &attributes,
                             &size, Order);
    if (status != EFI_SUCCESS) {
        return status;
    }
    if ((attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) == 0 || size == 0 ||
        (size % sizeof(UINT16)) != 0) {
        return EFI_LOAD_ERROR;
    }
    *Count = size / sizeof(UINT16);
    return EFI_SUCCESS;
}

static void fw_shell_show_boot_order(void)
{
    UINT16 order[FW_SHELL_BOOT_ORDER_MAX];
    UINTN count = FW_ARRAY_SIZE(order);
    UINTN index;
    EFI_STATUS status = fw_shell_get_boot_order(order, &count);

    if (status != EFI_SUCCESS) {
        fw_shell_puts("BootOrder is unavailable: ");
        fw_shell_put_status(status);
        fw_shell_puts("\r\n");
        return;
    }
    fw_shell_puts("BootOrder:");
    for (index = 0; index < count; index++) {
        fw_shell_puts(" Boot");
        fw_shell_put_hex(order[index], 4U);
    }
    fw_shell_puts("\r\n");
    for (index = 0; index < count; index++) {
        fw_shell_print_boot_option(order[index]);
    }
}

static EFI_STATUS fw_shell_set_boot_order(UINTN ArgumentCount,
                                          CHAR8 **Arguments)
{
    static CHAR16 name[] = { 'B', 'o', 'o', 't', 'O', 'r', 'd', 'e', 'r', 0 };
    UINT16 order[FW_SHELL_BOOT_ORDER_MAX];
    UINTN data_size;
    UINTN argument;
    UINTN previous;

    if (ArgumentCount == 0 || ArgumentCount > FW_ARRAY_SIZE(order)) {
        return EFI_INVALID_PARAMETER;
    }
    for (argument = 0; argument < ArgumentCount; argument++) {
        if (!fw_shell_parse_boot_number(Arguments[argument],
                                        &order[argument]) ||
            !fw_shell_boot_option_exists(order[argument])) {
            return EFI_NOT_FOUND;
        }
        for (previous = 0; previous < argument; previous++) {
            if (order[previous] == order[argument]) {
                return EFI_INVALID_PARAMETER;
            }
        }
    }
    data_size = ArgumentCount;
    data_size *= sizeof(UINT16);
    return rs_set_variable(name, (void *)mEfiGlobalVariableGuid,
                           EFI_VARIABLE_NON_VOLATILE |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS |
                               EFI_VARIABLE_RUNTIME_ACCESS,
                           data_size, order);
}

static EFI_STATUS fw_shell_set_boot_next(const CHAR8 *Argument)
{
    static CHAR16 name[] = { 'B', 'o', 'o', 't', 'N', 'e', 'x', 't', 0 };
    UINT16 number;

    if (!fw_shell_parse_boot_number(Argument, &number) ||
        !fw_shell_boot_option_exists(number)) {
        return EFI_NOT_FOUND;
    }
    return rs_set_variable(name, (void *)mEfiGlobalVariableGuid,
                           EFI_VARIABLE_NON_VOLATILE |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS |
                               EFI_VARIABLE_RUNTIME_ACCESS,
                           sizeof(number), &number);
}

static void fw_shell_show_boot_next(void)
{
    static CHAR16 name[] = { 'B', 'o', 'o', 't', 'N', 'e', 'x', 't', 0 };
    UINT16 number;
    UINTN size = sizeof(number);
    UINT32 attributes = 0;
    EFI_STATUS status;

    status = rs_get_variable(name, (void *)mEfiGlobalVariableGuid, &attributes,
                             &size, &number);
    if (status == EFI_NOT_FOUND) {
        fw_shell_puts("BootNext is not set.\r\n");
        return;
    }
    if (status != EFI_SUCCESS || size != sizeof(number) ||
        (attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) == 0) {
        fw_shell_puts("BootNext is invalid.\r\n");
        return;
    }
    fw_shell_puts("BootNext: Boot");
    fw_shell_put_hex(number, 4U);
    fw_shell_puts("\r\n");
    fw_shell_print_boot_option(number);
}

static BOOLEAN fw_shell_parse_decimal(const CHAR8 *Text, UINTN Length,
                                      UINTN *Value)
{
    UINTN result = 0;
    UINTN index;

    if (Text == NULL || Value == NULL || Length == 0) {
        return 0;
    }
    for (index = 0; index < Length; index++) {
        if (Text[index] < '0' || Text[index] > '9') {
            return 0;
        }
        result = result * 10U + (UINTN)(Text[index] - '0');
    }
    *Value = result;
    return 1;
}

static BOOLEAN fw_shell_parse_date(const CHAR8 *Text, EFI_TIME *Time)
{
    EFI_TIME next;
    UINTN year;
    UINTN month;
    UINTN day;

    if (Text == NULL || Time == NULL || fw_shell_ascii_length(Text) != 10U ||
        Text[4] != '-' || Text[7] != '-' ||
        !fw_shell_parse_decimal(Text, 4U, &year) ||
        !fw_shell_parse_decimal(Text + 5U, 2U, &month) ||
        !fw_shell_parse_decimal(Text + 8U, 2U, &day)) {
        return 0;
    }
    next = *Time;
    next.Year = (UINT16)year;
    next.Month = (UINT8)month;
    next.Day = (UINT8)day;
    if (!efi_time_valid(&next)) {
        return 0;
    }
    *Time = next;
    return 1;
}

static BOOLEAN fw_shell_parse_time(const CHAR8 *Text, EFI_TIME *Time)
{
    EFI_TIME next;
    UINTN hour;
    UINTN minute;
    UINTN second = 0;
    UINTN length = fw_shell_ascii_length(Text);

    if (Text == NULL || Time == NULL || (length != 5U && length != 8U) ||
        Text[2] != ':' || (length == 8U && Text[5] != ':') ||
        !fw_shell_parse_decimal(Text, 2U, &hour) ||
        !fw_shell_parse_decimal(Text + 3U, 2U, &minute) ||
        (length == 8U && !fw_shell_parse_decimal(Text + 6U, 2U, &second))) {
        return 0;
    }
    next = *Time;
    next.Hour = (UINT8)hour;
    next.Minute = (UINT8)minute;
    next.Second = (UINT8)second;
    next.Nanosecond = 0;
    if (!efi_time_valid(&next)) {
        return 0;
    }
    *Time = next;
    return 1;
}

static void fw_shell_show_date_time(void)
{
    EFI_TIME time;
    EFI_STATUS status = rs_get_time(&time, NULL);

    if (status != EFI_SUCCESS) {
        fw_shell_puts("Clock unavailable: ");
        fw_shell_put_status(status);
        fw_shell_puts("\r\n");
        return;
    }
    fw_shell_put_four_digits(time.Year);
    fw_shell_put_char('-');
    fw_shell_put_two_digits(time.Month);
    fw_shell_put_char('-');
    fw_shell_put_two_digits(time.Day);
    fw_shell_put_char(' ');
    fw_shell_put_two_digits(time.Hour);
    fw_shell_put_char(':');
    fw_shell_put_two_digits(time.Minute);
    fw_shell_put_char(':');
    fw_shell_put_two_digits(time.Second);
    fw_shell_puts("\r\n");
}

static EFI_STATUS fw_shell_set_date(const CHAR8 *Argument)
{
    EFI_TIME time;
    EFI_STATUS status = rs_get_time(&time, NULL);

    if (status != EFI_SUCCESS) {
        return status;
    }
    if (!fw_shell_parse_date(Argument, &time)) {
        return EFI_INVALID_PARAMETER;
    }
    return rs_set_time(&time);
}

static EFI_STATUS fw_shell_set_time(const CHAR8 *Argument)
{
    EFI_TIME time;
    EFI_STATUS status = rs_get_time(&time, NULL);

    if (status != EFI_SUCCESS) {
        return status;
    }
    if (!fw_shell_parse_time(Argument, &time)) {
        return EFI_INVALID_PARAMETER;
    }
    return rs_set_time(&time);
}

static void fw_shell_system_info(void)
{
    fw_shell_refresh_file_systems();
    fw_shell_puts("Firmware:       IA-64 EFI 1.10\r\n");
    fw_shell_puts("Processors:     ");
    fw_shell_put_uint64(fw_processor_count());
    fw_shell_puts("\r\nInstalled RAM:  ");
    fw_shell_put_uint64(fw_installed_ram_size() / (1024U * 1024U));
    fw_shell_puts(" MiB\r\nConsole:        ");
    fw_shell_puts(fw_handoff_vga_console_primary() ? "graphics" : "serial");
    fw_shell_puts("\r\nGraphics:       ");
    fw_shell_put_uint64(fw_graphics_width());
    fw_shell_put_char('x');
    fw_shell_put_uint64(fw_graphics_height());
    fw_shell_puts("x32\r\nBoot storage:   ");
    fw_shell_puts(fw_storage_description(1));
    fw_shell_puts("\r\nFixed storage:  ");
    fw_shell_puts(fw_storage_description(0));
    fw_shell_puts("\r\nPartitions:     ");
    fw_shell_put_uint64(fw_partition_count());
    fw_shell_puts("\r\nFile systems:   ");
    fw_shell_put_uint64(mShellFileSystemCount);
    fw_shell_puts("\r\nNVRAM backing:  ");
    fw_shell_puts(fw_handoff_nvram_persistent() ? "persistent" : "volatile");
    fw_shell_puts("\r\nDate and time:  ");
    fw_shell_show_date_time();
}

static EFI_STATUS fw_shell_edd30(UINTN Count, CHAR8 **Arguments)
{
    CHAR16 name[] = { 'E', 'D', 'D', '3', '0', 0 };
    BOOLEAN value;
    UINTN size = sizeof(value);
    EFI_STATUS status;

    if (!fw_i2000_profile_enabled()) {
        fw_shell_puts("EDD30 is available only for SCSI devices "
                      "on HP i2000.\r\n");
        return EFI_UNSUPPORTED;
    }
    if (Count == 1) {
        status = rs_get_variable(name, (VOID *)mBlockIoProtocolGuid, NULL,
                                 &size, &value);
        if (status != EFI_SUCCESS) {
            return status;
        }
        if (size != sizeof(value) || value > 1) {
            return EFI_INVALID_PARAMETER;
        }
        fw_shell_puts(value ? "EDD30: on\r\n" : "EDD30: off\r\n");
        return EFI_SUCCESS;
    }
    if (Count != 2 || (!fw_shell_ascii_equal_ci(Arguments[1], "on") &&
                       !fw_shell_ascii_equal_ci(Arguments[1], "off"))) {
        fw_shell_puts("Usage: edd30 [on|off]\r\n");
        return EFI_INVALID_PARAMETER;
    }
    value = fw_shell_ascii_equal_ci(Arguments[1], "on");
    status = rs_set_variable(name, (VOID *)mBlockIoProtocolGuid,
                             EFI_VARIABLE_NON_VOLATILE |
                                 EFI_VARIABLE_BOOTSERVICE_ACCESS |
                                 EFI_VARIABLE_RUNTIME_ACCESS,
                             sizeof(value), &value);
    if (status == EFI_SUCCESS) {
        fw_shell_puts("EDD30 saved; reset to apply.\r\n");
    }
    return status;
}

static void fw_shell_help(void)
{
    fw_shell_puts(
        "Commands:\r\n"
        "  help                         Show this help\r\n"
        "  info                         Show system configuration\r\n"
        "  map                          List readable file systems\r\n"
        "  fsN:                         Select a file system\r\n"
        "  pwd                          Show current path\r\n"
        "  ls [fsN:\\path]              List a directory or file\r\n"
        "  cd <fsN:\\path>              Change current directory\r\n"
        "  run <fsN:\\app.efi> [args]   Execute an EFI application\r\n"
        "  boot [Boot####|fsN:|path]    Show or launch boot target\r\n"
        "  bootorder [Boot#### ...]     Show or save boot order\r\n"
        "  bootnext [Boot####]          Show or select next-boot option\r\n"
        "  edd30 [on|off]               Show or set i2000 SCSI path mode\r\n"
        "  date [YYYY-MM-DD]            Show or set date\r\n"
        "  time [HH:MM[:SS]]            Show or set time\r\n"
        "  clear                        Clear the screen\r\n"
        "  reset                        Reset the machine\r\n"
        "  exit                         Resume normal boot\r\n");
}

static void fw_shell_prompt(void)
{
    if (mShellFileSystemCount != 0) {
        fw_shell_print_fs_name(mShellCurrentFileSystem);
        fw_shell_puts(mShellCurrentDirectory);
    }
    fw_shell_puts("> ");
}

static void fw_shell_report_command_status(EFI_STATUS Status)
{
    if (Status != EFI_SUCCESS && !fw_boot_services_exited()) {
        fw_shell_puts("Command failed: ");
        fw_shell_put_status(Status);
        fw_shell_puts("\r\n");
    }
}

static BOOLEAN fw_shell_looks_like_path(const CHAR8 *Command)
{
    UINTN length = fw_shell_ascii_length(Command);

    if (Command == NULL) {
        return 0;
    }
    return Command[0] == '\\' || Command[0] == '/' ||
           fw_shell_parse_fs_prefix(Command, NULL, NULL) ||
           (length >= 4U &&
            fw_shell_ascii_equal_ci(Command + length - 4U, ".efi"));
}

static BOOLEAN fw_shell_dispatch(UINTN ArgumentCount, CHAR8 **Arguments)
{
    EFI_STATUS status = EFI_SUCCESS;

    if (ArgumentCount == 0) {
        return 1;
    }
    if (fw_shell_ascii_equal_ci(Arguments[0], "help") ||
        fw_shell_ascii_equal_ci(Arguments[0], "?")) {
        fw_shell_help();
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "info")) {
        fw_shell_system_info();
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "map")) {
        fw_shell_map();
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "pwd")) {
        if (mShellFileSystemCount == 0) {
            fw_shell_puts("No current file system.\r\n");
        } else {
            fw_shell_print_fs_name(mShellCurrentFileSystem);
            fw_shell_puts(mShellCurrentDirectory);
            fw_shell_puts("\r\n");
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "ls") ||
               fw_shell_ascii_equal_ci(Arguments[0], "dir")) {
        status = fw_shell_list(ArgumentCount > 1U ? Arguments[1] : NULL);
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "cd")) {
        status = ArgumentCount == 2U ? fw_shell_change_directory(Arguments[1])
                                     : EFI_INVALID_PARAMETER;
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "run")) {
        status = ArgumentCount >= 2U
                     ? fw_shell_execute_path(Arguments[1], ArgumentCount - 2U,
                                             Arguments + 2U, 0)
                     : EFI_INVALID_PARAMETER;
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "bootorder")) {
        if (ArgumentCount == 1U) {
            fw_shell_show_boot_order();
        } else {
            status =
                fw_shell_set_boot_order(ArgumentCount - 1U, Arguments + 1U);
            if (status == EFI_SUCCESS) {
                fw_shell_puts(fw_handoff_nvram_persistent()
                                  ? "BootOrder saved to persistent NVRAM.\r\n"
                                  : "BootOrder saved for this session.\r\n");
                fw_shell_show_boot_order();
            }
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "bootnext")) {
        if (ArgumentCount == 1U) {
            fw_shell_show_boot_next();
        } else {
            status = ArgumentCount == 2U ? fw_shell_set_boot_next(Arguments[1])
                                         : EFI_INVALID_PARAMETER;
            if (status == EFI_SUCCESS) {
                fw_shell_puts(fw_handoff_nvram_persistent()
                                  ? "BootNext saved to persistent NVRAM.\r\n"
                                  : "BootNext saved for this session.\r\n");
            }
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "boot")) {
        if (ArgumentCount == 1U) {
            fw_shell_show_boot_order();
            fw_shell_map();
        } else {
            UINT16 number;
            UINTN fs_index;
            const CHAR8 *remaining;

            if (fw_shell_parse_boot_number(Arguments[1], &number)) {
                status = fw_boot_image_from_boot_option(number);
            } else if (fw_shell_parse_fs_prefix(Arguments[1], &fs_index,
                                                &remaining) &&
                       remaining[0] == 0) {
                static CHAR8 default_path[] = "\\EFI\\BOOT\\BOOTIA64.EFI";
                CHAR8 specification[FW_SHELL_PATH_MAX];
                UINTN length = 0;
                UINTN i;
                CHAR8 digits[20];
                UINTN digit_count = 0;

                specification[length++] = 'f';
                specification[length++] = 's';
                do {
                    digits[digit_count++] = (CHAR8)('0' + fs_index % 10U);
                    fs_index /= 10U;
                } while (fs_index != 0);
                while (digit_count != 0) {
                    specification[length++] = digits[--digit_count];
                }
                specification[length++] = ':';
                for (i = 0; default_path[i] != 0; i++) {
                    specification[length++] = default_path[i];
                }
                specification[length] = 0;
                status = fw_shell_execute_path(specification, 0, NULL, 1);
            } else {
                status = fw_shell_execute_path(Arguments[1], ArgumentCount - 2U,
                                               Arguments + 2U, 1);
            }
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "date")) {
        if (ArgumentCount == 1U) {
            fw_shell_show_date_time();
        } else {
            status = ArgumentCount == 2U ? fw_shell_set_date(Arguments[1])
                                         : EFI_INVALID_PARAMETER;
            if (status == EFI_SUCCESS) {
                fw_shell_show_date_time();
            }
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "time")) {
        if (ArgumentCount == 1U) {
            fw_shell_show_date_time();
        } else {
            status = ArgumentCount == 2U ? fw_shell_set_time(Arguments[1])
                                         : EFI_INVALID_PARAMETER;
            if (status == EFI_SUCCESS) {
                fw_shell_show_date_time();
            }
        }
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "clear") ||
               fw_shell_ascii_equal_ci(Arguments[0], "cls")) {
        (void)fw_console_clear();
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "edd30")) {
        status = fw_shell_edd30(ArgumentCount, Arguments);
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "reset")) {
        fw_reset_cold();
    } else if (fw_shell_ascii_equal_ci(Arguments[0], "exit")) {
        return 0;
    } else {
        UINTN fs_index;
        const CHAR8 *remaining;

        if (fw_shell_parse_fs_prefix(Arguments[0], &fs_index, &remaining) &&
            remaining[0] == 0 && ArgumentCount == 1U) {
            status = fw_shell_change_directory(Arguments[0]);
        } else if (fw_shell_looks_like_path(Arguments[0])) {
            status = fw_shell_execute_path(Arguments[0], ArgumentCount - 1U,
                                           Arguments + 1U, 0);
        } else {
            fw_shell_puts("Unknown command. Type 'help' for a list.\r\n");
            return 1;
        }
    }
    fw_shell_report_command_status(status);
    return !fw_boot_services_exited();
}

void fw_boot_shell_run(VOID)
{
    CHAR8 line[FW_SHELL_LINE_MAX];
    CHAR8 *arguments[FW_SHELL_ARG_MAX];

    fw_shell_refresh_file_systems();
    (void)fw_console_clear();
    fw_shell_puts("IA-64 EFI shell\r\n"
                  "Type 'help' for commands; 'exit' resumes boot.\r\n\r\n");
    while (!fw_boot_services_exited()) {
        UINTN argument_count;

        fw_shell_prompt();
        (void)fw_shell_read_line(line, sizeof(line));
        argument_count =
            fw_shell_split_line(line, arguments, FW_ARRAY_SIZE(arguments));
        if (!fw_shell_dispatch(argument_count, arguments)) {
            break;
        }
    }
    if (!fw_boot_services_exited()) {
        fw_shell_puts("Leaving EFI shell.\r\n");
    }
}
