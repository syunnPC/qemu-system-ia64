/*
 * HP i2000 workstation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/audio/cs4281.h"
#include "hw/display/ati_int.h"
#include "hw/display/nvidia_quadro2.h"
#include "hw/ia64/hp_int10.h"
#include "hw/ia64/hp_i2000.h"
#include "hw/ia64/hp_ia64.h"
#include "hw/ia64/intel_460gx_chipset.h"
#include "hw/ia64/intel_460gx_dma.h"
#include "hw/ia64/intel_460gx_host.h"
#include "hw/ia64/intel_460gx_ihpc.h"
#include "hw/ia64/intel_460gx_pid.h"
#include "hw/ia64/intel_460gx_root.h"
#include "hw/ide/ide-bus.h"
#include "hw/isa/isa.h"
#include "hw/isa/lpc47b27.h"
#include "hw/pci/pci.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/scsi/isp12160.h"
#include "hw/scsi/isp12160_abi.h"
#include "hw/scsi/scsi.h"
#include "hw/southbridge/intel_82468gx.h"
#include "hw/timer/i8254.h"
#include "hw/usb/usb.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/blockdev.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ia64/cpu-qom.h"

#define HP_I2000_FIRMWARE_BASE       UINT64_C(0x00100000)
#define HP_I2000_FIRMWARE_SIZE       UINT64_C(0x00200000)
#define HP_I2000_PID_BASE            UINT64_C(0xfec00000)
#define HP_I2000_PID_SIZE            UINT64_C(0x00001000)
#define HP_I2000_PIB_SIZE            UINT64_C(0x00200000)
#define HP_I2000_LEGACY_IO_SIZE      UINT64_C(0x04000000)
#define HP_I2000_IVT_BASE            UINT64_C(0x00010000)
#define HP_I2000_CBN                 UINT8_C(0x04)
#define HP_I2000_PID_ID              UINT8_C(0)
#define HP_I2000_PID_LEGACY_PIN      UINT32_C(0)
#define HP_I2000_PID_VERSION         UINT32_C(0x21)
#define HP_I2000_PCI_INTX_COUNT      4U
#define HP_I2000_EXPANDER_MASK       (BIT(0) | BIT(2) | BIT(3) | BIT(4))
#define HP_I2000_VGA_SLOT            0U
#define HP_I2000_VGA_GSI             28U
#define HP_I2000_IFB_SLOT            3U
#define HP_I2000_IFB_IDE_FUNCTION    1U
#define HP_I2000_IFB_USB_FUNCTION    2U
#define HP_I2000_IFB_USB_PIN         3U
#define HP_I2000_IFB_USB_GSI         19U
#define HP_I2000_IFB_USB_IO_BAR      UINT32_C(0x00001100)
#define HP_I2000_I82559_SLOT         5U
#define HP_I2000_I82559_GSI          16U
#define HP_I2000_CS4281_SLOT         4U
#define HP_I2000_CS4281_GSI          16U
#define HP_I2000_CS4281_BA1          UINT32_C(0x98000000)
#define HP_I2000_CS4281_BA0          UINT32_C(0x98010000)
#define HP_I2000_I82559_MMIO_BAR     UINT32_C(0x95000000)
#define HP_I2000_I82559_IO_BAR       UINT32_C(0x00001000)
#define HP_I2000_I82559_FLASH_BAR    UINT32_C(0x95100000)
#define HP_I2000_ISP12160_IO_BAR     UINT32_C(0x00005000)
#define HP_I2000_IHPC0_MMIO_BAR      UINT32_C(0xa0020000)
#define HP_I2000_IHPC1_MMIO_BAR      UINT32_C(0xb0020000)
#define HP_I2000_UART_PORT           UINT16_C(0x03f8)
#define HP_I2000_UART_CLOCK_HZ       UINT32_C(1843200)
#define HP_I2000_SUPERIO_PORT        UINT16_C(0x002e)
#define HP_I2000_RTC_PORT            UINT16_C(0x0070)
#define HP_I2000_RTC_IRQ             8U
#define HP_I2000_PIT_PORT            UINT16_C(0x0040)
#define HP_I2000_PIT_IRQ             0U
#define HP_I2000_RAGE128_FB_BAR      UINT32_C(0xe8000000)
#define HP_I2000_RAGE128_IO_BAR      UINT32_C(0x0000c000)
#define HP_I2000_RAGE128_MMIO_BAR    UINT32_C(0xe7000000)
#define HP_I2000_QUADRO2_FB_BAR      UINT32_C(0xe8000000)
#define HP_I2000_QUADRO2_MMIO_BAR    UINT32_C(0xe7000000)
#define HP_I2000_VGA_LEGACY_BASE     UINT64_C(0x000a0000)
#define HP_I2000_VGA_LEGACY_SIZE     UINT64_C(0x00020000)
#define HP_I2000_IDE_CHANNELS         2U

#define TYPE_HP_I2000_CF8_SUBWORD "hp-i2000-cf8-subword"
OBJECT_DECLARE_SIMPLE_TYPE(HPI2000CF8SubwordState, HP_I2000_CF8_SUBWORD)

#define TYPE_HP_I2000_NVRAM "hp-i2000-nvram"
OBJECT_DECLARE_SIMPLE_TYPE(HPI2000NvramState, HP_I2000_NVRAM)

#define TYPE_HP_I2000_PID_PCI "hp-i2000-pid-pci"

struct HPI2000CF8SubwordState {
    DeviceState parent_obj;

    MemoryRegion io;
    uint8_t bytes[4];
};

struct HPI2000NvramState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint8_t data[IA64_I2000_PROFILE_NVRAM_SIZE];
    char *path;
    bool write_warning;
};

typedef struct HPI2000RootLayout {
    uint8_t first_bus;
    uint8_t last_bus;
    uint8_t host_port;
    uint8_t intx_base;
    uint32_t io_base;
    uint32_t io_size;
    uint64_t mmio_base;
    uint64_t mmio_size;
} HPI2000RootLayout;

typedef struct HPI2000ChipsetFaultRoute {
    Intel460GXChipsetState *chipset;
    unsigned int port;
} HPI2000ChipsetFaultRoute;

struct HPI2000MachineState {
    HPIA64MachineState parent_obj;

    MemoryRegion low_ram;
    MemoryRegion high_ram;
    MemoryRegion sparse_io;
    MemoryRegion root_mmio[HP_I2000_PCI_ROOT_COUNT];
    MemoryRegion vga_legacy;
    AddressSpace host_conf_as;
    AddressSpace host_data_as;
    AddressSpace root_io[HP_I2000_PCI_ROOT_COUNT];

    Intel460GXHostState *host;
    Intel460GXChipsetState *chipset;
    Intel460GXPIDState *pid;
    HPI2000CF8SubwordState *cf8_subword;
    HPI2000NvramState *nvram;
    Intel460GXRootHostState *roots[HP_I2000_PCI_ROOT_COUNT];
    Intel460GXDMA *dma[HP_I2000_PCI_ROOT_COUNT];
    HPI2000ChipsetFaultRoute dma_fault_route[HP_I2000_PCI_ROOT_COUNT];
    Intel82468GXIFBState *ifb;
    PCIDevice *vga;
    HPIA64Int10 int10;
    PCIDevice *pci_pid;
    PCIDevice *audio;
    PCIDevice *ihpc[2];
    HPI2000ChipsetFaultRoute ihpc_fault_route[2];
    PCIDevice *i82559;
    PCIDevice *isp12160;

    bool host_spaces_initialized;
    bool root_io_initialized[HP_I2000_PCI_ROOT_COUNT];
    char *nvram_path;
};

static const HPI2000RootLayout hp_i2000_roots[] = {
    {
        .first_bus = 0x00,
        .last_bus = 0x00,
        .host_port = 0,
        .intx_base = 16,
        .io_base = 0x0000,
        .io_size = 0x4000,
        .mmio_base = UINT64_C(0x90000000),
        .mmio_size = UINT64_C(0x10000000),
    }, {
        .first_bus = 0x01,
        .last_bus = 0x01,
        .host_port = 2,
        .intx_base = 20,
        .io_base = 0x4000,
        .io_size = 0x4000,
        .mmio_base = UINT64_C(0xa0000000),
        .mmio_size = UINT64_C(0x10000000),
    }, {
        .first_bus = 0x02,
        .last_bus = 0x02,
        .host_port = 3,
        .intx_base = 24,
        .io_base = 0x8000,
        .io_size = 0x4000,
        .mmio_base = UINT64_C(0xb0000000),
        .mmio_size = UINT64_C(0x10000000),
    }, {
        .first_bus = 0x03,
        .last_bus = 0x03,
        .host_port = 4,
        .intx_base = 28,
        .io_base = 0xc000,
        .io_size = 0x4000,
        .mmio_base = UINT64_C(0xe0000000),
        .mmio_size = UINT64_C(0x10000000),
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(hp_i2000_roots) == HP_I2000_PCI_ROOT_COUNT);

enum {
    HP_I2000_LOW_RAM_DESCRIPTOR_SIZE =
        sizeof(IA64PlatformDescriptor) +
        sizeof(IA64PlatformRamRange) +
        HP_I2000_PCI_ROOT_COUNT * sizeof(IA64PlatformPciRoot) +
        sizeof(IA64PlatformIoSapic) +
        5 * sizeof(IA64PlatformPciRoute) +
        sizeof(IA64PlatformI2000Profile),
};

static DeviceState *hp_i2000_add_child(HPI2000MachineState *s,
                                        const char *name, const char *type)
{
    DeviceState *dev = qdev_new(type);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_unref(OBJECT(dev));
    return dev;
}

static bool hp_i2000_chipset_fault(void *opaque,
                                   const IA64ChipsetFault *fault)
{
    HPI2000ChipsetFaultRoute *route = opaque;

    return intel_460gx_chipset_report_expander_fault(
        route->chipset, route->port, fault);
}

static bool hp_i2000_nvram_persistent(const HPI2000NvramState *s)
{
    return s->path != NULL && g_strcmp0(s->path, "none") != 0;
}

static uint64_t hp_i2000_nvram_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    HPI2000NvramState *s = opaque;
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)s->data[addr + i] << (i * 8);
    }
    return value;
}

static void hp_i2000_nvram_commit(HPI2000NvramState *s)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) read_error = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = sizeof(s->data);

    if (!hp_i2000_nvram_persistent(s)) {
        return;
    }
    if (g_file_get_contents(s->path, &contents, &length, &read_error)) {
        if (length == 0) {
            length = sizeof(s->data);
        } else if (length != IA64_PLATFORM_MIN_NVRAM_SIZE &&
                   length != sizeof(s->data)) {
            if (!s->write_warning) {
                warn_report("refusing to overwrite i2000 NVRAM '%s': "
                            "expected %zu or %zu bytes, found %zu",
                            s->path,
                            (size_t)IA64_PLATFORM_MIN_NVRAM_SIZE,
                            sizeof(s->data), (size_t)length);
                s->write_warning = true;
            }
            return;
        }
    } else if (read_error &&
               !g_error_matches(read_error, G_FILE_ERROR,
                                G_FILE_ERROR_NOENT)) {
        if (!s->write_warning) {
            warn_report("failed to read i2000 NVRAM '%s' before saving: %s",
                        s->path, read_error->message);
            s->write_warning = true;
        }
        return;
    } else {
        length = sizeof(s->data);
    }
    if (!g_file_set_contents(s->path, (const char *)s->data,
                             length, &error) &&
        !s->write_warning) {
        warn_report("failed to save i2000 NVRAM '%s': %s",
                    s->path, error->message);
        s->write_warning = true;
    }
}

static void hp_i2000_nvram_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned int size)
{
    HPI2000NvramState *s = opaque;
    unsigned int i;

    if (addr == IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET &&
        size == sizeof(value) &&
        value == IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC) {
        hp_i2000_nvram_commit(s);
        return;
    }
    for (i = 0; i < size; i++) {
        s->data[addr + i] = value >> (i * 8);
    }
}

static const MemoryRegionOps hp_i2000_nvram_ops = {
    .read = hp_i2000_nvram_read,
    .write = hp_i2000_nvram_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static bool hp_i2000_nvram_load(HPI2000NvramState *s, Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = 0;

    memset(s->data, 0, sizeof(s->data));
    if (!hp_i2000_nvram_persistent(s)) {
        return true;
    }
    if (!g_file_get_contents(s->path, &contents, &length, &error)) {
        if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            return true;
        }
        error_setg(errp, "failed to load i2000 NVRAM '%s': %s",
                   s->path, error->message);
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (length > sizeof(s->data)) {
        error_setg(errp,
                   "i2000 NVRAM '%s' exceeds %zu bytes",
                   s->path, sizeof(s->data));
        return false;
    }
    if (length != IA64_PLATFORM_MIN_NVRAM_SIZE &&
        length != sizeof(s->data)) {
        error_setg(errp,
                   "i2000 NVRAM '%s' must be %zu or %zu bytes; found %zu",
                   s->path, (size_t)IA64_PLATFORM_MIN_NVRAM_SIZE,
                   sizeof(s->data), (size_t)length);
        return false;
    }
    memcpy(s->data, contents, length);
    return true;
}

static void hp_i2000_nvram_realize(DeviceState *dev, Error **errp)
{
    HPI2000NvramState *s = HP_I2000_NVRAM(dev);

    if (!hp_i2000_nvram_load(s, errp)) {
        return;
    }
    memory_region_init_io(&s->mmio, OBJECT(s), &hp_i2000_nvram_ops, s,
                          TYPE_HP_I2000_NVRAM,
                          IA64_I2000_PROFILE_NVRAM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static const VMStateDescription vmstate_hp_i2000_nvram = {
    .name = TYPE_HP_I2000_NVRAM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, HPI2000NvramState,
                            IA64_I2000_PROFILE_NVRAM_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static const Property hp_i2000_nvram_properties[] = {
    DEFINE_PROP_STRING("nvram", HPI2000NvramState, path),
};

static void hp_i2000_nvram_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->desc = "HP i2000 EFI NVRAM";
    dc->realize = hp_i2000_nvram_realize;
    dc->vmsd = &vmstate_hp_i2000_nvram;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    device_class_set_props(dc, hp_i2000_nvram_properties);
}

static const TypeInfo hp_i2000_nvram_type = {
    .name = TYPE_HP_I2000_NVRAM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HPI2000NvramState),
    .class_init = hp_i2000_nvram_class_init,
};

typedef struct HPI2000PciIdentity {
    const char *description;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint16_t class_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
} HPI2000PciIdentity;

static void hp_i2000_pci_identity_realize(PCIDevice *pdev, Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pdev);

    (void)errp;
    if (pc->device_id == 0x123d) {
        pci_config_set_prog_interface(pdev->config, 0x20);
        pci_set_word(pdev->config + PCI_SUBSYSTEM_VENDOR_ID, 0);
        pci_set_word(pdev->config + PCI_SUBSYSTEM_ID, 0);
    }
}

static void hp_i2000_pci_identity_class_init(ObjectClass *oc,
                                               const void *data)
{
    const HPI2000PciIdentity *identity = data;
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    dc->desc = identity->description;
    dc->vmsd = &vmstate_pci_device;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    pc->realize = hp_i2000_pci_identity_realize;
    pc->vendor_id = identity->vendor_id;
    pc->device_id = identity->device_id;
    pc->revision = identity->revision;
    pc->class_id = identity->class_id;
    pc->subsystem_vendor_id = identity->subsystem_vendor_id;
    pc->subsystem_id = identity->subsystem_id;
}

static const HPI2000PciIdentity hp_i2000_pid_pci_identity = {
    .description = "Intel 683053 Programmable Interrupt Device",
    .vendor_id = 0x8086,
    .device_id = 0x123d,
    .revision = 0x01,
    .class_id = PCI_CLASS_SYSTEM_PIC,
};

#define HP_I2000_PCI_IDENTITY_TYPE(_name, _identity) { \
    .name = (_name),                                      \
    .parent = TYPE_PCI_DEVICE,                            \
    .instance_size = sizeof(PCIDevice),                   \
    .class_init = hp_i2000_pci_identity_class_init,       \
    .class_data = (void *)&(_identity),                   \
    .interfaces = (const InterfaceInfo[]) {               \
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },            \
        { }                                                \
    },                                                     \
}

static const TypeInfo hp_i2000_pid_pci_type =
    HP_I2000_PCI_IDENTITY_TYPE(TYPE_HP_I2000_PID_PCI,
                               hp_i2000_pid_pci_identity);

static hwaddr hp_i2000_sparse_io_port(hwaddr encoded)
{
    return ((encoded >> 12) << 2) | (encoded & 3);
}

static MemTxResult hp_i2000_io_read(AddressSpace *as, hwaddr addr,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        *value = address_space_ldub(as, addr, attrs, &result);
        break;
    case 2:
        *value = address_space_lduw_le(as, addr, attrs, &result);
        break;
    case 4:
        *value = address_space_ldl_le(as, addr, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static MemTxResult hp_i2000_io_write(AddressSpace *as, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        address_space_stb(as, addr, value, attrs, &result);
        break;
    case 2:
        address_space_stw_le(as, addr, value, attrs, &result);
        break;
    case 4:
        address_space_stl_le(as, addr, value, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static AddressSpace *hp_i2000_io_space_for_port(HPI2000MachineState *s,
                                                 hwaddr port,
                                                 unsigned size)
{
    unsigned root;

    for (root = 0; root < HP_I2000_PCI_ROOT_COUNT; root++) {
        const HPI2000RootLayout *layout = &hp_i2000_roots[root];
        uint64_t end = (uint64_t)layout->io_base + layout->io_size;

        if (port >= layout->io_base && port < end && size <= end - port) {
            return &s->root_io[root];
        }
    }
    return NULL;
}

static bool hp_i2000_is_vga_legacy_io(hwaddr port, unsigned size)
{
    return (port >= 0x3b0 && port < 0x3e0 && size <= 0x3e0 - port) ||
           (port >= 0x1ce && port < 0x1d2 && size <= 0x1d2 - port);
}

static MemTxResult hp_i2000_sparse_io_read(void *opaque, hwaddr addr,
                                            uint64_t *value, unsigned size,
                                            MemTxAttrs attrs)
{
    HPI2000MachineState *s = opaque;
    hwaddr port = hp_i2000_sparse_io_port(addr);
    AddressSpace *as;

    if (port == 0xcf8 && size == 4) {
        return hp_i2000_io_read(&s->host_conf_as, 0, value, size, attrs);
    }
    if (port >= 0xcf8 && port < 0xcfc && size <= 0xcfc - port) {
        return hp_i2000_io_read(&s->root_io[0], port, value, size, attrs);
    }
    if (port >= 0xcfc && port < 0xd00 && size <= 0xd00 - port) {
        return hp_i2000_io_read(&s->host_data_as, port - 0xcfc,
                                value, size, attrs);
    }
    if (hp_i2000_is_vga_legacy_io(port, size)) {
        return hp_i2000_io_read(&s->root_io[3], port, value, size, attrs);
    }

    as = hp_i2000_io_space_for_port(s, port, size);
    if (!as) {
        *value = UINT64_MAX;
        return MEMTX_DECODE_ERROR;
    }
    return hp_i2000_io_read(as, port, value, size, attrs);
}

static MemTxResult hp_i2000_sparse_io_write(void *opaque, hwaddr addr,
                                             uint64_t value, unsigned size,
                                             MemTxAttrs attrs)
{
    HPI2000MachineState *s = opaque;
    hwaddr port = hp_i2000_sparse_io_port(addr);
    AddressSpace *as;

    if (port == 0xcf8 && size == 4) {
        return hp_i2000_io_write(&s->host_conf_as, 0, value, size, attrs);
    }
    if (port >= 0xcf8 && port < 0xcfc && size <= 0xcfc - port) {
        return hp_i2000_io_write(&s->root_io[0], port, value, size, attrs);
    }
    if (port >= 0xcfc && port < 0xd00 && size <= 0xd00 - port) {
        return hp_i2000_io_write(&s->host_data_as, port - 0xcfc,
                                 value, size, attrs);
    }
    if (hp_i2000_is_vga_legacy_io(port, size)) {
        return hp_i2000_io_write(&s->root_io[3], port, value, size, attrs);
    }

    as = hp_i2000_io_space_for_port(s, port, size);
    return as ? hp_i2000_io_write(as, port, value, size, attrs) :
                MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps hp_i2000_sparse_io_ops = {
    .read_with_attrs = hp_i2000_sparse_io_read,
    .write_with_attrs = hp_i2000_sparse_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static uint64_t hp_i2000_subword_cf8_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    HPI2000CF8SubwordState *s = opaque;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)s->bytes[addr + i] << (i * 8);
    }
    return value;
}

static void hp_i2000_subword_cf8_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    HPI2000CF8SubwordState *s = opaque;
    unsigned i;

    for (i = 0; i < size; i++) {
        s->bytes[addr + i] = value >> (i * 8);
    }
}

static bool hp_i2000_subword_cf8_accepts(void *opaque, hwaddr addr,
                                        unsigned size, bool is_write,
                                        MemTxAttrs attrs)
{
    (void)opaque;
    (void)is_write;
    (void)attrs;
    return (size == 1 || size == 2) && addr < 4 && size <= 4 - addr;
}

static const MemoryRegionOps hp_i2000_subword_cf8_ops = {
    .read = hp_i2000_subword_cf8_read,
    .write = hp_i2000_subword_cf8_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = true,
        .accepts = hp_i2000_subword_cf8_accepts,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = true,
    },
};

static void hp_i2000_cf8_subword_reset(DeviceState *dev)
{
    HPI2000CF8SubwordState *s = HP_I2000_CF8_SUBWORD(dev);

    memset(s->bytes, 0, sizeof(s->bytes));
}

static const VMStateDescription vmstate_hp_i2000_cf8_subword = {
    .name = TYPE_HP_I2000_CF8_SUBWORD,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(bytes, HPI2000CF8SubwordState, 4),
        VMSTATE_END_OF_LIST()
    },
};

static void hp_i2000_cf8_subword_init(Object *obj)
{
    HPI2000CF8SubwordState *s = HP_I2000_CF8_SUBWORD(obj);

    memory_region_init_io(&s->io, obj, &hp_i2000_subword_cf8_ops, s,
                          TYPE_HP_I2000_CF8_SUBWORD, 4);
}

static void hp_i2000_cf8_subword_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->vmsd = &vmstate_hp_i2000_cf8_subword;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    device_class_set_legacy_reset(dc, hp_i2000_cf8_subword_reset);
}

static const TypeInfo hp_i2000_cf8_subword_type = {
    .name = TYPE_HP_I2000_CF8_SUBWORD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HPI2000CF8SubwordState),
    .instance_init = hp_i2000_cf8_subword_init,
    .class_init = hp_i2000_cf8_subword_class_init,
};

static bool hp_i2000_create_nvram(HPI2000MachineState *s, Error **errp)
{
    g_autofree char *nvram_path = ia64_machine_resolve_nvram_path(
        MACHINE(s), s->nvram_path);
    DeviceState *dev = hp_i2000_add_child(
        s, "efi-nvram", TYPE_HP_I2000_NVRAM);

    s->nvram = HP_I2000_NVRAM(dev);
    qdev_prop_set_string(dev, "nvram", nvram_path ?: "none");
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                    IA64_I2000_PROFILE_NVRAM_BASE);
    return true;
}

static bool hp_i2000_create_chipset(HPI2000MachineState *s, Error **errp)
{
    DeviceState *dev;
    uint8_t expander_mask = HP_I2000_EXPANDER_MASK;

    dev = hp_i2000_add_child(s, "host", TYPE_INTEL_460GX_HOST);
    s->host = INTEL_460GX_HOST(dev);
    qdev_prop_set_uint16(dev, "x-initial-cbn", HP_I2000_CBN);
    qdev_prop_set_uint32(dev, "x-initial-chipset-present",
                        intel_460gx_chipset_present_mask(expander_mask));
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }

    address_space_init(&s->host_conf_as,
                       intel_460gx_host_conf_region(s->host),
                       "hp-i2000.host-conf");
    address_space_init(&s->host_data_as,
                       intel_460gx_host_data_region(s->host),
                       "hp-i2000.host-data");
    s->host_spaces_initialized = true;

    dev = hp_i2000_add_child(s, "cf8-subword", TYPE_HP_I2000_CF8_SUBWORD);
    s->cf8_subword = HP_I2000_CF8_SUBWORD(dev);
    if (!qdev_realize(dev, NULL, errp)) {
        return false;
    }

    dev = hp_i2000_add_child(s, "chipset", TYPE_INTEL_460GX_CHIPSET);
    s->chipset = INTEL_460GX_CHIPSET(dev);
    intel_460gx_chipset_set_fault_notify(
        s->chipset, ia64_ras_hub_report_chipset_fault,
        HP_IA64_MACHINE(s)->ras);
    qdev_prop_set_uint8(dev, INTEL_460GX_CHIPSET_PROP_EXPANDER_MASK,
                       expander_mask);
    if (!object_property_set_link(OBJECT(dev),
                                  INTEL_460GX_CHIPSET_PROP_HOST,
                                  OBJECT(s->host), errp) ||
        !qdev_realize(dev, NULL, errp)) {
        return false;
    }

    dev = hp_i2000_add_child(s, "pid", TYPE_INTEL_460GX_PID);
    s->pid = INTEL_460GX_PID(dev);
    qdev_prop_set_uint8(dev, INTEL_460GX_PID_PROP_INITIAL_ID,
                       HP_I2000_PID_ID);
    qdev_prop_set_uint32(dev, INTEL_460GX_PID_PROP_LEGACY_PIN,
                        HP_I2000_PID_LEGACY_PIN);
    return sysbus_realize(SYS_BUS_DEVICE(dev), errp);
}

static bool hp_i2000_create_root(HPI2000MachineState *s, unsigned root,
                                  Error **errp)
{
    MachineState *machine = MACHINE(s);
    const HPI2000RootLayout *layout = &hp_i2000_roots[root];
    g_autofree char *child_name = g_strdup_printf("pci-root%u", root);
    g_autofree char *io_name = g_strdup_printf("hp-i2000.root%u-io", root);
    g_autofree char *mmio_name =
        g_strdup_printf("hp-i2000.root%u-mmio", root);
    uint64_t low_size = MIN(machine->ram_size, HP_I2000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;
    DeviceState *dev;
    PCIBus *bus;
    unsigned pin;

    dev = hp_i2000_add_child(s, child_name, TYPE_INTEL_460GX_ROOT_HOST);
    s->roots[root] = INTEL_460GX_ROOT_HOST(dev);
    qdev_prop_set_uint16(dev, INTEL_460GX_ROOT_PROP_FIRST_BUS,
                        layout->first_bus);
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    bus = intel_460gx_root_host_bus(s->roots[root]);
    if (!intel_460gx_host_attach_downstream_bus(
            s->host, layout->host_port, bus,
            layout->first_bus, layout->last_bus, errp)) {
        return false;
    }
    intel_460gx_chipset_set_downstream_reset_range(
        s->chipset, layout->host_port,
        layout->first_bus, layout->last_bus);

    address_space_init(&s->root_io[root],
                       intel_460gx_root_host_io(s->roots[root]), io_name);
    s->root_io_initialized[root] = true;
    memory_region_init_alias(&s->root_mmio[root], OBJECT(s), mmio_name,
                             intel_460gx_root_host_mem(s->roots[root]),
                             layout->mmio_base, layout->mmio_size);

    s->dma[root] = intel_460gx_dma_new(
        0, INTEL_460GX_DMA_ADDRESS_LIMIT, errp);
    s->dma_fault_route[root] = (HPI2000ChipsetFaultRoute) {
        .chipset = s->chipset,
        .port = layout->host_port,
    };
    if (s->dma[root]) {
        intel_460gx_dma_set_fault_notify(
            s->dma[root], hp_i2000_chipset_fault,
            &s->dma_fault_route[root]);
    }
    if (!s->dma[root] ||
        !intel_460gx_dma_add_ram_alias(s->dma[root], 0, low_size,
                                       machine->ram, 0, errp) ||
        (high_size &&
         !intel_460gx_dma_add_ram_alias(
             s->dma[root], HP_I2000_HIGH_RAM_BASE, high_size,
             machine->ram, low_size, errp)) ||
        !intel_460gx_dma_add_pci_window_alias(
            s->dma[root], layout->mmio_base, layout->mmio_size,
            intel_460gx_root_host_mem(s->roots[root]),
            layout->mmio_base, errp) ||
        !intel_460gx_dma_seal(s->dma[root], errp) ||
        !intel_460gx_dma_attach_root(s->dma[root], bus, errp)) {
        return false;
    }

    for (pin = 0; pin < HP_I2000_PCI_INTX_COUNT; pin++) {
        qdev_connect_gpio_out_named(
            dev, INTEL_460GX_ROOT_GPIO_INTX, pin,
            qdev_get_gpio_in_named(DEVICE(s->pid),
                                   INTEL_460GX_PID_GPIO_IRQ,
                                   layout->intx_base + pin));
    }
    return true;
}

static void hp_i2000_map_chipset(HPI2000MachineState *s)
{
    unsigned root;

    memory_region_add_subregion(
        intel_460gx_root_host_io(s->roots[0]), 0xcf8,
        &s->cf8_subword->io);

    memory_region_transaction_begin();
    memory_region_add_subregion(
        get_system_memory(), HP_I2000_PID_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(s->pid), 0));
    for (root = 0; root < HP_I2000_PCI_ROOT_COUNT; root++) {
        memory_region_add_subregion(get_system_memory(),
                                    hp_i2000_roots[root].mmio_base,
                                    &s->root_mmio[root]);
    }
    memory_region_add_subregion(get_system_memory(), HP_I2000_LEGACY_IO_BASE,
                                &s->sparse_io);
    memory_region_transaction_commit();
}

static bool hp_i2000_create_isa_devices(HPI2000MachineState *s,
                                         Error **errp)
{
    ISABus *isa = intel_82468gx_ifb_isa_bus(s->ifb);
    ISADevice *dev;
    Chardev *chardev;

    if (!isa) {
        error_setg(errp, "%s did not create an ISA bus",
                   TYPE_INTEL_82468GX_IFB);
        return false;
    }
    if (!i8254_pit_init(isa, HP_I2000_PIT_PORT, HP_I2000_PIT_IRQ, NULL)) {
        error_setg(errp, "failed to create the HP i2000 PIT");
        return false;
    }

    dev = isa_new(TYPE_LPC47B27_ISA);
    qdev_prop_set_uint16(DEVICE(dev), LPC47B27_ISA_PROP_CONFIG_IOBASE,
                        HP_I2000_SUPERIO_PORT);
    qdev_prop_set_uint32(DEVICE(dev),
                        LPC47B27_ISA_PROP_UART_INPUT_CLOCK_HZ,
                        HP_I2000_UART_CLOCK_HZ);
    chardev = serial_hd(0);
    if (chardev) {
        qdev_prop_set_chr(DEVICE(dev), "chardev", chardev);
    }
    if (!isa_realize_and_unref(dev, isa, errp)) {
        return false;
    }

    dev = isa_new(TYPE_MC146818_RTC);
    qdev_prop_set_int32(DEVICE(dev), "base_year", 2000);
    qdev_prop_set_uint16(DEVICE(dev), "iobase", HP_I2000_RTC_PORT);
    qdev_prop_set_uint8(DEVICE(dev), "irq", HP_I2000_RTC_IRQ);
    if (!isa_realize_and_unref(dev, isa, errp)) {
        return false;
    }
    isa_connect_gpio_out(dev, 0, HP_I2000_RTC_IRQ);
    return true;
}

static bool hp_i2000_create_pci_devices(HPI2000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    PCIBus *root0 = intel_460gx_root_host_bus(s->roots[0]);
    PCIBus *root1 = intel_460gx_root_host_bus(s->roots[1]);
    PCIBus *root2 = intel_460gx_root_host_bus(s->roots[2]);
    PCIBus *root3 = intel_460gx_root_host_bus(s->roots[3]);
    PCIBus *ihpc_root[] = { root1, root2 };
    DriveInfo *ide_drive;
    BusState *scsi_bus;
    unsigned int channel, unit, irq, function;

    s->pci_pid = pci_new(PCI_DEVFN(0, 0), TYPE_HP_I2000_PID_PCI);
    if (!pci_realize_and_unref(s->pci_pid, root0, errp)) {
        return false;
    }

    s->vga = pci_vga_new();
    if (s->vga) {
        bool is_ati = object_dynamic_cast(OBJECT(s->vga), TYPE_ATI_VGA);
        bool is_quadro2 = object_dynamic_cast(OBJECT(s->vga),
                                              TYPE_NVIDIA_QUADRO2);

        if (!is_ati && !is_quadro2) {
            error_setg(errp,
                       "%s supports ATI VGA, NVIDIA Quadro2 Pro, or no VGA",
                       TYPE_HP_I2000_MACHINE);
            object_unref(OBJECT(s->vga));
            s->vga = NULL;
            return false;
        }
        s->vga->devfn = PCI_DEVFN(HP_I2000_VGA_SLOT, 0);
        if (is_ati) {
            qdev_prop_set_string(DEVICE(s->vga), "model", "rage128p");
        }
        qdev_prop_set_uint32(DEVICE(s->vga), "vgamem_mb", 64);
        qdev_prop_set_uint32(DEVICE(s->vga), "xres", 1280);
        qdev_prop_set_uint32(DEVICE(s->vga), "yres", 1024);
        qdev_prop_set_uint32(DEVICE(s->vga), "xmax", 1280);
        qdev_prop_set_uint32(DEVICE(s->vga), "ymax", 1024);
        qdev_prop_set_string(DEVICE(s->vga), "romfile", "");
        if (!pci_realize_and_unref(s->vga, root3, errp)) {
            return false;
        }
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), "hp-i2000.vga-legacy",
            intel_460gx_root_host_mem(s->roots[3]),
            HP_I2000_VGA_LEGACY_BASE, HP_I2000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_I2000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    s->ifb = intel_82468gx_ifb_create(
        root0, PCI_DEVFN(HP_I2000_IFB_SLOT, 0), errp);
    if (!s->ifb) {
        return false;
    }
    for (function = 0; function < INTEL_82468GX_IFB_FUNCTIONS;
         function++) {
        PCIDevice *ifb_function = intel_82468gx_ifb_function(
            s->ifb, function);

        if (!ifb_function) {
            error_setg(errp, "%s did not create function %u",
                       TYPE_INTEL_82468GX_IFB, function);
            return false;
        }
        pci_config_set_revision(ifb_function->config, 0x01);
        pci_set_word(ifb_function->config + PCI_SUBSYSTEM_VENDOR_ID, 0);
        pci_set_word(ifb_function->config + PCI_SUBSYSTEM_ID, 0);
    }
    qdev_connect_gpio_out_named(
        DEVICE(s->ifb), INTEL_82468GX_IFB_GPIO_LEGACY, 0,
        qdev_get_gpio_in_named(DEVICE(s->pid),
                               INTEL_460GX_PID_GPIO_LEGACY, 0));
    for (irq = 1; irq < ISA_NUM_IRQS; irq++) {
        if (irq == IA64_I2000_PROFILE_ACPI_SCI_IRQ) {
            continue;
        }
        qdev_connect_gpio_out_named(
            DEVICE(s->ifb), INTEL_82468GX_IFB_GPIO_ISA_IRQ, irq,
            qdev_get_gpio_in_named(DEVICE(s->pid),
                                   INTEL_460GX_PID_GPIO_IRQ, irq));
    }
    qdev_connect_gpio_out_named(
        DEVICE(s->ifb), INTEL_82468GX_IFB_GPIO_SCI, 0,
        qdev_get_gpio_in_named(
            DEVICE(s->pid), INTEL_460GX_PID_GPIO_IRQ,
            IA64_I2000_PROFILE_ACPI_SCI_IRQ));
    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb && defaults_enabled()) {
        PCIDevice *usb = intel_82468gx_ifb_function(
            s->ifb, HP_I2000_IFB_USB_FUNCTION);
        BusState *usb_bus = QLIST_FIRST(&DEVICE(usb)->child_bus);

        if (!usb_bus) {
            error_setg(errp, "%s did not create its USB bus",
                       TYPE_INTEL_82468GX_IFB_USB);
            return false;
        }
        usb_create_simple(USB_BUS(usb_bus), "usb-kbd");
        usb_create_simple(USB_BUS(usb_bus), "usb-tablet");
    }
    if (!hp_i2000_create_isa_devices(s, errp)) {
        return false;
    }

    s->audio = pci_new(PCI_DEVFN(HP_I2000_CS4281_SLOT, 0), TYPE_CS4281);
    if (!pci_realize_and_unref(s->audio, root0, errp)) {
        return false;
    }

    for (function = 0; function < G_N_ELEMENTS(s->ihpc); function++) {
        g_autofree char *name = g_strdup_printf("ihpc%u", function);
        DeviceState *ihpc = hp_i2000_add_child(
            s, name, TYPE_INTEL_82466GX_IHPC);

        s->ihpc[function] = PCI_DEVICE(ihpc);
        qdev_prop_set_int32(ihpc, "addr", PCI_DEVFN(0x0f, 0));
        s->ihpc_fault_route[function] = (HPI2000ChipsetFaultRoute) {
            .chipset = s->chipset,
            .port = hp_i2000_roots[function + 1].host_port,
        };
        intel_82466gx_ihpc_set_fault_notify(
            INTEL_82466GX_IHPC(ihpc), hp_i2000_chipset_fault,
            &s->ihpc_fault_route[function]);
        if (!qdev_realize(ihpc, BUS(ihpc_root[function]), errp)) {
            object_unparent(OBJECT(ihpc));
            s->ihpc[function] = NULL;
            return false;
        }
    }

    for (channel = 0; channel < HP_I2000_IDE_CHANNELS; channel++) {
        IDEBus *ide_bus = intel_82468gx_ifb_ide_bus(s->ifb, channel);

        if (!ide_bus) {
            error_setg(errp, "%s did not create IDE bus %u",
                       TYPE_INTEL_82468GX_IFB, channel);
            return false;
        }
        for (unit = 0; unit < ide_bus->max_units; unit++) {
            ide_drive = drive_get(IF_IDE, channel, unit);
            if (ide_drive) {
                ide_bus_create_drive(ide_bus, unit, ide_drive);
            }
        }
    }

    s->i82559 = pci_new(PCI_DEVFN(HP_I2000_I82559_SLOT, 0), "i82559c");
    qdev_prop_set_string(DEVICE(s->i82559), "romfile", "");
    qdev_prop_set_uint8(DEVICE(s->i82559), "x-pci-revision", 0x08);
    qdev_prop_set_uint16(DEVICE(s->i82559),
                         "x-pci-subsystem-vendor-id", PCI_VENDOR_ID_INTEL);
    qdev_prop_set_uint16(DEVICE(s->i82559),
                         "x-pci-subsystem-id", 0x3400);
    qdev_prop_set_uint16(DEVICE(s->i82559),
                         "x-eeprom-compatibility", 0x0803);
    qemu_configure_nic_device(DEVICE(s->i82559), true, NULL);
    if (!pci_realize_and_unref(s->i82559, root0, errp)) {
        return false;
    }

    s->isp12160 = pci_new(
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        TYPE_ISP12160_SCSI);
    if (!pci_realize_and_unref(s->isp12160, root1, errp)) {
        return false;
    }
    scsi_bus = qdev_get_child_bus(DEVICE(s->isp12160),
                                  "isp12160-scsi.0");
    if (!scsi_bus) {
        error_setg(errp, "%s did not create its SCSI bus",
                   TYPE_ISP12160_SCSI);
        return false;
    }
    scsi_bus_legacy_handle_cmdline(SCSI_BUS(scsi_bus));
    return true;
}

static void hp_i2000_configure_pci(HPI2000MachineState *s)
{
    PCIDevice *ide = intel_82468gx_ifb_function(
        s->ifb, HP_I2000_IFB_IDE_FUNCTION);
    PCIDevice *usb = intel_82468gx_ifb_function(
        s->ifb, HP_I2000_IFB_USB_FUNCTION);
    static const uint32_t ihpc_bar[] = {
        HP_I2000_IHPC0_MMIO_BAR,
        HP_I2000_IHPC1_MMIO_BAR,
    };
    unsigned int i;

    intel_82468gx_ifb_configure_acpi(
        s->ifb, IA64_I2000_PROFILE_ACPI_PM_IO_BASE);
    if (ide) {
        pci_default_write_config(
            ide, PCI_BASE_ADDRESS_4,
            IA64_I2000_PROFILE_IDE_BMDMA_PORT |
            PCI_BASE_ADDRESS_SPACE_IO, 4);
        pci_default_write_config(
            ide, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(ide, INTEL_82468GX_IFB_IDETIM_PRIMARY,
                                 INTEL_82468GX_IFB_IDETIM_DECODE, 2);
    }
    if (usb) {
        pci_default_write_config(
            usb, PCI_BASE_ADDRESS_4,
            HP_I2000_IFB_USB_IO_BAR | PCI_BASE_ADDRESS_SPACE_IO, 4);
        pci_default_write_config(
            usb, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(
            usb, PCI_INTERRUPT_LINE, HP_I2000_IFB_USB_GSI, 1);
    }
    if (s->audio) {
        pci_default_write_config(s->audio, PCI_BASE_ADDRESS_0,
                                 HP_I2000_CS4281_BA0, 4);
        pci_default_write_config(s->audio, PCI_BASE_ADDRESS_1,
                                 HP_I2000_CS4281_BA1, 4);
        pci_default_write_config(
            s->audio, PCI_COMMAND,
            PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(s->audio, PCI_INTERRUPT_LINE,
                                 HP_I2000_CS4281_GSI, 1);
    }
    for (i = 0; i < G_N_ELEMENTS(s->ihpc); i++) {
        if (!s->ihpc[i]) {
            continue;
        }
        pci_default_write_config(s->ihpc[i], PCI_BASE_ADDRESS_0,
                                 ihpc_bar[i], 4);
        pci_default_write_config(s->ihpc[i], PCI_COMMAND,
                                 PCI_COMMAND_MEMORY, 2);
        pci_default_write_config(s->ihpc[i], PCI_INTERRUPT_LINE,
                                 hp_i2000_roots[i + 1].intx_base, 1);
    }
    if (s->i82559) {
        pci_default_write_config(s->i82559, PCI_BASE_ADDRESS_0,
                                 HP_I2000_I82559_MMIO_BAR, 4);
        pci_default_write_config(
            s->i82559, PCI_BASE_ADDRESS_1,
            HP_I2000_I82559_IO_BAR | PCI_BASE_ADDRESS_SPACE_IO, 4);
        pci_default_write_config(s->i82559, PCI_BASE_ADDRESS_2,
                                 HP_I2000_I82559_FLASH_BAR, 4);
        pci_default_write_config(
            s->i82559, PCI_COMMAND,
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(s->i82559, PCI_INTERRUPT_LINE,
                                 HP_I2000_I82559_GSI, 1);
    }
    if (s->vga && object_dynamic_cast(OBJECT(s->vga), TYPE_ATI_VGA)) {
        pci_default_write_config(s->vga, PCI_BASE_ADDRESS_0,
                                 HP_I2000_RAGE128_FB_BAR, 4);
        pci_default_write_config(s->vga, PCI_BASE_ADDRESS_1,
                                 HP_I2000_RAGE128_IO_BAR, 4);
        pci_default_write_config(s->vga, PCI_BASE_ADDRESS_2,
                                 HP_I2000_RAGE128_MMIO_BAR, 4);
        pci_default_write_config(
            s->vga, PCI_COMMAND,
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(s->vga, PCI_INTERRUPT_LINE,
                                 HP_I2000_VGA_GSI, 1);
    } else if (s->vga) {
        pci_default_write_config(s->vga, PCI_BASE_ADDRESS_0,
                                 HP_I2000_QUADRO2_MMIO_BAR, 4);
        pci_default_write_config(s->vga, PCI_BASE_ADDRESS_1,
                                 HP_I2000_QUADRO2_FB_BAR, 4);
        pci_default_write_config(
            s->vga, PCI_COMMAND,
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 2);
        pci_default_write_config(s->vga, PCI_INTERRUPT_LINE,
                                 HP_I2000_VGA_GSI, 1);
    }
    if (!s->isp12160) {
        return;
    }
    pci_default_write_config(s->isp12160, PCI_BASE_ADDRESS_0,
                             HP_I2000_ISP12160_IO_BAR |
                             PCI_BASE_ADDRESS_SPACE_IO, 4);
    pci_default_write_config(s->isp12160, PCI_BASE_ADDRESS_1,
                             ISP12160_QEMU_I2000_BAR_ADDRESS, 4);
    pci_default_write_config(s->isp12160, PCI_COMMAND, 0, 2);
    pci_default_write_config(s->isp12160, PCI_INTERRUPT_LINE,
                             ISP12160_QEMU_I2000_GSI, 1);
}

static bool hp_i2000_init_int10(HPI2000MachineState *s, Error **errp)
{
    HPIA64Int10Config config;
    bool is_quadro2;

    if (!s->vga) {
        return true;
    }
    is_quadro2 = object_dynamic_cast(OBJECT(s->vga), TYPE_NVIDIA_QUADRO2);

    config = (HPIA64Int10Config) {
        .owner = OBJECT(s),
        .vga = s->vga,
        .service_io = intel_460gx_root_host_io(s->roots[0]),
        .vga_io = &s->root_io[3],
        .framebuffer_base = is_quadro2 ? HP_I2000_QUADRO2_FB_BAR :
                                        HP_I2000_RAGE128_FB_BAR,
        .framebuffer_bar = is_quadro2 ? 1 : 0,
        .mmio_bar = is_quadro2 ? 0 : 2,
        .region_name = "hp-i2000.int10-pci-io",
    };
    return hp_ia64_int10_init(&s->int10, &config, errp);
}

static uint64_t hp_i2000_sparse_io_address(uint16_t port)
{
    return HP_I2000_LEGACY_IO_BASE +
        ((uint64_t)(port >> 2) << 12) + (port & 0xfffU);
}

static void hp_i2000_map_ram(HPI2000MachineState *s)
{
    MachineState *machine = MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_I2000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;

    memory_region_init_alias(&s->low_ram, OBJECT(s), "hp-i2000.low-ram",
                             machine->ram, 0, low_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->low_ram);
    if (high_size) {
        memory_region_init_alias(&s->high_ram, OBJECT(s),
                                 "hp-i2000.high-ram", machine->ram,
                                 low_size, high_size);
        memory_region_add_subregion(get_system_memory(),
                                    HP_I2000_HIGH_RAM_BASE, &s->high_ram);
    }
}

static void hp_i2000_add_onboard_device(
    IA64PlatformDescriptor *descriptor, PCIDevice *device, uint8_t type,
    unsigned int root, uint8_t bar, uint64_t bar_size)
{
    IA64PlatformOnboardDevice *entry;
    uint32_t count;

    if (!device) {
        return;
    }
    count = le32_to_cpu(descriptor->OnboardDeviceCount);
    g_assert(count < IA64_PLATFORM_MAX_ONBOARD_DEVICES);
    entry = &descriptor->OnboardDevice[count];
    entry->Segment = cpu_to_le16(0);
    entry->Bus = hp_i2000_roots[root].first_bus;
    entry->Device = PCI_SLOT(device->devfn);
    entry->Function = PCI_FUNC(device->devfn);
    entry->Type = type;
    entry->Bar = bar;
    entry->VendorDeviceId = cpu_to_le32(
        pci_get_long(device->config + PCI_VENDOR_ID));
    entry->ClassCode = cpu_to_le32(
        pci_get_long(device->config + PCI_CLASS_REVISION) >> 8);
    entry->BarSize = cpu_to_le64(bar_size);
    descriptor->OnboardDeviceCount = cpu_to_le32(count + 1U);
}

static bool hp_i2000_install_descriptor(HPI2000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_I2000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;
    IA64PlatformDescriptor header = {
        .Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC),
        .FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION),
        .PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_I2000),
        .Flags = cpu_to_le32(IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS |
                             IA64_PLATFORM_FLAG_IDE_DMA),
        .RamSize = cpu_to_le64(machine->ram_size),
        .LowRamEnd = cpu_to_le64(low_size),
        .FirmwareBase = cpu_to_le64(HP_I2000_FIRMWARE_BASE),
        .FirmwareSize = cpu_to_le64(HP_I2000_FIRMWARE_SIZE),
        .ProcessorCount = cpu_to_le32(machine->smp.cpus),
        .SocketCount = cpu_to_le32(machine->smp.sockets),
        .CoresPerSocket = cpu_to_le32(machine->smp.cores),
        .ThreadsPerCore = cpu_to_le32(machine->smp.threads),
        .PhysicalAddressBits = cpu_to_le32(
            IA64_PLATFORM_I2000_PHYS_ADDR_BITS),
        .MaxSockets = cpu_to_le32(2),
        .MaxCoresPerSocket = cpu_to_le32(1),
        .MaxThreadsPerCore = cpu_to_le32(1),
        .MaxPciRoots = cpu_to_le32(HP_I2000_PCI_ROOT_COUNT),
        .PciRootIdentity = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_IDENTITY_GENERIC),
        .NumaNodeCount = cpu_to_le32(1),
        .LegacyIoBase = cpu_to_le64(HP_I2000_LEGACY_IO_BASE),
        .LegacyIoSize = cpu_to_le64(HP_I2000_LEGACY_IO_SIZE),
        .LocalSapicBase = cpu_to_le64(HP_I2000_PIB_BASE),
        .LocalSapicSize = cpu_to_le64(HP_I2000_PIB_SIZE),
        .ConsoleBase = cpu_to_le64(
            hp_i2000_sparse_io_address(HP_I2000_UART_PORT)),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(HP_I2000_UART_CLOCK_HZ),
        .NvramBase = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_BASE),
        .NvramSize = cpu_to_le64(IA64_I2000_PROFILE_NVRAM_SIZE),
        .RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE),
        .RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE),
    };
    IA64PlatformRamRange ram[] = {
        {
            .Base = cpu_to_le64(0),
            .Size = cpu_to_le64(low_size),
        }, {
            .Base = cpu_to_le64(HP_I2000_HIGH_RAM_BASE),
            .Size = cpu_to_le64(high_size),
        },
    };
    IA64PlatformPciRoot roots[HP_I2000_PCI_ROOT_COUNT] = { 0 };
    IA64PlatformIoSapic io_sapic = {
        .Base = cpu_to_le64(HP_I2000_PID_BASE),
        .GsiBase = cpu_to_le32(0),
        .RedirectionEntries = cpu_to_le32(INTEL_460GX_PID_NUM_PINS),
        .Version = cpu_to_le32(HP_I2000_PID_VERSION),
        .Id = HP_I2000_PID_ID,
    };
    IA64PlatformPciRoute routes[] = {
        {
            .Segment = cpu_to_le16(0),
            .Bus = hp_i2000_roots[3].first_bus,
            .Device = HP_I2000_VGA_SLOT,
            .Pin = 0,
            .Gsi = cpu_to_le32(HP_I2000_VGA_GSI),
        },
        {
            .Segment = cpu_to_le16(0),
            .Bus = hp_i2000_roots[0].first_bus,
            .Device = HP_I2000_I82559_SLOT,
            .Pin = 0,
            .Gsi = cpu_to_le32(HP_I2000_I82559_GSI),
        }, {
            .Segment = cpu_to_le16(0),
            .Bus = hp_i2000_roots[0].first_bus,
            .Device = HP_I2000_CS4281_SLOT,
            .Pin = 0,
            .Gsi = cpu_to_le32(HP_I2000_CS4281_GSI),
        }, {
            .Segment = cpu_to_le16(0),
            .Bus = hp_i2000_roots[0].first_bus,
            .Device = HP_I2000_IFB_SLOT,
            .Pin = HP_I2000_IFB_USB_PIN,
            .Gsi = cpu_to_le32(HP_I2000_IFB_USB_GSI),
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
        .ram_ranges = ram,
        .ram_range_count = high_size ? 2 : 1,
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = &io_sapic,
        .io_sapic_count = 1,
        .pci_routes = routes,
        .pci_route_count = G_N_ELEMENTS(routes),
        .profiles = &profile,
        .profile_count = 1,
    };
    unsigned root;

    header.ConsoleFlags = cpu_to_le32(
        s->vga ? IA64_PLATFORM_CONSOLE_FLAG_VGA_PRIMARY : 0);
    header.NumaNode[0].ProcessorCount = cpu_to_le32(machine->smp.cpus);
    header.NumaNode[0].RamRangeMask = cpu_to_le32(high_size ? 3U : 1U);
    header.NumaNode[0].Distance[0] = 10;
    if (s->vga && object_dynamic_cast(OBJECT(s->vga),
                                      TYPE_NVIDIA_QUADRO2)) {
        hp_i2000_add_onboard_device(
            &header, s->vga, IA64_PLATFORM_ONBOARD_GRAPHICS, 3, 1,
            NVIDIA_QUADRO2_FB_APERTURE_SIZE);
    } else {
        hp_i2000_add_onboard_device(
            &header, s->vga, IA64_PLATFORM_ONBOARD_GRAPHICS, 3, 0,
            64 * MiB);
    }
    hp_i2000_add_onboard_device(
        &header, intel_82468gx_ifb_function(
            s->ifb, HP_I2000_IFB_USB_FUNCTION),
        IA64_PLATFORM_ONBOARD_UHCI, 0, 4, 0x20);
    hp_i2000_add_onboard_device(
        &header, intel_82468gx_ifb_function(
            s->ifb, HP_I2000_IFB_IDE_FUNCTION),
        IA64_PLATFORM_ONBOARD_IDE, 0, 4, 0x10);
    hp_i2000_add_onboard_device(
        &header, s->isp12160, IA64_PLATFORM_ONBOARD_SCSI, 1,
        UINT8_MAX, 0);
    hp_i2000_add_onboard_device(
        &header, s->i82559, IA64_PLATFORM_ONBOARD_NETWORK, 0,
        UINT8_MAX, 0);

    if (hp_i2000_nvram_persistent(s->nvram)) {
        header.Flags = cpu_to_le32(le32_to_cpu(header.Flags) |
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT);
    }

    for (root = 0; root < HP_I2000_PCI_ROOT_COUNT; root++) {
        const HPI2000RootLayout *layout = &hp_i2000_roots[root];

        roots[root].Segment = cpu_to_le16(0);
        roots[root].Bus = layout->first_bus;
        roots[root].ConfigType = IA64_PLATFORM_PCI_CONFIG_CF8_CFC;
        roots[root].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
            (root == 3 ?
             IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY : 0));
        roots[root].IoBase = cpu_to_le64(layout->io_base);
        roots[root].IoSize = cpu_to_le64(layout->io_size);
        roots[root].Mmio32Base = cpu_to_le64(layout->mmio_base);
        roots[root].Mmio32Size = cpu_to_le64(layout->mmio_size);
        roots[root].DmaBase = cpu_to_le64(0);
        roots[root].DmaSize = cpu_to_le64(low_size);
        roots[root].BusEnd = layout->last_bus;
    }

    ia64_platform_i2000_profile_init(&profile);
    if (!hp_ia64_machine_install_platform_descriptor(
            hp, &header, &arrays, errp)) {
        return false;
    }
    if (hp->firmware_args.descriptor_size !=
        HP_I2000_LOW_RAM_DESCRIPTOR_SIZE +
        (high_size ? sizeof(ram[1]) : 0)) {
        error_setg(errp, "HP i2000 platform descriptor has size %" PRIu64,
                   hp->firmware_args.descriptor_size);
        return false;
    }
    return true;
}

static IA64BootInfo hp_i2000_boot_info(unsigned cpu_index, uint64_t entry,
                                        uint64_t global_pointer,
                                        void *opaque)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(opaque);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_ram_end = hp->descriptor_low_ram_end;
    uint64_t assist_base = low_ram_end - IA64_FW_BOOT_STACK_SIZE;
    bool applied;
    IA64BootInfo info = {
        .firmware_base = HP_I2000_FIRMWARE_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = HP_I2000_IVT_BASE,
        .bsp = assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        .stack_pointer = cpu_index == 0 ? low_ram_end - 16 :
            assist_base + IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_end,
        .io_port_base = HP_I2000_LEGACY_IO_BASE,
        .interrupt_block_base = HP_I2000_PIB_BASE,
        .powered_off = cpu_index != 0,
        .platform_addresses_valid = true,
    };

    applied = hp_ia64_machine_apply_platform_firmware_args(hp, &info);
    g_assert(applied);
    return info;
}

static IA64BootInfo hp_i2000_initial_boot_info(unsigned cpu_index,
                                                void *opaque)
{
    return hp_i2000_boot_info(cpu_index, HP_I2000_FIRMWARE_BASE,
                              HP_I2000_FIRMWARE_BASE, opaque);
}

static IA64BootInfo hp_i2000_firmware_boot_info(
    unsigned cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    return hp_i2000_boot_info(cpu_index, entry, global_pointer, opaque);
}

static int hp_i2000_pib_inta(void *opaque)
{
    HPI2000MachineState *s = opaque;

    return intel_82468gx_ifb_pic_read_irq(s->ifb);
}

static bool hp_i2000_build(MachineState *machine, Error **errp)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(machine);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    IA64MachineCpuConfig cpu_config = {
        .alat_full = hp->alat_full,
        .boot_info = hp_i2000_initial_boot_info,
        .boot_info_opaque = s,
    };
    unsigned root;

    if (!hp_ia64_machine_validate(hp, errp)) {
        return false;
    }
    if (g_strcmp0(machine->cpu_type, IA64_CPU_TYPE_NAME("merced")) != 0) {
        error_setg(errp, "%s requires the Merced CPU model",
                   TYPE_HP_I2000_MACHINE);
        return false;
    }
    if (!machine->ram || memory_region_size(machine->ram) !=
        machine->ram_size) {
        error_setg(errp, "%s requires machine RAM",
                   TYPE_HP_I2000_MACHINE);
        return false;
    }

    hp_i2000_map_ram(s);
    if (!hp_ia64_machine_create_ras(
            hp, IA64_RAS_HUB_DEFAULT_BASE, errp)) {
        return false;
    }
    ia64_machine_map_pib_with_inta(
        OBJECT(s), &hp->pib, "hp-i2000.pib",
        HP_I2000_PIB_BASE, HP_I2000_PIB_SIZE, hp_i2000_pib_inta, s);

    if (!hp_i2000_create_nvram(s, errp) ||
        !hp_i2000_create_chipset(s, errp)) {
        return false;
    }
    for (root = 0; root < HP_I2000_PCI_ROOT_COUNT; root++) {
        if (!hp_i2000_create_root(s, root, errp)) {
            return false;
        }
    }
    hp_i2000_map_chipset(s);
    if (!hp_i2000_create_pci_devices(s, errp)) {
        return false;
    }
    hp_i2000_configure_pci(s);
    if (!hp_i2000_init_int10(s, errp)) {
        return false;
    }

    if (!hp_i2000_install_descriptor(s, errp)) {
        return false;
    }
    cpu_config.firmware_compat_flags =
        hp->firmware_args.firmware_compat_flags;
    if (!ia64_machine_create_cpus(machine, &cpu_config, errp) ||
        !ia64_machine_load_firmware(machine, HP_I2000_FIRMWARE_BASE,
                                    HP_I2000_FIRMWARE_SIZE,
                                    &hp->firmware_size, errp)) {
        return false;
    }
    ia64_machine_init_firmware_notifier(
        &hp->firmware_notifier, machine, HP_I2000_FIRMWARE_BASE,
        hp->firmware_size, hp_i2000_firmware_boot_info, NULL, s);
    return true;
}

static void hp_i2000_machine_init(MachineState *machine)
{
    Error *err = NULL;

    if (!hp_i2000_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void hp_i2000_machine_reset(MachineState *machine, ResetType type)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(machine);

    qemu_devices_reset(type);
    resettable_reset(OBJECT(s->chipset), type);
    hp_i2000_cf8_subword_reset(DEVICE(s->cf8_subword));
    hp_i2000_configure_pci(s);
    hp_ia64_int10_reset(&s->int10);
    ia64_machine_reset_cpus();
}

static char *hp_i2000_get_nvram(Object *obj, Error **errp)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(obj);

    (void)errp;
    return g_strdup(s->nvram_path ?: "auto");
}

static void hp_i2000_set_nvram(Object *obj, const char *value, Error **errp)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(obj);

    (void)errp;
    g_free(s->nvram_path);
    s->nvram_path = g_strcmp0(value, "auto") == 0 ?
                    NULL : g_strdup(value);
}

static void hp_i2000_instance_init(Object *obj)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(obj);

    memory_region_init_io(&s->sparse_io, obj, &hp_i2000_sparse_io_ops, s,
                          "hp-i2000.sparse-io", HP_I2000_LEGACY_IO_SIZE);
    s->sparse_io.disable_reentrancy_guard = true;
}

static void hp_i2000_instance_finalize(Object *obj)
{
    HPI2000MachineState *s = HP_I2000_MACHINE(obj);
    Error *local_err = NULL;
    int root;

    hp_ia64_int10_destroy(&s->int10);
    for (root = HP_I2000_PCI_ROOT_COUNT - 1; root >= 0; root--) {
        if (s->dma[root] &&
            !intel_460gx_dma_destroy(s->dma[root], &local_err)) {
            error_report_err(local_err);
            local_err = NULL;
        }
        s->dma[root] = NULL;
        if (s->root_io_initialized[root]) {
            address_space_destroy(&s->root_io[root]);
            s->root_io_initialized[root] = false;
        }
    }
    if (s->host_spaces_initialized) {
        address_space_destroy(&s->host_data_as);
        address_space_destroy(&s->host_conf_as);
        s->host_spaces_initialized = false;
    }
    g_free(s->nvram_path);
}

static GlobalProperty hp_i2000_compat_defaults[] = {
    { "usb-kbd", "msos-desc", "off" },
    { "usb-mouse", "msos-desc", "off" },
    { "usb-tablet", "msos-desc", "off" },
};

static void hp_i2000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "HP i2000 workstation";
    mc->init = hp_i2000_machine_init;
    mc->reset = hp_i2000_machine_reset;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("merced");
    mc->default_ram_size = HP_I2000_MIN_RAM_SIZE;
    mc->default_ram_id = "hp-i2000.ram";
    mc->default_display = "quadro2";
    mc->default_nic = "i82559c";
    mc->default_machine_opts = "firmware=ia64-firmware.bin";
    mc->max_cpus = 2;
    mc->default_cpus = 1;
    mc->smp_props.prefer_sockets = true;
    mc->block_default_type = IF_SCSI;
    mc->block_default_cdrom_type = IF_IDE;
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;

    compat_props_add(mc->compat_props, hp_i2000_compat_defaults,
                     G_N_ELEMENTS(hp_i2000_compat_defaults));

    hmc->platform_id = IA64_PLATFORM_ID_HP_I2000;
    hmc->minimum_ram_size = HP_I2000_MIN_RAM_SIZE;
    hmc->maximum_ram_size = HP_I2000_MAX_RAM_SIZE;
    hmc->descriptor_gpa = HP_I2000_DESCRIPTOR_GPA;

    object_class_property_add_str(oc, "nvram", hp_i2000_get_nvram,
                                  hp_i2000_set_nvram);
    object_class_property_set_description(
        oc, "nvram", "Set the i2000 NVRAM mode: auto, none, or a file path");
}

static const TypeInfo hp_i2000_machine_type = {
    .name = TYPE_HP_I2000_MACHINE,
    .parent = TYPE_HP_IA64_MACHINE,
    .instance_size = sizeof(HPI2000MachineState),
    .instance_init = hp_i2000_instance_init,
    .instance_finalize = hp_i2000_instance_finalize,
    .class_init = hp_i2000_machine_class_init,
};

static void hp_i2000_register_types(void)
{
    type_register_static(&hp_i2000_cf8_subword_type);
    type_register_static(&hp_i2000_nvram_type);
    type_register_static(&hp_i2000_pid_pci_type);
    type_register_static(&hp_i2000_machine_type);
}

type_init(hp_i2000_register_types)
