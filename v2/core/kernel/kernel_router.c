#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "kernel_router.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "kernel_layout.h"
#include "kernel_theme.h"
#include "../hal/hal_input.h"
#include "../gfx/gfx.h"

enum drag_mode
{
    DRAG_NONE = 0,
    DRAG_MOVE = 1
};

static enum drag_mode g_drag_mode = DRAG_NONE;
static void * g_drag_task = NULL;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;
/* The dragged window's position at the moment the drag started -- needed
 * at release to repaint the union of where it WAS and where it ended up,
 * since that's the only region a plain move can possibly have disturbed. */
static int g_drag_orig_x = 0;
static int g_drag_orig_y = 0;
static bool g_was_pressed = false;
static void * g_desktop_task = NULL;
static int g_last_x = 0;
static int g_last_y = 0;
static void * g_focus_task = NULL;

static void send_event( struct kernel_window * win, int type, int x, int y, int pressed );
static void send_event_timeout( struct kernel_window * win, int type, int x, int y, int pressed,
                                 TickType_t timeout );

static int
rects_intersect( int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh )
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* Clears only the given rect and redraws every window that overlaps it,
 * back-to-front in z-order -- NOT the whole screen every time. This
 * project's HAL has no real compositor: every app draws straight onto one
 * shared framebuffer with no backing store or clipping, so a window that
 * closes, drags, or gets raised otherwise leaves stale pixels behind (the
 * phase 5 final review's "windows don't visually close" / "dragging
 * leaves trails" findings). A full per-window backing-buffer compositor is
 * phase-sized and was explicitly parked as a roadmap item. This is the
 * cheap alternative: since every window already knows how to fully
 * repaint itself, ask only the ones that could possibly be affected --
 * windows entirely outside the dirty rect were never disturbed by the
 * clear, so redrawing them too would just be a visible, pointless flicker
 * (and wasted time) for a part of the screen nothing happened to.
 *
 * Reuses the existing KERNEL_EVENT_MOVED, which AcidApp#start already
 * handles by calling redraw() (a full self-repaint including chrome), and
 * which AcidGame deliberately ignores since it repaints every tick anyway
 * -- so every current app is covered with no Ruby-side change. A window
 * only ever redraws its OWN full bounds regardless of how small the dirty
 * rect actually was; that's fine, it naturally clips to itself. */
static void
kernel_router_repaint_rect( int rx, int ry, int rw, int rh )
{
    gfx_fill_rect( rx, ry, rw, rh, THEME_BG );

    /* acid_activate_window (window_binding.c) calls kernel_router_activate_window
     * -- and so this function -- directly on the CALLING APP'S OWN task, not
     * the router's (e.g. desktop.rb's on_touch handler activating a taskbar
     * entry). If that caller's own window is anywhere in the z-order below,
     * waiting on its redraw_done_sem would wait forever on itself: its task
     * can't service its own queued MOVED event while it's sitting right here,
     * blocked inside this very call. Detected and skipped below -- the event
     * is still sent, so it's picked up (and acked) the moment this task next
     * reaches its own poll loop, just without this function blocking on it. */
    void * caller = ( void * ) xTaskGetCurrentTaskHandle();

    int z = -1;
    struct kernel_window * win;
    while( ( win = kernel_window_next_by_z( z ) ) != NULL )
    {
        z = win->z_order;
        if( !rects_intersect( win->x, win->y, win->w, win->h, rx, ry, rw, rh ) )
        {
            continue;
        }
        if( win->task == caller )
        {
            send_event( win, KERNEL_EVENT_MOVED, win->x, win->y, 0 );
            continue;
        }
        /* Drain any stale "done" signal left over from a redraw this
         * window finished on its own between repaints (nothing takes
         * this semaphore except this wait, but a defensive drain costs
         * nothing and guarantees the take below reflects THIS send). */
        xSemaphoreTake( ( SemaphoreHandle_t ) win->redraw_done_sem, 0 );
        send_event_timeout( win, KERNEL_EVENT_MOVED, win->x, win->y, 0, pdMS_TO_TICKS( 50 ) );
        /* Wait for this window's redraw to actually finish before moving
         * on to the next one -- see kernel_window.h's redraw_done_sem
         * comment for why send order alone doesn't guarantee draw-
         * completion order. Bounded so one slow/dead app can't hang the
         * whole compositor forever; a timeout here just means the next
         * window might still race this one, same as before this fix. */
        xSemaphoreTake( ( SemaphoreHandle_t ) win->redraw_done_sem, pdMS_TO_TICKS( 50 ) );
    }
}

/* Repaints the union of a window's own bounds at two positions (where it
 * was, and where it is now) -- the only region a plain move between those
 * two points could possibly have left a trail in or exposed. Used by both
 * the drag's in-progress and release paths. */
static void
kernel_router_repaint_move_union( int old_x, int old_y, int new_x, int new_y, int w, int h )
{
    int union_x = old_x < new_x ? old_x : new_x;
    int union_y = old_y < new_y ? old_y : new_y;
    int right = old_x + w > new_x + w ? old_x + w : new_x + w;
    int bottom = old_y + h > new_y + h ? old_y + h : new_y + h;
    kernel_router_repaint_rect( union_x, union_y, right - union_x, bottom - union_y );
}

void
kernel_router_set_desktop_task( void * task )
{
    g_desktop_task = task;
}

void
kernel_router_activate_window( void * task )
{
    /* If this window is already the frontmost one, bringing it to front
     * again is a complete no-op -- z-order doesn't actually change (it's
     * already at the top), so nothing on screen could possibly be wrong.
     * Skipping the repaint here matters: activating is the ONE thing that
     * happens on every single click a window gets, including clicks on a
     * window that's already focused and on top (the common case while
     * just using an app) -- without this check, every such click cleared
     * and redrew the window's whole rect for literally no visual change,
     * which is exactly what a "flickers every time you click a window"
     * report looks like. */
    struct kernel_window * win = kernel_window_by_task( task );
    struct kernel_window * top = kernel_window_topmost();
    if( win != NULL && top == win )
    {
        g_focus_task = task;
        return;
    }

    kernel_window_bring_to_front( task );
    g_focus_task = task;

    /* Force a repaint of this window's own footprint so raising it to
     * front is actually visible -- without this, z-order/focus state
     * changes correctly but the shared framebuffer still shows whatever
     * last drew on top there (final review finding: "raise-to-front is a
     * no-op on screen"). Nothing outside this window's own bounds could
     * possibly change from a pure z-order raise, so that's the only
     * region that ever needs to be touched. */
    if( win != NULL )
    {
        kernel_router_repaint_rect( win->x, win->y, win->w, win->h );
    }
}

void *
kernel_router_get_focus( void )
{
    return g_focus_task;
}

void
kernel_router_clear_focus( void * task )
{
    if( g_focus_task == task )
    {
        g_focus_task = NULL;
    }
}

static void
send_event_timeout( struct kernel_window * win, int type, int x, int y, int pressed,
                     TickType_t timeout )
{
    if( win == NULL || win->queue == NULL )
    {
        return;
    }
    struct kernel_event ev;
    ev.type = type;
    ev.x = x;
    ev.y = y;
    ev.pressed = pressed;
    xQueueSendToBack( ( QueueHandle_t ) win->queue, &ev, timeout );
}

static void
send_event( struct kernel_window * win, int type, int x, int y, int pressed )
{
    /* Best-effort, non-blocking: fine for high-frequency per-tick events
     * (touch drags, in-progress moves) where a dropped sample just means
     * the next tick's send supersedes it. The one place that needs a
     * stronger guarantee (the final MOVED at drag release) calls
     * send_event_timeout directly instead. */
    send_event_timeout( win, type, x, y, pressed, 0 );
}

static void
kernel_router_poll( void )
{
    int key_code;
    bool key_pressed;
    hal_input_poll_key( &key_code, &key_pressed );
    if( key_pressed && g_focus_task != NULL )
    {
        struct kernel_window * focus_win = kernel_window_by_task( g_focus_task );
        if( focus_win != NULL )
        {
            send_event( focus_win, KERNEL_EVENT_KEY, key_code, 0, 1 );
        }
    }

    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );

    /* x/y are only meaningful when pressed is true (hal_input.h's own
     * contract) -- g_last_x/g_last_y remember the last real touch point so
     * code below never reads x/y while !pressed (Fix 4: that read would be
     * of an indeterminate value). */
    if( pressed )
    {
        g_last_x = x;
        g_last_y = y;
    }

    bool fresh_press = pressed && !g_was_pressed;
    bool fresh_release = !pressed && g_was_pressed;
    g_was_pressed = pressed;

    if( g_desktop_task != NULL && g_drag_mode == DRAG_NONE && g_last_y < KERNEL_DESKTOP_STRIP_H )
    {
        struct kernel_window * desktop = kernel_window_by_task( g_desktop_task );
        if( desktop != NULL && ( fresh_press || pressed || fresh_release ) )
        {
            /* Window-relative, same convention every other window's touch
             * event already follows (Task 4) -- numerically a no-op today
             * since the desktop sits at (0,0), but this is the correct,
             * consistent form for whoever gives desktop.rb a real on_touch
             * later, not a coincidence to leave in place. */
            send_event( desktop, KERNEL_EVENT_TOUCH, g_last_x - desktop->x, g_last_y - desktop->y,
                        pressed ? 1 : 0 );
        }
        return;
    }

    if( g_drag_mode == DRAG_MOVE )
    {
        struct kernel_window * win = kernel_window_by_task( g_drag_task );
        if( win == NULL || !pressed )
        {
            /* Drag ending (release, or the window vanished mid-drag). Send
             * the FINAL position with a short, real blocking timeout
             * instead of the per-tick best-effort 0 -- this is the most
             * authoritative position update and must not be silently
             * dropped even if the queue was momentarily saturated by
             * earlier per-tick MOVED sends during a fast/long drag (Fix 2).
             * Nothing else ever re-syncs the app's idea of its own
             * position after this. */
            if( win != NULL )
            {
                send_event_timeout( win, KERNEL_EVENT_MOVED, win->x, win->y, 0,
                                     pdMS_TO_TICKS( 50 ) );
                /* The final authoritative position sync above only reaches
                 * the dragged window itself. Repaint the union of where it
                 * last got painted and where it ended up, so the trail is
                 * gone once the drag ends without disturbing any window
                 * the drag's path never crossed. Always unconditional here
                 * (unlike the in-progress case below), so the drag's final
                 * settled state is never skipped by anything. */
                kernel_router_repaint_move_union( g_drag_orig_x, g_drag_orig_y, win->x, win->y,
                                                    win->w, win->h );
            }
            g_drag_mode = DRAG_NONE;
            g_drag_task = NULL;
        }
        else
        {
            win->x = x - g_drag_offset_x;
            win->y = y - g_drag_offset_y;
            if( win->y < KERNEL_DESKTOP_STRIP_H )
            {
                /* Never let a window's title bar end up inside the
                 * desktop strip's protected row range -- once released
                 * there, every fresh press on it would be claimed by the
                 * strip's special case first, permanently stranding the
                 * window (Fix 8). */
                win->y = KERNEL_DESKTOP_STRIP_H;
            }
            /* Live drag feedback: repaint the union of where this window
             * last got painted and where it is now, then remember THIS
             * position as the new baseline for next tick -- keeps the
             * dirty rect a tight, incremental sliver instead of growing to
             * cover the whole drag path. (An earlier version of this code
             * skipped all mid-drag repaints entirely, from a since-
             * disproven theory that repeated redraw cycles corrupted
             * LGFX's own SDL present pipeline -- the real cause, fixed
             * separately, was a redraw-completion race between windows'
             * independent tasks with no ordering guarantee at all;
             * kernel_router_repaint_rect's redraw_done_sem wait now makes
             * every one of these calls safe.) */
            kernel_router_repaint_move_union( g_drag_orig_x, g_drag_orig_y, win->x, win->y,
                                                win->w, win->h );
            g_drag_orig_x = win->x;
            g_drag_orig_y = win->y;
        }
        return;
    }

    if( fresh_press )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win == NULL )
        {
            return;
        }

        int rel_x = x - win->x;
        int rel_y = y - win->y;

        if( rel_y < KERNEL_TITLE_BAR_H )
        {
            if( win->closable )
            {
                int cx = win->w - KERNEL_CLOSE_BTN_MARGIN;
                int cy = KERNEL_TITLE_BAR_H / 2;
                int dx = rel_x - cx;
                int dy = rel_y - cy;
                int hit_r = KERNEL_CLOSE_BTN_R + 3; /* a little forgiveness for touch */
                if( ( dx * dx + dy * dy ) <= ( hit_r * hit_r ) )
                {
                    /* Deliberately NOT calling kernel_router_activate_window
                     * here (it used to run unconditionally before this
                     * check) -- raising a window that's about to be
                     * destroyed is pointless, and worse, it's actively
                     * harmful: activate_window's own repaint_all() sends
                     * this window a MOVED event, which it may still be
                     * mid-processing (redrawing itself) when THIS repaint
                     * below runs moments later -- its late redraw then
                     * races back onto the screen on top of the clean
                     * repaint, resurrecting stale content right after this
                     * function just erased it. Confirmed by direct tracing:
                     * this was a real, reproduced bug, not a hypothetical. */
                    send_event( win, KERNEL_EVENT_CLOSE, 0, 0, 0 );
                    kernel_window_unregister( win->task );
                    if( g_focus_task == win->task )
                    {
                        /* Don't leave a dead task handle as the keyboard
                         * focus target. A later fresh press elsewhere, or
                         * a taskbar tap (Task 3), will pick a real one;
                         * until then key events are simply dropped -- the
                         * same "no window, no-op" behavior every other
                         * nowhere-to-deliver input path in this file
                         * already has. */
                        g_focus_task = NULL;
                    }
                    /* The closed window's last-drawn pixels are still on
                     * the shared framebuffer -- nothing else will ever
                     * erase them on its own (no compositor). Repaint just
                     * its own now-vacated footprint (the only region the
                     * close could possibly have exposed) so it's actually
                     * gone, without disturbing any window whose own area
                     * this window never overlapped. */
                    kernel_router_repaint_rect( win->x, win->y, win->w, win->h );
                    return;
                }
            }

            kernel_router_activate_window( win->task );
            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            g_drag_orig_x = win->x;
            g_drag_orig_y = win->y;
            return;
        }

        kernel_router_activate_window( win->task );
        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
    }

    if( pressed )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 1 );
        }
        return;
    }

    if( fresh_release )
    {
        struct kernel_window * win = kernel_window_find_at( g_last_x, g_last_y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, g_last_x - win->x, g_last_y - win->y, 0 );
        }
    }
}

void
kernel_router_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        if( hal_input_should_quit() )
        {
            /* _exit(0), not exit(0) -- see event_binding.c's acid_poll_event
             * for why (sidesteps the vendored LGFX Panel_sdl destructor
             * use-after-free that exit()'s static-destructor teardown would
             * otherwise trigger). */
            _exit( 0 );
        }
        kernel_router_poll();
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}
