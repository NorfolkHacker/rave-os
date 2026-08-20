#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB (set baud rate divisor) */
    outb(COM1 + 0, 0x03); /* divisor low byte: 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor high byte */
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs disabled, RTS/DSR set */
}

static int transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static void serial_write_char(char c) {
    while (!transmit_empty()) {
    }
    outb(COM1, (unsigned char)c);
}

void serial_write_str(const char *s) {
    while (*s) {
        serial_write_char(*s++);
    }
}

void serial_write_int(int v) {
    char buf[12];
    int i = 0, neg = v < 0;
    unsigned int uv = neg ? (unsigned int)(-v) : (unsigned int)v;

    if (uv == 0) {
        serial_write_char('0');
        return;
    }
    while (uv > 0) {
        buf[i++] = (char)('0' + uv % 10);
        uv /= 10;
    }
    if (neg) {
        serial_write_char('-');
    }
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}
