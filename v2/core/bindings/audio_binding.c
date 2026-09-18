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

static mrb_value
acid_stop_note( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice;
    mrb_get_args( mrb, "i", &voice );
    kernel_audio_enqueue_note_off( ( void * ) xTaskGetCurrentTaskHandle(), ( int ) voice );
    return mrb_nil_value();
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
}
