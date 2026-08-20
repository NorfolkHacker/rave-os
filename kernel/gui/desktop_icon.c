/* Desktop icons: the fourth and last stage of the SymbOS-style desktop
 * plan, and the only way to recover a WINDOW_CLOSED window -- the
 * taskbar (taskbar.c) only lists windows that are open or minimized.
 * Fixed per-index slots down a column, same reasoning as taskbar.c's
 * entries: there are only ever MAX_WINDOWS possible icons, so a slot
 * going empty (window not closed) is simpler than a packed list that
 * reflows as windows close and reopen. */

#include "desktop_icon.h"
#include "graphics.h"
#include "text.h"

#define ICON_BORDER_COLOR 0x00FF66      /* --hard, hovered */
#define ICON_BORDER_COLOR_IDLE 0x1F2E27 /* dim, idle */
#define ICON_FILL_COLOR 0x0A1A12
#define ICON_HOVER_COLOR 0x123322
#define ICON_LABEL_COLOR 0xD4E6DB
#define ICON_RADIUS 6
#define ICON_LABEL_GAP 4
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */

/* Vertical space one icon slot occupies: the box, its label, and the gap
 * before the next slot. Shared by desktop_icon_rect() (per-icon position)
 * and desktop_icons_init() (the column's total height) so the two can't
 * drift apart. */
#define SLOT_HEIGHT (DESKTOP_ICON_SIZE + DESKTOP_ICON_GAP + GLYPH_HEIGHT + ICON_LABEL_GAP)

void desktop_icons_init(struct desktop_icons *icons, int origin_x, int origin_y, int count) {
    icons->x = origin_x;
    icons->y = origin_y;
    icons->w = DESKTOP_ICON_COLUMN_WIDTH;
    icons->h = count * SLOT_HEIGHT;
}

static void desktop_icon_rect(const struct desktop_icons *icons, int index, int *x, int *y, int *w, int *h) {
    *x = icons->x;
    *y = icons->y + index * SLOT_HEIGHT;
    *w = DESKTOP_ICON_SIZE;
    *h = DESKTOP_ICON_SIZE;
}

int desktop_icon_hit(const struct desktop_icons *icons, const struct window *windows, int count, int px, int py) {
    int i;
    for (i = 0; i < count; i++) {
        int x, y, w, h;
        if (windows[i].state != WINDOW_CLOSED) {
            continue;
        }
        desktop_icon_rect(icons, i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            return i;
        }
    }
    return -1;
}

void desktop_icons_draw(const struct desktop_icons *icons, const struct window *windows, int count,
                        int hovered_index) {
    int i;

    for (i = 0; i < count; i++) {
        int x, y, w, h;
        int is_hovered = (i == hovered_index);
        uint32_t border, fill;

        if (windows[i].state != WINDOW_CLOSED) {
            continue;
        }
        desktop_icon_rect(icons, i, &x, &y, &w, &h);

        border = is_hovered ? ICON_BORDER_COLOR : ICON_BORDER_COLOR_IDLE;
        fill = is_hovered ? ICON_HOVER_COLOR : ICON_FILL_COLOR;

        gfx_fill_rounded_rect(x - 1, y - 1, w + 2, h + 2, ICON_RADIUS, border);
        gfx_fill_rounded_rect(x, y, w, h, ICON_RADIUS, fill);
        text_puts(x, y + h + ICON_LABEL_GAP, windows[i].title, ICON_LABEL_COLOR, 1);
    }
}
