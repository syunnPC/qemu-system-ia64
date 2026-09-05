/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 virtual PC platform.
 */

#include "qemu/osdep.h"

#include CONFIG_DEVICES

#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "hw/core/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "hw/display/bochs-vbe.h"
#include "hw/display/edid.h"
#include "hw/display/vga_regs.h"
#include "hw/core/sysbus.h"
#include "hw/ide/ahci-pci.h"
#include "hw/ide/ide-dev.h"
#include "hw/input/i8042.h"
#include "hw/acpi/acpi.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "net/net.h"
#include "hw/isa/isa.h"
#include "hw/usb/hcd-uhci.h"
#include "hw/usb/usb.h"
#include "hw/ia64/ia64_common.h"
#include "hw/ia64/ia64_pci.h"
#include "hw/ia64/ia64_iosapic.h"
#include "hw/ia64/ia64_ras.h"
#ifdef CONFIG_IA64_460GX_HOST
#include "hw/ia64/intel_460gx_host.h"
#endif
#ifdef CONFIG_IA64_460GX_DMA_TEST
#include "hw/ia64/intel_460gx_dma_test.h"
#endif
#ifdef CONFIG_IA64_460GX_PID
#include "hw/ia64/intel_460gx_pid.h"
#endif
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/rtc.h"
#include "system/runstate.h"
#include "system/system.h"
#include "system/reset.h"
#include "system/qtest.h"
#include "system/watchdog.h"
#include "target/ia64/cpu-qom.h"
#include "target/ia64/cpu.h"

#define IA64_FW_BASE    0x0000000000100000ULL
#define IA64_LOW_RAM_LIMIT 0x0000000080000000ULL
#define IA64_HIGH_RAM_BASE 0x0000000080200000ULL
#define IA64_FIRMWARE_ADDRESS_SPACE_BASE 0x00000000ff000000ULL
#define IA64_FIRMWARE_ADDRESS_SPACE_SIZE (16 * MiB)
#define IA64_RTC_BASE 0x00000000ffef0000ULL
#define IA64_RTC_SIZE (8 * KiB)
#define IA64_WATCHDOG_BASE 0x00000000ffee0000ULL
#define IA64_WATCHDOG_SIZE (4 * KiB)
#define IA64_WATCHDOG_TIMEOUT 0x00
#define IA64_WATCHDOG_CODE    0x08
#define IA64_NVRAM_BASE 0x00000000fff00000ULL
#define IA64_NVRAM_SIZE (64 * KiB)
#define IA64_NVRAM_EXTENDED_FILE_SIZE (512 * KiB)
#define IA64_NVRAM_COMMIT_OFFSET (IA64_NVRAM_SIZE - 8)
#define IA64_NVRAM_COMMIT_MAGIC 0x54494d4d4f43564eULL /* "NVCOMMIT" */
#define IA64_HIGH_RAM_AFTER_FIRMWARE_BASE \
    (IA64_FIRMWARE_ADDRESS_SPACE_BASE + IA64_FIRMWARE_ADDRESS_SPACE_SIZE)
#define IA64_VPC_MAX_RAM_SIZE \
    (IA64_LOW_RAM_LIMIT + \
     (IA64_PCI_MMIO_BASE - IA64_HIGH_RAM_BASE) + \
     (IA64_LOCAL_SAPIC_PA - \
      (IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE) - \
      IA64_RAS_HUB_SIZE) + \
     (IA64_PCI_IO_BASE - IA64_HIGH_RAM_AFTER_FIRMWARE_BASE))
#define IA64_FW_LOW_RAM_MIN IA64_FW_MIN_LOW_RAM_SIZE
#define IA64_IVT_BASE   0x10000ULL
#define IA64_IVT_SIZE   0x8000ULL
#define IA64_AHCI_IDP_IO_BASE   0x0000c100U
#define IA64_UHCI_IO_BASE       0x0000c120U
/* LSI BAR0 is 0x100 bytes and therefore requires 0x100-byte alignment. */
#define IA64_LSI_IO_BASE        0x0000c200U
#define IA64_VGA_IO_BASE        0x0000c300U
#define IA64_E1000_IO_BASE      0x0000c400U
#define IA64_OHCI_MMIO_PCI_BASE (IA64_PCI_MMIO_BASE + 0x00010000ULL)
#define IA64_AHCI_MMIO_PCI_BASE (IA64_PCI_MMIO_BASE + 0x00020000ULL)
#define IA64_LSI_MMIO_PCI_BASE  (IA64_PCI_MMIO_BASE + 0x00030000ULL)
#define IA64_LSI_RAM_PCI_BASE   (IA64_PCI_MMIO_BASE + 0x00032000ULL)
#define IA64_E1000_MMIO_PCI_BASE (IA64_PCI_MMIO_BASE + 0x00040000ULL)
#define IA64_E1000_MMIO_SIZE    0x00020000ULL
#define IA64_E1000_IO_SIZE      0x00000040U
#define IA64_VGA_FB_PCI_BASE    0x00000000c4000000ULL
#define IA64_VGA_MMIO_PCI_BASE  0x00000000c8000000ULL
#define IA64_VGA_LARGE_FB_PCI_BASE   0x00000000c8000000ULL
#define IA64_VGA_LARGE_MMIO_PCI_BASE 0x00000000d0000000ULL
#define IA64_VGA_LEGACY_BASE   0x000a0000U
#define IA64_VGA_LEGACY_SIZE   0x00020000U
#ifdef CONFIG_IA64_VPC_GRAPHICS
#define IA64_INT10_ROM_BASE     0x000c0000U
#define IA64_INT10_ROM_SIZE     0x00000800U
#define IA64_INT10_ROM_PCIR_OFFSET    0x0020U
#define IA64_INT10_ROM_ATI_SIGNATURE_OFFSET 0x0074U
#define IA64_INT10_ROM_ATI_HEADER_OFFSET 0x0080U
#define IA64_INT10_ROM_ATI_HEADER_SIZE 0x0060U
#define IA64_INT10_ROM_ATI_RAGE128_HEADER_SIZE 0x004aU
#define IA64_INT10_ROM_ATI_INIT_OFFSET 0x00e0U
#define IA64_INT10_ROM_ATI_INIT_READ_SIZE 10U
#define IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET 0x00f0U
#define IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE 12U
#define IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET 0x00f0U
#define IA64_INT10_ROM_ATI_RAGE128_MISC_SIZE 15U
#define IA64_INT10_ROM_ATI_MISC_OFFSET 0x00fcU
#define IA64_INT10_ROM_ATI_MISC_SIZE 2U
#define IA64_INT10_ROM_HANDLER_OFFSET 0x0100U
#define IA64_INT10_ROM_OEM_OFFSET     0x0180U
#define IA64_INT10_ROM_VENDOR_OFFSET  0x0190U
#define IA64_INT10_ROM_PRODUCT_OFFSET 0x01a0U
#define IA64_INT10_ROM_REVISION_OFFSET 0x01c0U
#define IA64_INT10_ROM_MODES_OFFSET   0x01d0U
#define IA64_INT10_ROM_ATI_CONNECTOR_OFFSET 0x02e0U
#define IA64_INT10_ROM_ATI_CONNECTOR_SIZE 6U
#define IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET 0x02e0U
#define IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE 30U
#define IA64_INT10_ROM_ATI_PLL_OFFSET 0x0300U
#define IA64_INT10_ROM_ATI_PLL_READ_SIZE 0x006eU
#define IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET 0x0383U
#define IA64_INT10_ROM_ATI_MEM_PREFIX_SIZE 3U
#define IA64_INT10_ROM_ATI_MEM_RESET_OFFSET 3U
#define IA64_INT10_ROM_ATI_MEM_RESET_SIZE 100U
#define IA64_INT10_VECTOR_ADDR  (0x10U * 4U)
#define IA64_INT10_IO_BASE      0x000001e0U
#define IA64_INT10_IO_SIZE      0x00000010U
#define IA64_INT10_TRIGGER      0x4941U
#define IA64_VBE2_SIGNATURE     0x32454256U
#define IA64_VBE_IO_INDEX       0x01ceU
#define IA64_VBE_IO_DATA        0x01d0U
#define IA64_VBE_DEFAULT_XRES   1280U
#define IA64_VBE_DEFAULT_YRES   1024U
#define IA64_VBE_MAX_MODES      96U
#define IA64_VBE_NATIVE_MODE_16 0x1f0U
#define IA64_VBE_NATIVE_MODE_24 0x1f1U
#define IA64_VBE_NATIVE_MODE_32 0x1f2U
#define IA64_VGA_FIXED_FB_SIZE  (128 * MiB)
#define IA64_VGA_PLANAR_MEMORY_SIZE (256 * KiB)
#define IA64_BDA_VIDEO_MODE      0x00000449U
#define IA64_BDA_VIDEO_COLUMNS   0x0000044aU
#define IA64_BDA_VIDEO_PAGE_SIZE 0x0000044cU
#define IA64_BDA_VIDEO_PAGE_START 0x0000044eU
#define IA64_BDA_CURSOR_POSITIONS 0x00000450U
#define IA64_BDA_CURSOR_TYPE     0x00000460U
#define IA64_BDA_VIDEO_PAGE      0x00000462U
#define IA64_BDA_CRTC_ADDRESS    0x00000463U
#define IA64_BDA_VIDEO_ROWS      0x00000484U
#define IA64_BDA_CHARACTER_HEIGHT 0x00000485U
#define IA64_BDA_VIDEO_CONTROL   0x00000487U
#define IA64_BDA_VIDEO_SWITCHES  0x00000488U
#define IA64_ATI_VENDOR_ID        0x1002U
#define IA64_ATI_RAGE128_DEVICE_ID 0x5046U
#define IA64_ATI_ES1000_DEVICE_ID 0x515eU
#endif
#define IA64_IOSAPIC_BASE       0x0000000080110000ULL
#define IA64_IOSAPIC_SIZE       0x0000000000002000ULL
#define IA64_ACPI_PM_IO_BASE    0x00002000U
#define IA64_ACPI_PM_IO_SIZE    0x00000010U
#define IA64_ACPI_PM_RESET_OFFSET 0x0000000cU
#define IA64_ACPI_PM_RESET_VALUE  0x01U
#define IA64_ACPI_SCI_IRQ       9
#define IA64_VPC_NIC_SLOT           6

#ifdef CONFIG_IA64_VPC_GRAPHICS
enum {
    IA64_INT10_REG_AX,
    IA64_INT10_REG_BX,
    IA64_INT10_REG_CX,
    IA64_INT10_REG_DX,
    IA64_INT10_REG_DI,
    IA64_INT10_REG_ES,
    IA64_INT10_REG_EXEC,
    IA64_INT10_REG_DATA,
};

typedef struct IA64Int10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
} IA64Int10Registers;

typedef struct IA64VbeMode {
    uint16_t number;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} IA64VbeMode;

typedef struct IA64VbeResolution {
    uint16_t base_number;
    uint16_t width;
    uint16_t height;
} IA64VbeResolution;

typedef struct IA64VgaLegacyMode {
    uint8_t number;
    uint8_t columns;
    uint8_t rows;
    uint8_t character_height;
    uint16_t page_size;
    uint8_t misc;
    const uint8_t *sequencer;
    const uint8_t *crtc;
    const uint8_t *attribute;
    const uint8_t *graphics;
} IA64VgaLegacyMode;

static const IA64VbeMode ia64_vbe_legacy_modes[] = {
    { 0x111,  640,  480, 16 },
    { 0x112,  640,  480, 24 },
    { 0x114,  800,  600, 16 },
    { 0x115,  800,  600, 24 },
    { 0x117, 1024,  768, 16 },
    { 0x118, 1024,  768, 24 },
    { 0x11a, 1280, 1024, 16 },
    { 0x11b, 1280, 1024, 24 },
    { 0x141,  640,  400, 32 },
    { 0x142,  640,  480, 32 },
    { 0x143,  800,  600, 32 },
    { 0x144, 1024,  768, 32 },
    { 0x145, 1280, 1024, 32 },
};

/*
 * Additional QEMU OEM modes.  Mode numbers are stable even when a configured
 * maximum resolution filters entries out of the option-ROM mode list.
 */
static const IA64VbeResolution ia64_vbe_oem_resolutions[] = {
    { 0x146, 1152,  864 },
    { 0x149, 1280,  720 },
    { 0x14c, 1280,  768 },
    { 0x14f, 1280,  800 },
    { 0x152, 1280,  960 },
    { 0x155, 1360,  768 },
    { 0x158, 1400, 1050 },
    { 0x15b, 1440,  900 },
    { 0x15e, 1600,  900 },
    { 0x161, 1600, 1200 },
    { 0x164, 1680, 1050 },
    { 0x167, 1920, 1080 },
    { 0x16a, 1920, 1200 },
    { 0x16d, 2048, 1152 },
    { 0x170, 2048, 1536 },
    { 0x173, 2560, 1080 },
    { 0x176, 2560, 1440 },
    { 0x179, 2560, 1600 },
    { 0x17c, 3440, 1440 },
    { 0x17f, 3840, 2160 },
    { 0x182, 4096, 2160 },
    { 0x185, 5120, 2880 },
};

/* Standard VGA BIOS mode 12h: 640x480, 16-color planar graphics. */
static const uint8_t ia64_vga_mode_12_sequencer[] = {
    0x01, 0x0f, 0x00, 0x06,
};

static const uint8_t ia64_vga_mode_12_crtc[] = {
    0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
    0xff,
};

static const uint8_t ia64_vga_mode_12_attribute[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x01, 0x00, 0x0f, 0x00, 0x00,
};

static const uint8_t ia64_vga_mode_12_graphics[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f,
    0xff,
};

static const IA64VgaLegacyMode ia64_vga_legacy_modes[] = {
    {
        .number = 0x12,
        .columns = 80,
        .rows = 30,
        .character_height = 16,
        .page_size = 0xa000,
        .misc = 0xe3,
        .sequencer = ia64_vga_mode_12_sequencer,
        .crtc = ia64_vga_mode_12_crtc,
        .attribute = ia64_vga_mode_12_attribute,
        .graphics = ia64_vga_mode_12_graphics,
    },
};

static const char ia64_vbe_oem[] = "QEMU IA64 VBE";
static const char ia64_vbe_vendor[] = "QEMU";
static const char ia64_vbe_product[] = "IA64 VGA VBE bridge";
static const char ia64_vbe_revision[] = "1.0";

/*
 * The real-mode INT 10h entry marshals the registers through the private
 * I/O window above.  Keeping the executable stub small is intentional: the
 * VBE implementation remains normal, testable C code, and the stub also
 * works when the guest uses a software x86 BIOS emulator instead of native
 * IA-32 execution.  The bytes below are 16-bit code equivalent to:
 *
 *     push bp                 ; save registers not returned by VBE
 *     mov  bp, sp
 *     push ax
 *     push dx
 *     mov  dx, 1e0h
 *     out  dx, ax
 *     add  dx, 2
 *     mov  ax, bx
 *     out  dx, ax
 *     add  dx, 2
 *     mov  ax, cx
 *     out  dx, ax
 *     add  dx, 2
 *     mov  ax, [bp-4]
 *     out  dx, ax
 *     add  dx, 2
 *     mov  ax, di
 *     out  dx, ax
 *     add  dx, 2
 *     mov  ax, es
 *     out  dx, ax
 *     add  dx, 2
 *     cmp  word [bp-2], 4f00h
 *     jne  execute
 *     add  dx, 2
 *     mov  ax, es:[di]
 *     out  dx, ax
 *     mov  ax, es:[di+2]
 *     out  dx, ax             ; pass the VBE2 input signature
 *     sub  dx, 2
 * execute:
 *     mov  ax, 4941h
 *     out  dx, ax             ; execute the request at 1ech
 *     in   ax, dx
 *     mov  cx, ax
 *     jcxz response_done
 *     push di
 *     add  dx, 2
 *     cld
 * response_loop:
 *     in   ax, dx
 *     stosw
 *     loop response_loop
 *     pop  di
 * response_done:
 *     mov  dx, 1e0h
 *     in   ax, dx
 *     mov  [bp-2], ax
 *     add  dx, 2
 *     in   ax, dx
 *     mov  bx, ax
 *     add  dx, 2
 *     in   ax, dx
 *     mov  cx, ax
 *     add  dx, 2
 *     in   ax, dx
 *     mov  dx, ax
 *     mov  ax, [bp-2]
 *     mov  sp, bp
 *     pop  bp
 *     iret
 */
