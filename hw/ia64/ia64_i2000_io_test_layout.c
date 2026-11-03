/*
 * IA-64 i2000 I/O test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "qapi/error.h"

typedef struct IoTestPortDecode {
    const IA64I2000IoTestIOPortRange *range;
    const char *name;
} IoTestPortDecode;

static const IA64I2000IoTestLayout fixed_layout = {
    .parent_root = IA64_I2000_IO_TEST_PARENT_ROOT,
    .parent_io = {
        .base = IA64_I2000_IO_TEST_PARENT_IO_BASE,
        .size = IA64_I2000_IO_TEST_PARENT_IO_SIZE,
    },
    .parent_cf8 = {
        .base = IA64_I2000_IO_TEST_PARENT_CF8_BASE,
        .size = IA64_I2000_IO_TEST_PCI_CONFIG_PORT_SIZE,
    },
    .parent_cfc = {
        .base = IA64_I2000_IO_TEST_PARENT_CFC_BASE,
        .size = IA64_I2000_IO_TEST_PCI_CONFIG_PORT_SIZE,
    },
    .pci_slot = IA64_I2000_IO_TEST_PCI_SLOT,
    .pci_function_count = IA64_I2000_IO_TEST_PCI_FUNCTION_COUNT,
    .f0 = {
        .vendor_id = IA64_I2000_IO_TEST_F0_VENDOR_ID,
        .device_id = IA64_I2000_IO_TEST_F0_DEVICE_ID,
        .class_id = IA64_I2000_IO_TEST_F0_CLASS,
        .subsystem_vendor_id =
            IA64_I2000_IO_TEST_F0_SUBSYSTEM_VENDOR_ID,
        .subsystem_id = IA64_I2000_IO_TEST_F0_SUBSYSTEM_ID,
        .revision = IA64_I2000_IO_TEST_F0_REVISION,
        .prog_if = IA64_I2000_IO_TEST_F0_PROG_IF,
        .function = IA64_I2000_IO_TEST_F0_FUNCTION,
    },
    .f1 = {
        .vendor_id = IA64_I2000_IO_TEST_F1_VENDOR_ID,
        .device_id = IA64_I2000_IO_TEST_F1_DEVICE_ID,
        .class_id = IA64_I2000_IO_TEST_F1_CLASS,
        .subsystem_vendor_id =
            IA64_I2000_IO_TEST_F1_SUBSYSTEM_VENDOR_ID,
        .subsystem_id = IA64_I2000_IO_TEST_F1_SUBSYSTEM_ID,
        .revision = IA64_I2000_IO_TEST_F1_REVISION,
        .prog_if = IA64_I2000_IO_TEST_F1_PROG_IF,
        .function = IA64_I2000_IO_TEST_F1_FUNCTION,
    },
    .i82559_parent_root = IA64_I2000_IO_TEST_I82559_PARENT_ROOT,
    .i82559_slot = IA64_I2000_IO_TEST_I82559_SLOT,
    .i82559_interrupt_pin = IA64_I2000_IO_TEST_I82559_INTERRUPT_PIN,
    .i82559_pid_pin = IA64_I2000_IO_TEST_I82559_PID_PIN,
    .i82559 = {
        .vendor_id = IA64_I2000_IO_TEST_I82559_VENDOR_ID,
        .device_id = IA64_I2000_IO_TEST_I82559_DEVICE_ID,
        .class_id = IA64_I2000_IO_TEST_I82559_CLASS,
        .subsystem_vendor_id =
            IA64_I2000_IO_TEST_I82559_SUBSYSTEM_VENDOR_ID,
        .subsystem_id = IA64_I2000_IO_TEST_I82559_SUBSYSTEM_ID,
        .revision = IA64_I2000_IO_TEST_I82559_REVISION,
        .prog_if = IA64_I2000_IO_TEST_I82559_PROG_IF,
        .function = IA64_I2000_IO_TEST_I82559_FUNCTION,
    },
    .i82559_mmio_bar_size = IA64_I2000_IO_TEST_I82559_MMIO_BAR_SIZE,
    .i82559_io_bar_size = IA64_I2000_IO_TEST_I82559_IO_BAR_SIZE,
    .i82559_flash_bar_size = IA64_I2000_IO_TEST_I82559_FLASH_BAR_SIZE,
    .i82559_eeprom_words = IA64_I2000_IO_TEST_I82559_EEPROM_WORDS,
    .i82559_eeprom_checksum =
        IA64_I2000_IO_TEST_I82559_EEPROM_CHECKSUM,
    .i82559_mac_word0 = IA64_I2000_IO_TEST_I82559_MAC_WORD0,
    .i82559_mac_word1 = IA64_I2000_IO_TEST_I82559_MAC_WORD1,
    .i82559_mac_word2 = IA64_I2000_IO_TEST_I82559_MAC_WORD2,
    .i82559_option_rom_enabled =
        IA64_I2000_IO_TEST_I82559_OPTION_ROM_ENABLED,
    .isp12160_parent_root =
        IA64_I2000_IO_TEST_ISP12160_PARENT_ROOT,
    .isp12160_bus = IA64_I2000_IO_TEST_ISP12160_BUS,
    .isp12160_slot = IA64_I2000_IO_TEST_ISP12160_SLOT,
    .isp12160_interrupt_pin =
        IA64_I2000_IO_TEST_ISP12160_INTERRUPT_PIN,
    .isp12160_pid_pin = IA64_I2000_IO_TEST_ISP12160_PID_PIN,
    .isp12160 = {
        .vendor_id = IA64_I2000_IO_TEST_ISP12160_VENDOR_ID,
        .device_id = IA64_I2000_IO_TEST_ISP12160_DEVICE_ID,
        .class_id = IA64_I2000_IO_TEST_ISP12160_CLASS,
        .subsystem_vendor_id =
            IA64_I2000_IO_TEST_ISP12160_SUBSYSTEM_VENDOR_ID,
        .subsystem_id = IA64_I2000_IO_TEST_ISP12160_SUBSYSTEM_ID,
        .revision = IA64_I2000_IO_TEST_ISP12160_REVISION,
        .prog_if = IA64_I2000_IO_TEST_ISP12160_PROG_IF,
        .function = IA64_I2000_IO_TEST_ISP12160_FUNCTION,
    },
    .isp12160_io_bar_base =
        IA64_I2000_IO_TEST_ISP12160_IO_BAR_BASE,
    .isp12160_io_bar_size =
        IA64_I2000_IO_TEST_ISP12160_IO_BAR_SIZE,
    .isp12160_mmio_bar_base =
        IA64_I2000_IO_TEST_ISP12160_MMIO_BAR_BASE,
    .isp12160_mmio_bar_size =
        IA64_I2000_IO_TEST_ISP12160_MMIO_BAR_SIZE,
    .isp12160_option_rom_enabled =
        IA64_I2000_IO_TEST_ISP12160_OPTION_ROM_ENABLED,
    .pic_master = {
        .base = IA64_I2000_IO_TEST_PIC_MASTER_BASE,
        .size = IA64_I2000_IO_TEST_PIC_PORT_SIZE,
    },
    .pic_slave = {
        .base = IA64_I2000_IO_TEST_PIC_SLAVE_BASE,
        .size = IA64_I2000_IO_TEST_PIC_PORT_SIZE,
    },
    .elcr_master = {
        .base = IA64_I2000_IO_TEST_ELCR_MASTER_BASE,
        .size = IA64_I2000_IO_TEST_ELCR_PORT_SIZE,
    },
    .elcr_slave = {
        .base = IA64_I2000_IO_TEST_ELCR_SLAVE_BASE,
        .size = IA64_I2000_IO_TEST_ELCR_PORT_SIZE,
    },
    .pic_cascade_irq = IA64_I2000_IO_TEST_PIC_CASCADE_IRQ,
    .pid_legacy_pin = IA64_I2000_IO_TEST_PID_LEGACY_PIN,
    .pit = {
        .base = IA64_I2000_IO_TEST_PIT_BASE,
        .size = IA64_I2000_IO_TEST_PIT_SIZE,
    },
    .pit_irq = IA64_I2000_IO_TEST_PIT_IRQ,
    .superio_config = {
        .base = IA64_I2000_IO_TEST_SIO_CONFIG_BASE,
        .size = IA64_I2000_IO_TEST_SIO_CONFIG_SIZE,
    },
    .superio_sysopt = IA64_I2000_IO_TEST_SIO_SYSOPT,
    .superio_enter_key = IA64_I2000_IO_TEST_SIO_ENTER_KEY,
    .superio_enter_count = IA64_I2000_IO_TEST_SIO_ENTER_COUNT,
    .superio_exit_key = IA64_I2000_IO_TEST_SIO_EXIT_KEY,
    .superio_ldn_select_cr = IA64_I2000_IO_TEST_SIO_LDN_SELECT_CR,
    .superio_device_id_cr = IA64_I2000_IO_TEST_SIO_DEVICE_ID_CR,
    .superio_device_id = IA64_I2000_IO_TEST_SIO_DEVICE_ID,
    .uart_ldn = IA64_I2000_IO_TEST_UART_LDN,
    .uart_activate_cr = IA64_I2000_IO_TEST_UART_ACTIVATE_CR,
    .uart_base_msb_cr = IA64_I2000_IO_TEST_UART_BASE_MSB_CR,
    .uart_base_lsb_cr = IA64_I2000_IO_TEST_UART_BASE_LSB_CR,
    .uart_irq_cr = IA64_I2000_IO_TEST_UART_IRQ_CR,
    .uart_mode_cr = IA64_I2000_IO_TEST_UART_MODE_CR,
    .uart_reset_active = IA64_I2000_IO_TEST_UART_RESET_ACTIVE,
    .uart = {
        .base = IA64_I2000_IO_TEST_UART_BASE,
        .size = IA64_I2000_IO_TEST_UART_SIZE,
    },
    .uart_irq = IA64_I2000_IO_TEST_UART_IRQ,
    .uart_input_clock_hz = IA64_I2000_IO_TEST_UART_INPUT_CLOCK_HZ,
    .i8042_ldn = IA64_I2000_IO_TEST_I8042_LDN,
    .i8042_activate_cr = IA64_I2000_IO_TEST_I8042_ACTIVATE_CR,
    .i8042_kbd_irq_cr = IA64_I2000_IO_TEST_I8042_KBD_IRQ_CR,
    .i8042_mouse_irq_cr = IA64_I2000_IO_TEST_I8042_MOUSE_IRQ_CR,
    .i8042_reset_active = IA64_I2000_IO_TEST_I8042_RESET_ACTIVE,
    .i8042_data = {
        .base = IA64_I2000_IO_TEST_I8042_DATA_BASE,
        .size = IA64_I2000_IO_TEST_I8042_PORT_SIZE,
    },
    .i8042_command = {
        .base = IA64_I2000_IO_TEST_I8042_COMMAND_BASE,
        .size = IA64_I2000_IO_TEST_I8042_PORT_SIZE,
    },
    .i8042_kbd_irq = IA64_I2000_IO_TEST_I8042_KBD_IRQ,
    .i8042_mouse_irq = IA64_I2000_IO_TEST_I8042_MOUSE_IRQ,
    .rtc_bank0 = {
        .base = IA64_I2000_IO_TEST_RTC_BANK0_BASE,
        .size = IA64_I2000_IO_TEST_RTC_BANK_SIZE,
    },
    .rtc_bank1 = {
        .base = IA64_I2000_IO_TEST_RTC_BANK1_BASE,
        .size = IA64_I2000_IO_TEST_RTC_BANK_SIZE,
    },
    .rtc_irq = IA64_I2000_IO_TEST_RTC_IRQ,
    .rtc_base_year = IA64_I2000_IO_TEST_RTC_BASE_YEAR,
    .ide_command = {
        .base = IA64_I2000_IO_TEST_IDE_COMMAND_BASE,
        .size = IA64_I2000_IO_TEST_IDE_COMMAND_SIZE,
    },
    .ide_control = {
        .base = IA64_I2000_IO_TEST_IDE_CONTROL_BASE,
        .size = IA64_I2000_IO_TEST_IDE_CONTROL_SIZE,
    },
    .ide_irq = IA64_I2000_IO_TEST_IDE_IRQ,
    .ide_master_unit = IA64_I2000_IO_TEST_IDE_MASTER_UNIT,
    .enabled_features = IA64_I2000_IO_TEST_ENABLED_FEATURES,
    .disabled_features = IA64_I2000_IO_TEST_DISABLED_FEATURES,
};

G_STATIC_ASSERT((IA64_I2000_IO_TEST_ENABLED_FEATURES &
                 IA64_I2000_IO_TEST_DISABLED_FEATURES) == 0);
G_STATIC_ASSERT((IA64_I2000_IO_TEST_ENABLED_FEATURES |
                 IA64_I2000_IO_TEST_DISABLED_FEATURES) ==
                IA64_I2000_IO_TEST_KNOWN_FEATURES);

static uint64_t io_test_port_end(const IA64I2000IoTestIOPortRange *range)
{
    return (uint64_t)range->base + range->size;
}

static bool io_test_port_range_valid(const IA64I2000IoTestIOPortRange *range)
{
    return range->size != 0 && io_test_port_end(range) <= UINT64_C(0x10000);
}

static bool io_test_port_range_contains(const IA64I2000IoTestIOPortRange *outer,
                                        const IA64I2000IoTestIOPortRange *inner)
{
    return inner->base >= outer->base &&
           io_test_port_end(inner) <= io_test_port_end(outer);
}

static bool io_test_port_ranges_overlap(const IA64I2000IoTestIOPortRange *a,
                                        const IA64I2000IoTestIOPortRange *b)
{
    return a->base < io_test_port_end(b) && b->base < io_test_port_end(a);
}

static bool io_test_port_range_matches(const IA64I2000IoTestIOPortRange *a,
                                       const IA64I2000IoTestIOPortRange *b)
{
    return a->base == b->base && a->size == b->size;
}

static bool io_test_pci_function_matches(const IA64I2000IoTestPCIFunction *a,
                                         const IA64I2000IoTestPCIFunction *b)
{
    return a->vendor_id == b->vendor_id && a->device_id == b->device_id &&
           a->class_id == b->class_id &&
           a->subsystem_vendor_id == b->subsystem_vendor_id &&
           a->subsystem_id == b->subsystem_id && a->revision == b->revision &&
           a->prog_if == b->prog_if && a->function == b->function;
}

static bool io_test_reserve_irq(bool used[16], uint8_t irq, const char *role,
                                Error **errp)
{
    if (irq >= 16) {
        error_setg(errp, "i2000 I/O test %s IRQ is outside 0..15", role);
        return false;
    }
    if (used[irq]) {
        error_setg(errp, "i2000 I/O test %s reuses IRQ %u", role, irq);
        return false;
    }
    used[irq] = true;
    return true;
}

static bool io_test_validate_pci(const IA64I2000IoTestLayout *layout,
                                 Error **errp)
{
    if (layout->pci_slot >= 32 || layout->pci_function_count != 2 ||
        layout->f0.function >= 8 || layout->f1.function >= 8 ||
        layout->f0.function == layout->f1.function) {
        error_setg(errp, "i2000 I/O test PCI multifunction pairing is invalid");
        return false;
    }
    if (layout->f0.function != 0 || layout->f1.function != 1) {
        error_setg(errp,
                   "i2000 I/O test IFB functions must be function 0 and 1");
        return false;
    }
    if (layout->i82559_parent_root >= 3 || layout->i82559_slot >= 32 ||
        layout->i82559.function >= 8 ||
        (layout->i82559_parent_root == layout->parent_root &&
         layout->i82559_slot == layout->pci_slot) ||
        layout->i82559_interrupt_pin < 1 || layout->i82559_interrupt_pin > 4 ||
        layout->i82559_pid_pin != 16 + layout->i82559_parent_root * 4 +
                                      layout->i82559_interrupt_pin - 1) {
        error_setg(errp,
                   "i2000 I/O test i82559 placement or INTx route is invalid");
        return false;
    }
    if (layout->i82559_mmio_bar_size == 0 ||
        !is_power_of_2(layout->i82559_mmio_bar_size) ||
        layout->i82559_io_bar_size == 0 ||
        !is_power_of_2(layout->i82559_io_bar_size) ||
        layout->i82559_flash_bar_size == 0 ||
        !is_power_of_2(layout->i82559_flash_bar_size) ||
        layout->i82559_eeprom_words != 64 ||
        layout->i82559_option_rom_enabled != 0) {
        error_setg(errp, "i2000 I/O test i82559 configuration is invalid");
        return false;
    }
    if (layout->isp12160_parent_root >= 3 ||
        layout->isp12160_bus != ISP12160_QEMU_I2000_BUS ||
        layout->isp12160_slot >= 32 || layout->isp12160.function >= 8 ||
        (layout->isp12160_parent_root == layout->parent_root &&
         layout->isp12160_slot == layout->pci_slot) ||
        (layout->isp12160_parent_root == layout->i82559_parent_root &&
         layout->isp12160_slot == layout->i82559_slot) ||
        layout->isp12160_interrupt_pin < 1 ||
        layout->isp12160_interrupt_pin > 4 ||
        layout->isp12160_pid_pin != 16 + layout->isp12160_parent_root * 4 +
                                        layout->isp12160_interrupt_pin - 1 ||
        layout->isp12160_io_bar_size == 0 ||
        !is_power_of_2(layout->isp12160_io_bar_size) ||
        (layout->isp12160_io_bar_base & (layout->isp12160_io_bar_size - 1U)) !=
            0 ||
        layout->isp12160_mmio_bar_size == 0 ||
        !is_power_of_2(layout->isp12160_mmio_bar_size) ||
        (layout->isp12160_mmio_bar_base &
         (layout->isp12160_mmio_bar_size - 1U)) != 0 ||
        layout->isp12160_option_rom_enabled != 0) {
        error_setg(
            errp,
            "i2000 I/O test ISP12160 placement, BAR, or INTx route is invalid");
        return false;
    }
    return true;
}

static bool io_test_validate_ports(const IA64I2000IoTestLayout *layout,
                                   Error **errp)
{
    const IoTestPortDecode ports[] = {
        { &layout->parent_cf8, "parent CF8" },
        { &layout->parent_cfc, "parent CFC" },
        { &layout->pic_master, "PIC master" },
        { &layout->pic_slave, "PIC slave" },
        { &layout->elcr_master, "ELCR master" },
        { &layout->elcr_slave, "ELCR slave" },
        { &layout->pit, "PIT" },
        { &layout->superio_config, "Super I/O configuration" },
        { &layout->uart, "UART" },
        { &layout->i8042_data, "i8042 data" },
        { &layout->i8042_command, "i8042 command" },
        { &layout->rtc_bank0, "RTC bank 0" },
        { &layout->rtc_bank1, "RTC bank 1" },
        { &layout->ide_command, "IDE primary command" },
        { &layout->ide_control, "IDE primary control" },
    };
    unsigned i;
    unsigned j;

    if (!io_test_port_range_valid(&layout->parent_io)) {
        error_setg(errp, "i2000 I/O test parent I/O range is invalid");
        return false;
    }

    for (i = 0; i < G_N_ELEMENTS(ports); i++) {
        if (!io_test_port_range_valid(ports[i].range) ||
            !io_test_port_range_contains(&layout->parent_io, ports[i].range)) {
            error_setg(errp,
                       "i2000 I/O test %s decode is outside parent root 0",
                       ports[i].name);
            return false;
        }
        for (j = 0; j < i; j++) {
            if (io_test_port_ranges_overlap(ports[j].range, ports[i].range)) {
                error_setg(errp, "i2000 I/O test %s and %s I/O decodes overlap",
                           ports[j].name, ports[i].name);
                return false;
            }
        }
    }

    /*
     * i8042 leaves an undecoded gap; the RTC banks use separate index/data
     * pairs.
     */
    if (io_test_port_end(&layout->i8042_data) >= layout->i8042_command.base) {
        error_setg(errp, "i2000 I/O test i8042 ports must be noncontiguous");
        return false;
    }
    if (io_test_port_end(&layout->rtc_bank0) != layout->rtc_bank1.base) {
        error_setg(errp,
                   "i2000 I/O test RTC banks must be adjacent and distinct");
        return false;
    }
    return true;
}

