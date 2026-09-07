#include <raylib.h>

#include "core/dependencies.h"

#include <stdio.h>

int main(void)
{
    const int initialWindowWidth = 960;
    const int initialWindowHeight = 600;
    const Color backgroundColor = { 247, 245, 252, 255 };

    if (!dependencies_initialize())
    {
        fprintf(stderr, "Could not initialize libsodium or SQLite.\n");
        return 1;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(initialWindowWidth, initialWindowHeight, "Socket Chat");
    SetWindowMinSize(640, 400);
    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(backgroundColor);
        EndDrawing();
    }

    CloseWindow();
    dependencies_shutdown();

    return 0;
}
