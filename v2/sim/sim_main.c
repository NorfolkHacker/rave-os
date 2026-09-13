#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../core/vm_host/vm_host.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

void sim_freertos_main( void )
{
    xTaskCreate( vm_host_task, "vm_host", 8192, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
