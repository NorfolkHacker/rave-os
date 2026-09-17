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
static bool g_was_pressed = false;
static void * g_desktop_task = NULL;
static int g_last_x = 0;
static int g_last_y = 0;
static void * g_focus_task = NULL;

static void send_event( struct kernel_window * win, int type, int x, int y, int pressed );
static void send_event_timeout( struct kernel_window * win, int type, int x, int y, int pressed,
                                 TickType_t timeout );

/* Clears the whole physical screen and redraws every window back-to-front,
 * in z-order. This project's HAL has no real compositor: every app draws
 * straight onto one shared framebuffer with no backing store or clipping,
 * so a window that closes, drags, or gets raised otherwise leaves stale
 * pixels behind (the phase 5 final review's "windows don't visually
 * close" / "dragging leaves trails" findings) -- fixing that properly
 * (per-window backing buffers, or a repaint-request/ack protocol) is
 * phase-sized and was explicitly parked as a roadmap item. This is the
 * cheap alternative: since every window already knows how to fully
 * repaint itself, just ask ALL of them to do it, in the right order, onto
 * a freshly-cleared screen, whenever anything changes.
 *
 * Reuses the existing KERNEL_EVENT_MOVED, which AcidApp#start already
 * handles by calling redraw() (a full self-repaint including chrome), and
 * which AcidGame deliberately ignores since it repaints every tick anyway
 * -- so every current app is covered with no Ruby-side change.
 *
 * Best-effort per window (0-timeout send, matching every other
 * high-frequency event in this file): this runs on every drag tick plus
 * every activate/close, so a single dropped frame is harmless -- the very
 * next trigger repaints again moments later. */
static void
kernel_router_repaint_all( void )
{
    gfx_clear_screen( THEME_BG );

    int z = -1;
    struct kernel_window * win;
    while( ( win = kernel_window_next_by_z( z ) ) != NULL )
    {
        z = win->z_order;
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

    /* Force a full repaint so raising a window to front is actually
     * visible -- without this, z-order/focus state changes correctly but
     * the shared framebuffer still shows whatever last drew on top (final
     * review finding: "raise-to-front is a no-op on screen"). */
    kernel_router_repaint_all();
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
            }
            g_drag_mode = DRAG_NONE;
            g_drag_task = NULL;
            /* The final authoritative position sync above only reaches
             * the dragged window itself. Repaint everything on top of a
             * clean screen so the drag's own trail (every intermediate
             * position this window passed through, still on the shared
             * framebuffer -- no compositor) is gone once the drag ends.
             * Always unconditional here (unlike the in-progress case
             * below), so the drag's final settled state is never skipped
             * by the throttle. */
            kernel_router_repaint_all();
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
            /* Deliberately no redraw/repaint of any kind while the drag is
             * still held -- only the position bookkeeping above. Confirmed
             * by direct, isolated testing: it's specifically TWO OR MORE
             * redraw-triggering sends in quick succession during one held
             * drag (whether that's kernel_router_repaint_all() touching
             * every window, or even just this ONE window's own MOVED sent
             * repeatedly per-tick) that corrupts LGFX's SDL rendering/
             * present pipeline hard enough that NOTHING ever renders
             * correctly again for the rest of the process's life, no
             * matter how long you wait afterward -- not the number of
             * windows touched, not full-screen clears specifically, just
             * repeated redraw cycles fired close together. A single
             * redraw cycle -- no matter how many windows it touches --
             * always works. This looks like a genuine concurrency
             * assumption in vendored LGFX's Panel_sdl (a semaphore-based
             * signal-and-wait-for-present protocol per draw call,
             * seemingly not designed for many FreeRTOS-task-pthreads
             * hammering it across repeated redraws) rather than a bug in
             * this file -- not fixed at that layer, since it's vendored
             * third-party code, and not root-caused further within this
             * fix's own budget.
             *
             * The trade-off: a window doesn't visually track the cursor
             * while actively being dragged (no live drag preview) -- it
             * "warps" to its final position only on release, via the one
             * guaranteed send_event_timeout + kernel_router_repaint_all()
             * below, which is the ONLY redraw this whole interaction ever
             * triggers. Losing live drag feedback is a real regression,
             * but it's the only way found so far to guarantee the screen
             * is never left permanently broken. */
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
                     * erase them on its own (no compositor). Repaint
                     * everything still open on top of a clean screen so
                     * the close is actually visible. */
                    kernel_router_repaint_all();
                    return;
                }
            }

            kernel_router_activate_window( win->task );
            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
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
