#ifndef RAVEOS_MOUSE_H
#define RAVEOS_MOUSE_H

/* Enables the PS/2 auxiliary (mouse) device and switches it into
 * streaming mode. Must be called once before mouse_read_packet(). */
void mouse_init(void);

/* Non-blocking: if a full packet is available right now, decodes it into
 * dx/dy/buttons and returns 1; otherwise returns 0 immediately.
 * dx/dy are relative movement since the last packet, with dy already
 * flipped so positive means "down" in screen coordinates (PS/2 reports
 * positive Y as "up"). buttons is a bitmask: bit0=left, bit1=right,
 * bit2=middle. Note: once a valid packet start byte is found, this does
 * briefly wait (hlt) for that same packet's remaining 2 bytes rather
 * than bailing out and risking misreading a later packet's first byte
 * as this one's second -- in practice all 3 bytes of a packet arrive in
 * one tight burst of interrupts, so this is a negligible wait, not a
 * real block. */
int mouse_poll_packet(int *dx, int *dy, int *buttons);

/* Blocks until a full 3-byte movement packet arrives. Built on
 * mouse_poll_packet(). */
void mouse_read_packet(int *dx, int *dy, int *buttons);

/* Called only from the IRQ12 handler (isr.c) -- pushes a raw mouse
 * protocol byte into the ring buffer mouse_read_packet() drains. Not for
 * general use. */
void mouse_irq_push_byte(unsigned char b);

#endif
