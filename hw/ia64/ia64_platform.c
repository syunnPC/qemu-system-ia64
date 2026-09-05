/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Validation for the IA-64 platform descriptor transport.
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_platform.h"
#include "qemu/bswap.h"
#include "qemu/units.h"

typedef struct IA64PlatformRange {
    uint32_t start;
    uint32_t end;
} IA64PlatformRange;

static uint8_t ia64_platform_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t checksum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        checksum += bytes[i];
    }
    return checksum;
}

void ia64_platform_i2000_profile_init(
    IA64PlatformI2000Profile *profile)
{
    QEMU_BUILD_BUG_ON(sizeof(*profile) % 8U != 0);
    QEMU_BUILD_BUG_ON(offsetof(IA64PlatformI2000Profile,
                               Isp12160Capabilities) != 80U);

    memset(profile, 0, sizeof(*profile));
    profile->ProfileType = cpu_to_le32(
        IA64_PLATFORM_PROFILE_TYPE_HP_I2000);
    profile->ProfileRevision = cpu_to_le32(
        IA64_PLATFORM_I2000_PROFILE_REVISION);
    profile->Length = cpu_to_le32(sizeof(*profile));
    profile->Flags = cpu_to_le32(IA64_I2000_PROFILE_REQUIRED_FLAGS);

    profile->SuperIoIndexPort = IA64_I2000_PROFILE_SIO_INDEX_PORT;
    profile->SuperIoDataPort = IA64_I2000_PROFILE_SIO_DATA_PORT;
    profile->SuperIoEnterKey = IA64_I2000_PROFILE_SIO_ENTER_KEY;
    profile->SuperIoEnterCount = IA64_I2000_PROFILE_SIO_ENTER_COUNT;
    profile->SuperIoExitKey = IA64_I2000_PROFILE_SIO_EXIT_KEY;
    profile->SuperIoLdnSelectRegister =
        IA64_I2000_PROFILE_SIO_LDN_SELECT_REGISTER;
    profile->SuperIoDeviceIdRegister =
        IA64_I2000_PROFILE_SIO_DEVICE_ID_REGISTER;
    profile->SuperIoDeviceId = IA64_I2000_PROFILE_SIO_DEVICE_ID;

    profile->UartLdn = IA64_I2000_PROFILE_UART_LDN;
    profile->UartActivateRegister = IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER;
    profile->UartActivateValue = IA64_I2000_PROFILE_SIO_ACTIVE_VALUE;
    profile->UartBaseMsbRegister =
        IA64_I2000_PROFILE_UART_BASE_MSB_REGISTER;
    profile->UartBaseMsbValue = IA64_I2000_PROFILE_UART_PORT >> 8;
    profile->UartBaseLsbRegister =
        IA64_I2000_PROFILE_UART_BASE_LSB_REGISTER;
    profile->UartBaseLsbValue = IA64_I2000_PROFILE_UART_PORT & 0xffU;
    profile->UartIrqRegister = IA64_I2000_PROFILE_UART_IRQ_REGISTER;
    profile->UartIrqValue = IA64_I2000_PROFILE_UART_IRQ;
    profile->UartModeRegister = IA64_I2000_PROFILE_UART_MODE_REGISTER;
    profile->UartModeValue = IA64_I2000_PROFILE_UART_MODE_VALUE;

    profile->I8042Ldn = IA64_I2000_PROFILE_I8042_LDN;
    profile->I8042ActivateRegister = IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER;
    profile->I8042ActivateValue = IA64_I2000_PROFILE_SIO_ACTIVE_VALUE;
    profile->I8042KeyboardIrqRegister =
        IA64_I2000_PROFILE_I8042_KBD_IRQ_REGISTER;
    profile->I8042KeyboardIrqValue = IA64_I2000_PROFILE_I8042_KBD_IRQ;
    profile->I8042MouseIrqRegister =
        IA64_I2000_PROFILE_I8042_MOUSE_IRQ_REGISTER;
    profile->I8042MouseIrqValue = IA64_I2000_PROFILE_I8042_MOUSE_IRQ;

    profile->UartPort = cpu_to_le16(IA64_I2000_PROFILE_UART_PORT);
    profile->UartSize = IA64_I2000_PROFILE_UART_SIZE;
    profile->UartIrq = IA64_I2000_PROFILE_UART_IRQ;
    profile->UartInputClockHz = cpu_to_le32(
        IA64_I2000_PROFILE_UART_INPUT_CLOCK_HZ);
    profile->I8042DataPort = cpu_to_le16(IA64_I2000_PROFILE_I8042_DATA_PORT);
    profile->I8042CommandPort = cpu_to_le16(
        IA64_I2000_PROFILE_I8042_COMMAND_PORT);
    profile->I8042PortSize = IA64_I2000_PROFILE_I8042_PORT_SIZE;
    profile->I8042KeyboardIrq = IA64_I2000_PROFILE_I8042_KBD_IRQ;
    profile->I8042MouseIrq = IA64_I2000_PROFILE_I8042_MOUSE_IRQ;

    profile->IdeSegment = cpu_to_le16(IA64_I2000_PROFILE_IDE_SEGMENT);
    profile->IdeCommandPort = cpu_to_le16(
        IA64_I2000_PROFILE_IDE_COMMAND_PORT);
    profile->IdeControlPort = cpu_to_le16(
        IA64_I2000_PROFILE_IDE_CONTROL_PORT);
    profile->IdeVendorId = cpu_to_le16(IA64_I2000_PROFILE_IDE_VENDOR_ID);
    profile->IdeDeviceId = cpu_to_le16(IA64_I2000_PROFILE_IDE_DEVICE_ID);
    profile->IdeClass = cpu_to_le16(IA64_I2000_PROFILE_IDE_CLASS);
    profile->IdeBus = IA64_I2000_PROFILE_IDE_BUS;
    profile->IdeDevice = IA64_I2000_PROFILE_IDE_DEVICE;
    profile->IdeFunction = IA64_I2000_PROFILE_IDE_FUNCTION;
    profile->IdeProgIf = IA64_I2000_PROFILE_IDE_PROG_IF;
    profile->IdeIrq = IA64_I2000_PROFILE_IDE_IRQ;
    profile->IdeUnitMask = IA64_I2000_PROFILE_IDE_PRIMARY_MASTER_UNIT_MASK;
    profile->IdeCommandSize = IA64_I2000_PROFILE_IDE_COMMAND_SIZE;
    profile->IdeControlSize = IA64_I2000_PROFILE_IDE_CONTROL_SIZE;
    profile->Isp12160Capabilities = cpu_to_le32(
        ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES);
}

static bool ia64_platform_array_range(uint32_t offset, uint32_t count,
                                      uint32_t stride, uint32_t minimum_size,
                                      uint32_t header_size,
                                      uint32_t total_size,
                                      IA64PlatformRange *range,
                                      const char *name, Error **errp)
{
    uint64_t end;

    if (count == 0) {
        if (offset != 0 || stride != 0) {
            error_setg(errp,
                       "%s offset or entry size is nonzero for an empty "
                       "array", name);
            return false;
        }
        range->start = range->end = 0;
        return true;
    }
    if (offset < header_size || (offset & 7U) != 0) {
        error_setg(errp, "%s array is inside the header or misaligned", name);
        return false;
    }
    if (stride < minimum_size || (stride & 7U) != 0) {
        error_setg(errp, "%s entry size is too small or misaligned", name);
        return false;
    }

    end = (uint64_t)offset + (uint64_t)count * stride;
    if (end > total_size) {
        error_setg(errp, "%s array exceeds the descriptor", name);
        return false;
    }
    range->start = offset;
    range->end = end;
    return true;
}

static bool ia64_platform_ranges_overlap(const IA64PlatformRange *a,
                                         const IA64PlatformRange *b)
{
    return a->start != a->end && b->start != b->end &&
           a->start < b->end && b->start < a->end;
}

static bool ia64_platform_u64_range_valid(uint64_t base, uint64_t size)
{
    /* Consumers use base + size as an exclusive end. */
    return size != 0 && base <= UINT64_MAX - size;
}

static bool ia64_platform_u64_rounded_end_valid(uint64_t base, uint64_t size,
                                                uint64_t alignment)
{
    uint64_t end;

    if (!ia64_platform_u64_range_valid(base, size)) {
        return false;
    }
    end = base + size;

    /* QEMU_ALIGN_UP(end, alignment) must remain representable. */
    return end <= UINT64_MAX - (alignment - 1U);
}

bool ia64_platform_desc_mapping_valid(hwaddr gpa, size_t descriptor_size)
{
    return gpa != 0 &&
           descriptor_size >= sizeof(IA64PlatformDescriptor) &&
           descriptor_size <= IA64_PLATFORM_DESC_MAX_SIZE &&
           (gpa & (IA64_PLATFORM_DESC_ALIGNMENT - 1U)) == 0 &&
           ia64_platform_u64_rounded_end_valid(
               gpa, IA64_PLATFORM_DESC_MAX_SIZE,
               IA64_PLATFORM_RESOURCE_ALIGNMENT);
}

static bool ia64_platform_optional_u64_range_valid(uint64_t base,
                                                   uint64_t size)
{
    return size == 0 ? base == 0 :
           ia64_platform_u64_range_valid(base, size);
}

static bool ia64_platform_u64_ranges_overlap(uint64_t first_base,
                                             uint64_t first_size,
                                             uint64_t second_base,
                                             uint64_t second_size)
{
    return first_size != 0 && second_size != 0 &&
           first_base < second_base + second_size &&
           second_base < first_base + first_size;
}

static bool ia64_platform_optional_range_aligned(uint64_t base, uint64_t size,
                                                 uint64_t alignment)
{
    return ia64_platform_optional_u64_range_valid(base, size) &&
           (size == 0 || ((base | size) & (alignment - 1U)) == 0);
}