static const uint8_t ia64_int10_handler[] = {
    0x55, 0x89, 0xe5, 0x50, 0x52, 0xba, 0xe0, 0x01,
    0xef, 0x83, 0xc2, 0x02, 0x89, 0xd8, 0xef, 0x83,
    0xc2, 0x02, 0x89, 0xc8, 0xef, 0x83, 0xc2, 0x02,
    0x8b, 0x46, 0xfc, 0xef, 0x83, 0xc2, 0x02, 0x89,
    0xf8, 0xef, 0x83, 0xc2, 0x02, 0x8c, 0xc0, 0xef,
    0x83, 0xc2, 0x02, 0x81, 0x7e, 0xfe, 0x00, 0x4f,
    0x75, 0x0f, 0x83, 0xc2, 0x02, 0x26, 0x8b, 0x05,
    0xef, 0x26, 0x8b, 0x45, 0x02, 0xef, 0x83, 0xea,
    0x02, 0xb8, 0x41, 0x49, 0xef, 0xed, 0x89, 0xc1,
    0xe3, 0x0a, 0x57, 0x83, 0xc2, 0x02, 0xfc, 0xed,
    0xab, 0xe2, 0xfc, 0x5f, 0xba, 0xe0, 0x01, 0xed,
    0x89, 0x46, 0xfe, 0x83, 0xc2, 0x02, 0xed, 0x89,
    0xc3, 0x83, 0xc2, 0x02, 0xed, 0x89, 0xc1, 0x83,
    0xc2, 0x02, 0xed, 0x89, 0xc2, 0x8b, 0x46, 0xfe,
    0x89, 0xec, 0x5d, 0xcf,
};

/* Option-ROM initialization entry: install C000:0100 as vector 10h. */
static const uint8_t ia64_int10_rom_init[] = {
    0x50, 0x1e, 0x31, 0xc0, 0x8e, 0xd8, 0xc7, 0x06,
    0x40, 0x00, 0x00, 0x01, 0xc7, 0x06, 0x42, 0x00,
    0x00, 0xc0, 0x1f, 0x58, 0xcb,
};
#endif

#define TYPE_IA64_VPC_MACHINE MACHINE_TYPE_NAME("ia64-vpc-base")
#define TYPE_ITANIUM_VPC_MACHINE MACHINE_TYPE_NAME("itanium-vpc")
#define TYPE_ITANIUM2_VPC_MACHINE MACHINE_TYPE_NAME("itanium2-vpc")
OBJECT_DECLARE_TYPE(IA64VpcMachineState, IA64VpcMachineClass,
                    IA64_VPC_MACHINE)

struct IA64VpcMachineClass {
    MachineClass parent_class;

    uint64_t firmware_compat_flags;
    bool default_ahci;
    bool default_i8042;
};

struct IA64VpcMachineState {
    MachineState parent_obj;

    bool i8042_enabled;
    bool firmware_ide_dma;
    uint64_t firmware_console;
    char *nvram_path;
    bool alat_full;
    bool pcie;

    PCIDevice *ahci_dev;
    PCIDevice *ohci_dev;
    PCIDevice *uhci_dev;
    PCIDevice *lsi_dev;
    PCIDevice *vga_dev;
    PCIDevice *nic_devs[MAX_NICS];
    unsigned int nic_count;

    MemoryRegion *ram_aliases[5];
    unsigned int ram_alias_count;
    MemoryRegion *vga_fb_alias;
    MemoryRegion *vga_mmio_alias;
    MemoryRegion *vga_legacy_alias;
    MemoryRegion *lsapic_mmio;
    IA64RasHubState *ras;
    MemoryRegion firmware_space;
    MemoryRegion rtc_mmio;
    MemoryRegion watchdog_mmio;
    MemoryRegion nvram_mmio;
    MemoryRegion acpi_pm;
    MemoryRegion acpi_reset;
    MemoryRegion uart_io_alias;
#ifdef CONFIG_IA64_VPC_GRAPHICS
    MemoryRegion int10_pci_io;
    IA64Int10Registers int10_request;
    IA64Int10Registers int10_result;
    uint32_t int10_input_signature;
    uint8_t int10_response[512];
    uint16_t int10_response_length;
    uint16_t int10_response_offset;
    uint8_t int10_input_signature_words;
    uint8_t int10_dpms_state;
    uint8_t int10_legacy_mode;
    uint8_t int10_legacy_columns;
    IA64VbeMode vbe_modes[IA64_VBE_MAX_MODES];
    uint16_t vbe_mode_count;
    uint32_t vbe_prefx;
    uint32_t vbe_prefy;
    uint32_t vbe_maxx;
    uint32_t vbe_maxy;
    uint8_t vbe_edid[384];
    uint8_t vbe_edid_blocks;
#endif

    Object *pci_fixup_reset;
    QEMUTimer *watchdog_timer;
    uint64_t watchdog_timeout;
    uint64_t watchdog_code;
    uint8_t nvram_data[IA64_NVRAM_SIZE];
    size_t firmware_size;
    char *nvram_resolved_path;
    bool nvram_write_warning;
    ACPIREGS acpi_regs;
    qemu_irq acpi_sci_irq;
    qemu_irq isa_irqs[ISA_NUM_IRQS];
    Notifier powerdown_notifier;
    IA64MachineFirmwareNotifier firmware_notifier;
    bool vmstate_registered;
};

static bool ia64_vpc_vga_uses_large_aperture(PCIDevice *pci_dev)
{
    return pci_dev != NULL &&
           pci_dev->io_regions[0].size >
           IA64_VGA_MMIO_PCI_BASE - IA64_VGA_FB_PCI_BASE;
}

static hwaddr ia64_vpc_vga_fb_pci_base(PCIDevice *pci_dev)
{
    return ia64_vpc_vga_uses_large_aperture(pci_dev) ?
           IA64_VGA_LARGE_FB_PCI_BASE : IA64_VGA_FB_PCI_BASE;
}

static hwaddr ia64_vpc_vga_mmio_pci_base(PCIDevice *pci_dev)
{
    return ia64_vpc_vga_uses_large_aperture(pci_dev) ?
           IA64_VGA_LARGE_MMIO_PCI_BASE : IA64_VGA_MMIO_PCI_BASE;
}

#ifdef CONFIG_IA64_VPC_GRAPHICS
static const IA64VbeMode *ia64_vbe_find_mode(IA64VpcMachineState *s,
                                              uint16_t number)
{
    size_t i;

    for (i = 0; i < s->vbe_mode_count; i++) {
        if (s->vbe_modes[i].number == number) {
            return &s->vbe_modes[i];
        }
    }
    return NULL;
}

static bool ia64_vpc_prepare_vga_properties(IA64VpcMachineState *s,
                                             PCIDevice *vga,
                                             Error **errp)
{
    Object *obj = OBJECT(vga);
    static const char * const required[] = {
        "xres", "yres", "xmax", "ymax", "vgamem_mb",
    };
    uint64_t xres;
    uint64_t yres;
    uint64_t xmax;
    uint64_t ymax;
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(required); i++) {
        if (!object_property_find(obj, required[i])) {
            error_setg(errp,
                       "VGA device '%s' does not provide the '%s' property "
                       "required by the IA-64 VBE bridge",
                       object_get_typename(obj), required[i]);
            return false;
        }
    }

    xres = object_property_get_uint(obj, "xres", errp);
    if (*errp) {
        return false;
    }
    yres = object_property_get_uint(obj, "yres", errp);
    if (*errp) {
        return false;
    }
    xmax = object_property_get_uint(obj, "xmax", errp);
    if (*errp) {
        return false;
    }
    ymax = object_property_get_uint(obj, "ymax", errp);
    if (*errp) {
        return false;
    }

    if ((xres == 0) != (yres == 0)) {
        error_setg(errp, "VGA properties xres and yres must be set together");
        return false;
    }
    if ((xmax == 0) != (ymax == 0)) {
        error_setg(errp, "VGA properties xmax and ymax must be set together");
        return false;
    }

    if (xres == 0) {
        xres = IA64_VBE_DEFAULT_XRES;
        yres = IA64_VBE_DEFAULT_YRES;
        if (!object_property_set_uint(obj, "xres", xres, errp) ||
            !object_property_set_uint(obj, "yres", yres, errp)) {
            return false;
        }
    }
    if (xmax == 0) {
        xmax = xres;
        ymax = yres;
        if (!object_property_set_uint(obj, "xmax", xmax, errp) ||
            !object_property_set_uint(obj, "ymax", ymax, errp)) {
            return false;
        }
    }

    if (xres < 8 || yres < 1 ||
        xres > VBE_DISPI_MAX_XRES || yres > VBE_DISPI_MAX_YRES) {
        error_setg(errp,
                   "VGA preferred resolution %" PRIu64 "x%" PRIu64
                   " is outside the Bochs VBE range 8x1 to %ux%u",
                   xres, yres, VBE_DISPI_MAX_XRES, VBE_DISPI_MAX_YRES);
        return false;
    }
    if (xmax > VBE_DISPI_MAX_XRES || ymax > VBE_DISPI_MAX_YRES) {
        error_setg(errp,
                   "VGA maximum resolution %" PRIu64 "x%" PRIu64
                   " exceeds the Bochs VBE limit %ux%u",
                   xmax, ymax, VBE_DISPI_MAX_XRES, VBE_DISPI_MAX_YRES);
        return false;
    }
    if (xres > xmax || yres > ymax) {
        error_setg(errp,
                   "VGA preferred resolution %" PRIu64 "x%" PRIu64
                   " exceeds maximum resolution %" PRIu64 "x%" PRIu64,
                   xres, yres, xmax, ymax);
        return false;
    }
    if (xres & 7) {
        error_setg(errp,
                   "VGA preferred horizontal resolution %" PRIu64
                   " is not a multiple of 8 required by Bochs VBE",
                   xres);
        return false;
    }

    s->vbe_prefx = xres;
    s->vbe_prefy = yres;
    s->vbe_maxx = xmax;
    s->vbe_maxy = ymax;
    return true;
}

static const IA64VgaLegacyMode *ia64_vga_find_legacy_mode(uint8_t number)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(ia64_vga_legacy_modes); i++) {
        if (ia64_vga_legacy_modes[i].number == number) {
            return &ia64_vga_legacy_modes[i];
        }
    }
    return NULL;
}

static void ia64_vbe_write(uint16_t index, uint16_t value)
{
    address_space_stw_le(&address_space_memory,
                         IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_INDEX),
                         index, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory,
                         IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_DATA),
                         value, MEMTXATTRS_UNSPECIFIED, NULL);
}

static uint16_t ia64_vbe_read(uint16_t index)
{
    address_space_stw_le(&address_space_memory,
                         IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_INDEX),
                         index, MEMTXATTRS_UNSPECIFIED, NULL);
    return address_space_lduw_le(&address_space_memory,
                                 IA64_LEGACY_IO_PORT_PA(IA64_VBE_IO_DATA),
                                 MEMTXATTRS_UNSPECIFIED, NULL);
}

static uint32_t ia64_vbe_memory_size(void)
{
    return (uint32_t)ia64_vbe_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K) *
           (64 * KiB);
}

static uint64_t ia64_vbe_mode_size(const IA64VbeMode *mode)
{
    return (uint64_t)mode->width * mode->height *
           DIV_ROUND_UP(mode->bpp, 8);
}

static bool ia64_vbe_has_geometry(IA64VpcMachineState *s,
                                  const IA64VbeMode *candidate)
{
    size_t i;

    for (i = 0; i < s->vbe_mode_count; i++) {
        const IA64VbeMode *mode = &s->vbe_modes[i];

        if (mode->width == candidate->width &&
            mode->height == candidate->height &&
            mode->bpp == candidate->bpp) {
            return true;
        }
    }
    return false;
}

static void ia64_vbe_add_mode(IA64VpcMachineState *s,
                              const IA64VbeMode *mode)
{
    g_assert(s->vbe_mode_count < IA64_VBE_MAX_MODES);
    s->vbe_modes[s->vbe_mode_count++] = *mode;
}

static void ia64_vbe_add_filtered_modes(IA64VpcMachineState *s,
                                        const IA64VbeMode *modes,
                                        size_t count,
                                        uint32_t memory_size)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const IA64VbeMode *mode = &modes[i];

        if (mode->width <= s->vbe_maxx && mode->height <= s->vbe_maxy &&
            ia64_vbe_mode_size(mode) <= memory_size) {
            ia64_vbe_add_mode(s, mode);
        }
    }
}

static void ia64_vbe_add_oem_modes(IA64VpcMachineState *s,
                                   uint32_t memory_size)
{
    static const uint8_t bpps[] = { 16, 24, 32 };
    size_t i;
    size_t depth;

    for (i = 0; i < G_N_ELEMENTS(ia64_vbe_oem_resolutions); i++) {
        const IA64VbeResolution *resolution =
            &ia64_vbe_oem_resolutions[i];

        for (depth = 0; depth < G_N_ELEMENTS(bpps); depth++) {
            IA64VbeMode mode = {
                .number = resolution->base_number + depth,
                .width = resolution->width,
                .height = resolution->height,
                .bpp = bpps[depth],
            };

            if (mode.width <= s->vbe_maxx && mode.height <= s->vbe_maxy &&
                ia64_vbe_mode_size(&mode) <= memory_size) {
                ia64_vbe_add_mode(s, &mode);
            }
        }
    }
}

