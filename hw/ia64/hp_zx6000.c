/*
 * HP zx6000 workstation
 *
 * Technical references are listed in
 * docs/devel/device-emulation-provenance.rst.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES

#include "hw/char/serial.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/display/ati_int.h"
#include "hw/ia64/hp_ia64.h"
#include "hw/ia64/hp_int10.h"
#include "hw/ia64/hp_zx6000.h"
#include "hw/ia64/hp_rx2660.h"
#include "hw/ia64/hp_zx6000_pdh.h"
#include "hw/ide/cmd649.h"
#include "hw/ide/ide-bus.h"
#include "hw/ide/pci.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa.h"
#include "hw/pci-host/hp-zx1-mio.h"
#include "hw/scsi/mptsas.h"
#include "hw/scsi/scsi.h"
#include "hw/usb/nec-usb.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/blockdev.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ia64/cpu-qom.h"

#define HP_ZX6000_LOW_RAM_LIMIT       UINT64_C(0x0000000040000000)
#define HP_ZX6000_HIGH_RAM_BASE       UINT64_C(0x0000000100000000)
#define HP_ZX6000_MIN_RAM_SIZE        (512 * MiB)
#define HP_ZX6000_MAX_RAM_SIZE        (24 * GiB)

#define HP_RX2660_MIN_RAM_SIZE        (1 * GiB)
#define HP_RX2660_MAX_RAM_SIZE        (32 * GiB)
#define HP_RX2660_PCI_ROOT_COUNT      5U

#define HP_ZX6000_DESCRIPTOR_GPA      UINT64_C(0x0000000000300000)
#define HP_ZX6000_IVT_BASE            UINT64_C(0x0000000000010000)
#define HP_ZX6000_PIB_BASE            UINT64_C(0x00000000fee00000)
#define HP_ZX6000_PIB_SIZE            UINT64_C(0x0000000000200000)
#define HP_ZX6000_LEGACY_IO_BASE      UINT64_C(0x00000ffffc000000)
#define HP_ZX6000_LEGACY_IO_SIZE      UINT64_C(0x0000000004000000)
#define HP_ZX6000_VGA_LEGACY_BASE     UINT64_C(0x00000000000a0000)
#define HP_ZX6000_VGA_LEGACY_SIZE     UINT64_C(0x0000000000020000)
#define HP_ZX6000_VGA_LEGACY_IO_BASE  UINT64_C(0x00000000000003b0)
#define HP_ZX6000_VGA_LEGACY_IO_SIZE  UINT64_C(0x0000000000000030)
#define HP_ZX6000_VBE_LEGACY_IO_BASE  UINT64_C(0x00000000000001ce)
#define HP_ZX6000_VBE_LEGACY_IO_SIZE  UINT64_C(0x0000000000000004)

#define HP_ZX6000_PDH_UART0_BASE      UINT64_C(0x00000000fec00000)
#define HP_ZX6000_PDH_UART1_BASE      UINT64_C(0x00000000fec02000)
#define HP_ZX6000_PDH_NVRAM_BASE      UINT64_C(0x00000000feb00000)
#define HP_ZX6000_PDH_RTC_BASE        UINT64_C(0x00000000feb80000)
#define HP_ZX6000_PDH_CONTROL_BASE    UINT64_C(0x00000000feb82000)
/* zx6000/rx2600 fixed ACPI register page in the system firmware aperture. */
#define HP_ZX6000_PDH_ACPI_PM_BASE    UINT64_C(0x00000000ff5c0000)

#define HP_ZX6000_MIO_SIZE            UINT64_C(0x0000000000010000)
#define HP_ZX6000_AGP_MMIO_SIZE       UINT64_C(0x0000000010000000)
#define HP_ZX6000_PCI_MMIO_SIZE       UINT64_C(0x0000000008000000)
#define HP_ZX6000_PCI_IO_SIZE         UINT64_C(0x0000000000002000)
#define HP_ZX6000_PCI_INPUT_COUNT     10U
#define HP_ZX6000_AGP_INPUT_COUNT     7U
#define HP_ZX6000_IO_SAPIC_VERSION    UINT32_C(0x000a0020)

#define HP_ZX6000_CORE_PCI_ROOT       0U
#define HP_ZX6000_SCSI_ROOT           1U
#define HP_ZX6000_AGP_ROOT            4U
#define HP_ZX6000_ACPI_SCI_INPUT      7U

#define HP_ZX6000_RV100_SLOT          0U
#define HP_ZX6000_OHCI_SLOT           1U
#define HP_ZX6000_CMD649_SLOT         2U
#define HP_ZX6000_I82550_SLOT         3U
#define HP_ZX6000_LSI53C1030_SLOT     1U
#define HP_ZX6000_BCM5701_SLOT        2U

G_STATIC_ASSERT(HP_ZX6000_ACPI_SCI_INPUT < HP_ZX6000_PCI_INPUT_COUNT);

#define HP_ZX6000_RV100_FB_BAR        UINT32_C(0xa0000000)
#define HP_ZX6000_RV100_IO_BAR        UINT32_C(0x00008000)
#define HP_ZX6000_RV100_MMIO_BAR      UINT32_C(0xa8000000)
#define HP_ZX6000_RV100_ROM_BAR       UINT32_C(0xa8010000)

#define HP_ZX6000_CMD649_DATA_BAR     UINT32_C(0x00000d58)
#define HP_ZX6000_CMD649_CONTROL_BAR  UINT32_C(0x00000d64)
#define HP_ZX6000_CMD649_SECONDARY_DATA_BAR \
                                            UINT32_C(0x00000d50)
#define HP_ZX6000_CMD649_SECONDARY_CONTROL_BAR \
                                            UINT32_C(0x00000d60)
#define HP_ZX6000_CMD649_BMDMA_BAR    UINT32_C(0x00000d40)

#define HP_ZX6000_I82550_MMIO_BAR     UINT32_C(0x80020000)
#define HP_ZX6000_I82550_IO_BAR       UINT32_C(0x00000d00)
#define HP_ZX6000_I82550_FLASH_BAR    UINT32_C(0x80040000)
#define HP_ZX6000_OHCI0_MMIO_BAR      UINT32_C(0x80023000)
#define HP_ZX6000_OHCI1_MMIO_BAR      UINT32_C(0x80022000)
#define HP_ZX6000_EHCI_MMIO_BAR       UINT32_C(0x80021000)

#define HP_ZX6000_LSI0_IO_BAR         UINT32_C(0x00002000)
#define HP_ZX6000_LSI0_MMIO_BAR       UINT32_C(0x88000000)
#define HP_ZX6000_LSI0_DIAG_BAR       UINT32_C(0x88010000)
#define HP_ZX6000_LSI1_IO_BAR         UINT32_C(0x00002100)
#define HP_ZX6000_LSI1_MMIO_BAR       UINT32_C(0x88004000)
#define HP_ZX6000_LSI1_DIAG_BAR       UINT32_C(0x88020000)
#define HP_ZX6000_BCM5701_MMIO_BAR     UINT32_C(0x88030000)

#define HP_ZX6000_LSI_FUNCTIONS       2U
#define HP_ZX6000_OHCI_FUNCTIONS      2U

#define HP_RX2660_CORE_PCI_ROOT       0U
#define HP_RX2660_FAST_PCI_ROOT       1U
#define HP_RX2660_MANAGEMENT_SLOT     1U
#define HP_RX2660_USB_SLOT            2U
#define HP_RX2660_RN50_SLOT           3U
#define HP_RX2660_SAS1068_SLOT        1U
#define HP_RX2660_BCM5704_SLOT        2U
#define HP_RX2660_BCM5704_FUNCTIONS   2U

#define HP_RX2660_RN50_FB_BAR         UINT32_C(0x80000000)
#define HP_RX2660_RN50_IO_BAR         UINT32_C(0x00001000)
#define HP_RX2660_RN50_MMIO_BAR       UINT32_C(0x88020000)
#define HP_RX2660_RN50_ROM_BAR        UINT32_C(0x88000000)
#define HP_RX2660_OHCI0_MMIO_BAR      UINT32_C(0x88032000)
#define HP_RX2660_OHCI1_MMIO_BAR      UINT32_C(0x88031000)
#define HP_RX2660_EHCI_MMIO_BAR       UINT32_C(0x88030000)
#define HP_RX2660_SAS_IO_BAR          UINT32_C(0x00001000)
#define HP_RX2660_SAS_MMIO_BAR        UINT32_C(0xa0470000)
#define HP_RX2660_SAS_DIAG_BAR        UINT32_C(0xa0460000)
#define HP_RX2660_SAS_ROM_BAR         UINT32_C(0xa0000000)
#define HP_RX2660_BCM0_MMIO_BAR       UINT32_C(0xa0450000)
#define HP_RX2660_BCM1_MMIO_BAR       UINT32_C(0xa0440000)
#define HP_RX2660_BCM0_ROM_BAR        UINT32_C(0xa0420000)
#define HP_RX2660_BCM1_ROM_BAR        UINT32_C(0xa0400000)
#define HP_RX2660_MANAGEMENT_MMIO_BAR UINT32_C(0x88034000)
#define HP_RX2660_CONSOLE_MMIO_BAR    UINT32_C(0x88033000)
#define HP_RX2660_MANAGEMENT_MMIO64_BAR UINT64_C(0x80080000000)

#define TYPE_HP_RX2660_MANAGEMENT_BASE "hp-rx2660-management-base"
#define TYPE_HP_RX2660_MANAGEMENT      "hp-rx2660-management"
#define TYPE_HP_RX2660_MP_INTERFACE    "hp-rx2660-mp-interface"
#define TYPE_HP_RX2660_CONSOLE         "hp-rx2660-console"

typedef struct HPRX2660ManagementState {
    PCIDevice parent_obj;
    MemoryRegion bar1;
    MemoryRegion bar3;
} HPRX2660ManagementState;

