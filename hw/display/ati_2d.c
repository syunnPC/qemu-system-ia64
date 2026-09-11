/*
 * QEMU ATI SVGA emulation
 * 2D engine functions
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "exec/target_page.h"
#include "qemu/log.h"
#include "ui/console.h"
#include "ui/rect.h"

static int ati_bpp_from_datatype(const ATIVGAState *s)
{
    switch (s->regs.dp_datatype & 0xf) {
    case 2:
        return 8;
    case 3:
    case 4:
        return 16;
    case 5:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            return 24;
        }
        break;
    case 6:
        return 32;
    default:
        break;
    }
    qemu_log_mask(LOG_UNIMP, "Unknown dst datatype %d\n",
                  s->regs.dp_datatype & 0xf);
    return 0;
}

static uint32_t ati_pixel_mask(unsigned int bpp)
{
    return bpp == 32 ? UINT32_MAX : bpp ? (1U << bpp) - 1 : 0;
}

static int ati_coord_14(uint32_t value)
{
    return sextract32(value, 0, 14);
}

static int ati_scissor_coord(const ATIVGAState *s, uint16_t value)
{
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        return ati_coord_14(value);
    }

    return value & BIT(15) ? -(value & 0x3fff) : value & 0x3fff;
}

static int ati_div_floor(int value, int divisor)
{
    int quotient = value / divisor;

    return quotient - (value % divisor < 0);
}

static int ati_div_ceil(int value, int divisor)
{
    int quotient = value / divisor;

    return quotient + (value % divisor > 0);
}

static uint16_t ati_scissor_extent(int first, int last, bool inclusive)
{
    int extent = last - first + inclusive;

    return extent > 0 ? extent : 0;
}

typedef struct {
    ATIVGAState *s;
    VGACommonState *vga;
    int bpp;
    uint32_t rop3;
    uint32_t rop_coeff[8];
    bool host_data_active;
    bool left_to_right;
    bool top_to_bottom;
    bool need_swap;
    bool solid_brush;
    bool mono_lsb_first;
    bool write_mask_active;
    bool color_compare_active;
    bool rage128;
    uint8_t brush_type;
    uint8_t brush_x;
    uint8_t brush_y;
    uint32_t frgd_clr;
    uint32_t bkgd_clr;
    const uint32_t *brush_data;
    uint32_t src_frgd_clr;
    uint32_t write_mask;
    uint32_t src_source;
    uint32_t clr_cmp_cntl;
    uint32_t clr_cmp_clr_src;
    uint32_t clr_cmp_clr_dst;
    uint32_t clr_cmp_mask;
    QemuRect scissor;
    bool source_clip_active;
    int source_clip_right;
    int source_clip_bottom;

    QemuRect dst;
    int dst_stride;
    uint8_t *dst_bits;
    uint32_t dst_offset;
    unsigned int dst_tile;
    uint64_t dst_vram_offset;

    QemuRect src;
    int src_stride;
    const uint8_t *src_bits;
    uint32_t src_offset;
    unsigned int src_tile;
    uint64_t src_vram_offset;
} ATI2DCtx;

static bool ati_2d_clip_rects(const ATI2DCtx *ctx, QemuRect *vis_src,
                              QemuRect *vis_dst)
{
    int right;
    int bottom;

    if (!qemu_rect_intersect(&ctx->dst, &ctx->scissor, vis_dst)) {
        return false;
    }

    /* Keep source and destination coordinates aligned after dst clipping. */
    vis_src->x = ctx->src.x + (vis_dst->x - ctx->dst.x);
    vis_src->y = ctx->src.y + (vis_dst->y - ctx->dst.y);
    vis_src->width = vis_dst->width;
    vis_src->height = vis_dst->height;

    if (!ctx->source_clip_active) {
        return true;
    }

    /* The source coordinate is top-left; SRC_SC_* is exclusive bottom-right. */
    right = MIN(vis_src->x + vis_src->width, ctx->source_clip_right);
    bottom = MIN(vis_src->y + vis_src->height, ctx->source_clip_bottom);
    if (right <= vis_src->x || bottom <= vis_src->y) {
        qemu_rect_init(vis_src, 0, 0, 0, 0);
        qemu_rect_init(vis_dst, 0, 0, 0, 0);
        return false;
    }
    vis_src->width = right - vis_src->x;
    vis_src->height = bottom - vis_src->y;
    vis_dst->width = vis_src->width;
    vis_dst->height = vis_src->height;
    return true;
}

static void ati_2d_mark_direct_dirty(const ATI2DCtx *ctx,
                                     const QemuRect *dirty)
{
    VGACommonState *vga = ctx->vga;
    unsigned int bypp = ctx->bpp / 8;
    uint64_t page_size = qemu_target_page_size();
    uint64_t page_mask = page_size - 1;
    uint64_t row_bytes;
    uint64_t run_start = 0;
    uint64_t run_end = 0;
    unsigned int y;

    g_assert(ctx->dst_bits);
    row_bytes = (uint64_t)dirty->width * bypp;
    if (!dirty->height || !row_bytes) {
        return;
    }

    /* Dirty logging is page-based, so coalesce only contiguous page runs. */
    for (y = 0; y < dirty->height; y++) {
        uint64_t offset = ctx->dst_vram_offset +
                          (uint64_t)(dirty->y + y) * ctx->dst_stride +
                          (uint64_t)dirty->x * bypp;
        uint64_t start = offset & ~page_mask;
        uint64_t end = MIN((offset + row_bytes + page_mask) & ~page_mask,
                           vga->vram_size);

        if (!y) {
            run_start = start;
            run_end = end;
        } else if (start <= run_end) {
            run_end = MAX(run_end, end);
        } else {
            memory_region_set_dirty(&vga->vram, run_start,
                                    run_end - run_start);
            run_start = start;
            run_end = end;
        }
    }
    memory_region_set_dirty(&vga->vram, run_start, run_end - run_start);
}

static void setup_2d_blt_ctx(ATIVGAState *s, ATI2DCtx *ctx)
{
    bool rage128 = s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF;
    int sc_left = ati_scissor_coord(s, s->regs.sc_left);
    int sc_right = ati_scissor_coord(s, s->regs.sc_right);
    int sc_top = ati_scissor_coord(s, s->regs.sc_top);
    int sc_bottom = ati_scissor_coord(s, s->regs.sc_bottom);
    int dst_x = ati_coord_14(s->regs.dst_x);
    int dst_y = ati_coord_14(s->regs.dst_y);
    int src_x = ati_coord_14(s->regs.src_x);
    int src_y = ati_coord_14(s->regs.src_y);
    uint64_t stride;

    ctx->s = s;
    ctx->vga = &s->vga;
    ctx->bpp = ati_bpp_from_datatype(s);
    ctx->rop3 = s->regs.dp_mix & GMC_ROP3_MASK;
    /* Algebraic normal form, with variable bits D, S, P in that order. */
    for (unsigned int i = 0; i < 8; i++) {
        ctx->rop_coeff[i] = -((ctx->rop3 >> (16 + i)) & 1U);
    }
    for (unsigned int bit = 1; bit < 8; bit <<= 1) {
        for (unsigned int i = 0; i < 8; i++) {
            if (i & bit) {
                ctx->rop_coeff[i] ^= ctx->rop_coeff[i ^ bit];
            }
        }
    }
    ctx->host_data_active = s->host_data.active;
    /*
     * X.Org xf86-video-ati 6.14.6 leaves XDIR unchanged in radeon_accelfuncs.c
     * scanline uploads; model Radeon host-data rows as advancing in positive X.
     */
    ctx->left_to_right = (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT) ||
                        (ctx->host_data_active && !rage128);
    ctx->top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM;
    ctx->need_swap = (HOST_BIG_ENDIAN != s->vga.big_endian_fb);
    ctx->brush_type = (s->regs.dp_datatype & DP_BRUSH_DATATYPE) >> 8;
    ctx->solid_brush =
        ctx->brush_type == (BRUSH_SOLIDCOLOR >> 8) ||
        ctx->brush_type == (BRUSH_SOLIDCOLOR_LINE >> 8);
    ctx->mono_lsb_first = s->regs.dp_datatype & DP_BYTE_PIX_ORDER;
    ctx->write_mask_active =
        (s->regs.dp_write_mask & ati_pixel_mask(ctx->bpp)) !=
        ati_pixel_mask(ctx->bpp);
    ctx->frgd_clr = s->regs.dp_brush_frgd_clr;
    ctx->bkgd_clr = s->regs.dp_brush_bkgd_clr;
    ctx->brush_x = s->regs.brush_y_x & (rage128 ? 31 : 7);
    ctx->brush_y = (s->regs.brush_y_x >> 8) & (rage128 ? 31 : 7);
    ctx->brush_data = s->regs.brush_data;
    ctx->src_frgd_clr = s->regs.dp_src_frgd_clr;
    ctx->write_mask = s->regs.dp_write_mask;
    ctx->src_source = s->regs.dp_mix & DP_SRC_SOURCE;
    ctx->source_clip_active = ctx->src_source == DP_SRC_RECT;
    ctx->source_clip_right =
        ati_scissor_coord(s, s->regs.src_sc_right) + rage128;
    ctx->source_clip_bottom =
        ati_scissor_coord(s, s->regs.src_sc_bottom) + rage128;
    ctx->clr_cmp_cntl = s->regs.clr_cmp_cntl;
    ctx->clr_cmp_clr_src = s->regs.clr_cmp_clr_src;
    ctx->clr_cmp_clr_dst = s->regs.clr_cmp_clr_dst;
    ctx->clr_cmp_mask = s->regs.clr_cmp_mask;
    ctx->rage128 = rage128;
    switch ((ctx->clr_cmp_cntl & CLR_CMP_ENABLE_MASK) >>
            CLR_CMP_ENABLE_SHIFT) {
    case CLR_CMP_ENABLE_DST:
        ctx->color_compare_active =
            ctx->clr_cmp_cntl & CLR_CMP_FN_DST_MASK;
        break;
    case CLR_CMP_ENABLE_SRC:
        ctx->color_compare_active =
            ctx->clr_cmp_cntl & CLR_CMP_FN_SRC_MASK;
        break;
    case CLR_CMP_ENABLE_BOTH:
        ctx->color_compare_active =
            ctx->clr_cmp_cntl &
            (CLR_CMP_FN_SRC_MASK | CLR_CMP_FN_DST_MASK);
        break;
    default:
        ctx->color_compare_active = true;
        break;
    }
    ctx->dst_offset = s->regs.dst_offset;
    ctx->dst_tile = s->regs.dst_tile;
    ctx->src_tile = s->regs.src_tile;

    if (rage128 && ctx->bpp == 24) {
        /* Rage128 packed-24 scissor X is expressed in byte coordinates. */
        sc_left = ati_div_ceil(sc_left, 3);
        sc_right = ati_div_floor(sc_right, 3);
    }
    /* Rage128 right/bottom edges are inclusive; Radeon edges are exclusive. */
    qemu_rect_init(&ctx->scissor, sc_left, sc_top,
                   ati_scissor_extent(sc_left, sc_right, rage128),
                   ati_scissor_extent(sc_top, sc_bottom, rage128));

    ctx->dst.width = s->regs.dst_width;
    ctx->dst.height = s->regs.dst_height;
    ctx->dst.x = (ctx->left_to_right ?
                 dst_x : dst_x + 1 - ctx->dst.width);
    ctx->dst.y = (ctx->top_to_bottom ?
                 dst_y : dst_y + 1 - ctx->dst.height);
    stride = s->regs.dst_pitch;
    if (rage128) {
        stride *= ctx->bpp == 24 ? 8 : ctx->bpp;
    }
    ctx->dst_stride = stride <= INT_MAX ? stride : 0;
    ctx->dst_bits = rage128 && s->regs.dst_offset <= s->vga.vram_size ?
                    s->vga.vram_ptr + s->regs.dst_offset : NULL;
    ctx->dst_vram_offset = rage128 ? s->regs.dst_offset : 0;

    ctx->src.x = (ctx->left_to_right ?
                 src_x : src_x + 1 - ctx->dst.width);
    ctx->src.y = (ctx->top_to_bottom ?
                 src_y : src_y + 1 - ctx->dst.height);
    ctx->src_offset = s->regs.src_offset;
    stride = s->regs.src_pitch;
    if (rage128) {
        stride *= ctx->bpp == 24 ? 8 : ctx->bpp;
    }
    ctx->src_stride = stride <= INT_MAX ? stride : 0;
    ctx->src_bits = rage128 && s->regs.src_offset <= s->vga.vram_size ?
                    s->vga.vram_ptr + s->regs.src_offset : NULL;
    ctx->src_vram_offset = rage128 ? s->regs.src_offset : 0;
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            ctx->src_stride, ctx->dst_stride, s->regs.default_pitch,
            ctx->src.x, ctx->src.y, ctx->dst.x, ctx->dst.y,
            ctx->dst.width, ctx->dst.height,
            (ctx->left_to_right ? '>' : '<'),
            (ctx->top_to_bottom ? 'v' : '^'));
}

