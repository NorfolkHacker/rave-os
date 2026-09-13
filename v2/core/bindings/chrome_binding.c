#include "chrome_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_theme.h"

static mrb_value
acid_draw_window_frame( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;

    gfx_fill_rect( ctx->window_x, ctx->window_y, ctx->window_w, KERNEL_TITLE_BAR_H, THEME_PANEL );

    int cx = ctx->window_x + ctx->window_w - KERNEL_CLOSE_BTN_MARGIN;
    int cy = ctx->window_y + ( KERNEL_TITLE_BAR_H / 2 );
    gfx_fill_circle( cx, cy, KERNEL_CLOSE_BTN_R, THEME_HARD );

    return mrb_nil_value();
}

static mrb_value
acid_clear_user_area( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x, ctx->window_y + KERNEL_TITLE_BAR_H,
                   ctx->window_w, ctx->window_h - KERNEL_TITLE_BAR_H, THEME_BG );
    return mrb_nil_value();
}

void
acid_chrome_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_window_frame",
                                 acid_draw_window_frame, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_clear_user_area",
                                 acid_clear_user_area, MRB_ARGS_NONE() );
}