typedef struct HPRX2660ConsoleState {
    PCIDevice parent_obj;
    MemoryRegion bar1;
    SerialState uart;
} HPRX2660ConsoleState;

typedef struct HPZX6000RootLayout {
    uint64_t cpu_mmio_base;
    uint64_t mmio_size;
    uint64_t cpu_mmio64_base;
    uint64_t mmio64_size;
    uint16_t io_base;
    uint8_t first_bus;
    uint8_t last_bus;
    uint8_t rope_mask;
    HPZX1IOAMode mode;
    uint64_t bus_mode;
} HPZX6000RootLayout;

struct HPZX6000MachineState {
    HPIA64MachineState parent_obj;

    MemoryRegion low_ram;
    MemoryRegion high_ram;
    MemoryRegion sparse_io;
    MemoryRegion vga_legacy;
    MemoryRegion root_mmio[HP_ZX6000_PCI_ROOT_COUNT];
    MemoryRegion root_mmio64[HP_ZX6000_PCI_ROOT_COUNT];
    AddressSpace root_io[HP_ZX6000_PCI_ROOT_COUNT];
    bool root_io_initialized[HP_ZX6000_PCI_ROOT_COUNT];

    HPZX1MIOState *mio;
    HPZX1IOAState *ioa[HP_ZX6000_PCI_ROOT_COUNT];
    HPZX6000PDHState *pdh;
    PCIDevice *rv100;
    HPIA64Int10 int10;
    PCIDevice *cmd649;
    PCIDevice *i82550;
    PCIDevice *ohci[HP_ZX6000_OHCI_FUNCTIONS];
    PCIDevice *ehci;
    PCIDevice *lsi53c1030[HP_ZX6000_LSI_FUNCTIONS];
    PCIDevice *bcm5701;
    PCIDevice *bcm5704[HP_RX2660_BCM5704_FUNCTIONS];
    PCIDevice *management[3];

    char *nvram_path;
    bool rx2660;
};

static const HPZX6000RootLayout hp_zx6000_roots[] = {
    {
        .cpu_mmio_base = UINT64_C(0x80000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x0000,
        .first_bus = 0x00,
        .last_bus = 0x1f,
        .rope_mask = 0x01,
        .mode = HP_ZX1_IOA_MODE_PCI,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L,
    }, {
        .cpu_mmio_base = UINT64_C(0x88000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x2000,
        .first_bus = 0x20,
        .last_bus = 0x3f,
        .rope_mask = 0x02,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0x90000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x4000,
        .first_bus = 0x40,
        .last_bus = 0x5f,
        .rope_mask = 0x04,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0x98000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x6000,
        .first_bus = 0x60,
        .last_bus = 0x7f,
        .rope_mask = 0x08,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0xa0000000),
        .mmio_size = HP_ZX6000_AGP_MMIO_SIZE,
        .io_base = 0x8000,
        .first_bus = 0x80,
        .last_bus = 0x9f,
        .rope_mask = 0x30,
        .mode = HP_ZX1_IOA_MODE_AGP,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_AGP,
    }, {
        .cpu_mmio_base = UINT64_C(0xb0000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0xa000,
        .first_bus = 0xc0,
        .last_bus = 0xdf,
        .rope_mask = 0x40,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(hp_zx6000_roots) ==
                HP_ZX6000_PCI_ROOT_COUNT);

static const HPZX6000RootLayout hp_rx2660_roots[] = {
    {
        .cpu_mmio_base = UINT64_C(0x80000000),
        .mmio_size = UINT64_C(0x10000000),
        .cpu_mmio64_base = UINT64_C(0x80004000000),
        .mmio64_size = UINT64_C(0xfc000000),
        .io_base = 0x0000,
        .first_bus = 0x00,
        .last_bus = 0x00,
        .rope_mask = 0x01,
        .mode = HP_ZX1_IOA_MODE_PCI,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L,
    }, {
        .cpu_mmio_base = UINT64_C(0xa0000000),
        .mmio_size = UINT64_C(0x10000000),
        .cpu_mmio64_base = UINT64_C(0x80204000000),
        .mmio64_size = UINT64_C(0xfc000000),
        .io_base = 0x4000,
        .first_bus = 0x01,
        .last_bus = 0x01,
        .rope_mask = 0x04,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0xb0000000),
        .mmio_size = UINT64_C(0x10000000),
        .cpu_mmio64_base = UINT64_C(0x80304000000),
        .mmio64_size = UINT64_C(0xfc000000),
        .io_base = 0x2000,
        .first_bus = 0x02,
        .last_bus = 0x02,
        .rope_mask = 0x08,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0xe0000000),
        .mmio_size = UINT64_C(0x10000000),
        .cpu_mmio64_base = UINT64_C(0x80604000000),
        .mmio64_size = UINT64_C(0xfc000000),
        .io_base = 0x6000,
        .first_bus = 0x03,
        .last_bus = 0x03,
        .rope_mask = 0x40,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0xf0000000),
        .mmio_size = UINT64_C(0x0e000000),
        .cpu_mmio64_base = UINT64_C(0x80704000000),
        .mmio64_size = UINT64_C(0xfc000000),
        .io_base = 0x8000,
        .first_bus = 0x04,
        .last_bus = 0x04,
        .rope_mask = 0x80,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(hp_rx2660_roots) ==
                HP_RX2660_PCI_ROOT_COUNT);

static unsigned int hp_zx_root_count(const HPZX6000MachineState *s)
{
    return s->rx2660 ? HP_RX2660_PCI_ROOT_COUNT :
                       HP_ZX6000_PCI_ROOT_COUNT;
}

static const HPZX6000RootLayout *hp_zx_root_layout(
    const HPZX6000MachineState *s, unsigned int root)
{
    return s->rx2660 ? &hp_rx2660_roots[root] : &hp_zx6000_roots[root];
}

static hwaddr hp_zx_ioa_address(const HPZX6000MachineState *s,
                                unsigned int root)
{
    return HP_ZX6000_IOA_BASE +
        ctz32(hp_zx_root_layout(s, root)->rope_mask) *
        HP_ZX6000_IOA_STRIDE;
}

static DeviceState *hp_zx6000_add_child(HPZX6000MachineState *s,
                                        const char *name, const char *type)
{
    DeviceState *dev = qdev_new(type);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_unref(OBJECT(dev));
    return dev;
}

static unsigned int hp_zx6000_route_input(unsigned int root,
                                          unsigned int slot,
                                          unsigned int pin)
{
    unsigned int inputs = root == HP_ZX6000_AGP_ROOT ?
                          HP_ZX6000_AGP_INPUT_COUNT :
                          HP_ZX6000_PCI_INPUT_COUNT;

    /* Onboard functions use direct routes; CMD649 00:02.0 uses INTIN 5. */
    if (root == HP_ZX6000_CORE_PCI_ROOT) {
        if (slot == HP_ZX6000_OHCI_SLOT) {
            return pin < 3 ? pin : 2;
        }
        if (slot == HP_ZX6000_CMD649_SLOT && pin == 0) {
            return 5;
        }
        if (slot == HP_ZX6000_I82550_SLOT && pin == 0) {
            return 4;
        }
    }
    if (root == HP_ZX6000_SCSI_ROOT &&
        slot == HP_ZX6000_LSI53C1030_SLOT) {
        return pin;
    }
    if (root == HP_ZX6000_SCSI_ROOT &&
        slot == HP_ZX6000_BCM5701_SLOT && pin == 0) {
        return 2;
    }
    if (root == HP_ZX6000_AGP_ROOT &&
        slot == HP_ZX6000_RV100_SLOT && pin == 0) {
        return 4;
    }

    return (slot + pin) % inputs;
}

static unsigned int hp_rx2660_route_input(unsigned int root,
                                          unsigned int slot,
                                          unsigned int pin)
{
    if (root == HP_RX2660_CORE_PCI_ROOT) {
        if (slot == HP_RX2660_MANAGEMENT_SLOT && pin == 0) {
            return 0;
        }
        if (slot == HP_RX2660_USB_SLOT) {
            return pin < 3 ? pin + 1 : 3;
        }
        if (slot == HP_RX2660_RN50_SLOT && pin == 0) {
            return 4;
        }
    }
    if (root == HP_RX2660_FAST_PCI_ROOT) {
        if (slot == HP_RX2660_SAS1068_SLOT) {
            return pin;
        }
        if (slot == HP_RX2660_BCM5704_SLOT) {
            return 2 + pin;
        }
    }
    return (slot + pin) % HP_ZX6000_PCI_INPUT_COUNT;
}

static unsigned int hp_zx_route_input(const HPZX6000MachineState *s,
                                      unsigned int root,
                                      unsigned int slot,
                                      unsigned int pin)
{
    return s->rx2660 ? hp_rx2660_route_input(root, slot, pin) :
                       hp_zx6000_route_input(root, slot, pin);
}

static uint32_t hp_zx6000_gsi_base(unsigned int root)
{
    /* MAX_REDIR == 10 makes each MADT GSI range span eleven values. */
    static const uint32_t base[HP_ZX6000_PCI_ROOT_COUNT] = {
        16, 27, 38, 49, 60, 71,
    };

    return base[root];
}

static uint32_t hp_zx6000_device_gsi(const HPZX6000MachineState *s,
                                     unsigned int root, unsigned int slot,
                                     unsigned int pin)
{
    return hp_zx6000_gsi_base(root) +
           hp_zx_route_input(s, root, slot, pin);
}

static hwaddr hp_zx6000_sparse_io_port(hwaddr encoded)
{
    return ((encoded >> 12) << 2) | (encoded & 3);
}

static AddressSpace *hp_zx6000_io_space_for_port(HPZX6000MachineState *s,
                                                 hwaddr port,
                                                 unsigned int size)
{
    bool vga_legacy = port >= HP_ZX6000_VGA_LEGACY_IO_BASE &&
        port < HP_ZX6000_VGA_LEGACY_IO_BASE +
               HP_ZX6000_VGA_LEGACY_IO_SIZE &&
        size <= HP_ZX6000_VGA_LEGACY_IO_BASE +
                HP_ZX6000_VGA_LEGACY_IO_SIZE - port;
    bool vbe_legacy = port >= HP_ZX6000_VBE_LEGACY_IO_BASE &&
        port < HP_ZX6000_VBE_LEGACY_IO_BASE +
               HP_ZX6000_VBE_LEGACY_IO_SIZE &&
        size <= HP_ZX6000_VBE_LEGACY_IO_BASE +
                HP_ZX6000_VBE_LEGACY_IO_SIZE - port;
    unsigned int root;

    if (s->rv100 && (vga_legacy || vbe_legacy)) {
        return &s->root_io[s->rx2660 ? HP_RX2660_CORE_PCI_ROOT :
                                        HP_ZX6000_AGP_ROOT];
    }

    for (root = 0; root < hp_zx_root_count(s); root++) {
        const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);
        uint64_t end = (uint64_t)layout->io_base + HP_ZX6000_PCI_IO_SIZE;

        if (port >= layout->io_base && port < end && size <= end - port) {
            return &s->root_io[root];
        }
    }
    return NULL;
}

