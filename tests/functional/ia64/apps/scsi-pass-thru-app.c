/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

typedef struct {
    UINT64 Timeout;
    VOID *DataBuffer;
    VOID *SenseData;
    VOID *Cdb;
    UINT32 TransferLength;
    UINT8 CdbLength;
    UINT8 DataDirection;
    UINT8 HostAdapterStatus;
    UINT8 TargetStatus;
    UINT8 SenseDataLength;
} SCSI_PACKET;

typedef struct {
    CHAR16 *ControllerName;
    CHAR16 *ChannelName;
    UINT32 AdapterId;
    UINT32 Attributes;
    UINT32 IoAlign;
} SCSI_MODE;

typedef struct SCSI_PROTOCOL SCSI_PROTOCOL;
struct SCSI_PROTOCOL {
    SCSI_MODE *Mode;
    EFI_STATUS (*PassThru)(SCSI_PROTOCOL *, UINT32, UINT64, SCSI_PACKET *,
                           EFI_EVENT);
    EFI_STATUS (*GetNextDevice)(SCSI_PROTOCOL *, UINT32 *, UINT64 *);
    EFI_STATUS (*BuildDevicePath)(SCSI_PROTOCOL *, UINT32, UINT64, VOID **);
    EFI_STATUS (*GetTargetLun)(SCSI_PROTOCOL *, VOID *, UINT32 *, UINT64 *);
    EFI_STATUS (*ResetChannel)(SCSI_PROTOCOL *);
    EFI_STATUS (*ResetTarget)(SCSI_PROTOCOL *, UINT32, UINT64);
};

static UINT8 scsi_guid[16] = {
    0xcf, 0x8f, 0x9e, 0xa5, 0xa0, 0xbd, 0xbb, 0x43,
    0x90, 0xb1, 0xd3, 0x73, 0x2e, 0xca, 0xa8, 0x77,
};
static UINT8 loaded_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 path_guid[16] = IA64_GUID_DEVICE_PATH;

static UINT32 scratch_target(SCSI_PROTOCOL *Scsi)
{
    return Scsi->Mode->AdapterId == 0xffffffffU ? 6 : 8;
}

static BOOLEAN enumerate(SCSI_PROTOCOL *Scsi)
{
    UINT32 target = 0xffffffffU;
    UINT64 lun = 0;

    return Scsi->Mode != NULL && Scsi->Mode->ControllerName != NULL &&
        Scsi->Mode->ChannelName != NULL && Scsi->Mode->IoAlign == 1 &&
        (Scsi->Mode->Attributes & 3U) == 3U &&
        Scsi->GetNextDevice(Scsi, &target, &lun) == EFI_SUCCESS &&
        target == 0 && lun == 0 &&
        Scsi->GetNextDevice(Scsi, &target, &lun) == EFI_SUCCESS &&
        target == scratch_target(Scsi) && lun == 0 &&
        Scsi->GetNextDevice(Scsi, &target, &lun) == EFI_NOT_FOUND;
}

static BOOLEAN paths(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs,
                     SCSI_PROTOCOL *Scsi)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    SCSI_PROTOCOL *located = NULL;
    EFI_HANDLE controller = NULL;
    VOID *path = NULL;
    VOID *remaining;
    VOID *node = NULL;
    UINT32 target = 0xffffffffU;
    UINT64 lun = ~(UINT64)0;
    BOOLEAN ok;

    if (Bs->HandleProtocol(Image, loaded_guid, (VOID **)&loaded) !=
            EFI_SUCCESS || loaded == NULL ||
        Bs->HandleProtocol(loaded->DeviceHandle, path_guid, &path) !=
            EFI_SUCCESS || path == NULL) {
        return 0;
    }
    remaining = path;
    if (Bs->LocateDevicePath(scsi_guid, &remaining, &controller) !=
            EFI_SUCCESS || remaining == path ||
        Bs->HandleProtocol(controller, scsi_guid, (VOID **)&located) !=
            EFI_SUCCESS || located != Scsi ||
        Scsi->GetTargetLun(Scsi, remaining, &target, &lun) != EFI_SUCCESS ||
        target != 0 || lun != 0 ||
        Scsi->BuildDevicePath(Scsi, scratch_target(Scsi), 0, &node) !=
            EFI_SUCCESS ||
        node == NULL) {
        return 0;
    }
    ok = Scsi->GetTargetLun(Scsi, node, &target, &lun) == EFI_SUCCESS &&
        target == scratch_target(Scsi) && lun == 0;
    Bs->FreePool(node);
    return ok;
}

static BOOLEAN inquiry(SCSI_PROTOCOL *Scsi)
{
    UINT8 cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
    UINT8 data[128];
    SCSI_PACKET packet = {
        .Cdb = cdb, .CdbLength = sizeof(cdb),
        .DataBuffer = data, .TransferLength = sizeof(data),
    };
    UINTN i;

    for (i = 0; i < sizeof(data); i++) {
        data[i] = 0;
    }
    return Scsi->PassThru(Scsi, scratch_target(Scsi), 0, &packet, NULL) ==
            EFI_SUCCESS &&
        packet.HostAdapterStatus == 0 && packet.TargetStatus == 0 &&
        packet.TransferLength == 36 && data[0] == 0 && data[4] >= 31 &&
        Scsi->PassThru(Scsi, scratch_target(Scsi), 1, &packet, NULL) ==
            EFI_INVALID_PARAMETER;
}

