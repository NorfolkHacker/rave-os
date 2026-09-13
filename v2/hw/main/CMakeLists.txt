idf_component_register(
    SRCS
        app_main.c
        hal_display_hw.c
        hal_input_hw.c
        ../../core/vm_host/vm_host.c
        ../../core/bindings/gfx_binding.c
        ../../core/gfx/gfx.c
    INCLUDE_DIRS
        ../../core/vm_host
        ../../core/hal
        ../../core/gfx
        ../../core/bindings
    REQUIRES mruby_component
)
