/*
 * HP I/O SAPIC register and delivery policy
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_IO_SAPIC_H
#define HW_PCI_HOST_HP_IO_SAPIC_H

#define HP_IO_SAPIC_ASTRO_REG_COUNT 0x20
#define HP_IO_SAPIC_ZX1_REG_COUNT   0x26

#define HP_IO_SAPIC_RTE_BASE        0x10
#define HP_IO_SAPIC_RTE_VECTOR      0x000000ffU
#define HP_IO_SAPIC_RTE_DELIVERY    0x00000700U
#define HP_IO_SAPIC_RTE_STATUS      0x00001000U
#define HP_IO_SAPIC_RTE_POLARITY    0x00002000U
#define HP_IO_SAPIC_RTE_TRIGGER     0x00008000U
#define HP_IO_SAPIC_RTE_MASK        0x00010000U

typedef enum HPIOSAPICVariant {
    /* Astro/Elroy register behavior. */
    HP_IO_SAPIC_VARIANT_ASTRO,
    /* zx1 IOA behavior; board routing remains external. */
    HP_IO_SAPIC_VARIANT_ZX1,
} HPIOSAPICVariant;

typedef struct HPIOSAPICPolicy {
    HPIOSAPICVariant variant;
    uint8_t input_count;
    uint8_t entry_count;
    uint8_t register_count;
    uint32_t version;
} HPIOSAPICPolicy;

typedef struct HPIOSAPICMessage {
    uint64_t address;
    uint32_t data;
} HPIOSAPICMessage;

typedef void (*HPIOSAPICDeliver)(void *opaque, const HPIOSAPICMessage *message);

extern const HPIOSAPICPolicy hp_io_sapic_astro_policy;
extern const HPIOSAPICPolicy hp_io_sapic_zx1_policy;

uint32_t hp_io_sapic_select_read(const HPIOSAPICPolicy *policy,
                                 uint32_t selector);
void hp_io_sapic_select_write(const HPIOSAPICPolicy *policy, uint32_t *selector,
                              uint64_t value);

bool hp_io_sapic_window_read(const HPIOSAPICPolicy *policy, uint32_t selector,
                             const uint64_t *regs, size_t reg_count,
                             uint64_t *value);
bool hp_io_sapic_window_write(const HPIOSAPICPolicy *policy, uint32_t selector,
                              uint64_t *regs, size_t reg_count, uint64_t value);

bool hp_io_sapic_reset(const HPIOSAPICPolicy *policy, uint32_t *selector,
                       uint64_t *regs, size_t reg_count, uint32_t *ilr,
                       uint64_t astro_destination);

bool hp_io_sapic_make_message(const HPIOSAPICPolicy *policy,
                              const uint64_t *regs, size_t reg_count,
                              unsigned int entry, HPIOSAPICMessage *message);

/*
 * External-input transitions are supported by the Astro policy.  ZX1 board
 * wiring and polarity resolution are handled by its board model.
 */
bool hp_io_sapic_set_input(const HPIOSAPICPolicy *policy, uint64_t *regs,
                           size_t reg_count, uint32_t *ilr, unsigned int input,
                           bool level, HPIOSAPICDeliver deliver, void *opaque);

/*
 * asserted is a bitmap of polarity-resolved active zx1 external
 * inputs.  Astro ignores it and uses clear-only EOI.
 */
unsigned int hp_io_sapic_eoi(const HPIOSAPICPolicy *policy, uint64_t *regs,
                             size_t reg_count, uint32_t *ilr, uint64_t value,
                             uint32_t asserted, HPIOSAPICDeliver deliver,
                             void *opaque);

bool hp_io_sapic_software_interrupt(const HPIOSAPICPolicy *policy,
                                    const uint64_t *regs, size_t reg_count,
                                    HPIOSAPICDeliver deliver, void *opaque);

#endif
