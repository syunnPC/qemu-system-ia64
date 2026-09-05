/*
 * QEMU ATI Radeon R100 fixed-function 3D emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synchronous software command processor and fixed-function rasterizer.
 * Technical references are listed in docs/devel/gpu-emulation-provenance.rst.
 *
 * TODO: Implement cube-map textures, ZPASS queries, and external index
 * buffers.
 */

/*
 * The render-target tiling address equations are adapted from Mesa's
 * radeon_span.c.  The following upstream notice applies to those equations:
 *
 * Copyright (C) The Weather Channel, Inc. 2002. All Rights Reserved.
 * Copyright 2000, 2001 ATI Technologies Inc., Ontario, Canada, and
 *                      VA Linux Systems Inc., Fremont, California.
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the initial
 * release of the Radeon 8500 driver under the XFree86 license. This notice
 * must be preserved.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Kevin E. Martin <martin@valinux.com>
 *   Gareth Hughes <gareth@valinux.com>
 *   Keith Whitwell <keith@tungstengraphics.com>
 */

#include "qemu/osdep.h"
#include <math.h>
#include "ati_int.h"
#include "ati_regs.h"
#include "exec/target_page.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "trace.h"

#define R100_CONTEXT_BASE 0x1c00
#define R100_CONTEXT_END  0x1dff
#define R100_MAX_DRAW_VERTICES 16384
#define R100_MAX_DRAW_PIXELS (16U * 1024U * 1024U)
#define R100_MAX_RING_DWORDS (1U << 20)
#define R100_MAX_COMMAND_WORK (2U * R100_MAX_RING_DWORDS)
#define R100_MAX_PACKET_DWORDS (1U << 14)
#define R100_VERTEX_WORK_OVERHEAD 8U
#define R100_MAX_VERTEX_COMPONENTS 64
#define R100_MAX_SAFE_SCREEN_COORD 1048576.0f
#define R100_MAX_SAFE_TEXEL_COORD 1073741824.0f
#define R100_DIRTY_BATCH_RANGES 8

/* Merge local-VRAM pages written by one synchronous draw command. */
typedef struct R100DirtyRange {
    uint64_t start;
    uint64_t end;
} R100DirtyRange;

typedef struct R100DirtyBatch {
    R100DirtyRange ranges[R100_DIRTY_BATCH_RANGES];
    unsigned int count;
} R100DirtyBatch;

typedef struct R100Color {
    float r;
    float g;
    float b;
    float a;
} R100Color;

typedef struct R100Vertex {
    float x;
    float y;
    float z;
    float w;
    float s[3];
    float t[3];
    float q[3];
    R100Color color;
    R100Color specular;
} R100Vertex;

typedef struct R100Stream {
    uint64_t base;
    uint32_t pos;
    uint32_t remaining;
    uint32_t mask;
    bool ring;
} R100Stream;

typedef struct R100TextureAxis {
    int texel[2];
    bool border[2];
    float fraction;
} R100TextureAxis;

typedef struct R100TextureGradients {
    float dsdx[3];
    float dsdy[3];
    float dtdx[3];
    float dtdy[3];
    float dqdx[3];
    float dqdy[3];
} R100TextureGradients;

typedef struct R100TextureState {
    uint32_t txformat;
    uint32_t txoffset;
    unsigned int format;
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int cpp;
    unsigned int block_bytes;
    uint64_t offset;
    R100Color border_color;
    unsigned int smode;
    unsigned int tmode;
    bool d3d_border;
    bool yuv_to_rgb;
} R100TextureState;

typedef struct R100TextureBlockCache {
    uint64_t address[4];
    uint8_t data[4][16];
    unsigned int count;
} R100TextureBlockCache;

static uint32_t *r100_context_reg(ATI3DState *r, hwaddr addr)
{
    if (addr < R100_CONTEXT_BASE || addr > R100_CONTEXT_END ||
        (addr & 3)) {
        return NULL;
    }
    return &r->context[(addr - R100_CONTEXT_BASE) / 4];
}

static uint32_t r100_context_read(const ATI3DState *r, hwaddr addr)
{
    return r->context[(addr - R100_CONTEXT_BASE) / 4];
}

static uint32_t *r100_register_ptr(ATI3DState *r, hwaddr addr)
{
    uint32_t *reg = r100_context_reg(r, addr);

    if (reg != NULL) {
        return reg;
    }
    if (addr >= R100_SCRATCH_REG0 && addr <= R100_SCRATCH_REG7 &&
        !(addr & 3)) {
        return &r->scratch[(addr - R100_SCRATCH_REG0) / 4];
    }
    switch (addr) {
    case R100_RE_TOP_LEFT:
        return &r->re_top_left;
    case R100_RE_MISC:
        return &r->re_misc;
    case R100_SE_VTX_FMT:
        return &r->se_vtx_fmt;
    case R100_SE_VF_CNTL:
        return &r->se_vf_cntl;
    case R100_CP_RB_BASE:
        return &r->cp_rb_base;
    case R100_CP_RB_CNTL:
        return &r->cp_rb_cntl;
    case R100_CP_RB_RPTR_ADDR:
        return &r->cp_rb_rptr_addr;
    case R100_CP_RB_RPTR:
        return &r->cp_rb_rptr;
    case R100_CP_RB_RPTR_WR:
        return &r->cp_rb_rptr_wr;
    case R100_CP_RB_WPTR:
        return &r->cp_rb_wptr;
    case R100_CP_IB_BASE:
        return &r->cp_ib_base;
    case R100_CP_IB_BUFSZ:
        return &r->cp_ib_bufsz;
    case R100_CP_CSQ_CNTL:
        return &r->cp_csq_cntl;
    case R100_CP_CSQ_MODE:
        return &r->cp_csq_mode;
    case R100_SCRATCH_UMSK:
        return &r->scratch_umsk;
    case R100_SCRATCH_ADDR:
        return &r->scratch_addr;
    case R100_CP_ME_RAM_ADDR:
        return &r->me_ram_addr;
    case R100_CP_ME_RAM_RADDR:
        return &r->me_ram_raddr;
    case R100_CP_ME_RAM_DATAH:
        return &r->me_datah;
    case R100_CP_ME_RAM_DATAL:
        return &r->me_datal;
    case MC_FB_LOCATION:
        return &r->mc_fb_location;
    case MC_AGP_LOCATION:
        return &r->mc_agp_location;
    case AGP_BASE:
        return &r->agp_base;
    case R100_AIC_CNTL:
        return &r->aic_cntl;
    case R100_AIC_PT_BASE:
        return &r->aic_pt_base;
    case R100_AIC_LO_ADDR:
        return &r->aic_lo_addr;
    case R100_AIC_HI_ADDR:
        return &r->aic_hi_addr;
    default:
        return NULL;
    }
}

static uint32_t r100_merge_write(uint32_t old, hwaddr addr, uint64_t data,
                                 unsigned int size)
{
    unsigned int offset = addr & 3;

    if (size == 4 && offset == 0) {
        return data;
    }
    if (size > 4 || offset + size > 4) {
        return old;
    }
    return deposit32(old, offset * 8, size * 8, data);
}

static uint64_t r100_extract_read(uint32_t value, hwaddr addr,
                                  unsigned int size)
{
    unsigned int offset = addr & 3;

    if (size == 4 && offset == 0) {
        return value;
    }
    if (size > 4 || offset + size > 4) {
        return 0;
    }
    return extract32(value, offset * 8, size * 8);
}

static bool r100_programmed_vram_span(ATIVGAState *s, uint64_t address,
                                      uint64_t length, uint64_t *offset,
                                      uint64_t *span)
{
    ATI3DState *r = &s->r100_3d;
    uint64_t start = (uint64_t)(r->mc_fb_location & 0xffffU) << 16;
    uint64_t top = (uint64_t)(r->mc_fb_location >> 16) << 16 | 0xffffU;
    uint64_t local;

    if (!length || top < start || address < start || address > top) {
        return false;
    }
    local = address - start;
    if (local >= s->vga.vram_size) {
        return false;
    }
    *offset = local;
    *span = MIN(length, MIN(top - address + 1,
                            s->vga.vram_size - local));
    return true;
}

static bool r100_zero_vram_span(ATIVGAState *s, uint64_t address,
                                uint64_t length, uint64_t *offset,
                                uint64_t *span)
{
    ATI3DState *r = &s->r100_3d;
    uint64_t start = (uint64_t)(r->mc_fb_location & 0xffffU) << 16;
    uint64_t top = (uint64_t)(r->mc_fb_location >> 16) << 16 | 0xffffU;

    /* Keep a zero-based view only while no valid framebuffer window exists. */
    if (!length || top >= start || address >= s->vga.vram_size) {
        return false;
    }
    *offset = address;
    *span = MIN(length, s->vga.vram_size - address);
    return true;
}

bool ati_r100_gpu_vram_offset(ATIVGAState *s, uint64_t address,
                              uint64_t length, uint64_t *offset)
{
    uint64_t span;

    return (r100_programmed_vram_span(s, address, length, offset, &span) ||
            r100_zero_vram_span(s, address, length, offset, &span)) &&
           span == length;
}

static bool r100_in_gart(const ATI3DState *r, uint64_t address)
{
    return (r->aic_cntl & R100_PCIGART_TRANSLATE_EN) &&
           address >= r->aic_lo_addr && address <= r->aic_hi_addr;
}

static bool r100_translate_gart(ATIVGAState *s, uint64_t address,
                                uint64_t length, uint64_t *translated,
                                uint64_t *span)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t entry;
    uint64_t page;

    page = (address - r->aic_lo_addr) >> 12;
    if (page > (UINT32_MAX - r->aic_pt_base) / 4 ||
        pci_dma_read(&s->dev, r->aic_pt_base + page * 4,
                     &entry, sizeof(entry)) != MEMTX_OK) {
        return false;
    }
    *translated = (uint64_t)(le32_to_cpu(entry) & ~0xfffU) |
                  (address & 0xfffU);
    *span = MIN(length, MIN(0x1000U - (address & 0xfffU),
                            (uint64_t)r->aic_hi_addr - address + 1));
    return true;
}

static void r100_cap_span_at(uint64_t address, uint64_t boundary,
                             uint64_t *span)
{
    if (address < boundary) {
        *span = MIN(*span, boundary - address);
    }
}

static bool r100_gpu_decode(ATIVGAState *s, uint64_t address,
                            uint64_t length, bool *vram,
                            uint64_t *translated, uint64_t *span)
{
    ATI3DState *r = &s->r100_3d;
    uint64_t fb_start = (uint64_t)(r->mc_fb_location & 0xffffU) << 16;
    uint64_t fb_top = (uint64_t)(r->mc_fb_location >> 16) << 16 | 0xffffU;

    if (r100_programmed_vram_span(s, address, length, translated, span)) {
        *vram = true;
        return true;
    }
    if (r100_in_gart(r, address)) {
        *vram = false;
        if (!r100_translate_gart(s, address, length, translated, span)) {
            return false;
        }
        if (fb_top >= fb_start && s->vga.vram_size) {
            r100_cap_span_at(address, fb_start, span);
        }
        return true;
    }
    if (r100_zero_vram_span(s, address, length, translated, span)) {
        *vram = true;
        if (r->aic_cntl & R100_PCIGART_TRANSLATE_EN) {
            r100_cap_span_at(address, r->aic_lo_addr, span);
        }
        return true;
    }
    if (r->aic_cntl & R100_DIS_OUT_OF_PCI_GART_ACCESS) {
        return false;
    }
    *vram = false;
    *translated = address;
    *span = length;
    if (fb_top >= fb_start && s->vga.vram_size) {
        r100_cap_span_at(address, fb_start, span);
    }
    if (r->aic_cntl & R100_PCIGART_TRANSLATE_EN) {
        r100_cap_span_at(address, r->aic_lo_addr, span);
    }
    return true;
}

bool ati_r100_gpu_access_valid(ATIVGAState *s, uint64_t address,
                               uint64_t length, bool is_write)
{
    uint64_t done = 0;
    uint64_t translated;
    uint64_t span;
    bool vram;

    if (length > UINT64_MAX - address) {
        return false;
    }
    while (done < length) {
        if (!r100_gpu_decode(s, address + done, length - done, &vram,
                             &translated, &span)) {
            return false;
        }
        if (!vram &&
            !dma_memory_valid(pci_get_address_space(&s->dev), translated,
                              span, is_write ? DMA_DIRECTION_FROM_DEVICE :
                                               DMA_DIRECTION_TO_DEVICE,
                              MEMTXATTRS_UNSPECIFIED)) {
            return false;
        }
        done += span;
    }
    return true;
}

bool ati_r100_gpu_ranges_overlap(ATIVGAState *s, uint64_t first,
                                 uint64_t first_length, uint64_t second,
                                 uint64_t second_length, bool *overlap)
{
    uint64_t first_done = 0;

    *overlap = false;
    if (first_length > UINT64_MAX - first ||
        second_length > UINT64_MAX - second) {
        return false;
    }
    while (first_done < first_length) {
        uint64_t first_translated;
        uint64_t first_span;
        uint64_t second_done = 0;
        bool first_vram;

        if (!r100_gpu_decode(s, first + first_done,
                             first_length - first_done, &first_vram,
                             &first_translated, &first_span)) {
            return false;
        }
        while (second_done < second_length) {
            uint64_t second_translated;
            uint64_t second_span;
            bool second_vram;

            if (!r100_gpu_decode(s, second + second_done,
                                 second_length - second_done, &second_vram,
                                 &second_translated, &second_span)) {
                return false;
            }
            if (first_vram == second_vram &&
                first_translated < second_translated + second_span &&
                second_translated < first_translated + first_span) {
                *overlap = true;
                return true;
            }
            second_done += second_span;
        }
        first_done += first_span;
    }
    return true;
}

bool ati_r100_gpu_read(ATIVGAState *s, uint64_t address, void *buf,
                       uint64_t length)
{
    uint8_t *out = buf;
    uint64_t done = 0;
    uint64_t translated;
    uint64_t span;
    bool vram;

    if (length > UINT64_MAX - address) {
        return false;
    }
    while (done < length) {
        if (!r100_gpu_decode(s, address + done, length - done, &vram,
                             &translated, &span)) {
            return false;
        }
        if (vram) {
            memcpy(out + done, s->vga.vram_ptr + translated, span);
        } else if (pci_dma_read(&s->dev, translated, out + done,
                                span) != MEMTX_OK) {
            return false;
        }
        done += span;
    }
    return true;
}

static void r100_dirty_batch_flush(ATIVGAState *s, R100DirtyBatch *batch)
{
    unsigned int i;

    for (i = 0; i < batch->count; i++) {
        R100DirtyRange *range = &batch->ranges[i];

        memory_region_set_dirty(&s->vga.vram, range->start,
                                range->end - range->start);
    }
    batch->count = 0;
}

static void r100_dirty_batch_add(ATIVGAState *s, R100DirtyBatch *batch,
                                 uint64_t offset, uint64_t length)
{
    uint64_t page_size = qemu_target_page_size();
    uint64_t page_mask = page_size - 1;
    uint64_t start = offset & ~page_mask;
    uint64_t end = MIN((offset + length + page_mask) & ~page_mask,
                       s->vga.vram_size);
    unsigned int i = 0;

    while (i < batch->count) {
        R100DirtyRange *range = &batch->ranges[i];

        if (start <= range->end && range->start <= end) {
            start = MIN(start, range->start);
            end = MAX(end, range->end);
            batch->ranges[i] = batch->ranges[--batch->count];
            i = 0;
        } else {
            i++;
        }
    }
    if (batch->count == R100_DIRTY_BATCH_RANGES) {
        r100_dirty_batch_flush(s, batch);
    }
    batch->ranges[batch->count++] = (R100DirtyRange) {
        .start = start,
        .end = end,
    };
}

static bool r100_gpu_write(ATIVGAState *s, uint64_t address, const void *buf,
                           uint64_t length, bool dirty,
                           R100DirtyBatch *batch)
{
    const uint8_t *in = buf;
    uint64_t done = 0;
    uint64_t translated;
    uint64_t span;
    bool vram;

    if (length > UINT64_MAX - address) {
        return false;
    }
    while (done < length) {
        if (!r100_gpu_decode(s, address + done, length - done, &vram,
                             &translated, &span)) {
            return false;
        }
        if (vram) {
            memcpy(s->vga.vram_ptr + translated, in + done, span);
            if (dirty) {
                if (batch) {
                    r100_dirty_batch_add(s, batch, translated, span);
                } else {
                    memory_region_set_dirty(&s->vga.vram, translated, span);
                }
            }
        } else if (pci_dma_write(&s->dev, translated, in + done,
                                 span) != MEMTX_OK) {
            return false;
        }
        done += span;
    }
    return true;
}

bool ati_r100_gpu_write(ATIVGAState *s, uint64_t address, const void *buf,
                        uint64_t length, bool dirty)
{
    return r100_gpu_write(s, address, buf, length, dirty, NULL);
}

static bool r100_gpu_read_u32(ATIVGAState *s, uint64_t address,
                              uint32_t *value)
{
    uint32_t raw;

    if (!ati_r100_gpu_read(s, address, &raw, sizeof(raw))) {
        return false;
    }
    *value = le32_to_cpu(raw);
    return true;
}

static bool r100_gpu_write_u32(ATIVGAState *s, uint64_t address,
                               uint32_t value, bool dirty)
{
    uint32_t raw = cpu_to_le32(value);

    return ati_r100_gpu_write(s, address, &raw, sizeof(raw), dirty);
}

static uint32_t r100_swap_word(uint32_t value, unsigned int mode)
{
    switch (mode & 3) {
    case 1: /* 16-bit swap */
        return ((value & 0x00ff00ffU) << 8) |
               ((value & 0xff00ff00U) >> 8);
    case 2: /* 32-bit swap */
        return bswap32(value);
    case 3: /* Half-dword swap */
        return value << 16 | value >> 16;
    default:
        return value;
    }
}

static float r100_float(uint32_t value)
{
    float result;

    memcpy(&result, &value, sizeof(result));
    return result;
}

static float r100_clamp_float(float value, float low, float high)
{
    if (!isfinite(value)) {
        return low;
    }
    return MIN(MAX(value, low), high);
}

