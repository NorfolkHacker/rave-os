# A paint/sprite designer for Rave-OS, and a real path for user-written programs

## Purpose

`docs/IDEAS.md` names two things this spec picks up together: "richer
palette use" (the graphics pipeline already draws in full 24-bit true
color, so "256 colour" was never a hardware constraint -- it was
shorthand for "more usable colors than one green accent") and "a real
path for user-written system programs, not just Forth scripts," named
via its own concrete example: a paint program. Both land on the same
answer. Today there is no way for a user to write and run an
interactive graphical program on Rave-OS at all -- `RUN` executes a
Forth script, but Forth has no graphics or mouse words; SHELL's commands
are fixed and compiled into the kernel. This spec gives Forth a small,
deliberate set of graphics/mouse words and ships a real paint/sprite
designer as an ordinary `/BIN` Forth script built on them -- one a user
can open in the EDITOR (`docs/superpowers/specs/2026-08-16-editor-design.md`)
and rewrite themselves, the same way any other `/BIN` script already
works with `RUN`.

## Scope

**In scope**: a new `WIN_KIND_PAINT` window (native C, same pattern
every window so far uses) holding a 16x16 pixel canvas and an 8-color
palette strip, both native; a `SAVE` button writing the canvas to a
fixed path; six new Forth primitives (`PIXEL`, `MOUSE-X`, `MOUSE-Y`,
`MOUSE-DOWN?`, `MOUSE-RIGHT-DOWN?`, `CURRENT-COLOR`) plus one more
(`REFRESH`) that performs a synchronous mid-script redraw -- the first
time this kernel draws-and-presents outside its normal per-frame damage
cycle; a bundled `/BIN/PAINT` Forth script implementing the actual
interactive drawing loop; and a new `PAINT` word (native, not part of
the drawing loop itself) that opens+raises the window.

**Out of scope, deliberately**: a `LOAD` word (loading a previously
saved sprite back in) -- a natural follow-up once saving works, not
needed for a first working paint-and-save loop. A full RGB color
picker -- the fixed 8-swatch palette is the whole "richer palette" ask
for v1. Multiple save slots or a user-chosen save path -- Forth here
has no string-literal mechanism to pass a path on the stack (confirmed:
`forth.h`'s data stack is `int32_t` cells only), so `SAVE` always
writes to one fixed path (`/HOME/SPRITE`), the same "one obvious slot"
simplicity `/ETC/CONFIG` already has. Undo. Any resize beyond 16x16.
Any keyboard input to the paint loop (only the two mouse buttons control
it -- see the loop-exit design below). Multitasking/interrupting a
running paint session from outside it -- this kernel has no
preemption, and giving it one is a far larger undertaking than this
feature; the trade-off (the OS is unresponsive to everything else
while a paint session runs) is accepted explicitly, not hidden.

## Design

### The `forth.c` boundary: hook functions, not a graphics dependency

`forth.c` is deliberately isolated from every GUI/graphics concern
today (no `window.h`, no `graphics.h`, nothing `console_*` -- its own
header says so). Rather than breaking that by including `graphics.h`/
`mouse.h` directly, the new primitives call a small set of `extern`
hook functions declared in a new `forth_hooks.h` (included only by
`forth.c` and `kernel.c`) and *implemented* in `kernel.c`, which already
owns every piece of state a hook needs (the window array, the paint
canvas, the live cursor position). This mirrors the existing
`kernel.c` <-> `forth.c` relationship already running the other
direction (`kernel.c` calls `forth_eval_line()`) -- `forth.c` gains a
few extension points, not a new dependency on an entire subsystem:

```c
/* forth_hooks.h -- forth.c's only window into graphics/mouse state,
 * implemented by kernel.c (which owns all of it already). */
void forth_hook_paint_open(void);
void forth_hook_pixel(int x, int y, int color);
int forth_hook_mouse_x(void);       /* canvas-relative cell column 0..15, or -1 if the cursor isn't over the canvas */
int forth_hook_mouse_y(void);       /* canvas-relative cell row 0..15, or -1 if the cursor isn't over the canvas */
int forth_hook_mouse_down(void);    /* 1 if the left button is currently held */
int forth_hook_mouse_right_down(void); /* 1 if the right button is currently held */
int forth_hook_current_color(void); /* the natively-selected palette index, 0..7 */
void forth_hook_refresh(void);
```

Each new primitive in `forth.c`'s `primitives[]` table (the existing
`{name, fn}` array `OP_CALL_PRIMITIVE` indexes into) is a thin wrapper
matching every existing primitive's own shape (pop/push
`vm->dstack`, call `forth_set_error()` on a bad argument): `PIXEL`
pops `color`, `y`, `x` and calls `forth_hook_pixel()`, erroring
(`"BAD PIXEL"`, same convention `!`/`@`'s `"BAD ADDR"` already
established) if `x`/`y` aren't `0..15` or `color` isn't `0..7`;
`MOUSE-X`/`MOUSE-Y`/`MOUSE-DOWN?`/`MOUSE-RIGHT-DOWN?`/`CURRENT-COLOR`
each just push whatever their hook returns; `REFRESH` calls
`forth_hook_refresh()` and pushes nothing.

