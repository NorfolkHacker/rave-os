# Ring 3 + a minimal syscall ABI

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry decomposes into (A) paging,
(B) ring 3 + a minimal syscall ABI, (C) a real syscall surface for
existing kernel services, (D) a loadable/relocatable program format.
(A) shipped 2026-08-26 (see
`docs/superpowers/specs/2026-08-26-paging-design.md`): the CPU now
always runs with a static, supervisor-only, identity-mapped page
directory. This spec is (B) -- getting the CPU to actually execute
code at CPL 3, and back again via a syscall, for the first time in
this kernel's history.

**This spec deliberately does not build anything ring 3 code is
useful for.** No real kernel service is callable from ring 3 -- that's
(C). No loadable program exists to run there -- that's (D). What this
spec proves, and only this: a GDT with real ring 3 segments and a TSS
exists, `iret` can drop CPL to 3 and code there actually runs with
reduced privilege (a CPL0-only instruction executed there raises
`#GP`, not silently succeeds), and a syscall (`int 0x80`) can carry an
argument in and a return value out across that boundary correctly.

## Scope

**In scope:**
- `kernel/arch/gdt.c`/`.h` -- a kernel-owned GDT (null, kernel
  code/data at the same `0x08`/`0x10` selectors `idt.c` already
  assumes, user code/data at `0x18`/`0x20`, a TSS descriptor at
  `0x28`), replacing `boot/stage2.asm`'s static one as the table the
  kernel actually runs on. A static `struct tss` with `esp0`/`ss0`
  pointing at a dedicated kernel-mode stack, loaded via `ltr`.
- `kernel/arch/syscall_entry.asm` + `kernel/arch/syscall.c`/`.h` -- an
  `int 0x80` IDT gate (DPL 3, trap gate) whose hand-written asm entry
  stub saves/restores the data segment registers, calls a C
  dispatcher with `eax`=syscall number/`ebx`=argument, and returns the
  dispatcher's result in `eax`. Two syscalls: `SYS_TEST` (returns a
  fixed, recognizable value) and `SYS_EXIT` (sets a flag; see Design
  for why it can't do more than that yet).
- `enter_ring3()`, a small asm helper building an `iret` frame to drop
  CPL0 -> CPL3.
- `kernel/arch/paging.c`/`.h` -- one small addition,
  `paging_set_user(pde_index, user)`, toggling the User/Supervisor bit
  on one already-built PDE in place. Necessary, not optional: every
  PDE (A) built is supervisor-only, and the U/S bit gates *all*
  access including instruction fetch, not just data -- ring 3 code
  can't fetch its own first instruction without it.
- `isr.c` -- upgrades the existing (currently generic-banner)
  `isr_general_protection` handler to decode and display its error
  code, the same way (A) upgraded the page-fault handler. No `CR2`
  involved (`#GP` doesn't set it, unlike `#PF`).
- One-time, throwaway verification during this task's own testing
  pass: a temporary `kmain()` hook enters ring 3, round-trips
  `SYS_TEST`, then deliberately executes `cli` (CPL0-only) to trigger
  `#GP` on purpose. Removed before shipping, mirroring exactly how (A)
  added and then removed its deliberate page-fault trigger.

**Out of scope, deliberately:**
- **A real syscall surface for fs/gfx/audio/window management.**
  Sub-project (C). `SYS_TEST`/`SYS_EXIT` are plumbing, not services --
  neither touches any kernel subsystem.
- **A loadable/relocatable program format.** Sub-project (D). The
  ring 3 test payload is a C function compiled straight into the
  kernel binary, exactly like every other "program" today, just
  executed at CPL 3 instead of CPL 0.
- **Scheduler integration.** `kernel/sched/scheduler.c`'s fiber pool
  is untouched -- this spec's ring 3 execution is a standalone,
  one-off proof, not a new kind of runnable program slot. Cooperative
  fibers keep sharing ring 0 exactly as they do today. Tying ring 3
  into the scheduler needs a real notion of a separate program to run
  that way, which doesn't exist until (D).
- **Per-process address spaces, or real memory isolation.**
  `paging_set_user()` is a blunt, permanent primitive (flip one PDE's
  U bit), not a policy. This spec's own temporary test uses it on the
  PDE containing the kernel's own code/data/stack (there is nowhere
  else to put a compiled-in payload without linker changes), meaning
  ring 3 code can address all of kernel memory within that PDE for as
  long as the flag is set -- zero isolation, deliberately. Real
  per-process address spaces are (D)-and-beyond territory.
- **`SYS_EXIT` as real process termination.** There is no process to
  tear down (no loadable program, no address space, no scheduler
  slot) -- see Design for what it actually does.
- **Preserving general-purpose registers across a syscall**, beyond
  `eax` (the return value). This minimal ABI's contract: `eax` is
  defined after `int 0x80` returns; every other GP register is
  clobbered. A real contract is (C)'s problem, once there's a real
  surface worth calling with real arguments.
- **`SYSENTER`/`SYSEXIT`** or any MSR-based fast syscall path.
  Rejected in brainstorming: adds MSR setup and hardware-model
  assumptions for a speed concern that doesn't exist yet (there are no
  real programs to make syscalls at any volume).

