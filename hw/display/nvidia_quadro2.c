/*
 * NVIDIA Quadro2 Pro (NV15) display adapter
 *
 * Technical references are listed in docs/devel/gpu-emulation-provenance.rst.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The following MIT notice applies to register, tiling and DMA-pusher
 * definitions derived from envytools:
 *
 * Copyright (C) 2012 Marcelina Kościelnicka <mwk@0x04.net>
 * Copyright (C) 2013 Marcelina Kościelnicka
 * Copyright (C) 2013 Ben Skeggs
 * Copyright (C) 2013 Martin Peres
 * Copyright (C) 2013 Maarten Lankhorst
 * Copyright (C) 2013 Roy Spliet
 * Copyright (C) 2013 Christoph Bumiller
 * Copyright (C) 2013 Marcin Ślusarz
 * Copyright (C) 2013 Emil Velikov
 * Copyright (C) 2013 Francisco Jerez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"

#include "hw/core/qdev-properties.h"
#include "hw/display/edid.h"
#include "hw/display/i2c-ddc.h"
#include "hw/display/nvidia_quadro2.h"
#include "hw/display/vga_int.h"
#include "hw/display/vga_regs.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"

#define NV15_PMC_BASE                  0x000000
#define NV15_PMC_SIZE                  0x001000
#define NV15_PBUS_PCI_BASE             0x001800
#define NV15_PBUS_PCI_SIZE             0x000100
#define NV15_PFIFO_BASE                0x002000
#define NV15_PFIFO_SIZE                0x002000
#define NV15_PTIMER_BASE               0x009000
#define NV15_PTIMER_SIZE               0x001000
#define NV15_PRMVIO_BASE               0x0c0000
#define NV15_PRMVIO_SIZE               0x001000
#define NV15_PFB_BASE                  0x100000
#define NV15_PFB_SIZE                  0x001000
#define NV15_PEXTDEV_BASE              0x101000
#define NV15_PEXTDEV_SIZE              0x001000
#define NV15_PGRAPH_BASE               0x400000
#define NV15_PGRAPH_SIZE               0x002000
#define NV15_PCRTC_BASE                0x600000
#define NV15_PCRTC_SIZE                0x001000
#define NV15_PRMCIO_BASE               0x601000
#define NV15_PRMCIO_SIZE               0x001000
#define NV15_PRAMDAC_BASE              0x680000
#define NV15_PRAMDAC_SIZE              0x001000
#define NV15_PRMDIO_BASE               0x681000
#define NV15_PRMDIO_SIZE               0x001000
#define NV15_PRAMIN_BASE               0x700000
#define NV15_PRAMIN_SIZE               0x100000
#define NV15_USER_BASE                 0x800000

#define NV15_PMC_BOOT_0                0x000000
#define NV15_PMC_BOOT_1                0x000004
#define NV15_PMC_INTR_0                0x000100
#define NV15_PMC_INTR_EN_0             0x000140
#define NV15_PMC_ENABLE                0x000200

#define NV15_PFIFO_INTR_0              0x002100
#define NV15_PFIFO_INTR_EN_0           0x002140
#define NV15_PFIFO_RAMHT               0x002210
#define NV15_PFIFO_RAMFC               0x002214
#define NV15_PFIFO_RAMRO               0x002218
#define NV15_PFIFO_CACHES              0x002500
#define NV15_PFIFO_MODE                0x002504
#define NV15_PFIFO_DMA                 0x002508
#define NV15_PFIFO_SIZE_REG            0x00250c
#define NV15_PFIFO_CACHE1_PUSH0        0x003200
#define NV15_PFIFO_CACHE1_PUSH1        0x003204
#define NV15_PFIFO_CACHE1_PUT          0x003210
#define NV15_PFIFO_CACHE1_DMA_PUSH     0x003220
#define NV15_PFIFO_CACHE1_DMA_FETCH    0x003224
#define NV15_PFIFO_CACHE1_DMA_STATE    0x003228
#define NV15_PFIFO_CACHE1_DMA_INSTANCE 0x00322c
#define NV15_PFIFO_CACHE1_DMA_CTL      0x003230
#define NV15_PFIFO_CACHE1_DMA_PUT      0x003240
#define NV15_PFIFO_CACHE1_DMA_GET      0x003244
#define NV15_PFIFO_CACHE1_REF_CNT      0x003248
#define NV15_PFIFO_CACHE1_DMA_SUBROUTINE 0x00324c
#define NV15_PFIFO_CACHE1_PULL0        0x003250
#define NV15_PFIFO_CACHE1_PULL1        0x003254
#define NV15_PFIFO_CACHE1_HASH         0x003258
#define NV15_PFIFO_CACHE1_ENGINE       0x003280
#define NV15_PFIFO_CACHE1_DMA_DCOUNT   0x0032a0

#define NV15_PTIMER_INTR_0             0x009100
#define NV15_PTIMER_INTR_EN_0          0x009140
#define NV15_PTIMER_NUMERATOR          0x009200
#define NV15_PTIMER_DENOMINATOR        0x009210
#define NV15_PTIMER_TIME_0             0x009400
#define NV15_PTIMER_TIME_1             0x009410
#define NV15_PTIMER_ALARM_0            0x009420

#define NV15_PFB_BOOT_0                0x100000
#define NV15_PFB_CFG0                  0x100200
#define NV15_PFB_CFG1                  0x100204
#define NV15_PFB_FIFO_DATA             0x10020c

#define NV15_PGRAPH_INTR               0x400100
#define NV15_PGRAPH_NSTATUS            0x400104
#define NV15_PGRAPH_NSOURCE            0x400108
#define NV15_PGRAPH_INTR_EN            0x400140
#define NV15_PGRAPH_STATUS             0x400700
#define NV15_PGRAPH_TRAPPED_ADDR       0x400704
#define NV15_PGRAPH_TRAPPED_DATA       0x400708
#define NV15_PGRAPH_TRAPPED_DATA_HIGH  0x40070c
#define NV15_PGRAPH_FIFO_ACCESS        0x400720

#define NV15_PCRTC_INTR_0              0x600100
#define NV15_PCRTC_INTR_EN_0           0x600140
#define NV15_PCRTC_START               0x600800
#define NV15_PCRTC_CONFIG              0x600804
#define NV15_PCRTC_CURSOR_CONFIG       0x600810

#define NV15_PRAMDAC_CURSOR_POS        0x680300
#define NV15_PRAMDAC_NVPLL_COEFF       0x680500
#define NV15_PRAMDAC_MPLL_COEFF        0x680504
#define NV15_PRAMDAC_VPLL_COEFF        0x680508
#define NV15_PRAMDAC_PLL_COEFF_SELECT  0x68050c
#define NV15_PRAMDAC_GENERAL_CONTROL   0x680600

#define NV15_PMC_INTR_PFIFO            BIT(8)
#define NV15_PMC_INTR_PGRAPH           BIT(12)
#define NV15_PMC_INTR_PTIMER           BIT(20)
#define NV15_PMC_INTR_PCRTC            BIT(24)
#define NV15_PMC_INTR_MASTER_ENABLE    BIT(0)
#define NV15_PTIMER_INTR_ALARM         BIT(0)
#define NV15_PTIMER_ALARM_MASK         UINT32_C(0xffffffe0)
#define NV15_PTIMER_LOW_BITS           27
#define NV15_PTIMER_LOW_PERIOD         (UINT64_C(1) << NV15_PTIMER_LOW_BITS)
#define NV15_PTIMER_LOW_MASK           (NV15_PTIMER_LOW_PERIOD - 1)
#define NV15_PTIMER_HIGH_MASK          UINT32_C(0x1fffffff)
#define NV15_PTIMER_TIME_MASK          ((UINT64_C(1) << 56) - 1)
/* The reset NVPLL is 100.226 MHz; this ratio produces 31.25 MHz. */
#define NV15_XTAL_HZ                   UINT32_C(14318000)
#define NV15_PTIMER_DEFAULT_DIV        UINT32_C(0xc3c1)
#define NV15_PTIMER_DEFAULT_MUL        UINT32_C(0x3d09)
#define NV15_PCRTC_INTR_VBLANK         BIT(0)

#define NV15_CURSOR_CONFIG_DOUBLE_SCAN BIT(4)
#define NV15_CURSOR_CONFIG_PNVM        BIT(8)
#define NV15_CURSOR_CONFIG_32BPP       BIT(12)
#define NV15_CURSOR_CONFIG_64_PIXELS   BIT(16)
#define NV15_CURSOR_CONFIG_LINES_MASK  (0xfU << 24)
#define NV15_CURSOR_CONFIG_ALPHA_BLEND BIT(28)
#define NV15_CURSOR_CRTC_ADDR2         0x2f
#define NV15_CURSOR_CRTC_ADDR0         0x30
#define NV15_CURSOR_CRTC_ADDR1         0x31
#define NV15_CURSOR_ADDR0_PNVM         BIT(7)
#define NV15_CURSOR_ENABLE             BIT(0)
#define NV15_CURSOR_DOUBLE_SCAN        BIT(1)
#define NV15_CURSOR_MAX_WIDTH          64U
#define NV15_CURSOR_MAX_HEIGHT         64U
#define NV15_CURSOR_MAX_BYTES          \
    (NV15_CURSOR_MAX_WIDTH * NV15_CURSOR_MAX_HEIGHT * sizeof(uint32_t))

#define NV15_PFIFO_INTR_CACHE_ERROR    BIT(0)
#define NV15_PFIFO_INTR_DMA_PUSHER     BIT(12)
#define NV15_PFIFO_PUSH1_DMA           BIT(8)
#define NV15_PFIFO_DMA_PUSH_ACCESS     BIT(0)
#define NV15_PFIFO_DMA_PUSH_STATUS     BIT(12)
#define NV15_PFIFO_DMA_STATE_NONINC    BIT(0)
#define NV15_PFIFO_DMA_STATE_METHOD    0x00001ffcU
#define NV15_PFIFO_DMA_STATE_SUBCH     0x0000e000U
#define NV15_PFIFO_DMA_STATE_COUNT     0x1ffc0000U
#define NV15_PFIFO_DMA_STATE_ERROR     0xe0000000U
#define NV15_PFIFO_DMA_ERROR_INVALID_METHOD (2U << 29)
#define NV15_PFIFO_DMA_ERROR_INVALID_COMMAND (4U << 29)
#define NV15_PFIFO_DMA_ERROR_MEMORY    (6U << 29)
#define NV15_PFIFO_DMA_SUBROUTINE_ACTIVE BIT(0)

#define NV15_PGRAPH_INTR_NOTIFY        BIT(0)
#define NV15_PGRAPH_INTR_ERROR         BIT(20)
#define NV15_PGRAPH_NSTATUS_STATE_IN_USE BIT(23)
#define NV15_PGRAPH_NSTATUS_INVALID_STATE BIT(24)
#define NV15_PGRAPH_NSTATUS_BAD_ARGUMENT BIT(25)
#define NV15_PGRAPH_NSTATUS_PROTECTION BIT(26)
#define NV15_PGRAPH_NSOURCE_DATA_ERROR BIT(1)
#define NV15_PGRAPH_NSOURCE_ILLEGAL_METHOD BIT(6)
#define NV15_PGRAPH_NSOURCE_STATE_INVALID BIT(11)
#define NV15_PGRAPH_NSOURCE_DOUBLE_NOTIFY BIT(12)
#define NV15_PGRAPH_NSOURCE_NOTIFY_IN_USE BIT(13)

#define NV15_USER_CHANNEL_SIZE         0x00010000U
#define NV15_USER_CHANNELS             32U
#define NV15_USER_SUBCHANNEL_SIZE      0x00002000U
#define NV15_USER_SUBCHANNELS          8U
#define NV15_USER_DMA_PUT              0x40U
#define NV15_USER_DMA_GET              0x44U
#define NV15_USER_REF_CNT              0x48U

#define NV15_RAMIN_SIZE                NV15_PRAMIN_SIZE
#define NV15_RAMHT_VALID               BIT(31)
#define NV15_RAMHT_ENGINE_MASK         (3U << 16)
#define NV15_RAMHT_ENGINE_DMA          0
#define NV15_RAMHT_ENGINE_GRAPHICS     BIT(16)
#define NV15_RAMHT_CHANNEL_SHIFT       24
#define NV15_RAMHT_INSTANCE_MASK       0x0000ffffU

#define NV15_DMA_CLASS_MASK            0x00000fffU
#define NV15_GRAPHICS_CLASS_MASK       0x000000ffU
#define NV15_DMA_TARGET_MASK           0x00030000U
#define NV15_DMA_TARGET_VRAM           0x00000000U
#define NV15_DMA_TARGET_VRAM_TILED     0x00010000U
#define NV15_DMA_TARGET_PCI            0x00020000U
#define NV15_DMA_TARGET_AGP            0x00030000U
#define NV15_DMA_PRESENT                BIT(12)
#define NV15_DMA_LINEAR                 BIT(13)
#define NV15_DMA_ADJUST_MASK           0xfff00000U
#define NV15_DMA_FRAME_MASK            0xfffff000U
#define NV15_DMA_FROM_MEMORY           0x02U
#define NV15_DMA_TO_MEMORY             0x03U
#define NV15_DMA_IN_MEMORY             0x3dU

#define NV15_CLASS_CONTEXT_CLIP        0x19U
#define NV15_CLASS_M2MF                0x39U
#define NV15_CLASS_CONTEXT_ROP         0x43U
#define NV15_CLASS_IMAGE_PATTERN       0x44U
#define NV15_CLASS_GDI_RECTANGLE_TEXT  0x4aU
#define NV15_CLASS_LINE                0x5cU
#define NV15_CLASS_RECTANGLE           0x5eU
#define NV15_CLASS_IMAGE_BLIT          0x5fU
#define NV15_CLASS_IMAGE_FROM_CPU      0x61U
#define NV15_CLASS_NV5_SCALED_IMAGE    0x63U
#define NV15_CLASS_NV5_IMAGE_FROM_CPU  0x65U
#define NV15_CLASS_NV4_SCALED_IMAGE    0x77U
#define NV15_CLASS_CONTEXT_SURFACES_2D 0x62U
#define NV15_CLASS_SCALED_IMAGE        0x89U
#define NV15_CLASS_NV10_IMAGE_FROM_CPU 0x8aU
#define NV15_CLASS_NV11_IMAGE_BLIT     0x9fU

#define NV15_GRAPH_NOP                 0x0100U
#define NV15_GRAPH_NOTIFY              0x0104U
#define NV15_GRAPH_WAIT_FOR_IDLE       0x0108U
#define NV15_GRAPH_DMA_NOTIFY          0x0180U
#define NV15_GRAPH_OPERATION           0x02fcU

#define NV15_SURFACE_DMA_SOURCE        0x0184U
#define NV15_SURFACE_DMA_DEST          0x0188U
#define NV15_SURFACE_FORMAT            0x0300U
#define NV15_SURFACE_PITCH             0x0304U
#define NV15_SURFACE_OFFSET_SOURCE     0x0308U
#define NV15_SURFACE_OFFSET_DEST       0x030cU

#define NV15_ROP_VALUE                 0x0300U
#define NV15_PATTERN_FORMAT            0x0300U
#define NV15_PATTERN_MONO_FORMAT       0x0304U
#define NV15_PATTERN_MONO_SHAPE        0x0308U
#define NV15_PATTERN_SELECT            0x030cU
#define NV15_PATTERN_MONO_COLOR0       0x0310U
#define NV15_PATTERN_MONO_COLOR1       0x0314U
#define NV15_PATTERN_MONO_BITMAP0      0x0318U
#define NV15_PATTERN_MONO_BITMAP1      0x031cU

#define NV15_RECT_PATTERN              0x0188U
#define NV15_RECT_ROP                  0x018cU
#define NV15_RECT_SURFACE              0x0198U
#define NV15_RECT_FORMAT               0x0300U
#define NV15_RECT_MONO_FORMAT          0x0304U
#define NV15_RECT_COLOR                0x03fcU
#define NV15_RECT_POINT_BASE           0x0400U
#define NV15_MONO_FORMAT_CGA6          1U
#define NV15_MONO_FORMAT_LE            2U
#define NV15_GDI_MONO1_CLIP_POINT0     0x07ecU
#define NV15_GDI_MONO1_CLIP_POINT1     0x07f0U
#define NV15_GDI_MONO1_COLOR           0x07f4U
#define NV15_GDI_MONO1_SIZE            0x07f8U
#define NV15_GDI_MONO1_POINT           0x07fcU
#define NV15_GDI_MONO1_DATA_BASE       0x0800U
#define NV15_GDI_MONO2_CLIP_POINT0     0x0be4U
#define NV15_GDI_MONO2_CLIP_POINT1     0x0be8U
#define NV15_GDI_MONO2_COLOR0          0x0becU
#define NV15_GDI_MONO2_COLOR1          0x0bf0U
#define NV15_GDI_MONO2_SIZE_IN         0x0bf4U
#define NV15_GDI_MONO2_SIZE_OUT        0x0bf8U
#define NV15_GDI_MONO2_POINT           0x0bfcU
#define NV15_GDI_MONO2_DATA_BASE       0x0c00U
#define NV15_GDI_MONO_DATA_WORDS       128U

#define NV15_LINE_CLIP                 0x0184U
#define NV15_LINE_PATTERN              0x0188U
#define NV15_LINE_ROP                  0x018cU
#define NV15_LINE_SURFACE              0x0198U
#define NV15_LINE_FORMAT               0x0300U
#define NV15_LINE_COLOR                0x0304U
#define NV15_LINE_POINT_BASE           0x0400U
#define NV15_LINE_POINT_SIZE           0x0080U
#define NV15_LINE_POINT32_BASE         0x0480U
#define NV15_LINE_POLYLINE_BASE        0x0500U
#define NV15_LINE_POLYLINE32_BASE      0x0580U
#define NV15_LINE_CPOLYLINE_BASE       0x0600U
#define NV15_LINE_VERTEX_VALID         BIT(0)
#define NV15_LINE_X_VALID              BIT(1)

#define NV15_SOLID_CLIP                0x0184U
#define NV15_SOLID_PATTERN             0x0188U
#define NV15_SOLID_ROP                 0x018cU
#define NV15_SOLID_SURFACE             0x0198U
#define NV15_SOLID_FORMAT              0x0300U
#define NV15_SOLID_COLOR               0x0304U
#define NV15_SOLID_POINT_BASE          0x0400U

#define NV15_BLIT_CLIP                 0x0188U
#define NV15_BLIT_PATTERN              0x018cU
#define NV15_BLIT_ROP                  0x0190U
#define NV15_BLIT_SURFACES             0x019cU
#define NV15_BLIT_POINT_IN             0x0300U
#define NV15_BLIT_POINT_OUT            0x0304U
#define NV15_BLIT_SIZE                 0x0308U

#define NV15_CPU_IMAGE_PATTERN         0x018cU
#define NV15_CPU_IMAGE_ROP             0x0190U
#define NV15_CPU_IMAGE_SURFACE         0x019cU
#define NV15_CPU_IMAGE_FORMAT          0x0300U
#define NV15_CPU_IMAGE_POINT           0x0304U
#define NV15_CPU_IMAGE_SIZE_OUT        0x0308U
#define NV15_CPU_IMAGE_SIZE_IN         0x030cU
#define NV15_CPU_IMAGE_DATA_BASE       0x0400U

#define NV15_SCALED_DMA_IMAGE          0x0184U
#define NV15_SCALED_PATTERN            0x0188U
#define NV15_SCALED_ROP                0x018cU
#define NV15_SCALED_BETA               0x0190U
#define NV15_SCALED_BETA4              0x0194U
#define NV15_SCALED_SURFACE            0x0198U
#define NV15_SCALED_COLOR_CONVERSION   0x02fcU
#define NV15_SCALED_COLOR_FORMAT       0x0300U
#define NV15_SCALED_OPERATION          0x0304U
#define NV15_SCALED_CLIP_POINT         0x0308U
#define NV15_SCALED_CLIP_SIZE          0x030cU
#define NV15_SCALED_OUT_POINT          0x0310U
#define NV15_SCALED_OUT_SIZE           0x0314U
#define NV15_SCALED_DU_DX              0x0318U
#define NV15_SCALED_DV_DY              0x031cU
#define NV15_SCALED_SIZE               0x0400U
#define NV15_SCALED_FORMAT             0x0404U
#define NV15_SCALED_OFFSET             0x0408U
#define NV15_SCALED_POINT              0x040cU
#define NV15_SCALED_ORIGIN_MASK        0x00ff0000U
#define NV15_SCALED_ORIGIN_CORNER      0x00020000U
#define NV15_SCALED_FILTER_MASK        0xff000000U
#define NV15_SCALED_FILTER_BILINEAR    0x01000000U

#define NV15_M2MF_DMA_IN               0x0184U
#define NV15_M2MF_DMA_OUT              0x0188U
#define NV15_M2MF_OFFSET_IN            0x030cU
#define NV15_M2MF_OFFSET_OUT           0x0310U
#define NV15_M2MF_PITCH_IN             0x0314U
#define NV15_M2MF_PITCH_OUT            0x0318U
#define NV15_M2MF_LINE_LENGTH          0x031cU
#define NV15_M2MF_LINE_COUNT           0x0320U
#define NV15_M2MF_FORMAT               0x0324U
#define NV15_M2MF_NOTIFY               0x0328U

#define NV15_CLIP_POINT                0x0300U
#define NV15_CLIP_SIZE                 0x0304U

#define NV15_MAX_OBJECTS               128U
/* Yield between commands when either dispatch budget is exhausted. */
#define NV15_MAX_PUSH_WORDS            (1U << 16)
#define NV15_MAX_PUSH_WORK_BYTES       (16U * 1024U * 1024U)
#define NV15_MAX_2D_PIXELS             (16U * 1024U * 1024U)

#define NV15_CONTEXT_EXPLICIT_PATTERN  BIT(0)
#define NV15_CONTEXT_EXPLICIT_ROP      BIT(1)
#define NV15_CONTEXT_EXPLICIT_CLIP     BIT(2)

#define NV15_PMC_ENABLE_PFIFO          BIT(8)
#define NV15_PMC_ENABLE_PGRAPH         BIT(12)
#define NV15_PMC_ENABLE_PTIMER         BIT(16)
#define NV15_PMC_ENABLE_PFB            BIT(20)
#define NV15_PMC_ENABLE_PCRTC          BIT(24)

#define NV15_BOOT_0_VALUE              0x015000a4
#define NV15_AGP_CAP_OFFSET            0x44
#define NV15_PM_CAP_OFFSET             0x60
#define NV15_AGP_STATUS                0x1f000017
#define NV15_AGP_COMMAND_MASK          0xff000117

#define NV15_REG_WORDS(size)           ((size) / sizeof(uint32_t))
#define NV15_VGA_HPEL_NEUTRAL          8

typedef struct NV15DMAObject {
    uint64_t address;
    uint64_t length;
    uint32_t target;
    uint32_t class_id;
    bool valid;
} NV15DMAObject;

typedef struct NV15Surface {
    NV15DMAObject dma;
    uint64_t offset;
    uint32_t pitch;
    uint32_t format;
    unsigned int cpp;
} NV15Surface;

typedef struct NV15UploadCache {
    uint32_t key[11];
    NV15Surface destination;
    uint32_t width;
    uint32_t height;
    unsigned int cpp;
    bool valid;
} NV15UploadCache;

typedef struct NV15WorkBudget {
    uint64_t bytes;
    bool exhausted;
} NV15WorkBudget;

typedef struct NV15CursorParams {
    uint32_t offset;
    uint32_t config;
    unsigned int width;
    unsigned int height;
    unsigned int draw_height;
    unsigned int cpp;
    int x;
    int y;
    bool enabled;
    bool double_scan;
    bool bpp32;
    bool alpha_blend;
} NV15CursorParams;

typedef struct NV15ChannelContext {
    uint32_t rop;
    uint32_t color_format;
    uint32_t mono_format;
    uint32_t mono_shape;
    uint32_t pattern_select;
    uint32_t mono_color[2];
    uint32_t mono_bitmap[2];
} NV15ChannelContext;

typedef struct NV15GraphicsObject {
    uint32_t instance;
    uint32_t class_id;
    uint32_t dma_notify;
    uint32_t dma_source;
    uint32_t dma_dest;
    uint32_t surface;
    uint32_t pattern;
    uint32_t rop;
    uint32_t clip;
    uint32_t operation;
    uint32_t format;
    uint32_t pitch;
    uint32_t offset_source;
    uint32_t offset_dest;
    uint32_t color_format;
    uint32_t color;
    uint32_t point_in;
    uint32_t point_out;
    uint32_t size;
    uint32_t mono_format;
    uint32_t mono_shape;
    uint32_t pattern_select;
    uint32_t mono_color[2];
    uint32_t mono_bitmap[2];
    uint32_t upload_point;
    uint32_t upload_size_out;
    uint32_t upload_size_in;
    uint32_t upload_pixel;
    uint32_t m2mf_pitch_in;
    uint32_t m2mf_pitch_out;
    uint32_t m2mf_line_length;
    uint32_t m2mf_line_count;
    uint32_t m2mf_format;
    bool valid;
} NV15GraphicsObject;

typedef struct NV15Tile {
    uint32_t base;
    uint32_t limit;
    uint32_t pitch;
    unsigned int shift;
    bool enabled;
    bool valid;
} NV15Tile;

struct NVIDIAQuadro2State {
    PCIDevice parent_obj;
    VGACommonState vga;

    MemoryRegion mmio;
    MemoryRegion fb_aperture;
    PortioList vga_port_list;
    QEMUTimer vblank_timer;
    QEMUTimer ptimer_alarm_timer;
    bitbang_i2c_interface bbi2c;
    I2CDDCState i2cddc;

    uint32_t pmc[NV15_REG_WORDS(NV15_PMC_SIZE)];
    uint32_t pfifo[NV15_REG_WORDS(NV15_PFIFO_SIZE)];
    uint32_t ptimer[NV15_REG_WORDS(NV15_PTIMER_SIZE)];
    QEMUBH *fifo_bh;
    uint32_t fifo_pending_channels;
    uint32_t pfb[NV15_REG_WORDS(NV15_PFB_SIZE)];
    NV15Tile tiles[8];
    uint64_t tile_cached_page;
    int tile_cached_index;
    unsigned int tile_bank_shift;
    bool tile_bank_xor;
    bool tiles_valid;
    uint32_t pextdev[NV15_REG_WORDS(NV15_PEXTDEV_SIZE)];
    uint32_t pgraph[NV15_REG_WORDS(NV15_PGRAPH_SIZE)];
    uint32_t pcrtc[NV15_REG_WORDS(NV15_PCRTC_SIZE)];
    uint32_t pramdac[NV15_REG_WORDS(NV15_PRAMDAC_SIZE)];
    uint32_t subchannel_instance[NV15_USER_CHANNELS]
                                [NV15_USER_SUBCHANNELS];
    uint32_t active_surface[NV15_USER_CHANNELS];
    NV15ChannelContext channel_context[NV15_USER_CHANNELS];
    uint8_t notify_pending[NV15_USER_CHANNELS];
    uint8_t notify_type[NV15_USER_CHANNELS];
    uint8_t notify_subchannel[NV15_USER_CHANNELS];
    uint32_t notify_dma_instance[NV15_USER_CHANNELS];
    bool surface_explicit[NV15_MAX_OBJECTS];
    uint8_t context_explicit[NV15_MAX_OBJECTS];
    NV15GraphicsObject objects[NV15_MAX_OBJECTS];
    /* Host-derived slot + 1 indices; retain RAMHT and replacement order. */
    NV15UploadCache upload_cache;
    uint8_t object_hash[256];
    uint8_t object_next[NV15_MAX_OBJECTS];
    uint8_t bound_slot[NV15_USER_CHANNELS][NV15_USER_SUBCHANNELS];
    uint32_t rejected_methods;
    uint8_t active_channel;
    uint64_t ptimer_time_offset;
    uint64_t ptimer_time_snapshot;
    uint64_t ptimer_clock_snapshot;
    bool ptimer_alarm_after_match;
    bool ptimer_legacy_clock;
    uint64_t ptimer_multiplier;
    uint64_t ptimer_divisor;

    bool cursor_guest_mode;
    QEMUCursor *cursor;
    uint8_t cursor_image[NV15_CURSOR_MAX_BYTES];
    uint32_t cursor_image_offset;
    uint32_t cursor_image_config;
    size_t cursor_image_size;
    bool cursor_image_valid;
    bool cursor_host_visible;
    int cursor_host_x;
    int cursor_host_y;
    NV15CursorParams guest_cursor;
};

static void nv15_channel_context_reset(NV15ChannelContext *context)
{
    memset(context, 0, sizeof(*context));
    context->rop = 0xcc;
    context->pattern_select = 1;
    context->mono_color[1] = UINT32_MAX;
    context->mono_bitmap[0] = UINT32_MAX;
    context->mono_bitmap[1] = UINT32_MAX;
}

static void nv15_channel_contexts_reset(NVIDIAQuadro2State *s)
{
    unsigned int channel;

    for (channel = 0; channel < NV15_USER_CHANNELS; channel++) {
        nv15_channel_context_reset(&s->channel_context[channel]);
    }
}

static bool nv15_vbe_enabled(const VGACommonState *vga)
{
    return vga->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED;
}

static bool nv15_native_mode(const VGACommonState *vga)
{
    return !nv15_vbe_enabled(vga) && (vga->cr[0x28] & 3);
}

static int nv15_get_bpp(VGACommonState *vga)
{
    NVIDIAQuadro2State *s = container_of(vga, NVIDIAQuadro2State, vga);

    if (nv15_vbe_enabled(vga)) {
        return vga->vbe_regs[VBE_DISPI_INDEX_BPP];
    }

    switch (vga->cr[0x28] & 3) {
    case 1:
        return 8;
    case 2:
        return s->pramdac[(NV15_PRAMDAC_GENERAL_CONTROL -
                           NV15_PRAMDAC_BASE) >> 2] & BIT(12) ? 16 : 15;
    case 3:
        return 32;
    default:
        return 0;
    }
}

