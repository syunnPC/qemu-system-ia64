/*
 * HP zx6000 processor-dependent hardware
 *
 * Models UART, RTC/NVRAM, reset/poweroff, and ACPI PM/SCI functions. Other
 * PDH functions are not implemented.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/acpi/acpi.h"
#include "hw/char/serial-mm.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/ia64/hp_zx6000_pdh.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/notify.h"
#include "system/runstate.h"
#include "system/system.h"

struct HPZX6000PDHState {
    SysBusDevice parent_obj;

    SerialMM uart[HP_ZX6000_PDH_UART_COUNT];
    qemu_irq uart_irq[HP_ZX6000_PDH_UART_COUNT];
    MemoryRegion nvram;
    MemoryRegion rtc;
    MemoryRegion control;
    MemoryRegion acpi_pm;
    MemoryRegion acpi_pm_io;
    MemoryRegion acpi_gpe;
    ACPIREGS acpi_regs;
    qemu_irq acpi_sci;
    Notifier powerdown_notifier;
    uint8_t nvram_data[HP_ZX6000_PDH_NVRAM_SIZE];
    int64_t rtc_offset;
    char *nvram_path;
    bool nvram_write_warning;
};

G_STATIC_ASSERT(IA64_PLATFORM_ACPI_PM_TMR_OFFSET + 4 <=
                IA64_PLATFORM_ACPI_PM1_EVT_OFFSET);
G_STATIC_ASSERT(IA64_PLATFORM_ACPI_PM1_EVT_OFFSET + 4 <=
                IA64_PLATFORM_ACPI_PM1_CNT_OFFSET);
G_STATIC_ASSERT(IA64_PLATFORM_ACPI_PM1_CNT_OFFSET + 2 <=
                IA64_PLATFORM_ACPI_GPE0_STS_OFFSET);
G_STATIC_ASSERT(IA64_PLATFORM_ACPI_GPE0_STS_OFFSET +
                IA64_PLATFORM_ACPI_GPE0_LENGTH <=
                IA64_PLATFORM_ACPI_PM_SIZE);
G_STATIC_ASSERT(IA64_PLATFORM_ACPI_GPE0_EN_OFFSET ==
                IA64_PLATFORM_ACPI_GPE0_STS_OFFSET +
                IA64_PLATFORM_ACPI_GPE0_LENGTH / 2);

static uint64_t hp_zx6000_pdh_nvram_read(void *opaque, hwaddr addr,
                                         unsigned int size)
{
    HPZX6000PDHState *s = opaque;
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)s->nvram_data[addr + i] << (i * 8);
    }
    return value;
}

static void hp_zx6000_pdh_nvram_commit(HPZX6000PDHState *s)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) read_err = NULL;
    g_autoptr(GError) err = NULL;
    gsize length = sizeof(s->nvram_data);

    if (!hp_zx6000_pdh_nvram_persistent(s)) {
        return;
    }
    if (g_file_get_contents(s->nvram_path, &contents, &length, &read_err)) {
        if (length == 0) {
            length = sizeof(s->nvram_data);
        } else if (length != IA64_PLATFORM_MIN_NVRAM_SIZE &&
                   length != sizeof(s->nvram_data)) {
            if (!s->nvram_write_warning) {
                warn_report("refusing to overwrite zx6000 NVRAM '%s': "
                            "expected %zu or %zu bytes, found %zu",
                            s->nvram_path,
                            (size_t)IA64_PLATFORM_MIN_NVRAM_SIZE,
                            sizeof(s->nvram_data), (size_t)length);
                s->nvram_write_warning = true;
            }
            return;
        }
    } else if (read_err &&
               !g_error_matches(read_err, G_FILE_ERROR,
                                G_FILE_ERROR_NOENT)) {
        if (!s->nvram_write_warning) {
            warn_report("failed to read zx6000 NVRAM '%s' before saving: %s",
                        s->nvram_path, read_err->message);
            s->nvram_write_warning = true;
        }
        return;
    } else {
        length = sizeof(s->nvram_data);
    }
    if (!g_file_set_contents(s->nvram_path, (const char *)s->nvram_data,
                             length, &err) &&
        !s->nvram_write_warning) {
        warn_report("failed to save zx6000 NVRAM '%s': %s",
                    s->nvram_path,
                    err ? err->message : "unknown error");
        s->nvram_write_warning = true;
    }
}

static void hp_zx6000_pdh_nvram_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned int size)
{
    HPZX6000PDHState *s = opaque;
    unsigned int i;

    if (addr == HP_ZX6000_PDH_NVRAM_COMMIT_OFFSET &&
        size == sizeof(value) &&
        value == HP_ZX6000_PDH_NVRAM_COMMIT_MAGIC) {
        hp_zx6000_pdh_nvram_commit(s);
        return;
    }
    for (i = 0; i < size; i++) {
        s->nvram_data[addr + i] = value >> (i * 8);
    }
}

static const MemoryRegionOps hp_zx6000_pdh_nvram_ops = {
    .read = hp_zx6000_pdh_nvram_read,
    .write = hp_zx6000_pdh_nvram_write,
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

static int64_t hp_zx6000_pdh_host_seconds(void)
{
    return time(NULL);
}

static uint64_t hp_zx6000_pdh_rtc_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    HPZX6000PDHState *s = opaque;
    int64_t now = hp_zx6000_pdh_host_seconds();

    if (addr != 0 || size != sizeof(uint64_t)) {
        return 0;
    }
    if (s->rtc_offset > 0 && now > INT64_MAX - s->rtc_offset) {
        return INT64_MAX;
    }
    if (s->rtc_offset < 0 && now < -s->rtc_offset) {
        return 0;
    }
    return now + s->rtc_offset;
}

static void hp_zx6000_pdh_rtc_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned int size)
{
    HPZX6000PDHState *s = opaque;

    if (addr == 0 && size == sizeof(value) && value <= INT64_MAX) {
        s->rtc_offset = (int64_t)value - hp_zx6000_pdh_host_seconds();
    }
}

static const MemoryRegionOps hp_zx6000_pdh_rtc_ops = {
    .read = hp_zx6000_pdh_rtc_read,
    .write = hp_zx6000_pdh_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static uint64_t hp_zx6000_pdh_control_read(void *opaque, hwaddr addr,
                                           unsigned int size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* Reset and poweroff controls are write-only and read as zero. */
    return 0;
}

