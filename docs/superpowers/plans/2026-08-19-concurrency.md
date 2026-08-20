# Cooperative Concurrency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Rave-OS a cooperative fiber scheduler so long-running Forth scripts (PAINT's `PLOOP`) no longer freeze `kmain()`'s own per-frame loop, and delete the five hand-copied workaround hooks that problem caused.

**Architecture:** A fixed pool of program slots, each with its own static stack; one assembly `context_switch()` primitive swaps between them. `forth.c`'s compiler auto-inserts a yield call at every `BEGIN...UNTIL` back-edge, so no script has to opt in. `RUN` becomes "spawn a program and return" instead of "block until done." `kmain()` stays the always-running, kernel-resident driver, ticking the scheduler once per frame.

**Tech Stack:** C (freestanding, `-m32 -ffreestanding -nostdlib`), NASM (`elf32`), this project's existing headless-QEMU test workflow (`socat` + monitor + screendump + Python/PIL pixel inspection).

**Spec:** `docs/superpowers/specs/2026-08-19-concurrency-design.md`

## Global Constraints

- `MAX_PROGRAMS = 4`, `PROGRAM_STACK_SIZE = 8192` bytes per program stack, both static allocations (no heap/`malloc` exists in this kernel).
- No ring 3, no paging, no syscall ABI -- everything stays ring 0, one flat address space. A crashing program can still corrupt the kernel; this plan does not defend against that beyond a stack-overflow canary.
- Cooperative only -- no timer interrupt, no preemption. A yield point only exists at `BEGIN...UNTIL` back-edges (the only loop construct this Forth has); a hypothetical future non-Forth program with a raw non-yielding loop is out of scope.
- `kmain()`'s own window-manager loop stays kernel-resident and always runs every frame -- it is never itself a scheduled program.
- Each scheduled Forth program gets its own private `struct forth_vm` -- no shared interpreter state between programs. This is an observable behavior change from today: words a `RUN`-launched script defines no longer remain callable from the interactive console after the script finishes.
- Close-then-timeout grace period: `SCHEDULER_CLOSE_GRACE_FRAMES = 60`.
- Build via `distrobox enter forth-os -- bash -c "cd <dir> && make clean && make"`; both `kernel/` and `boot/` must be rebuilt for a real QEMU/hardware test (a stale `disk.img` bundling an old `kernel.bin` has bitten this project before).

---

## File Structure

- **Create `kernel/context_switch.asm`** -- the one assembly primitive, `context_switch(uint32_t *save_esp_here, uint32_t new_esp)`. No kernel dependencies; also builds as a normal hosted object for host-side testing.
- **Create `kernel/scheduler.h` / `kernel/scheduler.c`** -- program slot table, `context_switch()`-based fiber start/yield/tick, close-timeout tracking. Deliberately as self-contained as `forth.c` already is (no `graphics.h`/`window.h`/etc.) so it stays host-testable and reusable by any future non-Forth program.
- **Modify `kernel/forth.h`** -- add `OP_CALL_YIELD` to `enum forth_op`.
- **Modify `kernel/forth.c`** -- emit `OP_CALL_YIELD` at every compiled `UNTIL`; handle it in `forth_exec()`; add `WINDOW-CLOSED?`; delete `PALETTE-PICK`/`SAVE-PICK`/`REFRESH` and their `prim_*` wrappers.
- **Modify `kernel/forth_hooks.h`** -- add `forth_hook_yield()`/`forth_hook_window_closed()`; delete `forth_hook_refresh()`/`forth_hook_palette_pick()`/`forth_hook_save_pick()`.
- **Modify `kernel/kernel.c`** -- `#include "scheduler.h"`; implement the two new hooks; call `scheduler_tick()` once per frame and fix the idle-`hlt` path; replace synchronous `forth_run_command()` with a scheduled `run_program_entry()`; delete the three old hook implementations and `forth_hook_paint_open()`'s direct `forth_hook_refresh()` call; update `bin_paint_default[]`'s script text; wire `paint_program_slot` + `scheduler_request_close()` into the window-close handler.
- **Modify `kernel/Makefile`** -- build `context_switch.o` (NASM) and `scheduler.o` (GCC), link both into `kernel.elf`, add `scheduler.h` to `kernel.o`'s and `forth.o`'s dependency lines (`forth.o` doesn't need `scheduler.h` itself -- only `forth_hooks.h`, unchanged there).
- **Create `kernel/tests/test_context_switch.c`, `kernel/tests/test_scheduler.c`** -- host-side (non-freestanding) tests, built and run natively inside the `forth-os` container, not part of `kernel.bin`.

---

### Task 1: `context_switch()` assembly primitive

**Files:**
- Create: `kernel/context_switch.asm`
- Create: `kernel/tests/test_context_switch.c`
- Test: run natively inside the `forth-os` distrobox container (no QEMU needed)

**Interfaces:**
- Produces: `void context_switch(uint32_t *save_esp_here, uint32_t new_esp);` (cdecl, 32-bit) -- every later task's fiber switching goes through this exact signature.

- [x] **Step 1: Write `context_switch.asm`**

```nasm
; void context_switch(uint32_t *save_esp_here, uint32_t new_esp)
;
; Saves the four callee-saved registers (ebx/esi/edi/ebp) onto whichever
; stack is currently active, stores the resulting esp into
; *save_esp_here, switches esp to new_esp, and restores the other
; side's saved registers before ret'ing -- which resumes execution
; wherever that side last called context_switch() itself (or, the
; first time a program stack is used, wherever its stack was primed to
; "return" into -- see scheduler.c's scheduler_activate()). Same code
; serves both directions: it's a symmetric swap, not two routines.
global context_switch
section .text
context_switch:
    push ebp
    push edi
    push esi
    push ebx
    ; 4 pushes = 16 bytes consumed, so the caller's own stack layout
    ; (return addr, then the two arguments) is now 16 bytes higher up.
    mov eax, [esp+20]      ; save_esp_here
    mov [eax], esp
    mov eax, [esp+24]      ; new_esp
    mov esp, eax
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
```

- [x] **Step 2: Write the host-side test**

```c
/* kernel/tests/test_context_switch.c -- host-side only, never linked
 * into kernel.bin. Proves context_switch() both (a) can "start" a
 * hand-primed stack (the same priming scheduler.c's
 * scheduler_activate() will do) and (b) can yield back and forth
 * between two contexts repeatedly. */
#include <stdio.h>
#include <stdint.h>

extern void context_switch(uint32_t *save_esp_here, uint32_t new_esp);

#define STACK_SIZE 4096
static uint8_t fiber_stack[STACK_SIZE];
static uint32_t main_esp;
static uint32_t fiber_esp;
static int step;

static void fiber_entry(void) {
    step = 1;
    context_switch(&fiber_esp, main_esp);
    step = 3;
    context_switch(&fiber_esp, main_esp);
    for (;;) { } /* never reached by this test */
}

int main(void) {
    uint32_t top;
    uint32_t *frame;

    top = (uint32_t)(fiber_stack + STACK_SIZE);
    top &= ~0xFu;
    frame = ((uint32_t *)top) - 5; /* ebx, esi, edi, ebp, return-address */
    frame[0] = 0;
    frame[1] = 0;
    frame[2] = 0;
    frame[3] = 0;
    frame[4] = (uint32_t)fiber_entry;
    fiber_esp = (uint32_t)frame;

    step = 0;
    context_switch(&main_esp, fiber_esp);
    if (step != 1) {
        printf("FAIL: expected step==1 after first switch, got %d\n", step);
        return 1;
    }

    step = 2;
    context_switch(&main_esp, fiber_esp);
    if (step != 3) {
        printf("FAIL: expected step==3 after second switch, got %d\n", step);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
```

- [x] **Step 3: Build and run the test**

Run (inside the `forth-os` distrobox container):
```bash
cd kernel/tests
nasm -f elf32 ../context_switch.asm -o context_switch.o
gcc -m32 test_context_switch.c context_switch.o -o test_context_switch
./test_context_switch
```
Expected: `PASS`. If `gcc -m32` fails to link a hosted binary (missing 32-bit libc, distinct from the freestanding kernel build), install the container's 32-bit dev package (e.g. `gcc-multilib`/`libc6-dev-i386` depending on the container's base) and retry -- this is a one-time container setup issue, not a code problem.