## Design

### GDT: `kernel/arch/gdt.c`/`.h`

Standard flat-model descriptor layout, six entries:

```c
struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char  base_mid;
    unsigned char  access;
    unsigned char  granularity;
    unsigned char  base_high;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdtp;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, unsigned char access, unsigned char gran) {
    gdt[i].base_low    = (unsigned short)(base & 0xFFFF);
    gdt[i].base_mid    = (unsigned char)((base >> 16) & 0xFF);
    gdt[i].base_high   = (unsigned char)((base >> 24) & 0xFF);
    gdt[i].limit_low   = (unsigned short)(limit & 0xFFFF);
    gdt[i].granularity = (unsigned char)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access      = access;
}
```
`gran`'s low nibble is discarded -- only its top nibble (`G`/`D`/`L`/`AVL`,
`0xC` for every 4KB-granular 32-bit entry here) is used; the
descriptor's actual low nibble comes from `limit`'s bits 19:16. Table
below shows each entry's fully-resolved granularity byte for
reference, not a literal call argument.

| # | Selector | Segment | Base/Limit | Access | Notes |
|---|----------|---------|------------|--------|-------|
| 0 | --   | null | 0/0 | 0x00 | required |
| 1 | 0x08 | kernel code | 0 / 4GB | 0x9A | present, DPL0, exec/read -- must match `idt.c`'s `KERNEL_CODE_SEGMENT` |
| 2 | 0x10 | kernel data | 0 / 4GB | 0x92 | present, DPL0, read/write |
| 3 | 0x18 | user code   | 0 / 4GB | 0xFA | present, DPL3, exec/read (`0x9A \| 0x60`) |
| 4 | 0x20 | user data   | 0 / 4GB | 0xF2 | present, DPL3, read/write (`0x92 \| 0x60`) |
| 5 | 0x28 | TSS         | `&tss` / `sizeof(tss)-1` | 0x89 | present, DPL0, 32-bit TSS available |

Base 0 / limit 4GB on every non-TSS entry keeps this the same flat
model the codebase already runs on (`boot/stage2.asm`'s GDT is
base-0 too) -- the only thing that changes between kernel and user
segments is the DPL, not addressing.

```c
struct tss_entry {
    uint32_t prev_tss, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

static struct tss_entry tss;
static uint8_t syscall_kernel_stack[4096] __attribute__((aligned(16)));

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    tss.ss0 = 0x10;
    tss.esp0 = (uint32_t)(syscall_kernel_stack + sizeof(syscall_kernel_stack));
    tss.iomap_base = sizeof(tss);   /* no I/O bitmap -- ring 3 gets no port access */
    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)&gdt[0];
    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp $0x08, $1f\n\t"   /* reload CS through the new table */
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
        : "m"(gdtp)
        : "eax", "memory"
    );
}
```

`tss.esp0`/`ss0` is what the CPU auto-loads for `ESP`/`SS` on *any*
ring3->ring0 transition (a syscall or a fault) -- this is why a TSS is
mandatory even though this kernel never does a hardware task switch.
`syscall_kernel_stack` is a dedicated 4KB static buffer, not any
existing fiber stack: keeping it separate means a syscall/fault taken
while inside a ring 3 payload never touches whatever stack the
kernel's own main flow happens to be using.

`gdt_init()` must run before anything else in `kmain()` -- before
`interrupts_init()` -- since it's what makes the `0x08`/`0x10`
selectors `idt_set_gate()` and every ISR already assume into a table
the kernel itself owns, rather than the one `boot/stage2.asm` built to
get here. Selector *values* don't change, so this is a like-for-like
swap as far as everything downstream is concerned.