static bool ia64_vpc_build_vbe_config(IA64VpcMachineState *s, Error **errp)
{
    static const uint8_t native_bpps[] = { 16, 24, 32 };
    static const uint16_t native_numbers[] = {
        IA64_VBE_NATIVE_MODE_16,
        IA64_VBE_NATIVE_MODE_24,
        IA64_VBE_NATIVE_MODE_32,
    };
    PCIIORegion *fb = &s->vga_dev->io_regions[0];
    PCIIORegion *mmio = &s->vga_dev->io_regions[2];
    qemu_edid_info edid_info = {
        .vendor = "RHT",
        .name = "QEMU IA64",
        .prefx = s->vbe_prefx,
        .prefy = s->vbe_prefy,
        .maxx = s->vbe_maxx,
        .maxy = s->vbe_maxy,
        .refresh_rate = 60000,
    };
    uint32_t memory_size;
    uint64_t required_size;
    uint64_t required_mb;
    uint64_t suggested_mb;
    size_t edid_size;
    size_t i;

    _Static_assert(G_N_ELEMENTS(ia64_vbe_legacy_modes) +
                   G_N_ELEMENTS(ia64_vbe_oem_resolutions) * 3 +
                   G_N_ELEMENTS(native_bpps) <= IA64_VBE_MAX_MODES,
                   "IA-64 VBE mode array is too small");

    if (fb->memory == NULL || mmio->memory == NULL) {
        error_setg(errp,
                   "VGA device '%s' does not provide framebuffer BAR 0 and "
                   "MMIO BAR 2 required by the IA-64 VBE bridge",
                   object_get_typename(OBJECT(s->vga_dev)));
        return false;
    }
    if (fb->size > IA64_VGA_FIXED_FB_SIZE) {
        error_setg(errp,
                   "VGA framebuffer aperture is 0x%" PRIx64
                   " bytes, exceeding the IA-64 fixed-window limit of "
                   "0x%" PRIx64 " bytes",
                   (uint64_t)fb->size, (uint64_t)IA64_VGA_FIXED_FB_SIZE);
        return false;
    }

    memory_size = ia64_vbe_memory_size();
    if (memory_size == 0 || memory_size > fb->size ||
        memory_size > IA64_VGA_FIXED_FB_SIZE) {
        error_setg(errp,
                   "VGA memory size 0x%x is not addressable through the "
                   "IA-64 framebuffer aperture of 0x%" PRIx64 " bytes",
                   memory_size, (uint64_t)fb->size);
        return false;
    }

    required_size = (uint64_t)s->vbe_prefx * s->vbe_prefy * 4;
    if (required_size > memory_size) {
        required_mb = DIV_ROUND_UP(required_size, MiB);
        suggested_mb = 1;
        while (suggested_mb < required_mb) {
            suggested_mb <<= 1;
        }
        suggested_mb = MAX(suggested_mb, 16);
        error_setg(errp,
                   "VGA preferred mode %ux%ux32 requires %" PRIu64
                   " MiB of video memory; set vgamem_mb=%" PRIu64
                   " or larger",
                   s->vbe_prefx, s->vbe_prefy, required_mb, suggested_mb);
        return false;
    }

    s->vbe_mode_count = 0;
    if (s->vbe_prefx == IA64_VBE_DEFAULT_XRES &&
        s->vbe_prefy == IA64_VBE_DEFAULT_YRES &&
        s->vbe_maxx == IA64_VBE_DEFAULT_XRES &&
        s->vbe_maxy == IA64_VBE_DEFAULT_YRES) {
        for (i = 0; i < G_N_ELEMENTS(ia64_vbe_legacy_modes); i++) {
            ia64_vbe_add_mode(s, &ia64_vbe_legacy_modes[i]);
        }
    } else {
        ia64_vbe_add_filtered_modes(s, ia64_vbe_legacy_modes,
                                    G_N_ELEMENTS(ia64_vbe_legacy_modes),
                                    memory_size);
        ia64_vbe_add_oem_modes(s, memory_size);
        for (i = 0; i < G_N_ELEMENTS(native_bpps); i++) {
            IA64VbeMode native = {
                .number = native_numbers[i],
                .width = s->vbe_prefx,
                .height = s->vbe_prefy,
                .bpp = native_bpps[i],
            };

            if (!ia64_vbe_has_geometry(s, &native)) {
                ia64_vbe_add_mode(s, &native);
            }
        }
    }

    memset(s->vbe_edid, 0, sizeof(s->vbe_edid));
    /*
     * Preserve the historical single-block EDID for the default mode.  For
     * configured modes, give qemu_edid_generate() room for CEA and DisplayID
     * extensions when the preferred timing no longer fits in a base EDID
     * detailed-timing descriptor (4K60 is one such case).
     */
    edid_size = s->vbe_prefx == IA64_VBE_DEFAULT_XRES &&
                s->vbe_prefy == IA64_VBE_DEFAULT_YRES &&
                s->vbe_maxx == IA64_VBE_DEFAULT_XRES &&
                s->vbe_maxy == IA64_VBE_DEFAULT_YRES ?
                128 : sizeof(s->vbe_edid);
    qemu_edid_generate(s->vbe_edid, edid_size, &edid_info);
    s->vbe_edid_blocks = 1 + s->vbe_edid[126];
    g_assert(s->vbe_edid_blocks <= sizeof(s->vbe_edid) / 128);
    return true;
}

static void ia64_vga_writeb(uint16_t port, uint8_t value)
{
    address_space_stb(&address_space_memory, IA64_LEGACY_IO_PORT_PA(port),
                      value, MEMTXATTRS_UNSPECIFIED, NULL);
}

static uint8_t ia64_vga_readb(uint16_t port)
{
    return address_space_ldub(&address_space_memory,
                              IA64_LEGACY_IO_PORT_PA(port),
                              MEMTXATTRS_UNSPECIFIED, NULL);
}

static void ia64_vga_indexed_write(uint16_t index_port,
                                   uint16_t data_port,
                                   uint8_t index, uint8_t value)
{
    ia64_vga_writeb(index_port, index);
    ia64_vga_writeb(data_port, value);
}

static void ia64_int10_update_legacy_bda(const IA64VgaLegacyMode *mode,
                                         bool no_clear)
{
    address_space_stb(&address_space_memory, IA64_BDA_VIDEO_MODE,
                      mode->number, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, IA64_BDA_VIDEO_COLUMNS,
                         mode->columns, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, IA64_BDA_VIDEO_PAGE_SIZE,
                         mode->page_size, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, IA64_BDA_VIDEO_PAGE_START,
                         0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_set(&address_space_memory, IA64_BDA_CURSOR_POSITIONS,
                      0, 16, MEMTXATTRS_UNSPECIFIED);
    address_space_stw_le(&address_space_memory, IA64_BDA_CURSOR_TYPE,
                         0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, IA64_BDA_VIDEO_PAGE,
                      0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, IA64_BDA_CRTC_ADDRESS,
                         VGA_CRT_IC, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, IA64_BDA_VIDEO_ROWS,
                      mode->rows - 1, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, IA64_BDA_CHARACTER_HEIGHT,
                         mode->character_height,
                         MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, IA64_BDA_VIDEO_CONTROL,
                      0x60 | (no_clear ? 0x80 : 0),
                      MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, IA64_BDA_VIDEO_SWITCHES,
                      0xf9, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void ia64_vga_load_ega_palette(void)
{
    unsigned int color;

    ia64_vga_writeb(VGA_PEL_MSK, 0xff);
    ia64_vga_writeb(VGA_PEL_IW, 0);
    for (color = 0; color < 64; color++) {
        uint8_t red = (color & 0x04 ? 0x2a : 0) |
                      (color & 0x20 ? 0x15 : 0);
        uint8_t green = (color & 0x02 ? 0x2a : 0) |
                        (color & 0x10 ? 0x15 : 0);
        uint8_t blue = (color & 0x01 ? 0x2a : 0) |
                       (color & 0x08 ? 0x15 : 0);

        ia64_vga_writeb(VGA_PEL_D, red);
        ia64_vga_writeb(VGA_PEL_D, green);
        ia64_vga_writeb(VGA_PEL_D, blue);
    }
}

static void ia64_int10_program_legacy_mode(IA64VpcMachineState *s,
                                            const IA64VgaLegacyMode *mode,
                                            bool no_clear)
{
    size_t i;

    /*
     * A legacy VGA caller uses the planar A0000h aperture.  Disable the
     * synthetic VBE layout before programming standard VGA registers so a
     * previous packed-pixel framebuffer cannot reinterpret those writes.
     */
    ia64_vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    ia64_vga_indexed_write(VGA_SEQ_I, VGA_SEQ_D, VGA_SEQ_RESET, 0x01);
    for (i = 0; i < VGA_SEQ_C - 1; i++) {
        ia64_vga_indexed_write(VGA_SEQ_I, VGA_SEQ_D, i + 1,
                               mode->sequencer[i]);
    }
    ia64_vga_writeb(VGA_MIS_W, mode->misc);
    ia64_vga_indexed_write(VGA_GFX_I, VGA_GFX_D, VGA_GFX_MISC,
                           mode->graphics[VGA_GFX_MISC]);
    ia64_vga_indexed_write(VGA_SEQ_I, VGA_SEQ_D, VGA_SEQ_RESET, 0x03);
    for (i = 0; i < VGA_GFX_C; i++) {
        ia64_vga_indexed_write(VGA_GFX_I, VGA_GFX_D, i,
                               mode->graphics[i]);
    }

    ia64_vga_indexed_write(VGA_CRT_IC, VGA_CRT_DC,
                           VGA_CRTC_V_SYNC_END, 0);
    for (i = 0; i < VGA_CRT_C; i++) {
        ia64_vga_indexed_write(VGA_CRT_IC, VGA_CRT_DC, i,
                               mode->crtc[i]);
    }
    for (i = 0; i < VGA_ATT_C; i++) {
        (void)ia64_vga_readb(VGA_IS1_RC);
        ia64_vga_writeb(VGA_ATT_W, i);
        ia64_vga_writeb(VGA_ATT_W, mode->attribute[i]);
    }
    ia64_vga_load_ega_palette();

    if (!no_clear) {
        address_space_set(&address_space_memory,
                          ia64_vpc_vga_fb_pci_base(s->vga_dev),
                          0, IA64_VGA_PLANAR_MEMORY_SIZE,
                          MEMTXATTRS_UNSPECIFIED);
    }
    (void)ia64_vga_readb(VGA_IS1_RC);
    ia64_vga_writeb(VGA_ATT_W, VGA_AR_ENABLE_DISPLAY);

    s->int10_legacy_mode = mode->number;
    s->int10_legacy_columns = mode->columns;
    ia64_int10_update_legacy_bda(mode, no_clear);
}

static bool ia64_int10_set_legacy_mode(IA64VpcMachineState *s,
                                       uint8_t request)
{
    uint8_t number = request & 0x7f;
    bool no_clear = request & 0x80;
    const IA64VgaLegacyMode *mode;

    if (number == 3) {
        ia64_vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        s->int10_legacy_mode = number;
        s->int10_legacy_columns = 80;
        return true;
    }

    mode = ia64_vga_find_legacy_mode(number);
    if (mode == NULL) {
        return false;
    }
    ia64_int10_program_legacy_mode(s, mode, no_clear);
    return true;
}

static uint32_t ia64_int10_rom_pointer(uint16_t offset)
{
    return ((IA64_INT10_ROM_BASE >> 4) << 16) | offset;
}

static void ia64_int10_response_clear(IA64VpcMachineState *s)
{
    memset(s->int10_response, 0, sizeof(s->int10_response));
    s->int10_response_length = 0;
    s->int10_response_offset = 0;
}

static void ia64_int10_response_size(IA64VpcMachineState *s, size_t size)
{
    g_assert(size <= sizeof(s->int10_response));
    g_assert((size & 1) == 0);
    memset(s->int10_response, 0, size);
    s->int10_response_length = size;
    s->int10_response_offset = 0;
}

static void ia64_int10_vbe_success(IA64VpcMachineState *s)
{
    s->int10_result.ax = 0x004f;
}

static void ia64_int10_vbe_failure(IA64VpcMachineState *s)
{
    s->int10_result.ax = 0x014f;
}

static void ia64_int10_vbe_unsupported(IA64VpcMachineState *s)
{
    s->int10_result.ax = 0x024f;
}

static void ia64_int10_vbe_invalid_mode(IA64VpcMachineState *s)
{
    s->int10_result.ax = 0x034f;
}

static void ia64_int10_controller_info(IA64VpcMachineState *s)
{
    size_t response_size;
    uint8_t *info;

    response_size = s->int10_input_signature == IA64_VBE2_SIGNATURE ?
                    512 : 256;
    ia64_int10_response_size(s, response_size);
    info = s->int10_response;
    memcpy(info, "VESA", 4);
    stw_le_p(info + 4, 0x0300);
    stl_le_p(info + 6,
             ia64_int10_rom_pointer(IA64_INT10_ROM_OEM_OFFSET));
    stl_le_p(info + 10, 0);
    stl_le_p(info + 14,
             ia64_int10_rom_pointer(IA64_INT10_ROM_MODES_OFFSET));
    stw_le_p(info + 18,
             ia64_vbe_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K));
    stw_le_p(info + 20, 0x0100);
    stl_le_p(info + 22,
             ia64_int10_rom_pointer(IA64_INT10_ROM_VENDOR_OFFSET));
    stl_le_p(info + 26,
             ia64_int10_rom_pointer(IA64_INT10_ROM_PRODUCT_OFFSET));
    stl_le_p(info + 30,
             ia64_int10_rom_pointer(IA64_INT10_ROM_REVISION_OFFSET));
    ia64_int10_vbe_success(s);
}

static void ia64_int10_mode_info(IA64VpcMachineState *s)
{
    const IA64VbeMode *mode =
        ia64_vbe_find_mode(s, s->int10_request.cx & 0x01ff);
    uint32_t pitch;
    uint32_t image_size;
    uint32_t memory_size;
    uint32_t pages;
    uint8_t red_size;
    uint8_t green_size;
    uint8_t alpha_size;
    uint8_t alpha_pos;
    uint8_t *info;

    if (mode == NULL) {
        ia64_int10_vbe_failure(s);
        return;
    }

    ia64_int10_response_size(s, 256);
    info = s->int10_response;
    pitch = mode->width * DIV_ROUND_UP(mode->bpp, 8);
    image_size = pitch * mode->height;
    memory_size = ia64_vbe_memory_size();
    if (image_size > memory_size) {
        ia64_int10_response_clear(s);
        ia64_int10_vbe_failure(s);
        return;
    }
    pages = memory_size /
            ((image_size + 64 * KiB - 1) & ~((64 * KiB) - 1));
    pages = CLAMP(pages, 1, 256) - 1;

    stw_le_p(info + 0, 0x00bb);
    info[2] = 0x07;
    info[3] = 0;
    stw_le_p(info + 4, 64);
    stw_le_p(info + 6, 64);
    stw_le_p(info + 8, 0xa000);
    stw_le_p(info + 10, 0);
    stl_le_p(info + 12, 0);
    stw_le_p(info + 16, pitch);
    stw_le_p(info + 18, mode->width);
    stw_le_p(info + 20, mode->height);
    info[22] = 8;
    info[23] = 16;
    info[24] = 1;
    info[25] = mode->bpp;
    info[26] = 1;
    info[27] = 6; /* Direct-color memory model. */
    info[28] = 0;
    info[29] = pages;
    info[30] = 1;

    red_size = mode->bpp == 16 ? 5 : 8;
    green_size = mode->bpp == 16 ? 6 : 8;
    alpha_size = mode->bpp == 32 ? 8 : 0;
    alpha_pos = mode->bpp == 32 ? 24 : 0;
    info[31] = red_size;
    info[32] = mode->bpp == 16 ? 11 : 16;
    info[33] = green_size;
    info[34] = mode->bpp == 16 ? 5 : 8;
    info[35] = mode->bpp == 16 ? 5 : 8;
    info[36] = 0;
    info[37] = alpha_size;
    info[38] = alpha_pos;
    info[39] = mode->bpp == 32 ? 2 : 0;
    stl_le_p(info + 40, ia64_vpc_vga_fb_pci_base(s->vga_dev));
    stw_le_p(info + 50, pitch);
    info[52] = pages;
    info[53] = pages;
    memcpy(info + 54, info + 31, 8);
    ia64_int10_vbe_success(s);
}

static const IA64VbeMode *ia64_int10_current_mode(IA64VpcMachineState *s,
                                                   uint16_t *number)
{
    const IA64VbeMode *mode = NULL;
    uint16_t enable = ia64_vbe_read(VBE_DISPI_INDEX_ENABLE);
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    size_t i;

    if (!(enable & VBE_DISPI_ENABLED)) {
        *number = 3;
        return NULL;
    }
    width = ia64_vbe_read(VBE_DISPI_INDEX_XRES);
    height = ia64_vbe_read(VBE_DISPI_INDEX_YRES);
    bpp = ia64_vbe_read(VBE_DISPI_INDEX_BPP);
    for (i = 0; i < s->vbe_mode_count; i++) {
        if (s->vbe_modes[i].width == width &&
            s->vbe_modes[i].height == height &&
            s->vbe_modes[i].bpp == bpp) {
            mode = &s->vbe_modes[i];
            break;
        }
    }
    *number = mode ? mode->number : 3;
    if (mode && (enable & VBE_DISPI_LFB_ENABLED)) {
        *number |= 0x4000;
    }
    if (mode && (enable & VBE_DISPI_NOCLEARMEM)) {
        *number |= 0x8000;
    }
    return mode;
}

static void ia64_int10_set_mode(IA64VpcMachineState *s)
{
    const IA64VbeMode *mode =
        ia64_vbe_find_mode(s, s->int10_request.bx & 0x01ff);
    uint32_t image_size;
    uint16_t enable;

    if (mode == NULL) {
        ia64_int10_vbe_failure(s);
        return;
    }
    image_size = mode->width * mode->height *
                 DIV_ROUND_UP(mode->bpp, 8);
    if (image_size > ia64_vbe_memory_size()) {
        ia64_int10_vbe_failure(s);
        return;
    }

    ia64_vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    ia64_vbe_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);
    ia64_vbe_write(VBE_DISPI_INDEX_BPP, mode->bpp);
    ia64_vbe_write(VBE_DISPI_INDEX_XRES, mode->width);
    ia64_vbe_write(VBE_DISPI_INDEX_YRES, mode->height);
    ia64_vbe_write(VBE_DISPI_INDEX_BANK, 0);
    ia64_vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, mode->width);
    ia64_vbe_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    ia64_vbe_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    enable = VBE_DISPI_ENABLED;
    if (s->int10_request.bx & 0x4000) {
        enable |= VBE_DISPI_LFB_ENABLED;
    }
    if (s->int10_request.bx & 0x8000) {
        enable |= VBE_DISPI_NOCLEARMEM;
    }
    ia64_vbe_write(VBE_DISPI_INDEX_ENABLE, enable);
    ia64_int10_vbe_success(s);
}

