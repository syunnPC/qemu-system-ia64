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
#include "hw/ia64/ia64_iosapic.h"
#include "hw/ia64/ia64_pci.h"
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
#define HP_ZX_MAX_PCI_ROOTS           IA64_PLATFORM_MAX_PCI_ROOTS

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
#define TYPE_HP_ZX_PCIE_TEST_MACHINE   MACHINE_TYPE_NAME("hp-zx-pcie-test")

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

typedef enum HPZX6000RootKind {
    HP_ZX_ROOT_LBA,
    HP_ZX_ROOT_PCIE,
} HPZX6000RootKind;

typedef struct HPZX6000RootLayout {
    uint16_t segment;
    uint64_t config_base;
    uint64_t io_sapic_base;
    uint64_t cpu_mmio_base;
    uint64_t mmio_size;
    uint64_t cpu_mmio64_base;
    uint64_t mmio64_size;
    uint16_t io_base;
    uint8_t first_bus;
    uint8_t last_bus;
    uint16_t rope_mask;
    HPZX6000RootKind kind;
    HPZX1IOAMode mode;
    uint64_t bus_mode;
    uint32_t descriptor_flags;
    uint8_t sapic_entries;
    uint32_t gsi_base;
} HPZX6000RootLayout;

typedef struct HPZX6000CPUProfile {
    const char *type;
    uint8_t cores_per_socket;
    uint8_t max_threads_per_core;
} HPZX6000CPUProfile;

typedef struct HPZX6000MachineProfile HPZX6000MachineProfile;

typedef struct HPZX6000IntxRoute {
    uint8_t root;
    uint8_t slot;
    uint8_t pin;
    uint8_t input;
} HPZX6000IntxRoute;

typedef enum HPZX6000RoutePresence {
    HP_ZX_ROUTE_ALWAYS,
    HP_ZX_ROUTE_VGA,
    HP_ZX_ROUTE_USB,
} HPZX6000RoutePresence;

typedef struct HPZX6000PlatformRoute {
    uint8_t root;
    uint8_t slot;
    uint8_t pin;
    HPZX6000RoutePresence presence;
} HPZX6000PlatformRoute;

typedef struct HPZX6000PciResourceProfile {
    uint32_t vga_bars[3];
    uint32_t ohci_bars[HP_ZX6000_OHCI_FUNCTIONS];
    uint32_t ehci_bar;
    uint32_t ide_bars[5];
    uint32_t storage_bars[HP_ZX6000_LSI_FUNCTIONS][5];
    uint32_t network_bars[HP_RX2660_BCM5704_FUNCTIONS][3];
    uint32_t network_rom_bars[HP_RX2660_BCM5704_FUNCTIONS];
    uint32_t secondary_network_bars[HP_RX2660_BCM5704_FUNCTIONS][2];
    uint32_t management_bars[5];
    uint32_t console_bars[3];
} HPZX6000PciResourceProfile;

typedef bool (*HPZX6000CreateDevices)(HPZX6000MachineState *s,
                                      Error **errp);
typedef void (*HPZX6000ConfigureDevices)(HPZX6000MachineState *s);
typedef bool (*HPZX6000AttachRoot)(HPZX6000MachineState *s,
                                   unsigned int root, Error **errp);

struct HPZX6000MachineProfile {
    const char *machine_type;
    const char *region_prefix;
    const char *pib_region_name;
    const char *int10_region_name;
    const char *mio_type;
    const char *ioa_type;
    uint32_t platform_id;
    uint32_t descriptor_flags;
    uint8_t physical_address_bits;
    uint8_t pci_root_identity;
    const HPZX6000RootLayout *roots;
    unsigned int root_count;
    unsigned int core_root;
    unsigned int vga_root;
    unsigned int vga_slot;
    unsigned int usb_root;
    unsigned int usb_slot;
    unsigned int ide_root;
    unsigned int ide_slot;
    unsigned int storage_root;
    unsigned int storage_slot;
    unsigned int network_root;
    unsigned int network_slot;
    unsigned int secondary_network_root;
    unsigned int secondary_network_slot;
    unsigned int management_root;
    unsigned int management_slot;
    const char *vga_model;
    uint32_t vga_memory_mb;
    uint16_t vga_subsystem_id;
    uint8_t vga_revision;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t vga_rom_size;
    uint32_t vga_rom_bar;
    bool vga_rom_enabled;
    const char *network_romfile;
    uint16_t network_subsystem_id;
    uint8_t network_revision;
    uint16_t secondary_network_subsystem_id;
    uint16_t storage_subsystem_id;
    uint8_t storage_revision;
    unsigned int network_bar_count;
    unsigned int storage_bar_count;
    uint32_t storage_rom_size;
    uint32_t storage_rom_bar;
    const HPZX6000CPUProfile *cpus;
    size_t cpu_count;
    uint8_t max_sockets;
    uint8_t max_cores_per_socket;
    uint8_t max_threads_per_core;
    const char *cpu_requirement;
    const HPZX6000IntxRoute *intx_routes;
    size_t intx_route_count;
    const HPZX6000PlatformRoute *platform_routes;
    size_t platform_route_count;
    const HPZX6000PciResourceProfile *pci_resources;
    HPZX6000CreateDevices create_devices;
    HPZX6000ConfigureDevices configure_devices;
    HPZX6000AttachRoot attach_root;
};

struct HPZX6000MachineState {
    HPIA64MachineState parent_obj;

    MemoryRegion low_ram;
    MemoryRegion high_ram;
    MemoryRegion sparse_io;
    MemoryRegion vga_legacy;
    MemoryRegion root_mmio[HP_ZX_MAX_PCI_ROOTS];
    MemoryRegion root_mmio64[HP_ZX_MAX_PCI_ROOTS];
    AddressSpace root_io[HP_ZX_MAX_PCI_ROOTS];
    bool root_io_initialized[HP_ZX_MAX_PCI_ROOTS];

    HPZX1MIOState *mio;
    HPZX1IOAState *ioa[HP_ZX_MAX_PCI_ROOTS];
    IA64PCIState *pcie_host[HP_ZX_MAX_PCI_ROOTS];
    DeviceState *io_sapic[HP_ZX_MAX_PCI_ROOTS];
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
    const HPZX6000MachineProfile *profile;
};

static const HPZX6000MachineProfile hp_zx6000_profile;
#ifdef CONFIG_HP_RX2660
static const HPZX6000MachineProfile hp_rx2660_profile;
#endif

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
        .gsi_base = 16,
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
        .gsi_base = 27,
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
        .gsi_base = 38,
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
        .gsi_base = 49,
    }, {
        .cpu_mmio_base = UINT64_C(0xa0000000),
        .mmio_size = HP_ZX6000_AGP_MMIO_SIZE,
        .io_base = 0x8000,
        .first_bus = 0x80,
        .last_bus = 0x9f,
        .rope_mask = 0x30,
        .mode = HP_ZX1_IOA_MODE_AGP,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_AGP,
        .descriptor_flags = IA64_PLATFORM_PCI_ROOT_FLAG_AGP,
        .sapic_entries = HP_ZX6000_AGP_INPUT_COUNT,
        .gsi_base = 60,
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
        .gsi_base = 71,
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
        .gsi_base = 16,
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
        .gsi_base = 27,
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
        .gsi_base = 38,
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
        .gsi_base = 49,
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
        .gsi_base = 60,
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(hp_rx2660_roots) ==
                HP_RX2660_PCI_ROOT_COUNT);

static const HPZX6000IntxRoute hp_zx6000_intx_routes[] = {
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 0, 0 },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 1, 1 },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 2, 2 },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 3, 2 },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_CMD649_SLOT, 0, 5 },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_I82550_SLOT, 0, 4 },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 0, 0 },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 1, 1 },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 2, 2 },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 3, 3 },
    { HP_ZX6000_AGP_ROOT, HP_ZX6000_RV100_SLOT, 0, 4 },
};

static const HPZX6000PlatformRoute hp_zx6000_platform_routes[] = {
    { HP_ZX6000_AGP_ROOT, HP_ZX6000_RV100_SLOT, 0, HP_ZX_ROUTE_VGA },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_CMD649_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_I82550_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 0, HP_ZX_ROUTE_USB },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 1, HP_ZX_ROUTE_USB },
    { HP_ZX6000_CORE_PCI_ROOT, HP_ZX6000_OHCI_SLOT, 2, HP_ZX_ROUTE_USB },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_LSI53C1030_SLOT, 1,
      HP_ZX_ROUTE_ALWAYS },
    { HP_ZX6000_SCSI_ROOT, HP_ZX6000_BCM5701_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
};

static const HPZX6000IntxRoute hp_rx2660_intx_routes[] = {
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_MANAGEMENT_SLOT, 0, 0 },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 0, 1 },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 1, 2 },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 2, 3 },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 3, 3 },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_RN50_SLOT, 0, 4 },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_SAS1068_SLOT, 0, 0 },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_SAS1068_SLOT, 1, 1 },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_SAS1068_SLOT, 2, 2 },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_SAS1068_SLOT, 3, 3 },
};