static const IA64PlatformRamRange *ia64_platform_ram_range(
    const IA64PlatformDescriptor *descriptor, uint32_t index)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;

    return (const IA64PlatformRamRange *)(
        bytes + le32_to_cpu(descriptor->RamRangeOffset) +
        index * le32_to_cpu(descriptor->RamRangeEntrySize));
}

bool ia64_platform_desc_mapping_in_ram(
    const IA64PlatformDescriptor *descriptor, hwaddr gpa,
    size_t descriptor_size)
{
    uint64_t start;
    uint64_t end;
    uint64_t descriptor_end;
    uint32_t count;
    uint32_t i;

    if (descriptor == NULL ||
        descriptor_size != le32_to_cpu(descriptor->TotalSize) ||
        !ia64_platform_desc_mapping_valid(gpa, descriptor_size)) {
        return false;
    }
    start = gpa & ~(uint64_t)(IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U);
    descriptor_end = gpa + descriptor_size;
    end = QEMU_ALIGN_UP(descriptor_end, IA64_PLATFORM_RESOURCE_ALIGNMENT);
    count = le32_to_cpu(descriptor->RamRangeCount);

    for (i = 0; i < count; i++) {
        const IA64PlatformRamRange *range = ia64_platform_ram_range(
            descriptor, i);
        uint64_t base = le64_to_cpu(range->Base);
        uint64_t size = le64_to_cpu(range->Size);

        if (start >= base && end <= base + size) {
            return true;
        }
    }
    return false;
}

static const IA64PlatformPciRoot *ia64_platform_pci_root(
    const IA64PlatformDescriptor *descriptor, uint32_t index)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;

    return (const IA64PlatformPciRoot *)(
        bytes + le32_to_cpu(descriptor->PciRootOffset) +
        index * le32_to_cpu(descriptor->PciRootEntrySize));
}

static const IA64PlatformPciRoute *ia64_platform_pci_route(
    const IA64PlatformDescriptor *descriptor, uint32_t index)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;

    return (const IA64PlatformPciRoute *)(
        bytes + le32_to_cpu(descriptor->PciRouteOffset) +
        index * le32_to_cpu(descriptor->PciRouteEntrySize));
}

static const IA64PlatformI2000Profile *ia64_platform_profile(
    const IA64PlatformDescriptor *descriptor, uint32_t index)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;

    return (const IA64PlatformI2000Profile *)(
        bytes + le32_to_cpu(descriptor->ProfileOffset) +
        index * le32_to_cpu(descriptor->ProfileEntrySize));
}

static const IA64PlatformPciRoot *ia64_platform_root_for_bus(
    const IA64PlatformDescriptor *descriptor, uint16_t segment, uint8_t bus)
{
    uint32_t count = le32_to_cpu(descriptor->PciRootCount);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformPciRoot *root = ia64_platform_pci_root(
            descriptor, i);

        if (le16_to_cpu(root->Segment) == segment &&
            bus >= root->Bus && bus <= root->BusEnd) {
            return root;
        }
    }
    return NULL;
}

static bool ia64_platform_root_bus_present(
    const IA64PlatformDescriptor *descriptor, uint16_t segment, uint8_t bus)
{
    uint32_t count = le32_to_cpu(descriptor->PciRootCount);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformPciRoot *root = ia64_platform_pci_root(
            descriptor, i);

        /* Routes are _PRT-style entries in the root-bus slot namespace. */
        if (le16_to_cpu(root->Segment) == segment && bus == root->Bus) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_policy_valid(
    const IA64PlatformDescriptor *descriptor, Error **errp)
{
    uint32_t root_count = le32_to_cpu(descriptor->PciRootCount);
    uint32_t device_count = le32_to_cpu(descriptor->OnboardDeviceCount);
    uint32_t node_count = le32_to_cpu(descriptor->NumaNodeCount);
    uint32_t processor_count = le32_to_cpu(descriptor->ProcessorCount);
    uint32_t ram_count = le32_to_cpu(descriptor->RamRangeCount);
    uint32_t max_roots = le32_to_cpu(descriptor->MaxPciRoots);
    uint32_t processor_cursor = 0;
    uint32_t ram_mask = 0;
    uint32_t i;

    if (max_roots == 0 || max_roots > IA64_PLATFORM_MAX_PCI_ROOTS ||
        root_count > max_roots ||
        le32_to_cpu(descriptor->PciRootIdentity) >
            IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX ||
        device_count > IA64_PLATFORM_MAX_ONBOARD_DEVICES ||
        node_count == 0 || node_count > IA64_PLATFORM_MAX_NUMA_NODES) {
        error_setg(errp, "invalid IA-64 platform policy limits");
        return false;
    }

    for (i = 0; i < device_count; i++) {
        const IA64PlatformOnboardDevice *device =
            &descriptor->OnboardDevice[i];

        if (device->Type < IA64_PLATFORM_ONBOARD_GRAPHICS ||
            device->Type > IA64_PLATFORM_ONBOARD_UART ||
            device->Device >= 32 || device->Function >= 8 ||
            !((device->Bar < 6 && le64_to_cpu(device->BarSize) != 0) ||
              (device->Bar == UINT8_MAX && device->BarSize == 0)) ||
            device->Reserved0 != 0 || device->Reserved1 != 0 ||
            device->Flags != 0 || device->VendorDeviceId == 0 ||
            device->VendorDeviceId == UINT32_MAX ||
            (le32_to_cpu(device->ClassCode) & 0xff000000U) != 0 ||
            !ia64_platform_root_for_bus(
                descriptor, le16_to_cpu(device->Segment), device->Bus)) {
            error_setg(errp, "invalid IA-64 onboard device %u", i);
            return false;
        }
    }

    for (i = 0; i < node_count; i++) {
        const IA64PlatformNumaNode *node = &descriptor->NumaNode[i];
        uint32_t count = le32_to_cpu(node->ProcessorCount);
        uint32_t node_ram_mask = le32_to_cpu(node->RamRangeMask);
        uint32_t j;

        if (le32_to_cpu(node->ProximityDomain) != i ||
            le32_to_cpu(node->ProcessorStart) != processor_cursor ||
            count == 0 || count > processor_count - processor_cursor ||
            (node_ram_mask & ram_mask) != 0 ||
            (node_ram_mask >> ram_count) != 0) {
            error_setg(errp, "invalid IA-64 NUMA node %u", i);
            return false;
        }
        for (j = 0; j < sizeof(node->Reserved); j++) {
            if (node->Reserved[j] != 0) {
                error_setg(errp, "invalid IA-64 NUMA node reserved data");
                return false;
            }
        }
        for (j = 0; j < node_count; j++) {
            uint8_t distance = node->Distance[j];

            if ((i == j && distance != 10) ||
                (i != j && distance < 10) ||
                distance != descriptor->NumaNode[j].Distance[i]) {
                error_setg(errp, "invalid IA-64 NUMA distance matrix");
                return false;
            }
        }
        processor_cursor += count;
        ram_mask |= node_ram_mask;
    }
    if (processor_cursor != processor_count ||
        ram_mask != ((1U << ram_count) - 1U)) {
        error_setg(errp, "incomplete IA-64 NUMA affinity map");
        return false;
    }
    return true;
}

static bool ia64_platform_pci_route_present(
    const IA64PlatformDescriptor *descriptor, uint16_t segment, uint8_t bus,
    uint8_t device, uint8_t pin, uint32_t gsi)
{
    uint32_t count = le32_to_cpu(descriptor->PciRouteCount);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformPciRoute *route = ia64_platform_pci_route(
            descriptor, i);

        if (le16_to_cpu(route->Segment) == segment &&
            route->Bus == bus && route->Device == device &&
            route->Pin == pin && le32_to_cpu(route->Gsi) == gsi) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_translate_range(uint64_t base, uint64_t size,
                                          uint64_t raw_offset,
                                          uint64_t *cpu_base)
{
    uint64_t magnitude;

    if (size == 0) {
        *cpu_base = 0;
        return base == 0 && raw_offset == 0;
    }
    if (!ia64_platform_u64_range_valid(base, size)) {
        return false;
    }

    if ((raw_offset & (1ULL << 63)) == 0) {
        if (base > UINT64_MAX - raw_offset) {
            return false;
        }
        *cpu_base = base + raw_offset;
    } else {
        magnitude = 0 - raw_offset;
        if (base < magnitude) {
            return false;
        }
        *cpu_base = base - magnitude;
    }
    return ia64_platform_u64_range_valid(*cpu_base, size);
}

static bool ia64_platform_range_overlaps_ram(
    const IA64PlatformDescriptor *descriptor, uint64_t base, uint64_t size)
{
    uint32_t count = le32_to_cpu(descriptor->RamRangeCount);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformRamRange *range = ia64_platform_ram_range(
            descriptor, i);

        if (ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(range->Base),
                le64_to_cpu(range->Size))) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_range_in_single_ram(
    const IA64PlatformDescriptor *descriptor, uint64_t base, uint64_t size)
{
    uint32_t count = le32_to_cpu(descriptor->RamRangeCount);
    uint32_t i;

    if (!ia64_platform_u64_range_valid(base, size)) {
        return false;
    }

    for (i = 0; i < count; i++) {
        const IA64PlatformRamRange *range = ia64_platform_ram_range(
            descriptor, i);
        uint64_t ram_base = le64_to_cpu(range->Base);
        uint64_t ram_size = le64_to_cpu(range->Size);
        uint64_t offset;

        if (base < ram_base) {
            continue;
        }
        offset = base - ram_base;
        if (offset < ram_size && size <= ram_size - offset) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_range_overlaps_fixed(
    const IA64PlatformDescriptor *descriptor, uint64_t base, uint64_t size)
{
    uint64_t console_size = 8 * (uint64_t)le32_to_cpu(
        descriptor->ConsoleRegisterStride);

    return ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->FirmwareBase),
               le64_to_cpu(descriptor->FirmwareSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->LegacyIoBase),
               le64_to_cpu(descriptor->LegacyIoSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->LocalSapicBase),
               le64_to_cpu(descriptor->LocalSapicSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->ConsoleBase),
               console_size) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->NvramBase),
               le64_to_cpu(descriptor->NvramSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->RtcBase),
               le64_to_cpu(descriptor->RtcSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->ControlBase),
               le64_to_cpu(descriptor->ControlSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->AcpiPmBase),
               le64_to_cpu(descriptor->AcpiPmSize)) ||
           ia64_platform_u64_ranges_overlap(
               base, size, le64_to_cpu(descriptor->RasBase),
               le64_to_cpu(descriptor->RasSize));
}

