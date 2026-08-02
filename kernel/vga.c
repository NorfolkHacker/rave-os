/* VGA text-mode driver: cursor tracking, newline handling, scrolling, and
 * a hardware cursor (the blinking block a real terminal shows), replacing
 * the one-shot "poke bytes at 0xB8000" approach from the first kernel. */

#include "vga.h"
#include "io.h"

#define VGA_MEMORY ((volatile unsigned char *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

/* CRTC (CRT Controller) index/data ports -- how software tells the VGA
 * card where to draw the hardware cursor. Register 0x0E/0x0F are the
 * high/low bytes of the cursor's linear position (row * VGA_COLS + col). */
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5

static int cursor_row = 0;
static int cursor_col = 0;
static unsigned char current_attr = 0x07; /* light grey on black */

static void vga_set_hw_cursor(int row, int col) {
    unsigned short pos = (unsigned short)(row * VGA_COLS + col);

    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (unsigned char)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (unsigned char)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    int row, col;

    for (row = 1; row < VGA_ROWS; row++) {
        for (col = 0; col < VGA_COLS; col++) {
            int dst = ((row - 1) * VGA_COLS + col) * 2;
            int src = (row * VGA_COLS + col) * 2;
            VGA_MEMORY[dst] = VGA_MEMORY[src];
            VGA_MEMORY[dst + 1] = VGA_MEMORY[src + 1];
        }
    }

    int last_row = (VGA_ROWS - 1) * VGA_COLS;
    for (col = 0; col < VGA_COLS; col++) {
        VGA_MEMORY[(last_row + col) * 2] = ' ';
        VGA_MEMORY[(last_row + col) * 2 + 1] = current_attr;
    }

    cursor_row = VGA_ROWS - 1;
}

void vga_set_color(unsigned char fg, unsigned char bg) {
    current_attr = (unsigned char)((bg << 4) | (fg & 0x0F));
}

void vga_clear(void) {
    int i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_MEMORY[i * 2] = ' ';
        VGA_MEMORY[i * 2 + 1] = current_attr;
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_set_hw_cursor(cursor_row, cursor_col);
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else {
        int offset = (cursor_row * VGA_COLS + cursor_col) * 2;
        VGA_MEMORY[offset] = (unsigned char)c;
        VGA_MEMORY[offset + 1] = current_attr;
        cursor_col++;
        if (cursor_col >= VGA_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_ROWS) {
        vga_scroll();
    }

    vga_set_hw_cursor(cursor_row, cursor_col);
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s);
        s++;
    }
}