static const HPZX6000PlatformRoute hp_rx2660_platform_routes[] = {
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_MANAGEMENT_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_RN50_SLOT, 0, HP_ZX_ROUTE_VGA },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 0, HP_ZX_ROUTE_USB },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 1, HP_ZX_ROUTE_USB },
    { HP_RX2660_CORE_PCI_ROOT, HP_RX2660_USB_SLOT, 2, HP_ZX_ROUTE_USB },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_SAS1068_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_BCM5704_SLOT, 0,
      HP_ZX_ROUTE_ALWAYS },
    { HP_RX2660_FAST_PCI_ROOT, HP_RX2660_BCM5704_SLOT, 1,
      HP_ZX_ROUTE_ALWAYS },
};

static unsigned int hp_zx_root_count(const HPZX6000MachineState *s)
{
    return s->profile->root_count;
}

static const HPZX6000RootLayout *hp_zx_root_layout(
    const HPZX6000MachineState *s, unsigned int root)
{
    g_assert(root < s->profile->root_count);
    return &s->profile->roots[root];
}

static bool hp_zx_ranges_overlap(uint64_t first_base, uint64_t first_size,
                                 uint64_t second_base, uint64_t second_size)
{
    return first_size && second_size &&
           first_base < second_base + second_size &&
           second_base < first_base + first_size;
}

static bool hp_zx6000_validate_profile(const HPZX6000MachineProfile *profile,
                                       Error **errp)
{
    uint16_t used_ropes = 0;
    bool has_lba = false;
    bool has_pcie = false;
    size_t index;
    unsigned int root;

    if (!profile || !profile->machine_type || !profile->region_prefix ||
        !profile->pib_region_name || !profile->int10_region_name ||
        !profile->mio_type || !profile->ioa_type || !profile->roots ||
        !profile->cpus || !profile->cpu_count || !profile->create_devices ||
        !profile->configure_devices || !profile->pci_resources ||
        !profile->attach_root ||
        !profile->root_count ||
        profile->root_count > HP_ZX_MAX_PCI_ROOTS ||
        profile->core_root >= profile->root_count ||
        profile->vga_root >= profile->root_count ||
        profile->usb_root >= profile->root_count ||
        profile->ide_root >= profile->root_count ||
        profile->storage_root >= profile->root_count ||
        profile->network_root >= profile->root_count ||
        profile->secondary_network_root >= profile->root_count ||
        profile->management_root >= profile->root_count ||
        profile->vga_slot >= PCI_SLOT_MAX ||
        profile->usb_slot >= PCI_SLOT_MAX ||
        profile->ide_slot >= PCI_SLOT_MAX ||
        profile->storage_slot >= PCI_SLOT_MAX ||
        profile->network_slot >= PCI_SLOT_MAX ||
        profile->secondary_network_slot >= PCI_SLOT_MAX ||
        profile->management_slot >= PCI_SLOT_MAX ||
        !profile->network_bar_count || profile->network_bar_count > 3 ||
        !profile->storage_bar_count || profile->storage_bar_count > 5 ||
        !profile->max_sockets || !profile->max_cores_per_socket ||
        !profile->max_threads_per_core ||
        profile->physical_address_bits < 32 ||
        profile->physical_address_bits >= 64 ||
        profile->pci_root_identity >
            IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX ||
        (profile->intx_route_count && !profile->intx_routes) ||
        (profile->platform_route_count && !profile->platform_routes)) {
        error_setg(errp, "invalid HP zx-family machine profile");
        return false;
    }

    for (index = 0; index < profile->cpu_count; index++) {
        if (!profile->cpus[index].type ||
            !profile->cpus[index].cores_per_socket ||
            profile->cpus[index].cores_per_socket >
                profile->max_cores_per_socket ||
            !profile->cpus[index].max_threads_per_core ||
            profile->cpus[index].max_threads_per_core >
                profile->max_threads_per_core) {
            error_setg(errp, "%s has invalid CPU profile %zu",
                       profile->machine_type, index);
            return false;
        }
    }

    for (root = 0; root < profile->root_count; root++) {
        const HPZX6000RootLayout *layout = &profile->roots[root];
        unsigned int entries = layout->sapic_entries ?:
                               HP_ZX6000_PCI_INPUT_COUNT;
        unsigned int other;

        if (!layout->rope_mask || (layout->rope_mask & used_ropes) ||
            layout->kind > HP_ZX_ROOT_PCIE ||
            (layout->kind == HP_ZX_ROOT_LBA &&
             layout->rope_mask > UINT8_MAX) ||
            layout->first_bus > layout->last_bus || !layout->mmio_size ||
            entries > IA64_IOSAPIC_NUM_PINS ||
            (layout->kind == HP_ZX_ROOT_PCIE &&
             (!layout->config_base || !layout->io_sapic_base ||
              (layout->config_base &
               (IA64_PLATFORM_PCI_ECAM_ALIGNMENT - 1U)) != 0 ||
              (layout->io_sapic_base &
               (IA64_PLATFORM_IO_SAPIC_ALIGNMENT - 1U)) != 0 ||
              layout->mmio64_size != 0)) ||
            layout->cpu_mmio_base > UINT64_MAX - layout->mmio_size ||
            (layout->mmio64_size &&
             layout->cpu_mmio64_base > UINT64_MAX - layout->mmio64_size) ||
            layout->gsi_base > UINT32_MAX - entries) {
            error_setg(errp, "%s has invalid PCI root %u",
                       profile->machine_type, root);
            return false;
        }
        has_lba |= layout->kind == HP_ZX_ROOT_LBA;
        has_pcie |= layout->kind == HP_ZX_ROOT_PCIE;
        used_ropes |= layout->rope_mask;

        for (other = 0; other < root; other++) {
            const HPZX6000RootLayout *previous = &profile->roots[other];
            unsigned int previous_entries = previous->sapic_entries ?:
                                            HP_ZX6000_PCI_INPUT_COUNT;

            if (!(layout->last_bus < previous->first_bus ||
                  previous->last_bus < layout->first_bus) ||
                hp_zx_ranges_overlap(layout->io_base,
                                     HP_ZX6000_PCI_IO_SIZE,
                                     previous->io_base,
                                     HP_ZX6000_PCI_IO_SIZE) ||
                hp_zx_ranges_overlap(layout->cpu_mmio_base,
                                     layout->mmio_size,
                                     previous->cpu_mmio_base,
                                     previous->mmio_size) ||
                hp_zx_ranges_overlap(layout->cpu_mmio64_base,
                                     layout->mmio64_size,
                                     previous->cpu_mmio64_base,
                                     previous->mmio64_size) ||
                hp_zx_ranges_overlap(layout->gsi_base, entries,
                                     previous->gsi_base,
                                     previous_entries)) {
                error_setg(errp, "%s PCI roots %u and %u overlap",
                           profile->machine_type, other, root);
                return false;
            }
        }
    }
    if ((has_lba &&
         !(profile->descriptor_flags & IA64_PLATFORM_FLAG_PCI_ZX1_LBA)) ||
        (has_pcie &&
         (!(profile->descriptor_flags & IA64_PLATFORM_FLAG_PCI_ECAM) ||
          (profile->descriptor_flags & IA64_PLATFORM_FLAG_NO_MCFG))) ||
        (!has_pcie &&
         !(profile->descriptor_flags & IA64_PLATFORM_FLAG_NO_MCFG))) {
        error_setg(errp, "%s PCI root backends do not match its descriptor",
                   profile->machine_type);
        return false;
    }

    for (index = 0; index < profile->intx_route_count; index++) {
        const HPZX6000IntxRoute *route = &profile->intx_routes[index];
        unsigned int entries;

        if (route->root >= profile->root_count ||
            route->slot >= PCI_SLOT_MAX || route->pin >= PCI_NUM_PINS) {
            error_setg(errp, "%s has invalid INTx route %zu",
                       profile->machine_type, index);
            return false;
        }
        entries = profile->roots[route->root].sapic_entries ?:
                  HP_ZX6000_PCI_INPUT_COUNT;
        if (route->input >= entries) {
            error_setg(errp, "%s INTx route %zu exceeds root %u inputs",
                       profile->machine_type, index, route->root);
            return false;
        }
    }

    for (index = 0; index < profile->platform_route_count; index++) {
        const HPZX6000PlatformRoute *route =
            &profile->platform_routes[index];

        if (route->root >= profile->root_count ||
            route->slot >= PCI_SLOT_MAX || route->pin >= PCI_NUM_PINS ||
            route->presence > HP_ZX_ROUTE_USB) {
            error_setg(errp, "%s has invalid platform route %zu",
                       profile->machine_type, index);
            return false;
        }
    }
    return true;
}

static hwaddr hp_zx_ioa_address(const HPZX6000MachineState *s,
                                unsigned int root)
{
    return HP_ZX6000_IOA_BASE +
        ctz32(hp_zx_root_layout(s, root)->rope_mask) *
        HP_ZX6000_IOA_STRIDE;
}

static PCIBus *hp_zx_root_bus(HPZX6000MachineState *s, unsigned int root)
{
    return hp_zx_root_layout(s, root)->kind == HP_ZX_ROOT_PCIE ?
        ia64_pci_host_bus(s->pcie_host[root]) :
        hp_zx1_ioa_bus(s->ioa[root]);
}

