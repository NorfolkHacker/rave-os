/* PS/2 mouse driver, polling-based like keyboard.c (no IDT/IRQ12 yet).
 *
 * The PS/2 mouse isn't a separate chip from the keyboard's point of view --
 * both sit behind the same 8042 "keyboard controller", which multiplexes
 * a keyboard port and an auxiliary ("aux") port onto the same two I/O
 * ports (0x60 data, 0x64 status/command). Status bit 5 tells you which
 * device a waiting byte came from, which is how the two streams are told
 * apart without needing separate hardware. */

#include "mouse.h"
#include "io.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01   /* controller has a byte ready at 0x60 */
#define PS2_STATUS_INPUT_FULL 0x02    /* controller hasn't consumed our last write yet */
#define PS2_STATUS_AUX_DATA 0x20      /* the waiting byte is from the mouse, not the keyboard */

static void ps2_wait_input_clear(void) {
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) {
    }
}

static void ps2_write_command(unsigned char cmd) {
    ps2_wait_input_clear();
    outb(PS2_CMD_PORT, cmd);
}

static void ps2_write_data(unsigned char data) {
    ps2_wait_input_clear();
    outb(PS2_DATA_PORT, data);
}

static unsigned char ps2_read_data(void) {
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)) {
    }
    return inb(PS2_DATA_PORT);
}

/* Command byte 0xD4 tells the controller "route the next data byte to the
 * mouse port instead of the keyboard port" -- every mouse command goes
 * through this prefix. */
static void mouse_command(unsigned char data) {
    ps2_write_command(0xD4);
    ps2_write_data(data);
}

void mouse_init(void) {
    unsigned char config;

    ps2_write_command(0xA8);   /* enable the auxiliary (mouse) port */

    ps2_write_command(0x20);   /* "read controller configuration byte" */
    config = ps2_read_data();
    config |= 0x02;             /* enable IRQ12 (mouse interrupt line) -- unused while polling, but part of standard init */
    config &= (unsigned char)~0x20;   /* clear "disable mouse clock" */
    ps2_write_command(0x60);   /* "write controller configuration byte" */
    ps2_write_data(config);

    mouse_command(0xF6);        /* "set defaults" */
    ps2_read_data();             /* discard ACK (0xFA) */

    mouse_command(0xF4);        /* "enable data reporting" -- mouse starts streaming packets */
    ps2_read_data();             /* discard ACK (0xFA) */
}

/* Waits specifically for a byte tagged as coming from the aux port,
 * silently discarding any keyboard byte that shows up in the meantime --
 * without an IDT demuxing IRQ1 vs IRQ12 properly, this polling loop is
 * the only thing standing between "mouse data" and "keyboard data
 * misread as mouse data". */
static unsigned char read_mouse_byte(void) {
    unsigned char status;
    for (;;) {
        status = inb(PS2_STATUS_PORT);
        if ((status & PS2_STATUS_OUTPUT_FULL) && (status & PS2_STATUS_AUX_DATA)) {
            return inb(PS2_DATA_PORT);
        }
        if (status & PS2_STATUS_OUTPUT_FULL) {
            inb(PS2_DATA_PORT); /* stray keyboard byte -- discard */
        }
    }
}

void mouse_read_packet(int *dx, int *dy, int *buttons) {
    unsigned char b0, b1, b2;
    int raw_dx, raw_dy;

    do {
        b0 = read_mouse_byte();
    } while (!(b0 & 0x08)); /* bit 3 is always 1 in byte 0 of a real packet; resync on anything else */

    b1 = read_mouse_byte();
    b2 = read_mouse_byte();

    raw_dx = b1;
    raw_dy = b2;
    if (b0 & 0x10) {
        raw_dx -= 256; /* sign-extend X */
    }
    if (b0 & 0x20) {
        raw_dy -= 256; /* sign-extend Y */
    }

    *dx = raw_dx;
    *dy = -raw_dy; /* PS/2 reports +Y as "up"; screen coordinates want +Y as "down" */
    *buttons = b0 & 0x07;
}
