/*
 * IA-64 i2000 I/O layout tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "qapi/error.h"

static IA64I2000IoTestLayout io_test_layout(void)
{
    IA64I2000IoTestLayout layout;

    ia64_i2000_io_test_layout_init(&layout);
    return layout;
}

static void assert_invalid(IA64I2000IoTestLayout *layout)
{
    Error *err = NULL;

    g_assert_false(ia64_i2000_io_test_layout_validate(layout, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_fixed_layout(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();
    Error *err = NULL;

    g_assert_true(ia64_i2000_io_test_layout_validate(&layout, &err));
    g_assert_null(err);

    g_assert_cmpuint(layout.parent_root, ==, 0);
    g_assert_cmphex(layout.parent_io.base, ==, 0x0000);
    g_assert_cmphex(layout.parent_io.size, ==, 0x4000);
    g_assert_cmphex(layout.parent_cf8.base, ==, 0x0cf8);
    g_assert_cmphex(layout.parent_cfc.base, ==, 0x0cfc);

    g_assert_cmpuint(layout.pci_slot, ==, 2);
    g_assert_cmpuint(layout.pci_function_count, ==, 2);
    g_assert_cmpuint(layout.f0.function, ==, 0);
    g_assert_cmphex(layout.f0.vendor_id, ==, 0x1234);
    g_assert_cmphex(layout.f0.device_id, ==, 0x0460);
    g_assert_cmphex(layout.f0.class_id, ==, 0x0601);
    g_assert_cmphex(layout.f0.subsystem_vendor_id, ==, 0);
    g_assert_cmphex(layout.f0.subsystem_id, ==, 0);
    g_assert_cmpuint(layout.f0.revision, ==, 0);
    g_assert_cmpuint(layout.f0.prog_if, ==, 0);
    g_assert_cmpuint(layout.f1.function, ==, 1);
    g_assert_cmphex(layout.f1.vendor_id, ==, 0x8086);
    g_assert_cmphex(layout.f1.device_id, ==, 0x7601);
    g_assert_cmphex(layout.f1.class_id, ==, 0x0101);
    g_assert_cmphex(layout.f1.subsystem_vendor_id, ==, 0);
    g_assert_cmphex(layout.f1.subsystem_id, ==, 0);
    g_assert_cmpuint(layout.f1.revision, ==, 0);
    g_assert_cmpuint(layout.f1.prog_if, ==, 0x80);

    g_assert_cmpuint(layout.i82559_parent_root, ==, 0);
    g_assert_cmpuint(layout.i82559_slot, ==, 3);
    g_assert_cmpuint(layout.i82559.function, ==, 0);
    g_assert_cmphex(layout.i82559.vendor_id, ==, 0x8086);
    g_assert_cmphex(layout.i82559.device_id, ==, 0x1229);
    g_assert_cmphex(layout.i82559.class_id, ==, 0x0200);
    g_assert_cmphex(layout.i82559.subsystem_vendor_id, ==, 0x8086);
    g_assert_cmphex(layout.i82559.subsystem_id, ==, 0x0040);
    g_assert_cmpuint(layout.i82559.revision, ==, 0x08);
    g_assert_cmpuint(layout.i82559.prog_if, ==, 0);
    g_assert_cmpuint(layout.i82559_interrupt_pin, ==, 1);
    g_assert_cmpuint(layout.i82559_pid_pin, ==, 16);
    g_assert_cmphex(layout.i82559_mmio_bar_size, ==, 0x1000);
    g_assert_cmphex(layout.i82559_io_bar_size, ==, 0x40);
    g_assert_cmphex(layout.i82559_flash_bar_size, ==, 0x20000);
    g_assert_cmpuint(layout.i82559_eeprom_words, ==, 64);
    g_assert_cmphex(layout.i82559_eeprom_checksum, ==, 0xbaba);
    g_assert_cmphex(layout.i82559_mac_word0, ==, 0x5452);
    g_assert_cmphex(layout.i82559_mac_word1, ==, 0x2000);
    g_assert_cmphex(layout.i82559_mac_word2, ==, 0x0100);
    g_assert_cmpuint(layout.i82559_option_rom_enabled, ==, 0);

    g_assert_cmpuint(layout.isp12160_parent_root, ==, 1);
    g_assert_cmpuint(layout.isp12160_bus, ==, 0x01);
    g_assert_cmpuint(layout.isp12160_slot, ==, 0);
    g_assert_cmpuint(layout.isp12160.function, ==, 0);
    g_assert_cmphex(layout.isp12160.vendor_id, ==, 0x1077);
    g_assert_cmphex(layout.isp12160.device_id, ==, 0x1216);
    g_assert_cmphex(layout.isp12160.class_id, ==, 0x0100);
    g_assert_cmphex(layout.isp12160.subsystem_vendor_id, ==, 0x1077);
    g_assert_cmphex(layout.isp12160.subsystem_id, ==, 0x0007);
    g_assert_cmpuint(layout.isp12160.revision, ==, 0x06);
    g_assert_cmpuint(layout.isp12160.prog_if, ==, 0);
    g_assert_cmpuint(layout.isp12160_interrupt_pin, ==, 1);
    g_assert_cmpuint(layout.isp12160_pid_pin, ==, 20);
    g_assert_cmphex(layout.isp12160_io_bar_base, ==, 0x5000);
    g_assert_cmphex(layout.isp12160_io_bar_size, ==, 0x100);
    g_assert_cmphex(layout.isp12160_mmio_bar_base, ==, 0xa0010000);
    g_assert_cmphex(layout.isp12160_mmio_bar_size, ==, 0x1000);
    g_assert_cmpuint(layout.isp12160_option_rom_enabled, ==, 0);

    g_assert_cmphex(layout.pic_master.base, ==, 0x20);
    g_assert_cmphex(layout.pic_slave.base, ==, 0xa0);
    g_assert_cmphex(layout.elcr_master.base, ==, 0x4d0);
    g_assert_cmphex(layout.elcr_slave.base, ==, 0x4d1);
    g_assert_cmpuint(layout.pic_cascade_irq, ==, 2);
    g_assert_cmpuint(layout.pid_legacy_pin, ==, 0);
    g_assert_cmphex(layout.pit.base, ==, 0x40);
    g_assert_cmpuint(layout.pit.size, ==, 4);
    g_assert_cmpuint(layout.pit_irq, ==, 0);

    g_assert_cmphex(layout.superio_config.base, ==, 0x2e);
    g_assert_cmpuint(layout.superio_sysopt, ==, 0);
    g_assert_cmphex(layout.superio_enter_key, ==, 0x55);
    g_assert_cmpuint(layout.superio_enter_count, ==, 1);
    g_assert_cmphex(layout.superio_exit_key, ==, 0xaa);
    g_assert_cmphex(layout.superio_ldn_select_cr, ==, 0x07);
    g_assert_cmphex(layout.superio_device_id_cr, ==, 0x20);
    g_assert_cmphex(layout.superio_device_id, ==, 0x51);

    g_assert_cmpuint(layout.uart_ldn, ==, 4);
    g_assert_cmphex(layout.uart_activate_cr, ==, 0x30);
    g_assert_cmphex(layout.uart_base_msb_cr, ==, 0x60);
    g_assert_cmphex(layout.uart_base_lsb_cr, ==, 0x61);
    g_assert_cmphex(layout.uart_irq_cr, ==, 0x70);
    g_assert_cmphex(layout.uart_mode_cr, ==, 0xf0);
    g_assert_cmpuint(layout.uart_reset_active, ==, 0);
    g_assert_cmphex(layout.uart.base, ==, 0x3f8);
    g_assert_cmpuint(layout.uart.size, ==, 8);
    g_assert_cmpuint(layout.uart_irq, ==, 4);
    g_assert_cmpuint(layout.uart_input_clock_hz, ==, 1843200);

    g_assert_cmpuint(layout.i8042_ldn, ==, 7);
    g_assert_cmphex(layout.i8042_activate_cr, ==, 0x30);
    g_assert_cmphex(layout.i8042_kbd_irq_cr, ==, 0x70);
    g_assert_cmphex(layout.i8042_mouse_irq_cr, ==, 0x72);
    g_assert_cmpuint(layout.i8042_reset_active, ==, 0);
    g_assert_cmphex(layout.i8042_data.base, ==, 0x60);
    g_assert_cmphex(layout.i8042_command.base, ==, 0x64);
    g_assert_cmpuint(layout.i8042_kbd_irq, ==, 1);
    g_assert_cmpuint(layout.i8042_mouse_irq, ==, 12);

    g_assert_cmphex(layout.rtc_bank0.base, ==, 0x70);
    g_assert_cmphex(layout.rtc_bank1.base, ==, 0x72);
    g_assert_cmpuint(layout.rtc_irq, ==, 8);
    g_assert_cmpint(layout.rtc_base_year, ==, 2000);

    g_assert_cmphex(layout.ide_command.base, ==, 0x1f0);
    g_assert_cmpuint(layout.ide_command.size, ==, 8);
    g_assert_cmphex(layout.ide_control.base, ==, 0x3f6);
    g_assert_cmpuint(layout.ide_control.size, ==, 1);
    g_assert_cmpuint(layout.ide_irq, ==, 14);
    g_assert_cmpuint(layout.ide_master_unit, ==, 0);

    g_assert_cmphex(layout.enabled_features, ==,
                    IA64_I2000_IO_TEST_ENABLED_FEATURES);
    g_assert_cmphex(layout.disabled_features, ==,
                    IA64_I2000_IO_TEST_DISABLED_FEATURES);
}

static void test_null_layout(void)
{
    Error *err = NULL;

    g_assert_false(ia64_i2000_io_test_layout_validate(NULL, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_parent_and_pci_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.parent_root = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.parent_io.size = 0;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.parent_cf8.base = layout.parent_cfc.base;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.pci_slot = 32;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.pci_function_count = 3;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.f0.function = layout.f1.function;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.f0.class_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.f1.device_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.f0.subsystem_vendor_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.f1.subsystem_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_slot = layout.pci_slot;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_parent_root = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_interrupt_pin = 2;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559.device_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_mmio_bar_size = 0x1800;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_eeprom_checksum++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i82559_option_rom_enabled = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_parent_root = 0;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_bus = 0;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_slot = 32;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160.device_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_interrupt_pin = 2;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_io_bar_base++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_mmio_bar_size = 0x180;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.isp12160_option_rom_enabled = 1;
    assert_invalid(&layout);
}

static void test_port_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.uart.base = layout.pic_master.base;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i8042_command.base = layout.i8042_data.base + 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.rtc_bank1.base = layout.rtc_bank0.base + 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.ide_control.base = 0x4000;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.pit.size = 0;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.ide_command.base = layout.parent_cf8.base;
    assert_invalid(&layout);
}

static void test_irq_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.uart_irq = layout.pit_irq;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.rtc_irq = 7;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i8042_mouse_irq = 16;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.pic_cascade_irq = 3;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.pid_legacy_pin = 16;
    assert_invalid(&layout);
}

static void test_superio_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.superio_sysopt = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.superio_enter_count = 2;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.superio_device_id++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.uart_ldn = layout.i8042_ldn;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.uart_activate_cr++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.i8042_mouse_irq_cr++;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.uart_reset_active = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.uart_input_clock_hz++;
    assert_invalid(&layout);
}

static void test_rtc_and_ide_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.rtc_base_year = 1980;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.ide_master_unit = 1;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.ide_control.size = 2;
    assert_invalid(&layout);
}

static void test_feature_mutations(void)
{
    IA64I2000IoTestLayout layout = io_test_layout();

    layout.enabled_features &= ~IA64_I2000_IO_TEST_FEATURE_UART;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.disabled_features &= ~IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.enabled_features |= IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA;
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.enabled_features |= BIT(31);
    assert_invalid(&layout);

    layout = io_test_layout();
    layout.disabled_features &= ~IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA;
    layout.enabled_features |= IA64_I2000_IO_TEST_FEATURE_IDE_BMDMA;
    assert_invalid(&layout);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ia64/i2000-io-layout/fixed", test_fixed_layout);
    g_test_add_func("/ia64/i2000-io-layout/null", test_null_layout);
    g_test_add_func("/ia64/i2000-io-layout/parent-pci",
                    test_parent_and_pci_mutations);
    g_test_add_func("/ia64/i2000-io-layout/ports", test_port_mutations);
    g_test_add_func("/ia64/i2000-io-layout/irqs", test_irq_mutations);
    g_test_add_func("/ia64/i2000-io-layout/superio", test_superio_mutations);
    g_test_add_func("/ia64/i2000-io-layout/rtc-ide",
                    test_rtc_and_ide_mutations);
    g_test_add_func("/ia64/i2000-io-layout/features", test_feature_mutations);

    return g_test_run();
}
