/*
 * QEMU ATI SVGA emulation
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

/*
 * Rage 128 Pro, Radeon RV100, and Radeon ES1000 VGA, display, cursor, and 2D
 * emulation. RV100 also provides a synchronous software command processor and
 * fixed-function rasterizer.
 * Technical references are listed in docs/devel/gpu-emulation-provenance.rst.
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "vga-access.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_regs.h"
#include "vga_regs.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "trace.h"

#define ATI_DEBUG_HW_CURSOR 0

#define ATI_RAGE128_GEN_INT_STATUS_RESET       0x00080000U
#define ATI_R100_GEN_INT_STATUS_RESET          0x00080000U
#define ATI_CRTC_GEN_CNTL_RESET                0x04000000U
#define ATI_RAGE128_CRTC_EXT_CNTL_RESET        0x00200000U
#define ATI_RAGE128_DAC_CNTL_RESET             0xff00000aU
#define ATI_R100_DAC_CNTL_RESET                0xff00000aU
#define ATI_RV100_CRTC_OFFSET_CNTL_RESET        0x10000000U
/* TODO: Derive CRTC cadence from the programmed PLL and timing registers. */
#define ATI_CRTC_FRAME_NS                       (NANOSECONDS_PER_SECOND / 60)
#define ATI_CRTC_FRAME_MASK                     0x001fffffU

#ifdef CONFIG_PIXMAN
#define DEFAULT_X_PIXMAN 3
#else
#define DEFAULT_X_PIXMAN 0
#endif

static const struct {
    const char *name;
    uint16_t dev_id;
} ati_model_aliases[] = {
    { "rage128p", PCI_DEVICE_ID_ATI_RAGE128_PF },
    { "rv100", PCI_DEVICE_ID_ATI_RADEON_QY },
    { "es1000", PCI_DEVICE_ID_ATI_ES1000 },
};

enum { VGA_MODE, EXT_MODE };

static void ati_vga_set_offset(VGACommonState *vga, uint32_t offs)
{
    int bypp = DIV_ROUND_UP(vga->vbe_regs[VBE_DISPI_INDEX_BPP], BITS_PER_BYTE);

    if (!bypp ||
        vga->vbe_regs[VBE_DISPI_INDEX_YRES] *
        vga->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] * bypp + offs >
        vga->vbe_size) {
        return;
    }
    vga->vbe_start_addr = offs / 4;
}

static bool ati_crtc_enabled(const ATIVGAState *s)
{
    return (s->regs.crtc_gen_cntl & (CRTC2_EXT_DISP_EN | CRTC2_EN)) ==
           (CRTC2_EXT_DISP_EN | CRTC2_EN);
}

static void ati_vga_switch_mode(ATIVGAState *s);

static void ati_crtc_commit_offset(ATIVGAState *s)
{
    bool pitch_changed;

    if (!(s->regs.crtc_offset & CRTC_OFFSET_GUI_TRIG_OFFSET) ||
        (s->regs.crtc_offset & CRTC_OFFSET_LOCK)) {
        return;
    }

    pitch_changed = s->crtc_pitch_active != s->regs.crtc_pitch;
    s->crtc_offset_active = s->regs.crtc_offset & CRTC_OFFSET_MASK;
    s->crtc_pitch_active = s->regs.crtc_pitch;
    s->regs.crtc_offset &= ~CRTC_OFFSET_GUI_TRIG_OFFSET;
    if (ati_crtc_enabled(s)) {
        if (pitch_changed) {
            ati_vga_switch_mode(s);
        } else {
            ati_vga_set_offset(&s->vga, s->crtc_offset_active);
        }
        graphic_hw_invalidate(s->vga.con);
    }
}

static void ati_vga_switch_mode(ATIVGAState *s)
{
    /* TODO: Render overlay/scaler planes; only primary scanout is exposed. */
    DPRINTF("%d -> %d\n",
            s->mode, !!(s->regs.crtc_gen_cntl & CRTC2_EXT_DISP_EN));
    if (s->regs.crtc_gen_cntl & CRTC2_EXT_DISP_EN) {
        /* Extended mode enabled */
        s->mode = EXT_MODE;
        if (s->regs.crtc_gen_cntl & CRTC2_EN) {
            /* CRT controller enabled, use CRTC values */
            uint32_t offs = s->crtc_offset_active;
            int stride = (s->crtc_pitch_active & 0x7ff) * 8;
            int bpp = 0;
            int h, v;

            if (s->regs.crtc_h_total_disp == 0) {
                s->regs.crtc_h_total_disp = ((640 / 8) - 1) << 16;
            }
            if (s->regs.crtc_v_total_disp == 0) {
                s->regs.crtc_v_total_disp = (480 - 1) << 16;
            }
            h = ((s->regs.crtc_h_total_disp >> 16) + 1) * 8;
            v = (s->regs.crtc_v_total_disp >> 16) + 1;
            switch (s->regs.crtc_gen_cntl & CRTC_PIX_WIDTH_MASK) {
            case CRTC_PIX_WIDTH_4BPP:
                bpp = 4;
                break;
            case CRTC_PIX_WIDTH_8BPP:
                bpp = 8;
                break;
            case CRTC_PIX_WIDTH_15BPP:
                bpp = 15;
                break;
            case CRTC_PIX_WIDTH_16BPP:
                bpp = 16;
                break;
            case CRTC_PIX_WIDTH_24BPP:
                bpp = 24;
                break;
            case CRTC_PIX_WIDTH_32BPP:
                bpp = 32;
                break;
            default:
                qemu_log_mask(LOG_UNIMP, "Unsupported bpp value\n");
                return;
            }
            DPRINTF("Switching to %dx%d %d %d @ %x\n", h, v, stride, bpp, offs);
            vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
            vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_DISABLED);
            s->vga.big_endian_fb = (s->regs.config_cntl & APER_0_ENDIAN ||
                                    s->regs.config_cntl & APER_1_ENDIAN ?
                                    true : false);
            /* reset VBE regs then set up mode */
            s->vga.vbe_regs[VBE_DISPI_INDEX_XRES] = h;
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] = v;
            s->vga.vbe_regs[VBE_DISPI_INDEX_BPP] = bpp;
            /* enable mode via ioport so it updates vga regs */
            vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
            vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_ENABLED |
                VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM |
                (s->regs.dac_cntl & DAC_8BIT_EN ? VBE_DISPI_8BIT_DAC : 0));
            /* now set offset and stride because enable resets these */
            if (stride) {
                vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_VIRT_WIDTH);
                vbe_ioport_write_data(&s->vga, 0, stride);
            }
            ati_vga_set_offset(&s->vga, offs);
        }
    } else {
        /* VGA mode enabled */
        s->mode = VGA_MODE;
        vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
        vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_DISABLED);
    }
}

#define ATI_CURSOR_DIMENSION 64
#define ATI_MONO_CURSOR_STRIDE 16
#define ATI_ARGB_CURSOR_STRIDE (ATI_CURSOR_DIMENSION * sizeof(uint32_t))

typedef struct ATICursorParams {
    uint32_t offset;
    uint16_t stride;
    uint8_t x_offset;
    uint8_t width;
    uint8_t height;
    uint8_t mode;
    int x;
    int y;
    bool enabled;
} ATICursorParams;

static void ati_cursor_commit(ATIVGAState *s)
{
    s->cursor_active.offset = s->regs.cur_offset & 0x07fffff0;
    s->cursor_active.hv_pos = s->regs.cur_hv_pos;
    s->cursor_active.hv_offs = s->regs.cur_hv_offs;
}

static bool ati_cursor_get_params(ATIVGAState *s, ATICursorParams *params)
{
    const ATICursorState *cursor = &s->cursor_active;
    uint32_t x_offset = extract32(cursor->hv_offs, 16, 6);
    uint32_t y_offset = extract32(cursor->hv_offs, 0, 6);
    uint64_t bytes;

    memset(params, 0, sizeof(*params));
    params->mode = ati_is_rv100_family(s) ?
        extract32(s->regs.crtc_gen_cntl, R100_CRTC_CUR_MODE_SHIFT, 3) :
        R100_CRTC_CUR_MODE_MONO;
    if (params->mode == R100_CRTC_CUR_MODE_MONO) {
        params->stride = ATI_MONO_CURSOR_STRIDE;
    } else if (ati_is_rv100_family(s) &&
               params->mode == R100_CRTC_CUR_MODE_24BPP) {
        params->stride = ATI_ARGB_CURSOR_STRIDE;
    } else {
        return false;
    }

    params->offset = cursor->offset;
    params->x_offset = x_offset;
    params->width = ATI_CURSOR_DIMENSION - x_offset;
    params->height = ATI_CURSOR_DIMENSION - y_offset;
    params->x = extract32(cursor->hv_pos, 16, 14);
    params->y = extract32(cursor->hv_pos, 0, 12);
    params->enabled = s->regs.crtc_gen_cntl & CRTC2_CUR_EN;

    bytes = (uint64_t)params->height * params->stride;
    return params->offset <= s->vga.vram_size &&
           bytes <= s->vga.vram_size - params->offset;
}

static uint32_t ati_cursor_argb_to_rgba(uint32_t pixel)
{
    uint8_t alpha = pixel >> 24;
    uint8_t red = pixel >> 16;
    uint8_t green = pixel >> 8;
    uint8_t blue = pixel;

    /* R100 stores premultiplied A8R8G8B8; QEMUCursor uses straight RGBA. */
    if (alpha) {
        red = MIN(255U, ((unsigned int)red * 255 + alpha / 2) / alpha);
        green = MIN(255U, ((unsigned int)green * 255 + alpha / 2) / alpha);
        blue = MIN(255U, ((unsigned int)blue * 255 + alpha / 2) / alpha);
    }
    return ((uint32_t)alpha << 24) | ((uint32_t)blue << 16) |
           ((uint32_t)green << 8) | red;
}

static bool ati_cursor_define(ATIVGAState *s, const ATICursorParams *params)
{
    uint8_t and_mask[ATI_CURSOR_DIMENSION *
                     DIV_ROUND_UP(ATI_CURSOR_DIMENSION, 8)] = { 0 };
    uint8_t xor_mask[ATI_CURSOR_DIMENSION *
                     DIV_ROUND_UP(ATI_CURSOR_DIMENSION, 8)] = { 0 };
    unsigned int x;
    unsigned int y;

    if (!ati_3d_consume_command_work(s,
                                     params->width * params->height)) {
        return false;
    }
    if (!s->cursor || s->cursor->width != params->width ||
        s->cursor->height != params->height) {
        cursor_unref(s->cursor);
        s->cursor = cursor_alloc(params->width, params->height);
        if (!s->cursor) {
            return false;
        }
    }
    s->cursor->hot_x = 0;
    s->cursor->hot_y = 0;

    if (params->mode == R100_CRTC_CUR_MODE_24BPP) {
        for (y = 0; y < params->height; y++) {
            const uint8_t *row = s->vga.vram_ptr + params->offset +
                                 (uint64_t)y * params->stride;

            for (x = 0; x < params->width; x++) {
                uint32_t pixel = ldl_le_p(row +
                                         (params->x_offset + x) * 4);

                s->cursor->data[y * params->width + x] =
                    ati_cursor_argb_to_rgba(pixel);
            }
        }
    } else {
        unsigned int bpl = DIV_ROUND_UP(params->width, 8);

        for (y = 0; y < params->height; y++) {
            const uint8_t *row = s->vga.vram_ptr + params->offset +
                                 (uint64_t)y * params->stride;
            uint64_t abits = ldq_be_p(row);
            uint64_t xbits = ldq_be_p(row + 8);

            for (x = 0; x < params->width; x++) {
                uint64_t bit = BIT_ULL(63 - params->x_offset - x);
                uint8_t out_bit = 0x80U >> (x & 7);
                size_t out = y * bpl + x / 8;

                if (abits & bit) {
                    and_mask[out] |= out_bit;
                }
                if (xbits & bit) {
                    xor_mask[out] |= out_bit;
                }
            }
        }
        cursor_set_mono(s->cursor,
                        ati_cursor_argb_to_rgba(0xff000000U |
                                                s->regs.cur_color1),
                        ati_cursor_argb_to_rgba(0xff000000U |
                                                s->regs.cur_color0),
                        xor_mask, 1, and_mask);
    }
    dpy_cursor_define(s->vga.con, s->cursor);
    return true;
}

