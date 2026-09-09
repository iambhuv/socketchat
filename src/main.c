#include <raylib.h>

#include "client/connection_screen.h"
#include "core/dependencies.h"
#include "ui/ui_font.h"

#include <stdio.h>

int main(void)
{
    const int initialWindowWidth = 960;
    const int initialWindowHeight = 600;
    ConnectionScreen connection_screen;

    if (!dependencies_initialize())
    {
        fprintf(stderr, "Could not initialize libsodium or SQLite.\n");
        return 1;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(initialWindowWidth, initialWindowHeight, "Socket Chat");

    if (!IsWindowReady())
    {
        fprintf(stderr, "Could not create the Raylib window.\n");
        dependencies_shutdown();
        return 1;
    }

    SetWindowMinSize(640, 540);
    SetTargetFPS(60);
    ui_font_load();
    connection_screen_init(&connection_screen);

    while (!WindowShouldClose())
    {
        bool connect_requested;

        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        BeginDrawing();
        connect_requested = connection_screen_draw(&connection_screen);
        EndDrawing();

        /* The TCP connection will use this event in the next project step. */
        (void)connect_requested;
    }

    ui_font_unload();
    CloseWindow();
    dependencies_shutdown();

    return 0;
}
