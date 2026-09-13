#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

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

extern "C" void hal_input_poll_touch( int * x, int * y, bool * pressed )
{
    lgfx::v1::touch_point_t tp;
    uint_fast8_t count = lcd.getTouch( &tp, 1 );
    *pressed = ( count > 0 );
    if( count > 0 )
    {
        *x = tp.x;
        *y = tp.y;
    }
}
