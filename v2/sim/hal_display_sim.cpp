#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

extern "C" {
#include "../core/gfx/gfx.h"
}

static LGFX lcd( 320, 240 );

extern "C" void hal_display_init( void )
{
    /* Must be called before init() (it only affects window creation).
     * Without this, the host window manager is free to resize the SDL
     * window on its own initiative -- observed live doing exactly that,
     * repeatedly, unprompted by anything this app does. Every such resize
     * makes Panel_sdl recompute its touch-to-framebuffer scaling factor
     * from whatever size the window ended up at, so a resize landing at
     * the wrong moment desyncs where a click is reported from where it
     * visually landed -- reported live as "clicking the close button (and
     * separately, the menu) does nothing." A fixed-size, non-resizable
     * window removes the whole class of problem instead of chasing scaling
     * math. (Matches the reference project's own approach: family-mruby's
     * Linux sim boots at one fixed resolution per hardware target,
     * selected once, never resized at runtime.) */
    lgfx::v1::Panel_sdl::setResizable( false );
    /* Also observed live, even with the window locked to a fixed size:
     * enough rapid clicks (the window manager focusing this window on
     * each one) leave the accelerated/vsync'd renderer presenting
     * nothing at all -- the window stays mapped and otherwise normal, it
     * just stops drawing, which looks identical to "the menu doesn't
     * work" since nothing on screen ever changes again. Confirmed by
     * pixel-sampling the window after reproducing it live: solid black,
     * every time, only after several clicks in quick succession, never
     * on the Xvfb harness (no real window manager, so no focus churn).
     * Software rendering doesn't depend on holding a live GPU context
     * across a focus change, and costs nothing noticeable for this
     * small, mostly-flat-fills-and-text UI. */
    lgfx::v1::Panel_sdl::setAccelerated( false );
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
