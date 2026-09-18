#include <stdatomic.h>

#include "FreeRTOS.h"
#include "task.h"

#include "kernel_audio.h"
#include "kernel_audio_command.h"
#include "../audio/synth.h"

#define KERNEL_AUDIO_QUEUE_LEN 16 /* must be a power of two -- see mask use below */

static struct kernel_audio_command g_audio_cmd_buf[ KERNEL_AUDIO_QUEUE_LEN ];
/* head: next slot the audio thread will read. Written only by the audio
 * thread, read by producers. tail: next slot a producer will write.
 * Written only inside the producer-side critical section, read by the
 * audio thread. Both _Atomic so the foreign (non-FreeRTOS) audio thread
 * can touch them without depending on any FreeRTOS synchronization
 * primitive -- see this file's header comment and the final-review
 * finding that motivated this design (a raw xQueueReceive/xQueueSendToBack
 * pair races when one side is not a FreeRTOS-tracked thread). */
static _Atomic unsigned int g_audio_cmd_head;
static _Atomic unsigned int g_audio_cmd_tail;

static void * g_voice_owner[ SYNTH_NUM_VOICES ];

void
kernel_audio_init( void )
{
    synth_init();
    atomic_store_explicit( &g_audio_cmd_head, 0, memory_order_relaxed );
    atomic_store_explicit( &g_audio_cmd_tail, 0, memory_order_relaxed );

    int i;
    for( i = 0; i < SYNTH_NUM_VOICES; i++ )
    {
        g_voice_owner[ i ] = NULL;
    }
}

/* Producer side only -- called from app tasks (real FreeRTOS tasks), never
 * from the audio thread. taskENTER_CRITICAL/taskEXIT_CRITICAL correctly
 * serialize FreeRTOS tasks against each other on this port; they are never
 * used by the audio thread, so this never crosses the FreeRTOS/foreign-
 * thread boundary. Returns false if the ring buffer is full (caller
 * decides whether that's acceptable to drop or worth retrying). */
static bool
try_enqueue( const struct kernel_audio_command * cmd )
{
    bool ok = false;
    taskENTER_CRITICAL();
    unsigned int tail = g_audio_cmd_tail;
    unsigned int head = atomic_load_explicit( &g_audio_cmd_head, memory_order_acquire );
    if( ( tail - head ) < KERNEL_AUDIO_QUEUE_LEN )
    {
        g_audio_cmd_buf[ tail & ( KERNEL_AUDIO_QUEUE_LEN - 1 ) ] = *cmd;
        atomic_store_explicit( &g_audio_cmd_tail, tail + 1, memory_order_release );
        ok = true;
    }
    taskEXIT_CRITICAL();
    return ok;
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
    /* Best-effort: a dropped note_on/note_off under extreme load is an
     * acceptable, rare degradation for this phase -- unlike
     * AUDIO_CMD_RELEASE_OWNER below, whose loss would orphan a voice. */
    try_enqueue( &cmd );
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
    try_enqueue( &cmd );
}

void
kernel_audio_enqueue_configure_voice( int voice, int filter_route,
                                       int attack_ms, int decay_ms,
                                       int sustain_percent, int release_ms )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_CONFIGURE_VOICE;
    cmd.voice = voice;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = NULL;
    cmd.filter_route = filter_route;
    cmd.attack_ms = attack_ms;
    cmd.decay_ms = decay_ms;
    cmd.sustain_percent = sustain_percent;
    cmd.release_ms = release_ms;
    try_enqueue( &cmd );
}

void
kernel_audio_enqueue_configure_filter( int cutoff, int resonance, int filter_mode )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_CONFIGURE_FILTER;
    cmd.voice = 0;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = NULL;
    cmd.cutoff = cutoff;
    cmd.resonance = resonance;
    cmd.filter_mode = filter_mode;
    try_enqueue( &cmd );
}

