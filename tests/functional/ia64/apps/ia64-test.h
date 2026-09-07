/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal EFI 1.10 definitions shared by the IA-64 functional test apps. */

#ifndef IA64_FUNCTIONAL_TEST_APP_H
#define IA64_FUNCTIONAL_TEST_APP_H

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef signed short INT16;
typedef unsigned int UINT32;
typedef unsigned long UINT64;
typedef signed int INT32;
typedef unsigned long UINTN;
typedef signed long INTN;
typedef UINT16 CHAR16;
typedef UINT64 EFI_STATUS;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;
typedef void VOID;
typedef VOID *EFI_HANDLE;
typedef VOID *EFI_EVENT;
typedef UINT8 BOOLEAN;
typedef UINTN EFI_TPL;

EFI_STATUS test_call_virtual_procedure(
    const UINT64 *Descriptor, UINT64 TargetPsr, UINT64 Arg1, UINT64 Arg2,
    UINT64 Arg3, UINT64 Arg4, UINT64 Arg5);

#define NULL ((VOID *)0)
#define EFI_ERROR_BIT                 0x8000000000000000ULL
#define EFIERR(Value)                 (EFI_ERROR_BIT | (Value))
#define EFI_SUCCESS                   0
#define EFI_LOAD_ERROR                EFIERR(1)
#define EFI_INVALID_PARAMETER         EFIERR(2)
#define EFI_UNSUPPORTED               EFIERR(3)
#define EFI_BAD_BUFFER_SIZE           EFIERR(4)
#define EFI_BUFFER_TOO_SMALL          EFIERR(5)
#define EFI_NOT_READY                 EFIERR(6)
#define EFI_DEVICE_ERROR              EFIERR(7)
#define EFI_WRITE_PROTECTED           EFIERR(8)
#define EFI_OUT_OF_RESOURCES          EFIERR(9)
#define EFI_NOT_FOUND                 EFIERR(14)
#define EFI_ACCESS_DENIED             EFIERR(15)
#define EFI_TIMEOUT                   EFIERR(18)
#define EFI_ALREADY_STARTED           EFIERR(20)
#define EFI_ABORTED                   EFIERR(21)
#define EFI_ERROR(Status)             (((Status) & EFI_ERROR_BIT) != 0)

#define EFI_SYSTEM_TABLE_SIGNATURE    0x5453595320494249ULL
#define EFI_BOOT_SERVICES_SIGNATURE   0x56524553544f4f42ULL
#define EFI_RUNTIME_SERVICES_SIGNATURE 0x56524553544e5552ULL
#define EFI_PAGE_SIZE                 4096U
#define EFI_MEMORY_UC                 0x0000000000000001ULL
#define EFI_MEMORY_WB                 0x0000000000000008ULL
#define EFI_MEMORY_RUNTIME            0x8000000000000000ULL
#define EFI_OPTIONAL_PTR              0x0000000000000001ULL

#define IA64_TEST_UART_BASE           0x00000047f0000000ULL
#define IA64_TEST_UART_THR            0U
#define IA64_TEST_UART_LSR            5U
#define IA64_TEST_UART_LSR_THRE       0x20U

#define IA64_TEST_LEGACY_IO_BASE      0x00000ffffc000000ULL
#define IA64_TEST_LEGACY_IO_SIZE      0x0000000004000000ULL
#define IA64_TEST_IO_PORT_PA(Port) \
    (IA64_TEST_LEGACY_IO_BASE + \
     ((((UINT64)(Port) >> 2) << 12) | ((UINT64)(Port) & 0xfffULL)))

#define EfiLoaderCode                 1U
#define EfiLoaderData                 2U
#define EfiBootServicesCode           3U
#define EfiBootServicesData           4U
#define EfiRuntimeServicesCode        5U
#define EfiRuntimeServicesData        6U
#define EfiConventionalMemory         7U
#define EfiACPIReclaimMemory          9U
#define EfiACPIMemoryNVS              10U
#define EfiMemoryMappedIO             11U
#define EfiMemoryMappedIOPortSpace    12U
#define AllocateAnyPages              0U
#define AllocateMaxAddress            1U
#define AllocateAddress               2U