static uint8_t r100_float_to_u8(float value)
{
    return (uint8_t)(r100_clamp_float(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static unsigned int r100_vertex_dwords(uint32_t format)
{
    unsigned int size = 2;

    size += !!(format & R100_VTX_FMT_Z);
    size += !!(format & R100_VTX_FMT_W0);
    size += (format & R100_VTX_FMT_FPCOLOR) ? 3 : 0;
    size += !!(format & R100_VTX_FMT_FPALPHA);
    size += !!(format & R100_VTX_FMT_PKCOLOR);
    size += (format & R100_VTX_FMT_FPSPEC) ? 3 : 0;
    size += !!(format & R100_VTX_FMT_FPFOG);
    size += !!(format & R100_VTX_FMT_PKSPEC);
    size += (format & R100_VTX_FMT_ST0) ? 2 : 0;
    size += (format & R100_VTX_FMT_ST1) ? 2 : 0;
    size += !!(format & R100_VTX_FMT_Q1);
    size += (format & R100_VTX_FMT_ST2) ? 2 : 0;
    size += !!(format & R100_VTX_FMT_Q2);
    size += (format & R100_VTX_FMT_ST3) ? 2 : 0;
    size += !!(format & R100_VTX_FMT_Q3);
    size += !!(format & R100_VTX_FMT_Q0);
    size += extract32(format, 15, 3);
    size += (format & R100_VTX_FMT_N0) ? 3 : 0;
    size += (format & R100_VTX_FMT_XY1) ? 2 : 0;
    size += !!(format & R100_VTX_FMT_Z1);
    size += !!(format & R100_VTX_FMT_W1);
    size += !!(format & R100_VTX_FMT_N1);
    return size;
}

static bool r100_parse_vertex(const uint32_t *words, unsigned int count,
                              uint32_t format, uint32_t vf_cntl,
                              R100Vertex *vertex)
{
    unsigned int needed = r100_vertex_dwords(format);
    unsigned int i = 0;
    uint32_t packed;

    if (count < needed) {
        return false;
    }

    vertex->x = r100_float(words[i++]);
    vertex->y = r100_float(words[i++]);
    vertex->z = (format & R100_VTX_FMT_Z) ? r100_float(words[i++]) : 0.0f;
    vertex->w = (format & R100_VTX_FMT_W0) ? r100_float(words[i++]) : 1.0f;
    vertex->color = (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    vertex->specular = (R100Color) { 0.0f, 0.0f, 0.0f, 0.0f };
    vertex->q[0] = vertex->q[1] = vertex->q[2] = 1.0f;

    if (format & R100_VTX_FMT_FPCOLOR) {
        vertex->color.r = r100_float(words[i++]);
        vertex->color.g = r100_float(words[i++]);
        vertex->color.b = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_FPALPHA) {
        vertex->color.a = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_PKCOLOR) {
        packed = words[i++];
        if (vf_cntl & R100_VF_COLOR_ORDER_RGBA) {
            vertex->color.r = (packed & 0xff) / 255.0f;
            vertex->color.g = ((packed >> 8) & 0xff) / 255.0f;
            vertex->color.b = ((packed >> 16) & 0xff) / 255.0f;
        } else {
            vertex->color.b = (packed & 0xff) / 255.0f;
            vertex->color.g = ((packed >> 8) & 0xff) / 255.0f;
            vertex->color.r = ((packed >> 16) & 0xff) / 255.0f;
        }
        vertex->color.a = ((packed >> 24) & 0xff) / 255.0f;
    }
    if (format & R100_VTX_FMT_FPSPEC) {
        vertex->specular.r = r100_float(words[i++]);
        vertex->specular.g = r100_float(words[i++]);
        vertex->specular.b = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_FPFOG) {
        vertex->specular.a = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_PKSPEC) {
        packed = words[i++];
        vertex->specular.b = (packed & 0xff) / 255.0f;
        vertex->specular.g = ((packed >> 8) & 0xff) / 255.0f;
        vertex->specular.r = ((packed >> 16) & 0xff) / 255.0f;
        vertex->specular.a = ((packed >> 24) & 0xff) / 255.0f;
    }

    if (format & R100_VTX_FMT_ST0) {
        vertex->s[0] = r100_float(words[i++]);
        vertex->t[0] = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_ST1) {
        vertex->s[1] = r100_float(words[i++]);
        vertex->t[1] = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_Q1) {
        vertex->q[1] = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_ST2) {
        vertex->s[2] = r100_float(words[i++]);
        vertex->t[2] = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_Q2) {
        vertex->q[2] = r100_float(words[i++]);
    }
    if (format & R100_VTX_FMT_ST3) {
        i += 2;
    }
    i += !!(format & R100_VTX_FMT_Q3);
    if (format & R100_VTX_FMT_Q0) {
        vertex->q[0] = r100_float(words[i++]);
    }
    i += extract32(format, 15, 3);
    if (format & R100_VTX_FMT_N0) {
        i += 3;
    }
    if (format & R100_VTX_FMT_XY1) {
        i += 2;
    }
    i += !!(format & R100_VTX_FMT_Z1);
    i += !!(format & R100_VTX_FMT_W1);
    if (format & R100_VTX_FMT_N1) {
        i++;
    }
    return i == needed;
}

static void r100_transform_vertex(ATIVGAState *s, R100Vertex *v)
{
    uint32_t se_cntl = r100_context_read(&s->r100_3d, R100_SE_CNTL);
    float reciprocal_w = v->w != 0.0f ? 1.0f / v->w : 1.0f;

    if (se_cntl & R100_VPORT_XY_XFORM_ENABLE) {
        v->x = v->x * reciprocal_w *
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_XSCALE)) +
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_XOFFSET));
        v->y = v->y * reciprocal_w *
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_YSCALE)) +
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_YOFFSET));
    }
    if (se_cntl & R100_VPORT_Z_XFORM_ENABLE) {
        v->z = v->z * reciprocal_w *
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_ZSCALE)) +
               r100_float(r100_context_read(&s->r100_3d,
                                             R100_SE_VPORT_ZOFFSET));
    }
}

static bool r100_vertex_valid(const R100Vertex *v)
{
    unsigned int i;

    if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) ||
        !isfinite(v->w) || fabsf(v->x) > R100_MAX_SAFE_SCREEN_COORD ||
        fabsf(v->y) > R100_MAX_SAFE_SCREEN_COORD ||
        !isfinite(v->color.r) || !isfinite(v->color.g) ||
        !isfinite(v->color.b) || !isfinite(v->color.a) ||
        !isfinite(v->specular.r) || !isfinite(v->specular.g) ||
        !isfinite(v->specular.b) || !isfinite(v->specular.a)) {
        return false;
    }
    for (i = 0; i < 3; i++) {
        if (!isfinite(v->s[i]) || !isfinite(v->t[i]) ||
            !isfinite(v->q[i])) {
            return false;
        }
    }
    return true;
}

static R100Color r100_decode_color(uint32_t raw, unsigned int format)
{
    R100Color c = { 0.0f, 0.0f, 0.0f, 1.0f };

    switch (format) {
    case 3: /* ARGB1555 */
        c.a = (raw & 0x8000) ? 1.0f : 0.0f;
        c.r = ((raw >> 10) & 0x1f) / 31.0f;
        c.g = ((raw >> 5) & 0x1f) / 31.0f;
        c.b = (raw & 0x1f) / 31.0f;
        break;
    case 4: /* RGB565 */
        c.r = ((raw >> 11) & 0x1f) / 31.0f;
        c.g = ((raw >> 5) & 0x3f) / 63.0f;
        c.b = (raw & 0x1f) / 31.0f;
        break;
    case 6: /* ARGB8888 */
        c.a = ((raw >> 24) & 0xff) / 255.0f;
        c.r = ((raw >> 16) & 0xff) / 255.0f;
        c.g = ((raw >> 8) & 0xff) / 255.0f;
        c.b = (raw & 0xff) / 255.0f;
        break;
    case 7: /* RGB332 */
        c.r = ((raw >> 5) & 7) / 7.0f;
        c.g = ((raw >> 2) & 7) / 7.0f;
        c.b = (raw & 3) / 3.0f;
        break;
    case 8: /* Y8 */
        c.r = c.g = c.b = (raw & 0xff) / 255.0f;
        break;
    case 15: /* ARGB4444 */
        c.a = ((raw >> 12) & 0xf) / 15.0f;
        c.r = ((raw >> 8) & 0xf) / 15.0f;
        c.g = ((raw >> 4) & 0xf) / 15.0f;
        c.b = (raw & 0xf) / 15.0f;
        break;
    default:
        break;
    }
    return c;
}

static uint32_t r100_encode_color(R100Color c, unsigned int format)
{
    uint32_t a = r100_float_to_u8(c.a);
    uint32_t r = r100_float_to_u8(c.r);
    uint32_t g = r100_float_to_u8(c.g);
    uint32_t b = r100_float_to_u8(c.b);

    switch (format) {
    case 3:
        return (a >= 128 ? 0x8000 : 0) | ((r >> 3) << 10) |
               ((g >> 3) << 5) | (b >> 3);
    case 4:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case 6:
        return a << 24 | r << 16 | g << 8 | b;
    case 7:
        return (r & 0xe0) | ((g >> 3) & 0x1c) | (b >> 6);
    case 8:
        return (r * 77 + g * 150 + b * 29) >> 8;
    case 15:
        return (a >> 4) << 12 | (r >> 4) << 8 |
               (g >> 4) << 4 | (b >> 4);
    default:
        return 0;
    }
}

static unsigned int r100_color_cpp(unsigned int format)
{
    switch (format) {
    case 7:
    case 8:
        return 1;
    case 3:
    case 4:
    case 15:
        return 2;
    case 6:
        return 4;
    default:
        return 0;
    }
}

static bool r100_read_pixel(ATIVGAState *s, uint64_t address,
                            unsigned int cpp, uint32_t *raw)
{
    uint8_t data[4] = { 0 };

    if (!cpp || cpp > sizeof(data) ||
        !ati_r100_gpu_read(s, address, data, cpp)) {
        return false;
    }
    *raw = ldn_le_p(data, cpp);
    return true;
}

static bool r100_write_pixel(ATIVGAState *s, uint64_t address,
                             unsigned int cpp, uint32_t raw,
                             R100DirtyBatch *batch)
{
    uint8_t data[4];

    if (!cpp || cpp > sizeof(data)) {
        return false;
    }
    stn_le_p(data, cpp, raw);
    return r100_gpu_write(s, address, data, cpp, true, batch);
}

static int r100_wrap_texel(int value, int size)
{
    value %= size;
    return value < 0 ? value + size : value;
}

static R100TextureAxis r100_texture_axis(float coord, int size,
                                         unsigned int mode, bool d3d_border,
                                         bool linear)
{
    R100TextureAxis axis = { 0 };
    float sample;
    bool use_border = false;
    int count = linear ? 2 : 1;
    int i;

    /* MIRRORED_REPEAT mirrors the continuous coordinate, then clamps it. */
    if (mode == R100_TXFILTER_CLAMP_MIRROR) {
        float period = size * 2.0f;

        coord = fmodf(coord, period);
        if (coord < 0.0f) {
            coord += period;
        }
        if (coord > size) {
            coord = period - coord;
        }
        mode = R100_TXFILTER_CLAMP_LAST;
    }

    /* Mirror-once modes reflect around zero before applying their clamp. */
    if (mode == R100_TXFILTER_MIRROR_CLAMP_LAST ||
        mode == R100_TXFILTER_MIRROR_CLAMP_BORDER ||
        mode == R100_TXFILTER_MIRROR_CLAMP_GL) {
        coord = fabsf(coord);
    }

    switch (mode) {
    case R100_TXFILTER_CLAMP_LAST:
    case R100_TXFILTER_MIRROR_CLAMP_LAST:
        coord = MAX(0.5f, MIN(coord, size - 0.5f));
        break;
    case R100_TXFILTER_CLAMP_BORDER:
    case R100_TXFILTER_CLAMP_GL:
        if (mode == R100_TXFILTER_CLAMP_GL && d3d_border) {
            coord = MAX(-0.5f, MIN(coord, size + 0.5f));
        } else {
            coord = MAX(0.0f, MIN(coord, (float)size));
        }
        use_border = mode == R100_TXFILTER_CLAMP_BORDER || linear ||
                     d3d_border;
        break;
    case R100_TXFILTER_MIRROR_CLAMP_BORDER:
    case R100_TXFILTER_MIRROR_CLAMP_GL:
        if (mode == R100_TXFILTER_MIRROR_CLAMP_GL && d3d_border) {
            coord = MAX(0.5f, MIN(coord, size + 0.5f));
        } else {
            coord = MAX(0.5f, MIN(coord, (float)size));
        }
        use_border = mode == R100_TXFILTER_MIRROR_CLAMP_BORDER || linear ||
                     d3d_border;
        break;
    default:
        break;
    }
    sample = linear ? coord - 0.5f : coord;
    if (!linear && d3d_border && coord == size &&
        (mode == R100_TXFILTER_CLAMP_GL ||
         mode == R100_TXFILTER_MIRROR_CLAMP_GL)) {
        /* GL nearest sampling maps the exact normalized upper edge to N - 1. */
        sample = size - 1;
    }

    axis.texel[0] = floorf(sample);
    axis.texel[1] = axis.texel[0] + 1;
    axis.fraction = sample - axis.texel[0];
    for (i = 0; i < count; i++) {
        int texel = axis.texel[i];

        if (mode == R100_TXFILTER_CLAMP_WRAP) {
            axis.texel[i] = r100_wrap_texel(texel, size);
        } else if (use_border && (texel < 0 || texel >= size)) {
            axis.border[i] = true;
            axis.texel[i] = MIN(MAX(texel, 0), size - 1);
        } else {
            axis.texel[i] = MIN(MAX(texel, 0), size - 1);
        }
    }
    return axis;
}

static unsigned int r100_texture_block_bytes(unsigned int format)
{
    switch (format) {
    case R100_TXFORMAT_DXT1:
        return 8;
    case R100_TXFORMAT_DXT23:
    case R100_TXFORMAT_DXT45:
        return 16;
    default:
        return 0;
    }
}

static bool r100_texture_info(ATIVGAState *s, unsigned int unit,
                              R100TextureState *texture)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t tex_size;
    uint32_t tex_pitch;

    if (unit >= 3) {
        return false;
    }
    texture->txformat = r100_context_read(
        r, R100_PP_TXFORMAT_0 + unit * 0x18);
    texture->txoffset = r100_context_read(
        r, R100_PP_TXOFFSET_0 + unit * 0x18);
    tex_size = r100_context_read(r, R100_PP_TEX_SIZE_0 + unit * 8);
    tex_pitch = r100_context_read(r, R100_PP_TEX_PITCH_0 + unit * 8);

    texture->format = texture->txformat & R100_TXFORMAT_FORMAT_MASK;
    texture->block_bytes = r100_texture_block_bytes(texture->format);
    switch (texture->format) {
    case 0: /* I8 */
    case 2: /* RGB332 */
    case 8: /* Y8 */
        texture->cpp = 1;
        break;
    case 1: /* AI88 */
    case 3: /* ARGB1555 */
    case 4: /* RGB565 */
    case 5: /* ARGB4444 */
    case R100_TXFORMAT_VYUY422:
    case R100_TXFORMAT_YVYU422:
        texture->cpp = 2;
        break;
    case 6: /* ARGB8888 */
    case 7: /* RGBA8888 */
        texture->cpp = 4;
        break;
    case R100_TXFORMAT_DXT1:
    case R100_TXFORMAT_DXT23:
    case R100_TXFORMAT_DXT45:
        texture->cpp = 0;
        break;
    default:
        return false;
    }

    if (texture->block_bytes &&
        (texture->txformat & R100_TXFORMAT_NON_POWER2 ||
         texture->txoffset & (R100_TXO_MACRO_TILE |
                              R100_TXO_MICRO_TILE_X2 |
                              R100_TXO_MICRO_TILE_OPT))) {
        /* TODO: Implement tiled and non-power-of-two compressed textures. */
        return false;
    }
    if (texture->txformat & R100_TXFORMAT_NON_POWER2) {
        texture->width = (tex_size & 0x7ffU) + 1;
        texture->height = ((tex_size >> 16) & 0x7ffU) + 1;
        texture->pitch = (tex_pitch & 0x3fe0U) + 32;
    } else {
        texture->width = 1U << extract32(
            texture->txformat, R100_TXFORMAT_WIDTH_SHIFT, 4);
        texture->height = 1U << extract32(
            texture->txformat, R100_TXFORMAT_HEIGHT_SHIFT, 4);
        if (texture->block_bytes) {
            texture->pitch = MAX(DIV_ROUND_UP(texture->width, 4) *
                                 texture->block_bytes, 32U);
        } else {
            texture->pitch = QEMU_ALIGN_UP(
                texture->width * texture->cpp, 32);
        }
    }
    texture->offset = texture->txoffset & ~0x1fU;
    return texture->width <= 2048 && texture->height <= 2048 &&
           (texture->block_bytes ?
            texture->pitch >= DIV_ROUND_UP(texture->width, 4) *
                              texture->block_bytes :
            texture->pitch >= texture->width * texture->cpp);
}