static bool ia64_platform_range_overlaps_io_sapic(
    const IA64PlatformDescriptor *descriptor, uint64_t base, uint64_t size)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;
    uint32_t offset = le32_to_cpu(descriptor->IoSapicOffset);
    uint32_t count = le32_to_cpu(descriptor->IoSapicCount);
    uint32_t stride = le32_to_cpu(descriptor->IoSapicEntrySize);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformIoSapic *sapic =
            (const IA64PlatformIoSapic *)(bytes + offset + i * stride);
        uint64_t sapic_base = le64_to_cpu(sapic->Base);
        uint64_t efi_base = sapic_base &
            ~(uint64_t)(IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U);

        if (ia64_platform_u64_ranges_overlap(
                base, size, efi_base,
                IA64_PLATFORM_RESOURCE_ALIGNMENT)) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_zx1_io_sapic_embedded(
    const IA64PlatformDescriptor *descriptor, uint64_t sapic_base)
{
    uint32_t count = le32_to_cpu(descriptor->PciRootCount);
    uint32_t i;

    QEMU_BUILD_BUG_ON(IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET +
                      IA64_PLATFORM_IO_SAPIC_SIZE >
                      IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE);

    if (!(le32_to_cpu(descriptor->Flags) &
          IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC)) {
        return false;
    }
    for (i = 0; i < count; i++) {
        const IA64PlatformPciRoot *root = ia64_platform_pci_root(
            descriptor, i);

        if (root->ConfigType == IA64_PLATFORM_PCI_CONFIG_ZX1_LBA &&
            ia64_platform_zx1_embedded_io_sapic(
                le64_to_cpu(root->ConfigBase), sapic_base)) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_root_config_io_sapics_valid(
    const IA64PlatformDescriptor *descriptor,
    const IA64PlatformPciRoot *root, uint64_t config_base,
    uint64_t config_size)
{
    bool embedded_io_sapic =
        le32_to_cpu(descriptor->Flags) &
        IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC;
    const uint8_t *bytes = (const uint8_t *)descriptor;
    uint32_t offset = le32_to_cpu(descriptor->IoSapicOffset);
    uint32_t count = le32_to_cpu(descriptor->IoSapicCount);
    uint32_t stride = le32_to_cpu(descriptor->IoSapicEntrySize);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformIoSapic *sapic =
            (const IA64PlatformIoSapic *)(bytes + offset + i * stride);
        uint64_t sapic_base = le64_to_cpu(sapic->Base);
        uint64_t efi_base = sapic_base &
            ~(uint64_t)(IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U);

        if (!ia64_platform_u64_ranges_overlap(
                config_base, config_size,
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT)) {
            continue;
        }
        if (!embedded_io_sapic) {
            return false;
        }
        if (root->ConfigType != IA64_PLATFORM_PCI_CONFIG_ZX1_LBA ||
            !ia64_platform_zx1_embedded_io_sapic(config_base,
                                                  sapic_base)) {
            return false;
        }
    }
    return true;
}

static bool ia64_platform_gsi_present(
    const IA64PlatformDescriptor *descriptor, uint32_t gsi)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;
    uint32_t offset = le32_to_cpu(descriptor->IoSapicOffset);
    uint32_t count = le32_to_cpu(descriptor->IoSapicCount);
    uint32_t stride = le32_to_cpu(descriptor->IoSapicEntrySize);
    uint32_t i;

    for (i = 0; i < count; i++) {
        const IA64PlatformIoSapic *sapic =
            (const IA64PlatformIoSapic *)(bytes + offset + i * stride);
        uint32_t base = le32_to_cpu(sapic->GsiBase);
        uint32_t entries = le32_to_cpu(sapic->RedirectionEntries);

        if (gsi >= base && gsi - base < entries) {
            return true;
        }
    }
    return false;
}

