/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_FIRMWARE_FW_UART_H
#define IA64_FIRMWARE_FW_UART_H

#define FW_SERIAL_DEVICE_PATH_MAX 64U

#include "fw-device-path.h"
#include "fw-uart-policy.h"

typedef enum {
    DefaultParity,
    NoParity,
    EvenParity,
    OddParity,
    MarkParity,
    SpaceParity
} EFI_PARITY_TYPE;

typedef enum {
    DefaultStopBits,
    OneStopBit,
    OneFiveStopBits,
    TwoStopBits
} EFI_STOP_BITS_TYPE;

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT32 Reserved;
    UINT64 BaudRate;
    UINT8 DataBits;
    UINT8 Parity;
    UINT8 StopBits;
} __attribute__((packed)) FW_UART_DEVICE_PATH_NODE;

typedef struct {
    FW_ACPI_HID_DEVICE_PATH_NODE Acpi;
    FW_UART_DEVICE_PATH_NODE Uart;
    FW_DEVICE_PATH_NODE End;
} __attribute__((packed)) FW_SERIAL_DEVICE_PATH;

typedef struct {
    FW_MEMORY_MAPPED_DEVICE_PATH_NODE Memory;
    FW_UART_DEVICE_PATH_NODE Uart;
    FW_DEVICE_PATH_NODE End;
} __attribute__((packed)) FW_MMIO_SERIAL_DEVICE_PATH;

#endif /* IA64_FIRMWARE_FW_UART_H */