static void ia64_int10_window_control(IA64VpcMachineState *s)
{
    uint8_t subfunction = s->int10_request.bx >> 8;
    uint8_t window = s->int10_request.bx;

    if (window != 0 || subfunction > 1) {
        ia64_int10_vbe_failure(s);
        return;
    }
    if (ia64_vbe_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_LFB_ENABLED) {
        ia64_int10_vbe_invalid_mode(s);
        return;
    }
    if (subfunction == 0) {
        ia64_vbe_write(VBE_DISPI_INDEX_BANK, s->int10_request.dx);
    } else {
        s->int10_result.dx = ia64_vbe_read(VBE_DISPI_INDEX_BANK);
    }
    ia64_int10_vbe_success(s);
}

static void ia64_int10_scanline(IA64VpcMachineState *s)
{
    uint16_t number;
    const IA64VbeMode *mode = ia64_int10_current_mode(s, &number);
    uint8_t subfunction = s->int10_request.bx;
    uint32_t bytes_per_pixel;
    uint32_t max_width;
    uint32_t memory_size;
    uint32_t width;
    uint32_t pitch;

    if (mode == NULL || subfunction > 3) {
        ia64_int10_vbe_failure(s);
        return;
    }

    bytes_per_pixel = DIV_ROUND_UP(mode->bpp, 8);
    memory_size = ia64_vbe_memory_size();
    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / mode->height / bytes_per_pixel) & ~7U;
    if (max_width < mode->width) {
        ia64_int10_vbe_failure(s);
        return;
    }

    if (subfunction == 0) {
        width = QEMU_ALIGN_UP((uint32_t)s->int10_request.cx, 8);
        width = MAX(width, (uint32_t)mode->width);
        if (width > max_width) {
            ia64_int10_vbe_unsupported(s);
            return;
        }
        ia64_vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    } else if (subfunction == 2) {
        width = DIV_ROUND_UP(s->int10_request.cx, bytes_per_pixel);
        if (width == 0) {
            ia64_int10_vbe_failure(s);
            return;
        }
        width = QEMU_ALIGN_UP(width, 8);
        width = MAX(width, (uint32_t)mode->width);
        if (width > max_width) {
            ia64_int10_vbe_unsupported(s);
            return;
        }
        ia64_vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    } else if (subfunction == 3) {
        width = max_width;
    }

    if (subfunction != 3) {
        width = ia64_vbe_read(VBE_DISPI_INDEX_VIRT_WIDTH);
    }
    pitch = width * bytes_per_pixel;
    if (pitch == 0) {
        ia64_int10_vbe_failure(s);
        return;
    }
    s->int10_result.bx = pitch;
    s->int10_result.cx = width;
    s->int10_result.dx = MIN(memory_size / pitch, UINT16_MAX);
    ia64_int10_vbe_success(s);
}

static void ia64_int10_display_start(IA64VpcMachineState *s)
{
    uint16_t number;
    const IA64VbeMode *mode = ia64_int10_current_mode(s, &number);
    uint8_t subfunction = s->int10_request.bx;

    if (mode == NULL) {
        ia64_int10_vbe_failure(s);
        return;
    }
    switch (subfunction) {
    case 0x00:
    case 0x80:
        ia64_vbe_write(VBE_DISPI_INDEX_X_OFFSET, s->int10_request.cx);
        ia64_vbe_write(VBE_DISPI_INDEX_Y_OFFSET, s->int10_request.dx);
        break;
    case 0x01:
        s->int10_result.cx = ia64_vbe_read(VBE_DISPI_INDEX_X_OFFSET);
        s->int10_result.dx = ia64_vbe_read(VBE_DISPI_INDEX_Y_OFFSET);
        break;
    default:
        ia64_int10_vbe_failure(s);
        return;
    }
    ia64_int10_vbe_success(s);
}

static void ia64_int10_dpms(IA64VpcMachineState *s)
{
    uint8_t subfunction = s->int10_request.bx;

    switch (subfunction) {
    case 0:
        s->int10_result.bx = 0x0f30;
        break;
    case 1:
        s->int10_dpms_state = (s->int10_request.bx >> 8) & 0x0f;
        break;
    case 2:
        s->int10_result.bx = (uint16_t)s->int10_dpms_state << 8 | 2;
        break;
    default:
        ia64_int10_vbe_failure(s);
        return;
    }
    ia64_int10_vbe_success(s);
}

static void ia64_int10_ddc(IA64VpcMachineState *s)
{
    uint8_t subfunction = s->int10_request.bx;
    uint16_t block = s->int10_request.dx;

    switch (subfunction) {
    case 0:
        s->int10_result.bx = 0x0103;
        break;
    case 1:
        if (block >= s->vbe_edid_blocks) {
            ia64_int10_vbe_failure(s);
            return;
        }
        ia64_int10_response_size(s, 128);
        memcpy(s->int10_response, s->vbe_edid + block * 128, 128);
        break;
    default:
        ia64_int10_vbe_failure(s);
        return;
    }
    ia64_int10_vbe_success(s);
}

static void ia64_int10_execute(IA64VpcMachineState *s)
{
    uint16_t current_mode;

    s->int10_result = s->int10_request;
    ia64_int10_response_clear(s);

    if ((s->int10_request.ax & 0xff00) == 0x4f00) {
        switch (s->int10_request.ax & 0xff) {
        case 0x00:
            ia64_int10_controller_info(s);
            return;
        case 0x01:
            ia64_int10_mode_info(s);
            return;
        case 0x02:
            ia64_int10_set_mode(s);
            return;
        case 0x03:
            ia64_int10_current_mode(s, &current_mode);
            s->int10_result.bx = current_mode;
            ia64_int10_vbe_success(s);
            return;
        case 0x05:
            ia64_int10_window_control(s);
            return;
        case 0x06:
            ia64_int10_scanline(s);
            return;
        case 0x07:
            ia64_int10_display_start(s);
            return;
        case 0x10:
            ia64_int10_dpms(s);
            return;
        case 0x15:
            ia64_int10_ddc(s);
            return;
        default:
            ia64_int10_vbe_unsupported(s);
            return;
        }
    }

    switch (s->int10_request.ax >> 8) {
    case 0x00:
        ia64_int10_set_legacy_mode(s, s->int10_request.ax);
        break;
    case 0x0f:
        if (ia64_vbe_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED) {
            s->int10_result.ax = 80 << 8 | 3;
        } else {
            s->int10_result.ax = (uint16_t)s->int10_legacy_columns << 8 |
                                 s->int10_legacy_mode;
        }
        s->int10_result.bx &= 0x00ff;
        break;
    case 0x1a:
        if ((s->int10_request.ax & 0xff) == 0) {
            s->int10_result.ax = 0x001a;
            s->int10_result.bx = 0x0008;
        }
        break;
    default:
        break;
    }
}

static uint64_t ia64_int10_io_read(void *opaque, hwaddr addr, unsigned size)
{
    IA64VpcMachineState *s = opaque;
    unsigned reg = addr >> 1;

    if (size != 2 || (addr & 1)) {
        return 0xffff;
    }
    switch (reg) {
    case IA64_INT10_REG_AX:
        return s->int10_result.ax;
    case IA64_INT10_REG_BX:
        return s->int10_result.bx;
    case IA64_INT10_REG_CX:
        return s->int10_result.cx;
    case IA64_INT10_REG_DX:
        return s->int10_result.dx;
    case IA64_INT10_REG_DI:
        return s->int10_result.di;
    case IA64_INT10_REG_ES:
        return s->int10_result.es;
    case IA64_INT10_REG_EXEC:
        return s->int10_response_length / 2;
    case IA64_INT10_REG_DATA:
        if (s->int10_response_offset < s->int10_response_length) {
            uint16_t value = lduw_le_p(s->int10_response +
                                      s->int10_response_offset);

            s->int10_response_offset += 2;
            return value;
        }
        return 0;
    default:
        return 0xffff;
    }
}

static void ia64_int10_io_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
    IA64VpcMachineState *s = opaque;
    unsigned reg = addr >> 1;

    if (size != 2 || (addr & 1)) {
        return;
    }
    switch (reg) {
    case IA64_INT10_REG_AX:
        s->int10_request.ax = value;
        s->int10_input_signature = 0;
        s->int10_input_signature_words = 0;
        break;
    case IA64_INT10_REG_BX:
        s->int10_request.bx = value;
        break;
    case IA64_INT10_REG_CX:
        s->int10_request.cx = value;
        break;
    case IA64_INT10_REG_DX:
        s->int10_request.dx = value;
        break;
    case IA64_INT10_REG_DI:
        s->int10_request.di = value;
        break;
    case IA64_INT10_REG_ES:
        s->int10_request.es = value;
        break;
    case IA64_INT10_REG_EXEC:
        if ((uint16_t)value == IA64_INT10_TRIGGER) {
            ia64_int10_execute(s);
        }
        break;
    case IA64_INT10_REG_DATA:
        if (s->int10_input_signature_words < 2) {
            s->int10_input_signature |=
                (uint32_t)(uint16_t)value <<
                (s->int10_input_signature_words * 16);
            s->int10_input_signature_words++;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ia64_int10_io_ops = {
    .read = ia64_int10_io_read,
    .write = ia64_int10_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void ia64_int10_install_ati_clock_range(uint8_t *pll, size_t offset,
                                                uint16_t reference,
                                                uint16_t divider,
                                                uint32_t minimum,
                                                uint32_t maximum)
{
    stw_le_p(pll + offset, reference);
    stw_le_p(pll + offset + 2, divider);
    stl_le_p(pll + offset + 4, minimum);
    stl_le_p(pll + offset + 8, maximum);
}

static void ia64_int10_install_ati_bios_info(uint8_t *rom, PCIDevice *vga,
                                             uint32_t memory_size)
{
    static const char ati_bios_signature[] = "761295520";
    static const uint8_t ati_rage128_header[] = {
        0x02, 0xa0, 0x01, 0x01, 0x03, 0x01,
        IA64_INT10_ROM_ATI_RAGE128_HEADER_SIZE, 0x00,
    };
    static const char ati_rage128_misc[] = "R128AGP SGS1UN";
    static const uint8_t
        ati_rage128_crt[IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE] = {
        0x12, 0x00, 0x80, 0x00, 0x00, 0x00, 0x63, 0x4f,
        0x51, 0x8c, 0x0c, 0x02, 0xdf, 0x01, 0xe9, 0x01,
        0x82, 0x00, 0xd6, 0x09,
    };
    static const uint8_t ati_vga_connector[] = {
        0x11, 0x11, 0x00, 0x23, 0x00, 0x00,
    };
    uint8_t *header = rom + IA64_INT10_ROM_ATI_HEADER_OFFSET;
    uint8_t *pll = rom + IA64_INT10_ROM_ATI_PLL_OFFSET;
    uint16_t vendor = pci_get_word(vga->config + PCI_VENDOR_ID);
    uint16_t device = pci_get_word(vga->config + PCI_DEVICE_ID);
    bool rage128 = device == IA64_ATI_RAGE128_DEVICE_ID;
    bool es1000 = device == IA64_ATI_ES1000_DEVICE_ID;
    uint32_t memory_mb = MIN(memory_size / MiB, 256U);
    uint32_t memory_step = memory_mb > UINT8_MAX ? 2 : 1;
    uint32_t memory_units = memory_mb / memory_step;

    if (vendor != IA64_ATI_VENDOR_ID) {
        return;
    }
    g_assert(memory_size % MiB == 0);
    g_assert(memory_mb != 0 && memory_mb % memory_step == 0);
    g_assert(memory_units != 0 && memory_units <= UINT8_MAX);

    memcpy(rom + IA64_INT10_ROM_ATI_SIGNATURE_OFFSET,
           ati_bios_signature, sizeof(ati_bios_signature));
    stw_le_p(rom + 0x48, IA64_INT10_ROM_ATI_HEADER_OFFSET);
    if (rage128) {
        memcpy(header, ati_rage128_header, sizeof(ati_rage128_header));
    } else {
        header[0] = 8;
        header[1] = 0xa0;
        stw_le_p(header + 0x06, IA64_INT10_ROM_ATI_HEADER_SIZE);
    }
    stw_le_p(header + 0x0c, IA64_INT10_ROM_ATI_INIT_OFFSET);
    stw_le_p(header + 0x1c,
             pci_get_word(vga->config + PCI_SUBSYSTEM_VENDOR_ID));
    stw_le_p(header + 0x1e,
             pci_get_word(vga->config + PCI_SUBSYSTEM_ID));
    stw_le_p(header + 0x30, IA64_INT10_ROM_ATI_PLL_OFFSET);
    if (rage128) {
        stw_le_p(header + 0x14, IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET);
        stw_le_p(header + 0x2e, IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET);
        memcpy(rom + IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET,
               ati_rage128_misc, sizeof(ati_rage128_misc));
        memcpy(rom + IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET,
               ati_rage128_crt, sizeof(ati_rage128_crt));
    } else {
        stw_le_p(header + 0x14, IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET);
        stw_le_p(header + 0x46, IA64_INT10_ROM_ATI_INIT_OFFSET);
        stw_le_p(header + 0x48, IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET);
        stw_le_p(header + 0x4e, IA64_INT10_ROM_ATI_INIT_OFFSET);
        /* One VGA/primary-DAC connector entry. */
        stw_le_p(header + 0x50, IA64_INT10_ROM_ATI_CONNECTOR_OFFSET);
        memcpy(rom + IA64_INT10_ROM_ATI_CONNECTOR_OFFSET,
               ati_vga_connector, sizeof(ati_vga_connector));
        stw_le_p(header + 0x52, IA64_INT10_ROM_ATI_INIT_OFFSET);
        stw_le_p(header + 0x5e, IA64_INT10_ROM_ATI_MISC_OFFSET);
    }

    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET - 3] =
        IA64_INT10_ROM_ATI_MEM_RESET_OFFSET;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET - 2] =
        memory_step == 1 ? 0 : memory_step;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET - 1] = 0;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET] = (uint8_t)memory_units;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET + 1] =
        memory_step == 1 ? 0x25 : 0x2d;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET + 2] = 0;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET + 3] = 1;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET + 4] = 0;
    rom[IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET + 5] = 0xff;

    pll[0] = rage128 ? 6 : 0x0a;
    pll[1] = rage128 ? 0x32 : 0x46;
    pll[2] = 3;
    pll[3] = rage128 ? 2 : 3;
    stw_le_p(pll + 0x04, rage128 ? 0x0600 :
             (es1000 ? 0x05ee : 0x05a6));
    stw_le_p(pll + 0x06, rage128 ? 0x05f8 :
             (es1000 ? 0x05e6 : 0x059e));
    stw_le_p(pll + 0x08, rage128 ? 12000 : (es1000 ? 20000 : 16600));
    stw_le_p(pll + 0x0a, rage128 ? 12000 : (es1000 ? 20000 : 16600));
    pll[0x0c] = 3;
    pll[0x0d] = 12;
    if (rage128) {
        ia64_int10_install_ati_clock_range(pll, 0x0e,
                                           2950, 65, 12500, 40000);
        ia64_int10_install_ati_clock_range(pll, 0x1a,
                                           2950, 29, 12500, 26041);
        ia64_int10_install_ati_clock_range(pll, 0x26,
                                           2950, 29, 12500, 26041);
    } else {
        ia64_int10_install_ati_clock_range(pll, 0x0e,
                                           2700, 60, 12000, 35000);
        ia64_int10_install_ati_clock_range(pll, 0x1a,
                                           2700, 12, 20000, 40000);
        ia64_int10_install_ati_clock_range(pll, 0x26,
                                           2700, 12, 20000, 40000);
        pll[0x32] = 1;
        pll[0x33] = 0x12;
        stw_le_p(pll + 0x34, 2700);
        stl_le_p(pll + 0x36, 40);
        stl_le_p(pll + 0x3a, 3000);
        stl_le_p(pll + 0x3e, 12000);
        stl_le_p(pll + 0x42, 35000);
    }
}

