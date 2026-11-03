/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 UGA I/O protocol for the platform framebuffer.
 */

#include "fw-services.h"
#include "fw-uga-io.h"

typedef UINT32 UGA_STATUS;

typedef enum {
    UgaDtParentBus = 1,
    UgaDtGraphicsController,
    UgaDtOutputController,
    UgaDtOutputPort,
    UgaDtOther,
} UGA_DEVICE_TYPE;

typedef UINT32 UGA_DEVICE_ID;

typedef struct {
    UGA_DEVICE_TYPE deviceType;
    UGA_DEVICE_ID deviceId;
    UINT32 ui32DeviceContextSize;
    UINT32 ui32SharedContextSize;
} UGA_DEVICE_DATA;

typedef struct _UGA_DEVICE {
    VOID *pvDeviceContext;
    VOID *pvSharedContext;
    VOID *pvRunTimeContext;
    struct _UGA_DEVICE *pParentDevice;
    VOID *pvBusIoServices;
    VOID *pvStdIoServices;
    UGA_DEVICE_DATA deviceData;
} UGA_DEVICE;

typedef enum {
    UgaIoGetVersion = 1,
    UgaIoGetChildDevice,
    UgaIoStartDevice,
    UgaIoStopDevice,
    UgaIoFlushDevice,
    UgaIoResetDevice,
    UgaIoGetDeviceState,
    UgaIoSetDeviceState,
    UgaIoSetPowerState,
    UgaIoGetMemoryConfiguration,
    UgaIoSetVideoMode,
    UgaIoCopyRectangle,
    UgaIoGetEdidSegment,
    UgaIoDeviceChannelOpen,
    UgaIoDeviceChannelClose,
    UgaIoDeviceChannelRead,
    UgaIoDeviceChannelWrite,
    UgaIoGetPersistentDataSize,
    UgaIoGetPersistentData,
    UgaIoSetPersistentData,
    UgaIoGetDevicePropertySize,
    UgaIoGetDeviceProperty,
    UgaIoBtPrivateInterface,
} UGA_IO_REQUEST_CODE;

typedef struct {
    UGA_IO_REQUEST_CODE ioRequestCode;
    VOID *pvInBuffer;
    UINT64 ui64InBufferSize;
    VOID *pvOutBuffer;
    UINT64 ui64OutBufferSize;
    UINT64 ui64BytesReturned;
} UGA_IO_REQUEST;

typedef enum {
    UgaMtSystemToVideo = 1,
    UgaMtVideoToSystem,
    UgaMtVideoToVideo,
} UGA_MEMORY_TRANSFER_TYPE;

typedef struct {
    UGA_MEMORY_TRANSFER_TYPE transferType;
    union {
        struct {
            UINT64 ui64Source;
            UINT64 ui64Destination;
        } videoToVideo;
        struct {
            VOID *pvSource;
            UINT64 ui64Destination;
        } systemToVideo;
        struct {
            UINT64 ui64Source;
            VOID *pvDestination;
        } videoToSystem;
    } memory;
    INT32 i32Width;
    INT32 i32Height;
    INT32 i32SourceDelta;
    INT32 i32DestinationDelta;
    BOOLEAN bEndOfTransfer;
} UGA_MEMORY_TRANSFER;

typedef struct {
    UINT64 ui64VideoMemorySize;
    UINT64 ui64PrimaryOffset;
    UINT32 ui32PrimaryDelta;
    UINT64 ui64OffScreenOffset;
    UINT64 ui64OffScreenSize;
    UINT32 ui32OffScreenAlignment;
} UGA_MEMORY_CONFIGURATION;

typedef struct {
    UINT32 ui32HorizontalResolution;
    UINT32 ui32VerticalResolution;
    UINT32 ui32ColorDepth;
    UINT32 ui32RefreshRate;
} UGA_VIDEO_MODE;

typedef struct {
    UINT32 vmUgaSpecificationVersion;
    UINT32 vmVirtualMachineVersion;
    UINT32 fwUgaSpecificationVersion;
    UINT32 fwFirmwareVersion;
} UGA_VERSION;

typedef enum {
    UgaDsEnable = 1,
    UgaDsDisable,
    UgaDsNotAvailable,
    UgaDsDisabled,
    UgaDsEnabled,
    UgaDsActive,
} UGA_DEVICE_STATE;

