/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_ROOT_COUNT                 2U
#define TEST_RESOURCE_DESCRIPTOR_SIZE 46U
#define TEST_PCI_PROBE_DEVICE           1U
#define TEST_PCI_ID_OFFSET              0U
#define TEST_PCI_COMMAND_OFFSET         4U
#define TEST_PCI_COMMAND_MEMORY         0x0002U
#define TEST_PCI_COMMAND_BUS_MASTER     0x0004U

#define TEST_ROOT0_BUS_BASE             0x20U
#define TEST_ROOT0_BUS_END              0x2fU
#define TEST_ROOT1_BUS_BASE             0x40U
#define TEST_ROOT1_BUS_END              0x4fU
#define TEST_ROOT0_CPU_MEMORY_BASE      0x90000000ULL
#define TEST_ROOT1_CPU_MEMORY_BASE      0xa0000000ULL
#define TEST_ROOT_MEMORY_SIZE           0x01000000ULL

typedef struct {
    EFI_HANDLE Handle;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Io;
    UINT8 *DevicePath;
    UINT32 Uid;
    UINT8 BusBase;
    UINT8 BusEnd;
    UINT64 CpuMemoryBase;
    UINT64 MemoryTranslation;
    UINT64 MemoryLength;
} TEST_ROOT_VIEW;

typedef struct {
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host;
    EFI_HANDLE HostHandle;
    TEST_ROOT_VIEW Root[TEST_ROOT_COUNT];
} TEST_PCI_BRIDGE_CONTEXT;

static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_host_guid[16] = IA64_GUID_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 identity_dma_buffer[64] __attribute__((aligned(64)));

static UINT16 test_get_u16(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 test_get_u32(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static UINT64 test_get_u64(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT64)test_get_u32(p) | ((UINT64)test_get_u32(p + 4) << 32);
}

static void test_zero(VOID *Address, UINTN Size)
{
    /* Volatile stores keep this freestanding loop independent of memset. */
    volatile UINT8 *p = (volatile UINT8 *)Address;
    UINTN i;

    for (i = 0; i < Size; i++) {
        p[i] = 0;
    }
}

static BOOLEAN test_fail(EFI_STATUS *Code, const char **Detail,
                         EFI_STATUS Status, const char *Text)
{
    *Code = Status == EFI_SUCCESS ? EFI_DEVICE_ERROR : Status;
    *Detail = Text;
    return 0;
}

static void test_result_init(EFI_STATUS *Code, const char **Detail)
{
    *Code = EFI_SUCCESS;
    *Detail = "ok";
}

static BOOLEAN test_protocol_methods_valid(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Root,
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host)
{
    return Root != NULL && Root->PollMem != NULL && Root->PollIo != NULL &&
           Root->Mem.Read != NULL && Root->Mem.Write != NULL &&
           Root->Io.Read != NULL && Root->Io.Write != NULL &&
           Root->Pci.Read != NULL && Root->Pci.Write != NULL &&
           Root->CopyMem != NULL && Root->Map != NULL && Root->Unmap != NULL &&
           Root->AllocateBuffer != NULL && Root->FreeBuffer != NULL &&
           Root->Flush != NULL && Root->GetAttributes != NULL &&
           Root->SetAttributes != NULL && Root->Configuration != NULL &&
           Host != NULL && Host->NotifyPhase != NULL &&
           Host->GetNextRootBridge != NULL &&
           Host->GetAllocAttributes != NULL &&
           Host->StartBusEnumeration != NULL && Host->SetBusNumbers != NULL &&
           Host->SubmitResources != NULL &&
           Host->GetProposedResources != NULL &&
           Host->PreprocessController != NULL;
}

static BOOLEAN test_root_handle_in_set(TEST_PCI_BRIDGE_CONTEXT *Context,
                                       EFI_HANDLE Handle)
{
    UINTN i;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (Context->Root[i].Handle == Handle) {
            return 1;
        }
    }
    return 0;
}

