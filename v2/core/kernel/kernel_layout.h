#ifndef ACID_KERNEL_LAYOUT_H
#define ACID_KERNEL_LAYOUT_H

/* Per-window chrome geometry, shared between kernel_router's hit-testing
 * (this task, and Task 5) and the chrome-drawing binding (Task 6) -- one
 * source of truth so a click always lands exactly where the chrome is
 * actually drawn. */
#define KERNEL_TITLE_BAR_H 16
#define KERNEL_CLOSE_BTN_R 5
#define KERNEL_CLOSE_BTN_MARGIN 8

/* The desktop's own top strip is a separate, larger concept from the
 * per-window title bar above -- kept as its own constant so the two never
 * get confused with each other. 24px (not the original 20) leaves real
 * room for phase 5's taskbar buttons (label + padding), not just a bare
 * accent bar. */
#define KERNEL_DESKTOP_STRIP_H 24

#endif