static bool ati_2d_rect_layout(const ATI2DCtx *ctx, int stride,
                               const QemuRect *rect, const char *name,
                               uint64_t *end)
{
    uint64_t bypp = ctx->bpp / 8;
    uint64_t right;
    uint64_t bottom;
    uint64_t row;

    if (!bypp || stride <= 0 || rect->x < 0 || rect->y < 0 ||
        rect->width <= 0 || rect->height <= 0 ||
        rect->x > 0x3fff || rect->y > 0x3fff) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid ATI 2D %s rectangle\n",
                      name);
        return false;
    }

    right = (uint64_t)rect->x + rect->width;
    bottom = (uint64_t)rect->y + rect->height;
    if (right > UINT64_MAX / bypp || right * bypp > (uint64_t)stride ||
        bottom - 1 > UINT64_MAX / (uint64_t)stride) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ATI 2D %s rectangle exceeds its pitch\n", name);
        return false;
    }

    row = (bottom - 1) * stride;
    *end = row + right * bypp;
    return true;
}

static bool ati_2d_prepare_surface(ATIVGAState *s, const ATI2DCtx *ctx,
                                   uint32_t address, int stride,
                                   unsigned int tile, const QemuRect *rect,
                                   const char *name,
                                   bool is_write, uint8_t **bits,
                                   uint64_t *vram_offset)
{
    uint64_t bypp = ctx->bpp / 8;
    uint64_t end;
    unsigned int y;

    *bits = NULL;
    *vram_offset = 0;
    if (!ati_2d_rect_layout(ctx, stride, rect, name, &end)) {
        return false;
    }
    if (tile) {
        if (ctx->rage128) {
            qemu_log_mask(LOG_UNIMP,
                          "ATI Rage128 tiled %s is not implemented\n", name);
            return false;
        }
        if ((uint64_t)rect->width * rect->height > ATI_2D_MAX_PIXELS) {
            return false;
        }
        for (unsigned int row = 0; row < rect->height; row++) {
            for (unsigned int col = 0; col < rect->width * bypp; ) {
                uint64_t offset, physical;
                uint32_t xbyte = rect->x * bypp + col;
                unsigned int count = MIN(rect->width * bypp - col,
                                         16 - (xbyte & 15));

                if (!ati_2d_tile_offset(s, address, stride, bypp, tile, xbyte,
                                        rect->y + row, &offset) ||
                    !ati_r100_gpu_vram_offset(s, address + offset, count,
                                              &physical)) {
                    return false;
                }
                col += count;
            }
        }
        return true;
    }
    if (!ctx->rage128 && end > UINT64_C(1) + UINT32_MAX - address) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ATI 2D %s rectangle exceeds GPU address space\n",
                      name);
        return false;
    }
    if (ctx->rage128) {
        if (address > ctx->vga->vram_size ||
            end > ctx->vga->vram_size - address) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ATI 2D %s rectangle is outside VRAM\n", name);
            return false;
        }
        *vram_offset = address;
        *bits = ctx->vga->vram_ptr + address;
        return true;
    }
    if (ati_r100_gpu_vram_offset(s, address, end, vram_offset)) {
        *bits = ctx->vga->vram_ptr + *vram_offset;
        return true;
    }

    for (y = 0; y < rect->height; y++) {
        uint64_t row = address + (uint64_t)(rect->y + y) * stride +
                       (uint64_t)rect->x * bypp;
        uint64_t row_bytes = (uint64_t)rect->width * bypp;

        if (!ati_r100_gpu_access_valid(s, row, row_bytes, is_write)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ATI 2D %s rectangle is outside GPU memory\n",
                          name);
            return false;
        }
    }
    return true;
}

static bool ati_2d_rects_overlap_in_vram(const ATI2DCtx *ctx,
                                         const QemuRect *src,
                                         const QemuRect *dst)
{
    uint64_t bypp = ctx->bpp / 8;
    uint64_t src_start = ctx->src_vram_offset +
                         (uint64_t)src->y * ctx->src_stride +
                         (uint64_t)src->x * bypp;
    uint64_t src_end = ctx->src_vram_offset +
                       (uint64_t)(src->y + src->height - 1) *
                       ctx->src_stride +
                       (uint64_t)(src->x + src->width) * bypp;
    uint64_t dst_start = ctx->dst_vram_offset +
                         (uint64_t)dst->y * ctx->dst_stride +
                         (uint64_t)dst->x * bypp;
    uint64_t dst_end = ctx->dst_vram_offset +
                       (uint64_t)(dst->y + dst->height - 1) *
                       ctx->dst_stride +
                       (uint64_t)(dst->x + dst->width) * bypp;

    return src_start < dst_end && dst_start < src_end;
}

static uint32_t make_filler(int bpp, uint32_t color)
{
    if (bpp < 24) {
        color |= color << 16;
        if (bpp < 15) {
            color |= color << 8;
        }
    }
    return color;
}

