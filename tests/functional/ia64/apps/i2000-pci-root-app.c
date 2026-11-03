/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_ROOT_COUNT                4U
#define TEST_RESOURCE_DESCRIPTOR_SIZE 46U
#define TEST_MAX_RESOURCE_DESCRIPTORS  9U
#define TEST_DEVICE_PATH_NODE_SIZE    12U

typedef struct {
    UINT8 Type;
    UINT64 Granularity;
    UINT64 Base;
    UINT64 Size;
} TEST_RESOURCE_EXPECTED;

typedef struct {
    const TEST_RESOURCE_EXPECTED *Resources;
    UINTN ResourceCount;
} TEST_ROOT_EXPECTED;

typedef struct {
    EFI_HANDLE Handle;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Io;
    UINT8 *DevicePath;
} TEST_ROOT;

static const TEST_RESOURCE_EXPECTED expected_root0_resources[] = {
    { 2, 0, 0x00, 1 },
    { 1, 0, 0x0000, 0x01ce },
    { 1, 0, 0x01d2, 0x01de },
    { 1, 0, 0x03e0, 0x3c20 },
    { 0, 32, 0x90000000ULL, 0x10000000ULL },
};

static const TEST_RESOURCE_EXPECTED expected_root1_resources[] = {
    { 2, 0, 0x01, 1 },
    { 1, 0, 0x4000, 0x4000 },
    { 0, 32, 0xa0000000ULL, 0x10000000ULL },
};

static const TEST_RESOURCE_EXPECTED expected_root2_resources[] = {
    { 2, 0, 0x02, 1 },
    { 1, 0, 0x8000, 0x4000 },
    { 0, 32, 0xb0000000ULL, 0x10000000ULL },
};

static const TEST_RESOURCE_EXPECTED expected_root3_resources[] = {
    { 2, 0, 0x03, 1 },
    { 1, 0, 0xc000, 0x4000 },
    { 1, 0, 0x01ce, 0x0004 },
    { 1, 0, 0x03b0, 0x0030 },
    { 0, 32, 0x000a0000, 0x00060000 },
    { 0, 32, 0xe0000000ULL, 0x10000000ULL },
};

static const TEST_ROOT_EXPECTED expected_roots[TEST_ROOT_COUNT] = {
    { expected_root0_resources, 5 },
    { expected_root1_resources, 3 },
    { expected_root2_resources, 3 },
    { expected_root3_resources, 6 },
};

static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_host_guid[16] = IA64_GUID_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;

