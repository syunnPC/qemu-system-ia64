/*
 * IA-64 i2000 I/O test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_I2000_TEST_LAYOUT_H
#define HW_IA64_I2000_TEST_LAYOUT_H

#include "hw/ia64/ia64_i2000_profile_abi.h"
#include "qemu/bitops.h"
#include "qemu/typedefs.h"

/* Fixed guest-visible I/O test layout. */
#define IA64_I2000_IO_TEST_PARENT_ROOT              0U
#define IA64_I2000_IO_TEST_PARENT_IO_BASE           UINT32_C(0x0000)
#define IA64_I2000_IO_TEST_PARENT_IO_SIZE           UINT32_C(0x4000)
#define IA64_I2000_IO_TEST_PARENT_CF8_BASE          UINT32_C(0x0cf8)
#define IA64_I2000_IO_TEST_PARENT_CFC_BASE          UINT32_C(0x0cfc)
#define IA64_I2000_IO_TEST_PCI_CONFIG_PORT_SIZE     UINT32_C(4)

#define IA64_I2000_IO_TEST_PCI_SLOT                 2U
#define IA64_I2000_IO_TEST_PCI_FUNCTION_COUNT       2U
#define IA64_I2000_IO_TEST_F0_FUNCTION              0U
#define IA64_I2000_IO_TEST_F0_VENDOR_ID             UINT16_C(0x1234)
#define IA64_I2000_IO_TEST_F0_DEVICE_ID             UINT16_C(0x0460)
#define IA64_I2000_IO_TEST_F0_CLASS                 UINT16_C(0x0601)
#define IA64_I2000_IO_TEST_F0_REVISION              0U
#define IA64_I2000_IO_TEST_F0_PROG_IF               0U
#define IA64_I2000_IO_TEST_F0_SUBSYSTEM_VENDOR_ID   UINT16_C(0)
#define IA64_I2000_IO_TEST_F0_SUBSYSTEM_ID          UINT16_C(0)
#define IA64_I2000_IO_TEST_F1_FUNCTION              1U
#define IA64_I2000_IO_TEST_F1_VENDOR_ID             \
    IA64_I2000_PROFILE_IDE_VENDOR_ID
#define IA64_I2000_IO_TEST_F1_DEVICE_ID             \
    IA64_I2000_PROFILE_IDE_DEVICE_ID
#define IA64_I2000_IO_TEST_F1_CLASS                 \
    IA64_I2000_PROFILE_IDE_CLASS
#define IA64_I2000_IO_TEST_F1_REVISION              0U
#define IA64_I2000_IO_TEST_F1_PROG_IF               \
    IA64_I2000_PROFILE_IDE_PROG_IF
#define IA64_I2000_IO_TEST_F1_SUBSYSTEM_VENDOR_ID   UINT16_C(0)
#define IA64_I2000_IO_TEST_F1_SUBSYSTEM_ID          UINT16_C(0)

/*
 * The onboard NIC identity, BAR geometry, and EEPROM checksum match the
 * configured i82559c device.
 */
#define IA64_I2000_IO_TEST_I82559_PARENT_ROOT        0U
#define IA64_I2000_IO_TEST_I82559_SLOT               3U
#define IA64_I2000_IO_TEST_I82559_FUNCTION           0U
#define IA64_I2000_IO_TEST_I82559_VENDOR_ID          UINT16_C(0x8086)
#define IA64_I2000_IO_TEST_I82559_DEVICE_ID          UINT16_C(0x1229)
#define IA64_I2000_IO_TEST_I82559_CLASS              UINT16_C(0x0200)
#define IA64_I2000_IO_TEST_I82559_REVISION           0x08U
#define IA64_I2000_IO_TEST_I82559_PROG_IF            0U
#define IA64_I2000_IO_TEST_I82559_SUBSYSTEM_VENDOR_ID \
    UINT16_C(0x8086)
