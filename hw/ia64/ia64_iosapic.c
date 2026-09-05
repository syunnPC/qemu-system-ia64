/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 I/O SAPIC device model.
 * Routes 24 external interrupt pins to the CPU Local SAPIC.
 */

#include "qemu/osdep.h"
#include "hw/ia64/ia64_iosapic.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"

#define IOSAPIC_IOREGSEL   0x00
#define IOSAPIC_IOWIN      0x10
#define IOSAPIC_EOI        0x40

#define IOSAPIC_REG_ID     0x00
#define IOSAPIC_REG_VER    0x01
#define IOSAPIC_RTE_BASE   0x10

#define RTE_VECTOR_MASK      0x00000000000000FFULL
#define RTE_DELIVERY_MODE    0x0000000000000700ULL
#define RTE_DELIVERY_STATUS  0x0000000000001000ULL
#define RTE_POLARITY_LOW     0x0000000000002000ULL
#define RTE_REMOTE_IRR       0x0000000000004000ULL
#define RTE_MASKED           0x0000000000010000ULL
#define RTE_TRIGGER_LEVEL    0x0000000000008000ULL
#define RTE_DESTINATION      0xffff000000000000ULL
#define RTE_RO_BITS          (RTE_DELIVERY_STATUS | RTE_REMOTE_IRR)
#define RTE_WRITABLE         (RTE_VECTOR_MASK | RTE_DELIVERY_MODE | \
                              RTE_POLARITY_LOW | RTE_MASKED | \
                              RTE_TRIGGER_LEVEL | RTE_DESTINATION)

struct IA64IOSapicState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint64_t rte[IA64_IOSAPIC_NUM_PINS];
    uint8_t  irq_level[IA64_IOSAPIC_NUM_PINS];
    uint32_t reg_select;
};

static void iosapic_update(IA64IOSapicState *s, int pin)
{
    uint64_t rte = s->rte[pin];
    IA64SapicDeliveryMode delivery =
        (IA64SapicDeliveryMode)((rte & RTE_DELIVERY_MODE) >> 8);
    uint8_t id = rte >> 56;
    uint8_t eid = rte >> 48;
    bool masked = (rte & RTE_MASKED) != 0;
    bool level_triggered = (rte & RTE_TRIGGER_LEVEL) != 0;

    if (masked || (level_triggered && !s->irq_level[pin])) {
        return;
    }

    if (level_triggered) {
        if (rte & RTE_REMOTE_IRR) {
            return;
        }
        s->rte[pin] |= RTE_REMOTE_IRR;
    }

    if (!ia64_sapic_deliver(IA64_SAPIC_DESTINATION_PHYSICAL,
                            id, eid, false, delivery,
                            rte & RTE_VECTOR_MASK) && level_triggered) {
        s->rte[pin] &= ~RTE_REMOTE_IRR;
    }
}

static void iosapic_fix_edge_remote_irr(IA64IOSapicState *s, int pin)
{
    if (!(s->rte[pin] & RTE_TRIGGER_LEVEL)) {
        s->rte[pin] &= ~RTE_REMOTE_IRR;
    }
}

