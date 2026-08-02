#ifndef RAVEOS_KEYBOARD_H
#define RAVEOS_KEYBOARD_H

/* Blocks until a key is pressed, returns its ASCII value (shift-aware).
 * Non-printable keys (ctrl, alt, function keys, capslock, ...) are
 * silently skipped -- this only ever returns printable characters,
 * backspace ('\b'), or newline ('\n'). */
char keyboard_read_char(void);

/* Called only from the IRQ1 handler (isr.c) -- pushes a raw scancode
 * byte into the ring buffer keyboard_read_char() drains. Not for
 * general use. */
void keyboard_irq_push_scancode(unsigned char scancode);

#endif
