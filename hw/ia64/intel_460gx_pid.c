/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Intel 460GX Programmable Interrupt Device (PID) core.
 *
 * The PID is a 64-input SAPIC with select, window, and EOI MMIO registers.
 * PCI identity and MMIO placement are supplied by the machine.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/bitops.h"
#include "hw/core/qdev-properties.h"
#include "hw/ia64/intel_460gx_pid.h"
#include "migration/vmstate.h"
#include "cpu.h"

#define PID_IOREGSEL                  0x00
#define PID_IOWIN                     0x10
#define PID_EOI                       0x40
#define PID_MMIO_SIZE                 0x1000

#define PID_REG_ID                    0x00
#define PID_REG_VERSION               0x01
#define PID_REG_ARB_ID                0x02
#define PID_RTE_BASE                  0x10

#define PID_VERSION_SAPIC             0x21
#define PID_ID_DELIVERY_SAPIC         BIT(15)

#define RTE_VECTOR_MASK               UINT64_C(0x00000000000000ff)
#define RTE_DELIVERY_MODE             UINT64_C(0x0000000000000700)
#define RTE_DELIVERY_STATUS           UINT64_C(0x0000000000001000)
#define RTE_POLARITY_LOW              UINT64_C(0x0000000000002000)
#define RTE_REMOTE_IRR                UINT64_C(0x0000000000004000)
#define RTE_TRIGGER_LEVEL             UINT64_C(0x0000000000008000)
#define RTE_MASKED                    UINT64_C(0x0000000000010000)
#define RTE_FLUSH_ENABLE              UINT64_C(0x0000000000020000)
#define RTE_DESTINATION               UINT64_C(0xffff000000000000)

#define RTE_READ_ONLY                 (RTE_DELIVERY_STATUS | RTE_REMOTE_IRR)
#define RTE_WRITABLE                  (RTE_VECTOR_MASK | RTE_DELIVERY_MODE | \
                                       RTE_POLARITY_LOW | RTE_TRIGGER_LEVEL | \
                                       RTE_MASKED | RTE_FLUSH_ENABLE | \
                                       RTE_DESTINATION)

#define PID_TEST_MMIO_UNMAPPED        UINT64_MAX

struct Intel460GXPIDState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint64_t rte[INTEL_460GX_PID_NUM_PINS];
    uint8_t irq_level[INTEL_460GX_PID_NUM_PINS];
    uint8_t mask_latched[INTEL_460GX_PID_NUM_PINS];
    uint8_t reg_select;
    uint8_t apic_id;
    uint8_t apic_arb_id;

    /* Board wiring, not guest-visible PID state. */
    uint8_t initial_id;
    uint16_t initial_id_migration;
    uint32_t legacy_pin;

    /* qtest MMIO alias; boards map the SysBus region. */
    uint64_t test_mmio_base;
};

static unsigned pid_delivery_mode(uint64_t rte)
{
    return (rte & RTE_DELIVERY_MODE) >> 8;
}

static bool pid_level_route(uint64_t rte)
{
    unsigned delivery = pid_delivery_mode(rte);

    /* NMI and ExtINT delivery is edge-triggered. */
    return (delivery == IA64_SAPIC_DELIVERY_INT ||
            delivery == IA64_SAPIC_DELIVERY_INT_REDIRECT) &&
           (rte & RTE_TRIGGER_LEVEL);
}

static bool pid_pin_active(const Intel460GXPIDState *s, unsigned pin)
{
    return s->irq_level[pin] != 0;
}

static void pid_sync_level_status(Intel460GXPIDState *s, unsigned pin);

static void pid_update(Intel460GXPIDState *s, unsigned pin)
{
    uint64_t rte = s->rte[pin];
    IA64SapicDeliveryMode delivery =
        (IA64SapicDeliveryMode)pid_delivery_mode(rte);
    uint8_t id = rte >> 56;
    uint8_t eid = rte >> 48;
    bool level_route = pid_level_route(rte);

    if ((rte & RTE_MASKED) ||
        (level_route && !pid_pin_active(s, pin) &&
         !s->mask_latched[pin])) {
        return;
    }

    if (level_route) {
        if (rte & RTE_REMOTE_IRR) {
            return;
        }
        s->rte[pin] |= RTE_REMOTE_IRR;
    }

    if (!ia64_sapic_deliver(IA64_SAPIC_DESTINATION_PHYSICAL,
                            id, eid, false, delivery,
                            rte & RTE_VECTOR_MASK)) {
        if (level_route) {
            s->rte[pin] &= ~RTE_REMOTE_IRR;
        }
        return;
    }

    if (level_route) {
        s->mask_latched[pin] = 0;
        pid_sync_level_status(s, pin);
    } else {
        /* Edge delivery completed successfully. */
        s->rte[pin] &= ~RTE_DELIVERY_STATUS;
    }
}

