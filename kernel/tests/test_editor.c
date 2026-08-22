/* kernel/tests/test_editor.c -- host-side only, never linked into
 * kernel.bin. editor.c's buffer logic (feed_char/move_cursor/move_line/
 * move_home/move_end/delete_forward) has no freestanding-only
 * dependencies and compiles natively here exactly as it will inside
 * kernel.bin -- only editor_draw() calls into the real graphics/text
 * pipeline, so those three functions are stubbed below purely to let
 * the link succeed; no test in this file calls editor_draw(). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../gui/editor.h"

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    (void)x; (void)y; (void)w; (void)h; (void)rgb;
}

int text_width(const char *s, int scale) {
    (void)s; (void)scale;
    return 0;
}

void text_puts(int x, int y, const char *s, uint32_t rgb, int scale) {
    (void)x; (void)y; (void)s; (void)rgb; (void)scale;
}

int main(void) {
    struct editor ed;

    /* editor_move_home: single line, cursor mid-line -> column 0. */
    editor_init(&ed, 0, 0, 100, 100);
    editor_set_text(&ed, "hello", 5);
    ed.cursor = 3;
    editor_move_home(&ed);
    if (ed.cursor != 0) {
        printf("FAIL move_home (single line): expected cursor 0, got %u\n", ed.cursor);
        return 1;
    }

    /* editor_move_home: multi-line, cursor on the middle line -> start
     * of that line, not the whole buffer. */
    editor_set_text(&ed, "abc\ndefgh\nij", 12);
    ed.cursor = 7; /* inside "defgh", the middle line (starts at offset 4) */
    editor_move_home(&ed);
    if (ed.cursor != 4) {
        printf("FAIL move_home (multi-line): expected cursor 4, got %u\n", ed.cursor);
        return 1;
    }

    /* editor_move_end: single line, cursor mid-line -> end of buffer. */
    editor_set_text(&ed, "hello", 5);
    ed.cursor = 2;
    editor_move_end(&ed);
    if (ed.cursor != 5) {
        printf("FAIL move_end (single line): expected cursor 5, got %u\n", ed.cursor);
        return 1;
    }

    /* editor_move_end: multi-line, cursor on the middle line -> just
     * before that line's '\n', not the whole buffer's end. */
    editor_set_text(&ed, "abc\ndefgh\nij", 12);
    ed.cursor = 5; /* inside "defgh" */
    editor_move_end(&ed);
    if (ed.cursor != 9) {
        printf("FAIL move_end (multi-line): expected cursor 9, got %u\n", ed.cursor);
        return 1;
    }

    /* editor_move_end: last line has no trailing '\n' -> end of buffer. */
    editor_set_text(&ed, "abc\ndefgh\nij", 12);
    ed.cursor = 10; /* inside "ij", the last line */
    editor_move_end(&ed);
    if (ed.cursor != 12) {
        printf("FAIL move_end (last line): expected cursor 12, got %u\n", ed.cursor);
        return 1;
    }

    /* editor_delete_forward: removes the byte at cursor, shifts left,
     * cursor itself doesn't move. */
    editor_set_text(&ed, "hello", 5);
    ed.cursor = 1;
    editor_delete_forward(&ed);
    if (ed.len != 4 || strcmp(ed.buf, "hllo") != 0 || ed.cursor != 1) {
        printf("FAIL delete_forward (mid-buffer): got buf=\"%s\" len=%u cursor=%u\n",
               ed.buf, ed.len, ed.cursor);
        return 1;
    }

    /* editor_delete_forward: deleting the '\n' at cursor merges the
     * following line into the current one. */
    editor_set_text(&ed, "abc\ndef", 7);
    ed.cursor = 3; /* sits on the '\n' */
    editor_delete_forward(&ed);
    if (ed.len != 6 || strcmp(ed.buf, "abcdef") != 0) {
        printf("FAIL delete_forward (merge lines): got buf=\"%s\" len=%u\n", ed.buf, ed.len);
        return 1;
    }

    /* editor_delete_forward: no-op at end of buffer. */
    editor_set_text(&ed, "hello", 5);
    ed.cursor = 5;
    editor_delete_forward(&ed);
    if (ed.len != 5 || strcmp(ed.buf, "hello") != 0 || ed.cursor != 5) {
        printf("FAIL delete_forward (end of buffer): got buf=\"%s\" len=%u cursor=%u\n",
               ed.buf, ed.len, ed.cursor);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