### `paging_set_user()`: `kernel/arch/paging.c`/`.h`

```c
/* Sets (user=1) or clears (user=0) the User/Supervisor bit (bit 2) on
 * page_directory[pde_index] in place. Does not rebuild the directory
 * or touch any other entry. pde_index must be < PAGE_DIRECTORY_ENTRIES
 * and already built present by paging_build_directory() -- toggling
 * the bit on a not-present entry is legal (the bit is simply ignored
 * by the MMU until the entry is present) but pointless. */
void paging_set_user(uint32_t pde_index, int user);
```

Operates on the module-static `page_directory` array already declared
in `paging.c` -- `pde_index`'s existing value gets `| PDE_USER_FLAG`
(`0x4`) or `& ~PDE_USER_FLAG`, nothing else changes. No `paging_build_directory()`
signature change, no new caller-visible state beyond this one function.

### Syscall path: `kernel/arch/syscall.c`/`.h` + `syscall_entry.asm`

```c
#define SYS_TEST 0   /* returns a fixed value -- proves the round trip */
#define SYS_EXIT 1   /* sets ring3_test_done; see below */

int syscall_dispatch(int num, int arg) {
    (void)arg;
    switch (num) {
        case SYS_TEST: return 0x1234;
        case SYS_EXIT: ring3_test_done = 1; return 0;
        default:       return -1;
    }
}

void syscall_init(void) {
    idt_set_gate(0x80, (void *)syscall_entry, IDT_TYPE_TRAP_GATE_32_DPL3);
}
```

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
This is a new constant alongside `idt.h`'s existing
`IDT_TYPE_INTERRUPT_GATE_32`. A trap gate (not interrupt gate) is used
so `int 0x80` doesn't clear `IF` -- keyboard/mouse IRQs stay
deliverable while a syscall is executing, same as on any real OS.
`syscall_init()` runs in `kmain()` right after `interrupts_init()`:
`idt_set_gate()` just writes into the already-`lidt`'d array in place,
so ordering relative to `idt_init()`'s own `lidt` call doesn't matter,
only that it happens before ring 3 code could ever execute `int
0x80`.

`syscall_entry.asm` is hand-written, not `__attribute__((interrupt))`
like every other ISR in this codebase (`isr.c`'s own header comment
explains why that attribute is used everywhere else: it generates the
save/restore/`iret` a normal handler needs). It can't be used here
because a syscall genuinely needs to read `eax`/`ebx` as *arguments*
and leave a *chosen* value in `eax` as the return -- register-precise
control the interrupt attribute doesn't expose. `context_switch.asm`
already establishes the precedent for hand-written asm exactly when C
can't express the needed register discipline.

```asm
; int syscall_dispatch(int num, int arg); -- cdecl, called below
global syscall_entry
extern syscall_dispatch

syscall_entry:
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10        ; kernel data selector -- DS/ES/FS/GS still
    mov ds, ax           ; hold whatever the ring3 caller had loaded,
    mov es, ax           ; and must not be trusted for kernel-side
    mov fs, ax           ; work even though this build's user/kernel
    mov gs, ax           ; data segments are numerically identical

    push ebx             ; arg
    push eax             ; num
    call syscall_dispatch
    add esp, 8            ; eax now holds syscall_dispatch's return --
                           ; untouched by anything below

    pop gs
    pop fs
    pop es
    pop ds
    iret
```

`eax` is deliberately never saved/restored: it is the one register
this minimal ABI defines as meaningful after `int 0x80` returns
(`syscall_dispatch`'s C return value, via the ordinary cdecl
return-in-`eax` convention), so leaving it alone is what makes the
return value reach the caller at all. Every other GP register
(`ecx`/`edx`/`esi`/`edi`/`ebp`) is left genuinely clobbered -- not
saved, not restored, not part of this minimal contract (see Out of
Scope).

### `enter_ring3()`: dropping to CPL 3

```asm
; void enter_ring3(void (*entry)(void), void *user_stack_top);
global enter_ring3
enter_ring3:
    mov eax, [esp+4]     ; entry
    mov ecx, [esp+8]     ; user_stack_top

    mov dx, 0x20 | 3      ; user data selector, RPL 3
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push dword 0x20 | 3   ; SS
    push ecx               ; ESP
    pushf
    pop edx
    or edx, 0x200            ; IF -- ring3 code stays interruptible
    push edx                  ; EFLAGS
    push dword 0x18 | 3        ; CS
    push eax                    ; EIP
    iret
```

