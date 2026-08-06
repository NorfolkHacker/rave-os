#include "console_input.h"
#include "graphics.h"
#include "text.h"

/* Same androidacid.com-derived palette as textfield.c. */
#define CONSOLE_INPUT_BORDER_COLOR 0x00FF66      /* --hard, focused */
#define CONSOLE_INPUT_BORDER_COLOR_IDLE 0x1F2E27 /* dim, unfocused */
#define CONSOLE_INPUT_FILL_COLOR 0x0A1A12
#define CONSOLE_INPUT_TEXT_COLOR 0xD4E6DB
#define CONSOLE_INPUT_CARET_COLOR 0x00FF66
#define CONSOLE_INPUT_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define CARET_WIDTH 2
#define TEXT_PAD_X 4
#define CONSOLE_INPUT_CORNER_RADIUS 4

int console_input_hit_test(const struct console_input *ci, int px, int py) {
    return px >= ci->x && px < ci->x + ci->w && py >= ci->y && py < ci->y + ci->h;
}

int console_input_feed_char(struct console_input *ci, char c) {
    int i;

    if (!ci->focused) {
        return 0;
    }
    if (c == '\n') {
        return 1;
    }
    if (c == '\b') {
        if (ci->cursor > 0) {
            for (i = ci->cursor - 1; i < ci->len - 1; i++) {
                ci->text[i] = ci->text[i + 1];
            }
            ci->len--;
            ci->cursor--;
            ci->text[ci->len] = 0;
        }
    } else if (ci->len < CONSOLE_INPUT_MAX) {
        for (i = ci->len; i > ci->cursor; i--) {
            ci->text[i] = ci->text[i - 1];
        }
        ci->text[ci->cursor] = c;
        ci->len++;
        ci->cursor++;
        ci->text[ci->len] = 0;
    }
    return 0;
}

void console_input_move_cursor(struct console_input *ci, int delta) {
    int c = ci->cursor + delta;
    if (c < 0) {
        c = 0;
    }
    if (c > ci->len) {
        c = ci->len;
    }
    ci->cursor = c;
}

void console_input_set_text(struct console_input *ci, const char *text) {
    int i = 0;
    while (text[i] && i < CONSOLE_INPUT_MAX) {
        ci->text[i] = text[i];
        i++;
    }
    ci->text[i] = 0;
    ci->len = i;
    ci->cursor = i;
}

void console_input_clear(struct console_input *ci) {
    ci->len = 0;
    ci->cursor = 0;
    ci->text[0] = 0;
}

void console_input_draw(const struct console_input *ci) {
    uint32_t border = ci->focused ? CONSOLE_INPUT_BORDER_COLOR : CONSOLE_INPUT_BORDER_COLOR_IDLE;
    int text_x = ci->x + TEXT_PAD_X;
    int text_y = ci->y + (ci->h - GLYPH_HEIGHT * CONSOLE_INPUT_SCALE) / 2;

    gfx_fill_rounded_rect(ci->x - 1, ci->y - 1, ci->w + 2, ci->h + 2, CONSOLE_INPUT_CORNER_RADIUS, border);
    gfx_fill_rounded_rect(ci->x, ci->y, ci->w, ci->h, CONSOLE_INPUT_CORNER_RADIUS, CONSOLE_INPUT_FILL_COLOR);
    text_puts(text_x, text_y, ci->text, CONSOLE_INPUT_TEXT_COLOR, CONSOLE_INPUT_SCALE);

    if (ci->focused) {
        /* The caret sits at ci->cursor, not always at the end of the
         * text -- measure the width of just the prefix up to the
         * cursor (reusing text_width() rather than duplicating its
         * per-glyph advance formula here). */
        char prefix[CONSOLE_INPUT_MAX + 1];
        int i;
        for (i = 0; i < ci->cursor; i++) {
            prefix[i] = ci->text[i];
        }
        prefix[i] = 0;
        int caret_x = text_x + text_width(prefix, CONSOLE_INPUT_SCALE) + 1;
        gfx_fill_rect(caret_x, ci->y + 2, CARET_WIDTH, ci->h - 4, CONSOLE_INPUT_CARET_COLOR);
    }
}
