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

/* Which corners gfx_fill_rounded_rect_ex() should actually round -- a
 * corner not in the mask is filled square instead. Lets a caller round
 * only the outer silhouette of a shape assembled from several adjoining
 * rects (e.g. window.c's titlebar+body) without rounding the internal
 * seam between them. */
#define GFX_CORNER_TL 0x1
#define GFX_CORNER_TR 0x2
#define GFX_CORNER_BL 0x4
#define GFX_CORNER_BR 0x8
#define GFX_CORNER_ALL (GFX_CORNER_TL | GFX_CORNER_TR | GFX_CORNER_BL | GFX_CORNER_BR)

/* Same as gfx_fill_rect(), but with a circular cut at each corner in
 * `corners`, of `r` pixels' radius (clamped to half the shape's smaller
 * dimension). No float/sqrt -- each corner's RxR block is tested
 * pixel-by-pixel against the integer circle equation. Pixels outside the
 * circle in a rounded corner are left untouched (not painted over), so
 * whatever was already drawn underneath -- typically a border color laid
 * down first -- shows through as the rounding. */
void gfx_fill_rounded_rect_ex(int x, int y, int w, int h, int r, int corners, uint32_t rgb);
void gfx_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t rgb);

/* Copies the entire backbuffer to the real VBE framebuffer, converting
 * from the backbuffer's fixed 32-bit packed-RGB format to whatever pixel
 * format boot_info->bpp actually says the hardware wants. This is the
 * only place that needs to know about hardware pixel format at all. */
void gfx_present(void);

/* Same as gfx_present(), but only for the given rectangle -- callers that
 * know only part of the screen actually changed (see kernel.c's damage
 * tracking) can present just that instead of paying for the whole
 * screen every frame. Clips to the screen bounds itself, so callers
 * don't need to. */
void gfx_present_rect(int x, int y, int w, int h);

#endif