static BOOLEAN test_locate_protocols(TEST_PCI_BRIDGE_CONTEXT *Context,
                                     EFI_STATUS *Code, const char **Detail)
{
    EFI_BOOT_SERVICES *bs = Context->SystemTable->BootServices;
    EFI_HANDLE *host_handles = NULL;
    EFI_HANDLE *root_handles = NULL;
    EFI_HANDLE enumerated = NULL;
    EFI_HANDLE first_enumerated;
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *host_interface = NULL;
    UINTN host_count = 0;
    UINTN root_count = 0;
    UINTN i;
    EFI_STATUS status;
    BOOLEAN valid = 0;

    test_result_init(Code, Detail);
    status = bs->LocateProtocol(pci_host_guid, NULL, (VOID **)&Context->Host);
    if (status != EFI_SUCCESS || Context->Host == NULL) {
        return test_fail(Code, Detail, status, "host-locate");
    }
    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_host_guid, NULL,
                                    &host_count, &host_handles);
    if (status != EFI_SUCCESS || host_count != 1 || host_handles == NULL) {
        test_fail(Code, Detail, status, "host-handle");
        goto out;
    }
    Context->HostHandle = host_handles[0];
    status = bs->HandleProtocol(Context->HostHandle, pci_host_guid,
                                (VOID **)&host_interface);
    if (status != EFI_SUCCESS || host_interface != Context->Host) {
        test_fail(Code, Detail, status, "host-interface");
        goto out;
    }

    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_root_guid, NULL,
                                    &root_count, &root_handles);
    if (status != EFI_SUCCESS || root_count != TEST_ROOT_COUNT ||
        root_handles == NULL) {
        test_fail(Code, Detail, status, "root-handles");
        goto out;
    }
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        TEST_ROOT_VIEW *root = &Context->Root[i];

        root->Handle = root_handles[i];
        status =
            bs->HandleProtocol(root->Handle, pci_root_guid, (VOID **)&root->Io);
        if (status != EFI_SUCCESS || root->Io == NULL) {
            test_fail(Code, Detail, status, "root-interface");
            goto out;
        }
        status = bs->HandleProtocol(root->Handle, device_path_guid,
                                    (VOID **)&root->DevicePath);
        if (status != EFI_SUCCESS || root->DevicePath == NULL) {
            test_fail(Code, Detail, status, "root-device-path");
            goto out;
        }
        if (!test_protocol_methods_valid(root->Io, Context->Host) ||
            root->Io->ParentHandle != Context->HostHandle) {
            test_fail(Code, Detail, EFI_DEVICE_ERROR, "protocol-methods");
            goto out;
        }
    }
    if (Context->Root[0].Handle == Context->Root[1].Handle ||
        Context->Root[0].Io == Context->Root[1].Io) {
        test_fail(Code, Detail, EFI_DEVICE_ERROR, "root-identity");
        goto out;
    }

    status = Context->Host->GetNextRootBridge(Context->Host, &enumerated);
    if (status != EFI_SUCCESS ||
        !test_root_handle_in_set(Context, enumerated)) {
        test_fail(Code, Detail, status, "host-first-root");
        goto out;
    }
    first_enumerated = enumerated;
    status = Context->Host->GetNextRootBridge(Context->Host, &enumerated);
    if (status != EFI_SUCCESS || enumerated == first_enumerated ||
        !test_root_handle_in_set(Context, enumerated)) {
        test_fail(Code, Detail, status, "host-second-root");
        goto out;
    }
    status = Context->Host->GetNextRootBridge(Context->Host, &enumerated);
    if (status != EFI_NOT_FOUND) {
        test_fail(Code, Detail, status, "host-root-end");
        goto out;
    }
    valid = 1;

out:
    if (root_handles != NULL && bs->FreePool(root_handles) != EFI_SUCCESS) {
        valid = test_fail(Code, Detail, EFI_DEVICE_ERROR, "root-handle-free");
    }
    if (host_handles != NULL && bs->FreePool(host_handles) != EFI_SUCCESS) {
        valid = test_fail(Code, Detail, EFI_DEVICE_ERROR, "host-handle-free");
    }
    return valid;
}

static BOOLEAN test_parse_root_device_path(TEST_ROOT_VIEW *Root)
{
    const UINT8 *path = Root->DevicePath;
    const UINT8 *end;
    UINT32 uid;

    if (path[0] != 0x02 || path[1] != 0x01 || test_get_u16(path + 2) != 12U ||
        test_get_u32(path + 4) != 0x000222f0U) {
        return 0;
    }
    uid = test_get_u32(path + 8);
    end = path + 12U;
    if (end[0] != 0x7f || end[1] != 0xff || test_get_u16(end + 2) != 4U) {
        return 0;
    }
    Root->Uid = uid;
    return 1;
}