static R100Color r100_decode_texture_color(uint32_t raw,
                                            unsigned int format,
                                            bool alpha_in_map)
{
    R100Color c;

    switch (format) {
    case 0: /* I8 */
        c.r = c.g = c.b = (raw & 0xff) / 255.0f;
        c.a = alpha_in_map ? c.r : 1.0f;
        return c;
    case 1: /* AI88 */
        c.r = c.g = c.b = (raw & 0xff) / 255.0f;
        c.a = ((raw >> 8) & 0xff) / 255.0f;
        break;
    case 2: /* RGB332 */
        return r100_decode_color(raw, 7);
    case 3: /* ARGB1555 */
        c = r100_decode_color(raw, 3);
        break;
    case 4: /* RGB565 */
        return r100_decode_color(raw, 4);
    case 5: /* ARGB4444 */
        c = r100_decode_color(raw, 15);
        break;
    case 6: /* ARGB8888 */
        c = r100_decode_color(raw, 6);
        break;
    case 7: /* RGBA8888 */
        c.r = ((raw >> 24) & 0xff) / 255.0f;
        c.g = ((raw >> 16) & 0xff) / 255.0f;
        c.b = ((raw >> 8) & 0xff) / 255.0f;
        c.a = (raw & 0xff) / 255.0f;
        break;
    case 8: /* Y8 */
        return r100_decode_color(raw, 8);
    default:
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (!alpha_in_map) {
        c.a = 1.0f;
    }
    return c;
}

static R100Color r100_decode_yuv422(const uint8_t pair[4],
                                    unsigned int format, bool odd,
                                    bool yuv_to_rgb)
{
    unsigned int y;
    unsigned int cb;
    unsigned int cr;
    float r;
    float g;
    float b;

    if (format == R100_TXFORMAT_VYUY422) {
        /* Xorg programs this format for FOURCC_YUY2: Y0 Cb Y1 Cr. */
        y = pair[odd ? 2 : 0];
        cb = pair[1];
        cr = pair[3];
    } else {
        /* Xorg programs this format for FOURCC_UYVY: Cb Y0 Cr Y1. */
        y = pair[odd ? 3 : 1];
        cb = pair[0];
        cr = pair[2];
    }
    if (!yuv_to_rgb) {
        return (R100Color) {
            y / 255.0f, cb / 255.0f, cr / 255.0f, 1.0f,
        };
    }

    r = 1.164f * ((int)y - 16) + 1.596f * ((int)cr - 128);
    g = 1.164f * ((int)y - 16) - 0.813f * ((int)cr - 128) -
        0.391f * ((int)cb - 128);
    b = 1.164f * ((int)y - 16) + 2.018f * ((int)cb - 128);
    return (R100Color) {
        r100_clamp_float(r / 255.0f, 0.0f, 1.0f),
        r100_clamp_float(g / 255.0f, 0.0f, 1.0f),
        r100_clamp_float(b / 255.0f, 0.0f, 1.0f),
        1.0f,
    };
}

static bool r100_texture_pixel_offset(uint32_t txoffset, unsigned int pitch,
                                      unsigned int cpp, int x, int y,
                                      uint64_t *pixel_offset)
{
    unsigned int tile_width;
    unsigned int tile_height;
    unsigned int micro = extract32(txoffset, 3, 2);
    bool macro = txoffset & R100_TXO_MACRO_TILE;
    uint64_t offset;

    if (macro) {
        /* TODO: Add macro-tile addressing for 8- and 16-bit textures. */
        if (cpp != 4 || pitch < 128) {
            return false;
        }
        if (micro) {
            offset = (((uint64_t)y >> 4) * (pitch >> 7) + (x >> 5)) << 11;
            offset += ((((y >> 3) ^ (x >> 5)) & 1U) << 10);
            offset += ((((y >> 4) ^ (x >> 4)) & 1U) << 9);
            offset += ((((y >> 2) ^ (x >> 4)) & 1U) << 8);
            offset += ((((y >> 3) ^ (x >> 3)) & 1U) << 7);
            offset += ((y >> 1) & 1U) << 6;
            offset += ((x >> 2) & 1U) << 5;
            offset += (y & 1U) << 4;
            offset += (x & 3U) << 2;
        } else {
            offset = (((uint64_t)y >> 3) * (pitch >> 8) + (x >> 6)) << 11;
            offset += ((((y >> 2) ^ (x >> 6)) & 1U) << 10);
            offset += ((((y >> 3) ^ (x >> 5)) & 1U) << 9);
            offset += ((((y >> 1) ^ (x >> 5)) & 1U) << 8);
            offset += ((((y >> 2) ^ (x >> 4)) & 1U) << 7);
            offset += (y & 1U) << 6;
            offset += (x & 15U) << 2;
        }
        *pixel_offset = offset;
        return true;
    }
    if (!micro) {
        *pixel_offset = (uint64_t)y * pitch + (uint64_t)x * cpp;
        return true;
    }
    switch (cpp) {
    case 1:
        tile_width = 8;
        tile_height = 4;
        break;
    case 2:
        tile_width = micro == 1 ? 8 : 4;
        tile_height = micro == 1 ? 2 : 4;
        break;
    case 4:
        tile_width = 4;
        tile_height = 2;
        break;
    default:
        return false;
    }
    *pixel_offset = (uint64_t)(y / tile_height) * pitch * tile_height +
                    (uint64_t)(x / tile_width) * 32 +
                    (uint64_t)(y % tile_height) * tile_width * cpp +
                    (uint64_t)(x % tile_width) * cpp;
    return true;
}

static R100Color r100_dxt_interpolate(R100Color a, R100Color b,
                                      unsigned int a_weight,
                                      unsigned int b_weight,
                                      unsigned int divisor)
{
    return (R100Color) {
        (a.r * a_weight + b.r * b_weight) / divisor,
        (a.g * a_weight + b.g * b_weight) / divisor,
        (a.b * a_weight + b.b * b_weight) / divisor,
        (a.a * a_weight + b.a * b_weight) / divisor,
    };
}

static const uint8_t *r100_texture_cache_read(ATIVGAState *s,
                                              R100TextureBlockCache *cache,
                                              uint64_t address,
                                              unsigned int bytes)
{
    unsigned int entry;

    for (entry = 0; entry < cache->count; entry++) {
        if (cache->address[entry] == address) {
            return cache->data[entry];
        }
    }
    entry = cache->count < ARRAY_SIZE(cache->data) ? cache->count : 0;
    if (!ati_r100_gpu_read(s, address, cache->data[entry], bytes)) {
        return NULL;
    }
    cache->address[entry] = address;
    if (cache->count < ARRAY_SIZE(cache->data)) {
        cache->count++;
    }
    return cache->data[entry];
}

static const uint8_t *r100_texture_block(ATIVGAState *s,
                                         R100TextureBlockCache *cache,
                                         uint64_t texture_offset,
                                         unsigned int pitch,
                                         unsigned int block_bytes,
                                         int x, int y)
{
    uint64_t address = texture_offset + (uint64_t)(y >> 2) * pitch +
                       (uint64_t)(x >> 2) * block_bytes;

    return r100_texture_cache_read(s, cache, address, block_bytes);
}

static R100Color r100_decode_dxt_texel(const uint8_t *block,
                                       unsigned int format,
                                       bool alpha_in_map, int x, int y)
{
    const uint8_t *color_block = block +
        (format == R100_TXFORMAT_DXT1 ? 0 : 8);
    uint16_t color0 = lduw_le_p(color_block);
    uint16_t color1 = lduw_le_p(color_block + 2);
    uint32_t selectors = ldl_le_p(color_block + 4);
    unsigned int index = (y & 3) * 4 + (x & 3);
    unsigned int selector = extract32(selectors, index * 2, 2);
    R100Color palette[4];
    R100Color color;

    palette[0] = r100_decode_color(color0, 4);
    palette[1] = r100_decode_color(color1, 4);
    if (format != R100_TXFORMAT_DXT1 || color0 > color1) {
        palette[2] = r100_dxt_interpolate(palette[0], palette[1], 2, 1, 3);
        palette[3] = r100_dxt_interpolate(palette[0], palette[1], 1, 2, 3);
    } else {
        palette[2] = r100_dxt_interpolate(palette[0], palette[1], 1, 1, 2);
        palette[3] = (R100Color) { 0.0f, 0.0f, 0.0f,
                                   alpha_in_map ? 0.0f : 1.0f };
    }
    color = palette[selector];

    if (format == R100_TXFORMAT_DXT23) {
        uint64_t alpha = ldq_le_p(block);

        color.a = extract64(alpha, index * 4, 4) / 15.0f;
    } else if (format == R100_TXFORMAT_DXT45) {
        float alpha[8];
        uint64_t selectors_alpha = ldq_le_p(block) >> 16;
        unsigned int alpha_selector = extract64(selectors_alpha,
                                                index * 3, 3);
        unsigned int i;

        alpha[0] = block[0] / 255.0f;
        alpha[1] = block[1] / 255.0f;
        if (block[0] > block[1]) {
            for (i = 2; i < 8; i++) {
                alpha[i] = ((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7;
            }
        } else {
            for (i = 2; i < 6; i++) {
                alpha[i] = ((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5;
            }
            alpha[6] = 0.0f;
            alpha[7] = 1.0f;
        }
        color.a = alpha[alpha_selector];
    }
    if (!alpha_in_map) {
        color.a = 1.0f;
    }
    return color;
}

static R100Color r100_texture_texel(ATIVGAState *s, uint32_t txformat,
                                    uint32_t txoffset, unsigned int format,
                                    unsigned int width, unsigned int height,
                                    unsigned int pitch, unsigned int cpp,
                                    uint64_t offset, R100Color border_color,
                                    bool yuv_to_rgb, bool border, int x, int y,
                                    R100TextureBlockCache *cache)
{
    unsigned int block_bytes = r100_texture_block_bytes(format);
    uint64_t pixel_offset;
    uint32_t raw;

    if (border) {
        return border_color;
    }
    if (block_bytes) {
        const uint8_t *block = r100_texture_block(
            s, cache, offset, pitch, block_bytes, x, y);

        return block ? r100_decode_dxt_texel(
                           block, format,
                           txformat & R100_TXFORMAT_ALPHA_IN_MAP, x, y) :
                       (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (format == R100_TXFORMAT_VYUY422 ||
        format == R100_TXFORMAT_YVYU422) {
        const uint8_t *pair;

        if (!r100_texture_pixel_offset(txoffset, pitch, cpp, x & ~1, y,
                                       &pixel_offset)) {
            return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        pair = r100_texture_cache_read(s, cache, offset + pixel_offset, 4);
        if (!pair) {
            return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        return r100_decode_yuv422(pair, format, x & 1, yuv_to_rgb);
    }
    if (!r100_texture_pixel_offset(txoffset, pitch, cpp, x, y,
                                   &pixel_offset) ||
        !r100_read_pixel(s, offset + pixel_offset, cpp, &raw)) {
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    return r100_decode_texture_color(raw, format,
                                     txformat & R100_TXFORMAT_ALPHA_IN_MAP);
}

static R100Color r100_color_lerp(R100Color a, R100Color b, float f)
{
    return (R100Color) {
        a.r + (b.r - a.r) * f,
        a.g + (b.g - a.g) * f,
        a.b + (b.b - a.b) * f,
        a.a + (b.a - a.a) * f,
    };
}

static float r100_texture_lod_bias(uint32_t filter)
{
    int bias = sextract32(filter, R100_TXFILTER_LOD_BIAS_SHIFT, 8);

    /* R100 uses separate fixed-point slopes on either side of zero. */
    return bias < 0 ? bias / 128.0f : bias / 32.0f;
}

static float r100_texture_lambda(const R100TextureState *texture,
                                 const R100TextureGradients *gradients,
                                 unsigned int route, float s, float t, float q,
                                 bool nonparametric)
{
    float s_scale = nonparametric ? 1.0f : texture->width;
    float t_scale = nonparametric ? 1.0f : texture->height;
    float dudx, dudy, dvdx, dvdy;
    float rho;

    if (!gradients) {
        return -INFINITY;
    }
    if (texture->txformat & R100_TXFORMAT_PERSPECTIVE_ENABLE) {
        float qx = q + gradients->dqdx[route];
        float qy = q + gradients->dqdy[route];

        if (qx == 0.0f || qy == 0.0f) {
            return INFINITY;
        }
        dudx = s_scale * ((s + gradients->dsdx[route]) / qx - s / q);
        dvdx = t_scale * ((t + gradients->dtdx[route]) / qx - t / q);
        dudy = s_scale * ((s + gradients->dsdy[route]) / qy - s / q);
        dvdy = t_scale * ((t + gradients->dtdy[route]) / qy - t / q);
    } else {
        dudx = s_scale * gradients->dsdx[route];
        dvdx = t_scale * gradients->dtdx[route];
        dudy = s_scale * gradients->dsdy[route];
        dvdy = t_scale * gradients->dtdy[route];
    }
    rho = MAX(hypotf(dudx, dvdx), hypotf(dudy, dvdy));
    if (isnan(rho) || rho <= 0.0f) {
        return -INFINITY;
    }
    return log2f(rho);
}

static void r100_texture_level_info(const R100TextureState *texture,
                                    unsigned int level,
                                    unsigned int *width,
                                    unsigned int *height,
                                    unsigned int *pitch, uint64_t *offset)
{
    *width = texture->width;
    *height = texture->height;
    *pitch = texture->pitch;
    *offset = texture->offset;

    while (level--) {
        *offset += (uint64_t)*pitch *
                   (texture->block_bytes ? DIV_ROUND_UP(*height, 4) :
                                           *height);
        *width = MAX(*width >> 1, 1U);
        *height = MAX(*height >> 1, 1U);
        if (texture->block_bytes) {
            *pitch = MAX(DIV_ROUND_UP(*width, 4) * texture->block_bytes,
                         32U);
        } else {
            *pitch = QEMU_ALIGN_UP(*width * texture->cpp, 32);
        }
    }
}

static R100Color r100_sample_texture_level(ATIVGAState *s,
                                           const R100TextureState *texture,
                                           float s_coord, float t_coord,
                                           bool nonparametric,
                                           unsigned int level, bool linear)
{
    unsigned int width, height, pitch;
    uint64_t offset;
    R100TextureAxis xaxis;
    R100TextureAxis yaxis;
    R100TextureBlockCache cache = { 0 };
    float u, v;

    r100_texture_level_info(texture, level, &width, &height, &pitch, &offset);
    if (nonparametric) {
        u = s_coord * width / texture->width;
        v = t_coord * height / texture->height;
    } else {
        u = s_coord * width;
        v = t_coord * height;
    }
    if (!isfinite(u) || !isfinite(v) ||
        fabsf(u) >= R100_MAX_SAFE_TEXEL_COORD ||
        fabsf(v) >= R100_MAX_SAFE_TEXEL_COORD) {
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    xaxis = r100_texture_axis(u, width, texture->smode,
                              texture->d3d_border, linear);
    yaxis = r100_texture_axis(v, height, texture->tmode,
                              texture->d3d_border, linear);
    if (!linear) {
        return r100_texture_texel(
            s, texture->txformat, texture->txoffset, texture->format,
            width, height, pitch, texture->cpp, offset,
            texture->border_color, texture->yuv_to_rgb,
            xaxis.border[0] || yaxis.border[0],
            xaxis.texel[0], yaxis.texel[0], &cache);
    }
    {
        R100Color c00 = r100_texture_texel(
            s, texture->txformat, texture->txoffset, texture->format,
            width, height, pitch, texture->cpp, offset,
            texture->border_color, texture->yuv_to_rgb,
            xaxis.border[0] || yaxis.border[0],
            xaxis.texel[0], yaxis.texel[0], &cache);
        R100Color c10 = r100_texture_texel(
            s, texture->txformat, texture->txoffset, texture->format,
            width, height, pitch, texture->cpp, offset,
            texture->border_color, texture->yuv_to_rgb,
            xaxis.border[1] || yaxis.border[0],
            xaxis.texel[1], yaxis.texel[0], &cache);
        R100Color c01 = r100_texture_texel(
            s, texture->txformat, texture->txoffset, texture->format,
            width, height, pitch, texture->cpp, offset,
            texture->border_color, texture->yuv_to_rgb,
            xaxis.border[0] || yaxis.border[1],
            xaxis.texel[0], yaxis.texel[1], &cache);
        R100Color c11 = r100_texture_texel(
            s, texture->txformat, texture->txoffset, texture->format,
            width, height, pitch, texture->cpp, offset,
            texture->border_color, texture->yuv_to_rgb,
            xaxis.border[1] || yaxis.border[1],
            xaxis.texel[1], yaxis.texel[1], &cache);

        return r100_color_lerp(
            r100_color_lerp(c00, c10, xaxis.fraction),
            r100_color_lerp(c01, c11, xaxis.fraction), yaxis.fraction);
    }
}

static R100Color r100_sample_texture(ATIVGAState *s, unsigned int unit,
                                     const float tex_s[3],
                                     const float tex_t[3],
                                     const float tex_q[3],
                                     const R100TextureGradients *gradients)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t filter = r100_context_read(r, R100_PP_TXFILTER_0 + unit * 0x18);
    uint32_t coord_fmt = r100_context_read(r, R100_SE_COORD_FMT);
    R100TextureState texture;
    unsigned int route;
    unsigned int min_filter = filter & R100_TXFILTER_MIN_MASK;
    unsigned int max_level;
    unsigned int level;
    bool nonparametric;
    bool linear;
    float s_coord, t_coord, q;
    float lambda;

    if (!r100_texture_info(s, unit, &texture)) {
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    texture.border_color = r100_decode_color(r100_context_read(
        r, R100_PP_BORDER_COLOR_0 + unit * 4), 6);
    texture.smode = extract32(filter, R100_TXFILTER_CLAMP_S_SHIFT, 3);
    texture.tmode = extract32(filter, R100_TXFILTER_CLAMP_T_SHIFT, 3);
    texture.d3d_border = filter & R100_TXFILTER_BORDER_MODE_D3D;
    texture.yuv_to_rgb = unit == 0 &&
                         (filter & R100_TXFILTER_YUV_TO_RGB);
    if (!(texture.txformat & R100_TXFORMAT_ALPHA_IN_MAP)) {
        texture.border_color.a = 1.0f;
    }
    route = extract32(texture.txformat, R100_TXFORMAT_ST_ROUTE_SHIFT, 2);
    if (route >= 3) {
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    s_coord = tex_s[route];
    t_coord = tex_t[route];
    q = tex_q[route];
    nonparametric = coord_fmt & R100_VTX_ST_NONPARAMETRIC(route);
    if ((texture.txformat & R100_TXFORMAT_PERSPECTIVE_ENABLE) &&
        (!isfinite(q) || fabsf(q) < 0.00000000000000000001f)) {
        return (R100Color) { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    lambda = r100_texture_lambda(&texture, gradients, route, s_coord,
                                 t_coord, q, nonparametric);
    lambda += r100_texture_lod_bias(filter);
    if (texture.txformat & R100_TXFORMAT_PERSPECTIVE_ENABLE) {
        s_coord /= q;
        t_coord /= q;
    }

    max_level = extract32(filter, R100_TXFILTER_MAX_MIP_LEVEL_SHIFT, 4);
    /* NPOT textures have one level. TODO: Compute tiled POT mip layouts. */
    if (texture.txformat & R100_TXFORMAT_NON_POWER2 ||
        texture.txoffset & (R100_TXO_MACRO_TILE | R100_TXO_MICRO_TILE_X2 |
                            R100_TXO_MICRO_TILE_OPT)) {
        max_level = 0;
    } else {
        unsigned int last_level = MAX(
            extract32(texture.txformat, R100_TXFORMAT_WIDTH_SHIFT, 4),
            extract32(texture.txformat, R100_TXFORMAT_HEIGHT_SHIFT, 4));

        max_level = MIN(max_level, last_level);
    }

    /* Two nearest-texel mip modes move the MAG/MIN boundary to lambda 0.5. */
    if (lambda <= ((filter & R100_TXFILTER_MAG_LINEAR) &&
                   (min_filter == R100_TXFILTER_MIN_NEAREST_MIP_NEAREST ||
                    min_filter == R100_TXFILTER_MIN_LINEAR_MIP_NEAREST) ?
                   0.5f : 0.0f)) {
        return r100_sample_texture_level(
            s, &texture, s_coord, t_coord, nonparametric, 0,
            filter & R100_TXFILTER_MAG_LINEAR);
    }

    switch (min_filter) {
    case R100_TXFILTER_MIN_NEAREST:
    case R100_TXFILTER_MIN_LINEAR:
        return r100_sample_texture_level(
            s, &texture, s_coord, t_coord, nonparametric, 0,
            min_filter == R100_TXFILTER_MIN_LINEAR);
    case R100_TXFILTER_MIN_NEAREST_MIP_NEAREST:
    case R100_TXFILTER_MIN_NEAREST_MIP_LINEAR:
        if (!isfinite(lambda) || lambda >= max_level + 0.5f) {
            level = max_level;
        } else {
            level = lambda <= 0.5f ? 0 : (unsigned int)(lambda + 0.5f);
        }
        linear = min_filter == R100_TXFILTER_MIN_NEAREST_MIP_LINEAR;
        return r100_sample_texture_level(s, &texture, s_coord, t_coord,
                                         nonparametric, level, linear);
    case R100_TXFILTER_MIN_LINEAR_MIP_NEAREST:
    case R100_TXFILTER_MIN_LINEAR_MIP_LINEAR:
    {
        float lod;
        float fraction;
        R100Color lower;
        R100Color upper;

        if (!isfinite(lambda) || lambda >= max_level) {
            lod = max_level;
        } else {
            lod = MAX(lambda, 0.0f);
        }
        level = floorf(lod);
        fraction = lod - level;
        linear = min_filter == R100_TXFILTER_MIN_LINEAR_MIP_LINEAR;
        lower = r100_sample_texture_level(s, &texture, s_coord, t_coord,
                                          nonparametric, level, linear);
        if (level == max_level) {
            return lower;
        }
        upper = r100_sample_texture_level(s, &texture, s_coord, t_coord,
                                          nonparametric, level + 1, linear);
        return r100_color_lerp(lower, upper, fraction);
    }
    default:
        return r100_sample_texture_level(
            s, &texture, s_coord, t_coord, nonparametric, 0,
            filter & R100_TXFILTER_MAG_LINEAR);
    }
}

static bool r100_compare(unsigned int operation, uint32_t source,
                         uint32_t destination)
{
    switch (operation & 7) {
    case 0:
        return false;
    case 1:
        return source < destination;
    case 2:
        return source <= destination;
    case 3:
        return source == destination;
    case 4:
        return source >= destination;
    case 5:
        return source > destination;
    case 6:
        return source != destination;
    case 7:
        return true;
    default:
        return false;
    }
}

static float r100_blend_factor(unsigned int factor, R100Color src,
                               R100Color dst, unsigned int component)
{
    float sc[] = { src.r, src.g, src.b, src.a };
    float dc[] = { dst.r, dst.g, dst.b, dst.a };

    switch (factor) {
    case 32:
        return 0.0f;
    case 33:
        return 1.0f;
    case 34:
        return sc[component];
    case 35:
        return 1.0f - sc[component];
    case 36:
        return dc[component];
    case 37:
        return 1.0f - dc[component];
    case 38:
        return src.a;
    case 39:
        return 1.0f - src.a;
    case 40:
        return dst.a;
    case 41:
        return 1.0f - dst.a;
    case 42:
        return component == 3 ? 1.0f : MIN(src.a, 1.0f - dst.a);
    default:
        return 1.0f;
    }
}

static R100Color r100_blend(ATI3DState *r, R100Color src, R100Color dst)
{
    uint32_t cntl = r100_context_read(r, R100_RB3D_BLENDCNTL);
    unsigned int src_factor = extract32(cntl, 16, 6);
    unsigned int dst_factor = extract32(cntl, 24, 6);
    unsigned int equation = extract32(cntl, 12, 2);
    float sc[] = { src.r, src.g, src.b, src.a };
    float dc[] = { dst.r, dst.g, dst.b, dst.a };
    float out[4];
    unsigned int i;

    for (i = 0; i < 4; i++) {
        float a = sc[i] * r100_blend_factor(src_factor, src, dst, i);
        float b = dc[i] * r100_blend_factor(dst_factor, src, dst, i);

        out[i] = equation >= 2 ? a - b : a + b;
    }
    return (R100Color) { out[0], out[1], out[2], out[3] };
}

static uint32_t r100_apply_rop(unsigned int rop, uint32_t src, uint32_t dst)
{
    switch (rop & 15) {
    case 0:
        return 0;
    case 1:
        return ~(src | dst);
    case 2:
        return ~src & dst;
    case 3:
        return ~src;
    case 4:
        return src & ~dst;
    case 5:
        return ~dst;
    case 6:
        return src ^ dst;
    case 7:
        return ~(src & dst);
    case 8:
        return src & dst;
    case 9:
        return ~(src ^ dst);
    case 10:
        return dst;
    case 11:
        return ~src | dst;
    case 12:
        return src;
    case 13:
        return src | ~dst;
    case 14:
        return src | dst;
    case 15:
        return UINT32_MAX;
    default:
        g_assert_not_reached();
    }
}

static R100Color r100_color_splat(float value)
{
    return (R100Color) { value, value, value, value };
}

static R100Color r100_combiner_color_arg(unsigned int arg,
                                         R100Color current,
                                         R100Color diffuse,
                                         R100Color specular,
                                         R100Color tfactor,
                                         const R100Color texture[3])
{
    const R100Color *source;

    switch (arg) {
    case 2:
    case 3:
        source = &current;
        break;
    case 4:
    case 5:
        source = &diffuse;
        break;
    case 6:
    case 7:
        source = &specular;
        break;
    case 8:
    case 9:
        source = &tfactor;
        break;
    case 10:
    case 11:
        source = &texture[0];
        break;
    case 12:
    case 13:
        source = &texture[1];
        break;
    case 14:
    case 15:
        source = &texture[2];
        break;
    default:
        return r100_color_splat(0.0f);
    }
    return (arg & 1) ? r100_color_splat(source->a) : *source;
}

static float r100_combiner_alpha_arg(unsigned int arg, R100Color current,
                                     R100Color diffuse, R100Color specular,
                                     R100Color tfactor,
                                     const R100Color texture[3])
{
    switch (arg) {
    case 1:
        return current.a;
    case 2:
        return diffuse.a;
    case 3:
        return specular.a;
    case 4:
        return tfactor.a;
    case 5:
        return texture[0].a;
    case 6:
        return texture[1].a;
    case 7:
        return texture[2].a;
    default:
        return 0.0f;
    }
}

static float r100_combiner_component(unsigned int operation, float a,
                                     float b, float c)
{
    switch (operation) {
    case R100_COMBINER_OP_ADD:
        return a * b + c;
    case R100_COMBINER_OP_SUBTRACT:
        return a * b - c;
    case R100_COMBINER_OP_ADDSIGNED:
        return a * b + c - 0.5f;
    case R100_COMBINER_OP_BLEND:
        return a * (1.0f - c) + b * c;
    default:
        return 0.0f;
    }
}

static R100Color r100_run_combiner(ATI3DState *r, unsigned int unit,
                                   R100Color current, R100Color diffuse,
                                   R100Color specular,
                                   const R100Color texture[3])
{
    uint32_t cblend = r100_context_read(r, R100_PP_TXCBLEND_0 + unit * 0x18);
    uint32_t ablend = r100_context_read(r, R100_PP_TXABLEND_0 + unit * 0x18);
    R100Color tfactor = r100_decode_color(
        r100_context_read(r, R100_PP_TFACTOR_0 + unit * 0x18), 6);
    R100Color args[3];
    R100Color result;
    float alpha[3];
    float *result_component[] = {
        &result.r, &result.g, &result.b,
    };
    unsigned int operation = extract32(cblend, R100_COMBINER_OP_SHIFT, 3);
    unsigned int alpha_operation = extract32(ablend,
                                              R100_COMBINER_OP_SHIFT, 3);
    unsigned int scale = 1U << extract32(cblend,
                                         R100_COMBINER_SCALE_SHIFT, 2);
    unsigned int alpha_scale = 1U << extract32(ablend,
                                               R100_COMBINER_SCALE_SHIFT, 2);
    unsigned int i;

    args[0] = r100_combiner_color_arg(extract32(
        cblend, R100_COMBINER_ARG_A_SHIFT, 5), current, diffuse, specular,
        tfactor, texture);
    args[1] = r100_combiner_color_arg(extract32(
        cblend, R100_COMBINER_ARG_B_SHIFT, 5), current, diffuse, specular,
        tfactor, texture);
    args[2] = r100_combiner_color_arg(extract32(
        cblend, R100_COMBINER_ARG_C_SHIFT, 5), current, diffuse, specular,
        tfactor, texture);
    for (i = 0; i < 3; i++) {
        if (cblend & BIT(15 + i)) {
            args[i].r = 1.0f - args[i].r;
            args[i].g = 1.0f - args[i].g;
            args[i].b = 1.0f - args[i].b;
        }
    }
    if (operation == R100_COMBINER_OP_DOT3) {
        float dot = 4.0f * ((args[0].r - 0.5f) * (args[1].r - 0.5f) +
                            (args[0].g - 0.5f) * (args[1].g - 0.5f) +
                            (args[0].b - 0.5f) * (args[1].b - 0.5f));

        result.r = result.g = result.b = dot * scale;
    } else {
        const float a[] = { args[0].r, args[0].g, args[0].b };
        const float b[] = { args[1].r, args[1].g, args[1].b };
        const float c[] = { args[2].r, args[2].g, args[2].b };

        for (i = 0; i < 3; i++) {
            *result_component[i] =
                r100_combiner_component(operation, a[i], b[i], c[i]) * scale;
        }
    }
    if (cblend & R100_COMBINER_CLAMP) {
        result.r = r100_clamp_float(result.r, 0.0f, 1.0f);
        result.g = r100_clamp_float(result.g, 0.0f, 1.0f);
        result.b = r100_clamp_float(result.b, 0.0f, 1.0f);
    }

    alpha[0] = r100_combiner_alpha_arg(extract32(
        ablend, R100_ALPHA_ARG_A_SHIFT, 4), current, diffuse, specular,
        tfactor, texture);
    alpha[1] = r100_combiner_alpha_arg(extract32(
        ablend, R100_ALPHA_ARG_B_SHIFT, 4), current, diffuse, specular,
        tfactor, texture);
    alpha[2] = r100_combiner_alpha_arg(extract32(
        ablend, R100_ALPHA_ARG_C_SHIFT, 4), current, diffuse, specular,
        tfactor, texture);
    for (i = 0; i < 3; i++) {
        if (ablend & BIT(15 + i)) {
            alpha[i] = 1.0f - alpha[i];
        }
    }
    result.a = r100_combiner_component(alpha_operation, alpha[0], alpha[1],
                                       alpha[2]) * alpha_scale;
    if (ablend & R100_COMBINER_CLAMP) {
        result.a = r100_clamp_float(result.a, 0.0f, 1.0f);
    }
    return result;
}

static R100Color r100_texture_pipeline(ATIVGAState *s, R100Color diffuse,
                                       R100Color specular,
                                       const float tex_s[3],
                                       const float tex_t[3],
                                       const float tex_q[3],
                                       const R100TextureGradients *gradients)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t pp_cntl = r100_context_read(r, R100_PP_CNTL);
    R100Color texture[3] = {
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
    };
    R100Color current = diffuse;
    unsigned int i;

    for (i = 0; i < 3; i++) {
        if (pp_cntl & BIT(4 + i)) {
            texture[i] = r100_sample_texture(s, i, tex_s, tex_t, tex_q,
                                             gradients);
        }
    }
    for (i = 0; i < 3; i++) {
        if (pp_cntl & BIT(12 + i)) {
            current = r100_run_combiner(r, i, current, diffuse, specular,
                                        texture);
        }
    }
    if (pp_cntl & R100_PP_SPECULAR_ENABLE) {
        current.r += specular.r;
        current.g += specular.g;
        current.b += specular.b;
    }
    return current;
}

static float r100_table_fog_factor(const ATI3DState *r, float coordinate)
{
    float scaled = r100_clamp_float(coordinate, 0.0f, 1.0f) *
                   ATI_3D_FOG_TABLE_ENTRIES;
    unsigned int index = scaled;
    float fraction;

    if (index >= ATI_3D_FOG_TABLE_ENTRIES - 1) {
        return r->fog_table[ATI_3D_FOG_TABLE_ENTRIES - 1] / 255.0f;
    }
    fraction = scaled - index;
    return ((1.0f - fraction) * r->fog_table[index] +
            fraction * r->fog_table[index + 1]) / 255.0f;
}

static R100Color r100_fog(ATI3DState *r, R100Color color, float depth,
                          float diffuse_alpha, float specular_alpha)
{
    uint32_t fog_reg = r100_context_read(r, R100_PP_FOG_COLOR);
    R100Color fog_color;
    float factor;
    float inverse_factor;

    if (fog_reg & R100_FOG_TABLE) {
        switch (fog_reg & R100_FOG_SOURCE_MASK) {
        case R100_FOG_USE_DEPTH:
            factor = r100_table_fog_factor(r, depth);
            break;
        case R100_FOG_USE_DIFFUSE_ALPHA:
            factor = r100_table_fog_factor(r, diffuse_alpha);
            break;
        case R100_FOG_USE_SPEC_ALPHA:
            factor = r100_table_fog_factor(r, specular_alpha);
            break;
        default:
            return color;
        }
    } else {
        factor = specular_alpha;
    }
    fog_color = r100_decode_color(fog_reg & R100_FOG_COLOR_MASK, 6);
    factor = r100_clamp_float(factor, 0.0f, 1.0f);
    inverse_factor = 1.0f - factor;
    color.r = factor * r100_clamp_float(color.r, 0.0f, 1.0f) +
              inverse_factor * fog_color.r;
    color.g = factor * r100_clamp_float(color.g, 0.0f, 1.0f) +
              inverse_factor * fog_color.g;
    color.b = factor * r100_clamp_float(color.b, 0.0f, 1.0f) +
              inverse_factor * fog_color.b;
    return color;
}

static uint8_t r100_stencil_operation(unsigned int operation, uint8_t old,
                                      uint8_t reference)
{
    switch (operation & 7) {
    case 0:
        return old;
    case 1:
        return 0;
    case 2:
        return reference;
    case 3:
        return old == UINT8_MAX ? UINT8_MAX : old + 1;
    case 4:
        return old == 0 ? 0 : old - 1;
    case 5:
        return ~old;
    case 6:
        return old + 1;
    case 7:
        return old - 1;
    default:
        return old;
    }
}

static bool r100_color_pixel_offset(uint32_t pitch_reg, unsigned int pitch,
                                    unsigned int cpp, int x, int y,
                                    uint64_t *pixel_offset)
{
    bool macro = pitch_reg & R100_COLOR_TILE_ENABLE;
    bool micro = pitch_reg & R100_COLOR_MICROTILE_ENABLE;
    uint64_t row_bytes = (uint64_t)pitch * cpp;
    uint64_t offset;

    if (!macro && !micro) {
        *pixel_offset = (uint64_t)y * row_bytes + (uint64_t)x * cpp;
        return true;
    }
    if (!macro) {
        return r100_texture_pixel_offset(R100_TXO_MICRO_TILE_X2, row_bytes,
                                         cpp, x, y, pixel_offset);
    }

    /* TODO: Add macro-tile addressing for 8- and 16-bit color targets. */
    if (cpp != 4 || row_bytes < 128) {
        return false;
    }
    if (micro) {
        offset = (((uint64_t)y >> 4) * (row_bytes >> 7) + (x >> 5)) << 11;
        offset += ((((y >> 3) ^ (x >> 5)) & 1U) << 10);
        offset += ((((y >> 4) ^ (x >> 4)) & 1U) << 9);
        offset += ((((y >> 2) ^ (x >> 4)) & 1U) << 8);
        offset += ((((y >> 3) ^ (x >> 3)) & 1U) << 7);
        offset += ((y >> 1) & 1U) << 6;
        offset += ((x >> 2) & 1U) << 5;
        offset += (y & 1U) << 4;
        offset += (x & 3U) << 2;
    } else {
        offset = (((uint64_t)y >> 3) * (row_bytes >> 8) + (x >> 6)) << 11;
        offset += ((((y >> 2) ^ (x >> 6)) & 1U) << 10);
        offset += ((((y >> 3) ^ (x >> 5)) & 1U) << 9);
        offset += ((((y >> 1) ^ (x >> 5)) & 1U) << 8);
        offset += ((((y >> 2) ^ (x >> 4)) & 1U) << 7);
        offset += (y & 1U) << 6;
        offset += (x & 15U) << 2;
    }
    *pixel_offset = offset;
    return true;
}

static bool r100_fragment(ATIVGAState *s, int x, int y, float z,
                          R100Color color, R100Color specular,
                          const float tex_s[3], const float tex_t[3],
                          const float tex_q[3],
                          const R100TextureGradients *gradients,
                          R100DirtyBatch *batch)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t pp_cntl = r100_context_read(r, R100_PP_CNTL);
    uint32_t rb_cntl = r100_context_read(r, R100_RB3D_CNTL);
    uint32_t color_format = extract32(rb_cntl,
                                      R100_RB3D_COLOR_FORMAT_SHIFT, 5);
    uint32_t color_pitch_reg = r100_context_read(r, R100_RB3D_COLORPITCH);
    uint32_t color_pitch = color_pitch_reg & 0x1ff8U;
    uint32_t color_offset = r100_context_read(r, R100_RB3D_COLOROFFSET) &
                            ~0xfU;
    uint32_t top_left = r->re_top_left;
    uint32_t width_height = r100_context_read(r, R100_RE_WIDTH_HEIGHT);
    int clip_left = top_left & 0xffff;
    int clip_top = top_left >> 16;
    int clip_right = clip_left + (width_height & 0x7ff);
    int clip_bottom = clip_top + ((width_height >> 16) & 0x7ff);
    unsigned int color_cpp = r100_color_cpp(color_format);
    uint64_t color_address;
    uint64_t color_pixel_offset;
    uint32_t dst_raw = 0;
    float diffuse_alpha = color.a;
    R100Color dst;
    uint32_t result;

    if (x < clip_left || x > clip_right || y < clip_top ||
        y > clip_bottom || !color_cpp || color_pitch == 0 ||
        (unsigned int)x >= color_pitch) {
        return false;
    }

    color = r100_texture_pipeline(s, color, specular, tex_s, tex_t, tex_q,
                                  gradients);
    if (pp_cntl & R100_PP_FOG_ENABLE) {
        color = r100_fog(r, color, z, diffuse_alpha, specular.a);
    }

    if (pp_cntl & R100_PP_ALPHA_TEST_ENABLE) {
        uint32_t misc = r100_context_read(r, R100_PP_MISC);
        uint32_t alpha = r100_float_to_u8(color.a);

        if (!r100_compare(extract32(misc, 8, 3), alpha, misc & 0xff)) {
            return false;
        }
    }

    if (rb_cntl & (R100_RB3D_Z_ENABLE | R100_RB3D_STENCIL_ENABLE)) {
        uint32_t zcntl = r100_context_read(r, R100_RB3D_ZSTENCILCNTL);
        uint32_t stencil_refmask = r100_context_read(
            r, R100_RB3D_STENCILREFMASK);
        uint32_t depth_pitch = r100_context_read(r, R100_RB3D_DEPTHPITCH) &
                               0x1ff8U;
        uint32_t depth_offset = r100_context_read(r,
                                                  R100_RB3D_DEPTHOFFSET);
        unsigned int depth_format = zcntl & 0xf;
        unsigned int depth_cpp = (depth_format == 0 || depth_format == 7) ?
                                 2 : 4;
        uint32_t depth_mask = depth_cpp == 2 ? 0xffffU :
                              (depth_format == 2 || depth_format == 3 ||
                               depth_format == 9) ? 0xffffffU : UINT32_MAX;
        bool stencil_enabled = rb_cntl & R100_RB3D_STENCIL_ENABLE;
        bool depth_enabled = rb_cntl & R100_RB3D_Z_ENABLE;
        uint64_t depth_address;
        uint32_t old_depth;
        uint32_t new_depth;
        uint32_t new_depth_stencil;
        uint8_t old_stencil;
        uint8_t new_stencil;
        uint8_t stencil_reference = extract32(stencil_refmask,
                                               R100_STENCIL_REF_SHIFT, 8);
        uint8_t stencil_mask = extract32(stencil_refmask,
                                         R100_STENCIL_MASK_SHIFT, 8);
        uint8_t stencil_writemask = extract32(
            stencil_refmask, R100_STENCIL_WRITEMASK_SHIFT, 8);
        bool stencil_pass = true;
        bool depth_pass = true;
        unsigned int stencil_op;

        if (!depth_pitch || (unsigned int)x >= depth_pitch) {
            return false;
        }
        depth_address = (depth_offset & ~0xfU) +
                        ((uint64_t)y * depth_pitch + x) * depth_cpp;
        if (!r100_read_pixel(s, depth_address, depth_cpp, &old_depth)) {
            return false;
        }
        if (stencil_enabled && (depth_cpp != 4 || depth_mask != 0xffffffU)) {
            return false;
        }
        old_stencil = old_depth >> 24;
        new_stencil = old_stencil;
        if (stencil_enabled) {
            stencil_pass = r100_compare(extract32(
                zcntl, R100_STENCIL_TEST_SHIFT, 3),
                stencil_reference & stencil_mask,
                old_stencil & stencil_mask);
        }
        if (depth_cpp == 2) {
            new_depth = r100_clamp_float(z, 0.0f, 1.0f) * 65535.0f;
        } else if (depth_mask == 0xffffffU) {
            new_depth = (uint32_t)(r100_clamp_float(z, 0.0f, 1.0f) *
                                   16777215.0f);
        } else {
            new_depth = (uint32_t)(r100_clamp_float(z, 0.0f, 1.0f) *
                                   4294967295.0);
        }
        new_depth &= depth_mask;
        if (stencil_pass && depth_enabled) {
            depth_pass = r100_compare(extract32(zcntl, 4, 3), new_depth,
                                      old_depth & depth_mask);
        }
        if (stencil_enabled) {
            stencil_op = !stencil_pass ? extract32(
                zcntl, R100_STENCIL_FAIL_SHIFT, 3) :
                !depth_pass ? extract32(
                    zcntl, R100_STENCIL_ZFAIL_SHIFT, 3) :
                    extract32(zcntl, R100_STENCIL_ZPASS_SHIFT, 3);
            new_stencil = r100_stencil_operation(stencil_op, old_stencil,
                                                 stencil_reference);
            new_stencil = (new_stencil & stencil_writemask) |
                          (old_stencil & ~stencil_writemask);
        }
        new_depth_stencil = old_depth;
        if (stencil_pass && depth_enabled && depth_pass &&
            (zcntl & R100_RB3D_Z_WRITE_ENABLE)) {
            new_depth_stencil = (new_depth_stencil & ~depth_mask) | new_depth;
        }
        if (stencil_enabled) {
            new_depth_stencil = (new_depth_stencil & 0x00ffffffU) |
                                (uint32_t)new_stencil << 24;
        }
        if (new_depth_stencil != old_depth &&
            !r100_write_pixel(s, depth_address, depth_cpp,
                              new_depth_stencil, batch)) {
            return false;
        }
        if (!stencil_pass || !depth_pass) {
            return false;
        }
    }

    if (!r100_color_pixel_offset(color_pitch_reg, color_pitch, color_cpp,
                                 x, y, &color_pixel_offset)) {
        return false;
    }
    color_address = color_offset + color_pixel_offset;
    if (!r100_read_pixel(s, color_address, color_cpp, &dst_raw)) {
        return false;
    }
    dst = r100_decode_color(dst_raw, color_format);
    if (rb_cntl & R100_RB3D_ALPHA_BLEND_ENABLE) {
        color = r100_blend(r, color, dst);
    }
    result = r100_encode_color(color, color_format);
    if (rb_cntl & R100_RB3D_ROP_ENABLE) {
        result = r100_apply_rop(extract32(
            r100_context_read(r, R100_RB3D_ROPCNTL), 8, 4),
            result, dst_raw);
    }
    if (rb_cntl & R100_RB3D_PLANE_MASK_ENABLE) {
        uint32_t mask = r100_context_read(r, R100_RB3D_PLANEMASK);

        result = (result & mask) | (dst_raw & ~mask);
    }
    return r100_write_pixel(s, color_address, color_cpp, result, batch);
}

typedef struct R100Edge {
    double x;
    double y;
    double constant;
    bool inclusive;
} R100Edge;

static R100Edge r100_triangle_edge(const R100Vertex *a,
                                   const R100Vertex *b,
                                   bool reverse)
{
    double ax = a->x;
    double ay = a->y;
    double bx = b->x;
    double by = b->y;
    double direction = reverse ? -1.0 : 1.0;
    double dx = (bx - ax) * direction;
    double dy = (by - ay) * direction;

    return (R100Edge) {
        .x = (by - ay) * direction,
        .y = (ax - bx) * direction,
        .constant = (ay * bx - ax * by) * direction,
        /* Framebuffer Y grows downwards: these are the top and left edges. */
        .inclusive = dy > 0.0 || (dy == 0.0 && dx < 0.0),
    };
}

static double r100_edge_value(const R100Edge *edge, double x, double y)
{
    return edge->x * x + edge->y * y + edge->constant;
}

static bool r100_edge_contains(const R100Edge *edge, double value)
{
    return value > 0.0 || (value == 0.0 && edge->inclusive);
}

static bool r100_triangle_visible(ATI3DState *r, double area)
{
    uint32_t se_cntl = r100_context_read(r, R100_SE_CNTL);
    bool ccw_front = !!(se_cntl & BIT(0));
    bool front = (area < 0.0f) == ccw_front;
    unsigned int mode = front ? extract32(se_cntl, 3, 2) :
                                extract32(se_cntl, 1, 2);

    return mode == 3;
}

static bool r100_consume_draw_budget(ATI3DState *r, uint64_t pixels)
{
    if (pixels > r->draw_budget_remaining) {
        r->draw_budget_remaining = 0;
        r->rejected_commands++;
        return false;
    }
    r->draw_budget_remaining -= pixels;
    return true;
}

static bool r100_consume_command_work(ATI3DState *r, uint64_t work)
{
    if (work > r->command_work_remaining) {
        r->command_work_remaining = 0;
        r->command_budget_exhausted = true;
        return false;
    }
    r->command_work_remaining -= work;
    return true;
}

static bool r100_consume_blit_work(ATI3DState *r, uint64_t work)
{
    if (work > r->blit_work_remaining) {
        r->blit_work_remaining = 0;
        r->command_budget_exhausted = true;
        return false;
    }
    r->blit_work_remaining -= work;
    return true;
}

bool ati_3d_consume_command_work(ATIVGAState *s, uint64_t work)
{
    ATI3DState *r = &s->r100_3d;

    return !r->processing_depth || r100_consume_command_work(r, work);
}

bool ati_3d_consume_2d_work(ATIVGAState *s, uint64_t work)
{
    ATI3DState *r = &s->r100_3d;

    return !r->processing_depth || r100_consume_blit_work(r, work);
}

static void r100_draw_triangle(ATIVGAState *s, const R100Vertex *v0,
                               const R100Vertex *v1, const R100Vertex *v2,
                               R100DirtyBatch *batch)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t top_left = r->re_top_left;
    uint32_t width_height = r100_context_read(r, R100_RE_WIDTH_HEIGHT);
    int clip_left = top_left & 0xffff;
    int clip_top = top_left >> 16;
    int clip_right = clip_left + (width_height & 0x7ff);
    int clip_bottom = clip_top + ((width_height >> 16) & 0x7ff);
    R100Edge area_edge = r100_triangle_edge(v0, v1, false);
    double area = r100_edge_value(&area_edge, v2->x, v2->y);
    bool reverse = area < 0.0;
    R100Edge edge0;
    R100Edge edge1;
    R100Edge edge2;
    double absolute_area;
    double row_value0;
    double row_value1;
    double row_value2;
    R100TextureGradients gradients;
    int min_x, min_y, max_x, max_y;
    uint64_t pixels;
    unsigned int unit;
    int x, y;

    if (!isfinite(area) || fabs(area) < 0.0000000001 ||
        !r100_triangle_visible(r, area)) {
        return;
    }
    edge0 = r100_triangle_edge(v1, v2, reverse);
    edge1 = r100_triangle_edge(v2, v0, reverse);
    edge2 = r100_triangle_edge(v0, v1, reverse);
    absolute_area = fabs(area);
    for (unit = 0; unit < 3; unit++) {
        gradients.dsdx[unit] = (v0->s[unit] * edge0.x +
                                v1->s[unit] * edge1.x +
                                v2->s[unit] * edge2.x) / absolute_area;
        gradients.dsdy[unit] = (v0->s[unit] * edge0.y +
                                v1->s[unit] * edge1.y +
                                v2->s[unit] * edge2.y) / absolute_area;
        gradients.dtdx[unit] = (v0->t[unit] * edge0.x +
                                v1->t[unit] * edge1.x +
                                v2->t[unit] * edge2.x) / absolute_area;
        gradients.dtdy[unit] = (v0->t[unit] * edge0.y +
                                v1->t[unit] * edge1.y +
                                v2->t[unit] * edge2.y) / absolute_area;
        gradients.dqdx[unit] = (v0->q[unit] * edge0.x +
                                v1->q[unit] * edge1.x +
                                v2->q[unit] * edge2.x) / absolute_area;
        gradients.dqdy[unit] = (v0->q[unit] * edge0.y +
                                v1->q[unit] * edge1.y +
                                v2->q[unit] * edge2.y) / absolute_area;
    }
    min_x = MAX((int)floorf(MIN(v0->x, MIN(v1->x, v2->x))), clip_left);
    min_y = MAX((int)floorf(MIN(v0->y, MIN(v1->y, v2->y))), clip_top);
    max_x = MIN((int)ceilf(MAX(v0->x, MAX(v1->x, v2->x))), clip_right);
    max_y = MIN((int)ceilf(MAX(v0->y, MAX(v1->y, v2->y))), clip_bottom);
    if (min_x > max_x || min_y > max_y) {
        return;
    }
    pixels = (uint64_t)(max_x - min_x + 1) * (max_y - min_y + 1);
    if (!r100_consume_draw_budget(r, pixels)) {
        return;
    }

    row_value0 = r100_edge_value(&edge0, min_x + 0.5, min_y + 0.5);
    row_value1 = r100_edge_value(&edge1, min_x + 0.5, min_y + 0.5);
    row_value2 = r100_edge_value(&edge2, min_x + 0.5, min_y + 0.5);
    for (y = min_y; y <= max_y; y++) {
        double value0 = row_value0;
        double value1 = row_value1;
        double value2 = row_value2;

        for (x = min_x; x <= max_x; x++) {
            double pixel_value0 = value0;
            double pixel_value1 = value1;
            double pixel_value2 = value2;
            float w0;
            float w1;
            float w2;
            R100Color c;
            R100Color specular;
            float tex_s[3];
            float tex_t[3];
            float tex_q[3];

            value0 += edge0.x;
            value1 += edge1.x;
            value2 += edge2.x;
            if (!r100_edge_contains(&edge0, pixel_value0) ||
                !r100_edge_contains(&edge1, pixel_value1) ||
                !r100_edge_contains(&edge2, pixel_value2)) {
                continue;
            }
            w0 = pixel_value0 / absolute_area;
            w1 = pixel_value1 / absolute_area;
            w2 = pixel_value2 / absolute_area;
            c.r = v0->color.r * w0 + v1->color.r * w1 + v2->color.r * w2;
            c.g = v0->color.g * w0 + v1->color.g * w1 + v2->color.g * w2;
            c.b = v0->color.b * w0 + v1->color.b * w1 + v2->color.b * w2;
            c.a = v0->color.a * w0 + v1->color.a * w1 + v2->color.a * w2;
            specular.r = v0->specular.r * w0 + v1->specular.r * w1 +
                         v2->specular.r * w2;
            specular.g = v0->specular.g * w0 + v1->specular.g * w1 +
                         v2->specular.g * w2;
            specular.b = v0->specular.b * w0 + v1->specular.b * w1 +
                         v2->specular.b * w2;
            specular.a = v0->specular.a * w0 + v1->specular.a * w1 +
                         v2->specular.a * w2;
            for (unit = 0; unit < 3; unit++) {
                tex_s[unit] = v0->s[unit] * w0 + v1->s[unit] * w1 +
                              v2->s[unit] * w2;
                tex_t[unit] = v0->t[unit] * w0 + v1->t[unit] * w1 +
                              v2->t[unit] * w2;
                tex_q[unit] = v0->q[unit] * w0 + v1->q[unit] * w1 +
                              v2->q[unit] * w2;
            }
            r100_fragment(s, x, y,
                          v0->z * w0 + v1->z * w1 + v2->z * w2, c,
                          specular, tex_s, tex_t, tex_q, &gradients, batch);
        }
        row_value0 += edge0.y;
        row_value1 += edge1.y;
        row_value2 += edge2.y;
    }
    r->submitted_primitives++;
}

static void r100_draw_line(ATIVGAState *s, const R100Vertex *a,
                           const R100Vertex *b, R100DirtyBatch *batch)
{
    /* TODO: Honor SE_LINE_WIDTH and RE_LINE_PATTERN. */
    int steps = ceilf(MAX(fabsf(b->x - a->x), fabsf(b->y - a->y)));
    int i;

    if (steps <= 0 || steps > 8192 ||
        !r100_consume_draw_budget(&s->r100_3d, steps + 1)) {
        return;
    }
    for (i = 0; i <= steps; i++) {
        float f = (float)i / steps;
        R100Color c = {
            a->color.r + (b->color.r - a->color.r) * f,
            a->color.g + (b->color.g - a->color.g) * f,
            a->color.b + (b->color.b - a->color.b) * f,
            a->color.a + (b->color.a - a->color.a) * f,
        };
        R100Color specular = {
            a->specular.r + (b->specular.r - a->specular.r) * f,
            a->specular.g + (b->specular.g - a->specular.g) * f,
            a->specular.b + (b->specular.b - a->specular.b) * f,
            a->specular.a + (b->specular.a - a->specular.a) * f,
        };
        float tex_s[3];
        float tex_t[3];
        float tex_q[3];
        unsigned int unit;

        int x = floorf(a->x + (b->x - a->x) * f + 0.5f);
        int y = floorf(a->y + (b->y - a->y) * f + 0.5f);

        for (unit = 0; unit < 3; unit++) {
            tex_s[unit] = a->s[unit] + (b->s[unit] - a->s[unit]) * f;
            tex_t[unit] = a->t[unit] + (b->t[unit] - a->t[unit]) * f;
            tex_q[unit] = a->q[unit] + (b->q[unit] - a->q[unit]) * f;
        }
        r100_fragment(s, x, y, a->z + (b->z - a->z) * f, c,
                      specular, tex_s, tex_t, tex_q, NULL, batch);
    }
    s->r100_3d.submitted_primitives++;
}

static R100Color r100_rectangle_fourth_color(R100Color c0, R100Color c1,
                                             R100Color c2)
{
    return (R100Color) {
        c0.r + c2.r - c1.r,
        c0.g + c2.g - c1.g,
        c0.b + c2.b - c1.b,
        c0.a + c2.a - c1.a,
    };
}

static R100Vertex r100_rectangle_fourth_vertex(const R100Vertex *v0,
                                                const R100Vertex *v1,
                                                const R100Vertex *v2)
{
    R100Vertex result;
    unsigned int i;

    result.x = v0->x + v2->x - v1->x;
    result.y = v0->y + v2->y - v1->y;
    result.z = v0->z + v2->z - v1->z;
    result.w = v0->w + v2->w - v1->w;
    result.color = r100_rectangle_fourth_color(v0->color, v1->color,
                                               v2->color);
    result.specular = r100_rectangle_fourth_color(
        v0->specular, v1->specular, v2->specular);
    for (i = 0; i < 3; i++) {
        result.s[i] = v0->s[i] + v2->s[i] - v1->s[i];
        result.t[i] = v0->t[i] + v2->t[i] - v1->t[i];
        result.q[i] = v0->q[i] + v2->q[i] - v1->q[i];
    }
    return result;
}

enum {
    R100_VARY_DIFFUSE = BIT(0),
    R100_VARY_ALPHA = BIT(1),
    R100_VARY_SPECULAR = BIT(2),
    R100_VARY_FOG = BIT(3),
};

static uint32_t r100_vertex_variation(const R100Vertex *a,
                                      const R100Vertex *b)
{
    uint32_t variation = 0;

    if (a->color.r != b->color.r || a->color.g != b->color.g ||
        a->color.b != b->color.b) {
        variation |= R100_VARY_DIFFUSE;
    }
    if (a->color.a != b->color.a) {
        variation |= R100_VARY_ALPHA;
    }
    if (a->specular.r != b->specular.r ||
        a->specular.g != b->specular.g ||
        a->specular.b != b->specular.b) {
        variation |= R100_VARY_SPECULAR;
    }
    if (a->specular.a != b->specular.a) {
        variation |= R100_VARY_FOG;
    }
    return variation;
}

static uint32_t r100_primitive_variation(const R100Vertex *vertices,
                                         unsigned int count,
                                         unsigned int primitive)
{
    uint32_t variation = 0;
    unsigned int i;

    switch (primitive) {
    case R100_VF_PRIM_POINT_LIST:
        break;
    case R100_VF_PRIM_LINE_LIST:
        for (i = 0; i + 1 < count; i += 2) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
        }
        break;
    case R100_VF_PRIM_LINE_STRIP:
        for (i = 0; i + 1 < count; i++) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_LIST:
    case R100_VF_PRIM_RECTANGLE_LIST:
        for (i = 0; i + 2 < count; i += 3) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 2]);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_FAN:
    case R100_VF_PRIM_POLYGON:
        for (i = 1; i + 1 < count; i++) {
            variation |= r100_vertex_variation(&vertices[0],
                                                &vertices[i]);
            variation |= r100_vertex_variation(&vertices[0],
                                                &vertices[i + 1]);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < count; i++) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 2]);
        }
        break;
    case R100_VF_PRIM_QUAD_LIST:
        for (i = 0; i + 3 < count; i += 4) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 2]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 3]);
        }
        break;
    case R100_VF_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < count; i += 2) {
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 1]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 2]);
            variation |= r100_vertex_variation(&vertices[i],
                                                &vertices[i + 3]);
        }
        break;
    default:
        break;
    }
    return variation;
}

