#ifndef RAVEOS_CONSOLE_HISTORY_H
#define RAVEOS_CONSOLE_HISTORY_H

#include "console_input.h"

/* Command history for the Forth console's input line -- a fixed-size
 * ring buffer of past submitted lines (no malloc anywhere in this
 * kernel), same style as console_output.c's scrollback ring. Knows
 * nothing about GUI/window code, same "no dependencies beyond its own
 * concern" discipline as forth.c and console_output.c. */
#define CONSOLE_HISTORY_MAX 16

struct console_history {
    char lines[CONSOLE_HISTORY_MAX][CONSOLE_INPUT_MAX + 1];
    int count;  /* valid entries so far, capped at CONSOLE_HISTORY_MAX */
    int next;   /* ring index the next push() writes to */

    /* -1 = not currently browsing (nothing recalled); otherwise how many
     * steps back from the most recent entry the caller is currently
     * looking at (0 = most recent). Reset by push() and by
     * console_history_reset_browse() -- deliberately NOT touched by
     * cursor movement, only by actual edits to the line's text, so a
     * caller can move around inside a recalled line and still press
     * "prev" again to go further back. */
    int browse_offset;
};

void console_history_init(struct console_history *h);

/* Appends a submitted line, oldest entry overwritten once full. A no-op
 * for an empty line (nothing worth remembering). Always resets browsing
 * back to "not browsing", same as a real shell after Enter. */
void console_history_push(struct console_history *h, const char *line);

/* Call whenever the input line's text is actually edited (typing,
 * backspace) so the next "prev" starts over from the most recent entry
 * rather than continuing from wherever browsing had reached. */
void console_history_reset_browse(struct console_history *h);

/* Steps one entry further back in time. On success, writes the recalled
 * line into out (a CONSOLE_INPUT_MAX+1 buffer) and returns 1. Returns 0
 * (out left untouched) if there's no history, or browsing is already at
 * the oldest entry. */
int console_history_prev(struct console_history *h, char *out);

/* Steps one entry back toward the present. On success, writes the
 * recalled line into out and returns 1 -- stepping past the most recent
 * entry writes "" (an empty line) and exits browsing. Returns 0 (out
 * left untouched) only when not currently browsing at all. */
int console_history_next(struct console_history *h, char *out);

#endif
