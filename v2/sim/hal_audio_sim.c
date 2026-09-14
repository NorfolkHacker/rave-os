#include <SDL2/SDL.h>

#include "../core/hal/hal_audio.h"
#include "../core/kernel/kernel_audio.h"
#include "../core/audio/synth.h"

static void
sdl_audio_callback( void * userdata, Uint8 * stream, int len )
{
    ( void ) userdata;
    kernel_audio_drain_and_render( ( unsigned char * ) stream, ( unsigned int ) len );
}

void
hal_audio_init( void )
{
    SDL_InitSubSystem( SDL_INIT_AUDIO );

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero( desired );
    desired.freq = SYNTH_SAMPLE_RATE;
    desired.format = AUDIO_U8;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = sdl_audio_callback;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice( NULL, 0, &desired, &obtained, 0 );
    SDL_PauseAudioDevice( dev, 0 );
}
