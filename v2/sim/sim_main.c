#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "../core/vm_host/vm_host.h"
#include "../core/kernel/kernel_event.h"
#include "../core/hal/hal_input.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

static QueueHandle_t g_demo_queue;

static void
input_feed_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        int x, y;
        bool pressed;
        hal_input_poll_touch( &x, &y, &pressed );
        if( pressed )
        {
            struct kernel_event ev;
            ev.type = KERNEL_EVENT_TOUCH;
            ev.x = x;
            ev.y = y;
            ev.pressed = 1;
            xQueueSendToBack( g_demo_queue, &ev, 0 );
        }
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}

void
sim_freertos_main( void )
{
    g_demo_queue = xQueueCreate( 8, sizeof( struct kernel_event ) );

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = "v2/apps/demo_touch.rb";
    params->queue = g_demo_queue;
    params->window_x = 0;
    params->window_y = 0;
    params->window_w = 320;
    params->window_h = 240;

    xTaskCreate( vm_host_task, "demo", 8192, params, tskIDLE_PRIORITY + 1, NULL );
    xTaskCreate( input_feed_task, "input_feed", 4096, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
