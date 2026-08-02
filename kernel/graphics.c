/* Offscreen backbuffer + present, sitting in front of the VBE linear
 * framebuffer stage2 switched the display into (described in boot_info).
 *
 * All drawing targets a plain 32-bit-per-pixel array in RAM; gfx_present()
 * is the only place that touches real video memory, converting to
 * whatever pixel format the hardware actually wants. Before this, every
 * draw call wrote straight to video memory and "erasing" something (e.g.
 * a moving mouse cursor) meant procedurally recomputing what should have
 * been there -- correct only for a background you can recompute, and
 * actively wrong for anything else (like text) it happened to cover. A
 * real backing store fixes that at the root: erasing is just restoring
 * pixels you saved from the backbuffer, whatever they were.
 *
 * The backbuffer lives at a fixed physical address (BACKBUFFER_ADDR)
 * rather than as a static C array. Two reasons: first, a ~1.2MB array
 * would land in .bss, and objcopy -O binary drops .bss entirely (verified
 * empirically -- our kernel.bin is exactly .text+.rodata+.data in size,
 * confirming the kernel has only "worked" this whole time because
 * whatever RAM it's been given happened to read as zero, for the small
 * amount of .bss used so far). Second, even setting that aside, a .bss
 * array this size would push the kernel's memory footprint past 0x90000,
 * where boot/stage2.asm parks the stack -- colliding with it. A fixed
 * high address (2MB, well clear of both problems) sidesteps all of this,
 * the same way boot_info.h already treats BOOT_INFO_ADDR as a fixed
 * physical location rather than a normal variable. Explicitly zeroed in
 * gfx_init() rather than relying on any zero-init guarantee. */

#include "graphics.h"
#include "boot_info.h"

#define BACKBUFFER_ADDR 0x200000

static uint32_t *const backbuffer = (uint32_t *)BACKBUFFER_ADDR;

void gfx_init(void) {
    int i;
    int count = gfx_width() * gfx_height();
    for (i = 0; i < count; i++) {
        backbuffer[i] = 0;
    }
}

int gfx_width(void) {
    return boot_info->width;
}

int gfx_height(void) {
    return boot_info->height;
}

uint32_t gfx_get_pixel(int x, int y) {
    return backbuffer[y * gfx_width() + x];
}

void gfx_put_pixel(int x, int y, uint32_t rgb) {
    backbuffer[y * gfx_width() + x] = rgb;
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

/* Writes bytes_per_pixel bytes per pixel rather than always writing a
 * fixed 32-bit word: VBE mode 0x112 was assumed to be 32bpp (matching its
 * standard VESA definition) but this BIOS/QEMU's ModeInfoBlock actually
 * reports BitsPerPixel=24 (confirmed by BytesPerScanLine = 640*3, not
 * 640*4) -- a fixed 4-byte write at a 3-bytes/pixel stride corrupted
 * every pixel, each write bleeding into the next one's bytes. Reading
 * boot_info->bpp instead of hardcoding 4 makes this correct for whatever
 * the BIOS actually handed back. */
void gfx_present(void) {
    int x, y;
    int w = gfx_width();
    int h = gfx_height();
    int bytes_per_pixel = boot_info->bpp / 8;
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)boot_info->framebuffer_addr;

    for (y = 0; y < h; y++) {
        volatile uint8_t *row = fb + (uint32_t)y * boot_info->pitch;
        for (x = 0; x < w; x++) {
            uint32_t color = backbuffer[y * w + x];
            volatile uint8_t *pixel = row + x * bytes_per_pixel;

            pixel[0] = (uint8_t)(color & 0xFF);
            pixel[1] = (uint8_t)((color >> 8) & 0xFF);
            pixel[2] = (uint8_t)((color >> 16) & 0xFF);
            if (bytes_per_pixel >= 4) {
                pixel[3] = 0;
            }
        }
    }
}