static void ati_cursor_update_host(ATIVGAState *s, bool redefine)
{
    ATICursorParams params;
    const uint8_t *source;
    uint32_t image_size;
    bool shape_changed;

    if (s->cursor_guest_mode) {
        return;
    }
    if (!ati_cursor_get_params(s, &params) || !params.enabled) {
        s->cursor_image_valid = false;
        if (s->cursor_host_visible) {
            dpy_mouse_set(s->vga.con, 0, 0, false);
            s->cursor_host_visible = false;
        }
        return;
    }

    image_size = params.height * params.stride;
    source = s->vga.vram_ptr + params.offset;
    shape_changed = redefine || !s->cursor_image_valid ||
                    s->cursor_image_offset != params.offset ||
                    s->cursor_image_size != image_size ||
                    s->cursor_image_stride != params.stride ||
                    s->cursor_image_x_offset != params.x_offset ||
                    s->cursor_image_width != params.width ||
                    s->cursor_image_height != params.height ||
                    s->cursor_image_mode != params.mode ||
                    memcmp(s->cursor_image, source, image_size);
    if (shape_changed) {
        if (!ati_cursor_define(s, &params)) {
            s->cursor_image_valid = false;
            if (s->cursor_host_visible) {
                dpy_mouse_set(s->vga.con, 0, 0, false);
                s->cursor_host_visible = false;
            }
            return;
        }
        memcpy(s->cursor_image, source, image_size);
        s->cursor_image_offset = params.offset;
        s->cursor_image_size = image_size;
        s->cursor_image_stride = params.stride;
        s->cursor_image_x_offset = params.x_offset;
        s->cursor_image_width = params.width;
        s->cursor_image_height = params.height;
        s->cursor_image_mode = params.mode;
        s->cursor_image_valid = true;
    }
    if (!s->cursor_host_visible || s->cursor_host_x != params.x ||
        s->cursor_host_y != params.y) {
        dpy_mouse_set(s->vga.con, params.x, params.y, true);
        s->cursor_host_visible = true;
        s->cursor_host_x = params.x;
        s->cursor_host_y = params.y;
    }
}

static void ati_cursor_update_guest_mode(ATIVGAState *s)
{
    ATICursorParams params;
    bool enabled = ati_cursor_get_params(s, &params) && params.enabled;

    if (s->vga.force_shadow != enabled) {
        s->vga.force_shadow = enabled;
        s->vga.graphic_mode = -1;
    }
    graphic_hw_invalidate(s->vga.con);
}

static void ati_cursor_hide_host(ATIVGAState *s)
{
    QEMUCursor *hidden = cursor_builtin_hidden();

    dpy_cursor_define(s->vga.con, hidden);
    cursor_unref(hidden);
}

static void ati_cursor_changed(ATIVGAState *s, bool redefine)
{
    if (s->cursor_guest_mode) {
        ati_cursor_update_guest_mode(s);
    } else {
        ati_cursor_update_host(s, redefine);
    }
}

static void ati_cursor_invalidate_range(VGACommonState *vga, int y,
                                        unsigned int height)
{
    int first = MAX(y, 0);
    int last = MIN(y + (int)height, VGA_MAX_HEIGHT);

    if (first < last) {
        vga_invalidate_scanlines(vga, first, last);
    }
}

static void ati_cursor_invalidate(VGACommonState *vga)
{
    ATIVGAState *s = container_of(vga, ATIVGAState, vga);
    ATICursorParams params;
    const uint8_t *source = NULL;
    uint32_t image_size = 0;
    bool changed;
    bool valid;
    uint8_t width;
    uint8_t height;

    valid = ati_cursor_get_params(s, &params) && params.enabled;
    width = valid ? params.width : 0;
    height = valid ? params.height : 0;
    changed = s->cursor_width != width || s->cursor_height != height ||
              s->cursor_mode != (valid ? params.mode : 0) ||
              s->cursor_x_offset != (valid ? params.x_offset : 0) ||
              vga->hw_cursor_x != (valid ? params.x : 0) ||
              vga->hw_cursor_y != (valid ? params.y : 0) ||
              s->cursor_offset != (valid ? params.offset : 0);
    if (valid) {
        image_size = params.height * params.stride;
        source = vga->vram_ptr + params.offset;
        changed |= !s->cursor_image_valid ||
                   s->cursor_image_offset != params.offset ||
                   s->cursor_image_size != image_size ||
                   s->cursor_image_stride != params.stride ||
                   s->cursor_image_x_offset != params.x_offset ||
                   s->cursor_image_width != params.width ||
                   s->cursor_image_height != params.height ||
                   s->cursor_image_mode != params.mode ||
                   memcmp(s->cursor_image, source, image_size);
    } else {
        changed |= s->cursor_image_valid;
    }
    if (!changed) {
        return;
    }

    ati_cursor_invalidate_range(vga, vga->hw_cursor_y,
                                s->cursor_height);
    vga->hw_cursor_x = valid ? params.x : 0;
    vga->hw_cursor_y = valid ? params.y : 0;
    s->cursor_offset = valid ? params.offset : 0;
    s->cursor_width = width;
    s->cursor_height = height;
    s->cursor_mode = valid ? params.mode : 0;
    s->cursor_x_offset = valid ? params.x_offset : 0;
    if (valid) {
        memcpy(s->cursor_image, source, image_size);
        s->cursor_image_offset = params.offset;
        s->cursor_image_size = image_size;
        s->cursor_image_stride = params.stride;
        s->cursor_image_x_offset = params.x_offset;
        s->cursor_image_width = params.width;
        s->cursor_image_height = params.height;
        s->cursor_image_mode = params.mode;
        s->cursor_image_valid = true;
        ati_cursor_invalidate_range(vga, vga->hw_cursor_y,
                                    s->cursor_height);
    } else {
        s->cursor_image_valid = false;
    }
}

static uint8_t ati_cursor_blend_channel(uint8_t source, uint8_t destination,
                                        uint8_t alpha)
{
    unsigned int value = (unsigned int)source * 255 +
                         (unsigned int)destination * (255 - alpha);

    return MIN(255U, (value + 127) / 255);
}

static void ati_cursor_draw_line(VGACommonState *vga, uint8_t *d, int scr_y)
{
    ATIVGAState *s = container_of(vga, ATIVGAState, vga);
    uint32_t *dp = (uint32_t *)d;
    uint32_t stride;
    uint32_t srcoff;
    int first_x;
    int last_x;
    int screen_x;

    if (!s->cursor_width || !s->cursor_height ||
        scr_y < vga->hw_cursor_y ||
        scr_y >= vga->hw_cursor_y + s->cursor_height ||
        scr_y > s->regs.crtc_v_total_disp >> 16) {
        return;
    }
    stride = s->cursor_mode == R100_CRTC_CUR_MODE_24BPP ?
             ATI_ARGB_CURSOR_STRIDE : ATI_MONO_CURSOR_STRIDE;
    srcoff = s->cursor_offset +
             (scr_y - vga->hw_cursor_y) * stride;
    first_x = MAX(vga->hw_cursor_x, 0);
    last_x = MIN(vga->hw_cursor_x + s->cursor_width,
                 vga->last_scr_width);
    if (first_x >= last_x || srcoff > s->vga.vram_size - stride) {
        return;
    }

    if (s->cursor_mode == R100_CRTC_CUR_MODE_24BPP) {
        for (screen_x = first_x; screen_x < last_x; screen_x++) {
            unsigned int source_x = s->cursor_x_offset +
                                    screen_x - vga->hw_cursor_x;
            uint32_t source = ldl_le_p(vga->vram_ptr + srcoff + source_x * 4);
            uint8_t alpha = source >> 24;
            uint8_t red;
            uint8_t green;
            uint8_t blue;

            if (!alpha) {
                continue;
            }
            red = source >> 16;
            green = source >> 8;
            blue = source;
            if (alpha != 0xff) {
                uint32_t destination = dp[screen_x];

                red = ati_cursor_blend_channel(red, destination >> 16,
                                               alpha);
                green = ati_cursor_blend_channel(green, destination >> 8,
                                                 alpha);
                blue = ati_cursor_blend_channel(blue, destination, alpha);
            }
            dp[screen_x] = rgb_to_pixel32(red, green, blue);
        }
    } else {
        uint64_t abits = ldq_be_p(vga->vram_ptr + srcoff);
        uint64_t xbits = ldq_be_p(vga->vram_ptr + srcoff + 8);

        for (screen_x = first_x; screen_x < last_x; screen_x++) {
            unsigned int source_x = s->cursor_x_offset +
                                    screen_x - vga->hw_cursor_x;
            uint64_t mask = BIT_ULL(63 - source_x);
            uint32_t color;

            if (abits & mask) {
                if (xbits & mask) {
                    color = dp[screen_x] ^ 0xffffffff;
                } else {
                    continue;
                }
            } else {
                color = (xbits & mask ? s->regs.cur_color1 :
                                        s->regs.cur_color0) |
                        0xff000000;
            }
            dp[screen_x] = color;
        }
    }
}

static void ati_graphic_invalidate(void *opaque)
{
    ATIVGAState *s = opaque;

    s->vga.hw_ops->invalidate(&s->vga);
}

static bool ati_graphic_update(void *opaque)
{
    ATIVGAState *s = opaque;
    bool complete = s->vga.hw_ops->gfx_update(&s->vga);

    ati_cursor_update_host(s, false);
    return complete;
}

static void ati_graphic_text_update(void *opaque, uint32_t *text)
{
    ATIVGAState *s = opaque;

    s->vga.hw_ops->text_update(&s->vga, text);
}

static const GraphicHwOps ati_graphic_ops = {
    .invalidate = ati_graphic_invalidate,
    .gfx_update = ati_graphic_update,
    .text_update = ati_graphic_text_update,
};

static uint64_t ati_i2c(bitbang_i2c_interface *i2c, uint64_t data, int base)
{
    bool c = (data & BIT(base + 17) ? !!(data & BIT(base + 1)) : 1);
    bool d = (data & BIT(base + 16) ? !!(data & BIT(base)) : 1);

    bitbang_i2c_set(i2c, BITBANG_I2C_SCL, c);
    d = bitbang_i2c_set(i2c, BITBANG_I2C_SDA, d);

    data &= ~0xf00ULL;
    if (c) {
        data |= BIT(base + 9);
    }
    if (d) {
        data |= BIT(base + 8);
    }
    return data;
}

static void ati_vga_update_irq(ATIVGAState *s)
{
    pci_set_irq(&s->dev, !!(s->regs.gen_int_status & s->regs.gen_int_cntl));
}

static uint32_t ati_crtc_line_mask(const ATIVGAState *s)
{
    return ati_is_rv100_family(s) ? 0xfff : 0x7ff;
}

static uint32_t ati_crtc_h_total_disp_mask(const ATIVGAState *s)
{
    return ati_is_rv100_family(s) ? 0x01ff03ff : 0x00ff01ff;
}

static uint32_t ati_crtc_h_sync_mask(const ATIVGAState *s)
{
    return ati_is_rv100_family(s) ? 0x17bf1fff : 0x00bf0fff;
}

static uint32_t ati_crtc_vtotal(const ATIVGAState *s)
{
    return (s->regs.crtc_v_total_disp & ati_crtc_line_mask(s)) + 1;
}

static bool ati_crtc_vblank_line(const ATIVGAState *s, uint32_t *line)
{
    uint32_t value = ((s->regs.crtc_v_total_disp >> 16) &
                      ati_crtc_line_mask(s)) + 1;

    if (value >= ati_crtc_vtotal(s)) {
        return false;
    }
    *line = value;
    return true;
}

static bool ati_crtc_vsync_line(const ATIVGAState *s, uint32_t *line)
{
    uint32_t value = (s->regs.crtc_v_sync_strt_wid &
                      ati_crtc_line_mask(s)) + 1;

    if (value >= ati_crtc_vtotal(s)) {
        return false;
    }
    *line = value;
    return true;
}

static uint32_t ati_crtc_current_line(const ATIVGAState *s)
{
    int64_t now, elapsed;

    if (!ati_crtc_enabled(s)) {
        return 0;
    }
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    elapsed = now - s->crtc_frame_start_ns;
    if (elapsed <= 0) {
        return 0;
    }
    return muldiv64(elapsed % ATI_CRTC_FRAME_NS,
                    ati_crtc_vtotal(s), ATI_CRTC_FRAME_NS);
}

static uint32_t ati_crtc_next_event_line(const ATIVGAState *s,
                                         uint32_t current)
{
    uint32_t next = ati_crtc_vtotal(s);
    uint32_t line;

    if (ati_crtc_vblank_line(s, &line) && line > current) {
        next = MIN(next, line);
    }
    if (ati_crtc_vsync_line(s, &line) && line > current) {
        next = MIN(next, line);
    }
    line = s->crtc_vline & ati_crtc_line_mask(s);
    if (line < ati_crtc_vtotal(s) && line > current) {
        next = MIN(next, line);
    }
    return next;
}