static BOOLEAN test_root_device_paths(TEST_PCI_BRIDGE_CONTEXT *Context,
                                      EFI_STATUS *Code, const char **Detail)
{
    UINT32 uid_mask = 0;
    UINTN i;

    test_result_init(Code, Detail);
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (!test_parse_root_device_path(&Context->Root[i]) ||
            Context->Root[i].Uid >= TEST_ROOT_COUNT) {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR, "hp-acpi-path");
        }
        uid_mask |= 1U << Context->Root[i].Uid;
    }
    if (uid_mask != 3U) {
        return test_fail(Code, Detail, EFI_DEVICE_ERROR, "root-uids");
    }
    return 1;
}

static BOOLEAN test_parse_root_configuration(TEST_ROOT_VIEW *Root,
                                             EFI_STATUS *Code,
                                             const char **Detail)
{
    UINT8 *descriptor;
    VOID *resources = NULL;
    UINTN count;
    UINTN bus_count = 0;
    UINTN memory_count = 0;
    EFI_STATUS status;

    status = Root->Io->Configuration(Root->Io, &resources);
    if (status != EFI_SUCCESS || resources == NULL) {
        return test_fail(Code, Detail, status, "configuration-call");
    }
    descriptor = (UINT8 *)resources;
    for (count = 0; count < 4U;
         count++, descriptor += TEST_RESOURCE_DESCRIPTOR_SIZE) {
        UINT8 type;
        UINT64 granularity;
        UINT64 minimum;
        UINT64 maximum;
        UINT64 translation;
        UINT64 length;

        if (descriptor[0] == 0x79) {
            if (descriptor[1] == 0 && count == 2U && bus_count == 1U &&
                memory_count == 1U) {
                return 1;
            }
            return test_fail(Code, Detail, EFI_DEVICE_ERROR, "resource-count");
        }
        if (descriptor[0] != 0x8a || test_get_u16(descriptor + 1) != 0x2b ||
            descriptor[4] != 0 || descriptor[5] != 0) {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR, "resource-header");
        }
        type = descriptor[3];
        granularity = test_get_u64(descriptor + 6);
        minimum = test_get_u64(descriptor + 14);
        maximum = test_get_u64(descriptor + 22);
        translation = test_get_u64(descriptor + 30);
        length = test_get_u64(descriptor + 38);
        if (maximum < minimum || length == 0 ||
            length - 1U != maximum - minimum) {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR, "resource-range");
        }
        if (type == 2U) {
            if (bus_count != 0 || granularity != 0 || translation != 0 ||
                maximum > 255U) {
                return test_fail(Code, Detail, EFI_DEVICE_ERROR,
                                 "bus-resource");
            }
            Root->BusBase = (UINT8)minimum;
            Root->BusEnd = (UINT8)maximum;
            bus_count++;
        } else if (type == 0U) {
            if (memory_count != 0 || granularity != 32U ||
                minimum + translation != 0) {
                return test_fail(Code, Detail, EFI_DEVICE_ERROR,
                                 "memory-resource");
            }
            Root->CpuMemoryBase = minimum;
            Root->MemoryTranslation = translation;
            Root->MemoryLength = length;
            memory_count++;
        } else {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR, "resource-type");
        }
    }
    return test_fail(Code, Detail, EFI_DEVICE_ERROR, "resource-end-tag");
}

static void test_sort_roots(TEST_PCI_BRIDGE_CONTEXT *Context)
{
    if (Context->Root[0].BusBase > Context->Root[1].BusBase) {
        TEST_ROOT_VIEW saved = Context->Root[0];

        Context->Root[0] = Context->Root[1];
        Context->Root[1] = saved;
    }
}

