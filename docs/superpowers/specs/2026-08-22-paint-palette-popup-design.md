# PAINT's palette-chooser popup: an on-demand 16-color picker

## Purpose

`docs/IDEAS.md`'s "PAINT sprite editor" item flagged the current
8-swatch bottom strip as "too small and always visible, wasting canvas
space," wanting "a real palette-chooser popup/dialog that appears on
demand" and "almost certainly a bigger-than-8 color set once it's not
fighting for screen space" -- plus a separate ask, "need the ability to
delete colours from whatever the palette becomes." This spec covers
both together, since they're coupled: the popup is what makes room for
more colors, and "delete" only makes sense once there's a place to
delete *from*.

Brainstormed via `superpowers:brainstorming`'s architectural path
(clarifying questions on palette source, popup trigger, delete
semantics, and persistence -- see this session's transcript).

## Scope

**In scope**: replacing the always-visible 8-swatch strip with one
small current-color swatch that opens a popup grid on click; growing
the master palette from 8 to 16 colors (the existing 8 unchanged and
index-stable, 8 new ones appended); a hide/restore mechanism for
individual colors, session-only (resets on boot); all click routing
(open, select, hide, restore, dismiss) and rendering for the popup.

**Out of scope, deliberately**: a full custom RGB color picker (sliders
or hex entry) -- the popup only ever picks from the fixed 16-preset
list, no new color values a user can invent. Any change to the sprite
file format, or to how `SAVE`/`LOAD` work -- the master palette's index
meanings never change (colors are hidden, never removed/renumbered),
so an already-saved sprite always renders correctly regardless of what's
currently hidden; `paint_build_sprite_path()`/the 256-raw-bytes format
are untouched. Persisting the hidden set across a reboot, or saving it
alongside a sprite -- resets to all-visible every boot, same "session
state, not disk state" treatment `paint.current_color` already gets.
Click-to-paint on the canvas itself (still `PIXEL`-word-driven only --
a separate, larger open item).

## Design

### Data: two new fields on `struct paint`, not new globals or `window_content` fields

```c
struct paint {
    int grid[PAINT_GRID_SIZE][PAINT_GRID_SIZE];
    int current_color;
    int opened_once;
    int palette_popup_open;        /* new */
    uint32_t palette_hidden_mask;  /* new -- bit i set means color i is hidden */
};
```

`draw_paint_group()` already receives the whole `paint` struct by
pointer (for `current_color`/`grid`), so both new fields ride along for
free -- no changes needed to `struct window_content`,
`move_window_content()`, `draw_window_by_index()`, or any of the three
`window_content` literal call sites. Only `draw_paint_group()`'s body
and `kmain()`'s PAINT click-handling/`touched[WIN_KIND_PAINT]` diff
need updates.

`PAINT_PALETTE_COLORS` becomes 16. The existing 8
(`0x050607`/`0xFFFFFF`/`0xFF3B30`/`0xFF9500`/`0xFFEB3B`/`0x00FF66`/
`0x2979FF`/`0xB026FF`) stay at indices 0-7 unchanged. Eight more are
appended at indices 8-15, in the same vivid/saturated family as the
existing set:

| idx | hex       | name        |
|-----|-----------|-------------|
| 8   | `0x8D6E4C` | brown       |
| 9   | `0xFF4FA3` | pink        |
| 10  | `0x18E0E0` | cyan        |
| 11  | `0x0A6E3D` | dark green  |
| 12  | `0x1A2E8C` | dark blue   |
| 13  | `0x808080` | mid grey    |
| 14  | `0x2B2B2B` | dark grey   |
| 15  | `0xCC3300` | burnt red/orange |

### Layout: the popup overlays the canvas's top-left corner

The current 8-swatch strip (full canvas width, 24px tall, directly
below the canvas) is replaced by one small current-color swatch at the
same position (`canvas_x`, `palette_y`), sized 40x24 -- a clickable
"well" showing what you'll paint with next, always visible. The
existing `paint_palette_hit_test()` (which hit-tests the old 8-cell
strip) is deleted along with the strip-drawing loop it served; two new
hit-test functions replace it -- a plain rect check for the
current-color swatch, and a grid hit-test returning which of the 16
popup cells (or -1) a point falls in, reused for both the left-click
(select) and right-click (toggle-hidden) paths.

The popup itself does **not** appear below that swatch: the window is
already vertically tight (356px tall with only ~8px of slack), and a
140px-tall popup wouldn't fit there without extending past the
taskbar. Instead it overlays the canvas's own top-left corner
(`canvas_x`, `canvas_y`) -- the canvas is 256x256, so a 140x140 popup
temporarily covering part of it while open is unsurprising and matches
how real paint apps float a color picker over the working area.

4x4 grid, 32px swatches with 4px gaps (4*32 + 3*4 = 140 both
dimensions). A 2px accent border around the whole popup, same
`0x00FF66` hard-accent language every other clickable/focused thing in
this UI already uses, so it reads as a floating dialog rather than
part of the canvas. The selected color keeps the existing 2px
top/bottom accent-border marker the old strip already drew for
`current_color`. A hidden color renders dimmed -- each palette RGB
channel halved via `(color >> 1) & 0x7F7F7F`, no blending primitive
needed.

### Interaction

Mirrors FILES' existing `right_held && !prev_right_held` edge pattern
for right-click, and the start menu's own "click outside closes it"
popup convention:

- Popup closed, click lands on the current-color swatch -> open it
  (`palette_popup_open = 1`).
- Popup open, left-click a **visible** grid swatch -> `current_color`
  = that index, close the popup.
- Popup open, left-click a **hidden** grid swatch -> no-op (silent,
  same "no-error-UI" convention as SAVE/LOAD's own failure paths);
  popup stays open.
- Popup open, right-click any grid swatch -> toggle that color's
  hidden bit (`palette_hidden_mask ^= 1u << i`); popup stays open, so
  several colors can be toggled in one visit.
- Popup open, click anywhere else this frame (including re-clicking
  the current-color swatch, which naturally makes it toggle open/closed)
  -> close the popup, `current_color` unchanged.
- A new drag starting on any window (the existing `dragging_window`
  transition from -1 to >=0) force-closes the popup first, rather than
  threading a second movable position through `move_window_content()`.

### `forth.c`'s `PIXEL` bounds check needs to grow too

`prim_pixel()` (`forth/forth.c`) hardcodes `color < 0 || color >= 8` as
its own bounds check on `PIXEL`'s color argument, independent of
`kernel.c`'s `PAINT_PALETTE_COLORS` (the same deliberate
`forth.c`/GUI isolation the original paint spec established -- `forth.c`
doesn't include kernel.c's headers, so it hardcodes its own literal,
same as the `x >= 16`/`y >= 16` checks right next to it already do for
the grid dimensions). Missed, colors 8-15 would exist in the palette
but could never actually be painted via the `PIXEL` word. Changes to
`color >= 16`.

### Redraw tracking

`touched[WIN_KIND_PAINT]`'s existing hand-written diff (already checks
`paint.current_color`, the name field, the save/load buttons) gains two
more terms: `paint.palette_popup_open != old_...` and
`paint.palette_hidden_mask != old_...` -- same shape as every other
term already there, two more `old_paint_*` locals alongside the
existing ones.

Note: this session's LOAD verification pass found that
`touched[WIN_KIND_PAINT]` doesn't track `grid[][]` content changes at
all and may only redraw live today by incidental overlap with another
dirty window (flagged separately in `docs/IDEAS.md`, not fixed here --
out of scope for this feature, and the popup's own state changes are
explicitly tracked above so they're unaffected by that separate gap).

## Testing

`kernel.c`'s GUI pipeline isn't host-buildable, so this follows the
same headless-QEMU convention as every other kernel.c-level feature
this session (LOAD, SHELL RUN): open PAINT, open the popup, confirm
the 4x4 grid renders including the two new rows (indices 8-15);
left-click a new preset (e.g. index 9) and confirm it becomes
`current_color` (verified via `PIXEL`-painting a cell and sampling its
on-screen color, same technique the LOAD verification used);
right-click to hide a color, confirm it dims and a left-click on it no
longer changes `current_color`; right-click again to restore it and
confirm it's selectable again; click outside the popup and confirm it
closes with `current_color` unchanged; start dragging the PAINT window
while the popup is open and confirm it closes.