static MemoryRegion *hp_zx_root_pci_mem(HPZX6000MachineState *s,
                                        unsigned int root)
{
    return hp_zx_root_layout(s, root)->kind == HP_ZX_ROOT_PCIE ?
        ia64_pci_host_memory(s->pcie_host[root]) :
        hp_zx1_ioa_pci_mem(s->ioa[root]);
}

static MemoryRegion *hp_zx_root_pci_io(HPZX6000MachineState *s,
                                       unsigned int root)
{
    return hp_zx_root_layout(s, root)->kind == HP_ZX_ROOT_PCIE ?
        ia64_pci_host_io(s->pcie_host[root]) :
        hp_zx1_ioa_pci_io(s->ioa[root]);
}

static qemu_irq hp_zx_root_irq(HPZX6000MachineState *s, unsigned int root,
                               unsigned int input)
{
    if (hp_zx_root_layout(s, root)->kind == HP_ZX_ROOT_PCIE) {
        return qdev_get_gpio_in(s->io_sapic[root], input);
    }
    return qdev_get_gpio_in_named(DEVICE(s->ioa[root]),
                                  HP_ZX1_IOA_GPIO_INTX, input);
}

static bool hp_zx6000_attach_lba_root(HPZX6000MachineState *s,
                                      unsigned int root, Error **errp)
{
    if (hp_zx_root_layout(s, root)->kind != HP_ZX_ROOT_LBA) {
        error_setg(errp, "%s root %u requires a PCIe attach callback",
                   s->profile->machine_type, root);
        return false;
    }
    return hp_zx1_mio_attach_ioa(s->mio, s->ioa[root], errp);
}

static bool hp_rx2660_attach_root(HPZX6000MachineState *s,
                                  unsigned int root, Error **errp)
{
    const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);

    if (layout->kind == HP_ZX_ROOT_PCIE) {
        return hp_zx2_mio_attach_pci_root(
            s->mio, ia64_pci_host_bus(s->pcie_host[root]),
            layout->rope_mask, errp);
    }
    return hp_zx1_mio_attach_ioa(s->mio, s->ioa[root], errp);
}

static DeviceState *hp_zx6000_add_child(HPZX6000MachineState *s,
                                        const char *name, const char *type)
{
    DeviceState *dev = qdev_new(type);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_unref(OBJECT(dev));
    return dev;
}

static unsigned int hp_zx_route_input(const HPZX6000MachineState *s,
                                      unsigned int root,
                                      unsigned int slot,
                                      unsigned int pin)
{
    const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);
    size_t index;

    for (index = 0; index < s->profile->intx_route_count; index++) {
        const HPZX6000IntxRoute *route = &s->profile->intx_routes[index];

        if (route->root == root && route->slot == slot &&
            route->pin == pin) {
            return route->input;
        }
    }
    return (slot + pin) %
        (layout->sapic_entries ?: HP_ZX6000_PCI_INPUT_COUNT);
}

