/*
 * HP IA-64 legacy INT 10h/VBE bridge
 *
 * Implements a post-ExitBootServices INT 10h subset.  X86 option-ROM
 * execution is not implemented; the real-mode stub forwards calls to QEMU C
 * code through a dedicated PCI I/O window.
 * Technical references are listed in docs/devel/gpu-emulation-provenance.rst.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/cpu-common.h"
#include "hw/display/ati_int.h"
#include "hw/display/ati_regs.h"
#include "hw/display/bochs-vbe.h"
#include "hw/display/edid.h"
#include "hw/display/vga_regs.h"
#include "hw/ia64/hp_int10.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "system/address-spaces.h"

#define HP_INT10_ROM_BASE             0x000c0000U
#define HP_INT10_ROM_SIZE             0x00000800U
#define HP_INT10_ROM_PCIR_OFFSET      0x0020U
#define HP_INT10_ROM_ATI_SIGNATURE    0x0074U
#define HP_INT10_ROM_ATI_HEADER       0x0080U
#define HP_INT10_ROM_ATI_HEADER_SIZE  0x0060U
#define HP_INT10_ROM_ATI_RAGE128_HEADER_SIZE 0x004aU
#define HP_INT10_ROM_ATI_INIT         0x00e0U
#define HP_INT10_ROM_ATI_INIT_READ    10U
#define HP_INT10_ROM_ATI_BIOS_SUPPORT 0x00f0U
#define HP_INT10_ROM_ATI_BIOS_SUPPORT_SIZE 12U
#define HP_INT10_ROM_ATI_RAGE128_MISC 0x00f0U
#define HP_INT10_ROM_ATI_RAGE128_MISC_SIZE 15U
#define HP_INT10_ROM_ATI_MISC         0x00fcU
#define HP_INT10_ROM_ATI_MISC_SIZE    2U
#define HP_INT10_ROM_HANDLER          0x0100U
#define HP_INT10_ROM_OEM              0x0180U
#define HP_INT10_ROM_VENDOR           0x0190U
#define HP_INT10_ROM_PRODUCT          0x01a0U
#define HP_INT10_ROM_REVISION         0x01c0U
#define HP_INT10_ROM_MODES            0x01d0U
#define HP_INT10_ROM_ATI_CONNECTOR    0x02e0U
#define HP_INT10_ROM_ATI_CONNECTOR_SIZE 6U
#define HP_INT10_ROM_ATI_RAGE128_CRT  0x02e0U
#define HP_INT10_ROM_ATI_RAGE128_CRT_SIZE 30U
#define HP_INT10_ROM_ATI_PLL          0x0300U
#define HP_INT10_ROM_ATI_PLL_READ_SIZE 0x006eU
#define HP_INT10_ROM_ATI_MEM_CONFIG   0x0383U
#define HP_INT10_ROM_ATI_MEM_PREFIX_SIZE 3U
#define HP_INT10_ROM_ATI_MEM_RESET_OFFSET 3U
#define HP_INT10_ROM_ATI_MEM_RESET_SIZE 100U
#define HP_INT10_ROM_NVIDIA_BMP       0x0600U
#define HP_INT10_VECTOR_ADDR          (0x10U * 4U)

#define HP_INT10_IO_BASE              0x000001e0U
#define HP_INT10_IO_SIZE              0x00000010U
#define HP_INT10_TRIGGER              0x4941U

#define HP_INT10_VBE2_SIGNATURE       0x32454256U
#define HP_INT10_VBE_INDEX            0x01ceU
#define HP_INT10_VBE_DATA             0x01d0U
#define HP_INT10_DEFAULT_XRES         1280U
#define HP_INT10_DEFAULT_YRES         1024U
#define HP_INT10_NATIVE_MODE_16       0x1f0U
#define HP_INT10_NATIVE_MODE_24       0x1f1U
#define HP_INT10_NATIVE_MODE_32       0x1f2U

#define HP_INT10_PLANAR_MEMORY_SIZE   (256 * KiB)
#define HP_INT10_BDA_VIDEO_MODE       0x00000449U
#define HP_INT10_BDA_VIDEO_COLUMNS    0x0000044aU
#define HP_INT10_BDA_VIDEO_PAGE_SIZE  0x0000044cU
#define HP_INT10_BDA_VIDEO_PAGE_START 0x0000044eU
#define HP_INT10_BDA_CURSOR_POSITIONS 0x00000450U
#define HP_INT10_BDA_CURSOR_TYPE      0x00000460U
#define HP_INT10_BDA_VIDEO_PAGE       0x00000462U
#define HP_INT10_BDA_CRTC_ADDRESS     0x00000463U
#define HP_INT10_BDA_VIDEO_ROWS       0x00000484U
#define HP_INT10_BDA_CHARACTER_HEIGHT 0x00000485U
#define HP_INT10_BDA_VIDEO_CONTROL    0x00000487U
#define HP_INT10_BDA_VIDEO_SWITCHES   0x00000488U

#define HP_INT10_ATI_VENDOR_ID        0x1002U
#define HP_INT10_ATI_RAGE128_DEVICE_ID 0x5046U
#define HP_INT10_ATI_ES1000_DEVICE_ID 0x515eU
#define HP_INT10_ATI_RV100_DEVICE_ID  0x5159U
#define HP_INT10_ATI_CLOCK_REF        2700U
#define HP_INT10_ATI_CLOCK_REF_DIV    27U

#define HP_INT10_NVIDIA_VENDOR_ID     0x10deU
#define HP_INT10_NVIDIA_BMP_MAJOR     0x05U
#define HP_INT10_NVIDIA_BMP_MINOR     0x06U
#define HP_INT10_NVIDIA_PLL_MIN_KHZ   128000U
#define HP_INT10_NVIDIA_PLL_MAX_KHZ   350000U

enum {
    HP_INT10_REG_AX,
    HP_INT10_REG_BX,
    HP_INT10_REG_CX,
    HP_INT10_REG_DX,
    HP_INT10_REG_DI,
    HP_INT10_REG_ES,
    HP_INT10_REG_EXEC,
    HP_INT10_REG_DATA,
};

typedef struct HPIA64VbeResolution {
    uint16_t base_number;
    uint16_t width;
    uint16_t height;
} HPIA64VbeResolution;

typedef struct HPIA64VgaLegacyMode {
    uint8_t number;
    uint8_t columns;
    uint8_t rows;
    uint8_t character_height;
    uint16_t page_size;
    uint8_t misc;
    const uint8_t *sequencer;
    const uint8_t *crtc;
    const uint8_t *attribute;
    const uint8_t *graphics;
} HPIA64VgaLegacyMode;

static const HPIA64VbeMode hp_int10_legacy_modes[] = {
    { 0x111,  640,  480, 16 },
    { 0x112,  640,  480, 24 },
    { 0x114,  800,  600, 16 },
    { 0x115,  800,  600, 24 },
    { 0x117, 1024,  768, 16 },
    { 0x118, 1024,  768, 24 },
    { 0x11a, 1280, 1024, 16 },
    { 0x11b, 1280, 1024, 24 },
    { 0x141,  640,  400, 32 },
    { 0x142,  640,  480, 32 },
    { 0x143,  800,  600, 32 },
    { 0x144, 1024,  768, 32 },
    { 0x145, 1280, 1024, 32 },
};

static const HPIA64VbeResolution hp_int10_oem_resolutions[] = {
    { 0x146, 1152,  864 },
    { 0x149, 1280,  720 },
    { 0x14c, 1280,  768 },
    { 0x14f, 1280,  800 },
    { 0x152, 1280,  960 },
    { 0x155, 1360,  768 },
    { 0x158, 1400, 1050 },
    { 0x15b, 1440,  900 },
    { 0x15e, 1600,  900 },
    { 0x161, 1600, 1200 },
    { 0x164, 1680, 1050 },
    { 0x167, 1920, 1080 },
    { 0x16a, 1920, 1200 },
    { 0x16d, 2048, 1152 },
    { 0x170, 2048, 1536 },
    { 0x173, 2560, 1080 },
    { 0x176, 2560, 1440 },
    { 0x179, 2560, 1600 },
    { 0x17c, 3440, 1440 },
    { 0x17f, 3840, 2160 },
    { 0x182, 4096, 2160 },
    { 0x185, 5120, 2880 },
};

static const uint8_t hp_int10_mode_12_sequencer[] = {
    0x01, 0x0f, 0x00, 0x06,
};

static const uint8_t hp_int10_mode_12_crtc[] = {
    0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
    0xff,
};

static const uint8_t hp_int10_mode_12_attribute[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x01, 0x00, 0x0f, 0x00, 0x00,
};

static const uint8_t hp_int10_mode_12_graphics[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f,
    0xff,
};

static const HPIA64VgaLegacyMode hp_int10_vga_modes[] = {
    {
        .number = 0x12,
        .columns = 80,
        .rows = 30,
        .character_height = 16,
        .page_size = 0xa000,
        .misc = 0xe3,
        .sequencer = hp_int10_mode_12_sequencer,
        .crtc = hp_int10_mode_12_crtc,
        .attribute = hp_int10_mode_12_attribute,
        .graphics = hp_int10_mode_12_graphics,
    },
};

static const char hp_int10_oem[] = "QEMU HP IA64";
static const char hp_int10_vendor[] = "QEMU";
static const char hp_int10_product[] = "HP IA64 VGA VBE bridge";
static const char hp_int10_revision[] = "1.0";

/*
 * 16-bit INT 10h entry.  AX/BX/CX/DX/DI/ES are marshalled through ports
 * 1e0h..1ebh; 4941h written to 1ech executes the request.  Response words
 * are returned through 1eeh.  Keep this in sync with HP_INT10_IO_BASE.
 */
