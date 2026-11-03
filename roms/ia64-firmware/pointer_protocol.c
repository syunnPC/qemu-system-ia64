/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 Simple Pointer protocol for the auxiliary input port.
 */

#include "fw-device-path.h"
#include "fw-pointer.h"
#include "fw-services.h"

typedef struct {
    UINT64 ResolutionX;
    UINT64 ResolutionY;
    UINT64 ResolutionZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_MODE;

typedef struct {
    INT32 RelativeMovementX;
    INT32 RelativeMovementY;
    INT32 RelativeMovementZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct _EFI_SIMPLE_POINTER_PROTOCOL EFI_SIMPLE_POINTER_PROTOCOL;

struct _EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_POINTER_PROTOCOL *This,
                        BOOLEAN ExtendedVerification);
    EFI_STATUS (*GetState)(EFI_SIMPLE_POINTER_PROTOCOL *This,
                           EFI_SIMPLE_POINTER_STATE *State);
    EFI_EVENT WaitForInput;
    EFI_SIMPLE_POINTER_MODE *Mode;
};

typedef struct {
    FW_ACPI_HID_DEVICE_PATH_NODE Acpi;
    FW_DEVICE_PATH_NODE End;
} __attribute__((packed)) FW_POINTER_DEVICE_PATH;

#define PS2_CMD_MOUSE_ENABLE_PORT 0xa8U
#define PS2_CMD_WRITE_MOUSE       0xd4U
#define PS2_MOUSE_CMD_RESET       0xffU
#define PS2_MOUSE_CMD_DEFAULTS    0xf6U
#define PS2_MOUSE_CMD_DISABLE     0xf5U
#define PS2_MOUSE_CMD_ENABLE      0xf4U
#define PS2_MOUSE_CMD_RESOLUTION  0xe8U
#define PS2_MOUSE_ACK             0xfaU
#define PS2_MOUSE_BAT_OK          0xaaU
#define PS2_MOUSE_STANDARD_ID     0x00U
#define PS2_MOUSE_PACKET_SYNC     0x08U
#define PS2_MOUSE_X_SIGN          0x10U
#define PS2_MOUSE_Y_SIGN          0x20U
#define PS2_MOUSE_X_OVERFLOW      0x40U
#define PS2_MOUSE_Y_OVERFLOW      0x80U

static const UINT8 mSimplePointerProtocolGuid[16] = { 0x87, 0x8c, 0x87, 0x31,
                                                      0x75, 0x0b, 0xd5, 0x11,
                                                      0x9a, 0x4f, 0x00, 0x90,
                                                      0x27, 0x3f, 0xc1, 0x4d };

static EFI_SIMPLE_POINTER_PROTOCOL mSimplePointerProtocol;
static EFI_SIMPLE_POINTER_MODE mSimplePointerMode = {
    .ResolutionX = 4,
    .ResolutionY = 4,
    .ResolutionZ = 0,
    .LeftButton = 1,
    .RightButton = 1,
};
static EFI_SIMPLE_POINTER_STATE mSimplePointerState;
static FW_POINTER_DEVICE_PATH mSimplePointerDevicePath = {
    .Acpi = {
        .Header = { 0x02, 0x01, sizeof(FW_ACPI_HID_DEVICE_PATH_NODE) },
        .Hid = 0x0f1341d0U,
        .Uid = 0,
    },
    .End = { 0x7f, 0xff, sizeof(FW_DEVICE_PATH_NODE) },
};
static EFI_HANDLE mSimplePointerHandle;
static BOOLEAN mSimplePointerChanged;
static BOOLEAN mSimplePointerReady;
static UINT8 mSimplePointerPacket[3];
static UINT8 mSimplePointerPacketBytes;

static INT32 pointer_saturating_add(INT32 Left, INT32 Right)
{
    if (Right > 0 && Left > (INT32)0x7fffffff - Right) {
        return (INT32)0x7fffffff;
    }
    if (Right < 0 && Left < (INT32)0x80000000U - Right) {
        return (INT32)0x80000000U;
    }
    return Left + Right;
}

static VOID pointer_clear_accumulator(VOID)
{
    mSimplePointerState.RelativeMovementX = 0;
    mSimplePointerState.RelativeMovementY = 0;
    mSimplePointerState.RelativeMovementZ = 0;
    mSimplePointerChanged = 0;
}

VOID fw_pointer_consume_byte(UINT8 Byte)
{
    UINT8 flags;
    INT32 x;
    INT32 y;
    BOOLEAN left;
    BOOLEAN right;

    if (mSimplePointerPacketBytes == 0 && (Byte & PS2_MOUSE_PACKET_SYNC) == 0) {
        return;
    }
    mSimplePointerPacket[mSimplePointerPacketBytes++] = Byte;
    if (mSimplePointerPacketBytes != sizeof(mSimplePointerPacket)) {
        return;
    }
    mSimplePointerPacketBytes = 0;
    flags = mSimplePointerPacket[0];
    x = (INT8)mSimplePointerPacket[1];
    y = -(INT32)(INT8)mSimplePointerPacket[2];
    if ((flags & PS2_MOUSE_X_OVERFLOW) != 0) {
        x = (flags & PS2_MOUSE_X_SIGN) != 0 ? -255 : 255;
    }
    if ((flags & PS2_MOUSE_Y_OVERFLOW) != 0) {
        y = (flags & PS2_MOUSE_Y_SIGN) != 0 ? 255 : -255;
    }
    left = (flags & 1U) != 0;
    right = (flags & 2U) != 0;
    if (x != 0 || y != 0 || left != mSimplePointerState.LeftButton ||
        right != mSimplePointerState.RightButton) {
        mSimplePointerChanged = 1;
    }
    mSimplePointerState.RelativeMovementX =
        pointer_saturating_add(mSimplePointerState.RelativeMovementX, x);
    mSimplePointerState.RelativeMovementY =
        pointer_saturating_add(mSimplePointerState.RelativeMovementY, y);
    mSimplePointerState.LeftButton = left;
    mSimplePointerState.RightButton = right;
}

