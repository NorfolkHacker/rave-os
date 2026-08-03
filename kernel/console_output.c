/* No box/border of its own -- unlike console_input.c's input line, the
 * scrollback pane just renders text straight onto the window's body
 * (same convention draw_info_group() already uses for the info window's
 * plain text), since the window body itself already reads as the
 * pane's background. */

#include "console_output.h"
#include "text.h"

#define CONSOLE_OUTPUT_TEXT_COLOR 0xD4E6DB
#define CONSOLE_OUTPUT_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define LINE_HEIGHT (GLYPH_HEIGHT * CONSOLE_OUTPUT_SCALE + 2)
#define TEXT_PAD_X 4

void console_output_init(struct console_output *co, int x, int y, int w, int h) {
    co->x = x;
    co->y = y;
    co->w = w;
    co->h = h;
    co->line_count = 0;
    co->next_line = 0;
    co->generation = 0;
}

void console_output_append_line(struct console_output *co, const char *text) {
    char *dst = co->lines[co->next_line];
    int i = 0;

    while (text[i] && i < CONSOLE_LINE_MAX) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = 0;

    co->next_line = (co->next_line + 1) % CONSOLE_OUTPUT_MAX_LINES;
    if (co->line_count < CONSOLE_OUTPUT_MAX_LINES) {
        co->line_count++;
    }
    co->generation++;
}

/* No clip/scissor primitive exists anywhere in graphics.h (gfx_put_pixel
 * has never bounds-checked) -- self-clipping by only ever computing
 * y-coordinates already proven to land inside [co->y, co->y + co->h) is
 * the only option this graphics layer offers, not a shortcut. */
void console_output_draw(const struct console_output *co) {
    int visible_rows = co->h / LINE_HEIGHT;
    int shown = co->line_count < visible_rows ? co->line_count : visible_rows;
    int i;

    for (i = 0; i < shown; i++) {
        int offset_from_newest = shown - 1 - i; /* 0 = newest (bottom row) */
        int ring_idx = (co->next_line - 1 - offset_from_newest + 2 * CONSOLE_OUTPUT_MAX_LINES) %
                       CONSOLE_OUTPUT_MAX_LINES;

        text_puts(co->x + TEXT_PAD_X, co->y + i * LINE_HEIGHT, co->lines[ring_idx], CONSOLE_OUTPUT_TEXT_COLOR,
                  CONSOLE_OUTPUT_SCALE);
    }
}
