/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "vga.h"

void kmain(void) {
    int i;

    vga_set_color(0x0A, 0x00); /* green on black */
    vga_clear();
    vga_puts("Rave-OS kernel: VGA driver online.\n");

    vga_set_color(0x0F, 0x00); /* white on black */
    vga_puts("Cursor tracking, newlines, and scrolling all work.\n\n");

    /* Print more lines than fit on a 25-row screen to prove vga_scroll()
     * actually shifts old lines up instead of overwriting/wrapping. */
    for (i = 0; i < 30; i++) {
        vga_puts("scroll test line\n");
    }

    for (;;) {
        __asm__ volatile("hlt");
    }
}
