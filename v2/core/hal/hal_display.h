#ifndef ACID_HAL_DISPLAY_H
#define ACID_HAL_DISPLAY_H

void hal_display_init( void );

/* An opaque, private offscreen pixel buffer the size of one window --
 * every window gets exactly one (see kernel_window.h), created when the
 * window is registered and destroyed when it closes. Apps draw into their
 * own canvas using the SAME primitives below (fill_rect/fill_circle/
 * draw_text), passing it as `target`; the real screen is never touched
 * directly by app drawing any more. hal_display_blit_canvas is the only
 * path a canvas's pixels ever reach the actual visible display. */
void * hal_display_create_canvas( int w, int h );
void hal_display_destroy_canvas( void * canvas );

/* `target` selects where these draw: NULL means the real screen, non-NULL
 * means that canvas (see hal_display_create_canvas above). Every app
 * binding passes its own window's canvas; the router itself is the only
 * caller that ever passes NULL (compositing the shared background). */
void hal_display_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void hal_display_fill_circle( void * target, int x, int y, int r, unsigned int color );
void hal_display_draw_text( void * target, int x, int y, const char * str, unsigned int fg, unsigned int bg );

/* Copies `canvas`'s current pixels into `target` at (x, y) -- a plain
 * in-memory blit, not a request the owning app has to service. `target`
 * follows the same convention as the primitives above: NULL is the real
 * screen, non-NULL is another canvas. The router composites every visible
 * window into an offscreen back buffer with this (canvas-to-canvas), then
 * makes the finished frame visible with a single canvas-to-screen call --
 * see gfx.h's gfx_present and kernel_router_composite_frame. */
void hal_display_blit_canvas( void * target, void * canvas, int x, int y );

/* As hal_display_blit_canvas, but every pixel of `canvas` equal to `key` is
 * left untouched in `target` instead of copied. The one thing in this
 * codebase that isn't an opaque copy: it is how the kernel's overlay
 * (kernel_overlay.h) draws over the whole screen without erasing the
 * wallpaper and windows it passes over. `key` is an RGB888 literal, the
 * same as every other colour argument here. */
void hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y,
                                     unsigned int key );

/* Fills the entire physical screen with one color, regardless of window
 * boundaries -- used by the router's own compositor before blitting any
 * window canvases on top. Each target knows its own real resolution;
 * callers never need to. */
void hal_display_clear_screen( unsigned int color );

#endif
