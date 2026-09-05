/*
 * QTest testcase for vga cards
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/sockets.h"
#include "libqtest.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "qobject/qdict.h"

#define VBE_DISPI_IOPORT_INDEX 0x1ce
#define VBE_DISPI_IOPORT_DATA  0x1cf
#define VBE_DISPI_INDEX_ID     0
#define VBE_DISPI_INDEX_XRES   1
#define VBE_DISPI_INDEX_YRES   2
#define VBE_DISPI_INDEX_BPP    3
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_INDEX_VIRT_WIDTH 6
#define VBE_DISPI_ID5          0xb0c5
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40
#define VBE_DISPI_NOCLEARMEM   0x80
#define IA64_ATI_FB_BASE       0x00000000c4000000ULL
#define IA64_ATI_MMIO_BASE     0x00000000c8000000ULL
#define IA64_RV100_FB_BASE     0x00000000c8000000ULL
#define IA64_RV100_MMIO_BASE   0x00000000d0000000ULL
#define IA64_PCI_CONFIG_BASE   0x0000007ff0000000ULL
#define IA64_RV100_PCI_DEVFN   (5U << 3)
#define PCI_COMMAND_OFFSET     0x04
#define PCI_COMMAND_IO_MEMORY_MASTER 0x0007
#define IA64_VGA_LEGACY_BASE   0x00000000000a0000ULL
#define HP_QUADRO2_LEGACY_IO_BASE UINT64_C(0x0000000ffc000000)
#define HP_QUADRO2_FB_BASE      UINT64_C(0xe8000000)
#define HP_QUADRO2_MMIO_BASE    UINT64_C(0xe7000000)
#define HP_QUADRO2_VRAM_SIZE    (64U * 1024U * 1024U)
#define HP_QUADRO2_PRAMIN       UINT64_C(0x00700000)
#define HP_QUADRO2_PFIFO_INTR_0 UINT64_C(0x00002100)
#define HP_QUADRO2_PFIFO_RAMHT  UINT64_C(0x00002210)
#define HP_QUADRO2_PFIFO_RAMFC  UINT64_C(0x00002214)
#define HP_QUADRO2_PFIFO_MODE   UINT64_C(0x00002504)
#define HP_QUADRO2_PFIFO_PUSH1  UINT64_C(0x00003204)
#define HP_QUADRO2_PFIFO_DMA_PUSH UINT64_C(0x00003220)
#define HP_QUADRO2_PFIFO_DMA_INSTANCE UINT64_C(0x0000322c)
#define HP_QUADRO2_PFIFO_DMA_PUT UINT64_C(0x00003240)
#define HP_QUADRO2_PFIFO_DMA_GET UINT64_C(0x00003244)
#define HP_QUADRO2_PFIFO_REF_CNT UINT64_C(0x00003248)
#define HP_QUADRO2_PMC_INTR_0 UINT64_C(0x00000100)
#define HP_QUADRO2_PMC_INTR_EN_0 UINT64_C(0x00000140)
#define HP_QUADRO2_PMC_ENABLE UINT64_C(0x00000200)
#define HP_QUADRO2_PTIMER_INTR_0 UINT64_C(0x00009100)
#define HP_QUADRO2_PTIMER_INTR_EN_0 UINT64_C(0x00009140)
#define HP_QUADRO2_PTIMER_NUMERATOR UINT64_C(0x00009200)
#define HP_QUADRO2_PTIMER_DENOMINATOR UINT64_C(0x00009210)
#define HP_QUADRO2_PTIMER_TIME_0 UINT64_C(0x00009400)
#define HP_QUADRO2_PTIMER_TIME_1 UINT64_C(0x00009410)
#define HP_QUADRO2_PTIMER_ALARM_0 UINT64_C(0x00009420)
#define HP_QUADRO2_PTIMER_DEFAULT_DIV UINT32_C(0xc3c1)
#define HP_QUADRO2_PTIMER_DEFAULT_MUL UINT32_C(0x3d09)
#define HP_QUADRO2_PTIMER_DOUBLE_MUL UINT32_C(0x7a12)
#define HP_QUADRO2_INTR_PTIMER   (1U << 20)
#define HP_QUADRO2_ENABLE_PTIMER (1U << 16)
#define HP_QUADRO2_PGRAPH_INTR   UINT64_C(0x00400100)
#define HP_QUADRO2_PGRAPH_NSTATUS UINT64_C(0x00400104)
#define HP_QUADRO2_PGRAPH_NSOURCE UINT64_C(0x00400108)
#define HP_QUADRO2_PGRAPH_TRAPPED_ADDR UINT64_C(0x00400704)
#define HP_QUADRO2_PGRAPH_TRAPPED_DATA UINT64_C(0x00400708)
#define HP_QUADRO2_PGRAPH_TRAPPED_DATA_HIGH UINT64_C(0x0040070c)
#define HP_QUADRO2_PGRAPH_FIFO_ACCESS UINT64_C(0x00400720)
#define HP_QUADRO2_PCRTC_START  UINT64_C(0x00600800)
#define HP_QUADRO2_CURSOR_CONFIG UINT64_C(0x00600810)
#define HP_QUADRO2_CURSOR_POS   UINT64_C(0x00680300)
#define HP_QUADRO2_USER         UINT64_C(0x00800000)
#define HP_QUADRO2_PRMCIO       UINT64_C(0x00601000)
#define HP_QUADRO2_CRTC_INDEX   UINT64_C(0x006013d4)
#define HP_QUADRO2_CRTC_DATA    UINT64_C(0x006013d5)
#define HP_QUADRO2_RAMHT_VALID  (1U << 31)
#define HP_QUADRO2_RAMHT_GRAPHICS (1U << 16)
#define HP_QUADRO2_CHANNEL_SIZE 0x10000U
#define HP_QUADRO2_SUBCHANNEL_SIZE 0x2000U
#define ATI_MM_INDEX           0x0000
#define ATI_MM_DATA            0x0004
#define ATI_GEN_INT_STATUS     0x0044
#define ATI_CLOCK_CNTL_INDEX   0x0008
#define ATI_CLOCK_CNTL_DATA    0x000c
#define ATI_PLL_WR_EN          0x00000080
#define ATI_PLL_DIV_SEL_3      0x00000300
#define ATI_PPLL_REF_DIV       0x03
#define ATI_PPLL_DIV_3         0x07
#define ATI_PPLL_ATOMIC_UPDATE 0x00008000
#define ATI_CRTC_GEN_CNTL      0x0050
#define ATI_CRTC_STATUS        0x005c
#define ATI_CNFG_CNTL          0x00e0
#define ATI_CRTC_EXT_CNTL      0x0054
#define ATI_DAC_CNTL           0x0058
#define ATI_DAC_EXT_CNTL       0x0280
#define ATI_DAC_MACRO_CNTL     0x0d04
#define ATI_DAC_CMP_OUTPUT     (1U << 7)
#define ATI_PALETTE_INDEX      0x00b0
#define ATI_PALETTE_DATA       0x00b4
#define ATI_PALETTE_30_DATA    0x00b8
#define ATI_CRTC_H_TOTAL_DISP  0x0200
#define ATI_CRTC_V_TOTAL_DISP  0x0208
#define ATI_CRTC_V_SYNC_STRT_WID 0x020c
#define ATI_CRTC_VLINE_CRNT_VLINE 0x0210
#define ATI_CRTC_CRNT_FRAME    0x0214
#define ATI_CRTC_OFFSET        0x0224
#define ATI_CRTC_OFFSET_PENDING (1U << 30)
#define ATI_CRTC_OFFSET_LOCK    (1U << 31)
#define ATI_CRTC_OFFSET_CNTL   0x0228
#define ATI_CRTC_PITCH         0x022c
#define ATI_CUR_OFFSET         0x0260
#define ATI_CUR_HORZ_VERT_POSN 0x0264
#define ATI_CUR_HORZ_VERT_OFF  0x0268
#define ATI_CUR_CLR0           0x026c
#define ATI_VBLANK_FRAME_NS    (1000000000LL / 60)
#define ATI_CRTC_EXT_DISP_EN   0x01000000
#define ATI_CRTC_EN            0x02000000
#define ATI_CRTC_CUR_EN        (1U << 16)
#define ATI_CRTC_CUR_MODE_24BPP (2U << 20)
#define ATI_CRTC_PIX_WIDTH_8   0x00000200
#define ATI_CRTC_PIX_WIDTH_32  0x00000600
#define ATI_APER_0_ENDIAN      0x00000001
#define ATI_DST_OFFSET         0x1404
#define ATI_DST_PITCH          0x1408
#define ATI_DST_WIDTH          0x140c
#define ATI_DST_HEIGHT         0x1410
#define ATI_SRC_PITCH_OFFSET   0x1428
#define ATI_DST_PITCH_OFFSET   0x142c
#define ATI_SRC_X              0x1414
#define ATI_SRC_Y              0x1418
#define ATI_DST_X              0x141c
#define ATI_DST_Y              0x1420
#define ATI_DP_GUI_MASTER_CNTL 0x146c
#define ATI_BRUSH_Y_X          0x1474
#define ATI_DP_BRUSH_BKGD_CLR  0x1478
#define ATI_DP_BRUSH_FRGD_CLR  0x147c
#define ATI_BRUSH_DATA0        0x1480
#define ATI_DP_SRC_FRGD_CLR    0x15d8
#define ATI_DP_SRC_BKGD_CLR    0x15dc
#define ATI_CLR_CMP_CNTL       0x15c0
#define ATI_CLR_CMP_CLR_SRC    0x15c4
#define ATI_CLR_CMP_CLR_DST    0x15c8
#define ATI_CLR_CMP_MASK       0x15cc
#define ATI_SRC_OFFSET         0x15ac
#define ATI_SRC_PITCH          0x15b0
#define ATI_SC_TOP_LEFT        0x16ec
#define ATI_SC_LEFT            0x1640
#define ATI_SC_RIGHT           0x1644
#define ATI_SC_TOP             0x1648
#define ATI_SC_BOTTOM          0x164c
#define ATI_SC_BOTTOM_RIGHT    0x16f0
#define ATI_SRC_SC_BOTTOM_RIGHT 0x16f4
#define ATI_DP_CNTL            0x16c0
#define ATI_DP_DATATYPE        0x16c4
#define ATI_DP_MIX             0x16c8
#define ATI_RBBM_GUICNTL       0x172c
#define ATI_DST_LTR_TTB        0x00000003
#define ATI_DST_RTL_TTB        0x00000002
#define ATI_GMC_SRC_PITCH      0x00000001
#define ATI_GMC_DST_PITCH      0x00000002
#define ATI_GMC_SRC_CLIPPING   0x00000004
#define ATI_GMC_DST_CLIPPING   0x00000008
#define ATI_GMC_BRUSH_MONO_FG_LA 0x00000010
#define ATI_GMC_BRUSH_COLOR    0x000000a0
#define ATI_GMC_BRUSH_SOLID    0x000000d0
#define ATI_GMC_BRUSH_SOLID_LINE 0x000000e0
#define ATI_GMC_BRUSH_NONE     0x000000f0
#define ATI_GMC_DST_8BPP       0x00000200
#define ATI_GMC_DST_16BPP      0x00000400
#define ATI_GMC_DST_24BPP      0x00000500
#define ATI_GMC_DST_32BPP      0x00000600
#define ATI_GMC_SRC_MONO_FG_LA 0x00001000
#define ATI_GMC_SRC_COLOR      0x00003000
#define ATI_GMC_BYTE_LSB_TO_MSB 0x00004000
#define ATI_GMC_ROP3_SRCCOPY   0x00cc0000
#define ATI_GMC_ROP3_PATCOPY   0x00f00000
#define ATI_GMC_ROP3_SRCINVERT 0x00660000
#define ATI_GMC_ROP3_PATINVERT 0x005a0000
#define ATI_GMC_ROP3_PATXORSRC 0x003c0000
#define ATI_GMC_ROP3_BLACKNESS 0x00000000
#define ATI_GMC_ROP3_WHITENESS 0x00ff0000
#define ATI_GMC_DP_SRC_RECT    0x02000000
#define ATI_GMC_DP_SRC_HOST    0x03000000
#define ATI_GMC_DP_SRC_HOST_BYTEALIGN 0x04000000
#define ATI_GMC_CLR_CMP_DIS    0x10000000
#define ATI_GMC_WR_MSK_DIS     0x40000000
#define ATI_GMC_LD_BRUSH_Y_X   0x80000000
#define ATI_DP_WRITE_MASK      0x16cc
#define ATI_HOST_DATA0         0x17c0
#define ATI_HOST_DATA_LAST     0x17e0
#define ATI_DP_DST_16BPP       0x00000004
#define ATI_DP_DST_32BPP       0x00000006
#define ATI_DP_SRC_COLOR       0x00030000
#define ATI_DP_HOST_BIG_ENDIAN 0x20000000
#define ATI_HOST_SWAP_16BIT    0x00000001
#define ATI_HOST_SWAP_32BIT    0x00000002
#define ATI_HOST_SWAP_HDW      0x00000003
#define ATI_DP_MIX_SRC_HOST    0x00000300
#define ATI_DP_MIX_ROP3_SRCCOPY 0x00cc0000
#define ATI_DEFAULT_SC_BOTTOM_RIGHT 0x16e8
#define ATI_TEST_VRAM_SIZE     (16U * 1024U * 1024U)
#define ATI_GEN_INT_CNTL       0x0040
#define ATI_GPIO_VGA_DDC       0x0060
#define ATI_CUR_CLR1           0x0270
#define ATI_RAGE128_GEN_INT_STATUS_RESET  0x00080000U
#define ATI_R100_GEN_INT_STATUS_RESET     0x00080000U
#define ATI_CRTC_GEN_CNTL_RESET           0x04000000U
#define ATI_RAGE128_CRTC_EXT_CNTL_RESET   0x00200000U
#define ATI_RAGE128_DAC_CNTL_RESET        0xff00000aU
#define ATI_R100_DAC_CNTL_RESET           0xff00000aU
#define ATI_RV100_CRTC_OFFSET_CNTL_RESET  0x10000000U
#define ATI_CRTC_VBLANK_INT    (1U << 0)
#define ATI_CRTC_VLINE_INT     (1U << 1)
#define ATI_CRTC_VSYNC_INT     (1U << 2)
#define ATI_CRTC_VBLANK_CUR    (1U << 0)
#define ATI_CRTC_VBLANK_SAVE   (1U << 1)
#define ATI_CRTC_VLINE_SYNC    (1U << 2)
#define ATI_CRTC_FRAME         (1U << 3)
#define ATI_CRTC_FIX_VSYNC_TIMING (1U << 31)
#define ATI_SW_INT_ENABLE      (1U << 25)
#define ATI_SW_INT_TEST        (1U << 25)
#define ATI_SW_INT_FIRE        (1U << 26)
#define R100_CP_RB_BASE        0x0700
#define R100_CP_RB_CNTL        0x0704
#define R100_CP_RB_RPTR_ADDR   0x070c
#define R100_CP_RB_RPTR        0x0710
#define R100_CP_RB_WPTR        0x0714
#define R100_CP_RB_RPTR_WR     0x071c
#define R100_CP_IB_BASE        0x0738
#define R100_CP_IB_BUFSZ       0x073c
#define R100_CP_CSQ_CNTL       0x0740
#define R100_CSQ_PRIPIO_INDDIS (1U << 28)
#define R100_CSQ_PRIBM_INDDIS  (2U << 28)
#define R100_CSQ_PRIBM_INDBM   (4U << 28)
#define R100_SCRATCH_UMSK      0x0770
#define R100_SCRATCH_ADDR      0x0774
#define R100_CP_ME_RAM_ADDR    0x07d4
#define R100_CP_ME_RAM_DATAH   0x07dc
#define R100_CP_ME_RAM_DATAL   0x07e0
#define R100_SCRATCH_REG0      0x15e0
#define R100_FOG_TABLE_INDEX   0x1a14
#define R100_FOG_TABLE_DATA    0x1a18
#define R100_PP_FOG_COLOR      0x1c18
#define R100_RB3D_BLENDCNTL    0x1c20
#define R100_RB3D_DEPTHOFFSET  0x1c24
#define R100_RB3D_DEPTHPITCH   0x1c28
#define R100_RB3D_ZSTENCILCNTL 0x1c2c
#define R100_PP_CNTL           0x1c38
#define R100_RB3D_CNTL         0x1c3c
#define R100_RB3D_COLOROFFSET  0x1c40
#define R100_RE_WIDTH_HEIGHT   0x1c44
#define R100_RB3D_COLORPITCH   0x1c48
#define R100_SE_CNTL           0x1c4c
#define R100_SE_COORD_FMT      0x1c50
#define R100_PP_TXFILTER_0     0x1c54
#define R100_PP_TXFORMAT_0     0x1c58
#define R100_PP_TXOFFSET_0     0x1c5c
#define R100_PP_TXCBLEND_0     0x1c60
#define R100_PP_TXABLEND_0     0x1c64
#define R100_PP_TXFILTER_1     0x1c6c
#define R100_PP_TXFORMAT_1     0x1c70
#define R100_PP_TXOFFSET_1     0x1c74
#define R100_PP_TXCBLEND_1     0x1c78
#define R100_PP_TXABLEND_1     0x1c7c
#define R100_PP_TEX_SIZE_0     0x1d04
#define R100_PP_TEX_PITCH_0    0x1d08
#define R100_PP_TEX_SIZE_1     0x1d0c
#define R100_PP_TEX_PITCH_1    0x1d10
#define R100_PP_BORDER_COLOR_0 0x1d40
#define R100_RB3D_STENCILREFMASK 0x1d7c
#define R100_RB3D_ROPCNTL      0x1d80
#define R100_SE_VPORT_ZSCALE  0x1da8
#define R100_SE_VPORT_ZOFFSET 0x1dac
#define R100_RE_TOP_LEFT       0x26c0
#define R100_SE_PORT_DATA0     0x2000
#define R100_SE_VTX_FMT        0x2080
#define R100_SE_VF_CNTL        0x2084
#define R100_SE_CNTL_STATUS    0x2140
#define R100_MC_FB_LOCATION    0x0148
#define R100_AIC_CNTL          0x01d0
#define R100_AIC_PT_BASE       0x01d8
#define R100_AIC_LO_ADDR       0x01dc
#define R100_AIC_HI_ADDR       0x01e0
#define R100_CP_PACKET2        (2U << 30)
#define R100_CP_PACKET3        (3U << 30)
#define R100_CP_PACKET0_ONE_REG (1U << 15)
#define R100_PACKET3_DRAW_VBUF 0x28
#define R100_PACKET3_RNDR_GEN_PRIM 0x25
#define R100_PACKET3_DRAW_IMMD 0x29
#define R100_PACKET3_DRAW_INDX 0x2a
#define R100_PACKET3_LOAD_VBPNTR 0x2f
#define R100_PACKET3_NEXT_CHAR  0x19
#define R100_PACKET3_PLY_NEXTSCAN 0x1d
#define R100_PACKET3_CNTL_POLYSCANLINES 0x98
#define R100_PACKET3_CNTL_BITBLT     0x92
#define R100_PACKET3_CNTL_HOSTDATA_BLT 0x94
#define R100_PACKET3_CNTL_TRANS_BITBLT 0x9c
#define R100_PACKET3_CNTL_PAINT_MULTI 0x9a
#define R100_PACKET3_BITBLT_MULTI 0x9b
#define R100_VTX_FMT_PKCOLOR   (1U << 3)
#define R100_VTX_FMT_FPSPEC    (1U << 4)
#define R100_VTX_FMT_FPFOG     (1U << 5)
#define R100_VTX_FMT_PKSPEC    (1U << 6)
#define R100_VTX_FMT_W0        (1U << 0)
#define R100_VTX_FMT_ST0       (1U << 7)
#define R100_VTX_FMT_ST1       (1U << 8)
#define R100_VTX_FMT_Q0        (1U << 14)
#define R100_VTX_FMT_N1        (1U << 30)
#define R100_VTX_FMT_Z         (1U << 31)
#define R100_VF_POINT_LIST     1
#define R100_VF_LINE_LIST      2
#define R100_VF_TRIANGLE_LIST  4
#define R100_VF_RECTANGLE_LIST 8
#define R100_VF_POLYGON        15
#define R100_VF_WALK_DATA      (3U << 4)
#define R100_VF_WALK_LIST      (2U << 4)
#define R100_VF_WALK_IND       (1U << 4)
#define R100_VF_COLOR_RGBA     (1U << 6)
#define R100_VF_INDEX_SIZE_32  (1U << 11)
#define R100_RB_Z_ENABLE       (1U << 8)
#define R100_RB_STENCIL_ENABLE (1U << 7)
#define R100_RB_ROP_ENABLE     (1U << 6)
#define R100_RB_ALPHA_BLEND    (1U << 0)
#define R100_RB_COLOR_ARGB8888 (6U << 10)
#define R100_SRC_BLEND_ONE     (33U << 16)
#define R100_DST_BLEND_ONE     (33U << 24)
#define R100_COLOR_TILE_ENABLE (1U << 16)
#define R100_Z_WRITE_ENABLE    (1U << 30)
#define R100_VPORT_Z_XFORM     (1U << 25)
#define R100_TX0_ENABLE        (1U << 4)
#define R100_TX1_ENABLE        (1U << 5)
#define R100_TX_BLEND0_ENABLE  (1U << 12)
#define R100_TX_BLEND1_ENABLE  (1U << 13)
#define R100_PP_SPECULAR_ENABLE (1U << 21)
#define R100_PP_FOG_ENABLE     (1U << 22)
#define R100_FOG_TABLE         (1U << 24)
#define R100_FOG_USE_DIFFUSE_ALPHA (2U << 25)
#define R100_FOG_USE_SPEC_ALPHA (3U << 25)
#define R100_TX_ARGB8888       6
#define R100_TX_I8             0
#define R100_TX_AI88           1
#define R100_TX_Y8             8
#define R100_TX_VYUY422        10
#define R100_TX_YVYU422        11
#define R100_TX_DXT1           12
#define R100_TX_DXT3           14
#define R100_TX_DXT5           15
#define R100_TX_ALPHA_IN_MAP   (1U << 6)
#define R100_TX_NON_POWER2     (1U << 7)
#define R100_TX_PERSPECTIVE    (1U << 31)
#define R100_TX_ROUTE_ST1      (1U << 24)
#define R100_TX_MICRO_X2       (1U << 3)
#define R100_TX_MACRO_TILE     (1U << 2)
#define R100_TX_MAG_LINEAR     (1U << 0)
#define R100_TX_MIN_LINEAR     (1U << 1)
#define R100_TX_MIN_NEAREST_MIP_NEAREST (2U << 1)
#define R100_TX_MIN_MIP_NEAREST R100_TX_MIN_NEAREST_MIP_NEAREST
#define R100_TX_MIN_NEAREST_MIP_LINEAR (3U << 1)
#define R100_TX_MIN_LINEAR_MIP_NEAREST (6U << 1)
#define R100_TX_MIN_LINEAR_MIP_LINEAR (7U << 1)
#define R100_TX_LOD_BIAS(value) ((uint32_t)(value) << 8)
#define R100_TX_MAX_MIP_LEVEL(value) ((uint32_t)(value) << 16)
#define R100_TX_YUV_TO_RGB     (1U << 20)
#define R100_TX_LINEAR         (R100_TX_MAG_LINEAR | R100_TX_MIN_LINEAR)
#define R100_TX_CLAMP_S(mode)  ((uint32_t)(mode) << 23)
#define R100_TX_CLAMP_T(mode)  ((uint32_t)(mode) << 27)
#define R100_TX_BORDER_D3D     (1U << 31)
#define R100_COMBINER_CLAMP    (1U << 23)
#define R100_COORD_ST0_NONPARAM (1U << 8)
#define R100_COORD_ST1_NONPARAM (1U << 9)
#define R100_RB_RPTR_WR_ENA    (1U << 31)
#define R100_RB_NO_UPDATE      (1U << 27)
#define R100_RB_BUF_SWAP_32BIT (2U << 16)
#define R100_SCRATCH_SWAP_32BIT (2U << 16)
#define R100_RPTR_SWAP_32BIT   2U
#define R100_VC_SWAP_32BIT     2U
#define R100_TCL_BYPASS        (1U << 8)
#define R100_PCIGART_TRANSLATE_EN (1U << 0)
#define R100_DIS_OUT_OF_PCI_GART_ACCESS (1U << 1)
#define VGA_SEQ_INDEX          0x3c4
#define VGA_SEQ_DATA           0x3c5
#define VGA_SEQ_RESET          0
#define VGA_SEQ_PLANE_WRITE    2
#define VGA_SEQ_MEMORY_MODE    4
#define VGA_GFX_INDEX          0x3ce
#define VGA_GFX_DATA           0x3cf
#define VGA_GFX_SR_ENABLE      1
#define VGA_GFX_DATA_ROTATE    3
#define VGA_GFX_PLANE_READ     4
#define VGA_GFX_MODE           5
#define VGA_GFX_MISC           6
#define VGA_GFX_BIT_MASK       8
#define VGA_CRTC_INDEX         0x3b4
#define VGA_CRTC_DATA          0x3b5
#define VGA_CRTC_OFFSET        0x13
#define VGA_ATTR_INDEX         0x3c0
#define VGA_INPUT_STATUS1      0x3ba
#define VGA_PEL_WRITE_INDEX    0x3c8
#define VGA_PEL_DATA           0x3c9

static const char *machine_args(void)
{
    return g_str_equal(qtest_get_arch(), "ia64") ?
           "-machine ia64-vpc,nvram=none " : "";
}

/* ISA I/O-port words are little-endian, independent of target byte order. */
static void legacy_outw_le(QTestState *qts, uint16_t addr, uint16_t value)
{
    qtest_outw(qts, addr,
               qtest_big_endian(qts) ? bswap16(value) : value);
}

static uint16_t legacy_inw_le(QTestState *qts, uint16_t addr)
{
    uint16_t value = qtest_inw(qts, addr);

    return qtest_big_endian(qts) ? bswap16(value) : value;
}

static uint32_t f32_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint16_t ati_vbe_read(QTestState *qts, uint16_t index);
static void display_wait_for_migration_status(QTestState *qts,
                                              const char *wanted);
static void display_wait_for_migration(QTestState *qts);

static void ati_pci_enable(QTestState *qts)
{
    qtest_writew(qts, IA64_PCI_CONFIG_BASE +
                 ((uint64_t)IA64_RV100_PCI_DEVFN << 12) +
                 PCI_COMMAND_OFFSET, PCI_COMMAND_IO_MEMORY_MASTER);
}

static void ati_ddc_lines(QTestState *qts, bool clock, bool data)
{
    uint32_t value = (1U << 16) | (1U << 17);

    value |= clock ? 1U << 1 : 0;
    value |= data ? 1U : 0;
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GPIO_VGA_DDC, value);
}

static void ati_ddc_start(QTestState *qts)
{
    ati_ddc_lines(qts, true, true);
    ati_ddc_lines(qts, true, false);
    ati_ddc_lines(qts, false, false);
}

static void ati_ddc_stop(QTestState *qts)
{
    ati_ddc_lines(qts, false, false);
    ati_ddc_lines(qts, true, false);
    ati_ddc_lines(qts, true, true);
}

static bool ati_ddc_send_byte(QTestState *qts, uint8_t value)
{
    unsigned int i;
    uint32_t gpio;

    for (i = 0; i < 8; i++) {
        bool bit = value & (0x80U >> i);

        ati_ddc_lines(qts, false, bit);
        ati_ddc_lines(qts, true, bit);
        ati_ddc_lines(qts, false, bit);
    }
    ati_ddc_lines(qts, false, true);
    ati_ddc_lines(qts, true, true);
    gpio = qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_GPIO_VGA_DDC);
    ati_ddc_lines(qts, false, true);
    return !(gpio & (1U << 8));
}

static uint8_t ati_ddc_read_byte(QTestState *qts, bool acknowledge)
{
    uint8_t value = 0;
    unsigned int i;

    for (i = 0; i < 8; i++) {
        ati_ddc_lines(qts, false, true);
        ati_ddc_lines(qts, true, true);
        value = value << 1 | !!(qtest_readl(
            qts, IA64_RV100_MMIO_BASE + ATI_GPIO_VGA_DDC) & (1U << 8));
        ati_ddc_lines(qts, false, true);
    }
    ati_ddc_lines(qts, false, !acknowledge);
    ati_ddc_lines(qts, true, !acknowledge);
    ati_ddc_lines(qts, false, true);
    return value;
}

static void pci_multihead(void)
{
    QTestState *qts;

    qts = qtest_initf("%s-vga none -device VGA -device secondary-vga",
                      machine_args());
    qtest_quit(qts);
}

static void test_vga(gconstpointer data)
{
    QTestState *qts;

    qts = qtest_initf("%s-vga none -device %s", machine_args(),
                      (const char *)data);
    qtest_quit(qts);
}

static void ati_es1000_realize(void)
{
    QTestState *qts;

    qts = qtest_initf("%s-vga none -device ati-vga,model=es1000",
                      machine_args());
    qtest_quit(qts);
}

static void ati_rv100_mm_aper(void)
{
    QTestState *qts;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_MM_INDEX, 0x80000004);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA, 0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 4), ==,
                    0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA),
                    ==, 0x11223344);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_MM_INDEX, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_MM_INDEX),
                    ==, 0xfffffffc);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA, 0xaabbccdd);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                    ATI_TEST_VRAM_SIZE - 4), ==,
                    0xaabbccdd);
    qtest_writel(qts, IA64_RV100_FB_BASE + ATI_TEST_VRAM_SIZE - 4,
                 0x55667788);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA),
                    ==, 0x55667788);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_MM_INDEX, 0x81000007);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_MM_INDEX),
                    ==, 0x81000004);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA),
                    ==, 0x11223344);
    qtest_writeb(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA + 1, 0x5a);
    qtest_writew(qts, IA64_RV100_MMIO_BASE + ATI_MM_DATA + 2, 0xc3d4);
    g_assert_cmphex(qtest_readb(qts, IA64_RV100_MMIO_BASE +
                                    ATI_MM_DATA + 1), ==, 0x5a);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_MMIO_BASE +
                                    ATI_MM_DATA + 2), ==, 0xc3d4);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 4), ==,
                    0xc3d45a44);

    qtest_quit(qts);
}

static void ati_palette_access(void)
{
    static const struct {
        const char *model;
        uint64_t mmio;
    } devices[] = {
        { "rage128p", IA64_ATI_MMIO_BASE },
        { "rv100", IA64_RV100_MMIO_BASE },
        { "es1000", IA64_RV100_MMIO_BASE },
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(devices); i++) {
        const uint64_t mmio = devices[i].mmio;
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", devices[i].model);

        ati_pci_enable(qts);
        qtest_writel(qts, mmio + ATI_PALETTE_INDEX, 0x00a5003c);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_INDEX), ==,
                        0x00a5003c);
        g_assert_cmphex(qtest_readb(qts, mmio + ATI_PALETTE_INDEX + 2),
                        ==, 0xa5);

        /* Reserved byte lanes must not change either index. */
        qtest_writeb(qts, mmio + ATI_PALETTE_INDEX + 1, 0xff);
        qtest_writeb(qts, mmio + ATI_PALETTE_INDEX + 3, 0xff);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_INDEX), ==,
                        0x00a5003c);
        qtest_writew(qts, mmio + ATI_PALETTE_INDEX, 0xff80);
        qtest_writew(qts, mmio + ATI_PALETTE_INDEX + 2, 0xff80);
        g_assert_cmphex(qtest_readw(qts, mmio + ATI_PALETTE_INDEX), ==,
                        0x80);
        g_assert_cmphex(qtest_readw(qts, mmio + ATI_PALETTE_INDEX + 2),
                        ==, 0x80);

        /* A 32-bit MMIO access transfers one complete RGB entry. */
        qtest_writel(qts, mmio + ATI_PALETTE_DATA, 0x00123456);
        qtest_writel(qts, mmio + ATI_PALETTE_DATA, 0x00789abc);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_DATA), ==,
                        0x00123456);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_DATA), ==,
                        0x00789abc);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_INDEX), ==,
                        0x00820082);

        /* Byte/word accesses use B/G/R lanes and advance one entry. */
        qtest_writeb(qts, mmio + ATI_PALETTE_INDEX + 2, 0x80);
        g_assert_cmphex(qtest_readb(qts, mmio + ATI_PALETTE_DATA + 2),
                        ==, 0x12);
        g_assert_cmphex(qtest_readw(qts, mmio + ATI_PALETTE_DATA), ==,
                        0x9abc);
        qtest_writeb(qts, mmio + ATI_PALETTE_INDEX, 0x80);
        qtest_writeb(qts, mmio + ATI_PALETTE_DATA + 1, 0xde);
        qtest_writew(qts, mmio + ATI_PALETTE_DATA, 0x1357);
        qtest_writeb(qts, mmio + ATI_PALETTE_INDEX + 2, 0x80);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_DATA), ==,
                        0x0012de56);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_DATA), ==,
                        0x00781357);

        /* MMIO and VGA accesses share palette entries and indices. */
        qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(0x3c7), 0x80);
        g_assert_cmphex(qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(0x3c9)),
                        ==, 0x12);
        g_assert_cmphex(qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(0x3c9)),
                        ==, 0xde);
        g_assert_cmphex(qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(0x3c9)),
                        ==, 0x56);
        g_assert_cmphex(qtest_readb(qts, mmio + ATI_PALETTE_INDEX + 2),
                        ==, 0x81);

        /* The 10-bit interface also advances the read index and wraps. */
        qtest_writel(qts, mmio + ATI_PALETTE_INDEX, 0x00ff00ff);
        qtest_writel(qts, mmio + ATI_PALETTE_30_DATA, 0x2abcdef1);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_30_DATA), ==,
                        0x2abcdef1);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_PALETTE_INDEX), ==, 0);
        qtest_quit(qts);
    }
}

static void ati_radeon_dac_detect(void)
{
    static const char *models[] = { "rv100", "es1000" };

    for (unsigned int i = 0; i < ARRAY_SIZE(models); i++) {
        const uint64_t mmio = IA64_RV100_MMIO_BASE;
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", models[i]);

        ati_pci_enable(qts);

        /* Detect a connected CRT with the primary DAC's load comparator. */
        qtest_writel(qts, mmio + ATI_DAC_MACRO_CNTL, 0);
        qtest_writel(qts, mmio + ATI_CRTC_EXT_CNTL, 0x8000);
        qtest_writel(qts, mmio + ATI_DAC_EXT_CNTL, 0x1b6f0);
        qtest_writel(qts, mmio + ATI_DAC_CNTL, 0xff00000a);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_EXT_CNTL), ==,
                        0x1b6f0);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_CNTL) &
                        ATI_DAC_CMP_OUTPUT, ==, ATI_DAC_CMP_OUTPUT);

        /* Status is read-only; powering down the DAC removes the result. */
        qtest_writew(qts, mmio + ATI_DAC_CNTL, 0x808a);
        g_assert_cmphex(qtest_readb(qts, mmio + ATI_DAC_CNTL) &
                        ATI_DAC_CMP_OUTPUT, ==, 0);
        qtest_writew(qts, mmio + ATI_DAC_CNTL, 0x000a);
        qtest_writeb(qts, mmio + ATI_DAC_MACRO_CNTL + 2, 7);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_MACRO_CNTL), ==,
                        0x70000);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_CNTL) &
                        ATI_DAC_CMP_OUTPUT, ==, 0);
        qtest_writeb(qts, mmio + ATI_DAC_MACRO_CNTL + 2, 0);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_CNTL) &
                        ATI_DAC_CMP_OUTPUT, ==, ATI_DAC_CMP_OUTPUT);

        /* Clearing the load-detection setup returns the comparator to idle. */
        qtest_writel(qts, mmio + ATI_DAC_EXT_CNTL, 0);
        qtest_writel(qts, mmio + ATI_CRTC_EXT_CNTL, 0);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_CNTL), ==,
                        0xff00000a);
        qtest_quit(qts);
    }
}

static void ati_es1000_crtc_2d(void)
{
    static const uint8_t edid_header[] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    };
    static const uint32_t host_pixels[] = {
        0x11111111, 0x22222222, 0x33333333, 0x44444444,
        0x55555555, 0x66666666, 0x77777777, 0x88888888,
    };
    static const uint32_t host_expected[] = {
        0x88888888, 0x77777777, 0x66666666, 0x55555555,
        0x44444444, 0x33333333, 0x22222222, 0x11111111,
    };
    static const uint8_t mono_initial[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    };
    static const uint8_t mono_expected[] = {
        0xee, 0x11, 0xee, 0x13, 0xee, 0x15, 0xee, 0x17,
    };
    static const uint16_t mono_rop_expected[] = {
        0x1000, 0x1001, 0x10f2, 0x10f3, 0x1004, 0x1005,
    };
    static const uint8_t mono_linear_expected[] = {
        0xee, 0x11, 0xee, 0x11, 0xee,
        0x11, 0x11, 0x11, 0x11, 0xee,
    };
    static const uint8_t mono_bytealign_expected[] = {
        0xee, 0x11, 0xee, 0x11, 0xee,
        0x11, 0xee, 0x11, 0xee, 0xee,
    };
    uint8_t mono_actual[sizeof(mono_expected)];
    uint8_t mono_rows_actual[sizeof(mono_linear_expected)];
    uint8_t endian_rop_actual[2];
    uint8_t edid_actual[sizeof(edid_header)];
    uint32_t host_actual[ARRAY_SIZE(host_expected)];
    QTestState *qts;
    unsigned int i;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=es1000");
    qtest_irq_intercept_in(qts, "/machine/unattached/device[1]");
    qtest_memset(qts, IA64_RV100_FB_BASE, 0, 0x3000);

    ati_ddc_start(qts);
    g_assert_false(ati_ddc_send_byte(qts, 0xa2));
    ati_ddc_stop(qts);

    /* Reset must terminate a transaction instead of retaining its slave. */
    ati_ddc_start(qts);
    g_assert_true(ati_ddc_send_byte(qts, 0xa0));
    qtest_system_reset(qts);
    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DEFAULT_SC_BOTTOM_RIGHT,
                 0x1fff1fff);

    /* GUI master writes force Y positive on Radeon but leave X unchanged. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL),
                    ==, ATI_DST_RTL_TTB);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GPIO_VGA_DDC) & 0x300, ==, 0x300);
    ati_ddc_start(qts);
    g_assert_false(ati_ddc_send_byte(qts, 0xa2));
    ati_ddc_stop(qts);

    ati_ddc_start(qts);
    g_assert_true(ati_ddc_send_byte(qts, 0xa0));
    g_assert_true(ati_ddc_send_byte(qts, 0));
    ati_ddc_start(qts);
    g_assert_true(ati_ddc_send_byte(qts, 0xa1));
    for (i = 0; i < ARRAY_SIZE(edid_actual); i++) {
        edid_actual[i] = ati_ddc_read_byte(
            qts, i + 1 < ARRAY_SIZE(edid_actual));
    }
    ati_ddc_stop(qts);
    g_assert_cmpmem(edid_actual, sizeof(edid_actual), edid_header,
                    sizeof(edid_header));

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_H_TOTAL_DISP,
                 ((640 / 8) - 1) << 16);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP,
                 (500 - 1) | ((480 - 1) << 16));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_V_SYNC_STRT_WID,
                 (490 - 1) | (2 << 16));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_PITCH, 640 / 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_32);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_CNTL,
                 ATI_CRTC_VBLANK_INT);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GEN_INT_STATUS) & ATI_CRTC_VBLANK_INT,
                    ==, 0);
    g_assert_false(qtest_get_irq(qts, 17));
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GEN_INT_STATUS) & ATI_CRTC_VBLANK_INT,
                    ==, ATI_CRTC_VBLANK_INT);
    g_assert_true(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VBLANK_INT);
    g_assert_false(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_CNTL, 0);

    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 640);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 480);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, 32);

    /* Destination datatype 5 is reserved on Radeon-family hardware. */
    qtest_memset(qts, IA64_RV100_FB_BASE, 0xa5, 12);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 12);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 4U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x00112233);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_24BPP |
                 ATI_GMC_ROP3_PATCOPY);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_WRITE_MASK), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_CLR_CMP_MASK), ==, UINT32_MAX);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 4);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                        sizeof(uint32_t) * i), ==,
                        0xa5a5a5a5);
    }

    /* HOST_DATA follows both reverse X and reverse Y endpoint semantics. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x1000);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4 * 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (3U << 16) | 4U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 3);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 4);
    for (i = 0; i < ARRAY_SIZE(host_pixels); i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA0 +
                     (i % 4) * 4, host_pixels[i]);
    }
    qtest_memread(qts, IA64_RV100_FB_BASE + 0x1000, host_actual,
                  sizeof(host_actual));
    for (i = 0; i < ARRAY_SIZE(host_actual); i++) {
        host_actual[i] = le32_to_cpu(host_actual[i]);
    }
    g_assert_cmpmem(host_actual, sizeof(host_actual), host_expected,
                    sizeof(host_expected));

    /* BYTEALIGN is a host-data source mode, not a stalled upload. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2200, 0,
                 4 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2200);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH,
                 4 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 4U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY |
                 ATI_GMC_DP_SRC_HOST_BYTEALIGN);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 4);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA0 + i * 4,
                     host_pixels[i]);
    }
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2200 +
                                        i * sizeof(uint32_t)), ==,
                        host_pixels[i]);
    }

    /* A zero-width reverse HOST_DATA blit must not touch one pixel per row. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2300, 0xa5,
                 4 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2300);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 3);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 0);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA0 + i * 4,
                     host_pixels[i]);
    }
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2300 +
                                        i * sizeof(uint32_t)), ==,
                        0xa5a5a5a5);
    }

    /* Radeon LAST flushes valid input once and ends the host upload. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2580, 0xa5,
                 8 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2580);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH,
                 8 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 8U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2580), ==,
                    0x11223344);
    for (i = 1; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2580 +
                                        sizeof(uint32_t) * i), ==,
                        0xa5a5a5a5);
    }
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA0,
                 0x55667788);
    for (i = 1; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2580 +
                                        sizeof(uint32_t) * i), ==,
                        0xa5a5a5a5);
    }

    /* 8/16-bpp HOST color data remains packed across scanlines. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2500, 0xa5, 16);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2500);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (4U << 16) | 1U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_8BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x44332211);
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE + 0x2500 +
                                        i * 4), ==, 0x11U * (i + 1));
    }

    /* Mono foreground/leave-alone preserves pixels selected by zero bits. */
    qtest_memwrite(qts, IA64_RV100_FB_BASE + 0x2600, mono_initial,
                   sizeof(mono_initial));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2600);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 8U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_SRC_FRGD_CLR, 0xee);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_8BPP | ATI_GMC_SRC_MONO_FG_LA |
                 ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_SRCCOPY |
                 ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST, 0x55);
    qtest_memread(qts, IA64_RV100_FB_BASE + 0x2600, mono_actual,
                  sizeof(mono_actual));
    g_assert_cmpmem(mono_actual, sizeof(mono_actual), mono_expected,
                    sizeof(mono_expected));

    /* Leave-alone bits remain no-ops through ROP, mask, clip, and RTL. */
    for (i = 0; i < ARRAY_SIZE(mono_rop_expected); i++) {
        qtest_writew(qts, IA64_RV100_FB_BASE + 0x2b00 + i * 2,
                     0x1000 | i);
    }
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2b00);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 12);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 5U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_RTL_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_SRC_FRGD_CLR, 0x00f0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK, 0x00ff);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_16BPP | ATI_GMC_SRC_MONO_FG_LA |
                 ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_SRCINVERT |
                 ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 5);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 6);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST, 0x2d);
    for (i = 0; i < ARRAY_SIZE(mono_rop_expected); i++) {
        g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2b00 +
                                        i * 2), ==, mono_rop_expected[i]);
    }
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);

    /* Linear mono rows share a byte; BYTEALIGN starts each row at a byte. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 5);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (2U << 16) | 5U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_SRC_FRGD_CLR, 0xee);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_SRC_BKGD_CLR, 0x11);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 2);

    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2800, 0,
                 sizeof(mono_rows_actual));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2800);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_8BPP | ATI_GMC_BYTE_LSB_TO_MSB |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_WRITE_MASK), ==, UINT32_MAX);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 5);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST, 0x1a15);
    qtest_memread(qts, IA64_RV100_FB_BASE + 0x2800, mono_rows_actual,
                  sizeof(mono_rows_actual));
    g_assert_cmpmem(mono_rows_actual, sizeof(mono_rows_actual),
                    mono_linear_expected, sizeof(mono_linear_expected));

    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2820, 0,
                 sizeof(mono_rows_actual));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2820);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_8BPP | ATI_GMC_BYTE_LSB_TO_MSB |
                 ATI_GMC_ROP3_SRCCOPY |
                 ATI_GMC_DP_SRC_HOST_BYTEALIGN);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 5);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST, 0x1a15);
    qtest_memread(qts, IA64_RV100_FB_BASE + 0x2820, mono_rows_actual,
                  sizeof(mono_rows_actual));
    g_assert_cmpmem(mono_rows_actual, sizeof(mono_rows_actual),
                    mono_bytealign_expected,
                    sizeof(mono_bytealign_expected));

    /* Radeon HOST_DATA_SWAP mode 1 swaps bytes in each 16-bit half-word. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2700, 0, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2700);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_16BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_DATATYPE,
                 ATI_DP_HOST_BIG_ENDIAN | ATI_DP_SRC_COLOR |
                 ATI_DP_DST_16BPP);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_DP_DATATYPE) & ATI_DP_HOST_BIG_ENDIAN,
                    ==, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_RBBM_GUICNTL,
                 ATI_HOST_SWAP_16BIT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_MIX,
                 ATI_DP_MIX_ROP3_SRCCOPY | ATI_DP_MIX_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2700), ==,
                    0x4433);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2702), ==,
                    0x2211);

    /* Modes 2 and 3 reverse bytes and exchange 16-bit halves respectively. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2720, 0, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2720);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 1U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_DATATYPE,
                 ATI_DP_SRC_COLOR | ATI_DP_DST_32BPP);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_MIX,
                 ATI_DP_MIX_ROP3_SRCCOPY | ATI_DP_MIX_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_RBBM_GUICNTL,
                 ATI_HOST_SWAP_32BIT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2720), ==,
                    0x44332211);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2730);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_RBBM_GUICNTL,
                 ATI_HOST_SWAP_HDW);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2730), ==,
                    0x33441122);

    /* Narrow HOST colors retain their value in a big-endian framebuffer. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CNFG_CNTL,
                 ATI_APER_0_ENDIAN);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_32);
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2750, 0, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2750);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_16BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_DATATYPE,
                 ATI_DP_SRC_COLOR | ATI_DP_DST_16BPP);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_MIX,
                 ATI_DP_MIX_ROP3_SRCCOPY | ATI_DP_MIX_SRC_HOST);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_RBBM_GUICNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2750), ==,
                    0x4433);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2752), ==,
                    0x2211);

    /* Generic narrow ROP results retain their bytes in big-endian VRAM. */
    qtest_memwrite(qts, IA64_RV100_FB_BASE + 0x2760,
                   (const uint8_t[]) { 0x00, 0xf0 }, 2);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + 0x2770,
                   (const uint8_t[]) { 0x12, 0x34 }, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_OFFSET, 0x2760);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_PITCH, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2770);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 1U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_DST_16BPP |
                 ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCINVERT |
                 ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 1);
    qtest_memread(qts, IA64_RV100_FB_BASE + 0x2770,
                  endian_rop_actual, sizeof(endian_rop_actual));
    g_assert_cmpmem(endian_rop_actual, sizeof(endian_rop_actual),
                    ((const uint8_t[]) { 0x12, 0xc4 }), 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CNFG_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_32);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);

    /* A row wider than its pitch must be rejected before touching VRAM. */
    qtest_memset(qts, IA64_RV100_FB_BASE + ATI_TEST_VRAM_SIZE - 16,
                 0xa5, 16);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET,
                 ATI_TEST_VRAM_SIZE - 16);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 8U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x11223344);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_32BPP |
                 ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 8);
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                        ATI_TEST_VRAM_SIZE - 16 +
                                        sizeof(uint32_t) * i), ==,
                        0xa5a5a5a5);
    }

    /* The generic ROP3 truth table and per-bit write mask are both active. */
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x2000, 0x0f0f0f0f);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x2004, UINT32_MAX);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x2100, 0x33333333);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x2104, 0xaaaaaaaa);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_OFFSET, 0x2000);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_PITCH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2100);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (2U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK,
                 0x00ff00ff);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_DST_32BPP |
                 ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCINVERT |
                 ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2100), ==,
                    0x333c333c);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2104), ==,
                    0xaa55aa55);

    /* Constant ROPs produce bitwise zero/one, independent of the DAC. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_PALETTE_INDEX, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_PALETTE_DATA, 0x00112233);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_PALETTE_DATA, 0x00445566);
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2400, 0xa5,
                 2 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2400);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH,
                 2 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_ROP3_BLACKNESS);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_WRITE_MASK), ==, UINT32_MAX);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2400), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2404), ==, 0);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_ROP3_WHITENESS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2400), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2404), ==,
                    UINT32_MAX);

    /* An absent pattern brush must not silently become a solid brush. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2440, 0xa5,
                 2 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2440);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH,
                 2 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x11223344);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_CLR_CMP_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_NONE |
                 ATI_GMC_DST_32BPP | ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2440), ==,
                    0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x2444), ==,
                    0xa5a5a5a5);

    /* Radeon source function 4 skips pixels equal to the transparency key. */
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x24c0, 0x11223344);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x24c4, 0x55667788);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x24d0, 0xaabbccdd);
    qtest_writel(qts, IA64_RV100_FB_BASE + 0x24d4, 0xaabbccdd);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_OFFSET, 0x24c0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_PITCH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x24d0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 8);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_CLR_SRC,
                 0x11223344);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_MASK,
                 UINT32_MAX);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_CNTL,
                 (1U << 24) | 4U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_NONE |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SRC_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x24d0), ==,
                    0xaabbccdd);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + 0x24d4), ==,
                    0x55667788);

    /* Radeon scissors use signed-magnitude coordinates and an exclusive RB. */
    qtest_memset(qts, IA64_RV100_FB_BASE + 0x2480, 0xa5, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0x2480);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH, 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT,
                 (1U << 15) | 1U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 2U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR, 0x5a);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                 ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_SOLID |
                 ATI_GMC_DST_8BPP | ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE + 0x2480), ==,
                    0x5a);
    g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE + 0x2481), ==,
                    0x5a);
    g_assert_cmphex(qtest_readw(qts, IA64_RV100_FB_BASE + 0x2482), ==,
                    0xa5a5);

    /* A cold reset restores CRTC defaults and clears the 2D register file. */
    qtest_system_reset(qts);
    ati_pci_enable(qts);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_GEN_INT_STATUS), ==,
                    ATI_R100_GEN_INT_STATUS_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_CRTC_GEN_CNTL), ==,
                    ATI_CRTC_GEN_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_CRTC_EXT_CNTL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DAC_CNTL), ==,
                    ATI_R100_DAC_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_CRTC_OFFSET_CNTL), ==,
                    ATI_RV100_CRTC_OFFSET_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DST_OFFSET), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DST_PITCH), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_GUI_MASTER_CNTL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_WRITE_MASK), ==, 0);

    qtest_quit(qts);
}

static void ati_rage128_vsync(void)
{
    enum {
        VTOTAL = 8,
        VDISPLAY = 4,
        VLINE = 5,
        VSYNC_START = 6,
    };
    QTestState *qts;
    uint32_t status;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p");
    qtest_irq_intercept_in(qts, "/machine/unattached/device[1]");
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS),
                    ==, ATI_CRTC_FIX_VSYNC_TIMING);

    qtest_writew(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP,
                 VTOTAL - 1);
    qtest_writew(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP + 2,
                 VDISPLAY - 1);
    g_assert_cmpuint(qtest_readw(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_V_TOTAL_DISP), ==,
                     VTOTAL - 1);
    g_assert_cmpuint(qtest_readw(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_V_TOTAL_DISP + 2), ==,
                     VDISPLAY - 1);
    qtest_writew(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_SYNC_STRT_WID,
                 VSYNC_START - 1);
    qtest_writew(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_SYNC_STRT_WID + 2,
                 1);
    g_assert_cmpuint(qtest_readw(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_V_SYNC_STRT_WID + 2), ==, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_VLINE_CRNT_VLINE,
                 VLINE);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                 ATI_CRTC_VSYNC_INT);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_8);

    /* Status latches independently of the interrupt enable mask. */
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS * VDISPLAY / VTOTAL + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                             ATI_CRTC_VSYNC_INT), ==,
                    ATI_CRTC_VBLANK_INT);
    g_assert_cmpuint(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_CRNT_FRAME), ==, 0);
    g_assert_cmpuint(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_VLINE_CRNT_VLINE) >> 16,
                     ==, VDISPLAY);
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE),
                    ==, ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE);
    g_assert_false(qtest_get_irq(qts, 17));

    /* The mask controls the IRQ pin, but not event generation or latches. */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_CNTL,
                 ATI_CRTC_VBLANK_INT);
    g_assert_true(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_CNTL, 0);
    g_assert_false(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VBLANK_INT);
    qtest_writeb(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS,
                 ATI_CRTC_VBLANK_SAVE);
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                             ATI_CRTC_FIX_VSYNC_TIMING), ==,
                    ATI_CRTC_VBLANK_CUR | ATI_CRTC_FIX_VSYNC_TIMING);

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS / VTOTAL + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                             ATI_CRTC_VSYNC_INT), ==,
                    ATI_CRTC_VLINE_INT);
    g_assert_cmpuint(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_VLINE_CRNT_VLINE) >> 16,
                     ==, VLINE);
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                             ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                    ATI_CRTC_VBLANK_CUR | ATI_CRTC_VLINE_SYNC);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VLINE_INT);

    /* The X.Org Rage128 path acknowledges VSYNC, then polls without a mask. */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VSYNC_INT);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS / VTOTAL + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                             ATI_CRTC_VSYNC_INT), ==,
                    ATI_CRTC_VSYNC_INT);
    g_assert_cmpuint(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_CRNT_FRAME), ==, 1);
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                             ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                    ATI_CRTC_VBLANK_CUR | ATI_CRTC_FRAME);
    g_assert_false(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_CNTL,
                 ATI_CRTC_VSYNC_INT);
    g_assert_true(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_CRTC_VSYNC_INT);
    g_assert_false(qtest_get_irq(qts, 17));

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS *
                          (VTOTAL - VSYNC_START) / VTOTAL + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmpuint(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CRTC_VLINE_CRNT_VLINE) >> 16,
                     ==, 0);
    status = qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_STATUS);
    g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                             ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                    ATI_CRTC_FRAME);

    qtest_quit(qts);
}

static void ati_source_datatype_alias(void)
{
    static const struct {
        const char *model;
        uint64_t mmio;
        bool legacy;
    } devices[] = {
        { "rv100", IA64_RV100_MMIO_BASE, false },
        { "es1000", IA64_RV100_MMIO_BASE, false },
        { "rage128p", IA64_ATI_MMIO_BASE, true },
    };
    const uint32_t gui_base = ATI_GMC_DST_PITCH | ATI_GMC_BRUSH_NONE |
                              ATI_GMC_DST_32BPP | ATI_GMC_ROP3_SRCCOPY |
                              ATI_GMC_DP_SRC_HOST;
    const uint32_t datatype_base = 0xf00 | ATI_DP_DST_32BPP;

    for (unsigned int i = 0; i < ARRAY_SIZE(devices); i++) {
        const uint64_t mmio = devices[i].mmio;
        const uint32_t legacy_control = devices[i].legacy ? 1U << 27 : 0;
        const uint32_t extended_type = devices[i].legacy ? 0 : 1U << 27;
        const struct {
            unsigned int reg;
            uint32_t value;
            uint32_t gui;
            uint32_t datatype;
        } steps[] = {
            {
                ATI_DP_GUI_MASTER_CNTL,
                gui_base | (1U << 27) | (1U << 12),
                gui_base | (1U << 27) | (1U << 12),
                datatype_base | ((devices[i].legacy ? 1U : 5U) << 16),
            }, {
                ATI_DP_DATATYPE,
                datatype_base | (3U << 16),
                gui_base | legacy_control | (3U << 12),
                datatype_base | (3U << 16),
            }, {
                ATI_DP_GUI_MASTER_CNTL,
                gui_base | (3U << 12),
                gui_base | (3U << 12),
                datatype_base | (3U << 16),
            }, {
                ATI_DP_DATATYPE,
                datatype_base | (5U << 16),
                gui_base | extended_type | (1U << 12),
                datatype_base | (5U << 16),
            },
        };
        g_autofree char *path = g_build_filename(
            g_get_tmp_dir(), "ati-source-datatype-alias.XXXXXX", NULL);
        g_autofree char *uri = NULL;
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", devices[i].model);
        int fd;

        ati_pci_enable(qts);
        /* Raw writes must update both directions of the source-type alias. */
        for (unsigned int step = 0; step < ARRAY_SIZE(steps); step++) {
            qtest_writel(qts, mmio + steps[step].reg, steps[step].value);
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_GUI_MASTER_CNTL),
                            ==, steps[step].gui);
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_DATATYPE),
                            ==, steps[step].datatype);
        }

        /* Preserve a type set through DP_DATATYPE across file migration. */
        fd = g_mkstemp(path);
        g_assert_cmpint(fd, >=, 0);
        close(fd);
        uri = g_strdup_printf("file:%s", path);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
        display_wait_for_migration(qts);
        qtest_quit(qts);
        qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s -incoming defer",
            devices[i].model);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate-incoming','arguments':"
                 "{'uri':%s,'exit-on-error':false}}", uri);
        display_wait_for_migration(qts);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_GUI_MASTER_CNTL),
                        ==, gui_base | extended_type | (1U << 12));
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_DATATYPE),
                        ==, datatype_base | (5U << 16));

        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     gui_base | (3U << 12));
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_GUI_MASTER_CNTL),
                        ==, gui_base | (3U << 12));
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_DATATYPE),
                        ==, datatype_base | (3U << 16));
        qtest_quit(qts);
        g_assert_cmpint(g_unlink(path), ==, 0);
    }
}

static void ati_crtc_timing_migration(void)
{
    static const struct {
        const char *model;
        uint64_t mmio;
    } devices[] = {
        { "rage128p", IA64_ATI_MMIO_BASE },
        { "rv100", IA64_RV100_MMIO_BASE },
        { "es1000", IA64_RV100_MMIO_BASE },
    };
    enum {
        VTOTAL = 8,
        VDISPLAY = 4,
        VLINE = 5,
        VSYNC_START = 6,
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(devices); i++) {
        g_autofree char *path = g_build_filename(
            g_get_tmp_dir(), "ati-crtc-timing-migration.XXXXXX", NULL);
        g_autofree char *uri = NULL;
        const uint64_t mmio = devices[i].mmio;
        QTestState *qts;
        uint32_t status;
        int fd;

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s",
                          devices[i].model);
        ati_pci_enable(qts);

        /*
         * PLL writes require PLL_WR_EN; UPDATE_W is acknowledged immediately.
         */
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_INDEX, ATI_PPLL_DIV_3);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_DATA, 0xdeadbeef);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_CLOCK_CNTL_DATA), ==, 0);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_INDEX,
                     ATI_PLL_DIV_SEL_3 | ATI_PLL_WR_EN | ATI_PPLL_DIV_3);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_DATA, 0x0003005a);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_INDEX,
                     ATI_PLL_DIV_SEL_3 | ATI_PPLL_DIV_3);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_CLOCK_CNTL_DATA), ==,
                        0x0003005a);
        qtest_writeb(qts, mmio + ATI_CLOCK_CNTL_INDEX,
                     ATI_PLL_WR_EN | ATI_PPLL_REF_DIV);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_DATA,
                     ATI_PPLL_ATOMIC_UPDATE | 0x2d);
        qtest_writeb(qts, mmio + ATI_CLOCK_CNTL_INDEX, ATI_PPLL_REF_DIV);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_CLOCK_CNTL_DATA), ==,
                        0x2d);
        qtest_writel(qts, mmio + ATI_CLOCK_CNTL_INDEX,
                     ATI_PLL_DIV_SEL_3 | ATI_PPLL_DIV_3);

        qtest_writel(qts, mmio + ATI_CRTC_V_TOTAL_DISP,
                     (VTOTAL - 1) | ((VDISPLAY - 1) << 16));
        qtest_writel(qts, mmio + ATI_CRTC_V_SYNC_STRT_WID,
                     (VSYNC_START - 1) | (1 << 16));
        qtest_writel(qts, mmio + ATI_CRTC_VLINE_CRNT_VLINE, VLINE);
        qtest_writel(qts, mmio + ATI_GEN_INT_CNTL, 0);
        qtest_writel(qts, mmio + ATI_GEN_INT_STATUS,
                     ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                     ATI_CRTC_VSYNC_INT);
        qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL,
                     ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                     ATI_CRTC_PIX_WIDTH_8);
        if (!strcmp(devices[i].model, "rage128p")) {
            qtest_writeb(qts, mmio + ATI_CRTC_STATUS + 3, 0);
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_STATUS) &
                            ATI_CRTC_FIX_VSYNC_TIMING, ==, 0);
        }

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts,
                         ATI_VBLANK_FRAME_NS * VDISPLAY / VTOTAL + 1);
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_GEN_INT_STATUS) &
                        (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                         ATI_CRTC_VSYNC_INT), ==,
                        ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                        ATI_CRTC_VSYNC_INT);
        g_assert_cmpuint(qtest_readl(qts, mmio +
                                     ATI_CRTC_VLINE_CRNT_VLINE), ==,
                         (VDISPLAY << 16) | VLINE);
        g_assert_cmpuint(qtest_readl(qts, mmio + ATI_CRTC_CRNT_FRAME), ==,
                         1);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                        ATI_CRTC_FRAME);
        qtest_writel(qts, mmio + ATI_GEN_INT_STATUS,
                     ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                     ATI_CRTC_VSYNC_INT);

        if (strcmp(devices[i].model, "rage128p")) {
            qtest_writel(qts, mmio + ATI_CRTC_EXT_CNTL, 0x8000);
            qtest_writel(qts, mmio + ATI_DAC_EXT_CNTL, 0x18030);
            qtest_writel(qts, mmio + ATI_DAC_MACRO_CNTL, 0x20000);
            qtest_writel(qts, mmio + ATI_DAC_CNTL, 0xff00000a);
        }

        fd = g_mkstemp(path);
        g_assert_cmpint(fd, >=, 0);
        close(fd);
        uri = g_strdup_printf("file:%s", path);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
        display_wait_for_migration(qts);
        qtest_quit(qts);

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s "
                          "-incoming defer", devices[i].model);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate-incoming','arguments':"
                 "{'uri':%s,'exit-on-error':false}}", uri);
        display_wait_for_migration(qts);

        g_assert_cmphex(qtest_readl(qts, mmio + ATI_CLOCK_CNTL_INDEX), ==,
                        ATI_PLL_DIV_SEL_3 | ATI_PPLL_DIV_3);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_CLOCK_CNTL_DATA), ==,
                        0x0003005a);

        if (strcmp(devices[i].model, "rage128p")) {
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_EXT_CNTL), ==,
                            0x18030);
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_MACRO_CNTL), ==,
                            0x20000);
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_DAC_CNTL) &
                            ATI_DAC_CMP_OUTPUT, ==, ATI_DAC_CMP_OUTPUT);
        }

        if (!strcmp(devices[i].model, "rage128p")) {
            g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_STATUS) &
                            ATI_CRTC_FIX_VSYNC_TIMING, ==, 0);
        }
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_GEN_INT_STATUS) &
                        (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                         ATI_CRTC_VSYNC_INT), ==, 0);
        g_assert_cmpuint(qtest_readl(qts, mmio +
                                     ATI_CRTC_VLINE_CRNT_VLINE), ==,
                         (VDISPLAY << 16) | VLINE);
        g_assert_cmpuint(qtest_readl(qts, mmio + ATI_CRTC_CRNT_FRAME), ==,
                         1);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE |
                        ATI_CRTC_FRAME);
        qtest_writel(qts, mmio + ATI_CRTC_STATUS, ATI_CRTC_VBLANK_SAVE);

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS / VTOTAL + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_GEN_INT_STATUS) &
                        (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                         ATI_CRTC_VSYNC_INT), ==, ATI_CRTC_VLINE_INT);
        g_assert_cmpuint(qtest_readl(qts, mmio +
                                     ATI_CRTC_VLINE_CRNT_VLINE), ==,
                         (VLINE << 16) | VLINE);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        ATI_CRTC_VBLANK_CUR | ATI_CRTC_VLINE_SYNC |
                        ATI_CRTC_FRAME);
        qtest_writel(qts, mmio + ATI_GEN_INT_STATUS, ATI_CRTC_VLINE_INT);

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS / VTOTAL + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_GEN_INT_STATUS) &
                        (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                         ATI_CRTC_VSYNC_INT), ==, ATI_CRTC_VSYNC_INT);
        g_assert_cmpuint(qtest_readl(qts, mmio + ATI_CRTC_CRNT_FRAME), ==,
                         2);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        ATI_CRTC_VBLANK_CUR);
        qtest_writel(qts, mmio + ATI_GEN_INT_STATUS, ATI_CRTC_VSYNC_INT);

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS *
                              (VTOTAL - VSYNC_START) / VTOTAL + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmpuint(qtest_readl(qts, mmio +
                                     ATI_CRTC_VLINE_CRNT_VLINE), ==, VLINE);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        0);

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts,
                         ATI_VBLANK_FRAME_NS * VDISPLAY / VTOTAL + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_GEN_INT_STATUS) &
                        (ATI_CRTC_VBLANK_INT | ATI_CRTC_VLINE_INT |
                         ATI_CRTC_VSYNC_INT), ==, ATI_CRTC_VBLANK_INT);
        status = qtest_readl(qts, mmio + ATI_CRTC_STATUS);
        g_assert_cmphex(status & (ATI_CRTC_VBLANK_CUR |
                                 ATI_CRTC_VBLANK_SAVE |
                                 ATI_CRTC_VLINE_SYNC | ATI_CRTC_FRAME), ==,
                        ATI_CRTC_VBLANK_CUR | ATI_CRTC_VBLANK_SAVE);

        qtest_quit(qts);
        g_assert_cmpint(g_unlink(path), ==, 0);
    }
}

static void ati_rage128_host_data(void)
{
    static const uint8_t fill_expected[] = {
        0x33, 0x22, 0x11, 0x33, 0x22, 0x11,
        0x33, 0x22, 0x11, 0x33, 0x22, 0x11,
    };
    static const uint32_t host_24_words[] = {
        0x44332211, 0x88776655, 0xccbbaa99,
        0x01ffeedd, 0xa5a50302,
    };
    static const uint8_t host_24_expected[] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
        0xdd, 0xee, 0xff, 0x01, 0x02, 0x03,
    };
    static const uint32_t packed_rows_words[] = {
        0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d,
        0x14131211, 0x18171615, 0x1c1b1a19, 0x201f1e1d,
        0x24232221,
    };
    static const uint8_t packed_rows_expected[][18] = {
        {
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
            0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
        }, {
            0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
            0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
            0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
        },
    };
    static const uint32_t ring_seed[] = {
        0x01010101, 0x02020202, 0x03030303, 0x04040404,
        0x05050505, 0x06060606, 0x07070707, 0x08080808,
    };
    uint8_t fill_actual[sizeof(fill_expected)];
    uint8_t host_24_actual[sizeof(host_24_expected)];
    uint8_t packed_rows_actual[sizeof(packed_rows_expected[0])];
    uint8_t packed_clip_actual[12];
    QTestState *qts;
    unsigned int i;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p");
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DEFAULT_SC_BOTTOM_RIGHT,
                 0x1fff1fff);

    /* Rage128 GUI master writes force both destination directions positive. */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL),
                    ==, ATI_DST_LTR_TTB);

    /* Rage128 packed 24-bpp destinations use byte-addressed scissors. */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 2);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 11);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x00112233);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_24BPP |
                 ATI_GMC_ROP3_PATCOPY);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_CLR_CMP_MASK), ==, UINT32_MAX);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 4);
    qtest_memread(qts, IA64_ATI_FB_BASE, fill_actual,
                  sizeof(fill_actual));
    g_assert_cmpmem(fill_actual, sizeof(fill_actual), fill_expected,
                    sizeof(fill_expected));

    /* Packed 24-bpp pixels remain contiguous across 128-bit FIFO banks. */
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x2500, 0xa5,
                 sizeof(host_24_expected));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2500);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 3);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 17);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_24BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 6);
    for (i = 0; i < ARRAY_SIZE(host_24_words) - 1; i++) {
        qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA0 + i * 4,
                     host_24_words[i]);
    }
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA_LAST,
                 host_24_words[ARRAY_SIZE(host_24_words) - 1]);
    qtest_memread(qts, IA64_ATI_FB_BASE + 0x2500, host_24_actual,
                  sizeof(host_24_actual));
    g_assert_cmpmem(host_24_actual, sizeof(host_24_actual),
                    host_24_expected, sizeof(host_24_expected));

    /* Packed 24-bpp pixels remain contiguous across scanlines. */
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x2600, 0xa5, 48);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2600);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 3);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | 17U);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 2);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 6);
    for (i = 0; i < ARRAY_SIZE(packed_rows_words) - 1; i++) {
        qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA0 +
                     (i % 4) * 4,
                     packed_rows_words[i]);
    }
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA_LAST,
                 packed_rows_words[ARRAY_SIZE(packed_rows_words) - 1]);
    for (i = 0; i < ARRAY_SIZE(packed_rows_expected); i++) {
        qtest_memread(qts, IA64_ATI_FB_BASE + 0x2600 + i * 24,
                      packed_rows_actual, sizeof(packed_rows_actual));
        g_assert_cmpmem(packed_rows_actual, sizeof(packed_rows_actual),
                        packed_rows_expected[i],
                        sizeof(packed_rows_expected[i]));
    }

    /* Packed-24 scissor X coordinates are byte positions, not pixels. */
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x2700, 0xa5,
                 sizeof(packed_clip_actual));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2700);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 2);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 3);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x00a1b2c3);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_SOLID |
                 ATI_GMC_DST_24BPP | ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 4);
    qtest_memread(qts, IA64_ATI_FB_BASE + 0x2700, packed_clip_actual,
                  sizeof(packed_clip_actual));
    g_assert_cmpmem(packed_clip_actual, sizeof(packed_clip_actual),
                    ((const uint8_t[]) {
                        0xa5, 0xa5, 0xa5, 0xc3, 0xb2, 0xa1,
                        0xc3, 0xb2, 0xa1, 0xa5, 0xa5, 0xa5,
                    }), sizeof(packed_clip_actual));

    /* Rage128 source transparency uses function 5 to skip the key. */
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2740, 0x11223344);
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2744, 0x55667788);
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2760, 0xaabbccdd);
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2764, 0xaabbccdd);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SRC_OFFSET, 0x2740);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SRC_PITCH, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2760);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CLR_SRC,
                 0x11223344);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_MASK, UINT32_MAX);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CNTL,
                 (1U << 24) | 5U);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_NONE |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SRC_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SRC_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_FB_BASE + 0x2760), ==,
                    0xaabbccdd);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_FB_BASE + 0x2764), ==,
                    0x55667788);

    /* Destination function 4 preserves pixels equal to the key. */
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2780, 0x11223344);
    qtest_writel(qts, IA64_ATI_FB_BASE + 0x2784, 0x55667788);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2780);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CLR_DST,
                 0x11223344);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CNTL, 4U << 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0xdecafbad);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_32BPP |
                 ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 2);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_FB_BASE + 0x2780), ==,
                    0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_FB_BASE + 0x2784), ==,
                    0xdecafbad);

    /* Host-data writes preserve submission order across buffer flushes. */
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x2800, 0xa5,
                 sizeof(ring_seed));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2800);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 7);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_CLR_CMP_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 8);
    for (i = 0; i < ARRAY_SIZE(ring_seed); i++) {
        qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA0 +
                     (i % 4) * 4, ring_seed[i]);
    }
    for (i = 0; i < ARRAY_SIZE(ring_seed); i++) {
        g_assert_cmphex(qtest_readl(qts, IA64_ATI_FB_BASE + 0x2800 + i * 4),
                        ==, ring_seed[i]);
    }

    /* Rage128 uses DP_DATATYPE bit 29 rather than RBBM_GUICNTL. */
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x2a00, 0, 4);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x2a00);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_16BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_DATATYPE,
                 ATI_DP_HOST_BIG_ENDIAN | ATI_DP_SRC_COLOR |
                 ATI_DP_DST_16BPP);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_MIX,
                 ATI_DP_MIX_ROP3_SRCCOPY | ATI_DP_MIX_SRC_HOST);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 2);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA_LAST,
                 0x11223344);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_FB_BASE + 0x2a00), ==,
                    0x4433);
    g_assert_cmphex(qtest_readw(qts, IA64_ATI_FB_BASE + 0x2a02), ==,
                    0x2211);

    qtest_quit(qts);
}

static void ati_8x8_pattern_brush(void)
{
    static const char *models[] = {
        "rv100", "es1000",
    };
    static const uint32_t mono_pattern[] = {
        0x08040201, 0x80402010,
    };
    enum {
        width = 10,
        height = 10,
        stride = 16,
    };

    for (unsigned int model = 0; model < ARRAY_SIZE(models); model++) {
        const uint64_t fb = IA64_RV100_FB_BASE;
        const uint64_t mmio = IA64_RV100_MMIO_BASE;
        uint8_t source[stride * height];
        uint8_t actual[stride * height];
        QTestState *qts;
        uint32_t scissor;
        uint32_t common;

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s",
                          models[model]);
        ati_pci_enable(qts);
        scissor = (height << 16) | width;
        common = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                 ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_8BPP;
        qtest_writel(qts, mmio + ATI_DST_PITCH, stride);
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 0);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT, scissor);
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_BRUSH_Y_X, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_BRUSH_Y_X), ==,
                        0x00000707);
        qtest_writel(qts, mmio + ATI_BRUSH_Y_X, (1U << 8) | 2U);
        qtest_writel(qts, mmio + ATI_DP_BRUSH_BKGD_CLR, 0x1b);
        qtest_writel(qts, mmio + ATI_DP_BRUSH_FRGD_CLR, 0xe1);
        for (unsigned int i = 0; i < ARRAY_SIZE(mono_pattern); i++) {
            qtest_writel(qts, mmio + ATI_BRUSH_DATA0 + i * 4,
                         mono_pattern[i]);
        }

        /* Opaque mono uses row-major bytes and LSB-first pixel polarity. */
        qtest_memset(qts, fb + 0x4000, 0xa5, sizeof(actual));
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4000);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_BYTE_LSB_TO_MSB |
                     ATI_GMC_ROP3_PATCOPY);
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, height);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        qtest_memread(qts, fb + 0x4000, actual, sizeof(actual));
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < stride; x++) {
                uint8_t expected = 0xa5;

                if (x < width) {
                    expected = (((x - 2) & 7) == ((y - 1) & 7)) ?
                               0xe1 : 0x1b;
                }
                g_assert_cmphex(actual[y * stride + x], ==, expected);
            }
        }

        /* MSB-first polarity maps the high bit to the first pattern pixel. */
        qtest_memset(qts, fb + 0x4100, 0xa5, stride);
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4100);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_ROP3_PATCOPY);
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        g_assert_cmphex(qtest_readb(qts, fb + 0x4102), ==, 0xe1);
        g_assert_cmphex(qtest_readb(qts, fb + 0x4109), ==, 0x1b);

        /* The transparent mono brush leaves its zero bits untouched. */
        qtest_memset(qts, fb + 0x4200, 0x5a, sizeof(actual));
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4200);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_BRUSH_MONO_FG_LA |
                     ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_PATCOPY);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, height);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        qtest_memread(qts, fb + 0x4200, actual, sizeof(actual));
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < stride; x++) {
                uint8_t expected = 0x5a;

                if (x < width &&
                    ((x - 2) & 7) == ((y - 1) & 7)) {
                    expected = 0xe1;
                }
                g_assert_cmphex(actual[y * stride + x], ==, expected);
            }
        }

        /* 8-bpp color pixels are packed four per brush-data DWORD. */
        for (unsigned int i = 0; i < 16; i++) {
            uint32_t first = 0x40 + i * 4;

            qtest_writel(qts, mmio + ATI_BRUSH_DATA0 + i * 4,
                         first | (first + 1) << 8 |
                         (first + 2) << 16 | (first + 3) << 24);
        }
        qtest_memset(qts, fb + 0x4400, 0xa5, sizeof(actual));
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4400);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_BRUSH_COLOR |
                     ATI_GMC_ROP3_PATCOPY);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, height);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        qtest_memread(qts, fb + 0x4400, actual, sizeof(actual));
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < stride; x++) {
                uint8_t expected = 0xa5;

                if (x < width) {
                    expected = 0x40 + ((y - 1) & 7) * 8 +
                               ((x - 2) & 7);
                }
                g_assert_cmphex(actual[y * stride + x], ==, expected);
            }
        }

        /* Pattern/source and pattern/destination ROP3 inputs stay distinct. */
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < stride; x++) {
                source[y * stride + x] = x * 3 + y;
            }
        }
        qtest_memwrite(qts, fb + 0x4600, source, sizeof(source));
        qtest_memset(qts, fb + 0x4800, 0xa5, sizeof(actual));
        qtest_writel(qts, mmio + ATI_SRC_OFFSET, 0x4600);
        qtest_writel(qts, mmio + ATI_SRC_PITCH, stride);
        qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT, scissor);
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4800);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_SRC_PITCH | ATI_GMC_SRC_CLIPPING |
                     ATI_GMC_BRUSH_COLOR |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_PATXORSRC |
                     ATI_GMC_DP_SRC_RECT);
        qtest_writel(qts, mmio + ATI_SRC_X, 0);
        qtest_writel(qts, mmio + ATI_SRC_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, height);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        qtest_memread(qts, fb + 0x4800, actual, sizeof(actual));
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                uint8_t pattern = 0x40 + ((y - 1) & 7) * 8 +
                                  ((x - 2) & 7);

                g_assert_cmphex(actual[y * stride + x], ==,
                                pattern ^ source[y * stride + x]);
            }
        }

        qtest_memset(qts, fb + 0x4a00, 0xf3, sizeof(actual));
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0x4a00);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     common | ATI_GMC_BRUSH_COLOR |
                     ATI_GMC_ROP3_PATINVERT);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, height);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
        qtest_memread(qts, fb + 0x4a00, actual, sizeof(actual));
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                uint8_t pattern = 0x40 + ((y - 1) & 7) * 8 +
                                  ((x - 2) & 7);

                g_assert_cmphex(actual[y * stride + x], ==,
                                pattern ^ 0xf3);
            }
        }

        qtest_quit(qts);
    }
}

static void ati_rage128_host_data_migration(void)
{
    static const uint32_t words[] = {
        0x03020100, 0x07060504, 0x0b0a0908,
        0x0f0e0d0c, 0x13121110, 0x17161514,
    };
    static const uint8_t expected[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
        0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    };
    g_autofree char *path = g_strdup_printf(
        "%s/ati-rage128-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    uint8_t actual[sizeof(expected)];
    QTestState *qts;
    int fd;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p");
    qtest_memset(qts, IA64_ATI_FB_BASE + 0x3000, 0, sizeof(expected));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0x3000);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH, 3);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT, 23);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_DST_24BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CNTL, 1U << 24);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CLR_SRC,
                 0x10203040);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_CLR_DST,
                 0x50607080);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CLR_CMP_MASK,
                 0xff00ff00);
    for (unsigned int i = 0; i < ARRAY_SIZE(words) - 1; i++) {
        qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA0 +
                     (i % 4) * 4, words[i]);
    }
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_Y_X, 0x00120304);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_DATA0, 0x89abcdef);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_DATA0 + 63 * 4,
                 0x01234567);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_Y_X),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_DATA0),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_DATA0 +
                                     63 * 4), ==, 0);

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p "
                     "-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CLR_CMP_CNTL), ==, 1U << 24);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CLR_CMP_CLR_SRC), ==, 0x10203040);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CLR_CMP_CLR_DST), ==, 0x50607080);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                     ATI_CLR_CMP_MASK), ==, 0xff00ff00);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_Y_X),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE + ATI_BRUSH_DATA0),
                    ==, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_HOST_DATA_LAST,
                 words[ARRAY_SIZE(words) - 1]);
    qtest_memread(qts, IA64_ATI_FB_BASE + 0x3000, actual,
                  sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void vbe_legacy_data_port(void)
{
    QTestState *qts;
    uint16_t id;

    if (g_str_equal(qtest_get_arch(), "ia64")) {
        qts = qtest_init("-machine ia64-vpc,nvram=none -vga std");
        qtest_writew(qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX),
                     VBE_DISPI_INDEX_ID);
        id = qtest_readw(
            qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX + 2));
        g_assert_cmphex(id, ==, VBE_DISPI_ID5);

        qtest_writew(qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX),
                     VBE_DISPI_INDEX_ENABLE);
        qtest_writew(qts,
                     IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX + 2),
                     VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        g_assert_cmphex(qtest_readw(
            qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX + 2)), ==,
            VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_INDEX),
                     VGA_SEQ_RESET);
        qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 1);
        g_assert_cmphex(qtest_readw(
            qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX + 2)), ==, 0);
    } else {
        qts = qtest_init("-vga none -device VGA");
        legacy_outw_le(qts, VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
        id = legacy_inw_le(qts, VBE_DISPI_IOPORT_INDEX + 2);
        g_assert_cmphex(id, ==, VBE_DISPI_ID5);
        g_assert_cmphex(legacy_inw_le(qts, VBE_DISPI_IOPORT_DATA), ==, id);
    }
    qtest_quit(qts);
}

static void vga_wide_planar_access(void)
{
    const uint64_t plane0 = 0x0123456789abcdefULL;
    const uint64_t plane2 = 0xfedcba9876543210ULL;
    const uint64_t unaligned = 0x55aa996633ccf00fULL;
    const uint64_t colors = 0x0f0e0d0c0b0a0908ULL;
    const uint64_t expanded[4] = {
        0xff00ff00ff00ff00ULL,
        0xffff0000ffff0000ULL,
        0xffffffff00000000ULL,
        0xffffffffffffffffULL,
    };
    QTestState *qts;
    unsigned plane;

    qts = qtest_init("-machine ia64-vpc,nvram=none -vga std -S");

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_INDEX),
                 VGA_SEQ_MEMORY_MODE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 0x06);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX), VGA_GFX_MISC);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0x01);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX), VGA_GFX_MODE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_SR_ENABLE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_DATA_ROTATE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_BIT_MASK);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0xff);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_INDEX),
                 VGA_SEQ_PLANE_WRITE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 1U << 0);
    qtest_writeq(qts, IA64_VGA_LEGACY_BASE, plane0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 1U << 2);
    qtest_writeq(qts, IA64_VGA_LEGACY_BASE, plane2);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_PLANE_READ);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0);
    g_assert_cmphex(qtest_readq(qts, IA64_VGA_LEGACY_BASE), ==, plane0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 2);
    g_assert_cmphex(qtest_readq(qts, IA64_VGA_LEGACY_BASE), ==, plane2);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_INDEX),
                 VGA_SEQ_PLANE_WRITE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 1U << 1);
    qtest_writeq(qts, IA64_VGA_LEGACY_BASE + 1, unaligned);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_PLANE_READ);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 1);
    g_assert_cmphex(qtest_readq(qts, IA64_VGA_LEGACY_BASE + 1), ==,
                    unaligned);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 0x0f);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX), VGA_GFX_MODE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 2);
    qtest_writeq(qts, IA64_VGA_LEGACY_BASE + 0x100, colors);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX), VGA_GFX_MODE);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), 0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_INDEX),
                 VGA_GFX_PLANE_READ);
    for (plane = 0; plane < 4; plane++) {
        qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_GFX_DATA), plane);
        g_assert_cmphex(qtest_readq(qts, IA64_VGA_LEGACY_BASE + 0x100), ==,
                        expanded[plane]);
    }

    qtest_quit(qts);
}

static uint16_t ati_vbe_read(QTestState *qts, uint16_t index)
{
    qtest_writew(qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX),
                 index);
    return qtest_readw(
        qts, IA64_LEGACY_IO_PORT_PA(VBE_DISPI_IOPORT_INDEX + 2));
}

static char *ppm_next_token(const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *start;

    while (*cursor < end) {
        if (g_ascii_isspace(**cursor)) {
            (*cursor)++;
            continue;
        }
        if (**cursor == '#') {
            while (*cursor < end && **cursor != '\n') {
                (*cursor)++;
            }
            continue;
        }
        break;
    }
    g_assert_cmpuint(end - *cursor, >, 0);
    start = *cursor;
    while (*cursor < end && !g_ascii_isspace(**cursor) && **cursor != '#') {
        (*cursor)++;
    }
    g_assert_cmpuint(*cursor - start, >, 0);
    return g_strndup((const char *)start, *cursor - start);
}

static void assert_ppm_stride(const char *filename, unsigned width,
                              unsigned height)
{
    g_autofree char *contents = NULL;
    g_autofree char *magic = NULL;
    g_autofree char *width_token = NULL;
    g_autofree char *height_token = NULL;
    g_autofree char *max_token = NULL;
    g_autoptr(GError) error = NULL;
    const uint8_t *cursor;
    const uint8_t *end;
    const uint8_t *row0;
    const uint8_t *row1;
    gsize length;

    g_assert_true(g_file_get_contents(filename, &contents, &length, &error));
    g_assert_no_error(error);
    cursor = (const uint8_t *)contents;
    end = cursor + length;
    magic = ppm_next_token(&cursor, end);
    width_token = ppm_next_token(&cursor, end);
    height_token = ppm_next_token(&cursor, end);
    max_token = ppm_next_token(&cursor, end);
    g_assert_cmpstr(magic, ==, "P6");
    g_assert_cmpuint(g_ascii_strtoull(width_token, NULL, 10), ==, width);
    g_assert_cmpuint(g_ascii_strtoull(height_token, NULL, 10), ==, height);
    g_assert_cmpuint(g_ascii_strtoull(max_token, NULL, 10), ==, 255);

    g_assert_true(cursor < end && g_ascii_isspace(*cursor));
    if (*cursor++ == '\r' && cursor < end && *cursor == '\n') {
        cursor++;
    }
    g_assert_cmpuint(end - cursor, >=, (gsize)width * height * 3);
    row0 = cursor;
    row1 = cursor + width * 3;

    /* Both markers are visible only if row 1 starts at the virtual pitch. */
    g_assert_cmpmem(row0, 3, row1, 3);
    g_assert_cmpint(memcmp(row0, row0 + 3, 3), !=, 0);
}

static void assert_ppm_pixel(const char *filename, unsigned width,
                             unsigned height, unsigned x, unsigned y,
                             uint8_t red, uint8_t green, uint8_t blue)
{
    g_autofree char *contents = NULL;
    g_autofree char *magic = NULL;
    g_autofree char *width_token = NULL;
    g_autofree char *height_token = NULL;
    g_autofree char *max_token = NULL;
    g_autoptr(GError) error = NULL;
    const uint8_t *cursor;
    const uint8_t *end;
    const uint8_t *pixel;
    gsize length;

    g_assert_true(g_file_get_contents(filename, &contents, &length, &error));
    g_assert_no_error(error);
    cursor = (const uint8_t *)contents;
    end = cursor + length;
    magic = ppm_next_token(&cursor, end);
    width_token = ppm_next_token(&cursor, end);
    height_token = ppm_next_token(&cursor, end);
    max_token = ppm_next_token(&cursor, end);
    g_assert_cmpstr(magic, ==, "P6");
    g_assert_cmpuint(g_ascii_strtoull(width_token, NULL, 10), ==, width);
    g_assert_cmpuint(g_ascii_strtoull(height_token, NULL, 10), ==, height);
    g_assert_cmpuint(g_ascii_strtoull(max_token, NULL, 10), ==, 255);
    g_assert_cmpuint(x, <, width);
    g_assert_cmpuint(y, <, height);

    g_assert_true(cursor < end && g_ascii_isspace(*cursor));
    if (*cursor++ == '\r' && cursor < end && *cursor == '\n') {
        cursor++;
    }
    g_assert_cmpuint(end - cursor, >=, (gsize)width * height * 3);
    pixel = cursor + ((gsize)y * width + x) * 3;
    g_assert_cmphex(pixel[0], ==, red);
    g_assert_cmphex(pixel[1], ==, green);
    g_assert_cmphex(pixel[2], ==, blue);
}

static void ati_crtc_live_mode(void)
{
    static const struct {
        const char *model;
        uint64_t mmio;
        uint64_t fb;
    } devices[] = {
        { "rage128p", IA64_ATI_MMIO_BASE, IA64_ATI_FB_BASE },
        { "rv100", IA64_RV100_MMIO_BASE, IA64_RV100_FB_BASE },
        { "es1000", IA64_RV100_MMIO_BASE, IA64_RV100_FB_BASE },
    };
    static const unsigned int bpps[] = { 8, 15, 16, 24, 32 };

    for (unsigned int i = 0; i < ARRAY_SIZE(devices); i++) {
        const uint64_t mmio = devices[i].mmio;
        g_autofree char *ppm = g_build_filename(
            g_get_tmp_dir(), "ati-crtc-live-mode.XXXXXX", NULL);
        QTestState *qts;
        int fd = g_mkstemp(ppm);

        g_assert_cmpint(fd, >=, 0);
        close(fd);
        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s",
                          devices[i].model);
        ati_pci_enable(qts);
        qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL, 0);
        qtest_writel(qts, mmio + ATI_CRTC_H_TOTAL_DISP,
                     (640 / 8 - 1) << 16);
        qtest_writel(qts, mmio + ATI_CRTC_V_TOTAL_DISP, (480 - 1) << 16);
        qtest_writel(qts, mmio + ATI_CRTC_PITCH, 800 / 8);
        qtest_writel(qts, mmio + ATI_CRTC_EXT_CNTL, 0);
        qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL,
                     ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                     ATI_CRTC_PIX_WIDTH_32);

        /* A depth change alone must update scanout, including byte writes. */
        for (unsigned int depth = 0; depth < ARRAY_SIZE(bpps); depth++) {
            qtest_writeb(qts, mmio + ATI_CRTC_GEN_CNTL + 1, depth + 2);
            g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==,
                             bpps[depth]);
        }

        /* Update each visible dimension without toggling CRTC enable. */
        qtest_writew(qts, mmio + ATI_CRTC_H_TOTAL_DISP + 2, 800 / 8 - 1);
        g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, 800);
        qtest_writew(qts, mmio + ATI_CRTC_V_TOTAL_DISP + 2, 600 - 1);
        g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, 600);
        qtest_writel(qts, devices[i].fb + ((599 * 800 + 799) * 4),
                     0x00ff0000);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 "{'filename':%s}}", ppm);
        assert_ppm_pixel(ppm, 800, 600, 799, 599, 0xff, 0, 0);
        qtest_quit(qts);
        g_assert_cmpint(g_unlink(ppm), ==, 0);
    }
}

static void ati_blit_visible_intersection(void)
{
    const unsigned width = 640;
    const unsigned height = 480;
    const unsigned virtual_width = 704;
    const unsigned pitch = virtual_width;
    const unsigned offset = pitch;
    QTestState *qts;
    g_autofree char *tmpdir = NULL;
    g_autofree char *before = NULL;
    g_autofree char *after = NULL;
    g_autoptr(GError) error = NULL;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p");
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_H_TOTAL_DISP,
                 ((width / 8) - 1) << 16);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP,
                 (height - 1) << 16);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_OFFSET, offset);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_PITCH,
                 virtual_width / 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_8);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_PEL_WRITE_INDEX), 1);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_PEL_DATA), 0x3f);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_PEL_DATA), 0);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_PEL_DATA), 0);
    qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(VGA_INPUT_STATUS1));
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_ATTR_INDEX), 0x20);

    tmpdir = g_dir_make_tmp("ia64-ati-dirty-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    before = g_build_filename(tmpdir, "before.ppm", NULL);
    after = g_build_filename(tmpdir, "after.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", before);
    assert_ppm_pixel(before, width, height, 0, 0, 0, 0, 0);

    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_OFFSET, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_PITCH,
                 virtual_width / 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (height << 16) | (width - 1));
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_CNTL, ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_8BPP |
                 ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_Y, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_DST_WIDTH, 16);

    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", after);
    assert_ppm_pixel(after, width, height, 0, 0, 0xff, 0, 0);
    assert_ppm_pixel(after, width, height, 16, 0, 0, 0, 0);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(before), ==, 0);
    g_assert_cmpint(g_unlink(after), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void ati_reverse_overlap_blit(void)
{
    enum { PITCH = 64 };
    static const struct {
        const char *args;
        uint64_t fb;
        uint64_t mmio;
        unsigned int pitch_value;
        bool inclusive_scissor;
    } devices[] = {
        {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rage128p",
            IA64_ATI_FB_BASE, IA64_ATI_MMIO_BASE, PITCH / 8, true,
        }, {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100",
            IA64_RV100_FB_BASE, IA64_RV100_MMIO_BASE, PITCH, false,
        }, {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=es1000",
            IA64_RV100_FB_BASE, IA64_RV100_MMIO_BASE, PITCH, false,
        },
    };
    uint8_t initial[32];
    uint8_t expected[32];
    uint8_t actual[32];
    size_t device;
    size_t i;

    for (i = 0; i < sizeof(initial); i++) {
        initial[i] = i;
    }

    for (device = 0; device < ARRAY_SIZE(devices); device++) {
        const uint64_t fb = devices[device].fb;
        const uint64_t mmio = devices[device].mmio;
        uint32_t scissor_right = devices[device].inclusive_scissor ?
                                 PITCH - 1 : PITCH;
        QTestState *qts = qtest_init(devices[device].args);

        qtest_writel(qts, mmio + ATI_DEFAULT_SC_BOTTOM_RIGHT, 0x1fff1fff);
        qtest_writel(qts, mmio + ATI_DST_OFFSET, 0);
        qtest_writel(qts, mmio + ATI_DST_PITCH, devices[device].pitch_value);
        qtest_writel(qts, mmio + ATI_SRC_OFFSET, 0);
        qtest_writel(qts, mmio + ATI_SRC_PITCH, devices[device].pitch_value);
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 0);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (1U << 16) | scissor_right);
        qtest_writel(qts, mmio + ATI_DP_CNTL, 0);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_SRC_PITCH |
                     ATI_GMC_DST_PITCH |
                     ATI_GMC_DST_CLIPPING | ATI_GMC_DST_8BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                     ATI_GMC_DP_SRC_RECT);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_DP_CNTL), ==,
                        devices[device].inclusive_scissor ?
                        ATI_DST_LTR_TTB : ATI_DST_RTL_TTB);

        /* Rightward overlap is safe only when pixels are visited RTL. */
        memcpy(expected, initial, sizeof(expected));
        memmove(&expected[4], &expected[0], 16);
        qtest_memwrite(qts, fb, initial, sizeof(initial));
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_RTL_TTB);
        qtest_writel(qts, mmio + ATI_SRC_X, 15);
        qtest_writel(qts, mmio + ATI_SRC_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_X, 19);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 16);
        qtest_memread(qts, fb, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

        /* A deliberately wrong LTR direction propagates overwritten data. */
        memcpy(expected, initial, sizeof(expected));
        for (i = 0; i < 16; i++) {
            expected[4 + i] = expected[i];
        }
        qtest_memwrite(qts, fb, initial, sizeof(initial));
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_SRC_X, 0);
        qtest_writel(qts, mmio + ATI_DST_X, 4);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 16);
        qtest_memread(qts, fb, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

        /* Leftward overlap is safe only when pixels are visited LTR. */
        memcpy(expected, initial, sizeof(expected));
        memmove(&expected[0], &expected[4], 16);
        qtest_memwrite(qts, fb, initial, sizeof(initial));
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_SRC_X, 4);
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 16);
        qtest_memread(qts, fb, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

        /* A deliberately wrong RTL direction propagates overwritten data. */
        memcpy(expected, initial, sizeof(expected));
        for (i = 16; i-- > 0;) {
            expected[i] = expected[4 + i];
        }
        qtest_memwrite(qts, fb, initial, sizeof(initial));
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_RTL_TTB);
        qtest_writel(qts, mmio + ATI_SRC_X, 19);
        qtest_writel(qts, mmio + ATI_DST_X, 15);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 16);
        qtest_memread(qts, fb, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        qtest_quit(qts);
    }
}

static void ati_source_scissor(void)
{
    enum {
        SOURCE_OFFSET = 0x1000,
        DESTINATION_OFFSET = 0x2000,
        PITCH = 32,
        SOURCE_X = 1,
        SOURCE_Y = 1,
        DESTINATION_X = 8,
        DESTINATION_Y = 4,
        WIDTH = 4,
        HEIGHT = 3,
    };
    static const struct {
        const char *args;
        uint64_t fb_base;
        uint64_t mmio_base;
        unsigned int pitch_value;
        bool inclusive;
    } devices[] = {
        {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rage128p",
            IA64_ATI_FB_BASE, IA64_ATI_MMIO_BASE, PITCH / 8, true,
        }, {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100",
            IA64_RV100_FB_BASE, IA64_RV100_MMIO_BASE, PITCH, false,
        }, {
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=es1000",
            IA64_RV100_FB_BASE, IA64_RV100_MMIO_BASE, PITCH, false,
        },
    };
    static const uint32_t directions[] = {
        ATI_DST_LTR_TTB, 0,
    };
    const uint8_t canary = 0xa5;
    unsigned int device;
    unsigned int direction;

    for (device = 0; device < ARRAY_SIZE(devices); device++) {
        for (direction = 0; direction < ARRAY_SIZE(directions);
             direction++) {
            const uint64_t fb = devices[device].fb_base;
            const uint64_t mmio = devices[device].mmio_base;
            bool forward = directions[direction] == ATI_DST_LTR_TTB;
            uint32_t source_right = SOURCE_X + 2 -
                                    devices[device].inclusive;
            uint32_t source_bottom = SOURCE_Y + 2 -
                                     devices[device].inclusive;
            QTestState *qts = qtest_init(devices[device].args);
            unsigned int x;
            unsigned int y;

            qtest_memset(qts, fb + SOURCE_OFFSET, 0, PITCH * 5);
            qtest_memset(qts, fb + DESTINATION_OFFSET, canary,
                         PITCH * 8);
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 8; x++) {
                    qtest_writeb(qts, fb + SOURCE_OFFSET + y * PITCH + x,
                                 (y << 4) | x);
                }
            }

            qtest_writel(qts, mmio + ATI_SRC_OFFSET, SOURCE_OFFSET);
            qtest_writel(qts, mmio + ATI_SRC_PITCH,
                         devices[device].pitch_value);
            qtest_writel(qts, mmio + ATI_DST_OFFSET, DESTINATION_OFFSET);
            qtest_writel(qts, mmio + ATI_DST_PITCH,
                         devices[device].pitch_value);
            qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 0);
            qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                         (31U << 16) | 31U);
            qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT,
                         source_bottom << 16 | source_right);
            qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                         ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                         ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                         ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                         ATI_GMC_BRUSH_NONE | ATI_GMC_DST_8BPP |
                         ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                         ATI_GMC_DP_SRC_RECT);
            qtest_writel(qts, mmio + ATI_DP_CNTL, directions[direction]);
            qtest_writel(qts, mmio + ATI_SRC_X,
                         SOURCE_X + (forward ? 0 : WIDTH - 1));
            qtest_writel(qts, mmio + ATI_SRC_Y,
                         SOURCE_Y + (forward ? 0 : HEIGHT - 1));
            qtest_writel(qts, mmio + ATI_DST_X,
                         DESTINATION_X + (forward ? 0 : WIDTH - 1));
            qtest_writel(qts, mmio + ATI_DST_Y,
                         DESTINATION_Y + (forward ? 0 : HEIGHT - 1));
            qtest_writel(qts, mmio + ATI_DST_HEIGHT, HEIGHT);
            qtest_writel(qts, mmio + ATI_DST_WIDTH, WIDTH);

            for (y = 0; y < HEIGHT; y++) {
                for (x = 0; x < WIDTH; x++) {
                    uint8_t expected = canary;

                    if (x < 2 && y < 2) {
                        expected = ((SOURCE_Y + y) << 4) | (SOURCE_X + x);
                    }
                    g_assert_cmphex(qtest_readb(
                        qts, fb + DESTINATION_OFFSET +
                        (DESTINATION_Y + y) * PITCH + DESTINATION_X + x),
                        ==, expected);
                }
            }

            if (forward) {
                /* A clear GMC_SRC_CLIPPING reloads the default bounds. */
                qtest_memset(qts, fb + DESTINATION_OFFSET, canary,
                             PITCH * 8);
                qtest_writel(qts, mmio + ATI_DEFAULT_SC_BOTTOM_RIGHT,
                             0x1fff1fff);
                qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                             ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                             ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                             ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_NONE |
                             ATI_GMC_DST_8BPP | ATI_GMC_SRC_COLOR |
                             ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT);
                qtest_writel(qts, mmio + ATI_SRC_X, SOURCE_X);
                qtest_writel(qts, mmio + ATI_SRC_Y, SOURCE_Y);
                qtest_writel(qts, mmio + ATI_DST_X, DESTINATION_X);
                qtest_writel(qts, mmio + ATI_DST_Y, DESTINATION_Y);
                qtest_writel(qts, mmio + ATI_DST_HEIGHT, HEIGHT);
                qtest_writel(qts, mmio + ATI_DST_WIDTH, WIDTH);
                for (y = 0; y < HEIGHT; y++) {
                    for (x = 0; x < WIDTH; x++) {
                        uint8_t expected = ((SOURCE_Y + y) << 4) |
                                           (SOURCE_X + x);

                        g_assert_cmphex(qtest_readb(
                            qts, fb + DESTINATION_OFFSET +
                            (DESTINATION_Y + y) * PITCH +
                            DESTINATION_X + x), ==, expected);
                    }
                }
            }
            qtest_quit(qts);
        }
    }
}

static void ati_rv100_3d_ring(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        TEXTURE_OFFSET = 0x10000,
        DEPTH_OFFSET = 0x20000,
        RING_OFFSET = 0x30000,
        IB_OFFSET = 0x31000,
        VERTEX_XYZ_OFFSET = 0x32000,
        VERTEX_COLOR_OFFSET = 0x32100,
        VERTEX_ST_OFFSET = 0x32200,
        RPTR_WRITEBACK_OFFSET = 0x32300,
        WRITEBACK_RING_OFFSET = 0x32400,
        RPTR_SCALED_DECOY_OFFSET = RPTR_WRITEBACK_OFFSET * 4,
    };
    const uint32_t texture_color = 0xff3366cc;
    const uint32_t marker = 0x52563130;
    const uint32_t vertex_format = R100_VTX_FMT_Z |
                                   R100_VTX_FMT_PKCOLOR |
                                   R100_VTX_FMT_ST0;
    const uint32_t vf_cntl = R100_VF_TRIANGLE_LIST |
                             R100_VF_WALK_DATA |
                             R100_VF_COLOR_RGBA | (3U << 16);
    const uint32_t indexed_vf_cntl = R100_VF_TRIANGLE_LIST |
                                     R100_VF_WALK_IND |
                                     R100_VF_COLOR_RGBA |
                                     R100_VF_INDEX_SIZE_32 | (3U << 16);
    uint32_t depth[WIDTH * HEIGHT];
    uint32_t vertex_xyz[] = {
        f32_bits(32.0f), f32_bits(32.0f), f32_bits(0.25f),
        f32_bits(56.0f), f32_bits(32.0f), f32_bits(0.25f),
        f32_bits(32.0f), f32_bits(56.0f), f32_bits(0.25f),
    };
    uint32_t vertex_color[] = { UINT32_MAX };
    uint32_t vertex_st[] = {
        f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(0.75f), f32_bits(0.25f),
    };
    uint32_t ib[] = {
        R100_CP_PACKET3 | (19U << 16) | (R100_PACKET3_DRAW_IMMD << 8),
        vertex_format,
        vf_cntl,
        f32_bits(4.0f), f32_bits(4.0f), f32_bits(0.25f), UINT32_MAX,
        f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(52.0f), f32_bits(4.0f), f32_bits(0.25f), UINT32_MAX,
        f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(4.0f), f32_bits(52.0f), f32_bits(0.25f), UINT32_MAX,
        f32_bits(0.75f), f32_bits(0.25f),
        R100_CP_PACKET3 | (5U << 16) | (R100_PACKET3_LOAD_VBPNTR << 8),
        3,
        3U | (3U << 8) | (1U << 16),
        VERTEX_XYZ_OFFSET,
        VERTEX_COLOR_OFFSET,
        2U | (2U << 8),
        VERTEX_ST_OFFSET,
        R100_CP_PACKET3 | (4U << 16) | (R100_PACKET3_DRAW_INDX << 8),
        vertex_format,
        indexed_vf_cntl,
        0, 1, 2,
    };
    uint32_t ring[] = {
        R100_SCRATCH_REG0 >> 2,
        marker,
        (1U << 16) | (R100_CP_IB_BASE >> 2),
        IB_OFFSET,
        ARRAY_SIZE(ib),
        ATI_GEN_INT_STATUS >> 2,
        ATI_SW_INT_FIRE,
    };
    uint32_t legacy_ib[] = {
        R100_CP_PACKET3 | (13U << 16) | (R100_PACKET3_RNDR_GEN_PRIM << 8),
        R100_VTX_FMT_ST0,
        R100_VF_RECTANGLE_LIST | R100_VF_WALK_DATA | (3U << 16),
        f32_bits(32.0f), f32_bits(4.0f), f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(48.0f), f32_bits(4.0f), f32_bits(0.75f), f32_bits(0.25f),
        f32_bits(48.0f), f32_bits(20.0f), f32_bits(0.75f), f32_bits(0.25f),
        R100_SCRATCH_REG0 >> 2, marker + 1,
    };
    QTestState *qts;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(depth); i++) {
        depth[i] = UINT32_MAX;
    }
    for (i = 0; i < ARRAY_SIZE(vertex_xyz); i++) {
        vertex_xyz[i] = cpu_to_le32(vertex_xyz[i]);
    }
    for (i = 0; i < ARRAY_SIZE(vertex_color); i++) {
        vertex_color[i] = cpu_to_le32(vertex_color[i]);
    }
    for (i = 0; i < ARRAY_SIZE(vertex_st); i++) {
        vertex_st[i] = cpu_to_le32(vertex_st[i]);
    }
    for (i = 0; i < ARRAY_SIZE(ib); i++) {
        ib[i] = cpu_to_le32(ib[i]);
    }
    for (i = 0; i < ARRAY_SIZE(ring); i++) {
        ring[i] = cpu_to_le32(ring[i]);
    }
    for (i = 0; i < ARRAY_SIZE(depth); i++) {
        depth[i] = cpu_to_le32(depth[i]);
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_irq_intercept_in(qts, "/machine/unattached/device[1]");
    qtest_writel(qts, IA64_RV100_FB_BASE + TEXTURE_OFFSET + 4,
                 texture_color);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + DEPTH_OFFSET,
                   depth, sizeof(depth));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                   ring, sizeof(ring));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET, ib, sizeof(ib));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_XYZ_OFFSET,
                   vertex_xyz, sizeof(vertex_xyz));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_COLOR_OFFSET,
                   vertex_color, sizeof(vertex_color));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_ST_OFFSET,
                   vertex_st, sizeof(vertex_st));
    qtest_writel(qts, IA64_RV100_FB_BASE + RPTR_WRITEBACK_OFFSET,
                 0xfeedface);
    qtest_writel(qts, IA64_RV100_FB_BASE + RPTR_SCALED_DECOY_OFFSET,
                 0xdeadbeef);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_CNTL,
                 R100_RB_Z_ENABLE | R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_DEPTHOFFSET,
                 DEPTH_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_DEPTHPITCH, WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_ZSTENCILCNTL,
                 4 | (1U << 4) | R100_Z_WRITE_ENABLE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TXFILTER_0, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TXOFFSET_0,
                 TEXTURE_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TEX_SIZE_0,
                 1U << 16 | 1U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TEX_PITCH_0, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TXCBLEND_0,
                 4U | (10U << 5) | R100_COMBINER_CLAMP);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_TXABLEND_0,
                 2U | (5U << 4) | R100_COMBINER_CLAMP);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_CNTL,
                 ATI_SW_INT_ENABLE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_ME_RAM_ADDR, 7);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_ME_RAM_DATAH,
                 0x01234567);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_ME_RAM_ADDR), ==, 7);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_ME_RAM_DATAL,
                 0x89abcdef);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_ME_RAM_ADDR), ==, 8);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE, RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_ADDR,
                 RPTR_WRITEBACK_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(ring));

    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0), ==, marker);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_RB_RPTR), ==, ARRAY_SIZE(ring));
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_FB_BASE +
                                 RPTR_WRITEBACK_OFFSET), ==,
                     ARRAY_SIZE(ring));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                RPTR_SCALED_DECOY_OFFSET), ==, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (10 * WIDTH + 10) * 4), ==, texture_color);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (40 * WIDTH + 40) * 4), ==, texture_color);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_FB_BASE + DEPTH_OFFSET +
                                 (10 * WIDTH + 10) * 4), <, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GEN_INT_STATUS) & ATI_SW_INT_TEST,
                    ==, ATI_SW_INT_TEST);
    g_assert_true(qtest_get_irq(qts, 17));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_SW_INT_TEST);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GEN_INT_STATUS) & ATI_SW_INT_TEST,
                    ==, 0);
    g_assert_false(qtest_get_irq(qts, 17));

    /* Zero is valid; the low two register bits are not address bits. */
    qtest_writel(qts, IA64_RV100_FB_BASE + WRITEBACK_RING_OFFSET,
                 R100_CP_PACKET2);
    qtest_writel(qts, IA64_RV100_FB_BASE, 0xfeedface);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 WRITEBACK_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_ADDR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR, 1);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_FB_BASE), ==, 1);

    /* Legacy opcode 0x25 uses Radeon XY/ST vertices without implicit Z. */
    for (i = 0; i < ARRAY_SIZE(legacy_ib); i++) {
        legacy_ib[i] = cpu_to_le32(legacy_ib[i]);
    }
    qtest_memset(qts, IA64_RV100_FB_BASE, 0, WIDTH * HEIGHT * 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                   legacy_ib, sizeof(legacy_ib));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                 ARRAY_SIZE(legacy_ib));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0), ==, marker + 1);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (10 * WIDTH + 40) * 4), ==, texture_color);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (10 * WIDTH + 31) * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (10 * WIDTH + 48) * 4), ==, 0);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_STATUS,
                 ATI_SW_INT_FIRE);
    g_assert_true(qtest_get_irq(qts, 17));
    qtest_system_reset(qts);
    ati_pci_enable(qts);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                ATI_GEN_INT_STATUS), ==,
                    ATI_R100_GEN_INT_STATUS_RESET);
    g_assert_false(qtest_get_irq(qts, 17));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_CP_RB_RPTR), ==, 0);
    qtest_quit(qts);
}

static void ati_rv100_cp_hostdata_blt(void)
{
    enum {
        DST_WIDTH = 32,
        VISIBLE_WIDTH = 13,
        DST_HEIGHT = 2,
        DST_PITCH = 64,
        DST_OFFSET = 0x4000,
        RING_OFFSET = 0x34000,
        BAD_RING_OFFSET = 0x35000,
        MODE_IB_OFFSET = 0x36000,
        MULTI_RING_OFFSET = 0x37000,
        HOSTDATA_DWORDS = 2,
        HOSTDATA_PAYLOAD_DWORDS = 9 + HOSTDATA_DWORDS,
        RING_DWORDS = 1 + HOSTDATA_PAYLOAD_DWORDS + 2,
        MULTI_DST_HEIGHT = 8,
        MULTI_PAYLOAD_DWORDS = 12,
        MULTI_RING_DWORDS = 1 + MULTI_PAYLOAD_DWORDS + 2,
        LARGE_DST_HEIGHT = 4097,
        LARGE_RING_OFFSET = 0x80000,
        LARGE_HOSTDATA_DWORDS = LARGE_DST_HEIGHT,
        LARGE_HOSTDATA_PAYLOAD_DWORDS = 9 + LARGE_HOSTDATA_DWORDS,
        LARGE_RING_DWORDS = 1 + LARGE_HOSTDATA_PAYLOAD_DWORDS + 2,
    };
    const uint8_t foreground = 0xe7;
    const uint8_t background = 0x19;
    const uint8_t canary = 0xa5;
    const uint32_t marker = 0x4844424c;
    const uint32_t rejected_marker = 0xdec0adde;
    const uint32_t raster[HOSTDATA_DWORDS] = { 0x1555, 0x0aaa };
    const uint32_t multi_raster[] = { 0x15, 0x9a5 };
    const unsigned int large_check_rows[] = {
        0, LARGE_DST_HEIGHT - 2, LARGE_DST_HEIGHT - 1,
    };
    const unsigned int primary_bm_modes[] = { 2, 4, 6, 8 };
    const unsigned int indirect_bm_modes[] = { 3, 4, 5, 6, 7, 8 };
    uint32_t ring[RING_DWORDS] = {
        R100_CP_PACKET3 | ((HOSTDATA_PAYLOAD_DWORDS - 1) << 16) |
        (R100_PACKET3_CNTL_HOSTDATA_BLT << 8),
        ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
        ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_NONE | ATI_GMC_DST_8BPP |
        ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_SRCCOPY |
        ATI_GMC_DP_SRC_HOST,
        ((DST_PITCH / 64) << 22) | (DST_OFFSET >> 10),
        0,
        (DST_HEIGHT << 16) | VISIBLE_WIDTH,
        foreground,
        background,
        0,
        (DST_HEIGHT << 16) | DST_WIDTH,
        HOSTDATA_DWORDS,
        raster[0], raster[1],
        R100_SCRATCH_REG0 >> 2, marker,
    };
    uint32_t bad_ring[RING_DWORDS];
    uint32_t mode_ib[] = {
        R100_SCRATCH_REG0 >> 2, marker,
    };
    uint32_t multi_ring[MULTI_RING_DWORDS] = {
        R100_CP_PACKET3 | ((MULTI_PAYLOAD_DWORDS - 1) << 16) |
        (R100_PACKET3_CNTL_HOSTDATA_BLT << 8),
        ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
        ATI_GMC_DST_PITCH | ATI_GMC_BRUSH_NONE | ATI_GMC_DST_8BPP |
        ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_SRCCOPY |
        ATI_GMC_DP_SRC_HOST,
        ((DST_PITCH / 64) << 22) | (DST_OFFSET >> 10),
        foreground,
        background,
        (3U << 16) | 2U,
        (1U << 16) | 5U,
        1,
        multi_raster[0],
        (4U << 16) | 10U,
        (2U << 16) | 6U,
        1,
        multi_raster[1],
        R100_SCRATCH_REG0 >> 2, marker,
    };
    g_autofree uint32_t *large_ring = g_new0(uint32_t, LARGE_RING_DWORDS);
    QTestState *qts;
    unsigned int i;
    unsigned int x;
    unsigned int y;

    memcpy(bad_ring, ring, sizeof(bad_ring));
    bad_ring[9] = HOSTDATA_DWORDS + 1;
    large_ring[0] = R100_CP_PACKET3 |
                    ((LARGE_HOSTDATA_PAYLOAD_DWORDS - 1) << 16) |
                    (R100_PACKET3_CNTL_HOSTDATA_BLT << 8);
    memcpy(&large_ring[1], &ring[1], 3 * sizeof(*large_ring));
    large_ring[4] = (LARGE_DST_HEIGHT << 16) | VISIBLE_WIDTH;
    memcpy(&large_ring[5], &ring[5], 3 * sizeof(*large_ring));
    large_ring[8] = (LARGE_DST_HEIGHT << 16) | DST_WIDTH;
    large_ring[9] = LARGE_HOSTDATA_DWORDS;
    for (i = 0; i < LARGE_HOSTDATA_DWORDS; i++) {
        large_ring[10 + i] = raster[i & 1];
    }
    large_ring[LARGE_RING_DWORDS - 2] = R100_SCRATCH_REG0 >> 2;
    large_ring[LARGE_RING_DWORDS - 1] = marker;
    for (i = 0; i < RING_DWORDS; i++) {
        ring[i] = cpu_to_le32(ring[i]);
        bad_ring[i] = cpu_to_le32(bad_ring[i]);
    }
    for (i = 0; i < ARRAY_SIZE(mode_ib); i++) {
        mode_ib[i] = cpu_to_le32(mode_ib[i]);
    }
    for (i = 0; i < MULTI_RING_DWORDS; i++) {
        multi_ring[i] = cpu_to_le32(multi_ring[i]);
    }
    for (i = 0; i < LARGE_RING_DWORDS; i++) {
        large_ring[i] = cpu_to_le32(large_ring[i]);
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);
    qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary,
                 DST_PITCH * DST_HEIGHT);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                   ring, sizeof(ring));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + MODE_IB_OFFSET,
                   mode_ib, sizeof(mode_ib));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 3);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    /* Primary PIO mode must not consume the bus-master ring. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIPIO_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 RING_DWORDS);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==, 0);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_CP_RB_RPTR), ==, 0);
    for (i = 0; i < ARRAY_SIZE(primary_bm_modes); i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                     rejected_marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     primary_bm_modes[i] << 28);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        R100_SCRATCH_REG0), ==, marker);
        g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_CP_RB_RPTR), ==, RING_DWORDS);
    }

    for (y = 0; y < DST_HEIGHT; y++) {
        for (x = 0; x < DST_WIDTH; x++) {
            uint8_t expected = canary;

            if (x < VISIBLE_WIDTH) {
                expected = (raster[y] >> x) & 1 ? foreground : background;
            }
            g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                             DST_OFFSET + y * DST_PITCH + x),
                            ==, expected);
        }
    }
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==, marker);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_CP_RB_RPTR), ==, RING_DWORDS);

    /* Variable SETTINGS and multiple BIGCHARs share one HOSTDATA packet. */
    qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary,
                 DST_PITCH * MULTI_DST_HEIGHT);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + MULTI_RING_OFFSET,
                   multi_ring, sizeof(multi_ring));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DEFAULT_SC_BOTTOM_RIGHT,
                 (MULTI_DST_HEIGHT << 16) | DST_WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 MULTI_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 MULTI_RING_DWORDS);
    for (y = 0; y < MULTI_DST_HEIGHT; y++) {
        for (x = 0; x < DST_WIDTH; x++) {
            uint8_t expected = canary;

            if (y == 3 && x >= 2 && x < 7) {
                expected = multi_raster[0] & (1U << (x - 2)) ?
                           foreground : background;
            } else if (y >= 4 && y < 6 && x >= 10 && x < 16) {
                unsigned int bit = (y - 4) * 6 + x - 10;

                expected = multi_raster[1] & (1U << bit) ?
                           foreground : background;
            }
            g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                             DST_OFFSET + y * DST_PITCH + x),
                            ==, expected);
        }
    }
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==, marker);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_CP_RB_RPTR), ==,
                     MULTI_RING_DWORDS);

    /* Primary-only BM mode ignores IB submissions; combined BM accepts one. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 rejected_marker);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE,
                 MODE_IB_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                 ARRAY_SIZE(mode_ib));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==,
                    rejected_marker);
    for (i = 0; i < ARRAY_SIZE(indirect_bm_modes); i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                     rejected_marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     indirect_bm_modes[i] << 28);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(mode_ib));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        R100_SCRATCH_REG0), ==, marker);
    }

    /* A mismatched inline count is rejected before register side effects. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL, 0);
    qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary,
                 DST_PITCH * DST_HEIGHT);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + BAD_RING_OFFSET,
                   bad_ring, sizeof(bad_ring));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 BAD_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK, 0x00ff);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 rejected_marker);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 RING_DWORDS);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    ATI_DP_WRITE_MASK), ==, 0x00ff);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==,
                    rejected_marker);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_CP_RB_RPTR), ==, RING_DWORDS);
    for (y = 0; y < DST_HEIGHT; y++) {
        for (x = 0; x < DST_WIDTH; x++) {
            g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                             DST_OFFSET + y * DST_PITCH + x),
                            ==, canary);
        }
    }

    /* Xorg emits HOSTDATA packets larger than the immediate-vertex limit. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL, 0);
    qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary,
                 DST_PITCH * LARGE_DST_HEIGHT);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + LARGE_RING_OFFSET,
                   large_ring, LARGE_RING_DWORDS * sizeof(*large_ring));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 rejected_marker);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 LARGE_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 12);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 LARGE_RING_DWORDS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    for (i = 0; i < ARRAY_SIZE(large_check_rows); i++) {
        y = large_check_rows[i];
        for (x = 0; x < DST_WIDTH; x++) {
            uint8_t expected = canary;

            if (x < VISIBLE_WIDTH) {
                expected = (raster[y & 1] >> x) & 1 ?
                           foreground : background;
            }
            g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                             DST_OFFSET + y * DST_PITCH + x),
                            ==, expected);
        }
    }
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==, marker);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_CP_RB_RPTR), ==,
                     LARGE_RING_DWORDS);
    qtest_quit(qts);
}

static void ati_rv100_cp_bitblt_multi(void)
{
    enum {
        SRC_OFFSET = 0x1000,
        DST_OFFSET = 0x2000,
        RING_OFFSET = 0x4000,
        PITCH = 64,
        RING_DWORDS = 15,
        LARGE_DST_OFFSET = 0x100000,
        LARGE_RING_OFFSET = 0xf00000,
        LARGE_WIDTH = 1024,
        LARGE_HEIGHT = 2049,
        LARGE_PITCH = LARGE_WIDTH * sizeof(uint32_t),
    };
    static const struct {
        uint32_t dp_cntl;
        uint32_t rop;
        uint32_t destination;
        uint32_t expected[3];
    } cases[] = {
        {
            .dp_cntl = 0,
            .rop = ATI_GMC_ROP3_SRCCOPY,
            .destination = 0,
            .expected = { 0x11223344, 0x55667788, 0xaabbccdd },
        }, {
            .dp_cntl = ATI_DST_LTR_TTB,
            .rop = ATI_GMC_ROP3_SRCINVERT,
            .destination = UINT32_MAX,
            .expected = { 0xeeddccbb, 0xaa998877, 0x55443322 },
        },
    };
    const uint32_t marker = 0xdeadbeef;
    const uint32_t rejected_marker = 0xdec0adde;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(cases); i++) {
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                       ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                       ATI_GMC_SRC_COLOR | cases[i].rop |
                       ATI_GMC_DP_SRC_RECT;
        uint32_t ring[RING_DWORDS] = {
            R100_CP_PACKET3 | (11U << 16) |
            (R100_PACKET3_BITBLT_MULTI << 8),
            gui,
            ((PITCH / 64) << 22) | (SRC_OFFSET >> 10),
            ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
            (4U << 16) | 3U,
            0,
            0x1fff1fff,
            (1U << 16) | 1U,
            (5U << 16) | 2U,
            (3U << 16) | 1U,
            (2U << 16) | 3U,
            (7U << 16) | 4U,
            (2U << 16) | 2U,
            R100_SCRATCH_REG0 >> 2,
            marker,
        };
        QTestState *qts = qtest_init(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100");
        unsigned int word;

        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + SRC_OFFSET, 0,
                     PITCH * 5);
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                     cases[i].destination & 0xff, PITCH * 6);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + PITCH + 4,
                     0x11223344);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + PITCH + 8,
                     0x55667788);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + PITCH + 12,
                     0xdeadbeef);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + 3 * PITCH + 8,
                     0xaabbccdd);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + 3 * PITCH + 12,
                     0x0badf00d);
        qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + 4 * PITCH + 8,
                     0x12345678);
        for (word = 0; word < ARRAY_SIZE(ring); word++) {
            ring[word] = cpu_to_le32(ring[word]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                       ring, sizeof(ring));

        /* The packet must clear stale compare and write-mask state. */
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_CNTL,
                     (1U << 24) | 1U);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_MASK,
                     UINT32_MAX);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     cases[i].dp_cntl);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                     RING_OFFSET);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL, 4);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                     ARRAY_SIZE(ring));

        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         2 * PITCH + 5 * 4), ==,
                        cases[i].expected[0]);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         2 * PITCH + 6 * 4), ==,
                        cases[i].expected[1]);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         4 * PITCH + 7 * 4), ==,
                        cases[i].expected[2]);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         2 * PITCH + 4 * 4), ==,
                        cases[i].destination);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         2 * PITCH + 7 * 4), ==,
                        cases[i].destination);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         4 * PITCH + 8 * 4), ==,
                        cases[i].destination);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                                         5 * PITCH + 7 * 4), ==,
                        cases[i].destination);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                          R100_CP_RB_RPTR), ==,
                         ARRAY_SIZE(ring));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_CLR_CMP_CNTL) & 0x707, ==, 0);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_WRITE_MASK), ==, UINT32_MAX);
        qtest_quit(qts);
    }

    /* BITBLT_MULTI loads its monochrome brush data and alignment origin. */
    {
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_DST_8BPP | ATI_GMC_BYTE_LSB_TO_MSB |
                       ATI_GMC_ROP3_PATCOPY | ATI_GMC_LD_BRUSH_Y_X;
        uint32_t ring[RING_DWORDS] = {
            R100_CP_PACKET3 | (11U << 16) |
            (R100_PACKET3_BITBLT_MULTI << 8),
            gui,
            ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
            0,
            (2U << 16) | 8U,
            0x11,
            0xe2,
            0x08040201,
            0x80402010,
            (1U << 8) | 2U,
            0,
            0,
            (8U << 16) | 2U,
            R100_SCRATCH_REG0 >> 2,
            marker,
        };
        QTestState *qts = qtest_init(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100");
        unsigned int word;

        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, 0xa5,
                     PITCH * 2);
        for (word = 0; word < ARRAY_SIZE(ring); word++) {
            ring[word] = cpu_to_le32(ring[word]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                       ring, sizeof(ring));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                     RING_OFFSET);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL, 4);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                     ARRAY_SIZE(ring));

        for (unsigned int y = 0; y < 2; y++) {
            for (unsigned int x = 0; x < PITCH; x++) {
                uint8_t expected = 0xa5;

                if (x < 8) {
                    expected = (((x - 2) & 7) == ((y - 1) & 7)) ?
                               0xe2 : 0x11;
                }
                g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                                 DST_OFFSET + y * PITCH + x),
                                ==, expected);
            }
        }
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                          R100_CP_RB_RPTR), ==,
                         ARRAY_SIZE(ring));
        qtest_quit(qts);
    }

    /* Brush type 14 carries the packet's solid foreground color. */
    {
        const uint8_t color = 0x6d;
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_BRUSH_SOLID_LINE | ATI_GMC_DST_8BPP |
                       ATI_GMC_ROP3_PATCOPY;
        uint32_t ring[] = {
            R100_CP_PACKET3 | (7U << 16) |
            (R100_PACKET3_BITBLT_MULTI << 8),
            gui,
            ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
            0,
            (3U << 16) | 8U,
            color,
            0,
            2U << 16,
            (4U << 16) | 2U,
            R100_SCRATCH_REG0 >> 2,
            marker,
        };
        QTestState *qts = qtest_init(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100");
        unsigned int word;

        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, 0xa5,
                     PITCH * 3);
        for (word = 0; word < ARRAY_SIZE(ring); word++) {
            ring[word] = cpu_to_le32(ring[word]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                       ring, sizeof(ring));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                     RING_OFFSET);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL, 4);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                     ARRAY_SIZE(ring));
        for (unsigned int y = 0; y < 3; y++) {
            for (unsigned int x = 0; x < 8; x++) {
                uint8_t expected = y < 2 && x >= 2 && x < 6 ? color : 0xa5;

                g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                                 DST_OFFSET + y * PITCH + x),
                                ==, expected);
            }
        }
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        qtest_quit(qts);
    }

    /* A legal BITBLT_MULTI larger than the parser budget must still finish. */
    {
        const uint32_t color = 0x1234abcd;
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_BRUSH_SOLID_LINE | ATI_GMC_DST_32BPP |
                       ATI_GMC_ROP3_PATCOPY;
        uint32_t ring[] = {
            R100_CP_PACKET3 | (7U << 16) |
            (R100_PACKET3_BITBLT_MULTI << 8),
            gui,
            ((LARGE_PITCH / 64) << 22) | (LARGE_DST_OFFSET >> 10),
            0,
            (LARGE_HEIGHT << 16) | LARGE_WIDTH,
            color,
            0,
            0,
            (LARGE_WIDTH << 16) | LARGE_HEIGHT,
            R100_SCRATCH_REG0 >> 2,
            marker,
        };
        uint64_t last = IA64_RV100_FB_BASE + LARGE_DST_OFFSET +
                        (uint64_t)(LARGE_HEIGHT - 1) * LARGE_PITCH +
                        (LARGE_WIDTH - 1) * sizeof(uint32_t);
        QTestState *qts = qtest_init(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=rv100");
        unsigned int word;

        ati_pci_enable(qts);
        qtest_writel(qts, IA64_RV100_FB_BASE + LARGE_DST_OFFSET,
                     rejected_marker);
        qtest_writel(qts, last, rejected_marker);
        for (word = 0; word < ARRAY_SIZE(ring); word++) {
            ring[word] = cpu_to_le32(ring[word]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + LARGE_RING_OFFSET,
                       ring, sizeof(ring));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                     rejected_marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                     LARGE_RING_OFFSET);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL, 4);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                     ARRAY_SIZE(ring));

        g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                         LARGE_DST_OFFSET), ==, color);
        g_assert_cmphex(qtest_readl(qts, last), ==, color);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                          R100_CP_RB_RPTR), ==,
                         ARRAY_SIZE(ring));
        qtest_quit(qts);
    }

    /* 2D packets reject invalid destination types and unsupported brushes. */
    {
        static const struct {
            uint32_t gui;
            unsigned int brush_dwords;
        } invalid[] = {
            {
                .gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_BRUSH_NONE |
                       (7U << 8) | ATI_GMC_SRC_COLOR |
                       ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT,
            }, {
                .gui = ATI_GMC_WR_MSK_DIS | (6U << 4) |
                       ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                       ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT,
                .brush_dwords = 3,
            },
        };

        for (i = 0; i < ARRAY_SIZE(invalid); i++) {
            unsigned int payload_dwords = 4 + invalid[i].brush_dwords;
            unsigned int ring_dwords = 1 + payload_dwords + 2;
            uint32_t ring[10] = {
                R100_CP_PACKET3 | ((payload_dwords - 1) << 16) |
                (R100_PACKET3_BITBLT_MULTI << 8),
                invalid[i].gui,
            };
            QTestState *qts;
            unsigned int word = 2 + invalid[i].brush_dwords;

            ring[word++] = 0;
            ring[word++] = 0;
            ring[word++] = (1U << 16) | 1U;
            ring[word++] = R100_SCRATCH_REG0 >> 2;
            ring[word++] = marker;
            g_assert_cmpuint(word, ==, ring_dwords);
            for (word = 0; word < ring_dwords; word++) {
                ring[word] = cpu_to_le32(ring[word]);
            }

            qts = qtest_init(
                "-machine ia64-vpc,nvram=none -m 256M -S "
                "-vga ati -global ati-vga.model=rv100");
            ati_pci_enable(qts);
            qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                           ring, ring_dwords * sizeof(ring[0]));
            qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK,
                         0x12345678);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                         rejected_marker);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                         RING_OFFSET);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL, 3);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                         R100_CSQ_PRIBM_INDDIS);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                         ring_dwords);
            g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                             ATI_DP_WRITE_MASK), ==,
                            0x12345678);
            g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                             R100_SCRATCH_REG0), ==,
                            rejected_marker);
            qtest_quit(qts);
        }
    }
}

static void ati_rv100_cp_nextchar(void)
{
    enum {
        DST_OFFSET = 0x2000,
        IB_OFFSET = 0x4000,
        PITCH = 128,
        HEIGHT = 8,
        GLYPH_WIDTH = 13,
        GLYPH_HEIGHT = 3,
    };
    const uint16_t glyph[] = { 0x1555, 0x1aab, 0x1234 };
    const uint32_t foreground = 0xff12ab34;
    const uint32_t background = 0xff3456ef;
    const uint32_t canary = 0xa5a5a5a5;
    const uint32_t marker = 0x43484152;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-vga ati -global ati-vga.model=rv100");

    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE, IB_OFFSET);

    for (unsigned int transparent = 0; transparent < 2; transparent++) {
        uint32_t commands[] = {
            R100_CP_PACKET3 | (3U << 16) | (R100_PACKET3_NEXT_CHAR << 8),
            (2U << 16) | 3U,
            (GLYPH_HEIGHT << 16) | GLYPH_WIDTH,
            0, 0,
            R100_SCRATCH_REG0 >> 2, marker,
        };
        uint32_t bad_commands[ARRAY_SIZE(commands) - 1];
        uint32_t pixels[PITCH / sizeof(uint32_t) * HEIGHT];
        unsigned int row_bits = transparent ? 16 : GLYPH_WIDTH;
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                       ATI_GMC_BYTE_LSB_TO_MSB | ATI_GMC_ROP3_SRCCOPY |
                       (transparent ? ATI_GMC_SRC_MONO_FG_LA |
                                      ATI_GMC_DP_SRC_HOST_BYTEALIGN :
                                      ATI_GMC_DP_SRC_HOST);
        uint32_t setup[] = {
            R100_CP_PACKET3 | (5U << 16) |
            (R100_PACKET3_CNTL_HOSTDATA_BLT << 8),
            gui,
            ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
            (3U << 16) | 5U,
            (5U << 16) | 14U,
            foreground, background,
            R100_SCRATCH_REG0 >> 2, marker,
        };

        for (unsigned int y = 0; y < GLYPH_HEIGHT; y++) {
            for (unsigned int x = 0; x < GLYPH_WIDTH; x++) {
                unsigned int bit = y * row_bits + x;

                commands[3 + bit / 32] |= ((glyph[y] >> x) & 1) << (bit % 32);
            }
        }
        memcpy(bad_commands, commands, 4 * sizeof(commands[0]));
        bad_commands[0] = R100_CP_PACKET3 | (2U << 16) |
                          (R100_PACKET3_NEXT_CHAR << 8);
        bad_commands[4] = R100_SCRATCH_REG0 >> 2;
        bad_commands[5] = marker;
        for (unsigned int word = 0; word < ARRAY_SIZE(commands); word++) {
            commands[word] = cpu_to_le32(commands[word]);
        }
        for (unsigned int word = 0; word < ARRAY_SIZE(bad_commands); word++) {
            bad_commands[word] = cpu_to_le32(bad_commands[word]);
        }
        for (unsigned int word = 0; word < ARRAY_SIZE(setup); word++) {
            setup[word] = cpu_to_le32(setup[word]);
        }
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary & 0xff,
                     sizeof(pixels));
        /* A HOSTDATA packet without a bitmap establishes text drawing state. */
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0, 0);
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       setup, sizeof(setup));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(setup));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 7);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 6);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0, 0);

        /* A missing bitmap word must leave coordinates and pixels untouched. */
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       bad_commands, sizeof(bad_commands));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(bad_commands));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_DST_X),
                        ==, 7);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y),
                        ==, 6);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, 0);
        qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                      pixels, sizeof(pixels));
        for (unsigned int i = 0; i < ARRAY_SIZE(pixels); i++) {
            g_assert_cmphex(le32_to_cpu(pixels[i]), ==, canary);
        }

        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       commands, sizeof(commands));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(commands));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                      pixels, sizeof(pixels));
        for (unsigned int y = 0; y < HEIGHT; y++) {
            for (unsigned int x = 0; x < PITCH / sizeof(uint32_t); x++) {
                uint32_t expected = canary;

                if (x >= 5 && x < 14 && y >= 3 && y < 5) {
                    if ((glyph[y - 2] >> (x - 3)) & 1) {
                        expected = foreground;
                    } else if (!transparent) {
                        expected = background;
                    }
                }
                g_assert_cmphex(le32_to_cpu(pixels[y * PITCH / 4 + x]),
                                ==, expected);
            }
        }
    }
    qtest_quit(qts);
}

static void ati_rv100_cp_polyscanlines(void)
{
    enum {
        DST_OFFSET = 0x2000,
        IB_OFFSET = 0x4000,
        PITCH = 64,
        HEIGHT = 8,
    };
    const uint32_t color = 0x12345678;
    const uint32_t canary = 0xa5a5a5a5;
    const uint32_t write_mask = 0x00ff00ff;
    const uint32_t marker = 0x5343414e;
    const uint32_t rejected_marker = 0xdec0adde;
    const uint32_t gui = ATI_GMC_CLR_CMP_DIS | ATI_GMC_DST_PITCH |
                         ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_SOLID |
                         ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                         ATI_GMC_ROP3_PATINVERT;
    uint32_t setup_and_spans[] = {
        R100_CP_PACKET3 | (4U << 16) |
        (R100_PACKET3_CNTL_POLYSCANLINES << 8),
        gui,
        ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
        (2U << 16) | 2U,
        (6U << 16) | 10U,
        color,
        R100_CP_PACKET3 | (3U << 16) | (R100_PACKET3_PLY_NEXTSCAN << 8),
        (2U << 16) | 1U,
        (5U << 16) | 1U,
        (4U << 16) | 4U,
        (11U << 16) | 8U,
        R100_CP_PACKET3 | (1U << 16) | (R100_PACKET3_PLY_NEXTSCAN << 8),
        2U,
        (10U << 16) | 2U,
        R100_SCRATCH_REG0 >> 2, marker,
    };
    uint32_t counted[] = {
        R100_CP_PACKET3 | (12U << 16) |
        (R100_PACKET3_CNTL_POLYSCANLINES << 8),
        gui,
        ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
        (2U << 16) | 2U,
        (6U << 16) | 10U,
        color,
        2,
        2, (2U << 16) | 3U, (6U << 16) | 3U, (10U << 16) | 8U,
        1, (1U << 16) | 5U, (7U << 16) | 4U,
        R100_SCRATCH_REG0 >> 2, marker,
    };
    uint32_t truncated[ARRAY_SIZE(counted) - 1];
    const struct {
        uint32_t *commands;
        unsigned int count;
        bool valid;
    } cases[] = {
        { setup_and_spans, ARRAY_SIZE(setup_and_spans), true },
        { truncated, ARRAY_SIZE(truncated), false },
        { counted, ARRAY_SIZE(counted), true },
    };
    QTestState *qts;

    /* The final counted block lacks its promised span. */
    memcpy(truncated, counted, 13 * sizeof(counted[0]));
    truncated[0] = R100_CP_PACKET3 | (11U << 16) |
                   (R100_PACKET3_CNTL_POLYSCANLINES << 8);
    truncated[13] = R100_SCRATCH_REG0 >> 2;
    truncated[14] = marker;
    for (unsigned int i = 0; i < ARRAY_SIZE(cases); i++) {
        for (unsigned int word = 0; word < cases[i].count; word++) {
            cases[i].commands[word] = cpu_to_le32(cases[i].commands[word]);
        }
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE, IB_OFFSET);
    for (unsigned int i = 0; i < ARRAY_SIZE(cases); i++) {
        uint32_t pixels[PITCH / sizeof(uint32_t) * HEIGHT];
        bool valid = cases[i].valid;
        uint32_t expected_marker = valid ? marker : rejected_marker;
        uint32_t expected_gui = valid ? gui : 0;
        uint32_t expected_color = valid ? color : rejected_marker;

        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary & 0xff,
                     sizeof(pixels));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                     rejected_marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK, write_mask);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                     rejected_marker);
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       cases[i].commands, cases[i].count * sizeof(uint32_t));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     cases[i].count);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==,
                        expected_marker);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_GUI_MASTER_CNTL), ==,
                        expected_gui);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_BRUSH_FRGD_CLR), ==,
                        expected_color);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_WRITE_MASK), ==, write_mask);
        qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                      pixels, sizeof(pixels));
        for (unsigned int y = 0; y < HEIGHT; y++) {
            for (unsigned int x = 0; x < PITCH / sizeof(uint32_t); x++) {
                bool painted = i == 0 ?
                    y == 2 && ((x >= 2 && x < 5) || (x >= 8 && x < 10)) :
                    i == 2 &&
                    ((y >= 3 && y < 5 &&
                      ((x >= 3 && x < 6) || (x >= 8 && x < 10))) ||
                     (y == 5 && x >= 4 && x < 7));
                uint32_t expected = painted ?
                    canary ^ (color & write_mask) : canary;

                g_assert_cmphex(le32_to_cpu(pixels[y * PITCH / 4 + x]),
                                ==, expected);
            }
        }
    }
    qtest_quit(qts);
}

static void ati_rv100_cp_paint_multi(void)
{
    enum {
        DST_OFFSET = 0x2000,
        IB_OFFSET = 0x4000,
        PITCH = 64,
        HEIGHT = 8,
    };
    const uint32_t color = 0x12345678;
    const uint32_t canary = 0xa5a5a5a5;
    const uint32_t marker = 0x5041494e;
    const uint32_t rejected_marker = 0xdec0adde;
    const uint32_t gui = ATI_GMC_CLR_CMP_DIS | ATI_GMC_DST_PITCH |
                         ATI_GMC_DST_CLIPPING | ATI_GMC_BRUSH_SOLID |
                         ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                         ATI_GMC_ROP3_PATCOPY;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-vga ati -global ati-vga.model=rv100");

    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE, IB_OFFSET);

    for (unsigned int reverse = 0; reverse < 2; reverse++) {
        uint32_t commands[] = {
            R100_CP_PACKET3 | (8U << 16) |
            (R100_PACKET3_CNTL_PAINT_MULTI << 8),
            gui | (reverse ? 0 : ATI_GMC_WR_MSK_DIS),
            ((PITCH / 64) << 22) | (DST_OFFSET >> 10),
            (1U << 16) | 2U,
            (5U << 16) | 10U,
            color,
            (1U << 16) | 0U,
            (4U << 16) | 3U,
            (8U << 16) | 3U,
            (4U << 16) | 3U,
            R100_SCRATCH_REG0 >> 2, marker,
        };
        uint32_t bad_commands[ARRAY_SIZE(commands) - 1];
        uint32_t pixels[PITCH / sizeof(uint32_t) * HEIGHT];
        uint32_t write_mask = reverse ? 0x00ff00ff : UINT32_MAX;

        /* An incomplete second rectangle must not paint the first one. */
        memcpy(bad_commands, commands, 9 * sizeof(commands[0]));
        bad_commands[0] = R100_CP_PACKET3 | (7U << 16) |
                          (R100_PACKET3_CNTL_PAINT_MULTI << 8);
        bad_commands[9] = R100_SCRATCH_REG0 >> 2;
        bad_commands[10] = marker;
        for (unsigned int word = 0; word < ARRAY_SIZE(commands); word++) {
            commands[word] = cpu_to_le32(commands[word]);
        }
        for (unsigned int word = 0; word < ARRAY_SIZE(bad_commands); word++) {
            bad_commands[word] = cpu_to_le32(bad_commands[word]);
        }
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, canary & 0xff,
                     sizeof(pixels));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                     rejected_marker);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_WRITE_MASK,
                     0x00ff00ff);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     reverse ? 0 : ATI_DST_LTR_TTB);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                     rejected_marker);
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       bad_commands, sizeof(bad_commands));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(bad_commands));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_GUI_MASTER_CNTL), ==, 0);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_BRUSH_FRGD_CLR), ==,
                        rejected_marker);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_WRITE_MASK), ==, 0x00ff00ff);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==,
                        rejected_marker);
        qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                      pixels, sizeof(pixels));
        for (unsigned int i = 0; i < ARRAY_SIZE(pixels); i++) {
            g_assert_cmphex(le32_to_cpu(pixels[i]), ==, canary);
        }

        /* A later submission paints both clipped rectangles with its brush. */
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       commands, sizeof(commands));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ,
                     ARRAY_SIZE(commands));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         ATI_DP_BRUSH_FRGD_CLR), ==, color);
        qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                      pixels, sizeof(pixels));
        for (unsigned int y = 0; y < HEIGHT; y++) {
            for (unsigned int x = 0; x < PITCH / sizeof(uint32_t); x++) {
                uint32_t expected = canary;

                if ((x >= 2 && x < 5 && y >= 1 && y < 3) ||
                    (x >= 8 && x < 10 && y >= 3 && y < 5)) {
                    expected = (canary & ~write_mask) | (color & write_mask);
                }
                g_assert_cmphex(le32_to_cpu(pixels[y * PITCH / 4 + x]),
                                ==, expected);
            }
        }
    }
    qtest_quit(qts);
}

static void ati_rv100_cp_legacy_bitblt(void)
{
    enum {
        SRC_OFFSET = 0x1000,
        DST_OFFSET = 0x2000,
        IB_OFFSET = 0x4000,
        PITCH = 64,
    };
    const uint32_t source[] = { 0x11223344, 0x55667788, 0x99aabbcc };
    const uint32_t destination[] = { 0xa5a5a5a5, 0x24681357, 0xa5a5a5a5 };
    const uint32_t marker = 0x424c4954;
    QTestState *qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 256M -S "
        "-vga ati -global ati-vga.model=rv100");

    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE, IB_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL, ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CLR_CMP_MASK, UINT32_MAX);

    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DEFAULT_SC_BOTTOM_RIGHT,
                 (4U << 16) | (PITCH / 4));

    for (unsigned int transparent = 0; transparent < 2; transparent++) {
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_SRC_PITCH |
                       ATI_GMC_DST_PITCH | ATI_GMC_BRUSH_NONE |
                       ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                       ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT;
        uint32_t commands[12];
        unsigned int word = 0;

        commands[word++] = R100_CP_PACKET3 |
            ((5U + 3U * transparent) << 16) |
            ((transparent ? R100_PACKET3_CNTL_TRANS_BITBLT :
                            R100_PACKET3_CNTL_BITBLT) << 8);
        commands[word++] = gui | (transparent ? 0 : ATI_GMC_CLR_CMP_DIS);
        commands[word++] = ((PITCH / 64) << 22) | (SRC_OFFSET >> 10);
        commands[word++] = ((PITCH / 64) << 22) | (DST_OFFSET >> 10);
        if (transparent) {
            /* Reject source matches and preserve destination matches. */
            commands[word++] = (2U << 24) | (4U << 8) | 4U;
            commands[word++] = source[0];
            commands[word++] = destination[1];
        }
        commands[word++] = 0;
        commands[word++] = (2U << 16) | 1U;
        commands[word++] = (3U << 16) | 1U;
        commands[word++] = R100_SCRATCH_REG0 >> 2;
        commands[word++] = marker;
        for (unsigned int i = 0; i < word; i++) {
            commands[i] = cpu_to_le32(commands[i]);
        }
        for (unsigned int i = 0; i < ARRAY_SIZE(source); i++) {
            qtest_writel(qts, IA64_RV100_FB_BASE + SRC_OFFSET + i * 4,
                         source[i]);
            qtest_writel(qts, IA64_RV100_FB_BASE + DST_OFFSET +
                         PITCH + (i + 2) * 4, destination[i]);
        }
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0, 0);
        qtest_memwrite(qts, IA64_RV100_FB_BASE + IB_OFFSET,
                       commands, word * sizeof(commands[0]));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ, word);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                    R100_SCRATCH_REG0), ==, marker);
        for (unsigned int i = 0; i < ARRAY_SIZE(source); i++) {
            uint32_t expected = transparent && i < 2 ?
                                destination[i] : source[i];

            g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                        DST_OFFSET + PITCH + (i + 2) * 4),
                            ==, expected);
        }
    }
    qtest_quit(qts);
}

static void ati_rv100_command_budget(void)
{
    enum {
        RING_OFFSET = 0x10000,
        IB_DWORDS = 700000,
        VERTEX_RING_OFFSET = 0xb0000,
        VERTEX_OFFSET = 0xb00000,
        VERTEX_COUNT = 16384,
        DRAW_REPEATS = 13,
        VERTEX_RING_DWORDS = 4 + DRAW_REPEATS * 3 + 2,
        BLIT_RING_OFFSET = 0xe00000,
        BLIT_WIDTH = 1024,
        BLIT_HEIGHT = 1024,
        CURSOR_RING_OFFSET = 0xf00000,
        CURSOR_WRITES = 600,
        CURSOR_RING_DWORDS = 1 + CURSOR_WRITES + 2,
    };
    static const uint32_t ib_offset[] = {
        0x100000, 0x400000, 0x700000,
    };
    static const uint32_t marker[] = {
        0x10000001, 0x20000002, 0x30000003,
    };
    uint32_t ring[] = {
        (1U << 16) | (R100_CP_IB_BASE >> 2),
        ib_offset[0], IB_DWORDS,
        (1U << 16) | (R100_CP_IB_BASE >> 2),
        ib_offset[1], IB_DWORDS,
        (1U << 16) | (R100_CP_IB_BASE >> 2),
        ib_offset[2], IB_DWORDS,
    };
    uint32_t vertex_ring[VERTEX_RING_DWORDS];
    uint32_t blit_ring[] = {
        (1U << 16) | R100_CP_PACKET0_ONE_REG | (ATI_DST_WIDTH >> 2),
        BLIT_WIDTH, BLIT_WIDTH,
        (R100_SCRATCH_REG0 + 12) >> 2, marker[2],
    };
    uint32_t cursor_ring[CURSOR_RING_DWORDS];
    g_autofree uint32_t *ib = g_new(uint32_t, IB_DWORDS);
    QTestState *qts;
    unsigned int i, j, word;

    for (i = 0; i < IB_DWORDS - 2; i++) {
        ib[i] = cpu_to_le32(R100_CP_PACKET2);
    }
    ib[IB_DWORDS - 2] = cpu_to_le32((R100_SCRATCH_REG0 + 4) >> 2);
    for (i = 0; i < ARRAY_SIZE(ring); i++) {
        ring[i] = cpu_to_le32(ring[i]);
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100 "
                     "-global ati-vga.guest_hwcursor=off");
    qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                   ring, sizeof(ring));
    for (i = 0; i < ARRAY_SIZE(ib_offset); i++) {
        ib[IB_DWORDS - 1] = cpu_to_le32(marker[i]);
        qtest_memwrite(qts, IA64_RV100_FB_BASE + ib_offset[i], ib,
                       IB_DWORDS * sizeof(*ib));
    }

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 4);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDBM);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(ring));

    /* The shared budget completes two IBs, then rejects the third. */
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0 + 4), ==, marker[1]);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_RB_RPTR), ==, ARRAY_SIZE(ring));
    for (j = 0; j < ARRAY_SIZE(ib_offset); j++) {
        g_assert_cmpuint(ib_offset[j] + IB_DWORDS * sizeof(*ib),
                         <, ATI_TEST_VRAM_SIZE);
    }

    qtest_system_reset(qts);
    ati_pci_enable(qts);
    word = 0;
    vertex_ring[word++] = R100_CP_PACKET3 | (2U << 16) |
                          (R100_PACKET3_LOAD_VBPNTR << 8);
    vertex_ring[word++] = 1;
    vertex_ring[word++] = 2U | (2U << 8);
    vertex_ring[word++] = VERTEX_OFFSET;
    for (i = 0; i < DRAW_REPEATS; i++) {
        vertex_ring[word++] = R100_CP_PACKET3 | (1U << 16) |
                              (R100_PACKET3_DRAW_VBUF << 8);
        vertex_ring[word++] = 0;
        vertex_ring[word++] = R100_VF_TRIANGLE_LIST |
                              R100_VF_WALK_LIST | (VERTEX_COUNT << 16);
    }
    vertex_ring[word++] = (R100_SCRATCH_REG0 + 8) >> 2;
    vertex_ring[word++] = marker[2];
    g_assert_cmpuint(word, ==, ARRAY_SIZE(vertex_ring));
    for (i = 0; i < ARRAY_SIZE(vertex_ring); i++) {
        vertex_ring[i] = cpu_to_le32(vertex_ring[i]);
    }
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_RING_OFFSET,
                   vertex_ring, sizeof(vertex_ring));
    qtest_memset(qts, IA64_RV100_FB_BASE + VERTEX_OFFSET, 0,
                 VERTEX_COUNT * 2 * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0 + 8,
                 marker[0]);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 VERTEX_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 5);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(vertex_ring));

    /* Degenerate DMA draws consume vertex work despite drawing no pixels. */
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0 + 8), ==, marker[0]);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_RB_RPTR), ==,
                     ARRAY_SIZE(vertex_ring));

    qtest_system_reset(qts);
    ati_pci_enable(qts);
    for (i = 0; i < ARRAY_SIZE(blit_ring); i++) {
        blit_ring[i] = cpu_to_le32(blit_ring[i]);
    }
    qtest_memwrite(qts, IA64_RV100_FB_BASE + BLIT_RING_OFFSET,
                   blit_ring, sizeof(blit_ring));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_OFFSET, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_PITCH,
                 BLIT_WIDTH * sizeof(uint32_t));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_SC_BOTTOM_RIGHT,
                 (BLIT_HEIGHT << 16) | BLIT_WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                 ATI_DST_LTR_TTB);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_BRUSH_FRGD_CLR,
                 0x11223344);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_DST_PITCH |
                 ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_SOLID | ATI_GMC_DST_32BPP |
                 ATI_GMC_ROP3_PATCOPY);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_X, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_Y, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DST_HEIGHT, BLIT_HEIGHT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0 + 12,
                 marker[0]);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 BLIT_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 2);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(blit_ring));

    /* 2D work has a separate budget from parser and vertex processing. */
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE), ==, 0x11223344);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0 + 12), ==, marker[2]);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_RB_RPTR), ==,
                     ARRAY_SIZE(blit_ring));

    qtest_system_reset(qts);
    ati_pci_enable(qts);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_CUR_EN);
    cursor_ring[0] = ((CURSOR_WRITES - 1) << 16) |
                     R100_CP_PACKET0_ONE_REG | (ATI_CUR_CLR1 >> 2);
    for (i = 1; i <= CURSOR_WRITES; i++) {
        cursor_ring[i] = 0x00ffffff;
    }
    cursor_ring[CURSOR_WRITES + 1] = (R100_SCRATCH_REG0 + 16) >> 2;
    cursor_ring[CURSOR_WRITES + 2] = marker[2];
    for (i = 0; i < ARRAY_SIZE(cursor_ring); i++) {
        cursor_ring[i] = cpu_to_le32(cursor_ring[i]);
    }
    qtest_memwrite(qts, IA64_RV100_FB_BASE + CURSOR_RING_OFFSET,
                   cursor_ring, sizeof(cursor_ring));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0 + 16,
                 marker[0]);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 CURSOR_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 9);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(cursor_ring));

    /* Each host cursor rebuild charges all 64x64 expanded pixels. */
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0 + 16), ==, marker[0]);
    g_assert_cmpuint(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                 R100_CP_RB_RPTR), ==,
                     ARRAY_SIZE(cursor_ring));
    qtest_quit(qts);
}

static void ati_rv100_draw_immediate(QTestState *qts, uint32_t format,
                                     uint32_t primitive,
                                     const uint32_t *data,
                                     size_t data_dwords,
                                     unsigned int vertex_count)
{
    uint32_t vf_cntl = primitive | R100_VF_WALK_DATA |
                       R100_VF_COLOR_RGBA | (vertex_count << 16);
    size_t i;

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_VTX_FMT, format);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_VF_CNTL, vf_cntl);
    for (i = 0; i < data_dwords; i++) {
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_PORT_DATA0,
                     data[i]);
    }
}

static uint32_t ati_rv100_rop(unsigned int rop, uint32_t src, uint32_t dst)
{
    switch (rop) {
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

static uint32_t ati_rv100_rgba_vertex_color(uint32_t argb)
{
    return (argb & 0xff00ff00U) | ((argb & 0xffU) << 16) |
           ((argb >> 16) & 0xffU);
}

static void ati_rv100_vertex_fog(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
    };
    const uint32_t fp_fog_point[] = {
        f32_bits(2.0f), f32_bits(2.0f),
        ati_rv100_rgba_vertex_color(0x20000000),
        f32_bits(2.0f), f32_bits(0.0f), f32_bits(0.0f),
        f32_bits(0.25f),
    };
    const uint32_t packed_fog_point[] = {
        f32_bits(4.0f), f32_bits(2.0f),
        ati_rv100_rgba_vertex_color(0x20ffffff), 0x80000000,
    };
    const uint32_t table_fog_points[] = {
        f32_bits(6.0f), f32_bits(2.0f), f32_bits(0.0f),
        ati_rv100_rgba_vertex_color(0x30506070), f32_bits(1.0f),
        f32_bits(8.0f), f32_bits(2.0f), f32_bits(64.5f / 256.0f),
        ati_rv100_rgba_vertex_color(0x30506070), f32_bits(0.0f),
        f32_bits(10.0f), f32_bits(2.0f), f32_bits(1.0f),
        ati_rv100_rgba_vertex_color(0x30506070), f32_bits(0.0f),
    };
    const uint32_t diffuse_table_fog_point[] = {
        f32_bits(12.0f), f32_bits(2.0f), f32_bits(0.0f),
        ati_rv100_rgba_vertex_color(0x30506070), f32_bits(0.0f),
    };
    const uint32_t spec_table_fog_point[] = {
        f32_bits(14.0f), f32_bits(2.0f), f32_bits(0.0f),
        ati_rv100_rgba_vertex_color(0x30506070), f32_bits(0.5f),
    };
    const uint32_t migrated_table_fog_point[] = {
        f32_bits(16.0f), f32_bits(2.0f), f32_bits(64.5f / 256.0f),
        ati_rv100_rgba_vertex_color(0x30506070),
    };
    const uint32_t migrated_table_index_point[] = {
        f32_bits(18.0f), f32_bits(2.0f), f32_bits(200.0f / 256.0f),
        ati_rv100_rgba_vertex_color(0x30506070),
    };
    const uint32_t fog_triangles[] = {
        f32_bits(4.0f), f32_bits(36.0f), UINT32_MAX, f32_bits(0.0f),
        f32_bits(20.0f), f32_bits(36.0f), UINT32_MAX, f32_bits(1.0f),
        f32_bits(4.0f), f32_bits(52.0f), UINT32_MAX, f32_bits(1.0f),
        f32_bits(36.0f), f32_bits(36.0f), UINT32_MAX, f32_bits(0.0f),
        f32_bits(36.0f), f32_bits(52.0f), UINT32_MAX, f32_bits(1.0f),
        f32_bits(52.0f), f32_bits(36.0f), UINT32_MAX, f32_bits(1.0f),
    };
    const uint64_t color_base = IA64_RV100_FB_BASE;
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    g_autofree char *path = g_strdup_printf(
        "%s/ati-rv100-table-fog.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    QTestState *qts;
    int fd;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_memset(qts, color_base, 0, WIDTH * HEIGHT * 4);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10) |
                 (2U << 14));
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    /* PP_FOG_COLOR's factor-source bits are unused in vertex-fog mode. */
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR,
                 R100_FOG_USE_DIFFUSE_ALPHA | 0x000000ff);

    /* Color sum is clamped before fog; primary alpha bypasses fog unchanged. */
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_PP_SPECULAR_ENABLE | R100_PP_FOG_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_FPSPEC | R100_VTX_FMT_FPFOG,
                             R100_VF_POINT_LIST, fp_fog_point,
                             ARRAY_SIZE(fp_fog_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 2) * 4),
                    ==, 0x204000bf);

    /* Software TCL packs the same fog factor into PKSPEC alpha. */
    qtest_writel(qts, mmio + R100_PP_CNTL, R100_PP_FOG_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_PKSPEC, R100_VF_POINT_LIST,
                             packed_fog_point, ARRAY_SIZE(packed_fog_point),
                             1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 4) * 4),
                    ==, 0x208080ff);

    /* DATA's low 8 bits post-increment INDEX; Z interpolates table entries. */
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 0);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 0);
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 48);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 64);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 0xdeadbe40);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 0xcafebac0);
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 128);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 255);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR,
                 R100_FOG_TABLE | 0x000000ff);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z | R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_FPFOG, R100_VF_POINT_LIST,
                             table_fog_points, ARRAY_SIZE(table_fog_points),
                             3);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 6) * 4),
                    ==, 0x300000ff);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 8) * 4),
                    ==, 0x302830b7);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 10) * 4),
                    ==, 0x30506070);

    /* The table source field can select either interpolated color alpha. */
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR,
                 R100_FOG_TABLE | R100_FOG_USE_DIFFUSE_ALPHA | 0x000000ff);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z | R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_FPFOG, R100_VF_POINT_LIST,
                             diffuse_table_fog_point,
                             ARRAY_SIZE(diffuse_table_fog_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 12) * 4),
                    ==, 0x30506070);
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR,
                 R100_FOG_TABLE | R100_FOG_USE_SPEC_ALPHA | 0x000000ff);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z | R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_FPFOG, R100_VF_POINT_LIST,
                             spec_table_fog_point,
                             ARRAY_SIZE(spec_table_fog_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 14) * 4),
                    ==, 0x30506070);

    /* Both table RAM and its current write index survive migration. */
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR,
                 R100_FOG_TABLE | 0x000000ff);
    qtest_writel(qts, mmio + R100_FOG_TABLE_INDEX, 200);
    qtest_writel(qts, mmio + ATI_BRUSH_Y_X, (6U << 8) | 5U);
    qtest_writel(qts, mmio + ATI_BRUSH_DATA0, 0x89abcdef);
    qtest_writel(qts, mmio + ATI_BRUSH_DATA0 + 63 * 4, 0x01234567);
    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100 "
                     "-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_BRUSH_Y_X), ==,
                    (6U << 8) | 5U);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_BRUSH_DATA0), ==,
                    0x89abcdef);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_BRUSH_DATA0 + 63 * 4), ==,
                    0x01234567);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z | R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, migrated_table_fog_point,
                             ARRAY_SIZE(migrated_table_fog_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 16) * 4),
                    ==, 0x302830b7);
    qtest_writel(qts, mmio + R100_FOG_TABLE_DATA, 255);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z | R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST,
                             migrated_table_index_point,
                             ARRAY_SIZE(migrated_table_index_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + (2 * WIDTH + 18) * 4),
                    ==, 0x30506070);

    /* Gouraud fog interpolation is independent of triangle winding. */
    qtest_writel(qts, mmio + R100_PP_FOG_COLOR, 0);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_FPFOG, R100_VF_TRIANGLE_LIST,
                             fog_triangles, ARRAY_SIZE(fog_triangles), 6);
    g_assert_cmphex(qtest_readl(qts, color_base + (40 * WIDTH + 8) * 4),
                    ==, 0xff8f8f8f);
    g_assert_cmphex(qtest_readl(qts, color_base + (40 * WIDTH + 40) * 4),
                    ==, 0xff8f8f8f);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void ati_rv100_endian_scratch_rop(void)
{
    enum {
        WIDTH = 32,
        HEIGHT = 32,
        BE_RING_OFFSET = 0x40000,
        VERTEX_RING_OFFSET = 0x41000,
        VERTEX_OFFSET = 0x42000,
        RPTR_WB_OFFSET = 0x43000,
    };
    const uint32_t ring_marker = 0x52494e47;
    const uint32_t vertex_color = 0xff2468ac;
    const uint32_t scratch_marker = 0x53435241;
    const uint32_t rop_src = 0x3c12a5e7;
    const uint32_t rop_dst = 0xa55ac33c;
    uint32_t be_ring[] = {
        R100_SCRATCH_REG0 >> 2, ring_marker,
    };
    uint32_t vertex_ring[] = {
        R100_CP_PACKET3 | (2U << 16) |
        (R100_PACKET3_LOAD_VBPNTR << 8),
        1, 3U | (3U << 8), VERTEX_OFFSET,
        R100_CP_PACKET3 | (1U << 16) | (R100_PACKET3_DRAW_VBUF << 8),
        R100_VTX_FMT_PKCOLOR,
        R100_VF_POINT_LIST | R100_VF_WALK_LIST | R100_VF_COLOR_RGBA |
        (1U << 16),
    };
    uint32_t be_vertex[] = {
        f32_bits(6.0f), f32_bits(7.0f),
        ati_rv100_rgba_vertex_color(vertex_color),
    };
    QTestState *qts;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(be_ring); i++) {
        be_ring[i] = cpu_to_be32(be_ring[i]);
    }
    for (i = 0; i < ARRAY_SIZE(vertex_ring); i++) {
        vertex_ring[i] = cpu_to_le32(vertex_ring[i]);
    }
    for (i = 0; i < ARRAY_SIZE(be_vertex); i++) {
        be_vertex[i] = cpu_to_be32(be_vertex[i]);
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_memset(qts, IA64_RV100_FB_BASE, 0, 0x50000);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + BE_RING_OFFSET,
                   be_ring, sizeof(be_ring));

    /* PPC drivers store ring dwords in big-endian byte order. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 BE_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE |
                 R100_RB_BUF_SWAP_32BIT | 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(be_ring));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SCRATCH_REG0), ==, ring_marker);

    /* RPTR writeback has an independent byte-swap control in ADDR[1:0]. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_ADDR,
                 RPTR_WB_OFFSET | R100_RPTR_SWAP_32BIT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_BUF_SWAP_32BIT | 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(be_ring));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE + RPTR_WB_OFFSET),
                    ==, bswap32(ARRAY_SIZE(be_ring)));

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_WIDTH_HEIGHT,
                 ((HEIGHT - 1) << 16) | (WIDTH - 1));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_RING_OFFSET,
                   vertex_ring, sizeof(vertex_ring));
    qtest_memwrite(qts, IA64_RV100_FB_BASE + VERTEX_OFFSET,
                   be_vertex, sizeof(be_vertex));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_CNTL_STATUS,
                 R100_VC_SWAP_32BIT);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                R100_SE_CNTL_STATUS), ==,
                    R100_TCL_BYPASS | R100_VC_SWAP_32BIT);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 VERTEX_RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 3);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(vertex_ring));
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (7 * WIDTH + 6) * 4), ==, vertex_color);

    /* SCRATCH_ADDR bits 0..4 are controls, and address zero is valid. */
    qtest_writel(qts, IA64_RV100_FB_BASE, 0xdeadbeef);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_ADDR, 0x1f);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_UMSK, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 scratch_marker);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE), ==,
                    scratch_marker);

    /* SCRATCH_UMSK[17:16] controls the scratch DMA writeback byte order. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_UMSK,
                 R100_SCRATCH_SWAP_32BIT | 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 scratch_marker);
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE), ==,
                    bswap32(scratch_marker));

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_CNTL_STATUS, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_ROP_ENABLE);
    for (i = 0; i < 16; i++) {
        uint32_t point[] = {
            f32_bits(i + 1), f32_bits(20.0f),
            ati_rv100_rgba_vertex_color(rop_src),
        };
        uint64_t pixel = IA64_RV100_FB_BASE +
                         (20 * WIDTH + i + 1) * 4;

        qtest_writel(qts, pixel, rop_dst);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_ROPCNTL,
                     i << 8);
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                                 R100_VF_POINT_LIST, point,
                                 ARRAY_SIZE(point), 1);
        g_assert_cmphex(qtest_readl(qts, pixel), ==,
                        ati_rv100_rop(i, rop_src, rop_dst));
    }
    qtest_quit(qts);
}

static void ati_rv100_gart(void)
{
    enum {
        WIDTH = 16,
        HEIGHT = 16,
        GPU_FB_BASE = 0x10000000,
        GART_BASE = 0x00100000,
        RING_OFFSET = 0x30000,
        PAGE_TABLE = 0x08000000,
        GART_PAGE0 = 0x08001000,
        CONTIGUOUS_DECOY = 0x08002000,
        GART_PAGE1 = 0x08003000,
        OUTSIDE_ADDRESS = 0x08005000,
    };
    const uint32_t color = 0xff55aa33;
    const uint32_t outside_canary = 0xdec0adde;
    const uint32_t outside_marker = 0x47505521;
    uint32_t ring[] = {
        R100_CP_PACKET3 | (2U << 16) |
        (R100_PACKET3_LOAD_VBPNTR << 8),
        1, 3U | (3U << 8), GART_BASE + 0xffe,
        R100_CP_PACKET3 | (1U << 16) | (R100_PACKET3_DRAW_VBUF << 8),
        R100_VTX_FMT_PKCOLOR,
        R100_VF_POINT_LIST | R100_VF_WALK_LIST | R100_VF_COLOR_RGBA |
        (1U << 16),
    };
    uint32_t vertex[] = {
        f32_bits(6.0f), f32_bits(7.0f),
        ati_rv100_rgba_vertex_color(color),
    };
    QTestState *qts;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(ring); i++) {
        ring[i] = cpu_to_le32(ring[i]);
    }
    for (i = 0; i < ARRAY_SIZE(vertex); i++) {
        vertex[i] = cpu_to_le32(vertex[i]);
    }

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);
    qtest_memset(qts, IA64_RV100_FB_BASE, 0, 0x40000);
    qtest_memset(qts, CONTIGUOUS_DECOY, 0, 0x1000);
    qtest_memwrite(qts, GART_PAGE0 + 0xffe, vertex, 2);
    qtest_memwrite(qts, GART_PAGE1, (uint8_t *)vertex + 2,
                   sizeof(vertex) - 2);
    qtest_writel(qts, PAGE_TABLE, GART_PAGE0);
    qtest_writel(qts, PAGE_TABLE + 4, GART_PAGE1);
    qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                   ring, sizeof(ring));

    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_MC_FB_LOCATION,
                 (0x10ffU << 16) | 0x1000U);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_AIC_PT_BASE,
                 PAGE_TABLE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_AIC_LO_ADDR,
                 GART_BASE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_AIC_HI_ADDR,
                 GART_BASE + 0x1fff);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_AIC_CNTL,
                 R100_PCIGART_TRANSLATE_EN |
                 R100_DIS_OUT_OF_PCI_GART_ACCESS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLOROFFSET,
                 GPU_FB_BASE);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RE_WIDTH_HEIGHT,
                 ((HEIGHT - 1) << 16) | (WIDTH - 1));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_PP_CNTL, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_BASE,
                 GPU_FB_BASE + RING_OFFSET);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_CNTL,
                 R100_RB_RPTR_WR_ENA | R100_RB_NO_UPDATE | 3);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_RPTR_WR, 0);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                 R100_CSQ_PRIBM_INDDIS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_RB_WPTR,
                 ARRAY_SIZE(ring));

    /* Vertex dword 0 spans two non-contiguous GART PTEs. */
    g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                (7 * WIDTH + 6) * 4), ==, color);

    qtest_writel(qts, OUTSIDE_ADDRESS, outside_canary);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_ADDR,
                 OUTSIDE_ADDRESS);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_UMSK, 1);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 outside_marker);
    g_assert_cmphex(qtest_readl(qts, OUTSIDE_ADDRESS), ==, outside_canary);

    /* Clearing DIS_OUT restores untranslated PCI DMA outside the aperture. */
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_AIC_CNTL,
                 R100_PCIGART_TRANSLATE_EN);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0,
                 outside_marker);
    g_assert_cmphex(qtest_readl(qts, OUTSIDE_ADDRESS), ==, outside_marker);
    qtest_quit(qts);
}

static void ati_radeon_2d_copy(QTestState *qts, uint32_t source,
                               uint32_t destination, unsigned int width)
{
    uint64_t mmio = IA64_RV100_MMIO_BASE;
    uint32_t source_base = source & ~0x3ffU;
    uint32_t destination_base = destination & ~0x3ffU;
    unsigned int source_x = (source - source_base) / sizeof(uint32_t);
    unsigned int destination_x =
        (destination - destination_base) / sizeof(uint32_t);
    uint32_t pitch = 4096;

    qtest_writel(qts, mmio + ATI_SRC_PITCH_OFFSET,
                 ((pitch / 64) << 22) | (source_base >> 10));
    qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                 ((pitch / 64) << 22) | (destination_base >> 10));
    qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, destination_x);
    qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                 (1U << 16) | (destination_x + width));
    qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT,
                 (1U << 16) | (source_x + width));
    qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
    qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_NONE |
                 ATI_GMC_DST_32BPP | ATI_GMC_SRC_COLOR |
                 ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, mmio + ATI_SRC_X, source_x);
    qtest_writel(qts, mmio + ATI_SRC_Y, 0);
    qtest_writel(qts, mmio + ATI_DST_X, destination_x);
    qtest_writel(qts, mmio + ATI_DST_Y, 0);
    qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, mmio + ATI_DST_WIDTH, width);
}

static void ati_radeon_2d_overlap_copy(QTestState *qts, bool reverse)
{
    uint64_t mmio = IA64_RV100_MMIO_BASE;
    uint32_t pitch_offset = ((4096 / 64) << 22) | (0x00100000 >> 10);
    unsigned int source_x = reverse ? 67 : 64;
    unsigned int destination_x = reverse ? 68 : 65;

    qtest_writel(qts, mmio + ATI_SRC_PITCH_OFFSET, pitch_offset);
    qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET, pitch_offset);
    qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 65);
    qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT, (1U << 16) | 69U);
    qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT,
                 (1U << 16) | 68U);
    qtest_writel(qts, mmio + ATI_DP_CNTL,
                 reverse ? ATI_DST_RTL_TTB : ATI_DST_LTR_TTB);
    qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                 ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                 ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                 ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                 ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                 ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                 ATI_GMC_DP_SRC_RECT);
    qtest_writel(qts, mmio + ATI_SRC_X, source_x);
    qtest_writel(qts, mmio + ATI_SRC_Y, 0);
    qtest_writel(qts, mmio + ATI_DST_X, destination_x);
    qtest_writel(qts, mmio + ATI_DST_Y, 0);
    qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
    qtest_writel(qts, mmio + ATI_DST_WIDTH, 4);
}

static void ati_radeon_2d_gart(void)
{
    enum {
        GPU_FB_BASE = 0x10000000,
        GART_BASE = 0x00100000,
        GART_CROSSING = GART_BASE + 0xff0,
        VRAM_SOURCE = 0x00004000,
        VRAM_DESTINATION = 0x00005000,
        PITCH_DESTINATION = 0x00008000,
        PAGE_TABLE = 0x08000000,
        GART_PAGE0 = 0x08001000,
        CONTIGUOUS_DECOY = 0x08002000,
        GART_PAGE1 = 0x08003000,
        OUTSIDE_ADDRESS = 0x08005000,
        HOST_GART_PAGE_BASE = 0x08400000,
        HOST_ROWS = 64,
        HOST_WIDTH = 4,
        INVALID_PAGE = 0x40000000,
    };
    static const char *models[] = { "rv100", "es1000" };
    const uint32_t source[] = {
        0x00112233, 0x44556677, 0x8899aabb, 0xccddeeff,
        0x10213243, 0x54657687, 0x98a9bacb, 0xdcedfe0f,
    };
    const uint32_t reverse[] = {
        0xfedcba98, 0x76543210, 0x0badc0de, 0xc001d00d,
        0x13579bdf, 0x2468ace0, 0x55aa33cc, 0xaa55cc33,
    };
    const uint32_t destination[] = {
        0xaaaaaaaa, 0x55555555, 0x0f0f0f0f, 0xf0f0f0f0,
        0x12345678, 0x87654321, 0x00ff00ff, 0xff00ff00,
    };
    const uint32_t host[] = {
        0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004,
        0xdead0005, 0xdead0006, 0xdead0007, 0xdead0008,
    };
    const uint32_t pitch_rows[] = {
        0x11000001, 0x11000002, 0x11000003, 0x11000004,
        0x22000001, 0x22000002, 0x22000003, 0x22000004,
    };
    const uint32_t overlap_initial[] = { 1, 2, 3, 4, 5, 6 };
    const uint32_t overlap_reverse[] = { 1, 1, 2, 3, 4, 6 };
    const uint32_t overlap_forward[] = { 1, 1, 1, 1, 1, 6 };
    uint32_t source_le[ARRAY_SIZE(source)];
    uint32_t reverse_le[ARRAY_SIZE(reverse)];
    uint32_t destination_le[ARRAY_SIZE(destination)];
    uint32_t xor_le[ARRAY_SIZE(destination)];
    uint32_t host_le[ARRAY_SIZE(host)];
    uint32_t pitch_rows_le[ARRAY_SIZE(pitch_rows)];
    uint32_t overlap_initial_le[ARRAY_SIZE(overlap_initial)];
    uint32_t overlap_reverse_le[ARRAY_SIZE(overlap_reverse)];
    uint32_t overlap_forward_le[ARRAY_SIZE(overlap_forward)];
    uint8_t actual[sizeof(source)];
    uint8_t canary[16];
    uint8_t decoy[16];
    uint8_t mono_actual[8];
    const uint8_t mono_expected[] = {
        0xee, 0x11, 0xee, 0x11, 0xee, 0x11, 0xee, 0x11,
    };
    unsigned int model_index;

    for (unsigned int i = 0; i < ARRAY_SIZE(source); i++) {
        source_le[i] = cpu_to_le32(source[i]);
        reverse_le[i] = cpu_to_le32(reverse[i]);
        destination_le[i] = cpu_to_le32(destination[i]);
        xor_le[i] = cpu_to_le32(reverse[i] ^ destination[i]);
    }
    for (unsigned int i = 0; i < ARRAY_SIZE(host); i++) {
        host_le[i] = cpu_to_le32(host[i]);
    }
    for (unsigned int i = 0; i < ARRAY_SIZE(pitch_rows); i++) {
        pitch_rows_le[i] = cpu_to_le32(pitch_rows[i]);
    }
    for (unsigned int i = 0; i < ARRAY_SIZE(overlap_initial); i++) {
        overlap_initial_le[i] = cpu_to_le32(overlap_initial[i]);
        overlap_reverse_le[i] = cpu_to_le32(overlap_reverse[i]);
        overlap_forward_le[i] = cpu_to_le32(overlap_forward[i]);
    }
    memset(canary, 0xa5, sizeof(canary));

    for (model_index = 0; model_index < ARRAY_SIZE(models); model_index++) {
        g_autofree char *args = g_strdup_printf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", models[model_index]);
        QTestState *qts = qtest_init(args);
        uint64_t mmio = IA64_RV100_MMIO_BASE;

        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + VRAM_SOURCE, 0,
                     sizeof(source));
        qtest_memset(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION, 0,
                     sizeof(source));
        qtest_writel(qts, PAGE_TABLE, GART_PAGE0);
        qtest_writel(qts, PAGE_TABLE + 4, GART_PAGE1);
        qtest_memset(qts, CONTIGUOUS_DECOY, 0x5a, sizeof(decoy));
        qtest_writel(qts, mmio + R100_MC_FB_LOCATION,
                     (0x10ffU << 16) | 0x1000U);
        qtest_writel(qts, mmio + R100_AIC_PT_BASE, PAGE_TABLE);
        qtest_writel(qts, mmio + R100_AIC_LO_ADDR, GART_BASE);
        qtest_writel(qts, mmio + R100_AIC_HI_ADDR, GART_BASE + 0x1fff);
        qtest_writel(qts, mmio + R100_AIC_CNTL,
                     R100_PCIGART_TRANSLATE_EN |
                     R100_DIS_OUT_OF_PCI_GART_ACCESS);

        qtest_memwrite(qts, GART_PAGE0 + 0x100, overlap_initial_le,
                       sizeof(overlap_initial_le));
        ati_radeon_2d_overlap_copy(qts, true);
        qtest_memread(qts, GART_PAGE0 + 0x100, actual,
                      sizeof(overlap_reverse_le));
        g_assert_cmpmem(actual, sizeof(overlap_reverse_le),
                        overlap_reverse_le, sizeof(overlap_reverse_le));

        qtest_memwrite(qts, GART_PAGE0 + 0x100, overlap_initial_le,
                       sizeof(overlap_initial_le));
        ati_radeon_2d_overlap_copy(qts, false);
        qtest_memread(qts, GART_PAGE0 + 0x100, actual,
                      sizeof(overlap_forward_le));
        g_assert_cmpmem(actual, sizeof(overlap_forward_le),
                        overlap_forward_le, sizeof(overlap_forward_le));

        /* Packed offsets and pitches advance both surfaces independently. */
        qtest_memwrite(qts, GART_PAGE0 + 0x100, pitch_rows_le,
                       4 * sizeof(uint32_t));
        qtest_memwrite(qts, GART_PAGE1 + 0x100, pitch_rows_le + 4,
                       4 * sizeof(uint32_t));
        qtest_memset(qts, IA64_RV100_FB_BASE + PITCH_DESTINATION, 0xa5,
                     3 * 4096);
        qtest_writel(qts, mmio + ATI_SRC_PITCH_OFFSET,
                     ((4096 / 64) << 22) | (GART_BASE >> 10));
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((2048 / 64) << 22) |
                     ((GPU_FB_BASE + PITCH_DESTINATION) >> 10));
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 1U << 16);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (3U << 16) | 4U);
        qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT,
                     (2U << 16) | 68U);
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                     ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                     ATI_GMC_DP_SRC_RECT);
        qtest_writel(qts, mmio + ATI_SRC_X, 64);
        qtest_writel(qts, mmio + ATI_SRC_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_Y, 1);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 2);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 4);
        qtest_memread(qts, IA64_RV100_FB_BASE + PITCH_DESTINATION + 2048,
                      actual, 4 * sizeof(uint32_t));
        g_assert_cmpmem(actual, 4 * sizeof(uint32_t), pitch_rows_le,
                        4 * sizeof(uint32_t));
        qtest_memread(qts, IA64_RV100_FB_BASE + PITCH_DESTINATION + 4096,
                      actual, 4 * sizeof(uint32_t));
        g_assert_cmpmem(actual, 4 * sizeof(uint32_t), pitch_rows_le + 4,
                        4 * sizeof(uint32_t));
        g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                    PITCH_DESTINATION), ==, 0xa5);
        g_assert_cmphex(qtest_readb(qts, IA64_RV100_FB_BASE +
                                    PITCH_DESTINATION + 2048 + 16), ==,
                        0xa5);

        /* A single 2D source row spans two non-contiguous GART PTEs. */
        qtest_memwrite(qts, GART_PAGE0 + 0xff0, source_le, 16);
        qtest_memwrite(qts, GART_PAGE1, (uint8_t *)source_le + 16, 16);
        ati_radeon_2d_copy(qts, GART_CROSSING,
                           GPU_FB_BASE + VRAM_DESTINATION,
                           ARRAY_SIZE(source));
        qtest_memread(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION,
                      actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), source_le,
                        sizeof(source_le));

        /* The same address decoder also supports VRAM-to-GART blits. */
        qtest_memwrite(qts, IA64_RV100_FB_BASE + VRAM_SOURCE,
                       reverse_le, sizeof(reverse_le));
        qtest_memset(qts, GART_PAGE0 + 0xff0, 0, 16);
        qtest_memset(qts, GART_PAGE1, 0, 16);
        ati_radeon_2d_copy(qts, GPU_FB_BASE + VRAM_SOURCE, GART_CROSSING,
                           ARRAY_SIZE(reverse));
        qtest_memread(qts, GART_PAGE0 + 0xff0, actual, 16);
        qtest_memread(qts, GART_PAGE1, actual + 16, 16);
        g_assert_cmpmem(actual, sizeof(actual), reverse_le,
                        sizeof(reverse_le));

        /* Generic ROPs read and write the translated destination row. */
        qtest_memwrite(qts, GART_PAGE0 + 0xff0, destination_le, 16);
        qtest_memwrite(qts, GART_PAGE1, (uint8_t *)destination_le + 16, 16);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                     ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCINVERT |
                     ATI_GMC_DP_SRC_RECT);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, ARRAY_SIZE(destination));
        qtest_memread(qts, GART_PAGE0 + 0xff0, actual, 16);
        qtest_memread(qts, GART_PAGE1, actual + 16, 16);
        g_assert_cmpmem(actual, sizeof(actual), xor_le, sizeof(xor_le));

        /* A source color-compare skip preserves staged GART destination. */
        qtest_memwrite(qts, IA64_RV100_FB_BASE + VRAM_SOURCE,
                       reverse_le, 2 * sizeof(uint32_t));
        qtest_writel(qts, GART_PAGE0 + 0x200, 0xaabbccdd);
        qtest_writel(qts, GART_PAGE0 + 0x204, 0xaabbccdd);
        qtest_writel(qts, mmio + ATI_SRC_PITCH_OFFSET,
                     ((4096 / 64) << 22) |
                     ((GPU_FB_BASE + VRAM_SOURCE) >> 10));
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((4096 / 64) << 22) | (GART_BASE >> 10));
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 128);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (1U << 16) | 130U);
        qtest_writel(qts, mmio + ATI_SRC_SC_BOTTOM_RIGHT,
                     (1U << 16) | 2U);
        qtest_writel(qts, mmio + ATI_CLR_CMP_CLR_SRC, reverse[0]);
        qtest_writel(qts, mmio + ATI_CLR_CMP_MASK, UINT32_MAX);
        qtest_writel(qts, mmio + ATI_CLR_CMP_CNTL, (1U << 24) | 4U);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS |
                     ATI_GMC_SRC_PITCH | ATI_GMC_DST_PITCH |
                     ATI_GMC_SRC_CLIPPING | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                     ATI_GMC_DP_SRC_RECT);
        qtest_writel(qts, mmio + ATI_SRC_X, 0);
        qtest_writel(qts, mmio + ATI_SRC_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_X, 128);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 2);
        g_assert_cmphex(qtest_readl(qts, GART_PAGE0 + 0x200), ==,
                        0xaabbccdd);
        g_assert_cmphex(qtest_readl(qts, GART_PAGE0 + 0x204), ==,
                        reverse[1]);

        /* Validate every destination PTE before modifying the first page. */
        qtest_memwrite(qts, GART_PAGE0 + 0xff0, canary, sizeof(canary));
        qtest_writel(qts, PAGE_TABLE + 4, INVALID_PAGE);
        ati_radeon_2d_copy(qts, GPU_FB_BASE + VRAM_SOURCE, GART_CROSSING,
                           ARRAY_SIZE(reverse));
        qtest_memread(qts, GART_PAGE0 + 0xff0, actual, sizeof(canary));
        g_assert_cmpmem(actual, sizeof(canary), canary, sizeof(canary));

        qtest_memset(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION, 0xa5,
                     sizeof(actual));
        ati_radeon_2d_copy(qts, GART_CROSSING,
                           GPU_FB_BASE + VRAM_DESTINATION,
                           ARRAY_SIZE(source));
        qtest_memread(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION,
                      actual, sizeof(actual));
        for (unsigned int i = 0; i < ARRAY_SIZE(actual); i++) {
            g_assert_cmphex(actual[i], ==, 0xa5);
        }
        qtest_writel(qts, PAGE_TABLE + 4, GART_PAGE1);

        /* Packed base plus coordinates must not wrap the 32-bit GPU VA. */
        qtest_memset(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION, 0xa5, 8);
        ati_radeon_2d_copy(qts, 0xfffffffc,
                           GPU_FB_BASE + VRAM_DESTINATION, 2);
        qtest_memread(qts, IA64_RV100_FB_BASE + VRAM_DESTINATION,
                      actual, 8);
        for (unsigned int i = 0; i < 8; i++) {
            g_assert_cmphex(actual[i], ==, 0xa5);
        }

        qtest_memwrite(qts, OUTSIDE_ADDRESS, canary, sizeof(canary));
        ati_radeon_2d_copy(qts, GPU_FB_BASE + VRAM_SOURCE,
                           OUTSIDE_ADDRESS, 4);
        qtest_memread(qts, OUTSIDE_ADDRESS, actual, sizeof(canary));
        g_assert_cmpmem(actual, sizeof(canary), canary, sizeof(canary));
        qtest_writel(qts, mmio + R100_AIC_CNTL,
                     R100_PCIGART_TRANSLATE_EN);
        ati_radeon_2d_copy(qts, GPU_FB_BASE + VRAM_SOURCE,
                           OUTSIDE_ADDRESS, 4);
        qtest_memread(qts, OUTSIDE_ADDRESS, actual, sizeof(canary));
        g_assert_cmpmem(actual, sizeof(canary), reverse_le, sizeof(canary));
        qtest_writel(qts, mmio + R100_AIC_CNTL,
                     R100_PCIGART_TRANSLATE_EN |
                     R100_DIS_OUT_OF_PCI_GART_ACCESS);

        /* Transparent monochrome HOST_DATA must not bypass GART decoding. */
        qtest_memset(qts, GART_PAGE0 + 0xffc, 0x11, 4);
        qtest_memset(qts, GART_PAGE1, 0x11, 4);
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((4096 / 64) << 22) |
                     ((GART_BASE + 0xc00) >> 10));
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 1020);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (1U << 16) | 1028U);
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_DP_SRC_FRGD_CLR, 0xee);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_8BPP |
                     ATI_GMC_SRC_MONO_FG_LA | ATI_GMC_BYTE_LSB_TO_MSB |
                     ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST);
        qtest_writel(qts, mmio + ATI_DST_X, 1020);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 8);
        qtest_writel(qts, mmio + ATI_HOST_DATA_LAST, 0x55);
        qtest_memread(qts, GART_PAGE0 + 0xffc, mono_actual, 4);
        qtest_memread(qts, GART_PAGE1, mono_actual + 4, 4);
        g_assert_cmpmem(mono_actual, sizeof(mono_actual), mono_expected,
                        sizeof(mono_expected));

        qtest_memset(qts, GART_PAGE0 + 0xffc, 0x11, 4);
        qtest_memset(qts, GART_PAGE1, 0x11, 4);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 8);
        qtest_writel(qts, mmio + ATI_HOST_DATA_LAST, 0x0f);
        qtest_memread(qts, GART_PAGE0 + 0xffc, mono_actual, 4);
        qtest_memread(qts, GART_PAGE1, mono_actual + 4, 4);
        for (unsigned int i = 0; i < 4; i++) {
            g_assert_cmphex(mono_actual[i], ==, 0xee);
            g_assert_cmphex(mono_actual[i + 4], ==, 0x11);
        }

        qtest_memset(qts, GART_PAGE0 + 0xffc, 0x11, 4);
        qtest_writel(qts, PAGE_TABLE + 4, INVALID_PAGE);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 8);
        qtest_writel(qts, mmio + ATI_HOST_DATA_LAST, UINT32_MAX);
        qtest_memread(qts, GART_PAGE0 + 0xffc, mono_actual, 4);
        for (unsigned int i = 0; i < 4; i++) {
            g_assert_cmphex(mono_actual[i], ==, 0x11);
        }

        /* Transparent background pixels do not access an invalid PTE. */
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 8);
        qtest_writel(qts, mmio + ATI_HOST_DATA_LAST, 0x01);
        qtest_memread(qts, GART_PAGE0 + 0xffc, mono_actual, 4);
        g_assert_cmphex(mono_actual[0], ==, 0xee);
        for (unsigned int i = 1; i < 4; i++) {
            g_assert_cmphex(mono_actual[i], ==, 0x11);
        }
        qtest_writel(qts, PAGE_TABLE + 4, GART_PAGE1);

        /* HOST_DATA uses the same path and resumes across migration. */
        qtest_memset(qts, GART_PAGE0 + 0xff0, 0, 16);
        qtest_memset(qts, GART_PAGE1, 0, 16);
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((4096 / 64) << 22) |
                     ((GART_BASE + 0xc00) >> 10));
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 252);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (1U << 16) | (252 + ARRAY_SIZE(host)));
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                     ATI_GMC_DP_SRC_HOST);
        qtest_writel(qts, mmio + ATI_DST_X, 252);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 1);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, ARRAY_SIZE(host));
        for (unsigned int i = 0; i < 6; i++) {
            qtest_writel(qts, mmio + ATI_HOST_DATA0 + (i % 4) * 4,
                         host[i]);
        }

        if (model_index == 0) {
            char path[] = "/tmp/ati-2d-gart-migration-XXXXXX";
            g_autofree char *uri = NULL;
            g_autofree char *incoming_args = NULL;
            int fd = g_mkstemp(path);

            g_assert_cmpint(fd, >=, 0);
            close(fd);
            uri = g_strdup_printf("file:%s", path);
            qtest_qmp_assert_success(
                qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
            display_wait_for_migration(qts);
            qtest_quit(qts);
            incoming_args = g_strdup_printf(
                "-machine ia64-vpc,nvram=none -m 256M -S "
                "-vga ati -global ati-vga.model=%s -incoming defer",
                models[model_index]);
            qts = qtest_init(incoming_args);
            qtest_qmp_assert_success(
                qts, "{'execute':'migrate-incoming','arguments':"
                     "{'uri':%s,'exit-on-error':false}}", uri);
            display_wait_for_migration(qts);
            g_assert_cmpint(g_unlink(path), ==, 0);
        }

        qtest_writel(qts, mmio + ATI_HOST_DATA0 + 2 * 4, host[6]);
        qtest_writel(qts, mmio + ATI_HOST_DATA_LAST, host[7]);
        qtest_memread(qts, GART_PAGE0 + 0xff0, actual, 16);
        qtest_memread(qts, GART_PAGE1, actual + 16, 16);
        g_assert_cmpmem(actual, sizeof(host_le), host_le, sizeof(host_le));
        qtest_memread(qts, CONTIGUOUS_DECOY, decoy, sizeof(decoy));
        for (unsigned int i = 0; i < ARRAY_SIZE(decoy); i++) {
            g_assert_cmphex(decoy[i], ==, 0x5a);
        }

        /* Multi-bank HOST_DATA advances through a pitched GART surface. */
        for (unsigned int row = 0; row < HOST_ROWS; row++) {
            uint32_t page = HOST_GART_PAGE_BASE + row * 4096;

            qtest_writel(qts, PAGE_TABLE + row * sizeof(uint32_t), page);
            qtest_memset(qts, page, 0, HOST_WIDTH * sizeof(uint32_t));
        }
        qtest_writel(qts, mmio + R100_AIC_HI_ADDR,
                     GART_BASE + HOST_ROWS * 4096 - 1);
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((4096 / 64) << 22) | (GART_BASE >> 10));
        qtest_writel(qts, mmio + ATI_SC_TOP_LEFT, 0);
        qtest_writel(qts, mmio + ATI_SC_BOTTOM_RIGHT,
                     (HOST_ROWS << 16) | HOST_WIDTH);
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_NONE | ATI_GMC_DST_32BPP |
                     ATI_GMC_SRC_COLOR | ATI_GMC_ROP3_SRCCOPY |
                     ATI_GMC_DP_SRC_HOST);
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, HOST_ROWS);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, HOST_WIDTH);
        for (unsigned int i = 0; i < HOST_ROWS * HOST_WIDTH; i++) {
            uint32_t value = 0x60000000 | i;
            uint64_t reg = i + 1 == HOST_ROWS * HOST_WIDTH ?
                           ATI_HOST_DATA_LAST :
                           ATI_HOST_DATA0 + (i % 4) * sizeof(uint32_t);

            qtest_writel(qts, mmio + reg, value);
        }
        for (unsigned int row = 0; row < HOST_ROWS; row++) {
            for (unsigned int col = 0; col < HOST_WIDTH; col++) {
                uint32_t expected = 0x60000000 | (row * HOST_WIDTH + col);

                g_assert_cmphex(qtest_readl(
                    qts, HOST_GART_PAGE_BASE + row * 4096 +
                         col * sizeof(uint32_t)), ==, expected);
            }
        }
        qtest_quit(qts);
    }
}

static void display_wait_for_u32(QTestState *qts, uint64_t address,
                                 uint32_t expected)
{
    int64_t deadline = g_get_monotonic_time() + 60 * G_TIME_SPAN_SECOND;
    uint32_t actual;

    do {
        actual = qtest_readl(qts, address);
        if (actual == expected) {
            return;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_assert_cmphex(actual, ==, expected);
}

static void ati_radeon_2d_live_migration(void)
{
    enum {
        GPU_FB_BASE = 0x10000000,
        GART_BASE = 0x00100000,
        VRAM_SOURCE = 0x00004000,
        DIRECT_DESTINATION = 0x00006000,
        STAGED_DESTINATION = 0x00009000,
        PAGE_TABLE = 0x08000000,
        GART_PAGE = 0x08001000,
        GART_SOURCE_OFFSET = 0x100,
    };
    static const char * const models[] = { "rv100", "es1000" };
    const uint32_t direct_value = 0x13579bdf;
    const uint32_t staged_value = 0x2468ace0;
    const uint32_t direct_old = 0xaaaaaaaa;
    const uint32_t staged_old = 0x55555555;

    for (unsigned int i = 0; i < ARRAY_SIZE(models); i++) {
        g_autoptr(GError) error = NULL;
        g_autofree char *tmpdir = NULL;
        g_autofree char *socket_path = NULL;
        g_autofree char *uri = NULL;
        QTestState *from;
        QTestState *to;
        uint64_t mmio = IA64_RV100_MMIO_BASE;

        tmpdir = g_dir_make_tmp("ati-2d-live-migration-XXXXXX", &error);
        g_assert_no_error(error);
        g_assert_nonnull(tmpdir);
        socket_path = g_build_filename(tmpdir, "migration.sock", NULL);
        uri = g_strdup_printf("unix:%s", socket_path);

        from = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                           "-vga ati -global ati-vga.model=%s", models[i]);
        to = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                         "-vga ati -global ati-vga.model=%s "
                         "-incoming defer", models[i]);
        ati_pci_enable(from);
        ati_pci_enable(to);

        qtest_writel(from, IA64_RV100_FB_BASE + VRAM_SOURCE, direct_value);
        qtest_writel(from, IA64_RV100_FB_BASE + DIRECT_DESTINATION,
                     direct_old);
        qtest_writel(from, IA64_RV100_FB_BASE + STAGED_DESTINATION,
                     staged_old);
        qtest_writel(from, PAGE_TABLE, GART_PAGE);
        qtest_writel(from, GART_PAGE + GART_SOURCE_OFFSET, staged_value);
        qtest_writel(from, mmio + R100_MC_FB_LOCATION,
                     (0x10ffU << 16) | 0x1000U);
        qtest_writel(from, mmio + R100_AIC_PT_BASE, PAGE_TABLE);
        qtest_writel(from, mmio + R100_AIC_LO_ADDR, GART_BASE);
        qtest_writel(from, mmio + R100_AIC_HI_ADDR, GART_BASE + 0xfff);
        qtest_writel(from, mmio + R100_AIC_CNTL,
                     R100_PCIGART_TRANSLATE_EN |
                     R100_DIS_OUT_OF_PCI_GART_ACCESS);

        qtest_qmp_assert_success(
            from, "{'execute':'migrate-set-capabilities','arguments':"
                  " {'capabilities':[{'capability':"
                  " 'pause-before-switchover','state':true}]}}");
        qtest_qmp_assert_success(
            to, "{'execute':'migrate-incoming','arguments':"
                " {'uri':%s,'exit-on-error':false}}", uri);
        qtest_qmp_assert_success(from, "{'execute':'cont'}");
        qtest_qmp_assert_success(
            from, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
        display_wait_for_migration_status(from, "pre-switchover");

        /* Prove both destination pages completed an initial precopy pass. */
        display_wait_for_u32(to, IA64_RV100_FB_BASE + DIRECT_DESTINATION,
                             direct_old);
        display_wait_for_u32(to, IA64_RV100_FB_BASE + STAGED_DESTINATION,
                             staged_old);

        /* Exercise both the direct VRAM and staged GART-source paths. */
        ati_radeon_2d_copy(from, GPU_FB_BASE + VRAM_SOURCE,
                           GPU_FB_BASE + DIRECT_DESTINATION, 1);
        ati_radeon_2d_copy(from, GART_BASE + GART_SOURCE_OFFSET,
                           GPU_FB_BASE + STAGED_DESTINATION, 1);
        g_assert_cmphex(qtest_readl(from, IA64_RV100_FB_BASE +
                                         DIRECT_DESTINATION), ==,
                        direct_value);
        g_assert_cmphex(qtest_readl(from, IA64_RV100_FB_BASE +
                                         STAGED_DESTINATION), ==,
                        staged_value);

        qtest_qmp_assert_success(
            from, "{'execute':'migrate-continue','arguments':"
                  " {'state':'pre-switchover'}}");
        display_wait_for_migration(from);
        display_wait_for_migration(to);
        g_assert_cmphex(qtest_readl(to, IA64_RV100_FB_BASE +
                                       DIRECT_DESTINATION), ==,
                        direct_value);
        g_assert_cmphex(qtest_readl(to, IA64_RV100_FB_BASE +
                                       STAGED_DESTINATION), ==,
                        staged_value);

        qtest_quit(from);
        qtest_quit(to);
        if (g_file_test(socket_path, G_FILE_TEST_EXISTS)) {
            g_assert_cmpint(g_unlink(socket_path), ==, 0);
        }
        g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
    }
}

static void ati_rv100_3d_live_migration(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        GPU_FB_BASE = 0x10000000,
        COLOR_OFFSET = 0x00004000,
        DEPTH_OFFSET = 0x00008000,
        TILED_COLOR_OFFSET = 0x0000c000,
        PARTIAL_DEPTH_OFFSET = 0x00010000,
        SPARSE_COLOR_OFFSET = 0x00020000,
        TILED_PIXEL_OFFSET = 272,
        SPARSE_PITCH = 2048,
        SPARSE_POINTS = 40,
        GART_BASE = 0x00100000,
        GART_COLOR_OFFSET = 0x100,
        PAGE_TABLE = 0x08000000,
        GART_PAGE = 0x08001000,
        SAMPLE_X = 6,
        SAMPLE_Y = 6,
        TILED_X = 4,
        TILED_Y = 2,
        GART_X = 8,
        GART_Y = 8,
        PARTIAL_X = 12,
        PARTIAL_Y = 12,
        SPARSE_X = 32,
    };
    const uint32_t color = 0xff13579b;
    const uint32_t tiled_color = 0xff2468ac;
    const uint32_t gart_color = 0xff55aa33;
    const uint32_t old_color = 0x11111111;
    const uint32_t old_depth = UINT32_MAX;
    const uint32_t old_tiled = 0x22222222;
    const uint32_t old_gart = 0x33333333;
    const uint32_t old_sparse_first = 0x44444444;
    const uint32_t old_sparse_last = 0x55555555;
    const uint32_t sparse_first = 0xff600000;
    const uint32_t sparse_last = 0xff600000 | (SPARSE_POINTS - 1);
    const uint32_t depth = 0xff3fffff;
    const uint32_t rectangle[] = {
        f32_bits(4.0f), f32_bits(24.0f), f32_bits(0.25f),
        ati_rv100_rgba_vertex_color(color),
        f32_bits(24.0f), f32_bits(24.0f), f32_bits(0.25f),
        ati_rv100_rgba_vertex_color(color),
        f32_bits(24.0f), f32_bits(4.0f), f32_bits(0.25f),
        ati_rv100_rgba_vertex_color(color),
    };
    const uint32_t tiled_point[] = {
        f32_bits(TILED_X), f32_bits(TILED_Y),
        ati_rv100_rgba_vertex_color(tiled_color),
    };
    const uint32_t gart_point[] = {
        f32_bits(GART_X), f32_bits(GART_Y),
        ati_rv100_rgba_vertex_color(gart_color),
    };
    const uint32_t partial_point[] = {
        f32_bits(PARTIAL_X), f32_bits(PARTIAL_Y), f32_bits(0.25f),
        ati_rv100_rgba_vertex_color(color),
    };
    uint32_t sparse_points[SPARSE_POINTS * 3];
    const uint64_t color_address = IA64_RV100_FB_BASE + COLOR_OFFSET +
        (SAMPLE_Y * WIDTH + SAMPLE_X) * sizeof(uint32_t);
    const uint64_t depth_address = IA64_RV100_FB_BASE + DEPTH_OFFSET +
        (SAMPLE_Y * WIDTH + SAMPLE_X) * sizeof(uint32_t);
    const uint64_t tiled_address = IA64_RV100_FB_BASE +
        TILED_COLOR_OFFSET + TILED_PIXEL_OFFSET;
    const uint64_t gart_address = GART_PAGE + GART_COLOR_OFFSET +
        (GART_Y * WIDTH + GART_X) * sizeof(uint32_t);
    const uint64_t partial_depth_address = IA64_RV100_FB_BASE +
        PARTIAL_DEPTH_OFFSET +
        (PARTIAL_Y * WIDTH + PARTIAL_X) * sizeof(uint32_t);
    const uint64_t sparse_first_address = IA64_RV100_FB_BASE +
        SPARSE_COLOR_OFFSET + SPARSE_X * sizeof(uint32_t);
    const uint64_t sparse_last_address = IA64_RV100_FB_BASE +
        SPARSE_COLOR_OFFSET +
        ((SPARSE_POINTS - 1) * SPARSE_PITCH + SPARSE_X) * sizeof(uint32_t);
    g_autoptr(GError) error = NULL;
    g_autofree char *tmpdir = NULL;
    g_autofree char *socket_path = NULL;
    g_autofree char *uri = NULL;
    QTestState *from;
    QTestState *to;
    uint64_t mmio = IA64_RV100_MMIO_BASE;

    for (unsigned int i = 0; i < SPARSE_POINTS; i++) {
        uint32_t sparse_color = sparse_first | i;

        sparse_points[i * 3] = f32_bits(SPARSE_X);
        sparse_points[i * 3 + 1] = f32_bits(i);
        sparse_points[i * 3 + 2] =
            ati_rv100_rgba_vertex_color(sparse_color);
    }

    tmpdir = g_dir_make_tmp("ati-rv100-3d-live-migration-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    socket_path = g_build_filename(tmpdir, "migration.sock", NULL);
    uri = g_strdup_printf("unix:%s", socket_path);

    from = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                      "-vga ati -global ati-vga.model=rv100");
    to = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                    "-vga ati -global ati-vga.model=rv100 "
                    "-incoming defer");
    ati_pci_enable(from);
    ati_pci_enable(to);

    qtest_writel(from, color_address, old_color);
    qtest_writel(from, depth_address, old_depth);
    qtest_writel(from, tiled_address, old_tiled);
    qtest_writel(from, partial_depth_address, old_depth);
    qtest_writel(from, sparse_first_address, old_sparse_first);
    qtest_writel(from, sparse_last_address, old_sparse_last);
    qtest_writel(from, PAGE_TABLE, GART_PAGE);
    qtest_writel(from, gart_address, old_gart);
    qtest_writel(from, mmio + R100_MC_FB_LOCATION,
                 (0x10ffU << 16) | 0x1000U);
    qtest_writel(from, mmio + R100_AIC_PT_BASE, PAGE_TABLE);
    qtest_writel(from, mmio + R100_AIC_LO_ADDR, GART_BASE);
    qtest_writel(from, mmio + R100_AIC_HI_ADDR, GART_BASE + 0xfff);
    qtest_writel(from, mmio + R100_AIC_CNTL,
                 R100_PCIGART_TRANSLATE_EN |
                 R100_DIS_OUT_OF_PCI_GART_ACCESS);
    qtest_writel(from, mmio + R100_RB3D_COLOROFFSET,
                 GPU_FB_BASE + COLOR_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(from, mmio + R100_RB3D_DEPTHOFFSET,
                 GPU_FB_BASE + DEPTH_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_DEPTHPITCH, WIDTH);
    qtest_writel(from, mmio + R100_RB3D_ZSTENCILCNTL,
                 2U | (7U << 4) | R100_Z_WRITE_ENABLE);
    qtest_writel(from, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(from, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(from, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(from, mmio + R100_PP_CNTL, 0);
    qtest_writel(from, mmio + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_Z_ENABLE);

    qtest_qmp_assert_success(
        from, "{'execute':'migrate-set-capabilities','arguments':"
              " {'capabilities':[{'capability':"
              " 'pause-before-switchover','state':true}]}}");
    qtest_qmp_assert_success(
        to, "{'execute':'migrate-incoming','arguments':"
            " {'uri':%s,'exit-on-error':false}}", uri);
    qtest_qmp_assert_success(from, "{'execute':'cont'}");
    qtest_qmp_assert_success(
        from, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration_status(from, "pre-switchover");

    /* The target must have received the old pages before the late draw. */
    display_wait_for_u32(to, color_address, old_color);
    display_wait_for_u32(to, depth_address, old_depth);
    display_wait_for_u32(to, tiled_address, old_tiled);
    display_wait_for_u32(to, gart_address, old_gart);
    display_wait_for_u32(to, partial_depth_address, old_depth);
    display_wait_for_u32(to, sparse_first_address, old_sparse_first);
    display_wait_for_u32(to, sparse_last_address, old_sparse_last);

    /* One batched draw dirties both linear color and depth VRAM ranges. */
    ati_rv100_draw_immediate(from, R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_RECTANGLE_LIST, rectangle,
                             ARRAY_SIZE(rectangle), 3);
    g_assert_cmphex(qtest_readl(from, color_address), ==, color);
    g_assert_cmphex(qtest_readl(from, depth_address), ==, depth);

    /* Tiled color writes must dirty the swizzled physical VRAM address. */
    qtest_writel(from, mmio + R100_RB3D_COLOROFFSET,
                 GPU_FB_BASE + TILED_COLOR_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_COLORPITCH,
                 WIDTH | R100_COLOR_TILE_ENABLE);
    qtest_writel(from, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    ati_rv100_draw_immediate(from, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, tiled_point,
                             ARRAY_SIZE(tiled_point), 1);
    g_assert_cmphex(qtest_readl(from, tiled_address), ==, tiled_color);

    /* GART targets remain PCI RAM writes and migrate through RAM dirtiness. */
    qtest_writel(from, mmio + R100_RB3D_COLOROFFSET,
                 GART_BASE + GART_COLOR_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_COLORPITCH, WIDTH);
    ati_rv100_draw_immediate(from, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, gart_point,
                             ARRAY_SIZE(gart_point), 1);
    g_assert_cmphex(qtest_readl(from, gart_address), ==, gart_color);

    /* A later color fault must not lose an already batched depth write. */
    qtest_writel(from, mmio + R100_RB3D_COLOROFFSET, GART_BASE + 0x2000);
    qtest_writel(from, mmio + R100_RB3D_DEPTHOFFSET,
                 GPU_FB_BASE + PARTIAL_DEPTH_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_Z_ENABLE);
    ati_rv100_draw_immediate(from, R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, partial_point,
                             ARRAY_SIZE(partial_point), 1);
    g_assert_cmphex(qtest_readl(from, partial_depth_address), ==, depth);

    /*
     * Forty 8 KiB-pitched points occupy separate dirty pages.  This exceeds
     * the eight-range batch: the first page is flushed on capacity and the
     * last page by the final flush at the end of this single POINT_LIST.
     */
    qtest_writel(from, mmio + R100_RB3D_COLOROFFSET,
                 GPU_FB_BASE + SPARSE_COLOR_OFFSET);
    qtest_writel(from, mmio + R100_RB3D_COLORPITCH, SPARSE_PITCH);
    qtest_writel(from, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    ati_rv100_draw_immediate(from, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, sparse_points,
                             ARRAY_SIZE(sparse_points), SPARSE_POINTS);
    g_assert_cmphex(qtest_readl(from, sparse_first_address), ==,
                    sparse_first);
    g_assert_cmphex(qtest_readl(from, sparse_last_address), ==, sparse_last);

    qtest_qmp_assert_success(
        from, "{'execute':'migrate-continue','arguments':"
              " {'state':'pre-switchover'}}");
    display_wait_for_migration(from);
    display_wait_for_migration(to);
    g_assert_cmphex(qtest_readl(to, color_address), ==, color);
    g_assert_cmphex(qtest_readl(to, depth_address), ==, depth);
    g_assert_cmphex(qtest_readl(to, tiled_address), ==, tiled_color);
    g_assert_cmphex(qtest_readl(to, gart_address), ==, gart_color);
    g_assert_cmphex(qtest_readl(to, partial_depth_address), ==, depth);
    g_assert_cmphex(qtest_readl(to, sparse_first_address), ==,
                    sparse_first);
    g_assert_cmphex(qtest_readl(to, sparse_last_address), ==, sparse_last);

    qtest_quit(from);
    qtest_quit(to);
    if (g_file_test(socket_path, G_FILE_TEST_EXISTS)) {
        g_assert_cmpint(g_unlink(socket_path), ==, 0);
    }
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void ati_rv100_fixed_function(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        I8_OFFSET = 0x10000,
        Y8_OFFSET = 0x10100,
        AI88_OFFSET = 0x10180,
        TEX0_OFFSET = 0x10200,
        TEX1_OFFSET = 0x10300,
        MICRO_OFFSET = 0x10400,
        FILTER_OFFSET = 0x10500,
        MACRO_TEXTURE_OFFSET = 0x10600,
        MIRROR_TEXTURE_OFFSET = 0x10700,
        POT_TEXTURE_OFFSET = 0x10800,
        POT_MICRO_TEXTURE_OFFSET = 0x10900,
        DEPTH_OFFSET = 0x20000,
        TILED_COLOR_OFFSET = 0x24000,
    };
    const uint32_t replace_texture_color =
        (10U << 10) | R100_COMBINER_CLAMP;
    const uint32_t replace_texture_alpha =
        (5U << 8) | R100_COMBINER_CLAMP;
    const uint32_t rectangle[] = {
        f32_bits(4.0f), f32_bits(24.0f), UINT32_MAX,
        f32_bits(24.0f), f32_bits(24.0f), UINT32_MAX,
        f32_bits(24.0f), f32_bits(4.0f), UINT32_MAX,
    };
    const uint32_t additive_rectangle[] = {
        f32_bits(4.0f), f32_bits(24.0f), 0x10101010,
        f32_bits(24.0f), f32_bits(24.0f), 0x10101010,
        f32_bits(24.0f), f32_bits(4.0f), 0x10101010,
    };
    const uint32_t additive_triangles[] = {
        f32_bits(32.0f), f32_bits(24.0f), 0x10101010,
        f32_bits(52.0f), f32_bits(4.0f), 0x10101010,
        f32_bits(52.0f), f32_bits(24.0f), 0x10101010,
        f32_bits(32.0f), f32_bits(24.0f), 0x10101010,
        f32_bits(32.0f), f32_bits(4.0f), 0x10101010,
        f32_bits(52.0f), f32_bits(4.0f), 0x10101010,
    };
    const uint32_t perspective_i8[] = {
        f32_bits(30.0f), f32_bits(2.0f), UINT32_MAX,
        f32_bits(2.0f), f32_bits(0.0f), f32_bits(2.0f),
    };
    const uint32_t y8[] = {
        f32_bits(32.0f), f32_bits(2.0f), UINT32_MAX,
        f32_bits(0.0f), f32_bits(0.0f),
    };
    const uint32_t ai88[] = {
        f32_bits(34.0f), f32_bits(2.0f), UINT32_MAX,
        f32_bits(0.0f), f32_bits(0.0f),
    };
    const uint32_t stencil_pass[] = {
        f32_bits(40.0f), f32_bits(2.0f), f32_bits(0.25f), 0xff0000ff,
    };
    const uint32_t stencil_fail[] = {
        f32_bits(40.0f), f32_bits(2.0f), f32_bits(0.10f), 0xffff0000,
    };
    const uint32_t viewport_z[] = {
        f32_bits(42.0f), f32_bits(6.0f), f32_bits(0.5f),
        f32_bits(2.0f), UINT32_MAX,
    };
    const uint32_t multitexture[] = {
        f32_bits(50.0f), f32_bits(2.0f), UINT32_MAX,
        f32_bits(0.0f), f32_bits(0.0f),
        f32_bits(0.0f), f32_bits(0.0f),
    };
    const uint32_t microtiled[] = {
        f32_bits(60.0f), f32_bits(2.0f), UINT32_MAX,
        f32_bits(4.0f), f32_bits(0.0f),
    };
    const uint32_t clipped_point[] = {
        f32_bits(5.0f), f32_bits(31.0f), UINT32_MAX,
    };
    const uint32_t tiled_target[] = {
        f32_bits(4.0f), f32_bits(2.0f), 0xff0000ff,
    };
    const uint32_t filtered[] = {
        f32_bits(58.0f), f32_bits(10.0f), UINT32_MAX,
        f32_bits(1.0f), f32_bits(1.0f),
    };
    const uint32_t macro_texture[] = {
        f32_bits(56.0f), f32_bits(12.0f), UINT32_MAX,
        f32_bits(4.0f), f32_bits(2.0f),
    };
    const uint32_t pot_pitch[] = {
        f32_bits(54.0f), f32_bits(18.0f), UINT32_MAX,
        f32_bits(0.0f), f32_bits(1.0f),
    };
    const uint32_t pot_micro[] = {
        f32_bits(52.0f), f32_bits(18.0f), UINT32_MAX,
        f32_bits(0.0f), f32_bits(2.0f),
    };
    static const struct {
        uint32_t filter;
        float s;
        uint32_t expected;
    } nearest_address[] = {
        { R100_TX_CLAMP_S(0), 5.25f, 0xffff0000 },
        { R100_TX_CLAMP_S(0), -1.25f, 0xff00ff00 },
        { R100_TX_CLAMP_S(1), 5.25f, 0xff00ff00 },
        { R100_TX_CLAMP_S(1), -1.25f, 0xffff0000 },
        { R100_TX_CLAMP_S(1), 5.0f, 0xff0000ff },
        { R100_TX_CLAMP_S(1), -1.0f, 0xffff0000 },
        { R100_TX_CLAMP_S(2), 5.25f, 0xff0000ff },
        { R100_TX_CLAMP_S(2), -1.25f, 0xff000000 },
        { R100_TX_CLAMP_S(3), 5.25f, 0xff0000ff },
        { R100_TX_CLAMP_S(3), -1.25f, 0xffff0000 },
        { R100_TX_CLAMP_S(4), 5.25f, 0xffff00ff },
        { R100_TX_CLAMP_S(4), -1.25f, 0xff000000 },
        { R100_TX_CLAMP_S(5), 5.25f, 0xffff00ff },
        { R100_TX_CLAMP_S(5), -1.25f, 0xffff0000 },
        { R100_TX_CLAMP_S(6), 5.25f, 0xff0000ff },
        { R100_TX_CLAMP_S(6), -1.25f, 0xff000000 },
        { R100_TX_CLAMP_S(7), 5.25f, 0xff0000ff },
        { R100_TX_CLAMP_S(7), -1.25f, 0xffff0000 },
        { R100_TX_CLAMP_S(6) | R100_TX_BORDER_D3D,
          5.25f, 0xffff00ff },
        { R100_TX_CLAMP_S(6) | R100_TX_BORDER_D3D,
          -1.25f, 0xffff00ff },
        { R100_TX_CLAMP_S(7) | R100_TX_BORDER_D3D,
          5.25f, 0xffff00ff },
        { R100_TX_CLAMP_S(7) | R100_TX_BORDER_D3D,
          -1.25f, 0xffff0000 },
    };
    static const struct {
        uint32_t filter;
        float s;
        uint32_t expected;
    } linear_address[] = {
        { R100_TX_LINEAR | R100_TX_CLAMP_S(0),
          0.0f, 0xff000080 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(1),
          0.0f, 0xff000000 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(2),
          0.0f, 0xff000000 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(3),
          0.0f, 0xff000000 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(4),
          0.0f, 0xff800080 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(5),
          0.0f, 0xff000000 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(6),
          0.0f, 0xff800080 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(7),
          0.0f, 0xff000000 },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(4),
          5.0f, 0xff8000ff },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(5),
          5.0f, 0xff8000ff },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(6),
          5.0f, 0xff8000ff },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(7),
          5.0f, 0xff8000ff },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(6) |
          R100_TX_BORDER_D3D, 5.0f, 0xffff00ff },
        { R100_TX_LINEAR | R100_TX_CLAMP_S(7) |
          R100_TX_BORDER_D3D, 5.0f, 0xffff00ff },
    };
    const uint32_t invalid_point[] = {
        0x7fc00000, f32_bits(40.0f), UINT32_MAX,
    };
    const uint32_t packed_n1_points[] = {
        f32_bits(10.0f), f32_bits(2.0f), UINT32_MAX, 0,
        f32_bits(12.0f), f32_bits(2.0f), UINT32_MAX, 0,
    };
    const uint32_t invalid_line[] = {
        f32_bits(0.0f), f32_bits(40.0f), UINT32_MAX,
        0x7f800000, f32_bits(40.0f), UINT32_MAX,
    };
    const uint32_t invalid_triangle[] = {
        f32_bits(1.0e30f), f32_bits(40.0f), UINT32_MAX,
        f32_bits(1.0e30f), f32_bits(41.0f), UINT32_MAX,
        f32_bits(1.0e30f), f32_bits(42.0f), UINT32_MAX,
    };
    const uint32_t clipped_line[] = {
        f32_bits(0.0f), f32_bits(32.0f), UINT32_MAX,
        f32_bits(20.0f), f32_bits(32.0f), UINT32_MAX,
    };
    const uint32_t unsupported_depth[] = {
        f32_bits(42.0f), f32_bits(18.0f), f32_bits(0.5f), UINT32_MAX,
    };
    const uint32_t flat_shade_line[] = {
        f32_bits(40.0f), f32_bits(20.0f), 0xff0000ff,
        f32_bits(44.0f), f32_bits(20.0f), 0xffff0000,
    };
    const uint32_t flat_shade_triangle[] = {
        f32_bits(40.0f), f32_bits(34.0f), 0xff0000ff,
        f32_bits(44.0f), f32_bits(34.0f), 0xff00ff00,
        f32_bits(40.0f), f32_bits(38.0f), 0xffff0000,
    };
    const uint32_t flat_shade_polygon[] = {
        f32_bits(48.0f), f32_bits(34.0f), 0xff0000ff,
        f32_bits(52.0f), f32_bits(34.0f), 0xff00ff00,
        f32_bits(52.0f), f32_bits(38.0f), 0xffff0000,
        f32_bits(48.0f), f32_bits(38.0f), 0xff00ffff,
    };
    const uint32_t driver_style_rectangle[] = {
        f32_bits(40.0f), f32_bits(26.0f),
        f32_bits(0.0f), f32_bits(1.0f),
        f32_bits(44.0f), f32_bits(26.0f),
        f32_bits(1.0f), f32_bits(1.0f),
        f32_bits(44.0f), f32_bits(22.0f),
        f32_bits(1.0f), f32_bits(0.0f),
    };
    const uint32_t flat_triangle_list[] = {
        f32_bits(40.0f), f32_bits(28.0f), 0xff0000ff,
        f32_bits(44.0f), f32_bits(28.0f), 0xff0000ff,
        f32_bits(40.0f), f32_bits(32.0f), 0xff0000ff,
        f32_bits(46.0f), f32_bits(28.0f), 0xff00ff00,
        f32_bits(50.0f), f32_bits(28.0f), 0xff00ff00,
        f32_bits(46.0f), f32_bits(32.0f), 0xff00ff00,
    };
    const uint32_t zero_gradient_triangle[] = {
        f32_bits(52.0f), f32_bits(28.0f),
        f32_bits(0.0f), f32_bits(0.0f),
        f32_bits(56.0f), f32_bits(28.0f),
        f32_bits(0.0f), f32_bits(0.0f),
        f32_bits(52.0f), f32_bits(32.0f),
        f32_bits(0.0f), f32_bits(0.0f),
    };
    const uint64_t color_base = IA64_RV100_FB_BASE;
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    const uint64_t stencil_color = color_base + (2 * WIDTH + 40) * 4;
    const uint64_t stencil_depth = color_base + DEPTH_OFFSET +
                                   (2 * WIDTH + 40) * 4;
    uint32_t texture_point[5];
    QTestState *qts;
    unsigned int x;
    unsigned int y;
    size_t i;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_memset(qts, color_base, 0, 0x30000);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);

    /* A rectangle list supplies three corners; hardware infers the fourth. */
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_RECTANGLE_LIST, rectangle,
                             ARRAY_SIZE(rectangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (6 * WIDTH + 6) * 4), ==, UINT32_MAX);

    /* Shared polygon edges have single coverage, including R100 RECT_LIST. */
    qtest_memset(qts, color_base, 0, WIDTH * HEIGHT * 4);
    qtest_writel(qts, mmio + R100_RB3D_BLENDCNTL,
                 R100_SRC_BLEND_ONE | R100_DST_BLEND_ONE);
    qtest_writel(qts, mmio + R100_RB3D_CNTL,
                 R100_RB_ALPHA_BLEND | R100_RB_COLOR_ARGB8888);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_RECTANGLE_LIST, additive_rectangle,
                             ARRAY_SIZE(additive_rectangle), 3);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_TRIANGLE_LIST, additive_triangles,
                             ARRAY_SIZE(additive_triangles), 6);
    for (y = 4; y < 24; y++) {
        for (x = 4; x < 24; x++) {
            g_assert_cmphex(qtest_readl(qts, color_base +
                                            (y * WIDTH + x) * 4), ==,
                            0x10101010);
            g_assert_cmphex(qtest_readl(qts, color_base +
                                            (y * WIDTH + x + 28) * 4), ==,
                            0x10101010);
        }
    }
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (24 * WIDTH + 4) * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (4 * WIDTH + 24) * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (4 * WIDTH + 52) * 4), ==, 0);
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);

    /* N1 is one packed dword; it must not shift the following vertex. */
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR | R100_VTX_FMT_N1,
                             R100_VF_POINT_LIST, packed_n1_points,
                             ARRAY_SIZE(packed_n1_points), 2);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 10) * 4), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 12) * 4), ==, UINT32_MAX);

    /* I8 carries alpha; Q0 perspective-divides non-parametric S. */
    qtest_writeb(qts, color_base + I8_OFFSET, 0x20);
    qtest_writeb(qts, color_base + I8_OFFSET + 1, 0x80);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, 0);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_I8 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2 | R100_TX_PERSPECTIVE);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, I8_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, 1);
    qtest_writel(qts, mmio + R100_PP_TEX_PITCH_0, 0);
    qtest_writel(qts, mmio + R100_PP_TXCBLEND_0,
                 replace_texture_color);
    qtest_writel(qts, mmio + R100_PP_TXABLEND_0,
                 replace_texture_alpha);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0 | R100_VTX_FMT_Q0,
                             R100_VF_POINT_LIST, perspective_i8,
                             ARRAY_SIZE(perspective_i8), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 30) * 4), ==, 0x80808080);

    /* Y8 is an opaque luminance texture, distinct from intensity I8. */
    qtest_writeb(qts, color_base + Y8_OFFSET, 0x44);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_Y8 | R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, Y8_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, 0);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, y8,
                             ARRAY_SIZE(y8), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 32) * 4), ==, 0xff444444);

    /* AI88 only exposes its stored alpha when ALPHA_IN_MAP is set. */
    qtest_writew(qts, color_base + AI88_OFFSET, 0x4080);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_AI88 | R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, AI88_OFFSET);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, ai88,
                             ARRAY_SIZE(ai88), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 34) * 4), ==, 0xff808080);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_AI88 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, ai88,
                             ARRAY_SIZE(ai88), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 34) * 4), ==, 0x40808080);

    /* D24S8: a passing fragment writes depth and increments stencil. */
    qtest_writel(qts, stencil_depth, 0x7f800000);
    qtest_writel(qts, mmio + R100_PP_CNTL, 0);
    qtest_writel(qts, mmio + R100_RB3D_DEPTHOFFSET, DEPTH_OFFSET);
    qtest_writel(qts, mmio + R100_RB3D_DEPTHPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RB3D_STENCILREFMASK,
                 (0xffU << 16) | (0xffU << 24));
    qtest_writel(qts, mmio + R100_RB3D_ZSTENCILCNTL,
                 2U | (1U << 4) | (7U << 12) | (3U << 20) |
                 R100_Z_WRITE_ENABLE);
    qtest_writel(qts, mmio + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_Z_ENABLE |
                 R100_RB_STENCIL_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, stencil_pass,
                             ARRAY_SIZE(stencil_pass), 1);
    g_assert_cmphex(qtest_readl(qts, stencil_depth), ==, 0x803fffff);
    g_assert_cmphex(qtest_readl(qts, stencil_color), ==, 0xffff0000);

    /* A stencil failure applies REPLACE but preserves depth and color. */
    qtest_writel(qts, mmio + R100_RB3D_STENCILREFMASK,
                 0x12U | (0xffU << 16) | (0xffU << 24));
    qtest_writel(qts, mmio + R100_RB3D_ZSTENCILCNTL,
                 2U | (1U << 4) | (3U << 12) | (2U << 16) |
                 R100_Z_WRITE_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, stencil_fail,
                             ARRAY_SIZE(stencil_fail), 1);
    g_assert_cmphex(qtest_readl(qts, stencil_depth), ==, 0x123fffff);
    g_assert_cmphex(qtest_readl(qts, stencil_color), ==, 0xffff0000);

    /* Viewport Z transformation divides clip-space Z by W before scaling. */
    qtest_writel(qts, color_base + DEPTH_OFFSET +
                 (6 * WIDTH + 42) * 4, UINT32_MAX);
    qtest_writel(qts, mmio + R100_SE_VPORT_ZSCALE, f32_bits(1.0f));
    qtest_writel(qts, mmio + R100_SE_VPORT_ZOFFSET, f32_bits(0.0f));
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10) |
                 R100_VPORT_Z_XFORM);
    qtest_writel(qts, mmio + R100_RB3D_ZSTENCILCNTL,
                 2U | (7U << 4) | R100_Z_WRITE_ENABLE);
    qtest_writel(qts, mmio + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_Z_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_W0 | R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, viewport_z,
                             ARRAY_SIZE(viewport_z), 1);
    g_assert_cmphex(qtest_readl(qts, color_base + DEPTH_OFFSET +
                                (6 * WIDTH + 42) * 4), ==, 0xff3fffff);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));

    /* Stage 0 replaces with T0; stage 1 adds routed ST1/T1. */
    qtest_writel(qts, color_base + TEX0_OFFSET, 0xffff0000);
    qtest_writel(qts, color_base + TEX1_OFFSET, 0xff0000ff);
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM | R100_COORD_ST1_NONPARAM);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, TEX0_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, 0);
    qtest_writel(qts, mmio + R100_PP_TXCBLEND_0,
                 replace_texture_color);
    qtest_writel(qts, mmio + R100_PP_TXABLEND_0,
                 replace_texture_alpha);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_1, 0);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_1,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2 | R100_TX_ROUTE_ST1);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_1, TEX1_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_1, 0);
    qtest_writel(qts, mmio + R100_PP_TEX_PITCH_1, 0);
    qtest_writel(qts, mmio + R100_PP_TXCBLEND_1,
                 2U | (1U << 16) | (12U << 10) |
                 R100_COMBINER_CLAMP);
    qtest_writel(qts, mmio + R100_PP_TXABLEND_1,
                 (1U << 8) | R100_COMBINER_CLAMP);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX1_ENABLE |
                 R100_TX_BLEND0_ENABLE | R100_TX_BLEND1_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0 | R100_VTX_FMT_ST1,
                             R100_VF_POINT_LIST, multitexture,
                             ARRAY_SIZE(multitexture), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 50) * 4), ==, 0xffff00ff);

    /* An 8x2 ARGB texture maps texel x=4 to the second 32-byte microtile. */
    qtest_writel(qts, color_base + MICRO_OFFSET + 16, 0xffff0000);
    qtest_writel(qts, color_base + MICRO_OFFSET + 32, 0xff00ff00);
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0,
                 MICRO_OFFSET | R100_TX_MICRO_X2);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, (1U << 16) | 7U);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, microtiled,
                             ARRAY_SIZE(microtiled), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (2 * WIDTH + 60) * 4), ==, 0xff00ff00);

    /* RB3D macro tiling swizzles (4, 2) away from its linear address. */
    qtest_writel(qts, color_base + TILED_COLOR_OFFSET + 528, 0x11223344);
    qtest_writel(qts, mmio + R100_PP_CNTL, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, TILED_COLOR_OFFSET);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH,
                 WIDTH | R100_COLOR_TILE_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, tiled_target,
                             ARRAY_SIZE(tiled_target), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                TILED_COLOR_OFFSET + 272), ==, 0xffff0000);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                TILED_COLOR_OFFSET + 528), ==, 0x11223344);

    /* Linear filtering at the four-texel center averages all four colors. */
    qtest_writel(qts, color_base + FILTER_OFFSET, 0xffff0000);
    qtest_writel(qts, color_base + FILTER_OFFSET + 4, 0xff00ff00);
    qtest_writel(qts, color_base + FILTER_OFFSET + 32, 0xff0000ff);
    qtest_writel(qts, color_base + FILTER_OFFSET + 36, UINT32_MAX);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, R100_TX_LINEAR);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, FILTER_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, (1U << 16) | 1U);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, filtered,
                             ARRAY_SIZE(filtered), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (10 * WIDTH + 58) * 4), ==, 0xff808080);

    /* A macro-tiled texture uses its swizzled, not linear, texel address. */
    qtest_writel(qts, color_base + MACRO_TEXTURE_OFFSET + 272, 0xff00ff00);
    qtest_writel(qts, color_base + MACRO_TEXTURE_OFFSET + 528, 0xffff0000);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, 0);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 R100_TX_NON_POWER2);
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0,
                 MACRO_TEXTURE_OFFSET | R100_TX_MACRO_TILE);
    qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, (7U << 16) | 63U);
    qtest_writel(qts, mmio + R100_PP_TEX_PITCH_0, 256 - 32);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, macro_texture,
                             ARRAY_SIZE(macro_texture), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (12 * WIDTH + 56) * 4), ==, 0xff00ff00);

    /* Linear POT texture rows retain the R100 32-byte pitch alignment. */
    qtest_writel(qts, color_base + POT_TEXTURE_OFFSET + 16, 0xffff0000);
    qtest_writel(qts, color_base + POT_TEXTURE_OFFSET + 32, 0xff00ff00);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 (2U << 8) | (1U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, POT_TEXTURE_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, 0);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, pot_pitch,
                             ARRAY_SIZE(pot_pitch), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 54) * 4), ==, 0xff00ff00);

    /* POT microtiled rows retain the same 32-byte pitch alignment. */
    qtest_writel(qts, color_base + POT_MICRO_TEXTURE_OFFSET + 8,
                 0xffff0000);
    qtest_writel(qts, color_base + POT_MICRO_TEXTURE_OFFSET + 64,
                 0xff00ff00);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP | (2U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0,
                 POT_MICRO_TEXTURE_OFFSET | R100_TX_MICRO_X2);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, pot_micro,
                             ARRAY_SIZE(pot_micro), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 52) * 4), ==, 0xff00ff00);

    /* Exercise all R100 address modes with a power-of-two texture. */
    qtest_writel(qts, color_base + MIRROR_TEXTURE_OFFSET, 0xff000000);
    qtest_writel(qts, color_base + MIRROR_TEXTURE_OFFSET + 4, 0xffff0000);
    qtest_writel(qts, color_base + MIRROR_TEXTURE_OFFSET + 8, 0xff00ff00);
    qtest_writel(qts, color_base + MIRROR_TEXTURE_OFFSET + 12, 0xff0000ff);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP | (2U << 8));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, MIRROR_TEXTURE_OFFSET);
    qtest_writel(qts, mmio + R100_PP_BORDER_COLOR_0, 0xffff00ff);
    for (i = 0; i < ARRAY_SIZE(nearest_address); i++) {
        texture_point[0] = f32_bits(2.0f + i);
        texture_point[1] = f32_bits(14.0f);
        texture_point[2] = UINT32_MAX;
        texture_point[3] = f32_bits(nearest_address[i].s);
        texture_point[4] = f32_bits(0.0f);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     nearest_address[i].filter);
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                                 R100_VTX_FMT_ST0,
                                 R100_VF_POINT_LIST, texture_point,
                                 ARRAY_SIZE(texture_point), 1);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                    sizeof(uint32_t) *
                                    (14 * WIDTH + 2 + i)), ==,
                        nearest_address[i].expected);
    }

    /* Normalized GL clamp coordinates select the last texel at s == 1. */
    qtest_writel(qts, mmio + R100_SE_COORD_FMT, 0);
    for (i = 0; i < 4; i++) {
        texture_point[0] = f32_bits(24.0f + i);
        texture_point[1] = f32_bits(14.0f);
        texture_point[2] = UINT32_MAX;
        texture_point[3] = f32_bits(1.0f);
        texture_point[4] = f32_bits(0.0f);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     R100_TX_CLAMP_S(6 + i % 2) |
                     (i >= 2 ? R100_TX_BORDER_D3D : 0));
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                                 R100_VTX_FMT_ST0,
                                 R100_VF_POINT_LIST, texture_point,
                                 ARRAY_SIZE(texture_point), 1);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                    sizeof(uint32_t) *
                                    (14 * WIDTH + 24 + i)), ==,
                        0xff0000ff);
    }
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM);

    for (i = 0; i < ARRAY_SIZE(linear_address); i++) {
        texture_point[0] = f32_bits(2.0f + i);
        texture_point[1] = f32_bits(16.0f);
        texture_point[2] = UINT32_MAX;
        texture_point[3] = f32_bits(linear_address[i].s);
        texture_point[4] = f32_bits(0.0f);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     linear_address[i].filter);
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                                 R100_VTX_FMT_ST0,
                                 R100_VF_POINT_LIST, texture_point,
                                 ARRAY_SIZE(texture_point), 1);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                    sizeof(uint32_t) *
                                    (16 * WIDTH + 2 + i)), ==,
                        linear_address[i].expected);
    }

    /* Border colors are always supplied as ARGB8888. */
    qtest_writel(qts, mmio + R100_PP_BORDER_COLOR_0, 0x80402010);
    texture_point[0] = f32_bits(2.0f);
    texture_point[1] = f32_bits(18.0f);
    texture_point[2] = UINT32_MAX;
    texture_point[3] = f32_bits(-1.25f);
    texture_point[4] = f32_bits(0.0f);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_CLAMP_S(6) | R100_TX_BORDER_D3D);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, texture_point,
                             ARRAY_SIZE(texture_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 2) * 4), ==, 0x80402010);

    /* A one-texel T axis still returns border outside its coordinate range. */
    texture_point[0] = f32_bits(3.0f);
    texture_point[3] = f32_bits(0.0f);
    texture_point[4] = f32_bits(-1.25f);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_CLAMP_T(6) | R100_TX_BORDER_D3D);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, texture_point,
                             ARRAY_SIZE(texture_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 3) * 4), ==, 0x80402010);

    /* MIN and MAG remain independent; point sampling uses the MAG filter. */
    qtest_writel(qts, color_base + (18 * WIDTH + 40) * 4, 0);
    texture_point[0] = f32_bits(40.0f);
    texture_point[1] = f32_bits(18.0f);
    texture_point[2] = UINT32_MAX;
    texture_point[3] = f32_bits(0.0f);
    texture_point[4] = f32_bits(0.0f);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, R100_TX_MAG_LINEAR);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, texture_point,
                             ARRAY_SIZE(texture_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 40) * 4), ==, 0xff000080);

    qtest_writel(qts, color_base + (18 * WIDTH + 41) * 4, 0);
    texture_point[0] = f32_bits(41.0f);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, R100_TX_MIN_LINEAR);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, texture_point,
                             ARRAY_SIZE(texture_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 41) * 4), ==, 0xff000000);

    /* A point has no texture gradients, so mipmap MIN still uses MAG. */
    qtest_writel(qts, color_base + (18 * WIDTH + 39) * 4, 0);
    texture_point[0] = f32_bits(39.0f);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_MIN_MIP_NEAREST);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR |
                             R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, texture_point,
                             ARRAY_SIZE(texture_point), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 39) * 4), ==, 0xff000000);

    /* Zero-gradient triangles also use MAG regardless of their MIN mode. */
    qtest_writel(qts, color_base + (29 * WIDTH + 53) * 4, 0x11223344);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, R100_TX_MAG_LINEAR);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                             R100_VF_TRIANGLE_LIST,
                             zero_gradient_triangle,
                             ARRAY_SIZE(zero_gradient_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (29 * WIDTH + 53) * 4), ==, 0xff000080);
    qtest_writel(qts, color_base + (29 * WIDTH + 53) * 4, 0x22334455);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_MIN_MIP_NEAREST);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                             R100_VF_TRIANGLE_LIST,
                             zero_gradient_triangle,
                             ARRAY_SIZE(zero_gradient_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (29 * WIDTH + 53) * 4), ==, 0xff000000);

    qtest_writel(qts, color_base + (18 * WIDTH + 42) * 4, 0x22334455);
    qtest_writel(qts, color_base + DEPTH_OFFSET +
                 (18 * WIDTH + 42) * 4, 0x33445566);
    qtest_writel(qts, mmio + R100_PP_CNTL, 0);
    qtest_writel(qts, mmio + R100_RB3D_DEPTHOFFSET, DEPTH_OFFSET);
    qtest_writel(qts, mmio + R100_RB3D_DEPTHPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RB3D_ZSTENCILCNTL,
                 3U | (7U << 4) | R100_Z_WRITE_ENABLE);
    qtest_writel(qts, mmio + R100_RB3D_CNTL,
                 R100_RB_COLOR_ARGB8888 | R100_RB_Z_ENABLE);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_Z |
                             R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, unsupported_depth,
                             ARRAY_SIZE(unsupported_depth), 1);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (18 * WIDTH + 42) * 4), ==, 0x22334455);
    g_assert_cmphex(qtest_readl(qts, color_base + DEPTH_OFFSET +
                                (18 * WIDTH + 42) * 4), ==, 0x33445566);

    /* Flat shading selects one provoking vertex instead of rejecting it. */
    qtest_writel(qts, color_base + (20 * WIDTH + 42) * 4, 0);
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (1U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_LINE_LIST, flat_shade_line,
                             ARRAY_SIZE(flat_shade_line), 2);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (20 * WIDTH + 42) * 4), ==, 0xffff0000);
    qtest_writel(qts, color_base + (20 * WIDTH + 42) * 4, 0);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (3U << 6) |
                 (1U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_LINE_LIST, flat_shade_line,
                             ARRAY_SIZE(flat_shade_line), 2);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (20 * WIDTH + 42) * 4), ==, 0xff0000ff);

    /* VTX_1 and VTX_2 are distinct selectors for polygonal primitives. */
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (1U << 6) |
                 (1U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_TRIANGLE_LIST, flat_shade_triangle,
                             ARRAY_SIZE(flat_shade_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (35 * WIDTH + 41) * 4), ==, 0xff00ff00);
    qtest_writel(qts, color_base + (35 * WIDTH + 41) * 4, 0);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 6) |
                 (1U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_TRIANGLE_LIST, flat_shade_triangle,
                             ARRAY_SIZE(flat_shade_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (35 * WIDTH + 41) * 4), ==, 0xff0000ff);

    /* A polygon is one primitive, so LAST stays fixed across its fan. */
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (3U << 6) |
                 (1U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POLYGON, flat_shade_polygon,
                             ARRAY_SIZE(flat_shade_polygon), 4);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (35 * WIDTH + 51) * 4), ==, 0xffffff00);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (37 * WIDTH + 49) * 4), ==, 0xffffff00);

    /* Reserved mode 3 with varying diffuse color remains unsupported. */
    qtest_writel(qts, color_base + (20 * WIDTH + 42) * 4, 0x44556677);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (3U << 8) | (2U << 10));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_LINE_LIST, flat_shade_line,
                             ARRAY_SIZE(flat_shade_line), 2);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (20 * WIDTH + 42) * 4), ==, 0x44556677);

    /* X.Org rectangles omit vertex colors and leave alpha shading solid. */
    qtest_writel(qts, color_base + (24 * WIDTH + 42) * 4, 0);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                             R100_VF_RECTANGLE_LIST,
                             driver_style_rectangle,
                             ARRAY_SIZE(driver_style_rectangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (24 * WIDTH + 42) * 4), ==, UINT32_MAX);

    /* Flat batches may use a different constant color per primitive. */
    qtest_writel(qts, color_base + (29 * WIDTH + 41) * 4, 0);
    qtest_writel(qts, color_base + (29 * WIDTH + 47) * 4, 0);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (1U << 8));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_TRIANGLE_LIST, flat_triangle_list,
                             ARRAY_SIZE(flat_triangle_list), 6);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (29 * WIDTH + 41) * 4), ==, 0xffff0000);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (29 * WIDTH + 47) * 4), ==, 0xff00ff00);
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));

    /* Point and line paths obey the same render-engine scissor as triangles. */
    qtest_writel(qts, mmio + R100_PP_CNTL, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, (30U << 16) | 10U);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT, (4U << 16) | 4U);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, clipped_point,
                             ARRAY_SIZE(clipped_point), 1);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_LINE_LIST, clipped_line,
                             ARRAY_SIZE(clipped_line), 2);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (31 * WIDTH + 5) * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (32 * WIDTH + 9) * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (32 * WIDTH + 12) * 4), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (32 * WIDTH + 15) * 4), ==, 0);

    /* Malformed guest floats are rejected before any float-to-int cast. */
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_POINT_LIST, invalid_point,
                             ARRAY_SIZE(invalid_point), 1);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_LINE_LIST, invalid_line,
                             ARRAY_SIZE(invalid_line), 2);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_PKCOLOR,
                             R100_VF_TRIANGLE_LIST, invalid_triangle,
                             ARRAY_SIZE(invalid_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (40 * WIDTH) * 4), ==, 0);

    qtest_quit(qts);
}

static void ati_rv100_mipmap_triangle(uint32_t triangle[12], float x, float y,
                                      float gradient)
{
    float origin = 0.5f - gradient * 0.5f;

    triangle[0] = f32_bits(x);
    triangle[1] = f32_bits(y);
    triangle[2] = f32_bits(origin);
    triangle[3] = f32_bits(origin);
    triangle[4] = f32_bits(x + 4.0f);
    triangle[5] = f32_bits(y);
    triangle[6] = f32_bits(origin + 4.0f * gradient);
    triangle[7] = f32_bits(origin);
    triangle[8] = f32_bits(x);
    triangle[9] = f32_bits(y + 4.0f);
    triangle[10] = f32_bits(origin);
    triangle[11] = f32_bits(origin + 4.0f * gradient);
}

static void ati_rv100_texture_mipmaps(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        PATTERN_OFFSET = 0x10000,
        SOLID_OFFSET = 0x10200,
        LEVEL1_OFFSET = 4 * 32,
        LEVEL2_OFFSET = LEVEL1_OFFSET + 2 * 32,
    };
    static const struct {
        uint32_t filter;
        float gradient;
        uint32_t expected;
    } filter_cases[] = {
        { R100_TX_MAX_MIP_LEVEL(2), 0.5f, 0xffff0000 },
        { R100_TX_MIN_LINEAR | R100_TX_MAX_MIP_LEVEL(2),
          0.5f, 0xff400000 },
        { R100_TX_MIN_NEAREST_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(2),
          0.5f, 0xffffffff },
        { R100_TX_MIN_NEAREST_MIP_LINEAR | R100_TX_MAX_MIP_LEVEL(2),
          0.5f, 0xff808080 },
        { R100_TX_MIN_LINEAR_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(2),
          0.4204482f, 0xffffbfbf },
        { R100_TX_MIN_LINEAR_MIP_LINEAR | R100_TX_MAX_MIP_LEVEL(2),
          0.4204482f, 0xff706060 },
        { R100_TX_MAG_LINEAR | R100_TX_MIN_NEAREST_MIP_NEAREST |
          R100_TX_MAX_MIP_LEVEL(2), 0.2973018f, 0xff400000 },
    };
    static const struct {
        uint32_t filter;
        float gradient;
        uint32_t expected;
    } lod_cases[] = {
        { R100_TX_MIN_NEAREST_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(2) |
          R100_TX_LOD_BIAS(0x20), 0.25f, 0xff0000ff },
        { R100_TX_MIN_NEAREST_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(2) |
          R100_TX_LOD_BIAS(0x80), 0.5f, 0xffff0000 },
        { R100_TX_MIN_NEAREST_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(2),
          1.0f, 0xff00ff00 },
        { R100_TX_MIN_NEAREST_MIP_NEAREST | R100_TX_MAX_MIP_LEVEL(1),
          1.0f, 0xff0000ff },
    };
    const uint32_t replace_texture_color =
        (10U << 10) | R100_COMBINER_CLAMP;
    const uint32_t replace_texture_alpha =
        (5U << 8) | R100_COMBINER_CLAMP;
    const uint64_t color_base = IA64_RV100_FB_BASE;
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    uint32_t triangle[12];
    const uint32_t perspective_triangle[] = {
        f32_bits(40.0f), f32_bits(16.0f),
        f32_bits(0.375f), f32_bits(0.375f), f32_bits(1.0f),
        f32_bits(44.0f), f32_bits(16.0f),
        f32_bits(0.375f), f32_bits(0.375f), f32_bits(-1.0f),
        f32_bits(40.0f), f32_bits(20.0f),
        f32_bits(0.375f), f32_bits(0.375f), f32_bits(1.0f),
    };
    QTestState *qts;
    unsigned int x;
    unsigned int y;
    size_t i;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_memset(qts, color_base, 0, 0x11000);

    /* A 4x4, 2x2, 1x1 POT chain uses a 32-byte pitch at every level. */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            qtest_writel(qts, color_base + PATTERN_OFFSET + y * 32 + x * 4,
                         0xff000000);
        }
    }
    qtest_writel(qts, color_base + PATTERN_OFFSET + 2 * 32 + 2 * 4,
                 0xffff0000);
    qtest_writel(qts, color_base + PATTERN_OFFSET + LEVEL1_OFFSET,
                 0xffff0000);
    qtest_writel(qts, color_base + PATTERN_OFFSET + LEVEL1_OFFSET + 4,
                 0xff00ff00);
    qtest_writel(qts, color_base + PATTERN_OFFSET + LEVEL1_OFFSET + 32,
                 0xff0000ff);
    qtest_writel(qts, color_base + PATTERN_OFFSET + LEVEL1_OFFSET + 36,
                 0xffffffff);
    qtest_writel(qts, color_base + PATTERN_OFFSET + LEVEL2_OFFSET,
                 0xff000000);

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            qtest_writel(qts, color_base + SOLID_OFFSET + y * 32 + x * 4,
                         0xffff0000);
        }
    }
    for (y = 0; y < 2; y++) {
        for (x = 0; x < 2; x++) {
            qtest_writel(qts, color_base + SOLID_OFFSET + LEVEL1_OFFSET +
                         y * 32 + x * 4, 0xff0000ff);
        }
    }
    qtest_writel(qts, color_base + SOLID_OFFSET + LEVEL2_OFFSET,
                 0xff00ff00);

    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, mmio + R100_SE_COORD_FMT, 0);
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 (2U << 8) | (2U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, PATTERN_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TXCBLEND_0, replace_texture_color);
    qtest_writel(qts, mmio + R100_PP_TXABLEND_0, replace_texture_alpha);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);

    /* The register names list the mip kernel before the texel kernel. */
    for (i = 0; i < ARRAY_SIZE(filter_cases); i++) {
        x = 4 + i * 8;
        ati_rv100_mipmap_triangle(triangle, x, 4.0f,
                                  filter_cases[i].gradient);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     filter_cases[i].filter);
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                                 R100_VF_TRIANGLE_LIST, triangle,
                                 ARRAY_SIZE(triangle), 3);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                    (4 * WIDTH + x) * 4), ==,
                        filter_cases[i].expected);
    }

    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, SOLID_OFFSET);
    for (i = 0; i < ARRAY_SIZE(lod_cases); i++) {
        x = 4 + i * 8;
        ati_rv100_mipmap_triangle(triangle, x, 16.0f,
                                  lod_cases[i].gradient);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0, lod_cases[i].filter);
        ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                                 R100_VF_TRIANGLE_LIST, triangle,
                                 ARRAY_SIZE(triangle), 3);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                    (16 * WIDTH + x) * 4), ==,
                        lod_cases[i].expected);
    }

    /* Perspective LOD accounts for Q's gradient, not just affine S and T. */
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_ARGB8888 | R100_TX_ALPHA_IN_MAP |
                 (2U << 8) | (2U << 12) | R100_TX_PERSPECTIVE);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_MIN_NEAREST_MIP_NEAREST |
                 R100_TX_MAX_MIP_LEVEL(2));
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0 | R100_VTX_FMT_Q0,
                             R100_VF_TRIANGLE_LIST, perspective_triangle,
                             ARRAY_SIZE(perspective_triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                (16 * WIDTH + 40) * 4), ==, 0xff00ff00);
    qtest_quit(qts);
}

static void ati_rv100_texture_point(QTestState *qts, float x, float y,
                                    float s, float t)
{
    const uint32_t point[] = {
        f32_bits(x), f32_bits(y), f32_bits(s), f32_bits(t),
    };

    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                             R100_VF_POINT_LIST, point,
                             ARRAY_SIZE(point), 1);
}

static void ati_rv100_yuv_textures(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        TEXTURE_OFFSET = 0x10000,
    };
    static const struct {
        uint32_t format;
        uint8_t texels[8];
    } cases[] = {
        {
            R100_TX_VYUY422,
            { 16, 128, 235, 128, 81, 90, 81, 240 },
        }, {
            R100_TX_YVYU422,
            { 128, 16, 128, 235, 90, 81, 240, 81 },
        },
    };
    static const uint32_t expected[] = {
        0xff000000, 0xffffffff, 0xfffe0000, 0xfffe0000,
    };
    const uint32_t replace_texture_color =
        (10U << 10) | R100_COMBINER_CLAMP;
    const uint32_t replace_texture_alpha =
        (5U << 8) | R100_COMBINER_CLAMP;
    const uint64_t color_base = IA64_RV100_FB_BASE;
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    QTestState *qts;
    size_t i;
    size_t j;

    for (i = 0; i < ARRAY_SIZE(cases); i++) {
        qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                         "-vga ati -global ati-vga.model=rv100");
        qtest_memset(qts, color_base, 0, 0x11000);
        qtest_memwrite(qts, color_base + TEXTURE_OFFSET,
                       cases[i].texels, sizeof(cases[i].texels));

        qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
        qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
        qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
        qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                     (HEIGHT - 1) << 16 | (WIDTH - 1));
        qtest_writel(qts, mmio + R100_SE_CNTL,
                     (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
        qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                     R100_COORD_ST0_NONPARAM);
        qtest_writel(qts, mmio + R100_RB3D_CNTL,
                     R100_RB_COLOR_ARGB8888);
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     R100_TX_YUV_TO_RGB);
        qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                     cases[i].format | R100_TX_NON_POWER2);
        qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, TEXTURE_OFFSET);
        qtest_writel(qts, mmio + R100_PP_TEX_SIZE_0, 3);
        qtest_writel(qts, mmio + R100_PP_TEX_PITCH_0, 0);
        qtest_writel(qts, mmio + R100_PP_TXCBLEND_0,
                     replace_texture_color);
        qtest_writel(qts, mmio + R100_PP_TXABLEND_0,
                     replace_texture_alpha);
        qtest_writel(qts, mmio + R100_PP_CNTL,
                     R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);

        for (j = 0; j < ARRAY_SIZE(expected); j++) {
            ati_rv100_texture_point(qts, 4.0f + j, 4.0f, j, 0.0f);
            g_assert_cmphex(qtest_readl(
                qts, color_base + (4 * WIDTH + 4 + j) * 4), ==,
                expected[j]);
        }
        qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                     R100_TX_YUV_TO_RGB | R100_TX_LINEAR);
        ati_rv100_texture_point(qts, 10.0f, 4.0f, 1.0f, 0.5f);
        g_assert_cmphex(qtest_readl(qts, color_base +
                                        (4 * WIDTH + 10) * 4), ==,
                        0xff7f7f7f);
        qtest_quit(qts);
    }
}

static void ati_rv100_compressed_textures(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 64,
        DXT1_OFFSET = 0x10000,
        DXT1_ALPHA_OFFSET = 0x10100,
        DXT3_OFFSET = 0x10200,
        DXT5_SEVEN_OFFSET = 0x10300,
        DXT5_FIVE_OFFSET = 0x10400,
        PITCH_OFFSET = 0x10600,
        MIP_OFFSET = 0x10800,
    };
    static const uint8_t dxt1_four[] = {
        0x00, 0xf8, 0xe0, 0x07, 0xe4, 0x00, 0x00, 0x00,
    };
    static const uint32_t dxt1_four_expected[] = {
        0xffff0000, 0xff00ff00, 0xffaa5500, 0xff55aa00,
    };
    static const uint8_t dxt1_alpha[] = {
        0x1f, 0x00, 0xff, 0xff, 0xe4, 0x00, 0x00, 0x00,
    };
    static const uint32_t dxt1_alpha_expected[] = {
        0xff0000ff, 0xffffffff, 0xff8080ff, 0x00000000,
    };
    static const uint8_t dxt3[] = {
        0x50, 0xfa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1f, 0x00, 0x00, 0xf8, 0xe4, 0x00, 0x00, 0x00,
    };
    static const uint32_t dxt3_expected[] = {
        0x000000ff, 0x55ff0000, 0xaa5500aa, 0xffaa0055,
    };
    static const uint8_t dxt5_seven[] = {
        0xff, 0x00, 0x88, 0x0e, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xf8, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00,
    };
    static const uint32_t dxt5_seven_expected[] = {
        0xffff0000, 0x00ff0000, 0xdbff0000, 0x24ff0000,
    };
    static const uint8_t dxt5_five[] = {
        0x00, 0xff, 0xaa, 0x0f, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xf8, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00,
    };
    static const uint32_t dxt5_five_expected[] = {
        0x33ff0000, 0xccff0000, 0x00ff0000, 0xffff0000,
    };
    static const uint8_t solid_red[] = {
        0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static const uint8_t solid_green[] = {
        0xe0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static const struct {
        uint32_t offset;
        uint32_t format;
        const uint8_t *block;
        size_t block_size;
        const uint32_t *expected;
    } cases[] = {
        { DXT1_OFFSET, R100_TX_DXT1, dxt1_four,
          sizeof(dxt1_four), dxt1_four_expected },
        { DXT1_ALPHA_OFFSET, R100_TX_DXT1 | R100_TX_ALPHA_IN_MAP,
          dxt1_alpha, sizeof(dxt1_alpha), dxt1_alpha_expected },
        { DXT3_OFFSET, R100_TX_DXT3 | R100_TX_ALPHA_IN_MAP,
          dxt3, sizeof(dxt3), dxt3_expected },
        { DXT5_SEVEN_OFFSET, R100_TX_DXT5 | R100_TX_ALPHA_IN_MAP,
          dxt5_seven, sizeof(dxt5_seven), dxt5_seven_expected },
        { DXT5_FIVE_OFFSET, R100_TX_DXT5 | R100_TX_ALPHA_IN_MAP,
          dxt5_five, sizeof(dxt5_five), dxt5_five_expected },
    };
    const uint32_t replace_texture_color =
        (10U << 10) | R100_COMBINER_CLAMP;
    const uint32_t replace_texture_alpha =
        (5U << 8) | R100_COMBINER_CLAMP;
    const uint64_t color_base = IA64_RV100_FB_BASE;
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    uint32_t triangle[12];
    QTestState *qts;
    size_t i;
    size_t j;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rv100");
    qtest_memset(qts, color_base, 0, 0x11000);
    qtest_writel(qts, mmio + R100_RB3D_COLOROFFSET, 0);
    qtest_writel(qts, mmio + R100_RB3D_COLORPITCH, WIDTH);
    qtest_writel(qts, mmio + R100_RE_TOP_LEFT, 0);
    qtest_writel(qts, mmio + R100_RE_WIDTH_HEIGHT,
                 (HEIGHT - 1) << 16 | (WIDTH - 1));
    qtest_writel(qts, mmio + R100_SE_CNTL,
                 (3U << 1) | (3U << 3) | (2U << 8) | (2U << 10));
    qtest_writel(qts, mmio + R100_SE_COORD_FMT,
                 R100_COORD_ST0_NONPARAM);
    qtest_writel(qts, mmio + R100_RB3D_CNTL, R100_RB_COLOR_ARGB8888);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, 0);
    qtest_writel(qts, mmio + R100_PP_TXCBLEND_0, replace_texture_color);
    qtest_writel(qts, mmio + R100_PP_TXABLEND_0, replace_texture_alpha);
    qtest_writel(qts, mmio + R100_PP_CNTL,
                 R100_TX0_ENABLE | R100_TX_BLEND0_ENABLE);

    for (i = 0; i < ARRAY_SIZE(cases); i++) {
        qtest_memwrite(qts, color_base + cases[i].offset,
                       cases[i].block, cases[i].block_size);
        qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                     cases[i].format | (2U << 8) | (2U << 12));
        qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, cases[i].offset);
        for (j = 0; j < 4; j++) {
            ati_rv100_texture_point(qts, 4.0f + j, 4.0f + i * 2,
                                    j, 0.0f);
            g_assert_cmphex(qtest_readl(
                qts, color_base + ((4 + i * 2) * WIDTH + 4 + j) * 4),
                ==, cases[i].expected[j]);
        }
    }

    /* RGB DXT1 keeps selector 3 opaque while retaining its black color. */
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_DXT1 | (2U << 8) | (2U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, DXT1_ALPHA_OFFSET);
    ati_rv100_texture_point(qts, 10.0f, 6.0f, 3.0f, 0.0f);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                    (6 * WIDTH + 10) * 4), ==,
                    0xff000000);

    /* Bilinear sampling reuses a decoded block and blends adjacent texels. */
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_DXT1 | (2U << 8) | (2U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, DXT1_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, R100_TX_LINEAR);
    ati_rv100_texture_point(qts, 10.0f, 8.0f, 1.0f, 0.5f);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                    (8 * WIDTH + 10) * 4), ==,
                    0xff808000);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0, 0);

    /* A 32x8 DXT1 image uses a 64-byte block-row pitch, not 32 bytes. */
    qtest_memset(qts, color_base + PITCH_OFFSET, 0, 96);
    qtest_memwrite(qts, color_base + PITCH_OFFSET + 32,
                   solid_red, sizeof(solid_red));
    qtest_memwrite(qts, color_base + PITCH_OFFSET + 64,
                   solid_green, sizeof(solid_green));
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_DXT1 | (5U << 8) | (3U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, PITCH_OFFSET);
    ati_rv100_texture_point(qts, 4.0f, 16.0f, 16.0f, 0.0f);
    ati_rv100_texture_point(qts, 5.0f, 16.0f, 0.0f, 4.0f);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                    (16 * WIDTH + 4) * 4), ==,
                    0xffff0000);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                    (16 * WIDTH + 5) * 4), ==,
                    0xff00ff00);

    /* A 4x4 DXT1 level occupies 32 bytes before its 2x2 mip level. */
    qtest_memset(qts, color_base + MIP_OFFSET, 0, 64);
    qtest_memwrite(qts, color_base + MIP_OFFSET,
                   solid_red, sizeof(solid_red));
    qtest_memwrite(qts, color_base + MIP_OFFSET + 32,
                   solid_green, sizeof(solid_green));
    qtest_writel(qts, mmio + R100_SE_COORD_FMT, 0);
    qtest_writel(qts, mmio + R100_PP_TXFORMAT_0,
                 R100_TX_DXT1 | (2U << 8) | (2U << 12));
    qtest_writel(qts, mmio + R100_PP_TXOFFSET_0, MIP_OFFSET);
    qtest_writel(qts, mmio + R100_PP_TXFILTER_0,
                 R100_TX_MIN_NEAREST_MIP_NEAREST |
                 R100_TX_MAX_MIP_LEVEL(1));
    ati_rv100_mipmap_triangle(triangle, 16.0f, 16.0f, 0.5f);
    ati_rv100_draw_immediate(qts, R100_VTX_FMT_ST0,
                             R100_VF_TRIANGLE_LIST, triangle,
                             ARRAY_SIZE(triangle), 3);
    g_assert_cmphex(qtest_readl(qts, color_base +
                                    (16 * WIDTH + 16) * 4), ==,
                    0xff00ff00);
    qtest_quit(qts);
}

static void ati_stride(void)
{
    const unsigned width = 640;
    const unsigned height = 480;
    const unsigned bpp = 32;
    const unsigned virtual_width = 704;
    const uint64_t pitch = virtual_width * (bpp / 8);
    const uint32_t marker = 0x00123456;
    const uint32_t padding_decoy = 0x00654321;
    QTestState *qts;
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p");
    /*
     * Program the ATI CRTC, not the generic VBE ports.  ati_vga_switch_mode()
     * must translate the Rage128 pitch (eight-pixel units) into the VBE
     * virtual width used by the common scanout path.
     */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_H_TOTAL_DISP,
                 ((width / 8) - 1) << 16);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP,
                 (height - 1) << 16);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_OFFSET, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_PITCH,
                 virtual_width / 8);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_32);

    g_assert_cmphex(ati_vbe_read(qts, VBE_DISPI_INDEX_ENABLE), ==,
                    VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED |
                    VBE_DISPI_NOCLEARMEM);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_XRES), ==, width);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_YRES), ==, height);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_BPP), ==, bpp);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_VIRT_WIDTH), ==,
                     virtual_width);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_CRTC_INDEX),
                 VGA_CRTC_OFFSET);
    g_assert_cmphex(qtest_readb(
        qts, IA64_LEGACY_IO_PORT_PA(VGA_CRTC_DATA)), ==,
        (pitch / 8) & 0xff);

    /* Leave attribute-controller blanking, as a real VBE client does. */
    qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(VGA_INPUT_STATUS1));
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_ATTR_INDEX), 0x20);

    qtest_writel(qts, IA64_ATI_FB_BASE, marker);
    qtest_writel(qts, IA64_ATI_FB_BASE + width * (bpp / 8), padding_decoy);
    qtest_writel(qts, IA64_ATI_FB_BASE + pitch, marker);

    tmpdir = g_dir_make_tmp("ia64-ati-stride-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "stride.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    assert_ppm_stride(ppm, width, height);

    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_INDEX),
                 VGA_SEQ_RESET);
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_SEQ_DATA), 1);
    g_assert_cmphex(ati_vbe_read(qts, VBE_DISPI_INDEX_ENABLE), ==, 0);
    g_assert_cmpuint(ati_vbe_read(qts, VBE_DISPI_INDEX_VIRT_WIDTH), ==,
                     virtual_width);

    qtest_system_reset(qts);
    ati_pci_enable(qts);
    g_assert_cmphex(ati_vbe_read(qts, VBE_DISPI_INDEX_ENABLE), ==, 0);
    g_assert_cmphex(ati_vbe_read(qts, VBE_DISPI_INDEX_VIRT_WIDTH), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_GEN_INT_STATUS), ==,
                    ATI_RAGE128_GEN_INT_STATUS_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_CRTC_GEN_CNTL), ==,
                    ATI_CRTC_GEN_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_CRTC_EXT_CNTL), ==,
                    ATI_RAGE128_CRTC_EXT_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_DAC_CNTL), ==,
                    ATI_RAGE128_DAC_CNTL_RESET);
    g_assert_cmphex(qtest_readl(qts, IA64_ATI_MMIO_BASE +
                                    ATI_CRTC_OFFSET_CNTL), ==, 0);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void ati_cursor_prepare_scanout(QTestState *qts, uint64_t fb,
                                       uint64_t mmio, unsigned width,
                                       unsigned height, uint32_t cursor_mode)
{
    qtest_memset(qts, fb, 0, width * height * sizeof(uint32_t));
    qtest_writel(qts, mmio + ATI_CRTC_H_TOTAL_DISP,
                 ((width / 8) - 1) << 16);
    qtest_writel(qts, mmio + ATI_CRTC_V_TOTAL_DISP,
                 (height - 1) << 16);
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET, 0);
    qtest_writel(qts, mmio + ATI_CRTC_PITCH, width / 8);
    qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                 ATI_CRTC_PIX_WIDTH_32 | ATI_CRTC_CUR_EN | cursor_mode);
    qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(VGA_INPUT_STATUS1));
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_ATTR_INDEX), 0x20);
}

static void ati_cursor_screendump(QTestState *qts, const char *filename)
{
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", filename);
}

static void ati_cursor_vnc_read(int fd, uint8_t *data, size_t size)
{
    while (size) {
        GPollFD pollfd = { .fd = fd, .events = G_IO_IN };
        ssize_t received;

        g_assert_cmpint(g_poll(&pollfd, 1, 5000), ==, 1);
        received = recv(fd, data, size, 0);
        g_assert_cmpint(received, >, 0);
        data += received;
        size -= received;
    }
}

static void ati_cursor_assert_vnc_hidden(QTestState *qts)
{
    static const uint32_t encodings[] = {
        0xffffff11U, /* RichCursor */
        0xfffffec6U, /* AlphaCursor */
    };
    uint8_t header[24];
    uint8_t reply;
    g_autofree uint8_t *name = NULL;
    uint32_t name_length;
    int pair[2];
    int fd;

    g_assert_cmpint(qemu_socketpair(PF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    qtest_qmp_add_client(qts, "vnc", pair[1]);
    close(pair[1]);
    fd = pair[0];

    /* RFB 3.8, unauthenticated, shared connection. */
    ati_cursor_vnc_read(fd, header, 12);
    g_assert_cmpmem(header, 12, "RFB 003.008\n", 12);
    g_assert_cmpint(qemu_send_full(fd, header, 12), ==, 12);
    ati_cursor_vnc_read(fd, header, 2);
    g_assert_cmphex(header[0], ==, 1);
    g_assert_cmphex(header[1], ==, 1);
    reply = 1;
    g_assert_cmpint(qemu_send_full(fd, &reply, 1), ==, 1);
    ati_cursor_vnc_read(fd, header, 4);
    g_assert_cmphex(ldl_be_p(header), ==, 0);
    g_assert_cmpint(qemu_send_full(fd, &reply, 1), ==, 1);
    ati_cursor_vnc_read(fd, header, sizeof(header));
    g_assert_cmpuint(header[4], ==, 32);
    name_length = ldl_be_p(header + 20);
    g_assert_cmpuint(name_length, <, 4096);
    name = g_malloc(name_length);
    ati_cursor_vnc_read(fd, name, name_length);

    for (unsigned int i = 0; i < ARRAY_SIZE(encodings); i++) {
        uint8_t request[8] = { 2, 0, 0, 1 };
        g_autofree uint8_t *pixels = NULL;
        g_autofree uint8_t *mask = NULL;
        unsigned int width, height, size;

        /* SetEncodings sends the current cursor without a screen request. */
        stl_be_p(request + 4, encodings[i]);
        g_assert_cmpint(qemu_send_full(fd, request, sizeof(request)), ==,
                        sizeof(request));
        ati_cursor_vnc_read(fd, header, 4);
        g_assert_cmphex(header[0], ==, 0);
        g_assert_cmpuint(lduw_be_p(header + 2), ==, 1);
        ati_cursor_vnc_read(fd, header, 12);
        g_assert_cmphex((uint32_t)ldl_be_p(header + 8), ==, encodings[i]);
        width = lduw_be_p(header + 4);
        height = lduw_be_p(header + 6);
        g_assert_cmpuint(width, >, 0);
        g_assert_cmpuint(width, <=, 64);
        g_assert_cmpuint(height, >, 0);
        g_assert_cmpuint(height, <=, 64);
        if (i) {
            ati_cursor_vnc_read(fd, header, 4);
            g_assert_cmphex(ldl_be_p(header), ==, 0); /* Raw pixels */
        }
        size = width * height * 4;
        pixels = g_malloc(size);
        ati_cursor_vnc_read(fd, pixels, size);
        if (i) {
            for (unsigned int j = 3; j < size; j += 4) {
                g_assert_cmphex(pixels[j], ==, 0);
            }
        } else {
            size = DIV_ROUND_UP(width, 8) * height;
            mask = g_malloc(size);
            ati_cursor_vnc_read(fd, mask, size);
            for (unsigned int j = 0; j < size; j++) {
                g_assert_cmphex(mask[j], ==, 0);
            }
        }
    }
    close(fd);
}

static void ati_radeon_cursor_position(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 48,
        CURSOR_OFFSET = 0x10000,
        PAD_X = 7,
        PAD_Y = 11,
    };
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    const uint32_t control = ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                             ATI_CRTC_PIX_WIDTH_32 | ATI_CRTC_CUR_MODE_24BPP;
    g_autofree char *tmpdir = g_dir_make_tmp("ati-cursor-position-XXXXXX",
                                           NULL);
    g_autofree char *screen = NULL;
    QTestState *qts;

    g_assert_nonnull(tmpdir);
    screen = g_build_filename(tmpdir, "screen.ppm", NULL);
    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-display vnc=none -vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);
    qtest_memset(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET, 0, 64 * 64 * 4);
    qtest_writel(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET +
                      (PAD_Y * 64 + PAD_X) * 4, 0xffff0000);
    ati_cursor_prepare_scanout(qts, IA64_RV100_FB_BASE, mmio,
                               WIDTH, HEIGHT, ATI_CRTC_CUR_MODE_24BPP);
    qtest_writel(qts, mmio + ATI_CUR_HORZ_VERT_OFF, 0);
    qtest_writel(qts, mmio + ATI_CUR_HORZ_VERT_POSN, (17U << 16) | 19U);
    qtest_writel(qts, mmio + ATI_CUR_OFFSET, CURSOR_OFFSET);

    /* Position names the image corner, including its transparent padding. */
    ati_cursor_screendump(qts, screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 17, 19, 0, 0, 0);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 24, 30, 0xff, 0, 0);
    ati_cursor_assert_vnc_hidden(qts);

    qtest_writel(qts, mmio + ATI_CUR_HORZ_VERT_POSN, (29U << 16) | 9U);
    ati_cursor_screendump(qts, screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 24, 30, 0, 0, 0);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 36, 20, 0xff, 0, 0);

    qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL, control);
    ati_cursor_screendump(qts, screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 36, 20, 0, 0, 0);
    qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL, control | ATI_CRTC_CUR_EN);
    ati_cursor_screendump(qts, screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 36, 20, 0xff, 0, 0);

    /* Top clipping advances the address; OFF supplies the remaining height. */
    qtest_writel(qts, mmio + ATI_CUR_HORZ_VERT_OFF, (PAD_X << 16) | PAD_Y);
    qtest_writel(qts, mmio + ATI_CUR_HORZ_VERT_POSN, 0);
    qtest_writel(qts, mmio + ATI_CUR_OFFSET, CURSOR_OFFSET + PAD_Y * 256);
    ati_cursor_screendump(qts, screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, PAD_X, PAD_Y, 0, 0, 0);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 36, 20, 0, 0, 0);

    qtest_system_reset(qts);
    ati_cursor_assert_vnc_hidden(qts);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(screen), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void ati_radeon_hwcursor(void)
{
    static const char * const models[] = { "rv100", "es1000" };
    enum {
        WIDTH = 64,
        HEIGHT = 48,
        CURSOR_OFFSET = 0x10000,
        CROPPED_OFFSET = 0x14000,
        LOCKED_OFFSET = 0x18000,
        CURSOR_BYTES = 64 * 64 * sizeof(uint32_t),
        XORIGIN = 2,
        YORIGIN = 3,
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(models); i++) {
        g_autofree char *migration = g_strdup_printf(
            "%s/ati-%s-hwcursor-migration.XXXXXX",
            g_get_tmp_dir(), models[i]);
        g_autofree char *uri = NULL;
        g_autofree char *tmpdir = NULL;
        g_autofree char *initial = NULL;
        g_autofree char *cropped = NULL;
        g_autofree char *locked = NULL;
        g_autofree char *unlocked = NULL;
        g_autofree char *migrated = NULL;
        g_autoptr(GError) error = NULL;
        QTestState *qts;
        int fd;

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s "
                          "-global ati-vga.guest_hwcursor=on", models[i]);
        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET, 0,
                     CURSOR_BYTES);
        qtest_memset(qts, IA64_RV100_FB_BASE + CROPPED_OFFSET, 0,
                     CURSOR_BYTES);
        qtest_memset(qts, IA64_RV100_FB_BASE + LOCKED_OFFSET, 0,
                     CURSOR_BYTES);
        qtest_writel(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET,
                     0xffff0000);
        qtest_writel(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET + 4,
                     0x80008000);
        qtest_writel(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET + 8,
                     0x000000ff);
        qtest_writel(qts, IA64_RV100_FB_BASE + CROPPED_OFFSET +
                          (YORIGIN * 64 + XORIGIN) * sizeof(uint32_t),
                     0xffff00ff);
        qtest_writel(qts, IA64_RV100_FB_BASE + LOCKED_OFFSET,
                     0xffffff00);
        ati_cursor_prepare_scanout(qts, IA64_RV100_FB_BASE,
                                   IA64_RV100_MMIO_BASE, WIDTH, HEIGHT,
                                   ATI_CRTC_CUR_MODE_24BPP);
        qtest_writel(qts, IA64_RV100_FB_BASE +
                          (5 * WIDTH + 5) * sizeof(uint32_t),
                     0x00ff0000);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN,
                     (4U << 16) | 5U);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_OFFSET,
                     CURSOR_OFFSET);

        tmpdir = g_dir_make_tmp("ia64-ati-hwcursor-XXXXXX", &error);
        g_assert_no_error(error);
        g_assert_nonnull(tmpdir);
        initial = g_build_filename(tmpdir, "initial.ppm", NULL);
        cropped = g_build_filename(tmpdir, "cropped.ppm", NULL);
        locked = g_build_filename(tmpdir, "locked.ppm", NULL);
        unlocked = g_build_filename(tmpdir, "unlocked.ppm", NULL);
        migrated = g_build_filename(tmpdir, "migrated.ppm", NULL);

        ati_cursor_screendump(qts, initial);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 4, 5, 0xff, 0, 0);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 5, 5, 0x7f, 0x80, 0);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 6, 5, 0, 0, 0);

        /* Cursor VRAM changes are visible without rewriting its registers. */
        qtest_writel(qts, IA64_RV100_FB_BASE + CURSOR_OFFSET,
                     0xff0000ff);
        ati_cursor_screendump(qts, initial);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 4, 5, 0, 0, 0xff);

        /* Linux advances OFFSET by whole ARGB rows and crops with OFF. */
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF,
                     (XORIGIN << 16) | YORIGIN);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_OFFSET,
                     CROPPED_OFFSET + YORIGIN * 256);
        ati_cursor_screendump(qts, cropped);
        assert_ppm_pixel(cropped, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0xff);

        /* Locked register writes become visible together when unlocked. */
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_OFFSET,
                     (CROPPED_OFFSET + YORIGIN * 256) |
                     ATI_CRTC_OFFSET_LOCK);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF,
                     ATI_CRTC_OFFSET_LOCK);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN,
                     ATI_CRTC_OFFSET_LOCK | (12U << 16) | 10U);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_OFFSET,
                     ATI_CRTC_OFFSET_LOCK | LOCKED_OFFSET);
        ati_cursor_screendump(qts, locked);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0xff);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 12, 10, 0, 0, 0);

        /* The pre-lock image remains active across migration. */
        fd = g_mkstemp(migration);
        g_assert_cmpint(fd, >=, 0);
        close(fd);
        uri = g_strdup_printf("file:%s", migration);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
        display_wait_for_migration(qts);
        qtest_quit(qts);

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s "
                          "-global ati-vga.guest_hwcursor=on "
                          "-incoming defer", models[i]);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate-incoming','arguments':"
                 "{'uri':%s,'exit-on-error':false}}", uri);
        display_wait_for_migration(qts);
        ati_cursor_screendump(qts, migrated);
        assert_ppm_pixel(migrated, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0xff);
        assert_ppm_pixel(migrated, WIDTH, HEIGHT, 12, 10, 0, 0, 0);

        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CUR_OFFSET,
                     LOCKED_OFFSET);
        ati_cursor_screendump(qts, unlocked);
        assert_ppm_pixel(unlocked, WIDTH, HEIGHT, 0, 0, 0, 0, 0);
        assert_ppm_pixel(unlocked, WIDTH, HEIGHT, 12, 10, 0xff, 0xff, 0);
        qtest_quit(qts);

        g_assert_cmpint(g_unlink(migration), ==, 0);
        g_assert_cmpint(g_unlink(initial), ==, 0);
        g_assert_cmpint(g_unlink(cropped), ==, 0);
        g_assert_cmpint(g_unlink(locked), ==, 0);
        g_assert_cmpint(g_unlink(unlocked), ==, 0);
        g_assert_cmpint(g_unlink(migrated), ==, 0);
        g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
    }
}

static void ati_rage128_mono_hwcursor(void)
{
    enum {
        WIDTH = 64,
        HEIGHT = 48,
        CURSOR_OFFSET = 0x10000,
        CURSOR_BYTES = 64 * 16,
        XORIGIN = 5,
        STAGED_XORIGIN = 9,
        YORIGIN = 3,
        STAGED_X = 7,
        STAGED_Y = 6,
    };
    g_autofree char *tmpdir = NULL;
    g_autofree char *cropped = NULL;
    g_autofree char *locked = NULL;
    g_autofree char *unlocked = NULL;
    g_autoptr(GError) error = NULL;
    uint8_t cursor[CURSOR_BYTES];
    QTestState *qts;

    /* A=1/X=0 is transparent; source[XORIGIN,YORIGIN] selects CUR_CLR1. */
    memset(cursor, 0, sizeof(cursor));
    for (unsigned int y = 0; y < 64; y++) {
        memset(&cursor[y * 16], 0xff, 8);
    }
    cursor[YORIGIN * 16 + XORIGIN / 8] &= ~(0x80U >> (XORIGIN & 7));
    cursor[YORIGIN * 16 + 8 + XORIGIN / 8] |= 0x80U >> (XORIGIN & 7);
    cursor[YORIGIN * 16 + STAGED_XORIGIN / 8] &=
        ~(0x80U >> (STAGED_XORIGIN & 7));
    cursor[YORIGIN * 16 + 8 + STAGED_XORIGIN / 8] |=
        0x80U >> (STAGED_XORIGIN & 7);

    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                     "-vga ati -global ati-vga.model=rage128p "
                     "-global ati-vga.guest_hwcursor=on");
    qtest_memwrite(qts, IA64_ATI_FB_BASE + CURSOR_OFFSET,
                   cursor, sizeof(cursor));
    ati_cursor_prepare_scanout(qts, IA64_ATI_FB_BASE, IA64_ATI_MMIO_BASE,
                               WIDTH, HEIGHT, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_CLR0, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_CLR1, 0x00ff0000);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF,
                 (XORIGIN << 16) | YORIGIN);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN, 0);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_OFFSET,
                 CURSOR_OFFSET + YORIGIN * 16);

    tmpdir = g_dir_make_tmp("ia64-ati-rage128-mono-cursor-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    cropped = g_build_filename(tmpdir, "cropped.ppm", NULL);
    locked = g_build_filename(tmpdir, "locked.ppm", NULL);
    unlocked = g_build_filename(tmpdir, "unlocked.ppm", NULL);
    ati_cursor_screendump(qts, cropped);
    assert_ppm_pixel(cropped, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
    assert_ppm_pixel(cropped, WIDTH, HEIGHT, 1, 0, 0, 0, 0);

    /* OFF/POS stay pending under CUR_LOCK, while cursor colors remain live. */
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_OFFSET,
                 ATI_CRTC_OFFSET_LOCK | CURSOR_OFFSET | YORIGIN * 16);
    qtest_writeb(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF + 2,
                 STAGED_XORIGIN);
    qtest_writeb(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_OFF, YORIGIN);
    qtest_writeb(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN + 2,
                 STAGED_X);
    qtest_writeb(qts, IA64_ATI_MMIO_BASE + ATI_CUR_HORZ_VERT_POSN, STAGED_Y);
    g_assert_cmphex(qtest_readb(qts, IA64_ATI_MMIO_BASE +
                                ATI_CUR_HORZ_VERT_OFF + 2), ==,
                    STAGED_XORIGIN);
    g_assert_cmphex(qtest_readb(qts, IA64_ATI_MMIO_BASE +
                                ATI_CUR_HORZ_VERT_OFF + 3), ==, 0x80);
    g_assert_cmphex(qtest_readb(qts, IA64_ATI_MMIO_BASE +
                                ATI_CUR_HORZ_VERT_POSN + 2), ==, STAGED_X);
    g_assert_cmphex(qtest_readb(qts, IA64_ATI_MMIO_BASE +
                                ATI_CUR_HORZ_VERT_POSN + 3), ==, 0x80);
    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_CLR1, 0x000000ff);
    ati_cursor_screendump(qts, locked);
    assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 0, 0, 0, 0xff);
    assert_ppm_pixel(locked, WIDTH, HEIGHT, STAGED_X, STAGED_Y, 0, 0, 0);

    qtest_writel(qts, IA64_ATI_MMIO_BASE + ATI_CUR_OFFSET,
                 CURSOR_OFFSET + YORIGIN * 16);
    ati_cursor_screendump(qts, unlocked);
    assert_ppm_pixel(unlocked, WIDTH, HEIGHT, 0, 0, 0, 0, 0);
    assert_ppm_pixel(unlocked, WIDTH, HEIGHT, STAGED_X, STAGED_Y,
                     0, 0, 0xff);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(cropped), ==, 0);
    g_assert_cmpint(g_unlink(locked), ==, 0);
    g_assert_cmpint(g_unlink(unlocked), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static QTestState *quadro2_start(const char *options)
{
    const char *firmware = g_getenv("QTEST_IA64_FIRMWARE");

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none -m 2G -smp 1 -S "
                       "-nodefaults -display none -serial none -net none "
                       "-bios %s -vga quadro2 %s", firmware, options);
}

static void quadro2_crtc_write(QTestState *qts, uint8_t index,
                               uint8_t value)
{
    qtest_writeb(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CRTC_INDEX, index);
    qtest_writeb(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CRTC_DATA, value);
}

static uint8_t quadro2_crtc_read(QTestState *qts, uint8_t index)
{
    qtest_writeb(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CRTC_INDEX, index);
    return qtest_readb(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CRTC_DATA);
}

static void quadro2_ddc_lines(QTestState *qts, bool clock, bool data)
{
    quadro2_crtc_write(qts, 0x3f,
                       (1U << 0) | (clock ? (1U << 5) : 0) |
                       (data ? (1U << 4) : 0));
}

static void quadro2_ddc_start(QTestState *qts)
{
    quadro2_ddc_lines(qts, true, true);
    quadro2_ddc_lines(qts, true, false);
    quadro2_ddc_lines(qts, false, false);
}

static void quadro2_ddc_stop(QTestState *qts)
{
    quadro2_ddc_lines(qts, false, false);
    quadro2_ddc_lines(qts, true, false);
    quadro2_ddc_lines(qts, true, true);
}

static bool quadro2_ddc_send(QTestState *qts, uint8_t value)
{
    unsigned int i;

    for (i = 0; i < 8; i++) {
        bool bit = value & (0x80U >> i);

        quadro2_ddc_lines(qts, false, bit);
        quadro2_ddc_lines(qts, true, bit);
        quadro2_ddc_lines(qts, false, bit);
    }
    quadro2_ddc_lines(qts, false, true);
    quadro2_ddc_lines(qts, true, true);
    value = quadro2_crtc_read(qts, 0x3e);
    quadro2_ddc_lines(qts, false, true);
    return !(value & (1U << 3));
}

static uint64_t quadro2_sparse_io_address(uint16_t port)
{
    return HP_QUADRO2_LEGACY_IO_BASE +
           ((uint64_t)(port >> 2) << 12) + (port & 0xfffU);
}

static uint8_t quadro2_sparse_inb(QTestState *qts, uint16_t port)
{
    return qtest_readb(qts, quadro2_sparse_io_address(port));
}

static uint16_t quadro2_sparse_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, quadro2_sparse_io_address(port));
}

static void quadro2_sparse_outb(QTestState *qts, uint16_t port,
                                 uint8_t value)
{
    qtest_writeb(qts, quadro2_sparse_io_address(port), value);
}

static void quadro2_sparse_outw(QTestState *qts, uint16_t port,
                                 uint16_t value)
{
    qtest_writew(qts, quadro2_sparse_io_address(port), value);
}

static void quadro2_prepare_native_scanout(QTestState *qts,
                                            uint32_t scanout_offset)
{
    enum {
        WIDTH = 640,
        HEIGHT = 480,
        VBE_INDEX_PORT = 0x01ce,
        VBE_DATA_PORT = 0x01d0,
    };

    /*
     * Seed the packed-pixel VGA registers through VBE, then leave VBE and
     * select the framebuffer solely through the native NV15 registers.
     */
    quadro2_sparse_outw(qts, VBE_INDEX_PORT, VBE_DISPI_INDEX_XRES);
    quadro2_sparse_outw(qts, VBE_DATA_PORT, WIDTH);
    quadro2_sparse_outw(qts, VBE_INDEX_PORT, VBE_DISPI_INDEX_YRES);
    quadro2_sparse_outw(qts, VBE_DATA_PORT, HEIGHT);
    quadro2_sparse_outw(qts, VBE_INDEX_PORT, VBE_DISPI_INDEX_BPP);
    quadro2_sparse_outw(qts, VBE_DATA_PORT, 32);
    quadro2_sparse_outw(qts, VBE_INDEX_PORT, VBE_DISPI_INDEX_ENABLE);
    quadro2_sparse_outw(qts, VBE_DATA_PORT,
                        VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED |
                        VBE_DISPI_NOCLEARMEM);
    quadro2_sparse_outb(qts, VGA_SEQ_INDEX, VGA_SEQ_RESET);
    quadro2_sparse_outb(qts, VGA_SEQ_DATA, 1);
    quadro2_sparse_outb(qts, VGA_SEQ_DATA, 3);
    quadro2_sparse_outb(qts, 0x03c2, 1);
    quadro2_crtc_write(qts, 0x19, 0x20);
    quadro2_crtc_write(qts, 0x42, 0);
    quadro2_crtc_write(qts, 0x28, 3);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PCRTC_START,
                 scanout_offset);
    quadro2_sparse_inb(qts, 0x03da);
    quadro2_sparse_outb(qts, VGA_ATTR_INDEX, 0x20);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + scanout_offset, 0,
                 WIDTH * HEIGHT * sizeof(uint32_t));
}

static void quadro2_program_cursor_address(QTestState *qts, uint32_t offset,
                                            bool enabled)
{
    quadro2_crtc_write(qts, 0x2f, offset >> 24);
    quadro2_crtc_write(qts, 0x30,
                       0x80 | ((offset >> 17) & 0x7f));
    quadro2_crtc_write(qts, 0x31,
                       (enabled ? 1 : 0) | ((offset >> 9) & 0xfc));
}

static void quadro2_user_write(QTestState *qts, unsigned int channel,
                               unsigned int subchannel, uint32_t method,
                               uint32_t value)
{
    uint64_t address = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_USER +
                       channel * HP_QUADRO2_CHANNEL_SIZE +
                       subchannel * HP_QUADRO2_SUBCHANNEL_SIZE + method;

    qtest_writel(qts, address, value);
}

static uint64_t quadro2_timer_read(QTestState *qts)
{
    uint64_t mmio = HP_QUADRO2_MMIO_BASE;
    uint32_t high;
    uint32_t next_high;
    uint32_t low;

    do {
        high = qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_TIME_1);
        low = qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_TIME_0);
        next_high = qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_TIME_1);
    } while (high != next_high);
    return ((uint64_t)high << 27) | (low >> 5);
}

static void quadro2_timer_write(QTestState *qts, uint64_t value)
{
    uint64_t mmio = HP_QUADRO2_MMIO_BASE;

    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_TIME_1, value >> 27);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_TIME_0,
                 (value << 5) | 0x1f);
}

static void nvidia_quadro2_ptimer_alarm(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/nvidia-quadro2-ptimer-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    const uint64_t low_mask = (UINT64_C(1) << 27) - 1;
    const uint64_t mmio = HP_QUADRO2_MMIO_BASE;
    const uint64_t delta = 128;
    const uint64_t tick_ns = 32;
    const uint64_t double_tick_ns = 16;
    const uint64_t migration_delta = 1024;
    const uint64_t epoch = (UINT64_C(0x12345) << 27) | 0x23456;
    uint64_t current;
    uint64_t remaining;
    uint64_t source_remaining;
    uint64_t target;
    uint64_t step;
    QTestState *qts = quadro2_start("");
    int64_t now;
    int fd;

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    now = qtest_clock_step(qts, 1);
    if (now % tick_ns) {
        qtest_clock_step(qts, tick_ns - now % tick_ns);
    }

    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_NUMERATOR), ==,
                    HP_QUADRO2_PTIMER_DEFAULT_DIV);
    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_DENOMINATOR), ==,
                    HP_QUADRO2_PTIMER_DEFAULT_MUL);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PMC_ENABLE) &
                                HP_QUADRO2_ENABLE_PTIMER, ==,
                    HP_QUADRO2_ENABLE_PTIMER);

    /* TIME_1/TIME_0 set the 56-bit epoch; TIME_0 bits 0-4 are ignored. */
    quadro2_timer_write(qts, epoch);
    g_assert_cmphex(quadro2_timer_read(qts), ==, epoch);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_TIME_0) &
                                0x1f, ==, 0);
    qtest_clock_step(qts, tick_ns - 1);
    g_assert_cmphex(quadro2_timer_read(qts), ==, epoch);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(quadro2_timer_read(qts), ==, epoch + 1);

    current = quadro2_timer_read(qts) & low_mask;
    target = (current + delta) & low_mask;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0,
                 (target << 5) | 0x1f);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0),
                    ==, target << 5);

    qtest_clock_step(qts, delta * tick_ns - 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 1);
    /* A masked event remains latched but does not reach PMC. */
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PMC_INTR_0) &
                                HP_QUADRO2_INTR_PTIMER, ==, 0);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_EN_0, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PMC_INTR_0) &
                                HP_QUADRO2_INTR_PTIMER, ==,
                    HP_QUADRO2_INTR_PTIMER);
    qtest_writel(qts, mmio + HP_QUADRO2_PMC_INTR_EN_0, 1);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_0, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PMC_INTR_0) &
                                HP_QUADRO2_INTR_PTIMER, ==, 0);

    /* A high-word-only epoch update must not retrigger the low comparator. */
    current = quadro2_timer_read(qts);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_TIME_1, current >> 27);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, tick_ns - 1);

    /* The comparator also fires when the low 27-bit counter wraps. */
    current = quadro2_timer_read(qts) & low_mask;
    step = (low_mask - 15 - current) & low_mask;
    qtest_clock_step(qts, step * tick_ns);
    current = quadro2_timer_read(qts) & low_mask;
    g_assert_cmphex(current, ==, low_mask - 15);
    target = 16;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0, target << 5);
    qtest_clock_step(qts, 32 * tick_ns - 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 1);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_0, 1);

    /* CLOCK_MUL doubles the rate; zero freezes the visible counter. */
    current = quadro2_timer_read(qts) & low_mask;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_NUMERATOR,
                 0xabcd0000 | HP_QUADRO2_PTIMER_DEFAULT_DIV);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_DENOMINATOR,
                 0xbeef0000 | HP_QUADRO2_PTIMER_DOUBLE_MUL);
    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_NUMERATOR), ==,
                    HP_QUADRO2_PTIMER_DEFAULT_DIV);
    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_DENOMINATOR), ==,
                    HP_QUADRO2_PTIMER_DOUBLE_MUL);
    g_assert_cmphex(quadro2_timer_read(qts) & low_mask, ==, current);

    target = (current + 64) & low_mask;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0, target << 5);
    qtest_clock_step(qts, 64 * double_tick_ns - 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 1);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_0, 1);

    current = quadro2_timer_read(qts) & low_mask;
    target = (current + 64) & low_mask;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0, target << 5);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_DENOMINATOR, 0);
    current = quadro2_timer_read(qts);
    qtest_clock_step(qts, 10 * double_tick_ns);
    g_assert_cmphex(quadro2_timer_read(qts), ==, current);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_DENOMINATOR,
                 HP_QUADRO2_PTIMER_DOUBLE_MUL);
    g_assert_cmphex(quadro2_timer_read(qts), ==, current);
    qtest_clock_step(qts, 64 * double_tick_ns - 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 1);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_0, 1);

    /* A non-default ratio and pending comparator migrate together. */
    current = quadro2_timer_read(qts) & low_mask;
    target = (current + migration_delta) & low_mask;
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0, target << 5);
    qtest_clock_step(qts, 64);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_ALARM_0),
                    ==, target << 5);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_NUMERATOR), ==,
                    HP_QUADRO2_PTIMER_DEFAULT_DIV);
    g_assert_cmphex(qtest_readl(qts, mmio +
                                     HP_QUADRO2_PTIMER_DENOMINATOR), ==,
                    HP_QUADRO2_PTIMER_DOUBLE_MUL);
    current = quadro2_timer_read(qts) & low_mask;
    remaining = (target - current) & low_mask;
    g_assert_cmpuint(remaining, ==,
                     migration_delta - 64 / double_tick_ns);
    source_remaining = remaining * double_tick_ns;
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, source_remaining - 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PTIMER_INTR_0) & 1,
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PMC_INTR_0) &
                                HP_QUADRO2_INTR_PTIMER, ==,
                    HP_QUADRO2_INTR_PTIMER);
    qtest_writel(qts, mmio + HP_QUADRO2_PTIMER_INTR_0, 1);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static uint32_t quadro2_ramht_hash(uint32_t handle, unsigned int channel)
{
    uint32_t hash = 0;
    uint32_t key = handle;

    while (key) {
        hash ^= key & 0x1ffU;
        key >>= 9;
    }
    return (hash ^ (channel << 5)) & 0x1ffU;
}

static void quadro2_ramht_insert(QTestState *qts, unsigned int channel,
                                 uint32_t handle, uint32_t engine,
                                 uint32_t instance)
{
    const uint32_t ramht_offset = 0x10000;
    uint32_t slot = quadro2_ramht_hash(handle, channel);
    unsigned int i;

    for (i = 0; i < 512; i++, slot = (slot + 1) & 0x1ffU) {
        uint64_t address = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN +
                           ramht_offset + slot * 8;

        if (!(qtest_readl(qts, address + 4) & HP_QUADRO2_RAMHT_VALID)) {
            qtest_writel(qts, address, handle);
            qtest_writel(qts, address + 4,
                         HP_QUADRO2_RAMHT_VALID | engine |
                         (channel << 24) | (instance >> 4));
            return;
        }
    }
    g_assert_not_reached();
}

static void quadro2_prepare_rectangle(QTestState *qts, unsigned int channel,
                                      uint32_t dma_limit,
                                      uint32_t destination_offset,
                                      uint32_t color)
{
    const uint32_t dma_instance = 0x19010;
    const uint32_t surface_instance = 0x19020;
    const uint32_t rectangle_instance = 0x19030;
    const uint32_t dma_handle = 0x80000001;
    const uint32_t surface_handle = 0x80000002;
    const uint32_t rectangle_handle = 0x80000003;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + dma_instance, 0x0000303d);
    qtest_writel(qts, ramin + dma_instance + 4, dma_limit);
    qtest_writel(qts, ramin + dma_instance + 8, 0);
    qtest_writel(qts, ramin + surface_instance, 0x62);
    qtest_writel(qts, ramin + rectangle_instance, 0x4a);
    quadro2_ramht_insert(qts, channel, dma_handle, 0, dma_instance);
    quadro2_ramht_insert(qts, channel, surface_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, surface_instance);
    quadro2_ramht_insert(qts, channel, rectangle_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, rectangle_instance);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);

    quadro2_user_write(qts, channel, 0, 0, surface_handle);
    quadro2_user_write(qts, channel, 0, 0x184, dma_handle);
    quadro2_user_write(qts, channel, 0, 0x188, dma_handle);
    quadro2_user_write(qts, channel, 0, 0x300, 7);
    quadro2_user_write(qts, channel, 0, 0x304, (256U << 16) | 256U);
    quadro2_user_write(qts, channel, 0, 0x308, destination_offset);
    quadro2_user_write(qts, channel, 0, 0x30c, destination_offset);

    quadro2_user_write(qts, channel, 1, 0, rectangle_handle);
    quadro2_user_write(qts, channel, 1, 0x198, surface_handle);
    quadro2_user_write(qts, channel, 1, 0x2fc, 3);
    quadro2_user_write(qts, channel, 1, 0x3fc, color);
    quadro2_user_write(qts, channel, 1, 0x400, 0);
}

static void nvidia_quadro2_hwcursor(void)
{
    enum {
        WIDTH = 640,
        HEIGHT = 480,
        SCANOUT_OFFSET = 0,
        CURSOR_OFFSET = 0x200000,
        CURSOR_64_BYTES = 64 * 64 * sizeof(uint32_t),
        CURSOR_32_BYTES = 32 * 32 * sizeof(uint16_t),
        CURSOR_CONFIG_64_PM = 0x04011100,
        CURSOR_CONFIG_32_1555 = 0x02000100,
    };
    g_autofree char *migration = g_strdup_printf(
        "%s/nvidia-quadro2-hwcursor.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    g_autofree char *tmpdir = NULL;
    g_autofree char *initial = NULL;
    g_autofree char *updated = NULL;
    g_autofree char *negative = NULL;
    g_autofree char *hidden = NULL;
    g_autofree char *legacy = NULL;
    g_autofree char *migrated = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;
    int fd;

    qts = quadro2_start(
        "-global nvidia-quadro2.guest_hwcursor=on");
    quadro2_prepare_native_scanout(qts, SCANOUT_OFFSET);
    qtest_writel(qts, HP_QUADRO2_FB_BASE +
                      (100 * WIDTH + 100) * sizeof(uint32_t),
                 0x00ff0000);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0,
                 CURSOR_64_BYTES);

    /* NV15's normal path is a 64x64 little-endian premultiplied ARGB image. */
    qtest_writel(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0xffff0000);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET + 2 * 4,
                 0x80008000);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET + (64 + 1) * 4,
                 0xffff00ff);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_CONFIG,
                 CURSOR_CONFIG_64_PM);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_POS,
                 (12U << 16) | 10U);
    quadro2_program_cursor_address(qts, CURSOR_OFFSET, true);
    quadro2_sparse_outw(qts, 0x01ce, VBE_DISPI_INDEX_ENABLE);
    g_assert_cmphex(quadro2_sparse_inw(qts, 0x01d0), ==, 0);
    g_assert_cmphex(quadro2_crtc_read(qts, 0x28), ==, 3);
    g_assert_cmphex(quadro2_crtc_read(qts, 0x2f), ==,
                    CURSOR_OFFSET >> 24);
    g_assert_cmphex(quadro2_crtc_read(qts, 0x30), ==,
                    0x80 | ((CURSOR_OFFSET >> 17) & 0x7f));
    g_assert_cmphex(quadro2_crtc_read(qts, 0x31), ==,
                    1 | ((CURSOR_OFFSET >> 9) & 0xfc));
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                     HP_QUADRO2_CURSOR_CONFIG), ==,
                    CURSOR_CONFIG_64_PM);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                     HP_QUADRO2_CURSOR_POS), ==,
                    (12U << 16) | 10U);

    tmpdir = g_dir_make_tmp("ia64-quadro2-hwcursor-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    initial = g_build_filename(tmpdir, "initial.ppm", NULL);
    updated = g_build_filename(tmpdir, "updated.ppm", NULL);
    negative = g_build_filename(tmpdir, "negative.ppm", NULL);
    hidden = g_build_filename(tmpdir, "hidden.ppm", NULL);
    legacy = g_build_filename(tmpdir, "legacy.ppm", NULL);
    migrated = g_build_filename(tmpdir, "migrated.ppm", NULL);

    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", initial);
    assert_ppm_pixel(initial, WIDTH, HEIGHT, 100, 100, 0xff, 0, 0);
    assert_ppm_pixel(initial, WIDTH, HEIGHT, 10, 12, 0xff, 0, 0);
    assert_ppm_pixel(initial, WIDTH, HEIGHT, 11, 12, 0, 0, 0);
    assert_ppm_pixel(initial, WIDTH, HEIGHT, 12, 12, 0, 0x80, 0);

    /* A BAR1-only image update must repaint without touching cursor state. */
    qtest_writel(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0xff0000ff);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", updated);
    assert_ppm_pixel(updated, WIDTH, HEIGHT, 10, 12, 0, 0, 0xff);

    /* Position is signed; (-1, -1) exposes source pixel (1, 1) at (0, 0). */
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_POS,
                 UINT32_MAX);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", negative);
    assert_ppm_pixel(negative, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0xff);

    quadro2_program_cursor_address(qts, CURSOR_OFFSET, false);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", hidden);
    assert_ppm_pixel(hidden, WIDTH, HEIGHT, 0, 0, 0, 0, 0);

    /* The legacy configuration is 32x32 A1R5G5B5 with bit 15 as opacity. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0,
                 CURSOR_32_BYTES);
    qtest_writew(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0xfc00);
    qtest_writew(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET + 2, 0x7fff);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_CONFIG,
                 CURSOR_CONFIG_32_1555);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_POS,
                 (22U << 16) | 20U);
    quadro2_program_cursor_address(qts, CURSOR_OFFSET, true);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", legacy);
    assert_ppm_pixel(legacy, WIDTH, HEIGHT, 20, 22, 0xff, 0, 0);
    assert_ppm_pixel(legacy, WIDTH, HEIGHT, 21, 22, 0, 0, 0);

    /* Leave a distinct 64x64 cursor active across migration. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0,
                 CURSOR_64_BYTES);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + CURSOR_OFFSET, 0xffffff00);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_CONFIG,
                 CURSOR_CONFIG_64_PM);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_CURSOR_POS,
                 (31U << 16) | 30U);
    quadro2_program_cursor_address(qts, CURSOR_OFFSET, true);

    fd = g_mkstemp(migration);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", migration);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start(
        "-global nvidia-quadro2.guest_hwcursor=on -incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                     HP_QUADRO2_CURSOR_CONFIG), ==,
                    CURSOR_CONFIG_64_PM);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                     HP_QUADRO2_CURSOR_POS), ==,
                    (31U << 16) | 30U);
    g_assert_cmphex(quadro2_crtc_read(qts, 0x2f), ==,
                    CURSOR_OFFSET >> 24);
    g_assert_cmphex(quadro2_crtc_read(qts, 0x30), ==,
                    0x80 | ((CURSOR_OFFSET >> 17) & 0x7f));
    g_assert_cmphex(quadro2_crtc_read(qts, 0x31), ==,
                    1 | ((CURSOR_OFFSET >> 9) & 0xfc));
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", migrated);
    assert_ppm_pixel(migrated, WIDTH, HEIGHT, 30, 31, 0xff, 0xff, 0);
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(migration), ==, 0);
    g_assert_cmpint(g_unlink(initial), ==, 0);
    g_assert_cmpint(g_unlink(updated), ==, 0);
    g_assert_cmpint(g_unlink(negative), ==, 0);
    g_assert_cmpint(g_unlink(hidden), ==, 0);
    g_assert_cmpint(g_unlink(legacy), ==, 0);
    g_assert_cmpint(g_unlink(migrated), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void nvidia_quadro2_nv4_rectangle(void)
{
    enum {
        WIDTH = 8,
        HEIGHT = 6,
        PITCH = WIDTH * sizeof(uint32_t),
    };
    const uint32_t dma_instance = 0x1a010;
    const uint32_t surface_instance = 0x1a020;
    const uint32_t clip_instance = 0x1a030;
    const uint32_t rectangle_instance = 0x1a040;
    const uint32_t blit_instance = 0x1a050;
    const uint32_t tiled_dma_instance = 0x1a060;
    const uint32_t dma_handle = 0x81000001;
    const uint32_t surface_handle = 0x81000002;
    const uint32_t clip_handle = 0x81000003;
    const uint32_t rectangle_handle = 0x81000004;
    const uint32_t blit_handle = 0x81000005;
    const uint32_t tiled_dma_handle = 0x81000006;
    const uint32_t rectangle_offset = 0x300000;
    const uint32_t blit_source_offset = 0x301000;
    const uint32_t blit_dest_offset = 0x302000;
    const uint32_t color = 0x89abcdef;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    QTestState *qts = quadro2_start("");
    unsigned int x;
    unsigned int y;

    /* A sub-dword read samples a normal MMIO register as one dword. */
    g_assert_cmphex(qtest_readw(qts, HP_QUADRO2_MMIO_BASE + 1), ==,
                    0x5000);

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + dma_instance, 0x0000303d);
    qtest_writel(qts, ramin + dma_instance + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + dma_instance + 8, 0);
    qtest_writel(qts, ramin + tiled_dma_instance, 0x0001303d);
    qtest_writel(qts, ramin + tiled_dma_instance + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + tiled_dma_instance + 8, 0);
    qtest_writel(qts, ramin + surface_instance, 0x62);
    qtest_writel(qts, ramin + clip_instance, 0x19);
    qtest_writel(qts, ramin + rectangle_instance, 0x5e);
    qtest_writel(qts, ramin + blit_instance, 0x5f);
    quadro2_ramht_insert(qts, 0, dma_handle, 0, dma_instance);
    quadro2_ramht_insert(qts, 0, surface_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, surface_instance);
    quadro2_ramht_insert(qts, 0, clip_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, clip_instance);
    quadro2_ramht_insert(qts, 0, rectangle_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, rectangle_instance);
    quadro2_ramht_insert(qts, 0, blit_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, blit_instance);
    quadro2_ramht_insert(qts, 0, tiled_dma_handle, 0,
                         tiled_dma_instance);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);

    quadro2_user_write(qts, 0, 0, 0, surface_handle);
    quadro2_user_write(qts, 0, 0, 0x184, dma_handle);
    quadro2_user_write(qts, 0, 0, 0x188, dma_handle);
    quadro2_user_write(qts, 0, 0, 0x300, 7);
    quadro2_user_write(qts, 0, 0, 0x304, (PITCH << 16) | PITCH);
    quadro2_user_write(qts, 0, 0, 0x308, rectangle_offset);
    quadro2_user_write(qts, 0, 0, 0x30c, rectangle_offset);

    quadro2_user_write(qts, 0, 1, 0, clip_handle);
    quadro2_user_write(qts, 0, 1, 0x300, (3U << 16) | 3U);
    quadro2_user_write(qts, 0, 1, 0x304, (1U << 16) | 2U);

    qtest_memset(qts, HP_QUADRO2_FB_BASE + rectangle_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, 2, 0, rectangle_handle);
    quadro2_user_write(qts, 0, 2, 0x184, clip_handle);
    quadro2_user_write(qts, 0, 2, 0x198, surface_handle);
    quadro2_user_write(qts, 0, 2, 0x2fc, 3);
    quadro2_user_write(qts, 0, 2, 0x300, 3);
    quadro2_user_write(qts, 0, 2, 0x304, color);
    quadro2_user_write(qts, 0, 2, 0x400, (2U << 16) | 1U);
    /* Apply the context clip before rejecting excessive emulated work. */
    quadro2_user_write(qts, 0, 2, 0x404, UINT32_MAX);

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint32_t expected = y == 3 && (x == 3 || x == 4) ? color : 0;

            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             rectangle_offset +
                                             y * PITCH + x * 4), ==,
                            expected);
        }
    }

    /* Both rectangle and clip points use signed XY16 coordinates. */
    quadro2_user_write(qts, 0, 1, 0x300, UINT32_MAX);
    quadro2_user_write(qts, 0, 1, 0x304, (3U << 16) | 3U);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + rectangle_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, 2, 0x400, UINT32_MAX);
    quadro2_user_write(qts, 0, 2, 0x404, (3U << 16) | 3U);

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint32_t expected = x < 2 && y < 2 ? color : 0;

            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             rectangle_offset +
                                             y * PITCH + x * 4), ==,
                            expected);
        }
    }

    /* Updating POINT alone retains SIZE; updating SIZE retains POINT. */
    quadro2_user_write(qts, 0, 1, 0x300, (3U << 16) | 3U);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + rectangle_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, 2, 0x400, 0);
    quadro2_user_write(qts, 0, 2, 0x404, UINT32_MAX);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint32_t expected = x >= 3 && x < 6 && y >= 3 ? color : 0;

            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             rectangle_offset +
                                             y * PITCH + x * 4), ==,
                            expected);
        }
    }
    quadro2_user_write(qts, 0, 1, 0x304, (1U << 16) | 2U);

    quadro2_user_write(qts, 0, 0, 0x308, blit_source_offset);
    quadro2_user_write(qts, 0, 0, 0x30c, blit_dest_offset);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + blit_dest_offset, 0,
                 PITCH * HEIGHT);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            qtest_writel(qts, HP_QUADRO2_FB_BASE + blit_source_offset +
                              y * PITCH + x * 4,
                         0x10000000U | (y << 8) | x);
        }
    }

    quadro2_user_write(qts, 0, 3, 0, blit_handle);
    quadro2_user_write(qts, 0, 3, 0x188, clip_handle);
    quadro2_user_write(qts, 0, 3, 0x19c, surface_handle);
    quadro2_user_write(qts, 0, 3, 0x2fc, 3);
    quadro2_user_write(qts, 0, 3, 0x300, (2U << 16) | 1U);
    quadro2_user_write(qts, 0, 3, 0x304, (2U << 16) | 1U);
    quadro2_user_write(qts, 0, 3, 0x308, UINT32_MAX);

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint32_t expected = y == 3 && (x == 3 || x == 4) ?
                                0x10000300U | x : 0;

            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             blit_dest_offset +
                                             y * PITCH + x * 4), ==,
                            expected);
        }
    }

    /* Differing pitches need a snapshot when the DMA rectangles overlap. */
    quadro2_user_write(qts, 0, 0, 0x300, 1);
    quadro2_user_write(qts, 0, 0, 0x304, (2U << 16) | 4U);
    quadro2_user_write(qts, 0, 0, 0x308, 0);
    quadro2_user_write(qts, 0, 0, 0x30c, 1);
    qtest_memset(qts, HP_QUADRO2_FB_BASE, 0, 8);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE, 'A');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 1, 'B');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 4, 'C');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 5, 'D');
    quadro2_user_write(qts, 0, 3, 0x188, 0);
    quadro2_user_write(qts, 0, 3, 0x300, 0);
    quadro2_user_write(qts, 0, 3, 0x304, 0);
    quadro2_user_write(qts, 0, 3, 0x308, (2U << 16) | 2U);
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 1), ==, 'A');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 2), ==, 'B');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 3), ==, 'C');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 4), ==, 'D');

    /* Equal under-stride surfaces also need an overlap snapshot. */
    quadro2_user_write(qts, 0, 0, 0x304, (1U << 16) | 1U);
    quadro2_user_write(qts, 0, 0, 0x308, 0);
    quadro2_user_write(qts, 0, 0, 0x30c, 1);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE, 'A');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 1, 'B');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 2, 'C');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 3, 'D');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 4, 'E');
    quadro2_user_write(qts, 0, 3, 0x300, 0);
    quadro2_user_write(qts, 0, 3, 0x304, 0);
    quadro2_user_write(qts, 0, 3, 0x308, (2U << 16) | 3U);
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE), ==, 'A');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 1), ==, 'A');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 2), ==, 'B');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 3), ==, 'C');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 4), ==, 'D');

    /* With no PFB tile region, both targets address the same VRAM. */
    quadro2_user_write(qts, 0, 0, 0x188, tiled_dma_handle);
    quadro2_user_write(qts, 0, 0, 0x304, (4U << 16) | 4U);
    quadro2_user_write(qts, 0, 0, 0x308, 0);
    quadro2_user_write(qts, 0, 0, 0x30c, 1);
    qtest_memset(qts, HP_QUADRO2_FB_BASE, 0, 8);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE, 'A');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 1, 'B');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 2, 'C');
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + 3, 'D');
    quadro2_user_write(qts, 0, 3, 0x300, 0);
    quadro2_user_write(qts, 0, 3, 0x304, 0);
    quadro2_user_write(qts, 0, 3, 0x308, (1U << 16) | 4U);
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 1), ==, 'A');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 2), ==, 'B');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 3), ==, 'C');
    g_assert_cmphex(qtest_readb(qts, HP_QUADRO2_FB_BASE + 4), ==, 'D');

    qtest_quit(qts);
}

static void nvidia_quadro2_scaled_yuv(void)
{
    enum {
        PITCH = 16,
        SOURCE_OFFSET = 0x310000,
        DESTINATION_OFFSET = 0x320000,
        SECOND_DESTINATION_OFFSET = 0x321000,
    };
    const uint32_t dma_instance = 0x1b010;
    const uint32_t surface_instance = 0x1b020;
    const uint32_t scaled_instance = 0x1b030;
    const uint32_t dma_handle = 0x82000001;
    const uint32_t surface_handle = 0x82000002;
    const uint32_t scaled_handle = 0x82000003;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    QTestState *qts = quadro2_start("");

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + dma_instance, 0x0000303d);
    qtest_writel(qts, ramin + dma_instance + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + dma_instance + 8, 0);
    qtest_writel(qts, ramin + surface_instance, 0x62);
    qtest_writel(qts, ramin + scaled_instance, 0x89);
    quadro2_ramht_insert(qts, 0, dma_handle, 0, dma_instance);
    quadro2_ramht_insert(qts, 0, surface_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, surface_instance);
    quadro2_ramht_insert(qts, 0, scaled_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, scaled_instance);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);

    quadro2_user_write(qts, 0, 0, 0, surface_handle);
    quadro2_user_write(qts, 0, 0, 0x184, dma_handle);
    quadro2_user_write(qts, 0, 0, 0x188, dma_handle);
    quadro2_user_write(qts, 0, 0, 0x300, 7);
    quadro2_user_write(qts, 0, 0, 0x304, (PITCH << 16) | PITCH);
    quadro2_user_write(qts, 0, 0, 0x30c, DESTINATION_OFFSET);

    quadro2_user_write(qts, 0, 1, 0, scaled_handle);
    quadro2_user_write(qts, 0, 1, 0x184, dma_handle);
    quadro2_user_write(qts, 0, 1, 0x198, surface_handle);
    quadro2_user_write(qts, 0, 1, 0x2fc, 0);
    quadro2_user_write(qts, 0, 1, 0x300, 5);
    quadro2_user_write(qts, 0, 1, 0x304, 3);
    quadro2_user_write(qts, 0, 1, 0x308, 0);
    quadro2_user_write(qts, 0, 1, 0x30c, (1U << 16) | 2U);
    quadro2_user_write(qts, 0, 1, 0x310, 0);
    quadro2_user_write(qts, 0, 1, 0x314, (1U << 16) | 2U);
    quadro2_user_write(qts, 0, 1, 0x318, 1U << 20);
    quadro2_user_write(qts, 0, 1, 0x31c, 1U << 20);
    quadro2_user_write(qts, 0, 1, 0x400, (1U << 16) | 2U);
    /* Match the X.Org NV blit-video path: center origin and bilinear. */
    quadro2_user_write(qts, 0, 1, 0x404,
                       (1U << 24) | (1U << 16) | 4U);
    quadro2_user_write(qts, 0, 1, 0x408, SOURCE_OFFSET);

    /* YUYV shares neutral chroma while carrying black and white luma. */
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET, 0x80eb8010);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + DESTINATION_OFFSET, 0,
                 PITCH);
    quadro2_user_write(qts, 0, 1, 0x40c, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET), ==, 0xff000000);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 4), ==,
                    0xffffffff);

    /* UYVY uses the other byte order for the same two-pixel chroma pair. */
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET, 0x51f0515a);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + SECOND_DESTINATION_OFFSET, 0,
                 PITCH);
    quadro2_user_write(qts, 0, 0, 0x30c, SECOND_DESTINATION_OFFSET);
    quadro2_user_write(qts, 0, 1, 0x2fc, 2);
    quadro2_user_write(qts, 0, 1, 0x300, 6);
    quadro2_user_write(qts, 0, 1, 0x40c, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    SECOND_DESTINATION_OFFSET), ==,
                    0xffff0000);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    SECOND_DESTINATION_OFFSET + 4), ==,
                    0xffff0000);

    /* NV4+ SCALED_IMAGE rejects odd source widths before reading a pair. */
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SECOND_DESTINATION_OFFSET,
                 0x5aa5c33c);
    quadro2_user_write(qts, 0, 1, 0x30c, (1U << 16) | 1U);
    quadro2_user_write(qts, 0, 1, 0x310, 0);
    quadro2_user_write(qts, 0, 1, 0x314, (1U << 16) | 1U);
    quadro2_user_write(qts, 0, 1, 0x400, (1U << 16) | 1U);
    quadro2_user_write(qts, 0, 1, 0x40c, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    SECOND_DESTINATION_OFFSET), ==,
                    0x5aa5c33c);

    qtest_quit(qts);
}

static void nvidia_quadro2_m2mf_notify(void)
{
    enum {
        PITCH = 8,
        SOURCE_OFFSET = 0x330000,
        DESTINATION_OFFSET = 0x331000,
        NOTIFY_OFFSET = 0x332000,
    };
    const uint32_t data_dma_instance = 0x1b110;
    const uint32_t notify_dma_instance = 0x1b120;
    const uint32_t m2mf_instance = 0x1b130;
    const uint32_t data_dma_handle = 0x83000001;
    const uint32_t notify_dma_handle = 0x83000002;
    const uint32_t m2mf_handle = 0x83000003;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    const uint32_t canary = 0xa5a5a5a5;
    QTestState *qts = quadro2_start("");
    uint64_t timer_before;
    uint64_t timer_after;
    uint64_t notifier_time;
    uint32_t notifier_time_low;
    uint32_t notifier_time_high;

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + data_dma_instance, 0x0000303d);
    qtest_writel(qts, ramin + data_dma_instance + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + data_dma_instance + 8, 0);
    qtest_writel(qts, ramin + notify_dma_instance, 0x0000303d);
    qtest_writel(qts, ramin + notify_dma_instance + 4, 0xff);
    qtest_writel(qts, ramin + notify_dma_instance + 8, NOTIFY_OFFSET);
    qtest_writel(qts, ramin + m2mf_instance, 0x39);
    quadro2_ramht_insert(qts, 0, data_dma_handle, 0, data_dma_instance);
    quadro2_ramht_insert(qts, 0, notify_dma_handle, 0,
                         notify_dma_instance);
    quadro2_ramht_insert(qts, 0, m2mf_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, m2mf_instance);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);

    quadro2_user_write(qts, 0, 0, 0, m2mf_handle);
    quadro2_user_write(qts, 0, 0, 0x180, notify_dma_handle);
    quadro2_user_write(qts, 0, 0, 0x184, data_dma_handle);
    quadro2_user_write(qts, 0, 0, 0x188, data_dma_handle);
    quadro2_user_write(qts, 0, 0, 0x30c, SOURCE_OFFSET);
    quadro2_user_write(qts, 0, 0, 0x310, DESTINATION_OFFSET);
    quadro2_user_write(qts, 0, 0, 0x314, PITCH);
    quadro2_user_write(qts, 0, 0, 0x318, PITCH);
    quadro2_user_write(qts, 0, 0, 0x31c, 4);
    quadro2_user_write(qts, 0, 0, 0x320, 2);
    quadro2_user_write(qts, 0, 0, 0x324, 0x101);

    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET, 0x44332211);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + PITCH,
                 0x88776655);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + DESTINATION_OFFSET, 0xa5,
                 PITCH * 2);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET, 0xa5, 0x20);

    /* WRITE_ONLY copies pitched lines and writes notifier slot 0x10. */
    timer_before = quadro2_timer_read(qts);
    quadro2_user_write(qts, 0, 0, 0x328, 0);
    timer_after = quadro2_timer_read(qts);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET), ==, 0x44332211);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 4), ==, canary);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + PITCH), ==,
                    0x88776655);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0x18), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0x1c), ==, 0);
    notifier_time_low = qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                         NOTIFY_OFFSET + 0x10);
    notifier_time_high = qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                          NOTIFY_OFFSET + 0x14);
    notifier_time = ((uint64_t)notifier_time_high << 27) |
                    (notifier_time_low >> 5);
    g_assert_cmphex(notifier_time_low & 0x1f, ==, 0);
    g_assert_cmpuint(notifier_time, >=, timer_before);
    g_assert_cmpuint(notifier_time, <=, timer_after);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PGRAPH_INTR) & 1, ==, 0);

    /* WRITE_THEN_AWAKEN writes the same slot and raises NOTIFY. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET + 0x10, 0xa5,
                 0x10);
    quadro2_user_write(qts, 0, 0, 0x328, 1);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0x18), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0x1c), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PGRAPH_INTR) & 1, ==, 1);

    /* FORMAT increments gather bytes; only BUF_NOTIFY bit 0 is significant. */
    quadro2_user_write(qts, 0, 0, 0x30c, SOURCE_OFFSET + 0x100);
    quadro2_user_write(qts, 0, 0, 0x310, DESTINATION_OFFSET + 0x100);
    quadro2_user_write(qts, 0, 0, 0x314, 20);
    quadro2_user_write(qts, 0, 0, 0x318, 8);
    quadro2_user_write(qts, 0, 0, 0x324, 0x104);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x100, 0xa5,
                 40);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + DESTINATION_OFFSET + 0x100, 0xa5,
                 16);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x100, 0x10);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x104, 0x11);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x108, 0x12);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x10c, 0x13);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x114, 0x20);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x118, 0x21);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x11c, 0x22);
    qtest_writeb(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x120, 0x23);
    quadro2_user_write(qts, 0, 0, 0x328, 0x100);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x100), ==,
                    0x13121110);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x108), ==,
                    0x23222120);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x104), ==, canary);

    /* FORMAT increments scatter bytes without touching intervening data. */
    quadro2_user_write(qts, 0, 0, 0x30c, SOURCE_OFFSET + 0x200);
    quadro2_user_write(qts, 0, 0, 0x310, DESTINATION_OFFSET + 0x200);
    quadro2_user_write(qts, 0, 0, 0x314, 8);
    quadro2_user_write(qts, 0, 0, 0x318, 20);
    quadro2_user_write(qts, 0, 0, 0x324, 0x401);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x200,
                 0x33323130);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + SOURCE_OFFSET + 0x208,
                 0x43424140);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + DESTINATION_OFFSET + 0x200, 0xa5,
                 40);
    quadro2_user_write(qts, 0, 0, 0x328, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x200), ==,
                    0xa5a5a530);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x204), ==,
                    0xa5a5a531);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x20c), ==,
                    0xa5a5a533);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x214), ==,
                    0xa5a5a540);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 0x220), ==,
                    0xa5a5a543);

    /* Envytools records 0x7fff as the last valid M2MF pitch. */
    quadro2_user_write(qts, 0, 0, 0x314, 0x7fff);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PFIFO_INTR_0) & 1, ==, 0);
    quadro2_user_write(qts, 0, 0, 0x314, 0x8000);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PFIFO_INTR_0) & 1, ==, 1);

    qtest_quit(qts);
}

static void nvidia_quadro2_notify_pending(void)
{
    enum {
        CHANNEL = 2,
        OTHER_CHANNEL = 3,
        SUBCHANNEL = 2,
        OTHER_SUBCHANNEL = 3,
        NOTIFY_OFFSET = 0x333000,
        ALT_NOTIFY_OFFSET = 0x334000,
        NOTIFY_DMA_INSTANCE = 0x1b140,
        M2MF_INSTANCE = 0x1b150,
        ALT_NOTIFY_DMA_INSTANCE = 0x1b160,
    };
    const uint32_t notify_dma_handle = 0x84000001;
    const uint32_t m2mf_handle = 0x84000002;
    const uint32_t alt_notify_dma_handle = 0x84000003;
    const uint64_t mmio = HP_QUADRO2_MMIO_BASE;
    const uint64_t ramin = mmio + HP_QUADRO2_PRAMIN;
    const uint32_t canary = 0xa5a5a5a5;
    char path[] = "/tmp/quadro2-notify-migration-XXXXXX";
    char *uri;
    QTestState *qts = quadro2_start("");
    int fd;

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE, 0x0000303d);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE + 4, 0xff);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE + 8, NOTIFY_OFFSET);
    qtest_writel(qts, ramin + ALT_NOTIFY_DMA_INSTANCE, 0x0000303d);
    qtest_writel(qts, ramin + ALT_NOTIFY_DMA_INSTANCE + 4, 0xff);
    qtest_writel(qts, ramin + ALT_NOTIFY_DMA_INSTANCE + 8,
                 ALT_NOTIFY_OFFSET);
    qtest_writel(qts, ramin + M2MF_INSTANCE, 0x39);
    quadro2_ramht_insert(qts, CHANNEL, notify_dma_handle, 0,
                         NOTIFY_DMA_INSTANCE);
    quadro2_ramht_insert(qts, CHANNEL, m2mf_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, M2MF_INSTANCE);
    quadro2_ramht_insert(qts, OTHER_CHANNEL, m2mf_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, M2MF_INSTANCE);
    quadro2_ramht_insert(qts, OTHER_CHANNEL, alt_notify_dma_handle, 0,
                         ALT_NOTIFY_DMA_INSTANCE);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_RAMHT, 0x03000100);

    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0, m2mf_handle);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x180,
                       notify_dma_handle);
    quadro2_user_write(qts, CHANNEL, OTHER_SUBCHANNEL, 0, m2mf_handle);
    quadro2_user_write(qts, OTHER_CHANNEL, SUBCHANNEL, 0, m2mf_handle);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET, 0xa5, 0x10);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + ALT_NOTIFY_OFFSET, 0xa5, 0x10);

    /* NOTIFY arms channel-local state; neither it nor another channel fires. */
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 1);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, canary);
    quadro2_user_write(qts, OTHER_CHANNEL, SUBCHANNEL, 0x100, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, canary);
    /* Another channel may mutate the shared GROBJ after this arm. */
    quadro2_user_write(qts, OTHER_CHANNEL, SUBCHANNEL, 0x180,
                       alt_notify_dma_handle);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    ALT_NOTIFY_OFFSET + 0xc), ==, canary);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==, 0);

    /* An armed descriptor may become invalid without blocking migration. */
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE, 0);

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);

    g_assert_cmphex(qtest_readl(qts, ramin + NOTIFY_DMA_INSTANCE), ==, 0);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE, 0x0000303d);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE + 4, 0xff);
    qtest_writel(qts, ramin + NOTIFY_DMA_INSTANCE + 8, NOTIFY_OFFSET);

    /* The next same-object method completes the migrated request. */
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x100, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 8), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    ALT_NOTIFY_OFFSET + 0xc), ==, canary);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==, 1);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x180,
                       notify_dma_handle);

    /* Changing DMA_NOTIFY while armed raises NOTIFY_IN_USE. */
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_INTR, 1);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET, 0xa5, 0x10);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 0);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x180,
                       notify_dma_handle);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==,
                    1U << 20);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSTATUS), ==,
                    1U << 23);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    1U << 13);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_ADDR), ==,
                    (CHANNEL << 20) | (SUBCHANNEL << 16) | 0x180);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, canary);

    /* Recovery keeps the request armed so the original object can finish. */
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_INTR, 1U << 20);
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS, 1);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x100, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, 0);

    /* A second NOTIFY while armed raises the distinct DOUBLE_NOTIFY fault. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET, 0xa5, 0x10);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 0);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==,
                    1U << 20);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    1U << 12);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_ADDR), ==,
                    (CHANNEL << 20) | (SUBCHANNEL << 16) | 0x104);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, canary);
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_INTR, 1U << 20);
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS, 1);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x100, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, 0);

    /* A context switch drops the pending request without a method fault. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + NOTIFY_OFFSET, 0xa5, 0x10);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 0);
    quadro2_user_write(qts, CHANNEL, OTHER_SUBCHANNEL, 0, m2mf_handle);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS), ==,
                    1);
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x100, 0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    NOTIFY_OFFSET + 0xc), ==, canary);

    /* Invalid NOTIFY data is a PGRAPH data error, not a PFIFO fault. */
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x104, 2);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==,
                    1U << 20);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    1U << 1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSTATUS) &
                    (1U << 25), ==, 1U << 25);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PFIFO_INTR_0), ==, 0);

    qtest_quit(qts);
    g_free(uri);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void display_wait_for_migration_status(QTestState *qts,
                                              const char *wanted)
{
    int64_t deadline = g_get_monotonic_time() + 60 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-migrate'}");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, wanted)) {
            qobject_unref(result);
            return;
        }
        if (!strcmp(status, "failed") || !strcmp(status, "failing") ||
            !strcmp(status, "cancelled") ||
            (!strcmp(status, "completed") && strcmp(wanted, "completed"))) {
            g_error("migration entered terminal status '%s'", status);
        }
        qobject_unref(result);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void display_wait_for_migration(QTestState *qts)
{
    display_wait_for_migration_status(qts, "completed");
}

static void ati_crtc_page_flip(void)
{
    static const char * const models[] = { "rv100", "es1000" };
    enum {
        WIDTH = 64,
        HEIGHT = 32,
        NEW_PITCH = WIDTH * 2,
        PAGE_OFFSET = WIDTH * HEIGHT * 4,
        VRAM_BYTES = PAGE_OFFSET + NEW_PITCH * HEIGHT * 4,
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(models); i++) {
        g_autofree char *tmpdir = NULL;
        g_autofree char *initial = NULL;
        g_autofree char *locked = NULL;
        g_autofree char *migrated = NULL;
        g_autofree char *flipped = NULL;
        g_autofree char *uri = NULL;
        g_autoptr(GError) error = NULL;
        char path[] = "/tmp/ati-page-flip-migration-XXXXXX";
        QTestState *qts;
        int fd;

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s", models[i]);
        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE, 0, VRAM_BYTES);
        qtest_writel(qts, IA64_RV100_FB_BASE, 0x00ff0000);
        qtest_writel(qts, IA64_RV100_FB_BASE + WIDTH * 4, 0x00ff0000);
        qtest_writel(qts, IA64_RV100_FB_BASE + PAGE_OFFSET, 0x0000ff00);
        qtest_writel(qts, IA64_RV100_FB_BASE + PAGE_OFFSET + WIDTH * 4,
                     0x000000ff);
        qtest_writel(qts, IA64_RV100_FB_BASE + PAGE_OFFSET + NEW_PITCH * 4,
                     0x0000ff00);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_H_TOTAL_DISP,
                     ((WIDTH / 8) - 1) << 16);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_V_TOTAL_DISP,
                     (HEIGHT + 4 - 1) | ((HEIGHT - 1) << 16));
        qtest_writel(qts, IA64_RV100_MMIO_BASE +
                          ATI_CRTC_V_SYNC_STRT_WID,
                     (HEIGHT + 2 - 1) | (1 << 16));
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_PITCH, WIDTH / 8);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                     ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                     ATI_CRTC_PIX_WIDTH_32);
        qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(VGA_INPUT_STATUS1));
        qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_ATTR_INDEX), 0x20);

        tmpdir = g_dir_make_tmp("ia64-ati-page-flip-XXXXXX", &error);
        g_assert_no_error(error);
        g_assert_nonnull(tmpdir);
        initial = g_build_filename(tmpdir, "initial.ppm", NULL);
        locked = g_build_filename(tmpdir, "locked.ppm", NULL);
        migrated = g_build_filename(tmpdir, "migrated.ppm", NULL);
        flipped = g_build_filename(tmpdir, "flipped.ppm", NULL);

        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", initial);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
        assert_ppm_pixel(initial, WIDTH, HEIGHT, 0, 1, 0xff, 0, 0);

        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET,
                     PAGE_OFFSET | ATI_CRTC_OFFSET_LOCK);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_PITCH,
                     NEW_PITCH / 8);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_CRTC_OFFSET), ==,
                        PAGE_OFFSET | ATI_CRTC_OFFSET_LOCK |
                        ATI_CRTC_OFFSET_PENDING);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", locked);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 1, 0xff, 0, 0);

        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET,
                     PAGE_OFFSET);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_CRTC_OFFSET), ==,
                        PAGE_OFFSET | ATI_CRTC_OFFSET_PENDING);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_CNTL,
                     ATI_CRTC_VBLANK_INT);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_CRTC_OFFSET), ==,
                        PAGE_OFFSET | ATI_CRTC_OFFSET_PENDING);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", locked);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
        assert_ppm_pixel(locked, WIDTH, HEIGHT, 0, 1, 0xff, 0, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_CNTL, 0);

        fd = g_mkstemp(path);
        g_assert_cmpint(fd, >=, 0);
        close(fd);
        uri = g_strdup_printf("file:%s", path);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
        display_wait_for_migration(qts);
        qtest_quit(qts);

        qts = qtest_initf("-machine ia64-vpc,nvram=none -m 256M -S "
                          "-vga ati -global ati-vga.model=%s "
                          "-incoming defer", models[i]);
        qtest_qmp_assert_success(
            qts, "{'execute':'migrate-incoming','arguments':"
                 "{'uri':%s,'exit-on-error':false}}", uri);
        display_wait_for_migration(qts);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_CRTC_OFFSET), ==,
                        PAGE_OFFSET | ATI_CRTC_OFFSET_PENDING);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", migrated);
        assert_ppm_pixel(migrated, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
        assert_ppm_pixel(migrated, WIDTH, HEIGHT, 0, 1, 0xff, 0, 0);

        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_CRTC_OFFSET), ==, PAGE_OFFSET);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", flipped);
        assert_ppm_pixel(flipped, WIDTH, HEIGHT, 0, 0, 0, 0xff, 0);
        assert_ppm_pixel(flipped, WIDTH, HEIGHT, 0, 1, 0, 0xff, 0);

        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_GEN_INT_STATUS,
                     ATI_CRTC_VBLANK_INT);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET,
                     ATI_CRTC_OFFSET_LOCK);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_PITCH, WIDTH / 8);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_OFFSET, 0);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_CRTC_GEN_CNTL,
                     ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN |
                     ATI_CRTC_PIX_WIDTH_32);
        qtest_qmp_assert_success(qts, "{'execute':'cont'}");
        qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
        qtest_qmp_assert_success(qts, "{'execute':'stop'}");
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                        ATI_GEN_INT_STATUS) &
                        ATI_CRTC_VBLANK_INT, ==, ATI_CRTC_VBLANK_INT);
        qtest_qmp_assert_success(qts,
                                 "{'execute':'screendump','arguments':"
                                 " {'filename':%s}}", flipped);
        assert_ppm_pixel(flipped, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
        assert_ppm_pixel(flipped, WIDTH, HEIGHT, 0, 1, 0xff, 0, 0);
        qtest_quit(qts);

        g_assert_cmpint(g_unlink(path), ==, 0);
        g_assert_cmpint(g_unlink(initial), ==, 0);
        g_assert_cmpint(g_unlink(locked), ==, 0);
        g_assert_cmpint(g_unlink(migrated), ==, 0);
        g_assert_cmpint(g_unlink(flipped), ==, 0);
        g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
    }
}

static void ati_crtc_offset_control(void)
{
    enum { WIDTH = 64, HEIGHT = 32, PAGE_OFFSET = 0x20000 };
    const uint64_t mmio = IA64_RV100_MMIO_BASE;
    const uint32_t control = ATI_RV100_CRTC_OFFSET_CNTL_RESET;
    g_autofree char *tmpdir = g_dir_make_tmp("ati-offset-control-XXXXXX", NULL);
    g_autofree char *screen = NULL;
    QTestState *qts;

    g_assert_nonnull(tmpdir);
    screen = g_build_filename(tmpdir, "screen.ppm", NULL);
    qts = qtest_init("-machine ia64-vpc,nvram=none -m 256M -S "
                      "-display vnc=none -vga ati -global ati-vga.model=rv100");
    ati_pci_enable(qts);
    qtest_memset(qts, IA64_RV100_FB_BASE, 0, PAGE_OFFSET + WIDTH * HEIGHT * 4);
    qtest_writel(qts, IA64_RV100_FB_BASE, 0x00ff0000);
    qtest_writel(qts, IA64_RV100_FB_BASE + PAGE_OFFSET, 0x0000ff00);
    qtest_writel(qts, mmio + ATI_CRTC_H_TOTAL_DISP, ((WIDTH / 8) - 1) << 16);
    qtest_writel(qts, mmio + ATI_CRTC_V_TOTAL_DISP,
                 (HEIGHT + 4 - 1) | ((HEIGHT - 1) << 16));
    qtest_writel(qts, mmio + ATI_CRTC_V_SYNC_STRT_WID,
                 (HEIGHT + 2 - 1) | (1 << 16));
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET, 0);
    qtest_writel(qts, mmio + ATI_CRTC_PITCH, WIDTH / 8);
    qtest_writel(qts, mmio + ATI_CRTC_GEN_CNTL,
                 ATI_CRTC_EXT_DISP_EN | ATI_CRTC_EN | ATI_CRTC_PIX_WIDTH_32);
    qtest_readb(qts, IA64_LEGACY_IO_PORT_PA(VGA_INPUT_STATUS1));
    qtest_writeb(qts, IA64_LEGACY_IO_PORT_PA(VGA_ATTR_INDEX), 0x20);

    /* The lock and pending indication must be visible through either alias. */
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET,
                 PAGE_OFFSET | ATI_CRTC_OFFSET_LOCK);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET_CNTL), ==,
                    control | ATI_CRTC_OFFSET_LOCK | ATI_CRTC_OFFSET_PENDING);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS * 2 + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
    g_assert_cmphex(qtest_readb(qts, mmio + ATI_CRTC_OFFSET_CNTL + 3), ==,
                    (control | ATI_CRTC_OFFSET_LOCK |
                     ATI_CRTC_OFFSET_PENDING) >> 24);

    /* A low-byte control update must preserve the shared lock. */
    qtest_writeb(qts, mmio + ATI_CRTC_OFFSET_CNTL, 3);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==,
                    PAGE_OFFSET | ATI_CRTC_OFFSET_LOCK |
                    ATI_CRTC_OFFSET_PENDING);
    qtest_writew(qts, mmio + ATI_CRTC_OFFSET_CNTL, 0);

    /* Unlock through OFFSET_CNTL without rewriting OFFSET. */
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET_CNTL, control);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==,
                    PAGE_OFFSET | ATI_CRTC_OFFSET_PENDING);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET_CNTL), ==,
                    control | ATI_CRTC_OFFSET_PENDING);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==, PAGE_OFFSET);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET_CNTL), ==, control);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 0, 0, 0, 0xff, 0);

    /* The pending status is read-only; the lock accepts byte writes as well. */
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET_CNTL,
                 control | ATI_CRTC_OFFSET_PENDING);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET_CNTL), ==, control);
    qtest_writeb(qts, mmio + ATI_CRTC_OFFSET_CNTL + 3,
                 (control | ATI_CRTC_OFFSET_LOCK) >> 24);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==,
                    PAGE_OFFSET | ATI_CRTC_OFFSET_LOCK);
    qtest_writel(qts, mmio + ATI_CRTC_OFFSET, ATI_CRTC_OFFSET_LOCK);
    qtest_writeb(qts, mmio + ATI_CRTC_OFFSET_CNTL + 3, control >> 24);
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==,
                    ATI_CRTC_OFFSET_PENDING);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_clock_step(qts, ATI_VBLANK_FRAME_NS + 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(qtest_readl(qts, mmio + ATI_CRTC_OFFSET), ==, 0);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", screen);
    assert_ppm_pixel(screen, WIDTH, HEIGHT, 0, 0, 0xff, 0, 0);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(screen), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void nvidia_quadro2_nvidiafb_blit(void)
{
    enum {
        WIDTH = 4,
        HEIGHT = 3,
        PITCH = 32,
        SURFACE_SUBCHANNEL = 0,
        PATTERN_SUBCHANNEL = 1,
        ROP_SUBCHANNEL = 2,
        EXPLICIT_BLIT_SUBCHANNEL = 4,
        BLIT_SUBCHANNEL = 5,
        GDI_SUBCHANNEL = 6,
    };
    g_autofree char *path = g_strdup_printf(
        "%s/nvidia-quadro2-nvidiafb.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    const uint32_t dma_instance = 0x12000;
    const uint32_t surface_instance = 0x12010;
    const uint32_t pattern_instance = 0x12020;
    const uint32_t rop_instance = 0x12030;
    const uint32_t blit_instance = 0x12060;
    const uint32_t gdi_instance = 0x12070;
    const uint32_t explicit_blit_instance = 0x12080;
    const uint32_t dma_handle = 0x80000001;
    const uint32_t surface_handle = 0x80000010;
    const uint32_t pattern_handle = 0x80000012;
    const uint32_t rop_handle = 0x80000011;
    const uint32_t blit_handle = 0x80000015;
    const uint32_t gdi_handle = 0x80000016;
    const uint32_t explicit_blit_handle = 0x80000018;
    const uint32_t source_offset = 0x340000;
    const uint32_t destination_offset = 0x341000;
    const uint32_t fill_color = 0x89abcdef;
    const uint32_t pattern_color = 0x13579bdf;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    QTestState *qts = quadro2_start("");
    unsigned int x;
    unsigned int y;
    int fd;

    qtest_memset(qts, ramin + 0x10000, 0, 4096);

    /* Linux nvidiafb's classless, linear VRAM framebuffer DMA object. */
    qtest_writel(qts, ramin + dma_instance, 0x00003000);
    qtest_writel(qts, ramin + dma_instance + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + dma_instance + 8, 2);
    qtest_writel(qts, ramin + dma_instance + 12, 2);

    /* Its NV10 surface and NV15 2D GROBJ seeds share that DMA object. */
    qtest_writel(qts, ramin + surface_instance, 0x01008062);
    qtest_writel(qts, ramin + surface_instance + 4, 0);
    qtest_writel(qts, ramin + surface_instance + 8, 0x12001200);
    qtest_writel(qts, ramin + surface_instance + 12, 0);
    qtest_writel(qts, ramin + pattern_instance, 0x01008044);
    qtest_writel(qts, ramin + pattern_instance + 4, 2);
    qtest_writel(qts, ramin + rop_instance, 0x01008043);
    qtest_writel(qts, ramin + blit_instance, 0x0100809f);
    qtest_writel(qts, ramin + blit_instance + 4, 0);
    qtest_writel(qts, ramin + blit_instance + 8, 0x12001200);
    qtest_writel(qts, ramin + blit_instance + 12, 0);
    qtest_writel(qts, ramin + gdi_instance, 0x0100804a);
    qtest_writel(qts, ramin + gdi_instance + 4, 2);
    qtest_writel(qts, ramin + gdi_instance + 8, 0);
    qtest_writel(qts, ramin + gdi_instance + 12, 0);
    qtest_writel(qts, ramin + explicit_blit_instance, 0x0100809f);
    qtest_writel(qts, ramin + explicit_blit_instance + 4, 0);
    qtest_writel(qts, ramin + explicit_blit_instance + 8, 0x12001200);
    qtest_writel(qts, ramin + explicit_blit_instance + 12, 0);
    quadro2_ramht_insert(qts, 0, dma_handle, 0, dma_instance);
    quadro2_ramht_insert(qts, 0, surface_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, surface_instance);
    quadro2_ramht_insert(qts, 0, pattern_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, pattern_instance);
    quadro2_ramht_insert(qts, 0, rop_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, rop_instance);
    quadro2_ramht_insert(qts, 0, blit_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, blit_instance);
    quadro2_ramht_insert(qts, 0, gdi_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, gdi_instance);
    quadro2_ramht_insert(qts, 0, explicit_blit_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS,
                         explicit_blit_instance);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);

    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0, surface_handle);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x300, 6);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x304,
                       (PITCH << 16) | PITCH);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x308, source_offset);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x30c,
                       destination_offset);
    quadro2_user_write(qts, 0, PATTERN_SUBCHANNEL, 0, pattern_handle);
    quadro2_user_write(qts, 0, PATTERN_SUBCHANNEL, 0x30c, 0);
    quadro2_user_write(qts, 0, PATTERN_SUBCHANNEL, 0x314, pattern_color);
    quadro2_user_write(qts, 0, ROP_SUBCHANNEL, 0, rop_handle);
    quadro2_user_write(qts, 0, ROP_SUBCHANNEL, 0x300, 0xcc);
    quadro2_user_write(qts, 0, EXPLICIT_BLIT_SUBCHANNEL, 0,
                       explicit_blit_handle);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0, blit_handle);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0, gdi_handle);

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            qtest_writel(qts, HP_QUADRO2_FB_BASE + source_offset +
                              y * PITCH + x * sizeof(uint32_t),
                         0x10000000U | (y << 8) | x);
        }
    }
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);

    /* Drawing methods inherit the seeded BLIT state. */
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0x10000000U | (y << 8) | x);
        }
    }

    /* Fill methods inherit the seeded GDI state. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x3fc, fill_color);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x400,
                       (1U << 16) | 1U);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x404,
                       (2U << 16) | 1U);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint32_t expected = y == 1 && (x == 1 || x == 2) ?
                                fill_color : 0;

            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            expected);
        }
    }

    /* Classless framebuffer DMA remains valid through explicit references. */
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x184, 0);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x188, 0);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x184, dma_handle);
    quadro2_user_write(qts, 0, SURFACE_SUBCHANNEL, 0x188, dma_handle);
    quadro2_user_write(qts, 0, EXPLICIT_BLIT_SUBCHANNEL, 0x19c,
                       surface_handle);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, EXPLICIT_BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, EXPLICIT_BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, EXPLICIT_BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0x10000000U | (y << 8) | x);
        }
    }

    /* Native BLIT inherits the bound pattern and ROP context objects. */
    quadro2_user_write(qts, 0, ROP_SUBCHANNEL, 0x300, 0xf0);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            pattern_color);
        }
    }

    /* Explicit NULL disconnects pattern instead of falling back to it. */
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x18c, 0);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            UINT32_MAX);
        }
    }

    /* Explicit NULL ROP restores the class's default source copy. */
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x190, 0);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0x10000000U | (y << 8) | x);
        }
    }

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);

    /* Explicit NULL pattern/ROP connections survive migration. */
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            qtest_writel(qts, HP_QUADRO2_FB_BASE + source_offset +
                              y * PITCH + x * sizeof(uint32_t),
                         0x20000000U | (y << 8) | x);
        }
    }
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0x20000000U | (y << 8) | x);
        }
    }

    /* An explicit NULL GDI surface disables inherited active-surface state. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0x5a,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x198, 0);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x3fc, fill_color);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x400,
                       (1U << 16) | 1U);
    quadro2_user_write(qts, 0, GDI_SUBCHANNEL, 0x404,
                       (2U << 16) | 1U);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0x5a5a5a5a);
        }
    }

    /* An explicit NULL BLIT surface disables inherited active-surface state. */
    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0xa5,
                 PITCH * HEIGHT);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x19c, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x300, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x304, 0);
    quadro2_user_write(qts, 0, BLIT_SUBCHANNEL, 0x308,
                       (HEIGHT << 16) | WIDTH);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                             destination_offset +
                                             y * PITCH +
                                             x * sizeof(uint32_t)), ==,
                            0xa5a5a5a5);
        }
    }

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void nvidia_quadro2_shared_context(void)
{
    enum {
        CHANNEL0 = 3,
        CHANNEL1 = 4,
        SURFACE_SUBCHANNEL = 0,
        PATTERN_A_SUBCHANNEL = 1,
        ROP_A_SUBCHANNEL = 2,
        PATTERN_B_SUBCHANNEL = 3,
        ROP_B_SUBCHANNEL = 4,
        GDI_SUBCHANNEL = 5,
        DMA_INSTANCE = 0x1c000,
        SURFACE_INSTANCE = 0x1c010,
        GDI_INSTANCE = 0x1c020,
        PATTERN_A_INSTANCE = 0x1c030,
        PATTERN_B_INSTANCE = 0x1c040,
        ROP_A_INSTANCE = 0x1c050,
        ROP_B_INSTANCE = 0x1c060,
        DESTINATION_OFFSET = 0x350000,
        PITCH = 64,
    };
    static const unsigned int channels[] = { CHANNEL0, CHANNEL1 };
    const uint32_t dma_handle = 0x85000001;
    const uint32_t surface_handle = 0x85000002;
    const uint32_t gdi_handle = 0x85000003;
    const uint32_t pattern_a_handle = 0x85000004;
    const uint32_t pattern_b_handle = 0x85000005;
    const uint32_t rop_a_handle = 0x85000006;
    const uint32_t rop_b_handle = 0x85000007;
    const uint32_t color_a = 0x11223344;
    const uint32_t color_b = 0x55667788;
    const uint32_t color_channel1 = 0x99aabbcc;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    g_autofree char *path = g_strdup_printf(
        "%s/nvidia-quadro2-shared-context.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    QTestState *qts = quadro2_start("");
    unsigned int i;
    int fd;

    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_writel(qts, ramin + DMA_INSTANCE, 0x0000303d);
    qtest_writel(qts, ramin + DMA_INSTANCE + 4,
                 HP_QUADRO2_VRAM_SIZE - 1);
    qtest_writel(qts, ramin + DMA_INSTANCE + 8, 0);
    qtest_writel(qts, ramin + SURFACE_INSTANCE, 0x62);
    qtest_writel(qts, ramin + GDI_INSTANCE, 0x4a);
    qtest_writel(qts, ramin + PATTERN_A_INSTANCE, 0x44);
    qtest_writel(qts, ramin + PATTERN_B_INSTANCE, 0x44);
    qtest_writel(qts, ramin + ROP_A_INSTANCE, 0x43);
    qtest_writel(qts, ramin + ROP_B_INSTANCE, 0x43);

    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned int channel = channels[i];

        quadro2_ramht_insert(qts, channel, dma_handle, 0, DMA_INSTANCE);
        quadro2_ramht_insert(qts, channel, surface_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS, SURFACE_INSTANCE);
        quadro2_ramht_insert(qts, channel, gdi_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS, GDI_INSTANCE);
        quadro2_ramht_insert(qts, channel, pattern_a_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS,
                             PATTERN_A_INSTANCE);
        quadro2_ramht_insert(qts, channel, pattern_b_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS,
                             PATTERN_B_INSTANCE);
        quadro2_ramht_insert(qts, channel, rop_a_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS, ROP_A_INSTANCE);
        quadro2_ramht_insert(qts, channel, rop_b_handle,
                             HP_QUADRO2_RAMHT_GRAPHICS, ROP_B_INSTANCE);
    }
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_RAMHT,
                 0x03000100);
    qtest_memset(qts, HP_QUADRO2_FB_BASE + DESTINATION_OFFSET, 0, PITCH);

    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned int channel = channels[i];

        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0,
                           surface_handle);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x184,
                           dma_handle);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x188,
                           dma_handle);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x300, 7);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x304,
                           (PITCH << 16) | PITCH);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x308,
                           DESTINATION_OFFSET);
        quadro2_user_write(qts, channel, SURFACE_SUBCHANNEL, 0x30c,
                           DESTINATION_OFFSET);
        quadro2_user_write(qts, channel, GDI_SUBCHANNEL, 0, gdi_handle);
        quadro2_user_write(qts, channel, GDI_SUBCHANNEL, 0x198,
                           surface_handle);
        quadro2_user_write(qts, channel, GDI_SUBCHANNEL, 0x2fc, 1);
        quadro2_user_write(qts, channel, GDI_SUBCHANNEL, 0x300, 3);
    }

    quadro2_user_write(qts, CHANNEL0, PATTERN_A_SUBCHANNEL, 0,
                       pattern_a_handle);
    quadro2_user_write(qts, CHANNEL0, PATTERN_A_SUBCHANNEL, 0x30c, 0);
    quadro2_user_write(qts, CHANNEL0, PATTERN_A_SUBCHANNEL, 0x314,
                       color_a);
    quadro2_user_write(qts, CHANNEL0, ROP_A_SUBCHANNEL, 0, rop_a_handle);
    quadro2_user_write(qts, CHANNEL0, ROP_A_SUBCHANNEL, 0x300, 0x0f);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x400, 0);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET), ==, ~color_a);

    /* The last method updates shared PGRAPH state, not an object snapshot. */
    quadro2_user_write(qts, CHANNEL0, PATTERN_B_SUBCHANNEL, 0,
                       pattern_b_handle);
    quadro2_user_write(qts, CHANNEL0, PATTERN_B_SUBCHANNEL, 0x30c, 0);
    quadro2_user_write(qts, CHANNEL0, PATTERN_B_SUBCHANNEL, 0x314,
                       color_b);
    quadro2_user_write(qts, CHANNEL0, ROP_B_SUBCHANNEL, 0, rop_b_handle);
    quadro2_user_write(qts, CHANNEL0, ROP_B_SUBCHANNEL, 0x300, 0xf0);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x188,
                       pattern_a_handle);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x18c,
                       rop_a_handle);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x400, 1U << 16);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 4), ==, color_b);

    /* Context switches preserve independent state for each FIFO channel. */
    quadro2_user_write(qts, CHANNEL1, PATTERN_A_SUBCHANNEL, 0,
                       pattern_a_handle);
    quadro2_user_write(qts, CHANNEL1, PATTERN_A_SUBCHANNEL, 0x30c, 0);
    quadro2_user_write(qts, CHANNEL1, PATTERN_A_SUBCHANNEL, 0x314,
                       color_channel1);
    quadro2_user_write(qts, CHANNEL1, ROP_A_SUBCHANNEL, 0, rop_a_handle);
    quadro2_user_write(qts, CHANNEL1, ROP_A_SUBCHANNEL, 0x300, 0x0f);
    quadro2_user_write(qts, CHANNEL1, GDI_SUBCHANNEL, 0x400, 2U << 16);
    quadro2_user_write(qts, CHANNEL1, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x400, 3U << 16);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 8), ==,
                    ~color_channel1);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 12), ==, color_b);

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x400, 4U << 16);
    quadro2_user_write(qts, CHANNEL0, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    quadro2_user_write(qts, CHANNEL1, GDI_SUBCHANNEL, 0x400, 5U << 16);
    quadro2_user_write(qts, CHANNEL1, GDI_SUBCHANNEL, 0x404,
                       (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 16), ==, color_b);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    DESTINATION_OFFSET + 20), ==,
                    ~color_channel1);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PGRAPH_INTR), ==, 0);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void nvidia_quadro2_state(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/nvidia-quadro2-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    const uint32_t destination_offset = 0x200000;
    const uint32_t color = 0x1234abcd;
    const uint32_t canary0 = 0x11111111;
    const uint32_t canary1 = 0x22222222;
    const uint32_t ref_marker = 0x51a7c0de;
    const uint32_t clip_instance = 0x19040;
    const uint32_t rectangle_instance = 0x19050;
    const uint32_t surface_handle = 0x80000002;
    const uint32_t clip_handle = 0x80000004;
    const uint32_t rectangle_handle = 0x80000005;
    const uint32_t clip_color = 0x89abcdef;
    const unsigned int channel = 7;
    const uint64_t ramin = HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRAMIN;
    QTestState *qts;
    int fd;

    qts = quadro2_start("");

    /* Split an unaligned access at the PCRTC/PRMCIO alias boundary. */
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRMCIO - 4,
                 0xa5b6c7d8);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PRMCIO - 2), ==, 0x0000a5b6);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PRMCIO - 2,
                 0x11223344);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PRMCIO - 4), ==, 0x3344c7d8);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_PRMCIO + 0xffe), ==, 0);

    qtest_writeb(qts, quadro2_sparse_io_address(0x3c2), 1);
    quadro2_ddc_start(qts);
    g_assert_true(quadro2_ddc_send(qts, 0xa0));
    qtest_system_reset(qts);
    qtest_writeb(qts, quadro2_sparse_io_address(0x3c2), 1);
    quadro2_ddc_start(qts);
    g_assert_false(quadro2_ddc_send(qts, 0xa2));
    quadro2_ddc_stop(qts);
    quadro2_ddc_start(qts);
    g_assert_true(quadro2_ddc_send(qts, 0xa0));
    quadro2_ddc_stop(qts);

    quadro2_prepare_rectangle(qts, 0, destination_offset + 3,
                              destination_offset, color);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + destination_offset, canary0);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + destination_offset + 4, canary1);
    quadro2_user_write(qts, 0, 1, 0x404, (2U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset), ==, canary0);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 4), ==, canary1);

    qtest_system_reset(qts);
    quadro2_prepare_rectangle(qts, channel, HP_QUADRO2_VRAM_SIZE - 1,
                              destination_offset, color);
    qtest_writel(qts, ramin + clip_instance, 0x19);
    qtest_writel(qts, ramin + rectangle_instance, 0x5e);
    quadro2_ramht_insert(qts, channel, clip_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, clip_instance);
    quadro2_ramht_insert(qts, channel, rectangle_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, rectangle_instance);
    quadro2_user_write(qts, channel, 2, 0, clip_handle);
    quadro2_user_write(qts, channel, 2, 0x300, UINT32_MAX);
    quadro2_user_write(qts, channel, 2, 0x304, (3U << 16) | 3U);
    quadro2_user_write(qts, channel, 3, 0, rectangle_handle);
    quadro2_user_write(qts, channel, 3, 0x184, clip_handle);
    quadro2_user_write(qts, channel, 3, 0x198, surface_handle);
    quadro2_user_write(qts, channel, 3, 0x2fc, 3);
    quadro2_user_write(qts, channel, 3, 0x300, 3);
    quadro2_user_write(qts, channel, 3, 0x304, clip_color);
    quadro2_user_write(qts, channel, 3, 0x400, UINT32_MAX);
    qtest_writel(qts, HP_QUADRO2_FB_BASE + destination_offset, canary0);
    quadro2_user_write(qts, channel, 0, 0x50, ref_marker);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_MODE,
                 1U << channel);
    qtest_writeb(qts, quadro2_sparse_io_address(0x3c2), 1);
    quadro2_ddc_start(qts);

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    display_wait_for_migration(qts);
    qtest_quit(qts);

    qts = quadro2_start("-incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    display_wait_for_migration(qts);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_MMIO_BASE +
                                    HP_QUADRO2_USER +
                                    channel * HP_QUADRO2_CHANNEL_SIZE +
                                    0x48), ==, ref_marker);
    qtest_writel(qts, HP_QUADRO2_MMIO_BASE + HP_QUADRO2_PFIFO_MODE, 0);
    quadro2_user_write(qts, channel, 1, 0x404, (1U << 16) | 1U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset), ==, color);

    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0, 512);
    quadro2_user_write(qts, channel, 3, 0x404, (3U << 16) | 3U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset), ==, clip_color);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 4), ==, clip_color);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 256), ==,
                    clip_color);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 260), ==,
                    clip_color);

    qtest_memset(qts, HP_QUADRO2_FB_BASE + destination_offset, 0, 768);
    quadro2_user_write(qts, channel, 2, 0x304, (4U << 16) | 4U);
    quadro2_user_write(qts, channel, 3, 0x408, 0);
    quadro2_user_write(qts, channel, 3, 0x40c, (4U << 16) | 4U);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 2 * 256 + 8), ==,
                    clip_color);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 256 + 4), ==,
                    clip_color);
    g_assert_cmphex(qtest_readl(qts, HP_QUADRO2_FB_BASE +
                                    destination_offset + 256 + 12), ==, 0);
    quadro2_ddc_stop(qts);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void nvidia_quadro2_pgraph_trap(void)
{
    enum {
        CHANNEL = 7,
        SUBCHANNEL = 5,
        DMA_INSTANCE = 0x1b000,
        OBJECT_INSTANCE = 0x1b010,
        PUSH_OFFSET = 0x380000,
        RAMFC_BASE = 0x18000,
        RAMFC_OFFSET = RAMFC_BASE + CHANNEL * 32,
    };
    const uint32_t object_handle = 0x82000001;
    const uint32_t trapped_data = 0x51a7c0de;
    const uint32_t ref_marker = 0x13579bdf;
    uint32_t push[] = {
        (1U << 18) | (SUBCHANNEL << 13), object_handle,
        (1U << 18) | (SUBCHANNEL << 13) | 0x110, trapped_data,
        (1U << 18) | 0x50, ref_marker,
    };
    const uint64_t mmio = HP_QUADRO2_MMIO_BASE;
    const uint64_t ramin = mmio + HP_QUADRO2_PRAMIN;
    QTestState *qts = quadro2_start("");
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(push); i++) {
        push[i] = cpu_to_le32(push[i]);
    }
    qtest_memset(qts, ramin + 0x10000, 0, 4096);
    qtest_memset(qts, ramin + RAMFC_BASE, 0, 32 * 32);
    qtest_writel(qts, ramin + DMA_INSTANCE, 0x0000303d);
    qtest_writel(qts, ramin + DMA_INSTANCE + 4, sizeof(push) - 1);
    qtest_writel(qts, ramin + DMA_INSTANCE + 8, PUSH_OFFSET);
    qtest_writel(qts, ramin + OBJECT_INSTANCE, 0x9f);
    quadro2_ramht_insert(qts, CHANNEL, object_handle,
                         HP_QUADRO2_RAMHT_GRAPHICS, OBJECT_INSTANCE);
    qtest_memwrite(qts, HP_QUADRO2_FB_BASE + PUSH_OFFSET,
                   push, sizeof(push));

    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_RAMHT, 0x03000100);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_RAMFC, RAMFC_BASE >> 8);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_MODE, 1U << CHANNEL);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_PUSH1,
                 CHANNEL | (1U << 8));
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_DMA_INSTANCE,
                 DMA_INSTANCE >> 4);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_DMA_PUSH, 1);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_DMA_GET, 0);
    qtest_writel(qts, mmio + HP_QUADRO2_PFIFO_DMA_PUT, sizeof(push));

    /* NV15 records the faulting method and halts before the next packet. */
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==,
                    1U << 20);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSTATUS), ==,
                    1U << 26);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    1U << 6);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_ADDR), ==,
                    (CHANNEL << 20) | (SUBCHANNEL << 16) | 0x110);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_DATA), ==,
                    trapped_data);
    g_assert_cmphex(qtest_readl(
        qts, mmio + HP_QUADRO2_PGRAPH_TRAPPED_DATA_HIGH), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS), ==,
                    0);
    g_assert_cmpuint(qtest_readl(qts, mmio + HP_QUADRO2_PFIFO_DMA_GET), ==,
                     4 * sizeof(uint32_t));
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PFIFO_REF_CNT), ==,
                    0);
    g_assert_cmpuint(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x00), ==,
                     sizeof(push));
    g_assert_cmpuint(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x04), ==,
                     4 * sizeof(uint32_t));
    g_assert_cmphex(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x08), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x0c), ==,
                    (2U << 16) | (DMA_INSTANCE >> 4));

    /* PIO methods cannot replace the latched trap while PGRAPH is stopped. */
    quadro2_user_write(qts, CHANNEL, SUBCHANNEL, 0x110, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_ADDR), ==,
                    (CHANNEL << 20) | (SUBCHANNEL << 16) | 0x110);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_TRAPPED_DATA), ==,
                    trapped_data);

    /* The native interrupt recovery sequence clears ERROR, then resumes. */
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_INTR, 1U << 20);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSTATUS), ==,
                    1U << 26);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_NSOURCE), ==,
                    0);
    qtest_writel(qts, mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS, 1);
    g_assert_cmpuint(qtest_readl(qts, mmio + HP_QUADRO2_PFIFO_DMA_GET), ==,
                     sizeof(push));
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PFIFO_REF_CNT), ==,
                    ref_marker);
    g_assert_cmpuint(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x04), ==,
                     sizeof(push));
    g_assert_cmphex(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x08), ==,
                    ref_marker);
    g_assert_cmphex(qtest_readl(qts, ramin + RAMFC_OFFSET + 0x0c), ==,
                    (3U << 16) | (DMA_INSTANCE >> 4));
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts,
                                mmio + HP_QUADRO2_PGRAPH_FIFO_ACCESS), ==,
                    1);
    g_assert_cmphex(qtest_readl(qts, mmio + HP_QUADRO2_PGRAPH_INTR), ==, 0);
    qtest_quit(qts);
}

static void ati_radeon_cp_set_scissors(void)
{
    static const char * const models[] = { "rv100", "es1000" };
    enum { RING_OFFSET = 0x1000, DST_OFFSET = 0x20000, PITCH = 64 };
    const uint32_t marker = 0x12345678;
    const uint32_t top_left = (1U << 16) | 2;
    const uint32_t bottom_right = (3U << 16) | 6;

    for (unsigned int model = 0; model < ARRAY_SIZE(models); model++) {
        g_autofree char *args = g_strdup_printf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", models[model]);
        QTestState *qts = qtest_init(args);
        uint32_t ring[] = {
            R100_CP_PACKET3 | (1U << 16) | (0x1eU << 8),
            top_left, bottom_right,
            R100_SCRATCH_REG0 >> 2, marker,
        };
        uint32_t invalid[] = {
            R100_CP_PACKET3 | (0x1eU << 8), 0,
            R100_SCRATCH_REG0 >> 2, 0,
        };
        uint64_t mmio = IA64_RV100_MMIO_BASE;

        ati_pci_enable(qts);
        qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, 0xa5, PITCH * 4);
        qtest_writel(qts, mmio + ATI_DP_GUI_MASTER_CNTL,
                     ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                     ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                     ATI_GMC_BRUSH_SOLID_LINE | ATI_GMC_DST_32BPP |
                     ATI_GMC_ROP3_PATCOPY);
        qtest_writel(qts, mmio + ATI_DST_PITCH_OFFSET,
                     ((PITCH / 64) << 22) | (DST_OFFSET >> 10));
        qtest_writel(qts, mmio + ATI_DP_BRUSH_FRGD_CLR, 0x00123456);
        qtest_writel(qts, mmio + ATI_DP_CNTL, ATI_DST_LTR_TTB);
        for (unsigned int i = 0; i < ARRAY_SIZE(ring); i++) {
            ring[i] = cpu_to_le32(ring[i]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                       ring, sizeof(ring));
        qtest_writel(qts, mmio + R100_CP_RB_BASE, RING_OFFSET);
        qtest_writel(qts, mmio + R100_CP_RB_CNTL, 4);
        qtest_writel(qts, mmio + R100_CP_CSQ_CNTL, R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, mmio + R100_CP_RB_WPTR, ARRAY_SIZE(ring));
        qtest_writel(qts, mmio + ATI_DST_X, 0);
        qtest_writel(qts, mmio + ATI_DST_Y, 0);
        qtest_writel(qts, mmio + ATI_DST_HEIGHT, 4);
        qtest_writel(qts, mmio + ATI_DST_WIDTH, 8);
        for (unsigned int y = 0; y < 4; y++) {
            for (unsigned int x = 0; x < 8; x++) {
                uint32_t expected = y >= 1 && y < 3 && x >= 2 && x < 6 ?
                                    0x00123456 : 0xa5a5a5a5;

                g_assert_cmphex(qtest_readl(qts, IA64_RV100_FB_BASE +
                                             DST_OFFSET + y * PITCH + x * 4),
                                ==, expected);
            }
        }
        g_assert_cmphex(qtest_readl(qts, mmio + R100_SCRATCH_REG0), ==,
                        marker);

        /* A missing bottom-right word must preserve both scissors. */
        qtest_writel(qts, mmio + R100_CP_CSQ_CNTL, 0);
        for (unsigned int i = 0; i < ARRAY_SIZE(invalid); i++) {
            invalid[i] = cpu_to_le32(invalid[i]);
        }
        qtest_memwrite(qts, IA64_RV100_FB_BASE + RING_OFFSET,
                       invalid, sizeof(invalid));
        qtest_writel(qts, mmio + R100_CP_RB_RPTR_WR, 0);
        qtest_writel(qts, mmio + R100_CP_RB_WPTR, 0);
        qtest_writel(qts, mmio + R100_CP_CSQ_CNTL, R100_CSQ_PRIBM_INDDIS);
        qtest_writel(qts, mmio + R100_CP_RB_WPTR, ARRAY_SIZE(invalid));
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_SC_LEFT), ==, 2);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_SC_RIGHT), ==, 6);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_SC_TOP), ==, 1);
        g_assert_cmphex(qtest_readl(qts, mmio + ATI_SC_BOTTOM), ==, 3);
        g_assert_cmphex(qtest_readl(qts, mmio + R100_SCRATCH_REG0), ==,
                        marker);
        qtest_quit(qts);
    }
}

static void ati_cp_palette_submit(QTestState *qts, const uint32_t *words,
                                  unsigned int count)
{
    g_autofree uint32_t *encoded = g_new(uint32_t, count);

    for (unsigned int i = 0; i < count; i++) {
        encoded[i] = cpu_to_le32(words[i]);
    }
    qtest_memwrite(qts, IA64_RV100_FB_BASE + 0x10000,
                   encoded, count * sizeof(*encoded));
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BASE, 0x10000);
    qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_IB_BUFSZ, count);
}

static void ati_radeon_cp_palette(void)
{
    enum { DST_OFFSET = 0x20000, PITCH = 64 };
    static const uint8_t indices8[] = {
        0, 1, 15, 128, 255, 3, 4, 5, 6, 7, 8, 9,
    };
    static const uint8_t indices16[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0,
    };
    static const char * const models[] = { "rv100", "es1000" };

    for (unsigned int mode = 0; mode < ARRAY_SIZE(models); mode++) {
        const uint8_t *indices = mode ? indices16 : indices8;
        unsigned int width = mode ? 8 : 6;
        unsigned int entries = mode ? 16 : 256;
        unsigned int cpp = mode ? 2 : 4;
        unsigned int raster_dwords = mode ? 4 : 3;
        uint32_t palette[256];
        uint32_t load[260];
        uint32_t blit[16] = { 0 };
        uint32_t invalid[] = {
            R100_CP_PACKET3 | (1U << 16) | (0x2cU << 8), 1, 0,
            R100_SCRATCH_REG0 >> 2, 0,
        };
        uint32_t marker = 0x12345678;
        uint32_t gui = ATI_GMC_WR_MSK_DIS | ATI_GMC_CLR_CMP_DIS |
                       ATI_GMC_DST_PITCH | ATI_GMC_DST_CLIPPING |
                       ATI_GMC_BRUSH_NONE | ATI_GMC_SRC_MONO_FG_LA |
                       ATI_GMC_ROP3_SRCCOPY | ATI_GMC_DP_SRC_HOST_BYTEALIGN |
                       (1U << 27) |
                       (mode ? ATI_GMC_DST_16BPP : ATI_GMC_DST_32BPP);
        g_autofree char *path = g_build_filename(
            g_get_tmp_dir(), "ati-cp-palette-XXXXXX", NULL);
        g_autofree char *uri = NULL;
        QTestState *qts = qtest_initf(
            "-machine ia64-vpc,nvram=none -m 256M -S "
            "-vga ati -global ati-vga.model=%s", models[mode]);
        unsigned int word = 0;
        int fd;

        ati_pci_enable(qts);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_CP_CSQ_CNTL,
                     R100_CSQ_PRIBM_INDBM);
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_CNTL,
                     ATI_DST_LTR_TTB);
        load[word++] = R100_CP_PACKET3 | (entries << 16) | (0x2cU << 8);
        load[word++] = mode ? 1 : 2;
        for (unsigned int i = 0; i < entries; i++) {
            palette[i] = mode ? (0x1357U ^ (i * 0x111U)) :
                                (0x00305070U ^ (i * 0x00010307U));
            load[word++] = palette[i];
        }
        load[word++] = R100_SCRATCH_REG0 >> 2;
        load[word++] = marker;
        ati_cp_palette_submit(qts, load, word);
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_SCRATCH_REG0), ==, marker);

        /* Selecting a non-indexed source must preserve the loaded CLUT. */
        qtest_writel(qts, IA64_RV100_MMIO_BASE + ATI_DP_GUI_MASTER_CNTL,
                     gui & ~(1U << 27));

        /* Reject an incomplete replacement without destroying the palette. */
        ati_cp_palette_submit(qts, invalid, ARRAY_SIZE(invalid));
        g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                     R100_SCRATCH_REG0), ==, marker);

        word = 0;
        blit[word++] = R100_CP_PACKET3 | ((8 + raster_dwords) << 16) |
                       (R100_PACKET3_CNTL_HOSTDATA_BLT << 8);
        blit[word++] = gui;
        blit[word++] = ((PITCH / 64) << 22) | (DST_OFFSET >> 10);
        blit[word++] = (1U << 16) | 2;
        blit[word++] = (3U << 16) | 5;
        blit[word++] = 0;
        blit[word++] = 0;
        blit[word++] = (1U << 16) | 1;
        blit[word++] = (2U << 16) | width;
        blit[word++] = raster_dwords;
        for (unsigned int i = 0; i < width * 2; i++) {
            blit[word + i / 4] |= (uint32_t)indices[i] << ((i % 4) * 8);
        }
        word += raster_dwords;
        blit[word++] = R100_SCRATCH_REG0 >> 2;
        blit[word++] = marker + 1;

        for (unsigned int pass = 0; pass < 2; pass++) {
            if (pass) {
                /* Repeat the indexed upload after migration. */
                fd = g_mkstemp(path);
                g_assert_cmpint(fd, >=, 0);
                close(fd);
                uri = g_strdup_printf("file:%s", path);
                qtest_qmp_assert_success(
                    qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
                display_wait_for_migration(qts);
                qtest_quit(qts);
                qts = qtest_initf(
                    "-machine ia64-vpc,nvram=none -m 256M -S "
                    "-vga ati -global ati-vga.model=%s -incoming defer",
                    models[mode]);
                qtest_qmp_assert_success(
                    qts, "{'execute':'migrate-incoming','arguments':"
                         "{'uri':%s,'exit-on-error':false}}", uri);
                display_wait_for_migration(qts);
            }
            qtest_memset(qts, IA64_RV100_FB_BASE + DST_OFFSET, 0xa5,
                         PITCH * 4);
            qtest_writel(qts, IA64_RV100_MMIO_BASE + R100_SCRATCH_REG0, marker);
            /* Reserved and other conversion types cannot use byte indices. */
            for (unsigned int type = 4; type <= 7; type++) {
                uint32_t previous_gui;
                uint8_t pixels[PITCH * 4];

                if (type == 5) {
                    continue;
                }
                previous_gui = qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                               ATI_DP_GUI_MASTER_CNTL);
                blit[1] = (gui & ~(3U << 12)) | ((type & 3) << 12);
                ati_cp_palette_submit(qts, blit, word);
                g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                                 R100_SCRATCH_REG0), ==,
                                marker);
                g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                                 ATI_DP_GUI_MASTER_CNTL), ==,
                                previous_gui);
                qtest_memread(qts, IA64_RV100_FB_BASE + DST_OFFSET,
                              pixels, sizeof(pixels));
                for (unsigned int i = 0; i < sizeof(pixels); i++) {
                    g_assert_cmphex(pixels[i], ==, 0xa5);
                }
            }
            blit[1] = gui;
            ati_cp_palette_submit(qts, blit, word);
            g_assert_cmphex(qtest_readl(qts, IA64_RV100_MMIO_BASE +
                                         R100_SCRATCH_REG0), ==, marker + 1);
            for (unsigned int y = 0; y < 4; y++) {
                for (unsigned int x = 0; x < PITCH / cpp; x++) {
                    uint32_t expected = mode ? 0xa5a5 : 0xa5a5a5a5;
                    uint64_t address = IA64_RV100_FB_BASE + DST_OFFSET +
                                       y * PITCH + x * cpp;
                    uint32_t actual = mode ? qtest_readw(qts, address) :
                                             qtest_readl(qts, address);

                    if (y >= 1 && y < 3 && x >= 2 && x < 5) {
                        expected = palette[indices[(y - 1) * width + x - 1]];
                    }
                    g_assert_cmphex(actual, ==, expected);
                }
            }
        }
        qtest_quit(qts);
        g_assert_cmpint(g_unlink(path), ==, 0);
    }
}

int main(int argc, char **argv)
{
    static const char *devices[] = {
        "cirrus-vga",
        "nvidia-quadro2",
        "VGA",
        "secondary-vga",
        "virtio-gpu-pci",
        "virtio-vga"
    };

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(devices); i++) {
        if (qtest_has_device(devices[i])) {
            char *testpath = g_strdup_printf("/display/pci/%s", devices[i]);
            qtest_add_data_func(testpath, devices[i], test_vga);
            g_free(testpath);
        }
    }

    if (qtest_has_device("secondary-vga")) {
        qtest_add_func("/display/pci/multihead", pci_multihead);
    }
    if (qtest_has_device("VGA")) {
        qtest_add_func("/display/pci/vbe-legacy-data-port",
                       vbe_legacy_data_port);
    }
    if (g_str_equal(qtest_get_arch(), "ia64") &&
        qtest_has_device("ati-vga")) {
        qtest_add_func("/display/pci/ati-es1000-realize",
                       ati_es1000_realize);
        qtest_add_func("/display/pci/ati-radeon-cp-set-scissors",
                       ati_radeon_cp_set_scissors);
        qtest_add_func("/display/pci/ati-radeon-cp-palette",
                       ati_radeon_cp_palette);
        qtest_add_func("/display/pci/ati-rv100-mm-aper",
                       ati_rv100_mm_aper);
        qtest_add_func("/display/pci/ati-palette-access", ati_palette_access);
        qtest_add_func("/display/pci/ati-crtc-live-mode", ati_crtc_live_mode);
        qtest_add_func("/display/pci/ati-radeon-dac-detect",
                       ati_radeon_dac_detect);
        qtest_add_func("/display/pci/ati-source-datatype-alias",
                       ati_source_datatype_alias);
        qtest_add_func("/display/pci/ati-es1000-crtc-2d",
                       ati_es1000_crtc_2d);
        qtest_add_func("/display/pci/ati-rage128-host-data-migration",
                       ati_rage128_host_data_migration);
        qtest_add_func("/display/pci/ati-rage128-host-data",
                       ati_rage128_host_data);
        qtest_add_func("/display/pci/ati-rage128-vsync",
                       ati_rage128_vsync);
        qtest_add_func("/display/pci/ati-crtc-timing-migration",
                       ati_crtc_timing_migration);
        qtest_add_func("/display/pci/ati-8x8-pattern-brush",
                       ati_8x8_pattern_brush);
        qtest_add_func("/display/pci/ati-stride", ati_stride);
        qtest_add_func("/display/pci/ati-blit-visible-intersection",
                       ati_blit_visible_intersection);
        qtest_add_func("/display/pci/ati-reverse-overlap-blit",
                       ati_reverse_overlap_blit);
        qtest_add_func("/display/pci/ati-source-scissor",
                       ati_source_scissor);
        qtest_add_func("/display/pci/ati-crtc-page-flip",
                       ati_crtc_page_flip);
        qtest_add_func("/display/pci/ati-crtc-offset-control",
                       ati_crtc_offset_control);
        qtest_add_func("/display/pci/ati-radeon-hwcursor",
                       ati_radeon_hwcursor);
        qtest_add_func("/display/pci/ati-radeon-cursor-position",
                       ati_radeon_cursor_position);
        qtest_add_func("/display/pci/ati-rage128-mono-hwcursor",
                       ati_rage128_mono_hwcursor);
        qtest_add_func("/display/pci/ati-rv100-3d-ring",
                       ati_rv100_3d_ring);
        qtest_add_func("/display/pci/ati-rv100-cp-hostdata-blt",
                       ati_rv100_cp_hostdata_blt);
        qtest_add_func("/display/pci/ati-rv100-cp-bitblt-multi",
                       ati_rv100_cp_bitblt_multi);
        qtest_add_func("/display/pci/ati-rv100-cp-paint-multi",
                       ati_rv100_cp_paint_multi);
        qtest_add_func("/display/pci/ati-rv100-cp-nextchar",
                       ati_rv100_cp_nextchar);
        qtest_add_func("/display/pci/ati-rv100-cp-polyscanlines",
                       ati_rv100_cp_polyscanlines);
        qtest_add_func("/display/pci/ati-rv100-cp-legacy-bitblt",
                       ati_rv100_cp_legacy_bitblt);
        qtest_add_func("/display/pci/ati-rv100-command-budget",
                       ati_rv100_command_budget);
        qtest_add_func("/display/pci/ati-rv100-vertex-fog",
                       ati_rv100_vertex_fog);
        qtest_add_func("/display/pci/ati-rv100-endian-scratch-rop",
                       ati_rv100_endian_scratch_rop);
        qtest_add_func("/display/pci/ati-rv100-gart", ati_rv100_gart);
        qtest_add_func("/display/pci/ati-radeon-2d-gart",
                       ati_radeon_2d_gart);
        qtest_add_func("/display/pci/ati-radeon-2d-live-migration",
                       ati_radeon_2d_live_migration);
        qtest_add_func("/display/pci/ati-rv100-3d-live-migration",
                       ati_rv100_3d_live_migration);
        qtest_add_func("/display/pci/ati-rv100-fixed-function",
                       ati_rv100_fixed_function);
        qtest_add_func("/display/pci/ati-rv100-texture-mipmaps",
                       ati_rv100_texture_mipmaps);
        qtest_add_func("/display/pci/ati-rv100-compressed-textures",
                       ati_rv100_compressed_textures);
        qtest_add_func("/display/pci/ati-rv100-yuv-textures",
                       ati_rv100_yuv_textures);
    }
    if (g_str_equal(qtest_get_arch(), "ia64") &&
        qtest_has_machine("hp-i2000") &&
        qtest_has_device("nvidia-quadro2")) {
        qtest_add_func("/display/pci/nvidia-quadro2-nv4-rectangle",
                       nvidia_quadro2_nv4_rectangle);
        qtest_add_func("/display/pci/nvidia-quadro2-scaled-yuv",
                       nvidia_quadro2_scaled_yuv);
        qtest_add_func("/display/pci/nvidia-quadro2-m2mf-notify",
                       nvidia_quadro2_m2mf_notify);
        qtest_add_func("/display/pci/nvidia-quadro2-notify-pending",
                       nvidia_quadro2_notify_pending);
        qtest_add_func("/display/pci/nvidia-quadro2-nvidiafb-blit",
                       nvidia_quadro2_nvidiafb_blit);
        qtest_add_func("/display/pci/nvidia-quadro2-shared-context",
                       nvidia_quadro2_shared_context);
        qtest_add_func("/display/pci/nvidia-quadro2-hwcursor",
                       nvidia_quadro2_hwcursor);
        qtest_add_func("/display/pci/nvidia-quadro2-state",
                       nvidia_quadro2_state);
        qtest_add_func("/display/pci/nvidia-quadro2-ptimer-alarm",
                       nvidia_quadro2_ptimer_alarm);
        qtest_add_func("/display/pci/nvidia-quadro2-pgraph-trap",
                       nvidia_quadro2_pgraph_trap);
    }
    if (g_str_equal(qtest_get_arch(), "ia64") &&
        qtest_has_device("VGA")) {
        qtest_add_func("/display/pci/vga-wide-planar-access",
                       vga_wide_planar_access);
    }

    return g_test_run();
}