#define EVT_TIMER                     0x80000000U
#define EVT_NOTIFY_WAIT               0x00000100U
#define EVT_NOTIFY_SIGNAL             0x00000200U
#define TPL_APPLICATION               4U
#define TPL_CALLBACK                  8U
#define EFI_NATIVE_INTERFACE          0U
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x00000002U
#define EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER 0x00000008U
#define EFI_OPEN_PROTOCOL_BY_DRIVER   0x00000010U
#define EFI_LOCATE_BY_PROTOCOL         2U
#define EFI_PCI_IO_PASS_THROUGH_BAR   0xffU
#define EFI_PCI_ATTRIBUTE_IO          0x0100ULL
#define EFI_PCI_ATTRIBUTE_MEMORY      0x0200ULL
#define EFI_PCI_ATTRIBUTE_BUS_MASTER  0x0400ULL
#define EFI_FILE_MODE_READ             0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE            0x0000000000000002ULL
#define EFI_FILE_READ_ONLY             0x0000000000000001ULL
#define EFI_FILE_DIRECTORY             0x0000000000000010ULL
#define EFI_FILE_ARCHIVE               0x0000000000000020ULL
#define EFI_SHIFT_STATE_VALID          0x80000000U
#define EFI_RIGHT_SHIFT_PRESSED        0x00000001U
#define EFI_LEFT_SHIFT_PRESSED         0x00000002U

#define EFI_VARIABLE_NON_VOLATILE       0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS     0x00000004U

