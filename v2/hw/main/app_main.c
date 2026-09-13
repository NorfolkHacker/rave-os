#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../core/vm_host/vm_host.h"

void
app_main( void )
{
    xTaskCreate( vm_host_task, "vm_host", 16384, NULL, 5, NULL );
}