static void nv15_get_params(VGACommonState *vga, VGADisplayParams *params)
{
    NVIDIAQuadro2State *s = container_of(vga, NVIDIAQuadro2State, vga);

    if (nv15_vbe_enabled(vga)) {
        params->line_offset = vga->vbe_line_offset;
        params->start_addr = vga->vbe_start_addr;
        params->line_compare = UINT16_MAX;
        params->hpel = NV15_VGA_HPEL_NEUTRAL;
        params->hpel_split = false;
        return;
    }

    if (nv15_native_mode(vga)) {
        uint32_t pitch_units = vga->cr[VGA_CRTC_OFFSET] |
                               ((vga->cr[0x19] & 0xe0) << 3) |
                               ((vga->cr[0x42] & 0x40) << 5);
        uint32_t start = s->pcrtc[(NV15_PCRTC_START -
                                   NV15_PCRTC_BASE) >> 2] & ~3U;

        params->line_offset = pitch_units << 3;
        params->start_addr = start >> 2;
        params->line_compare = UINT16_MAX;
        params->hpel = NV15_VGA_HPEL_NEUTRAL;
        params->hpel_split = false;
        return;
    }

    params->line_offset = vga->cr[VGA_CRTC_OFFSET] << 3;
    params->start_addr = vga->cr[VGA_CRTC_START_LO] |
                         (vga->cr[VGA_CRTC_START_HI] << 8);
    params->line_compare = vga->cr[VGA_CRTC_LINE_COMPARE] |
                           ((vga->cr[VGA_CRTC_OVERFLOW] & 0x10) << 4) |
                           ((vga->cr[VGA_CRTC_MAX_SCAN] & 0x40) << 3);
    params->hpel = vga->ar[VGA_ATC_PEL];
    params->hpel_split = vga->ar[VGA_ATC_MODE] & 0x20;
}

static void nv15_get_resolution(VGACommonState *vga, int *pwidth,
                                int *pheight)
{
    uint32_t width;
    uint32_t height;

    if (nv15_vbe_enabled(vga)) {
        *pwidth = vga->vbe_regs[VBE_DISPI_INDEX_XRES];
        *pheight = vga->vbe_regs[VBE_DISPI_INDEX_YRES];
        return;
    }

    width = vga->cr[VGA_CRTC_H_DISP] |
            ((vga->cr[0x2d] & BIT(1)) << 7);
    height = vga->cr[VGA_CRTC_V_DISP_END] |
             ((vga->cr[VGA_CRTC_OVERFLOW] & BIT(1)) << 7) |
             ((vga->cr[VGA_CRTC_OVERFLOW] & BIT(6)) << 3);
    if (nv15_native_mode(vga)) {
        height |= (vga->cr[0x25] & BIT(1)) << 9;
        height |= (vga->cr[0x41] & BIT(2)) << 9;
    }

    *pwidth = MIN((width + 1) * 8, VBE_DISPI_MAX_XRES);
    *pheight = MIN(height + 1, VBE_DISPI_MAX_YRES);
}

static bool nv15_cursor_get_params(NVIDIAQuadro2State *s,
                                   NV15CursorParams *params)
{
    uint32_t config = s->pcrtc[(NV15_PCRTC_CURSOR_CONFIG -
                                NV15_PCRTC_BASE) >> 2];
    uint8_t addr0 = s->vga.cr[NV15_CURSOR_CRTC_ADDR0];
    uint8_t addr1 = s->vga.cr[NV15_CURSOR_CRTC_ADDR1];
    unsigned int lines = (config & NV15_CURSOR_CONFIG_LINES_MASK) >> 24;
    uint64_t bytes;

    memset(params, 0, sizeof(*params));
    if ((lines != 2 && lines != 4) ||
        !(config & NV15_CURSOR_CONFIG_PNVM) ||
        !(addr0 & NV15_CURSOR_ADDR0_PNVM)) {
        return false;
    }

    params->config = config;
    params->width = config & NV15_CURSOR_CONFIG_64_PIXELS ? 64 : 32;
    params->height = lines * 16;
    params->double_scan = (config & NV15_CURSOR_CONFIG_DOUBLE_SCAN) &&
                          (addr1 & NV15_CURSOR_DOUBLE_SCAN);
    params->draw_height = params->height << params->double_scan;
    params->bpp32 = config & NV15_CURSOR_CONFIG_32BPP;
    params->alpha_blend = config & NV15_CURSOR_CONFIG_ALPHA_BLEND;
    params->cpp = params->bpp32 ? 4 : 2;
    params->enabled = addr1 & NV15_CURSOR_ENABLE;
    params->offset = ((uint32_t)s->vga.cr[NV15_CURSOR_CRTC_ADDR2] << 24) |
                     ((uint32_t)(addr0 & 0x7f) << 17) |
                     ((uint32_t)(addr1 & 0xfc) << 9);
    params->x = (int16_t)s->pramdac[(NV15_PRAMDAC_CURSOR_POS -
                                     NV15_PRAMDAC_BASE) >> 2];
    params->y = (int16_t)(s->pramdac[(NV15_PRAMDAC_CURSOR_POS -
                                      NV15_PRAMDAC_BASE) >> 2] >> 16);

    bytes = (uint64_t)params->width * params->height * params->cpp;
    return bytes <= s->vga.vram_size &&
           params->offset <= s->vga.vram_size - bytes;
}

static uint8_t nv15_cursor_unpremultiply(uint8_t color, uint8_t alpha)
{
    if (!alpha) {
        return 0;
    }
    return MIN(255U, ((unsigned int)color * 255 + alpha / 2) / alpha);
}

static uint32_t nv15_cursor_host_pixel(const NV15CursorParams *params,
                                       const uint8_t *source)
{
    uint8_t alpha;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (params->bpp32) {
        uint32_t pixel = ldl_le_p(source);

        alpha = pixel >> 24;
        red = pixel >> 16;
        green = pixel >> 8;
        blue = pixel;
        if (!params->alpha_blend) {
            red = nv15_cursor_unpremultiply(red, alpha);
            green = nv15_cursor_unpremultiply(green, alpha);
            blue = nv15_cursor_unpremultiply(blue, alpha);
        }
    } else {
        uint16_t pixel = lduw_le_p(source);

        alpha = pixel & BIT(15) ? 0xff : 0;
        red = (pixel >> 10) & 0x1f;
        green = (pixel >> 5) & 0x1f;
        blue = pixel & 0x1f;
        red = (red << 3) | (red >> 2);
        green = (green << 3) | (green >> 2);
        blue = (blue << 3) | (blue >> 2);
    }

    /* QEMUCursor is byte-ordered RGBA, unlike NV15's little-endian A8R8G8B8. */
    return ((uint32_t)alpha << 24) | ((uint32_t)blue << 16) |
           ((uint32_t)green << 8) | red;
}

static void nv15_cursor_update_host_dirty(NVIDIAQuadro2State *s,
                                           bool check_image)
{
    NV15CursorParams params;
    const uint8_t *source;
    size_t image_size;
    bool shape_changed;
    unsigned int x;
    unsigned int y;

    if (s->cursor_guest_mode) {
        bool enabled = nv15_cursor_get_params(s, &params) && params.enabled;

        if (s->vga.force_shadow != enabled) {
            s->vga.force_shadow = enabled;
            s->vga.graphic_mode = -1;
            graphic_hw_invalidate(s->vga.con);
        }
        return;
    }
    if (!nv15_cursor_get_params(s, &params) || !params.enabled) {
        if (s->cursor_host_visible) {
            dpy_mouse_set(s->vga.con, 0, 0, false);
            s->cursor_host_visible = false;
        }
        return;
    }

    image_size = (size_t)params.width * params.height * params.cpp;
    source = s->vga.vram_ptr + params.offset;
    shape_changed = !s->cursor_image_valid ||
                    s->cursor_image_offset != params.offset ||
                    s->cursor_image_config != params.config ||
                    s->cursor_image_size != image_size ||
                    (check_image &&
                     memcmp(s->cursor_image, source, image_size));
    if (shape_changed) {
        if (!s->cursor || s->cursor->width != params.width ||
            s->cursor->height != params.draw_height) {
            cursor_unref(s->cursor);
            s->cursor = cursor_alloc(params.width, params.draw_height);
            assert(s->cursor != NULL);
        }
        for (y = 0; y < params.draw_height; y++) {
            unsigned int source_y = params.double_scan ? y / 2 : y;

            for (x = 0; x < params.width; x++) {
                size_t source_offset =
                    ((size_t)source_y * params.width + x) * params.cpp;

                s->cursor->data[(size_t)y * params.width + x] =
                    nv15_cursor_host_pixel(&params, source + source_offset);
            }
        }
        memcpy(s->cursor_image, source, image_size);
        s->cursor_image_offset = params.offset;
        s->cursor_image_config = params.config;
        s->cursor_image_size = image_size;
        s->cursor_image_valid = true;
        dpy_cursor_define(s->vga.con, s->cursor);
    }

    if (!s->cursor_host_visible || s->cursor_host_x != params.x ||
        s->cursor_host_y != params.y) {
        dpy_mouse_set(s->vga.con, params.x, params.y, true);
        s->cursor_host_visible = true;
        s->cursor_host_x = params.x;
        s->cursor_host_y = params.y;
    }
}

static void nv15_cursor_update_host(NVIDIAQuadro2State *s)
{
    nv15_cursor_update_host_dirty(s, true);
}

static void nv15_cursor_invalidate_range(VGACommonState *vga, int y,
                                         unsigned int height)
{
    int first = MAX(y, 0);
    int last = MIN(y + (int)height, VGA_MAX_HEIGHT);

    if (first < last) {
        vga_invalidate_scanlines(vga, first, last);
    }
}

static void nv15_cursor_invalidate(VGACommonState *vga)
{
    NVIDIAQuadro2State *s = container_of(vga, NVIDIAQuadro2State, vga);
    NV15CursorParams params;
    bool valid = nv15_cursor_get_params(s, &params) && params.enabled;
    size_t image_size = valid ? params.width * params.height * params.cpp : 0;
    const uint8_t *source = valid ? vga->vram_ptr + params.offset : NULL;

    if (valid && s->guest_cursor.enabled && s->cursor_image_valid &&
        s->guest_cursor.offset == params.offset &&
        s->guest_cursor.config == params.config &&
        s->guest_cursor.x == params.x && s->guest_cursor.y == params.y &&
        s->guest_cursor.draw_height == params.draw_height &&
        s->cursor_image_size == image_size &&
        !memcmp(s->cursor_image, source, image_size)) {
        return;
    }

    if (s->guest_cursor.enabled) {
        nv15_cursor_invalidate_range(vga, s->guest_cursor.y,
                                     s->guest_cursor.draw_height);
    }
    if (!valid) {
        memset(&s->guest_cursor, 0, sizeof(s->guest_cursor));
        s->cursor_image_valid = false;
        return;
    }

    memcpy(s->cursor_image, source, image_size);
    s->cursor_image_size = image_size;
    s->cursor_image_valid = true;
    s->guest_cursor = params;
    nv15_cursor_invalidate_range(vga, params.y, params.draw_height);
}

static uint8_t nv15_cursor_blend_channel(uint8_t source, uint8_t destination,
                                         uint8_t alpha, bool alpha_blend)
{
    unsigned int value = destination * (255 - alpha);

    value += alpha_blend ? source * alpha : source * 255;
    return MIN(255U, (value + 127) / 255);
}

static void nv15_cursor_draw_line(VGACommonState *vga, uint8_t *line,
                                  int screen_y)
{
    NVIDIAQuadro2State *s = container_of(vga, NVIDIAQuadro2State, vga);
    const NV15CursorParams *params = &s->guest_cursor;
    uint32_t *destination = (uint32_t *)line;
    int first_x;
    int last_x;
    unsigned int source_y;
    int screen_x;

    if (!params->enabled || screen_y < params->y ||
        screen_y >= params->y + (int)params->draw_height) {
        return;
    }
    first_x = MAX(params->x, 0);
    last_x = MIN(params->x + (int)params->width, vga->last_scr_width);
    if (first_x >= last_x) {
        return;
    }
    source_y = screen_y - params->y;
    if (params->double_scan) {
        source_y /= 2;
    }

    for (screen_x = first_x; screen_x < last_x; screen_x++) {
        unsigned int source_x = screen_x - params->x;
        size_t offset = params->offset +
                        ((size_t)source_y * params->width + source_x) *
                        params->cpp;

        if (params->bpp32) {
            uint32_t source = ldl_le_p(s->vga.vram_ptr + offset);
            uint8_t alpha = source >> 24;
            uint32_t old = destination[screen_x];
            uint8_t red = nv15_cursor_blend_channel(
                source >> 16, old >> 16, alpha, params->alpha_blend);
            uint8_t green = nv15_cursor_blend_channel(
                source >> 8, old >> 8, alpha, params->alpha_blend);
            uint8_t blue = nv15_cursor_blend_channel(
                source, old, alpha, params->alpha_blend);

            destination[screen_x] = rgb_to_pixel32(red, green, blue);
        } else {
            uint16_t source = lduw_le_p(s->vga.vram_ptr + offset);

            if (source & BIT(15)) {
                uint8_t red = (source >> 10) & 0x1f;
                uint8_t green = (source >> 5) & 0x1f;
                uint8_t blue = source & 0x1f;

                destination[screen_x] = rgb_to_pixel32(
                    (red << 3) | (red >> 2),
                    (green << 3) | (green >> 2),
                    (blue << 3) | (blue >> 2));
            }
        }
    }
}

static void nv15_graphic_invalidate(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;

    s->vga.hw_ops->invalidate(&s->vga);
}

/* Packed rendering records VRAM damage without invalidating the mode. */
static void nv15_graphic_damage(NVIDIAQuadro2State *s)
{
    if (nv15_get_bpp(&s->vga) < 8 ||
        ((s->vga.gr[VGA_GFX_MODE] >> 5) & 3) < 2) {
        graphic_hw_invalidate(s->vga.con);
    }
}

/* NV10/NV15 PFB tiles, following envytools nvhw/tile.c. */
static bool nv15_tile_pitch(uint32_t pitch, unsigned int *shift,
                            unsigned int *factor)
{
    unsigned int units = pitch >> 8;

    if ((pitch & ~0xff00U) || !units || (units & 1)) {
        return false;
    }
    *shift = ctz32(units);
    units >>= *shift;
    if (units != 1 && units != 3 && units != 5 && units != 7) {
        return false;
    }
    *factor = units >> 1;
    return true;
}

static void nv15_tiles_prepare(NVIDIAQuadro2State *s)
{
    uint32_t cfg = s->pfb[(NV15_PFB_CFG0 - NV15_PFB_BASE) >> 2];
    unsigned int parts = (cfg & 0x30) == 0x10 ? 2 :
                         (cfg & 0x30) == 0x20 ? 0 : 1;
    unsigned int columns = extract32(cfg, 24, 4);

    /* Unspecified geometry defaults to nine column bits. */
    columns = columns ? MIN(columns, 9) : 9;
    s->tile_bank_shift = 2 + parts + columns;
    s->tile_bank_xor = 2 + parts + (cfg & 1) > 4;
    for (unsigned int i = 0; i < 8; i++) {
        const uint32_t *regs = &s->pfb[(0x240 + i * 16) >> 2];
        NV15Tile *tile = &s->tiles[i];
        unsigned int factor;

        tile->base = regs[0] & 0x07ffc000;
        tile->limit = (regs[1] & 0x07ffc000) | 0x3fff;
        tile->pitch = regs[2] & 0xff00;
        tile->enabled = regs[0] & BIT(31);
        tile->valid = s->tile_bank_shift >= 8 &&
                      nv15_tile_pitch(tile->pitch, &tile->shift, &factor);
    }
    s->tile_cached_page = UINT64_MAX;
    s->tiles_valid = true;
}

static uint64_t nv15_tile_address(NVIDIAQuadro2State *s, uint64_t address)
{
    const NV15Tile *tile;
    unsigned int bank_shift;
    uint64_t offset, x, y, block, inside;

    if (!s->tiles_valid) {
        nv15_tiles_prepare(s);
    }
    /* All descriptor boundaries are aligned to 16 KiB. */
    if (s->tile_cached_page != address >> 14) {
        s->tile_cached_page = address >> 14;
        s->tile_cached_index = -1;
        for (unsigned int i = 0; i < ARRAY_SIZE(s->tiles); i++) {
            tile = &s->tiles[i];
            if (tile->enabled && address >= tile->base &&
                address <= tile->limit) {
                s->tile_cached_index = i;
                break;
            }
        }
    }
    if (s->tile_cached_index < 0) {
        return address;
    }
    tile = &s->tiles[s->tile_cached_index];
    if (!tile->valid) {
        return UINT64_MAX;
    }
    bank_shift = s->tile_bank_shift;
    offset = address - tile->base;
    x = offset % tile->pitch;
    y = (offset / tile->pitch) >> (bank_shift - 8);
    block = y * (tile->pitch >> 8) + (x >> 8);
    inside = (x & 0xff) |
             (((offset >> (tile->shift + 8)) &
               ((1U << (bank_shift - 8)) - 1)) << 8);
    block ^= y & 1;
    if (s->tile_bank_xor && (offset & 0x100)) {
        inside ^= 0x10;
    }
    return tile->base + (block << bank_shift) + inside;
}

static uint64_t nv15_scanout_map(VGACommonState *vga, uint32_t address,
                                 uint32_t *length)
{
    NVIDIAQuadro2State *s = container_of(vga, NVIDIAQuadro2State, vga);

    *length = MIN(*length, 16 - (address & 15));
    return nv15_tile_address(s, address);
}

static bool nv15_has_tiles(NVIDIAQuadro2State *s)
{
    for (unsigned int i = 0; i < 8; i++) {
        if (s->pfb[(0x240 + i * 16) >> 2] & BIT(31)) {
            return true;
        }
    }
    return false;
}

static bool nv15_graphic_update(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;
    bool complete;

    s->vga.scanout_map = nv15_native_mode(&s->vga) && nv15_has_tiles(s) &&
                          ((s->vga.gr[VGA_GFX_MODE] >> 5) & 3) >= 2 ?
                         nv15_scanout_map : NULL;
    s->vga.cursor_dirty_size = !s->cursor_guest_mode && s->cursor_image_valid ?
                              s->cursor_image_size : 0;
    s->vga.cursor_dirty_offset = s->cursor_image_offset;
    s->vga.cursor_dirty_valid = false;
    complete = s->vga.hw_ops->gfx_update(&s->vga);
    s->vga.scanout_map = NULL;
    nv15_cursor_update_host_dirty(s, !s->vga.cursor_dirty_valid ||
                                    s->vga.cursor_image_dirty);
    return complete;
}

static void nv15_graphic_text_update(void *opaque, uint32_t *text)
{
    NVIDIAQuadro2State *s = opaque;

    s->vga.hw_ops->text_update(&s->vga, text);
}

static const GraphicHwOps nv15_graphic_ops = {
    .invalidate = nv15_graphic_invalidate,
    .gfx_update = nv15_graphic_update,
    .text_update = nv15_graphic_text_update,
};

static bool nv15_in_range(hwaddr addr, hwaddr base, hwaddr size)
{
    return addr >= base && addr < base + size;
}

