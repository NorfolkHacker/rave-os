/* Linear-framebuffer graphics driver. Draws straight into the VBE
 * framebuffer stage2 switched the display into and described in
 * boot_info -- no VGA text mode underneath this anymore. */

#include "graphics.h"
#include "boot_info.h"

/* Writes bytes_per_pixel bytes per call rather than always writing a fixed
 * 32-bit word: VBE mode 0x112 was assumed to be 32bpp (matching its
 * standard VESA definition) but this BIOS/QEMU's ModeInfoBlock actually
 * reports BitsPerPixel=24 (confirmed by BytesPerScanLine = 640*3, not
 * 640*4) -- writing 4-byte words at a 3-bytes/pixel stride corrupted
 * every single pixel, each write bleeding into the next one's bytes.
 * Reading boot_info->bpp instead of hardcoding 4 makes this correct for
 * whatever the BIOS actually handed back. */
void gfx_put_pixel(int x, int y, uint32_t rgb) {
    int bytes_per_pixel = boot_info->bpp / 8;
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)boot_info->framebuffer_addr;
    volatile uint8_t *pixel = fb + (uint32_t)y * boot_info->pitch + (uint32_t)x * bytes_per_pixel;

    pixel[0] = (uint8_t)(rgb & 0xFF);         /* blue */
    pixel[1] = (uint8_t)((rgb >> 8) & 0xFF);   /* green */
    pixel[2] = (uint8_t)((rgb >> 16) & 0xFF);  /* red */
    if (bytes_per_pixel >= 4) {
        pixel[3] = 0;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            gfx_put_pixel(x + col, y + row, rgb);
        }
    }
}

void gfx_clear(uint32_t rgb) {
    gfx_fill_rect(0, 0, gfx_width(), gfx_height(), rgb);
}

int gfx_width(void) {
    return boot_info->width;
}

int gfx_height(void) {
    return boot_info->height;
}