void
kernel_audio_enqueue_trigger_arp( int voice, const int notes[4], int count, int rate_ms )
{
    struct kernel_audio_command cmd;
    int i;
    cmd.type = AUDIO_CMD_TRIGGER_ARP;
    cmd.voice = voice;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = NULL;
    for( i = 0; i < 4; i++ )
    {
        cmd.arp_notes[ i ] = notes[ i ];
    }
    cmd.arp_count = count;
    cmd.arp_rate_ms = rate_ms;
    try_enqueue( &cmd );
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

    /* Unlike note_on/note_off, this command must not be silently dropped:
     * losing it orphans a voice with no way for anyone to stop it. Called
     * from a genuine FreeRTOS task (vm_host_task's own cleanup), so it is
     * safe to block here -- vTaskDelay only ever runs on real FreeRTOS
     * task threads, never on the audio thread. The audio thread drains at
     * roughly one buffer (~46ms) per call, so 50ms of retrying is many
     * drain cycles of headroom. */
    TickType_t start = xTaskGetTickCount();
    while( !try_enqueue( &cmd ) )
    {
        if( ( xTaskGetTickCount() - start ) >= pdMS_TO_TICKS( 50 ) )
        {
            break;
        }
        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }
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
        /* Stop any arpeggio this voice was running too -- otherwise a
         * now-silent (gated off) voice would keep stepping phase_increment
         * forever, and the NEXT plain note_on (no arp) on this voice would
         * inherit whatever step it happened to land on. */
        synth_voices[ cmd->voice ].arp_active = 0;
        g_voice_owner[ cmd->voice ] = NULL;
    }
    else if( cmd->type == AUDIO_CMD_TRIGGER_ARP )
    {
        int i;
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        for( i = 0; i < 4; i++ )
        {
            synth_set_arp_note( cmd->voice, i, cmd->arp_notes[ i ] );
        }
        synth_set_arp_rate( cmd->voice, cmd->arp_rate_ms );
        /* Always restart from the first step -- see this command's own
         * comment in kernel_audio_command.h on why (repeated triggers,
         * e.g. one per enemy hit, must all sound the same, not continue
         * from wherever a previous run left off). synth_arp_on() itself
         * doesn't reset these (it only validates+sets count and flips
         * arp_active), so it's done directly here -- apply() is the one
         * place allowed to touch synth_voices[] fields with no setter. */
        synth_voices[ cmd->voice ].arp_step = 0;
        synth_voices[ cmd->voice ].arp_step_counter = 0;
        synth_arp_on( cmd->voice, cmd->arp_count );
    }
    else if( cmd->type == AUDIO_CMD_CONFIGURE_VOICE )
    {
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        synth_set_voice_filter_route( cmd->voice, cmd->filter_route );
        synth_set_adsr( cmd->voice, cmd->attack_ms, cmd->decay_ms,
                         cmd->sustain_percent, cmd->release_ms );
    }
    else if( cmd->type == AUDIO_CMD_CONFIGURE_FILTER )
    {
        synth_set_filter_cutoff( cmd->cutoff );
        synth_set_filter_resonance( cmd->resonance );
        synth_set_filter_mode( cmd->filter_mode );
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

/* Consumer side only -- called exclusively from the audio thread (the SDL
 * callback on sim; the future hw ISR/task on hw). Deliberately makes NO
 * FreeRTOS API calls at all: this thread is not a FreeRTOS task on sim, so
 * none of FreeRTOS's synchronization primitives are valid to call from it.
 * head/tail are _Atomic for exactly this reason. */
void
kernel_audio_drain_and_render( unsigned char * buf, unsigned int len )
{
    unsigned int head = atomic_load_explicit( &g_audio_cmd_head, memory_order_relaxed );
    unsigned int tail = atomic_load_explicit( &g_audio_cmd_tail, memory_order_acquire );
    while( head != tail )
    {
        struct kernel_audio_command cmd = g_audio_cmd_buf[ head & ( KERNEL_AUDIO_QUEUE_LEN - 1 ) ];
        apply( &cmd );
        head++;
    }
    atomic_store_explicit( &g_audio_cmd_head, head, memory_order_release );
    synth_render_half( buf, len );
}