static MemTxResult hp_zx6000_io_read(AddressSpace *as, hwaddr addr,
                                     uint64_t *value, unsigned int size,
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

static MemTxResult hp_zx6000_io_write(AddressSpace *as, hwaddr addr,
                                      uint64_t value, unsigned int size,
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

static MemTxResult hp_zx6000_sparse_io_read(void *opaque, hwaddr addr,
                                            uint64_t *value,
                                            unsigned int size,
                                            MemTxAttrs attrs)
{
    HPZX6000MachineState *s = opaque;
    hwaddr port = hp_zx6000_sparse_io_port(addr);
    AddressSpace *as = hp_zx6000_io_space_for_port(s, port, size);

    if (!as) {
        *value = UINT64_MAX;
        return MEMTX_DECODE_ERROR;
    }
    return hp_zx6000_io_read(as, port, value, size, attrs);
}

static MemTxResult hp_zx6000_sparse_io_write(void *opaque, hwaddr addr,
                                             uint64_t value,
                                             unsigned int size,
                                             MemTxAttrs attrs)
{
    HPZX6000MachineState *s = opaque;
    hwaddr port = hp_zx6000_sparse_io_port(addr);
    AddressSpace *as = hp_zx6000_io_space_for_port(s, port, size);

    return as ? hp_zx6000_io_write(as, port, value, size, attrs) :
                MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps hp_zx6000_sparse_io_ops = {
    .read_with_attrs = hp_zx6000_sparse_io_read,
    .write_with_attrs = hp_zx6000_sparse_io_write,
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

static void hp_zx6000_deliver_interrupt(void *opaque,
                                        const HPIOSAPICMessage *message)
{
    MemTxResult result;

    (void)opaque;
    if ((message->address & 7) || message->address < HP_ZX6000_PIB_BASE ||
        message->address > HP_ZX6000_PIB_BASE + HP_ZX6000_PIB_SIZE - 8) {
        return;
    }
    address_space_stq_le(&address_space_memory, message->address,
                         message->data, MEMTXATTRS_UNSPECIFIED, &result);
}

static void hp_zx6000_map_ram(HPZX6000MachineState *s)
{
    MachineState *machine = MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_ZX6000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;
    const char *prefix = s->rx2660 ? "hp-rx2660" : "hp-zx6000";
    g_autofree char *low_name = g_strdup_printf("%s.low-ram", prefix);
    g_autofree char *high_name = g_strdup_printf("%s.high-ram", prefix);

    memory_region_init_alias(&s->low_ram, OBJECT(s), low_name,
                             machine->ram, 0, low_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->low_ram);
    if (high_size) {
        memory_region_init_alias(&s->high_ram, OBJECT(s), high_name,
                                 machine->ram,
                                 low_size, high_size);
        memory_region_add_subregion(get_system_memory(),
                                    HP_ZX6000_HIGH_RAM_BASE, &s->high_ram);
    }
}

static bool hp_zx6000_create_chipset(HPZX6000MachineState *s, Error **errp)
{
    HPZX1MIOIOMMUResetConfig iommu_reset = { 0 };
    DeviceState *dev;
    unsigned int root;

    dev = hp_zx6000_add_child(s, "mio",
                              s->rx2660 ? TYPE_HP_ZX2_MIO : TYPE_HP_ZX1_MIO);
    s->mio = HP_ZX1_MIO(dev);
    if (!hp_zx1_mio_configure_iommu_reset(s->mio, &iommu_reset, errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, HP_ZX6000_MIO_BASE);

    for (root = 0; root < hp_zx_root_count(s); root++) {
        const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);
        HPZX1IOASetup setup = {
            .mode = layout->mode,
            .rope_mask = layout->rope_mask,
            .secondary_bus = layout->first_bus,
            .subordinate_bus = layout->last_bus,
            .pci_reset_asserted = false,
            .bus_mode_reset = layout->bus_mode,
            .deliver = hp_zx6000_deliver_interrupt,
            .delivery_opaque = s,
        };
        g_autofree char *name = g_strdup_printf("ioa%u", root);
        g_autofree char *mmio_name =
            g_strdup_printf("hp-zx6000.root%u-mmio", root);
        g_autofree char *mmio64_name =
            g_strdup_printf("hp-rx2660.root%u-mmio64", root);
        g_autofree char *io_name =
            g_strdup_printf("hp-zx6000.root%u-io", root);
        unsigned int pin;
        unsigned int slot;

        for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
            for (pin = 0; pin < PCI_NUM_PINS; pin++) {
                setup.intx_route[slot][pin] =
                    hp_zx_route_input(s, root, slot, pin);
            }
        }

        dev = hp_zx6000_add_child(s, name, TYPE_HP_ZX1_IOA);
        s->ioa[root] = HP_ZX1_IOA(dev);
        if (!hp_zx1_ioa_setup(s->ioa[root], &setup, errp) ||
            !sysbus_realize(SYS_BUS_DEVICE(dev), errp) ||
            !hp_zx1_mio_attach_ioa(s->mio, s->ioa[root], errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                        hp_zx_ioa_address(s, root));

        memory_region_init_alias(&s->root_mmio[root], OBJECT(s), mmio_name,
                                 hp_zx1_ioa_pci_mem(s->ioa[root]),
                                 layout->cpu_mmio_base,
                                 layout->mmio_size);
        memory_region_add_subregion(get_system_memory(),
                                    layout->cpu_mmio_base,
                                    &s->root_mmio[root]);
        if (layout->mmio64_size) {
            memory_region_init_alias(
                &s->root_mmio64[root], OBJECT(s), mmio64_name,
                hp_zx1_ioa_pci_mem(s->ioa[root]),
                layout->cpu_mmio64_base, layout->mmio64_size);
            memory_region_add_subregion(get_system_memory(),
                                        layout->cpu_mmio64_base,
                                        &s->root_mmio64[root]);
        }
        address_space_init(&s->root_io[root],
                           hp_zx1_ioa_pci_io(s->ioa[root]), io_name);
        s->root_io_initialized[root] = true;
    }
    return true;
}

static bool hp_zx6000_create_pdh(HPZX6000MachineState *s, Error **errp)
{
    static const hwaddr bases[] = {
        HP_ZX6000_PDH_UART0_BASE,
        HP_ZX6000_PDH_UART1_BASE,
        HP_ZX6000_PDH_NVRAM_BASE,
        HP_ZX6000_PDH_RTC_BASE,
        HP_ZX6000_PDH_CONTROL_BASE,
        HP_ZX6000_PDH_ACPI_PM_BASE,
    };
    g_autofree char *nvram_path = ia64_machine_resolve_nvram_path(
        MACHINE(s), s->nvram_path);
    DeviceState *dev = hp_zx6000_add_child(s, "pdh", TYPE_HP_ZX6000_PDH);
    Chardev *chardev;
    unsigned int region;

    G_STATIC_ASSERT(G_N_ELEMENTS(bases) == HP_ZX6000_PDH_MMIO_COUNT);
    s->pdh = HP_ZX6000_PDH(dev);
    chardev = serial_hd(0);
    if (chardev) {
        qdev_prop_set_chr(dev, "chardev0", chardev);
    }
    chardev = serial_hd(1);
    if (chardev) {
        qdev_prop_set_chr(dev, "chardev1", chardev);
    }
    qdev_prop_set_string(dev, HP_ZX6000_PDH_PROP_NVRAM,
                         nvram_path ?: "none");
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX, 8));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX, 9));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX,
                               HP_ZX6000_ACPI_SCI_INPUT));
    for (region = 0; region < G_N_ELEMENTS(bases); region++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), region, bases[region]);
    }

    /* Fixed platform registers must win over an incorrectly assigned BAR. */
    memory_region_add_subregion_overlap(
        hp_zx1_ioa_pci_io(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
        IA64_PLATFORM_ACPI_PM_IO_BASE,
        hp_zx6000_pdh_acpi_pm_io(s->pdh), 2);

    return true;
}

static bool hp_zx6000_realize_pci_device(PCIDevice *dev, PCIBus *bus,
                                         Error **errp)
{
    return pci_realize_and_unref(dev, bus, errp);
}

static uint64_t hp_rx2660_management_read(void *opaque, hwaddr addr,
                                          unsigned int size)
{
    return UINT64_MAX;
}

static void hp_rx2660_management_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned int size)
{
}

