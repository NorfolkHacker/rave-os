#ifndef RAVEOS_WINDOW_H
#define RAVEOS_WINDOW_H

#define WINDOW_TITLEBAR_HEIGHT 20

/* x/y/w/h describe the body -- the title bar sits WINDOW_TITLEBAR_HEIGHT
 * pixels above (x,y), and the border is drawn 2px further out again. */
struct window {
    int x, y, w, h;
    const char *title;
};

void window_draw(const struct window *win);

#endif
