#include "FreeRTOS.h"
#include "queue.h"

#include "kernel_audio.h"
#include "kernel_audio_command.h"
#include "../audio/synth.h"

#define KERNEL_AUDIO_QUEUE_LEN 16

static QueueHandle_t g_audio_queue;
static void * g_voice_owner[ SYNTH_NUM_VOICES ];

void
kernel_audio_init( void )
{
    synth_init();
    g_audio_queue = xQueueCreate( KERNEL_AUDIO_QUEUE_LEN, sizeof( struct kernel_audio_command ) );

    int i;
    for( i = 0; i < SYNTH_NUM_VOICES; i++ )
    {
        g_voice_owner[ i ] = NULL;
    }
}

static void
enqueue( struct kernel_audio_command * cmd )
{
    xQueueSendToBack( g_audio_queue, cmd, 0 );
}

void
kernel_audio_enqueue_note_on( void * owner_task, int voice, int ona, int volume )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_NOTE_ON;
    cmd.voice = voice;
    cmd.ona = ona;
    cmd.volume = volume;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

void
kernel_audio_enqueue_note_off( void * owner_task, int voice )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_NOTE_OFF;
    cmd.voice = voice;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

void
kernel_audio_release_owner( void * owner_task )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_RELEASE_OWNER;
    cmd.voice = 0;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

/* Only ever called from the audio callback's own thread -- see this
 * file's header comment on kernel_audio_drain_and_render. Every mutation
 * of synth_voices[]/g_voice_owner[] happens here and nowhere else. */
static void
apply( const struct kernel_audio_command * cmd )
{
    if( cmd->type == AUDIO_CMD_NOTE_ON )
    {
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        synth_set_ona( cmd->voice, cmd->ona );
        /* volume is 0..100 from the Ruby-facing API; synth's own envelope
         * scale is 0..SYNTH_ENV_FULL (32768) -- sustain_level is what
         * actually caps a gated-on voice's held volume. */
        synth_voices[ cmd->voice ].sustain_level =
            ( ( cmd->volume < 0 ? 0 : ( cmd->volume > 100 ? 100 : cmd->volume ) )
              * SYNTH_ENV_FULL / 100 ) << 8; /* Q8 scale, per synth.h's own struct comment */
        synth_gate_on( cmd->voice );
        g_voice_owner[ cmd->voice ] = cmd->owner_task;
    }
    else if( cmd->type == AUDIO_CMD_NOTE_OFF )
    {
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        /* Unconditional, per this plan's spec: whichever voice is
         * currently there gets gated off, regardless of who is asking --
         * see the spec's "Ambiguity closed explicitly" note on voice
         * stealing. */
        synth_gate_off( cmd->voice );
        g_voice_owner[ cmd->voice ] = NULL;
    }
    else if( cmd->type == AUDIO_CMD_RELEASE_OWNER )
    {
        int i;
        for( i = 0; i < SYNTH_NUM_VOICES; i++ )
        {
            if( g_voice_owner[ i ] == cmd->owner_task )
            {
                synth_gate_off( i );
                g_voice_owner[ i ] = NULL;
            }
        }
    }
}

void
kernel_audio_drain_and_render( unsigned char * buf, unsigned int len )
{
    struct kernel_audio_command cmd;
    while( xQueueReceive( g_audio_queue, &cmd, 0 ) == pdTRUE )
    {
        apply( &cmd );
    }
    synth_render_half( buf, len );
}
