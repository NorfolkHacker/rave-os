/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "graphics.h"

/* Note: stage2 now switches the display into a VBE graphics mode before
 * the kernel even starts, so the vga.c text driver and 0xB8000 no longer
 * apply here -- text output in graphics mode needs a bitmap font renderer,
 * which doesn't exist yet. vga.c/keyboard.c are left in the tree; keyboard
 * input still works identically, it just has nowhere to echo to visibly
 * until that renderer exists. */

void kmain(void) {
    int x, y;
    int w = gfx_width();
    int h = gfx_height();

    /* Classic demoscene XOR pattern: cheap to compute, never the same
     * color twice in a row, and unmistakably "acid" -- proof the
     * framebuffer write path (boot_info -> gfx_put_pixel) actually works. */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t r = (uint8_t)(x * 255 / w);
            uint8_t g = (uint8_t)(y * 255 / h);
            uint8_t b = (uint8_t)((x ^ y) & 0xFF);
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            gfx_put_pixel(x, y, color);
        }
    }

    for (;;) {
        __asm__ volatile("hlt");
    }
}