#define IA64_I2000_IO_TEST_I82559_SUBSYSTEM_ID       UINT16_C(0x0040)
#define IA64_I2000_IO_TEST_I82559_INTERRUPT_PIN      1U
#define IA64_I2000_IO_TEST_I82559_PID_PIN            16U
#define IA64_I2000_IO_TEST_I82559_MMIO_BAR_SIZE      UINT32_C(0x1000)
#define IA64_I2000_IO_TEST_I82559_IO_BAR_SIZE        UINT32_C(0x0040)
/* The third BAR is the flash aperture; flash behavior is not implemented. */
#define IA64_I2000_IO_TEST_I82559_FLASH_BAR_SIZE     UINT32_C(0x020000)
#define IA64_I2000_IO_TEST_I82559_EEPROM_WORDS       UINT16_C(64)
#define IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM    UINT16_C(0xbaba)
#define IA64_I2000_IO_TEST_I82559_EEPROM_COMPATIBILITY UINT16_C(0x0203)
#define IA64_I2000_IO_TEST_I82559_EEPROM_CONTROLLER  UINT16_C(0x0201)
#define IA64_I2000_IO_TEST_I82559_EEPROM_PHY         UINT16_C(0x4701)
#define IA64_I2000_IO_TEST_I82559_EEPROM_ID           UINT16_C(0x4880)
/* 52:54:00:20:00:01, encoded as guest-visible little-endian words. */
#define IA64_I2000_IO_TEST_I82559_MAC_WORD0          UINT16_C(0x5452)
#define IA64_I2000_IO_TEST_I82559_MAC_WORD1          UINT16_C(0x2000)
#define IA64_I2000_IO_TEST_I82559_MAC_WORD2          UINT16_C(0x0100)
#define IA64_I2000_IO_TEST_I82559_OPTION_ROM_ENABLED 0U

/*
 * The ISP12160 uses root 1, separate from the root-0 IFB and NIC.
 */
#define IA64_I2000_IO_TEST_ISP12160_PARENT_ROOT       1U
#define IA64_I2000_IO_TEST_ISP12160_BUS               \
    ISP12160_QEMU_I2000_BUS
#define IA64_I2000_IO_TEST_ISP12160_SLOT              \
    ISP12160_QEMU_I2000_DEVICE
#define IA64_I2000_IO_TEST_ISP12160_FUNCTION          \
    ISP12160_QEMU_I2000_FUNCTION
#define IA64_I2000_IO_TEST_ISP12160_VENDOR_ID         \
    ISP12160_PCI_VENDOR_ID
#define IA64_I2000_IO_TEST_ISP12160_DEVICE_ID         \
    ISP12160_PCI_DEVICE_ID
#define IA64_I2000_IO_TEST_ISP12160_CLASS             \
    ISP12160_PCI_CLASS
#define IA64_I2000_IO_TEST_ISP12160_REVISION          0x06U
#define IA64_I2000_IO_TEST_ISP12160_PROG_IF           0U
#define IA64_I2000_IO_TEST_ISP12160_SUBSYSTEM_VENDOR_ID \
    ISP12160_PCI_VENDOR_ID
#define IA64_I2000_IO_TEST_ISP12160_SUBSYSTEM_ID      UINT16_C(0x0007)
#define IA64_I2000_IO_TEST_ISP12160_INTERRUPT_PIN     \
    ISP12160_QEMU_I2000_INTERRUPT_PIN
#define IA64_I2000_IO_TEST_ISP12160_PID_PIN           \
    ISP12160_QEMU_I2000_GSI
#define IA64_I2000_IO_TEST_ISP12160_IO_BAR_BASE       UINT32_C(0x5000)
#define IA64_I2000_IO_TEST_ISP12160_IO_BAR_SIZE       UINT32_C(0x0100)
#define IA64_I2000_IO_TEST_ISP12160_MMIO_BAR_BASE     \
    ISP12160_QEMU_I2000_BAR_ADDRESS
#define IA64_I2000_IO_TEST_ISP12160_MMIO_BAR_SIZE     \
    ISP12160_QEMU_I2000_BAR_SIZE
#define IA64_I2000_IO_TEST_ISP12160_OPTION_ROM_ENABLED 0U

