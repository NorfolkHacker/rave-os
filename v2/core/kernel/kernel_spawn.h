#ifndef ACID_KERNEL_SPAWN_H
#define ACID_KERNEL_SPAWN_H

/* Spawns one app: a fresh FreeRTOS task running its own mruby VM against
 * script_path, with its own input queue, registered in the kernel window
 * list at (x, y, w, h). Returns the new task handle (cast to void*) on
 * success, or NULL if the queue or task couldn't be created.
 *
 * `arg` is an optional startup string the spawned app can read back via
 * the acid_launch_arg binding (NULL or "" both mean "no argument") --
 * e.g. a file path to open, for File Manager launching Editor on a
 * specific file rather than Editor's own hardcoded default. Same
 * ownership contract as script_path: this function does not copy it,
 * the caller must ensure it outlives the spawned task (every current
 * caller either passes a string literal or a heap copy that's never
 * freed -- see window_binding.c's dup_cstr).
 *
 * `libs` is this app's own comma-separated module list from its manifest
 * (NULL for none) -- see vm_host.h's own field comment, and the same
 * ownership contract as `arg`. */
void * kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable,
                          const char * arg, const char * libs );

#endif
