#ifndef ACID_VM_HOST_H
#define ACID_VM_HOST_H

#include "queue.h"

struct vm_host_params
{
    const char * script_path;
    QueueHandle_t queue;
    void * redraw_done_sem;   /* opaque SemaphoreHandle_t (binary) */
    void * canvas;            /* opaque, see hal_display.h/kernel_window.h */
    const char * arg;         /* optional startup string, NULL if none -- see kernel_spawn.h */
    /* Comma-separated list of this app's own Ruby modules, relative to
     * v2/apps (e.g. "editor/buffer.rb, editor/hl.rb"), or NULL for an app
     * that has none. Loaded into this VM after the shared apps/lib/*.rb
     * set and before the app's own script, so a module can define classes
     * the script then uses at its top level. Same ownership contract as
     * script_path and arg: not copied here, the caller must keep it
     * alive for the task's lifetime. */
    const char * libs;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
};

/* pvParameters must point to a heap-allocated struct vm_host_params -- the
 * task frees it itself once the app's script has finished running. */
void vm_host_task( void * pvParameters );

/* Creates the mutex that serializes mruby's Prism parser across
 * concurrently-starting app VMs (see vm_host.c's own comment on
 * load_file_into_vm for why). Call exactly once, at boot, before any app
 * spawns -- same ordering discipline gfx_init()/kernel_window_init()/
 * kernel_audio_init() already established. */
void vm_host_init( void );

#endif
