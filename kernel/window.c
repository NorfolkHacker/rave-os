/* The first Rave-OS GUI primitive: a plain rectangular window with a
 * title bar. No dragging, no close button, no z-ordering between
 * multiple windows yet -- just enough to give widgets somewhere to live
 * and to prove the drawing primitives (graphics.c, text.c) compose into
 * something that reads as "a window" rather than just shapes. */

#include "window.h"
#include "graphics.h"
#include "text.h"

#define WINDOW_BORDER_COLOR 0xFFFFFF
#define WINDOW_TITLEBAR_COLOR 0xC000C0 /* acid magenta */
#define WINDOW_BODY_COLOR 0x181820
#define WINDOW_TITLE_TEXT_COLOR 0xFFFFFF

void window_draw(const struct window *win) {
    int border_top = win->y - WINDOW_TITLEBAR_HEIGHT - 2;
    int border_height = win->h + WINDOW_TITLEBAR_HEIGHT + 4;

    gfx_fill_rect(win->x - 2, border_top, win->w + 4, border_height, WINDOW_BORDER_COLOR);
    gfx_fill_rect(win->x, win->y - WINDOW_TITLEBAR_HEIGHT, win->w, WINDOW_TITLEBAR_HEIGHT, WINDOW_TITLEBAR_COLOR);
    text_puts(win->x + 4, win->y - WINDOW_TITLEBAR_HEIGHT + 6, win->title, WINDOW_TITLE_TEXT_COLOR, 1);
    gfx_fill_rect(win->x, win->y, win->w, win->h, WINDOW_BODY_COLOR);
}
