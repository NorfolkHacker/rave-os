#ifndef ACID_KERNEL_WINDOW_H
#define ACID_KERNEL_WINDOW_H

#define KERNEL_WINDOW_MAX 8

struct kernel_window
{
    void * task;          /* opaque TaskHandle_t */
    void * queue;          /* opaque QueueHandle_t */
    /* Unused by the router any more (the compositor no longer waits on
     * apps to redraw -- see kernel_router.c's kernel_router_composite_frame),
     * but kept: acid_notify_redraw_done still posts it harmlessly, and
     * removing the whole plumbing (kernel_spawn.c, vm_host.c, acid_app.rb,
     * acid_game.rb) isn't needed just to land the compositor fix. */
    void * redraw_done_sem;
    /* This window's private offscreen pixel buffer -- see hal_display.h's
     * own comment. Every app draws into this (never the real screen
     * directly), using its own window-relative coordinates; the router's
     * compositor is the only thing that ever blits it onto the visible
     * screen. Created in kernel_window_register, destroyed in
     * kernel_window_unregister. Opaque outside the gfx layer -- treat as
     * a plain handle, pass straight through to gfx_blit_canvas. */
    void * canvas;
    const char * app_name;
    int x, y, w, h;
    int z_order;
    int closable;
    int in_use;
};

void kernel_window_init( void );
int kernel_window_register( void * task, void * queue, void * redraw_done_sem, void * canvas,
                             const char * app_name,
                             int x, int y, int w, int h, int closable );
void kernel_window_unregister( void * task );
struct kernel_window * kernel_window_find_at( int x, int y );
struct kernel_window * kernel_window_by_task( void * task );
void kernel_window_bring_to_front( void * task );
int kernel_window_count( void );
struct kernel_window * kernel_window_at_index( int index );

/* Returns the in-use window with the smallest z_order strictly greater
 * than `after_z`, or NULL if none -- lets a caller walk every window
 * back-to-front (oldest-raised first) without exposing the internal
 * array. Pass -1 to start from the very back. Used by the router's
 * dirty-rect repaint (kernel_router_repaint_rect). */
struct kernel_window * kernel_window_next_by_z( int after_z );

/* Returns the in-use window with the largest z_order (whatever is
 * currently drawn frontmost), or NULL if no window is registered at all. */
struct kernel_window * kernel_window_topmost( void );

/* Opposite of kernel_window_bring_to_front: reassigns this window's
 * z_order below every other in-use window's, so it's drawn first (and
 * loses every future hit-test) until something else raises it again.
 * A no-op if the task has no window. */
void kernel_window_send_to_back( void * task );

#endif
