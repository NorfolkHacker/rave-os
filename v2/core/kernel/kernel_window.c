#include <string.h>

#include "kernel_window.h"
#include "../gfx/gfx.h"

static struct kernel_window g_windows[ KERNEL_WINDOW_MAX ];
static int g_next_z;

void
kernel_window_init( void )
{
    memset( g_windows, 0, sizeof( g_windows ) );
    g_next_z = 1;
}

int
kernel_window_register( void * task, void * queue, void * redraw_done_sem, void * canvas,
                         const char * app_name,
                         int x, int y, int w, int h, int closable )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            g_windows[ i ].task = task;
            g_windows[ i ].queue = queue;
            g_windows[ i ].redraw_done_sem = redraw_done_sem;
            g_windows[ i ].canvas = canvas;
            g_windows[ i ].app_name = app_name;
            g_windows[ i ].x = x;
            g_windows[ i ].y = y;
            g_windows[ i ].w = w;
            g_windows[ i ].h = h;
            g_windows[ i ].z_order = g_next_z++;
            g_windows[ i ].closable = closable;
            g_windows[ i ].in_use = 1;
            gfx_mark_dirty();
            return 1;
        }
    }
    return 0;
}

void
kernel_window_unregister( void * task )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use && g_windows[ i ].task == task )
        {
            g_windows[ i ].in_use = 0;
            gfx_destroy_canvas( g_windows[ i ].canvas );
            g_windows[ i ].canvas = NULL;
            gfx_mark_dirty();
            return;
        }
    }
}

struct kernel_window *
kernel_window_find_at( int x, int y )
{
    /* best == NULL is the "nothing found yet" guard -- NOT a hardcoded
     * z_order sentinel. This used to start best_z at -1 and require
     * z_order > best_z, which quietly assumed z_order never goes negative.
     * kernel_window_send_to_back sets a window's z_order to (the current
     * lowest z_order among every OTHER window) - 1 every time it's called
     * -- repeated calls (desktop.rb's dropdown menu calls it on every
     * close) drive it arbitrarily negative over a long session. Once a
     * window's z_order dropped below -1, it could never be found by this
     * function again, even as the ONLY window covering that point --
     * confirmed live: reproduced exactly this, desktop's dropdown menu
     * became permanently unclickable after enough open/close cycles
     * pushed its z_order to -4. */
    struct kernel_window * best = NULL;
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            continue;
        }
        if( x >= g_windows[ i ].x && x < g_windows[ i ].x + g_windows[ i ].w &&
            y >= g_windows[ i ].y && y < g_windows[ i ].y + g_windows[ i ].h )
        {
            if( best == NULL || g_windows[ i ].z_order > best->z_order )
            {
                best = &g_windows[ i ];
            }
        }
    }
    return best;
}

struct kernel_window *
kernel_window_by_task( void * task )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use && g_windows[ i ].task == task )
        {
            return &g_windows[ i ];
        }
    }
    return NULL;
}

void
kernel_window_bring_to_front( void * task )
{
    struct kernel_window * win = kernel_window_by_task( task );
    if( win != NULL )
    {
        win->z_order = g_next_z++;
        gfx_mark_dirty();
    }
}

int
kernel_window_count( void )
{
    int i, count = 0;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use )
        {
            count++;
        }
    }
    return count;
}

struct kernel_window *
kernel_window_at_index( int index )
{
    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return NULL;
    }
    return &g_windows[ index ];
}

struct kernel_window *
kernel_window_next_by_z( int after_z )
{
    struct kernel_window * best = NULL;
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use || g_windows[ i ].z_order <= after_z )
        {
            continue;
        }
        if( best == NULL || g_windows[ i ].z_order < best->z_order )
        {
            best = &g_windows[ i ];
        }
    }
    return best;
}

struct kernel_window *
kernel_window_topmost( void )
{
    struct kernel_window * best = NULL;
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            continue;
        }
        if( best == NULL || g_windows[ i ].z_order > best->z_order )
        {
            best = &g_windows[ i ];
        }
    }
    return best;
}

void
kernel_window_send_to_back( void * task )
{
    struct kernel_window * win = kernel_window_by_task( task );
    if( win == NULL )
    {
        return;
    }
    int min_z = win->z_order;
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use && &g_windows[ i ] != win && g_windows[ i ].z_order < min_z )
        {
            min_z = g_windows[ i ].z_order;
        }
    }
    win->z_order = min_z - 1;
    gfx_mark_dirty();
}
