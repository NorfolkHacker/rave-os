#ifndef RAVEOS_TASKBAR_H
#define RAVEOS_TASKBAR_H

#include "window.h"

#define TASKBAR_HEIGHT 24

struct taskbar {
    int x, y, w, h;
};

/* Index of the window entry hit by (px, py), or -1. A WINDOW_CLOSED
 * window has no entry at all -- closed windows only show up as desktop
 * icons (once those exist), not on the taskbar; open and minimized ones
 * both do, since both are "running." `count` is the number of windows in
 * `windows`/`z_order` (kernel.c's MAX_WINDOWS) -- taskbar.c doesn't need
 * to know that constant by name. */
int taskbar_hit_entry(const struct taskbar *bar, const struct window *windows, int count, int px, int py);

void taskbar_draw(const struct taskbar *bar, const struct window *windows, const int *z_order, int count,
                  int hovered_entry);

#endif
