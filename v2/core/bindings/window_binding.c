#include <string.h>

#include "window_binding.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_router.h"
#include "../kernel/kernel_spawn.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_audio.h"
#include "../gfx/gfx.h"
#include "../gfx/wallpaper.h"

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
    /* True for the handful of apps that make sense to have several
     * windows of at once (Editor, File Manager, Terminal -- explicit
     * user request); every other app is a singleton, see
     * find_running_task_by_path's own comment. */
    int multi;
    /* This app's own Ruby modules, from its manifest's `libs` field --
     * see vm_host.h. NULL for the great majority of apps, which have
     * none. Held here rather than passed to acid_spawn_app because the
     * registry is already the one place both launch paths (the Menu's
     * acid_launcher_spawn and File Manager's acid_spawn_app) agree on. */
    char * libs;
};

/* Bounded, not a Ruby-sized dynamic array -- matches this codebase's own
 * style elsewhere (KERNEL_WINDOW_MAX, etc.). desktop.rb's own dropdown
 * only has room to ever show MAX_LAUNCHER_ITEMS (6) of these regardless;
 * this is deliberately a little larger so a scan that finds more doesn't
 * silently drop entries the UI might grow room for later. */
#define MAX_REGISTERED_APPS 16
static struct launchable_app g_registered[ MAX_REGISTERED_APPS ];
static int g_registered_count = 0;

/* Copies exactly `len` bytes and NUL-terminates the copy, rather than
 * relying on strlen -- required for an mrb_get_args "s" pointer, which is
 * a pointer+length pair into an mruby string's buffer with NO guarantee
 * of NUL termination at that length (a substring, e.g. desktop.rb's
 * manifest parser's line[eq+1, ...].strip, can share its parent string's
 * underlying buffer, so strlen() on it can run past the intended end and
 * absorb whatever follows in that shared buffer). Every mrb string
 * reaching this file's heap copies goes through here for that reason. */
static char *
dup_cstr_len( const char * src, size_t len )
{
    char * copy = ( char * ) pvPortMalloc( len + 1 );
    if( copy != NULL )
    {
        memcpy( copy, src, len );
        copy[ len ] = '\0';
    }
    return copy;
}

/* Clamps a cascade-computed spawn position so a window of size w x h
 * actually lands fully on screen. Both spawn sites below cascade new
 * windows by a simple offset that grows with kernel_window_count() (see
 * either call site's own comment), tuned back when every window this OS
 * spawned was around 240x170 against a 640x360 screen. That stopped
 * holding once the editor grew to 420x280 (v2/apps/editor/layout.rb's
 * WINDOW_W/WINDOW_H): the cascade's own raw maximum, x=418/y=233, puts a
 * 420x280 editor's right edge at 838 and its bottom edge at 513 -- both
 * well past KERNEL_SCREEN_W/KERNEL_SCREEN_H (640x360) -- and it's cheap
 * to reach in practice, since the desktop itself counts toward
 * kernel_window_count(), so a fourth real window is already enough. Seen
 * live: the editor opened with its title bar and most of its text area
 * off-screen, only the status line inside the visible area.
 *
 * Clamping here keeps every window fully reachable regardless of how
 * large the raw cascade offset grows, while still letting successive
 * windows cascade away from each other right up to the point where
 * they'd run off an edge. A window taller or wider than the screen
 * itself (shouldn't happen, but cheap to make safe) pins to the top-left
 * corner of the usable area -- below the desktop strip -- rather than
 * producing a negative coordinate. */