static const MemoryRegionOps hp_rx2660_management_ops = {
    .read = hp_rx2660_management_read,
    .write = hp_rx2660_management_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static void hp_rx2660_management_realize(PCIDevice *dev, Error **errp)
{
    HPRX2660ManagementState *s =
        (HPRX2660ManagementState *)OBJECT_CHECK(
            HPRX2660ManagementState, dev, TYPE_HP_RX2660_MANAGEMENT_BASE);

    if (object_dynamic_cast(OBJECT(dev), TYPE_HP_RX2660_MP_INTERFACE)) {
        memory_region_init_io(&s->bar1, OBJECT(s),
                              &hp_rx2660_management_ops, s,
                              "hp-rx2660-mp-interface-mmio", 4 * KiB);
        pci_register_bar(dev, 1,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                         &s->bar1);
        memory_region_init_io(&s->bar3, OBJECT(s),
                              &hp_rx2660_management_ops, s,
                              "hp-rx2660-mp-interface-mmio64", 128 * KiB);
        pci_register_bar(dev, 3,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64 |
                         PCI_BASE_ADDRESS_MEM_PREFETCH,
                         &s->bar3);
    }
}

static void hp_rx2660_console_realize(PCIDevice *dev, Error **errp)
{
    HPRX2660ConsoleState *s = OBJECT_CHECK(
        HPRX2660ConsoleState, dev, TYPE_HP_RX2660_CONSOLE);

    if (!qdev_realize(DEVICE(&s->uart), NULL, errp)) {
        return;
    }

    /*
     * The RMP3 Diva (103c:1048, subsystem 103c:1301) has one 16550 at BAR1
     * offset zero with byte register spacing and a 115200 baud base.
     */
    memory_region_init(&s->bar1, OBJECT(s),
                       "hp-rx2660-console-mmio", 4 * KiB);
    memory_region_init_io(&s->uart.io, OBJECT(s), &serial_io_ops, &s->uart,
                          "hp-rx2660-console-uart", 8);
    memory_region_add_subregion(&s->bar1, 0, &s->uart.io);
    pci_register_bar(dev, 1,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &s->bar1);
    pci_set_byte(dev->config + PCI_CLASS_PROG, 0x02);
    pci_set_byte(dev->config + PCI_INTERRUPT_PIN, 1);
    s->uart.irq = pci_allocate_irq(dev);
}

static void hp_rx2660_console_exit(PCIDevice *dev)
{
    HPRX2660ConsoleState *s = OBJECT_CHECK(
        HPRX2660ConsoleState, dev, TYPE_HP_RX2660_CONSOLE);

    memory_region_del_subregion(&s->bar1, &s->uart.io);
    qdev_unrealize(DEVICE(&s->uart));
    qemu_free_irq(s->uart.irq);
}

static void hp_rx2660_console_init(Object *obj)
{
    HPRX2660ConsoleState *s = OBJECT_CHECK(
        HPRX2660ConsoleState, obj, TYPE_HP_RX2660_CONSOLE);

    object_initialize_child(obj, "uart", &s->uart, TYPE_SERIAL);
    qdev_alias_all_properties(DEVICE(&s->uart), obj);
}

static const VMStateDescription vmstate_hp_rx2660_console = {
    .name = TYPE_HP_RX2660_CONSOLE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, HPRX2660ConsoleState),
        VMSTATE_STRUCT(uart, HPRX2660ConsoleState, 0,
                       vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    },
};

typedef struct HPRX2660ManagementInfo {
    uint16_t device_id;
    uint16_t subsystem_id;
    uint16_t class_id;
    const char *description;
} HPRX2660ManagementInfo;

static void hp_rx2660_management_class_init(ObjectClass *klass,
                                             const void *data)
{
    const HPRX2660ManagementInfo *info = data;
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = hp_rx2660_management_realize;
    pc->vendor_id = PCI_VENDOR_ID_HP;
    pc->device_id = info->device_id;
    pc->revision = 0;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    pc->subsystem_id = info->subsystem_id;
    pc->class_id = info->class_id;
    dc->desc = info->description;
    dc->vmsd = &vmstate_pci_device;
    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void hp_rx2660_console_class_init(ObjectClass *klass,
                                         const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    hp_rx2660_management_class_init(klass, data);
    pc->realize = hp_rx2660_console_realize;
    pc->exit = hp_rx2660_console_exit;
    dc->vmsd = &vmstate_hp_rx2660_console;
    dc->hotpluggable = false;
}

static const HPRX2660ManagementInfo hp_rx2660_management_info[] = {
    {
        .device_id = 0x1303,
        .subsystem_id = 0x1303,
        .class_id = 0xff00,
        .description = "HP rx2660 management function",
    }, {
        .device_id = 0x1302,
        .subsystem_id = 0x1302,
        .class_id = PCI_CLASS_COMMUNICATION_OTHER,
        .description = "HP rx2660 management processor interface",
    }, {
        .device_id = 0x1048,
        .subsystem_id = 0x1301,
        .class_id = PCI_CLASS_COMMUNICATION_SERIAL,
        .description = "HP rx2660 management serial interface",
    },
};

static const TypeInfo hp_rx2660_management_base_type = {
    .name = TYPE_HP_RX2660_MANAGEMENT_BASE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(HPRX2660ManagementState),
    .abstract = true,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo hp_rx2660_management_types[] = {
    {
        .name = TYPE_HP_RX2660_MANAGEMENT,
        .parent = TYPE_HP_RX2660_MANAGEMENT_BASE,
        .class_init = hp_rx2660_management_class_init,
        .class_data = &hp_rx2660_management_info[0],
    }, {
        .name = TYPE_HP_RX2660_MP_INTERFACE,
        .parent = TYPE_HP_RX2660_MANAGEMENT_BASE,
        .class_init = hp_rx2660_management_class_init,
        .class_data = &hp_rx2660_management_info[1],
    }, {
        .name = TYPE_HP_RX2660_CONSOLE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(HPRX2660ConsoleState),
        .instance_init = hp_rx2660_console_init,
        .class_init = hp_rx2660_console_class_init,
        .class_data = &hp_rx2660_management_info[2],
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },
};

static bool hp_rx2660_create_pci_devices(HPZX6000MachineState *s,
                                         Error **errp)
{
    static const char *const management_types[] = {
        TYPE_HP_RX2660_MANAGEMENT,
        TYPE_HP_RX2660_MP_INTERFACE,
        TYPE_HP_RX2660_CONSOLE,
    };
    MachineState *machine = MACHINE(s);
    PCIBus *core = hp_zx1_ioa_bus(s->ioa[HP_RX2660_CORE_PCI_ROOT]);
    PCIBus *fast = hp_zx1_ioa_bus(s->ioa[HP_RX2660_FAST_PCI_ROOT]);
    SCSIBus *scsi_bus;
    BusState *usb_bus;
    unsigned int function;

    s->rv100 = pci_vga_new();
    if (s->rv100) {
        if (!object_dynamic_cast(OBJECT(s->rv100), TYPE_ATI_VGA)) {
            error_setg(errp, "%s supports ATI VGA or no VGA",
                       TYPE_HP_RX2660_MACHINE);
            object_unref(OBJECT(s->rv100));
            s->rv100 = NULL;
            return false;
        }
        s->rv100->devfn = PCI_DEVFN(HP_RX2660_RN50_SLOT, 0);
        qdev_prop_set_string(DEVICE(s->rv100), "model", "es1000");
        qdev_prop_set_uint32(DEVICE(s->rv100), "romsize", 128 * KiB);
        if (!hp_zx6000_realize_pci_device(s->rv100, core, errp)) {
            return false;
        }
        pci_set_byte(s->rv100->config + PCI_REVISION_ID, 0x02);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_HP);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_ID, 0x1304);
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), "hp-rx2660.vga-legacy",
            hp_zx1_ioa_pci_mem(s->ioa[HP_RX2660_CORE_PCI_ROOT]),
            HP_ZX6000_VGA_LEGACY_BASE, HP_ZX6000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_ZX6000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb) {
        s->ehci = pci_new_multifunction(
            PCI_DEVFN(HP_RX2660_USB_SLOT, 2), TYPE_NEC_USB_EHCI);
        qdev_prop_set_uint8(DEVICE(s->ehci), "interrupt-pin", 3);
        qdev_prop_set_uint8(DEVICE(s->ehci), "num-ports", 5);
        if (!hp_zx6000_realize_pci_device(s->ehci, core, errp)) {
            return false;
        }
        pci_set_byte(s->ehci->config + PCI_REVISION_ID, 0x02);
        usb_bus = QLIST_FIRST(&DEVICE(s->ehci)->child_bus);
        if (!usb_bus) {
            error_setg(errp, "%s did not create its USB bus",
                       TYPE_NEC_USB_EHCI);
            return false;
        }
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            s->ohci[function] = pci_new_multifunction(
                PCI_DEVFN(HP_RX2660_USB_SLOT, function),
                TYPE_NEC_USB_OHCI);
            qdev_prop_set_string(DEVICE(s->ohci[function]), "masterbus",
                                 usb_bus->name);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "firstport",
                                 function * 3);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "num-ports",
                                 function ? 2 : 3);
            qdev_prop_set_uint8(DEVICE(s->ohci[function]),
                                "interrupt-pin", function + 1);
            if (!hp_zx6000_realize_pci_device(s->ohci[function], core,
                                               errp)) {
                return false;
            }
            pci_set_byte(s->ohci[function]->config + PCI_REVISION_ID, 0x41);
        }
        if (defaults_enabled()) {
            usb_create_simple(USB_BUS(usb_bus), "usb-kbd");
            usb_create_simple(USB_BUS(usb_bus), "usb-tablet");
        }
    }

    s->lsi53c1030[0] = pci_new(
        PCI_DEVFN(HP_RX2660_SAS1068_SLOT, 0), TYPE_MPTSAS1068);
    qdev_prop_set_bit(DEVICE(s->lsi53c1030[0]), "x-pci-64bit-bars", true);
    qdev_prop_set_uint32(DEVICE(s->lsi53c1030[0]), "x-pci-rom-size",
                         4 * MiB);
    if (!hp_zx6000_realize_pci_device(s->lsi53c1030[0], fast, errp)) {
        return false;
    }
    pci_set_byte(s->lsi53c1030[0]->config + PCI_REVISION_ID, 0x01);
    pci_set_word(s->lsi53c1030[0]->config + PCI_SUBSYSTEM_VENDOR_ID,
                 PCI_VENDOR_ID_HP);
    pci_set_word(s->lsi53c1030[0]->config + PCI_SUBSYSTEM_ID, 0x1312);
    scsi_bus = mpt_fusion_get_scsi_bus(s->lsi53c1030[0]);
    scsi_bus_legacy_handle_cmdline(scsi_bus);

    for (function = 0; function < HP_RX2660_BCM5704_FUNCTIONS;
         function++) {
        s->bcm5704[function] = pci_new_multifunction(
            PCI_DEVFN(HP_RX2660_BCM5704_SLOT, function), TYPE_BCM5704);
        qemu_configure_nic_device(DEVICE(s->bcm5704[function]), true,
                                  NULL);
        if (!hp_zx6000_realize_pci_device(s->bcm5704[function], fast,
                                           errp)) {
            return false;
        }
    }

    for (function = 0; function < G_N_ELEMENTS(management_types);
         function++) {
        s->management[function] = pci_new_multifunction(
            PCI_DEVFN(HP_RX2660_MANAGEMENT_SLOT, function),
            management_types[function]);
        if (function == 2 && serial_hd(2)) {
            qdev_prop_set_chr(DEVICE(s->management[function]), "chardev",
                              serial_hd(2));
        }
        if (!hp_zx6000_realize_pci_device(s->management[function], core,
                                           errp)) {
            return false;
        }
        pci_set_byte(s->management[function]->config + PCI_INTERRUPT_PIN,
                     1);
    }
    return true;
}

