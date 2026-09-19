#include "chrome_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_theme.h"

/* How many pixels the corner-rounding staircase cuts, at most, from each
 * edge -- see draw_rounded_corners' own comment. Small on purpose: these
 * windows run as small as ~100px, and a staircase this size already
 * reads clearly as "rounded" rather than "square" at that scale without
 * eating a visually significant chunk of a small window's own corner. */
#define CORNER_RADIUS 3

/* Rounds one corner of an R x R box: for row i (0..R-1, measured inward
 * from the edge), the first (R-1-i) pixels are background (the true
 * "outside the curve" cut) and pixel (R-1-i) itself is border-colored --
 * that single pixel is the important part. A first version of this
 * function only did the background cut and skipped that pixel, which
 * left the two straight border lines meeting nothing at the corner --
 * an actual gap in the outline, reported live as looking "broken", not
 * rounded. This is the standard small-radius pixel-art rounded-corner
 * construction (System 6/7, Amiga Workbench-era GUIs drew corners
 * exactly this way): the border pixels across every row still form a
 * single connected staircase from one straight edge to the other, which
 * is what actually reads as "rounded" at this scale rather than "a
 * chunk is missing". flip_x/flip_y mirror the same math onto whichever
 * of the four corners is being drawn -- corner_x/corner_y are that
 * corner's own outer pixel (0 or w-1 / 0 or h-1). */
static void
draw_corner( struct kernel_app_context * ctx, int r, int corner_x, int corner_y,
             int flip_x, int flip_y, unsigned int bg_color )
{
    int i;
    for( i = 0; i < r; i++ )
    {
        int bg_width = r - 1 - i;
        int y = flip_y ? corner_y - i : corner_y + i;
        if( bg_width > 0 )
        {
            int bg_x = flip_x ? corner_x - bg_width + 1 : corner_x;
            gfx_fill_rect( ctx->canvas, bg_x, y, bg_width, 1, bg_color );
        }
        int px = flip_x ? corner_x - bg_width : corner_x + bg_width;
        gfx_fill_rect( ctx->canvas, px, y, 1, 1, THEME_HARD );
    }
}

/* Rounds all four corners -- see draw_corner's own comment for the
 * per-pixel construction. Restores whatever color belongs in each
 * corner's own "outside the curve" cut (THEME_PANEL for the two corners
 * inside the title bar, THEME_BG for the two inside the body), matching
 * acid_draw_window_frame/acid_clear_user_area's own fills there. See
 * acid_draw_window_border's own comment for why this has to run after
 * the straight border lines. Clamps the radius down for a window too
 * small to fit it (defensive -- every real window in this codebase is
 * comfortably bigger than CORNER_RADIUS*2, but a future tiny one
 * shouldn't cut into itself). */
static void
draw_rounded_corners( struct kernel_app_context * ctx )
{
    int w = ctx->window_w;
    int h = ctx->window_h;
    int r = CORNER_RADIUS;
    if( r * 2 > w || r * 2 > h )
    {
        r = ( w < h ? w : h ) / 2;
    }
    draw_corner( ctx, r, 0, 0, 0, 0, THEME_PANEL );
    draw_corner( ctx, r, w - 1, 0, 1, 0, THEME_PANEL );
    draw_corner( ctx, r, 0, h - 1, 0, 1, THEME_BG );
    draw_corner( ctx, r, w - 1, h - 1, 1, 1, THEME_BG );
}

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

    /* Rounds all four corners -- a small pixel staircase (3, then 2, then
     * 1 pixel cut per row moving inward), the same technique classic
     * low-res pixel UIs (System 6/7, Amiga Workbench) used for rounded
     * windows before anti-aliased curves were affordable, and it matches
     * this OS's own existing blocky look (Tetris/Breakout/Acid Blaster's
     * alien all draw the same way) far better than a smooth arc would --
     * there's no arc/rounded-rect primitive in this codebase anyway (only
     * fill_rect/fill_circle), so this is also just what's achievable with
     * what already exists. Requested explicitly by the user, border color
     * (THEME_HARD, green) unchanged -- only the corners are cut away, back
     * to whatever's already under them (THEME_PANEL in the title bar,
     * THEME_BG in the body, matching acid_draw_window_frame/
     * acid_clear_user_area's own fills so the cut blends in rather than
     * showing a mismatched patch). Deliberately called after the straight
     * border lines above (same "must run last" reasoning as this whole
     * function), so the notches actually override those lines' own corner
     * pixels instead of being drawn under them. */
    draw_rounded_corners( ctx );

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