static uint32_t hp_zx6000_device_gsi(const HPZX6000MachineState *s,
                                     unsigned int root, unsigned int slot,
                                     unsigned int pin)
{
    return hp_zx_root_layout(s, root)->gsi_base +
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
        return &s->root_io[s->profile->vga_root];
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
    g_autofree char *low_name = g_strdup_printf("%s.low-ram",
                                                s->profile->region_prefix);
    g_autofree char *high_name = g_strdup_printf("%s.high-ram",
                                                 s->profile->region_prefix);

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

    dev = hp_zx6000_add_child(s, "mio", s->profile->mio_type);
    s->mio = HP_ZX1_MIO(dev);
    if (!hp_zx1_mio_configure_iommu_reset(s->mio, &iommu_reset, errp) ||
        !hp_zx1_mio_set_fault_notifier(
            s->mio, ia64_ras_hub_report_chipset_fault,
            HP_IA64_MACHINE(s)->ras, errp) ||
        !hp_zx1_mio_set_error_delivery(s->mio,
                                       hp_zx6000_deliver_interrupt,
                                       s, errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, HP_ZX6000_MIO_BASE);

    for (root = 0; root < hp_zx_root_count(s); root++) {
        const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);
        g_autofree char *name = g_strdup_printf("ioa%u", root);
        g_autofree char *mmio_name =
            g_strdup_printf("%s.root%u-mmio",
                            s->profile->region_prefix, root);
        g_autofree char *mmio64_name =
            g_strdup_printf("%s.root%u-mmio64",
                            s->profile->region_prefix, root);
        g_autofree char *io_name =
            g_strdup_printf("%s.root%u-io",
                            s->profile->region_prefix, root);
        unsigned int pin;
        unsigned int slot;

        if (layout->kind == HP_ZX_ROOT_PCIE) {
            IA64PCIHostConfig config = {
                .segment = layout->segment,
                .first_bus = layout->first_bus,
                .last_bus = layout->last_bus,
                .ecam_base = layout->config_base,
                .ecam_size =
                    ((uint64_t)layout->last_bus - layout->first_bus + 1) * MiB,
                .mmio_cpu_base = layout->cpu_mmio_base,
                .mmio_bus_base = layout->cpu_mmio_base,
                .mmio_size = layout->mmio_size,
                .io_cpu_base = HP_ZX6000_LEGACY_IO_BASE +
                    ((uint64_t)layout->io_base << 10),
                .io_bus_base = layout->io_base,
                .io_size = HP_ZX6000_PCI_IO_SIZE,
                .gsi_base = layout->gsi_base,
            };
            g_autofree char *host_name = g_strdup_printf("pcie%u", root);
            g_autofree char *sapic_name =
                g_strdup_printf("pcie-iosapic%u", root);

            dev = hp_zx6000_add_child(
                s, host_name, TYPE_IA64_PCIE_HOST_BRIDGE);
            s->pcie_host[root] = IA64_PCI_HOST_BRIDGE(dev);
            if (!ia64_pci_host_configure(
                    s->pcie_host[root], &config, errp) ||
                !ia64_pcie_host_set_fault_notifier(
                    s->pcie_host[root],
                    ia64_ras_hub_report_chipset_fault,
                    HP_IA64_MACHINE(s)->ras, errp) ||
                !sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
                return false;
            }
            s->io_sapic[root] = hp_zx6000_add_child(
                s, sapic_name, TYPE_IA64_IOSAPIC);
            if (!sysbus_realize(
                    SYS_BUS_DEVICE(s->io_sapic[root]), errp)) {
                return false;
            }
            sysbus_mmio_map(SYS_BUS_DEVICE(s->io_sapic[root]), 0,
                            layout->io_sapic_base);
            for (pin = 0; pin < IA64_PCI_INTX_LINES; pin++) {
                qdev_connect_gpio_out(
                    DEVICE(s->pcie_host[root]), pin,
                    qdev_get_gpio_in(s->io_sapic[root], pin));
            }
            if (!s->profile->attach_root(s, root, errp)) {
                return false;
            }
            address_space_init(&s->root_io[root],
                               ia64_pci_host_io(s->pcie_host[root]), io_name);
            s->root_io_initialized[root] = true;
            continue;
        }

        HPZX1IOASetup setup = {
            .mode = layout->mode,
            .rope_mask = layout->rope_mask,
            .secondary_bus = layout->first_bus,
            .subordinate_bus = layout->last_bus,
            .pci_reset_asserted = false,
            .bus_mode_reset = layout->bus_mode,
            .deliver = hp_zx6000_deliver_interrupt,
            .delivery_opaque = s,
            .fault_notify = ia64_ras_hub_report_chipset_fault,
            .fault_opaque = HP_IA64_MACHINE(s)->ras,
        };

        for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
            for (pin = 0; pin < PCI_NUM_PINS; pin++) {
                setup.intx_route[slot][pin] =
                    hp_zx_route_input(s, root, slot, pin);
            }
        }

        dev = hp_zx6000_add_child(s, name, s->profile->ioa_type);
        s->ioa[root] = HP_ZX1_IOA(dev);
        if (!hp_zx1_ioa_setup(s->ioa[root], &setup, errp) ||
            !sysbus_realize(SYS_BUS_DEVICE(dev), errp) ||
            !s->profile->attach_root(s, root, errp)) {
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
        hp_zx_root_irq(s, s->profile->core_root, 8));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
        hp_zx_root_irq(s, s->profile->core_root, 9));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
        hp_zx_root_irq(s, s->profile->core_root,
                       HP_ZX6000_ACPI_SCI_INPUT));
    for (region = 0; region < G_N_ELEMENTS(bases); region++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), region, bases[region]);
    }

    /* Fixed platform registers must win over an incorrectly assigned BAR. */
    memory_region_add_subregion_overlap(
        hp_zx_root_pci_io(s, s->profile->core_root),
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
    PCIBus *vga = hp_zx_root_bus(s, s->profile->vga_root);
    PCIBus *usb = hp_zx_root_bus(s, s->profile->usb_root);
    PCIBus *storage = hp_zx_root_bus(s, s->profile->storage_root);
    PCIBus *network = hp_zx_root_bus(s, s->profile->network_root);
    PCIBus *management = hp_zx_root_bus(s, s->profile->management_root);
    SCSIBus *scsi_bus;
    BusState *usb_bus;
    unsigned int function;

    s->rv100 = pci_vga_new();
    if (s->rv100) {
        g_autofree char *legacy_name = g_strdup_printf(
            "%s.vga-legacy", s->profile->region_prefix);

        if (!object_dynamic_cast(OBJECT(s->rv100), TYPE_ATI_VGA)) {
            error_setg(errp, "%s supports ATI VGA or no VGA",
                       s->profile->machine_type);
            object_unref(OBJECT(s->rv100));
            s->rv100 = NULL;
            return false;
        }
        s->rv100->devfn = PCI_DEVFN(s->profile->vga_slot, 0);
        qdev_prop_set_string(DEVICE(s->rv100), "model",
                             s->profile->vga_model);
        qdev_prop_set_uint32(DEVICE(s->rv100), "romsize",
                             s->profile->vga_rom_size);
        if (s->profile->vga_memory_mb) {
            qdev_prop_set_uint32(DEVICE(s->rv100), "vgamem_mb",
                                 s->profile->vga_memory_mb);
        }
        if (!hp_zx6000_realize_pci_device(s->rv100, vga, errp)) {
            return false;
        }
        pci_set_byte(s->rv100->config + PCI_REVISION_ID,
                     s->profile->vga_revision);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_HP);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_ID,
                     s->profile->vga_subsystem_id);
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), legacy_name,
            hp_zx_root_pci_mem(s, s->profile->vga_root),
            HP_ZX6000_VGA_LEGACY_BASE, HP_ZX6000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_ZX6000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb) {
        s->ehci = pci_new_multifunction(
            PCI_DEVFN(s->profile->usb_slot, 2), TYPE_NEC_USB_EHCI);
        qdev_prop_set_uint8(DEVICE(s->ehci), "interrupt-pin", 3);
        qdev_prop_set_uint8(DEVICE(s->ehci), "num-ports", 5);
        if (!hp_zx6000_realize_pci_device(s->ehci, usb, errp)) {
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
                PCI_DEVFN(s->profile->usb_slot, function),
                TYPE_NEC_USB_OHCI);
            qdev_prop_set_string(DEVICE(s->ohci[function]), "masterbus",
                                 usb_bus->name);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "firstport",
                                 function * 3);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "num-ports",
                                 function ? 2 : 3);
            qdev_prop_set_uint8(DEVICE(s->ohci[function]),
                                "interrupt-pin", function + 1);
            if (!hp_zx6000_realize_pci_device(s->ohci[function], usb,
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
        PCI_DEVFN(s->profile->storage_slot, 0), TYPE_MPTSAS1068);
    qdev_prop_set_bit(DEVICE(s->lsi53c1030[0]), "x-pci-64bit-bars", true);
    qdev_prop_set_uint32(DEVICE(s->lsi53c1030[0]), "x-pci-rom-size",
                         s->profile->storage_rom_size);
    if (!hp_zx6000_realize_pci_device(s->lsi53c1030[0], storage, errp)) {
        return false;
    }
    pci_set_byte(s->lsi53c1030[0]->config + PCI_REVISION_ID,
                 s->profile->storage_revision);
    pci_set_word(s->lsi53c1030[0]->config + PCI_SUBSYSTEM_VENDOR_ID,
                 PCI_VENDOR_ID_HP);
    pci_set_word(s->lsi53c1030[0]->config + PCI_SUBSYSTEM_ID,
                 s->profile->storage_subsystem_id);
    scsi_bus = mpt_fusion_get_scsi_bus(s->lsi53c1030[0]);
    scsi_bus_legacy_handle_cmdline(scsi_bus);

    for (function = 0; function < HP_RX2660_BCM5704_FUNCTIONS;
         function++) {
        s->bcm5704[function] = pci_new_multifunction(
            PCI_DEVFN(s->profile->network_slot, function), TYPE_BCM5704);
        qemu_configure_nic_device(DEVICE(s->bcm5704[function]), true,
                                  NULL);
        if (!hp_zx6000_realize_pci_device(s->bcm5704[function], network,
                                           errp)) {
            return false;
        }
    }

    for (function = 0; function < G_N_ELEMENTS(management_types);
         function++) {
        s->management[function] = pci_new_multifunction(
            PCI_DEVFN(s->profile->management_slot, function),
            management_types[function]);
        if (function == 2 && serial_hd(2)) {
            qdev_prop_set_chr(DEVICE(s->management[function]), "chardev",
                              serial_hd(2));
        }
        if (!hp_zx6000_realize_pci_device(s->management[function],
                                           management,
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
    PCIBus *vga = hp_zx_root_bus(s, s->profile->vga_root);
    PCIBus *usb = hp_zx_root_bus(s, s->profile->usb_root);
    PCIBus *ide = hp_zx_root_bus(s, s->profile->ide_root);
    PCIBus *storage = hp_zx_root_bus(s, s->profile->storage_root);
    PCIBus *network = hp_zx_root_bus(s, s->profile->network_root);
    PCIBus *secondary_network =
        hp_zx_root_bus(s, s->profile->secondary_network_root);
    DriveInfo *ide_drive;
    SCSIBus *scsi_bus;
    BusState *usb_bus;
    unsigned int channel, function, unit;

    s->rv100 = pci_vga_new();
    if (s->rv100) {
        g_autofree char *legacy_name = g_strdup_printf(
            "%s.vga-legacy", s->profile->region_prefix);

        if (!object_dynamic_cast(OBJECT(s->rv100), TYPE_ATI_VGA)) {
            error_setg(errp, "%s supports ATI VGA or no VGA",
                       s->profile->machine_type);
            object_unref(OBJECT(s->rv100));
            s->rv100 = NULL;
            return false;
        }
        s->rv100->devfn = PCI_DEVFN(s->profile->vga_slot, 0);
        qdev_prop_set_string(DEVICE(s->rv100), "model",
                             s->profile->vga_model);
        qdev_prop_set_uint32(DEVICE(s->rv100), "vgamem_mb",
                             s->profile->vga_memory_mb);
        if (!hp_zx6000_realize_pci_device(s->rv100, vga, errp)) {
            return false;
        }
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_HP);
        pci_set_word(s->rv100->config + PCI_SUBSYSTEM_ID,
                     s->profile->vga_subsystem_id);
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), legacy_name,
            hp_zx_root_pci_mem(s, s->profile->vga_root),
            HP_ZX6000_VGA_LEGACY_BASE, HP_ZX6000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_ZX6000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    s->cmd649 = pci_new(PCI_DEVFN(s->profile->ide_slot, 0),
                        TYPE_CMD649_IDE);
    qdev_prop_set_bit(DEVICE(s->cmd649), "primary-cable80", true);
    if (!hp_zx6000_realize_pci_device(s->cmd649, ide, errp)) {
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

    s->i82550 = pci_new(PCI_DEVFN(s->profile->network_slot, 0), "i82550");
    if (s->profile->network_romfile) {
        qdev_prop_set_string(DEVICE(s->i82550), "romfile",
                             s->profile->network_romfile);
    }
    qdev_prop_set_uint8(DEVICE(s->i82550), "x-pci-revision",
                        s->profile->network_revision);
    qdev_prop_set_uint16(DEVICE(s->i82550),
                         "x-pci-subsystem-vendor-id", PCI_VENDOR_ID_HP);
    qdev_prop_set_uint16(DEVICE(s->i82550),
                         "x-pci-subsystem-id",
                         s->profile->network_subsystem_id);
    qemu_configure_nic_device(DEVICE(s->i82550), true, NULL);
    if (!hp_zx6000_realize_pci_device(s->i82550, network, errp)) {
        return false;
    }

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb) {
        s->ehci = pci_new_multifunction(
            PCI_DEVFN(s->profile->usb_slot, 2),
            TYPE_NEC_USB_EHCI);
        qdev_prop_set_uint8(DEVICE(s->ehci), "interrupt-pin", 3);
        qdev_prop_set_uint8(DEVICE(s->ehci), "num-ports", 5);
        if (!hp_zx6000_realize_pci_device(s->ehci, usb, errp)) {
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
                PCI_DEVFN(s->profile->usb_slot, function),
                TYPE_NEC_USB_OHCI);
            qdev_prop_set_string(DEVICE(s->ohci[function]), "masterbus",
                                 usb_bus->name);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "firstport",
                                 function * 3);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "num-ports",
                                 function ? 2 : 3);
            qdev_prop_set_uint8(DEVICE(s->ohci[function]),
                                "interrupt-pin", function + 1);
            if (!hp_zx6000_realize_pci_device(s->ohci[function], usb,
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
            PCI_DEVFN(s->profile->storage_slot, function),
            TYPE_LSI53C1030);
        if (!hp_zx6000_realize_pci_device(s->lsi53c1030[function], storage,
                                           errp)) {
            return false;
        }
        pci_set_byte(s->lsi53c1030[function]->config + PCI_REVISION_ID,
                     s->profile->storage_revision);
        pci_set_byte(s->lsi53c1030[function]->config + PCI_INTERRUPT_PIN,
                     function + 1);
        scsi_bus = mpt_fusion_get_scsi_bus(s->lsi53c1030[function]);
        scsi_bus_legacy_handle_cmdline(scsi_bus);
    }

    s->bcm5701 = pci_new(
        PCI_DEVFN(s->profile->secondary_network_slot, 0),
                         TYPE_BCM5701);
    qemu_configure_nic_device(DEVICE(s->bcm5701), true, NULL);
    if (!hp_zx6000_realize_pci_device(s->bcm5701, secondary_network,
                                       errp)) {
        return false;
    }
    pci_set_word(s->bcm5701->config + PCI_SUBSYSTEM_VENDOR_ID,
                 PCI_VENDOR_ID_HP);
    pci_set_word(s->bcm5701->config + PCI_SUBSYSTEM_ID,
                 s->profile->secondary_network_subsystem_id);
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
    const HPZX6000PciResourceProfile *resources =
        s->profile->pci_resources;
    unsigned int bar;
    unsigned int function;

    if (s->rv100) {
        for (bar = 0; bar < G_N_ELEMENTS(resources->vga_bars); bar++) {
            hp_zx6000_write_bar(s->rv100, bar,
                                resources->vga_bars[bar]);
        }
        hp_zx6000_write_rom_bar(s->rv100,
                                s->profile->vga_rom_bar |
                                (s->profile->vga_rom_enabled ?
                                 PCI_ROM_ADDRESS_ENABLE : 0));
        hp_zx6000_enable_pci_device(
            s->rv100, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
            PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->vga_root,
                                 s->profile->vga_slot, 0));
    }

    if (s->ehci) {
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
            function++) {
            hp_zx6000_write_bar(s->ohci[function], 0,
                                resources->ohci_bars[function]);
            hp_zx6000_enable_pci_device(
                s->ohci[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                hp_zx6000_device_gsi(s, s->profile->usb_root,
                                     s->profile->usb_slot, function));
        }
        hp_zx6000_write_bar(s->ehci, 0, resources->ehci_bar);
        hp_zx6000_enable_pci_device(
            s->ehci, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->usb_root,
                                 s->profile->usb_slot, 2));
    }

    for (bar = 0; bar < s->profile->storage_bar_count; bar++) {
        hp_zx6000_write_bar(s->lsi53c1030[0], bar,
                            resources->storage_bars[0][bar]);
    }
    hp_zx6000_write_rom_bar(s->lsi53c1030[0],
                            s->profile->storage_rom_bar);
    hp_zx6000_enable_pci_device(
        s->lsi53c1030[0],
        PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, s->profile->storage_root,
                             s->profile->storage_slot, 0));

    for (function = 0; function < HP_RX2660_BCM5704_FUNCTIONS;
         function++) {
        pci_set_byte(s->bcm5704[function]->config + PCI_INTERRUPT_PIN,
                     function + 1);
        for (bar = 0; bar < s->profile->network_bar_count; bar++) {
            hp_zx6000_write_bar(
                s->bcm5704[function], bar,
                resources->network_bars[function][bar]);
        }
        hp_zx6000_write_rom_bar(s->bcm5704[function],
                                resources->network_rom_bars[function]);
        hp_zx6000_enable_pci_device(
            s->bcm5704[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->network_root,
                                 s->profile->network_slot, function));
    }

    for (bar = 1; bar < G_N_ELEMENTS(resources->management_bars); bar++) {
        hp_zx6000_write_bar(s->management[1], bar,
                            resources->management_bars[bar]);
    }
    hp_zx6000_enable_pci_device(
        s->management[1], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 0);

    for (bar = 1; bar < G_N_ELEMENTS(resources->console_bars); bar++) {
        hp_zx6000_write_bar(s->management[2], bar,
                            resources->console_bars[bar]);
    }
    hp_zx6000_enable_pci_device(
        s->management[2], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        0);

}

