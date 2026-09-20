#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "kernel_spawn.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "../vm_host/vm_host.h"
#include "../gfx/gfx.h"

void *
kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable,
                   const char * arg, const char * libs )
{
    /* Checked BEFORE creating the task, not after: xTaskCreate can start
     * the task running immediately (this is a pthread underneath, on the
     * sim), so if registration were attempted only after task creation
     * and found the table full, cleaning up would mean deleting a task
     * that may already be mid-draw -- possibly holding gfx's own lock,
     * which vTaskDelete would then leave stuck forever. Simpler and
     * fully safe: never create the task at all if there's nowhere for it
     * to go. Only reachable once app launching becomes dynamic (the
     * menu) -- the fixed boot set in app_main.c never exceeds the
     * table. */
    if( kernel_window_count() >= KERNEL_WINDOW_MAX )
    {
        return NULL;
    }

    QueueHandle_t queue = xQueueCreate( 8, sizeof( struct kernel_event ) );
    if( queue == NULL )
    {
        return NULL;
    }

    /* Binary semaphore, starts empty: only the app itself ever gives it
     * (once, right after finishing a moved-triggered redraw), and only
     * kernel_router_repaint_all ever takes it. */
    SemaphoreHandle_t redraw_done_sem = xSemaphoreCreateBinary();
    if( redraw_done_sem == NULL )
    {
        vQueueDelete( queue );
        return NULL;
    }

    /* This window's own private offscreen canvas (see hal_display.h) --
     * every draw call the app makes goes here, never straight to the real
     * screen; the router's compositor blits it onto the screen every
     * frame (kernel_router.c's kernel_router_composite_frame). */
    void * canvas = gfx_create_canvas( w, h );
    if( canvas == NULL )
    {
        vSemaphoreDelete( redraw_done_sem );
        vQueueDelete( queue );
        return NULL;
    }

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = script_path;
    params->queue = queue;
    params->redraw_done_sem = redraw_done_sem;
    params->canvas = canvas;
    params->arg = arg;
    params->libs = libs;
    params->window_x = x;
    params->window_y = y;
    params->window_w = w;
    params->window_h = h;

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate( vm_host_task, script_path, 8192, params,
                                  tskIDLE_PRIORITY + 1, &task );
    if( ok != pdPASS )
    {
        gfx_destroy_canvas( canvas );
        vSemaphoreDelete( redraw_done_sem );
        vQueueDelete( queue );
        vPortFree( params );
        return NULL;
    }

    /* Guaranteed to succeed: the capacity check at the top of this
     * function already reserved a slot's worth of headroom, and nothing
     * else registers windows concurrently. */
    kernel_window_register( ( void * ) task, ( void * ) queue, ( void * ) redraw_done_sem, canvas,
                             script_path, x, y, w, h, closable );
    return ( void * ) task;
}
