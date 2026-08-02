#ifndef RAVEOS_WINDOW_H
#define RAVEOS_WINDOW_H

#define WINDOW_TITLEBAR_HEIGHT 20

/* A closed window has been dismissed entirely (becomes a desktop icon,
 * once desktop icons exist) -- distinct from minimized, which keeps it
 * alive off-screen (recoverable from a taskbar, once one exists). Neither
 * behavior is wired up yet; the state lives on the struct now because
 * every caller that touches struct window is being generalized to a real
 * window list in the same pass that would otherwise need to revisit them
 * again once minimize/close controls land. */
#define WINDOW_OPEN 0
#define WINDOW_MINIMIZED 1
#define WINDOW_CLOSED 2

/* x/y/w/h describe the body -- the title bar sits WINDOW_TITLEBAR_HEIGHT
 * pixels above (x,y), and the border is drawn 2px further out again. */
struct window {
    int x, y, w, h;
    const char *title;
    int state;
    int minimize_hovered;
    int close_hovered;
};

void window_draw(const struct window *win);

/* Point-in-rect test against just the title bar strip (not the body) --
 * what a caller should check before starting a drag. */
int window_titlebar_hit_test(const struct window *win, int px, int py);

#endif
