/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "graphics.h"
#include "text.h"
#include "mouse.h"
#include "interrupts.h"

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

/* Real save-under cursor compositing: before drawing the cursor
 * somewhere, remember exactly what was already in the backbuffer there;
 * before moving it, put those exact pixels back. Unlike the old
 * "recompute the plasma formula" approach, this is correct regardless of
 * what's underneath -- plasma, text, or (eventually) a window -- because
 * it never has to know or guess; it just remembers. */
static uint32_t cursor_under[CURSOR_SIZE][CURSOR_SIZE];

static void save_under_cursor(int x, int y) {
    int row, col;
    for (row = 0; row < CURSOR_SIZE; row++) {
        for (col = 0; col < CURSOR_SIZE; col++) {
            cursor_under[row][col] = gfx_get_pixel(x + col, y + row);
        }
    }
}

static void restore_under_cursor(int x, int y) {
    int row, col;
    for (row = 0; row < CURSOR_SIZE; row++) {
        for (col = 0; col < CURSOR_SIZE; col++) {
            gfx_put_pixel(x + col, y + row, cursor_under[row][col]);
        }
    }
}

static void draw_cursor(int x, int y, uint32_t color) {
    gfx_fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, color);
}

void kmain(void) {
    int x, y;
    int w, h;
    const char *title = "RAVE-OS";
    const char *subtitle = "KERNEL: COMPOSITOR ONLINE";
    int title_scale = 4;
    int subtitle_scale = 2;
    int mx, my;

    gfx_init();
    w = gfx_width();
    h = gfx_height();

    /* Classic demoscene XOR pattern as backdrop: cheap to compute, never
     * the same color twice in a row, unmistakably "acid". */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, plasma_color(x, y, w, h));
        }
    }

    text_puts((w - text_width(title, title_scale)) / 2, 40, title, 0xFFFFFF, title_scale);
    text_puts((w - text_width(subtitle, subtitle_scale)) / 2, 90, subtitle, 0x000000, subtitle_scale);

    /* IDT/PIC set up first (masked, no sti yet), then the mouse's polling
     * handshake runs with IRQ12 still masked so it can't race the new
     * interrupt handler for the same bytes, then interrupts are actually
     * enabled once both are ready. */
    interrupts_init();
    mouse_init();
    interrupts_enable();

    mx = w / 2;
    my = h - 100; /* start clear of the title text above */
    save_under_cursor(mx, my);
    draw_cursor(mx, my, 0xFFFFFF);
    gfx_present();

    for (;;) {
        int dx, dy, buttons;
        uint32_t color;

        mouse_read_packet(&dx, &dy, &buttons);

        restore_under_cursor(mx, my);

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

        save_under_cursor(mx, my);
        color = (buttons & 0x01) ? 0xFF0000 : 0xFFFFFF; /* red while left button held */
        draw_cursor(mx, my, color);

        gfx_present();
    }
}
