/* A single-line text input: a box that can be focused by clicking it, and
 * only consumes keystrokes while focused -- the panel's old always-on
 * "TYPE:" line accepted every keystroke unconditionally, which stops being
 * sane the moment more than one thing on screen could want the keyboard. */

#include "textfield.h"
#include "graphics.h"
#include "text.h"

/* Same androidacid.com-derived palette as button.c/window.c. Unfocused
 * border is a dim, desaturated green rather than the full --hard accent --
 * focus needs to read as a real state change, not just a hover flicker. */
#define TEXTFIELD_BORDER_COLOR 0x00FF66      /* --hard, focused */
#define TEXTFIELD_BORDER_COLOR_IDLE 0x1F2E27 /* dim, unfocused */
#define TEXTFIELD_FILL_COLOR 0x0A1A12        /* same as button idle fill */
#define TEXTFIELD_TEXT_COLOR 0xD4E6DB        /* --text at 0.9 alpha over --bg */
#define TEXTFIELD_CARET_COLOR 0x00FF66       /* --hard */
#define TEXTFIELD_LABEL_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define CARET_WIDTH 2
#define TEXT_PAD_X 4

int textfield_hit_test(const struct textfield *tf, int px, int py) {
    return px >= tf->x && px < tf->x + tf->w && py >= tf->y && py < tf->y + tf->h;
}

void textfield_feed_char(struct textfield *tf, char c) {
    if (!tf->focused) {
        return;
    }
    if (c == '\b') {
        if (tf->len > 0) {
            tf->text[--tf->len] = 0;
        }
    } else if (c == '\n') {
        tf->len = 0;
        tf->text[0] = 0;
    } else if (tf->len < TEXTFIELD_MAX) {
        tf->text[tf->len++] = c;
        tf->text[tf->len] = 0;
    }
}

void textfield_draw(const struct textfield *tf) {
    uint32_t border = tf->focused ? TEXTFIELD_BORDER_COLOR : TEXTFIELD_BORDER_COLOR_IDLE;
    int text_x = tf->x + TEXT_PAD_X;
    int text_y = tf->y + (tf->h - GLYPH_HEIGHT * TEXTFIELD_LABEL_SCALE) / 2;

    gfx_fill_rect(tf->x - 1, tf->y - 1, tf->w + 2, tf->h + 2, border);
    gfx_fill_rect(tf->x, tf->y, tf->w, tf->h, TEXTFIELD_FILL_COLOR);
    text_puts(text_x, text_y, tf->text, TEXTFIELD_TEXT_COLOR, TEXTFIELD_LABEL_SCALE);

    if (tf->focused) {
        int caret_x = text_x + text_width(tf->text, TEXTFIELD_LABEL_SCALE) + 1;
        gfx_fill_rect(caret_x, tf->y + 2, CARET_WIDTH, tf->h - 4, TEXTFIELD_CARET_COLOR);
    }
}
