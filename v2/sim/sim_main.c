#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

static void print_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        printf( "acid OS v2 sim: FreeRTOS task alive\n" );
        fflush( stdout );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void sim_freertos_main( void )
{
    xTaskCreate( print_task, "print", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    /* vTaskStartScheduler() only returns if there was insufficient heap
     * to create the idle/timer tasks. */
    for( ;; ) {}
}

int main( void )
{
    sim_freertos_main();
    return 0;
}
