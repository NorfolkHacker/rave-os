#ifndef ACID_KERNEL_LAYOUT_H
#define ACID_KERNEL_LAYOUT_H

/* Per-window chrome geometry, shared between kernel_router's hit-testing
 * (this task, and Task 5) and the chrome-drawing binding (Task 6) -- one
 * source of truth so a click always lands exactly where the chrome is
 * actually drawn. */
#define KERNEL_TITLE_BAR_H 16
#define KERNEL_CLOSE_BTN_R 5
#define KERNEL_CLOSE_BTN_MARGIN 8

/* The desktop's own top strip is a separate, larger concept (Task 7) --
 * defined here now so Task 5's close-button geometry and this one never
 * get confused with each other. */
#define KERNEL_DESKTOP_STRIP_H 20

#endif
