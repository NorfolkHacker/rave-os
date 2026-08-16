/* A multi-line, freely-cursor-navigable text buffer -- the widget behind
 * Rave-OS's in-OS text editor (WIN_KIND_EDITOR in kernel.c, opened via
 * SHELL's EDIT command). Mirrors console_input.c's insert-at-cursor
 * shape (see its own feed_char/move_cursor), generalized from "always a
 * single line" to "any number of lines, arrow keys move freely within
 * them". Knows nothing about the filesystem -- kernel.c's SHELL
 * interception owns loading/saving. */

#include "editor.h"
#include "graphics.h"
#include "text.h"

#define EDITOR_BG_COLOR 0x0A1A12
#define EDITOR_TEXT_COLOR 0xD4E6DB
#define EDITOR_CARET_COLOR 0x00FF66
#define EDITOR_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define LINE_HEIGHT (GLYPH_HEIGHT * EDITOR_SCALE + 2)
#define TEXT_PAD_X 4
#define TEXT_PAD_Y 4
#define CARET_WIDTH 2

void editor_init(struct editor *ed, int x, int y, int w, int h) {
    ed->x = x;
    ed->y = y;
    ed->w = w;
    ed->h = h;
    editor_clear(ed);
}

void editor_set_text(struct editor *ed, const char *text, unsigned int size) {
    unsigned int i;
    if (size > EDITOR_BUF_SIZE) {
        size = EDITOR_BUF_SIZE;
    }
    for (i = 0; i < size; i++) {
        ed->buf[i] = text[i];
    }
    ed->buf[size] = 0;
    ed->len = size;
    ed->cursor = size;
    ed->focused = 0;
}

void editor_clear(struct editor *ed) {
    ed->buf[0] = 0;
    ed->len = 0;
    ed->cursor = 0;
    ed->focused = 0;
}

void editor_feed_char(struct editor *ed, char c) {
    unsigned int i;

    if (!ed->focused) {
        return;
    }
    if (c == '\b') {
        if (ed->cursor > 0) {
            for (i = ed->cursor - 1; i < ed->len - 1; i++) {
                ed->buf[i] = ed->buf[i + 1];
            }
            ed->len--;
            ed->cursor--;
            ed->buf[ed->len] = 0;
        }
        return;
    }
    /* Printable characters and '\n' (a real inserted newline, not
     * "submit" -- this widget has no submit concept) share the same
     * insert-and-shift path. */
    if (ed->len < EDITOR_BUF_SIZE) {
        for (i = ed->len; i > ed->cursor; i--) {
            ed->buf[i] = ed->buf[i - 1];
        }
        ed->buf[ed->cursor] = c;
        ed->len++;
        ed->cursor++;
        ed->buf[ed->len] = 0;
    }
}

void editor_move_cursor(struct editor *ed, int delta) {
    int c = (int)ed->cursor + delta;
    if (c < 0) {
        c = 0;
    }
    if (c > (int)ed->len) {
        c = (int)ed->len;
    }
    ed->cursor = (unsigned int)c;
}

/* Scans backward from off for the start of the line off sits on (the
 * byte right after the nearest preceding '\n', or 0 if there is none). */
static unsigned int editor_line_start(const struct editor *ed, unsigned int off) {
    while (off > 0 && ed->buf[off - 1] != '\n') {
        off--;
    }
    return off;
}

/* Scans forward from off for the end of the line off sits on (the
 * nearest '\n', or ed->len if there is none). */
static unsigned int editor_line_end(const struct editor *ed, unsigned int off) {
    while (off < ed->len && ed->buf[off] != '\n') {
        off++;
    }
    return off;
}

void editor_move_line(struct editor *ed, int delta) {
    unsigned int line_start = editor_line_start(ed, ed->cursor);
    unsigned int col = ed->cursor - line_start;
    unsigned int target_start;
    unsigned int target_end;

    if (delta < 0) {
        if (line_start == 0) {
            return; /* already on the first line */
        }
        target_start = editor_line_start(ed, line_start - 1);
    } else {
        unsigned int line_end = editor_line_end(ed, ed->cursor);
        if (line_end == ed->len) {
            return; /* already on the last line */
        }
        target_start = line_end + 1; /* just past the '\n' */
    }

    target_end = editor_line_end(ed, target_start);
    if (target_start + col > target_end) {
        ed->cursor = target_end;
    } else {
        ed->cursor = target_start + col;
    }
}

int editor_hit_test(const struct editor *ed, int px, int py) {
    return px >= ed->x && px < ed->x + ed->w && py >= ed->y && py < ed->y + ed->h;
}

void editor_draw(const struct editor *ed) {
    int row = 0;
    unsigned int i = 0;
    unsigned int line_start = 0;
    int caret_drawn = 0;

    gfx_fill_rect(ed->x, ed->y, ed->w, ed->h, EDITOR_BG_COLOR);

    while (i <= ed->len) {
        if (i == ed->len || ed->buf[i] == '\n') {
            char line[EDITOR_BUF_SIZE + 1];
            unsigned int line_len = i - line_start;
            unsigned int j;
            int row_y = ed->y + TEXT_PAD_Y + row * LINE_HEIGHT;

            for (j = 0; j < line_len; j++) {
                line[j] = ed->buf[line_start + j];
            }
            line[line_len] = 0;

            /* Rows past the widget's own height are simply not drawn --
             * same silent-truncation convention draw_files_group()'s
             * own row clip already uses in kernel.c, no scrolling in
             * v1. */
            if (row_y + LINE_HEIGHT <= ed->y + ed->h) {
                text_puts(ed->x + TEXT_PAD_X, row_y, line, EDITOR_TEXT_COLOR, EDITOR_SCALE);

                if (ed->focused && !caret_drawn && ed->cursor >= line_start && ed->cursor <= i) {
                    char prefix[EDITOR_BUF_SIZE + 1];
                    unsigned int k;
                    unsigned int prefix_len = ed->cursor - line_start;
                    int caret_x;

                    for (k = 0; k < prefix_len; k++) {
                        prefix[k] = ed->buf[line_start + k];
                    }
                    prefix[prefix_len] = 0;
                    caret_x = ed->x + TEXT_PAD_X + text_width(prefix, EDITOR_SCALE) + 1;
                    gfx_fill_rect(caret_x, row_y - 1, CARET_WIDTH, LINE_HEIGHT - 1, EDITOR_CARET_COLOR);
                    caret_drawn = 1;
                }
            }

            row++;
            line_start = i + 1;
        }
        i++;
    }
}
