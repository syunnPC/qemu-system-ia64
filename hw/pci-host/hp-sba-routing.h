/*
 * HP SBA/LBA routing helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_SBA_ROUTING_H
#define HW_PCI_HOST_HP_SBA_ROUTING_H

#include "system/memory.h"

typedef struct HPSBARootSpec {
    uint32_t hpa_offset;
    uint32_t bus_num;
    bool user_attachable;
} HPSBARootSpec;

/* Multiple physical ropes may select one LBA root. */
typedef struct HPSBARopeMap {
    uint8_t rope;
    uint8_t root;
} HPSBARopeMap;

typedef struct HPSBARoutingVariant HPSBARoutingVariant;

typedef bool (*HPSBADirectRouteLookup)(const HPSBARoutingVariant *variant,
                                       uint64_t route,
                                       unsigned int *root_index);
typedef uint64_t (*HPSBAExtendAddress)(void *opaque, uint64_t address);
typedef void (*HPSBAInterruptDeliver)(void *opaque, uint64_t address,
                                      uint32_t data);

struct HPSBARoutingVariant {
    const HPSBARootSpec *roots;
    size_t root_count;

    const HPSBARopeMap *rope_map;
    size_t rope_map_count;
    uint8_t physical_rope_count;
    uint8_t direct_range_count;

    enum device_endian sba_reg_endianness;
    enum device_endian lba_reg_endianness;
    enum device_endian config_endianness;

    HPSBADirectRouteLookup direct_route_lookup;
    HPSBAExtendAddress extend_address;
    HPSBAInterruptDeliver deliver_interrupt;
};

bool hp_sba_routing_variant_valid(const HPSBARoutingVariant *variant);
bool hp_sba_routing_root_for_rope(const HPSBARoutingVariant *variant,
                                  unsigned int rope, unsigned int *root_index);
bool hp_sba_routing_rope_for_root(const HPSBARoutingVariant *variant,
                                  unsigned int root_index, unsigned int ordinal,
                                  unsigned int *rope);
bool hp_sba_routing_direct_root(const HPSBARoutingVariant *variant,
                                uint64_t route, unsigned int *root_index);

uint64_t hp_sba_routing_extend_address(const HPSBARoutingVariant *variant,
                                       void *opaque, uint64_t address);
void hp_sba_routing_deliver_interrupt(const HPSBARoutingVariant *variant,
                                      void *opaque, uint64_t address,
                                      uint32_t data);

bool hp_sba_reg64_access_valid(uint64_t offset, unsigned int size);
/* True when a valid access includes logical register bits 31:0. */
bool hp_sba_reg64_access_covers_low32(enum device_endian endianness,
                                      uint64_t offset, unsigned int size);
bool hp_sba_reg64_read(enum device_endian endianness, uint64_t reg,
                       uint64_t offset, unsigned int size, uint64_t *value);
bool hp_sba_reg64_write(enum device_endian endianness, uint64_t *reg,
                        uint64_t offset, unsigned int size, uint64_t value);

#endif
