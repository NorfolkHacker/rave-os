#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/error.h"

#include "vm_host.h"
#include "../bindings/gfx_binding.h"

extern void sim_gfx_init( void );
extern int sim_should_quit( void );

void
vm_host_task( void * pvParameters )
{
    ( void ) pvParameters;

    sim_gfx_init();

    mrb_state * mrb = mrb_open();
    acid_bindings_register( mrb );

    mrb_ccontext * cxt = mrb_ccontext_new( mrb );
    FILE * fp = fopen( "v2/apps/hello.rb", "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "acid OS v2: could not open v2/apps/hello.rb\n" );
    }
    else
    {
        mrb_load_detect_file_cxt( mrb, fp, cxt );
        if( mrb->exc )
        {
            mrb_print_error( mrb );
        }
        fclose( fp );
    }

    mrb_ccontext_free( mrb, cxt );
    mrb_close( mrb );

    /* The VM run is done (or faulted, per the error handling above, which
     * already contains the fault to this task). Per the spec, bring-up does
     * no restart/recovery -- just keep the task parked, watching for the
     * sim window to close, since sim_freertos_main()/vTaskStartScheduler()
     * never returns on their own otherwise. */
    for( ;; )
    {
        if( sim_should_quit() )
        {
            exit( 0 );
        }
        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
}
