#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/error.h"

#include "vm_host.h"
#include "../bindings/gfx_binding.h"
#include "../bindings/event_binding.h"
#include "../bindings/chrome_binding.h"
#include "../bindings/audio_binding.h"
#include "../bindings/window_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_audio.h"

/* Loaded into every app's VM before its own script, so AcidApp is always
 * defined -- the vendored mruby's default gembox (mrbgems/default.gembox)
 * has no require/require_relative gem, so the host loads framework code and
 * app code as two separate sequential mrb_load_detect_file_cxt calls into
 * the same VM instance instead. */
#define ACID_APP_LIB_PATH "v2/apps/lib/acid_app.rb"
#define ACID_GAME_LIB_PATH "v2/apps/lib/acid_game.rb"
#define ACID_KEYS_LIB_PATH "v2/apps/lib/acid_keys.rb"

static SemaphoreHandle_t g_parse_lock = NULL;

static void
load_file_into_vm( mrb_state * mrb, mrb_ccontext * cxt, const char * path )
{
    FILE * fp = fopen( path, "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "acid OS v2: could not open %s\n", path );
        return;
    }
    mrb_load_detect_file_cxt( mrb, fp, cxt );
    if( mrb->exc )
    {
        mrb_print_error( mrb );
        mrb->exc = NULL;
    }
    fclose( fp );
}

void
vm_host_init( void )
{
    g_parse_lock = xSemaphoreCreateMutex();
}

void
vm_host_task( void * pvParameters )
{
    struct vm_host_params * params = ( struct vm_host_params * ) pvParameters;

    struct kernel_app_context ctx;
    ctx.queue = params->queue;
    ctx.window_x = params->window_x;
    ctx.window_y = params->window_y;
    ctx.window_w = params->window_w;
    ctx.window_h = params->window_h;

    mrb_state * mrb = mrb_open();
    mrb->ud = &ctx;
    acid_bindings_register( mrb );
    acid_event_bindings_register( mrb );
    acid_chrome_bindings_register( mrb );
    acid_audio_bindings_register( mrb );
    acid_window_bindings_register( mrb );

    mrb_ccontext * cxt = mrb_ccontext_new( mrb );
    /* mruby's Prism parser is not thread-safe -- concurrent parsing across
     * different app VMs' own pthreads (this project's POSIX port backs
     * each FreeRTOS task with a real pthread) corrupts shared parser
     * state, reproduced as both a SIGSEGV in pm_constant_pool_insert and a
     * SIGABRT assertion in pm_newline_list_line. Serializing every VM's
     * parse/load phase behind one global mutex is the fix -- cheap (parse
     * time for these small files is negligible) and doesn't require
     * understanding Prism's internals. */
    xSemaphoreTake( g_parse_lock, portMAX_DELAY );
    load_file_into_vm( mrb, cxt, ACID_KEYS_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_APP_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_GAME_LIB_PATH );
    load_file_into_vm( mrb, cxt, params->script_path );
    xSemaphoreGive( g_parse_lock );
    mrb_ccontext_free( mrb, cxt );
    mrb_close( mrb );

    /* Bring-up's vm_host_task parked forever here (instead of returning)
     * purely to keep something alive checking hal_input_should_quit(),
     * since it was the only task in the system and vTaskStartScheduler()
     * never returns on its own. That's no longer this task's job: from
     * Task 4 onward, kernel_router_task is the one permanent task doing
     * that (and acid_poll_event's own hal_input_should_quit() check
     * already covers it before then, per Task 3's own verification step).
     * A script ending -- whether by closing normally or by faulting, with
     * mrb->exc already handled by load_file_into_vm above -- now just
     * cleanly frees this one app's resources instead of leaking a
     * forever-parked task; other apps and the router are unaffected
     * either way, which is the spec's actual fault-containment
     * requirement, not the parking loop itself. */

    /* Unconditional cleanup covering EVERY exit path (normal script end,
     * unhandled exception, or load_file_into_vm failing to open a file) --
     * not just the close-button path, which was previously the only caller
     * of kernel_window_unregister anywhere in the codebase. Safe to call
     * unconditionally: kernel_window_unregister is a no-op if this task's
     * window was already unregistered (e.g. by the close-button path,
     * kernel_router.c, before this task got here). The queue, by contrast,
     * is deleted from exactly this one place for every app -- the
     * close-button path only unregisters the window, it never deletes the
     * queue -- so this is the sole deletion point and safe to call once,
     * unconditionally, here. */
    kernel_window_unregister( ( void * ) xTaskGetCurrentTaskHandle() );
    kernel_audio_release_owner( ( void * ) xTaskGetCurrentTaskHandle() );
    vQueueDelete( params->queue );

    /* vPortFree, not free -- params was allocated with pvPortMalloc in
     * kernel_spawn.c; mismatching the allocator/deallocator pair happens to
     * work under the sim's heap_3.c (a thin wrapper over libc malloc/free)
     * but is wrong by contract and real heap corruption under FreeRTOS's
     * heap_4/heap_5 allocators. */
    vPortFree( params );
    vTaskDelete( NULL );
}
