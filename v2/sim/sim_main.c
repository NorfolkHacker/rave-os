#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../core/kernel/kernel_window.h"
#include "../core/kernel/kernel_spawn.h"
#include "../core/kernel/kernel_router.h"
#include "../core/gfx/gfx.h"
#include "../core/kernel/kernel_audio.h"
#include "../core/hal/hal_audio.h"
#include "../core/vm_host/vm_host.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

void
sim_freertos_main( void )
{
    kernel_window_init();
    vm_host_init();
    gfx_init();
    kernel_audio_init();
    hal_audio_init();

    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 24, 0 );
    kernel_router_set_desktop_task( desktop_task );

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/acid_blaster.rb", 30, 40, 250, 180, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, tskIDLE_PRIORITY + 2, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