#define IA64_I2000_IO_TEST_PIC_MASTER_BASE          UINT32_C(0x0020)
#define IA64_I2000_IO_TEST_PIC_SLAVE_BASE           UINT32_C(0x00a0)
#define IA64_I2000_IO_TEST_PIC_PORT_SIZE            UINT32_C(2)
#define IA64_I2000_IO_TEST_PIC_CASCADE_IRQ          2U
#define IA64_I2000_IO_TEST_ELCR_MASTER_BASE         UINT32_C(0x04d0)
#define IA64_I2000_IO_TEST_ELCR_SLAVE_BASE          UINT32_C(0x04d1)
#define IA64_I2000_IO_TEST_ELCR_PORT_SIZE           UINT32_C(1)
#define IA64_I2000_IO_TEST_PID_LEGACY_PIN           0U
#define IA64_I2000_IO_TEST_460GX_LEGACY_PIN_COUNT      16U

#define IA64_I2000_IO_TEST_PIT_BASE                 UINT32_C(0x0040)
#define IA64_I2000_IO_TEST_PIT_SIZE                 UINT32_C(4)
#define IA64_I2000_IO_TEST_PIT_IRQ                  0U

#define IA64_I2000_IO_TEST_SIO_CONFIG_BASE          \
    IA64_I2000_PROFILE_SIO_INDEX_PORT
#define IA64_I2000_IO_TEST_SIO_CONFIG_SIZE          UINT32_C(2)
#define IA64_I2000_IO_TEST_SIO_SYSOPT                0U
#define IA64_I2000_IO_TEST_SIO_ENTER_KEY            \
    IA64_I2000_PROFILE_SIO_ENTER_KEY
#define IA64_I2000_IO_TEST_SIO_ENTER_COUNT          \
    IA64_I2000_PROFILE_SIO_ENTER_COUNT
#define IA64_I2000_IO_TEST_SIO_EXIT_KEY             \
    IA64_I2000_PROFILE_SIO_EXIT_KEY
#define IA64_I2000_IO_TEST_SIO_LDN_SELECT_CR        \
    IA64_I2000_PROFILE_SIO_LDN_SELECT_REGISTER
#define IA64_I2000_IO_TEST_SIO_DEVICE_ID_CR         \
    IA64_I2000_PROFILE_SIO_DEVICE_ID_REGISTER
#define IA64_I2000_IO_TEST_SIO_DEVICE_ID            \
    IA64_I2000_PROFILE_SIO_DEVICE_ID

#define IA64_I2000_IO_TEST_UART_LDN                 IA64_I2000_PROFILE_UART_LDN
#define IA64_I2000_IO_TEST_UART_ACTIVATE_CR         \
    IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER
#define IA64_I2000_IO_TEST_UART_BASE_MSB_CR         \
    IA64_I2000_PROFILE_UART_BASE_MSB_REGISTER
#define IA64_I2000_IO_TEST_UART_BASE_LSB_CR         \
    IA64_I2000_PROFILE_UART_BASE_LSB_REGISTER
#define IA64_I2000_IO_TEST_UART_IRQ_CR              \
    IA64_I2000_PROFILE_UART_IRQ_REGISTER
#define IA64_I2000_IO_TEST_UART_MODE_CR             \
    IA64_I2000_PROFILE_UART_MODE_REGISTER
#define IA64_I2000_IO_TEST_UART_RESET_ACTIVE         0U
#define IA64_I2000_IO_TEST_UART_BASE                IA64_I2000_PROFILE_UART_PORT
#define IA64_I2000_IO_TEST_UART_SIZE                IA64_I2000_PROFILE_UART_SIZE
#define IA64_I2000_IO_TEST_UART_IRQ                 IA64_I2000_PROFILE_UART_IRQ
#define IA64_I2000_IO_TEST_UART_INPUT_CLOCK_HZ      \
    IA64_I2000_PROFILE_UART_INPUT_CLOCK_HZ

