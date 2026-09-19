#include "gfx.h"
#include "../hal/hal_display.h"

/* Serializes every access to the HAL's shared underlying display object
 * (e.g. the sim's single `static LGFX lcd`), which is touched from
 * multiple real FreeRTOS-POSIX pthreads (every app task's draw calls, plus
 * the router task's own touch polling) with no synchronization of its own.
 * Also covers per-window canvases: the owning app task draws into its own
 * canvas while the router task concurrently reads it (blitting to the
 * screen every ~16ms frame, see kernel_router_composite_frame) -- the same
 * one global lock serializes both, which costs little since canvas draws
 * no longer round-trip through the real display's own synchronization
 * (see hal_display_sim.cpp's Panel_sdl comment history). Created once in
 * gfx_init(), which itself runs single-threaded at boot before any app
 * task or the router task exists (Task 6's fix), so gfx_init() itself
 * needs no locking. */
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

void *
gfx_create_canvas( int w, int h )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    void * canvas = hal_display_create_canvas( w, h );
    xSemaphoreGive( g_gfx_lock );
    return canvas;
}

void
gfx_destroy_canvas( void * canvas )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_destroy_canvas( canvas );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_fill_rect( target, x, y, w, h, color );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_fill_circle( void * target, int x, int y, int r, unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_fill_circle( target, x, y, r, color );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_draw_text( void * target, int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_draw_text( target, x, y, str, fg, bg );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_clear_screen( unsigned int color )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_clear_screen( color );
    xSemaphoreGive( g_gfx_lock );
}

void
gfx_blit_canvas( void * canvas, int x, int y )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_blit_canvas( canvas, x, y );
    xSemaphoreGive( g_gfx_lock );
}
