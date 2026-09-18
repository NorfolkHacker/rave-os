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

    /* Height is taller than the visible strip (24px) on purpose: desktop.rb's
     * launcher dropdown needs its clickable area to extend down to where it
     * actually draws (24 + 4 entries * 18px = 96) -- see desktop.rb's
     * DROPDOWN_H/TOTAL_H comment for the full explanation. Must match that
     * constant exactly. */
    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 96, 0 );
    kernel_router_set_desktop_task( desktop_task );

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );
    /* acid_blaster is the only continuously self-redrawing window in this
     * boot set; on this screen size its window geometrically cannot avoid
     * overlapping file_manager/editor, and unlike two static windows
     * overlapping (fully fixed by kernel_router_activate_window's new
     * repaint-on-raise, see kernel_router.c), an animating window keeps
     * re-covering whatever's under it every tick regardless of z-order.
     * Not spawned by default for now -- still a fully working app, just
     * not auto-launched alongside five other apps on a small screen. See
     * the phase 5 final review's finding on this. */
    kernel_spawn_app( "v2/apps/file_manager.rb", 40, 50, 220, 160, 1 );
    void * editor_task = kernel_spawn_app( "v2/apps/editor.rb", 60, 60, 240, 170, 1 );

    /* Without this, boot left g_focus_task NULL -- no taskbar entry ever
     * highlighted until the user clicked something, even though a window
     * (editor, spawned last) was already visually on top. Every real
     * desktop starts with something focused; this just makes that
     * already-true z-order state explicit. Safe to call before the
     * router/scheduler start: editor is already topmost from registration
     * order, so this only sets focus, no repaint is attempted. */
    if( editor_task != NULL )
    {
        kernel_router_activate_window( editor_task );
    }

    xTaskCreate( kernel_router_task, "router", 4096, NULL, tskIDLE_PRIORITY + 2, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