static bool ia64_platform_desc_validate_entries(
    const IA64PlatformDescriptor *descriptor, Error **errp)
{
    const uint8_t *bytes = (const uint8_t *)descriptor;
    uint32_t ram_count = le32_to_cpu(descriptor->RamRangeCount);
    uint32_t root_offset = le32_to_cpu(descriptor->PciRootOffset);
    uint32_t root_count = le32_to_cpu(descriptor->PciRootCount);
    uint32_t root_stride = le32_to_cpu(descriptor->PciRootEntrySize);
    uint32_t sapic_offset = le32_to_cpu(descriptor->IoSapicOffset);
    uint32_t sapic_count = le32_to_cpu(descriptor->IoSapicCount);
    uint32_t sapic_stride = le32_to_cpu(descriptor->IoSapicEntrySize);
    uint32_t route_offset = le32_to_cpu(descriptor->PciRouteOffset);
    uint32_t route_count = le32_to_cpu(descriptor->PciRouteCount);
    uint32_t route_stride = le32_to_cpu(descriptor->PciRouteEntrySize);
    uint32_t platform_flags = le32_to_cpu(descriptor->Flags);
    uint32_t physical_address_bits =
        le32_to_cpu(descriptor->PhysicalAddressBits);
    uint64_t ram_size = le64_to_cpu(descriptor->RamSize);
    uint64_t low_ram_end = le64_to_cpu(descriptor->LowRamEnd);
    uint64_t ram_total = 0;
    uint64_t previous_end = 0;
    uint32_t ecam_count = 0;
    uint32_t i;
    uint32_t j;

    if (physical_address_bits < 32 || physical_address_bits >= 64 ||
        ram_count == 0 || ram_count > IA64_PLATFORM_MAX_RAM_RANGES ||
        root_count == 0 || root_count > IA64_PLATFORM_MAX_PCI_ROOTS ||
        sapic_count == 0 || sapic_count > IA64_PLATFORM_MAX_IO_SAPICS ||
        route_count > IA64_PLATFORM_MAX_PCI_ROUTES) {
        error_setg(errp,
                   "HP IA-64 descriptor needs RAM, PCI and I/O SAPIC roots");
        return false;
    }

    for (i = 0; i < ram_count; i++) {
        const IA64PlatformRamRange *range = ia64_platform_ram_range(
            descriptor, i);
        uint64_t base = le64_to_cpu(range->Base);
        uint64_t size = le64_to_cpu(range->Size);

        if (!ia64_platform_u64_range_valid(base, size) ||
            ((base | size) & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
            (i == 0 && (base != 0 || size != low_ram_end)) ||
            (i != 0 && base < previous_end) ||
            ram_total > UINT64_MAX - size) {
            error_setg(errp, "invalid IA-64 platform RAM range");
            return false;
        }
        previous_end = base + size;
        ram_total += size;

        if (ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->LegacyIoBase),
                le64_to_cpu(descriptor->LegacyIoSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->LocalSapicBase),
                le64_to_cpu(descriptor->LocalSapicSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->ConsoleBase),
                8 * (uint64_t)le32_to_cpu(
                    descriptor->ConsoleRegisterStride)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->NvramBase),
                le64_to_cpu(descriptor->NvramSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->RtcBase),
                le64_to_cpu(descriptor->RtcSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->ControlBase),
                le64_to_cpu(descriptor->ControlSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->AcpiPmBase),
                le64_to_cpu(descriptor->AcpiPmSize)) ||
            ia64_platform_u64_ranges_overlap(
                base, size, le64_to_cpu(descriptor->RasBase),
                le64_to_cpu(descriptor->RasSize))) {
            error_setg(errp,
                       "IA-64 platform RAM overlaps a fixed resource");
            return false;
        }
    }
    if (ram_total != ram_size) {
        error_setg(errp, "IA-64 platform RAM ranges do not match RAM size");
        return false;
    }

    for (i = 0; i < root_count; i++) {
        const IA64PlatformPciRoot *root =
            (const IA64PlatformPciRoot *)(bytes + root_offset +
                                          i * root_stride);
        uint8_t config_type = root->ConfigType;
        uint16_t segment = le16_to_cpu(root->Segment);
        uint64_t io_base = le64_to_cpu(root->IoBase);
        uint64_t io_size = le64_to_cpu(root->IoSize);
        uint64_t mmio32_base = le64_to_cpu(root->Mmio32Base);
        uint64_t mmio32_size = le64_to_cpu(root->Mmio32Size);
        uint64_t mmio64_base = le64_to_cpu(root->Mmio64Base);
        uint64_t mmio64_size = le64_to_cpu(root->Mmio64Size);
        uint64_t dma_base = le64_to_cpu(root->DmaBase);
        uint64_t dma_size = le64_to_cpu(root->DmaSize);
        uint64_t config_base = le64_to_cpu(root->ConfigBase);
        uint64_t io_translation =
            le64_to_cpu(root->IoTranslationOffset);
        uint64_t mmio32_translation =
            le64_to_cpu(root->Mmio32TranslationOffset);
        uint64_t mmio64_translation =
            le64_to_cpu(root->Mmio64TranslationOffset);
        uint64_t cpu_mmio32_base = 0;
        uint64_t cpu_mmio64_base = 0;
        uint64_t config_size = ia64_platform_pci_config_size(
            config_type, root->Bus, root->BusEnd);
        uint64_t config_offset = ia64_platform_pci_config_offset(
            config_type, root->Bus);
        uint64_t config_window_base = config_base + config_offset;
        uint32_t flags = le32_to_cpu(root->Flags);
        uint32_t rope = le32_to_cpu(root->Rope);

        if (config_type != IA64_PLATFORM_PCI_CONFIG_CF8_CFC &&
            config_type != IA64_PLATFORM_PCI_CONFIG_ZX1_LBA &&
            config_type != IA64_PLATFORM_PCI_CONFIG_ECAM) {
            error_setg(errp, "unsupported IA-64 PCI configuration backend");
            return false;
        }
        if ((config_type == IA64_PLATFORM_PCI_CONFIG_CF8_CFC &&
             !(platform_flags & IA64_PLATFORM_FLAG_PCI_CF8)) ||
            (config_type == IA64_PLATFORM_PCI_CONFIG_ZX1_LBA &&
             !(platform_flags & IA64_PLATFORM_FLAG_PCI_ZX1_LBA)) ||
            (config_type == IA64_PLATFORM_PCI_CONFIG_ECAM &&
             !(platform_flags & IA64_PLATFORM_FLAG_PCI_ECAM))) {
            error_setg(errp,
                       "IA-64 PCI configuration backend does not match "
                       "the platform");
            return false;
        }
        if (root->BusEnd < root->Bus ||
            (flags & ~IA64_PLATFORM_PCI_ROOT_KNOWN_FLAGS) != 0 ||
            root->Reserved[0] != 0 || root->Reserved[1] != 0 ||
            root->Reserved[2] != 0 ||
            (config_type == IA64_PLATFORM_PCI_CONFIG_CF8_CFC &&
             (segment != 0 || config_base != 0 || rope != 0)) ||
            (config_type == IA64_PLATFORM_PCI_CONFIG_ZX1_LBA &&
             (config_base == 0 ||
             (config_base & (IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE - 1U)) != 0 ||
              config_base >
                  (1ULL << physical_address_bits) -
                  IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE ||
              rope > 7)) ||
            (config_type == IA64_PLATFORM_PCI_CONFIG_ECAM &&
             ((config_base &
               (IA64_PLATFORM_PCI_ECAM_ALIGNMENT - 1U)) != 0 ||
              config_base >
                  (1ULL << physical_address_bits) -
                  config_offset - config_size))) {
            error_setg(errp, "invalid IA-64 PCI root identity");
            return false;
        }
        if (config_type == IA64_PLATFORM_PCI_CONFIG_ECAM) {
            ecam_count++;
        }
        if (!ia64_platform_optional_u64_range_valid(io_base, io_size) ||
            ((flags & IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO) != 0 ?
             (!(platform_flags & IA64_PLATFORM_FLAG_SPARSE_IO) ||
              io_size == 0 ||
              io_translation != le64_to_cpu(descriptor->LegacyIoBase)) :
             io_translation != 0) ||
            (io_size != 0 && io_base + io_size > 0x10000ULL) ||
            !ia64_platform_optional_range_aligned(
                mmio32_base, mmio32_size,
                IA64_PLATFORM_RESOURCE_ALIGNMENT) ||
            !ia64_platform_optional_range_aligned(
                mmio64_base, mmio64_size,
                IA64_PLATFORM_RESOURCE_ALIGNMENT) ||
            !ia64_platform_optional_range_aligned(
                dma_base, dma_size, IA64_PLATFORM_DESC_ALIGNMENT) ||
            ((flags & IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA) != 0 &&
             !ia64_platform_range_in_single_ram(
                 descriptor, dma_base, dma_size)) ||
            (mmio32_size != 0 &&
             mmio32_base + mmio32_size > 0x100000000ULL) ||
            (mmio64_size != 0 && mmio64_base < 0x100000000ULL) ||
            ((mmio32_translation | mmio64_translation) &
             (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
            !ia64_platform_translate_range(
                mmio32_base, mmio32_size, mmio32_translation,
                &cpu_mmio32_base) ||
            !ia64_platform_translate_range(
                mmio64_base, mmio64_size, mmio64_translation,
                &cpu_mmio64_base) ||
            ia64_platform_range_overlaps_ram(
                descriptor, cpu_mmio32_base, mmio32_size) ||
            ia64_platform_range_overlaps_ram(
                descriptor, cpu_mmio64_base, mmio64_size) ||
            ia64_platform_range_overlaps_fixed(
                descriptor, cpu_mmio32_base, mmio32_size) ||
            ia64_platform_range_overlaps_fixed(
                descriptor, cpu_mmio64_base, mmio64_size) ||
            ia64_platform_range_overlaps_io_sapic(
                descriptor, cpu_mmio32_base, mmio32_size) ||
            ia64_platform_range_overlaps_io_sapic(
                descriptor, cpu_mmio64_base, mmio64_size) ||
            (config_size != 0 &&
             (ia64_platform_range_overlaps_ram(
                  descriptor, config_window_base, config_size) ||
              ia64_platform_range_overlaps_fixed(
                  descriptor, config_window_base, config_size) ||
              !ia64_platform_root_config_io_sapics_valid(
                  descriptor, root, config_window_base, config_size))) ||
            ia64_platform_u64_ranges_overlap(
                cpu_mmio32_base, mmio32_size,
                cpu_mmio64_base, mmio64_size) ||
            ia64_platform_u64_ranges_overlap(
                config_window_base, config_size,
                cpu_mmio32_base, mmio32_size) ||
            ia64_platform_u64_ranges_overlap(
                config_window_base, config_size,
                cpu_mmio64_base, mmio64_size)) {
            error_setg(errp, "invalid IA-64 PCI root aperture");
            return false;
        }
        for (j = 0; j < i; j++) {
            const IA64PlatformPciRoot *other =
                (const IA64PlatformPciRoot *)(bytes + root_offset +
                                              j * root_stride);
            uint64_t other_io_base = le64_to_cpu(other->IoBase);
            uint64_t other_io_size = le64_to_cpu(other->IoSize);
            uint64_t other_mmio32_size = le64_to_cpu(other->Mmio32Size);
            uint64_t other_mmio64_size = le64_to_cpu(other->Mmio64Size);
            uint64_t other_cpu_mmio32_base;
            uint64_t other_cpu_mmio64_base;
            uint64_t other_config_base = le64_to_cpu(other->ConfigBase);
            uint64_t other_config_size = ia64_platform_pci_config_size(
                other->ConfigType, other->Bus, other->BusEnd);
            uint64_t other_config_window_base = other_config_base +
                ia64_platform_pci_config_offset(other->ConfigType,
                                                other->Bus);

            if (!ia64_platform_translate_range(
                    le64_to_cpu(other->Mmio32Base), other_mmio32_size,
                    le64_to_cpu(other->Mmio32TranslationOffset),
                    &other_cpu_mmio32_base) ||
                !ia64_platform_translate_range(
                    le64_to_cpu(other->Mmio64Base), other_mmio64_size,
                    le64_to_cpu(other->Mmio64TranslationOffset),
                    &other_cpu_mmio64_base)) {
                error_setg(errp, "invalid prior IA-64 PCI root aperture");
                return false;
            }

            if (root->Segment == other->Segment &&
                root->Bus <= other->BusEnd && other->Bus <= root->BusEnd) {
                error_setg(errp, "overlapping IA-64 PCI root bus ranges");
                return false;
            }
            if (ia64_platform_u64_ranges_overlap(
                    io_base, io_size, other_io_base, other_io_size)) {
                error_setg(errp,
                           "overlapping IA-64 PCI root I/O port ranges");
                return false;
            }
            if (ia64_platform_u64_ranges_overlap(
                    cpu_mmio32_base, mmio32_size,
                    other_cpu_mmio32_base, other_mmio32_size) ||
                ia64_platform_u64_ranges_overlap(
                    cpu_mmio32_base, mmio32_size,
                    other_cpu_mmio64_base, other_mmio64_size) ||
                ia64_platform_u64_ranges_overlap(
                    cpu_mmio64_base, mmio64_size,
                    other_cpu_mmio32_base, other_mmio32_size) ||
                ia64_platform_u64_ranges_overlap(
                    cpu_mmio64_base, mmio64_size,
                    other_cpu_mmio64_base, other_mmio64_size) ||
                ia64_platform_u64_ranges_overlap(
                    config_window_base, config_size,
                    other_config_window_base, other_config_size) ||
                ia64_platform_u64_ranges_overlap(
                    config_window_base, config_size,
                    other_cpu_mmio32_base, other_mmio32_size) ||
                ia64_platform_u64_ranges_overlap(
                    config_window_base, config_size,
                    other_cpu_mmio64_base, other_mmio64_size) ||
                ia64_platform_u64_ranges_overlap(
                    cpu_mmio32_base, mmio32_size,
                    other_config_window_base, other_config_size) ||
                ia64_platform_u64_ranges_overlap(
                    cpu_mmio64_base, mmio64_size,
                    other_config_window_base, other_config_size)) {
                error_setg(errp,
                           "overlapping IA-64 PCI root CPU MMIO ranges");
                return false;
            }
        }
    }

    for (i = 0; i < sapic_count; i++) {
        const IA64PlatformIoSapic *sapic =
            (const IA64PlatformIoSapic *)(bytes + sapic_offset +
                                          i * sapic_stride);
        uint64_t base = le64_to_cpu(sapic->Base);
        uint64_t efi_base = base &
            ~(uint64_t)(IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U);
        uint64_t legacy_io_base = le64_to_cpu(descriptor->LegacyIoBase);
        uint64_t legacy_io_size = le64_to_cpu(descriptor->LegacyIoSize);
        uint64_t local_sapic_base = le64_to_cpu(descriptor->LocalSapicBase);
        uint64_t local_sapic_size = le64_to_cpu(descriptor->LocalSapicSize);
        uint64_t console_base = le64_to_cpu(descriptor->ConsoleBase);
        uint64_t console_size =
            8 * (uint64_t)le32_to_cpu(descriptor->ConsoleRegisterStride);
        uint64_t firmware_base = le64_to_cpu(descriptor->FirmwareBase);
        uint64_t firmware_size = le64_to_cpu(descriptor->FirmwareSize);
        uint64_t nvram_base = le64_to_cpu(descriptor->NvramBase);
        uint64_t nvram_size = le64_to_cpu(descriptor->NvramSize);
        uint64_t rtc_base = le64_to_cpu(descriptor->RtcBase);
        uint64_t rtc_size = le64_to_cpu(descriptor->RtcSize);
        uint64_t control_base = le64_to_cpu(descriptor->ControlBase);
        uint64_t control_size = le64_to_cpu(descriptor->ControlSize);
        uint64_t acpi_pm_base = le64_to_cpu(descriptor->AcpiPmBase);
        uint64_t acpi_pm_size = le64_to_cpu(descriptor->AcpiPmSize);
        uint64_t ras_base = le64_to_cpu(descriptor->RasBase);
        uint64_t ras_size = le64_to_cpu(descriptor->RasSize);
        uint32_t gsi_base = le32_to_cpu(sapic->GsiBase);
        uint32_t entries = le32_to_cpu(sapic->RedirectionEntries);

        if (base == 0 ||
            ((base & (IA64_PLATFORM_IO_SAPIC_ALIGNMENT - 1U)) != 0 &&
             !ia64_platform_zx1_io_sapic_embedded(descriptor, base)) ||
            !ia64_platform_u64_rounded_end_valid(
                base, IA64_PLATFORM_IO_SAPIC_SIZE,
                IA64_PLATFORM_RESOURCE_ALIGNMENT) ||
            entries == 0 || entries > 256 ||
            le32_to_cpu(sapic->Version) == 0 ||
            sapic->Id > IA64_PLATFORM_IO_SAPIC_MAX_ID ||
            sapic->Reserved[0] != 0 || sapic->Reserved[1] != 0 ||
            sapic->Reserved[2] != 0 ||
            gsi_base > UINT32_MAX - (entries - 1)) {
            error_setg(errp, "invalid IA-64 I/O SAPIC description");
            return false;
        }
        if (ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                legacy_io_base, legacy_io_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                local_sapic_base, local_sapic_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                console_base, console_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                firmware_base, firmware_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                nvram_base, nvram_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                rtc_base, rtc_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                control_base, control_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                acpi_pm_base, acpi_pm_size) ||
            ia64_platform_u64_ranges_overlap(
                efi_base, IA64_PLATFORM_RESOURCE_ALIGNMENT,
                ras_base, ras_size)) {
            error_setg(errp,
                       "IA-64 I/O SAPIC overlaps a fixed platform resource");
            return false;
        }
        if (ia64_platform_range_overlaps_ram(
                descriptor, efi_base,
                IA64_PLATFORM_RESOURCE_ALIGNMENT)) {
            error_setg(errp, "IA-64 I/O SAPIC overlaps platform RAM");
            return false;
        }
        for (j = 0; j < i; j++) {
            const IA64PlatformIoSapic *other =
                (const IA64PlatformIoSapic *)(bytes + sapic_offset +
                                              j * sapic_stride);
            uint64_t other_mmio_base = le64_to_cpu(other->Base);
            uint32_t other_base = le32_to_cpu(other->GsiBase);
            uint32_t other_entries =
                le32_to_cpu(other->RedirectionEntries);

            if (ia64_platform_u64_ranges_overlap(
                    base, IA64_PLATFORM_IO_SAPIC_SIZE,
                    other_mmio_base, IA64_PLATFORM_IO_SAPIC_SIZE)) {
                error_setg(errp,
                           "overlapping IA-64 I/O SAPIC MMIO ranges");
                return false;
            }
            if ((uint64_t)gsi_base <
                    (uint64_t)other_base + other_entries &&
                (uint64_t)other_base < (uint64_t)gsi_base + entries) {
                error_setg(errp, "overlapping IA-64 I/O SAPIC GSI ranges");
                return false;
            }
            if (sapic->Id == other->Id) {
                error_setg(errp, "duplicate IA-64 I/O SAPIC ID");
                return false;
            }
        }
    }

    for (i = 0; i < route_count; i++) {
        const IA64PlatformPciRoute *route =
            (const IA64PlatformPciRoute *)(bytes + route_offset +
                                           i * route_stride);
        uint16_t segment = le16_to_cpu(route->Segment);
        uint32_t gsi = le32_to_cpu(route->Gsi);

        /* Pin uses the PCI encoding 0..3 for INTA through INTD. */
        if (route->Device > 31 || route->Pin > 3 || route->Reserved0 != 0 ||
            le16_to_cpu(route->Reserved1) != 0 ||
            le32_to_cpu(route->Flags) != 0 ||
            !ia64_platform_root_bus_present(descriptor, segment,
                                            route->Bus) ||
            !ia64_platform_gsi_present(descriptor, gsi)) {
            error_setg(errp, "invalid IA-64 PCI interrupt route");
            return false;
        }
        for (j = 0; j < i; j++) {
            const IA64PlatformPciRoute *other =
                (const IA64PlatformPciRoute *)(bytes + route_offset +
                                               j * route_stride);

            if (route->Segment == other->Segment &&
                route->Bus == other->Bus &&
                route->Device == other->Device &&
                route->Pin == other->Pin) {
                error_setg(errp, "duplicate IA-64 PCI interrupt route");
                return false;
            }
        }
    }
    if (((le32_to_cpu(descriptor->Flags) &
          IA64_PLATFORM_FLAG_NO_MCFG) != 0) != (ecam_count == 0)) {
        error_setg(errp,
                   "IA-64 MCFG flag does not match the ECAM roots");
        return false;
    }
    return true;
}