static void ia64_vpc_install_int10(IA64VpcMachineState *s)
{
    uint8_t rom[IA64_INT10_ROM_SIZE] = { 0 };
    uint8_t vector[4];
    uint8_t checksum = 0;
    uint16_t vendor = pci_get_word(s->vga_dev->config + PCI_VENDOR_ID);
    uint16_t device = pci_get_word(s->vga_dev->config + PCI_DEVICE_ID);
    size_t i;

    g_assert(IA64_INT10_ROM_HANDLER_OFFSET +
             sizeof(ia64_int10_handler) <= IA64_INT10_ROM_OEM_OFFSET);
    g_assert(IA64_INT10_ROM_OEM_OFFSET + sizeof(ia64_vbe_oem) <=
             IA64_INT10_ROM_VENDOR_OFFSET);
    g_assert(IA64_INT10_ROM_VENDOR_OFFSET + sizeof(ia64_vbe_vendor) <=
             IA64_INT10_ROM_PRODUCT_OFFSET);
    g_assert(IA64_INT10_ROM_PRODUCT_OFFSET + sizeof(ia64_vbe_product) <=
             IA64_INT10_ROM_REVISION_OFFSET);
    g_assert(IA64_INT10_ROM_REVISION_OFFSET + sizeof(ia64_vbe_revision) <=
             IA64_INT10_ROM_MODES_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_HEADER_OFFSET +
             IA64_INT10_ROM_ATI_HEADER_SIZE <=
             IA64_INT10_ROM_ATI_INIT_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_INIT_OFFSET +
             IA64_INT10_ROM_ATI_INIT_READ_SIZE <=
             IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_BIOS_SUPPORT_OFFSET +
             IA64_INT10_ROM_ATI_BIOS_SUPPORT_SIZE <=
             IA64_INT10_ROM_ATI_MISC_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_RAGE128_MISC_OFFSET +
             IA64_INT10_ROM_ATI_RAGE128_MISC_SIZE <=
             IA64_INT10_ROM_HANDLER_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_MISC_OFFSET +
             IA64_INT10_ROM_ATI_MISC_SIZE <=
             IA64_INT10_ROM_HANDLER_OFFSET);
    g_assert(IA64_INT10_ROM_MODES_OFFSET +
             (s->vbe_mode_count + 1) * 2 <=
             IA64_INT10_ROM_ATI_CONNECTOR_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_CONNECTOR_OFFSET +
             IA64_INT10_ROM_ATI_CONNECTOR_SIZE <=
             IA64_INT10_ROM_ATI_PLL_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_RAGE128_CRT_OFFSET +
             IA64_INT10_ROM_ATI_RAGE128_CRT_SIZE <=
             IA64_INT10_ROM_ATI_PLL_OFFSET);
    g_assert(IA64_INT10_ROM_ATI_PLL_OFFSET +
             IA64_INT10_ROM_ATI_PLL_READ_SIZE <=
             IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET -
             IA64_INT10_ROM_ATI_MEM_PREFIX_SIZE);
    g_assert(IA64_INT10_ROM_ATI_MEM_CONFIG_OFFSET +
             IA64_INT10_ROM_ATI_MEM_RESET_OFFSET +
             IA64_INT10_ROM_ATI_MEM_RESET_SIZE < sizeof(rom));
    rom[0] = 0x55;
    rom[1] = 0xaa;
    rom[2] = IA64_INT10_ROM_SIZE / 512;
    memcpy(rom + 3, ia64_int10_rom_init, sizeof(ia64_int10_rom_init));

    /*
     * Keep PCIR away from the legacy ATI BIOS pointer at 48h.  Both fields
     * are consumed by real drivers and ROM validators.
     */
    stw_le_p(rom + 0x18, IA64_INT10_ROM_PCIR_OFFSET);
    memcpy(rom + IA64_INT10_ROM_PCIR_OFFSET, "PCIR", 4);
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x04, vendor);
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x06, device);
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x08, 0);
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x0a, 0x18);
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x0c] = 0;
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x0d] = 0;
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x0e] = 0;
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x0f] =
        PCI_CLASS_DISPLAY_VGA >> 8;
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x10,
             IA64_INT10_ROM_SIZE / 512);
    stw_le_p(rom + IA64_INT10_ROM_PCIR_OFFSET + 0x12, 0x0100);
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x14] = 0;
    rom[IA64_INT10_ROM_PCIR_OFFSET + 0x15] = 0x80;
    memcpy(rom + 0x60, "QEMU IA64 VBE INT10", 20);
    ia64_int10_install_ati_bios_info(rom, s->vga_dev,
                                     ia64_vbe_memory_size());
    memcpy(rom + IA64_INT10_ROM_HANDLER_OFFSET, ia64_int10_handler,
           sizeof(ia64_int10_handler));
    memcpy(rom + IA64_INT10_ROM_OEM_OFFSET,
           ia64_vbe_oem, sizeof(ia64_vbe_oem));
    memcpy(rom + IA64_INT10_ROM_VENDOR_OFFSET,
           ia64_vbe_vendor, sizeof(ia64_vbe_vendor));
    memcpy(rom + IA64_INT10_ROM_PRODUCT_OFFSET,
           ia64_vbe_product, sizeof(ia64_vbe_product));
    memcpy(rom + IA64_INT10_ROM_REVISION_OFFSET,
           ia64_vbe_revision, sizeof(ia64_vbe_revision));
    for (i = 0; i < s->vbe_mode_count; i++) {
        stw_le_p(rom + IA64_INT10_ROM_MODES_OFFSET + i * 2,
                 s->vbe_modes[i].number);
    }
    stw_le_p(rom + IA64_INT10_ROM_MODES_OFFSET +
             s->vbe_mode_count * 2, 0xffff);

    for (i = 0; i < sizeof(rom) - 1; i++) {
        checksum += rom[i];
    }
    rom[sizeof(rom) - 1] = -checksum;
    cpu_physical_memory_write(IA64_INT10_ROM_BASE, rom, sizeof(rom));

    /*
     * Keep the interrupt entry inside its option ROM, following the
     * conventional PC BIOS layout and the read-only ROM resource exposed by
     * the PCI root bridge.
     */
    stw_le_p(vector, IA64_INT10_ROM_HANDLER_OFFSET);
    stw_le_p(vector + 2, IA64_INT10_ROM_BASE >> 4);
    cpu_physical_memory_write(IA64_INT10_VECTOR_ADDR, vector,
                              sizeof(vector));
}

static void ia64_vpc_reset_int10(IA64VpcMachineState *s)
{
    memset(&s->int10_request, 0, sizeof(s->int10_request));
    memset(&s->int10_result, 0, sizeof(s->int10_result));
    s->int10_input_signature = 0;
    s->int10_input_signature_words = 0;
    ia64_int10_response_clear(s);
    s->int10_dpms_state = 0;
    s->int10_legacy_mode = 3;
    s->int10_legacy_columns = 80;
    ia64_vpc_install_int10(s);
}

static void ia64_vpc_init_int10(IA64VpcMachineState *s,
                                MemoryRegion *pci_io)
{
    memory_region_init_io(&s->int10_pci_io, OBJECT(s),
                          &ia64_int10_io_ops, s,
                          "ia64-vpc.int10-pci-io", IA64_INT10_IO_SIZE);
    memory_region_add_subregion(pci_io, IA64_INT10_IO_BASE,
                                &s->int10_pci_io);
    ia64_vpc_reset_int10(s);
}
#endif

static uint64_t ia64_vpc_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    struct tm tm;

    (void)opaque;
    if (addr != 0 || size != sizeof(uint64_t)) {
        return 0;
    }

    /*
     * Expose the QEMU-configured RTC as seconds since the Unix epoch.  A
     * single aligned 64-bit read is intrinsically coherent, unlike a bank of
     * calendar registers whose fields could straddle a second boundary.
     */
    qemu_get_timedate(&tm, 0);
    return mktimegm(&tm);
}

static void ia64_vpc_rtc_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    /* The platform RTC is a read-only seconds-since-epoch register. */
    (void)opaque;
    (void)addr;
    (void)value;
    (void)size;
}

static const MemoryRegionOps ia64_vpc_rtc_ops = {
    .read = ia64_vpc_rtc_read,
    .write = ia64_vpc_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void ia64_vpc_init_rtc(IA64VpcMachineState *s)
{
    memory_region_init_io(&s->rtc_mmio, OBJECT(s),
                          &ia64_vpc_rtc_ops, s, "ia64-vpc.rtc",
                          IA64_RTC_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(), IA64_RTC_BASE,
                                        &s->rtc_mmio, 2);
}

static void ia64_vpc_watchdog_expired(void *opaque)
{
    IA64VpcMachineState *s = opaque;

    warn_report("IA-64 firmware watchdog expired (code 0x%" PRIx64 ")",
                s->watchdog_code);
    s->watchdog_timeout = 0;
    watchdog_perform_action();
}

static uint64_t ia64_vpc_watchdog_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    IA64VpcMachineState *s = opaque;

    (void)size;

    switch (addr) {
    case IA64_WATCHDOG_TIMEOUT:
        return s->watchdog_timeout;
    case IA64_WATCHDOG_CODE:
        return s->watchdog_code;
    default:
        return 0;
    }
}

static void ia64_vpc_watchdog_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    IA64VpcMachineState *s = opaque;
    int64_t now;
    int64_t delta;

    if (size != sizeof(uint64_t)) {
        return;
    }

    switch (addr) {
    case IA64_WATCHDOG_CODE:
        s->watchdog_code = value;
        break;
    case IA64_WATCHDOG_TIMEOUT:
        s->watchdog_timeout = value;
        timer_del(s->watchdog_timer);
        if (value == 0) {
            break;
        }
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (value > (uint64_t)(INT64_MAX - now) / NANOSECONDS_PER_SECOND) {
            delta = INT64_MAX - now;
        } else {
            delta = value * NANOSECONDS_PER_SECOND;
        }
        timer_mod(s->watchdog_timer, now + delta);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ia64_vpc_watchdog_ops = {
    .read = ia64_vpc_watchdog_read,
    .write = ia64_vpc_watchdog_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void ia64_vpc_watchdog_reset(void *opaque)
{
    IA64VpcMachineState *s = opaque;

    timer_del(s->watchdog_timer);
    s->watchdog_timeout = 0;
    s->watchdog_code = 0;
}

static void ia64_vpc_init_watchdog(IA64VpcMachineState *s)
{
    s->watchdog_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     ia64_vpc_watchdog_expired, s);
    memory_region_init_io(&s->watchdog_mmio, OBJECT(s),
                          &ia64_vpc_watchdog_ops, s,
                          "ia64-vpc.firmware-watchdog",
                          IA64_WATCHDOG_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(),
                                        IA64_WATCHDOG_BASE,
                                        &s->watchdog_mmio, 2);
    qemu_register_reset(ia64_vpc_watchdog_reset, s);
}

static char *ia64_vpc_get_nvram(Object *obj, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    (void)errp;

    return g_strdup(s->nvram_path ?: "auto");
}

static void ia64_vpc_set_nvram(Object *obj, const char *value, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    (void)errp;

    g_free(s->nvram_path);
    s->nvram_path = g_strcmp0(value, "auto") == 0 ?
                    NULL : g_strdup(value);
}

static uint64_t ia64_vpc_nvram_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    IA64VpcMachineState *s = opaque;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)s->nvram_data[addr + i] << (i * 8);
    }
    return value;
}

static void ia64_vpc_nvram_commit(IA64VpcMachineState *s)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) read_err = NULL;
    g_autoptr(GError) err = NULL;
    const char *data;
    gsize length = 0;

    if (!s->nvram_resolved_path) {
        return;
    }
    if (g_file_get_contents(s->nvram_resolved_path, &contents,
                            &length, &read_err)) {
        if (length == IA64_NVRAM_EXTENDED_FILE_SIZE) {
            memcpy(contents, s->nvram_data, sizeof(s->nvram_data));
            data = contents;
        } else if (length == sizeof(s->nvram_data)) {
            data = (const char *)s->nvram_data;
        } else if (length == 0) {
            g_clear_pointer(&contents, g_free);
            contents = g_malloc0(IA64_NVRAM_EXTENDED_FILE_SIZE);
            memcpy(contents, s->nvram_data, sizeof(s->nvram_data));
            data = contents;
            length = IA64_NVRAM_EXTENDED_FILE_SIZE;
        } else {
            if (!s->nvram_write_warning) {
                warn_report("refusing to overwrite IA-64 NVRAM '%s': "
                            "expected %zu or %zu bytes, found %zu",
                            s->nvram_resolved_path,
                            sizeof(s->nvram_data),
                            (size_t)IA64_NVRAM_EXTENDED_FILE_SIZE,
                            (size_t)length);
                s->nvram_write_warning = true;
            }
            return;
        }
    } else if (!read_err ||
               !g_error_matches(read_err, G_FILE_ERROR,
                                G_FILE_ERROR_NOENT)) {
        if (!s->nvram_write_warning) {
            warn_report("failed to read IA-64 NVRAM '%s' before saving: %s",
                        s->nvram_resolved_path,
                        read_err ? read_err->message : "unknown error");
            s->nvram_write_warning = true;
        }
        return;
    } else {
        contents = g_malloc0(IA64_NVRAM_EXTENDED_FILE_SIZE);
        memcpy(contents, s->nvram_data, sizeof(s->nvram_data));
        data = contents;
        length = IA64_NVRAM_EXTENDED_FILE_SIZE;
    }
    if (!g_file_set_contents(s->nvram_resolved_path, data, length, &err) &&
        !s->nvram_write_warning) {
        warn_report("failed to save IA-64 NVRAM '%s': %s",
                    s->nvram_resolved_path,
                    err ? err->message : "unknown error");
        s->nvram_write_warning = true;
    }
}

static void ia64_vpc_nvram_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    IA64VpcMachineState *s = opaque;
    unsigned i;

    if (addr == IA64_NVRAM_COMMIT_OFFSET && size == 8 &&
        value == IA64_NVRAM_COMMIT_MAGIC) {
        ia64_vpc_nvram_commit(s);
        return;
    }
    for (i = 0; i < size; i++) {
        s->nvram_data[addr + i] = value >> (i * 8);
    }
}

