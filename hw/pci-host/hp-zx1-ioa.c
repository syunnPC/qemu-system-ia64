/*
 * HP zx1 Mercury I/O adapter QOM wrapper
 *
 * Board supplies placement, rope topology, PCI identity, and interrupt
 * routing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_device.h"
#include "hw/pci-host/hp-zx1-ioa.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_HP_ZX1_IOA_BUS TYPE_HP_ZX1_IOA ".bus"
OBJECT_DECLARE_SIMPLE_TYPE(HPZX1IOABus, HP_ZX1_IOA_BUS)

#define IOA_AGP_COMMAND_WRITABLE       UINT32_C(0x00000337)
#define IOA_PCIX_COMMAND_WRITABLE      UINT16_C(0x0001)
#define IOA_PCIX_STATUS_RESET          UINT32_C(0x0013ff00)
#define IOA_ARBITRATION_MASK_WRITABLE  UINT32_C(0x0000007f)
#define IOA_ARBITRATION_MASK_F         UINT32_C(0x00000040)
#define IOA_ERROR_CONFIG_SMART         UINT32_C(0x00000020)
#define IOA_AGP_EXTERNAL_INPUTS         7
#define IOA_PCI_IO_SIZE                UINT64_C(0x10000)
#define IOA_MSI_ADDRESS_SPACE_SIZE     (UINT64_C(1) << 44)

#define IOA_SIC_LATCH_MASK (HP_ZX1_IOA_SIC_FORWARD_VGA | \
                            HP_ZX1_IOA_SIC_CLEAR_LOG | \
                            HP_ZX1_IOA_SIC_CLEAR_ENABLE | \
                            HP_ZX1_IOA_SIC_HARD_FAIL)

#define IOA_SAPIC_RTE_LOW_PERSISTENT (HP_IO_SAPIC_RTE_VECTOR | \
                                      HP_IO_SAPIC_RTE_DELIVERY | \
                                      HP_IO_SAPIC_RTE_POLARITY | \
                                      HP_IO_SAPIC_RTE_TRIGGER | \
                                      HP_IO_SAPIC_RTE_MASK)
#define IOA_SAPIC_RTE_HIGH_PERSISTENT UINT32_C(0xffff0000)

typedef struct HPZX1IOARoute {
    uint32_t packed;
} HPZX1IOARoute;

typedef struct HPZX1IOABaseline {
    uint8_t mode;
    uint8_t rope_mask;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t pci_reset_asserted;
    uint64_t bus_mode_reset;
    uint64_t slave_control_reset_straps;
    uint32_t error_configuration_reset_straps;
    HPZX1IOARoute intx_route[PCI_SLOT_MAX];
} HPZX1IOABaseline;

struct HPZX1IOABus {
    PCIBus parent_obj;

    uint8_t first_bus;
};

struct HPZX1IOAState {
    PCIHostState parent_obj;

    MemoryRegion csr;
    MemoryRegion pci_mem;
    MemoryRegion pci_io;
    MemoryRegion msi;
    MemoryRegion *msi_dma_root;
    uint64_t msi_window_base;
    HPZX1IOARegs regs;

    HPZX1IOABaseline baseline;
    HPIOSAPICDeliver deliver;
    void *delivery_opaque;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
    bool setup_done;
    char root_bus_name[8];
    char root_bus_path[8];
};

static unsigned int hp_zx1_ioa_external_inputs(const HPZX1IOAState *s)
{
    return s->baseline.mode == HP_ZX1_IOA_MODE_AGP ?
           IOA_AGP_EXTERNAL_INPUTS :
           HP_ZX1_IOA_EXTERNAL_INPUTS;
}

static HPZX1IOARegsConfig hp_zx1_ioa_regs_config(HPZX1IOAState *s);
static void hp_zx1_ioa_deliver(void *opaque,
                               const HPIOSAPICMessage *message);

static void hp_zx1_ioa_update_msi_window(HPZX1IOAState *s)
{
    uint64_t base;
    uint64_t size;
    bool valid = hp_zx1_ioa_regs_msi_range(&s->regs, &base, &size);

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->msi, false);
    if (valid) {
        s->msi_window_base = base;
        memory_region_set_address(&s->msi, base);
        memory_region_set_size(&s->msi, size);
        memory_region_set_enabled(&s->msi, s->msi_dma_root != NULL);
    } else {
        s->msi_window_base = 0;
    }
    memory_region_transaction_commit();
}

static MemTxResult hp_zx1_ioa_msi_read(void *opaque, hwaddr addr,
                                       uint64_t *data, unsigned int size,
                                       MemTxAttrs attrs)
{
    HPZX1IOAState *s = opaque;

    *data = 0;
    hp_zx1_ioa_regs_report_fault(&s->regs, HP_ZX1_IOA_FAULT_MSI_DECODE,
                                 s->msi_window_base + addr, 0);
    return MEMTX_DECODE_ERROR;
}

static MemTxResult hp_zx1_ioa_msi_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned int size,
                                        MemTxAttrs attrs)
{
    HPZX1IOAState *s = opaque;
    HPIOSAPICMessage message;
    uint64_t address;

    if (size != 4 || (addr & 3) || !s->msi_dma_root ||
        addr > UINT64_MAX - s->msi_window_base) {
        hp_zx1_ioa_regs_report_fault(&s->regs,
                                     HP_ZX1_IOA_FAULT_MSI_DECODE,
                                     s->msi_window_base + addr, value);
        return MEMTX_DECODE_ERROR;
    }

    address = s->msi_window_base + addr;
    if (!hp_zx1_ioa_regs_msi_contains(&s->regs, address)) {
        hp_zx1_ioa_regs_report_fault(&s->regs,
                                     HP_ZX1_IOA_FAULT_MSI_DECODE,
                                     address, value);
        return MEMTX_DECODE_ERROR;
    }

    message = (HPIOSAPICMessage) {
        .address = address,
        .data = (uint32_t)value,
    };
    hp_zx1_ioa_deliver(s, &message);
    return MEMTX_OK;
}

static const MemoryRegionOps hp_zx1_ioa_msi_ops = {
    .read_with_attrs = hp_zx1_ioa_msi_read,
    .write_with_attrs = hp_zx1_ioa_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static bool hp_zx1_ioa_config_address(HPZX1IOAState *s, uint32_t selector,
                                      uint32_t *address)
{
    uint32_t bus = extract32(selector, 16, 8);
    uint8_t first_bus = s->baseline.secondary_bus;

    if (bus == 0) {
        *address = deposit32(selector, 16, 8, first_bus);
        return true;
    }

    /* Map type-0 bus 0 to @first_bus and reject its type-1 alias. */
    if (first_bus && bus == first_bus) {
        return false;
    }

    *address = selector;
    return true;
}

