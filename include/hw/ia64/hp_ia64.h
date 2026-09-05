/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Common state for HP IA-64 machine builders.
 */

#ifndef HW_IA64_HP_IA64_H
#define HW_IA64_HP_IA64_H

#include "hw/core/boards.h"
#include "hw/ia64/ia64_common.h"
#include "hw/ia64/ia64_platform.h"
#include "hw/ia64/ia64_ras.h"
#include "system/memory.h"

#define TYPE_HP_IA64_MACHINE MACHINE_TYPE_NAME("hp-ia64-base")
OBJECT_DECLARE_TYPE(HPIA64MachineState, HPIA64MachineClass, HP_IA64_MACHINE)

struct HPIA64MachineState {
    MachineState parent_obj;

    bool alat_full;
    MemoryRegion *pib;
    IA64RasHubState *ras;
    IA64PlatformDescriptorDevice *descriptor_device;
    IA64PlatformFirmwareArgs firmware_args;
    uint64_t descriptor_low_ram_end;
    bool firmware_args_valid;
    size_t firmware_size;
    IA64MachineFirmwareNotifier firmware_notifier;
};

struct HPIA64MachineClass {
    MachineClass parent_class;

    uint32_t platform_id;
    uint64_t minimum_ram_size;
    uint64_t maximum_ram_size;
    hwaddr descriptor_gpa;
    bool (*validate_smp)(const MachineState *machine, Error **errp);
};

bool hp_ia64_machine_validate(HPIA64MachineState *machine, Error **errp);
bool hp_ia64_machine_create_ras(HPIA64MachineState *machine,
                                hwaddr base, Error **errp);

/*
 * Install the immutable firmware descriptor after RAM mapping.  Platform
 * identity and GPA come from the machine class.
 */
bool hp_ia64_machine_install_platform_descriptor(
    HPIA64MachineState *machine,
    const IA64PlatformDescriptor *header,
    const IA64PlatformDescriptorArrays *arrays, Error **errp);

/*
 * Apply the installed descriptor address, size, and platform ID to boot info.
 * The BSP stack and low-RAM size must match descriptor LowRamEnd.
 */
bool hp_ia64_machine_apply_platform_firmware_args(
    const HPIA64MachineState *machine, IA64BootInfo *info);

#endif /* HW_IA64_HP_IA64_H */
