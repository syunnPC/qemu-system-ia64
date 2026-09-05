/*
 * HP zx1 Mercury I/O adapter register core
 *
 * Board placement, rope routing, PCI topology, and interrupt destinations are
 * supplied by the caller.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-ioa-regs.h"

#define IOA_AGP_CAPABILITY_RESET UINT64_C(0x0f00023700200002)
#define IOA_AGP_COMMAND_WRITABLE UINT32_C(0x00000337)

#define IOA_PCIX_CAPABILITY_ID   UINT64_C(0x0000000000000007)
#define IOA_PCIX_STATUS_RESET    UINT32_C(0x0013ff00)
#define IOA_PCIX_COMMAND_WRITABLE UINT16_C(0x0001)

#define IOA_ARBITRATION_MASK_RESET UINT32_C(0x00000001)
#define IOA_ARBITRATION_MASK_WRITE UINT32_C(0x0000007f)
#define IOA_ARBITRATION_MASK_F     UINT32_C(0x00000040)
#define IOA_ERROR_CONFIG_SMART     UINT32_C(0x00000020)
#define IOA_44BIT_ADDRESS_MASK      UINT64_C(0x00000fffffffffff)
#define IOA_MSI_ABOVE_2G_MASK       UINT64_C(0x00000fff80000000)

#define IOA_SIC_LATCH_MASK (HP_ZX1_IOA_SIC_FORWARD_VGA | \
                            HP_ZX1_IOA_SIC_CLEAR_LOG | \
                            HP_ZX1_IOA_SIC_CLEAR_ENABLE | \
                            HP_ZX1_IOA_SIC_HARD_FAIL)

#define IOA_SOFTWARE_ENTRY 10

typedef struct HPZX1IOAEOIDelivery {
    HPZX1IOARegs *ioa;
    unsigned int entries[HP_ZX1_IOA_EXTERNAL_INPUTS];
    unsigned int count;
    unsigned int next;
} HPZX1IOAEOIDelivery;

static unsigned int ioa_popcount8(uint8_t value)
{
    unsigned int count = 0;

    while (value) {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

static bool ioa_access_valid(uint64_t offset, unsigned int size)
{
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return false;
    }
    if (offset & (size - 1)) {
        return false;
    }
    if (offset >= HP_ZX1_IOA_CONFIG_APERTURE_SIZE ||
        size > HP_ZX1_IOA_CONFIG_APERTURE_SIZE - offset) {
        return false;
    }
    return (offset & 7) + size <= 8;
}

static uint64_t ioa_low_mask(unsigned int size)
{
    return size == 8 ? UINT64_MAX : (UINT64_C(1) << (size * 8)) - 1;
}

static uint64_t ioa_access_mask(uint64_t offset, unsigned int size)
{
    return ioa_low_mask(size) << ((offset & 7) * 8);
}

static uint64_t ioa_access_data(uint64_t offset, unsigned int size,
                                uint64_t value)
{
    return (value & ioa_low_mask(size)) << ((offset & 7) * 8);
}

static uint64_t ioa_extract(uint64_t offset, unsigned int size,
                            uint64_t value)
{
    return (value >> ((offset & 7) * 8)) & ioa_low_mask(size);
}

static void ioa_latch_write(uint64_t *latch, uint64_t writable,
                            uint64_t access_mask, uint64_t data)
{
    uint64_t mask = writable & access_mask;

    *latch = (*latch & ~mask) | (data & mask);
}

static unsigned int ioa_external_inputs(const HPZX1IOARegs *s)
{
    /* AGP provides seven interrupt inputs; PCI and PCI-X provide ten. */
    return s->reset_config.mode == HP_ZX1_IOA_MODE_AGP ? 7 :
           HP_ZX1_IOA_EXTERNAL_INPUTS;
}

static unsigned int ioa_rte_low(unsigned int entry)
{
    return HP_IO_SAPIC_RTE_BASE + 2 * entry;
}

static void ioa_function_reset(HPZX1IOARegs *s)
{
    unsigned int entry;

    /* Function reset clears AGP command and masks all SAPIC entries. */
    s->agp_command = 0;
    s->sapic_in_service = 0;
    for (entry = 0; entry < hp_io_sapic_zx1_policy.entry_count; entry++) {
        s->sapic_regs[ioa_rte_low(entry)] |= HP_IO_SAPIC_RTE_MASK;
    }
}