typedef enum {
    UgaPrProbe = 1,
    UgaPrCommit,
    UgaPrCancel,
} UGA_POWER_REQUEST_TYPE;

typedef enum {
    UgaPsD0 = 1,
    UgaPsD1,
    UgaPsD2,
    UgaPsD3,
} UGA_POWER_STATE_DEVICE;

typedef enum {
    UgaPsS0 = 1,
    UgaPsS1,
    UgaPsS2,
    UgaPsS3,
    UgaPsS4,
    UgaPsS5,
} UGA_POWER_STATE_SYSTEM;

typedef struct {
    UGA_POWER_REQUEST_TYPE powerRequestType;
    UGA_POWER_STATE_DEVICE powerStateDevice;
    UGA_POWER_STATE_SYSTEM powerStateSystem;
} UGA_POWER_REQUEST;

typedef enum {
    UgaDpType = 1,
    UgaDpId,
    UgaDpAcpiId,
    UgaDpPnpId,
    UgaDpDescription,
    UgaDpManufacturer,
    UgaDpPrivateData = 0x10000,
} UGA_DEVICE_PROPERTY;

typedef struct _EFI_UGA_IO_PROTOCOL EFI_UGA_IO_PROTOCOL;

struct _EFI_UGA_IO_PROTOCOL {
    EFI_STATUS (*CreateDevice)(EFI_UGA_IO_PROTOCOL *This,
                               UGA_DEVICE *ParentDevice,
                               UGA_DEVICE_DATA *DeviceData,
                               VOID *RunTimeContext, UGA_DEVICE **Device);
    EFI_STATUS (*DeleteDevice)(EFI_UGA_IO_PROTOCOL *This, UGA_DEVICE *Device);
    UGA_STATUS (*DispatchService)(UGA_DEVICE *Device, UGA_IO_REQUEST *Request);
};

#define UGA_STATUS_SUCCESS             0x00000000U
#define UGA_STATUS_ERROR               0xc0000000U
#define UGA_STATUS_INVALID_DEVICE      (UGA_STATUS_ERROR | 0x01U)
#define UGA_STATUS_INVALID_MODE        (UGA_STATUS_ERROR | 0x02U)
#define UGA_STATUS_INVALID_FUNCTION    (UGA_STATUS_ERROR | 0x03U)
#define UGA_STATUS_UNSUPPORTED         (UGA_STATUS_ERROR | 0x04U)
#define UGA_STATUS_OPERATION_FAILED    (UGA_STATUS_ERROR | 0x05U)
#define UGA_STATUS_INSUFFICIENT_BUFFER (UGA_STATUS_ERROR | 0x07U)
#define UGA_STATUS_NO_MORE_DATA        (UGA_STATUS_ERROR | 0x08U)
#define UGA_STATUS_INVALID_PARAMETER   (UGA_STATUS_ERROR | 0x0aU)
#define UGA_STATUS_OUT_OF_RESOURCES    (UGA_STATUS_ERROR | 0x0bU)

#define FW_UGA_DEVICE_MAX 8U
#define FW_UGA_SPEC_VERSION 0x00000100U

typedef struct {
    BOOLEAN in_use;
    UGA_DEVICE *device;
    BOOLEAN started;
    UGA_DEVICE_STATE state;
    UGA_POWER_STATE_DEVICE power;
    BOOLEAN power_probe;
    UGA_POWER_STATE_DEVICE probed_power;
} FW_UGA_DEVICE_RECORD;

static const UINT8 mUgaIoProtocolGuid[16] = {
    0x9e, 0xd4, 0xa4, 0x61, 0x68, 0x6f, 0x1b, 0x4f,
    0xb9, 0x22, 0xa8, 0x6e, 0xed, 0x0b, 0x07, 0xa2,
};

static EFI_UGA_IO_PROTOCOL mUgaIoProtocol;
static FW_UGA_DEVICE_RECORD mUgaIoDevices[FW_UGA_DEVICE_MAX];
static UINT8 mUgaIoEdid[128];

static FW_UGA_DEVICE_RECORD *uga_io_record(UGA_DEVICE *Device)
{
    UINTN i;

    for (i = 0; i < FW_ARRAY_SIZE(mUgaIoDevices); i++) {
        if (mUgaIoDevices[i].in_use && mUgaIoDevices[i].device == Device) {
            return &mUgaIoDevices[i];
        }
    }
    return NULL;
}

