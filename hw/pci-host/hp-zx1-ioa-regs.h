/*
 * HP zx1 Mercury I/O adapter register core
 *
 * The caller supplies board placement, bus numbering, rope selection, and
 * interrupt delivery.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_ZX1_IOA_REGS_H
#define HW_PCI_HOST_HP_ZX1_IOA_REGS_H

#include "hw/pci-host/hp-io-sapic.h"
#include "hw/ia64/ia64_ras.h"

#define HP_ZX1_IOA_CONFIG_APERTURE_SIZE  0x2000
#define HP_ZX1_IOA_IMPLEMENTED_SIZE      0x1000
#define HP_ZX1_IOA_ROOT_COUNT            1
#define HP_ZX1_IOA_EXTERNAL_INPUTS       10

#define HP_ZX1_IOA_FUNCTION_ID           0x0000
#define HP_ZX1_IOA_FUNCTION_CLASS        0x0008
#define HP_ZX1_IOA_CAPABILITIES_POINTER  0x0030
#define HP_ZX1_IOA_CONFIG_ADDRESS        0x0040
#define HP_ZX1_IOA_CONFIG_DATA           0x0048
#define HP_ZX1_IOA_BUS_NUMBER            0x0058
#define HP_ZX1_IOA_AGP_CAPABILITY        0x0060
#define HP_ZX1_IOA_AGP_COMMAND           0x0068
#define HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS 0x0070
#define HP_ZX1_IOA_ARBITRATION_MASK      0x0080
#define HP_ZX1_IOA_PCIX_CAPABILITY       0x00a0
#define HP_ZX1_IOA_STATUS_CONTROL        0x0108
#define HP_ZX1_IOA_LMMIO_BASE            0x0200
#define HP_ZX1_IOA_LMMIO_MASK            0x0208
#define HP_ZX1_IOA_GMMIO_BASE            0x0210
#define HP_ZX1_IOA_GMMIO_MASK            0x0218
#define HP_ZX1_IOA_WLMMIO_BASE           0x0220
#define HP_ZX1_IOA_WLMMIO_MASK           0x0228
#define HP_ZX1_IOA_WGMMIO_BASE           0x0230
#define HP_ZX1_IOA_WGMMIO_MASK           0x0238
#define HP_ZX1_IOA_ELMMIO_BASE           0x0250
#define HP_ZX1_IOA_ELMMIO_MASK           0x0258
#define HP_ZX1_IOA_SLAVE_CONTROL         0x0278
#define HP_ZX1_IOA_MSI_BASE              0x0280
#define HP_ZX1_IOA_MSI_MASK              0x0288
#define HP_ZX1_IOA_BUS_MODE              0x0620
#define HP_ZX1_IOA_ERROR_CONFIGURATION   0x0680
#define HP_ZX1_IOA_ERROR_STATUS          0x0688
#define HP_ZX1_IOA_ERROR_MASTER_ID       0x0690
#define HP_ZX1_IOA_IOREGSEL              0x0800
#define HP_ZX1_IOA_IOWIN                 0x0810
#define HP_ZX1_IOA_IOEOI                 0x0840
#define HP_ZX1_IOA_SOFTWARE_INTERRUPT    0x0850

#define HP_ZX1_IOA_VENDOR_ID             0x103c
#define HP_ZX1_IOA_DEVICE_ID             0x122e
#define HP_ZX1_IOA_CLASS_CODE            0x060000
#define HP_ZX1_IOA_REVISION              0x32

#define HP_ZX1_IOA_CONFIG_ADDRESS_MASK   0x00fffffcU
#define HP_ZX1_IOA_PCI_COMMAND_MASK      0x0146U
#define HP_ZX1_IOA_PCI_STATUS_W1C        0xf900U
#define HP_ZX1_IOA_PCI_STATUS_RESET      0x02b0U
#define HP_ZX1_IOA_PCIX_STATUS_W1C       0x40080000U

#define HP_ZX1_IOA_SIC_RESET_FUNCTION    (UINT64_C(1) << 0)
#define HP_ZX1_IOA_SIC_FORWARD_VGA       (UINT64_C(1) << 3)
#define HP_ZX1_IOA_SIC_CLEAR_LOG         (UINT64_C(1) << 4)
#define HP_ZX1_IOA_SIC_CLEAR_ENABLE      (UINT64_C(1) << 5)
#define HP_ZX1_IOA_SIC_HARD_FAIL         (UINT64_C(1) << 6)
#define HP_ZX1_IOA_SIC_RESET_COMPLETE    (UINT64_C(1) << 32)

#define HP_ZX1_IOA_LMMIO_BASE_RESET      UINT64_C(0x80000000)
#define HP_ZX1_IOA_LMMIO_BASE_WRITABLE   UINT64_C(0x7fff0001)
#define HP_ZX1_IOA_LMMIO_MASK_RESET      UINT64_C(0x80000000)
#define HP_ZX1_IOA_LMMIO_MASK_WRITABLE   UINT64_C(0x7fff0000)
#define HP_ZX1_IOA_GMMIO_BASE_WRITABLE   UINT64_C(0x00000ffffc000001)
#define HP_ZX1_IOA_GMMIO_MASK_WRITABLE   UINT64_C(0x00000ffffc000000)
#define HP_ZX1_IOA_WLMMIO_BASE_RESET     UINT64_C(0x80000000)
#define HP_ZX1_IOA_WLMMIO_BASE_WRITABLE  UINT64_C(0x7ff00001)
#define HP_ZX1_IOA_WLMMIO_MASK_RESET     UINT64_C(0x80000000)
#define HP_ZX1_IOA_WLMMIO_MASK_WRITABLE  UINT64_C(0x7ff00000)
#define HP_ZX1_IOA_WGMMIO_BASE_WRITABLE  UINT64_C(0x00000fff00000001)
#define HP_ZX1_IOA_WGMMIO_MASK_WRITABLE  UINT64_C(0x00000fff00000000)
#define HP_ZX1_IOA_ELMMIO_BASE_RESET     UINT64_C(0x80000000)
#define HP_ZX1_IOA_ELMMIO_BASE_WRITABLE  UINT64_C(0x7ff00001)
#define HP_ZX1_IOA_ELMMIO_MASK_RESET     UINT64_C(0x80000000)
#define HP_ZX1_IOA_ELMMIO_MASK_WRITABLE  UINT64_C(0x7ff00000)
#define HP_ZX1_IOA_MSI_BASE_RESET        UINT64_C(0x00000000fee00001)
#define HP_ZX1_IOA_MSI_BASE_WRITABLE     UINT64_C(0x00000fffffff0001)
#define HP_ZX1_IOA_MSI_MASK_RESET        UINT64_C(0x00000ffffff00000)
#define HP_ZX1_IOA_MSI_MASK_WRITABLE     UINT64_C(0x00000fffffff0000)

#define HP_ZX1_IOA_BUS_MODE_AGP          (1U << 0)
#define HP_ZX1_IOA_BUS_MODE_SIX_MASTERS  (1U << 3)
#define HP_ZX1_IOA_BUS_MODE_ROPE_2X_L    (1U << 5)
#define HP_ZX1_IOA_BUS_MODE_BUS_SHIFT    13
#define HP_ZX1_IOA_BUS_MODE_BUS_MASK     (3U << 13)
#define HP_ZX1_IOA_BUS_MODE_VISIBLE      0x000161ffU
#define HP_ZX1_IOA_BUS_MODE_SAFE_WRITE   0x00010100U

#define HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE 0x0006200fU
#define HP_ZX1_IOA_SLAVE_CONTROL_RESET   0x00000006U

/* Mercury ERS 6.3.2: read-only error log, cleared through SIC.CE/CL. */
#define HP_ZX1_IOA_ERROR_CODE_MASK       UINT64_C(0x1f)
#define HP_ZX1_IOA_ERROR_MASTER_ABORT    UINT64_C(0x0c)
#define HP_ZX1_IOA_ERROR_OV             (UINT64_C(1) << 5)
#define HP_ZX1_IOA_ERROR_FE             (UINT64_C(1) << 9)
#define HP_ZX1_IOA_ERROR_UNC            (UINT64_C(1) << 10)
#define HP_ZX1_IOA_ERROR_CORR           (UINT64_C(1) << 11)
#define HP_ZX1_IOA_ERROR_HF             (UINT64_C(1) << 32)
#define HP_ZX1_IOA_ERROR_SMART          (UINT64_C(1) << 33)
#define HP_ZX1_IOA_ERROR_FE_OV          (UINT64_C(1) << 41)
#define HP_ZX1_IOA_ERROR_UNC_OV         (UINT64_C(1) << 42)
#define HP_ZX1_IOA_ERROR_CORR_OV        (UINT64_C(1) << 43)
#define HP_ZX1_IOA_ERROR_SEVERITY_MASK  \
    (HP_ZX1_IOA_ERROR_FE | HP_ZX1_IOA_ERROR_UNC | HP_ZX1_IOA_ERROR_CORR)
