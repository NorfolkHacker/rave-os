# Keyboard events for the ring-3 window: RING3_EVENT_KEY

## Purpose

`docs/superpowers/specs/2026-08-29-ring3-window-events-design.md`
shipped `SYS_WAIT_EVENT` with two event types, click and close, but
explicitly deferred keyboard input as out of scope. Content
persistence shipped separately
(`docs/superpowers/specs/2026-08-29-ring3-window-content-persistence-design.md`).
This spec closes the last major gap named in both: a ring-3 program
still has no way to receive a keystroke, even once it has keyboard
focus by every visible measure (its window is topmost, the user has
clicked into it).

## Scope

**In scope:**
- One new event type, `RING3_EVENT_KEY`, delivered through the
  existing `SYS_WAIT_EVENT` mechanism -- no new syscall number.
- A persistent `ring3_focused` flag, set the same way, at the same
  site, and with the same semantics as every other window's own
  `.focused` field already in `kmain_frame()` (`ci.focused`,
  `shell_ci.focused`, `name_input.focused`, `ed.focused`,
  `paint_name_input.focused`): on a click edge, focused if and only if
  the ring-3 window is topmost *and* the click landed inside its body
  rect; any click elsewhere (another window, the backdrop, the start
  menu) clears it. Confirmed with the user: no auto-focus-on-open --
  matches every existing window, no special-casing for this one.
- The event's `key` field carries exactly the `char` value
  `keyboard_poll_char()` already produces for the currently-focused
  ring-3 window: printable ASCII, `'\b'`, `'\n'`, or one of
  `KEY_UP`/`KEY_DOWN`/`KEY_LEFT`/`KEY_RIGHT`/`KEY_HOME`/`KEY_END`/
  `KEY_DELETE` (`kernel/drivers/keyboard.h`). No new decoding: this is
  the same input vocabulary every other focused text field in this
  kernel already receives, nothing more.

**Out of scope, deliberately:**
- **Raw scancodes, modifier keys (ctrl/alt/shift-as-a-distinct-event),
  key-up events.** `keyboard_poll_char()` already discards all of this
  silently before a caller ever sees it; ring-3 programs get the same
  reduced vocabulary as everywhere else in this codebase, not a richer
  one.
- **A queue, or any depth beyond a single pending event.** Same
  justification as the click/close design: only one ring-3 program
  ever runs at a time, it consumes events synchronously, and human
  typing speed cannot outrun one `SYS_WAIT_EVENT` round-trip per
  keystroke in practice.
- **Solving the same-frame click-vs-key race.** `ring3_event_pending`
  is one slot. If a click and a keypress both become detectable inside
  the same `kmain_frame()` iteration, whichever check runs second
  overwrites the first's result and that event is silently lost. This
  category of race already exists today (click vs. close can already
  collide the same way) and is not solved by this slice either --
  correctly fixing it means a real event queue, a bigger lift than
  this feature justifies yet.
- **Auto-focus-on-open, or any other new focus-acquisition path.**
  Explicitly decided against above.

## Design

### File-scope state (`kernel/kernel.c`)

```c
#define RING3_EVENT_NONE 0
#define RING3_EVENT_CLICK 1
#define RING3_EVENT_CLOSED 2
#define RING3_EVENT_KEY 3

static int ring3_event_pending = RING3_EVENT_NONE;
static int ring3_event_x, ring3_event_y;   /* valid only for CLICK */
static char ring3_event_key;               /* valid only for KEY */
static int ring3_focused;                  /* persists across frames, like ci.focused etc. */
```

### Acquiring focus (alongside the existing click-edge focus block)

The existing block:

```c
if (click_edge) {
    ci.focused = forth_is_topmost && console_input_hit_test(&ci, cx, cy);
    name_input.focused = files_is_topmost && console_input_hit_test(&name_input, cx, cy);
    shell_ci.focused = shell_is_topmost && console_input_hit_test(&shell_ci, cx, cy);
    ed.focused = editor_is_topmost && editor_hit_test(&ed, cx, cy);
    paint_name_input.focused = paint_is_topmost && console_input_hit_test(&paint_name_input, cx, cy);
}
```

gains one more line, reusing the same body-rect check the click-event
block just above it already computes:

```c
    ring3_focused = ring3_is_topmost &&
        cx >= windows[WIN_KIND_RING3].x && cx < windows[WIN_KIND_RING3].x + windows[WIN_KIND_RING3].w &&
        cy >= windows[WIN_KIND_RING3].y && cy < windows[WIN_KIND_RING3].y + windows[WIN_KIND_RING3].h;
```