static const uint8_t hp_int10_handler[] = {
    0x55, 0x89, 0xe5, 0x50, 0x52, 0xba, 0xe0, 0x01,
    0xef, 0x83, 0xc2, 0x02, 0x89, 0xd8, 0xef, 0x83,
    0xc2, 0x02, 0x89, 0xc8, 0xef, 0x83, 0xc2, 0x02,
    0x8b, 0x46, 0xfc, 0xef, 0x83, 0xc2, 0x02, 0x89,
    0xf8, 0xef, 0x83, 0xc2, 0x02, 0x8c, 0xc0, 0xef,
    0x83, 0xc2, 0x02, 0x81, 0x7e, 0xfe, 0x00, 0x4f,
    0x75, 0x0f, 0x83, 0xc2, 0x02, 0x26, 0x8b, 0x05,
    0xef, 0x26, 0x8b, 0x45, 0x02, 0xef, 0x83, 0xea,
    0x02, 0xb8, 0x41, 0x49, 0xef, 0xed, 0x89, 0xc1,
    0xe3, 0x0a, 0x57, 0x83, 0xc2, 0x02, 0xfc, 0xed,
    0xab, 0xe2, 0xfc, 0x5f, 0xba, 0xe0, 0x01, 0xed,
    0x89, 0x46, 0xfe, 0x83, 0xc2, 0x02, 0xed, 0x89,
    0xc3, 0x83, 0xc2, 0x02, 0xed, 0x89, 0xc1, 0x83,
    0xc2, 0x02, 0xed, 0x89, 0xc2, 0x8b, 0x46, 0xfe,
    0x89, 0xec, 0x5d, 0xcf,
};

/* Option-ROM initialization entry: install C000:0100 as vector 10h. */
static const uint8_t hp_int10_rom_init[] = {
    0x50, 0x1e, 0x31, 0xc0, 0x8e, 0xd8, 0xc7, 0x06,
    0x40, 0x00, 0x00, 0x01, 0xc7, 0x06, 0x42, 0x00,
    0x00, 0xc0, 0x1f, 0x58, 0xcb,
};

static const HPIA64VbeMode *hp_int10_find_mode(HPIA64Int10 *s,
                                                uint16_t number)
{
    size_t i;

    for (i = 0; i < s->mode_count; i++) {
        if (s->modes[i].number == number) {
            return &s->modes[i];
        }
    }
    return NULL;
}

static const HPIA64VgaLegacyMode *hp_int10_find_legacy_mode(uint8_t number)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(hp_int10_vga_modes); i++) {
        if (hp_int10_vga_modes[i].number == number) {
            return &hp_int10_vga_modes[i];
        }
    }
    return NULL;
}

static void hp_int10_vbe_write(HPIA64Int10 *s, uint16_t index,
                               uint16_t value)
{
    address_space_stw_le(s->vga_io, HP_INT10_VBE_INDEX, index,
                         MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(s->vga_io, HP_INT10_VBE_DATA, value,
                         MEMTXATTRS_UNSPECIFIED, NULL);
}

static uint16_t hp_int10_vbe_read(HPIA64Int10 *s, uint16_t index)
{
    address_space_stw_le(s->vga_io, HP_INT10_VBE_INDEX, index,
                         MEMTXATTRS_UNSPECIFIED, NULL);
    return address_space_lduw_le(s->vga_io, HP_INT10_VBE_DATA,
                                 MEMTXATTRS_UNSPECIFIED, NULL);
}

static void hp_int10_vga_writeb(HPIA64Int10 *s, uint16_t port,
                                uint8_t value)
{
    address_space_stb(s->vga_io, port, value, MEMTXATTRS_UNSPECIFIED, NULL);
}

static uint8_t hp_int10_vga_readb(HPIA64Int10 *s, uint16_t port)
{
    return address_space_ldub(s->vga_io, port, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void hp_int10_vga_indexed_write(HPIA64Int10 *s,
                                       uint16_t index_port,
                                       uint16_t data_port,
                                       uint8_t index, uint8_t value)
{
    hp_int10_vga_writeb(s, index_port, index);
    hp_int10_vga_writeb(s, data_port, value);
}

static uint32_t hp_int10_vbe_memory_size(HPIA64Int10 *s)
{
    return (uint32_t)hp_int10_vbe_read(
        s, VBE_DISPI_INDEX_VIDEO_MEMORY_64K) * (64 * KiB);
}

static uint64_t hp_int10_mode_size(const HPIA64VbeMode *mode)
{
    return (uint64_t)mode->width * mode->height *
           DIV_ROUND_UP(mode->bpp, 8);
}

static bool hp_int10_has_geometry(HPIA64Int10 *s,
                                  const HPIA64VbeMode *candidate)
{
    size_t i;

    for (i = 0; i < s->mode_count; i++) {
        const HPIA64VbeMode *mode = &s->modes[i];

        if (mode->width == candidate->width &&
            mode->height == candidate->height &&
            mode->bpp == candidate->bpp) {
            return true;
        }
    }
    return false;
}

static void hp_int10_add_mode(HPIA64Int10 *s,
                              const HPIA64VbeMode *mode)
{
    g_assert(s->mode_count < HP_IA64_INT10_MAX_MODES);
    s->modes[s->mode_count++] = *mode;
}

static void hp_int10_add_filtered_modes(HPIA64Int10 *s,
                                        const HPIA64VbeMode *modes,
                                        size_t count, uint32_t memory_size)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const HPIA64VbeMode *mode = &modes[i];

        if (mode->width <= s->maxx && mode->height <= s->maxy &&
            hp_int10_mode_size(mode) <= memory_size) {
            hp_int10_add_mode(s, mode);
        }
    }
}

static void hp_int10_add_oem_modes(HPIA64Int10 *s, uint32_t memory_size)
{
    static const uint8_t bpps[] = { 16, 24, 32 };
    size_t i;
    size_t depth;

    for (i = 0; i < G_N_ELEMENTS(hp_int10_oem_resolutions); i++) {
        const HPIA64VbeResolution *resolution =
            &hp_int10_oem_resolutions[i];

        for (depth = 0; depth < G_N_ELEMENTS(bpps); depth++) {
            HPIA64VbeMode mode = {
                .number = resolution->base_number + depth,
                .width = resolution->width,
                .height = resolution->height,
                .bpp = bpps[depth],
            };

            if (mode.width <= s->maxx && mode.height <= s->maxy &&
                hp_int10_mode_size(&mode) <= memory_size) {
                hp_int10_add_mode(s, &mode);
            }
        }
    }
}

static bool hp_int10_read_properties(HPIA64Int10 *s, Error **errp)
{
    Object *obj = OBJECT(s->vga);
    static const char * const required[] = {
        "xres", "yres", "xmax", "ymax", "vgamem_mb",
    };
    uint64_t xres;
    uint64_t yres;
    uint64_t xmax;
    uint64_t ymax;
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(required); i++) {
        if (!object_property_find(obj, required[i])) {
            error_setg(errp,
                       "VGA device '%s' lacks property '%s' required by "
                       "the HP IA-64 INT 10h bridge",
                       object_get_typename(obj), required[i]);
            return false;
        }
    }

    xres = object_property_get_uint(obj, "xres", errp);
    if (*errp) {
        return false;
    }
    yres = object_property_get_uint(obj, "yres", errp);
    if (*errp) {
        return false;
    }
    xmax = object_property_get_uint(obj, "xmax", errp);
    if (*errp) {
        return false;
    }
    ymax = object_property_get_uint(obj, "ymax", errp);
    if (*errp) {
        return false;
    }

    if ((xres == 0) != (yres == 0)) {
        error_setg(errp, "VGA properties xres and yres must be set together");
        return false;
    }
    if ((xmax == 0) != (ymax == 0)) {
        error_setg(errp, "VGA properties xmax and ymax must be set together");
        return false;
    }
    if (xres == 0) {
        xres = HP_INT10_DEFAULT_XRES;
        yres = HP_INT10_DEFAULT_YRES;
    }
    if (xmax == 0) {
        xmax = xres;
        ymax = yres;
    }

    if (xres < 8 || yres < 1 ||
        xres > VBE_DISPI_MAX_XRES || yres > VBE_DISPI_MAX_YRES) {
        error_setg(errp,
                   "VGA preferred resolution %" PRIu64 "x%" PRIu64
                   " is outside the Bochs VBE range 8x1 to %ux%u",
                   xres, yres, VBE_DISPI_MAX_XRES, VBE_DISPI_MAX_YRES);
        return false;
    }
    if (xmax > VBE_DISPI_MAX_XRES || ymax > VBE_DISPI_MAX_YRES) {
        error_setg(errp,
                   "VGA maximum resolution %" PRIu64 "x%" PRIu64
                   " exceeds the Bochs VBE limit %ux%u",
                   xmax, ymax, VBE_DISPI_MAX_XRES, VBE_DISPI_MAX_YRES);
        return false;
    }
    if (xres > xmax || yres > ymax) {
        error_setg(errp,
                   "VGA preferred resolution %" PRIu64 "x%" PRIu64
                   " exceeds maximum resolution %" PRIu64 "x%" PRIu64,
                   xres, yres, xmax, ymax);
        return false;
    }
    if (xres & 7) {
        error_setg(errp,
                   "VGA preferred horizontal resolution %" PRIu64
                   " is not a multiple of 8", xres);
        return false;
    }

    s->prefx = xres;
    s->prefy = yres;
    s->maxx = xmax;
    s->maxy = ymax;
    return true;
}