static bool ioa_deliver_entry(HPZX1IOARegs *s, unsigned int entry,
                              bool level)
{
    HPIOSAPICMessage message;
    uint32_t bit = 1U << entry;
    unsigned int low_index = ioa_rte_low(entry);

    if (!hp_io_sapic_make_message(&hp_io_sapic_zx1_policy, s->sapic_regs,
                                  G_N_ELEMENTS(s->sapic_regs), entry,
                                  &message)) {
        return false;
    }

    if (level) {
        s->sapic_in_service |= bit;
    }

    /* Delivery Status is set while the callback accepts the message. */
    s->sapic_regs[low_index] |= HP_IO_SAPIC_RTE_STATUS;
    if (s->reset_config.deliver) {
        s->reset_config.deliver(s->reset_config.delivery_opaque, &message);
    }
    s->sapic_regs[low_index] &= ~HP_IO_SAPIC_RTE_STATUS;
    return true;
}

static bool ioa_reevaluate_level(HPZX1IOARegs *s, unsigned int input)
{
    uint32_t bit = 1U << input;
    uint32_t low = s->sapic_regs[ioa_rte_low(input)];

    if (!(s->sapic_asserted & bit) ||
        !(low & HP_IO_SAPIC_RTE_TRIGGER) ||
        (low & HP_IO_SAPIC_RTE_MASK) ||
        (s->sapic_in_service & bit)) {
        return false;
    }
    return ioa_deliver_entry(s, input, true);
}

static void ioa_forward_eoi_delivery(void *opaque,
                                     const HPIOSAPICMessage *message)
{
    HPZX1IOAEOIDelivery *delivery = opaque;
    HPZX1IOARegs *s = delivery->ioa;
    unsigned int entry;
    unsigned int low_index;

    if (delivery->next >= delivery->count) {
        return;
    }

    entry = delivery->entries[delivery->next++];
    low_index = ioa_rte_low(entry);
    s->sapic_regs[low_index] |= HP_IO_SAPIC_RTE_STATUS;
    if (s->reset_config.deliver) {
        s->reset_config.deliver(s->reset_config.delivery_opaque, message);
    }
    s->sapic_regs[low_index] &= ~HP_IO_SAPIC_RTE_STATUS;
}

static void ioa_eoi(HPZX1IOARegs *s, uint64_t value)
{
    HPZX1IOAEOIDelivery delivery = { .ioa = s };
    uint32_t vector = value & HP_IO_SAPIC_RTE_VECTOR;
    unsigned int input;

    /* Preserve the matching entry order for the delivery callback. */
    for (input = 0; input < ioa_external_inputs(s); input++) {
        uint32_t bit = 1U << input;
        uint32_t low = s->sapic_regs[ioa_rte_low(input)];

        if ((low & HP_IO_SAPIC_RTE_VECTOR) == vector &&
            (low & HP_IO_SAPIC_RTE_TRIGGER) &&
            !(low & HP_IO_SAPIC_RTE_MASK) &&
            (s->sapic_asserted & bit)) {
            delivery.entries[delivery.count++] = input;
        }
    }

    hp_io_sapic_eoi(&hp_io_sapic_zx1_policy, s->sapic_regs,
                    G_N_ELEMENTS(s->sapic_regs), &s->sapic_in_service,
                    value, s->sapic_asserted, ioa_forward_eoi_delivery,
                    &delivery);
}

static void ioa_forward_software_delivery(void *opaque,
                                          const HPIOSAPICMessage *message)
{
    HPZX1IOARegs *s = opaque;
    unsigned int low_index = ioa_rte_low(IOA_SOFTWARE_ENTRY);

    s->sapic_regs[low_index] |= HP_IO_SAPIC_RTE_STATUS;
    if (s->reset_config.deliver) {
        s->reset_config.deliver(s->reset_config.delivery_opaque, message);
    }
    s->sapic_regs[low_index] &= ~HP_IO_SAPIC_RTE_STATUS;
}

static bool ioa_sapic_direct(uint64_t base)
{
    return base == HP_ZX1_IOA_IOREGSEL || base == HP_ZX1_IOA_IOWIN ||
           base == HP_ZX1_IOA_IOEOI ||
           base == HP_ZX1_IOA_SOFTWARE_INTERRUPT;
}

static bool ioa_sapic_read(HPZX1IOARegs *s, uint64_t offset,
                           unsigned int size, uint64_t *value)
{
    uint64_t base = offset & ~UINT64_C(7);
    uint64_t window = 0;

    /* The SAPIC interface is 32-bit.  Its upper qword lanes are reserved. */
    if (size != 4) {
        return false;
    }
    if ((offset & 7) == 4) {
        *value = 0;
        return true;
    }
    if (offset != base) {
        return false;
    }

    switch (base) {
    case HP_ZX1_IOA_IOREGSEL:
        *value = hp_io_sapic_select_read(&hp_io_sapic_zx1_policy,
                                         s->sapic_selector);
        return true;
    case HP_ZX1_IOA_IOWIN:
        if (hp_io_sapic_window_read(&hp_io_sapic_zx1_policy,
                                    s->sapic_selector, s->sapic_regs,
                                    G_N_ELEMENTS(s->sapic_regs), &window)) {
            *value = (uint32_t)window;
        } else {
            *value = 0;
        }
        return true;
    case HP_ZX1_IOA_IOEOI:
    case HP_ZX1_IOA_SOFTWARE_INTERRUPT:
        *value = 0;
        return true;
    default:
        return false;
    }
}