#define HP_ZX1_IOA_ERROR_STATUS_MASK   \
    (HP_ZX1_IOA_ERROR_CODE_MASK | HP_ZX1_IOA_ERROR_OV | \
     HP_ZX1_IOA_ERROR_SEVERITY_MASK | HP_ZX1_IOA_ERROR_HF | \
     HP_ZX1_IOA_ERROR_SMART | HP_ZX1_IOA_ERROR_FE_OV | \
     HP_ZX1_IOA_ERROR_UNC_OV | HP_ZX1_IOA_ERROR_CORR_OV)
#define HP_ZX1_IOA_OUTBOUND_CONFIG_CYCLE (UINT64_C(1) << 62)
#define HP_ZX1_IOA_PCI_STATUS_MASTER_ABORT UINT16_C(0x2000)

/* Internal frontend fault reasons, not hardware error-register bits. */
typedef enum HPZX1IOAFault {
    HP_ZX1_IOA_FAULT_CONFIG_ABORT,
    HP_ZX1_IOA_FAULT_MSI_DECODE,
    HP_ZX1_IOA_FAULT_CSR_DECODE,
} HPZX1IOAFault;

typedef enum HPZX1IOAMode {
    HP_ZX1_IOA_MODE_PCI,
    HP_ZX1_IOA_MODE_PCIX,
    HP_ZX1_IOA_MODE_AGP,
} HPZX1IOAMode;

