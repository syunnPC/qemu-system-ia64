/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 debug-port protocol for the optional platform debug UART.
 */

#include "fw-debug-port.h"
#include "fw-device-path.h"
#include "fw-services.h"
#include "fw-uart.h"

typedef struct _EFI_DEBUGPORT_PROTOCOL EFI_DEBUGPORT_PROTOCOL;

struct _EFI_DEBUGPORT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_DEBUGPORT_PROTOCOL *This);
    EFI_STATUS (*Write)(EFI_DEBUGPORT_PROTOCOL *This, UINT32 Timeout,
                        UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (*Read)(EFI_DEBUGPORT_PROTOCOL *This, UINT32 Timeout,
                       UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (*Poll)(EFI_DEBUGPORT_PROTOCOL *This);
};

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT8 Guid[16];
} __attribute__((packed)) FW_VENDOR_DEVICE_PATH_NODE;

typedef struct {
    FW_MEMORY_MAPPED_DEVICE_PATH_NODE Memory;
    FW_UART_DEVICE_PATH_NODE Uart;
    FW_VENDOR_DEVICE_PATH_NODE DebugPort;
    FW_DEVICE_PATH_NODE End;
} __attribute__((packed)) FW_DEBUGPORT_DEVICE_PATH;

static const UINT8 mDebugPortProtocolGuid[16] = { 0xd2, 0xe8, 0xa4, 0xeb,
                                                  0x58, 0x38, 0xec, 0x41,
                                                  0xa2, 0x81, 0x26, 0x47,
                                                  0xba, 0x96, 0x60, 0xd0 };

static EFI_DEBUGPORT_PROTOCOL mDebugPortProtocol;
static EFI_HANDLE mDebugPortHandle;
static UINT64 mDebugPortBase;
static FW_DEBUGPORT_DEVICE_PATH mDebugPortDevicePath;

#define DEBUG_UART_LSR_OVERRUN 0x02U
#define DEBUG_UART_LSR_ERROR   0x1eU

static volatile UINT8 *debug_uart_reg(UINTN Register)
{
    return (volatile UINT8 *)(UINTN)(mDebugPortBase + Register);
}

static BOOLEAN debug_port_valid(EFI_DEBUGPORT_PROTOCOL *This)
{
    return This == &mDebugPortProtocol && mDebugPortBase != 0;
}

static VOID debug_uart_configure(VOID)
{
    volatile UINT8 *ier = debug_uart_reg(UART_IER);
    volatile UINT8 *fcr = debug_uart_reg(UART_FCR);
    volatile UINT8 *lcr = debug_uart_reg(UART_LCR);
    volatile UINT8 *mcr = debug_uart_reg(UART_MCR);

    *ier = 0;
    *lcr = UART_LCR_DLAB;
    *debug_uart_reg(UART_THR) = 1;
    *ier = 0;
    *lcr = 0x03U;
    *fcr = 0x07U;
    *mcr = UART_MCR_DTR | UART_MCR_RTS;
}

