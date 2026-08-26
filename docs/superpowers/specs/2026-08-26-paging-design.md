# Paging: an identity-mapped foundation for real userspace

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry (raised 2026-08-26) decomposes
into independent sub-projects the same way the audio subsystem did:
(A) paging, (B) ring 3 + a minimal syscall ABI, (C) a real syscall
surface for existing kernel services, (D) a loadable/relocatable
program format. This spec is (A) -- the true foundation everything
else depends on, and the one piece that's fully buildable and testable
in ring 0 alone, with no dependency on any of the others landing first.

RaveOS runs today with paging entirely off: one flat, unpaged 32-bit
address space, the same physical and virtual address for everything.
`docs/superpowers/specs/2026-08-19-concurrency-design.md`'s own
scheduler design explicitly flagged this as a real gap it wasn't
solving -- its stack-overflow protection is a canary check, "not a
guard page," because "no paging at all" existed to build one with.

**This spec deliberately does not build that guard page.** Scoped down
in brainstorming to exactly one thing: get the CPU into paging mode
with a full identity map (every legitimate address maps to itself,
functionally invisible to the rest of the kernel) and a page-fault
handler that can actually report what happened. Guard pages, per-
process address spaces, and everything else that makes paging
*useful* for isolation are follow-up sub-projects that build on this
mechanism once it exists.

## Scope

**In scope:**
- Enable `CR4.PSE` (4MB pages) and `CR0.PG` (paging), backed by a
  single static page directory that identity-maps physical memory
  from `0x00000000` up through comfortably past the VBE linear
  framebuffer's physical address, using 4MB pages -- no second-level
  page tables at all.
- Extend the existing (currently unreachable) `isr_page_fault` handler
  in `kernel/arch/isr.c` to read `CR2` (the faulting linear address)
  and the pushed error code, and report them, instead of today's bare
  `panic("PANIC: PAGE FAULT")`.
- One-time, throwaway verification during this task's own testing
  pass: deliberately touch an address past the mapped range, confirm
  the fault fires and the panic banner shows the right faulting
  address, then remove the deliberate trigger before shipping.

**Out of scope, deliberately:**
- **Guard pages on scheduler program stacks.** The concurrency spec's
  own gap. Real follow-up work, once this mechanism exists to build it
  on -- needs converting at least the stack region's PDE from a 4MB
  page to a real page-table-backed set of 4KB pages, which this spec
  doesn't do anywhere.
- **Ring 3, a syscall ABI, or any privilege-level change at all.**
  Sub-project (B). This spec's identity map has no notion of user vs.
  supervisor pages (every PDE is supervisor-only by omission, since
  nothing runs at ring 3 yet to need otherwise) and doesn't touch the
  GDT or add a TSS.
- **Per-process address spaces, or more than one page directory ever
  existing.** There is exactly one address space in this design, used
  by everything, forever, identically to how the kernel already
  behaves today -- this spec changes the *mechanism* (MMU translation
  now happens) without changing the *behavior* (every access still
  goes exactly where it always went).
