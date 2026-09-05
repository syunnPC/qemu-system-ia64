/*
 * HP zx1 MIO CSR device
 *
 * The board supplies placement, rope topology, and interrupt routing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/hp-zx1-ioa.h"
#include "hw/pci-host/hp-zx1-mio.h"
#include "hw/pci-host/hp-zx1-iommu.h"
#include "hw/pci-host/hp-zx1-mio-regs.h"
#include "hw/pci-host/hp-zx2-mio-regs.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "system/address-spaces.h"

#define TYPE_HP_ZX1_MIO_IOMMU_MEMORY_REGION \
    TYPE_HP_ZX1_MIO ".iommu-memory-region"

#define HP_ZX1_MIO_IOC_FUNCTION_OFFSET      UINT64_C(0x1000)
#define HP_ZX1_MIO_IOMMU_FIRST              UINT64_C(0x1300)
#define HP_ZX1_MIO_IOMMU_LAST               UINT64_C(0x1320)
#define HP_ZX1_MIO_IOMMU_END                UINT64_C(0x1328)
#define HP_ZX1_MIO_IOMMU_SIZE               \
    (HP_ZX1_IOMMU_PHYS_MASK + UINT64_C(1))
#define HP_ZX2_MIO_EXTRA_CONTEXTS            \
    (HP_ZX2_MIO_GROUP_COUNT - 1)

typedef struct HPZX1MIOMigrationState {
    HPZX1MIORegs regs;
    HPZX1IOMMUFrontend iommu;
    HPZX1IOMMUFrontend zx2_iommu[HP_ZX2_MIO_EXTRA_CONTEXTS];
    HPZX2MIORegs zx2_regs;
} HPZX1MIOMigrationState;

typedef struct HPZX1MIORootFrontend {
    MemoryRegion root;
    MemoryRegion iommu_alias[HP_ZX2_MIO_GROUP_COUNT];
    AddressSpace as;
    PCIBus *bus;
    HPZX1IOAState *ioa;
    uint16_t ropes;
    uint8_t group;
    uint8_t context;
} HPZX1MIORootFrontend;

struct HPZX1MIOState {
    SysBusDevice parent_obj;

    MemoryRegion csr;
    IOMMUMemoryRegion iommu_mr;
    AddressSpace iommu_as;
    IOMMUMemoryRegion zx2_iommu_mr[HP_ZX2_MIO_EXTRA_CONTEXTS];
    AddressSpace zx2_iommu_as[HP_ZX2_MIO_EXTRA_CONTEXTS];
    QemuRecMutex iommu_lock;

    HPZX1MIORegs regs;
    HPZX1IOMMUFrontend iommu;
    HPZX1IOMMUFrontend zx2_iommu[HP_ZX2_MIO_EXTRA_CONTEXTS];
    HPZX1MIOMigrationState migration;
    HPZX1MIOIOMMUResetConfig iommu_reset;
    HPZX1MIORootFrontend root_frontends[HP_ZX1_MIO_LBA_PORT_COUNT];
    bool iommu_configured;
    bool zx2;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
    HPIOSAPICDeliver error_deliver;
    void *error_delivery_opaque;
    HPZX2MIORegs zx2_regs;
};

static HPZX1IOMMUFrontend *hp_zx1_mio_iommu_context(
    HPZX1MIOState *s, unsigned int context)
{
    g_assert(context < HP_ZX2_MIO_GROUP_COUNT);
    return context == 0 ? &s->iommu : &s->zx2_iommu[context - 1];
}

static IOMMUMemoryRegion *hp_zx1_mio_iommu_region(
    HPZX1MIOState *s, unsigned int context)
{
    g_assert(context < HP_ZX2_MIO_GROUP_COUNT);
    return context == 0 ? &s->iommu_mr : &s->zx2_iommu_mr[context - 1];
}

static unsigned int hp_zx1_mio_iommu_region_context(
    HPZX1MIOState *s, IOMMUMemoryRegion *mr)
{
    unsigned int context;

    for (context = 0; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
        if (hp_zx1_mio_iommu_region(s, context) == mr) {
            return context;
        }
    }
    g_assert_not_reached();
}

static unsigned int hp_zx1_mio_selected_context(const HPZX1MIOState *s)
{
    return s->zx2 ?
        s->zx2_regs.iommu_select & HP_ZX2_MIO_IOMMU_SELECT_MASK : 0;
}

#define HP_ZX2_MIO_F0_ID_VALUE UINT64_C(0x4030103c)
#define HP_ZX2_MIO_F1_ID_VALUE UINT64_C(0x4031103c)

static const HPSBAIOMMUPurge hp_zx1_mio_full_unmap = {
    .iova = 0,
    .size = HP_ZX1_MIO_IOMMU_SIZE,
};

static bool hp_zx2_mio_update_frontend_groups(HPZX1MIOState *s);

bool hp_zx1_mio_set_fault_notifier(HPZX1MIOState *s,
                                    IA64ChipsetFaultNotify notify,
                                    void *opaque, Error **errp)
{
    if (!s || !notify) {
        error_setg(errp, "MIO fault setup requires a device and callback");
        return false;
    }
    if (qdev_is_realized(DEVICE(s))) {
        error_setg(errp, "MIO fault setup must precede device realization");
        return false;
    }
    if (s->fault_notify) {
        error_setg(errp, "MIO fault callback is already configured");
        return false;
    }
    s->fault_notify = notify;
    s->fault_opaque = opaque;
    return true;
}

bool hp_zx1_mio_set_error_delivery(HPZX1MIOState *s,
                                    HPIOSAPICDeliver deliver,
                                    void *opaque, Error **errp)
{
    if (!s || !deliver) {
        error_setg(errp, "MIO error delivery requires a device and callback");
        return false;
    }
    if (qdev_is_realized(DEVICE(s))) {
        error_setg(errp, "MIO error delivery must precede realization");
        return false;
    }
    if (s->error_deliver) {
        error_setg(errp, "MIO error delivery is already configured");
        return false;
    }
    s->error_deliver = deliver;
    s->error_delivery_opaque = opaque;
    return true;
}

static void hp_zx1_mio_report_fault(HPZX1MIOState *s, uint64_t status,
                                    uint64_t address, uint64_t information,
                                    IA64ChipsetFaultReason reason)
{
    IA64ChipsetFault fault;

    hp_zx1_mio_regs_report_fault(&s->regs, status, address, information);
    if (s->zx2) {
        uint64_t config = s->zx2_regs.error_interrupt;

        hp_zx2_mio_regs_report_fault(&s->zx2_regs, status, address,
                                     information);
        if (s->error_deliver &&
            (config & HP_ZX2_MIO_ERROR_INTERRUPT_ENABLE)) {
            HPIOSAPICMessage message = {
                .address = UINT64_C(0xfee00000) |
                    (((config & HP_ZX2_MIO_ERROR_INTERRUPT_ID) >> 16) << 12) |
                    (((config & HP_ZX2_MIO_ERROR_INTERRUPT_EID) >> 24) << 4),
                .data = UINT32_C(0x4000) |
                    ((config & HP_ZX2_MIO_ERROR_INTERRUPT_VECTOR) >> 8),
            };

            s->error_deliver(s->error_delivery_opaque, &message);
        }
    }
    if (!s->fault_notify ||
        !(s->regs.error_config & HP_ZX1_MIO_ERROR_CONFIG_NOTIFY)) {
        return;
    }
    fault = (IA64ChipsetFault) {
        .source = s->zx2 ? IA64_CHIPSET_FAULT_ZX2_MIO :
                  IA64_CHIPSET_FAULT_ZX1_MIO,
        .reason = reason,
        .address = address,
        .status = s->regs.error_status,
        .information = information,
    };
    s->fault_notify(s->fault_opaque, &fault);
}

static HPZX1IOMMUResetConfig hp_zx1_mio_iommu_reset_config(
    const HPZX1MIOState *s)
{
    return (HPZX1IOMMUResetConfig) {
        .ibase = s->iommu_reset.ibase,
        .imask = s->iommu_reset.imask,
        .pcom = s->iommu_reset.pcom,
        .tcnfg = s->iommu_reset.tcnfg,
        .pdir_base = s->iommu_reset.pdir_base,
    };
}

bool hp_zx1_mio_configure_iommu_reset(
    HPZX1MIOState *s, const HPZX1MIOIOMMUResetConfig *config,
    Error **errp)
{
    HPZX1IOMMUFrontend frontend = { 0 };
    HPZX1IOMMUResetConfig frontend_config;

    if (!s || !config) {
        error_setg(errp,
                   "zx1 MIO IOMMU setup requires a device and reset configuration");
        return false;
    }
    if (qdev_is_realized(DEVICE(s))) {
        error_setg(errp,
                   "zx1 MIO IOMMU reset configuration must precede device realization");
        return false;
    }
    if (s->iommu_configured) {
        error_setg(errp,
                   "zx1 MIO IOMMU reset configuration may be supplied only once");
        return false;
    }

    frontend_config = (HPZX1IOMMUResetConfig) {
        .ibase = config->ibase,
        .imask = config->imask,
        .pcom = config->pcom,
        .tcnfg = config->tcnfg,
        .pdir_base = config->pdir_base,
    };
    if (!hp_zx1_iommu_frontend_reset(&frontend, &frontend_config)) {
        error_setg(errp, "invalid zx1 MIO IOMMU reset configuration");
        return false;
    }

    qemu_rec_mutex_lock(&s->iommu_lock);
    s->iommu_reset = *config;
    s->iommu = frontend;
    for (unsigned int context = 1;
         context < HP_ZX2_MIO_GROUP_COUNT; context++) {
        s->zx2_iommu[context - 1] = frontend;
    }
    s->iommu_configured = true;
    qemu_rec_mutex_unlock(&s->iommu_lock);
    return true;
}

static void hp_zx1_mio_notify_unmap(HPZX1MIOState *s,
                                    unsigned int context,
                                    const HPSBAIOMMUPurge *purge)
{
    IOMMUTLBEvent event;
    IOMMUMemoryRegion *mr = hp_zx1_mio_iommu_region(s, context);

    g_assert(purge && purge->size);
    event = (IOMMUTLBEvent) {
        .type = IOMMU_NOTIFIER_UNMAP,
        .entry = {
            .target_as = &address_space_memory,
            .iova = purge->iova,
            .translated_addr = 0,
            .addr_mask = purge->size - 1,
            .perm = IOMMU_NONE,
        },
    };

    if (mr->iommu_notify_flags & IOMMU_NOTIFIER_UNMAP) {
        memory_region_notify_iommu(mr, 0, event);
    }
}

static bool hp_zx1_mio_is_iommu_address(hwaddr addr)
{
    return addr >= HP_ZX1_MIO_IOMMU_FIRST &&
           addr < HP_ZX1_MIO_IOMMU_END;
}

static bool hp_zx1_mio_iommu_read_locked(HPZX1MIOState *s, hwaddr addr,
                                         unsigned int size, uint64_t *data)
{
    uint64_t reg_offset;

    if (size != 8 || (addr & 7) || addr > HP_ZX1_MIO_IOMMU_LAST) {
        return false;
    }

    reg_offset = addr - HP_ZX1_MIO_IOC_FUNCTION_OFFSET;
    return hp_zx1_iommu_frontend_reg_latch(
        hp_zx1_mio_iommu_context(s, hp_zx1_mio_selected_context(s)),
        reg_offset, data);
}

static bool hp_zx1_mio_contiguous_write_shape(
    hwaddr addr, uint64_t value, unsigned int size, uint64_t *base,
    uint64_t *reg_value, uint8_t *byte_enable)
{
    unsigned int lane = addr & 7;

    if ((size != 1 && size != 2 && size != 4 && size != 8) ||
        lane + size > 8) {
        return false;
    }

    *base = addr & ~UINT64_C(7);
    *reg_value = value << (lane * 8);
    *byte_enable = size == 8 ? UINT8_MAX :
                   ((1U << size) - 1) << lane;
    return true;
}

static bool hp_zx1_mio_iommu_write_locked(
    HPZX1MIOState *s, hwaddr addr, uint64_t value, unsigned int size,
    HPSBAIOMMUPurge *unmap, bool *notify)
{
    HPZX1IOMMUWriteResult result;
    uint64_t base;
    uint64_t reg_offset;
    uint64_t reg_value;
    uint8_t byte_enable;

    if (!hp_zx1_mio_contiguous_write_shape(addr, value, size, &base,
                                            &reg_value, &byte_enable) ||
        base < HP_ZX1_MIO_IOMMU_FIRST ||
        base > HP_ZX1_MIO_IOMMU_LAST) {
        return false;
    }
    reg_offset = base - HP_ZX1_MIO_IOC_FUNCTION_OFFSET;
    if (!hp_zx1_iommu_frontend_reg_write(
            hp_zx1_mio_iommu_context(s, hp_zx1_mio_selected_context(s)),
            reg_offset, reg_value, byte_enable, &result)) {
        return false;
    }

    if (reg_offset == HP_ZX1_IOC_IOMMU_PCOM) {
        if (result.purged) {
            *unmap = result.purge;
            *notify = true;
        }
    } else {
        *unmap = hp_zx1_mio_full_unmap;
        *notify = true;
    }
    return true;
}

static bool hp_zx1_mio_f1_write_locked(HPZX1MIOState *s, hwaddr addr,
                                        uint64_t value, unsigned int size)
{
    uint64_t base;
    uint64_t reg_value;
    uint8_t byte_enable;

    return hp_zx1_mio_contiguous_write_shape(
               addr, value, size, &base, &reg_value, &byte_enable) &&
           hp_zx1_mio_regs_write_be(&s->regs, base, reg_value,
                                     byte_enable);
}

static MemTxResult hp_zx1_mio_read(void *opaque, hwaddr addr,
                                   uint64_t *data, unsigned int size,
                                   MemTxAttrs attrs)
{
    HPZX1MIOState *s = opaque;
    bool ok;

    qemu_rec_mutex_lock(&s->iommu_lock);
    if (s->zx2 && hp_zx2_mio_regs_read(&s->zx2_regs, addr, size, data)) {
        ok = true;
    } else if (s->zx2 && size == 8 && addr == HP_ZX1_MIO_F0_ID) {
        *data = HP_ZX2_MIO_F0_ID_VALUE;
        ok = true;
    } else if (s->zx2 && size == 8 && addr == HP_ZX1_MIO_F1_ID) {
        *data = HP_ZX2_MIO_F1_ID_VALUE;
        ok = true;
    } else if (hp_zx1_mio_is_iommu_address(addr)) {
        ok = hp_zx1_mio_iommu_read_locked(s, addr, size, data);
    } else {
        ok = hp_zx1_mio_regs_read(&s->regs, addr, size, data);
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);

    if (!ok) {
        *data = 0;
        qemu_rec_mutex_lock(&s->iommu_lock);
        hp_zx1_mio_report_fault(s, HP_ZX1_MIO_ERROR_CSR_DECODE,
                                addr, 0, IA64_CHIPSET_FAULT_DECODE);
        qemu_rec_mutex_unlock(&s->iommu_lock);
        return MEMTX_DECODE_ERROR;
    }

    return MEMTX_OK;
}

static MemTxResult hp_zx1_mio_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned int size,
                                    MemTxAttrs attrs)
{
    HPZX1MIOState *s = opaque;
    HPSBAIOMMUPurge unmap = { 0 };
    bool notify = false;
    bool ok;

    qemu_rec_mutex_lock(&s->iommu_lock);
    if (s->zx2) {
        HPZX2MIORegs old = s->zx2_regs;

        ok = hp_zx2_mio_regs_write(&s->zx2_regs, addr, size, value);
        if (ok && (!hp_zx2_mio_regs_state_valid(&s->zx2_regs) ||
                   !hp_zx2_mio_update_frontend_groups(s))) {
            s->zx2_regs = old;
        }
    } else {
        ok = false;
    }
    if (!ok) {
        if (hp_zx1_mio_is_iommu_address(addr)) {
            ok = hp_zx1_mio_iommu_write_locked(s, addr, value, size,
                                                &unmap, &notify);
        } else if (addr >= HP_ZX1_MIO_F1_ID) {
            ok = hp_zx1_mio_f1_write_locked(s, addr, value, size);
        } else {
            ok = hp_zx1_mio_regs_write(&s->regs, addr, size, value);
        }
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);

    if (!ok) {
        qemu_rec_mutex_lock(&s->iommu_lock);
        hp_zx1_mio_report_fault(s, HP_ZX1_MIO_ERROR_CSR_DECODE,
                                addr, value, IA64_CHIPSET_FAULT_DECODE);
        qemu_rec_mutex_unlock(&s->iommu_lock);
        return MEMTX_DECODE_ERROR;
    }
    if (notify) {
        hp_zx1_mio_notify_unmap(s, hp_zx1_mio_selected_context(s),
                                &unmap);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps hp_zx1_mio_ops = {
    .read_with_attrs = hp_zx1_mio_read,
    .write_with_attrs = hp_zx1_mio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static bool hp_zx1_mio_pdir_read(void *opaque, uint64_t address,
                                 uint64_t *entry)
{
    uint8_t buffer[sizeof(*entry)];

    (void)opaque;
    if (address_space_read(&address_space_memory, address,
                           MEMTXATTRS_UNSPECIFIED, buffer,
                           sizeof(buffer)) != MEMTX_OK) {
        return false;
    }

    *entry = ldq_le_p(buffer);
    return true;
}

static IOMMUTLBEntry hp_zx1_mio_iommu_translate(
    IOMMUMemoryRegion *iommu_mr, hwaddr addr, IOMMUAccessFlags flag,
    int iommu_idx)
{
    HPZX1MIOState *s = HP_ZX1_MIO(
        memory_region_owner(MEMORY_REGION(iommu_mr)));
    unsigned int context = hp_zx1_mio_iommu_region_context(s, iommu_mr);
    HPZX1IOMMUEvictionResult eviction = { 0 };
    HPZX1IOMMUFault iommu_fault = { 0 };
    HPZX1IOMMUTranslateResult result;
    HPSBAIOMMUEntry entry;

    (void)flag;
    if (iommu_idx != 0) {
        return (IOMMUTLBEntry) { .perm = IOMMU_NONE };
    }

    qemu_rec_mutex_lock(&s->iommu_lock);
    /* The shared DMA frontend performs translations with DVI disabled. */
    result = hp_zx1_iommu_frontend_translate_ex(
        hp_zx1_mio_iommu_context(s, context), addr, false,
        hp_zx1_mio_pdir_read, s, &entry,
        &eviction, &iommu_fault);
    if (result == HP_ZX1_IOMMU_TRANSLATE_BLOCKED) {
        uint64_t information =
            ((uint64_t)iommu_fault.reason << 56) |
            (iommu_fault.pdir_address & UINT64_C(0x00ffffffffffffff));

        hp_zx1_mio_report_fault(s, HP_ZX1_MIO_ERROR_IOMMU, addr,
                                information, IA64_CHIPSET_FAULT_IOMMU);
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);

    if (result == HP_ZX1_IOMMU_TRANSLATE_BLOCKED) {
        return (IOMMUTLBEntry) { .perm = IOMMU_NONE };
    }
    if (eviction.evicted) {
        hp_zx1_mio_notify_unmap(s, context, &eviction.range);
    }

    return (IOMMUTLBEntry) {
        .target_as = &address_space_memory,
        .iova = entry.iova,
        .translated_addr = entry.translated_addr,
        .addr_mask = entry.addr_mask,
        .perm = IOMMU_RW,
    };
}

