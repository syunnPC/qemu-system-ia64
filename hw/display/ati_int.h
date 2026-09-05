/*
 * QEMU ATI SVGA emulation
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#ifndef ATI_INT_H
#define ATI_INT_H

#include "qemu/timer.h"
#include "qemu/units.h"
#include "hw/pci/pci_device.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/display/i2c-ddc.h"
#include "migration/vmstate.h"
#include "vga_int.h"
#include "qom/object.h"

/*#define DEBUG_ATI*/

#ifdef DEBUG_ATI
#define DPRINTF(fmt, ...) printf("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define PCI_VENDOR_ID_ATI 0x1002
/* Rage128 Pro GL */
#define PCI_DEVICE_ID_ATI_RAGE128_PF 0x5046
/* Radeon RV100 (VE) */
#define PCI_DEVICE_ID_ATI_RADEON_QY 0x5159
/* Radeon RN50 / ES1000 */
#define PCI_DEVICE_ID_ATI_ES1000 0x515e

#define ATI_RAGE128_LINEAR_APER_SIZE (64 * MiB)
#define ATI_R100_LINEAR_APER_SIZE (128 * MiB)
#define ATI_RAGE128_MMIO_SIZE (16 * KiB)
#define ATI_R100_MMIO_SIZE (64 * KiB)
#define ATI_HOST_DATA_BANK_DWORDS 4
#define ATI_2D_MAX_PIXELS (16U * 1024U * 1024U)
#define ATI_3D_CONTEXT_DWORDS 128
#define ATI_3D_FOG_TABLE_ENTRIES 256
#define ATI_3D_MAX_VERTEX_DWORDS 4096
#define ATI_3D_MAX_VERTEX_ARRAYS 12
#define ATI_CURSOR_MAX_BYTES (64 * 64 * 4)

#define TYPE_ATI_VGA "ati-vga"
OBJECT_DECLARE_SIMPLE_TYPE(ATIVGAState, ATI_VGA)

#define ATI_PLL_REG_COUNT 64

typedef struct ATIVGARegs {
    uint32_t mm_index;
    uint32_t clock_cntl_index;
    uint32_t pll[ATI_PLL_REG_COUNT];
    uint32_t bios_scratch[8];
    uint32_t gen_int_cntl;
    uint32_t gen_int_status;
    uint32_t crtc_gen_cntl;
    uint32_t crtc_ext_cntl;
    uint32_t dac_cntl;
    uint32_t dac_ext_cntl;
    uint32_t dac_macro_cntl;
    uint32_t gpio_vga_ddc;
    uint32_t gpio_dvi_ddc;
    uint32_t gpio_monid;
    uint32_t config_cntl;
    uint32_t palette[256];
    uint32_t crtc_h_total_disp;
    uint32_t crtc_h_sync_strt_wid;
    uint32_t crtc_v_total_disp;
    uint32_t crtc_v_sync_strt_wid;
    uint32_t crtc_offset;
    uint32_t crtc_offset_cntl;
    uint32_t crtc_pitch;
    uint32_t cur_offset;
    uint32_t cur_hv_pos;
    uint32_t cur_hv_offs;
    uint32_t cur_color0;
    uint32_t cur_color1;
    uint32_t dst_offset;
    uint32_t dst_pitch;
    uint32_t dst_tile;
    uint32_t dst_width;
    uint32_t dst_height;
    uint32_t src_offset;
    uint32_t src_pitch;
    uint32_t src_tile;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t dst_x;
    uint32_t dst_y;
    uint32_t dp_gui_master_cntl;
    uint32_t brush_y_x;
    uint32_t brush_data[64];
    uint32_t dp_brush_bkgd_clr;
    uint32_t dp_brush_frgd_clr;
    uint32_t dp_src_frgd_clr;
    uint32_t dp_src_bkgd_clr;
    uint16_t sc_top;
    uint16_t sc_left;
    uint16_t sc_bottom;
    uint16_t sc_right;
    uint16_t src_sc_bottom;
    uint16_t src_sc_right;
    uint32_t dp_cntl;
    uint32_t dp_datatype;
    uint32_t dp_mix;
    uint32_t dp_write_mask;
    uint32_t clr_cmp_cntl;
    uint32_t clr_cmp_clr_src;
    uint32_t clr_cmp_clr_dst;
    uint32_t clr_cmp_mask;
    uint32_t rbbm_guicntl;
    uint32_t default_offset;
    uint32_t default_pitch;
    uint16_t default_sc_bottom;
    uint16_t default_sc_right;
    uint32_t default_tile;
} ATIVGARegs;

typedef struct ATIHostDataState {
    bool active;
    uint32_t row;
    uint32_t col;
    uint32_t next;
    uint32_t acc[ATI_HOST_DATA_BANK_DWORDS];
    uint8_t pending[3];
    uint8_t pending_count;
} ATIHostDataState;

typedef struct ATICursorState {
    uint32_t offset;
    uint32_t hv_pos;
    uint32_t hv_offs;
} ATICursorState;

typedef struct ATI3DVertexArray {
    uint32_t address;
    uint16_t stride;
    uint8_t components;
    uint8_t reserved;
} ATI3DVertexArray;

