/*
 * ATI VGA register definitions
 *
 * based on:
 * linux/include/video/aty128.h
 *     Register definitions for ATI Rage128 boards
 *     Anthony Tong <atong@uiuc.edu>, 1999
 *     Brad Douglas <brad@neruo.com>, 2000
 *
 * and linux/include/video/radeon.h
 *
 * This work is licensed under the GNU GPL license version 2.
 */

/*
 * The R100 register names and values added below are derived in part from
 * Linux drivers/gpu/drm/radeon/radeon_reg.h.  The following upstream notice
 * applies to those definitions:
 *
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * ATI, VA LINUX SYSTEMS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Register mapping:
 * 0x0000-0x00ff Misc regs also accessible via io and mmio space
 * 0x0100-0x0eff Misc regs only accessible via mmio
 * 0x0f00-0x0fff Read-only copy of PCI config regs
 * 0x1000-0x13ff Concurrent Command Engine (CCE) regs
 * 0x1400-0x1fff GUI (drawing engine) regs
 */

#ifndef ATI_REGS_H
#define ATI_REGS_H

#undef DEFAULT_PITCH /* needed for mingw builds */

#define MM_INDEX                                0x0000
#define MM_DATA                                 0x0004
#define CLOCK_CNTL_INDEX                        0x0008
#define CLOCK_CNTL_DATA                         0x000c
#define BIOS_0_SCRATCH                          0x0010
#define BUS_CNTL                                0x0030
#define BUS_CNTL1                               0x0034
#define GEN_INT_CNTL                            0x0040
#define GEN_INT_STATUS                          0x0044
#define CRTC_GEN_CNTL                           0x0050
#define CRTC_EXT_CNTL                           0x0054
#define DAC_CNTL                                0x0058
#define DAC_EXT_CNTL                            0x0280
#define DAC_MACRO_CNTL                          0x0d04
#define CRTC_STATUS                             0x005c
#define GPIO_VGA_DDC                            0x0060
#define GPIO_DVI_DDC                            0x0064
#define GPIO_MONID                              0x0068
#define I2C_CNTL_1                              0x0094
#define AMCGPIO_MASK_MIR                        0x009c
#define AMCGPIO_A_MIR                           0x00a0
#define AMCGPIO_Y_MIR                           0x00a4
#define AMCGPIO_EN_MIR                          0x00a8
#define PALETTE_INDEX                           0x00b0
#define PALETTE_DATA                            0x00b4
#define PALETTE_30_DATA                         0x00b8
#define CNFG_CNTL                               0x00e0
#define GEN_RESET_CNTL                          0x00f0
#define CNFG_MEMSIZE                            0x00f8
#define CONFIG_APER_0_BASE                      0x0100
#define CONFIG_APER_1_BASE                      0x0104
#define CONFIG_APER_SIZE                        0x0108
#define CONFIG_REG_1_BASE                       0x010c
#define CONFIG_REG_APER_SIZE                    0x0110
#define HOST_PATH_CNTL                          0x0130
#define MEM_CNTL                                0x0140
#define MC_FB_LOCATION                          0x0148
#define MC_AGP_LOCATION                         0x014C
#define MC_STATUS                               0x0150
#define MEM_SDRAM_MODE_REG                      0x0158
#define MEM_POWER_MISC                          0x015c
#define AGP_BASE                                0x0170
#define AGP_CNTL                                0x0174
#define AGP_APER_OFFSET                         0x0178
#define PCI_GART_PAGE                           0x017c
#define PC_NGUI_MODE                            0x0180
#define PC_NGUI_CTLSTAT                         0x0184
#define MPP_TB_CONFIG                           0x01C0
#define MPP_GP_CONFIG                           0x01C8
#define VIPH_CONTROL                            0x01D0
#define CRTC_H_TOTAL_DISP                       0x0200
#define CRTC_H_SYNC_STRT_WID                    0x0204
#define CRTC_V_TOTAL_DISP                       0x0208
#define CRTC_V_SYNC_STRT_WID                    0x020c
#define CRTC_VLINE_CRNT_VLINE                   0x0210
#define CRTC_CRNT_FRAME                         0x0214
#define CRTC_GUI_TRIG_VLINE                     0x0218
#define CRTC_OFFSET                             0x0224
#define CRTC_OFFSET_GUI_TRIG_OFFSET             BIT(30)
#define CRTC_OFFSET_LOCK                        BIT(31)
#define CRTC_OFFSET_MASK                        0x07fffff8U
#define CRTC_OFFSET_CNTL                        0x0228
#define CRTC_PITCH                              0x022c
#define OVR_CLR                                 0x0230
#define OVR_WID_LEFT_RIGHT                      0x0234
#define OVR_WID_TOP_BOTTOM                      0x0238
#define CUR_OFFSET                              0x0260
#define CUR_HORZ_VERT_POSN                      0x0264
#define CUR_HORZ_VERT_OFF                       0x0268
#define CUR_CLR0                                0x026c
#define CUR_CLR1                                0x0270
#define LVDS_GEN_CNTL                           0x02d0
#define DDA_CONFIG                              0x02e0
#define DDA_ON_OFF                              0x02e4
#define VGA_DDA_CONFIG                          0x02e8
#define VGA_DDA_ON_OFF                          0x02ec
#define CRTC2_H_TOTAL_DISP                      0x0300
#define CRTC2_H_SYNC_STRT_WID                   0x0304
#define CRTC2_V_TOTAL_DISP                      0x0308
#define CRTC2_V_SYNC_STRT_WID                   0x030c
#define CRTC2_VLINE_CRNT_VLINE                  0x0310
#define CRTC2_CRNT_FRAME                        0x0314
#define CRTC2_GUI_TRIG_VLINE                    0x0318
#define CRTC2_OFFSET                            0x0324
#define CRTC2_OFFSET_CNTL                       0x0328
#define CRTC2_PITCH                             0x032c
#define DDA2_CONFIG                             0x03e0
#define DDA2_ON_OFF                             0x03e4
#define CRTC2_GEN_CNTL                          0x03f8
#define CRTC2_STATUS                            0x03fc
#define OV0_SCALE_CNTL                          0x0420
#define SUBPIC_CNTL                             0x0540
#define PM4_BUFFER_OFFSET                       0x0700
#define PM4_BUFFER_CNTL                         0x0704
#define PM4_BUFFER_WM_CNTL                      0x0708
#define PM4_BUFFER_DL_RPTR_ADDR                 0x070c
#define PM4_BUFFER_DL_RPTR                      0x0710
#define PM4_BUFFER_DL_WPTR                      0x0714
#define PM4_VC_FPU_SETUP                        0x071c
#define PM4_FPU_CNTL                            0x0720
#define PM4_VC_FORMAT                           0x0724
#define PM4_VC_CNTL                             0x0728
#define PM4_VC_I01                              0x072c
#define PM4_VC_VLOFF                            0x0730
#define PM4_VC_VLSIZE                           0x0734
#define PM4_IW_INDOFF                           0x0738
#define PM4_IW_INDSIZE                          0x073c
#define PM4_FPU_FPX0                            0x0740
#define PM4_FPU_FPY0                            0x0744
#define PM4_FPU_FPX1                            0x0748
#define PM4_FPU_FPY1                            0x074c
#define PM4_FPU_FPX2                            0x0750
#define PM4_FPU_FPY2                            0x0754
#define PM4_FPU_FPY3                            0x0758
#define PM4_FPU_FPY4                            0x075c
#define PM4_FPU_FPY5                            0x0760
#define PM4_FPU_FPY6                            0x0764
#define PM4_FPU_FPR                             0x0768
#define PM4_FPU_FPG                             0x076c
#define PM4_FPU_FPB                             0x0770
#define PM4_FPU_FPA                             0x0774
#define PM4_FPU_INTXY0                          0x0780
#define PM4_FPU_INTXY1                          0x0784
#define PM4_FPU_INTXY2                          0x0788
#define PM4_FPU_INTARGB                         0x078c
#define PM4_FPU_FPTWICEAREA                     0x0790
#define PM4_FPU_DMAJOR01                        0x0794
#define PM4_FPU_DMAJOR12                        0x0798
#define PM4_FPU_DMAJOR02                        0x079c
#define PM4_FPU_STAT                            0x07a0
#define PM4_STAT                                0x07b8
#define PM4_TEST_CNTL                           0x07d0
#define PM4_MICROCODE_ADDR                      0x07d4
#define PM4_MICROCODE_RADDR                     0x07d8
#define PM4_MICROCODE_DATAH                     0x07dc
#define PM4_MICROCODE_DATAL                     0x07e0
#define PM4_CMDFIFO_ADDR                        0x07e4
#define PM4_CMDFIFO_DATAH                       0x07e8
#define PM4_CMDFIFO_DATAL                       0x07ec
#define PM4_BUFFER_ADDR                         0x07f0
#define PM4_BUFFER_DATAH                        0x07f4
#define PM4_BUFFER_DATAL                        0x07f8
#define PM4_MICRO_CNTL                          0x07fc
#define R100_CP_RB_BASE                         0x0700
#define R100_CP_RB_CNTL                         0x0704
#define R100_CP_RB_RPTR_ADDR                    0x070c
#define R100_CP_RB_RPTR                         0x0710
#define R100_CP_RB_WPTR                         0x0714
#define R100_CP_RB_RPTR_WR                      0x071c
#define R100_CP_IB_BASE                         0x0738
#define R100_CP_IB_BUFSZ                        0x073c
#define R100_CP_CSQ_CNTL                        0x0740
#define R100_CP_CSQ_MODE                        0x0744
#define R100_CSQ_MODE_MASK                      (0xfU << 28)
#define R100_CSQ_PRIPIO_INDDIS                  (1U << 28)
#define R100_CSQ_PRIBM_INDDIS                   (2U << 28)
#define R100_CSQ_PRIPIO_INDBM                   (3U << 28)
#define R100_CSQ_PRIBM_INDBM                    (4U << 28)
#define R100_SCRATCH_UMSK                       0x0770
#define R100_SCRATCH_ADDR                       0x0774
#define R100_CP_ME_RAM_ADDR                     0x07d4
#define R100_CP_ME_RAM_RADDR                    0x07d8
#define R100_CP_ME_RAM_DATAH                    0x07dc
#define R100_CP_ME_RAM_DATAL                    0x07e0
#define R100_CP_STAT                            0x07c0
#define CAP0_TRIG_CNTL                          0x0950
#define CAP1_TRIG_CNTL                          0x09c0