static uint64_t hp_zx1_mio_iommu_min_page_size(
    IOMMUMemoryRegion *iommu_mr)
{
    (void)iommu_mr;
    return UINT64_C(1) << 12;
}

static int hp_zx1_mio_iommu_notify_flag_changed(
    IOMMUMemoryRegion *iommu_mr, IOMMUNotifierFlag old_flags,
    IOMMUNotifierFlag new_flags, Error **errp)
{
    unsigned int unsupported = (unsigned int)new_flags &
                               ~(unsigned int)IOMMU_NOTIFIER_UNMAP;

    (void)iommu_mr;
    (void)old_flags;
    if (unsupported) {
        error_setg(errp,
                   "zx1 IOC IOMMU does not support notifier flags 0x%x",
                   unsupported);
        return -EINVAL;
    }
    return 0;
}

static int hp_zx1_mio_pci_root_slot(const HPZX1MIOState *s,
                                    const PCIBus *bus);

static PCIBus *hp_zx1_mio_pci_root_for_bus(PCIBus *bus)
{
    while (bus && !pci_bus_is_root(bus)) {
        if (!bus->parent_dev) {
            return NULL;
        }
        bus = pci_get_bus(bus->parent_dev);
    }
    return bus;
}

static bool hp_zx1_mio_pci_supports_address_space(
    PCIBus *bus, void *opaque, int devfn, Error **errp)
{
    HPZX1MIOState *s = opaque;
    PCIBus *root = hp_zx1_mio_pci_root_for_bus(bus);

    (void)devfn;
    if (!root || hp_zx1_mio_pci_root_slot(s, root) < 0) {
        error_setg(errp,
                   "PCI device is not below a root attached to this zx1 MIO");
        return false;
    }
    return true;
}

