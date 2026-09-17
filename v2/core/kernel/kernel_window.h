#ifndef ACID_KERNEL_WINDOW_H
#define ACID_KERNEL_WINDOW_H

#define KERNEL_WINDOW_MAX 8

struct kernel_window
{
    void * task;          /* opaque TaskHandle_t */
    void * queue;          /* opaque QueueHandle_t */
    const char * app_name;
    int x, y, w, h;
    int z_order;
    int closable;
    int in_use;
};

void kernel_window_init( void );
int kernel_window_register( void * task, void * queue, const char * app_name,
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
 * full-screen repaint (kernel_router_repaint_all). */
struct kernel_window * kernel_window_next_by_z( int after_z );

#endif