static bool ioa_sapic_write(HPZX1IOARegs *s, uint64_t offset,
                            unsigned int size, uint64_t value)
{
    uint64_t base = offset & ~UINT64_C(7);
    uint32_t old_low = 0;
    uint32_t new_low;
    unsigned int entry;

    if (size != 4) {
        return false;
    }
    if ((offset & 7) == 4) {
        return true;
    }
    if (offset != base) {
        return false;
    }

    switch (base) {
    case HP_ZX1_IOA_IOREGSEL:
        hp_io_sapic_select_write(&hp_io_sapic_zx1_policy,
                                 &s->sapic_selector, value);
        return true;
    case HP_ZX1_IOA_IOWIN:
        if (s->sapic_selector >= HP_IO_SAPIC_RTE_BASE &&
            !(s->sapic_selector & 1)) {
            entry = (s->sapic_selector - HP_IO_SAPIC_RTE_BASE) / 2;
            if (entry < hp_io_sapic_zx1_policy.entry_count) {
                old_low = s->sapic_regs[s->sapic_selector];
            }
        } else {
            entry = UINT_MAX;
        }

        /* Unsupported selectors and read-only registers discard writes. */
        hp_io_sapic_window_write(&hp_io_sapic_zx1_policy,
                                 s->sapic_selector, s->sapic_regs,
                                 G_N_ELEMENTS(s->sapic_regs), value);
        if (entry >= ioa_external_inputs(s)) {
            return true;
        }

        new_low = s->sapic_regs[s->sapic_selector];
        if ((old_low & HP_IO_SAPIC_RTE_TRIGGER) &&
            !(new_low & HP_IO_SAPIC_RTE_TRIGGER)) {
            s->sapic_in_service &= ~(1U << entry);
        }
        ioa_reevaluate_level(s, entry);
        return true;
    case HP_ZX1_IOA_IOEOI:
        ioa_eoi(s, value);
        return true;
    case HP_ZX1_IOA_SOFTWARE_INTERRUPT:
        hp_io_sapic_software_interrupt(&hp_io_sapic_zx1_policy,
                                       s->sapic_regs,
                                       G_N_ELEMENTS(s->sapic_regs),
                                       ioa_forward_software_delivery, s);
        return true;
    default:
        return false;
    }
}

static bool ioa_config_target_present(const HPZX1IOARegs *s)
{
    uint32_t bus = (s->config_address >> 16) & UINT8_MAX;
    uint32_t device = (s->config_address >> 11) & 0x1f;

    /* Type 0 configuration cycles have only sixteen physical IDSEL lines. */
    return bus != 0 || device <= 15;
}

static void ioa_log_config_abort(HPZX1IOARegs *s, uint32_t address)
{
    uint64_t status = s->error_status;
    uint32_t bus_address;

    /* ERS 6.5: configuration master-aborts are nonfatal in either mode. */
    s->pci_status |= HP_ZX1_IOA_PCI_STATUS_MASTER_ABORT;
    s->status_control &= ~HP_ZX1_IOA_SIC_CLEAR_ENABLE;
    if (!(status & (HP_ZX1_IOA_ERROR_FE | HP_ZX1_IOA_ERROR_UNC))) {
        status &= ~(HP_ZX1_IOA_ERROR_CODE_MASK | HP_ZX1_IOA_ERROR_HF |
                    HP_ZX1_IOA_ERROR_SMART);
        status |= HP_ZX1_IOA_ERROR_MASTER_ABORT;
        if (s->status_control & HP_ZX1_IOA_SIC_HARD_FAIL) {
            status |= HP_ZX1_IOA_ERROR_HF;
        }
        if (s->error_configuration & IOA_ERROR_CONFIG_SMART) {
            status |= HP_ZX1_IOA_ERROR_SMART;
        }

        /* ERS 6.3.6/8.3: log the address actually driven on the PCI bus. */
        if (address & 0xff0000) {
            bus_address = (address & HP_ZX1_IOA_CONFIG_ADDRESS_MASK) | 1;
        } else {
            unsigned int device = (address >> 11) & 0x1f;

            bus_address = address & 0x7fc;
            if (device < 16) {
                bus_address |= 1U << (device + 16);
            }
        }
        s->outbound_error_address = HP_ZX1_IOA_OUTBOUND_CONFIG_CYCLE |
                                    bus_address;
    }
    if (status & HP_ZX1_IOA_ERROR_UNC) {
        status |= HP_ZX1_IOA_ERROR_UNC_OV;
    }
    status |= HP_ZX1_IOA_ERROR_UNC;
    /* OV describes repeats of the highest severity, not of any error. */
    if (!(status & HP_ZX1_IOA_ERROR_FE) &&
        (status & HP_ZX1_IOA_ERROR_UNC_OV)) {
        status |= HP_ZX1_IOA_ERROR_OV;
    }
    s->error_status = status;
}

