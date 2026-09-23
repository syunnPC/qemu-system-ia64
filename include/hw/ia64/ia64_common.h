/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Common helpers for IA-64 system machines.
 */

#ifndef HW_IA64_COMMON_H
#define HW_IA64_COMMON_H

#include "exec/hwaddr.h"
#include "qemu/notify.h"
#include "qemu/typedefs.h"
#include "target/ia64/cpu.h"

typedef struct IA64MachineFirmware {
    uint64_t base;
    uint64_t size;
    uint64_t entry;
    uint64_t global_pointer;
    uint64_t pal_entry;
    bool automatic;
    bool elf;
    bool legacy_raw;
} IA64MachineFirmware;

void ia64_machine_firmware_init(Object *obj, IA64MachineFirmware *firmware);
void ia64_machine_firmware_boot_info(const IA64MachineFirmware *firmware,
                                      IA64BootInfo *info);

typedef IA64BootInfo (*IA64MachineBootInfoFn)(unsigned int cpu_index,
                                              void *opaque);
typedef IA64BootInfo (*IA64MachineFirmwareBootInfoFn)(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque);
typedef void (*IA64MachineDoneFn)(void *opaque);
typedef int (*IA64MachinePibIntaFn)(void *opaque);

typedef struct IA64MachineFirmwareNotifier {
    Notifier notifier;
    bool registered;
    MachineState *machine;
    hwaddr firmware_base;
    size_t firmware_size;
    IA64MachineFirmwareBootInfoFn boot_info;
    IA64MachineDoneFn machine_done;
    void *opaque;
} IA64MachineFirmwareNotifier;

typedef struct IA64MachineCpuConfig {
    bool alat_full;
    uint64_t firmware_compat_flags;
    IA64MachineBootInfoFn boot_info;
    void *boot_info_opaque;
} IA64MachineCpuConfig;

bool ia64_machine_effective_alat_full(const MachineState *machine,
                                      bool configured_full);
bool ia64_machine_create_cpus(MachineState *machine,
                              const IA64MachineCpuConfig *config,
                              Error **errp);
void ia64_machine_reset_cpus(void);

bool ia64_machine_validate_socket_smp(const MachineState *machine,
                                      unsigned int max_cpus, Error **errp);

void ia64_machine_map_pib(Object *owner, MemoryRegion **pib,
                          const char *name, hwaddr base, uint64_t size);
void ia64_machine_map_pib_with_inta(Object *owner, MemoryRegion **pib,
                                    const char *name, hwaddr base,
                                    uint64_t size,
                                    IA64MachinePibIntaFn inta,
                                    void *inta_opaque);

bool ia64_machine_load_firmware(MachineState *machine,
                                IA64MachineFirmware *firmware,
                                uint64_t low_ram_end,
                                Error **errp);

char *ia64_machine_resolve_nvram_path(MachineState *machine,
                                      const char *nvram_path);

void ia64_machine_init_firmware_notifier(
    IA64MachineFirmwareNotifier *notifier, MachineState *machine,
    hwaddr firmware_base, size_t firmware_size,
    IA64MachineFirmwareBootInfoFn boot_info,
    IA64MachineDoneFn machine_done, void *opaque);
void ia64_machine_cleanup_firmware_notifier(
    IA64MachineFirmwareNotifier *notifier);

#endif