static UINT16 test_get_u16(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 test_get_u32(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static UINT64 test_get_u64(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT64)test_get_u32(p) | ((UINT64)test_get_u32(p + 4) << 32);
}

static BOOLEAN test_root_methods_valid(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Root)
{
    return Root != NULL && Root->PollMem != NULL && Root->PollIo != NULL &&
           Root->Mem.Read != NULL && Root->Mem.Write != NULL &&
           Root->Io.Read != NULL && Root->Io.Write != NULL &&
           Root->Pci.Read != NULL && Root->Pci.Write != NULL &&
           Root->CopyMem != NULL && Root->Map != NULL && Root->Unmap != NULL &&
           Root->AllocateBuffer != NULL && Root->FreeBuffer != NULL &&
           Root->Flush != NULL && Root->GetAttributes != NULL &&
           Root->SetAttributes != NULL && Root->Configuration != NULL;
}

static BOOLEAN
test_host_methods_valid(EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host)
{
    return Host != NULL && Host->NotifyPhase != NULL &&
           Host->GetNextRootBridge != NULL &&
           Host->GetAllocAttributes != NULL &&
           Host->StartBusEnumeration != NULL && Host->SetBusNumbers != NULL &&
           Host->SubmitResources != NULL &&
           Host->GetProposedResources != NULL &&
           Host->PreprocessController != NULL;
}

static BOOLEAN test_descriptor(const UINT8 *Descriptor, UINT8 Type,
                               UINT64 Granularity, UINT64 Minimum,
                               UINT64 Maximum, UINT64 Length)
{
    return Descriptor[0] == 0x8a && test_get_u16(Descriptor + 1) == 0x2b &&
           Descriptor[3] == Type && Descriptor[4] == 0 && Descriptor[5] == 0 &&
           test_get_u64(Descriptor + 6) == Granularity &&
           test_get_u64(Descriptor + 14) == Minimum &&
           test_get_u64(Descriptor + 22) == Maximum &&
           test_get_u64(Descriptor + 30) == 0 &&
           test_get_u64(Descriptor + 38) == Length;
}

static BOOLEAN test_expected_descriptor(const UINT8 *Descriptor,
                                        const TEST_RESOURCE_EXPECTED *Expected)
{
    return test_descriptor(Descriptor, Expected->Type, Expected->Granularity,
                           Expected->Base, Expected->Base + Expected->Size - 1U,
                           Expected->Size);
}

static INTN test_root_index(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Root)
{
    const UINT8 *descriptors[TEST_MAX_RESOURCE_DESCRIPTORS];
    BOOLEAN matched[TEST_MAX_RESOURCE_DESCRIPTORS] = { 0 };
    const TEST_ROOT_EXPECTED *expected;
    VOID *configuration = NULL;
    const UINT8 *descriptor;
    INTN root_index = -1;
    UINTN descriptor_count = 0;
    UINTN index;
    UINTN resource;

    if (Root->Configuration(Root, &configuration) != EFI_SUCCESS ||
        configuration == NULL) {
        return -1;
    }
    descriptor = configuration;
    while (descriptor_count < TEST_MAX_RESOURCE_DESCRIPTORS &&
           descriptor[0] != 0x79) {
        if (descriptor[0] != 0x8a || test_get_u16(descriptor + 1) != 0x2b) {
            return -1;
        }
        descriptors[descriptor_count++] = descriptor;
        descriptor += TEST_RESOURCE_DESCRIPTOR_SIZE;
    }
    if (descriptor[0] != 0x79 || descriptor[1] != 0) {
        return -1;
    }

    for (resource = 0; resource < descriptor_count; resource++) {
        if (descriptors[resource][3] != 2) {
            continue;
        }
        if (root_index >= 0) {
            return -1;
        }
        for (index = 0; index < TEST_ROOT_COUNT; index++) {
            if (test_expected_descriptor(descriptors[resource],
                                         &expected_roots[index].Resources[0])) {
                root_index = (INTN)index;
                break;
            }
        }
        if (index == TEST_ROOT_COUNT) {
            return -1;
        }
    }
    if (root_index < 0) {
        return -1;
    }
    expected = &expected_roots[root_index];
    if (descriptor_count != expected->ResourceCount) {
        return -1;
    }
    for (resource = 0; resource < descriptor_count; resource++) {
        for (index = 0; index < expected->ResourceCount; index++) {
            if (!matched[index] &&
                test_expected_descriptor(descriptors[resource],
                                         &expected->Resources[index])) {
                matched[index] = 1;
                break;
            }
        }
        if (index == expected->ResourceCount) {
            return -1;
        }
    }
    return root_index;
}

static BOOLEAN test_root_device_path(const TEST_ROOT *Root, UINTN Index)
{
    const UINT8 *path = Root->DevicePath;
    const UINT8 *end;

    if (path == NULL || path[0] != 0x02 || path[1] != 0x01 ||
        test_get_u16(path + 2) != TEST_DEVICE_PATH_NODE_SIZE ||
        test_get_u32(path + 4) != 0x0A0341D0 ||
        test_get_u32(path + 8) != Index) {
        return 0;
    }
    end = path + TEST_DEVICE_PATH_NODE_SIZE;
    return end[0] == 0x7f && end[1] == 0xff && test_get_u16(end + 2) == 4;
}

static BOOLEAN test_handle_in_roots(const TEST_ROOT *Roots, EFI_HANDLE Handle,
                                    UINTN *Index)
{
    UINTN i;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (Roots[i].Handle == Handle) {
            if (Index != NULL) {
                *Index = i;
            }
            return 1;
        }
    }
    return 0;
}

