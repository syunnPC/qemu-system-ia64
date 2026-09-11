/*
 * QEMU VGA Emulator templates
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

static inline uint8_t vga_read_byte(VGACommonState *vga, uint32_t addr)
{
    if (vga->scanout_map) {
        uint32_t delta = addr - vga->scanout_address;

        if (delta >= vga->scanout_length) {
            uint32_t length = MIN(UINT64_C(256),
                                  UINT64_C(0x100000000) - addr);
            uint64_t offset = vga->scanout_map(vga, addr, &length);

            if (offset < vga->vram_size) {
                length = MIN(length, vga->vram_size - offset);
                vga->scanout_data = vga->vram_ptr + offset;
            } else {
                vga->scanout_data = NULL;
            }
            vga->scanout_address = addr;
            vga->scanout_length = length;
            delta = 0;
        }
        return vga->scanout_data ? vga->scanout_data[delta] : 0;
    }
    return vga->vram_ptr[addr & vga->vbe_size_mask];
}

static inline uint16_t vga_read_word_le(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~1;
    uint16_t *ptr = (uint16_t *)(vga->vram_ptr + offset);
    if (vga->scanout_map) {
        return vga_read_byte(vga, addr) |
               (vga_read_byte(vga, addr + 1) << 8);
    }
    return lduw_le_p(ptr);
}

static inline uint16_t vga_read_word_be(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~1;
    uint16_t *ptr = (uint16_t *)(vga->vram_ptr + offset);
    if (vga->scanout_map) {
        return (vga_read_byte(vga, addr) << 8) |
               vga_read_byte(vga, addr + 1);
    }
    return lduw_be_p(ptr);
}

static inline uint32_t vga_read_dword_le(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~3;
    uint32_t *ptr = (uint32_t *)(vga->vram_ptr + offset);
    return ldl_le_p(ptr);
}
