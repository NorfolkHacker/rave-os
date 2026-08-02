#ifndef RAVEOS_KEYBOARD_H
#define RAVEOS_KEYBOARD_H

/* Blocks until a key is pressed, returns its ASCII value (shift-aware).
 * Non-printable keys (ctrl, alt, function keys, capslock, ...) are
 * silently skipped -- this only ever returns printable characters,
 * backspace ('\b'), or newline ('\n'). */
char keyboard_read_char(void);

#endif
