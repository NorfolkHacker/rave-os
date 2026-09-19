#include <stddef.h>

#include "wallpaper.h"
#include "gfx.h"
#include "wallpaper_data.h"
#include "../kernel/kernel_theme.h"

/* This codebase's gfx stack has no PNG/image decoder wired in anywhere (see
 * gfx.h) -- only fill_rect/fill_circle/draw_text primitives reach an app or
 * the screen. wallpaper_data.h is the pixel-art background baked into a
 * table of horizontal, single-color runs at build time (by
 * v2/tools/gen_wallpaper.py, from v2/fsroot/wallpaper.png), replayed
 * once into an offscreen canvas here. Every later frame just blits that
 * already-built canvas, same cost as the flat gfx_clear_screen call it
 * replaces -- the thousands of individual fill_rect calls only ever run
 * once, the first time this is called. */
static void * g_wallpaper_canvas = NULL;

/* Config's own wallpaper on/off toggle (acid_set_wallpaper_enabled/
 * acid_get_wallpaper_enabled, window_binding.c) -- in-memory only, same as
 * every other runtime setting this codebase has (kernel_audio's volume
 * gain included), reset to enabled on restart rather than persisted to
 * disk. */
static int g_wallpaper_enabled = 1;

void
wallpaper_set_enabled( int enabled )
{
    g_wallpaper_enabled = enabled ? 1 : 0;
}

int
wallpaper_is_enabled( void )
{
    return g_wallpaper_enabled;
}

void
wallpaper_blit( void )
{
    if( !g_wallpaper_enabled )
    {
        gfx_clear_screen( THEME_BG );
        return;
    }
    if( g_wallpaper_canvas == NULL )
    {
        g_wallpaper_canvas = gfx_create_canvas( WALLPAPER_W, WALLPAPER_H );
        size_t i;
        for( i = 0; i < WALLPAPER_RUN_COUNT; i++ )
        {
            const struct wallpaper_run * run = &g_wallpaper_runs[i];
            gfx_fill_rect( g_wallpaper_canvas, run->x, run->y, run->len, 1,
                            g_wallpaper_palette[run->color] );
        }
    }
    gfx_blit_canvas( g_wallpaper_canvas, 0, 0 );
}

void
wallpaper_draw_into( void * canvas, int x, int y, int w, int h )
{
    if( !g_wallpaper_enabled )
    {
        gfx_fill_rect( canvas, x, y, w, h, THEME_BG );
        return;
    }
    size_t i;
    for( i = 0; i < WALLPAPER_RUN_COUNT; i++ )
    {
        const struct wallpaper_run * run = &g_wallpaper_runs[i];
        if( run->y < y || run->y >= y + h )
        {
            continue;
        }
        int run_x0 = run->x;
        int run_x1 = run->x + run->len;
        int clip_x0 = run_x0 > x ? run_x0 : x;
        int clip_x1 = run_x1 < x + w ? run_x1 : x + w;
        if( clip_x1 <= clip_x0 )
        {
            continue;
        }
        gfx_fill_rect( canvas, clip_x0, run->y, clip_x1 - clip_x0, 1,
                        g_wallpaper_palette[run->color] );
    }
}