static uint32_t *nv15_shadow_word(NVIDIAQuadro2State *s, hwaddr addr)
{
    if (nv15_in_range(addr, NV15_PMC_BASE, NV15_PMC_SIZE)) {
        return &s->pmc[(addr - NV15_PMC_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PFIFO_BASE, NV15_PFIFO_SIZE)) {
        return &s->pfifo[(addr - NV15_PFIFO_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PTIMER_BASE, NV15_PTIMER_SIZE)) {
        return &s->ptimer[(addr - NV15_PTIMER_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PFB_BASE, NV15_PFB_SIZE)) {
        return &s->pfb[(addr - NV15_PFB_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PEXTDEV_BASE, NV15_PEXTDEV_SIZE)) {
        return &s->pextdev[(addr - NV15_PEXTDEV_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PGRAPH_BASE, NV15_PGRAPH_SIZE)) {
        return &s->pgraph[(addr - NV15_PGRAPH_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PCRTC_BASE, NV15_PCRTC_SIZE)) {
        return &s->pcrtc[(addr - NV15_PCRTC_BASE) >> 2];
    }
    if (nv15_in_range(addr, NV15_PRAMDAC_BASE, NV15_PRAMDAC_SIZE)) {
        return &s->pramdac[(addr - NV15_PRAMDAC_BASE) >> 2];
    }
    return NULL;
}

static uint64_t nv15_muldiv64_full(uint64_t value, uint64_t multiplier,
                                   uint64_t divisor, uint64_t *remainder)
{
    uint64_t low;
    uint64_t high;
    uint64_t rem;

    mulu64(&low, &high, value, multiplier);
    rem = divu128(&low, &high, divisor);
    if (remainder) {
        *remainder = rem;
    }
    return low;
}

static uint64_t nv15_muldiv64(uint64_t value, uint64_t multiplier,
                              uint64_t divisor)
{
    return nv15_muldiv64_full(value, multiplier, divisor, NULL);
}

static bool nv15_ptimer_alarm_delay(uint64_t delta, uint64_t multiplier,
                                    uint64_t divisor, uint64_t phase,
                                    uint64_t *delay)
{
    uint64_t low;
    uint64_t high;
    uint64_t remainder;

    if (!delta) {
        *delay = 0;
        return true;
    }
    mulu64(&low, &high, delta, divisor);
    if (low < phase) {
        high--;
    }
    low -= phase;
    remainder = divu128(&low, &high, multiplier);
    if (high || (remainder && low == UINT64_MAX)) {
        return false;
    }
    *delay = low + !!remainder;
    return true;
}

static bool nv15_ptimer_compute_rate(NVIDIAQuadro2State *s,
                                     uint64_t *multiplier,
                              uint64_t *divisor)
{
    uint32_t coeff =
        s->pramdac[(NV15_PRAMDAC_NVPLL_COEFF - NV15_PRAMDAC_BASE) >> 2];
    uint32_t m = extract32(coeff, 0, 8);
    uint32_t n = extract32(coeff, 8, 8);
    uint32_t p = extract32(coeff, 16, 3);
    uint32_t numerator =
        s->ptimer[(NV15_PTIMER_NUMERATOR - NV15_PTIMER_BASE) >> 2] &
        UINT16_MAX;
    uint32_t denominator =
        s->ptimer[(NV15_PTIMER_DENOMINATOR - NV15_PTIMER_BASE) >> 2] &
        UINT16_MAX;
    uint64_t nvclk;

    if (s->ptimer_legacy_clock) {
        *multiplier = 1;
        *divisor = 1;
        return true;
    }
    if (!m || !n || !numerator || !denominator) {
        return false;
    }
    nvclk = ((uint64_t)NV15_XTAL_HZ * n / m) >> p;
    *multiplier = nvclk * denominator;
    *divisor = (uint64_t)NANOSECONDS_PER_SECOND * numerator;
    return *multiplier != 0;
}

static void nv15_ptimer_update_rate(NVIDIAQuadro2State *s)
{
    s->ptimer_multiplier = 0;
    s->ptimer_divisor = 0;
    if (!nv15_ptimer_compute_rate(s, &s->ptimer_multiplier,
                                   &s->ptimer_divisor)) {
        s->ptimer_multiplier = 0;
    }
}

static bool nv15_ptimer_rate(NVIDIAQuadro2State *s, uint64_t *multiplier,
                              uint64_t *divisor)
{
    *multiplier = s->ptimer_multiplier;
    *divisor = s->ptimer_divisor;
    return *multiplier != 0;
}

static bool nv15_ptimer_running(NVIDIAQuadro2State *s)
{
    uint64_t multiplier;
    uint64_t divisor;

    return nv15_ptimer_rate(s, &multiplier, &divisor);
}

static uint64_t nv15_ptimer_scaled_clock_at(NVIDIAQuadro2State *s,
                                             uint64_t now)
{
    uint64_t multiplier;
    uint64_t divisor;

    if (!nv15_ptimer_rate(s, &multiplier, &divisor)) {
        return 0;
    }
    return nv15_muldiv64(now, multiplier, divisor);
}

static uint64_t nv15_timer_value_at(NVIDIAQuadro2State *s, uint64_t now)
{
    return (nv15_ptimer_scaled_clock_at(s, now) +
            s->ptimer_time_offset) & NV15_PTIMER_TIME_MASK;
}

static uint64_t nv15_timer_value(NVIDIAQuadro2State *s)
{
    return nv15_timer_value_at(s,
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static uint32_t nv15_pending_sources(NVIDIAQuadro2State *s)
{
    uint32_t pending = s->pmc[(NV15_PMC_INTR_0 - NV15_PMC_BASE) >> 2];

    if (s->pfifo[(NV15_PFIFO_INTR_0 - NV15_PFIFO_BASE) >> 2] &
        s->pfifo[(NV15_PFIFO_INTR_EN_0 - NV15_PFIFO_BASE) >> 2]) {
        pending |= NV15_PMC_INTR_PFIFO;
    }
    if (s->pgraph[(NV15_PGRAPH_INTR - NV15_PGRAPH_BASE) >> 2] &
        s->pgraph[(NV15_PGRAPH_INTR_EN - NV15_PGRAPH_BASE) >> 2]) {
        pending |= NV15_PMC_INTR_PGRAPH;
    }
    if (s->ptimer[(NV15_PTIMER_INTR_0 - NV15_PTIMER_BASE) >> 2] &
        s->ptimer[(NV15_PTIMER_INTR_EN_0 - NV15_PTIMER_BASE) >> 2]) {
        pending |= NV15_PMC_INTR_PTIMER;
    }
    if (s->pcrtc[(NV15_PCRTC_INTR_0 - NV15_PCRTC_BASE) >> 2] &
        s->pcrtc[(NV15_PCRTC_INTR_EN_0 - NV15_PCRTC_BASE) >> 2]) {
        pending |= NV15_PMC_INTR_PCRTC;
    }
    return pending;
}

static void nv15_update_irq(NVIDIAQuadro2State *s)
{
    bool enabled = s->pmc[(NV15_PMC_INTR_EN_0 - NV15_PMC_BASE) >> 2] &
                   NV15_PMC_INTR_MASTER_ENABLE;

    pci_set_irq(&s->parent_obj, enabled && nv15_pending_sources(s));
}

static void nv15_ptimer_schedule_alarm(NVIDIAQuadro2State *s,
                                       bool after_match)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t multiplier;
    uint64_t divisor;
    uint64_t scaled;
    uint64_t current;
    uint64_t target;
    uint64_t delta;
    uint64_t phase;
    uint64_t delay;
    uint64_t deadline;

    s->ptimer_alarm_after_match = after_match;
    if (!nv15_ptimer_rate(s, &multiplier, &divisor)) {
        timer_del(&s->ptimer_alarm_timer);
        return;
    }
    scaled = nv15_muldiv64_full(now, multiplier, divisor, &phase);
    current = (scaled + s->ptimer_time_offset) & NV15_PTIMER_LOW_MASK;
    target =
        s->ptimer[(NV15_PTIMER_ALARM_0 - NV15_PTIMER_BASE) >> 2] >> 5;
    delta = (target - current) & NV15_PTIMER_LOW_MASK;

    if (after_match && !delta) {
        delta = NV15_PTIMER_LOW_PERIOD;
    }
    if (!nv15_ptimer_alarm_delay(delta, multiplier, divisor, phase, &delay) ||
        delay > INT64_MAX - now) {
        deadline = INT64_MAX;
    } else {
        deadline = now + delay;
    }
    timer_mod_ns(&s->ptimer_alarm_timer, deadline);
}

static void nv15_ptimer_alarm(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;

    s->ptimer[(NV15_PTIMER_INTR_0 - NV15_PTIMER_BASE) >> 2] |=
        NV15_PTIMER_INTR_ALARM;
    nv15_update_irq(s);
    nv15_ptimer_schedule_alarm(s, true);
}

static void nv15_vblank_irq(void *opaque);

static void nv15_update_vblank_timer(NVIDIAQuadro2State *s)
{
    bool enabled = s->pcrtc[(NV15_PCRTC_INTR_EN_0 -
                             NV15_PCRTC_BASE) >> 2] &
                   NV15_PCRTC_INTR_VBLANK;

    if (enabled) {
        if (!timer_pending(&s->vblank_timer)) {
            nv15_vblank_irq(s);
        }
    } else {
        timer_del(&s->vblank_timer);
    }
}

static void nv15_vblank_irq(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;

    if (!(s->pcrtc[(NV15_PCRTC_INTR_EN_0 - NV15_PCRTC_BASE) >> 2] &
          NV15_PCRTC_INTR_VBLANK)) {
        return;
    }

    s->pcrtc[(NV15_PCRTC_INTR_0 - NV15_PCRTC_BASE) >> 2] |=
        NV15_PCRTC_INTR_VBLANK;
    nv15_update_irq(s);
    /* TODO: Derive vblank edges from the programmed CRTC totals and VPLL. */
    timer_mod(&s->vblank_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              NANOSECONDS_PER_SECOND / 60);
}

static void nv15_ddc_write(NVIDIAQuadro2State *s, uint8_t index,
                           uint8_t value)
{
    bool clock;
    bool data;
    uint8_t status_index;

    if (index != 0x37 && index != 0x3f) {
        return;
    }

    /*
     * NV04-family DDC GPIO uses bit 0 as output enable, bits 5/4 as
     * SCL/SDA output, and bits 2/3 of the preceding register as input.
     */
    clock = !(value & BIT(0)) || (value & BIT(5));
    data = !(value & BIT(0)) || (value & BIT(4));
    bitbang_i2c_set(&s->bbi2c, BITBANG_I2C_SCL, clock);
    data = bitbang_i2c_set(&s->bbi2c, BITBANG_I2C_SDA, data);

    status_index = index - 1;
    s->vga.cr[status_index] &= ~(BIT(2) | BIT(3));
    s->vga.cr[status_index] |= (clock ? BIT(2) : 0) |
                               (data ? BIT(3) : 0);
}

static uint32_t nv15_vga_ioport_read(void *opaque, uint32_t addr)
{
    NVIDIAQuadro2State *s = opaque;

    return vga_ioport_read(&s->vga, addr);
}

static void nv15_vga_ioport_write(void *opaque, uint32_t addr, uint32_t value)
{
    NVIDIAQuadro2State *s = opaque;
    bool crtc_data = addr == VGA_CRT_DM || addr == VGA_CRT_DC;
    uint8_t index = s->vga.cr_index;
    uint8_t old = s->vga.cr[index];

    vga_ioport_write(&s->vga, addr, value);
    if (crtc_data) {
        nv15_ddc_write(s, index, value);
        if (s->vga.cr[index] == old) {
            return;
        }
        if (index == NV15_CURSOR_CRTC_ADDR2 ||
            index == NV15_CURSOR_CRTC_ADDR0 ||
            index == NV15_CURSOR_CRTC_ADDR1) {
            s->cursor_image_valid = false;
            nv15_cursor_update_host(s);
        }
        if (index == VGA_CRTC_H_DISP || index == VGA_CRTC_V_DISP_END ||
            index == VGA_CRTC_OFFSET || index == 0x19 || index == 0x25 ||
            index == 0x28 || index == 0x2d || index == 0x41 ||
            index == 0x42) {
            s->vga.graphic_mode = -1;
            graphic_hw_invalidate(s->vga.con);
        }
    }
}

static const MemoryRegionPortio nv15_vga_portio_list[] = {
    { 0x04,  2, 1, .read = nv15_vga_ioport_read,
                    .write = nv15_vga_ioport_write }, /* 3b4 */
    { 0x0a,  1, 1, .read = nv15_vga_ioport_read,
                    .write = nv15_vga_ioport_write }, /* 3ba */
    { 0x10, 16, 1, .read = nv15_vga_ioport_read,
                    .write = nv15_vga_ioport_write }, /* 3c0 */
    { 0x24,  2, 1, .read = nv15_vga_ioport_read,
                    .write = nv15_vga_ioport_write }, /* 3d4 */
    { 0x2a,  1, 1, .read = nv15_vga_ioport_read,
                    .write = nv15_vga_ioport_write }, /* 3da */
    PORTIO_END_OF_LIST(),
};

static bool nv15_is_vga_alias(hwaddr addr)
{
    return nv15_in_range(addr, NV15_PRMVIO_BASE, NV15_PRMVIO_SIZE) ||
           nv15_in_range(addr, NV15_PRMCIO_BASE, NV15_PRMCIO_SIZE) ||
           nv15_in_range(addr, NV15_PRMDIO_BASE, NV15_PRMDIO_SIZE);
}

static uint16_t nv15_vga_alias_port(hwaddr addr)
{
    if (nv15_in_range(addr, NV15_PRMVIO_BASE, NV15_PRMVIO_SIZE)) {
        return addr - NV15_PRMVIO_BASE;
    }
    if (nv15_in_range(addr, NV15_PRMCIO_BASE, NV15_PRMCIO_SIZE)) {
        return addr - NV15_PRMCIO_BASE;
    }
    return addr - NV15_PRMDIO_BASE;
}

static hwaddr nv15_pramin_to_vram(NVIDIAQuadro2State *s, hwaddr addr)
{
    hwaddr ramin = addr - NV15_PRAMIN_BASE;
    hwaddr block = ramin & ~UINT64_C(0xf);

    /* NV04/NV10 RAMIN reverses 16-byte blocks at the end of VRAM. */
    return s->vga.vram_size - block - 16 + (ramin & 0xf);
}

static uint64_t nv15_pramin_read(NVIDIAQuadro2State *s, hwaddr addr,
                                 unsigned int size)
{
    uint8_t bytes[8] = { 0 };

    assert(size <= sizeof(bytes));
    for (unsigned int i = 0; i < size; ) {
        hwaddr vram = nv15_pramin_to_vram(s, addr + i);
        unsigned int count = MIN(size - i, 16 - ((addr + i) & 15));

        memcpy(bytes + i, s->vga.vram_ptr + vram, count);
        i += count;
    }
    return ldq_le_p(bytes);
}

static void nv15_pramin_write(NVIDIAQuadro2State *s, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    uint8_t bytes[8];

    assert(size <= sizeof(bytes));
    stq_le_p(bytes, value);
    for (unsigned int i = 0; i < size; ) {
        hwaddr vram = nv15_pramin_to_vram(s, addr + i);
        unsigned int count = MIN(size - i, 16 - ((addr + i) & 15));

        memcpy(s->vga.vram_ptr + vram, bytes + i, count);
        memory_region_set_dirty(&s->vga.vram, vram, count);
        i += count;
    }
}

static bool nv15_ramin_range_valid(uint32_t offset, uint32_t size)
{
    return offset <= NV15_RAMIN_SIZE && size <= NV15_RAMIN_SIZE - offset;
}

static uint32_t nv15_ramin_read32(NVIDIAQuadro2State *s, uint32_t offset)
{
    hwaddr vram;

    if (!nv15_ramin_range_valid(offset, sizeof(uint32_t))) {
        return 0;
    }
    /*
     * An aligned 32-bit access cannot cross one of RAMIN's reversed 16-byte
     * blocks.  Bytes within a block retain ascending order in VRAM.
     */
    if (QEMU_IS_ALIGNED(offset, sizeof(uint32_t))) {
        vram = nv15_pramin_to_vram(s, NV15_PRAMIN_BASE + offset);
        return ldl_le_p(s->vga.vram_ptr + vram);
    }
    return nv15_pramin_read(s, NV15_PRAMIN_BASE + offset,
                            sizeof(uint32_t));
}

static void nv15_ramin_write32(NVIDIAQuadro2State *s, uint32_t offset,
                               uint32_t value)
{
    hwaddr vram;

    if (!nv15_ramin_range_valid(offset, sizeof(uint32_t))) {
        return;
    }
    if (QEMU_IS_ALIGNED(offset, sizeof(uint32_t))) {
        vram = nv15_pramin_to_vram(s, NV15_PRAMIN_BASE + offset);
        stl_le_p(s->vga.vram_ptr + vram, value);
        memory_region_set_dirty(&s->vga.vram, vram, sizeof(value));
    } else {
        nv15_pramin_write(s, NV15_PRAMIN_BASE + offset, value,
                          sizeof(value));
    }
}

static bool nv15_ramht_lookup(NVIDIAQuadro2State *s, unsigned int channel,
                              uint32_t handle, uint32_t *instance,
                              uint32_t *engine)
{
    uint32_t ramht = s->pfifo[(NV15_PFIFO_RAMHT - NV15_PFIFO_BASE) >> 2];
    unsigned int bits = 9 + ((ramht >> 16) & 3);
    unsigned int entries = 1U << bits;
    unsigned int probes = 16U << ((ramht >> 24) & 3);
    uint32_t mask = entries - 1;
    uint32_t base = (ramht & 0x1f0U) << 8;
    uint32_t hash = 0;
    uint32_t key = handle;
    unsigned int i;

    while (key) {
        hash ^= key & mask;
        key >>= bits;
    }
    hash = (hash ^ (channel << (bits - 4))) & mask;

    probes = MIN(probes, entries);
    for (i = 0; i < probes; i++) {
        uint32_t slot = (hash + i) & mask;
        uint32_t offset = base + slot * 8;
        uint32_t context;

        if (!nv15_ramin_range_valid(offset, 8)) {
            return false;
        }
        context = nv15_ramin_read32(s, offset + 4);
        if (nv15_ramin_read32(s, offset) == handle &&
            (context & NV15_RAMHT_VALID) &&
            ((context >> NV15_RAMHT_CHANNEL_SHIFT) & 0x1f) == channel) {
            *instance = (context & NV15_RAMHT_INSTANCE_MASK) << 4;
            *engine = context & NV15_RAMHT_ENGINE_MASK;
            return nv15_ramin_range_valid(*instance, 16);
        }
    }
    return false;
}

static unsigned int nv15_object_hash(uint32_t instance)
{
    return ((instance >> 4) ^ (instance >> 12)) & 255;
}

static void nv15_object_index_remove(NVIDIAQuadro2State *s,
                                     NV15GraphicsObject *object)
{
    unsigned int slot = object - s->objects;
    uint8_t *link = &s->object_hash[nv15_object_hash(object->instance)];

    while (*link && *link != slot + 1) {
        link = &s->object_next[*link - 1];
    }
    if (*link) {
        *link = s->object_next[slot];
    }
}

static void nv15_object_index_insert(NVIDIAQuadro2State *s,
                                     NV15GraphicsObject *object)
{
    unsigned int slot = object - s->objects;
    uint8_t *link = &s->object_hash[nv15_object_hash(object->instance)];

    /* Duplicate entries use the lowest slot. */
    while (*link && *link < slot + 1) {
        link = &s->object_next[*link - 1];
    }
    s->object_next[slot] = *link;
    *link = slot + 1;
}

static void nv15_object_index_rebuild(NVIDIAQuadro2State *s)
{
    s->upload_cache.valid = false;
    memset(s->object_hash, 0, sizeof(s->object_hash));
    memset(s->object_next, 0, sizeof(s->object_next));
    memset(s->bound_slot, 0, sizeof(s->bound_slot));
    for (unsigned int i = 0; i < NV15_MAX_OBJECTS; i++) {
        if (s->objects[i].valid) {
            nv15_object_index_insert(s, &s->objects[i]);
        }
    }
}

static NV15GraphicsObject *nv15_find_object(NVIDIAQuadro2State *s,
                                             uint32_t instance)
{
    unsigned int slot = s->object_hash[nv15_object_hash(instance)];

    while (slot) {
        NV15GraphicsObject *object = &s->objects[slot - 1];

        if (object->instance == instance) {
            return object;
        }
        slot = s->object_next[slot - 1];
    }
    return NULL;
}

static NV15GraphicsObject *nv15_bound_object(NVIDIAQuadro2State *s,
                                            unsigned int channel,
                                            unsigned int subchannel)
{
    uint32_t instance = s->subchannel_instance[channel][subchannel];
    unsigned int slot = s->bound_slot[channel][subchannel];
    NV15GraphicsObject *object;

    if (slot && s->objects[slot - 1].valid &&
        s->objects[slot - 1].instance == instance) {
        return &s->objects[slot - 1];
    }
    object = nv15_find_object(s, instance);
    s->bound_slot[channel][subchannel] = object ? object - s->objects + 1 : 0;
    return object;
}

static bool nv15_object_bound_on_active_channel(NVIDIAQuadro2State *s,
                                                 uint32_t instance)
{
    unsigned int subchannel;

    if (s->active_channel >= NV15_USER_CHANNELS) {
        return false;
    }
    for (subchannel = 0; subchannel < NV15_USER_SUBCHANNELS; subchannel++) {
        if (s->subchannel_instance[s->active_channel][subchannel] ==
            instance) {
            return true;
        }
    }
    return false;
}

static NV15GraphicsObject *nv15_find_bound_object(NVIDIAQuadro2State *s,
                                                   unsigned int channel,
                                                   uint32_t class_id)
{
    unsigned int subchannel;

    for (subchannel = 0; subchannel < NV15_USER_SUBCHANNELS; subchannel++) {
        NV15GraphicsObject *object = nv15_bound_object(s, channel, subchannel);

        if (object && object->class_id == class_id) {
            return object;
        }
    }
    return NULL;
}

static NV15GraphicsObject *nv15_get_context(NVIDIAQuadro2State *s,
                                             unsigned int channel,
                                             NV15GraphicsObject *object,
                                             uint32_t instance,
                                             uint8_t explicit_mask,
                                             uint32_t class_id,
                                             bool inherit)
{
    unsigned int object_index = object - s->objects;
    NV15GraphicsObject *context;

    if (instance) {
        context = nv15_find_object(s, instance);
        return context && context->class_id == class_id ? context : NULL;
    }
    if (!inherit || (s->context_explicit[object_index] & explicit_mask)) {
        return NULL;
    }
    return nv15_find_bound_object(s, channel, class_id);
}

static NV15ChannelContext *nv15_get_channel_context(
    NVIDIAQuadro2State *s, unsigned int channel, NV15GraphicsObject *object,
    uint32_t instance, uint8_t explicit_mask, uint32_t class_id,
    bool inherit)
{
    if (!nv15_get_context(s, channel, object, instance, explicit_mask,
                          class_id, inherit)) {
        return NULL;
    }
    return &s->channel_context[channel];
}

static NV15GraphicsObject *nv15_get_object(NVIDIAQuadro2State *s,
                                            uint32_t instance)
{
    NV15GraphicsObject *object = nv15_find_object(s, instance);
    uint32_t word0 = nv15_ramin_read32(s, instance);
    uint32_t word1 = nv15_ramin_read32(s, instance + 4);
    uint32_t word2 = nv15_ramin_read32(s, instance + 8);
    uint32_t class_id = word0 & NV15_GRAPHICS_CLASS_MASK;
    unsigned int i;

    if (!object) {
        for (i = 0; i < NV15_MAX_OBJECTS; i++) {
            if (!s->objects[i].valid) {
                object = &s->objects[i];
                break;
            }
        }
    }
    if (!object) {
        unsigned int start = (instance >> 4) % NV15_MAX_OBJECTS;

        for (i = 0; i < NV15_MAX_OBJECTS; i++) {
            NV15GraphicsObject *candidate =
                &s->objects[(start + i) % NV15_MAX_OBJECTS];

            if (!nv15_object_bound_on_active_channel(s,
                                                       candidate->instance)) {
                object = candidate;
                break;
            }
        }
        if (!object) {
            return NULL;
        }
    }
    if (!object->valid || object->instance != instance ||
        object->class_id != class_id) {
        i = object - s->objects;
        if (object->valid) {
            nv15_object_index_remove(s, object);
        }
        memset(object, 0, sizeof(*object));
        s->surface_explicit[i] = false;
        s->context_explicit[i] = 0;
        object->valid = true;
        object->instance = instance;
        object->class_id = class_id;
        nv15_object_index_insert(s, object);
        /* NV10 GROBJs carry their initial state in the first three words. */
        object->operation = extract32(word0, 15, 3);
        object->mono_format = extract32(word1, 0, 2);
        object->color_format = extract32(word1, 8, 6);
        object->dma_notify = extract32(word1, 16, 16) << 4;
        object->dma_source = extract32(word2, 0, 16) << 4;
        object->dma_dest = extract32(word2, 16, 16) << 4;
        if (class_id == NV15_CLASS_CONTEXT_ROP) {
            object->color = 0xcc;
        } else if (class_id == NV15_CLASS_IMAGE_PATTERN) {
            object->pattern_select = 1;
            object->mono_color[1] = UINT32_MAX;
            object->mono_bitmap[0] = UINT32_MAX;
            object->mono_bitmap[1] = UINT32_MAX;
        }
    }
    return object;
}

static NV15GraphicsObject *nv15_rehydrate_channel_objects(
    NVIDIAQuadro2State *s, unsigned int channel, uint32_t wanted_instance)
{
    unsigned int subchannel;

    for (subchannel = 0; subchannel < NV15_USER_SUBCHANNELS; subchannel++) {
        uint32_t instance = s->subchannel_instance[channel][subchannel];
        NV15GraphicsObject *object;

        if (!instance || !nv15_ramin_range_valid(instance, 12)) {
            continue;
        }
        object = nv15_get_object(s, instance);
        if (!object) {
            continue;
        }
    }
    return nv15_find_object(s, wanted_instance);
}

static bool nv15_dma_object_decode(uint32_t flags, uint32_t limit,
                                   uint32_t frame, bool allow_classless_fb,
                                   NV15DMAObject *dma)
{
    uint32_t class_id = flags & NV15_DMA_CLASS_MASK;

    memset(dma, 0, sizeof(*dma));
    if (class_id != NV15_DMA_FROM_MEMORY &&
        class_id != NV15_DMA_TO_MEMORY && class_id != NV15_DMA_IN_MEMORY &&
        !(allow_classless_fb && class_id == 0 &&
          (flags & (NV15_DMA_PRESENT | NV15_DMA_LINEAR)) ==
          (NV15_DMA_PRESENT | NV15_DMA_LINEAR) &&
          (flags & NV15_DMA_TARGET_MASK) == NV15_DMA_TARGET_VRAM)) {
        return false;
    }

    dma->address = (frame & NV15_DMA_FRAME_MASK) |
                   ((flags & NV15_DMA_ADJUST_MASK) >> 20);
    dma->length = (uint64_t)limit + 1;
    dma->target = flags & NV15_DMA_TARGET_MASK;
    dma->class_id = class_id;
    dma->valid = true;
    return true;
}

static bool nv15_dma_object_load_internal(NVIDIAQuadro2State *s,
                                          uint32_t instance,
                                          bool allow_classless_fb,
                                          NV15DMAObject *dma)
{
    uint32_t flags;
    uint32_t limit;
    uint32_t frame;

    memset(dma, 0, sizeof(*dma));
    if (!nv15_ramin_range_valid(instance, 12)) {
        return false;
    }
    flags = nv15_ramin_read32(s, instance);
    limit = nv15_ramin_read32(s, instance + 4);
    frame = nv15_ramin_read32(s, instance + 8);
    return nv15_dma_object_decode(flags, limit, frame, allow_classless_fb, dma);
}

static bool nv15_dma_object_load(NVIDIAQuadro2State *s, uint32_t instance,
                                 NV15DMAObject *dma)
{
    return nv15_dma_object_load_internal(s, instance, false, dma);
}

static bool nv15_dma_readable(const NV15DMAObject *dma)
{
    return dma->class_id == NV15_DMA_FROM_MEMORY ||
           dma->class_id == NV15_DMA_IN_MEMORY;
}

static bool nv15_dma_writable(const NV15DMAObject *dma)
{
    return dma->class_id == NV15_DMA_TO_MEMORY ||
           dma->class_id == NV15_DMA_IN_MEMORY;
}

static bool nv15_dma_range_valid(const NV15DMAObject *dma, uint64_t offset,
                                 uint64_t size)
{
    return dma->valid && offset <= dma->length &&
           size <= dma->length - offset;
}

static bool nv15_dma_is_vram(const NV15DMAObject *dma)
{
    return dma->target == NV15_DMA_TARGET_VRAM;
}

static bool nv15_dma_backing_range_valid(NVIDIAQuadro2State *s,
                                          const NV15DMAObject *dma,
                                          uint64_t offset, uint64_t size)
{
    uint64_t address;

    if (!nv15_dma_range_valid(dma, offset, size) ||
        uadd64_overflow(dma->address, offset, &address)) {
        return false;
    }
    if (dma->target == NV15_DMA_TARGET_VRAM ||
        dma->target == NV15_DMA_TARGET_VRAM_TILED) {
        if (address > s->vga.vram_size ||
            size > s->vga.vram_size - address) {
            return false;
        }
        if (dma->target == NV15_DMA_TARGET_VRAM_TILED) {
            for (uint64_t done = 0; done < size; ) {
                uint64_t physical = nv15_tile_address(s, address + done);
                uint64_t count = MIN(size - done, 16 - ((address + done) & 15));

                if (physical >= s->vga.vram_size ||
                    count > s->vga.vram_size - physical) {
                    return false;
                }
                done += count;
            }
        }
        return true;
    }
    return dma->target == NV15_DMA_TARGET_PCI ||
           dma->target == NV15_DMA_TARGET_AGP;
}

static bool nv15_dma_tiled_access(NVIDIAQuadro2State *s,
                                   const NV15DMAObject *dma, uint64_t offset,
                                   void *buffer, uint64_t size, bool write,
                                   bool dirty)
{
    struct {
        uint64_t physical;
        unsigned int length;
    } spans[257];
    VGADirtyRange damage = { 0 };
    uint64_t address;
    unsigned int n = 0;
    unsigned int done = 0;

    assert(size <= 4096);
    if (!nv15_dma_range_valid(dma, offset, size) ||
        uadd64_overflow(dma->address, offset, &address) ||
        address > s->vga.vram_size || size > s->vga.vram_size - address) {
        return false;
    }
    while (done < size) {
        uint64_t physical = nv15_tile_address(s, address + done);
        unsigned int length = MIN(size - done, 16 - ((address + done) & 15));

        if (physical >= s->vga.vram_size ||
            length > s->vga.vram_size - physical) {
            return false;
        }
        spans[n].physical = physical;
        spans[n++].length = length;
        done += length;
    }
    /* PFB stays unchanged during these local VRAM accesses. */
    done = 0;
    for (unsigned int i = 0; i < n; i++) {
        uint8_t *vram = s->vga.vram_ptr + spans[i].physical;
        uint8_t *data = (uint8_t *)buffer + done;
        unsigned int length = spans[i].length;

        if (write) {
            memcpy(vram, data, length);
            if (dirty) {
                vga_dirty_range_add(&s->vga, &damage,
                                     spans[i].physical, length);
            }
        } else {
            memcpy(data, vram, length);
        }
        done += length;
    }
    vga_dirty_range_flush(&s->vga, &damage);
    return true;
}

static bool nv15_dma_read(NVIDIAQuadro2State *s, const NV15DMAObject *dma,
                          uint64_t offset, void *buffer, uint64_t size)
{
    uint64_t address;

    if (dma->target == NV15_DMA_TARGET_VRAM_TILED && size <= 4096) {
        return nv15_dma_tiled_access(s, dma, offset, buffer, size,
                                      false, false);
    }
    if (!nv15_dma_backing_range_valid(s, dma, offset, size)) {
        return false;
    }
    address = dma->address + offset;
    if (dma->target == NV15_DMA_TARGET_VRAM_TILED) {
        for (uint64_t done = 0; done < size; ) {
            uint64_t physical = nv15_tile_address(s, address + done);
            uint64_t count = MIN(size - done, 16 - ((address + done) & 15));

            memcpy((uint8_t *)buffer + done,
                   s->vga.vram_ptr + physical, count);
            done += count;
        }
        return true;
    }
    if (dma->target == NV15_DMA_TARGET_VRAM) {
        memcpy(buffer, s->vga.vram_ptr + address, size);
        return true;
    }
    if (dma->target == NV15_DMA_TARGET_PCI ||
        dma->target == NV15_DMA_TARGET_AGP) {
        return pci_dma_read(&s->parent_obj, address, buffer, size) == MEMTX_OK;
    }
    return false;
}

static bool nv15_dma_write(NVIDIAQuadro2State *s, const NV15DMAObject *dma,
                           uint64_t offset, const void *buffer, uint64_t size,
                           bool dirty)
{
    uint64_t address;

    if (dma->target == NV15_DMA_TARGET_VRAM_TILED && size <= 4096) {
        return nv15_dma_tiled_access(s, dma, offset, (void *)buffer, size,
                                      true, dirty);
    }
    if (!nv15_dma_backing_range_valid(s, dma, offset, size)) {
        return false;
    }
    address = dma->address + offset;
    if (dma->target == NV15_DMA_TARGET_VRAM_TILED) {
        VGADirtyRange damage = { 0 };

        for (uint64_t done = 0; done < size; ) {
            uint64_t physical = nv15_tile_address(s, address + done);
            uint64_t count = MIN(size - done, 16 - ((address + done) & 15));

            memcpy(s->vga.vram_ptr + physical,
                   (const uint8_t *)buffer + done, count);
            if (dirty) {
                vga_dirty_range_add(&s->vga, &damage, physical, count);
            }
            done += count;
        }
        vga_dirty_range_flush(&s->vga, &damage);
        return true;
    }
    if (dma->target == NV15_DMA_TARGET_VRAM) {
        memcpy(s->vga.vram_ptr + address, buffer, size);
        if (dirty && size) {
            memory_region_set_dirty(&s->vga.vram, address, size);
        }
        return true;
    }
    if (dma->target == NV15_DMA_TARGET_PCI ||
        dma->target == NV15_DMA_TARGET_AGP) {
        return pci_dma_write(&s->parent_obj, address, buffer, size) == MEMTX_OK;
    }
    return false;
}

static bool nv15_dma_read32(NVIDIAQuadro2State *s, const NV15DMAObject *dma,
                            uint64_t offset, uint32_t *value)
{
    uint32_t raw;

    if (!nv15_dma_read(s, dma, offset, &raw, sizeof(raw))) {
        return false;
    }
    *value = le32_to_cpu(raw);
    return true;
}

static bool nv15_resolve_handle(NVIDIAQuadro2State *s, unsigned int channel,
                                uint32_t handle, uint32_t expected_engine,
                                uint32_t *instance)
{
    uint32_t engine;

    if (!handle) {
        *instance = 0;
        return true;
    }
    return nv15_ramht_lookup(s, channel, handle, instance, &engine) &&
           engine == expected_engine;
}

static unsigned int nv15_surface_cpp(uint32_t format)
{
    switch (format & 0xff) {
    case 1:
        return 1;
    case 2:
    case 3:
    case 4:
    case 5:
        return 2;
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        return 4;
    default:
        return 0;
    }
}

static bool nv15_surface_load(NVIDIAQuadro2State *s, uint32_t instance,
                              bool source, NV15Surface *surface)
{
    NV15GraphicsObject *object = nv15_find_object(s, instance);
    uint32_t dma_instance;

    memset(surface, 0, sizeof(*surface));
    if (!object || object->class_id != NV15_CLASS_CONTEXT_SURFACES_2D) {
        return false;
    }
    surface->format = object->format & 0xff;
    surface->cpp = nv15_surface_cpp(object->format);
    if (!surface->cpp) {
        return false;
    }
    if (source) {
        dma_instance = object->dma_source;
        surface->pitch = object->pitch & 0xffff;
        surface->offset = object->offset_source;
    } else {
        dma_instance = object->dma_dest;
        surface->pitch = object->pitch >> 16;
        surface->offset = object->offset_dest;
    }
    if (!surface->pitch || !dma_instance) {
        return false;
    }
    /* Native drivers use a classless linear VRAM DMA as framebuffer storage. */
    return nv15_dma_object_load_internal(s, dma_instance, true,
                                         &surface->dma);
}

static bool nv15_surface_pixel_offset(const NV15Surface *surface,
                                      int32_t x, int32_t y,
                                      uint64_t *offset)
{
    uint64_t row;
    uint64_t column;

    if (x < 0 || y < 0 ||
        umul64_overflow((uint64_t)y, surface->pitch, &row) ||
        umul64_overflow((uint64_t)x, surface->cpp, &column) ||
        row > UINT64_MAX - column ||
        surface->offset > UINT64_MAX - row - column) {
        return false;
    }
    *offset = surface->offset + row + column;
    return nv15_dma_range_valid(&surface->dma, *offset, surface->cpp);
}

static bool nv15_surface_read_pixel(NVIDIAQuadro2State *s,
                                    const NV15Surface *surface,
                                    int32_t x, int32_t y, uint32_t *pixel)
{
    uint8_t bytes[4] = { 0 };
    uint64_t offset;

    if (!nv15_surface_pixel_offset(surface, x, y, &offset) ||
        !nv15_dma_read(s, &surface->dma, offset, bytes, surface->cpp)) {
        return false;
    }
    switch (surface->cpp) {
    case 1:
        *pixel = bytes[0];
        break;
    case 2:
        *pixel = lduw_le_p(bytes);
        break;
    case 4:
        *pixel = ldl_le_p(bytes);
        break;
    default:
        return false;
    }
    return true;
}

static bool nv15_surface_write_pixel(NVIDIAQuadro2State *s,
                                     const NV15Surface *surface,
                                     int32_t x, int32_t y, uint32_t pixel,
                                     bool dirty)
{
    uint8_t bytes[4];
    uint64_t offset;

    if (!nv15_surface_pixel_offset(surface, x, y, &offset)) {
        return false;
    }
    switch (surface->cpp) {
    case 1:
        bytes[0] = pixel;
        break;
    case 2:
        stw_le_p(bytes, pixel);
        break;
    case 4:
        stl_le_p(bytes, pixel);
        break;
    default:
        return false;
    }
    return nv15_dma_write(s, &surface->dma, offset, bytes, surface->cpp,
                          dirty);
}

static bool nv15_surface_rectangle_valid(NVIDIAQuadro2State *s,
                                         const NV15Surface *surface,
                                         int32_t x, int32_t y,
                                         uint32_t width, uint32_t height)
{
    uint64_t first;
    uint64_t last;
    int64_t last_x;
    int64_t last_y;

    if (!width || !height || x < 0 || y < 0) {
        return false;
    }
    last_x = (int64_t)x + width - 1;
    last_y = (int64_t)y + height - 1;
    if (last_x > INT32_MAX || last_y > INT32_MAX ||
        !nv15_surface_pixel_offset(surface, x, y, &first) ||
        !nv15_surface_pixel_offset(surface, last_x, last_y, &last)) {
        return false;
    }
    return nv15_dma_backing_range_valid(s, &surface->dma, first,
                                         surface->cpp) &&
           nv15_dma_backing_range_valid(s, &surface->dma, last,
                                         surface->cpp);
}

static void nv15_surface_mark_dirty(NVIDIAQuadro2State *s,
                                    const NV15Surface *surface,
                                    int32_t x, int32_t y,
                                    uint32_t width, uint32_t height)
{
    uint64_t first;
    uint64_t last;
    uint64_t bytes;
    uint64_t page_mask = qemu_target_page_size() - 1;
    uint64_t run_start;
    uint64_t run_end;
    uint64_t row_bytes = (uint64_t)width * surface->cpp;

    if (!width || !height ||
        (surface->dma.target != NV15_DMA_TARGET_VRAM &&
         surface->dma.target != NV15_DMA_TARGET_VRAM_TILED) ||
        !nv15_surface_pixel_offset(surface, x, y, &first) ||
        !nv15_surface_pixel_offset(surface, x + width - 1,
                                   y + height - 1, &last) ||
        last > UINT64_MAX - surface->cpp ||
        surface->dma.address > UINT64_MAX - first) {
        return;
    }
    last += surface->cpp;
    bytes = last - first;
    if (!nv15_dma_backing_range_valid(s, &surface->dma, first, bytes)) {
        return;
    }
    if (surface->dma.target == NV15_DMA_TARGET_VRAM_TILED) {
        memory_region_set_dirty(&s->vga.vram, 0, s->vga.vram_size);
        return;
    }
    first += surface->dma.address;
    run_start = first & ~page_mask;
    run_end = (first + row_bytes + page_mask) & ~page_mask;
    for (uint32_t row = 1; row < height; row++) {
        uint64_t address = first + (uint64_t)row * surface->pitch;
        uint64_t start = address & ~page_mask;
        uint64_t end = (address + row_bytes + page_mask) & ~page_mask;

        if (start <= run_end) {
            run_end = MAX(run_end, end);
        } else {
            memory_region_set_dirty(&s->vga.vram, run_start,
                                    run_end - run_start);
            run_start = start;
            run_end = end;
        }
    }
    run_end = MIN(run_end, s->vga.vram_size);
    memory_region_set_dirty(&s->vga.vram, run_start, run_end - run_start);
}

static uint32_t nv15_pixel_mask(unsigned int cpp)
{
    return cpp == 1 ? 0xff : cpp == 2 ? 0xffff : UINT32_MAX;
}

static uint32_t nv15_rop3(uint8_t rop, uint32_t pattern, uint32_t source,
                          uint32_t destination)
{
    uint32_t result = 0;
    unsigned int p;
    unsigned int s;
    unsigned int d;

    switch (rop) {
    case 0x00:
        return 0;
    case 0xff:
        return UINT32_MAX;
    case 0xaa:
        return destination;
    case 0x55:
        return ~destination;
    case 0xcc:
        return source;
    case 0x33:
        return ~source;
    case 0xf0:
        return pattern;
    case 0x0f:
        return ~pattern;
    case 0x66:
        return source ^ destination;
    case 0x88:
        return source & destination;
    case 0xee:
        return source | destination;
    case 0x5a:
        return pattern ^ destination;
    case 0xa0:
        return pattern & destination;
    case 0xfa:
        return pattern | destination;
    case 0x3c:
        return pattern ^ source;
    case 0xc0:
        return pattern & source;
    case 0xfc:
        return pattern | source;
    case 0x96:
        return pattern ^ source ^ destination;
    case 0xca:
        return (pattern & source) | (~pattern & destination);
    default:
        break;
    }

    for (p = 0; p < 2; p++) {
        for (s = 0; s < 2; s++) {
            for (d = 0; d < 2; d++) {
                unsigned int index = (p << 2) | (s << 1) | d;

                if (rop & BIT(index)) {
                    result |= (p ? pattern : ~pattern) &
                              (s ? source : ~source) &
                              (d ? destination : ~destination);
                }
            }
        }
    }
    return result;
}

static uint32_t nv15_pattern_pixel(const NV15ChannelContext *pattern,
                                   int32_t x, int32_t y)
{
    unsigned int bit;
    uint32_t bitmap;

    if (!pattern) {
        return UINT32_MAX;
    }
    if (pattern->pattern_select != 1) {
        return pattern->mono_color[1];
    }
    bit = ((unsigned int)y & 7) * 8 + ((unsigned int)x & 7);
    bitmap = pattern->mono_bitmap[bit >> 5];
    return pattern->mono_color[(bitmap >> (bit & 31)) & 1];
}

static uint32_t nv15_apply_operation(const NV15GraphicsObject *object,
                                     const NV15ChannelContext *rop_context,
                                     uint32_t pattern, uint32_t source,
                                     uint32_t destination, unsigned int cpp)
{
    uint32_t mask = nv15_pixel_mask(cpp);

    if (object->operation == 1) {
        uint8_t rop = rop_context ? rop_context->rop : 0xcc;

        return nv15_rop3(rop, pattern, source, destination) & mask;
    }
    return source & mask;
}

static bool nv15_operation_reads_destination(
    const NV15GraphicsObject *object,
    const NV15ChannelContext *rop_context)
{
    uint8_t rop;

    if (object->operation != 1) {
        return false;
    }
    rop = rop_context ? rop_context->rop : 0xcc;
    return ((rop ^ (rop >> 1)) & 0x55) != 0;
}

static uint8_t nv15_operation_rop(const NV15GraphicsObject *object,
                                 const NV15ChannelContext *rop_context)
{
    return object->operation == 1 && rop_context ? rop_context->rop : 0xcc;
}

static bool nv15_pattern_constant(const NV15ChannelContext *pattern,
                                  uint32_t *pixel)
{
    if (!pattern) {
        *pixel = UINT32_MAX;
    } else if (pattern->pattern_select != 1 ||
               pattern->mono_color[0] == pattern->mono_color[1] ||
               (pattern->mono_bitmap[0] == UINT32_MAX &&
                pattern->mono_bitmap[1] == UINT32_MAX)) {
        *pixel = pattern->mono_color[1];
    } else if (!pattern->mono_bitmap[0] && !pattern->mono_bitmap[1]) {
        *pixel = pattern->mono_color[0];
    } else {
        return false;
    }
    return true;
}

/* Called only after complete local VRAM rectangle validation. */
static void nv15_surface_fill_rows(NVIDIAQuadro2State *s,
                                   const NV15Surface *surface,
                                   int32_t x, int32_t y,
                                   uint32_t width, uint32_t height,
                                   uint32_t pixel)
{
    size_t row_bytes = (size_t)width * surface->cpp;
    uint8_t *first = s->vga.vram_ptr + surface->dma.address + surface->offset +
                     (uint64_t)y * surface->pitch + (uint64_t)x * surface->cpp;
    uint8_t pattern[192];
    bool same_byte = true;

    stl_le_p(pattern, pixel);
    for (unsigned int i = 1; i < surface->cpp; i++) {
        same_byte &= pattern[i] == pattern[0];
    }
    if (!same_byte) {
        for (size_t filled = surface->cpp; filled < sizeof(pattern);) {
            size_t chunk = MIN(filled, sizeof(pattern) - filled);

            memcpy(pattern + filled, pattern, chunk);
            filled += chunk;
        }
    }
    for (uint32_t row = 0; row < height; row++) {
        uint8_t *dst = first + (size_t)row * surface->pitch;

        if (same_byte) {
            memset(dst, pattern[0], row_bytes);
        } else if (surface->cpp == 2 || surface->cpp == 4) {
            uint64_t wide = ldq_he_p(pattern);
            size_t pos = 0;

            for (; pos + 8 <= row_bytes; pos += 8) {
                stq_he_p(dst + pos, wide);
            }
            for (; pos < row_bytes; pos += surface->cpp) {
                memcpy(dst + pos, pattern, surface->cpp);
            }
        } else {
            /* CPU writes to an earlier row must not change the fill color. */
            for (size_t pos = 0; pos < row_bytes; pos += sizeof(pattern)) {
                memcpy(dst + pos, pattern,
                       MIN(sizeof(pattern), row_bytes - pos));
            }
        }
    }
}

static void nv15_clip_fill_rect(int32_t *x, int32_t *y,
                                uint32_t *width, uint32_t *height)
{
    if (*x < 0) {
        uint32_t clipped = MIN((uint32_t)-*x, *width);

        *x += clipped;
        *width -= clipped;
    }
    if (*y < 0) {
        uint32_t clipped = MIN((uint32_t)-*y, *height);

        *y += clipped;
        *height -= clipped;
    }
}

static bool nv15_context_clip_fill_rect(NVIDIAQuadro2State *s,
                                        const NV15GraphicsObject *object,
                                        int32_t *x, int32_t *y,
                                        uint32_t *width, uint32_t *height)
{
    NV15GraphicsObject *clip;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (!object->clip) {
        return true;
    }
    clip = nv15_find_object(s, object->clip);
    if (!clip || clip->class_id != NV15_CLASS_CONTEXT_CLIP) {
        return false;
    }

    left = sextract32(clip->point_in, 0, 16);
    top = sextract32(clip->point_in, 16, 16);
    right = left + (clip->size & 0xffff);
    bottom = top + (clip->size >> 16);
    left = MAX((int64_t)*x, left);
    top = MAX((int64_t)*y, top);
    right = MIN((int64_t)*x + *width, right);
    bottom = MIN((int64_t)*y + *height, bottom);
    if (right <= left || bottom <= top) {
        *width = 0;
        *height = 0;
        return true;
    }
    *x = left;
    *y = top;
    *width = (uint32_t)(right - left);
    *height = (uint32_t)(bottom - top);
    return true;
}

static bool nv15_charge_work(NV15WorkBudget *budget, uint64_t bytes)
{
    if (bytes > budget->bytes) {
        /*
         * Each primitive has its own size validation. Admit one bounded
         * primitive even when it is larger than a scheduling quantum.
         */
        if (budget->bytes == NV15_MAX_PUSH_WORK_BYTES &&
            bytes <= (uint64_t)NV15_MAX_2D_PIXELS * 20) {
            budget->bytes = 0;
            return true;
        }
        budget->exhausted = bytes <= (uint64_t)NV15_MAX_2D_PIXELS * 20;
        return false;
    }
    budget->bytes -= bytes;
    return true;
}

typedef struct NV15RopPixels {
    const NV15GraphicsObject *object;
    const NV15ChannelContext *rop;
    const NV15ChannelContext *pattern;
    const uint8_t *source;
    uint8_t *destination;
    uint64_t source_pitch;
    uint64_t destination_pitch;
    int32_t x, y;
    uint32_t width, height;
    uint32_t color;
    bool backwards;
    bool read_source;
    bool read_destination;
    bool read_pattern;
} NV15RopPixels;

static inline QEMU_ALWAYS_INLINE void
nv15_rop_pixels_cpp(const NV15RopPixels *p, unsigned int cpp)
{
    for (uint32_t row = 0; row < p->height; row++) {
        uint32_t y = p->backwards ? p->height - 1 - row : row;
        uint8_t *dst = p->destination + y * p->destination_pitch;
        const uint8_t *src = p->source ?
            p->source + y * p->source_pitch : NULL;

        for (uint32_t col = 0; col < p->width; col++) {
            uint32_t x = p->backwards ? p->width - 1 - col : col;
            uint8_t *pixel = dst + x * cpp;
            uint32_t source = p->color;
            uint32_t destination = 0;
            uint32_t pattern = p->read_pattern ?
                nv15_pattern_pixel(p->pattern, p->x + x, p->y + y) : 0;
            uint32_t result;

            if (p->read_source) {
                const uint8_t *input = src + x * cpp;

                source = cpp == 1 ? *input : cpp == 2 ?
                         lduw_le_p(input) : ldl_le_p(input);
            }
            if (p->read_destination) {
                destination = cpp == 1 ? *pixel : cpp == 2 ?
                              lduw_le_p(pixel) : ldl_le_p(pixel);
            }
            result = nv15_apply_operation(p->object, p->rop, pattern,
                                           source, destination, cpp);
            if (cpp == 1) {
                *pixel = result;
            } else if (cpp == 2) {
                stw_le_p(pixel, result);
            } else {
                stl_le_p(pixel, result);
            }
        }
    }
}

static void nv15_rop_pixels(const NV15RopPixels *p, unsigned int cpp)
{
    switch (cpp) {
    case 1:
        nv15_rop_pixels_cpp(p, 1);
        break;
    case 2:
        nv15_rop_pixels_cpp(p, 2);
        break;
    case 4:
        nv15_rop_pixels_cpp(p, 4);
        break;
    default:
        g_assert_not_reached();
    }
}

static bool nv15_fill_rectangle(NVIDIAQuadro2State *s, unsigned int channel,
                                NV15GraphicsObject *object,
                                uint32_t point, uint32_t size,
                                NV15WorkBudget *budget)
{
    unsigned int object_index = object - s->objects;
    bool inherited_surface =
        object->class_id == NV15_CLASS_GDI_RECTANGLE_TEXT &&
        !s->surface_explicit[object_index];
    bool gdi_layout = object->class_id == NV15_CLASS_GDI_RECTANGLE_TEXT;
    uint32_t surface_instance = inherited_surface ?
        s->active_surface[channel] : object->surface;
    NV15ChannelContext *pattern = nv15_get_channel_context(
        s, channel, object, object->pattern, NV15_CONTEXT_EXPLICIT_PATTERN,
        NV15_CLASS_IMAGE_PATTERN, gdi_layout);
    NV15ChannelContext *rop_context = nv15_get_channel_context(
        s, channel, object, object->rop, NV15_CONTEXT_EXPLICIT_ROP,
        NV15_CLASS_CONTEXT_ROP, gdi_layout);
    bool read_destination =
        nv15_operation_reads_destination(object, rop_context);
    NV15Surface destination;
    int32_t x = gdi_layout ? (int16_t)(point >> 16) : (int16_t)point;
    int32_t y = gdi_layout ? (int16_t)point : (int16_t)(point >> 16);
    uint32_t width = gdi_layout ? size >> 16 : size & 0xffff;
    uint32_t height = gdi_layout ? size & 0xffff : size >> 16;
    uint32_t iy;
    uint32_t ix;
    bool wrote = false;

    nv15_clip_fill_rect(&x, &y, &width, &height);
    if (!nv15_context_clip_fill_rect(s, object, &x, &y, &width, &height)) {
        return false;
    }
    if (!width || !height) {
        return true;
    }
    if ((uint64_t)width * height > NV15_MAX_2D_PIXELS) {
        return false;
    }
    if (!surface_instance) {
        return false;
    }
    if (!nv15_surface_load(s, surface_instance, false, &destination)) {
        return false;
    }
    if (!nv15_surface_rectangle_valid(s, &destination, x, y,
                                      width, height)) {
        return false;
    }
    if (!nv15_charge_work(budget, (uint64_t)width * height *
                                  destination.cpp)) {
        return false;
    }

    if (!read_destination && nv15_dma_is_vram(&destination.dma) &&
        (uint64_t)width * destination.cpp <= destination.pitch) {
        uint8_t rop = nv15_operation_rop(object, rop_context);
        bool pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;
        uint32_t pattern_pixel = 0;

        if (!pattern_needed || nv15_pattern_constant(pattern, &pattern_pixel)) {
            uint32_t pixel = nv15_apply_operation(object, rop_context,
                pattern_pixel, object->color, 0, destination.cpp);

            nv15_surface_fill_rows(s, &destination, x, y, width, height, pixel);
            nv15_surface_mark_dirty(s, &destination, x, y, width, height);
            nv15_graphic_damage(s);
            return true;
        }
    }

    if (nv15_dma_is_vram(&destination.dma)) {
        NV15RopPixels pixels = {
            .object = object, .rop = rop_context, .pattern = pattern,
            .destination = s->vga.vram_ptr + destination.dma.address +
                destination.offset + (uint64_t)y * destination.pitch +
                (uint64_t)x * destination.cpp,
            .destination_pitch = destination.pitch,
            .x = x, .y = y, .width = width, .height = height,
            .color = object->color,
            .read_destination = read_destination, .read_pattern = true,
        };

        nv15_rop_pixels(&pixels, destination.cpp);
        nv15_surface_mark_dirty(s, &destination, x, y, width, height);
        nv15_graphic_damage(s);
        return true;
    }
    for (iy = 0; iy < height; iy++) {
        for (ix = 0; ix < width; ix++) {
            uint32_t destination_pixel = 0;
            uint32_t pattern_pixel;
            uint32_t pixel;

            if (read_destination &&
                !nv15_surface_read_pixel(s, &destination, x + ix, y + iy,
                                         &destination_pixel)) {
                goto fail;
            }
            pattern_pixel = nv15_pattern_pixel(pattern, x + ix, y + iy);
            pixel = nv15_apply_operation(object, rop_context, pattern_pixel,
                                          object->color, destination_pixel,
                                          destination.cpp);
            if (!nv15_surface_write_pixel(s, &destination, x + ix, y + iy,
                                          pixel, false)) {
                goto fail;
            }
            if (!wrote) {
                nv15_surface_mark_dirty(s, &destination, x, y,
                                        width, height);
                wrote = true;
            }
        }
    }
    nv15_graphic_damage(s);
    return true;

fail:
    if (wrote) {
        nv15_graphic_damage(s);
    }
    return false;
}

static bool nv15_draw_line(NVIDIAQuadro2State *s, unsigned int channel,
                           NV15GraphicsObject *object,
                           int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1,
                           NV15WorkBudget *budget)
{
    unsigned int object_index = object - s->objects;
    bool inherited_surface = !s->surface_explicit[object_index];
    uint32_t surface_instance = inherited_surface ?
        s->active_surface[channel] : object->surface;
    NV15ChannelContext *pattern = nv15_get_channel_context(
        s, channel, object, object->pattern, NV15_CONTEXT_EXPLICIT_PATTERN,
        NV15_CLASS_IMAGE_PATTERN, true);
    NV15ChannelContext *rop_context = nv15_get_channel_context(
        s, channel, object, object->rop, NV15_CONTEXT_EXPLICIT_ROP,
        NV15_CLASS_CONTEXT_ROP, true);
    bool read_destination =
        nv15_operation_reads_destination(object, rop_context);
    NV15GraphicsObject *clip = nv15_get_context(
        s, channel, object, object->clip, NV15_CONTEXT_EXPLICIT_CLIP,
        NV15_CLASS_CONTEXT_CLIP, true);
    NV15Surface destination;
    int32_t dx;
    int32_t dy;
    bool x_major;
    int32_t major0;
    int32_t major1;
    int32_t minor0;
    int32_t minor1;
    int32_t delta_minor;
    int32_t delta_major;
    int32_t major_step;
    int32_t error = 0;
    int32_t major;
    int32_t minor;
    int64_t clip_left = INT32_MIN;
    int64_t clip_top = INT32_MIN;
    int64_t clip_right = (int64_t)INT32_MAX + 1;
    int64_t clip_bottom = (int64_t)INT32_MAX + 1;
    uint32_t canvas_width;
    int32_t min_x = INT32_MAX;
    int32_t min_y = INT32_MAX;
    int32_t max_x = INT32_MIN;
    int32_t max_y = INT32_MIN;
    bool wrote = false;

    /* NV4 PGRAPH records full words but rejects non-sign-extended xy16. */
    if (x0 != (int16_t)x0 || y0 != (int16_t)y0 ||
        x1 != (int16_t)x1 || y1 != (int16_t)y1) {
        return false;
    }
    dx = x1 - x0;
    dy = y1 - y0;
    x_major = ABS(dx) > ABS(dy);
    major0 = x_major ? x0 : y0;
    major1 = x_major ? x1 : y1;
    minor0 = x_major ? y0 : x0;
    minor1 = x_major ? y1 : x1;

    if (!surface_instance ||
        (clip && clip->class_id != NV15_CLASS_CONTEXT_CLIP) ||
        !nv15_surface_load(s, surface_instance, false, &destination) ||
        !nv15_dma_backing_range_valid(s, &destination.dma,
                                      destination.offset,
                                      destination.cpp)) {
        return false;
    }
    canvas_width = destination.pitch / destination.cpp;
    if (!canvas_width) {
        return false;
    }
    if (clip) {
        clip_left = sextract32(clip->point_in, 0, 16);
        clip_top = sextract32(clip->point_in, 16, 16);
        clip_right = clip_left + (clip->size & 0xffff);
        clip_bottom = clip_top + (clip->size >> 16);
    }

    if (minor1 < minor0) {
        int32_t swap;

        swap = minor0;
        minor0 = minor1;
        minor1 = swap;
        swap = major0;
        major0 = major1;
        major1 = swap;
    }
    delta_minor = minor1 - minor0;
    delta_major = ABS(major1 - major0);
    if (!delta_major) {
        return true;
    }
    if (!nv15_charge_work(budget, (uint64_t)delta_major *
                                  destination.cpp)) {
        return false;
    }
    major_step = major1 > major0 ? 1 : -1;
    minor = minor0;
    for (major = major0; major != major1 + major_step;
         major += major_step) {
        int32_t x;
        int32_t y;
        uint64_t offset;
        uint32_t destination_pixel = 0;
        uint32_t pattern_pixel;
        uint32_t pixel;

        if (error >= delta_major) {
            minor++;
            error -= delta_major * 2;
        }
        x = x_major ? major : minor;
        y = x_major ? minor : major;
        error += delta_minor * 2;

        /* Class 0x5c is LIN: the caller's second endpoint is excluded. */
        if ((x == x1 && y == y1) || x < 0 || y < 0 ||
            (uint32_t)x >= canvas_width || x < clip_left ||
            x >= clip_right || y < clip_top || y >= clip_bottom ||
            !nv15_surface_pixel_offset(&destination, x, y, &offset)) {
            continue;
        }
        if (!nv15_dma_backing_range_valid(s, &destination.dma, offset,
                                           destination.cpp)) {
            goto fail;
        }
        if (read_destination &&
            !nv15_surface_read_pixel(s, &destination, x, y,
                                     &destination_pixel)) {
            goto fail;
        }
        pattern_pixel = nv15_pattern_pixel(pattern, x, y);
        pixel = nv15_apply_operation(object, rop_context, pattern_pixel,
                                     object->color, destination_pixel,
                                     destination.cpp);
        if (!nv15_surface_write_pixel(s, &destination, x, y, pixel, false)) {
            goto fail;
        }
        min_x = MIN(min_x, x);
        min_y = MIN(min_y, y);
        max_x = MAX(max_x, x);
        max_y = MAX(max_y, y);
        wrote = true;
    }
    if (wrote) {
        nv15_surface_mark_dirty(s, &destination, min_x, min_y,
                                max_x - min_x + 1, max_y - min_y + 1);
        nv15_graphic_damage(s);
    }
    return true;

fail:
    if (wrote) {
        nv15_surface_mark_dirty(s, &destination, min_x, min_y,
                                max_x - min_x + 1, max_y - min_y + 1);
        nv15_graphic_damage(s);
    }
    return false;
}

static void nv15_line_set_vertex(NV15GraphicsObject *object,
                                 int32_t x, int32_t y)
{
    /* These generic fields are otherwise unused by class 0x5c. */
    object->point_in = x;
    object->point_out = y;
    object->format = NV15_LINE_VERTEX_VALID;
}

static bool nv15_line_draw_to(NVIDIAQuadro2State *s, unsigned int channel,
                              NV15GraphicsObject *object,
                              int32_t x, int32_t y,
                              NV15WorkBudget *budget)
{
    int32_t x0;
    int32_t y0;

    if (!(object->format & NV15_LINE_VERTEX_VALID)) {
        return false;
    }
    x0 = object->point_in;
    y0 = object->point_out;

    if (!nv15_draw_line(s, channel, object, x0, y0, x, y, budget)) {
        return false;
    }

    /* Keep the old endpoint until drawing succeeds, so a yield can retry. */
    nv15_line_set_vertex(object, x, y);
    return true;
}

static bool nv15_gdi_mono_data(NVIDIAQuadro2State *s, unsigned int channel,
                               NV15GraphicsObject *object, uint32_t value,
                               bool transparent, NV15WorkBudget *budget)
{
    unsigned int object_index = object - s->objects;
    bool inherited_surface = !s->surface_explicit[object_index];
    uint32_t surface_instance = inherited_surface ?
        s->active_surface[channel] : object->surface;
    NV15ChannelContext *pattern = nv15_get_channel_context(
        s, channel, object, object->pattern, NV15_CONTEXT_EXPLICIT_PATTERN,
        NV15_CLASS_IMAGE_PATTERN, true);
    NV15ChannelContext *rop_context = nv15_get_channel_context(
        s, channel, object, object->rop, NV15_CONTEXT_EXPLICIT_ROP,
        NV15_CLASS_CONTEXT_ROP, true);
    bool read_destination =
        nv15_operation_reads_destination(object, rop_context);
    NV15Surface destination;
    uint32_t input_width = object->upload_size_in & 0xffff;
    uint32_t input_height = object->upload_size_in >> 16;
    uint32_t output_width = object->upload_size_out & 0xffff;
    uint32_t output_height = object->upload_size_out >> 16;
    uint32_t draw_width = MIN(input_width, output_width);
    uint32_t draw_height = MIN(input_height, output_height);
    uint32_t dest_x = object->upload_point & 0xffff;
    uint32_t dest_y = object->upload_point >> 16;
    uint32_t clip_left = object->point_in & 0xffff;
    uint32_t clip_top = object->point_in >> 16;
    uint32_t clip_right = object->point_out & 0xffff;
    uint32_t clip_bottom = object->point_out >> 16;
    uint32_t visible_left;
    uint32_t visible_top;
    uint32_t visible_right;
    uint32_t visible_bottom;
    uint64_t total;
    uint64_t remaining;
    uint32_t pixels;
    uint32_t min_x = UINT32_MAX;
    uint32_t min_y = UINT32_MAX;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    uint32_t i;
    bool wrote = false;

    total = (uint64_t)input_width * input_height;
    if ((object->mono_format != NV15_MONO_FORMAT_CGA6 &&
         object->mono_format != NV15_MONO_FORMAT_LE) ||
        !input_width || !input_height || !output_width || !output_height ||
        total > NV15_MAX_2D_PIXELS ||
        (uint64_t)output_width * output_height > NV15_MAX_2D_PIXELS ||
        !surface_instance ||
        !nv15_surface_load(s, surface_instance, false, &destination)) {
        return false;
    }
    if (object->upload_pixel >= total) {
        return true;
    }

    visible_left = MAX(dest_x, clip_left);
    visible_top = MAX(dest_y, clip_top);
    visible_right = MIN(dest_x + draw_width, clip_right);
    visible_bottom = MIN(dest_y + draw_height, clip_bottom);
    if (visible_right > visible_left && visible_bottom > visible_top &&
        !nv15_surface_rectangle_valid(s, &destination,
                                      visible_left, visible_top,
                                      visible_right - visible_left,
                                      visible_bottom - visible_top)) {
        return false;
    }

    remaining = total - object->upload_pixel;
    pixels = MIN(remaining, 32);
    if (!nv15_charge_work(budget, (uint64_t)pixels * destination.cpp)) {
        return false;
    }

    for (i = 0; i < pixels; i++) {
        uint32_t position = object->upload_pixel++;
        uint32_t source_x = position % input_width;
        uint32_t source_y = position / input_width;
        unsigned int bit = object->mono_format == NV15_MONO_FORMAT_CGA6 ?
                           (i ^ 7) : i;
        uint32_t x;
        uint32_t y;
        uint32_t destination_pixel = 0;
        uint32_t pattern_pixel;
        uint32_t source_pixel;
        uint32_t pixel;
        bool foreground = (value >> bit) & 1;

        if (source_x >= draw_width || source_y >= draw_height) {
            continue;
        }
        x = dest_x + source_x;
        y = dest_y + source_y;
        if (x < visible_left || x >= visible_right ||
            y < visible_top || y >= visible_bottom) {
            continue;
        }
        if (transparent && !foreground) {
            continue;
        }
        if (read_destination &&
            !nv15_surface_read_pixel(s, &destination, x, y,
                                     &destination_pixel)) {
            goto fail;
        }
        pattern_pixel = nv15_pattern_pixel(pattern, x, y);
        source_pixel = object->mono_color[foreground];
        pixel = nv15_apply_operation(object, rop_context, pattern_pixel,
                                     source_pixel, destination_pixel,
                                     destination.cpp);
        if (!nv15_surface_write_pixel(s, &destination, x, y, pixel, false)) {
            goto fail;
        }
        min_x = MIN(min_x, (uint32_t)x);
        min_y = MIN(min_y, (uint32_t)y);
        max_x = MAX(max_x, (uint32_t)x);
        max_y = MAX(max_y, (uint32_t)y);
        wrote = true;
    }
    if (wrote) {
        nv15_surface_mark_dirty(s, &destination, min_x, min_y,
                                max_x - min_x + 1, max_y - min_y + 1);
        nv15_graphic_damage(s);
    }
    return true;

fail:
    if (wrote) {
        nv15_surface_mark_dirty(s, &destination, min_x, min_y,
                                max_x - min_x + 1, max_y - min_y + 1);
        nv15_graphic_damage(s);
    }
    return false;
}

static void nv15_clip_blit(int32_t *source_x, int32_t *source_y,
                           int32_t *dest_x, int32_t *dest_y,
                           uint32_t *width, uint32_t *height)
{
    int32_t clip_x = MAX(-*source_x, -*dest_x);
    int32_t clip_y = MAX(-*source_y, -*dest_y);

    if (clip_x > 0) {
        uint32_t clipped = MIN((uint32_t)clip_x, *width);

        *source_x += clipped;
        *dest_x += clipped;
        *width -= clipped;
    }
    if (clip_y > 0) {
        uint32_t clipped = MIN((uint32_t)clip_y, *height);

        *source_y += clipped;
        *dest_y += clipped;
        *height -= clipped;
    }
}

static bool nv15_context_clip_blit(NVIDIAQuadro2State *s,
                                   const NV15GraphicsObject *object,
                                   int32_t *source_x, int32_t *source_y,
                                   int32_t *dest_x, int32_t *dest_y,
                                   uint32_t *width, uint32_t *height)
{
    NV15GraphicsObject *clip;
    int64_t clip_left;
    int64_t clip_top;
    int64_t clip_right;
    int64_t clip_bottom;
    uint32_t clipped;

    if (!object->clip) {
        return true;
    }
    clip = nv15_find_object(s, object->clip);
    if (!clip || clip->class_id != NV15_CLASS_CONTEXT_CLIP) {
        return false;
    }

    clip_left = sextract32(clip->point_in, 0, 16);
    clip_top = sextract32(clip->point_in, 16, 16);
    clip_right = clip_left + (clip->size & 0xffff);
    clip_bottom = clip_top + (clip->size >> 16);
    if (*dest_x < clip_left) {
        clipped = MIN((uint64_t)((int64_t)clip_left - *dest_x),
                      (uint64_t)*width);
        *source_x += clipped;
        *dest_x += clipped;
        *width -= clipped;
    }
    if (*dest_y < clip_top) {
        clipped = MIN((uint64_t)((int64_t)clip_top - *dest_y),
                      (uint64_t)*height);
        *source_y += clipped;
        *dest_y += clipped;
        *height -= clipped;
    }
    if ((int64_t)*dest_x + *width > clip_right) {
        *width = clip_right > *dest_x ?
                 (uint32_t)(clip_right - *dest_x) : 0;
    }
    if ((int64_t)*dest_y + *height > clip_bottom) {
        *height = clip_bottom > *dest_y ?
                  (uint32_t)(clip_bottom - *dest_y) : 0;
    }
    return true;
}

static bool nv15_dma_share_backing(const NV15DMAObject *source,
                                   const NV15DMAObject *destination)
{
    bool source_vram = source->target == NV15_DMA_TARGET_VRAM ||
                       source->target == NV15_DMA_TARGET_VRAM_TILED;
    bool destination_vram = destination->target == NV15_DMA_TARGET_VRAM ||
                            destination->target == NV15_DMA_TARGET_VRAM_TILED;

    return source_vram == destination_vram;
}

static bool nv15_surface_vram_bounds(NVIDIAQuadro2State *s,
                                      const NV15Surface *surface,
                                      uint64_t address, uint64_t row_bytes,
                                      uint32_t height, uint64_t *first,
                                      uint64_t *last)
{
    if (surface->dma.target == NV15_DMA_TARGET_VRAM) {
        *first = address;
        *last = address + (uint64_t)(height - 1) * surface->pitch + row_bytes;
        return true;
    }
    if (surface->dma.target != NV15_DMA_TARGET_VRAM_TILED) {
        return false;
    }
    *first = UINT64_MAX;
    *last = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint64_t row = address + (uint64_t)y * surface->pitch;

        for (uint64_t done = 0; done < row_bytes; ) {
            uint64_t physical = nv15_tile_address(s, row + done);
            uint64_t length = MIN(row_bytes - done, 16 - ((row + done) & 15));

            if (physical >= s->vga.vram_size ||
                length > s->vga.vram_size - physical) {
                return false;
            }
            *first = MIN(*first, physical);
            *last = MAX(*last, physical + length);
            done += length;
        }
    }
    return true;
}

static bool nv15_blit_rectangles_overlap(NVIDIAQuadro2State *s,
                                         const NV15Surface *source,
                                         uint64_t source_start,
                                         const NV15Surface *destination,
                                         uint64_t destination_start,
                                         uint32_t width, uint32_t height)
{
    uint64_t row_bytes = (uint64_t)width * source->cpp;
    uint32_t source_row = 0;
    uint32_t destination_row = 0;

    if (!nv15_dma_share_backing(&source->dma, &destination->dma)) {
        return false;
    }
    if (source->dma.target == NV15_DMA_TARGET_VRAM_TILED ||
        destination->dma.target == NV15_DMA_TARGET_VRAM_TILED) {
        uint64_t src_first, src_last, dst_first, dst_last;

        return !nv15_surface_vram_bounds(s, source, source_start, row_bytes,
                                          height, &src_first, &src_last) ||
               !nv15_surface_vram_bounds(s, destination, destination_start,
                                          row_bytes, height,
                                          &dst_first, &dst_last) ||
               (src_first < dst_last && dst_first < src_last);
    }
    while (source_row < height && destination_row < height) {
        uint64_t source_address = source_start +
                                  (uint64_t)source_row * source->pitch;
        uint64_t destination_address = destination_start +
                                       (uint64_t)destination_row *
                                       destination->pitch;
        uint64_t source_end = source_address + row_bytes;
        uint64_t destination_end = destination_address + row_bytes;

        if (source_address < destination_end &&
            destination_address < source_end) {
            return true;
        }
        if (source_end <= destination_address) {
            source_row++;
        } else {
            destination_row++;
        }
    }
    return false;
}

static bool nv15_blit(NVIDIAQuadro2State *s, unsigned int channel,
                      NV15GraphicsObject *object, NV15WorkBudget *budget)
{
    g_autofree uint8_t *source_snapshot = NULL;
    unsigned int object_index = object - s->objects;
    bool inherited_surface =
        object->class_id == NV15_CLASS_NV11_IMAGE_BLIT &&
        !s->surface_explicit[object_index];
    bool native_contexts = object->class_id == NV15_CLASS_NV11_IMAGE_BLIT;
    uint32_t surface_instance = inherited_surface ?
        s->active_surface[channel] : object->surface;
    NV15GraphicsObject *surfaces;
    NV15ChannelContext *pattern = nv15_get_channel_context(
        s, channel, object, object->pattern, NV15_CONTEXT_EXPLICIT_PATTERN,
        NV15_CLASS_IMAGE_PATTERN, native_contexts);
    NV15ChannelContext *rop_context = nv15_get_channel_context(
        s, channel, object, object->rop, NV15_CONTEXT_EXPLICIT_ROP,
        NV15_CLASS_CONTEXT_ROP, native_contexts);
    bool read_destination =
        nv15_operation_reads_destination(object, rop_context);
    NV15Surface source;
    NV15Surface destination;
    int32_t source_x = (int16_t)object->point_in;
    int32_t source_y = (int16_t)(object->point_in >> 16);
    int32_t dest_x = (int16_t)object->point_out;
    int32_t dest_y = (int16_t)(object->point_out >> 16);
    uint32_t width = object->size & 0xffff;
    uint32_t height = object->size >> 16;
    uint64_t pixels;
    uint64_t row_bytes;
    uint64_t source_start;
    uint64_t destination_start;
    bool backwards;
    bool wrote = false;
    bool read_source = true;
    bool read_pattern = true;
    bool local;
    uint8_t rop;

    if (!surface_instance) {
        return false;
    }
    surfaces = nv15_find_object(s, surface_instance);
    if (!surfaces) {
        return false;
    }
    nv15_clip_blit(&source_x, &source_y, &dest_x, &dest_y,
                   &width, &height);
    /* Native NV11 BLIT seeds leave clip-enable clear; only 0x188 enables it. */
    if (!nv15_context_clip_blit(s, object, &source_x, &source_y,
                                &dest_x, &dest_y, &width, &height)) {
        return false;
    }
    if (!width || !height) {
        return true;
    }
    if ((uint64_t)width * height > NV15_MAX_2D_PIXELS) {
        return false;
    }
    if (!nv15_surface_load(s, surfaces->instance, true, &source) ||
        !nv15_surface_load(s, surfaces->instance, false, &destination) ||
        source.cpp != destination.cpp ||
        !nv15_surface_rectangle_valid(s, &source, source_x, source_y,
                                      width, height) ||
        !nv15_surface_rectangle_valid(s, &destination, dest_x, dest_y,
                                      width, height) ||
        !nv15_surface_pixel_offset(&source, source_x, source_y,
                                   &source_start) ||
        !nv15_surface_pixel_offset(&destination, dest_x, dest_y,
                                   &destination_start) ||
        source.dma.address > UINT64_MAX - source_start ||
        destination.dma.address > UINT64_MAX - destination_start) {
        return false;
    }

    pixels = (uint64_t)width * height;
    row_bytes = (uint64_t)width * source.cpp;
    if (!nv15_charge_work(budget, pixels * destination.cpp)) {
        return false;
    }
    local = nv15_dma_is_vram(&source.dma) &&
            nv15_dma_is_vram(&destination.dma);
    rop = nv15_operation_rop(object, rop_context);
    if (local) {
        /* DMA/MMIO may change live context state; keep its original reads. */
        read_source = ((rop ^ (rop >> 2)) & 0x33) != 0;
        read_pattern = ((rop ^ (rop >> 4)) & 0x0f) != 0;
    }
    source_start += source.dma.address;
    destination_start += destination.dma.address;
    if (read_source &&
        (source.dma.target == NV15_DMA_TARGET_VRAM_TILED ||
         destination.dma.target == NV15_DMA_TARGET_VRAM_TILED ||
         source.pitch != destination.pitch ||
         source.pitch < row_bytes || destination.pitch < row_bytes) &&
        nv15_blit_rectangles_overlap(s, &source, source_start, &destination,
                                     destination_start, width, height)) {
        uint32_t iy;

        source_snapshot = g_malloc(pixels * source.cpp);
        for (iy = 0; iy < height; iy++) {
            uint64_t source_offset;

            if (!nv15_surface_pixel_offset(&source, source_x, source_y + iy,
                                           &source_offset) ||
                !nv15_dma_read(s, &source.dma, source_offset,
                               source_snapshot + iy * row_bytes,
                               row_bytes)) {
                return false;
            }
        }
    }
    backwards = nv15_dma_share_backing(&source.dma, &destination.dma) &&
                source_start < destination_start;
    if (local && rop == 0xcc && source.pitch >= row_bytes &&
        destination.pitch >= row_bytes) {
        if (!source_snapshot && source.pitch == row_bytes &&
            destination.pitch == row_bytes &&
            (source_start + row_bytes * height <= destination_start ||
             destination_start + row_bytes * height <= source_start)) {
            memcpy(s->vga.vram_ptr + destination_start,
                   s->vga.vram_ptr + source_start, row_bytes * height);
            nv15_surface_mark_dirty(s, &destination, dest_x, dest_y,
                                     width, height);
            nv15_graphic_damage(s);
            return true;
        }
        for (uint32_t row = 0; row < height; row++) {
            uint32_t iy = backwards ? height - 1 - row : row;
            const uint8_t *src = source_snapshot ?
                source_snapshot + (uint64_t)iy * row_bytes :
                s->vga.vram_ptr + source_start + (uint64_t)iy * source.pitch;
            uint8_t *dst = s->vga.vram_ptr + destination_start +
                           (uint64_t)iy * destination.pitch;

            memmove(dst, src, row_bytes);
        }
        nv15_surface_mark_dirty(s, &destination, dest_x, dest_y, width, height);
        nv15_graphic_damage(s);
        return true;
    }
    if (local) {
        NV15RopPixels operation = {
            .object = object, .rop = rop_context, .pattern = pattern,
            .source = source_snapshot ? source_snapshot :
                      s->vga.vram_ptr + source_start,
            .destination = s->vga.vram_ptr + destination_start,
            .source_pitch = source_snapshot ? row_bytes : source.pitch,
            .destination_pitch = destination.pitch,
            .x = dest_x, .y = dest_y, .width = width, .height = height,
            .backwards = backwards, .read_source = read_source,
            .read_destination = read_destination, .read_pattern = read_pattern,
        };

        nv15_rop_pixels(&operation, destination.cpp);
        nv15_surface_mark_dirty(s, &destination, dest_x, dest_y, width, height);
        nv15_graphic_damage(s);
        return true;
    }
    for (uint32_t row = 0; row < height; row++) {
        uint32_t iy = backwards ? height - 1 - row : row;

        for (uint32_t col = 0; col < width; col++) {
            uint32_t ix = backwards ? width - 1 - col : col;
            uint32_t source_pixel = 0;
            uint32_t destination_pixel = 0;
            uint32_t pattern_pixel;
            uint32_t result;

            if (source_snapshot) {
                const uint8_t *bytes = source_snapshot +
                    (uint64_t)iy * row_bytes + (uint64_t)ix * source.cpp;

                source_pixel = source.cpp == 1 ? bytes[0] :
                               source.cpp == 2 ? lduw_le_p(bytes) :
                               ldl_le_p(bytes);
            } else if (read_source &&
                       !nv15_surface_read_pixel(s, &source, source_x + ix,
                                                source_y + iy, &source_pixel)) {
                goto fail;
            }
            if (read_destination &&
                !nv15_surface_read_pixel(s, &destination, dest_x + ix,
                                         dest_y + iy, &destination_pixel)) {
                goto fail;
            }
            pattern_pixel = read_pattern ?
                nv15_pattern_pixel(pattern, dest_x + ix, dest_y + iy) : 0;
            result = nv15_apply_operation(object, rop_context, pattern_pixel,
                                          source_pixel, destination_pixel,
                                          destination.cpp);
            if (!nv15_surface_write_pixel(s, &destination, dest_x + ix,
                                          dest_y + iy, result, false)) {
                goto fail;
            }
            if (!wrote) {
                nv15_surface_mark_dirty(s, &destination, dest_x, dest_y,
                                        width, height);
                wrote = true;
            }
        }
    }
    nv15_graphic_damage(s);
    return true;

fail:
    if (wrote) {
        nv15_graphic_damage(s);
    }
    return false;
}

static bool nv15_cpu_image_surface(NVIDIAQuadro2State *s,
                                    NV15GraphicsObject *object,
                                    NV15UploadCache *result)
{
    NV15UploadCache *cache = &s->upload_cache;
    NV15GraphicsObject *surface = nv15_find_object(s, object->surface);
    uint32_t key[ARRAY_SIZE(cache->key)];
    uint32_t dma_instance;
    uint32_t width;
    uint32_t height;
    unsigned int cpp;

    if (!surface || surface->class_id != NV15_CLASS_CONTEXT_SURFACES_2D) {
        cache->valid = false;
        return false;
    }
    dma_instance = surface->dma_dest;
    if (!dma_instance || !nv15_ramin_range_valid(dma_instance, 12)) {
        cache->valid = false;
        return false;
    }
    key[0] = object->format;
    key[1] = object->upload_size_out;
    key[2] = object->upload_size_in;
    key[3] = object->surface;
    key[4] = surface->format;
    key[5] = surface->pitch;
    key[6] = surface->offset_dest;
    key[7] = dma_instance;
    /*
     * RAMIN aliases ordinary VRAM, which CPU and DMA can write directly.
     * Validate the raw descriptor on every word, including cache hits.
     */
    key[8] = nv15_ramin_read32(s, dma_instance);
    key[9] = nv15_ramin_read32(s, dma_instance + 4);
    key[10] = nv15_ramin_read32(s, dma_instance + 8);
    if (cache->valid && !memcmp(key, cache->key, sizeof(key))) {
        *result = *cache;
        return true;
    }
    cache->valid = false;
    width = key[1] & 0xffff;
    height = key[1] >> 16;
    if (!width || !height || (uint64_t)width * height > NV15_MAX_2D_PIXELS) {
        width = key[2] & 0xffff;
        height = key[2] >> 16;
    }
    cpp = key[0] <= 3 ? 2 : key[0] <= 5 ? 4 : 0;
    if (!cpp || !width || !height ||
        (uint64_t)width * height > NV15_MAX_2D_PIXELS ||
        nv15_surface_cpp(key[4]) != cpp || !(key[5] >> 16) ||
        !nv15_dma_object_decode(key[8], key[9], key[10], true,
                                 &cache->destination.dma)) {
        return false;
    }
    cache->destination.format = key[4] & 0xff;
    cache->destination.cpp = cpp;
    cache->destination.pitch = key[5] >> 16;
    cache->destination.offset = key[6];
    memcpy(cache->key, key, sizeof(key));
    cache->width = width;
    cache->height = height;
    cache->cpp = cpp;
    cache->valid = true;
    *result = *cache;
    return true;
}

static bool nv15_cpu_image_data(NVIDIAQuadro2State *s,
                                NV15GraphicsObject *object, uint32_t value,
                                NV15WorkBudget *budget)
{
    NV15Surface destination;
    NV15UploadCache upload;
    uint64_t total;
    uint64_t remaining;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_word;
    uint32_t cpp;
    uint32_t i;
    bool wrote = false;

    if (!nv15_cpu_image_surface(s, object, &upload)) {
        return false;
    }
    destination = upload.destination;
    width = upload.width;
    height = upload.height;
    cpp = upload.cpp;
    pixels_per_word = 4 / cpp;
    total = (uint64_t)width * height;
    if (object->upload_pixel >= total) {
        return true;
    }
    remaining = total - object->upload_pixel;
    if (!nv15_charge_work(budget,
                          MIN((uint64_t)pixels_per_word, remaining) * cpp)) {
        return false;
    }
    if (cpp == 2 && remaining >= 2 && nv15_dma_is_vram(&destination.dma) &&
        object->upload_pixel % width + 1 < width) {
        int32_t x = (int16_t)object->upload_point +
                    (int32_t)(object->upload_pixel % width);
        int32_t y = (int16_t)(object->upload_point >> 16) +
                    (int32_t)(object->upload_pixel / width);
        uint64_t offset;

        if (nv15_surface_pixel_offset(&destination, x, y, &offset) &&
            nv15_dma_backing_range_valid(s, &destination.dma, offset, 4)) {
            uint64_t address = destination.dma.address + offset;

            stl_le_p(s->vga.vram_ptr + address, value);
            memory_region_set_dirty(&s->vga.vram, address, 4);
            object->upload_pixel += 2;
            nv15_graphic_damage(s);
            return true;
        }
    }
    for (i = 0; i < pixels_per_word; i++) {
        uint32_t position = object->upload_pixel++;
        int32_t x;
        int32_t y;
        uint32_t pixel;

        if (position >= total) {
            break;
        }
        x = (int16_t)object->upload_point + (int32_t)(position % width);
        y = (int16_t)(object->upload_point >> 16) +
            (int32_t)(position / width);
        pixel = cpp == 2 ? (value >> (i * 16)) & 0xffff : value;
        if (x >= 0 && y >= 0 &&
            !nv15_surface_write_pixel(s, &destination, x, y, pixel, true)) {
            goto fail;
        }
        if (x >= 0 && y >= 0) {
            wrote = true;
        }
    }
    nv15_graphic_damage(s);
    return true;

fail:
    if (wrote) {
        nv15_graphic_damage(s);
    }
    return false;
}

#define NV15_SCALED_FIXED_ONE (UINT32_C(1) << 20)

static uint8_t nv15_expand_5(uint32_t value)
{
    return (value << 3) | (value >> 2);
}

static uint8_t nv15_expand_6(uint32_t value)
{
    return (value << 2) | (value >> 4);
}

static uint8_t nv15_argb_luma(uint32_t argb)
{
    uint32_t red = (argb >> 16) & 0xff;
    uint32_t green = (argb >> 8) & 0xff;
    uint32_t blue = argb & 0xff;

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

static unsigned int nv15_scaled_source_cpp(uint32_t format)
{
    switch (format & 0xff) {
    case 1:
    case 2:
    case 5:
    case 6:
    case 7:
    case 9:
        return 2;
    case 3:
    case 4:
        return 4;
    case 8:
        return 1;
    default:
        return 0;
    }
}

static uint32_t nv15_yuv_to_argb(uint8_t y, uint8_t u, uint8_t v)
{
    int32_t c = (int32_t)y - 16;
    int32_t d = (int32_t)u - 128;
    int32_t e = (int32_t)v - 128;
    int32_t red = (298 * c + 409 * e + 128) >> 8;
    int32_t green = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int32_t blue = (298 * c + 516 * d + 128) >> 8;

    return UINT32_C(0xff000000) |
           (uint32_t)CLAMP(red, 0, 255) << 16 |
           (uint32_t)CLAMP(green, 0, 255) << 8 |
           CLAMP(blue, 0, 255);
}

typedef struct NV15ScaledCacheEntry {
    uint64_t offset;
    uint64_t sample;
    uint8_t bytes[4];
    uint32_t argb[2];
    uint8_t decoded;
} NV15ScaledCacheEntry;

typedef struct NV15ScaledCache {
    NV15ScaledCacheEntry entry[4];
    unsigned int count;
    unsigned int next;
    uint64_t sample;
} NV15ScaledCache;

typedef struct NV15ScaledAxis {
    int32_t first;
    int32_t second;
    uint32_t fraction;
} NV15ScaledAxis;

static bool nv15_scaled_decode_source(uint32_t source_format, int32_t x,
                                      const uint8_t bytes[4], uint32_t *argb)
{
    unsigned int cpp = nv15_scaled_source_cpp(source_format);
    bool packed_yuv = (source_format & 0xff) == 5 ||
                      (source_format & 0xff) == 6;
    uint32_t raw;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t alpha = 0xff;

    if (packed_yuv) {
        uint8_t y_sample;
        uint8_t u;
        uint8_t v;

        if ((source_format & 0xff) == 5) {
            y_sample = bytes[(x & 1) ? 2 : 0];
            u = bytes[1];
            v = bytes[3];
        } else {
            y_sample = bytes[(x & 1) ? 3 : 1];
            u = bytes[0];
            v = bytes[2];
        }
        *argb = nv15_yuv_to_argb(y_sample, u, v);
        return true;
    }

    raw = cpp == 1 ? bytes[0] :
          cpp == 2 ? lduw_le_p(bytes) : ldl_le_p(bytes);
    switch (source_format & 0xff) {
    case 1: /* A1R5G5B5 */
        alpha = raw & BIT(15) ? 0xff : 0;
        red = nv15_expand_5((raw >> 10) & 0x1f);
        green = nv15_expand_5((raw >> 5) & 0x1f);
        blue = nv15_expand_5(raw & 0x1f);
        break;
    case 2: /* X1R5G5B5 */
        red = nv15_expand_5((raw >> 10) & 0x1f);
        green = nv15_expand_5((raw >> 5) & 0x1f);
        blue = nv15_expand_5(raw & 0x1f);
        break;
    case 3: /* A8R8G8B8 */
        *argb = raw;
        return true;
    case 4: /* X8R8G8B8 */
        *argb = raw | 0xff000000U;
        return true;
    case 7: /* R5G6B5 */
        red = nv15_expand_5((raw >> 11) & 0x1f);
        green = nv15_expand_6((raw >> 5) & 0x3f);
        blue = nv15_expand_5(raw & 0x1f);
        break;
    case 8: /* Y8 */
        red = green = blue = raw;
        break;
    case 9: /* A8Y8 */
        alpha = raw >> 8;
        red = green = blue = raw & 0xff;
        break;
    default:
        return false;
    }
    *argb = alpha << 24 | red << 16 | green << 8 | blue;
    return true;
}

static bool nv15_scaled_read_source(NVIDIAQuadro2State *s,
                                    const NV15DMAObject *source,
                                    uint32_t source_offset,
                                    uint32_t source_pitch,
                                    uint32_t source_format,
                                    uint32_t source_width,
                                    uint32_t source_height,
                                    int32_t x, int32_t y, uint32_t *argb,
                                    NV15ScaledCache *cache)
{
    uint8_t bytes[4] = { 0 };
    unsigned int cpp = nv15_scaled_source_cpp(source_format);
    bool packed_yuv = (source_format & 0xff) == 5 ||
                      (source_format & 0xff) == 6;
    unsigned int component = packed_yuv ? x & 1 : 0;
    NV15ScaledCacheEntry *entry = NULL;
    uint64_t row;
    uint64_t column;
    uint64_t offset;

    if (!cpp || x < 0 || y < 0 || x >= source_width || y >= source_height ||
        umul64_overflow((uint64_t)y, source_pitch, &row) ||
        umul64_overflow((uint64_t)(packed_yuv ? x & ~1 : x), cpp, &column) ||
        uadd64_overflow(source_offset, row, &offset) ||
        uadd64_overflow(offset, column, &offset)) {
        return false;
    }
    if (cache) {
        for (unsigned int i = 0; i < cache->count; i++) {
            if (cache->entry[i].offset == offset) {
                entry = &cache->entry[i];
                break;
            }
        }
    }
    if (entry && entry->sample == cache->sample) {
        if (entry->decoded & BIT(component)) {
            *argb = entry->argb[component];
            return true;
        }
        memcpy(bytes, entry->bytes, sizeof(bytes));
    } else {
        if (!nv15_dma_read(s, source, offset, bytes, packed_yuv ? 4 : cpp)) {
            return false;
        }
        if (cache) {
            if (!entry) {
                unsigned int index = cache->count < ARRAY_SIZE(cache->entry) ?
                    cache->count++ : cache->next++ % ARRAY_SIZE(cache->entry);

                entry = &cache->entry[index];
                entry->offset = offset;
                entry->decoded = 0;
            } else if (memcmp(entry->bytes, bytes, sizeof(bytes))) {
                entry->decoded = 0;
            }
            entry->sample = cache->sample;
            memcpy(entry->bytes, bytes, sizeof(bytes));
            if (entry->decoded & BIT(component)) {
                *argb = entry->argb[component];
                return true;
            }
        }
    }
    if (!nv15_scaled_decode_source(source_format, x, bytes, argb)) {
        return false;
    }
    if (entry) {
        entry->argb[component] = *argb;
        entry->decoded |= BIT(component);
    }
    return true;
}

static uint8_t nv15_scaled_lerp(uint8_t a, uint8_t b, uint32_t fraction)
{
    return ((uint64_t)a * (NV15_SCALED_FIXED_ONE - fraction) +
            (uint64_t)b * fraction + NV15_SCALED_FIXED_ONE / 2) >> 20;
}

static uint32_t nv15_scaled_lerp_argb(uint32_t a, uint32_t b,
                                      uint32_t fraction)
{
    uint32_t result = 0;
    unsigned int shift;

    for (shift = 0; shift < 32; shift += 8) {
        result |= (uint32_t)nv15_scaled_lerp(a >> shift, b >> shift,
                                             fraction) << shift;
    }
    return result;
}

static int32_t nv15_scaled_fixed_floor(int64_t value)
{
    if (value >= 0) {
        return value / NV15_SCALED_FIXED_ONE;
    }
    return -DIV_ROUND_UP(-value, NV15_SCALED_FIXED_ONE);
}

static NV15ScaledAxis nv15_scaled_axis(int64_t value, uint32_t size,
                                        bool bilinear, bool corner_origin)
{
    NV15ScaledAxis axis;
    int32_t first;

    if (bilinear && corner_origin) {
        value -= NV15_SCALED_FIXED_ONE / 2;
    }
    first = nv15_scaled_fixed_floor(value);
    axis.first = bilinear ? CLAMP(first, 0, (int32_t)size - 1) : first;
    axis.second = bilinear ? CLAMP(first + 1, 0, (int32_t)size - 1) : first;
    axis.fraction = value - (int64_t)first * NV15_SCALED_FIXED_ONE;
    return axis;
}

static bool nv15_scaled_sample(NVIDIAQuadro2State *s,
                               const NV15DMAObject *source,
                               uint32_t source_offset,
                               uint32_t source_pitch,
                               uint32_t source_format,
                               uint32_t source_width,
                               uint32_t source_height,
                               int64_t u, int64_t v, bool bilinear,
                               bool corner_origin, uint32_t *argb,
                               NV15ScaledCache *shared_cache,
                               const NV15ScaledAxis *horizontal,
                               const NV15ScaledAxis *vertical)
{
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint32_t fraction_x;
    uint32_t fraction_y;
    uint32_t p00;
    uint32_t p10;
    uint32_t p01;
    uint32_t p11;
    NV15ScaledCache local_cache = { 0 };
    NV15ScaledCache *cache = nv15_dma_is_vram(source) ?
                            (shared_cache ? shared_cache : &local_cache) : NULL;
    NV15ScaledAxis local_horizontal;
    NV15ScaledAxis local_vertical;

    if (cache) {
        cache->sample++;
    }
    if (!horizontal) {
        local_horizontal = nv15_scaled_axis(u, source_width,
                                             bilinear, corner_origin);
        horizontal = &local_horizontal;
        local_vertical = nv15_scaled_axis(v, source_height,
                                           bilinear, corner_origin);
        vertical = &local_vertical;
    }

    if (!bilinear) {
        return nv15_scaled_read_source(s, source, source_offset,
                                       source_pitch, source_format,
                                       source_width, source_height,
                                       horizontal->first, vertical->first,
                                       argb, cache);
    }
    x0 = horizontal->first;
    y0 = vertical->first;
    fraction_x = horizontal->fraction;
    fraction_y = vertical->fraction;
    x1 = horizontal->second;
    y1 = vertical->second;
    if (!nv15_scaled_read_source(s, source, source_offset, source_pitch,
                                 source_format, source_width, source_height,
                                 x0, y0, &p00, cache)) {
        return false;
    }
    /* A shared cache is used only for prevalidated local VRAM surfaces. */
    if (shared_cache && !fraction_x) {
        p10 = p00;
    } else if (!nv15_scaled_read_source(s, source, source_offset, source_pitch,
                                 source_format, source_width, source_height,
                                 x1, y0, &p10, cache)) {
        return false;
    }
    if (shared_cache && !fraction_y) {
        *argb = nv15_scaled_lerp_argb(p00, p10, fraction_x);
        return true;
    }
    if (!nv15_scaled_read_source(s, source, source_offset, source_pitch,
                                 source_format, source_width, source_height,
                                 x0, y1, &p01, cache)) {
        return false;
    }
    if (shared_cache && !fraction_x) {
        p11 = p01;
    } else if (!nv15_scaled_read_source(s, source, source_offset, source_pitch,
                                 source_format, source_width, source_height,
                                 x1, y1, &p11, cache)) {
        return false;
    }
    p00 = nv15_scaled_lerp_argb(p00, p10, fraction_x);
    p01 = nv15_scaled_lerp_argb(p01, p11, fraction_x);
    *argb = nv15_scaled_lerp_argb(p00, p01, fraction_y);
    return true;
}

static uint32_t nv15_scaled_encode_destination(const NV15Surface *destination,
                                                uint32_t argb)
{
    uint32_t alpha = argb >> 24;
    uint32_t red = (argb >> 16) & 0xff;
    uint32_t green = (argb >> 8) & 0xff;
    uint32_t blue = argb & 0xff;
    uint32_t luma = nv15_argb_luma(argb);

    switch (destination->format) {
    case 1:
        return luma;
    case 2:
    case 3:
        return BIT(15) | (red >> 3) << 10 | (green >> 3) << 5 |
               (blue >> 3);
    case 4:
        return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3);
    case 5:
        return luma | luma << 8;
    case 6:
    case 7:
        return 0xff000000U | (argb & 0x00ffffffU);
    case 8:
    case 9:
        return BIT(31) | (alpha >> 1) << 24 | (argb & 0x00ffffffU);
    case 10:
        return argb;
    case 11:
        return luma * UINT32_C(0x01010101);
    default:
        return 0;
    }
}

static bool nv15_scaled_image(NVIDIAQuadro2State *s, unsigned int channel,
                              NV15GraphicsObject *object,
                              NV15WorkBudget *budget)
{
    NV15ChannelContext *pattern = nv15_get_channel_context(
        s, channel, object, object->pattern, NV15_CONTEXT_EXPLICIT_PATTERN,
        NV15_CLASS_IMAGE_PATTERN, false);
    NV15ChannelContext *rop_context = nv15_get_channel_context(
        s, channel, object, object->rop, NV15_CONTEXT_EXPLICIT_ROP,
        NV15_CLASS_CONTEXT_ROP, false);
    bool read_destination =
        nv15_operation_reads_destination(object, rop_context);
    NV15DMAObject source;
    NV15Surface destination;
    unsigned int source_cpp = nv15_scaled_source_cpp(object->color_format);
    uint32_t source_width = object->size & 0xffff;
    uint32_t source_height = object->size >> 16;
    uint32_t source_pitch = object->format & 0xffff;
    uint32_t source_row_bytes = source_width * source_cpp;
    uint32_t output_width = object->upload_size_in & 0xffff;
    uint32_t output_height = object->upload_size_in >> 16;
    uint32_t output_x = (uint16_t)object->point_out;
    uint32_t output_y = (uint16_t)(object->point_out >> 16);
    int64_t left = output_x;
    int64_t top = output_y;
    int64_t right = (int64_t)output_x + output_width;
    int64_t bottom = (int64_t)output_y + output_height;
    bool bilinear = (object->format & NV15_SCALED_FILTER_MASK) ==
                    NV15_SCALED_FILTER_BILINEAR;
    bool corner_origin = (object->format & NV15_SCALED_ORIGIN_MASK) ==
                         NV15_SCALED_ORIGIN_CORNER;
    uint64_t source_row;
    uint64_t source_end;
    uint64_t pixels;
    bool wrote = false;
    int64_t y;
    int64_t x;
    g_autofree NV15ScaledAxis *horizontal = NULL;
    NV15ScaledCache cache = { 0 };

    if (!source_cpp || !source_width || (source_width & 1) ||
        !source_height || !source_pitch ||
        !output_width || !output_height ||
        source_row_bytes > source_pitch ||
        !nv15_dma_object_load(s, object->dma_source, &source) ||
        !nv15_surface_load(s, object->surface, false, &destination) ||
        umul64_overflow(source_height - 1, source_pitch, &source_row) ||
        uadd64_overflow(object->offset_source, source_row, &source_end) ||
        uadd64_overflow(source_end, source_row_bytes, &source_end) ||
        !nv15_dma_backing_range_valid(s, &source, 0, source_end)) {
        return false;
    }

    if ((object->upload_size_out & 0xffff) &&
        (object->upload_size_out >> 16)) {
        uint32_t clip_x = (uint16_t)object->upload_point;
        uint32_t clip_y = (uint16_t)(object->upload_point >> 16);
        uint32_t clip_width = object->upload_size_out & 0xffff;
        uint32_t clip_height = object->upload_size_out >> 16;

        left = MAX(left, clip_x);
        top = MAX(top, clip_y);
        right = MIN(right, (int64_t)clip_x + clip_width);
        bottom = MIN(bottom, (int64_t)clip_y + clip_height);
    }
    if (left >= right || top >= bottom || right > INT32_MAX ||
        bottom > INT32_MAX ||
        (uint64_t)right * destination.cpp > destination.pitch ||
        !nv15_surface_rectangle_valid(s, &destination, left, top,
                                      right - left, bottom - top)) {
        return false;
    }
    pixels = (right - left) * (bottom - top);
    if (pixels > NV15_MAX_2D_PIXELS ||
        !nv15_charge_work(budget, pixels *
                          (destination.cpp + source_cpp *
                           (bilinear ? 4 : 1)))) {
        return false;
    }

    if (nv15_dma_is_vram(&source) && nv15_dma_is_vram(&destination.dma)) {
        horizontal = g_new(NV15ScaledAxis, right - left);
        for (x = left; x < right; x++) {
            int64_t u = (int64_t)(int16_t)object->point_in * UINT32_C(65536) +
                        (x - output_x) * (int32_t)object->m2mf_pitch_in;

            horizontal[x - left] = nv15_scaled_axis(u, source_width,
                                                     bilinear, corner_origin);
        }
    }
    for (y = top; y < bottom; y++) {
        int64_t source_v = (int64_t)(int16_t)(object->point_in >> 16) *
                           UINT32_C(65536);
        NV15ScaledAxis vertical;

        source_v += (y - output_y) * (int32_t)object->m2mf_pitch_out;
        if (horizontal) {
            vertical = nv15_scaled_axis(source_v, source_height,
                                         bilinear, corner_origin);
        }
        for (x = left; x < right; x++) {
            int64_t source_u = 0;
            uint32_t argb;
            uint32_t source_pixel;
            uint32_t destination_pixel = 0;
            uint32_t pattern_pixel;
            uint32_t result;

            if (!horizontal) {
                source_u = (int64_t)(int16_t)object->point_in *
                           UINT32_C(65536) +
                           (x - output_x) * (int32_t)object->m2mf_pitch_in;
            }
            if (!nv15_scaled_sample(s, &source, object->offset_source,
                                    source_pitch, object->color_format,
                                    source_width, source_height,
                                    source_u, source_v, bilinear,
                                    corner_origin, &argb,
                                    horizontal ? &cache : NULL,
                                    horizontal ? &horizontal[x - left] : NULL,
                                    horizontal ? &vertical : NULL)) {
                goto fail;
            }
            source_pixel = nv15_scaled_encode_destination(&destination,
                                                           argb);
            if (read_destination &&
                !nv15_surface_read_pixel(s, &destination, x, y,
                                         &destination_pixel)) {
                goto fail;
            }
            pattern_pixel = nv15_pattern_pixel(pattern, x, y);
            result = nv15_apply_operation(object, rop_context, pattern_pixel,
                                          source_pixel, destination_pixel,
                                          destination.cpp);
            if (!nv15_surface_write_pixel(s, &destination, x, y, result,
                                          false)) {
                goto fail;
            }
            if (!wrote) {
                nv15_surface_mark_dirty(s, &destination, left, top,
                                        right - left, bottom - top);
                wrote = true;
            }
        }
    }
    nv15_graphic_damage(s);
    return true;

fail:
    if (wrote) {
        nv15_graphic_damage(s);
    }
    return false;
}

static bool nv15_m2mf_format_valid(uint32_t value)
{
    uint32_t input_increment = value & 0xff;
    uint32_t output_increment = (value >> 8) & 0xff;

    return !(value & ~0x7ffU) &&
           (input_increment == 1 || input_increment == 2 ||
            input_increment == 4) &&
           (output_increment == 1 || output_increment == 2 ||
            output_increment == 4);
}

static bool nv15_m2mf_read_line(NVIDIAQuadro2State *s,
                                const NV15DMAObject *dma, uint64_t offset,
                                uint8_t *buffer, uint32_t count,
                                uint32_t increment)
{
    uint64_t span = (uint64_t)(count - 1) * increment + 1;
    uint32_t i;

    if (increment == 1) {
        return nv15_dma_read(s, dma, offset, buffer, count);
    }
    if (!nv15_dma_backing_range_valid(s, dma, offset, span)) {
        return false;
    }
    if (dma->target == NV15_DMA_TARGET_VRAM) {
        const uint8_t *source = s->vga.vram_ptr + dma->address + offset;

        for (i = 0; i < count; i++) {
            buffer[i] = source[(uint64_t)i * increment];
        }
        return true;
    }
    for (i = 0; i < count; i++) {
        if (!nv15_dma_read(s, dma, offset + (uint64_t)i * increment,
                           &buffer[i], 1)) {
            return false;
        }
    }
    return true;
}

static bool nv15_m2mf_write_line(NVIDIAQuadro2State *s,
                                 const NV15DMAObject *dma, uint64_t offset,
                                 const uint8_t *buffer, uint32_t count,
                                 uint32_t increment)
{
    uint64_t span = (uint64_t)(count - 1) * increment + 1;
    uint32_t i;

    if (increment == 1) {
        return nv15_dma_write(s, dma, offset, buffer, count, true);
    }
    if (!nv15_dma_backing_range_valid(s, dma, offset, span)) {
        return false;
    }
    if (dma->target == NV15_DMA_TARGET_VRAM) {
        uint64_t address = dma->address + offset;
        uint8_t *destination = s->vga.vram_ptr + address;

        for (i = 0; i < count; i++) {
            destination[(uint64_t)i * increment] = buffer[i];
        }
        memory_region_set_dirty(&s->vga.vram, address, span);
        return true;
    }
    for (i = 0; i < count; i++) {
        if (!nv15_dma_write(s, dma, offset + (uint64_t)i * increment,
                            &buffer[i], 1, true)) {
            return false;
        }
    }
    return true;
}

static bool nv15_m2mf_copy(NVIDIAQuadro2State *s,
                           NV15GraphicsObject *object,
                           NV15WorkBudget *budget)
{
    NV15DMAObject input;
    NV15DMAObject output;
    g_autofree uint8_t *buffer = NULL;
    uint32_t line_length = object->m2mf_line_length;
    uint32_t line_count = object->m2mf_line_count;
    uint32_t input_increment = object->m2mf_format & 0xff;
    uint32_t output_increment = (object->m2mf_format >> 8) & 0xff;
    uint64_t input_row_span;
    uint64_t output_row_span;
    uint64_t input_extent;
    uint64_t output_extent;
    uint64_t total;
    uint64_t stride;
    bool wrote = false;
    uint32_t i;

    if (!line_length || !line_count ||
        !nv15_m2mf_format_valid(object->m2mf_format) ||
        umul64_overflow(line_length, line_count, &total) ||
        total > NV15_MAX_2D_PIXELS * 4ULL ||
        !nv15_dma_object_load(s, object->dma_source, &input) ||
        !nv15_dma_readable(&input) ||
        !nv15_dma_object_load(s, object->dma_dest, &output) ||
        !nv15_dma_writable(&output) ||
        !nv15_charge_work(budget, total) ||
        umul64_overflow(line_length - 1, input_increment,
                       &input_row_span) ||
        uadd64_overflow(input_row_span, 1, &input_row_span) ||
        umul64_overflow(line_length - 1, output_increment,
                       &output_row_span) ||
        uadd64_overflow(output_row_span, 1, &output_row_span) ||
        umul64_overflow(line_count - 1, object->m2mf_pitch_in, &stride) ||
        uadd64_overflow(stride, input_row_span, &input_extent) ||
        !nv15_dma_backing_range_valid(s, &input, object->offset_source,
                                      input_extent) ||
        umul64_overflow(line_count - 1, object->m2mf_pitch_out, &stride) ||
        uadd64_overflow(stride, output_row_span, &output_extent) ||
        !nv15_dma_backing_range_valid(s, &output, object->offset_dest,
                                      output_extent)) {
        return false;
    }
    if (input_increment == 1 && output_increment == 1 &&
        nv15_dma_is_vram(&input) && nv15_dma_is_vram(&output)) {
        uint64_t source = input.address + object->offset_source;
        uint64_t dest = output.address + object->offset_dest;

        if (source + input_extent <= dest || dest + output_extent <= source) {
            if (object->m2mf_pitch_in == line_length &&
                object->m2mf_pitch_out == line_length) {
                memcpy(s->vga.vram_ptr + dest, s->vga.vram_ptr + source, total);
                memory_region_set_dirty(&s->vga.vram, dest, total);
            } else {
                for (i = 0; i < line_count; i++) {
                    memcpy(s->vga.vram_ptr + dest, s->vga.vram_ptr + source,
                           line_length);
                    memory_region_set_dirty(&s->vga.vram, dest, line_length);
                    source += object->m2mf_pitch_in;
                    dest += object->m2mf_pitch_out;
                }
            }
            nv15_graphic_damage(s);
            return true;
        }
    }
    buffer = g_try_malloc(total);
    if (!buffer) {
        return false;
    }

    /* Snapshot all input columns so overlapping 2D copies stay coherent. */
    for (i = 0; i < line_count; i++) {
        uint64_t source = object->offset_source +
                          (uint64_t)i * object->m2mf_pitch_in;

        if (!nv15_m2mf_read_line(s, &input, source,
                                 buffer + (uint64_t)i * line_length,
                                 line_length, input_increment)) {
            return false;
        }
    }
    for (i = 0; i < line_count; i++) {
        uint64_t dest = object->offset_dest +
                        (uint64_t)i * object->m2mf_pitch_out;

        if (!nv15_m2mf_write_line(s, &output, dest,
                                  buffer + (uint64_t)i * line_length,
                                  line_length, output_increment)) {
            goto fail;
        }
        wrote = true;
    }
    nv15_graphic_damage(s);
    return true;

fail:
    if (wrote) {
        nv15_graphic_damage(s);
    }
    return false;
}

static bool nv15_write_notifier_instance(NVIDIAQuadro2State *s,
                                         uint32_t dma_instance,
                                         uint32_t offset, uint32_t info,
                                         bool awaken)
{
    NV15DMAObject notify;
    uint32_t record[4];
    uint64_t timestamp = nv15_timer_value(s);

    if (!dma_instance ||
        !nv15_dma_object_load(s, dma_instance, &notify) ||
        !nv15_dma_writable(&notify)) {
        return false;
    }
    record[0] = cpu_to_le32((timestamp & MAKE_64BIT_MASK(0, 27)) << 5);
    record[1] = cpu_to_le32(timestamp >> 27);
    record[2] = cpu_to_le32(info);
    record[3] = 0;
    if (!nv15_dma_write(s, &notify, offset, record, sizeof(record), true)) {
        return false;
    }
    if (awaken) {
        s->pgraph[(NV15_PGRAPH_INTR - NV15_PGRAPH_BASE) >> 2] |= BIT(0);
        nv15_update_irq(s);
    }
    return true;
}

static bool nv15_write_notifier(NVIDIAQuadro2State *s,
                                NV15GraphicsObject *object,
                                uint32_t offset, uint32_t info, bool awaken)
{
    return nv15_write_notifier_instance(s, object->dma_notify, offset, info,
                                        awaken);
}

static bool nv15_notifier_instance_valid(NVIDIAQuadro2State *s,
                                         uint32_t dma_instance,
                                         uint32_t offset)
{
    NV15DMAObject notify;

    return dma_instance &&
           nv15_dma_object_load(s, dma_instance, &notify) &&
           nv15_dma_writable(&notify) &&
           nv15_dma_backing_range_valid(s, &notify, offset, 16);
}

static bool nv15_notifier_valid(NVIDIAQuadro2State *s,
                                NV15GraphicsObject *object,
                                uint32_t offset)
{
    return nv15_notifier_instance_valid(s, object->dma_notify, offset);
}

static bool nv15_set_object_reference(NVIDIAQuadro2State *s,
                                      unsigned int channel, uint32_t handle,
                                      uint32_t expected_engine,
                                      uint32_t *field)
{
    NV15DMAObject dma;
    uint32_t instance;

    if (!nv15_resolve_handle(s, channel, handle, expected_engine,
                             &instance)) {
        return false;
    }
    if (instance &&
        ((expected_engine == NV15_RAMHT_ENGINE_DMA &&
          !nv15_dma_object_load(s, instance, &dma)) ||
         (expected_engine == NV15_RAMHT_ENGINE_GRAPHICS &&
          !nv15_get_object(s, instance)))) {
        return false;
    }
    *field = instance;
    return true;
}

static bool nv15_set_surface_dma_reference(NVIDIAQuadro2State *s,
                                            unsigned int channel,
                                            uint32_t handle, uint32_t *field)
{
    NV15DMAObject dma;
    uint32_t instance;

    if (!nv15_resolve_handle(s, channel, handle, NV15_RAMHT_ENGINE_DMA,
                             &instance) ||
        (instance && !nv15_dma_object_load_internal(s, instance, true,
                                                     &dma))) {
        return false;
    }
    *field = instance;
    return true;
}

static bool nv15_set_context_reference(NVIDIAQuadro2State *s,
                                        unsigned int channel,
                                        NV15GraphicsObject *object,
                                        uint32_t handle, uint32_t *field,
                                        uint8_t explicit_mask,
                                        uint32_t class_id)
{
    NV15GraphicsObject *context;
    uint32_t instance;

    if (!nv15_set_object_reference(s, channel, handle,
                                   NV15_RAMHT_ENGINE_GRAPHICS, &instance)) {
        return false;
    }
    context = nv15_find_object(s, instance);
    if (instance && (!context || context->class_id != class_id)) {
        return false;
    }
    *field = instance;
    s->context_explicit[object - s->objects] |= explicit_mask;
    return true;
}

static bool nv15_set_surface_reference(NVIDIAQuadro2State *s,
                                       unsigned int channel,
                                       NV15GraphicsObject *object,
                                       uint32_t handle)
{
    if (!nv15_set_object_reference(s, channel, handle,
                                   NV15_RAMHT_ENGINE_GRAPHICS,
                                   &object->surface)) {
        return false;
    }
    s->surface_explicit[object - s->objects] = true;
    return true;
}

/* TODO: Implement the NV10/NV15 3D object classes. */
static bool nv15_graphics_method(NVIDIAQuadro2State *s,
                                 unsigned int channel,
                                 NV15GraphicsObject *object,
                                 uint32_t method, uint32_t value,
                                 NV15WorkBudget *budget, bool *unknown)
{
    *unknown = false;
    if (method == NV15_GRAPH_NOP) {
        return true;
    }
    if (method == NV15_GRAPH_WAIT_FOR_IDLE) {
        /* FIFO ordering guarantees that earlier primitives have completed. */
        return true;
    }
    if (method == NV15_GRAPH_DMA_NOTIFY) {
        return nv15_set_object_reference(s, channel, value,
                                         NV15_RAMHT_ENGINE_DMA,
                                         &object->dma_notify);
    }

    switch (object->class_id) {
    case NV15_CLASS_CONTEXT_SURFACES_2D:
        switch (method) {
        case NV15_SURFACE_DMA_SOURCE:
            return nv15_set_surface_dma_reference(s, channel, value,
                                                  &object->dma_source);
        case NV15_SURFACE_DMA_DEST:
            return nv15_set_surface_dma_reference(s, channel, value,
                                                  &object->dma_dest);
        case NV15_SURFACE_FORMAT:
            object->format = value;
            return true;
        case NV15_SURFACE_PITCH:
            object->pitch = value;
            return true;
        case NV15_SURFACE_OFFSET_SOURCE:
            object->offset_source = value;
            return true;
        case NV15_SURFACE_OFFSET_DEST:
            object->offset_dest = value;
            return true;
        default:
            break;
        }
        break;
    case NV15_CLASS_CONTEXT_ROP:
        if (method == NV15_ROP_VALUE) {
            object->color = value & 0xff;
            s->channel_context[channel].rop = value & 0xff;
            return true;
        }
        break;
    case NV15_CLASS_IMAGE_PATTERN:
        switch (method) {
        case NV15_PATTERN_FORMAT:
            object->color_format = value;
            s->channel_context[channel].color_format = value;
            return true;
        case NV15_PATTERN_MONO_FORMAT:
            object->mono_format = value;
            s->channel_context[channel].mono_format = value;
            return true;
        case NV15_PATTERN_MONO_SHAPE:
            object->mono_shape = value;
            s->channel_context[channel].mono_shape = value;
            return true;
        case NV15_PATTERN_SELECT:
            object->pattern_select = value;
            s->channel_context[channel].pattern_select = value;
            return true;
        case NV15_PATTERN_MONO_COLOR0:
            object->mono_color[0] = value;
            s->channel_context[channel].mono_color[0] = value;
            return true;
        case NV15_PATTERN_MONO_COLOR1:
            object->mono_color[1] = value;
            s->channel_context[channel].mono_color[1] = value;
            return true;
        case NV15_PATTERN_MONO_BITMAP0:
            object->mono_bitmap[0] = value;
            s->channel_context[channel].mono_bitmap[0] = value;
            return true;
        case NV15_PATTERN_MONO_BITMAP1:
            object->mono_bitmap[1] = value;
            s->channel_context[channel].mono_bitmap[1] = value;
            return true;
        default:
            break;
        }
        break;
    case NV15_CLASS_CONTEXT_CLIP:
        if (method == NV15_CLIP_POINT) {
            object->point_in = value;
            return true;
        }
        if (method == NV15_CLIP_SIZE) {
            object->size = value;
            return true;
        }
        break;
    case NV15_CLASS_LINE:
        switch (method) {
        case NV15_LINE_CLIP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->clip,
                NV15_CONTEXT_EXPLICIT_CLIP, NV15_CLASS_CONTEXT_CLIP);
        case NV15_LINE_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_LINE_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_LINE_SURFACE:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_GRAPH_OPERATION:
            object->operation = value;
            return true;
        case NV15_LINE_FORMAT:
            object->color_format = value;
            return true;
        case NV15_LINE_COLOR:
            object->color = value;
            return true;
        default:
            if (method >= NV15_LINE_POINT_BASE &&
                method < NV15_LINE_POINT_BASE + NV15_LINE_POINT_SIZE) {
                if ((method - NV15_LINE_POINT_BASE) & 4) {
                    return nv15_line_draw_to(s, channel, object,
                                             sextract32(value, 0, 16),
                                             sextract32(value, 16, 16),
                                             budget);
                }
                nv15_line_set_vertex(object,
                                     sextract32(value, 0, 16),
                                     sextract32(value, 16, 16));
                return true;
            }
            if (method >= NV15_LINE_POINT32_BASE &&
                method < NV15_LINE_POINT32_BASE + NV15_LINE_POINT_SIZE) {
                switch ((method - NV15_LINE_POINT32_BASE) & 0xf) {
                case 0:
                    object->size = value;
                    object->format = NV15_LINE_X_VALID;
                    return true;
                case 4:
                    if (!(object->format & NV15_LINE_X_VALID)) {
                        return false;
                    }
                    nv15_line_set_vertex(object, (int32_t)object->size,
                                         (int32_t)value);
                    return true;
                case 8:
                    object->size = value;
                    object->format |= NV15_LINE_X_VALID;
                    return true;
                case 12:
                    if (!(object->format & NV15_LINE_X_VALID)) {
                        return false;
                    }
                    return nv15_line_draw_to(s, channel, object,
                                             (int32_t)object->size,
                                             (int32_t)value, budget);
                default:
                    g_assert_not_reached();
                }
            }
            if (method >= NV15_LINE_POLYLINE_BASE &&
                method < NV15_LINE_POLYLINE_BASE + NV15_LINE_POINT_SIZE) {
                return nv15_line_draw_to(s, channel, object,
                                         sextract32(value, 0, 16),
                                         sextract32(value, 16, 16), budget);
            }
            if (method >= NV15_LINE_POLYLINE32_BASE &&
                method < NV15_LINE_POLYLINE32_BASE +
                         NV15_LINE_POINT_SIZE) {
                if (!((method - NV15_LINE_POLYLINE32_BASE) & 4)) {
                    object->size = value;
                    object->format |= NV15_LINE_X_VALID;
                    return true;
                }
                if (!(object->format & NV15_LINE_X_VALID)) {
                    return false;
                }
                return nv15_line_draw_to(s, channel, object,
                                         (int32_t)object->size,
                                         (int32_t)value, budget);
            }
            if (method >= NV15_LINE_CPOLYLINE_BASE &&
                method < NV15_LINE_CPOLYLINE_BASE +
                         NV15_LINE_POINT_SIZE) {
                if (!((method - NV15_LINE_CPOLYLINE_BASE) & 4)) {
                    object->color = value;
                    return true;
                }
                return nv15_line_draw_to(s, channel, object,
                                         sextract32(value, 0, 16),
                                         sextract32(value, 16, 16), budget);
            }
            break;
        }
        break;
    case NV15_CLASS_GDI_RECTANGLE_TEXT:
        switch (method) {
        case NV15_RECT_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_RECT_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_RECT_SURFACE:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_GRAPH_OPERATION:
            object->operation = value;
            return true;
        case NV15_RECT_FORMAT:
            object->color_format = value;
            return true;
        case NV15_RECT_MONO_FORMAT:
            object->mono_format = value;
            return true;
        case NV15_RECT_COLOR:
            object->color = value;
            return true;
        case NV15_GDI_MONO1_CLIP_POINT0:
            object->point_in = value;
            return true;
        case NV15_GDI_MONO1_CLIP_POINT1:
            object->point_out = value;
            return true;
        case NV15_GDI_MONO1_COLOR:
            object->mono_color[1] = value;
            return true;
        case NV15_GDI_MONO1_SIZE:
            object->upload_size_in = value;
            object->upload_size_out = value;
            return true;
        case NV15_GDI_MONO1_POINT:
            object->upload_point = value;
            object->upload_pixel = 0;
            return true;
        case NV15_GDI_MONO2_CLIP_POINT0:
            object->point_in = value;
            return true;
        case NV15_GDI_MONO2_CLIP_POINT1:
            object->point_out = value;
            return true;
        case NV15_GDI_MONO2_COLOR0:
            object->mono_color[0] = value;
            return true;
        case NV15_GDI_MONO2_COLOR1:
            object->mono_color[1] = value;
            return true;
        case NV15_GDI_MONO2_SIZE_IN:
            object->upload_size_in = value;
            return true;
        case NV15_GDI_MONO2_SIZE_OUT:
            object->upload_size_out = value;
            return true;
        case NV15_GDI_MONO2_POINT:
            object->upload_point = value;
            object->upload_pixel = 0;
            return true;
        default:
            if (method >= NV15_RECT_POINT_BASE &&
                method < NV15_RECT_POINT_BASE + 0x100) {
                if ((method - NV15_RECT_POINT_BASE) & 4) {
                    object->size = value;
                    return nv15_fill_rectangle(s, channel, object,
                                               object->point_in,
                                               object->size, budget);
                }
                object->point_in = value;
                return true;
            }
            if (method >= NV15_GDI_MONO1_DATA_BASE &&
                method < NV15_GDI_MONO1_DATA_BASE +
                         NV15_GDI_MONO_DATA_WORDS * sizeof(uint32_t)) {
                return nv15_gdi_mono_data(s, channel, object, value, true,
                                          budget);
            }
            if (method >= NV15_GDI_MONO2_DATA_BASE &&
                method < NV15_GDI_MONO2_DATA_BASE +
                         NV15_GDI_MONO_DATA_WORDS * sizeof(uint32_t)) {
                return nv15_gdi_mono_data(s, channel, object, value, false,
                                          budget);
            }
            break;
        }
        break;
    case NV15_CLASS_RECTANGLE:
        switch (method) {
        case NV15_SOLID_CLIP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->clip,
                NV15_CONTEXT_EXPLICIT_CLIP, NV15_CLASS_CONTEXT_CLIP);
        case NV15_SOLID_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_SOLID_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_SOLID_SURFACE:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_GRAPH_OPERATION:
            object->operation = value;
            return true;
        case NV15_SOLID_FORMAT:
            object->color_format = value;
            return true;
        case NV15_SOLID_COLOR:
            object->color = value;
            return true;
        default:
            if (method >= NV15_SOLID_POINT_BASE &&
                method < NV15_SOLID_POINT_BASE + 0x80) {
                if ((method - NV15_SOLID_POINT_BASE) & 4) {
                    object->size = value;
                    return nv15_fill_rectangle(s, channel, object,
                                               object->point_in,
                                               object->size, budget);
                }
                object->point_in = value;
                return true;
            }
            break;
        }
        break;
    case NV15_CLASS_IMAGE_BLIT:
    case NV15_CLASS_NV11_IMAGE_BLIT:
        switch (method) {
        case NV15_BLIT_CLIP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->clip,
                NV15_CONTEXT_EXPLICIT_CLIP, NV15_CLASS_CONTEXT_CLIP);
        case NV15_BLIT_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_BLIT_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_BLIT_SURFACES:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_GRAPH_OPERATION:
            object->operation = value;
            return true;
        case NV15_BLIT_POINT_IN:
            object->point_in = value;
            return true;
        case NV15_BLIT_POINT_OUT:
            object->point_out = value;
            return true;
        case NV15_BLIT_SIZE:
            object->size = value;
            return nv15_blit(s, channel, object, budget);
        default:
            break;
        }
        break;
    case NV15_CLASS_IMAGE_FROM_CPU:
    case NV15_CLASS_NV5_IMAGE_FROM_CPU:
    case NV15_CLASS_NV10_IMAGE_FROM_CPU:
        switch (method) {
        case NV15_CPU_IMAGE_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_CPU_IMAGE_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_CPU_IMAGE_SURFACE:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_GRAPH_OPERATION:
            object->operation = value;
            return true;
        case NV15_CPU_IMAGE_FORMAT:
            object->format = value;
            return true;
        case NV15_CPU_IMAGE_POINT:
            object->upload_point = value;
            return true;
        case NV15_CPU_IMAGE_SIZE_OUT:
            object->upload_size_out = value;
            return true;
        case NV15_CPU_IMAGE_SIZE_IN:
            object->upload_size_in = value;
            object->upload_pixel = 0;
            return true;
        default:
            if (method >= NV15_CPU_IMAGE_DATA_BASE && method < 0x2000) {
                return nv15_cpu_image_data(s, object, value, budget);
            }
            break;
        }
        break;
    case NV15_CLASS_M2MF:
        switch (method) {
        case NV15_M2MF_DMA_IN:
            return nv15_set_object_reference(s, channel, value,
                                             NV15_RAMHT_ENGINE_DMA,
                                             &object->dma_source);
        case NV15_M2MF_DMA_OUT:
            if (!object->dma_source) {
                return false;
            }
            return nv15_set_object_reference(s, channel, value,
                                             NV15_RAMHT_ENGINE_DMA,
                                             &object->dma_dest);
        case NV15_M2MF_OFFSET_IN:
            object->offset_source = value;
            return true;
        case NV15_M2MF_OFFSET_OUT:
            object->offset_dest = value;
            return true;
        case NV15_M2MF_PITCH_IN:
            if (value > 0x7fff) {
                return false;
            }
            object->m2mf_pitch_in = value;
            return true;
        case NV15_M2MF_PITCH_OUT:
            if (value > 0x7fff) {
                return false;
            }
            object->m2mf_pitch_out = value;
            return true;
        case NV15_M2MF_LINE_LENGTH:
            if (value >= 0x400000) {
                return false;
            }
            object->m2mf_line_length = value;
            return true;
        case NV15_M2MF_LINE_COUNT:
            if (value >= 0x800) {
                return false;
            }
            object->m2mf_line_count = value;
            return true;
        case NV15_M2MF_FORMAT:
            if (!nv15_m2mf_format_valid(value)) {
                return false;
            }
            object->m2mf_format = value;
            return true;
        case NV15_M2MF_NOTIFY:
            if (!nv15_notifier_valid(s, object, 0x10) ||
                !nv15_m2mf_copy(s, object, budget)) {
                return false;
            }
            return nv15_write_notifier(s, object, 0x10, 0, value & 1);
        default:
            break;
        }
        break;
    case NV15_CLASS_NV5_SCALED_IMAGE:
    case NV15_CLASS_NV4_SCALED_IMAGE:
    case NV15_CLASS_SCALED_IMAGE:
        switch (method) {
        case NV15_SCALED_DMA_IMAGE:
            return nv15_set_object_reference(s, channel, value,
                                             NV15_RAMHT_ENGINE_DMA,
                                             &object->dma_source);
        case NV15_SCALED_PATTERN:
            return nv15_set_context_reference(
                s, channel, object, value, &object->pattern,
                NV15_CONTEXT_EXPLICIT_PATTERN, NV15_CLASS_IMAGE_PATTERN);
        case NV15_SCALED_ROP:
            return nv15_set_context_reference(
                s, channel, object, value, &object->rop,
                NV15_CONTEXT_EXPLICIT_ROP, NV15_CLASS_CONTEXT_ROP);
        case NV15_SCALED_BETA:
            return nv15_set_object_reference(s, channel, value,
                                             NV15_RAMHT_ENGINE_GRAPHICS,
                                             &object->clip);
        case NV15_SCALED_BETA4:
            return nv15_set_object_reference(s, channel, value,
                                             NV15_RAMHT_ENGINE_GRAPHICS,
                                             &object->dma_dest);
        case NV15_SCALED_SURFACE:
            return nv15_set_surface_reference(s, channel, object, value);
        case NV15_SCALED_COLOR_CONVERSION:
            object->mono_format = value;
            return value <= 2;
        case NV15_SCALED_COLOR_FORMAT:
            object->color_format = value;
            return nv15_scaled_source_cpp(value) != 0;
        case NV15_SCALED_OPERATION:
            object->operation = value;
            return value <= 5;
        case NV15_SCALED_CLIP_POINT:
            object->upload_point = value;
            return true;
        case NV15_SCALED_CLIP_SIZE:
            object->upload_size_out = value;
            return true;
        case NV15_SCALED_OUT_POINT:
            object->point_out = value;
            return true;
        case NV15_SCALED_OUT_SIZE:
            object->upload_size_in = value;
            return true;
        case NV15_SCALED_DU_DX:
            object->m2mf_pitch_in = value;
            return true;
        case NV15_SCALED_DV_DY:
            object->m2mf_pitch_out = value;
            return true;
        case NV15_SCALED_SIZE:
            object->size = value;
            return true;
        case NV15_SCALED_FORMAT:
            object->format = value;
            return (value & NV15_SCALED_FILTER_MASK) <=
                   NV15_SCALED_FILTER_BILINEAR;
        case NV15_SCALED_OFFSET:
            object->offset_source = value;
            return true;
        case NV15_SCALED_POINT:
            object->point_in = value;
            return nv15_scaled_image(s, channel, object, budget);
        default:
            break;
        }
        break;
    default:
        break;
    }

    s->rejected_methods++;
    *unknown = true;
    return false;
}

static void nv15_fifo_pusher_fault(NVIDIAQuadro2State *s, uint32_t error)
{
    uint32_t *state = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_STATE -
                                 NV15_PFIFO_BASE) >> 2];
    uint32_t *push = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUSH -
                                NV15_PFIFO_BASE) >> 2];

    *state = (*state & ~NV15_PFIFO_DMA_STATE_ERROR) | error;
    *push |= NV15_PFIFO_DMA_PUSH_STATUS;
    s->pfifo[(NV15_PFIFO_INTR_0 - NV15_PFIFO_BASE) >> 2] |=
        NV15_PFIFO_INTR_DMA_PUSHER;
    nv15_update_irq(s);
}

