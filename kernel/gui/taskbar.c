/* A SymbOS/Win95-style taskbar: a strip pinned to the bottom of the
 * screen listing one entry per non-closed window, the first way to
 * recover a window hidden by the minimize/close controls window.c grew
 * last stage. Fixed per-index slots (entry i is always window i's slot)
 * rather than a packed list that reflows as windows close -- there are
 * only ever MAX_WINDOWS possible entries, so a slot going empty when its
 * window closes is simpler than compacting a list for it. */

#include "taskbar.h"
#include "graphics.h"
#include "text.h"

#define TASKBAR_BG_COLOR 0x070C09     /* --panel's literal composite, dark enough to read as a separate bar */
#define TASKBAR_BORDER_COLOR 0x00FF66 /* --hard */
#define TASKBAR_ENTRY_BORDER_COLOR 0x00FF66      /* --hard, hovered */
#define TASKBAR_ENTRY_BORDER_COLOR_IDLE 0x1F2E27 /* dim, idle */
#define TASKBAR_ENTRY_FILL_COLOR 0x0A1A12
#define TASKBAR_ENTRY_HOVER_COLOR 0x123322
#define TASKBAR_ENTRY_ACTIVE_COLOR 0x00FF66 /* solid --hard, the frontmost open window's entry */
#define TASKBAR_ENTRY_LABEL_COLOR 0xD4E6DB
#define TASKBAR_ENTRY_LABEL_COLOR_ACTIVE 0x050607   /* dark label against the solid-green active fill */
#define TASKBAR_ENTRY_LABEL_COLOR_MINIMIZED 0x9DAAA3 /* muted, so a minimized entry visibly reads as inactive */
#define TASKBAR_ENTRY_WIDTH 140
#define TASKBAR_ENTRY_HEIGHT 18
#define TASKBAR_ENTRY_GAP 8
#define TASKBAR_ENTRY_MARGIN 8
#define TASKBAR_ENTRY_RADIUS 4
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */

static void taskbar_entry_rect(const struct taskbar *bar, int index, int *x, int *y, int *w, int *h) {
    *x = bar->x + TASKBAR_ENTRY_MARGIN + index * (TASKBAR_ENTRY_WIDTH + TASKBAR_ENTRY_GAP);
    *y = bar->y + (bar->h - TASKBAR_ENTRY_HEIGHT) / 2;
    *w = TASKBAR_ENTRY_WIDTH;
    *h = TASKBAR_ENTRY_HEIGHT;
}

int taskbar_hit_entry(const struct taskbar *bar, const struct window *windows, int count, int px, int py) {
    int i;
    for (i = 0; i < count; i++) {
        int x, y, w, h;
        if (windows[i].state == WINDOW_CLOSED) {
            continue;
        }
        taskbar_entry_rect(bar, i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            return i;
        }
    }
    return -1;
}

void taskbar_draw(const struct taskbar *bar, const struct window *windows, const int *z_order, int count,
                  int hovered_entry) {
    int i;

    gfx_fill_rect(bar->x, bar->y, bar->w, bar->h, TASKBAR_BG_COLOR);
    gfx_fill_rect(bar->x, bar->y, bar->w, 1, TASKBAR_BORDER_COLOR);

    for (i = 0; i < count; i++) {
        int x, y, w, h;
        int is_active = (windows[i].state == WINDOW_OPEN) && (z_order[0] == i);
        int is_hovered = (i == hovered_entry);
        uint32_t border, fill, label_color;

        if (windows[i].state == WINDOW_CLOSED) {
            continue;
        }
        taskbar_entry_rect(bar, i, &x, &y, &w, &h);

        if (is_active) {
            border = TASKBAR_ENTRY_ACTIVE_COLOR;
            fill = TASKBAR_ENTRY_ACTIVE_COLOR;
            label_color = TASKBAR_ENTRY_LABEL_COLOR_ACTIVE;
        } else if (is_hovered) {
            border = TASKBAR_ENTRY_BORDER_COLOR;
            fill = TASKBAR_ENTRY_HOVER_COLOR;
            label_color = (windows[i].state == WINDOW_MINIMIZED) ? TASKBAR_ENTRY_LABEL_COLOR_MINIMIZED
                                                                  : TASKBAR_ENTRY_LABEL_COLOR;
        } else {
            border = TASKBAR_ENTRY_BORDER_COLOR_IDLE;
            fill = TASKBAR_ENTRY_FILL_COLOR;
            label_color = (windows[i].state == WINDOW_MINIMIZED) ? TASKBAR_ENTRY_LABEL_COLOR_MINIMIZED
                                                                  : TASKBAR_ENTRY_LABEL_COLOR;
        }

        gfx_fill_rounded_rect(x - 1, y - 1, w + 2, h + 2, TASKBAR_ENTRY_RADIUS, border);
        gfx_fill_rounded_rect(x, y, w, h, TASKBAR_ENTRY_RADIUS, fill);
        text_puts(x + 6, y + (h - GLYPH_HEIGHT) / 2, windows[i].title, label_color, 1);
    }
}
