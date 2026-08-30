# A standalone, clickable paint binary (retiring the in-kernel PAINT window)

## Purpose

Today "PAINT" is not really an application -- it's kernel-resident
window chrome (`draw_paint_group()`, hit-testing, save/load button
handlers, all in `kernel.c`) plus a Forth script (`/BIN/PAINT`, seeded
by `seed_bin_paint_script()`) that drives the actual drawing loop
through a handful of paint-specific Forth hooks
(`forth_hook_pixel()`/`forth_hook_mouse_x()`/`forth_hook_current_color()`/
etc.). It only launches by typing `RUN PAINT` in a console; there is no
clickable icon for it, matching the deliberate "PAINT/EDITOR get no
start-menu entry" precedent noted in `kmain()`.

This spec moves PAINT out of the kernel entirely into
`programs/paint/paint.bin` -- a genuinely standalone, on-disk,
freestanding flat binary in the same shape as `programs/hello/`, driven
purely through the existing ring-3 syscall surface (window/gfx/fs) with
no kernel-side paint state, no Forth involvement, and no new syscalls.
It becomes the first real, user-facing consumer of `SYS_WINDOW_OPEN`/
`SYS_WAIT_EVENT` and of `program_load_and_run()` -- both already shipped
as working plumbing but never wired to anything a user can actually
click ("there's no launcher for this one" / "not wired to any UI
trigger" in the current source). A new Start Menu "PAINT" entry becomes
that trigger.

## Scope

**In scope:**
- `programs/paint/`: `paint.c`, `paint.ld` (fixed load address
  `0x00200000`, identical to `programs/hello/hello.ld`), a `font.c`/
  `font.h` holding a trimmed copy of `kernel/gfx/font.c`'s glyph table
  (only the characters PAINT's UI actually draws: `A-Z`, `0-9`, and the
  literal `SAVE`/`LOAD` labels), and a `Makefile` mirroring
  `programs/hello/Makefile`'s cross-toolchain invocation.
- A 16x16-cell canvas (256x256px, 16px cells, same as today), a
  single-row 16-swatch color strip beneath it (replacing today's
  swatch-plus-popup -- see the palette decision below), a filename text
  field, and SAVE/LOAD buttons -- built entirely from
  `SYS_GFX_PUT_PIXEL`/`SYS_GFX_FILL_RECT`/`SYS_GFX_PRESENT_RECT` and the
  embedded font, with no widget code borrowed from the kernel (`button.c`/
  `console_input.h` stay kernel-internal, unreachable from ring 3).
- Save/load using the exact on-disk format the old code used: 256 raw
  bytes, one per cell, row-major, palette index 0-15, written to
  `/HOME/<NAME>` (name-field text, uppercased) via `SYS_DELETE` +
  `SYS_CREATE_FILE`, read back via `SYS_READ_FILE` with the same
  "only accept an exact 256-byte file, clamp any out-of-range index to
  0" validation the kernel version already had.
- `kernel/kernel.c`: embedding the built `paint.bin` as a byte array
  (same technique as `hello_bin`) and seeding it to `/BIN/PAINT.BIN` via
  `fs_create_file()` in `kmain()`, alongside the existing `/BIN/HELLO`/
  `/BIN/USERPROG.BIN` seeding.
- `kernel/gui/startmenu.h` + its click handler in `kernel.c`: a new
  `STARTMENU_ITEM_PAINT` that calls `program_load_and_run("/BIN/PAINT.BIN")`
  directly instead of opening a window -- the first real caller of
  `program_load_and_run()` from a genuine user click.
- Deleting every kernel-side PAINT thing: `struct paint`/the `paint`
  global, `paint_save_btn`/`paint_load_btn`/`paint_name_input`/
  `paint_program_slot`, `paint_palette[]`, `draw_paint_group()`,
  `paint_swatch_hit_test()`, `paint_popup_grid_hit_test()`,
  `paint_mouse_cell()`, `paint_build_sprite_path()`, every
  `forth_hook_paint_open`/`forth_hook_pixel`/`forth_hook_mouse_x`/
  `forth_hook_mouse_y`/`forth_hook_mouse_down`/
  `forth_hook_mouse_right_down`/`forth_hook_current_color`/
  `forth_hook_window_closed` function and its `forth_hooks.h`
  declaration and `forth.c` primitive-word binding, `seed_bin_paint_script()`
  and `BIN_PAINT_PATH`, the `PAINT_*` layout macros, the `WIN_KIND_PAINT`
  enum value and every `if`/`switch` arm keyed on it (window init,
  `draw_window_by_index()`, click dispatch, drag/move,
  `touched[WIN_KIND_PAINT]` damage tracking), and the `pt`/
  `paint_name_input`/`paint_save_btn`/`paint_load_btn` fields of `struct
  window_content`.
- A `docs/BUILD_LOG.md` closeout entry, matching every prior slice.

**Out of scope, deliberately:**
- **The palette popup and per-color hide/restore.** These depended on
  right-click, which `SYS_WAIT_EVENT` cannot report (only a left-click
  edge is ever delivered to a ring-3 window -- see
  `docs/superpowers/specs/2026-08-29-ring3-window-events-design.md`).
  Confirmed with the user: v1 uses a plain always-visible 16-swatch
  strip, left-click only. No syscall ABI change.
- **Any change to `SYS_WAIT_EVENT`, `SYS_WINDOW_OPEN`, or any other
  existing syscall.** PAINT is built entirely on what already exists.
- **Fixing "only one ring-3 program per boot."** Confirmed with the
  user: PAINT can be launched once per boot; reopening it after its
  window is closed requires a reboot, the same pre-existing limitation
  every prior ring-3 slice already carries. Real process teardown is a
  separate, much larger future change.
- **A second launch path.** Only the new Start Menu entry launches it;
  `RUN PAINT` in a console goes away with the Forth script it used to
  run (there is nothing left at `/BIN/PAINT` for it to run).
- **Dragging the window.** The generic `WIN_KIND_RING3` window slot
  handles raising and the close button identically to every other
  window kind, but *not* dragging: `move_window_content()` (kernel.c)
  has no `WIN_KIND_RING3` arm to translate `ring3_shadow` (a
  screen-space buffer -- it would blit at the window's new position
  showing pixels never written there) or to tell the running program
  its window moved (a ring-3 program has no move event, and caches its
  own click-coordinate state, e.g. `win_x`/`win_y`, once at startup).
  A real fix means adding a move event plus shadow-buffer geometry
  translation -- out of scope for a branch about *moving PAINT out of
  the kernel*, not adding ring-3 window mobility. Dragging is disabled
  for `WIN_KIND_RING3` windows (title-bar drag is a no-op; the window
  simply doesn't move) rather than shipping broken-but-draggable. This
  is generic to every ring-3 window, not paint-specific, the same kind
  of limitation as "only one ring-3 program at a time" above.

## Design

### `programs/paint/paint.ld`

Identical in shape to `programs/hello/hello.ld`: `ENTRY(_start)`,
origin `0x00200000`, `.text`/`.rodata`/`.data`/`.bss` sections,
`/DISCARD/`s `.eh_frame`/`.comment`/`.note.*`.

### `programs/paint/paint.c`

Freestanding, no libc, no kernel headers -- same stance
`programs/hello/hello.c` already documents: the syscall numbers and
argument structs it needs are re-declared locally as the same fixed
values `kernel/arch/syscall.h` already documents, not included from it.

```c
#define SYS_READ_FILE 2
struct sys_read_file_args { const char *path; void *buf; unsigned int buf_size; unsigned int *out_size; };
#define SYS_CREATE_FILE 3
struct sys_create_file_args { const char *path; const void *data; unsigned int size; };
#define SYS_DELETE 5              /* arg IS the path pointer directly */
#define SYS_GFX_WIDTH 11
#define SYS_GFX_HEIGHT 12
#define SYS_GFX_PUT_PIXEL 14
struct sys_gfx_put_pixel_args { int x; int y; unsigned int rgb; };
#define SYS_GFX_FILL_RECT 15
struct sys_gfx_fill_rect_args { int x; int y; int w; int h; unsigned int rgb; };
#define SYS_GFX_PRESENT_RECT 16
struct sys_gfx_present_rect_args { int x; int y; int w; int h; };
#define SYS_WINDOW_OPEN 22
struct sys_window_open_args { int x; int y; int w; int h; const char *title; };
#define SYS_WAIT_EVENT 24
#define RING3_EVENT_NONE 0
#define RING3_EVENT_CLICK 1
#define RING3_EVENT_CLOSED 2
#define RING3_EVENT_KEY 3
struct sys_wait_event_args { int type; int x; int y; char key; };
```

A single `syscall1(num, arg)` inline-asm helper (`int $0x80`, `eax` in/out,
`ebx` arg, matching `hello.c`'s own inline asm exactly) backs every call
below.

**Layout** (window body, top-left-relative): canvas at `(8, 8)`,
256x256; a 16-swatch strip at `(8, 272)`, each swatch 16x20 with a 2px
accent border on the selected one; filename field at `(8, 300)`,
`208x20`; SAVE/LOAD buttons at `(8, 326)` and `(116, 326)`, each
`100x22`. Window opened at a fixed size `272x356` (same size as the
old kernel-side window -- the canvas dimensions are unchanged, so the
same `8px` margins on a `256px`-wide canvas still require `272px`)
positioned proportionally via
`SYS_GFX_WIDTH`/`SYS_GFX_HEIGHT` (same `340*w/BASELINE_W`-style
placement `kmain()` already uses for every other window, computed
locally since paint.c has no access to `kmain()`'s own constants).

**State:** `int grid[16][16]` (palette index per cell, all zero at
start -- no `opened_once` flag needed, since this is a fresh process
every launch), `int current_color`, a `char name[FS_PATH_MAX]` buffer
with length/cursor for the filename field (always focused -- the only
text field in this program, so no click-to-focus logic is needed).

**Main loop:**
```
open window (SYS_WINDOW_OPEN)
draw canvas + palette strip + filename field + SAVE/LOAD buttons
present the whole window body (SYS_GFX_PRESENT_RECT)
loop:
    SYS_WAIT_EVENT -> {type, x, y, key}
    if type == RING3_EVENT_CLICK:
        if click lands in canvas: set that cell to current_color, redraw+present just that cell
        else if click lands in swatch strip: set current_color, redraw+present the strip
        else if click lands on SAVE: build path, SYS_DELETE + SYS_CREATE_FILE(256 raw bytes)
        else if click lands on LOAD: SYS_READ_FILE into a 256-byte buffer, accept only if out_size == 256,
                                      clamp each byte to < 16 else 0, redraw+present canvas
    else if type == RING3_EVENT_KEY:
        backspace: drop last char; printable ASCII: append (bounded by FS_PATH_MAX - 1);
        redraw+present the filename field
    else if type == RING3_EVENT_CLOSED:
        break
idle tail loop: SYS_WAIT_EVENT forever, discarding whatever it returns --
    keeps re-driving kmain_frame() (see ring3_wait_event()'s own
    doc-comment in kernel.c) so the rest of the desktop stays fully
    responsive after PAINT's own window is gone, exactly like every
    other window kind already does; this program's own execution never
    returns to kmain()'s original call site (enter_ring3() is one-way),
    so there is nothing else it could do instead.
```

Text rendering (`font.c`, embedded copy of the relevant glyphs from
`kernel/gfx/font.c`) draws each 5x7 glyph as up to 35
`SYS_GFX_PUT_PIXEL` calls -- the same per-pixel cost the kernel's own
text rendering already pays, just issued across the syscall boundary
instead of a direct call; this only happens when a label is
(re)drawn, not per frame.

### `kernel/kernel.c` additions

```c
static const unsigned char paint_bin[] = { /* real programs/paint/paint.bin bytes */ };
fs_create_file("/BIN/PAINT.BIN", paint_bin, (unsigned int)sizeof(paint_bin));
```
seeded alongside the existing `hello_bin`/`/BIN/USERPROG.BIN` block.

### `kernel/gui/startmenu.h` / start-menu click handling

```c
#define STARTMENU_ITEM_PAINT 7   /* existing items 0-6 unchanged, count 7 -> 8 */
```
Its click handler calls `program_load_and_run("/BIN/PAINT.BIN")`
directly -- unlike every other item (which just opens a window and lets
`kmain()`'s normal loop continue), this call never returns to that loop.

## Testing

**Host-buildable:** `programs/paint/` builds standalone with the
existing cross-toolchain (`i686-elf-gcc`/`ld`/`objcopy`), producing a
flat `paint.bin` fixed at `0x00200000`, verified with `objdump`/`nm`
the same way `hello.elf` already is.

**Headless QEMU, screendump-based** (no permanent test payload --
`paint.bin` itself ships as the permanent artifact, so unlike prior
slices there's no throwaway proof to remove afterward):
1. Boot, click the Start Menu, click PAINT -- screendump shows the
   window with an empty canvas, color strip, filename field, and
   SAVE/LOAD buttons.
2. Click a few different swatches and canvas cells -- screendump
   confirms the right cells turn the right colors.
3. Type a filename, click SAVE; modify a cell; click LOAD -- screendump
   confirms the grid reverts to the saved pattern (proving the
   round-trip through real `SYS_CREATE_FILE`/`SYS_READ_FILE` file I/O,
   not just in-memory state).
4. Click the window's close button -- screendump confirms the window
   is gone and the rest of the desktop (drag another window, open
   FILES and browse to `/HOME` to see the saved sprite file) still
   works normally, proving the idle tail loop keeps `kmain_frame()`
   alive.
5. Confirm a normal boot screendump (no PAINT click at all) is
   unchanged from before this change -- the new seeding/menu item are
   true no-ops until clicked.