static void nv15_fifo_cache_fault(NVIDIAQuadro2State *s)
{
    s->pfifo[(NV15_PFIFO_CACHE1_PULL0 - NV15_PFIFO_BASE) >> 2] &= ~1U;
    s->pfifo[(NV15_PFIFO_INTR_0 - NV15_PFIFO_BASE) >> 2] |=
        NV15_PFIFO_INTR_CACHE_ERROR;
    nv15_update_irq(s);
}

static void nv15_pgraph_method_error(NVIDIAQuadro2State *s,
                                     unsigned int channel,
                                     unsigned int subchannel,
                                     uint32_t method, uint32_t value,
                                     uint32_t nstatus, uint32_t nsource)
{
    s->pgraph[(NV15_PGRAPH_NSTATUS - NV15_PGRAPH_BASE) >> 2] |=
        nstatus;
    s->pgraph[(NV15_PGRAPH_NSOURCE - NV15_PGRAPH_BASE) >> 2] |=
        nsource;
    s->pgraph[(NV15_PGRAPH_INTR - NV15_PGRAPH_BASE) >> 2] |=
        NV15_PGRAPH_INTR_ERROR;
    s->pgraph[(NV15_PGRAPH_TRAPPED_ADDR - NV15_PGRAPH_BASE) >> 2] =
        (channel << 20) | (subchannel << 16) | (method & 0x1ffcU);
    s->pgraph[(NV15_PGRAPH_TRAPPED_DATA - NV15_PGRAPH_BASE) >> 2] = value;
    s->pgraph[(NV15_PGRAPH_TRAPPED_DATA_HIGH - NV15_PGRAPH_BASE) >> 2] = 0;
    s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] = 0;
    nv15_update_irq(s);
}

