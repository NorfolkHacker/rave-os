#include <assert.h>
#include <stdio.h>

#include "synth.h"

int
main( void )
{
    synth_init();

    /* Voice 0, ona 49 = A4 = 440 Hz (per synth.h's own documented
     * numbering: "Standard 88-key piano numbering, ona 49 = A4 = 440.0Hz").
     * synth_init() leaves attack_rate at its maximum (SYNTH_ENV_FULL << 8),
     * so gating on should bring the envelope to full volume within a
     * handful of samples, not a slow fade. */
    synth_set_voice_waveform( 0, WAVE_PULSE );
    synth_set_ona( 0, 49 );
    synth_gate_on( 0 );

    unsigned char buf[ 512 ];
    synth_render_half( buf, sizeof( buf ) );

    /* Real audio, not silence: at least some bytes must differ from 128
     * (the documented silence value) once the voice has been gated on
     * and rendered for this many samples. */
    int i;
    int saw_non_silence = 0;
    for( i = 0; i < 512; i++ )
    {
        if( buf[ i ] != 128 )
        {
            saw_non_silence = 1;
            break;
        }
    }
    assert( saw_non_silence );

    /* Gate off, then render enough samples for the release stage (default
     * release_rate is also SYNTH_ENV_FULL << 8, i.e. fast) to bring the
     * envelope back down. A voice with envelope_stage == ENV_OFF (or
     * ENV_RELEASE settled at envelope_level == 0) renders pure silence. */
    synth_gate_off( 0 );
    unsigned char buf2[ 4096 ];
    synth_render_half( buf2, sizeof( buf2 ) );

    int all_silent_after_release = 1;
    for( i = 0; i < 4096; i++ )
    {
        if( buf2[ i ] != 128 )
        {
            all_silent_after_release = 0;
            break;
        }
    }
    assert( all_silent_after_release );

    printf( "test_synth: all assertions passed\n" );
    return 0;
}
