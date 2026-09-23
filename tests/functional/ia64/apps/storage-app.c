/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define MAX_TEST_BLOCKS 256U
#define EFI_NO_MEDIA EFIERR(12)
#define EFI_MEDIA_CHANGED EFIERR(13)

static UINT8 loaded_image_guid[16] = IA64_GUID_LOADED_IMAGE;
static UINT8 block_io_guid[16] = IA64_GUID_BLOCK_IO;
static UINT8 disk_io_guid[16] = IA64_GUID_DISK_IO;
static UINT8 simple_fs_guid[16] = IA64_GUID_SIMPLE_FILE_SYSTEM;
static UINT8 file_info_guid[16] = IA64_GUID_FILE_INFO;
static UINT8 file_system_info_guid[16] = IA64_GUID_FILE_SYSTEM_INFO;
static UINT8 volume_label_guid[16] = IA64_GUID_FILE_SYSTEM_VOLUME_LABEL;
static UINT8 unicode_collation_guid[16] = IA64_GUID_UNICODE_COLLATION;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 driver_binding_guid[16] = IA64_GUID_DRIVER_BINDING;
static CHAR16 boot_app_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', '\\',
    'B', 'O', 'O', 'T', 'I', 'A', '6', '4', '.', 'E', 'F', 'I', 0,
};
static CHAR16 short_form_app_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', '\\',
    'S', 'T', 'A', 'R', 'T', '.', 'E', 'F', 'I', 0,
};

static UINT8 pattern_byte(UINTN Index)
{
    return (UINT8)(((Index * 37U) + (Index >> 8) + 0x5aU) & 0xffU);
}

static UINT8 fixture_byte(UINTN Index)
{
    return (UINT8)((Index * 19U + 0x31U) & 0xffU);
}

static UINT16 get_u16(const UINT8 *Buffer)
{
    return (UINT16)Buffer[0] | ((UINT16)Buffer[1] << 8);
}

static UINT32 get_u32(const UINT8 *Buffer)
{
    return (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) |
           ((UINT32)Buffer[2] << 16) | ((UINT32)Buffer[3] << 24);
}

static UINT64 get_u64(const UINT8 *Buffer)
{
    return (UINT64)get_u32(Buffer) | ((UINT64)get_u32(Buffer + 4) << 32);
}

