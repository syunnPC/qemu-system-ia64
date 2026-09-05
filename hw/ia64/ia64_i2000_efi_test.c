/*
 * IA-64 i2000 EFI test machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/ia64/hp_ia64.h"
#include "hw/ia64/ia64_i2000_460gx_test.h"
#include "hw/ia64/ia64_i2000_460gx_test_layout.h"
#include "hw/ia64/ia64_i2000_efi_test.h"
#include "hw/ia64/ia64_i2000_io_test.h"
#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "hw/ia64/ia64_platform.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/scsi/isp12160_abi.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/blockdev.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ia64/cpu-qom.h"

#define IA64_I2000_EFI_TEST_TOPOLOGY(obj) \
    OBJECT_CHECK(IA64I2000EfiTestTopologyState, (obj), \
                 TYPE_IA64_I2000_EFI_TEST_TOPOLOGY)

#define IA64_I2000_EFI_TEST_IVT_BASE UINT64_C(0x00010000)
#define IA64_I2000_EFI_TEST_PID_VERSION 0x21U

typedef struct IA64I2000EfiTestTopologyState {
    DeviceState parent_obj;

    IA64I2000460GXTestState *test_460gx;
    IA64I2000IoTestState *io_test;
    Chardev *uart_chardev;
    DriveInfo *cd_drive;
} IA64I2000EfiTestTopologyState;

struct IA64I2000EfiTestMachineState {
    HPIA64MachineState parent_obj;

    MemoryRegion nvram;
    IA64I2000EfiTestTopologyState *topology;
};

enum {
    IA64_I2000_EFI_TEST_CALCULATED_DESCRIPTOR_SIZE =
        sizeof(IA64PlatformDescriptor) +
        sizeof(IA64PlatformRamRange) +
        IA64_I2000_460GX_TEST_ROOT_COUNT *
            sizeof(IA64PlatformPciRoot) +
        sizeof(IA64PlatformIoSapic) +
        2 * sizeof(IA64PlatformPciRoute) +
        sizeof(IA64PlatformI2000Profile),
};

G_STATIC_ASSERT(IA64_I2000_EFI_TEST_DESCRIPTOR_GPA ==
                IA64_I2000_460GX_TEST_DESC_ROM_BASE);
G_STATIC_ASSERT(IA64_I2000_EFI_TEST_CALCULATED_DESCRIPTOR_SIZE ==
                IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE);

static DeviceState *i2000_topology_add_child(DeviceState *parent,
                                              const char *name,
                                              const char *type)
{
    DeviceState *child = qdev_new(type);

    object_property_add_child(OBJECT(parent), name, OBJECT(child));
    object_unref(OBJECT(child));
    return child;
}

static void i2000_topology_remove_child(DeviceState **child)
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

static void i2000_topology_cleanup(
    IA64I2000EfiTestTopologyState *topology)
{
    DeviceState *child = topology->io_test ? DEVICE(topology->io_test) : NULL;

    i2000_topology_remove_child(&child);
    topology->io_test = NULL;
    child = topology->test_460gx ? DEVICE(topology->test_460gx) : NULL;
    i2000_topology_remove_child(&child);
    topology->test_460gx = NULL;
}

static void i2000_topology_realize(DeviceState *dev, Error **errp)
{
    IA64I2000EfiTestTopologyState *topology =
        IA64_I2000_EFI_TEST_TOPOLOGY(dev);
    IA64I2000IoTestLayout io_test_layout;
    MachineState *machine = MACHINE(qdev_get_machine());
    DeviceState *child;
    Error *local_err = NULL;

    if (machine->ram == NULL) {
        error_setg(errp, "%s requires parent-machine RAM",
                   TYPE_IA64_I2000_EFI_TEST_TOPOLOGY);
        return;
    }
    ia64_i2000_io_test_layout_init(&io_test_layout);

    child = i2000_topology_add_child(
        dev, IA64_I2000_EFI_TEST_460GX_TEST_CHILD,
        TYPE_IA64_I2000_460GX_TEST);
    topology->test_460gx = IA64_I2000_460GX_TEST(child);
    qdev_prop_set_uint32(child, IA64_I2000_460GX_TEST_PROP_LEGACY_PIN,
                        io_test_layout.pid_legacy_pin);
    if (!object_property_set_link(OBJECT(child),
                                  IA64_I2000_460GX_TEST_PROP_RAM,
                                  OBJECT(machine->ram), &local_err) ||
        !qdev_realize(child, NULL, &local_err)) {
        goto fail;
    }

    child = i2000_topology_add_child(
        dev, IA64_I2000_EFI_TEST_IO_TEST_CHILD,
        TYPE_IA64_I2000_IO_TEST);
    topology->io_test = IA64_I2000_IO_TEST(child);
    if (!object_property_set_link(OBJECT(child),
                                  IA64_I2000_IO_TEST_PROP_460GX_TEST,
                                  OBJECT(topology->test_460gx), &local_err) ||
        !ia64_i2000_io_test_set_uart_chardev(
            topology->io_test, topology->uart_chardev, &local_err) ||
        !ia64_i2000_io_test_set_cd_drive(
            topology->io_test, topology->cd_drive, &local_err) ||
        !qdev_realize(child, NULL, &local_err)) {
        goto fail;
    }
    return;

fail:
    i2000_topology_cleanup(topology);
    error_propagate(errp, local_err);
}

static void i2000_topology_unrealize(DeviceState *dev)
{
    i2000_topology_cleanup(IA64_I2000_EFI_TEST_TOPOLOGY(dev));
}

static void i2000_topology_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->desc = "i2000 460GX and I/O test topology";
    dc->realize = i2000_topology_realize;
    dc->unrealize = i2000_topology_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
}

static bool i2000_machine_create_topology(
    IA64I2000EfiTestMachineState *machine, Error **errp)
{
    DeviceState *dev = qdev_new(TYPE_IA64_I2000_EFI_TEST_TOPOLOGY);
    IA64I2000EfiTestTopologyState *topology =
        IA64_I2000_EFI_TEST_TOPOLOGY(dev);

    object_property_add_child(OBJECT(machine),
                              IA64_I2000_EFI_TEST_TOPOLOGY_CHILD,
                              OBJECT(dev));
    object_unref(OBJECT(dev));
    machine->topology = topology;
    topology->uart_chardev = serial_hd(0);
    topology->cd_drive = drive_get(
        IF_IDE, 0, IA64_I2000_IO_TEST_IDE_MASTER_UNIT);

    if (!qdev_realize(dev, NULL, errp)) {
        object_unparent(OBJECT(dev));
        machine->topology = NULL;
        return false;
    }
    return true;
}

static void i2000_descriptor_header_init(
    IA64PlatformDescriptor *header,
    const IA64I2000460GXTestLayout *layout,
    const MachineState *machine)
{
    *header = (IA64PlatformDescriptor) {
        .Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC),
        .FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION),
        .PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_I2000),
        .Flags = cpu_to_le32(IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS),
        .RamSize = cpu_to_le64(layout->ram.size),
        .LowRamEnd = cpu_to_le64(layout->ram.size),
        .FirmwareBase = cpu_to_le64(layout->firmware.base),
        .FirmwareSize = cpu_to_le64(layout->firmware.size),
        .ProcessorCount = cpu_to_le32(machine->smp.cpus),
        .SocketCount = cpu_to_le32(machine->smp.sockets),
        .CoresPerSocket = cpu_to_le32(machine->smp.cores),
        .ThreadsPerCore = cpu_to_le32(machine->smp.threads),
        .PhysicalAddressBits = cpu_to_le32(
            IA64_PLATFORM_I2000_PHYS_ADDR_BITS),
        .MaxSockets = cpu_to_le32(2),
        .MaxCoresPerSocket = cpu_to_le32(1),
        .MaxThreadsPerCore = cpu_to_le32(1),
        .MaxPciRoots = cpu_to_le32(
            IA64_I2000_460GX_TEST_ROOT_COUNT),
        .PciRootIdentity = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_IDENTITY_GENERIC),
        .NumaNodeCount = cpu_to_le32(1),
        .LegacyIoBase = cpu_to_le64(layout->legacy_io.base),
        .LegacyIoSize = cpu_to_le64(layout->legacy_io.size),
        .LocalSapicBase = cpu_to_le64(layout->pib.base),
        .LocalSapicSize = cpu_to_le64(layout->pib.size),
        .ConsoleBase = cpu_to_le64(
            ia64_i2000_460gx_test_sparse_io_pa(
                IA64_I2000_IO_TEST_UART_BASE)),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(
            IA64_I2000_IO_TEST_UART_INPUT_CLOCK_HZ),
        /* No interrupt is advertised; GSI is zero. */
        .ConsoleIrq = 0,
        .NvramBase = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_BASE),
        .NvramSize = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_SIZE),
        .RtcBase = 0,
        .RtcSize = 0,
        .RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE),
        .RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE),
    };
    header->NumaNode[0].ProcessorCount = cpu_to_le32(machine->smp.cpus);
    header->NumaNode[0].RamRangeMask = cpu_to_le32(1);
    header->NumaNode[0].Distance[0] = 10;
}