static bool hp_int10_build_vbe_config(HPIA64Int10 *s, Error **errp)
{
    static const uint8_t native_bpps[] = { 16, 24, 32 };
    static const uint16_t native_numbers[] = {
        HP_INT10_NATIVE_MODE_16,
        HP_INT10_NATIVE_MODE_24,
        HP_INT10_NATIVE_MODE_32,
    };
    PCIIORegion *fb = &s->vga->io_regions[s->framebuffer_bar];
    PCIIORegion *mmio = &s->vga->io_regions[s->mmio_bar];
    qemu_edid_info edid_info = {
        .vendor = "HWP",
        .name = "QEMU HP IA64",
        .prefx = s->prefx,
        .prefy = s->prefy,
        .maxx = s->maxx,
        .maxy = s->maxy,
        .refresh_rate = 60000,
    };
    uint32_t memory_size;
    uint64_t required_size;
    size_t edid_size;
    size_t i;

    _Static_assert(G_N_ELEMENTS(hp_int10_legacy_modes) +
                   G_N_ELEMENTS(hp_int10_oem_resolutions) * 3 +
                   G_N_ELEMENTS(native_bpps) <= HP_IA64_INT10_MAX_MODES,
                   "HP IA-64 VBE mode array is too small");

    if (fb->memory == NULL || mmio->memory == NULL) {
        error_setg(errp,
                   "VGA device '%s' does not provide framebuffer BAR %u "
                   "and MMIO BAR %u",
                   object_get_typename(OBJECT(s->vga)),
                   s->framebuffer_bar, s->mmio_bar);
        return false;
    }
    s->framebuffer_size = fb->size;
    if (s->framebuffer_base > UINT32_MAX ||
        s->framebuffer_size > (UINT64_C(1) << 32) -
                              s->framebuffer_base) {
        error_setg(errp,
                   "HP IA-64 VBE framebuffer is outside 32-bit PCI memory");
        return false;
    }

    memory_size = hp_int10_vbe_memory_size(s);
    if (memory_size == 0 || memory_size > s->framebuffer_size) {
        error_setg(errp,
                   "VGA memory size 0x%x exceeds framebuffer aperture "
                   "0x%" PRIx64,
                   memory_size, s->framebuffer_size);
        return false;
    }

    required_size = (uint64_t)s->prefx * s->prefy * 4;
    if (required_size > memory_size) {
        error_setg(errp,
                   "VGA preferred mode %ux%ux32 requires %" PRIu64
                   " bytes, but only 0x%x bytes are available",
                   s->prefx, s->prefy, required_size, memory_size);
        return false;
    }

    s->mode_count = 0;
    if (s->prefx == HP_INT10_DEFAULT_XRES &&
        s->prefy == HP_INT10_DEFAULT_YRES &&
        s->maxx == HP_INT10_DEFAULT_XRES &&
        s->maxy == HP_INT10_DEFAULT_YRES) {
        for (i = 0; i < G_N_ELEMENTS(hp_int10_legacy_modes); i++) {
            hp_int10_add_mode(s, &hp_int10_legacy_modes[i]);
        }
    } else {
        hp_int10_add_filtered_modes(s, hp_int10_legacy_modes,
                                    G_N_ELEMENTS(hp_int10_legacy_modes),
                                    memory_size);
        hp_int10_add_oem_modes(s, memory_size);
        for (i = 0; i < G_N_ELEMENTS(native_bpps); i++) {
            HPIA64VbeMode native = {
                .number = native_numbers[i],
                .width = s->prefx,
                .height = s->prefy,
                .bpp = native_bpps[i],
            };

            if (!hp_int10_has_geometry(s, &native)) {
                hp_int10_add_mode(s, &native);
            }
        }
    }

    memset(s->edid, 0, sizeof(s->edid));
    edid_size = s->prefx == HP_INT10_DEFAULT_XRES &&
                s->prefy == HP_INT10_DEFAULT_YRES &&
                s->maxx == HP_INT10_DEFAULT_XRES &&
                s->maxy == HP_INT10_DEFAULT_YRES ?
                128 : sizeof(s->edid);
    qemu_edid_generate(s->edid, edid_size, &edid_info);
    s->edid_blocks = 1 + s->edid[126];
    g_assert(s->edid_blocks <= sizeof(s->edid) / 128);
    return true;
}

