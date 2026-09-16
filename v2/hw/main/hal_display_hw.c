#include "esp_log.h"
#include "../../core/hal/hal_display.h"

static const char * TAG = "hal_display_hw";

void
hal_display_init( void )
{
    ESP_LOGI( TAG, "hal_display_init: stub, no panel driver wired up yet" );
}

void
hal_display_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_rect(%d, %d, %d, %d, 0x%06x): stub, not drawn", x, y, w, h, color );
}

void
hal_display_fill_circle( int x, int y, int r, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_circle(%d, %d, %d, 0x%06x): stub, not drawn", x, y, r, color );
}

void
hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    ESP_LOGI( TAG, "hal_display_draw_text(%d, %d, \"%s\", 0x%06x, 0x%06x): stub, not drawn", x, y, str, fg, bg );
}
