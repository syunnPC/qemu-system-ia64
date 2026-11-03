/*
 * HP IA-64 legacy INT 10h/VBE bridge
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_HP_INT10_H
#define HW_IA64_HP_INT10_H

#include "hw/pci/pci.h"

#define HP_IA64_INT10_IO_BASE 0x000001e0U
#define HP_IA64_INT10_IO_SIZE 0x00000010U
#define HP_IA64_INT10_MAX_MODES 96U

typedef struct HPIA64Int10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
} HPIA64Int10Registers;

typedef struct HPIA64VbeMode {
    uint16_t number;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} HPIA64VbeMode;

typedef struct HPIA64Int10 {
    MemoryRegion service_io;
    MemoryRegion *service_parent;
    PCIDevice *vga;
    AddressSpace *vga_io;
    hwaddr framebuffer_base;
    uint64_t framebuffer_size;
    uint8_t framebuffer_bar;
    uint8_t mmio_bar;

    HPIA64Int10Registers request;
    HPIA64Int10Registers result;
    uint32_t input_signature;
    uint8_t response[512];
    uint16_t response_length;
    uint16_t response_offset;
    uint8_t input_signature_words;
    uint8_t dpms_state;
    uint8_t legacy_mode;
    uint8_t legacy_columns;

    HPIA64VbeMode modes[HP_IA64_INT10_MAX_MODES];
    uint16_t mode_count;
    uint32_t prefx;
    uint32_t prefy;
    uint32_t maxx;
    uint32_t maxy;
    uint8_t edid[384];
    uint8_t edid_blocks;
    bool initialized;
    bool vmstate_registered;
} HPIA64Int10;

typedef struct HPIA64Int10Config {
    Object *owner;
    PCIDevice *vga;

    /* The x86 INT 10h stub issues its private requests through this root. */
    MemoryRegion *service_io;

    /* Legacy VGA and Bochs VBE cycles are issued through this root. */
    AddressSpace *vga_io;

    hwaddr framebuffer_base;
    uint8_t framebuffer_bar;
    uint8_t mmio_bar;
    const char *region_name;
} HPIA64Int10Config;

bool hp_ia64_int10_init(HPIA64Int10 *s, const HPIA64Int10Config *config,
                        Error **errp);
void hp_ia64_int10_reset(HPIA64Int10 *s);
void hp_ia64_int10_destroy(HPIA64Int10 *s);

#endif
