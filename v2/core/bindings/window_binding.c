#include <string.h>

#include "window_binding.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_router.h"
#include "../kernel/kernel_spawn.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_theme.h"
#include "../gfx/gfx.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mruby/array.h"

/* Apps the launcher menu can start. Populated at runtime by
 * acid_launcher_register (desktop.rb scans v2/apps for *.app.toml
 * manifests at boot and registers whatever it finds -- see its own
 * comment), NOT a fixed compile-time table anymore: dropping a new
 * <name>.rb + <name>.app.toml pair into that directory is enough to make
 * it launchable, no C change or rebuild required.
 *
 * Still C-owned storage, not a Ruby-string pointer taken directly:
 * kernel_spawn_app stores the pointer it's given (app_name in
 * kernel_window, script_path in vm_host_params) for the ENTIRE lifetime
 * of the spawned task, well past any binding call returning -- a
 * Ruby-string's char* would only be safe until mruby's next GC pass.
 * acid_launcher_register copies both strings into heap memory that's
 * never freed (matches every other "lives for the process" allocation
 * in this codebase, e.g. kernel_spawn.c's own vm_host_params), which is
 * the only way to let Ruby discover what's launchable at runtime while
 * keeping that same safety guarantee. */
struct launchable_app
{
    char * path;
    char * name;
    int w;
    int h;
};

/* Bounded, not a Ruby-sized dynamic array -- matches this codebase's own
 * style elsewhere (KERNEL_WINDOW_MAX, etc.). desktop.rb's own dropdown
 * only has room to ever show MAX_LAUNCHER_ITEMS (6) of these regardless;
 * this is deliberately a little larger so a scan that finds more doesn't
 * silently drop entries the UI might grow room for later. */
#define MAX_REGISTERED_APPS 16
static struct launchable_app g_registered[ MAX_REGISTERED_APPS ];
static int g_registered_count = 0;

static char *
dup_cstr( const char * src )
{
    size_t len = strlen( src ) + 1;
    char * copy = ( char * ) pvPortMalloc( len );
    if( copy != NULL )
    {
        memcpy( copy, src, len );
    }
    return copy;
}

static mrb_value
acid_launcher_register( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    char * path;
    mrb_int path_len;
    char * name;
    mrb_int name_len;
    mrb_int w, h;
    mrb_get_args( mrb, "ssii", &path, &path_len, &name, &name_len, &w, &h );
    ( void ) path_len;
    ( void ) name_len;

    if( g_registered_count >= MAX_REGISTERED_APPS )
    {
        return mrb_bool_value( 0 );
    }

    struct launchable_app * slot = &g_registered[ g_registered_count ];
    slot->path = dup_cstr( path );
    slot->name = dup_cstr( name );
    if( slot->path == NULL || slot->name == NULL )
    {
        return mrb_bool_value( 0 );
    }
    slot->w = ( int ) w;
    slot->h = ( int ) h;
    g_registered_count++;
    return mrb_bool_value( 1 );
}

static mrb_value
acid_launcher_count( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( ( mrb_int ) g_registered_count );
}

static mrb_value
acid_launcher_path( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || index >= g_registered_count )
    {
        return mrb_nil_value();
    }
    /* mrb_str_new_cstr copies into a fresh Ruby String -- safe regardless
     * of the source's own lifetime, which outlives the process anyway. */
    return mrb_str_new_cstr( mrb, g_registered[ index ].path );
}

static mrb_value
acid_launcher_name( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || index >= g_registered_count )
    {
        return mrb_nil_value();
    }
    return mrb_str_new_cstr( mrb, g_registered[ index ].name );
}

static mrb_value
acid_launcher_spawn( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || index >= g_registered_count )
    {
        return mrb_bool_value( 0 );
    }

    /* Simple cascade so successively launched windows don't all land
     * exactly on top of each other -- based on how many windows already
     * exist, wrapped so it stays roughly on screen regardless of count. */
    int n = kernel_window_count();
    int x = 20 + ( ( n * 18 ) % 140 );
    int y = KERNEL_DESKTOP_STRIP_H + 10 + ( ( n * 18 ) % 90 );

    void * task = kernel_spawn_app( g_registered[ index ].path, x, y,
                                     g_registered[ index ].w, g_registered[ index ].h, 1 );
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
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    /* Under the compositor (kernel_router.c's kernel_router_composite_
     * frame), the real screen is recomputed fresh every frame purely from
     * each window's own canvas -- there's no persistent "real screen"
     * state left for a caller to hand back to whatever's underneath the
     * way there used to be. What this call actually needs to do now is
     * erase the caller's OWN claim on that region by clearing it, in its
     * OWN canvas, to background: the very next composite tick then shows
     * whatever's really there (another window, or plain background) on
     * its own, automatically. desktop.rb's dropdown-close is the one
     * caller -- it drew the dropdown into its own (oversized, invisible-
     * outside-:launcher-mode) canvas, and closing the menu needs those
     * pixels actually gone from that canvas, not just a request to some
     * other window to redraw over now-stale real-screen pixels that no
     * longer exist as a persistent concept. */
    gfx_fill_rect( ctx->canvas, ( int ) x, ( int ) y, ( int ) w, ( int ) h, THEME_BG );
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
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_register",
                                 acid_launcher_register, MRB_ARGS_REQ( 4 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_count",
                                 acid_launcher_count, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_path",
                                 acid_launcher_path, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_name",
                                 acid_launcher_name, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_spawn",
                                 acid_launcher_spawn, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_send_self_to_back",
                                 acid_send_self_to_back, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_repaint_region",
                                 acid_repaint_region, MRB_ARGS_REQ( 4 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_am_i_focused",
                                 acid_am_i_focused, MRB_ARGS_NONE() );
}