static bool io_test_validate_irqs(const IA64I2000IoTestLayout *layout,
                                  Error **errp)
{
    bool used[16] = { false };

    /* IRQ 2 is the PIC cascade; slave IRQs 8..15 remain distinct ISA inputs. */
    if (!io_test_reserve_irq(used, layout->pic_cascade_irq, "PIC cascade",
                             errp) ||
        !io_test_reserve_irq(used, layout->pit_irq, "PIT", errp) ||
        !io_test_reserve_irq(used, layout->i8042_kbd_irq, "i8042 keyboard",
                             errp) ||
        !io_test_reserve_irq(used, layout->uart_irq, "UART", errp) ||
        !io_test_reserve_irq(used, layout->rtc_irq, "RTC", errp) ||
        !io_test_reserve_irq(used, layout->i8042_mouse_irq, "i8042 mouse",
                             errp) ||
        !io_test_reserve_irq(used, layout->ide_irq, "IDE", errp)) {
        return false;
    }
    if (layout->pic_cascade_irq >= 8 || layout->pit_irq >= 8 ||
        layout->i8042_kbd_irq >= 8 || layout->uart_irq >= 8 ||
        layout->rtc_irq < 8 || layout->i8042_mouse_irq < 8 ||
        layout->ide_irq < 8) {
        error_setg(errp, "i2000 I/O test IRQ roles do not match the PIC pair");
        return false;
    }
    if (layout->pid_legacy_pin >= IA64_I2000_IO_TEST_460GX_LEGACY_PIN_COUNT) {
        error_setg(errp,
                   "i2000 I/O test PID legacy pin is outside its reserve");
        return false;
    }
    return true;
}