### The canvas, palette, and window (`kernel.c`, native)

`WIN_KIND_PAINT` (a fifth window kind), same closed-at-boot,
open-via-a-word pattern every window already has -- opened by the new
`PAINT` word (see below), not by any start-menu entry (matching
EDIT's own SHELL-only precedent from the prior stage: this is reached
through Forth, not a mouse launcher).

```c
struct paint {
    int grid[16][16]; /* palette index 0..7 per cell, row-major */
    int current_color; /* which palette swatch is natively selected, 0..7 */
    int opened_once;   /* clears grid to all-zero only the first time PAINT ever opens */
};
```

**Layout**: canvas 256x256px (16px per cell -- chunky enough to click
precisely, matching the editor's own reasoning for picking sizes
against this project's documented mouse-precision quirks), an 8-swatch
palette strip directly beneath it (256/8 = 32px per swatch exactly, no
gaps, 24px tall), a `SAVE` button beneath that (full 256px wide, 22px
tall, matching FILES'/EDITOR's existing button height). Window body:
272px wide (256 + 16px margins), 330px tall (8 top margin + 256 canvas
+ 4 gap + 24 palette + 8 gap + 22 button + 8 bottom margin).

**Palette** (8 fixed swatches, index 0-7, chosen for real sprite-art
variety while keeping index 5 as the OS's own signature acid-green
accent):

| Index | Color | Hex |
|---|---|---|
| 0 | near-black (background/eraser) | `0x050607` |
| 1 | white | `0xFFFFFF` |
| 2 | red | `0xFF3B30` |
| 3 | orange | `0xFF9500` |
| 4 | yellow | `0xFFEB3B` |
| 5 | acid green | `0x00FF66` |
| 6 | blue | `0x2979FF` |
| 7 | purple | `0xB026FF` |

Clicking a swatch (native click handling, same `button_hit_test()`-
style hit-test every other clickable rect in this codebase already
uses) sets `paint.current_color` -- this is what `CURRENT-COLOR`
reads. Clicking `SAVE` writes the grid to disk (below). The canvas
itself is drawn by iterating `grid[16][16]` and filling each cell's
16x16px rect with its palette color -- this happens both in the normal
per-frame draw path (`draw_paint_group()`, same shape
`draw_files_group()`/`draw_editor_group()` already have) and, mid-script,
inside `forth_hook_refresh()` (next section).

**`PAINT` word**: opens the primitive as `forth_hook_paint_open()` --
if `paint.opened_once` is false, zeroes `grid[16][16]` and sets it
true (so re-running `PAINT` later, e.g. after closing the window,
preserves whatever was drawn rather than wiping it -- same "opening
again just raises" convention FORTH/FILES/SHELL/EDITOR's own launchers
already have); either way, sets `windows[WIN_KIND_PAINT].state =
WINDOW_OPEN` and calls `raise_window()`.

### `SAVE`: fixed path, raw palette-index bytes

`SAVE`'s click handler: `fs_delete("/HOME/SPRITE")` (ignored, same
"doesn't exist yet is fine" convention EDITOR's own `SAVE` already
established) then `fs_create_file("/HOME/SPRITE", &grid_bytes,
256)`, where `grid_bytes` is `grid[16][16]` flattened row-major into
256 raw bytes (each byte already `0..7`, no encoding needed -- the
simplest possible format, matching `/ETC/CONFIG`'s own "just the plain
bytes" minimalism). A failure is a silent no-op, same blanket
no-error-UI convention this whole kernel already follows for
filesystem mutations.

### `REFRESH`: a synchronous mid-script redraw, and its real limitation

This is the one genuinely new piece of architecture here. While a
Forth script's `BEGIN ... UNTIL` loop is executing inside one
`forth_eval_line()` call, `kmain()`'s own event loop -- the only place
that normally calls `gfx_present()` -- is not running at all; this
kernel has no preemption, so nothing else on screen updates until the
whole script call returns. `REFRESH` has to draw-and-present
*immediately*, synchronously, from inside the hook function itself,
bypassing the normal damage-tracked update cycle entirely for this one
call: `forth_hook_refresh()` calls `draw_paint_group()` directly (the
same function the normal per-frame path uses) followed by
`gfx_present_rect()` scoped to just the `WIN_KIND_PAINT` window's
screen region.

**The accepted limitation**: this makes the *canvas* update live while
painting, but nothing else does -- the mouse cursor sprite itself does
not visually move on screen during a running paint session, even
though its live position is being read correctly by
`forth_hook_mouse_x()`/`forth_hook_mouse_y()` each loop iteration (the
PS/2 driver's own interrupt-driven packet decode keeps running
regardless of what the CPU's main flow is doing -- only the *drawing*
of the cursor sprite is tied to `kmain()`'s own loop). A user painting
sees their strokes appear correctly; they just don't see a cursor
glyph tracking their hand while it happens. This is a real, known v1
trade-off, not a bug to silently work around.

### `/BIN/PAINT`: the actual paint program, as ordinary Forth

Bundled at boot the same way `fx_default_from_config()` seeds
`/ETC/CONFIG` -- a real file at `/BIN/PAINT`, written once via
`fs_create_file()` if it doesn't already exist, so a user who edits it
(via the EDITOR: `edit /BIN/PAINT`) keeps their changes across
reboots rather than having them silently overwritten. Its shape:

```forth
PAINT
BEGIN
  MOUSE-DOWN? IF
    MOUSE-X MOUSE-Y
    OVER 0 >= OVER 16 < AND OVER 0 >= OVER 16 < AND
    IF CURRENT-COLOR PIXEL REFRESH ELSE DROP DROP THEN
  THEN
  MOUSE-RIGHT-DOWN?
UNTIL
```

(Illustrative -- the exact token sequence is an implementation detail
for the plan, not a spec-level commitment; the shape is: while the
left button is held and the cursor is over the canvas, paint the
current color at the cursor's cell and refresh; stop when the right
button is pressed.) The bounds check exists because
`forth_hook_mouse_x()`/`forth_hook_mouse_y()` return `-1` when the
cursor isn't over the canvas at all (e.g. hovering the palette strip or
outside the window entirely) -- `PIXEL` would otherwise error on an
out-of-range coordinate.

## Testing

All headless, via the existing `-display none -monitor unix:...`
harness. Real-time mouse-drag testing is exactly the class of thing
this project's own notes flag as unreliable via synthetic
`mouse_move`/`mouse_button` monitor commands for anything requiring
precise, sustained positioning -- so this stage's plan should expect a
live-hardware pass to matter more than usual here, same as prior
mouse-drag-sensitive stages in `docs/BUILD_LOG.md` already needed.

1. Type `PAINT` at the FORTH console -- confirm the window opens,
   raises, and shows a blank 16x16 grid plus the 8-swatch palette
   strip in the documented colors.
2. Click a non-default palette swatch -- confirm `CURRENT-COLOR`
   (typed directly at the FORTH console, `CURRENT-COLOR .`) reflects
   the click.
3. `RUN PAINT` -- with the mouse positioned over a canvas cell and the
   left button held (headless: `mouse_button 1` before `RUN`, since
   the script polls state that must already be true when it starts
   evaluating), confirm that cell fills with the selected color and
   the screen visibly updates via `REFRESH` *before* the script's
   `UNTIL` condition is ever met -- proving the synchronous mid-loop
   redraw actually fires, not just that content changes by the time
   the whole call returns.
4. Right-click (headless: `mouse_button 2`) -- confirm the loop exits
   (control returns to the FORTH/SHELL prompt) and the rest of the OS
   is responsive again (e.g. `CR` or any other word evaluates
   normally right after).
5. Click `SAVE` -- parse `/HOME/SPRITE` directly (256 raw bytes, each
   `0..7`) to confirm it matches the grid's actual content, byte-for-
   byte, at the cell positions painted in step 3.
6. Close the PAINT window, run `PAINT` again -- confirm the canvas
   still shows the previously-painted content (not cleared), proving
   `opened_once` correctly gates the one-time-only clear.
7. `PIXEL` called directly at the FORTH console with an out-of-range
   argument (e.g. `20 0 0 PIXEL`) -- confirm a Forth-level error
   (`"BAD PIXEL"`), not a crash or silent corruption.
8. A live-hardware pass, per this project's own standing note on
   synthetic mouse input's limits: drag the mouse across several
   canvas cells with the left button held during a real `RUN PAINT`
   session, confirming the visual painting genuinely feels live and
   the strokes land where the hand actually is.
