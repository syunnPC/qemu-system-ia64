/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 platform descriptor ABI.
 *
 * Freestanding transport shared by IA-64 machine models and
 * roms/ia64-firmware.
 */

#ifndef HW_IA64_PLATFORM_ABI_H
#define HW_IA64_PLATFORM_ABI_H

#include "hw/ia64/ia64_firmware_compat.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"
#include "hw/ia64/ia64_ras_abi.h"

#define IA64_PLATFORM_DESC_MAGIC          0x44504c5034364951ULL /* QI64PLPD */
#define IA64_PLATFORM_DESC_REVISION       7U
#define IA64_PLATFORM_DESC_ALIGNMENT      0x1000U
#define IA64_PLATFORM_DESC_MAX_SIZE       0x1000U
#define IA64_PLATFORM_FIRMWARE_BASE       0x0000000000100000ULL
#define IA64_PLATFORM_FIRMWARE_SIZE       0x0000000000200000ULL
#define IA64_PLATFORM_MIN_LOW_RAM_SIZE    0x0000000008000000ULL
#define IA64_PLATFORM_MIN_LEGACY_IO_SIZE  0x0000000004000000ULL
#define IA64_PLATFORM_LEGACY_IO_ALIGNMENT 0x0000000004000000ULL
#define IA64_PLATFORM_I2000_PHYS_ADDR_BITS 44U
#define IA64_PLATFORM_ZX6000_PHYS_ADDR_BITS 50U
/* QEMU PAL firmware-update exclusion window, with an exclusive limit. */
#define IA64_PLATFORM_PAL_FW_UPDATE_BASE  0x00000000ff000000ULL
#define IA64_PLATFORM_PAL_FW_UPDATE_LIMIT 0x0000000100000000ULL
#define IA64_PLATFORM_MIN_NVRAM_SIZE      0x0000000000010000ULL
#define IA64_PLATFORM_MIN_RTC_SIZE        0x0000000000000008ULL
#define IA64_PLATFORM_MIN_CONTROL_SIZE    0x0000000000002000ULL
#define IA64_PLATFORM_ACPI_PM_SIZE        0x0000000000002000ULL
#define IA64_PLATFORM_ACPI_PM_TMR_OFFSET  0x1004U
#define IA64_PLATFORM_ACPI_PM1_EVT_OFFSET 0x1008U
#define IA64_PLATFORM_ACPI_PM1_CNT_OFFSET 0x100cU
#define IA64_PLATFORM_ACPI_GPE0_STS_OFFSET 0x1010U
#define IA64_PLATFORM_ACPI_GPE0_EN_OFFSET  0x1014U
#define IA64_PLATFORM_ACPI_GPE0_LENGTH     0x0008U
/* The ACPI PM MMIO and SystemIO aliases address the same register storage. */
#define IA64_PLATFORM_ACPI_PM_IO_BASE \
    IA64_PLATFORM_ACPI_PM_TMR_OFFSET
#define IA64_PLATFORM_ACPI_PM_IO_SIZE \
    (IA64_PLATFORM_ACPI_GPE0_STS_OFFSET + \
     IA64_PLATFORM_ACPI_GPE0_LENGTH - IA64_PLATFORM_ACPI_PM_IO_BASE)
#define IA64_PLATFORM_RESOURCE_ALIGNMENT  0x2000U
#define IA64_PLATFORM_IO_SAPIC_ALIGNMENT  0x1000U
#define IA64_PLATFORM_IO_SAPIC_SIZE       0x1000U
#define IA64_PLATFORM_IO_SAPIC_MAX_ID     15U
#define IA64_PLATFORM_UART_OVERSAMPLING    16U
#define IA64_PLATFORM_UART_MIN_BAUD        2U
#define IA64_PLATFORM_UART_MIN_CLOCK_HZ \
    (IA64_PLATFORM_UART_OVERSAMPLING * IA64_PLATFORM_UART_MIN_BAUD)
/* ZX1-LBA configuration backend aperture. */
#define IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE 0x2000U
/* Mercury I/O SAPIC selector offset inside an IOA aperture. */
#define IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET 0x0800U

