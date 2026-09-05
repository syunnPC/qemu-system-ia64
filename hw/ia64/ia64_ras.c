/*
 * Common IA-64 firmware RAS mailbox and SAL error-record store.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/ia64/ia64_ras.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "target/ia64/cpu.h"

#define IA64_RAS_BANK_COUNT \
    (IA64_RAS_MAX_CPUS * IA64_RAS_RECORD_TYPE_COUNT)
#define IA64_RAS_SLOT_COUNT \
    (IA64_RAS_BANK_COUNT * IA64_RAS_RECORD_DEPTH)

typedef struct QEMU_PACKED IA64SalRecordHeader {
    uint64_t record_id;
    uint8_t revision[2];
    uint8_t severity;
    uint8_t validation;
    uint32_t length;
    uint64_t timestamp;
    uint8_t platform_id[16];
} IA64SalRecordHeader;

typedef struct QEMU_PACKED IA64SalSectionHeader {
    uint8_t guid[16];
    uint8_t revision[2];
    uint8_t recovery;
    uint8_t reserved;
    uint32_t length;
} IA64SalSectionHeader;

typedef struct QEMU_PACKED IA64SalMemoryBody {
    uint64_t validation;
    uint64_t error_status;
    uint64_t address;
    uint64_t address_mask;
    uint16_t node;
    uint16_t card;
    uint16_t module;
    uint16_t bank;
    uint16_t device;
    uint16_t row;
    uint16_t column;
    uint16_t bit_position;
    uint64_t requestor;
    uint64_t responder;
    uint64_t target;
    uint64_t bus_data;
    uint8_t oem_id[16];
} IA64SalMemoryBody;

typedef struct QEMU_PACKED IA64SalPciBusBody {
    uint64_t validation;
    uint64_t error_status;
    uint16_t error_type;
    uint16_t bus_id;
    uint32_t reserved;
    uint64_t address;
    uint64_t data;
    uint64_t command;
    uint64_t requestor;
    uint64_t completer;
    uint64_t target;
    uint8_t oem_id[16];
} IA64SalPciBusBody;

typedef struct QEMU_PACKED IA64SalPCIeBody {
    uint64_t validation;
    uint32_t port_type;
    uint32_t version;
    uint32_t command_status;
    uint32_t reserved0;
    uint8_t device_id[16];
    uint64_t serial_number;
    uint32_t bridge_control_status;
    uint8_t capability[60];
    uint32_t reserved1;
    uint8_t aer[96];
    uint8_t oem_id[16];
    uint32_t variable_data_offset;
    uint64_t reserved2;
} IA64SalPCIeBody;

QEMU_BUILD_BUG_ON(sizeof(IA64SalPCIeBody) != 240);

typedef struct QEMU_PACKED IA64SalProcessorBody {
    uint64_t validation;
    uint64_t error_map;
    uint64_t state_parameter;
    uint64_t cr_lid;
    struct QEMU_PACKED {
        uint64_t validation;
        uint64_t check_info;
        uint64_t requestor;
        uint64_t responder;
        uint64_t target;
        uint64_t precise_ip;
    } machine_specific;
} IA64SalProcessorBody;

typedef struct IA64RasRecordSlot {
    uint16_t length;
    uint8_t data[IA64_RAS_MAX_RECORD_SIZE];
} IA64RasRecordSlot;

struct IA64RasHubState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint64_t mca_entry;
    uint64_t mca_gp;
    uint64_t init_entry;
    uint64_t init_gp;
    uint64_t cpe_vector;
    uint64_t rendezvous_vector;
    uint64_t rendezvous_timeout;
    uint64_t rendezvous_options;
    uint64_t wakeup_mechanism;
    uint64_t wakeup_value;
    uint64_t cpu_online;
    uint64_t rendezvous_required;
    uint64_t rendezvous_arrived;
    uint64_t wakeup_pending;
    uint8_t wakeup_current;
    bool rendezvous_active;
    bool rendezvous_fallback;
    uint64_t next_record_id;
    uint8_t head[IA64_RAS_BANK_COUNT];
    uint8_t count[IA64_RAS_BANK_COUNT];
    bool overflow[IA64_RAS_BANK_COUNT];
    IA64RasRecordSlot slots[IA64_RAS_SLOT_COUNT];
};

static bool ia64_ras_queue_init_record(IA64RasHubState *s,
                                       unsigned int cpu_index);

static const uint8_t ia64_sal_memory_guid[16] = {
    0xf2, 0xfa, 0x29, 0xe4, 0xb7, 0x3c, 0xd4, 0x11,
    0xbc, 0xa7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81,
};

static const uint8_t ia64_sal_pci_bus_guid[16] = {
    0xf4, 0xfa, 0x29, 0xe4, 0xb7, 0x3c, 0xd4, 0x11,
    0xbc, 0xa7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81,
};

static const uint8_t ia64_sal_pcie_guid[16] = {
    0x30, 0x24, 0xf4, 0x09, 0x41, 0xd4, 0xdc, 0x11,
    0x95, 0xff, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66,
};

static const uint8_t ia64_sal_processor_guid[16] = {
    0xf1, 0xfa, 0x29, 0xe4, 0xb7, 0x3c, 0xd4, 0x11,
    0xbc, 0xa7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81,
};

static unsigned int ia64_ras_bank(unsigned int cpu, unsigned int type)
{
    return cpu * IA64_RAS_RECORD_TYPE_COUNT + type;
}

static IA64RasRecordSlot *ia64_ras_slot(IA64RasHubState *s,
                                        unsigned int bank,
                                        unsigned int queue_index)
{
    unsigned int slot = bank * IA64_RAS_RECORD_DEPTH + queue_index;

    return &s->slots[slot];
}

static IA64RasRecordSlot *ia64_ras_head_slot(IA64RasHubState *s,
                                             unsigned int bank)
{
    return s->count[bank] ? ia64_ras_slot(s, bank, s->head[bank]) : NULL;
}

static bool ia64_ras_bank_decode(hwaddr addr, unsigned int *bank,
                                 hwaddr *bank_offset)
{
    uint64_t relative;
    unsigned int index;

    if (addr < IA64_RAS_RECORD_BANK_BASE) {
        return false;
    }
    relative = addr - IA64_RAS_RECORD_BANK_BASE;
    index = relative / IA64_RAS_RECORD_BANK_STRIDE;
    if (index >= IA64_RAS_BANK_COUNT) {
        return false;
    }
    *bank = index;
    *bank_offset = relative % IA64_RAS_RECORD_BANK_STRIDE;
    return true;
}

static uint64_t ia64_ras_error_status(IA64ChipsetFaultReason reason)
{
    uint64_t type;
    uint64_t qualifiers = BIT_ULL(21);

    switch (reason) {
    case IA64_CHIPSET_FAULT_IOMMU:
        type = 17;
        qualifiers |= BIT_ULL(16);
        break;
    case IA64_CHIPSET_FAULT_CONFIG_ABORT:
    case IA64_CHIPSET_FAULT_DECODE:
        type = 19;
        qualifiers |= BIT_ULL(16) | BIT_ULL(17);
        break;
    case IA64_CHIPSET_FAULT_PARITY:
        type = 22;
        qualifiers |= BIT_ULL(18);
        break;
    case IA64_CHIPSET_FAULT_PROTOCOL:
    case IA64_CHIPSET_FAULT_AER:
        type = 23;
        qualifiers |= BIT_ULL(17);
        break;
    case IA64_CHIPSET_FAULT_MEMORY_CORRECTED:
    case IA64_CHIPSET_FAULT_MEMORY_UNCORRECTED:
        type = 4;
        qualifiers |= BIT_ULL(18);
        break;
    case IA64_CHIPSET_FAULT_POWER:
    default:
        type = 1;
        break;
    }
    return (type << 8) | qualifiers;
}

static uint16_t ia64_ras_pci_error_type(IA64ChipsetFaultReason reason)
{
    switch (reason) {
    case IA64_CHIPSET_FAULT_PARITY:
        return 1;
    case IA64_CHIPSET_FAULT_CONFIG_ABORT:
        return 3;
    case IA64_CHIPSET_FAULT_DECODE:
    case IA64_CHIPSET_FAULT_IOMMU:
        return 4;
    case IA64_CHIPSET_FAULT_PROTOCOL:
    case IA64_CHIPSET_FAULT_AER:
        return 2;
    default:
        return 0;
    }
}

static uint8_t ia64_ras_recovery_info(IA64RasSeverity severity)
{
    uint8_t value = BIT(7);

    if (severity == IA64_RAS_SEVERITY_CORRECTED) {
        value |= BIT(0);
    } else if (severity == IA64_RAS_SEVERITY_FATAL) {
        value |= BIT(1);
    }
    return value;
}

static size_t ia64_ras_record_begin(IA64RasHubState *s, uint8_t *record,
                                    IA64RasSeverity severity,
                                    const uint8_t guid[16],
                                    uint32_t section_length)
{
    IA64SalRecordHeader *header = (IA64SalRecordHeader *)record;
    IA64SalSectionHeader *section =
        (IA64SalSectionHeader *)(record + sizeof(*header));
    uint32_t record_length = sizeof(*header) + section_length;

    memset(record, 0, record_length);
    header->record_id = cpu_to_le64(s->next_record_id++);
    header->revision[0] = 0x07;
    header->revision[1] = 0x00;
    header->severity = severity;
    header->validation = BIT(1);
    header->length = cpu_to_le32(record_length);

    memcpy(section->guid, guid, sizeof(section->guid));
    section->revision[0] = 0x03;
    section->revision[1] = 0x00;
    section->recovery = ia64_ras_recovery_info(severity);
    section->length = cpu_to_le32(section_length);
    return record_length;
}

static bool ia64_ras_queue_record(IA64RasHubState *s, unsigned int cpu,
                                  unsigned int type, const uint8_t *record,
                                  size_t length)
{
    unsigned int bank;
    unsigned int tail;
    IA64RasRecordSlot *slot;

    g_assert(cpu < IA64_RAS_MAX_CPUS);
    g_assert(type < IA64_RAS_RECORD_TYPE_COUNT);
    g_assert(length <= IA64_RAS_MAX_RECORD_SIZE);

    bank = ia64_ras_bank(cpu, type);
    if (s->count[bank] == IA64_RAS_RECORD_DEPTH) {
        s->overflow[bank] = true;
        return false;
    }
    tail = (s->head[bank] + s->count[bank]) % IA64_RAS_RECORD_DEPTH;
    slot = ia64_ras_slot(s, bank, tail);
    slot->length = length;
    memcpy(slot->data, record, length);
    if (length < sizeof(slot->data)) {
        memset(slot->data + length, 0, sizeof(slot->data) - length);
    }
    s->count[bank]++;
    return true;
}

static bool ia64_ras_queue_init_record(IA64RasHubState *s,
                                       unsigned int cpu_index)
{
    uint8_t record[IA64_RAS_MAX_RECORD_SIZE];
    IA64SalProcessorBody *body;
    CPUState *cs;
    IA64CPU *cpu;
    size_t length;

    if (cpu_index >= IA64_RAS_MAX_CPUS ||
        !(cs = qemu_get_cpu(cpu_index))) {
        return false;
    }
    cpu = IA64_CPU(cs);
    length = ia64_ras_record_begin(
        s, record, IA64_RAS_SEVERITY_RECOVERABLE,
        ia64_sal_processor_guid,
        sizeof(IA64SalSectionHeader) + sizeof(IA64SalProcessorBody));
    body = (IA64SalProcessorBody *)(record + sizeof(IA64SalRecordHeader) +
                                   sizeof(IA64SalSectionHeader));
    body->validation = cpu_to_le64(BIT_ULL(2));
    body->cr_lid = cpu_to_le64(cpu->env.cr[IA64_CR_SAPIC_LID]);
    return ia64_ras_queue_record(s, cpu_index, IA64_RAS_RECORD_TYPE_INIT,
                                 record, length);
}

static void ia64_ras_pop_record(IA64RasHubState *s, unsigned int bank)
{
    IA64RasRecordSlot *slot = ia64_ras_head_slot(s, bank);

    if (!slot) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
    s->head[bank] = (s->head[bank] + 1) % IA64_RAS_RECORD_DEPTH;
    s->count[bank]--;
    if (!s->count[bank]) {
        s->head[bank] = 0;
        s->overflow[bank] = false;
    }
}

static uint64_t ia64_ras_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IA64RasHubState *s = opaque;
    IA64RasRecordSlot *slot;
    unsigned int bank;
    hwaddr offset;

    if (size != 8 || (addr & 7)) {
        return 0;
    }
    switch (addr) {
    case IA64_RAS_REG_MAGIC:
        return IA64_RAS_HUB_MAGIC;
    case IA64_RAS_REG_REVISION:
        return IA64_RAS_HUB_REVISION;
    case IA64_RAS_REG_CAPABILITIES:
        return IA64_RAS_CAP_MCA | IA64_RAS_CAP_CMC | IA64_RAS_CAP_CPE |
               IA64_RAS_CAP_RENDEZVOUS | IA64_RAS_CAP_SAL_RECORDS |
               IA64_RAS_CAP_INIT | IA64_RAS_CAP_MEMORY_WAKEUP;
    case IA64_RAS_REG_MAX_RECORD_SIZE:
        return IA64_RAS_MAX_RECORD_SIZE;
    case IA64_RAS_REG_MCA_ENTRY:
        return s->mca_entry;
    case IA64_RAS_REG_MCA_GP:
        return s->mca_gp;
    case IA64_RAS_REG_INIT_ENTRY:
        return s->init_entry;
    case IA64_RAS_REG_INIT_GP:
        return s->init_gp;
    case IA64_RAS_REG_CPE_VECTOR:
        return s->cpe_vector;
    case IA64_RAS_REG_RENDEZVOUS_VECTOR:
        return s->rendezvous_vector;
    case IA64_RAS_REG_RENDEZVOUS_TIMEOUT:
        return s->rendezvous_timeout;
    case IA64_RAS_REG_RENDEZVOUS_OPTIONS:
        return s->rendezvous_options;
    case IA64_RAS_REG_WAKEUP_MECHANISM:
        return s->wakeup_mechanism;
    case IA64_RAS_REG_WAKEUP_VALUE:
        return s->wakeup_value;
    case IA64_RAS_REG_CPU_ONLINE:
        return s->cpu_online;
    case IA64_RAS_REG_RENDEZVOUS_ACTIVE:
        return s->rendezvous_active;
    case IA64_RAS_REG_RENDEZVOUS_REQUIRED:
        return s->rendezvous_required;
    case IA64_RAS_REG_RENDEZVOUS_ARRIVED:
        return s->rendezvous_arrived;
    case IA64_RAS_REG_WAKEUP_PENDING:
        return s->wakeup_pending;
    case IA64_RAS_REG_RENDEZVOUS_FALLBACK:
        return s->rendezvous_fallback;
    default:
        break;
    }

    if (!ia64_ras_bank_decode(addr, &bank, &offset)) {
        return 0;
    }
    slot = ia64_ras_head_slot(s, bank);
    switch (offset) {
    case IA64_RAS_RECORD_REG_LENGTH:
        return slot ? slot->length : 0;
    case IA64_RAS_RECORD_REG_ID:
        return slot && slot->length >= sizeof(uint64_t) ?
            ldq_le_p(slot->data) : 0;
    case IA64_RAS_RECORD_REG_STATUS:
        return (slot ? IA64_RAS_RECORD_STATUS_PRESENT : 0) |
               (s->count[bank] > 1 ? IA64_RAS_RECORD_STATUS_MORE : 0) |
               (s->overflow[bank] ? IA64_RAS_RECORD_STATUS_OVERFLOW : 0);
    default:
        if (slot && offset >= IA64_RAS_RECORD_DATA &&
            offset - IA64_RAS_RECORD_DATA <= slot->length - 8) {
            return ldq_le_p(slot->data + offset - IA64_RAS_RECORD_DATA);
        }
        return 0;
    }
}

static void ia64_ras_try_deliver_mca(IA64RasHubState *s)
{
    unsigned int cpu;

    if (!s->mca_entry) {
        return;
    }
    for (cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
        CPUState *cs = qemu_get_cpu(cpu);
        unsigned int bank = ia64_ras_bank(cpu,
                                         IA64_RAS_RECORD_TYPE_MCA);
        IA64RasRecordSlot *slot = ia64_ras_head_slot(s, bank);
        const IA64SalRecordHeader *header;

        if (!slot || !cs || !(s->cpu_online & BIT_ULL(cpu))) {
            continue;
        }
        header = (const IA64SalRecordHeader *)slot->data;
        ia64_cpu_request_mca(cs, s->mca_entry, s->mca_gp,
                             le64_to_cpu(header->record_id),
                             header->severity);
    }
}

static bool ia64_ras_vector_valid(uint64_t vector)
{
    return vector != 0 && vector <= UINT8_MAX &&
           ia64_external_interrupt_vector_valid((uint8_t)vector);
}

static uint64_t ia64_ras_present_cpus(void)
{
    CPUState *cs;
    uint64_t mask = 0;

    CPU_FOREACH(cs) {
        if ((unsigned int)cs->cpu_index < IA64_RAS_MAX_CPUS) {
            mask |= BIT_ULL(cs->cpu_index);
        }
    }
    return mask;
}

static void ia64_ras_signal_init_mask(uint64_t mask)
{
    unsigned int cpu;

    for (cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
        CPUState *cs;

        if (!(mask & BIT_ULL(cpu)) || !(cs = qemu_get_cpu(cpu))) {
            continue;
        }
        ia64_sapic_set_init(cs, 1);
    }
}

static void ia64_ras_signal_mask(uint64_t mask, uint8_t vector)
{
    unsigned int cpu;

    if (!ia64_external_interrupt_vector_valid(vector)) {
        return;
    }
    for (cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
        CPUState *cs;

        if (!(mask & BIT_ULL(cpu)) || !(cs = qemu_get_cpu(cpu))) {
            continue;
        }
        ia64_sapic_set_irq(cs, vector);
    }
}

static void ia64_ras_begin_rendezvous(IA64RasHubState *s,
                                      uint64_t initiator)
{
    if (initiator >= IA64_RAS_MAX_CPUS || !qemu_get_cpu(initiator)) {
        return;
    }
    s->rendezvous_required = s->cpu_online & ia64_ras_present_cpus() &
                             ~BIT_ULL(initiator);
    s->rendezvous_arrived = 0;
    s->rendezvous_fallback = false;
    s->rendezvous_active = s->rendezvous_required != 0;
    if (s->rendezvous_active) {
        if (ia64_ras_vector_valid(s->rendezvous_vector)) {
            ia64_ras_signal_mask(s->rendezvous_required,
                                 s->rendezvous_vector);
        } else {
            s->rendezvous_fallback = true;
            ia64_ras_signal_init_mask(s->rendezvous_required);
        }
    }
}

static void ia64_ras_start_memory_wakeup(IA64RasHubState *s)
{
    unsigned int cpu;
    MemTxResult result;

    if (s->wakeup_mechanism != 2 || !s->wakeup_pending) {
        return;
    }
    cpu = ctz64(s->wakeup_pending);
    s->wakeup_current = cpu;
    address_space_stq_le(&address_space_memory,
                         s->wakeup_value & ~IA64_PHYS_UC_BIT,
                         cpu + 1, MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        s->wakeup_pending = 0;
    }
}

static void ia64_ras_release_rendezvous(IA64RasHubState *s)
{
    uint64_t participants = s->rendezvous_required;

    s->rendezvous_active = false;
    s->rendezvous_required = 0;
    s->rendezvous_arrived = 0;
    if (s->wakeup_mechanism == 1 &&
        ia64_ras_vector_valid(s->wakeup_value)) {
        ia64_ras_signal_mask(participants, (uint8_t)s->wakeup_value);
    } else if (s->wakeup_mechanism == 2) {
        s->wakeup_pending = participants;
        ia64_ras_start_memory_wakeup(s);
    }
}

static void ia64_ras_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
    IA64RasHubState *s = opaque;
    unsigned int bank;
    hwaddr offset;

    if (size != 8 || (addr & 7)) {
        return;
    }
    switch (addr) {
    case IA64_RAS_REG_MCA_ENTRY:
        s->mca_entry = value;
        ia64_ras_try_deliver_mca(s);
        return;
    case IA64_RAS_REG_MCA_GP:
        s->mca_gp = value;
        ia64_ras_try_deliver_mca(s);
        return;
    case IA64_RAS_REG_INIT_ENTRY:
        s->init_entry = value;
        for (unsigned int cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
            ia64_cpu_set_init_entry(qemu_get_cpu(cpu), s->init_entry,
                                    s->init_gp);
        }
        return;
    case IA64_RAS_REG_INIT_GP:
        s->init_gp = value;
        for (unsigned int cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
            ia64_cpu_set_init_entry(qemu_get_cpu(cpu), s->init_entry,
                                    s->init_gp);
        }
        return;
    case IA64_RAS_REG_CPE_VECTOR:
        s->cpe_vector = value == 0 ||
            ia64_ras_vector_valid(value) ? value : 0;
        if (s->cpe_vector &&
            ia64_ras_head_slot(s, ia64_ras_bank(
                0, IA64_RAS_RECORD_TYPE_CPE)) != NULL) {
            ia64_ras_signal_mask(BIT_ULL(0), s->cpe_vector);
        }
        return;
    case IA64_RAS_REG_RENDEZVOUS_VECTOR:
        s->rendezvous_vector = value == 0 ||
            ia64_ras_vector_valid(value) ? value : 0;
        return;
    case IA64_RAS_REG_RENDEZVOUS_TIMEOUT:
        s->rendezvous_timeout = value;
        return;
    case IA64_RAS_REG_RENDEZVOUS_OPTIONS:
        s->rendezvous_options = value & 3;
        return;
    case IA64_RAS_REG_WAKEUP_MECHANISM:
        s->wakeup_mechanism = value <= 2 ? value : 0;
        return;
    case IA64_RAS_REG_WAKEUP_VALUE:
        s->wakeup_value = value;
        return;
    case IA64_RAS_REG_CPU_ONLINE:
        s->cpu_online |= value & ia64_ras_present_cpus();
        ia64_ras_try_deliver_mca(s);
        return;
    case IA64_RAS_REG_RENDEZVOUS_BEGIN:
        ia64_ras_begin_rendezvous(s, value);
        return;
    case IA64_RAS_REG_RENDEZVOUS_ARRIVED:
        if (s->rendezvous_active) {
            s->rendezvous_arrived |= value & s->rendezvous_required;
        }
        return;
    case IA64_RAS_REG_RENDEZVOUS_RELEASE:
        if (value == 1) {
            ia64_ras_release_rendezvous(s);
        }
        return;
    case IA64_RAS_REG_RENDEZVOUS_INIT:
        if (s->rendezvous_active) {
            uint64_t mask = s->rendezvous_required & ~s->rendezvous_arrived;

            s->rendezvous_fallback = true;
            ia64_ras_signal_init_mask(mask);
        }
        return;
    case IA64_RAS_REG_INIT_CAPTURE:
        ia64_ras_queue_init_record(s, value);
        return;
    case IA64_RAS_REG_WAKEUP_ACK:
        if (s->wakeup_pending & value & BIT_ULL(s->wakeup_current)) {
            s->wakeup_pending &= ~BIT_ULL(s->wakeup_current);
            ia64_ras_start_memory_wakeup(s);
        }
        return;
    default:
        break;
    }

    if (ia64_ras_bank_decode(addr, &bank, &offset) &&
        offset == IA64_RAS_RECORD_REG_CLEAR &&
        value == IA64_RAS_RECORD_CLEAR_VALUE) {
        ia64_ras_pop_record(s, bank);
        if (bank % IA64_RAS_RECORD_TYPE_COUNT ==
            IA64_RAS_RECORD_TYPE_MCA) {
            ia64_ras_try_deliver_mca(s);
        }
    }
}

static const MemoryRegionOps ia64_ras_mmio_ops = {
    .read = ia64_ras_mmio_read,
    .write = ia64_ras_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void ia64_ras_hub_reset(DeviceState *dev)
{
    IA64RasHubState *s = IA64_RAS_HUB(dev);

    s->mca_entry = 0;
    s->mca_gp = 0;
    s->init_entry = 0;
    s->init_gp = 0;
    s->cpe_vector = 0;
    s->rendezvous_vector = 0;
    s->rendezvous_timeout = 0;
    s->rendezvous_options = 0;
    s->wakeup_mechanism = 0;
    s->wakeup_value = 0;
    s->cpu_online = 0;
    s->rendezvous_required = 0;
    s->rendezvous_arrived = 0;
    s->wakeup_pending = 0;
    s->wakeup_current = 0;
    s->rendezvous_active = false;
    s->rendezvous_fallback = false;
}

static void ia64_ras_hub_init(Object *obj)
{
    IA64RasHubState *s = IA64_RAS_HUB(obj);

    memory_region_init_io(&s->mmio, obj, &ia64_ras_mmio_ops, s,
                          TYPE_IA64_RAS_HUB, IA64_RAS_HUB_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    s->next_record_id = 1;
}

static int ia64_ras_hub_post_load(void *opaque, int version_id)
{
    IA64RasHubState *s = opaque;
    unsigned int bank;

    if (version_id < 2) {
        s->init_entry = 0;
        s->init_gp = 0;
        s->rendezvous_fallback = false;
        s->wakeup_pending = 0;
        s->wakeup_current = 0;
    }

    if ((s->cpu_online & ~ia64_ras_present_cpus()) != 0 ||
        s->wakeup_current >= IA64_RAS_MAX_CPUS ||
        (s->cpe_vector != 0 && !ia64_ras_vector_valid(s->cpe_vector)) ||
        (s->rendezvous_vector != 0 &&
         !ia64_ras_vector_valid(s->rendezvous_vector)) ||
        s->rendezvous_options > 3 || s->wakeup_mechanism > 2 ||
        (s->wakeup_mechanism == 1 && s->wakeup_value != 0 &&
         !ia64_ras_vector_valid(s->wakeup_value)) ||
        s->next_record_id == 0) {
        return -EINVAL;
    }
    for (bank = 0; bank < IA64_RAS_BANK_COUNT; bank++) {
        unsigned int queue_index;

        if (s->head[bank] >= IA64_RAS_RECORD_DEPTH ||
            s->count[bank] > IA64_RAS_RECORD_DEPTH ||
            (!s->count[bank] && s->head[bank])) {
            return -EINVAL;
        }
        for (queue_index = 0; queue_index < IA64_RAS_RECORD_DEPTH;
             queue_index++) {
            IA64RasRecordSlot *slot = ia64_ras_slot(s, bank, queue_index);
            bool queued = false;
            unsigned int i;

            for (i = 0; i < s->count[bank]; i++) {
                if (queue_index ==
                    (s->head[bank] + i) % IA64_RAS_RECORD_DEPTH) {
                    queued = true;
                    break;
                }
            }
            if (!queued) {
                if (slot->length != 0) {
                    return -EINVAL;
                }
                continue;
            }
            if (slot->length < sizeof(IA64SalRecordHeader) +
                               sizeof(IA64SalSectionHeader) ||
                slot->length > IA64_RAS_MAX_RECORD_SIZE ||
                (slot->length & 7) != 0) {
                return -EINVAL;
            }
            {
                IA64SalRecordHeader *header =
                    (IA64SalRecordHeader *)slot->data;
                IA64SalSectionHeader *section =
                    (IA64SalSectionHeader *)(slot->data + sizeof(*header));

                if (le32_to_cpu(header->length) != slot->length ||
                    header->severity > IA64_RAS_SEVERITY_CORRECTED ||
                    (header->validation & ~3U) != 0 ||
                    le32_to_cpu(section->length) < sizeof(*section) ||
                    le32_to_cpu(section->length) !=
                        slot->length - sizeof(*header)) {
                    return -EINVAL;
                }
            }
        }
    }
    if ((s->rendezvous_required & ~s->cpu_online) != 0 ||
        (s->rendezvous_arrived & ~s->rendezvous_required) != 0 ||
        (s->wakeup_pending & ~s->cpu_online) != 0 ||
        (s->wakeup_pending != 0 &&
         (s->wakeup_mechanism != 2 ||
          !(s->wakeup_pending & BIT_ULL(s->wakeup_current)))) ||
        (s->rendezvous_active && s->rendezvous_required == 0) ||
        (!s->rendezvous_active &&
         (s->rendezvous_required != 0 || s->rendezvous_arrived != 0))) {
        return -EINVAL;
    }
    if (s->rendezvous_active) {
        uint64_t missing = s->rendezvous_required &
                           ~s->rendezvous_arrived;

        if (s->rendezvous_fallback) {
            ia64_ras_signal_init_mask(missing);
        } else {
            ia64_ras_signal_mask(missing, s->rendezvous_vector);
        }
    }
    for (unsigned int cpu = 0; cpu < IA64_RAS_MAX_CPUS; cpu++) {
        ia64_cpu_set_init_entry(qemu_get_cpu(cpu), s->init_entry,
                                s->init_gp);
    }
    ia64_ras_start_memory_wakeup(s);
    ia64_ras_try_deliver_mca(s);
    return 0;
}

static const VMStateDescription vmstate_ia64_ras_slot = {
    .name = "ia64-ras-hub/slot",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(length, IA64RasRecordSlot),
        VMSTATE_UINT8_ARRAY(data, IA64RasRecordSlot,
                            IA64_RAS_MAX_RECORD_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ia64_ras_hub = {
    .name = "ia64-ras-hub",
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = ia64_ras_hub_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(mca_entry, IA64RasHubState),
        VMSTATE_UINT64(mca_gp, IA64RasHubState),
        VMSTATE_UINT64_V(init_entry, IA64RasHubState, 2),
        VMSTATE_UINT64_V(init_gp, IA64RasHubState, 2),
        VMSTATE_UINT64(cpe_vector, IA64RasHubState),
        VMSTATE_UINT64(rendezvous_vector, IA64RasHubState),
        VMSTATE_UINT64(rendezvous_timeout, IA64RasHubState),
        VMSTATE_UINT64(rendezvous_options, IA64RasHubState),
        VMSTATE_UINT64(wakeup_mechanism, IA64RasHubState),
        VMSTATE_UINT64(wakeup_value, IA64RasHubState),
        VMSTATE_UINT64(cpu_online, IA64RasHubState),
        VMSTATE_UINT64(rendezvous_required, IA64RasHubState),
        VMSTATE_UINT64(rendezvous_arrived, IA64RasHubState),
        VMSTATE_BOOL(rendezvous_active, IA64RasHubState),
        VMSTATE_BOOL_V(rendezvous_fallback, IA64RasHubState, 2),
        VMSTATE_UINT64_V(wakeup_pending, IA64RasHubState, 2),
        VMSTATE_UINT8_V(wakeup_current, IA64RasHubState, 2),
        VMSTATE_UINT64(next_record_id, IA64RasHubState),
        VMSTATE_UINT8_ARRAY(head, IA64RasHubState, IA64_RAS_BANK_COUNT),
        VMSTATE_UINT8_ARRAY(count, IA64RasHubState, IA64_RAS_BANK_COUNT),
        VMSTATE_BOOL_ARRAY(overflow, IA64RasHubState, IA64_RAS_BANK_COUNT),
        VMSTATE_STRUCT_ARRAY(slots, IA64RasHubState, IA64_RAS_SLOT_COUNT, 1,
                             vmstate_ia64_ras_slot, IA64RasRecordSlot),
        VMSTATE_END_OF_LIST()
    },
};

static void ia64_ras_hub_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->vmsd = &vmstate_ia64_ras_hub;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, ia64_ras_hub_reset);
}

static const TypeInfo ia64_ras_hub_type_info = {
    .name = TYPE_IA64_RAS_HUB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IA64RasHubState),
    .instance_init = ia64_ras_hub_init,
    .class_init = ia64_ras_hub_class_init,
};

IA64RasHubState *ia64_ras_hub_create(Object *parent, const char *name,
                                     hwaddr base, Error **errp)
{
    DeviceState *dev = qdev_new(TYPE_IA64_RAS_HUB);

    object_property_add_child(parent, name, OBJECT(dev));
    object_unref(OBJECT(dev));
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        object_unparent(OBJECT(dev));
        return NULL;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    return IA64_RAS_HUB(dev);
}

bool ia64_ras_hub_report_chipset_fault(void *opaque,
                                       const IA64ChipsetFault *fault)
{
    IA64RasHubState *s = opaque;
    uint8_t record[IA64_RAS_MAX_RECORD_SIZE];
    IA64RasSeverity severity;
    unsigned int type;
    size_t length;

    if (!s || !fault || fault->severity > IA64_RAS_SEVERITY_CORRECTED) {
        return false;
    }
    severity = fault->severity;
    type = severity == IA64_RAS_SEVERITY_CORRECTED ?
        IA64_RAS_RECORD_TYPE_CPE : IA64_RAS_RECORD_TYPE_MCA;

    if (fault->reason == IA64_CHIPSET_FAULT_MEMORY_CORRECTED ||
        fault->reason == IA64_CHIPSET_FAULT_MEMORY_UNCORRECTED) {
        IA64SalMemoryBody *body;

        length = ia64_ras_record_begin(
            s, record, severity, ia64_sal_memory_guid,
            sizeof(IA64SalSectionHeader) + sizeof(IA64SalMemoryBody));
        body = (IA64SalMemoryBody *)(record + sizeof(IA64SalRecordHeader) +
                                    sizeof(IA64SalSectionHeader));
        body->validation = cpu_to_le64(BIT_ULL(0) | BIT_ULL(1) |
                                       BIT_ULL(11) | BIT_ULL(13) |
                                       BIT_ULL(14));
        body->error_status = cpu_to_le64(
            ia64_ras_error_status(fault->reason));
        body->address = cpu_to_le64(fault->address);
        body->requestor = cpu_to_le64(fault->requester);
        body->target = cpu_to_le64(fault->address);
        body->bus_data = cpu_to_le64(fault->information);
    } else if (fault->source == IA64_CHIPSET_FAULT_PCIE &&
               fault->reason == IA64_CHIPSET_FAULT_AER &&
               fault->pcie.valid) {
        IA64SalPCIeBody *body;

        length = ia64_ras_record_begin(
            s, record, severity, ia64_sal_pcie_guid,
            sizeof(IA64SalSectionHeader) + sizeof(IA64SalPCIeBody));
        body = (IA64SalPCIeBody *)(record + sizeof(IA64SalRecordHeader) +
                                  sizeof(IA64SalSectionHeader));
        body->validation = cpu_to_le64(BIT_ULL(0) | BIT_ULL(1) |
                                       BIT_ULL(2) | BIT_ULL(3) |
                                       BIT_ULL(5) | BIT_ULL(6) |
                                       BIT_ULL(7));
        body->port_type = cpu_to_le32(fault->pcie.port_type);
        body->version = cpu_to_le32(fault->pcie.version);
        body->command_status = cpu_to_le32(fault->pcie.command_status);
        memcpy(body->device_id, fault->pcie.device_id,
               sizeof(body->device_id));
        body->bridge_control_status = cpu_to_le32(
            fault->pcie.bridge_control_status);
        memcpy(body->capability, fault->pcie.capability,
               sizeof(body->capability));
        memcpy(body->aer, fault->pcie.aer, sizeof(body->aer));
    } else {
        IA64SalPciBusBody *body;

        length = ia64_ras_record_begin(
            s, record, severity, ia64_sal_pci_bus_guid,
            sizeof(IA64SalSectionHeader) + sizeof(IA64SalPciBusBody));
        body = (IA64SalPciBusBody *)(record + sizeof(IA64SalRecordHeader) +
                                    sizeof(IA64SalSectionHeader));
        body->validation = cpu_to_le64(BIT_ULL(0) | BIT_ULL(1) |
                                       BIT_ULL(2) | BIT_ULL(3) |
                                       BIT_ULL(4) | BIT_ULL(6) |
                                       BIT_ULL(8));
        body->error_status = cpu_to_le64(
            ia64_ras_error_status(fault->reason));
        body->error_type = cpu_to_le16(
            ia64_ras_pci_error_type(fault->reason));
        body->bus_id = cpu_to_le16((fault->segment << 8) | fault->bus);
        body->address = cpu_to_le64(fault->address);
        body->data = cpu_to_le64(fault->information);
        body->requestor = cpu_to_le64(fault->requester);
        body->target = cpu_to_le64(fault->address);
    }

    if (!ia64_ras_queue_record(s, 0, type, record, length)) {
        return false;
    }
    if (type == IA64_RAS_RECORD_TYPE_CPE) {
        CPUState *cs = qemu_get_cpu(0);

        if (cs && s->cpe_vector) {
            ia64_sapic_set_irq(cs, s->cpe_vector);
        }
    } else {
        ia64_ras_try_deliver_mca(s);
    }
    return true;
}

bool ia64_ras_hub_report_processor_error(IA64RasHubState *s,
                                         CPUState *cs,
                                         IA64RasSeverity severity,
                                         uint64_t status,
                                         uint64_t address,
                                         uint64_t information)
{
    uint8_t record[IA64_RAS_MAX_RECORD_SIZE];
    IA64SalProcessorBody *body;
    IA64CPU *cpu;
    unsigned int type;
    unsigned int index;
    uint64_t psp;
    size_t length;

    if (!s || !cs || !object_dynamic_cast(OBJECT(cs), TYPE_IA64_CPU) ||
        severity > IA64_RAS_SEVERITY_CORRECTED) {
        return false;
    }
    index = cs->cpu_index;
    if (index >= IA64_RAS_MAX_CPUS) {
        return false;
    }
    cpu = IA64_CPU(cs);
    type = severity == IA64_RAS_SEVERITY_CORRECTED ?
        IA64_RAS_RECORD_TYPE_CMC : IA64_RAS_RECORD_TYPE_MCA;
    length = ia64_ras_record_begin(
        s, record, severity, ia64_sal_processor_guid,
        sizeof(IA64SalSectionHeader) + sizeof(IA64SalProcessorBody));
    body = (IA64SalProcessorBody *)(record + sizeof(IA64SalRecordHeader) +
                                   sizeof(IA64SalSectionHeader));
    psp = BIT_ULL(5) | BIT_ULL(6) | BIT_ULL(7) | BIT_ULL(8) |
          BIT_ULL(12) | BIT_ULL(13) | BIT_ULL(14) | BIT_ULL(17) |
          BIT_ULL(20) | BIT_ULL(24) | BIT_ULL(25) | BIT_ULL(26) |
          BIT_ULL(27) | BIT_ULL(29) | BIT_ULL(30) | BIT_ULL(31) |
          BIT_ULL(61);
    if (severity == IA64_RAS_SEVERITY_CORRECTED) {
        psp |= BIT_ULL(18);
    }
    body->validation = cpu_to_le64(BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(2) |
                                   BIT_ULL(12));
    body->error_map = cpu_to_le64(
        BIT_ULL(24) | ((uint64_t)(cpu->thread_id & 0xf) << 4) |
        (cpu->core_id & 0xf));
    body->state_parameter = cpu_to_le64(psp);
    body->cr_lid = cpu_to_le64(cpu->env.cr[IA64_CR_SAPIC_LID]);
    body->machine_specific.validation = cpu_to_le64(
        BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(3) | BIT_ULL(4));
    body->machine_specific.check_info = cpu_to_le64(status);
    body->machine_specific.requestor = cpu_to_le64(information);
    body->machine_specific.target = cpu_to_le64(address);
    body->machine_specific.precise_ip = cpu_to_le64(cpu->env.ip);
    if (!ia64_ras_queue_record(s, index, type, record, length)) {
        return false;
    }
    ia64_cpu_record_machine_check(cs, severity, status, address, information);
    if (severity != IA64_RAS_SEVERITY_CORRECTED) {
        ia64_ras_try_deliver_mca(s);
    }
    return true;
}

static void ia64_ras_hub_register_types(void)
{
    type_register_static(&ia64_ras_hub_type_info);
}
type_init(ia64_ras_hub_register_types)