static BOOLEAN uga_io_child_data(UGA_DEVICE *Parent, UINT32 Index,
                                 UGA_DEVICE_DATA *Data)
{
    if (Index != 0 || Data == NULL) {
        return 0;
    }
    fw_set_mem(Data, sizeof(*Data), 0);
    if (Parent == NULL) {
        Data->deviceType = UgaDtGraphicsController;
        Data->deviceId = 1;
    } else if (uga_io_record(Parent) != NULL &&
               Parent->deviceData.deviceType == UgaDtGraphicsController) {
        Data->deviceType = UgaDtOutputController;
        Data->deviceId = 2;
    } else {
        return 0;
    }
    Data->ui32DeviceContextSize = sizeof(UINT64);
    return 1;
}

static EFI_STATUS uga_io_create_device(EFI_UGA_IO_PROTOCOL *This,
                                       UGA_DEVICE *Parent,
                                       UGA_DEVICE_DATA *Data,
                                       VOID *RunTimeContext,
                                       UGA_DEVICE **Device)
{
    UGA_DEVICE_DATA expected = { 0 };
    UGA_DEVICE *created;
    UINTN size;
    UINTN i;
    EFI_STATUS status;

    if (This != &mUgaIoProtocol || Data == NULL || Device == NULL ||
        (Parent != NULL && uga_io_record(Parent) == NULL)) {
        return EFI_INVALID_PARAMETER;
    }
    *Device = NULL;
    if (!uga_io_child_data(Parent, 0, &expected) ||
        Data->deviceType != expected.deviceType ||
        Data->deviceId != expected.deviceId ||
        Data->ui32DeviceContextSize != expected.ui32DeviceContextSize ||
        Data->ui32SharedContextSize != expected.ui32SharedContextSize) {
        return EFI_INVALID_PARAMETER;
    }
    for (i = 0; i < FW_ARRAY_SIZE(mUgaIoDevices); i++) {
        if (!mUgaIoDevices[i].in_use) {
            break;
        }
    }
    if (i == FW_ARRAY_SIZE(mUgaIoDevices)) {
        return EFI_OUT_OF_RESOURCES;
    }
    size = sizeof(*created) + Data->ui32DeviceContextSize +
           Data->ui32SharedContextSize;
    status = bs_allocate_pool(EfiBootServicesData, size, (VOID **)&created);
    if (status != EFI_SUCCESS) {
        return EFI_OUT_OF_RESOURCES;
    }
    fw_set_mem(created, size, 0);
    created->pvDeviceContext = (UINT8 *)created + sizeof(*created);
    if (Data->ui32SharedContextSize != 0) {
        created->pvSharedContext =
            (UINT8 *)created->pvDeviceContext + Data->ui32DeviceContextSize;
    } else if (Parent != NULL) {
        created->pvSharedContext = Parent->pvSharedContext;
    }
    created->pvRunTimeContext = RunTimeContext;
    created->pParentDevice = Parent;
    created->deviceData = *Data;

    mUgaIoDevices[i].in_use = 1;
    mUgaIoDevices[i].device = created;
    mUgaIoDevices[i].state = UgaDsDisabled;
    mUgaIoDevices[i].power = UgaPsD3;
    *Device = created;
    return EFI_SUCCESS;
}