void hp_zx1_ioa_regs_report_fault(HPZX1IOARegs *s, HPZX1IOAFault reason,
                                  uint64_t address, uint64_t data)
{
    IA64ChipsetFault fault;

    if (!s) {
        return;
    }
    /*
     * Configuration master-aborts update the local PCI error log without
     * notifying the platform RAS hub.
     */
    if (reason == HP_ZX1_IOA_FAULT_CONFIG_ABORT) {
        ioa_log_config_abort(s, address);
        return;
    }
    if (!s->reset_config.fault_notify) {
        return;
    }
    /*
     * Frontend decode faults use the platform RAS hub, not the IOA PCI error
     * registers.
     */
    fault = (IA64ChipsetFault) {
        .source = IA64_CHIPSET_FAULT_ZX1_IOA,
        .reason = IA64_CHIPSET_FAULT_DECODE,
        .bus = s->reset_config.secondary_bus,
        .address = address,
        .information = data,
    };
    s->reset_config.fault_notify(s->reset_config.fault_opaque, &fault);
}

static bool ioa_config_data_read(HPZX1IOARegs *s, uint64_t offset,
                                 unsigned int size, uint64_t *value)
{
    unsigned int lane = offset - HP_ZX1_IOA_CONFIG_DATA;
    uint32_t data = UINT32_MAX;
    unsigned int cycle_size = size;

    if (size == 8 && lane == 0) {
        cycle_size = 4;
    } else if (size > 4 || lane + size > 4) {
        *value = 0;
        return true;
    }

    if (!ioa_config_target_present(s) || !s->reset_config.config_read ||
        !s->reset_config.config_read(s->reset_config.config_opaque,
                                     s->config_address + lane, cycle_size,
                                     &data)) {
        data = UINT32_MAX;
        hp_zx1_ioa_regs_report_fault(s, HP_ZX1_IOA_FAULT_CONFIG_ABORT,
                                     s->config_address + lane, 0);
    }
    *value = data & ioa_low_mask(cycle_size);
    return true;
}

static bool ioa_config_data_write(HPZX1IOARegs *s, uint64_t offset,
                                  unsigned int size, uint64_t value)
{
    unsigned int lane = offset - HP_ZX1_IOA_CONFIG_DATA;
    unsigned int cycle_size = size;

    if (size == 8 && lane == 0) {
        cycle_size = 4;
    } else if (size > 4 || lane + size > 4) {
        return true;
    }

    if (!ioa_config_target_present(s) || !s->reset_config.config_write ||
        !s->reset_config.config_write(s->reset_config.config_opaque,
                                      s->config_address + lane, cycle_size,
                                      value & ioa_low_mask(cycle_size))) {
        hp_zx1_ioa_regs_report_fault(s, HP_ZX1_IOA_FAULT_CONFIG_ABORT,
                                     s->config_address + lane, value);
    }
    return true;
}

bool hp_zx1_ioa_regs_init(HPZX1IOARegs *s,
                          const HPZX1IOARegsConfig *config)
{
    HPZX1IOARegsConfig reset_config;
    unsigned int ropes;
    uint64_t bus_operation;
    bool agp_mode;
    bool dual_rope;

    if (!s || !config ||
        (unsigned int)config->mode > HP_ZX1_IOA_MODE_AGP ||
        config->secondary_bus > config->subordinate_bus) {
        return false;
    }

    ropes = ioa_popcount8(config->rope_mask);
    if (ropes < 1 || ropes > 2) {
        return false;
    }

    agp_mode = config->bus_mode_reset & HP_ZX1_IOA_BUS_MODE_AGP;
    dual_rope = !(config->bus_mode_reset &
                  HP_ZX1_IOA_BUS_MODE_ROPE_2X_L);
    bus_operation = config->bus_mode_reset & HP_ZX1_IOA_BUS_MODE_BUS_MASK;
    if (agp_mode != (config->mode == HP_ZX1_IOA_MODE_AGP) ||
        dual_rope != (ropes == 2) ||
        (config->mode == HP_ZX1_IOA_MODE_AGP && ropes != 2) ||
        (config->mode == HP_ZX1_IOA_MODE_PCI && bus_operation != 0) ||
        (config->mode == HP_ZX1_IOA_MODE_PCIX && bus_operation == 0) ||
        (config->slave_control_reset_straps &
         HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE) ||
        (config->error_configuration_reset_straps &
         IOA_ERROR_CONFIG_SMART)) {
        return false;
    }

    /* Copy first so config may safely alias s->reset_config. */
    reset_config = *config;
    memset(s, 0, sizeof(*s));
    s->reset_config = reset_config;
    hp_zx1_ioa_regs_reset(s);
    return true;
}

