/* PS/2 keyboard driver, interrupt-driven via IRQ1 (see isr.c). Reads
 * "scancode set 1", the default set the PS/2 controller emulated by QEMU
 * (and real PC hardware, for compatibility) produces: pressing a key
 * sends its "make code"; releasing it sends the same code with the top
 * bit set (make code | 0x80), the "break code". */

#include "keyboard.h"

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

        if (code == SC_LSHIFT || code == SC_RSHIFT) {
            shift_held = !released;
            continue;
        }

        if (released || code >= SCANCODE_TABLE_LEN) {
            continue;
        }

        char c = shift_held ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
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