- [x] **Step 4: Commit**

```bash
git add kernel/context_switch.asm kernel/tests/test_context_switch.c
git commit -m "kernel: context_switch() fiber primitive, host-tested"
```

---

### Task 2: `scheduler.c`/`scheduler.h` -- program table and round-robin tick

**Files:**
- Create: `kernel/scheduler.h`
- Create: `kernel/scheduler.c`
- Create: `kernel/tests/test_scheduler.c`
- Test: run natively inside the `forth-os` container

**Interfaces:**
- Consumes: `context_switch()` (Task 1).
- Produces (used by Tasks 3-7):
  ```c
  #define MAX_PROGRAMS 4
  #define SCHEDULER_CLOSE_GRACE_FRAMES 60

  int scheduler_reserve(const char *name);
  void scheduler_activate(int slot, void (*entry)(void *arg), void *arg);
  void scheduler_yield(void);
  void scheduler_tick(void);
  int scheduler_current_slot(void);
  void scheduler_request_close(int slot);
  int scheduler_any_active(void);
  ```

- [x] **Step 1: Write `scheduler.h`**

```c
#ifndef RAVEOS_SCHEDULER_H
#define RAVEOS_SCHEDULER_H

/* Cooperative fiber scheduler: a fixed pool of program slots, each
 * with its own static stack, switched via context_switch.asm's
 * context_switch(). No malloc, no paging, no ring 3, no preemption --
 * see docs/superpowers/specs/2026-08-19-concurrency-design.md for the
 * full design and what's deliberately out of scope. Self-contained,
 * same isolation discipline forth.c already holds itself to: no
 * graphics.h/window.h/etc. here, so this stays reusable by any future
 * non-Forth program and host-testable without a kernel build. */

#define MAX_PROGRAMS 4

/* If a program hasn't finished on its own within this many more
 * scheduler_tick() calls after scheduler_request_close(), it's
 * force-freed -- see scheduler_request_close()'s own comment. */
#define SCHEDULER_CLOSE_GRACE_FRAMES 60

/* Reserves a free slot without starting it -- lets a caller build up
 * an entry function's argument (typically into its own slot-indexed
 * array) using the returned index before the slot can possibly be
 * switched into, closing the gap between "which slot did I get" and
 * "what should it run". Returns the slot index, or -1 if all
 * MAX_PROGRAMS slots are in use. */
int scheduler_reserve(const char *name);

/* Primes slot's stack so it starts at entry(arg) the next time
 * scheduler_tick() switches into it, and marks it runnable. slot must
 * be a value scheduler_reserve() just returned, not yet activated. */
void scheduler_activate(int slot, void (*entry)(void *arg), void *arg);

/* Callable only from inside a running program (scheduler_current_slot()
 * >= 0) -- switches back to whichever context called into this
 * program, resuming here on this program's next scheduler_tick()
 * slice. */
void scheduler_yield(void);

/* Called once per kmain() frame. Gives every runnable slot one slice:
 * switches in, runs until that program calls scheduler_yield() or its
 * entry function returns, then moves to the next slot. Also advances
 * close-request timeouts (see scheduler_request_close()). */
void scheduler_tick(void);

/* -1 outside any program; otherwise the slot index currently
 * mid-switch. Lets a hook (e.g. forth_hook_paint_open()) learn which
 * program it's running inside of without any string-matching on how
 * that program was launched. */
int scheduler_current_slot(void);

/* Starts the close-then-timeout sequence for slot: if the program is
 * still running SCHEDULER_CLOSE_GRACE_FRAMES scheduler_tick() calls
 * from now, it's force-freed outright (its stack abandoned -- safe
 * here specifically because nothing in this kernel holds an open
 * resource across calls). A slot that isn't currently running is
 * unaffected. Idempotent: calling this again on a slot whose timeout
 * is already counting down does not restart the count. */
void scheduler_request_close(int slot);

/* 1 if any program slot is reserved or running, 0 if the whole table
 * is empty. This kernel has no timer interrupt (isr.c/pic.c only wire
 * keyboard/mouse IRQs) -- kmain()'s idle path halts the CPU until the
 * next hardware interrupt when nothing changed on screen, which would
 * starve a running program of scheduler_tick() calls until the user
 * next touches the mouse or keyboard. kmain() checks this before
 * halting. */
int scheduler_any_active(void);

#endif
```