static AddressSpace *hp_zx1_mio_pci_address_space(
    PCIBus *bus, void *opaque, int devfn)
{
    HPZX1MIOState *s = opaque;
    PCIBus *root = hp_zx1_mio_pci_root_for_bus(bus);
    int slot;

    (void)devfn;
    slot = root ? hp_zx1_mio_pci_root_slot(s, root) : -1;
    g_assert(slot >= 0);
    return &s->root_frontends[slot].as;
}

static const PCIIOMMUOps hp_zx1_mio_pci_iommu_ops = {
    .supports_address_space = hp_zx1_mio_pci_supports_address_space,
    .get_address_space = hp_zx1_mio_pci_address_space,
};

static bool hp_zx1_mio_pci_bus_empty(const PCIBus *bus)
{
    unsigned int devfn;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        if (bus->devices[devfn]) {
            return false;
        }
    }
    return true;
}

static int hp_zx1_mio_pci_root_slot(const HPZX1MIOState *s,
                                    const PCIBus *bus)
{
    unsigned int slot;

    for (slot = 0; slot < HP_ZX1_MIO_LBA_PORT_COUNT; slot++) {
        if (s->root_frontends[slot].bus == bus) {
            return slot;
        }
    }
    return -1;
}

static int hp_zx1_mio_free_pci_root_slot(const HPZX1MIOState *s)
{
    return hp_zx1_mio_pci_root_slot(s, NULL);
}