static BOOLEAN
test_protocols(EFI_SYSTEM_TABLE *SystemTable, TEST_ROOT *Roots,
               EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL **Host,
               EFI_STATUS *Code, const char **Detail)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_HANDLE *host_handles = NULL;
    EFI_HANDLE *root_handles = NULL;
    UINTN host_count = 0;
    UINTN root_count = 0;
    UINT32 mask = 0;
    UINTN i;
    EFI_STATUS status;
    BOOLEAN valid = 0;

    status = bs->LocateProtocol(pci_host_guid, NULL, (VOID **)Host);
    if (status != EFI_SUCCESS || !test_host_methods_valid(*Host)) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-protocol";
        return 0;
    }
    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_host_guid, NULL,
                                    &host_count, &host_handles);
    if (status != EFI_SUCCESS || host_count != 1 || host_handles == NULL) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-handle";
        goto out;
    }
    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_root_guid, NULL,
                                    &root_count, &root_handles);
    if (status != EFI_SUCCESS || root_count != TEST_ROOT_COUNT ||
        root_handles == NULL) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "root-handles";
        goto out;
    }
    for (i = 0; i < root_count; i++) {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *io = NULL;
        UINT8 *device_path = NULL;
        INTN index;

        status =
            bs->HandleProtocol(root_handles[i], pci_root_guid, (VOID **)&io);
        if (status != EFI_SUCCESS || !test_root_methods_valid(io) ||
            io->ParentHandle != host_handles[0] || io->SegmentNumber != 0) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "root-protocol";
            goto out;
        }
        status = bs->HandleProtocol(root_handles[i], device_path_guid,
                                    (VOID **)&device_path);
        if (status != EFI_SUCCESS || device_path == NULL) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "root-device-path";
            goto out;
        }
        index = test_root_index(io);
        if (index < 0 || (mask & (1U << index)) != 0) {
            *Code = EFI_DEVICE_ERROR;
            *Detail = "root-resources";
            goto out;
        }
        Roots[index].Handle = root_handles[i];
        Roots[index].Io = io;
        Roots[index].DevicePath = device_path;
        mask |= 1U << index;
    }
    if (mask != (1U << TEST_ROOT_COUNT) - 1U) {
        *Code = EFI_DEVICE_ERROR;
        *Detail = "root-set";
        goto out;
    }
    valid = 1;

out:
    if (root_handles != NULL && bs->FreePool(root_handles) != EFI_SUCCESS) {
        valid = 0;
        *Code = EFI_DEVICE_ERROR;
        *Detail = "root-handle-free";
    }
    if (host_handles != NULL && bs->FreePool(host_handles) != EFI_SUCCESS) {
        valid = 0;
        *Code = EFI_DEVICE_ERROR;
        *Detail = "host-handle-free";
    }
    return valid;
}

static BOOLEAN test_device_paths(const TEST_ROOT *Roots, EFI_STATUS *Code,
                                 const char **Detail)
{
    UINTN i;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (!test_root_device_path(&Roots[i], i)) {
            *Code = EFI_DEVICE_ERROR;
            *Detail = "root-device-path";
            return 0;
        }
    }
    return 1;
}

static UINT64 test_pci_address(UINT8 Bus, UINT8 Device)
{
    return ((UINT64)Bus << 24) | ((UINT64)Device << 16);
}