static void hp_zx6000_configure_pci(HPZX6000MachineState *s)
{
    const HPZX6000PciResourceProfile *resources =
        s->profile->pci_resources;
    unsigned int bar;
    unsigned int function;

    if (s->rv100) {
        for (bar = 0; bar < G_N_ELEMENTS(resources->vga_bars); bar++) {
            hp_zx6000_write_bar(s->rv100, bar,
                                resources->vga_bars[bar]);
        }
        hp_zx6000_write_rom_bar(s->rv100,
                                s->profile->vga_rom_bar |
                                (s->profile->vga_rom_enabled ?
                                 PCI_ROM_ADDRESS_ENABLE : 0));
        hp_zx6000_enable_pci_device(
            s->rv100, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
            PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->vga_root,
                                 s->profile->vga_slot, 0));
    }

    for (bar = 0; bar < G_N_ELEMENTS(resources->ide_bars); bar++) {
        hp_zx6000_write_bar(s->cmd649, bar, resources->ide_bars[bar]);
    }
    hp_zx6000_enable_pci_device(
        s->cmd649, PCI_COMMAND_IO | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, s->profile->ide_root,
                             s->profile->ide_slot, 0));

    for (bar = 0; bar < s->profile->network_bar_count; bar++) {
        hp_zx6000_write_bar(s->i82550, bar,
                            resources->network_bars[0][bar]);
    }
    hp_zx6000_enable_pci_device(
        s->i82550, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, s->profile->network_root,
                             s->profile->network_slot, 0));

    if (s->ehci) {
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
            function++) {
            hp_zx6000_write_bar(s->ohci[function], 0,
                                resources->ohci_bars[function]);
            hp_zx6000_enable_pci_device(
                s->ohci[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                hp_zx6000_device_gsi(s, s->profile->usb_root,
                                     s->profile->usb_slot, function));
        }
        hp_zx6000_write_bar(s->ehci, 0, resources->ehci_bar);
        hp_zx6000_enable_pci_device(
            s->ehci, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->usb_root,
                                 s->profile->usb_slot, 2));
    }

    for (function = 0; function < HP_ZX6000_LSI_FUNCTIONS; function++) {
        for (bar = 0; bar < s->profile->storage_bar_count; bar++) {
            hp_zx6000_write_bar(
                s->lsi53c1030[function], bar,
                resources->storage_bars[function][bar]);
        }
        hp_zx6000_enable_pci_device(
            s->lsi53c1030[function],
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(s, s->profile->storage_root,
                                 s->profile->storage_slot, function));
    }

    for (bar = 0;
         bar < G_N_ELEMENTS(resources->secondary_network_bars[0]); bar++) {
        hp_zx6000_write_bar(
            s->bcm5701, bar, resources->secondary_network_bars[0][bar]);
    }
    hp_zx6000_enable_pci_device(
        s->bcm5701, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(s, s->profile->secondary_network_root,
                             s->profile->secondary_network_slot, 0));
}

static void hp_zx6000_machine_done(void *opaque)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(opaque);

    s->profile->configure_devices(s);
}

static bool hp_zx6000_init_int10(HPZX6000MachineState *s, Error **errp)
{
    HPIA64Int10Config config;
    unsigned int vga_root = s->profile->vga_root;

    if (!s->rv100) {
        return true;
    }

    config = (HPIA64Int10Config) {
        .owner = OBJECT(s),
        .vga = s->rv100,
        .service_io = hp_zx_root_pci_io(s, s->profile->core_root),
        .vga_io = &s->root_io[vga_root],
        .framebuffer_base = s->profile->framebuffer_base,
        .framebuffer_bar = 0,
        .mmio_bar = 2,
        .region_name = s->profile->int10_region_name,
    };
    return hp_ia64_int10_init(&s->int10, &config, errp);
}

static void hp_zx6000_add_route(const HPZX6000MachineState *s,
                                IA64PlatformPciRoute *route,
                                unsigned int root, unsigned int slot,
                                unsigned int pin)
{
    route->Segment = cpu_to_le16(hp_zx_root_layout(s, root)->segment);
    route->Bus = hp_zx_root_layout(s, root)->first_bus;
    route->Device = slot;
    route->Pin = pin;
    route->Gsi = cpu_to_le32(hp_zx6000_device_gsi(s, root, slot, pin));
}

static unsigned int hp_zx6000_populate_routes(
    HPZX6000MachineState *s, IA64PlatformPciRoute *routes)
{
    unsigned int route_count = 0;
    size_t index;

    for (index = 0; index < s->profile->platform_route_count; index++) {
        const HPZX6000PlatformRoute *route =
            &s->profile->platform_routes[index];

        if ((route->presence == HP_ZX_ROUTE_VGA && !s->rv100) ||
            (route->presence == HP_ZX_ROUTE_USB && !s->ehci)) {
            continue;
        }
        hp_zx6000_add_route(s, &routes[route_count++], route->root,
                            route->slot, route->pin);
    }
    return route_count;
}

