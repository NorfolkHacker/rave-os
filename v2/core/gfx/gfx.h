#ifndef ACID_GFX_H
#define ACID_GFX_H

#include "FreeRTOS.h"
#include "semphr.h"

void gfx_init( void );

/* Per-window offscreen canvas -- see hal_display.h's own comment on the
 * HAL functions these wrap. Created once per window (kernel_window.c, via
 * kernel_spawn.c) and destroyed when the window closes. */
void * gfx_create_canvas( int w, int h );
void gfx_destroy_canvas( void * canvas );

/* `target` is NULL for the real screen or a canvas from gfx_create_canvas
 * -- see hal_display.h. Every app binding (chrome_binding.c, gfx_binding.c)
 * passes its own window's canvas; only the router's compositor ever passes
 * NULL, and only for the shared background. */
void gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void gfx_fill_circle( void * target, int x, int y, int r, unsigned int color );
void gfx_draw_text( void * target, int x, int y, const char * str, unsigned int fg, unsigned int bg );
void gfx_clear_screen( unsigned int color );

/* Copies a canvas's current pixels onto the real screen at (x, y). The
 * only way a canvas's contents ever reach the actual display -- see
 * kernel_router.c's kernel_router_composite_frame, the sole caller. */
void gfx_blit_canvas( void * canvas, int x, int y );

/* Marks that the screen needs recompositing -- set automatically by
 * gfx_fill_rect/fill_circle/draw_text whenever they target a canvas (an
 * app actually drew something), and explicitly by kernel_window.c/
 * kernel_router.c whenever a window's position or z-order changes (a
 * canvas draw call alone can't cover a plain move, since the canvas's
 * own pixels don't change when a window just slides to a new spot).
 * gfx_take_dirty reads and clears it in one step; kernel_router_task
 * calls it once per tick and only recomposites when it comes back true.
 *
 * This exists because recompositing is NOT free: it blits every visible
 * window's canvas onto the real screen, and each blit round-trips
 * through this project's SDL/LGFX backend's own per-call synchronization
 * (see hal_display_sim.cpp's git history) -- the same cost that made a
 * handful of draw calls take 50ms+ under real desktop load during the
 * drag-trail investigation. Doing that unconditionally on every ~16ms
 * tick, whether or not anything changed, was tried first and made the
 * router's own loop period balloon well past 16ms under real load,
 * which made it miss fast keydown/keyup transitions entirely (a key
 * scan only detects a press if it's still down on the NEXT poll --
 * confirmed live, instrumented: roughly 60% of keystrokes were silently
 * dropped with unconditional per-tick compositing). Skipping the
 * composite step on ticks where nothing visible changed keeps the
 * router's loop fast and responsive the rest of the time, which is most
 * ticks in ordinary use. */
void gfx_mark_dirty( void );
int gfx_take_dirty( void );

/* Accessor for the mutex created inside gfx_init() that serializes access
 * to the underlying HAL display object, which is shared (unsynchronized at
 * the HAL layer) across every app task and the router task. Callers that
 * touch the same underlying object outside of gfx_fill_rect/gfx_fill_circle
 * (currently: the sim's hal_input_poll_touch, which reads touch state off
 * the same LGFX object) must take/give this same lock around their access. */
SemaphoreHandle_t gfx_get_lock( void );

#endif