static BOOLEAN test_root_configurations(TEST_PCI_BRIDGE_CONTEXT *Context,
                                        EFI_STATUS *Code, const char **Detail)
{
    TEST_ROOT_VIEW *root0;
    TEST_ROOT_VIEW *root1;
    UINTN i;

    test_result_init(Code, Detail);
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (!test_parse_root_configuration(&Context->Root[i], Code, Detail)) {
            return 0;
        }
    }
    test_sort_roots(Context);
    root0 = &Context->Root[0];
    root1 = &Context->Root[1];
    if (root0->Uid != 0 || root1->Uid != 1 || root0->Io->SegmentNumber != 0 ||
        root1->Io->SegmentNumber != 0 ||
        root0->BusBase != TEST_ROOT0_BUS_BASE ||
        root0->BusEnd != TEST_ROOT0_BUS_END ||
        root1->BusBase != TEST_ROOT1_BUS_BASE ||
        root1->BusEnd != TEST_ROOT1_BUS_END ||
        root0->CpuMemoryBase != TEST_ROOT0_CPU_MEMORY_BASE ||
        root1->CpuMemoryBase != TEST_ROOT1_CPU_MEMORY_BASE ||
        root0->MemoryLength != TEST_ROOT_MEMORY_SIZE ||
        root1->MemoryLength != TEST_ROOT_MEMORY_SIZE ||
        root0->MemoryTranslation != 0 - TEST_ROOT0_CPU_MEMORY_BASE ||
        root1->MemoryTranslation != 0 - TEST_ROOT1_CPU_MEMORY_BASE) {
        return test_fail(Code, Detail, EFI_DEVICE_ERROR,
                         "root-resource-values");
    }
    return 1;
}

static UINT64 test_pci_config_address(UINT8 Bus, UINT8 Device, UINT8 Function,
                                      UINT8 Register)
{
    return ((UINT64)Bus << 24) | ((UINT64)Device << 16) |
           ((UINT64)Function << 8) | Register;
}

static BOOLEAN test_pci_config_isolation(TEST_PCI_BRIDGE_CONTEXT *Context,
                                         EFI_STATUS *Code, const char **Detail)
{
    TEST_ROOT_VIEW *root0 = &Context->Root[0];
    TEST_ROOT_VIEW *root1 = &Context->Root[1];
    UINT64 address0 = test_pci_config_address(
        root0->BusBase, TEST_PCI_PROBE_DEVICE, 0, TEST_PCI_ID_OFFSET);
    UINT64 address1 = test_pci_config_address(
        root1->BusBase, TEST_PCI_PROBE_DEVICE, 0, TEST_PCI_ID_OFFSET);
    UINT64 command_address0 = address0 + TEST_PCI_COMMAND_OFFSET;
    UINT64 command_address1 = address1 + TEST_PCI_COMMAND_OFFSET;
    UINT32 id0 = 0;
    UINT32 id1 = 0;
    UINT32 ignored = 0;
    UINT16 original0 = 0;
    UINT16 original1 = 0;
    UINT16 command0;
    UINT16 command1;
    UINT16 desired0;
    UINT16 desired1;
    BOOLEAN wrote0 = 0;
    BOOLEAN wrote1 = 0;
    BOOLEAN valid = 0;
    EFI_STATUS status;

    test_result_init(Code, Detail);
    status =
        root0->Io->Pci.Read(root0->Io, EfiPciWidthUint32, address0, 1, &id0);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "root0-config-read");
    }
    status =
        root1->Io->Pci.Read(root1->Io, EfiPciWidthUint32, address1, 1, &id1);
    if (status != EFI_SUCCESS || id0 == 0xffffffffU || id0 != id1) {
        return test_fail(Code, Detail, status, "root1-config-read");
    }
    status = root0->Io->Pci.Read(root0->Io, EfiPciWidthUint32, address1, 1,
                                 &ignored);
    if (status != EFI_INVALID_PARAMETER) {
        return test_fail(Code, Detail, status, "root0-bus-ownership");
    }
    status = root1->Io->Pci.Read(root1->Io, EfiPciWidthUint32, address0, 1,
                                 &ignored);
    if (status != EFI_INVALID_PARAMETER) {
        return test_fail(Code, Detail, status, "root1-bus-ownership");
    }

    status = root0->Io->Pci.Read(root0->Io, EfiPciWidthUint16, command_address0,
                                 1, &original0);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "root0-command-read");
    }
    status = root1->Io->Pci.Read(root1->Io, EfiPciWidthUint16, command_address1,
                                 1, &original1);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "root1-command-read");
    }
    desired0 =
        (original0 & ~(TEST_PCI_COMMAND_MEMORY | TEST_PCI_COMMAND_BUS_MASTER)) |
        TEST_PCI_COMMAND_MEMORY;
    desired1 =
        (original1 & ~(TEST_PCI_COMMAND_MEMORY | TEST_PCI_COMMAND_BUS_MASTER)) |
        TEST_PCI_COMMAND_BUS_MASTER;
    status = root0->Io->Pci.Write(root0->Io, EfiPciWidthUint16,
                                  command_address0, 1, &desired0);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "root0-command-write");
    }
    wrote0 = 1;
    status = root1->Io->Pci.Write(root1->Io, EfiPciWidthUint16,
                                  command_address1, 1, &desired1);
    if (status != EFI_SUCCESS) {
        test_fail(Code, Detail, status, "root1-command-write");
        goto out;
    }
    wrote1 = 1;
    status = root0->Io->Pci.Read(root0->Io, EfiPciWidthUint16, command_address0,
                                 1, &command0);
    if (status != EFI_SUCCESS) {
        test_fail(Code, Detail, status, "root0-command-verify");
        goto out;
    }
    status = root1->Io->Pci.Read(root1->Io, EfiPciWidthUint16, command_address1,
                                 1, &command1);
    if (status != EFI_SUCCESS || command0 != desired0 || command1 != desired1 ||
        command0 == command1) {
        test_fail(Code, Detail, status, "root1-command-verify");
        goto out;
    }
    valid = 1;

