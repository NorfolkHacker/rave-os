#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define FB_W 320
#define FB_H 240

/* 0xAARRGGBB */
typedef uint32_t Color;

#define RGB(r,g,b) ((Color)(0xFF000000u | ((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b)))

extern Color g_fb[FB_H][FB_W];

void gfx_clear(Color c);
void gfx_put(int x, int y, Color c);
void gfx_fill_rect(int x, int y, int w, int h, Color c);
void gfx_rect(int x, int y, int w, int h, Color c);
void gfx_line(int x0, int y0, int x1, int y1, Color c);
void gfx_circle(int cx, int cy, int r, Color c);
void gfx_fill_circle(int cx, int cy, int r, Color c);

/* text, 5x7 glyphs, `scale` = pixel block size (1 = native 5x7) */
void gfx_text(int x, int y, const char *s, Color c, int scale);
int  gfx_text_width(const char *s, int scale);

/* acid plasma background, t = seconds */
void gfx_plasma(double t);

Color hsv_to_rgb(double h, double s, double v);

#endif