static bool hp_zx6000_create_pci_devices(HPZX6000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    PCIBus *agp = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_AGP_ROOT]);
    PCIBus *core = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_CORE_PCI_ROOT]);
    PCIBus *scsi = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_SCSI_ROOT]);
    DriveInfo *ide_drive;
    SCSIBus *scsi_bus;
    BusState *usb_bus;
    unsigned int channel, function, unit;

    if (s->rx2660) {
        return hp_rx2660_create_pci_devices(s, errp);
    }

    s->rv100 = pci_vga_new();
    if (s->rv100) {
        if (!object_dynamic_cast(OBJECT(s->rv100), TYPE_ATI_VGA)) {
            error_setg(errp, "%s supports ATI VGA or no VGA",
                       TYPE_HP_ZX6000_MACHINE);
            object_unref(OBJECT(s->rv100));
            s->rv100 = NULL;
            return false;
        }
        s->rv100->devfn = PCI_DEVFN(HP_ZX6000_RV100_SLOT, 0);
        qdev_prop_set_string(DEVICE(s->rv100), "model", "rv100");
        qdev_prop_set_uint32(DEVICE(s->rv100), "vgamem_mb", 32);
        if (!hp_zx6000_realize_pci_device(s->rv100, agp, errp)) {
            return false;
        }
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_HP);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_ID, 0x1292);
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), "hp-zx6000.vga-legacy",
            hp_zx1_ioa_pci_mem(s->ioa[HP_ZX6000_AGP_ROOT]),
            HP_ZX6000_VGA_LEGACY_BASE, HP_ZX6000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_ZX6000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    s->cmd649 = pci_new(PCI_DEVFN(HP_ZX6000_CMD649_SLOT, 0),
                        TYPE_CMD649_IDE);
    qdev_prop_set_bit(DEVICE(s->cmd649), "primary-cable80", true);
    if (!hp_zx6000_realize_pci_device(s->cmd649, core, errp)) {
        return false;
    }
    for (channel = 0; channel < ARRAY_SIZE(PCI_IDE(s->cmd649)->bus);
         channel++) {
        IDEBus *ide_bus = &PCI_IDE(s->cmd649)->bus[channel];

        for (unit = 0; unit < ide_bus->max_units; unit++) {
            ide_drive = drive_get(IF_IDE, channel, unit);
            if (ide_drive) {
                ide_bus_create_drive(ide_bus, unit, ide_drive);
            }
        }
    }

    s->i82550 = pci_new(PCI_DEVFN(HP_ZX6000_I82550_SLOT, 0), "i82550");
    qdev_prop_set_string(DEVICE(s->i82550), "romfile", "");
    qdev_prop_set_uint8(DEVICE(s->i82550), "x-pci-revision", 0x0d);
    qdev_prop_set_uint16(DEVICE(s->i82550),
                         "x-pci-subsystem-vendor-id", PCI_VENDOR_ID_HP);
    qdev_prop_set_uint16(DEVICE(s->i82550),
                         "x-pci-subsystem-id", 0x1274);
    qemu_configure_nic_device(DEVICE(s->i82550), true, NULL);
    if (!hp_zx6000_realize_pci_device(s->i82550, core, errp)) {
        return false;
    }

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb) {
        s->ehci = pci_new_multifunction(
            PCI_DEVFN(HP_ZX6000_OHCI_SLOT, 2),
            TYPE_NEC_USB_EHCI);
        qdev_prop_set_uint8(DEVICE(s->ehci), "interrupt-pin", 3);
        qdev_prop_set_uint8(DEVICE(s->ehci), "num-ports", 5);
        if (!hp_zx6000_realize_pci_device(s->ehci, core, errp)) {
            return false;
        }
        pci_set_byte(s->ehci->config + PCI_REVISION_ID, 0x02);
        usb_bus = QLIST_FIRST(&DEVICE(s->ehci)->child_bus);
        if (!usb_bus) {
            error_setg(errp, "%s did not create its USB bus",
                       TYPE_NEC_USB_EHCI);
            return false;
        }
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            s->ohci[function] = pci_new_multifunction(
                PCI_DEVFN(HP_ZX6000_OHCI_SLOT, function),
                TYPE_NEC_USB_OHCI);
            qdev_prop_set_string(DEVICE(s->ohci[function]), "masterbus",
                                 usb_bus->name);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "firstport",
                                 function * 3);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "num-ports",
                                 function ? 2 : 3);
            qdev_prop_set_uint8(DEVICE(s->ohci[function]),
                                "interrupt-pin", function + 1);
            if (!hp_zx6000_realize_pci_device(s->ohci[function], core,
                                               errp)) {
                return false;
            }
            pci_set_byte(s->ohci[function]->config + PCI_REVISION_ID, 0x41);
        }
        if (defaults_enabled()) {
            usb_create_simple(USB_BUS(usb_bus), "usb-kbd");
            usb_create_simple(USB_BUS(usb_bus), "usb-tablet");
        }
    }

    for (function = 0; function < HP_ZX6000_LSI_FUNCTIONS; function++) {
        s->lsi53c1030[function] = pci_new_multifunction(
            PCI_DEVFN(HP_ZX6000_LSI53C1030_SLOT, function),
            TYPE_LSI53C1030);
        if (!hp_zx6000_realize_pci_device(s->lsi53c1030[function], scsi,
                                           errp)) {
            return false;
        }
        pci_set_byte(s->lsi53c1030[function]->config + PCI_REVISION_ID,
                     0x07);
        pci_set_byte(s->lsi53c1030[function]->config + PCI_INTERRUPT_PIN,
                     function + 1);
        scsi_bus = mpt_fusion_get_scsi_bus(s->lsi53c1030[function]);
        scsi_bus_legacy_handle_cmdline(scsi_bus);
    }

    s->bcm5701 = pci_new(PCI_DEVFN(HP_ZX6000_BCM5701_SLOT, 0),
                         TYPE_BCM5701);
    qemu_configure_nic_device(DEVICE(s->bcm5701), true, NULL);
    if (!hp_zx6000_realize_pci_device(s->bcm5701, scsi, errp)) {
        return false;
    }
    pci_set_word(s->bcm5701->config + PCI_SUBSYSTEM_VENDOR_ID,
                 PCI_VENDOR_ID_HP);
    pci_set_word(s->bcm5701->config + PCI_SUBSYSTEM_ID, 0x12a4);
    pci_set_byte(s->bcm5701->config + PCI_INTERRUPT_PIN, 1);
    return true;
}

static void hp_zx6000_write_bar(PCIDevice *dev, unsigned int bar,
                                uint32_t address)
{
    pci_default_write_config(dev, PCI_BASE_ADDRESS_0 + bar * 4,
                             address, sizeof(address));
}

static void hp_zx6000_write_rom_bar(PCIDevice *dev, uint32_t address)
{
    pci_default_write_config(dev, PCI_ROM_ADDRESS, address,
                             sizeof(address));
}

static void hp_zx6000_enable_pci_device(PCIDevice *dev, uint16_t command,
                                        uint8_t interrupt_line)
{
    pci_default_write_config(dev, PCI_COMMAND, command, sizeof(command));
    pci_default_write_config(dev, PCI_INTERRUPT_LINE, interrupt_line,
                             sizeof(interrupt_line));
}