#define RBBM_STATUS                             0x0e40

/*
 * GUI Block Memory Mapped Registers
 * These registers are FIFOed.
 */
#define PM4_FIFO_DATA_EVEN                      0x1000
#define PM4_FIFO_DATA_ODD                       0x1004

#define DST_OFFSET                              0x1404
#define DST_PITCH                               0x1408
#define DST_WIDTH                               0x140c
#define DST_HEIGHT                              0x1410
#define SRC_X                                   0x1414
#define SRC_Y                                   0x1418
#define DST_X                                   0x141c
#define DST_Y                                   0x1420
#define SRC_PITCH_OFFSET                        0x1428
#define DST_PITCH_OFFSET                        0x142c
#define SRC_Y_X                                 0x1434
#define DST_Y_X                                 0x1438
#define DST_HEIGHT_WIDTH                        0x143c
#define DP_GUI_MASTER_CNTL                      0x146c
#define BRUSH_SCALE                             0x1470
#define BRUSH_Y_X                               0x1474
#define DP_BRUSH_BKGD_CLR                       0x1478
#define DP_BRUSH_FRGD_CLR                       0x147c
#define BRUSH_DATA0                             0x1480
#define BRUSH_DATA63                            0x157c
#define DST_WIDTH_X                             0x1588
#define DST_HEIGHT_WIDTH_8                      0x158c
#define SRC_X_Y                                 0x1590
#define DST_X_Y                                 0x1594
#define DST_WIDTH_HEIGHT                        0x1598
#define DST_WIDTH_X_INCY                        0x159c
#define DST_HEIGHT_Y                            0x15a0
#define DST_X_SUB                               0x15a4
#define DST_Y_SUB                               0x15a8
#define SRC_OFFSET                              0x15ac
#define SRC_PITCH                               0x15b0
#define DST_HEIGHT_WIDTH_BW                     0x15b4
#define CLR_CMP_CNTL                            0x15c0
#define CLR_CMP_CLR_SRC                         0x15c4
#define CLR_CMP_CLR_DST                         0x15c8
#define CLR_CMP_MASK                            0x15cc
#define DP_SRC_FRGD_CLR                         0x15d8
#define DP_SRC_BKGD_CLR                         0x15dc
#define DST_BRES_ERR                            0x1628
#define DST_BRES_INC                            0x162c
#define DST_BRES_DEC                            0x1630
#define DST_BRES_LNTH                           0x1634
#define DST_BRES_LNTH_SUB                       0x1638
#define SC_LEFT                                 0x1640
#define SC_RIGHT                                0x1644
#define SC_TOP                                  0x1648
#define SC_BOTTOM                               0x164c
#define SRC_SC_RIGHT                            0x1654
#define SRC_SC_BOTTOM                           0x165c
#define GUI_DEBUG0                              0x16a0
#define GUI_DEBUG1                              0x16a4
#define GUI_TIMEOUT                             0x16b0
#define GUI_TIMEOUT0                            0x16b4
#define GUI_TIMEOUT1                            0x16b8
#define GUI_PROBE                               0x16bc
#define DP_CNTL                                 0x16c0
#define DP_DATATYPE                             0x16c4
#define DP_MIX                                  0x16c8
#define DP_WRITE_MASK                           0x16cc
#define DP_CNTL_XDIR_YDIR_YMAJOR                0x16d0
#define DEFAULT_OFFSET                          0x16e0
#define DEFAULT_PITCH                           0x16e4
#define DEFAULT_SC_BOTTOM_RIGHT                 0x16e8
#define SC_TOP_LEFT                             0x16ec
#define SC_BOTTOM_RIGHT                         0x16f0
#define SRC_SC_BOTTOM_RIGHT                     0x16f4
#define DST_TILE                                0x1700
#define WAIT_UNTIL                              0x1720
#define CACHE_CNTL                              0x1724
#define RBBM_GUICNTL                           0x172c
#define GUI_STAT                                0x1740
#define PC_GUI_MODE                             0x1744
#define PC_GUI_CTLSTAT                          0x1748
#define PC_DEBUG_MODE                           0x1760
#define BRES_DST_ERR_DEC                        0x1780
#define TRAIL_BRES_T12_ERR_DEC                  0x1784
#define TRAIL_BRES_T12_INC                      0x1788
#define DP_T12_CNTL                             0x178c
#define DST_BRES_T1_LNTH                        0x1790
#define DST_BRES_T2_LNTH                        0x1794
#define HOST_DATA0                              0x17c0
#define HOST_DATA1                              0x17c4
#define HOST_DATA2                              0x17c8
#define HOST_DATA3                              0x17cc
#define HOST_DATA4                              0x17d0
#define HOST_DATA5                              0x17d4
#define HOST_DATA6                              0x17d8
#define HOST_DATA7                              0x17dc
#define HOST_DATA_LAST                          0x17e0
#define R100_SCRATCH_REG0                       0x15e0
#define R100_SCRATCH_REG7                       0x15fc
#define SCALE_SRC_HEIGHT_WIDTH                  0x1994
#define SCALE_OFFSET_0                          0x1998
#define SCALE_PITCH                             0x199c
#define SCALE_X_INC                             0x19a0
#define SCALE_Y_INC                             0x19a4
#define SCALE_HACC                              0x19a8
#define SCALE_VACC                              0x19ac
#define SCALE_DST_X_Y                           0x19b0
#define SCALE_DST_HEIGHT_WIDTH                  0x19b4
#define SCALE_3D_CNTL                           0x1a00
#define SCALE_3D_DATATYPE                       0x1a20
#define SETUP_CNTL                              0x1bc4
#define SOLID_COLOR                             0x1bc8
#define WINDOW_XY_OFFSET                        0x1bcc
#define DRAW_LINE_POINT                         0x1bd0
#define SETUP_CNTL_PM4                          0x1bd4
#define DST_PITCH_OFFSET_C                      0x1c80
#define DP_GUI_MASTER_CNTL_C                    0x1c84
#define SC_TOP_LEFT_C                           0x1c88
#define SC_BOTTOM_RIGHT_C                       0x1c8c

