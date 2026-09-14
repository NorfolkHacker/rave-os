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
}