static bool io_test_layout_is_fixed(const IA64I2000IoTestLayout *layout)
{
    return layout->parent_root == fixed_layout.parent_root &&
           io_test_port_range_matches(&layout->parent_io,
                                      &fixed_layout.parent_io) &&
           io_test_port_range_matches(&layout->parent_cf8,
                                      &fixed_layout.parent_cf8) &&
           io_test_port_range_matches(&layout->parent_cfc,
                                      &fixed_layout.parent_cfc) &&
           layout->pci_slot == fixed_layout.pci_slot &&
           layout->pci_function_count == fixed_layout.pci_function_count &&
           io_test_pci_function_matches(&layout->f0, &fixed_layout.f0) &&
           io_test_pci_function_matches(&layout->f1, &fixed_layout.f1) &&
           layout->i82559_parent_root == fixed_layout.i82559_parent_root &&
           layout->i82559_slot == fixed_layout.i82559_slot &&
           layout->i82559_interrupt_pin == fixed_layout.i82559_interrupt_pin &&
           layout->i82559_pid_pin == fixed_layout.i82559_pid_pin &&
           io_test_pci_function_matches(&layout->i82559,
                                        &fixed_layout.i82559) &&
           layout->i82559_mmio_bar_size == fixed_layout.i82559_mmio_bar_size &&
           layout->i82559_io_bar_size == fixed_layout.i82559_io_bar_size &&
           layout->i82559_flash_bar_size ==
               fixed_layout.i82559_flash_bar_size &&
           layout->i82559_eeprom_words == fixed_layout.i82559_eeprom_words &&
           layout->i82559_eeprom_checksum ==
               fixed_layout.i82559_eeprom_checksum &&
           layout->i82559_mac_word0 == fixed_layout.i82559_mac_word0 &&
           layout->i82559_mac_word1 == fixed_layout.i82559_mac_word1 &&
           layout->i82559_mac_word2 == fixed_layout.i82559_mac_word2 &&
           layout->i82559_option_rom_enabled ==
               fixed_layout.i82559_option_rom_enabled &&
           layout->isp12160_parent_root == fixed_layout.isp12160_parent_root &&
           layout->isp12160_bus == fixed_layout.isp12160_bus &&
           layout->isp12160_slot == fixed_layout.isp12160_slot &&
           layout->isp12160_interrupt_pin ==
               fixed_layout.isp12160_interrupt_pin &&
           layout->isp12160_pid_pin == fixed_layout.isp12160_pid_pin &&
           io_test_pci_function_matches(&layout->isp12160,
                                        &fixed_layout.isp12160) &&
           layout->isp12160_io_bar_base == fixed_layout.isp12160_io_bar_base &&
           layout->isp12160_io_bar_size == fixed_layout.isp12160_io_bar_size &&
           layout->isp12160_mmio_bar_base ==
               fixed_layout.isp12160_mmio_bar_base &&
           layout->isp12160_mmio_bar_size ==
               fixed_layout.isp12160_mmio_bar_size &&
           layout->isp12160_option_rom_enabled ==
               fixed_layout.isp12160_option_rom_enabled &&
           io_test_port_range_matches(&layout->pic_master,
                                      &fixed_layout.pic_master) &&
           io_test_port_range_matches(&layout->pic_slave,
                                      &fixed_layout.pic_slave) &&
           io_test_port_range_matches(&layout->elcr_master,
                                      &fixed_layout.elcr_master) &&
           io_test_port_range_matches(&layout->elcr_slave,
                                      &fixed_layout.elcr_slave) &&
           layout->pic_cascade_irq == fixed_layout.pic_cascade_irq &&
           layout->pid_legacy_pin == fixed_layout.pid_legacy_pin &&
           io_test_port_range_matches(&layout->pit, &fixed_layout.pit) &&
           layout->pit_irq == fixed_layout.pit_irq &&
           io_test_port_range_matches(&layout->superio_config,
                                      &fixed_layout.superio_config) &&
           layout->superio_sysopt == fixed_layout.superio_sysopt &&
           layout->superio_enter_key == fixed_layout.superio_enter_key &&
           layout->superio_enter_count == fixed_layout.superio_enter_count &&
           layout->superio_exit_key == fixed_layout.superio_exit_key &&
           layout->superio_ldn_select_cr ==
               fixed_layout.superio_ldn_select_cr &&
           layout->superio_device_id_cr == fixed_layout.superio_device_id_cr &&
           layout->superio_device_id == fixed_layout.superio_device_id &&
           layout->uart_ldn == fixed_layout.uart_ldn &&
           layout->uart_activate_cr == fixed_layout.uart_activate_cr &&
           layout->uart_base_msb_cr == fixed_layout.uart_base_msb_cr &&
           layout->uart_base_lsb_cr == fixed_layout.uart_base_lsb_cr &&
           layout->uart_irq_cr == fixed_layout.uart_irq_cr &&
           layout->uart_mode_cr == fixed_layout.uart_mode_cr &&
           layout->uart_reset_active == fixed_layout.uart_reset_active &&
           io_test_port_range_matches(&layout->uart, &fixed_layout.uart) &&
           layout->uart_irq == fixed_layout.uart_irq &&
           layout->uart_input_clock_hz == fixed_layout.uart_input_clock_hz &&
           layout->i8042_ldn == fixed_layout.i8042_ldn &&
           layout->i8042_activate_cr == fixed_layout.i8042_activate_cr &&
           layout->i8042_kbd_irq_cr == fixed_layout.i8042_kbd_irq_cr &&
           layout->i8042_mouse_irq_cr == fixed_layout.i8042_mouse_irq_cr &&
           layout->i8042_reset_active == fixed_layout.i8042_reset_active &&
           io_test_port_range_matches(&layout->i8042_data,
                                      &fixed_layout.i8042_data) &&
           io_test_port_range_matches(&layout->i8042_command,
                                      &fixed_layout.i8042_command) &&
           layout->i8042_kbd_irq == fixed_layout.i8042_kbd_irq &&
           layout->i8042_mouse_irq == fixed_layout.i8042_mouse_irq &&
           io_test_port_range_matches(&layout->rtc_bank0,
                                      &fixed_layout.rtc_bank0) &&
           io_test_port_range_matches(&layout->rtc_bank1,
                                      &fixed_layout.rtc_bank1) &&
           layout->rtc_irq == fixed_layout.rtc_irq &&
           layout->rtc_base_year == fixed_layout.rtc_base_year &&
           io_test_port_range_matches(&layout->ide_command,
                                      &fixed_layout.ide_command) &&
           io_test_port_range_matches(&layout->ide_control,
                                      &fixed_layout.ide_control) &&
           layout->ide_irq == fixed_layout.ide_irq &&
           layout->ide_master_unit == fixed_layout.ide_master_unit &&
           layout->enabled_features == fixed_layout.enabled_features &&
           layout->disabled_features == fixed_layout.disabled_features;
}