static void hp_zx6000_add_onboard_device(
    HPZX6000MachineState *s, IA64PlatformDescriptor *descriptor,
    PCIDevice *device, uint8_t type, unsigned int root, uint8_t bar,
    uint64_t bar_size)
{
    IA64PlatformOnboardDevice *entry;
    uint32_t count;

    if (!device) {
        return;
    }
    count = le32_to_cpu(descriptor->OnboardDeviceCount);
    g_assert(count < IA64_PLATFORM_MAX_ONBOARD_DEVICES);
    entry = &descriptor->OnboardDevice[count];
    entry->Segment = cpu_to_le16(hp_zx_root_layout(s, root)->segment);
    entry->Bus = hp_zx_root_layout(s, root)->first_bus;
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
        .PlatformId = cpu_to_le32(s->profile->platform_id),
        .Flags = cpu_to_le32(s->profile->descriptor_flags),
        .RamSize = cpu_to_le64(machine->ram_size),
        .LowRamEnd = cpu_to_le64(low_size),
        .FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE),
        .FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE),
        .ProcessorCount = cpu_to_le32(machine->smp.cpus),
        .SocketCount = cpu_to_le32(machine->smp.sockets),
        .CoresPerSocket = cpu_to_le32(machine->smp.cores),
        .ThreadsPerCore = cpu_to_le32(machine->smp.threads),
        .PhysicalAddressBits = cpu_to_le32(
            s->profile->physical_address_bits),
        .MaxSockets = cpu_to_le32(s->profile->max_sockets),
        .MaxCoresPerSocket = cpu_to_le32(
            s->profile->max_cores_per_socket),
        .MaxThreadsPerCore = cpu_to_le32(
            s->profile->max_threads_per_core),
        .MaxPciRoots = cpu_to_le32(s->profile->root_count),
        .PciRootIdentity = cpu_to_le32(
            s->profile->pci_root_identity),
        .NumaNodeCount = cpu_to_le32(1),
        .LegacyIoBase = cpu_to_le64(HP_ZX6000_LEGACY_IO_BASE),
        .LegacyIoSize = cpu_to_le64(HP_ZX6000_LEGACY_IO_SIZE),
        .LocalSapicBase = cpu_to_le64(HP_ZX6000_PIB_BASE),
        .LocalSapicSize = cpu_to_le64(HP_ZX6000_PIB_SIZE),
        .ConsoleBase = cpu_to_le64(HP_ZX6000_PDH_UART0_BASE),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(
            HP_ZX6000_PDH_UART_INPUT_CLOCK_HZ),
        .ConsoleIrq = cpu_to_le32(
            hp_zx_root_layout(s, s->profile->core_root)->gsi_base + 8),
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
            hp_zx_root_layout(s, s->profile->core_root)->gsi_base +
            HP_ZX6000_ACPI_SCI_INPUT),
        .RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE),
        .RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE),
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
    IA64PlatformPciRoot roots[HP_ZX_MAX_PCI_ROOTS] = { 0 };
    IA64PlatformIoSapic sapics[HP_ZX_MAX_PCI_ROOTS] = { 0 };
    g_autofree IA64PlatformPciRoute *routes = g_new0(
        IA64PlatformPciRoute, s->profile->platform_route_count);
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

    header.ConsoleFlags = cpu_to_le32(
        s->rv100 ? IA64_PLATFORM_CONSOLE_FLAG_VGA_PRIMARY : 0);
    header.NumaNode[0].ProcessorCount = cpu_to_le32(machine->smp.cpus);
    header.NumaNode[0].RamRangeMask = cpu_to_le32(high_size ? 3U : 1U);
    header.NumaNode[0].Distance[0] = 10;
    hp_zx6000_add_onboard_device(
        s, &header, s->rv100, IA64_PLATFORM_ONBOARD_GRAPHICS,
        s->profile->vga_root, 0, s->profile->framebuffer_size);
    for (root = 0; root < HP_ZX6000_OHCI_FUNCTIONS; root++) {
        hp_zx6000_add_onboard_device(
            s, &header, s->ohci[root], IA64_PLATFORM_ONBOARD_OHCI,
            s->profile->usb_root, 0, 0x1000);
    }
    hp_zx6000_add_onboard_device(
        s, &header, s->ehci, IA64_PLATFORM_ONBOARD_EHCI,
        s->profile->usb_root, 0, 0x1000);
    hp_zx6000_add_onboard_device(
        s, &header, s->cmd649, IA64_PLATFORM_ONBOARD_IDE,
        s->profile->ide_root, 4, 0x10);
    for (root = 0; root < HP_ZX6000_LSI_FUNCTIONS; root++) {
        hp_zx6000_add_onboard_device(
            s, &header, s->lsi53c1030[root], IA64_PLATFORM_ONBOARD_MPT,
            s->profile->storage_root, 1, 0x4000);
    }
    hp_zx6000_add_onboard_device(
        s, &header, s->i82550, IA64_PLATFORM_ONBOARD_NETWORK,
        s->profile->network_root, UINT8_MAX, 0);
    hp_zx6000_add_onboard_device(
        s, &header, s->bcm5701, IA64_PLATFORM_ONBOARD_NETWORK,
        s->profile->secondary_network_root, UINT8_MAX, 0);
    for (root = 0; root < HP_RX2660_BCM5704_FUNCTIONS; root++) {
        hp_zx6000_add_onboard_device(
            s, &header, s->bcm5704[root], IA64_PLATFORM_ONBOARD_NETWORK,
            s->profile->network_root, UINT8_MAX, 0);
    }
    for (root = 0; root < G_N_ELEMENTS(s->management); root++) {
        hp_zx6000_add_onboard_device(
            s, &header, s->management[root],
            root == 2 ? IA64_PLATFORM_ONBOARD_UART :
                        IA64_PLATFORM_ONBOARD_MGMT,
            s->profile->management_root, UINT8_MAX, 0);
    }

    if (hp_zx6000_pdh_nvram_persistent(s->pdh)) {
        header.Flags = cpu_to_le32(le32_to_cpu(header.Flags) |
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT);
    }

    for (root = 0; root < hp_zx_root_count(s); root++) {
        const HPZX6000RootLayout *layout = hp_zx_root_layout(s, root);

        roots[root].Segment = cpu_to_le16(layout->segment);
        roots[root].Bus = layout->first_bus;
        roots[root].ConfigType = layout->kind == HP_ZX_ROOT_PCIE ?
            IA64_PLATFORM_PCI_CONFIG_ECAM :
            IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
        roots[root].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
            IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO |
            layout->descriptor_flags |
            (s->rv100 && root == s->profile->vga_root ?
             IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY : 0));
        roots[root].ConfigBase = cpu_to_le64(
            layout->kind == HP_ZX_ROOT_PCIE ? layout->config_base :
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
            layout->kind == HP_ZX_ROOT_LBA ?
            hp_zx_ioa_address(s, root) +
                IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET :
            layout->io_sapic_base);
        sapics[root].GsiBase = cpu_to_le32(layout->gsi_base);
        sapics[root].RedirectionEntries = cpu_to_le32(
            layout->sapic_entries ?: HP_ZX6000_PCI_INPUT_COUNT);
        sapics[root].Version = cpu_to_le32(
            layout->kind == HP_ZX_ROOT_PCIE ? IA64_IOSAPIC_VERSION :
                                               HP_ZX6000_IO_SAPIC_VERSION);
        sapics[root].Id = ctz32(layout->rope_mask);
    }

    route_count = hp_zx6000_populate_routes(s, routes);
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

static const HPZX6000CPUProfile *
hp_zx6000_find_cpu_profile(const HPZX6000MachineProfile *profile,
                           const char *cpu_type)
{
    size_t index;

    for (index = 0; index < profile->cpu_count; index++) {
        if (g_str_equal(cpu_type, profile->cpus[index].type)) {
            return &profile->cpus[index];
        }
    }
    return NULL;
}

static bool hp_zx6000_validate_smp(const MachineState *machine,
                                   Error **errp)
{
    const HPZX6000MachineState *s = HP_ZX6000_MACHINE(machine);
    const HPZX6000CPUProfile *cpu =
        hp_zx6000_find_cpu_profile(s->profile, machine->cpu_type);
    const CpuTopology *smp = &machine->smp;

    if (!cpu) {
        error_setg(errp, "%s requires %s", s->profile->machine_type,
                   s->profile->cpu_requirement);
        return false;
    }
    if (smp->drawers != 1 || smp->books != 1 || smp->dies != 1 ||
        smp->clusters != 1 || smp->modules != 1) {
        error_setg(errp, "%s supports only socket/core/thread topology",
                   s->profile->machine_type);
        return false;
    }
    if (smp->sockets < 1 || smp->sockets > s->profile->max_sockets ||
        smp->cores != cpu->cores_per_socket ||
        smp->threads < 1 || smp->threads > cpu->max_threads_per_core ||
        smp->cpus != smp->sockets * smp->cores * smp->threads ||
        smp->max_cpus != smp->cpus) {
        error_setg(errp, "%s with %s requires one to %u sockets, "
                   "%u core%s per socket, one to %u threads per core, "
                   "and no CPU hotplug headroom",
                   s->profile->machine_type, machine->cpu_type,
                   s->profile->max_sockets, cpu->cores_per_socket,
                   cpu->cores_per_socket == 1 ? "" : "s",
                   cpu->max_threads_per_core);
        return false;
    }
    return true;
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
    if (!hp_zx6000_validate_profile(s->profile, errp)) {
        return false;
    }
    if (!hp_zx6000_find_cpu_profile(s->profile, machine->cpu_type)) {
        error_setg(errp, "%s requires %s", s->profile->machine_type,
                   s->profile->cpu_requirement);
        return false;
    }
    if (!machine->ram || memory_region_size(machine->ram) !=
        machine->ram_size) {
        error_setg(errp, "%s requires machine RAM",
                   s->profile->machine_type);
        return false;
    }

    hp_zx6000_map_ram(s);
    if (!hp_ia64_machine_create_ras(
            hp, IA64_RAS_HUB_DEFAULT_BASE, errp)) {
        return false;
    }
    ia64_machine_map_pib(OBJECT(s), &hp->pib,
                         s->profile->pib_region_name,
                         HP_ZX6000_PIB_BASE, HP_ZX6000_PIB_SIZE);
    memory_region_add_subregion(get_system_memory(), HP_ZX6000_LEGACY_IO_BASE,
                                &s->sparse_io);

    if (!hp_zx6000_create_chipset(s, errp) ||
        !hp_zx6000_create_pdh(s, errp) ||
        !s->profile->create_devices(s, errp)) {
        return false;
    }
    s->profile->configure_devices(s);
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
    s->profile->configure_devices(s);
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

    s->profile = &hp_zx6000_profile;
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
    for (root = 0; root < HP_ZX_MAX_PCI_ROOTS; root++) {
        if (s->root_io_initialized[root]) {
            address_space_destroy(&s->root_io[root]);
        }
    }
    g_free(s->nvram_path);
}