The classic ring0->ring3 idiom: build the five-word frame `iret`
expects for a privilege-level change and let it do the CPL switch.
Never returns to its caller -- there is no mechanism in this minimal
design for ring 3 code to hand control back to whatever called
`enter_ring3()` (that would need at least a real return-from-syscall
convention, which is (C)'s problem); this spec's own use of it ends in
a deliberate fault instead (see Testing).

### `isr.c`: decoding `#GP`

```c
static void panic_with_code(const char *msg, unsigned int error_code) {
    /* Same banner/text layout as panic_with_addr() (kernel/arch/isr.c),
     * minus the address line -- CR2 is meaningless for #GP (only #PF
     * sets it). error_code is nonzero only when a specific segment
     * selector caused the fault; 0 covers the far more common case (a
     * privileged instruction executed at insufficient CPL), which
     * this spec's own verification exercises. The exact buffer-
     * building code is an implementation detail for the plan to write
     * out in full -- mechanically the same hex32_to_str()-plus-
     * manual-copy-loop panic_with_addr() already does, just one field
     * shorter. */
}

__attribute__((interrupt)) static void isr_general_protection(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    panic_with_code("PANIC: GENERAL PROTECTION FAULT", error_code);
}
```

### Boot order

```c
gdt_init();
interrupts_init();
syscall_init();
paging_enable();
mouse_init();
interrupts_enable();
```

`gdt_init()` first (everything downstream assumes its selectors);
`paging_enable()` keeps its existing position from (A) -- nothing
about ring 3/syscalls changes when paging is switched on, since (B)
adds no new page mappings, only a per-PDE flag toggle used solely by
this task's own throwaway test.

## Testing

**Host-buildable**, `kernel/tests/test_gdt.c` and
`kernel/tests/test_paging.c` (extending the existing file): assert
`gdt_set_entry()` packs base/limit/access into the documented byte
layout for a few representative inputs (base 0, limit 0xFFFFFFFF, and
a non-trivial base like the TSS case), and assert `paging_set_user()`
only ever changes bit 2 of the targeted entry -- every other bit,
and every other entry in the array, is untouched before vs. after.

**Headless QEMU, one-time and throwaway**, same technique (A) used
(`-accel kvm -display none`, monitor socket + `socat`):

1. **Regression, ring3/syscall machinery wired in but never invoked:**
   boot, screendump, confirm the normal desktop -- `gdt_init()`
   replacing stage2's GDT and `syscall_init()` adding an IDT gate must
   be true no-ops for every existing code path, the same bar (A) held
   its identity map to.
2. **The proof itself:** temporarily add, right after
   `interrupts_enable()`:
   ```c
   static uint8_t ring3_test_stack[1024] __attribute__((aligned(16)));
   paging_set_user(0, 1);
   enter_ring3(ring3_test_payload, ring3_test_stack + sizeof(ring3_test_stack));
   ```
   where `ring3_test_payload` (also temporary) is:
   ```c
   static void ring3_test_payload(void) {
       int result;
       __asm__ volatile("int $0x80" : "=a"(result) : "a"(0), "b"(0));
       if (result == 0x1234) {
           __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
           __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 */
       }
       for (;;) { }
   }
   ```
   Boot headless, screendump. Expected: the red `#GP` panic banner,
   with `error_code=0x00000000` (a privileged-instruction violation
   carries no selector index). This single screendump is proof of
   *both* halves at once -- reaching the deliberate `cli` at all
   requires `result == 0x1234`, i.e. `SYS_TEST`'s argument and return
   value both crossed the ring3/ring0 boundary intact; a broken
   round-trip would instead show a permanently black screen (the
   `for (;;) {}` fallback), a clearly distinguishable failure mode
   from the expected banner.
3. **Cleanup:** remove `ring3_test_payload`, the `paging_set_user`/
   `enter_ring3` call, and the temporary test stack. `git diff` against
   the Task-2 (regression-verified) state should show only the
   permanent files: `gdt.c`/`.h`, `syscall.c`/`.h`,
   `syscall_entry.asm`, `enter_ring3`'s asm, `paging_set_user()`, and
   `isr.c`'s `#GP` upgrade. Final rebuild + headless boot confirms the
   desktop is back to the same normal state as step 1.
