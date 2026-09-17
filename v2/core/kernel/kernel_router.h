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

/* Which window's task currently has keyboard focus, or NULL if none does
 * (nothing has been clicked/activated yet). Used by window_binding.c so
 * Ruby can report a window's focused state (the taskbar highlights it). */
void * kernel_router_get_focus( void );

/* Clears g_focus_task if it currently equals `task` -- a no-op otherwise.
 * Called from vm_host_task's own unconditional per-app cleanup (every exit
 * path: normal end, unhandled exception, failed script load), covering
 * every way a task can end, not just the close-button path (which already
 * clears focus itself, inline, since it also needs to send the CLOSE event
 * first). */
void kernel_router_clear_focus( void * task );

#endif