static void ati_crtc_schedule_from_line(ATIVGAState *s, uint32_t current)
{
    uint32_t total = ati_crtc_vtotal(s);

    s->crtc_event_line = ati_crtc_next_event_line(s, current);
    timer_mod(&s->vblank_timer,
              s->crtc_frame_start_ns +
              DIV_ROUND_UP((uint64_t)ATI_CRTC_FRAME_NS *
                           s->crtc_event_line, total));
}

static void ati_crtc_start(ATIVGAState *s)
{
    timer_del(&s->vblank_timer);
    if (!ati_crtc_enabled(s)) {
        return;
    }
    s->crtc_frame_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (!(s->crtc_vline & ati_crtc_line_mask(s))) {
        s->regs.gen_int_status |= CRTC_VLINE_INT;
    }
    ati_crtc_schedule_from_line(s, 0);
    ati_vga_update_irq(s);
}

static void ati_crtc_reschedule(ATIVGAState *s)
{
    uint32_t line;

    if (!ati_crtc_enabled(s)) {
        timer_del(&s->vblank_timer);
        return;
    }
    line = ati_crtc_current_line(s);
    timer_del(&s->vblank_timer);
    ati_crtc_schedule_from_line(s, line);
}

static uint32_t ati_crtc_status(const ATIVGAState *s)
{
    uint32_t status = s->crtc_vblank_save ? CRTC_VBLANK_SAVE : 0;
    uint32_t line = ati_crtc_current_line(s);
    uint32_t vblank;

    if (ati_crtc_enabled(s)) {
        if (ati_crtc_vblank_line(s, &vblank) && line >= vblank) {
            status |= CRTC_VBLANK_CUR;
        }
        if (line & 1) {
            status |= CRTC_VLINE_SYNC;
        }
    }
    if (s->crtc_frame & 1) {
        status |= CRTC_FRAME;
    }
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF &&
        s->crtc_fix_vsync_timing) {
        status |= CRTC_FIX_VSYNC_TIMING;
    }
    return status;
}

static void ati_crtc_event(void *opaque)
{
    ATIVGAState *s = opaque;
    uint32_t line = s->crtc_event_line;
    uint32_t event_line;

    if (!ati_crtc_enabled(s)) {
        return;
    }
    if (line == ati_crtc_vtotal(s)) {
        s->crtc_frame_start_ns += ATI_CRTC_FRAME_NS;
        if (!(s->crtc_vline & ati_crtc_line_mask(s))) {
            s->regs.gen_int_status |= CRTC_VLINE_INT;
        }
        ati_crtc_schedule_from_line(s, 0);
        ati_vga_update_irq(s);
        return;
    }

    if (ati_crtc_vblank_line(s, &event_line) && line == event_line) {
        ati_crtc_commit_offset(s);
        s->crtc_vblank_save = true;
        s->regs.gen_int_status |= CRTC_VBLANK_INT;
    }
    if ((s->crtc_vline & ati_crtc_line_mask(s)) == line) {
        s->regs.gen_int_status |= CRTC_VLINE_INT;
    }
    if (ati_crtc_vsync_line(s, &event_line) && line == event_line) {
        s->crtc_frame = (s->crtc_frame + 1) & ATI_CRTC_FRAME_MASK;
        s->regs.gen_int_status |= CRTC_VSYNC_INT;
    }
    ati_crtc_schedule_from_line(s, line);
    ati_vga_update_irq(s);
}

static inline uint32_t ati_reg_read_offs(uint32_t reg, int offs,
                                         unsigned int size)
{
    if (offs == 0 && size == 4) {
        return reg;
    } else {
        return extract32(reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE);
    }
}

static inline void ati_reg_write_offs(uint32_t *reg, int offs,
                                      uint64_t data, unsigned int size)
{
    if (offs == 0 && size == 4) {
        *reg = data;
    } else {
        *reg = deposit32(*reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE,
                         data);
    }
}

static uint32_t ati_pll_read(ATIVGAState *s, hwaddr addr,
                             unsigned int size)
{
    unsigned int index = s->regs.clock_cntl_index & PLL_INDEX_MASK;

    return ati_reg_read_offs(s->regs.pll[index], addr - CLOCK_CNTL_DATA,
                             size);
}

static void ati_pll_write(ATIVGAState *s, hwaddr addr, uint64_t data,
                          unsigned int size)
{
    unsigned int index = s->regs.clock_cntl_index & PLL_INDEX_MASK;

    if (!(s->regs.clock_cntl_index & PLL_WR_EN)) {
        return;
    }
    ati_reg_write_offs(&s->regs.pll[index], addr - CLOCK_CNTL_DATA,
                       data, size);
    if (index == PPLL_REF_DIV) {
        /* TODO: Stage PPLL divisors and commit them on ATOMIC_UPDATE_W. */
        s->regs.pll[index] &= ~PPLL_ATOMIC_UPDATE_R;
    }
}

static uint32_t ati_mm_aper_offset(const ATIVGAState *s, hwaddr addr)
{
    uint32_t offset = (s->regs.mm_index & ~BIT(31)) + addr - MM_DATA;

    /* MM_APER (bit 31) selects Linear Aperture 0; physical VRAM aliases. */
    return offset & (s->vga.vram_size - 1);
}

static uint32_t ati_dac_read(const ATIVGAState *s)
{
    uint32_t value = s->regs.dac_cntl;
    uint32_t force = R100_DAC_FORCE_BLANK_OFF_EN | R100_DAC_FORCE_DATA_EN;
    uint32_t channels;
    unsigned int select;

    if (!ati_is_rv100_family(s)) {
        return value;
    }
    value &= ~R100_DAC_CMP_OUTPUT;
    select = (s->regs.dac_ext_cntl & R100_DAC_FORCE_DATA_SEL_MASK) >> 6;
    channels = select == 3 ? R100_DAC_PDWN_R | R100_DAC_PDWN_G |
                            R100_DAC_PDWN_B : BIT(16 + select);

    /* A powered primary DAC reports the connected CRT load. */
    if ((value & R100_DAC_CMP_EN) && !(value & DAC_PDWN) &&
        (s->regs.crtc_ext_cntl & CRT_CRTC_ON) &&
        (s->regs.dac_ext_cntl & force) == force &&
        (s->regs.dac_ext_cntl & R100_DAC_FORCE_DATA_MASK) &&
        !(s->regs.dac_macro_cntl & channels)) {
        value |= R100_DAC_CMP_OUTPUT;
    }
    return value;
}

