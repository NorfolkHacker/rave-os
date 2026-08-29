#include "gdt.h"

void gdt_pack_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit, unsigned char access, unsigned char gran) {
    entry->base_low    = (unsigned short)(base & 0xFFFF);
    entry->base_mid    = (unsigned char)((base >> 16) & 0xFF);
    entry->base_high   = (unsigned char)((base >> 24) & 0xFF);
    entry->limit_low   = (unsigned short)(limit & 0xFFFF);
    entry->granularity = (unsigned char)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    entry->access      = access;
}

struct gdt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

/* Standard 32-bit TSS layout. Only ss0/esp0 (loaded on any ring3->
 * ring0 transition) and iomap_base (set past the struct's own end,
 * disabling the I/O permission bitmap entirely -- ring 3 gets no port
 * access) are meaningful here; every other field is zeroed and
 * unused, since this kernel never does a hardware task switch. */
struct tss_entry {
    uint32_t prev_tss, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    unsigned short trap, iomap_base;
} __attribute__((packed));

#define GDT_ENTRY_COUNT 6

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_ptr gdtp;
static struct tss_entry tss;
/* 4096 was enough for every syscall before SYS_WAIT_EVENT: each one
 * was a thin wrapper with a shallow call chain and small locals.
 * SYS_WAIT_EVENT re-drives kmain_frame() (kernel/kernel.c) from
 * inside this same stack -- confirmed via objdump that kmain_frame()
 * alone reserves 0x77c (1916) bytes for its own locals, on top of the
 * int 0x80 interrupt frame and the syscall_entry -> syscall_dispatch
 * -> _gfx -> _audio -> _window -> ring3_wait_event call chain's own
 * overhead, plus update_and_present() (0x13c/316 bytes) whenever a
 * real redraw happens inside that call -- close enough to 4096 to be
 * worth real headroom, even though the actual bug this investigation
 * was chasing (SYS_WAIT_EVENT's first proof showing a clean-looking
 * desktop with no window ever drawn) turned out to be a different,
 * separately-fixed problem (draw_window_by_index()'s WIN_KIND_RING3
 * case, see its own comment) -- this stack was never proven to
 * overflow, only shown to be closer to its limit than any syscall
 * before it came near. Sized generously here rather than trimmed
 * tightly, matching this codebase's own established preference (e.g.
 * PROGRAM_LOAD_MAX_SIZE) for headroom over a minimal-fit calculation
 * that would need re-deriving every time a syscall's own call depth
 * changes. See
 * docs/superpowers/specs/2026-08-29-ring3-window-events-design.md. */
static uint8_t syscall_kernel_stack[16384] __attribute__((aligned(16)));

void gdt_init(void) {
    gdt_pack_entry(&gdt[0], 0, 0, 0, 0);                    /* null */
    gdt_pack_entry(&gdt[1], 0, 0xFFFFFFFF, 0x9A, 0xC0);      /* 0x08 kernel code */
    gdt_pack_entry(&gdt[2], 0, 0xFFFFFFFF, 0x92, 0xC0);      /* 0x10 kernel data */
    gdt_pack_entry(&gdt[3], 0, 0xFFFFFFFF, 0xFA, 0xC0);      /* 0x18 user code, DPL3 */
    gdt_pack_entry(&gdt[4], 0, 0xFFFFFFFF, 0xF2, 0xC0);      /* 0x20 user data, DPL3 */

    tss.ss0 = GDT_KERNEL_DATA_SELECTOR;
    tss.esp0 = (uint32_t)(syscall_kernel_stack + sizeof(syscall_kernel_stack));
    tss.iomap_base = (unsigned short)sizeof(tss);
    gdt_pack_entry(&gdt[5], (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00); /* 0x28 TSS */

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)&gdt[0];

    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp $0x08, $1f\n\t"    /* reload CS through the new table */
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m" (gdtp)
        : "eax", "memory"
    );
}
