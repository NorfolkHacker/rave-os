#include "gfx.h"
#include "../hal/hal_display.h"

void
gfx_init( void )
{
    hal_display_init();
}

void
gfx_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    hal_display_fill_rect( x, y, w, h, color );
}
