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

/* Capped at desktop.rb's own MAX_LAUNCHER_ITEMS (currently 6, the
 * dropdown's own row capacity -- independent of the taskbar's narrower
 * MAX_TASKBAR_SLOTS, since dropdown rows are full-width, not
 * BUTTON_W-wide columns). acid_blaster.rb is a continuously
 * self-redrawing window (an AcidGame, not a static AcidApp); it's only
 * safe to list here now that it skips its own per-tick redraw while
 * covered (see acid_blaster.rb's on_tick and AcidApp#focused?) --
 * before that fix, it would repaint over whatever window was actually
 * on top of it, every tick, with no z-order awareness at all. */
static const struct launchable_app g_launchable[] = {
    { "v2/apps/demo_touch.rb", 140, 100 },
    { "v2/apps/demo_swatch.rb", 140, 100 },
    { "v2/apps/file_manager.rb", 220, 160 },
    { "v2/apps/editor.rb", 240, 170 },
    { "v2/apps/acid_blaster.rb", 250, 180 },
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

/* True if the CALLING app's own window currently holds keyboard focus
 * (xTaskGetCurrentTaskHandle, the same self-detection pattern
 * acid_send_self_to_back already uses) -- in this codebase, focus and
 * being the topmost/frontmost window always change together
 * (kernel_router_activate_window sets both in the same call, and nothing
 * else changes either independently), so this doubles as "am I the
 * window actually visible on top right now." AcidGame uses this to skip
 * its own per-tick self-redraw while covered: unlike a static window's
 * redraw (which only ever runs inside the compositor's own z-order-aware
 * repaint), a game repaints itself directly, every tick, with no z-order
 * awareness at all -- if it kept doing that while some other window was
 * genuinely on top, it would just paint over that window's visible
 * content on every single tick. */
static mrb_value
acid_am_i_focused( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_bool_value( kernel_router_get_focus() == ( void * ) xTaskGetCurrentTaskHandle() );
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
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_am_i_focused",
                                 acid_am_i_focused, MRB_ARGS_NONE() );
}