static void i2000_descriptor_roots_init(
    IA64PlatformPciRoot roots[IA64_I2000_460GX_TEST_ROOT_COUNT],
    const IA64I2000460GXTestLayout *layout)
{
    unsigned int i;

    memset(roots, 0,
           sizeof(*roots) * IA64_I2000_460GX_TEST_ROOT_COUNT);
    for (i = 0; i < IA64_I2000_460GX_TEST_ROOT_COUNT; i++) {
        const IA64I2000460GXTestRoot *source = &layout->roots[i];

        roots[i].Segment = cpu_to_le16(source->segment);
        roots[i].Bus = source->first_bus;
        roots[i].ConfigType = source->config_mechanism;
        roots[i].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA);
        roots[i].ConfigBase = 0;
        roots[i].IoBase = cpu_to_le64(source->io_base);
        roots[i].IoSize = cpu_to_le64(source->io_size);
        roots[i].Mmio32Base = cpu_to_le64(source->pci_mmio32_base);
        roots[i].Mmio32Size = cpu_to_le64(source->mmio32_size);
        roots[i].Mmio64Base = cpu_to_le64(source->mmio64_base);
        roots[i].Mmio64Size = cpu_to_le64(source->mmio64_size);
        roots[i].DmaBase = cpu_to_le64(source->dma_base);
        roots[i].DmaSize = cpu_to_le64(source->dma_size);
        /* CF8/CFC roots require Rope and all translations to be zero. */
        roots[i].Rope = 0;
        roots[i].BusEnd = source->last_bus;
    }
}

