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
    if (!ci->focused) {
        return 0;
    }
    if (c == '\n') {
        return 1;
    }
    if (c == '\b') {
        if (ci->len > 0) {
            ci->text[--ci->len] = 0;
        }
    } else if (ci->len < CONSOLE_INPUT_MAX) {
        ci->text[ci->len++] = c;
        ci->text[ci->len] = 0;
    }
    return 0;
}

void console_input_clear(struct console_input *ci) {
    ci->len = 0;
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
        int caret_x = text_x + text_width(ci->text, CONSOLE_INPUT_SCALE) + 1;
        gfx_fill_rect(caret_x, ci->y + 2, CARET_WIDTH, ci->h - 4, CONSOLE_INPUT_CARET_COLOR);
    }
}
