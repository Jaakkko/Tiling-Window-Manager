cmake_minimum_required(VERSION 3.15)
project(WM C)

set(CMAKE_C_STANDARD 99)

include_directories(/usr/include/freetype2)

add_executable(WM
        main.c
        events.c
        input.c
        api.c
        instance.c
        logger.c
        tree.c
        bar/bar.c)

target_link_libraries(WM X11 Xrender Xft)
