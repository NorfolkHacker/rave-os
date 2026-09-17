#include <assert.h>
#include <stdio.h>

#include "kernel_window.h"

int
main( void )
{
    kernel_window_init();

    int dummy_a, dummy_b, dummy_c;
    void * task_a = &dummy_a;
    void * task_b = &dummy_b;
    void * task_c = &dummy_c;

    assert( kernel_window_count() == 0 );
    assert( kernel_window_register( task_a, NULL, NULL, "a", 0, 0, 100, 100, 1 ) == 1 );
    assert( kernel_window_register( task_b, NULL, NULL, "b", 50, 50, 100, 100, 1 ) == 1 );
    assert( kernel_window_count() == 2 );

    /* (60,60) is inside both; b was registered later so it has the higher
     * z-order and should win the hit-test. */
    struct kernel_window * hit = kernel_window_find_at( 60, 60 );
    assert( hit != NULL && hit->task == task_b );

    hit = kernel_window_find_at( 10, 10 );
    assert( hit != NULL && hit->task == task_a );

    hit = kernel_window_find_at( 500, 500 );
    assert( hit == NULL );

    /* Bring a to front; the overlap region should now hit a instead. */
    kernel_window_bring_to_front( task_a );
    hit = kernel_window_find_at( 60, 60 );
    assert( hit != NULL && hit->task == task_a );

    kernel_window_unregister( task_a );
    assert( kernel_window_count() == 1 );
    hit = kernel_window_find_at( 10, 10 );
    assert( hit == NULL );

    /* Fill the remaining slots and confirm the cap is enforced. */
    int i;
    int registered = 1; /* b is still registered */
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        void * extra_task = ( void * ) ( long ) ( 1000 + i );
        if( kernel_window_register( extra_task, NULL, NULL, "extra", 0, 0, 1, 1, 1 ) )
        {
            registered++;
        }
    }
    assert( registered == KERNEL_WINDOW_MAX );
    assert( kernel_window_register( task_c, NULL, NULL, "c", 0, 0, 1, 1, 1 ) == 0 );

    printf( "kernel_window: all assertions passed\n" );
    return 0;
}
