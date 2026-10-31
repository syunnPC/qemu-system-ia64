/*
 * Intel 7500-class VT-d 1.x hub for IA-64.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Intel 322827-001, sections 18 and 21.14; VT-d D51397-005 rev 1.3.
 */
#include "qemu/osdep.h"
#include "hw/ia64/intel_7500.h"
#include "hw/i386/intel_iommu_internal.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "system/address-spaces.h"

#define TYPE_7500_IOMMU_MEMORY "intel-7500-iommu-memory"
#define HPA_MASK ((1ULL << INTEL_7500_HAW) - 1)
#define PAGE_MASK (HPA_MASK & ~0xfffULL)
#define IOVA_MASK ((1ULL << 48) - 1)
#define FAULT_BASE 0x100
#define FAULT_NR 8
#define IVA 0x200
#define IOTLB 0x208
#define FAULT_VALID (1ULL << 63)
#define FSTS_PFO 1U
#define FSTS_PPF 2U
#define FSTS_IQE 16U
/* PFO, PPF, IQE, ICE and ITE: the fields that each raise a fault event. */
#define FSTS_EVENTS 0x73U
#define EVENT_IM (1U << 31)
#define EVENT_IP (1U << 30)

typedef struct Intel7500Unit {
    struct Intel7500State *hub;
    MemoryRegion mmio;
    uint8_t regs[INTEL_7500_UNIT_SIZE];
    uint64_t root;
    uint8_t next_fault;
    bool enabled;
} Intel7500Unit;

typedef struct Intel7500AddressSpace {
    IOMMUMemoryRegion iommu;
    AddressSpace as;
    PCIBus *bus;
    uint8_t devfn;
    Intel7500Unit *unit;
    QLIST_ENTRY(Intel7500AddressSpace) next;
} Intel7500AddressSpace;

struct Intel7500State {
    SysBusDevice parent_obj;
    bool coherent;
    Intel7500Unit units[2];
    QLIST_HEAD(, Intel7500AddressSpace) spaces;
};

static uint64_t reg64(Intel7500Unit *u, unsigned reg)
{
    return ldq_le_p(u->regs + reg);
}

static uint32_t reg32(Intel7500Unit *u, unsigned reg)
{
    return ldl_le_p(u->regs + reg);
}

static void put64(Intel7500Unit *u, unsigned reg, uint64_t val)
{
    stq_le_p(u->regs + reg, val);
}

static void put32(Intel7500Unit *u, unsigned reg, uint32_t val)
{
    stl_le_p(u->regs + reg, val);
}

static void send_event(Intel7500Unit *u, unsigned ctl, unsigned addr,
                        unsigned data)
{
    /* Itanium compatible-format message, delivered by the existing PIB. */
    uint32_t control = reg32(u, ctl) | EVENT_IP;

    if (!(control & EVENT_IM)) {
        uint64_t pa = reg32(u, addr) |
                      (uint64_t)reg32(u, addr + 4) << 32;
        address_space_stl_le(&address_space_memory, pa, reg32(u, data),
                             (MemTxAttrs) { .requester_id = 0 }, NULL);
        control &= ~EVENT_IP;
    }
    put32(u, ctl, control);
}

/*
 * VT-d 1.3, 7.3.1: signal only the first pending fault status field.
 * @old_status is FSTS before the update.
 */
static void fault_event(Intel7500Unit *u, uint32_t old_status)
{
    if (!(old_status & FSTS_EVENTS)) {
        send_event(u, DMAR_FECTL_REG, DMAR_FEADDR_REG, DMAR_FEDATA_REG);
    }
}

/* Software serviced every status field: a masked event is no longer pending. */
static void fault_status_serviced(Intel7500Unit *u)
{
    if (!(reg32(u, DMAR_FSTS_REG) & FSTS_EVENTS)) {
        put32(u, DMAR_FECTL_REG, reg32(u, DMAR_FECTL_REG) & ~EVENT_IP);
    }
}