static void
cascade_clamp( int * x, int * y, int w, int h )
{
    int max_x = KERNEL_SCREEN_W - w;
    int max_y = KERNEL_SCREEN_H - h;

    if( max_x < 0 )
    {
        *x = 0;
    }
    else if( *x > max_x )
    {
        *x = max_x;
    }

    if( max_y < KERNEL_DESKTOP_STRIP_H )
    {
        *y = KERNEL_DESKTOP_STRIP_H;
    }
    else if( *y > max_y )
    {
        *y = max_y;
    }
    if( *y < KERNEL_DESKTOP_STRIP_H )
    {
        *y = KERNEL_DESKTOP_STRIP_H;
    }
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
    mrb_bool multi;
    char * libs;
    mrb_int libs_len;
    mrb_get_args( mrb, "ssiibs", &path, &path_len, &name, &name_len, &w, &h, &multi,
                  &libs, &libs_len );

    if( g_registered_count >= MAX_REGISTERED_APPS )
    {
        return mrb_bool_value( 0 );
    }

    struct launchable_app * slot = &g_registered[ g_registered_count ];
    slot->path = dup_cstr_len( path, ( size_t ) path_len );
    slot->name = dup_cstr_len( name, ( size_t ) name_len );
    if( slot->path == NULL || slot->name == NULL )
    {
        return mrb_bool_value( 0 );
    }
    slot->w = ( int ) w;
    slot->h = ( int ) h;
    slot->multi = multi ? 1 : 0;
    /* An empty manifest field and a missing one are the same thing to
     * vm_host, which takes NULL to mean "this app has no modules". */
    if( libs_len == 0 )
    {
        slot->libs = NULL;
    }
    else
    {
        slot->libs = dup_cstr_len( libs, ( size_t ) libs_len );
        if( slot->libs == NULL )
        {
            return mrb_bool_value( 0 );
        }
    }
    g_registered_count++;
    return mrb_bool_value( 1 );
}

/* Finds a currently-open window spawned from this exact script path,
 * for singleton enforcement -- see struct launchable_app's own comment
 * on `multi`. A plain linear scan of every registered window (at most
 * KERNEL_WINDOW_MAX, a handful), same cost class as active_windows'
 * equivalent scan in desktop.rb. Returns the owning task handle, or
 * NULL if no such window is open. */
static void *
find_running_task_by_path( const char * path )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        struct kernel_window * win = kernel_window_at_index( i );
        if( win != NULL && win->in_use && win->app_name != NULL &&
            strcmp( win->app_name, path ) == 0 )
        {
            return win->task;
        }
    }
    return NULL;
}

/* The registered `multi` flag for a script path -- used by acid_spawn_app,
 * which (unlike acid_launcher_spawn) only has a path, not a registry
 * index. Defaults to 0 (singleton) for a path with no registry entry at
 * all, which shouldn't happen in practice (every real app has a manifest
 * and is registered at boot regardless of Menu visibility) but is the
 * safer default if it ever did. */
static int
is_multi_by_path( const char * path )
{
    int i;
    for( i = 0; i < g_registered_count; i++ )
    {
        if( strcmp( g_registered[ i ].path, path ) == 0 )
        {
            return g_registered[ i ].multi;
        }
    }
    return 0;
}

/* The registered `libs` for a script path -- the acid_spawn_app half of
 * the same lookup is_multi_by_path does, so File Manager launching an app
 * by path gets that app's modules exactly as the Menu does. NULL for an
 * unregistered path, which is also the right answer: no manifest, no
 * modules. */
static const char *
libs_by_path( const char * path )
{
    int i;
    for( i = 0; i < g_registered_count; i++ )
    {
        if( strcmp( g_registered[ i ].path, path ) == 0 )
        {
            return g_registered[ i ].libs;
        }
    }
    return NULL;
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

    /* Singleton enforcement -- see struct launchable_app's own comment
     * on `multi`. A non-multi app that's already open gets focused
     * instead of duplicated; only the handful of apps that opted into
     * `multi = true` in their own manifest can have more than one
     * window at once. */
    if( !g_registered[ index ].multi )
    {
        void * existing = find_running_task_by_path( g_registered[ index ].path );
        if( existing != NULL )
        {
            kernel_router_activate_window( existing );
            return mrb_bool_value( 1 );
        }
    }

    /* Simple cascade so successively launched windows don't all land
     * exactly on top of each other -- based on how many windows already
     * exist, wrapped so it stays roughly on screen regardless of count. */
    int n = kernel_window_count();
    int x = 20 + ( ( n * 18 ) % 400 );
    int y = KERNEL_DESKTOP_STRIP_H + 10 + ( ( n * 18 ) % 200 );
    cascade_clamp( &x, &y, g_registered[ index ].w, g_registered[ index ].h );

    void * task = kernel_spawn_app( g_registered[ index ].path, x, y,
                                     g_registered[ index ].w, g_registered[ index ].h, 1, NULL,
                                     g_registered[ index ].libs );
    if( task != NULL )
    {
        kernel_router_activate_window( task );
    }
    return mrb_bool_value( task != NULL );
}