static void hp_zx1_mio_frontend_set_context(HPZX1MIORootFrontend *frontend,
                                             unsigned int context)
{
    unsigned int index;

    g_assert(context < HP_ZX2_MIO_GROUP_COUNT);
    memory_region_transaction_begin();
    for (index = 0; index < HP_ZX2_MIO_GROUP_COUNT; index++) {
        memory_region_set_enabled(&frontend->iommu_alias[index],
                                  index == context);
    }
    memory_region_transaction_commit();
    frontend->context = context;
}

static bool hp_zx2_mio_update_frontend_groups(HPZX1MIOState *s)
{
    uint8_t groups[HP_ZX1_MIO_LBA_PORT_COUNT] = { 0 };
    uint8_t contexts[HP_ZX1_MIO_LBA_PORT_COUNT] = { 0 };
    unsigned int slot;

    for (slot = 0; slot < HP_ZX1_MIO_LBA_PORT_COUNT; slot++) {
        HPZX1MIORootFrontend *frontend = &s->root_frontends[slot];
        unsigned int group;
        unsigned int context;

        if (!frontend->bus) {
            continue;
        }
        if (!hp_zx2_mio_regs_group_for_ropes(
                &s->zx2_regs, frontend->ropes,
                &group, &context)) {
            return false;
        }
        groups[slot] = group;
        contexts[slot] = context;
    }

    for (slot = 0; slot < HP_ZX1_MIO_LBA_PORT_COUNT; slot++) {
        HPZX1MIORootFrontend *frontend = &s->root_frontends[slot];

        if (!frontend->bus) {
            continue;
        }
        frontend->group = groups[slot];
        hp_zx1_mio_frontend_set_context(frontend, contexts[slot]);
    }
    return true;
}

