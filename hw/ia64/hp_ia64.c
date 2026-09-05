/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Common validation and state for HP IA-64 machines.
 */

#include "qemu/osdep.h"

#include "hw/ia64/hp_ia64.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/units.h"

#define HP_IA64_DESCRIPTOR_CHILD "platform-descriptor"

bool hp_ia64_machine_create_ras(HPIA64MachineState *machine,
                                hwaddr base, Error **errp)
{
    if (!machine) {
        error_setg(errp, "invalid HP IA-64 machine for RAS setup");
        return false;
    }
    if (machine->ras) {
        error_setg(errp, "HP IA-64 RAS hub is already installed");
        return false;
    }
    machine->ras = ia64_ras_hub_create(OBJECT(machine), "ras", base, errp);
    return machine->ras != NULL;
}

bool hp_ia64_machine_install_platform_descriptor(
    HPIA64MachineState *machine,
    const IA64PlatformDescriptor *header,
    const IA64PlatformDescriptorArrays *arrays, Error **errp)
{
    HPIA64MachineClass *hmc;
    MachineState *ms;
    IA64PlatformDescriptorDevice *device;
    IA64PlatformFirmwareArgs args;
    uint32_t platform_id;

    if (machine == NULL || header == NULL || arrays == NULL) {
        error_setg(errp, "invalid HP IA-64 platform descriptor input");
        return false;
    }
    hmc = HP_IA64_MACHINE_GET_CLASS(machine);
    ms = MACHINE(machine);
    platform_id = le32_to_cpu(header->PlatformId);
    if (platform_id != hmc->platform_id) {
        error_setg(errp,
                   "HP IA-64 descriptor platform does not match the machine");
        return false;
    }
    if (le64_to_cpu(header->RamSize) != ms->ram_size ||
        le32_to_cpu(header->ProcessorCount) != ms->smp.cpus ||
        le32_to_cpu(header->SocketCount) != ms->smp.sockets ||
        le32_to_cpu(header->CoresPerSocket) != ms->smp.cores ||
        le32_to_cpu(header->ThreadsPerCore) != ms->smp.threads) {
        error_setg(errp,
                   "HP IA-64 descriptor RAM or CPU topology does not match "
                   "the machine");
        return false;
    }
    if (machine->descriptor_device != NULL || machine->firmware_args_valid) {
        error_setg(errp, "HP IA-64 platform descriptor is already installed");
        return false;
    }

    device = ia64_platform_desc_device_create(
        OBJECT(machine), HP_IA64_DESCRIPTOR_CHILD, hmc->descriptor_gpa,
        header, arrays, errp);
    if (device == NULL) {
        return false;
    }
    if (!ia64_platform_desc_device_get_firmware_args(device, &args) ||
        args.platform_id != hmc->platform_id) {
        ia64_platform_desc_device_destroy(device);
        error_setg(errp,
                   "HP IA-64 platform descriptor transport is unavailable");
        return false;
    }

    machine->firmware_args = args;
    machine->descriptor_low_ram_end = le64_to_cpu(header->LowRamEnd);
    machine->descriptor_device = device;
    machine->firmware_args_valid = true;
    return true;
}

bool hp_ia64_machine_apply_platform_firmware_args(
    const HPIA64MachineState *machine, IA64BootInfo *info)
{
    IA64PlatformFirmwareArgs installed_args;
    IA64BootInfo result;

    if (machine == NULL || info == NULL ||
        !machine->firmware_args_valid ||
        machine->descriptor_device == NULL ||
        !ia64_platform_desc_device_get_firmware_args(
            machine->descriptor_device, &installed_args) ||
        installed_args.descriptor_gpa !=
            machine->firmware_args.descriptor_gpa ||
        installed_args.descriptor_size !=
            machine->firmware_args.descriptor_size ||
        installed_args.firmware_compat_flags !=
            machine->firmware_args.firmware_compat_flags ||
        installed_args.platform_id != machine->firmware_args.platform_id ||
        info->low_ram_size != machine->descriptor_low_ram_end ||
        (!info->powered_off &&
         (machine->descriptor_low_ram_end < 16 ||
          info->stack_pointer != machine->descriptor_low_ram_end - 16))) {
        return false;
    }

    result = *info;
    result.firmware_arg0 = installed_args.descriptor_gpa;
    result.firmware_arg1 = installed_args.descriptor_size;
    result.firmware_arg2 = installed_args.platform_id;
    result.firmware_args_valid = true;
    *info = result;
    return true;
}

bool hp_ia64_machine_validate(HPIA64MachineState *machine, Error **errp)
{
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_GET_CLASS(machine);
    MachineState *ms = MACHINE(machine);

    if (ms->ram_size < hmc->minimum_ram_size) {
        g_autofree char *size = size_to_str(hmc->minimum_ram_size);

        error_setg(errp, "Invalid RAM size, should be at least %s", size);
        return false;
    }
    if (ms->ram_size > hmc->maximum_ram_size) {
        g_autofree char *size = size_to_str(hmc->maximum_ram_size);

        error_setg(errp, "Invalid RAM size, should be at most %s", size);
        return false;
    }
    if (hmc->validate_smp ? !hmc->validate_smp(ms, errp) :
        !ia64_machine_validate_socket_smp(ms, 2, errp)) {
        return false;
    }
    return true;
}

static char *hp_ia64_machine_get_alat(Object *obj, Error **errp)
{
    HPIA64MachineState *s = HP_IA64_MACHINE(obj);
    MachineState *machine = MACHINE(obj);

    (void)errp;
    return g_strdup(ia64_machine_effective_alat_full(machine, s->alat_full) ?
                    "full" : "zero");
}

static void hp_ia64_machine_set_alat(Object *obj, const char *value,
                                     Error **errp)
{
    HPIA64MachineState *s = HP_IA64_MACHINE(obj);

    if (g_str_equal(value, "zero")) {
        s->alat_full = false;
    } else if (g_str_equal(value, "full")) {
        s->alat_full = true;
    } else {
        error_setg(errp, "alat must be 'zero' or 'full'");
    }
}

static void hp_ia64_machine_instance_init(Object *obj)
{
    HPIA64MachineState *s = HP_IA64_MACHINE(obj);

    s->alat_full = false;
}

static void hp_ia64_machine_instance_finalize(Object *obj)
{
    HPIA64MachineState *s = HP_IA64_MACHINE(obj);

    ia64_machine_cleanup_firmware_notifier(&s->firmware_notifier);
}

static void hp_ia64_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    (void)data;
    mc->max_cpus = 2;
    mc->default_cpus = 1;
    mc->smp_props.prefer_sockets = true;
    mc->default_ram_size = 2 * GiB;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    object_class_property_add_str(oc, "alat", hp_ia64_machine_get_alat,
                                  hp_ia64_machine_set_alat);
    object_class_property_set_description(oc, "alat",
        "Set the IA-64 ALAT model to 'zero' (default) or 'full'");
}

static const TypeInfo hp_ia64_machine_type = {
    .name = TYPE_HP_IA64_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(HPIA64MachineState),
    .instance_init = hp_ia64_machine_instance_init,
    .instance_finalize = hp_ia64_machine_instance_finalize,
    .class_size = sizeof(HPIA64MachineClass),
    .class_init = hp_ia64_machine_class_init,
};

static void hp_ia64_machine_register_types(void)
{
    type_register_static(&hp_ia64_machine_type);
}

type_init(hp_ia64_machine_register_types)