static inline int ia64_platform_zx1_embedded_io_sapic(
    unsigned long long config_base, unsigned long long io_sapic_base)
{
    return config_base <=
        ~0ULL - IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET &&
        io_sapic_base ==
        config_base + IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET;
}

#define IA64_PLATFORM_MAX_PCI_ROOTS       16U
#define IA64_PLATFORM_MAX_IO_SAPICS       16U
#define IA64_PLATFORM_MAX_PCI_ROUTES      128U
#define IA64_PLATFORM_MAX_RAM_RANGES      5U
#define IA64_PLATFORM_MAX_PROFILES        1U
#define IA64_PLATFORM_MAX_ONBOARD_DEVICES 16U
#define IA64_PLATFORM_MAX_NUMA_NODES      8U

#define IA64_PLATFORM_ID_HP_I2000         0x00002000U
#define IA64_PLATFORM_ID_HP_RX2660        0x00002660U
#define IA64_PLATFORM_ID_HP_ZX6000        0x00006000U

#define IA64_PLATFORM_FLAG_NO_MCFG        (1U << 0)
#define IA64_PLATFORM_FLAG_QEMU_EXTENSION (1U << 1)
#define IA64_PLATFORM_FLAG_IDE_DMA        (1U << 2)
#define IA64_PLATFORM_FLAG_PS2_PRESENT    (1U << 3)
#define IA64_PLATFORM_FLAG_NVRAM_PERSISTENT (1U << 4)
#define IA64_PLATFORM_FLAG_FIRMWARE_COMPAT (1U << 5)
#define IA64_PLATFORM_FLAG_FAMILY_HP_I2000 (1U << 6)
#define IA64_PLATFORM_FLAG_FAMILY_HP_ZX    (1U << 7)
#define IA64_PLATFORM_FLAG_PCI_CF8         (1U << 8)
#define IA64_PLATFORM_FLAG_PCI_ZX1_LBA     (1U << 9)
#define IA64_PLATFORM_FLAG_PCI_ECAM        (1U << 10)
#define IA64_PLATFORM_FLAG_SPARSE_IO       (1U << 11)
#define IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC (1U << 12)
#define IA64_PLATFORM_FLAG_ACPI_PM         (1U << 13)

#define IA64_PLATFORM_FLAG_FAMILY_MASK \
    (IA64_PLATFORM_FLAG_FAMILY_HP_I2000 | \
     IA64_PLATFORM_FLAG_FAMILY_HP_ZX)

#define IA64_PLATFORM_KNOWN_FLAGS \
    (IA64_PLATFORM_FLAG_NO_MCFG | \
     IA64_PLATFORM_FLAG_QEMU_EXTENSION | \
     IA64_PLATFORM_FLAG_IDE_DMA | \
     IA64_PLATFORM_FLAG_PS2_PRESENT | \
     IA64_PLATFORM_FLAG_NVRAM_PERSISTENT | \
     IA64_PLATFORM_FLAG_FIRMWARE_COMPAT | \
     IA64_PLATFORM_FLAG_FAMILY_MASK | \
     IA64_PLATFORM_FLAG_PCI_CF8 | \
     IA64_PLATFORM_FLAG_PCI_ZX1_LBA | \
     IA64_PLATFORM_FLAG_PCI_ECAM | \
     IA64_PLATFORM_FLAG_SPARSE_IO | \
     IA64_PLATFORM_FLAG_EMBEDDED_IO_SAPIC | \
     IA64_PLATFORM_FLAG_ACPI_PM)

#define IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS \
    (IA64_PLATFORM_FLAG_NO_MCFG | \
     IA64_PLATFORM_FLAG_QEMU_EXTENSION | \
     IA64_PLATFORM_FLAG_PS2_PRESENT | \
     IA64_PLATFORM_FLAG_FIRMWARE_COMPAT | \
     IA64_PLATFORM_FLAG_FAMILY_HP_I2000 | \
     IA64_PLATFORM_FLAG_PCI_CF8)