static BOOLEAN bytes_equal(const UINT8 *Left, const UINT8 *Right, UINTN Size)
{
    UINTN index;

    for (index = 0; index < Size; index++) {
        if (Left[index] != Right[index]) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN fat_boot_sector_valid(const UINT8 *Sector, UINTN SectorSize,
                                     UINT64 *VolumeBlocks)
{
    UINT32 blocks;

    if (SectorSize < 512U || Sector[0] != 0xebU || Sector[2] != 0x90U ||
        !bytes_equal(Sector + 3,
                                          (const UINT8 *)"QEMUIA64", 8) ||
        get_u16(Sector + 11) != 512U || Sector[13] == 0 ||
        Sector[16] == 0 || Sector[510] != 0x55U || Sector[511] != 0xaaU) {
        return 0;
    }
    blocks = get_u16(Sector + 19);
    if (blocks == 0) {
        blocks = get_u32(Sector + 32);
    }
    if (blocks == 0) {
        return 0;
    }
    *VolumeBlocks = blocks;
    return 1;
}

static BOOLEAN find_fat_test_region(EFI_BLOCK_IO_PROTOCOL *Block,
                                    EFI_BLOCK_IO_MEDIA *Media, UINT8 *Sector,
                                    UINT64 *StartLba, UINT64 *BlockCount)
{
    static const UINT8 esp_guid[16] = {
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
        0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
    };
    UINT64 volume_start = 0;
    UINT64 volume_blocks = 0;
    EFI_STATUS status;
    UINTN index;

    status = Block->ReadBlocks(Block, Media->MediaId, 0, Media->BlockSize,
                               Sector);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    if (!fat_boot_sector_valid(Sector, Media->BlockSize, &volume_blocks)) {
        UINTN selected = 4U;

        if (Media->BlockSize != 512U || Sector[510] != 0x55U ||
            Sector[511] != 0xaaU) {
            return 0;
        }
        for (index = 0; index < 4U; index++) {
            const UINT8 *entry = Sector + 446U + index * 16U;

            if (entry[4] == 0xefU) {
                selected = index;
                break;
            }
            if (selected == 4U && entry[4] != 0 && entry[4] != 0xeeU) {
                selected = index;
            }
        }
        if (Sector[446U + 4U] == 0xeeU) {
            UINT64 entries_lba;
            UINT32 entry_size;

            status = Block->ReadBlocks(Block, Media->MediaId, 1,
                                       Media->BlockSize, Sector);
            if (status != EFI_SUCCESS ||
                !bytes_equal(Sector, (const UINT8 *)"EFI PART", 8)) {
                return 0;
            }
            entries_lba = get_u64(Sector + 72);
            entry_size = get_u32(Sector + 84);
            if (entries_lba > Media->LastBlock || entry_size < 128U ||
                entry_size > Media->BlockSize) {
                return 0;
            }
            status = Block->ReadBlocks(Block, Media->MediaId, entries_lba,
                                       Media->BlockSize, Sector);
            if (status != EFI_SUCCESS ||
                !bytes_equal(Sector, esp_guid, sizeof(esp_guid))) {
                return 0;
            }
            volume_start = get_u64(Sector + 32);
            volume_blocks = get_u64(Sector + 40);
            if (volume_blocks < volume_start) {
                return 0;
            }
            volume_blocks = volume_blocks - volume_start + 1U;
        } else {
            const UINT8 *entry;

            if (selected == 4U) {
                return 0;
            }
            entry = Sector + 446U + selected * 16U;
            volume_start = get_u32(entry + 8);
            volume_blocks = get_u32(entry + 12);
        }

        if (volume_blocks == 0 || volume_start > Media->LastBlock ||
            volume_blocks - 1U > Media->LastBlock - volume_start) {
            return 0;
        }
        status = Block->ReadBlocks(Block, Media->MediaId, volume_start,
                                   Media->BlockSize, Sector);
        if (status != EFI_SUCCESS) {
            return 0;
        }
        {
            UINT64 fat_blocks;

            if (!fat_boot_sector_valid(Sector, Media->BlockSize,
                                       &fat_blocks) ||
                fat_blocks != volume_blocks) {
                return 0;
            }
        }
    }

    if (volume_blocks == 0 || volume_start > Media->LastBlock ||
        volume_blocks - 1U > Media->LastBlock - volume_start) {
        return 0;
    }
    *BlockCount = volume_blocks > MAX_TEST_BLOCKS ?
        MAX_TEST_BLOCKS : volume_blocks;
    *StartLba = volume_start + volume_blocks - *BlockCount;
    return 1;
}

static BOOLEAN original_fixture_data_valid(const UINT8 *Buffer, UINTN Size)
{
    UINTN index;

    for (index = 0; index < Size; index++) {
        if (Buffer[index] != fixture_byte(index)) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN probe_file_protocol_contracts(EFI_BOOT_SERVICES *bs,
                                             EFI_HANDLE loaded_handle);

static BOOLEAN probe_unicode_collation(EFI_BOOT_SERVICES *bs)
{
    EFI_UNICODE_COLLATION_PROTOCOL *collation = NULL;
    CHAR16 mixed_case[] = { 'B', 'o', 'O', 't', 0 };
    CHAR16 lower_case[] = { 'b', 'O', 'o', 'T', 0 };
    CHAR16 wildcard_name[] = {
        'B', 'O', 'O', 'T', 'I', 'A', '6', '4', '.', 'E', 'F', 'I', 0,
    };
    CHAR16 wildcard_pattern[] = { '*', '.', 'e', 'f', 'i', 0 };
    CHAR16 range_name[] = { 'D', '7', '.', 'f', 'w', 0 };
    CHAR16 range_pattern[] = {
        '[', 'a', '-', 'z', ']', '?', '.', '[', 'F', 'W', ']', 'w', 0,
    };
    CHAR16 empty_string[] = { 0 };
    CHAR16 repeated_star[] = { '*', '*', 0 };
    CHAR16 malformed_pattern[] = { '[', 'a', '-', 'z', 0 };
    CHAR16 latin_case[] = { 0x00c4, 0x00f6, 0 };
    char fat_source[] = { 'A', 'B', 0, 'C' };
    CHAR16 fat_string[5] = { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff };
    CHAR16 short_source[] = {
        'r', 'e', 'a', 'd', ' ', 'm', 'e', '.', 't', 'x', 't', 0,
    };
    CHAR16 long_source[] = { 'a', '+', 0x0100, 'b', 0 };
    char short_name[10];
    char long_name[5];

    if (bs->LocateProtocol(unicode_collation_guid, NULL,
                           (VOID **)&collation) != EFI_SUCCESS ||
        collation == NULL || collation->SupportedLanguages == NULL ||
        collation->SupportedLanguages[0] != 'e' ||
        collation->SupportedLanguages[1] != 'n' ||
        collation->SupportedLanguages[2] != 'g' ||
        collation->SupportedLanguages[3] != 0 ||
        collation->StriColl(collation, mixed_case, lower_case) != 0 ||
        !collation->MetaiMatch(collation, wildcard_name, wildcard_pattern) ||
        !collation->MetaiMatch(collation, range_name, range_pattern) ||
        !collation->MetaiMatch(collation, empty_string, repeated_star) ||
        collation->MetaiMatch(collation, range_name, malformed_pattern)) {
        return 0;
    }

    collation->StrLwr(collation, latin_case);
    if (latin_case[0] != 0x00e4 || latin_case[1] != 0x00f6) {
        return 0;
    }
    collation->StrUpr(collation, latin_case);
    if (latin_case[0] != 0x00c4 || latin_case[1] != 0x00d6) {
        return 0;
    }

    collation->FatToStr(collation, sizeof(fat_source), fat_source,
                        fat_string);
    if (fat_string[0] != 'A' || fat_string[1] != 'B' ||
        fat_string[2] != 0 || fat_string[3] != 0xffff) {
        return 0;
    }
    bs->SetMem(short_name, sizeof(short_name), 0x5a);
    if (collation->StrToFat(collation, short_source, sizeof(short_name),
                            short_name) ||
        short_name[0] != 'R' || short_name[1] != 'E' ||
        short_name[2] != 'A' || short_name[3] != 'D' ||
        short_name[4] != 'M' || short_name[5] != 'E' ||
        short_name[6] != 'T' || short_name[7] != 'X' ||
        short_name[8] != 'T' || short_name[9] != 0x5a) {
        return 0;
    }
    bs->SetMem(long_name, sizeof(long_name), 0x5a);
    if (!collation->StrToFat(collation, long_source, sizeof(long_name),
                             long_name) ||
        long_name[0] != 'A' || long_name[1] != '_' ||
        long_name[2] != '_' || long_name[3] != 'B' ||
        long_name[4] != 0x5a) {
        return 0;
    }
    return 1;
}

static BOOLEAN probe_empty_removable_media(EFI_BOOT_SERVICES *bs,
                                           EFI_HANDLE loaded_handle,
                                           BOOLEAN *Found)
{
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;
    UINTN index;
    BOOLEAN valid = 0;

    *Found = 0;
    if (bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, block_io_guid, NULL,
                               &handle_count, &handles) != EFI_SUCCESS ||
        handles == NULL) {
        return 0;
    }
    for (index = 0; index < handle_count; index++) {
        EFI_BLOCK_IO_PROTOCOL *block = NULL;
        EFI_DISK_IO_PROTOCOL *disk = NULL;
        UINT8 buffer[4096] __attribute__((aligned(8)));
        EFI_STATUS reset_status;
        EFI_STATUS read_status;
        EFI_STATUS write_status;
        EFI_STATUS flush_status;

        if (handles[index] == loaded_handle ||
            bs->HandleProtocol(handles[index], block_io_guid,
                               (VOID **)&block) != EFI_SUCCESS ||
            block == NULL || block->Media == NULL ||
            !block->Media->RemovableMedia || block->Media->MediaPresent ||
            block->Media->LogicalPartition) {
            continue;
        }
        *Found = 1;
        reset_status = block->Reset(block, 0);
        read_status = block->ReadBlocks(
            block, block->Media->MediaId, 0, block->Media->BlockSize, buffer);
        write_status = block->WriteBlocks(
            block, block->Media->MediaId, 0, block->Media->BlockSize, buffer);
        flush_status = block->FlushBlocks(block);
        valid = reset_status == EFI_SUCCESS &&
            !block->Media->MediaPresent &&
            block->Media->BlockSize > 0 &&
            block->Media->BlockSize <= sizeof(buffer) &&
            read_status == EFI_NO_MEDIA && write_status == EFI_NO_MEDIA &&
            flush_status == EFI_NO_MEDIA &&
            bs->HandleProtocol(handles[index], disk_io_guid,
                               (VOID **)&disk) == EFI_SUCCESS && disk != NULL;
        break;
    }
    (void)bs->FreePool(handles);
    return valid;
}

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} __attribute__((packed)) TEST_DEVICE_PATH_NODE;

typedef struct {
    TEST_DEVICE_PATH_NODE Header;
    UINT32 PartitionNumber;
    UINT64 PartitionStart;
    UINT64 PartitionSize;
    UINT8 PartitionSignature[16];
    UINT8 MbrType;
    UINT8 SignatureType;
} __attribute__((packed)) TEST_HARD_DRIVE_DEVICE_PATH_NODE;

static TEST_HARD_DRIVE_DEVICE_PATH_NODE *find_hard_drive_path_node(
    EFI_BOOT_SERVICES *bs, EFI_HANDLE handle)
{
    TEST_DEVICE_PATH_NODE *node = NULL;
    UINTN index;

    if (bs->HandleProtocol(handle, device_path_guid,
                           (VOID **)&node) != EFI_SUCCESS || node == NULL) {
        return NULL;
    }
    for (index = 0; index < 64U; index++) {
        if (node->Length < sizeof(*node)) {
            return NULL;
        }
        if (node->Type == 0x04U && node->SubType == 0x01U &&
            node->Length == sizeof(TEST_HARD_DRIVE_DEVICE_PATH_NODE)) {
            return (TEST_HARD_DRIVE_DEVICE_PATH_NODE *)node;
        }
        if (node->Type == 0x7fU && node->SubType == 0xffU) {
            return NULL;
        }
        node = (TEST_DEVICE_PATH_NODE *)((UINT8 *)node + node->Length);
    }
    return NULL;
}

static BOOLEAN probe_short_form_hard_drive_path(
    EFI_BOOT_SERVICES *bs, EFI_HANDLE image_handle,
    EFI_HANDLE loaded_handle, UINT32 block_size)
{
    static const UINT16 hard_drive_lengths[] = {
        sizeof(TEST_HARD_DRIVE_DEVICE_PATH_NODE), 48U,
    };
    UINT8 short_path[
        48U + sizeof(TEST_DEVICE_PATH_NODE) + sizeof(short_form_app_path) +
        sizeof(TEST_DEVICE_PATH_NODE)] __attribute__((aligned(8)));
    TEST_HARD_DRIVE_DEVICE_PATH_NODE *source;
    UINTN variant;

    source = find_hard_drive_path_node(bs, loaded_handle);
    if (source == NULL || block_size == 0 ||
        source->PartitionStart > ~0ULL / block_size ||
        source->PartitionSize > ~0ULL / block_size) {
        return 0;
    }

    for (variant = 0; variant < sizeof(hard_drive_lengths) /
                                     sizeof(hard_drive_lengths[0]);
         variant++) {
        TEST_HARD_DRIVE_DEVICE_PATH_NODE *hard_drive =
            (TEST_HARD_DRIVE_DEVICE_PATH_NODE *)short_path;
        TEST_DEVICE_PATH_NODE *file_header;
        TEST_DEVICE_PATH_NODE *end;
        EFI_LOADED_IMAGE_PROTOCOL *child_loaded = NULL;
        EFI_HANDLE located_handle = NULL;
        EFI_HANDLE child_image = NULL;
        CHAR16 *path_name;
        VOID *remaining;
        EFI_STATUS status;

        bs->SetMem(short_path, sizeof(short_path), 0x5a);
        bs->CopyMem(hard_drive, source, sizeof(*source));
        hard_drive->Header.Length = hard_drive_lengths[variant];
        /*
         * Short-form matching uses the signature and partition number.  Keep
         * deliberately byte-scaled geometry here to cover boot variables
         * whose otherwise valid partition identity carries non-LBA values.
         */
        hard_drive->PartitionStart *= block_size;
        hard_drive->PartitionSize *= block_size;
        file_header = (TEST_DEVICE_PATH_NODE *)(short_path +
                                                hard_drive->Header.Length);
        file_header->Type = 0x04U;
        file_header->SubType = 0x04U;
        file_header->Length = sizeof(*file_header) +
                              sizeof(short_form_app_path);
        path_name = (CHAR16 *)((UINT8 *)file_header + sizeof(*file_header));
        bs->CopyMem(path_name, short_form_app_path,
                    sizeof(short_form_app_path));
        end = (TEST_DEVICE_PATH_NODE *)((UINT8 *)file_header +
                                        file_header->Length);
        end->Type = 0x7fU;
        end->SubType = 0xffU;
        end->Length = sizeof(*end);

        remaining = hard_drive;
        status = bs->LocateDevicePath(simple_fs_guid, &remaining,
                                      &located_handle);
        if (status != EFI_SUCCESS || located_handle != loaded_handle ||
            remaining != file_header ||
            bs->LoadImage(1, image_handle, hard_drive,
                          NULL, 0, &child_image) != EFI_SUCCESS ||
            child_image == NULL ||
            bs->HandleProtocol(child_image, loaded_image_guid,
                               (VOID **)&child_loaded) != EFI_SUCCESS ||
            child_loaded == NULL ||
            child_loaded->DeviceHandle != loaded_handle ||
            child_loaded->FilePath == NULL ||
            bs->UnloadImage(child_image) != EFI_SUCCESS) {
            if (child_image != NULL) {
                (void)bs->UnloadImage(child_image);
            }
            return 0;
        }
    }
    return 1;
}

static BOOLEAN probe_partition_driver_contracts(
    EFI_BOOT_SERVICES *bs, EFI_HANDLE loaded_handle)
{
    struct {
        TEST_DEVICE_PATH_NODE Unsupported;
        TEST_DEVICE_PATH_NODE End;
    } __attribute__((packed, aligned(8))) unsupported_path = {
        { 0x01, 0x01, sizeof(TEST_DEVICE_PATH_NODE) },
        { 0x7f, 0xff, sizeof(TEST_DEVICE_PATH_NODE) },
    };
    TEST_DEVICE_PATH_NODE end_path __attribute__((aligned(8))) = {
        0x7f, 0xff, sizeof(TEST_DEVICE_PATH_NODE),
    };
    EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_BLOCK_IO_PROTOCOL *child_block = NULL;
    EFI_HANDLE *handles = NULL;
    EFI_HANDLE parent_handle = NULL;
    UINTN handle_count = 0;
    UINTN index;
    EFI_STATUS status;
    BOOLEAN valid = 0;

    if (bs->HandleProtocol(loaded_handle, block_io_guid,
                           (VOID **)&child_block) != EFI_SUCCESS ||
        child_block == NULL || child_block->Media == NULL ||
        !child_block->Media->LogicalPartition ||
        bs->HandleProtocol(loaded_handle, simple_fs_guid,
                           (VOID **)&fs) != EFI_SUCCESS || fs == NULL ||
        bs->LocateProtocol(driver_binding_guid, NULL,
                           (VOID **)&binding) != EFI_SUCCESS ||
        binding == NULL || binding->Supported == NULL ||
        binding->Start == NULL || binding->Stop == NULL ||
        bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, block_io_guid, NULL,
                               &handle_count, &handles) != EFI_SUCCESS ||
        handles == NULL) {
        goto out;
    }
    for (index = 0; index < handle_count; index++) {
        EFI_BLOCK_IO_PROTOCOL *candidate = NULL;
        VOID *candidate_path = NULL;

        if (handles[index] == loaded_handle ||
            bs->HandleProtocol(handles[index], block_io_guid,
                               (VOID **)&candidate) != EFI_SUCCESS ||
            candidate == NULL || candidate->Media == NULL ||
            candidate->Media->LogicalPartition ||
            !candidate->Media->MediaPresent ||
            candidate->Media->BlockSize != child_block->Media->BlockSize ||
            bs->HandleProtocol(handles[index], device_path_guid,
                               &candidate_path) != EFI_SUCCESS ||
            candidate_path == NULL) {
            continue;
        }
        parent_handle = handles[index];
        break;
    }
    if (parent_handle == NULL ||
        binding->Supported(binding, parent_handle, &unsupported_path) !=
            EFI_UNSUPPORTED ||
        binding->Start(binding, parent_handle, &unsupported_path) !=
            EFI_UNSUPPORTED ||
        bs->ConnectController(parent_handle, NULL, &end_path, 0) !=
            EFI_SUCCESS ||
        fs->OpenVolume(fs, &root) != EFI_SUCCESS || root == NULL) {
        goto out;
    }

    status = bs->DisconnectController(parent_handle, NULL, loaded_handle);
    if (!EFI_ERROR(status) ||
        root->SetPosition(root, 0) != EFI_SUCCESS ||
        bs->HandleProtocol(loaded_handle, block_io_guid,
                           (VOID **)&child_block) != EFI_SUCCESS ||
        child_block == NULL ||
        bs->HandleProtocol(loaded_handle, simple_fs_guid,
                           (VOID **)&fs) != EFI_SUCCESS || fs == NULL) {
        goto out;
    }
    valid = 1;

out:
    if (root != NULL) {
        (void)root->Close(root);
    }
    if (handles != NULL) {
        (void)bs->FreePool(handles);
    }
    return valid;
}

/*
 * The El Torito boot image has its own FAT SimpleFS handle.  A UDF bridge
 * additionally exposes the raw optical handle.  Opening the application from
 * a handle other than the loaded image's handle proves that the native UDF
 * directory tree, not merely the El Torito FAT image, was parsed correctly.
 */
static BOOLEAN probe_native_optical_filesystem(EFI_BOOT_SERVICES *bs,
                                                EFI_HANDLE loaded_handle)
{
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;
    UINTN index;
    EFI_STATUS status;
    BOOLEAN found = 0;

    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, simple_fs_guid,
                                    NULL, &handle_count, &handles);
    if (status != EFI_SUCCESS || handles == NULL) {
        return 0;
    }
    for (index = 0; index < handle_count; index++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
        EFI_BLOCK_IO_PROTOCOL *raw_block = NULL;
        EFI_FILE_PROTOCOL *root = NULL;
        EFI_FILE_PROTOCOL *file = NULL;
        UINT8 signature[2] = { 0, 0 };
        UINTN size = sizeof(signature);

        if (handles[index] == loaded_handle ||
            bs->HandleProtocol(handles[index], block_io_guid,
                               (VOID **)&raw_block) != EFI_SUCCESS ||
            raw_block == NULL || raw_block->Media == NULL ||
            !raw_block->Media->MediaPresent ||
            !raw_block->Media->RemovableMedia ||
            !raw_block->Media->ReadOnly ||
            raw_block->Media->LogicalPartition ||
            raw_block->Media->BlockSize != 2048U ||
            bs->HandleProtocol(handles[index], simple_fs_guid,
                               (VOID **)&fs) != EFI_SUCCESS ||
            fs == NULL || fs->OpenVolume(fs, &root) != EFI_SUCCESS ||
            root == NULL) {
            continue;
        }
        status = root->Open(root, &file, boot_app_path,
                            EFI_FILE_MODE_READ, 0);
        if (status == EFI_SUCCESS && file != NULL &&
            file->Read(file, &size, signature) == EFI_SUCCESS && size == 2 &&
            signature[0] == 'M' && signature[1] == 'Z' &&
            probe_file_protocol_contracts(bs, handles[index])) {
            found = 1;
        }
        if (file != NULL) {
            (void)file->Close(file);
        }
        (void)root->Close(root);
        if (found) {
            break;
        }
    }
    (void)bs->FreePool(handles);
    return found;
}

