#ifndef RAVEOS_EDITOR_H
#define RAVEOS_EDITOR_H

/* One sector's worth -- matches VIEWER_BUF_SIZE/SHELL_CAT_BUF_SIZE's
 * existing "every real file here is tiny" sizing (kernel.c, shell.c).
 * buf itself is EDITOR_BUF_SIZE + 1 bytes so a full buffer can still
 * carry a terminating nul. */
#define EDITOR_BUF_SIZE 512

struct editor {
    int x, y, w, h;
    char buf[EDITOR_BUF_SIZE + 1];
    unsigned int len;    /* bytes currently used in buf, 0..EDITOR_BUF_SIZE */
    unsigned int cursor; /* byte offset into buf, 0..len */
    int focused;         /* only a focused editor consumes keystrokes, same convention console_input.c already uses */
};

void editor_init(struct editor *ed, int x, int y, int w, int h);

/* Loads text into the buffer, truncated to fit if longer than
 * EDITOR_BUF_SIZE (same "truncate rather than overflow" guarantee every
 * bounded buffer in this codebase already gives). Cursor moves to the
 * end of whatever loaded. Also clears focused to 0, same as
 * console_input_clear() -- a freshly loaded buffer isn't implicitly
 * focused. */
void editor_set_text(struct editor *ed, const char *text, unsigned int size);

/* Same effect as editor_set_text(ed, "", 0) -- for opening a path that
 * doesn't exist yet. */
void editor_clear(struct editor *ed);

/* Feeds one character: a printable character inserts at ed->cursor,
 * shifting every byte from cursor to len right by one; '\b' removes the
 * byte before the cursor (shifting left), merging two lines if it
 * removes a '\n'; '\n' inserts a literal newline at the cursor via the
 * same insert-and-shift path a printable character uses -- a real
 * mid-buffer split, not an append, and NOT "submit" (unlike
 * console_input_feed_char(), nothing is ever submitted here; SAVE is a
 * separate, explicit button). A no-op if unfocused, or if inserting
 * would grow len past EDITOR_BUF_SIZE. Callers are expected to
 * intercept KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT (see keyboard.h)
 * themselves via editor_move_line()/editor_move_cursor() before calling
 * this -- it only understands text, '\b', and '\n'. */
void editor_feed_char(struct editor *ed, char c);

/* Moves the cursor by delta bytes, clamped to [0, len] -- for KEY_LEFT
 * (-1) and KEY_RIGHT (+1). */
void editor_move_cursor(struct editor *ed, int delta);

/* Moves the cursor up (delta -1) or down (delta +1) one line, landing on
 * the same column if that line is long enough, or that line's own end
 * if it's shorter -- for KEY_UP/KEY_DOWN. A no-op moving up from the
 * first line or down from the last. */
void editor_move_line(struct editor *ed, int delta);

/* Moves the cursor to the start of its current line -- for KEY_HOME. */
void editor_move_home(struct editor *ed);

/* Moves the cursor to the end of its current line (just before that
 * line's '\n', or ed->len on the last line) -- for KEY_END. */
void editor_move_end(struct editor *ed);

/* Deletes the byte at the cursor, shifting everything after it left by
 * one -- mirror image of editor_feed_char()'s '\b' branch, which
 * deletes the byte *before* the cursor instead. Deleting a '\n' merges
 * the following line into the current one. A no-op at the end of the
 * buffer. For KEY_DELETE. */
void editor_delete_forward(struct editor *ed);

int editor_hit_test(const struct editor *ed, int px, int py);

void editor_draw(const struct editor *ed);

#endif
