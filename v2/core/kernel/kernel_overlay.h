#ifndef ACID_KERNEL_OVERLAY_H
#define ACID_KERNEL_OVERLAY_H

/* One screen-sized canvas, owned by the kernel rather than by any window,
 * composited last (kernel_router.c's kernel_router_composite_frame) with
 * ACID_OVERLAY_KEY treated as transparent. It is how an app draws over the
 * WHOLE screen -- across the wallpaper, the desktop strip and every open
 * window -- which nothing else here can do: every app draws only into its
 * own window's canvas in window-relative coordinates, and each of those is
 * blitted opaquely.
 *
 * Deliberately not a window. It owns no task and no mruby VM, is never
 * hit-tested (so clicks land on whatever is really underneath), never takes
 * keyboard focus, and never appears in the taskbar -- none of which needs
 * any code here, because a window is the only thing that gets any of it.
 * The terminal's easter eggs (apps/lib/acid_eggs.rb) are the first caller;
 * spawning an app for them would have cost an 8KB task stack, a whole
 * mrb_open() and six Ruby files compiled from source (kernel_spawn.c:75,
 * vm_host.c:260-276) to fly a sprite for three seconds.
 *
 * Not thread-safe against itself: one caller at a time, which is what the
 * single acid_overlay_* binding surface gives. Drawing into the canvas from
 * an app task while the router task blits it is the same benign race every
 * window canvas already has -- the blit is a plain memory copy, so the
 * worst case is one frame showing a sprite mid-move. */

/* Claims the overlay for `owner_task`, allocating its canvas on first use
 * and clearing it to ACID_OVERLAY_KEY. Returns 1, or 0 if ANOTHER task
 * already holds it, or if the canvas could not be allocated. Re-opening
 * from the task that already owns it succeeds and re-clears.
 *
 * One owner at a time, and that is the point: there is exactly one canvas,
 * so a second animation starting mid-flight would clear the first one's
 * frames and the two would fight over it. This cannot be enforced by the
 * caller -- apps/terminal.rb is `multi = true`, so two terminal windows are
 * two separate mruby VMs, each with its own copy of AcidEggs' module state,
 * neither able to see the other. The claim is what makes "only one easter
 * egg on screen at a time" true across the whole OS rather than inside one
 * VM.
 *
 * A refusal is not an error worth showing anyone: an easter egg that
 * declines to fire because another one is already flying should simply do
 * nothing. */
int kernel_overlay_open( void * owner_task );

/* Hides the overlay from the compositor and releases the claim. A no-op
 * unless `owner_task` is the current owner, so one app can never close
 * another's animation. Deliberately does NOT free the canvas -- see
 * kernel_overlay.c. */
void kernel_overlay_close( void * owner_task );

/* Drops `owner_task`'s claim if it holds one, and does nothing otherwise.
 * Called from vm_host_task's unconditional per-app cleanup for EVERY app
 * that exits, beside the kernel_audio_release_owner call already there, so
 * that an app which dies mid-animation -- normally, or on an unhandled Ruby
 * exception -- cannot leave the overlay claimed forever by a task that no
 * longer exists. */
void kernel_overlay_release_owner( void * owner_task );

int kernel_overlay_is_open( void );

/* The compositor's read: the canvas to blit, or NULL when closed. Ownership
 * is irrelevant here -- the router blits whatever is open, whoever opened
 * it. */
void * kernel_overlay_canvas( void );

/* A drawer's read: the canvas to draw into, or NULL when the overlay is
 * closed OR `owner_task` is not the task holding it. Every drawing binding
 * goes through this rather than kernel_overlay_canvas, so a non-owner
 * silently draws nothing instead of scribbling on someone else's frame. */
void * kernel_overlay_canvas_for( void * owner_task );

#endif