#define IA64_I2000_IO_TEST_I8042_LDN                IA64_I2000_PROFILE_I8042_LDN
#define IA64_I2000_IO_TEST_I8042_ACTIVATE_CR        \
    IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER
#define IA64_I2000_IO_TEST_I8042_KBD_IRQ_CR         \
    IA64_I2000_PROFILE_I8042_KBD_IRQ_REGISTER
#define IA64_I2000_IO_TEST_I8042_MOUSE_IRQ_CR       \
    IA64_I2000_PROFILE_I8042_MOUSE_IRQ_REGISTER
#define IA64_I2000_IO_TEST_I8042_RESET_ACTIVE        0U
#define IA64_I2000_IO_TEST_I8042_DATA_BASE          \
    IA64_I2000_PROFILE_I8042_DATA_PORT
#define IA64_I2000_IO_TEST_I8042_COMMAND_BASE       \
    IA64_I2000_PROFILE_I8042_COMMAND_PORT
#define IA64_I2000_IO_TEST_I8042_PORT_SIZE          \
    IA64_I2000_PROFILE_I8042_PORT_SIZE
#define IA64_I2000_IO_TEST_I8042_KBD_IRQ            \
    IA64_I2000_PROFILE_I8042_KBD_IRQ
#define IA64_I2000_IO_TEST_I8042_MOUSE_IRQ          \
    IA64_I2000_PROFILE_I8042_MOUSE_IRQ

#define IA64_I2000_IO_TEST_RTC_BANK0_BASE           UINT32_C(0x0070)
#define IA64_I2000_IO_TEST_RTC_BANK1_BASE           UINT32_C(0x0072)
/* The two banks use adjacent, independent index/data pairs. */
#define IA64_I2000_IO_TEST_RTC_BANK_SIZE            UINT32_C(2)
#define IA64_I2000_IO_TEST_RTC_IRQ                  8U
#define IA64_I2000_IO_TEST_RTC_BASE_YEAR            2000

#define IA64_I2000_IO_TEST_IDE_COMMAND_BASE         \
    IA64_I2000_PROFILE_IDE_COMMAND_PORT
#define IA64_I2000_IO_TEST_IDE_COMMAND_SIZE         \
    IA64_I2000_PROFILE_IDE_COMMAND_SIZE
#define IA64_I2000_IO_TEST_IDE_CONTROL_BASE         \
    IA64_I2000_PROFILE_IDE_CONTROL_PORT
#define IA64_I2000_IO_TEST_IDE_CONTROL_SIZE         \
    IA64_I2000_PROFILE_IDE_CONTROL_SIZE
#define IA64_I2000_IO_TEST_IDE_IRQ                  IA64_I2000_PROFILE_IDE_IRQ
#define IA64_I2000_IO_TEST_IDE_MASTER_UNIT          0U

/* Features in IA64_I2000_IO_TEST_DISABLED_FEATURES are not implemented. */
#define IA64_I2000_IO_TEST_FEATURE_PIC                      BIT(0)
#define IA64_I2000_IO_TEST_FEATURE_PIT                      BIT(1)
#define IA64_I2000_IO_TEST_FEATURE_SUPERIO_CONFIG           BIT(2)
#define IA64_I2000_IO_TEST_FEATURE_UART                     BIT(3)
#define IA64_I2000_IO_TEST_FEATURE_I8042                    BIT(4)
#define IA64_I2000_IO_TEST_FEATURE_RTC_BANK0                BIT(5)
#define IA64_I2000_IO_TEST_FEATURE_RTC_BANK1                BIT(6)
#define IA64_I2000_IO_TEST_FEATURE_IDE_PRIMARY_MASTER_PIO   BIT(7)
#define IA64_I2000_IO_TEST_FEATURE_IDE_SECONDARY            BIT(8)
#define IA64_I2000_IO_TEST_FEATURE_IDE_SLAVE                BIT(9)
#define IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA                BIT(10)
#define IA64_I2000_IO_TEST_FEATURE_SUPERIO_PARALLEL         BIT(11)
#define IA64_I2000_IO_TEST_FEATURE_SUPERIO_FLOPPY           BIT(12)
#define IA64_I2000_IO_TEST_FEATURE_SUPERIO_ADDITIONAL_UART  BIT(13)
#define IA64_I2000_IO_TEST_FEATURE_IFB_USB                  BIT(14)
#define IA64_I2000_IO_TEST_FEATURE_IFB_PM                   BIT(15)
#define IA64_I2000_IO_TEST_FEATURE_IFB_SMBUS                BIT(16)
#define IA64_I2000_IO_TEST_FEATURE_IFB_FWH                  BIT(17)
#define IA64_I2000_IO_TEST_FEATURE_PICMODE                  BIT(18)
#define IA64_I2000_IO_TEST_FEATURE_EXTINT_ACK               BIT(19)
#define IA64_I2000_IO_TEST_FEATURE_RTC_PERSISTENCE          BIT(20)
#define IA64_I2000_IO_TEST_FEATURE_EFI_VARIABLES            BIT(21)
#define IA64_I2000_IO_TEST_FEATURE_ISA_DMA                  BIT(22)
#define IA64_I2000_IO_TEST_FEATURE_I82559C                  BIT(23)
#define IA64_I2000_IO_TEST_FEATURE_ISP12160                 BIT(24)