static GlobalProperty hp_zx6000_compat_defaults[] = {
    /* Default HID devices omit optional extended-property descriptors. */
    { "usb-kbd", "msos-desc", "off" },
    { "usb-mouse", "msos-desc", "off" },
    { "usb-tablet", "msos-desc", "off" },
};

static const HPZX6000PciResourceProfile hp_zx6000_pci_resources = {
    .vga_bars = {
        HP_ZX6000_RV100_FB_BAR,
        HP_ZX6000_RV100_IO_BAR,
        HP_ZX6000_RV100_MMIO_BAR,
    },
    .ohci_bars = {
        HP_ZX6000_OHCI0_MMIO_BAR,
        HP_ZX6000_OHCI1_MMIO_BAR,
    },
    .ehci_bar = HP_ZX6000_EHCI_MMIO_BAR,
    .ide_bars = {
        HP_ZX6000_CMD649_DATA_BAR,
        HP_ZX6000_CMD649_CONTROL_BAR,
        HP_ZX6000_CMD649_SECONDARY_DATA_BAR,
        HP_ZX6000_CMD649_SECONDARY_CONTROL_BAR,
        HP_ZX6000_CMD649_BMDMA_BAR,
    },
    .storage_bars = {
        {
            HP_ZX6000_LSI0_IO_BAR,
            HP_ZX6000_LSI0_MMIO_BAR,
            HP_ZX6000_LSI0_DIAG_BAR,
        }, {
            HP_ZX6000_LSI1_IO_BAR,
            HP_ZX6000_LSI1_MMIO_BAR,
            HP_ZX6000_LSI1_DIAG_BAR,
        },
    },
    .network_bars = {
        {
            HP_ZX6000_I82550_MMIO_BAR,
            HP_ZX6000_I82550_IO_BAR,
            HP_ZX6000_I82550_FLASH_BAR,
        },
    },
    .secondary_network_bars = {
        { HP_ZX6000_BCM5701_MMIO_BAR, 0 },
    },
};

static const HPZX6000PciResourceProfile hp_rx2660_pci_resources = {
    .vga_bars = {
        HP_RX2660_RN50_FB_BAR,
        HP_RX2660_RN50_IO_BAR,
        HP_RX2660_RN50_MMIO_BAR,
    },
    .ohci_bars = {
        HP_RX2660_OHCI0_MMIO_BAR,
        HP_RX2660_OHCI1_MMIO_BAR,
    },
    .ehci_bar = HP_RX2660_EHCI_MMIO_BAR,
    .storage_bars = {
        {
            HP_RX2660_SAS_IO_BAR,
            HP_RX2660_SAS_MMIO_BAR,
            0,
            HP_RX2660_SAS_DIAG_BAR,
            0,
        },
    },
    .network_bars = {
        { HP_RX2660_BCM0_MMIO_BAR, 0 },
        { HP_RX2660_BCM1_MMIO_BAR, 0 },
    },
    .network_rom_bars = {
        HP_RX2660_BCM0_ROM_BAR,
        HP_RX2660_BCM1_ROM_BAR,
    },
    .management_bars = {
        0,
        HP_RX2660_MANAGEMENT_MMIO_BAR,
        0,
        (uint32_t)HP_RX2660_MANAGEMENT_MMIO64_BAR,
        (uint32_t)(HP_RX2660_MANAGEMENT_MMIO64_BAR >> 32),
    },
    .console_bars = {
        0,
        HP_RX2660_CONSOLE_MMIO_BAR,
        0,
    },
};

static const HPZX6000CPUProfile hp_zx6000_cpus[] = {
    {
        .type = IA64_CPU_TYPE_NAME("madison-zx6000"),
        .cores_per_socket = 1,
        .max_threads_per_core = 1,
    },
};

static const HPZX6000MachineProfile hp_zx6000_profile = {
    .machine_type = TYPE_HP_ZX6000_MACHINE,
    .region_prefix = "hp-zx6000",
    .pib_region_name = "hp-zx6000.pib",
    .int10_region_name = "hp-zx6000.int10-pci-io",
    .mio_type = TYPE_HP_ZX1_MIO,
    .ioa_type = TYPE_HP_ZX1_IOA,
    .platform_id = IA64_PLATFORM_ID_HP_ZX6000,
    .descriptor_flags = IA64_PLATFORM_FLAG_NO_MCFG |
                        IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                        IA64_PLATFORM_FLAG_IDE_DMA |
                        IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
                        IA64_PLATFORM_FLAG_PCI_ZX1_LBA |
                        IA64_PLATFORM_FLAG_SPARSE_IO |
                        IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC |
                        IA64_PLATFORM_FLAG_ACPI_PM,
    .physical_address_bits = IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
    .pci_root_identity = IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX,
    .roots = hp_zx6000_roots,
    .root_count = G_N_ELEMENTS(hp_zx6000_roots),
    .core_root = HP_ZX6000_CORE_PCI_ROOT,
    .vga_root = HP_ZX6000_AGP_ROOT,
    .vga_slot = HP_ZX6000_RV100_SLOT,
    .usb_root = HP_ZX6000_CORE_PCI_ROOT,
    .usb_slot = HP_ZX6000_OHCI_SLOT,
    .ide_root = HP_ZX6000_CORE_PCI_ROOT,
    .ide_slot = HP_ZX6000_CMD649_SLOT,
    .storage_root = HP_ZX6000_SCSI_ROOT,
    .storage_slot = HP_ZX6000_LSI53C1030_SLOT,
    .network_root = HP_ZX6000_CORE_PCI_ROOT,
    .network_slot = HP_ZX6000_I82550_SLOT,
    .secondary_network_root = HP_ZX6000_SCSI_ROOT,
    .secondary_network_slot = HP_ZX6000_BCM5701_SLOT,
    .management_root = HP_ZX6000_CORE_PCI_ROOT,
    .vga_model = "rv100",
    .vga_memory_mb = 32,
    .vga_subsystem_id = 0x1292,
    .framebuffer_base = HP_ZX6000_RV100_FB_BAR,
    .framebuffer_size = 128 * MiB,
    .vga_rom_bar = HP_ZX6000_RV100_ROM_BAR,
    .vga_rom_enabled = true,
    .network_romfile = "",
    .network_subsystem_id = 0x1274,
    .network_revision = 0x0d,
    .secondary_network_subsystem_id = 0x12a4,
    .storage_revision = 0x07,
    .network_bar_count = 3,
    .storage_bar_count = 3,
    .cpus = hp_zx6000_cpus,
    .cpu_count = G_N_ELEMENTS(hp_zx6000_cpus),
    .max_sockets = 2,
    .max_cores_per_socket = 1,
    .max_threads_per_core = 1,
    .cpu_requirement = "the Madison zx6000 CPU profile",
    .intx_routes = hp_zx6000_intx_routes,
    .intx_route_count = G_N_ELEMENTS(hp_zx6000_intx_routes),
    .platform_routes = hp_zx6000_platform_routes,
    .platform_route_count = G_N_ELEMENTS(hp_zx6000_platform_routes),
    .pci_resources = &hp_zx6000_pci_resources,
    .create_devices = hp_zx6000_create_pci_devices,
    .configure_devices = hp_zx6000_configure_pci,
    .attach_root = hp_zx6000_attach_lba_root,
};

