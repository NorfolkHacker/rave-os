/* Draws text into the graphics-mode framebuffer using font.c's 5x7
 * glyphs, following the exact layout ACIDSTORM's gfx_text/gfx_text_width
 * use (demos/acidstorm/src/gfx.c) so text sized "scale 2" here and
 * "scale 2" there look the same: each lit glyph dot becomes a
 * scale x scale block, and characters advance 6*scale pixels
 * (5 wide + 1 spacing). */

#include "text.h"
#include "font.h"
#include "graphics.h"

static int strlen_(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int text_width(const char *s, int scale) {
    int n = strlen_(s);
    if (n == 0) {
        return 0;
    }
    return n * 6 * scale - scale;
}

void text_puts(int x, int y, const char *s, uint32_t rgb, int scale) {
    int cx = x;
    const char *p;

    for (p = s; *p; p++) {
        const char *const *glyph = font_glyph(*p);
        if (glyph) {
            int row, col;
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (glyph[row][col] == 'X') {
                        gfx_fill_rect(cx + col * scale, y + row * scale, scale, scale, rgb);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}