static bool r100_shade_mode_supported(uint32_t variation,
                                      uint32_t variation_bit,
                                      uint32_t se_cntl,
                                      unsigned int shift)
{
    unsigned int mode = extract32(se_cntl, shift, 2);

    return !(variation & variation_bit) || mode == R100_SE_SHADE_FLAT ||
           mode == R100_SE_SHADE_GOURAUD;
}

static void r100_apply_flat_shading(ATI3DState *r, R100Vertex *vertices,
                                    unsigned int count)
{
    uint32_t se_cntl = r100_context_read(r, R100_SE_CNTL);
    unsigned int source = extract32(se_cntl,
                                    R100_SE_FLAT_SHADE_VTX_SHIFT, 2);
    R100Color color;
    R100Color specular;
    unsigned int i;

    if (!count) {
        return;
    }
    if (source >= count || source == 3) {
        source = count - 1;
    }
    color = vertices[source].color;
    specular = vertices[source].specular;
    for (i = 0; i < count; i++) {
        if (extract32(se_cntl, R100_SE_DIFFUSE_SHADE_SHIFT, 2) ==
            R100_SE_SHADE_FLAT) {
            vertices[i].color.r = color.r;
            vertices[i].color.g = color.g;
            vertices[i].color.b = color.b;
        }
        if (extract32(se_cntl, R100_SE_ALPHA_SHADE_SHIFT, 2) ==
            R100_SE_SHADE_FLAT) {
            vertices[i].color.a = color.a;
        }
        if (extract32(se_cntl, R100_SE_SPECULAR_SHADE_SHIFT, 2) ==
            R100_SE_SHADE_FLAT) {
            vertices[i].specular.r = specular.r;
            vertices[i].specular.g = specular.g;
            vertices[i].specular.b = specular.b;
        }
        if (extract32(se_cntl, R100_SE_FOG_SHADE_SHIFT, 2) ==
            R100_SE_SHADE_FLAT) {
            vertices[i].specular.a = specular.a;
        }
    }
}

