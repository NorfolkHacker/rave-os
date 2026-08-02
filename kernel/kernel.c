/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#define VGA_MEMORY ((volatile unsigned char *)0xB8000)
#define VGA_COLS 80

static void kputs(int row, int col, const char *s, unsigned char attr) {
    int offset = (row * VGA_COLS + col) * 2;
    while (*s) {
        VGA_MEMORY[offset] = (unsigned char)*s;
        VGA_MEMORY[offset + 1] = attr;
        offset += 2;
        s++;
    }
}

void kmain(void) {
    kputs(1, 0, "Rave-OS kernel: hello from C!", 0x0A); /* green on black */

    for (;;) {
        __asm__ volatile("hlt");
    }
}
