#include <stdio.h>

#include "../core/hal/hal_meminfo.h"

/* /proc/self/status's VmRSS -- this whole simulator process's real
 * resident memory (every app's mruby VM, every window's canvas, the
 * framebuffer, all of it), the only meaningful "memory" number a sim
 * process running as an ordinary Linux process can honestly report.
 * Linux-only, sim-only -- there is no equivalent file on the real ESP32
 * hardware target, see hal_meminfo_hw.c. */
long
hal_meminfo_used_kb( void )
{
    FILE * f = fopen( "/proc/self/status", "r" );
    if( f == NULL )
    {
        return -1;
    }
    char line[ 256 ];
    long kb = -1;
    while( fgets( line, sizeof( line ), f ) != NULL )
    {
        if( sscanf( line, "VmRSS: %ld kB", &kb ) == 1 )
        {
            break;
        }
    }
    fclose( f );
    return kb;
}