static void r100_draw_shaded_line(ATIVGAState *s, const R100Vertex *v0,
                                  const R100Vertex *v1,
                                  R100DirtyBatch *batch)
{
    R100Vertex vertices[] = { *v0, *v1 };

    r100_apply_flat_shading(&s->r100_3d, vertices, ARRAY_SIZE(vertices));
    r100_draw_line(s, &vertices[0], &vertices[1], batch);
}

static void r100_draw_shaded_triangle(ATIVGAState *s,
                                      const R100Vertex *v0,
                                      const R100Vertex *v1,
                                      const R100Vertex *v2,
                                      R100DirtyBatch *batch)
{
    R100Vertex vertices[] = { *v0, *v1, *v2 };

    r100_apply_flat_shading(&s->r100_3d, vertices, ARRAY_SIZE(vertices));
    r100_draw_triangle(s, &vertices[0], &vertices[1], &vertices[2], batch);
}

static void r100_draw_shaded_quad(ATIVGAState *s, const R100Vertex *v0,
                                  const R100Vertex *v1,
                                  const R100Vertex *v2,
                                  const R100Vertex *v3,
                                  bool strip, R100DirtyBatch *batch)
{
    R100Vertex vertices[] = { *v0, *v1, *v2, *v3 };

    r100_apply_flat_shading(&s->r100_3d, vertices, ARRAY_SIZE(vertices));
    r100_draw_triangle(s, &vertices[0], &vertices[1], &vertices[2], batch);
    if (strip) {
        r100_draw_triangle(s, &vertices[1], &vertices[3], &vertices[2],
                           batch);
    } else {
        r100_draw_triangle(s, &vertices[0], &vertices[2], &vertices[3],
                           batch);
    }
}

