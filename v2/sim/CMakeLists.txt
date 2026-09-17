cmake_minimum_required(VERSION 3.15)
project(acidos_v2_sim C CXX)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../components/freertos-kernel)
set(LOVYANGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/lovyangfx)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(FREERTOS_HEAP "3" CACHE STRING "" FORCE)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)

add_subdirectory(${FREERTOS_KERNEL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/FreeRTOS-Kernel)

add_definitions(-DLGFX_SDL)

file(GLOB LGFX_SOURCES CONFIGURE_DEPENDS
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/efont/*.c
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/IPA/*.c
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/lvgl/*.c
    ${LOVYANGFX_DIR}/src/lgfx/utility/*.c
    ${LOVYANGFX_DIR}/src/lgfx/v1/*.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/lv_font/*.c
    ${LOVYANGFX_DIR}/src/lgfx/v1/misc/*.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_Device.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp
)

set(MRUBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/mruby)
set(LIBMRUBY_FILE ${MRUBY_DIR}/build/host/lib/libmruby.a)

add_custom_command(
    OUTPUT ${LIBMRUBY_FILE}
    COMMAND rake
    WORKING_DIRECTORY ${MRUBY_DIR}
    VERBATIM
)
add_custom_target(mruby_build DEPENDS ${LIBMRUBY_FILE})

add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    hal_input_sim.cpp
    hal_audio_sim.c
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/event_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/chrome_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/audio_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/window_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/audio/synth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_window.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_spawn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_router.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_audio.c
    ${LGFX_SOURCES}
)
add_dependencies(acidos_sim mruby_build)

# MRB_USE_DEBUG_HOOK must match the ABI of the prebuilt libmruby.a linked below.
# The mruby build (v2/components/mruby, built via `rake` using the stock
# build_config/default.rb -> mrbgems/default.gembox) pulls in the
# mruby-bin-debugger gem to produce the mrdb binary; that gem's mrbgem.rake
# does `spec.build.defines << "MRB_USE_DEBUG_HOOK"`, and mruby's gem build
# system applies a gem's build.defines to the WHOLE library compilation, not
# just that gem's own sources -- so every core .c file in libmruby.a (state.c,
# gc.c, class.c, ...) was actually compiled with this macro defined.
# MRB_USE_DEBUG_HOOK guards two extra function-pointer fields in struct
# mrb_state (mruby.h, code_fetch_hook/debug_op_hook), which shifts every
# later field -- including rom_mt, root_shape, and ud -- by 16 bytes.
# Without this define here, this target's own view of struct mrb_state
# (vm_host.c, gfx_binding.c, event_binding.c -- all of which read/write
# mrb->ud) disagrees with libmruby.a's real layout by exactly that 16 bytes,
# so mrb->ud = &ctx silently aliases and clobbers the real mrb->rom_mt field,
# corrupting mrb_close()'s ROM-wrapper cleanup list with a stack address and
# crashing on free() at the next mrb_close(). Keep this in lock-step with
# whatever build-wide defines the mruby gembox in use actually applies.
target_compile_definitions(acidos_sim PRIVATE MRB_USE_DEBUG_HOOK)

target_include_directories(acidos_sim PRIVATE
    ${LOVYANGFX_DIR}/src
    ${MRUBY_DIR}/build/host/include
    ${MRUBY_DIR}/include
)
target_compile_features(acidos_sim PUBLIC cxx_std_17)

find_package(SDL2 REQUIRED)
target_include_directories(acidos_sim PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(acidos_sim PRIVATE
    freertos_kernel freertos_config pthread ${SDL2_LIBRARIES} ${LIBMRUBY_FILE} m
)