- [x] **Step 2: Write `scheduler.c`**

```c
#include "scheduler.h"
#include <stdint.h>

#define PROGRAM_STACK_SIZE 8192
#define STACK_CANARY 0xC0FFEEu

enum program_state { PROGRAM_EMPTY, PROGRAM_RESERVED, PROGRAM_READY };

struct program {
    uint32_t saved_esp;
    enum program_state state;
    const char *name;
    void (*entry)(void *arg);
    void *arg;
    int close_requested_frame; /* -1 = no close requested */
};

static uint8_t program_stacks[MAX_PROGRAMS][PROGRAM_STACK_SIZE];
static struct program programs[MAX_PROGRAMS];
static uint32_t kernel_esp;
static int running_slot = -1;

extern void context_switch(uint32_t *save_esp_here, uint32_t new_esp);

static uint32_t *stack_canary_ptr(int slot) {
    return (uint32_t *)&program_stacks[slot][0];
}

/* "Returned into" by the first context_switch() into a freshly
 * activated slot (see scheduler_activate()'s priming below) -- runs
 * the program's real entry point, then marks the slot done and
 * switches back to the kernel side for good. running_slot is already
 * correct by the time this runs: scheduler_tick() sets it immediately
 * before the context_switch() call that lands here. */
static void program_trampoline(void) {
    struct program *self = &programs[running_slot];
    self->entry(self->arg);
    self->state = PROGRAM_EMPTY;
    context_switch(&self->saved_esp, kernel_esp); /* never returns */
}

int scheduler_reserve(const char *name) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state == PROGRAM_EMPTY) {
            programs[i].state = PROGRAM_RESERVED;
            programs[i].name = name;
            programs[i].close_requested_frame = -1;
            *stack_canary_ptr(i) = STACK_CANARY;
            return i;
        }
    }
    return -1;
}

void scheduler_activate(int slot, void (*entry)(void *arg), void *arg) {
    uint32_t top;
    uint32_t *frame;

    programs[slot].entry = entry;
    programs[slot].arg = arg;

    /* The first context_switch() into this slot must "return" into
     * program_trampoline() rather than resuming a real prior call.
     * context_switch()'s epilogue pops 4 registers then ret's, so the
     * primed stack (low to high address) must be: 4 dummy
     * callee-saved register values, then the return address --
     * exactly what a real context_switch() call would have left
     * behind, just hand-built instead of pushed by real code. */
    top = (uint32_t)&program_stacks[slot][PROGRAM_STACK_SIZE];
    top &= ~0xFu;
    frame = ((uint32_t *)top) - 5;
    frame[0] = 0; /* ebx */
    frame[1] = 0; /* esi */
    frame[2] = 0; /* edi */
    frame[3] = 0; /* ebp */
    frame[4] = (uint32_t)program_trampoline;
    programs[slot].saved_esp = (uint32_t)frame;
    programs[slot].state = PROGRAM_READY;
}

void scheduler_yield(void) {
    struct program *self = &programs[running_slot];
    context_switch(&self->saved_esp, kernel_esp);
}

void scheduler_tick(void) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state != PROGRAM_READY) {
            continue;
        }

        if (programs[i].close_requested_frame >= 0) {
            programs[i].close_requested_frame++;
            if (programs[i].close_requested_frame > SCHEDULER_CLOSE_GRACE_FRAMES) {
                programs[i].state = PROGRAM_EMPTY;
                continue;
            }
        }

        if (*stack_canary_ptr(i) != STACK_CANARY) {
            /* Stack overflow smashed into the canary -- can't safely
             * resume this slot. No paging means no way to prevent the
             * corruption itself, only to stop compounding it. */
            programs[i].state = PROGRAM_EMPTY;
            continue;
        }

        running_slot = i;
        context_switch(&kernel_esp, programs[i].saved_esp);
        running_slot = -1;
    }
}

int scheduler_current_slot(void) {
    return running_slot;
}

void scheduler_request_close(int slot) {
    if (programs[slot].state == PROGRAM_READY && programs[slot].close_requested_frame < 0) {
        programs[slot].close_requested_frame = 0;
    }
}

int scheduler_any_active(void) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state != PROGRAM_EMPTY) {
            return 1;
        }
    }
    return 0;
}
```

- [x] **Step 3: Write the host-side scheduler test**

```c
/* kernel/tests/test_scheduler.c -- host-side only. scheduler.c has no
 * freestanding-only dependencies, so it compiles and runs natively
 * here exactly as it will inside kernel.bin. */
#include <stdio.h>
#include "../scheduler.h"

static int counter_a;
static int counter_b;

static void fiber_a(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 3; i++) {
        counter_a++;
        scheduler_yield();
    }
}

static void fiber_b(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 3; i++) {
        counter_b++;
        scheduler_yield();
    }
}

int main(void) {
    int slot_a, slot_b, i;

    slot_a = scheduler_reserve("A");
    scheduler_activate(slot_a, fiber_a, 0);
    slot_b = scheduler_reserve("B");
    scheduler_activate(slot_b, fiber_b, 0);

    for (i = 0; i < 4; i++) {
        scheduler_tick();
    }

    if (counter_a != 3 || counter_b != 3) {
        printf("FAIL: counter_a=%d counter_b=%d (expected 3, 3)\n", counter_a, counter_b);
        return 1;
    }
    if (scheduler_current_slot() != -1) {
        printf("FAIL: scheduler_current_slot() should be -1 between ticks, got %d\n",
               scheduler_current_slot());
        return 1;
    }
    if (scheduler_any_active()) {
        printf("FAIL: scheduler_any_active() should be 0 once both fibers finished\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
```

- [x] **Step 4: Build and run the test**

