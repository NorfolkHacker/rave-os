# Ring 3 + Minimal Syscall ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get RaveOS's CPU to execute code at ring 3 (CPL 3) for the first time, and back again via a minimal `int 0x80` syscall, proven end-to-end with a one-time headless-QEMU verification.

**Architecture:** A kernel-owned GDT (replacing `boot/stage2.asm`'s static one) adds real ring-3 code/data segments and a TSS; a DPL-3 `int 0x80` trap gate with a hand-written asm entry stub carries a syscall number/argument in and a return value out; a small `paging_set_user()` primitive makes one already-identity-mapped PDE ring-3-accessible (required because sub-project A's page directory is entirely supervisor-only, and the U/S bit gates *all* access, including instruction fetch). A temporary, throwaway test payload proves the whole path: enter ring 3, round-trip a syscall, then deliberately execute a CPL0-only instruction and confirm `#GP` fires with a correctly decoded error code.

**Tech Stack:** C (`i686-elf-gcc`, freestanding), x86 assembly (NASM), QEMU (headless verification via monitor socket + `socat`), host `gcc -m32` for pure-logic unit tests.

**Spec:** `docs/superpowers/specs/2026-08-27-ring3-syscall-design.md`

## Global Constraints

- Selectors `0x08`/`0x10` (kernel code/data) must keep their exact values — `kernel/arch/idt.c`'s `KERNEL_CODE_SEGMENT` and every existing ISR assume them.
- New user segments: `0x18` (user code, DPL3), `0x20` (user data, DPL3); TSS descriptor at `0x28`.
- No real syscall surface for fs/gfx/audio/window management (sub-project C), no loadable program format (sub-project D), no scheduler integration, no per-process address spaces — all deliberately out of scope (see spec's Scope section).
- This minimal syscall ABI's only guaranteed register after `int 0x80` returns is `eax`; every other GP register is clobbered.
- Every host test compiles with: `gcc -m32 -Wall -Wextra -o /tmp/test_<name> kernel/tests/test_<name>.c kernel/arch/<name>.c && /tmp/test_<name>` — run from the repo root (`/home/norfolkh/os`), zero warnings required.
- Every kernel rebuild + headless-QEMU check: `cd boot && make` (rebuilds `kernel.bin` and `disk.img` together via `kernel/Makefile`'s existing rules), then boot with `-display none -monitor unix:/tmp/qemu-mon.sock,server,nowait`, `screendump` over that socket via `socat`, convert the `.ppm` to `.png` with `python3`/Pillow, and view it.

---

## Task 1: `paging_set_user()` — toggle one PDE's User/Supervisor bit

**Files:**
- Modify: `kernel/arch/paging.h`
- Modify: `kernel/arch/paging.c`
- Modify: `kernel/tests/test_paging.c`

**Interfaces:**
- Consumes: nothing new (extends the existing `PAGE_DIRECTORY_ENTRIES`, `PDE_IDENTITY_FLAGS` already in `paging.h`).
- Produces: `void paging_set_user_entry(uint32_t *pd, uint32_t pde_index, int user);` (pure), `void paging_set_user(uint32_t pde_index, int user);` (real — operates on `paging.c`'s existing static `page_directory`). Task 6 calls `paging_set_user(0, 1)` on the temporary ring-3 test payload's PDE.

- [ ] **Step 1: Write the failing test**

Append to `kernel/tests/test_paging.c`, immediately before the final `if (failures == 0) {` block:

```c
    /* paging_set_user_entry(): toggles bit 2 (User/Supervisor) on one
     * entry in place -- nothing else about that entry, or any other
     * entry, may change. */
    {
        uint32_t before[PAGE_DIRECTORY_ENTRIES];
        uint32_t after[PAGE_DIRECTORY_ENTRIES];

        for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
            before[i] = 0xDEADBEEF;
        }
        paging_build_directory(before, 10);
        for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
            after[i] = before[i];
        }

        paging_set_user_entry(after, 3, 1);
        check((after[3] & 0x4) != 0, "paging_set_user_entry(3, 1): User bit should now be set");
        check((after[3] & ~0x4u) == (before[3] & ~0x4u), "paging_set_user_entry(3, 1): every other bit of entry 3 should be unchanged");
        for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
            if (i != 3) {
                check(after[i] == before[i], "paging_set_user_entry(3, 1): every other entry should be untouched");
            }
        }

        paging_set_user_entry(after, 3, 0);
        check(after[3] == before[3], "paging_set_user_entry(3, 0): entry 3 should be back to its original value");
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_paging kernel/tests/test_paging.c kernel/arch/paging.c && /tmp/test_paging`
Expected: compile error, `undefined reference to 'paging_set_user_entry'` (or, if `-c`-only, a link failure — either way, it must not compile clean yet).

- [ ] **Step 3: Write minimal implementation**

In `kernel/arch/paging.h`, add after `PDE_IDENTITY_FLAGS`:

```c
/* User/Supervisor bit (bit 2). paging_build_directory() never sets
 * this -- every PDE it builds is supervisor-only by construction (see
 * PDE_IDENTITY_FLAGS). paging_set_user_entry()/paging_set_user() are
 * the only way any PDE ever becomes ring-3-accessible, and only ever
 * one entry at a time, in place. */
#define PDE_USER_FLAG 0x4
```

and after `paging_build_directory()`'s declaration:

```c
/* Sets (user=1) or clears (user=0) PDE_USER_FLAG on pd[pde_index] in
 * place -- no other bit of that entry, and no other entry, changes.
 * pde_index must be < PAGE_DIRECTORY_ENTRIES. Pure function: no asm,
 * no hardware access. */
void paging_set_user_entry(uint32_t *pd, uint32_t pde_index, int user);

/* Real: applies paging_set_user_entry() to the live page directory
 * paging_enable() already built and switched CR3 to. Ring 3 code
 * cannot fetch its own first instruction without this -- the U/S bit
 * gates all access, not just data, and every PDE paging_enable()
 * builds starts supervisor-only. */
void paging_set_user(uint32_t pde_index, int user);
```

In `kernel/arch/paging.c`, add after `paging_build_directory()`:

```c
void paging_set_user_entry(uint32_t *pd, uint32_t pde_index, int user) {
    if (user) {
        pd[pde_index] |= PDE_USER_FLAG;
    } else {
        pd[pde_index] &= ~(uint32_t)PDE_USER_FLAG;
    }
}
```

and after the existing `static uint32_t page_directory[...]` declaration (before `paging_enable()`):

```c
void paging_set_user(uint32_t pde_index, int user) {
    paging_set_user_entry(page_directory, pde_index, user);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_paging kernel/tests/test_paging.c kernel/arch/paging.c && /tmp/test_paging`
Expected: `PASS: all paging tests passed`, zero warnings.

- [ ] **Step 5: Commit**

```bash
git add kernel/arch/paging.h kernel/arch/paging.c kernel/tests/test_paging.c
git commit -m "kernel: paging_set_user() -- toggle one PDE's User/Supervisor bit"
```

---

## Task 2: `#GP` decode — `panic_with_code()` in `isr.c`

**Files:**
- Modify: `kernel/arch/isr.c`

**Interfaces:**
- Consumes: `hex32_to_str()` (`kernel/arch/hexfmt.h`, already used by `panic_with_addr()`), `gfx_fill_rect`/`gfx_present` (`gfx/graphics.h`), `text_puts` (`gfx/text.h`) — all already included in `isr.c`.
- Produces: `isr_general_protection` now shows a decoded banner instead of the generic one. Verified end-to-end in Task 6 (this is the first time `#GP` will ever actually fire in this kernel).

No host test — `panic_with_addr()`, its existing sibling, isn't host-tested either (both call real graphics functions with no freestanding-safe host equivalent). This task's own deliverable is verified by a clean kernel build; the banner's correctness is verified in Task 6's headless-QEMU screendump.

- [ ] **Step 1: Add `panic_with_code()`**

In `kernel/arch/isr.c`, add immediately after `panic_with_addr()`:

```c
/* Same banner/text layout as panic_with_addr(), minus the address
 * line -- CR2 is meaningless for #GP (only #PF sets it). error_code
 * is nonzero only when a specific segment selector caused the fault;
 * 0 covers the far more common case (a privileged instruction
 * executed at insufficient CPL). */
static void panic_with_code(const char *msg, unsigned int error_code) {
    char line2[64];
    int pos = 0;
    const char *p;
    char code_hex[9];

    hex32_to_str((uint32_t)error_code, code_hex);

    for (p = "CODE=0x"; *p && pos < (int)sizeof(line2) - 1; p++) {
        line2[pos++] = *p;
    }
    for (p = code_hex; *p && pos < (int)sizeof(line2) - 1; p++) {
        line2[pos++] = *p;
    }
    line2[pos] = '\0';

    gfx_fill_rect(0, 0, gfx_width(), 40, 0xCC0000);
    text_puts(10, 8, msg, 0xFFFFFF, 2);
    text_puts(10, 26, line2, 0xFFFFFF, 1);
    gfx_present();
    for (;;) {
        __asm__ volatile("cli\n\thlt");
    }
}
```

- [ ] **Step 2: Wire it into `isr_general_protection`**

Replace:

```c
__attribute__((interrupt)) static void isr_general_protection(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    panic("PANIC: GENERAL PROTECTION FAULT");
}
```

with:

```c
__attribute__((interrupt)) static void isr_general_protection(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    panic_with_code("PANIC: GENERAL PROTECTION FAULT", error_code);
}
```

- [ ] **Step 3: Confirm a clean build**

Run: `cd kernel && make clean && make 2>&1 | tail -30`
Expected: builds `kernel.bin` with no new warnings (the file already builds with `-Wall -Wextra` clean; this change must not introduce any).

- [ ] **Step 4: Commit**

```bash
git add kernel/arch/isr.c
git commit -m "kernel: decode the #GP error code instead of a bare generic banner"
```

---

## Task 3: `gdt.c`/`.h` — kernel-owned GDT with ring-3 segments and a TSS

**Files:**
- Create: `kernel/arch/gdt.h`
- Create: `kernel/arch/gdt.c`
- Create: `kernel/tests/test_gdt.c`

**Interfaces:**
- Consumes: nothing (self-contained, `<stdint.h>` only).
- Produces: `struct gdt_entry` (packed descriptor layout), `GDT_KERNEL_CODE_SELECTOR`(`0x08`)/`GDT_KERNEL_DATA_SELECTOR`(`0x10`)/`GDT_USER_CODE_SELECTOR`(`0x18`)/`GDT_USER_DATA_SELECTOR`(`0x20`)/`GDT_TSS_SELECTOR`(`0x28`), `void gdt_pack_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit, unsigned char access, unsigned char gran);` (pure), `void gdt_init(void);` (real). Task 5 calls `gdt_init()` from `kmain()`; Task 5's `enter_ring3()` (in `ring3.asm`) uses `GDT_USER_CODE_SELECTOR`/`GDT_USER_DATA_SELECTOR` as raw immediates (`0x18 | 3`/`0x20 | 3`), so those two numeric values must not change without updating that file too.

- [ ] **Step 1: Write the failing test**

Create `kernel/tests/test_gdt.c`:

```c
/* kernel/tests/test_gdt.c -- host-side only, never linked into
 * kernel.bin. gdt_pack_entry() has no freestanding-only dependencies
 * (pure struct-packing logic, no asm), so it compiles and runs
 * natively here exactly as it will inside kernel.bin -- same
 * convention as test_paging.c. gdt_init() itself (real lgdt/ltr/
 * segment-register access) is NOT exercised here; it can only be
 * verified by actually booting (see this feature's BUILD_LOG entry
 * for the one-time headless-QEMU verification). */
#include <stdio.h>
#include <string.h>
#include "../arch/gdt.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    struct gdt_entry e;

    /* Flat kernel-code-shaped entry: base 0, limit 4GB, 4KB-granular. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0, 0xFFFFFFFF, 0x9A, 0xC0);
    check(e.base_low == 0 && e.base_mid == 0 && e.base_high == 0, "base 0 should pack to all-zero base fields");
    check(e.limit_low == 0xFFFF, "limit 0xFFFFFFFF should pack limit_low = 0xFFFF");
    check(e.granularity == 0xCF, "limit 0xFFFFFFFF + gran 0xC0 should pack granularity byte to 0xCF");
    check(e.access == 0x9A, "access byte should pass through unchanged");

    /* Non-zero base, small byte-granular limit -- the TSS descriptor's shape. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0x12345678, 0x67, 0x89, 0x00);
    check(e.base_low == 0x5678, "base 0x12345678: base_low should be 0x5678");
    check(e.base_mid == 0x34, "base 0x12345678: base_mid should be 0x34");
    check(e.base_high == 0x12, "base 0x12345678: base_high should be 0x12");
    check(e.limit_low == 0x67, "limit 0x67: limit_low should be 0x67");
    check(e.granularity == 0x00, "limit 0x67 (bits 19:16 = 0) + gran 0x00: granularity byte should be 0x00");
    check(e.access == 0x89, "access byte should pass through unchanged");

    /* gran's low nibble must be discarded, not merged in. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    check(e.granularity == 0xCF, "gran low nibble is discarded -- 0xCF and 0xC0 must pack identically here");

    if (failures == 0) {
        printf("PASS: all gdt tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_gdt kernel/tests/test_gdt.c kernel/arch/gdt.c`
Expected: fails — `kernel/arch/gdt.c` doesn't exist yet (`No such file or directory`).

- [ ] **Step 3: Write minimal implementation**

Create `kernel/arch/gdt.h`:

```c
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
```

Create `kernel/arch/gdt.c`:

```c
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
static uint8_t syscall_kernel_stack[4096] __attribute__((aligned(16)));

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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_gdt kernel/tests/test_gdt.c kernel/arch/gdt.c && /tmp/test_gdt`
Expected: `PASS: all gdt tests passed`, zero warnings. (`gdt_init()` itself compiles as part of this command but is never called by the test — confirms the inline asm assembles cleanly under host `-m32` without needing to execute it.)

- [ ] **Step 5: Commit**

```bash
git add kernel/arch/gdt.h kernel/arch/gdt.c kernel/tests/test_gdt.c
git commit -m "kernel: kernel-owned GDT with ring-3 segments and a TSS (gdt_init)"
```

---

## Task 4: `syscall.c`/`.h` — the minimal syscall dispatch table

**Files:**
- Create: `kernel/arch/syscall.h`
- Create: `kernel/arch/syscall.c`
- Create: `kernel/tests/test_syscall.c`
- Modify: `kernel/arch/idt.h`

**Interfaces:**
- Consumes: nothing (self-contained).
- Produces: `SYS_TEST`(`0`)/`SYS_EXIT`(`1`), `int syscall_dispatch(int num, int arg);` (pure), and (in `idt.h`) `IDT_TYPE_TRAP_GATE_32_DPL3`. Task 5's `syscall_entry` (`ring3.asm`) calls `syscall_dispatch()` directly (`extern` in the asm file); Task 5 wires `idt_set_gate(0x80, (void *)syscall_entry, IDT_TYPE_TRAP_GATE_32_DPL3)` from `kmain()`. Task 6's temporary test payload issues `int 0x80` with `eax=SYS_TEST`/`eax=SYS_EXIT`.

- [ ] **Step 1: Write the failing test**

Create `kernel/tests/test_syscall.c`:

```c
/* kernel/tests/test_syscall.c -- host-side only, never linked into
 * kernel.bin. syscall_dispatch() has no freestanding-only
 * dependencies (pure switch/return logic, no asm, no other kernel
 * module) -- same convention as test_paging.c/test_gdt.c. The real
 * int 0x80 entry path (ring3.asm's syscall_entry, and the register-
 * level ABI it establishes) can only be verified by actually booting
 * (see this feature's BUILD_LOG entry for the one-time headless-QEMU
 * verification). */
#include <stdio.h>
#include "../arch/syscall.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    check(syscall_dispatch(SYS_TEST, 0) == 0x1234, "SYS_TEST should return the fixed value 0x1234");
    check(syscall_dispatch(SYS_TEST, 999) == 0x1234, "SYS_TEST should ignore its argument and still return 0x1234");
    check(syscall_dispatch(SYS_EXIT, 0) == 0, "SYS_EXIT should return 0");
    check(syscall_dispatch(SYS_EXIT, 42) == 0, "SYS_EXIT should return 0 regardless of its argument");
    check(syscall_dispatch(42, 0) == -1, "an unknown syscall number should return -1");
    check(syscall_dispatch(-1, 0) == -1, "a negative syscall number should return -1");

    if (failures == 0) {
        printf("PASS: all syscall tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_syscall kernel/tests/test_syscall.c kernel/arch/syscall.c`
Expected: fails — `kernel/arch/syscall.c` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

In `kernel/arch/idt.h`, add after `IDT_TYPE_INTERRUPT_GATE_32`:

```c
/* present=1, ring=3, 32-bit trap gate (type 0xF). Trap gates (vs.
 * interrupt gates) leave IF alone on entry -- unlike a hardware IRQ,
 * a syscall is deliberate, synchronous code the caller chose to run,
 * so there's no reason to block other interrupts while it executes.
 * DPL=3 (vs. IDT_TYPE_INTERRUPT_GATE_32's DPL=0) is what lets ring 3
 * code invoke `int 0x80` at all -- executing INT n from a CPL
 * numerically greater than the gate's DPL is otherwise itself a #GP. */
#define IDT_TYPE_TRAP_GATE_32_DPL3 0xEF
```

Create `kernel/arch/syscall.h`:

```c
#ifndef RAVEOS_SYSCALL_H
#define RAVEOS_SYSCALL_H

/* Returns a fixed, recognizable value -- pure plumbing, proves a
 * syscall's argument and return value both cross the ring3/ring0
 * boundary intact. Ignores arg. */
#define SYS_TEST 0

/* Signals "the caller is done" by setting an internal flag -- this
 * minimal ABI has no process/address-space/scheduler-slot concept to
 * actually tear down (see docs/superpowers/specs/2026-08-27-ring3-syscall-design.md's
 * Out of Scope), so this is not real process termination. Ignores arg. */
#define SYS_EXIT 1

/* The C-side half of the syscall ABI. num is the requested syscall
 * (SYS_TEST/SYS_EXIT/anything else -- an unrecognized num returns
 * -1); arg is its one integer argument. Returns the value int 0x80
 * hands back in eax (see ring3.asm's syscall_entry). Pure function:
 * no asm, no hardware access, no other kernel module. */
int syscall_dispatch(int num, int arg);

#endif
```

Create `kernel/arch/syscall.c`:

```c
#include "syscall.h"

int syscall_dispatch(int num, int arg) {
    (void)arg;
    switch (num) {
        case SYS_TEST:
            return 0x1234;
        case SYS_EXIT:
            return 0;
        default:
            return -1;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_syscall kernel/tests/test_syscall.c kernel/arch/syscall.c && /tmp/test_syscall`
Expected: `PASS: all syscall tests passed`, zero warnings.

- [ ] **Step 5: Commit**

```bash
git add kernel/arch/idt.h kernel/arch/syscall.h kernel/arch/syscall.c kernel/tests/test_syscall.c
git commit -m "kernel: minimal syscall dispatch table (SYS_TEST/SYS_EXIT)"
```

---

## Task 5: `ring3.asm` + wiring `gdt_init()`/the syscall gate into `kmain()`

**Files:**
- Create: `kernel/arch/ring3.asm`
- Modify: `kernel/Makefile`
- Modify: `kernel/kernel.c`

**Interfaces:**
- Consumes: `gdt_init()` (Task 3), `syscall_dispatch()` (Task 4, called from `ring3.asm` directly via `extern`), `IDT_TYPE_TRAP_GATE_32_DPL3` (Task 4), `idt_set_gate()` (`kernel/arch/idt.h`, pre-existing).
- Produces: `enter_ring3(void (*entry)(void), void *user_stack_top)` and `syscall_entry(void)` (both `global` in `ring3.asm`, no C header -- declared `extern` directly at each call site, matching this codebase's existing `context_switch()` convention in `kernel/sched/scheduler.c`). Task 6 declares its own `extern void enter_ring3(...)` and calls it.

- [ ] **Step 1: Write `ring3.asm`**

Create `kernel/arch/ring3.asm`:

```nasm
; kernel/arch/ring3.asm -- the whole ring3 entry/exit boundary in one
; file: enter_ring3() drops CPL0 -> CPL3, syscall_entry is what ring 3
; code calls back into. Hand-written (not GCC's
; __attribute__((interrupt)), the way every other ISR in this codebase
; is written -- see kernel/arch/isr.c's own header comment) because a
; syscall needs register-precise control over arguments/return value
; that attribute doesn't expose; kernel/sched/context_switch.asm
; already establishes the precedent for hand-written asm exactly when
; C can't express the needed register discipline. See
; docs/superpowers/specs/2026-08-27-ring3-syscall-design.md.

extern syscall_dispatch

global enter_ring3
global syscall_entry

section .text

; void enter_ring3(void (*entry)(void), void *user_stack_top)
; Builds the 5-word frame iret expects for a privilege-level change
; and lets it do the CPL0 -> CPL3 switch. Never returns to its caller
; -- there is no return-from-ring3 mechanism in this minimal design;
; this codebase's own use of it (kernel.c's temporary ring3 test)
; ends in a deliberate fault instead.
enter_ring3:
    mov eax, [esp+4]        ; entry
    mov ecx, [esp+8]        ; user_stack_top

    mov dx, 0x20 | 3         ; user data selector, RPL 3
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push dword 0x20 | 3       ; SS
    push ecx                   ; ESP
    pushfd
    pop edx
    or edx, 0x200                ; IF -- ring3 code stays interruptible
    push edx                      ; EFLAGS
    push dword 0x18 | 3            ; CS
    push eax                        ; EIP
    iret

; int 0x80 entry point (installed via idt_set_gate() from kmain(),
; DPL 3 trap gate -- see arch/idt.h's IDT_TYPE_TRAP_GATE_32_DPL3).
; eax = syscall number (in), ebx = argument (in), eax = return value
; (out). Every other GP register is left genuinely clobbered -- not
; part of this minimal ABI's contract.
syscall_entry:
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10           ; kernel data selector -- DS/ES/FS/GS still
    mov ds, ax               ; hold whatever the ring3 caller had
    mov es, ax                ; loaded, and must not be trusted for
    mov fs, ax                 ; kernel-side work even though this
    mov gs, ax                  ; build's user/kernel data segments
                                  ; are numerically identical

    push ebx                ; arg
    push eax                 ; num
    call syscall_dispatch
    add esp, 8                ; eax now holds syscall_dispatch's
                                ; return value -- untouched below

    pop gs
    pop fs
    pop es
    pop ds
    iret
```

- [ ] **Step 2: Wire the build**

In `kernel/Makefile`, add `gdt.o` and `syscall.o` to `C_OBJS` (after `hexfmt.o`):

```makefile
C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o sb16.o fs.o editor.o serial.o scheduler.o synth.o boot_splash.o paging.o hexfmt.o gdt.o syscall.o
```

Add build rules (after the existing `hexfmt.o` rule):

```makefile
gdt.o: arch/gdt.c arch/gdt.h
	$(CC) $(CFLAGS) -c arch/gdt.c -o gdt.o

syscall.o: arch/syscall.c arch/syscall.h
	$(CC) $(CFLAGS) -c arch/syscall.c -o syscall.o
```

Add an asm rule (after the existing `context_switch.o` rule):

```makefile
ring3.o: arch/ring3.asm
	$(AS) -f elf32 arch/ring3.asm -o ring3.o
```

Update the link line and `clean` rule to include `ring3.o`:

```makefile
kernel.elf: kernel_entry.o context_switch.o ring3.o $(C_OBJS) arch/linker.ld
	$(LD) -m elf_i386 -T arch/linker.ld -nostdlib -o kernel.elf kernel_entry.o context_switch.o ring3.o $(C_OBJS)
```

```makefile
clean:
	rm -f kernel_entry.o context_switch.o ring3.o $(C_OBJS) kernel.elf kernel.bin
```

Update `kernel.o`'s prerequisite list to include the new headers it will `#include`:

```makefile
kernel.o: kernel.c gfx/graphics.h gfx/text.h drivers/mouse.h drivers/keyboard.h arch/interrupts.h gui/window.h gui/button.h gui/taskbar.h gui/startmenu.h gui/console_input.h gui/console_history.h gui/console_output.h forth/forth.h gui/shell.h arch/io.h drivers/ata.h fs/fs.h gui/editor.h forth/forth_hooks.h drivers/serial.h sched/scheduler.h drivers/sb16.h audio/synth.h gui/boot_splash.h arch/gdt.h arch/idt.h arch/syscall.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
```

- [ ] **Step 3: Wire `gdt_init()` and the syscall gate into `kmain()`**

In `kernel/kernel.c`, add two new includes after `#include "interrupts.h"` (line 10):

```c
#include "gdt.h"
```

and after `#include "paging.h"` (currently line 25):

```c
#include "idt.h"
#include "syscall.h"
```

Change:

```c
    serial_init();
    interrupts_init();
    paging_enable();
    mouse_init();
    interrupts_enable();
```

to:

```c
    gdt_init();
    serial_init();
    interrupts_init();

    /* extern, not declared in a header -- matches this codebase's
     * existing convention for asm-defined symbols only ever referenced
     * at their one call site (see kernel/sched/scheduler.c's own
     * extern void context_switch(...) declaration). */
    {
        extern void syscall_entry(void);
        idt_set_gate(0x80, (void *)syscall_entry, IDT_TYPE_TRAP_GATE_32_DPL3);
    }

    paging_enable();
    mouse_init();
    interrupts_enable();
```

`gdt_init()` runs first because everything downstream (`interrupts_init()`'s IDT gates, every existing ISR) assumes `GDT_KERNEL_CODE_SELECTOR`/`GDT_KERNEL_DATA_SELECTOR` (`0x08`/`0x10`) are already valid -- selector *values* don't change, so this is a like-for-like swap of which table backs them. `paging_enable()` keeps its existing position; nothing about the syscall gate or GDT changes when paging switches on.

- [ ] **Step 4: Rebuild and confirm a clean build**

Run: `cd kernel && make clean && make 2>&1 | tail -30`
Expected: builds `kernel.bin` with no warnings.

- [ ] **Step 5: Headless-QEMU regression check**

Nothing ring-3-related is invoked yet -- this confirms the new GDT/TSS/syscall gate are a true no-op for the existing boot path, the same bar sub-project (A)'s identity map was held to.

Run:
```bash
cd /home/norfolkh/os/boot
make
rm -f /tmp/qemu-mon.sock
qemu-system-i386 -accel kvm -display none -monitor unix:/tmp/qemu-mon.sock,server,nowait -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 &
sleep 3
echo "screendump /tmp/shot.ppm" | socat - unix-connect:/tmp/qemu-mon.sock
python3 -c "from PIL import Image; Image.open('/tmp/shot.ppm').save('/tmp/shot.png')"
echo "quit" | socat - unix-connect:/tmp/qemu-mon.sock
```
Then view `/tmp/shot.png`.
Expected: the normal desktop -- black backdrop, green cursor square, taskbar's green rule and `MENU` label at the bottom, no red panic banner, no hang.

- [ ] **Step 6: Commit**

```bash
git add kernel/arch/ring3.asm kernel/Makefile kernel/kernel.c
git commit -m "kernel: wire gdt_init() and the int 0x80 syscall gate into kmain()"
```

---

## Task 6: the one-time ring-3 proof, cleanup, and closeout

**Files:**
- Modify: `kernel/kernel.c` (temporarily, then reverted within this same task)
- Modify: `docs/BUILD_LOG.md`
- Modify: `docs/IDEAS.md`

**Interfaces:**
- Consumes: `paging_set_user()` (Task 1), `panic_with_code()`/`isr_general_protection` (Task 2, exercised for the first time here), `enter_ring3()` (Task 5), `SYS_TEST`/`SYS_EXIT` (Task 4).
- Produces: nothing new and permanent in code -- this task's own temporary code is added and then removed within itself, proving the mechanism built in Tasks 1-5 actually works before sub-project (B) is declared done.

- [ ] **Step 1: Add the temporary ring-3 test payload**

In `kernel/kernel.c`, immediately after `interrupts_enable();` (the end of the block Task 5 edited), add:

```c
    /* TEMPORARY -- one-time proof that ring 3 + the syscall ABI work,
     * removed later in this same commit's own history (see
     * docs/superpowers/specs/2026-08-27-ring3-syscall-design.md's
     * Testing section). Not present in the final state of this file. */
    {
        extern void enter_ring3(void (*entry)(void), void *user_stack_top);
        static uint8_t ring3_test_stack[1024] __attribute__((aligned(16)));
        paging_set_user(0, 1);
        enter_ring3(ring3_test_payload, ring3_test_stack + sizeof(ring3_test_stack));
    }
```

and, above `kmain()`'s own definition, add the payload function it references:

```c
/* TEMPORARY -- see the call site inside kmain(). Runs entirely at
 * CPL 3. Reaching the deliberate cli at all requires result ==
 * 0x1234, i.e. SYS_TEST's argument and return value both crossed the
 * ring3/ring0 boundary intact -- a broken round-trip instead falls
 * into the infinite loop below, a permanently black screen on
 * screendump, clearly distinguishable from the expected #GP banner. */
static void ring3_test_payload(void) {
    int result;
    /* Clobber list matches this ABI's own documented contract (see
     * ring3.asm's syscall_entry header comment): only eax is
     * guaranteed meaningful after int 0x80 returns, so GCC must be
     * told ecx/edx (caller-saved under cdecl, and ecx is syscall_entry's
     * own segment-selector scratch register) are not preserved across
     * it, plus memory since a syscall is a real side effect boundary. */
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(0), "b"(0) : "ecx", "edx", "memory");
    if (result == 0x1234) {
        __asm__ volatile("int $0x80" : : "a"(1), "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 */
    }
    for (;;) { }
}
```

- [ ] **Step 2: Rebuild and confirm a clean build**

Run: `cd kernel && make clean && make 2>&1 | tail -30`
Expected: builds `kernel.bin` with no warnings.

- [ ] **Step 3: Headless-QEMU proof screendump**

Run:
```bash
cd /home/norfolkh/os/boot
make
rm -f /tmp/qemu-mon.sock
qemu-system-i386 -accel kvm -display none -monitor unix:/tmp/qemu-mon.sock,server,nowait -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 &
sleep 3
echo "screendump /tmp/shot.ppm" | socat - unix-connect:/tmp/qemu-mon.sock
python3 -c "from PIL import Image; Image.open('/tmp/shot.ppm').save('/tmp/shot.png')"
echo "quit" | socat - unix-connect:/tmp/qemu-mon.sock
```
Then view `/tmp/shot.png`.
Expected: the red `#GP` panic banner reading `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000` (a privileged-instruction violation carries no selector index). This single screendump is proof of both halves at once, per Step 1's payload logic above.

If instead the screen is black (not the desktop, not a banner): the `for (;;) {}` fallback was hit, meaning `SYS_TEST` did not round-trip correctly -- do not proceed to Step 4; debug the syscall path (check `ring3.asm`'s register handling and `syscall_dispatch()`'s wiring) before continuing.

- [ ] **Step 4: Remove the temporary code**

Revert exactly the two additions from Step 1 -- delete the `{ ... }` block added after `interrupts_enable();` and delete the `ring3_test_payload()` function. Confirm with:

Run: `git diff kernel/kernel.c`
Expected: empty (or, if Task 5's commit already landed separately, the diff shows only Task 5's `gdt_init()`/syscall-gate lines -- nothing from Step 1 of this task remains).

- [ ] **Step 5: Rebuild and final regression screendump**

Run: `cd kernel && make clean && make 2>&1 | tail -30` (expect clean build), then repeat Step 3's exact QEMU sequence.
Expected: back to the normal desktop, identical to Task 5 Step 5's screendump -- confirming removal was clean and the permanent mechanism (GDT/TSS/syscall gate/`paging_set_user`/`#GP` decode) is still a true no-op with nothing invoking it.

- [ ] **Step 6: `docs/BUILD_LOG.md` entry**

Append a new dated entry (`## 2026-08-27 -- Ring 3 + a minimal syscall ABI (userspace sub-project B)`) summarizing: the GDT/TSS rebuild in C (`gdt_init()`), the `paging_set_user()` primitive and why it's necessary (the U/S bit gates instruction fetch too), the `int 0x80` trap-gate ABI (`eax` in/out, `ebx` argument, every other register clobbered) and why a hand-written asm stub was needed instead of `__attribute__((interrupt))`, the `#GP` decode upgrade, and the Step 3 screendump's exact banner text as the verification evidence -- following the same structure and level of detail as the `2026-08-26` paging entry immediately above it in the same file.

- [ ] **Step 7: `docs/IDEAS.md` update**

In the "Real userspace" entry (the one paragraph starting "(A), paging itself, has now also shipped..."), add a paragraph in the same style marking (B) done as of 2026-08-27: what shipped (GDT/TSS, syscall ABI, the ring-3 proof), and that (C) (a real syscall surface for fs/gfx/audio/window management) and (D) (a loadable/relocatable program format) remain entirely unbuilt -- mirroring exactly how the existing paragraph closed out (A) while listing (B)/(C)/(D) as still open.

- [ ] **Step 8: Commit**

```bash
git add docs/BUILD_LOG.md docs/IDEAS.md
git commit -m "kernel: prove ring 3 + the syscall ABI end-to-end; close out userspace sub-project (B)"
```

(Note: if Step 4's `git diff kernel/kernel.c` was empty, there's nothing further to stage from `kernel.c` itself -- Tasks 1-5's commits already carry the permanent code changes. This final commit is docs-only, matching how sub-project (A)'s own closeout commit was titled.)
