#include "gfx_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../kernel/kernel_overlay.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_theme.h"

static mrb_value
acid_fill_rect( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;

    /* Clip to this window's own bounds so a caller with a miscalculated
     * position/size (e.g. an unclamped scrolling cursor -- see the phase 5
     * final review's finding on editor.rb) can never paint outside its own
     * window onto the desktop or another app's window. */
    int cx = ( int ) x;
    int cy = ( int ) y;
    int cw = ( int ) w;
    int ch = ( int ) h;
    if( cx < 0 ) { cw += cx; cx = 0; }
    if( cy < 0 ) { ch += cy; cy = 0; }
    if( cx + cw > ctx->window_w ) { cw = ctx->window_w - cx; }
    if( cy + ch > ctx->window_h ) { ch = ctx->window_h - cy; }
    if( cw <= 0 || ch <= 0 )
    {
        return mrb_nil_value();
    }

    gfx_fill_rect( ctx->canvas, cx, cy, cw, ch, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_fill_circle( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, r, color;
    mrb_get_args( mrb, "iiii", &x, &y, &r, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_circle( ctx->canvas, ( int ) x, ( int ) y, ( int ) r, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_draw_text( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    const char * str;
    mrb_int x, y, fg, bg;
    /* 'z' = NUL-terminated const char* (verified against mruby's own
     * mrb_get_args format-specifier doc comment in src/class.c). */
    mrb_get_args( mrb, "ziiii", &str, &x, &y, &fg, &bg );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;

    /* Coarse origin guard: if the text's own start point is already
     * outside this window's bounds, don't draw it at all -- prevents an
     * unclamped caller from painting far outside its own window (see the
     * phase 5 final review's finding). Not per-glyph clipping. */
    if( ( int ) x < 0 || ( int ) x >= ctx->window_w ||
        ( int ) y < 0 || ( int ) y >= ctx->window_h )
    {
        return mrb_nil_value();
    }

    gfx_draw_text( ctx->canvas, ( int ) x, ( int ) y, str, ( unsigned int ) fg, ( unsigned int ) bg );
    return mrb_nil_value();
}

/* The overlay (kernel_overlay.h) is the one drawing surface that is NOT
 * this app's own window: coordinates are screen-absolute, and what is
 * drawn here appears over every window including the caller's. Clipping is
 * therefore against the screen, not ctx->window_w/h, and there is no
 * ctx->canvas anywhere in these four functions. */
static mrb_value
acid_overlay_open( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    /* xTaskGetCurrentTaskHandle as the claim, the same self-identification
     * pattern acid_send_self_to_back and the audio bindings already use.
     * False here means another app is already animating -- see
     * kernel_overlay_open. */
    return mrb_bool_value( kernel_overlay_open( ( void * ) xTaskGetCurrentTaskHandle() ) != 0 );
}

static mrb_value
acid_overlay_clear( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    void * canvas = kernel_overlay_canvas_for( ( void * ) xTaskGetCurrentTaskHandle() );
    if( canvas == NULL )
    {
        return mrb_nil_value();
    }
    gfx_fill_rect( canvas, 0, 0, KERNEL_SCREEN_W, KERNEL_SCREEN_H, ACID_OVERLAY_KEY );
    return mrb_nil_value();
}

static mrb_value
acid_overlay_fill_rect( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );

    void * canvas = kernel_overlay_canvas_for( ( void * ) xTaskGetCurrentTaskHandle() );
    if( canvas == NULL )
    {
        /* Closed overlay, or a caller that doesn't own it: silently
         * nothing. A sprite whose animation ended a frame ago, an egg that
         * never got a canvas, and an app drawing while another one holds
         * the overlay must none of them be an error a user sees. */
        return mrb_nil_value();
    }

    /* Same clipping discipline as acid_fill_rect above, against the screen
     * rather than a window -- a sprite flying in from off-screen has
     * negative coordinates by design, so this path is normal, not
     * defensive. */
    int cx = ( int ) x;
    int cy = ( int ) y;
    int cw = ( int ) w;
    int ch = ( int ) h;
    if( cx < 0 ) { cw += cx; cx = 0; }
    if( cy < 0 ) { ch += cy; cy = 0; }
    if( cx + cw > KERNEL_SCREEN_W ) { cw = KERNEL_SCREEN_W - cx; }
    if( cy + ch > KERNEL_SCREEN_H ) { ch = KERNEL_SCREEN_H - cy; }
    if( cw <= 0 || ch <= 0 )
    {
        return mrb_nil_value();
    }

    gfx_fill_rect( canvas, cx, cy, cw, ch, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_overlay_close( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    kernel_overlay_close( ( void * ) xTaskGetCurrentTaskHandle() );
    return mrb_nil_value();
}

void
acid_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_rect",
                                 acid_fill_rect, MRB_ARGS_REQ( 5 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_circle",
                                 acid_fill_circle, MRB_ARGS_REQ( 4 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_text",
                                 acid_draw_text, MRB_ARGS_REQ( 5 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_open",
                                 acid_overlay_open, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_clear",
                                 acid_overlay_clear, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_fill_rect",
                                 acid_overlay_fill_rect, MRB_ARGS_REQ( 5 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_close",
                                 acid_overlay_close, MRB_ARGS_NONE() );
}
