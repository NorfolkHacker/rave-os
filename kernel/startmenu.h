#ifndef RAVEOS_STARTMENU_H
#define RAVEOS_STARTMENU_H

#define STARTMENU_BUTTON_WIDTH 64

/* Popup item order, top of the popup to bottom (closest to the button).
 * FORTH, FILES, and SHELL are the three windows that exist at all now,
 * all launched directly since no window opens on its own at boot
 * anymore (see kmain()) and there's no desktop-icon fallback either
 * (removed on request -- every window already has a menu launcher, so
 * the icon column was pure redundancy). CONFIG/GAMES are the two folder
 * shortcuts kept on request despite FILES already being able to browse
 * there itself -- unlike a plain FILES launch, they also navigate cwd
 * (kernel.c's open_files_at()). FX and EXIT are the two system actions
 * that used to live in the old PANEL demo window, EXIT last, same spot
 * a real start menu puts its power option. */
#define STARTMENU_ITEM_FORTH 0
#define STARTMENU_ITEM_FILES 1
#define STARTMENU_ITEM_SHELL 2
#define STARTMENU_ITEM_CONFIG 3
#define STARTMENU_ITEM_GAMES 4
#define STARTMENU_ITEM_FX 5
#define STARTMENU_ITEM_EXIT 6
#define STARTMENU_ITEM_COUNT 7

/* x/y/w/h describe the start button itself -- flush with the taskbar's
 * own left edge and the same height, so kernel.c narrows the taskbar's
 * rect to make room and the two read as one continuous bottom bar. The
 * popup's rect is derived from this plus the fixed item count, the same
 * way desktop_icon.c derives each icon's rect from its column origin. */
struct startmenu {
    int x, y, w, h;
    int open;
};

int startmenu_hit_button(const struct startmenu *menu, int px, int py);

/* Index of the popup item hit by (px, py), or -1. Only ever non-empty
 * while menu->open -- a closed menu has no clickable items. */
int startmenu_hit_item(const struct startmenu *menu, int px, int py);

/* Union of the button and the popup's full span, regardless of open
 * state -- so a caller redrawing over "the popup just closed" still
 * repaints the area it occupied last frame. */
void startmenu_bounds(const struct startmenu *menu, int *x0, int *y0, int *x1, int *y1);

void startmenu_draw(const struct startmenu *menu, int hovered_item, int fx_enabled);

#endif