static bool i2000_machine_install_descriptor(
    IA64I2000EfiTestMachineState *machine,
    const IA64I2000460GXTestLayout *layout_460gx_test,
    Error **errp)
{
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    MachineState *ms = MACHINE(machine);
    IA64PlatformDescriptor header;
    IA64PlatformRamRange ram = {
        .Base = cpu_to_le64(layout_460gx_test->ram.base),
        .Size = cpu_to_le64(layout_460gx_test->ram.size),
    };
    IA64PlatformPciRoot roots[IA64_I2000_460GX_TEST_ROOT_COUNT];
    IA64PlatformIoSapic io_sapic = {
        .Base = cpu_to_le64(layout_460gx_test->pid_decode.base),
        .GsiBase = cpu_to_le32(0),
        .RedirectionEntries = cpu_to_le32(layout_460gx_test->pid_pin_count),
        .Version = cpu_to_le32(IA64_I2000_EFI_TEST_PID_VERSION),
        .Id = layout_460gx_test->pid_id,
    };
    IA64PlatformPciRoute pci_routes[] = {
        {
            .Segment = cpu_to_le16(0),
            .Bus = layout_460gx_test->roots[
                IA64_I2000_IO_TEST_I82559_PARENT_ROOT].first_bus,
            .Device = IA64_I2000_IO_TEST_I82559_SLOT,
            /* Descriptor pins use zero-based PCI INTA..INTD numbering. */
            .Pin = IA64_I2000_IO_TEST_I82559_INTERRUPT_PIN - 1,
            .Gsi = cpu_to_le32(IA64_I2000_IO_TEST_I82559_PID_PIN),
        }, {
            .Segment = cpu_to_le16(ISP12160_QEMU_I2000_SEGMENT),
            .Bus = ISP12160_QEMU_I2000_BUS,
            .Device = ISP12160_QEMU_I2000_DEVICE,
            .Pin = ISP12160_QEMU_I2000_INTERRUPT_PIN - 1,
            .Gsi = cpu_to_le32(ISP12160_QEMU_I2000_GSI),
        },
    };
    IA64PlatformI2000Profile profile;
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = &ram,
        .ram_range_count = 1,
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = &io_sapic,
        .io_sapic_count = 1,
        .pci_routes = pci_routes,
        .pci_route_count = G_N_ELEMENTS(pci_routes),
        .profiles = &profile,
        .profile_count = 1,
    };

    i2000_descriptor_header_init(&header, layout_460gx_test, ms);
    i2000_descriptor_roots_init(roots, layout_460gx_test);
    ia64_platform_i2000_profile_init(&profile);
    if (!hp_ia64_machine_install_platform_descriptor(
            hp, &header, &arrays, errp)) {
        return false;
    }
    if (hp->firmware_args.descriptor_size !=
        IA64_I2000_EFI_TEST_DESCRIPTOR_SIZE) {
        error_setg(errp,
                   "i2000 EFI descriptor has unexpected size %" PRIu64,
                   hp->firmware_args.descriptor_size);
        return false;
    }
    return true;
}

