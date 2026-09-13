#ifndef ACID_KERNEL_SPAWN_H
#define ACID_KERNEL_SPAWN_H

/* Spawns one app: a fresh FreeRTOS task running its own mruby VM against
 * script_path, with its own input queue, registered in the kernel window
 * list at (x, y, w, h). Returns the new task handle (cast to void*) on
 * success, or NULL if the queue or task couldn't be created. */
void * kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable );

#endif
