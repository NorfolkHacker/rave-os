# Persisting ring-3 window content across unrelated redraws

## Purpose

`docs/superpowers/specs/2026-08-29-ring3-window-events-design.md` shipped
real event delivery for the ring-3 window, but documented a real,
unsolved limitation found while verifying it: the window's *content*
area (whatever the ring-3 program drew via the gfx syscalls) reverts to
plain backdrop whenever the window gets swept into a redraw it didn't
cause -- confirmed to happen on essentially every click, since each one
triggers its own redraw pass. This spec fixes that.

Investigated first: could this be fixed by excluding the ring-3
window's body from the shared backdrop-repaint loop, leaving whatever
pixels were already there untouched? No -- read `window_draw()`
(`kernel/gui/window.c`) directly rather than assumed, and found it
draws the window's *entire* silhouette (border + titlebar + body) as
one filled rounded rect in the window's accent color first, then paints
the titlebar and body on top of that in their own colors. Any redraw of
the window's chrome -- which is still necessary, since the chrome
itself is a real, kernel-owned thing that must survive redraws too --
unavoidably repaints over the *entire* window including the body,
regardless of whether the body-fill step specifically is skipped. There
is no way to redraw only "the border and titlebar, definitely not the
body" without either restructuring `window_draw()`'s own drawing order
(a shared function five other window kinds also depend on, not worth
risking for this one kind's sake) or -- the approach this spec takes --
having something else restore the body's correct content immediately
after.

## Scope

**In scope:**
- A persistent shadow buffer, sized to the fixed 640x480 resolution
  this codebase already commits to (see `README.md`'s own "one fixed
  resolution" limitation -- not a new constraint), that mirrors every
  pixel a ring-3 program's own `SYS_GFX_PUT_PIXEL`/`SYS_GFX_FILL_RECT`
  call writes. Safe to key off "any syscall_gfx.c write" without
  checking window ownership or bounds against the ring-3 window
  specifically: by construction, these syscalls are only ever callable
  from ring-3 code, so anything written through them *is* the ring-3
  program's own content, whether or not a window happens to be open at
  that exact moment.
- Restoring that content: `draw_window_by_index()`'s `WIN_KIND_RING3`
  case blits the shadow buffer's content for the window's current body
  rect back onto the real backbuffer immediately after redrawing
  chrome (`window_draw()`), so whatever was last drawn survives
  regardless of why the redraw happened.
- Pre-filling the shadow buffer's body-sized region with the same
  `WINDOW_BODY_COLOR` `window_draw()` itself paints, at
  `SYS_WINDOW_OPEN` time (via a new small getter, `window_body_color()`,
  added to `window.h`/`window.c` rather than duplicating that color
  value in `kernel.c`) -- so the body looks correct even before the
  ring-3 program has drawn its first pixel, not black.

**Out of scope, deliberately:**
- **Any change to `window_draw()` itself, or to how the other five
  window kinds redraw.** This is purely additive: a new getter
  function on `window.h`'s existing module, called from exactly one
  new call site.
- **Bounds/bookkeeping for more than one ring-3 window, or a window
  that moves.** There is still only one `WIN_KIND_RING3` slot, and
  dragging it (already generically possible, per the events slice's
  own findings) is not addressed here -- the shadow buffer is a flat
  640x480 mirror of screen-space writes, not aware of the window's
  position changing after the fact. A dragged ring-3 window's content
  would still be restored at its *original* screen coordinates, not
  wherever it was dragged to; fixing that is separate future work
  alongside real drag-content persistence for this window kind.
- **Any validation of the coordinates a ring-3 program passes to the
  gfx syscalls.** Same stance every prior syscall sub-project has
  taken -- the shadow buffer's own mirror functions clamp/ignore
  out-of-bounds writes purely for memory safety (a fixed 640x480
  buffer, no dynamic sizing), not as a new validation feature.
- **Any attempt to distinguish "the ring-3 program hasn't drawn here
  yet" from "the ring-3 program deliberately drew this color."**
  The shadow buffer is a plain color mirror; the pre-fill at open time
  covers the one gap that would otherwise be visible (a body showing
  black before anything is drawn).

## Design

### The shadow buffer (`kernel/kernel.c`)

```c
/* Mirrors every pixel a ring-3 program's own gfx syscalls write --
 * safe to key off "any syscall_gfx.c write" without checking window
 * ownership, since these syscalls are only ever callable from ring-3
 * code (see docs/superpowers/specs/2026-08-29-ring3-window-content-
 * persistence-design.md). Sized to the fixed 640x480 resolution this
 * codebase already commits to (README.md's own "one fixed resolution"
 * limitation), not dynamically -- one flat buffer, no per-window
 * bookkeeping, since there is still only one ring-3 window slot. */
#define RING3_SHADOW_W 640
#define RING3_SHADOW_H 480
static uint32_t ring3_shadow[RING3_SHADOW_W * RING3_SHADOW_H];

void ring3_shadow_put_pixel(int x, int y, uint32_t rgb) {
    if (x < 0 || x >= RING3_SHADOW_W || y < 0 || y >= RING3_SHADOW_H) {
        return;
    }
    ring3_shadow[y * RING3_SHADOW_W + x] = rgb;
}

void ring3_shadow_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    int px, py;
    for (py = y; py < y + h; py++) {
        for (px = x; px < x + w; px++) {
            ring3_shadow_put_pixel(px, py, rgb);
        }
    }
}
```

### Mirroring writes (`kernel/arch/syscall_gfx.c`)

```c
extern void ring3_shadow_put_pixel(int x, int y, unsigned int rgb);
extern void ring3_shadow_fill_rect(int x, int y, int w, int h, unsigned int rgb);

/* inside syscall_dispatch_gfx(), alongside the existing calls: */
if (num == SYS_GFX_PUT_PIXEL) {
    const struct sys_gfx_put_pixel_args *a = (const struct sys_gfx_put_pixel_args *)arg;
    gfx_put_pixel(a->x, a->y, a->rgb);
    ring3_shadow_put_pixel(a->x, a->y, a->rgb);
    return 0;
}
if (num == SYS_GFX_FILL_RECT) {
    const struct sys_gfx_fill_rect_args *a = (const struct sys_gfx_fill_rect_args *)arg;
    gfx_fill_rect(a->x, a->y, a->w, a->h, a->rgb);
    ring3_shadow_fill_rect(a->x, a->y, a->w, a->h, a->rgb);
    return 0;
}
```

`SYS_GFX_CLEAR` deliberately does not mirror -- it fills the *entire*
backbuffer, not something scoped to a window; a ring-3 program clearing
the whole screen is already explicitly out of any window's business per
the original gfx slice's own "no ownership" stance, and mirroring a
640x480 fill into the shadow buffer on every call would be needless
work for a syscall no proof payload has exercised this way.

### Accepted cosmetic tradeoff: square corners after a blit-back

`window_draw()`'s body-fill rounds its bottom two corners
(`WINDOW_INNER_RADIUS`, 6px). The rectangular blit-back below paints
every pixel in the body's bounding rect, including the small triangular
areas outside that rounding that should show the accent-colored border
underneath instead. Confined to a ~6x6px region in two corners, and
only visible right after a redraw this slice's own fix now makes
survivable in the first place (previously the whole window vanished,
which is worse) -- accepted as a minor, documented tradeoff rather than
duplicating `gfx_fill_rounded_rect_ex()`'s own circle-equation logic
here for a few pixels' sake.

### Restoring content (`kernel/kernel.c`'s `draw_window_by_index()`)

```c
} else if (idx == WIN_KIND_RING3) {
    int sx, sy;
    window_draw(&windows[idx]);
    for (sy = windows[idx].y; sy < windows[idx].y + windows[idx].h; sy++) {
        for (sx = windows[idx].x; sx < windows[idx].x + windows[idx].w; sx++) {
            gfx_put_pixel(sx, sy, ring3_shadow[sy * RING3_SHADOW_W + sx]);
        }
    }
}
```

### Pre-filling on open (`kernel/kernel.c`'s `window_ring3_open()`)

```c
window_ring3_open(int x, int y, int w, int h, const char *title) {
    ...existing field assignments...
    ring3_shadow_fill_rect(x, y, w, h, window_body_color());
    ...existing raise_window()/window_draw()/gfx_present_rect() calls...
}
```

### New getter (`kernel/gui/window.h`/`window.c`)

```c
/* window.h */
uint32_t window_body_color(void);

/* window.c */
uint32_t window_body_color(void) {
    return WINDOW_BODY_COLOR;
}
```

Avoids `kernel.c` duplicating `window.c`'s own private color constant.

## Testing

**Host-buildable:** no new host test -- `ring3_shadow_put_pixel()`/
`_fill_rect()` are thin, bounds-checked mirrors with no logic
interesting enough to warrant one in isolation, same reasoning every
prior thin-wrapper slice used.

**Headless QEMU**, extending the same proof shape the events slice
already used: open the window, draw a rect via a click (as before),
then -- new for this slice -- deliberately trigger an unrelated redraw
(the same boot-splash-hide transition that originally exposed this
bug) and screendump *after* it, confirming the click's rect is still
there rather than having reverted to backdrop. Then a second click at
a different position, screendump immediately after, confirming *both*
rects are now visible simultaneously -- the concrete, user-visible
proof this slice actually fixes the "second click wipes the first"
symptom the events slice's own proof found and documented. Finally the
usual close-event proof tail, unchanged from the events slice.
