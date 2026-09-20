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

    /* Height is taller than the visible strip (24px) on purpose: desktop.rb's
     * launcher dropdown needs its clickable area to extend down to where it
     * actually draws (24 + 10 entries * 18px = 204) -- see desktop.rb's
     * DROPDOWN_H/TOTAL_H comment for the full explanation. Must match that
     * constant exactly. */
    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 640, 204, 0, NULL, NULL );
    kernel_router_set_desktop_task( desktop_task );

    /* Boot used to also auto-spawn demo_touch/demo_swatch/file_manager/
     * editor here, so there was always something to test the launcher
     * against before it could discover apps itself. Now that desktop.rb
     * scans v2/apps for *.app.toml manifests at boot (see its own
     * comment), that crutch just clutters a fresh boot with four windows
     * nobody asked to open -- every app is reachable from Menu instead. */

    xTaskCreate( kernel_router_task, "router", 4096, NULL, 5, NULL );
}