static IA64BootInfo i2000_machine_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    IA64I2000EfiTestMachineState *machine =
        IA64_I2000_EFI_TEST_MACHINE(opaque);
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    const uint64_t low_ram_end = hp->descriptor_low_ram_end;
    const uint64_t cpu_assist_base = low_ram_end - IA64_FW_BOOT_STACK_SIZE;
    bool applied;
    IA64BootInfo info = {
        .firmware_base = IA64_I2000_460GX_TEST_FIRMWARE_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = IA64_I2000_EFI_TEST_IVT_BASE,
        .bsp = cpu_assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        /* The descriptor transport requires the BSP at LowRamEnd - 16. */
        .stack_pointer = cpu_index == 0 ? low_ram_end - 16 :
            cpu_assist_base + IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_end,
        .io_port_base = IA64_I2000_460GX_TEST_LEGACY_IO_BASE,
        .interrupt_block_base = IA64_I2000_460GX_TEST_PIB_BASE,
        .powered_off = cpu_index != 0,
        .platform_addresses_valid = true,
    };

    applied = hp_ia64_machine_apply_platform_firmware_args(hp, &info);
    g_assert(applied);
    return info;
}

static IA64BootInfo i2000_machine_initial_boot_info(unsigned int cpu_index,
                                                    void *opaque)
{
    return i2000_machine_boot_info(
        cpu_index, IA64_I2000_460GX_TEST_FIRMWARE_BASE,
        IA64_I2000_460GX_TEST_FIRMWARE_BASE, opaque);
}

static IA64BootInfo i2000_machine_firmware_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    return i2000_machine_boot_info(cpu_index, entry, global_pointer, opaque);
}

