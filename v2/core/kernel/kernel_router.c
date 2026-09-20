#include <stdbool.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "kernel_router.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "kernel_layout.h"
#include "kernel_overlay.h"
#include "kernel_theme.h"
#include "../hal/hal_input.h"
#include "../gfx/gfx.h"
#include "../gfx/wallpaper.h"

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

/* The window a touch gesture began on, held until the finger lifts --
 * pointer capture, the same shape g_drag_task already gives a title-bar
 * drag, extended to ordinary taps.
 *
 * Without it, the held and release branches below re-ran
 * kernel_window_find_at on EVERY ~16ms tick, so a gesture was delivered
 * to whatever happened to be under the finger at that instant rather
 * than to the window it started on. Three things went wrong, all
 * reported as "taps behaving erratically":
 *
 *   - Tapping a Menu entry spawns an app UNDER the finger, and the rest
 *     of that same tap landed inside the new window. Reproduced every
 *     time: Menu -> File Manager opened already navigated into
 *     v2/fsroot/Bin, because the tail of the tap hit its file list.
 *   - Dragging from one window across another delivered touches to the
 *     second one mid-gesture.
 *   - Tapping a close button sent the remainder of the tap to whatever
 *     the closed window had been covering.
 *
 * A gesture belongs to one window for its whole life. NULL means no
 * gesture is in progress. */
static void * g_touch_task = NULL;
static int g_last_x = 0;
static int g_last_y = 0;

/* Plain counters, not _Atomic: written only from this task's own loop
 * (kernel_router_task below), read from any app task via
 * kernel_router_composited_frames/kernel_router_skipped_frames for a
 * system-monitor app to show real compositor activity -- e.g. how much
 * the dirty-flag check (gfx_take_dirty, see its own comment) is actually
 * saving. A plain int read from another thread isn't formally
 * synchronized, but this codebase already reads kernel_window_count()
 * the same cross-thread way for plain display/bookkeeping purposes
 * (kernel_spawn.c); a monitoring counter that's occasionally one tick
 * stale costs nothing real. */
static int g_frames_composited = 0;
static int g_frames_skipped = 0;
static void * g_focus_task = NULL;

static void send_event( struct kernel_window * win, int type, int x, int y, int pressed );

/* The compositor. Every window owns a private offscreen canvas (see
 * kernel_window.h/hal_display.h) that it draws into using its own
 * window-relative coordinates, entirely independent of where the window
 * currently sits on screen (gfx_binding.c/chrome_binding.c). This
 * function is the ONLY place a canvas's pixels ever reach the real,
 * visible screen: paint the shared background into an offscreen back
 * buffer, walk every window back-to-front in z-order and blit (copy) its
 * current canvas into that same back buffer at its current position, then
 * gfx_present the finished frame to the screen in one go (see gfx.h's
 * gfx_present -- building the frame on screen, a piece at a time, was
 * itself a reported flicker bug). Called from kernel_router_task whenever
 * gfx_take_dirty() says something changed since the last tick (not on
 * every single tick regardless, and not reactively on move/raise/close
 * the way an earlier version of this file worked either -- see
 * gfx_mark_dirty's own comment for why both of those turned out wrong).
 *
 * That earlier version asked each window's own app task to synchronously
 * redraw itself, live, on demand, and waited on a semaphore for that
 * redraw to finish before compositing the next window. It produced two
 * real, separately reported bugs: a permanent "ghost" trail when a
 * window's redraw didn't finish inside the wait's bound (confirmed live,
 * with direct instrumentation: on real hardware, under real desktop-
 * environment load, a handful of fillRect/drawString calls routinely took
 * well over the original 50ms bound, because each one round-tripped
 * through this project's SDL/LGFX backend's own per-call synchronization
 * -- see hal_display_sim.cpp's git history), and a visible flicker even
 * when a redraw DID finish in time (the screen briefly showed bare
 * background between the router's clear and the app's redraw completing).
 * Compositing from an always-current, already-rendered canvas needs no
 * app cooperation and no waiting at all: repositioning, raising, or
 * closing a window is now just a cheap in-memory copy done entirely on
 * this task's own thread, so there is nothing left to race or time out.
 * Apps still draw into their own canvas exactly as before, at their own
 * pace (on_touch, on_tick, etc.) -- this function just doesn't need to
 * ask them to, or wait for them, to make that visible. */