#define IA64_GUID_LOADED_IMAGE \
    { 0xa1, 0x31, 0x1b, 0x5b, 0x62, 0x95, 0xd2, 0x11, \
      0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_LOADED_IMAGE_DEVICE_PATH \
    { 0x7e, 0x15, 0x62, 0xbc, 0x33, 0x3e, 0xec, 0x4f, \
      0x99, 0x20, 0x2d, 0x3b, 0x36, 0xd7, 0x50, 0xdf }
#define IA64_GUID_HII_PACKAGE_LIST \
    { 0x63, 0xe7, 0x1e, 0x6a, 0x7a, 0xd4, 0xb4, 0x43, \
      0xaa, 0xbe, 0xef, 0x1d, 0xe2, 0xab, 0x56, 0xfc }
#define IA64_GUID_DEVICE_PATH \
    { 0x91, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_BLOCK_IO \
    { 0x21, 0x5b, 0x4e, 0x96, 0x59, 0x64, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_DISK_IO \
    { 0x71, 0x51, 0x34, 0xce, 0x0b, 0xba, 0xd2, 0x11, \
      0x8e, 0x4f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_SIMPLE_FILE_SYSTEM \
    { 0x22, 0x5b, 0x4e, 0x96, 0x59, 0x64, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_FILE_INFO \
    { 0x92, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_FILE_SYSTEM_INFO \
    { 0x93, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_FILE_SYSTEM_VOLUME_LABEL \
    { 0xd3, 0xd7, 0x47, 0xdb, 0x81, 0xfe, 0xd3, 0x11, \
      0x9a, 0x35, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
#define IA64_GUID_UNICODE_COLLATION \
    { 0x7f, 0xcd, 0x85, 0x1d, 0x3d, 0xf4, 0xd2, 0x11, \
      0x9a, 0x0c, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
#define IA64_GUID_DRIVER_BINDING \
    { 0xab, 0x31, 0xa0, 0x18, 0x43, 0xb4, 0x1a, 0x4d, \
      0xa5, 0xc0, 0x0c, 0x09, 0x26, 0x1e, 0x9f, 0x71 }
#define IA64_GUID_PLATFORM_DRIVER_OVERRIDE \
    { 0x38, 0xc7, 0x30, 0x6b, 0x91, 0xa3, 0xd4, 0x11, \
      0x9a, 0x3b, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
#define IA64_GUID_BUS_SPECIFIC_DRIVER_OVERRIDE \
    { 0x85, 0xb2, 0xc1, 0x3b, 0x15, 0x8a, 0x82, 0x4a, \
      0xaa, 0xbf, 0x4d, 0x7d, 0x13, 0xfb, 0x32, 0x65 }
#define IA64_GUID_DRIVER_FAMILY_OVERRIDE \
    { 0x9e, 0x12, 0xee, 0xb1, 0x36, 0xda, 0x81, 0x41, \
      0x91, 0xf8, 0x04, 0xa4, 0x92, 0x37, 0x66, 0xa7 }
#define IA64_GUID_LOAD_FILE \
    { 0x91, 0x30, 0xec, 0x56, 0x4c, 0x95, 0xd2, 0x11, \
      0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_LOAD_FILE2 \
    { 0xc1, 0xc0, 0x06, 0x40, 0xb3, 0xfc, 0x3e, 0x40, \
      0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d }
#define IA64_GUID_TEXT_INPUT_EX \
    { 0x34, 0x75, 0x9e, 0xdd, 0x62, 0x77, 0x98, 0x46, \
      0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa }
#define IA64_GUID_TEXT_OUTPUT \
    { 0xc2, 0x77, 0x74, 0x38, 0xc7, 0x69, 0xd2, 0x11, \
      0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
#define IA64_GUID_GOP \
    { 0xde, 0xa9, 0x42, 0x90, 0xdc, 0x23, 0x38, 0x4a, \
      0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
#define IA64_GUID_UGA_DRAW \
    { 0x8b, 0x29, 0x2c, 0x98, 0xfa, 0xf4, 0xcb, 0x41, \
      0xb8, 0x38, 0x77, 0xaa, 0x68, 0x8f, 0xb8, 0x39 }
#define IA64_GUID_UGA_IO \
    { 0x9e, 0xd4, 0xa4, 0x61, 0x68, 0x6f, 0x1b, 0x4f, \
      0xb9, 0x22, 0xa8, 0x6e, 0xed, 0x0b, 0x07, 0xa2 }
#define IA64_GUID_PCI_ROOT_IO \
    { 0xbb, 0x7e, 0x70, 0x2f, 0x1a, 0x4a, 0xd4, 0x11, \
      0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
#define IA64_GUID_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION \
    { 0xbe, 0x34, 0x80, 0xcf, 0x68, 0x67, 0x8b, 0x4d, \
      0xb7, 0x39, 0x7c, 0xce, 0x68, 0x3a, 0x9f, 0xbe }
#define IA64_GUID_PCI_IO \
    { 0x00, 0xb2, 0xf5, 0x4c, 0xb8, 0x68, 0xa5, 0x4c, \
      0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a }
#define IA64_GUID_TCG \
    { 0x6d, 0x79, 0x41, 0xf5, 0x2e, 0xa6, 0x54, 0x49, \
      0xa7, 0x75, 0x95, 0x84, 0xf6, 0x1b, 0x9c, 0xdd }
#define IA64_GUID_ACPI20 \
    { 0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11, \
      0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
#define IA64_GUID_SAL \
    { 0x32, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11, \
      0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
#define IA64_GUID_HCDP \
    { 0x8d, 0x93, 0x51, 0xf9, 0x0b, 0x62, 0xef, 0x42, \
      0x82, 0x79, 0xa8, 0x4b, 0x79, 0x61, 0x78, 0x98 }
#define IA64_GUID_SMBIOS \
    { 0x31, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11, \
      0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

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

typedef struct _EFI_SIMPLE_TEXT_OUT_PROTOCOL EFI_SIMPLE_TEXT_OUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_OUT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_OUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUT_PROTOCOL *, CHAR16 *);
    VOID *TestString;
    EFI_STATUS (*QueryMode)(EFI_SIMPLE_TEXT_OUT_PROTOCOL *, UINTN,
                            UINTN *, UINTN *);
    VOID *SetMode;
    VOID *SetAttribute;
    VOID *ClearScreen;
    VOID *SetCursorPosition;
    VOID *EnableCursor;
    VOID *Mode;
};

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *,
                                EFI_INPUT_KEY *);
    EFI_EVENT WaitForKey;
};

typedef struct {
    UINT32 KeyShiftState;
    UINT8 KeyToggleState;
} EFI_KEY_STATE;
typedef struct {
    EFI_INPUT_KEY Key;
    EFI_KEY_STATE KeyState;
} EFI_KEY_DATA;
typedef struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*ReadKeyStrokeEx)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *,
                                  EFI_KEY_DATA *);
    EFI_EVENT WaitForKeyEx;
    EFI_STATUS (*SetState)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *, UINT8 *);
    EFI_STATUS (*RegisterKeyNotify)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *,
                                    EFI_KEY_DATA *, VOID *, VOID **);
    EFI_STATUS (*UnregisterKeyNotify)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *,
                                      VOID *);
};

typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct _EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;
typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef EFI_STATUS (*EFI_PROTOCOLS_PER_HANDLE)(EFI_HANDLE, VOID ***, UINTN *);
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_UNICODE_COLLATION_PROTOCOL
    EFI_UNICODE_COLLATION_PROTOCOL;
typedef struct _EFI_DRIVER_BINDING_PROTOCOL EFI_DRIVER_BINDING_PROTOCOL;

struct _EFI_UNICODE_COLLATION_PROTOCOL {
    INTN (*StriColl)(EFI_UNICODE_COLLATION_PROTOCOL *, CHAR16 *, CHAR16 *);
    BOOLEAN (*MetaiMatch)(EFI_UNICODE_COLLATION_PROTOCOL *, CHAR16 *,
                          CHAR16 *);
    VOID (*StrLwr)(EFI_UNICODE_COLLATION_PROTOCOL *, CHAR16 *);
    VOID (*StrUpr)(EFI_UNICODE_COLLATION_PROTOCOL *, CHAR16 *);
    VOID (*FatToStr)(EFI_UNICODE_COLLATION_PROTOCOL *, UINTN, char *,
                     CHAR16 *);
    BOOLEAN (*StrToFat)(EFI_UNICODE_COLLATION_PROTOCOL *, CHAR16 *, UINTN,
                        char *);
    char *SupportedLanguages;
};

struct _EFI_DRIVER_BINDING_PROTOCOL {
    EFI_STATUS (*Supported)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE,
                            VOID *);
    EFI_STATUS (*Start)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, VOID *);
    EFI_STATUS (*Stop)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, UINTN,
                       EFI_HANDLE *);
    UINT32 Version;
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DriverBindingHandle;
};

typedef VOID (*EFI_EVENT_NOTIFY)(EFI_EVENT, VOID *);

struct _EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_TPL (*RaiseTPL)(EFI_TPL);
    VOID (*RestoreTPL)(EFI_TPL);
    EFI_STATUS (*AllocatePages)(UINTN, UINT32, UINTN,
                                EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (*GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *,
                               UINTN *, UINT32 *);
    EFI_STATUS (*AllocatePool)(UINT32, UINTN, VOID **);
    EFI_STATUS (*FreePool)(VOID *);
    EFI_STATUS (*CreateEvent)(UINT32, EFI_TPL, EFI_EVENT_NOTIFY, VOID *,
                              EFI_EVENT *);
    EFI_STATUS (*SetTimer)(EFI_EVENT, UINTN, UINT64);
    EFI_STATUS (*WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
    EFI_STATUS (*SignalEvent)(EFI_EVENT);
    EFI_STATUS (*CloseEvent)(EFI_EVENT);
    EFI_STATUS (*CheckEvent)(EFI_EVENT);
    EFI_STATUS (*InstallProtocolInterface)(EFI_HANDLE *, VOID *, UINTN,
                                            VOID *);
    VOID *ReinstallProtocolInterface;
    EFI_STATUS (*UninstallProtocolInterface)(EFI_HANDLE, VOID *, VOID *);
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, VOID *, VOID **);
    VOID *Reserved;
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    EFI_STATUS (*LocateDevicePath)(VOID *, VOID **, EFI_HANDLE *);
    VOID *InstallConfigurationTable;
    EFI_STATUS (*LoadImage)(BOOLEAN, EFI_HANDLE, VOID *, VOID *, UINTN,
                            EFI_HANDLE *);
    EFI_STATUS (*StartImage)(EFI_HANDLE, UINTN *, CHAR16 **);
    EFI_STATUS (*Exit)(EFI_HANDLE, EFI_STATUS, UINTN, CHAR16 *);
    EFI_STATUS (*UnloadImage)(EFI_HANDLE);
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
    EFI_STATUS (*GetNextMonotonicCount)(UINT64 *);
    EFI_STATUS (*Stall)(UINTN);
    EFI_STATUS (*SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16 *);
    EFI_STATUS (*ConnectController)(EFI_HANDLE, EFI_HANDLE *, VOID *,
                                    BOOLEAN);
    EFI_STATUS (*DisconnectController)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
    EFI_STATUS (*OpenProtocol)(EFI_HANDLE, VOID *, VOID **, EFI_HANDLE,
                               EFI_HANDLE, UINT32);
    EFI_STATUS (*CloseProtocol)(EFI_HANDLE, VOID *, EFI_HANDLE, EFI_HANDLE);
    VOID *OpenProtocolInformation;
    EFI_PROTOCOLS_PER_HANDLE ProtocolsPerHandle;
    EFI_STATUS (*LocateHandleBuffer)(UINTN, VOID *, VOID *, UINTN *,
                                     EFI_HANDLE **);
    EFI_STATUS (*LocateProtocol)(VOID *, VOID *, VOID **);
    EFI_STATUS (*InstallMultipleProtocolInterfaces)(EFI_HANDLE *, ...);
    EFI_STATUS (*UninstallMultipleProtocolInterfaces)(EFI_HANDLE, ...);
    EFI_STATUS (*CalculateCrc32)(VOID *, UINTN, UINT32 *);
    VOID (*CopyMem)(VOID *, VOID *, UINTN);
    VOID (*SetMem)(VOID *, UINTN, UINT8);
    VOID *CreateEventEx;
};

struct _EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (*GetTime)(EFI_TIME *, VOID *);
    EFI_STATUS (*SetTime)(EFI_TIME *);
    EFI_STATUS (*GetWakeupTime)(BOOLEAN *, BOOLEAN *, EFI_TIME *);
    EFI_STATUS (*SetWakeupTime)(BOOLEAN, EFI_TIME *);
    EFI_STATUS (*SetVirtualAddressMap)(UINTN, UINTN, UINT32,
                                       EFI_MEMORY_DESCRIPTOR *);
    EFI_STATUS (*ConvertPointer)(UINTN, VOID **);
    EFI_STATUS (*GetVariable)(CHAR16 *, VOID *, UINT32 *, UINTN *, VOID *);
    EFI_STATUS (*GetNextVariableName)(UINTN *, CHAR16 *, VOID *);
    EFI_STATUS (*SetVariable)(CHAR16 *, VOID *, UINT32, UINTN, VOID *);
    EFI_STATUS (*GetNextHighMonotonicCount)(UINT32 *);
    VOID (*ResetSystem)(UINTN, EFI_STATUS, UINTN, VOID *);
    EFI_STATUS (*QueryVariableInfo)(UINT32, UINT64 *, UINT64 *, UINT64 *);
};

typedef struct {
    UINT8 VendorGuid[16];
    UINTN VendorTable;
} EFI_CONFIGURATION_TABLE;

struct _EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
};

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    EFI_STATUS (*Unload)(EFI_HANDLE);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    UINT32 MediaId;
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition;
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
} EFI_BLOCK_IO_MEDIA;
typedef struct _EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct _EFI_BLOCK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_STATUS (*Reset)(EFI_BLOCK_IO_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN,
                             VOID *);
    EFI_STATUS (*WriteBlocks)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN,
                              VOID *);
    EFI_STATUS (*FlushBlocks)(EFI_BLOCK_IO_PROTOCOL *);
};
typedef struct _EFI_DISK_IO_PROTOCOL EFI_DISK_IO_PROTOCOL;
struct _EFI_DISK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*ReadDisk)(EFI_DISK_IO_PROTOCOL *, UINT32, UINT64, UINTN,
                           VOID *);
    EFI_STATUS (*WriteDisk)(EFI_DISK_IO_PROTOCOL *, UINT32, UINT64, UINTN,
                            VOID *);
};

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *,
                             EFI_FILE_PROTOCOL **);
};
struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *,
                       UINT64, UINT64);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Delete)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (*Write)(EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL *, UINT64 *);
    EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *, UINT64);
    EFI_STATUS (*GetInfo)(EFI_FILE_PROTOCOL *, VOID *, UINTN *, VOID *);
    EFI_STATUS (*SetInfo)(EFI_FILE_PROTOCOL *, VOID *, UINTN, VOID *);
    EFI_STATUS (*Flush)(EFI_FILE_PROTOCOL *);
};

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    UINT64 Size;
    BOOLEAN ReadOnly;
    UINT8 Reserved[7];
    UINT64 VolumeSize;
    UINT64 FreeSpace;
    UINT32 BlockSize;
    CHAR16 VolumeLabel[1];
} EFI_FILE_SYSTEM_INFO;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    UINT32 PixelInformation[4];
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;
typedef enum {
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
    EfiGraphicsOutputBltOperationMax,
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (*QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *,
                            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
    EFI_STATUS (*SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
    EFI_STATUS (*Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *,
                      EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN, UINTN,
                      UINTN, UINTN, UINTN, UINTN, UINTN);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

typedef struct _EFI_UGA_DRAW_PROTOCOL EFI_UGA_DRAW_PROTOCOL;
struct _EFI_UGA_DRAW_PROTOCOL {
    EFI_STATUS (*GetMode)(EFI_UGA_DRAW_PROTOCOL *, UINT32 *, UINT32 *,
                          UINT32 *, UINT32 *);
    EFI_STATUS (*SetMode)(EFI_UGA_DRAW_PROTOCOL *, UINT32, UINT32,
                          UINT32, UINT32);
    EFI_STATUS (*Blt)(EFI_UGA_DRAW_PROTOCOL *,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *,
                      EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN, UINTN,
                      UINTN, UINTN, UINTN, UINTN, UINTN);
};

typedef struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;

typedef enum {
    EfiPciWidthUint8,
    EfiPciWidthUint16,
    EfiPciWidthUint32,
    EfiPciWidthUint64,
    EfiPciWidthFifoUint8,
    EfiPciWidthFifoUint16,
    EfiPciWidthFifoUint32,
    EfiPciWidthFifoUint64,
    EfiPciWidthFillUint8,
    EfiPciWidthFillUint16,
    EfiPciWidthFillUint32,
    EfiPciWidthFillUint64,
    EfiPciWidthMaximum,
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH;

typedef enum {
    EfiPciOperationBusMasterRead,
    EfiPciOperationBusMasterWrite,
    EfiPciOperationBusMasterCommonBuffer,
    EfiPciOperationBusMasterRead64,
    EfiPciOperationBusMasterWrite64,
    EfiPciOperationBusMasterCommonBuffer64,
    EfiPciOperationMaximum,
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_OPERATION;

typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self,
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH width, UINT64 address,
    UINT64 mask, UINT64 value, UINT64 delay, UINT64 *result);

typedef EFI_STATUS (*EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM)(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *,
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH, UINT64, UINTN, VOID *);

typedef struct {
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM Read;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM Write;
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS;

typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_COPY_MEM(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self,
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH width, UINT64 dest_address,
    UINT64 src_address, UINTN count);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_MAP(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self,
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_OPERATION operation, VOID *host_address,
    UINTN *number_of_bytes, EFI_PHYSICAL_ADDRESS *device_address,
    VOID **mapping);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_UNMAP(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self, VOID *mapping);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ALLOCATE_BUFFER(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self, UINT32 type, UINT32 memory_type,
    UINTN pages, VOID **host_address, UINT64 attributes);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FREE_BUFFER(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self, UINTN pages, VOID *host_address);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FLUSH(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GET_ATTRIBUTES(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self, UINT64 *supports,
    UINT64 *attributes);
typedef EFI_STATUS EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_SET_ATTRIBUTES(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *self, UINT64 attributes,
    UINT64 *resource_base, UINT64 *resource_length);

struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL {
    EFI_HANDLE ParentHandle;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM *PollMem;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM *PollIo;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS Mem;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS Io;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS Pci;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_COPY_MEM *CopyMem;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_MAP *Map;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_UNMAP *Unmap;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ALLOCATE_BUFFER *AllocateBuffer;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FREE_BUFFER *FreeBuffer;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FLUSH *Flush;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GET_ATTRIBUTES *GetAttributes;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_SET_ATTRIBUTES *SetAttributes;
    EFI_STATUS (*Configuration)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *, VOID **);
    UINT32 SegmentNumber;
};

typedef struct _EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL;

typedef enum {
    EfiPciHostBridgeBeginEnumeration,
    EfiPciHostBridgeBeginBusAllocation,
    EfiPciHostBridgeEndBusAllocation,
    EfiPciHostBridgeBeginResourceAllocation,
    EfiPciHostBridgeAllocateResources,
    EfiPciHostBridgeSetResources,
    EfiPciHostBridgeFreeResources,
    EfiPciHostBridgeEndResourceAllocation,
    EfiPciHostBridgeEndEnumeration,
    EfiMaxPciHostBridgeEnumerationPhase,
} EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PHASE;

typedef enum {
    EfiPciBeforeChildBusEnumeration,
    EfiPciBeforeResourceCollection,
} EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE;

typedef struct {
    UINT8 Register;
    UINT8 Function;
    UINT8 Device;
    UINT8 Bus;
    UINT32 ExtendedRegister;
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS;

typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_NOTIFY_PHASE(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PHASE phase);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_GET_NEXT_ROOT_BRIDGE(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE *root_bridge_handle);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_GET_ATTRIBUTES(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle, UINT64 *attributes);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_START_BUS_ENUMERATION(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle, VOID **configuration);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_SET_BUS_NUMBERS(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle, VOID *configuration);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_SUBMIT_RESOURCES(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle, VOID *configuration);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_GET_PROPOSED_RESOURCES(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle, VOID **configuration);
typedef EFI_STATUS EFI_PCI_HOST_BRIDGE_PREPROCESS_CONTROLLER(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *self,
    EFI_HANDLE root_bridge_handle,
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS pci_address,
    EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE phase);

struct _EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL {
    EFI_PCI_HOST_BRIDGE_NOTIFY_PHASE *NotifyPhase;
    EFI_PCI_HOST_BRIDGE_GET_NEXT_ROOT_BRIDGE *GetNextRootBridge;
    EFI_PCI_HOST_BRIDGE_GET_ATTRIBUTES *GetAllocAttributes;
    EFI_PCI_HOST_BRIDGE_START_BUS_ENUMERATION *StartBusEnumeration;
    EFI_PCI_HOST_BRIDGE_SET_BUS_NUMBERS *SetBusNumbers;
    EFI_PCI_HOST_BRIDGE_SUBMIT_RESOURCES *SubmitResources;
    EFI_PCI_HOST_BRIDGE_GET_PROPOSED_RESOURCES *GetProposedResources;
    EFI_PCI_HOST_BRIDGE_PREPROCESS_CONTROLLER *PreprocessController;
};

typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_CONFIG)(
    EFI_PCI_IO_PROTOCOL *, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH,
    UINT32, UINTN, VOID *);

typedef struct {
    EFI_PCI_IO_PROTOCOL_CONFIG Read;
    EFI_PCI_IO_PROTOCOL_CONFIG Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_IO_MEM)(
    EFI_PCI_IO_PROTOCOL *, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH,
    UINT8, UINT64, UINTN, VOID *);

typedef struct {
    EFI_PCI_IO_PROTOCOL_IO_MEM Read;
    EFI_PCI_IO_PROTOCOL_IO_MEM Write;
} EFI_PCI_IO_PROTOCOL_IO_MEM_ACCESS;

typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_MAP)(
    EFI_PCI_IO_PROTOCOL *, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_OPERATION,
    VOID *, UINTN *, EFI_PHYSICAL_ADDRESS *, VOID **);
typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_UNMAP)(
    EFI_PCI_IO_PROTOCOL *, VOID *);
typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER)(
    EFI_PCI_IO_PROTOCOL *, UINT32, UINT32, UINTN, VOID **, UINT64);
typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_FREE_BUFFER)(
    EFI_PCI_IO_PROTOCOL *, UINTN, VOID *);
typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_FLUSH)(EFI_PCI_IO_PROTOCOL *);