void hp_zx1_ioa_regs_reset(HPZX1IOARegs *s)
{
    if (!s) {
        return;
    }

    s->pci_command = 0;
    s->pci_status = HP_ZX1_IOA_PCI_STATUS_RESET;
    s->latency_timer = 0;
    s->cache_line_size = 0;
    s->config_address = 0;
    s->bus_number = ((uint16_t)s->reset_config.subordinate_bus << 8) |
                    s->reset_config.secondary_bus;
    s->agp_command = 0;
    s->pcix_command = 0;
    s->pcix_status = IOA_PCIX_STATUS_RESET;
    s->arbitration_mask = IOA_ARBITRATION_MASK_RESET;
    s->status_control = 0;
    s->pci_reset_asserted = s->reset_config.pci_reset_asserted;

    s->lmmio_base = HP_ZX1_IOA_LMMIO_BASE_RESET;
    s->lmmio_mask = HP_ZX1_IOA_LMMIO_MASK_RESET;
    s->gmmio_base = 0;
    s->gmmio_mask = 0;
    s->wlmmio_base = HP_ZX1_IOA_WLMMIO_BASE_RESET;
    s->wlmmio_mask = HP_ZX1_IOA_WLMMIO_MASK_RESET;
    s->wgmmio_base = 0;
    s->wgmmio_mask = 0;
    s->elmmio_base = HP_ZX1_IOA_ELMMIO_BASE_RESET;
    s->elmmio_mask = HP_ZX1_IOA_ELMMIO_MASK_RESET;
    s->msi_base = HP_ZX1_IOA_MSI_BASE_RESET;
    s->msi_mask = HP_ZX1_IOA_MSI_MASK_RESET;
    s->bus_mode = s->reset_config.bus_mode_reset;
    s->slave_control = s->reset_config.slave_control_reset_straps |
                       HP_ZX1_IOA_SLAVE_CONTROL_RESET;
    s->error_configuration =
        s->reset_config.error_configuration_reset_straps;
    s->error_status = 0;
    s->outbound_error_address = 0;

    s->sapic_in_service = 0;
    s->sapic_asserted = 0;
    hp_io_sapic_reset(&hp_io_sapic_zx1_policy, &s->sapic_selector,
                      s->sapic_regs, G_N_ELEMENTS(s->sapic_regs),
                      &s->sapic_in_service, 0);
}