#define IA64_I2000_IO_TEST_ENABLED_FEATURES         \
    (IA64_I2000_IO_TEST_FEATURE_PIC |                       \
     IA64_I2000_IO_TEST_FEATURE_PIT |                       \
     IA64_I2000_IO_TEST_FEATURE_SUPERIO_CONFIG |            \
     IA64_I2000_IO_TEST_FEATURE_SUPERIO_PARALLEL |          \
     IA64_I2000_IO_TEST_FEATURE_UART |                      \
     IA64_I2000_IO_TEST_FEATURE_I8042 |                     \
     IA64_I2000_IO_TEST_FEATURE_RTC_BANK0 |                 \
     IA64_I2000_IO_TEST_FEATURE_RTC_BANK1 |                 \
     IA64_I2000_IO_TEST_FEATURE_IDE_PRIMARY_MASTER_PIO |    \
     IA64_I2000_IO_TEST_FEATURE_EXTINT_ACK |                \
     IA64_I2000_IO_TEST_FEATURE_I82559C |                    \
     IA64_I2000_IO_TEST_FEATURE_ISP12160)

#define IA64_I2000_IO_TEST_DISABLED_FEATURES        \
    (IA64_I2000_IO_TEST_FEATURE_IDE_SECONDARY |             \
     IA64_I2000_IO_TEST_FEATURE_IDE_SLAVE |                 \
     IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA |                 \
     IA64_I2000_IO_TEST_FEATURE_SUPERIO_FLOPPY |            \
     IA64_I2000_IO_TEST_FEATURE_SUPERIO_ADDITIONAL_UART |   \
     IA64_I2000_IO_TEST_FEATURE_IFB_USB |                   \
     IA64_I2000_IO_TEST_FEATURE_IFB_PM |                    \
     IA64_I2000_IO_TEST_FEATURE_IFB_SMBUS |                 \
     IA64_I2000_IO_TEST_FEATURE_IFB_FWH |                   \
     IA64_I2000_IO_TEST_FEATURE_PICMODE |                   \
     IA64_I2000_IO_TEST_FEATURE_RTC_PERSISTENCE |           \
     IA64_I2000_IO_TEST_FEATURE_EFI_VARIABLES |             \
     IA64_I2000_IO_TEST_FEATURE_ISA_DMA)

#define IA64_I2000_IO_TEST_KNOWN_FEATURES           \
    (IA64_I2000_IO_TEST_ENABLED_FEATURES |          \
     IA64_I2000_IO_TEST_DISABLED_FEATURES)

typedef struct IA64I2000IoTestIOPortRange {
    uint32_t base;
    uint32_t size;
} IA64I2000IoTestIOPortRange;

typedef struct IA64I2000IoTestPCIFunction {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t class_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t function;
} IA64I2000IoTestPCIFunction;