static void pid_sync_level_status(Intel460GXPIDState *s, unsigned pin)
{
    /* A value latched before masking is held until unmasked and delivered. */
    if (s->rte[pin] & RTE_MASKED) {
        if (s->mask_latched[pin] &&
            !(s->rte[pin] & RTE_REMOTE_IRR)) {
            s->rte[pin] |= RTE_DELIVERY_STATUS;
        } else {
            s->rte[pin] &= ~RTE_DELIVERY_STATUS;
        }
        return;
    }
    if (s->mask_latched[pin] || pid_pin_active(s, pin)) {
        s->rte[pin] |= RTE_DELIVERY_STATUS;
    } else {
        s->rte[pin] &= ~RTE_DELIVERY_STATUS;
    }
}

static void pid_set_pin(Intel460GXPIDState *s, unsigned pin, int level)
{
    bool old_active;
    bool new_active;
    bool level_route;

    if (pin >= INTEL_460GX_PID_NUM_PINS) {
        return;
    }

    old_active = pid_pin_active(s, pin);
    s->irq_level[pin] = !!level;
    new_active = pid_pin_active(s, pin);
    level_route = pid_level_route(s->rte[pin]);

    if (level_route) {
        pid_sync_level_status(s, pin);
        if (new_active) {
            pid_update(s, pin);
        }
    } else if (new_active && !old_active &&
               !(s->rte[pin] & RTE_MASKED)) {
        s->rte[pin] |= RTE_DELIVERY_STATUS;
        pid_update(s, pin);
    }
}

static void pid_irq_handler(void *opaque, int pin, int level)
{
    pid_set_pin(opaque, pin, level);
}

static void pid_legacy_handler(void *opaque, int line, int level)
{
    Intel460GXPIDState *s = opaque;

    if (s->legacy_pin != INTEL_460GX_PID_LEGACY_PIN_DISCONNECTED) {
        pid_set_pin(s, s->legacy_pin, level);
    }
}

static void pid_rte_write(Intel460GXPIDState *s, unsigned pin,
                          uint32_t value, bool high)
{
    uint64_t old = s->rte[pin];
    uint64_t candidate;
    bool old_level = pid_level_route(old);
    bool old_masked = old & RTE_MASKED;

    if (high) {
        candidate = (old & UINT64_C(0x00000000ffffffff)) |
                    ((uint64_t)value << 32);
    } else {
        candidate = (old & UINT64_C(0xffffffff00000000)) | value;
    }

    s->rte[pin] = (candidate & RTE_WRITABLE) | (old & RTE_READ_ONLY);
    if ((s->rte[pin] & RTE_MASKED) && !old_masked &&
        (old & RTE_DELIVERY_STATUS) && !(old & RTE_REMOTE_IRR)) {
        s->mask_latched[pin] = 1;
    }
    if (pid_level_route(s->rte[pin])) {
        if (!old_level) {
            bool retain_masked = (old & RTE_DELIVERY_STATUS) &&
                ((old | s->rte[pin]) & RTE_MASKED);

            /* An unmasked edge is not a sampled level assertion. */
            s->rte[pin] &= ~(RTE_DELIVERY_STATUS | RTE_REMOTE_IRR);
            s->mask_latched[pin] = retain_masked;
        }
        pid_sync_level_status(s, pin);
        pid_update(s, pin);
    } else {
        /* RIRR is undefined for edge routes; expose the stable zero value. */
        s->rte[pin] &= ~RTE_REMOTE_IRR;
        if (old_level && !s->mask_latched[pin]) {
            /* A sampled level is not an edge pending-delivery latch. */
            s->rte[pin] &= ~RTE_DELIVERY_STATUS;
        }
        s->mask_latched[pin] = 0;
        if (s->rte[pin] & RTE_DELIVERY_STATUS) {
            /* Retry a latched edge after mask or destination changes. */
            pid_update(s, pin);
        }
    }
}

