#ifndef RAVEOS_IDT_H
#define RAVEOS_IDT_H

/* present=1, ring=0, 32-bit interrupt gate (type 0xE). Interrupt gates
 * (vs. trap gates) clear the CPU's interrupt flag on entry, so a second
 * interrupt can't preempt a handler that's still running. */
#define IDT_TYPE_INTERRUPT_GATE_32 0x8E

/* present=1, ring=3, 32-bit trap gate (type 0xF). Trap gates (vs.
 * interrupt gates) leave IF alone on entry -- unlike a hardware IRQ,
 * a syscall is deliberate, synchronous code the caller chose to run,
 * so there's no reason to block other interrupts while it executes.
 * DPL=3 (vs. IDT_TYPE_INTERRUPT_GATE_32's DPL=0) is what lets ring 3
 * code invoke `int 0x80` at all -- executing INT n from a CPL
 * numerically greater than the gate's DPL is otherwise itself a #GP. */
#define IDT_TYPE_TRAP_GATE_32_DPL3 0xEF

void idt_init(void);
void idt_set_gate(int vector, void *handler, unsigned char type_attr);

#endif
