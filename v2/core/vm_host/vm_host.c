#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/error.h"
#include "mruby/proc.h"

#include "vm_host.h"
#include "../bindings/gfx_binding.h"
#include "../bindings/event_binding.h"
#include "../bindings/chrome_binding.h"
#include "../bindings/audio_binding.h"
#include "../bindings/window_binding.h"
#include "../bindings/task_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_audio.h"
#include "../kernel/kernel_router.h"

/* Loaded into every app's VM before its own script, so AcidApp is always
 * defined -- the vendored mruby's default gembox (mrbgems/default.gembox)
 * has no require/require_relative gem, so the host loads framework code and
 * app code as two separate sequential mrb_load_detect_file_cxt calls into
 * the same VM instance instead. */
#define ACID_APP_LIB_PATH "v2/apps/lib/acid_app.rb"
#define ACID_GAME_LIB_PATH "v2/apps/lib/acid_game.rb"
#define ACID_KEYS_LIB_PATH "v2/apps/lib/acid_keys.rb"
#define ACID_PALETTE_LIB_PATH "v2/apps/lib/acid_palette.rb"

/* mruby's Prism parser/codegen are not thread-safe -- concurrent parsing
 * across different app VMs' own pthreads (this project's POSIX port backs
 * each FreeRTOS task with a real pthread) corrupts shared parser state,
 * reproduced as both a SIGSEGV in pm_constant_pool_insert and a SIGABRT
 * assertion in pm_newline_list_line. This mutex serializes only the
 * parse+codegen phase (parse_and_compile below) across VMs -- cheap, since
 * parse time for these small files is negligible. It must NOT be held
 * across execution (run_compiled below): an app's own script's top-level
 * statement is typically AppClass.new.start, which runs that app's entire
 * event loop and never returns, so holding this lock across execution
 * would serialize apps' entire lifetimes instead of just their brief
 * parse phase. */
static SemaphoreHandle_t g_parse_lock = NULL;

/* Parses and compiles `s` (length `len`) into a runnable proc. Must only be
 * called while holding g_parse_lock -- mruby's Prism parser/codegen are not
 * safe to run concurrently across this project's per-app VM pthreads (see
 * the comment on g_parse_lock's own declaration). Returns NULL (with
 * mrb->exc set) on a parse or codegen failure. This function only compiles
 * -- it never executes anything, so it always returns quickly regardless
 * of what the source code does at runtime. */
static struct RProc *
parse_and_compile( mrb_state * mrb, const char * s, size_t len, mrb_ccontext * c )
{
    struct mrb_parser_state * p = mrb_parse_nstring( mrb, s, len, c );
    if( p == NULL )
    {
        if( mrb->exc == NULL )
        {
            mrb->exc = mrb_obj_ptr( mrb_exc_new_lit( mrb, E_SCRIPT_ERROR, "cannot load source" ) );
        }
        return NULL;
    }
    if( p->tree == NULL || p->nerr )
    {
        if( c != NULL )
        {
            c->parser_nerr = p->nerr;
        }
        if( mrb->exc == NULL )
        {
            const char * message = "syntax error";
            if( p->error_buffer[ 0 ].message )
            {
                message = p->error_buffer[ 0 ].message;
            }
            mrb->exc = mrb_obj_ptr( mrb_exc_new( mrb, E_SYNTAX_ERROR, message, strlen( message ) ) );
        }
        mrb_parser_free( p );
        return NULL;
    }

    struct RProc * proc = mrb_generate_code( mrb, p );
    mrb_parser_free( p );
    if( proc == NULL && mrb->exc == NULL )
    {
        mrb->exc = mrb_obj_ptr( mrb_exc_new_lit( mrb, E_SCRIPT_ERROR, "codegen error" ) );
    }
    return proc;
}

/* Runs a proc already compiled by parse_and_compile. Safe to call WITHOUT
 * holding g_parse_lock -- pure bytecode execution touches only this
 * mrb_state's own VM state, never mruby's shared Prism parser/codegen
 * state, so it's fine for this to block forever (as an app's own
 * AppClass.new.start does) while other VMs' parse_and_compile calls
 * proceed serialized through the lock in the meantime. */
static mrb_value
run_compiled( mrb_state * mrb, struct RProc * proc, mrb_ccontext * c )
{
    struct RClass * target = mrb->object_class;
    mrb_int keep = 0;

    if( c != NULL )
    {
        if( c->no_exec )
        {
            return mrb_obj_value( proc );
        }
        if( c->target_class )
        {
            target = c->target_class;
        }
        if( c->keep_lv )
        {
            keep = c->slen + 1;
        }
        else
        {
            c->keep_lv = TRUE;
        }
    }
    MRB_PROC_SET_TARGET_CLASS( proc, target );
    proc->flags |= MRB_PROC_CREF;
    if( mrb->c->ci )
    {
        mrb_vm_ci_target_class_set( mrb->c->ci, target );
    }
    return mrb_top_run( mrb, proc, mrb_top_self( mrb ), keep );
}

static void
load_file_into_vm( mrb_state * mrb, mrb_ccontext * cxt, const char * path )
{
    FILE * fp = fopen( path, "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "acid OS v2: could not open %s\n", path );
        return;
    }

    fseek( fp, 0, SEEK_END );
    long size = ftell( fp );
    fseek( fp, 0, SEEK_SET );
    char * buf = ( char * ) mrb_malloc( mrb, ( size_t ) size );
    size_t nread = fread( buf, 1, ( size_t ) size, fp );
    fclose( fp );

    xSemaphoreTake( g_parse_lock, portMAX_DELAY );
    struct RProc * proc = parse_and_compile( mrb, buf, nread, cxt );
    xSemaphoreGive( g_parse_lock );
    mrb_free( mrb, buf );

    if( proc != NULL )
    {
        run_compiled( mrb, proc, cxt );
    }

    if( mrb->exc )
    {
        mrb_print_error( mrb );
        mrb->exc = NULL;
    }
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
    ctx.redraw_done_sem = params->redraw_done_sem;
    ctx.canvas = params->canvas;
    ctx.arg = params->arg;
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
    acid_task_bindings_register( mrb );

    mrb_ccontext * cxt = mrb_ccontext_new( mrb );
    load_file_into_vm( mrb, cxt, ACID_KEYS_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_PALETTE_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_APP_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_GAME_LIB_PATH );
    load_file_into_vm( mrb, cxt, params->script_path );
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
    kernel_router_clear_focus( ( void * ) xTaskGetCurrentTaskHandle() );
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
