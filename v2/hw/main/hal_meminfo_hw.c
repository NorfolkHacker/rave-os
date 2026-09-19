#include "esp_log.h"
#include "../../core/hal/hal_meminfo.h"

static const char * TAG = "hal_meminfo_hw";

long
hal_meminfo_used_kb( void )
{
    ESP_LOGI( TAG, "hal_meminfo_used_kb: stub, no real reading wired up yet" );
    return -1;
}
