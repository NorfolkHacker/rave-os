#ifndef ACID_KERNEL_WINDOW_H
#define ACID_KERNEL_WINDOW_H

#define KERNEL_WINDOW_MAX 8

struct kernel_window
{
    void * task;          /* opaque TaskHandle_t */
    void * queue;          /* opaque QueueHandle_t */
    /* opaque SemaphoreHandle_t (binary), given by the app itself right
     * after it finishes handling a KERNEL_EVENT_MOVED-triggered redraw --
     * see acid_notify_redraw_done. Every app on screen draws on its own
     * real FreeRTOS task/pthread with no ordering between them otherwise;
     * the router takes this after each send in kernel_router_repaint_all
     * so a window's redraw is confirmed FINISHED before the next window
     * (which may visually overlap it) starts its own -- without this,
     * back-to-front z-order is only the ORDER EVENTS ARE SENT IN, not the
     * order drawing actually completes in, so a higher window's draw can
     * finish before a lower one's and get silently painted over. */
    void * redraw_done_sem;
    const char * app_name;
    int x, y, w, h;
    int z_order;
    int closable;
    int in_use;
};

void kernel_window_init( void );
int kernel_window_register( void * task, void * queue, void * redraw_done_sem,
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
