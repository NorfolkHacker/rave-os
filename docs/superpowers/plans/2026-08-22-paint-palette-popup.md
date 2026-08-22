# PAINT Palette-Chooser Popup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PAINT's always-visible 8-swatch palette strip with a click-to-open popup showing 16 colors, with a per-color hide/restore mechanism (session-only) standing in for "delete."

**Architecture:** All new state (`palette_popup_open`, `palette_hidden_mask`) lives as two new fields on the existing `struct paint`, so it rides along through the window-content pipeline for free -- no changes to `struct window_content` or any of its three call sites. Rendering and click-routing changes are confined to `draw_paint_group()` and `kmain()`'s existing PAINT click-handling block in `kernel/kernel.c`, plus one bounds-check literal in `kernel/forth/forth.c`.

**Tech Stack:** Freestanding C (`-m32 -ffreestanding`), no libc. `kernel.c`'s GUI pipeline is not host-buildable -- verification is a clean rebuild (compile-time correctness) plus a headless QEMU pass (`-display none` + monitor socket, per `docs/BUILD_LOG.md`'s `[[feedback_qemu_input_testing]]` convention: one command per round-trip, no batching, screendumps sampled with PIL for exact pixel colors).

**Spec:** `docs/superpowers/specs/2026-08-22-paint-palette-popup-design.md`

## Global Constraints