static BOOLEAN probe_loaded_filesystem(EFI_BOOT_SERVICES *bs,
                                       EFI_HANDLE loaded_handle)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    UINT8 signature[2] = { 0, 0 };
    UINTN size = sizeof(signature);
    EFI_STATUS status;
    BOOLEAN valid;

    status = bs->HandleProtocol(loaded_handle, simple_fs_guid,
                                (VOID **)&fs);
    if (status != EFI_SUCCESS || fs == NULL ||
        fs->OpenVolume(fs, &root) != EFI_SUCCESS || root == NULL) {
        return 0;
    }
    status = root->Open(root, &file, boot_app_path, EFI_FILE_MODE_READ, 0);
    valid = status == EFI_SUCCESS && file != NULL &&
        file->Read(file, &size, signature) == EFI_SUCCESS && size == 2 &&
        signature[0] == 'M' && signature[1] == 'Z';
    if (file != NULL) {
        (void)file->Close(file);
    }
    (void)root->Close(root);
    return valid;
}

static BOOLEAN loaded_file_exists(EFI_BOOT_SERVICES *bs,
                                  EFI_HANDLE loaded_handle,
                                  CHAR16 *path)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    BOOLEAN found = 0;

    if (bs->HandleProtocol(loaded_handle, simple_fs_guid,
                           (VOID **)&fs) == EFI_SUCCESS && fs != NULL &&
        fs->OpenVolume(fs, &root) == EFI_SUCCESS && root != NULL &&
        root->Open(root, &file, path, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS &&
        file != NULL) {
        found = 1;
    }
    if (file != NULL) {
        (void)file->Close(file);
    }
    if (root != NULL) {
        (void)root->Close(root);
    }
    return found;
}

