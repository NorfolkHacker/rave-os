#ifndef ACID_HAL_DISPLAY_H
#define ACID_HAL_DISPLAY_H

void hal_display_init( void );
void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color );
void hal_display_fill_circle( int x, int y, int r, unsigned int color );
void hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg );

/* Fills the entire physical screen with one color, regardless of window
 * boundaries -- used by the kernel's own full-screen repaint (see
 * kernel_router.c's kernel_router_repaint_all), not by any app binding.
 * Each target knows its own real resolution; callers never need to. */
void hal_display_clear_screen( unsigned int color );

#endif
