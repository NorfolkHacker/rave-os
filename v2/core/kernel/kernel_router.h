#ifndef ACID_KERNEL_ROUTER_H
#define ACID_KERNEL_ROUTER_H

void kernel_router_task( void * pvParameters );

/* Call once at boot, right after spawning the desktop app, so the router
 * knows which window's queue owns the top strip unconditionally (see
 * kernel_router_poll). Passing NULL disables the special case. */
void kernel_router_set_desktop_task( void * task );

/* Raises a window to front AND makes it the keyboard focus target, in one
 * step -- the one function both a direct click on a window (this file's
 * own fresh_press handling) and a taskbar tap (Task 3's acid_activate_
 * window binding) call, so there is exactly one rule for how a window
 * gains keyboard focus, not two competing ones. */
void kernel_router_activate_window( void * task );

#endif
