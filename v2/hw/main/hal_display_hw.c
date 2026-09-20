#include "esp_log.h"
#include "../../core/hal/hal_display.h"

static const char * TAG = "hal_display_hw";

void
hal_display_init( void )
{
    ESP_LOGI( TAG, "hal_display_init: stub, no panel driver wired up yet" );
}

void *
hal_display_create_canvas( int w, int h )
{
    ESP_LOGI( TAG, "hal_display_create_canvas(%d, %d): stub, no canvas allocated", w, h );
    return NULL;
}

void
hal_display_destroy_canvas( void * canvas )
{
    ( void ) canvas;
}

void
hal_display_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_rect(target=%p, %d, %d, %d, %d, 0x%06x): stub, not drawn",
              target, x, y, w, h, color );
}

void
hal_display_fill_circle( void * target, int x, int y, int r, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_circle(target=%p, %d, %d, %d, 0x%06x): stub, not drawn",
              target, x, y, r, color );
}

void
hal_display_draw_text( void * target, int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    ESP_LOGI( TAG, "hal_display_draw_text(target=%p, %d, %d, \"%s\", 0x%06x, 0x%06x): stub, not drawn",
              target, x, y, str, fg, bg );
}

void
hal_display_blit_canvas( void * target, void * canvas, int x, int y )
{
    ESP_LOGI( TAG, "hal_display_blit_canvas(target=%p, canvas=%p, %d, %d): stub, not drawn",
              target, canvas, x, y );
}

void
hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y,
                                unsigned int key )
{
    ESP_LOGI( TAG, "hal_display_blit_canvas_keyed(target=%p, canvas=%p, %d, %d, 0x%06x): stub, not drawn",
              target, canvas, x, y, key );
}

void
hal_display_clear_screen( unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_clear_screen(0x%06x): stub, not drawn", color );
}