Run (inside the `forth-os` container):
```bash
cd kernel/tests
gcc -m32 -Wall -Wextra -c ../scheduler.c -o scheduler.o
gcc -m32 test_scheduler.c scheduler.o context_switch.o -o test_scheduler
./test_scheduler
```
Expected: `PASS`.

- [x] **Step 5: Commit**

```bash
git add kernel/scheduler.h kernel/scheduler.c kernel/tests/test_scheduler.c
git commit -m "kernel: cooperative program scheduler, host-tested"
```

---

### Task 3: Build integration and `kmain()` wiring

**Files:**
- Modify: `kernel/Makefile`
- Modify: `kernel/kernel.c` (top-of-file include; end of the `for (;;)` loop, kernel.c:2787-2790 region)
- Test: headless QEMU boot screendump (regression only -- nothing is spawned yet)

**Interfaces:**
- Consumes: `scheduler_tick()`, `scheduler_any_active()` (Task 2).

- [x] **Step 1: Add build rules to `kernel/Makefile`**

Add `context_switch.o scheduler.o` to `C_OBJS`... actually `context_switch.o` is assembled, not compiled -- keep it separate like `kernel_entry.o`:

```makefile
C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o desktop_icon.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o fs.o editor.o serial.o scheduler.o
```

Add, alongside the `kernel_entry.o` rule:
```makefile
context_switch.o: context_switch.asm
	$(AS) -f elf32 context_switch.asm -o context_switch.o
```

Add, alongside the other `.o` rules:
```makefile
scheduler.o: scheduler.c scheduler.h
	$(CC) $(CFLAGS) -c scheduler.c -o scheduler.o
```

Update `kernel.o`'s dependency line to add `scheduler.h`:
```makefile
kernel.o: kernel.c graphics.h text.h mouse.h keyboard.h interrupts.h window.h button.h taskbar.h startmenu.h console_input.h console_history.h console_output.h forth.h shell.h io.h ata.h fs.h editor.h forth_hooks.h serial.h scheduler.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
```

Update the link rule to include `context_switch.o`:
```makefile
kernel.elf: kernel_entry.o context_switch.o $(C_OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib -o kernel.elf kernel_entry.o context_switch.o $(C_OBJS)
```

Update `clean`:
```makefile
clean:
	rm -f kernel_entry.o context_switch.o $(C_OBJS) kernel.elf kernel.bin
```

- [x] **Step 2: `#include "scheduler.h"` in `kernel.c`**

Add after the existing `#include "forth_hooks.h"` (kernel.c:19):
```c
#include "forth_hooks.h"
#include "scheduler.h"
```

- [x] **Step 3: Call `scheduler_tick()` every frame and fix the idle path**

The loop currently ends (kernel.c:2781-2790):
```c
            update_and_present(w, h, windows, fx_enabled, &co, &ci, &shell_co, &shell_ci, z_order, old_z, old_mx,
                               old_my, mx, my, cursor_color, old_x, old_y, touched, fx_enabled != old_fx_enabled,
                               &bar, taskbar_hovered, old_taskbar_hovered, &menu, menu_hovered_item, menu_touched,
                               cwd, file_entries, file_entry_count, files_selected_mask, &name_input, &new_dir_btn,
                               &delete_btn, &cut_btn, &copy_btn, &paste_btn, &ed, &save_btn, &paint,
                               &paint_name_input, &paint_save_btn);
        } else {
            __asm__ volatile("hlt");
        }
    }
}
```

Find the `if (had_event) { ... update_and_present(...); } else { __asm__ volatile("hlt"); }` block this closes (search for `if (had_event`). `scheduler_tick()` must run every iteration regardless of `had_event` -- it's what makes any running program (once Task 5 lands) make progress -- and the idle branch must not `hlt` while a program is still active, since this kernel has no timer interrupt to wake it back up on a schedule. Change the branch condition itself, and call `scheduler_tick()` immediately before it:

```c
        scheduler_tick();

        if (had_event || scheduler_any_active()) {
            update_and_present(w, h, windows, fx_enabled, &co, &ci, &shell_co, &shell_ci, z_order, old_z, old_mx,
                               old_my, mx, my, cursor_color, old_x, old_y, touched, fx_enabled != old_fx_enabled,
                               &bar, taskbar_hovered, old_taskbar_hovered, &menu, menu_hovered_item, menu_touched,
                               cwd, file_entries, file_entry_count, files_selected_mask, &name_input, &new_dir_btn,
                               &delete_btn, &cut_btn, &copy_btn, &paste_btn, &ed, &save_btn, &paint,
                               &paint_name_input, &paint_save_btn);
        } else {
            __asm__ volatile("hlt");
        }
    }
}
```

