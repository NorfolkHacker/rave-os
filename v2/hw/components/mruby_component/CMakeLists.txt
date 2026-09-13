set(MRUBY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/mruby")
# Name of the MRuby::CrossBuild declared in build_config.rb. Shared between
# LIBMRUBY_FILE and the generated-header INCLUDE_DIRS entry below so the two
# can't drift apart.
set(MRUBY_CROSS_BUILD_NAME "esp32p4")
set(LIBMRUBY_FILE "${MRUBY_DIR}/build/${MRUBY_CROSS_BUILD_NAME}/lib/libmruby.a")
set(MRUBY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb")

idf_component_register(
    INCLUDE_DIRS
        # mruby.h unconditionally includes mruby/presym/id.h, which rake
        # generates per-build under the build directory (it does not exist
        # in the static vendored tree). Same fix as v2/sim/CMakeLists.txt.
        ${MRUBY_DIR}/build/${MRUBY_CROSS_BUILD_NAME}/include
        ${MRUBY_DIR}/include
)

add_custom_command(
    OUTPUT ${LIBMRUBY_FILE}
    COMMAND ${CMAKE_COMMAND} -E env
        "MRUBY_CONFIG=${MRUBY_CONFIG}"
        "CC=${CMAKE_C_COMPILER}"
        "LD=${CMAKE_C_COMPILER}"
        "AR=${CMAKE_AR}"
        "COMPONENT_INCLUDES=$<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>"
        rake
    WORKING_DIRECTORY ${MRUBY_DIR}
    VERBATIM
)

add_custom_target(mruby DEPENDS ${LIBMRUBY_FILE})
add_dependencies(${COMPONENT_LIB} mruby)

target_link_libraries(${COMPONENT_LIB} INTERFACE ${LIBMRUBY_FILE})
