#ifndef RAVEOS_DESKTOP_ICON_H
#define RAVEOS_DESKTOP_ICON_H

#include "window.h"

#define DESKTOP_ICON_SIZE 40
#define DESKTOP_ICON_GAP 20
#define DESKTOP_ICON_MARGIN 16
#define DESKTOP_ICON_COLUMN_WIDTH 90

/* x/y/w/h describe the whole icon column's bounding box (one fixed slot
 * per possible window, stacked top-to-bottom) -- computed once at boot
 * by desktop_icons_init(), same spirit as struct taskbar's x/y/w/h. */
struct desktop_icons {
    int x, y, w, h;
};

/* Lays out the column's bounding box for up to `count` icons (kernel.c's
 * MAX_WINDOWS) starting at (origin_x, origin_y) -- kept in this file so
 * kernel.c doesn't need to duplicate the per-slot spacing formula just to
 * compute a damage-tracking bounding rect. */
void desktop_icons_init(struct desktop_icons *icons, int origin_x, int origin_y, int count);

/* Index of the closed window whose icon is at (px, py), or -1. Only a
 * WINDOW_CLOSED window has an icon -- open/minimized ones are already on
 * the desktop or the taskbar, not here. `count` is windows[]/z_order[]'s
 * length; desktop_icon.c doesn't need to know that constant by name. */
int desktop_icon_hit(const struct desktop_icons *icons, const struct window *windows, int count, int px, int py);

void desktop_icons_draw(const struct desktop_icons *icons, const struct window *windows, int count,
                        int hovered_index);

#endif