#ifdef CONFIG_HP_RX2660
static const HPZX6000CPUProfile hp_rx2660_cpus[] = {
    {
        .type = IA64_CPU_TYPE_NAME("montecito-9010"),
        .cores_per_socket = 1,
        .max_threads_per_core = 1,
    },
    {
        .type = IA64_CPU_TYPE_NAME("montecito-9020"),
        .cores_per_socket = 2,
        .max_threads_per_core = 2,
    },
    {
        .type = IA64_CPU_TYPE_NAME("montecito-9040"),
        .cores_per_socket = 2,
        .max_threads_per_core = 2,
    },
    {
        .type = IA64_CPU_TYPE_NAME("montvale-9110n"),
        .cores_per_socket = 1,
        .max_threads_per_core = 1,
    },
    {
        .type = IA64_CPU_TYPE_NAME("montvale-9120n"),
        .cores_per_socket = 2,
        .max_threads_per_core = 2,
    },
    {
        .type = IA64_CPU_TYPE_NAME("montvale-9140m"),
        .cores_per_socket = 2,
        .max_threads_per_core = 2,
    },
};

static const HPZX6000MachineProfile hp_rx2660_profile = {
    .machine_type = TYPE_HP_RX2660_MACHINE,
    .region_prefix = "hp-rx2660",
    .pib_region_name = "hp-rx2660.pib",
    .int10_region_name = "hp-rx2660.int10-pci-io",
    .mio_type = TYPE_HP_ZX2_MIO,
    .ioa_type = TYPE_HP_ZX1_IOA,
    .platform_id = IA64_PLATFORM_ID_HP_RX2660,
    .descriptor_flags = IA64_PLATFORM_FLAG_NO_MCFG |
                        IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                        IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
                        IA64_PLATFORM_FLAG_PCI_ZX1_LBA |
                        IA64_PLATFORM_FLAG_SPARSE_IO |
                        IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC |
                        IA64_PLATFORM_FLAG_ACPI_PM,
    .physical_address_bits = IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
    .pci_root_identity = IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX,
    .roots = hp_rx2660_roots,
    .root_count = G_N_ELEMENTS(hp_rx2660_roots),
    .core_root = HP_RX2660_CORE_PCI_ROOT,
    .vga_root = HP_RX2660_CORE_PCI_ROOT,
    .vga_slot = HP_RX2660_RN50_SLOT,
    .usb_root = HP_RX2660_CORE_PCI_ROOT,
    .usb_slot = HP_RX2660_USB_SLOT,
    .ide_root = HP_RX2660_CORE_PCI_ROOT,
    .storage_root = HP_RX2660_FAST_PCI_ROOT,
    .storage_slot = HP_RX2660_SAS1068_SLOT,
    .network_root = HP_RX2660_FAST_PCI_ROOT,
    .network_slot = HP_RX2660_BCM5704_SLOT,
    .secondary_network_root = HP_RX2660_FAST_PCI_ROOT,
    .secondary_network_slot = HP_RX2660_BCM5704_SLOT,
    .management_root = HP_RX2660_CORE_PCI_ROOT,
    .management_slot = HP_RX2660_MANAGEMENT_SLOT,
    .vga_model = "es1000",
    .vga_subsystem_id = 0x1304,
    .vga_revision = 0x02,
    .framebuffer_base = HP_RX2660_RN50_FB_BAR,
    .framebuffer_size = 128 * MiB,
    .vga_rom_size = 128 * KiB,
    .vga_rom_bar = HP_RX2660_RN50_ROM_BAR,
    .network_bar_count = 2,
    .storage_bar_count = 5,
    .storage_rom_size = 4 * MiB,
    .storage_rom_bar = HP_RX2660_SAS_ROM_BAR,
    .storage_subsystem_id = 0x1312,
    .storage_revision = 0x01,
    .cpus = hp_rx2660_cpus,
    .cpu_count = G_N_ELEMENTS(hp_rx2660_cpus),
    .max_sockets = 2,
    .max_cores_per_socket = 2,
    .max_threads_per_core = 2,
    .cpu_requirement = "a supported Montecito or Montvale CPU profile",
    .intx_routes = hp_rx2660_intx_routes,
    .intx_route_count = G_N_ELEMENTS(hp_rx2660_intx_routes),
    .platform_routes = hp_rx2660_platform_routes,
    .platform_route_count = G_N_ELEMENTS(hp_rx2660_platform_routes),
    .pci_resources = &hp_rx2660_pci_resources,
    .create_devices = hp_rx2660_create_pci_devices,
    .configure_devices = hp_rx2660_configure_pci,
    .attach_root = hp_rx2660_attach_root,
};

#ifdef CONFIG_TEST_DEVICES
static const HPZX6000RootLayout hp_zx_pcie_test_roots[] = {
    {
        .config_base = UINT64_C(0x0000000500000000),
        .io_sapic_base = UINT64_C(0x00000000fed00000),
        .cpu_mmio_base = UINT64_C(0x00000000c0000000),
        .mmio_size = UINT64_C(0x10000000),
        .io_base = 0,
        .first_bus = 0x20,
        .last_bus = 0x2f,
        .rope_mask = 0x0001,
        .kind = HP_ZX_ROOT_PCIE,
        .sapic_entries = IA64_IOSAPIC_NUM_PINS,
        .gsi_base = 16,
    },
};

static bool hp_zx_pcie_test_create_devices(HPZX6000MachineState *s,
                                            Error **errp)
{
    (void)s;
    (void)errp;
    return true;
}

static void hp_zx_pcie_test_configure_devices(HPZX6000MachineState *s)
{
    (void)s;
}

static const HPZX6000PciResourceProfile hp_zx_pcie_test_resources;

static const HPZX6000MachineProfile hp_zx_pcie_test_profile = {
    .machine_type = TYPE_HP_ZX_PCIE_TEST_MACHINE,
    .region_prefix = "hp-zx-pcie-test",
    .pib_region_name = "hp-zx-pcie-test.pib",
    .int10_region_name = "hp-zx-pcie-test.int10-pci-io",
    .mio_type = TYPE_HP_ZX2_MIO,
    .ioa_type = TYPE_HP_ZX1_IOA,
    .platform_id = IA64_PLATFORM_ID_HP_RX2660,
    .descriptor_flags = IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                        IA64_PLATFORM_FLAG_FAMILY_HP_ZX |
                        IA64_PLATFORM_FLAG_PCI_ECAM |
                        IA64_PLATFORM_FLAG_SPARSE_IO |
                        IA64_PLATFORM_FLAG_ACPI_PM,
    .physical_address_bits = IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS,
    .pci_root_identity = IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX,
    .roots = hp_zx_pcie_test_roots,
    .root_count = G_N_ELEMENTS(hp_zx_pcie_test_roots),
    .core_root = 0,
    .vga_root = 0,
    .usb_root = 0,
    .ide_root = 0,
    .storage_root = 0,
    .network_root = 0,
    .secondary_network_root = 0,
    .management_root = 0,
    .network_bar_count = 1,
    .storage_bar_count = 1,
    .cpus = hp_rx2660_cpus,
    .cpu_count = G_N_ELEMENTS(hp_rx2660_cpus),
    .max_sockets = 2,
    .max_cores_per_socket = 2,
    .max_threads_per_core = 2,
    .cpu_requirement = "a supported Montecito or Montvale CPU profile",
    .pci_resources = &hp_zx_pcie_test_resources,
    .create_devices = hp_zx_pcie_test_create_devices,
    .configure_devices = hp_zx_pcie_test_configure_devices,
    .attach_root = hp_rx2660_attach_root,
};
#endif
#endif

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
    hmc->validate_smp = hp_zx6000_validate_smp;

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

    s->profile = &hp_rx2660_profile;
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
    hmc->validate_smp = hp_zx6000_validate_smp;
}
static const TypeInfo hp_rx2660_machine_type = {
    .name = TYPE_HP_RX2660_MACHINE,
    .parent = TYPE_HP_ZX6000_MACHINE,
    .instance_init = hp_rx2660_instance_init,
    .class_init = hp_rx2660_machine_class_init,
};

#ifdef CONFIG_TEST_DEVICES
static void hp_zx_pcie_test_instance_init(Object *obj)
{
    HP_ZX6000_MACHINE(obj)->profile = &hp_zx_pcie_test_profile;
}

static void hp_zx_pcie_test_machine_class_init(ObjectClass *oc,
                                                const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "HP zx-family PCIe profile test machine";
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("montecito-9010");
    mc->max_cpus = 8;
    mc->default_cpus = 1;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "hp-zx-pcie-test.ram";
    mc->default_nic = NULL;
    hmc->platform_id = IA64_PLATFORM_ID_HP_RX2660;
    hmc->minimum_ram_size = HP_RX2660_MIN_RAM_SIZE;
    hmc->maximum_ram_size = HP_RX2660_MAX_RAM_SIZE;
    hmc->descriptor_gpa = HP_ZX6000_DESCRIPTOR_GPA;
    hmc->validate_smp = hp_zx6000_validate_smp;
}

static const TypeInfo hp_zx_pcie_test_machine_type = {
    .name = TYPE_HP_ZX_PCIE_TEST_MACHINE,
    .parent = TYPE_HP_ZX6000_MACHINE,
    .instance_init = hp_zx_pcie_test_instance_init,
    .class_init = hp_zx_pcie_test_machine_class_init,
};
#endif
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
#ifdef CONFIG_TEST_DEVICES
    type_register_static(&hp_zx_pcie_test_machine_type);
#endif
#endif
}

type_init(hp_zx6000_register_types)