static void hp_zx6000_pdh_control_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned int size)
{
    (void)opaque;

    if (size != 1 || value != HP_ZX6000_PDH_CONTROL_VALUE) {
        return;
    }
    if (addr == HP_ZX6000_PDH_CONTROL_RESET_OFFSET) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    } else if (addr == HP_ZX6000_PDH_CONTROL_POWEROFF_OFFSET) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static const MemoryRegionOps hp_zx6000_pdh_control_ops = {
    .read = hp_zx6000_pdh_control_read,
    .write = hp_zx6000_pdh_control_write,
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

static void hp_zx6000_pdh_acpi_update_sci(ACPIREGS *ar)
{
    HPZX6000PDHState *s = container_of(ar, HPZX6000PDHState, acpi_regs);

    if (ar->pm1.cnt.cnt & ACPI_BITMASK_SCI_ENABLE) {
        acpi_update_sci(ar, s->acpi_sci);
    } else {
        qemu_set_irq(s->acpi_sci, 0);
    }
}

static uint64_t hp_zx6000_pdh_acpi_gpe_read(void *opaque, hwaddr addr,
                                             unsigned int size)
{
    HPZX6000PDHState *s = opaque;
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)acpi_gpe_ioport_readb(
            &s->acpi_regs, addr + i) << (i * 8);
    }
    return value;
}

