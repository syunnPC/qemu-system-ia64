/*
 * IA-64 i2000 460GX integration-test layout
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_I2000_460GX_TEST_LAYOUT_H
#define HW_IA64_I2000_460GX_TEST_LAYOUT_H

#include "qemu/typedefs.h"

/* Fixed addresses used by the integration test. */
#define IA64_I2000_460GX_TEST_ROOT_COUNT          3U
#define IA64_I2000_460GX_TEST_HOST_PORT_COUNT     8U
#define IA64_I2000_460GX_TEST_PCI_INTX_COUNT      4U

#define IA64_I2000_460GX_TEST_RAM_BASE            UINT64_C(0x00000000)
#define IA64_I2000_460GX_TEST_RAM_SIZE            UINT64_C(0x80000000)
#define IA64_I2000_460GX_TEST_FIRMWARE_BASE       UINT64_C(0x00100000)
#define IA64_I2000_460GX_TEST_FIRMWARE_SIZE       UINT64_C(0x00200000)
#define IA64_I2000_460GX_TEST_DESC_ROM_BASE       UINT64_C(0x00300000)
#define IA64_I2000_460GX_TEST_DESC_ROM_SIZE       UINT64_C(0x00001000)
#define IA64_I2000_460GX_TEST_DESC_ENVELOPE_SIZE  UINT64_C(0x00002000)
#define IA64_I2000_460GX_TEST_DMA_BASE            UINT64_C(0)
#define IA64_I2000_460GX_TEST_DMA_SIZE            IA64_I2000_460GX_TEST_RAM_SIZE

#define IA64_I2000_460GX_TEST_PID_BASE            UINT64_C(0xfec00000)
#define IA64_I2000_460GX_TEST_PID_SIZE            UINT64_C(0x00001000)
#define IA64_I2000_460GX_TEST_PID_ENVELOPE_SIZE   UINT64_C(0x00002000)
#define IA64_I2000_460GX_TEST_PIB_BASE            UINT64_C(0xfee00000)
#define IA64_I2000_460GX_TEST_PIB_SIZE            UINT64_C(0x00200000)

#define IA64_I2000_460GX_TEST_LEGACY_IO_BASE      \
    UINT64_C(0x0000000ffc000000)
#define IA64_I2000_460GX_TEST_LEGACY_IO_SIZE      UINT64_C(0x04000000)
#define IA64_I2000_460GX_TEST_CF8_PORT             UINT16_C(0x0cf8)
#define IA64_I2000_460GX_TEST_CFC_PORT             UINT16_C(0x0cfc)
#define IA64_I2000_460GX_TEST_CF8_PA               \
    UINT64_C(0x0000000ffc33ecf8)
#define IA64_I2000_460GX_TEST_CFC_PA               \
    UINT64_C(0x0000000ffc33fcfc)

#define IA64_I2000_460GX_TEST_CBN                  UINT8_C(0xff)
#define IA64_I2000_460GX_TEST_CHIPSET_PRESENT      UINT32_C(0)
#define IA64_I2000_460GX_TEST_PID_ID               UINT8_C(0)
#define IA64_I2000_460GX_TEST_PID_PIN_COUNT        64U
#define IA64_I2000_460GX_TEST_LEGACY_PIN_COUNT     16U
#define IA64_I2000_460GX_TEST_CONSOLE_CANDIDATE_GSI 4U
#define IA64_I2000_460GX_TEST_CF8_IO_ROOT       0U

typedef struct IA64I2000460GXTestRange {
    uint64_t base;
    uint64_t size;
} IA64I2000460GXTestRange;

typedef struct IA64I2000460GXTestRoot {
    uint16_t segment;
    uint8_t config_mechanism;
    uint8_t first_bus;
    uint8_t last_bus;
    uint8_t host_port;
    uint8_t intx_base;
    uint32_t io_base;
    uint32_t io_size;
    uint64_t pci_mmio32_base;
    uint64_t cpu_mmio32_base;
    uint64_t mmio32_size;
    uint64_t mmio64_base;
    uint64_t mmio64_size;
    uint64_t dma_base;
    uint64_t dma_size;
    uint64_t dma_target_offset;
} IA64I2000460GXTestRoot;

typedef struct IA64I2000460GXTestLayout {
    IA64I2000460GXTestRange ram;
    IA64I2000460GXTestRange firmware;
    IA64I2000460GXTestRange descriptor_rom;
    IA64I2000460GXTestRange descriptor_envelope;
    IA64I2000460GXTestRange pid_decode;
    IA64I2000460GXTestRange pid_envelope;
    IA64I2000460GXTestRange pib;
    IA64I2000460GXTestRange legacy_io;
    uint64_t cf8_pa;
    uint64_t cfc_pa;
    uint32_t chipset_present;
    uint8_t cbn;
    uint8_t pid_id;
    uint8_t pid_pin_count;
    uint8_t legacy_pin_count;
    uint8_t cf8_io_root;
    IA64I2000460GXTestRoot roots[IA64_I2000_460GX_TEST_ROOT_COUNT];
} IA64I2000460GXTestLayout;

/* Populate the fixed 460GX test layout. */
void ia64_i2000_460gx_test_layout_init(IA64I2000460GXTestLayout *layout);

/*
 * Validate the fixed addresses and their structural invariants without
 * creating or mapping devices.
 */
bool ia64_i2000_460gx_test_layout_validate(
    const IA64I2000460GXTestLayout *layout, Error **errp);

/* Translate a 16-bit logical port through the fixture's sparse I/O mapping. */
uint64_t ia64_i2000_460gx_test_sparse_io_pa(uint16_t port);

#endif
