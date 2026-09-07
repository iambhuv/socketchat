cmake_minimum_required(VERSION 3.20)

project(socket_chat VERSION 0.1.0 LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(chat_protocol STATIC
    src/protocol/protocol.c
)
target_include_directories(chat_protocol PUBLIC src)

add_executable(socket_chat src/main.c)
target_link_libraries(socket_chat PRIVATE chat_protocol)

if(MSVC)
    target_compile_options(socket_chat PRIVATE /W4)
    target_compile_options(chat_protocol PRIVATE /W4)
else()
    target_compile_options(socket_chat PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(chat_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(protocol_tests tests/protocol_test.c)
    target_link_libraries(protocol_tests PRIVATE chat_protocol)

    if(MSVC)
        target_compile_options(protocol_tests PRIVATE /W4)
    else()
        target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME protocol_tests COMMAND protocol_tests)
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