static uint64_t ati_mm_read(void *opaque, hwaddr addr, unsigned int size)
{
    ATIVGAState *s = opaque;
    uint32_t val = 0;
    uint64_t r100_val;

    if (ati_3d_read(s, addr, &r100_val, size)) {
        trace_ati_mm_read(size, addr, ati_reg_name(addr & ~3ULL), r100_val);
        return r100_val;
    }

    switch (addr) {
    case MM_INDEX:
        val = s->regs.mm_index;
        break;
    case MM_DATA ... MM_DATA + 3:
        /* indexed access to regs or memory */
        if (s->regs.mm_index & BIT(31)) {
            uint32_t idx = ati_mm_aper_offset(s, addr);

            val = ldn_le_p(s->vga.vram_ptr + idx, size);
        } else if (s->regs.mm_index > MM_DATA + 3) {
            val = ati_mm_read(s, s->regs.mm_index + addr - MM_DATA, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "ati_mm_read: mm_index too small: %u\n", s->regs.mm_index);
        }
        break;
    case CLOCK_CNTL_INDEX ... CLOCK_CNTL_INDEX + 3:
        val = ati_reg_read_offs(s->regs.clock_cntl_index,
                                addr - CLOCK_CNTL_INDEX, size);
        break;
    case CLOCK_CNTL_DATA ... CLOCK_CNTL_DATA + 3:
        val = ati_pll_read(s, addr, size);
        break;
    case BIOS_0_SCRATCH ... BUS_CNTL - 1:
    {
        int i = (addr - BIOS_0_SCRATCH) / 4;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF && i > 3) {
            break;
        }
        val = ati_reg_read_offs(s->regs.bios_scratch[i],
                                addr - (BIOS_0_SCRATCH + i * 4), size);
        break;
    }
    case GEN_INT_CNTL:
        val = s->regs.gen_int_cntl;
        break;
    case GEN_INT_STATUS:
        val = s->regs.gen_int_status;
        break;
    case CRTC_GEN_CNTL ... CRTC_GEN_CNTL + 3:
        val = ati_reg_read_offs(s->regs.crtc_gen_cntl,
                                addr - CRTC_GEN_CNTL, size);
        break;
    case CRTC_EXT_CNTL ... CRTC_EXT_CNTL + 3:
        val = ati_reg_read_offs(s->regs.crtc_ext_cntl,
                                addr - CRTC_EXT_CNTL, size);
        break;
    case DAC_CNTL ... DAC_CNTL + 3:
        val = ati_reg_read_offs(ati_dac_read(s), addr - DAC_CNTL, size);
        break;
    case DAC_EXT_CNTL ... DAC_EXT_CNTL + 3:
        if (ati_is_rv100_family(s)) {
            val = ati_reg_read_offs(s->regs.dac_ext_cntl,
                                    addr - DAC_EXT_CNTL, size);
        }
        break;
    case DAC_MACRO_CNTL ... DAC_MACRO_CNTL + 3:
        if (ati_is_rv100_family(s)) {
            val = ati_reg_read_offs(s->regs.dac_macro_cntl,
                                    addr - DAC_MACRO_CNTL, size);
        }
        break;
    case CRTC_STATUS ... CRTC_STATUS + 3:
        val = ati_reg_read_offs(ati_crtc_status(s), addr - CRTC_STATUS,
                                size);
        break;
    case GPIO_VGA_DDC ... GPIO_VGA_DDC + 3:
        val = ati_reg_read_offs(s->regs.gpio_vga_ddc,
                                addr - GPIO_VGA_DDC, size);
        break;
    case GPIO_DVI_DDC ... GPIO_DVI_DDC + 3:
        val = ati_reg_read_offs(s->regs.gpio_dvi_ddc,
                                addr - GPIO_DVI_DDC, size);
        break;
    case GPIO_MONID ... GPIO_MONID + 3:
        val = ati_reg_read_offs(s->regs.gpio_monid,
                                addr - GPIO_MONID, size);
        break;
    case PALETTE_INDEX ... PALETTE_INDEX + 3:
        /* VGA_PEL_IR reads DAC state, not the palette read index. */
        val = s->vga.dac_read_index << 16 | s->vga.dac_write_index;
        val = ati_reg_read_offs(val, addr - PALETTE_INDEX, size);
        break;
    case PALETTE_DATA ... PALETTE_DATA + 3:
        /* One MMIO access transfers an RGB entry, not a VGA component. */
        s->vga.dac_sub_index = 0;
        val = vga_ioport_read(&s->vga, VGA_PEL_D) << 16;
        val |= vga_ioport_read(&s->vga, VGA_PEL_D) << 8;
        val |= vga_ioport_read(&s->vga, VGA_PEL_D);
        val = ati_reg_read_offs(val, addr - PALETTE_DATA, size);
        break;
    case PALETTE_30_DATA:
    {
        unsigned int index = s->vga.dac_read_index++;
        uint8_t *rgb = &s->vga.palette[index * 3];

        val = (s->regs.palette[index] & 0x00300c03) |
              ((uint32_t)rgb[0] << 22) | (rgb[1] << 12) | (rgb[2] << 2);
        s->vga.dac_sub_index = 0;
        break;
    }
    case CNFG_CNTL:
        val = s->regs.config_cntl;
        break;
    case CNFG_MEMSIZE:
        val = s->vga.vram_size;
        break;
    case CONFIG_APER_0_BASE:
    case CONFIG_APER_1_BASE:
        val = pci_default_read_config(&s->dev,
                                      PCI_BASE_ADDRESS_0, size) & 0xfffffff0;
        break;
    case CONFIG_APER_SIZE:
        val = memory_region_size(&s->linear_aper) / 2;
        break;
    case CONFIG_REG_1_BASE:
        val = pci_default_read_config(&s->dev,
                                      PCI_BASE_ADDRESS_2, size) & 0xfffffff0;
        break;
    case CONFIG_REG_APER_SIZE:
        val = memory_region_size(&s->mm) / 2;
        break;
    case HOST_PATH_CNTL:
        val = BIT(23); /* Radeon HDP_APER_CNTL */
        break;
    case MC_STATUS:
        val = 5;
        break;
    case MEM_SDRAM_MODE_REG:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val = BIT(28) | BIT(20);
        }
        break;
    case RBBM_STATUS:
    case GUI_STAT:
        val = 64; /* free CMDFIFO entries */
        break;
    case CRTC_H_TOTAL_DISP ... CRTC_H_TOTAL_DISP + 3:
        val = ati_reg_read_offs(s->regs.crtc_h_total_disp,
                                addr - CRTC_H_TOTAL_DISP, size);
        break;
    case CRTC_H_SYNC_STRT_WID ... CRTC_H_SYNC_STRT_WID + 3:
        val = ati_reg_read_offs(s->regs.crtc_h_sync_strt_wid,
                                addr - CRTC_H_SYNC_STRT_WID, size);
        break;
    case CRTC_V_TOTAL_DISP ... CRTC_V_TOTAL_DISP + 3:
        val = ati_reg_read_offs(s->regs.crtc_v_total_disp,
                                addr - CRTC_V_TOTAL_DISP, size);
        break;
    case CRTC_V_SYNC_STRT_WID ... CRTC_V_SYNC_STRT_WID + 3:
        val = ati_reg_read_offs(s->regs.crtc_v_sync_strt_wid,
                                addr - CRTC_V_SYNC_STRT_WID, size);
        break;
    case CRTC_VLINE_CRNT_VLINE ... CRTC_VLINE_CRNT_VLINE + 3:
        val = ati_reg_read_offs(s->crtc_vline |
                                (ati_crtc_current_line(s) << 16),
                                addr - CRTC_VLINE_CRNT_VLINE, size);
        break;
    case CRTC_CRNT_FRAME ... CRTC_CRNT_FRAME + 3:
        val = ati_reg_read_offs(s->crtc_frame, addr - CRTC_CRNT_FRAME,
                                size);
        break;
    case CRTC_OFFSET:
        val = s->regs.crtc_offset;
        break;
    case CRTC_OFFSET_CNTL ... CRTC_OFFSET_CNTL + 3:
        val = ati_reg_read_offs(
            (s->regs.crtc_offset_cntl &
             ~(CRTC_OFFSET_LOCK | CRTC_OFFSET_GUI_TRIG_OFFSET)) |
            (s->regs.crtc_offset &
             (CRTC_OFFSET_LOCK | CRTC_OFFSET_GUI_TRIG_OFFSET)),
            addr - CRTC_OFFSET_CNTL, size);
        break;
    case CRTC_PITCH:
        val = s->regs.crtc_pitch;
        break;
    case 0xf00 ... 0xfff:
        val = pci_default_read_config(&s->dev, addr - 0xf00, size);
        break;
    case CUR_OFFSET ... CUR_OFFSET + 3:
        val = ati_reg_read_offs(s->regs.cur_offset, addr - CUR_OFFSET, size);
        break;
    case CUR_HORZ_VERT_POSN ... CUR_HORZ_VERT_POSN + 3:
        val = ati_reg_read_offs(s->regs.cur_hv_pos |
                                (s->regs.cur_offset & BIT(31)),
                                addr - CUR_HORZ_VERT_POSN, size);
        break;
    case CUR_HORZ_VERT_OFF ... CUR_HORZ_VERT_OFF + 3:
        val = ati_reg_read_offs(s->regs.cur_hv_offs |
                                (s->regs.cur_offset & BIT(31)),
                                addr - CUR_HORZ_VERT_OFF, size);
        break;
    case CUR_CLR0 ... CUR_CLR0 + 3:
        val = ati_reg_read_offs(s->regs.cur_color0, addr - CUR_CLR0, size);
        break;
    case CUR_CLR1 ... CUR_CLR1 + 3:
        val = ati_reg_read_offs(s->regs.cur_color1, addr - CUR_CLR1, size);
        break;
    case DST_OFFSET:
        val = s->regs.dst_offset;
        break;
    case DST_PITCH:
        val = s->regs.dst_pitch;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val |= s->regs.dst_tile << 16;
        }
        break;
    case DST_WIDTH:
        val = s->regs.dst_width;
        break;
    case DST_HEIGHT:
        val = s->regs.dst_height;
        break;
    case SRC_X:
        val = s->regs.src_x;
        break;
    case SRC_Y:
        val = s->regs.src_y;
        break;
    case DST_X:
        val = s->regs.dst_x;
        break;
    case DST_Y:
        val = s->regs.dst_y;
        break;
    case DP_GUI_MASTER_CNTL:
        /* DP_GUI_MASTER_CNTL aliases fields from DP_MIX and DP_DATATYPE */
        val = s->regs.dp_gui_master_cntl |
              ((s->regs.dp_datatype & DP_BRUSH_DATATYPE) >> 4) |
              ((s->regs.dp_datatype & DP_DST_DATATYPE) << 8) |
              ((s->regs.dp_datatype & DP_SRC_DATATYPE) >> 4) |
              (s->regs.dp_mix & DP_ROP3) |
              ((s->regs.dp_mix & DP_SRC_SOURCE) << 16);
        if (ati_is_rv100_family(s)) {
            val = (val & ~R100_GMC_SRC_DATATYPE2) |
                  ((s->regs.dp_datatype & R100_DP_SRC_DATATYPE2) << 9);
        }
        break;
    case BRUSH_Y_X ... BRUSH_Y_X + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val = ati_reg_read_offs(s->regs.brush_y_x,
                                    addr - BRUSH_Y_X, size);
        }
        break;
    case SRC_OFFSET:
        val = s->regs.src_offset;
        break;
    case SRC_PITCH:
        val = s->regs.src_pitch;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val |= s->regs.src_tile << 16;
        }
        break;
    case DP_BRUSH_BKGD_CLR:
        val = s->regs.dp_brush_bkgd_clr;
        break;
    case DP_BRUSH_FRGD_CLR:
        val = s->regs.dp_brush_frgd_clr;
        break;
    case BRUSH_DATA0 ... BRUSH_DATA63 + 3:
    {
        unsigned int i = (addr - BRUSH_DATA0) / sizeof(uint32_t);

        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val = ati_reg_read_offs(s->regs.brush_data[i],
                                    (addr - BRUSH_DATA0) % sizeof(uint32_t),
                                    size);
        }
        break;
    }
    case DP_SRC_FRGD_CLR:
        val = s->regs.dp_src_frgd_clr;
        break;
    case DP_SRC_BKGD_CLR:
        val = s->regs.dp_src_bkgd_clr;
        break;
    case DP_CNTL:
        val = s->regs.dp_cntl;
        break;
    case DP_DATATYPE:
        val = s->regs.dp_datatype;
        break;
    case DP_MIX:
        val = s->regs.dp_mix;
        break;
    case DP_WRITE_MASK:
        val = s->regs.dp_write_mask;
        break;
    case CLR_CMP_CNTL:
        val = s->regs.clr_cmp_cntl;
        break;
    case CLR_CMP_CLR_SRC:
        val = s->regs.clr_cmp_clr_src;
        break;
    case CLR_CMP_CLR_DST:
        val = s->regs.clr_cmp_clr_dst;
        break;
    case CLR_CMP_MASK:
        val = s->regs.clr_cmp_mask;
        break;
    case RBBM_GUICNTL ... RBBM_GUICNTL + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val = ati_reg_read_offs(s->regs.rbbm_guicntl,
                                    addr - RBBM_GUICNTL, size);
        }
        break;
    case DEFAULT_OFFSET:
        val = s->regs.default_offset;
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val >>= 10;
            val |= s->regs.default_pitch << 16;
            val |= s->regs.default_tile << 30;
        }
        break;
    case DEFAULT_PITCH:
        val = s->regs.default_pitch;
        val |= s->regs.default_tile << 16;
        break;
    case DEFAULT_SC_BOTTOM_RIGHT:
        val = s->regs.default_sc_right;
        val |= s->regs.default_sc_bottom << 16;
        break;
    case SC_TOP:
        val = s->regs.sc_top;
        break;
    case SC_LEFT:
        val = s->regs.sc_left;
        break;
    case SC_BOTTOM:
        val = s->regs.sc_bottom;
        break;
    case SC_RIGHT:
        val = s->regs.sc_right;
        break;
    case SRC_SC_BOTTOM:
        val = s->regs.src_sc_bottom;
        break;
    case SRC_SC_RIGHT:
        val = s->regs.src_sc_right;
        break;
    case SC_TOP_LEFT:
    case SC_BOTTOM_RIGHT:
    case SRC_SC_BOTTOM_RIGHT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from write-only register 0x%x\n", (unsigned)addr);
        break;
    default:
        break;
    }
    if (addr < CUR_OFFSET || addr > CUR_CLR1 || ATI_DEBUG_HW_CURSOR) {
        trace_ati_mm_read(size, addr, ati_reg_name(addr & ~3ULL), val);
    }
    return val;
}

static uint16_t ati_scissor_value(const ATIVGAState *s, uint32_t value)
{
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        return value & 0x3fff;
    }

    /* Radeon scissors use bit 15 as the sign for a 14-bit magnitude. */
    return value & (BIT(15) | 0x3fff);
}

static uint32_t ati_brush_y_x_mask(const ATIVGAState *s)
{
    return s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF ? 0 :
           R100_BRUSH_Y_X_MASK;
}

