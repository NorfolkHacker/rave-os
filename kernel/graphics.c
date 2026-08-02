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

void gfx_fill_rounded_rect_ex(int x, int y, int w, int h, int r, int corners, uint32_t rgb) {
    int i, j;

    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    if (r < 0) {
        r = 0;
    }

    /* Everything except the four RxR corner blocks -- always a plain
     * rectangle regardless of which corners end up rounded. */
    if (h - 2 * r > 0) {
        gfx_fill_rect(x, y + r, w, h - 2 * r, rgb);
    }
    if (r > 0) {
        gfx_fill_rect(x + r, y, w - 2 * r, r, rgb);
        gfx_fill_rect(x + r, y + h - r, w - 2 * r, r, rgb);
    }

    for (j = 0; j < r; j++) {
        for (i = 0; i < r; i++) {
            /* Distance from this pixel to the corner circle's center,
             * which sits one pixel in from the outer edge on each axis. */
            int dx = r - 1 - i;
            int dy = r - 1 - j;
            int inside = (dx * dx + dy * dy) <= r * r;

            if (!(corners & GFX_CORNER_TL) || inside) {
                gfx_put_pixel(x + i, y + j, rgb);
            }
            if (!(corners & GFX_CORNER_TR) || inside) {
                gfx_put_pixel(x + w - 1 - i, y + j, rgb);
            }
            if (!(corners & GFX_CORNER_BL) || inside) {
                gfx_put_pixel(x + i, y + h - 1 - j, rgb);
            }
            if (!(corners & GFX_CORNER_BR) || inside) {
                gfx_put_pixel(x + w - 1 - i, y + h - 1 - j, rgb);
            }
        }
    }
}

void gfx_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t rgb) {
    gfx_fill_rounded_rect_ex(x, y, w, h, r, GFX_CORNER_ALL, rgb);
}

void gfx_present(void) {
    gfx_present_rect(0, 0, gfx_width(), gfx_height());
}

/* Writes bytes_per_pixel bytes per pixel rather than always writing a
 * fixed 32-bit word: VBE mode 0x112 was assumed to be 32bpp (matching its
 * standard VESA definition) but this BIOS/QEMU's ModeInfoBlock actually
 * reports BitsPerPixel=24 (confirmed by BytesPerScanLine = 640*3, not
 * 640*4) -- a fixed 4-byte write at a 3-bytes/pixel stride corrupted
 * every pixel, each write bleeding into the next one's bytes. Reading
 * boot_info->bpp instead of hardcoding 4 makes this correct for whatever
 * the BIOS actually handed back.
 *
 * Clips (x,y,w,h) to the screen before blitting, rather than trusting the
 * caller -- kernel.c's damage rect is built from cursor/window positions
 * that are clamped on-screen by their own callers, but a present function
 * that only behaves correctly for in-range input is a landmine for
 * whatever uses it next. */
void gfx_present_rect(int x, int y, int w, int h) {
    int px, py;
    int fb_w = gfx_width();
    int fb_h = gfx_height();
    int bytes_per_pixel = boot_info->bpp / 8;
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)boot_info->framebuffer_addr;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb_w ? fb_w : x + w;
    int y1 = y + h > fb_h ? fb_h : y + h;

    for (py = y0; py < y1; py++) {
        volatile uint8_t *row = fb + (uint32_t)py * boot_info->pitch;
        for (px = x0; px < x1; px++) {
            uint32_t color = backbuffer[py * fb_w + px];
            volatile uint8_t *pixel = row + px * bytes_per_pixel;

            pixel[0] = (uint8_t)(color & 0xFF);
            pixel[1] = (uint8_t)((color >> 8) & 0xFF);
            pixel[2] = (uint8_t)((color >> 16) & 0xFF);
            if (bytes_per_pixel >= 4) {
                pixel[3] = 0;
            }
        }
    }
}
