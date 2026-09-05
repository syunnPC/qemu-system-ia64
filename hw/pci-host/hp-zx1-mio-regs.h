/*
 * Register helpers for the HP zx1 MIO
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX1_MIO_REGS_H
#define HW_PCI_HOST_HP_ZX1_MIO_REGS_H

#define HP_ZX1_MIO_CSR_SIZE                 UINT64_C(0x10000)

#define HP_ZX1_MIO_F0_ID                    0x0000
#define HP_ZX1_MIO_F0_CLASS                 0x0008
#define HP_ZX1_MIO_MODULE_INFO              0x0100

#define HP_ZX1_MIO_LMMIO_DIR_BASE(n)        (0x0300 + (n) * 0x18)
#define HP_ZX1_MIO_LMMIO_DIR_MASK(n)        (0x0308 + (n) * 0x18)
#define HP_ZX1_MIO_LMMIO_DIR_ROUTE(n)       (0x0310 + (n) * 0x18)
#define HP_ZX1_MIO_LMMIO_DIST_BASE          0x0360
#define HP_ZX1_MIO_LMMIO_DIST_MASK          0x0368
#define HP_ZX1_MIO_LMMIO_DIST_ROUTE         0x0370
#define HP_ZX1_MIO_GMMIO_DIST_BASE          0x0378
#define HP_ZX1_MIO_GMMIO_DIST_MASK          0x0380
#define HP_ZX1_MIO_GMMIO_DIST_ROUTE         0x0388
#define HP_ZX1_MIO_IOS_DIST_BASE            0x0390
#define HP_ZX1_MIO_IOS_DIST_MASK            0x0398
#define HP_ZX1_MIO_IOS_DIST_ROUTE           0x03a0
#define HP_ZX1_MIO_ROPE_CONFIG_BASE         0x03a8
#define HP_ZX1_MIO_VGA_ROUTE                0x03b0
#define HP_ZX1_MIO_IOS_DIR_BASE             0x03c0
#define HP_ZX1_MIO_IOS_DIR_MASK             0x03c8
#define HP_ZX1_MIO_IOS_DIR_ROUTE            0x03d0

#define HP_ZX1_MIO_F1_ID                    0x1000
#define HP_ZX1_MIO_F1_CLASS                 0x1008
#define HP_ZX1_MIO_ROPE_CONFIG              0x1040
#define HP_ZX1_MIO_ERROR_CONFIG              0x1080
#define HP_ZX1_MIO_ERROR_STATUS              0x1088
#define HP_ZX1_MIO_ERROR_ADDRESS             0x1090
#define HP_ZX1_MIO_ERROR_INFORMATION         0x1098
#define HP_ZX1_MIO_LBA_PORT_CONTROL(n)      (0x1200 + (n) * 8)
#define HP_ZX1_MIO_LBA_PORT_COUNT           8

#define HP_ZX1_MIO_ERROR_CONFIG_NOTIFY       (UINT64_C(1) << 0)
#define HP_ZX1_MIO_ERROR_IOMMU               (UINT64_C(1) << 0)
#define HP_ZX1_MIO_ERROR_CSR_DECODE          (UINT64_C(1) << 1)
#define HP_ZX1_MIO_ERROR_MULTIPLE            (UINT64_C(1) << 62)
#define HP_ZX1_MIO_ERROR_VALID               (UINT64_C(1) << 63)
#define HP_ZX1_MIO_ERROR_STATUS_W1C          \
    (HP_ZX1_MIO_ERROR_IOMMU | HP_ZX1_MIO_ERROR_CSR_DECODE | \
     HP_ZX1_MIO_ERROR_MULTIPLE | HP_ZX1_MIO_ERROR_VALID)

typedef struct HPZX1MIORegs {
    /*
     * Guest readback latches for writable topology registers. Writes do not
     * change the topology established when the machine is realized.
     */
    uint64_t lmmio_dir_base[2];
    uint64_t lmmio_dir_mask[2];
    uint64_t lmmio_dir_route[2];
    uint64_t lmmio_dist_base;
    uint64_t lmmio_dist_mask;
    uint64_t lmmio_dist_route;
    uint64_t gmmio_dist_base;
    uint64_t gmmio_dist_mask;
    uint64_t gmmio_dist_route;
    uint64_t ios_dist_base;
    uint64_t ios_dist_mask;
    uint64_t ios_dist_route;
    uint64_t rope_config_base;
    uint64_t vga_route;
    uint64_t ios_dir_base;
    uint64_t ios_dir_mask;
    uint64_t ios_dir_route;

    uint64_t rope_config;
    uint64_t lba_port_control[HP_ZX1_MIO_LBA_PORT_COUNT];
    uint64_t error_config;
    uint64_t error_status;
    uint64_t error_address;
    uint64_t error_information;
} HPZX1MIORegs;

void hp_zx1_mio_regs_report_fault(HPZX1MIORegs *regs, uint64_t status,
                                  uint64_t address, uint64_t information);

void hp_zx1_mio_regs_reset(HPZX1MIORegs *regs);
bool hp_zx1_mio_regs_read(const HPZX1MIORegs *regs, uint64_t offset,
                          unsigned int size, uint64_t *value);
bool hp_zx1_mio_regs_write(HPZX1MIORegs *regs, uint64_t offset,
                           unsigned int size, uint64_t value);

/*
 * Function 1 write interface: byte_enable bit N selects byte N of the
 * aligned 8-byte register value.  This represents arbitrary Itanium-2 write
 * byte enables which a contiguous MemoryRegion callback cannot express.
 */
bool hp_zx1_mio_regs_write_be(HPZX1MIORegs *regs, uint64_t offset,
                              uint64_t value, uint8_t byte_enable);

/* True only for state reachable through the writable masks. */
bool hp_zx1_mio_regs_state_valid(const HPZX1MIORegs *regs);

#endif
