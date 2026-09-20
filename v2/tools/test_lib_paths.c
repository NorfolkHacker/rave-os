/* Standalone test for lib_path_is_safe (v2/core/vm_host/lib_paths.c).
 * Not part of either CMake build -- that function deliberately depends on
 * nothing but libc, so it compiles and runs on its own in one cc command
 * (see the header of this file's own run instructions in the plan). */
#include <stdio.h>
#include <string.h>

#include "../core/vm_host/lib_paths.h"

static int g_fails = 0;

static void
check( const char * path, int expected )
{
    int got = lib_path_is_safe( path );
    if( got == expected )
    {
        printf( "  ok  %-28s -> %d\n", path ? path : "(null)", got );
    }
    else
    {
        printf( "FAIL  %-28s -> %d, expected %d\n", path ? path : "(null)", got, expected );
        g_fails++;
    }
}

int
main( void )
{
    check( "editor/buffer.rb", 1 );
    check( "hl.rb", 1 );
    check( "a/b/c/deep.rb", 1 );
    check( "weird..name.rb", 1 );   /* ".." as substring, not as a component */

    check( NULL, 0 );
    check( "", 0 );
    check( "../secret.rb", 0 );
    check( "editor/../../secret.rb", 0 );
    check( "..", 0 );
    check( "/etc/passwd", 0 );
    check( "/abs/path.rb", 0 );
    check( "editor//buffer.rb", 0 );
    check( "editor\\buffer.rb", 0 );
    check( "editor/buffer.txt", 0 );
    check( "buffer", 0 );
    check( ".rb", 1 );              /* odd but harmless: a file literally named ".rb" */

    if( g_fails > 0 )
    {
        printf( "%d failure(s)\n", g_fails );
        return 1;
    }
    printf( "all passed\n" );
    return 0;
}
