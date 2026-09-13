#include <stdlib.h>

#include "event_binding.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_event.h"
#include "../hal/hal_input.h"

#include "mruby/array.h"
#include "queue.h"

static mrb_value
acid_poll_event( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int timeout_ms;
    mrb_get_args( mrb, "i", &timeout_ms );

    if( hal_input_should_quit() )
    {
        exit( 0 );
    }

    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    struct kernel_event ev;
    if( xQueueReceive( ctx->queue, &ev, pdMS_TO_TICKS( timeout_ms ) ) != pdTRUE )
    {
        return mrb_nil_value();
    }

    if( ev.type == KERNEL_EVENT_CLOSE )
    {
        return mrb_symbol_value( mrb_intern_cstr( mrb, "close" ) );
    }

    mrb_value values[ 3 ];
    values[ 0 ] = mrb_fixnum_value( ev.x );
    values[ 1 ] = mrb_fixnum_value( ev.y );
    values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
    return mrb_ary_new_from_values( mrb, 3, values );
}

void
acid_event_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_poll_event",
                                 acid_poll_event, MRB_ARGS_REQ( 1 ) );
}
