/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Primitive types and status values shared by IA-64 firmware modules.
 *
 * This header deliberately has no hosted-library or QEMU dependencies: the
 * firmware is compiled with -nostdinc and every source file is an independent
 * freestanding translation unit.
 */

#ifndef IA64_FIRMWARE_FW_BASE_H
#define IA64_FIRMWARE_FW_BASE_H

typedef __UINT8_TYPE__ uint8_t;
typedef __INT8_TYPE__ int8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT64_TYPE__ int64_t;
typedef __SIZE_TYPE__ size_t;

#define NULL ((void *)0)
#define FW_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef uint8_t BOOLEAN;
typedef int8_t INT8;
typedef uint8_t UINT8;
typedef char CHAR8;
typedef int16_t INT16;
typedef uint16_t UINT16;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef int64_t INT64;
typedef uint64_t UINT64;
typedef int64_t INTN;
typedef uint64_t UINTN;
typedef INTN EFI_EXCEPTION_TYPE;
typedef uint16_t CHAR16;
typedef void VOID;

typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef void *EFI_HANDLE;
typedef UINTN EFI_STATUS;
typedef VOID *EFI_EVENT;
typedef VOID (*EFI_EVENT_NOTIFY)(EFI_EVENT event, VOID *context);
typedef UINTN EFI_TPL;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_ERROR_BIT            0x8000000000000000ULL
#define EFIERR(a)                (EFI_ERROR_BIT | (a))
#define EFI_SUCCESS              0
#define EFI_WARN_UNKNOWN_GLYPH   1
#define EFI_WARN_DELETE_FAILURE  2
#define EFI_LOAD_ERROR           EFIERR(1)
#define EFI_INVALID_PARAMETER    EFIERR(2)
#define EFI_UNSUPPORTED          EFIERR(3)
#define EFI_BAD_BUFFER_SIZE      EFIERR(4)
#define EFI_BUFFER_TOO_SMALL     EFIERR(5)
#define EFI_NOT_READY            EFIERR(6)
#define EFI_DEVICE_ERROR         EFIERR(7)
#define EFI_WRITE_PROTECTED      EFIERR(8)
#define EFI_OUT_OF_RESOURCES     EFIERR(9)
#define EFI_VOLUME_CORRUPTED     EFIERR(10)
#define EFI_VOLUME_FULL          EFIERR(11)
#define EFI_NO_MEDIA             EFIERR(12)
#define EFI_MEDIA_CHANGED        EFIERR(13)
#define EFI_NOT_FOUND            EFIERR(14)
#define EFI_ACCESS_DENIED        EFIERR(15)
#define EFI_NO_RESPONSE          EFIERR(16)
#define EFI_NO_MAPPING           EFIERR(17)
#define EFI_TIMEOUT              EFIERR(18)
#define EFI_NOT_STARTED          EFIERR(19)
#define EFI_ALREADY_STARTED      EFIERR(20)
#define EFI_ABORTED              EFIERR(21)
#define EFI_ICMP_ERROR           EFIERR(22)
#define EFI_TFTP_ERROR           EFIERR(23)
#define EFI_PROTOCOL_ERROR       EFIERR(24)

#define FW_STATIC_ASSERT(cond, name) \
    typedef char fw_static_assert_##name[(cond) ? 1 : -1]

/* Shared freestanding memory primitive implemented by firmware.c. */
void fw_set_mem(VOID *buffer, UINTN size, UINT8 value);

#endif /* IA64_FIRMWARE_FW_BASE_H */
