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
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${LGFX_SOURCES}
)
add_dependencies(acidos_sim mruby_build)

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