static inline unsigned long long ia64_platform_firmware_compat_flags(
    unsigned int platform_id, unsigned int flags)
{
    (void)platform_id;
    if ((flags & IA64_PLATFORM_FLAG_FAMILY_MASK) ==
            IA64_PLATFORM_FLAG_FAMILY_HP_I2000 &&
        (flags & IA64_PLATFORM_FLAG_FIRMWARE_COMPAT) != 0) {
        return IA64_FW_COMPAT_ALL_MASK;
    }
    return 0;
}

#define IA64_PLATFORM_PCI_CONFIG_CF8_CFC  1U
#define IA64_PLATFORM_PCI_CONFIG_ZX1_LBA  2U
#define IA64_PLATFORM_PCI_CONFIG_ECAM     3U
#define IA64_PLATFORM_PCI_ECAM_BUS_SIZE   0x00100000ULL
#define IA64_PLATFORM_PCI_ECAM_ALIGNMENT  0x10000000ULL

static inline unsigned long long ia64_platform_pci_config_size(
    unsigned int config_type, unsigned int first_bus,
    unsigned int last_bus)
{
    if (config_type == IA64_PLATFORM_PCI_CONFIG_ZX1_LBA) {
        return IA64_PLATFORM_ZX1_LBA_CONFIG_SIZE;
    }
    if (config_type == IA64_PLATFORM_PCI_CONFIG_ECAM &&
        last_bus >= first_bus) {
        return ((unsigned long long)last_bus - first_bus + 1U) *
            IA64_PLATFORM_PCI_ECAM_BUS_SIZE;
    }
    return 0;
}

static inline unsigned long long ia64_platform_pci_config_offset(
    unsigned int config_type, unsigned int first_bus)
{
    return config_type == IA64_PLATFORM_PCI_CONFIG_ECAM ?
        (unsigned long long)first_bus * IA64_PLATFORM_PCI_ECAM_BUS_SIZE : 0;
}

#define IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA (1U << 0)
#define IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO    (1U << 1)
#define IA64_PLATFORM_PCI_ROOT_FLAG_AGP          (1U << 2)
/* This root owns the platform's decoded legacy VGA I/O/memory aperture. */
#define IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY   (1U << 3)
#define IA64_PLATFORM_PCI_ROOT_KNOWN_FLAGS \
    (IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA | \
     IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO | \
     IA64_PLATFORM_PCI_ROOT_FLAG_AGP | \
     IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY)

#define IA64_PLATFORM_CONSOLE_FLAG_VGA_PRIMARY (1U << 0)
#define IA64_PLATFORM_CONSOLE_KNOWN_FLAGS \
    IA64_PLATFORM_CONSOLE_FLAG_VGA_PRIMARY

#define IA64_PLATFORM_PCI_ROOT_IDENTITY_GENERIC 0U
#define IA64_PLATFORM_PCI_ROOT_IDENTITY_HP_ZX   1U

#define IA64_PLATFORM_ONBOARD_GRAPHICS 1U
#define IA64_PLATFORM_ONBOARD_UHCI     2U
#define IA64_PLATFORM_ONBOARD_OHCI     3U
#define IA64_PLATFORM_ONBOARD_EHCI     4U
#define IA64_PLATFORM_ONBOARD_IDE      5U
#define IA64_PLATFORM_ONBOARD_SCSI     6U
#define IA64_PLATFORM_ONBOARD_MPT      7U
#define IA64_PLATFORM_ONBOARD_NETWORK  8U
#define IA64_PLATFORM_ONBOARD_MGMT     9U
#define IA64_PLATFORM_ONBOARD_UART     10U

typedef struct __attribute__((packed)) IA64PlatformOnboardDevice {
    unsigned short Segment;
    unsigned char Bus;
    unsigned char Device;
    unsigned char Function;
    unsigned char Type;
    unsigned char Bar;
    unsigned char Reserved0;
    unsigned int VendorDeviceId;
    unsigned int ClassCode;
    unsigned long long BarSize;
    unsigned int Flags;
    unsigned int Reserved1;
} IA64PlatformOnboardDevice;

