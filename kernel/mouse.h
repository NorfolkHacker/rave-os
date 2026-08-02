#ifndef RAVEOS_MOUSE_H
#define RAVEOS_MOUSE_H

/* Enables the PS/2 auxiliary (mouse) device and switches it into
 * streaming mode. Must be called once before mouse_read_packet(). */
void mouse_init(void);

/* Blocks until a full 3-byte movement packet arrives. dx/dy are relative
 * movement since the last packet, with dy already flipped so positive
 * means "down" in screen coordinates (PS/2 reports positive Y as "up").
 * buttons is a bitmask: bit0=left, bit1=right, bit2=middle. */
void mouse_read_packet(int *dx, int *dy, int *buttons);

/* Called only from the IRQ12 handler (isr.c) -- pushes a raw mouse
 * protocol byte into the ring buffer mouse_read_packet() drains. Not for
 * general use. */
void mouse_irq_push_byte(unsigned char b);

#endif