out:
    if (wrote1 &&
        root1->Io->Pci.Write(root1->Io, EfiPciWidthUint16, command_address1, 1,
                             &original1) != EFI_SUCCESS) {
        valid =
            test_fail(Code, Detail, EFI_DEVICE_ERROR, "root1-command-restore");
    }
    if (wrote0 &&
        root0->Io->Pci.Write(root0->Io, EfiPciWidthUint16, command_address0, 1,
                             &original0) != EFI_SUCCESS) {
        valid =
            test_fail(Code, Detail, EFI_DEVICE_ERROR, "root0-command-restore");
    }
    return valid;
}

static BOOLEAN test_identity_dma(TEST_PCI_BRIDGE_CONTEXT *Context,
                                 EFI_STATUS *Code, const char **Detail)
{
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root0 = Context->Root[0].Io;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root1 = Context->Root[1].Io;
    EFI_PHYSICAL_ADDRESS device_address = 0;
    VOID *mapping = NULL;
    VOID *wrong_mapping = NULL;
    VOID *allocation = NULL;
    UINTN bytes = sizeof(identity_dma_buffer);
    UINTN wrong_bytes;
    UINTN common_bytes;
    BOOLEAN allocation_valid = 0;
    BOOLEAN valid = 0;
    EFI_STATUS status;

    test_result_init(Code, Detail);
    status = root0->Map(root0, EfiPciOperationBusMasterRead64,
                        identity_dma_buffer, &bytes, &device_address, &mapping);
    if (status != EFI_SUCCESS || mapping == NULL ||
        device_address != (EFI_PHYSICAL_ADDRESS)(UINTN)identity_dma_buffer ||
        bytes != sizeof(identity_dma_buffer)) {
        test_fail(Code, Detail, status, "identity-map");
        goto out;
    }
    status = root1->Unmap(root1, mapping);
    if (status != EFI_INVALID_PARAMETER) {
        if (status == EFI_SUCCESS) {
            mapping = NULL;
        }
        test_fail(Code, Detail, status, "mapping-owner");
        goto out;
    }
    status = root0->Unmap(root0, mapping);
    mapping = NULL;
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "identity-unmap");
    }

    status = root0->AllocateBuffer(root0, AllocateAnyPages, EfiBootServicesData,
                                   1, &allocation, 0);
    if (status != EFI_SUCCESS || allocation == NULL) {
        return test_fail(Code, Detail, status, "allocate-buffer");
    }
    allocation_valid = 1;
    status = root1->FreeBuffer(root1, 1, allocation);
    if (status != EFI_INVALID_PARAMETER) {
        if (status == EFI_SUCCESS) {
            allocation_valid = 0;
        }
        test_fail(Code, Detail, status, "buffer-owner");
        goto out;
    }

    wrong_bytes = 64U;
    device_address = 0;
    status = root1->Map(root1, EfiPciOperationBusMasterCommonBuffer, allocation,
                        &wrong_bytes, &device_address, &wrong_mapping);
    if (status != EFI_UNSUPPORTED || wrong_mapping != NULL) {
        test_fail(Code, Detail, status, "common-buffer-owner");
        goto out;
    }

    common_bytes = 64U;
    device_address = 0;
    status = root0->Map(root0, EfiPciOperationBusMasterCommonBuffer, allocation,
                        &common_bytes, &device_address, &mapping);
    if (status != EFI_SUCCESS || mapping == NULL || common_bytes != 64U ||
        device_address != (EFI_PHYSICAL_ADDRESS)(UINTN)allocation) {
        test_fail(Code, Detail, status, "common-buffer-map");
        goto out;
    }
    status = root1->Unmap(root1, mapping);
    if (status != EFI_INVALID_PARAMETER) {
        if (status == EFI_SUCCESS) {
            mapping = NULL;
        }
        test_fail(Code, Detail, status, "common-mapping-owner");
        goto out;
    }
    status = root0->Unmap(root0, mapping);
    mapping = NULL;
    if (status != EFI_SUCCESS) {
        test_fail(Code, Detail, status, "common-buffer-unmap");
        goto out;
    }
    valid = 1;

