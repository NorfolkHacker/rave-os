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
    char * title;
    mrb_int title_len;
    mrb_get_args( mrb, "s", &title, &title_len );

    gfx_fill_rect( ctx->window_x, ctx->window_y, ctx->window_w, KERNEL_TITLE_BAR_H, THEME_PANEL );

    /* Every window used to be unlabeled chrome -- a bare title-bar-colored
     * strip and a close dot, no way to tell which app a window even was
     * without touching it. Every caller of this binding now passes a
     * display title (AcidApp#window_title derives one from the class name
     * automatically); title_len comes from mrb_get_args's "s" format but
     * isn't needed here since gfx_draw_text takes a null-terminated C
     * string and every title passed is a plain literal with no embedded
     * NUL. Deliberately not clipped against the close button -- callers
     * are expected to keep titles short (window_title caps at 16 chars),
     * matching how the taskbar's own labels are the caller's
     * responsibility to size, not this binding's. */
    ( void ) title_len;
    gfx_draw_text( ctx->window_x + 4, ctx->window_y + ( KERNEL_TITLE_BAR_H - 8 ) / 2,
                    title, THEME_TEXT, THEME_PANEL );

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

static mrb_value
acid_draw_desktop_strip( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x, ctx->window_y, ctx->window_w, ctx->window_h, THEME_PANEL );
    return mrb_nil_value();
}

void
acid_chrome_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_window_frame",
                                 acid_draw_window_frame, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_clear_user_area",
                                 acid_clear_user_area, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_desktop_strip",
                                 acid_draw_desktop_strip, MRB_ARGS_NONE() );
}