void ati_mmio_write(ATIVGAState *s, hwaddr addr, uint64_t data,
                    unsigned int size)
{
    if (addr < CUR_OFFSET || addr > CUR_CLR1 || ATI_DEBUG_HW_CURSOR) {
        trace_ati_mm_write(size, addr, ati_reg_name(addr & ~3ULL), data);
    }
    if (ati_3d_write(s, addr, data, size)) {
        return;
    }
    switch (addr) {
    case MM_INDEX:
        s->regs.mm_index = data & ~3;
        break;
    case MM_DATA ... MM_DATA + 3:
        /* indexed access to regs or memory */
        if (s->regs.mm_index & BIT(31)) {
            uint32_t idx = ati_mm_aper_offset(s, addr);

            stn_le_p(s->vga.vram_ptr + idx, size, data);
        } else if (s->regs.mm_index > MM_DATA + 3) {
            ati_mmio_write(s, s->regs.mm_index + addr - MM_DATA, data, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "ati_mm_write: mm_index too small: %u\n", s->regs.mm_index);
        }
        break;
    case CLOCK_CNTL_INDEX ... CLOCK_CNTL_INDEX + 3:
        ati_reg_write_offs(&s->regs.clock_cntl_index,
                           addr - CLOCK_CNTL_INDEX, data, size);
        s->regs.clock_cntl_index &= PLL_INDEX_CNTL_MASK;
        break;
    case CLOCK_CNTL_DATA ... CLOCK_CNTL_DATA + 3:
        ati_pll_write(s, addr, data, size);
        break;
    case BIOS_0_SCRATCH ... BUS_CNTL - 1:
    {
        int i = (addr - BIOS_0_SCRATCH) / 4;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF && i > 3) {
            break;
        }
        ati_reg_write_offs(&s->regs.bios_scratch[i],
                           addr - (BIOS_0_SCRATCH + i * 4), data, size);
        break;
    }
    case GEN_INT_CNTL:
        s->regs.gen_int_cntl = data;
        ati_vga_update_irq(s);
        break;
    case GEN_INT_STATUS:
    {
        bool fire = ati_is_rv100_family(s) && (data & SW_INT_FIRE);

        data &= (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF ?
                 0x000f040fUL : 0xfe080effUL);
        s->regs.gen_int_status &= ~data;
        if (fire) {
            s->regs.gen_int_status |= SW_INT_TEST;
        }
        ati_vga_update_irq(s);
        break;
    }
    case CRTC_STATUS ... CRTC_STATUS + 3:
    {
        uint32_t value = s->crtc_fix_vsync_timing ?
                         CRTC_FIX_VSYNC_TIMING : 0;

        ati_reg_write_offs(&value, addr - CRTC_STATUS, data, size);
        if (value & CRTC_VBLANK_SAVE) {
            s->crtc_vblank_save = false;
        }
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->crtc_fix_vsync_timing = value & CRTC_FIX_VSYNC_TIMING;
        }
        break;
    }
    case CRTC_GEN_CNTL ... CRTC_GEN_CNTL + 3:
    {
        uint32_t val = s->regs.crtc_gen_cntl;
        uint32_t cursor_mask = CRTC2_CUR_EN;
        bool was_enabled = ati_crtc_enabled(s);

        if (ati_is_rv100_family(s)) {
            cursor_mask |= R100_CRTC_CUR_MODE_MASK;
        }
        ati_reg_write_offs(&s->regs.crtc_gen_cntl,
                           addr - CRTC_GEN_CNTL, data, size);
        if ((val & CRTC2_CUR_EN) != (s->regs.crtc_gen_cntl & CRTC2_CUR_EN)) {
            ati_vga_switch_mode(s);
        }
        if ((val & cursor_mask) !=
            (s->regs.crtc_gen_cntl & cursor_mask)) {
            if (s->cursor_guest_mode) {
                ati_cursor_update_guest_mode(s);
            } else {
                ati_cursor_update_host(s, true);
            }
        }
        if ((val ^ s->regs.crtc_gen_cntl) &
            (CRTC2_EXT_DISP_EN | CRTC2_EN | CRTC_PIX_WIDTH_MASK)) {
            ati_vga_switch_mode(s);
        }
        if (was_enabled != ati_crtc_enabled(s)) {
            if (ati_crtc_enabled(s)) {
                ati_crtc_start(s);
            } else {
                timer_del(&s->vblank_timer);
            }
        }
        break;
    }
    case CRTC_EXT_CNTL ... CRTC_EXT_CNTL + 3:
    {
        uint32_t val = s->regs.crtc_ext_cntl;
        ati_reg_write_offs(&s->regs.crtc_ext_cntl,
                           addr - CRTC_EXT_CNTL, data, size);
        if (s->regs.crtc_ext_cntl & CRT_CRTC_DISPLAY_DIS) {
            DPRINTF("Display disabled\n");
            s->vga.ar_index &= ~BIT(5);
        } else {
            DPRINTF("Display enabled\n");
            s->vga.ar_index |= BIT(5);
            ati_vga_switch_mode(s);
        }
        if ((val & CRT_CRTC_DISPLAY_DIS) !=
            (s->regs.crtc_ext_cntl & CRT_CRTC_DISPLAY_DIS)) {
            ati_vga_switch_mode(s);
        }
        break;
    }
    case DAC_CNTL ... DAC_CNTL + 3:
        ati_reg_write_offs(&s->regs.dac_cntl, addr - DAC_CNTL, data, size);
        s->regs.dac_cntl &= 0xffffe3ff;
        if (ati_is_rv100_family(s)) {
            s->regs.dac_cntl &= ~R100_DAC_CMP_OUTPUT;
        }
        s->vga.dac_8bit = !!(s->regs.dac_cntl & DAC_8BIT_EN);
        break;
    case DAC_EXT_CNTL ... DAC_EXT_CNTL + 3:
        if (ati_is_rv100_family(s)) {
            ati_reg_write_offs(&s->regs.dac_ext_cntl,
                               addr - DAC_EXT_CNTL, data, size);
        }
        break;
    case DAC_MACRO_CNTL ... DAC_MACRO_CNTL + 3:
        if (ati_is_rv100_family(s)) {
            ati_reg_write_offs(&s->regs.dac_macro_cntl,
                               addr - DAC_MACRO_CNTL, data, size);
        }
        break;
    /*
     * GPIO regs for DDC access. Because some drivers access these via
     * multiple byte writes we have to be careful when we send bits to
     * avoid spurious changes in bitbang_i2c state. Only do it when either
     * the enable bits are changed or output bits changed while enabled.
     */
    case GPIO_VGA_DDC ... GPIO_VGA_DDC + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.gpio_vga_ddc,
                               addr - GPIO_VGA_DDC, data, size);
            if ((addr <= GPIO_VGA_DDC + 2 &&
                 addr + size > GPIO_VGA_DDC + 2) ||
                (addr == GPIO_VGA_DDC &&
                 (s->regs.gpio_vga_ddc & 0x30000))) {
                s->regs.gpio_vga_ddc = ati_i2c(&s->bbi2c,
                                               s->regs.gpio_vga_ddc, 0);
            }
        }
        break;
    case GPIO_DVI_DDC ... GPIO_DVI_DDC + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.gpio_dvi_ddc,
                               addr - GPIO_DVI_DDC, data, size);
            if ((addr <= GPIO_DVI_DDC + 2 && addr + size > GPIO_DVI_DDC + 2) ||
                (addr == GPIO_DVI_DDC && (s->regs.gpio_dvi_ddc & 0x30000))) {
                s->regs.gpio_dvi_ddc = ati_i2c(&s->bbi2c,
                                               s->regs.gpio_dvi_ddc, 0);
            }
        }
        break;
    case GPIO_MONID ... GPIO_MONID + 3:
        /* TODO: Implement Radeon MONID GPIO behavior. */
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            /* Rage128p accesses DDC via MONID(1-2) with additional mask bit */
            ati_reg_write_offs(&s->regs.gpio_monid,
                               addr - GPIO_MONID, data, size);
            if ((s->regs.gpio_monid & BIT(25)) &&
                ((addr <= GPIO_MONID + 2 && addr + size > GPIO_MONID + 2) ||
                 (addr == GPIO_MONID && (s->regs.gpio_monid & 0x60000)))) {
                s->regs.gpio_monid = ati_i2c(&s->bbi2c, s->regs.gpio_monid, 1);
            }
        }
        break;
    case PALETTE_INDEX ... PALETTE_INDEX + 3:
        if (addr <= PALETTE_INDEX + 2 && addr + size > PALETTE_INDEX + 2) {
            vga_ioport_write(&s->vga, VGA_PEL_IR,
                             (data >> ((PALETTE_INDEX + 2 - addr) * 8)) &
                             0xff);
        }
        if (addr == PALETTE_INDEX) {
            vga_ioport_write(&s->vga, VGA_PEL_IW, data & 0xff);
        }
        break;
    case PALETTE_DATA ... PALETTE_DATA + 3:
    {
        unsigned int index = s->vga.dac_write_index;
        uint8_t *rgb = &s->vga.palette[index * 3];
        uint32_t color = rgb[0] << 16 | rgb[1] << 8 | rgb[2];

        ati_reg_write_offs(&color, addr - PALETTE_DATA, data, size);
        s->regs.palette[index] = ((color & 0xff0000) << 6) |
                                 ((color & 0x00ff00) << 4) |
                                 ((color & 0x0000ff) << 2);
        s->vga.dac_sub_index = 0;
        vga_ioport_write(&s->vga, VGA_PEL_D, (color >> 16) & 0xff);
        vga_ioport_write(&s->vga, VGA_PEL_D, (color >> 8) & 0xff);
        vga_ioport_write(&s->vga, VGA_PEL_D, color & 0xff);
        break;
    }
    case PALETTE_30_DATA:
        s->regs.palette[vga_ioport_read(&s->vga, VGA_PEL_IW)] = data;
        s->vga.dac_sub_index = 0;
        vga_ioport_write(&s->vga, VGA_PEL_D, (data >> 22) & 0xff);
        vga_ioport_write(&s->vga, VGA_PEL_D, (data >> 12) & 0xff);
        vga_ioport_write(&s->vga, VGA_PEL_D, (data >> 2) & 0xff);
        break;
    case CNFG_CNTL:
        s->regs.config_cntl = data;
        break;
    case CRTC_H_TOTAL_DISP ... CRTC_H_TOTAL_DISP + 3:
    {
        uint32_t old = s->regs.crtc_h_total_disp;
        uint32_t value = s->regs.crtc_h_total_disp;

        ati_reg_write_offs(&value, addr - CRTC_H_TOTAL_DISP, data, size);
        s->regs.crtc_h_total_disp = value &
                                    ati_crtc_h_total_disp_mask(s);
        if (ati_crtc_enabled(s) &&
            ((old ^ s->regs.crtc_h_total_disp) & 0xffff0000)) {
            ati_vga_switch_mode(s);
        }
        break;
    }
    case CRTC_H_SYNC_STRT_WID ... CRTC_H_SYNC_STRT_WID + 3:
    {
        uint32_t value = s->regs.crtc_h_sync_strt_wid;

        ati_reg_write_offs(&value, addr - CRTC_H_SYNC_STRT_WID, data,
                           size);
        s->regs.crtc_h_sync_strt_wid = value & ati_crtc_h_sync_mask(s);
        break;
    }
    case CRTC_V_TOTAL_DISP ... CRTC_V_TOTAL_DISP + 3:
    {
        uint32_t old = s->regs.crtc_v_total_disp;
        uint32_t value = s->regs.crtc_v_total_disp;

        ati_reg_write_offs(&value, addr - CRTC_V_TOTAL_DISP, data, size);
        s->regs.crtc_v_total_disp = value &
            (ati_is_rv100_family(s) ? 0x0fff0fff : 0x07ff07ff);
        if (ati_crtc_enabled(s) &&
            ((old ^ s->regs.crtc_v_total_disp) & 0xffff0000)) {
            ati_vga_switch_mode(s);
        }
        ati_crtc_reschedule(s);
        break;
    }
    case CRTC_V_SYNC_STRT_WID ... CRTC_V_SYNC_STRT_WID + 3:
    {
        uint32_t value = s->regs.crtc_v_sync_strt_wid;

        ati_reg_write_offs(&value, addr - CRTC_V_SYNC_STRT_WID, data,
                           size);
        s->regs.crtc_v_sync_strt_wid = value &
            (ati_is_rv100_family(s) ? 0x009f0fff : 0x009f07ff);
        ati_crtc_reschedule(s);
        break;
    }
    case CRTC_VLINE_CRNT_VLINE ... CRTC_VLINE_CRNT_VLINE + 3:
    {
        uint32_t value = s->crtc_vline;

        ati_reg_write_offs(&value, addr - CRTC_VLINE_CRNT_VLINE,
                           data, size);
        s->crtc_vline = value & ati_crtc_line_mask(s);
        ati_crtc_reschedule(s);
        break;
    }
    case CRTC_OFFSET:
    {
        uint32_t old = s->regs.crtc_offset;
        uint32_t value = data & (CRTC_OFFSET_MASK | CRTC_OFFSET_LOCK);

        if (!ati_crtc_enabled(s)) {
            s->regs.crtc_offset = value;
            s->crtc_offset_active = value & CRTC_OFFSET_MASK;
            s->crtc_pitch_active = s->regs.crtc_pitch;
            ati_vga_set_offset(&s->vga, s->crtc_offset_active);
        } else if (value & CRTC_OFFSET_LOCK) {
            s->regs.crtc_offset = value | CRTC_OFFSET_GUI_TRIG_OFFSET;
        } else if (old & (CRTC_OFFSET_LOCK |
                          CRTC_OFFSET_GUI_TRIG_OFFSET)) {
            s->regs.crtc_offset = value | CRTC_OFFSET_GUI_TRIG_OFFSET;
            if (!timer_pending(&s->vblank_timer)) {
                ati_crtc_reschedule(s);
            }
        } else {
            s->regs.crtc_offset = value;
            s->crtc_offset_active = value & CRTC_OFFSET_MASK;
            ati_vga_set_offset(&s->vga, s->crtc_offset_active);
            graphic_hw_invalidate(s->vga.con);
        }
        break;
    }
    case CRTC_OFFSET_CNTL ... CRTC_OFFSET_CNTL + 3:
    {
        uint32_t value = (s->regs.crtc_offset_cntl &
                          ~(CRTC_OFFSET_LOCK | CRTC_OFFSET_GUI_TRIG_OFFSET)) |
                         (s->regs.crtc_offset & CRTC_OFFSET_LOCK);

        ati_reg_write_offs(&value, addr - CRTC_OFFSET_CNTL, data, size);
        /* TODO: Implement CRTC scanout tiling selected by this register. */
        s->regs.crtc_offset_cntl = value &
                                  ~(CRTC_OFFSET_LOCK |
                                    CRTC_OFFSET_GUI_TRIG_OFFSET);
        /* Shared OFFSET_LOCK is writable; GUI_TRIG_OFFSET is read-only. */
        s->regs.crtc_offset = (s->regs.crtc_offset & ~CRTC_OFFSET_LOCK) |
                              (value & CRTC_OFFSET_LOCK);
        if (!(value & CRTC_OFFSET_LOCK) &&
            (s->regs.crtc_offset & CRTC_OFFSET_GUI_TRIG_OFFSET)) {
            if (!ati_crtc_enabled(s)) {
                ati_crtc_commit_offset(s);
            } else if (!timer_pending(&s->vblank_timer)) {
                ati_crtc_reschedule(s);
            }
        }
        break;
    }
    case CRTC_PITCH:
        data &= 0x07ff07ff;
        if (s->regs.crtc_pitch != data) {
            s->regs.crtc_pitch = data;
            if (!(s->regs.crtc_offset & CRTC_OFFSET_GUI_TRIG_OFFSET)) {
                s->crtc_pitch_active = data;
                ati_vga_switch_mode(s);
            }
        }
        break;
    case 0xf00 ... 0xfff:
        /* read-only copy of PCI config space so ignore writes */
        break;
    case CUR_OFFSET ... CUR_OFFSET + 3:
    {
        uint32_t t = s->regs.cur_offset;

        ati_reg_write_offs(&t, addr - CUR_OFFSET, data, size);
        t &= 0x87fffff0;
        if (s->regs.cur_offset != t) {
            s->regs.cur_offset = t;
            if (!(t & BIT(31))) {
                ati_cursor_commit(s);
                ati_cursor_changed(s, true);
            }
        }
        break;
    }
    case CUR_HORZ_VERT_POSN ... CUR_HORZ_VERT_POSN + 3:
    {
        bool was_locked = s->regs.cur_offset & BIT(31);
        uint32_t t = s->regs.cur_hv_pos | (s->regs.cur_offset & BIT(31));

        ati_reg_write_offs(&t, addr - CUR_HORZ_VERT_POSN, data, size);
        s->regs.cur_hv_pos = t & 0x3fff0fff;
        if (t & BIT(31)) {
            s->regs.cur_offset |= t & BIT(31);
        } else if (s->regs.cur_offset & BIT(31)) {
            s->regs.cur_offset &= ~BIT(31);
        }
        if (!(t & BIT(31))) {
            ati_cursor_commit(s);
            ati_cursor_changed(s, was_locked);
        }
        break;
    }
    case CUR_HORZ_VERT_OFF ... CUR_HORZ_VERT_OFF + 3:
    {
        uint32_t t = s->regs.cur_hv_offs | (s->regs.cur_offset & BIT(31));

        ati_reg_write_offs(&t, addr - CUR_HORZ_VERT_OFF, data, size);
        s->regs.cur_hv_offs = t & 0x3f003f;
        if (t & BIT(31)) {
            s->regs.cur_offset |= t & BIT(31);
        } else if (s->regs.cur_offset & BIT(31)) {
            s->regs.cur_offset &= ~BIT(31);
        }
        if (!(t & BIT(31))) {
            ati_cursor_commit(s);
            ati_cursor_changed(s, true);
        }
        break;
    }
    case CUR_CLR0 ... CUR_CLR0 + 3:
    {
        uint32_t t = s->regs.cur_color0;

        ati_reg_write_offs(&t, addr - CUR_CLR0, data, size);
        t &= 0xffffff;
        if (s->regs.cur_color0 != t) {
            s->regs.cur_color0 = t;
            ati_cursor_changed(s, true);
        }
        break;
    }
    case CUR_CLR1 ... CUR_CLR1 + 3:
        /*
         * Update cursor unconditionally here because some clients set up
         * other registers before actually writing cursor data to memory at
         * offset so we would miss cursor change unless always updating here
         */
        ati_reg_write_offs(&s->regs.cur_color1, addr - CUR_CLR1, data, size);
        s->regs.cur_color1 &= 0xffffff;
        ati_cursor_changed(s, true);
        break;
    case DST_OFFSET:
            s->regs.dst_offset = data & 0xfffffff0;
        break;
    case DST_PITCH:
            s->regs.dst_pitch = data & 0x3fff;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dst_tile = (data >> 16) & 1;
        }
        break;
    case DST_TILE:
        if (ati_is_rv100_family(s)) {
            s->regs.dst_tile = data & 3;
        }
        break;
    case DST_WIDTH:
        s->regs.dst_width = data & 0x3fff;
        ati_2d_blt(s);
        break;
    case DST_HEIGHT:
        s->regs.dst_height = data & 0x3fff;
        break;
    case SRC_X:
        s->regs.src_x = data & 0x3fff;
        break;
    case SRC_Y:
        s->regs.src_y = data & 0x3fff;
        break;
    case DST_X:
        s->regs.dst_x = data & 0x3fff;
        break;
    case DST_Y:
        s->regs.dst_y = data & 0x3fff;
        break;
    case SRC_PITCH_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.src_offset = (data & 0x1fffff) << 5;
            s->regs.src_pitch = (data & 0x7fe00000) >> 21;
            s->regs.src_tile = data >> 31;
        } else {
            s->regs.src_offset = (data & 0x3fffff) << 10;
            s->regs.src_pitch = (data & 0x3fc00000) >> 16;
            s->regs.src_tile = (data >> 30) & 1;
        }
        break;
    case DST_PITCH_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dst_offset = (data & 0x1fffff) << 5;
            s->regs.dst_pitch = (data & 0x7fe00000) >> 21;
            s->regs.dst_tile = data >> 31;
        } else {
            s->regs.dst_offset = (data & 0x3fffff) << 10;
            s->regs.dst_pitch = (data & 0x3fc00000) >> 16;
            s->regs.dst_tile = data >> 30;
        }
        break;
    case SRC_Y_X:
        s->regs.src_x = data & 0x3fff;
        s->regs.src_y = (data >> 16) & 0x3fff;
        break;
    case DST_Y_X:
        s->regs.dst_x = data & 0x3fff;
        s->regs.dst_y = (data >> 16) & 0x3fff;
        break;
    case DST_HEIGHT_WIDTH:
        s->regs.dst_width = data & 0x3fff;
        s->regs.dst_height = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case DP_GUI_MASTER_CNTL:
        s->regs.dp_gui_master_cntl = data & 0xf800000f;
        s->regs.dp_datatype = (data & 0x0f00) >> 8 | (data & 0x30f0) << 4 |
                              (data & 0x4000) << 16;
        if (ati_is_rv100_family(s)) {
            s->regs.dp_datatype |= (data & R100_GMC_SRC_DATATYPE2) >> 9;
        }
        s->regs.dp_mix = (data & GMC_ROP3_MASK) | (data & 0x7000000) >> 16;
        s->regs.dp_cntl |= DST_Y_TOP_TO_BOTTOM;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dp_cntl |= DST_X_LEFT_TO_RIGHT;
        }
        if (data & GMC_WRITE_MASK_SET) {
            s->regs.dp_write_mask = UINT32_MAX;
            s->regs.clr_cmp_mask = UINT32_MAX;
        }
        if (data & GMC_DST_CLR_CMP_FCN_CLEAR) {
            s->regs.clr_cmp_cntl &=
                ~(CLR_CMP_FN_SRC_MASK | CLR_CMP_FN_DST_MASK);
        }

        if (!(data & GMC_SRC_PITCH_OFFSET_CNTL)) {
            s->regs.src_offset = s->regs.default_offset;
            s->regs.src_pitch = s->regs.default_pitch;
        }
        if (!(data & GMC_DST_PITCH_OFFSET_CNTL)) {
            s->regs.dst_offset = s->regs.default_offset;
            s->regs.dst_pitch = s->regs.default_pitch;
        }
        if (!(data & GMC_SRC_CLIPPING)) {
            s->regs.src_sc_right = s->regs.default_sc_right;
            s->regs.src_sc_bottom = s->regs.default_sc_bottom;
        }
        if (!(data & GMC_DST_CLIPPING)) {
            s->regs.sc_top = 0;
            s->regs.sc_left = 0;
            s->regs.sc_right = s->regs.default_sc_right;
            s->regs.sc_bottom = s->regs.default_sc_bottom;
        }
        break;
    case BRUSH_Y_X ... BRUSH_Y_X + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.brush_y_x, addr - BRUSH_Y_X,
                               data, size);
            s->regs.brush_y_x &= ati_brush_y_x_mask(s);
        }
        break;
    case DST_WIDTH_X:
        s->regs.dst_x = data & 0x3fff;
        s->regs.dst_width = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case SRC_X_Y:
        s->regs.src_y = data & 0x3fff;
        s->regs.src_x = (data >> 16) & 0x3fff;
        break;
    case DST_X_Y:
        s->regs.dst_y = data & 0x3fff;
        s->regs.dst_x = (data >> 16) & 0x3fff;
        break;
    case DST_WIDTH_HEIGHT:
        s->regs.dst_height = data & 0x3fff;
        s->regs.dst_width = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case DST_HEIGHT_Y:
        s->regs.dst_y = data & 0x3fff;
        s->regs.dst_height = (data >> 16) & 0x3fff;
        break;
    case SRC_OFFSET:
            s->regs.src_offset = data & 0xfffffff0;
        break;
    case SRC_PITCH:
            s->regs.src_pitch = data & 0x3fff;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.src_tile = (data >> 16) & 1;
        }
        break;
    case DP_BRUSH_BKGD_CLR:
        s->regs.dp_brush_bkgd_clr = data;
        break;
    case DP_BRUSH_FRGD_CLR:
        s->regs.dp_brush_frgd_clr = data;
        break;
    case BRUSH_DATA0 ... BRUSH_DATA63 + 3:
    {
        unsigned int i = (addr - BRUSH_DATA0) / sizeof(uint32_t);

        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.brush_data[i],
                               (addr - BRUSH_DATA0) % sizeof(uint32_t),
                               data, size);
        }
        break;
    }
    case DP_CNTL:
        s->regs.dp_cntl = data;
        break;
    case DP_SRC_FRGD_CLR:
        s->regs.dp_src_frgd_clr = data;
        break;
    case DP_SRC_BKGD_CLR:
        s->regs.dp_src_bkgd_clr = data;
        break;
    case DP_DATATYPE:
        s->regs.dp_datatype = data &
            (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF ?
             0xe0070f0f : 0xc0070f0f);
        break;
    case DP_MIX:
        s->regs.dp_mix = data & 0x00ff0700;
        break;
    case DP_WRITE_MASK:
        s->regs.dp_write_mask = data;
        break;
    case CLR_CMP_CNTL:
        s->regs.clr_cmp_cntl = data &
            (CLR_CMP_FN_SRC_MASK | CLR_CMP_FN_DST_MASK |
             CLR_CMP_ENABLE_MASK);
        break;
    case CLR_CMP_CLR_SRC:
        s->regs.clr_cmp_clr_src = data;
        break;
    case CLR_CMP_CLR_DST:
        s->regs.clr_cmp_clr_dst = data;
        break;
    case CLR_CMP_MASK:
        s->regs.clr_cmp_mask = data;
        break;
    case RBBM_GUICNTL ... RBBM_GUICNTL + 3:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.rbbm_guicntl,
                               addr - RBBM_GUICNTL, data, size);
            s->regs.rbbm_guicntl &= HOST_DATA_SWAP_MASK;
        }
        break;
    case DEFAULT_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.default_offset = data & 0xfffffff0;
        } else {
            /* Radeon has DEFAULT_PITCH_OFFSET here like DST_PITCH_OFFSET */
            s->regs.default_offset = (data & 0x3fffff) << 10;
            s->regs.default_pitch = (data & 0x3fc00000) >> 16;
            s->regs.default_tile = data >> 30;
        }
        break;
    case DEFAULT_PITCH:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.default_pitch = data & 0x3fff;
            s->regs.default_tile = (data >> 16) & 1;
        }
        break;
    case DEFAULT_SC_BOTTOM_RIGHT:
        s->regs.default_sc_right = data & 0x3fff;
        s->regs.default_sc_bottom = (data >> 16) & 0x3fff;
        break;
    case SC_TOP_LEFT:
        s->regs.sc_left = ati_scissor_value(s, data);
        s->regs.sc_top = ati_scissor_value(s, data >> 16);
        break;
    case SC_LEFT:
        s->regs.sc_left = ati_scissor_value(s, data);
        break;
    case SC_TOP:
        s->regs.sc_top = ati_scissor_value(s, data);
        break;
    case SC_BOTTOM_RIGHT:
        s->regs.sc_right = ati_scissor_value(s, data);
        s->regs.sc_bottom = ati_scissor_value(s, data >> 16);
        break;
    case SC_RIGHT:
        s->regs.sc_right = ati_scissor_value(s, data);
        break;
    case SC_BOTTOM:
        s->regs.sc_bottom = ati_scissor_value(s, data);
        break;
    case SRC_SC_BOTTOM_RIGHT:
        s->regs.src_sc_right = ati_scissor_value(s, data);
        s->regs.src_sc_bottom = ati_scissor_value(s, data >> 16);
        break;
    case SRC_SC_RIGHT:
        s->regs.src_sc_right = ati_scissor_value(s, data);
        break;
    case SRC_SC_BOTTOM:
        s->regs.src_sc_bottom = ati_scissor_value(s, data);
        break;
    case HOST_DATA0:
    case HOST_DATA1:
    case HOST_DATA2:
    case HOST_DATA3:
    case HOST_DATA4:
    case HOST_DATA5:
    case HOST_DATA6:
    case HOST_DATA7:
    case HOST_DATA_LAST:
        if (!s->host_data.active) {
            break;
        }
        ati_host_data_write(s, data, addr == HOST_DATA_LAST);
        break;
    default:
        break;
    }
}