out:
    if (wrong_mapping != NULL) {
        (void)root1->Unmap(root1, wrong_mapping);
    }
    if (mapping != NULL && root0->Unmap(root0, mapping) != EFI_SUCCESS) {
        valid = test_fail(Code, Detail, EFI_DEVICE_ERROR, "mapping-cleanup");
    }
    if (allocation_valid &&
        root0->FreeBuffer(root0, 1, allocation) != EFI_SUCCESS) {
        valid = test_fail(Code, Detail, EFI_DEVICE_ERROR, "buffer-cleanup");
    }
    return valid;
}

static BOOLEAN test_bus_descriptor_matches(const UINT8 *Descriptor,
                                           const TEST_ROOT_VIEW *Root)
{
    const UINT8 *end = Descriptor + TEST_RESOURCE_DESCRIPTOR_SIZE;

    return Descriptor[0] == 0x8a && test_get_u16(Descriptor + 1) == 0x2b &&
           Descriptor[3] == 2U && Descriptor[4] == 0 && Descriptor[5] == 0 &&
           test_get_u64(Descriptor + 6) == 0 &&
           test_get_u64(Descriptor + 14) == Root->BusBase &&
           test_get_u64(Descriptor + 22) == 0 &&
           test_get_u64(Descriptor + 30) == 0 &&
           test_get_u64(Descriptor + 38) ==
               (UINT64)Root->BusEnd - Root->BusBase + 1U &&
           end[0] == 0x79 && end[1] == 0;
}

