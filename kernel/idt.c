/* The IDT (Interrupt Descriptor Table) is the CPU's lookup table from
 * interrupt/exception vector number (0-255) to handler address -- the
 * protected-mode equivalent of the real-mode interrupt vector table
 * BIOS calls (int 0x10, int 0x13, ...) used back in boot/stage2.asm.
 * Nothing calls those BIOS interrupts anymore; this is a completely
 * separate table the kernel builds and owns. */

#include "idt.h"

struct idt_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

#define IDT_ENTRIES 256

/* Must match CODE_SEG in boot/stage2.asm's GDT -- that's the segment
 * selector already loaded into CS by the time kernel code runs, and the
 * one the CPU should switch back to when a handler fires. */
#define KERNEL_CODE_SEGMENT 0x08

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

void idt_set_gate(int vector, void *handler, unsigned char type_attr) {
    unsigned int addr = (unsigned int)handler;

    idt[vector].offset_low = (unsigned short)(addr & 0xFFFF);
    idt[vector].selector = KERNEL_CODE_SEGMENT;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (unsigned short)((addr >> 16) & 0xFFFF);
}

void idt_init(void) {
    idtp.limit = (unsigned short)(sizeof(idt) - 1);
    idtp.base = (unsigned int)&idt[0];
    __asm__ volatile("lidt %0" : : "m"(idtp));
}
