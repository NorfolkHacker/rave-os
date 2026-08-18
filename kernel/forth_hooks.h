#ifndef RAVEOS_FORTH_HOOKS_H
#define RAVEOS_FORTH_HOOKS_H

/* forth.c's only window into graphics/mouse state -- implemented in
 * kernel.c, which already owns all of it (the window array, the paint
 * canvas, the live cursor position). forth.c itself has no
 * graphics.h/mouse.h/window.h dependency; it only sees these eight
 * extension points. See docs/superpowers/specs/2026-08-16-paint-design.md. */

void forth_hook_paint_open(void);
void forth_hook_pixel(int x, int y, int color);

/* Canvas-relative cell column/row, 0..15, or -1 if the cursor isn't
 * currently over the PAINT window's canvas at all (window closed,
 * cursor elsewhere, or over the palette/SAVE area instead). */
int forth_hook_mouse_x(void);
int forth_hook_mouse_y(void);

int forth_hook_mouse_down(void);       /* 1 if the left button is currently held, 0 otherwise */
int forth_hook_mouse_right_down(void); /* 1 if the right button is currently held, 0 otherwise */
int forth_hook_current_color(void);    /* the natively-selected palette index, 0..7 */
void forth_hook_refresh(void);

/* If the left button is currently held over a palette swatch, selects
 * it (see forth_hook_current_color()'s comment above) -- a no-op
 * otherwise. Needed so a script's own loop can change color without
 * ever falling back to kmain()'s per-frame click handling, which
 * doesn't run at all while that loop blocks. */
void forth_hook_palette_pick(void);

#endif
