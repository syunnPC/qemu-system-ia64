/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Common helpers for IA-64 system machines.
 */

#include "qemu/osdep.h"

#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/cutils.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/ia64/ia64_common.h"
#include "hw/ia64/ia64_loader.h"
#include "exec/cpu-common.h"
#include "system/address-spaces.h"
#include "system/system.h"

#define IA64_PIB_IPI_LIMIT          0x00100000ULL
#define IA64_PIB_INTA_OFFSET        0x001e0000ULL
#define IA64_PIB_XTP_OFFSET         0x001e0008ULL

typedef struct IA64MachinePibState {
    MemoryRegion memory;
    IA64MachinePibIntaFn inta;
    void *inta_opaque;
} IA64MachinePibState;

static uint64_t ia64_pib_read(void *opaque, hwaddr addr, unsigned size)
{
    IA64MachinePibState *s = opaque;
    int vector;

    if (addr == IA64_PIB_XTP_OFFSET && size == 1) {
        return current_cpu != NULL ? ia64_sapic_get_xtp(current_cpu) : 0;
    }
    if (addr != IA64_PIB_INTA_OFFSET || size != 1 || s->inta == NULL) {
        return 0;
    }

    vector = s->inta(s->inta_opaque);
    return vector >= 0 && vector <= UINT8_MAX ? vector : 0;
}

static MemTxResult ia64_pib_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    uint8_t id;
    uint8_t eid;
    IA64SapicDeliveryMode delivery;

    (void)opaque;
    if (addr == IA64_PIB_XTP_OFFSET && size == 1) {
        if (current_cpu != NULL) {
            ia64_sapic_set_xtp(current_cpu, value);
        }
        return MEMTX_OK;
    }

    /* Address bit 3 is the redirection hint, not an alignment constraint. */
    if (addr >= IA64_PIB_IPI_LIMIT ||
        !((size == 8 && (addr & 7) == 0) ||
          (size == 4 && (addr & 3) == 0 && !attrs.unspecified))) {
        return MEMTX_OK;
    }

    /*
     * The lower half of the Processor Interrupt Block is the IPI delivery
     * region.  The address selects the target processor and the low data byte
     * carries the interrupt vector for INT delivery messages.
     */
    id = (addr >> 12) & 0xff;
    eid = (addr >> 4) & 0xff;
    delivery = (IA64SapicDeliveryMode)((value >> 8) & 7);
    ia64_sapic_deliver(IA64_SAPIC_DESTINATION_PHYSICAL,
                       id, eid, (addr & 8) != 0, delivery,
                       (uint8_t)value);
    return MEMTX_OK;
}