typedef struct IA64I2000IoTestLayout {
    uint8_t parent_root;
    IA64I2000IoTestIOPortRange parent_io;
    IA64I2000IoTestIOPortRange parent_cf8;
    IA64I2000IoTestIOPortRange parent_cfc;

    uint8_t pci_slot;
    uint8_t pci_function_count;
    IA64I2000IoTestPCIFunction f0;
    IA64I2000IoTestPCIFunction f1;

    uint8_t i82559_parent_root;
    uint8_t i82559_slot;
    uint8_t i82559_interrupt_pin;
    uint8_t i82559_pid_pin;
    IA64I2000IoTestPCIFunction i82559;
    uint32_t i82559_mmio_bar_size;
    uint32_t i82559_io_bar_size;
    uint32_t i82559_flash_bar_size;
    uint16_t i82559_eeprom_words;
    uint16_t i82559_eeprom_checksum;
    uint16_t i82559_mac_word0;
    uint16_t i82559_mac_word1;
    uint16_t i82559_mac_word2;
    uint8_t i82559_option_rom_enabled;

    uint8_t isp12160_parent_root;
    uint8_t isp12160_bus;
    uint8_t isp12160_slot;
    uint8_t isp12160_interrupt_pin;
    uint8_t isp12160_pid_pin;
    IA64I2000IoTestPCIFunction isp12160;
    uint32_t isp12160_io_bar_base;
    uint32_t isp12160_io_bar_size;
    uint32_t isp12160_mmio_bar_base;
    uint32_t isp12160_mmio_bar_size;
    uint8_t isp12160_option_rom_enabled;

    IA64I2000IoTestIOPortRange pic_master;
    IA64I2000IoTestIOPortRange pic_slave;
    IA64I2000IoTestIOPortRange elcr_master;
    IA64I2000IoTestIOPortRange elcr_slave;
    uint8_t pic_cascade_irq;
    uint8_t pid_legacy_pin;

    IA64I2000IoTestIOPortRange pit;
    uint8_t pit_irq;

    IA64I2000IoTestIOPortRange superio_config;
    uint8_t superio_sysopt;
    uint8_t superio_enter_key;
    uint8_t superio_enter_count;
    uint8_t superio_exit_key;
    uint8_t superio_ldn_select_cr;
    uint8_t superio_device_id_cr;
    uint8_t superio_device_id;

    uint8_t uart_ldn;
    uint8_t uart_activate_cr;
    uint8_t uart_base_msb_cr;
    uint8_t uart_base_lsb_cr;
    uint8_t uart_irq_cr;
    uint8_t uart_mode_cr;
    uint8_t uart_reset_active;
    IA64I2000IoTestIOPortRange uart;
    uint8_t uart_irq;
    uint32_t uart_input_clock_hz;

    uint8_t i8042_ldn;
    uint8_t i8042_activate_cr;
    uint8_t i8042_kbd_irq_cr;
    uint8_t i8042_mouse_irq_cr;
    uint8_t i8042_reset_active;
    IA64I2000IoTestIOPortRange i8042_data;
    IA64I2000IoTestIOPortRange i8042_command;
    uint8_t i8042_kbd_irq;
    uint8_t i8042_mouse_irq;

    IA64I2000IoTestIOPortRange rtc_bank0;
    IA64I2000IoTestIOPortRange rtc_bank1;
    uint8_t rtc_irq;
    int32_t rtc_base_year;

    IA64I2000IoTestIOPortRange ide_command;
    IA64I2000IoTestIOPortRange ide_control;
    uint8_t ide_irq;
    uint8_t ide_master_unit;

    uint32_t enabled_features;
    uint32_t disabled_features;
} IA64I2000IoTestLayout;

/* Populate the fixed i2000 I/O test layout. */
void ia64_i2000_io_test_layout_init(IA64I2000IoTestLayout *layout);

/* Validate without creating or mapping devices. */
bool ia64_i2000_io_test_layout_validate(const IA64I2000IoTestLayout *layout,
                                        Error **errp);

#endif /* HW_IA64_I2000_TEST_LAYOUT_H */