static void ati_store_pixel(const ATI2DCtx *ctx, uint8_t *dst,
                            uint32_t value)
{
    unsigned int bypp = ctx->bpp / 8;

    if (bypp != 3 && ctx->need_swap) {
        bswap32s(&value);
    }

    switch (bypp) {
    case 1:
        stb_p(dst, value);
        break;
    case 2:
        stw_he_p(dst, value);
        break;
    case 3:
        if (ctx->vga->big_endian_fb) {
            st24_be_p(dst, value);
        } else {
            st24_le_p(dst, value);
        }
        break;
    case 4:
        stl_he_p(dst, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint32_t ati_load_pixel(const ATI2DCtx *ctx, const uint8_t *src)
{
    switch (ctx->bpp / 8) {
    case 1:
        return *src;
    case 2:
        return ctx->vga->big_endian_fb ? lduw_be_p(src) : lduw_le_p(src);
    case 3:
        return ctx->vga->big_endian_fb ?
               (uint32_t)src[0] << 16 | (uint32_t)src[1] << 8 | src[2] :
               (uint32_t)src[2] << 16 | (uint32_t)src[1] << 8 | src[0];
    case 4:
        return ctx->vga->big_endian_fb ? ldl_be_p(src) : ldl_le_p(src);
    default:
        g_assert_not_reached();
    }
}

static void ati_2d_fill_rows(const ATI2DCtx *ctx, const QemuRect *rect,
                              uint32_t filler)
{
    unsigned int bypp = ctx->bpp / 8;
    size_t row_bytes = (size_t)rect->width * bypp;
    uint8_t pattern[192]; /* A multiple of every supported pixel size. */
    uint8_t *first = ctx->dst_bits + rect->y * ctx->dst_stride + rect->x * bypp;
    bool same_byte = true;

    ati_store_pixel(ctx, pattern, filler);
    for (unsigned int i = 1; i < bypp; i++) {
        same_byte &= pattern[i] == pattern[0];
    }
    if (!same_byte) {
        for (size_t filled = bypp; filled < sizeof(pattern);) {
            size_t chunk = MIN(filled, sizeof(pattern) - filled);

            memcpy(pattern + filled, pattern, chunk);
            filled += chunk;
        }
    }
    for (unsigned int y = 0; y < rect->height; y++) {
        uint8_t *row = first + (size_t)y * ctx->dst_stride;

        if (same_byte) {
            memset(row, pattern[0], row_bytes);
        } else {
            /* Never use guest-writable VRAM as the constant fill pattern. */
            for (size_t x = 0; x < row_bytes; x += sizeof(pattern)) {
                memcpy(row + x, pattern, MIN(sizeof(pattern), row_bytes - x));
            }
        }
    }
}

static bool ati_2d_brush_supported(const ATI2DCtx *ctx)
{
    return ctx->brush_type <= 14;
}

static unsigned int ati_2d_brush_width(const ATI2DCtx *ctx)
{
    unsigned int type = ctx->brush_type;

    return type == 4 || type == 5 || type == 12 ? 1 :
           type >= 6 && type <= 9 ? 32 : 8;
}

static unsigned int ati_2d_brush_height(const ATI2DCtx *ctx)
{
    unsigned int type = ctx->brush_type;

    return type == 2 || type == 3 || type == 6 || type == 7 || type == 11 ? 1 :
           type == 8 || type == 9 ? 32 : 8;
}

/*
 * The register brush is destination aligned.  BRUSH_Y_X names the origin
 * to which pattern element (0, 0) is aligned; each axis wraps to the
 * selected brush dimension.  Monochrome rows occupy consecutive bits in
 * BRUSH_DATA, with DP_BYTE_PIX_ORDER selecting bit order.
 */
static bool ati_2d_brush_pixel(const ATI2DCtx *ctx, unsigned int x,
                               unsigned int y, uint32_t *pattern)
{
    unsigned int type = ctx->brush_type;
    unsigned int width = ati_2d_brush_width(ctx);
    unsigned int height = ati_2d_brush_height(ctx);
    unsigned int px = (x - ctx->brush_x) & (width - 1);
    unsigned int py = (y - ctx->brush_y) & (height - 1);
    unsigned int pixel = py * width + px;
    uint32_t mask = ati_pixel_mask(ctx->bpp);

    if (type >= 13) {
        *pattern = ctx->frgd_clr & mask;
    } else if (type < 10) {
        unsigned int bit = py * width +
                           (ctx->mono_lsb_first ? px : width - 1 - px);
        bool foreground = ctx->brush_data[bit / 32] & BIT(bit % 32);

        if (!foreground && (type & 1)) {
            return false;
        }
        *pattern = (foreground ? ctx->frgd_clr : ctx->bkgd_clr) & mask;
    } else if (ctx->bpp == 8) {
        *pattern = extract32(ctx->brush_data[pixel / 4], pixel % 4 * 8, 8);
    } else if (ctx->bpp == 16) {
        *pattern = extract32(ctx->brush_data[pixel / 2], pixel % 2 * 16, 16);
    } else {
        *pattern = ctx->brush_data[pixel] & mask;
    }
    return true;
}

static uint32_t ati_apply_rop3(const ATI2DCtx *ctx, uint32_t pattern,
                               uint32_t source, uint32_t destination)
{
    const uint32_t *c = ctx->rop_coeff;

    switch (ctx->rop3 >> 16) {
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
    default:
        break;
    }

    return c[0] ^ (destination & c[1]) ^
           (source & (c[2] ^ (destination & c[3]))) ^
           (pattern & (c[4] ^ (destination & c[5]) ^
                       (source & (c[6] ^ (destination & c[7])))));
}

static bool ati_color_compare_source(const ATI2DCtx *ctx, uint32_t *source)
{
    uint32_t mask = ctx->clr_cmp_mask & ati_pixel_mask(ctx->bpp);
    unsigned int function = ctx->clr_cmp_cntl & CLR_CMP_FN_SRC_MASK;
    bool equal = ((*source ^ ctx->clr_cmp_clr_src) & mask) == 0;

    /* Radeon reverses the source comparator output relative to Rage128. */
    switch (function) {
    case CLR_CMP_FALSE:
        return true;
    case CLR_CMP_TRUE:
        return false;
    case CLR_CMP_EQUAL:
        return ctx->rage128 ? equal : !equal;
    case CLR_CMP_NOT_EQUAL:
        return ctx->rage128 ? !equal : equal;
    case CLR_CMP_EQUAL_FLIP:
        if (equal) {
            *source ^= ctx->src_frgd_clr;
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool ati_color_compare_destination(const ATI2DCtx *ctx,
                                          uint32_t destination)
{
    uint32_t mask = ctx->clr_cmp_mask & ati_pixel_mask(ctx->bpp);
    unsigned int function = (ctx->clr_cmp_cntl & CLR_CMP_FN_DST_MASK) >>
                            CLR_CMP_FN_DST_SHIFT;
    bool equal = ((destination ^ ctx->clr_cmp_clr_dst) & mask) == 0;

    switch (function) {
    case CLR_CMP_FALSE:
        return true;
    case CLR_CMP_TRUE:
        return false;
    case CLR_CMP_EQUAL:
        return !equal;
    case CLR_CMP_NOT_EQUAL:
        return equal;
    default:
        return false;
    }
}

static bool ati_color_compare(const ATI2DCtx *ctx, uint32_t *source,
                              uint32_t destination)
{
    unsigned int enable = (ctx->clr_cmp_cntl & CLR_CMP_ENABLE_MASK) >>
                          CLR_CMP_ENABLE_SHIFT;

    switch (enable) {
    case CLR_CMP_ENABLE_DST:
        return ati_color_compare_destination(ctx, destination);
    case CLR_CMP_ENABLE_SRC:
        return ati_color_compare_source(ctx, source);
    case CLR_CMP_ENABLE_BOTH:
        return ati_color_compare_source(ctx, source) &&
               ati_color_compare_destination(ctx, destination);
    default:
        return false;
    }
}

static bool ati_color_compare_needs_source(const ATI2DCtx *ctx)
{
    unsigned int enable = (ctx->clr_cmp_cntl & CLR_CMP_ENABLE_MASK) >>
                          CLR_CMP_ENABLE_SHIFT;

    return (enable == CLR_CMP_ENABLE_SRC ||
            enable == CLR_CMP_ENABLE_BOTH) &&
           (ctx->clr_cmp_cntl & CLR_CMP_FN_SRC_MASK);
}

static bool ati_color_compare_needs_destination(const ATI2DCtx *ctx)
{
    unsigned int enable = (ctx->clr_cmp_cntl & CLR_CMP_ENABLE_MASK) >>
                          CLR_CMP_ENABLE_SHIFT;

    return ctx->color_compare_active &&
           (enable == CLR_CMP_ENABLE_DST || enable == CLR_CMP_ENABLE_BOTH);
}

static bool ati_2d_uses_generic_rop(const ATI2DCtx *ctx)
{
    return ctx->write_mask_active || ctx->color_compare_active ||
           (ctx->rop3 == ROP3_PATCOPY && !ctx->solid_brush) ||
           (ctx->rop3 != ROP3_SRCCOPY && ctx->rop3 != ROP3_PATCOPY &&
            ctx->rop3 != ROP3_BLACKNESS && ctx->rop3 != ROP3_WHITENESS);
}

static bool ati_2d_source_needed(const ATI2DCtx *ctx)
{
    uint8_t rop = ctx->rop3 >> 16;

    if (!ati_2d_uses_generic_rop(ctx)) {
        return ctx->rop3 == ROP3_SRCCOPY;
    }
    return ((rop ^ (rop >> 2)) & 0x33) != 0 ||
           ati_color_compare_needs_source(ctx);
}

static bool ati_2d_destination_needed(const ATI2DCtx *ctx)
{
    uint8_t rop = ctx->rop3 >> 16;

    return ((rop ^ (rop >> 1)) & 0x55) != 0 ||
           ctx->write_mask_active ||
           ati_color_compare_needs_destination(ctx);
}

static bool ati_2d_preserve_destination(const ATI2DCtx *ctx)
{
    uint8_t rop = ctx->rop3 >> 16;
    bool pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;

    return ati_2d_destination_needed(ctx) || ctx->color_compare_active ||
           (pattern_needed &&
            ctx->brush_type < 10 && (ctx->brush_type & 1));
}

/* Shared by direct pixels and staged overlap; skipped pixels stay intact. */
static inline bool ati_2d_composite_pixel(const ATI2DCtx *ctx,
                                         uint32_t pattern, uint32_t source,
                                         uint32_t destination, bool simple,
                                         uint32_t *result)
{
    uint32_t pixel_mask = ati_pixel_mask(ctx->bpp);

    if (!simple && ctx->color_compare_active &&
        !ati_color_compare(ctx, &source, destination)) {
        return false;
    }
    *result = ati_apply_rop3(ctx, pattern, source, destination) & pixel_mask;
    if (!simple && ctx->write_mask_active) {
        uint32_t mask = ctx->write_mask & pixel_mask;

        *result = (*result & mask) | (destination & ~mask);
    }
    return true;
}

typedef struct ATI2DBrush {
    uint32_t color[32][32];
    uint32_t opaque[32];
    unsigned int x_mask;
    unsigned int y_mask;
} ATI2DBrush;

static inline uint32_t ati_2d_load_le(const uint8_t *ptr, unsigned int bypp)
{
    switch (bypp) {
    case 1:
        return *ptr;
    case 2:
        return lduw_le_p(ptr);
    case 4:
        return ldl_le_p(ptr);
    default:
        g_assert_not_reached();
    }
}

static inline void ati_2d_store_le(uint8_t *ptr, uint32_t value,
                                   unsigned int bypp)
{
    switch (bypp) {
    case 1:
        *ptr = value;
        break;
    case 2:
        stw_le_p(ptr, value);
        break;
    case 4:
        stl_le_p(ptr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Four instantiations: ordinary LE 8/16/32bpp, plus the general path. */
static inline QEMU_ALWAYS_INLINE void
ati_2d_generic_rop_pixels(const ATI2DCtx *ctx, const QemuRect *vis_src,
                         const QemuRect *vis_dst, const ATI2DBrush *brush,
                         bool source_needed, bool pattern_needed,
                         bool destination_needed, unsigned int bypp,
                         bool simple)
{
    for (unsigned int yi = 0; yi < vis_dst->height; yi++) {
        unsigned int y = ctx->top_to_bottom ? yi : vis_dst->height - 1 - yi;

        for (unsigned int xi = 0; xi < vis_dst->width; xi++) {
            unsigned int x = ctx->left_to_right ? xi : vis_dst->width - 1 - xi;
            uint8_t *dst = ctx->dst_bits +
                           (vis_dst->y + y) * ctx->dst_stride +
                           (vis_dst->x + x) * bypp;
            uint32_t destination = 0;
            uint32_t pattern = 0;
            uint32_t source = 0;
            uint32_t result;

            if (pattern_needed) {
                if (ctx->solid_brush) {
                    pattern = ctx->frgd_clr & ati_pixel_mask(ctx->bpp);
                } else {
                    unsigned int px = (vis_dst->x + x) & brush->x_mask;
                    unsigned int py = (vis_dst->y + y) & brush->y_mask;

                    if (!(brush->opaque[py] & BIT(px))) {
                        continue;
                    }
                    pattern = brush->color[py][px];
                }
            }
            if (destination_needed) {
                destination = simple ? ati_2d_load_le(dst, bypp) :
                                       ati_load_pixel(ctx, dst);
            }
            if (source_needed) {
                const uint8_t *src = ctx->src_bits +
                    (vis_src->y + y) * ctx->src_stride +
                    (vis_src->x + x) * bypp;

                source = simple ? ati_2d_load_le(src, bypp) :
                                  ati_load_pixel(ctx, src);
            }
            if (!ati_2d_composite_pixel(ctx, pattern, source, destination,
                                        simple, &result)) {
                continue;
            }
            if (simple) {
                ati_2d_store_le(dst, result, bypp);
            } else {
                ati_store_pixel(ctx, dst, make_filler(ctx->bpp, result));
            }
        }
    }
}

static bool ati_2d_generic_rop(const ATI2DCtx *ctx, const QemuRect *vis_src,
                               const QemuRect *vis_dst)
{
    uint8_t rop = ctx->rop3 >> 16;
    bool source_needed = ati_2d_source_needed(ctx);
    bool pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;
    bool destination_needed = ati_2d_destination_needed(ctx);
    ATI2DBrush brush;

    if (pattern_needed && !ati_2d_brush_supported(ctx)) {
        qemu_log_mask(LOG_UNIMP, "Unsupported ATI 2D brush type %u\n",
                      ctx->brush_type);
        return false;
    }
    /* No DMA callbacks occur in this loop: the register brush is stable. */
    if (pattern_needed && !ctx->solid_brush) {
        brush.x_mask = ati_2d_brush_width(ctx) - 1;
        brush.y_mask = ati_2d_brush_height(ctx) - 1;
        for (unsigned int y = 0; y <= brush.y_mask; y++) {
            brush.opaque[y] = 0;
            for (unsigned int x = 0; x <= brush.x_mask; x++) {
                if (ati_2d_brush_pixel(ctx, x, y, &brush.color[y][x])) {
                    brush.opaque[y] |= BIT(x);
                }
            }
        }
    }
    if (!ctx->write_mask_active && !ctx->color_compare_active &&
        !ctx->vga->big_endian_fb) {
        switch (ctx->bpp) {
        case 8:
            ati_2d_generic_rop_pixels(ctx, vis_src, vis_dst, &brush,
                source_needed, pattern_needed, destination_needed, 1, true);
            return true;
        case 16:
            ati_2d_generic_rop_pixels(ctx, vis_src, vis_dst, &brush,
                source_needed, pattern_needed, destination_needed, 2, true);
            return true;
        case 32:
            ati_2d_generic_rop_pixels(ctx, vis_src, vis_dst, &brush,
                source_needed, pattern_needed, destination_needed, 4, true);
            return true;
        }
    }
    ati_2d_generic_rop_pixels(ctx, vis_src, vis_dst, &brush,
        source_needed, pattern_needed, destination_needed, ctx->bpp / 8, false);
    return true;
}

static bool ati_2d_do_blt_direct(const ATI2DCtx *ctx, QemuRect vis_src,
                                 QemuRect vis_dst, uint8_t use_pixman)
{
    unsigned int x, y, i, j, bypp = ctx->bpp / 8;

    if (ati_2d_uses_generic_rop(ctx)) {
        return ati_2d_generic_rop(ctx, &vis_src, &vis_dst);
    }

    switch (ctx->rop3) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        bool overlap;
        size_t row_bytes = (size_t)vis_dst.width * bypp;

        overlap = !ctx->host_data_active &&
                  ati_2d_rects_overlap_in_vram(ctx, &vis_src, &vis_dst);

        DPRINTF("pixman_blt(%p, %p, %ld, %ld, %d, %d, "
                "%d, %d, %d, %d, %d, %d)\n",
                ctx->src_bits, ctx->dst_bits,
                ctx->src_stride / sizeof(uint32_t),
                ctx->dst_stride / sizeof(uint32_t),
                ctx->bpp, ctx->bpp, vis_src.x, vis_src.y, vis_dst.x, vis_dst.y,
                vis_dst.width, vis_dst.height);
#ifdef CONFIG_PIXMAN
        int src_stride_words = ctx->src_stride / sizeof(uint32_t);
        int dst_stride_words = ctx->dst_stride / sizeof(uint32_t);
        if ((use_pixman & BIT(1)) &&
            ctx->left_to_right && ctx->top_to_bottom && !overlap) {
            fallback = !pixman_blt((uint32_t *)ctx->src_bits,
                                   (uint32_t *)ctx->dst_bits, src_stride_words,
                                   dst_stride_words, ctx->bpp, ctx->bpp,
                                   vis_src.x, vis_src.y, vis_dst.x, vis_dst.y,
                                   vis_dst.width, vis_dst.height);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            for (y = 0; y < vis_dst.height; y++) {
                i = vis_dst.x * bypp;
                j = vis_src.x * bypp;
                if (ctx->top_to_bottom) {
                    i += (vis_dst.y + y) * ctx->dst_stride;
                    j += (vis_src.y + y) * ctx->src_stride;
                } else {
                    i += (vis_dst.y + vis_dst.height - 1 - y)
                         * ctx->dst_stride;
                    j += (vis_src.y + vis_dst.height - 1 - y)
                         * ctx->src_stride;
                }
                if (overlap) {
                    uint64_t src = ctx->src_vram_offset + j;
                    uint64_t dst = ctx->dst_vram_offset + i;
                    bool row_overlap = src < dst + row_bytes &&
                                       dst < src + row_bytes;
                    bool bulk_safe = (ctx->left_to_right && dst <= src) ||
                                     (!ctx->left_to_right && dst >= src);

                    if (!row_overlap || bulk_safe) {
                        memmove(&ctx->dst_bits[i], &ctx->src_bits[j],
                                row_bytes);
                    } else if (ctx->left_to_right) {
                        for (x = 0; x < vis_dst.width; x++) {
                            memmove(&ctx->dst_bits[i + x * bypp],
                                    &ctx->src_bits[j + x * bypp], bypp);
                        }
                    } else {
                        for (x = vis_dst.width; x-- > 0;) {
                            memmove(&ctx->dst_bits[i + x * bypp],
                                    &ctx->src_bits[j + x * bypp], bypp);
                        }
                    }
                } else {
                    memmove(&ctx->dst_bits[i], &ctx->src_bits[j],
                            row_bytes);
                }
            }
        }
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;
        switch (ctx->rop3) {
        case ROP3_PATCOPY:
            filler = make_filler(ctx->bpp, ctx->frgd_clr);
            break;
        case ROP3_BLACKNESS:
            filler = 0;
            break;
        case ROP3_WHITENESS:
            filler = make_filler(ctx->bpp, UINT32_MAX);
            break;
        }
        DPRINTF("pixman_fill(%p, %ld, %d, %d, %d, %d, %d, %x)\n",
                ctx->dst_bits, ctx->dst_stride / sizeof(uint32_t), ctx->bpp,
                vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height, filler);
#ifdef CONFIG_PIXMAN
        uint32_t pixman_filler = filler;

        if (ctx->need_swap) {
            bswap32s(&pixman_filler);
        }
        if (ctx->bpp == 24 || !(use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)ctx->dst_bits,
                         ctx->dst_stride / sizeof(uint32_t), ctx->bpp,
                         vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height,
                         pixman_filler))
#endif
        {
            ati_2d_fill_rows(ctx, &vis_dst, filler);
        }
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      ctx->rop3 >> 16);
        return false;
    }

    return true;
}

static bool ati_2d_tiled_access(ATIVGAState *s, const ATI2DCtx *ctx,
                                 bool source, int x, int y, uint8_t *buffer,
                                 uint64_t length, bool write)
{
    uint32_t address = source ? ctx->src_offset : ctx->dst_offset;
    unsigned int stride = source ? ctx->src_stride : ctx->dst_stride;
    unsigned int tile = source ? ctx->src_tile : ctx->dst_tile;
    uint32_t xbyte = x * (ctx->bpp / 8);

    for (uint64_t done = 0; done < length; ) {
        uint64_t offset, physical;
        unsigned int count = MIN(length - done, 16 - ((xbyte + done) & 15));

        if (!ati_2d_tile_offset(s, address, stride, ctx->bpp / 8, tile,
                                xbyte + done, y, &offset)) {
            return false;
        }
        if (ctx->rage128) {
            physical = address + offset;
            if (physical > s->vga.vram_size ||
                count > s->vga.vram_size - physical) {
                return false;
            }
        } else if (!ati_r100_gpu_vram_offset(s, address + offset, count,
                                              &physical)) {
            return false;
        }
        if (write) {
            memcpy(s->vga.vram_ptr + physical, buffer + done, count);
            memory_region_set_dirty(&s->vga.vram, physical, count);
        } else {
            memcpy(buffer + done, s->vga.vram_ptr + physical, count);
        }
        done += count;
    }
    return true;
}

static bool ati_2d_surface_read(ATIVGAState *s, const ATI2DCtx *ctx,
                                bool source, int x, int y,
                                uint8_t *buf, uint64_t length)
{
    const uint8_t *bits = source ? ctx->src_bits : ctx->dst_bits;
    uint32_t address = source ? ctx->src_offset : ctx->dst_offset;
    int stride = source ? ctx->src_stride : ctx->dst_stride;
    uint64_t offset = (uint64_t)y * stride +
                      (uint64_t)x * (ctx->bpp / 8);

    if ((source ? ctx->src_tile : ctx->dst_tile) &&
        !(source && ctx->host_data_active)) {
        return ati_2d_tiled_access(s, ctx, source, x, y, buf, length, false);
    }
    if (bits) {
        memcpy(buf, bits + offset, length);
        return true;
    }
    return ati_r100_gpu_read(s, (uint64_t)address + offset, buf, length);
}

static bool ati_2d_surface_write(ATIVGAState *s, const ATI2DCtx *ctx,
                                 int x, int y, const uint8_t *buf,
                                 uint64_t length)
{
    uint64_t offset = (uint64_t)y * ctx->dst_stride +
                      (uint64_t)x * (ctx->bpp / 8);

    if (ctx->dst_tile) {
        return ati_2d_tiled_access(s, ctx, false, x, y, (uint8_t *)buf,
                                   length, true);
    }
    if (ctx->dst_bits) {
        memcpy(ctx->dst_bits + offset, buf, length);
        memory_region_set_dirty(&ctx->vga->vram,
                                ctx->dst_vram_offset + offset, length);
        return true;
    }
    return ati_r100_gpu_write(s, (uint64_t)ctx->dst_offset + offset,
                              buf, length, true);
}

static bool ati_2d_staged_row_overlap(ATIVGAState *s, const ATI2DCtx *ctx,
                                      const QemuRect *vis_src,
                                      const QemuRect *vis_dst,
                                      unsigned int y, uint64_t row_bytes,
                                      bool *overlap)
{
    uint64_t bypp = ctx->bpp / 8;
    uint64_t src = ctx->src_offset +
                   (uint64_t)(vis_src->y + y) * ctx->src_stride +
                   (uint64_t)vis_src->x * bypp;
    uint64_t dst = ctx->dst_offset +
                   (uint64_t)(vis_dst->y + y) * ctx->dst_stride +
                   (uint64_t)vis_dst->x * bypp;

    return ati_r100_gpu_ranges_overlap(s, src, row_bytes, dst, row_bytes,
                                        overlap);
}

static bool ati_2d_do_blt_staged_overlap(ATIVGAState *s,
                                         const ATI2DCtx *ctx,
                                         const QemuRect *vis_src,
                                         const QemuRect *vis_dst,
                                         unsigned int y,
                                         bool preserve_destination)
{
    unsigned int bypp = ctx->bpp / 8;
    uint8_t rop = ctx->rop3 >> 16;
    bool generic = ati_2d_uses_generic_rop(ctx);
    bool pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;
    bool destination_needed = ati_2d_destination_needed(ctx);

    for (unsigned int xi = 0; xi < vis_dst->width; xi++) {
        uint8_t src_pixel[4];
        uint8_t dst_pixel[4];
        unsigned int x = ctx->left_to_right ? xi : vis_dst->width - 1 - xi;
        int src_x = vis_src->x + x;
        int src_y = vis_src->y + y;
        int dst_x = vis_dst->x + x;
        int dst_y = vis_dst->y + y;
        uint32_t pattern = 0;
        uint32_t result;

        /* Preserve reads, including their order, before brush evaluation. */
        if (!ati_2d_surface_read(s, ctx, true, src_x, src_y,
                                 src_pixel, bypp) ||
            (preserve_destination &&
             !ati_2d_surface_read(s, ctx, false, dst_x, dst_y,
                                  dst_pixel, bypp))) {
            return false;
        }
        if (!generic) {
            /* Only SRCCOPY needs a source in the non-generic path. */
            memcpy(dst_pixel, src_pixel, bypp);
        } else {
            if (pattern_needed && !ati_2d_brush_supported(ctx)) {
                qemu_log_mask(LOG_UNIMP, "Unsupported ATI 2D brush type %u\n",
                              ctx->brush_type);
                return false;
            }
            if ((!pattern_needed ||
                 ati_2d_brush_pixel(ctx, dst_x, dst_y, &pattern)) &&
                ati_2d_composite_pixel(ctx, pattern,
                    ati_load_pixel(ctx, src_pixel),
                    destination_needed ? ati_load_pixel(ctx, dst_pixel) : 0,
                    false, &result)) {
                ati_store_pixel(ctx, dst_pixel, make_filler(ctx->bpp, result));
            }
        }
        /* Skipped pixels write back their preserved values. */
        if (!ati_2d_surface_write(s, ctx, dst_x, dst_y, dst_pixel, bypp)) {
            return false;
        }
    }
    return true;
}

static bool ati_2d_do_blt_staged(ATIVGAState *s, const ATI2DCtx *ctx,
                                 QemuRect vis_src, QemuRect vis_dst,
                                 bool source_needed)
{
    unsigned int row_bytes = vis_dst.width * (ctx->bpp / 8);
    size_t buffer_size = (size_t)row_bytes * (source_needed ? 2 : 1);
    bool reuse = !s->blt_row_buffer_busy && buffer_size <= 128 * KiB;
    g_autofree uint8_t *private_buffer = NULL;
    uint8_t *buffer;
    uint8_t *src_row;
    uint8_t *dst_row;
    bool success = false;
    bool preserve_destination = ati_2d_preserve_destination(ctx);
    unsigned int yi;

    g_autofree uint8_t *tile_source = NULL;

    if (source_needed && !ctx->host_data_active &&
        (ctx->src_tile || ctx->dst_tile)) {
        tile_source = g_malloc((size_t)row_bytes * vis_src.height);
        for (unsigned int row = 0; row < vis_src.height; row++) {
            if (!ati_2d_surface_read(s, ctx, true, vis_src.x, vis_src.y + row,
                                     tile_source + (size_t)row * row_bytes,
                                     row_bytes)) {
                return false;
            }
        }
    }
    if (reuse) {
        s->blt_row_buffer_busy = true;
        if (s->blt_row_buffer_size < buffer_size) {
            s->blt_row_buffer = g_realloc(s->blt_row_buffer, buffer_size);
            s->blt_row_buffer_size = buffer_size;
        }
        buffer = s->blt_row_buffer;
    } else {
        private_buffer = g_malloc(buffer_size);
        buffer = private_buffer;
    }
    dst_row = buffer;
    src_row = source_needed ? buffer + row_bytes : NULL;

    for (yi = 0; yi < vis_dst.height; yi++) {
        ATI2DCtx row = *ctx;
        unsigned int y = ctx->top_to_bottom ? yi :
                         vis_dst.height - 1 - yi;
        int src_y = vis_src.y + y;
        int dst_y = vis_dst.y + y;
        QemuRect local_src;
        QemuRect local_dst;
        bool overlap = false;
        const uint8_t *source = src_row;
        uint8_t *destination = dst_row;
        unsigned int bypp = ctx->bpp / 8;

        if (source_needed && !ctx->host_data_active && !tile_source &&
            (!ati_2d_staged_row_overlap(s, ctx, &vis_src, &vis_dst, y,
                                        row_bytes, &overlap) ||
             (overlap &&
              !ati_2d_do_blt_staged_overlap(s, ctx, &vis_src, &vis_dst, y,
                                             preserve_destination)))) {
            goto out;
        }
        if (overlap) {
            continue;
        }

        /*
         * A staged destination is committed only after evaluation.  Reading
         * its old pixels through DMA could alter the source via MMIO, so
         * retain the source snapshot whenever those reads are required.
         */
        if (tile_source) {
            source = tile_source + (size_t)y * row_bytes;
        } else if (source_needed && ctx->src_bits && !preserve_destination) {
            source = ctx->src_bits + (size_t)src_y * ctx->src_stride +
                     (size_t)vis_src.x * bypp;
        } else if (source_needed &&
                   !ati_2d_surface_read(s, ctx, true, vis_src.x, src_y,
                                        src_row, row_bytes)) {
            goto out;
        }
        if (ctx->dst_bits) {
            /* All faultable source reads have finished before row writes. */
            destination = ctx->dst_bits + (size_t)dst_y * ctx->dst_stride +
                          (size_t)vis_dst.x * bypp;
        } else if (preserve_destination &&
                   !ati_2d_surface_read(s, ctx, false, vis_dst.x, dst_y,
                                        dst_row, row_bytes)) {
            goto out;
        }
        /* Otherwise every byte is overwritten, or rejected before writing. */

        qemu_rect_init(&local_src, 0, 0, vis_src.width, 1);
        qemu_rect_init(&local_dst, 0, 0, vis_dst.width, 1);
        row.src = local_src;
        row.dst = local_dst;
        row.scissor = local_dst;
        row.source_clip_active = false;
        row.src_bits = source;
        row.dst_bits = destination;
        row.src_stride = row_bytes;
        row.dst_stride = row_bytes;
        row.src_vram_offset = 0;
        row.dst_vram_offset = row_bytes;
        row.host_data_active = true;
        /* Keep the full phase; ati_2d_brush_pixel() wraps to the brush size. */
        row.brush_x = ctx->brush_x - vis_dst.x;
        row.brush_y = ctx->brush_y - dst_y;
        if (!ati_2d_do_blt_direct(&row, local_src, local_dst, 0)) {
            goto out;
        }
        if (ctx->dst_bits) {
            memory_region_set_dirty(&ctx->vga->vram,
                ctx->dst_vram_offset + (size_t)dst_y * ctx->dst_stride +
                (size_t)vis_dst.x * bypp, row_bytes);
        } else if (!ati_2d_surface_write(s, ctx, vis_dst.x, dst_y,
                                         dst_row, row_bytes)) {
            goto out;
        }
    }
    success = true;
out:
    if (reuse) {
        s->blt_row_buffer_busy = false;
    }
    return success;
}

static bool ati_2d_do_blt(ATIVGAState *s, const ATI2DCtx *ctx,
                          uint8_t use_pixman, bool account_work)
{
    ATI2DCtx direct = *ctx;
    QemuRect vis_src;
    QemuRect vis_dst;
    uint8_t *src_bits;
    uint8_t *read_bits;
    uint64_t pixels;
    uint64_t end;
    uint64_t read_vram_offset;
    bool source_needed;

    if (!ctx->bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return false;
    }
    if (!ati_2d_clip_rects(ctx, &vis_src, &vis_dst)) {
        return false;
    }
    if (!ati_2d_prepare_surface(s, ctx, ctx->dst_offset, ctx->dst_stride,
                                ctx->dst_tile, &vis_dst, "destination", true,
                                &direct.dst_bits,
                                &direct.dst_vram_offset)) {
        return false;
    }
    if (ati_2d_preserve_destination(ctx) && !direct.dst_bits &&
        !ati_2d_prepare_surface(s, ctx, ctx->dst_offset, ctx->dst_stride,
                                ctx->dst_tile, &vis_dst, "destination", false,
                                &read_bits, &read_vram_offset)) {
        return false;
    }
    source_needed = ati_2d_source_needed(ctx);
    if (source_needed) {
        if (ctx->host_data_active) {
            if (!ctx->src_bits ||
                !ati_2d_rect_layout(ctx, ctx->src_stride, &vis_src,
                                    "source", &end)) {
                return false;
            }
        } else if (!ati_2d_prepare_surface(s, ctx, ctx->src_offset,
                                           ctx->src_stride, ctx->src_tile,
                                           &vis_src, "source", false, &src_bits,
                                           &direct.src_vram_offset)) {
            return false;
        } else {
            direct.src_bits = src_bits;
        }
    }
    pixels = (uint64_t)vis_dst.width * vis_dst.height;
    if (pixels > ATI_2D_MAX_PIXELS ||
        (account_work && !ati_3d_consume_2d_work(s, pixels))) {
        return false;
    }
    DPRINTF("dst: (%d,%d) %dx%d -> vis_dst: (%d,%d) %dx%d\n",
            ctx->dst.x, ctx->dst.y, ctx->dst.width, ctx->dst.height,
            vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height);
    DPRINTF("src: (%d,%d) %dx%d -> vis_src: (%d,%d) %dx%d\n",
            ctx->src.x, ctx->src.y, ctx->dst.width, ctx->dst.height,
            vis_src.x, vis_src.y, vis_src.width, vis_src.height);

    if (direct.dst_bits && (!source_needed || direct.src_bits)) {
        bool success = ati_2d_do_blt_direct(&direct, vis_src, vis_dst,
                                            use_pixman);

        /*
         * Direct rendering either rejects the operation before writing or
         * completes it, so successful output can be dirtied once here.
         */
        if (success) {
            ati_2d_mark_direct_dirty(&direct, &vis_dst);
        }
        return success;
    }
    return ati_2d_do_blt_staged(s, &direct, vis_src, vis_dst,
                                source_needed);
}

void ati_2d_blt(ATIVGAState *s)
{
    ATI2DCtx ctx;
    uint32_t src_source = s->regs.dp_mix & DP_SRC_SOURCE;

    /* Finish any active HOST_DATA blits before starting a new blit */
    ati_host_data_finish(s);

    if (src_source == DP_SRC_HOST || src_source == DP_SRC_HOST_BYTEALIGN) {
        /* Begin a HOST_DATA blit */
        s->host_data.active = true;
        s->host_data.next = 0;
        s->host_data.col = 0;
        s->host_data.row = 0;
        memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
        s->host_data.pending_count = 0;
        return;
    }
    setup_2d_blt_ctx(s, &ctx);
    ati_2d_do_blt(s, &ctx, s->use_pixman, true);
    ati_2d_complete(s);
}

static bool ati_host_data_blit_pixels(ATIVGAState *s, const ATI2DCtx *ctx,
                                      const uint32_t *pixels,
                                      unsigned int count,
                                      unsigned int logical_col,
                                      bool account_work)
{
    ATI2DCtx chunk = *ctx;
    QemuRect visible;
    QEMU_UNINITIALIZED uint8_t stack_buf[128 * sizeof(uint32_t)];
    g_autofree uint8_t *heap_buf = NULL;
    uint8_t *pix_buf = stack_buf;
    unsigned int bypp = ctx->bpp / 8;
    unsigned int dst_col;
    unsigned int dst_row;
    unsigned int stride;

    if (!count) {
        return true;
    }

    dst_col = ctx->left_to_right ? logical_col :
              ctx->dst.width - logical_col - count;
    dst_row = ctx->top_to_bottom ? s->host_data.row :
              ctx->dst.height - 1 - s->host_data.row;
    stride = QEMU_ALIGN_UP(count * bypp, sizeof(uint32_t));
    if (stride > sizeof(stack_buf)) {
        heap_buf = g_malloc(stride);
        pix_buf = heap_buf;
    }
    for (unsigned int i = 0; i < count; i++) {
        unsigned int buf_col = ctx->left_to_right ? i : count - 1 - i;

        ati_store_pixel(ctx, &pix_buf[buf_col * bypp], pixels[i]);
    }
    /* Pixel stores initialize the payload; pixman may also read its padding. */
    for (unsigned int i = count * bypp; i < stride; i++) {
        pix_buf[i] = 0;
    }

    chunk.src_bits = pix_buf;
    chunk.src.x = 0;
    chunk.src.y = 0;
    chunk.src_stride = stride;
    chunk.dst.x = ctx->dst.x + dst_col;
    chunk.dst.y = ctx->dst.y + dst_row;
    chunk.dst.width = count;
    chunk.dst.height = 1;
    if (!qemu_rect_intersect(&chunk.dst, &chunk.scissor, &visible)) {
        return true;
    }
    return ati_2d_do_blt(s, &chunk, s->use_pixman, account_work);
}

static void ati_host_data_advance(ATIVGAState *s, const ATI2DCtx *ctx,
                                  unsigned int count)
{
    s->host_data.col += count;
    if (s->host_data.col >= ctx->dst.width) {
        s->host_data.col = 0;
        s->host_data.row++;
    }
    if (s->host_data.row >= ctx->dst.height) {
        s->host_data.active = false;
    }
}

static uint32_t ati_radeon_host_data_swap_word(const ATIVGAState *s,
                                               uint32_t data)
{
    switch (s->regs.rbbm_guicntl & HOST_DATA_SWAP_MASK) {
    case HOST_DATA_SWAP_16BIT:
        return ((data & 0x00ff00ff) << 8) |
               ((data & 0xff00ff00) >> 8);
    case HOST_DATA_SWAP_32BIT:
        return bswap32(data);
    case HOST_DATA_SWAP_HDW:
        return (data << 16) | (data >> 16);
    default:
        return data;
    }
}

static uint32_t ati_rage128_host_data_swap_word(const ATIVGAState *s,
                                                uint32_t data,
                                                unsigned int bpp)
{
    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF ||
        !(s->regs.dp_datatype & HOST_BIG_ENDIAN_EN)) {
        return data;
    }
    if (bpp == 16) {
        return ((data & 0x00ff00ff) << 8) |
               ((data & 0xff00ff00) >> 8);
    }
    return bpp == 32 ? bswap32(data) : data;
}

static uint32_t ati_host_data_color(const uint8_t *data, unsigned int bypp)
{
    switch (bypp) {
    case 1:
        return data[0];
    case 2:
        return lduw_le_p(data);
    case 3:
        return data[0] | (uint32_t)data[1] << 8 |
               (uint32_t)data[2] << 16;
    case 4:
        return ldl_le_p(data);
    default:
        g_assert_not_reached();
    }
}

static bool ati_host_data_color_write(ATIVGAState *s, const ATI2DCtx *ctx,
                                      unsigned int bank,
                                      unsigned int dwords, bool draw)
{
    uint8_t stream[3 + ATI_HOST_DATA_BANK_DWORDS * sizeof(uint32_t)];
    uint32_t pixels[ARRAY_SIZE(stream)];
    unsigned int bypp = ctx->bpp / 8;
    unsigned int total;
    unsigned int offset = 0;
    bool success = true;

    memcpy(stream, s->host_data.pending, s->host_data.pending_count);
    total = s->host_data.pending_count;
    for (unsigned int i = 0; i < dwords; i++) {
        uint8_t word[sizeof(uint32_t)];

        stl_le_p(word, ati_rage128_host_data_swap_word(
            s, s->host_data.acc[bank + i], ctx->bpp));
        memcpy(stream + total, word, sizeof(word));
        total += sizeof(word);
    }

    while (total - offset >= bypp && s->host_data.active) {
        unsigned int count = MIN((total - offset) / bypp,
                                 ctx->dst.width - s->host_data.col);

        if (draw) {
            for (unsigned int i = 0; i < count; i++) {
                pixels[i] = make_filler(
                    ctx->bpp,
                    ati_host_data_color(&stream[offset + i * bypp], bypp));
            }
            if (!ati_host_data_blit_pixels(s, ctx, pixels, count,
                                           s->host_data.col, true)) {
                s->host_data.active = false;
                success = false;
                break;
            }
        }
        offset += count * bypp;
        ati_host_data_advance(s, ctx, count);
    }

    if (s->host_data.active) {
        s->host_data.pending_count = total - offset;
        memcpy(s->host_data.pending, stream + offset,
               s->host_data.pending_count);
    } else {
        s->host_data.pending_count = 0;
        memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
    }
    return success;
}

static bool ati_host_data_mono_blit(ATIVGAState *s, const ATI2DCtx *ctx,
                                    const uint32_t *pixels,
                                    const bool *foreground,
                                    unsigned int count,
                                    uint32_t src_datatype,
                                    unsigned int start_col)
{
    uint8_t rop = ctx->rop3 >> 16;
    bool pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;
    bool destination_needed = ati_2d_destination_needed(ctx);
    bool preserve_destination = ati_2d_preserve_destination(ctx);
    uint32_t pixel_mask = ati_pixel_mask(ctx->bpp);
    unsigned int bypp = ctx->bpp / 8;
    unsigned int dst_row;
    unsigned int visible = 0;
    unsigned int first = 0;

    if (src_datatype == SRC_MONO_FRGD_BKGD) {
        return ati_host_data_blit_pixels(s, ctx, pixels, count, start_col,
                                         true);
    }

    dst_row = ctx->top_to_bottom ? s->host_data.row :
              ctx->dst.height - 1 - s->host_data.row;
    while (first < count) {
        unsigned int last;
        unsigned int logical_col;
        unsigned int dst_col;
        QemuRect run_rect;
        QemuRect clipped;
        uint8_t *bits;
        uint64_t vram_offset;

        while (first < count && !foreground[first]) {
            first++;
        }
        last = first;
        while (last < count && foreground[last]) {
            last++;
        }
        if (last == first) {
            break;
        }
        logical_col = start_col + first;
        dst_col = ctx->left_to_right ? logical_col :
                  ctx->dst.width - logical_col - (last - first);
        qemu_rect_init(&run_rect, ctx->dst.x + dst_col,
                       ctx->dst.y + dst_row, last - first, 1);
        if (!qemu_rect_intersect(&run_rect, &ctx->scissor, &clipped)) {
            first = last;
            continue;
        }
        if (!ati_2d_prepare_surface(s, ctx, ctx->dst_offset,
                                    ctx->dst_stride, ctx->dst_tile, &clipped,
                                    "destination", true, &bits,
                                    &vram_offset) ||
            ((destination_needed ||
              (last - first >= 4 && preserve_destination)) &&
             !ati_2d_prepare_surface(s, ctx, ctx->dst_offset,
                                     ctx->dst_stride, ctx->dst_tile, &clipped,
                                     "destination", false, &bits,
                                     &vram_offset))) {
            return false;
        }
        visible += clipped.width;
        first = last;
    }
    if (!ati_3d_consume_2d_work(s, visible)) {
        return false;
    }

    first = 0;
    while (first < count) {
        unsigned int last;

        while (first < count && !foreground[first]) {
            first++;
        }
        last = first;
        while (last < count && foreground[last]) {
            last++;
        }
        if (last == first) {
            break;
        }
        if (last - first >= 4) {
            if (!ati_host_data_blit_pixels(s, ctx, pixels + first,
                                           last - first, start_col + first,
                                           false)) {
                return false;
            }
        } else {
            for (unsigned int i = first; i < last; i++) {
                unsigned int logical_col = start_col + i;
                unsigned int dst_col = ctx->left_to_right ? logical_col :
                                       ctx->dst.width - 1 - logical_col;
                int x = ctx->dst.x + dst_col;
                int y = ctx->dst.y + dst_row;
                uint8_t dst_pixel[4] = { 0 };
                uint32_t destination = 0;
                uint32_t pattern = 0;
                uint32_t source;
                uint32_t result;

                if (x < ctx->scissor.x || y < ctx->scissor.y ||
                    x >= ctx->scissor.x + ctx->scissor.width ||
                    y >= ctx->scissor.y + ctx->scissor.height) {
                    continue;
                }
                if (pattern_needed &&
                    !ati_2d_brush_pixel(ctx, x, y, &pattern)) {
                    continue;
                }
                if (destination_needed) {
                    if (!ati_2d_surface_read(s, ctx, false, x, y,
                                             dst_pixel, bypp)) {
                        return false;
                    }
                    destination = ati_load_pixel(ctx, dst_pixel);
                }
                source = pixels[i];
                if (ctx->color_compare_active &&
                    !ati_color_compare(ctx, &source, destination)) {
                    continue;
                }
                result = ati_apply_rop3(ctx, pattern, source,
                                        destination) & pixel_mask;
                if (ctx->write_mask_active) {
                    uint32_t mask = ctx->write_mask & pixel_mask;

                    result = (result & mask) | (destination & ~mask);
                }
                ati_store_pixel(ctx, dst_pixel,
                                make_filler(ctx->bpp, result));
                if (!ati_2d_surface_write(s, ctx, x, y, dst_pixel, bypp)) {
                    return false;
                }
            }
        }
        first = last;
    }
    return true;
}

static bool ati_host_data_mono_write(ATIVGAState *s, const ATI2DCtx *ctx,
                                     unsigned int bank, unsigned int dwords,
                                     uint32_t src_datatype, bool draw)
{
    uint8_t stream[ATI_HOST_DATA_BANK_DWORDS * sizeof(uint32_t)];
    uint32_t pixels[ARRAY_SIZE(stream) * 8];
    bool foreground[ARRAY_SIZE(stream) * 8];
    uint32_t byte_pix_order = s->regs.dp_datatype & DP_BYTE_PIX_ORDER;
    uint32_t fg = make_filler(ctx->bpp, s->regs.dp_src_frgd_clr);
    uint32_t bg = make_filler(ctx->bpp, s->regs.dp_src_bkgd_clr);
    unsigned int total_bits = dwords * sizeof(uint32_t) * 8;
    unsigned int bit_offset = 0;

    s->host_data.pending_count = 0;
    memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
    for (unsigned int i = 0; i < dwords; i++) {
        stl_le_p(&stream[i * sizeof(uint32_t)],
                 ati_rage128_host_data_swap_word(
            s, s->host_data.acc[bank + i], ctx->bpp));
    }

    while (bit_offset < total_bits && s->host_data.active) {
        unsigned int old_row = s->host_data.row;
        unsigned int count = MIN(total_bits - bit_offset,
                                 ctx->dst.width - s->host_data.col);
        unsigned int start_col = s->host_data.col;

        if (draw) {
            for (unsigned int i = 0; i < count; i++) {
                unsigned int stream_bit = bit_offset + i;
                uint8_t byte_val = stream[stream_bit / 8];
                unsigned int bit = stream_bit % 8;

                foreground[i] = byte_val &
                                BIT(byte_pix_order ? bit : 7 - bit);
                pixels[i] = foreground[i] ? fg : bg;
            }
            if (!ati_host_data_mono_blit(s, ctx, pixels, foreground, count,
                                         src_datatype, start_col)) {
                s->host_data.active = false;
                return false;
            }
        }
        bit_offset += count;
        ati_host_data_advance(s, ctx, count);
        if (s->host_data.row != old_row &&
            ctx->src_source == DP_SRC_HOST_BYTEALIGN) {
            bit_offset = QEMU_ALIGN_UP(bit_offset, 8);
        }
    }
    return true;
}

static bool ati_host_data_consume(ATIVGAState *s, unsigned int bank,
                                  unsigned int dwords)
{
    ATI2DCtx ctx;
    QemuRect visible;
    uint32_t src_source = s->regs.dp_mix & DP_SRC_SOURCE;
    uint32_t src_datatype = s->regs.dp_datatype & DP_SRC_DATATYPE;
    uint8_t rop;
    bool pattern_needed;
    bool draw;

    if (!s->host_data.active || !dwords) {
        return false;
    }
    g_assert(bank <= ATI_HOST_DATA_BANK_DWORDS &&
             dwords <= ATI_HOST_DATA_BANK_DWORDS - bank);
    if (src_source != DP_SRC_HOST &&
        src_source != DP_SRC_HOST_BYTEALIGN) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "host_data_blt: unsupported src_source %x\n", src_source);
        s->host_data.active = false;
        return false;
    }
    if (src_datatype != SRC_MONO_FRGD_BKGD && src_datatype != SRC_MONO_FRGD &&
        src_datatype != SRC_COLOR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "host_data_blt: undefined src_datatype %x\n",
                      src_datatype);
        s->host_data.active = false;
        return false;
    }

    setup_2d_blt_ctx(s, &ctx);
    rop = ctx.rop3 >> 16;
    pattern_needed = ((rop ^ (rop >> 4)) & 0x0f) != 0;

    if (!ctx.dst.width || !ctx.dst.height) {
        s->host_data.active = false;
        return false;
    }
    if (s->host_data.row >= ctx.dst.height ||
        s->host_data.col >= ctx.dst.width) {
        s->host_data.active = false;
        return false;
    }
    if ((uint64_t)ctx.dst.width * ctx.dst.height > ATI_2D_MAX_PIXELS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "host_data_blt: pixel count exceeds limit\n");
        s->host_data.active = false;
        return false;
    }
    if (!ctx.bpp) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "host_data_blt: invalid bpp from datatype\n");
        s->host_data.active = false;
        return false;
    }
    qemu_rect_intersect(&ctx.dst, &ctx.scissor, &visible);
    draw = visible.width > 0 && visible.height > 0;
    if (draw && pattern_needed && !ati_2d_brush_supported(&ctx)) {
        qemu_log_mask(LOG_UNIMP, "Unsupported ATI 2D brush type %u\n",
                      ctx.brush_type);
        s->host_data.active = false;
        return false;
    }

    if (src_datatype == SRC_COLOR) {
        if (!ati_host_data_color_write(s, &ctx, bank, dwords, draw)) {
            return false;
        }
    } else {
        if (!ati_host_data_mono_write(s, &ctx, bank, dwords, src_datatype,
                                      draw)) {
            return false;
        }
    }

    return s->host_data.active;
}

