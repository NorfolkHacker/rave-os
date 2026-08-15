/* The bottom-left start menu: a small button flush with the taskbar's
 * left edge (kernel.c narrows the taskbar's own rect to make room, so
 * together the two read as one continuous bottom bar) that pops open a
 * fixed list of shortcuts -- the real actions that used to live in the
 * PANEL demo window (EXIT, FX) plus quick links into the standard
 * system folders fs_bootstrap_dirs() already guarantees exist. Same
 * fixed-slot-per-index shape as taskbar.c/desktop_icon.c: there are
 * only ever STARTMENU_ITEM_COUNT possible rows, so a static per-index
 * rect function is simpler than a real menu-widget framework. */

#include "startmenu.h"
#include "graphics.h"
#include "text.h"

/* Deliberately duplicate literal values, not shared #defines -- every
 * widget file in this codebase (taskbar.c, desktop_icon.c) keeps its
 * own copies of the shared androidacid.com palette rather than pulling
 * in a colors header, and STARTMENU_BG_COLOR/STARTMENU_BORDER_COLOR are
 * chosen to exactly match taskbar.c's TASKBAR_BG_COLOR/TASKBAR_BORDER_COLOR
 * so the button reads as part of the same bar. */
#define STARTMENU_BG_COLOR 0x070C09
#define STARTMENU_BORDER_COLOR 0x00FF66
#define STARTMENU_ITEM_BORDER_COLOR 0x00FF66      /* --hard, hovered */
#define STARTMENU_ITEM_BORDER_COLOR_IDLE 0x1F2E27 /* dim, idle */
#define STARTMENU_ITEM_FILL_COLOR 0x0A1A12
#define STARTMENU_ITEM_HOVER_COLOR 0x123322
#define STARTMENU_ITEM_LABEL_COLOR 0xD4E6DB
#define STARTMENU_LABEL_COLOR 0xD4E6DB
#define STARTMENU_POPUP_WIDTH 110
#define STARTMENU_ITEM_HEIGHT 20
#define STARTMENU_ITEM_MARGIN 6
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */

/* Item index -> screen rect. Popup grows upward from the button: index
 * STARTMENU_ITEM_COUNT - 1 (EXIT) sits immediately above the button,
 * index 0 (CONFIG) at the top of the stack. */
static void startmenu_item_rect(const struct startmenu *menu, int index, int *x, int *y, int *w, int *h) {
    *x = menu->x;
    *w = STARTMENU_POPUP_WIDTH;
    *h = STARTMENU_ITEM_HEIGHT;
    *y = menu->y - (STARTMENU_ITEM_COUNT - index) * STARTMENU_ITEM_HEIGHT;
}

int startmenu_hit_button(const struct startmenu *menu, int px, int py) {
    return px >= menu->x && px < menu->x + menu->w && py >= menu->y && py < menu->y + menu->h;
}

int startmenu_hit_item(const struct startmenu *menu, int px, int py) {
    int i;
    if (!menu->open) {
        return -1;
    }
    for (i = 0; i < STARTMENU_ITEM_COUNT; i++) {
        int x, y, w, h;
        startmenu_item_rect(menu, i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            return i;
        }
    }
    return -1;
}

void startmenu_bounds(const struct startmenu *menu, int *x0, int *y0, int *x1, int *y1) {
    int top_x, top_y, top_w, top_h;
    int popup_right = menu->x + STARTMENU_POPUP_WIDTH;
    int button_right = menu->x + menu->w;

    startmenu_item_rect(menu, 0, &top_x, &top_y, &top_w, &top_h);
    *x0 = menu->x;
    *y0 = top_y;
    *x1 = popup_right > button_right ? popup_right : button_right;
    *y1 = menu->y + menu->h;
}

static const char *startmenu_item_label(int index) {
    switch (index) {
    case STARTMENU_ITEM_FORTH:
        return "FORTH";
    case STARTMENU_ITEM_FILES:
        return "FILES";
    case STARTMENU_ITEM_CONFIG:
        return "CONFIG";
    case STARTMENU_ITEM_GAMES:
        return "GAMES";
    case STARTMENU_ITEM_EXIT:
        return "EXIT";
    default:
        return "";
    }
}

void startmenu_draw(const struct startmenu *menu, int hovered_item, int fx_enabled) {
    int i;

    gfx_fill_rect(menu->x, menu->y, menu->w, menu->h, STARTMENU_BG_COLOR);
    gfx_fill_rect(menu->x, menu->y, menu->w, 1, STARTMENU_BORDER_COLOR);
    text_puts(menu->x + STARTMENU_ITEM_MARGIN, menu->y + (menu->h - GLYPH_HEIGHT) / 2, "MENU", STARTMENU_LABEL_COLOR,
              1);

    if (!menu->open) {
        return;
    }

    for (i = 0; i < STARTMENU_ITEM_COUNT; i++) {
        int x, y, w, h;
        int is_hovered = (i == hovered_item);
        uint32_t border = is_hovered ? STARTMENU_ITEM_BORDER_COLOR : STARTMENU_ITEM_BORDER_COLOR_IDLE;
        uint32_t fill = is_hovered ? STARTMENU_ITEM_HOVER_COLOR : STARTMENU_ITEM_FILL_COLOR;

        startmenu_item_rect(menu, i, &x, &y, &w, &h);
        gfx_fill_rect(x, y, w, h, fill);
        gfx_fill_rect(x, y, w, 1, border);

        if (i == STARTMENU_ITEM_FX) {
            /* "FX [X]" / "FX [ ]" -- label leads so it starts at the same
             * left column every other item's plain label does (a leading
             * "[ ] " previously pushed "FX" out of alignment with them).
             * Built by hand rather than pulled from a table, same as this
             * file's other short labels. */
            char label[8];
            label[0] = 'F';
            label[1] = 'X';
            label[2] = ' ';
            label[3] = '[';
            label[4] = fx_enabled ? 'X' : ' ';
            label[5] = ']';
            label[6] = 0;
            text_puts(x + STARTMENU_ITEM_MARGIN, y + (h - GLYPH_HEIGHT) / 2, label, STARTMENU_ITEM_LABEL_COLOR, 1);
        } else {
            text_puts(x + STARTMENU_ITEM_MARGIN, y + (h - GLYPH_HEIGHT) / 2, startmenu_item_label(i),
                      STARTMENU_ITEM_LABEL_COLOR, 1);
        }
    }
}