static void nv15_pgraph_illegal_method(NVIDIAQuadro2State *s,
                                       unsigned int channel,
                                       unsigned int subchannel,
                                       uint32_t method, uint32_t value)
{
    nv15_pgraph_method_error(s, channel, subchannel, method, value,
                             NV15_PGRAPH_NSTATUS_PROTECTION,
                             NV15_PGRAPH_NSOURCE_ILLEGAL_METHOD);
}

static bool nv15_fifo_method(NVIDIAQuadro2State *s, unsigned int channel,
                             unsigned int subchannel, uint32_t method,
                             uint32_t value, NV15WorkBudget *budget)
{
    NV15GraphicsObject *object;
    uint32_t instance;
    uint32_t engine;
    bool unknown;
    bool result;

    if (subchannel >= NV15_USER_SUBCHANNELS) {
        return false;
    }
    if (method == 0x50) {
        s->pfifo[(NV15_PFIFO_CACHE1_REF_CNT - NV15_PFIFO_BASE) >> 2] =
            value;
        return true;
    }
    if (method < 0x100 && method != 0) {
        return false;
    }

    if (s->notify_pending[channel] &&
        (method == 0 || subchannel != s->notify_subchannel[channel])) {
        /* NV15 volatile context reset discards an armed notifier. */
        s->notify_pending[channel] = 0;
        s->notify_type[channel] = 0;
        s->notify_subchannel[channel] = 0;
        s->notify_dma_instance[channel] = 0;
    }
    if (s->notify_pending[channel]) {
        if (method == NV15_GRAPH_NOTIFY) {
            nv15_pgraph_method_error(s, channel, subchannel, method, value,
                                     NV15_PGRAPH_NSTATUS_STATE_IN_USE,
                                     NV15_PGRAPH_NSOURCE_DOUBLE_NOTIFY);
            return true;
        }
        if (method == NV15_GRAPH_DMA_NOTIFY) {
            nv15_pgraph_method_error(s, channel, subchannel, method, value,
                                     NV15_PGRAPH_NSTATUS_STATE_IN_USE,
                                     NV15_PGRAPH_NSOURCE_NOTIFY_IN_USE);
            return true;
        }
    }

    if (method == 0) {
        if (!nv15_ramht_lookup(s, channel, value, &instance, &engine) ||
            engine != NV15_RAMHT_ENGINE_GRAPHICS) {
            return false;
        }
        object = nv15_get_object(s, instance);
        if (!object) {
            return false;
        }
        s->subchannel_instance[channel][subchannel] = instance;
        if (object->class_id == NV15_CLASS_CONTEXT_SURFACES_2D) {
            s->active_surface[channel] = instance;
        }
        return true;
    }

    instance = s->subchannel_instance[channel][subchannel];
    object = nv15_bound_object(s, channel, subchannel);
    if (instance && !object) {
        /* PGRAPH reset drops contexts, while PFIFO retains its bindings. */
        object = nv15_rehydrate_channel_objects(s, channel, instance);
    }
    if (!instance || !object) {
        return false;
    }
    if (method == NV15_GRAPH_NOTIFY) {
        if (value > 1) {
            nv15_pgraph_method_error(s, channel, subchannel, method, value,
                                     NV15_PGRAPH_NSTATUS_BAD_ARGUMENT,
                                     NV15_PGRAPH_NSOURCE_DATA_ERROR);
            return true;
        }
        if (!nv15_notifier_valid(s, object, 0)) {
            nv15_pgraph_method_error(s, channel, subchannel, method, value,
                                     NV15_PGRAPH_NSTATUS_INVALID_STATE,
                                     NV15_PGRAPH_NSOURCE_STATE_INVALID);
            return true;
        }
        s->notify_pending[channel] = 1;
        s->notify_type[channel] = value;
        s->notify_subchannel[channel] = subchannel;
        s->notify_dma_instance[channel] = object->dma_notify;
        return true;
    }
    s->pgraph[(NV15_PGRAPH_STATUS - NV15_PGRAPH_BASE) >> 2] = 1;
    result = nv15_graphics_method(s, channel, object, method, value, budget,
                                  &unknown);
    s->pgraph[(NV15_PGRAPH_STATUS - NV15_PGRAPH_BASE) >> 2] = 0;
    if (!result && unknown) {
        nv15_pgraph_illegal_method(s, channel, subchannel, method, value);
        return true;
    }
    if (!result) {
        return false;
    }
    if (s->notify_pending[channel]) {
        if (!nv15_write_notifier_instance(
                s, s->notify_dma_instance[channel], 0, 0,
                s->notify_type[channel] == 1)) {
            return false;
        }
        s->notify_pending[channel] = 0;
        s->notify_type[channel] = 0;
        s->notify_subchannel[channel] = 0;
        s->notify_dma_instance[channel] = 0;
    }
    return true;
}