static const MemoryRegionOps ia64_vpc_nvram_ops = {
    .read = ia64_vpc_nvram_read,
    .write = ia64_vpc_nvram_write,
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

static void ia64_vpc_init_nvram(IA64VpcMachineState *s)
{
    MachineState *machine = MACHINE(s);
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;
    gsize length = 0;

    memset(s->nvram_data, 0, sizeof(s->nvram_data));
    g_clear_pointer(&s->nvram_resolved_path, g_free);
    s->nvram_write_warning = false;

    if (g_strcmp0(s->nvram_path, "none") != 0) {
        s->nvram_resolved_path =
            ia64_machine_resolve_nvram_path(machine, s->nvram_path);
    }

    if (s->nvram_resolved_path &&
        g_file_get_contents(s->nvram_resolved_path, &contents,
                            &length, &err)) {
        if (length == 0) {
            /* Treat an empty file like a not-yet-created backing store. */
        } else if (length == sizeof(s->nvram_data) ||
            length == IA64_NVRAM_EXTENDED_FILE_SIZE) {
            memcpy(s->nvram_data, contents, sizeof(s->nvram_data));
        } else {
            warn_report("ignoring IA-64 NVRAM '%s': expected %zu or %zu "
                        "bytes, found %zu",
                        s->nvram_resolved_path,
                        sizeof(s->nvram_data),
                        (size_t)IA64_NVRAM_EXTENDED_FILE_SIZE,
                        (size_t)length);
        }
    } else if (err && !g_error_matches(err, G_FILE_ERROR,
                                       G_FILE_ERROR_NOENT)) {
        warn_report("failed to load IA-64 NVRAM '%s': %s",
                    s->nvram_resolved_path, err->message);
    }

    memory_region_init_io(&s->nvram_mmio, OBJECT(s),
                          &ia64_vpc_nvram_ops, s, "ia64-vpc.nvram",
                          IA64_NVRAM_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(), IA64_NVRAM_BASE,
                                        &s->nvram_mmio, 2);
}

typedef struct IA64VpcCompatDefault {
    const char *driver;
    const char *property;
    const char *value;
} IA64VpcCompatDefault;

static const IA64VpcCompatDefault ia64_vpc_compat_defaults[] = {
    /* Keep the VGA's EDID and the IA-64 VBE default at the legacy 1280x1024. */
    { "ati-vga", "xres", "1280" },
    { "ati-vga", "yres", "1024" },
    { "VGA", "xres", "1280" },
    { "VGA", "yres", "1024" },
    /*
     * Some IA-64 USB hub drivers use an alignment-requiring 32-bit load for
     * packed extended-property descriptors.  Do not expose the optional
     * selective-suspend property on HID input devices.
     */
    { "usb-kbd", "msos-desc", "off" },
    { "usb-mouse", "msos-desc", "off" },
    { "usb-tablet", "msos-desc", "off" },
};

static void ia64_vpc_add_compat_defaults(MachineClass *mc)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(ia64_vpc_compat_defaults); i++) {
        const IA64VpcCompatDefault *value = &ia64_vpc_compat_defaults[i];
        GlobalProperty *property = g_new0(GlobalProperty, 1);

        property->driver = value->driver;
        property->property = value->property;
        property->value = value->value;
        g_ptr_array_add(mc->compat_props, property);
    }
}

static bool ia64_vpc_get_i8042(Object *obj, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    (void)errp;

    return s->i8042_enabled;
}

static void ia64_vpc_set_i8042(Object *obj, bool value, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

#ifndef CONFIG_IA64_VPC_PS2
    if (value) {
        error_setg(errp, "i8042 support is not present in this build");
        return;
    }
#else
    (void)errp;
#endif

    s->i8042_enabled = value;
}

static bool ia64_vpc_get_firmware_ide_dma(Object *obj, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    (void)errp;

    return s->firmware_ide_dma;
}

static void ia64_vpc_set_firmware_ide_dma(Object *obj, bool value,
                                          Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

#ifndef CONFIG_IA64_VPC_STORAGE
    if (value) {
        error_setg(errp,
                   "firmware IDE DMA support is not present in this build");
        return;
    }
#else
    (void)errp;
#endif

    s->firmware_ide_dma = value;
}

static char *ia64_vpc_get_firmware_console(Object *obj, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    (void)errp;

    return g_strdup(s->firmware_console == IA64_FW_CONSOLE_VGA ?
                    "vga" : "serial");
}

static void ia64_vpc_set_firmware_console(Object *obj, const char *value,
                                          Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    if (g_strcmp0(value, "serial") == 0) {
        s->firmware_console = IA64_FW_CONSOLE_SERIAL;
        return;
    }
    if (g_strcmp0(value, "vga") == 0) {
#ifndef CONFIG_IA64_VPC_GRAPHICS
        error_setg(errp, "VGA support is not present in this build");
#else
        s->firmware_console = IA64_FW_CONSOLE_VGA;
#endif
        return;
    }

    error_setg(errp, "firmware-console must be 'serial' or 'vga'");
}

static char *ia64_vpc_get_alat(Object *obj, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);
    MachineState *machine = MACHINE(obj);

    (void)errp;

    return g_strdup(ia64_machine_effective_alat_full(machine, s->alat_full) ?
                    "full" : "zero");
}

static void ia64_vpc_set_alat(Object *obj, const char *value, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    if (g_strcmp0(value, "zero") == 0) {
        s->alat_full = false;
        return;
    }
    if (g_strcmp0(value, "full") == 0) {
        s->alat_full = true;
        return;
    }

    error_setg(errp, "alat must be 'zero' or 'full'");
}

static bool ia64_vpc_get_pcie(Object *obj, Error **errp)
{
    (void)errp;
    return IA64_VPC_MACHINE(obj)->pcie;
}

static void ia64_vpc_set_pcie(Object *obj, bool value, Error **errp)
{
    (void)errp;
    IA64_VPC_MACHINE(obj)->pcie = value;
}

static void ia64_vpc_acpi_update_sci(ACPIREGS *ar)
{
    IA64VpcMachineState *s = container_of(ar, IA64VpcMachineState,
                                          acpi_regs);

    acpi_update_sci(ar, s->acpi_sci_irq);
}

static uint64_t ia64_vpc_acpi_reset_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void ia64_vpc_acpi_reset_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    (void)opaque;
    if (addr == 0 && size == 1 &&
        (value & 0xff) == IA64_ACPI_PM_RESET_VALUE) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static const MemoryRegionOps ia64_vpc_acpi_reset_ops = {
    .read = ia64_vpc_acpi_reset_read,
    .write = ia64_vpc_acpi_reset_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void ia64_vpc_init_acpi_pm(IA64VpcMachineState *s,
                                  DeviceState *iosapic,
                                  MemoryRegion *pci_io)
{
    s->acpi_sci_irq = qdev_get_gpio_in(iosapic, IA64_ACPI_SCI_IRQ);

    memory_region_init(&s->acpi_pm, OBJECT(s), "ia64-acpi-pm",
                       IA64_ACPI_PM_IO_SIZE);
    memory_region_add_subregion(pci_io, IA64_ACPI_PM_IO_BASE,
                                &s->acpi_pm);

    acpi_pm1_evt_init(&s->acpi_regs, ia64_vpc_acpi_update_sci,
                      &s->acpi_pm);
    acpi_pm1_cnt_init(&s->acpi_regs, &s->acpi_pm,
                      false, false, 0, true);
    acpi_pm_tmr_init(&s->acpi_regs, ia64_vpc_acpi_update_sci,
                     &s->acpi_pm);
    memory_region_init_io(&s->acpi_reset, OBJECT(s),
                          &ia64_vpc_acpi_reset_ops, s,
                          "ia64-acpi-reset", 1);
    memory_region_add_subregion(&s->acpi_pm,
                                IA64_ACPI_PM_RESET_OFFSET,
                                &s->acpi_reset);

    /*
     * acpi_update_sci() always folds in GPE status.  The current platform
     * exposes no GPE block to the guest, but the shared ACPI core still needs
     * backing storage for that internal zero-valued contribution.
     */
    acpi_gpe_init(&s->acpi_regs, 2);
}

static void ia64_vpc_powerdown_req(Notifier *n, void *opaque)
{
    IA64VpcMachineState *s = container_of(n, IA64VpcMachineState,
                                          powerdown_notifier);

    (void)opaque;

    if (s->acpi_regs.pm1.evt.en & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        acpi_pm1_evt_power_down(&s->acpi_regs);
    } else {
        /* Avoid making QEMU's powerdown action a no-op before ACPI is armed. */
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

#ifdef CONFIG_IA64_VPC_GRAPHICS
static const VMStateDescription vmstate_ia64_int10_registers = {
    .name = "ia64-vpc/int10-registers",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(ax, IA64Int10Registers),
        VMSTATE_UINT16(bx, IA64Int10Registers),
        VMSTATE_UINT16(cx, IA64Int10Registers),
        VMSTATE_UINT16(dx, IA64Int10Registers),
        VMSTATE_UINT16(di, IA64Int10Registers),
        VMSTATE_UINT16(es, IA64Int10Registers),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static int ia64_vpc_post_load(void *opaque, int version_id)
{
    IA64VpcMachineState *s = opaque;
    uint16_t pm_enable = s->acpi_regs.pm1.evt.en;

#ifdef CONFIG_IA64_VPC_GRAPHICS
    if (s->int10_response_length > sizeof(s->int10_response) ||
        s->int10_response_offset > s->int10_response_length ||
        ((s->int10_response_length | s->int10_response_offset) & 1) != 0 ||
        s->int10_input_signature_words > 2) {
        return -EINVAL;
    }
#endif

    qemu_system_wakeup_enable(
        QEMU_WAKEUP_REASON_RTC,
        (pm_enable & ACPI_BITMASK_RT_CLOCK_ENABLE) != 0);
    qemu_system_wakeup_enable(
        QEMU_WAKEUP_REASON_PMTIMER,
        (pm_enable & ACPI_BITMASK_TIMER_ENABLE) != 0);
    ia64_vpc_acpi_update_sci(&s->acpi_regs);
    return 0;
}

static const VMStateDescription vmstate_ia64_vpc = {
    .name = "ia64-vpc",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ia64_vpc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(watchdog_timeout, IA64VpcMachineState),
        VMSTATE_UINT64(watchdog_code, IA64VpcMachineState),
        VMSTATE_TIMER_PTR(watchdog_timer, IA64VpcMachineState),
        VMSTATE_UINT8_ARRAY(nvram_data, IA64VpcMachineState,
                            IA64_NVRAM_SIZE),

        VMSTATE_UINT16(acpi_regs.pm1.evt.sts, IA64VpcMachineState),
        VMSTATE_UINT16(acpi_regs.pm1.evt.en, IA64VpcMachineState),
        VMSTATE_UINT16(acpi_regs.pm1.cnt.cnt, IA64VpcMachineState),
        VMSTATE_TIMER_PTR(acpi_regs.tmr.timer, IA64VpcMachineState),
        VMSTATE_INT64(acpi_regs.tmr.overflow_time, IA64VpcMachineState),
        VMSTATE_BUFFER_POINTER_UNSAFE(acpi_regs.gpe.sts,
                                      IA64VpcMachineState, 1, 2),
        VMSTATE_BUFFER_POINTER_UNSAFE(acpi_regs.gpe.en,
                                      IA64VpcMachineState, 1, 2),

#ifdef CONFIG_IA64_VPC_GRAPHICS
        VMSTATE_STRUCT(int10_request, IA64VpcMachineState, 1,
                       vmstate_ia64_int10_registers, IA64Int10Registers),
        VMSTATE_STRUCT(int10_result, IA64VpcMachineState, 1,
                       vmstate_ia64_int10_registers, IA64Int10Registers),
        VMSTATE_UINT32(int10_input_signature, IA64VpcMachineState),
        VMSTATE_UINT8_ARRAY(int10_response, IA64VpcMachineState, 512),
        VMSTATE_UINT16(int10_response_length, IA64VpcMachineState),
        VMSTATE_UINT16(int10_response_offset, IA64VpcMachineState),
        VMSTATE_UINT8(int10_input_signature_words, IA64VpcMachineState),
        VMSTATE_UINT8(int10_dpms_state, IA64VpcMachineState),
        VMSTATE_UINT8(int10_legacy_mode, IA64VpcMachineState),
        VMSTATE_UINT8(int10_legacy_columns, IA64VpcMachineState),
#endif
        VMSTATE_END_OF_LIST()
    }
};

static bool ia64_vpc_map_firmware_address_space(IA64VpcMachineState *s,
                                                Error **errp)
{
    Error *local_err = NULL;

    /*
     * IA-64 reserves the top 16 MiB below 4 GiB for PAL/SAL firmware
     * resources.  Decode it so firmware identity mappings can use the
     * platform address space directly.
     */
    memory_region_init_ram(&s->firmware_space, NULL,
                           "ia64-firmware-address-space",
                           IA64_FIRMWARE_ADDRESS_SPACE_SIZE, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }
    memory_region_add_subregion_overlap(get_system_memory(),
                                        IA64_FIRMWARE_ADDRESS_SPACE_BASE,
                                        &s->firmware_space, 1);
    return true;
}

static uint64_t ia64_vpc_map_ram_alias(IA64VpcMachineState *s,
                                       hwaddr guest_base,
                                       uint64_t backing_offset,
                                       uint64_t remaining,
                                       uint64_t capacity,
                                       const char *name)
{
    MachineState *machine = MACHINE(s);
    MemoryRegion *alias;
    uint64_t size = MIN(remaining, capacity);

    if (size == 0) {
        return 0;
    }

    g_assert(s->ram_alias_count < ARRAY_SIZE(s->ram_aliases));
    alias = g_new(MemoryRegion, 1);
    s->ram_aliases[s->ram_alias_count++] = alias;
    memory_region_init_alias(alias, OBJECT(s), name, machine->ram,
                             backing_offset, size);
    memory_region_add_subregion(get_system_memory(), guest_base, alias);
    return size;
}

static void ia64_vpc_map_ram(IA64VpcMachineState *s)
{
    MachineState *machine = MACHINE(s);
    uint64_t remaining = machine->ram_size;
    uint64_t offset = 0;
    uint64_t size;

    if (machine->ram == NULL) {
        return;
    }

    /*
     * Keep the RAM backing densely packed while leaving the platform's
     * firmware and PCI apertures free in the guest physical address space.
     * RAM displaced by those apertures is mapped above 4 GiB instead of
     * being hidden by higher-priority device regions.
     */
    size = ia64_vpc_map_ram_alias(s, 0, offset, remaining,
                                  IA64_LOW_RAM_LIMIT,
                                  "ia64-vpc.low-ram");
    offset += size;
    remaining -= size;

    size = ia64_vpc_map_ram_alias(s, IA64_HIGH_RAM_BASE, offset,
                                  remaining,
                                  IA64_PCI_MMIO_BASE - IA64_HIGH_RAM_BASE,
                                  "ia64-vpc.high-ram-below-pci");
    offset += size;
    remaining -= size;

    size = ia64_vpc_map_ram_alias(
        s, IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE, offset, remaining,
        IA64_RAS_HUB_DEFAULT_BASE -
            (IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE),
        "ia64-vpc.high-ram-above-pci");
    offset += size;
    remaining -= size;

    size = ia64_vpc_map_ram_alias(
        s, IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_HUB_SIZE,
        offset, remaining,
        IA64_LOCAL_SAPIC_PA -
            (IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_HUB_SIZE),
        "ia64-vpc.high-ram-above-ras");
    offset += size;
    remaining -= size;

    size = ia64_vpc_map_ram_alias(
        s, IA64_HIGH_RAM_AFTER_FIRMWARE_BASE, offset, remaining,
        IA64_PCI_IO_BASE - IA64_HIGH_RAM_AFTER_FIRMWARE_BASE,
        "ia64-vpc.high-ram-above-4g");
    g_assert(size == remaining);
}

static void ia64_vpc_write_firmware_handoff(IA64VpcMachineState *s)
{
    MachineState *machine = MACHINE(s);
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_GET_CLASS(s);
    IA64VpcHandoff handoff = { 0 };
    IA64VpcCompatHandoff compat = { 0 };
    bool debug_port_present = debug_port_get_chardev() != NULL;

    _Static_assert(sizeof(IA64VpcHandoff) == 120,
                   "IA-64 firmware handoff ABI size changed");
    _Static_assert(offsetof(IA64VpcHandoff, ProcessorCount) == 64,
                   "IA-64 firmware handoff CPU count offset changed");
    _Static_assert(offsetof(IA64VpcHandoff, NvramPersistent) == 72,
                   "IA-64 firmware handoff NVRAM offset changed");
    _Static_assert(offsetof(IA64VpcHandoff, SocketCount) == 80,
                   "IA-64 firmware handoff socket count offset changed");
    _Static_assert(offsetof(IA64VpcHandoff, CoresPerSocket) == 88,
                   "IA-64 firmware handoff core count offset changed");
    _Static_assert(offsetof(IA64VpcHandoff, ThreadsPerCore) == 96,
                   "IA-64 firmware handoff thread count offset changed");
    _Static_assert(sizeof(IA64VpcCompatHandoff) == 32,
                   "IA-64 firmware compatibility handoff ABI size changed");

    handoff.Magic = cpu_to_le64(IA64_FW_HANDOFF_MAGIC);
    handoff.Version = cpu_to_le64(IA64_FW_HANDOFF_VERSION);
    handoff.RamSize = cpu_to_le64(machine->ram_size);
    handoff.ConsolePolicy = cpu_to_le64(s->firmware_console);
    handoff.IdeDmaEnabled = cpu_to_le64(s->firmware_ide_dma);
    handoff.DebugPortFlags = cpu_to_le64(
        debug_port_present ? IA64_FW_DEBUG_PORT_PRESENT : 0);
    handoff.DebugPortBase = cpu_to_le64(
        debug_port_present ? IA64_DEBUG_UART_BASE : 0);
    handoff.I8042Enabled = cpu_to_le64(s->i8042_enabled);
    handoff.ProcessorCount = cpu_to_le64(machine->smp.cpus);
    handoff.NvramPersistent = cpu_to_le64(
        s->nvram_resolved_path != NULL);
    handoff.SocketCount = cpu_to_le64(machine->smp.sockets);
    handoff.CoresPerSocket = cpu_to_le64(machine->smp.cores);
    handoff.ThreadsPerCore = cpu_to_le64(machine->smp.threads);
    handoff.RasBase = cpu_to_le64(IA64_RAS_HUB_DEFAULT_BASE);
    handoff.RasSize = cpu_to_le64(IA64_RAS_HUB_SIZE);
    cpu_physical_memory_write(IA64_FW_HANDOFF_ADDR, &handoff,
                              sizeof(handoff));

    compat.Magic = cpu_to_le64(IA64_FW_COMPAT_HANDOFF_MAGIC);
    compat.Version = cpu_to_le64(IA64_FW_COMPAT_HANDOFF_VERSION);
    compat.Size = cpu_to_le64(sizeof(compat));
    compat.Flags = cpu_to_le64(ivmc->firmware_compat_flags);
    cpu_physical_memory_write(IA64_FW_COMPAT_HANDOFF_ADDR, &compat,
                              sizeof(compat));
}

static void ia64_vpc_configure_pci_irq(PCIDevice *pci_dev)
{
    uint8_t pin;

    if (pci_dev == NULL) {
        return;
    }

    pin = pci_dev->config[PCI_INTERRUPT_PIN];
    if (pin >= 1 && pin <= PCI_NUM_PINS) {
        pci_default_write_config(pci_dev, PCI_INTERRUPT_LINE,
                                 ia64_pci_route_intx_gsi(pci_dev->devfn,
                                                         pin - 1), 1);
    }
}

static void ia64_vpc_configure_ahci(PCIDevice *pci_dev)
{
    if (pci_dev == NULL) {
        return;
    }

    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_4,
                             IA64_AHCI_IDP_IO_BASE, 4);
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_5,
                             IA64_AHCI_MMIO_PCI_BASE, 4);
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                             PCI_COMMAND_MASTER, 2);
}

