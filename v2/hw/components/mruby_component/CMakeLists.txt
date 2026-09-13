set(MRUBY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/mruby")
set(LIBMRUBY_FILE "${MRUBY_DIR}/build/esp32p4/lib/libmruby.a")
set(MRUBY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb")

idf_component_register(
    INCLUDE_DIRS ${MRUBY_DIR}/include
)

add_custom_command(
    OUTPUT ${LIBMRUBY_FILE}
    COMMAND ${CMAKE_COMMAND} -E env
        "MRUBY_CONFIG=${MRUBY_CONFIG}"
        "COMPONENT_INCLUDES=$<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>"
        rake
    WORKING_DIRECTORY ${MRUBY_DIR}
    VERBATIM
)

add_custom_target(mruby DEPENDS ${LIBMRUBY_FILE})
add_dependencies(${COMPONENT_LIB} mruby)

target_link_libraries(${COMPONENT_LIB} INTERFACE ${LIBMRUBY_FILE})