static bool ia64_platform_desc_validate_profile(
    const IA64PlatformDescriptor *descriptor, Error **errp)
{
    IA64PlatformI2000Profile expected;
    const IA64PlatformI2000Profile *profile;
    uint32_t count = le32_to_cpu(descriptor->ProfileCount);
    uint32_t flags = le32_to_cpu(descriptor->Flags);
    uint64_t legacy_base = le64_to_cpu(descriptor->LegacyIoBase);
    uint64_t console_base = le64_to_cpu(descriptor->ConsoleBase);
    uint64_t console_offset;
    const IA64PlatformPciRoot *ide_root;
    const IA64PlatformPciRoot *isp_root;
    uint64_t isp_mmio_base;
    uint64_t isp_mmio_size;
    uint32_t profile_flags;
    uint32_t isp_capabilities;

    if (count == 0) {
        return true;
    }
    if (count != 1 ||
        le32_to_cpu(descriptor->ProfileEntrySize) != sizeof(*profile)) {
        error_setg(errp, "invalid IA-64 platform profile count or size");
        return false;
    }

    profile = ia64_platform_profile(descriptor, 0);
    ia64_platform_i2000_profile_init(&expected);
    if (memcmp(profile, &expected, sizeof(expected)) != 0) {
        error_setg(errp, "invalid i2000 platform profile entry");
        return false;
    }

    console_offset =
        (((uint64_t)le16_to_cpu(profile->UartPort) >> 2) << 12) |
        ((uint64_t)le16_to_cpu(profile->UartPort) & 0xfffULL);
    ide_root = ia64_platform_root_for_bus(
        descriptor, le16_to_cpu(profile->IdeSegment), profile->IdeBus);
    isp_root = ia64_platform_root_for_bus(
        descriptor, ISP12160_QEMU_I2000_SEGMENT, ISP12160_QEMU_I2000_BUS);
    isp_mmio_base = isp_root ? le64_to_cpu(isp_root->Mmio32Base) : 0;
    isp_mmio_size = isp_root ? le64_to_cpu(isp_root->Mmio32Size) : 0;
    profile_flags = le32_to_cpu(profile->Flags);
    isp_capabilities = le32_to_cpu(profile->Isp12160Capabilities);
    if ((le32_to_cpu(descriptor->Flags) &
         IA64_PLATFORM_FLAG_FAMILY_MASK) !=
            IA64_PLATFORM_FLAG_FAMILY_HP_I2000 ||
        (flags & IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS) !=
            IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS ||
        le64_to_cpu(descriptor->NvramBase) !=
            IA64_I2000_PROFILE_NVRAM_BASE ||
        le64_to_cpu(descriptor->NvramSize) !=
            IA64_I2000_PROFILE_NVRAM_SIZE ||
        le64_to_cpu(descriptor->RtcBase) != 0 ||
        le64_to_cpu(descriptor->RtcSize) != 0 ||
        le64_to_cpu(descriptor->ControlBase) != 0 ||
        le64_to_cpu(descriptor->ControlSize) != 0 ||
        le32_to_cpu(descriptor->ResetControlOffset) != 0 ||
        le32_to_cpu(descriptor->PoweroffControlOffset) != 0 ||
        le32_to_cpu(descriptor->ControlValue) != 0 ||
        le64_to_cpu(descriptor->AcpiPmBase) != 0 ||
        le64_to_cpu(descriptor->AcpiPmSize) != 0 ||
        le32_to_cpu(descriptor->AcpiSciGsi) != 0 ||
        legacy_base > UINT64_MAX - console_offset ||
        console_base != legacy_base + console_offset ||
        le32_to_cpu(descriptor->ConsoleRegisterStride) != 1 ||
        le32_to_cpu(descriptor->ConsoleClockHz) !=
            le32_to_cpu(profile->UartInputClockHz) ||
        /* Poll-only profiles use GSI 0. */
        le32_to_cpu(descriptor->ConsoleIrq) != 0 || ide_root == NULL ||
        le64_to_cpu(ide_root->IoBase) >
            le16_to_cpu(profile->IdeCommandPort) ||
        le64_to_cpu(ide_root->IoBase) + le64_to_cpu(ide_root->IoSize) <
            (uint64_t)le16_to_cpu(profile->IdeCommandPort) +
                profile->IdeCommandSize ||
        le64_to_cpu(ide_root->IoBase) >
            le16_to_cpu(profile->IdeControlPort) ||
        le64_to_cpu(ide_root->IoBase) + le64_to_cpu(ide_root->IoSize) <
            (uint64_t)le16_to_cpu(profile->IdeControlPort) +
                profile->IdeControlSize ||
        (profile_flags & IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT) == 0 ||
        (isp_capabilities & ~ISP12160_QEMU_I2000_KNOWN_CAPABILITIES) != 0 ||
        isp_capabilities != ISP12160_QEMU_I2000_REQUIRED_CAPABILITIES ||
        isp_root == NULL ||
        le64_to_cpu(isp_root->Mmio32TranslationOffset) != 0 ||
        isp_mmio_base > ISP12160_QEMU_I2000_BAR_ADDRESS ||
        isp_mmio_size < ISP12160_QEMU_I2000_BAR_SIZE ||
        ISP12160_QEMU_I2000_BAR_ADDRESS - isp_mmio_base >
            isp_mmio_size - ISP12160_QEMU_I2000_BAR_SIZE ||
        le64_to_cpu(isp_root->DmaBase) !=
            ISP12160_QEMU_I2000_DMA_APERTURE_BASE ||
        le64_to_cpu(isp_root->DmaSize) !=
            ISP12160_QEMU_I2000_DMA_APERTURE_SIZE ||
        !ia64_platform_pci_route_present(
            descriptor, ISP12160_QEMU_I2000_SEGMENT,
            ISP12160_QEMU_I2000_BUS, ISP12160_QEMU_I2000_DEVICE,
            ISP12160_QEMU_I2000_INTERRUPT_PIN - 1U,
            ISP12160_QEMU_I2000_GSI)) {
        error_setg(errp,
                   "i2000 profile does not match its descriptor");
        return false;
    }
    return true;
}