static uint32_t nv15_ramfc_base(NVIDIAQuadro2State *s)
{
    return (s->pfifo[(NV15_PFIFO_RAMFC - NV15_PFIFO_BASE) >> 2] &
            0x1ffU) << 8;
}

static bool nv15_ramfc_channel_offset(NVIDIAQuadro2State *s,
                                      unsigned int channel,
                                      uint32_t *offset)
{
    uint32_t base = nv15_ramfc_base(s);

    if (!base || channel >= NV15_USER_CHANNELS ||
        !nv15_ramin_range_valid(base + channel * 32, 32)) {
        return false;
    }
    *offset = base + channel * 32;
    return true;
}

static void nv15_ramfc_save(NVIDIAQuadro2State *s, unsigned int channel)
{
    uint32_t offset;
    uint32_t instance;
    uint32_t dcount;

    if (!nv15_ramfc_channel_offset(s, channel, &offset)) {
        return;
    }
    nv15_ramin_write32(s, offset + 0x00,
        s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUT - NV15_PFIFO_BASE) >> 2]);
    nv15_ramin_write32(s, offset + 0x04,
        s->pfifo[(NV15_PFIFO_CACHE1_DMA_GET - NV15_PFIFO_BASE) >> 2]);
    nv15_ramin_write32(s, offset + 0x08,
        s->pfifo[(NV15_PFIFO_CACHE1_REF_CNT - NV15_PFIFO_BASE) >> 2]);
    instance = s->pfifo[(NV15_PFIFO_CACHE1_DMA_INSTANCE -
                          NV15_PFIFO_BASE) >> 2] & 0xffff;
    dcount = s->pfifo[(NV15_PFIFO_CACHE1_DMA_DCOUNT -
                       NV15_PFIFO_BASE) >> 2] & 0xffff;
    nv15_ramin_write32(s, offset + 0x0c, instance | (dcount << 16));
    nv15_ramin_write32(s, offset + 0x10,
        s->pfifo[(NV15_PFIFO_CACHE1_DMA_STATE - NV15_PFIFO_BASE) >> 2]);
    nv15_ramin_write32(s, offset + 0x14,
        s->pfifo[(NV15_PFIFO_CACHE1_DMA_FETCH - NV15_PFIFO_BASE) >> 2]);
    nv15_ramin_write32(s, offset + 0x18,
        s->pfifo[(NV15_PFIFO_CACHE1_ENGINE - NV15_PFIFO_BASE) >> 2]);
    nv15_ramin_write32(s, offset + 0x1c,
        s->pfifo[(NV15_PFIFO_CACHE1_PULL1 - NV15_PFIFO_BASE) >> 2]);
}

