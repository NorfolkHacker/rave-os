#include <string.h>

#include "esp_log.h"
#include "../../core/hal/hal_network.h"

static const char * TAG = "hal_network_hw";

/* Stub -- no WiFi/network stack wired up on this target yet (same status
 * as hal_meminfo_hw.c's own stub). */
int
hal_network_get_info( char * hostname_out, int hostname_len,
                       char * ip_out, int ip_len )
{
    ESP_LOGI( TAG, "hal_network_get_info: stub, no network stack wired up yet" );
    strncpy( hostname_out, "acidos", ( size_t ) hostname_len );
    hostname_out[ hostname_len - 1 ] = '\0';
    strncpy( ip_out, "none", ( size_t ) ip_len );
    ip_out[ ip_len - 1 ] = '\0';
    return 0;
}
