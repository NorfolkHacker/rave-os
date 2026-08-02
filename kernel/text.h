#ifndef RAVEOS_TEXT_H
#define RAVEOS_TEXT_H

#include <stdint.h>

/* Pixel width a string would render at, at the given integer scale (each
 * font dot becomes a scale x scale block). Same formula as ACIDSTORM's
 * gfx_text_width: 5 dots/glyph + 1 dot spacing, minus the trailing
 * spacing after the last character. */
int text_width(const char *s, int scale);

void text_puts(int x, int y, const char *s, uint32_t rgb, int scale);

#endif