static bool nv15_ramfc_load(NVIDIAQuadro2State *s, unsigned int channel)
{
    uint32_t offset;
    uint32_t combined;

    if (!nv15_ramfc_channel_offset(s, channel, &offset)) {
        return false;
    }
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUT - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x00);
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_GET - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x04);
    s->pfifo[(NV15_PFIFO_CACHE1_REF_CNT - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x08);
    combined = nv15_ramin_read32(s, offset + 0x0c);
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_INSTANCE - NV15_PFIFO_BASE) >> 2] =
        combined & 0xffff;
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_DCOUNT - NV15_PFIFO_BASE) >> 2] =
        combined >> 16;
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_STATE - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x10);
    s->pfifo[(NV15_PFIFO_CACHE1_DMA_FETCH - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x14);
    s->pfifo[(NV15_PFIFO_CACHE1_ENGINE - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x18);
    s->pfifo[(NV15_PFIFO_CACHE1_PULL1 - NV15_PFIFO_BASE) >> 2] =
        nv15_ramin_read32(s, offset + 0x1c);
    s->pfifo[(NV15_PFIFO_CACHE1_PUSH1 - NV15_PFIFO_BASE) >> 2] =
        channel | NV15_PFIFO_PUSH1_DMA;
    s->active_channel = channel;
    return true;
}

static bool nv15_fifo_switch_channel(NVIDIAQuadro2State *s,
                                     unsigned int channel)
{
    if (s->active_channel == channel) {
        return true;
    }
    if (s->active_channel < NV15_USER_CHANNELS) {
        nv15_ramfc_save(s, s->active_channel);
    }
    return nv15_ramfc_load(s, channel);
}

static void nv15_fifo_run_pusher(NVIDIAQuadro2State *s)
{
    uint32_t *push0 = &s->pfifo[(NV15_PFIFO_CACHE1_PUSH0 -
                                 NV15_PFIFO_BASE) >> 2];
    uint32_t *pull0 = &s->pfifo[(NV15_PFIFO_CACHE1_PULL0 -
                                 NV15_PFIFO_BASE) >> 2];
    uint32_t *dma_push = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUSH -
                                    NV15_PFIFO_BASE) >> 2];
    uint32_t *dma_state = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_STATE -
                                     NV15_PFIFO_BASE) >> 2];
    uint32_t *dma_get = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_GET -
                                   NV15_PFIFO_BASE) >> 2];
    uint32_t *dma_put = &s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUT -
                                   NV15_PFIFO_BASE) >> 2];
    uint32_t instance = s->pfifo[(NV15_PFIFO_CACHE1_DMA_INSTANCE -
                                  NV15_PFIFO_BASE) >> 2] & 0xffff;
    NV15DMAObject pushbuffer;
    NV15WorkBudget work = { .bytes = NV15_MAX_PUSH_WORK_BYTES };
    unsigned int channel = s->active_channel;
    unsigned int budget = NV15_MAX_PUSH_WORDS;

    if (channel >= NV15_USER_CHANNELS || !(*push0 & 1) ||
        !(*pull0 & 1) || !(*dma_push & NV15_PFIFO_DMA_PUSH_ACCESS) ||
        (*dma_push & NV15_PFIFO_DMA_PUSH_STATUS) ||
        !(s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] &
          1) ||
        !(s->pmc[(NV15_PMC_ENABLE - NV15_PMC_BASE) >> 2] &
          NV15_PMC_ENABLE_PFIFO) ||
        !(s->pfifo[(NV15_PFIFO_MODE - NV15_PFIFO_BASE) >> 2] &
          BIT(channel)) ||
        !nv15_dma_object_load(s, instance << 4, &pushbuffer)) {
        return;
    }

    if ((*dma_get & 3) || (*dma_put & 3) || *dma_get > pushbuffer.length ||
        *dma_put > pushbuffer.length) {
        nv15_fifo_pusher_fault(s, NV15_PFIFO_DMA_ERROR_MEMORY);
        return;
    }

    while (*dma_get != *dma_put && budget &&
           (s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] &
            1)) {
        uint32_t word;
        uint32_t count = (*dma_state & NV15_PFIFO_DMA_STATE_COUNT) >> 18;

        budget--;
        if (pushbuffer.length < sizeof(uint32_t) ||
            *dma_get > pushbuffer.length - sizeof(uint32_t) ||
            !nv15_dma_read32(s, &pushbuffer, *dma_get, &word)) {
            nv15_fifo_pusher_fault(s, NV15_PFIFO_DMA_ERROR_MEMORY);
            break;
        }
        *dma_get += sizeof(uint32_t);

        if (count) {
            uint32_t method = *dma_state & NV15_PFIFO_DMA_STATE_METHOD;
            unsigned int subchannel =
                (*dma_state & NV15_PFIFO_DMA_STATE_SUBCH) >> 13;

            if (method < 0x100 && method != 0 && method != 0x50) {
                nv15_fifo_pusher_fault(
                    s, NV15_PFIFO_DMA_ERROR_INVALID_METHOD);
                break;
            }
            if (!nv15_fifo_method(s, channel, subchannel, method, word,
                                  &work)) {
                if (work.exhausted) {
                    /* The method has not drawn anything: retry next quantum. */
                    *dma_get -= sizeof(uint32_t);
                } else {
                    nv15_fifo_cache_fault(s);
                }
                break;
            }
            count--;
            *dma_state = (*dma_state & ~NV15_PFIFO_DMA_STATE_COUNT) |
                         (count << 18);
            if (!(*dma_state & NV15_PFIFO_DMA_STATE_NONINC)) {
                method = (method + 4) & NV15_PFIFO_DMA_STATE_METHOD;
                *dma_state = (*dma_state & ~NV15_PFIFO_DMA_STATE_METHOD) |
                             method;
            }
            s->pfifo[(NV15_PFIFO_CACHE1_DMA_DCOUNT -
                       NV15_PFIFO_BASE) >> 2]++;
            continue;
        }

        /* NV15 only implements the pre-NV1A (old) jump encoding. */
        if ((word & 0xe0000003U) == 0x20000000U) {
            *dma_get = word & 0x1ffffffcU;
        } else if ((word & 0xe0030003U) == 0) {
            *dma_state = word & (NV15_PFIFO_DMA_STATE_METHOD |
                                 NV15_PFIFO_DMA_STATE_SUBCH |
                                 NV15_PFIFO_DMA_STATE_COUNT);
        } else if ((word & 0xe0030003U) == 0x40000000U) {
            *dma_state = (word & (NV15_PFIFO_DMA_STATE_METHOD |
                                  NV15_PFIFO_DMA_STATE_SUBCH |
                                  NV15_PFIFO_DMA_STATE_COUNT)) |
                         NV15_PFIFO_DMA_STATE_NONINC;
        } else {
            nv15_fifo_pusher_fault(s,
                                   NV15_PFIFO_DMA_ERROR_INVALID_COMMAND);
            break;
        }
    }
    if (*dma_get != *dma_put && (!budget || work.exhausted) &&
        !(*dma_push & NV15_PFIFO_DMA_PUSH_STATUS)) {
        s->fifo_pending_channels |= BIT(channel);
        qemu_bh_schedule(s->fifo_bh);
    }
    nv15_ramfc_save(s, channel);
}

static void nv15_fifo_resume(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;
    uint32_t pending = s->fifo_pending_channels;

    s->fifo_pending_channels = 0;
    while (pending) {
        unsigned channel = ctz32(pending);

        pending &= ~BIT(channel);
        if (nv15_fifo_switch_channel(s, channel)) {
            nv15_fifo_run_pusher(s);
        }
    }
}

static uint32_t nv15_user_read(NVIDIAQuadro2State *s, hwaddr addr)
{
    uint32_t relative = addr - NV15_USER_BASE;
    unsigned int channel = relative / NV15_USER_CHANNEL_SIZE;
    uint32_t offset = relative % NV15_USER_CHANNEL_SIZE;
    uint32_t ramfc;

    if (channel >= NV15_USER_CHANNELS) {
        return 0;
    }
    if (s->pfifo[(NV15_PFIFO_MODE - NV15_PFIFO_BASE) >> 2] & BIT(channel)) {
        if (s->active_channel == channel) {
            switch (offset) {
            case NV15_USER_DMA_PUT:
                return s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUT -
                                  NV15_PFIFO_BASE) >> 2];
            case NV15_USER_DMA_GET:
                return s->pfifo[(NV15_PFIFO_CACHE1_DMA_GET -
                                  NV15_PFIFO_BASE) >> 2];
            case NV15_USER_REF_CNT:
                return s->pfifo[(NV15_PFIFO_CACHE1_REF_CNT -
                                  NV15_PFIFO_BASE) >> 2];
            default:
                break;
            }
        } else if (nv15_ramfc_channel_offset(s, channel, &ramfc)) {
            switch (offset) {
            case NV15_USER_DMA_PUT:
                return nv15_ramin_read32(s, ramfc + 0x00);
            case NV15_USER_DMA_GET:
                return nv15_ramin_read32(s, ramfc + 0x04);
            case NV15_USER_REF_CNT:
                return nv15_ramin_read32(s, ramfc + 0x08);
            default:
                break;
            }
        }
    }
    return (offset % NV15_USER_SUBCHANNEL_SIZE) == 0x10 ? 0x1ffc : 0;
}

static void nv15_user_write(NVIDIAQuadro2State *s, hwaddr addr,
                            uint32_t value)
{
    uint32_t relative = addr - NV15_USER_BASE;
    unsigned int channel = relative / NV15_USER_CHANNEL_SIZE;
    uint32_t offset = relative % NV15_USER_CHANNEL_SIZE;
    NV15WorkBudget work = { .bytes = NV15_MAX_PUSH_WORK_BYTES };

    if (channel >= NV15_USER_CHANNELS) {
        return;
    }
    if (s->pfifo[(NV15_PFIFO_MODE - NV15_PFIFO_BASE) >> 2] & BIT(channel)) {
        if (offset == NV15_USER_DMA_PUT || offset == NV15_USER_DMA_GET ||
            offset == NV15_USER_REF_CNT) {
            if (!nv15_fifo_switch_channel(s, channel)) {
                nv15_fifo_pusher_fault(s, NV15_PFIFO_DMA_ERROR_MEMORY);
                return;
            }
            if (offset == NV15_USER_DMA_PUT) {
                s->pfifo[(NV15_PFIFO_CACHE1_DMA_PUT -
                           NV15_PFIFO_BASE) >> 2] = value;
                nv15_fifo_run_pusher(s);
            } else if (offset == NV15_USER_DMA_GET) {
                s->pfifo[(NV15_PFIFO_CACHE1_DMA_GET -
                           NV15_PFIFO_BASE) >> 2] = value;
            } else {
                s->pfifo[(NV15_PFIFO_CACHE1_REF_CNT -
                           NV15_PFIFO_BASE) >> 2] = value;
            }
            nv15_ramfc_save(s, channel);
            return;
        }
    }

    if (!(s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] &
          1)) {
        return;
    }

    s->active_channel = channel;
    s->pfifo[(NV15_PFIFO_CACHE1_PUSH1 - NV15_PFIFO_BASE) >> 2] = channel;
    if (!nv15_fifo_method(s, channel, offset / NV15_USER_SUBCHANNEL_SIZE,
                          offset % NV15_USER_SUBCHANNEL_SIZE, value, &work)) {
        nv15_fifo_cache_fault(s);
    }
}

static uint32_t nv15_mmio_read_word(NVIDIAQuadro2State *s, hwaddr addr)
{
    uint32_t *shadow;
    uint64_t timer;

    if (addr >= NV15_PFB_BASE + 0x240 &&
        addr < NV15_PFB_BASE + 0x2c0 && (addr & 15) == 12) {
        unsigned int shift, factor;
        uint32_t pitch = s->pfb[(addr - NV15_PFB_BASE - 4) >> 2];
        uint32_t enabled = s->pfb[(addr - NV15_PFB_BASE - 12) >> 2] & BIT(31);

        return nv15_tile_pitch(pitch, &shift, &factor) ?
               enabled | (shift << 4) | factor : 0;
    }
    switch (addr) {
    case NV15_PMC_BOOT_0:
        return NV15_BOOT_0_VALUE;
    case NV15_PMC_BOOT_1:
        return 0;
    case NV15_PMC_INTR_0:
        return nv15_pending_sources(s);
    case NV15_PTIMER_TIME_0:
        timer = nv15_timer_value(s);
        return (timer & MAKE_64BIT_MASK(0, 27)) << 5;
    case NV15_PTIMER_TIME_1:
        timer = nv15_timer_value(s);
        return (timer >> 27) & MAKE_64BIT_MASK(0, 29);
    case NV15_PFB_FIFO_DATA:
        return s->vga.vram_size_mb << 20;
    case NV15_PGRAPH_STATUS:
        return s->pgraph[(NV15_PGRAPH_STATUS - NV15_PGRAPH_BASE) >> 2];
    default:
        break;
    }

    if (addr >= NV15_USER_BASE) {
        return nv15_user_read(s, addr);
    }

    shadow = nv15_shadow_word(s, addr);
    return shadow ? *shadow : 0;
}

static void nv15_reset_engine(NVIDIAQuadro2State *s, uint32_t disabled)
{
    /* TODO: Model PTIMER clock/reset gating through PMC_ENABLE bit 16. */
    if (disabled & NV15_PMC_ENABLE_PFIFO) {
        qemu_bh_cancel(s->fifo_bh);
        s->fifo_pending_channels = 0;
        memset(s->pfifo, 0, sizeof(s->pfifo));
        memset(s->subchannel_instance, 0, sizeof(s->subchannel_instance));
        memset(s->active_surface, 0, sizeof(s->active_surface));
        memset(s->notify_pending, 0, sizeof(s->notify_pending));
        memset(s->notify_type, 0, sizeof(s->notify_type));
        memset(s->notify_subchannel, 0, sizeof(s->notify_subchannel));
        memset(s->notify_dma_instance, 0,
               sizeof(s->notify_dma_instance));
        s->active_channel = UINT8_MAX;
    }
    if (disabled & NV15_PMC_ENABLE_PGRAPH) {
        memset(s->pgraph, 0, sizeof(s->pgraph));
        s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] = 1;
        memset(s->objects, 0, sizeof(s->objects));
        nv15_object_index_rebuild(s);
        nv15_channel_contexts_reset(s);
        memset(s->notify_pending, 0, sizeof(s->notify_pending));
        memset(s->notify_type, 0, sizeof(s->notify_type));
        memset(s->notify_subchannel, 0, sizeof(s->notify_subchannel));
        memset(s->notify_dma_instance, 0,
               sizeof(s->notify_dma_instance));
        memset(s->surface_explicit, 0, sizeof(s->surface_explicit));
        memset(s->context_explicit, 0, sizeof(s->context_explicit));
    }
    if (disabled & NV15_PMC_ENABLE_PFB) {
        s->tiles_valid = false;
        memset(s->pfb, 0, sizeof(s->pfb));
        s->pfb[(NV15_PFB_BOOT_0 - NV15_PFB_BASE) >> 2] = 0x24;
        s->pfb[(NV15_PFB_CFG1 - NV15_PFB_BASE) >> 2] = 0x15;
        s->pfb[(NV15_PFB_FIFO_DATA - NV15_PFB_BASE) >> 2] =
            s->vga.vram_size_mb << 20;
        graphic_hw_invalidate(s->vga.con);
    }
    if (disabled & NV15_PMC_ENABLE_PCRTC) {
        memset(s->pcrtc, 0, sizeof(s->pcrtc));
        s->cursor_image_valid = false;
        nv15_cursor_update_host(s);
        nv15_update_vblank_timer(s);
    }
}

static void nv15_mmio_write_word(NVIDIAQuadro2State *s, hwaddr addr,
                                 uint32_t value, uint32_t mask)
{
    uint32_t *shadow = nv15_shadow_word(s, addr);
    uint32_t old;

    if (addr >= NV15_USER_BASE) {
        old = nv15_user_read(s, addr);
        nv15_user_write(s, addr, (old & ~mask) | (value & mask));
        return;
    }

    if (addr >= NV15_PFB_BASE + 0x240 &&
        addr < NV15_PFB_BASE + 0x2c0) {
        unsigned int word = (addr & 15) >> 2;
        uint32_t masks[] = { 0x87ffc000, 0x07ffc000, 0xff00, 0 };

        if (word != 3) {
            old = *shadow;
            *shadow = ((*shadow & ~mask) | (value & mask)) & masks[word];
            if (*shadow != old) {
                s->tiles_valid = false;
                graphic_hw_invalidate(s->vga.con);
            }
        }
        return;
    }
    switch (addr) {
    case NV15_PMC_BOOT_0:
    case NV15_PMC_BOOT_1:
    case NV15_PFB_FIFO_DATA:
    case NV15_PGRAPH_STATUS:
        return;
    case NV15_PTIMER_NUMERATOR:
    case NV15_PTIMER_DENOMINATOR:
    {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t current = nv15_timer_value_at(s, now);

        *shadow = ((*shadow & ~mask) | (value & mask)) & UINT16_MAX;
        s->ptimer_legacy_clock = false;
        nv15_ptimer_update_rate(s);
        s->ptimer_time_offset =
            (current - nv15_ptimer_scaled_clock_at(s, now)) &
            NV15_PTIMER_TIME_MASK;
        nv15_ptimer_schedule_alarm(s, s->ptimer_alarm_after_match);
        return;
    }
    case NV15_PRAMDAC_NVPLL_COEFF:
    {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t current = nv15_timer_value_at(s, now);

        *shadow = (*shadow & ~mask) | (value & mask);
        s->ptimer_legacy_clock = false;
        nv15_ptimer_update_rate(s);
        s->ptimer_time_offset =
            (current - nv15_ptimer_scaled_clock_at(s, now)) &
            NV15_PTIMER_TIME_MASK;
        nv15_ptimer_schedule_alarm(s, s->ptimer_alarm_after_match);
        return;
    }
    case NV15_PTIMER_TIME_0:
    case NV15_PTIMER_TIME_1:
    {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t current = nv15_timer_value_at(s, now);
        uint32_t word;

        s->ptimer_legacy_clock = false;
        nv15_ptimer_update_rate(s);
        if (addr == NV15_PTIMER_TIME_0) {
            word = (current & NV15_PTIMER_LOW_MASK) << 5;
            word = (word & ~mask) | (value & mask);
            current = (current & ~NV15_PTIMER_LOW_MASK) |
                      ((word >> 5) & NV15_PTIMER_LOW_MASK);
        } else {
            word = (current >> NV15_PTIMER_LOW_BITS) &
                   NV15_PTIMER_HIGH_MASK;
            word = (word & ~mask) | (value & mask);
            current = (current & NV15_PTIMER_LOW_MASK) |
                      ((uint64_t)(word & NV15_PTIMER_HIGH_MASK) <<
                       NV15_PTIMER_LOW_BITS);
        }
        s->ptimer_time_offset =
            (current - nv15_ptimer_scaled_clock_at(s, now)) &
            NV15_PTIMER_TIME_MASK;
        nv15_ptimer_schedule_alarm(
            s, addr == NV15_PTIMER_TIME_1 ?
               s->ptimer_alarm_after_match : false);
        return;
    }
    case NV15_PMC_INTR_0:
    case NV15_PFIFO_INTR_0:
    case NV15_PTIMER_INTR_0:
    case NV15_PGRAPH_INTR:
    case NV15_PCRTC_INTR_0:
        if (shadow) {
            *shadow &= ~(value & mask);
            if (addr == NV15_PGRAPH_INTR &&
                (value & mask & NV15_PGRAPH_INTR_ERROR)) {
                s->pgraph[(NV15_PGRAPH_NSOURCE - NV15_PGRAPH_BASE) >> 2] = 0;
            }
            nv15_update_irq(s);
        }
        return;
    default:
        break;
    }

    if (!shadow) {
        return;
    }

    old = *shadow;
    *shadow = (old & ~mask) | (value & mask);

    switch (addr) {
    case NV15_PTIMER_ALARM_0:
        *shadow &= NV15_PTIMER_ALARM_MASK;
        nv15_ptimer_schedule_alarm(s, false);
        break;
    case NV15_PMC_ENABLE:
        nv15_reset_engine(s, old & ~*shadow);
        break;
    case NV15_PFIFO_CACHE1_PUSH1:
        s->active_channel = *shadow & 0x1f;
        break;
    case NV15_PFIFO_CACHE1_DMA_PUT:
        nv15_fifo_run_pusher(s);
        if (s->active_channel < NV15_USER_CHANNELS) {
            nv15_ramfc_save(s, s->active_channel);
        }
        break;
    case NV15_PGRAPH_FIFO_ACCESS:
        if (*shadow & 1) {
            nv15_fifo_run_pusher(s);
            if (s->active_channel < NV15_USER_CHANNELS) {
                nv15_ramfc_save(s, s->active_channel);
            }
        }
        break;
    case NV15_PMC_INTR_EN_0:
    case NV15_PFIFO_INTR_EN_0:
    case NV15_PTIMER_INTR_EN_0:
    case NV15_PGRAPH_INTR_EN:
        nv15_update_irq(s);
        break;
    case NV15_PCRTC_INTR_EN_0:
        nv15_update_irq(s);
        nv15_update_vblank_timer(s);
        break;
    case NV15_PCRTC_START:
        if ((*shadow & ~UINT32_C(3)) < s->vga.vram_size &&
            s->vga.vbe_start_addr != (*shadow >> 2)) {
            s->vga.vbe_start_addr = (*shadow & ~UINT32_C(3)) >> 2;
            graphic_hw_invalidate(s->vga.con);
        }
        break;
    case NV15_PFB_CFG0:
        if (*shadow != old) {
            s->tiles_valid = false;
            graphic_hw_invalidate(s->vga.con);
        }
        break;
    case NV15_PCRTC_CURSOR_CONFIG:
        if (*shadow != old) {
            s->cursor_image_valid = false;
            nv15_cursor_update_host(s);
        }
        break;
    case NV15_PRAMDAC_CURSOR_POS:
        nv15_cursor_update_host(s);
        break;
    default:
        break;
    }
}

