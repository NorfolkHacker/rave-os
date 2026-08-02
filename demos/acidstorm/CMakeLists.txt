cmake_minimum_required(VERSION 3.16)
project(acidstorm C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(acidstorm
    src/main.c
    src/gfx.c
    src/font.c
    src/game.c
)

target_include_directories(acidstorm PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(acidstorm PRIVATE ${SDL2_LIBRARIES} m)
target_compile_options(acidstorm PRIVATE ${SDL2_CFLAGS_OTHER} -O2 -Wall -Wextra)