static void ia64_vpc_configure_ohci(PCIDevice *pci_dev)
{
    if (pci_dev == NULL) {
        return;
    }

    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0,
                             IA64_OHCI_MMIO_PCI_BASE, 4);
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, 2);
}

static void ia64_vpc_configure_uhci(PCIDevice *pci_dev)
{
    if (pci_dev == NULL) {
        return;
    }

    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_4,
                             IA64_UHCI_IO_BASE, 4);
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_IO | PCI_COMMAND_MASTER, 2);
}

static void ia64_vpc_configure_lsi(PCIDevice *pci_dev)
{
    if (pci_dev == NULL) {
        return;
    }

    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0,
                             IA64_LSI_IO_BASE, 4);
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_1,
                             IA64_LSI_MMIO_PCI_BASE, 4);
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_2,
                             IA64_LSI_RAM_PCI_BASE, 4);
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                             PCI_COMMAND_MASTER, 2);
}

static void ia64_vpc_configure_vga(PCIDevice *pci_dev)
{
    if (pci_dev == NULL) {
        return;
    }

    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0,
                             ia64_vpc_vga_fb_pci_base(pci_dev), 4);
    if (pci_dev->io_regions[1].memory != NULL) {
        pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0 + 4,
                                 IA64_VGA_IO_BASE, 4);
    }
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0 + 8,
                             ia64_vpc_vga_mmio_pci_base(pci_dev), 4);
    if (pci_get_word(pci_dev->config + PCI_VENDOR_ID) ==
            IA64_ATI_VENDOR_ID &&
        pci_get_word(pci_dev->config + PCI_DEVICE_ID) ==
            IA64_ATI_ES1000_DEVICE_ID) {
        pci_default_write_config(pci_dev, PCI_CACHE_LINE_SIZE, 0x10, 1);
        pci_default_write_config(pci_dev, PCI_LATENCY_TIMER, 0x40, 1);
    }
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY, 2);

}

static bool ia64_vpc_enable_vga_legacy_switch(PCIDevice *pci_dev,
                                               Error **errp)
{
    if (pci_dev == NULL ||
        !object_property_find(OBJECT(pci_dev),
                              "x-vbe-legacy-mode-switch")) {
        return true;
    }

    return object_property_set_bool(OBJECT(pci_dev),
                                    "x-vbe-legacy-mode-switch", true,
                                    errp);
}

static void ia64_vpc_configure_nic(PCIDevice *pci_dev, unsigned int index)
{
    uint64_t mmio_base;
    uint32_t io_base;

    if (pci_dev == NULL || index >= MAX_NICS) {
        return;
    }

    mmio_base = IA64_E1000_MMIO_PCI_BASE +
                index * IA64_E1000_MMIO_SIZE;
    io_base = IA64_E1000_IO_BASE + index * IA64_E1000_IO_SIZE;
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_0, mmio_base, 4);
    pci_default_write_config(pci_dev, PCI_BASE_ADDRESS_1, io_base, 4);
    pci_default_write_config(pci_dev, PCI_COMMAND,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                             PCI_COMMAND_MASTER, 2);
}

static void ia64_vpc_configure_platform_pci(IA64VpcMachineState *s)
{
    ia64_vpc_configure_ahci(s->ahci_dev);
    ia64_vpc_configure_ohci(s->ohci_dev);
    ia64_vpc_configure_uhci(s->uhci_dev);
    ia64_vpc_configure_lsi(s->lsi_dev);
    ia64_vpc_configure_vga(s->vga_dev);
    for (unsigned int i = 0; i < s->nic_count; i++) {
        ia64_vpc_configure_nic(s->nic_devs[i], i);
    }
    ia64_vpc_configure_pci_irq(s->ahci_dev);
    ia64_vpc_configure_pci_irq(s->ohci_dev);
    ia64_vpc_configure_pci_irq(s->uhci_dev);
    ia64_vpc_configure_pci_irq(s->lsi_dev);
    ia64_vpc_configure_pci_irq(s->vga_dev);
    for (unsigned int i = 0; i < s->nic_count; i++) {
        ia64_vpc_configure_pci_irq(s->nic_devs[i]);
    }
}

#ifdef CONFIG_IA64_VPC_NETWORK
static void ia64_vpc_record_nic(IA64VpcMachineState *s, PCIBus *bus,
                                PCIDevice *pci_dev)
{
    uint16_t class;

    if (pci_dev == NULL || s->nic_count >= MAX_NICS) {
        return;
    }

    class = pci_get_word(pci_dev->config + PCI_CLASS_DEVICE);
    if (class != PCI_CLASS_NETWORK_ETHERNET ||
        pci_dev->io_regions[0].size != IA64_E1000_MMIO_SIZE ||
        pci_dev->io_regions[1].size != IA64_E1000_IO_SIZE ||
        pci_get_bus(pci_dev) != bus) {
        return;
    }

    s->nic_devs[s->nic_count] = pci_dev;
    ia64_vpc_configure_nic(pci_dev, s->nic_count);
    ia64_vpc_configure_pci_irq(pci_dev);
    s->nic_count++;
}

static void ia64_vpc_init_network(IA64VpcMachineState *s, PCIBus *pci_bus)
{
    MachineState *machine = MACHINE(s);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    unsigned int slot;

    s->nic_count = 0;
    memset(s->nic_devs, 0, sizeof(s->nic_devs));

    /* Keep the default adapter at a stable BDF after the built-in devices. */
    pci_init_nic_in_slot(pci_bus, mc->default_nic, NULL,
                         stringify(IA64_VPC_NIC_SLOT));
    pci_init_nic_devices(pci_bus, mc->default_nic);

    for (slot = IA64_VPC_NIC_SLOT; slot < PCI_SLOT_MAX; slot++) {
        ia64_vpc_record_nic(s, pci_bus,
                            pci_find_device(pci_bus, 0, PCI_DEVFN(slot, 0)));
    }
}
#endif

#define TYPE_IA64_PCI_FIXUP_RESET "ia64-pci-fixup-reset"
OBJECT_DECLARE_SIMPLE_TYPE(IA64PciFixupReset, IA64_PCI_FIXUP_RESET)

struct IA64PciFixupReset {
    Object parent;
    ResettableState reset_state;
    IA64VpcMachineState *machine;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(
    IA64PciFixupReset, ia64_pci_fixup_reset, IA64_PCI_FIXUP_RESET, OBJECT,
    { TYPE_RESETTABLE_INTERFACE }, { })

static ResettableState *ia64_pci_fixup_reset_get_state(Object *obj)
{
    IA64PciFixupReset *s = IA64_PCI_FIXUP_RESET(obj);

    return &s->reset_state;
}

static void ia64_pci_fixup_reset_exit(Object *obj, ResetType type)
{
    IA64PciFixupReset *r = IA64_PCI_FIXUP_RESET(obj);

    (void)type;

    ia64_vpc_configure_platform_pci(r->machine);
}

static void ia64_pci_fixup_reset_class_init(ObjectClass *klass,
                                            const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    (void)data;
    rc->get_state = ia64_pci_fixup_reset_get_state;
    rc->phases.exit = ia64_pci_fixup_reset_exit;
}

static void ia64_pci_fixup_reset_init(Object *obj)
{
    (void)obj;
}

static void ia64_pci_fixup_reset_finalize(Object *obj)
{
    (void)obj;
}

static void ia64_vpc_map_vga_fixed_windows(IA64VpcMachineState *s,
                                           PCIDevice *pci_dev)
{
    PCIIORegion *fb;
    PCIIORegion *mmio;

    if (pci_dev == NULL) {
        return;
    }

    fb = &pci_dev->io_regions[0];
    mmio = &pci_dev->io_regions[2];
    if (fb->memory == NULL || mmio->memory == NULL ||
        fb->address_space == NULL || mmio->address_space == NULL) {
        return;
    }

    if (fb->address_space != mmio->address_space) {
        return;
    }

    if (s->vga_fb_alias == NULL) {
        s->vga_fb_alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(s->vga_fb_alias, OBJECT(s),
                                 "ia64-vga-fb-fixed", fb->memory, 0, fb->size);
        memory_region_add_subregion_overlap(fb->address_space,
                                            ia64_vpc_vga_fb_pci_base(pci_dev),
                                            s->vga_fb_alias, 1);
    }

    if (s->vga_mmio_alias == NULL) {
        s->vga_mmio_alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(s->vga_mmio_alias, OBJECT(s),
                                 "ia64-vga-mmio-fixed", mmio->memory, 0,
                                 mmio->size);
        memory_region_add_subregion_overlap(fb->address_space,
                                            ia64_vpc_vga_mmio_pci_base(pci_dev),
                                            s->vga_mmio_alias, 1);
    }

    if (s->vga_legacy_alias == NULL) {
        s->vga_legacy_alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(s->vga_legacy_alias,
                                 OBJECT(s),
                                 "ia64-vga-legacy-fixed",
                                 fb->address_space,
                                 IA64_VGA_LEGACY_BASE,
                                 IA64_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(get_system_memory(),
                                            IA64_VGA_LEGACY_BASE,
                                            s->vga_legacy_alias, 1);
    }
}

#ifdef CONFIG_IA64_VPC_USB
static bool ia64_vpc_init_usb(IA64VpcMachineState *s, PCIBus *pci_bus,
                              Error **errp)
{
    MachineState *machine = MACHINE(s);
    USBBus *usb_bus;
    bool add_default_input;

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (!machine->usb) {
        return true;
    }

    s->ohci_dev = pci_create_simple(pci_bus, -1, "pci-ohci");
    ia64_vpc_configure_ohci(s->ohci_dev);

    add_default_input = defaults_enabled() && !s->i8042_enabled;
    if (add_default_input) {
        /*
         * Attach default USB input only when PS/2 is disabled. HID keyboards
         * become QEMU's active input handler, which would otherwise hide
         * firmware-visible PS/2 input before a guest USB stack exists.  Use
         * an absolute pointer so graphical front ends do not require a
         * relative-pointer grab.
         */
        usb_bus = USB_BUS(object_resolve_type_unambiguous(TYPE_USB_BUS,
                                                          errp));
        if (usb_bus == NULL) {
            return false;
        }
        usb_create_simple(usb_bus, "usb-kbd");
        usb_create_simple(usb_bus, "usb-tablet");
    }

    s->uhci_dev = pci_create_simple(pci_bus, -1, TYPE_PIIX3_USB_UHCI);
    ia64_vpc_configure_uhci(s->uhci_dev);
    return true;
}
#endif

static IA64BootInfo ia64_vpc_boot_info(unsigned int cpu_index,
                                       uint64_t entry,
                                       uint64_t global_pointer,
                                       uint64_t low_ram_size)
{
    uint64_t cpu_assist_base = low_ram_size - IA64_FW_BOOT_STACK_SIZE;
    IA64BootInfo info = {
        .firmware_base = IA64_FW_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = IA64_IVT_BASE,
        .bsp = cpu_assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        .stack_pointer = cpu_assist_base +
            IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_size,
        .powered_off = cpu_index != 0,
    };

    return info;
}

static IA64BootInfo ia64_vpc_initial_boot_info(unsigned int cpu_index,
                                               void *opaque)
{
    MachineState *machine = opaque;

    return ia64_vpc_boot_info(cpu_index, IA64_FW_BASE, IA64_FW_BASE,
                              MIN(machine->ram_size, IA64_LOW_RAM_LIMIT));
}

static IA64BootInfo ia64_vpc_firmware_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    MachineState *machine = opaque;

    return ia64_vpc_boot_info(cpu_index, entry, global_pointer,
                              MIN(machine->ram_size, IA64_LOW_RAM_LIMIT));
}

/*
 * CPU state initialization — called on every reset.
 *
 * Sets up the CPU in physical mode with firmware entry point.
 * Note: ROM content is loaded by rom_reset() which may run before or
 * after this handler, so we must NOT read ROM content here.  PE32+
 * plabel parsing is deferred to the machine_done notifier.
 */
static void ia64_vpc_reset(void *opaque)
{
    IA64VpcMachineState *s = opaque;

    ia64_machine_reset_cpus();

    acpi_pm1_evt_reset(&s->acpi_regs);
    acpi_pm1_cnt_reset(&s->acpi_regs);
    acpi_pm_tmr_reset(&s->acpi_regs);
    acpi_gpe_reset(&s->acpi_regs);
#ifdef CONFIG_IA64_VPC_GRAPHICS
    if (s->vga_dev != NULL) {
        ia64_vpc_reset_int10(s);
    }
#endif
}

static void ia64_vpc_machine_done(void *opaque)
{
    IA64VpcMachineState *s = opaque;

    ia64_vpc_configure_platform_pci(s);
}

static bool ia64_vpc_validate_configuration(MachineState *machine,
                                            IA64VpcMachineState *s,
                                            Error **errp)
{
    if (machine->ram_size < IA64_FW_LOW_RAM_MIN) {
        g_autofree char *size = size_to_str(IA64_FW_LOW_RAM_MIN);

        error_setg(errp, "Invalid RAM size, should be at least %s", size);
        return false;
    }
    if (machine->ram_size > IA64_VPC_MAX_RAM_SIZE) {
        g_autofree char *size = size_to_str(IA64_VPC_MAX_RAM_SIZE);

        error_setg(errp, "Invalid RAM size, should be at most %s", size);
        return false;
    }
    return true;
}

static bool ia64_vpc_build(MachineState *machine, Error **errp)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(machine);
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_GET_CLASS(s);
    IA64MachineCpuConfig cpu_config = {
        .alat_full = s->alat_full,
        .firmware_compat_flags = ivmc->firmware_compat_flags,
        .boot_info = ia64_vpc_initial_boot_info,
        .boot_info_opaque = machine,
    };
    DeviceState *pci_host;
    DeviceState *iosapic;
    SerialMM *primary_uart;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    MemoryRegion *pci_io;
#ifdef CONFIG_IA64_VPC_STORAGE
    DriveInfo *sata_drives[6] = { NULL };
    AHCIPCIState *ahci;
#endif
    uint32_t reserved_pci_slots = 1U << 0;
    int i;

