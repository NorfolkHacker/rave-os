#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

extern "C" void sim_freertos_main( void );

static volatile bool * s_running = 0;

extern "C" int sim_should_quit( void )
{
    return ( s_running != 0 && *s_running == false ) ? 1 : 0;
}

static int freertos_thread_entry( bool * running )
{
    s_running = running;
    sim_freertos_main();  /* never returns on its own; the FreeRTOS side polls
                           * sim_should_quit() and calls exit(0) directly */
    return 0;
}

int main( int, char ** )
{
    return lgfx::Panel_sdl::main( freertos_thread_entry );
}
