#ifndef RAVEOS_MOUSE_H
#define RAVEOS_MOUSE_H

/* Enables the PS/2 auxiliary (mouse) device and switches it into
 * streaming mode. Must be called once before mouse_read_packet(). */
void mouse_init(void);

/* Non-blocking: if a full packet is available right now, decodes it into
 * dx/dy/buttons and returns 1; otherwise returns 0 immediately -- even
 * partway through a packet (e.g. byte 0 arrived but byte 1/2 haven't
 * yet, or never will because the ring buffer dropped them while full).
 * Partial bytes already captured are remembered internally (mouse.c's
 * pending[]/pending_count) and resumed on the next call rather than
 * re-waited-for, so this never blocks the caller itself.
 * dx/dy are relative movement since the last packet, with dy already
 * flipped so positive means "down" in screen coordinates (PS/2 reports
 * positive Y as "up"). buttons is a bitmask: bit0=left, bit1=right,
 * bit2=middle. */
int mouse_poll_packet(int *dx, int *dy, int *buttons);

/* Blocks until a full 3-byte movement packet arrives. Built on
 * mouse_poll_packet(). */
void mouse_read_packet(int *dx, int *dy, int *buttons);

/* Called only from the IRQ12 handler (isr.c) -- pushes a raw mouse
 * protocol byte into the ring buffer mouse_read_packet() drains. Not for
 * general use. */
void mouse_irq_push_byte(unsigned char b);

#endif