- The existing 8 palette colors (indices 0-7) and their hex values never change -- saved sprites' grid bytes must keep meaning the same thing.
- No changes to the sprite file format, `paint_build_sprite_path()`, or SAVE/LOAD's own logic.
- `palette_popup_open`/`palette_hidden_mask` reset to 0 every boot (session-only state, matches `paint.current_color`'s own treatment) -- no new persistence code.
- Every task must leave the kernel in a clean-building, working state (`make clean && make` with no new warnings) before its commit.

---

### Task 1: Grow the palette to 16 colors, fix `forth.c`'s `PIXEL` bounds check

**Files:**
- Modify: `kernel/kernel.c:342` (`PAINT_PALETTE_COLORS`), `kernel/kernel.c:374-376` (`paint_palette[]`)
- Modify: `kernel/forth/forth.c` (`prim_pixel()`'s bounds check)

**Interfaces:**
- Consumes: nothing new.
- Produces: `PAINT_PALETTE_COLORS` = 16 and a 16-entry `paint_palette[]`, which Task 2/3 build the popup grid on top of. `prim_pixel()` now accepts color arguments 0-15.

- [ ] **Step 1: Grow `PAINT_PALETTE_COLORS` and `paint_palette[]`**

In `kernel/kernel.c`, change:
```c
#define PAINT_PALETTE_COLORS 8
```
to:
```c
#define PAINT_PALETTE_COLORS 16
```

And change:
```c
static const uint32_t paint_palette[PAINT_PALETTE_COLORS] = {
    0x050607, 0xFFFFFF, 0xFF3B30, 0xFF9500, 0xFFEB3B, 0x00FF66, 0x2979FF, 0xB026FF,
};
```
to:
```c
static const uint32_t paint_palette[PAINT_PALETTE_COLORS] = {
    0x050607, 0xFFFFFF, 0xFF3B30, 0xFF9500, 0xFFEB3B, 0x00FF66, 0x2979FF, 0xB026FF,
    0x8D6E4C, 0xFF4FA3, 0x18E0E0, 0x0A6E3D, 0x1A2E8C, 0x808080, 0x2B2B2B, 0xCC3300,
};
```
(indices 8-15: brown, pink, cyan, dark green, dark blue, mid grey, dark grey, burnt red/orange -- see the design spec's table for the reasoning.)

The existing strip-drawing loop in `draw_paint_group()` and `paint_palette_hit_test()` both already use `PAINT_PALETTE_COLORS` dynamically (not a hardcoded `8`), so at this point in the plan the *old* strip UI keeps working, just with 16 narrower swatches instead of 8 wider ones -- this task is independently testable without needing Task 2's UI replacement yet.

- [ ] **Step 2: Fix `forth.c`'s `PIXEL` bounds check**

In `kernel/forth/forth.c`, find `prim_pixel()`:
```c
static void prim_pixel(struct forth_vm *vm) {
    int32_t x, y, color;
    if (!forth_pop(vm, &color) || !forth_pop(vm, &y) || !forth_pop(vm, &x)) {
        return;
    }
    if (x < 0 || x >= 16 || y < 0 || y >= 16 || color < 0 || color >= 8) {
        forth_set_error(vm, "BAD PIXEL");
        return;
    }
    forth_hook_pixel((int)x, (int)y, (int)color);
}
```
Change `color >= 8` to `color >= 16` (this is a hardcoded literal, deliberately independent of `kernel.c`'s `PAINT_PALETTE_COLORS` -- `forth.c` doesn't include `kernel.c`'s headers, same as the `x >= 16`/`y >= 16` grid-dimension checks right next to it).

- [ ] **Step 3: Build**

```bash
cd kernel && make clean && make
cd ../boot && make disk.img
```
Expect a clean build (only the pre-existing `ld: warning: kernel.elf has a LOAD segment with RWX permissions`, no new warnings or errors).

- [ ] **Step 4: Headless QEMU verification**

Boot headlessly (`-display none`, monitor socket, per `[[feedback_qemu_input_testing]]`: one command per round-trip). Open the start menu, click FORTH, click into its console input, type `paint` + Enter (opens the PAINT window via the `PAINT` Forth word), then type `8 8 9 pixel` + Enter -- this paints grid cell (8,8) with color index 9 (pink, `0xFF4FA3`), which only exists because of this task's changes (color 9 didn't exist before Step 1, and `prim_pixel()` would have rejected it with `BAD PIXEL` before Step 2). Screendump and sample the pixel at the center of cell (8,8) -- PAINT's canvas starts at `(win.x+8, win.y+8)` = `(348, 88)` at PAINT's default position (`win.x=340, win.y=80`), each cell is `PAINT_CELL_PX` (16px), so cell (8,8)'s center is at `(348 + 8*16 + 8, 88 + 8*16 + 8)` = `(484, 224)`. Confirm it reads as RGB `(255, 79, 163)` (`0xFF4FA3`), not the near-black default (`0x050607` = `(5, 6, 7)`) or a "BAD PIXEL" no-op.

- [ ] **Step 5: Commit**

```bash
git add kernel/kernel.c kernel/forth/forth.c
git commit -m "kernel: grow PAINT's palette to 16 colors, fix PIXEL's bounds check"
```

---

### Task 2: Replace the fixed strip with a click-to-open popup (no hide/restore yet)

**Files:**
- Modify: `kernel/kernel.c` -- `draw_paint_group()`, `paint_palette_hit_test()` (deleted, replaced by two new functions), the PAINT palette click-handling block in `kmain()`, the drag-start block in `kmain()`, `touched[WIN_KIND_PAINT]`'s diff and its `old_paint_*` locals.

**Interfaces:**
- Consumes: `PAINT_PALETTE_COLORS` (16, from Task 1), `paint_palette[]` (16 entries, from Task 1).
- Produces: `struct paint` gains `int palette_popup_open;` and `uint32_t palette_hidden_mask;` (the second unused by any read this task, but present so Task 3 only adds behavior, not the field). `paint_swatch_hit_test()` and `paint_popup_grid_hit_test()`, new functions Task 3 also calls.

- [ ] **Step 1: Add the two new fields to `struct paint`**

```c
struct paint {
    int grid[PAINT_GRID_SIZE][PAINT_GRID_SIZE]; /* palette index 0..15 per cell, row-major */
    int current_color;                          /* natively-selected palette swatch, 0..15 */
    int opened_once;                             /* clears grid to all-zero only the first time PAINT ever opens */
    int palette_popup_open;                      /* whether the palette-chooser popup is showing */
    uint32_t palette_hidden_mask;                /* bit i set means color i is hidden from selection (Task 3) */
};
```
(also update the two `0..7` comments on `grid`/`current_color` to `0..15`, since the palette grew in Task 1). `static struct paint paint;` is file-scope static storage, so both new fields are zero-initialized automatically -- no init code needed anywhere.

- [ ] **Step 2: Add the new layout constants**

Right after `#define PAINT_PALETTE_COLORS 16`:
```c
#define PAINT_SWATCH_W 40
#define PAINT_SWATCH_H 24
#define PAINT_POPUP_COLS 4
#define PAINT_POPUP_SWATCH 32
#define PAINT_POPUP_GAP 4
#define PAINT_POPUP_SIZE (PAINT_POPUP_COLS * PAINT_POPUP_SWATCH + (PAINT_POPUP_COLS - 1) * PAINT_POPUP_GAP)
```
(`PAINT_POPUP_SIZE` = 4*32 + 3*4 = 140, both dimensions of the 4x4 grid.)

- [ ] **Step 3: Replace `draw_paint_group()`'s strip-drawing with the swatch + popup**

Current code (the whole function):
```c
static void draw_paint_group(const struct window *win, const struct paint *pt, const struct console_input *name_input,
                             const struct button *save_btn, const struct button *load_btn) {
    int row, col, i;
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;
    int swatch_w = (PAINT_GRID_SIZE * PAINT_CELL_PX) / PAINT_PALETTE_COLORS;

    window_draw(win);

    for (row = 0; row < PAINT_GRID_SIZE; row++) {
        for (col = 0; col < PAINT_GRID_SIZE; col++) {
            gfx_fill_rect(canvas_x + col * PAINT_CELL_PX, canvas_y + row * PAINT_CELL_PX, PAINT_CELL_PX,
                         PAINT_CELL_PX, (uint32_t)paint_palette[pt->grid[row][col]]);
        }
    }

    for (i = 0; i < PAINT_PALETTE_COLORS; i++) {
        int sx = canvas_x + i * swatch_w;
        gfx_fill_rect(sx, palette_y, swatch_w, 24, paint_palette[i]);
        if (i == pt->current_color) {
            gfx_fill_rect(sx, palette_y, swatch_w, 2, 0x00FF66);
            gfx_fill_rect(sx, palette_y + 22, swatch_w, 2, 0x00FF66);
        }
    }

    console_input_draw(name_input);
    button_draw(save_btn);
    button_draw(load_btn);
}
```

Replace with:
```c
static void draw_paint_group(const struct window *win, const struct paint *pt, const struct console_input *name_input,
                             const struct button *save_btn, const struct button *load_btn) {
    int row, col;
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;

    window_draw(win);

    for (row = 0; row < PAINT_GRID_SIZE; row++) {
        for (col = 0; col < PAINT_GRID_SIZE; col++) {
            gfx_fill_rect(canvas_x + col * PAINT_CELL_PX, canvas_y + row * PAINT_CELL_PX, PAINT_CELL_PX,
                         PAINT_CELL_PX, (uint32_t)paint_palette[pt->grid[row][col]]);
        }
    }

    /* Current-color "well" -- always visible, shows what PIXEL paints
     * with next. Clicking it opens the popup grid (paint_swatch_hit_test()/
     * paint_popup_grid_hit_test()); the 1px accent border (same
     * border-rect-behind-a-smaller-fill-rect technique
     * console_input_draw() already uses) marks it as clickable. */
    gfx_fill_rect(canvas_x - 1, palette_y - 1, PAINT_SWATCH_W + 2, PAINT_SWATCH_H + 2, 0x00FF66);
    gfx_fill_rect(canvas_x, palette_y, PAINT_SWATCH_W, PAINT_SWATCH_H, paint_palette[pt->current_color]);

    if (pt->palette_popup_open) {
        int pi;

        /* Overlays the canvas's own top-left corner rather than
         * appearing below the swatch -- the window has no vertical
         * room left below the swatch row (see the design spec), so a
         * 140x140 popup temporarily covers part of the canvas while
         * open, same as any floating color-picker dialog would. */
        gfx_fill_rect(canvas_x - 2, canvas_y - 2, PAINT_POPUP_SIZE + 4, PAINT_POPUP_SIZE + 4, 0x00FF66);
        gfx_fill_rect(canvas_x, canvas_y, PAINT_POPUP_SIZE, PAINT_POPUP_SIZE, 0x0B1712);

        for (pi = 0; pi < PAINT_PALETTE_COLORS; pi++) {
            int pcol = pi % PAINT_POPUP_COLS;
            int prow = pi / PAINT_POPUP_COLS;
            int sx = canvas_x + pcol * (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
            int sy = canvas_y + prow * (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);

            gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, PAINT_POPUP_SWATCH, paint_palette[pi]);
            if (pi == pt->current_color) {
                gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, 2, 0x00FF66);
                gfx_fill_rect(sx, sy + PAINT_POPUP_SWATCH - 2, PAINT_POPUP_SWATCH, 2, 0x00FF66);
            }
        }
    }

    console_input_draw(name_input);
    button_draw(save_btn);
    button_draw(load_btn);
}
```
(`0x0B1712` matches `window.c`'s own `WINDOW_BODY_COLOR` literal -- duplicated rather than shared, same "each widget file owns its own copies" convention `startmenu.c`'s own comment already documents.)

- [ ] **Step 4: Replace `paint_palette_hit_test()` with two new hit-test functions**

Delete:
```c
/* Converts a click position into a palette swatch index (0..7), or -1
 * if the click missed the palette strip entirely. Mirrors
 * files_list_hit_test()'s own "convert a click into a logical index"
 * shape. */
static int paint_palette_hit_test(const struct window *win, int px, int py) {
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;
    int swatch_w = (PAINT_GRID_SIZE * PAINT_CELL_PX) / PAINT_PALETTE_COLORS;
    int canvas_x = win->x + 8;
    int idx;

    if (py < palette_y || py >= palette_y + 24) {
        return -1;
    }
    idx = (px - canvas_x) / swatch_w;
    if (idx < 0 || idx >= PAINT_PALETTE_COLORS) {
        return -1;
    }
    return idx;
}
```

Replace with:
```c
/* Hit-tests the always-visible current-color swatch -- clicking it
 * opens the popup grid (paint_popup_grid_hit_test()). */
static int paint_swatch_hit_test(const struct window *win, int px, int py) {
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;
    return px >= canvas_x && px < canvas_x + PAINT_SWATCH_W && py >= palette_y && py < palette_y + PAINT_SWATCH_H;
}

/* Converts a click position into a popup swatch index (0..15), or -1
 * if the click missed the grid entirely. Mirrors
 * files_list_hit_test()'s own "convert a click into a logical index"
 * shape -- same spirit the old paint_palette_hit_test() used for the
 * strip it replaced. */
static int paint_popup_grid_hit_test(const struct window *win, int px, int py) {
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int col, row;

    if (px < canvas_x || px >= canvas_x + PAINT_POPUP_SIZE || py < canvas_y || py >= canvas_y + PAINT_POPUP_SIZE) {
        return -1;
    }
    /* The bounds check above already guarantees col/row land in
     * [0, PAINT_POPUP_COLS) -- PAINT_POPUP_SIZE is exactly
     * PAINT_POPUP_COLS swatches plus the gaps between them, no
     * trailing gap past the last column. A click inside a gap between
     * swatches is attributed to the swatch just before it (integer
     * division) -- same loose tolerance files_list_hit_test() already
     * uses for its own row bands. */
    col = (px - canvas_x) / (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
    row = (py - canvas_y) / (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
    return row * PAINT_POPUP_COLS + col;
}
```

Also update `paint_mouse_cell()`'s own comment two functions above (currently says `"same 8px-margin offset draw_paint_group()/paint_palette_hit_test() already use"`) to say `paint_swatch_hit_test()` instead, since the function it referenced no longer exists.

- [ ] **Step 5: Replace the palette click-handling block in `kmain()`**

Current code:
```c
                /* Clicking a palette swatch selects it -- no
                 * confirmation, no separate "apply" step, matching
                 * this window's otherwise all-immediate click
                 * semantics. */
                if (paint_is_topmost && click_edge) {
                    int swatch = paint_palette_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                    if (swatch >= 0) {
                        paint.current_color = swatch;
                    }
                }
```

Replace with:
```c
                /* Click routing for the palette-chooser popup: closed
                 * + click on the swatch opens it; open + click on a
                 * grid swatch selects it and closes the popup; open +
                 * click on any *other* window (or the backdrop) closes
                 * it without changing current_color, same "click
                 * outside dismisses" convention the start menu's own
                 * popup already uses. (Task 3 adds right-click
                 * hide/restore and the "hidden swatches are a no-op"
                 * rule to the grid-click branch below.) */
                if (click_edge) {
                    if (paint_is_topmost) {
                        if (!paint.palette_popup_open) {
                            if (paint_swatch_hit_test(&windows[WIN_KIND_PAINT], cx, cy)) {
                                paint.palette_popup_open = 1;
                            }
                        } else {
                            int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                            if (swatch >= 0) {
                                paint.current_color = swatch;
                            }
                            paint.palette_popup_open = 0;
                        }
                    } else if (paint.palette_popup_open) {
                        paint.palette_popup_open = 0;
                    }
                }
```

- [ ] **Step 6: Force-close the popup when a new drag starts**

Find (in the mouse-button-press handling, where a titlebar hit starts a drag):
```c
                    } else {
                        raise_window(z_order, target);
                        if (window_titlebar_hit_test(&windows[target], cx, cy)) {
                            dragging_window = target;
                        }
                    }
```
Change to:
```c
                    } else {
                        raise_window(z_order, target);
                        if (window_titlebar_hit_test(&windows[target], cx, cy)) {
                            dragging_window = target;
                            /* Dragging any window while PAINT's popup is
                             * open would leave it rendered at a stale
                             * position relative to a window that just
                             * moved -- simplest fix is closing it
                             * outright rather than threading a second
                             * movable position through
                             * move_window_content(). */
                            paint.palette_popup_open = 0;
                        }
                    }
```

- [ ] **Step 7: Track the popup's open state in `touched[WIN_KIND_PAINT]`**

Add one more `old_paint_*` local next to the existing ones (near `old_paint_load_btn_pressed`):
```c
        int old_paint_palette_popup_open = paint.palette_popup_open;
```

And fold it into the existing OR-chain:
```c
            touched[WIN_KIND_PAINT] = touched[WIN_KIND_PAINT] || (paint.current_color != old_paint_current_color) ||
                                      (paint_name_input.len != old_paint_name_input_len) ||
                                      (paint_name_input.cursor != old_paint_name_input_cursor) ||
                                      (paint_name_input.focused != old_paint_name_input_focused) ||
                                      !str_eq(paint_name_input.text, old_paint_name_input_text) ||
                                      (paint_save_btn.hovered != old_paint_save_btn_hovered) ||
                                      (paint_save_btn.pressed != old_paint_save_btn_pressed) ||
                                      (paint_load_btn.hovered != old_paint_load_btn_hovered) ||
                                      (paint_load_btn.pressed != old_paint_load_btn_pressed) ||
                                      (paint.palette_popup_open != old_paint_palette_popup_open);
```

- [ ] **Step 8: Build**

```bash
cd kernel && make clean && make
cd ../boot && make disk.img
```
Expect a clean build, same as Task 1.

- [ ] **Step 9: Headless QEMU verification**

Boot headlessly, open PAINT the same way as Task 1 (start menu -> FORTH -> `paint` + Enter). Confirm via screendump that the old 16-wide strip is gone, replaced by one small swatch at `(canvas_x, palette_y)` = `(348, 348)` at PAINT's default position, showing the default color (index 0, near-black `0x050607`).

Click that swatch and confirm (via screendump) the 4x4 popup grid renders over the canvas's top-left corner, starting at `(canvas_x, canvas_y)` = `(348, 88)`, each swatch `PAINT_POPUP_SWATCH` (32px) with `PAINT_POPUP_GAP` (4px) between them -- sample a couple of known cells to confirm the right colors landed in the right grid position (e.g. index 1, at grid column 1 row 0, should read white `0xFFFFFF`; index 9, at column 1 row 2, should read pink `0xFF4FA3`).

Click swatch index 9 in the grid and confirm: the popup disappears (the area it covered now shows canvas content again), and the small current-color swatch now shows pink.

Re-open the popup, then click somewhere clearly outside both the popup and the swatch -- e.g. empty backdrop to the left of both windows -- and confirm the popup closes with the current-color swatch still pink (unchanged).

Re-open the popup once more, then start dragging the PAINT window by its titlebar (`window_titlebar_hit_test()`'s region, above `canvas_y`), and confirm the popup is gone immediately (no grid rendered at the new or old canvas position).

- [ ] **Step 10: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: PAINT's palette strip becomes a click-to-open popup"
```

---

### Task 3: Right-click hide/restore, dimmed rendering, no-op on hidden select

**Files:**
- Modify: `kernel/kernel.c` -- `draw_paint_group()` (dim hidden swatches), the grid-click branch added in Task 2 (skip hidden colors, add right-click), `touched[WIN_KIND_PAINT]`'s diff and its `old_paint_*` locals.

**Interfaces:**
- Consumes: `paint.palette_hidden_mask` (added in Task 2, unused until now), `paint_popup_grid_hit_test()` (from Task 2).
- Produces: nothing new for later tasks -- this is the last sub-project of `docs/IDEAS.md`'s "PAINT sprite editor" item.

- [ ] **Step 1: Dim hidden swatches in `draw_paint_group()`**

Inside the `if (pt->palette_popup_open)` block's `for` loop (added in Task 2), change:
```c
            gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, PAINT_POPUP_SWATCH, paint_palette[pi]);
            if (pi == pt->current_color) {
```
to:
```c
            uint32_t swatch_color = paint_palette[pi];
            if (pt->palette_hidden_mask & (1u << pi)) {
                /* Halves each RGB channel -- no blending primitive
                 * needed, just a bit-shift, enough to read as "dimmed"
                 * against the popup's dark backing. */
                swatch_color = (swatch_color >> 1) & 0x7F7F7F;
            }
            gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, PAINT_POPUP_SWATCH, swatch_color);
            if (pi == pt->current_color) {
```
(the existing `if (pi == pt->current_color) { ... }` selected-marker block right after stays exactly as-is.)

- [ ] **Step 2: Add right-click hide/restore, and skip hidden colors on left-click select**

Task 2 left this block in `kmain()`:
```c
                        } else {
                            int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                            if (swatch >= 0) {
                                paint.current_color = swatch;
                            }
                            paint.palette_popup_open = 0;
                        }
```
Replace with:
```c
                        } else {
                            int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                            /* A hidden swatch can't be selected -- same
                             * "no-error-UI, silent no-op" convention
                             * SAVE/LOAD's own failure paths already use
                             * -- but the popup stays open either way,
                             * so a miss or a hidden pick doesn't force
                             * the user to reopen it. */
                            if (swatch >= 0 && !(paint.palette_hidden_mask & (1u << swatch))) {
                                paint.current_color = swatch;
                                paint.palette_popup_open = 0;
                            } else if (swatch < 0) {
                                paint.palette_popup_open = 0;
                            }
                        }
```
(Note the behavior change from Task 2: a hidden-swatch click no longer closes the popup, since there's now something meaningful left to do there -- pick a different, visible color, or right-click to restore it. A miss within the popup's own bounds -- landing in a gap -- still closes it, same as before, since `paint_popup_grid_hit_test()` always returns a valid index for any in-bounds click per Task 2's own reasoning, so `swatch < 0` here only fires for clicks that fell through from the outer `else` -- this branch is reachable only when `paint.palette_popup_open` was already true, meaning `swatch` is always `>= 0` in practice; kept for defensive clarity, matching this codebase's existing style of an explicit check even where current geometry makes it unreachable.)

Immediately after that whole `if (click_edge) { ... }` block (the one Task 2 added, now modified above), add the right-click handling as its own statement:
```c
                if (paint_is_topmost && paint.palette_popup_open && right_held && !prev_right_held) {
                    int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                    if (swatch >= 0) {
                        paint.palette_hidden_mask ^= 1u << swatch;
                    }
                }
```
(mirrors FILES' own `right_held && !prev_right_held` edge-detection, already used elsewhere in this same function for right-click row-select.)

- [ ] **Step 3: Track the hidden mask in `touched[WIN_KIND_PAINT]`**

Add one more local next to `old_paint_palette_popup_open`:
```c
        uint32_t old_paint_palette_hidden_mask = paint.palette_hidden_mask;
```
And replace the whole `touched[WIN_KIND_PAINT] = ...;` statement (Task 2
left it ending in `(paint.palette_popup_open != old_paint_palette_popup_open);`)
with:
```c
            touched[WIN_KIND_PAINT] = touched[WIN_KIND_PAINT] || (paint.current_color != old_paint_current_color) ||
                                      (paint_name_input.len != old_paint_name_input_len) ||
                                      (paint_name_input.cursor != old_paint_name_input_cursor) ||
                                      (paint_name_input.focused != old_paint_name_input_focused) ||
                                      !str_eq(paint_name_input.text, old_paint_name_input_text) ||
                                      (paint_save_btn.hovered != old_paint_save_btn_hovered) ||
                                      (paint_save_btn.pressed != old_paint_save_btn_pressed) ||
                                      (paint_load_btn.hovered != old_paint_load_btn_hovered) ||
                                      (paint_load_btn.pressed != old_paint_load_btn_pressed) ||
                                      (paint.palette_popup_open != old_paint_palette_popup_open) ||
                                      (paint.palette_hidden_mask != old_paint_palette_hidden_mask);
```

- [ ] **Step 4: Build**

```bash
cd kernel && make clean && make
cd ../boot && make disk.img
```
Expect a clean build.

- [ ] **Step 5: Headless QEMU verification**

Boot headlessly, open PAINT, open the popup (same as Task 2's verification). Right-click swatch index 3 (orange, `0xFF9500`) and confirm via screendump it now renders dimmed -- sample its center pixel and confirm it reads close to `(127, 74, 0)` (`0xFF9500 >> 1 & 0x7F7F7F` = `0x7F4A00`), not the full-brightness `(255, 149, 0)`.

Left-click that same dimmed swatch and confirm: the current-color swatch does *not* change to orange (still whatever it was selected as before), and the popup stays open (still rendered).

Right-click it again and confirm it's restored to full brightness (`(255, 149, 0)`).

Left-click it now and confirm it selects successfully -- current-color swatch turns orange, popup closes.

- [ ] **Step 6: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: PAINT palette popup gets hide/restore (right-click)"
```

---

### Task 4: Update `docs/IDEAS.md` and `docs/BUILD_LOG.md`

**Files:**
- Modify: `docs/IDEAS.md` (mark the palette-chooser/delete-colors sub-items done)
- Modify: `docs/BUILD_LOG.md` (new entry)

**Interfaces:** None -- documentation only.

- [ ] **Step 1: Mark the whole "PAINT sprite editor" item done in `docs/IDEAS.md`**

Find this entire block (the parent bullet plus all four of its sub-bullets):
```
- **PAINT still needs to become an actual sprite editor, not just a
  proof of concept.** Raised 2026-08-17, alongside the real-hardware
  bug the 2026-08-18 pass fixed (see above) -- these are the parts of
  that same report that weren't about the bug itself and are still
  open:
  - ~~**Should launch from SHELL, not just via `RUN PAINT` typed into a
    FORTH console window.**~~ Done, 2026-08-22 -- SHELL now intercepts
    `RUN <target>` the same way it already intercepted `EDIT`, sharing
    a new `handle_run_command()` with the FORTH console instead of
    duplicating the launch logic. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - **The 8-color bottom-strip palette is too small and always visible,
    wasting canvas space.** Wanted instead: a real palette-chooser
    popup/dialog that appears on demand, not a fixed strip -- almost
    certainly needs a bigger-than-8 color set once it's not fighting for
    screen space.
  - **Need the ability to delete colours** from whatever the palette
    becomes -- not just pick from a fixed set.
  - ~~**Needs LOAD, not just SAVE.**~~ Done, 2026-08-22 -- a LOAD button
    sits beside SAVE (same filename field, split row, no window
    resize), reads `/HOME/<name>` back into the grid only if it's
    exactly 256 bytes, and clamps each byte to a valid palette index in
    case of a bad/hand-edited file. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - This adds up to substantially more than a bug-fix pass -- likely its
    own `superpowers:brainstorming` cycle (palette-chooser UI is a real
    design question, not just an implementation detail) rather than a
    quick patch to the existing plan.
```
Replace it with (all four sub-projects now done, so the whole item is struck through; the two now-completed sub-bullets get their own `Done` lines matching the other two's existing convention; the "adds up to..." bullet is dropped entirely since it was only explaining why the now-finished work needed its own brainstorming cycle, not a standing task):
```
- ~~**PAINT still needs to become an actual sprite editor, not just a
  proof of concept.**~~ Done, 2026-08-22 -- all four sub-projects raised
  2026-08-17 landed the same day (2026-08-22): SHELL launch, LOAD,
  and finally the palette-chooser popup with delete-colors below.
  - ~~**Should launch from SHELL, not just via `RUN PAINT` typed into a
    FORTH console window.**~~ Done, 2026-08-22 -- SHELL now intercepts
    `RUN <target>` the same way it already intercepted `EDIT`, sharing
    a new `handle_run_command()` with the FORTH console instead of
    duplicating the launch logic. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - ~~**The 8-color bottom-strip palette is too small and always
    visible, wasting canvas space.**~~ Done, 2026-08-22 -- replaced by a
    click-to-open popup showing 16 colors (up from 8), overlaying the
    canvas's own top-left corner. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - ~~**Need the ability to delete colours** from whatever the palette
    becomes -- not just pick from a fixed set.~~ Done, 2026-08-22 --
    right-click any popup swatch to hide/restore it (session-only; the
    master 16-color list and saved sprites' own meaning never change).
  - ~~**Needs LOAD, not just SAVE.**~~ Done, 2026-08-22 -- a LOAD button
    sits beside SAVE (same filename field, split row, no window
    resize), reads `/HOME/<name>` back into the grid only if it's
    exactly 256 bytes, and clamps each byte to a valid palette index in
    case of a bad/hand-edited file. See `docs/BUILD_LOG.md`'s entry for
    the same date.
```

- [ ] **Step 2: Add the `docs/BUILD_LOG.md` entry**

Append (matching this session's own established entry format -- see its last few entries for the exact prose/heading style):
```markdown
## 2026-08-22 -- PAINT's palette-chooser popup, closing out the sprite-editor item

Final sub-project of `docs/IDEAS.md`'s "PAINT sprite editor" item (SHELL
launch and LOAD landed earlier the same day). Full design in
`docs/superpowers/specs/2026-08-22-paint-palette-popup-design.md`,
brainstormed via `superpowers:brainstorming`'s architectural path.

Grew the master palette from 8 to 16 colors (the original 8
index-stable, so SAVE/LOAD's file format needed zero changes) and
replaced the always-visible strip with a small current-color swatch
that opens a 4x4 popup grid on click, overlaying the canvas's own
top-left corner. Both new pieces of state (`palette_popup_open`,
`palette_hidden_mask`) live as fields on `struct paint` itself, so
they ride through the window-content pipeline for free -- zero changes
to `struct window_content` or its three call sites.

A per-color hide/restore mechanism (right-click any popup swatch)
stands in for "delete": colors are hidden, never removed or
renumbered, so nothing already painted or previously saved can be
silently reinterpreted by a later palette edit -- the master list's
shape never changes, only what's currently offered for *new* picks.
Session-only, resets every boot, same treatment `current_color` itself
already got.

Also fixed a latent gap this surfaced: `forth.c`'s `PIXEL` word
hardcoded its own `color >= 8` bounds check, independent of `kernel.c`'s
`PAINT_PALETTE_COLORS` -- missed, the 8 new colors would have existed
in the palette but been unpaintable via Forth.

Verified via a headless QEMU pass across all three implementation
tasks: `PIXEL`-painting with a new color index (9, pink); opening the
popup and confirming the 4x4 grid renders the right colors in the
right positions; selecting a new color and confirming the popup
closes; clicking outside the popup and confirming it closes without
changing the selection; dragging the window while the popup is open
and confirming it closes; hiding a color (dims, becomes unselectable)
and restoring it (full brightness, selectable again) -- all via direct
pixel-color sampling of screendumps, not just visual inspection.

Files: `kernel/kernel.c` (`PAINT_PALETTE_COLORS`/`paint_palette[]`
grown; `struct paint` gains two fields; `draw_paint_group()`,
`paint_swatch_hit_test()`/`paint_popup_grid_hit_test()` -- replacing
`paint_palette_hit_test()` -- and the PAINT click-handling/`touched[]`
diff in `kmain()` all updated); `kernel/forth/forth.c` (`prim_pixel()`'s
bounds check).
```

- [ ] **Step 3: Commit**

```bash
git add docs/IDEAS.md docs/BUILD_LOG.md
git commit -m "docs: close out PAINT sprite-editor item -- palette popup + delete-colors done"
```
