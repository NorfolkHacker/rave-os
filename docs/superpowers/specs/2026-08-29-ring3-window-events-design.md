# Real event delivery for the ring-3 window: SYS_WAIT_EVENT

## Purpose

`docs/superpowers/specs/2026-08-29-ring3-window-design.md` shipped a
static, non-interactive ring-3 window: a program can open it, draw
into it once via the gfx syscalls, and close it, but nothing routes
mouse/keyboard input to it. `docs/superpowers/specs/2026-08-29-
kmain-frame-extraction-design.md` then removed the one structural
blocker to fixing that -- `kmain()`'s per-frame update loop is now a
standalone `kmain_frame()`, callable from anywhere, not inlined where
only `kmain()` itself could ever reach it. This spec is the payoff:
`SYS_WAIT_EVENT`, a syscall that blocks a ring-3 program until
something happens to its window, by re-driving `kmain_frame()` itself
from inside the syscall handler.

## Scope

**In scope:**
- Two event types: `RING3_EVENT_CLICK` (a left-click landed inside the
  window's content body, absolute screen coordinates) and
  `RING3_EVENT_CLOSED` (the user clicked the window's own close
  button). Chosen after discussion with the user, matching the YAGNI
  scoping every prior syscall slice used -- no keyboard events, no
  move/resize notifications, no drag events (dragging already works
  generically for every window kind, `WIN_KIND_RING3` included, and
  needs no new code -- see Design below).
- `SYS_WAIT_EVENT`: blocks (by looping `kmain_frame()`) until one of
  the two event types above is available, then returns it.
- Detecting a `RING3_EVENT_CLICK`: added to `kmain_frame()`'s existing
  per-window topmost-click routing block, the same shape every other
  window kind's own click handling already uses there.
- Detecting a `RING3_EVENT_CLOSED`: added to `kmain_frame()`'s existing
  close-button handling, alongside the `WIN_KIND_PAINT`-specific case
  that already lives there.
- A single-slot "last pending event," not a queue. Justified: only one
  ring-3 program can ever run at a time, it consumes events
  synchronously (blocking on the syscall), and real mouse clicks are
  naturally rate-limited by human interaction speed -- nothing in this
  design can generate events faster than one `SYS_WAIT_EVENT` call can
  drain them.

**Out of scope, deliberately:**
- **Keyboard events, window move/resize notifications, an event
  queue.** All named above; deferred, not designed.
- **Any change to dragging, raising, minimizing, or closing
  themselves.** All four already work for `WIN_KIND_RING3` exactly as
  they do for every other window kind -- confirmed by reading
  `kmain_frame()`'s existing click-routing code directly:
  `topmost_window_at()`/`dragging_window`/the close-button hit-test are
  not kind-filtered anywhere. This spec only adds *content*-click and
  *close* event *reporting*, on top of behavior that already exists.
- **Any change to `program_load_and_run()`, `enter_ring3()`, or
  `ring3.asm`.** `SYS_WAIT_EVENT` is an ordinary syscall using the
  existing ABI; nothing about how a program starts changes.
- **Any pointer/argument validation.** Same stance every prior syscall
  sub-project has taken. A program calling `SYS_WAIT_EVENT` before
  ever opening a window simply blocks forever, since no event can ever
  be detected for a window that was never opened -- an unhelpful but
  reasonable outcome for that programmer error, not a case worth
  guarding against.

## Design

### File-scope event state (`kernel/kernel.c`)

```c
#define RING3_EVENT_NONE 0
#define RING3_EVENT_CLICK 1
#define RING3_EVENT_CLOSED 2

static int ring3_event_pending = RING3_EVENT_NONE;
static int ring3_event_x, ring3_event_y;   /* valid only for CLICK */
```

### Detecting a click (inside `kmain_frame()`'s existing per-window routing)

The existing block already computes `int topmost = topmost_window_at(...)`
and per-kind booleans (`forth_is_topmost`, `files_is_topmost`, ...) right
before routing each window's own content clicks. This gains one more:

```c
int ring3_is_topmost = topmost == WIN_KIND_RING3;
```

...and, alongside the other windows' own `if (X_is_topmost && click_edge)`
blocks:

```c
if (ring3_is_topmost && click_edge &&
    cx >= windows[WIN_KIND_RING3].x && cx < windows[WIN_KIND_RING3].x + windows[WIN_KIND_RING3].w &&
    cy >= windows[WIN_KIND_RING3].y && cy < windows[WIN_KIND_RING3].y + windows[WIN_KIND_RING3].h) {
    ring3_event_pending = RING3_EVENT_CLICK;
    ring3_event_x = cx;
    ring3_event_y = cy;
}
```

Absolute coordinates, not window-relative: every other syscall a
ring-3 program uses (`SYS_WINDOW_OPEN`'s `x`/`y`, every gfx syscall)
already works in absolute screen space, so reporting click coordinates
the same way is the consistent choice, not a new convention. The rect
check against `windows[WIN_KIND_RING3].x/y/w/h` naturally excludes the
titlebar without any extra logic: `window.h`'s own documented
convention is that `x`/`y`/`w`/`h` already describe the body only, the
titlebar sitting `WINDOW_TITLEBAR_HEIGHT` pixels *above* `y` -- so a
titlebar click (which starts a drag, handled earlier and separately)
never satisfies this rect check at all.

### Detecting a close (inside `kmain_frame()`'s existing close-button handling)

The existing block already special-cases one kind:

```c
if (window_close_hit_test(&windows[target], cx, cy)) {
    windows[target].state = WINDOW_CLOSED;
    if (target == WIN_KIND_PAINT && paint_program_slot >= 0) {
        scheduler_request_close(paint_program_slot);
    }
```

This gains a second, same shape:

```c
    if (target == WIN_KIND_RING3) {
        ring3_event_pending = RING3_EVENT_CLOSED;
    }
```

### The blocking wrapper (`kernel/kernel.c`)

```c
/* Blocks -- by re-driving kmain_frame() itself, the same function
 * kmain()'s own for(;;) loop calls -- until a ring-3 window event is
 * available, then returns it and clears the pending slot. Not a
 * syscall itself; SYS_WAIT_EVENT's handler (syscall_window.c) calls
 * this directly, the same extern-at-call-site convention
 * window_ring3_open()/program_load_and_run() already use. Real mouse/
 * keyboard input keeps arriving and getting processed on every
 * kmain_frame() call this makes -- that's the entire point: a ring-3
 * program blocked in here does not freeze the desktop the way every
 * prior ring-3 proof's own infinite loop always has. */
void ring3_wait_event(int *type, int *x, int *y) {
    while (ring3_event_pending == RING3_EVENT_NONE) {
        kmain_frame();
    }
    *type = ring3_event_pending;
    *x = ring3_event_x;
    *y = ring3_event_y;
    ring3_event_pending = RING3_EVENT_NONE;
}
```

`kmain_frame()` itself stays `static` -- unlike the fs/gfx/audio/window
wrappers, `ring3_wait_event()` doesn't need to expose it, only call it
from within the same file.

### `kernel/arch/syscall.h` / `syscall_window.c`

```c
#define SYS_WAIT_EVENT 24

struct sys_wait_event_args {
    int type;   /* RING3_EVENT_CLICK or RING3_EVENT_CLOSED, filled in */
    int x;      /* filled in, valid only when type == RING3_EVENT_CLICK */
    int y;      /* filled in, valid only when type == RING3_EVENT_CLICK */
};
```

```c
if (num == SYS_WAIT_EVENT) {
    struct sys_wait_event_args *a = (struct sys_wait_event_args *)arg;
    extern void ring3_wait_event(int *type, int *x, int *y);
    ring3_wait_event(&a->type, &a->x, &a->y);
    return 0;
}
```

`syscall_dispatch_window()`'s own fallthrough (`syscall_dispatch_core()`)
is unchanged; this is one more `if` case in the same function.

## Testing

**Host-buildable:** no new host test, same reasoning as every prior
syscall slice -- `ring3_wait_event()` and the event-detection additions
are thin, and `syscall_dispatch_core()` stays dependency-free.

**Headless QEMU, the first proof this session that exercises a
genuinely blocking, event-driven interaction rather than a
straight-line sequence.** A temporary ring-3 payload opens the window,
then loops: call `SYS_WAIT_EVENT`; on `RING3_EVENT_CLICK`, draw a small
filled rect at the reported coordinates via the existing gfx syscalls
(visible, cumulative proof each click actually arrived, at the actual
reported position); on `RING3_EVENT_CLOSED`, break out and fall into
the usual deliberate `SYS_EXIT` + `cli` tail.

Verification sequence, using the same QEMU-monitor `mouse_move`/
`mouse_button` primitives already proven this session:

1. Boot; the ring-3 payload opens its window and immediately blocks in
   `SYS_WAIT_EVENT` -- confirm via screendump that the window's chrome
   is visible and the rest of the desktop (taskbar, cursor) is still
   live and redrawing normally, proving the block does not freeze
   `kmain_frame()`'s own real work.
2. Inject a `mouse_move`/`mouse_button` click landing inside the
   window's body. Screendump: confirm a rect appeared at (roughly) the
   click position -- proof the event reached the ring-3 program with
   correct coordinates, not just that the syscall returned.
3. Repeat step 2 at a different position inside the body -- confirm a
   *second* rect appeared, proving the loop genuinely continues (not a
   one-shot).
4. Inject a click on the window's own close button. Screendump:
   confirm the usual `PANIC: GENERAL PROTECTION FAULT` /
   `CODE=0x00000000` banner -- reaching it requires the `RING3_EVENT_CLOSED`
   path to have actually fired and the payload's own loop to have
   broken out cleanly, not just that a click was registered somewhere.

Then remove the temporary payload, rebuild, and confirm a final
regression screendump matches the ordinary desktop, same as every
prior slice.
