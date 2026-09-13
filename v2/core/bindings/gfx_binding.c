#include "gfx_binding.h"

extern void sim_gfx_fill_rect( int x, int y, int w, int h, unsigned int color );

static mrb_value
acid_fill_rect( mrb_state * mrb, mrb_value self )
{
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );
    sim_gfx_fill_rect( ( int ) x, ( int ) y, ( int ) w, ( int ) h, ( unsigned int ) color );
    return mrb_nil_value();
}

void
acid_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_rect",
                                 acid_fill_rect, MRB_ARGS_REQ( 5 ) );
}
