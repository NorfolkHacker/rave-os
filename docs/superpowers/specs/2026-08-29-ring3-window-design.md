# A static, non-interactive ring-3 window (userspace sub-project C, window-management slice)

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry lists window-management as
one of three remaining syscall surfaces for sub-project (C), alongside
the now-shipped fs.h/gfx/audio surfaces. This spec covers the smallest
real slice of it: giving a ring-3 program exactly one window it can
open, draw into (using the already-shipped gfx syscalls), and close --
no dragging, no click/keyboard event delivery back to the program.

That narrower scope was chosen deliberately after investigating what
full interactivity actually requires. `kernel.c`'s window system today
is five hardcoded slots (`WIN_KIND_FORTH`/`FILES`/`SHELL`/`EDITOR`/
`PAINT`), each with bespoke, inline content-drawing code -- there is no
generic "create a window" capability at all yet, and no
process/ownership concept anywhere in the kernel (a ring-3 "program" is
just a function pointer + a stack). Worse: once `kmain()` calls
`enter_ring3()`, it never gets control back -- `kmain()`'s own
per-frame loop (mouse/keyboard polling, redrawing the desktop and the
five built-in apps) simply stops running for good, same as every
existing ring-3 proof already relies on (each one ends in a deliberate
crash rather than returning). Real event delivery to a ring-3 window
would need a blocking `SYS_WAIT_EVENT` whose CPL0 handler internally
re-drives `kmain()`'s own per-frame update loop -- but that loop is
currently one large, deeply inlined function, not something callable.
Extracting it is a real refactor of working code five existing apps
depend on, and is comparable in scope to sub-project (D). This spec
defers all of that; it only covers a window a ring-3 program can open,
paint into once, and close, entirely synchronously within the syscalls
themselves.

## Scope

**In scope:**
- A sixth window slot, `WIN_KIND_RING3`, alongside the five existing
  hardcoded kinds. `MAX_WINDOWS` becomes 6.
- `SYS_WINDOW_OPEN` (`x, y, w, h, title`): sets the slot's geometry and
  title, opens it, raises it to the front of z-order, and -- since
  nothing will call `draw_scene()`/`update_and_present()` again after
  this point -- draws its chrome (`window_draw()`) and presents it
  (`gfx_present_rect()`) synchronously, inside the syscall itself.
- `SYS_WINDOW_CLOSE` (no args): marks the slot closed. Does not erase
  already-presented pixels (see "Known limitation" below).
- Drawing the window's *content* uses the syscalls sub-project (C)'s
  gfx slice already shipped (`SYS_GFX_FILL_RECT`/`SYS_GFX_PUT_PIXEL`/
  `SYS_GFX_PRESENT_RECT`) -- nothing new needed there. The ring-3
  program is responsible for computing coordinates inside its own
  window body; there is no clipping.

**Out of scope, deliberately:**
- **Dragging, minimize/close controls, or any mouse/keyboard event
  delivery to the ring-3 program.** This is the entire reason the
  slice is scoped this small -- see Purpose above.
- **Extracting `kmain()`'s per-frame update loop into a callable
  function**, and any `SYS_WAIT_EVENT`-style syscall built on it.
  Explicitly deferred future work, likely its own multi-piece project
  comparable to sub-project (D).
- **Multiple ring-3 windows, or a real per-process ownership model.**
  There is still only one ring-3 "program" ever running at a time (no
  process table exists), so one slot is all this needs.
- **Fixing `draw_window_by_index()`'s bare `else` fallback**, which
  currently assumes any window kind it doesn't recognize is `PAINT`.
  This is a real latent bug for `WIN_KIND_RING3` once `kmain()`'s loop
  can run again with a ring-3 window present -- but that loop never
  runs again in this architecture once `enter_ring3()` is called, so
  the bug is unreachable today. Left as a documented comment, not
  fixed, since fixing it now would be speculative work for a code path
  this slice never exercises.
- **Any pointer/bounds validation of ring-3-supplied pointers**
  (including `title`). Same stance every prior syscall sub-project has
  taken.

## Design

### `kernel/kernel.c` changes

```c
#define MAX_WINDOWS 6            /* was 5 */
#define WIN_KIND_RING3 5         /* new, alongside FORTH/FILES/SHELL/EDITOR/PAINT */
```

Boot-time init gains a sixth line alongside the other five
`windows[WIN_KIND_*].state = WINDOW_CLOSED;` calls (`WINDOW_OPEN` is
`#define`d `0`, so an unset slot defaults to open -- missing this would
show a garbage window on every ordinary boot) and `z_order[]`'s
init block gains `z_order[5] = WIN_KIND_RING3;`.