static void hp_int10_update_bda(const HPIA64VgaLegacyMode *mode,
                                bool no_clear)
{
    address_space_stb(&address_space_memory, HP_INT10_BDA_VIDEO_MODE,
                      mode->number, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, HP_INT10_BDA_VIDEO_COLUMNS,
                         mode->columns, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, HP_INT10_BDA_VIDEO_PAGE_SIZE,
                         mode->page_size, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, HP_INT10_BDA_VIDEO_PAGE_START,
                         0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_set(&address_space_memory, HP_INT10_BDA_CURSOR_POSITIONS,
                      0, 16, MEMTXATTRS_UNSPECIFIED);
    address_space_stw_le(&address_space_memory, HP_INT10_BDA_CURSOR_TYPE,
                         0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, HP_INT10_BDA_VIDEO_PAGE,
                      0, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory, HP_INT10_BDA_CRTC_ADDRESS,
                         VGA_CRT_IC, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, HP_INT10_BDA_VIDEO_ROWS,
                      mode->rows - 1, MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stw_le(&address_space_memory,
                         HP_INT10_BDA_CHARACTER_HEIGHT,
                         mode->character_height,
                         MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, HP_INT10_BDA_VIDEO_CONTROL,
                      0x60 | (no_clear ? 0x80 : 0),
                      MEMTXATTRS_UNSPECIFIED, NULL);
    address_space_stb(&address_space_memory, HP_INT10_BDA_VIDEO_SWITCHES,
                      0xf9, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void hp_int10_load_ega_palette(HPIA64Int10 *s)
{
    unsigned int color;

    hp_int10_vga_writeb(s, VGA_PEL_MSK, 0xff);
    hp_int10_vga_writeb(s, VGA_PEL_IW, 0);
    for (color = 0; color < 64; color++) {
        uint8_t red = (color & 0x04 ? 0x2a : 0) |
                      (color & 0x20 ? 0x15 : 0);
        uint8_t green = (color & 0x02 ? 0x2a : 0) |
                        (color & 0x10 ? 0x15 : 0);
        uint8_t blue = (color & 0x01 ? 0x2a : 0) |
                       (color & 0x08 ? 0x15 : 0);

        hp_int10_vga_writeb(s, VGA_PEL_D, red);
        hp_int10_vga_writeb(s, VGA_PEL_D, green);
        hp_int10_vga_writeb(s, VGA_PEL_D, blue);
    }
}

static void hp_int10_program_legacy_mode(HPIA64Int10 *s,
                                         const HPIA64VgaLegacyMode *mode,
                                         bool no_clear)
{
    size_t i;

    hp_int10_vbe_write(s, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    hp_int10_vga_indexed_write(s, VGA_SEQ_I, VGA_SEQ_D,
                               VGA_SEQ_RESET, 0x01);
    for (i = 0; i < VGA_SEQ_C - 1; i++) {
        hp_int10_vga_indexed_write(s, VGA_SEQ_I, VGA_SEQ_D, i + 1,
                                   mode->sequencer[i]);
    }
    hp_int10_vga_writeb(s, VGA_MIS_W, mode->misc);
    hp_int10_vga_indexed_write(s, VGA_GFX_I, VGA_GFX_D, VGA_GFX_MISC,
                               mode->graphics[VGA_GFX_MISC]);
    hp_int10_vga_indexed_write(s, VGA_SEQ_I, VGA_SEQ_D,
                               VGA_SEQ_RESET, 0x03);
    for (i = 0; i < VGA_GFX_C; i++) {
        hp_int10_vga_indexed_write(s, VGA_GFX_I, VGA_GFX_D, i,
                                   mode->graphics[i]);
    }

    hp_int10_vga_indexed_write(s, VGA_CRT_IC, VGA_CRT_DC,
                               VGA_CRTC_V_SYNC_END, 0);
    for (i = 0; i < VGA_CRT_C; i++) {
        hp_int10_vga_indexed_write(s, VGA_CRT_IC, VGA_CRT_DC, i,
                                   mode->crtc[i]);
    }
    for (i = 0; i < VGA_ATT_C; i++) {
        (void)hp_int10_vga_readb(s, VGA_IS1_RC);
        hp_int10_vga_writeb(s, VGA_ATT_W, i);
        hp_int10_vga_writeb(s, VGA_ATT_W, mode->attribute[i]);
    }
    hp_int10_load_ega_palette(s);

    if (!no_clear) {
        address_space_set(&address_space_memory, s->framebuffer_base, 0,
                          MIN(s->framebuffer_size,
                              HP_INT10_PLANAR_MEMORY_SIZE),
                          MEMTXATTRS_UNSPECIFIED);
    }
    (void)hp_int10_vga_readb(s, VGA_IS1_RC);
    hp_int10_vga_writeb(s, VGA_ATT_W, VGA_AR_ENABLE_DISPLAY);

    s->legacy_mode = mode->number;
    s->legacy_columns = mode->columns;
    hp_int10_update_bda(mode, no_clear);
}

static bool hp_int10_set_legacy_mode(HPIA64Int10 *s, uint8_t request)
{
    uint8_t number = request & 0x7f;
    bool no_clear = request & 0x80;
    const HPIA64VgaLegacyMode *mode;

    if (number == 3) {
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        s->legacy_mode = number;
        s->legacy_columns = 80;
        return true;
    }

    mode = hp_int10_find_legacy_mode(number);
    if (mode == NULL) {
        return false;
    }
    hp_int10_program_legacy_mode(s, mode, no_clear);
    return true;
}

static uint32_t hp_int10_rom_pointer(uint16_t offset)
{
    return ((HP_INT10_ROM_BASE >> 4) << 16) | offset;
}

static void hp_int10_response_clear(HPIA64Int10 *s)
{
    memset(s->response, 0, sizeof(s->response));
    s->response_length = 0;
    s->response_offset = 0;
}

static void hp_int10_response_size(HPIA64Int10 *s, size_t size)
{
    g_assert(size <= sizeof(s->response));
    g_assert((size & 1) == 0);
    memset(s->response, 0, size);
    s->response_length = size;
    s->response_offset = 0;
}

static void hp_int10_vbe_success(HPIA64Int10 *s)
{
    s->result.ax = 0x004f;
}

static void hp_int10_vbe_failure(HPIA64Int10 *s)
{
    s->result.ax = 0x014f;
}

static void hp_int10_vbe_unsupported(HPIA64Int10 *s)
{
    s->result.ax = 0x024f;
}

static void hp_int10_vbe_invalid_mode(HPIA64Int10 *s)
{
    s->result.ax = 0x034f;
}

static void hp_int10_controller_info(HPIA64Int10 *s)
{
    size_t response_size;
    uint8_t *info;

    response_size = s->input_signature == HP_INT10_VBE2_SIGNATURE ?
                    512 : 256;
    hp_int10_response_size(s, response_size);
    info = s->response;
    memcpy(info, "VESA", 4);
    stw_le_p(info + 4, 0x0300);
    stl_le_p(info + 6, hp_int10_rom_pointer(HP_INT10_ROM_OEM));
    stl_le_p(info + 10, 0);
    stl_le_p(info + 14, hp_int10_rom_pointer(HP_INT10_ROM_MODES));
    stw_le_p(info + 18,
             hp_int10_vbe_read(s, VBE_DISPI_INDEX_VIDEO_MEMORY_64K));
    stw_le_p(info + 20, 0x0100);
    stl_le_p(info + 22, hp_int10_rom_pointer(HP_INT10_ROM_VENDOR));
    stl_le_p(info + 26, hp_int10_rom_pointer(HP_INT10_ROM_PRODUCT));
    stl_le_p(info + 30, hp_int10_rom_pointer(HP_INT10_ROM_REVISION));
    hp_int10_vbe_success(s);
}

static void hp_int10_mode_info(HPIA64Int10 *s)
{
    const HPIA64VbeMode *mode =
        hp_int10_find_mode(s, s->request.cx & 0x01ff);
    uint32_t pitch;
    uint32_t image_size;
    uint32_t memory_size;
    uint32_t pages;
    uint8_t red_size;
    uint8_t green_size;
    uint8_t alpha_size;
    uint8_t alpha_pos;
    uint8_t *info;

    if (mode == NULL) {
        hp_int10_vbe_failure(s);
        return;
    }

    hp_int10_response_size(s, 256);
    info = s->response;
    pitch = mode->width * DIV_ROUND_UP(mode->bpp, 8);
    image_size = pitch * mode->height;
    memory_size = hp_int10_vbe_memory_size(s);
    if (image_size > memory_size) {
        hp_int10_response_clear(s);
        hp_int10_vbe_failure(s);
        return;
    }
    pages = memory_size /
            ((image_size + 64 * KiB - 1) & ~((64 * KiB) - 1));
    pages = CLAMP(pages, 1, 256) - 1;

    stw_le_p(info + 0, 0x00bb);
    info[2] = 0x07;
    info[3] = 0;
    stw_le_p(info + 4, 64);
    stw_le_p(info + 6, 64);
    stw_le_p(info + 8, 0xa000);
    stw_le_p(info + 10, 0);
    stl_le_p(info + 12, 0);
    stw_le_p(info + 16, pitch);
    stw_le_p(info + 18, mode->width);
    stw_le_p(info + 20, mode->height);
    info[22] = 8;
    info[23] = 16;
    info[24] = 1;
    info[25] = mode->bpp;
    info[26] = 1;
    info[27] = 6;
    info[28] = 0;
    info[29] = pages;
    info[30] = 1;

    red_size = mode->bpp == 16 ? 5 : 8;
    green_size = mode->bpp == 16 ? 6 : 8;
    alpha_size = mode->bpp == 32 ? 8 : 0;
    alpha_pos = mode->bpp == 32 ? 24 : 0;
    info[31] = red_size;
    info[32] = mode->bpp == 16 ? 11 : 16;
    info[33] = green_size;
    info[34] = mode->bpp == 16 ? 5 : 8;
    info[35] = mode->bpp == 16 ? 5 : 8;
    info[36] = 0;
    info[37] = alpha_size;
    info[38] = alpha_pos;
    info[39] = mode->bpp == 32 ? 2 : 0;
    stl_le_p(info + 40, s->framebuffer_base);
    stw_le_p(info + 50, pitch);
    info[52] = pages;
    info[53] = pages;
    memcpy(info + 54, info + 31, 8);
    hp_int10_vbe_success(s);
}

static const HPIA64VbeMode *hp_int10_current_mode(HPIA64Int10 *s,
                                                   uint16_t *number)
{
    const HPIA64VbeMode *mode = NULL;
    uint16_t enable = hp_int10_vbe_read(s, VBE_DISPI_INDEX_ENABLE);
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    size_t i;

    if (!(enable & VBE_DISPI_ENABLED)) {
        *number = 3;
        return NULL;
    }
    width = hp_int10_vbe_read(s, VBE_DISPI_INDEX_XRES);
    height = hp_int10_vbe_read(s, VBE_DISPI_INDEX_YRES);
    bpp = hp_int10_vbe_read(s, VBE_DISPI_INDEX_BPP);
    for (i = 0; i < s->mode_count; i++) {
        if (s->modes[i].width == width &&
            s->modes[i].height == height &&
            s->modes[i].bpp == bpp) {
            mode = &s->modes[i];
            break;
        }
    }
    *number = mode ? mode->number : 3;
    if (mode && (enable & VBE_DISPI_LFB_ENABLED)) {
        *number |= 0x4000;
    }
    if (mode && (enable & VBE_DISPI_NOCLEARMEM)) {
        *number |= 0x8000;
    }
    return mode;
}

static void hp_int10_set_mode(HPIA64Int10 *s)
{
    const HPIA64VbeMode *mode =
        hp_int10_find_mode(s, s->request.bx & 0x01ff);
    uint32_t image_size;
    uint16_t enable;

    if (mode == NULL) {
        hp_int10_vbe_failure(s);
        return;
    }
    image_size = mode->width * mode->height *
                 DIV_ROUND_UP(mode->bpp, 8);
    if (image_size > hp_int10_vbe_memory_size(s)) {
        hp_int10_vbe_failure(s);
        return;
    }

    hp_int10_vbe_write(s, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_BPP, mode->bpp);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_XRES, mode->width);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_YRES, mode->height);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_BANK, 0);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_VIRT_WIDTH, mode->width);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_X_OFFSET, 0);
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_Y_OFFSET, 0);
    enable = VBE_DISPI_ENABLED;
    if (s->request.bx & 0x4000) {
        enable |= VBE_DISPI_LFB_ENABLED;
    }
    if (s->request.bx & 0x8000) {
        enable |= VBE_DISPI_NOCLEARMEM;
    }
    hp_int10_vbe_write(s, VBE_DISPI_INDEX_ENABLE, enable);
    hp_int10_vbe_success(s);
}