static uint64_t nv15_mmio_read(void *opaque, hwaddr addr, unsigned int size)
{
    NVIDIAQuadro2State *s = opaque;
    uint64_t value = 0;
    unsigned int i;

    if (nv15_in_range(addr, NV15_PBUS_PCI_BASE, NV15_PBUS_PCI_SIZE) &&
        addr + size <= NV15_PBUS_PCI_BASE + NV15_PBUS_PCI_SIZE) {
        return pci_default_read_config(&s->parent_obj,
                                       addr - NV15_PBUS_PCI_BASE, size);
    }
    if (nv15_in_range(addr, NV15_PRAMIN_BASE, NV15_PRAMIN_SIZE) &&
        addr + size <= NV15_PRAMIN_BASE + NV15_PRAMIN_SIZE) {
        return nv15_pramin_read(s, addr, size);
    }
    if (!nv15_is_vga_alias(addr) && (addr & 3) + size <= 4) {
        unsigned int shift = (addr & 3) * 8;
        uint32_t word = nv15_mmio_read_word(s, addr & ~3ULL);

        return (word >> shift) & MAKE_64BIT_MASK(0, size * 8);
    }
    for (i = 0; i < size; i++) {
        hwaddr byte_addr = addr + i;

        if (nv15_is_vga_alias(byte_addr)) {
            value |= (uint64_t)nv15_vga_ioport_read(
                s, nv15_vga_alias_port(byte_addr)) << (i * 8);
        } else {
            uint32_t word = nv15_mmio_read_word(s, byte_addr & ~3ULL);

            value |= (uint64_t)((word >> ((byte_addr & 3) * 8)) & 0xff) <<
                     (i * 8);
        }
    }
    return value;
}

static void nv15_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned int size)
{
    NVIDIAQuadro2State *s = opaque;
    uint32_t shifted;
    uint32_t mask;
    unsigned int i;

    if (nv15_in_range(addr, NV15_PBUS_PCI_BASE, NV15_PBUS_PCI_SIZE) &&
        addr + size <= NV15_PBUS_PCI_BASE + NV15_PBUS_PCI_SIZE) {
        pci_default_write_config(&s->parent_obj,
                                 addr - NV15_PBUS_PCI_BASE, value, size);
        return;
    }
    if (nv15_in_range(addr, NV15_PRAMIN_BASE, NV15_PRAMIN_SIZE) &&
        addr + size <= NV15_PRAMIN_BASE + NV15_PRAMIN_SIZE) {
        nv15_pramin_write(s, addr, value, size);
        return;
    }
    for (i = 0; i < size; i++) {
        if (nv15_is_vga_alias(addr + i)) {
            break;
        }
    }
    if (i < size) {
        for (i = 0; i < size; i++) {
            hwaddr byte_addr = addr + i;

            if (nv15_is_vga_alias(byte_addr)) {
                nv15_vga_ioport_write(s, nv15_vga_alias_port(byte_addr),
                                      value >> (i * 8));
            } else {
                nv15_mmio_write(s, byte_addr, value >> (i * 8), 1);
            }
        }
        return;
    }

    if ((addr & 3) + size > 4) {
        for (i = 0; i < size; i++) {
            nv15_mmio_write(s, addr + i, value >> (i * 8), 1);
        }
        return;
    }

    shifted = value << ((addr & 3) * 8);
    mask = MAKE_64BIT_MASK((addr & 3) * 8, size * 8);
    nv15_mmio_write_word(s, addr & ~3ULL, shifted, mask);
}

static const MemoryRegionOps nv15_mmio_ops = {
    .read = nv15_mmio_read,
    .write = nv15_mmio_write,
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

static const VMStateDescription vmstate_nv15_bitbang_i2c = {
    .name = "nvidia-quadro2/bitbang-i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_SINGLE(state, bitbang_i2c_interface, 0,
                       vmstate_info_int32, bitbang_i2c_state),
        VMSTATE_INT32(last_data, bitbang_i2c_interface),
        VMSTATE_INT32(last_clock, bitbang_i2c_interface),
        VMSTATE_INT32(device_out, bitbang_i2c_interface),
        VMSTATE_UINT8(buffer, bitbang_i2c_interface),
        VMSTATE_INT32(current_addr, bitbang_i2c_interface),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_nv15_graphics_object = {
    .name = "nvidia-quadro2/graphics-object",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(instance, NV15GraphicsObject),
        VMSTATE_UINT32(class_id, NV15GraphicsObject),
        VMSTATE_UINT32(dma_notify, NV15GraphicsObject),
        VMSTATE_UINT32(dma_source, NV15GraphicsObject),
        VMSTATE_UINT32(dma_dest, NV15GraphicsObject),
        VMSTATE_UINT32(surface, NV15GraphicsObject),
        VMSTATE_UINT32(pattern, NV15GraphicsObject),
        VMSTATE_UINT32(rop, NV15GraphicsObject),
        VMSTATE_UINT32(clip, NV15GraphicsObject),
        VMSTATE_UINT32(operation, NV15GraphicsObject),
        VMSTATE_UINT32(format, NV15GraphicsObject),
        VMSTATE_UINT32(pitch, NV15GraphicsObject),
        VMSTATE_UINT32(offset_source, NV15GraphicsObject),
        VMSTATE_UINT32(offset_dest, NV15GraphicsObject),
        VMSTATE_UINT32(color_format, NV15GraphicsObject),
        VMSTATE_UINT32(color, NV15GraphicsObject),
        VMSTATE_UINT32(point_in, NV15GraphicsObject),
        VMSTATE_UINT32(point_out, NV15GraphicsObject),
        VMSTATE_UINT32(size, NV15GraphicsObject),
        VMSTATE_UINT32(mono_format, NV15GraphicsObject),
        VMSTATE_UINT32(mono_shape, NV15GraphicsObject),
        VMSTATE_UINT32(pattern_select, NV15GraphicsObject),
        VMSTATE_UINT32_ARRAY(mono_color, NV15GraphicsObject, 2),
        VMSTATE_UINT32_ARRAY(mono_bitmap, NV15GraphicsObject, 2),
        VMSTATE_UINT32(upload_point, NV15GraphicsObject),
        VMSTATE_UINT32(upload_size_out, NV15GraphicsObject),
        VMSTATE_UINT32(upload_size_in, NV15GraphicsObject),
        VMSTATE_UINT32(upload_pixel, NV15GraphicsObject),
        VMSTATE_UINT32(m2mf_pitch_in, NV15GraphicsObject),
        VMSTATE_UINT32(m2mf_pitch_out, NV15GraphicsObject),
        VMSTATE_UINT32(m2mf_line_length, NV15GraphicsObject),
        VMSTATE_UINT32(m2mf_line_count, NV15GraphicsObject),
        VMSTATE_UINT32(m2mf_format, NV15GraphicsObject),
        VMSTATE_BOOL(valid, NV15GraphicsObject),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_nv15_channel_context = {
    .name = "nvidia-quadro2/channel-context",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(rop, NV15ChannelContext),
        VMSTATE_UINT32(color_format, NV15ChannelContext),
        VMSTATE_UINT32(mono_format, NV15ChannelContext),
        VMSTATE_UINT32(mono_shape, NV15ChannelContext),
        VMSTATE_UINT32(pattern_select, NV15ChannelContext),
        VMSTATE_UINT32_ARRAY(mono_color, NV15ChannelContext, 2),
        VMSTATE_UINT32_ARRAY(mono_bitmap, NV15ChannelContext, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void nv15_reconstruct_channel_context(NVIDIAQuadro2State *s,
                                              unsigned int channel)
{
    NV15ChannelContext *context = &s->channel_context[channel];
    NV15GraphicsObject *object;

    nv15_channel_context_reset(context);
    object = nv15_find_bound_object(s, channel, NV15_CLASS_CONTEXT_ROP);
    if (object) {
        context->rop = object->color & 0xff;
    }
    object = nv15_find_bound_object(s, channel, NV15_CLASS_IMAGE_PATTERN);
    if (object) {
        context->color_format = object->color_format;
        context->mono_format = object->mono_format;
        context->mono_shape = object->mono_shape;
        context->pattern_select = object->pattern_select;
        memcpy(context->mono_color, object->mono_color,
               sizeof(context->mono_color));
        memcpy(context->mono_bitmap, object->mono_bitmap,
               sizeof(context->mono_bitmap));
    }
}

static int nv15_pre_save(void *opaque)
{
    NVIDIAQuadro2State *s = opaque;
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->ptimer_clock_snapshot = now;
    s->ptimer_time_snapshot = nv15_timer_value_at(s, now);
    return 0;
}

static int nv15_post_load(void *opaque, int version_id)
{
    NVIDIAQuadro2State *s = opaque;
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned int i;

    qemu_bh_cancel(s->fifo_bh);
    if (version_id < 6) {
        s->fifo_pending_channels = 0;
    }
    nv15_object_index_rebuild(s);
    if (version_id >= 5) {
        uint64_t expiry = timer_expire_time_ns(&s->ptimer_alarm_timer);

        s->ptimer[(NV15_PTIMER_NUMERATOR - NV15_PTIMER_BASE) >> 2] &=
            UINT16_MAX;
        s->ptimer[(NV15_PTIMER_DENOMINATOR - NV15_PTIMER_BASE) >> 2] &=
            UINT16_MAX;

        nv15_ptimer_update_rate(s);
        s->ptimer_time_snapshot &= NV15_PTIMER_TIME_MASK;
        s->ptimer_time_offset =
            (s->ptimer_time_snapshot -
             nv15_ptimer_scaled_clock_at(s, now)) &
            NV15_PTIMER_TIME_MASK;
        if (!nv15_ptimer_running(s)) {
            timer_del(&s->ptimer_alarm_timer);
        } else if (timer_pending(&s->ptimer_alarm_timer)) {
            uint64_t remaining = expiry > s->ptimer_clock_snapshot ?
                                 expiry - s->ptimer_clock_snapshot : 0;

            timer_mod_ns(&s->ptimer_alarm_timer, now + remaining);
        }
    } else {
        s->ptimer[(NV15_PTIMER_NUMERATOR - NV15_PTIMER_BASE) >> 2] &=
            UINT16_MAX;
        s->ptimer[(NV15_PTIMER_DENOMINATOR - NV15_PTIMER_BASE) >> 2] &=
            UINT16_MAX;
        /* Legacy streams express PTIMER in virtual nanoseconds. */
        s->ptimer_legacy_clock = true;
        nv15_ptimer_update_rate(s);
        s->ptimer_time_offset = 0;
        s->ptimer_time_snapshot = 0;
        s->ptimer_clock_snapshot = 0;
        s->ptimer_alarm_after_match = false;
        timer_del(&s->ptimer_alarm_timer);
    }

    if (s->bbi2c.state < STOPPED || s->bbi2c.state > SENT_NACK ||
        s->bbi2c.last_data < 0 || s->bbi2c.last_data > 1 ||
        s->bbi2c.last_clock < 0 || s->bbi2c.last_clock > 1 ||
        s->bbi2c.device_out < 0 || s->bbi2c.device_out > 1 ||
        s->bbi2c.current_addr < -1 ||
        s->bbi2c.current_addr > UINT8_MAX ||
        (s->active_channel >= NV15_USER_CHANNELS &&
         s->active_channel != UINT8_MAX)) {
        return -EINVAL;
    }
    if (version_id < 2) {
        s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] = 1;
        memset(s->active_surface, 0, sizeof(s->active_surface));
        memset(s->context_explicit, 0, sizeof(s->context_explicit));
        for (i = 0; i < NV15_MAX_OBJECTS; i++) {
            s->surface_explicit[i] = s->objects[i].valid;
            if (s->objects[i].valid) {
                s->context_explicit[i] = NV15_CONTEXT_EXPLICIT_PATTERN |
                                         NV15_CONTEXT_EXPLICIT_ROP |
                                         NV15_CONTEXT_EXPLICIT_CLIP;
            }
        }
    }
    if (version_id < 3) {
        memset(s->notify_pending, 0, sizeof(s->notify_pending));
        memset(s->notify_type, 0, sizeof(s->notify_type));
        memset(s->notify_subchannel, 0, sizeof(s->notify_subchannel));
        memset(s->notify_dma_instance, 0,
               sizeof(s->notify_dma_instance));
    }
    if (version_id < 4) {
        for (i = 0; i < NV15_USER_CHANNELS; i++) {
            nv15_reconstruct_channel_context(s, i);
        }
    }
    for (i = 0; i < NV15_USER_CHANNELS; i++) {
        if (s->channel_context[i].rop & ~0xffU ||
            s->notify_pending[i] > 1 || s->notify_type[i] > 1 ||
            s->notify_subchannel[i] >= NV15_USER_SUBCHANNELS ||
            (s->notify_pending[i] &&
             (!s->notify_dma_instance[i] ||
              (s->notify_dma_instance[i] & 0xf) ||
              !nv15_ramin_range_valid(s->notify_dma_instance[i], 16)))) {
            return -EINVAL;
        }
    }
    nv15_update_irq(s);
    s->vga.graphic_mode = -1;
    s->tiles_valid = false;
    s->cursor_image_valid = false;
    memset(&s->guest_cursor, 0, sizeof(s->guest_cursor));
    nv15_cursor_update_host(s);
    graphic_hw_invalidate(s->vga.con);
    if (s->fifo_pending_channels) {
        qemu_bh_schedule(s->fifo_bh);
    }
    return 0;
}

static const VMStateDescription vmstate_nv15 = {
    .name = "nvidia-quadro2",
    .version_id = 6,
    .minimum_version_id = 1,
    .pre_save = nv15_pre_save,
    .post_load = nv15_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, NVIDIAQuadro2State),
        VMSTATE_STRUCT(vga, NVIDIAQuadro2State, 0,
                       vmstate_vga_common, VGACommonState),
        VMSTATE_UINT32_ARRAY(pmc, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PMC_SIZE)),
        VMSTATE_UINT32_ARRAY(pfifo, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PFIFO_SIZE)),
        VMSTATE_UINT32_ARRAY(ptimer, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PTIMER_SIZE)),
        VMSTATE_UINT32_ARRAY(pfb, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PFB_SIZE)),
        VMSTATE_UINT32_ARRAY(pextdev, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PEXTDEV_SIZE)),
        VMSTATE_UINT32_ARRAY(pgraph, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PGRAPH_SIZE)),
        VMSTATE_UINT32_ARRAY(pcrtc, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PCRTC_SIZE)),
        VMSTATE_UINT32_ARRAY(pramdac, NVIDIAQuadro2State,
                             NV15_REG_WORDS(NV15_PRAMDAC_SIZE)),
        VMSTATE_STRUCT(bbi2c, NVIDIAQuadro2State, 0,
                       vmstate_nv15_bitbang_i2c, bitbang_i2c_interface),
        VMSTATE_TIMER(vblank_timer, NVIDIAQuadro2State),
        VMSTATE_UINT64_V(ptimer_time_snapshot, NVIDIAQuadro2State, 5),
        VMSTATE_UINT64_V(ptimer_clock_snapshot, NVIDIAQuadro2State, 5),
        VMSTATE_TIMER_V(ptimer_alarm_timer, NVIDIAQuadro2State, 5),
        VMSTATE_BOOL_V(ptimer_alarm_after_match, NVIDIAQuadro2State, 5),
        VMSTATE_BOOL_V(ptimer_legacy_clock, NVIDIAQuadro2State, 5),
        VMSTATE_UINT32_2DARRAY(subchannel_instance, NVIDIAQuadro2State,
                              NV15_USER_CHANNELS, NV15_USER_SUBCHANNELS),
        VMSTATE_STRUCT_ARRAY(objects, NVIDIAQuadro2State, NV15_MAX_OBJECTS,
                             0, vmstate_nv15_graphics_object,
                             NV15GraphicsObject),
        VMSTATE_UINT32(rejected_methods, NVIDIAQuadro2State),
        VMSTATE_UINT8(active_channel, NVIDIAQuadro2State),
        VMSTATE_UINT32_ARRAY_V(active_surface, NVIDIAQuadro2State,
                               NV15_USER_CHANNELS, 2),
        VMSTATE_STRUCT_ARRAY(channel_context, NVIDIAQuadro2State,
                             NV15_USER_CHANNELS, 4,
                             vmstate_nv15_channel_context,
                             NV15ChannelContext),
        VMSTATE_BOOL_ARRAY_V(surface_explicit, NVIDIAQuadro2State,
                             NV15_MAX_OBJECTS, 2),
        VMSTATE_UINT8_ARRAY_V(context_explicit, NVIDIAQuadro2State,
                              NV15_MAX_OBJECTS, 2),
        VMSTATE_UINT8_ARRAY_V(notify_pending, NVIDIAQuadro2State,
                              NV15_USER_CHANNELS, 3),
        VMSTATE_UINT8_ARRAY_V(notify_type, NVIDIAQuadro2State,
                              NV15_USER_CHANNELS, 3),
        VMSTATE_UINT8_ARRAY_V(notify_subchannel, NVIDIAQuadro2State,
                              NV15_USER_CHANNELS, 3),
        VMSTATE_UINT32_ARRAY_V(notify_dma_instance, NVIDIAQuadro2State,
                               NV15_USER_CHANNELS, 3),
        VMSTATE_UINT32_V(fifo_pending_channels, NVIDIAQuadro2State, 6),
        VMSTATE_END_OF_LIST()
    },
};

static void nv15_reset(DeviceState *dev)
{
    NVIDIAQuadro2State *s = NVIDIA_QUADRO2(dev);

    qemu_bh_cancel(s->fifo_bh);
    s->fifo_pending_channels = 0;
    timer_del(&s->vblank_timer);
    timer_del(&s->ptimer_alarm_timer);
    s->ptimer_time_offset = 0;
    s->ptimer_time_snapshot = 0;
    s->ptimer_clock_snapshot = 0;
    s->ptimer_alarm_after_match = false;
    s->ptimer_legacy_clock = false;
    i2c_end_transfer(s->bbi2c.bus);
    bitbang_i2c_init(&s->bbi2c, s->bbi2c.bus);
    s->bbi2c.state = STOPPED;
    s->bbi2c.buffer = 0;
    s->bbi2c.current_addr = -1;
    memset(s->pmc, 0, sizeof(s->pmc));
    memset(s->pfifo, 0, sizeof(s->pfifo));
    memset(s->ptimer, 0, sizeof(s->ptimer));
    memset(s->pfb, 0, sizeof(s->pfb));
    memset(s->pextdev, 0, sizeof(s->pextdev));
    memset(s->pgraph, 0, sizeof(s->pgraph));
    memset(s->pcrtc, 0, sizeof(s->pcrtc));
    memset(s->pramdac, 0, sizeof(s->pramdac));
    memset(s->subchannel_instance, 0, sizeof(s->subchannel_instance));
    memset(s->active_surface, 0, sizeof(s->active_surface));
    nv15_channel_contexts_reset(s);
    memset(s->notify_pending, 0, sizeof(s->notify_pending));
    memset(s->notify_type, 0, sizeof(s->notify_type));
    memset(s->notify_subchannel, 0, sizeof(s->notify_subchannel));
    memset(s->notify_dma_instance, 0, sizeof(s->notify_dma_instance));
    memset(s->surface_explicit, 0, sizeof(s->surface_explicit));
    memset(s->context_explicit, 0, sizeof(s->context_explicit));
    memset(s->objects, 0, sizeof(s->objects));
    nv15_object_index_rebuild(s);
    s->rejected_methods = 0;
    s->active_channel = UINT8_MAX;

    s->pmc[(NV15_PMC_ENABLE - NV15_PMC_BASE) >> 2] =
        NV15_PMC_ENABLE_PFIFO | NV15_PMC_ENABLE_PGRAPH |
        NV15_PMC_ENABLE_PTIMER | NV15_PMC_ENABLE_PFB |
        NV15_PMC_ENABLE_PCRTC;
    s->pfifo[(NV15_PFIFO_CACHE1_PUSH0 - NV15_PFIFO_BASE) >> 2] = 1;
    s->pfifo[(NV15_PFIFO_CACHE1_PULL0 - NV15_PFIFO_BASE) >> 2] = 1;
    s->pgraph[(NV15_PGRAPH_FIFO_ACCESS - NV15_PGRAPH_BASE) >> 2] = 1;
    s->ptimer[(NV15_PTIMER_NUMERATOR - NV15_PTIMER_BASE) >> 2] =
        NV15_PTIMER_DEFAULT_DIV;
    s->ptimer[(NV15_PTIMER_DENOMINATOR - NV15_PTIMER_BASE) >> 2] =
        NV15_PTIMER_DEFAULT_MUL;
    s->pfb[(NV15_PFB_BOOT_0 - NV15_PFB_BASE) >> 2] = 0x24;
    s->pfb[(NV15_PFB_CFG1 - NV15_PFB_BASE) >> 2] = 0x15;
    s->pfb[(NV15_PFB_FIFO_DATA - NV15_PFB_BASE) >> 2] =
        s->vga.vram_size_mb << 20;
    /* 128-bit memory interface and a 14.318 MHz reference crystal. */
    s->pextdev[0] = BIT(4) | BIT(6);
    s->pramdac[(NV15_PRAMDAC_NVPLL_COEFF - NV15_PRAMDAC_BASE) >> 2] =
        0x00011c02;
    s->pramdac[(NV15_PRAMDAC_MPLL_COEFF - NV15_PRAMDAC_BASE) >> 2] =
        0x00011c02;
    s->pramdac[(NV15_PRAMDAC_VPLL_COEFF - NV15_PRAMDAC_BASE) >> 2] =
        0x00011c02;
    s->pramdac[(NV15_PRAMDAC_PLL_COEFF_SELECT - NV15_PRAMDAC_BASE) >> 2] =
        0x10000700;
    s->pramdac[(NV15_PRAMDAC_GENERAL_CONTROL - NV15_PRAMDAC_BASE) >> 2] =
        0x00100100;

    nv15_ptimer_update_rate(s);
    vga_common_reset(&s->vga);
    s->tiles_valid = false;
    s->vga.force_shadow = false;
    s->vga.cr[0x36] = BIT(2) | BIT(3);
    s->vga.cr[0x3e] = BIT(2) | BIT(3);
    s->cursor_image_valid = false;
    memset(&s->guest_cursor, 0, sizeof(s->guest_cursor));
    if (!s->cursor_guest_mode) {
        dpy_mouse_set(s->vga.con, 0, 0, false);
        s->cursor_host_visible = false;
    }
    nv15_update_irq(s);
}

static bool nv15_init_pci_caps(NVIDIAQuadro2State *s, Error **errp)
{
    PCIDevice *dev = &s->parent_obj;
    int cap;

    cap = pci_add_capability(dev, PCI_CAP_ID_AGP, NV15_AGP_CAP_OFFSET,
                             PCI_AGP_SIZEOF, errp);
    if (cap < 0) {
        return false;
    }
    dev->config[cap + PCI_AGP_VERSION] = 0x20;
    pci_set_long(dev->config + cap + PCI_AGP_STATUS, NV15_AGP_STATUS);
    pci_set_long(dev->wmask + cap + PCI_AGP_COMMAND,
                 NV15_AGP_COMMAND_MASK);

    cap = pci_pm_init(dev, NV15_PM_CAP_OFFSET, errp);
    if (cap < 0) {
        return false;
    }
    pci_set_word(dev->config + cap + PCI_PM_PMC, 0x0001);
    pci_set_word(dev->wmask + cap + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK);
    return true;
}

static void nv15_realize(PCIDevice *dev, Error **errp)
{
    NVIDIAQuadro2State *s = NVIDIA_QUADRO2(dev);
    VGACommonState *vga = &s->vga;
    I2CBus *i2cbus;

    if (vga->vram_size_mb < 16 || vga->vram_size_mb > 128 ||
        !is_power_of_2(vga->vram_size_mb)) {
        error_setg(errp,
                   "nvidia-quadro2: vgamem_mb must be a power of two "
                   "from 16 through 128");
        return;
    }
    if (!vga_common_init(vga, OBJECT(s), errp)) {
        return;
    }
    vga->vbe_legacy_mode_switch = true;
    vga->get_bpp = nv15_get_bpp;
    vga->get_params = nv15_get_params;
    vga->get_resolution = nv15_get_resolution;
    vga_init(vga, OBJECT(s), pci_address_space(dev),
             pci_address_space_io(dev), false);
    portio_list_init(&s->vga_port_list, OBJECT(s), nv15_vga_portio_list,
                     s, "nvidia-quadro2-vga");
    portio_list_set_flush_coalesced(&s->vga_port_list);
    portio_list_add(&s->vga_port_list, pci_address_space_io(dev), 0x3b0);
    s->fifo_bh = qemu_bh_new_guarded(nv15_fifo_resume, s,
                                     &DEVICE(s)->mem_reentrancy_guard);
    vga->con = graphic_console_init(DEVICE(s), 0, &nv15_graphic_ops, s);
    if (s->cursor_guest_mode) {
        vga->cursor_invalidate = nv15_cursor_invalidate;
        vga->cursor_draw_line = nv15_cursor_draw_line;
    }

    i2cbus = i2c_init_bus(DEVICE(s), "nvidia-quadro2.ddc");
    bitbang_i2c_init(&s->bbi2c, i2cbus);
    i2c_slave_set_address(I2C_SLAVE(&s->i2cddc), 0x50);
    qdev_realize(DEVICE(&s->i2cddc), BUS(i2cbus), &error_abort);

    memory_region_init_io(&s->mmio, OBJECT(s), &nv15_mmio_ops, s,
                          "nvidia-quadro2-mmio", NVIDIA_QUADRO2_MMIO_SIZE);
    memory_region_init(&s->fb_aperture, OBJECT(s),
                       "nvidia-quadro2-fb-aperture",
                       NVIDIA_QUADRO2_FB_APERTURE_SIZE);
    memory_region_add_subregion(&s->fb_aperture, 0, &vga->vram);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &s->fb_aperture);

    pci_set_byte(dev->config + PCI_REVISION_ID, NVIDIA_QUADRO2_REVISION);
    dev->config[PCI_INTERRUPT_PIN] = 1;
    timer_init_ns(&s->vblank_timer, QEMU_CLOCK_VIRTUAL, nv15_vblank_irq, s);
    timer_init_ns(&s->ptimer_alarm_timer, QEMU_CLOCK_VIRTUAL,
                  nv15_ptimer_alarm, s);
    if (!nv15_init_pci_caps(s, errp)) {
        return;
    }
}

static void nv15_exit(PCIDevice *dev)
{
    NVIDIAQuadro2State *s = NVIDIA_QUADRO2(dev);

    qemu_bh_delete(s->fifo_bh);

    timer_del(&s->vblank_timer);
    timer_del(&s->ptimer_alarm_timer);
    graphic_console_close(s->vga.con);
    cursor_unref(s->cursor);
    s->cursor = NULL;
}

static const Property nv15_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", NVIDIAQuadro2State,
                       vga.vram_size_mb, 64),
    DEFINE_PROP_BOOL("guest_hwcursor", NVIDIAQuadro2State,
                     cursor_guest_mode, false),
    DEFINE_EDID_PROPERTIES(NVIDIAQuadro2State, i2cddc.edid_info),
};

static void nv15_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, nv15_reset);
    device_class_set_props(dc, nv15_properties);
    dc->vmsd = &vmstate_nv15;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    pc->class_id = PCI_CLASS_DISPLAY_VGA;
    pc->vendor_id = PCI_VENDOR_ID_NVIDIA;
    pc->device_id = NVIDIA_QUADRO2_DEVICE_ID;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_NVIDIA;
    pc->subsystem_id = NVIDIA_QUADRO2_SUBSYSTEM_ID;
    pc->romfile = "vgabios-stdvga.bin";
    pc->realize = nv15_realize;
    pc->exit = nv15_exit;
}

static void nv15_init(Object *obj)
{
    NVIDIAQuadro2State *s = NVIDIA_QUADRO2(obj);

    object_initialize_child(obj, "edid", &s->i2cddc, TYPE_I2CDDC);
}

static const TypeInfo nv15_type_info = {
    .name = TYPE_NVIDIA_QUADRO2,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NVIDIAQuadro2State),
    .class_init = nv15_class_init,
    .instance_init = nv15_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void nv15_register_types(void)
{
    type_register_static(&nv15_type_info);
}

type_init(nv15_register_types)
