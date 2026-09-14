#ifndef ACID_HAL_DISPLAY_H
#define ACID_HAL_DISPLAY_H

void hal_display_init( void );
void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color );
void hal_display_fill_circle( int x, int y, int r, unsigned int color );

#endif