bool hp_zx1_ioa_regs_read(HPZX1IOARegs *s, uint64_t offset,
                          unsigned int size, uint64_t *value)
{
    uint64_t base;
    uint64_t reg = 0;

    if (!s || !value || !ioa_access_valid(offset, size)) {
        return false;
    }

    *value = 0;
    base = offset & ~UINT64_C(7);
    if (ioa_sapic_direct(base)) {
        return ioa_sapic_read(s, offset, size, value);
    }
    if (base == HP_ZX1_IOA_CONFIG_DATA) {
        return ioa_config_data_read(s, offset, size, value);
    }

    switch (base) {
    case HP_ZX1_IOA_FUNCTION_ID:
        reg = HP_ZX1_IOA_VENDOR_ID |
              ((uint64_t)HP_ZX1_IOA_DEVICE_ID << 16) |
              ((uint64_t)s->pci_command << 32) |
              ((uint64_t)s->pci_status << 48);
        break;
    case HP_ZX1_IOA_FUNCTION_CLASS:
        reg = HP_ZX1_IOA_REVISION |
              ((uint64_t)HP_ZX1_IOA_CLASS_CODE << 8) |
              ((uint64_t)s->cache_line_size << 32) |
              ((uint64_t)s->latency_timer << 40);
        break;
    case HP_ZX1_IOA_CAPABILITIES_POINTER:
        reg = (uint64_t)(s->reset_config.mode == HP_ZX1_IOA_MODE_AGP ?
                         0x60 : 0xa0) << 32;
        break;
    case HP_ZX1_IOA_CONFIG_ADDRESS:
        reg = s->config_address;
        break;
    case HP_ZX1_IOA_BUS_NUMBER:
        reg = s->bus_number;
        break;
    case HP_ZX1_IOA_AGP_CAPABILITY:
        if (s->reset_config.mode == HP_ZX1_IOA_MODE_AGP) {
            reg = IOA_AGP_CAPABILITY_RESET;
        }
        break;
    case HP_ZX1_IOA_AGP_COMMAND:
        if (s->reset_config.mode == HP_ZX1_IOA_MODE_AGP) {
            reg = s->agp_command;
        }
        break;
    case HP_ZX1_IOA_ARBITRATION_MASK:
        reg = s->arbitration_mask & IOA_ARBITRATION_MASK_WRITE;
        break;
    case HP_ZX1_IOA_PCIX_CAPABILITY:
        if (s->reset_config.mode != HP_ZX1_IOA_MODE_AGP) {
            reg = IOA_PCIX_CAPABILITY_ID |
                  ((uint64_t)s->pcix_command << 16) |
                  ((uint64_t)s->pcix_status << 32);
        }
        break;
    case HP_ZX1_IOA_STATUS_CONTROL:
        reg = s->status_control & IOA_SIC_LATCH_MASK;
        if (s->pci_reset_asserted) {
            reg |= HP_ZX1_IOA_SIC_RESET_COMPLETE;
        }
        break;
    case HP_ZX1_IOA_LMMIO_BASE:
        reg = s->lmmio_base;
        break;
    case HP_ZX1_IOA_LMMIO_MASK:
        reg = s->lmmio_mask;
        break;
    case HP_ZX1_IOA_GMMIO_BASE:
        reg = s->gmmio_base;
        break;
    case HP_ZX1_IOA_GMMIO_MASK:
        reg = s->gmmio_mask;
        break;
    case HP_ZX1_IOA_WLMMIO_BASE:
        reg = s->wlmmio_base;
        break;
    case HP_ZX1_IOA_WLMMIO_MASK:
        reg = s->wlmmio_mask;
        break;
    case HP_ZX1_IOA_WGMMIO_BASE:
        reg = s->wgmmio_base;
        break;
    case HP_ZX1_IOA_WGMMIO_MASK:
        reg = s->wgmmio_mask;
        break;
    case HP_ZX1_IOA_ELMMIO_BASE:
        reg = s->elmmio_base;
        break;
    case HP_ZX1_IOA_ELMMIO_MASK:
        reg = s->elmmio_mask;
        break;
    case HP_ZX1_IOA_SLAVE_CONTROL:
        reg = s->slave_control & HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE;
        break;
    case HP_ZX1_IOA_MSI_BASE:
        reg = s->msi_base;
        break;
    case HP_ZX1_IOA_MSI_MASK:
        reg = s->msi_mask;
        break;
    case HP_ZX1_IOA_BUS_MODE:
        reg = s->bus_mode & HP_ZX1_IOA_BUS_MODE_VISIBLE;
        break;
    case HP_ZX1_IOA_ERROR_CONFIGURATION:
        reg = s->error_configuration & IOA_ERROR_CONFIG_SMART;
        break;
    case HP_ZX1_IOA_ERROR_STATUS:
        reg = s->error_status;
        break;
    case HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS:
        reg = s->outbound_error_address;
        break;
    default:
        /* Reserved and unimplemented registers read as zero. */
        reg = 0;
        break;
    }

    *value = ioa_extract(offset, size, reg);
    return true;
}