    if (!ia64_vpc_validate_configuration(machine, s, errp)) {
        return false;
    }

    ia64_vpc_map_ram(s);
    if (!ia64_vpc_map_firmware_address_space(s, errp)) {
        return false;
    }
    s->ras = ia64_ras_hub_create(
        OBJECT(s), "ras", IA64_RAS_HUB_DEFAULT_BASE, errp);
    if (!s->ras) {
        return false;
    }
    ia64_vpc_init_rtc(s);
    ia64_vpc_init_watchdog(s);
    ia64_vpc_init_nvram(s);
    ia64_vpc_write_firmware_handoff(s);

    if (!ia64_machine_create_cpus(machine, &cpu_config, errp)) {
        return false;
    }
    ia64_machine_map_pib(OBJECT(s), &s->lsapic_mmio,
                         "ia64-vpc.local-sapic", IA64_LOCAL_SAPIC_PA,
                         IA64_LOCAL_SAPIC_SIZE);

    iosapic = qdev_new(TYPE_IA64_IOSAPIC);
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(iosapic), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(iosapic), 0, IA64_IOSAPIC_BASE);

    primary_uart = serial_mm_init(get_system_memory(), IA64_UART_BASE, 0,
                                  qdev_get_gpio_in(iosapic, 4),
                                  115200, serial_hd(0),
                                  DEVICE_LITTLE_ENDIAN);
    if (debug_port_get_chardev()) {
        serial_mm_init(get_system_memory(), IA64_DEBUG_UART_BASE, 0,
                       qdev_get_gpio_in(iosapic, 3),
                       115200, debug_port_get_chardev(),
                       DEVICE_LITTLE_ENDIAN);
    }

    if (!ia64_machine_load_firmware(machine, IA64_FW_BASE,
                                    machine->ram_size - IA64_FW_BASE,
                                    &s->firmware_size, errp)) {
        return false;
    }

    /* Fill IVT with break bundles (one-time, before any reset) */
    {
        uint64_t break_bundle[2] = {0, 0};
        hwaddr offset;

        for (offset = 0; offset < IA64_IVT_SIZE; offset += 16) {
            cpu_physical_memory_write(IA64_IVT_BASE + offset,
                                      break_bundle, 16);
        }
    }

    /* Defer PE32+ plabel parsing until after ROM content is loaded. */
    ia64_machine_init_firmware_notifier(
        &s->firmware_notifier, machine, IA64_FW_BASE, s->firmware_size,
        ia64_vpc_firmware_boot_info, ia64_vpc_machine_done, s);

    pci_host = qdev_new(s->pcie ? TYPE_IA64_PCIE_HOST_BRIDGE :
                                  TYPE_IA64_PCI_HOST_BRIDGE);
    if (s->pcie &&
        !ia64_pcie_host_set_fault_notifier(
            IA64_PCI_HOST_BRIDGE(pci_host),
            ia64_ras_hub_report_chipset_fault, s->ras, errp)) {
        object_unref(OBJECT(pci_host));
        return false;
    }
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(pci_host), errp)) {
        return false;
    }
    pci_bus = PCI_HOST_BRIDGE(pci_host)->bus;

    /* Reserve the empty slot and the optional AHCI slot during device setup. */
#ifdef CONFIG_IA64_VPC_STORAGE
    if (!ivmc->default_ahci) {
        reserved_pci_slots |= 1U << 1;
    }
#endif
    pci_bus_set_slot_reserved_mask(pci_bus, reserved_pci_slots);
    pci_io = pci_bus->address_space_io;

    /*
     * Map COM1 through sparse I/O and alias the same SerialMM region at
     * IA64_UART_BASE.  ACPI and HCDP advertise only the I/O resource.
     */
    memory_region_init_alias(&s->uart_io_alias, OBJECT(s), "uart-io-alias",
                             sysbus_mmio_get_region(
                                 SYS_BUS_DEVICE(primary_uart), 0),
                             0, IA64_UART_IO_SIZE);
    memory_region_add_subregion(pci_io, IA64_UART_IO_PORT,
                                &s->uart_io_alias);

    ia64_vpc_init_acpi_pm(s, iosapic, pci_io);

    /* Leave ISA/SCI lines in the legacy range and route PCI INTx above 15. */
    for (i = 0; i < IA64_PCI_INTX_LINES; i++) {
        qdev_connect_gpio_out(pci_host, i,
                              qdev_get_gpio_in(iosapic,
                                               IA64_PCI_INTX_GSI_BASE + i));
    }

    /* The Itanium 2 profile adds AHCI; firmware boots from LSI SCSI below. */
#ifdef CONFIG_IA64_VPC_STORAGE
    if (ivmc->default_ahci) {
        s->ahci_dev = pci_create_simple(pci_bus, -1, TYPE_ICH9_AHCI);
        ia64_vpc_configure_ahci(s->ahci_dev);
        ahci = ICH9_AHCI(s->ahci_dev);
        g_assert(ahci->ahci.ports <= ARRAY_SIZE(sata_drives));
        ide_drive_get(sata_drives, ahci->ahci.ports);
        ahci_ide_create_devs(&ahci->ahci, sata_drives);
    }
#endif

    isa_bus = isa_bus_new(NULL, get_system_memory(), pci_io, errp);
    if (isa_bus == NULL) {
        return false;
    }
    for (i = 0; i < ISA_NUM_IRQS; i++) {
        s->isa_irqs[i] = qdev_get_gpio_in(iosapic, i);
    }
    isa_bus_register_input_irqs(isa_bus, s->isa_irqs);
#ifdef CONFIG_IA64_VPC_PS2
    if (s->i8042_enabled) {
        isa_create_simple(isa_bus, TYPE_I8042);
    }
#endif

#ifdef CONFIG_IA64_VPC_USB
    if (!ia64_vpc_init_usb(s, pci_bus, errp)) {
        return false;
    }
#endif

    /* Put the SCSI HBA on device 4. */
#ifdef CONFIG_IA64_VPC_STORAGE
    s->lsi_dev = pci_new(PCI_DEVFN(4, 0), "lsi53c895a");
    qdev_prop_set_bit(DEVICE(s->lsi_dev),
                      "disconnect-on-data-wait", false);
    if (!pci_realize_and_unref(s->lsi_dev, pci_bus, errp)) {
        return false;
    }
    ia64_vpc_configure_lsi(s->lsi_dev);
    lsi53c8xx_handle_legacy_cmdline(DEVICE(s->lsi_dev));
#endif

#ifdef CONFIG_IA64_VPC_GRAPHICS
    s->vga_dev = pci_vga_new();
    if (s->vga_dev != NULL) {
        if (!ia64_vpc_prepare_vga_properties(s, s->vga_dev, errp)) {
            object_unref(OBJECT(s->vga_dev));
            s->vga_dev = NULL;
            return false;
        }
        if (!pci_realize_and_unref(s->vga_dev, pci_bus, errp)) {
            s->vga_dev = NULL;
            return false;
        }
        if (!ia64_vpc_build_vbe_config(s, errp)) {
            return false;
        }
    }
#endif
    if (!ia64_vpc_enable_vga_legacy_switch(s->vga_dev, errp)) {
        return false;
    }
    ia64_vpc_configure_vga(s->vga_dev);
    ia64_vpc_map_vga_fixed_windows(s, s->vga_dev);
#ifdef CONFIG_IA64_VPC_GRAPHICS
    if (s->vga_dev != NULL) {
        ia64_vpc_init_int10(s, pci_io);
    }
#endif
#ifdef CONFIG_IA64_VPC_NETWORK
    ia64_vpc_init_network(s, pci_bus);
#endif
    pci_bus_clear_slot_reserved_mask(pci_bus, reserved_pci_slots);

    s->powerdown_notifier.notify = ia64_vpc_powerdown_req;
    qemu_register_powerdown_notifier(&s->powerdown_notifier);

    qemu_register_reset(ia64_vpc_reset, s);
    s->pci_fixup_reset = object_new(TYPE_IA64_PCI_FIXUP_RESET);
    IA64_PCI_FIXUP_RESET(s->pci_fixup_reset)->machine = s;
    qemu_register_resettable(s->pci_fixup_reset);
    if (vmstate_register_with_alias_id(NULL, 0, &vmstate_ia64_vpc, s,
                                       -1, 0, errp) < 0) {
        return false;
    }
    s->vmstate_registered = true;
    return true;
}

static void ia64_vpc_init(MachineState *machine)
{
    Error *err = NULL;

    /* 460GX core devices are command-line creatable only under qtest. */
    if (qtest_enabled()) {
#ifdef CONFIG_IA64_460GX_HOST
        if (!device_type_is_dynamic_sysbus(MACHINE_GET_CLASS(machine),
                                           TYPE_INTEL_460GX_HOST)) {
            machine_class_allow_dynamic_sysbus_dev(MACHINE_GET_CLASS(machine),
                                                   TYPE_INTEL_460GX_HOST);
        }
#endif
#ifdef CONFIG_IA64_460GX_PID
        if (!device_type_is_dynamic_sysbus(MACHINE_GET_CLASS(machine),
                                           TYPE_INTEL_460GX_PID)) {
            machine_class_allow_dynamic_sysbus_dev(MACHINE_GET_CLASS(machine),
                                                   TYPE_INTEL_460GX_PID);
        }
#endif
#ifdef CONFIG_IA64_460GX_DMA_TEST
        if (!device_type_is_dynamic_sysbus(
                MACHINE_GET_CLASS(machine),
                TYPE_INTEL_460GX_DMA_TEST_HOST)) {
            machine_class_allow_dynamic_sysbus_dev(
                MACHINE_GET_CLASS(machine),
                TYPE_INTEL_460GX_DMA_TEST_HOST);
        }
#endif
    }

    if (!ia64_vpc_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void ia64_vpc_machine_instance_init(Object *obj)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    s->alat_full = false;

#ifdef CONFIG_IA64_VPC_PS2
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_GET_CLASS(obj);

    s->i8042_enabled = ivmc->default_i8042;
#endif
#ifdef CONFIG_IA64_VPC_STORAGE
    s->firmware_ide_dma = true;
#endif
#ifdef CONFIG_IA64_VPC_GRAPHICS
    s->firmware_console = IA64_FW_CONSOLE_VGA;
#else
    s->firmware_console = IA64_FW_CONSOLE_SERIAL;
#endif
}

static void ia64_vpc_machine_instance_finalize(Object *obj)
{
    IA64VpcMachineState *s = IA64_VPC_MACHINE(obj);

    ia64_machine_cleanup_firmware_notifier(&s->firmware_notifier);
    if (s->vmstate_registered) {
        vmstate_unregister(NULL, &vmstate_ia64_vpc, s);
    }
    g_free(s->nvram_path);
    g_free(s->nvram_resolved_path);
}

static void ia64_vpc_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_CLASS(oc);

    (void)data;

    mc->init = ia64_vpc_init;
    mc->max_cpus = IA64_VPC_MAX_CPUS;
    mc->default_cpus = 1;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("montecito");
    mc->smp_props.prefer_sockets = true;
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "ia64-vpc.ram";
    mc->default_machine_opts = "firmware=ia64-firmware.bin";
#ifdef CONFIG_IA64_VPC_GRAPHICS
    mc->default_display = "ati";
#endif
#ifdef CONFIG_IA64_VPC_NETWORK
    mc->default_nic = "e1000";
#endif
#ifdef CONFIG_IA64_VPC_STORAGE
    mc->block_default_type = IF_SCSI;
#else
    mc->block_default_type = IF_NONE;
#endif
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    ivmc->default_i8042 = true;

    object_class_property_add_bool(oc, "i8042",
                                   ia64_vpc_get_i8042,
                                   ia64_vpc_set_i8042);
    object_class_property_set_description(oc, "i8042",
        "Set on/off to enable/disable the i8042 PS/2 controller");
    object_class_property_add_bool(oc, "firmware-ide-dma",
                                   ia64_vpc_get_firmware_ide_dma,
                                   ia64_vpc_set_firmware_ide_dma);
    object_class_property_set_description(oc, "firmware-ide-dma",
        "Set on/off to enable/disable firmware IDE bus-master DMA");
    object_class_property_add_str(oc, "firmware-console",
                                  ia64_vpc_get_firmware_console,
                                  ia64_vpc_set_firmware_console);
    object_class_property_set_description(oc, "firmware-console",
        "Set firmware HCDP primary console to 'serial' or 'vga'");
    object_class_property_add_str(oc, "nvram",
                                  ia64_vpc_get_nvram,
                                  ia64_vpc_set_nvram);
    object_class_property_set_description(oc, "nvram",
        "Set the IA-64 EFI NVRAM file path, 'auto', or 'none'");
    object_class_property_add_str(oc, "alat",
                                  ia64_vpc_get_alat,
                                  ia64_vpc_set_alat);
    object_class_property_set_description(oc, "alat",
        "Set the IA-64 ALAT model to 'zero' (default) or 'full'");
    object_class_property_add_bool(oc, "pcie",
                                   ia64_vpc_get_pcie,
                                   ia64_vpc_set_pcie);
    object_class_property_set_description(oc, "pcie",
        "Use a PCI Express root bus with extended configuration space");
}

static void itanium_vpc_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_CLASS(oc);

    (void)data;

    mc->desc = "IA-64 virtual PC with Merced CPU default";
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("merced");
    ivmc->firmware_compat_flags = IA64_FW_COMPAT_ALL_MASK;
    ivmc->default_ahci = false;
    ivmc->default_i8042 = true;
    ia64_vpc_add_compat_defaults(mc);
}

static void itanium2_vpc_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    IA64VpcMachineClass *ivmc = IA64_VPC_MACHINE_CLASS(oc);

    (void)data;

    mc->desc = "IA-64 virtual PC with Montecito CPU default";
    mc->alias = "ia64-vpc";
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("montecito");
    ivmc->firmware_compat_flags = 0;
    ivmc->default_ahci = true;
    ivmc->default_i8042 = false;
    ia64_vpc_add_compat_defaults(mc);
}

static const TypeInfo ia64_vpc_machine_types[] = {
    {
        .name = TYPE_IA64_VPC_MACHINE,
        .parent = TYPE_MACHINE,
        .abstract = true,
        .instance_size = sizeof(IA64VpcMachineState),
        .instance_init = ia64_vpc_machine_instance_init,
        .instance_finalize = ia64_vpc_machine_instance_finalize,
        .class_size = sizeof(IA64VpcMachineClass),
        .class_init = ia64_vpc_machine_class_init,
    }, {
        .name = TYPE_ITANIUM_VPC_MACHINE,
        .parent = TYPE_IA64_VPC_MACHINE,
        .class_init = itanium_vpc_machine_class_init,
    }, {
        .name = TYPE_ITANIUM2_VPC_MACHINE,
        .parent = TYPE_IA64_VPC_MACHINE,
        .class_init = itanium2_vpc_machine_class_init,
    },
};

DEFINE_TYPES(ia64_vpc_machine_types)