static void
kernel_router_composite_frame( void )
{
    wallpaper_blit();

    int z = -1;
    struct kernel_window * win;
    while( ( win = kernel_window_next_by_z( z ) ) != NULL )
    {
        z = win->z_order;
        gfx_blit_canvas( win->canvas, win->x, win->y );
    }

    /* Last, on top of every window: the kernel overlay, blitted with its
     * key colour treated as transparent, so it draws over the whole screen
     * while leaving everything it isn't actually painting visible
     * underneath. Nothing else in this frame is keyed -- see
     * kernel_overlay.h for why this is not a window.
     *
     * Read the pointer once and NULL-check THAT, rather than asking
     * is_open() and then separately asking canvas() -- an app task can call
     * kernel_overlay_close or kernel_overlay_release_owner in between those
     * two calls, and the second one would then hand the compositor a NULL,
     * which gfx_blit_canvas_keyed dereferences unconditionally (as_canvas(
     * NULL)->pushSprite(...) in hal_display_sim.cpp is a null-this member
     * call, i.e. a segfault of the whole OS, not just this frame). A single
     * read can still race a close, but the worst it can hand back is a
     * stale, non-NULL pointer to a canvas that just got marked closed --
     * and that is harmless, because kernel_overlay never frees the canvas
     * it allocates (see kernel_overlay.c). Blitting it one frame late just
     * shows one extra frame of an animation that has already ended, never
     * a use-after-free.
     *
     * This interleaving cannot actually happen on the sim today -- the
     * router task runs at tskIDLE_PRIORITY + 2 (sim/sim_main.c) and every
     * app task at tskIDLE_PRIORITY + 1 (core/kernel/kernel_spawn.c), and a
     * single-core preemptive scheduler never runs a lower-priority task
     * while a higher-priority one is ready -- but that is an unwritten
     * invariant of this one build, not a guarantee this code can lean on.
     * v2/hw runs ESP-IDF FreeRTOS, which is dual-core SMP, where an app
     * task genuinely executes at the same instant as the router task on
     * the other core. It is masked there today only because
     * hal_display_create_canvas is a stub on hw that never allocates, so
     * the overlay never opens on hw at all -- not because the race can't
     * happen. */
    void * overlay = kernel_overlay_canvas();
    if( overlay != NULL )
    {
        gfx_blit_canvas_keyed( overlay, 0, 0, ACID_OVERLAY_KEY );
    }

    /* Everything above went into an offscreen back buffer, not the
     * screen -- this is the one call that makes the finished frame
     * visible, all at once. See gfx.h's gfx_present for why compositing
     * straight onto the screen flickered. */
    gfx_present();
}

void
kernel_router_set_desktop_task( void * task )
{
    g_desktop_task = task;
}

void
kernel_router_activate_window( void * task )
{
    /* Raising a window and shifting focus is now a pure bookkeeping
     * change -- the next composite tick (at most 16ms away) already
     * reflects whatever z-order/focus is current, so there's no repaint
     * to trigger here, and no need to special-case "already topmost"
     * either (that used to matter only because every activation used to
     * force an immediate, visible repaint of its own). */
    kernel_window_bring_to_front( task );
    g_focus_task = task;
}

/* Closes any window by task handle, not just the caller's own -- the
 * fresh_press close-button hit-test below was the only caller until a
 * system-monitor app needed to end an app it doesn't own from its own
 * window list (see window_binding.c's acid_close_window), same idea as
 * the reference OS's own task-list [X] button, just targeting acid OS
 * v2's windows instead of generic OS tasks. */
