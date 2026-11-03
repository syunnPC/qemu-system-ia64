/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * HP i2000 firmware profile ABI.
 */

#ifndef HW_IA64_I2000_PROFILE_ABI_H
#define HW_IA64_I2000_PROFILE_ABI_H

#include "hw/scsi/isp12160_abi.h"

#define IA64_PLATFORM_PROFILE_TYPE_HP_I2000       1U
#define IA64_PLATFORM_I2000_PROFILE_REVISION         1U

#define IA64_I2000_PROFILE_NVRAM_BASE \
    0x00000000fff00000ULL
#define IA64_I2000_PROFILE_NVRAM_SIZE 0x00080000U
#define IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET 0x0000fff8U
#define IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC \
    0x54494d4d4f43564eULL

#define IA64_I2000_PROFILE_ACPI_PM_IO_BASE 0x0400U
#define IA64_I2000_PROFILE_ACPI_PM_IO_SIZE 0x0040U
#define IA64_I2000_PROFILE_ACPI_SCI_IRQ    9U

#define IA64_I2000_PROFILE_FLAG_CONSOLE_POLL_ONLY    (1U << 0)
#define IA64_I2000_PROFILE_FLAG_EFI_VARS_UNAVAILABLE (1U << 1)
#define IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE (1U << 2)
#define IA64_I2000_PROFILE_FLAG_ACPI_PM_UNAVAILABLE  (1U << 3)
#define IA64_I2000_PROFILE_FLAG_RESET_UNAVAILABLE    (1U << 4)
#define IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT     (1U << 5)
#define IA64_I2000_PROFILE_KNOWN_FLAGS               \
    (IA64_I2000_PROFILE_FLAG_CONSOLE_POLL_ONLY |     \
     IA64_I2000_PROFILE_FLAG_EFI_VARS_UNAVAILABLE |  \
     IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE |  \
     IA64_I2000_PROFILE_FLAG_ACPI_PM_UNAVAILABLE |   \
     IA64_I2000_PROFILE_FLAG_RESET_UNAVAILABLE |     \
     IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT)
#define IA64_I2000_PROFILE_REQUIRED_FLAGS \
    (IA64_I2000_PROFILE_KNOWN_FLAGS & \
     ~(IA64_I2000_PROFILE_FLAG_EFI_VARS_UNAVAILABLE | \
       IA64_I2000_PROFILE_FLAG_EFI_TIME_UNAVAILABLE | \
       IA64_I2000_PROFILE_FLAG_ACPI_PM_UNAVAILABLE | \
       IA64_I2000_PROFILE_FLAG_RESET_UNAVAILABLE))

/* HP i2000 Super-I/O programming values. */
#define IA64_I2000_PROFILE_SIO_INDEX_PORT                    0x2eU
#define IA64_I2000_PROFILE_SIO_DATA_PORT                     0x2fU
#define IA64_I2000_PROFILE_SIO_ENTER_KEY                     0x55U
#define IA64_I2000_PROFILE_SIO_ENTER_COUNT                   1U
#define IA64_I2000_PROFILE_SIO_EXIT_KEY                      0xaaU
#define IA64_I2000_PROFILE_SIO_LDN_SELECT_REGISTER           0x07U
#define IA64_I2000_PROFILE_SIO_DEVICE_ID_REGISTER            0x20U
#define IA64_I2000_PROFILE_SIO_DEVICE_ID                     0x51U
#define IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER             0x30U
#define IA64_I2000_PROFILE_SIO_ACTIVE_VALUE                  0x01U

#define IA64_I2000_PROFILE_UART_LDN                          4U
#define IA64_I2000_PROFILE_UART_BASE_MSB_REGISTER            0x60U
#define IA64_I2000_PROFILE_UART_BASE_LSB_REGISTER            0x61U
#define IA64_I2000_PROFILE_UART_IRQ_REGISTER                 0x70U
#define IA64_I2000_PROFILE_UART_MODE_REGISTER                0xf0U
#define IA64_I2000_PROFILE_UART_MODE_VALUE                   0U
#define IA64_I2000_PROFILE_UART_PORT                         0x03f8U
#define IA64_I2000_PROFILE_UART_SIZE                         8U
#define IA64_I2000_PROFILE_UART_IRQ                          4U
#define IA64_I2000_PROFILE_UART_INPUT_CLOCK_HZ               1843200U

