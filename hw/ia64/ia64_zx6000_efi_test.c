/*
 * IA-64 zx6000 EFI integration test machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/char/serial-mm.h"
#include "hw/core/qdev-properties.h"
#include "hw/ia64/hp_ia64.h"
#include "hw/ia64/ia64_platform.h"
#include "hw/ia64/ia64_zx6000_zx1_test.h"
#include "hw/ia64/ia64_zx6000_zx1_test_layout.h"
#include "hw/ia64/ia64_zx6000_efi_test.h"
#include "hw/pci/pci_device.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ia64/cpu-qom.h"

#define IA64_ZX6000_EFI_TEST_TOPOLOGY(obj) \
    OBJECT_CHECK(IA64Zx6000EfiTestTopologyState, (obj), \
                 TYPE_IA64_ZX6000_EFI_TEST_TOPOLOGY)

#define ZX6000_EFI_TEST_IVT_BASE UINT64_C(0x00010000)
#define ZX6000_EFI_TEST_IO_SAPIC_VERSION UINT32_C(0x000a0020)
#define ZX6000_EFI_TEST_UART_INPUT_CLOCK_HZ UINT32_C(1843200)

typedef struct IA64Zx6000EfiTestTopologyState {
    DeviceState parent_obj;

    IA64ZX6000ZX1TestState *fixture;
    PCIDevice *probes[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    DeviceState *uart;
    Chardev *uart_chardev;
    MemoryRegion nvram;
    MemoryRegion rtc;
    int64_t rtc_offset;
    bool resources_mapped;
} IA64Zx6000EfiTestTopologyState;

struct IA64Zx6000EfiTestMachineState {
    HPIA64MachineState parent_obj;

    IA64Zx6000EfiTestTopologyState *topology;
    bool pci_ecam;
};

enum {
    ZX6000_EFI_TEST_CALCULATED_DESCRIPTOR_SIZE =
        sizeof(IA64PlatformDescriptor) +
        sizeof(IA64PlatformRamRange) +
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT *
            sizeof(IA64PlatformPciRoot) +
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT *
            sizeof(IA64PlatformIoSapic) +
        IA64_ZX6000_ZX1_TEST_ROOT_COUNT *
            sizeof(IA64PlatformPciRoute),
};

static const uint32_t zx6000_efi_gsi_base[] = {
    IA64_ZX6000_EFI_TEST_ROOT0_GSI_BASE,
    IA64_ZX6000_EFI_TEST_ROOT1_GSI_BASE,
};

G_STATIC_ASSERT(ZX6000_EFI_TEST_CALCULATED_DESCRIPTOR_SIZE ==
                IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE);
G_STATIC_ASSERT(G_N_ELEMENTS(zx6000_efi_gsi_base) ==
                IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
G_STATIC_ASSERT(IA64_ZX6000_EFI_TEST_IDENTITY_DMA_BASE ==
                IA64_ZX6000_EFI_TEST_DESCRIPTOR_GPA +
                IA64_PLATFORM_RESOURCE_ALIGNMENT);
G_STATIC_ASSERT(IA64_ZX6000_EFI_TEST_IDENTITY_DMA_BASE +
                IA64_ZX6000_EFI_TEST_IDENTITY_DMA_SIZE ==
                IA64_ZX6000_ZX1_TEST_RAM_BASE +
                IA64_ZX6000_ZX1_TEST_RAM_SIZE);

static uint64_t zx6000_efi_rtc_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    IA64Zx6000EfiTestTopologyState *s = opaque;
    int64_t now = time(NULL);

    if (addr != 0 || size != sizeof(uint64_t)) {
        return 0;
    }
    if (s->rtc_offset > 0 && now > INT64_MAX - s->rtc_offset) {
        return INT64_MAX;
    }
    if (s->rtc_offset < 0 && now < -s->rtc_offset) {
        return 0;
    }
    return now + s->rtc_offset;
}

static void zx6000_efi_rtc_write(void *opaque, hwaddr addr, uint64_t value,
                         unsigned int size)
{
    IA64Zx6000EfiTestTopologyState *s = opaque;

    if (addr == 0 && size == sizeof(value) && value <= INT64_MAX) {
        s->rtc_offset = (int64_t)value - (int64_t)time(NULL);
    }
}

static const MemoryRegionOps zx6000_efi_rtc_ops = {
    .read = zx6000_efi_rtc_read,
    .write = zx6000_efi_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static DeviceState *zx6000_efi_add_child(DeviceState *parent, const char *name,
                                 const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void zx6000_efi_remove_child(DeviceState **child)
{
    if (*child == NULL) {
        return;
    }
    if (qdev_is_realized(*child)) {
        qdev_unrealize(*child);
    }
    object_unparent(OBJECT(*child));
    *child = NULL;
}

static void zx6000_efi_topology_unmap_resources(
    IA64Zx6000EfiTestTopologyState *s)
{
    if (!s->resources_mapped) {
        return;
    }
    memory_region_transaction_begin();
    memory_region_del_subregion(
        get_system_memory(),
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->uart), 0));
    memory_region_del_subregion(get_system_memory(), &s->rtc);
    memory_region_del_subregion(get_system_memory(), &s->nvram);
    memory_region_transaction_commit();
    s->resources_mapped = false;
}

static void zx6000_efi_topology_cleanup(IA64Zx6000EfiTestTopologyState *s)
{
    DeviceState *child;
    int root;

    zx6000_efi_topology_unmap_resources(s);
    child = s->uart;
    zx6000_efi_remove_child(&child);
    s->uart = NULL;
    for (root = IA64_ZX6000_ZX1_TEST_ROOT_COUNT - 1;
         root >= 0; root--) {
        ia64_zx6000_zx1_test_destroy_test_probe(s->probes[root]);
        s->probes[root] = NULL;
    }
    child = s->fixture ? DEVICE(s->fixture) : NULL;
    zx6000_efi_remove_child(&child);
    s->fixture = NULL;
}

static bool zx6000_efi_topology_create_uart(
    IA64Zx6000EfiTestTopologyState *s, Error **errp)
{
    qemu_irq irq = ia64_zx6000_zx1_test_io_sapic_input(
        s->fixture, 0, IA64_ZX6000_EFI_TEST_CONSOLE_INPUT);
    DeviceState *uart;

    if (irq == NULL) {
        error_setg(errp, "zx6000 EFI console I/O SAPIC input is absent");
        return false;
    }
    uart = zx6000_efi_add_child(DEVICE(s), IA64_ZX6000_EFI_TEST_UART_CHILD,
                        TYPE_SERIAL_MM);
    s->uart = uart;
    qdev_prop_set_uint8(uart, "regshift", 0);
    qdev_prop_set_uint32(uart, "baudbase", 115200);
    qdev_prop_set_chr(uart, "chardev", s->uart_chardev);
    qdev_prop_set_uint8(uart, "endianness", DEVICE_LITTLE_ENDIAN);
    qdev_set_legacy_instance_id(
        uart, IA64_ZX6000_EFI_TEST_CONSOLE_BASE, 2);
    if (!sysbus_realize(SYS_BUS_DEVICE(uart), errp)) {
        return false;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(uart), 0, irq);
    return true;
}

static void zx6000_efi_topology_map_resources(
    IA64Zx6000EfiTestTopologyState *s)
{
    memory_region_transaction_begin();
    memory_region_add_subregion(get_system_memory(),
                                IA64_ZX6000_EFI_TEST_NVRAM_BASE,
                                &s->nvram);
    memory_region_add_subregion(get_system_memory(),
                                IA64_ZX6000_EFI_TEST_RTC_BASE,
                                &s->rtc);
    memory_region_add_subregion(
        get_system_memory(), IA64_ZX6000_EFI_TEST_CONSOLE_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->uart), 0));
    memory_region_transaction_commit();
    s->resources_mapped = true;
}

static void zx6000_efi_topology_realize(DeviceState *dev, Error **errp)
{
    IA64Zx6000EfiTestTopologyState *s =
        IA64_ZX6000_EFI_TEST_TOPOLOGY(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *fixture;
    Error *local_err = NULL;
    unsigned int root;

    if (machine->ram == NULL) {
        error_setg(errp, "%s requires parent-machine RAM",
                   TYPE_IA64_ZX6000_EFI_TEST_TOPOLOGY);
        return;
    }

    fixture = zx6000_efi_add_child(dev, IA64_ZX6000_EFI_TEST_FIXTURE_CHILD,
                           TYPE_IA64_ZX6000_ZX1_TEST);
    s->fixture = IA64_ZX6000_ZX1_TEST(fixture);
    if (!object_property_set_link(OBJECT(fixture),
                                  IA64_ZX6000_ZX1_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(fixture, NULL, &local_err)) {
        goto fail;
    }

    for (root = 0; root < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; root++) {
        s->probes[root] = ia64_zx6000_zx1_test_create_test_probe(
            s->fixture, root, IA64_ZX6000_EFI_TEST_PROBE_SLOT,
            &local_err);
        if (s->probes[root] == NULL) {
            goto fail;
        }
    }
    if (!zx6000_efi_topology_create_uart(s, &local_err) ||
        !memory_region_init_ram(
            &s->nvram, OBJECT(s), "ia64-zx6000-efi-test.nvram",
            IA64_ZX6000_EFI_TEST_NVRAM_SIZE, &local_err)) {
        goto fail;
    }
    memory_region_init_io(&s->rtc, OBJECT(s), &zx6000_efi_rtc_ops, s,
                          "ia64-zx6000-efi-test.rtc",
                          IA64_ZX6000_EFI_TEST_RTC_SIZE);
    zx6000_efi_topology_map_resources(s);
    return;

fail:
    zx6000_efi_topology_cleanup(s);
    error_propagate(errp, local_err);
}

static void zx6000_efi_topology_unrealize(DeviceState *dev)
{
    zx6000_efi_topology_cleanup(IA64_ZX6000_EFI_TEST_TOPOLOGY(dev));
}

static void zx6000_efi_topology_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->desc = "zx6000 EFI test topology";
    dc->realize = zx6000_efi_topology_realize;
    dc->unrealize = zx6000_efi_topology_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
}

static bool zx6000_efi_machine_create_topology(
    IA64Zx6000EfiTestMachineState *machine, Error **errp)
{
    DeviceState *dev = qdev_new(TYPE_IA64_ZX6000_EFI_TEST_TOPOLOGY);
    IA64Zx6000EfiTestTopologyState *topology =
        IA64_ZX6000_EFI_TEST_TOPOLOGY(dev);

    object_property_add_child(OBJECT(machine),
                              IA64_ZX6000_EFI_TEST_TOPOLOGY_CHILD,
                              OBJECT(dev));
    object_unref(OBJECT(dev));
    machine->topology = topology;
    topology->uart_chardev = serial_hd(0);
    if (!qdev_realize(dev, NULL, errp)) {
        object_unparent(OBJECT(dev));
        machine->topology = NULL;
        return false;
    }
    return true;
}

static void zx6000_efi_descriptor_header_init(
    IA64PlatformDescriptor *header,
    const IA64ZX6000ZX1TestLayout *layout,
    const IA64Zx6000EfiTestMachineState *machine)
{
    const MachineState *ms = MACHINE(machine);

    *header = (IA64PlatformDescriptor) {
        .Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC),
        .FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION),
        .PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_ZX6000),
        .Flags = cpu_to_le32(
            (machine->pci_ecam ? 0 : IA64_PLATFORM_FLAG_NO_MCFG) |
            IA64_PLATFORM_FLAG_QEMU_EXTENSION |
            IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
            (machine->pci_ecam ? IA64_PLATFORM_FLAG_PCI_ECAM :
             IA64_PLATFORM_FLAG_PCI_ZX1_LBA |
             IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC)),
        .RamSize = cpu_to_le64(layout->ram.size),
        .LowRamEnd = cpu_to_le64(layout->ram.size),
        .FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE),
        .FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE),
        .ProcessorCount = cpu_to_le32(ms->smp.cpus),
        .SocketCount = cpu_to_le32(ms->smp.sockets),
        .CoresPerSocket = cpu_to_le32(ms->smp.cores),
        .ThreadsPerCore = cpu_to_le32(ms->smp.threads),
        .PhysicalAddressBits = cpu_to_le32(
            IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS),
        .MaxSockets = cpu_to_le32(2),
        .MaxCoresPerSocket = cpu_to_le32(2),
        .MaxThreadsPerCore = cpu_to_le32(2),
        .MaxPciRoots = cpu_to_le32(
            IA64_ZX6000_ZX1_TEST_ROOT_COUNT),
        .PciRootIdentity = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX),
        .NumaNodeCount = cpu_to_le32(1),
        .LegacyIoBase = cpu_to_le64(
            IA64_ZX6000_EFI_TEST_LEGACY_IO_BASE),
        .LegacyIoSize = cpu_to_le64(IA64_PLATFORM_MIN_LEGACY_IO_SIZE),
        .LocalSapicBase = cpu_to_le64(layout->pib.base),
        .LocalSapicSize = cpu_to_le64(
            IA64_ZX6000_EFI_TEST_LOCAL_SAPIC_SIZE),
        .ConsoleBase = cpu_to_le64(
            IA64_ZX6000_EFI_TEST_CONSOLE_BASE),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(ZX6000_EFI_TEST_UART_INPUT_CLOCK_HZ),
        .ConsoleIrq = cpu_to_le32(
            IA64_ZX6000_EFI_TEST_CONSOLE_GSI),
        .NvramBase = cpu_to_le64(IA64_ZX6000_EFI_TEST_NVRAM_BASE),
        .NvramSize = cpu_to_le64(IA64_ZX6000_EFI_TEST_NVRAM_SIZE),
        .RtcBase = cpu_to_le64(IA64_ZX6000_EFI_TEST_RTC_BASE),
        .RtcSize = cpu_to_le64(IA64_ZX6000_EFI_TEST_RTC_SIZE),
        .RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE),
        .RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE),
    };
    header->NumaNode[0].ProcessorCount = cpu_to_le32(ms->smp.cpus);
    header->NumaNode[0].RamRangeMask = cpu_to_le32(1);
    header->NumaNode[0].Distance[0] = 10;
}

static void zx6000_efi_descriptor_roots_init(
    IA64PlatformPciRoot roots[IA64_ZX6000_ZX1_TEST_ROOT_COUNT],
    const IA64ZX6000ZX1TestLayout *layout, bool pci_ecam)
{
    static const uint64_t ecam_base[] = {
        UINT64_C(0x0000000500000000),
        UINT64_C(0x0000000600000000),
    };
    unsigned int i;

    memset(roots, 0, sizeof(*roots) *
           IA64_ZX6000_ZX1_TEST_ROOT_COUNT);
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        const IA64ZX6000ZX1TestRoot *source = &layout->roots[i];

        roots[i].Segment = cpu_to_le16(pci_ecam ? i : 0);
        roots[i].Bus = source->first_bus;
        roots[i].ConfigType = pci_ecam ? IA64_PLATFORM_PCI_CONFIG_ECAM :
            IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
        roots[i].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
        roots[i].ConfigBase = cpu_to_le64(
            pci_ecam ? ecam_base[i] : source->ioa_csr.base);
        roots[i].Mmio32Base = cpu_to_le64(source->pci_mmio.base);
        roots[i].Mmio32Size = cpu_to_le64(source->pci_mmio.size);
        roots[i].DmaBase = cpu_to_le64(
            IA64_ZX6000_EFI_TEST_IDENTITY_DMA_BASE);
        roots[i].DmaSize = cpu_to_le64(
            IA64_ZX6000_EFI_TEST_IDENTITY_DMA_SIZE);
        roots[i].Rope = cpu_to_le32(ctz32(source->rope_mask));
        roots[i].BusEnd = source->last_bus;
        roots[i].Mmio32TranslationOffset = cpu_to_le64(
            source->cpu_mmio.base - source->pci_mmio.base);
    }
}

static bool zx6000_efi_machine_install_descriptor(
    IA64Zx6000EfiTestMachineState *machine,
    const IA64ZX6000ZX1TestLayout *layout, Error **errp)
{
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    IA64PlatformDescriptor header;
    IA64PlatformRamRange ram = {
        .Base = cpu_to_le64(layout->ram.base),
        .Size = cpu_to_le64(layout->ram.size),
    };
    IA64PlatformPciRoot roots[IA64_ZX6000_ZX1_TEST_ROOT_COUNT];
    IA64PlatformIoSapic sapics[IA64_ZX6000_ZX1_TEST_ROOT_COUNT] = { 0 };
    IA64PlatformPciRoute routes[IA64_ZX6000_ZX1_TEST_ROOT_COUNT] = { 0 };
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = &ram,
        .ram_range_count = 1,
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = sapics,
        .io_sapic_count = G_N_ELEMENTS(sapics),
        .pci_routes = routes,
        .pci_route_count = G_N_ELEMENTS(routes),
    };
    unsigned int i;

    zx6000_efi_descriptor_header_init(&header, layout, machine);
    zx6000_efi_descriptor_roots_init(roots, layout, machine->pci_ecam);
    for (i = 0; i < IA64_ZX6000_ZX1_TEST_ROOT_COUNT; i++) {
        sapics[i].Base = cpu_to_le64(
            layout->roots[i].ioa_csr.base +
            (machine->pci_ecam ? 0 : IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET));
        sapics[i].GsiBase = cpu_to_le32(zx6000_efi_gsi_base[i]);
        sapics[i].RedirectionEntries = cpu_to_le32(
            IA64_ZX6000_ZX1_TEST_PCI_INPUT_COUNT);
        sapics[i].Version = cpu_to_le32(ZX6000_EFI_TEST_IO_SAPIC_VERSION);
        sapics[i].Id = i;

        routes[i].Segment = roots[i].Segment;
        routes[i].Bus = layout->roots[i].first_bus;
        routes[i].Device = IA64_ZX6000_EFI_TEST_PROBE_SLOT;
        routes[i].Pin = 0;
        routes[i].Gsi = cpu_to_le32(zx6000_efi_gsi_base[i]);
    }

    if (!hp_ia64_machine_install_platform_descriptor(
            hp, &header, &arrays, errp)) {
        return false;
    }
    if (hp->firmware_args.descriptor_size !=
        IA64_ZX6000_EFI_TEST_DESCRIPTOR_SIZE) {
        error_setg(errp,
                   "zx6000 EFI descriptor has unexpected size %"
                   PRIu64, hp->firmware_args.descriptor_size);
        return false;
    }
    return true;
}

static IA64BootInfo zx6000_efi_machine_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    IA64Zx6000EfiTestMachineState *machine =
        IA64_ZX6000_EFI_TEST_MACHINE(opaque);
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    IA64ZX6000ZX1TestLayout layout;
    const uint64_t low_ram_end = hp->descriptor_low_ram_end;
    const uint64_t cpu_assist_base = low_ram_end - IA64_FW_BOOT_STACK_SIZE;
    bool applied;
    IA64BootInfo info;

    ia64_zx6000_zx1_test_layout_init(&layout);
    info = (IA64BootInfo) {
        .firmware_base = IA64_PLATFORM_FIRMWARE_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = ZX6000_EFI_TEST_IVT_BASE,
        .bsp = cpu_assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        .stack_pointer = cpu_index == 0 ? low_ram_end - 16 :
            cpu_assist_base + IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_end,
        .io_port_base = IA64_ZX6000_EFI_TEST_LEGACY_IO_BASE,
        .interrupt_block_base = layout.pib.base,
        .powered_off = cpu_index != 0,
        .platform_addresses_valid = true,
    };
    applied = hp_ia64_machine_apply_platform_firmware_args(hp, &info);
    g_assert(applied);
    return info;
}

static IA64BootInfo zx6000_efi_machine_initial_boot_info(unsigned int cpu_index,
                                                  void *opaque)
{
    return zx6000_efi_machine_boot_info(cpu_index, IA64_PLATFORM_FIRMWARE_BASE,
                                IA64_PLATFORM_FIRMWARE_BASE, opaque);
}

static IA64BootInfo zx6000_efi_machine_firmware_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    return zx6000_efi_machine_boot_info(cpu_index, entry, global_pointer,
                                        opaque);
}

static bool zx6000_efi_machine_build(MachineState *ms, Error **errp)
{
    IA64Zx6000EfiTestMachineState *machine =
        IA64_ZX6000_EFI_TEST_MACHINE(ms);
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    IA64ZX6000ZX1TestLayout layout;
    IA64MachineCpuConfig cpu_config = {
        .alat_full = hp->alat_full,
        .boot_info = zx6000_efi_machine_initial_boot_info,
        .boot_info_opaque = machine,
    };

    ia64_zx6000_zx1_test_layout_init(&layout);
    if (!ia64_zx6000_zx1_test_layout_validate(&layout, errp) ||
        !hp_ia64_machine_validate(hp, errp)) {
        return false;
    }
    if (g_strcmp0(ms->cpu_type,
                  IA64_CPU_TYPE_NAME("madison-zx6000")) != 0) {
        error_setg(errp, "%s requires the Madison zx6000 CPU profile",
                   TYPE_IA64_ZX6000_EFI_TEST_MACHINE);
        return false;
    }
    if (ms->ram == NULL || memory_region_size(ms->ram) != layout.ram.size) {
        error_setg(errp, "%s requires one flat 512-MiB RAM region",
                   TYPE_IA64_ZX6000_EFI_TEST_MACHINE);
        return false;
    }

    memory_region_add_subregion(get_system_memory(), layout.ram.base,
                                ms->ram);
    if (!hp_ia64_machine_create_ras(
            hp, IA64_RAS_HUB_DEFAULT_BASE, errp)) {
        return false;
    }
    ia64_machine_map_pib(
        OBJECT(machine), &hp->pib, "ia64-zx6000-efi-test.pib",
        layout.pib.base, IA64_ZX6000_EFI_TEST_LOCAL_SAPIC_SIZE);
    if (!zx6000_efi_machine_create_topology(machine, errp) ||
        !zx6000_efi_machine_install_descriptor(machine, &layout, errp) ||
        !ia64_machine_create_cpus(ms, &cpu_config, errp) ||
        !ia64_machine_load_firmware(
            ms, IA64_PLATFORM_FIRMWARE_BASE,
            IA64_PLATFORM_FIRMWARE_SIZE, &hp->firmware_size, errp)) {
        return false;
    }

    ia64_machine_init_firmware_notifier(
        &hp->firmware_notifier, ms, IA64_PLATFORM_FIRMWARE_BASE,
        hp->firmware_size, zx6000_efi_machine_firmware_boot_info,
        NULL, machine);
    return true;
}

static void zx6000_efi_machine_init(MachineState *machine)
{
    Error *err = NULL;

    if (!zx6000_efi_machine_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void zx6000_efi_machine_reset(MachineState *machine, ResetType type)
{
    (void)machine;
    qemu_devices_reset(type);
    ia64_machine_reset_cpus();
}

static bool zx6000_efi_machine_get_pci_ecam(Object *obj, Error **errp)
{
    (void)errp;
    return IA64_ZX6000_EFI_TEST_MACHINE(obj)->pci_ecam;
}

static void zx6000_efi_machine_set_pci_ecam(Object *obj, bool value,
                                             Error **errp)
{
    (void)errp;
    IA64_ZX6000_EFI_TEST_MACHINE(obj)->pci_ecam = value;
}

static void zx6000_efi_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "IA-64 zx6000 EFI integration test machine";
    mc->init = zx6000_efi_machine_init;
    mc->reset = zx6000_efi_machine_reset;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("madison-zx6000");
    mc->default_ram_size = IA64_ZX6000_ZX1_TEST_RAM_SIZE;
    mc->default_ram_id = "ia64-zx6000-efi-test.ram";
    mc->default_display = "none";
    mc->max_cpus = 2;
    mc->default_cpus = 1;
    mc->smp_props.prefer_sockets = true;
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;

    hmc->platform_id = IA64_PLATFORM_ID_HP_ZX6000;
    hmc->minimum_ram_size = IA64_ZX6000_ZX1_TEST_RAM_SIZE;
    hmc->maximum_ram_size = IA64_ZX6000_ZX1_TEST_RAM_SIZE;
    hmc->descriptor_gpa = IA64_ZX6000_EFI_TEST_DESCRIPTOR_GPA;

    object_class_property_add_bool(oc, "x-pci-ecam",
                                   zx6000_efi_machine_get_pci_ecam,
                                   zx6000_efi_machine_set_pci_ecam);
}

static const TypeInfo zx6000_efi_test_types[] = {
    {
        .name = TYPE_IA64_ZX6000_EFI_TEST_TOPOLOGY,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64Zx6000EfiTestTopologyState),
        .class_init = zx6000_efi_topology_class_init,
    }, {
        .name = TYPE_IA64_ZX6000_EFI_TEST_MACHINE,
        .parent = TYPE_HP_IA64_MACHINE,
        .instance_size = sizeof(IA64Zx6000EfiTestMachineState),
        .class_init = zx6000_efi_machine_class_init,
    },
};

DEFINE_TYPES(zx6000_efi_test_types)
