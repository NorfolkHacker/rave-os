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

/* Accessor for the mutex created inside gfx_init() that serializes access
 * to the underlying HAL display object, which is shared (unsynchronized at
 * the HAL layer) across every app task and the router task. Callers that
 * touch the same underlying object outside of gfx_fill_rect/gfx_fill_circle
 * (currently: the sim's hal_input_poll_touch, which reads touch state off
 * the same LGFX object) must take/give this same lock around their access. */
SemaphoreHandle_t gfx_get_lock( void );

#endif
