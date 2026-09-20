#include <assert.h>
#include <stdio.h>

#include "kernel_overlay.h"

void * gfx_create_canvas( int w, int h );
void gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void gfx_mark_dirty( void );

void *
gfx_create_canvas( int w, int h )
{
    ( void ) w;
    ( void ) h;
    return NULL;
}

void
gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    ( void ) target; ( void ) x; ( void ) y; ( void ) w; ( void ) h; ( void ) color;
    /* Reaching here would mean kernel_overlay_open() tried to clear a
     * canvas it never got. */
    assert( 0 );
}

void
gfx_mark_dirty( void )
{
}

static int g_task_storage;

int
main( void )
{
    void * task = &g_task_storage;
    assert( kernel_overlay_open( task ) == 0 );
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );
    assert( kernel_overlay_canvas_for( task ) == NULL );
    printf( "test_kernel_overlay_alloc_fail: all assertions passed\n" );
    return 0;
}
