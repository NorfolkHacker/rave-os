/* PS/2 keyboard driver, polling-based (no interrupts/IDT yet -- that's a
 * later stage). Reads "scancode set 1", the default set the PS/2
 * controller emulated by QEMU (and real PC hardware, for compatibility)
 * produces: pressing a key sends its "make code"; releasing it sends the
 * same code with the top bit set (make code | 0x80), the "break code". */

#include "keyboard.h"
#include "io.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64
#define KBD_STATUS_OUTPUT_FULL 0x01

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_RELEASE_BIT 0x80

static int shift_held = 0;

/* Index = make code. 0 means "no printable character" (ctrl, alt, esc,
 * capslock, function keys, etc. -- not handled yet). Covers the main
 * alphanumeric block only; plenty for a first driver. */
static const char scancode_to_ascii[] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,    '*', 0,   ' ', 0};

static const char scancode_to_ascii_shift[] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,    '*', 0,   ' ', 0};

#define SCANCODE_TABLE_LEN (sizeof(scancode_to_ascii) / sizeof(scancode_to_ascii[0]))

static unsigned char keyboard_read_scancode(void) {
    while (!(inb(KBD_STATUS_PORT) & KBD_STATUS_OUTPUT_FULL)) {
        /* spin until the controller says a byte is waiting in its output buffer */
    }
    return inb(KBD_DATA_PORT);
}

char keyboard_read_char(void) {
    for (;;) {
        unsigned char sc = keyboard_read_scancode();
        int released = sc & SC_RELEASE_BIT;
        unsigned char code = sc & (unsigned char)~SC_RELEASE_BIT;

        if (code == SC_LSHIFT || code == SC_RSHIFT) {
            shift_held = !released;
            continue;
        }

        if (released || code >= SCANCODE_TABLE_LEN) {
            continue;
        }

        char c = shift_held ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
        if (c != 0) {
            return c;
        }
        /* unmapped key -- keep polling for the next one */
    }
}