static BOOLEAN test_config_access(const TEST_ROOT *Roots, EFI_STATUS *Code,
                                  const char **Detail)
{
    UINT32 quadro_id = 0;
    UINT32 isp_id = 0;
    UINT32 ignored = 0;
    UINT16 vbe_index = 0;
    EFI_STATUS status;

    status = Roots[3].Io->Pci.Read(Roots[3].Io, EfiPciWidthUint32,
                                   test_pci_address(0x03, 0), 1, &quadro_id);
    if (status != EFI_SUCCESS || quadro_id != 0x015310deU) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "quadro2-config";
        return 0;
    }
    status = Roots[1].Io->Pci.Read(Roots[1].Io, EfiPciWidthUint32,
                                   test_pci_address(0x01, 0), 1, &isp_id);
    if (status != EFI_SUCCESS || isp_id != 0x12161077U) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "isp12160-config";
        return 0;
    }
    status = Roots[0].Io->Pci.Read(Roots[0].Io, EfiPciWidthUint32,
                                   test_pci_address(0x01, 0), 1, &ignored);
    if (status != EFI_INVALID_PARAMETER) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "root-isolation";
        return 0;
    }
    status = Roots[3].Io->Io.Read(Roots[3].Io, EfiPciWidthUint16, 0x01ce, 1,
                                  &vbe_index);
    if (status != EFI_SUCCESS) {
        *Code = status;
        *Detail = "vbe-owner";
        return 0;
    }
    status = Roots[0].Io->Io.Read(Roots[0].Io, EfiPciWidthUint16, 0x01ce, 1,
                                  &vbe_index);
    if (status != EFI_INVALID_PARAMETER) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "vbe-isolation";
        return 0;
    }
    return 1;
}

static BOOLEAN
test_host_enumeration(EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host,
                      const TEST_ROOT *Roots, EFI_STATUS *Code,
                      const char **Detail)
{
    EFI_HANDLE handle = NULL;
    UINT32 mask = 0;
    UINTN i;
    EFI_STATUS status;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        UINTN index;

        status = Host->GetNextRootBridge(Host, &handle);
        if (status != EFI_SUCCESS ||
            !test_handle_in_roots(Roots, handle, &index) ||
            (mask & (1U << index)) != 0) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "host-root-enumeration";
            return 0;
        }
        mask |= 1U << index;
    }
    status = Host->GetNextRootBridge(Host, &handle);
    if (status != EFI_NOT_FOUND || mask != (1U << TEST_ROOT_COUNT) - 1U) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-root-end";
        return 0;
    }
    return 1;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT output = {
        .SystemTable = SystemTable,
        .Suite = "i2000-pci-root",
    };
    TEST_ROOT roots[TEST_ROOT_COUNT] = { 0 };
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *host = NULL;
    EFI_STATUS code = EFI_DEVICE_ERROR;
    const char *detail = "system-table";
    BOOLEAN table_valid;
    BOOLEAN protocols_valid = 0;
    BOOLEAN paths_valid = 0;
    BOOLEAN config_valid = 0;
    BOOLEAN enumeration_valid = 0;

    (void)ImageHandle;
    table_valid = SystemTable != NULL && SystemTable->BootServices != NULL &&
                  SystemTable->BootServices->LocateProtocol != NULL &&
                  SystemTable->BootServices->LocateHandleBuffer != NULL &&
                  SystemTable->BootServices->HandleProtocol != NULL &&
                  SystemTable->BootServices->FreePool != NULL;
    ia64_test_check(&output, "system-table", table_valid, code, detail);

    if (table_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        protocols_valid =
            test_protocols(SystemTable, roots, &host, &code, &detail);
    }
    ia64_test_check(&output, "protocols", protocols_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        paths_valid = test_device_paths(roots, &code, &detail);
    }
    ia64_test_check(&output, "device-paths", paths_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        config_valid = test_config_access(roots, &code, &detail);
    }
    ia64_test_check(&output, "config-access", config_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        enumeration_valid = test_host_enumeration(host, roots, &code, &detail);
    }
    ia64_test_check(&output, "host-enumeration", enumeration_valid, code,
                    detail);

    ia64_test_done(&output);
    return output.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