static void hp_rx2660_configure_pci(HPZX6000MachineState *s)
{
    static const uint32_t ohci_bars[] = {
        HP_RX2660_OHCI0_MMIO_BAR,
        HP_RX2660_OHCI1_MMIO_BAR,
    };
    static const uint32_t bcm_rom_bars[] = {
        HP_RX2660_BCM0_ROM_BAR,
        HP_RX2660_BCM1_ROM_BAR,
    };
    static const uint32_t bcm_mmio_bars[] = {
        HP_RX2660_BCM0_MMIO_BAR,
        HP_RX2660_BCM1_MMIO_BAR,
    };
    unsigned int function;

    if (s->rv100) {
        hp_zx6000_write_bar(s->rv100, 0, HP_RX2660_RN50_FB_BAR);
        hp_zx6000_write_bar(s->rv100, 1, HP_RX2660_RN50_IO_BAR);
        hp_zx6000_write_bar(s->rv100, 2, HP_RX2660_RN50_MMIO_BAR);
        hp_zx6000_write_rom_bar(s->rv100, HP_RX2660_RN50_ROM_BAR);
        hp_zx6000_enable_pci_device(
            s->rv100, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
            PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_RX2660_CORE_PCI_ROOT,
                                 HP_RX2660_RN50_SLOT, 0));
    }

    if (s->ehci) {
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            hp_zx6000_write_bar(s->ohci[function], 0,
                                ohci_bars[function]);
            hp_zx6000_enable_pci_device(
                s->ohci[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                hp_zx6000_device_gsi(s, HP_RX2660_CORE_PCI_ROOT,
                                     HP_RX2660_USB_SLOT, function));
        }
        hp_zx6000_write_bar(s->ehci, 0, HP_RX2660_EHCI_MMIO_BAR);
        hp_zx6000_enable_pci_device(
            s->ehci, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_RX2660_CORE_PCI_ROOT,
                                 HP_RX2660_USB_SLOT, 2));
    }

    hp_zx6000_write_bar(s->lsi53c1030[0], 0, HP_RX2660_SAS_IO_BAR);
    hp_zx6000_write_bar(s->lsi53c1030[0], 1, HP_RX2660_SAS_MMIO_BAR);
    hp_zx6000_write_bar(s->lsi53c1030[0], 2, 0);
    hp_zx6000_write_bar(s->lsi53c1030[0], 3, HP_RX2660_SAS_DIAG_BAR);
    hp_zx6000_write_bar(s->lsi53c1030[0], 4, 0);
    hp_zx6000_write_rom_bar(s->lsi53c1030[0], HP_RX2660_SAS_ROM_BAR);
    hp_zx6000_enable_pci_device(
        s->lsi53c1030[0],
        PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, HP_RX2660_FAST_PCI_ROOT,
                             HP_RX2660_SAS1068_SLOT, 0));

    for (function = 0; function < HP_RX2660_BCM5704_FUNCTIONS;
         function++) {
        pci_set_byte(s->bcm5704[function]->config + PCI_INTERRUPT_PIN,
                     function + 1);
        hp_zx6000_write_bar(s->bcm5704[function], 0,
                            bcm_mmio_bars[function]);
        hp_zx6000_write_bar(s->bcm5704[function], 1, 0);
        hp_zx6000_write_rom_bar(s->bcm5704[function],
                                bcm_rom_bars[function]);
        hp_zx6000_enable_pci_device(
            s->bcm5704[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_RX2660_FAST_PCI_ROOT,
                                 HP_RX2660_BCM5704_SLOT, function));
    }

    hp_zx6000_write_bar(s->management[1], 1,
                        HP_RX2660_MANAGEMENT_MMIO_BAR);
    hp_zx6000_write_bar(s->management[1], 2, 0);
    hp_zx6000_write_bar(s->management[1], 3,
                        (uint32_t)HP_RX2660_MANAGEMENT_MMIO64_BAR);
    hp_zx6000_write_bar(
        s->management[1], 4,
        (uint32_t)(HP_RX2660_MANAGEMENT_MMIO64_BAR >> 32));
    hp_zx6000_enable_pci_device(
        s->management[1], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 0);

    hp_zx6000_write_bar(s->management[2], 1,
                        HP_RX2660_CONSOLE_MMIO_BAR);
    hp_zx6000_write_bar(s->management[2], 2, 0);
    hp_zx6000_enable_pci_device(
        s->management[2], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        0);

}

static void hp_zx6000_configure_pci(HPZX6000MachineState *s)
{
    static const uint32_t ohci_bars[] = {
        HP_ZX6000_OHCI0_MMIO_BAR,
        HP_ZX6000_OHCI1_MMIO_BAR,
    };
    static const uint32_t lsi_io_bars[] = {
        HP_ZX6000_LSI0_IO_BAR,
        HP_ZX6000_LSI1_IO_BAR,
    };
    static const uint32_t lsi_mmio_bars[] = {
        HP_ZX6000_LSI0_MMIO_BAR,
        HP_ZX6000_LSI1_MMIO_BAR,
    };
    static const uint32_t lsi_diag_bars[] = {
        HP_ZX6000_LSI0_DIAG_BAR,
        HP_ZX6000_LSI1_DIAG_BAR,
    };
    unsigned int function;

    if (s->rx2660) {
        hp_rx2660_configure_pci(s);
        return;
    }

    if (s->rv100) {
        hp_zx6000_write_bar(s->rv100, 0, HP_ZX6000_RV100_FB_BAR);
        hp_zx6000_write_bar(s->rv100, 1, HP_ZX6000_RV100_IO_BAR);
        hp_zx6000_write_bar(s->rv100, 2, HP_ZX6000_RV100_MMIO_BAR);
        hp_zx6000_write_rom_bar(s->rv100, HP_ZX6000_RV100_ROM_BAR |
                                PCI_ROM_ADDRESS_ENABLE);
        hp_zx6000_enable_pci_device(
            s->rv100, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
            PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_ZX6000_AGP_ROOT,
                                 HP_ZX6000_RV100_SLOT, 0));
    }

    hp_zx6000_write_bar(s->cmd649, 0, HP_ZX6000_CMD649_DATA_BAR);
    hp_zx6000_write_bar(s->cmd649, 1, HP_ZX6000_CMD649_CONTROL_BAR);
    hp_zx6000_write_bar(s->cmd649, 2,
                        HP_ZX6000_CMD649_SECONDARY_DATA_BAR);
    hp_zx6000_write_bar(s->cmd649, 3,
                        HP_ZX6000_CMD649_SECONDARY_CONTROL_BAR);
    hp_zx6000_write_bar(s->cmd649, 4, HP_ZX6000_CMD649_BMDMA_BAR);
    hp_zx6000_enable_pci_device(
        s->cmd649, PCI_COMMAND_IO | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, HP_ZX6000_CORE_PCI_ROOT,
                             HP_ZX6000_CMD649_SLOT, 0));

    hp_zx6000_write_bar(s->i82550, 0, HP_ZX6000_I82550_MMIO_BAR);
    hp_zx6000_write_bar(s->i82550, 1, HP_ZX6000_I82550_IO_BAR);
    hp_zx6000_write_bar(s->i82550, 2, HP_ZX6000_I82550_FLASH_BAR);
    hp_zx6000_enable_pci_device(
        s->i82550, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, HP_ZX6000_CORE_PCI_ROOT,
                             HP_ZX6000_I82550_SLOT, 0));

    if (s->ehci) {
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            hp_zx6000_write_bar(s->ohci[function], 0,
                                ohci_bars[function]);
            hp_zx6000_enable_pci_device(
                s->ohci[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                hp_zx6000_device_gsi(s, HP_ZX6000_CORE_PCI_ROOT,
                                     HP_ZX6000_OHCI_SLOT, function));
        }
        hp_zx6000_write_bar(s->ehci, 0, HP_ZX6000_EHCI_MMIO_BAR);
        hp_zx6000_enable_pci_device(
            s->ehci, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_ZX6000_CORE_PCI_ROOT,
                                 HP_ZX6000_OHCI_SLOT, 2));
    }

    for (function = 0; function < HP_ZX6000_LSI_FUNCTIONS; function++) {
        hp_zx6000_write_bar(s->lsi53c1030[function], 0,
                            lsi_io_bars[function]);
        hp_zx6000_write_bar(s->lsi53c1030[function], 1,
                            lsi_mmio_bars[function]);
        hp_zx6000_write_bar(s->lsi53c1030[function], 2,
                            lsi_diag_bars[function]);
        hp_zx6000_enable_pci_device(
            s->lsi53c1030[function],
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, HP_ZX6000_SCSI_ROOT,
                                 HP_ZX6000_LSI53C1030_SLOT, function));
    }

    hp_zx6000_write_bar(s->bcm5701, 0, HP_ZX6000_BCM5701_MMIO_BAR);
    hp_zx6000_write_bar(s->bcm5701, 1, 0);
    hp_zx6000_enable_pci_device(
        s->bcm5701, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, HP_ZX6000_SCSI_ROOT,
                             HP_ZX6000_BCM5701_SLOT, 0));
}

static void hp_zx6000_machine_done(void *opaque)
{
    hp_zx6000_configure_pci(HP_ZX6000_MACHINE(opaque));
}

