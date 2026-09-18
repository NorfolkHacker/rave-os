#include "window_binding.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_router.h"
#include "../kernel/kernel_spawn.h"
#include "../kernel/kernel_layout.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mruby/array.h"

/* Apps the launcher menu can start. A fixed, C-owned table rather than
 * taking a path string from Ruby: kernel_spawn_app stores the pointer it's
 * given (app_name in kernel_window, script_path in vm_host_params) for the
 * ENTIRE lifetime of the spawned task, well past this binding call
 * returning -- a Ruby-string's char* would be safe only until mruby's GC
 * next runs. C string literals live forever, so indexing into this table
 * is the only safe way to let Ruby choose what to launch. */
struct launchable_app
{
    const char * path;
    int w;
    int h;
};

/* Capped at desktop.rb's own MAX_TASKBAR_SLOTS (currently 4, one taskbar
 * row's worth before entries would start drawing under the reserved
 * Menu/Back slot) -- acid_blaster.rb deliberately isn't in this table for
 * that reason (a 5th entry has nowhere on screen to go without adding
 * pagination, which is out of scope for now). It's still a fully working
 * app, just not launchable from this menu yet. */
static const struct launchable_app g_launchable[] = {
    { "v2/apps/demo_touch.rb", 140, 100 },
    { "v2/apps/demo_swatch.rb", 140, 100 },
    { "v2/apps/file_manager.rb", 220, 160 },
    { "v2/apps/editor.rb", 240, 170 },
};
#define LAUNCHABLE_COUNT ( sizeof( g_launchable ) / sizeof( g_launchable[ 0 ] ) )

static mrb_value
acid_launcher_count( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( ( mrb_int ) LAUNCHABLE_COUNT );
}

static mrb_value
acid_launcher_path( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || ( size_t ) index >= LAUNCHABLE_COUNT )
    {
        return mrb_nil_value();
    }
    /* mrb_str_new_cstr copies into a fresh Ruby String -- safe regardless
     * of the source literal's own lifetime, which outlives the process
     * anyway. */
    return mrb_str_new_cstr( mrb, g_launchable[ index ].path );
}

static mrb_value
acid_launcher_spawn( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || ( size_t ) index >= LAUNCHABLE_COUNT )
    {
        return mrb_bool_value( 0 );
    }

    /* Simple cascade so successively launched windows don't all land
     * exactly on top of each other -- based on how many windows already
     * exist, wrapped so it stays roughly on screen regardless of count. */
    int n = kernel_window_count();
    int x = 20 + ( ( n * 18 ) % 140 );
    int y = KERNEL_DESKTOP_STRIP_H + 10 + ( ( n * 18 ) % 90 );

    void * task = kernel_spawn_app( g_launchable[ index ].path, x, y,
                                     g_launchable[ index ].w, g_launchable[ index ].h, 1 );
    if( task != NULL )
    {
        kernel_router_activate_window( task );
    }
    return mrb_bool_value( task != NULL );
}

/* Lets the CALLING app's own window drop to the very back of the z-order
 * -- the opposite of acid_activate_window, and always targets the caller
 * itself (xTaskGetCurrentTaskHandle), the same self-detection pattern
 * kernel_router_repaint_rect already uses, so there's no way to send some
 * OTHER window to the back by mistake. Desktop.rb's dropdown menu is the
 * only user so far: it temporarily raises itself and claims a taller
 * clickable area to show the dropdown, then needs to give both back
 * cleanly on close so it stops winning hit-tests for windows that are
 * actually there normally. */
static mrb_value
acid_send_self_to_back( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    kernel_window_send_to_back( ( void * ) xTaskGetCurrentTaskHandle() );
    return mrb_nil_value();
}

static mrb_value
acid_repaint_region( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h;
    mrb_get_args( mrb, "iiii", &x, &y, &w, &h );
    kernel_router_repaint_region( ( int ) x, ( int ) y, ( int ) w, ( int ) h );
    return mrb_nil_value();
}

static mrb_value
acid_window_max( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( KERNEL_WINDOW_MAX );
}

static mrb_value
acid_window_info( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );

    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return mrb_nil_value();
    }

    struct kernel_window * win = kernel_window_at_index( ( int ) index );
    if( win == NULL || !win->in_use )
    {
        return mrb_nil_value();
    }

    mrb_value values[ 6 ];
    values[ 0 ] = mrb_str_new_cstr( mrb, win->app_name );
    values[ 1 ] = mrb_fixnum_value( win->x );
    values[ 2 ] = mrb_fixnum_value( win->y );
    values[ 3 ] = mrb_fixnum_value( win->w );
    values[ 4 ] = mrb_fixnum_value( win->h );
    values[ 5 ] = mrb_bool_value( win->task == kernel_router_get_focus() );
    return mrb_ary_new_from_values( mrb, 6, values );
}

static mrb_value
acid_activate_window( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );

    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return mrb_nil_value();
    }

    struct kernel_window * win = kernel_window_at_index( ( int ) index );
    if( win != NULL && win->in_use )
    {
        kernel_router_activate_window( win->task );
    }
    return mrb_nil_value();
}

void
acid_window_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_window_max",
                                 acid_window_max, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_window_info",
                                 acid_window_info, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_activate_window",
                                 acid_activate_window, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_count",
                                 acid_launcher_count, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_path",
                                 acid_launcher_path, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_spawn",
                                 acid_launcher_spawn, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_send_self_to_back",
                                 acid_send_self_to_back, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_repaint_region",
                                 acid_repaint_region, MRB_ARGS_REQ( 4 ) );
}