static void ati_mm_write(void *opaque, hwaddr addr, uint64_t data,
                         unsigned int size)
{
    ati_mmio_write(opaque, addr, data, size);
}

static const MemoryRegionOps ati_mm_ops = {
    .read = ati_mm_read,
    .write = ati_mm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_ati_vga_regs = {
    .name = "ati-vga/regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mm_index, ATIVGARegs),
        VMSTATE_UINT32_ARRAY(bios_scratch, ATIVGARegs, 8),
        VMSTATE_UINT32(gen_int_cntl, ATIVGARegs),
        VMSTATE_UINT32(gen_int_status, ATIVGARegs),
        VMSTATE_UINT32(crtc_gen_cntl, ATIVGARegs),
        VMSTATE_UINT32(crtc_ext_cntl, ATIVGARegs),
        VMSTATE_UINT32(dac_cntl, ATIVGARegs),
        VMSTATE_UINT32(gpio_vga_ddc, ATIVGARegs),
        VMSTATE_UINT32(gpio_dvi_ddc, ATIVGARegs),
        VMSTATE_UINT32(gpio_monid, ATIVGARegs),
        VMSTATE_UINT32(config_cntl, ATIVGARegs),
        VMSTATE_UINT32_ARRAY(palette, ATIVGARegs, 256),
        VMSTATE_UINT32(crtc_h_total_disp, ATIVGARegs),
        VMSTATE_UINT32(crtc_h_sync_strt_wid, ATIVGARegs),
        VMSTATE_UINT32(crtc_v_total_disp, ATIVGARegs),
        VMSTATE_UINT32(crtc_v_sync_strt_wid, ATIVGARegs),
        VMSTATE_UINT32(crtc_offset, ATIVGARegs),
        VMSTATE_UINT32(crtc_offset_cntl, ATIVGARegs),
        VMSTATE_UINT32(crtc_pitch, ATIVGARegs),
        VMSTATE_UINT32(cur_offset, ATIVGARegs),
        VMSTATE_UINT32(cur_hv_pos, ATIVGARegs),
        VMSTATE_UINT32(cur_hv_offs, ATIVGARegs),
        VMSTATE_UINT32(cur_color0, ATIVGARegs),
        VMSTATE_UINT32(cur_color1, ATIVGARegs),
        VMSTATE_UINT32(dst_offset, ATIVGARegs),
        VMSTATE_UINT32(dst_pitch, ATIVGARegs),
        VMSTATE_UINT32(dst_tile, ATIVGARegs),
        VMSTATE_UINT32(dst_width, ATIVGARegs),
        VMSTATE_UINT32(dst_height, ATIVGARegs),
        VMSTATE_UINT32(src_offset, ATIVGARegs),
        VMSTATE_UINT32(src_pitch, ATIVGARegs),
        VMSTATE_UINT32(src_tile, ATIVGARegs),
        VMSTATE_UINT32(src_x, ATIVGARegs),
        VMSTATE_UINT32(src_y, ATIVGARegs),
        VMSTATE_UINT32(dst_x, ATIVGARegs),
        VMSTATE_UINT32(dst_y, ATIVGARegs),
        VMSTATE_UINT32(dp_gui_master_cntl, ATIVGARegs),
        VMSTATE_UINT32(dp_brush_bkgd_clr, ATIVGARegs),
        VMSTATE_UINT32(dp_brush_frgd_clr, ATIVGARegs),
        VMSTATE_UINT32(dp_src_frgd_clr, ATIVGARegs),
        VMSTATE_UINT32(dp_src_bkgd_clr, ATIVGARegs),
        VMSTATE_UINT16(sc_top, ATIVGARegs),
        VMSTATE_UINT16(sc_left, ATIVGARegs),
        VMSTATE_UINT16(sc_bottom, ATIVGARegs),
        VMSTATE_UINT16(sc_right, ATIVGARegs),
        VMSTATE_UINT16(src_sc_bottom, ATIVGARegs),
        VMSTATE_UINT16(src_sc_right, ATIVGARegs),
        VMSTATE_UINT32(dp_cntl, ATIVGARegs),
        VMSTATE_UINT32(dp_datatype, ATIVGARegs),
        VMSTATE_UINT32(dp_mix, ATIVGARegs),
        VMSTATE_UINT32(dp_write_mask, ATIVGARegs),
        VMSTATE_UINT32(default_offset, ATIVGARegs),
        VMSTATE_UINT32(default_pitch, ATIVGARegs),
        VMSTATE_UINT16(default_sc_bottom, ATIVGARegs),
        VMSTATE_UINT16(default_sc_right, ATIVGARegs),
        VMSTATE_UINT32(default_tile, ATIVGARegs),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ati_host_data = {
    .name = "ati-vga/host-data",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(active, ATIHostDataState),
        VMSTATE_UINT32(row, ATIHostDataState),
        VMSTATE_UINT32(col, ATIHostDataState),
        VMSTATE_UINT32(next, ATIHostDataState),
        VMSTATE_UINT32_ARRAY(acc, ATIHostDataState,
                             ATI_HOST_DATA_BANK_DWORDS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ati_cursor = {
    .name = "ati-vga/cursor",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(offset, ATICursorState),
        VMSTATE_UINT32(hv_pos, ATICursorState),
        VMSTATE_UINT32(hv_offs, ATICursorState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ati_bitbang_i2c = {
    .name = "ati-vga/bitbang-i2c",
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

static int ati_vga_pre_save(void *opaque)
{
    ATIVGAState *s = opaque;
    int64_t elapsed;

    if (!ati_crtc_enabled(s)) {
        s->crtc_frame_elapsed_ns = 0;
        return 0;
    }
    elapsed = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
              s->crtc_frame_start_ns;
    s->crtc_frame_elapsed_ns = elapsed > 0 ?
                               elapsed % ATI_CRTC_FRAME_NS : 0;
    return 0;
}

static int ati_vga_post_load(void *opaque, int version_id)
{
    ATIVGAState *s = opaque;

    if (s->host_data.next >= ATI_HOST_DATA_BANK_DWORDS ||
        (version_id >= 3 &&
         s->regs.brush_y_x & ~ati_brush_y_x_mask(s)) ||
        (version_id >= 3 && s->host_data.pending_count > 3) ||
        (version_id >= 3 &&
         s->regs.rbbm_guicntl & ~HOST_DATA_SWAP_MASK) ||
        (version_id >= 3 &&
         s->regs.clr_cmp_cntl &
         ~(CLR_CMP_FN_SRC_MASK | CLR_CMP_FN_DST_MASK |
           CLR_CMP_ENABLE_MASK)) ||
        (version_id >= 4 && (s->regs.cur_offset & BIT(31)) &&
         (s->cursor_active.offset & ~0x07fffff0U ||
          s->cursor_active.hv_pos & ~0x3fff0fffU ||
          s->cursor_active.hv_offs & ~0x003f003fU)) ||
        (version_id >= 4 &&
         (s->crtc_frame & ~ATI_CRTC_FRAME_MASK ||
          s->crtc_frame_elapsed_ns < 0 ||
          s->crtc_frame_elapsed_ns >= ATI_CRTC_FRAME_NS ||
          s->crtc_vline & ~ati_crtc_line_mask(s) ||
          (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF &&
           s->crtc_fix_vsync_timing))) ||
        (version_id >= 5 &&
         (s->regs.clock_cntl_index & ~PLL_INDEX_CNTL_MASK)) ||
        s->bbi2c.state < STOPPED || s->bbi2c.state > SENT_NACK ||
        s->bbi2c.last_data < 0 || s->bbi2c.last_data > 1 ||
        s->bbi2c.last_clock < 0 || s->bbi2c.last_clock > 1 ||
        s->bbi2c.device_out < 0 || s->bbi2c.device_out > 1 ||
        s->bbi2c.current_addr < -1 ||
        s->bbi2c.current_addr > UINT8_MAX) {
        return -EINVAL;
    }
    if (version_id < 7) {
        memset(s->r100_3d.scaler_palette, 0,
               sizeof(s->r100_3d.scaler_palette));
        s->r100_3d.scaler_palette_format = 0;
        s->r100_3d.scaler_palette_valid = false;
    }
    if (version_id >= 2 && ati_3d_post_load(s) < 0) {
        return -EINVAL;
    }
    if (version_id < 3) {
        memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
        s->host_data.pending_count = 0;
        s->regs.rbbm_guicntl = 0;
        s->regs.clr_cmp_cntl = 0;
        s->regs.clr_cmp_clr_src = 0;
        s->regs.clr_cmp_clr_dst = 0;
        s->regs.clr_cmp_mask = 0;
        s->regs.brush_y_x = 0;
        memset(s->regs.brush_data, 0, sizeof(s->regs.brush_data));
        memset(s->r100_3d.fog_table, 0,
               sizeof(s->r100_3d.fog_table));
        s->r100_3d.fog_table_index = 0;
        /* Older versions did not apply DP_WRITE_MASK to 2D blits. */
        s->regs.dp_write_mask = UINT32_MAX;
        s->crtc_offset_active = s->regs.crtc_offset & CRTC_OFFSET_MASK;
        s->crtc_pitch_active = s->regs.crtc_pitch;
    }
    if (version_id < 4 || !(s->regs.cur_offset & BIT(31))) {
        ati_cursor_commit(s);
    }
    if (version_id < 5) {
        s->regs.clock_cntl_index = 0;
        memset(s->regs.pll, 0, sizeof(s->regs.pll));
    }
    if (version_id < 6) {
        s->regs.dac_ext_cntl = 0;
        s->regs.dac_macro_cntl = 0;
    }
    if (version_id < 4) {
        s->crtc_frame_start_ns = 0;
        s->crtc_frame_elapsed_ns = 0;
        s->crtc_frame = 0;
        s->crtc_event_line = 0;
        s->crtc_vline = 0;
        s->crtc_vblank_save = false;
        s->crtc_fix_vsync_timing =
            s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF;
        timer_del(&s->vblank_timer);
        ati_crtc_start(s);
    } else if (!ati_crtc_enabled(s)) {
        s->crtc_frame_start_ns = 0;
        timer_del(&s->vblank_timer);
    } else {
        s->crtc_frame_start_ns =
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
            s->crtc_frame_elapsed_ns;
        timer_del(&s->vblank_timer);
        ati_crtc_reschedule(s);
    }
    s->mode = s->regs.crtc_gen_cntl & CRTC2_EXT_DISP_EN ?
              EXT_MODE : VGA_MODE;
    s->vga.graphic_mode = -1;
    s->cursor_width = 0;
    s->cursor_height = 0;
    s->cursor_mode = 0;
    s->cursor_x_offset = 0;
    s->cursor_offset = 0;
    s->cursor_image_valid = false;
    s->cursor_host_visible = false;
    s->cursor_host_x = 0;
    s->cursor_host_y = 0;
    if (s->cursor_guest_mode) {
        s->vga.force_shadow = false;
        ati_cursor_hide_host(s);
        ati_cursor_update_guest_mode(s);
    } else {
        s->vga.force_shadow = false;
        ati_cursor_update_host(s, true);
    }
    ati_vga_update_irq(s);
    graphic_hw_invalidate(s->vga.con);
    return 0;
}

static const VMStateDescription vmstate_ati_vga = {
    .name = "ati-vga",
    .version_id = 7,
    .minimum_version_id = 1,
    .pre_save = ati_vga_pre_save,
    .post_load = ati_vga_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, ATIVGAState),
        VMSTATE_STRUCT(vga, ATIVGAState, 0,
                       vmstate_vga_common, VGACommonState),
        VMSTATE_STRUCT(regs, ATIVGAState, 0,
                       vmstate_ati_vga_regs, ATIVGARegs),
        VMSTATE_STRUCT(cursor_active, ATIVGAState, 4,
                       vmstate_ati_cursor, ATICursorState),
        VMSTATE_STRUCT(host_data, ATIVGAState, 0,
                       vmstate_ati_host_data, ATIHostDataState),
        VMSTATE_UINT8_ARRAY_V(host_data.pending, ATIVGAState, 3, 3),
        VMSTATE_UINT8_V(host_data.pending_count, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.rbbm_guicntl, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.clr_cmp_cntl, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.clr_cmp_clr_src, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.clr_cmp_clr_dst, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.clr_cmp_mask, ATIVGAState, 3),
        VMSTATE_UINT32_V(regs.brush_y_x, ATIVGAState, 3),
        VMSTATE_UINT32_ARRAY_V(regs.brush_data, ATIVGAState, 64, 3),
        VMSTATE_STRUCT(r100_3d, ATIVGAState, 2,
                       vmstate_ati_3d, ATI3DState),
        VMSTATE_UINT8_ARRAY_V(r100_3d.fog_table, ATIVGAState,
                              ATI_3D_FOG_TABLE_ENTRIES, 3),
        VMSTATE_UINT8_V(r100_3d.fog_table_index, ATIVGAState, 3),
        VMSTATE_UINT32_V(crtc_offset_active, ATIVGAState, 3),
        VMSTATE_UINT32_V(crtc_pitch_active, ATIVGAState, 3),
        VMSTATE_INT64_V(crtc_frame_elapsed_ns, ATIVGAState, 4),
        VMSTATE_UINT32_V(crtc_frame, ATIVGAState, 4),
        VMSTATE_UINT16_V(crtc_vline, ATIVGAState, 4),
        VMSTATE_BOOL_V(crtc_vblank_save, ATIVGAState, 4),
        VMSTATE_BOOL_V(crtc_fix_vsync_timing, ATIVGAState, 4),
        VMSTATE_UINT32_V(regs.clock_cntl_index, ATIVGAState, 5),
        VMSTATE_UINT32_ARRAY_V(regs.pll, ATIVGAState,
                               ATI_PLL_REG_COUNT, 5),
        VMSTATE_UINT32_V(regs.dac_ext_cntl, ATIVGAState, 6),
        VMSTATE_UINT32_V(regs.dac_macro_cntl, ATIVGAState, 6),
        VMSTATE_UINT32_ARRAY_V(r100_3d.scaler_palette, ATIVGAState, 256, 7),
        VMSTATE_UINT8_V(r100_3d.scaler_palette_format, ATIVGAState, 7),
        VMSTATE_BOOL_V(r100_3d.scaler_palette_valid, ATIVGAState, 7),
        VMSTATE_STRUCT(bbi2c, ATIVGAState, 0,
                       vmstate_ati_bitbang_i2c, bitbang_i2c_interface),
        VMSTATE_TIMER(vblank_timer, ATIVGAState),
        VMSTATE_END_OF_LIST()
    },
};

static void ati_vga_realize(PCIDevice *dev, Error **errp)
{
    ATIVGAState *s = ATI_VGA(dev);
    VGACommonState *vga = &s->vga;
    I2CBus *i2cbus;

    /* PCI core fills in the class default after the device realizes. */
    s->default_rom = dev->romfile == NULL;

#ifndef CONFIG_PIXMAN
    if (s->use_pixman != 0) {
        warn_report("x-pixman != 0, not effective without PIXMAN");
    }
#endif

    if (s->model) {
        int i;
        for (i = 0; i < ARRAY_SIZE(ati_model_aliases); i++) {
            if (!strcmp(s->model, ati_model_aliases[i].name)) {
                s->dev_id = ati_model_aliases[i].dev_id;
                break;
            }
        }
        if (i >= ARRAY_SIZE(ati_model_aliases)) {
            warn_report("Unknown ATI VGA model name, "
                        "using default rage128p");
        }
    }
    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF &&
        !ati_is_rv100_family(s)) {
        error_setg(errp, "Unknown ATI VGA device id, "
                   "only 0x5046, 0x5159 and 0x515e are supported");
        return;
    }
    pci_set_word(dev->config + PCI_DEVICE_ID, s->dev_id);

    if (s->dev_id == PCI_DEVICE_ID_ATI_ES1000) {
        int pm_cap = pci_pm_init(dev, 0x50, errp);

        if (pm_cap < 0) {
            return;
        }
        /* ES1000 advertises PCI PM 1.1 (reported as version 2). */
        pci_set_word(dev->config + pm_cap + PCI_PM_PMC,
                     PCI_PM_CAP_VER_1_1 | PCI_PM_CAP_D1 | PCI_PM_CAP_D2);
        pci_set_word(dev->wmask + pm_cap + PCI_PM_CTRL,
                     PCI_PM_CTRL_STATE_MASK);
        if (dev->romsize == UINT32_MAX) {
            dev->romsize = 128 * KiB;
        }
    }

    if (s->dev_id == PCI_DEVICE_ID_ATI_ES1000) {
        pci_set_byte(dev->config + PCI_REVISION_ID, 0x02);
        pci_set_word(dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                     PCI_VENDOR_ID_HP);
        pci_set_word(dev->config + PCI_SUBSYSTEM_ID,
                     0x31fb);
        pci_set_byte(dev->config + PCI_CACHE_LINE_SIZE, 0x10);
        pci_set_byte(dev->config + PCI_LATENCY_TIMER, 0x40);
        pci_set_byte(dev->config + PCI_MIN_GNT, 0x08);
    }

    if (ati_is_rv100_family(s) && s->vga.vram_size_mb < 16) {
        warn_report("Too small video memory for device id");
        s->vga.vram_size_mb = 16;
    }

    /* init vga bits */
    if (!vga_common_init(vga, OBJECT(s), errp)) {
        return;
    }
    vga->vbe_legacy_mode_switch = true;
    vga_init(vga, OBJECT(s), pci_address_space(dev),
             pci_address_space_io(dev), true);
    vga->con = graphic_console_init(DEVICE(s), 0, &ati_graphic_ops, s);
    if (s->cursor_guest_mode) {
        vga->cursor_invalidate = ati_cursor_invalidate;
        vga->cursor_draw_line = ati_cursor_draw_line;
        ati_cursor_hide_host(s);
    }

    /* ddc, edid */
    i2cbus = i2c_init_bus(DEVICE(s), "ati-vga.ddc");
    bitbang_i2c_init(&s->bbi2c, i2cbus);
    i2c_slave_set_address(I2C_SLAVE(&s->i2cddc), 0x50);
    qdev_realize(DEVICE(&s->i2cddc), BUS(i2cbus), &error_abort);

    /* mmio register space */
    memory_region_init_io(&s->mm, OBJECT(s), &ati_mm_ops, s,
                          "ati.mmregs", ati_is_rv100_family(s) ?
                          ATI_R100_MMIO_SIZE : ATI_RAGE128_MMIO_SIZE);
    /* io space is alias to beginning of mmregs */
    memory_region_init_alias(&s->io, OBJECT(s), "ati.io", &s->mm, 0, 0x100);

    /*
     * The framebuffer is at the beginning of the linear aperture. For
     * Rage128 the upper half of the aperture is reserved for an AGP
     * window (which we do not emulate.)
     */
    if (!s->linear_aper_sz) {
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->linear_aper_sz = ATI_RAGE128_LINEAR_APER_SIZE;
        } else {
            s->linear_aper_sz = ATI_R100_LINEAR_APER_SIZE;
        }
    }
    if (s->linear_aper_sz > 256 * MiB) {
        error_setg(errp, "x-linear-aper-size is too large (maximum 256 MiB)");
        return;
    }
    if (s->linear_aper_sz < 16 * MiB) {
        error_setg(errp, "x-linear-aper-size is too small (minimum 16 MiB)");
        return;
    }
    if (!is_power_of_2(s->linear_aper_sz)) {
        error_setg(errp, "x-linear-aper-size must be a power of two");
        return;
    }
    memory_region_init(&s->linear_aper, OBJECT(dev), "ati-linear-aperture0",
                       s->linear_aper_sz);
    memory_region_add_subregion(&s->linear_aper, 0, &vga->vram);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->linear_aper);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mm);

    /* TODO: Implement the remaining ATI interrupt sources. */
    dev->config[PCI_INTERRUPT_PIN] = 1;
    timer_init_ns(&s->vblank_timer, QEMU_CLOCK_VIRTUAL, ati_crtc_event, s);
}

static void ati_vga_reset(DeviceState *dev)
{
    ATIVGAState *s = ATI_VGA(dev);

    timer_del(&s->vblank_timer);
    i2c_end_transfer(s->bbi2c.bus);
    bitbang_i2c_init(&s->bbi2c, s->bbi2c.bus);
    s->bbi2c.state = STOPPED;
    s->bbi2c.buffer = 0;
    s->bbi2c.current_addr = -1;

    /* Reset mutable MMIO state, then apply the modeled device defaults. */
    memset(&s->regs, 0, sizeof(s->regs));
    s->crtc_frame_start_ns = 0;
    s->crtc_frame_elapsed_ns = 0;
    s->crtc_frame = 0;
    s->crtc_event_line = 0;
    s->crtc_vline = 0;
    s->crtc_vblank_save = false;
    s->crtc_fix_vsync_timing =
        s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF;
    s->crtc_offset_active = 0;
    s->crtc_pitch_active = 0;
    s->regs.crtc_gen_cntl = ATI_CRTC_GEN_CNTL_RESET;
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        /* Rage128's documented reset fields differ from Radeon R100. */
        s->regs.gen_int_status = ATI_RAGE128_GEN_INT_STATUS_RESET;
        s->regs.crtc_ext_cntl = ATI_RAGE128_CRTC_EXT_CNTL_RESET;
        s->regs.dac_cntl = ATI_RAGE128_DAC_CNTL_RESET;
    } else if (ati_is_rv100_family(s)) {
        s->regs.gen_int_status = ATI_R100_GEN_INT_STATUS_RESET;
        s->regs.dac_cntl = ATI_R100_DAC_CNTL_RESET;
        s->regs.crtc_offset_cntl = ATI_RV100_CRTC_OFFSET_CNTL_RESET;
    }
    s->regs.gpio_vga_ddc = BIT(8) | BIT(9);
    s->regs.gpio_dvi_ddc = BIT(8) | BIT(9);
    s->regs.gpio_monid = BIT(9) | BIT(10);
    ati_cursor_commit(s);
    ati_vga_update_irq(s);

    /* reset vga */
    vga_common_reset(&s->vga);
    s->mode = VGA_MODE;
    s->vga.big_endian_fb = s->vga.default_endian_fb;
    s->vga.force_shadow = false;
    s->cursor_width = 0;
    s->cursor_height = 0;
    s->cursor_mode = 0;
    s->cursor_x_offset = 0;
    s->cursor_offset = 0;
    s->cursor_image_valid = false;
    s->cursor_host_visible = false;
    s->cursor_host_x = 0;
    s->cursor_host_y = 0;
    if (s->cursor_guest_mode) {
        ati_cursor_hide_host(s);
    } else {
        dpy_mouse_set(s->vga.con, 0, 0, false);
    }

    memset(&s->host_data, 0, sizeof(s->host_data));
    ati_3d_reset(s);
    graphic_hw_invalidate(s->vga.con);
}

