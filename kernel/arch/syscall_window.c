#include "syscall.h"

/* windows[]/z_order[]/raise_window() are all private to kernel.c, and
 * there's no window-management header the way fs.h/graphics.h/synth.h
 * exist for the other syscall families -- so these are declared extern
 * directly at the call site, matching this codebase's existing
 * context_switch()/enter_ring3() convention. */
extern void window_ring3_open(int x, int y, int w, int h, const char *title);
extern void window_ring3_close(void);
extern void ring3_wait_event(int *type, int *x, int *y);

int syscall_dispatch_window(int num, int arg) {
    if (num == SYS_WINDOW_OPEN) {
        const struct sys_window_open_args *a = (const struct sys_window_open_args *)arg;
        window_ring3_open(a->x, a->y, a->w, a->h, a->title);
        return 0;
    }
    if (num == SYS_WINDOW_CLOSE) {
        window_ring3_close();
        return 0;
    }
    if (num == SYS_WAIT_EVENT) {
        struct sys_wait_event_args *a = (struct sys_wait_event_args *)arg;
        ring3_wait_event(&a->type, &a->x, &a->y);
        return 0;
    }
    return syscall_dispatch_core(num, arg);
}