static bool ati_host_data_input_complete(const ATIVGAState *s)
{
    const ATIHostDataState *host = &s->host_data;
    uint32_t width = s->regs.dst_width;
    uint32_t height = s->regs.dst_height;
    uint32_t datatype = s->regs.dp_datatype & DP_SRC_DATATYPE;
    uint64_t buffered = host->next * 32;
    uint64_t remaining;

    if (!width || host->row >= height || host->col >= width) {
        return true;
    }
    if (datatype == SRC_COLOR) {
        unsigned int bpp = ati_bpp_from_datatype(s);

        remaining = ((uint64_t)(height - host->row) * width - host->col) *
                    bpp;
        buffered += host->pending_count * 8;
    } else if (datatype == SRC_MONO_FRGD_BKGD || datatype == SRC_MONO_FRGD) {
        unsigned int row_bits = width;

        if ((s->regs.dp_mix & DP_SRC_SOURCE) == DP_SRC_HOST_BYTEALIGN) {
            row_bits = QEMU_ALIGN_UP(width, 8);
        }
        remaining = (uint64_t)(height - host->row - 1) * row_bits +
                    width - host->col;
    } else {
        /* Let the consumer reject the unsupported source format. */
        return true;
    }
    return buffered >= remaining;
}

bool ati_host_data_write(ATIVGAState *s, uint32_t data, bool last)
{
    if (!s->host_data.active) {
        return false;
    }

    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
        data = ati_radeon_host_data_swap_word(s, data);
    }
    s->host_data.acc[s->host_data.next] = data;
    if (last) {
        ati_host_data_consume(s, 0, s->host_data.next + 1);
        if (s->host_data.active) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "HOST_DATA blit ended before all data was written\n");
        }
        s->host_data.active = false;
        s->host_data.next = 0;
        s->host_data.pending_count = 0;
        memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
    } else {
        s->host_data.next++;
        /*
         * Consume a complete rectangle even without HOST_DATA_LAST, before
         * a subsequent blit can change the drawing registers.
         */
        if (s->host_data.next == ATI_HOST_DATA_BANK_DWORDS ||
            ati_host_data_input_complete(s)) {
            ati_host_data_consume(s, 0, s->host_data.next);
            s->host_data.next = 0;
        }
    }

    if (!s->host_data.active) {
        ati_2d_complete(s);
    }
    return s->host_data.active;
}