static BOOLEAN test_host_allocation(TEST_PCI_BRIDGE_CONTEXT *Context,
                                    EFI_STATUS *Code, const char **Detail)
{
    EFI_BOOT_SERVICES *bs = Context->SystemTable->BootServices;
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *host = Context->Host;
    UINT8 end_tag[2] = { 0x79, 0 };
    UINTN i;
    EFI_STATUS status;

    test_result_init(Code, Detail);
    status = host->NotifyPhase(host, EfiPciHostBridgeBeginBusAllocation);
    if (status != EFI_NOT_READY) {
        return test_fail(Code, Detail, status, "phase-order");
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeBeginEnumeration);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "begin-enumeration");
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeBeginBusAllocation);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "begin-bus-allocation");
    }
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        VOID *configuration = NULL;

        status = host->StartBusEnumeration(host, Context->Root[i].Handle,
                                           &configuration);
        if (status != EFI_SUCCESS || configuration == NULL ||
            !test_bus_descriptor_matches(configuration, &Context->Root[i])) {
            if (configuration != NULL) {
                (void)bs->FreePool(configuration);
            }
            return test_fail(Code, Detail, status, "start-bus-enumeration");
        }
        status =
            host->SetBusNumbers(host, Context->Root[i].Handle, configuration);
        if (bs->FreePool(configuration) != EFI_SUCCESS) {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR,
                             "bus-configuration-free");
        }
        if (status != EFI_SUCCESS) {
            return test_fail(Code, Detail, status, "set-bus-numbers");
        }
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeEndBusAllocation);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "end-bus-allocation");
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeBeginResourceAllocation);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "begin-resource-allocation");
    }
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        status = host->SubmitResources(host, Context->Root[i].Handle, end_tag);
        if (status != EFI_SUCCESS) {
            return test_fail(Code, Detail, status, "submit-empty-resources");
        }
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeAllocateResources);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "allocate-resources");
    }
    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        VOID *resources = NULL;

        status = host->GetProposedResources(host, Context->Root[i].Handle,
                                            &resources);
        if (status != EFI_SUCCESS || resources == NULL ||
            ((UINT8 *)resources)[0] != 0x79 || ((UINT8 *)resources)[1] != 0) {
            if (resources != NULL) {
                (void)bs->FreePool(resources);
            }
            return test_fail(Code, Detail, status, "proposed-empty-resources");
        }
        if (bs->FreePool(resources) != EFI_SUCCESS) {
            return test_fail(Code, Detail, EFI_DEVICE_ERROR,
                             "proposed-resources-free");
        }
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeSetResources);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "set-resources");
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeEndResourceAllocation);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "end-resource-allocation");
    }
    status = host->NotifyPhase(host, EfiPciHostBridgeEndEnumeration);
    if (status != EFI_SUCCESS) {
        return test_fail(Code, Detail, status, "end-enumeration");
    }
    return 1;
}

static void test_check_prerequisite(IA64_TEST_CONTEXT *Output, const char *Case,
                                    BOOLEAN Ready,
                                    BOOLEAN (*Run)(TEST_PCI_BRIDGE_CONTEXT *,
                                                   EFI_STATUS *, const char **),
                                    TEST_PCI_BRIDGE_CONTEXT *Context)
{
    EFI_STATUS code = EFI_NOT_READY;
    const char *detail = "protocol-prerequisite";
    BOOLEAN passed = Ready && Run(Context, &code, &detail);

    ia64_test_check(Output, Case, passed, code, detail);
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT output = {
        .SystemTable = SystemTable,
        .Suite = "pci-bridge",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    TEST_PCI_BRIDGE_CONTEXT context;
    EFI_STATUS code = EFI_DEVICE_ERROR;
    const char *detail = "system-table";
    BOOLEAN table_valid;
    BOOLEAN located = 0;
    BOOLEAN paths_valid = 0;
    BOOLEAN resources_valid = 0;

    (void)ImageHandle;
    test_zero(&context, sizeof(context));
    context.SystemTable = SystemTable;
    table_valid = SystemTable != NULL && SystemTable->BootServices != NULL &&
                  SystemTable->BootServices->LocateProtocol != NULL &&
                  SystemTable->BootServices->LocateHandleBuffer != NULL &&
                  SystemTable->BootServices->HandleProtocol != NULL &&
                  SystemTable->BootServices->FreePool != NULL;
    ia64_test_check(&output, "system-table", table_valid, code, detail);

    if (table_valid) {
        located = test_locate_protocols(&context, &code, &detail);
    } else {
        code = EFI_NOT_READY;
        detail = "system-table-prerequisite";
    }
    ia64_test_check(&output, "protocol-locate", located, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    paths_valid = located && test_root_device_paths(&context, &code, &detail);
    ia64_test_check(&output, "root-device-paths", paths_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "device-path-prerequisite";
    resources_valid =
        paths_valid && test_root_configurations(&context, &code, &detail);
    ia64_test_check(&output, "root-configuration", resources_valid, code,
                    detail);

    test_check_prerequisite(&output, "pci-config-isolation", resources_valid,
                            test_pci_config_isolation, &context);
    test_check_prerequisite(&output, "identity-dma", resources_valid,
                            test_identity_dma, &context);
    test_check_prerequisite(&output, "host-allocation", resources_valid,
                            test_host_allocation, &context);

    ia64_test_done(&output);
    return output.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