#define CLR_CMP_MASK_3D                         0x1A28
#define MISC_3D_STATE_CNTL_REG                  0x1CA0
#define MC_SRC1_CNTL                            0x19D8
#define TEX_CNTL                                0x1800

/* R100 fixed-function 3D and command processor register definitions. */
#define R100_AIC_CNTL                           0x01d0
#define R100_AIC_PT_BASE                        0x01d8
#define R100_AIC_LO_ADDR                        0x01dc
#define R100_AIC_HI_ADDR                        0x01e0
#define R100_FOG_TABLE_INDEX                    0x1a14
#define R100_FOG_TABLE_DATA                     0x1a18
#define R100_PP_MISC                            0x1c14
#define R100_PP_FOG_COLOR                       0x1c18
#define R100_RE_SOLID_COLOR                     0x1c1c
#define R100_RB3D_BLENDCNTL                     0x1c20
#define R100_RB3D_DEPTHOFFSET                   0x1c24
#define R100_RB3D_DEPTHPITCH                    0x1c28
#define R100_RB3D_ZSTENCILCNTL                  0x1c2c
#define R100_PP_CNTL                            0x1c38
#define R100_RB3D_CNTL                          0x1c3c
#define R100_RB3D_COLOROFFSET                   0x1c40
#define R100_RE_WIDTH_HEIGHT                    0x1c44
#define R100_RB3D_COLORPITCH                    0x1c48
#define R100_SE_CNTL                            0x1c4c
#define R100_SE_COORD_FMT                       0x1c50
#define R100_PP_TXFILTER_0                      0x1c54
#define R100_PP_TXFORMAT_0                      0x1c58
#define R100_PP_TXOFFSET_0                      0x1c5c
#define R100_PP_TXCBLEND_0                      0x1c60
#define R100_PP_TXABLEND_0                      0x1c64
#define R100_PP_TFACTOR_0                       0x1c68
#define R100_PP_TXFILTER_1                      0x1c6c
#define R100_PP_TXFORMAT_1                      0x1c70
#define R100_PP_TXOFFSET_1                      0x1c74
#define R100_PP_TXCBLEND_1                      0x1c78
#define R100_PP_TXABLEND_1                      0x1c7c
#define R100_PP_TFACTOR_1                       0x1c80
#define R100_PP_TXFILTER_2                      0x1c84
#define R100_PP_TXFORMAT_2                      0x1c88
#define R100_PP_TXOFFSET_2                      0x1c8c
#define R100_PP_TXCBLEND_2                      0x1c90
#define R100_PP_TXABLEND_2                      0x1c94
#define R100_PP_TFACTOR_2                       0x1c98
#define R100_PP_TEX_SIZE_0                      0x1d04
#define R100_PP_TEX_PITCH_0                     0x1d08
#define R100_PP_TEX_SIZE_1                      0x1d0c
#define R100_PP_TEX_PITCH_1                     0x1d10
#define R100_PP_TEX_SIZE_2                      0x1d14
#define R100_PP_TEX_PITCH_2                     0x1d18
#define R100_PP_BORDER_COLOR_0                  0x1d40
#define R100_PP_BORDER_COLOR_1                  0x1d44
#define R100_PP_BORDER_COLOR_2                  0x1d48
#define R100_RB3D_STENCILREFMASK                0x1d7c
#define R100_RB3D_ROPCNTL                       0x1d80
#define R100_RB3D_PLANEMASK                     0x1d84
#define R100_SE_VPORT_XSCALE                    0x1d98
#define R100_SE_VPORT_XOFFSET                   0x1d9c
#define R100_SE_VPORT_YSCALE                    0x1da0
#define R100_SE_VPORT_YOFFSET                   0x1da4
#define R100_SE_VPORT_ZSCALE                    0x1da8
#define R100_SE_VPORT_ZOFFSET                   0x1dac
#define R100_SE_PORT_DATA0                      0x2000
#define R100_SE_PORT_DATA_END                   0x207c
#define R100_SE_VTX_FMT                         0x2080
#define R100_SE_VF_CNTL                         0x2084
#define R100_SE_CNTL_STATUS                     0x2140
#define R100_RE_TOP_LEFT                        0x26c0
#define R100_RE_MISC                            0x26c4

