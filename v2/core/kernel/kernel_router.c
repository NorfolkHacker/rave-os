#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "kernel_router.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "kernel_layout.h"
#include "../hal/hal_input.h"

enum drag_mode
{
    DRAG_NONE = 0,
    DRAG_MOVE = 1
};

static enum drag_mode g_drag_mode = DRAG_NONE;
static void * g_drag_task = NULL;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;
static bool g_was_pressed = false;
static void * g_desktop_task = NULL;
static int g_last_x = 0;
static int g_last_y = 0;
static void * g_focus_task = NULL;

void
kernel_router_set_desktop_task( void * task )
{
    g_desktop_task = task;
}

void
kernel_router_activate_window( void * task )
{
    kernel_window_bring_to_front( task );
    g_focus_task = task;
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
            send_event( win, KERNEL_EVENT_MOVED, win->x, win->y, 0 );
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

        kernel_router_activate_window( win->task );

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
                    return;
                }
            }

            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            return;
        }

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
