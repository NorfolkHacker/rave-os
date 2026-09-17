#include <string.h>

#include "kernel_window.h"

static struct kernel_window g_windows[ KERNEL_WINDOW_MAX ];
static int g_next_z;

void
kernel_window_init( void )
{
    memset( g_windows, 0, sizeof( g_windows ) );
    g_next_z = 1;
}

int
kernel_window_register( void * task, void * queue, const char * app_name,
                         int x, int y, int w, int h, int closable )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            g_windows[ i ].task = task;
            g_windows[ i ].queue = queue;
            g_windows[ i ].app_name = app_name;
            g_windows[ i ].x = x;
            g_windows[ i ].y = y;
            g_windows[ i ].w = w;
            g_windows[ i ].h = h;
            g_windows[ i ].z_order = g_next_z++;
            g_windows[ i ].closable = closable;
            g_windows[ i ].in_use = 1;
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
            return;
        }
    }
}

struct kernel_window *
kernel_window_find_at( int x, int y )
{
    struct kernel_window * best = NULL;
    int best_z = -1;
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
            if( g_windows[ i ].z_order > best_z )
            {
                best_z = g_windows[ i ].z_order;
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