#define R100_PP_TEX_0_ENABLE                    BIT(4)
#define R100_PP_TEX_1_ENABLE                    BIT(5)
#define R100_PP_TEX_2_ENABLE                    BIT(6)
#define R100_PP_TEX_BLEND_0_ENABLE              BIT(12)
#define R100_PP_TEX_BLEND_1_ENABLE              BIT(13)
#define R100_PP_TEX_BLEND_2_ENABLE              BIT(14)
#define R100_PP_SPECULAR_ENABLE                 BIT(21)
#define R100_PP_FOG_ENABLE                      BIT(22)
#define R100_PP_ALPHA_TEST_ENABLE               BIT(23)
#define R100_FOG_COLOR_MASK                     0x00ffffffU
#define R100_FOG_TABLE                          BIT(24)
#define R100_FOG_SOURCE_MASK                    (3U << 25)
#define R100_FOG_USE_DEPTH                      (0U << 25)
#define R100_FOG_USE_DIFFUSE_ALPHA              (2U << 25)
#define R100_FOG_USE_SPEC_ALPHA                 (3U << 25)
#define R100_RB3D_ALPHA_BLEND_ENABLE            BIT(0)
#define R100_RB3D_PLANE_MASK_ENABLE             BIT(1)
#define R100_RB3D_ROP_ENABLE                    BIT(6)
#define R100_RB3D_STENCIL_ENABLE                BIT(7)
#define R100_RB3D_Z_ENABLE                      BIT(8)
#define R100_RB3D_COLOR_FORMAT_SHIFT            10
#define R100_RB3D_COLOR_FORMAT_MASK             (0x1fU << 10)
#define R100_COLOR_TILE_ENABLE                   BIT(16)
#define R100_COLOR_MICROTILE_ENABLE              BIT(17)
#define R100_RB3D_Z_WRITE_ENABLE                BIT(30)
#define R100_DEPTH_FORMAT_MASK                  0xfU
#define R100_DEPTH_FORMAT_16BIT_INT_Z           0
#define R100_DEPTH_FORMAT_24BIT_INT_Z           2
#define R100_DEPTH_FORMAT_32BIT_INT_Z           4
#define R100_SE_FLAT_SHADE_VTX_SHIFT            6
#define R100_SE_DIFFUSE_SHADE_SHIFT             8
#define R100_SE_ALPHA_SHADE_SHIFT               10
#define R100_SE_SPECULAR_SHADE_SHIFT            12
#define R100_SE_FOG_SHADE_SHIFT                 14
#define R100_SE_SHADE_FLAT                      1
#define R100_SE_SHADE_GOURAUD                   2
#define R100_TXFORMAT_FORMAT_MASK               0x1fU
#define R100_TXFORMAT_VYUY422                   10U
#define R100_TXFORMAT_YVYU422                   11U
#define R100_TXFORMAT_DXT1                      12U
#define R100_TXFORMAT_DXT23                     14U
#define R100_TXFORMAT_DXT45                     15U
#define R100_TXFORMAT_NON_POWER2                BIT(7)
#define R100_TXFORMAT_ALPHA_IN_MAP              BIT(6)
#define R100_TXFORMAT_WIDTH_SHIFT               8
#define R100_TXFORMAT_HEIGHT_SHIFT              12
#define R100_TXFORMAT_ST_ROUTE_SHIFT             24
#define R100_TXFORMAT_ST_ROUTE_MASK              (3U << 24)
#define R100_TXFORMAT_PERSPECTIVE_ENABLE        BIT(31)
#define R100_TXO_MACRO_TILE                      BIT(2)
#define R100_TXO_MICRO_TILE_X2                   BIT(3)
#define R100_TXO_MICRO_TILE_OPT                  BIT(4)
#define R100_TXFILTER_MAG_LINEAR                BIT(0)
#define R100_TXFILTER_MIN_NEAREST               (0U << 1)
#define R100_TXFILTER_MIN_LINEAR                (1U << 1)
#define R100_TXFILTER_MIN_NEAREST_MIP_NEAREST   (2U << 1)
#define R100_TXFILTER_MIN_NEAREST_MIP_LINEAR    (3U << 1)
#define R100_TXFILTER_MIN_LINEAR_MIP_NEAREST    (6U << 1)
#define R100_TXFILTER_MIN_LINEAR_MIP_LINEAR     (7U << 1)
#define R100_TXFILTER_MIN_MASK                  (0xfU << 1)
#define R100_TXFILTER_LOD_BIAS_SHIFT            8
#define R100_TXFILTER_LOD_BIAS_MASK             (0xffU << 8)
#define R100_TXFILTER_MAX_MIP_LEVEL_SHIFT       16
#define R100_TXFILTER_YUV_TO_RGB                BIT(20)
#define R100_TXFILTER_MAX_MIP_LEVEL_MASK        (0xfU << 16)
#define R100_TXFILTER_WRAPEN_S                  BIT(22)
#define R100_TXFILTER_CLAMP_S_SHIFT             23
#define R100_TXFILTER_WRAPEN_T                  BIT(26)
#define R100_TXFILTER_CLAMP_T_SHIFT             27
#define R100_TXFILTER_CLAMP_WRAP                0
#define R100_TXFILTER_CLAMP_MIRROR              1
#define R100_TXFILTER_CLAMP_LAST                2
#define R100_TXFILTER_MIRROR_CLAMP_LAST         3
#define R100_TXFILTER_CLAMP_BORDER              4
#define R100_TXFILTER_MIRROR_CLAMP_BORDER       5
#define R100_TXFILTER_CLAMP_GL                  6
#define R100_TXFILTER_MIRROR_CLAMP_GL           7
#define R100_TXFILTER_BORDER_MODE_D3D            BIT(31)
#define R100_VTX_ST0_NONPARAMETRIC              BIT(8)
#define R100_VTX_ST_NONPARAMETRIC(unit)          BIT(8 + (unit))
#define R100_VPORT_XY_XFORM_ENABLE              BIT(24)
#define R100_VPORT_Z_XFORM_ENABLE               BIT(25)

