#ifndef RAVEOS_KEYBOARD_H
#define RAVEOS_KEYBOARD_H

/* Pseudo-characters keyboard_poll_char()/keyboard_read_char() return for
 * the 4 arrow keys, decoded from PS/2 "extended" scancodes (an 0xE0
 * prefix byte + a code byte -- see keyboard.c). Values are unused C0
 * control codes (DC1-DC4), chosen because nothing in this kernel already
 * treats them specially (unlike '\b'=8, '\n'=10). */
#define KEY_UP    0x11
#define KEY_DOWN  0x12
#define KEY_LEFT  0x13
#define KEY_RIGHT 0x14

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