bool hp_zx1_ioa_regs_write(HPZX1IOARegs *s, uint64_t offset,
                           unsigned int size, uint64_t value)
{
    uint64_t base;
    uint64_t mask;
    uint64_t data;
    uint64_t latch;
    uint64_t writable;
    uint16_t field_mask;
    bool old_clear_enable;
    bool reset;

    if (!s || !ioa_access_valid(offset, size)) {
        return false;
    }

    base = offset & ~UINT64_C(7);
    if (ioa_sapic_direct(base)) {
        return ioa_sapic_write(s, offset, size, value);
    }
    if (base == HP_ZX1_IOA_CONFIG_DATA) {
        return ioa_config_data_write(s, offset, size, value);
    }

    mask = ioa_access_mask(offset, size);
    data = ioa_access_data(offset, size, value);
    switch (base) {
    case HP_ZX1_IOA_FUNCTION_ID:
        field_mask = (mask >> 32) & HP_ZX1_IOA_PCI_COMMAND_MASK;
        s->pci_command = (s->pci_command & ~field_mask) |
                         ((data >> 32) & field_mask);
        s->pci_status &= ~((data >> 48) & (mask >> 48) &
                           HP_ZX1_IOA_PCI_STATUS_W1C);
        break;
    case HP_ZX1_IOA_FUNCTION_CLASS:
        latch = ((uint64_t)s->cache_line_size << 32) |
                ((uint64_t)s->latency_timer << 40);
        ioa_latch_write(&latch, UINT64_C(0x0000ffff00000000), mask, data);
        s->cache_line_size = latch >> 32;
        s->latency_timer = latch >> 40;
        break;
    case HP_ZX1_IOA_CONFIG_ADDRESS:
        latch = s->config_address;
        ioa_latch_write(&latch, HP_ZX1_IOA_CONFIG_ADDRESS_MASK, mask, data);
        s->config_address = latch & HP_ZX1_IOA_CONFIG_ADDRESS_MASK;
        break;
    case HP_ZX1_IOA_BUS_NUMBER:
        latch = s->bus_number;
        ioa_latch_write(&latch, UINT16_MAX, mask, data);
        s->bus_number = latch;
        break;
    case HP_ZX1_IOA_AGP_COMMAND:
        if (s->reset_config.mode == HP_ZX1_IOA_MODE_AGP) {
            latch = s->agp_command;
            ioa_latch_write(&latch, IOA_AGP_COMMAND_WRITABLE, mask, data);
            s->agp_command = latch;
        }
        break;
    case HP_ZX1_IOA_ARBITRATION_MASK:
        latch = s->arbitration_mask;
        writable = IOA_ARBITRATION_MASK_WRITE;
        if (!(s->bus_mode & HP_ZX1_IOA_BUS_MODE_SIX_MASTERS)) {
            writable &= ~IOA_ARBITRATION_MASK_F;
        }
        ioa_latch_write(&latch, writable, mask, data);
        s->arbitration_mask = latch;
        break;
    case HP_ZX1_IOA_PCIX_CAPABILITY:
        if (s->reset_config.mode != HP_ZX1_IOA_MODE_AGP) {
            field_mask = (mask >> 16) & IOA_PCIX_COMMAND_WRITABLE;
            s->pcix_command = (s->pcix_command & ~field_mask) |
                              ((data >> 16) & field_mask);
            s->pcix_status &= ~((data >> 32) & (mask >> 32) &
                                HP_ZX1_IOA_PCIX_STATUS_W1C);
        }
        break;
    case HP_ZX1_IOA_STATUS_CONTROL:
        latch = s->status_control;
        old_clear_enable = latch & HP_ZX1_IOA_SIC_CLEAR_ENABLE;
        ioa_latch_write(&latch, IOA_SIC_LATCH_MASK &
                        ~HP_ZX1_IOA_SIC_CLEAR_LOG, mask, data);
        if (mask & HP_ZX1_IOA_SIC_CLEAR_LOG) {
            /* A CE=CL=1 write clears CL and latches CE. */
            if ((data & HP_ZX1_IOA_SIC_CLEAR_LOG) &&
                !(data & HP_ZX1_IOA_SIC_CLEAR_ENABLE) &&
                old_clear_enable) {
                latch |= HP_ZX1_IOA_SIC_CLEAR_LOG;
            } else {
                latch &= ~HP_ZX1_IOA_SIC_CLEAR_LOG;
            }
        }
        s->status_control = latch;
        if ((mask & HP_ZX1_IOA_SIC_CLEAR_LOG) &&
            (latch & HP_ZX1_IOA_SIC_CLEAR_LOG)) {
            s->error_status = 0;
            s->outbound_error_address = 0;
        }
        if (mask & HP_ZX1_IOA_SIC_RESET_FUNCTION) {
            reset = data & HP_ZX1_IOA_SIC_RESET_FUNCTION;
            if (reset) {
                ioa_function_reset(s);
            }
            s->pci_reset_asserted = reset;
        }
        break;
    case HP_ZX1_IOA_LMMIO_BASE:
        ioa_latch_write(&s->lmmio_base, HP_ZX1_IOA_LMMIO_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_LMMIO_MASK:
        ioa_latch_write(&s->lmmio_mask, HP_ZX1_IOA_LMMIO_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_GMMIO_BASE:
        ioa_latch_write(&s->gmmio_base, HP_ZX1_IOA_GMMIO_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_GMMIO_MASK:
        ioa_latch_write(&s->gmmio_mask, HP_ZX1_IOA_GMMIO_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_WLMMIO_BASE:
        ioa_latch_write(&s->wlmmio_base, HP_ZX1_IOA_WLMMIO_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_WLMMIO_MASK:
        ioa_latch_write(&s->wlmmio_mask, HP_ZX1_IOA_WLMMIO_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_WGMMIO_BASE:
        ioa_latch_write(&s->wgmmio_base, HP_ZX1_IOA_WGMMIO_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_WGMMIO_MASK:
        ioa_latch_write(&s->wgmmio_mask, HP_ZX1_IOA_WGMMIO_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_ELMMIO_BASE:
        ioa_latch_write(&s->elmmio_base, HP_ZX1_IOA_ELMMIO_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_ELMMIO_MASK:
        ioa_latch_write(&s->elmmio_mask, HP_ZX1_IOA_ELMMIO_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_SLAVE_CONTROL:
        ioa_latch_write(&s->slave_control,
                        HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE, mask, data);
        break;
    case HP_ZX1_IOA_MSI_BASE:
        ioa_latch_write(&s->msi_base, HP_ZX1_IOA_MSI_BASE_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_MSI_MASK:
        ioa_latch_write(&s->msi_mask, HP_ZX1_IOA_MSI_MASK_WRITABLE,
                        mask, data);
        break;
    case HP_ZX1_IOA_BUS_MODE:
        ioa_latch_write(&s->bus_mode, HP_ZX1_IOA_BUS_MODE_SAFE_WRITE,
                        mask, data);
        break;
    case HP_ZX1_IOA_ERROR_CONFIGURATION:
        latch = s->error_configuration;
        ioa_latch_write(&latch, IOA_ERROR_CONFIG_SMART, mask, data);
        s->error_configuration = latch;
        break;
    default:
        /* Read-only, reserved, and unmodeled registers discard writes. */
        break;
    }
    return true;
}

bool hp_zx1_ioa_regs_set_input(HPZX1IOARegs *s, unsigned int input,
                               bool asserted)
{
    uint32_t bit;
    uint32_t low;
    bool old_asserted;

    if (!s || input >= ioa_external_inputs(s)) {
        return false;
    }

    bit = 1U << input;
    old_asserted = s->sapic_asserted & bit;
    if (asserted) {
        s->sapic_asserted |= bit;
    } else {
        s->sapic_asserted &= ~bit;
    }
    if (old_asserted == asserted || !asserted) {
        return false;
    }

    low = s->sapic_regs[ioa_rte_low(input)];
    if (low & HP_IO_SAPIC_RTE_MASK) {
        return false;
    }
    if (low & HP_IO_SAPIC_RTE_TRIGGER) {
        return ioa_reevaluate_level(s, input);
    }

    /* Masked edges are not queued for delivery after unmask. */
    return ioa_deliver_entry(s, input, false);
}

void hp_zx1_ioa_regs_set_pci_status(HPZX1IOARegs *s, uint16_t status)
{
    if (s) {
        s->pci_status |= status & HP_ZX1_IOA_PCI_STATUS_W1C;
    }
}

void hp_zx1_ioa_regs_set_pcix_status(HPZX1IOARegs *s, uint32_t status)
{
    if (s) {
        s->pcix_status |= status & HP_ZX1_IOA_PCIX_STATUS_W1C;
    }
}

bool hp_zx1_ioa_regs_msi_contains(const HPZX1IOARegs *s, uint64_t address)
{
    uint64_t base;
    uint64_t size;

    /* Addresses outside the 44-bit zx1 address set never match. */
    if ((address & ~IOA_44BIT_ADDRESS_MASK) ||
        !hp_zx1_ioa_regs_msi_range(s, &base, &size)) {
        return false;
    }
    return address >= base && address - base < size;
}

bool hp_zx1_ioa_regs_msi_range(const HPZX1IOARegs *s, uint64_t *base,
                               uint64_t *size)
{
    const uint64_t register_address_mask =
        HP_ZX1_IOA_MSI_BASE_WRITABLE & ~UINT64_C(1);
    uint64_t address;
    uint64_t inverse;
    uint64_t mask;
    uint64_t range_size;

    if (!s || !base || !size || !(s->msi_base & 1)) {
        return false;
    }

    address = s->msi_base & register_address_mask;
    mask = s->msi_mask & register_address_mask;
    inverse = (~mask) & IOA_44BIT_ADDRESS_MASK;
    range_size = inverse + 1;

    /*
     * A valid complement is one contiguous run of low bits.  The base must
     * be naturally aligned, and MSI Base[43:31] may not be all zero.
     */
    if ((inverse & range_size) || (address & inverse) ||
        !(address & IOA_MSI_ABOVE_2G_MASK)) {
        return false;
    }

    *base = address;
    *size = range_size;
    return true;
}

unsigned int hp_zx1_ioa_regs_root_count(const HPZX1IOARegs *s)
{
    return s ? HP_ZX1_IOA_ROOT_COUNT : 0;
}

uint8_t hp_zx1_ioa_regs_rope_mask(const HPZX1IOARegs *s)
{
    return s ? s->reset_config.rope_mask : 0;
}