#define R100_VTX_FMT_W0                         BIT(0)
#define R100_VTX_FMT_FPCOLOR                    BIT(1)
#define R100_VTX_FMT_FPALPHA                    BIT(2)
#define R100_VTX_FMT_PKCOLOR                    BIT(3)
#define R100_VTX_FMT_FPSPEC                     BIT(4)
#define R100_VTX_FMT_FPFOG                      BIT(5)
#define R100_VTX_FMT_PKSPEC                     BIT(6)
#define R100_VTX_FMT_ST0                        BIT(7)
#define R100_VTX_FMT_ST1                        BIT(8)
#define R100_VTX_FMT_Q1                         BIT(9)
#define R100_VTX_FMT_ST2                        BIT(10)
#define R100_VTX_FMT_Q2                         BIT(11)
#define R100_VTX_FMT_ST3                        BIT(12)
#define R100_VTX_FMT_Q3                         BIT(13)
#define R100_VTX_FMT_Q0                         BIT(14)
#define R100_VTX_FMT_N0                         BIT(18)
#define R100_VTX_FMT_XY1                        BIT(27)
#define R100_VTX_FMT_Z1                         BIT(28)
#define R100_VTX_FMT_W1                         BIT(29)
#define R100_VTX_FMT_N1                         BIT(30)
#define R100_VTX_FMT_Z                          BIT(31)

#define R100_VF_PRIM_TYPE_MASK                  0xfU
#define R100_VF_PRIM_POINT_LIST                 1
#define R100_VF_PRIM_LINE_LIST                  2
#define R100_VF_PRIM_LINE_STRIP                 3
#define R100_VF_PRIM_TRIANGLE_LIST              4
#define R100_VF_PRIM_TRIANGLE_FAN               5
#define R100_VF_PRIM_TRIANGLE_STRIP             6
#define R100_VF_PRIM_RECTANGLE_LIST             8
#define R100_VF_PRIM_QUAD_LIST                  13
#define R100_VF_PRIM_QUAD_STRIP                 14
#define R100_VF_PRIM_POLYGON                    15
#define R100_VF_PRIM_WALK_SHIFT                 4
#define R100_VF_PRIM_WALK_MASK                  (3U << 4)
#define R100_VF_PRIM_WALK_IND                   (1U << 4)
#define R100_VF_PRIM_WALK_LIST                  (2U << 4)
#define R100_VF_PRIM_WALK_DATA                  (3U << 4)
#define R100_VF_COLOR_ORDER_RGBA                BIT(6)
#define R100_VF_INDEX_SIZE_32                    BIT(11)
#define R100_VF_NUM_VERTICES_SHIFT              16

#define R100_COMBINER_ARG_A_SHIFT               0
#define R100_COMBINER_ARG_B_SHIFT               5
#define R100_COMBINER_ARG_C_SHIFT               10
#define R100_COMBINER_COMP_A                    BIT(15)
#define R100_COMBINER_COMP_B                    BIT(16)
#define R100_COMBINER_COMP_C                    BIT(17)
#define R100_COMBINER_OP_SHIFT                  18
#define R100_COMBINER_OP_ADD                    0
#define R100_COMBINER_OP_SUBTRACT               1
#define R100_COMBINER_OP_ADDSIGNED               2
#define R100_COMBINER_OP_BLEND                  3
#define R100_COMBINER_OP_DOT3                   4
#define R100_COMBINER_SCALE_SHIFT               21
#define R100_COMBINER_CLAMP                     BIT(23)

#define R100_ALPHA_ARG_A_SHIFT                  0
#define R100_ALPHA_ARG_B_SHIFT                  4
#define R100_ALPHA_ARG_C_SHIFT                  8

#define R100_STENCIL_TEST_SHIFT                 12
#define R100_STENCIL_FAIL_SHIFT                 16
#define R100_STENCIL_ZPASS_SHIFT                20
#define R100_STENCIL_ZFAIL_SHIFT                24
#define R100_STENCIL_REF_SHIFT                  0
#define R100_STENCIL_MASK_SHIFT                 16
#define R100_STENCIL_WRITEMASK_SHIFT            24