/* The caller must validate every descriptor-array range before this read. */
static bool ia64_platform_desc_profile_entry_valid(
    const IA64PlatformDescriptor *descriptor, bool *fixed_i2000,
    Error **errp)
{
    IA64PlatformI2000Profile expected;
    const IA64PlatformI2000Profile *profile;
    uint32_t count = le32_to_cpu(descriptor->ProfileCount);

    *fixed_i2000 = false;
    if (count == 0) {
        return true;
    }
    if (count != 1 ||
        le32_to_cpu(descriptor->ProfileEntrySize) != sizeof(*profile)) {
        error_setg(errp, "invalid IA-64 platform profile count or size");
        return false;
    }

    profile = ia64_platform_profile(descriptor, 0);
    ia64_platform_i2000_profile_init(&expected);
    if (memcmp(profile, &expected, sizeof(expected)) != 0) {
        error_setg(errp, "invalid i2000 platform profile entry");
        return false;
    }
    *fixed_i2000 = true;
    return true;
}

bool ia64_platform_desc_validate(const IA64PlatformDescriptor *descriptor,
                                 size_t available_size,
                                 uint32_t expected_platform_id,
                                 Error **errp)
{
    IA64PlatformRange ram_ranges;
    IA64PlatformRange roots;
    IA64PlatformRange sapics;
    IA64PlatformRange routes;
    IA64PlatformRange profiles;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t platform_id;
    uint32_t flags;
    uint32_t processors;
    uint32_t sockets;
    uint32_t cores;
    uint32_t threads;
    uint32_t physical_address_bits;
    uint32_t max_sockets;
    uint32_t max_cores;
    uint32_t max_threads;
    uint64_t ram_size;
    uint64_t low_ram_end;
    uint64_t legacy_io_base;
    uint64_t legacy_io_size;
    uint64_t local_sapic_base;
    uint64_t local_sapic_size;
    uint64_t console_base;
    uint32_t console_stride;
    uint32_t console_clock;
    uint64_t firmware_base;
    uint64_t firmware_size;
    uint64_t nvram_base;
    uint64_t nvram_size;
    uint64_t rtc_base;
    uint64_t rtc_size;
    uint64_t control_base;
    uint64_t control_size;
    uint32_t reset_control_offset;
    uint32_t poweroff_control_offset;
    uint32_t control_value;
    uint64_t acpi_pm_base;
    uint64_t acpi_pm_size;
    uint32_t acpi_sci_gsi;
    uint64_t ras_base;
    uint64_t ras_size;
    uint64_t console_size;
    bool fixed_i2000_profile;
    const uint32_t known_flags = IA64_PLATFORM_KNOWN_FLAGS;

    if (descriptor == NULL) {
        error_setg(errp, "missing IA-64 platform descriptor");
        return false;
    }
    if (available_size < sizeof(*descriptor)) {
        error_setg(errp, "truncated IA-64 platform descriptor header");
        return false;
    }
    if (le64_to_cpu(descriptor->Magic) != IA64_PLATFORM_DESC_MAGIC) {
        error_setg(errp, "invalid IA-64 platform descriptor magic");
        return false;
    }
    if (le32_to_cpu(descriptor->FormatRevision) !=
        IA64_PLATFORM_DESC_REVISION) {
        error_setg(errp, "unsupported IA-64 platform descriptor revision %u",
                   le32_to_cpu(descriptor->FormatRevision));
        return false;
    }

    header_size = le32_to_cpu(descriptor->HeaderSize);
    total_size = le32_to_cpu(descriptor->TotalSize);
    if (header_size != sizeof(*descriptor) || total_size < header_size ||
        total_size > available_size ||
        total_size > IA64_PLATFORM_DESC_MAX_SIZE) {
        error_setg(errp, "invalid IA-64 platform descriptor size");
        return false;
    }

    platform_id = le32_to_cpu(descriptor->PlatformId);
    if (platform_id != expected_platform_id) {
        error_setg(errp, "IA-64 platform descriptor ID mismatch");
        return false;
    }
    flags = le32_to_cpu(descriptor->Flags);
    if ((flags & ~known_flags) != 0 ||
        ((flags & IA64_PLATFORM_FLAG_FAMILY_MASK) !=
             IA64_PLATFORM_FLAG_FAMILY_HP_I2000 &&
         (flags & IA64_PLATFORM_FLAG_FAMILY_MASK) !=
             IA64_PLATFORM_FLAG_FAMILY_HP_ZX) ||
        descriptor->Reserved0 != 0 ||
        descriptor->Reserved1 != 0 || descriptor->Reserved2 != 0 ||
        descriptor->Reserved3 != 0 || descriptor->Reserved4 != 0) {
        error_setg(errp, "invalid HP IA-64 descriptor flags");
        return false;
    }

    /* Bound every array before profile bytes can select a resource policy. */
    if (!ia64_platform_array_range(
            le32_to_cpu(descriptor->RamRangeOffset),
            le32_to_cpu(descriptor->RamRangeCount),
            le32_to_cpu(descriptor->RamRangeEntrySize),
            sizeof(IA64PlatformRamRange), header_size, total_size,
            &ram_ranges, "RAM range", errp) ||
        !ia64_platform_array_range(
            le32_to_cpu(descriptor->PciRootOffset),
            le32_to_cpu(descriptor->PciRootCount),
            le32_to_cpu(descriptor->PciRootEntrySize),
            sizeof(IA64PlatformPciRoot), header_size, total_size,
            &roots, "PCI root", errp) ||
        !ia64_platform_array_range(
            le32_to_cpu(descriptor->IoSapicOffset),
            le32_to_cpu(descriptor->IoSapicCount),
            le32_to_cpu(descriptor->IoSapicEntrySize),
            sizeof(IA64PlatformIoSapic), header_size, total_size,
            &sapics, "I/O SAPIC", errp) ||
        !ia64_platform_array_range(
            le32_to_cpu(descriptor->PciRouteOffset),
            le32_to_cpu(descriptor->PciRouteCount),
            le32_to_cpu(descriptor->PciRouteEntrySize),
            sizeof(IA64PlatformPciRoute), header_size, total_size,
            &routes, "PCI route", errp) ||
        !ia64_platform_array_range(
            le32_to_cpu(descriptor->ProfileOffset),
            le32_to_cpu(descriptor->ProfileCount),
            le32_to_cpu(descriptor->ProfileEntrySize),
            sizeof(IA64PlatformI2000Profile), header_size, total_size,
            &profiles, "profile", errp)) {
        return false;
    }
    if (ia64_platform_ranges_overlap(&ram_ranges, &roots) ||
        ia64_platform_ranges_overlap(&ram_ranges, &sapics) ||
        ia64_platform_ranges_overlap(&ram_ranges, &routes) ||
        ia64_platform_ranges_overlap(&ram_ranges, &profiles) ||
        ia64_platform_ranges_overlap(&roots, &sapics) ||
        ia64_platform_ranges_overlap(&roots, &routes) ||
        ia64_platform_ranges_overlap(&roots, &profiles) ||
        ia64_platform_ranges_overlap(&sapics, &routes) ||
        ia64_platform_ranges_overlap(&sapics, &profiles) ||
        ia64_platform_ranges_overlap(&routes, &profiles)) {
        error_setg(errp, "IA-64 platform descriptor arrays overlap");
        return false;
    }
    if (!ia64_platform_desc_profile_entry_valid(
            descriptor, &fixed_i2000_profile, errp)) {
        return false;
    }
    if ((flags & IA64_PLATFORM_FLAG_FIRMWARE_COMPAT) != 0 &&
        !fixed_i2000_profile) {
        error_setg(errp,
                   "firmware behavior flags require the fixed i2000 profile");
        return false;
    }

    ram_size = le64_to_cpu(descriptor->RamSize);
    low_ram_end = le64_to_cpu(descriptor->LowRamEnd);
    if (((ram_size | low_ram_end) &
         (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
        low_ram_end < IA64_PLATFORM_MIN_LOW_RAM_SIZE ||
        low_ram_end > ram_size) {
        error_setg(errp, "invalid IA-64 platform RAM limits");
        return false;
    }

    firmware_base = le64_to_cpu(descriptor->FirmwareBase);
    firmware_size = le64_to_cpu(descriptor->FirmwareSize);
    if (firmware_base != IA64_PLATFORM_FIRMWARE_BASE ||
        firmware_size != IA64_PLATFORM_FIRMWARE_SIZE) {
        error_setg(errp, "invalid IA-64 platform firmware range");
        return false;
    }

    legacy_io_base = le64_to_cpu(descriptor->LegacyIoBase);
    legacy_io_size = le64_to_cpu(descriptor->LegacyIoSize);
    local_sapic_base = le64_to_cpu(descriptor->LocalSapicBase);
    local_sapic_size = le64_to_cpu(descriptor->LocalSapicSize);
    console_base = le64_to_cpu(descriptor->ConsoleBase);
    console_stride = le32_to_cpu(descriptor->ConsoleRegisterStride);
    console_clock = le32_to_cpu(descriptor->ConsoleClockHz);
    nvram_base = le64_to_cpu(descriptor->NvramBase);
    nvram_size = le64_to_cpu(descriptor->NvramSize);
    rtc_base = le64_to_cpu(descriptor->RtcBase);
    rtc_size = le64_to_cpu(descriptor->RtcSize);
    control_base = le64_to_cpu(descriptor->ControlBase);
    control_size = le64_to_cpu(descriptor->ControlSize);
    reset_control_offset = le32_to_cpu(descriptor->ResetControlOffset);
    poweroff_control_offset = le32_to_cpu(descriptor->PoweroffControlOffset);
    control_value = le32_to_cpu(descriptor->ControlValue);
    acpi_pm_base = le64_to_cpu(descriptor->AcpiPmBase);
    acpi_pm_size = le64_to_cpu(descriptor->AcpiPmSize);
    acpi_sci_gsi = le32_to_cpu(descriptor->AcpiSciGsi);
    ras_base = le64_to_cpu(descriptor->RasBase);
    ras_size = le64_to_cpu(descriptor->RasSize);
    console_size = 8 * (uint64_t)console_stride;
    physical_address_bits = le32_to_cpu(descriptor->PhysicalAddressBits);
    if (!ia64_platform_legacy_io_valid(physical_address_bits, legacy_io_base,
                                       legacy_io_size) ||
        local_sapic_size < 2 * MiB || local_sapic_base > UINT32_MAX ||
        (local_sapic_size & (IA64_PLATFORM_DESC_ALIGNMENT - 1U)) != 0 ||
        !ia64_platform_u64_range_valid(local_sapic_base, local_sapic_size) ||
        local_sapic_base + local_sapic_size > 0x100000000ULL ||
        (local_sapic_base & (2 * MiB - 1)) != 0 || console_base == 0 ||
        console_stride == 0 || !is_power_of_2(console_stride) ||
        console_stride > IA64_PLATFORM_DESC_ALIGNMENT ||
        console_clock < IA64_PLATFORM_UART_MIN_CLOCK_HZ ||
        (le32_to_cpu(descriptor->ConsoleFlags) &
         ~IA64_PLATFORM_CONSOLE_KNOWN_FLAGS) != 0 ||
        !ia64_platform_u64_range_valid(console_base, console_size) ||
        (fixed_i2000_profile ?
         (nvram_base != IA64_I2000_PROFILE_NVRAM_BASE ||
          nvram_size != IA64_I2000_PROFILE_NVRAM_SIZE ||
          rtc_base != 0 || rtc_size != 0) :
         (nvram_base == 0 ||
          nvram_size < IA64_PLATFORM_MIN_NVRAM_SIZE ||
          (nvram_base & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          (nvram_size & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          !ia64_platform_u64_range_valid(nvram_base, nvram_size) ||
          rtc_base == 0 || rtc_size < IA64_PLATFORM_MIN_RTC_SIZE ||
          (rtc_base & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          (rtc_size & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          !ia64_platform_u64_range_valid(rtc_base, rtc_size))) ||
        ((control_base | control_size | reset_control_offset |
          poweroff_control_offset | control_value) != 0 &&
         (control_base == 0 ||
          control_size < IA64_PLATFORM_MIN_CONTROL_SIZE ||
          (control_base & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          (control_size & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          !ia64_platform_u64_range_valid(control_base, control_size) ||
          reset_control_offset >= control_size ||
          poweroff_control_offset >= control_size ||
          reset_control_offset == poweroff_control_offset ||
          control_value == 0 || control_value > UINT8_MAX)) ||
        ((acpi_pm_base | acpi_pm_size | acpi_sci_gsi) != 0 &&
         (!(flags & IA64_PLATFORM_FLAG_ACPI_PM) ||
          acpi_pm_base == 0 ||
          acpi_pm_size != IA64_PLATFORM_ACPI_PM_SIZE ||
          acpi_sci_gsi == 0 || acpi_sci_gsi > UINT16_MAX ||
          (acpi_pm_base &
           (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
          !ia64_platform_u64_range_valid(acpi_pm_base, acpi_pm_size))) ||
        ras_base == 0 || ras_size < IA64_RAS_HUB_SIZE ||
        (ras_base & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
        (ras_size & (IA64_PLATFORM_RESOURCE_ALIGNMENT - 1U)) != 0 ||
        !ia64_platform_u64_range_valid(ras_base, ras_size)) {
        error_setg(errp, "invalid IA-64 platform I/O resources");
        return false;
    }

    if (ia64_platform_u64_ranges_overlap(legacy_io_base, legacy_io_size,
                                         local_sapic_base,
                                         local_sapic_size) ||
        ia64_platform_u64_ranges_overlap(legacy_io_base, legacy_io_size,
                                         firmware_base, firmware_size) ||
        ia64_platform_u64_ranges_overlap(nvram_base, nvram_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(local_sapic_base, local_sapic_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(local_sapic_base, local_sapic_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(legacy_io_base, legacy_io_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(legacy_io_base, legacy_io_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(firmware_base, firmware_size,
                                         local_sapic_base,
                                         local_sapic_size) ||
        ia64_platform_u64_ranges_overlap(firmware_base, firmware_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(firmware_base, firmware_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(control_base, control_size,
                                         legacy_io_base, legacy_io_size) ||
        ia64_platform_u64_ranges_overlap(control_base, control_size,
                                         local_sapic_base,
                                         local_sapic_size) ||
        ia64_platform_u64_ranges_overlap(control_base, control_size,
                                         firmware_base, firmware_size) ||
        ia64_platform_u64_ranges_overlap(control_base, control_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(control_base, control_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         legacy_io_base, legacy_io_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         local_sapic_base,
                                         local_sapic_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         firmware_base, firmware_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(acpi_pm_base, acpi_pm_size,
                                         control_base, control_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         legacy_io_base, legacy_io_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         local_sapic_base,
                                         local_sapic_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         firmware_base, firmware_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         nvram_base, nvram_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         rtc_base, rtc_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         control_base, control_size) ||
        ia64_platform_u64_ranges_overlap(ras_base, ras_size,
                                         acpi_pm_base, acpi_pm_size)) {
        error_setg(errp, "overlapping IA-64 fixed platform resources");
        return false;
    }
    if (ia64_platform_u64_ranges_overlap(console_base, console_size,
                                         legacy_io_base, legacy_io_size) &&
        !(console_base >= legacy_io_base &&
          console_base + console_size <= legacy_io_base + legacy_io_size)) {
        error_setg(errp,
                   "IA-64 console only partially overlaps legacy I/O");
        return false;
    }
    if (!(console_base >= legacy_io_base &&
          console_base + console_size <= legacy_io_base + legacy_io_size) &&
        (ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          local_sapic_base,
                                          local_sapic_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          nvram_base, nvram_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          rtc_base, rtc_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          control_base, control_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          acpi_pm_base, acpi_pm_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          ras_base, ras_size) ||
         ia64_platform_u64_ranges_overlap(console_base, console_size,
                                          firmware_base, firmware_size))) {
        error_setg(errp, "IA-64 console overlaps a fixed platform resource");
        return false;
    }

    processors = le32_to_cpu(descriptor->ProcessorCount);
    sockets = le32_to_cpu(descriptor->SocketCount);
    cores = le32_to_cpu(descriptor->CoresPerSocket);
    threads = le32_to_cpu(descriptor->ThreadsPerCore);
    max_sockets = le32_to_cpu(descriptor->MaxSockets);
    max_cores = le32_to_cpu(descriptor->MaxCoresPerSocket);
    max_threads = le32_to_cpu(descriptor->MaxThreadsPerCore);
    if (processors < 1 || processors > 256 || max_sockets < 1 ||
        max_sockets > 256 || max_cores < 1 || max_cores > 256 ||
        max_threads < 1 || max_threads > 256 ||
        sockets < 1 || sockets > max_sockets ||
        cores < 1 || cores > max_cores ||
        threads < 1 || threads > max_threads ||
        (uint64_t)sockets * cores * threads != processors) {
        error_setg(errp, "invalid HP IA-64 processor topology");
        return false;
    }

    if (!ia64_platform_policy_valid(descriptor, errp) ||
        !ia64_platform_desc_validate_entries(descriptor, errp) ||
        !ia64_platform_desc_validate_profile(descriptor, errp)) {
        return false;
    }
    if (le32_to_cpu(descriptor->ProfileCount) == 0 &&
        !ia64_platform_gsi_present(
            descriptor, le32_to_cpu(descriptor->ConsoleIrq))) {
        error_setg(errp, "IA-64 console interrupt has no I/O SAPIC owner");
        return false;
    }
    if (acpi_pm_size != 0 &&
        !ia64_platform_gsi_present(descriptor, acpi_sci_gsi)) {
        error_setg(errp, "IA-64 ACPI SCI has no I/O SAPIC owner");
        return false;
    }
    if (ia64_platform_checksum(descriptor, total_size) != 0) {
        error_setg(errp, "invalid IA-64 platform descriptor checksum");
        return false;
    }
    return true;
}

void ia64_platform_desc_finalize(IA64PlatformDescriptor *descriptor,
                                 size_t available_size)
{
    uint32_t total_size = le32_to_cpu(descriptor->TotalSize);
    uint8_t checksum;

    g_assert(total_size >= sizeof(*descriptor));
    g_assert(total_size <= available_size);
    g_assert(total_size <= IA64_PLATFORM_DESC_MAX_SIZE);

    descriptor->Checksum = 0;
    checksum = ia64_platform_checksum(descriptor, total_size);
    descriptor->Checksum = cpu_to_le32((uint8_t)(0U - checksum));
}

static bool ia64_platform_desc_append(void *storage, size_t storage_size,
                                      uint32_t *cursor, const void *entries,
                                      uint32_t count, uint32_t entry_size,
                                      uint32_t *offset, Error **errp)
{
    uint32_t aligned = (*cursor + 7U) & ~7U;
    size_t bytes;

    if (count == 0) {
        *offset = 0;
        return true;
    }
    if (entries == NULL || aligned < *cursor || aligned > storage_size ||
        count > (storage_size - aligned) / entry_size) {
        error_setg(errp, "IA-64 platform descriptor array is too large");
        return false;
    }
    bytes = (size_t)count * entry_size;
    memcpy((uint8_t *)storage + aligned, entries, bytes);
    *offset = aligned;
    *cursor = aligned + bytes;
    return true;
}

static bool ia64_platform_host_ranges_overlap(const void *first,
                                              size_t first_size,
                                              const void *second,
                                              size_t second_size)
{
    uintptr_t first_start = (uintptr_t)first;
    uintptr_t second_start = (uintptr_t)second;
    uintptr_t first_bytes;
    uintptr_t second_bytes;
    uintptr_t first_end;
    uintptr_t second_end;

    if (first_size == 0 || second_size == 0) {
        return false;
    }
    first_bytes = first_size;
    second_bytes = second_size;
    if ((size_t)first_bytes != first_size ||
        (size_t)second_bytes != second_size) {
        return true;
    }
    if (first_start > UINTPTR_MAX - first_bytes ||
        second_start > UINTPTR_MAX - second_bytes) {
        return true;
    }
    first_end = first_start + first_bytes;
    second_end = second_start + second_bytes;
    return first_start < second_end && second_start < first_end;
}

static bool ia64_platform_desc_array_input_valid(const void *entries,
                                                 uint32_t count,
                                                 size_t entry_size,
                                                 size_t storage_size,
                                                 size_t *bytes)
{
    if (count == 0) {
        *bytes = 0;
        return true;
    }
    if (entries == NULL || count > storage_size / entry_size) {
        return false;
    }
    *bytes = (size_t)count * entry_size;
    return true;
}

bool ia64_platform_desc_build(void *storage, size_t storage_size,
                              const IA64PlatformDescriptor *header,
                              const IA64PlatformDescriptorArrays *arrays,
                              size_t *descriptor_size, Error **errp)
{
    IA64PlatformDescriptor *descriptor = storage;
    uint32_t cursor = sizeof(*descriptor);
    uint32_t ram_offset;
    uint32_t root_offset;
    uint32_t sapic_offset;
    uint32_t route_offset;
    uint32_t profile_offset;
    uint32_t platform_id;
    size_t ram_bytes;
    size_t root_bytes;
    size_t sapic_bytes;
    size_t route_bytes;
    size_t profile_bytes;

    if (storage == NULL || header == NULL || arrays == NULL ||
        descriptor_size == NULL || storage_size < sizeof(*descriptor) ||
        storage_size > IA64_PLATFORM_DESC_MAX_SIZE) {
        error_setg(errp, "invalid IA-64 platform descriptor build buffer");
        return false;
    }

    if (ia64_platform_host_ranges_overlap(storage, storage_size,
                                          header, sizeof(*header)) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays, sizeof(*arrays)) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          descriptor_size,
                                          sizeof(*descriptor_size)) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          header, sizeof(*header)) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays, sizeof(*arrays))) {
        error_setg(errp, "IA-64 platform descriptor build buffers alias");
        return false;
    }
    if (!ia64_platform_desc_array_input_valid(
            arrays->ram_ranges, arrays->ram_range_count,
            sizeof(IA64PlatformRamRange), storage_size, &ram_bytes) ||
        !ia64_platform_desc_array_input_valid(
            arrays->pci_roots, arrays->pci_root_count,
            sizeof(IA64PlatformPciRoot), storage_size, &root_bytes) ||
        !ia64_platform_desc_array_input_valid(
            arrays->io_sapics, arrays->io_sapic_count,
            sizeof(IA64PlatformIoSapic), storage_size, &sapic_bytes) ||
        !ia64_platform_desc_array_input_valid(
            arrays->pci_routes, arrays->pci_route_count,
            sizeof(IA64PlatformPciRoute), storage_size, &route_bytes) ||
        !ia64_platform_desc_array_input_valid(
            arrays->profiles, arrays->profile_count,
            sizeof(IA64PlatformI2000Profile), storage_size,
            &profile_bytes)) {
        error_setg(errp, "IA-64 platform descriptor array is too large");
        return false;
    }
    if (ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays->ram_ranges, ram_bytes) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays->pci_roots, root_bytes) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays->io_sapics, sapic_bytes) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays->pci_routes, route_bytes) ||
        ia64_platform_host_ranges_overlap(storage, storage_size,
                                          arrays->profiles, profile_bytes) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays->ram_ranges, ram_bytes) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays->pci_roots, root_bytes) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays->io_sapics, sapic_bytes) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays->pci_routes, route_bytes) ||
        ia64_platform_host_ranges_overlap(descriptor_size,
                                          sizeof(*descriptor_size),
                                          arrays->profiles,
                                          profile_bytes)) {
        error_setg(errp, "IA-64 platform descriptor build buffers alias");
        return false;
    }

    memset(storage, 0, storage_size);
    memcpy(descriptor, header, sizeof(*descriptor));
    if (!ia64_platform_desc_append(storage, storage_size, &cursor,
                                   arrays->ram_ranges,
                                   arrays->ram_range_count,
                                   sizeof(IA64PlatformRamRange),
                                   &ram_offset, errp) ||
        !ia64_platform_desc_append(storage, storage_size, &cursor,
                                   arrays->pci_roots,
                                   arrays->pci_root_count,
                                   sizeof(IA64PlatformPciRoot),
                                   &root_offset, errp) ||
        !ia64_platform_desc_append(storage, storage_size, &cursor,
                                   arrays->io_sapics,
                                   arrays->io_sapic_count,
                                   sizeof(IA64PlatformIoSapic),
                                   &sapic_offset, errp) ||
        !ia64_platform_desc_append(storage, storage_size, &cursor,
                                   arrays->pci_routes,
                                   arrays->pci_route_count,
                                   sizeof(IA64PlatformPciRoute),
                                   &route_offset, errp) ||
        !ia64_platform_desc_append(storage, storage_size, &cursor,
                                   arrays->profiles,
                                   arrays->profile_count,
                                   sizeof(IA64PlatformI2000Profile),
                                   &profile_offset, errp)) {
        return false;
    }

    descriptor->HeaderSize = cpu_to_le32(sizeof(*descriptor));
    descriptor->TotalSize = cpu_to_le32(cursor);
    descriptor->RamRangeOffset = cpu_to_le32(ram_offset);
    descriptor->RamRangeCount = cpu_to_le32(arrays->ram_range_count);
    descriptor->RamRangeEntrySize = arrays->ram_range_count ?
        cpu_to_le32(sizeof(IA64PlatformRamRange)) : 0;
    descriptor->PciRootOffset = cpu_to_le32(root_offset);
    descriptor->PciRootCount = cpu_to_le32(arrays->pci_root_count);
    descriptor->PciRootEntrySize = arrays->pci_root_count ?
        cpu_to_le32(sizeof(IA64PlatformPciRoot)) : 0;
    descriptor->IoSapicOffset = cpu_to_le32(sapic_offset);
    descriptor->IoSapicCount = cpu_to_le32(arrays->io_sapic_count);
    descriptor->IoSapicEntrySize = arrays->io_sapic_count ?
        cpu_to_le32(sizeof(IA64PlatformIoSapic)) : 0;
    descriptor->PciRouteOffset = cpu_to_le32(route_offset);
    descriptor->PciRouteCount = cpu_to_le32(arrays->pci_route_count);
    descriptor->PciRouteEntrySize = arrays->pci_route_count ?
        cpu_to_le32(sizeof(IA64PlatformPciRoute)) : 0;
    descriptor->ProfileOffset = cpu_to_le32(profile_offset);
    descriptor->ProfileCount = cpu_to_le32(arrays->profile_count);
    descriptor->ProfileEntrySize = arrays->profile_count ?
        cpu_to_le32(sizeof(IA64PlatformI2000Profile)) : 0;
    ia64_platform_desc_finalize(descriptor, storage_size);

    platform_id = le32_to_cpu(descriptor->PlatformId);
    if (!ia64_platform_desc_validate(descriptor, cursor, platform_id, errp)) {
        return false;
    }
    *descriptor_size = cursor;
    return true;
}
