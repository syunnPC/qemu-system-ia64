/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * UART presentation policy.
 */

#ifndef IA64_FIRMWARE_FW_UART_POLICY_H
#define IA64_FIRMWARE_FW_UART_POLICY_H

#include "fw-base.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"
#include "hw/ia64/ia64_vpc_abi.h"

#define FW_UART_DEVICE_PATH_ACPI_PNP0501 1U
/* EFI device paths and ACPI/HCDP use different compressed-EISA byte orders. */
#define FW_UART_DEVICE_PATH_HID_PNP0501  0x050141d0U

#define FW_UART_HCDP_GAS_SYSTEM_IO        1U
#define FW_UART_HCDP_ACPI_HID_PNP0501     0x0105d041U
#define FW_UART_HCDP_FLAG_PRIMARY_CONSOLE (1U << 2)

typedef struct {
    UINT64 LegacyIoBase;
    UINT64 LegacyIoSize;
    UINT16 LogicalPort;
    UINT8 RegisterCount;
    UINT8 DevicePathKind;
    UINT32 DevicePathHid;
    UINT32 DevicePathUid;
    UINT8 HcdpSpaceId;
    UINT8 HcdpFlags;
    UINT16 HcdpConOutIndex;
    UINT64 HcdpAddress;
    UINT32 HcdpGlobalInterrupt;
    UINT32 HcdpAcpiHid;
} FW_UART_POLICY;

static inline BOOLEAN
fw_i2000_uart_policy_init(UINT64 LegacyIoBase, UINT64 LegacyIoSize,
                          UINT16 LogicalPort, UINT8 RegisterCount,
                          UINT32 ProfileFlags, FW_UART_POLICY *Policy)
{
    UINTN last_port;
    UINT64 last_offset;
    FW_UART_POLICY result = { 0 };

    if (Policy == NULL || LogicalPort != IA64_I2000_PROFILE_UART_PORT ||
        RegisterCount != IA64_I2000_PROFILE_UART_SIZE ||
        (ProfileFlags & IA64_I2000_PROFILE_FLAG_CONSOLE_POLL_ONLY) == 0 ||
        LegacyIoSize == 0 || LegacyIoBase > ~(UINT64)0 - LegacyIoSize ||
        LogicalPort > 0xffffU - (RegisterCount - 1U)) {
        return 0;
    }

    last_port = (UINTN)LogicalPort + RegisterCount - 1U;
    last_offset = IA64_LEGACY_IO_PORT_OFFSET(last_port);
    if (last_offset >= LegacyIoSize ||
        LegacyIoBase > ~(UINT64)0 - last_offset) {
        return 0;
    }

    result.LegacyIoBase = LegacyIoBase;
    result.LegacyIoSize = LegacyIoSize;
    result.LogicalPort = LogicalPort;
    result.RegisterCount = RegisterCount;
    result.DevicePathKind = FW_UART_DEVICE_PATH_ACPI_PNP0501;
    result.DevicePathHid = FW_UART_DEVICE_PATH_HID_PNP0501;
    result.DevicePathUid = 0;
    result.HcdpSpaceId = FW_UART_HCDP_GAS_SYSTEM_IO;
    result.HcdpFlags = FW_UART_HCDP_FLAG_PRIMARY_CONSOLE;
    result.HcdpConOutIndex = 0;
    result.HcdpAddress = LogicalPort;
    /* No interrupt is advertised; GSI is zero. */
    result.HcdpGlobalInterrupt = 0;
    result.HcdpAcpiHid = FW_UART_HCDP_ACPI_HID_PNP0501;
    *Policy = result;
    return 1;
}

static inline BOOLEAN fw_uart_policy_reg_address(const FW_UART_POLICY *Policy,
                                                 UINTN Register,
                                                 UINT64 *Address)
{
    UINTN logical_port;
    UINT64 offset;

    if (Policy == NULL || Address == NULL ||
        Register >= Policy->RegisterCount ||
        Policy->LogicalPort > 0xffffU - Register) {
        return 0;
    }

    logical_port = (UINTN)Policy->LogicalPort + Register;
    offset = IA64_LEGACY_IO_PORT_OFFSET(logical_port);
    if (offset >= Policy->LegacyIoSize ||
        Policy->LegacyIoBase > ~(UINT64)0 - offset) {
        return 0;
    }
    *Address = Policy->LegacyIoBase + offset;
    return 1;
}

#endif /* IA64_FIRMWARE_FW_UART_POLICY_H */
