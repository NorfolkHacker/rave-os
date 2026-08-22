#ifndef RAVEOS_FORTH_HOOKS_H
#define RAVEOS_FORTH_HOOKS_H

/* forth.c's only window into graphics/mouse state -- implemented in
 * kernel.c, which already owns all of it (the window array, the paint
 * canvas, the live cursor position). forth.c itself has no
 * graphics.h/mouse.h/window.h dependency; it only sees these eight
 * extension points. See docs/superpowers/specs/2026-08-16-paint-design.md. */

void forth_hook_paint_open(void);
void forth_hook_beep(void);
void forth_hook_pixel(int x, int y, int color);

/* Canvas-relative cell column/row, 0..15, or -1 if the cursor isn't
 * currently over the PAINT window's canvas at all (window closed,
 * cursor elsewhere, or over the palette/SAVE area instead). */
int forth_hook_mouse_x(void);
int forth_hook_mouse_y(void);

int forth_hook_mouse_down(void);       /* 1 if the left button is currently held, 0 otherwise */
int forth_hook_mouse_right_down(void); /* 1 if the right button is currently held, 0 otherwise */
int forth_hook_current_color(void);    /* the natively-selected palette index, 0..7 */

/* OP_CALL_YIELD's target -- called at every compiled BEGIN...UNTIL
 * loop back-edge (forth.c's handle_compile_token()), automatically,
 * with no script-source opt-in. A no-op when the calling code isn't
 * running inside a scheduled program (scheduler_current_slot() < 0 --
 * e.g. a word typed and invoked directly at the interactive console,
 * never spawned through RUN): a single typed line is never
 * long-running, so there's nothing to yield around there. See
 * docs/superpowers/specs/2026-08-19-concurrency-design.md. */
void forth_hook_yield(void);

/* 1 if the PAINT window has been closed (state != WINDOW_OPEN), 0
 * otherwise -- lets a script's own loop condition notice its window
 * closed instead of only ever checking its own domain-specific exit
 * condition (PLOOP's is MOUSE-RIGHT-DOWN?). */
int forth_hook_window_closed(void);

#endif