void ati_host_data_finish(ATIVGAState *s)
{
    unsigned int dwords;

    if (!s->host_data.active) {
        return;
    }
    dwords = s->host_data.next;
    if (dwords) {
        ati_host_data_consume(s, 0, dwords);
    }
    if (s->host_data.active) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "HOST_DATA blit ended before all data was written\n");
    }
    s->host_data.active = false;
    s->host_data.next = 0;
    s->host_data.pending_count = 0;
    memset(s->host_data.pending, 0, sizeof(s->host_data.pending));
    ati_2d_complete(s);
}

static bool ati_2d_line_pixel(const ATI2DCtx *ctx, int x, int y,
                             unsigned int phase)
{
    ATI2DCtx pixel = *ctx;

    if (!ati_3d_consume_2d_work(ctx->s, 1)) {
        return false;
    }
    qemu_rect_init(&pixel.dst, x, y, 1, 1);
    pixel.src = pixel.dst;
    /* Line brushes advance along the trajectory, even under clipping. */
    if (ctx->brush_type >= 2 && ctx->brush_type <= 7) {
        pixel.brush_x = (x - phase) & 31;
        pixel.brush_y = (y - phase) & 31;
    }
    ati_2d_do_blt(ctx->s, &pixel, 0, false);
    return !ctx->s->r100_3d.command_budget_exhausted;
}

