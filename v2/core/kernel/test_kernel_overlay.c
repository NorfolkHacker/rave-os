#include <assert.h>
#include <stdio.h>

#include "kernel_overlay.h"
#include "kernel_theme.h"
#include "kernel_layout.h"

/* Declared here rather than by including gfx.h: gfx.h pulls in FreeRTOS.h
 * and semphr.h for its lock accessor, none of which this test needs. These
 * three are the only gfx entry points kernel_overlay.c actually calls, and
 * these definitions are what it links against. */
void * gfx_create_canvas( int w, int h );
void gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void gfx_mark_dirty( void );

static int g_create_calls = 0;
static int g_dirty_calls = 0;
static int g_last_fill_w = 0;
static int g_last_fill_h = 0;
static unsigned int g_last_fill_color = 0;
static int g_fill_calls = 0;
static char g_fake_canvas[ 4 ];

void *
gfx_create_canvas( int w, int h )
{
    g_create_calls++;
    assert( w == KERNEL_SCREEN_W );
    assert( h == KERNEL_SCREEN_H );
    return g_fake_canvas;
}

void
gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    ( void ) target;
    ( void ) x;
    ( void ) y;
    g_fill_calls++;
    g_last_fill_w = w;
    g_last_fill_h = h;
    g_last_fill_color = color;
}

void
gfx_mark_dirty( void )
{
    g_dirty_calls++;
}

/* Stand-ins for two different app tasks' handles: the overlay only ever
 * compares these pointers, never dereferences them. */
static int g_task_a_storage;
static int g_task_b_storage;
static void * TASK_A = &g_task_a_storage;
static void * TASK_B = &g_task_b_storage;

int
main( void )
{
    /* Closed before anything opens it: nothing to composite, no canvas
     * allocated. A user who never types an easter egg never pays the
     * overlay's ~450KB. */
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );
    assert( g_create_calls == 0 );

    /* Opening allocates once, clears the whole canvas to the key colour
     * (so the very first composited frame shows nothing rather than the
     * black createSprite zero-fills it with), and marks the screen dirty
     * -- the open itself draws nothing, so nothing else would. */
    assert( kernel_overlay_open( TASK_A ) == 1 );
    assert( kernel_overlay_is_open() == 1 );
    assert( kernel_overlay_canvas() == g_fake_canvas );
    assert( g_create_calls == 1 );
    assert( g_fill_calls == 1 );
    assert( g_last_fill_w == KERNEL_SCREEN_W );
    assert( g_last_fill_h == KERNEL_SCREEN_H );
    assert( g_last_fill_color == ACID_OVERLAY_KEY );
    assert( g_dirty_calls == 1 );

    /* Opening again from the SAME task reuses the same canvas -- never a
     * second allocation. */
    assert( kernel_overlay_open( TASK_A ) == 1 );
    assert( g_create_calls == 1 );
    assert( kernel_overlay_canvas() == g_fake_canvas );

    /* --- one owner at a time --------------------------------------- */

    /* A second task is refused outright while A holds the overlay. This is
     * the whole "only one easter egg at a time" rule: two terminal windows
     * are two separate mruby VMs, so a Ruby-side guard in AcidEggs cannot
     * see the other one. Two sprites sharing one canvas would clear each
     * other's frames. */
    assert( kernel_overlay_open( TASK_B ) == 0 );
    assert( kernel_overlay_is_open() == 1 );

    /* ...and a non-owner can neither draw on it nor close it out from
     * under the task that is using it. */
    assert( kernel_overlay_canvas_for( TASK_B ) == NULL );
    assert( kernel_overlay_canvas_for( TASK_A ) == g_fake_canvas );
    kernel_overlay_close( TASK_B );
    assert( kernel_overlay_is_open() == 1 );
    assert( kernel_overlay_canvas_for( TASK_A ) == g_fake_canvas );

    /* A non-owner's release is likewise ignored -- it must not be able to
     * strip A's claim. */
    kernel_overlay_release_owner( TASK_B );
    assert( kernel_overlay_is_open() == 1 );

    /* Closing hides it from the compositor but deliberately does NOT free
     * the canvas: the task that closes it is an app task, while the router
     * task may be mid-blit on those same pixels. See kernel_overlay.c's own
     * comment. */
    kernel_overlay_close( TASK_A );
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );
    assert( kernel_overlay_canvas_for( TASK_A ) == NULL );

    /* Once released, the next task in gets it. */
    assert( kernel_overlay_open( TASK_B ) == 1 );
    assert( kernel_overlay_canvas_for( TASK_B ) == g_fake_canvas );
    assert( kernel_overlay_canvas_for( TASK_A ) == NULL );

    /* A crash mid-egg must not wedge the overlay shut forever: vm_host's
     * per-app cleanup releases whatever claim the dying task held. */
    kernel_overlay_release_owner( TASK_B );
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_open( TASK_A ) == 1 );

    /* Reopening after a close reuses the surviving canvas and re-clears it,
     * so the previous egg's last frame can't flash up. */
    kernel_overlay_close( TASK_A );
    int fills_before = g_fill_calls;
    assert( kernel_overlay_open( TASK_A ) == 1 );
    assert( g_create_calls == 1 );
    assert( g_fill_calls == fills_before + 1 );
    kernel_overlay_close( TASK_A );

    /* Releasing a claim nobody holds is harmless -- vm_host calls it for
     * every app that exits, and almost none of them ever touch the
     * overlay. */
    kernel_overlay_release_owner( TASK_A );
    assert( kernel_overlay_is_open() == 0 );

    printf( "test_kernel_overlay: all assertions passed\n" );
    return 0;
}