static void update_fault_pending(Intel7500Unit *u)
{
    uint32_t status = reg32(u, DMAR_FSTS_REG) & ~FSTS_PPF;
    unsigned i;

    for (i = 0; i < FAULT_NR; i++) {
        if (reg64(u, FAULT_BASE + i * 16 + 8) & FAULT_VALID) {
            status |= FSTS_PPF;
        }
    }
    put32(u, DMAR_FSTS_REG, status);
}

static void record_fault(Intel7500AddressSpace *as, hwaddr addr,
                          IOMMUAccessFlags flags, unsigned reason, bool fpd)
{
    Intel7500Unit *u = as->unit;
    unsigned i;
    uint16_t sid = PCI_BUILD_BDF(pci_bus_num(as->bus), as->devfn);
    uint32_t old_status = reg32(u, DMAR_FSTS_REG);

    /* VT-d 1.3, 7.2.1: no faults are recorded while PFO remains Set. */
    if (fpd || (old_status & FSTS_PFO)) {
        return;
    }
    for (i = 0; i < FAULT_NR; i++) {
        uint64_t hi = reg64(u, FAULT_BASE + i * 16 + 8);
        if ((hi & FAULT_VALID) && (uint16_t)hi == sid) {
            return; /* Collapse faults from the same requester. */
        }
    }
    i = u->next_fault;
    if (reg64(u, FAULT_BASE + i * 16 + 8) & FAULT_VALID) {
        put32(u, DMAR_FSTS_REG, old_status | FSTS_PFO);
    } else {
        put64(u, FAULT_BASE + i * 16, addr & ~0xfffULL);
        put64(u, FAULT_BASE + i * 16 + 8,
              FAULT_VALID | ((flags & IOMMU_WO) ? 0 : 1ULL << 62) |
              (uint64_t)reason << 32 | sid);
        if (!(old_status & FSTS_PPF)) {
            put32(u, DMAR_FSTS_REG, (old_status & ~0xff00U) | i << 8);
        }
        u->next_fault = (i + 1) % FAULT_NR;
        update_fault_pending(u);
    }
    fault_event(u, old_status);
}

static bool load_entry(hwaddr pa, uint64_t *lo, uint64_t *hi)
{
    MemTxResult result;

    if (pa > HPA_MASK - (hi ? 15 : 7)) {
        return false;
    }
    *lo = address_space_ldq_le(&address_space_memory, pa,
                               MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        return false;
    }
    if (hi) {
        *hi = address_space_ldq_le(&address_space_memory, pa + 8,
                                   MEMTXATTRS_UNSPECIFIED, &result);
    }
    return result == MEMTX_OK;
}