static bool i2000_machine_build(MachineState *ms, Error **errp)
{
    IA64I2000EfiTestMachineState *machine =
        IA64_I2000_EFI_TEST_MACHINE(ms);
    HPIA64MachineState *hp = HP_IA64_MACHINE(machine);
    IA64I2000460GXTestLayout layout_460gx_test;
    IA64I2000IoTestLayout io_test_layout;
    IA64MachineCpuConfig cpu_config = {
        .alat_full = hp->alat_full,
        .boot_info = i2000_machine_initial_boot_info,
        .boot_info_opaque = machine,
    };

    ia64_i2000_460gx_test_layout_init(&layout_460gx_test);
    ia64_i2000_io_test_layout_init(&io_test_layout);
    if (!ia64_i2000_460gx_test_layout_validate(&layout_460gx_test, errp) ||
        !ia64_i2000_io_test_layout_validate(&io_test_layout, errp) ||
        !hp_ia64_machine_validate(hp, errp)) {
        return false;
    }
    if (g_strcmp0(ms->cpu_type, IA64_CPU_TYPE_NAME("merced")) != 0) {
        error_setg(errp, "%s requires the Merced CPU model",
                   TYPE_IA64_I2000_EFI_TEST_MACHINE);
        return false;
    }
    if (ms->ram == NULL ||
        memory_region_size(ms->ram) != layout_460gx_test.ram.size) {
        error_setg(errp, "%s requires one flat two-GiB RAM region",
                   TYPE_IA64_I2000_EFI_TEST_MACHINE);
        return false;
    }

    memory_region_add_subregion(get_system_memory(), layout_460gx_test.ram.base,
                                ms->ram);
    if (!hp_ia64_machine_create_ras(
            hp, IA64_RAS_HUB_DEFAULT_BASE, errp)) {
        return false;
    }
    if (!memory_region_init_ram(
            &machine->nvram, NULL,
            "ia64-i2000-efi-test.nvram",
            IA64_I2000_PROFILE_NVRAM_SIZE, errp)) {
        return false;
    }
    memory_region_add_subregion(get_system_memory(),
                                IA64_I2000_PROFILE_NVRAM_BASE,
                                &machine->nvram);
    ia64_machine_map_pib(OBJECT(machine), &hp->pib,
                         "ia64-i2000-efi-test.pib",
                         layout_460gx_test.pib.base,
                         layout_460gx_test.pib.size);
    if (!i2000_machine_create_topology(machine, errp) ||
        !i2000_machine_install_descriptor(machine, &layout_460gx_test,
                                           errp)) {
        return false;
    }
    cpu_config.firmware_compat_flags =
        hp->firmware_args.firmware_compat_flags;
    if (!ia64_machine_create_cpus(ms, &cpu_config, errp) ||
        !ia64_machine_load_firmware(ms, layout_460gx_test.firmware.base,
                                    layout_460gx_test.firmware.size,
                                    &hp->firmware_size, errp)) {
        return false;
    }

    ia64_machine_init_firmware_notifier(
        &hp->firmware_notifier, ms, layout_460gx_test.firmware.base,
        hp->firmware_size, i2000_machine_firmware_boot_info,
        NULL, machine);
    return true;
}

static void i2000_machine_init(MachineState *machine)
{
    Error *err = NULL;

    if (!i2000_machine_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void i2000_machine_reset(MachineState *machine, ResetType type)
{
    IA64I2000EfiTestMachineState *s =
        IA64_I2000_EFI_TEST_MACHINE(machine);

    qemu_devices_reset(type);
    if (s->topology) {
        ia64_i2000_io_test_restore_pci_resources(s->topology->io_test);
    }
    ia64_machine_reset_cpus();
}

static void i2000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "IA-64 i2000 EFI test machine";
    mc->init = i2000_machine_init;
    mc->reset = i2000_machine_reset;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("merced");
    mc->default_ram_size = IA64_I2000_460GX_TEST_RAM_SIZE;
    mc->default_ram_id = "ia64-i2000-efi-test.ram";
    mc->default_display = "none";
    mc->max_cpus = 2;
    mc->default_cpus = 1;
    mc->smp_props.prefer_sockets = true;
    mc->block_default_type = IF_SCSI;
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 0;

    hmc->platform_id = IA64_PLATFORM_ID_HP_I2000;
    hmc->minimum_ram_size = IA64_I2000_460GX_TEST_RAM_SIZE;
    hmc->maximum_ram_size = IA64_I2000_460GX_TEST_RAM_SIZE;
    hmc->descriptor_gpa = IA64_I2000_EFI_TEST_DESCRIPTOR_GPA;
}

static const TypeInfo i2000_efi_test_types[] = {
    {
        .name = TYPE_IA64_I2000_EFI_TEST_TOPOLOGY,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(IA64I2000EfiTestTopologyState),
        .class_init = i2000_topology_class_init,
    }, {
        .name = TYPE_IA64_I2000_EFI_TEST_MACHINE,
        .parent = TYPE_HP_IA64_MACHINE,
        .instance_size = sizeof(IA64I2000EfiTestMachineState),
        .class_init = i2000_machine_class_init,
    },
};

DEFINE_TYPES(i2000_efi_test_types)
