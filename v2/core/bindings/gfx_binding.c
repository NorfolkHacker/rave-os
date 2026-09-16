#include "gfx_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"

static mrb_value
acid_fill_rect( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                   ( int ) w, ( int ) h, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_fill_circle( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, r, color;
    mrb_get_args( mrb, "iiii", &x, &y, &r, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_circle( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                      ( int ) r, ( unsigned int ) color );
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
    gfx_draw_text( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                   str, ( unsigned int ) fg, ( unsigned int ) bg );
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
}