void
kernel_router_close_window( void * task )
{
    struct kernel_window * win = kernel_window_by_task( task );
    if( win == NULL )
    {
        return;
    }
    send_event( win, KERNEL_EVENT_CLOSE, 0, 0, 0 );
    kernel_window_unregister( task );
    if( g_focus_task == task )
    {
        /* Don't leave a dead task handle as the keyboard focus target. A
         * later fresh press elsewhere, or a taskbar tap, will pick a real
         * one; until then key events are simply dropped -- the same
         * "no window, no-op" behavior every other nowhere-to-deliver
         * input path in this file already has. */
        g_focus_task = NULL;
    }
    if( g_touch_task == task )
    {
        /* Same reasoning as the focus handle above: don't leave a dead
         * task as the gesture's owner. kernel_window_by_task would return
         * NULL for it anyway, but clearing it here keeps "a gesture is in
         * progress" and "that window still exists" from disagreeing. */
        g_touch_task = NULL;
    }
    /* No repaint call needed -- kernel_window_unregister already marked
     * this window not-in-use (gfx_mark_dirty included), so the very next
     * composite tick simply stops blitting it, exposing whatever's
     * underneath (or background) on its own. The canvas itself isn't
     * freed here -- see kernel_window_unregister's own doc comment for
     * why that now only ever happens on the window's own owning task. */
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
send_event( struct kernel_window * win, int type, int x, int y, int pressed )
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
    /* Non-blocking: every event this router sends is either high-frequency
     * (a dropped sample just means the next tick's send supersedes it) or,
     * for KERNEL_EVENT_CLOSE, going to a queue that was just created and so
     * can never be full. There's no longer a MOVED event whose delivery
     * the compositor needs to guarantee -- repositioning doesn't need the
     * app to know about it at all any more (see kernel_router_composite_
     * frame's own comment). */
    xQueueSendToBack( ( QueueHandle_t ) win->queue, &ev, 0 );
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

    /* g_touch_task == NULL in the condition: the strip's special case
     * claims a gesture that BEGINS in it, never one already owned by a
     * window whose finger has since wandered up here. */
    if( g_desktop_task != NULL && g_drag_mode == DRAG_NONE && g_touch_task == NULL
        && g_last_y < KERNEL_DESKTOP_STRIP_H )
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
            /* Drag ending (release, or the window vanished mid-drag).
             * Nothing to repaint here any more -- win->x/y is already
             * wherever the drag left it, and the next composite tick
             * picks that up on its own (see kernel_router_composite_
             * frame). */
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
            /* A plain position change never touches any canvas, so
             * nothing else would tell the compositor a recomposite is
             * needed -- see gfx_mark_dirty's own comment. */
            gfx_mark_dirty();
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
                    kernel_router_close_window( win->task );
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
        g_touch_task = win->task;
        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
    }

    if( pressed )
    {
        /* Deliberately NOT kernel_window_find_at( x, y ) -- see
         * g_touch_task. A gesture with no owner (it began on a close
         * button, or in the strip, or over nothing) delivers nothing
         * rather than leaking into whichever window is under the finger
         * now. */
        struct kernel_window * win = kernel_window_by_task( g_touch_task );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 1 );
        }
        return;
    }

    if( fresh_release )
    {
        /* The release belongs to the window that got the press, even if
         * the finger has since moved off it -- an app that tracks its own
         * press/release pairs (editor/touch.rb) would otherwise never see
         * the end of a gesture that wandered, and would treat the next
         * unrelated press as a continuation of it. */
        struct kernel_window * win = kernel_window_by_task( g_touch_task );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, g_last_x - win->x, g_last_y - win->y, 0 );
        }
        g_touch_task = NULL;
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
        /* Only recomposite when something actually changed since the last
         * tick -- see gfx_mark_dirty's own comment for why unconditional
         * per-tick compositing (this used to just call
         * kernel_router_composite_frame() outright, every tick) was itself
         * the cause of a real, confirmed bug: it slowed this loop down
         * enough, under real load, to silently drop fast keydown/keyup
         * transitions. */
        if( gfx_take_dirty() )
        {
            kernel_router_composite_frame();
            g_frames_composited++;
        }
        else
        {
            g_frames_skipped++;
        }
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}

int
kernel_router_composited_frames( void )
{
    return g_frames_composited;
}

int
kernel_router_skipped_frames( void )
{
    return g_frames_skipped;
}