static void pid_eoi(Intel460GXPIDState *s, uint8_t vector)
{
    unsigned pin;

    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        if (!pid_level_route(s->rte[pin]) ||
            (s->rte[pin] & RTE_VECTOR_MASK) != vector ||
            !(s->rte[pin] & RTE_REMOTE_IRR)) {
            continue;
        }

        s->rte[pin] &= ~RTE_REMOTE_IRR;
        pid_update(s, pin);
    }
}

static uint64_t pid_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Intel460GXPIDState *s = opaque;
    uint32_t index;

    switch (addr) {
    case PID_IOREGSEL:
        return s->reg_select;
    case PID_IOWIN:
        index = s->reg_select;
        if (index == PID_REG_ID) {
            return ((uint32_t)s->apic_id << 24) |
                   PID_ID_DELIVERY_SAPIC;
        }
        if (index == PID_REG_VERSION) {
            return ((INTEL_460GX_PID_NUM_PINS - 1) << 16) |
                   PID_VERSION_SAPIC;
        }
        if (index == PID_REG_ARB_ID) {
            return (uint32_t)s->apic_arb_id << 24;
        }
        if (index >= PID_RTE_BASE &&
            index < PID_RTE_BASE + INTEL_460GX_PID_NUM_PINS * 2) {
            unsigned pin = (index - PID_RTE_BASE) / 2;

            if ((index - PID_RTE_BASE) & 1) {
                return s->rte[pin] >> 32;
            }
            return s->rte[pin];
        }
        /* Reserved selectors return zero. */
        return 0;
    case PID_EOI:
        /* EOI reads return zero. */
        return 0;
    default:
        return 0;
    }
}

