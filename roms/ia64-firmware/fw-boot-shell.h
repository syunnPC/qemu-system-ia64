/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Interactive boot shell and its firmware service boundary.
 */

#ifndef IA64_FIRMWARE_FW_BOOT_SHELL_H
#define IA64_FIRMWARE_FW_BOOT_SHELL_H

#include "fw-device-path.h"
#include "fw-services.h"

#define EFI_SCAN_UP        0x0001U
#define EFI_SCAN_DOWN      0x0002U
#define EFI_SCAN_RIGHT     0x0003U
#define EFI_SCAN_LEFT      0x0004U
#define EFI_SCAN_HOME      0x0005U
#define EFI_SCAN_END       0x0006U
#define EFI_SCAN_INSERT    0x0007U
#define EFI_SCAN_DELETE    0x0008U
#define EFI_SCAN_PAGE_UP   0x0009U
#define EFI_SCAN_PAGE_DOWN 0x000aU
#define EFI_SCAN_F1        0x000bU
#define EFI_SCAN_F2        0x000cU
#define EFI_SCAN_F3        0x000dU
#define EFI_SCAN_F4        0x000eU
#define EFI_SCAN_F5        0x000fU
#define EFI_SCAN_F6        0x0010U
#define EFI_SCAN_F7        0x0011U
#define EFI_SCAN_F8        0x0012U
#define EFI_SCAN_F9        0x0013U
#define EFI_SCAN_F10       0x0014U
#define EFI_SCAN_F11       0x0015U
#define EFI_SCAN_F12       0x0016U
#define EFI_SCAN_ESC       0x0017U

#define EFI_LOCATE_BY_PROTOCOL 2U

#define EFI_FILE_PROTOCOL_REVISION 0x00010000U
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000U
#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL
#define EFI_FILE_HIDDEN      0x0000000000000002ULL
#define EFI_FILE_SYSTEM      0x0000000000000004ULL
#define EFI_FILE_DIRECTORY   0x0000000000000010ULL
#define EFI_FILE_ARCHIVE     0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR  0x0000000000000037ULL

#define EFI_VARIABLE_NON_VOLATILE       0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS     0x00000004U
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD 0x00000008U
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS 0x00000010U
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS 0x00000020U
#define EFI_VARIABLE_APPEND_WRITE       0x00000040U
#define EFI_VARIABLE_ENHANCED_AUTHENTICATED_ACCESS 0x00000080U

#define FW_NVRAM_VARIABLE_DATA_MAX 1024U
#define NVRAM_VAR_DATA_MAX FW_NVRAM_VARIABLE_DATA_MAX

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct {
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    UINT8 Pad1;
    UINT32 Nanosecond;
    INT16 TimeZone;
    UINT8 Daylight;
    UINT8 Pad2;
} EFI_TIME;

typedef struct {
    UINT32 Resolution;
    UINT32 Accuracy;
    BOOLEAN SetsToZero;
} EFI_TIME_CAPABILITIES;

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_FILE_PROTOCOL *EFI_FILE_HANDLE;

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
                             EFI_FILE_HANDLE *Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_HANDLE *NewHandle,
                       CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (*Delete)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize,
                       VOID *Buffer);
    EFI_STATUS (*Write)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize,
                        VOID *Buffer);
    EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (*GetInfo)(EFI_FILE_PROTOCOL *This, VOID *InformationType,
                          UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (*SetInfo)(EFI_FILE_PROTOCOL *This, VOID *InformationType,
                          UINTN BufferSize, VOID *Buffer);
    EFI_STATUS (*Flush)(EFI_FILE_PROTOCOL *This);
};

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT8 CreateTime[16];
    UINT8 LastAccessTime[16];
    UINT8 ModificationTime[16];
    UINT64 Attribute;
    CHAR16 FileName[1];
} FW_EFI_FILE_INFO;