/* General-purpose spawn, for launching an app by path directly rather
 * than through the Menu dropdown's registered-index list -- File
 * Manager's own "open" action is the first caller: launching a game by
 * clicking its .app.toml, or opening Editor with a specific file to
 * view/edit (the `arg` string) rather than Editor's own hardcoded
 * default. `arg` is optional -- an empty string means none, read back on
 * the spawned side via acid_launch_arg. Both path and arg are heap-
 * copied here (never freed, same "lives for the process" contract as
 * acid_launcher_register's own copies) since the caller's mruby strings
 * are temporary and wouldn't outlive the spawned task otherwise. */
static mrb_value
acid_spawn_app( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    char * path;
    mrb_int path_len;
    mrb_int w, h;
    char * arg;
    mrb_int arg_len;
    mrb_get_args( mrb, "siis", &path, &path_len, &w, &h, &arg, &arg_len );

    /* Built up front, before any of the three lookups below, and compared
     * against from here on instead of the raw mrb pointer: mrb_get_args
     * "s" hands back a pointer+length pair into an mruby string's buffer
     * with NO guarantee of NUL termination at that length (see
     * dup_cstr_len's own comment) -- a String#[] slice shares its
     * parent's buffer, so a raw strcmp against `path` can run past the
     * intended end and pick up whatever follows in that shared buffer.
     * The dangerous one is libs_by_path: a spurious mismatch there
     * returns NULL, the spawned VM silently loads none of its modules,
     * and something as ordinary as `include EditorLayout` raises and
     * takes the window down. */
    char * path_copy = dup_cstr_len( path, ( size_t ) path_len );
    if( path_copy == NULL )
    {
        return mrb_bool_value( 0 );
    }

    /* Same singleton enforcement as acid_launcher_spawn -- looked up by
     * path against the registry (every app reachable this way, games
     * included, is registered at boot regardless of Menu visibility, so
     * this always finds a real multi flag rather than guessing one).
     * An app somehow not registered at all defaults to singleton, the
     * safer of the two behaviors. */
    if( !is_multi_by_path( path_copy ) )
    {
        void * existing = find_running_task_by_path( path_copy );
        if( existing != NULL )
        {
            kernel_router_activate_window( existing );
            /* Unlike the success path below, path_copy is never handed to
             * kernel_spawn_app on this branch -- no window ends up owning
             * it, so it would otherwise leak on every refocus of an
             * already-open singleton. */
            vPortFree( path_copy );
            return mrb_bool_value( 1 );
        }
    }

    char * arg_copy = NULL;
    if( arg_len != 0 )
    {
        arg_copy = dup_cstr_len( arg, ( size_t ) arg_len );
        if( arg_copy == NULL )
        {
            vPortFree( path_copy );
            return mrb_bool_value( 0 );
        }
    }

    int n = kernel_window_count();
    int x = 20 + ( ( n * 18 ) % 400 );
    int y = KERNEL_DESKTOP_STRIP_H + 10 + ( ( n * 18 ) % 200 );
    cascade_clamp( &x, &y, ( int ) w, ( int ) h );

    void * task = kernel_spawn_app( path_copy, x, y, ( int ) w, ( int ) h, 1, arg_copy,
                                     libs_by_path( path_copy ) );
    if( task != NULL )
    {
        kernel_router_activate_window( task );
    }
    return mrb_bool_value( task != NULL );
}