static bool hp_zx1_mio_attach_root(HPZX1MIOState *s, PCIBus *bus,
                                   HPZX1IOAState *ioa, uint16_t ropes,
                                   Error **errp)
{
    HPZX1MIORootFrontend *frontend;
    unsigned int group = 0;
    unsigned int context = 0;
    unsigned int index;
    int slot;

    if (!s) {
        error_setg(errp, "zx1 MIO IOMMU attachment requires a device");
        return false;
    }
    if (ioa && !qdev_is_realized(DEVICE(ioa))) {
        error_setg(errp,
                   "Mercury must be realized before zx1 MIO attachment");
        return false;
    }
    if (!bus) {
        error_setg(errp, "zx1 MIO IOMMU attachment requires a PCI root bus");
        return false;
    }
    if (ioa && hp_zx1_ioa_bus(ioa) != bus) {
        error_setg(errp,
                   "Mercury does not own the selected PCI root bus");
        return false;
    }
    if (ioa && ropes != hp_zx1_ioa_rope_mask(ioa)) {
        error_setg(errp, "Mercury rope ownership does not match attachment");
        return false;
    }
    if (s->zx2 && !ropes) {
        error_setg(errp, "zx2 MIOC root attachment requires a rope mask");
        return false;
    }
    if (!s->zx2 && ropes && !ioa) {
        error_setg(errp,
                   "zx1 MIO generic root attachment does not accept zx2 ropes");
        return false;
    }
    if (!s->iommu_configured || !qdev_is_realized(DEVICE(s))) {
        error_setg(errp,
                   "zx1 MIO IOMMU must be configured and realized before attachment");
        return false;
    }
    if (hp_zx1_mio_pci_root_slot(s, bus) >= 0) {
        error_setg(errp, "PCI root bus is already attached to this zx1 MIO");
        return false;
    }
    if (s->zx2) {
        for (index = 0; index < HP_ZX1_MIO_LBA_PORT_COUNT; index++) {
            frontend = &s->root_frontends[index];
            if (frontend->bus && (frontend->ropes & ropes)) {
                error_setg(errp,
                           "zx2 MIOC rope mask 0x%04x overlaps attached root mask 0x%04x",
                           ropes, frontend->ropes);
                return false;
            }
        }
    }
    if (!pci_bus_is_root(bus)) {
        error_setg(errp, "zx1 MIO IOMMU can only attach to a PCI root bus");
        return false;
    }
    if (pci_bus_bypass_iommu(bus)) {
        error_setg(errp,
                   "zx1 MIO IOMMU cannot attach to a bus that bypasses IOMMU operations");
        return false;
    }
    if (bus->iommu_ops || bus->iommu_opaque) {
        error_setg(errp, "PCI root bus already has IOMMU ownership state");
        return false;
    }
    if (!hp_zx1_mio_pci_bus_empty(bus)) {
        error_setg(errp,
                   "zx1 MIO IOMMU must attach before PCI devices are realized");
        return false;
    }
    slot = hp_zx1_mio_free_pci_root_slot(s);
    if (slot < 0) {
        error_setg(errp,
                   "zx1 MIO IOMMU has no free PCI root attachment slot");
        return false;
    }

    frontend = &s->root_frontends[slot];
    if (s->zx2 &&
        !hp_zx2_mio_regs_group_for_ropes(
            &s->zx2_regs, ropes,
            &group, &context)) {
        error_setg(errp,
                   "zx2 MIOC has no enabled group for rope mask 0x%04x",
                   ropes);
        return false;
    }
    frontend->group = group;
    hp_zx1_mio_frontend_set_context(frontend, context);
    if (ioa &&
        !hp_zx1_ioa_attach_msi_window(ioa, &frontend->root, errp)) {
        hp_zx1_mio_frontend_set_context(frontend, 0);
        frontend->group = 0;
        return false;
    }

    object_ref(OBJECT(bus));
    frontend->bus = bus;
    frontend->ioa = ioa;
    frontend->ropes = ropes;
    if (pci_bus_is_express(bus)) {
        pci_setup_iommu_per_bus(bus, &hp_zx1_mio_pci_iommu_ops, s);
    } else {
        pci_setup_iommu(bus, &hp_zx1_mio_pci_iommu_ops, s);
    }
    return true;
}

bool hp_zx1_mio_attach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                Error **errp)
{
    return hp_zx1_mio_attach_root(s, bus, NULL, 0, errp);
}

bool hp_zx2_mio_attach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                uint16_t ropes, Error **errp)
{
    if (!s || !s->zx2) {
        error_setg(errp, "zx2 MIOC root attachment requires a zx2 device");
        return false;
    }
    return hp_zx1_mio_attach_root(s, bus, NULL, ropes, errp);
}

bool hp_zx1_mio_attach_ioa(HPZX1MIOState *s, HPZX1IOAState *ioa,
                           Error **errp)
{
    return hp_zx1_mio_attach_root(s, hp_zx1_ioa_bus(ioa), ioa,
                                  hp_zx1_ioa_rope_mask(ioa), errp);
}

bool hp_zx1_mio_detach_pci_root(HPZX1MIOState *s, PCIBus *bus,
                                Error **errp)
{
    HPZX1MIORootFrontend *frontend;
    int slot;

    if (!s || !bus) {
        error_setg(errp,
                   "zx1 MIO IOMMU detach requires a device and PCI root bus");
        return false;
    }

    slot = hp_zx1_mio_pci_root_slot(s, bus);
    if (slot < 0) {
        error_setg(errp, "PCI root bus is not attached to this zx1 MIO");
        return false;
    }
    if (!hp_zx1_mio_pci_bus_empty(bus)) {
        error_setg(errp,
                   "zx1 MIO IOMMU cannot detach while PCI devices remain realized");
        return false;
    }
    if (bus->iommu_ops != &hp_zx1_mio_pci_iommu_ops ||
        bus->iommu_opaque != s) {
        error_setg(errp,
                   "zx1 MIO IOMMU does not own the PCI root bus attachment");
        return false;
    }

    frontend = &s->root_frontends[slot];
    bus->iommu_ops = NULL;
    bus->iommu_opaque = NULL;
    bus->iommu_per_bus = false;
    if (frontend->ioa) {
        hp_zx1_ioa_detach_msi_window(frontend->ioa, &frontend->root);
    }
    frontend->ioa = NULL;
    frontend->bus = NULL;
    frontend->ropes = 0;
    frontend->group = 0;
    hp_zx1_mio_frontend_set_context(frontend, 0);
    object_unref(OBJECT(bus));
    return true;
}