/* Software Bresenham arithmetic leaves the setup registers unchanged. */
static void ati_2d_bresenham(ATIVGAState *s, unsigned int length)
{
    ATI2DCtx ctx;
    int x = ati_coord_14(s->regs.dst_x);
    int y = ati_coord_14(s->regs.dst_y);
    int64_t error = (int32_t)s->regs.bres[0];
    int inc = (int32_t)s->regs.bres[1];
    int dec = (int32_t)s->regs.bres[2];
    int xdir = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ? 1 : -1;
    int ydir = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ? 1 : -1;
    bool major_y = s->regs.dp_cntl & DST_Y_MAJOR;

    ati_host_data_finish(s);
    setup_2d_blt_ctx(s, &ctx);
    ctx.source_clip_active = false;
    for (unsigned int i = 0; i < length; i++) {
        if (!ati_2d_line_pixel(&ctx, x, y,
                              i + (s->regs.brush_y_x & 31))) {
            break;
        }
        error += inc;
        if (error >= 0) {
            error += dec;
            if (major_y) {
                x += xdir;
            } else {
                y += ydir;
            }
        }
        if (major_y) {
            y += ydir;
        } else {
            x += xdir;
        }
    }
    ati_2d_complete(s);
}

void ati_2d_polyline(ATIVGAState *s, const uint32_t *points,
                     unsigned int count)
{
    ATI2DCtx ctx;
    unsigned int phase = s->regs.brush_y_x & 31;

    ati_host_data_finish(s);
    setup_2d_blt_ctx(s, &ctx);
    ctx.source_clip_active = false;
    for (unsigned int vertex = 1; vertex < count; vertex++) {
        int x = ati_coord_14(points[vertex - 1]);
        int y = ati_coord_14(points[vertex - 1] >> 16);
        int end_x = ati_coord_14(points[vertex]);
        int end_y = ati_coord_14(points[vertex] >> 16);
        int dx = abs(end_x - x);
        int dy = abs(end_y - y);
        int xdir = end_x >= x ? 1 : -1;
        int ydir = end_y >= y ? 1 : -1;
        bool major_y = dy > dx;
        int length = MAX(dx, dy);
        int error = -length;
        int inc = 2 * MIN(dx, dy);

        /* Exclude the endpoint so shared vertices are drawn once. */
        for (int i = 0; i < length; i++, phase++) {
            if (!ati_2d_line_pixel(&ctx, x, y, phase)) {
                ati_2d_complete(s);
                return;
            }
            error += inc;
            if (error >= 0) {
                error -= 2 * length;
                if (major_y) {
                    x += xdir;
                } else {
                    y += ydir;
                }
            }
            if (major_y) {
                y += ydir;
            } else {
                x += xdir;
            }
        }
    }
    ati_2d_complete(s);
}

