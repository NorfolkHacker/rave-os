/* kernel/tests/test_font.c -- host-side only, never linked into
 * kernel.bin. font.c has no freestanding-only dependencies, so it
 * compiles and runs natively here exactly as it will inside kernel.bin.
 * Guards against a glyph silently falling through font_glyph()'s
 * default case (which renders as blank/invisible, indistinguishable
 * from a real space) -- see docs/IDEAS.md's 2026-08-20 entry for how
 * '?' was found missing this way. */
#include <stdio.h>
#include <string.h>
#include "../gfx/font.h"

static int glyph_is_blank(const char *const *g) {
    int row;
    for (row = 0; row < 7; row++) {
        if (strchr(g[row], 'X') != NULL) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    /* Every printable, non-space character this font is expected to
     * cover -- if any of these silently falls through to the blank
     * default glyph, that's the bug this test exists to catch. */
    static const char covered[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        ":;-!.,'><%=+*/?@()";
    unsigned int i;

    for (i = 0; i < sizeof(covered) - 1; i++) {
        const char *const *g = font_glyph(covered[i]);
        if (glyph_is_blank(g)) {
            printf("FAIL: font_glyph('%c') is blank -- missing from the switch\n", covered[i]);
            return 1;
        }
    }

    if (!glyph_is_blank(font_glyph(' '))) {
        printf("FAIL: font_glyph(' ') should be blank\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
