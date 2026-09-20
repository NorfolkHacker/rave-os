#include <stddef.h>
#include <string.h>

#include "lib_paths.h"

int
lib_path_is_safe( const char * path )
{
    if( path == NULL || path[ 0 ] == '\0' )
    {
        return 0;
    }
    if( path[ 0 ] == '/' )
    {
        return 0;
    }
    if( strchr( path, '\\' ) != NULL )
    {
        return 0;
    }

    size_t len = strlen( path );
    if( len < 3 || strcmp( path + len - 3, ".rb" ) != 0 )
    {
        return 0;
    }

    /* Walk components rather than searching for ".." as a substring: a
     * file called "weird..name.rb" is perfectly fine, and "a/../../x.rb"
     * is not -- only a component that IS ".." escapes the directory. An
     * empty component (from "a//b.rb" or a trailing '/') is rejected too,
     * since it means the path wasn't written the way it appears. */
    const char * p = path;
    while( *p != '\0' )
    {
        const char * seg = p;
        while( *p != '\0' && *p != '/' )
        {
            p++;
        }
        size_t seg_len = ( size_t ) ( p - seg );
        if( seg_len == 0 )
        {
            return 0;
        }
        if( seg_len == 2 && seg[ 0 ] == '.' && seg[ 1 ] == '.' )
        {
            return 0;
        }
        if( *p == '/' )
        {
            p++;
        }
    }
    return 1;
}
