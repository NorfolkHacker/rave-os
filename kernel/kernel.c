/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "graphics.h"
#include "text.h"
#include "mouse.h"

/* Note: stage2 switches the display into a VBE graphics mode before the
 * kernel even starts, so the vga.c text driver and 0xB8000 no longer
 * apply here -- text.c (built on font.c, ported from ACIDSTORM) is the
 * real text output path now. vga.c/keyboard.c are left in the tree;
 * keyboard input still works identically, it just has nowhere to echo to
 * with vga_putc anymore. */

#define CURSOR_SIZE 8

static uint32_t plasma_color(int x, int y, int w, int h) {
    uint8_t r = (uint8_t)(x * 255 / w);
    uint8_t g = (uint8_t)(y * 255 / h);
    uint8_t b = (uint8_t)((x ^ y) & 0xFF);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* There's no backbuffer/compositor yet, so "erasing" the cursor just
 * recomputes what the plasma backdrop would be at those pixels -- correct
 * as long as the cursor stays over plasma. If it sweeps over the title
 * text, those pixels get overwritten with plasma instead of restored,
 * since nothing remembers "there used to be a letter here". A real
 * compositor (offscreen backbuffer, damage tracking) is future work. */
static void erase_cursor(int x, int y, int w, int h) {
    int row, col;
    for (row = 0; row < CURSOR_SIZE; row++) {
        for (col = 0; col < CURSOR_SIZE; col++) {
            gfx_put_pixel(x + col, y + row, plasma_color(x + col, y + row, w, h));
        }
    }
}

static void draw_cursor(int x, int y, uint32_t color) {
    gfx_fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, color);
}

void kmain(void) {
    int x, y;
    int w = gfx_width();
    int h = gfx_height();
    const char *title = "RAVE-OS";
    const char *subtitle = "KERNEL: MOUSE INPUT ONLINE";
    int title_scale = 4;
    int subtitle_scale = 2;
    int mx, my;

    /* Classic demoscene XOR pattern as backdrop: cheap to compute, never
     * the same color twice in a row, unmistakably "acid". */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, plasma_color(x, y, w, h));
        }
    }

    text_puts((w - text_width(title, title_scale)) / 2, 40, title, 0xFFFFFF, title_scale);
    text_puts((w - text_width(subtitle, subtitle_scale)) / 2, 90, subtitle, 0x000000, subtitle_scale);

    mouse_init();

    mx = w / 2;
    my = h - 100; /* start clear of the title text above */
    draw_cursor(mx, my, 0xFFFFFF);

    for (;;) {
        int dx, dy, buttons;
        uint32_t color;

        mouse_read_packet(&dx, &dy, &buttons);

        erase_cursor(mx, my, w, h);

        mx += dx;
        my += dy;
        if (mx < 0) {
            mx = 0;
        }
        if (my < 0) {
            my = 0;
        }
        if (mx > w - CURSOR_SIZE) {
            mx = w - CURSOR_SIZE;
        }
        if (my > h - CURSOR_SIZE) {
            my = h - CURSOR_SIZE;
        }

        color = (buttons & 0x01) ? 0xFF0000 : 0xFFFFFF; /* red while left button held */
        draw_cursor(mx, my, color);
    }
}