static bool hp_zx1_ioa_config_read(void *opaque, uint32_t address,
                                   unsigned int size, uint32_t *value)
{
    HPZX1IOAState *s = opaque;
    PCIBus *bus = PCI_HOST_BRIDGE(s)->bus;
    uint32_t translated;

    if (!bus || !value || (size != 1 && size != 2 && size != 4) ||
        !hp_zx1_ioa_config_address(s, address, &translated)) {
        return false;
    }
    if (!pci_find_device(bus, extract32(translated, 16, 8),
                         extract32(translated, 8, 8))) {
        return false;
    }

    *value = pci_data_read(bus, translated, size);
    return true;
}

static bool hp_zx1_ioa_config_write(void *opaque, uint32_t address,
                                    unsigned int size, uint32_t value)
{
    HPZX1IOAState *s = opaque;
    PCIBus *bus = PCI_HOST_BRIDGE(s)->bus;
    uint32_t translated;

    if (!bus || (size != 1 && size != 2 && size != 4) ||
        !hp_zx1_ioa_config_address(s, address, &translated)) {
        return false;
    }
    if (!pci_find_device(bus, extract32(translated, 16, 8),
                         extract32(translated, 8, 8))) {
        return false;
    }

    pci_data_write(bus, translated, value, size);
    return true;
}

static void hp_zx1_ioa_deliver(void *opaque,
                               const HPIOSAPICMessage *message)
{
    HPZX1IOAState *s = opaque;

    s->deliver(s->delivery_opaque, message);
}

static HPZX1IOARegsConfig hp_zx1_ioa_regs_config(HPZX1IOAState *s)
{
    return (HPZX1IOARegsConfig) {
        .mode = s->baseline.mode,
        .rope_mask = s->baseline.rope_mask,
        .secondary_bus = s->baseline.secondary_bus,
        .subordinate_bus = s->baseline.subordinate_bus,
        .pci_reset_asserted = s->baseline.pci_reset_asserted,
        .bus_mode_reset = s->baseline.bus_mode_reset,
        .slave_control_reset_straps =
            s->baseline.slave_control_reset_straps,
        .error_configuration_reset_straps =
            s->baseline.error_configuration_reset_straps,
        .config_read = hp_zx1_ioa_config_read,
        .config_write = hp_zx1_ioa_config_write,
        .config_opaque = s,
        .deliver = hp_zx1_ioa_deliver,
        .delivery_opaque = s,
        .fault_notify = s->fault_notify,
        .fault_opaque = s->fault_opaque,
    };
}