static BOOLEAN probe_file_protocol_contracts(EFI_BOOT_SERVICES *bs,
                                             EFI_HANDLE loaded_handle)
{
    static UINT8 unknown_guid[16] = {
        0x46, 0x49, 0x4c, 0x45, 0x54, 0x45, 0x53, 0x54,
        0x9a, 0x64, 0x55, 0x24, 0x70, 0x33, 0x11, 0x03,
    };
    static CHAR16 directory_path[] = {
        '\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', 0,
    };
    static CHAR16 relative_path[] = {
        '.', '\\', 'B', 'O', 'O', 'T', 'I', 'A', '6', '4', '.',
        'E', 'F', 'I', 0,
    };
    static CHAR16 parent_path[] = {
        '.', '.', '\\', 'B', 'O', 'O', 'T', '\\', 'B', 'O', 'O', 'T',
        'I', 'A', '6', '4', '.', 'E', 'F', 'I', 0,
    };
    static CHAR16 root_path[] = { '\\', 0 };
    static CHAR16 root_parent[] = { '.', '.', 0 };
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *second_root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_PROTOCOL *directory = NULL;
    EFI_FILE_PROTOCOL *relative = NULL;
    EFI_FILE_PROTOCOL *root_copy = NULL;
    EFI_FILE_PROTOCOL *write_file = (EFI_FILE_PROTOCOL *)(UINTN)1;
    EFI_FILE_SYSTEM_INFO *system_info = NULL;
    EFI_FILE_INFO *file_info = NULL;
    VOID *label = NULL;
    UINT8 byte = 0;
    UINT64 position;
    UINTN size;
    EFI_STATUS status;
    BOOLEAN valid = 0;

    if (bs->HandleProtocol(loaded_handle, simple_fs_guid,
                           (VOID **)&fs) != EFI_SUCCESS || fs == NULL ||
        fs->OpenVolume(fs, &root) != EFI_SUCCESS || root == NULL ||
        fs->OpenVolume(fs, &second_root) != EFI_SUCCESS ||
        second_root == NULL || second_root == root ||
        second_root->Close(second_root) != EFI_SUCCESS ||
        root->SetPosition(root, 0) != EFI_SUCCESS ||
        root->GetPosition(root, &position) != EFI_UNSUPPORTED ||
        root->SetPosition(root, 1) != EFI_UNSUPPORTED ||
        root->SetPosition(root, 0) != EFI_SUCCESS) {
        goto out;
    }
    second_root = NULL;
    if (root->Open(root, &directory, directory_path,
                   EFI_FILE_MODE_READ, 0) != EFI_SUCCESS ||
        directory == NULL ||
        directory->Open(directory, &relative, relative_path,
                        EFI_FILE_MODE_READ, 0) != EFI_SUCCESS ||
        relative == NULL || relative->Close(relative) != EFI_SUCCESS) {
        goto out;
    }
    relative = NULL;
    if (directory->Open(directory, &relative, parent_path,
                        EFI_FILE_MODE_READ, 0) != EFI_SUCCESS ||
        relative == NULL || relative->Close(relative) != EFI_SUCCESS ||
        root->Open(root, &root_copy, root_path,
                   EFI_FILE_MODE_READ, 0) != EFI_SUCCESS ||
        root_copy == NULL ||
        root_copy->GetPosition(root_copy, &position) != EFI_UNSUPPORTED) {
        goto out;
    }
    relative = (EFI_FILE_PROTOCOL *)(UINTN)1;
    if (root->Open(root, &relative, root_parent,
                   EFI_FILE_MODE_READ, 0) != EFI_NOT_FOUND ||
        relative != NULL) {
        goto out;
    }

    size = 0;
    status = root->GetInfo(root, file_system_info_guid, &size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || size < sizeof(*system_info) - 6U ||
        bs->AllocatePool(EfiLoaderData, size,
                         (VOID **)&system_info) != EFI_SUCCESS ||
        root->GetInfo(root, file_system_info_guid, &size,
                      system_info) != EFI_SUCCESS ||
        system_info->Size != size || !system_info->ReadOnly ||
        system_info->FreeSpace != 0 || system_info->BlockSize == 0) {
        goto out;
    }

    size = 0;
    status = root->GetInfo(root, volume_label_guid, &size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || size < sizeof(CHAR16) ||
        bs->AllocatePool(EfiLoaderData, size, &label) != EFI_SUCCESS ||
        root->GetInfo(root, volume_label_guid, &size, label) != EFI_SUCCESS ||
        ((CHAR16 *)label)[0] == 0) {
        goto out;
    }
    for (size = 0; ((CHAR16 *)label)[size] != 0; size++) {
        if (((CHAR16 *)label)[size] != system_info->VolumeLabel[size]) {
            goto out;
        }
    }
    if (system_info->VolumeLabel[size] != 0) {
        goto out;
    }
    status = root->Open(root, &write_file, boot_app_path,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (status != EFI_WRITE_PROTECTED || write_file != NULL ||
        root->Open(root, &file, boot_app_path,
                   EFI_FILE_MODE_READ, EFI_FILE_ARCHIVE) != EFI_SUCCESS ||
        file == NULL ||
        file->GetPosition(file, &position) != EFI_SUCCESS || position != 0) {
        goto out;
    }

    size = 0;
    status = file->GetInfo(file, file_info_guid, &size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL ||
        bs->AllocatePool(EfiLoaderData, size,
                         (VOID **)&file_info) != EFI_SUCCESS ||
        file->GetInfo(file, file_info_guid, &size, file_info) != EFI_SUCCESS ||
        file_info->Size != size || file_info->FileSize == 0 ||
        file_info->PhysicalSize < file_info->FileSize ||
        (file_info->Attribute & EFI_FILE_READ_ONLY) == 0 ||
        file_info->FileName[0] != 'B' ||
        file->GetInfo(file, unknown_guid, &size, file_info) !=
            EFI_UNSUPPORTED ||
        file->SetInfo(file, file_info_guid, size, file_info) !=
            EFI_WRITE_PROTECTED) {
        goto out;
    }
    size = sizeof(byte);
    if (file->Write(file, &size, &byte) != EFI_WRITE_PROTECTED ||
        file->SetPosition(file, ~0ULL) != EFI_SUCCESS ||
        file->GetPosition(file, &position) != EFI_SUCCESS ||
        position != file_info->FileSize ||
        file->SetPosition(file, 0) != EFI_SUCCESS) {
        goto out;
    }
    valid = 1;

out:
    if (file_info != NULL) {
        (void)bs->FreePool(file_info);
    }
    if (label != NULL) {
        (void)bs->FreePool(label);
    }
    if (system_info != NULL) {
        (void)bs->FreePool(system_info);
    }
    if (file != NULL) {
        (void)file->Close(file);
    }
    if (relative != NULL && relative != (EFI_FILE_PROTOCOL *)(UINTN)1) {
        (void)relative->Close(relative);
    }
    if (root_copy != NULL) {
        (void)root_copy->Close(root_copy);
    }
    if (directory != NULL) {
        (void)directory->Close(directory);
    }
    if (root != NULL) {
        (void)root->Close(root);
    }
    if (second_root != NULL) {
        (void)second_root->Close(second_root);
    }
    return valid;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "storage",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_BLOCK_IO_PROTOCOL *block = NULL;
    EFI_DISK_IO_PROTOCOL *disk = NULL;
    EFI_BLOCK_IO_MEDIA *media = NULL;
    UINT8 *original = NULL;
    UINT8 *buffer = NULL;
    UINT8 disk_probe[31];
    UINT64 block_count;
    UINT64 start_lba = 0;
    UINT64 disk_offset;
    UINT64 media_bytes;
    UINTN allocation_size = 0;
    UINTN size = 0;
    UINTN index;
    EFI_STATUS status;
    EFI_STATUS restore_status;
    UINT32 invalid_media_id;
    BOOLEAN layout_valid;
    BOOLEAN error_contracts_valid;
    BOOLEAN bulk_content_valid;
    BOOLEAN disk_content_valid;
    BOOLEAN restore_verified;
    BOOLEAN wrote = 0;
    BOOLEAN write_ok = 0;
    BOOLEAN empty_removable_found;
    const char *write_detail = "write-blocks";

    status = bs->HandleProtocol(ImageHandle, loaded_image_guid,
                                (VOID **)&loaded);
    if (status == EFI_SUCCESS && loaded != NULL &&
        loaded->DeviceHandle != NULL) {
        status = bs->HandleProtocol(loaded->DeviceHandle, block_io_guid,
                                    (VOID **)&block);
    }
    if (status == EFI_SUCCESS && loaded != NULL) {
        status = bs->HandleProtocol(loaded->DeviceHandle, disk_io_guid,
                                    (VOID **)&disk);
    }
    ia64_test_check(&context, "loaded-protocols",
                    status == EFI_SUCCESS && block != NULL && disk != NULL,
                    status, "loaded-device-protocols");
    ia64_test_check(&context, "unicode-collation",
                    probe_unicode_collation(bs), EFI_DEVICE_ERROR,
                    "unicode-collation-contracts");
    {
        BOOLEAN empty_removable_valid = probe_empty_removable_media(
            bs, loaded != NULL ? loaded->DeviceHandle : NULL,
            &empty_removable_found);

        if (empty_removable_found) {
            ia64_test_check(&context, "empty-removable-media",
                            empty_removable_valid, EFI_DEVICE_ERROR,
                            "empty-removable-contracts");
        }
    }
    if (loaded != NULL &&
        probe_native_optical_filesystem(bs, loaded->DeviceHandle)) {
        ia64_test_pass(&context, "udf-filesystem");
    }
    if (block == NULL || disk == NULL || block->Media == NULL) {
        ia64_test_done(&context);
        return EFI_DEVICE_ERROR;
    }

    media = block->Media;
    block_count = media->LastBlock + 1U;
    ia64_test_check(
        &context, "block-media",
        media->MediaPresent && media->BlockSize >= 512U &&
            media->BlockSize <= 4096U && block_count != 0,
        EFI_DEVICE_ERROR, "invalid-media");
    if (!media->MediaPresent || media->BlockSize < 512U ||
        media->BlockSize > 4096U || block_count == 0) {
        ia64_test_done(&context);
        return EFI_DEVICE_ERROR;
    }
    ia64_test_check(&context, "simple-filesystem",
                    probe_loaded_filesystem(bs, loaded->DeviceHandle),
                    EFI_DEVICE_ERROR, "loaded-device-simple-fs");
    ia64_test_check(&context, "file-protocol-contracts",
                    probe_file_protocol_contracts(bs,
                                                   loaded->DeviceHandle),
                    EFI_DEVICE_ERROR, "file-protocol-contracts");
    if (media->LogicalPartition) {
        ia64_test_pass(&context, "logical-partition-handle");
        if (!media->RemovableMedia &&
            loaded_file_exists(bs, loaded->DeviceHandle,
                               short_form_app_path)) {
            ia64_test_check(&context, "short-form-hard-drive-path",
                            probe_short_form_hard_drive_path(
                                bs, ImageHandle, loaded->DeviceHandle,
                                media->BlockSize),
                            EFI_DEVICE_ERROR,
                            "short-form-hard-drive-path");
        }
        if (!media->RemovableMedia) {
            ia64_test_check(&context, "partition-driver-contracts",
                            probe_partition_driver_contracts(
                                bs, loaded->DeviceHandle),
                            EFI_DEVICE_ERROR, "partition-driver-contracts");
        }
    }

    if (block_count > MAX_TEST_BLOCKS) {
        block_count = MAX_TEST_BLOCKS;
    }
    allocation_size = (UINTN)block_count * media->BlockSize;
    if (bs->AllocatePool(EfiLoaderData, allocation_size, (VOID **)&original) !=
            EFI_SUCCESS ||
        bs->AllocatePool(EfiLoaderData, allocation_size, (VOID **)&buffer) !=
            EFI_SUCCESS) {
        ia64_test_fail(&context, "bulk-read", EFI_OUT_OF_RESOURCES,
                       "buffer-allocation");
        goto out;
    }

    status = block->ReadBlocks(block, media->MediaId, 0, media->BlockSize,
                               buffer);
    if (status == EFI_SUCCESS && media->BlockSize >= 512U &&
        get_u16(buffer + 11) == 512U && get_u16(buffer + 17) == 0 &&
        get_u16(buffer + 22) == 0 && get_u32(buffer + 36) != 0) {
        ia64_test_pass(&context, "fat32-filesystem");
    }

    layout_valid = find_fat_test_region(block, media, buffer, &start_lba,
                                        &block_count);
    ia64_test_check(&context, "media-layout", layout_valid,
                    EFI_DEVICE_ERROR, "fat-test-region");
    if (!layout_valid) {
        goto out;
    }
    size = (UINTN)block_count * media->BlockSize;
    status = block->ReadBlocks(block, media->MediaId, start_lba, size,
                               original);
    bulk_content_valid = status == EFI_SUCCESS &&
        (media->ReadOnly || original_fixture_data_valid(original, size));
    ia64_test_check(&context, "bulk-read", bulk_content_valid,
                    status, "read-blocks");
    if (status != EFI_SUCCESS) {
        goto out;
    }

    invalid_media_id = media->MediaId ^ 1U;
    error_contracts_valid =
        block->ReadBlocks(block, invalid_media_id, start_lba,
                          media->BlockSize, buffer) == EFI_MEDIA_CHANGED &&
        block->ReadBlocks(block, media->MediaId, media->LastBlock + 1U,
                          media->BlockSize, buffer) == EFI_INVALID_PARAMETER &&
        block->ReadBlocks(block, media->MediaId, start_lba,
                          media->BlockSize - 1U,
                          buffer) == EFI_BAD_BUFFER_SIZE &&
        block->ReadBlocks(block, media->MediaId, start_lba,
                          media->BlockSize, NULL) == EFI_INVALID_PARAMETER;
    if (error_contracts_valid && media->IoAlign > 1U) {
        error_contracts_valid =
            block->ReadBlocks(block, media->MediaId, start_lba,
                              media->BlockSize, buffer + 1) ==
                EFI_INVALID_PARAMETER;
    }
    media_bytes = (media->LastBlock + 1U) * (UINT64)media->BlockSize;
    error_contracts_valid = error_contracts_valid &&
        disk->ReadDisk(disk, invalid_media_id, 0, 1,
                       disk_probe) == EFI_MEDIA_CHANGED &&
        disk->ReadDisk(disk, media->MediaId, media_bytes, 1,
                       disk_probe) == EFI_INVALID_PARAMETER;
    ia64_test_check(&context, "block-error-contracts", error_contracts_valid,
                    EFI_DEVICE_ERROR, "media-id-range-size-alignment");

    disk_offset = start_lba * (UINT64)media->BlockSize + 3U;
    status = disk->ReadDisk(disk, media->MediaId, disk_offset,
                            sizeof(disk_probe), disk_probe);
    disk_content_valid = status == EFI_SUCCESS &&
        bytes_equal(disk_probe, original + 3U, sizeof(disk_probe));
    ia64_test_check(&context, "disk-read", disk_content_valid,
                    status, "read-disk-content");

    if (media->ReadOnly) {
        status = block->WriteBlocks(block, media->MediaId, start_lba, size,
                                    original);
        ia64_test_check(&context, "read-only-media",
                        status == EFI_WRITE_PROTECTED, status,
                        "write-blocks-status");
        goto out;
    }

    for (index = 0; index < size; index++) {
        buffer[index] = pattern_byte(index);
    }
    /* A failing transfer is not required to be atomic; always restore it. */
    wrote = 1;
    status = block->WriteBlocks(block, media->MediaId, start_lba, size,
                                buffer);
    if (status != EFI_SUCCESS) {
        goto write_done;
    }
    write_detail = "read-written-blocks";
    for (index = 0; index < size; index++) {
        buffer[index] = 0;
    }
    status = block->ReadBlocks(block, media->MediaId, start_lba, size,
                               buffer);
    if (status != EFI_SUCCESS) {
        goto write_done;
    }
    for (index = 0; index < size; index++) {
        if (buffer[index] != pattern_byte(index)) {
            status = EFI_DEVICE_ERROR;
            goto write_done;
        }
    }
    status = block->WriteBlocks(block, media->MediaId, start_lba, size,
                                original);
    write_detail = "restore-blocks";
    if (status == EFI_SUCCESS) {
        write_detail = "flush-restored-blocks";
        status = block->FlushBlocks(block);
    }
    if (status == EFI_SUCCESS) {
        write_detail = "verify-restored-blocks";
        status = block->ReadBlocks(block, media->MediaId, start_lba, size,
                                   buffer);
    }
    restore_verified = status == EFI_SUCCESS &&
        bytes_equal(buffer, original, size);
    if (restore_verified) {
        wrote = 0;
        write_ok = 1;
    }

write_done:
    if (wrote) {
        restore_status = block->WriteBlocks(block, media->MediaId, start_lba,
                                            size, original);
        if (restore_status == EFI_SUCCESS) {
            restore_status = block->FlushBlocks(block);
        }
        if (restore_status == EFI_SUCCESS) {
            restore_status = block->ReadBlocks(block, media->MediaId,
                                               start_lba, size, buffer);
        }
        if (restore_status == EFI_SUCCESS &&
            bytes_equal(buffer, original, size)) {
            wrote = 0;
        } else {
            status = restore_status;
        }
    }
    ia64_test_check(&context, "write-read-restore",
                    write_ok && !wrote, status, write_detail);

out:
    if (buffer != NULL) {
        (void)bs->FreePool(buffer);
    }
    if (original != NULL) {
        (void)bs->FreePool(original);
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