static IOMMUTLBEntry translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                IOMMUAccessFlags flags, int idx)
{
    Intel7500AddressSpace *as = container_of(mr, Intel7500AddressSpace, iommu);
    Intel7500Unit *u = as->unit;
    IOMMUTLBEntry e = {
        .target_as = &address_space_memory,
        .iova = addr & ~0xfffULL,
        .addr_mask = 0xfff,
        .perm = IOMMU_NONE,
    };
    uint64_t lo, hi, table;
    unsigned reason, level, tt;
    bool fpd = false;
    IOMMUAccessFlags perm = IOMMU_RW;

    /* Compatible Itanium interrupts bypass DMA translation (rev 1.3, 5.4). */
    if (addr >= 0xfee00000 && addr <= 0xfeefffff) {
        if (flags & IOMMU_WO) {
            e.translated_addr = e.iova;
            e.perm = IOMMU_WO;
            return e;
        }
        reason = VTD_FR_INTERRUPT_ADDR;
        goto fault;
    }
    /* Protected memory is checked even with TE clear. */
    if (reg32(u, DMAR_PMEN_REG) & 1) {
        hwaddr low = reg32(u, DMAR_PLMBASE_REG) & ~0xfffffULL;
        hwaddr low_end = reg32(u, DMAR_PLMLIMIT_REG) | 0xfffffULL;
        hwaddr high = reg64(u, DMAR_PHMBASE_REG) & ~0xfffffULL;
        hwaddr high_end = reg64(u, DMAR_PHMLIMIT_REG) | 0xfffffULL;
        if ((addr >= low && addr <= low_end) ||
            (addr >= high && addr <= high_end)) {
            return e; /* Protected-memory blocks do not record faults. */
        }
    }
    if (!u->enabled) {
        if (addr > HPA_MASK) {
            reason = VTD_FR_ADDR_BEYOND_MGAW;
            goto fault;
        }
        e.translated_addr = e.iova;
        e.perm = IOMMU_RW;
        return e;
    }
    reason = VTD_FR_ROOT_TABLE_INV;
    if (!load_entry(u->root + pci_bus_num(as->bus) * 16, &lo, &hi)) {
        goto fault;
    }
    reason = VTD_FR_ROOT_ENTRY_P;
    if (!(lo & 1)) {
        goto fault;
    }
    reason = VTD_FR_ROOT_ENTRY_RSVD;
    if (hi || (lo & ~(PAGE_MASK | 1))) {
        goto fault;
    }
    table = lo & PAGE_MASK;
    reason = VTD_FR_CONTEXT_TABLE_INV;
    if (!load_entry(table + as->devfn * 16, &lo, &hi)) {
        goto fault;
    }
    fpd = lo & 2;
    reason = VTD_FR_CONTEXT_ENTRY_P;
    if (!(lo & 1)) {
        goto fault;
    }
    reason = VTD_FR_CONTEXT_ENTRY_RSVD;
    if ((lo & ~(PAGE_MASK | 0xf)) || (hi & ~0xff7fULL)) {
        goto fault;
    }
    tt = (lo >> 2) & 3;
    reason = VTD_FR_CONTEXT_ENTRY_TT;
    /* ATS is disabled by virtual firmware; TT=1 is consequently illegal. */
    if (tt == 1 || tt == 3) {
        goto fault;
    }
    reason = VTD_FR_CONTEXT_ENTRY_INV;
    /* VT-d 1.3, 9.2: pass-through also requires the largest supported AGAW. */
    if ((hi & 7) != 2) { /* AGAW=48, four levels only. */
        goto fault;
    }
    if (tt == 2) {
        if (addr > HPA_MASK) {
            reason = VTD_FR_ADDR_BEYOND_MGAW;
            goto fault;
        }
        e.translated_addr = e.iova;
        e.perm = IOMMU_RW;
        return e;
    }
    reason = VTD_FR_ADDR_BEYOND_MGAW;
    if (addr > IOVA_MASK) {
        goto fault;
    }
    table = lo & PAGE_MASK;
    for (level = 4; level; level--) {
        reason = VTD_FR_PAGING_ENTRY_INV;
        if (!load_entry(table + ((addr >> (12 + 9 * (level - 1))) & 511) * 8,
                         &lo, NULL)) {
            goto fault;
        }
        perm &= lo & 3;
        reason = flags & IOMMU_WO ? VTD_FR_WRITE : VTD_FR_READ;
        if (!(perm & flags)) {
            goto fault;
        }
        reason = VTD_FR_PAGING_ENTRY_RSVD;
        /* No superpages; SNP is supported on unit 0 only. */
        if (lo & ~(PAGE_MASK | 0xbff000000000007fULL |
                   (7ULL << 8) | (level == 1 ? (1ULL << 7) | (1ULL << 62) : 0) |
                   ((level == 1 && u == &u->hub->units[0]) ? 1ULL << 11 : 0))) {
            goto fault;
        }
        table = lo & PAGE_MASK;
    }
    reason = VTD_FR_INTERRUPT_ADDR;
    if (table >= 0xfee00000 && table <= 0xfeefffff) {
        goto fault;
    }
    e.translated_addr = table;
    e.perm = perm;
    return e;
fault:
    record_fault(as, addr, flags, reason, fpd);
    return e;
}