#define IA64_I2000_PROFILE_PARALLEL_LDN                      3U
#define IA64_I2000_PROFILE_PARALLEL_PORT                     0x0378U
#define IA64_I2000_PROFILE_PARALLEL_IRQ                      7U
#define IA64_I2000_PROFILE_PARALLEL_MODE                     0x3aU
#define IA64_I2000_PROFILE_PARALLEL_DMA_DISABLED             4U

#define IA64_I2000_PROFILE_I8042_LDN                         7U
#define IA64_I2000_PROFILE_I8042_KBD_IRQ_REGISTER            0x70U
#define IA64_I2000_PROFILE_I8042_MOUSE_IRQ_REGISTER          0x72U
#define IA64_I2000_PROFILE_I8042_DATA_PORT                   0x0060U
#define IA64_I2000_PROFILE_I8042_COMMAND_PORT                0x0064U
#define IA64_I2000_PROFILE_I8042_RESET_COMMAND               0xfeU
#define IA64_I2000_PROFILE_I8042_PORT_SIZE                   1U
#define IA64_I2000_PROFILE_I8042_KBD_IRQ                     1U
#define IA64_I2000_PROFILE_I8042_MOUSE_IRQ                   12U

/* Fixed primary IDE values. */
#define IA64_I2000_PROFILE_IDE_SEGMENT                       0U
#define IA64_I2000_PROFILE_IDE_BUS                           0U
#define IA64_I2000_PROFILE_IDE_DEVICE                        3U
#define IA64_I2000_PROFILE_IDE_FUNCTION                      1U
#define IA64_I2000_PROFILE_IDE_VENDOR_ID                     0x8086U
#define IA64_I2000_PROFILE_IDE_DEVICE_ID                     0x7601U
#define IA64_I2000_PROFILE_IDE_CLASS                         0x0101U
#define IA64_I2000_PROFILE_IDE_PROG_IF                       0x80U
#define IA64_I2000_PROFILE_IDE_COMMAND_PORT                  0x01f0U
#define IA64_I2000_PROFILE_IDE_COMMAND_SIZE                  8U
#define IA64_I2000_PROFILE_IDE_CONTROL_PORT                  0x03f6U
#define IA64_I2000_PROFILE_IDE_CONTROL_SIZE                  1U
#define IA64_I2000_PROFILE_IDE_BMDMA_PORT                    0x3ff0U
#define IA64_I2000_PROFILE_IDE_BMDMA_SIZE                    16U
#define IA64_I2000_PROFILE_IDE_IRQ                           14U
#define IA64_I2000_PROFILE_IDE_PRIMARY_MASTER_UNIT_MASK      (1U << 0)
#define IA64_I2000_PROFILE_IDE_UNIT_MASK                     0x03U