static void iosapic_rte_write(IA64IOSapicState *s, int pin, uint32_t val,
                              bool high)
{
    uint64_t ro_bits = s->rte[pin] & RTE_RO_BITS;

    if (high) {
        s->rte[pin] = (s->rte[pin] & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
    } else {
        s->rte[pin] = (s->rte[pin] & 0xFFFFFFFF00000000ULL) | val;
    }

    s->rte[pin] = (s->rte[pin] & RTE_WRITABLE) | ro_bits;
    iosapic_fix_edge_remote_irr(s, pin);
    /*
     * A redirection-table write is not an edge request.  Re-evaluate an
     * asserted level input after unmasking or rerouting it, but wait for a
     * new input transition for an edge-triggered route.
     */
    if (s->rte[pin] & RTE_TRIGGER_LEVEL) {
        iosapic_update(s, pin);
    }
}

static void iosapic_eoi(IA64IOSapicState *s, uint8_t vector)
{
    unsigned pin;

    for (pin = 0; pin < IA64_IOSAPIC_NUM_PINS; pin++) {
        if ((s->rte[pin] & RTE_VECTOR_MASK) != vector ||
            !(s->rte[pin] & RTE_TRIGGER_LEVEL)) {
            continue;
        }
        if (!(s->rte[pin] & RTE_REMOTE_IRR)) {
            continue;
        }
        s->rte[pin] &= ~RTE_REMOTE_IRR;
        iosapic_update(s, pin);
    }
}

static void iosapic_irq_handler(void *opaque, int pin, int level)
{
    IA64IOSapicState *s = opaque;
    bool old_level;
    bool level_triggered;

    if (pin < 0 || pin >= IA64_IOSAPIC_NUM_PINS) {
        return;
    }

    old_level = s->irq_level[pin] != 0;
    level = !!level;
    s->irq_level[pin] = level;
    level_triggered = (s->rte[pin] & RTE_TRIGGER_LEVEL) != 0;

    if (level && (level_triggered || !old_level)) {
        iosapic_update(s, pin);
    }
}

static uint64_t iosapic_read(void *opaque, hwaddr addr, unsigned size)
{
    IA64IOSapicState *s = opaque;
    uint32_t result = 0;
    uint32_t index;

    switch (addr) {
    case IOSAPIC_IOREGSEL:
        result = s->reg_select;
        break;
    case IOSAPIC_IOWIN:
        index = s->reg_select;
        if (index == IOSAPIC_REG_ID) {
            result = 0;
        } else if (index == IOSAPIC_REG_VER) {
            result = IA64_IOSAPIC_VERSION;
        } else if (index >= IOSAPIC_RTE_BASE &&
                   index < IOSAPIC_RTE_BASE + IA64_IOSAPIC_NUM_PINS * 2) {
            int pin = (index - IOSAPIC_RTE_BASE) / 2;
            if ((index - IOSAPIC_RTE_BASE) & 1) {
                result = (uint32_t)(s->rte[pin] >> 32);
            } else {
                result = (uint32_t)s->rte[pin];
            }
        }
        break;
    case IOSAPIC_EOI:
        break;
    default:
        break;
    }
    return result;
}

static void iosapic_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IA64IOSapicState *s = opaque;
    uint32_t index;

    switch (addr) {
    case IOSAPIC_IOREGSEL:
        s->reg_select = (uint32_t)val;
        break;
    case IOSAPIC_IOWIN:
        index = s->reg_select;
        if (index == IOSAPIC_REG_ID) {
            break;
        } else if (index >= IOSAPIC_RTE_BASE &&
                   index < IOSAPIC_RTE_BASE + IA64_IOSAPIC_NUM_PINS * 2) {
            int pin = (index - IOSAPIC_RTE_BASE) / 2;
            iosapic_rte_write(s, pin, (uint32_t)val,
                              (index - IOSAPIC_RTE_BASE) & 1);
        }
        break;
    case IOSAPIC_EOI:
        iosapic_eoi(s, (uint8_t)val);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps iosapic_ops = {
    .read = iosapic_read,
    .write = iosapic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void iosapic_realize(DeviceState *dev, Error **errp)
{
    IA64IOSapicState *s = IA64_IOSAPIC(dev);

    qdev_init_gpio_in(dev, iosapic_irq_handler, IA64_IOSAPIC_NUM_PINS);
    memory_region_init_io(&s->mmio, OBJECT(dev), &iosapic_ops, s,
                          "iosapic", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void iosapic_reset(DeviceState *dev)
{
    IA64IOSapicState *s = IA64_IOSAPIC(dev);
    int i;

    memset(s->rte, 0, sizeof(s->rte));
    memset(s->irq_level, 0, sizeof(s->irq_level));
    for (i = 0; i < IA64_IOSAPIC_NUM_PINS; i++) {
        s->rte[i] = RTE_MASKED;
    }
    s->reg_select = 0;
}

static int iosapic_post_load(void *opaque, int version_id)
{
    IA64IOSapicState *s = opaque;
    unsigned int pin;

    /*
     * Edge inputs are historical events and must not be replayed merely
     * because their input wire was high at the snapshot boundary.  An
     * asserted level input, however, must be re-evaluated if it did not
     * already have Remote IRR set.
     */
    for (pin = 0; pin < IA64_IOSAPIC_NUM_PINS; pin++) {
        s->rte[pin] &= RTE_WRITABLE | RTE_RO_BITS;
        if (s->rte[pin] & RTE_TRIGGER_LEVEL) {
            iosapic_update(s, pin);
        }
    }
    return 0;
}

static const VMStateDescription vmstate_ia64_iosapic = {
    .name = "ia64-iosapic",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = iosapic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(rte, IA64IOSapicState,
                             IA64_IOSAPIC_NUM_PINS),
        VMSTATE_UINT8_ARRAY(irq_level, IA64IOSapicState,
                            IA64_IOSAPIC_NUM_PINS),
        VMSTATE_UINT32(reg_select, IA64IOSapicState),
        VMSTATE_END_OF_LIST()
    }
};

static void iosapic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = iosapic_realize;
    device_class_set_legacy_reset(dc, iosapic_reset);
    dc->vmsd = &vmstate_ia64_iosapic;
}

static const TypeInfo iosapic_info = {
    .name          = TYPE_IA64_IOSAPIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IA64IOSapicState),
    .class_init    = iosapic_class_init,
};

static void iosapic_register_types(void)
{
    type_register_static(&iosapic_info);
}
type_init(iosapic_register_types);