typedef struct {
    UINT64 Size;
    BOOLEAN ReadOnly;
    UINT8 Reserved[7];
    UINT64 VolumeSize;
    UINT64 FreeSpace;
    UINT32 BlockSize;
    CHAR16 VolumeLabel[1];
} FW_EFI_FILE_SYSTEM_INFO;

typedef struct {
    CHAR16 VolumeLabel[1];
} FW_EFI_FILE_SYSTEM_VOLUME_LABEL;

extern const UINT8 mSimpleFileSystemProtocolGuid[16];
extern const UINT8 mFileInfoGuid[16];
extern const UINT8 mFileSystemInfoGuid[16];
extern const UINT8 mEfiGlobalVariableGuid[16];
extern const UINT8 mBlockIoProtocolGuid[16];

UINT8 fw_ascii_upper(UINT8 value);
void efi_conout_ascii(const CHAR8 *string);
const CHAR8 *efi_status_name(EFI_STATUS status);
UINTN fw_char16_bounded_len(const CHAR16 *name, UINTN maximum);
BOOLEAN efi_time_valid(const EFI_TIME *time);
UINT16 conin_ansi_numeric_scan(UINTN number);

EFI_STATUS fw_console_read_key(EFI_INPUT_KEY *key);
EFI_STATUS fw_console_clear(VOID);
BOOLEAN fw_boot_services_exited(VOID);

EFI_STATUS bs_handle_protocol(EFI_HANDLE handle, VOID *protocol,
                              VOID **interface);
EFI_STATUS bs_locate_handle_buffer(UINTN search_type, VOID *protocol,
                                   VOID *search_key, UINTN *handle_count,
                                   EFI_HANDLE **handles);
EFI_STATUS fw_build_file_device_path(EFI_HANDLE device_handle,
                                     const FW_DEVICE_PATH_NODE *file_path,
                                     UINT8 *full_path, UINTN full_path_size);
EFI_STATUS fw_copy_loaded_image_load_options(EFI_HANDLE image_handle,
                                             const VOID *load_options,
                                             UINT32 load_options_size,
                                             VOID **allocation);
EFI_STATUS fw_release_loaded_image_load_options(EFI_HANDLE image_handle,
                                                VOID *allocation);

EFI_STATUS fw_load_image(BOOLEAN boot_policy, VOID *device_path,
                         EFI_HANDLE *image);
EFI_STATUS fw_start_image(EFI_HANDLE image);
EFI_STATUS fw_unload_image(EFI_HANDLE image);
EFI_STATUS fw_set_watchdog_timeout(UINTN timeout_seconds);
void fw_set_sal_loader_handoff_pending(BOOLEAN pending);

EFI_STATUS rs_get_variable(CHAR16 *name, VOID *vendor_guid, UINT32 *attributes,
                           UINTN *data_size, VOID *data);
EFI_STATUS rs_set_variable(CHAR16 *name, VOID *vendor_guid, UINT32 attributes,
                           UINTN data_size, VOID *data);
EFI_STATUS rs_get_time(EFI_TIME *time, EFI_TIME_CAPABILITIES *capabilities);
EFI_STATUS rs_set_time(EFI_TIME *time);

void fw_boot_option_name(UINT16 option, CHAR16 name[9]);
UINTN fw_load_option_description_size(const UINT8 *option, UINTN option_size);
EFI_STATUS fw_boot_image_from_boot_option(UINT16 option_number);

UINTN fw_partition_count(VOID);
UINTN fw_processor_count(VOID);
UINT64 fw_installed_ram_size(VOID);
BOOLEAN fw_i2000_profile_enabled(void);
BOOLEAN fw_handoff_vga_console_primary(VOID);
UINT32 fw_graphics_width(VOID);
UINT32 fw_graphics_height(VOID);
const CHAR8 *fw_storage_description(BOOLEAN boot_device);
BOOLEAN fw_handoff_nvram_persistent(VOID);
void fw_reset_cold(VOID);

BOOLEAN fw_boot_shell_hotkey_window(VOID);
void fw_boot_shell_run(VOID);

#endif /* IA64_FIRMWARE_FW_BOOT_SHELL_H */
