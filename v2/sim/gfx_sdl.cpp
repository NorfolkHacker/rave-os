#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

static LGFX lcd( 320, 240 );

extern "C" void sim_gfx_init( void )
{
    lcd.init();
    lcd.fillScreen( TFT_BLACK );
}

extern "C" void sim_gfx_fill_rect( int x, int y, int w, int h, uint32_t color )
{
    lcd.fillRect( x, y, w, h, color );
}