typedef enum {
    EfiPciIoAttributeOperationGet,
    EfiPciIoAttributeOperationSet,
    EfiPciIoAttributeOperationEnable,
    EfiPciIoAttributeOperationDisable,
    EfiPciIoAttributeOperationSupported,
    EfiPciIoAttributeOperationMaximum,
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_ATTRIBUTES)(
    EFI_PCI_IO_PROTOCOL *, EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION,
    UINT64, UINT64 *);
typedef EFI_STATUS (*EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES)(
    EFI_PCI_IO_PROTOCOL *, UINT8, UINT64 *, VOID **);

struct _EFI_PCI_IO_PROTOCOL {
    VOID *PollMem;
    VOID *PollIo;
    EFI_PCI_IO_PROTOCOL_IO_MEM_ACCESS Mem;
    EFI_PCI_IO_PROTOCOL_IO_MEM_ACCESS Io;
    EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS Pci;
    VOID *CopyMem;
    EFI_PCI_IO_PROTOCOL_MAP Map;
    EFI_PCI_IO_PROTOCOL_UNMAP Unmap;
    EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER AllocateBuffer;
    EFI_PCI_IO_PROTOCOL_FREE_BUFFER FreeBuffer;
    EFI_PCI_IO_PROTOCOL_FLUSH Flush;
    EFI_STATUS (*GetLocation)(EFI_PCI_IO_PROTOCOL *, UINTN *, UINTN *,
                              UINTN *, UINTN *);
    EFI_PCI_IO_PROTOCOL_ATTRIBUTES Attributes;
    EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES GetBarAttributes;
    VOID *SetBarAttributes;
    UINT64 RomSize;
    VOID *RomImage;
};

