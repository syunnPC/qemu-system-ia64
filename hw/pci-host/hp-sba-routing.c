/*
 * HP SBA/LBA routing helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-sba-routing.h"
#include "qemu/bitops.h"

static bool hp_sba_endianness_valid(enum device_endian endianness)
{
    return endianness == DEVICE_LITTLE_ENDIAN ||
           endianness == DEVICE_BIG_ENDIAN;
}

bool hp_sba_routing_variant_valid(const HPSBARoutingVariant *variant)
{
    size_t i;
    size_t j;

    if (!variant || !variant->roots || !variant->root_count ||
        !variant->rope_map || !variant->rope_map_count ||
        !variant->physical_rope_count ||
        !hp_sba_endianness_valid(variant->sba_reg_endianness) ||
        !hp_sba_endianness_valid(variant->lba_reg_endianness) ||
        !hp_sba_endianness_valid(variant->config_endianness) ||
        !variant->extend_address || !variant->deliver_interrupt ||
        (variant->direct_range_count && !variant->direct_route_lookup)) {
        return false;
    }

    for (i = 0; i < variant->root_count; i++) {
        for (j = i + 1; j < variant->root_count; j++) {
            if (variant->roots[i].hpa_offset == variant->roots[j].hpa_offset ||
                variant->roots[i].bus_num == variant->roots[j].bus_num) {
                return false;
            }
        }
    }

    for (i = 0; i < variant->rope_map_count; i++) {
        const HPSBARopeMap *map = &variant->rope_map[i];

        if (map->rope >= variant->physical_rope_count ||
            map->root >= variant->root_count) {
            return false;
        }
        for (j = i + 1; j < variant->rope_map_count; j++) {
            if (map->rope == variant->rope_map[j].rope) {
                return false;
            }
        }
    }

    /* Every described root must be connected to at least one rope. */
    for (i = 0; i < variant->root_count; i++) {
        bool found = false;

        for (j = 0; j < variant->rope_map_count; j++) {
            if (variant->rope_map[j].root == i) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

bool hp_sba_routing_root_for_rope(const HPSBARoutingVariant *variant,
                                  unsigned int rope, unsigned int *root_index)
{
    size_t i;

    if (!variant || !root_index) {
        return false;
    }

    for (i = 0; i < variant->rope_map_count; i++) {
        if (variant->rope_map[i].rope == rope &&
            variant->rope_map[i].root < variant->root_count) {
            *root_index = variant->rope_map[i].root;
            return true;
        }
    }
    return false;
}

bool hp_sba_routing_rope_for_root(const HPSBARoutingVariant *variant,
                                  unsigned int root_index, unsigned int ordinal,
                                  unsigned int *rope)
{
    size_t i;

    if (!variant || !rope || root_index >= variant->root_count) {
        return false;
    }

    for (i = 0; i < variant->rope_map_count; i++) {
        if (variant->rope_map[i].root != root_index) {
            continue;
        }
        if (ordinal) {
            ordinal--;
            continue;
        }
        *rope = variant->rope_map[i].rope;
        return true;
    }
    return false;
}

bool hp_sba_routing_direct_root(const HPSBARoutingVariant *variant,
                                uint64_t route, unsigned int *root_index)
{
    unsigned int candidate;

    if (!variant || !root_index || !variant->direct_route_lookup ||
        !variant->direct_route_lookup(variant, route, &candidate) ||
        candidate >= variant->root_count) {
        return false;
    }

    *root_index = candidate;
    return true;
}

uint64_t hp_sba_routing_extend_address(const HPSBARoutingVariant *variant,
                                       void *opaque, uint64_t address)
{
    assert(variant && variant->extend_address);
    return variant->extend_address(opaque, address);
}

void hp_sba_routing_deliver_interrupt(const HPSBARoutingVariant *variant,
                                      void *opaque, uint64_t address,
                                      uint32_t data)
{
    assert(variant && variant->deliver_interrupt);
    variant->deliver_interrupt(opaque, address, data);
}

bool hp_sba_reg64_access_valid(uint64_t offset, unsigned int size)
{
    offset &= 7;
    return (size == 4 && (offset == 0 || offset == 4)) ||
           (size == 8 && offset == 0);
}

static bool hp_sba_reg64_shift(enum device_endian endianness, uint64_t offset,
                               unsigned int size, unsigned int *shift)
{
    unsigned int lane = offset & 7;

    if (!shift || !hp_sba_endianness_valid(endianness) ||
        !hp_sba_reg64_access_valid(offset, size)) {
        return false;
    }

    if (endianness == DEVICE_LITTLE_ENDIAN || size == 8) {
        *shift = lane * 8;
    } else {
        *shift = 64 - size * 8 - lane * 8;
    }
    return true;
}

bool hp_sba_reg64_access_covers_low32(enum device_endian endianness,
                                      uint64_t offset, unsigned int size)
{
    unsigned int shift;

    return hp_sba_reg64_shift(endianness, offset, size, &shift) && !shift;
}

bool hp_sba_reg64_read(enum device_endian endianness, uint64_t reg,
                       uint64_t offset, unsigned int size, uint64_t *value)
{
    unsigned int shift;

    if (!value || !hp_sba_reg64_shift(endianness, offset, size, &shift)) {
        return false;
    }

    *value = extract64(reg, shift, size * 8);
    return true;
}

bool hp_sba_reg64_write(enum device_endian endianness, uint64_t *reg,
                        uint64_t offset, unsigned int size, uint64_t value)
{
    unsigned int shift;

    if (!reg || !hp_sba_reg64_shift(endianness, offset, size, &shift)) {
        return false;
    }

    *reg = deposit64(*reg, shift, size * 8, value);
    return true;
}
