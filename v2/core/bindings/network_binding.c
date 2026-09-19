#include "network_binding.h"
#include "../hal/hal_network.h"

#include "mruby/array.h"

#define HOSTNAME_BUF_LEN 64
#define IP_BUF_LEN 32

/* [hostname, ip, connected] for a Network app -- see hal_network_get_info's
 * own doc comment on what "connected" actually means here (an address was
 * found, not a live reachability check). */
static mrb_value
acid_network_info( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    char hostname[ HOSTNAME_BUF_LEN ];
    char ip[ IP_BUF_LEN ];
    int connected = hal_network_get_info( hostname, HOSTNAME_BUF_LEN, ip, IP_BUF_LEN );

    mrb_value values[ 3 ];
    values[ 0 ] = mrb_str_new_cstr( mrb, hostname );
    values[ 1 ] = mrb_str_new_cstr( mrb, ip );
    values[ 2 ] = mrb_bool_value( connected );
    return mrb_ary_new_from_values( mrb, 3, values );
}

void
acid_network_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_network_info",
                                 acid_network_info, MRB_ARGS_NONE() );
}