- **A null-page guard (leaving physical page 0 unmapped).** Tempting,
  since it's a well-known convention and would double as a permanent
  proof the fault handler works -- but real, low, currently-used
  memory (`boot_info` at `0x9500`, the VBE scratch block at `0x9000`,
  the kernel's own stack) sits inside the first 4MB region, so leaving
  *just* page 0 unmapped needs the same page-table-backed 4KB
  machinery guard pages do. Deliberately deferred alongside them, not
  a smaller version of the same out-of-scope item.
- **Any physical memory allocator, `malloc`, or dynamic paging
  (page-in/page-out, swap).** Nothing here allocates memory at
  runtime; the page directory is a single static structure sized once
  at compile time.
- **Sub-projects (B)/(C)/(D)** in general -- separate specs, not
  started, not what this builds toward beyond being their prerequisite.

## Design

### Why 4MB pages (PSE), not the standard 4KB two-level scheme

A real 4KB page table needs a page table per mapped 4MB region (1024
entries × 4 bytes = 4KB of page-table memory per 4MB of address space
mapped) -- for an identity map reaching the framebuffer's physical
address, that's over a megabyte of page-table memory just to describe
mappings this spec's own scope has no need to vary at 4KB granularity
anywhere. 4MB pages (`CR4.PSE`) collapse that to zero second-level
structure: a page directory entry can describe a 4MB region directly.
Since this spec's entire address space is one flat identity map with
uniform permissions, there is no present benefit to 4KB granularity --
only cost. The future guard-page work is exactly the point where a
*specific* PDE (the one covering a program's stack) needs converting
from a 4MB page to a real page-table-backed region; nothing about
building the rest of the map with 4MB pages now blocks that later
change to one PDE.

### Physical address range mapped

Confirmed empirically (reading `boot_info` directly out of a running
QEMU instance, not assumed): this build's VBE linear framebuffer sits
at physical `0xFD000000` -- PDE index 1012 (`0xFD000000 / 0x400000`).
The identity map covers PDEs `0` through `1015` (`PAGING_IDENTITY_PDE_COUNT
= 1016`): the framebuffer's own PDE (1012) plus three full 4MB PDEs of
headroom (1013-1015, 12MB) past its base address, comfortably covering
any resolution's actual pixel data (the largest currently supported,
800x600x32bpp, is under 2MB) plus slack for `BytesPerScanLine`
padding. PDEs `1016`-`1023` (the top 32MB of the 32-bit address space)
are deliberately left at their zeroed, not-present default -- this
costs nothing (an unset entry already reads as not-present) and is
exactly the range this task's throwaway verification pokes at to
prove the fault handler fires on a real, unmapped address.

### `kernel/arch/paging.c` / `paging.h`

Split the way this codebase already splits hardware-touching code from
testable logic (`kernel/gui/boot_splash.c`'s pure tick function is the
most recent precedent): a pure function that builds the page
directory's contents, separate from the real mode-switching code that
can only be verified by actually booting.

```c
#define PAGING_IDENTITY_PDE_COUNT 1016
#define PAGE_DIRECTORY_ENTRIES 1024

/* Present | Read-Write | Page Size (4MB). No User/Supervisor bit set --
 * every mapped page is supervisor-only, since nothing runs at ring 3
 * yet (see this spec's Out of Scope). */
#define PDE_IDENTITY_FLAGS 0x83

/* Fills pd[0..pde_count) as present, read-write, 4MB identity pages
 * (pd[i] maps physical/virtual region [i*4MB, (i+1)*4MB)); zeroes
 * pd[pde_count..PAGE_DIRECTORY_ENTRIES) (not-present -- the same value
 * a zeroed .bss array already holds, made explicit here so the
 * function's output doesn't depend on the caller having zeroed the
 * buffer first). Pure function, no asm, no hardware access -- every
 * entry's value is computable and checkable on the host. */
void paging_build_directory(uint32_t *pd, uint32_t pde_count);

/* Builds the identity map into the static page directory and switches
 * the CPU into paging mode (CR4.PSE, CR3, CR0.PG). Never returns
 * early/on error -- an unsupported CPU (no PSE) is not a condition
 * this kernel has ever handled for any other feature (see
 * docs/IDEAS.md's dropped boot-time CPU/RAM config entry for the
 * project's established stance on hardware-capability detection), and
 * every environment this kernel targets (real x86 since the Pentium,
 * every QEMU CPU model) has had PSE for three decades. */
void paging_enable(void);
```

`paging_build_directory()`'s per-entry math: `pd[i] = (i << 22) |
PDE_IDENTITY_FLAGS` for `i < pde_count`. `i << 22` places `i` directly
into a 4MB-aligned PDE's physical-base-address field (bits 31:22);
since `i` never exceeds `1023` (10 bits), this can't collide with the
flag bits below it, and the reserved bits 21:13 (only meaningful for
4MB pages, must be zero) are zero for the same reason -- shifting a
10-bit value left by 22 leaves bits 21:0 entirely zero.

`paging_enable()`:
```c
static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(4096)));

