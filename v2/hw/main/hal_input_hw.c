#include "../../core/hal/hal_input.h"

int
hal_input_should_quit( void )
{
    return 0;
}

void
hal_input_poll_touch( int * x, int * y, bool * pressed )
{
    ( void ) x;
    ( void ) y;
    *pressed = false;
}
