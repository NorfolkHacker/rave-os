#ifndef RAVEOS_IDT_H
#define RAVEOS_IDT_H

/* present=1, ring=0, 32-bit interrupt gate (type 0xE). Interrupt gates
 * (vs. trap gates) clear the CPU's interrupt flag on entry, so a second
 * interrupt can't preempt a handler that's still running. */
#define IDT_TYPE_INTERRUPT_GATE_32 0x8E

void idt_init(void);
void idt_set_gate(int vector, void *handler, unsigned char type_attr);

#endif
