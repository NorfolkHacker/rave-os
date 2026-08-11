#ifndef RAVEOS_CONSOLE_OUTPUT_H
#define RAVEOS_CONSOLE_OUTPUT_H

/* A scrolling text pane: the Forth console's scrollback. Fixed-size ring
 * buffer of lines (no malloc anywhere in this kernel) -- once
 * CONSOLE_OUTPUT_MAX_LINES lines have been appended, the oldest is
 * overwritten rather than the buffer growing. There's no scrollbar or
 * scroll-back UI yet (nothing to scroll to beyond what's already
 * visible), so draw() only ever needs the tail: the most recent lines
 * that fit in the pane's height. */
#define CONSOLE_OUTPUT_MAX_LINES 40
#define CONSOLE_LINE_MAX 64

struct console_output {
    int x, y, w, h;
    char lines[CONSOLE_OUTPUT_MAX_LINES][CONSOLE_LINE_MAX + 1];
    int line_count; /* how many of lines[] currently hold real content, capped at MAX_LINES */
    int next_line;  /* ring index the next append will write to */
    /* Bumped on every append. update_and_present()'s damage tracking
     * compares this one int instead of diffing 40*64 bytes of text every
     * mouse packet -- the same "don't do wasted work every event"
     * discipline the erratic-mouse ring-buffer bug (see BUILD_LOG)
     * already taught this project to take seriously. */
    int generation;
};

void console_output_init(struct console_output *co, int x, int y, int w, int h);

/* Appends one line, truncating to CONSOLE_LINE_MAX chars if needed --
 * there's no wrapping, a line either fits or is cut short. Overwrites the
 * oldest line once the ring is full. */
void console_output_append_line(struct console_output *co, const char *text);

/* Resets line_count/next_line so a caller can start showing unrelated
 * content (e.g. a different file's contents) instead of appending onto
 * what's already there. Bumps generation rather than resetting it to 0 --
 * append_line()'s "generation only ever increases, so any change is
 * detectable as != " invariant has to keep holding even across a clear,
 * or two same-length pieces of content shown back to back could
 * coincidentally land on the same generation and silently skip a
 * redraw. */
void console_output_clear(struct console_output *co);

void console_output_draw(const struct console_output *co);

#endif
