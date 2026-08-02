/* The first Rave-OS GUI primitive: a plain rectangular window with a
 * title bar. No close button, no z-ordering between multiple windows
 * yet -- just enough to give widgets somewhere to live and to prove the
 * drawing primitives (graphics.c, text.c) compose into something that
 * reads as "a window" rather than just shapes. */

#include "window.h"
#include "graphics.h"
#include "text.h"

/* androidacid.com's "black and acid green" palette -- flat-RGB values
 * derived from the site's actual CSS custom properties, alpha-composited
 * onto its #050607 background by hand since this kernel only does opaque
 * fills (see kernel.c's backdrop_color() comment for the same math). */
#define WINDOW_BORDER_COLOR 0x00FF66  /* --hard */
#define WINDOW_TITLEBAR_COLOR 0x0F1F17 /* a shade brighter than the body, so the bar still reads as separate without needing a second border */
#define WINDOW_BODY_COLOR 0x0B1712    /* --panel over --bg, brightened slightly for legibility at low res -- the literal composite (~0x070C09) was nearly indistinguishable from the backdrop */
#define WINDOW_TITLE_TEXT_COLOR 0xD4E6DB /* --text at 0.9 alpha over --bg */

/* androidacid.com leans on large CSS border-radius throughout -- the outer
 * frame rounds all four corners, but the titlebar/body only round the
 * corners that are actually part of the window's silhouette (top two for
 * the titlebar, bottom two for the body). Rounding the titlebar's bottom
 * corners or the body's top corners too would carve rounded notches into
 * an internal seam that should just stay a flush straight line. */
#define WINDOW_OUTER_RADIUS 8
#define WINDOW_INNER_RADIUS 6

void window_draw(const struct window *win) {
    int border_top = win->y - WINDOW_TITLEBAR_HEIGHT - 2;
    int border_height = win->h + WINDOW_TITLEBAR_HEIGHT + 4;

    gfx_fill_rounded_rect(win->x - 2, border_top, win->w + 4, border_height, WINDOW_OUTER_RADIUS,
                          WINDOW_BORDER_COLOR);
    gfx_fill_rounded_rect_ex(win->x, win->y - WINDOW_TITLEBAR_HEIGHT, win->w, WINDOW_TITLEBAR_HEIGHT,
                             WINDOW_INNER_RADIUS, GFX_CORNER_TL | GFX_CORNER_TR, WINDOW_TITLEBAR_COLOR);
    text_puts(win->x + 4, win->y - WINDOW_TITLEBAR_HEIGHT + 6, win->title, WINDOW_TITLE_TEXT_COLOR, 1);
    gfx_fill_rounded_rect_ex(win->x, win->y, win->w, win->h, WINDOW_INNER_RADIUS, GFX_CORNER_BL | GFX_CORNER_BR,
                             WINDOW_BODY_COLOR);
}

int window_titlebar_hit_test(const struct window *win, int px, int py) {
    int bar_top = win->y - WINDOW_TITLEBAR_HEIGHT;
    return px >= win->x && px < win->x + win->w && py >= bar_top && py < win->y;
}
