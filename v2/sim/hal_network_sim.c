#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../core/hal/hal_network.h"

/* getifaddrs -- the real Linux interface list this simulator process
 * actually has, the same source `ip addr` itself reads. Picks the first
 * AF_INET, non-loopback interface with an address, which is the ordinary
 * "what's this machine's LAN IP" answer on a single-homed dev box; a
 * multi-homed box just gets whichever one getifaddrs happens to list
 * first, which is an acceptable, honestly-labeled simplification for a
 * Network app showing "am I on a network", not a routing table. */
int
hal_network_get_info( char * hostname_out, int hostname_len,
                       char * ip_out, int ip_len )
{
    if( gethostname( hostname_out, ( size_t ) hostname_len ) != 0 )
    {
        strncpy( hostname_out, "unknown", ( size_t ) hostname_len );
    }
    hostname_out[ hostname_len - 1 ] = '\0';

    strncpy( ip_out, "none", ( size_t ) ip_len );
    ip_out[ ip_len - 1 ] = '\0';

    struct ifaddrs * list = NULL;
    if( getifaddrs( &list ) != 0 )
    {
        return 0;
    }

    int found = 0;
    struct ifaddrs * it;
    for( it = list; it != NULL; it = it->ifa_next )
    {
        if( it->ifa_addr == NULL || it->ifa_addr->sa_family != AF_INET )
        {
            continue;
        }
        if( ( it->ifa_flags & IFF_LOOPBACK ) != 0 )
        {
            continue;
        }
        struct sockaddr_in * addr = ( struct sockaddr_in * ) ( void * ) it->ifa_addr;
        if( inet_ntop( AF_INET, &addr->sin_addr, ip_out, ( socklen_t ) ip_len ) != NULL )
        {
            found = 1;
            break;
        }
    }
    freeifaddrs( list );
    return found;
}