/* Integer edge slopes and half-open spans approximate trapezoid coverage. */
static void ati_2d_trapezoid(ATIVGAState *s, unsigned int height)
{
    ATI2DCtx ctx;
    int x = ati_coord_14(s->regs.dst_x);
    int y = ati_coord_14(s->regs.dst_y);
    int trail = ati_coord_14(s->regs.trail[3]);
    int64_t dx = (int32_t)s->regs.bres[1];
    int64_t dy = -(int64_t)(int32_t)s->regs.bres[2];
    int64_t trail_dx = (int32_t)s->regs.trail[1];
    int64_t trail_dy = -(int64_t)(int32_t)s->regs.trail[2];
    int xdir = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ? 1 : -1;
    int ydir = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ? 1 : -1;
    int trail_dir = s->regs.dp_cntl & DST_TRAIL_X_LEFT_TO_RIGHT ? 1 : -1;
    uint64_t pixels = 0;

    ati_host_data_finish(s);
    if (dx < 0 || trail_dx < 0 || dy <= 0 || trail_dy <= 0) {
        qemu_log_mask(LOG_UNIMP, "ATI unsupported trapezoid edge parameters\n");
        ati_2d_complete(s);
        return;
    }
    setup_2d_blt_ctx(s, &ctx);
    ctx.source_clip_active = false;
    for (unsigned int i = 0; i < height; i++) {
        int64_t lead_x = x + xdir * (i * dx / dy);
        int64_t trail_x = trail + trail_dir * (i * trail_dx / trail_dy);
        int64_t left = MAX(MIN(lead_x, trail_x), 0);
        int64_t right = MIN(MAX(lead_x, trail_x), INT16_MAX);
        ATI2DCtx row = ctx;

        if (right <= left) {
            continue;
        }
        pixels += right - left;
        if (pixels > ATI_2D_MAX_PIXELS ||
            !ati_3d_consume_2d_work(s, right - left)) {
            break;
        }
        qemu_rect_init(&row.dst, left, y + ydir * (int)i, right - left, 1);
        row.src = row.dst;
        ati_2d_do_blt(s, &row, 0, false);
    }
    ati_2d_complete(s);
}