static void hp_int10_window_control(HPIA64Int10 *s)
{
    uint8_t subfunction = s->request.bx >> 8;
    uint8_t window = s->request.bx;

    if (window != 0 || subfunction > 1) {
        hp_int10_vbe_failure(s);
        return;
    }
    if (hp_int10_vbe_read(s, VBE_DISPI_INDEX_ENABLE) &
        VBE_DISPI_LFB_ENABLED) {
        hp_int10_vbe_invalid_mode(s);
        return;
    }
    if (subfunction == 0) {
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_BANK, s->request.dx);
    } else {
        s->result.dx = hp_int10_vbe_read(s, VBE_DISPI_INDEX_BANK);
    }
    hp_int10_vbe_success(s);
}

static void hp_int10_scanline(HPIA64Int10 *s)
{
    uint16_t number;
    const HPIA64VbeMode *mode = hp_int10_current_mode(s, &number);
    uint8_t subfunction = s->request.bx;
    uint32_t bytes_per_pixel;
    uint32_t max_width;
    uint32_t memory_size;
    uint32_t width;
    uint32_t pitch;

    if (mode == NULL || subfunction > 3) {
        hp_int10_vbe_failure(s);
        return;
    }

    bytes_per_pixel = DIV_ROUND_UP(mode->bpp, 8);
    memory_size = hp_int10_vbe_memory_size(s);
    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / mode->height / bytes_per_pixel) & ~7U;
    if (max_width < mode->width) {
        hp_int10_vbe_failure(s);
        return;
    }

    if (subfunction == 0) {
        width = QEMU_ALIGN_UP((uint32_t)s->request.cx, 8);
        width = MAX(width, (uint32_t)mode->width);
        if (width > max_width) {
            hp_int10_vbe_unsupported(s);
            return;
        }
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_VIRT_WIDTH, width);
    } else if (subfunction == 2) {
        width = DIV_ROUND_UP(s->request.cx, bytes_per_pixel);
        if (width == 0) {
            hp_int10_vbe_failure(s);
            return;
        }
        width = QEMU_ALIGN_UP(width, 8);
        width = MAX(width, (uint32_t)mode->width);
        if (width > max_width) {
            hp_int10_vbe_unsupported(s);
            return;
        }
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_VIRT_WIDTH, width);
    } else if (subfunction == 3) {
        width = max_width;
    }

    if (subfunction != 3) {
        width = hp_int10_vbe_read(s, VBE_DISPI_INDEX_VIRT_WIDTH);
    }
    pitch = width * bytes_per_pixel;
    if (pitch == 0) {
        hp_int10_vbe_failure(s);
        return;
    }
    s->result.bx = pitch;
    s->result.cx = width;
    s->result.dx = MIN(memory_size / pitch, UINT16_MAX);
    hp_int10_vbe_success(s);
}

static void hp_int10_display_start(HPIA64Int10 *s)
{
    uint16_t number;
    const HPIA64VbeMode *mode = hp_int10_current_mode(s, &number);
    uint8_t subfunction = s->request.bx;

    if (mode == NULL) {
        hp_int10_vbe_failure(s);
        return;
    }
    switch (subfunction) {
    case 0x00:
    case 0x80:
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_X_OFFSET, s->request.cx);
        hp_int10_vbe_write(s, VBE_DISPI_INDEX_Y_OFFSET, s->request.dx);
        break;
    case 0x01:
        s->result.cx = hp_int10_vbe_read(s, VBE_DISPI_INDEX_X_OFFSET);
        s->result.dx = hp_int10_vbe_read(s, VBE_DISPI_INDEX_Y_OFFSET);
        break;
    default:
        hp_int10_vbe_failure(s);
        return;
    }
    hp_int10_vbe_success(s);
}

static void hp_int10_dpms(HPIA64Int10 *s)
{
    uint8_t subfunction = s->request.bx;

    switch (subfunction) {
    case 0:
        s->result.bx = 0x0f30;
        break;
    case 1:
        s->dpms_state = (s->request.bx >> 8) & 0x0f;
        break;
    case 2:
        s->result.bx = (uint16_t)s->dpms_state << 8 | 2;
        break;
    default:
        hp_int10_vbe_failure(s);
        return;
    }
    hp_int10_vbe_success(s);
}

static void hp_int10_ddc(HPIA64Int10 *s)
{
    uint8_t subfunction = s->request.bx;
    uint16_t block = s->request.dx;

    switch (subfunction) {
    case 0:
        s->result.bx = 0x0103;
        break;
    case 1:
        if (block >= s->edid_blocks) {
            hp_int10_vbe_failure(s);
            return;
        }
        hp_int10_response_size(s, 128);
        memcpy(s->response, s->edid + block * 128, 128);
        break;
    default:
        hp_int10_vbe_failure(s);
        return;
    }
    hp_int10_vbe_success(s);
}

