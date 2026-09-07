/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_FIRMWARE_FW_DEVICE_PATH_H
#define IA64_FIRMWARE_FW_DEVICE_PATH_H

#include "fw-base.h"

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} __attribute__((packed)) FW_DEVICE_PATH_NODE;

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT32 Hid;
    UINT32 Uid;
} __attribute__((packed)) FW_ACPI_HID_DEVICE_PATH_NODE;

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT8 Function;
    UINT8 Device;
} __attribute__((packed)) FW_PCI_DEVICE_PATH_NODE;

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT32 MemoryType;
    EFI_PHYSICAL_ADDRESS StartingAddress;
    EFI_PHYSICAL_ADDRESS EndingAddress;
} __attribute__((packed)) FW_MEMORY_MAPPED_DEVICE_PATH_NODE;

#define FW_SAS_DEVICE_PATH_GUID_BYTES { \
    0xb4, 0xdd, 0x87, 0xd4, 0x8b, 0x00, 0xd9, 0x11, \
    0xaf, 0xdc, 0x00, 0x10, 0x83, 0xff, 0xca, 0x4d \
}

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT8 Guid[16];
    UINT32 Reserved;
    UINT64 SasAddress;
    UINT64 Lun;
    UINT16 DeviceTopology;
    UINT16 RelativeTargetPort;
} __attribute__((packed)) FW_SAS_DEVICE_PATH_NODE;

static inline VOID fw_sas_device_path_init(FW_SAS_DEVICE_PATH_NODE *Node,
                                           UINT64 Address, UINT64 Lun)
{
    *Node = (FW_SAS_DEVICE_PATH_NODE) {
        .Header = { 3, 10, sizeof(*Node) },
        .Guid = FW_SAS_DEVICE_PATH_GUID_BYTES,
        .SasAddress = Address,
        .Lun = Lun,
    };
}

#endif /* IA64_FIRMWARE_FW_DEVICE_PATH_H */
