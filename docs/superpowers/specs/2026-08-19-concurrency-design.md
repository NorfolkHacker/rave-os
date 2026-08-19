# Cooperative concurrency for Rave-OS: real standalone programs, not kernel hooks

## Purpose

`kmain()`'s own per-frame loop (mouse/keyboard, redraw, window
management) does not run at all while a Forth script's `BEGIN...UNTIL`
loop is executing -- `forth_exec()` (forth.c) runs a word's whole body
to completion inside one C call, blocking everything else for as long
as that loop runs. This was the root cause behind five separate bugs
found and hand-patched during the 2026-08-18 real-hardware PAINT pass
(see `docs/BUILD_LOG.md`'s entry for that date): PLOOP's loop body had
to be given its own private copies of pieces of `kmain()`'s own
per-frame work (`REFRESH`, `PALETTE-PICK`, `SAVE-PICK`) one at a time,
as each gap was discovered. That pattern doesn't scale -- the next
long-running script hits the exact same wall.

Raised while scoping this design: the fix shouldn't be "make Forth's
loop construct special-case less" -- it should be "the kernel is just a
kernel; Forth and PAINT are standalone programs that run concurrently
with it," per [[project_os_vision]]'s self-hosting direction. This
spec designs that: a cooperative scheduler giving programs their own
execution context (real stack + saved registers), with the Forth
interpreter becoming the first real occupant of a program slot rather
than kernel-linked code.

## Scope

**In scope**: a fixed-size program-slot table with per-program stacks;
an assembly `context_switch()` primitive; a `yield()` call usable from
any program; automatic yield insertion at every `BEGIN...UNTIL` loop
back-edge (so scripts don't need to opt in); `RUN` changing from
"block until the script finishes" to "start a scheduled program and
return"; `kmain()` driving one scheduler slice per frame; a
close-then-timeout kill path for programs that ignore a graceful close
request; and deleting `forth_hook_refresh()`/`forth_hook_palette_pick()`/
`forth_hook_save_pick()` and their Forth primitives (`PALETTE-PICK`,
`SAVE-PICK`, PLOOP's forced `REFRESH` call) now that PAINT's window
gets serviced by `kmain()`'s own per-frame code like any other window.

**Out of scope, deliberately** (all explicitly discussed and declined
while scoping this):

- **Real process isolation** (ring 3, paging, a syscall ABI, separate
  address spaces). Everything still runs in ring 0, one flat address
  space, no memory protection -- a bug in one program can still
  corrupt the kernel or another program. That's a "Rave-OS v2" scale
  project, not an incremental step; this design does not build toward
  it.
- **Preemptive scheduling.** No timer-interrupt-driven context switch.
  A program that never yields still freezes everything -- this design
  closes that specific failure mode only for `BEGIN...UNTIL` loops (the
  only loop construct this Forth dialect has), by making every one of
  them yield automatically. A hypothetical future non-Forth program
  with a raw, no-yield infinite loop is not fixed by this design and
  would need to call `yield()` itself.
- **The desktop/window-manager itself becoming a scheduled program.**
  `kmain()`'s loop stays kernel-resident and always runs every frame --
  it's the one thing that must never freeze. Forth (and, through it,
  PAINT) become the first scheduled programs alongside it, not a
  replacement for it.
- **Multiple concurrent Forth interpreters sharing state.** Each
  scheduled program that runs Forth gets its own `struct forth_vm`
  (already stack-sized, not heap-allocated) -- no shared interpreter
  state between programs is introduced by this design.

## Design

### Program slots and stacks

```c
#define MAX_PROGRAMS 4
#define PROGRAM_STACK_SIZE 8192  /* bytes; no heap allocator exists,
                                     so this is a static reservation */

enum program_state { PROGRAM_EMPTY, PROGRAM_READY, PROGRAM_DONE };

struct program {
    uint32_t saved_esp;
    enum program_state state;
    const char *name;          /* for error/kill messages */
    int close_requested;       /* frame count since close was asked, or -1 if not requested */
};

static uint8_t program_stacks[MAX_PROGRAMS][PROGRAM_STACK_SIZE];
static struct program programs[MAX_PROGRAMS];
```

Fixed pool, no dynamic sizing -- matches every other fixed-capacity
table already in this kernel (`windows[]`, `file_entries[]`, etc.).
`MAX_PROGRAMS` of 4 is a starting number, not a derived one: today
there is exactly one long-running program (PLOOP); 4 gives headroom
for a couple of Forth consoles plus PAINT without guessing at a real
ceiling.

Each stack's lowest 4 bytes hold a fixed canary value, written when
the slot is primed. `scheduler_tick()` checks it before switching into
that slot; a corrupted canary means a stack overflow happened and the
slot is force-marked `PROGRAM_DONE` with an error surfaced to whatever
launched it, rather than switching into (and likely crashing on) a
smashed stack. This does not prevent corruption -- there's no guard
page, no paging at all -- it only turns a silent memory-smash into a
caught, named error instead of an undiagnosable hang or crash
elsewhere.

### `context_switch()`: the one asm primitive

```c
void context_switch(uint32_t *save_esp_here, uint32_t new_esp);
```

Pushes the callee-saved registers (`ebx`, `esi`, `edi`, `ebp`) onto
whichever stack is currently active, stores the resulting `esp` into
`*save_esp_here`, loads `new_esp` into `esp`, pops the other side's
saved registers, and `ret`s. That `ret` resumes execution wherever the
target last called `context_switch()` itself -- the same routine
serves both directions (kernel-to-program and program-to-kernel); it's
a symmetric swap, not two separate paths.

Starting a program for the first time (no prior `context_switch()` call
to resume from) means hand-priming its stack: the entry function's
address is placed where the first `ret` will read it, with dummy
values for the four popped registers underneath. This is the standard
fiber/coroutine bootstrap trick and is the one place this mechanism
needs care beyond the steady-state swap.

`yield()` is defined purely in terms of this primitive:

```c
void yield(void) {
    struct program *self = current_program;
    context_switch(&self->saved_esp, kernel_esp);
}
```

### Driving the scheduler from `kmain()`

Once per frame, after `kmain()` finishes its own drawing/event work, it
calls `scheduler_tick()`:

```c
void scheduler_tick(void) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state != PROGRAM_READY) { continue; }
        if (!stack_canary_intact(i)) {
            programs[i].state = PROGRAM_DONE;
            continue;
        }
        current_program = &programs[i];
        context_switch(&kernel_esp, programs[i].saved_esp);
        /* control returns here the instant program i calls yield()
           or its entry function returns */
    }
}
```

Each `READY` program gets exactly one slice per frame, in slot order --
a plain round-robin, no priority. Control always returns to `kmain()`
between slots, so `kmain()`'s own per-frame work (redraw, mouse,
keyboard, window management) runs at full rate regardless of how many
programs are scheduled.

### `RUN` and the Forth interpreter as a scheduled program

`RUN <name>` currently calls `forth_run_command()`, which reads the
script and feeds it line-by-line into `forth_eval_line()` synchronously
-- the whole script runs inside that one call, per forth.c's own
"forth_eval_line() never touches console_output.h... [BEGIN...UNTIL]
blocks inside this same forth_eval_line() call" existing comment.

Under this design, `RUN` instead:

1. Finds a free program slot (`PROGRAM_EMPTY`); if none, reports "TOO
   MANY PROGRAMS RUNNING" the same way other fixed-table-full
   conditions already report in this codebase.
2. Allocates that program's own `struct forth_vm` (stack-resident
   inside the program's entry function, not shared).
3. Primes the slot's stack with an entry function that calls
   `forth_run_command()`'s existing script-reading and line-evaluation
   logic, unmodified, against that program's own VM.
4. Marks the slot `PROGRAM_READY` and returns immediately -- the
   console window that typed `RUN` gets its prompt back right away
   instead of blocking until the whole script finishes.

The Forth interpreter itself is not special kernel code in this model
-- it's the first real occupant of a program slot. PAINT needs no
separate program type: it's the Forth interpreter, running PAINT's
script text, exactly as it already is today. When the script's own
logic finishes (or a `BEGIN...UNTIL` loop's condition becomes true and
the word returns, or a compile/runtime error sets `vm->error`), the
entry function returns and `scheduler_tick()` marks that slot
`PROGRAM_DONE` -- no different from today's error handling, just
happening inside a slot instead of inline in `kmain()`.

Interactive Forth typed directly into a console window (not via `RUN`)
keeps its current synchronous behavior -- a single typed line is never
long-running, so there's nothing to yield around. Only `RUN`-launched
scripts go through a program slot.

**Observable behavior change**: today, `RUN` evaluates the script
against the *same* `struct forth_vm` the console window's own typed
input uses (`kernel.c:2555` passes `&vm`, the console's own VM) -- so
a word a script defines with `:`/`;` remains callable interactively
after the script finishes. Under this design, a `RUN`-launched script
gets its own private VM (per "Out of scope" above), discarded when its
program slot frees. Words a script defines stop being visible to the
interactive console once it finishes -- correct given no shared
interpreter state is a deliberate design choice here, but a real,
visible change from today's behavior, not just an implementation
detail.

### Automatic yield insertion at `BEGIN...UNTIL`

`handle_compile_token()` (forth.c:521-531) already knows the exact
bytecode offset of every loop back-edge -- it's what `UNTIL` patches
`OP_BRANCH_IF_ZERO`'s target to. This design adds one `OP_CALL_YIELD`
emission immediately before that branch, for every `UNTIL` compiled,
with no change required to script source:

```c
if (str_eq_ci(token, "UNTIL")) {
    struct forth_ctrl_entry e;
    if (!ctrl_pop(vm, CTRL_KIND_BEGIN, &e, "MISMATCHED UNTIL")) { return; }
    forth_emit(vm, OP_CALL_YIELD, 0);
    forth_emit(vm, OP_BRANCH_IF_ZERO, e.value);
}
```

`OP_CALL_YIELD`'s handler in `forth_exec()`'s switch just calls
`yield()` -- but only when running inside a scheduled program (the
interactive-console path never enters a program slot, so
`current_program` is null there; `OP_CALL_YIELD` is a no-op in that
case, matching "a single typed line is never long-running" above).

Since `BEGIN...UNTIL` is the only loop construct this Forth dialect
has, this closes the entire class of bug the 2026-08-18 pass fixed by
hand: any future script with a long-running loop yields automatically,
without its author needing to know a scheduler exists.

### Deleting the special-case hooks

Once PLOOP yields back to `kmain()` every iteration, `kmain()`'s real
per-frame code -- window redraw, palette hit-test, SAVE hit-test,
mouse handling -- runs normally for PAINT's window exactly as it does
for every other window (which never call an explicit redraw primitive
themselves; `kmain()`'s own damage-tracked `draw_scene()` already
redraws whatever changed, every frame, for all of them). That makes
`REFRESH` redundant too, not just `PALETTE-PICK`/`SAVE-PICK` -- all
three exist solely to paper over `kmain()`'s loop not running, and
this design fixes that at the root.

Deleted as part of this work: `forth_hook_refresh()`,
`forth_hook_palette_pick()`, `forth_hook_save_pick()` (kernel.c) and
their declarations in `forth_hooks.h`; `prim_refresh()`,
`prim_palette_pick()`, `prim_save_pick()` and the `REFRESH`/
`PALETTE-PICK`/`SAVE-PICK` primitive-table entries (forth.c);
`forth_hook_paint_open()`'s own direct call to `forth_hook_refresh()`
(kernel.c:561) -- a just-opened PAINT window gets drawn on its next
frame by `kmain()`'s normal draw pipeline, the same as every other
window already is when opened, needing no special-cased call.
`seed_bin_paint_script()`'s `bin_paint_default[]` script text drops the
`PALETTE-PICK`/`SAVE-PICK`/forced-`REFRESH` lines it currently carries
from the 2026-08-18 pass.

### Killing a hung program

Checked against the actual script, not assumed: PLOOP's `UNTIL`
condition today only checks `MOUSE-RIGHT-DOWN?`
(`seed_bin_paint_script()`, kernel.c) -- closing PAINT's window
currently does *not* signal the script at all, graceful or otherwise.
This design adds that signal rather than assuming it exists: a new
hook, `forth_hook_window_closed(void)` (returns 1 if
`windows[WIN_KIND_PAINT].state != WINDOW_OPEN`, else 0), backing a new
Forth primitive `WINDOW-CLOSED?`. `bin_paint_default[]`'s `UNTIL` line
becomes `MOUSE-RIGHT-DOWN? WINDOW-CLOSED? +` -- `+` works as a logical
OR over this dialect's `{0, -1}` boolean convention (forth.c:218-221)
the same way the script's existing `OVER OVER SWAP -1 > SWAP -1 > *`
already uses `*` as AND, so no new primitive is needed for the
combinator itself. Closing PAINT's window now makes its own script
exit gracefully on its next iteration -- that's the normal path.

`kmain()`'s generic close-button handler (kernel.c:2245-2246, fires for
whichever window's close box was hit, identified by `target`) needs to
know which scheduler slot a given window's script is running in, to
call the new close-timeout API on it. `forth_hook_paint_open()` runs
from inside the running program's own context (it's called from a
Forth primitive), so it can record `paint_program_slot =
scheduler_current_slot()` (a new, generically-named accessor returning
whichever slot is mid-switch, or -1 outside any program) the moment
PAINT's window opens -- no string-matching on the `RUN` argument
needed. The close handler then does `if (target == WIN_KIND_PAINT &&
paint_program_slot >= 0) { scheduler_request_close(paint_program_slot); }`.

What's new beyond that: the desktop now tracks frames-since-close-requested per
program slot (`close_requested`, above). If a program hasn't freed its
own slot within a bounded grace period (60 frames, ~1 second at a
60fps tick) after its window's close was requested, `kmain()`
force-frees the slot directly -- sets `state = PROGRAM_DONE` and stops
switching into it, abandoning its stack outright. This is safe
specifically because nothing in this kernel holds an open resource
across calls (`fs_write`/`fs_create_file`/etc. are complete-in-one-call,
not open-handle-style) -- there's nothing to leak by walking away from
a slot mid-execution. No new UI: this reuses the existing window-close
button, escalating from graceful to forced after a timeout instead of
waiting forever.

This does not help a program that is looping without ever reaching a
`BEGIN...UNTIL` back-edge at all (see Scope's preemption note) --
`scheduler_tick()` itself never regains control from such a program,
so there is no frame on which to even check the timeout. That failure
mode is out of scope for cooperative scheduling by nature, not an
oversight here.

## Testing

- **Unit-level, host-side, no QEMU**: an isolated test harness for
  `context_switch()` alone -- two trivial fiber functions bouncing
  `yield()` back and forth a fixed number of times, asserting both ran
  to completion in the expected order. Cheapest place to catch a
  broken register save/restore before it's buried under Forth/graphics
  behavior.
- **Integration, headless QEMU**: run PLOOP as a scheduled program;
  while it's looping, drive mouse/keyboard through the QEMU monitor and
  confirm via screendump + pixel inspection (the established pattern
  from the 2026-08-18 pass) that an *unrelated* window (a second Forth
  console, or FILES) redraws and responds. This is the direct
  regression test for the bug class motivating this design -- today
  that scenario can't even be expressed as a test, since nothing else
  runs while PLOOP loops.
- **Real hardware**: same scenario, manually, per this project's
  established practice ([[feedback_qemu_input_testing]]) of confirming
  input-path-sensitive changes on real hardware, not just headless
  QEMU -- register save/restore correctness is exactly the kind of
  thing that can differ between QEMU's CPU emulation and real silicon.
- **Kill-timeout path, headless QEMU**: start a program that
  deliberately ignores its close flag, close its window, confirm the
  slot force-frees after the grace period rather than hanging forever.
