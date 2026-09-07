/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Legacy EFI I/O protocol module and platform adapter interface.
 */

#ifndef IA64_FIRMWARE_FW_LEGACY_IO_H
#define IA64_FIRMWARE_FW_LEGACY_IO_H

#include "fw-device-path.h"
#include "fw-services.h"

typedef enum {
    FwPciRootMemory,
    FwPciRootIo,
    FwPciRootConfiguration,
} FW_PCI_ROOT_SPACE;

typedef enum {
    FwLsiScriptSuccess,
    FwLsiScriptTargetStatus,
    FwLsiScriptSelectionTimeout,
    FwLsiScriptCommandTimeout,
    FwLsiScriptDeviceError,
} FW_LSI_SCRIPT_RESULT;

#define FW_SCSI_DEVICE_MAX  16U
#define FW_SCSI_HOST_ID     7U
#define FW_SCSI_CDB_MAX     16U
#define FW_SCSI_BOUNCE_SIZE (64U * 1024U)

EFI_STATUS fw_pci_root_read(FW_PCI_ROOT_SPACE space, UINTN width,
                            UINT64 address, UINTN count, VOID *buffer);
EFI_STATUS fw_pci_root_write(FW_PCI_ROOT_SPACE space, UINTN width,
                             UINT64 address, UINTN count, VOID *buffer);
EFI_STATUS fw_pci_root_map(UINTN operation, VOID *host_address,
                           UINTN *number_of_bytes,
                           EFI_PHYSICAL_ADDRESS *device_address,
                           VOID **mapping);
EFI_STATUS fw_pci_root_unmap(VOID *mapping);
EFI_STATUS fw_pci_root_allocate_buffer(EFI_ALLOCATE_TYPE type,
                                       EFI_MEMORY_TYPE memory_type,
                                       UINTN pages, VOID **host_address);
EFI_STATUS fw_pci_root_free_buffer(UINTN pages, VOID *host_address);
EFI_STATUS fw_pci_root_flush(VOID);
EFI_STATUS fw_pci_copy_device_path(UINT8 bus, UINT8 device, UINT8 function,
                                   FW_DEVICE_PATH_NODE **path);
EFI_HANDLE fw_pci_root_handle(VOID);

BOOLEAN fw_scsi_controller_present(VOID);
BOOLEAN fw_scsi_device_present(UINTN target);
BOOLEAN fw_scsi_target_valid(UINT32 target, UINT64 lun);
UINT32 fw_scsi_adapter_id(VOID);
UINT64 fw_scsi_sas_address(UINT32 target);
EFI_HANDLE fw_scsi_controller_handle(VOID);
FW_LSI_SCRIPT_RESULT fw_scsi_execute_buffered(
    UINT8 target, const UINT8 *cdb, UINTN cdb_length, VOID *data,
    UINT32 data_length, BOOLEAN write_to_device, UINT64 timeout_100ns,
    UINT8 *target_status, UINT32 *transferred, VOID *sense_data,
    UINT8 *sense_length);
EFI_STATUS fw_scsi_reset_channel(VOID);
FW_LSI_SCRIPT_RESULT fw_scsi_reset_target(UINT8 target,
                                           UINT64 timeout_100ns);

VOID *fw_serial_device_path(VOID);
UINTN fw_serial_device_path_size(VOID);
UINTN fw_platform_console_pci_path(UINT8 *Buffer, UINTN Capacity);

BOOLEAN fw_legacy_io_protocols_install(VOID);

#endif /* IA64_FIRMWARE_FW_LEGACY_IO_H */