AddressSpace *hp_zx1_mio_iommu_address_space(HPZX1MIOState *s)
{
    return s && s->iommu_configured && qdev_is_realized(DEVICE(s)) ?
           &s->iommu_as : NULL;
}

static void hp_zx1_mio_reset_hold(Object *obj, ResetType type)
{
    HPZX1MIOState *s = HP_ZX1_MIO(obj);
    HPZX1IOMMUResetConfig config;
    bool reset;
    bool notify = false;
    unsigned int context;

    qemu_rec_mutex_lock(&s->iommu_lock);
    hp_zx1_mio_regs_reset(&s->regs);
    if (s->zx2) {
        hp_zx2_mio_regs_reset(&s->zx2_regs);
    }
    if (s->iommu_configured) {
        config = hp_zx1_mio_iommu_reset_config(s);
        reset = hp_zx1_iommu_frontend_reset(&s->iommu, &config);
        g_assert(reset);
        for (context = 1; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
            reset = hp_zx1_iommu_frontend_reset(
                &s->zx2_iommu[context - 1], &config);
            g_assert(reset);
        }
        notify = true;
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);

    if (s->zx2) {
        reset = hp_zx2_mio_update_frontend_groups(s);
        g_assert(reset);
    }

    if (notify) {
        for (context = 0;
             context < (s->zx2 ? HP_ZX2_MIO_GROUP_COUNT : 1); context++) {
            hp_zx1_mio_notify_unmap(s, context,
                                    &hp_zx1_mio_full_unmap);
        }
    }
}

static bool hp_zx1_mio_iotlb_mask_valid(uint64_t addr_mask)
{
    return addr_mask == UINT64_C(0xfff) ||
           addr_mask == UINT64_C(0x1fff) ||
           addr_mask == UINT64_C(0x3fff) ||
           addr_mask == UINT64_C(0xffff);
}

static bool hp_zx1_mio_iotlb_migration_valid(
    const HPZX1IOMMUFrontend *iommu, Error **errp)
{
    unsigned int slot_index;

    if (iommu->rr_next >= HP_ZX1_IOTLB_SLOT_COUNT) {
        error_setg(errp,
                   "zx1 MIO migration has invalid IOTLB replacement cursor %u",
                   iommu->rr_next);
        return false;
    }

    for (slot_index = 0; slot_index < HP_ZX1_IOTLB_SLOT_COUNT;
         slot_index++) {
        const HPZX1IOTLBSlot *slot =
            &iommu->iotlb.slots[slot_index];
        const HPSBAIOMMUEntry *entry = &slot->entry;

        if (!slot->valid) {
            continue;
        }
        if (!hp_zx1_mio_iotlb_mask_valid(entry->addr_mask) ||
            (entry->iova & entry->addr_mask) ||
            (entry->translated_addr & entry->addr_mask) ||
            entry->iova > HP_ZX1_IOMMU_PHYS_MASK - entry->addr_mask ||
            entry->translated_addr >
                HP_ZX1_IOMMU_PHYS_MASK - entry->addr_mask) {
            error_setg(errp,
                       "zx1 MIO migration has invalid IOTLB slot %u",
                       slot_index);
            return false;
        }
    }
    return true;
}

static bool hp_zx1_mio_post_load(void *opaque, int version_id,
                                 Error **errp)
{
    HPZX1MIOState *s = opaque;
    unsigned int context;

    if (version_id < 1 || version_id > 2 || !s->iommu_configured) {
        error_setg(errp,
                   "zx1 MIO migration destination has no IOMMU reset configuration");
        return false;
    }

    if (!hp_zx1_mio_regs_state_valid(&s->migration.regs)) {
        error_setg(errp,
                   "zx1 MIO migration has unreachable register state");
        return false;
    }
    if (s->zx2 && !hp_zx2_mio_regs_state_valid(&s->migration.zx2_regs)) {
        error_setg(errp, "zx2 MIOC migration has invalid register state");
        return false;
    }
    if (!hp_zx1_mio_iotlb_migration_valid(&s->migration.iommu, errp)) {
        return false;
    }
    if (s->zx2) {
        for (context = 1; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
            if (!hp_zx1_mio_iotlb_migration_valid(
                    &s->migration.zx2_iommu[context - 1], errp)) {
                return false;
            }
        }
    }

    qemu_rec_mutex_lock(&s->iommu_lock);
    s->regs = s->migration.regs;
    s->iommu = s->migration.iommu;
    if (s->zx2) {
        s->zx2_regs = s->migration.zx2_regs;
        memcpy(s->zx2_iommu, s->migration.zx2_iommu,
               sizeof(s->zx2_iommu));
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);

    if (s->zx2 && !hp_zx2_mio_update_frontend_groups(s)) {
        error_setg(errp, "zx2 MIOC migration cannot route an attached rope");
        return false;
    }

    for (context = 0;
         context < (s->zx2 ? HP_ZX2_MIO_GROUP_COUNT : 1); context++) {
        hp_zx1_mio_notify_unmap(s, context, &hp_zx1_mio_full_unmap);
    }
    return true;
}

static bool hp_zx1_mio_pre_save(void *opaque, Error **errp)
{
    HPZX1MIOState *s = opaque;

    if (!s->iommu_configured) {
        error_setg(errp,
                   "zx1 MIO migration source has no IOMMU reset configuration");
        return false;
    }

    qemu_rec_mutex_lock(&s->iommu_lock);
    s->migration.regs = s->regs;
    s->migration.iommu = s->iommu;
    if (s->zx2) {
        s->migration.zx2_regs = s->zx2_regs;
        memcpy(s->migration.zx2_iommu, s->zx2_iommu,
               sizeof(s->zx2_iommu));
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);
    return true;
}