static VOID ps2_pointer_poll(VOID)
{
    UINTN count;

    if (!mSimplePointerReady) {
        return;
    }
    for (count = 0; count < 32U; count++) {
        UINT8 status = ps2_read_status();

        if ((status & PS2_STATUS_OBF) == 0 ||
            (status & PS2_STATUS_MOUSE_OBF) == 0) {
            return;
        }
        fw_pointer_consume_byte(*ps2_reg(PS2_DATA_PORT));
    }
}

static BOOLEAN ps2_mouse_wait_byte(UINT8 Expected)
{
    UINTN limit;

    for (limit = 0; limit < 1000000U; limit++) {
        UINT8 status = ps2_read_status();
        UINT8 data;

        if ((status & PS2_STATUS_OBF) == 0) {
            continue;
        }
        data = *ps2_reg(PS2_DATA_PORT);
        if ((status & PS2_STATUS_MOUSE_OBF) == 0) {
            (void)ps2_keyboard_raw_push(data);
            continue;
        }
        return data == Expected;
    }
    return 0;
}

static BOOLEAN ps2_mouse_send(UINT8 Command)
{
    return ps2_write_command(PS2_CMD_WRITE_MOUSE) && ps2_write_data(Command) &&
           ps2_mouse_wait_byte(PS2_MOUSE_ACK);
}

static BOOLEAN ps2_mouse_configure(BOOLEAN ExtendedVerification)
{
    mSimplePointerReady = 0;
    mSimplePointerPacketBytes = 0;
    pointer_clear_accumulator();
    mSimplePointerState.LeftButton = 0;
    mSimplePointerState.RightButton = 0;
    if (!fw_handoff_i8042_enabled() ||
        !ps2_write_command(PS2_CMD_MOUSE_ENABLE_PORT)) {
        return 0;
    }
    if (ExtendedVerification) {
        if (!ps2_mouse_send(PS2_MOUSE_CMD_RESET) ||
            !ps2_mouse_wait_byte(PS2_MOUSE_BAT_OK) ||
            !ps2_mouse_wait_byte(PS2_MOUSE_STANDARD_ID)) {
            return 0;
        }
    } else if (!ps2_mouse_send(PS2_MOUSE_CMD_DISABLE)) {
        return 0;
    }
    if (!ps2_mouse_send(PS2_MOUSE_CMD_DEFAULTS) ||
        !ps2_mouse_send(PS2_MOUSE_CMD_RESOLUTION) || !ps2_mouse_send(2U) ||
        !ps2_mouse_send(PS2_MOUSE_CMD_ENABLE)) {
        return 0;
    }
    mSimplePointerReady = 1;
    return 1;
}

static EFI_STATUS simple_pointer_reset(EFI_SIMPLE_POINTER_PROTOCOL *This,
                                       BOOLEAN ExtendedVerification)
{
    if (This != &mSimplePointerProtocol) {
        return EFI_INVALID_PARAMETER;
    }
    return ps2_mouse_configure(ExtendedVerification) ? EFI_SUCCESS
                                                     : EFI_DEVICE_ERROR;
}

static EFI_STATUS simple_pointer_get_state(EFI_SIMPLE_POINTER_PROTOCOL *This,
                                           EFI_SIMPLE_POINTER_STATE *State)
{
    if (This != &mSimplePointerProtocol || State == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    ps2_pointer_poll();
    if (!mSimplePointerReady) {
        return EFI_DEVICE_ERROR;
    }
    if (!mSimplePointerChanged) {
        return EFI_NOT_READY;
    }
    *State = mSimplePointerState;
    pointer_clear_accumulator();
    return EFI_SUCCESS;
}

static VOID simple_pointer_wait_notify(EFI_EVENT Event, VOID *Context)
{
    (void)Context;
    ps2_pointer_poll();
    if (mSimplePointerChanged) {
        (void)bs_signal_event(Event);
    }
}

BOOLEAN fw_pointer_install(VOID)
{
    EFI_STATUS status;

    if (!fw_handoff_i8042_enabled()) {
        return 1;
    }
    if (!ps2_mouse_configure(0)) {
        return 0;
    }
    mSimplePointerProtocol.Reset = simple_pointer_reset;
    mSimplePointerProtocol.GetState = simple_pointer_get_state;
    mSimplePointerProtocol.Mode = &mSimplePointerMode;
    status = bs_create_event(EVT_NOTIFY_WAIT, TPL_CALLBACK,
                             simple_pointer_wait_notify, NULL,
                             &mSimplePointerProtocol.WaitForInput);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    mSimplePointerHandle = NULL;
    status = bs_install_protocol(&mSimplePointerHandle,
                                 (VOID *)mSimplePointerProtocolGuid, 0,
                                 &mSimplePointerProtocol);
    if (status == EFI_SUCCESS) {
        status = bs_install_protocol(&mSimplePointerHandle,
                                     (VOID *)mDevicePathProtocolGuid, 0,
                                     &mSimplePointerDevicePath);
    }
    if (status != EFI_SUCCESS) {
        if (mSimplePointerHandle != NULL) {
            (void)bs_uninstall_protocol(mSimplePointerHandle,
                                        (VOID *)mSimplePointerProtocolGuid,
                                        &mSimplePointerProtocol);
        }
        mSimplePointerHandle = NULL;
        (void)bs_close_event(mSimplePointerProtocol.WaitForInput);
        mSimplePointerProtocol.WaitForInput = NULL;
        return 0;
    }
    return 1;
}