static bool r100_draw_state_supported(ATIVGAState *s,
                                      const R100Vertex *vertices,
                                      unsigned int count,
                                      unsigned int primitive)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t rb_cntl = r100_context_read(r, R100_RB3D_CNTL);
    uint32_t variation;
    uint32_t se_cntl;

    if (rb_cntl & (R100_RB3D_Z_ENABLE | R100_RB3D_STENCIL_ENABLE)) {
        uint32_t zcntl = r100_context_read(r, R100_RB3D_ZSTENCILCNTL);
        unsigned int format = zcntl & R100_DEPTH_FORMAT_MASK;

        if (format != R100_DEPTH_FORMAT_16BIT_INT_Z &&
            format != R100_DEPTH_FORMAT_24BIT_INT_Z &&
            format != R100_DEPTH_FORMAT_32BIT_INT_Z) {
            return false;
        }
        if ((rb_cntl & R100_RB3D_STENCIL_ENABLE) &&
            format != R100_DEPTH_FORMAT_24BIT_INT_Z) {
            return false;
        }
    }

    variation = r100_primitive_variation(vertices, count, primitive);
    se_cntl = r100_context_read(r, R100_SE_CNTL);
    if (!r100_shade_mode_supported(variation, R100_VARY_DIFFUSE, se_cntl,
                                   R100_SE_DIFFUSE_SHADE_SHIFT) ||
        !r100_shade_mode_supported(variation, R100_VARY_ALPHA, se_cntl,
                                   R100_SE_ALPHA_SHADE_SHIFT) ||
        !r100_shade_mode_supported(variation, R100_VARY_SPECULAR, se_cntl,
                                   R100_SE_SPECULAR_SHADE_SHIFT) ||
        !r100_shade_mode_supported(variation, R100_VARY_FOG, se_cntl,
                                   R100_SE_FOG_SHADE_SHIFT)) {
        return false;
    }
    return true;
}

static void r100_draw_vertices(ATIVGAState *s, R100Vertex *vertices,
                               unsigned int count, uint32_t vf_cntl)
{
    unsigned int primitive = vf_cntl & R100_VF_PRIM_TYPE_MASK;
    R100DirtyBatch batch = { 0 };
    unsigned int i;

    if (!r100_draw_state_supported(s, vertices, count, primitive)) {
        s->r100_3d.rejected_commands++;
        goto out;
    }
    /* TODO: Clip primitives in homogeneous space before viewport transform. */
    for (i = 0; i < count; i++) {
        r100_transform_vertex(s, &vertices[i]);
        if (!r100_vertex_valid(&vertices[i])) {
            s->r100_3d.rejected_commands++;
            goto out;
        }
    }
    switch (primitive) {
    case R100_VF_PRIM_POINT_LIST:
        for (i = 0; i < count; i++) {
            int x = floorf(vertices[i].x + 0.5f);
            int y = floorf(vertices[i].y + 0.5f);

            if (!r100_consume_draw_budget(&s->r100_3d, 1)) {
                break;
            }
            r100_fragment(s, x, y, vertices[i].z, vertices[i].color,
                          vertices[i].specular, vertices[i].s,
                          vertices[i].t, vertices[i].q, NULL, &batch);
            s->r100_3d.submitted_primitives++;
        }
        break;
    case R100_VF_PRIM_LINE_LIST:
        for (i = 0; i + 1 < count; i += 2) {
            r100_draw_shaded_line(s, &vertices[i], &vertices[i + 1],
                                  &batch);
        }
        break;
    case R100_VF_PRIM_LINE_STRIP:
        for (i = 0; i + 1 < count; i++) {
            r100_draw_shaded_line(s, &vertices[i], &vertices[i + 1],
                                  &batch);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_LIST:
        for (i = 0; i + 2 < count; i += 3) {
            r100_draw_shaded_triangle(s, &vertices[i], &vertices[i + 1],
                                      &vertices[i + 2], &batch);
        }
        break;
    case R100_VF_PRIM_RECTANGLE_LIST:
        for (i = 0; i + 2 < count; i += 3) {
            R100Vertex rectangle[] = {
                vertices[i], vertices[i + 1], vertices[i + 2],
            };
            R100Vertex fourth;

            r100_apply_flat_shading(&s->r100_3d, rectangle,
                                    ARRAY_SIZE(rectangle));
            fourth = r100_rectangle_fourth_vertex(
                &rectangle[0], &rectangle[1], &rectangle[2]);
            r100_draw_triangle(s, &rectangle[0], &rectangle[1],
                               &rectangle[2], &batch);
            r100_draw_triangle(s, &rectangle[0], &rectangle[2], &fourth,
                               &batch);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_FAN:
        for (i = 1; i + 1 < count; i++) {
            r100_draw_shaded_triangle(s, &vertices[0], &vertices[i],
                                      &vertices[i + 1], &batch);
        }
        break;
    case R100_VF_PRIM_POLYGON:
        r100_apply_flat_shading(&s->r100_3d, vertices, count);
        for (i = 1; i + 1 < count; i++) {
            r100_draw_triangle(s, &vertices[0], &vertices[i],
                               &vertices[i + 1], &batch);
        }
        break;
    case R100_VF_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < count; i++) {
            R100Vertex triangle[] = {
                vertices[i], vertices[i + 1], vertices[i + 2],
            };

            r100_apply_flat_shading(&s->r100_3d, triangle,
                                    ARRAY_SIZE(triangle));
            if (i & 1) {
                r100_draw_triangle(s, &triangle[1], &triangle[0],
                                   &triangle[2], &batch);
            } else {
                r100_draw_triangle(s, &triangle[0], &triangle[1],
                                   &triangle[2], &batch);
            }
        }
        break;
    case R100_VF_PRIM_QUAD_LIST:
        for (i = 0; i + 3 < count; i += 4) {
            r100_draw_shaded_quad(s, &vertices[i], &vertices[i + 1],
                                  &vertices[i + 2], &vertices[i + 3], false,
                                  &batch);
        }
        break;
    case R100_VF_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < count; i += 2) {
            r100_draw_shaded_quad(s, &vertices[i], &vertices[i + 1],
                                  &vertices[i + 2], &vertices[i + 3], true,
                                  &batch);
        }
        break;
    default:
        s->r100_3d.rejected_commands++;
        break;
    }
out:
    r100_dirty_batch_flush(s, &batch);
}

static bool r100_draw_immediate(ATIVGAState *s, uint32_t format,
                                uint32_t vf_cntl, const uint32_t *words,
                                unsigned int word_count)
{
    unsigned int vertex_size = r100_vertex_dwords(format);
    unsigned int vertex_count = extract32(vf_cntl,
                                           R100_VF_NUM_VERTICES_SHIFT, 16);
    g_autofree R100Vertex *vertices = NULL;
    unsigned int i;

    if (!ati_has_rv100_3d(s) ||
        (vf_cntl & R100_VF_PRIM_WALK_MASK) != R100_VF_PRIM_WALK_DATA ||
        vertex_count == 0 || vertex_count > R100_MAX_DRAW_VERTICES ||
        vertex_size == 0 || vertex_count > word_count / vertex_size ||
        vertex_count * vertex_size != word_count) {
        s->r100_3d.rejected_commands++;
        return false;
    }

    vertices = g_new0(R100Vertex, vertex_count);
    for (i = 0; i < vertex_count; i++) {
        if (!r100_parse_vertex(words + i * vertex_size, vertex_size,
                               format, vf_cntl, &vertices[i])) {
            s->r100_3d.rejected_commands++;
            return false;
        }
    }
    s->r100_3d.se_vtx_fmt = format;
    s->r100_3d.se_vf_cntl = vf_cntl;
    r100_draw_vertices(s, vertices, vertex_count, vf_cntl);
    return true;
}

static bool r100_fetch_vertex(ATIVGAState *s, uint32_t index,
                              uint32_t format, uint32_t vf_cntl,
                              R100Vertex *vertex)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int vertex_size = r100_vertex_dwords(format);
    uint32_t words[R100_MAX_VERTEX_COMPONENTS];
    unsigned int word = 0;
    unsigned int i, j;

    if (!vertex_size || vertex_size > ARRAY_SIZE(words)) {
        return false;
    }
    for (i = 0; i < r->vertex_array_count; i++) {
        ATI3DVertexArray *array = &r->vertex_array[i];

        if (!array->components ||
            (array->stride && array->stride < array->components) ||
            array->components > vertex_size - word) {
            return false;
        }
        for (j = 0; j < array->components; j++) {
            uint64_t dword = (uint64_t)index * array->stride + j;

            if (dword > (UINT64_MAX - array->address) / 4 ||
                !r100_gpu_read_u32(s, array->address + dword * 4,
                                   &words[word++])) {
                return false;
            }
            words[word - 1] = r100_swap_word(words[word - 1],
                                             r->se_cntl_status &
                                             R100_VC_SWAP_MASK);
        }
    }
    return word == vertex_size &&
           r100_parse_vertex(words, vertex_size, format, vf_cntl, vertex);
}

static bool r100_draw_vbuf(ATIVGAState *s, uint32_t format,
                           uint32_t vf_cntl)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int vertex_size = r100_vertex_dwords(format);
    unsigned int vertex_count = extract32(vf_cntl,
                                           R100_VF_NUM_VERTICES_SHIFT, 16);
    g_autofree R100Vertex *vertices = NULL;
    unsigned int i;

    if (!ati_has_rv100_3d(s) || r->vertex_array_count == 0 ||
        (vf_cntl & R100_VF_PRIM_WALK_MASK) != R100_VF_PRIM_WALK_LIST ||
        vertex_count == 0 || vertex_count > R100_MAX_DRAW_VERTICES ||
        vertex_size == 0 || vertex_size > R100_MAX_VERTEX_COMPONENTS ||
        !r100_consume_command_work(
            r, (uint64_t)vertex_count *
               (vertex_size + R100_VERTEX_WORK_OVERHEAD))) {
        r->rejected_commands++;
        return false;
    }

    vertices = g_new0(R100Vertex, vertex_count);
    for (i = 0; i < vertex_count; i++) {
        if (!r100_fetch_vertex(s, i, format, vf_cntl, &vertices[i])) {
            r->rejected_commands++;
            return false;
        }
    }
    r->se_vtx_fmt = format;
    r->se_vf_cntl = vf_cntl;
    r100_draw_vertices(s, vertices, vertex_count, vf_cntl);
    return true;
}

static bool r100_draw_indexed(ATIVGAState *s, uint32_t format,
                              uint32_t vf_cntl, const uint32_t *indices,
                              unsigned int index_dwords)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int vertex_size = r100_vertex_dwords(format);
    unsigned int vertex_count = extract32(vf_cntl,
                                           R100_VF_NUM_VERTICES_SHIFT, 16);
    bool index32 = vf_cntl & R100_VF_INDEX_SIZE_32;
    unsigned int expected_dwords = index32 ? vertex_count :
                                           DIV_ROUND_UP(vertex_count, 2);
    g_autofree R100Vertex *vertices = NULL;
    unsigned int i;

    if (!ati_has_rv100_3d(s) || !r->vertex_array_count ||
        (vf_cntl & R100_VF_PRIM_WALK_MASK) != R100_VF_PRIM_WALK_IND ||
        !vertex_count || vertex_count > R100_MAX_DRAW_VERTICES ||
        index_dwords != expected_dwords || vertex_size == 0 ||
        vertex_size > R100_MAX_VERTEX_COMPONENTS ||
        !r100_consume_command_work(
            r, (uint64_t)vertex_count *
               (vertex_size + R100_VERTEX_WORK_OVERHEAD))) {
        r->rejected_commands++;
        return false;
    }
    vertices = g_new0(R100Vertex, vertex_count);
    for (i = 0; i < vertex_count; i++) {
        uint32_t index = index32 ? indices[i] :
                         i & 1 ? indices[i / 2] >> 16 :
                                 indices[i / 2] & 0xffff;

        if (!r100_fetch_vertex(s, index, format, vf_cntl, &vertices[i])) {
            r->rejected_commands++;
            return false;
        }
    }
    r->se_vtx_fmt = format;
    r->se_vf_cntl = vf_cntl;
    r100_draw_vertices(s, vertices, vertex_count, vf_cntl);
    return true;
}

static bool r100_stream_read(ATIVGAState *s, R100Stream *stream,
                             uint32_t *value)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t index;

    if (!stream->remaining) {
        return false;
    }
    if (!r100_consume_command_work(r, 1)) {
        return false;
    }
    index = stream->ring ? stream->pos & stream->mask : stream->pos;
    if (!r100_gpu_read_u32(s, stream->base + (uint64_t)index * 4, value)) {
        return false;
    }
    if (stream->ring) {
        *value = r100_swap_word(*value, extract32(
            r->cp_rb_cntl, R100_RB_BUF_SWAP_SHIFT, 2));
    }
    stream->pos++;
    stream->remaining--;
    return true;
}

static void r100_write_scratchback(ATIVGAState *s, unsigned int index)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t address = r->scratch_addr & R100_SCRATCH_ADDR_MASK;
    uint32_t value = r100_swap_word(r->scratch[index],
                                    extract32(r->scratch_umsk,
                                              R100_SCRATCH_SWAP_SHIFT, 2));

    if (r->scratch_umsk & BIT(index)) {
        r100_gpu_write_u32(s, address + index * 4,
                           value, true);
    }
}

static bool r100_process_ib(ATIVGAState *s);

static bool r100_packet0_write(ATIVGAState *s, hwaddr reg, uint32_t value)
{
    ATI3DState *r = &s->r100_3d;

    if (!r100_consume_command_work(r, 1)) {
        return false;
    }
    if (reg == R100_CP_IB_BUFSZ) {
        r->cp_ib_bufsz = value;
        return r100_process_ib(s);
    }
    ati_mmio_write(s, reg, value, 4);
    return !r->command_budget_exhausted;
}

static bool r100_load_vbpntr(ATIVGAState *s, const uint32_t *payload,
                             unsigned int count)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int arrays;
    unsigned int word = 1;
    unsigned int i;

    if (!count) {
        return false;
    }
    arrays = payload[0];
    if (!arrays || arrays > ATI_3D_MAX_VERTEX_ARRAYS) {
        return false;
    }
    memset(r->vertex_array, 0, sizeof(r->vertex_array));
    for (i = 0; i < arrays; i += 2) {
        uint32_t descriptor;

        if (word >= count) {
            return false;
        }
        descriptor = payload[word++];
        if (word >= count) {
            return false;
        }
        r->vertex_array[i].components = descriptor & 0xff;
        r->vertex_array[i].stride = (descriptor >> 8) & 0xff;
        r->vertex_array[i].address = payload[word++];
        if (i + 1 < arrays) {
            if (word >= count) {
                return false;
            }
            r->vertex_array[i + 1].components = (descriptor >> 16) & 0xff;
            r->vertex_array[i + 1].stride = descriptor >> 24;
            r->vertex_array[i + 1].address = payload[word++];
        }
    }
    r->vertex_array_count = arrays;
    return word == count;
}

static bool r100_bitblt_brush_dwords(uint32_t gui, unsigned int *dwords)
{
    unsigned int brush = extract32(gui, 4, 4);
    unsigned int dst_type = extract32(gui, 8, 4);
    unsigned int bytes_per_pixel;

    switch (brush) {
    case 0:
        *dwords = 4;
        return true;
    case 1:
        *dwords = 3;
        return true;
    case 10:
        switch (dst_type) {
        case 2:
            bytes_per_pixel = 1;
            break;
        case 3:
        case 4:
            bytes_per_pixel = 2;
            break;
        case 6:
            bytes_per_pixel = 4;
            break;
        default:
            return false;
        }
        *dwords = 16 * bytes_per_pixel;
        return true;
    case 13:
    case 14:
        *dwords = 1;
        return true;
    case 15:
        *dwords = 0;
        return true;
    default:
        return false;
    }
}

