#ifndef RAVEOS_GRAPHICS_H
#define RAVEOS_GRAPHICS_H

#include <stdint.h>

int gfx_width(void);
int gfx_height(void);
void gfx_put_pixel(int x, int y, uint32_t rgb);
void gfx_clear(uint32_t rgb);

#endif