static void pid_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size)
{
    Intel460GXPIDState *s = opaque;
    uint32_t index;

    switch (addr) {
    case PID_IOREGSEL:
        /* The register is 32 bits wide but only bits 7:0 exist. */
        s->reg_select = value;
        break;
    case PID_IOWIN:
        index = s->reg_select;
        if (index == PID_REG_ID) {
            s->apic_id = (value >> 24) & 0xf;
            s->apic_arb_id = s->apic_id;
        } else if (index >= PID_RTE_BASE &&
                   index < PID_RTE_BASE +
                           INTEL_460GX_PID_NUM_PINS * 2) {
            unsigned pin = (index - PID_RTE_BASE) / 2;

            pid_rte_write(s, pin, value, (index - PID_RTE_BASE) & 1);
        }
        break;
    case PID_EOI:
        pid_eoi(s, value);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pid_mmio_ops = {
    .read = pid_mmio_read,
    .write = pid_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pid_reset(DeviceState *dev)
{
    Intel460GXPIDState *s = INTEL_460GX_PID(dev);
    unsigned pin;

    memset(s->rte, 0, sizeof(s->rte));
    memset(s->irq_level, 0, sizeof(s->irq_level));
    memset(s->mask_latched, 0, sizeof(s->mask_latched));
    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        s->rte[pin] = RTE_MASKED;
    }
    s->reg_select = 0;
    s->apic_id = s->initial_id;
    s->apic_arb_id = s->initial_id;
}

static int pid_post_load(void *opaque, int version_id)
{
    Intel460GXPIDState *s = opaque;
    unsigned pin;

    s->apic_id &= 0xf;
    s->apic_arb_id &= 0xf;
    for (pin = 0; pin < INTEL_460GX_PID_NUM_PINS; pin++) {
        s->rte[pin] &= RTE_WRITABLE | RTE_READ_ONLY;
        if (pid_level_route(s->rte[pin])) {
            s->mask_latched[pin] = !!s->mask_latched[pin] &&
                (s->rte[pin] & RTE_DELIVERY_STATUS) &&
                !(s->rte[pin] & RTE_REMOTE_IRR);
            pid_sync_level_status(s, pin);
            pid_update(s, pin);
        } else {
            s->mask_latched[pin] = 0;
            s->rte[pin] &= ~RTE_REMOTE_IRR;
        }
    }
    return 0;
}

static const VMStateDescription vmstate_intel_460gx_pid = {
    .name = TYPE_INTEL_460GX_PID,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = pid_post_load,
    .fields = (const VMStateField[]) {
        /* Reject a v2 stream before overwriting any mutable PID state. */
        VMSTATE_UINT16_EQUAL_V(initial_id_migration,
                               Intel460GXPIDState, 2),
        VMSTATE_UINT32_EQUAL_V(legacy_pin, Intel460GXPIDState, 2),
        VMSTATE_UINT64_EQUAL_V(test_mmio_base, Intel460GXPIDState, 2),
        VMSTATE_UINT64_ARRAY(rte, Intel460GXPIDState,
                             INTEL_460GX_PID_NUM_PINS),
        VMSTATE_UINT8_ARRAY(irq_level, Intel460GXPIDState,
                            INTEL_460GX_PID_NUM_PINS),
        VMSTATE_UINT8_ARRAY(mask_latched, Intel460GXPIDState,
                            INTEL_460GX_PID_NUM_PINS),
        VMSTATE_UINT8(reg_select, Intel460GXPIDState),
        VMSTATE_UINT8(apic_id, Intel460GXPIDState),
        VMSTATE_UINT8(apic_arb_id, Intel460GXPIDState),
        VMSTATE_END_OF_LIST()
    },
};

static void pid_realize(DeviceState *dev, Error **errp)
{
    Intel460GXPIDState *s = INTEL_460GX_PID(dev);

    if (s->initial_id > INTEL_460GX_PID_MAX_ID) {
        error_setg(errp, "%s must be 0..%u",
                   INTEL_460GX_PID_PROP_INITIAL_ID,
                   INTEL_460GX_PID_MAX_ID);
        return;
    }
    if (s->legacy_pin != INTEL_460GX_PID_LEGACY_PIN_DISCONNECTED &&
        s->legacy_pin >= INTEL_460GX_PID_NUM_PINS) {
        error_setg(errp, "legacy-pin must be 0..%u or %u (disconnected)",
                   INTEL_460GX_PID_NUM_PINS - 1,
                   INTEL_460GX_PID_LEGACY_PIN_DISCONNECTED);
        return;
    }
    s->initial_id_migration = s->initial_id;

    if (s->test_mmio_base != PID_TEST_MMIO_UNMAPPED) {
        if (s->test_mmio_base & (PID_MMIO_SIZE - 1)) {
            error_setg(errp, "x-test-mmio-base must be 4 KiB aligned");
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, s->test_mmio_base);
    }
}

static void pid_init(Object *obj)
{
    Intel460GXPIDState *s = INTEL_460GX_PID(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pid_mmio_ops, s,
                          TYPE_INTEL_460GX_PID, PID_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_in_named(dev, pid_irq_handler,
                            INTEL_460GX_PID_GPIO_IRQ,
                            INTEL_460GX_PID_NUM_PINS);
    qdev_init_gpio_in_named(dev, pid_legacy_handler,
                            INTEL_460GX_PID_GPIO_LEGACY, 1);
}

static const Property pid_properties[] = {
    DEFINE_PROP_UINT8(INTEL_460GX_PID_PROP_INITIAL_ID,
                      Intel460GXPIDState, initial_id, 0),
    DEFINE_PROP_UINT32(INTEL_460GX_PID_PROP_LEGACY_PIN,
                       Intel460GXPIDState, legacy_pin,
                       INTEL_460GX_PID_LEGACY_PIN_DISCONNECTED),
    DEFINE_PROP_UINT64("x-test-mmio-base", Intel460GXPIDState,
                       test_mmio_base, PID_TEST_MMIO_UNMAPPED),
};

static void pid_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Intel 460GX Programmable Interrupt Device core";
    dc->realize = pid_realize;
    device_class_set_legacy_reset(dc, pid_reset);
    device_class_set_props(dc, pid_properties);
    dc->vmsd = &vmstate_intel_460gx_pid;
}

static const TypeInfo pid_type_info = {
    .name = TYPE_INTEL_460GX_PID,
    .parent = TYPE_DYNAMIC_SYS_BUS_DEVICE,
    .instance_size = sizeof(Intel460GXPIDState),
    .instance_init = pid_init,
    .class_init = pid_class_init,
};

static void pid_register_types(void)
{
    type_register_static(&pid_type_info);
}
type_init(pid_register_types)
