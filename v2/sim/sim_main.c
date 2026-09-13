#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

extern void sim_gfx_init( void );
extern void sim_gfx_fill_rect( int x, int y, int w, int h, unsigned int color );
extern int sim_should_quit( void );

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

static void print_task( void * pvParameters )
{
    ( void ) pvParameters;
    sim_gfx_init();
    int x = 0;
    for( ;; )
    {
        if( sim_should_quit() )
        {
            exit( 0 );
        }
        printf( "acid OS v2 sim: FreeRTOS task alive\n" );
        fflush( stdout );
        sim_gfx_fill_rect( x % 260, 90, 60, 60, 0xF800 );
        x += 10;
        vTaskDelay( pdMS_TO_TICKS( 200 ) );
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
