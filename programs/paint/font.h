#ifndef RAVEOS_PAINT_FONT_H
#define RAVEOS_PAINT_FONT_H

/* Returns 7 row-strings (5 chars each, 'X'=pixel on, '.'=off) for a
 * glyph. Never returns NULL -- font.c's default case falls back to a
 * blank space glyph for any character with no dedicated table entry,
 * so every call returns a valid glyph pointer. */
const char *const *font_glyph(char c);

#endif