/* address includes the byte lane; value is right-justified for size. */
typedef bool (*HPZX1IOAConfigRead)(void *opaque, uint32_t address,
                                   unsigned int size, uint32_t *value);
typedef bool (*HPZX1IOAConfigWrite)(void *opaque, uint32_t address,
                                    unsigned int size, uint32_t value);

typedef struct HPZX1IOARegsConfig {
    HPZX1IOAMode mode;
    uint8_t rope_mask;

    /* Initial values for the secondary and subordinate bus registers. */
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    bool pci_reset_asserted;

    /*
     * Board-supplied reset latches.  Reserved bits read as zero and are
     * guest read-only.
     */
    uint64_t bus_mode_reset;
    uint64_t slave_control_reset_straps;
    uint32_t error_configuration_reset_straps;

    HPZX1IOAConfigRead config_read;
    HPZX1IOAConfigWrite config_write;
    void *config_opaque;
    HPIOSAPICDeliver deliver;
    void *delivery_opaque;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
} HPZX1IOARegsConfig;

typedef struct HPZX1IOARegs {
    HPZX1IOARegsConfig reset_config;

    uint16_t pci_command;
    uint16_t pci_status;
    uint8_t latency_timer;
    uint8_t cache_line_size;
    uint32_t config_address;
    uint16_t bus_number;
    uint32_t agp_command;
    uint16_t pcix_command;
    uint32_t pcix_status;
    uint32_t arbitration_mask;
    uint32_t status_control;
    bool pci_reset_asserted;

    uint64_t lmmio_base;
    uint64_t lmmio_mask;
    uint64_t gmmio_base;
    uint64_t gmmio_mask;
    uint64_t wlmmio_base;
    uint64_t wlmmio_mask;
    uint64_t wgmmio_base;
    uint64_t wgmmio_mask;
    uint64_t elmmio_base;
    uint64_t elmmio_mask;
    uint64_t msi_base;
    uint64_t msi_mask;
    uint64_t bus_mode;
    uint64_t slave_control;
    uint32_t error_configuration;
    uint64_t error_status;
    uint64_t outbound_error_address;

    uint32_t sapic_selector;
    uint32_t sapic_in_service;
    uint32_t sapic_asserted;
    uint64_t sapic_regs[HP_IO_SAPIC_ZX1_REG_COUNT];
} HPZX1IOARegs;

bool hp_zx1_ioa_regs_init(HPZX1IOARegs *s,
                          const HPZX1IOARegsConfig *config);
void hp_zx1_ioa_regs_reset(HPZX1IOARegs *s);

bool hp_zx1_ioa_regs_read(HPZX1IOARegs *s, uint64_t offset,
                          unsigned int size, uint64_t *value);
bool hp_zx1_ioa_regs_write(HPZX1IOARegs *s, uint64_t offset,
                           unsigned int size, uint64_t value);

/* asserted is the board frontend's polarity-resolved logical pin state. */
bool hp_zx1_ioa_regs_set_input(HPZX1IOARegs *s, unsigned int input,
                               bool asserted);
void hp_zx1_ioa_regs_set_pci_status(HPZX1IOARegs *s, uint16_t status);
void hp_zx1_ioa_regs_set_pcix_status(HPZX1IOARegs *s, uint32_t status);
void hp_zx1_ioa_regs_report_fault(HPZX1IOARegs *s, HPZX1IOAFault reason,
                                  uint64_t address, uint64_t data);

bool hp_zx1_ioa_regs_msi_contains(const HPZX1IOARegs *s, uint64_t address);

/*
 * Decode the enabled MSI range.  Its mask must be naturally aligned and
 * contiguous, and its base must be at least 2 GiB.  Invalid programming
 * remains readable but performs no MSI decode.  base and size are written
 * only on success.
 */
bool hp_zx1_ioa_regs_msi_range(const HPZX1IOARegs *s, uint64_t *base,
                               uint64_t *size);
unsigned int hp_zx1_ioa_regs_root_count(const HPZX1IOARegs *s);
uint8_t hp_zx1_ioa_regs_rope_mask(const HPZX1IOARegs *s);

#endif