static bool hp_zx6000_init_int10(HPZX6000MachineState *s, Error **errp)
{
    HPIA64Int10Config config;
    unsigned int vga_root = s->rx2660 ? HP_RX2660_CORE_PCI_ROOT :
                                        HP_ZX6000_AGP_ROOT;

    if (!s->rv100) {
        return true;
    }

    config = (HPIA64Int10Config) {
        .owner = OBJECT(s),
        .vga = s->rv100,
        .service_io = hp_zx1_ioa_pci_io(
            s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
        .vga_io = &s->root_io[vga_root],
        .framebuffer_base = s->rx2660 ? HP_RX2660_RN50_FB_BAR :
                                       HP_ZX6000_RV100_FB_BAR,
        .framebuffer_bar = 0,
        .mmio_bar = 2,
        .region_name = s->rx2660 ? "hp-rx2660.int10-pci-io" :
                                   "hp-zx6000.int10-pci-io",
    };
    return hp_ia64_int10_init(&s->int10, &config, errp);
}

static void hp_zx6000_add_route(const HPZX6000MachineState *s,
                                IA64PlatformPciRoute *route,
                                unsigned int root, unsigned int slot,
                                unsigned int pin)
{
    route->Segment = cpu_to_le16(0);
    route->Bus = hp_zx_root_layout(s, root)->first_bus;
    route->Device = slot;
    route->Pin = pin;
    route->Gsi = cpu_to_le32(hp_zx6000_device_gsi(s, root, slot, pin));
}

static bool hp_zx6000_install_descriptor(HPZX6000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_ZX6000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;
    IA64PlatformDescriptor header = {
        .Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC),
        .FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION),
        .PlatformId = cpu_to_le32(s->rx2660 ?
                                  IA64_PLATFORM_ID_HP_RX2660 :
                                  IA64_PLATFORM_ID_HP_ZX6000),
        .Flags = cpu_to_le32(IA64_PLATFORM_FLAG_NO_MCFG |
                             IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                             (s->rx2660 ? 0 :
                              IA64_PLATFORM_FLAG_IDE_DMA)),
        .RamSize = cpu_to_le64(machine->ram_size),
        .LowRamEnd = cpu_to_le64(low_size),
        .FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE),
        .FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE),
        .ProcessorCount = cpu_to_le32(machine->smp.cpus),
        .SocketCount = cpu_to_le32(machine->smp.sockets),
        .CoresPerSocket = cpu_to_le32(machine->smp.cores),
        .ThreadsPerCore = cpu_to_le32(machine->smp.threads),
        .LegacyIoBase = cpu_to_le64(HP_ZX6000_LEGACY_IO_BASE),
        .LegacyIoSize = cpu_to_le64(HP_ZX6000_LEGACY_IO_SIZE),
        .LocalSapicBase = cpu_to_le64(HP_ZX6000_PIB_BASE),
        .LocalSapicSize = cpu_to_le64(HP_ZX6000_PIB_SIZE),
        .ConsoleBase = cpu_to_le64(HP_ZX6000_PDH_UART0_BASE),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(
            HP_ZX6000_PDH_UART_INPUT_CLOCK_HZ),
        .ConsoleIrq = cpu_to_le32(
            hp_zx6000_gsi_base(HP_ZX6000_CORE_PCI_ROOT) + 8),
        .NvramBase = cpu_to_le64(HP_ZX6000_PDH_NVRAM_BASE),
        .NvramSize = cpu_to_le64(HP_ZX6000_PDH_NVRAM_SIZE),
        .RtcBase = cpu_to_le64(HP_ZX6000_PDH_RTC_BASE),
        .RtcSize = cpu_to_le64(HP_ZX6000_PDH_RTC_SIZE),
        .ControlBase = cpu_to_le64(HP_ZX6000_PDH_CONTROL_BASE),
        .ControlSize = cpu_to_le64(HP_ZX6000_PDH_CONTROL_SIZE),
        .ResetControlOffset = cpu_to_le32(
            HP_ZX6000_PDH_CONTROL_RESET_OFFSET),
        .PoweroffControlOffset = cpu_to_le32(
            HP_ZX6000_PDH_CONTROL_POWEROFF_OFFSET),
        .ControlValue = cpu_to_le32(HP_ZX6000_PDH_CONTROL_VALUE),
        .AcpiPmBase = cpu_to_le64(HP_ZX6000_PDH_ACPI_PM_BASE),
        .AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE),
        .AcpiSciGsi = cpu_to_le32(
            hp_zx6000_gsi_base(HP_ZX6000_CORE_PCI_ROOT) +
            HP_ZX6000_ACPI_SCI_INPUT),
    };
    IA64PlatformRamRange ram[2] = {
        {
            .Base = cpu_to_le64(0),
            .Size = cpu_to_le64(low_size),
        }, {
            .Base = cpu_to_le64(HP_ZX6000_HIGH_RAM_BASE),
            .Size = cpu_to_le64(high_size),
        },
    };
    IA64PlatformPciRoot roots[HP_ZX6000_PCI_ROOT_COUNT] = { 0 };
    IA64PlatformIoSapic sapics[HP_ZX6000_PCI_ROOT_COUNT] = { 0 };
    IA64PlatformPciRoute routes[9] = { 0 };
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = ram,
        .ram_range_count = high_size ? 2 : 1,
        .pci_roots = roots,
        .pci_root_count = hp_zx_root_count(s),
        .io_sapics = sapics,
        .io_sapic_count = hp_zx_root_count(s),
        .pci_routes = routes,
    };
    unsigned int route_count = 0;
    unsigned int root;

    if (hp_zx6000_pdh_nvram_persistent(s->pdh)) {
        header.Flags = cpu_to_le32(le32_to_cpu(header.Flags) |
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT);
    }

    for (root = 0; root < hp_zx_root_count(s); root++) {
        const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);

        roots[root].Segment = cpu_to_le16(0);
        roots[root].Bus = layout->first_bus;
        roots[root].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
        roots[root].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
            IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO |
            (!s->rx2660 && root == HP_ZX6000_AGP_ROOT ?
             IA64_PLATFORM_PCI_ROOT_FLAG_AGP : 0) |
            (s->rv100 &&
             root == (s->rx2660 ? HP_RX2660_CORE_PCI_ROOT :
                                  HP_ZX6000_AGP_ROOT) ?
             IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY : 0));
        roots[root].ConfigBase = cpu_to_le64(
            hp_zx_ioa_address(s, root));
        roots[root].IoBase = cpu_to_le64(layout->io_base);
        roots[root].IoSize = cpu_to_le64(HP_ZX6000_PCI_IO_SIZE);
        roots[root].IoTranslationOffset = cpu_to_le64(
            HP_ZX6000_LEGACY_IO_BASE);
        roots[root].Mmio32Base = cpu_to_le64(layout->cpu_mmio_base);
        roots[root].Mmio32Size = cpu_to_le64(layout->mmio_size);
        roots[root].Mmio64Base = cpu_to_le64(layout->cpu_mmio64_base);
        roots[root].Mmio64Size = cpu_to_le64(layout->mmio64_size);
        roots[root].DmaBase = cpu_to_le64(0);
        roots[root].DmaSize = cpu_to_le64(low_size);
        roots[root].Rope = cpu_to_le32(ctz32(layout->rope_mask));
        roots[root].BusEnd = layout->last_bus;
        roots[root].Mmio32TranslationOffset = 0;

        sapics[root].Base = cpu_to_le64(
            hp_zx_ioa_address(s, root) +
            IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
        sapics[root].GsiBase = cpu_to_le32(hp_zx6000_gsi_base(root));
        sapics[root].RedirectionEntries = cpu_to_le32(
            !s->rx2660 && root == HP_ZX6000_AGP_ROOT ?
            HP_ZX6000_AGP_INPUT_COUNT :
            HP_ZX6000_PCI_INPUT_COUNT);
        sapics[root].Version = cpu_to_le32(HP_ZX6000_IO_SAPIC_VERSION);
        sapics[root].Id = ctz32(layout->rope_mask);
    }

    if (s->rx2660) {
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_RX2660_CORE_PCI_ROOT,
                            HP_RX2660_MANAGEMENT_SLOT, 0);
        if (s->rv100) {
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_RX2660_CORE_PCI_ROOT,
                                HP_RX2660_RN50_SLOT, 0);
        }
        if (s->ehci) {
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_RX2660_CORE_PCI_ROOT,
                                HP_RX2660_USB_SLOT, 0);
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_RX2660_CORE_PCI_ROOT,
                                HP_RX2660_USB_SLOT, 1);
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_RX2660_CORE_PCI_ROOT,
                                HP_RX2660_USB_SLOT, 2);
        }
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_RX2660_FAST_PCI_ROOT,
                            HP_RX2660_SAS1068_SLOT, 0);
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_RX2660_FAST_PCI_ROOT,
                            HP_RX2660_BCM5704_SLOT, 0);
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_RX2660_FAST_PCI_ROOT,
                            HP_RX2660_BCM5704_SLOT, 1);
    } else {
        if (s->rv100) {
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_ZX6000_AGP_ROOT,
                                HP_ZX6000_RV100_SLOT, 0);
        }
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_ZX6000_CORE_PCI_ROOT,
                            HP_ZX6000_CMD649_SLOT, 0);
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_ZX6000_CORE_PCI_ROOT,
                            HP_ZX6000_I82550_SLOT, 0);
        if (s->ehci) {
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_ZX6000_CORE_PCI_ROOT,
                                HP_ZX6000_OHCI_SLOT, 0);
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_ZX6000_CORE_PCI_ROOT,
                                HP_ZX6000_OHCI_SLOT, 1);
            hp_zx6000_add_route(s, &routes[route_count++],
                                HP_ZX6000_CORE_PCI_ROOT,
                                HP_ZX6000_OHCI_SLOT, 2);
        }
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_ZX6000_SCSI_ROOT,
                            HP_ZX6000_LSI53C1030_SLOT, 0);
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_ZX6000_SCSI_ROOT,
                            HP_ZX6000_LSI53C1030_SLOT, 1);
        hp_zx6000_add_route(s, &routes[route_count++],
                            HP_ZX6000_SCSI_ROOT,
                            HP_ZX6000_BCM5701_SLOT, 0);
    }
    arrays.pci_route_count = route_count;

    return hp_ia64_machine_install_platform_descriptor(hp, &header,
                                                        &arrays, errp);
}

static IA64BootInfo hp_zx6000_boot_info(unsigned int cpu_index,
                                        uint64_t entry,
                                        uint64_t global_pointer,
                                        void *opaque)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(opaque);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_ram_end = hp->descriptor_low_ram_end;
    uint64_t assist_base = low_ram_end - IA64_FW_BOOT_STACK_SIZE;
    bool applied;
    IA64BootInfo info = {
        .firmware_base = IA64_PLATFORM_FIRMWARE_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = HP_ZX6000_IVT_BASE,
        .bsp = assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        .stack_pointer = cpu_index == 0 ? low_ram_end - 16 :
            assist_base + IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_end,
        .io_port_base = HP_ZX6000_LEGACY_IO_BASE,
        .interrupt_block_base = HP_ZX6000_PIB_BASE,
        .powered_off = cpu_index != 0,
        .platform_addresses_valid = true,
    };

    applied = hp_ia64_machine_apply_platform_firmware_args(hp, &info);
    g_assert(applied);
    return info;
}

static IA64BootInfo hp_zx6000_initial_boot_info(unsigned int cpu_index,
                                                void *opaque)
{
    return hp_zx6000_boot_info(cpu_index, IA64_PLATFORM_FIRMWARE_BASE,
                               IA64_PLATFORM_FIRMWARE_BASE, opaque);
}

static IA64BootInfo hp_zx6000_firmware_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    return hp_zx6000_boot_info(cpu_index, entry, global_pointer, opaque);
}