static EFI_STATUS uga_io_delete_device(EFI_UGA_IO_PROTOCOL *This,
                                       UGA_DEVICE *Device)
{
    FW_UGA_DEVICE_RECORD *record;
    UINTN i;

    if (This != &mUgaIoProtocol || Device == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    record = uga_io_record(Device);
    if (record == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    for (i = 0; i < FW_ARRAY_SIZE(mUgaIoDevices); i++) {
        if (mUgaIoDevices[i].in_use &&
            mUgaIoDevices[i].device->pParentDevice == Device) {
            return EFI_ACCESS_DENIED;
        }
    }
    if (bs_free_pool(Device) != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    fw_set_mem(record, sizeof(*record), 0);
    return EFI_SUCCESS;
}

static UGA_STATUS uga_io_output(UGA_IO_REQUEST *Request, const VOID *Data,
                                UINTN Size)
{
    Request->ui64BytesReturned = Size;
    if (Request->pvOutBuffer == NULL || Request->ui64OutBufferSize < Size) {
        return UGA_STATUS_INSUFFICIENT_BUFFER;
    }
    fw_copy_mem(Request->pvOutBuffer, Data, Size);
    return UGA_STATUS_SUCCESS;
}

static BOOLEAN uga_io_no_buffers(const UGA_IO_REQUEST *Request)
{
    return Request->pvInBuffer == NULL && Request->ui64InBufferSize == 0 &&
           Request->pvOutBuffer == NULL && Request->ui64OutBufferSize == 0;
}

static BOOLEAN uga_io_video_row(UINT64 Base, INT32 Delta, UINTN Row,
                                UINTN Width, UINT64 Limit, UINT64 *Address)
{
    UINT64 amount;
    UINT64 value;

    if (Delta >= 0) {
        amount = (UINT64)(UINT32)Delta * Row;
        if ((Row != 0 && amount / Row != (UINT32)Delta) ||
            Base > ~0ULL - amount) {
            return 0;
        }
        value = Base + amount;
    } else {
        amount = (UINT64)(-(INT64)Delta) * Row;
        if ((Row != 0 && amount / Row != (UINT64)(-(INT64)Delta)) ||
            Base < amount) {
            return 0;
        }
        value = Base - amount;
    }
    if (value > Limit || Width > Limit - value) {
        return 0;
    }
    *Address = value;
    return 1;
}

static UGA_STATUS uga_io_copy_rectangle(const UGA_MEMORY_TRANSFER *Transfer)
{
    UINT64 video_size = fw_graphics_bar_length();
    UINTN width;
    UINTN height;
    UINTN row;
    VOID *temporary = NULL;

    if (Transfer == NULL || Transfer->i32Width <= 0 ||
        Transfer->i32Height <= 0 ||
        Transfer->transferType < UgaMtSystemToVideo ||
        Transfer->transferType > UgaMtVideoToVideo) {
        return UGA_STATUS_INVALID_PARAMETER;
    }
    width = (UINTN)Transfer->i32Width;
    height = (UINTN)Transfer->i32Height;
    if (height > 1 && ((Transfer->i32SourceDelta >= 0 &&
                        (UINT32)Transfer->i32SourceDelta < width) ||
                       (Transfer->i32DestinationDelta >= 0 &&
                        (UINT32)Transfer->i32DestinationDelta < width))) {
        return UGA_STATUS_INVALID_PARAMETER;
    }

    if (Transfer->transferType == UgaMtVideoToVideo) {
        UINTN bytes;

        if (height > ~(UINTN)0 / width) {
            return UGA_STATUS_OUT_OF_RESOURCES;
        }
        bytes = width * height;
        if (bs_allocate_pool(EfiBootServicesData, bytes, &temporary) !=
            EFI_SUCCESS) {
            return UGA_STATUS_OUT_OF_RESOURCES;
        }
        for (row = 0; row < height; row++) {
            UINT64 source;

            if (!uga_io_video_row(Transfer->memory.videoToVideo.ui64Source,
                                  Transfer->i32SourceDelta, row, width,
                                  video_size, &source)) {
                (void)bs_free_pool(temporary);
                return UGA_STATUS_INVALID_PARAMETER;
            }
            fw_copy_mem(
                (UINT8 *)temporary + row * width,
                (VOID *)(UINTN)(fw_graphics_framebuffer_base() + source),
                width);
        }
        for (row = 0; row < height; row++) {
            UINT64 destination;

            if (!uga_io_video_row(Transfer->memory.videoToVideo.ui64Destination,
                                  Transfer->i32DestinationDelta, row, width,
                                  video_size, &destination)) {
                (void)bs_free_pool(temporary);
                return UGA_STATUS_INVALID_PARAMETER;
            }
            fw_copy_mem(
                (VOID *)(UINTN)(fw_graphics_framebuffer_base() + destination),
                (UINT8 *)temporary + row * width, width);
        }
        (void)bs_free_pool(temporary);
    } else if (Transfer->transferType == UgaMtSystemToVideo) {
        UINT64 source_base =
            (UINT64)(UINTN)Transfer->memory.systemToVideo.pvSource;

        if (source_base == 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        for (row = 0; row < height; row++) {
            UINT64 source;
            UINT64 destination;

            if (!uga_io_video_row(source_base, Transfer->i32SourceDelta, row,
                                  width, ~0ULL, &source) ||
                !uga_io_video_row(
                    Transfer->memory.systemToVideo.ui64Destination,
                    Transfer->i32DestinationDelta, row, width, video_size,
                    &destination)) {
                return UGA_STATUS_INVALID_PARAMETER;
            }
            fw_copy_mem(
                (VOID *)(UINTN)(fw_graphics_framebuffer_base() + destination),
                (VOID *)(UINTN)source, width);
        }
    } else {
        UINT64 destination_base =
            (UINT64)(UINTN)Transfer->memory.videoToSystem.pvDestination;

        if (destination_base == 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        for (row = 0; row < height; row++) {
            UINT64 source;
            UINT64 destination;

            if (!uga_io_video_row(Transfer->memory.videoToSystem.ui64Source,
                                  Transfer->i32SourceDelta, row, width,
                                  video_size, &source) ||
                !uga_io_video_row(destination_base,
                                  Transfer->i32DestinationDelta, row, width,
                                  ~0ULL, &destination)) {
                return UGA_STATUS_INVALID_PARAMETER;
            }
            fw_copy_mem(
                (VOID *)(UINTN)destination,
                (VOID *)(UINTN)(fw_graphics_framebuffer_base() + source),
                width);
        }
    }
    if (Transfer->bEndOfTransfer) {
        __asm__ volatile("mf;;" ::: "memory");
    }
    return UGA_STATUS_SUCCESS;
}

static const CHAR8 *uga_io_property_text(const UGA_DEVICE *Device,
                                         UGA_DEVICE_PROPERTY Property)
{
    if (Property == UgaDpManufacturer) {
        return "Virtual";
    }
    if (Property != UgaDpDescription) {
        return NULL;
    }
    return Device->deviceData.deviceType == UgaDtGraphicsController
               ? "Graphics controller"
               : "Display controller";
}

static UINTN uga_io_ascii_size(const CHAR8 *Text)
{
    UINTN size = 1;

    while (*Text++ != 0) {
        size++;
    }
    return size;
}

static UGA_STATUS uga_io_property(UGA_DEVICE *Device, UGA_IO_REQUEST *Request,
                                  BOOLEAN SizeOnly)
{
    UGA_DEVICE_PROPERTY property;
    const VOID *data;
    UINTN size;
    UINT32 value;
    const CHAR8 *text;

    if (Request->pvInBuffer == NULL ||
        Request->ui64InBufferSize != sizeof(property)) {
        return UGA_STATUS_INVALID_PARAMETER;
    }
    property = *(UGA_DEVICE_PROPERTY *)Request->pvInBuffer;
    if (property == UgaDpType) {
        value = Device->deviceData.deviceType;
        data = &value;
        size = sizeof(value);
    } else if (property == UgaDpId) {
        value = Device->deviceData.deviceId;
        data = &value;
        size = sizeof(value);
    } else {
        text = uga_io_property_text(Device, property);
        if (text == NULL) {
            return UGA_STATUS_UNSUPPORTED;
        }
        data = text;
        size = uga_io_ascii_size(text);
    }
    if (SizeOnly) {
        UINT64 size64 = size;

        return uga_io_output(Request, &size64, sizeof(size64));
    }
    return uga_io_output(Request, data, size);
}

static UGA_STATUS uga_io_dispatch(UGA_DEVICE *Device, UGA_IO_REQUEST *Request)
{
    FW_UGA_DEVICE_RECORD *record =
        Device != NULL ? uga_io_record(Device) : NULL;

    if (Request == NULL || Request->ioRequestCode < UgaIoGetVersion ||
        Request->ioRequestCode > UgaIoBtPrivateInterface ||
        (Device != NULL && record == NULL)) {
        return UGA_STATUS_INVALID_PARAMETER;
    }
    Request->ui64BytesReturned = 0;
    if (Request->ioRequestCode == UgaIoGetVersion) {
        UGA_VERSION version = {
            FW_UGA_SPEC_VERSION,
            FW_UGA_SPEC_VERSION,
            FW_UGA_SPEC_VERSION,
            FW_UGA_SPEC_VERSION,
        };

        return uga_io_output(Request, &version, sizeof(version));
    }
    if (Request->ioRequestCode == UgaIoGetChildDevice) {
        UGA_DEVICE_DATA child;
        UINT32 index;

        if (Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(index)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        index = *(UINT32 *)Request->pvInBuffer;
        if (!uga_io_child_data(Device, index, &child)) {
            return UGA_STATUS_NO_MORE_DATA;
        }
        return uga_io_output(Request, &child, sizeof(child));
    }
    if (Device == NULL) {
        return UGA_STATUS_INVALID_DEVICE;
    }

    switch (Request->ioRequestCode) {
    case UgaIoStartDevice:
        if (!uga_io_no_buffers(Request)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        record->started = 1;
        record->state = UgaDsActive;
        record->power = UgaPsD0;
        return UGA_STATUS_SUCCESS;
    case UgaIoStopDevice:
        if (!uga_io_no_buffers(Request)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        record->started = 0;
        record->state = UgaDsDisabled;
        record->power = UgaPsD3;
        return UGA_STATUS_SUCCESS;
    default:
        break;
    }
    if (!record->started) {
        return UGA_STATUS_INVALID_DEVICE;
    }

    switch (Request->ioRequestCode) {
    case UgaIoFlushDevice:
        if (!uga_io_no_buffers(Request)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        __asm__ volatile("mf;;" ::: "memory");
        return UGA_STATUS_SUCCESS;
    case UgaIoResetDevice:
        if (!uga_io_no_buffers(Request)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        return fw_graphics_reset_current_mode(1) == EFI_SUCCESS
                   ? UGA_STATUS_SUCCESS
                   : UGA_STATUS_OPERATION_FAILED;
    case UgaIoGetDeviceState:
        return uga_io_output(Request, &record->state, sizeof(record->state));
    case UgaIoSetDeviceState:
        if (Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(UGA_DEVICE_STATE) ||
            Request->pvOutBuffer != NULL || Request->ui64OutBufferSize != 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        if (*(UGA_DEVICE_STATE *)Request->pvInBuffer == UgaDsEnable) {
            record->state = UgaDsActive;
        } else if (*(UGA_DEVICE_STATE *)Request->pvInBuffer == UgaDsDisable) {
            record->state = UgaDsDisabled;
        } else {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        return UGA_STATUS_SUCCESS;
    case UgaIoSetPowerState:
        if (Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(UGA_POWER_REQUEST) ||
            Request->pvOutBuffer != NULL || Request->ui64OutBufferSize != 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        } else {
            UGA_POWER_REQUEST *power = (UGA_POWER_REQUEST *)Request->pvInBuffer;

            if (power->powerStateDevice < UgaPsD0 ||
                power->powerStateDevice > UgaPsD3 ||
                power->powerStateSystem < UgaPsS0 ||
                power->powerStateSystem > UgaPsS5) {
                return UGA_STATUS_INVALID_PARAMETER;
            }
            if (power->powerRequestType == UgaPrProbe) {
                record->power_probe = 1;
                record->probed_power = power->powerStateDevice;
            } else if (power->powerRequestType == UgaPrCommit) {
                record->power = record->power_probe ? record->probed_power
                                                    : power->powerStateDevice;
                record->power_probe = 0;
            } else if (power->powerRequestType == UgaPrCancel) {
                record->power_probe = 0;
            } else {
                return UGA_STATUS_INVALID_PARAMETER;
            }
        }
        return UGA_STATUS_SUCCESS;
    case UgaIoGetMemoryConfiguration:
        if (Device->deviceData.deviceType != UgaDtOutputController) {
            return UGA_STATUS_INVALID_FUNCTION;
        } else {
            UINT64 visible = fw_graphics_framebuffer_size();
            UINT64 total = fw_graphics_bar_length();
            UGA_MEMORY_CONFIGURATION configuration = {
                total,
                0,
                fw_graphics_pixels_per_scan_line() * 4U,
                visible,
                visible < total ? total - visible : 0,
                4,
            };

            return uga_io_output(Request, &configuration,
                                 sizeof(configuration));
        }
    case UgaIoSetVideoMode:
        if (Device->deviceData.deviceType != UgaDtOutputController ||
            Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(UGA_VIDEO_MODE) ||
            Request->pvOutBuffer != NULL || Request->ui64OutBufferSize != 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        } else {
            UGA_VIDEO_MODE *mode = (UGA_VIDEO_MODE *)Request->pvInBuffer;
            EFI_STATUS status = fw_graphics_set_uga_mode(
                mode->ui32HorizontalResolution, mode->ui32VerticalResolution,
                mode->ui32ColorDepth, mode->ui32RefreshRate);

            return status == EFI_SUCCESS       ? UGA_STATUS_SUCCESS
                   : status == EFI_UNSUPPORTED ? UGA_STATUS_INVALID_MODE
                                               : UGA_STATUS_OPERATION_FAILED;
        }
    case UgaIoCopyRectangle:
        if (Device->deviceData.deviceType != UgaDtOutputController ||
            Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(UGA_MEMORY_TRANSFER) ||
            Request->pvOutBuffer != NULL || Request->ui64OutBufferSize != 0) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        return uga_io_copy_rectangle(
            (UGA_MEMORY_TRANSFER *)Request->pvInBuffer);
    case UgaIoGetEdidSegment:
        if (Device->deviceData.deviceType != UgaDtOutputController ||
            Request->pvInBuffer == NULL ||
            Request->ui64InBufferSize != sizeof(UINT32)) {
            return UGA_STATUS_INVALID_PARAMETER;
        }
        if (*(UINT32 *)Request->pvInBuffer != 0) {
            return UGA_STATUS_NO_MORE_DATA;
        }
        return uga_io_output(Request, mUgaIoEdid, sizeof(mUgaIoEdid));
    case UgaIoGetPersistentDataSize: {
        UINT64 size = 0;

        return uga_io_output(Request, &size, sizeof(size));
    }
    case UgaIoGetPersistentData:
        return UGA_STATUS_SUCCESS;
    case UgaIoSetPersistentData:
        if (Request->ui64InBufferSize != 0) {
            return UGA_STATUS_UNSUPPORTED;
        } else {
            UINT64 written = 0;

            return uga_io_output(Request, &written, sizeof(written));
        }
    case UgaIoGetDevicePropertySize:
        return uga_io_property(Device, Request, 1);
    case UgaIoGetDeviceProperty:
        return uga_io_property(Device, Request, 0);
    case UgaIoDeviceChannelOpen:
    case UgaIoDeviceChannelClose:
    case UgaIoDeviceChannelRead:
    case UgaIoDeviceChannelWrite:
        return UGA_STATUS_UNSUPPORTED;
    case UgaIoBtPrivateInterface:
        return UGA_STATUS_UNSUPPORTED;
    default:
        return UGA_STATUS_INVALID_FUNCTION;
    }
}

static void uga_io_init_edid(VOID)
{
    static const UINT8 body[127] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x04, 0x21, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x20, 0x01, 0x03, 0x80, 0x20, 0x18, 0x78,
        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x08, 0x00, 0x00,
        0x31, 0x40, 0x45, 0x40, 0x61, 0x40, 0x81, 0x80, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0xa0, 0x0f, 0x20, 0x00, 0x31, 0x58, 0x1c, 0x20,
        0x28, 0x80, 0x14, 0x00, 0x40, 0xf0, 0x11, 0x00, 0x00, 0x1e, 0x00, 0x00,
        0x00, 0xfc, 0x00, 0x56, 0x69, 0x72, 0x74, 0x75, 0x61, 0x6c, 0x20, 0x44,
        0x69, 0x73, 0x70, 0x0a, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x3c, 0x3c, 0x1e,
        0x50, 0x0d, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    UINT8 sum = 0;
    UINTN i;

    fw_copy_mem(mUgaIoEdid, body, sizeof(body));
    for (i = 0; i < sizeof(body); i++) {
        sum = (UINT8)(sum + mUgaIoEdid[i]);
    }
    mUgaIoEdid[127] = (UINT8)(0U - sum);
}

BOOLEAN fw_uga_io_install(VOID)
{
    EFI_HANDLE handle = fw_graphics_handle();

    if (!fw_graphics_present()) {
        return 1;
    }
    mUgaIoProtocol.CreateDevice = uga_io_create_device;
    mUgaIoProtocol.DeleteDevice = uga_io_delete_device;
    mUgaIoProtocol.DispatchService = uga_io_dispatch;
    uga_io_init_edid();
    return bs_install_protocol(&handle, (VOID *)mUgaIoProtocolGuid, 0,
                               &mUgaIoProtocol) == EFI_SUCCESS;
}