void ia64_i2000_io_test_layout_init(IA64I2000IoTestLayout *layout)
{
    g_assert(layout != NULL);
    *layout = fixed_layout;
}

bool ia64_i2000_io_test_layout_validate(const IA64I2000IoTestLayout *layout,
                                        Error **errp)
{
    if (layout == NULL) {
        error_setg(errp, "i2000 I/O test layout is NULL");
        return false;
    }
    if (!io_test_validate_pci(layout, errp) ||
        !io_test_validate_ports(layout, errp) ||
        !io_test_validate_irqs(layout, errp)) {
        return false;
    }
    if (layout->uart_ldn == layout->i8042_ldn || layout->uart_ldn >= 32 ||
        layout->i8042_ldn >= 32 || layout->superio_enter_count == 0 ||
        layout->uart_input_clock_hz == 0 ||
        layout->uart_input_clock_hz % 16 != 0 ||
        layout->uart_reset_active != 0 || layout->i8042_reset_active != 0) {
        error_setg(errp, "i2000 I/O test Super I/O roles or clock are invalid");
        return false;
    }
    if (layout->rtc_base_year <= 0 || layout->ide_master_unit >= 2) {
        error_setg(errp, "i2000 I/O test RTC or IDE role is invalid");
        return false;
    }
    if ((layout->enabled_features & layout->disabled_features) != 0 ||
        (layout->enabled_features | layout->disabled_features) !=
            IA64_I2000_IO_TEST_KNOWN_FEATURES) {
        error_setg(errp, "i2000 I/O test feature masks are inconsistent");
        return false;
    }
    if (!io_test_layout_is_fixed(layout)) {
        error_setg(errp, "i2000 I/O test layout is not fixed");
        return false;
    }
    return true;
}