static const MemoryRegionOps ia64_pib_ops = {
    .read = ia64_pib_read,
    .write_with_attrs = ia64_pib_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

void ia64_machine_map_pib(Object *owner, MemoryRegion **pib,
                          const char *name, hwaddr base, uint64_t size)
{
    ia64_machine_map_pib_with_inta(owner, pib, name, base, size,
                                   NULL, NULL);
}

void ia64_machine_map_pib_with_inta(Object *owner, MemoryRegion **pib,
                                    const char *name, hwaddr base,
                                    uint64_t size,
                                    IA64MachinePibIntaFn inta,
                                    void *inta_opaque)
{
    IA64MachinePibState *s;

    g_assert(size != 0);

    if (*pib != NULL) {
        return;
    }

    s = g_new0(IA64MachinePibState, 1);
    s->inta = inta;
    s->inta_opaque = inta_opaque;
    *pib = &s->memory;
    memory_region_init_io(*pib, owner, &ia64_pib_ops, s, name,
                          size);
    memory_region_add_subregion(get_system_memory(), base, *pib);
}

bool ia64_machine_create_cpus(MachineState *machine,
                              const IA64MachineCpuConfig *config,
                              Error **errp)
{
    g_autoptr(GPtrArray) cpus = g_ptr_array_new_with_free_func(
        (GDestroyNotify)object_unref);
    uint32_t threads = MAX(machine->smp.threads, 1U);
    uint32_t cores = MAX(machine->smp.cores, 1U);
    uint32_t per_socket = threads * cores;
    bool alat_full;
    int i;

    if (config == NULL || config->boot_info == NULL) {
        error_setg(errp, "IA-64 CPU creation requires boot information");
        return false;
    }

    alat_full = ia64_machine_effective_alat_full(machine,
                                                 config->alat_full);
    for (i = 0; i < machine->smp.cpus; i++) {
        uint32_t package_base = (i / per_socket) * per_socket;
        IA64BootInfo boot_info = config->boot_info(i,
                                                   config->boot_info_opaque);
        IA64CPU *cpu = IA64_CPU(object_new(machine->cpu_type));

        g_ptr_array_add(cpus, cpu);
        cpu->alat_full = alat_full;
        cpu->firmware_compat_flags = config->firmware_compat_flags;
        cpu->socket_id = i / per_socket;
        cpu->core_id = (i / threads) % cores;
        cpu->thread_id = i % threads;
        cpu->cores_per_socket = cores;
        cpu->threads_per_core = threads;
        cpu->package_base = package_base;
        cpu->package_cpus = MIN(per_socket,
                                machine->smp.cpus - package_base);
        ia64_cpu_set_boot_info(cpu, &boot_info);
        if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
            int rollback;

            for (rollback = i - 1; rollback >= 0; rollback--) {
                IA64CPU *realized = g_ptr_array_index(cpus, rollback);

                qdev_unrealize(DEVICE(realized));
                object_unparent(OBJECT(realized));
            }
            return false;
        }
    }

    return true;
}

bool ia64_machine_effective_alat_full(const MachineState *machine,
                                      bool configured_full)
{
    (void)machine;
    return configured_full;
}

void ia64_machine_reset_cpus(void)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        /* IA-64 CPUs are not children of the platform system bus. */
        ia64_cpu_reset_to_boot_info(IA64_CPU(cs));
    }
}

bool ia64_machine_validate_socket_smp(const MachineState *machine,
                                      unsigned int max_cpus, Error **errp)
{
    const CpuTopology *smp = &machine->smp;

    if (smp->cpus < 1 || smp->cpus > max_cpus) {
        error_setg(errp, "IA-64 machine supports between 1 and %u CPUs",
                   max_cpus);
        return false;
    }
    if (smp->sockets != smp->cpus || smp->drawers != 1 || smp->books != 1 ||
        smp->dies != 1 || smp->clusters != 1 || smp->modules != 1 ||
        smp->cores != 1 || smp->threads != 1) {
        error_setg(errp, "IA-64 machine requires one single-core, "
                   "single-thread processor per socket");
        return false;
    }
    return true;
}

static void ia64_firmware_base_get(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    IA64MachineFirmware *fw = opaque;
    g_autofree char *value = fw->automatic ? g_strdup("auto") :
        g_strdup_printf("0x%" PRIx64, fw->base);

    visit_type_str(v, name, &value, errp);
}

static void ia64_firmware_base_set(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    IA64MachineFirmware *fw = opaque;
    g_autofree char *value = NULL;
    uint64_t base;

    if (!visit_type_str(v, name, &value, errp)) {
        return;
    }
    if (!strcmp(value, "auto")) {
        fw->automatic = true;
    } else if (qemu_strtou64(value, NULL, 0, &base) ||
               base < IA64_PLATFORM_FIRMWARE_BASE || (base & 0x1fff)) {
        error_setg(errp, "firmware-base must be 'auto' or an 8 KiB aligned "
                   "RAM address at or above 1 MiB");
    } else {
        fw->automatic = false;
        fw->base = base;
    }
}