#define R100_CP_PACKET_TYPE_MASK                (3U << 30)
#define R100_CP_PACKET0                         (0U << 30)
#define R100_CP_PACKET1                         (1U << 30)
#define R100_CP_PACKET2                         (2U << 30)
#define R100_CP_PACKET3                         (3U << 30)
#define R100_CP_PACKET0_ONE_REG                 BIT(15)
#define R100_CP_PACKET_COUNT_SHIFT              16
#define R100_CP_PACKET_COUNT_MASK               (0x3fffU << 16)
#define R100_PACKET3_NOP                        0x10
#define R100_PACKET3_PLY_NEXTSCAN               0x1d
#define R100_PACKET3_SET_SCISSORS               0x1e
#define R100_PACKET3_3D_RNDR_GEN_INDX_PRIM      0x23
#define R100_PACKET3_3D_RNDR_GEN_PRIM           0x25
#define R100_PACKET3_WAIT_FOR_IDLE               0x26
#define R100_PACKET3_3D_DRAW_VBUF                0x28
#define R100_PACKET3_3D_DRAW_IMMD                0x29
#define R100_PACKET3_3D_DRAW_INDX                0x2a
#define R100_PACKET3_3D_LOAD_VBPNTR              0x2f
#define R100_PACKET3_3D_DRAW_VBUF_2              0x34
#define R100_PACKET3_3D_DRAW_IMMD_2              0x35
#define R100_PACKET3_3D_DRAW_INDX_2              0x36
#define R100_PACKET3_CNTL_BITBLT                  0x92
#define R100_PACKET3_CNTL_HOSTDATA_BLT            0x94
#define R100_PACKET3_CNTL_POLYSCANLINES           0x98
#define R100_PACKET3_LOAD_PALETTE                 0x2c
#define R100_PACKET3_NEXT_CHAR                    0x19
#define R100_PACKET3_CNTL_PAINT_MULTI             0x9a
#define R100_PACKET3_CNTL_BITBLT_MULTI            0x9b
#define R100_PACKET3_CNTL_TRANS_BITBLT            0x9c

#define R100_RB_BUFSZ_MASK                      0x3fU
#define R100_RB_BUF_SWAP_SHIFT                  16
#define R100_RB_BUF_SWAP_MASK                   (3U << 16)
#define R100_RB_NO_UPDATE                       BIT(27)
#define R100_RB_RPTR_WR_ENA                     BIT(31)
#define R100_RB_RPTR_SWAP_MASK                  3U
#define R100_SCRATCH_SWAP_SHIFT                 16
#define R100_VC_SWAP_MASK                       3U
#define R100_TCL_BYPASS                         BIT(8)
#define R100_SCRATCH_ADDR_MASK                  0xffffffe0U
#define R100_PCIGART_TRANSLATE_EN               BIT(0)
#define R100_DIS_OUT_OF_PCI_GART_ACCESS         BIT(1)

/* CONSTANTS */
#define GUI_ACTIVE                              0x80000000
#define ENGINE_IDLE                             0x0

#define PLL_WR_EN                               0x00000080
#define PLL_INDEX_MASK                          0x0000003f
#define PLL_DIV_SEL_MASK                        0x00000300
#define PLL_INDEX_CNTL_MASK                     \
    (PLL_INDEX_MASK | PLL_WR_EN | PLL_DIV_SEL_MASK)

#define CLK_PIN_CNTL                            0x01
#define PPLL_CNTL                               0x02
#define PPLL_REF_DIV                            0x03
#define PPLL_DIV_0                              0x04
#define PPLL_DIV_1                              0x05
#define PPLL_DIV_2                              0x06
#define PPLL_DIV_3                              0x07
#define VCLK_ECP_CNTL                           0x08
#define HTOTAL_CNTL                             0x09
#define X_MPLL_REF_FB_DIV                       0x0a
#define XPLL_CNTL                               0x0b
#define XDLL_CNTL                               0x0c
#define XCLK_CNTL                               0x0d
#define MPLL_CNTL                               0x0e
#define MCLK_CNTL                               0x0f
#define AGP_PLL_CNTL                            0x10
#define FCP_CNTL                                0x12
#define PLL_TEST_CNTL                           0x13
#define P2PLL_CNTL                              0x2a
#define P2PLL_REF_DIV                           0x2b
#define P2PLL_DIV_0                             0x2b
#define POWER_MANAGEMENT                        0x2f

/* Radeon R100/RV100 clock registers differ from the Rage128 aliases above. */
#define R100_M_SPLL_REF_FB_DIV                   0x0a
#define R100_SCLK_CNTL                           0x0d
#define R100_MCLK_CNTL                           0x12
#define R100_PLL_SRC_DIV2                        2
#define R100_YCLKA_SRC_SHIFT                     4

#define PPLL_RESET                              0x00000001
#define PPLL_ATOMIC_UPDATE_EN                   0x00010000
#define PPLL_VGA_ATOMIC_UPDATE_EN               0x00020000
#define PPLL_REF_DIV_MASK                       0x000003FF
#define PPLL_FB3_DIV_MASK                       0x000007FF
#define PPLL_POST3_DIV_MASK                     0x00070000
#define PPLL_ATOMIC_UPDATE_R                    0x00008000
#define PPLL_ATOMIC_UPDATE_W                    0x00008000
#define MEM_CFG_TYPE_MASK                       0x00000003
#define XCLK_SRC_SEL_MASK                       0x00000007
#define XPLL_FB_DIV_MASK                        0x0000FF00
#define X_MPLL_REF_DIV_MASK                     0x000000FF

/* GEN_INT_CNTL) */
#define CRTC_VBLANK_INT                         0x00000001
#define CRTC_VLINE_INT                          0x00000002
#define CRTC_VSYNC_INT                          0x00000004
#define CRTC_VBLANK_CUR                         BIT(0)
#define CRTC_VBLANK_SAVE                        BIT(1)
#define CRTC_VLINE_SYNC                         BIT(2)
#define CRTC_FRAME                              BIT(3)
#define CRTC_FIX_VSYNC_TIMING                   BIT(31)
#define SW_INT_ENABLE                           BIT(25)
#define SW_INT_TEST                             BIT(25)
#define SW_INT_FIRE                             BIT(26)

/* Config control values (CONFIG_CNTL) */
#define APER_0_ENDIAN                           0x00000003
#define APER_1_ENDIAN                           0x0000000c
#define CFG_VGA_IO_DIS                          0x00000400