/* Generic bilinear filtering, rounded once per channel, with clamped edges. */
static uint32_t ati_2d_scale_blend(uint32_t a, uint32_t b,
                                  uint32_t c, uint32_t d,
                                  uint32_t fx, uint32_t fy,
                                  unsigned int datatype)
{
    uint32_t result = 0;
    unsigned int shift = 0;

    while (shift < (datatype == 3 ? 15 : datatype == 4 ? 16 : 32)) {
        unsigned int bits = datatype == 3 ? 5 :
                            datatype == 4 ? (shift == 5 ? 6 : 5) : 8;
        uint32_t mask = (1U << bits) - 1;
        uint64_t top = ((a >> shift) & mask) * (65536 - fx) +
                       ((b >> shift) & mask) * fx;
        uint64_t bottom = ((c >> shift) & mask) * (65536 - fx) +
                          ((d >> shift) & mask) * fx;
        uint32_t v = (top * (65536 - fy) + bottom * fy + BIT_ULL(31)) >> 32;

        result |= v << shift;
        shift += bits;
    }
    return result;
}

static void ati_2d_stretch(ATIVGAState *s)
{
    ATI2DCtx ctx, source;
    uint32_t *scale = s->regs.scale;
    unsigned int sw = scale[0] & 0x3fff, sh = (scale[0] >> 16) & 0x3fff;
    unsigned int dw = scale[8] & 0x3fff, dh = (scale[8] >> 16) & 0x3fff;
    unsigned int datatype = s->regs.dp_datatype & 15;
    unsigned int cpp;
    uint8_t *bits;
    uint64_t physical;
    QemuRect rect;
    g_autofree uint8_t *input = NULL;
    g_autofree uint8_t *output = NULL;

    ati_host_data_finish(s);
    if ((s->regs.scale_3d_cntl & 0xc0) != 0x40 ||
        !sw || !sh || !dw || !dh ||
        (uint64_t)sw * sh > ATI_2D_MAX_PIXELS ||
        (uint64_t)dw * dh > ATI_2D_MAX_PIXELS ||
        ((s->regs.scale_3d_datatype & 15) &&
         (s->regs.scale_3d_datatype & 15) != datatype)) {
        return;
    }
    setup_2d_blt_ctx(s, &ctx);
    cpp = ctx.bpp / 8;
    if (!cpp || !ati_3d_consume_2d_work(s, (uint64_t)dw * dh)) {
        return;
    }
    source = ctx;
    source.src_offset = scale[1];
    source.src_stride = (scale[2] & 0x7ff) * 8 * (cpp == 3 ? 1 : cpp);
    source.src_tile = (scale[2] >> 16) & 1;
    source.host_data_active = false;
    qemu_rect_init(&rect, 0, 0, sw, sh);
    if (!ati_2d_prepare_surface(s, &source, source.src_offset,
                                source.src_stride, source.src_tile, &rect,
                                "scaler source", false, &bits, &physical)) {
        ati_2d_complete(s);
        return;
    }
    source.src_bits = bits;
    input = g_malloc((size_t)sw * sh * cpp);
    output = g_malloc((size_t)dw * dh * cpp);
    for (unsigned int y = 0; y < sh; y++) {
        if (!ati_2d_surface_read(s, &source, true, 0, y,
                                 input + (size_t)y * sw * cpp, sw * cpp)) {
            return;
        }
    }
    for (unsigned int y = 0; y < dh; y++) {
        uint64_t sy = (uint64_t)scale[6] + (uint64_t)y * scale[4];
        unsigned int y0 = MIN(sy >> 16, sh - 1);
        unsigned int y1 = MIN(y0 + 1, sh - 1);

        for (unsigned int x = 0; x < dw; x++) {
            uint64_t sx = (uint64_t)scale[5] + (uint64_t)x * scale[3];
            unsigned int x0 = MIN(sx >> 16, sw - 1);
            unsigned int x1 = MIN(x0 + 1, sw - 1);
            uint32_t a = ati_load_pixel(&ctx, input +
                                        ((size_t)y0 * sw + x0) * cpp);

            if (!(s->regs.scale_3d_cntl & BIT(8)) && cpp != 1) {
                uint32_t b = ati_load_pixel(&ctx, input +
                                            ((size_t)y0 * sw + x1) * cpp);
                uint32_t c = ati_load_pixel(&ctx, input +
                                            ((size_t)y1 * sw + x0) * cpp);
                uint32_t d = ati_load_pixel(&ctx, input +
                                            ((size_t)y1 * sw + x1) * cpp);

                a = ati_2d_scale_blend(a, b, c, d, sx & 0xffff, sy & 0xffff,
                                      datatype);
            }
            ati_store_pixel(&ctx, output + ((size_t)y * dw + x) * cpp,
                             make_filler(ctx.bpp, a));
        }
    }
    ctx.src_bits = output;
    ctx.src_stride = dw * cpp;
    ctx.src_tile = 0;
    ctx.host_data_active = true;
    ctx.source_clip_active = false;
    ctx.left_to_right = ctx.top_to_bottom = true;
    qemu_rect_init(&ctx.src, 0, 0, dw, dh);
    qemu_rect_init(&ctx.dst, ati_coord_14(scale[7] >> 16),
                   ati_coord_14(scale[7]), dw, dh);
    ati_2d_do_blt(s, &ctx, 0, false);
    ati_2d_complete(s);
}

static uint32_t *ati_2d_extra_register(ATIVGAState *s, hwaddr address)
{
    if (address >= DST_BRES_ERR && address <= DST_BRES_DEC) {
        return &s->regs.bres[(address - DST_BRES_ERR) / 4];
    }
    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
        return NULL;
    }
    if (address >= LEAD_BRES_ERR && address <= LEAD_BRES_DEC) {
        return &s->regs.bres[(address - LEAD_BRES_ERR) / 4];
    }
    if (address >= TRAIL_BRES_ERR && address <= TRAIL_X) {
        return &s->regs.trail[(address - TRAIL_BRES_ERR) / 4];
    }
    if (address >= SCALE_SRC_HEIGHT_WIDTH &&
        address <= SCALE_DST_HEIGHT_WIDTH) {
        return &s->regs.scale[(address - SCALE_SRC_HEIGHT_WIDTH) / 4];
    }
    if (address == SCALE_3D_CNTL) {
        return &s->regs.scale_3d_cntl;
    }
    if (address == SCALE_3D_DATATYPE) {
        return &s->regs.scale_3d_datatype;
    }
    return NULL;
}

bool ati_2d_reg_read(ATIVGAState *s, hwaddr addr, uint64_t *value,
                     unsigned int size)
{
    uint32_t *reg = ati_2d_extra_register(s, addr & ~3U);

    if (!reg || (addr & 3) + size > 4) {
        return false;
    }
    *value = extract32(*reg, (addr & 3) * 8, size * 8);
    return true;
}

bool ati_2d_reg_write(ATIVGAState *s, hwaddr addr, uint64_t value,
                      unsigned int size)
{
    uint32_t *reg = ati_2d_extra_register(s, addr & ~3U);

    if (reg && (addr & 3) + size <= 4) {
        *reg = deposit32(*reg, (addr & 3) * 8, size * 8, value);
        if (addr >= SCALE_DST_HEIGHT_WIDTH && addr + size ==
            SCALE_DST_HEIGHT_WIDTH + 4) {
            ati_2d_stretch(s);
        }
        return true;
    }
    if (size != 4) {
        return false;
    }
    if (addr == DST_BRES_LNTH || addr == DST_BRES_LNTH_SUB) {
        /* Subpixel commands use integer coordinates and lengths. */
        ati_2d_bresenham(s, (value >> (addr == DST_BRES_LNTH_SUB ? 4 : 0)) &
                            0x3fff);
        return true;
    }
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        if (addr == LEAD_BRES_LNTH || addr == LEAD_BRES_LNTH_SUB) {
            ati_2d_trapezoid(s, (value >>
                                (addr == LEAD_BRES_LNTH_SUB ? 4 : 0)) &
                                0x3fff);
            return true;
        }
        if (addr == DST_X_SUB || addr == DST_Y_SUB || addr == TRAIL_X_SUB) {
            uint32_t *coord = addr == DST_X_SUB ? &s->regs.dst_x :
                              addr == DST_Y_SUB ? &s->regs.dst_y :
                              &s->regs.trail[3];

            *coord = (value >> 4) & 0x3fff;
            return true;
        }
    }
    return false;
}