static void hp_int10_execute(HPIA64Int10 *s)
{
    uint16_t current_mode;

    s->result = s->request;
    hp_int10_response_clear(s);

    if ((s->request.ax & 0xff00) == 0x4f00) {
        switch (s->request.ax & 0xff) {
        case 0x00:
            hp_int10_controller_info(s);
            return;
        case 0x01:
            hp_int10_mode_info(s);
            return;
        case 0x02:
            hp_int10_set_mode(s);
            return;
        case 0x03:
            hp_int10_current_mode(s, &current_mode);
            s->result.bx = current_mode;
            hp_int10_vbe_success(s);
            return;
        case 0x05:
            hp_int10_window_control(s);
            return;
        case 0x06:
            hp_int10_scanline(s);
            return;
        case 0x07:
            hp_int10_display_start(s);
            return;
        case 0x10:
            hp_int10_dpms(s);
            return;
        case 0x15:
            hp_int10_ddc(s);
            return;
        default:
            hp_int10_vbe_unsupported(s);
            return;
        }
    }

    switch (s->request.ax >> 8) {
    case 0x00:
        hp_int10_set_legacy_mode(s, s->request.ax);
        break;
    case 0x0f:
        if (hp_int10_vbe_read(s, VBE_DISPI_INDEX_ENABLE) &
            VBE_DISPI_ENABLED) {
            s->result.ax = 80 << 8 | 3;
        } else {
            s->result.ax = (uint16_t)s->legacy_columns << 8 |
                           s->legacy_mode;
        }
        s->result.bx &= 0x00ff;
        break;
    case 0x1a:
        if ((s->request.ax & 0xff) == 0) {
            s->result.ax = 0x001a;
            s->result.bx = 0x0008;
        }
        break;
    default:
        break;
    }
}

static uint64_t hp_int10_io_read(void *opaque, hwaddr addr, unsigned size)
{
    HPIA64Int10 *s = opaque;
    unsigned int reg = addr >> 1;

    if (size != 2 || (addr & 1)) {
        return 0xffff;
    }
    switch (reg) {
    case HP_INT10_REG_AX:
        return s->result.ax;
    case HP_INT10_REG_BX:
        return s->result.bx;
    case HP_INT10_REG_CX:
        return s->result.cx;
    case HP_INT10_REG_DX:
        return s->result.dx;
    case HP_INT10_REG_DI:
        return s->result.di;
    case HP_INT10_REG_ES:
        return s->result.es;
    case HP_INT10_REG_EXEC:
        return s->response_length / 2;
    case HP_INT10_REG_DATA:
        if (s->response_offset < s->response_length) {
            uint16_t value = lduw_le_p(s->response + s->response_offset);

            s->response_offset += 2;
            return value;
        }
        return 0;
    default:
        return 0xffff;
    }
}

static void hp_int10_io_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    HPIA64Int10 *s = opaque;
    unsigned int reg = addr >> 1;

    if (size != 2 || (addr & 1)) {
        return;
    }
    switch (reg) {
    case HP_INT10_REG_AX:
        s->request.ax = value;
        s->input_signature = 0;
        s->input_signature_words = 0;
        break;
    case HP_INT10_REG_BX:
        s->request.bx = value;
        break;
    case HP_INT10_REG_CX:
        s->request.cx = value;
        break;
    case HP_INT10_REG_DX:
        s->request.dx = value;
        break;
    case HP_INT10_REG_DI:
        s->request.di = value;
        break;
    case HP_INT10_REG_ES:
        s->request.es = value;
        break;
    case HP_INT10_REG_EXEC:
        if ((uint16_t)value == HP_INT10_TRIGGER) {
            hp_int10_execute(s);
        }
        break;
    case HP_INT10_REG_DATA:
        if (s->input_signature_words < 2) {
            s->input_signature |=
                (uint32_t)(uint16_t)value <<
                (s->input_signature_words * 16);
            s->input_signature_words++;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps hp_int10_io_ops = {
    .read = hp_int10_io_read,
    .write = hp_int10_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void hp_int10_install_ati_clock_range(uint8_t *pll, size_t offset,
                                              uint16_t reference,
                                              uint16_t divider,
                                              uint32_t minimum,
                                              uint32_t maximum)
{
    stw_le_p(pll + offset, reference);
    stw_le_p(pll + offset + 2, divider);
    stl_le_p(pll + offset + 4, minimum);
    stl_le_p(pll + offset + 8, maximum);
}

static ATIVGAState *hp_int10_default_ati_vga(PCIDevice *vga)
{
    if (!object_dynamic_cast(OBJECT(vga), TYPE_ATI_VGA) ||
        !ATI_VGA(vga)->default_rom) {
        return NULL;
    }
    return ATI_VGA(vga);
}

static bool hp_int10_default_radeon(PCIDevice *vga)
{
    uint16_t device = pci_get_word(vga->config + PCI_DEVICE_ID);

    return hp_int10_default_ati_vga(vga) &&
        (device == HP_INT10_ATI_RV100_DEVICE_ID ||
         device == HP_INT10_ATI_ES1000_DEVICE_ID);
}

static void hp_int10_post_ati_clocks(PCIDevice *vga)
{
    ATIVGAState *ati;
    uint32_t clock, feedback;

    if (!hp_int10_default_radeon(vga)) {
        return;
    }
    ati = ATI_VGA(vga);
    clock = ati->dev_id == HP_INT10_ATI_ES1000_DEVICE_ID ? 20000 : 16600;

    /*
     * The bridge initializes the memory and system clocks described by its
     * COMBIOS tables.  With a /2 output, the clock is
     * 2 * reference * feedback / reference_divider / 2.  A 27 MHz reference
     * and /27 divider represent both advertised defaults exactly.
     */
    feedback = clock * HP_INT10_ATI_CLOCK_REF_DIV / HP_INT10_ATI_CLOCK_REF;
    g_assert(feedback <= UINT8_MAX);
    ati->regs.pll[R100_M_SPLL_REF_FB_DIV] =
        HP_INT10_ATI_CLOCK_REF_DIV | feedback << 8 | feedback << 16;
    ati->regs.pll[R100_SCLK_CNTL] = R100_PLL_SRC_DIV2;
    ati->regs.pll[R100_MCLK_CNTL] =
        R100_PLL_SRC_DIV2 | R100_PLL_SRC_DIV2 << R100_YCLKA_SRC_SHIFT;
}

static void hp_int10_install_ati_bios_info(uint8_t *rom, PCIDevice *vga,
                                           uint32_t memory_size)
{
    static const char ati_bios_signature[] = "761295520";
    static const uint8_t ati_rage128_header[] = {
        0x02, 0xa0, 0x01, 0x01, 0x03, 0x01,
        HP_INT10_ROM_ATI_RAGE128_HEADER_SIZE, 0x00,
    };
    static const char ati_rage128_misc[] = "R128AGP SGS1UN";
    static const uint8_t
        ati_rage128_crt[HP_INT10_ROM_ATI_RAGE128_CRT_SIZE] = {
        0x12, 0x00, 0x80, 0x00, 0x00, 0x00, 0x63, 0x4f,
        0x51, 0x8c, 0x0c, 0x02, 0xdf, 0x01, 0xe9, 0x01,
        0x82, 0x00, 0xd6, 0x09,
    };
    static const uint8_t ati_vga_connector[] = {
        0x11, 0x11, 0x00, 0x23, 0x00, 0x00,
    };
    uint8_t *header = rom + HP_INT10_ROM_ATI_HEADER;
    uint8_t *pll = rom + HP_INT10_ROM_ATI_PLL;
    uint16_t vendor = pci_get_word(vga->config + PCI_VENDOR_ID);
    uint16_t device = pci_get_word(vga->config + PCI_DEVICE_ID);
    bool rage128 = device == HP_INT10_ATI_RAGE128_DEVICE_ID;
    bool es1000 = device == HP_INT10_ATI_ES1000_DEVICE_ID;
    uint32_t memory_mb = MIN(memory_size / MiB, 256U);
    uint32_t memory_step = memory_mb > UINT8_MAX ? 2 : 1;
    uint32_t memory_units = memory_mb / memory_step;
    uint16_t clock_divider = hp_int10_default_radeon(vga) ?
        HP_INT10_ATI_CLOCK_REF_DIV : 12;

    if (vendor != HP_INT10_ATI_VENDOR_ID) {
        return;
    }
    g_assert(memory_size % MiB == 0);
    g_assert(memory_mb != 0 && memory_mb % memory_step == 0);
    g_assert(memory_units != 0 && memory_units <= UINT8_MAX);

    memcpy(rom + HP_INT10_ROM_ATI_SIGNATURE,
           ati_bios_signature, sizeof(ati_bios_signature));
    stw_le_p(rom + 0x48, HP_INT10_ROM_ATI_HEADER);
    if (rage128) {
        memcpy(header, ati_rage128_header, sizeof(ati_rage128_header));
    } else {
        header[0] = 8;
        header[1] = 0xa0;
        stw_le_p(header + 0x06, HP_INT10_ROM_ATI_HEADER_SIZE);
    }
    stw_le_p(header + 0x0c, HP_INT10_ROM_ATI_INIT);
    stw_le_p(header + 0x1c,
             pci_get_word(vga->config + PCI_SUBSYSTEM_VENDOR_ID));
    stw_le_p(header + 0x1e,
             pci_get_word(vga->config + PCI_SUBSYSTEM_ID));
    stw_le_p(header + 0x30, HP_INT10_ROM_ATI_PLL);
    if (rage128) {
        stw_le_p(header + 0x14, HP_INT10_ROM_ATI_RAGE128_MISC);
        stw_le_p(header + 0x2e, HP_INT10_ROM_ATI_RAGE128_CRT);
        memcpy(rom + HP_INT10_ROM_ATI_RAGE128_MISC, ati_rage128_misc,
               sizeof(ati_rage128_misc));
        memcpy(rom + HP_INT10_ROM_ATI_RAGE128_CRT, ati_rage128_crt,
               sizeof(ati_rage128_crt));
    } else {
        stw_le_p(header + 0x14, HP_INT10_ROM_ATI_BIOS_SUPPORT);
        stw_le_p(header + 0x46, HP_INT10_ROM_ATI_INIT);
        stw_le_p(header + 0x48, HP_INT10_ROM_ATI_MEM_CONFIG);
        stw_le_p(header + 0x4e, HP_INT10_ROM_ATI_INIT);
        /* One VGA/primary-DAC connector entry. */
        stw_le_p(header + 0x50, HP_INT10_ROM_ATI_CONNECTOR);
        memcpy(rom + HP_INT10_ROM_ATI_CONNECTOR, ati_vga_connector,
               sizeof(ati_vga_connector));
        stw_le_p(header + 0x52, HP_INT10_ROM_ATI_INIT);
        stw_le_p(header + 0x5e, HP_INT10_ROM_ATI_MISC);
    }

    rom[HP_INT10_ROM_ATI_MEM_CONFIG - 3] =
        HP_INT10_ROM_ATI_MEM_RESET_OFFSET;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG - 2] =
        memory_step == 1 ? 0 : memory_step;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG - 1] = 0;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG] = (uint8_t)memory_units;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG + 1] =
        memory_step == 1 ? 0x25 : 0x2d;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG + 2] = 0;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG + 3] = 1;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG + 4] = 0;
    rom[HP_INT10_ROM_ATI_MEM_CONFIG + 5] = 0xff;

    pll[0] = rage128 ? 6 : 0x0a;
    pll[1] = rage128 ? 0x32 : 0x46;
    pll[2] = 3;
    pll[3] = rage128 ? 2 : 3;
    stw_le_p(pll + 0x04, rage128 ? 0x0600 :
             (es1000 ? 0x05ee : 0x05a6));
    stw_le_p(pll + 0x06, rage128 ? 0x05f8 :
             (es1000 ? 0x05e6 : 0x059e));
    stw_le_p(pll + 0x08, rage128 ? 12000 : (es1000 ? 20000 : 16600));
    stw_le_p(pll + 0x0a, rage128 ? 12000 : (es1000 ? 20000 : 16600));
    pll[0x0c] = 3;
    pll[0x0d] = 12;
    if (rage128) {
        hp_int10_install_ati_clock_range(pll, 0x0e,
                                         2950, 65, 12500, 40000);
        hp_int10_install_ati_clock_range(pll, 0x1a,
                                         2950, 29, 12500, 26041);
        hp_int10_install_ati_clock_range(pll, 0x26,
                                         2950, 29, 12500, 26041);
    } else {
        hp_int10_install_ati_clock_range(pll, 0x0e,
                                         2700, 60, 12000, 35000);
        hp_int10_install_ati_clock_range(pll, 0x1a,
                                         HP_INT10_ATI_CLOCK_REF,
                                         clock_divider, 20000, 40000);
        hp_int10_install_ati_clock_range(pll, 0x26,
                                         HP_INT10_ATI_CLOCK_REF,
                                         clock_divider, 20000, 40000);
        pll[0x32] = 1;
        pll[0x33] = 0x12;
        stw_le_p(pll + 0x34, 2700);
        stl_le_p(pll + 0x36, 40);
        stl_le_p(pll + 0x3a, 3000);
        stl_le_p(pll + 0x3e, 12000);
        stl_le_p(pll + 0x42, 35000);
    }
}

