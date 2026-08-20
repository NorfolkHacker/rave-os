#ifndef RAVEOS_TEXTFIELD_H
#define RAVEOS_TEXTFIELD_H

#define TEXTFIELD_MAX 24

struct textfield {
    int x, y, w, h;
    char text[TEXTFIELD_MAX + 1];
    int len;
    int focused; /* only a focused field consumes keystrokes */
};

/* Point-in-rect test against the field's box -- what a caller should check
 * on a click edge to decide whether to focus (or defocus) it, same pattern
 * as button_hit_test(). */
int textfield_hit_test(const struct textfield *tf, int px, int py);

/* Feeds one character into the field. No-op if the field isn't focused, so
 * callers can pass every polled character unconditionally rather than
 * duplicating the focus check themselves. Backspace removes the last
 * character; newline clears the field (there's nowhere for a field to
 * "submit" to yet); other printable characters append, up to TEXTFIELD_MAX. */
void textfield_feed_char(struct textfield *tf, char c);

void textfield_draw(const struct textfield *tf);

#endif
