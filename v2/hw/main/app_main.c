#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../core/kernel/kernel_window.h"
#include "../../core/kernel/kernel_spawn.h"
#include "../../core/kernel/kernel_router.h"
#include "../../core/kernel/kernel_audio.h"
#include "../../core/gfx/gfx.h"
#include "../../core/hal/hal_audio.h"
#include "../../core/vm_host/vm_host.h"

void
app_main( void )
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
    kernel_spawn_app( "v2/apps/editor.rb", 60, 60, 240, 170, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, 5, NULL );
}
