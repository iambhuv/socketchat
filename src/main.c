#include <raylib.h>

int main(void)
{
    const int initialWindowWidth = 960;
    const int initialWindowHeight = 600;
    const Color backgroundColor = { 247, 245, 252, 255 };

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

    return 0;
}
