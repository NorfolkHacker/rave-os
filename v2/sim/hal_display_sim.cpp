#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

extern "C" {
#include "../core/gfx/gfx.h"
}

static LGFX lcd( 320, 240 );

extern "C" void hal_display_init( void )
{
    lcd.init();
    lcd.fillScreen( TFT_BLACK );
}

extern "C" void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    lcd.fillRect( x, y, w, h, color );
}

extern "C" void hal_display_clear_screen( unsigned int color )
{
    /* Deliberately NOT lcd.fillScreen(color) -- LGFXBase.hpp defines
     * fillScreen(color) as setColor(color) + the colorless fillRect(x,y,w,h)
     * overload, a different internal path than the (x,y,w,h,color) overload
     * every other draw call in this project already uses successfully.
     * Confirmed by direct testing on this SDL backend: fillScreen's own
     * path silently fails to present to the actual window (real fill_rect/
     * fill_circle/draw_text calls right after it show up fine; fillScreen's
     * own output never does) -- fillRect with the full screen's own
     * dimensions is the reliable, already-proven-working call. */
    lcd.fillRect( 0, 0, lcd.width(), lcd.height(), color );
}

extern "C" void hal_display_fill_circle( int x, int y, int r, unsigned int color )
{
    lcd.fillCircle( x, y, r, color );
}

extern "C" void hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    lcd.setTextColor( fg, bg );
    lcd.drawString( str, x, y );
}

extern "C" void hal_input_poll_touch( int * x, int * y, bool * pressed )
{
    lgfx::v1::touch_point_t tp;
    SemaphoreHandle_t lock = gfx_get_lock();
    xSemaphoreTake( lock, portMAX_DELAY );
    uint_fast8_t count = lcd.getTouch( &tp, 1 );
    xSemaphoreGive( lock );
    *pressed = ( count > 0 );
    if( count > 0 )
    {
        *x = tp.x;
        *y = tp.y;
    }
}