/* Each RAM-range bit and processor index is owned by exactly one node. */
typedef struct __attribute__((packed)) IA64PlatformNumaNode {
    unsigned int ProximityDomain;
    unsigned int ProcessorStart;
    unsigned int ProcessorCount;
    unsigned int RamRangeMask;
    unsigned char Distance[IA64_PLATFORM_MAX_NUMA_NODES];
    unsigned char Reserved[8];
} IA64PlatformNumaNode;

/*
 * PAL_PLATFORM_ADDR type 1 registers exactly one 64 MiB sparse-I/O block.
 * Keep descriptor acceptance within the CPU model paired with each machine
 * and outside the QEMU PAL firmware-update exclusion window.
 */
static inline int ia64_platform_legacy_io_valid(
    unsigned int physical_address_bits, unsigned long long base,
    unsigned long long size)
{
    unsigned long long implemented_limit;

    if (physical_address_bits < 32U || physical_address_bits >= 64U) {
        return 0;
    }
    implemented_limit = 1ULL << physical_address_bits;
    if (size != IA64_PLATFORM_MIN_LEGACY_IO_SIZE ||
        (base & (IA64_PLATFORM_LEGACY_IO_ALIGNMENT - 1U)) != 0 ||
        base > implemented_limit - size) {
        return 0;
    }
    return base >= IA64_PLATFORM_PAL_FW_UPDATE_LIMIT ||
        base + size <= IA64_PLATFORM_PAL_FW_UPDATE_BASE;
}

/*
 * Multi-byte fields are little-endian, and the byte sum over TotalSize is
 * zero.  Offset/count/stride triples permit arrays and trailing extensions.
 */
typedef struct __attribute__((packed)) IA64PlatformDescriptor {
    unsigned long long Magic;
    unsigned int FormatRevision;
    unsigned int HeaderSize;
    unsigned int TotalSize;
    unsigned int PlatformId;
    unsigned int Flags;
    unsigned int Checksum;

    unsigned long long RamSize;
    unsigned long long LowRamEnd;
    unsigned long long FirmwareBase;
    unsigned long long FirmwareSize;

    unsigned int ProcessorCount;
    unsigned int SocketCount;
    unsigned int CoresPerSocket;
    unsigned int ThreadsPerCore;
    unsigned int PhysicalAddressBits;
    unsigned int MaxSockets;
    unsigned int MaxCoresPerSocket;
    unsigned int MaxThreadsPerCore;
    unsigned int MaxPciRoots;
    unsigned int PciRootIdentity;
    unsigned int OnboardDeviceCount;
    unsigned int NumaNodeCount;

    IA64PlatformOnboardDevice
        OnboardDevice[IA64_PLATFORM_MAX_ONBOARD_DEVICES];
    IA64PlatformNumaNode NumaNode[IA64_PLATFORM_MAX_NUMA_NODES];

    unsigned int PciRootOffset;
    unsigned int PciRootCount;
    unsigned int PciRootEntrySize;
    unsigned int IoSapicOffset;
    unsigned int IoSapicCount;
    unsigned int IoSapicEntrySize;
    unsigned int PciRouteOffset;
    unsigned int PciRouteCount;
    unsigned int PciRouteEntrySize;
    unsigned int Reserved0;

    unsigned long long LegacyIoBase;
    unsigned long long LegacyIoSize;
    unsigned long long LocalSapicBase;
    unsigned long long LocalSapicSize;

    unsigned long long ConsoleBase;
    unsigned int ConsoleRegisterStride;
    /* Input clock used by the 16550-compatible divisor latch. */
    unsigned int ConsoleClockHz;
    unsigned int ConsoleIrq;
    unsigned int ConsoleFlags;

    unsigned long long NvramBase;
    unsigned long long NvramSize;
    unsigned long long RtcBase;
    unsigned long long RtcSize;

    unsigned int RamRangeOffset;
    unsigned int RamRangeCount;
    unsigned int RamRangeEntrySize;
    unsigned int Reserved1;

    unsigned int ProfileOffset;
    unsigned int ProfileCount;
    unsigned int ProfileEntrySize;
    unsigned int Reserved2;

    unsigned long long ControlBase;
    unsigned long long ControlSize;
    unsigned int ResetControlOffset;
    unsigned int PoweroffControlOffset;
    unsigned int ControlValue;
    unsigned int Reserved3;

    /*
     * Optional ACPI PM1a event/control/timer register block in SystemMemory.
     * A zero Size means absent.  The register offsets are the ACPI_PM macros
     * above, and AcpiSciGsi names an input owned by a described I/O SAPIC.
     */
    unsigned long long AcpiPmBase;
    unsigned long long AcpiPmSize;
    unsigned int AcpiSciGsi;
    unsigned int Reserved4;

    /* Firmware-visible machine-check and corrected-error record transport. */
    unsigned long long RasBase;
    unsigned long long RasSize;
} IA64PlatformDescriptor;

