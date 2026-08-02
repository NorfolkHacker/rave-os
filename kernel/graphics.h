#ifndef RAVEOS_GRAPHICS_H
#define RAVEOS_GRAPHICS_H

#include <stdint.h>

/* Zeroes the backbuffer. Call once before any drawing. */
void gfx_init(void);

int gfx_width(void);
int gfx_height(void);

/* All drawing (gfx_put_pixel, gfx_fill_rect, gfx_clear, and text_puts on
 * top of them) targets the offscreen backbuffer, not the real screen --
 * nothing is visible until gfx_present() copies it out. */
uint32_t gfx_get_pixel(int x, int y);
void gfx_put_pixel(int x, int y, uint32_t rgb);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_clear(uint32_t rgb);

/* Copies the entire backbuffer to the real VBE framebuffer, converting
 * from the backbuffer's fixed 32-bit packed-RGB format to whatever pixel
 * format boot_info->bpp actually says the hardware wants. This is the
 * only place that needs to know about hardware pixel format at all. */
void gfx_present(void);

#endif
