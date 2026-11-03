/*
 * IA-64 i2000 test upper CMOS bank
 *
 * This device exposes an independent 128-byte SRAM index/data pair.  SRAM
 * contents live for the VM lifetime and migrate, with no host persistence.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/ia64/ia64_i2000_rtc_bank1.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

struct IA64I2000RTCBank1State {
    ISADevice parent_obj;

    MemoryRegion io;
    uint16_t iobase;
    uint8_t index;
    uint8_t data[IA64_I2000_RTC_BANK1_SIZE];
};

static uint64_t rtc_bank1_read(void *opaque, hwaddr addr, unsigned size)
{
    IA64I2000RTCBank1State *s = opaque;

    return (addr & 1) ? s->data[s->index] : UINT8_MAX;
}

static void rtc_bank1_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    IA64I2000RTCBank1State *s = opaque;

    if (addr & 1) {
        s->data[s->index] = value;
    } else {
        /* Match the MC146818 index-port convention; bit 7 is not storage. */
        s->index = value & (IA64_I2000_RTC_BANK1_SIZE - 1);
    }
}

static const MemoryRegionOps rtc_bank1_ops = {
    .read = rtc_bank1_read,
    .write = rtc_bank1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void rtc_bank1_realize(DeviceState *dev, Error **errp)
{
    IA64I2000RTCBank1State *s = IA64_I2000_RTC_BANK1(dev);

    if (s->iobase > UINT16_MAX - 1) {
        error_setg(errp, "%s must leave room for an index/data pair",
                   IA64_I2000_RTC_BANK1_PROP_IOBASE);
        return;
    }
    isa_register_ioport(ISA_DEVICE(dev), &s->io, s->iobase);
}

static void rtc_bank1_init(Object *obj)
{
    IA64I2000RTCBank1State *s = IA64_I2000_RTC_BANK1(obj);

    memory_region_init_io(&s->io, obj, &rtc_bank1_ops, s,
                          TYPE_IA64_I2000_RTC_BANK1, 2);
}

static const VMStateDescription vmstate_rtc_bank1 = {
    .name = TYPE_IA64_I2000_RTC_BANK1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]) {
            VMSTATE_UINT16_EQUAL(iobase, IA64I2000RTCBank1State),
            VMSTATE_UINT8(index, IA64I2000RTCBank1State),
            VMSTATE_UINT8_ARRAY(data, IA64I2000RTCBank1State,
                                IA64_I2000_RTC_BANK1_SIZE),
            VMSTATE_END_OF_LIST() },
};

static const Property rtc_bank1_properties[] = {
    DEFINE_PROP_UINT16(IA64_I2000_RTC_BANK1_PROP_IOBASE, IA64I2000RTCBank1State,
                       iobase, 0x72),
};

static void rtc_bank1_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "IA-64 i2000 upper CMOS bank";
    dc->realize = rtc_bank1_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_rtc_bank1;
    device_class_set_props(dc, rtc_bank1_properties);
}

static const TypeInfo rtc_bank1_type_info = {
    .name = TYPE_IA64_I2000_RTC_BANK1,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(IA64I2000RTCBank1State),
    .instance_init = rtc_bank1_init,
    .class_init = rtc_bank1_class_init,
};

static void rtc_bank1_register_types(void)
{
    type_register_static(&rtc_bank1_type_info);
}

type_init(rtc_bank1_register_types)
