#ifndef ACID_VM_HOST_H
#define ACID_VM_HOST_H

#include "queue.h"

struct vm_host_params
{
    const char * script_path;
    QueueHandle_t queue;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
};

/* pvParameters must point to a heap-allocated struct vm_host_params -- the
 * task frees it itself once the app's script has finished running. */
void vm_host_task( void * pvParameters );

#endif