/*
 * The complete installed-RAM map.  Entries are sorted by Base, do not
 * overlap, and their sizes sum to Descriptor.RamSize.  Entry zero is exactly
 * [0, Descriptor.LowRamEnd); subsequent entries describe RAM above platform
 * holes.  Bases and sizes use the IA-64 EFI 8 KiB resource alignment so no
 * RAM byte is lost when the firmware emits memory descriptors.  These ranges
 * describe RAM backing, not its EFI allocation type.
 */
typedef struct __attribute__((packed)) IA64PlatformRamRange {
    unsigned long long Base;
    unsigned long long Size;
} IA64PlatformRamRange;

typedef struct __attribute__((packed)) IA64PlatformPciRoot {
    unsigned short Segment;
    unsigned char Bus;
    unsigned char ConfigType;
    unsigned int Flags;
    /* ECAM uses the processor-relative bus-zero base reported by MCFG. */
    unsigned long long ConfigBase;
    /* Logical PCI I/O port numbers, never sparse CPU physical addresses. */
    unsigned long long IoBase;
    unsigned long long IoSize;
    /* PCI child-bus addresses.  The CPU address is Base + Translation. */
    unsigned long long Mmio32Base;
    unsigned long long Mmio32Size;
    unsigned long long Mmio64Base;
    unsigned long long Mmio64Size;
    /*
     * PCI-visible DMA address aperture; it is not a CPU MMIO reservation.
     * IDENTITY_DMA declares that every address in this aperture is the same
     * CPU physical address and is backed by one declared RAM range.
     */
    unsigned long long DmaBase;
    unsigned long long DmaSize;
    unsigned int Rope;
    unsigned char BusEnd;
    unsigned char Reserved[3];
    /*
     * Signed two's-complement offsets from child-bus to CPU address space.
     * SPARSE_IO declares that IoTranslationOffset is the CPU sparse-aperture
     * base.  Otherwise IoTranslationOffset is zero.
     */
    unsigned long long IoTranslationOffset;
    unsigned long long Mmio32TranslationOffset;
    unsigned long long Mmio64TranslationOffset;
} IA64PlatformPciRoot;

typedef struct __attribute__((packed)) IA64PlatformIoSapic {
    unsigned long long Base;
    unsigned int GsiBase;
    unsigned int RedirectionEntries;
    unsigned int Version;
    /* Descriptor ID used by the MADT I/O SAPIC structure. */
    unsigned char Id;
    unsigned char Reserved[3];
} IA64PlatformIoSapic;

typedef struct __attribute__((packed)) IA64PlatformPciRoute {
    /*
     * Segment/Bus names the root bus; Device/Pin is its direct-slot _PRT key.
     */
    unsigned short Segment;
    unsigned char Bus;
    unsigned char Device;
    unsigned char Pin;
    unsigned char Reserved0;
    unsigned short Reserved1;
    unsigned int Gsi;
    unsigned int Flags;
} IA64PlatformPciRoute;

#endif /* HW_IA64_PLATFORM_ABI_H */