(The `if (had_event` this replaces is the one immediately preceding this `update_and_present()` call -- find and change just that `if`'s condition; do not touch any other `had_event` check earlier in the loop, e.g. the ones setting `had_event = 1` on each input source.)

- [x] **Step 4: Build and boot-test (regression only)**

Nothing calls `scheduler_reserve()`/`scheduler_activate()` yet, so `scheduler_any_active()` is always 0 and behavior must be pixel-identical to before this task.

Run (inside the `forth-os` container):
```bash
cd kernel && make clean && make
cd ../boot && make clean && make
```
Then boot headlessly (matching this project's established pattern) and screendump the desktop; confirm it matches a screendump taken before this task (same taskbar, same idle desktop, no visual regression).

- [x] **Step 5: Commit**

```bash
git add kernel/Makefile kernel/kernel.c
git commit -m "kernel: wire scheduler_tick() into kmain(), fix idle-hlt starvation"
```

---

### Task 4: `OP_CALL_YIELD` auto-yield, `WINDOW-CLOSED?`

**Files:**
- Modify: `kernel/forth.h`
- Modify: `kernel/forth.c`
- Modify: `kernel/forth_hooks.h`
- Modify: `kernel/kernel.c`
- Test: headless QEMU -- `RUN` still works synchronously as before (Task 5 makes it async); confirm no `UNKNOWN`/compile errors from the new opcode or primitive.

**Interfaces:**
- Consumes: `scheduler_current_slot()`, `scheduler_yield()` (Task 2).
- Produces: `forth_hook_yield()`, `forth_hook_window_closed()` (used starting Task 6's script update; `forth_hook_window_closed()` also used by Task 7).

- [x] **Step 1: Add `OP_CALL_YIELD` to `forth.h`**

```c
enum forth_op { OP_LITERAL, OP_CALL_PRIMITIVE, OP_CALL_WORD, OP_EXIT, OP_BRANCH, OP_BRANCH_IF_ZERO, OP_CALL_YIELD };
```

- [x] **Step 2: Emit it at every compiled `UNTIL`**

In `handle_compile_token()` (forth.c:526-533):
```c
    if (str_eq_ci(token, "UNTIL")) {
        struct forth_ctrl_entry e;
        if (!ctrl_pop(vm, CTRL_KIND_BEGIN, &e, "MISMATCHED UNTIL")) {
            return;
        }
        forth_emit(vm, OP_CALL_YIELD, 0);
        forth_emit(vm, OP_BRANCH_IF_ZERO, e.value); /* loop back if false; falls through if true */
        return;
    }
```

- [x] **Step 3: Handle it in `forth_exec()`'s switch**

In `forth_exec()` (forth.c:389-423), add a case alongside the existing ones:
```c
        case OP_CALL_YIELD:
            forth_hook_yield();
            ip++;
            break;
```

- [x] **Step 4: Add `WINDOW-CLOSED?`**

Add a primitive next to `prim_mouse_right_down()` (forth.c:317-319):
```c
static void prim_window_closed(struct forth_vm *vm) {
    forth_push(vm, forth_hook_window_closed() ? -1 : 0);
}
```

Add it to `primitives[]` (forth.c:348-357), next to `MOUSE-RIGHT-DOWN?`:
```c
    {"MOUSE-DOWN?", prim_mouse_down}, {"MOUSE-RIGHT-DOWN?", prim_mouse_right_down},
    {"WINDOW-CLOSED?", prim_window_closed},
```

- [x] **Step 5: Declare the two new hooks in `forth_hooks.h`**

```c
/* OP_CALL_YIELD's target -- called at every compiled BEGIN...UNTIL
 * loop back-edge (forth.c's handle_compile_token()), automatically,
 * with no script-source opt-in. A no-op when the calling code isn't
 * running inside a scheduled program (scheduler_current_slot() < 0 --
 * e.g. a word typed and invoked directly at the interactive console,
 * never spawned through RUN): a single typed line is never
 * long-running, so there's nothing to yield around there. See
 * docs/superpowers/specs/2026-08-19-concurrency-design.md. */
void forth_hook_yield(void);

/* 1 if the PAINT window has been closed (state != WINDOW_OPEN), 0
 * otherwise -- lets a script's own loop condition notice its window
 * closed instead of only ever checking its own domain-specific exit
 * condition (PLOOP's is MOUSE-RIGHT-DOWN?). */
int forth_hook_window_closed(void);
```

- [x] **Step 6: Implement both hooks in `kernel.c`**

Add near the other `forth_hook_*` implementations (after `forth_hook_current_color()`, kernel.c:629-631):
```c
void forth_hook_yield(void) {
    if (scheduler_current_slot() >= 0) {
        scheduler_yield();
    }
}

int forth_hook_window_closed(void) {
    return windows[WIN_KIND_PAINT].state != WINDOW_OPEN;
}
```

- [x] **Step 7: Build and smoke-test**

Run (inside the `forth-os` container):
```bash
cd kernel && make clean && make
cd ../boot && make clean && make
```
Boot headlessly; type `RUN PAINT` at the Forth console; confirm it still opens and paints exactly as before this task (RUN is still synchronous until Task 5, so `WINDOW-CLOSED?`/auto-yield exist but don't change observable behavior yet -- this step only confirms the new opcode/primitive compile and don't break existing behavior).

- [x] **Step 8: Commit**

```bash
git add kernel/forth.h kernel/forth.c kernel/forth_hooks.h kernel/kernel.c
git commit -m "forth: auto-yield at every BEGIN...UNTIL back-edge, add WINDOW-CLOSED?"
```

---

### Task 5: `RUN` becomes a scheduled program (the actual concurrency fix)

**Files:**
- Modify: `kernel/kernel.c` (replace `forth_run_command()`, kernel.c:983-1030; replace its call site, kernel.c:2553-2555)
- Test: headless QEMU -- the real regression test for the bug this whole design fixes

**Interfaces:**
- Consumes: `scheduler_reserve()`, `scheduler_activate()` (Task 2).

- [x] **Step 1: Replace `forth_run_command()` with a scheduled entry point**

Delete `forth_run_command()` (kernel.c:983-1030) entirely and replace it with:

```c
struct run_program_ctx {
    struct forth_vm vm;
    struct console_output *co;
    char path[FILES_PATH_MAX];
};
static struct run_program_ctx run_ctxs[MAX_PROGRAMS];

/* Entry point for a RUN-launched program, executed inside its own
 * scheduler slot (scheduler.h) instead of blocking kmain() -- replaces
 * the old synchronous forth_run_command(), which read and evaluated a
 * whole script in a single call. path/vm/co are filled in by the RUN
 * call site (below) before scheduler_activate() ever runs this, since
 * ci.text (the source of the raw argument) is cleared as soon as that
 * call site returns -- path resolution can't be deferred to here. The
 * read-and-line-feed logic itself is otherwise identical to before;
 * the only behavioral difference is that a BEGIN...UNTIL loop inside
 * the script now yields (forth.c's OP_CALL_YIELD) back to
 * scheduler_tick(), and through it to kmain()'s own per-frame work,
 * between iterations instead of blocking here until the whole script
 * finishes. */
static void run_program_entry(void *arg) {
    struct run_program_ctx *ctx = (struct run_program_ctx *)arg;
    char buf[VIEWER_BUF_SIZE];
    unsigned int out_size;
    int oi, line_start;

    if (fs_read_file(ctx->path, buf, VIEWER_BUF_SIZE - 1, &out_size) != 0) {
        console_output_append_line(ctx->co, "(RUN FAILED)");
        return;
    }
    buf[out_size] = 0;

    line_start = 0;
    for (oi = 0; oi <= (int)out_size; oi++) {
        if (oi == (int)out_size || buf[oi] == '\n') {
            char eval_out[128];
            char saved = buf[oi];
            buf[oi] = 0;
            forth_eval_line(&ctx->vm, &buf[line_start], eval_out, sizeof(eval_out));
            append_split_lines(ctx->co, eval_out);
            buf[oi] = saved;
            line_start = oi + 1;
        }
    }
}
```

- [x] **Step 2: Replace the `RUN` call site**

At kernel.c:2553-2555, currently:
```c
                    run_arg = vm.compiling ? 0 : match_run_command(ci.text);
                    if (run_arg) {
                        forth_run_command(&vm, &co, run_arg);
                    } else {
```

Replace with:
```c
                    run_arg = vm.compiling ? 0 : match_run_command(ci.text);
                    if (run_arg) {
                        int run_slot = scheduler_reserve("RUN");
                        if (run_slot < 0) {
                            console_output_append_line(&co, "(TOO MANY PROGRAMS RUNNING)");
                        } else {
                            struct run_program_ctx *ctx = &run_ctxs[run_slot];
                            int rpos = 0;

                            forth_init(&ctx->vm);
                            ctx->co = &co;
                            /* Same case-fold as before (see the old
                             * forth_run_command()'s comment, now moved
                             * here): every real path in this filesystem
                             * is uppercase by convention, and fs.c's
                             * lookups are byte-exact. */
                            if (run_arg[0] == '/') {
                                str_append(ctx->path, &rpos, (int)sizeof(ctx->path), run_arg);
                            } else {
                                str_append(ctx->path, &rpos, (int)sizeof(ctx->path), "/BIN/");
                                str_append(ctx->path, &rpos, (int)sizeof(ctx->path), run_arg);
                            }
                            for (rpos = 0; ctx->path[rpos]; rpos++) {
                                if (ctx->path[rpos] >= 'a' && ctx->path[rpos] <= 'z') {
                                    ctx->path[rpos] = (char)(ctx->path[rpos] - 32);
                                }
                            }
                            scheduler_activate(run_slot, run_program_entry, ctx);
                        }
                    } else {
```

- [x] **Step 3: Build**

```bash
cd kernel && make clean && make
cd ../boot && make clean && make
```
Fix any compile errors (e.g. if `forth_run_command`'s old prototype/declaration lingers anywhere -- it shouldn't, it was file-local `static`).

- [x] **Step 4: Headless QEMU regression test -- the actual fix**

This is the direct test for the bug motivating this whole design. Boot headlessly, open the Forth console, type `RUN PAINT` -- confirm the console's own prompt returns immediately (not after the whole script "finishes", since PLOOP never finishes on its own). While PLOOP is looping:
- Drive the mouse over a *different* window (e.g. open FILES via the start menu) through the QEMU monitor and confirm via screendump that FILES opens and redraws -- today (before this task) that's impossible, since nothing but PLOOP's own hand-copied hooks ever ran during the loop.
- Confirm PAINT itself still works: paint a pixel, pick a palette color, click SAVE -- exercising the exact three code paths the 2026-08-18 pass hand-patched, now happening via the general mechanism (kmain()'s own per-frame click handling, running because it never actually stopped) rather than the `PALETTE-PICK`/`SAVE-PICK`/`REFRESH` primitives (still present at this point in the plan -- Task 6 removes them, and this same scenario is the regression test for that removal too).

- [x] **Step 5: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: RUN spawns a scheduled program instead of blocking kmain()"
```

---

### Task 6: Delete the five workaround hooks/primitives, update `PLOOP`'s script

**Files:**
- Modify: `kernel/forth.c` (delete `prim_refresh`/`prim_palette_pick`/`prim_save_pick` and their table entries)
- Modify: `kernel/forth_hooks.h` (delete the three hook declarations)
- Modify: `kernel/kernel.c` (delete the three hook implementations, `forth_hook_paint_open()`'s direct call, update `bin_paint_default[]`)
- Test: headless QEMU -- re-run Task 5's exact scenario; must still pass with these deleted

**Interfaces:**
- None produced -- pure deletion plus one script-text change.

- [x] **Step 1: Delete the three primitive wrappers and table entries in `forth.c`**

Delete `prim_refresh()`, `prim_palette_pick()`, `prim_save_pick()` (forth.c:325-338):
```c
static void prim_refresh(struct forth_vm *vm) {
    (void)vm;
    forth_hook_refresh();
}

static void prim_palette_pick(struct forth_vm *vm) {
    (void)vm;
    forth_hook_palette_pick();
}

static void prim_save_pick(struct forth_vm *vm) {
    (void)vm;
    forth_hook_save_pick();
}
```

In `primitives[]`, Task 4 Step 4 already added `{"WINDOW-CLOSED?", prim_window_closed},` right after the `MOUSE-RIGHT-DOWN?` entry, so the table currently reads:
```c
    {"MOUSE-DOWN?", prim_mouse_down}, {"MOUSE-RIGHT-DOWN?", prim_mouse_right_down},
    {"WINDOW-CLOSED?", prim_window_closed},
    {"CURRENT-COLOR", prim_current_color}, {"REFRESH", prim_refresh},
    {"PALETTE-PICK", prim_palette_pick},
    {"SAVE-PICK", prim_save_pick},
};
```
Delete just the `REFRESH`/`PALETTE-PICK`/`SAVE-PICK` lines (leave `WINDOW-CLOSED?` where it already is):
```c
    {"MOUSE-DOWN?", prim_mouse_down}, {"MOUSE-RIGHT-DOWN?", prim_mouse_right_down},
    {"WINDOW-CLOSED?", prim_window_closed},
    {"CURRENT-COLOR", prim_current_color},
};
```

- [x] **Step 2: Delete the three hook declarations from `forth_hooks.h`**

Delete:
```c
void forth_hook_refresh(void);
```
and (from further down):
```c
/* If the left button is currently held over a palette swatch, selects
 * it (see forth_hook_current_color()'s comment above) -- a no-op
 * otherwise. Needed so a script's own loop can change color without
 * ever falling back to kmain()'s per-frame click handling, which
 * doesn't run at all while that loop blocks. */
void forth_hook_palette_pick(void);

/* If the left button is currently held over the SAVE button, writes
 * the grid to /HOME/<the filename field's own text> -- a no-op
 * otherwise, or if that field is empty. Needed for the same reason as
 * forth_hook_palette_pick(): kmain()'s own per-frame click handling
 * doesn't run at all while a script's loop blocks. */
void forth_hook_save_pick(void);
```

- [x] **Step 3: Delete the three hook implementations in `kernel.c`**

Delete `forth_hook_refresh()` in full (kernel.c:633-679, the whole function including its doc comment).

Delete `forth_hook_palette_pick()` in full (kernel.c:702-731, including its doc comment).

Delete `forth_hook_save_pick()` in full (kernel.c:760-789, including its doc comment). Note: `paint_build_save_path()` (kernel.c:733-758) stays -- it's still used by `kmain()`'s own SAVE-button click handler, which was always the normal path; only the hook that duplicated it for the blocked-loop case is deleted.

In `forth_hook_paint_open()` (kernel.c:533-562), delete the trailing call and its comment:
```c
    /* Without this, the window stays completely undrawn -- raise_window()
     * only reorders z_order, it doesn't paint anything, and kmain()'s own
     * per-frame draw loop isn't running at all while /BIN/PAINT's PLOOP
     * (BEGIN...UNTIL) blocks inside this same forth_eval_line() call
     * chain (see forth_hook_refresh()'s own comment). Root-caused via a
     * headless repro (screendump right after RUN PAINT, before any
     * click): the entire screen -- not just this window -- sits frozen
     * exactly as it was the instant Enter was pressed, console input box
     * included, until the first successful left-click-on-canvas pixel
     * paint finally calls forth_hook_refresh(). On real hardware this
     * read as "PAINT takes ~10 seconds to launch and only works
     * sometimes": the window is invisible so the user is clicking blind,
     * and only succeeds once a guess happens to land inside the canvas's
     * actual (unseen) bounds. */
    forth_hook_refresh();
```
so the function ends at `raise_window(z_order, WIN_KIND_PAINT);` (a just-opened PAINT window is drawn on its next frame by `kmain()`'s normal `update_and_present()`, same as every other window when opened -- no special-cased call needed once `kmain()`'s own loop is guaranteed to run again on the next tick).

- [x] **Step 4: Update `bin_paint_default[]`**

In `seed_bin_paint_script()` (kernel.c:1138-1156), change:
```c
    static const char bin_paint_default[] =
        "PAINT\n"
        ": PLOOP\n"
        "  BEGIN\n"
        "    PALETTE-PICK\n"
        "    SAVE-PICK\n"
        "    MOUSE-DOWN? IF\n"
        "      MOUSE-X MOUSE-Y\n"
        "      OVER OVER SWAP -1 > SWAP -1 > *\n"
        "      IF CURRENT-COLOR PIXEL ELSE DROP DROP THEN\n"
        "    THEN\n"
        "    REFRESH\n"
        "    MOUSE-RIGHT-DOWN?\n"
        "  UNTIL\n"
        ";\n"
        "PLOOP\n";
```
to:
```c
    static const char bin_paint_default[] =
        "PAINT\n"
        ": PLOOP\n"
        "  BEGIN\n"
        "    MOUSE-DOWN? IF\n"
        "      MOUSE-X MOUSE-Y\n"
        "      OVER OVER SWAP -1 > SWAP -1 > *\n"
        "      IF CURRENT-COLOR PIXEL ELSE DROP DROP THEN\n"
        "    THEN\n"
        "    MOUSE-RIGHT-DOWN? WINDOW-CLOSED? +\n"
        "  UNTIL\n"
        ";\n"
        "PLOOP\n";
```
(`PALETTE-PICK`/`SAVE-PICK`/`REFRESH` are gone -- `kmain()`'s own per-frame click handling and damage-tracked redraw now run every frame regardless, since the loop yields. `+` combines the two exit conditions as a logical OR over this dialect's `{0, -1}` boolean convention, the same trick the script's own `OVER OVER SWAP -1 > SWAP -1 > *` already uses `*` for as AND.)

Also update the function's own doc comment (kernel.c:1063-1137) to remove the now-stale `PALETTE-PICK`/`SAVE-PICK`/forced-`REFRESH` explanation paragraphs, since they describe primitives that no longer exist.

- [x] **Step 5: Build**

```bash
cd kernel && make clean && make
cd ../boot && make clean && make
```
This should surface a compile error if any deleted symbol is still referenced somewhere missed above -- fix any such reference before proceeding.

Note: because `/BIN/PAINT` is seeded idempotently (`fs_create_file()` only succeeds the first time the path exists, per `seed_bin_paint_script()`'s own comment), a `fs.img` from an earlier test run still has the *old* script text on disk. Use a fresh `fs.img` (or delete the existing one and let it reseed) for this test, or the new `WINDOW-CLOSED?` word won't actually be exercised.

- [x] **Step 6: Headless QEMU regression test**

Re-run Task 5 Step 4's exact scenario against this build: `RUN PAINT`, confirm another window (FILES) redraws and responds while PLOOP loops, confirm paint/palette-pick/SAVE all still work -- now via the general per-frame path alone, with no `PALETTE-PICK`/`SAVE-PICK`/`REFRESH` primitives present at all. Additionally: click PAINT's window close button (X) while PLOOP is looping and confirm the window actually closes within roughly one frame (previously impossible; also the first real exercise of `WINDOW-CLOSED?`).

- [x] **Step 7: Commit**

```bash
git add kernel/forth.c kernel/forth_hooks.h kernel/kernel.c
git commit -m "kernel: delete PALETTE-PICK/SAVE-PICK/REFRESH workaround hooks, now redundant"
```

---

### Task 7: Kill-timeout wiring

**Files:**
- Modify: `kernel/kernel.c` (`forth_hook_paint_open()`; the window-close handler, kernel.c:2245-2246)
- Test: headless QEMU (graceful path, already covered by Task 6's close-button test) plus one manual forced-timeout verification using the in-OS `EDIT` command

**Interfaces:**
- Consumes: `scheduler_current_slot()` (Task 2, also used since Task 4), `scheduler_request_close()` (Task 2).

- [x] **Step 1: Track which scheduler slot opened PAINT**

Add a file-scope static near `paint`/`paint_save_btn`/`paint_name_input` (kernel.c:356-358):
```c
static struct paint paint;
static struct button paint_save_btn;
static struct console_input paint_name_input;
static int paint_program_slot = -1;
```

In `forth_hook_paint_open()` (kernel.c:533-, after Task 6's deletion its body now ends with `raise_window(z_order, WIN_KIND_PAINT);`), add:
```c
    raise_window(z_order, WIN_KIND_PAINT);
    paint_program_slot = scheduler_current_slot();
```
This runs from inside whichever program's own context called the `PAINT` Forth word, so `scheduler_current_slot()` correctly identifies that program's slot with no string-matching on what the script was named.

- [x] **Step 2: Request close when PAINT's window is closed**

At kernel.c:2245-2246, currently:
```c
                    if (window_close_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_CLOSED;
                    } else if (window_minimize_hit_test(&windows[target], cx, cy)) {
```
Change to:
```c
                    if (window_close_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_CLOSED;
                        if (target == WIN_KIND_PAINT && paint_program_slot >= 0) {
                            scheduler_request_close(paint_program_slot);
                        }
                    } else if (window_minimize_hit_test(&windows[target], cx, cy)) {
```
This is the forced backstop only -- the graceful path (Task 6's `WINDOW-CLOSED? +` in `PLOOP`'s `UNTIL`) is what normally makes the script exit on its very next iteration, well within the 60-frame grace period. `scheduler_request_close()` only matters when a script's own loop condition doesn't check `WINDOW-CLOSED?` at all (a future/edited script bug), which is exactly the case Step 3 below deliberately constructs to verify.

- [x] **Step 3: Build and test the graceful path (automated)**

```bash
cd kernel && make clean && make
cd ../boot && make clean && make
```
Headless QEMU: `RUN PAINT`, click the close button without right-clicking first -- confirm (screendump) the window disappears within about one frame, same as Task 6 Step 6 already established (this step just confirms `paint_program_slot`/`scheduler_request_close()` compile in and don't break that path).

- [x] **Step 4: Manually verify the forced-timeout path**

This exercises the case where a script's own loop ignores the close signal -- not reachable through the shipped `PLOOP` script anymore (Task 6 made it check `WINDOW-CLOSED?`), so verify it using this OS's own `EDIT` command rather than a source change:
1. Boot (headless or real hardware), open a SHELL or FORTH window, run `EDIT /BIN/PAINT`.
2. Change the `UNTIL` line back to just `MOUSE-RIGHT-DOWN?` (drop ` WINDOW-CLOSED? +`), save.
3. `RUN PAINT`, then click its window's close button (do not right-click).
4. Confirm the window closes visually within one frame (the close-button handler always sets `windows[WIN_KIND_PAINT].state = WINDOW_CLOSED` regardless of what the script checks), but the script keeps consuming a scheduler slot for about 60 more frames (~1 second) -- verify by immediately trying `RUN PAINT` again from a second FORTH console: it should still work (a free slot exists, `MAX_PROGRAMS = 4`), so instead verify indirectly by running `RUN PAINT` three more times in quick succession right after the close click (filling slots 2-4) and confirming the *fifth* `RUN` briefly reports `(TOO MANY PROGRAMS RUNNING)` and then succeeds about a second later once the original hung slot's timeout fires and frees it.
5. Re-run `EDIT /BIN/PAINT` and restore the ` WINDOW-CLOSED? +` line before finishing.

Label this step's script edit explicitly as a throwaway verification, not a change to keep.

- [x] **Step 5: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: wire close-then-timeout kill path for PAINT's scheduled program"
```

---

### Task 8: Documentation

**Files:**
- Modify: `docs/BUILD_LOG.md`
- Modify: `docs/IDEAS.md`

**Interfaces:** None -- documentation only.

- [x] **Step 1: Append a `docs/BUILD_LOG.md` entry**

Cover: the root cause (no timer interrupt, no preemption, `kmain()`'s loop fully blocked during any `BEGIN...UNTIL`), the fiber/`context_switch()` mechanism, auto-yield at every compiled `UNTIL`, `RUN` becoming async, the five deleted workaround hooks/primitives, the new `WINDOW-CLOSED?` word and kill-timeout path, and the observable behavior change (RUN-launched scripts no longer share dictionary state with the interactive console). Follow this project's established `## YYYY-MM-DD -- <title>` entry style (see the 2026-08-18 entry for the shape/depth expected).

- [x] **Step 2: Update `docs/IDEAS.md`**

Strike through the `kmain()`'s loop doesn't run at all while a Forth script's own loop blocks` entry (currently un-struck, describing exactly what this plan just fixed) with a `Done, <date>` note pointing at this plan/spec and the `BUILD_LOG.md` entry, following this file's existing convention for closed items (see the `A real path for user-written system programs` and `FILES' plain start-menu launcher shows stale data` entries for the exact strikethrough + "Done, date --" format).

- [x] **Step 3: Commit**

```bash
git add docs/BUILD_LOG.md docs/IDEAS.md
git commit -m "docs: concurrency scheduler build log entry, close out the IDEAS.md item"
```

---

**Real-hardware verification (before considering this plan done):** every prior feature touching input/timing in this project has needed a real-hardware pass beyond headless QEMU (`feedback_qemu_input_testing` -- QEMU's monitor input paths don't validate real hardware behavior, and register save/restore in particular is exactly the kind of thing that can differ between QEMU's CPU emulation and real silicon). Re-run Task 5 Step 4's scenario (`RUN PAINT`, confirm another window responds while `PLOOP` loops, confirm paint/palette/SAVE all still work) on real hardware once Task 7 is committed, per this project's established practice of not calling a feature done on QEMU testing alone.