static void hp_zx6000_pdh_acpi_gpe_write(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned int size)
{
    HPZX6000PDHState *s = opaque;
    unsigned int i;

    for (i = 0; i < size; i++) {
        acpi_gpe_ioport_writeb(&s->acpi_regs, addr + i,
                               value >> (i * 8));
    }
    hp_zx6000_pdh_acpi_update_sci(&s->acpi_regs);
}

static const MemoryRegionOps hp_zx6000_pdh_acpi_gpe_ops = {
    .read = hp_zx6000_pdh_acpi_gpe_read,
    .write = hp_zx6000_pdh_acpi_gpe_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void hp_zx6000_pdh_powerdown(Notifier *notifier, void *opaque)
{
    HPZX6000PDHState *s = container_of(notifier, HPZX6000PDHState,
                                       powerdown_notifier);

    (void)opaque;
    if (s->acpi_regs.pm1.evt.en & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        acpi_pm1_evt_power_down(&s->acpi_regs);
    } else {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void hp_zx6000_pdh_reset(DeviceState *dev)
{
    HPZX6000PDHState *s = HP_ZX6000_PDH(dev);

    acpi_pm1_evt_reset(&s->acpi_regs);
    acpi_pm1_cnt_reset(&s->acpi_regs);
    acpi_pm_tmr_reset(&s->acpi_regs);
    acpi_gpe_reset(&s->acpi_regs);
    hp_zx6000_pdh_acpi_update_sci(&s->acpi_regs);
}

bool hp_zx6000_pdh_nvram_persistent(const HPZX6000PDHState *s)
{
    return s->nvram_path != NULL &&
        g_strcmp0(s->nvram_path, "none") != 0;
}

MemoryRegion *hp_zx6000_pdh_acpi_pm_io(HPZX6000PDHState *s)
{
    return &s->acpi_pm_io;
}

static bool hp_zx6000_pdh_load_nvram(HPZX6000PDHState *s, Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;
    gsize length = 0;

    memset(s->nvram_data, 0, sizeof(s->nvram_data));
    if (!hp_zx6000_pdh_nvram_persistent(s)) {
        return true;
    }
    if (!g_file_get_contents(s->nvram_path, &contents, &length, &err)) {
        if (g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            return true;
        }
        error_setg(errp, "failed to load zx6000 NVRAM '%s': %s",
                   s->nvram_path, err->message);
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (length > sizeof(s->nvram_data)) {
        error_setg(errp,
                   "zx6000 NVRAM '%s' is %zu bytes; maximum is %zu bytes",
                   s->nvram_path, (size_t)length,
                   sizeof(s->nvram_data));
        return false;
    }
    if (length != IA64_PLATFORM_MIN_NVRAM_SIZE &&
        length != sizeof(s->nvram_data)) {
        error_setg(errp,
                   "zx6000 NVRAM '%s' must be %zu or %zu bytes; found %zu",
                   s->nvram_path,
                   (size_t)IA64_PLATFORM_MIN_NVRAM_SIZE,
                   sizeof(s->nvram_data), (size_t)length);
        return false;
    }
    memcpy(s->nvram_data, contents, length);
    return true;
}

static void hp_zx6000_pdh_uart_irq(void *opaque, int n, int level)
{
    HPZX6000PDHState *s = opaque;

    qemu_set_irq(s->uart_irq[n], level);
}

static void hp_zx6000_pdh_realize(DeviceState *dev, Error **errp)
{
    HPZX6000PDHState *s = HP_ZX6000_PDH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    unsigned int i;

    if (!hp_zx6000_pdh_load_nvram(s, errp)) {
        return;
    }
    for (i = 0; i < HP_ZX6000_PDH_UART_COUNT; i++) {
        DeviceState *uart = DEVICE(&s->uart[i]);

        qdev_prop_set_uint8(uart, "regshift", 0);
        qdev_prop_set_uint32(uart, "baudbase",
                            HP_ZX6000_PDH_UART_BAUDBASE);
        qdev_prop_set_uint8(uart, "endianness", DEVICE_LITTLE_ENDIAN);
        if (!sysbus_realize(SYS_BUS_DEVICE(uart), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(uart), 0,
                           qdev_get_gpio_in_named(dev, "uart-irq", i));
        sysbus_init_mmio(sbd,
                         sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0));
    }

    memory_region_init_io(&s->nvram, OBJECT(s), &hp_zx6000_pdh_nvram_ops, s,
                          "hp-zx6000-pdh.nvram",
                          HP_ZX6000_PDH_NVRAM_SIZE);
    memory_region_init_io(&s->rtc, OBJECT(s), &hp_zx6000_pdh_rtc_ops, s,
                          "hp-zx6000-pdh.rtc", HP_ZX6000_PDH_RTC_SIZE);
    memory_region_init_io(&s->control, OBJECT(s),
                          &hp_zx6000_pdh_control_ops, s,
                          "hp-zx6000-pdh.control",
                          HP_ZX6000_PDH_CONTROL_SIZE);
    memory_region_init(&s->acpi_pm, OBJECT(s),
                       "hp-zx6000-pdh.acpi-pm",
                       IA64_PLATFORM_ACPI_PM_SIZE);
    acpi_pm1_evt_init(&s->acpi_regs, hp_zx6000_pdh_acpi_update_sci,
                      &s->acpi_pm);
    acpi_pm1_cnt_init(&s->acpi_regs, &s->acpi_pm,
                      false, false, 0, true);
    acpi_pm_tmr_init(&s->acpi_regs, hp_zx6000_pdh_acpi_update_sci,
                     &s->acpi_pm);
    /* The common helpers use PC-style offsets; relocate their leaf regions. */
    memory_region_del_subregion(&s->acpi_pm, &s->acpi_regs.pm1.evt.io);
    memory_region_del_subregion(&s->acpi_pm, &s->acpi_regs.pm1.cnt.io);
    memory_region_del_subregion(&s->acpi_pm, &s->acpi_regs.tmr.io);
    memory_region_add_subregion(&s->acpi_pm,
                                IA64_PLATFORM_ACPI_PM1_EVT_OFFSET,
                                &s->acpi_regs.pm1.evt.io);
    memory_region_add_subregion(&s->acpi_pm,
                                IA64_PLATFORM_ACPI_PM1_CNT_OFFSET,
                                &s->acpi_regs.pm1.cnt.io);
    memory_region_add_subregion(&s->acpi_pm,
                                IA64_PLATFORM_ACPI_PM_TMR_OFFSET,
                                &s->acpi_regs.tmr.io);
    acpi_gpe_init(&s->acpi_regs, IA64_PLATFORM_ACPI_GPE0_LENGTH);
    memory_region_init_io(&s->acpi_gpe, OBJECT(s),
                          &hp_zx6000_pdh_acpi_gpe_ops, s,
                          "hp-zx6000-pdh.acpi-gpe",
                          IA64_PLATFORM_ACPI_GPE0_LENGTH);
    memory_region_add_subregion(&s->acpi_pm,
                                IA64_PLATFORM_ACPI_GPE0_STS_OFFSET,
                                &s->acpi_gpe);
    memory_region_init_alias(&s->acpi_pm_io, OBJECT(s),
                             "hp-zx6000-pdh.acpi-pm-io",
                             &s->acpi_pm,
                             IA64_PLATFORM_ACPI_PM_IO_BASE,
                             IA64_PLATFORM_ACPI_PM_IO_SIZE);
    s->powerdown_notifier.notify = hp_zx6000_pdh_powerdown;
    qemu_register_powerdown_notifier(&s->powerdown_notifier);
    sysbus_init_mmio(sbd, &s->nvram);
    sysbus_init_mmio(sbd, &s->rtc);
    sysbus_init_mmio(sbd, &s->control);
    sysbus_init_mmio(sbd, &s->acpi_pm);
}

static void hp_zx6000_pdh_init(Object *obj)
{
    HPZX6000PDHState *s = HP_ZX6000_PDH(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    qdev_init_gpio_in_named(DEVICE(obj), hp_zx6000_pdh_uart_irq,
                            "uart-irq", HP_ZX6000_PDH_UART_COUNT);
    for (i = 0; i < HP_ZX6000_PDH_UART_COUNT; i++) {
        g_autofree char *name = g_strdup_printf("uart[%u]", i);
        g_autofree char *property = g_strdup_printf("chardev%u", i);

        object_initialize_child(obj, name, &s->uart[i], TYPE_SERIAL_MM);
        object_property_add_alias(obj, property, OBJECT(&s->uart[i]),
                                  "chardev");
        sysbus_init_irq(sbd, &s->uart_irq[i]);
    }
    sysbus_init_irq(sbd, &s->acpi_sci);
}

static int hp_zx6000_pdh_post_load(void *opaque, int version_id)
{
    HPZX6000PDHState *s = opaque;
    uint16_t pm_enable;

    pm_enable = s->acpi_regs.pm1.evt.en;
    qemu_system_wakeup_enable(
        QEMU_WAKEUP_REASON_RTC,
        (pm_enable & ACPI_BITMASK_RT_CLOCK_ENABLE) != 0);
    qemu_system_wakeup_enable(
        QEMU_WAKEUP_REASON_PMTIMER,
        (pm_enable & ACPI_BITMASK_TIMER_ENABLE) != 0);
    hp_zx6000_pdh_acpi_update_sci(&s->acpi_regs);
    return 0;
}

static const VMStateDescription vmstate_hp_zx6000_pdh = {
    .name = TYPE_HP_ZX6000_PDH,
    .version_id = 3,
    .minimum_version_id = 3,
    .post_load = hp_zx6000_pdh_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(nvram_data, HPZX6000PDHState,
                            HP_ZX6000_PDH_NVRAM_SIZE),
        VMSTATE_INT64(rtc_offset, HPZX6000PDHState),
        VMSTATE_UINT16(acpi_regs.pm1.evt.sts, HPZX6000PDHState),
        VMSTATE_UINT16(acpi_regs.pm1.evt.en, HPZX6000PDHState),
        VMSTATE_UINT16(acpi_regs.pm1.cnt.cnt, HPZX6000PDHState),
        VMSTATE_TIMER_PTR(acpi_regs.tmr.timer, HPZX6000PDHState),
        VMSTATE_INT64(acpi_regs.tmr.overflow_time, HPZX6000PDHState),
        VMSTATE_BUFFER_POINTER_UNSAFE(acpi_regs.gpe.sts,
                                      HPZX6000PDHState, 0,
                                      IA64_PLATFORM_ACPI_GPE0_LENGTH),
        VMSTATE_BUFFER_POINTER_UNSAFE(acpi_regs.gpe.en,
                                      HPZX6000PDHState, 0,
                                      IA64_PLATFORM_ACPI_GPE0_LENGTH),
        VMSTATE_END_OF_LIST()
    },
};

static const Property hp_zx6000_pdh_properties[] = {
    DEFINE_PROP_STRING(HP_ZX6000_PDH_PROP_NVRAM,
                       HPZX6000PDHState, nvram_path),
};

static void hp_zx6000_pdh_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    (void)data;
    dc->desc = "HP zx6000 processor-dependent hardware";
    dc->realize = hp_zx6000_pdh_realize;
    dc->vmsd = &vmstate_hp_zx6000_pdh;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    device_class_set_props(dc, hp_zx6000_pdh_properties);
    device_class_set_legacy_reset(dc, hp_zx6000_pdh_reset);
}

static const TypeInfo hp_zx6000_pdh_type_info = {
    .name = TYPE_HP_ZX6000_PDH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HPZX6000PDHState),
    .instance_init = hp_zx6000_pdh_init,
    .class_init = hp_zx6000_pdh_class_init,
};

static void hp_zx6000_pdh_register_types(void)
{
    type_register_static(&hp_zx6000_pdh_type_info);
}

type_init(hp_zx6000_pdh_register_types)