void paging_enable(void) {
    paging_build_directory(page_directory, PAGING_IDENTITY_PDE_COUNT);
    __asm__ volatile(
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"      /* CR4.PSE */
        "mov %%eax, %%cr4\n\t"
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t" /* CR0.PG */
        "mov %%eax, %%cr0\n\t"
        "jmp 1f\n\t"                /* flush prefetch queue */
        "1:\n\t"
        :
        : "r" (page_directory)
        : "eax", "memory"
    );
}
```
`page_directory` is a normal static array, not a fixed physical
address like `boot_info`/the backbuffer -- at 4KB it's nowhere near
large enough to threaten the `0x90000` stack-collision limit those two
were sized around, so it doesn't need their special treatment. Paging
is off when `paging_enable()` runs, and this kernel's segmentation is
already flat with a zero base (`boot/stage2.asm`'s GDT setup), so
`page_directory`'s own address *is* its physical address -- no
translation needed to load it into `CR3`, the same reasoning
`boot_info.h` already documents for reading `boot_info` as a plain
physical pointer.

**Verify empirically, don't assume:** `__attribute__((aligned(4096)))`
on a `.bss`-resident array needs to actually land 4KB-aligned once
linked (`CR3` requires 4KB alignment) -- confirm via `nm kernel.elf |
grep page_directory` or the equivalent during implementation, not by
trusting the attribute compiled without error.

### Call site and ordering

`kmain()` calls `paging_enable()` once, right after `interrupts_init()`
and before `mouse_init()`/`interrupts_enable()` (`kernel/kernel.c`,
around line 2304's existing block). CPU exceptions (`#PF` included)
don't need `STI` or PIC unmasking to be delivered -- only external
maskable interrupts do -- so the IDT gate `interrupts_init()` installs
for vector 14 is already live before `paging_enable()` runs, meaning
any bug in the identity-map setup itself surfaces as a caught,
diagnosable panic rather than an silent triple-fault reset.

### `isr_page_fault`: making the existing handler actually useful

Current (`kernel/arch/isr.c:71-75`):
```c
__attribute__((interrupt)) static void isr_page_fault(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    panic("PANIC: PAGE FAULT");
}
```
This has never fired in this kernel's history -- paging has never been
on. Once it can fire, the two pieces of real diagnostic information a
`#PF` carries (the faulting linear address, in `CR2`; and the error
code's low three bits: present/write/user) are both being silently
discarded. Extend `panic()` to accept and display them:
```c
static void panic_with_addr(const char *msg, uint32_t addr, unsigned int error_code) {
    /* format msg plus "addr=0x%08x code=0x%x" into a fixed buffer, then
     * the same gfx_fill_rect/text_puts/gfx_present/halt panic() already
     * does. No snprintf in this freestanding kernel, and no existing
     * hex-formatting helper anywhere in the tree (checked: no
     * itoa/snprintf/hex-to-string function exists in kernel/ today) --
     * this needs a small new one, e.g. a static `hex8_to_str(uint32_t
     * v, char *out)` writing 8 hex digits, the first freestanding
     * number-to-text conversion this kernel has needed. */
}

__attribute__((interrupt)) static void isr_page_fault(struct interrupt_frame *frame, unsigned int error_code) {
    uint32_t fault_addr;
    (void)frame;
    __asm__ volatile("mov %%cr2, %0" : "=r" (fault_addr));
    panic_with_addr("PANIC: PAGE FAULT", fault_addr, error_code);
}
```
The exact buffer/formatting code is an implementation detail for the
plan to write out in full (a small hex-digit-writing loop, `unsigned
int` -> 8 ASCII hex chars) -- the requirement this spec fixes is
behavioral: a page fault's on-screen report must include the faulting
address, not just a generic banner.

## Testing

**Host-buildable**, new `kernel/tests/test_paging.c` (same convention
as `test_font.c`/`test_synth.c`/`test_boot_splash.c`): calls
`paging_build_directory()` on a local `uint32_t[1024]` buffer with a
small `pde_count` (not the real 1016 -- a test-sized value keeps the
assertions fast and readable) and asserts: every entry below
`pde_count` decodes to the right physical base (`entry & 0xFFC00000 ==
i << 22`) and has Present/RW/PS all set; every entry at or above
`pde_count` is exactly zero. Also test the boundary cases `pde_count =
0` (everything zero) and `pde_count = PAGE_DIRECTORY_ENTRIES` (nothing
left unmapped).

**Headless QEMU, one-time and throwaway:** temporarily add a
deliberate touch of an address in the deliberately-unmapped range
(e.g. `0xFFFFFFF0`) right after `paging_enable()` returns, boot
headless, screendump, and confirm the panic banner is on screen
showing `addr=0xfffffff0` and the expected error-code bits (not
present, and the write/read bit matching whichever access was used).
Remove the deliberate touch before the final commit -- this is a
one-time proof the mechanism works, not a permanent behavior; normal
boot must reach the desktop exactly as it does today, since this
spec's entire premise is that the identity map is functionally
invisible.

**Full-boot regression check:** with the deliberate fault removed,
confirm the kernel still boots to a working desktop in headless QEMU
(screendump showing the normal desktop, same as every prior feature's
verification pass) -- proving the identity map really is a no-op for
every legitimate address the kernel already uses (kernel code/data,
the stack, `boot_info`, the backbuffer, the real framebuffer).