/* CRTC control values (CRTC_GEN_CNTL) */
#define CRTC_CSYNC_EN                           0x00000010

#define CRTC2_DBL_SCAN_EN                       0x00000001
#define CRTC2_DISPLAY_DIS                       0x00800000
#define CRTC2_FIFO_EXTSENSE                     0x00200000
#define CRTC2_ICON_EN                           0x00100000
#define CRTC2_CUR_EN                            0x00010000
#define R100_CRTC_CUR_MODE_SHIFT                20
#define R100_CRTC_CUR_MODE_MASK                 (7U << R100_CRTC_CUR_MODE_SHIFT)
#define R100_CRTC_CUR_MODE_MONO                 0
#define R100_CRTC_CUR_MODE_24BPP                2
#define CRTC2_EXT_DISP_EN                       0x01000000
#define CRTC2_EN                                0x02000000
#define CRTC2_DISP_REQ_EN_B                     0x04000000

#define CRTC_PIX_WIDTH_MASK                     0x00000700
#define CRTC_PIX_WIDTH_4BPP                     0x00000100
#define CRTC_PIX_WIDTH_8BPP                     0x00000200
#define CRTC_PIX_WIDTH_15BPP                    0x00000300
#define CRTC_PIX_WIDTH_16BPP                    0x00000400
#define CRTC_PIX_WIDTH_24BPP                    0x00000500
#define CRTC_PIX_WIDTH_32BPP                    0x00000600

/* DAC_CNTL bit constants */
#define DAC_8BIT_EN                             0x00000100
#define DAC_MASK                                0xFF000000
#define DAC_BLANKING                            0x00000004
#define DAC_RANGE_CNTL                          0x00000003
#define DAC_CLK_SEL                             0x00000010
#define DAC_PALETTE_ACCESS_CNTL                 0x00000020
#define DAC_PALETTE2_SNOOP_EN                   0x00000040
#define DAC_PDWN                                0x00008000
#define R100_DAC_CMP_EN                         BIT(3)
#define R100_DAC_CMP_OUTPUT                     BIT(7)
#define R100_DAC_FORCE_BLANK_OFF_EN             BIT(4)
#define R100_DAC_FORCE_DATA_EN                  BIT(5)
#define R100_DAC_FORCE_DATA_SEL_MASK            (3U << 6)
#define R100_DAC_FORCE_DATA_MASK                (0x3ffU << 8)
#define R100_DAC_PDWN_R                         BIT(16)
#define R100_DAC_PDWN_G                         BIT(17)
#define R100_DAC_PDWN_B                         BIT(18)

/* CRTC_EXT_CNTL */
#define CRT_CRTC_DISPLAY_DIS                    0x00000400
#define CRT_CRTC_ON                             0x00008000

/* GEN_RESET_CNTL bit constants */
#define SOFT_RESET_GUI                          0x00000001
#define SOFT_RESET_VCLK                         0x00000100
#define SOFT_RESET_PCLK                         0x00000200
#define SOFT_RESET_ECP                          0x00000400
#define SOFT_RESET_DISPENG_XCLK                 0x00000800

/* PC_GUI_CTLSTAT bit constants */
#define PC_BUSY_INIT                            0x10000000
#define PC_BUSY_GUI                             0x20000000
#define PC_BUSY_NGUI                            0x40000000
#define PC_BUSY                                 0x80000000

#define BUS_MASTER_DIS                          0x00000040
#define PM4_BUFFER_CNTL_NONPM4                  0x00000000

/* DP_DATATYPE bit constants */
#define DST_8BPP                                0x00000002
#define DST_15BPP                               0x00000003
#define DST_16BPP                               0x00000004
#define DST_24BPP                               0x00000005
#define DST_32BPP                               0x00000006
#define DP_DST_DATATYPE                         0x0000000f
#define DP_BRUSH_DATATYPE                       0x00000f00
#define SRC_MONO_FRGD_BKGD                      0x00000000
#define SRC_MONO_FRGD                           0x00010000
#define SRC_COLOR                               0x00030000
#define DP_SRC_DATATYPE                         0x00030000
#define R100_DP_SRC_DATATYPE2                   BIT(18)
#define HOST_BIG_ENDIAN_EN                      0x20000000
#define DP_BYTE_PIX_ORDER                       0x40000000

/* RBBM_GUICNTL bit constants (Radeon only) */
#define HOST_DATA_SWAP_MASK                     0x00000003
#define HOST_DATA_SWAP_NONE                     0x00000000
#define HOST_DATA_SWAP_16BIT                    0x00000001
#define HOST_DATA_SWAP_32BIT                    0x00000002
#define HOST_DATA_SWAP_HDW                      0x00000003

#define BRUSH_8X8_MONO_FRGD_BKGD                0x00000000
#define BRUSH_8X8_MONO_FRGD_LEAVE               0x00000100
#define BRUSH_8X8_COLOR                         0x00000a00
#define BRUSH_SOLIDCOLOR                        0x00000d00
#define BRUSH_SOLIDCOLOR_LINE                   0x00000e00

/* BRUSH_Y_X fields */
#define R100_BRUSH_X_MASK                       0x00000007
#define R100_BRUSH_Y_MASK                       0x00000700
#define R100_BRUSH_Y_X_MASK                     (R100_BRUSH_X_MASK | \
                                                 R100_BRUSH_Y_MASK)