static void invalidate(Intel7500Unit *u)
{
    Intel7500AddressSpace *as;
    IOMMUTLBEvent event = {
        .type = IOMMU_NOTIFIER_UNMAP,
        .entry = {
            .target_as = &address_space_memory,
            .iova = 0,
            .addr_mask = UINT64_MAX,
            .perm = IOMMU_NONE,
        },
    };

    /* No internal cache: all invalidations may legally complete globally. */
    QLIST_FOREACH(as, &u->hub->spaces, next) {
        if (as->unit == u) {
            memory_region_notify_iommu(&as->iommu, 0, event);
        }
    }
}

static void queue_fault(Intel7500Unit *u)
{
    uint32_t old = reg32(u, DMAR_FSTS_REG);

    put32(u, DMAR_FSTS_REG, old | FSTS_IQE);
    fault_event(u, old);
}

static void process_queue(Intel7500Unit *u)
{
    uint64_t iqa = reg64(u, DMAR_IQA_REG);
    unsigned size = 4096U << (iqa & 7);
    unsigned head = reg64(u, DMAR_IQH_REG);
    unsigned tail = reg64(u, DMAR_IQT_REG);

    if (!(reg32(u, DMAR_GSTS_REG) & VTD_GCMD_QIE) ||
        (reg32(u, DMAR_FSTS_REG) & FSTS_IQE)) {
        return;
    }
    if (tail >= size) {
        queue_fault(u);
        return;
    }
    while (head != tail) {
        uint64_t lo, hi;
        unsigned type;
        if (!load_entry((iqa & PAGE_MASK) + head, &lo, &hi)) {
            queue_fault(u);
            return;
        }
        type = lo & 15;
        switch (type) {
        case 1: /* Context-cache invalidation */
        case 2: /* IOTLB invalidation */
            if (((lo >> 4) & 3) == 0 ||
                (type == 1 && (hi || (lo & VTD_INV_DESC_CC_RSVD))) ||
                (type == 2 && ((lo & VTD_INV_DESC_IOTLB_RSVD_LO) ||
                               (hi & VTD_INV_DESC_IOTLB_RSVD_HI))) ||
                (type == 2 && (hi & 63) > 9)) {
                queue_fault(u);
                return;
            }
            invalidate(u);
            break;
        case 5: /* Invalidation wait */
            if ((lo & VTD_INV_DESC_WAIT_RSVD_LO(0)) ||
                (hi & VTD_INV_DESC_WAIT_RSVD_HI)) {
                queue_fault(u);
                return;
            }
            if (lo & (1U << 5)) {
                MemTxResult result;
                if ((hi & 3) || hi > HPA_MASK - 3) {
                    queue_fault(u);
                    return;
                }
                address_space_stl_le(&address_space_memory, hi, lo >> 32,
                                     MEMTXATTRS_UNSPECIFIED, &result);
                if (result != MEMTX_OK) {
                    queue_fault(u);
                    return;
                }
            }
            /* VT-d 1.3, 6.2.2.6: coalesce completions until IWC is serviced. */
            if ((lo & VTD_INV_DESC_WAIT_IF) &&
                !(reg32(u, DMAR_ICS_REG) & VTD_ICS_IWC)) {
                put32(u, DMAR_ICS_REG, reg32(u, DMAR_ICS_REG) | VTD_ICS_IWC);
                send_event(u, DMAR_IECTL_REG, DMAR_IEADDR_REG,
                             DMAR_IEDATA_REG);
            }
            break;
        default:
            queue_fault(u);
            return;
        }
        head = (head + 16) % size;
        put64(u, DMAR_IQH_REG, head);
    }
}

