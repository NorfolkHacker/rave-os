#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "kernel_spawn.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "../vm_host/vm_host.h"

void *
kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable )
{
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

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = script_path;
    params->queue = queue;
    params->redraw_done_sem = redraw_done_sem;
    params->window_x = x;
    params->window_y = y;
    params->window_w = w;
    params->window_h = h;

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate( vm_host_task, script_path, 8192, params,
                                  tskIDLE_PRIORITY + 1, &task );
    if( ok != pdPASS )
    {
        vSemaphoreDelete( redraw_done_sem );
        vQueueDelete( queue );
        vPortFree( params );
        return NULL;
    }

    kernel_window_register( ( void * ) task, ( void * ) queue, ( void * ) redraw_done_sem,
                             script_path, x, y, w, h, closable );
    return ( void * ) task;
}