static BOOLEAN capacity(SCSI_PROTOCOL *Scsi)
{
    UINT8 cdb[10] = { 0x25 };
    UINT8 data[8] = { 0 };
    SCSI_PACKET packet = {
        .Timeout = 10000000, .Cdb = cdb, .CdbLength = sizeof(cdb),
        .DataBuffer = data, .TransferLength = sizeof(data),
    };

    return Scsi->PassThru(Scsi, scratch_target(Scsi), 0, &packet, NULL) ==
            EFI_SUCCESS &&
        packet.HostAdapterStatus == 0 && packet.TargetStatus == 0 &&
        packet.TransferLength == sizeof(data) &&
        data[0] == 0 && data[1] == 0 && data[2] == 0x7f &&
        data[3] == 0xff && data[4] == 0 && data[5] == 0 &&
        data[6] == 2 && data[7] == 0;
}

static BOOLEAN sense(SCSI_PROTOCOL *Scsi)
{
    UINT8 cdb[16] = { 0xff };
    UINT8 data[32] = { 0 };
    SCSI_PACKET packet = {
        .Timeout = 10000000, .Cdb = cdb, .CdbLength = sizeof(cdb),
        .SenseData = data, .SenseDataLength = sizeof(data),
    };

    return Scsi->PassThru(Scsi, scratch_target(Scsi), 0, &packet, NULL) ==
            EFI_SUCCESS &&
        packet.HostAdapterStatus == 0 && packet.TargetStatus == 2 &&
        packet.TransferLength == 0 && packet.SenseDataLength >= 14 &&
        (data[0] & 0x7fU) == 0x70 && (data[2] & 15U) == 5 &&
        data[12] == 0x20 && data[13] == 0;
}

static BOOLEAN transfer(SCSI_PROTOCOL *Scsi, UINT8 *Data, BOOLEAN Write)
{
    UINT8 cdb[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };
    SCSI_PACKET packet = {
        .Timeout = 10000000, .Cdb = cdb, .CdbLength = sizeof(cdb),
        .DataBuffer = Data, .TransferLength = 512,
        .DataDirection = Write,
    };

    if (Write) {
        cdb[0] = 0x2a;
    }
    return Scsi->PassThru(Scsi, scratch_target(Scsi), 0, &packet, NULL) ==
            EFI_SUCCESS &&
        packet.HostAdapterStatus == 0 && packet.TargetStatus == 0 &&
        packet.TransferLength == 512;
}

static BOOLEAN write_read_restore(SCSI_PROTOCOL *Scsi)
{
    UINT8 original[512];
    UINT8 data[512];
    UINTN i;
    BOOLEAN ok;

    if (!transfer(Scsi, original, 0)) {
        return 0;
    }
    for (i = 0; i < sizeof(data); i++) {
        data[i] = (UINT8)(i * 37U + 19U);
    }
    ok = transfer(Scsi, data, 1);
    for (i = 0; i < sizeof(data); i++) {
        data[i] = 0;
    }
    ok = transfer(Scsi, data, 0) && ok;
    for (i = 0; i < sizeof(data); i++) {
        if (data[i] != (UINT8)(i * 37U + 19U)) {
            ok = 0;
        }
    }
    return transfer(Scsi, original, 1) && ok;
}

EFI_STATUS efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable, .Suite = "scsi-pass-thru",
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    SCSI_PROTOCOL *scsi = NULL;
    EFI_STATUS status = bs->LocateProtocol(scsi_guid, NULL, (VOID **)&scsi);
    BOOLEAN available = status == EFI_SUCCESS && scsi != NULL;

    ia64_test_check(&context, "enumerate", available && enumerate(scsi),
                    EFI_DEVICE_ERROR, "targets-mode");
    ia64_test_check(&context, "device-path",
                    available && paths(Image, bs, scsi),
                    EFI_DEVICE_ERROR, "locate-roundtrip");
    ia64_test_check(&context, "inquiry", available && inquiry(scsi),
                    EFI_DEVICE_ERROR, "underrun-zero-timeout");
    ia64_test_check(&context, "capacity", available && capacity(scsi),
                    EFI_DEVICE_ERROR, "capacity-block-size");
    ia64_test_check(&context, "autosense", available && sense(scsi),
                    EFI_DEVICE_ERROR, "invalid-command-sense");
    ia64_test_check(&context, "write-read-restore",
                    available && write_read_restore(scsi),
                    EFI_DEVICE_ERROR, "scratch-sector-roundtrip");
    ia64_test_check(&context, "reset", available &&
                    scsi->ResetTarget(scsi, scratch_target(scsi), 0) ==
                        EFI_SUCCESS &&
                    scsi->ResetChannel(scsi) == EFI_SUCCESS && inquiry(scsi),
                    EFI_DEVICE_ERROR, "reset-command-recovery");
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
