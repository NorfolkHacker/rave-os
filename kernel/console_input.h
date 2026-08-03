#ifndef RAVEOS_CONSOLE_INPUT_H
#define RAVEOS_CONSOLE_INPUT_H

/* A single-line text input, sibling to textfield.c rather than a reuse of
 * it: TEXTFIELD_MAX (24) is too short for a real Forth source line, and
 * textfield_feed_char() hardcodes '\n' to clear the field -- fine for a
 * demo widget with nowhere to submit to, wrong for a REPL where Enter has
 * to hand the typed line to a caller before it's gone. The panel's
 * existing textfield still depends on clear-on-Enter, so this is a new
 * widget, not a modified one. */
#define CONSOLE_INPUT_MAX 127

struct console_input {
    int x, y, w, h;
    char text[CONSOLE_INPUT_MAX + 1];
    int len;
    int focused; /* only a focused field consumes keystrokes */
};

int console_input_hit_test(const struct console_input *ci, int px, int py);

/* Returns 1 exactly when Enter was pressed (a line was submitted), 0
 * otherwise -- including when unfocused, so callers can feed every
 * polled character unconditionally, same convention as
 * textfield_feed_char(). Unlike textfield_feed_char(), does NOT clear the
 * buffer on Enter: the caller needs ci->text to still hold the submitted
 * line (to echo it, then hand it to the interpreter) after this returns
 * 1. Call console_input_clear() explicitly once the caller is done
 * reading it, so "read" and "clear" can never be silently conflated. */
int console_input_feed_char(struct console_input *ci, char c);

void console_input_clear(struct console_input *ci);

void console_input_draw(const struct console_input *ci);

#endif
