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

    gfx_fill_rect( ctx->canvas, 0, 0, ctx->window_w, KERNEL_TITLE_BAR_H, THEME_PANEL );

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
    gfx_draw_text( ctx->canvas, 4, ( KERNEL_TITLE_BAR_H - 8 ) / 2, title, THEME_TEXT, THEME_PANEL );

    int cx = ctx->window_w - KERNEL_CLOSE_BTN_MARGIN;
    int cy = KERNEL_TITLE_BAR_H / 2;
    gfx_fill_circle( ctx->canvas, cx, cy, KERNEL_CLOSE_BTN_R, THEME_HARD );

    return mrb_nil_value();
}

static mrb_value
acid_draw_window_border( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;

    /* Previously a window had no visible edge at all -- its body was
     * THEME_BG, the desktop behind it is also near-black, so two
     * non-overlapping windows were only distinguishable by their title
     * bars; anywhere the body showed past another window's edge, it was
     * ambiguous where one stopped and the background started. A 1px
     * THEME_HARD outline around the whole window (title bar + body) fixes
     * that. Deliberately four explicit gfx_fill_rect calls, not LGFX's
     * drawRect(x,y,w,h,color) convenience overload: that overload
     * internally does setColor() then the COLORLESS drawRect(x,y,w,h)
     * overload -- the exact same "colored convenience wrapper is a
     * different, less-tested internal path" shape that silently failed to
     * present for fillScreen() on this SDL backend (see
     * hal_display_sim.cpp's hal_display_clear_screen comment) -- not worth
     * the risk when fillRect(x,y,w,h,color) is already proven reliable
     * everywhere else in this file.
     *
     * Deliberately called LAST, as its own step, not folded into
     * acid_draw_window_frame -- every app's own content is drawn using
     * window-relative coordinates starting at (0,0) and running to the
     * window's own full width/height (e.g. file_manager's row highlights
     * span the full WINDOW_W), which is exactly where this border's own
     * pixels sit. Drawing the border before that content would just get
     * silently painted over the moment the app draws its own UI; callers
     * must invoke this after everything else in their redraw. */
    gfx_fill_rect( ctx->canvas, 0, 0, ctx->window_w, 1, THEME_HARD );
    gfx_fill_rect( ctx->canvas, 0, ctx->window_h - 1, ctx->window_w, 1, THEME_HARD );
    gfx_fill_rect( ctx->canvas, 0, 0, 1, ctx->window_h, THEME_HARD );
    gfx_fill_rect( ctx->canvas, ctx->window_w - 1, 0, 1, ctx->window_h, THEME_HARD );

    return mrb_nil_value();
}

static mrb_value
acid_clear_user_area( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->canvas, 0, KERNEL_TITLE_BAR_H,
                   ctx->window_w, ctx->window_h - KERNEL_TITLE_BAR_H, THEME_BG );
    return mrb_nil_value();
}

void
acid_chrome_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_window_frame",
                                 acid_draw_window_frame, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_window_border",
                                 acid_draw_window_border, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_clear_user_area",
                                 acid_clear_user_area, MRB_ARGS_NONE() );
}
