#ifndef RAVEOS_GDT_H
#define RAVEOS_GDT_H

#include <stdint.h>

struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char  base_mid;
    unsigned char  access;
    unsigned char  granularity;
    unsigned char  base_high;
} __attribute__((packed));

/* Must match KERNEL_CODE_SEGMENT/KERNEL_DATA_SEGMENT in arch/idt.c --
 * every ISR (and idt_set_gate()'s hardcoded selector) assumes these
 * exact values. gdt_init() keeps them numerically identical to
 * boot/stage2.asm's original static GDT; only the table backing them
 * becomes kernel-owned. */
#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_CODE_SELECTOR   0x18
#define GDT_USER_DATA_SELECTOR   0x20
#define GDT_TSS_SELECTOR         0x28

/* Packs base/limit/access/gran into *entry per the standard x86 GDT
 * descriptor layout. gran's low nibble is discarded -- the
 * descriptor's actual low nibble comes from limit's bits 19:16.
 * Pure function: no asm, no hardware access. */
void gdt_pack_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit, unsigned char access, unsigned char gran);

/* Builds a 6-entry GDT (null, kernel code/data at
 * GDT_KERNEL_CODE_SELECTOR/GDT_KERNEL_DATA_SELECTOR, user code/data
 * at GDT_USER_CODE_SELECTOR/GDT_USER_DATA_SELECTOR, a TSS at
 * GDT_TSS_SELECTOR), with a static TSS whose esp0/ss0 point at a
 * dedicated kernel-mode stack -- what the CPU auto-loads on any
 * ring3->ring0 transition (a syscall or a fault). Loads it via lgdt,
 * reloads every segment register, and loads the TSS via ltr. Call
 * once, before interrupts_init() -- everything downstream (every
 * existing ISR, idt_set_gate()'s hardcoded 0x08) assumes
 * GDT_KERNEL_CODE_SELECTOR/GDT_KERNEL_DATA_SELECTOR are already
 * valid, and this keeps their numeric values unchanged from
 * boot/stage2.asm's table, just kernel-owned from here on. */
void gdt_init(void);

#endif
