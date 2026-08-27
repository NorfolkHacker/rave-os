# `fs_read_file` Syscall Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give RaveOS its first syscall that touches a real kernel service: a ring-3 program can read an existing file's contents via `int 0x80`, proven end-to-end with a one-time headless-QEMU verification.

**Architecture:** `kernel/arch/syscall.c`'s existing (sub-project B) dispatcher is renamed to `syscall_dispatch_core()` and stays pure/host-testable, unchanged in behavior. A new file, `kernel/arch/syscall_fs.c`, defines the real `syscall_dispatch()` -- the exact name `ring3.asm` already calls -- handling a new `SYS_READ_FILE` syscall (arguments packed into a caller-built struct, passed as a single pointer in `ebx`, since `ring3.asm`'s one-argument ABI is otherwise unchanged) and falling through to `syscall_dispatch_core()` for everything else. `ring3.asm` needs no changes at all.

**Tech Stack:** C (`i686-elf-gcc`, freestanding), QEMU (headless verification via monitor socket + `socat`), host `gcc -m32` for pure-logic unit tests.

**Spec:** `docs/superpowers/specs/2026-08-27-fs-syscall-design.md`

## Global Constraints

- `SYS_READ_FILE` must equal `2` (`SYS_TEST`=0, `SYS_EXIT`=1 already exist from sub-project B).
- `struct sys_read_file_args` fields, in order: `const char *path; void *buf; unsigned int buf_size; unsigned int *out_size;` -- matching `fs_read_file()`'s own signature (`kernel/fs/fs.h`) exactly.
- `syscall_dispatch_core()` (renamed from B's `syscall_dispatch()`) must stay in `kernel/arch/syscall.c`, must stay pure (no `fs.h`, no other kernel module, no asm), and must be byte-identical in behavior to what it already does today.
- The real `syscall_dispatch()` (the name `kernel/arch/ring3.asm`'s `syscall_entry` already calls via `extern`/`call`) must be defined in the new `kernel/arch/syscall_fs.c`, not in `syscall.c` -- this is required, not stylistic: a C linker must resolve every symbol a translation unit references before producing any binary from it, so a `fs_read_file()`-calling case inside `syscall.c` itself would break `kernel/tests/test_syscall.c`'s existing host-test link (confirmed empirically during spec-writing).
- `kernel/arch/ring3.asm` must not be modified by this plan at all.
- Host test command (unchanged from sub-project B): `gcc -m32 -Wall -Wextra -o /tmp/test_syscall kernel/tests/test_syscall.c kernel/arch/syscall.c && /tmp/test_syscall` -- run from the repo root, zero warnings required, links only `syscall.c` (never `syscall_fs.c`).
- `i686-elf-gcc`/`i686-elf-ld`/`i686-elf-objcopy` are NOT on the default PATH in this environment -- every kernel/boot build must prefix `export PATH="$HOME/opt/cross/bin:$PATH"` first.
- Every kernel rebuild + headless-QEMU check: `cd boot && make` (rebuilds `kernel.bin` and `disk.img` together), then boot with `-display none -monitor unix:/tmp/qemu-mon.sock,server,nowait`, `screendump` over that socket via `socat`, convert the `.ppm` to `.png` with `python3`/Pillow, and view it.

---

## Task 1: rename `syscall_dispatch()` to `syscall_dispatch_core()`, add `syscall_fs.c`

**Files:**
- Modify: `kernel/arch/syscall.h`
- Modify: `kernel/arch/syscall.c`
- Create: `kernel/arch/syscall_fs.c`
- Modify: `kernel/tests/test_syscall.c`
- Modify: `kernel/Makefile`

**Interfaces:**
- Consumes: `fs_read_file(const char *path, void *buf, unsigned int buf_size, unsigned int *out_size)` (`kernel/fs/fs.h`, pre-existing, unchanged). `ring3.asm`'s `syscall_entry` (pre-existing, unchanged) -- its `extern syscall_dispatch` / `call syscall_dispatch` now resolve to this task's new `syscall_fs.c` definition instead of `syscall.c`'s old one.
- Produces: `int syscall_dispatch_core(int num, int arg);` (pure, in `syscall.c`) and `int syscall_dispatch(int num, int arg);` (real, in `syscall_fs.c`). `SYS_READ_FILE` (`= 2`) and `struct sys_read_file_args` (in `syscall.h`). Task 2 uses all of these directly in its temporary test payload.

- [ ] **Step 1: Write the failing test**

Modify `kernel/tests/test_syscall.c` -- replace every call to `syscall_dispatch(` with `syscall_dispatch_core(` (six call sites: the file becomes exactly this, unchanged otherwise):

```c
/* kernel/tests/test_syscall.c -- host-side only, never linked into
 * kernel.bin. syscall_dispatch_core() has no freestanding-only
 * dependencies (pure switch/return logic, no asm, no other kernel
 * module) -- same convention as test_paging.c/test_gdt.c. The real
 * int 0x80 entry path (ring3.asm's syscall_entry, and syscall_fs.c's
 * syscall_dispatch(), which SYS_READ_FILE touches) can only be
 * verified by actually booting (see this feature's BUILD_LOG entry
 * for the one-time headless-QEMU verification). */
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
    check(syscall_dispatch_core(SYS_TEST, 0) == 0x1234, "SYS_TEST should return the fixed value 0x1234");
    check(syscall_dispatch_core(SYS_TEST, 999) == 0x1234, "SYS_TEST should ignore its argument and still return 0x1234");
    check(syscall_dispatch_core(SYS_EXIT, 0) == 0, "SYS_EXIT should return 0");
    check(syscall_dispatch_core(SYS_EXIT, 42) == 0, "SYS_EXIT should return 0 regardless of its argument");
    check(syscall_dispatch_core(42, 0) == -1, "an unknown syscall number should return -1");
    check(syscall_dispatch_core(-1, 0) == -1, "a negative syscall number should return -1");

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
Expected: compile error, `implicit declaration of function 'syscall_dispatch_core'` (or a link failure for the same reason) -- `syscall_dispatch_core` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

Replace `kernel/arch/syscall.h`'s entire contents with:

```c
#ifndef RAVEOS_SYSCALL_H
#define RAVEOS_SYSCALL_H

/* Returns a fixed, recognizable value -- pure plumbing, proves a
 * syscall's argument and return value both cross the ring3/ring0
 * boundary intact. Ignores arg. */
#define SYS_TEST 0

/* Reserved syscall number for "the caller is done"; currently just
 * returns 0 and does nothing else -- this minimal ABI has no
 * process/address-space/scheduler-slot concept to actually tear down
 * (see docs/superpowers/specs/2026-08-27-ring3-syscall-design.md's
 * Out of Scope), so this is not real process termination. Ignores arg. */
#define SYS_EXIT 1

/* Reads an existing file's contents into a ring-3-owned buffer,
 * wrapping fs_read_file() (kernel/fs/fs.h) with zero changes to its
 * behavior. arg is the address of a struct sys_read_file_args built
 * by the caller -- ring3.asm's ABI still passes exactly one value in
 * ebx; for this syscall, that value is a pointer instead of a plain
 * integer. */
#define SYS_READ_FILE 2

/* Carries fs_read_file()'s four arguments across the syscall boundary
 * as a single pointer. Field types and order match fs_read_file()'s
 * own signature exactly (kernel/fs/fs.h) -- this struct exists only
 * to fit four arguments through one register, not to add or
 * reinterpret any of them. */
struct sys_read_file_args {
    const char *path;
    void *buf;
    unsigned int buf_size;
    unsigned int *out_size;
};

/* Pure -- exactly what sub-project (B) shipped as syscall_dispatch(),
 * renamed. SYS_TEST/SYS_EXIT/default only, zero dependency on fs.h or
 * any other kernel module. This is what kernel/tests/test_syscall.c
 * links and calls directly. */
int syscall_dispatch_core(int num, int arg);

/* Real -- defined in syscall_fs.c, not syscall.c. This is the exact
 * name ring3.asm's syscall_entry already calls; giving the real
 * dispatcher this name in a different file means ring3.asm needs no
 * changes at all. Handles SYS_READ_FILE, falls through to
 * syscall_dispatch_core() for everything else. */
int syscall_dispatch(int num, int arg);

#endif
```

Replace `kernel/arch/syscall.c`'s entire contents with:

```c
#include "syscall.h"

int syscall_dispatch_core(int num, int arg) {
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

Create `kernel/arch/syscall_fs.c`:

```c
#include "syscall.h"
#include "fs.h"

int syscall_dispatch(int num, int arg) {
    if (num == SYS_READ_FILE) {
        const struct sys_read_file_args *a = (const struct sys_read_file_args *)arg;
        return fs_read_file(a->path, a->buf, a->buf_size, a->out_size);
    }
    return syscall_dispatch_core(num, arg);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -m32 -Wall -Wextra -o /tmp/test_syscall kernel/tests/test_syscall.c kernel/arch/syscall.c && /tmp/test_syscall`
Expected: `PASS: all syscall tests passed`, zero warnings. (This command links only `syscall.c` -- confirms `syscall_dispatch_core()` stays fully host-testable and, critically, that nothing in `syscall.c` itself needs `fs_read_file` resolved.)

- [ ] **Step 5: Wire the build**

In `kernel/Makefile`, add `syscall_fs.o` to `C_OBJS` (after the existing `syscall.o`):

```makefile
C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o sb16.o fs.o editor.o serial.o scheduler.o synth.o boot_splash.o paging.o hexfmt.o gdt.o syscall.o syscall_fs.o
```

Add a build rule (after the existing `syscall.o` rule):

```makefile
syscall_fs.o: arch/syscall_fs.c arch/syscall.h fs/fs.h
	$(CC) $(CFLAGS) -c arch/syscall_fs.c -o syscall_fs.o
```

- [ ] **Step 6: Rebuild and confirm a clean build**

Run:
```
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel && make clean && make 2>&1 | tail -20
```
Expected: builds `kernel.bin` with no warnings beyond the pre-existing linker warning `kernel.elf has a LOAD segment with RWX permissions`.

- [ ] **Step 7: Headless-QEMU regression check**

Nothing new is invoked yet -- `SYS_READ_FILE` exists in `syscall_dispatch()`'s permanent switch, but nothing calls `int 0x80` with that number anywhere in the shipped tree. This confirms the rename + file split + Makefile wiring are a true no-op for the existing boot path.

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

- [ ] **Step 8: Commit**

```bash
git add kernel/arch/syscall.h kernel/arch/syscall.c kernel/arch/syscall_fs.c kernel/tests/test_syscall.c kernel/Makefile
git commit -m "kernel: split syscall_dispatch() into a pure core + a real fs-touching dispatcher"
```

---

## Task 2: the ring-3 `SYS_READ_FILE` proof, cleanup, and closeout

**Files:**
- Modify: `kernel/kernel.c` (temporarily, then reverted within this same task)
- Modify: `docs/BUILD_LOG.md`
- Modify: `docs/IDEAS.md`

**Interfaces:**
- Consumes: `syscall_dispatch()`/`SYS_READ_FILE`/`struct sys_read_file_args` (Task 1), `enter_ring3()`/`paging_set_user()` (sub-project B, unchanged).
- Produces: nothing new and permanent in code -- this task's own temporary code is added and then removed within itself, proving `SYS_READ_FILE` actually works before this slice of sub-project (C) is declared done.

- [ ] **Step 1: Add the temporary ring-3 test payload**

In `kernel/kernel.c`, add `#include "syscall.h"` to the include list (after `#include "idt.h"`, alongside the other arch-level includes) -- this is temporary; see Step 4.

Immediately after `interrupts_enable();` in `kmain()` (currently the line right after the block Task 1 of sub-project B's plan added), add:

```c
    /* TEMPORARY -- one-time proof that SYS_READ_FILE works, removed
     * later in this same task's own history (see
     * docs/superpowers/specs/2026-08-27-fs-syscall-design.md's
     * Testing section). Not present in the final state of this file. */
    {
        extern void enter_ring3(void (*entry)(void), void *user_stack_top);
        static uint8_t ring3_fs_test_stack[1024] __attribute__((aligned(16)));
        paging_set_user(0, 1);
        enter_ring3(ring3_fs_test_payload, ring3_fs_test_stack + sizeof(ring3_fs_test_stack));
    }
```

and, above `kmain()`'s own definition, add the payload function it references:

```c
/* TEMPORARY -- see the call site inside kmain(). Runs entirely at
 * CPL 3. Reads /BIN/HELLO (seeded at every boot, further up in this
 * same file: ": GREET 42 . CR ;\nGREET\n", 24 bytes) via
 * SYS_READ_FILE. Reaching the deliberate cli at all requires the read
 * to have actually succeeded -- a broken syscall path instead falls
 * into the infinite loop below, a permanently black screen on
 * screendump, clearly distinguishable from the expected #GP banner. */
static void ring3_fs_test_payload(void) {
    static char buf[64];
    unsigned int out_size = 0;
    static const struct sys_read_file_args args = {
        "/BIN/HELLO", buf, sizeof(buf), &out_size
    };
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(SYS_READ_FILE), "b"(&args) : "ecx", "edx", "memory");
    if (result >= 0 && out_size == 24 &&
        buf[0] == ':' && buf[1] == ' ' && buf[2] == 'G' && buf[3] == 'R') {
        __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 */
    }
    for (;;) { }
}
```

- [ ] **Step 2: Rebuild and confirm a clean build**

Run: `export PATH="$HOME/opt/cross/bin:$PATH"; cd kernel && make clean && make 2>&1 | tail -30`
Expected: builds `kernel.bin` with no warnings beyond the pre-existing RWX-segment linker warning.

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
Expected: the red `#GP` panic banner reading `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000` -- reaching it is proof `/BIN/HELLO`'s real, on-disk content came back through `SYS_READ_FILE` correctly (matching sub-project B's exact proof banner, since this reuses B's deliberate-fault tail).

If instead the screen is black (not the desktop, not a banner): the `for (;;) {}` fallback was hit, meaning `SYS_READ_FILE` did not round-trip correctly -- do not proceed to Step 4; debug the syscall path (check `syscall_fs.c`'s dispatch, the args struct's field order, and `fs_read_file()`'s own return contract) before continuing.

- [ ] **Step 4: Remove the temporary code**

Revert exactly the additions from Step 1 -- delete the `{ ... }` block added after `interrupts_enable();`, delete the `ring3_fs_test_payload()` function, and delete the `#include "syscall.h"` line you added (nothing permanent in `kernel.c` needs it -- the permanent `idt_set_gate(0x80, ...)` call added by sub-project B only needs `idt.h`'s `IDT_TYPE_TRAP_GATE_32_DPL3`, not anything from `syscall.h`; leaving the include in would reintroduce the exact dead-include class of finding sub-project B's own final review already had to catch once).

Confirm with:

Run: `git diff kernel/kernel.c`
Expected: empty.

- [ ] **Step 5: Rebuild and final regression screendump**

Run: `export PATH="$HOME/opt/cross/bin:$PATH"; cd kernel && make clean && make 2>&1 | tail -30` (expect clean build), then repeat Step 3's exact QEMU sequence.
Expected: back to the normal desktop, identical to Task 1 Step 7's screendump -- confirming removal was clean and `SYS_READ_FILE` being wired into the permanent switch is still a true no-op with nothing invoking it.

- [ ] **Step 6: `docs/BUILD_LOG.md` entry**

Append a new dated entry (`## 2026-08-27 -- A real fs_read_file syscall (userspace sub-project C, first slice)`) summarizing: the `syscall_dispatch_core()`/`syscall_fs.c` split and *why* it was necessary (a C linker needs `fs_read_file` resolved for the whole translation unit, not just the reachable code path -- confirmed empirically during spec-writing, unlike `paging.c`/`gdt.c`'s pure/real splits which stay in one file because their real halves are impure only via inline asm), the args-struct ABI choice and why a register-based extension was rejected (would touch `ring3.asm`'s register-scratch discipline, the exact routine with two prior real bugs), and the Step 3 screendump's exact banner text as the verification evidence -- following the same structure and level of detail as the `2026-08-27` ring3-syscall entry immediately above it in the same file.

- [ ] **Step 7: `docs/IDEAS.md` update**

In the "Real userspace" entry (the paragraph that already marks (B) done, dated 2026-08-27), add a paragraph in the same style marking this first slice of (C) done as of 2026-08-27: what shipped (`SYS_READ_FILE`, wrapping `fs_read_file()`), and that this is deliberately only one operation out of `fs.h`'s ~13 -- the rest of `fs`'s surface, plus gfx/audio/window-management entirely, remain unbuilt, separate future work (window-management and audio in particular need real new per-program ownership design, not just a thin wrapper, per this spec's own Purpose section).

- [ ] **Step 8: Commit**

```bash
git add docs/BUILD_LOG.md docs/IDEAS.md
git commit -m "kernel: prove SYS_READ_FILE end-to-end; close out sub-project (C)'s first slice"
```

(Note: if Step 4's `git diff kernel/kernel.c` was empty, there's nothing further to stage from `kernel.c` itself -- Task 1's commit already carries the permanent code changes. This final commit is docs-only, matching how sub-projects (A) and (B) both closed out.)
