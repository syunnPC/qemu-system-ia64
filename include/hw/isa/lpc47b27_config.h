/*
 * Microchip LPC47B27x configuration-state core
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ISA_LPC47B27_CONFIG_H
#define HW_ISA_LPC47B27_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define LPC47B27_CONFIG_INDEX_PORT              UINT16_C(0x002e)
#define LPC47B27_CONFIG_DATA_PORT               UINT16_C(0x002f)
#define LPC47B27_CONFIG_ENTER_KEY                0x55U
#define LPC47B27_CONFIG_EXIT_KEY                 0xaaU

#define LPC47B27_CONFIG_CR_LDN                   0x07U
#define LPC47B27_CONFIG_CR_DEVICE_ID             0x20U
#define LPC47B27_CONFIG_DEVICE_ID                0x51U

#define LPC47B27_CONFIG_LDN_PARALLEL             0x03U
#define LPC47B27_CONFIG_LDN_SERIAL1              0x04U
#define LPC47B27_CONFIG_LDN_KEYBOARD             0x07U

#define LPC47B27_CONFIG_CR_ACTIVATE              0x30U
#define LPC47B27_CONFIG_CR_BASE_MSB              0x60U
#define LPC47B27_CONFIG_CR_BASE_LSB              0x61U
#define LPC47B27_CONFIG_CR_PRIMARY_IRQ           0x70U
#define LPC47B27_CONFIG_CR_SECONDARY_IRQ         0x72U
#define LPC47B27_CONFIG_CR_DMA                   0x74U
#define LPC47B27_CONFIG_CR_MODE                  0xf0U
#define LPC47B27_CONFIG_CR_MODE2                 0xf1U

#define LPC47B27_CONFIG_UART_BASE_MIN            UINT16_C(0x0100)
#define LPC47B27_CONFIG_UART_BASE_MAX            UINT16_C(0x0ff8)
#define LPC47B27_CONFIG_UART_BASE_ALIGN          UINT16_C(8)

/* UART, keyboard, mouse, and parallel outputs in the ISA wrapper. */
#define LPC47B27_IRQ_ROUTER_SOURCE_COUNT          4U

typedef uint32_t LPC47B27ConfigChange;

#define LPC47B27_CONFIG_CHANGE_NONE              UINT32_C(0)
#define LPC47B27_CONFIG_CHANGE_CONFIG_MODE       (UINT32_C(1) << 0)
#define LPC47B27_CONFIG_CHANGE_INDEX             (UINT32_C(1) << 1)
#define LPC47B27_CONFIG_CHANGE_LDN               (UINT32_C(1) << 2)
#define LPC47B27_CONFIG_CHANGE_UART_ACTIVATE     (UINT32_C(1) << 3)
#define LPC47B27_CONFIG_CHANGE_UART_BASE         (UINT32_C(1) << 4)
#define LPC47B27_CONFIG_CHANGE_UART_IRQ          (UINT32_C(1) << 5)
#define LPC47B27_CONFIG_CHANGE_UART_MODE         (UINT32_C(1) << 6)
#define LPC47B27_CONFIG_CHANGE_I8042_ACTIVATE    (UINT32_C(1) << 7)
#define LPC47B27_CONFIG_CHANGE_I8042_KBD_IRQ     (UINT32_C(1) << 8)
#define LPC47B27_CONFIG_CHANGE_I8042_MOUSE_IRQ   (UINT32_C(1) << 9)
#define LPC47B27_CONFIG_CHANGE_I8042_MODE        (UINT32_C(1) << 10)
#define LPC47B27_CONFIG_CHANGE_PARALLEL          (UINT32_C(1) << 11)

#define LPC47B27_CONFIG_CHANGE_UART_RESOURCES    \
    (LPC47B27_CONFIG_CHANGE_UART_ACTIVATE |      \
     LPC47B27_CONFIG_CHANGE_UART_BASE |          \
     LPC47B27_CONFIG_CHANGE_UART_IRQ |           \
     LPC47B27_CONFIG_CHANGE_UART_MODE)

#define LPC47B27_CONFIG_CHANGE_I8042_RESOURCES   \
    (LPC47B27_CONFIG_CHANGE_I8042_ACTIVATE |     \
     LPC47B27_CONFIG_CHANGE_I8042_KBD_IRQ |      \
     LPC47B27_CONFIG_CHANGE_I8042_MOUSE_IRQ |    \
     LPC47B27_CONFIG_CHANGE_I8042_MODE)

typedef struct LPC47B27ConfigState {
    bool config_mode;
    uint8_t index;
    uint8_t ldn;

    uint8_t serial1_activate;
    uint8_t serial1_base_msb;
    uint8_t serial1_base_lsb;
    uint8_t serial1_irq;
    uint8_t serial1_mode;

    uint8_t keyboard_activate;
    uint8_t keyboard_irq;
    uint8_t mouse_irq;
    uint8_t keyboard_mode;

    uint8_t parallel_activate;
    uint8_t parallel_base_msb;
    uint8_t parallel_base_lsb;
    uint8_t parallel_irq;
    uint8_t parallel_dma;
    uint8_t parallel_mode;
    uint8_t parallel_mode2;
} LPC47B27ConfigState;

typedef struct LPC47B27UartConfig {
    bool active;
    bool base_valid;
    uint16_t base;
    uint8_t irq;
    uint8_t mode;
} LPC47B27UartConfig;

typedef struct LPC47B27I8042Config {
    bool active;
    uint8_t keyboard_irq;
    uint8_t mouse_irq;
    uint8_t mode;
} LPC47B27I8042Config;

/* Reset to the hard-reset register values and Run Mode. */
void lpc47b27_config_reset(LPC47B27ConfigState *state);

/*
 * Access the SYSOPT=0 pair.  Writes return an effective-change mask.  Outside
 * the pair or in Run Mode, reads return 0xff and writes are ignored.
 */
uint8_t lpc47b27_config_read(const LPC47B27ConfigState *state, uint16_t port);
LPC47B27ConfigChange lpc47b27_config_write(LPC47B27ConfigState *state,
                                           uint16_t port, uint8_t value);

uint16_t lpc47b27_config_uart_base(const LPC47B27ConfigState *state);
bool lpc47b27_config_uart_base_valid(const LPC47B27ConfigState *state);
void lpc47b27_config_get_uart(const LPC47B27ConfigState *state,
                              LPC47B27UartConfig *config);
void lpc47b27_config_get_i8042(const LPC47B27ConfigState *state,
                               LPC47B27I8042Config *config);

/* Calculate wired-OR level; route 0 is disabled. */
bool lpc47b27_irq_router_level(const uint8_t *routed_irq,
                               const bool *source_level, uint8_t irq);

#endif /* HW_ISA_LPC47B27_CONFIG_H */