bool hp_zx1_ioa_setup(HPZX1IOAState *s, const HPZX1IOASetup *setup,
                      Error **errp)
{
    HPZX1IOARegsConfig config;
    HPZX1IOABaseline baseline;
    unsigned int inputs;
    unsigned int pin;
    unsigned int slot;

    if (!s || !setup) {
        error_setg(errp, "Mercury setup requires a device and configuration");
        return false;
    }
    if (qdev_is_realized(DEVICE(s))) {
        error_setg(errp, "Mercury setup must precede device realization");
        return false;
    }
    if (s->setup_done) {
        error_setg(errp, "Mercury setup may be performed only once");
        return false;
    }
    if (!setup->deliver) {
        error_setg(errp,
                   "Mercury setup requires an interrupt delivery callback");
        return false;
    }
    if ((unsigned int)setup->mode > HP_ZX1_IOA_MODE_AGP) {
        error_setg(errp, "invalid Mercury operating mode");
        return false;
    }
    baseline = (HPZX1IOABaseline) {
        .mode = setup->mode,
        .rope_mask = setup->rope_mask,
        .secondary_bus = setup->secondary_bus,
        .subordinate_bus = setup->subordinate_bus,
        .pci_reset_asserted = setup->pci_reset_asserted,
        .bus_mode_reset = setup->bus_mode_reset,
        .slave_control_reset_straps =
            setup->slave_control_reset_straps,
        .error_configuration_reset_straps =
            setup->error_configuration_reset_straps,
    };
    inputs = setup->mode == HP_ZX1_IOA_MODE_AGP ?
             IOA_AGP_EXTERNAL_INPUTS :
             HP_ZX1_IOA_EXTERNAL_INPUTS;
    for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
        uint32_t packed = 0;

        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            uint8_t input = setup->intx_route[slot][pin];

            if (input >= inputs) {
                error_setg(errp,
                           "Mercury INTx route slot %u pin %u uses input "
                           "%u (mode limit %u)", slot, pin, input,
                           inputs - 1);
                return false;
            }
            packed |= (uint32_t)input << (pin * 8);
        }
        baseline.intx_route[slot].packed = packed;
    }

    s->baseline = baseline;
    s->deliver = setup->deliver;
    s->delivery_opaque = setup->delivery_opaque;
    s->fault_notify = setup->fault_notify;
    s->fault_opaque = setup->fault_opaque;
    config = hp_zx1_ioa_regs_config(s);
    if (!hp_zx1_ioa_regs_init(&s->regs, &config)) {
        memset(&s->baseline, 0, sizeof(s->baseline));
        s->deliver = NULL;
        s->delivery_opaque = NULL;
        s->fault_notify = NULL;
        s->fault_opaque = NULL;
        error_setg(errp, "invalid Mercury mode, rope, bus, or reset straps");
        return false;
    }

    s->setup_done = true;
    PCI_HOST_BRIDGE(s)->config_reg = s->regs.config_address;
    return true;
}

static MemTxResult hp_zx1_ioa_read(void *opaque, hwaddr addr,
                                   uint64_t *data, unsigned int size,
                                   MemTxAttrs attrs)
{
    HPZX1IOAState *s = opaque;

    if (!hp_zx1_ioa_regs_read(&s->regs, addr, size, data)) {
        *data = 0;
        hp_zx1_ioa_regs_report_fault(&s->regs,
                                     HP_ZX1_IOA_FAULT_CSR_DECODE,
                                     addr, 0);
        return MEMTX_DECODE_ERROR;
    }
    return MEMTX_OK;
}