No hit-test helper needed (unlike the text-field windows): the
ring-3 window has no sub-widgets, so its entire body rect is the
target, the same rect already used for click-event detection just
above.

### Detecting a key (inside the existing `keyboard_poll_char()` chain)

The existing chain is one long `if (ci.focused) {...} else if
(shell_ci.focused) {...} else if (name_input.focused) {...} else if
(ed.focused) {...} else if (paint_name_input.focused) {...}` --
mutually exclusive today because the click-edge block above enforces a
single focus owner. This gains one more branch, in the same chain:

```c
if (keyboard_poll_char(&c)) {
    if (ci.focused && c == KEY_UP) {
        ...
    /* ...existing branches, unchanged... */
    } else if (paint_name_input.focused) {
        ...
    } else if (ring3_focused) {
        ring3_event_pending = RING3_EVENT_KEY;
        ring3_event_key = c;
    }
}
```

Placed last in the chain: harmless either way since focus is already
mutually exclusive, but keeping existing branches' order untouched
minimizes the diff.

### The blocking wrapper (`kernel/kernel.c`)

`ring3_wait_event()` gains one output parameter:

```c
void ring3_wait_event(int *type, int *x, int *y, char *key) {
    while (ring3_event_pending == RING3_EVENT_NONE) {
        kmain_frame();
    }
    *type = ring3_event_pending;
    *x = ring3_event_x;
    *y = ring3_event_y;
    *key = ring3_event_key;
    ring3_event_pending = RING3_EVENT_NONE;
}
```

`x`/`y` remain valid only for `RING3_EVENT_CLICK`, `key` only for
`RING3_EVENT_KEY` -- callers only ever read the field matching the
returned `type`, same convention as today.

### `kernel/arch/syscall.h` / `syscall_window.c`

```c
#define RING3_EVENT_KEY 3

struct sys_wait_event_args {
    int type;
    int x;      /* valid only when type == RING3_EVENT_CLICK */
    int y;      /* valid only when type == RING3_EVENT_CLICK */
    char key;   /* valid only when type == RING3_EVENT_KEY */
};
```

```c
if (num == SYS_WAIT_EVENT) {
    struct sys_wait_event_args *a = (struct sys_wait_event_args *)arg;
    extern void ring3_wait_event(int *type, int *x, int *y, char *key);
    ring3_wait_event(&a->type, &a->x, &a->y, &a->key);
    return 0;
}
```

Existing callers of `SYS_WAIT_EVENT` (the old proof payloads, all
already removed) needed no changes for this -- the struct only grows.

## Testing

**Host-buildable:** no new host test, same reasoning as the click/close
slice -- the additions are thin, and `syscall_dispatch_core()` stays
dependency-free.

**Headless QEMU**, same technique as every prior slice. A temporary
ring-3 payload opens the window, then loops on `SYS_WAIT_EVENT`: on
`RING3_EVENT_CLICK`, draw a small rect at the click position (reusing
the existing click-proof shape, so a click still visibly proves the
window has the event mechanism working at all); on `RING3_EVENT_KEY`,
draw a distinct small rect (a different color) whose position advances
each time, so multiple keypresses are visibly distinguishable in one
screendump; on `RING3_EVENT_CLOSED`, break out into the usual
deliberate `SYS_EXIT` + `cli` tail.

Verification sequence:

1. Boot; click inside the window body to focus it (per the "click
   first" design decision) -- screendump: confirm the click's own rect
   appeared, proving click-vs-focus don't interfere with each other.
2. Inject a `sendkey` (e.g. a letter, matching `keyboard_poll_char()`'s
   translated ASCII) via the QEMU monitor. Screendump: confirm a new
   key-rect appeared -- proof the keystroke reached the ring-3 program
   while it was focused.
3. Click on a *different* window (e.g. the desktop backdrop or another
   window) to defocus the ring-3 window, then inject another `sendkey`.
   Screendump: confirm *no* new key-rect appeared -- proof focus is
   correctly required, not just "any key always reaches the ring-3
   window."
4. Click back inside the ring-3 window body to refocus it, inject one
   more `sendkey`. Screendump: confirm a new key-rect appeared again --
   proof focus can be reacquired, not a one-shot.
5. Click the window's own close button. Screendump: confirm the usual
   panic banner.

Then remove the temporary payload, rebuild, and confirm a final
regression screendump matches the ordinary desktop, same as every
prior slice.
