#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

extern "C" void sim_freertos_main( void );

static volatile bool * s_running = 0;

extern "C" int hal_input_should_quit( void )
{
    return ( s_running != 0 && *s_running == false ) ? 1 : 0;
}

static int freertos_thread_entry( bool * running )
{
    s_running = running;
    sim_freertos_main();  /* never returns on its own; the FreeRTOS side polls
                           * hal_input_should_quit() and calls exit(0) directly */
    return 0;
}

int main( int, char ** )
{
    /* Panel_sdl's own debug rotate/zoom hotkeys (bare r/l/1-6) collide with
     * this OS's real keyboard input -- typing plain text would also
     * rotate/rescale the display. Require Ctrl so the hotkeys stay
     * available without stealing keys apps need for real text entry. */
    lgfx::Panel_sdl::setShortcutKeymod( KMOD_LCTRL );
    return lgfx::Panel_sdl::main( freertos_thread_entry );
}