static uint64_t mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Intel7500Unit *u = opaque;

    if (addr == DMAR_GCMD_REG) {
        return 0; /* Write-only. */
    }
    return size == 8 ? reg64(u, addr) : reg32(u, addr);
}

static void mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    Intel7500Unit *u = opaque;
    unsigned reg = addr & ~7U;
    unsigned shift = (addr & 7) * 8;
    uint64_t bits = size == 8 ? UINT64_MAX : 0xffffffffULL << shift;
    uint64_t data = val << shift;
    uint64_t merged = (reg64(u, reg) & ~bits) | data;
    uint64_t mask = 0;

    if (addr == DMAR_GCMD_REG) {
        uint32_t status = reg32(u, DMAR_GSTS_REG);
        if (val & VTD_GCMD_SRTP) {
            u->root = reg64(u, DMAR_RTADDR_REG) & PAGE_MASK;
            status |= VTD_GCMD_SRTP;
        }
        u->enabled = !!(val & VTD_GCMD_TE);
        status = (status & ~VTD_GCMD_TE) | (val & VTD_GCMD_TE);
        if ((val & VTD_GCMD_QIE) ||
            reg64(u, DMAR_IQH_REG) == reg64(u, DMAR_IQT_REG)) {
            status = (status & ~VTD_GCMD_QIE) | (val & VTD_GCMD_QIE);
        }
        /* VT-d 1.3, 10.4.22: IQH reads zero whenever QIES is Clear. */
        if (!(status & VTD_GCMD_QIE)) {
            put64(u, DMAR_IQH_REG, 0);
        }
        put32(u, DMAR_GSTS_REG, status);
        invalidate(u);
        process_queue(u);
        return;
    }
    if (reg >= FAULT_BASE && reg < FAULT_BASE + 16 * FAULT_NR) {
        if ((reg & 15) == 8 && (data & FAULT_VALID)) {
            put64(u, reg, reg64(u, reg) & ~FAULT_VALID);
            update_fault_pending(u);
            fault_status_serviced(u);
        }
        return;
    }
    switch (reg) {
    case DMAR_RTADDR_REG:
        mask = PAGE_MASK;
        break;
    case DMAR_CCMD_REG:
        if (!(reg32(u, DMAR_GSTS_REG) & VTD_GCMD_QIE)) {
            mask = 0xe0000003ffffffffULL;
        }
        break;
    case DMAR_FSTS_REG & ~7:
        if (addr == DMAR_FSTS_REG) {
            put32(u, addr, reg32(u, addr) & ~(val & (FSTS_PFO | FSTS_IQE)));
            fault_status_serviced(u);
            process_queue(u);
        }
        return;
    case DMAR_FECTL_REG:
        if (addr == DMAR_FECTL_REG) {
            put32(u, addr, (reg32(u, addr) & EVENT_IP) | (val & EVENT_IM));
            if (size == 8) {
                put32(u, DMAR_FEDATA_REG, (val >> 32) & 0xffff);
            }
            if (!(val & EVENT_IM) && (reg32(u, addr) & EVENT_IP)) {
                send_event(u, addr, DMAR_FEADDR_REG, DMAR_FEDATA_REG);
            }
        } else {
            put32(u, addr, val & 0xffff);
        }
        return;
    case DMAR_FEADDR_REG:
        mask = 0xfffffffffffffffcULL;
        break;
    case DMAR_PMEN_REG & ~7:
        if (addr == DMAR_PMEN_REG) {
            put32(u, addr, (val & (1U << 31)) | ((val >> 31) & 1));
            invalidate(u);
        }
        return;
    case DMAR_PLMBASE_REG:
        mask = 0xfff00000fff00000ULL;
        break;
    case DMAR_PHMBASE_REG:
    case DMAR_PHMLIMIT_REG:
        mask = HPA_MASK & ~0xfffffULL;
        break;
    case DMAR_IQT_REG:
        mask = 0x7fff0;
        break;
    case DMAR_IQA_REG:
        if (!(reg32(u, DMAR_GSTS_REG) & VTD_GCMD_QIE)) {
            mask = PAGE_MASK | 7;
            put64(u, DMAR_IQH_REG, 0);
        }
        break;
    case DMAR_ICS_REG & ~7:
        if (addr == DMAR_ICS_REG) {
            put32(u, addr, reg32(u, addr) & ~(val & VTD_ICS_IWC));
            if (!(reg32(u, addr) & VTD_ICS_IWC)) {
                put32(u, DMAR_IECTL_REG, reg32(u, DMAR_IECTL_REG) & ~EVENT_IP);
            }
        }
        return;
    case DMAR_IECTL_REG & ~7:
        if (addr == DMAR_IECTL_REG) {
            if (size == 8) {
                put32(u, DMAR_IEDATA_REG, (val >> 32) & 0xffff);
            }
            put32(u, addr, (reg32(u, addr) & EVENT_IP) | (val & EVENT_IM));
            if (!(val & EVENT_IM) && (reg32(u, addr) & EVENT_IP)) {
                send_event(u, addr, DMAR_IEADDR_REG, DMAR_IEDATA_REG);
            }
        } else {
            put32(u, addr, val & 0xffff);
        }
        return;
    case DMAR_IEADDR_REG:
        mask = 0xfffffffffffffffcULL;
        break;
    case IVA:
        mask = PAGE_MASK | 0x7f;
        break;
    case IOTLB:
        if (!(reg32(u, DMAR_GSTS_REG) & VTD_GCMD_QIE)) {
            mask = 0xb003ffff00000000ULL;
        }
        break;
    }
    if (!mask) {
        return;
    }
    put64(u, reg, merged & mask);
    if (reg == DMAR_CCMD_REG && (reg64(u, reg) & (1ULL << 63))) {
        unsigned gran = (reg64(u, reg) >> 61) & 3;
        if (gran) {
            invalidate(u);
        }
        put64(u, reg, (reg64(u, reg) & ~((1ULL << 63) | (3ULL << 59))) |
                      (gran ? 1ULL << 59 : 0)); /* Global actual granularity. */
    }
    if (reg == IOTLB && (reg64(u, reg) & (1ULL << 63))) {
        unsigned gran = (reg64(u, reg) >> 60) & 3;
        if (gran && (reg64(u, IVA) & 63) <= 9) {
            invalidate(u);
        } else {
            gran = 0;
        }
        put64(u, reg, (reg64(u, reg) & ~(0x86ULL << 56)) |
                      (gran ? 1ULL << 57 : 0));
    }
    if (reg == DMAR_IQT_REG) {
        process_queue(u);
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = mmio_read,
    .write = mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl = { .min_access_size = 4, .max_access_size = 8 },
};

