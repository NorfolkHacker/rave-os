#ifndef ACID_KERNEL_APP_CONTEXT_H
#define ACID_KERNEL_APP_CONTEXT_H

/* queue.h (below) requires FreeRTOS.h to already be included in the same
 * translation unit -- true even via this header, since gfx_binding.c and
 * event_binding.c pull this in without including FreeRTOS.h/task.h
 * themselves first. Including it here keeps that ordering requirement
 * local to this header instead of leaking it to every includer. */
#include "FreeRTOS.h"
#include "queue.h"

/* Stored in mrb->ud (mrb_state's own auxiliary-data field, confirmed present
 * at v2/components/mruby/include/mruby.h:480) for the lifetime of one app's
 * VM, so any mruby binding can reach its owning app's queue and window
 * geometry without a separate lookup mechanism. */
struct kernel_app_context
{
    QueueHandle_t queue;
    void * redraw_done_sem;   /* opaque SemaphoreHandle_t (binary) */
    /* This app's own private offscreen canvas -- every drawing binding
     * (gfx_binding.c, chrome_binding.c) targets this, using coordinates
     * relative to the window's own origin (0,0), never the real screen
     * directly. See hal_display.h's own comment. */
    void * canvas;
    const char * arg;   /* optional startup string, NULL if none -- see kernel_spawn.h */
    int window_x;
    int window_y;
    int window_w;
    int window_h;
};

#endif