/* DP_GUI_MASTER_CNTL bit constants */
#define GMC_SRC_PITCH_OFFSET_CNTL               0x00000001
#define GMC_DST_PITCH_OFFSET_CNTL               0x00000002
#define GMC_SRC_CLIP_DEFAULT                    0x00000000
#define GMC_DST_CLIP_DEFAULT                    0x00000000
#define GMC_SRC_CLIPPING                        0x00000004
#define GMC_DST_CLIPPING                        0x00000008
#define GMC_BRUSH_DATATYPE_MASK                 0x000000f0
#define GMC_BRUSH_8X8_MONO_FRGD_BKGD            0x00000000
#define GMC_BRUSH_8X8_MONO_FRGD_LEAVE           0x00000010
#define GMC_BRUSH_8X8_COLOR                     0x000000a0
#define GMC_BRUSH_SOLIDCOLOR                    0x000000d0
#define GMC_BRUSH_SOLIDCOLOR_LINE               0x000000e0
#define GMC_BRUSH_NONE                          0x000000f0
#define GMC_SRC_DSTCOLOR                        0x00003000
#define GMC_BYTE_ORDER_MSB_TO_LSB               0x00000000
#define GMC_DP_SRC_SOURCE_MASK                  0x07000000
#define GMC_DP_SRC_RECT                         0x02000000
#define GMC_DP_SRC_HOST                         0x03000000
#define GMC_DP_SRC_HOST_BYTEALIGN               0x04000000
#define GMC_3D_FCN_EN_CLR                       0x00000000
#define GMC_3D_FCN_EN                           0x08000000
#define R100_GMC_SRC_DATATYPE2                  BIT(27)
#define GMC_AUX_CLIP_CLEAR                      0x20000000
#define GMC_DST_CLR_CMP_FCN_CLEAR               0x10000000
#define GMC_WRITE_MASK_SET                      0x40000000
#define GMC_LD_BRUSH_Y_X                        0x80000000
#define GMC_DP_CONVERSION_TEMP_6500             0x00000000

/* CLR_CMP_CNTL fields */
#define CLR_CMP_FN_SRC_MASK                     0x00000007
#define CLR_CMP_FN_DST_MASK                     0x00000700
#define CLR_CMP_FN_DST_SHIFT                    8
#define CLR_CMP_ENABLE_MASK                     0x03000000
#define CLR_CMP_ENABLE_SHIFT                    24
#define CLR_CMP_ENABLE_DST                      0
#define CLR_CMP_ENABLE_SRC                      1
#define CLR_CMP_ENABLE_BOTH                     2
#define CLR_CMP_FALSE                           0
#define CLR_CMP_TRUE                            1
#define CLR_CMP_EQUAL                           4
#define CLR_CMP_NOT_EQUAL                       5
#define CLR_CMP_EQUAL_FLIP                      7

/* DP_GUI_MASTER_CNTL ROP3 named constants */
#define GMC_ROP3_MASK                           0x00ff0000
#define ROP3_BLACKNESS                          0x00000000
#define ROP3_SRCCOPY                            0x00cc0000
#define ROP3_PATCOPY                            0x00f00000
#define ROP3_WHITENESS                          0x00ff0000

#define SRC_DSTCOLOR                            0x00030000

/* DP_CNTL bit constants */
#define DST_X_RIGHT_TO_LEFT                     0x00000000
#define DST_X_LEFT_TO_RIGHT                     0x00000001
#define DST_Y_BOTTOM_TO_TOP                     0x00000000
#define DST_Y_TOP_TO_BOTTOM                     0x00000002
#define DST_X_MAJOR                             0x00000000
#define DST_Y_MAJOR                             0x00000004
#define DST_X_TILE                              0x00000008
#define DST_Y_TILE                              0x00000010
#define DST_LAST_PEL                            0x00000020
#define DST_TRAIL_X_RIGHT_TO_LEFT               0x00000000
#define DST_TRAIL_X_LEFT_TO_RIGHT               0x00000040
#define DST_TRAP_FILL_RIGHT_TO_LEFT             0x00000000
#define DST_TRAP_FILL_LEFT_TO_RIGHT             0x00000080
#define DST_BRES_SIGN                           0x00000100
#define DST_HOST_BIG_ENDIAN_EN                  0x00000200
#define DST_POLYLINE_NONLAST                    0x00008000
#define DST_RASTER_STALL                        0x00010000
#define DST_POLY_EDGE                           0x00040000

/* DP_MIX bit constants */
#define DP_SRC_RECT                             0x00000200
#define DP_SRC_HOST                             0x00000300
#define DP_SRC_HOST_BYTEALIGN                   0x00000400
#define DP_SRC_SOURCE                           0x00000700
#define DP_ROP3                                 0x00ff0000

/* LVDS_GEN_CNTL constants */
#define LVDS_BL_MOD_LEVEL_MASK                  0x0000ff00
#define LVDS_BL_MOD_LEVEL_SHIFT                 8
#define LVDS_BL_MOD_EN                          0x00010000
#define LVDS_DIGION                             0x00040000
#define LVDS_BLON                               0x00080000
#define LVDS_ON                                 0x00000001
#define LVDS_DISPLAY_DIS                        0x00000002
#define LVDS_PANEL_TYPE_2PIX_PER_CLK            0x00000004
#define LVDS_PANEL_24BITS_TFT                   0x00000008
#define LVDS_FRAME_MOD_NO                       0x00000000
#define LVDS_FRAME_MOD_2_LEVELS                 0x00000010
#define LVDS_FRAME_MOD_4_LEVELS                 0x00000020
#define LVDS_RST_FM                             0x00000040
#define LVDS_EN                                 0x00000080

/* CRTC2_GEN_CNTL constants */
#define CRTC2_EN                                0x02000000

/* POWER_MANAGEMENT constants */
#define PWR_MGT_ON                              0x00000001
#define PWR_MGT_MODE_MASK                       0x00000006
#define PWR_MGT_MODE_PIN                        0x00000000
#define PWR_MGT_MODE_REGISTER                   0x00000002
#define PWR_MGT_MODE_TIMER                      0x00000004
#define PWR_MGT_MODE_PCI                        0x00000006
#define PWR_MGT_AUTO_PWR_UP_EN                  0x00000008
#define PWR_MGT_ACTIVITY_PIN_ON                 0x00000010
#define PWR_MGT_STANDBY_POL                     0x00000020
#define PWR_MGT_SUSPEND_POL                     0x00000040
#define PWR_MGT_SELF_REFRESH                    0x00000080
#define PWR_MGT_ACTIVITY_PIN_EN                 0x00000100
#define PWR_MGT_KEYBD_SNOOP                     0x00000200
#define PWR_MGT_TRISTATE_MEM_EN                 0x00000800
#define PWR_MGT_SELW4MS                         0x00001000
#define PWR_MGT_SLOWDOWN_MCLK                   0x00002000

#define PMI_PMSCR_REG                           0x60

/* used by ATI bug fix for hardware ROM */
#define RAGE128_MPP_TB_CONFIG                   0x01c0

#endif /* ATI_REGS_H */
