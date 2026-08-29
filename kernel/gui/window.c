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
 * fills (see kernel.c's backdrop_color() comment for the same math).
 * The border/control-hover accent itself is no longer one fixed value
 * here -- each window carries its own win->accent_color (set once at
 * init in kmain(), reusing PAINT's own already-vetted palette colors)
 * so windows read as visually distinct at a glance; body/titlebar stay
 * this same neutral dark shade for every window regardless. */
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
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */

/* SymbOS/Win95-style titlebar controls: minimize (a flat bar -- not a
 * text glyph, since an underscore's presence in font.c was never
 * confirmed and a bar reads as "minimize" regardless) and close (an "X"
 * glyph -- already confirmed rendered correctly today, since both
 * EXIT's label and the checkbox's "FX" label contain one). Every window
 * gets both; there's no per-window opt-out yet. */
#define WINDOW_CONTROL_SIZE 12
#define WINDOW_CONTROL_MARGIN 4 /* gap from the titlebar's right edge to the close control */
#define WINDOW_CONTROL_GAP 4    /* gap between the minimize and close controls */
#define WINDOW_CONTROL_RADIUS 3
#define WINDOW_CONTROL_BORDER_COLOR_IDLE 0x1F2E27 /* dim, not hovered -- same regardless of the window's own accent */
#define WINDOW_CONTROL_FILL_COLOR 0x0A1A12
#define WINDOW_CONTROL_HOVER_COLOR 0x123322
#define WINDOW_CONTROL_GLYPH_COLOR 0xD4E6DB

static void window_close_rect(const struct window *win, int *x, int *y) {
    *x = win->x + win->w - WINDOW_CONTROL_MARGIN - WINDOW_CONTROL_SIZE;
    *y = win->y - WINDOW_TITLEBAR_HEIGHT + (WINDOW_TITLEBAR_HEIGHT - WINDOW_CONTROL_SIZE) / 2;
}

static void window_minimize_rect(const struct window *win, int *x, int *y) {
    int close_x, close_y;
    window_close_rect(win, &close_x, &close_y);
    *x = close_x - WINDOW_CONTROL_GAP - WINDOW_CONTROL_SIZE;
    *y = close_y;
}

static void window_draw_minimize_control(int x, int y, int hovered, uint32_t accent) {
    uint32_t border = hovered ? accent : WINDOW_CONTROL_BORDER_COLOR_IDLE;
    uint32_t fill = hovered ? WINDOW_CONTROL_HOVER_COLOR : WINDOW_CONTROL_FILL_COLOR;
    int bar_w = 6;

    gfx_fill_rounded_rect(x - 1, y - 1, WINDOW_CONTROL_SIZE + 2, WINDOW_CONTROL_SIZE + 2, WINDOW_CONTROL_RADIUS,
                          border);
    gfx_fill_rounded_rect(x, y, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_RADIUS, fill);
    gfx_fill_rect(x + (WINDOW_CONTROL_SIZE - bar_w) / 2, y + WINDOW_CONTROL_SIZE - 4, bar_w, 2,
                 WINDOW_CONTROL_GLYPH_COLOR);
}

static void window_draw_close_control(int x, int y, int hovered, uint32_t accent) {
    uint32_t border = hovered ? accent : WINDOW_CONTROL_BORDER_COLOR_IDLE;
    uint32_t fill = hovered ? WINDOW_CONTROL_HOVER_COLOR : WINDOW_CONTROL_FILL_COLOR;
    int gw = text_width("X", 1);

    gfx_fill_rounded_rect(x - 1, y - 1, WINDOW_CONTROL_SIZE + 2, WINDOW_CONTROL_SIZE + 2, WINDOW_CONTROL_RADIUS,
                          border);
    gfx_fill_rounded_rect(x, y, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_RADIUS, fill);
    text_puts(x + (WINDOW_CONTROL_SIZE - gw) / 2, y + (WINDOW_CONTROL_SIZE - GLYPH_HEIGHT) / 2, "X",
             WINDOW_CONTROL_GLYPH_COLOR, 1);
}

uint32_t window_body_color(void) {
    return WINDOW_BODY_COLOR;
}

void window_draw(const struct window *win) {
    int border_top = win->y - WINDOW_TITLEBAR_HEIGHT - 2;
    int border_height = win->h + WINDOW_TITLEBAR_HEIGHT + 4;
    int min_x, min_y, close_x, close_y;

    gfx_fill_rounded_rect(win->x - 2, border_top, win->w + 4, border_height, WINDOW_OUTER_RADIUS,
                          win->accent_color);
    gfx_fill_rounded_rect_ex(win->x, win->y - WINDOW_TITLEBAR_HEIGHT, win->w, WINDOW_TITLEBAR_HEIGHT,
                             WINDOW_INNER_RADIUS, GFX_CORNER_TL | GFX_CORNER_TR, WINDOW_TITLEBAR_COLOR);
    text_puts(win->x + 4, win->y - WINDOW_TITLEBAR_HEIGHT + 6, win->title, WINDOW_TITLE_TEXT_COLOR, 1);
    gfx_fill_rounded_rect_ex(win->x, win->y, win->w, win->h, WINDOW_INNER_RADIUS, GFX_CORNER_BL | GFX_CORNER_BR,
                             WINDOW_BODY_COLOR);

    window_minimize_rect(win, &min_x, &min_y);
    window_close_rect(win, &close_x, &close_y);
    window_draw_minimize_control(min_x, min_y, win->minimize_hovered, win->accent_color);
    window_draw_close_control(close_x, close_y, win->close_hovered, win->accent_color);
}

int window_titlebar_hit_test(const struct window *win, int px, int py) {
    int bar_top = win->y - WINDOW_TITLEBAR_HEIGHT;
    int min_x, min_y;
    window_minimize_rect(win, &min_x, &min_y);
    return px >= win->x && px < min_x && py >= bar_top && py < win->y;
}

int window_minimize_hit_test(const struct window *win, int px, int py) {
    int x, y;
    window_minimize_rect(win, &x, &y);
    return px >= x && px < x + WINDOW_CONTROL_SIZE && py >= y && py < y + WINDOW_CONTROL_SIZE;
}

int window_close_hit_test(const struct window *win, int px, int py) {
    int x, y;
    window_close_rect(win, &x, &y);
    return px >= x && px < x + WINDOW_CONTROL_SIZE && py >= y && py < y + WINDOW_CONTROL_SIZE;
}
