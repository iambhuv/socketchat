cmake_minimum_required(VERSION 3.20)

project(socket_chat VERSION 0.1.0 LANGUAGES C)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(socket_chat_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# vcpkg supplies CMake targets for both libraries. Other package managers
# expose normal headers and library files, which the fallback finders locate.
find_package(unofficial-sodium CONFIG QUIET)

if(TARGET unofficial-sodium::sodium)
    set(SOCKET_CHAT_SODIUM_TARGET unofficial-sodium::sodium)
else()
    find_package(sodium REQUIRED)
    set(SOCKET_CHAT_SODIUM_TARGET sodium::sodium)
endif()

find_package(unofficial-sqlite3 CONFIG QUIET)

if(TARGET unofficial::sqlite3::sqlite3)
    set(SOCKET_CHAT_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
    find_package(SQLite3 REQUIRED)

    if(TARGET SQLite3::SQLite3)
        set(SOCKET_CHAT_SQLITE_TARGET SQLite3::SQLite3)
    else()
        set(SOCKET_CHAT_SQLITE_TARGET SQLite::SQLite3)
    endif()
endif()

add_library(chat_dependencies STATIC
    src/core/dependencies.c
)
target_include_directories(chat_dependencies PUBLIC src)
target_link_libraries(chat_dependencies PUBLIC
    ${SOCKET_CHAT_SODIUM_TARGET}
    ${SOCKET_CHAT_SQLITE_TARGET}
)
socket_chat_enable_warnings(chat_dependencies)

add_library(chat_client_core STATIC
    src/client/client_connection.c
    src/client/client_database.c
    src/client/client_identity.c
)
target_include_directories(chat_client_core PUBLIC src)
target_link_libraries(chat_client_core PUBLIC
    chat_dependencies
    chat_net
    chat_protocol
)
socket_chat_enable_warnings(chat_client_core)

add_library(chat_protocol STATIC
    src/protocol/protocol.c
)
target_include_directories(chat_protocol PUBLIC src)
socket_chat_enable_warnings(chat_protocol)

add_library(chat_net STATIC
    src/net/net.c
)
target_include_directories(chat_net PUBLIC src)

if(WIN32)
    target_link_libraries(chat_net PUBLIC ws2_32)
endif()

socket_chat_enable_warnings(chat_net)

add_library(chat_server STATIC
    src/server/server.c
    src/server/server_session.c
)
target_include_directories(chat_server PUBLIC src)
target_link_libraries(chat_server PUBLIC chat_net chat_protocol)
socket_chat_enable_warnings(chat_server)

add_library(chat_ui STATIC
    src/ui/ui_animation.c
    src/ui/ui_font.c
    src/ui/ui_theme.c
    src/ui/ui_widgets.c
)
target_include_directories(chat_ui PUBLIC src)
socket_chat_enable_warnings(chat_ui)

add_library(chat_client_ui STATIC
    src/client/onboarding_screen.c
)
target_include_directories(chat_client_ui PUBLIC src)
target_link_libraries(chat_client_ui PUBLIC chat_ui chat_protocol)
socket_chat_enable_warnings(chat_client_ui)

add_executable(socket_chat src/main.c)
target_link_libraries(socket_chat PRIVATE
    chat_client_core
    chat_client_ui
    chat_dependencies
    chat_protocol
)
socket_chat_enable_warnings(socket_chat)

add_custom_command(TARGET socket_chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:socket_chat>/assets"
)

add_executable(socket_chat_server src/server/server_main.c)
target_link_libraries(socket_chat_server PRIVATE chat_server)
socket_chat_enable_warnings(socket_chat_server)

if(BUILD_TESTING)
    add_executable(dependencies_tests tests/dependencies_test.c)
    target_link_libraries(dependencies_tests PRIVATE chat_dependencies)
    socket_chat_enable_warnings(dependencies_tests)
    add_test(NAME dependencies_tests COMMAND dependencies_tests)

    add_executable(client_identity_tests tests/client_identity_test.c)
    target_link_libraries(client_identity_tests PRIVATE chat_client_core)
    socket_chat_enable_warnings(client_identity_tests)
    add_test(NAME client_identity_tests COMMAND client_identity_tests)

    add_executable(client_connection_tests tests/client_connection_test.c)
    target_link_libraries(client_connection_tests PRIVATE chat_client_core)
    socket_chat_enable_warnings(client_connection_tests)
    add_test(NAME client_connection_tests COMMAND client_connection_tests)

    add_executable(protocol_tests tests/protocol_test.c)
    target_link_libraries(protocol_tests PRIVATE chat_protocol)
    socket_chat_enable_warnings(protocol_tests)
    add_test(NAME protocol_tests COMMAND protocol_tests)

    add_executable(net_tests tests/net_test.c)
    target_link_libraries(net_tests PRIVATE chat_net)
    socket_chat_enable_warnings(net_tests)
    add_test(NAME net_tests COMMAND net_tests)

    add_executable(server_session_tests tests/server_session_test.c)
    target_link_libraries(server_session_tests PRIVATE chat_server)
    socket_chat_enable_warnings(server_session_tests)
    add_test(NAME server_session_tests COMMAND server_session_tests)
endif()

find_package(raylib 6.0 QUIET)

if(raylib_FOUND)
    target_link_libraries(chat_ui PUBLIC raylib)
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
    target_link_libraries(chat_ui PUBLIC raylib)
    target_link_libraries(socket_chat PRIVATE raylib)
endif()
