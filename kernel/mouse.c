/* PS/2 mouse driver. Device-level init (mouse_init()) still polls the
 * controller directly -- that's a request/ACK protocol exchange, not the
 * ongoing data stream, so it runs before interrupts_enable() unmasks
 * IRQ12 (see kernel.c) and there's no handler yet to race against. Once
 * streaming starts, incoming bytes arrive via IRQ12 (isr.c) instead.
 *
 * The PS/2 mouse isn't a separate chip from the keyboard's point of view --
 * both sit behind the same 8042 "keyboard controller", multiplexing a
 * keyboard port and an auxiliary ("aux") port onto the same two I/O ports
 * (0x60 data, 0x64 status/command). With real interrupts, IRQ1 and IRQ12
 * are separate vectors, so the hardware itself keeps the two streams
 * apart -- unlike the earlier polling version of this driver, which had
 * to check status port bit 5 in software to tell a mouse byte from a
 * stray keyboard byte. */

#include "mouse.h"
#include "io.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01 /* controller has a byte ready at 0x60 */
#define PS2_STATUS_INPUT_FULL 0x02  /* controller hasn't consumed our last write yet */

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

    ps2_write_command(0xA8); /* enable the auxiliary (mouse) port */

    ps2_write_command(0x20); /* "read controller configuration byte" */
    config = ps2_read_data();
    config |= 0x02;                  /* enable IRQ12 (mouse interrupt line) */
    config &= (unsigned char)~0x20; /* clear "disable mouse clock" */
    ps2_write_command(0x60);        /* "write controller configuration byte" */
    ps2_write_data(config);

    mouse_command(0xF6); /* "set defaults" */
    ps2_read_data();       /* discard ACK (0xFA) */

    mouse_command(0xF4); /* "enable data reporting" -- mouse starts streaming packets */
    ps2_read_data();       /* discard ACK (0xFA) */
}

/* Ring buffer of raw mouse protocol bytes, filled by mouse_irq_push_byte()
 * (called from IRQ12's handler in isr.c) and drained by
 * mouse_read_packet(). volatile for the same reason as keyboard.c's
 * buffer: written from an interrupt handler that can preempt the reader
 * at any point. */
#define MOUSE_BUFFER_SIZE 32
static volatile unsigned char mouse_buffer[MOUSE_BUFFER_SIZE];
static volatile int mouse_head = 0;
static volatile int mouse_tail = 0;

void mouse_irq_push_byte(unsigned char b) {
    int next = (mouse_head + 1) % MOUSE_BUFFER_SIZE;
    if (next != mouse_tail) { /* drop the byte if the buffer is full */
        mouse_buffer[mouse_head] = b;
        mouse_head = next;
    }
}

static int mouse_buffer_pop(unsigned char *out) {
    if (mouse_tail == mouse_head) {
        return 0;
    }
    *out = mouse_buffer[mouse_tail];
    mouse_tail = (mouse_tail + 1) % MOUSE_BUFFER_SIZE;
    return 1;
}

/* Bytes already captured toward the packet currently in progress (0, 1,
 * or 2) -- lets mouse_poll_packet() resume across calls instead of
 * blocking internally to complete a packet. Genuinely necessary, not just
 * defensive: mouse_irq_push_byte() above silently drops a byte if the
 * ring is full, which can leave byte 1 or 2 of a packet missing after
 * byte 0 was already consumed. Blocking (the previous wait_mouse_byte()
 * approach) would then hang forever waiting for a byte that's never
 * coming -- freezing the whole single-threaded event loop, despite
 * mouse.h documenting this function as non-blocking. mouse_read_packet()
 * below already supplies its own block-until-ready loop on top of this,
 * so making this function honestly non-blocking doesn't change that
 * caller's behavior at all. */
static unsigned char pending[2];
static int pending_count = 0;

int mouse_poll_packet(int *dx, int *dy, int *buttons) {
    unsigned char b0, b1, b2;
    int raw_dx, raw_dy;

    if (pending_count == 0) {
        unsigned char b;
        if (!mouse_buffer_pop(&b)) {
            return 0;
        }
        while (!(b & 0x08)) { /* bit 3 is always 1 in byte 0 of a real packet; resync on anything else */
            if (!mouse_buffer_pop(&b)) {
                return 0; /* ran out of bytes mid-resync; caller tries again later */
            }
        }
        pending[0] = b;
        pending_count = 1;
    }

    if (pending_count == 1) {
        if (!mouse_buffer_pop(&pending[1])) {
            return 0; /* byte 0 already captured; caller tries again later */
        }
        pending_count = 2;
    }

    if (!mouse_buffer_pop(&b2)) {
        return 0; /* bytes 0/1 already captured; caller tries again later */
    }

    b0 = pending[0];
    b1 = pending[1];
    pending_count = 0;

    raw_dx = b1;
    raw_dy = b2;
    if (b0 & 0x10) {
        raw_dx -= 256; /* sign-extend X */
    }
    if (b0 & 0x20) {
        raw_dy -= 256; /* sign-extend Y */
    }

    /* Bits 6/7 mean the real motion overflowed the 9-bit signed range this
     * packet can carry -- the device is telling us raw_dx/raw_dy are not
     * trustworthy, not just large. A fast real mouse flick can trip this;
     * the slow one-packet-at-a-time monitor commands used to test this
     * driver never moved fast enough to. Clamp to the max representable
     * magnitude in the reported direction rather than trust a bogus delta. */
    if (b0 & 0x40) {
        raw_dx = (raw_dx < 0) ? -255 : 255;
    }
    if (b0 & 0x80) {
        raw_dy = (raw_dy < 0) ? -255 : 255;
    }

    *dx = raw_dx;
    *dy = -raw_dy; /* PS/2 reports +Y as "up"; screen coordinates want +Y as "down" */
    *buttons = b0 & 0x07;
    return 1;
}

void mouse_read_packet(int *dx, int *dy, int *buttons) {
    while (!mouse_poll_packet(dx, dy, buttons)) {
        __asm__ volatile("hlt");
    }
}