static MemTxResult hp_zx1_ioa_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned int size,
                                    MemTxAttrs attrs)
{
    HPZX1IOAState *s = opaque;

    if (!hp_zx1_ioa_regs_write(&s->regs, addr, size, value)) {
        hp_zx1_ioa_regs_report_fault(&s->regs,
                                     HP_ZX1_IOA_FAULT_CSR_DECODE,
                                     addr, value);
        return MEMTX_DECODE_ERROR;
    }
    PCI_HOST_BRIDGE(s)->config_reg = s->regs.config_address;
    if ((addr & ~UINT64_C(7)) == HP_ZX1_IOA_MSI_BASE ||
        (addr & ~UINT64_C(7)) == HP_ZX1_IOA_MSI_MASK) {
        hp_zx1_ioa_update_msi_window(s);
    }

    /* RF assertion resets the subordinate PCI hierarchy, not the wrapper. */
    if (addr == HP_ZX1_IOA_STATUS_CONTROL &&
        (value & HP_ZX1_IOA_SIC_RESET_FUNCTION) &&
        PCI_HOST_BRIDGE(s)->bus) {
        bus_cold_reset(BUS(PCI_HOST_BRIDGE(s)->bus));
    }
    return MEMTX_OK;
}

static const MemoryRegionOps hp_zx1_ioa_ops = {
    .read_with_attrs = hp_zx1_ioa_read,
    .write_with_attrs = hp_zx1_ioa_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void hp_zx1_ioa_set_irq(void *opaque, int input, int level)
{
    HPZX1IOAState *s = opaque;

    /* The named bank stays at ten pins; the AGP core rejects inputs 7..9. */
    if (input >= 0 && input < HP_ZX1_IOA_EXTERNAL_INPUTS) {
        hp_zx1_ioa_regs_set_input(&s->regs, input, level != 0);
    }
}

static int hp_zx1_ioa_map_irq(PCIDevice *pdev, int pin)
{
    PCIBus *root = pci_device_root_bus(pdev);
    HPZX1IOAState *s = HP_ZX1_IOA(BUS(root)->parent);
    unsigned int slot = PCI_SLOT(pdev->devfn);

    g_assert(pin >= 0 && pin < PCI_NUM_PINS);

    /*
     * Direct devices use their root slot and INTA..INTD.  PCI bridges apply
     * their own swizzle before this root map is consulted.
     */
    return extract32(s->baseline.intx_route[slot].packed, pin * 8, 8);
}

static int hp_zx1_ioa_bus_num(PCIBus *bus)
{
    return HP_ZX1_IOA_BUS(bus)->first_bus;
}

static void hp_zx1_ioa_bus_class_init(ObjectClass *klass, const void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(klass);

    pbc->bus_num = hp_zx1_ioa_bus_num;
}

static const TypeInfo hp_zx1_ioa_bus_info = {
    .name = TYPE_HP_ZX1_IOA_BUS,
    .parent = TYPE_PCI_BUS,
    .instance_size = sizeof(HPZX1IOABus),
    .class_init = hp_zx1_ioa_bus_class_init,
};

static void hp_zx1_ioa_reset_hold(Object *obj, ResetType type)
{
    HPZX1IOAState *s = HP_ZX1_IOA(obj);

    if (s->setup_done) {
        hp_zx1_ioa_regs_reset(&s->regs);
        PCI_HOST_BRIDGE(s)->config_reg = s->regs.config_address;
        hp_zx1_ioa_update_msi_window(s);
    }
}

static bool hp_zx1_ioa_latch_valid(uint64_t value, uint64_t writable,
                                   uint64_t fixed, const char *name,
                                   Error **errp)
{
    if ((value & ~(writable | fixed)) || (value & fixed) != fixed) {
        error_setg(errp, "Mercury migration has invalid %s 0x%016" PRIx64,
                   name, value);
        return false;
    }
    return true;
}

static bool hp_zx1_ioa_sapic_valid(const HPZX1IOAState *s, Error **errp)
{
    const HPZX1IOARegs *regs = &s->regs;
    unsigned int inputs = hp_zx1_ioa_external_inputs(s);
    uint32_t input_mask = MAKE_64BIT_MASK(0, inputs);
    unsigned int entry;
    unsigned int index;

    if (regs->sapic_selector > UINT8_MAX ||
        (regs->sapic_asserted & ~input_mask) ||
        (regs->sapic_in_service & ~input_mask)) {
        error_setg(errp, "Mercury migration has invalid I/O SAPIC state");
        return false;
    }

    for (index = 0; index < HP_IO_SAPIC_RTE_BASE; index++) {
        if (regs->sapic_regs[index]) {
            error_setg(errp,
                       "Mercury migration has data in read-only SAPIC "
                       "register %u", index);
            return false;
        }
    }

    for (entry = 0; entry < hp_io_sapic_zx1_policy.entry_count; entry++) {
        uint32_t bit = 1U << entry;
        uint64_t low = regs->sapic_regs[HP_IO_SAPIC_RTE_BASE + 2 * entry];
        uint64_t high =
            regs->sapic_regs[HP_IO_SAPIC_RTE_BASE + 2 * entry + 1];

        if (low & ~(uint64_t)IOA_SAPIC_RTE_LOW_PERSISTENT) {
            error_setg(errp,
                       "Mercury migration has invalid SAPIC RTE %u low "
                       "0x%016" PRIx64, entry, low);
            return false;
        }
        if (high & ~(uint64_t)IOA_SAPIC_RTE_HIGH_PERSISTENT) {
            error_setg(errp,
                       "Mercury migration has invalid SAPIC RTE %u high "
                       "0x%016" PRIx64, entry, high);
            return false;
        }
        if ((regs->sapic_in_service & bit) &&
            !(low & HP_IO_SAPIC_RTE_TRIGGER)) {
            error_setg(errp,
                       "Mercury migration has edge RTE %u in service",
                       entry);
            return false;
        }
    }
    return true;
}

static bool hp_zx1_ioa_post_load(void *opaque, int version_id, Error **errp)
{
    HPZX1IOAState *s = opaque;
    HPZX1IOARegs *regs = &s->regs;
    PCIBus *bus = PCI_HOST_BRIDGE(s)->bus;
    HPZX1IOARegsConfig config;
    uint16_t pci_status_allowed = HP_ZX1_IOA_PCI_STATUS_RESET |
                                  HP_ZX1_IOA_PCI_STATUS_W1C;
    uint32_t pcix_status_allowed = IOA_PCIX_STATUS_RESET |
                                   HP_ZX1_IOA_PCIX_STATUS_W1C;

    if (version_id < 1 || version_id > 2 || !s->setup_done || !bus ||
        !s->deliver ||
        pci_bus_num(bus) != s->baseline.secondary_bus ||
        bus->nirq != hp_zx1_ioa_external_inputs(s)) {
        error_setg(errp, "Mercury migration destination is not configured");
        return false;
    }
    if ((regs->pci_command & ~HP_ZX1_IOA_PCI_COMMAND_MASK) ||
        (regs->pci_status & ~pci_status_allowed) ||
        (regs->pci_status & HP_ZX1_IOA_PCI_STATUS_RESET) !=
            HP_ZX1_IOA_PCI_STATUS_RESET ||
        (regs->config_address & ~HP_ZX1_IOA_CONFIG_ADDRESS_MASK)) {
        error_setg(errp, "Mercury migration has invalid PCI state");
        return false;
    }
    if ((regs->agp_command & ~IOA_AGP_COMMAND_WRITABLE) ||
        (s->baseline.mode != HP_ZX1_IOA_MODE_AGP && regs->agp_command) ||
        (regs->pcix_command & ~IOA_PCIX_COMMAND_WRITABLE) ||
        (s->baseline.mode == HP_ZX1_IOA_MODE_AGP && regs->pcix_command) ||
        (regs->pcix_status & ~pcix_status_allowed) ||
        (regs->pcix_status & IOA_PCIX_STATUS_RESET) !=
            IOA_PCIX_STATUS_RESET) {
        error_setg(errp, "Mercury migration has invalid AGP/PCI-X state");
        return false;
    }
    if ((regs->arbitration_mask & ~IOA_ARBITRATION_MASK_WRITABLE) ||
        (!(s->baseline.bus_mode_reset &
           HP_ZX1_IOA_BUS_MODE_SIX_MASTERS) &&
         (regs->arbitration_mask & IOA_ARBITRATION_MASK_F)) ||
        (regs->status_control & ~IOA_SIC_LATCH_MASK) ||
        ((regs->status_control & HP_ZX1_IOA_SIC_CLEAR_LOG) &&
         (regs->status_control & HP_ZX1_IOA_SIC_CLEAR_ENABLE))) {
        error_setg(errp, "Mercury migration has invalid control state");
        return false;
    }

    if (!hp_zx1_ioa_latch_valid(regs->lmmio_base,
                                HP_ZX1_IOA_LMMIO_BASE_WRITABLE,
                                HP_ZX1_IOA_LMMIO_BASE_RESET,
                                "LMMIO base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->lmmio_mask,
                                HP_ZX1_IOA_LMMIO_MASK_WRITABLE,
                                HP_ZX1_IOA_LMMIO_MASK_RESET,
                                "LMMIO mask", errp) ||
        !hp_zx1_ioa_latch_valid(regs->gmmio_base,
                                HP_ZX1_IOA_GMMIO_BASE_WRITABLE, 0,
                                "GMMIO base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->gmmio_mask,
                                HP_ZX1_IOA_GMMIO_MASK_WRITABLE, 0,
                                "GMMIO mask", errp) ||
        !hp_zx1_ioa_latch_valid(regs->wlmmio_base,
                                HP_ZX1_IOA_WLMMIO_BASE_WRITABLE,
                                HP_ZX1_IOA_WLMMIO_BASE_RESET,
                                "WLMMIO base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->wlmmio_mask,
                                HP_ZX1_IOA_WLMMIO_MASK_WRITABLE,
                                HP_ZX1_IOA_WLMMIO_MASK_RESET,
                                "WLMMIO mask", errp) ||
        !hp_zx1_ioa_latch_valid(regs->wgmmio_base,
                                HP_ZX1_IOA_WGMMIO_BASE_WRITABLE, 0,
                                "WGMMIO base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->wgmmio_mask,
                                HP_ZX1_IOA_WGMMIO_MASK_WRITABLE, 0,
                                "WGMMIO mask", errp) ||
        !hp_zx1_ioa_latch_valid(regs->elmmio_base,
                                HP_ZX1_IOA_ELMMIO_BASE_WRITABLE,
                                HP_ZX1_IOA_ELMMIO_BASE_RESET,
                                "ELMMIO base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->elmmio_mask,
                                HP_ZX1_IOA_ELMMIO_MASK_WRITABLE,
                                HP_ZX1_IOA_ELMMIO_MASK_RESET,
                                "ELMMIO mask", errp) ||
        !hp_zx1_ioa_latch_valid(regs->msi_base,
                                HP_ZX1_IOA_MSI_BASE_WRITABLE, 0,
                                "MSI base", errp) ||
        !hp_zx1_ioa_latch_valid(regs->msi_mask,
                                HP_ZX1_IOA_MSI_MASK_WRITABLE, 0,
                                "MSI mask", errp)) {
        return false;
    }

    if (((regs->bus_mode & ~HP_ZX1_IOA_BUS_MODE_SAFE_WRITE) !=
         (s->baseline.bus_mode_reset & ~HP_ZX1_IOA_BUS_MODE_SAFE_WRITE)) ||
        ((regs->slave_control & ~HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE) !=
         s->baseline.slave_control_reset_straps) ||
        ((regs->error_configuration & ~IOA_ERROR_CONFIG_SMART) !=
         s->baseline.error_configuration_reset_straps)) {
        error_setg(errp, "Mercury migration changed immutable reset straps");
        return false;
    }
    if (version_id < 2) {
        regs->error_status = 0;
        regs->outbound_error_address = 0;
    }
    if ((regs->error_status & ~HP_ZX1_IOA_ERROR_STATUS_MASK) ||
        (!(regs->error_status & HP_ZX1_IOA_ERROR_SEVERITY_MASK) &&
         (regs->error_status || regs->outbound_error_address))) {
        error_setg(errp, "Mercury migration has invalid fault state");
        return false;
    }
    if (!hp_zx1_ioa_sapic_valid(s, errp)) {
        return false;
    }

    config = hp_zx1_ioa_regs_config(s);
    regs->reset_config = config;
    PCI_HOST_BRIDGE(s)->config_reg = regs->config_address;
    hp_zx1_ioa_update_msi_window(s);
    return true;
}

static const VMStateDescription vmstate_hp_zx1_ioa_route = {
    .name = TYPE_HP_ZX1_IOA "/intx-route",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(packed, HPZX1IOARoute),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hp_zx1_ioa = {
    .name = TYPE_HP_ZX1_IOA,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load_errp = hp_zx1_ioa_post_load,
    .fields = (const VMStateField[]) {
        /* Reject migration between differently wired board instances. */
        VMSTATE_UINT8_EQUAL(baseline.mode, HPZX1IOAState),
        VMSTATE_UINT8_EQUAL(baseline.rope_mask, HPZX1IOAState),
        VMSTATE_UINT8_EQUAL(baseline.secondary_bus, HPZX1IOAState),
        VMSTATE_UINT8_EQUAL(baseline.subordinate_bus, HPZX1IOAState),
        VMSTATE_UINT8_EQUAL(baseline.pci_reset_asserted, HPZX1IOAState),
        VMSTATE_UINT64_EQUAL(baseline.bus_mode_reset, HPZX1IOAState),
        VMSTATE_UINT64_EQUAL(baseline.slave_control_reset_straps,
                             HPZX1IOAState),
        VMSTATE_UINT32_EQUAL(baseline.error_configuration_reset_straps,
                             HPZX1IOAState),
        VMSTATE_STRUCT_ARRAY(baseline.intx_route, HPZX1IOAState,
                             PCI_SLOT_MAX, 1, vmstate_hp_zx1_ioa_route,
                             HPZX1IOARoute),

        VMSTATE_UINT16(regs.pci_command, HPZX1IOAState),
        VMSTATE_UINT16(regs.pci_status, HPZX1IOAState),
        VMSTATE_UINT8(regs.latency_timer, HPZX1IOAState),
        VMSTATE_UINT8(regs.cache_line_size, HPZX1IOAState),
        VMSTATE_UINT32(regs.config_address, HPZX1IOAState),
        VMSTATE_UINT16(regs.bus_number, HPZX1IOAState),
        VMSTATE_UINT32(regs.agp_command, HPZX1IOAState),
        VMSTATE_UINT16(regs.pcix_command, HPZX1IOAState),
        VMSTATE_UINT32(regs.pcix_status, HPZX1IOAState),
        VMSTATE_UINT32(regs.arbitration_mask, HPZX1IOAState),
        VMSTATE_UINT32(regs.status_control, HPZX1IOAState),
        VMSTATE_BOOL(regs.pci_reset_asserted, HPZX1IOAState),

        VMSTATE_UINT64(regs.lmmio_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.lmmio_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.gmmio_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.gmmio_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.wlmmio_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.wlmmio_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.wgmmio_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.wgmmio_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.elmmio_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.elmmio_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.msi_base, HPZX1IOAState),
        VMSTATE_UINT64(regs.msi_mask, HPZX1IOAState),
        VMSTATE_UINT64(regs.bus_mode, HPZX1IOAState),
        VMSTATE_UINT64(regs.slave_control, HPZX1IOAState),
        VMSTATE_UINT32(regs.error_configuration, HPZX1IOAState),
        VMSTATE_UINT64_V(regs.error_status, HPZX1IOAState, 2),
        VMSTATE_UINT64_V(regs.outbound_error_address, HPZX1IOAState, 2),

        VMSTATE_UINT32(regs.sapic_selector, HPZX1IOAState),
        VMSTATE_UINT32(regs.sapic_in_service, HPZX1IOAState),
        VMSTATE_UINT32(regs.sapic_asserted, HPZX1IOAState),
        VMSTATE_UINT64_ARRAY(regs.sapic_regs, HPZX1IOAState,
                             HP_IO_SAPIC_ZX1_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void hp_zx1_ioa_realize(DeviceState *dev, Error **errp)
{
    HPZX1IOAState *s = HP_ZX1_IOA(dev);
    PCIHostState *host = PCI_HOST_BRIDGE(dev);
    PCIBus *bus;

    if (!s->setup_done) {
        error_setg(errp, "Mercury device requires explicit setup");
        return;
    }

    snprintf(s->root_bus_name, sizeof(s->root_bus_name), "pci.%x",
             s->baseline.secondary_bus);
    bus = pci_register_root_bus(dev, s->root_bus_name, hp_zx1_ioa_set_irq,
                                hp_zx1_ioa_map_irq, s, &s->pci_mem,
                                &s->pci_io, PCI_DEVFN(0, 0),
                                hp_zx1_ioa_external_inputs(s),
                                TYPE_HP_ZX1_IOA_BUS);
    HP_ZX1_IOA_BUS(bus)->first_bus = s->baseline.secondary_bus;
    host->bus = bus;
    snprintf(s->root_bus_path, sizeof(s->root_bus_path), "0000:%02x",
             s->baseline.secondary_bus);
    msi_nonbroken = true;
}

static void hp_zx1_ioa_unrealize(DeviceState *dev)
{
    HPZX1IOAState *s = HP_ZX1_IOA(dev);
    PCIHostState *host = PCI_HOST_BRIDGE(dev);

    g_assert(!s->msi_dma_root);
    g_assert(!memory_region_is_mapped(&s->msi));
    if (host->bus) {
        pci_unregister_root_bus(host->bus);
        host->bus = NULL;
    }
}

static const char *hp_zx1_ioa_root_bus_path(PCIHostState *host,
                                            PCIBus *root_bus)
{
    HPZX1IOAState *s = HP_ZX1_IOA(host);

    g_assert(root_bus == host->bus);
    return s->root_bus_path;
}

static void hp_zx1_ioa_init(Object *obj)
{
    HPZX1IOAState *s = HP_ZX1_IOA(obj);

    memory_region_init_io(&s->csr, obj, &hp_zx1_ioa_ops, s,
                          TYPE_HP_ZX1_IOA, HP_ZX1_IOA_CONFIG_APERTURE_SIZE);
    memory_region_init(&s->pci_mem, obj, TYPE_HP_ZX1_IOA ".pci-mem",
                       UINT64_MAX);
    memory_region_init(&s->pci_io, obj, TYPE_HP_ZX1_IOA ".pci-io",
                       IOA_PCI_IO_SIZE);
    memory_region_init_io(&s->msi, obj, &hp_zx1_ioa_msi_ops, s,
                          TYPE_HP_ZX1_IOA ".msi", 1);
    memory_region_set_enabled(&s->msi, false);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->csr);
    qdev_init_gpio_in_named(DEVICE(s), hp_zx1_ioa_set_irq,
                            HP_ZX1_IOA_GPIO_INTX,
                            HP_ZX1_IOA_EXTERNAL_INPUTS);
}

static void hp_zx1_ioa_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "HP zx1 Mercury I/O adapter (internal)";
    dc->realize = hp_zx1_ioa_realize;
    dc->unrealize = hp_zx1_ioa_unrealize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_hp_zx1_ioa;
    hc->root_bus_path = hp_zx1_ioa_root_bus_path;
    rc->phases.hold = hp_zx1_ioa_reset_hold;
}

static const TypeInfo hp_zx1_ioa_info = {
    .name = TYPE_HP_ZX1_IOA,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(HPZX1IOAState),
    .instance_init = hp_zx1_ioa_init,
    .class_init = hp_zx1_ioa_class_init,
};

static void hp_zx1_ioa_register_types(void)
{
    type_register_static(&hp_zx1_ioa_bus_info);
    type_register_static(&hp_zx1_ioa_info);
}

type_init(hp_zx1_ioa_register_types)

bool hp_zx1_ioa_attach_msi_window(HPZX1IOAState *s,
                                  MemoryRegion *dma_root, Error **errp)
{
    if (!s || !dma_root) {
        error_setg(errp,
                   "Mercury MSI attachment requires a device and DMA root");
        return false;
    }
    if (!s->setup_done || !qdev_is_realized(DEVICE(s)) ||
        !PCI_HOST_BRIDGE(s)->bus) {
        error_setg(errp,
                   "Mercury must be configured and realized before MSI "
                   "attachment");
        return false;
    }
    if (s->msi_dma_root || memory_region_is_mapped(&s->msi)) {
        error_setg(errp, "Mercury MSI window is already attached");
        return false;
    }
    if (memory_region_size(dma_root) < IOA_MSI_ADDRESS_SPACE_SIZE) {
        error_setg(errp,
                   "Mercury MSI DMA root does not cover the 44-bit address "
                   "space");
        return false;
    }

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->msi, false);
    memory_region_add_subregion_overlap(dma_root, 0, &s->msi, 1);
    s->msi_dma_root = dma_root;
    hp_zx1_ioa_update_msi_window(s);
    memory_region_transaction_commit();
    return true;
}

void hp_zx1_ioa_detach_msi_window(HPZX1IOAState *s,
                                  MemoryRegion *dma_root)
{
    g_assert(s && dma_root);
    g_assert(s->msi_dma_root == dma_root);
    g_assert(memory_region_is_mapped(&s->msi));

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->msi, false);
    memory_region_del_subregion(dma_root, &s->msi);
    s->msi_dma_root = NULL;
    s->msi_window_base = 0;
    memory_region_transaction_commit();
}

PCIBus *hp_zx1_ioa_bus(HPZX1IOAState *s)
{
    return s ? PCI_HOST_BRIDGE(s)->bus : NULL;
}

MemoryRegion *hp_zx1_ioa_pci_mem(HPZX1IOAState *s)
{
    return s ? &s->pci_mem : NULL;
}

MemoryRegion *hp_zx1_ioa_pci_io(HPZX1IOAState *s)
{
    return s ? &s->pci_io : NULL;
}

uint8_t hp_zx1_ioa_root_bus_num(const HPZX1IOAState *s)
{
    g_assert(s && s->setup_done);
    return s->baseline.secondary_bus;
}

uint8_t hp_zx1_ioa_rope_mask(const HPZX1IOAState *s)
{
    return s && s->setup_done ? s->baseline.rope_mask : 0;
}
