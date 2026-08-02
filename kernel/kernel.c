/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "graphics.h"
#include "text.h"

/* Note: stage2 switches the display into a VBE graphics mode before the
 * kernel even starts, so the vga.c text driver and 0xB8000 no longer
 * apply here -- text.c (built on font.c, ported from ACIDSTORM) is the
 * real text output path now. vga.c/keyboard.c are left in the tree;
 * keyboard input still works identically, it just has nowhere to echo to
 * with vga_putc anymore. */

void kmain(void) {
    int x, y;
    int w = gfx_width();
    int h = gfx_height();
    const char *title = "RAVE-OS";
    const char *subtitle = "KERNEL: FONT RENDERER ONLINE";
    int title_scale = 4;
    int subtitle_scale = 2;

    /* Classic demoscene XOR pattern as backdrop: cheap to compute, never
     * the same color twice in a row, unmistakably "acid". */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t r = (uint8_t)(x * 255 / w);
            uint8_t g = (uint8_t)(y * 255 / h);
            uint8_t b = (uint8_t)((x ^ y) & 0xFF);
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            gfx_put_pixel(x, y, color);
        }
    }

    text_puts((w - text_width(title, title_scale)) / 2, 40, title, 0xFFFFFF, title_scale);
    text_puts((w - text_width(subtitle, subtitle_scale)) / 2, 90, subtitle, 0x000000, subtitle_scale);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