/* The optional startup string this app was spawned with (kernel_spawn_
 * app's own `arg`, see its comment) -- "" if none. Reading it more than
 * once is safe (it's just ctx state, not consumed), though every current
 * caller only checks it once, in on_create. */
static mrb_value
acid_launch_arg( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    return mrb_str_new_cstr( mrb, ctx->arg ? ctx->arg : "" );
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
     * longer exist as a persistent concept. That "background" is the real
     * wallpaper now, not a flat THEME_BG fill -- see wallpaper.h's own
     * comment on wallpaper_draw_into -- so the desktop area behind a
     * closed dropdown actually shows it instead of a permanent black
     * rectangle. */
    wallpaper_draw_into( ctx->canvas, ( int ) x, ( int ) y, ( int ) w, ( int ) h );
    return mrb_nil_value();
}

/* Config's wallpaper on/off toggle -- see wallpaper.h's own comment. Only
 * flips the flag and asks the router to recomposite (gfx_mark_dirty);
 * desktop.rb is responsible for noticing the change and repainting its own
 * already-drawn dropdown-closed area to match (see its state_signature/
 * redraw_if_changed), the same way it already notices a window opening or
 * closing. */
static mrb_value
acid_set_wallpaper_enabled( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_bool enabled;
    mrb_get_args( mrb, "b", &enabled );
    wallpaper_set_enabled( enabled );
    gfx_mark_dirty();
    return mrb_nil_value();
}

static mrb_value
acid_get_wallpaper_enabled( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_bool_value( wallpaper_is_enabled() );
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

/* Ends any window by its acid_window_info index, not just the caller's
 * own -- for a system-monitor app to offer a close/[stop] button per
 * row the way the reference OS's own task-list monitor does, just for
 * acid OS v2's windows rather than generic OS tasks. Refuses to close
 * the CALLING app's own window (compare kernel_window_by_task's result
 * against the running task): a monitor ending itself via its own window
 * list is a confusing way to quit, and this codebase already has a
 * normal close button (the title bar dot) for that. */
static mrb_value
acid_close_window( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );

    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return mrb_bool_value( 0 );
    }

    struct kernel_window * win = kernel_window_at_index( ( int ) index );
    if( win == NULL || !win->in_use || win->task == ( void * ) xTaskGetCurrentTaskHandle() )
    {
        return mrb_bool_value( 0 );
    }
    kernel_router_close_window( win->task );
    return mrb_bool_value( 1 );
}

/* Kernel/compositor/audio internals exposed purely for a system-monitor
 * app to show real activity of THIS build's own architecture -- what's
 * actually distinctive about acid OS v2 (its own compositor, its own
 * hand-built synth), not generic OS bookkeeping. See kernel_router.h/
 * kernel_audio.h for what each of these actually counts. */
static mrb_value
acid_composited_frames( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( kernel_router_composited_frames() );
}

static mrb_value
acid_skipped_frames( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( kernel_router_skipped_frames() );
}

static mrb_value
acid_active_voice_count( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( kernel_audio_active_voice_count() );
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
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_close_window",
                                 acid_close_window, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launcher_register",
                                 acid_launcher_register, MRB_ARGS_REQ( 6 ) );
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
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_set_wallpaper_enabled",
                                 acid_set_wallpaper_enabled, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_get_wallpaper_enabled",
                                 acid_get_wallpaper_enabled, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_am_i_focused",
                                 acid_am_i_focused, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_spawn_app",
                                 acid_spawn_app, MRB_ARGS_REQ( 4 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_launch_arg",
                                 acid_launch_arg, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_composited_frames",
                                 acid_composited_frames, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_skipped_frames",
                                 acid_skipped_frames, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_active_voice_count",
                                 acid_active_voice_count, MRB_ARGS_NONE() );
}
