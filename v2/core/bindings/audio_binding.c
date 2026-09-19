#include "audio_binding.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../kernel/kernel_audio.h"

static mrb_value
acid_play_note( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, ona, volume;
    mrb_get_args( mrb, "iii", &voice, &ona, &volume );
    kernel_audio_enqueue_note_on( ( void * ) xTaskGetCurrentTaskHandle(),
                                   ( int ) voice, ( int ) ona, ( int ) volume );
    return mrb_nil_value();
}

/* Shapes a voice's envelope and filter routing -- see synth_init()'s own
 * comment on why the default (instant attack/release, unfiltered pulse)
 * sounds harsh, and AUDIO_CMD_CONFIGURE_VOICE's comment for why this has
 * to go through the audio command queue rather than touching
 * synth_voices[] directly from this (non-audio) thread. */
static mrb_value
acid_configure_voice( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, filter_route, attack_ms, decay_ms, sustain_percent, release_ms;
    mrb_get_args( mrb, "iiiiii", &voice, &filter_route, &attack_ms, &decay_ms,
                  &sustain_percent, &release_ms );
    kernel_audio_enqueue_configure_voice( ( int ) voice, ( int ) filter_route,
                                           ( int ) attack_ms, ( int ) decay_ms,
                                           ( int ) sustain_percent, ( int ) release_ms );
    return mrb_nil_value();
}

/* Sets the one shared filter's cutoff (0..255)/resonance (0..15)/mode
 * (SYNTH_FILTER_MODE_* bits, see synth.h) -- a global setting, not
 * per-voice (see AUDIO_CMD_CONFIGURE_FILTER's own comment). Only takes
 * effect for voices whose filter_route was turned on via
 * acid_configure_voice. */
static mrb_value
acid_configure_filter( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int cutoff, resonance, mode;
    mrb_get_args( mrb, "iii", &cutoff, &resonance, &mode );
    kernel_audio_enqueue_configure_filter( ( int ) cutoff, ( int ) resonance, ( int ) mode );
    return mrb_nil_value();
}

/* Starts a voice stepping through up to 4 notes instead of holding one
 * flat pitch -- call right after acid_play_note on the same voice. Unused
 * slots (count < 4) can be any valid 1..88 value; only the first count
 * slots are ever read. See AUDIO_CMD_TRIGGER_ARP's own comment. */
static mrb_value
acid_trigger_arp( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, note0, note1, note2, note3, count, rate_ms;
    mrb_get_args( mrb, "iiiiiii", &voice, &note0, &note1, &note2, &note3,
                  &count, &rate_ms );
    int notes[ 4 ];
    notes[ 0 ] = ( int ) note0;
    notes[ 1 ] = ( int ) note1;
    notes[ 2 ] = ( int ) note2;
    notes[ 3 ] = ( int ) note3;
    kernel_audio_enqueue_trigger_arp( ( int ) voice, notes, ( int ) count, ( int ) rate_ms );
    return mrb_nil_value();
}

static mrb_value
acid_stop_note( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice;
    mrb_get_args( mrb, "i", &voice );
    kernel_audio_enqueue_note_off( ( void * ) xTaskGetCurrentTaskHandle(), ( int ) voice );
    return mrb_nil_value();
}

/* Sets a voice's oscillator shape -- waveform (0=pulse, 1=saw,
 * 2=triangle, 3=noise) and duty cycle (0..100, audible only on a pulse
 * wave). The engine has supported both since the audio phase shipped;
 * neither was ever reachable from Ruby until now -- found and fixed
 * during a dead-code audit (synth_set_voice_waveform/synth_set_duty had
 * zero callers anywhere in the tree). */
static mrb_value
acid_configure_osc( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, waveform, duty_percent;
    mrb_get_args( mrb, "iii", &voice, &waveform, &duty_percent );
    kernel_audio_enqueue_configure_osc( ( int ) voice, ( int ) waveform, ( int ) duty_percent );
    return mrb_nil_value();
}

/* Pairs `voice` with `partner` for ring modulation (audible only on a
 * WAVE_TRIANGLE voice -- see synth.h's own comment on why), or clears it
 * if partner is negative. Same "engine-supported but never exposed"
 * history as acid_configure_osc above. */
static mrb_value
acid_set_ring_partner( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, partner;
    mrb_get_args( mrb, "ii", &voice, &partner );
    kernel_audio_enqueue_set_ring_partner( ( int ) voice, ( int ) partner );
    return mrb_nil_value();
}

/* System-wide output gain (0..100) -- for a Config app's volume control.
 * See kernel_audio_set_master_volume's own doc comment. */
static mrb_value
acid_set_volume( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int percent;
    mrb_get_args( mrb, "i", &percent );
    kernel_audio_set_master_volume( ( int ) percent );
    return mrb_nil_value();
}

static mrb_value
acid_get_volume( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( kernel_audio_get_master_volume() );
}

void
acid_audio_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_play_note",
                                 acid_play_note, MRB_ARGS_REQ( 3 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_stop_note",
                                 acid_stop_note, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_configure_voice",
                                 acid_configure_voice, MRB_ARGS_REQ( 6 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_configure_filter",
                                 acid_configure_filter, MRB_ARGS_REQ( 3 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_trigger_arp",
                                 acid_trigger_arp, MRB_ARGS_REQ( 7 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_configure_osc",
                                 acid_configure_osc, MRB_ARGS_REQ( 3 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_set_ring_partner",
                                 acid_set_ring_partner, MRB_ARGS_REQ( 2 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_set_volume",
                                 acid_set_volume, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_get_volume",
                                 acid_get_volume, MRB_ARGS_NONE() );
}