/* Multi-byte fields use little-endian wire order; byte fields are direct. */
typedef struct __attribute__((packed)) IA64PlatformI2000Profile {
    unsigned int ProfileType;
    unsigned int ProfileRevision;
    unsigned int Length;
    unsigned int Flags;

    unsigned char SuperIoIndexPort;
    unsigned char SuperIoDataPort;
    unsigned char SuperIoEnterKey;
    unsigned char SuperIoEnterCount;
    unsigned char SuperIoExitKey;
    unsigned char SuperIoLdnSelectRegister;
    unsigned char SuperIoDeviceIdRegister;
    unsigned char SuperIoDeviceId;

    unsigned char UartLdn;
    unsigned char UartActivateRegister;
    unsigned char UartActivateValue;
    unsigned char UartBaseMsbRegister;
    unsigned char UartBaseMsbValue;
    unsigned char UartBaseLsbRegister;
    unsigned char UartBaseLsbValue;
    unsigned char UartIrqRegister;
    unsigned char UartIrqValue;
    unsigned char UartModeRegister;
    unsigned char UartModeValue;

    unsigned char I8042Ldn;
    unsigned char I8042ActivateRegister;
    unsigned char I8042ActivateValue;
    unsigned char I8042KeyboardIrqRegister;
    unsigned char I8042KeyboardIrqValue;
    unsigned char I8042MouseIrqRegister;
    unsigned char I8042MouseIrqValue;
    unsigned char Reserved0[2];

    unsigned short UartPort;
    unsigned char UartSize;
    unsigned char UartIrq;
    unsigned int UartInputClockHz;

    unsigned short I8042DataPort;
    unsigned short I8042CommandPort;
    unsigned char I8042PortSize;
    unsigned char I8042KeyboardIrq;
    unsigned char I8042MouseIrq;
    unsigned char Reserved1;

    unsigned short IdeSegment;
    unsigned short IdeCommandPort;
    unsigned short IdeControlPort;
    unsigned short IdeVendorId;
    unsigned short IdeDeviceId;
    unsigned short IdeClass;
    unsigned char IdeBus;
    unsigned char IdeDevice;
    unsigned char IdeFunction;
    unsigned char IdeProgIf;
    unsigned char IdeIrq;
    unsigned char IdeUnitMask;
    unsigned char IdeCommandSize;
    unsigned char IdeControlSize;
    /* Valid when IA64_I2000_PROFILE_FLAG_ISP12160_PRESENT is set. */
    unsigned int Isp12160Capabilities;
    unsigned int Reserved3;
} IA64PlatformI2000Profile;

typedef void (*IA64I2000SuperIoWrite)(void *opaque, unsigned int logical_port,
                                      unsigned char value);

static inline void
ia64_i2000_profile_superio_emit(const IA64PlatformI2000Profile *profile,
                                IA64I2000SuperIoWrite write, void *opaque)
{
    unsigned int i;

    for (i = 0; i < profile->SuperIoEnterCount; i++) {
        write(opaque, profile->SuperIoIndexPort, profile->SuperIoEnterKey);
    }

    write(opaque, profile->SuperIoIndexPort, profile->SuperIoLdnSelectRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartLdn);
    write(opaque, profile->SuperIoIndexPort, profile->UartBaseMsbRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartBaseMsbValue);
    write(opaque, profile->SuperIoIndexPort, profile->UartBaseLsbRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartBaseLsbValue);
    write(opaque, profile->SuperIoIndexPort, profile->UartIrqRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartIrqValue);
    write(opaque, profile->SuperIoIndexPort, profile->UartModeRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartModeValue);
    /* Activation is the final write to this logical device. */
    write(opaque, profile->SuperIoIndexPort, profile->UartActivateRegister);
    write(opaque, profile->SuperIoDataPort, profile->UartActivateValue);

    write(opaque, profile->SuperIoIndexPort, profile->SuperIoLdnSelectRegister);
    write(opaque, profile->SuperIoDataPort, profile->I8042Ldn);
    write(opaque, profile->SuperIoIndexPort, profile->I8042KeyboardIrqRegister);
    write(opaque, profile->SuperIoDataPort, profile->I8042KeyboardIrqValue);
    write(opaque, profile->SuperIoIndexPort, profile->I8042MouseIrqRegister);
    write(opaque, profile->SuperIoDataPort, profile->I8042MouseIrqValue);
    /* Activation is the final write to this logical device. */
    write(opaque, profile->SuperIoIndexPort, profile->I8042ActivateRegister);
    write(opaque, profile->SuperIoDataPort, profile->I8042ActivateValue);

    write(opaque, profile->SuperIoIndexPort, profile->SuperIoExitKey);
}

#endif /* HW_IA64_I2000_PROFILE_ABI_H */