static void hp_int10_install_nvidia_bios_info(uint8_t *rom, uint16_t vendor)
{
    uint8_t *bmp = rom + HP_INT10_ROM_NVIDIA_BMP;
    uint8_t checksum = 0;
    size_t i;

    if (vendor != HP_INT10_NVIDIA_VENDOR_ID) {
        return;
    }

    /* NV15 metadata with zero init-script pointers. */
    memcpy(bmp, "\xff\x7f" "NV\0", 5);
    bmp[5] = HP_INT10_NVIDIA_BMP_MAJOR;
    bmp[6] = HP_INT10_NVIDIA_BMP_MINOR;
    for (i = 0; i < 7; i++) {
        checksum += bmp[i];
    }
    bmp[7] = -checksum;

    /* BIOS version 03.15.00.01 and the two emulated DDC CRTC pairs. */
    bmp[10] = 0x01;
    bmp[11] = 0x00;
    bmp[12] = 0x15;
    bmp[13] = 0x03;
    bmp[54] = 0;
    bmp[55] = 0xff;
    bmp[56] = 1;
    bmp[58] = 0x3f;
    bmp[59] = 0x3e;
    bmp[60] = 0x37;
    bmp[61] = 0x36;
    stl_le_p(bmp + 67, HP_INT10_NVIDIA_PLL_MAX_KHZ);
    stl_le_p(bmp + 71, HP_INT10_NVIDIA_PLL_MIN_KHZ);
}

