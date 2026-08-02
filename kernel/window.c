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

void window_draw(const struct window *win) {
    int border_top = win->y - WINDOW_TITLEBAR_HEIGHT - 2;
    int border_height = win->h + WINDOW_TITLEBAR_HEIGHT + 4;

    gfx_fill_rect(win->x - 2, border_top, win->w + 4, border_height, WINDOW_BORDER_COLOR);
    gfx_fill_rect(win->x, win->y - WINDOW_TITLEBAR_HEIGHT, win->w, WINDOW_TITLEBAR_HEIGHT, WINDOW_TITLEBAR_COLOR);
    text_puts(win->x + 4, win->y - WINDOW_TITLEBAR_HEIGHT + 6, win->title, WINDOW_TITLE_TEXT_COLOR, 1);
    gfx_fill_rect(win->x, win->y, win->w, win->h, WINDOW_BODY_COLOR);
}

int window_titlebar_hit_test(const struct window *win, int px, int py) {
    int bar_top = win->y - WINDOW_TITLEBAR_HEIGHT;
    return px >= win->x && px < win->x + win->w && py >= bar_top && py < win->y;
}