static AddressSpace *dma_space(PCIBus *bus, void *opaque, int devfn)
{
    Intel7500State *s = opaque;
    Intel7500AddressSpace *as;
    PCIBus *root = bus;
    unsigned root_devfn = devfn;
    unsigned unit;

    while (!pci_bus_is_root(root)) {
        root_devfn = root->parent_dev->devfn;
        root = pci_get_bus(root->parent_dev);
    }
    unit = PCI_SLOT(root_devfn) == INTEL_7500_SECONDARY_SLOT;
    QLIST_FOREACH(as, &s->spaces, next) {
        if (as->bus == bus && as->devfn == devfn) {
            return &as->as;
        }
    }
    as = g_new0(Intel7500AddressSpace, 1);
    as->bus = bus;
    as->devfn = devfn;
    as->unit = &s->units[unit];
    memory_region_init_iommu(&as->iommu, sizeof(as->iommu),
                             TYPE_7500_IOMMU_MEMORY, OBJECT(s),
                             "7500-dma", UINT64_MAX);
    address_space_init(&as->as, MEMORY_REGION(&as->iommu), "7500-dma");
    QLIST_INSERT_HEAD(&s->spaces, as, next);
    return &as->as;
}

static const PCIIOMMUOps pci_iommu_ops = { .get_address_space = dma_space };