typedef struct {
    UINT8 Major;
    UINT8 Minor;
    UINT8 RevMajor;
    UINT8 RevMinor;
} TCG_VERSION;
typedef struct {
    UINT8 Size;
    TCG_VERSION StructureVersion;
    TCG_VERSION ProtocolSpecVersion;
    UINT8 HashAlgorithmBitmap;
    BOOLEAN TPMPresentFlag;
    BOOLEAN TPMDeactivatedFlag;
} TCG_EFI_BOOT_SERVICE_CAPABILITY;
typedef struct _EFI_TCG_PROTOCOL EFI_TCG_PROTOCOL;
struct _EFI_TCG_PROTOCOL {
    EFI_STATUS (*StatusCheck)(EFI_TCG_PROTOCOL *,
                              TCG_EFI_BOOT_SERVICE_CAPABILITY *, UINT32 *,
                              EFI_PHYSICAL_ADDRESS *, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (*HashAll)(EFI_TCG_PROTOCOL *, UINT8 *, UINT64, UINT32,
                          UINT64 *, UINT8 **);
    VOID *LogEvent;
    EFI_STATUS (*PassThroughToTpm)(EFI_TCG_PROTOCOL *, UINT32, UINT8 *,
                                   UINT32, UINT8 *);
    VOID *HashLogExtendEvent;
};

typedef struct {
    EFI_SYSTEM_TABLE *SystemTable;
    const char *Suite;
    UINTN Passed;
    UINTN Failed;
    BOOLEAN DirectUart;
} IA64_TEST_CONTEXT;

static inline UINTN ia64_ascii_len(const char *String)
{
    UINTN length = 0;

    while (String[length] != 0) {
        length++;
    }
    return length;
}

static inline VOID ia64_copy(VOID *Destination, const VOID *Source,
                             UINTN Size)
{
    UINT8 *dst = (UINT8 *)Destination;
    const UINT8 *src = (const UINT8 *)Source;
    UINTN i;

    for (i = 0; i < Size; i++) {
        dst[i] = src[i];
    }
}

static inline BOOLEAN ia64_bytes_equal(const VOID *Left, const VOID *Right,
                                        UINTN Size)
{
    const UINT8 *left = (const UINT8 *)Left;
    const UINT8 *right = (const UINT8 *)Right;
    UINTN i;

    for (i = 0; i < Size; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static inline VOID ia64_uart_putc(char Character)
{
    volatile UINT8 *uart =
        (volatile UINT8 *)(UINTN)IA64_TEST_UART_BASE;

    if (Character == '\n') {
        while ((uart[IA64_TEST_UART_LSR] &
                IA64_TEST_UART_LSR_THRE) == 0) {
            __asm__ volatile ("hint @pause" ::: "memory");
        }
        uart[IA64_TEST_UART_THR] = '\r';
    }
    while ((uart[IA64_TEST_UART_LSR] &
            IA64_TEST_UART_LSR_THRE) == 0) {
        __asm__ volatile ("hint @pause" ::: "memory");
    }
    uart[IA64_TEST_UART_THR] = (UINT8)Character;
}

static inline VOID ia64_test_write(IA64_TEST_CONTEXT *Context,
                                   const char *String)
{
    if (Context->DirectUart || Context->SystemTable == NULL ||
        Context->SystemTable->ConOut == NULL) {
        while (*String != 0) {
            ia64_uart_putc(*String++);
        }
        return;
    }

    while (*String != 0) {
        CHAR16 buffer[80];
        UINTN count = 0;

        while (count + 1U < sizeof(buffer) / sizeof(buffer[0]) &&
               *String != 0) {
            if (*String == '\n') {
                buffer[count++] = '\r';
            }
            buffer[count++] = (UINT8)*String++;
        }
        buffer[count] = 0;
        Context->SystemTable->ConOut->OutputString(
            Context->SystemTable->ConOut, buffer);
    }
}

static inline VOID ia64_test_hex(IA64_TEST_CONTEXT *Context, UINT64 Value)
{
    static const char digits[] = "0123456789abcdef";
    char text[17];
    UINTN i;

    for (i = 0; i < 16; i++) {
        text[i] = digits[(Value >> ((15U - i) * 4U)) & 0xfU];
    }
    text[16] = 0;
    ia64_test_write(Context, text);
}

static inline VOID ia64_test_uint(IA64_TEST_CONTEXT *Context, UINTN Value)
{
    char text[24];
    UINTN offset = sizeof(text);

    text[--offset] = 0;
    do {
        text[--offset] = (char)('0' + Value % 10U);
        Value /= 10U;
    } while (Value != 0);
    ia64_test_write(Context, &text[offset]);
}

static inline VOID ia64_test_pass(IA64_TEST_CONTEXT *Context,
                                  const char *Case)
{
    ia64_test_write(Context, "IA64TEST suite=");
    ia64_test_write(Context, Context->Suite);
    ia64_test_write(Context, " case=");
    ia64_test_write(Context, Case);
    ia64_test_write(Context, " status=PASS\n");
    Context->Passed++;
}

static inline VOID ia64_test_fail(IA64_TEST_CONTEXT *Context,
                                  const char *Case, EFI_STATUS Code,
                                  const char *Detail)
{
    ia64_test_write(Context, "IA64TEST suite=");
    ia64_test_write(Context, Context->Suite);
    ia64_test_write(Context, " case=");
    ia64_test_write(Context, Case);
    ia64_test_write(Context, " status=FAIL code=");
    ia64_test_hex(Context, Code);
    ia64_test_write(Context, " detail=");
    ia64_test_write(Context, Detail);
    ia64_test_write(Context, "\n");
    Context->Failed++;
}

static inline VOID ia64_test_check(IA64_TEST_CONTEXT *Context,
                                   const char *Case, BOOLEAN Passed,
                                   EFI_STATUS Code, const char *Detail)
{
    if (Passed) {
        ia64_test_pass(Context, Case);
    } else {
        ia64_test_fail(Context, Case, Code, Detail);
    }
}

static inline VOID ia64_test_done(IA64_TEST_CONTEXT *Context)
{
    ia64_test_write(Context, "IA64TEST suite=");
    ia64_test_write(Context, Context->Suite);
    ia64_test_write(Context, " status=DONE passed=");
    ia64_test_uint(Context, Context->Passed);
    ia64_test_write(Context, " failed=");
    ia64_test_uint(Context, Context->Failed);
    ia64_test_write(Context, "\n");
}

static inline UINT8 ia64_checksum8(const VOID *Buffer, UINTN Size)
{
    const UINT8 *bytes = (const UINT8 *)Buffer;
    UINT8 sum = 0;
    UINTN i;

    for (i = 0; i < Size; i++) {
        sum = (UINT8)(sum + bytes[i]);
    }
    return sum;
}

#endif
