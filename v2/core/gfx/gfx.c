#include "gfx.h"
#include "../hal/hal_display.h"

/* Serializes every access to the HAL's shared underlying display object
 * (e.g. the sim's single `static LGFX lcd`), which is touched from
 * multiple real FreeRTOS-POSIX pthreads (every app task's draw calls, plus
 * the router task's own touch polling) with no synchronization of its own.
 * Created once in gfx_init(), which itself runs single-threaded at boot
 * before any app task or the router task exists (Task 6's fix), so
 * gfx_init() itself needs no locking. */
static SemaphoreHandle_t g_gfx_lock = NULL;

void
gfx_init( void )
{
    hal_display_init();
    g_gfx_lock = xSemaphoreCreateMutex();
}

SemaphoreHandle_t
gfx_get_lock( void )
{
    return g_gfx_lock;
}

void
gfx_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_fill_rect( x, y, w, h, color );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_fill_circle( int x, int y, int r, unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_fill_circle( x, y, r, color );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_draw_text( x, y, str, fg, bg );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_clear_screen( unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_clear_screen( color );
    xSemaphoreGive( g_gfx_lock );
}