static void ati_vga_exit(PCIDevice *dev)
{
    ATIVGAState *s = ATI_VGA(dev);

    timer_del(&s->vblank_timer);
    graphic_console_close(s->vga.con);
    cursor_unref(s->cursor);
    s->cursor = NULL;
}

static const Property ati_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", ATIVGAState, vga.vram_size_mb, 16),
    DEFINE_PROP_STRING("model", ATIVGAState, model),
    DEFINE_PROP_UINT16("x-device-id", ATIVGAState, dev_id,
                       PCI_DEVICE_ID_ATI_RAGE128_PF),
    /* Position registers specify the cursor image origin. */
    DEFINE_PROP_BOOL("guest_hwcursor", ATIVGAState, cursor_guest_mode, true),
    /* this is a debug option, prefer PROP_UINT over PROP_BIT for simplicity */
    DEFINE_PROP_UINT8("x-pixman", ATIVGAState, use_pixman, DEFAULT_X_PIXMAN),
    DEFINE_PROP_UINT64("x-linear-aper-size", ATIVGAState, linear_aper_sz, 0),
    DEFINE_EDID_PROPERTIES(ATIVGAState, i2cddc.edid_info),
};

static void ati_vga_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ati_vga_reset);
    device_class_set_props(dc, ati_vga_properties);
    dc->vmsd = &vmstate_ati_vga;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->vendor_id = PCI_VENDOR_ID_ATI;
    k->device_id = PCI_DEVICE_ID_ATI_RAGE128_PF;
    k->romfile = "vgabios-ati.bin";
    k->realize = ati_vga_realize;
    k->exit = ati_vga_exit;
}

static void ati_vga_init(Object *o)
{
    ATIVGAState *s = ATI_VGA(o);

    object_initialize_child(o, "edid", &s->i2cddc, TYPE_I2CDDC);
    object_property_set_description(o, "x-pixman", "Use pixman for: "
                                    "1: fill, 2: blit");
}

static const TypeInfo ati_vga_info = {
    .name = TYPE_ATI_VGA,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ATIVGAState),
    .class_init = ati_vga_class_init,
    .instance_init = ati_vga_init,
    .interfaces = (const InterfaceInfo[]) {
          { INTERFACE_CONVENTIONAL_PCI_DEVICE },
          { },
    },
};

static void ati_vga_register_types(void)
{
    type_register_static(&ati_vga_info);
}

type_init(ati_vga_register_types)
