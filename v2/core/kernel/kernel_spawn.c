#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

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

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = script_path;
    params->queue = queue;
    params->window_x = x;
    params->window_y = y;
    params->window_w = w;
    params->window_h = h;

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate( vm_host_task, script_path, 8192, params,
                                  tskIDLE_PRIORITY + 1, &task );
    if( ok != pdPASS )
    {
        vQueueDelete( queue );
        vPortFree( params );
        return NULL;
    }

    kernel_window_register( ( void * ) task, ( void * ) queue, script_path,
                             x, y, w, h, closable );
    return ( void * ) task;
}