static void reset(DeviceState *dev)
{
    Intel7500State *s = INTEL_7500_IOMMU(dev);
    unsigned i;

    for (i = 0; i < 2; i++) {
        Intel7500Unit *u = &s->units[i];
        memset(u->regs, 0, sizeof(u->regs));
        u->root = 0;
        u->next_fault = 0;
        u->enabled = false;
        put32(u, DMAR_VER_REG, 0x10);
        /* NFR is encoded as count-1; SAGAW bit 2 means four levels. */
        put64(u, DMAR_CAP_REG, (3ULL << 54) | (9ULL << 48) |
              ((FAULT_NR - 1ULL) << 40) | (1ULL << 39) |
              (0x10ULL << 24) | (47ULL << 16) | (4ULL << 8) | 0x62);
        /* ATS and interrupt remapping are unavailable. */
        put64(u, DMAR_ECAP_REG, (0x20ULL << 8) |
              (i == 0 ? 1ULL << 7 : 0) | 0x62 | s->coherent);
        put32(u, DMAR_FECTL_REG, EVENT_IM);
        put32(u, DMAR_IECTL_REG, EVENT_IM);
        invalidate(u);
    }
}

static void instance_init(Object *obj)
{
    Intel7500State *s = INTEL_7500_IOMMU(obj);
    unsigned i;

    QLIST_INIT(&s->spaces);
    for (i = 0; i < 2; i++) {
        Intel7500Unit *u = &s->units[i];
        u->hub = s;
        memory_region_init_io(&u->mmio, obj, &mmio_ops, u,
                              "7500-vtd", INTEL_7500_UNIT_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &u->mmio);
    }
}

/* No stable migration format; refuse migration and snapshots. */
static const VMStateDescription vmstate_hub = {
    .name = TYPE_INTEL_7500_IOMMU,
    .unmigratable = 1,
};

static const Property properties[] = {
    DEFINE_PROP_BOOL("coherent", Intel7500State, coherent, false),
};

static void class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->vmsd = &vmstate_hub;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, reset);
    device_class_set_props(dc, properties);
}

static int notify_flags(IOMMUMemoryRegion *mr, IOMMUNotifierFlag old,
                         IOMMUNotifierFlag flags, Error **errp)
{
    if (flags & IOMMU_NOTIFIER_MAP) {
        error_setg(errp, "Intel 7500 supports emulated DMA only; "
                   "external mapping notifiers are unavailable");
        return -EINVAL;
    }
    return 0;
}

static void iommu_class_init(ObjectClass *klass, const void *data)
{
    IOMMU_MEMORY_REGION_CLASS(klass)->translate = translate;
    IOMMU_MEMORY_REGION_CLASS(klass)->notify_flag_changed = notify_flags;
}

static const TypeInfo types[] = {
    {
        .name = TYPE_INTEL_7500_IOMMU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Intel7500State),
        .instance_init = instance_init,
        .class_init = class_init,
    }, {
        .name = TYPE_7500_IOMMU_MEMORY,
        .parent = TYPE_IOMMU_MEMORY_REGION,
        .class_init = iommu_class_init,
    },
};
DEFINE_TYPES(types)

bool intel_7500_create(Object *parent, PCIBus *bus, hwaddr base,
                       bool coherent, Error **errp)
{
    DeviceState *dev = qdev_new(TYPE_INTEL_7500_IOMMU);
    object_property_add_child(parent, "ioh7500", OBJECT(dev));
    qdev_prop_set_bit(dev, "coherent", coherent);
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, base + INTEL_7500_UNIT_SIZE);
    pci_setup_iommu(bus, &pci_iommu_ops, dev);
    return true;
}