static void hp_int10_install_rom(HPIA64Int10 *s)
{
    uint8_t rom[HP_INT10_ROM_SIZE] = { 0 };
    uint8_t vector[4];
    uint8_t checksum = 0;
    uint16_t vendor = pci_get_word(s->vga->config + PCI_VENDOR_ID);
    uint16_t device = pci_get_word(s->vga->config + PCI_DEVICE_ID);
    size_t i;

    g_assert(HP_INT10_ROM_HANDLER + sizeof(hp_int10_handler) <=
             HP_INT10_ROM_OEM);
    g_assert(HP_INT10_ROM_OEM + sizeof(hp_int10_oem) <=
             HP_INT10_ROM_VENDOR);
    g_assert(HP_INT10_ROM_VENDOR + sizeof(hp_int10_vendor) <=
             HP_INT10_ROM_PRODUCT);
    g_assert(HP_INT10_ROM_PRODUCT + sizeof(hp_int10_product) <=
             HP_INT10_ROM_REVISION);
    g_assert(HP_INT10_ROM_REVISION + sizeof(hp_int10_revision) <=
             HP_INT10_ROM_MODES);
    g_assert(HP_INT10_ROM_ATI_HEADER + HP_INT10_ROM_ATI_HEADER_SIZE <=
             HP_INT10_ROM_ATI_INIT);
    g_assert(HP_INT10_ROM_ATI_INIT + HP_INT10_ROM_ATI_INIT_READ <=
             HP_INT10_ROM_ATI_BIOS_SUPPORT);
    g_assert(HP_INT10_ROM_ATI_BIOS_SUPPORT +
             HP_INT10_ROM_ATI_BIOS_SUPPORT_SIZE <=
             HP_INT10_ROM_ATI_MISC);
    g_assert(HP_INT10_ROM_ATI_RAGE128_MISC +
             HP_INT10_ROM_ATI_RAGE128_MISC_SIZE <=
             HP_INT10_ROM_HANDLER);
    g_assert(HP_INT10_ROM_ATI_MISC + HP_INT10_ROM_ATI_MISC_SIZE <=
             HP_INT10_ROM_HANDLER);
    g_assert(HP_INT10_ROM_MODES + (s->mode_count + 1) * 2 <=
             HP_INT10_ROM_ATI_CONNECTOR);
    g_assert(HP_INT10_ROM_ATI_CONNECTOR +
             HP_INT10_ROM_ATI_CONNECTOR_SIZE <= HP_INT10_ROM_ATI_PLL);
    g_assert(HP_INT10_ROM_ATI_RAGE128_CRT +
             HP_INT10_ROM_ATI_RAGE128_CRT_SIZE <= HP_INT10_ROM_ATI_PLL);
    g_assert(HP_INT10_ROM_ATI_PLL + HP_INT10_ROM_ATI_PLL_READ_SIZE <=
             HP_INT10_ROM_ATI_MEM_CONFIG -
             HP_INT10_ROM_ATI_MEM_PREFIX_SIZE);
    g_assert(HP_INT10_ROM_ATI_MEM_CONFIG +
             HP_INT10_ROM_ATI_MEM_RESET_OFFSET +
             HP_INT10_ROM_ATI_MEM_RESET_SIZE <=
             HP_INT10_ROM_NVIDIA_BMP);

    rom[0] = 0x55;
    rom[1] = 0xaa;
    rom[2] = HP_INT10_ROM_SIZE / 512;
    memcpy(rom + 3, hp_int10_rom_init, sizeof(hp_int10_rom_init));

    stw_le_p(rom + 0x18, HP_INT10_ROM_PCIR_OFFSET);
    memcpy(rom + HP_INT10_ROM_PCIR_OFFSET, "PCIR", 4);
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x04, vendor);
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x06, device);
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x08, 0);
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x0a, 0x18);
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x0c] = 0;
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x0d] = 0;
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x0e] = 0;
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x0f] =
        PCI_CLASS_DISPLAY_VGA >> 8;
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x10,
             HP_INT10_ROM_SIZE / 512);
    stw_le_p(rom + HP_INT10_ROM_PCIR_OFFSET + 0x12, 0x0100);
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x14] = 0;
    rom[HP_INT10_ROM_PCIR_OFFSET + 0x15] = 0x80;
    memcpy(rom + 0x60, "QEMU HP IA64 INT10", 19);
    hp_int10_install_ati_bios_info(rom, s->vga,
                                   hp_int10_vbe_memory_size(s));
    hp_int10_install_nvidia_bios_info(rom, vendor);
    memcpy(rom + HP_INT10_ROM_HANDLER, hp_int10_handler,
           sizeof(hp_int10_handler));
    memcpy(rom + HP_INT10_ROM_OEM, hp_int10_oem,
           sizeof(hp_int10_oem));
    memcpy(rom + HP_INT10_ROM_VENDOR, hp_int10_vendor,
           sizeof(hp_int10_vendor));
    memcpy(rom + HP_INT10_ROM_PRODUCT, hp_int10_product,
           sizeof(hp_int10_product));
    memcpy(rom + HP_INT10_ROM_REVISION, hp_int10_revision,
           sizeof(hp_int10_revision));
    for (i = 0; i < s->mode_count; i++) {
        stw_le_p(rom + HP_INT10_ROM_MODES + i * 2, s->modes[i].number);
    }
    stw_le_p(rom + HP_INT10_ROM_MODES + s->mode_count * 2, 0xffff);

    for (i = 0; i < sizeof(rom) - 1; i++) {
        checksum += rom[i];
    }
    rom[sizeof(rom) - 1] = -checksum;

    /*
     * Mirror the bridge-generated default ROM so the legacy shadow and PCI
     * ROM BAR expose identical COMBIOS tables.  User-supplied ROMs retain
     * their own contents.
     */
    if (s->vga->has_rom && hp_int10_default_ati_vga(s->vga)) {
        uint8_t *pci_rom = memory_region_get_ram_ptr(&s->vga->rom);
        uint64_t pci_rom_size = memory_region_size(&s->vga->rom);

        g_assert(pci_rom_size >= sizeof(rom));
        memset(pci_rom, 0xff, pci_rom_size);
        memcpy(pci_rom, rom, sizeof(rom));
        memory_region_set_dirty(&s->vga->rom, 0, pci_rom_size);
    }

    cpu_physical_memory_write(HP_INT10_ROM_BASE, rom, sizeof(rom));

    stw_le_p(vector, HP_INT10_ROM_HANDLER);
    stw_le_p(vector + 2, HP_INT10_ROM_BASE >> 4);
    cpu_physical_memory_write(HP_INT10_VECTOR_ADDR, vector, sizeof(vector));
}

static const VMStateDescription vmstate_hp_int10_registers = {
    .name = "hp-ia64-int10/registers",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(ax, HPIA64Int10Registers),
        VMSTATE_UINT16(bx, HPIA64Int10Registers),
        VMSTATE_UINT16(cx, HPIA64Int10Registers),
        VMSTATE_UINT16(dx, HPIA64Int10Registers),
        VMSTATE_UINT16(di, HPIA64Int10Registers),
        VMSTATE_UINT16(es, HPIA64Int10Registers),
        VMSTATE_END_OF_LIST()
    }
};

static int hp_int10_post_load(void *opaque, int version_id)
{
    HPIA64Int10 *s = opaque;

    (void)version_id;
    if (s->response_length > sizeof(s->response) ||
        s->response_offset > s->response_length ||
        ((s->response_length | s->response_offset) & 1) != 0 ||
        s->input_signature_words > 2) {
        return -EINVAL;
    }
    return 0;
}

static const VMStateDescription vmstate_hp_int10 = {
    .name = "hp-ia64-int10",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = hp_int10_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(request, HPIA64Int10, 1,
                       vmstate_hp_int10_registers,
                       HPIA64Int10Registers),
        VMSTATE_STRUCT(result, HPIA64Int10, 1,
                       vmstate_hp_int10_registers,
                       HPIA64Int10Registers),
        VMSTATE_UINT32(input_signature, HPIA64Int10),
        VMSTATE_UINT8_ARRAY(response, HPIA64Int10, 512),
        VMSTATE_UINT16(response_length, HPIA64Int10),
        VMSTATE_UINT16(response_offset, HPIA64Int10),
        VMSTATE_UINT8(input_signature_words, HPIA64Int10),
        VMSTATE_UINT8(dpms_state, HPIA64Int10),
        VMSTATE_UINT8(legacy_mode, HPIA64Int10),
        VMSTATE_UINT8(legacy_columns, HPIA64Int10),
        VMSTATE_END_OF_LIST()
    }
};

void hp_ia64_int10_reset(HPIA64Int10 *s)
{
    if (!s->initialized) {
        return;
    }
    memset(&s->request, 0, sizeof(s->request));
    memset(&s->result, 0, sizeof(s->result));
    s->input_signature = 0;
    s->input_signature_words = 0;
    hp_int10_response_clear(s);
    s->dpms_state = 0;
    s->legacy_mode = 3;
    s->legacy_columns = 80;
    hp_int10_post_ati_clocks(s->vga);
    hp_int10_install_rom(s);
}

bool hp_ia64_int10_init(HPIA64Int10 *s,
                        const HPIA64Int10Config *config,
                        Error **errp)
{
    g_assert(!s->initialized);
    g_assert(config->owner != NULL);
    g_assert(config->vga != NULL);
    g_assert(config->service_io != NULL);
    g_assert(config->vga_io != NULL);
    g_assert(config->region_name != NULL);
    g_assert(config->framebuffer_bar < PCI_NUM_REGIONS);
    g_assert(config->mmio_bar < PCI_NUM_REGIONS);

    s->vga = config->vga;
    s->vga_io = config->vga_io;
    s->framebuffer_base = config->framebuffer_base;
    s->framebuffer_bar = config->framebuffer_bar;
    s->mmio_bar = config->mmio_bar;
    if (!hp_int10_read_properties(s, errp) ||
        !hp_int10_build_vbe_config(s, errp)) {
        return false;
    }

    memory_region_init_io(&s->service_io, config->owner,
                          &hp_int10_io_ops, s,
                          config->region_name, HP_INT10_IO_SIZE);
    s->service_parent = config->service_io;
    memory_region_add_subregion(s->service_parent, HP_INT10_IO_BASE,
                                &s->service_io);
    s->initialized = true;
    hp_ia64_int10_reset(s);

    if (vmstate_register_with_alias_id(NULL, 0, &vmstate_hp_int10, s,
                                       -1, 0, errp) < 0) {
        memory_region_del_subregion(s->service_parent, &s->service_io);
        s->service_parent = NULL;
        s->initialized = false;
        return false;
    }
    s->vmstate_registered = true;
    return true;
}

void hp_ia64_int10_destroy(HPIA64Int10 *s)
{
    if (s->vmstate_registered) {
        vmstate_unregister(NULL, &vmstate_hp_int10, s);
        s->vmstate_registered = false;
    }
    if (s->initialized) {
        memory_region_del_subregion(s->service_parent, &s->service_io);
        s->service_parent = NULL;
        s->initialized = false;
    }
}
