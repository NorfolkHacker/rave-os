/* Linear-framebuffer graphics driver. Draws straight into the VBE
 * framebuffer stage2 switched the display into and described in
 * boot_info -- no VGA text mode underneath this anymore. */

#include "graphics.h"
#include "boot_info.h"

/* Assumes a 32-bit-per-pixel packed-RGB mode, true for the VBE mode 0x112
 * stage2 sets. A driver supporting other bit depths would branch on
 * boot_info->bpp here; not needed yet since stage2 only ever asks for one
 * mode. */
void gfx_put_pixel(int x, int y, uint32_t rgb) {
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)boot_info->framebuffer_addr;
    uint32_t offset = (uint32_t)y * boot_info->pitch + (uint32_t)x * 4;
    *(volatile uint32_t *)(fb + offset) = rgb;
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
