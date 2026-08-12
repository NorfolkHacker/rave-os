/* PS/2 keyboard driver, interrupt-driven via IRQ1 (see isr.c). Reads
 * "scancode set 1", the default set the PS/2 controller emulated by QEMU
 * (and real PC hardware, for compatibility) produces: pressing a key
 * sends its "make code"; releasing it sends the same code with the top
 * bit set (make code | 0x80), the "break code". */

#include "keyboard.h"

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_RELEASE_BIT 0x80

/* Tracked per physical key, not as one shared flag -- with a single
 * shift_held, pressing both shift keys and releasing only one (ordinary
 * two-handed typing) incorrectly cleared it while the other was still
 * physically held, typing unshifted until the remaining key was pressed
 * and released once more to accidentally "resync" it. */
static int lshift_held = 0;
static int rshift_held = 0;

/* Set by an 0xE0 prefix byte, consumed by the very next byte -- PS/2
 * "extended" scancodes (arrow keys, among others) are always a 2-byte
 * sequence: 0xE0 followed by the actual make/break code. */
static int pending_extended = 0;

#define SC_EXT_UP    0x48
#define SC_EXT_DOWN  0x50
#define SC_EXT_LEFT  0x4B
#define SC_EXT_RIGHT 0x4D

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

/* Ring buffer of raw scancodes, filled by keyboard_irq_push_scancode()
 * (called from IRQ1's handler in isr.c) and drained by
 * keyboard_read_char(). volatile because it's written from an interrupt
 * handler that can preempt keyboard_read_char() at any point. */
#define KBD_BUFFER_SIZE 32
static volatile unsigned char kbd_buffer[KBD_BUFFER_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

void keyboard_irq_push_scancode(unsigned char scancode) {
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) { /* drop the byte if the buffer is full */
        kbd_buffer[kbd_head] = scancode;
        kbd_head = next;
    }
}

static int kbd_buffer_pop(unsigned char *out) {
    if (kbd_tail == kbd_head) {
        return 0;
    }
    *out = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return 1;
}

int keyboard_poll_char(char *out) {
    unsigned char sc;

    while (kbd_buffer_pop(&sc)) {
        int released = sc & SC_RELEASE_BIT;
        unsigned char code = sc & (unsigned char)~SC_RELEASE_BIT;

        if (sc == 0xE0) {
            pending_extended = 1;
            continue;
        }

        if (pending_extended) {
            pending_extended = 0;
            if (!released) {
                char key = 0;
                switch (code) {
                    case SC_EXT_UP:    key = KEY_UP;    break;
                    case SC_EXT_DOWN:  key = KEY_DOWN;  break;
                    case SC_EXT_LEFT:  key = KEY_LEFT;  break;
                    case SC_EXT_RIGHT: key = KEY_RIGHT; break;
                    default: break; /* unhandled extended key -- drop it */
                }
                if (key != 0) {
                    *out = key;
                    return 1;
                }
            }
            continue; /* release of an extended key, or an unhandled one */
        }

        if (code == SC_LSHIFT) {
            lshift_held = !released;
            continue;
        }
        if (code == SC_RSHIFT) {
            rshift_held = !released;
            continue;
        }

        if (released || code >= SCANCODE_TABLE_LEN) {
            continue;
        }

        char c = (lshift_held || rshift_held) ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
        if (c != 0) {
            *out = c;
            return 1;
        }
        /* unmapped key -- keep draining this call rather than returning "nothing" early */
    }
    return 0;
}

/* Blocks by halting the CPU (hlt) until the next interrupt, rather than
 * busy-spinning on a port -- the actual point of moving this driver to
 * IRQ1: the CPU is free (idle, not burning cycles) between keystrokes. */
char keyboard_read_char(void) {
    char c;
    while (!keyboard_poll_char(&c)) {
        __asm__ volatile("hlt");
    }
    return c;
}
