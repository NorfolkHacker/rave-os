#ifndef RAVEOS_KEYBOARD_H
#define RAVEOS_KEYBOARD_H

/* Non-blocking: if a translated character is available right now, writes
 * it to *out and returns 1; otherwise returns 0 immediately. Drains and
 * discards scancodes for non-printable keys (ctrl, alt, function keys,
 * capslock, ...) and shift presses internally without blocking, so a
 * single call may consume several scancodes before returning either 1
 * (found a char) or 0 (buffer's empty for now). */
int keyboard_poll_char(char *out);

/* Blocks until a key is pressed, returns its ASCII value (shift-aware).
 * Built on keyboard_poll_char(); only ever returns printable characters,
 * backspace ('\b'), or newline ('\n'). */
char keyboard_read_char(void);

/* Called only from the IRQ1 handler (isr.c) -- pushes a raw scancode
 * byte into the ring buffer keyboard_read_char() drains. Not for
 * general use. */
void keyboard_irq_push_scancode(unsigned char scancode);

#endif
