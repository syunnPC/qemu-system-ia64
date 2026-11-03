/*
 * Microchip LPC47B27x configuration-state core
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/isa/lpc47b27_config.h"

#define LPC47B27_ACTIVATE_MASK                  0x01U
#define LPC47B27_BASE_MSB_MASK                  0x0fU
#define LPC47B27_BASE_LSB_MASK                  0xf8U
#define LPC47B27_IRQ_MASK                       0x0fU
#define LPC47B27_SERIAL1_MODE_MASK              0x83U
#define LPC47B27_KEYBOARD_MODE_MASK             0xfcU

static LPC47B27ConfigChange lpc47b27_update(uint8_t *reg, uint8_t value,
                                            uint8_t mask,
                                            LPC47B27ConfigChange change)
{
    value &= mask;
    if (*reg == value) {
        return LPC47B27_CONFIG_CHANGE_NONE;
    }

    *reg = value;
    return change;
}

void lpc47b27_config_reset(LPC47B27ConfigState *state)
{
    assert(state);

    *state = (LPC47B27ConfigState) { 0 };
    state->parallel_dma = 4;
    state->parallel_mode = 0x3c;
}

static uint8_t lpc47b27_config_data_read(const LPC47B27ConfigState *state)
{
    if (state->index == LPC47B27_CONFIG_CR_LDN) {
        return state->ldn;
    }
    if (state->index == LPC47B27_CONFIG_CR_DEVICE_ID) {
        return LPC47B27_CONFIG_DEVICE_ID;
    }
    if (state->index < LPC47B27_CONFIG_CR_ACTIVATE) {
        return 0;
    }

    switch (state->ldn) {
    case LPC47B27_CONFIG_LDN_PARALLEL:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return state->parallel_activate;
        case LPC47B27_CONFIG_CR_BASE_MSB:
            return state->parallel_base_msb;
        case LPC47B27_CONFIG_CR_BASE_LSB:
            return state->parallel_base_lsb;
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return state->parallel_irq;
        case LPC47B27_CONFIG_CR_DMA:
            return state->parallel_dma;
        case LPC47B27_CONFIG_CR_MODE:
            return state->parallel_mode;
        case LPC47B27_CONFIG_CR_MODE2:
            return state->parallel_mode2;
        default:
            return 0;
        }
    case LPC47B27_CONFIG_LDN_SERIAL1:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return state->serial1_activate;
        case LPC47B27_CONFIG_CR_BASE_MSB:
            return state->serial1_base_msb;
        case LPC47B27_CONFIG_CR_BASE_LSB:
            return state->serial1_base_lsb;
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return state->serial1_irq;
        case LPC47B27_CONFIG_CR_MODE:
            return state->serial1_mode;
        default:
            return 0;
        }
    case LPC47B27_CONFIG_LDN_KEYBOARD:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return state->keyboard_activate;
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return state->keyboard_irq;
        case LPC47B27_CONFIG_CR_SECONDARY_IRQ:
            return state->mouse_irq;
        case LPC47B27_CONFIG_CR_MODE:
            return state->keyboard_mode;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

uint8_t lpc47b27_config_read(const LPC47B27ConfigState *state, uint16_t port)
{
    assert(state);

    if (!state->config_mode) {
        return 0xff;
    }
    if (port == LPC47B27_CONFIG_INDEX_PORT) {
        return state->index;
    }
    if (port == LPC47B27_CONFIG_DATA_PORT) {
        return lpc47b27_config_data_read(state);
    }
    return 0xff;
}

static LPC47B27ConfigChange
lpc47b27_config_data_write(LPC47B27ConfigState *state, uint8_t value)
{
    if (state->index == LPC47B27_CONFIG_CR_LDN) {
        return lpc47b27_update(&state->ldn, value, 0xff,
                               LPC47B27_CONFIG_CHANGE_LDN);
    }
    if (state->index < LPC47B27_CONFIG_CR_ACTIVATE) {
        /* CR20 is read-only; every other unimplemented global is reserved. */
        return LPC47B27_CONFIG_CHANGE_NONE;
    }

    switch (state->ldn) {
    case LPC47B27_CONFIG_LDN_PARALLEL:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return lpc47b27_update(&state->parallel_activate, value, 1,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_BASE_MSB:
            return lpc47b27_update(&state->parallel_base_msb, value, 0x0f,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_BASE_LSB:
            return lpc47b27_update(&state->parallel_base_lsb, value, 0xfc,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return lpc47b27_update(&state->parallel_irq, value, 0x0f,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_DMA:
            return lpc47b27_update(&state->parallel_dma, value, 7,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_MODE:
            return lpc47b27_update(&state->parallel_mode, value, 0xff,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        case LPC47B27_CONFIG_CR_MODE2:
            return lpc47b27_update(&state->parallel_mode2, value, 3,
                                   LPC47B27_CONFIG_CHANGE_PARALLEL);
        default:
            return LPC47B27_CONFIG_CHANGE_NONE;
        }
    case LPC47B27_CONFIG_LDN_SERIAL1:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return lpc47b27_update(&state->serial1_activate, value,
                                   LPC47B27_ACTIVATE_MASK,
                                   LPC47B27_CONFIG_CHANGE_UART_ACTIVATE);
        case LPC47B27_CONFIG_CR_BASE_MSB:
            return lpc47b27_update(&state->serial1_base_msb, value,
                                   LPC47B27_BASE_MSB_MASK,
                                   LPC47B27_CONFIG_CHANGE_UART_BASE);
        case LPC47B27_CONFIG_CR_BASE_LSB:
            return lpc47b27_update(&state->serial1_base_lsb, value,
                                   LPC47B27_BASE_LSB_MASK,
                                   LPC47B27_CONFIG_CHANGE_UART_BASE);
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return lpc47b27_update(&state->serial1_irq, value,
                                   LPC47B27_IRQ_MASK,
                                   LPC47B27_CONFIG_CHANGE_UART_IRQ);
        case LPC47B27_CONFIG_CR_MODE:
            return lpc47b27_update(&state->serial1_mode, value,
                                   LPC47B27_SERIAL1_MODE_MASK,
                                   LPC47B27_CONFIG_CHANGE_UART_MODE);
        default:
            return LPC47B27_CONFIG_CHANGE_NONE;
        }
    case LPC47B27_CONFIG_LDN_KEYBOARD:
        switch (state->index) {
        case LPC47B27_CONFIG_CR_ACTIVATE:
            return lpc47b27_update(&state->keyboard_activate, value,
                                   LPC47B27_ACTIVATE_MASK,
                                   LPC47B27_CONFIG_CHANGE_I8042_ACTIVATE);
        case LPC47B27_CONFIG_CR_PRIMARY_IRQ:
            return lpc47b27_update(&state->keyboard_irq, value,
                                   LPC47B27_IRQ_MASK,
                                   LPC47B27_CONFIG_CHANGE_I8042_KBD_IRQ);
        case LPC47B27_CONFIG_CR_SECONDARY_IRQ:
            return lpc47b27_update(&state->mouse_irq, value, LPC47B27_IRQ_MASK,
                                   LPC47B27_CONFIG_CHANGE_I8042_MOUSE_IRQ);
        case LPC47B27_CONFIG_CR_MODE:
            return lpc47b27_update(&state->keyboard_mode, value,
                                   LPC47B27_KEYBOARD_MODE_MASK,
                                   LPC47B27_CONFIG_CHANGE_I8042_MODE);
        default:
            return LPC47B27_CONFIG_CHANGE_NONE;
        }
    default:
        return LPC47B27_CONFIG_CHANGE_NONE;
    }
}

LPC47B27ConfigChange lpc47b27_config_write(LPC47B27ConfigState *state,
                                           uint16_t port, uint8_t value)
{
    assert(state);

    if (port == LPC47B27_CONFIG_INDEX_PORT) {
        if (!state->config_mode) {
            if (value == LPC47B27_CONFIG_ENTER_KEY) {
                state->config_mode = true;
                return LPC47B27_CONFIG_CHANGE_CONFIG_MODE;
            }
            return LPC47B27_CONFIG_CHANGE_NONE;
        }
        if (value == LPC47B27_CONFIG_EXIT_KEY) {
            state->config_mode = false;
            return LPC47B27_CONFIG_CHANGE_CONFIG_MODE;
        }
        return lpc47b27_update(&state->index, value, 0xff,
                               LPC47B27_CONFIG_CHANGE_INDEX);
    }

    if (port == LPC47B27_CONFIG_DATA_PORT && state->config_mode) {
        return lpc47b27_config_data_write(state, value);
    }
    return LPC47B27_CONFIG_CHANGE_NONE;
}

uint16_t lpc47b27_config_uart_base(const LPC47B27ConfigState *state)
{
    assert(state);

    return ((uint16_t)state->serial1_base_msb << 8) | state->serial1_base_lsb;
}

bool lpc47b27_config_uart_base_valid(const LPC47B27ConfigState *state)
{
    uint16_t base = lpc47b27_config_uart_base(state);

    return base >= LPC47B27_CONFIG_UART_BASE_MIN &&
           base <= LPC47B27_CONFIG_UART_BASE_MAX &&
           (base & (LPC47B27_CONFIG_UART_BASE_ALIGN - 1)) == 0;
}

void lpc47b27_config_get_uart(const LPC47B27ConfigState *state,
                              LPC47B27UartConfig *config)
{
    assert(state);
    assert(config);

    config->active = state->serial1_activate != 0;
    config->base_valid = lpc47b27_config_uart_base_valid(state);
    config->base = lpc47b27_config_uart_base(state);
    config->irq = state->serial1_irq;
    config->mode = state->serial1_mode;
}

void lpc47b27_config_get_i8042(const LPC47B27ConfigState *state,
                               LPC47B27I8042Config *config)
{
    assert(state);
    assert(config);

    config->active = state->keyboard_activate != 0;
    config->keyboard_irq = state->keyboard_irq;
    config->mouse_irq = state->mouse_irq;
    config->mode = state->keyboard_mode;
}

bool lpc47b27_irq_router_level(const uint8_t *routed_irq,
                               const bool *source_level, uint8_t irq)
{
    unsigned i;

    assert(routed_irq);
    assert(source_level);
    if (!irq) {
        return false;
    }

    for (i = 0; i < LPC47B27_IRQ_ROUTER_SOURCE_COUNT; i++) {
        if (routed_irq[i] == irq && source_level[i]) {
            return true;
        }
    }
    return false;
}
