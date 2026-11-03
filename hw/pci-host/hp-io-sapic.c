/*
 * HP I/O SAPIC register and delivery policy
 *
 * System interrupt routing is outside this helper.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-io-sapic.h"

#define ASTRO_VERSION              ((32U << 16) | 1U)
#define ASTRO_INPUT_COUNT          8
#define ASTRO_RESET_VECTOR_BASE    2
#define ASTRO_VECTOR_MASK          63U

#define ZX1_VERSION                0x000a0020U
#define ZX1_INPUT_COUNT            10
#define ZX1_SOFTWARE_ENTRY         10
#define ZX1_ENTRY_COUNT            11
#define ZX1_RTE_LOW_WRITABLE       (HP_IO_SAPIC_RTE_VECTOR | \
                                    HP_IO_SAPIC_RTE_DELIVERY | \
                                    HP_IO_SAPIC_RTE_POLARITY | \
                                    HP_IO_SAPIC_RTE_TRIGGER | \
                                    HP_IO_SAPIC_RTE_MASK)
#define ZX1_RTE_HIGH_WRITABLE      0xffff0000U
#define ZX1_MESSAGE_ADDRESS_BASE   0xfee00000ULL
#define ZX1_MESSAGE_REDIRECT_HINT  0x8U
#define ZX1_MESSAGE_TRIGGER        0x8000U
#define ZX1_MESSAGE_ASSERT         0x4000U

const HPIOSAPICPolicy hp_io_sapic_astro_policy = {
    .variant = HP_IO_SAPIC_VARIANT_ASTRO,
    .input_count = ASTRO_INPUT_COUNT,
    .entry_count = ASTRO_INPUT_COUNT,
    .register_count = HP_IO_SAPIC_ASTRO_REG_COUNT,
    .version = ASTRO_VERSION,
};

const HPIOSAPICPolicy hp_io_sapic_zx1_policy = {
    .variant = HP_IO_SAPIC_VARIANT_ZX1,
    .input_count = ZX1_INPUT_COUNT,
    .entry_count = ZX1_ENTRY_COUNT,
    .register_count = HP_IO_SAPIC_ZX1_REG_COUNT,
    .version = ZX1_VERSION,
};

static bool hp_io_sapic_storage_valid(const HPIOSAPICPolicy *policy,
                                      const uint64_t *regs, size_t reg_count)
{
    return policy && regs && reg_count >= policy->register_count;
}

static unsigned int hp_io_sapic_rte_low(unsigned int entry)
{
    return HP_IO_SAPIC_RTE_BASE + 2 * entry;
}

uint32_t hp_io_sapic_select_read(const HPIOSAPICPolicy *policy,
                                 uint32_t selector)
{
    if (policy && policy->variant == HP_IO_SAPIC_VARIANT_ZX1) {
        return selector & UINT8_MAX;
    }

    return selector;
}

void hp_io_sapic_select_write(const HPIOSAPICPolicy *policy, uint32_t *selector,
                              uint64_t value)
{
    if (!policy || !selector) {
        return;
    }

    if (policy->variant == HP_IO_SAPIC_VARIANT_ZX1) {
        *selector = value & UINT8_MAX;
    } else {
        *selector = value;
    }
}

bool hp_io_sapic_window_read(const HPIOSAPICPolicy *policy, uint32_t selector,
                             const uint64_t *regs, size_t reg_count,
                             uint64_t *value)
{
    uint32_t rte_mask;

    if (!value || !hp_io_sapic_storage_valid(policy, regs, reg_count)) {
        return false;
    }

    if (selector == 0x01) {
        *value = policy->version;
        return true;
    }

    if (policy->variant == HP_IO_SAPIC_VARIANT_ASTRO) {
        if (selector >= policy->register_count) {
            return false;
        }
        *value = regs[selector];
        return true;
    }

    if (selector >= policy->register_count) {
        return false;
    }
    if (selector < HP_IO_SAPIC_RTE_BASE) {
        *value = 0;
        return true;
    }

    rte_mask = (selector & 1) ? ZX1_RTE_HIGH_WRITABLE
                              : ZX1_RTE_LOW_WRITABLE | HP_IO_SAPIC_RTE_STATUS;
    *value = (uint32_t)regs[selector] & rte_mask;
    return true;
}

bool hp_io_sapic_window_write(const HPIOSAPICPolicy *policy, uint32_t selector,
                              uint64_t *regs, size_t reg_count, uint64_t value)
{
    uint32_t old_status;

    if (!hp_io_sapic_storage_valid(policy, regs, reg_count)) {
        return false;
    }

    if (policy->variant == HP_IO_SAPIC_VARIANT_ASTRO) {
        if (selector >= policy->register_count) {
            return false;
        }
        regs[selector] = value;
        return true;
    }

    if (selector >= policy->register_count) {
        return false;
    }
    if (selector < HP_IO_SAPIC_RTE_BASE) {
        return true;
    }

    if (selector & 1) {
        regs[selector] = (uint32_t)value & ZX1_RTE_HIGH_WRITABLE;
    } else {
        old_status = regs[selector] & HP_IO_SAPIC_RTE_STATUS;
        regs[selector] = old_status | ((uint32_t)value & ZX1_RTE_LOW_WRITABLE);
    }
    return true;
}

bool hp_io_sapic_reset(const HPIOSAPICPolicy *policy, uint32_t *selector,
                       uint64_t *regs, size_t reg_count, uint32_t *ilr,
                       uint64_t astro_destination)
{
    unsigned int entry;

    if (!selector || !ilr ||
        !hp_io_sapic_storage_valid(policy, regs, reg_count)) {
        return false;
    }

    *ilr = 0;
    if (policy->variant == HP_IO_SAPIC_VARIANT_ASTRO) {
        for (entry = 0; entry < policy->entry_count; entry++) {
            regs[hp_io_sapic_rte_low(entry)] =
                HP_IO_SAPIC_RTE_POLARITY | HP_IO_SAPIC_RTE_TRIGGER |
                HP_IO_SAPIC_RTE_MASK | (ASTRO_RESET_VECTOR_BASE + entry);
            regs[hp_io_sapic_rte_low(entry) + 1] = astro_destination;
        }
        return true;
    }

    *selector = 0;
    memset(regs, 0, policy->register_count * sizeof(*regs));
    for (entry = 0; entry < policy->entry_count; entry++) {
        regs[hp_io_sapic_rte_low(entry)] = HP_IO_SAPIC_RTE_MASK;
    }
    return true;
}

bool hp_io_sapic_make_message(const HPIOSAPICPolicy *policy,
                              const uint64_t *regs, size_t reg_count,
                              unsigned int entry, HPIOSAPICMessage *message)
{
    uint32_t low;
    uint32_t high;
    uint32_t delivery;

    if (!message || !hp_io_sapic_storage_valid(policy, regs, reg_count) ||
        entry >= policy->entry_count) {
        return false;
    }

    low = regs[hp_io_sapic_rte_low(entry)];
    high = regs[hp_io_sapic_rte_low(entry) + 1];
    if (policy->variant == HP_IO_SAPIC_VARIANT_ASTRO) {
        message->address = (((uint64_t)high << 4) & 0x0ff00000) |
                           (((uint64_t)high >> 12) & 0x000ff000) | 0xf0000000;
        message->data = low & ASTRO_VECTOR_MASK;
        return true;
    }

    delivery = (low & HP_IO_SAPIC_RTE_DELIVERY) >> 8;
    message->address = ZX1_MESSAGE_ADDRESS_BASE |
                       ((uint64_t)(high >> 24) << 12) |
                       ((uint64_t)((high >> 16) & UINT8_MAX) << 4);
    if (delivery == 1) {
        message->address |= ZX1_MESSAGE_REDIRECT_HINT;
    }
    message->data = ZX1_MESSAGE_ASSERT |
                    (low & (HP_IO_SAPIC_RTE_DELIVERY | HP_IO_SAPIC_RTE_VECTOR));
    if (low & HP_IO_SAPIC_RTE_TRIGGER) {
        message->data |= ZX1_MESSAGE_TRIGGER;
    }
    return true;
}

bool hp_io_sapic_set_input(const HPIOSAPICPolicy *policy, uint64_t *regs,
                           size_t reg_count, uint32_t *ilr, unsigned int input,
                           bool level, HPIOSAPICDeliver deliver, void *opaque)
{
    HPIOSAPICMessage message;
    uint32_t old_ilr;
    uint32_t bit;
    uint32_t low;

    if (!ilr || !hp_io_sapic_storage_valid(policy, regs, reg_count) ||
        policy->variant != HP_IO_SAPIC_VARIANT_ASTRO ||
        input >= policy->input_count) {
        return false;
    }

    low = regs[hp_io_sapic_rte_low(input)];
    bit = 1U << (low & (policy->input_count - 1));
    old_ilr = *ilr;

    if (level && !(low & HP_IO_SAPIC_RTE_MASK) &&
        hp_io_sapic_make_message(policy, regs, reg_count, input, &message) &&
        message.address) {
        *ilr = old_ilr | bit;
        if (!(old_ilr & bit)) {
            if (deliver) {
                deliver(opaque, &message);
            }
            return true;
        }
    } else {
        *ilr = old_ilr & ~bit;
    }
    return false;
}

unsigned int hp_io_sapic_eoi(const HPIOSAPICPolicy *policy, uint64_t *regs,
                             size_t reg_count, uint32_t *ilr, uint64_t value,
                             uint32_t asserted, HPIOSAPICDeliver deliver,
                             void *opaque)
{
    HPIOSAPICMessage message;
    unsigned int deliveries = 0;
    unsigned int input;
    uint32_t vector_mask;
    uint32_t vector;
    uint32_t low;
    uint32_t bit;

    if (!ilr || !hp_io_sapic_storage_valid(policy, regs, reg_count)) {
        return 0;
    }

    vector_mask = policy->variant == HP_IO_SAPIC_VARIANT_ASTRO
                      ? ASTRO_VECTOR_MASK
                      : HP_IO_SAPIC_RTE_VECTOR;
    vector = value & vector_mask;
    for (input = 0; input < policy->input_count; input++) {
        low = regs[hp_io_sapic_rte_low(input)];
        if ((low & vector_mask) != vector) {
            continue;
        }

        bit = 1U << input;
        *ilr &= ~bit;
        if (policy->variant == HP_IO_SAPIC_VARIANT_ASTRO ||
            !(low & HP_IO_SAPIC_RTE_TRIGGER) || !(asserted & bit) ||
            (low & HP_IO_SAPIC_RTE_MASK)) {
            continue;
        }

        if (hp_io_sapic_make_message(policy, regs, reg_count, input,
                                     &message)) {
            *ilr |= bit;
            if (deliver) {
                deliver(opaque, &message);
            }
            deliveries++;
        }
    }
    return deliveries;
}

bool hp_io_sapic_software_interrupt(const HPIOSAPICPolicy *policy,
                                    const uint64_t *regs, size_t reg_count,
                                    HPIOSAPICDeliver deliver, void *opaque)
{
    HPIOSAPICMessage message;
    uint32_t low;

    if (!hp_io_sapic_storage_valid(policy, regs, reg_count) ||
        policy->variant != HP_IO_SAPIC_VARIANT_ZX1) {
        return false;
    }

    low = regs[hp_io_sapic_rte_low(ZX1_SOFTWARE_ENTRY)];
    if (low & (HP_IO_SAPIC_RTE_MASK | HP_IO_SAPIC_RTE_POLARITY |
               HP_IO_SAPIC_RTE_TRIGGER)) {
        return false;
    }

    if (!hp_io_sapic_make_message(policy, regs, reg_count, ZX1_SOFTWARE_ENTRY,
                                  &message)) {
        return false;
    }
    if (deliver) {
        deliver(opaque, &message);
    }
    return true;
}