static bool hp_zx6000_build(MachineState *machine, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(machine);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    IA64MachineCpuConfig cpu_config = {
        .alat_full = hp->alat_full,
        .boot_info = hp_zx6000_initial_boot_info,
        .boot_info_opaque = s,
    };

    if (!hp_ia64_machine_validate(hp, errp)) {
        return false;
    }
    if (s->rx2660) {
        if (g_strcmp0(machine->cpu_type,
                      IA64_CPU_TYPE_NAME("montecito-9010")) != 0 &&
            g_strcmp0(machine->cpu_type,
                      IA64_CPU_TYPE_NAME("montecito-9040")) != 0) {
            error_setg(errp, "%s requires a Montecito 9010 or 9040 "
                       "CPU profile", TYPE_HP_RX2660_MACHINE);
            return false;
        }
    } else if (g_strcmp0(machine->cpu_type,
                         IA64_CPU_TYPE_NAME("madison-zx6000")) != 0) {
        error_setg(errp, "%s requires the Madison zx6000 CPU profile",
                   TYPE_HP_ZX6000_MACHINE);
        return false;
    }
    if (!machine->ram || memory_region_size(machine->ram) !=
        machine->ram_size) {
        error_setg(errp, "%s requires machine RAM",
                   s->rx2660 ? TYPE_HP_RX2660_MACHINE :
                               TYPE_HP_ZX6000_MACHINE);
        return false;
    }

    hp_zx6000_map_ram(s);
    ia64_machine_map_pib(OBJECT(s), &hp->pib,
                         s->rx2660 ? "hp-rx2660.pib" : "hp-zx6000.pib",
                         HP_ZX6000_PIB_BASE, HP_ZX6000_PIB_SIZE);
    memory_region_add_subregion(get_system_memory(), HP_ZX6000_LEGACY_IO_BASE,
                                &s->sparse_io);

    if (!hp_zx6000_create_chipset(s, errp) ||
        !hp_zx6000_create_pdh(s, errp) ||
        !hp_zx6000_create_pci_devices(s, errp)) {
        return false;
    }
    hp_zx6000_configure_pci(s);
    if (!hp_zx6000_init_int10(s, errp) ||
        !hp_zx6000_install_descriptor(s, errp) ||
        !ia64_machine_create_cpus(machine, &cpu_config, errp) ||
        !ia64_machine_load_firmware(
            machine, IA64_PLATFORM_FIRMWARE_BASE,
            IA64_PLATFORM_FIRMWARE_SIZE, &hp->firmware_size, errp)) {
        return false;
    }
    ia64_machine_init_firmware_notifier(
        &hp->firmware_notifier, machine, IA64_PLATFORM_FIRMWARE_BASE,
        hp->firmware_size, hp_zx6000_firmware_boot_info,
        hp_zx6000_machine_done, s);
    return true;
}

static void hp_zx6000_machine_init(MachineState *machine)
{
    Error *err = NULL;

    if (!hp_zx6000_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void hp_zx6000_machine_reset(MachineState *machine, ResetType type)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(machine);

    qemu_devices_reset(type);
    hp_zx6000_configure_pci(s);
    hp_ia64_int10_reset(&s->int10);
    ia64_machine_reset_cpus();
}

static char *hp_zx6000_get_nvram(Object *obj, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    (void)errp;
    return g_strdup(s->nvram_path ?: "auto");
}

static void hp_zx6000_set_nvram(Object *obj, const char *value, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    (void)errp;
    g_free(s->nvram_path);
    s->nvram_path = g_strcmp0(value, "auto") == 0 ?
                    NULL : g_strdup(value);
}

static void hp_zx6000_instance_init(Object *obj)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    memory_region_init_io(&s->sparse_io, obj, &hp_zx6000_sparse_io_ops, s,
                          "hp-zx6000.sparse-io",
                          HP_ZX6000_LEGACY_IO_SIZE);
    s->sparse_io.disable_reentrancy_guard = true;
}

static void hp_zx6000_instance_finalize(Object *obj)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);
    unsigned int root;

    hp_ia64_int10_destroy(&s->int10);
    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        if (s->root_io_initialized[root]) {
            address_space_destroy(&s->root_io[root]);
        }
    }
    g_free(s->nvram_path);
}

#ifdef CONFIG_HP_RX2660
static bool hp_rx2660_validate_smp(const MachineState *machine,
                                   Error **errp)
{
    const CpuTopology *smp = &machine->smp;
    bool is_9010 = g_str_equal(machine->cpu_type,
                               IA64_CPU_TYPE_NAME("montecito-9010"));
    bool is_9040 = g_str_equal(machine->cpu_type,
                               IA64_CPU_TYPE_NAME("montecito-9040"));
    unsigned int expected_cores = is_9040 ? 2 : 1;
    unsigned int max_threads = is_9010 ? 1 : 2;

    if (!is_9010 && !is_9040) {
        error_setg(errp, "%s supports only montecito-9010 and "
                   "montecito-9040", TYPE_HP_RX2660_MACHINE);
        return false;
    }
    if (smp->drawers != 1 || smp->books != 1 || smp->dies != 1 ||
        smp->clusters != 1 || smp->modules != 1) {
        error_setg(errp, "%s supports only socket/core/thread topology",
                   TYPE_HP_RX2660_MACHINE);
        return false;
    }
    if (smp->sockets < 1 || smp->sockets > 2 ||
        smp->cores != expected_cores ||
        smp->threads < 1 || smp->threads > max_threads ||
        smp->cpus != smp->sockets * smp->cores * smp->threads ||
        smp->max_cpus != smp->cpus) {
        error_setg(errp, "%s %s requires one or two sockets, %u core%s "
                   "per socket, %s per core, and no CPU "
                   "hotplug headroom", TYPE_HP_RX2660_MACHINE,
                   is_9010 ? "9010" : "9040", expected_cores,
                   expected_cores == 1 ? "" : "s",
                   is_9010 ? "one thread" : "one or two threads");
        return false;
    }
    return true;
}
#endif

static GlobalProperty hp_zx6000_compat_defaults[] = {
    /* Default HID devices omit optional extended-property descriptors. */
    { "usb-kbd", "msos-desc", "off" },
    { "usb-mouse", "msos-desc", "off" },
    { "usb-tablet", "msos-desc", "off" },
};

static void hp_zx6000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "HP zx6000 workstation";
    mc->init = hp_zx6000_machine_init;
    mc->reset = hp_zx6000_machine_reset;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("madison-zx6000");
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "hp-zx6000.ram";
    mc->default_display = "ati";
    mc->default_nic = "i82550";
    mc->default_machine_opts = "firmware=ia64-firmware.bin";
    mc->block_default_type = IF_SCSI;
    mc->block_default_cdrom_type = IF_IDE;
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;

    compat_props_add(mc->compat_props, hp_zx6000_compat_defaults,
                     G_N_ELEMENTS(hp_zx6000_compat_defaults));

    hmc->platform_id = IA64_PLATFORM_ID_HP_ZX6000;
    hmc->minimum_ram_size = HP_ZX6000_MIN_RAM_SIZE;
    hmc->maximum_ram_size = HP_ZX6000_MAX_RAM_SIZE;
    hmc->descriptor_gpa = HP_ZX6000_DESCRIPTOR_GPA;

    object_class_property_add_str(oc, "nvram", hp_zx6000_get_nvram,
                                  hp_zx6000_set_nvram);
    object_class_property_set_description(
        oc, "nvram",
        "Set the HP zx-family NVRAM mode: auto, none, or a file path");
}

static const TypeInfo hp_zx6000_machine_type = {
    .name = TYPE_HP_ZX6000_MACHINE,
    .parent = TYPE_HP_IA64_MACHINE,
    .instance_size = sizeof(HPZX6000MachineState),
    .instance_init = hp_zx6000_instance_init,
    .instance_finalize = hp_zx6000_instance_finalize,
    .class_init = hp_zx6000_machine_class_init,
};

#ifdef CONFIG_HP_RX2660
static void hp_rx2660_instance_init(Object *obj)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    s->rx2660 = true;
}

static void hp_rx2660_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "HP Integrity rx2660 server";
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("montecito-9010");
    mc->max_cpus = 8;
    mc->default_cpus = 1;
    mc->smp_props.prefer_sockets = true;
    mc->default_ram_size = 8 * GiB;
    mc->default_ram_id = "hp-rx2660.ram";
    mc->default_display = "ati";
    mc->default_nic = TYPE_BCM5704;
    mc->block_default_type = IF_SCSI;
    mc->block_default_cdrom_type = IF_SCSI;

    compat_props_add(mc->compat_props, hp_zx6000_compat_defaults,
                     G_N_ELEMENTS(hp_zx6000_compat_defaults));

    hmc->platform_id = IA64_PLATFORM_ID_HP_RX2660;
    hmc->minimum_ram_size = HP_RX2660_MIN_RAM_SIZE;
    hmc->maximum_ram_size = HP_RX2660_MAX_RAM_SIZE;
    hmc->descriptor_gpa = HP_ZX6000_DESCRIPTOR_GPA;
    hmc->validate_smp = hp_rx2660_validate_smp;
}
static const TypeInfo hp_rx2660_machine_type = {
    .name = TYPE_HP_RX2660_MACHINE,
    .parent = TYPE_HP_ZX6000_MACHINE,
    .instance_init = hp_rx2660_instance_init,
    .class_init = hp_rx2660_machine_class_init,
};
#endif

static void hp_zx6000_register_types(void)
{
    unsigned int i;

    type_register_static(&hp_rx2660_management_base_type);
    for (i = 0; i < G_N_ELEMENTS(hp_rx2660_management_types); i++) {
        type_register_static(&hp_rx2660_management_types[i]);
    }
    type_register_static(&hp_zx6000_machine_type);
#ifdef CONFIG_HP_RX2660
    type_register_static(&hp_rx2660_machine_type);
#endif
}

type_init(hp_zx6000_register_types)