typedef struct R1002DSettings {
    uint32_t gui;
    unsigned int brush;
    unsigned int brush_dwords;
    unsigned int dwords;
} R1002DSettings;

static bool r100_2d_settings_info(const uint32_t *payload,
                                  unsigned int count,
                                  R1002DSettings *settings)
{
    unsigned int dst_type;

    if (!count) {
        return false;
    }
    settings->gui = payload[0];
    dst_type = extract32(settings->gui, 8, 4);
    settings->brush = extract32(settings->gui, 4, 4);
    if ((dst_type != 2 && dst_type != 3 && dst_type != 4 && dst_type != 6) ||
        !r100_bitblt_brush_dwords(settings->gui,
                                  &settings->brush_dwords)) {
        return false;
    }
    settings->dwords = 1 +
        !!(settings->gui & GMC_SRC_PITCH_OFFSET_CNTL) +
        !!(settings->gui & GMC_DST_PITCH_OFFSET_CNTL) +
        !!(settings->gui & GMC_SRC_CLIPPING) +
        2 * !!(settings->gui & GMC_DST_CLIPPING) +
        settings->brush_dwords +
        !!(settings->gui & GMC_LD_BRUSH_Y_X);
    return settings->dwords <= count;
}

static void r100_2d_settings_apply(ATIVGAState *s, const uint32_t *payload,
                                   const R1002DSettings *settings)
{
    uint32_t gui = settings->gui;
    unsigned int word = 1;

    ati_mmio_write(s, DP_GUI_MASTER_CNTL, gui, sizeof(gui));
    if (gui & GMC_SRC_PITCH_OFFSET_CNTL) {
        ati_mmio_write(s, SRC_PITCH_OFFSET, payload[word++],
                       sizeof(payload[0]));
    }
    if (gui & GMC_DST_PITCH_OFFSET_CNTL) {
        ati_mmio_write(s, DST_PITCH_OFFSET, payload[word++],
                       sizeof(payload[0]));
    }
    if (gui & GMC_SRC_CLIPPING) {
        ati_mmio_write(s, SRC_SC_BOTTOM_RIGHT, payload[word++],
                       sizeof(payload[0]));
    }
    if (gui & GMC_DST_CLIPPING) {
        ati_mmio_write(s, SC_TOP_LEFT, payload[word++], sizeof(payload[0]));
        ati_mmio_write(s, SC_BOTTOM_RIGHT, payload[word++],
                       sizeof(payload[0]));
    }
    switch (settings->brush) {
    case 0:
        ati_mmio_write(s, DP_BRUSH_BKGD_CLR, payload[word],
                       sizeof(payload[0]));
        ati_mmio_write(s, DP_BRUSH_FRGD_CLR, payload[word + 1],
                       sizeof(payload[0]));
        ati_mmio_write(s, BRUSH_DATA0, payload[word + 2],
                       sizeof(payload[0]));
        ati_mmio_write(s, BRUSH_DATA0 + sizeof(payload[0]),
                       payload[word + 3], sizeof(payload[0]));
        break;
    case 1:
        ati_mmio_write(s, DP_BRUSH_FRGD_CLR, payload[word],
                       sizeof(payload[0]));
        ati_mmio_write(s, BRUSH_DATA0, payload[word + 1],
                       sizeof(payload[0]));
        ati_mmio_write(s, BRUSH_DATA0 + sizeof(payload[0]),
                       payload[word + 2], sizeof(payload[0]));
        break;
    case 10:
        for (unsigned int i = 0; i < settings->brush_dwords; i++) {
            ati_mmio_write(s, BRUSH_DATA0 + i * sizeof(payload[0]),
                           payload[word + i], sizeof(payload[0]));
        }
        break;
    case 13:
    case 14:
        ati_mmio_write(s, DP_BRUSH_FRGD_CLR, payload[word],
                       sizeof(payload[0]));
        break;
    default:
        break;
    }
    word += settings->brush_dwords;
    if (gui & GMC_LD_BRUSH_Y_X) {
        ati_mmio_write(s, BRUSH_Y_X, payload[word++], sizeof(payload[0]));
    }
    g_assert(word == settings->dwords);
}

static uint32_t r100_2d_engine_xy(const ATIVGAState *s, uint32_t xy,
                                  uint32_t width_height)
{
    int x = sextract32(xy, 16, 14);
    int y = sextract32(xy, 0, 14);
    unsigned int width = extract32(width_height, 16, 14);
    unsigned int height = extract32(width_height, 0, 14);

    /*
     * The multi-rectangle packets name the top-left corner.  The 2D registers,
     * however, name the first pixel visited, as selected by DP_CNTL.
     */
    if (!(s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT) && width) {
        x += width - 1;
    }
    if (!(s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM) && height) {
        y += height - 1;
    }
    return ((uint32_t)x & 0x3fff) << 16 | ((uint32_t)y & 0x3fff);
}

static bool r100_paint_multi(ATIVGAState *s, const uint32_t *payload,
                             unsigned int count)
{
    ATI3DState *r = &s->r100_3d;
    R1002DSettings settings;
    unsigned int word;

    if (!r100_2d_settings_info(payload, count, &settings) ||
        count < settings.dwords + 2 ||
        (count - settings.dwords) % 2) {
        return false;
    }
    r100_2d_settings_apply(s, payload, &settings);

    for (word = settings.dwords; word < count; word += 2) {
        uint32_t width_height = payload[word + 1];

        ati_mmio_write(s, DST_X_Y,
                       r100_2d_engine_xy(s, payload[word], width_height),
                       sizeof(payload[0]));
        ati_mmio_write(s, DST_WIDTH_HEIGHT, width_height,
                       sizeof(payload[0]));
        if (r->command_budget_exhausted) {
            return false;
        }
    }
    return true;
}

static bool r100_scanline_spans(ATIVGAState *s, uint32_t height_top,
                                const uint32_t *spans, unsigned int count)
{
    unsigned int height = extract32(height_top, 16, 14);

    for (unsigned int word = 0; word < count; word++) {
        int start = sextract32(spans[word], 0, 16);
        int end = sextract32(spans[word], 16, 16);
        uint32_t width_height;
        uint32_t xy;

        if (!height || end <= start) {
            continue;
        }
        /* Scanline endpoints are exclusive, as with rectangle right edges. */
        width_height = ((end - start) & 0x3fffU) << 16 | height;
        xy = ((uint32_t)start & 0x3fff) << 16 | (height_top & 0x3fff);
        ati_mmio_write(s, DST_X_Y,
                       r100_2d_engine_xy(s, xy, width_height),
                       sizeof(spans[0]));
        ati_mmio_write(s, DST_WIDTH_HEIGHT, width_height, sizeof(spans[0]));
        if (s->r100_3d.command_budget_exhausted) {
            return false;
        }
    }
    return true;
}

static bool r100_polyscanlines(ATIVGAState *s, const uint32_t *payload,
                               unsigned int count)
{
    R1002DSettings settings;
    unsigned int scans;
    unsigned int word;

    if (!r100_2d_settings_info(payload, count, &settings)) {
        return false;
    }
    word = settings.dwords;
    if (word == count) {
        /* Establish brush and destination for later PLY_NEXTSCAN packets. */
        r100_2d_settings_apply(s, payload, &settings);
        return true;
    }
    scans = payload[word++];
    if (scans > (count - word) / 2) {
        return false;
    }
    /* Validate every counted subpacket before changing the drawing state. */
    for (unsigned int scan = 0; scan < scans; scan++) {
        unsigned int spans;

        if (count - word < 2) {
            return false;
        }
        spans = payload[word] & 0x3fff;
        word += 2;
        if (spans > count - word) {
            return false;
        }
        word += spans;
    }
    if (word != count) {
        return false;
    }

    r100_2d_settings_apply(s, payload, &settings);
    word = settings.dwords + 1;
    for (unsigned int scan = 0; scan < scans; scan++) {
        unsigned int spans = payload[word++] & 0x3fff;
        uint32_t height_top = payload[word++];

        if (!r100_scanline_spans(s, height_top, payload + word, spans)) {
            return false;
        }
        word += spans;
    }
    return true;
}

static bool r100_bitblt(ATIVGAState *s, unsigned int opcode,
                         const uint32_t *payload, unsigned int count)
{
    ATI3DState *r = &s->r100_3d;
    R1002DSettings settings;
    bool transparent = opcode == R100_PACKET3_CNTL_TRANS_BITBLT;
    unsigned int extra = transparent ? 3 : 0;
    unsigned int rectangles;
    unsigned int word;

    if (!r100_2d_settings_info(payload, count, &settings) ||
        count < settings.dwords + extra + 3) {
        return false;
    }
    rectangles = count - settings.dwords - extra;
    if (rectangles % 3 ||
        (opcode != R100_PACKET3_CNTL_BITBLT_MULTI && rectangles != 3)) {
        return false;
    }
    r100_2d_settings_apply(s, payload, &settings);
    word = settings.dwords;
    if (transparent) {
        ati_mmio_write(s, CLR_CMP_CNTL, payload[word++], sizeof(payload[0]));
        ati_mmio_write(s, CLR_CMP_CLR_SRC, payload[word++], sizeof(payload[0]));
        ati_mmio_write(s, CLR_CMP_CLR_DST, payload[word++], sizeof(payload[0]));
    }

    while (word < count) {
        uint32_t width_height = payload[word + 2];

        ati_mmio_write(s, SRC_X_Y,
                       r100_2d_engine_xy(s, payload[word], width_height),
                       sizeof(payload[0]));
        ati_mmio_write(s, DST_X_Y,
                       r100_2d_engine_xy(s, payload[word + 1], width_height),
                       sizeof(payload[0]));
        ati_mmio_write(s, DST_WIDTH_HEIGHT, width_height,
                       sizeof(payload[0]));
        if (r->command_budget_exhausted) {
            return false;
        }
        word += 3;
    }
    return true;
}

static bool r100_nextchar(ATIVGAState *s, const uint32_t *payload,
                           unsigned int count)
{
    uint32_t source = s->regs.dp_mix & DP_SRC_SOURCE;
    uint32_t source_type = s->regs.dp_datatype & DP_SRC_DATATYPE;
    uint32_t destination_type = s->regs.dp_datatype & DP_DST_DATATYPE;
    uint64_t row_bits;
    unsigned int width;
    unsigned int height;
    uint32_t xy;

    if (count < 3 ||
        (source != DP_SRC_HOST && source != DP_SRC_HOST_BYTEALIGN) ||
        (source_type != SRC_MONO_FRGD_BKGD &&
         source_type != SRC_MONO_FRGD && source_type != SRC_COLOR) ||
        destination_type < DST_8BPP || destination_type > DST_32BPP) {
        return false;
    }
    width = extract32(payload[1], 0, 14);
    height = extract32(payload[1], 16, 14);
    if (!width || !height || (uint64_t)width * height > ATI_2D_MAX_PIXELS) {
        return false;
    }
    if (source_type == SRC_COLOR) {
        unsigned int bytes_per_pixel = destination_type == DST_8BPP ? 1 :
                                       destination_type <= DST_16BPP ? 2 :
                                       destination_type == DST_24BPP ? 3 : 4;

        row_bits = (uint64_t)width * bytes_per_pixel * 8;
    } else {
        row_bits = source == DP_SRC_HOST_BYTEALIGN ?
                   QEMU_ALIGN_UP(width, 8) : width;
    }
    /* Validate the complete inline bitmap before changing the 2D state. */
    if (DIV_ROUND_UP(row_bits * height, 32) != count - 2) {
        return false;
    }

    /* NEXTCHAR uses Y:X and H:W ordering, and names the top-left corner. */
    xy = r100_2d_engine_xy(s, rol32(payload[0], 16),
                           rol32(payload[1], 16));
    ati_mmio_write(s, DST_Y_X, rol32(xy, 16), sizeof(payload[0]));
    ati_mmio_write(s, DST_HEIGHT_WIDTH, payload[1], sizeof(payload[0]));
    if (!s->host_data.active || s->r100_3d.command_budget_exhausted) {
        return false;
    }
    for (unsigned int word = 2; word < count; word++) {
        bool last = word + 1 == count;
        bool active = ati_host_data_write(s, payload[word], last);

        if (s->r100_3d.command_budget_exhausted || active == last) {
            ati_host_data_finish(s);
            return false;
        }
    }
    return true;
}

static bool r100_load_palette(ATIVGAState *s, const uint32_t *payload,
                               unsigned int count)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int entries;

    if (!count || (payload[0] != 1 && payload[0] != 2)) {
        return false;
    }
    entries = payload[0] == 1 ? 16 : 256;
    if (count != entries + 1) {
        return false;
    }
    /* LOAD_PALETTE entries are already encoded in the destination format. */
    memcpy(r->scaler_palette, payload + 1, entries * sizeof(payload[0]));
    r->scaler_palette_format = payload[0];
    r->scaler_palette_valid = true;
    return true;
}

static uint32_t r100_indexed_host_word(uint32_t data, uint32_t swap)
{
    switch (swap & HOST_DATA_SWAP_MASK) {
    case HOST_DATA_SWAP_16BIT:
        return ((data & 0x00ff00ff) << 8) | ((data & 0xff00ff00) >> 8);
    case HOST_DATA_SWAP_32BIT:
        return bswap32(data);
    case HOST_DATA_SWAP_HDW:
        return rol32(data, 16);
    default:
        return data;
    }
}

static bool r100_hostdata_indexed(ATIVGAState *s, const uint32_t *raster,
                                   unsigned int pixels, unsigned int dst_bits,
                                   uint32_t swap)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t packed = 0;
    uint32_t mask = MAKE_64BIT_MASK(0, dst_bits);
    unsigned int packed_bits = 0;

    for (unsigned int pixel = 0; pixel < pixels; pixel++) {
        uint32_t data = r100_indexed_host_word(raster[pixel / 4], swap);
        unsigned int index = (data >> ((pixel % 4) * 8)) & 0xff;
        bool last = pixel + 1 == pixels;

        packed |= (r->scaler_palette[index] & mask) << packed_bits;
        packed_bits += dst_bits;
        if (packed_bits == 32 || last) {
            bool active = ati_host_data_write(s, packed, last);

            if (r->command_budget_exhausted || active == last) {
                ati_host_data_finish(s);
                return false;
            }
            packed = 0;
            packed_bits = 0;
        }
    }
    return true;
}

static bool r100_hostdata_blt(ATIVGAState *s, const uint32_t *payload,
                              unsigned int count)
{
    ATI3DState *r = &s->r100_3d;
    R1002DSettings settings;
    unsigned int word;
    unsigned int dst_bits;
    unsigned int source_type;
    uint32_t datatype, mix, guicntl;
    bool indexed;
    bool success = true;

    if (!r100_2d_settings_info(payload, count, &settings)) {
        return false;
    }
    /* RV100 GMC_SRC_DATATYPE2 extends the source type at bits 13:12. */
    source_type = extract32(settings.gui, 12, 2) |
                  (extract32(settings.gui, 27, 1) << 2);
    indexed = source_type == 5;
    if ((source_type >= 4 && !indexed) ||
        (indexed && !r->scaler_palette_valid)) {
        return false;
    }
    if ((!indexed &&
         (settings.gui & GMC_DP_SRC_SOURCE_MASK) != GMC_DP_SRC_HOST &&
         (settings.gui & GMC_DP_SRC_SOURCE_MASK) !=
         GMC_DP_SRC_HOST_BYTEALIGN) ||
        count < settings.dwords + 2) {
        return false;
    }
    word = settings.dwords + 2;
    while (word < count) {
        unsigned int data_dwords;

        if (count - word < 3) {
            return false;
        }
        data_dwords = payload[word + 2] & 0x3fffU;
        if (!data_dwords || data_dwords > count - word - 3) {
            return false;
        }
        if (indexed) {
            unsigned int width = extract32(payload[word + 1], 0, 14);
            unsigned int height = extract32(payload[word + 1], 16, 14);
            uint64_t pixels = (uint64_t)width * height;

            if (!pixels || pixels > ATI_2D_MAX_PIXELS ||
                DIV_ROUND_UP(pixels, 4) != data_dwords) {
                return false;
            }
        }
        word += 3 + data_dwords;
    }
    if (word != count) {
        return false;
    }

    r100_2d_settings_apply(s, payload, &settings);
    datatype = s->regs.dp_datatype;
    mix = s->regs.dp_mix;
    guicntl = s->regs.rbbm_guicntl;
    dst_bits = extract32(settings.gui, 8, 4);
    dst_bits = dst_bits == 6 ? 32 : dst_bits == 2 ? 8 : 16;
    if (indexed) {
        /* Expanded pixels use the existing ROP/clipping/write-mask path. */
        s->regs.dp_datatype =
            (datatype & ~(DP_SRC_DATATYPE | R100_DP_SRC_DATATYPE2)) | SRC_COLOR;
        s->regs.dp_mix = (mix & ~DP_SRC_SOURCE) | DP_SRC_HOST;
        s->regs.rbbm_guicntl &= ~HOST_DATA_SWAP_MASK;
    }
    word = settings.dwords;
    ati_mmio_write(s, DP_SRC_FRGD_CLR, payload[word++], sizeof(payload[0]));
    ati_mmio_write(s, DP_SRC_BKGD_CLR, payload[word++], sizeof(payload[0]));
    while (word < count) {
        unsigned int data_dwords = payload[word + 2] & 0x3fffU;
        unsigned int pixels = extract32(payload[word + 1], 0, 14) *
                              extract32(payload[word + 1], 16, 14);

        ati_mmio_write(s, DST_Y_X, payload[word++], sizeof(payload[0]));
        ati_mmio_write(s, DST_HEIGHT_WIDTH, payload[word++],
                       sizeof(payload[0]));
        word++;
        if (!s->host_data.active || r->command_budget_exhausted) {
            success = false;
            break;
        }
        if (indexed) {
            success = r100_hostdata_indexed(s, payload + word, pixels,
                                            dst_bits, guicntl);
            if (!success) {
                break;
            }
            word += data_dwords;
            continue;
        }
        for (unsigned int i = 0; i < data_dwords; i++) {
            bool last = i + 1 == data_dwords;
            bool active = ati_host_data_write(s, payload[word++], last);

            if (r->command_budget_exhausted || active == last) {
                ati_host_data_finish(s);
                success = false;
                break;
            }
        }
        if (!success) {
            break;
        }
    }
    s->regs.dp_datatype = datatype;
    s->regs.dp_mix = mix;
    s->regs.rbbm_guicntl = guicntl;
    return success;
}

