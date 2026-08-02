#ifndef FONT_H
#define FONT_H

/* Returns 7 row-strings (5 chars each, 'X'=pixel on, '.'=off) for a glyph,
 * or NULL if the character has no glyph (rendered blank). */
const char *const *font_glyph(char c);

#endif
