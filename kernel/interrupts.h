#ifndef RAVEOS_INTERRUPTS_H
#define RAVEOS_INTERRUPTS_H

/* Builds the IDT and remaps+masks the PIC. Safe to call any time, but
 * doesn't enable interrupts (no sti) or unmask any IRQ line -- call
 * interrupts_enable() only once any device-level init that relies on
 * polled reads (e.g. mouse_init()'s handshake) has finished. Unmasking
 * too early would race those polled reads against the new interrupt
 * handlers for the same bytes. */
void interrupts_init(void);

/* Unmasks IRQ1 (keyboard), IRQ2 (the PIC cascade line -- required for any
 * IRQ8-15, e.g. IRQ12, to reach the CPU at all), and IRQ12 (mouse), then
 * enables interrupts (sti). */
void interrupts_enable(void);

#endif
