#include <stddef.h>

#include "kernel_overlay.h"
#include "kernel_layout.h"
#include "kernel_theme.h"
#include "../gfx/gfx.h"

/* Allocated on the first open and then kept for the life of the OS. Freeing
 * it on close would mean an app task destroying memory the router task can
 * be blitting at that very moment: exactly the use-after-free this codebase
 * already hit once with window canvases, where the fix was to move the free
 * onto the owning task after its VM had stopped (see kernel_window_
 * unregister's own comment). There is no equivalent "the owner has stopped"
 * moment for something no task owns, so this design never creates the
 * hazard. The cost is ~450KB (640*360*2) retained after the first egg --
 * and only after the first one, so it is charged solely to a user who
 * triggers it. On the sim that comes from LovyanGFX's own allocator
 * (hal_display_create_canvas), not the 4MB FreeRTOS heap in
 * sim/FreeRTOSConfig.h; on hw the display layer is a stub. */
static void * g_canvas = NULL;
static int g_open = 0;

/* Whoever currently holds the overlay, or NULL when nobody does. Compared
 * as an opaque handle and never dereferenced -- it may well belong to a
 * task that has already died by the time release_owner runs. */
static void * g_owner = NULL;

int
kernel_overlay_open( void * owner_task )
{
    if( g_open && g_owner != owner_task )
    {
        return 0;
    }

    if( g_canvas == NULL )
    {
        g_canvas = gfx_create_canvas( KERNEL_SCREEN_W, KERNEL_SCREEN_H );
        if( g_canvas == NULL )
        {
            return 0;
        }
    }

    /* Always cleared on open, not just on allocation: createSprite
     * zero-fills to black, which would otherwise blacken the whole screen
     * for one frame, and a reopen would otherwise flash the previous
     * egg's last frame. */
    gfx_fill_rect( g_canvas, 0, 0, KERNEL_SCREEN_W, KERNEL_SCREEN_H, ACID_OVERLAY_KEY );
    g_open = 1;
    g_owner = owner_task;

    /* gfx_fill_rect already marks dirty for a non-NULL target, but an open
     * has to survive that being true for other reasons too -- this is the
     * call that guarantees the next tick recomposites with the overlay in
     * the frame. The same goes for close, which draws nothing at all. */
    gfx_mark_dirty();
    return 1;
}

void
kernel_overlay_close( void * owner_task )
{
    if( !g_open || g_owner != owner_task )
    {
        return;
    }
    g_open = 0;
    g_owner = NULL;
    gfx_mark_dirty();
}

void
kernel_overlay_release_owner( void * owner_task )
{
    /* Same shape as kernel_overlay_close, and separate from it on purpose:
     * close is a deliberate act by a running app, this is cleanup for one
     * that may already be gone. Called for every app that exits, almost
     * none of which ever touched the overlay, so the "not the owner" case
     * is the common one and must stay silent. */
    if( !g_open || g_owner != owner_task )
    {
        return;
    }
    g_open = 0;
    g_owner = NULL;
    gfx_mark_dirty();
}

int
kernel_overlay_is_open( void )
{
    return g_open;
}

void *
kernel_overlay_canvas( void )
{
    return g_open ? g_canvas : NULL;
}

void *
kernel_overlay_canvas_for( void * owner_task )
{
    if( !g_open || g_owner != owner_task )
    {
        return NULL;
    }
    return g_canvas;
}