static const VMStateDescription vmstate_hp_zx1_mio_iotlb_slot = {
    .name = TYPE_HP_ZX1_MIO "/iotlb-slot",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(entry.iova, HPZX1IOTLBSlot),
        VMSTATE_UINT64(entry.translated_addr, HPZX1IOTLBSlot),
        VMSTATE_UINT64(entry.addr_mask, HPZX1IOTLBSlot),
        VMSTATE_BOOL(valid, HPZX1IOTLBSlot),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hp_zx2_mio_iommu_context = {
    .name = TYPE_HP_ZX2_MIO "/iommu-context",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(ibase, HPZX1IOMMUFrontend),
        VMSTATE_UINT64(imask, HPZX1IOMMUFrontend),
        VMSTATE_UINT64(pcom, HPZX1IOMMUFrontend),
        VMSTATE_UINT64(tcnfg, HPZX1IOMMUFrontend),
        VMSTATE_UINT64(pdir_base, HPZX1IOMMUFrontend),
        VMSTATE_STRUCT_ARRAY(iotlb.slots, HPZX1IOMMUFrontend,
                             HP_ZX1_IOTLB_SLOT_COUNT, 1,
                             vmstate_hp_zx1_mio_iotlb_slot,
                             HPZX1IOTLBSlot),
        VMSTATE_UINT8(rr_next, HPZX1IOMMUFrontend),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hp_zx1_mio = {
    .name = TYPE_HP_ZX1_MIO,
    .version_id = 2,
    .minimum_version_id = 1,
    .priority = MIG_PRI_IOMMU,
    .pre_save_errp = hp_zx1_mio_pre_save,
    .post_load_errp = hp_zx1_mio_post_load,
    .fields = (const VMStateField[]) {
        /* Reject migration between differently configured IOC instances. */
        VMSTATE_UINT64_EQUAL(iommu_reset.ibase, HPZX1MIOState),
        VMSTATE_UINT64_EQUAL(iommu_reset.imask, HPZX1MIOState),
        VMSTATE_UINT64_EQUAL(iommu_reset.pcom, HPZX1MIOState),
        VMSTATE_UINT64_EQUAL(iommu_reset.tcnfg, HPZX1MIOState),
        VMSTATE_UINT64_EQUAL(iommu_reset.pdir_base, HPZX1MIOState),

        VMSTATE_UINT64(migration.iommu.ibase, HPZX1MIOState),
        VMSTATE_UINT64(migration.iommu.imask, HPZX1MIOState),
        VMSTATE_UINT64(migration.iommu.pcom, HPZX1MIOState),
        VMSTATE_UINT64(migration.iommu.tcnfg, HPZX1MIOState),
        VMSTATE_UINT64(migration.iommu.pdir_base, HPZX1MIOState),
        VMSTATE_STRUCT_ARRAY(migration.iommu.iotlb.slots, HPZX1MIOState,
                             HP_ZX1_IOTLB_SLOT_COUNT, 1,
                             vmstate_hp_zx1_mio_iotlb_slot,
                             HPZX1IOTLBSlot),
        VMSTATE_UINT8(migration.iommu.rr_next, HPZX1MIOState),
        VMSTATE_STRUCT_ARRAY(migration.zx2_iommu, HPZX1MIOState,
                             HP_ZX2_MIO_EXTRA_CONTEXTS, 2,
                             vmstate_hp_zx2_mio_iommu_context,
                             HPZX1IOMMUFrontend),

        VMSTATE_UINT64_ARRAY(migration.regs.lmmio_dir_base,
                             HPZX1MIOState, 2),
        VMSTATE_UINT64_ARRAY(migration.regs.lmmio_dir_mask,
                             HPZX1MIOState, 2),
        VMSTATE_UINT64_ARRAY(migration.regs.lmmio_dir_route,
                             HPZX1MIOState, 2),
        VMSTATE_UINT64(migration.regs.lmmio_dist_base, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.lmmio_dist_mask, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.lmmio_dist_route, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.gmmio_dist_base, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.gmmio_dist_mask, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.gmmio_dist_route, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dist_base, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dist_mask, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dist_route, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.rope_config_base, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.vga_route, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dir_base, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dir_mask, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.ios_dir_route, HPZX1MIOState),
        VMSTATE_UINT64(migration.regs.rope_config, HPZX1MIOState),
        VMSTATE_UINT64_ARRAY(migration.regs.lba_port_control,
                             HPZX1MIOState,
                             HP_ZX1_MIO_LBA_PORT_COUNT),
        VMSTATE_UINT64_V(migration.regs.error_config, HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.regs.error_status, HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.regs.error_address, HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.regs.error_information,
                         HPZX1MIOState, 2),
        VMSTATE_UINT64_ARRAY_V(migration.zx2_regs.group_ropes,
                               HPZX1MIOState, HP_ZX2_MIO_GROUP_COUNT, 2),
        VMSTATE_UINT64_ARRAY_V(migration.zx2_regs.group_control,
                               HPZX1MIOState, HP_ZX2_MIO_GROUP_COUNT, 2),
        VMSTATE_UINT64_V(migration.zx2_regs.iommu_select,
                         HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.zx2_regs.error_interrupt,
                         HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.zx2_regs.error_status,
                         HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.zx2_regs.error_address,
                         HPZX1MIOState, 2),
        VMSTATE_UINT64_V(migration.zx2_regs.error_information,
                         HPZX1MIOState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void hp_zx1_mio_realize(DeviceState *dev, Error **errp)
{
    HPZX1MIOState *s = HP_ZX1_MIO(dev);
    HPZX1IOMMUResetConfig config;
    bool reset;
    unsigned int context;

    if (!s->iommu_configured) {
        error_setg(errp,
                   "zx1 MIO device requires explicit IOMMU reset configuration");
        return;
    }

    config = hp_zx1_mio_iommu_reset_config(s);
    qemu_rec_mutex_lock(&s->iommu_lock);
    hp_zx1_mio_regs_reset(&s->regs);
    if (s->zx2) {
        hp_zx2_mio_regs_reset(&s->zx2_regs);
    }
    reset = hp_zx1_iommu_frontend_reset(&s->iommu, &config);
    g_assert(reset);
    for (context = 1; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
        reset = hp_zx1_iommu_frontend_reset(
            &s->zx2_iommu[context - 1], &config);
        g_assert(reset);
    }
    qemu_rec_mutex_unlock(&s->iommu_lock);
}

static void hp_zx1_mio_init(Object *obj)
{
    HPZX1MIOState *s = HP_ZX1_MIO(obj);
    unsigned int context;
    unsigned int slot;

    qemu_rec_mutex_init(&s->iommu_lock);
    memory_region_init_io(&s->csr, obj, &hp_zx1_mio_ops, s,
                          TYPE_HP_ZX1_MIO, HP_ZX1_MIO_CSR_SIZE);
    memory_region_init_iommu(&s->iommu_mr, sizeof(s->iommu_mr),
                             TYPE_HP_ZX1_MIO_IOMMU_MEMORY_REGION, obj,
                             TYPE_HP_ZX1_MIO ".iommu",
                             HP_ZX1_MIO_IOMMU_SIZE);
    address_space_init(&s->iommu_as, MEMORY_REGION(&s->iommu_mr),
                       TYPE_HP_ZX1_MIO ".iommu-as");
    for (context = 1; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
        g_autofree char *mr_name =
            g_strdup_printf(TYPE_HP_ZX2_MIO ".iommu-%u", context);
        g_autofree char *as_name =
            g_strdup_printf(TYPE_HP_ZX2_MIO ".iommu-as-%u", context);

        memory_region_init_iommu(&s->zx2_iommu_mr[context - 1],
                                 sizeof(s->zx2_iommu_mr[context - 1]),
                                 TYPE_HP_ZX1_MIO_IOMMU_MEMORY_REGION, obj,
                                 mr_name, HP_ZX1_MIO_IOMMU_SIZE);
        address_space_init(&s->zx2_iommu_as[context - 1],
                           MEMORY_REGION(&s->zx2_iommu_mr[context - 1]),
                           as_name);
    }
    for (slot = 0; slot < HP_ZX1_MIO_LBA_PORT_COUNT; slot++) {
        HPZX1MIORootFrontend *frontend = &s->root_frontends[slot];
        g_autofree char *root_name =
            g_strdup_printf(TYPE_HP_ZX1_MIO ".root-dma-%u", slot);
        g_autofree char *as_name =
            g_strdup_printf(TYPE_HP_ZX1_MIO ".root-dma-as-%u", slot);

        memory_region_init(&frontend->root, obj, root_name,
                           HP_ZX1_MIO_IOMMU_SIZE);
        for (context = 0; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
            g_autofree char *alias_name = g_strdup_printf(
                TYPE_HP_ZX1_MIO ".root-iommu-%u-%u", slot, context);

            memory_region_init_alias(&frontend->iommu_alias[context], obj,
                                     alias_name,
                                     MEMORY_REGION(hp_zx1_mio_iommu_region(
                                                       s, context)),
                                     0, HP_ZX1_MIO_IOMMU_SIZE);
            memory_region_set_enabled(&frontend->iommu_alias[context],
                                      context == 0);
            memory_region_add_subregion_overlap(
                &frontend->root, 0, &frontend->iommu_alias[context], 0);
        }
        address_space_init(&frontend->as, &frontend->root, as_name);
    }
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->csr);
}

static void hp_zx1_mio_finalize(Object *obj)
{
    HPZX1MIOState *s = HP_ZX1_MIO(obj);
    unsigned int context;
    unsigned int slot;

    for (slot = 0; slot < HP_ZX1_MIO_LBA_PORT_COUNT; slot++) {
        HPZX1MIORootFrontend *frontend = &s->root_frontends[slot];

        g_assert(!frontend->bus);
        g_assert(!frontend->ioa);
        g_assert(!frontend->ropes);
        address_space_remove_listeners(&frontend->as);
        address_space_destroy(&frontend->as);
        for (context = 0; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
            memory_region_del_subregion(&frontend->root,
                                        &frontend->iommu_alias[context]);
            object_unparent(OBJECT(&frontend->iommu_alias[context]));
        }
        object_unparent(OBJECT(&frontend->root));
    }
    for (context = 1; context < HP_ZX2_MIO_GROUP_COUNT; context++) {
        address_space_remove_listeners(&s->zx2_iommu_as[context - 1]);
        address_space_destroy(&s->zx2_iommu_as[context - 1]);
        object_unparent(OBJECT(&s->zx2_iommu_mr[context - 1]));
    }
    address_space_remove_listeners(&s->iommu_as);
    address_space_destroy(&s->iommu_as);
    object_unparent(OBJECT(&s->iommu_mr));
    qemu_rec_mutex_destroy(&s->iommu_lock);
}

static void hp_zx1_mio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "HP zx1 MIO CSR block (internal)";
    dc->realize = hp_zx1_mio_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_hp_zx1_mio;
    rc->phases.hold = hp_zx1_mio_reset_hold;
}

static void hp_zx2_mio_init(Object *obj)
{
    HP_ZX1_MIO(obj)->zx2 = true;
}

static void hp_zx2_mio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "HP zx2 MIOC/SBA CSR block (internal)";
}

static void hp_zx1_mio_iommu_memory_region_class_init(
    ObjectClass *klass, const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = hp_zx1_mio_iommu_translate;
    imrc->get_min_page_size = hp_zx1_mio_iommu_min_page_size;
    imrc->notify_flag_changed = hp_zx1_mio_iommu_notify_flag_changed;
}

static const TypeInfo hp_zx1_mio_iommu_memory_region_info = {
    .name = TYPE_HP_ZX1_MIO_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = hp_zx1_mio_iommu_memory_region_class_init,
};

static const TypeInfo hp_zx1_mio_info = {
    .name = TYPE_HP_ZX1_MIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HPZX1MIOState),
    .instance_init = hp_zx1_mio_init,
    .instance_finalize = hp_zx1_mio_finalize,
    .class_init = hp_zx1_mio_class_init,
};

static const TypeInfo hp_zx2_mio_info = {
    .name = TYPE_HP_ZX2_MIO,
    .parent = TYPE_HP_ZX1_MIO,
    .instance_init = hp_zx2_mio_init,
    .class_init = hp_zx2_mio_class_init,
};

static void hp_zx1_mio_register_types(void)
{
    type_register_static(&hp_zx1_mio_iommu_memory_region_info);
    type_register_static(&hp_zx1_mio_info);
    type_register_static(&hp_zx2_mio_info);
}

type_init(hp_zx1_mio_register_types)
