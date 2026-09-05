/*
 * Intel 460GX chipset configuration targets
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_CHIPSET_H
#define HW_IA64_INTEL_460GX_CHIPSET_H

#include "hw/ia64/ia64_ras.h"
#include "qemu/bitops.h"
#include "qom/object.h"

#define TYPE_INTEL_460GX_CHIPSET "intel-460gx-chipset"
OBJECT_DECLARE_SIMPLE_TYPE(Intel460GXChipsetState, INTEL_460GX_CHIPSET)

#define INTEL_460GX_CHIPSET_PROP_HOST          "host"
#define INTEL_460GX_CHIPSET_PROP_EXPANDER_MASK "expander-mask"

#define INTEL_460GX_SAC_CBN_OFFSET       0x40
#define INTEL_460GX_SAC_DEVNPRES_OFFSET  0x44
#define INTEL_460GX_XXB_BUSNO_OFFSET     0x40
#define INTEL_460GX_XXB_SUBNO_OFFSET     0x41

#define INTEL_460GX_CHIPSET_SAC_DEVICE          0x00
#define INTEL_460GX_CHIPSET_SAC_MEMORY_DEVICE   0x01
#define INTEL_460GX_CHIPSET_SDC_DEVICE          0x04
#define INTEL_460GX_CHIPSET_MEMORY_CARD_A_DEVICE 0x05
#define INTEL_460GX_CHIPSET_MEMORY_CARD_B_DEVICE 0x06
#define INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE 0x10

#define INTEL_460GX_PXB_PORT  0
#define INTEL_460GX_WXB0_PORT 2
#define INTEL_460GX_WXB1_PORT 3
#define INTEL_460GX_GXB_PORT  4

#define INTEL_460GX_CHIPSET_FIXED_PRESENT_MASK \
    (BIT(INTEL_460GX_CHIPSET_SAC_DEVICE) |      \
     BIT(INTEL_460GX_CHIPSET_SAC_MEMORY_DEVICE) | \
     BIT(INTEL_460GX_CHIPSET_SDC_DEVICE) |      \
     BIT(INTEL_460GX_CHIPSET_MEMORY_CARD_A_DEVICE) | \
     BIT(INTEL_460GX_CHIPSET_MEMORY_CARD_B_DEVICE))

typedef enum Intel460GXMemoryError {
    INTEL_460GX_MEMORY_ERROR_CORRECTED,
    INTEL_460GX_MEMORY_ERROR_UNCORRECTED,
    INTEL_460GX_MEMORY_ERROR_COMMAND_PARITY,
    INTEL_460GX_MEMORY_ERROR_QUEUE_OVERFLOW,
} Intel460GXMemoryError;

void intel_460gx_chipset_set_fault_notify(
    Intel460GXChipsetState *s, IA64ChipsetFaultNotify notify, void *opaque);
void intel_460gx_chipset_report_memory_error(
    Intel460GXChipsetState *s, unsigned int card, unsigned int mac,
    Intel460GXMemoryError error, uint64_t address, uint64_t data,
    uint8_t ecc, uint8_t chunk, uint8_t itid);
bool intel_460gx_chipset_report_expander_fault(
    Intel460GXChipsetState *s, unsigned int port,
    const IA64ChipsetFault *fault);
void intel_460gx_chipset_set_downstream_reset_range(
    Intel460GXChipsetState *s, unsigned int port,
    uint8_t first_bus, uint8_t last_bus);

uint32_t intel_460gx_chipset_present_mask(uint8_t expander_mask);

#endif