void ia64_machine_firmware_init(Object *obj, IA64MachineFirmware *fw)
{
    fw->base = IA64_PLATFORM_FIRMWARE_BASE;
    fw->size = IA64_PLATFORM_FIRMWARE_SIZE;
    fw->entry = fw->base;
    fw->global_pointer = fw->base;
    fw->pal_entry = fw->base + 0x60;
    object_property_add(obj, "firmware-base", "str", ia64_firmware_base_get,
                        ia64_firmware_base_set, NULL, fw);
    object_property_set_description(obj, "firmware-base",
        "Firmware RAM address (default 0x100000), or auto for upper low RAM");
}

void ia64_machine_firmware_boot_info(const IA64MachineFirmware *fw,
                                      IA64BootInfo *info)
{
    info->firmware_base = fw->base;
    info->firmware_size = fw->size;
    info->pal_entry = fw->pal_entry;
    if (fw->elf) {
        info->firmware_entry = fw->entry;
        info->global_pointer = fw->global_pointer;
    }
}

static bool ia64_firmware_placement_valid(uint64_t base, uint64_t size,
                                          uint64_t limit)
{
    /* ACPI tables occupy 8--8.125 MiB; assist storage is above limit. */
    return base >= IA64_PLATFORM_FIRMWARE_BASE && base <= limit &&
           size <= limit - base &&
           !(base < 0x820000 && base + size > 0x800000);
}

bool ia64_machine_load_firmware(MachineState *machine,
                                IA64MachineFirmware *fw,
                                uint64_t low_ram_end, Error **errp)
{
    g_autofree char *firmware_path = NULL;
    g_autofree char *image = NULL;
    g_autoptr(GError) gerr = NULL;
    IA64FirmwareElf elf = { 0 };
    uint64_t limit;
    gsize image_size;
    unsigned i;

    if (low_ram_end <= IA64_FW_BOOT_STACK_SIZE + 0x400000) {
        error_setg(errp, "insufficient low RAM for IA-64 firmware");
        return false;
    }
    /* Leave the aligned EFI system table pointer page below assist storage. */
    limit = (low_ram_end - IA64_FW_BOOT_STACK_SIZE - 0x1000) &
            ~UINT64_C(0x3fffff);
    if (!machine->firmware || g_str_equal(machine->firmware, "none")) {
        if (fw->automatic || fw->base != IA64_PLATFORM_FIRMWARE_BASE) {
            error_setg(errp,
                       "firmware-base requires a relocatable firmware ELF");
            return false;
        }
        return true;
    }
    firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (!firmware_path) {
        firmware_path = g_strdup(machine->firmware);
    }
    if (!g_file_get_contents(firmware_path, &image, &image_size, &gerr)) {
        error_setg(errp, "failed to load firmware '%s': %s",
                   machine->firmware, gerr->message);
        return false;
    }
    if (image_size >= 4 && !memcmp(image, "\177ELF", 4)) {
        if (!ia64_firmware_elf_load(image, image_size, 0, &elf, errp)) {
            return false;
        }
        if (fw->automatic) {
            fw->base = (limit - ROUND_UP(elf.size, 0x2000)) &
                       ~(elf.alignment - 1);
        }
        fw->size = elf.size;
        if (!ia64_firmware_placement_valid(fw->base,
                                          ROUND_UP(fw->size, 0x2000), limit)) {
            ia64_firmware_elf_clear(&elf);
            error_setg(errp, "firmware-base overlaps reserved memory or "
                       "is outside low RAM");
            return false;
        }
        ia64_firmware_elf_clear(&elf);
        if (!ia64_firmware_elf_load(image, image_size, fw->base, &elf, errp)) {
            return false;
        }
        fw->elf = true;
        fw->entry = elf.entry;
        fw->global_pointer = elf.global_pointer;
        fw->pal_entry = elf.pal_entry;
        for (i = 0; i < elf.segments->len; i++) {
            IA64FirmwareSegment *segment = &g_array_index(
                elf.segments, IA64FirmwareSegment, i);
            g_autofree char *name = g_strdup_printf("%s.segment.%u",
                                                    machine->firmware, i);
            rom_add_blob_fixed(name, elf.data + segment->offset, segment->size,
                               fw->base + segment->offset);
        }
        ia64_firmware_elf_clear(&elf);
    } else {
        if (fw->automatic || fw->base != IA64_PLATFORM_FIRMWARE_BASE) {
            error_setg(errp,
                       "raw IA-64 firmware requires firmware-base=0x100000");
            return false;
        }
        if (!image_size || image_size > IA64_PLATFORM_FIRMWARE_SIZE) {
            error_setg(errp, "invalid raw IA-64 firmware size");
            return false;
        }
        fw->legacy_raw = true;
        rom_add_blob_fixed(machine->firmware, image, image_size, fw->base);
    }
    return true;
}