static EFI_STATUS debug_port_reset(EFI_DEBUGPORT_PROTOCOL *This)
{
    if (!debug_port_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    debug_uart_configure();
    return EFI_SUCCESS;
}

static BOOLEAN debug_port_timed_out(UINT64 Start, UINT32 Timeout)
{
    UINT64 ticks = (UINT64)Timeout * FW_ITC_TICKS_PER_MICROSECOND;

    return fw_read_itc() - Start >= ticks;
}

static EFI_STATUS debug_port_write(EFI_DEBUGPORT_PROTOCOL *This, UINT32 Timeout,
                                   UINTN *BufferSize, VOID *Buffer)
{
    UINT8 *bytes = (UINT8 *)Buffer;
    UINTN requested;
    UINTN completed = 0;
    UINT64 start;

    if (!debug_port_valid(This) || BufferSize == NULL ||
        (Buffer == NULL && *BufferSize != 0)) {
        return EFI_INVALID_PARAMETER;
    }
    requested = *BufferSize;
    *BufferSize = 0;
    if (requested == 0) {
        return EFI_SUCCESS;
    }

    start = fw_read_itc();
    while (completed < requested) {
        UINT8 lsr = *debug_uart_reg(UART_LSR);

        if ((lsr & (DEBUG_UART_LSR_ERROR & ~DEBUG_UART_LSR_OVERRUN)) != 0) {
            *BufferSize = completed;
            return EFI_DEVICE_ERROR;
        }
        if ((lsr & UART_LSR_THRE) != 0) {
            *debug_uart_reg(UART_THR) = bytes[completed++];
            continue;
        }
        if (Timeout == 0 || debug_port_timed_out(start, Timeout)) {
            *BufferSize = completed;
            return EFI_TIMEOUT;
        }
    }
    *BufferSize = completed;
    return EFI_SUCCESS;
}

static EFI_STATUS debug_port_read(EFI_DEBUGPORT_PROTOCOL *This, UINT32 Timeout,
                                  UINTN *BufferSize, VOID *Buffer)
{
    UINT8 *bytes = (UINT8 *)Buffer;
    UINTN requested;
    UINTN completed = 0;
    UINT64 start;

    if (!debug_port_valid(This) || BufferSize == NULL ||
        (Buffer == NULL && *BufferSize != 0)) {
        return EFI_INVALID_PARAMETER;
    }
    requested = *BufferSize;
    *BufferSize = 0;
    if (requested == 0) {
        return EFI_SUCCESS;
    }

    start = fw_read_itc();
    while (completed < requested) {
        UINT8 lsr = *debug_uart_reg(UART_LSR);

        if ((lsr & DEBUG_UART_LSR_OVERRUN) != 0) {
            *BufferSize = completed;
            return EFI_TIMEOUT;
        }
        if ((lsr & (DEBUG_UART_LSR_ERROR & ~DEBUG_UART_LSR_OVERRUN)) != 0) {
            *BufferSize = completed;
            return EFI_DEVICE_ERROR;
        }
        if ((lsr & UART_LSR_DR) != 0) {
            bytes[completed++] = *debug_uart_reg(UART_RBR);
            continue;
        }
        if (Timeout == 0 || debug_port_timed_out(start, Timeout)) {
            *BufferSize = completed;
            return EFI_TIMEOUT;
        }
    }
    *BufferSize = completed;
    return EFI_SUCCESS;
}

static EFI_STATUS debug_port_poll(EFI_DEBUGPORT_PROTOCOL *This)
{
    UINT8 lsr;

    if (!debug_port_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    lsr = *debug_uart_reg(UART_LSR);
    if ((lsr & DEBUG_UART_LSR_ERROR) != 0) {
        return EFI_DEVICE_ERROR;
    }
    return (lsr & UART_LSR_DR) != 0 ? EFI_SUCCESS : EFI_NOT_READY;
}

static VOID debug_port_init_device_path(VOID)
{
    fw_set_mem(&mDebugPortDevicePath, sizeof(mDebugPortDevicePath), 0);
    mDebugPortDevicePath.Memory.Header.Type = 0x01;
    mDebugPortDevicePath.Memory.Header.SubType = 0x03;
    mDebugPortDevicePath.Memory.Header.Length =
        sizeof(mDebugPortDevicePath.Memory);
    mDebugPortDevicePath.Memory.MemoryType = EfiMemoryMappedIO;
    mDebugPortDevicePath.Memory.StartingAddress = mDebugPortBase;
    mDebugPortDevicePath.Memory.EndingAddress = mDebugPortBase + 7U;

    mDebugPortDevicePath.Uart.Header.Type = 0x03;
    mDebugPortDevicePath.Uart.Header.SubType = 0x0e;
    mDebugPortDevicePath.Uart.Header.Length = sizeof(mDebugPortDevicePath.Uart);
    mDebugPortDevicePath.Uart.BaudRate = 115200;
    mDebugPortDevicePath.Uart.DataBits = 8;
    mDebugPortDevicePath.Uart.Parity = NoParity;
    mDebugPortDevicePath.Uart.StopBits = OneStopBit;

    mDebugPortDevicePath.DebugPort.Header.Type = 0x03;
    mDebugPortDevicePath.DebugPort.Header.SubType = 0x0a;
    mDebugPortDevicePath.DebugPort.Header.Length =
        sizeof(mDebugPortDevicePath.DebugPort);
    fw_copy_mem(mDebugPortDevicePath.DebugPort.Guid, mDebugPortProtocolGuid,
                sizeof(mDebugPortProtocolGuid));
    mDebugPortDevicePath.End.Type = 0x7f;
    mDebugPortDevicePath.End.SubType = 0xff;
    mDebugPortDevicePath.End.Length = sizeof(FW_DEVICE_PATH_NODE);
}

BOOLEAN fw_debug_port_install(VOID)
{
    EFI_HANDLE handle = NULL;
    EFI_STATUS st;

    mDebugPortBase = fw_handoff_debug_port_base();
    if (mDebugPortBase == 0) {
        mDebugPortHandle = NULL;
        return 1;
    }

    mDebugPortProtocol.Reset = debug_port_reset;
    mDebugPortProtocol.Write = debug_port_write;
    mDebugPortProtocol.Read = debug_port_read;
    mDebugPortProtocol.Poll = debug_port_poll;
    debug_port_init_device_path();
    debug_uart_configure();

    st = bs_install_protocol(&handle, (VOID *)mDebugPortProtocolGuid, 0,
                             &mDebugPortProtocol);
    if (st != EFI_SUCCESS) {
        return 0;
    }
    st = bs_install_protocol(&handle, (VOID *)mDevicePathProtocolGuid, 0,
                             &mDebugPortDevicePath);
    if (st != EFI_SUCCESS) {
        (void)bs_uninstall_protocol(handle, (VOID *)mDebugPortProtocolGuid,
                                    &mDebugPortProtocol);
        return 0;
    }
    mDebugPortHandle = handle;
    return 1;
}

#undef DEBUG_UART_LSR_OVERRUN
#undef DEBUG_UART_LSR_ERROR