static bool r100_process_packet3(ATIVGAState *s, unsigned int opcode,
                                 const uint32_t *payload,
                                 unsigned int count)
{
    ATI3DState *r = &s->r100_3d;

    switch (opcode) {
    case R100_PACKET3_SET_SCISSORS:
        if (count != 2) {
            return false;
        }
        ati_mmio_write(s, SC_TOP_LEFT, payload[0], sizeof(payload[0]));
        ati_mmio_write(s, SC_BOTTOM_RIGHT, payload[1], sizeof(payload[1]));
        return true;
    case R100_PACKET3_NOP:
        return true;
    case R100_PACKET3_WAIT_FOR_IDLE:
        /* TODO: Implement asynchronous CP execution and a real idle wait. */
        return true;
    case R100_PACKET3_3D_LOAD_VBPNTR:
        return r100_load_vbpntr(s, payload, count);
    case R100_PACKET3_3D_RNDR_GEN_INDX_PRIM:
        if (count < 4) {
            return false;
        }
        r->vertex_array_count = 1;
        r->vertex_array[0].address = payload[0];
        r->vertex_array[0].components = r100_vertex_dwords(payload[2]);
        r->vertex_array[0].stride = r->vertex_array[0].components;
        if ((payload[3] & R100_VF_PRIM_WALK_MASK) ==
            R100_VF_PRIM_WALK_IND) {
            return r100_draw_indexed(s, payload[2], payload[3],
                                     payload + 4, count - 4);
        }
        return count == 4 && r100_draw_vbuf(s, payload[2], payload[3]);
    case R100_PACKET3_3D_DRAW_VBUF:
        return count >= 2 && r100_draw_vbuf(s, payload[0], payload[1]);
    case R100_PACKET3_3D_DRAW_VBUF_2:
        return count >= 1 && r100_draw_vbuf(s, r->se_vtx_fmt, payload[0]);
    case R100_PACKET3_3D_RNDR_GEN_PRIM:
    case R100_PACKET3_3D_DRAW_IMMD:
        return count >= 2 && r100_draw_immediate(s, payload[0], payload[1],
                                                 payload + 2, count - 2);
    case R100_PACKET3_3D_DRAW_IMMD_2:
        return count >= 1 && r100_draw_immediate(s, r->se_vtx_fmt,
                                                 payload[0], payload + 1,
                                                 count - 1);
    case R100_PACKET3_3D_DRAW_INDX:
        return count >= 2 && r100_draw_indexed(s, payload[0], payload[1],
                                               payload + 2, count - 2);
    case R100_PACKET3_3D_DRAW_INDX_2:
        return count >= 1 && r100_draw_indexed(s, r->se_vtx_fmt,
                                               payload[0], payload + 1,
                                               count - 1);
    case R100_PACKET3_NEXT_CHAR:
        return r100_nextchar(s, payload, count);
    case R100_PACKET3_PLY_NEXTSCAN:
        return count >= 2 &&
               r100_scanline_spans(s, payload[0], payload + 1, count - 1);
    case R100_PACKET3_CNTL_POLYSCANLINES:
        return r100_polyscanlines(s, payload, count);
    case R100_PACKET3_LOAD_PALETTE:
        return r100_load_palette(s, payload, count);
    case R100_PACKET3_CNTL_HOSTDATA_BLT:
        return r100_hostdata_blt(s, payload, count);
    case R100_PACKET3_CNTL_PAINT_MULTI:
        return r100_paint_multi(s, payload, count);
    case R100_PACKET3_CNTL_BITBLT:
    case R100_PACKET3_CNTL_BITBLT_MULTI:
    case R100_PACKET3_CNTL_TRANS_BITBLT:
        return r100_bitblt(s, opcode, payload, count);
    default:
        trace_ati_cp_packet3_unknown(opcode, count,
                                    count > 0 ? payload[0] : 0,
                                    count > 1 ? payload[1] : 0,
                                    count > 2 ? payload[2] : 0,
                                    count > 3 ? payload[3] : 0);
        qemu_log_mask(LOG_UNIMP,
                      "ati-r100: unimplemented PACKET3 opcode 0x%x\n",
                      opcode);
        r->rejected_commands++;
        return false;
    }
}

static bool r100_process_stream(ATIVGAState *s, R100Stream *stream)
{
    ATI3DState *r = &s->r100_3d;
    bool outermost = r->processing_depth == 0;
    bool ok = true;

    if (r->processing_depth >= 4) {
        r->rejected_commands++;
        return false;
    }
    if (outermost) {
        r->draw_budget_remaining = R100_MAX_DRAW_PIXELS;
        r->command_work_remaining = R100_MAX_COMMAND_WORK;
        r->blit_work_remaining = ATI_2D_MAX_PIXELS;
        r->command_budget_exhausted = false;
    }
    r->processing_depth++;
    while (stream->remaining) {
        g_autofree uint32_t *payload = NULL;
        uint32_t header;
        unsigned int type;
        unsigned int count;
        unsigned int max_count = ATI_3D_MAX_VERTEX_DWORDS;
        unsigned int i;

        if (!r100_stream_read(s, stream, &header)) {
            ok = false;
            break;
        }
        type = header & R100_CP_PACKET_TYPE_MASK;
        if (type == R100_CP_PACKET2) {
            continue;
        }
        if (type == R100_CP_PACKET1) {
            uint32_t first, second;

            if (!r100_stream_read(s, stream, &first) ||
                !r100_stream_read(s, stream, &second)) {
                ok = false;
                break;
            }
            if (!r100_packet0_write(s, (header & 0x7ffU) << 2, first) ||
                !r100_packet0_write(s, ((header >> 11) & 0x7ffU) << 2,
                                    second)) {
                ok = false;
                break;
            }
            continue;
        }

        if (type == R100_CP_PACKET3 &&
            (extract32(header, 8, 8) == R100_PACKET3_CNTL_HOSTDATA_BLT ||
             extract32(header, 8, 8) == R100_PACKET3_NEXT_CHAR ||
             extract32(header, 8, 8) == R100_PACKET3_PLY_NEXTSCAN ||
             extract32(header, 8, 8) == R100_PACKET3_CNTL_POLYSCANLINES ||
             extract32(header, 8, 8) == R100_PACKET3_CNTL_PAINT_MULTI ||
             extract32(header, 8, 8) == R100_PACKET3_CNTL_BITBLT_MULTI)) {
            max_count = R100_MAX_PACKET_DWORDS;
        }
        count = extract32(header, R100_CP_PACKET_COUNT_SHIFT, 14) + 1;
        if (count > stream->remaining || count > max_count) {
            ok = false;
            break;
        }
        payload = g_new(uint32_t, count);
        for (i = 0; i < count; i++) {
            if (!r100_stream_read(s, stream, &payload[i])) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }
        if (type == R100_CP_PACKET0) {
            hwaddr reg = (header & 0x1fffU) << 2;
            bool one_reg = header & R100_CP_PACKET0_ONE_REG;

            for (i = 0; i < count; i++) {
                if (!r100_packet0_write(s, reg, payload[i])) {
                    ok = false;
                    break;
                }
                if (!one_reg) {
                    reg += 4;
                }
            }
            if (!ok) {
                break;
            }
        } else if (type == R100_CP_PACKET3) {
            if (!r100_process_packet3(s, extract32(header, 8, 8),
                                      payload, count)) {
                ok = false;
                break;
            }
        } else {
            ok = false;
            break;
        }
    }
    r->processing_depth--;
    if (outermost) {
        r->draw_budget_remaining = 0;
        r->command_work_remaining = 0;
        r->blit_work_remaining = 0;
        r->command_budget_exhausted = false;
    }
    if (!ok) {
        r->rejected_commands++;
    }
    return ok;
}

static uint32_t r100_ring_dwords(const ATI3DState *r)
{
    unsigned int order = r->cp_rb_cntl & R100_RB_BUFSZ_MASK;

    if (order >= 20) {
        return 0;
    }
    return 1U << (order + 1);
}

static void r100_rptr_writeback(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;
    uint32_t address = r->cp_rb_rptr_addr & ~R100_RB_RPTR_SWAP_MASK;
    uint32_t value = r100_swap_word(r->cp_rb_rptr,
                                    r->cp_rb_rptr_addr &
                                    R100_RB_RPTR_SWAP_MASK);

    if (!(r->cp_rb_cntl & R100_RB_NO_UPDATE)) {
        r100_gpu_write_u32(s, address, value, true);
    }
}

static void r100_process_ring(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int mode = extract32(r->cp_csq_cntl, 28, 4);
    uint32_t dwords = r100_ring_dwords(r);
    uint32_t mask;
    R100Stream stream;

    if (r->processing_depth || mode < 2 || mode > 8 || (mode & 1) || !dwords ||
        dwords > R100_MAX_RING_DWORDS) {
        return;
    }
    mask = dwords - 1;
    r->cp_rb_rptr &= mask;
    r->cp_rb_wptr &= mask;
    stream = (R100Stream) {
        .base = r->cp_rb_base,
        .pos = r->cp_rb_rptr,
        .remaining = (r->cp_rb_wptr - r->cp_rb_rptr) & mask,
        .mask = mask,
        .ring = true,
    };
    if (!stream.remaining) {
        return;
    }
    if (!r100_process_stream(s, &stream)) {
        r->cp_rb_rptr = r->cp_rb_wptr;
    } else {
        r->cp_rb_rptr = stream.pos & mask;
    }
    r100_rptr_writeback(s);
}

static bool r100_process_ib(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;
    unsigned int mode = extract32(r->cp_csq_cntl, 28, 4);
    R100Stream stream;

    if (mode < 3 || mode > 8 || !r->cp_ib_bufsz) {
        return true;
    }
    if (r->cp_ib_bufsz > R100_MAX_RING_DWORDS) {
        return false;
    }
    stream = (R100Stream) {
        .base = r->cp_ib_base,
        .remaining = r->cp_ib_bufsz,
    };
    return r100_process_stream(s, &stream);
}

static void r100_port_submit(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;
    bool owns_budget;

    if (!r->port_data_expected ||
        r->port_data_count < r->port_data_expected) {
        return;
    }
    owns_budget = r->processing_depth == 0;
    if (owns_budget) {
        r->draw_budget_remaining = R100_MAX_DRAW_PIXELS;
    }
    r100_draw_immediate(s, r->se_vtx_fmt, r->se_vf_cntl,
                        r->port_data, r->port_data_expected);
    if (owns_budget) {
        r->draw_budget_remaining = 0;
    }
    r->port_data_count = 0;
}

bool ati_3d_read(ATIVGAState *s, hwaddr addr, uint64_t *data,
                 unsigned int size)
{
    ATI3DState *r = &s->r100_3d;
    hwaddr base = addr & ~3ULL;
    uint32_t *reg;
    uint32_t value;

    if (!ati_is_rv100_family(s)) {
        return false;
    }
    if (base == R100_CP_STAT) {
        value = 0;
    } else if (base == R100_SE_CNTL_STATUS) {
        value = r->se_cntl_status;
    } else {
        reg = r100_register_ptr(r, base);
        if (reg == NULL) {
            return false;
        }
        value = *reg;
    }
    *data = r100_extract_read(value, addr, size);
    return true;
}

bool ati_3d_write(ATIVGAState *s, hwaddr addr, uint64_t data,
                  unsigned int size)
{
    ATI3DState *r = &s->r100_3d;
    hwaddr base = addr & ~3ULL;
    uint32_t *reg;
    uint32_t value;

    if (!ati_is_rv100_family(s)) {
        return false;
    }
    if (base == R100_FOG_TABLE_INDEX) {
        if (size == 4 && !(addr & 3)) {
            r->fog_table_index = data & UINT8_MAX;
        }
        return true;
    }
    if (base == R100_FOG_TABLE_DATA) {
        if (size == 4 && !(addr & 3)) {
            r->fog_table[r->fog_table_index++] = data & UINT8_MAX;
        }
        return true;
    }
    if (base >= R100_SE_PORT_DATA0 && base <= R100_SE_PORT_DATA_END) {
        if (size == 4 && !(addr & 3) &&
            r->port_data_count < ATI_3D_MAX_VERTEX_DWORDS) {
            r->port_data[r->port_data_count++] = data;
            r100_port_submit(s);
        }
        return true;
    }
    if (base == R100_CP_STAT) {
        return true;
    }
    if (base == R100_SE_CNTL_STATUS) {
        /* TODO: Implement the TCL engine instead of forcing bypass. */
        value = r100_merge_write(r->se_cntl_status, addr, data, size);
        r->se_cntl_status = (value & R100_VC_SWAP_MASK) | R100_TCL_BYPASS;
        return true;
    }
    reg = r100_register_ptr(r, base);
    if (reg == NULL) {
        return false;
    }
    value = r100_merge_write(*reg, addr, data, size);

    if (base == R100_CP_RB_RPTR) {
        return true;
    }
    *reg = value;
    if (base >= R100_SCRATCH_REG0 && base <= R100_SCRATCH_REG7) {
        r100_write_scratchback(s, (base - R100_SCRATCH_REG0) / 4);
    }
    switch (base) {
    case R100_CP_RB_RPTR_WR:
        if (r->cp_rb_cntl & R100_RB_RPTR_WR_ENA) {
            r->cp_rb_rptr = value;
        }
        break;
    case R100_CP_RB_WPTR:
    case R100_CP_CSQ_CNTL:
        r100_process_ring(s);
        break;
    case R100_CP_IB_BUFSZ:
        (void)r100_process_ib(s);
        break;
    case R100_SE_VF_CNTL:
        r->port_data_count = 0;
        r->port_data_expected = r100_vertex_dwords(r->se_vtx_fmt) *
            extract32(r->se_vf_cntl, R100_VF_NUM_VERTICES_SHIFT, 16);
        if (r->port_data_expected > ATI_3D_MAX_VERTEX_DWORDS) {
            r->port_data_expected = 0;
            r->rejected_commands++;
        }
        break;
    /* TODO: Store and read back CP ME RAM; packets are decoded directly. */
    case R100_CP_ME_RAM_DATAL:
        r->me_ram_addr++;
        break;
    default:
        break;
    }
    return true;
}

void ati_3d_reset(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;
    uint64_t top = s->vga.vram_size ? s->vga.vram_size - 1 : 0;

    memset(r, 0, sizeof(*r));
    r->mc_fb_location = ((top >> 16) & 0xffffU) << 16;
    r->se_cntl_status = R100_TCL_BYPASS;
    r->context[(R100_RB3D_PLANEMASK - R100_CONTEXT_BASE) / 4] = UINT32_MAX;
    r->context[(R100_SE_CNTL - R100_CONTEXT_BASE) / 4] =
        (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10);
}

int ati_3d_post_load(ATIVGAState *s)
{
    ATI3DState *r = &s->r100_3d;

    r->processing_depth = 0;
    r->draw_budget_remaining = 0;
    r->command_work_remaining = 0;
    r->blit_work_remaining = 0;
    r->command_budget_exhausted = false;
    r->se_cntl_status = (r->se_cntl_status & R100_VC_SWAP_MASK) |
                        R100_TCL_BYPASS;
    if (r->vertex_array_count > ATI_3D_MAX_VERTEX_ARRAYS ||
        r->port_data_count > ATI_3D_MAX_VERTEX_DWORDS ||
        r->port_data_expected > ATI_3D_MAX_VERTEX_DWORDS ||
        r->scaler_palette_format > 2 ||
        (r->scaler_palette_valid && !r->scaler_palette_format)) {
        return -EINVAL;
    }
    return 0;
}

static const VMStateDescription vmstate_ati_3d_vertex_array = {
    .name = "ati-vga/r100-3d/vertex-array",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(address, ATI3DVertexArray),
        VMSTATE_UINT16(stride, ATI3DVertexArray),
        VMSTATE_UINT8(components, ATI3DVertexArray),
        VMSTATE_UINT8(reserved, ATI3DVertexArray),
        VMSTATE_END_OF_LIST()
    },
};

const VMStateDescription vmstate_ati_3d = {
    .name = "ati-vga/r100-3d",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(context, ATI3DState, ATI_3D_CONTEXT_DWORDS),
        VMSTATE_UINT32(re_top_left, ATI3DState),
        VMSTATE_UINT32(re_misc, ATI3DState),
        VMSTATE_UINT32(se_vtx_fmt, ATI3DState),
        VMSTATE_UINT32(se_vf_cntl, ATI3DState),
        VMSTATE_UINT32(se_cntl_status, ATI3DState),
        VMSTATE_UINT32(cp_rb_base, ATI3DState),
        VMSTATE_UINT32(cp_rb_cntl, ATI3DState),
        VMSTATE_UINT32(cp_rb_rptr_addr, ATI3DState),
        VMSTATE_UINT32(cp_rb_rptr, ATI3DState),
        VMSTATE_UINT32(cp_rb_rptr_wr, ATI3DState),
        VMSTATE_UINT32(cp_rb_wptr, ATI3DState),
        VMSTATE_UINT32(cp_ib_base, ATI3DState),
        VMSTATE_UINT32(cp_ib_bufsz, ATI3DState),
        VMSTATE_UINT32(cp_csq_cntl, ATI3DState),
        VMSTATE_UINT32(cp_csq_mode, ATI3DState),
        VMSTATE_UINT32_ARRAY(scratch, ATI3DState, 8),
        VMSTATE_UINT32(scratch_umsk, ATI3DState),
        VMSTATE_UINT32(scratch_addr, ATI3DState),
        VMSTATE_UINT32(me_ram_addr, ATI3DState),
        VMSTATE_UINT32(me_ram_raddr, ATI3DState),
        VMSTATE_UINT32(me_datah, ATI3DState),
        VMSTATE_UINT32(me_datal, ATI3DState),
        VMSTATE_UINT32(mc_fb_location, ATI3DState),
        VMSTATE_UINT32(mc_agp_location, ATI3DState),
        VMSTATE_UINT32(agp_base, ATI3DState),
        VMSTATE_UINT32(aic_cntl, ATI3DState),
        VMSTATE_UINT32(aic_pt_base, ATI3DState),
        VMSTATE_UINT32(aic_lo_addr, ATI3DState),
        VMSTATE_UINT32(aic_hi_addr, ATI3DState),
        VMSTATE_STRUCT_ARRAY(vertex_array, ATI3DState,
                             ATI_3D_MAX_VERTEX_ARRAYS, 1,
                             vmstate_ati_3d_vertex_array,
                             ATI3DVertexArray),
        VMSTATE_UINT32(vertex_array_count, ATI3DState),
        VMSTATE_UINT32_ARRAY(port_data, ATI3DState,
                             ATI_3D_MAX_VERTEX_DWORDS),
        VMSTATE_UINT32(port_data_count, ATI3DState),
        VMSTATE_UINT32(port_data_expected, ATI3DState),
        VMSTATE_UINT64(submitted_primitives, ATI3DState),
        VMSTATE_UINT64(rejected_commands, ATI3DState),
        VMSTATE_END_OF_LIST()
    },
};
