/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host-side helpers for the IA-64 platform descriptor.
 */

#ifndef HW_IA64_PLATFORM_H
#define HW_IA64_PLATFORM_H

#include "hw/ia64/ia64_platform_abi.h"
#include "qapi/error.h"
#include "exec/hwaddr.h"
#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_IA64_PLATFORM_DESCRIPTOR_DEVICE \
    "ia64-platform-descriptor-device"
OBJECT_DECLARE_SIMPLE_TYPE(IA64PlatformDescriptorDevice,
                           IA64_PLATFORM_DESCRIPTOR_DEVICE)

typedef struct IA64PlatformDescriptorArrays {
    const IA64PlatformRamRange *ram_ranges;
    uint32_t ram_range_count;
    const IA64PlatformPciRoot *pci_roots;
    uint32_t pci_root_count;
    const IA64PlatformIoSapic *io_sapics;
    uint32_t io_sapic_count;
    const IA64PlatformPciRoute *pci_routes;
    uint32_t pci_route_count;
    const IA64PlatformI2000Profile *profiles;
    uint32_t profile_count;
} IA64PlatformDescriptorArrays;

/* Initialize the fixed little-endian i2000 profile entry. */
void ia64_platform_i2000_profile_init(IA64PlatformI2000Profile *profile);

/*
 * Complete the firmware entry transport.  descriptor_size is the
 * descriptor's exact TotalSize, not the size of its immutable ROM page.
 */
typedef struct IA64PlatformFirmwareArgs {
    uint64_t descriptor_gpa;
    uint64_t descriptor_size;
    uint64_t firmware_compat_flags;
    uint32_t platform_id;
} IA64PlatformFirmwareArgs;

bool ia64_platform_desc_validate(const IA64PlatformDescriptor *descriptor,
                                 size_t available_size,
                                 uint32_t expected_platform_id, Error **errp);
void ia64_platform_desc_finalize(IA64PlatformDescriptor *descriptor,
                                 size_t available_size);

/*
 * Pack the variable arrays after an already little-endian descriptor header.
 * Every multi-byte field in the array entries must likewise already be in
 * little-endian wire order.  Structural offset/count/stride fields and the
 * checksum are filled here.
 *
 * The writable storage and descriptor_size objects must not overlap each
 * other or any read-only input: header, arrays, or a nonempty array backing
 * store.  The function rejects such aliases before modifying either output.
 */
bool ia64_platform_desc_build(void *storage, size_t storage_size,
                              const IA64PlatformDescriptor *header,
                              const IA64PlatformDescriptorArrays *arrays,
                              size_t *descriptor_size, Error **errp);

/*
 * Validate the numeric placement of the complete immutable assist page,
 * including representable outward rounding to the EFI IA-64 resource-page
 * alignment.  Machine-specific collision checks and EFI Reserved publication
 * remain the board builder's responsibility.
 */
bool ia64_platform_desc_mapping_valid(hwaddr gpa, size_t descriptor_size);

/*
 * The descriptor must already have passed ia64_platform_desc_validate().
 * Check that its complete outward-rounded EFI reservation lies in one of its
 * declared RAM ranges.
 */
bool ia64_platform_desc_mapping_in_ram(const IA64PlatformDescriptor *descriptor,
                                       hwaddr gpa, size_t descriptor_size);

/*
 * Install one immutable, zero-padded firmware-assist page over existing
 * ordinary writable RAM.  The owner must derive from DeviceState.  On
 * failure, region is either untouched or fully unmapped and finalized.
 */
bool ia64_platform_desc_install_rom(MemoryRegion *region, Object *owner,
                                    const char *name, hwaddr gpa,
                                    const void *descriptor,
                                    size_t descriptor_size, Error **errp);

/*
 * Build, validate, and install a descriptor owned by an internal DeviceState.
 * Inputs are consumed synchronously and are not retained.  @parent owns the
 * returned borrowed pointer through its child property @name.  System RAM must
 * already back the complete descriptor page.  Machine-specific placement and
 * collision checks remain the caller's responsibility.
 */
IA64PlatformDescriptorDevice *
ia64_platform_desc_device_create(Object *parent, const char *name, hwaddr gpa,
                                 const IA64PlatformDescriptor *header,
                                 const IA64PlatformDescriptorArrays *arrays,
                                 Error **errp);

/*
 * Remove and unrealize a descriptor created above.  The borrowed pointer is
 * invalid after this call unless the caller independently holds a reference.
 */
void ia64_platform_desc_device_destroy(IA64PlatformDescriptorDevice *device);

/* Leave @args unchanged unless the descriptor is successfully installed. */
bool ia64_platform_desc_device_get_firmware_args(
    const IA64PlatformDescriptorDevice *device, IA64PlatformFirmwareArgs *args);

#endif /* HW_IA64_PLATFORM_H */