char *ia64_machine_resolve_nvram_path(MachineState *machine,
                                      const char *nvram_path)
{
    g_autofree char *firmware_path = NULL;
    g_autofree char *directory = NULL;

    if (nvram_path && g_strcmp0(nvram_path, "auto") != 0) {
        return g_strdup(nvram_path);
    }
    if (!machine->firmware || g_str_equal(machine->firmware, "none")) {
        return NULL;
    }

    firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (!firmware_path) {
        firmware_path = g_strdup(machine->firmware);
    }
    directory = g_path_get_dirname(firmware_path);
    return g_build_filename(directory, "nvram", NULL);
}

static void ia64_machine_firmware_done(Notifier *notifier, void *data)
{
    IA64MachineFirmwareNotifier *context = container_of(
        notifier, IA64MachineFirmwareNotifier, notifier);
    g_autofree uint8_t *image = NULL;
    IA64FirmwareEntrypoint entrypoint;
    CPUState *cs;

    (void)data;
    ia64_machine_cleanup_firmware_notifier(context);
    if (context->machine_done != NULL) {
        context->machine_done(context->opaque);
    }
    if (context->machine->firmware == NULL || context->firmware_size == 0 ||
        context->boot_info == NULL) {
        return;
    }

    /* A valid PE32+ plabel replaces the configured flat-image entry point. */
    image = g_malloc(context->firmware_size);
    cpu_physical_memory_read(context->firmware_base, image,
                             context->firmware_size);
    if (!ia64_loader_parse_pe_plabel(image, context->firmware_size,
                                     &entrypoint)) {
        return;
    }

    CPU_FOREACH(cs) {
        IA64BootInfo info = context->boot_info(
            cs->cpu_index, entrypoint.entry, entrypoint.global_pointer,
            context->opaque);

        ia64_cpu_set_boot_info(IA64_CPU(cs), &info);
        ia64_cpu_reset_to_boot_info(IA64_CPU(cs));
    }
}

void ia64_machine_init_firmware_notifier(
    IA64MachineFirmwareNotifier *notifier, MachineState *machine,
    hwaddr firmware_base, size_t firmware_size,
    IA64MachineFirmwareBootInfoFn boot_info,
    IA64MachineDoneFn machine_done, void *opaque)
{
    g_assert(notifier != NULL);
    g_assert(machine != NULL);
    g_assert(!notifier->registered);

    *notifier = (IA64MachineFirmwareNotifier) {
        .notifier.notify = ia64_machine_firmware_done,
        .machine = machine,
        .firmware_base = firmware_base,
        .firmware_size = firmware_size,
        .boot_info = boot_info,
        .machine_done = machine_done,
        .opaque = opaque,
    };
    notifier->registered = true;
    qemu_add_machine_init_done_notifier(&notifier->notifier);
}

void ia64_machine_cleanup_firmware_notifier(
    IA64MachineFirmwareNotifier *notifier)
{
    if (notifier != NULL && notifier->registered) {
        qemu_remove_machine_init_done_notifier(&notifier->notifier);
        notifier->registered = false;
    }
}
