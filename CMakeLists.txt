cmake_minimum_required(VERSION 3.20)

project(socket_chat VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(socket_chat src/main.c)

if(MSVC)
    target_compile_options(socket_chat PRIVATE /W4)
else()
    target_compile_options(socket_chat PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(raylib 6.0 QUIET)

if(raylib_FOUND)
    target_link_libraries(socket_chat PRIVATE raylib)
else()
    include(FetchContent)

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )

    FetchContent_MakeAvailable(raylib)
    target_link_libraries(socket_chat PRIVATE raylib)
endif()