Two new non-static functions (declared `extern` directly at the call
site in `syscall_window.c`, matching this codebase's existing
`context_switch()`/`enter_ring3()` convention -- no new header):

```c
void window_ring3_open(int x, int y, int w, int h, const char *title) {
    windows[WIN_KIND_RING3].x = x;
    windows[WIN_KIND_RING3].y = y;
    windows[WIN_KIND_RING3].w = w;
    windows[WIN_KIND_RING3].h = h;
    windows[WIN_KIND_RING3].title = title;
    windows[WIN_KIND_RING3].state = WINDOW_OPEN;
    raise_window(z_order, WIN_KIND_RING3);
    window_draw(&windows[WIN_KIND_RING3]);
    gfx_present_rect(x - 2, y - WINDOW_TITLEBAR_HEIGHT - 2,
                      w + 4, h + WINDOW_TITLEBAR_HEIGHT + 4);
}

void window_ring3_close(void) {
    windows[WIN_KIND_RING3].state = WINDOW_CLOSED;
}
```

The `-2`/`+4` border padding matches `window.h`'s own documented
convention ("the border is drawn 2px further out again" beyond the
titlebar).

### `kernel/arch/syscall.h` additions

```c
#define SYS_WINDOW_OPEN 22
struct sys_window_open_args {
    int x;
    int y;
    int w;
    int h;
    const char *title;
};

#define SYS_WINDOW_CLOSE 23
```

### `kernel/arch/syscall_window.c` (new file)

```c
#include "syscall.h"

extern void window_ring3_open(int x, int y, int w, int h, const char *title);
extern void window_ring3_close(void);

int syscall_dispatch_window(int num, int arg) {
    if (num == SYS_WINDOW_OPEN) {
        const struct sys_window_open_args *a = (const struct sys_window_open_args *)arg;
        window_ring3_open(a->x, a->y, a->w, a->h, a->title);
        return 0;
    }
    if (num == SYS_WINDOW_CLOSE) {
        window_ring3_close();
        return 0;
    }
    return syscall_dispatch_core(num, arg);
}
```

`syscall_audio.c`'s `syscall_dispatch_audio()` falls through to this
instead of `syscall_dispatch_core()` directly, extending the chain to
five links: `syscall_dispatch()` (fs) -> `syscall_dispatch_gfx()`
(gfx) -> `syscall_dispatch_audio()` (audio) ->
`syscall_dispatch_window()` (window) -> `syscall_dispatch_core()`
(pure fallback). `kernel/Makefile` gains a `syscall_window.o` rule and
joins `C_OBJS`.

### Known limitation: `SYS_WINDOW_CLOSE` doesn't erase pixels

Since nothing redraws the screen after this point in the current
architecture (same reasoning as every prior proof payload), closing
the window only updates its `state` -- the chrome and content drawn by
`SYS_WINDOW_OPEN`/the ring-3 program's own gfx calls stay visible on
screen. This is a real, understood limitation of the "no interactivity"
scope, not a bug: a real close (erasing/redrawing whatever was
underneath) needs the same per-frame-loop machinery the "out of scope"
section above defers.

## Testing

**Host-buildable:** no new host test -- same reasoning as every prior
syscall slice; `syscall_dispatch_core()` stays dependency-free on
`window`-related state, confirmed by the existing host test still
linking and passing unchanged.

**Headless QEMU, one-time and throwaway**, same technique every prior
entry has used: a temporary ring-3 payload calls `SYS_WINDOW_OPEN`
with a distinct title and geometry, draws recognizable content into
the window body via the existing gfx syscalls (e.g. a filled rect),
reads `windows[WIN_KIND_RING3]` back directly afterward (a legitimate
read, same justification the audio slice's proof already established:
PDE 0 is fully user-accessible, and this is an ordinary struct in a
`static` array the syscall boundary doesn't hide the existence of --
only the syscall path can *write* it) to confirm `x`/`y`/`w`/`h`/
`title`/`state` all match what was requested, calls `SYS_WINDOW_CLOSE`,
reads `windows[WIN_KIND_RING3].state` again to confirm it's now
`WINDOW_CLOSED`, then falls into the usual deliberate `SYS_EXIT` +
`cli` tail gated on all of the above. The proof screendump itself
should show a real bordered window with a titlebar and title text
sitting on the ordinary desktop (not a full-screen overwrite, unlike
the gfx slice's proof) -- visual confirmation `window_draw()` really
ran, on top of the struct-read confirmation that the syscall's
arguments really reached it. A final regression screendump (after
removing the temporary payload and reverting `kernel.c`) must still
show the ordinary desktop, confirming the sixth window slot's addition
is a true no-op when nothing opens it -- in particular confirming the
new `windows[WIN_KIND_RING3].state = WINDOW_CLOSED;` boot-time init
was not missed.
