#include <stdlib.h>
#include <unistd.h>

#include "event_binding.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_event.h"
#include "../hal/hal_input.h"

#include "mruby/array.h"
#include "queue.h"
#include "semphr.h"

static mrb_value
acid_poll_event( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int timeout_ms;
    mrb_get_args( mrb, "i", &timeout_ms );

    if( hal_input_should_quit() )
    {
        /* _exit(0), not exit(0): exit() runs C++ static-destructor teardown,
         * which for the sim would invoke lgfx::v1::Panel_sdl::~Panel_sdl()'s
         * pre-existing (vendored, out-of-scope) use-after-free. _exit skips
         * that teardown entirely while still exiting cleanly with status 0. */
        _exit( 0 );
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

    if( ev.type == KERNEL_EVENT_MOVED )
    {
        ctx->window_x = ev.x;
        ctx->window_y = ev.y;
        return mrb_symbol_value( mrb_intern_cstr( mrb, "moved" ) );
    }

    if( ev.type == KERNEL_EVENT_KEY )
    {
        mrb_value values[ 3 ];
        values[ 0 ] = mrb_symbol_value( mrb_intern_cstr( mrb, "key" ) );
        values[ 1 ] = mrb_fixnum_value( ev.x );
        values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
        return mrb_ary_new_from_values( mrb, 3, values );
    }

    mrb_value values[ 3 ];
    values[ 0 ] = mrb_fixnum_value( ev.x );
    values[ 1 ] = mrb_fixnum_value( ev.y );
    values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
    return mrb_ary_new_from_values( mrb, 3, values );
}

/* Called by AcidApp/AcidGame right after finishing a :moved-triggered
 * redraw -- lets kernel_router_repaint_all wait for this specific window's
 * drawing to actually land before it moves on to the next one, so
 * back-to-front z-order is real completion order, not just send order
 * (see kernel_window.h's redraw_done_sem comment). */
static mrb_value
acid_notify_redraw_done( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    xSemaphoreGive( ( SemaphoreHandle_t ) ctx->redraw_done_sem );
    return mrb_nil_value();
}

void
acid_event_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_poll_event",
                                 acid_poll_event, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_notify_redraw_done",
                                 acid_notify_redraw_done, MRB_ARGS_NONE() );
}
