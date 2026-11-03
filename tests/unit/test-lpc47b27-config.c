/*
 * Microchip LPC47B27x configuration-state core tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_i2000_io_test_layout.h"
#include "hw/isa/lpc47b27_config.h"

G_STATIC_ASSERT(LPC47B27_CONFIG_INDEX_PORT ==
                IA64_I2000_IO_TEST_SIO_CONFIG_BASE);
G_STATIC_ASSERT(LPC47B27_CONFIG_DATA_PORT ==
                IA64_I2000_IO_TEST_SIO_CONFIG_BASE + 1);
G_STATIC_ASSERT(LPC47B27_CONFIG_ENTER_KEY ==
                IA64_I2000_IO_TEST_SIO_ENTER_KEY);
G_STATIC_ASSERT(LPC47B27_CONFIG_EXIT_KEY ==
                IA64_I2000_IO_TEST_SIO_EXIT_KEY);
G_STATIC_ASSERT(LPC47B27_CONFIG_CR_LDN ==
                IA64_I2000_IO_TEST_SIO_LDN_SELECT_CR);
G_STATIC_ASSERT(LPC47B27_CONFIG_CR_DEVICE_ID ==
                IA64_I2000_IO_TEST_SIO_DEVICE_ID_CR);
G_STATIC_ASSERT(LPC47B27_CONFIG_DEVICE_ID ==
                IA64_I2000_IO_TEST_SIO_DEVICE_ID);
G_STATIC_ASSERT(LPC47B27_CONFIG_LDN_SERIAL1 ==
                IA64_I2000_IO_TEST_UART_LDN);
G_STATIC_ASSERT(LPC47B27_CONFIG_LDN_KEYBOARD ==
                IA64_I2000_IO_TEST_I8042_LDN);

static void enter_config(LPC47B27ConfigState *state)
{
    g_assert_cmphex(lpc47b27_config_write(state, LPC47B27_CONFIG_INDEX_PORT,
                                          LPC47B27_CONFIG_ENTER_KEY),
                    ==, LPC47B27_CONFIG_CHANGE_CONFIG_MODE);
}

static void select_index(LPC47B27ConfigState *state, uint8_t index)
{
    LPC47B27ConfigChange expected = state->index == index
                                        ? LPC47B27_CONFIG_CHANGE_NONE
                                        : LPC47B27_CONFIG_CHANGE_INDEX;

    g_assert_cmphex(
        lpc47b27_config_write(state, LPC47B27_CONFIG_INDEX_PORT, index), ==,
        expected);
}

static void select_ldn(LPC47B27ConfigState *state, uint8_t ldn)
{
    LPC47B27ConfigChange expected = state->ldn == ldn
                                        ? LPC47B27_CONFIG_CHANGE_NONE
                                        : LPC47B27_CONFIG_CHANGE_LDN;

    select_index(state, LPC47B27_CONFIG_CR_LDN);
    g_assert_cmphex(
        lpc47b27_config_write(state, LPC47B27_CONFIG_DATA_PORT, ldn), ==,
        expected);
    g_assert_cmphex(state->ldn, ==, ldn);
}

static uint8_t read_data(LPC47B27ConfigState *state, uint8_t index)
{
    select_index(state, index);
    return lpc47b27_config_read(state, LPC47B27_CONFIG_DATA_PORT);
}

static LPC47B27ConfigChange write_data(LPC47B27ConfigState *state,
                                       uint8_t index, uint8_t value)
{
    select_index(state, index);
    return lpc47b27_config_write(state, LPC47B27_CONFIG_DATA_PORT, value);
}

static void test_reset_and_run_mode(void)
{
    LPC47B27ConfigState state;
    LPC47B27UartConfig uart;
    LPC47B27I8042Config i8042;

    memset(&state, 0xff, sizeof(state));
    lpc47b27_config_reset(&state);

    g_assert_false(state.config_mode);
    g_assert_cmphex(state.index, ==, 0);
    g_assert_cmphex(state.ldn, ==, 0);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_INDEX_PORT),
                    ==, 0xff);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_DATA_PORT), ==,
                    0xff);
    g_assert_cmphex(lpc47b27_config_read(&state, 0x30), ==, 0xff);

    g_assert_cmphex(
        lpc47b27_config_write(&state, LPC47B27_CONFIG_INDEX_PORT, 0x54), ==,
        LPC47B27_CONFIG_CHANGE_NONE);
    g_assert_cmphex(
        lpc47b27_config_write(&state, LPC47B27_CONFIG_DATA_PORT, 0x55), ==,
        LPC47B27_CONFIG_CHANGE_NONE);
    g_assert_cmphex(lpc47b27_config_write(&state, 0x30, 0x55), ==,
                    LPC47B27_CONFIG_CHANGE_NONE);

    lpc47b27_config_get_uart(&state, &uart);
    g_assert_false(uart.active);
    g_assert_false(uart.base_valid);
    g_assert_cmphex(uart.base, ==, 0);
    g_assert_cmphex(uart.irq, ==, 0);
    g_assert_cmphex(uart.mode, ==, 0);

    lpc47b27_config_get_i8042(&state, &i8042);
    g_assert_false(i8042.active);
    g_assert_cmphex(i8042.keyboard_irq, ==, 0);
    g_assert_cmphex(i8042.mouse_irq, ==, 0);
    g_assert_cmphex(i8042.mode, ==, 0);
}

static void test_enter_exit_and_index(void)
{
    LPC47B27ConfigState state;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    g_assert_true(state.config_mode);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_INDEX_PORT),
                    ==, 0);

    select_index(&state, 0x55);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_INDEX_PORT),
                    ==, 0x55);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_DATA_PORT), ==,
                    0);

    g_assert_cmphex(lpc47b27_config_write(&state, LPC47B27_CONFIG_INDEX_PORT,
                                          LPC47B27_CONFIG_EXIT_KEY),
                    ==, LPC47B27_CONFIG_CHANGE_CONFIG_MODE);
    g_assert_false(state.config_mode);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_INDEX_PORT),
                    ==, 0xff);

    enter_config(&state);
    g_assert_cmphex(lpc47b27_config_read(&state, LPC47B27_CONFIG_INDEX_PORT),
                    ==, 0x55);
}

static void test_global_registers(void)
{
    LPC47B27ConfigState state;

    lpc47b27_config_reset(&state);
    enter_config(&state);

    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_DEVICE_ID), ==,
                    LPC47B27_CONFIG_DEVICE_ID);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_DEVICE_ID, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_NONE);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_DEVICE_ID), ==,
                    LPC47B27_CONFIG_DEVICE_ID);

    select_ldn(&state, LPC47B27_CONFIG_LDN_SERIAL1);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_LDN), ==,
                    LPC47B27_CONFIG_LDN_SERIAL1);
    g_assert_cmphex(read_data(&state, 0x03), ==, 0);
    g_assert_cmphex(write_data(&state, 0x22, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_NONE);
    g_assert_cmphex(read_data(&state, 0x22), ==, 0);
}

static void test_serial1_registers(void)
{
    LPC47B27ConfigState state;
    LPC47B27UartConfig uart;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    select_ldn(&state, LPC47B27_CONFIG_LDN_SERIAL1);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_ACTIVATE, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_UART_ACTIVATE);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_ACTIVATE), ==, 0x01);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_BASE_MSB, 0x13), ==,
                    LPC47B27_CONFIG_CHANGE_UART_BASE);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_BASE_LSB, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_UART_BASE);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_BASE_MSB), ==, 0x03);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_BASE_LSB), ==, 0xf8);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 0xf4),
                    ==, LPC47B27_CONFIG_CHANGE_UART_IRQ);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_PRIMARY_IRQ), ==,
                    0x04);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_MODE, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_UART_MODE);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_MODE), ==, 0x83);

    lpc47b27_config_get_uart(&state, &uart);
    g_assert_true(uart.active);
    g_assert_true(uart.base_valid);
    g_assert_cmphex(uart.base, ==, 0x03f8);
    g_assert_cmphex(uart.irq, ==, 4);
    g_assert_cmphex(uart.mode, ==, 0x83);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_MODE, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_NONE);
}

static void test_uart_base_validity(void)
{
    LPC47B27ConfigState state;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    select_ldn(&state, LPC47B27_CONFIG_LDN_SERIAL1);

    g_assert_false(lpc47b27_config_uart_base_valid(&state));
    write_data(&state, LPC47B27_CONFIG_CR_BASE_MSB, 0x01);
    g_assert_true(lpc47b27_config_uart_base_valid(&state));
    g_assert_cmphex(lpc47b27_config_uart_base(&state), ==, 0x0100);

    write_data(&state, LPC47B27_CONFIG_CR_BASE_MSB, 0x0f);
    write_data(&state, LPC47B27_CONFIG_CR_BASE_LSB, 0xff);
    g_assert_true(lpc47b27_config_uart_base_valid(&state));
    g_assert_cmphex(lpc47b27_config_uart_base(&state), ==, 0x0ff8);

    state.serial1_base_lsb = 0xf9;
    g_assert_false(lpc47b27_config_uart_base_valid(&state));
    state.serial1_base_lsb = 0xf8;
    state.serial1_base_msb = 0x10;
    g_assert_false(lpc47b27_config_uart_base_valid(&state));
}

static void test_keyboard_registers(void)
{
    LPC47B27ConfigState state;
    LPC47B27I8042Config i8042;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    select_ldn(&state, LPC47B27_CONFIG_LDN_KEYBOARD);

    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_ACTIVATE, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_I8042_ACTIVATE);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 0xf1),
                    ==, LPC47B27_CONFIG_CHANGE_I8042_KBD_IRQ);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_SECONDARY_IRQ, 0xfc),
                    ==, LPC47B27_CONFIG_CHANGE_I8042_MOUSE_IRQ);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_MODE, 0xff), ==,
                    LPC47B27_CONFIG_CHANGE_I8042_MODE);

    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_ACTIVATE), ==, 0x01);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_PRIMARY_IRQ), ==,
                    0x01);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_SECONDARY_IRQ), ==,
                    0x0c);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_MODE), ==, 0xfc);

    lpc47b27_config_get_i8042(&state, &i8042);
    g_assert_true(i8042.active);
    g_assert_cmphex(i8042.keyboard_irq, ==, 1);
    g_assert_cmphex(i8042.mouse_irq, ==, 12);
    g_assert_cmphex(i8042.mode, ==, 0xfc);
}

static void test_unsupported_selection(void)
{
    LPC47B27ConfigState state;
    LPC47B27UartConfig before;
    LPC47B27UartConfig after;
    const uint8_t registers[] = {
        LPC47B27_CONFIG_CR_ACTIVATE,
        LPC47B27_CONFIG_CR_BASE_MSB,
        LPC47B27_CONFIG_CR_BASE_LSB,
        LPC47B27_CONFIG_CR_PRIMARY_IRQ,
        LPC47B27_CONFIG_CR_SECONDARY_IRQ,
        LPC47B27_CONFIG_CR_MODE,
        0xff,
    };
    unsigned i;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    lpc47b27_config_get_uart(&state, &before);
    select_ldn(&state, 0x05);

    for (i = 0; i < G_N_ELEMENTS(registers); i++) {
        g_assert_cmphex(write_data(&state, registers[i], 0xff), ==,
                        LPC47B27_CONFIG_CHANGE_NONE);
        g_assert_cmphex(read_data(&state, registers[i]), ==, 0);
    }

    lpc47b27_config_get_uart(&state, &after);
    g_assert_cmpint(before.active, ==, after.active);
    g_assert_cmpint(before.base_valid, ==, after.base_valid);
    g_assert_cmphex(before.base, ==, after.base);
    g_assert_cmphex(before.irq, ==, after.irq);
    g_assert_cmphex(before.mode, ==, after.mode);

    select_ldn(&state, LPC47B27_CONFIG_LDN_SERIAL1);
    g_assert_cmphex(write_data(&state, LPC47B27_CONFIG_CR_SECONDARY_IRQ, 0xff),
                    ==, LPC47B27_CONFIG_CHANGE_NONE);
    g_assert_cmphex(read_data(&state, LPC47B27_CONFIG_CR_SECONDARY_IRQ), ==, 0);
}

static void test_reset_clears_resources(void)
{
    LPC47B27ConfigState state;

    lpc47b27_config_reset(&state);
    enter_config(&state);
    select_ldn(&state, LPC47B27_CONFIG_LDN_SERIAL1);
    write_data(&state, LPC47B27_CONFIG_CR_ACTIVATE, 1);
    write_data(&state, LPC47B27_CONFIG_CR_BASE_MSB, 3);
    write_data(&state, LPC47B27_CONFIG_CR_BASE_LSB, 0xf8);
    write_data(&state, LPC47B27_CONFIG_CR_PRIMARY_IRQ, 4);
    write_data(&state, LPC47B27_CONFIG_CR_MODE, 0x83);

    lpc47b27_config_reset(&state);
    g_assert_false(state.config_mode);
    g_assert_false(state.serial1_activate);
    g_assert_cmphex(lpc47b27_config_uart_base(&state), ==, 0);
    g_assert_cmphex(state.serial1_irq, ==, 0);
    g_assert_cmphex(state.serial1_mode, ==, 0);
}

static void test_irq_router_wired_or(void)
{
    uint8_t routed_irq[LPC47B27_IRQ_ROUTER_SOURCE_COUNT] = { 4, 4, 12 };
    bool source_level[LPC47B27_IRQ_ROUTER_SOURCE_COUNT] = { true, true, false };

    g_assert_false(lpc47b27_irq_router_level(routed_irq, source_level, 0));
    g_assert_true(lpc47b27_irq_router_level(routed_irq, source_level, 4));
    g_assert_false(lpc47b27_irq_router_level(routed_irq, source_level, 12));

    source_level[0] = false;
    g_assert_true(lpc47b27_irq_router_level(routed_irq, source_level, 4));

    routed_irq[1] = 3;
    g_assert_false(lpc47b27_irq_router_level(routed_irq, source_level, 4));
    g_assert_true(lpc47b27_irq_router_level(routed_irq, source_level, 3));

    routed_irq[1] = 0;
    g_assert_false(lpc47b27_irq_router_level(routed_irq, source_level, 0));

    source_level[2] = true;
    g_assert_true(lpc47b27_irq_router_level(routed_irq, source_level, 12));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/lpc47b27/config/reset-run-mode", test_reset_and_run_mode);
    g_test_add_func("/lpc47b27/config/enter-exit-index",
                    test_enter_exit_and_index);
    g_test_add_func("/lpc47b27/config/global-registers", test_global_registers);
    g_test_add_func("/lpc47b27/config/serial1-registers",
                    test_serial1_registers);
    g_test_add_func("/lpc47b27/config/uart-base-validity",
                    test_uart_base_validity);
    g_test_add_func("/lpc47b27/config/keyboard-registers",
                    test_keyboard_registers);
    g_test_add_func("/lpc47b27/config/unsupported-selection",
                    test_unsupported_selection);
    g_test_add_func("/lpc47b27/config/reset-clears-resources",
                    test_reset_clears_resources);
    g_test_add_func("/lpc47b27/irq-router/wired-or", test_irq_router_wired_or);

    return g_test_run();
}