typedef struct ATI3DState {
    /* R100 context registers from 0x1c00 through 0x1dff. */
    uint32_t context[ATI_3D_CONTEXT_DWORDS];
    uint32_t re_top_left;
    uint32_t re_misc;
    uint32_t se_vtx_fmt;
    uint32_t se_vf_cntl;
    uint32_t se_cntl_status;
    uint8_t fog_table[ATI_3D_FOG_TABLE_ENTRIES];
    uint8_t fog_table_index;
    /* CP scaler palette, independent of the display DAC palette. */
    uint32_t scaler_palette[256];
    uint8_t scaler_palette_format;
    bool scaler_palette_valid;

    uint32_t cp_rb_base;
    uint32_t cp_rb_cntl;
    uint32_t cp_rb_rptr_addr;
    uint32_t cp_rb_rptr;
    uint32_t cp_rb_rptr_wr;
    uint32_t cp_rb_wptr;
    uint32_t cp_ib_base;
    uint32_t cp_ib_bufsz;
    uint32_t cp_csq_cntl;
    uint32_t cp_csq_mode;
    uint32_t scratch[8];
    uint32_t scratch_umsk;
    uint32_t scratch_addr;
    uint32_t me_ram_addr;
    uint32_t me_ram_raddr;
    uint32_t me_datah;
    uint32_t me_datal;

    uint32_t mc_fb_location;
    uint32_t mc_agp_location;
    uint32_t agp_base;
    uint32_t aic_cntl;
    uint32_t aic_pt_base;
    uint32_t aic_lo_addr;
    uint32_t aic_hi_addr;

    ATI3DVertexArray vertex_array[ATI_3D_MAX_VERTEX_ARRAYS];
    uint32_t vertex_array_count;
    uint32_t port_data[ATI_3D_MAX_VERTEX_DWORDS];
    uint32_t port_data_count;
    uint32_t port_data_expected;
    uint64_t submitted_primitives;
    uint64_t rejected_commands;

    /* Synchronous command processing never crosses a migration boundary. */
    uint64_t draw_budget_remaining;
    uint64_t command_work_remaining;
    uint64_t blit_work_remaining;
    uint8_t processing_depth;
    bool command_budget_exhausted;
} ATI3DState;

struct ATIVGAState {
    PCIDevice dev;
    VGACommonState vga;
    char *model;
    uint16_t dev_id;
    uint8_t mode;
    uint8_t use_pixman;
    bool cursor_guest_mode;
    uint8_t cursor_width;
    uint8_t cursor_height;
    uint8_t cursor_mode;
    uint8_t cursor_x_offset;
    uint32_t cursor_offset;
    ATICursorState cursor_active;
    QEMUCursor *cursor;
    uint8_t cursor_image[ATI_CURSOR_MAX_BYTES];
    uint32_t cursor_image_offset;
    uint32_t cursor_image_size;
    uint16_t cursor_image_stride;
    uint8_t cursor_image_x_offset;
    uint8_t cursor_image_width;
    uint8_t cursor_image_height;
    uint8_t cursor_image_mode;
    bool cursor_image_valid;
    bool cursor_host_visible;
    int cursor_host_x;
    int cursor_host_y;
    QEMUTimer vblank_timer;
    int64_t crtc_frame_start_ns;
    int64_t crtc_frame_elapsed_ns;
    uint32_t crtc_frame;
    uint16_t crtc_event_line;
    uint16_t crtc_vline;
    bool crtc_vblank_save;
    bool crtc_fix_vsync_timing;
    uint32_t crtc_offset_active;
    uint32_t crtc_pitch_active;
    bitbang_i2c_interface bbi2c;
    I2CDDCState i2cddc;
    uint64_t linear_aper_sz;
    MemoryRegion linear_aper;
    MemoryRegion io;
    MemoryRegion mm;
    ATIVGARegs regs;
    ATIHostDataState host_data;
    ATI3DState r100_3d;
    bool default_rom;
};

static inline bool ati_is_rv100_family(const ATIVGAState *s)
{
    return s->dev_id == PCI_DEVICE_ID_ATI_RADEON_QY ||
           s->dev_id == PCI_DEVICE_ID_ATI_ES1000;
}

static inline bool ati_has_rv100_3d(const ATIVGAState *s)
{
    return s->dev_id == PCI_DEVICE_ID_ATI_RADEON_QY;
}

const char *ati_reg_name(int num);

void ati_2d_blt(ATIVGAState *s);
bool ati_host_data_write(ATIVGAState *s, uint32_t data, bool last);
void ati_host_data_finish(ATIVGAState *s);
bool ati_3d_read(ATIVGAState *s, hwaddr addr, uint64_t *data,
                 unsigned int size);
bool ati_3d_write(ATIVGAState *s, hwaddr addr, uint64_t data,
                  unsigned int size);
void ati_3d_reset(ATIVGAState *s);
int ati_3d_post_load(ATIVGAState *s);
bool ati_3d_consume_command_work(ATIVGAState *s, uint64_t work);
bool ati_3d_consume_2d_work(ATIVGAState *s, uint64_t work);
bool ati_r100_gpu_vram_offset(ATIVGAState *s, uint64_t address,
                              uint64_t length, uint64_t *offset);
bool ati_r100_gpu_access_valid(ATIVGAState *s, uint64_t address,
                               uint64_t length, bool is_write);
bool ati_r100_gpu_ranges_overlap(ATIVGAState *s, uint64_t first,
                                 uint64_t first_length, uint64_t second,
                                 uint64_t second_length, bool *overlap);
bool ati_r100_gpu_read(ATIVGAState *s, uint64_t address, void *buf,
                       uint64_t length);
bool ati_r100_gpu_write(ATIVGAState *s, uint64_t address, const void *buf,
                        uint64_t length, bool dirty);
void ati_mmio_write(ATIVGAState *s, hwaddr addr, uint64_t data,
                    unsigned int size);
extern const VMStateDescription vmstate_ati_3d;

#endif /* ATI_INT_H */
